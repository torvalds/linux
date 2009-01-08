//
// common.h
//
// This file contains the OS dependant definition and function.
// Every OS has this file individual.
//
#ifndef COMMON_DEF
#define COMMON_DEF

//#define DEBUG_ENABLED  1

//==================================================================================================
// Common function definition
//==================================================================================================
#define DEBUG_ENABLED
#ifdef DEBUG_ENABLED
#define WBDEBUG( _M )	printk _M
#else
#define WBDEBUG( _M )	0
#endif

#endif // COMMON_DEF

