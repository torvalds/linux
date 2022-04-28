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
#include "rgxfwdbg.h"
#include "rgxfwutils.h"
#include "rgxta3d.h"
#include "pdump_km.h"
#include "mmu_common.h"
#include "devicemem_server.h"
#include "osfunc.h"

PVRSRV_ERROR
PVRSRVRGXFWDebugQueryFWLogKM(
	const CONNECTION_DATA *psConnection,
	const PVRSRV_DEVICE_NODE *psDeviceNode,
	IMG_UINT32 *pui32RGXFWLogType)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_IMPLEMENTED);

	if (!psDeviceNode || !pui32RGXFWLogType)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = psDeviceNode->pvDevice;

	if (!psDevInfo || !psDevInfo->psRGXFWIfTraceBufCtl)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*pui32RGXFWLogType = psDevInfo->psRGXFWIfTraceBufCtl->ui32LogType;
	return PVRSRV_OK;
}


PVRSRV_ERROR
PVRSRVRGXFWDebugSetFWLogKM(
	const CONNECTION_DATA * psConnection,
	const PVRSRV_DEVICE_NODE *psDeviceNode,
	IMG_UINT32  ui32RGXFWLogType)
{
	RGXFWIF_KCCB_CMD sLogTypeUpdateCmd;
	PVRSRV_DEV_POWER_STATE ePowerState;
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_RGXDEV_INFO* psDevInfo = psDeviceNode->pvDevice;
	IMG_UINT32 ui32OldRGXFWLogTpe;
	IMG_UINT32 ui32kCCBCommandSlot;
	IMG_BOOL bWaitForFwUpdate = IMG_FALSE;

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_IMPLEMENTED);

	ui32OldRGXFWLogTpe = psDevInfo->psRGXFWIfTraceBufCtl->ui32LogType;

	/* check log type is valid */
	if (ui32RGXFWLogType & ~RGXFWIF_LOG_TYPE_MASK)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	OSLockAcquire(psDevInfo->hRGXFWIfBufInitLock);

	/* set the new log type and ensure the new log type is written to memory
	 * before requesting the FW to read it
	 */
	psDevInfo->psRGXFWIfTraceBufCtl->ui32LogType = ui32RGXFWLogType;
	OSMemoryBarrier();

	/* Allocate firmware trace buffer resource(s) if not already done */
	if (RGXTraceBufferIsInitRequired(psDevInfo))
	{
		eError = RGXTraceBufferInitOnDemandResources(psDevInfo, RGX_FWSHAREDMEM_CPU_RO_ALLOCFLAGS);
	}
#if defined(SUPPORT_TBI_INTERFACE)
	/* Check if LogType is TBI then allocate resource on demand and copy
	 * SFs to it
	 */
	else if (RGXTBIBufferIsInitRequired(psDevInfo))
	{
		eError = RGXTBIBufferInitOnDemandResources(psDevInfo);
	}

	/* TBI buffer address will be 0 if not initialised */
	sLogTypeUpdateCmd.uCmdData.sTBIBuffer = psDevInfo->sRGXFWIfTBIBuffer;
#else
	sLogTypeUpdateCmd.uCmdData.sTBIBuffer.ui32Addr = 0;
#endif

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Failed to allocate resource on-demand. Reverting to old value",
		         __func__));
		psDevInfo->psRGXFWIfTraceBufCtl->ui32LogType = ui32OldRGXFWLogTpe;
		OSMemoryBarrier();

		OSLockRelease(psDevInfo->hRGXFWIfBufInitLock);

		return eError;
	}

	OSLockRelease(psDevInfo->hRGXFWIfBufInitLock);

	eError = PVRSRVPowerLock((const PPVRSRV_DEVICE_NODE) psDeviceNode);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Failed to acquire power lock (%u)",
		         __func__,
		         eError));
		return eError;
	}

	eError = PVRSRVGetDevicePowerState((const PPVRSRV_DEVICE_NODE) psDeviceNode, &ePowerState);

	if ((eError == PVRSRV_OK) && (ePowerState != PVRSRV_DEV_POWER_STATE_OFF))
	{
		/* Ask the FW to update its cached version of logType value */
		sLogTypeUpdateCmd.eCmdType = RGXFWIF_KCCB_CMD_LOGTYPE_UPDATE;

		eError = RGXSendCommandAndGetKCCBSlot(psDevInfo,
											  &sLogTypeUpdateCmd,
											  PDUMP_FLAGS_CONTINUOUS,
											  &ui32kCCBCommandSlot);
		PVR_LOG_GOTO_IF_ERROR(eError, "RGXSendCommandAndGetKCCBSlot", unlock);
		bWaitForFwUpdate = IMG_TRUE;
	}

unlock:
	PVRSRVPowerUnlock((const PPVRSRV_DEVICE_NODE) psDeviceNode);
	if (bWaitForFwUpdate)
	{
		/* Wait for the LogType value to be updated in FW */
		eError = RGXWaitForKCCBSlotUpdate(psDevInfo, ui32kCCBCommandSlot, PDUMP_FLAGS_CONTINUOUS);
		PVR_LOG_IF_ERROR(eError, "RGXWaitForKCCBSlotUpdate");
	}
	return eError;
}

PVRSRV_ERROR
PVRSRVRGXFWDebugSetHCSDeadlineKM(
	CONNECTION_DATA *psConnection,
	PVRSRV_DEVICE_NODE *psDeviceNode,
	IMG_UINT32  ui32HCSDeadlineMS)
{
	PVRSRV_RGXDEV_INFO* psDevInfo = psDeviceNode->pvDevice;
	PVR_UNREFERENCED_PARAMETER(psConnection);

	return RGXFWSetHCSDeadline(psDevInfo, ui32HCSDeadlineMS);
}

PVRSRV_ERROR
PVRSRVRGXFWDebugSetOSidPriorityKM(
	CONNECTION_DATA *psConnection,
	PVRSRV_DEVICE_NODE *psDeviceNode,
	IMG_UINT32  ui32OSid,
	IMG_UINT32  ui32OSidPriority)
{
	PVRSRV_RGXDEV_INFO* psDevInfo = psDeviceNode->pvDevice;
	PVR_UNREFERENCED_PARAMETER(psConnection);

	return RGXFWChangeOSidPriority(psDevInfo, ui32OSid, ui32OSidPriority);
}

PVRSRV_ERROR
PVRSRVRGXFWDebugSetOSNewOnlineStateKM(
	CONNECTION_DATA *psConnection,
	PVRSRV_DEVICE_NODE *psDeviceNode,
	IMG_UINT32  ui32OSid,
	IMG_UINT32  ui32OSNewState)
{
	PVRSRV_RGXDEV_INFO* psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_OS_STATE_CHANGE eState;
	PVR_UNREFERENCED_PARAMETER(psConnection);

	eState = (ui32OSNewState) ? (RGXFWIF_OS_ONLINE) : (RGXFWIF_OS_OFFLINE);
	return RGXFWSetFwOsState(psDevInfo, ui32OSid, eState);
}

PVRSRV_ERROR
PVRSRVRGXFWDebugPHRConfigureKM(
	CONNECTION_DATA *psConnection,
	PVRSRV_DEVICE_NODE *psDeviceNode,
	IMG_UINT32 ui32PHRMode)
{
	PVRSRV_RGXDEV_INFO* psDevInfo = psDeviceNode->pvDevice;
	PVR_UNREFERENCED_PARAMETER(psConnection);

	return RGXFWConfigPHR(psDevInfo,
	                      ui32PHRMode);
}

PVRSRV_ERROR
PVRSRVRGXFWDebugWdgConfigureKM(
	CONNECTION_DATA *psConnection,
	PVRSRV_DEVICE_NODE *psDeviceNode,
	IMG_UINT32 ui32WdgPeriodUs)
{
	PVRSRV_RGXDEV_INFO* psDevInfo = psDeviceNode->pvDevice;
	PVR_UNREFERENCED_PARAMETER(psConnection);

	return RGXFWConfigWdg(psDevInfo,
	                      ui32WdgPeriodUs);
}

PVRSRV_ERROR
PVRSRVRGXFWDebugDumpFreelistPageListKM(
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
