/*************************************************************************/ /*!
@File
@Title          RGX Bridge Functionality
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the rgx Bridge code
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

#if !defined(__RGX_BRIDGE_H__)
#define __RGX_BRIDGE_H__

#include "pvr_bridge.h"

#if defined (__cplusplus)
extern "C" {
#endif

#include "common_rgxinit_bridge.h"
#include "common_rgxta3d_bridge.h"
#include "common_rgxcmp_bridge.h"
#include "common_rgxtq_bridge.h"
#include "common_breakpoint_bridge.h"
#include "common_debugmisc_bridge.h"
#include "common_rgxpdump_bridge.h"
#include "common_rgxhwperf_bridge.h"
#if defined(RGX_FEATURE_RAY_TRACING)
#include "common_rgxray_bridge.h"
#endif
#include "common_regconfig_bridge.h"
#include "common_timerquery_bridge.h"

/* 
 * Bridge Cmd Ids
 */

/* *REMEMBER* to update PVRSRV_BRIDGE_LAST_RGX_CMD if you add/remove a command! 
 * Also you need to ensure all PVRSRV_BRIDGE_RGX_CMD_BASE+ offsets are sequential!
 */

#define PVRSRV_BRIDGE_RGX_CMD_BASE (PVRSRV_BRIDGE_LAST_NON_DEVICE_CMD+1)

/* "last" below actually means last, not plus 1 as elsewhere */
#define PVRSRV_BRIDGE_RGXTQ_START      (PVRSRV_BRIDGE_RGX_CMD_BASE + 0)
#define PVRSRV_BRIDGE_RGXCMP_START     (PVRSRV_BRIDGE_RGXTQ_CMD_LAST + 1)
#define PVRSRV_BRIDGE_RGXINIT_START    (PVRSRV_BRIDGE_RGXCMP_CMD_LAST + 1)
#define PVRSRV_BRIDGE_RGXTA3D_START    (PVRSRV_BRIDGE_RGXINIT_CMD_LAST + 1)
#define PVRSRV_BRIDGE_BREAKPOINT_START (PVRSRV_BRIDGE_RGXTA3D_CMD_LAST + 1)
#define PVRSRV_BRIDGE_DEBUGMISC_START  (PVRSRV_BRIDGE_BREAKPOINT_CMD_LAST + 1)
#define PVRSRV_BRIDGE_RGXPDUMP_START   (PVRSRV_BRIDGE_DEBUGMISC_CMD_LAST +1)
#define PVRSRV_BRIDGE_RGXHWPERF_START  (PVRSRV_BRIDGE_RGXPDUMP_CMD_LAST +1)
#define PVRSRV_BRIDGE_RGXRAY_START     (PVRSRV_BRIDGE_RGXHWPERF_CMD_LAST +1)
#ifndef RGX_FEATURE_RAY_TRACING
#define PVRSRV_BRIDGE_RGXRAY_CMD_LAST     (PVRSRV_BRIDGE_RGXRAY_START -1)
#endif
#define PVRSRV_BRIDGE_REGCONFIG_START  (PVRSRV_BRIDGE_RGXRAY_CMD_LAST +1)
#define PVRSRV_BRIDGE_TIMERQUERY_START (PVRSRV_BRIDGE_REGCONFIG_CMD_LAST + 1)
#define PVRSRV_BRIDGE_LAST_RGX_CMD     (PVRSRV_BRIDGE_TIMERQUERY_CMD_LAST)

#if defined (__cplusplus)
}
#endif

#endif /* __RGX_BRIDGE_H__ */

