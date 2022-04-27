/*************************************************************************/ /*!
@File           rgxworkest.c
@Title          RGX Workload Estimation Functionality
@Codingstyle    IMG
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Kernel mode workload estimation functionality.
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

#include "rgxdevice.h"
#include "rgxworkest.h"
#include "rgxfwutils.h"
#include "rgxpdvfs.h"
#include "rgx_options.h"
#include "device.h"
#include "hash.h"
#include "pvr_debug.h"

#define ROUND_DOWN_TO_NEAREST_1024(number) (((number) >> 10) << 10)

static inline IMG_BOOL _WorkEstEnabled(void)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	if (psPVRSRVData->sDriverInfo.sKMBuildInfo.ui32BuildOptions &
	    psPVRSRVData->sDriverInfo.sUMBuildInfo.ui32BuildOptions &
	    OPTIONS_WORKLOAD_ESTIMATION_MASK)
	{
		return IMG_TRUE;
	}

	return IMG_FALSE;
}

static inline IMG_UINT32 _WorkEstDoHash(IMG_UINT32 ui32Input)
{
	IMG_UINT32 ui32HashPart;

	/* Hash function borrowed from hash.c */
	ui32HashPart = ui32Input;
	ui32HashPart += (ui32HashPart << 12);
	ui32HashPart ^= (ui32HashPart >> 22);
	ui32HashPart += (ui32HashPart << 4);
	ui32HashPart ^= (ui32HashPart >> 9);
	ui32HashPart += (ui32HashPart << 10);
	ui32HashPart ^= (ui32HashPart >> 2);
	ui32HashPart += (ui32HashPart << 7);
	ui32HashPart ^= (ui32HashPart >> 12);

	return ui32HashPart;
}

/*! Hash functions for TA/3D workload estimation */
IMG_BOOL WorkEstHashCompareTA3D(size_t uKeySize, void *pKey1, void *pKey2);
IMG_UINT32 WorkEstHashFuncTA3D(size_t uKeySize, void *pKey, IMG_UINT32 uHashTabLen);

/*! Hash functions for compute workload estimation */
IMG_BOOL WorkEstHashCompareCompute(size_t uKeySize, void *pKey1, void *pKey2);
IMG_UINT32 WorkEstHashFuncCompute(size_t uKeySize, void *pKey, IMG_UINT32 uHashTabLen);

/*! Hash functions for TDM/transfer workload estimation */
IMG_BOOL WorkEstHashCompareTDM(size_t uKeySize, void *pKey1, void *pKey2);
IMG_UINT32 WorkEstHashFuncTDM(size_t uKeySize, void *pKey, IMG_UINT32 uHashTabLen);


IMG_BOOL WorkEstHashCompareTA3D(size_t uKeySize, void *pKey1, void *pKey2)
{
	RGX_WORKLOAD *psWorkload1;
	RGX_WORKLOAD *psWorkload2;
	PVR_UNREFERENCED_PARAMETER(uKeySize);

	if (pKey1 && pKey2)
	{
		psWorkload1 = *((RGX_WORKLOAD **)pKey1);
		psWorkload2 = *((RGX_WORKLOAD **)pKey2);

		PVR_ASSERT(psWorkload1);
		PVR_ASSERT(psWorkload2);

		if (psWorkload1->sTA3D.ui32RenderTargetSize  == psWorkload2->sTA3D.ui32RenderTargetSize &&
		    psWorkload1->sTA3D.ui32NumberOfDrawCalls == psWorkload2->sTA3D.ui32NumberOfDrawCalls &&
		    psWorkload1->sTA3D.ui32NumberOfIndices   == psWorkload2->sTA3D.ui32NumberOfIndices &&
		    psWorkload1->sTA3D.ui32NumberOfMRTs      == psWorkload2->sTA3D.ui32NumberOfMRTs)
		{
			/* This is added to allow this memory to be freed */
			*(uintptr_t*)pKey2 = *(uintptr_t*)pKey1;
			return IMG_TRUE;
		}
	}

	return IMG_FALSE;
}

IMG_UINT32 WorkEstHashFuncTA3D(size_t uKeySize, void *pKey, IMG_UINT32 uHashTabLen)
{
	RGX_WORKLOAD *psWorkload = *((RGX_WORKLOAD**)pKey);
	IMG_UINT32 ui32HashKey = 0;
	PVR_UNREFERENCED_PARAMETER(uHashTabLen);
	PVR_UNREFERENCED_PARAMETER(uKeySize);

	/* Hash key predicated on multiple render target attributes */
	ui32HashKey += _WorkEstDoHash(psWorkload->sTA3D.ui32RenderTargetSize);
	ui32HashKey += _WorkEstDoHash(psWorkload->sTA3D.ui32NumberOfDrawCalls);
	ui32HashKey += _WorkEstDoHash(psWorkload->sTA3D.ui32NumberOfIndices);
	ui32HashKey += _WorkEstDoHash(psWorkload->sTA3D.ui32NumberOfMRTs);

	return ui32HashKey;
}

IMG_BOOL WorkEstHashCompareCompute(size_t uKeySize, void *pKey1, void *pKey2)
{
	RGX_WORKLOAD *psWorkload1;
	RGX_WORKLOAD *psWorkload2;
	PVR_UNREFERENCED_PARAMETER(uKeySize);

	if (pKey1 && pKey2)
	{
		psWorkload1 = *((RGX_WORKLOAD **)pKey1);
		psWorkload2 = *((RGX_WORKLOAD **)pKey2);

		PVR_ASSERT(psWorkload1);
		PVR_ASSERT(psWorkload2);

		if (psWorkload1->sCompute.ui32NumberOfWorkgroups == psWorkload2->sCompute.ui32NumberOfWorkgroups &&
		    psWorkload1->sCompute.ui32NumberOfWorkitems  == psWorkload2->sCompute.ui32NumberOfWorkitems)
		{
			/* This is added to allow this memory to be freed */
			*(uintptr_t*)pKey2 = *(uintptr_t*)pKey1;
			return IMG_TRUE;
		}
	}

	return IMG_FALSE;
}

IMG_UINT32 WorkEstHashFuncCompute(size_t uKeySize, void *pKey, IMG_UINT32 uHashTabLen)
{
	RGX_WORKLOAD *psWorkload = *((RGX_WORKLOAD**)pKey);
	IMG_UINT32 ui32HashKey = 0;
	PVR_UNREFERENCED_PARAMETER(uHashTabLen);
	PVR_UNREFERENCED_PARAMETER(uKeySize);

	/* Hash key predicated on multiple render target attributes */
	ui32HashKey += _WorkEstDoHash(psWorkload->sCompute.ui32NumberOfWorkgroups);
	ui32HashKey += _WorkEstDoHash(psWorkload->sCompute.ui32NumberOfWorkitems);
	return ui32HashKey;
}

IMG_BOOL WorkEstHashCompareTDM(size_t uKeySize, void *pKey1, void *pKey2)
{
	RGX_WORKLOAD *psWorkload1;
	RGX_WORKLOAD *psWorkload2;
	PVR_UNREFERENCED_PARAMETER(uKeySize);

	if (pKey1 && pKey2)
	{
		psWorkload1 = *((RGX_WORKLOAD **)pKey1);
		psWorkload2 = *((RGX_WORKLOAD **)pKey2);

		PVR_ASSERT(psWorkload1);
		PVR_ASSERT(psWorkload2);

		if (psWorkload1->sTransfer.ui32Characteristic1 == psWorkload2->sTransfer.ui32Characteristic1 &&
		    psWorkload1->sTransfer.ui32Characteristic2 == psWorkload2->sTransfer.ui32Characteristic2)
		{
			/* This is added to allow this memory to be freed */
			*(uintptr_t*)pKey2 = *(uintptr_t*)pKey1;
			return IMG_TRUE;
		}
	}

	return IMG_FALSE;
}

IMG_UINT32 WorkEstHashFuncTDM(size_t uKeySize, void *pKey, IMG_UINT32 uHashTabLen)
{
	RGX_WORKLOAD *psWorkload = *((RGX_WORKLOAD**)pKey);
	IMG_UINT32 ui32HashKey = 0;
	PVR_UNREFERENCED_PARAMETER(uHashTabLen);
	PVR_UNREFERENCED_PARAMETER(uKeySize);

	/* Hash key predicated on transfer src/dest attributes */
	ui32HashKey += _WorkEstDoHash(psWorkload->sTransfer.ui32Characteristic1);
	ui32HashKey += _WorkEstDoHash(psWorkload->sTransfer.ui32Characteristic2);

	return ui32HashKey;
}

void WorkEstHashLockCreate(POS_LOCK *ppsHashLock)
{
	if (*ppsHashLock == NULL)
	{
		OSLockCreate(ppsHashLock);
	}
}

void WorkEstHashLockDestroy(POS_LOCK psWorkEstHashLock)
{
	if (psWorkEstHashLock != NULL)
	{
		OSLockDestroy(psWorkEstHashLock);
		psWorkEstHashLock = NULL;
	}
}

void WorkEstCheckFirmwareCCB(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	RGXFWIF_WORKEST_FWCCB_CMD *psFwCCBCmd;
	IMG_UINT8 *psFWCCB = psDevInfo->psWorkEstFirmwareCCB;
	RGXFWIF_CCB_CTL *psFWCCBCtl = psDevInfo->psWorkEstFirmwareCCBCtl;

	while (psFWCCBCtl->ui32ReadOffset != psFWCCBCtl->ui32WriteOffset)
	{
		PVRSRV_ERROR eError;

		/* Point to the next command */
		psFwCCBCmd = (RGXFWIF_WORKEST_FWCCB_CMD *)((uintptr_t)psFWCCB + psFWCCBCtl->ui32ReadOffset * sizeof(RGXFWIF_WORKEST_FWCCB_CMD));

		eError = WorkEstRetire(psDevInfo, psFwCCBCmd);
		PVR_LOG_IF_ERROR(eError, "WorkEstCheckFirmwareCCB: WorkEstRetire failed");

		/* Update read offset */
		psFWCCBCtl->ui32ReadOffset = (psFWCCBCtl->ui32ReadOffset + 1) & psFWCCBCtl->ui32WrapMask;
	}
}

PVRSRV_ERROR WorkEstPrepare(PVRSRV_RGXDEV_INFO        *psDevInfo,
                            WORKEST_HOST_DATA         *psWorkEstHostData,
                            WORKLOAD_MATCHING_DATA    *psWorkloadMatchingData,
                            const RGXFWIF_CCB_CMD_TYPE eDMCmdType,
                            const RGX_WORKLOAD        *psWorkloadCharsIn,
                            IMG_UINT64                ui64DeadlineInus,
                            RGXFWIF_WORKEST_KICK_DATA *psWorkEstKickData)
{
	RGX_WORKLOAD          *psWorkloadCharacteristics;
	IMG_UINT64            *pui64CyclePrediction;
	IMG_UINT64            ui64CurrentTime;
	WORKEST_RETURN_DATA   *psReturnData;
	IMG_UINT32            ui32ReturnDataWO;
#if defined(SUPPORT_SOC_TIMER)
	PVRSRV_DEVICE_CONFIG  *psDevConfig;
	IMG_UINT64            ui64CurrentSoCTime;
#endif
	PVRSRV_ERROR          eError = PVRSRV_ERROR_INVALID_PARAMS;

	if (!_WorkEstEnabled())
	{
		/* No error message to avoid excessive messages */
		return PVRSRV_OK;
	}

	if (eDMCmdType == RGXFWIF_CCB_CMD_TYPE_NULL)
	{
		/* No workload, only fence updates */
		return PVRSRV_OK;
	}

#if !defined(PVRSRV_NEED_PVR_DPF)
	PVR_UNREFERENCED_PARAMETER(eDMCmdType);
#endif

	/* Validate all required objects required for preparing work estimation */
	PVR_LOG_RETURN_IF_FALSE(psDevInfo, "device info not available", eError);
	PVR_LOG_RETURN_IF_FALSE(psWorkEstHostData, "host data not available", eError);
	PVR_LOG_RETURN_IF_FALSE(psWorkloadMatchingData, "Workload Matching Data not available", eError);
	PVR_LOG_RETURN_IF_FALSE(psWorkloadMatchingData->psHashLock, "hash lock not available", eError);
	PVR_LOG_RETURN_IF_FALSE(psWorkloadMatchingData->psHashTable, "hash table not available", eError);

#if defined(SUPPORT_SOC_TIMER)
	psDevConfig = psDevInfo->psDeviceNode->psDevConfig;
	PVR_LOG_RETURN_IF_FALSE(psDevConfig->pfnSoCTimerRead, "SoC timer not available", eError);
	ui64CurrentSoCTime = psDevConfig->pfnSoCTimerRead(psDevConfig->hSysData);
#endif

	eError = OSClockMonotonicus64(&ui64CurrentTime);
	PVR_LOG_RETURN_IF_ERROR(eError, "unable to access System Monotonic clock");

	OSLockAcquire(psDevInfo->hWorkEstLock);

	/* Select the next index for the return data and update it (is this thread safe?) */
	ui32ReturnDataWO = psDevInfo->ui32ReturnDataWO;
	psDevInfo->ui32ReturnDataWO = (ui32ReturnDataWO + 1) & RETURN_DATA_ARRAY_WRAP_MASK;

	/* Index for the return data passed to/from the firmware. */
	psWorkEstKickData->ui64ReturnDataIndex = ui32ReturnDataWO;
	if (ui64DeadlineInus > ui64CurrentTime)
	{
		/* Rounding is done to reduce multiple deadlines with minor spread flooding the fw workload array. */
#if defined(SUPPORT_SOC_TIMER)
		IMG_UINT64 ui64TimeDelta = (ui64DeadlineInus - ui64CurrentTime) * SOC_TIMER_FREQ;
		psWorkEstKickData->ui64Deadline = ROUND_DOWN_TO_NEAREST_1024(ui64CurrentSoCTime + ui64TimeDelta);
#else
		psWorkEstKickData->ui64Deadline = ROUND_DOWN_TO_NEAREST_1024(ui64DeadlineInus);
#endif
	}
	else
	{
		/* If deadline has already passed, assign zero to suggest full frequency */
		psWorkEstKickData->ui64Deadline = 0;
	}

	/* Set up data for the return path to process the workload; the matching data is needed
	   as it holds the hash data, the host data is needed for completion updates */
	psReturnData = &psDevInfo->asReturnData[ui32ReturnDataWO];
	psReturnData->psWorkloadMatchingData = psWorkloadMatchingData;
	psReturnData->psWorkEstHostData = psWorkEstHostData;

	/* The workload characteristic is needed in the return data for the matching
	   of future workloads via the hash. */
	psWorkloadCharacteristics = &psReturnData->sWorkloadCharacteristics;
	memcpy(psWorkloadCharacteristics, psWorkloadCharsIn, sizeof(RGX_WORKLOAD));

	OSLockRelease(psDevInfo->hWorkEstLock);

	/* Acquire the lock to access hash */
	OSLockAcquire(psWorkloadMatchingData->psHashLock);

	/* Check if there is a prediction for this workload */
	pui64CyclePrediction = (IMG_UINT64*) HASH_Retrieve(psWorkloadMatchingData->psHashTable,
													   (uintptr_t)psWorkloadCharacteristics);

	/* Release lock */
	OSLockRelease(psWorkloadMatchingData->psHashLock);

	if (pui64CyclePrediction != NULL)
	{
		/* Cycle prediction is available, store this prediction */
		psWorkEstKickData->ui64CyclesPrediction = *pui64CyclePrediction;

#if defined(PVRSRV_NEED_PVR_DPF)
		switch (eDMCmdType)
		{
			case RGXFWIF_CCB_CMD_TYPE_GEOM:
			case RGXFWIF_CCB_CMD_TYPE_3D:
			PVR_DPF((PVR_DBG_MESSAGE, "%s: RT size = %u, draw count = %u, indices = %u, prediction = " IMG_DEVMEM_SIZE_FMTSPEC,
					 __func__,
					 psWorkloadCharacteristics->sTA3D.ui32RenderTargetSize,
					 psWorkloadCharacteristics->sTA3D.ui32NumberOfDrawCalls,
					 psWorkloadCharacteristics->sTA3D.ui32NumberOfIndices,
					 *pui64CyclePrediction));
				break;
			case RGXFWIF_CCB_CMD_TYPE_CDM:
			PVR_DPF((PVR_DBG_MESSAGE, "%s: Number of workgroups = %u, max workgroup size = %u, prediction = " IMG_DEVMEM_SIZE_FMTSPEC,
					 __func__,
					 psWorkloadCharacteristics->sCompute.ui32NumberOfWorkgroups,
					 psWorkloadCharacteristics->sCompute.ui32NumberOfWorkitems,
					 *pui64CyclePrediction));
				break;
			case RGXFWIF_CCB_CMD_TYPE_TQ_TDM:
			PVR_DPF((PVR_DBG_MESSAGE, "%s: Dest size = %u, Pixel format ID = %u, prediction = " IMG_DEVMEM_SIZE_FMTSPEC,
					 __func__,
					 psWorkloadCharacteristics->sTransfer.ui32Characteristic1,
					 psWorkloadCharacteristics->sTransfer.ui32Characteristic2,
					 *pui64CyclePrediction));
				break;
			default:
				break;
		}
#endif
	}
	else
	{
		/* There is no prediction */
		psWorkEstKickData->ui64CyclesPrediction = 0;
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR WorkEstRetire(PVRSRV_RGXDEV_INFO *psDevInfo,
						   RGXFWIF_WORKEST_FWCCB_CMD *psReturnCmd)
{
	RGX_WORKLOAD           *psWorkloadCharacteristics;
	WORKLOAD_MATCHING_DATA *psWorkloadMatchingData;
	IMG_UINT64             *paui64WorkloadHashData;
	RGX_WORKLOAD           *pasWorkloadHashKeys;
	IMG_UINT32             ui32HashArrayWO;
	IMG_UINT64             *pui64CyclesTaken;
	WORKEST_RETURN_DATA    *psReturnData;
	WORKEST_HOST_DATA      *psWorkEstHostData;

	if (!_WorkEstEnabled())
	{
		/* No error message to avoid excessive messages */
		return PVRSRV_OK;
	}

	PVR_LOG_RETURN_IF_FALSE(psReturnCmd,
	                        "WorkEstRetire: Missing return command",
	                        PVRSRV_ERROR_INVALID_PARAMS);

	if (psReturnCmd->ui64ReturnDataIndex >= RETURN_DATA_ARRAY_SIZE)
	{
		PVR_DPF((PVR_DBG_ERROR, "WorkEstRetire: Handle reference out-of-bounds:"
		        " %" IMG_UINT64_FMTSPEC " >= %" IMG_UINT64_FMTSPEC,
		        psReturnCmd->ui64ReturnDataIndex,
		        (IMG_UINT64) RETURN_DATA_ARRAY_SIZE));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	OSLockAcquire(psDevInfo->hWorkEstLock);

	/* Retrieve/validate the return data from this completed workload */
	psReturnData = &psDevInfo->asReturnData[psReturnCmd->ui64ReturnDataIndex];
	psWorkloadCharacteristics = &psReturnData->sWorkloadCharacteristics;
	psWorkEstHostData = psReturnData->psWorkEstHostData;
	PVR_LOG_GOTO_IF_FALSE(psWorkEstHostData,
	                      "WorkEstRetire: Missing host data",
	                      unlock_workest);

	/* Retrieve/validate completed workload matching data */
	psWorkloadMatchingData = psReturnData->psWorkloadMatchingData;
	PVR_LOG_GOTO_IF_FALSE(psWorkloadMatchingData,
	                      "WorkEstRetire: Missing matching data",
	                      unlock_workest);
	PVR_LOG_GOTO_IF_FALSE(psWorkloadMatchingData->psHashTable,
	                      "WorkEstRetire: Missing hash",
	                      unlock_workest);
	PVR_LOG_GOTO_IF_FALSE(psWorkloadMatchingData->psHashLock,
	                      "WorkEstRetire: Missing hash/lock",
	                      unlock_workest);
	paui64WorkloadHashData = psWorkloadMatchingData->aui64HashData;
	pasWorkloadHashKeys = psWorkloadMatchingData->asHashKeys;
	ui32HashArrayWO = psWorkloadMatchingData->ui32HashArrayWO;

	OSLockRelease(psDevInfo->hWorkEstLock);

	OSLockAcquire(psWorkloadMatchingData->psHashLock);

	/* Update workload prediction by removing old hash entry (if any)
	 * & inserting new hash entry */
	pui64CyclesTaken = (IMG_UINT64*) HASH_Remove(psWorkloadMatchingData->psHashTable,
												 (uintptr_t)psWorkloadCharacteristics);

	if (paui64WorkloadHashData[ui32HashArrayWO] > 0)
	{
		/* Out-of-space so remove the oldest hash data before it becomes
		 * overwritten */
		RGX_WORKLOAD *psWorkloadHashKey = &pasWorkloadHashKeys[ui32HashArrayWO];
		(void) HASH_Remove(psWorkloadMatchingData->psHashTable, (uintptr_t)psWorkloadHashKey);
	}

	if (pui64CyclesTaken == NULL)
	{
		/* There is no existing entry for this workload characteristics,
		 * store it */
		paui64WorkloadHashData[ui32HashArrayWO] = psReturnCmd->ui64CyclesTaken;
		pasWorkloadHashKeys[ui32HashArrayWO] = *psWorkloadCharacteristics;
	}
	else
	{
		/* Found prior entry for workload characteristics, average with
		 * completed; also reset the old value to 0 so it is known to be
		 * invalid */
		paui64WorkloadHashData[ui32HashArrayWO] = (*pui64CyclesTaken + psReturnCmd->ui64CyclesTaken)/2;
		pasWorkloadHashKeys[ui32HashArrayWO] = *psWorkloadCharacteristics;
		*pui64CyclesTaken = 0;
	}

	/* Hash insertion should not fail but if it does best we can do is to exit
	 * gracefully and not update the FW received counter */
	if (IMG_TRUE != HASH_Insert((HASH_TABLE*)psWorkloadMatchingData->psHashTable,
								(uintptr_t)&pasWorkloadHashKeys[ui32HashArrayWO],
								(uintptr_t)&paui64WorkloadHashData[ui32HashArrayWO]))
	{
		PVR_ASSERT(0);
		PVR_LOG(("WorkEstRetire: HASH_Insert failed"));
	}

	psWorkloadMatchingData->ui32HashArrayWO = (ui32HashArrayWO + 1) & WORKLOAD_HASH_WRAP_MASK;

	OSLockRelease(psWorkloadMatchingData->psHashLock);

	/* Update the received counter so that the FW is able to check as to whether
	 * all the workloads connected to a render context are finished.
	 * Note: needs to be done also for *unlock_workest* label below. */
	psWorkEstHostData->ui32WorkEstCCBReceived++;

	return PVRSRV_OK;

unlock_workest:
	OSLockRelease(psDevInfo->hWorkEstLock);
	psWorkEstHostData->ui32WorkEstCCBReceived++;

	return PVRSRV_ERROR_INVALID_PARAMS;
}

static void _WorkEstInit(PVRSRV_RGXDEV_INFO *psDevInfo,
						 WORKLOAD_MATCHING_DATA *psWorkloadMatchingData,
						 HASH_FUNC *pfnWorkEstHashFunc,
						 HASH_KEY_COMP *pfnWorkEstHashCompare)
{
	HASH_TABLE *psWorkloadHashTable;
	PVR_UNREFERENCED_PARAMETER(psDevInfo);

	/* Create a lock to protect the per-DM hash table */
	WorkEstHashLockCreate(&psWorkloadMatchingData->psHashLock);

	/* Create hash table for the per-DM workload matching */
	psWorkloadHashTable = HASH_Create_Extended(WORKLOAD_HASH_SIZE,
											   sizeof(RGX_WORKLOAD *),
											   pfnWorkEstHashFunc,
											   pfnWorkEstHashCompare);
	psWorkloadMatchingData->psHashTable = psWorkloadHashTable;
}

static void _WorkEstDeInit(PVRSRV_RGXDEV_INFO *psDevInfo,
						   WORKLOAD_MATCHING_DATA *psWorkloadMatchingData)
{
	HASH_TABLE        *psWorkloadHashTable;
	RGX_WORKLOAD      *pasWorkloadHashKeys;
	RGX_WORKLOAD      *psWorkloadHashKey;
	IMG_UINT64        *paui64WorkloadCycleData;
	IMG_UINT32        ui32Itr;

	/* Tear down per-DM hash */
	pasWorkloadHashKeys = psWorkloadMatchingData->asHashKeys;
	paui64WorkloadCycleData = psWorkloadMatchingData->aui64HashData;
	psWorkloadHashTable = psWorkloadMatchingData->psHashTable;

	if (psWorkloadHashTable)
	{
		for (ui32Itr = 0; ui32Itr < WORKLOAD_HASH_SIZE; ui32Itr++)
		{
			if (paui64WorkloadCycleData[ui32Itr] > 0)
			{
				psWorkloadHashKey = &pasWorkloadHashKeys[ui32Itr];
				HASH_Remove(psWorkloadHashTable, (uintptr_t)psWorkloadHashKey);
			}
		}

		HASH_Delete(psWorkloadHashTable);
	}

	/* Remove the hash lock */
	WorkEstHashLockDestroy(psWorkloadMatchingData->psHashLock);

	return;
}

void WorkEstInitTA3D(PVRSRV_RGXDEV_INFO *psDevInfo, WORKEST_HOST_DATA *psWorkEstData)
{
	_WorkEstInit(psDevInfo,
		&psWorkEstData->uWorkloadMatchingData.sTA3D.sDataTA,
		(HASH_FUNC *)WorkEstHashFuncTA3D,
		(HASH_KEY_COMP *)WorkEstHashCompareTA3D);
	_WorkEstInit(psDevInfo,
		&psWorkEstData->uWorkloadMatchingData.sTA3D.sData3D,
		(HASH_FUNC *)WorkEstHashFuncTA3D,
		(HASH_KEY_COMP *)WorkEstHashCompareTA3D);
}

void WorkEstDeInitTA3D(PVRSRV_RGXDEV_INFO *psDevInfo, WORKEST_HOST_DATA *psWorkEstData)
{
	_WorkEstDeInit(psDevInfo, &psWorkEstData->uWorkloadMatchingData.sTA3D.sDataTA);
	_WorkEstDeInit(psDevInfo, &psWorkEstData->uWorkloadMatchingData.sTA3D.sData3D);
}

void WorkEstInitCompute(PVRSRV_RGXDEV_INFO *psDevInfo, WORKEST_HOST_DATA *psWorkEstData)
{
	_WorkEstInit(psDevInfo,
		&psWorkEstData->uWorkloadMatchingData.sCompute.sDataCDM,
		(HASH_FUNC *)WorkEstHashFuncCompute,
		(HASH_KEY_COMP *)WorkEstHashCompareCompute);
}

void WorkEstDeInitCompute(PVRSRV_RGXDEV_INFO *psDevInfo, WORKEST_HOST_DATA *psWorkEstData)
{
	_WorkEstDeInit(psDevInfo, &psWorkEstData->uWorkloadMatchingData.sCompute.sDataCDM);
}

void WorkEstInitTDM(PVRSRV_RGXDEV_INFO *psDevInfo, WORKEST_HOST_DATA *psWorkEstData)
{
	_WorkEstInit(psDevInfo,
		&psWorkEstData->uWorkloadMatchingData.sTransfer.sDataTDM,
		(HASH_FUNC *)WorkEstHashFuncTDM,
		(HASH_KEY_COMP *)WorkEstHashCompareTDM);
}

void WorkEstDeInitTDM(PVRSRV_RGXDEV_INFO *psDevInfo, WORKEST_HOST_DATA *psWorkEstData)
{
	_WorkEstDeInit(psDevInfo, &psWorkEstData->uWorkloadMatchingData.sTransfer.sDataTDM);
}
