/*
 *
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *
 */


#ifndef _LOGGING_H_
#define _LOGGING_H_

//#include <linux/init.h>
//#include <linux/module.h>

#include "osd.h"

#define VMBUS				0x0001
#define STORVSC				0x0002
#define NETVSC				0x0004
#define INPUTVSC			0x0008
#define BLKVSC				0x0010
#define VMBUS_DRV			0x0100
#define STORVSC_DRV			0x0200
#define NETVSC_DRV			0x0400
#define INPUTVSC_DRV		0x0800
#define BLKVSC_DRV			0x1000

#define ALL_MODULES			(VMBUS		|\
							STORVSC		|\
							NETVSC		|\
							INPUTVSC	|\
							BLKVSC		|\
							VMBUS_DRV	|\
							STORVSC_DRV	|\
							NETVSC_DRV	|\
							INPUTVSC_DRV|\
							BLKVSC_DRV)

// Logging Level
#define CRITICAL_LVL				2
#define ERROR_LVL					3
#define WARNING_LVL					4
#define INFO_LVL					6
#define DEBUG_LVL					7
#define DEBUG_LVL_ENTEREXIT			8
#define DEBUG_RING_LVL				9

extern unsigned int vmbus_loglevel;

#define ASSERT(expr)	\
        if (!(expr)) {	\
		LogMsg("<%d>Assertion failed! %s,%s,%s,line=%d\n", CRITICAL_LVL, #expr,__FILE__,__FUNCTION__,__LINE__);	\
		__asm__ __volatile__("int3");	\
        }

#define DPRINT(mod, lvl, fmt, args...) do {\
	if (mod & (HIWORD(vmbus_loglevel))) \
		(lvl <= LOWORD(vmbus_loglevel))?(LogMsg("<%d>" #mod": %s() " fmt "\n", DEBUG_LVL, __FUNCTION__, ## args)):(0);\
	} while (0)

#define DPRINT_DBG(mod, fmt, args...) do {\
	if (mod & (HIWORD(vmbus_loglevel))) \
		(DEBUG_LVL <= LOWORD(vmbus_loglevel))?(LogMsg("<%d>" #mod": %s() " fmt "\n", DEBUG_LVL, __FUNCTION__, ## args)):(0);\
	} while (0)

#define DPRINT_INFO(mod, fmt, args...) do {\
	if (mod & (HIWORD(vmbus_loglevel))) \
		(INFO_LVL <= LOWORD(vmbus_loglevel))?(LogMsg("<%d>" #mod": " fmt "\n", INFO_LVL, ## args)):(0);\
	} while (0)

#define DPRINT_WARN(mod, fmt, args...) do {\
	if (mod & (HIWORD(vmbus_loglevel))) \
		(WARNING_LVL <= LOWORD(vmbus_loglevel))?(LogMsg("<%d>" #mod": WARNING! " fmt "\n", WARNING_LVL, ## args)):(0);\
	} while (0)

#define DPRINT_ERR(mod, fmt, args...) do {\
	if (mod & (HIWORD(vmbus_loglevel))) \
		(ERROR_LVL <= LOWORD(vmbus_loglevel))?(LogMsg("<%d>" #mod": %s() ERROR!! " fmt "\n", ERROR_LVL, __FUNCTION__, ## args)):(0);\
	} while (0)

#ifdef DEBUG
#define DPRINT_ENTER(mod) do {\
	if (mod & (HIWORD(vmbus_loglevel))) \
		(DEBUG_LVL_ENTEREXIT <= LOWORD(vmbus_loglevel))?(LogMsg("<%d>" "["#mod"]: %s() enter\n", DEBUG_LVL, __FUNCTION__)):(0);\
	} while (0)

#define DPRINT_EXIT(mod) do {\
	if (mod & (HIWORD(vmbus_loglevel))) \
		(DEBUG_LVL_ENTEREXIT <= LOWORD(vmbus_loglevel))?(LogMsg("<%d>" "["#mod"]: %s() exit\n", DEBUG_LVL, __FUNCTION__)):(0);\
	} while (0)
#else
#define DPRINT_ENTER(mod)
#define DPRINT_EXIT(mod)
#endif

static inline void PrintBytes(const unsigned char* bytes, int len)
{
	int i=0;

	LogMsg("\n<< ");
	for (i=0; i< len; i++)
	{
		LogMsg("0x%x ", bytes[i]);
	}
	LogMsg(">>\n");
}

//
// Inline
//
//static inline void GuidToStr(const GUID g, char *str)
//{
//	sprintf(str, "{%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x%02x%02x}",
//	g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
//
//}

#endif //_LOGGING_H_
