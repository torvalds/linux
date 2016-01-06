/*************************************************************************/ /*!
@File
@Title          Server bridge for timerquery
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for timerquery
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

#include <stddef.h>
#include <asm/uaccess.h>

#include "img_defs.h"

#include "rgxtimerquery.h"


#include "common_timerquery_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#include "rgx_bridge.h"
#include "srvcore.h"
#include "handle.h"

#if defined (SUPPORT_AUTH)
#include "osauth.h"
#endif

#include <linux/slab.h>




/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeRGXBeginTimerQuery(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXBEGINTIMERQUERY *psRGXBeginTimerQueryIN,
					  PVRSRV_BRIDGE_OUT_RGXBEGINTIMERQUERY *psRGXBeginTimerQueryOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psRGXBeginTimerQueryOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &hDevNodeInt,
											psRGXBeginTimerQueryIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXBeginTimerQueryOUT->eError != PVRSRV_OK)
					{
						goto RGXBeginTimerQuery_exit;
					}
				}


	psRGXBeginTimerQueryOUT->eError =
		PVRSRVRGXBeginTimerQueryKM(
					hDevNodeInt,
					psRGXBeginTimerQueryIN->ui32QueryId);




RGXBeginTimerQuery_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXEndTimerQuery(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXENDTIMERQUERY *psRGXEndTimerQueryIN,
					  PVRSRV_BRIDGE_OUT_RGXENDTIMERQUERY *psRGXEndTimerQueryOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psRGXEndTimerQueryOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &hDevNodeInt,
											psRGXEndTimerQueryIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXEndTimerQueryOUT->eError != PVRSRV_OK)
					{
						goto RGXEndTimerQuery_exit;
					}
				}


	psRGXEndTimerQueryOUT->eError =
		PVRSRVRGXEndTimerQueryKM(
					hDevNodeInt);




RGXEndTimerQuery_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXQueryTimer(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXQUERYTIMER *psRGXQueryTimerIN,
					  PVRSRV_BRIDGE_OUT_RGXQUERYTIMER *psRGXQueryTimerOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psRGXQueryTimerOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &hDevNodeInt,
											psRGXQueryTimerIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXQueryTimerOUT->eError != PVRSRV_OK)
					{
						goto RGXQueryTimer_exit;
					}
				}


	psRGXQueryTimerOUT->eError =
		PVRSRVRGXQueryTimerKM(
					hDevNodeInt,
					psRGXQueryTimerIN->ui32QueryId,
					&psRGXQueryTimerOUT->ui64StartTime,
					&psRGXQueryTimerOUT->ui64EndTime);




RGXQueryTimer_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXCurrentTime(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCURRENTTIME *psRGXCurrentTimeIN,
					  PVRSRV_BRIDGE_OUT_RGXCURRENTTIME *psRGXCurrentTimeOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psRGXCurrentTimeOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &hDevNodeInt,
											psRGXCurrentTimeIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXCurrentTimeOUT->eError != PVRSRV_OK)
					{
						goto RGXCurrentTime_exit;
					}
				}


	psRGXCurrentTimeOUT->eError =
		PVRSRVRGXCurrentTime(
					hDevNodeInt,
					&psRGXCurrentTimeOUT->ui64Time);




RGXCurrentTime_exit:

	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */


PVRSRV_ERROR InitTIMERQUERYBridge(IMG_VOID);
PVRSRV_ERROR DeinitTIMERQUERYBridge(IMG_VOID);

/*
 * Register all TIMERQUERY functions with services
 */
PVRSRV_ERROR InitTIMERQUERYBridge(IMG_VOID)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_TIMERQUERY, PVRSRV_BRIDGE_TIMERQUERY_RGXBEGINTIMERQUERY, PVRSRVBridgeRGXBeginTimerQuery,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_TIMERQUERY, PVRSRV_BRIDGE_TIMERQUERY_RGXENDTIMERQUERY, PVRSRVBridgeRGXEndTimerQuery,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_TIMERQUERY, PVRSRV_BRIDGE_TIMERQUERY_RGXQUERYTIMER, PVRSRVBridgeRGXQueryTimer,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_TIMERQUERY, PVRSRV_BRIDGE_TIMERQUERY_RGXCURRENTTIME, PVRSRVBridgeRGXCurrentTime,
					IMG_NULL, IMG_NULL,
					0, 0);


	return PVRSRV_OK;
}

/*
 * Unregister all timerquery functions with services
 */
PVRSRV_ERROR DeinitTIMERQUERYBridge(IMG_VOID)
{
	return PVRSRV_OK;
}

