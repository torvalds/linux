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

#include "rgxworkest.h"
#include "rgxfwutils.h"
#include "rgxdevice.h"
#include "rgxpdvfs.h"
#include "device.h"
#include "pvr_debug.h"

#define ROUND_DOWN_TO_NEAREST_1024(number) (((number) >> 10) << 10)

void WorkEstRCInit(WORKEST_HOST_DATA *psWorkEstData)
{
	/* Create hash tables for workload matching */
	psWorkEstData->sWorkloadMatchingDataTA.psWorkloadDataHash =
		HASH_Create_Extended(WORKLOAD_HASH_SIZE,
							 sizeof(RGX_WORKLOAD_TA3D *),
							 &WorkEstHashFuncTA3D,
							 (HASH_KEY_COMP *)&WorkEstHashCompareTA3D);

	/* Create a lock to protect the hash table */
	WorkEstHashLockCreate(&(psWorkEstData->sWorkloadMatchingDataTA.psWorkEstHashLock));

	psWorkEstData->sWorkloadMatchingData3D.psWorkloadDataHash =
		HASH_Create_Extended(WORKLOAD_HASH_SIZE,
							 sizeof(RGX_WORKLOAD_TA3D *),
							 &WorkEstHashFuncTA3D,
							 (HASH_KEY_COMP *)&WorkEstHashCompareTA3D);

	/* Create a lock to protect the hash tables */
	WorkEstHashLockCreate(&(psWorkEstData->sWorkloadMatchingData3D.psWorkEstHashLock));
}

void WorkEstRCDeInit(WORKEST_HOST_DATA *psWorkEstData,
                     PVRSRV_RGXDEV_INFO *psDevInfo)
{
	HASH_TABLE        *psWorkloadDataHash;
	RGX_WORKLOAD_TA3D *pasWorkloadHashKeys;
	RGX_WORKLOAD_TA3D *psWorkloadHashKey;
	IMG_UINT32        ui32i;
	IMG_UINT64        *paui64WorkloadCycleData;

	pasWorkloadHashKeys = psWorkEstData->sWorkloadMatchingDataTA.asWorkloadHashKeys;
	paui64WorkloadCycleData = psWorkEstData->sWorkloadMatchingDataTA.aui64HashCycleData;
	psWorkloadDataHash = psWorkEstData->sWorkloadMatchingDataTA.psWorkloadDataHash;

	if(psWorkloadDataHash)
	{
		for(ui32i = 0; ui32i < WORKLOAD_HASH_SIZE; ui32i++)
		{
			if(paui64WorkloadCycleData[ui32i] > 0)
			{
				psWorkloadHashKey = &pasWorkloadHashKeys[ui32i];
				HASH_Remove_Extended(psWorkloadDataHash,
									 (uintptr_t*)&psWorkloadHashKey);
			}
		}

		HASH_Delete(psWorkloadDataHash);
	}

	/* Remove the hash lock */
	WorkEstHashLockDestroy(psWorkEstData->sWorkloadMatchingDataTA.psWorkEstHashLock);

	pasWorkloadHashKeys = psWorkEstData->sWorkloadMatchingData3D.asWorkloadHashKeys;
	paui64WorkloadCycleData = psWorkEstData->sWorkloadMatchingData3D.aui64HashCycleData;
	psWorkloadDataHash = psWorkEstData->sWorkloadMatchingData3D.psWorkloadDataHash;

	if(psWorkloadDataHash)
	{
		for(ui32i = 0; ui32i < WORKLOAD_HASH_SIZE; ui32i++)
		{
			if(paui64WorkloadCycleData[ui32i] > 0)
			{
				psWorkloadHashKey = &pasWorkloadHashKeys[ui32i];
				HASH_Remove_Extended(psWorkloadDataHash,
									 (uintptr_t*)&psWorkloadHashKey);
			}
		}

		HASH_Delete(psWorkloadDataHash);
	}

	/* Remove the hash lock */
	WorkEstHashLockDestroy(psWorkEstData->sWorkloadMatchingData3D.psWorkEstHashLock);

	return;
}

IMG_BOOL WorkEstHashCompareTA3D(size_t uKeySize,
								void *pKey1,
								void *pKey2)
{
	RGX_WORKLOAD_TA3D *psWorkload1;
	RGX_WORKLOAD_TA3D *psWorkload2;

	if(pKey1 && pKey2)
	{
		psWorkload1 = *((RGX_WORKLOAD_TA3D **)pKey1);
		psWorkload2 = *((RGX_WORKLOAD_TA3D **)pKey2);

		PVR_ASSERT(psWorkload1);
		PVR_ASSERT(psWorkload2);

		if(psWorkload1->ui32RenderTargetSize == psWorkload2->ui32RenderTargetSize
		   && psWorkload1->ui32NumberOfDrawCalls == psWorkload2->ui32NumberOfDrawCalls
		   && psWorkload1->ui32NumberOfIndices == psWorkload2->ui32NumberOfIndices
		   && psWorkload1->ui32NumberOfMRTs == psWorkload2->ui32NumberOfMRTs)
		{
			/* This is added to allow this memory to be freed */
			*(uintptr_t*)pKey2 = *(uintptr_t*)pKey1;
			return IMG_TRUE;
		}
	}
	return IMG_FALSE;
}

static inline IMG_UINT32 WorkEstDoHash(IMG_UINT32 ui32Input)
{
	IMG_UINT32 ui32HashPart;

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

IMG_UINT32 WorkEstHashFuncTA3D(size_t uKeySize, void *pKey, IMG_UINT32 uHashTabLen)
{
	RGX_WORKLOAD_TA3D *psWorkload = *((RGX_WORKLOAD_TA3D**)pKey);
	IMG_UINT32 ui32HashKey = 0;
	PVR_UNREFERENCED_PARAMETER(uHashTabLen);
	PVR_UNREFERENCED_PARAMETER(uKeySize);

	ui32HashKey += WorkEstDoHash(psWorkload->ui32RenderTargetSize);
	ui32HashKey += WorkEstDoHash(psWorkload->ui32NumberOfDrawCalls);
	ui32HashKey += WorkEstDoHash(psWorkload->ui32NumberOfIndices);
	ui32HashKey += WorkEstDoHash(psWorkload->ui32NumberOfMRTs);

	return ui32HashKey;
}

PVRSRV_ERROR WorkEstPrepare(PVRSRV_RGXDEV_INFO        *psDevInfo,
                            WORKEST_HOST_DATA         *psWorkEstHostData,
                            WORKLOAD_MATCHING_DATA    *psWorkloadMatchingData,
                            IMG_UINT32                ui32RenderTargetSize,
                            IMG_UINT32                ui32NumberOfDrawCalls,
                            IMG_UINT32                ui32NumberOfIndices,
                            IMG_UINT32                ui32NumberOfMRTs,
                            IMG_UINT64                ui64DeadlineInus,
                            RGXFWIF_WORKEST_KICK_DATA *psWorkEstKickData)
{
	PVRSRV_ERROR          eError;
	RGX_WORKLOAD_TA3D     *psWorkloadCharacteristics;
	IMG_UINT64            *pui64CyclePrediction;
	POS_LOCK              psWorkEstHashLock;
	IMG_UINT64            ui64WorkloadDeadlineInus = ui64DeadlineInus;
	IMG_UINT64            ui64CurrentTime;
	HASH_TABLE            *psWorkloadDataHash;
	WORKEST_RETURN_DATA   *psReturnData;

	if(psDevInfo == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"WorkEstPrepare: Device Info not available"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if(psDevInfo->bWorkEstEnabled != IMG_TRUE)
	{
		/* No error message to avoid excessive messages */
		return PVRSRV_OK;
	}

	if(psWorkEstHostData == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "WorkEstPrepare: Host data not available"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if(psWorkloadMatchingData == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "WorkEstPrepare: Workload Matching Data not available"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psWorkloadDataHash = psWorkloadMatchingData->psWorkloadDataHash;
	if(psWorkloadDataHash == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"WorkEstPrepare: Hash Table not available"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psWorkEstHashLock = psWorkloadMatchingData->psWorkEstHashLock;
	if(psWorkEstHashLock == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "WorkEstPrepare: Hash lock not available"
		        ));
		eError = PVRSRV_ERROR_UNABLE_TO_RETRIEVE_HASH_VALUE;
		return eError;
	}

	eError = OSClockMonotonicus64(&ui64CurrentTime);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "WorkEstPrepare: Unable to access System Monotonic clock"));
		PVR_ASSERT(eError == PVRSRV_OK);
		return eError;
	}

#if defined(SUPPORT_PDVFS)
	psDevInfo->psDeviceNode->psDevConfig->sDVFS.sPDVFSData.bWorkInFrame = IMG_TRUE;
#endif

	/* Set up data for the return path to process the workload */

	/* Any host side data needed for the return path is stored in an array and
	 * only the array's index is passed to and from the firmware. This is a
	 * similar abstraction to using handles but is optimised for this case.
	 */
	psReturnData =
		&psDevInfo->asReturnData[psDevInfo->ui32ReturnDataWO];

	/* The index for the specific data is passed to the FW */
	psWorkEstKickData->ui64ReturnDataIndex = psDevInfo->ui32ReturnDataWO;

	psDevInfo->ui32ReturnDataWO =
		(psDevInfo->ui32ReturnDataWO + 1) & RETURN_DATA_ARRAY_WRAP_MASK;

	/* The workload characteristics are needed in the return data for the
	 * matching of future workloads via the hash.
	 */
	psWorkloadCharacteristics = &psReturnData->sWorkloadCharacteristics;
	psWorkloadCharacteristics->ui32RenderTargetSize = ui32RenderTargetSize;
	psWorkloadCharacteristics->ui32NumberOfDrawCalls = ui32NumberOfDrawCalls;
	psWorkloadCharacteristics->ui32NumberOfIndices = ui32NumberOfIndices;
	psWorkloadCharacteristics->ui32NumberOfMRTs = ui32NumberOfMRTs;

	/* The matching data is needed as it holds the hash data. */
	psReturnData->psWorkloadMatchingData = psWorkloadMatchingData;

	/* The host data for the completion updates */
	psReturnData->psWorkEstHostData = psWorkEstHostData;
	if(ui64WorkloadDeadlineInus > ui64CurrentTime)
	{
		/* This is rounded to reduce multiple deadlines with a minor spread
		 * flooding the fw workload array.
		 */
		psWorkEstKickData->ui64DeadlineInus =
			ROUND_DOWN_TO_NEAREST_1024(ui64WorkloadDeadlineInus);
	}
	else
	{
		/* If the deadline has already passed assign as zero to suggest full
		 * frequency
		 */
		psWorkEstKickData->ui64DeadlineInus = 0;
	}

	/* Acquire the lock to access hash */
	OSLockAcquire(psWorkEstHashLock);

	/* Check if there is a prediction for this workload */
	pui64CyclePrediction =
		(IMG_UINT64*) HASH_Retrieve(psWorkloadDataHash,
		                            (uintptr_t)psWorkloadCharacteristics);

	/* Release lock */
	OSLockRelease(psWorkEstHashLock);

	if(pui64CyclePrediction != NULL)
	{
		/* Cycle prediction is available, store this prediction */
		psWorkEstKickData->ui64CyclesPrediction = *pui64CyclePrediction;
	}
	else
	{
		/* There is no prediction */
		psWorkEstKickData->ui64CyclesPrediction = 0;
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR WorkEstWorkloadFinished(PVRSRV_RGXDEV_INFO        *psDevInfo,
                                     RGXFWIF_WORKEST_FWCCB_CMD *psReturnCmd)
{
	RGX_WORKLOAD_TA3D           *psWorkloadCharacteristics;
	RGX_WORKLOAD_TA3D           *pasWorkloadHashKeys;
	IMG_UINT64                  *paui64HashCycleData;
	IMG_UINT32                  *pui32HashArrayWO;
	RGX_WORKLOAD_TA3D           *psWorkloadHashKey;
	IMG_UINT64                  *pui64CyclesTaken;
	HASH_TABLE                  *psWorkloadHash;
	WORKLOAD_MATCHING_DATA      *psWorkloadMatchingData;
	POS_LOCK                    psWorkEstHashLock;
	IMG_BOOL                    bHashSucess;
	WORKEST_RETURN_DATA         *psReturnData;
	WORKEST_HOST_DATA           *psWorkEstHostData;
	PVRSRV_ERROR                eError = PVRSRV_OK;

	if(psDevInfo->bWorkEstEnabled != IMG_TRUE)
	{
		/* No error message to avoid excessive messages */
		return PVRSRV_OK;
	}

	if(psReturnCmd == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "WorkEstFinished: Missing Return Command"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if(psReturnCmd->ui64ReturnDataIndex >= RETURN_DATA_ARRAY_SIZE)
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "WorkEstFinished: Handle Reference Out of Bounds"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Retrieve the return data for this workload */
	psReturnData = &psDevInfo->asReturnData[psReturnCmd->ui64ReturnDataIndex];

	psWorkEstHostData = psReturnData->psWorkEstHostData;

	if(psWorkEstHostData == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "WorkEstFinished: Missing host data"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		return eError;
	}

	psWorkloadCharacteristics = &psReturnData->sWorkloadCharacteristics;

	if(psWorkloadCharacteristics == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "WorkEstFinished: Missing workload characteristics"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto hasherror;
	}

	psWorkloadMatchingData = psReturnData->psWorkloadMatchingData;

	psWorkloadHash = psWorkloadMatchingData->psWorkloadDataHash;
	if(psWorkloadHash == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "WorkEstFinished: Missing hash"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto hasherror;
	}

	psWorkEstHashLock = psWorkloadMatchingData->psWorkEstHashLock;
	if(psWorkEstHashLock == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "WorkEstFinished: Missing hash lock"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto hasherror;
	}

	OSLockAcquire(psWorkEstHashLock);

	pui64CyclesTaken =
		(IMG_UINT64*) HASH_Remove_Extended(psWorkloadHash,
		                                   (uintptr_t*)&psWorkloadCharacteristics);

	pui32HashArrayWO = &(psWorkloadMatchingData->ui32HashArrayWO);
	paui64HashCycleData = psWorkloadMatchingData->aui64HashCycleData;
	pasWorkloadHashKeys = psWorkloadMatchingData->asWorkloadHashKeys;

	/* Remove the oldest Hash data before it becomes overwritten */
	if(paui64HashCycleData[*pui32HashArrayWO] > 0)
	{
		psWorkloadHashKey = &pasWorkloadHashKeys[*pui32HashArrayWO];
		HASH_Remove_Extended(psWorkloadHash,
		                     (uintptr_t*)&psWorkloadHashKey);
	}

	if(pui64CyclesTaken == NULL)
	{
		/* There is no existing entry for these characteristics. */
		pasWorkloadHashKeys[*pui32HashArrayWO] = *psWorkloadCharacteristics;

		paui64HashCycleData[*pui32HashArrayWO] = psReturnCmd->ui64CyclesTaken;
	}
	else
	{
		*pui64CyclesTaken =
			(*pui64CyclesTaken + psReturnCmd->ui64CyclesTaken)/2;

		pasWorkloadHashKeys[*pui32HashArrayWO] = *psWorkloadCharacteristics;

		paui64HashCycleData[*pui32HashArrayWO] = *pui64CyclesTaken;

		/* Set the old value to 0 so it is known to be invalid */
		*pui64CyclesTaken = 0;
	}


	bHashSucess = HASH_Insert((HASH_TABLE*)(psWorkloadHash),
			(uintptr_t)&pasWorkloadHashKeys[*pui32HashArrayWO],
			(uintptr_t)&paui64HashCycleData[*pui32HashArrayWO]);
	PVR_ASSERT(bHashSucess);

	if(*pui32HashArrayWO == WORKLOAD_HASH_SIZE-1)
	{
		*pui32HashArrayWO = 0;
	}
	else
	{
		(*pui32HashArrayWO)++;
	}

	OSLockRelease(psWorkEstHashLock);

hasherror:

	/* Update the received counter so that the FW is able to check as to whether
	 * all the workloads connected to a render context are finished.
	 */
	psWorkEstHostData->ui32WorkEstCCBReceived++;
	return eError;
}

void WorkEstHashLockCreate(POS_LOCK *psWorkEstHashLock)
{
	if(*psWorkEstHashLock == NULL)
	{
		OSLockCreate(psWorkEstHashLock, LOCK_TYPE_DISPATCH);
	}
	return;
}

void WorkEstHashLockDestroy(POS_LOCK sWorkEstHashLock)
{
	if(sWorkEstHashLock != NULL)
	{
		OSLockDestroy(sWorkEstHashLock);
		sWorkEstHashLock = NULL;
	}
	return;
}

void WorkEstCheckFirmwareCCB(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	RGXFWIF_WORKEST_FWCCB_CMD *psFwCCBCmd;

	RGXFWIF_CCB_CTL *psFWCCBCtl = psDevInfo->psWorkEstFirmwareCCBCtl;
	IMG_UINT8 *psFWCCB = psDevInfo->psWorkEstFirmwareCCB;

	while (psFWCCBCtl->ui32ReadOffset != psFWCCBCtl->ui32WriteOffset)
	{
		/* Point to the next command */
		psFwCCBCmd = ((RGXFWIF_WORKEST_FWCCB_CMD *)psFWCCB) + psFWCCBCtl->ui32ReadOffset;

		WorkEstWorkloadFinished(psDevInfo, psFwCCBCmd);

		/* Update read offset */
		psFWCCBCtl->ui32ReadOffset = (psFWCCBCtl->ui32ReadOffset + 1) & psFWCCBCtl->ui32WrapMask;
	}
}
