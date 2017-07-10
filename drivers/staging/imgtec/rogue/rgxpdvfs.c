/*************************************************************************/ /*!
@File           rgxpdvfs.c
@Title          RGX Proactive DVFS Functionality
@Codingstyle    IMG
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Kernel mode Proactive DVFS Functionality.
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

#include "rgxpdvfs.h"
#include "rgxfwutils.h"

#define USEC_TO_MSEC 1000

PVRSRV_ERROR PDVFSLimitMaxFrequency(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32MaxOPPPoint)
{
	RGXFWIF_KCCB_CMD		sGPCCBCmd;
	PVRSRV_ERROR			eError;

	if(psDevInfo->bPDVFSEnabled != IMG_TRUE)
	{
		/* No error message to avoid excessive messages */
		return PVRSRV_OK;
	}

	/* send feedback */
	sGPCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_PDVFS_LIMIT_MAX_FREQ;
	sGPCCBCmd.uCmdData.sPDVFSMaxFreqData.ui32MaxOPPPoint = ui32MaxOPPPoint;


	/* Submit command to the firmware.  */
	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		eError = RGXScheduleCommand(psDevInfo,
									RGXFWIF_DM_GP,
									&sGPCCBCmd,
									sizeof(sGPCCBCmd),
									0,
									PDUMP_FLAGS_CONTINUOUS);
		if (eError != PVRSRV_ERROR_RETRY)
		{
			break;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} END_LOOP_UNTIL_TIMEOUT();
	
	return PVRSRV_OK;
}


void PDVFSRequestReactiveUpdate(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	RGXFWIF_KCCB_CMD sGPCCBCmd;
	PVRSRV_ERROR eError;

	if(psDevInfo->bPDVFSEnabled != IMG_TRUE)
	{
		/* No error message to avoid excessive messages */
		return;
	}

	if(psDevInfo->psDeviceNode->psDevConfig->sDVFS.sPDVFSData.bWorkInFrame == IMG_FALSE)
	{
		return;
	}

	sGPCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_PDVFS_REQUEST_REACTIVE_UPDATE;

	/* Submit command to the firmware.  */
	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		eError = RGXScheduleCommand(psDevInfo,
		                            RGXFWIF_DM_GP,
		                            &sGPCCBCmd,
		                            sizeof(sGPCCBCmd),
		                            0,
		                            PDUMP_FLAGS_CONTINUOUS);
		if (eError != PVRSRV_ERROR_RETRY)
		{
			break;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} END_LOOP_UNTIL_TIMEOUT();

	psDevInfo->psDeviceNode->psDevConfig->sDVFS.sPDVFSData.bWorkInFrame = IMG_FALSE;
}

/*************************************************************************/ /*!
@Function       PDVFSProcessCoreClkRateChange
@Description    Processes the core clock rate change request or notification.
                Processes as notification, if SUPPORT_PDVFS_GPIO feature is enabled
                i.e. firmware (PDVFS) can use GPIO to change core clock rate
                else processes as request (uses system layer API to change core
                clock rate)
@Input          psDevInfo            A pointer to PVRSRV_RGXDEV_INFO.
@Input          ui32CoreClockRate    New core clock rate.
@Return         PVRSRV_ERROR.
*/ /**************************************************************************/
PVRSRV_ERROR PDVFSProcessCoreClkRateChange(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32CoreClockRate)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig = psDevInfo->psDeviceNode->psDevConfig;
	IMG_DVFS_DEVICE_CFG *psDVFSDeviceCfg = &psDevConfig->sDVFS.sDVFSDeviceCfg;
	RGX_TIMING_INFORMATION *psRGXTimingInfo = ((RGX_DATA*)(psDevConfig->hDevData))->psRGXTimingInfo;
	PVRSRV_ERROR eError;

#if !defined (SUPPORT_PDVFS_GPIO)
	IMG_UINT32 ui32CoreClockRateCurrent = psRGXTimingInfo->ui32CoreClockSpeed;
#endif
	IMG_UINT32 ui32Index;
	const IMG_OPP *psOpp = NULL;


	if(psDevInfo->bPDVFSEnabled != IMG_TRUE)
	{
		/* No error message to avoid excessive messages */
		return PVRSRV_OK;
	}

	PVR_DPF((PVR_DBG_MESSAGE,"Core clock rate = %u\n", ui32CoreClockRate));

	/**
	 * Find the matching OPP (Exact).
	 */
	for (ui32Index = 0; ui32Index < psDVFSDeviceCfg->ui32OPPTableSize; ui32Index++)
	{
		if (ui32CoreClockRate == psDVFSDeviceCfg->pasOPPTable[ui32Index].ui32Freq)
		{
			psOpp = &psDVFSDeviceCfg->pasOPPTable[ui32Index];
			break;
		}
	}

	if (!psOpp)
	{
		PVR_DPF((PVR_DBG_ERROR, "Frequency not present in OPP table - %u", ui32CoreClockRate));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = PVRSRVDevicePreClockSpeedChange(psDevInfo->psDeviceNode, psDVFSDeviceCfg->bIdleReq, NULL);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVDevicePreClockSpeedChange failed"));
		return eError;
	}

#if !defined (SUPPORT_PDVFS_GPIO)
	/**
	 * Increasing frequency, change voltage first
	 */
	if(ui32CoreClockRate > ui32CoreClockRateCurrent)
	{
		psDVFSDeviceCfg->pfnSetVoltage(psOpp->ui32Volt);
	}

	psDVFSDeviceCfg->pfnSetFrequency(ui32CoreClockRate);

	/**
	 * Decreasing frequency, change frequency first
	 */
	if (ui32CoreClockRate < ui32CoreClockRateCurrent)
	{
		psDVFSDeviceCfg->pfnSetVoltage(psOpp->ui32Volt);
	}
#endif

	psRGXTimingInfo->ui32CoreClockSpeed = ui32CoreClockRate;

	PVRSRVDevicePostClockSpeedChange(psDevInfo->psDeviceNode, psDVFSDeviceCfg->bIdleReq, NULL);

	return PVRSRV_OK;
}

#if defined (RGXFW_META_SUPPORT_2ND_THREAD)
/*************************************************************************/ /*!
@Function       RGXPDVFSCheckCoreClkRateChange 
@Description    Checks if core clock rate has changed since the last snap-shot.
@Input          psDevInfo    A pointer to PVRSRV_RGXDEV_INFO.
@Return         None.
*/ /**************************************************************************/
void RGXPDVFSCheckCoreClkRateChange(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	IMG_UINT32 ui32CoreClkRate = *(psDevInfo->pui32RGXFWIFCoreClkRate);

	if(psDevInfo->bPDVFSEnabled != IMG_TRUE)
	{
		/* No error message to avoid excessive messages */
		return;
	}

	if ((ui32CoreClkRate != 0) &&
		(psDevInfo->ui32CoreClkRateSnapshot != ui32CoreClkRate))
	{
		psDevInfo->ui32CoreClkRateSnapshot = ui32CoreClkRate;
		PDVFSProcessCoreClkRateChange(psDevInfo, ui32CoreClkRate);
	}
}
#endif
