/*************************************************************************/ /*!
@File
@Title          RGX Breakpoint routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX Breakpoint routines
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

#include "rgxbreakpoint.h"
#include "pvr_debug.h"
#include "rgxutils.h"
#include "rgxfwutils.h"
#include "rgxmem.h"
#include "device.h"
#include "sync_internal.h"
#include "pdump_km.h"
#include "pvrsrv.h"

PVRSRV_ERROR PVRSRVRGXSetBreakpointKM(CONNECTION_DATA    * psConnection,
                                      PVRSRV_DEVICE_NODE * psDeviceNode,
                                      IMG_HANDLE           hMemCtxPrivData,
                                      RGXFWIF_DM           eFWDataMaster,
                                      IMG_UINT64           ui64TempSpillingAddr,
                                      IMG_UINT32           ui32BPAddr,
                                      IMG_UINT32           ui32HandlerAddr,
                                      IMG_UINT32           ui32DataMaster)
{
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	DEVMEM_MEMDESC		*psFWMemContextMemDesc = RGXGetFWMemDescFromMemoryContextHandle(hMemCtxPrivData);
	PVRSRV_ERROR		eError = PVRSRV_OK;
	RGXFWIF_KCCB_CMD	sBPCmd;
	IMG_UINT32			ui32kCCBCommandSlot;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	OSLockAcquire(psDevInfo->hBPLock);

	if (psDevInfo->bBPSet)
	{
		eError = PVRSRV_ERROR_BP_ALREADY_SET;
		goto unlock;
	}

	sBPCmd.eCmdType = RGXFWIF_KCCB_CMD_BP;
	sBPCmd.uCmdData.sBPData.ui32BPAddr = ui32BPAddr;
	sBPCmd.uCmdData.sBPData.ui32HandlerAddr = ui32HandlerAddr;
	sBPCmd.uCmdData.sBPData.ui32BPDM = ui32DataMaster;
	sBPCmd.uCmdData.sBPData.ui64SpillAddr = ui64TempSpillingAddr;
	sBPCmd.uCmdData.sBPData.ui32BPDataFlags = RGXFWIF_BPDATA_FLAGS_WRITE | RGXFWIF_BPDATA_FLAGS_ENABLE;
	sBPCmd.uCmdData.sBPData.eDM = eFWDataMaster;

	eError = RGXSetFirmwareAddress(&sBPCmd.uCmdData.sBPData.psFWMemContext,
				psFWMemContextMemDesc,
				0 ,
				RFW_FWADDR_NOREF_FLAG);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXSetFirmwareAddress", unlock);

	eError = RGXScheduleCommandAndGetKCCBSlot(psDevInfo,
	                                          eFWDataMaster,
	                                          &sBPCmd,
	                                          PDUMP_FLAGS_CONTINUOUS,
	                                          &ui32kCCBCommandSlot);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXScheduleCommandAndGetKCCBSlot", unlock);

	/* Wait for FW to complete command execution */
	eError = RGXWaitForKCCBSlotUpdate(psDevInfo, ui32kCCBCommandSlot, PDUMP_FLAGS_CONTINUOUS);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXWaitForKCCBSlotUpdate", unlock);

	psDevInfo->eBPDM = eFWDataMaster;
	psDevInfo->bBPSet = IMG_TRUE;

unlock:
	OSLockRelease(psDevInfo->hBPLock);

	return eError;
}

PVRSRV_ERROR PVRSRVRGXClearBreakpointKM(CONNECTION_DATA    * psConnection,
                                        PVRSRV_DEVICE_NODE * psDeviceNode,
                                        IMG_HANDLE           hMemCtxPrivData)
{
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	DEVMEM_MEMDESC		*psFWMemContextMemDesc = RGXGetFWMemDescFromMemoryContextHandle(hMemCtxPrivData);
	PVRSRV_ERROR		eError = PVRSRV_OK;
	RGXFWIF_KCCB_CMD	sBPCmd;
	IMG_UINT32			ui32kCCBCommandSlot;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	sBPCmd.eCmdType = RGXFWIF_KCCB_CMD_BP;
	sBPCmd.uCmdData.sBPData.ui32BPAddr = 0;
	sBPCmd.uCmdData.sBPData.ui32HandlerAddr = 0;
	sBPCmd.uCmdData.sBPData.ui32BPDataFlags = RGXFWIF_BPDATA_FLAGS_WRITE | RGXFWIF_BPDATA_FLAGS_CTL;
	sBPCmd.uCmdData.sBPData.eDM = psDevInfo->eBPDM;

	OSLockAcquire(psDevInfo->hBPLock);

	eError = RGXSetFirmwareAddress(&sBPCmd.uCmdData.sBPData.psFWMemContext,
				psFWMemContextMemDesc,
				0 ,
				RFW_FWADDR_NOREF_FLAG);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXSetFirmwareAddress", unlock);

	eError = RGXScheduleCommandAndGetKCCBSlot(psDevInfo,
	                                          psDevInfo->eBPDM,
	                                          &sBPCmd,
	                                          PDUMP_FLAGS_CONTINUOUS,
	                                          &ui32kCCBCommandSlot);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXScheduleCommandAndGetKCCBSlot", unlock);

	/* Wait for FW to complete command execution */
	eError = RGXWaitForKCCBSlotUpdate(psDevInfo, ui32kCCBCommandSlot, PDUMP_FLAGS_CONTINUOUS);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXWaitForKCCBSlotUpdate", unlock);

	psDevInfo->bBPSet = IMG_FALSE;

unlock:
	OSLockRelease(psDevInfo->hBPLock);

	return eError;
}

PVRSRV_ERROR PVRSRVRGXEnableBreakpointKM(CONNECTION_DATA    * psConnection,
                                         PVRSRV_DEVICE_NODE * psDeviceNode,
                                         IMG_HANDLE           hMemCtxPrivData)
{
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	DEVMEM_MEMDESC		*psFWMemContextMemDesc = RGXGetFWMemDescFromMemoryContextHandle(hMemCtxPrivData);
	PVRSRV_ERROR		eError = PVRSRV_OK;
	RGXFWIF_KCCB_CMD	sBPCmd;
	IMG_UINT32			ui32kCCBCommandSlot;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	OSLockAcquire(psDevInfo->hBPLock);

	if (psDevInfo->bBPSet == IMG_FALSE)
	{
		eError = PVRSRV_ERROR_BP_NOT_SET;
		goto unlock;
	}

	sBPCmd.eCmdType = RGXFWIF_KCCB_CMD_BP;
	sBPCmd.uCmdData.sBPData.ui32BPDataFlags = RGXFWIF_BPDATA_FLAGS_CTL | RGXFWIF_BPDATA_FLAGS_ENABLE;
	sBPCmd.uCmdData.sBPData.eDM = psDevInfo->eBPDM;

	eError = RGXSetFirmwareAddress(&sBPCmd.uCmdData.sBPData.psFWMemContext,
				psFWMemContextMemDesc,
				0 ,
				RFW_FWADDR_NOREF_FLAG);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXSetFirmwareAddress", unlock);

	eError = RGXScheduleCommandAndGetKCCBSlot(psDevInfo,
	                                          psDevInfo->eBPDM,
	                                          &sBPCmd,
	                                          PDUMP_FLAGS_CONTINUOUS,
	                                          &ui32kCCBCommandSlot);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXScheduleCommandAndGetKCCBSlot", unlock);

	/* Wait for FW to complete command execution */
	eError = RGXWaitForKCCBSlotUpdate(psDevInfo, ui32kCCBCommandSlot, PDUMP_FLAGS_CONTINUOUS);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXWaitForKCCBSlotUpdate", unlock);

unlock:
	OSLockRelease(psDevInfo->hBPLock);

	return eError;
}

PVRSRV_ERROR PVRSRVRGXDisableBreakpointKM(CONNECTION_DATA    * psConnection,
                                          PVRSRV_DEVICE_NODE * psDeviceNode,
                                          IMG_HANDLE           hMemCtxPrivData)
{
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	DEVMEM_MEMDESC		*psFWMemContextMemDesc = RGXGetFWMemDescFromMemoryContextHandle(hMemCtxPrivData);
	PVRSRV_ERROR		eError = PVRSRV_OK;
	RGXFWIF_KCCB_CMD	sBPCmd;
	IMG_UINT32			ui32kCCBCommandSlot;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	OSLockAcquire(psDevInfo->hBPLock);

	if (psDevInfo->bBPSet == IMG_FALSE)
	{
		eError = PVRSRV_ERROR_BP_NOT_SET;
		goto unlock;
	}

	sBPCmd.eCmdType = RGXFWIF_KCCB_CMD_BP;
	sBPCmd.uCmdData.sBPData.ui32BPDataFlags = RGXFWIF_BPDATA_FLAGS_CTL;
	sBPCmd.uCmdData.sBPData.eDM = psDevInfo->eBPDM;

	eError = RGXSetFirmwareAddress(&sBPCmd.uCmdData.sBPData.psFWMemContext,
				psFWMemContextMemDesc,
				0 ,
				RFW_FWADDR_NOREF_FLAG);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXSetFirmwareAddress", unlock);

	eError = RGXScheduleCommandAndGetKCCBSlot(psDevInfo,
	                                          psDevInfo->eBPDM,
	                                          &sBPCmd,
	                                          PDUMP_FLAGS_CONTINUOUS,
	                                          &ui32kCCBCommandSlot);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXScheduleCommandAndGetKCCBSlot", unlock);

	/* Wait for FW to complete command execution */
	eError = RGXWaitForKCCBSlotUpdate(psDevInfo, ui32kCCBCommandSlot, PDUMP_FLAGS_CONTINUOUS);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXWaitForKCCBSlotUpdate", unlock);

unlock:
	OSLockRelease(psDevInfo->hBPLock);

	return eError;
}

PVRSRV_ERROR PVRSRVRGXOverallocateBPRegistersKM(CONNECTION_DATA    * psConnection,
                                                PVRSRV_DEVICE_NODE * psDeviceNode,
                                                IMG_UINT32           ui32TempRegs,
                                                IMG_UINT32           ui32SharedRegs)
{
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_ERROR		eError = PVRSRV_OK;
	RGXFWIF_KCCB_CMD	sBPCmd;
	IMG_UINT32			ui32kCCBCommandSlot;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	sBPCmd.eCmdType = RGXFWIF_KCCB_CMD_BP;
	sBPCmd.uCmdData.sBPData.ui32BPDataFlags = RGXFWIF_BPDATA_FLAGS_REGS;
	sBPCmd.uCmdData.sBPData.ui32TempRegs = ui32TempRegs;
	sBPCmd.uCmdData.sBPData.ui32SharedRegs = ui32SharedRegs;
	sBPCmd.uCmdData.sBPData.psFWMemContext.ui32Addr = 0U;
	sBPCmd.uCmdData.sBPData.eDM = RGXFWIF_DM_GP;

	OSLockAcquire(psDevInfo->hBPLock);

	eError = RGXScheduleCommandAndGetKCCBSlot(psDeviceNode->pvDevice,
	                                          RGXFWIF_DM_GP,
	                                          &sBPCmd,
	                                          PDUMP_FLAGS_CONTINUOUS,
	                                          &ui32kCCBCommandSlot);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXScheduleCommandAndGetKCCBSlot", unlock);

	/* Wait for FW to complete command execution */
	eError = RGXWaitForKCCBSlotUpdate(psDevInfo, ui32kCCBCommandSlot, PDUMP_FLAGS_CONTINUOUS);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXWaitForKCCBSlotUpdate", unlock);

unlock:
	OSLockRelease(psDevInfo->hBPLock);

	return eError;
}

/******************************************************************************
 End of file (rgxbreakpoint.c)
******************************************************************************/
