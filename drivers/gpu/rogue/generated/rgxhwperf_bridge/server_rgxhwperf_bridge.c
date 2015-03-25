/*************************************************************************/ /*!
@File
@Title          Server bridge for rgxhwperf
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for rgxhwperf
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

#include "rgxhwperf.h"


#include "common_rgxhwperf_bridge.h"

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
 * Bridge proxy functions
 */



/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeRGXCtrlHWPerf(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXCTRLHWPERF *psRGXCtrlHWPerfIN,
					 PVRSRV_BRIDGE_OUT_RGXCTRLHWPERF *psRGXCtrlHWPerfOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXHWPERF_RGXCTRLHWPERF);





				{
					/* Look up the address from the handle */
					psRGXCtrlHWPerfOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psRGXCtrlHWPerfIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXCtrlHWPerfOUT->eError != PVRSRV_OK)
					{
						goto RGXCtrlHWPerf_exit;
					}

				}

	psRGXCtrlHWPerfOUT->eError =
		PVRSRVRGXCtrlHWPerfKM(
					hDevNodeInt,
					psRGXCtrlHWPerfIN->bToggle,
					psRGXCtrlHWPerfIN->ui64Mask);



RGXCtrlHWPerf_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXConfigEnableHWPerfCounters(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXCONFIGENABLEHWPERFCOUNTERS *psRGXConfigEnableHWPerfCountersIN,
					 PVRSRV_BRIDGE_OUT_RGXCONFIGENABLEHWPERFCOUNTERS *psRGXConfigEnableHWPerfCountersOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	RGX_HWPERF_CONFIG_CNTBLK *psBlockConfigsInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXHWPERF_RGXCONFIGENABLEHWPERFCOUNTERS);




	if (psRGXConfigEnableHWPerfCountersIN->ui32ArrayLen != 0)
	{
		psBlockConfigsInt = OSAllocMem(psRGXConfigEnableHWPerfCountersIN->ui32ArrayLen * sizeof(RGX_HWPERF_CONFIG_CNTBLK));
		if (!psBlockConfigsInt)
		{
			psRGXConfigEnableHWPerfCountersOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXConfigEnableHWPerfCounters_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXConfigEnableHWPerfCountersIN->psBlockConfigs, psRGXConfigEnableHWPerfCountersIN->ui32ArrayLen * sizeof(RGX_HWPERF_CONFIG_CNTBLK))
				|| (OSCopyFromUser(NULL, psBlockConfigsInt, psRGXConfigEnableHWPerfCountersIN->psBlockConfigs,
				psRGXConfigEnableHWPerfCountersIN->ui32ArrayLen * sizeof(RGX_HWPERF_CONFIG_CNTBLK)) != PVRSRV_OK) )
			{
				psRGXConfigEnableHWPerfCountersOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXConfigEnableHWPerfCounters_exit;
			}

				{
					/* Look up the address from the handle */
					psRGXConfigEnableHWPerfCountersOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psRGXConfigEnableHWPerfCountersIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXConfigEnableHWPerfCountersOUT->eError != PVRSRV_OK)
					{
						goto RGXConfigEnableHWPerfCounters_exit;
					}

				}

	psRGXConfigEnableHWPerfCountersOUT->eError =
		PVRSRVRGXConfigEnableHWPerfCountersKM(
					hDevNodeInt,
					psRGXConfigEnableHWPerfCountersIN->ui32ArrayLen,
					psBlockConfigsInt);



RGXConfigEnableHWPerfCounters_exit:
	if (psBlockConfigsInt)
		OSFreeMem(psBlockConfigsInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXCtrlHWPerfCounters(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXCTRLHWPERFCOUNTERS *psRGXCtrlHWPerfCountersIN,
					 PVRSRV_BRIDGE_OUT_RGXCTRLHWPERFCOUNTERS *psRGXCtrlHWPerfCountersOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	IMG_UINT8 *ui8BlockIDsInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXHWPERF_RGXCTRLHWPERFCOUNTERS);




	if (psRGXCtrlHWPerfCountersIN->ui32ArrayLen != 0)
	{
		ui8BlockIDsInt = OSAllocMem(psRGXCtrlHWPerfCountersIN->ui32ArrayLen * sizeof(IMG_UINT8));
		if (!ui8BlockIDsInt)
		{
			psRGXCtrlHWPerfCountersOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXCtrlHWPerfCounters_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXCtrlHWPerfCountersIN->pui8BlockIDs, psRGXCtrlHWPerfCountersIN->ui32ArrayLen * sizeof(IMG_UINT8))
				|| (OSCopyFromUser(NULL, ui8BlockIDsInt, psRGXCtrlHWPerfCountersIN->pui8BlockIDs,
				psRGXCtrlHWPerfCountersIN->ui32ArrayLen * sizeof(IMG_UINT8)) != PVRSRV_OK) )
			{
				psRGXCtrlHWPerfCountersOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXCtrlHWPerfCounters_exit;
			}

				{
					/* Look up the address from the handle */
					psRGXCtrlHWPerfCountersOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psRGXCtrlHWPerfCountersIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXCtrlHWPerfCountersOUT->eError != PVRSRV_OK)
					{
						goto RGXCtrlHWPerfCounters_exit;
					}

				}

	psRGXCtrlHWPerfCountersOUT->eError =
		PVRSRVRGXCtrlHWPerfCountersKM(
					hDevNodeInt,
					psRGXCtrlHWPerfCountersIN->bEnable,
					psRGXCtrlHWPerfCountersIN->ui32ArrayLen,
					ui8BlockIDsInt);



RGXCtrlHWPerfCounters_exit:
	if (ui8BlockIDsInt)
		OSFreeMem(ui8BlockIDsInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXConfigCustomCounters(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXCONFIGCUSTOMCOUNTERS *psRGXConfigCustomCountersIN,
					 PVRSRV_BRIDGE_OUT_RGXCONFIGCUSTOMCOUNTERS *psRGXConfigCustomCountersOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	IMG_UINT32 *ui32CustomCounterIDsInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXHWPERF_RGXCONFIGCUSTOMCOUNTERS);




	if (psRGXConfigCustomCountersIN->ui16NumCustomCounters != 0)
	{
		ui32CustomCounterIDsInt = OSAllocMem(psRGXConfigCustomCountersIN->ui16NumCustomCounters * sizeof(IMG_UINT32));
		if (!ui32CustomCounterIDsInt)
		{
			psRGXConfigCustomCountersOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXConfigCustomCounters_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXConfigCustomCountersIN->pui32CustomCounterIDs, psRGXConfigCustomCountersIN->ui16NumCustomCounters * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32CustomCounterIDsInt, psRGXConfigCustomCountersIN->pui32CustomCounterIDs,
				psRGXConfigCustomCountersIN->ui16NumCustomCounters * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXConfigCustomCountersOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXConfigCustomCounters_exit;
			}

				{
					/* Look up the address from the handle */
					psRGXConfigCustomCountersOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psRGXConfigCustomCountersIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXConfigCustomCountersOUT->eError != PVRSRV_OK)
					{
						goto RGXConfigCustomCounters_exit;
					}

				}

	psRGXConfigCustomCountersOUT->eError =
		PVRSRVRGXConfigCustomCountersKM(
					hDevNodeInt,
					psRGXConfigCustomCountersIN->ui16CustomBlockID,
					psRGXConfigCustomCountersIN->ui16NumCustomCounters,
					ui32CustomCounterIDsInt);



RGXConfigCustomCounters_exit:
	if (ui32CustomCounterIDsInt)
		OSFreeMem(ui32CustomCounterIDsInt);

	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */
 
PVRSRV_ERROR RegisterRGXHWPERFFunctions(IMG_VOID);
IMG_VOID UnregisterRGXHWPERFFunctions(IMG_VOID);

/*
 * Register all RGXHWPERF functions with services
 */
PVRSRV_ERROR RegisterRGXHWPERFFunctions(IMG_VOID)
{
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXHWPERF_RGXCTRLHWPERF, PVRSRVBridgeRGXCtrlHWPerf);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXHWPERF_RGXCONFIGENABLEHWPERFCOUNTERS, PVRSRVBridgeRGXConfigEnableHWPerfCounters);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXHWPERF_RGXCTRLHWPERFCOUNTERS, PVRSRVBridgeRGXCtrlHWPerfCounters);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXHWPERF_RGXCONFIGCUSTOMCOUNTERS, PVRSRVBridgeRGXConfigCustomCounters);

	return PVRSRV_OK;
}

/*
 * Unregister all rgxhwperf functions with services
 */
IMG_VOID UnregisterRGXHWPERFFunctions(IMG_VOID)
{
}
