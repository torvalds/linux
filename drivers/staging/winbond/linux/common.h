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

typedef u8*            PUCHAR;
typedef s8*            PCHAR;
typedef u8*            PBOOLEAN;
typedef u16*           PUSHORT;
typedef u32*           PULONG;
typedef s16*   PSHORT;


//===========================================
#define IGNORE      2
#define	SUCCESS     1
#define	FAILURE     0


#ifndef true
#define true        1
#endif

#ifndef false
#define false       0
#endif

// PD43 20021108
#ifndef TRUE
#define TRUE        1
#endif

#ifndef FALSE
#define FALSE       0
#endif

#define STATUS_MEDIA_CONNECT 1
#define STATUS_MEDIA_DISCONNECT 0

#ifndef BIT
#define BIT(x)                  (1 << (x))
#endif

typedef struct urb * PURB;



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
#define OS_MEMORY_CLEAR( _A, _S )	memset( (PUCHAR)_A,0,_S)
#define OS_MEMORY_COMPARE( _A, _B, _S )	(memcmp(_A,_B,_S)? 0 : 1) // Definition is reverse with Ndis 1: the same 0: different


#define OS_SPIN_LOCK				spinlock_t
#define OS_SPIN_LOCK_ALLOCATE( _S )		spin_lock_init( _S );
#define OS_SPIN_LOCK_FREE( _S )
#define OS_SPIN_LOCK_ACQUIRED( _S )		spin_lock_irq( _S )
#define OS_SPIN_LOCK_RELEASED( _S )		spin_unlock_irq( _S );

#define OS_TIMER	struct timer_list
#define OS_TIMER_INITIAL( _T, _F, _P )			\
{							\
	init_timer( _T );				\
	(_T)->function = (void *)_F##_1a;		\
	(_T)->data = (unsigned long)_P;			\
}

// _S : Millisecond
// 20060420 At least 1 large than jiffies
#define OS_TIMER_SET( _T, _S )					\
{								\
	(_T)->expires = jiffies + ((_S*HZ+999)/1000);\
	add_timer( _T );					\
}
#define OS_TIMER_CANCEL( _T, _B )		del_timer_sync( _T )
#define OS_TIMER_GET_SYS_TIME( _T )		(*_T=jiffies)


#endif // COMMON_DEF

