//
// common.h
//
// This file contains the OS dependant definition and function.
// Every OS has this file individual.
//

#define DebugUsbdStatusInformation( _A )

#ifndef COMMON_DEF
#define COMMON_DEF

//#define DEBUG_ENABLED  1

//==================================================================================================
// Common function definition
//==================================================================================================
#define DEBUG_ENABLED
#define ETH_LENGTH_OF_ADDRESS	6
#ifdef DEBUG_ENABLED
#define WBDEBUG( _M )	printk _M
#else
#define WBDEBUG( _M )	0
#endif

#define OS_EVENT_INDICATE( _A, _B, _F )
#define OS_PMKID_STATUS_EVENT( _A )

#endif // COMMON_DEF

