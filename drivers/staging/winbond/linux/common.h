//
// common.h
//
// This file contains the OS dependant definition and function.
// Every OS has this file individual.
//

#define DebugUsbdStatusInformation( _A )

#ifndef COMMON_DEF
#define COMMON_DEF

#include <linux/version.h>
#include <linux/usb.h>
#include <linux/kernel.h> //need for kernel alert
#include <linux/autoconf.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/slab.h> //memory allocate
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>//need for init and exit modules marco
#include <linux/ctype.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/wireless.h>
#include <linux/if_arp.h>
#include <asm/uaccess.h>
#include <net/iw_handler.h>
#include <linux/skbuff.h>


//#define DEBUG_ENABLED  1



//===============================================================
// Common type definition
//===============================================================

//===========================================
#define IGNORE      2
#define	SUCCESS     1
#define	FAILURE     0


#define STATUS_MEDIA_CONNECT 1
#define STATUS_MEDIA_DISCONNECT 0

#ifndef BIT
#define BIT(x)                  (1 << (x))
#endif

//==================================================================================================
// Common function definition
//==================================================================================================
#ifndef abs
#define abs(_T)							((_T) < 0 ? -_T : _T)
#endif
#define DEBUG_ENABLED
#define ETH_LENGTH_OF_ADDRESS	6
#ifdef DEBUG_ENABLED
#define WBDEBUG( _M )	printk _M
#else
#define WBDEBUG( _M )	0
#endif

#define OS_DISCONNECTED	0
#define OS_CONNECTED	1


#define OS_EVENT_INDICATE( _A, _B, _F )
#define OS_PMKID_STATUS_EVENT( _A )


/* Uff, no, longs are not atomic on all architectures Linux
 * supports. This should really use atomic_t */

#define OS_ATOMIC			u32
#define OS_ATOMIC_READ( _A, _V )	_V
#define OS_ATOMIC_INC( _A, _V )		EncapAtomicInc( _A, (void*)_V )
#define OS_ATOMIC_DEC( _A, _V )		EncapAtomicDec( _A, (void*)_V )
#define OS_MEMORY_CLEAR( _A, _S )	memset( (u8 *)_A,0,_S)
#define OS_MEMORY_COMPARE( _A, _B, _S )	(memcmp(_A,_B,_S)? 0 : 1) // Definition is reverse with Ndis 1: the same 0: different

#endif // COMMON_DEF

