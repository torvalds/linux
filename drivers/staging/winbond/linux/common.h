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

#define OS_DISCONNECTED	0
#define OS_CONNECTED	1


#define OS_EVENT_INDICATE( _A, _B, _F )
#define OS_PMKID_STATUS_EVENT( _A )

#endif // COMMON_DEF

