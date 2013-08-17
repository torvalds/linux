/*
 * Minimal debug/trace/assert driver definitions for
 * Broadcom 802.11 Networking Adapter.
 *
 * Copyright (C) 1999-2012, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: wl_dbg.h 326635 2012-04-10 03:15:29Z $
 */


#ifndef _wl_dbg_h_
#define _wl_dbg_h_


extern uint32 wl_msg_level;
extern uint32 wl_msg_level2;

#define WL_TIMESTAMP()

#if 0 && (VERSION_MAJOR > 9)
#include <IOKit/apple80211/IO8Log.h>
#define WL_PRINT(args)		do { printf args; IO8Log args; } while (0)
#else
#define WL_PRINT(args)		do { WL_TIMESTAMP(); printf args; } while (0)
#endif



#define WL_NONE(args)

#define	WL_ERROR(args)
#define	WL_TRACE(args)
#define WL_APSTA_UPDN(args)
#define WL_APSTA_RX(args)
#ifdef WLMSG_WSEC
#define WL_WSEC(args)		WL_PRINT(args)
#define WL_WSEC_DUMP(args)	WL_PRINT(args)
#else
#define WL_WSEC(args)
#define WL_WSEC_DUMP(args)
#endif

extern uint32 wl_msg_level;
extern uint32 wl_msg_level2;
#endif 
