/*************************************************************************/ /*!
@File
@Title          RGX Timer queries
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX Regconfig routines
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

#include "rgxtimerquery.h"
#include "rgxdevice.h"

#include "rgxfwutils.h"

PVRSRV_ERROR
PVRSRVRGXBeginTimerQueryKM(PVRSRV_DEVICE_NODE * psDeviceNode,
                           IMG_UINT32         ui32QueryId)
{
	PVRSRV_RGXDEV_INFO * psDevInfo = (PVRSRV_RGXDEV_INFO *)psDeviceNode->pvDevice;

	if (ui32QueryId >= RGX_MAX_TIMER_QUERIES)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo->bSaveStart = IMG_TRUE;
	psDevInfo->bSaveEnd   = IMG_TRUE;

	/* clear the stamps, in case there is no Kick */
	psDevInfo->pui64StartTimeById[ui32QueryId] = 0UL;
	psDevInfo->pui64EndTimeById[ui32QueryId]   = 0UL;

	/* save of the active query index */
	psDevInfo->ui32ActiveQueryId = ui32QueryId;

	return PVRSRV_OK;
}


PVRSRV_ERROR
PVRSRVRGXEndTimerQueryKM(PVRSRV_DEVICE_NODE * psDeviceNode)
{
	PVRSRV_RGXDEV_INFO * psDevInfo = (PVRSRV_RGXDEV_INFO *)psDeviceNode->pvDevice;

	/* clear off the flags set by Begin(). Note that _START_TIME is
	 * probably already cleared by Kick()
	 */
	psDevInfo->bSaveStart = IMG_FALSE;
	psDevInfo->bSaveEnd   = IMG_FALSE;

	return PVRSRV_OK;
}


PVRSRV_ERROR
PVRSRVRGXQueryTimerKM(PVRSRV_DEVICE_NODE * psDeviceNode,
                      IMG_UINT32         ui32QueryId,
                      IMG_UINT64         * pui64StartTime,
                      IMG_UINT64         * pui64EndTime)
{
	PVRSRV_RGXDEV_INFO * psDevInfo = (PVRSRV_RGXDEV_INFO *)psDeviceNode->pvDevice;
	IMG_UINT32         ui32Scheduled;
	IMG_UINT32         ui32Completed;

	if (ui32QueryId >= RGX_MAX_TIMER_QUERIES)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	ui32Scheduled = psDevInfo->aui32ScheduledOnId[ui32QueryId];
	ui32Completed = psDevInfo->pui32CompletedById[ui32QueryId];

	/* if there was no kick since the Begin() on this id we return 0-s as Begin cleared
	 * the stamps. If there was no begin the returned data is undefined - but still
	 * safe from services pov
	 */
	if (ui32Completed >= ui32Scheduled)
	{
		* pui64StartTime = psDevInfo->pui64StartTimeById[ui32QueryId];
		* pui64EndTime   = psDevInfo->pui64EndTimeById[ui32QueryId];

		return PVRSRV_OK;
	}
	else
	{
		return PVRSRV_ERROR_RESOURCE_UNAVAILABLE;
	}
}


PVRSRV_ERROR
PVRSRVRGXCurrentTime(PVRSRV_DEVICE_NODE * psDeviceNode,
                     IMG_UINT64         * pui64Time)
{
	IMG_UINT64         ui64Result;
	PVRSRV_RGXDEV_INFO * psDevInfo = (PVRSRV_RGXDEV_INFO *)psDeviceNode->pvDevice;
	PVRSRV_ERROR       eError;

	eError = PVRSRVPowerLock();

	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCurrentTime: Failed to acquire PowerLock (device index: %d, error: %s)", 
		         psDeviceNode->sDevId.ui32DeviceIndex,
		         PVRSRVGetErrorStringKM(eError)));
		return eError;
	}

	eError = PVRSRVSetDevicePowerStateKM(psDeviceNode->sDevId.ui32DeviceIndex, PVRSRV_DEV_POWER_STATE_ON, IMG_TRUE); /* forced */

	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	ui64Result = RGXReadHWTimerReg(psDevInfo);

	PVRSRVPowerUnlock();

	*pui64Time = ui64Result;

	return PVRSRV_OK;
}



/******************************************************************************
 NOT BRIDGED/EXPORTED FUNCS
******************************************************************************/
/* writes a time stamp command in the client CCB */
IMG_VOID
RGXWriteTimestampCommand(IMG_PBYTE               * ppbyPtr,
                         RGXFWIF_CCB_CMD_TYPE    eCmdType,
                         PRGXFWIF_TIMESTAMP_ADDR pAddr)
{
	RGXFWIF_CCB_CMD_HEADER * psHeader;

	psHeader = (RGXFWIF_CCB_CMD_HEADER *) (*ppbyPtr);

	PVR_ASSERT(eCmdType == RGXFWIF_CCB_CMD_TYPE_PRE_TIMESTAMP
	           || eCmdType == RGXFWIF_CCB_CMD_TYPE_POST_TIMESTAMP);

	psHeader->eCmdType    = eCmdType;
	psHeader->ui32CmdSize = (sizeof(RGXFWIF_DEV_VIRTADDR) + RGXFWIF_FWALLOC_ALIGN - 1) & ~(RGXFWIF_FWALLOC_ALIGN  - 1);

	(*ppbyPtr) += sizeof(RGXFWIF_CCB_CMD_HEADER);
 
	(*(PRGXFWIF_TIMESTAMP_ADDR*)*ppbyPtr) = pAddr;

	(*ppbyPtr) += psHeader->ui32CmdSize;
}


IMG_VOID
RGX_GetTimestampCmdHelper(PVRSRV_RGXDEV_INFO      * psDevInfo,
                          PRGXFWIF_TIMESTAMP_ADDR * ppPreAddr,
                          PRGXFWIF_TIMESTAMP_ADDR * ppPostAddr,
                          PRGXFWIF_UFO_ADDR       * ppUpdate)
{
	if (ppPreAddr != IMG_NULL)
	{
		if (psDevInfo->bSaveStart)
		{
			/* drop the SaveStart on the first Kick */
			psDevInfo->bSaveStart = IMG_FALSE;

			RGXSetFirmwareAddress(ppPreAddr,
			                      psDevInfo->psStartTimeMemDesc,
			                      sizeof(IMG_UINT64) * psDevInfo->ui32ActiveQueryId,
			                      RFW_FWADDR_NOREF_FLAG);
		}
		else
		{
			ppPreAddr->ui32Addr = 0;
		}
	}

	if (ppPostAddr != IMG_NULL && ppUpdate != IMG_NULL)
	{
		if (psDevInfo->bSaveEnd)
		{
			RGXSetFirmwareAddress(ppPostAddr,
			                      psDevInfo->psEndTimeMemDesc,
			                      sizeof(IMG_UINT64) * psDevInfo->ui32ActiveQueryId,
			                      RFW_FWADDR_NOREF_FLAG);

			psDevInfo->aui32ScheduledOnId[psDevInfo->ui32ActiveQueryId]++;

			RGXSetFirmwareAddress(ppUpdate,
			                      psDevInfo->psCompletedMemDesc,
			                      sizeof(IMG_UINT32) * psDevInfo->ui32ActiveQueryId,
			                      RFW_FWADDR_NOREF_FLAG);
		}
		else
		{
			ppUpdate->ui32Addr   = 0;
			ppPostAddr->ui32Addr = 0;
		}
	}
}


/******************************************************************************
 End of file (rgxtimerquery.c)
******************************************************************************/
