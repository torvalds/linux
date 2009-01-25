/* wlan_compat.h
*
* Types and macros to aid in portability
*
* Copyright (C) 1999 AbsoluteValue Systems, Inc.  All Rights Reserved.
* --------------------------------------------------------------------
*
* linux-wlan
*
*   The contents of this file are subject to the Mozilla Public
*   License Version 1.1 (the "License"); you may not use this file
*   except in compliance with the License. You may obtain a copy of
*   the License at http://www.mozilla.org/MPL/
*
*   Software distributed under the License is distributed on an "AS
*   IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
*   implied. See the License for the specific language governing
*   rights and limitations under the License.
*
*   Alternatively, the contents of this file may be used under the
*   terms of the GNU Public License version 2 (the "GPL"), in which
*   case the provisions of the GPL are applicable instead of the
*   above.  If you wish to allow the use of your version of this file
*   only under the terms of the GPL and not to allow others to use
*   your version of this file under the MPL, indicate your decision
*   by deleting the provisions above and replace them with the notice
*   and other provisions required by the GPL.  If you do not delete
*   the provisions above, a recipient may use your version of this
*   file under either the MPL or the GPL.
*
* --------------------------------------------------------------------
*
* Inquiries regarding the linux-wlan Open Source project can be
* made directly to:
*
* AbsoluteValue Systems Inc.
* info@linux-wlan.com
* http://www.linux-wlan.com
*
* --------------------------------------------------------------------
*
* Portions of the development of this software were funded by
* Intersil Corporation as part of PRISM(R) chipset product development.
*
* --------------------------------------------------------------------
*/

#ifndef _WLAN_COMPAT_H
#define _WLAN_COMPAT_H

/*=============================================================*/
/*------ OS Portability Macros --------------------------------*/
/*=============================================================*/

#ifndef WLAN_DBVAR
#define WLAN_DBVAR	wlan_debug
#endif

#include <linux/hardirq.h>

#define WLAN_LOG_ERROR(x,args...) printk(KERN_ERR "%s: " x , __func__ , ##args);

#define WLAN_LOG_WARNING(x,args...) printk(KERN_WARNING "%s: " x , __func__ , ##args);

#define WLAN_LOG_NOTICE(x,args...) printk(KERN_NOTICE "%s: " x , __func__ , ##args);

#define WLAN_LOG_INFO(args... ) printk(KERN_INFO args)

#if defined(WLAN_INCLUDE_DEBUG)
	#define WLAN_HEX_DUMP( l, x, p, n)	if( WLAN_DBVAR >= (l) ){ \
		int __i__; \
		printk(KERN_DEBUG x ":"); \
		for( __i__=0; __i__ < (n); __i__++) \
			printk( " %02x", ((u8*)(p))[__i__]); \
		printk("\n"); }

	#define WLAN_LOG_DEBUG(l,x,args...) if ( WLAN_DBVAR >= (l)) printk(KERN_DEBUG "%s(%lu): " x ,  __func__, (preempt_count() & PREEMPT_MASK), ##args );
#else
	#define WLAN_HEX_DUMP( l, s, p, n)

	#define WLAN_LOG_DEBUG(l, s, args...)
#endif

#undef netdevice_t
typedef struct net_device netdevice_t;

#define URB_ASYNC_UNLINK 0
#define USB_QUEUE_BULK 0

/*=============================================================*/
/*--- General Macros ------------------------------------------*/
/*=============================================================*/

#define wlan_max(a, b) (((a) > (b)) ? (a) : (b))
#define wlan_min(a, b) (((a) < (b)) ? (a) : (b))

#define wlan_isprint(c)	(((c) > (0x19)) && ((c) < (0x7f)))

#define wlan_hexchar(x) (((x) < 0x0a) ? ('0' + (x)) : ('a' + ((x) - 0x0a)))

/* Create a string of printable chars from something that might not be */
/* It's recommended that the str be 4*len + 1 bytes long */
#define wlan_mkprintstr(buf, buflen, str, strlen) \
{ \
	int i = 0; \
	int j = 0; \
	memset(str, 0, (strlen)); \
	for (i = 0; i < (buflen); i++) { \
		if ( wlan_isprint((buf)[i]) ) { \
			(str)[j] = (buf)[i]; \
			j++; \
		} else { \
			(str)[j] = '\\'; \
			(str)[j+1] = 'x'; \
			(str)[j+2] = wlan_hexchar(((buf)[i] & 0xf0) >> 4); \
			(str)[j+3] = wlan_hexchar(((buf)[i] & 0x0f)); \
			j += 4; \
		} \
	} \
}

/*=============================================================*/
/*--- Variables -----------------------------------------------*/
/*=============================================================*/

#ifdef WLAN_INCLUDE_DEBUG
extern int wlan_debug;
#endif

#endif /* _WLAN_COMPAT_H */
