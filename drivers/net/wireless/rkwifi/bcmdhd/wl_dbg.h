/*
 * Minimal debug/trace/assert driver definitions for
 * Broadcom 802.11 Networking Adapter.
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: wl_dbg.h 376019 2012-12-21 01:00:06Z $
 */


#ifndef _wl_dbg_h_
#define _wl_dbg_h_

/* wl_msg_level is a bit vector with defs in wlioctl.h */
extern uint32 wl_msg_level;
extern uint32 wl_msg_level2;

#define WL_TIMESTAMP()

#if 0 && (VERSION_MAJOR > 9)
#include <IOKit/apple80211/IO8Log.h>
#define WL_PRINT(args)		do { printf args; IO8Log args; } while (0)
#else
#define WL_PRINT(args)		do { WL_TIMESTAMP(); printf args; } while (0)
#endif


/* To disable a message completely ... until you need it again */
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
#endif /* _wl_dbg_h_ */
