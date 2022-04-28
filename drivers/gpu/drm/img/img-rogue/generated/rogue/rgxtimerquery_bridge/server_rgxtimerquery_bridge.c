/*******************************************************************************
@File
@Title          Server bridge for rgxtimerquery
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for rgxtimerquery
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
*******************************************************************************/

#include <linux/uaccess.h>

#include "img_defs.h"

#include "rgxtimerquery.h"

#include "common_rgxtimerquery_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#if defined(SUPPORT_RGX)
#include "rgx_bridge.h"
#endif
#include "srvcore.h"
#include "handle.h"

#include <linux/slab.h>

/* ***************************************************************************
 * Server-side bridge entry points
 */

static IMG_INT
PVRSRVBridgeRGXBeginTimerQuery(IMG_UINT32 ui32DispatchTableEntry,
			       IMG_UINT8 * psRGXBeginTimerQueryIN_UI8,
			       IMG_UINT8 * psRGXBeginTimerQueryOUT_UI8,
			       CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXBEGINTIMERQUERY *psRGXBeginTimerQueryIN =
	    (PVRSRV_BRIDGE_IN_RGXBEGINTIMERQUERY *) IMG_OFFSET_ADDR(psRGXBeginTimerQueryIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXBEGINTIMERQUERY *psRGXBeginTimerQueryOUT =
	    (PVRSRV_BRIDGE_OUT_RGXBEGINTIMERQUERY *) IMG_OFFSET_ADDR(psRGXBeginTimerQueryOUT_UI8,
								     0);

	psRGXBeginTimerQueryOUT->eError =
	    PVRSRVRGXBeginTimerQueryKM(psConnection, OSGetDevNode(psConnection),
				       psRGXBeginTimerQueryIN->ui32QueryId);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXEndTimerQuery(IMG_UINT32 ui32DispatchTableEntry,
			     IMG_UINT8 * psRGXEndTimerQueryIN_UI8,
			     IMG_UINT8 * psRGXEndTimerQueryOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXENDTIMERQUERY *psRGXEndTimerQueryIN =
	    (PVRSRV_BRIDGE_IN_RGXENDTIMERQUERY *) IMG_OFFSET_ADDR(psRGXEndTimerQueryIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXENDTIMERQUERY *psRGXEndTimerQueryOUT =
	    (PVRSRV_BRIDGE_OUT_RGXENDTIMERQUERY *) IMG_OFFSET_ADDR(psRGXEndTimerQueryOUT_UI8, 0);

	PVR_UNREFERENCED_PARAMETER(psRGXEndTimerQueryIN);

	psRGXEndTimerQueryOUT->eError =
	    PVRSRVRGXEndTimerQueryKM(psConnection, OSGetDevNode(psConnection));

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXQueryTimer(IMG_UINT32 ui32DispatchTableEntry,
			  IMG_UINT8 * psRGXQueryTimerIN_UI8,
			  IMG_UINT8 * psRGXQueryTimerOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXQUERYTIMER *psRGXQueryTimerIN =
	    (PVRSRV_BRIDGE_IN_RGXQUERYTIMER *) IMG_OFFSET_ADDR(psRGXQueryTimerIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXQUERYTIMER *psRGXQueryTimerOUT =
	    (PVRSRV_BRIDGE_OUT_RGXQUERYTIMER *) IMG_OFFSET_ADDR(psRGXQueryTimerOUT_UI8, 0);

	psRGXQueryTimerOUT->eError =
	    PVRSRVRGXQueryTimerKM(psConnection, OSGetDevNode(psConnection),
				  psRGXQueryTimerIN->ui32QueryId,
				  &psRGXQueryTimerOUT->ui64StartTime,
				  &psRGXQueryTimerOUT->ui64EndTime);

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

PVRSRV_ERROR InitRGXTIMERQUERYBridge(void);
PVRSRV_ERROR DeinitRGXTIMERQUERYBridge(void);

/*
 * Register all RGXTIMERQUERY functions with services
 */
PVRSRV_ERROR InitRGXTIMERQUERYBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTIMERQUERY,
			      PVRSRV_BRIDGE_RGXTIMERQUERY_RGXBEGINTIMERQUERY,
			      PVRSRVBridgeRGXBeginTimerQuery, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTIMERQUERY,
			      PVRSRV_BRIDGE_RGXTIMERQUERY_RGXENDTIMERQUERY,
			      PVRSRVBridgeRGXEndTimerQuery, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTIMERQUERY,
			      PVRSRV_BRIDGE_RGXTIMERQUERY_RGXQUERYTIMER, PVRSRVBridgeRGXQueryTimer,
			      NULL);

	return PVRSRV_OK;
}

/*
 * Unregister all rgxtimerquery functions with services
 */
PVRSRV_ERROR DeinitRGXTIMERQUERYBridge(void)
{

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTIMERQUERY,
				PVRSRV_BRIDGE_RGXTIMERQUERY_RGXBEGINTIMERQUERY);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTIMERQUERY,
				PVRSRV_BRIDGE_RGXTIMERQUERY_RGXENDTIMERQUERY);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTIMERQUERY,
				PVRSRV_BRIDGE_RGXTIMERQUERY_RGXQUERYTIMER);

	return PVRSRV_OK;
}
