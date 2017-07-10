/*************************************************************************/ /*!
@File
@Title          Debugging and miscellaneous functions server implementation
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Kernel services functions for debugging and other
                miscellaneous functionality.
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

#include "pvrsrv.h"
#include "pvr_debug.h"
#include "debugmisc_server.h"
#include "rgxfwutils.h"
#include "rgxta3d.h"
#include "pdump_km.h"
#include "mmu_common.h"
#include "devicemem_server.h"
#include "osfunc.h"

IMG_EXPORT PVRSRV_ERROR
PVRSRVDebugMiscSLCSetBypassStateKM(
	CONNECTION_DATA * psConnection,
	PVRSRV_DEVICE_NODE *psDeviceNode,
	IMG_UINT32  uiFlags,
	IMG_BOOL bSetBypassed)
{
	RGXFWIF_KCCB_CMD  sSLCBPCtlCmd;
	PVRSRV_ERROR  eError = PVRSRV_OK;

	PVR_UNREFERENCED_PARAMETER(psConnection);
	
	sSLCBPCtlCmd.eCmdType = RGXFWIF_KCCB_CMD_SLCBPCTL;
	sSLCBPCtlCmd.uCmdData.sSLCBPCtlData.bSetBypassed = bSetBypassed;
	sSLCBPCtlCmd.uCmdData.sSLCBPCtlData.uiFlags = uiFlags;

	eError = RGXScheduleCommand(psDeviceNode->pvDevice,
	                            RGXFWIF_DM_GP,
	                            &sSLCBPCtlCmd,
	                            sizeof(sSLCBPCtlCmd),
	                            0,
	                            PDUMP_FLAGS_CONTINUOUS);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVDebugMiscSLCSetEnableStateKM: RGXScheduleCommandfailed. Error:%u", eError));
	}
	else
	{
		/* Wait for the SLC flush to complete */
		eError = RGXWaitForFWOp(psDeviceNode->pvDevice, RGXFWIF_DM_GP, psDeviceNode->psSyncPrim, PDUMP_FLAGS_CONTINUOUS);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVDebugMiscSLCSetEnableStateKM: Waiting for value aborted with error (%u)", eError));
		}
	}

	return PVRSRV_OK;
}

IMG_EXPORT PVRSRV_ERROR
PVRSRVRGXDebugMiscQueryFWLogKM(
	const CONNECTION_DATA *psConnection,
	const PVRSRV_DEVICE_NODE *psDeviceNode,
	IMG_UINT32 *pui32RGXFWLogType)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_UNREFERENCED_PARAMETER(psConnection);

#if defined(PVRSRV_GPUVIRT_GUESTDRV)
	/* Guest drivers do not support tracebuf */
	PVR_UNREFERENCED_PARAMETER(psDevInfo);
	PVR_UNREFERENCED_PARAMETER(pui32RGXFWLogType);
	return PVRSRV_ERROR_NOT_IMPLEMENTED;
#else
	if (!psDeviceNode || !pui32RGXFWLogType)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = psDeviceNode->pvDevice;

	if (!psDevInfo || !psDevInfo->psRGXFWIfTraceBuf)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*pui32RGXFWLogType = psDevInfo->psRGXFWIfTraceBuf->ui32LogType;
	return PVRSRV_OK;
#endif
}


IMG_EXPORT PVRSRV_ERROR
PVRSRVRGXDebugMiscSetFWLogKM(
	const CONNECTION_DATA * psConnection,
	const PVRSRV_DEVICE_NODE *psDeviceNode,
	IMG_UINT32  ui32RGXFWLogType)
{
	RGXFWIF_KCCB_CMD sLogTypeUpdateCmd;
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_RGXDEV_INFO* psDevInfo = psDeviceNode->pvDevice;

	PVR_UNREFERENCED_PARAMETER(psConnection);
	
	/* check log type is valid */
	if (ui32RGXFWLogType & ~RGXFWIF_LOG_TYPE_MASK)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

#if defined(PVRSRV_GPUVIRT_GUESTDRV)
	/* Guest drivers do not support tracebuf */
	PVR_UNREFERENCED_PARAMETER(psDevInfo);
	PVR_UNREFERENCED_PARAMETER(sLogTypeUpdateCmd);
	eError = PVRSRV_ERROR_NOT_IMPLEMENTED;
#else
	/* set the new log type */
	psDevInfo->psRGXFWIfTraceBuf->ui32LogType = ui32RGXFWLogType;

	/* Allocate firmware trace buffer resource(s) if not already done */
	if (RGXTraceBufferIsInitRequired(psDevInfo))
	{
		RGXTraceBufferInitOnDemandResources(psDevInfo);
	}

	/* Ask the FW to update its cached version of logType value */
	sLogTypeUpdateCmd.eCmdType = RGXFWIF_KCCB_CMD_LOGTYPE_UPDATE;
	eError = RGXScheduleCommand(psDevInfo,
	                            RGXFWIF_DM_GP,
	                            &sLogTypeUpdateCmd,
	                            sizeof(sLogTypeUpdateCmd),
	                            0,
	                            PDUMP_FLAGS_CONTINUOUS);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: RGXScheduleCommandfailed. Error:%u", __FUNCTION__, eError));
	}
	else
	{
		/* Wait for the LogType value to be updated */
		eError = RGXWaitForFWOp(psDevInfo, RGXFWIF_DM_GP, psDeviceNode->psSyncPrim, PDUMP_FLAGS_CONTINUOUS);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"%s: Waiting for value aborted with error (%u)", __FUNCTION__, eError));
		}
	}
#endif

	return eError;
}

IMG_EXPORT PVRSRV_ERROR
PVRSRVRGXDebugMiscSetHCSDeadlineKM(
	CONNECTION_DATA *psConnection,
	PVRSRV_DEVICE_NODE *psDeviceNode,
	IMG_UINT32  ui32HCSDeadlineMS)
{
	PVRSRV_RGXDEV_INFO* psDevInfo = psDeviceNode->pvDevice;
	PVR_UNREFERENCED_PARAMETER(psConnection);
	
	return RGXFWSetHCSDeadline(psDevInfo, ui32HCSDeadlineMS);
}

IMG_EXPORT PVRSRV_ERROR
PVRSRVRGXDebugMiscSetOSidPriorityKM(
	CONNECTION_DATA *psConnection,
	PVRSRV_DEVICE_NODE *psDeviceNode,
	IMG_UINT32  ui32OSid,
	IMG_UINT32  ui32OSidPriority)
{
	PVRSRV_RGXDEV_INFO* psDevInfo = psDeviceNode->pvDevice;
	PVR_UNREFERENCED_PARAMETER(psConnection);

	return RGXFWChangeOSidPriority(psDevInfo, ui32OSid, ui32OSidPriority);
}

IMG_EXPORT PVRSRV_ERROR
PVRSRVRGXDebugMiscSetOSNewOnlineStateKM(
	CONNECTION_DATA *psConnection,
	PVRSRV_DEVICE_NODE *psDeviceNode,
	IMG_UINT32  ui32OSid,
	IMG_UINT32  ui32OSNewState)
{
	PVRSRV_RGXDEV_INFO* psDevInfo = psDeviceNode->pvDevice;
	PVR_UNREFERENCED_PARAMETER(psConnection);

	if (ui32OSNewState)
	{
		return RGXFWSetVMOnlineState(psDevInfo, ui32OSid, RGXFWIF_OS_ONLINE);
	}

	return RGXFWSetVMOnlineState(psDevInfo, ui32OSid, RGXFWIF_OS_OFFLINE);
}

IMG_EXPORT PVRSRV_ERROR
PVRSRVRGXDebugMiscDumpFreelistPageListKM(
	CONNECTION_DATA * psConnection,
	PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_RGXDEV_INFO* psDevInfo = psDeviceNode->pvDevice;
	DLLIST_NODE *psNode, *psNext;

	PVR_UNREFERENCED_PARAMETER(psConnection);
	
	if (dllist_is_empty(&psDevInfo->sFreeListHead))
	{
		return PVRSRV_OK;
	}

	PVR_LOG(("---------------[ Begin Freelist Page List Dump ]------------------"));

	OSLockAcquire(psDevInfo->hLockFreeList);
	dllist_foreach_node(&psDevInfo->sFreeListHead, psNode, psNext)
	{
		RGX_FREELIST *psFreeList = IMG_CONTAINER_OF(psNode, RGX_FREELIST, sNode);
		RGXDumpFreeListPageList(psFreeList);
	}
	OSLockRelease(psDevInfo->hLockFreeList);

	PVR_LOG(("----------------[ End Freelist Page List Dump ]-------------------"));

	return PVRSRV_OK;

}
