/*************************************************************************/ /*!
@File
@Title          RGX Bridge Functionality
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the Rogue Bridge code
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#ifndef RGX_BRIDGE_H
#define RGX_BRIDGE_H

#include "pvr_bridge.h"

#if defined(__cplusplus)
extern "C" {
#endif

#include "rgx_fwif_km.h"

#define RGXFWINITPARAMS_VERSION   1
#define RGXFWINITPARAMS_EXTENSION 128

#include "common_rgxta3d_bridge.h"
#include "common_rgxcmp_bridge.h"
#if defined(SUPPORT_FASTRENDER_DM)
#include "common_rgxtq2_bridge.h"
#endif
#if defined(SUPPORT_RGXTQ_BRIDGE)
#include "common_rgxtq_bridge.h"
#endif
#if defined(SUPPORT_USC_BREAKPOINT)
#include "common_rgxbreakpoint_bridge.h"
#endif
#include "common_rgxfwdbg_bridge.h"
#if defined(PDUMP)
#include "common_rgxpdump_bridge.h"
#endif
#include "common_rgxhwperf_bridge.h"
#if !defined(EXCLUDE_RGXREGCONFIG_BRIDGE)
#include "common_rgxregconfig_bridge.h"
#endif
#if defined(SUPPORT_RGXKICKSYNC_BRIDGE)
#include "common_rgxkicksync_bridge.h"
#endif
#include "common_rgxtimerquery_bridge.h"
#if defined(SUPPORT_RGXRAY_BRIDGE)
#include "common_rgxray_bridge.h"
#endif
/*
 * Bridge Cmd Ids
 */

/* *REMEMBER* to update PVRSRV_BRIDGE_RGX_LAST if you add/remove a bridge
 * group!
 * Also you need to ensure all PVRSRV_BRIDGE_RGX_xxx_DISPATCH_FIRST offsets
 * follow on from the previous bridge group's commands!
 *
 * If a bridge group is optional, ensure you *ALWAYS* define its index
 * (e.g. PVRSRV_BRIDGE_RGXCMP is always 151, even is the feature is not
 * defined). If an optional bridge group is not defined you must still
 * define PVRSRV_BRIDGE_RGX_xxx_DISPATCH_FIRST for it with an assigned
 * value of 0.
 */

/* The RGX bridge groups start at 128 (PVRSRV_BRIDGE_RGX_FIRST) rather than
 * follow-on from the other non-device bridge groups (meaning that they then
 * won't be displaced if other non-device bridge groups are added).
 */

#define PVRSRV_BRIDGE_RGX_FIRST                  128UL

/* 128: RGX TQ interface functions */
#define PVRSRV_BRIDGE_RGXTQ                      128UL
/* The RGXTQ bridge is conditional since the definitions in this header file
 * support both the rogue and volcanic servers, but the RGXTQ bridge is not
 * required at all on the Volcanic architecture.
 */
#if defined(SUPPORT_RGXTQ_BRIDGE)
#define PVRSRV_BRIDGE_RGXTQ_DISPATCH_FIRST       (PVRSRV_BRIDGE_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_RGXTQ_DISPATCH_LAST        (PVRSRV_BRIDGE_RGXTQ_DISPATCH_FIRST + PVRSRV_BRIDGE_RGXTQ_CMD_LAST)
#else
#define PVRSRV_BRIDGE_RGXTQ_DISPATCH_FIRST       0
#define PVRSRV_BRIDGE_RGXTQ_DISPATCH_LAST        (PVRSRV_BRIDGE_DISPATCH_LAST)
#endif

/* 129: RGX Compute interface functions */
#define PVRSRV_BRIDGE_RGXCMP                     129UL
#define PVRSRV_BRIDGE_RGXCMP_DISPATCH_FIRST      (PVRSRV_BRIDGE_RGXTQ_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_RGXCMP_DISPATCH_LAST       (PVRSRV_BRIDGE_RGXCMP_DISPATCH_FIRST + PVRSRV_BRIDGE_RGXCMP_CMD_LAST)

/* 130: RGX TA/3D interface functions */
#define PVRSRV_BRIDGE_RGXTA3D                    130UL
#define PVRSRV_BRIDGE_RGXTA3D_DISPATCH_FIRST     (PVRSRV_BRIDGE_RGXCMP_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_RGXTA3D_DISPATCH_LAST      (PVRSRV_BRIDGE_RGXTA3D_DISPATCH_FIRST + PVRSRV_BRIDGE_RGXTA3D_CMD_LAST)

/* 131: RGX Breakpoint interface functions */
#define PVRSRV_BRIDGE_RGXBREAKPOINT                 131UL
#if defined(SUPPORT_USC_BREAKPOINT)
#define PVRSRV_BRIDGE_RGXBREAKPOINT_DISPATCH_FIRST  (PVRSRV_BRIDGE_RGXTA3D_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_RGXBREAKPOINT_DISPATCH_LAST   (PVRSRV_BRIDGE_RGXBREAKPOINT_DISPATCH_FIRST + PVRSRV_BRIDGE_RGXBREAKPOINT_CMD_LAST)
#else
#define PVRSRV_BRIDGE_RGXBREAKPOINT_DISPATCH_FIRST  0
#define PVRSRV_BRIDGE_RGXBREAKPOINT_DISPATCH_LAST   (PVRSRV_BRIDGE_RGXTA3D_DISPATCH_LAST)
#endif

/* 132: RGX Debug/Misc interface functions */
#define PVRSRV_BRIDGE_RGXFWDBG                   132UL
#define PVRSRV_BRIDGE_RGXFWDBG_DISPATCH_FIRST    (PVRSRV_BRIDGE_RGXBREAKPOINT_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_RGXFWDBG_DISPATCH_LAST     (PVRSRV_BRIDGE_RGXFWDBG_DISPATCH_FIRST + PVRSRV_BRIDGE_RGXFWDBG_CMD_LAST)

/* 133: RGX PDump interface functions */
#define PVRSRV_BRIDGE_RGXPDUMP                   133UL
#if defined(PDUMP)
#define PVRSRV_BRIDGE_RGXPDUMP_DISPATCH_FIRST    (PVRSRV_BRIDGE_RGXFWDBG_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_RGXPDUMP_DISPATCH_LAST     (PVRSRV_BRIDGE_RGXPDUMP_DISPATCH_FIRST + PVRSRV_BRIDGE_RGXPDUMP_CMD_LAST)
#else
#define PVRSRV_BRIDGE_RGXPDUMP_DISPATCH_FIRST    0
#define PVRSRV_BRIDGE_RGXPDUMP_DISPATCH_LAST     (PVRSRV_BRIDGE_RGXFWDBG_DISPATCH_LAST)
#endif

/* 134: RGX HWPerf interface functions */
#define PVRSRV_BRIDGE_RGXHWPERF                  134UL
#define PVRSRV_BRIDGE_RGXHWPERF_DISPATCH_FIRST   (PVRSRV_BRIDGE_RGXPDUMP_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_RGXHWPERF_DISPATCH_LAST    (PVRSRV_BRIDGE_RGXHWPERF_DISPATCH_FIRST + PVRSRV_BRIDGE_RGXHWPERF_CMD_LAST)

/* 135: RGX Register Configuration interface functions */
#define PVRSRV_BRIDGE_RGXREGCONFIG                  135UL
#if !defined(EXCLUDE_RGXREGCONFIG_BRIDGE)
#define PVRSRV_BRIDGE_RGXREGCONFIG_DISPATCH_FIRST   (PVRSRV_BRIDGE_RGXHWPERF_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_RGXREGCONFIG_DISPATCH_LAST    (PVRSRV_BRIDGE_RGXREGCONFIG_DISPATCH_FIRST + PVRSRV_BRIDGE_RGXREGCONFIG_CMD_LAST)
#else
#define PVRSRV_BRIDGE_RGXREGCONFIG_DISPATCH_FIRST   0
#define PVRSRV_BRIDGE_RGXREGCONFIG_DISPATCH_LAST    (PVRSRV_BRIDGE_RGXHWPERF_DISPATCH_LAST)
#endif

/* 136: RGX kicksync interface */
#define PVRSRV_BRIDGE_RGXKICKSYNC                136UL
#if defined(SUPPORT_RGXKICKSYNC_BRIDGE)
#define PVRSRV_BRIDGE_RGXKICKSYNC_DISPATCH_FIRST (PVRSRV_BRIDGE_RGXREGCONFIG_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_RGXKICKSYNC_DISPATCH_LAST  (PVRSRV_BRIDGE_RGXKICKSYNC_DISPATCH_FIRST + PVRSRV_BRIDGE_RGXKICKSYNC_CMD_LAST)
#else
#define PVRSRV_BRIDGE_RGXKICKSYNC_DISPATCH_FIRST   0
#define PVRSRV_BRIDGE_RGXKICKSYNC_DISPATCH_LAST    (PVRSRV_BRIDGE_RGXREGCONFIG_DISPATCH_LAST)
#endif
/* 137: RGX TQ2 interface */
#define PVRSRV_BRIDGE_RGXTQ2                     137UL
#if defined(SUPPORT_FASTRENDER_DM)
#define PVRSRV_BRIDGE_RGXTQ2_DISPATCH_FIRST      (PVRSRV_BRIDGE_RGXKICKSYNC_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_RGXTQ2_DISPATCH_LAST       (PVRSRV_BRIDGE_RGXTQ2_DISPATCH_FIRST + PVRSRV_BRIDGE_RGXTQ2_CMD_LAST)
#else
#define PVRSRV_BRIDGE_RGXTQ2_DISPATCH_FIRST      (0)
#define PVRSRV_BRIDGE_RGXTQ2_DISPATCH_LAST       (PVRSRV_BRIDGE_RGXKICKSYNC_DISPATCH_LAST)
#endif

/* 138: RGX timer query interface */
#define PVRSRV_BRIDGE_RGXTIMERQUERY                 138UL
#define PVRSRV_BRIDGE_RGXTIMERQUERY_DISPATCH_FIRST  (PVRSRV_BRIDGE_RGXTQ2_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_RGXTIMERQUERY_DISPATCH_LAST   (PVRSRV_BRIDGE_RGXTIMERQUERY_DISPATCH_FIRST + PVRSRV_BRIDGE_RGXTIMERQUERY_CMD_LAST)

/* 139: RGX Ray tracing interface */
#define PVRSRV_BRIDGE_RGXRAY                 139UL
#if defined(SUPPORT_RGXRAY_BRIDGE)
#define PVRSRV_BRIDGE_RGXRAY_DISPATCH_FIRST  (PVRSRV_BRIDGE_RGXTIMERQUERY_DISPATCH_LAST + 1)
#define PVRSRV_BRIDGE_RGXRAY_DISPATCH_LAST   (PVRSRV_BRIDGE_RGXRAY_DISPATCH_FIRST + PVRSRV_BRIDGE_RGXRAY_CMD_LAST)
#else
#define PVRSRV_BRIDGE_RGXRAY_DISPATCH_FIRST  0
#define PVRSRV_BRIDGE_RGXRAY_DISPATCH_LAST   (PVRSRV_BRIDGE_RGXTIMERQUERY_DISPATCH_LAST)
#endif

#define PVRSRV_BRIDGE_RGX_LAST                   (PVRSRV_BRIDGE_RGXRAY)
#define PVRSRV_BRIDGE_RGX_DISPATCH_LAST          (PVRSRV_BRIDGE_RGXRAY_DISPATCH_LAST)

/* bit mask representing the enabled RGX bridges */

static const IMG_UINT32 gui32RGXBridges =
	  (1U << (PVRSRV_BRIDGE_RGXTQ - PVRSRV_BRIDGE_RGX_FIRST))
#if defined(RGX_FEATURE_COMPUTE) || defined(__KERNEL__)
	| (1U << (PVRSRV_BRIDGE_RGXCMP - PVRSRV_BRIDGE_RGX_FIRST))
#endif
	| (1U << (PVRSRV_BRIDGE_RGXTA3D - PVRSRV_BRIDGE_RGX_FIRST))
#if defined(SUPPORT_BREAKPOINT)
	| (1U << (PVRSRV_BRIDGE_BREAKPOINT - PVRSRV_BRIDGE_RGX_FIRST))
#endif
	| (1U << (PVRSRV_BRIDGE_RGXFWDBG - PVRSRV_BRIDGE_RGX_FIRST))
#if defined(PDUMP)
	| (1U << (PVRSRV_BRIDGE_RGXPDUMP - PVRSRV_BRIDGE_RGX_FIRST))
#endif
#if defined(SUPPORT_RGXKICKSYNC_BRIDGE)
	| (1U << (PVRSRV_BRIDGE_RGXKICKSYNC - PVRSRV_BRIDGE_RGX_FIRST))
#endif
	| (1U << (PVRSRV_BRIDGE_RGXHWPERF - PVRSRV_BRIDGE_RGX_FIRST))
#if defined(SUPPORT_REGCONFIG)
	| (1U << (PVRSRV_BRIDGE_RGXREGCONFIG - PVRSRV_BRIDGE_RGX_FIRST))
#endif
	| (1U << (PVRSRV_BRIDGE_RGXKICKSYNC - PVRSRV_BRIDGE_RGX_FIRST))
#if defined(SUPPORT_FASTRENDER_DM) || defined(__KERNEL__)
	| (1U << (PVRSRV_BRIDGE_RGXTQ2 - PVRSRV_BRIDGE_RGX_FIRST))
#endif
#if defined(SUPPORT_TIMERQUERY)
	| (1U << (PVRSRV_BRIDGE_RGXTIMERQUERY - PVRSRV_BRIDGE_RGX_FIRST))
#endif
	| (1U << (PVRSRV_BRIDGE_RGXRAY - PVRSRV_BRIDGE_RGX_FIRST))
	;
/* bit field representing which RGX bridge groups may optionally not
 * be present in the server
 */

#define RGX_BRIDGES_OPTIONAL \
	( \
		0 /* no RGX bridges are currently optional */ \
	)

#if defined(__cplusplus)
}
#endif

#endif /* RGX_BRIDGE_H */
