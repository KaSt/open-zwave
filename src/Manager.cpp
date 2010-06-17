//-----------------------------------------------------------------------------
//
//	Manager.h
//
//	Communicates with a Z-Wave network
//
//	Copyright (c) 2010 Mal Lansell <openzwave@lansell.org>
//
//	SOFTWARE NOTICE AND LICENSE
//
//	This file is part of OpenZWave.
//
//	OpenZWave is free software: you can redistribute it and/or modify
//	it under the terms of the GNU Lesser General Public License as published
//	by the Free Software Foundation, either version 3 of the License,
//	or (at your option) any later version.
//
//	OpenZWave is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU Lesser General Public License for more details.
//
//	You should have received a copy of the GNU Lesser General Public License
//	along with OpenZWave.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------------

#include "Defs.h"
#include "Manager.h"
#include "Driver.h"
#include "Node.h"
#include "Notification.h"

#include "Event.h"
#include "Log.h"

#include "CommandClasses.h"
#include "CommandClass.h"

#include "ValueID.h"
#include "ValueBool.h"
#include "ValueByte.h"
#include "ValueDecimal.h"
#include "ValueInt.h"
#include "ValueList.h"
#include "ValueShort.h"
#include "ValueString.h"

using namespace OpenZWave;

Manager* Manager::s_instance = NULL;


//-----------------------------------------------------------------------------
//	Construction
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//	<Manager::Create>
//	Static creation of the singleton
//-----------------------------------------------------------------------------
Manager* Manager::Create
(
	string const& _configPath,
	string const& _userPath
)
{
	if( NULL == s_instance )
	{
		s_instance = new Manager( _configPath, _userPath );
	}

	return s_instance;
}

//-----------------------------------------------------------------------------
//	<Manager::Destroy>
//	Static method to destroy the singleton.
//-----------------------------------------------------------------------------
void Manager::Destroy
(
)
{
	delete s_instance;
	s_instance = NULL;
}

//-----------------------------------------------------------------------------
// <Manager::Manager>
// Constructor
//-----------------------------------------------------------------------------
Manager::Manager
( 
	string const& _configPath,
	string const& _userPath
):
	m_configPath( _configPath ),
	m_userPath( _userPath ),
	m_exitEvent( new Event() )
{
	// Create the log file
	string logFilename = _userPath + string( "OZW_Log.txt" );
	Log::Create( logFilename );

	CommandClasses::RegisterCommandClasses();

	// Ensure the singleton instance is set
	s_instance = this;
}

//-----------------------------------------------------------------------------
// <Manager::Manager>
// Destructor
//-----------------------------------------------------------------------------
Manager::~Manager
(
)
{
	// Clear the pending list
	list<Driver*>::iterator pit = m_pendingDrivers.begin();
	while( !m_pendingDrivers.empty() )
	{
		delete *pit;
		m_pendingDrivers.erase( pit );
		pit = m_pendingDrivers.begin();
	}

	// Clear the ready map
	map<uint32,Driver*>::iterator rit = m_readyDrivers.begin();
	while( !m_readyDrivers.empty() )
	{
		delete rit->second;
		m_readyDrivers.erase( rit );
		rit = m_readyDrivers.begin();
	}
	delete m_exitEvent;
	Log::Destroy();
}

//-----------------------------------------------------------------------------
// Configuration
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// <Manager::WriteConfig>
// Save the configuration of a driver to a file
//-----------------------------------------------------------------------------
void Manager::WriteConfig
(
	uint32 const _homeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		driver->WriteConfig();
		Log::Write( "Manager::WriteConfig completed for driver with home ID of 0x%.8x", _homeId );
	}
	else
	{
		Log::Write( "Manager::WriteConfig failed - _homeId %d not found", _homeId );
	}
}

//-----------------------------------------------------------------------------
//	Drivers
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// <Manager::AddDriver>
// Add a new Z-Wave PC Interface
//-----------------------------------------------------------------------------
bool Manager::AddDriver
(
	string const& _serialPortName
)
{
	// Make sure we don't already have a driver for this serial port
	
	// Search the pending list
	for( list<Driver*>::iterator pit = m_pendingDrivers.begin(); pit != m_pendingDrivers.end(); ++pit )
	{
		if( _serialPortName == (*pit)->GetSerialPortName() )
		{
			Log::Write( "Cannot add driver for serial port %s - driver already exists", _serialPortName.c_str() );
			return false;
		}
	}

	// Search the ready map
	for( map<uint32,Driver*>::iterator rit = m_readyDrivers.begin(); rit != m_readyDrivers.end(); ++rit )
	{
		if( _serialPortName == rit->second->GetSerialPortName() )
		{
			Log::Write( "Cannot add driver for serial port %s - driver already exists", _serialPortName.c_str() );
			return false;
		}
	}

	Driver* driver = new Driver( _serialPortName );
	m_pendingDrivers.push_back( driver );
	driver->Start();

	Log::Write( "Added driver for serial port %s", _serialPortName.c_str() );
	return true;
}

//-----------------------------------------------------------------------------
// <Manager::RemoveDriver>
// Remove a Z-Wave PC Interface
//-----------------------------------------------------------------------------
bool Manager::RemoveDriver
(
	string const& _serialPortName
)
{
	// Search the pending list
	for( list<Driver*>::iterator pit = m_pendingDrivers.begin(); pit != m_pendingDrivers.end(); ++pit )
	{
		if( _serialPortName == (*pit)->GetSerialPortName() )
		{
			delete *pit;
			m_pendingDrivers.erase( pit );
			Log::Write( "Driver for serial port %s removed", _serialPortName.c_str() );
			return true;
		}
	}

	// Search the ready map
	for( map<uint32,Driver*>::iterator rit = m_readyDrivers.begin(); rit != m_readyDrivers.end(); ++rit )
	{
		if( _serialPortName == rit->second->GetSerialPortName() )
		{
			delete rit->second;
			m_readyDrivers.erase( rit );
			Log::Write( "Driver for serial port %s removed", _serialPortName.c_str() );
			return true;
		}
	}

	Log::Write( "Failed to remove driver for serial port %s", _serialPortName.c_str() );
	return false;
}

//-----------------------------------------------------------------------------
// <Manager::GetDriver>
// Get a pointer to the driver for a Z-Wave PC Interface
//-----------------------------------------------------------------------------
Driver* Manager::GetDriver
(
	uint32 const _homeId
)
{
	map<uint32,Driver*>::iterator it = m_readyDrivers.find( _homeId );
	if( it != m_readyDrivers.end() )
	{
		return it->second;
	}

	assert(0);
	Log::Write( "Manager::GetDriver failed - Home ID 0x%.8x is unknown", _homeId );
	return NULL;
}

//-----------------------------------------------------------------------------
// <Manager::SetDriverReady>
// Move a driver from pending to ready, and notify any watchers
//-----------------------------------------------------------------------------
void Manager::SetDriverReady
(
	Driver* _driver
)
{
	// Search the pending list
	bool found = false;
	for( list<Driver*>::iterator it = m_pendingDrivers.begin(); it != m_pendingDrivers.end(); ++it )
	{
		if( (*it) == _driver )
		{
			// Remove the driver from the pending list
			m_pendingDrivers.erase( it );
			found = true;
			break;
		}
	}

	if( found )
	{
		Log::Write( "Driver with Home ID of 0x%.8x is now ready.", _driver->GetHomeId() );

		// Add the driver to the ready map
		m_readyDrivers[_driver->GetHomeId()] = _driver;

		// Notify the watchers
		Notification notification( Notification::Type_DriverReady );
		notification.SetHomeAndNodeIds( _driver->GetHomeId(), 0 );
		Manager::Get()->NotifyWatchers( &notification ); 
	}
}

//-----------------------------------------------------------------------------
// <Manager::IsSlave>
// 
//-----------------------------------------------------------------------------
bool Manager::IsSlave
(
	uint32 const _homeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		return driver->IsSlave();
	}

	Log::Write( "IsSlave() failed - _homeId %d not found", _homeId );
	return false;
}

//-----------------------------------------------------------------------------
// <Manager::HasTimerSupport>
// 
//-----------------------------------------------------------------------------
bool Manager::HasTimerSupport
(
	uint32 const _homeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		return driver->HasTimerSupport();
	}

	Log::Write( "HasTimerSupport() failed - _homeId %d not found", _homeId );
	return false;
}

//-----------------------------------------------------------------------------
// <Manager::IsPrimaryController>
// 
//-----------------------------------------------------------------------------
bool Manager::IsPrimaryController
(
	uint32 const _homeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		return driver->IsPrimaryController();
	}

	Log::Write( "IsPrimaryController() failed - _homeId %d not found", _homeId );
	return false;
}

//-----------------------------------------------------------------------------
// <Manager::IsStaticUpdateController>
// 
//-----------------------------------------------------------------------------
bool Manager::IsStaticUpdateController
(
	uint32 const _homeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		return driver->IsStaticUpdateController();
	}

	Log::Write( "IsStaticUpdateController() failed - _homeId %d not found", _homeId );
	return false;
}

//-----------------------------------------------------------------------------
//	Polling Z-Wave values
//-----------------------------------------------------------------------------
				  		
//-----------------------------------------------------------------------------
// <Manager::SetPollInterval>
// Set the polling interval on all drivers
//-----------------------------------------------------------------------------
void Manager::SetPollInterval
(
	int32 _seconds
)
{
	for( list<Driver*>::iterator pit = m_pendingDrivers.begin(); pit != m_pendingDrivers.end(); ++pit )
	{
		(*pit)->SetPollInterval( _seconds );
	}

	for( map<uint32,Driver*>::iterator rit = m_readyDrivers.begin(); rit != m_readyDrivers.end(); ++rit )
	{
		rit->second->SetPollInterval( _seconds );
	}
}

//-----------------------------------------------------------------------------
// <Manager::EnablePoll>
// Enable polling of a value
//-----------------------------------------------------------------------------
bool Manager::EnablePoll
( 
	uint32 const _homeId,
	uint8 const _nodeId 
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		return( driver->EnablePoll( _nodeId ) );
	}

	Log::Write( "EnablePoll failed - Driver with Home ID 0x%.8x is not available", _homeId );
	return false;
}

//-----------------------------------------------------------------------------
// <Manager::DisablePoll>
// Disable polling of a value
//-----------------------------------------------------------------------------
bool Manager::DisablePoll
( 
	uint32 const _homeId,
	uint8 const _nodeId 
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		return( driver->DisablePoll( _nodeId ) );
	}

	Log::Write( "DisablePoll failed - Driver with Home ID 0x%.8x is not available", _homeId );
	return false;
}

//-----------------------------------------------------------------------------
//	Retrieving Node information
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// <Manager::RefreshNodeInfo>
// Fetch the data for a node from the Z-Wave network
//-----------------------------------------------------------------------------
bool Manager::RefreshNodeInfo
(
	uint32 const _homeId,
	uint8 const _nodeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		// Cause the node's data to be obtained from the Z-Wave network
		// in the same way as if it had just been added.
		driver->AddInfoRequest( _nodeId );
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// <Manager::RequestState>
// Fetch the command class data for a node from the Z-Wave network
//-----------------------------------------------------------------------------
void Manager::RequestState
(
	uint32 const _homeId,
	uint8 const _nodeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		// Retreive the Node's Session and/or Dynamic data
		driver->RequestState( _nodeId, CommandClass::RequestFlag_Session | CommandClass::RequestFlag_Dynamic );
	}
}

//-----------------------------------------------------------------------------
// <Manager::GetBasicLabel>
// Get the basiclabel value with the specified ID
//-----------------------------------------------------------------------------
string const& Manager::GetBasicLabel
(
	uint32 const _homeId,
	uint8 const _nodeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		return driver->GetBasicLabel( _nodeId );
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// <Manager::GetGenericLabel>
// Get the basiclabel value with the specified ID
//-----------------------------------------------------------------------------
string const& Manager::GetGenericLabel
(
	uint32 const _homeId,
	uint8 const _nodeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		return driver->GetGenericLabel( _nodeId );
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// <Manager::GetManufacturerName>
// Get the manufacturer name value with the specified ID
//-----------------------------------------------------------------------------
string const& Manager::GetManufacturerName
(
	uint32 const _homeId,
	uint8 const _nodeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		return driver->GetManufacturerName( _nodeId );
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// <Manager::GetProductName>
// Get the product name value with the specified ID
//-----------------------------------------------------------------------------
string const& Manager::GetProductName
(
	uint32 const _homeId,
	uint8 const _nodeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		return driver->GetProductName( _nodeId );
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// <Manager::GetNodeName>
// Get the node name value with the specified ID
//-----------------------------------------------------------------------------
string const& Manager::GetNodeName
(
	uint32 const _homeId,
	uint8 const _nodeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		return driver->GetNodeName( _nodeId );
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// <Manager::GetLocation>
// Get the lcation value with the specified ID
//-----------------------------------------------------------------------------
string const& Manager::GetLocation
(
	uint32 const _homeId,
	uint8 const _nodeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		return driver->GetLocation( _nodeId );
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// <Manager::SetManufacturerName>
// Set the manufacturer name value with the specified ID
//-----------------------------------------------------------------------------
void Manager::SetManufacturerName
(
	uint32 const _homeId,
	uint8 const _nodeId,
	string const& _manufacturerName
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		driver->SetManufacturerName( _nodeId, _manufacturerName );
	}
}

//-----------------------------------------------------------------------------
// <Manager::SetProductName>
// Set the manufacturer name value with the specified ID
//-----------------------------------------------------------------------------
void Manager::SetProductName
(
	uint32 const _homeId,
	uint8 const _nodeId,
	string const& _productName
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		driver->SetProductName( _nodeId, _productName );
	}
}

//-----------------------------------------------------------------------------
// <Manager::SetNodeName>
// Set the node name value with the specified ID
//-----------------------------------------------------------------------------
void Manager::SetNodeName
(
	uint32 const _homeId,
	uint8 const _nodeId,
	string const& _nodeName
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		driver->SetNodeName( _nodeId, _nodeName );
	}
}

//-----------------------------------------------------------------------------
// <Manager::SetLocation>
// Set the location value with the specified ID
//-----------------------------------------------------------------------------
void Manager::SetLocation
(
	uint32 const _homeId,
	uint8 const _nodeId,
	string const& _location

)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		driver->SetLocation( _nodeId, _location );
	}
}

//-----------------------------------------------------------------------------
// <Manager::GetManufacturerId>
// Get the manufacturer Id value with the specified ID
//-----------------------------------------------------------------------------
string const& Manager::GetManufacturerId
(
	uint32 const _homeId,
	uint8 const _nodeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		return driver->GetManufacturerId( _nodeId );
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// <Manager::GetProductType>
// Get the product type value with the specified ID
//-----------------------------------------------------------------------------
string const& Manager::GetProductType
(
	uint32 const _homeId,
	uint8 const _nodeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		return driver->GetProductType( _nodeId );
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// <Manager::GetProductId>
// Get the product Id value with the specified ID
//-----------------------------------------------------------------------------
string const& Manager::GetProductId
(
	uint32 const _homeId,
	uint8 const _nodeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		return driver->GetProductId( _nodeId );
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// <Manager::GetValueBool>
// Get the bool value object with the specified ID
//-----------------------------------------------------------------------------
ValueBool* Manager::GetValueBool
(
	ValueID const& _id
)
{
	if( Driver* driver = GetDriver( _id.GetHomeId() ) )
	{
		return driver->GetValueBool( _id );
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// <Manager::GetValueByte>
// Get the byte value object with the specified ID
//-----------------------------------------------------------------------------
ValueByte* Manager::GetValueByte
(
	ValueID const& _id
)
{
	if( Driver* driver = GetDriver( _id.GetHomeId() ) )
	{
		return driver->GetValueByte( _id );
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// <Manager::GetValueDecimal>
// Get the decimal value object with the specified ID
//-----------------------------------------------------------------------------
ValueDecimal* Manager::GetValueDecimal
(
	ValueID const& _id
)
{
	if( Driver* driver = GetDriver( _id.GetHomeId() ) )
	{
		return driver->GetValueDecimal( _id );
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// <Manager::GetValueInt>
// Get the int value object with the specified ID
//-----------------------------------------------------------------------------
ValueInt* Manager::GetValueInt
(
	ValueID const& _id
)
{
	if( Driver* driver = GetDriver( _id.GetHomeId() ) )
	{
		return driver->GetValueInt( _id );
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// <Manager::GetValueList>
// Get the list value object with the specified ID
//-----------------------------------------------------------------------------
ValueList* Manager::GetValueList
(
	ValueID const& _id
)
{
	if( Driver* driver = GetDriver( _id.GetHomeId() ) )
	{
		return driver->GetValueList( _id );
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// <Manager::GetValueShort>
// Get the short value object with the specified ID
//-----------------------------------------------------------------------------
ValueShort* Manager::GetValueShort
(
	ValueID const& _id
)
{
	if( Driver* driver = GetDriver( _id.GetHomeId() ) )
	{
		return driver->GetValueShort( _id );
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// <Manager::GetValueString>
// Get the string value object with the specified ID
//-----------------------------------------------------------------------------
ValueString* Manager::GetValueString
(
	ValueID const& _id
)
{
	if( Driver* driver = GetDriver( _id.GetHomeId() ) )
	{
		return driver->GetValueString( _id );
	}

	return NULL;
}

//-----------------------------------------------------------------------------
//	Notifications
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// <Manager::AddWatcher>
// Add a watcher to the list
//-----------------------------------------------------------------------------
bool Manager::AddWatcher
(
	pfnOnNotification_t _pWatcher,
	void* _context
)
{
	// Ensure this watcher is not already on the list
	for( list<Watcher*>::iterator it = m_watchers.begin(); it != m_watchers.end(); ++it )
	{
		if( ((*it)->m_callback == _pWatcher ) && ( (*it)->m_context == _context ) )
		{
			// Already in the list
			return false;
		}
	}

	m_watchers.push_back( new Watcher( _pWatcher, _context ) );
	return true;
}

//-----------------------------------------------------------------------------
// <Manager::RemoveWatcher>
// Remove a watcher from the list
//-----------------------------------------------------------------------------
bool Manager::RemoveWatcher
(
	pfnOnNotification_t _pWatcher,
	void* _context
)
{
	list<Watcher*>::iterator it = m_watchers.begin();
	while( it != m_watchers.end() )
	{
		if( ((*it)->m_callback == _pWatcher ) && ( (*it)->m_context == _context ) )
		{
			delete (*it);
			m_watchers.erase( it );
			return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// <Manager::NotifyWatchers>
// Notify any watching objects of a value change
//-----------------------------------------------------------------------------
void Manager::NotifyWatchers
(
	Notification const* _notification
)
{
	for( list<Watcher*>::iterator it = m_watchers.begin(); it != m_watchers.end(); ++it )
	{
		Watcher* pWatcher = *it;
		pWatcher->m_callback( _notification, pWatcher->m_context );
	}
}


//-----------------------------------------------------------------------------
//	Controller commands
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// <Manager::ResetController>
// Reset controller and erase all node information
//-----------------------------------------------------------------------------
void Manager::ResetController
(
	uint32 const _homeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		driver->ResetController();
	}
}

//-----------------------------------------------------------------------------
// <Manager::SoftReset>
// Soft-reset the Z-Wave controller chip
//-----------------------------------------------------------------------------
void Manager::SoftReset
(
	uint32 const _homeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		driver->SoftReset();
	}
}

//-----------------------------------------------------------------------------
// <Manager::RequestNodeNeighborUpdate>
// 
//-----------------------------------------------------------------------------
void Manager::RequestNodeNeighborUpdate
(
	uint32 const _homeId,
	uint8 const _nodeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		driver->RequestNodeNeighborUpdate( _nodeId );
	}
}

//-----------------------------------------------------------------------------
// <Manager::AssignReturnRoute>
// 
//-----------------------------------------------------------------------------
void Manager::AssignReturnRoute
(
	uint32 const _homeId,
	uint8 const _nodeId,
	uint8 const _targetNodeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		driver->AssignReturnRoute( _nodeId, _targetNodeId );
	}
} 

//-----------------------------------------------------------------------------
// <Manager::BeginControllerCommand>
// Start the controller performing one of its network management functions
//-----------------------------------------------------------------------------
bool Manager::BeginControllerCommand
(
	uint32 const _homeId, 
	Driver::ControllerCommand _command,
	Driver::pfnControllerCallback_t _callback,	// = NULL
	void* _context,								// = NULL
	bool _highPower								// = false
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		return( driver->BeginControllerCommand( _command, _callback, _context, _highPower ) );
	}

	return false;
}

//-----------------------------------------------------------------------------
// <Manager::CancelControllerCommand>
// Stop the current controller function
//-----------------------------------------------------------------------------
bool Manager::CancelControllerCommand
(
	uint32 const _homeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		return( driver->CancelControllerCommand() );
	}

	return false;
}

//-----------------------------------------------------------------------------
// <Manager::RequestNetworkUpdate>
// Request a network update
//-----------------------------------------------------------------------------
void Manager::RequestNetworkUpdate
(
	uint32 const _homeId
)
{
	if( Driver* driver = GetDriver( _homeId ) )
	{
		driver->RequestNetworkUpdate();
	}
}

