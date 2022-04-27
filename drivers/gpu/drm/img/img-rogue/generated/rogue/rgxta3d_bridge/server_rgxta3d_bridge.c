/*******************************************************************************
@File
@Title          Server bridge for rgxta3d
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for rgxta3d
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

#include "rgxta3d.h"

#include "common_rgxta3d_bridge.h"

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

static PVRSRV_ERROR _RGXCreateHWRTDataSetpsKmHwRTDataSet0IntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = RGXDestroyHWRTDataSet((RGX_KM_HW_RT_DATASET *) pvData);
	return eError;
}

static PVRSRV_ERROR _RGXCreateHWRTDataSetpsKmHwRTDataSet1IntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = RGXDestroyHWRTDataSet((RGX_KM_HW_RT_DATASET *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgeRGXCreateHWRTDataSet(IMG_UINT32 ui32DispatchTableEntry,
				 IMG_UINT8 * psRGXCreateHWRTDataSetIN_UI8,
				 IMG_UINT8 * psRGXCreateHWRTDataSetOUT_UI8,
				 CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXCREATEHWRTDATASET *psRGXCreateHWRTDataSetIN =
	    (PVRSRV_BRIDGE_IN_RGXCREATEHWRTDATASET *) IMG_OFFSET_ADDR(psRGXCreateHWRTDataSetIN_UI8,
								      0);
	PVRSRV_BRIDGE_OUT_RGXCREATEHWRTDATASET *psRGXCreateHWRTDataSetOUT =
	    (PVRSRV_BRIDGE_OUT_RGXCREATEHWRTDATASET *)
	    IMG_OFFSET_ADDR(psRGXCreateHWRTDataSetOUT_UI8, 0);

	RGX_FREELIST **psapsFreeListsInt = NULL;
	IMG_HANDLE *hapsFreeListsInt2 = NULL;
	RGX_KM_HW_RT_DATASET *psKmHwRTDataSet0Int = NULL;
	RGX_KM_HW_RT_DATASET *psKmHwRTDataSet1Int = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize =
	    (RGXFW_MAX_FREELISTS * sizeof(RGX_FREELIST *)) +
	    (RGXFW_MAX_FREELISTS * sizeof(IMG_HANDLE)) + 0;

	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psRGXCreateHWRTDataSetIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psRGXCreateHWRTDataSetIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocZMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psRGXCreateHWRTDataSetOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXCreateHWRTDataSet_exit;
			}
		}
	}

	{
		psapsFreeListsInt =
		    (RGX_FREELIST **) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += RGXFW_MAX_FREELISTS * sizeof(RGX_FREELIST *);
		hapsFreeListsInt2 =
		    (IMG_HANDLE *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += RGXFW_MAX_FREELISTS * sizeof(IMG_HANDLE);
	}

	/* Copy the data over */
	if (RGXFW_MAX_FREELISTS * sizeof(IMG_HANDLE) > 0)
	{
		if (OSCopyFromUser
		    (NULL, hapsFreeListsInt2,
		     (const void __user *)psRGXCreateHWRTDataSetIN->phapsFreeLists,
		     RGXFW_MAX_FREELISTS * sizeof(IMG_HANDLE)) != PVRSRV_OK)
		{
			psRGXCreateHWRTDataSetOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXCreateHWRTDataSet_exit;
		}
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	{
		IMG_UINT32 i;

		for (i = 0; i < RGXFW_MAX_FREELISTS; i++)
		{
			/* Look up the address from the handle */
			psRGXCreateHWRTDataSetOUT->eError =
			    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
						       (void **)&psapsFreeListsInt[i],
						       hapsFreeListsInt2[i],
						       PVRSRV_HANDLE_TYPE_RGX_FREELIST, IMG_TRUE);
			if (unlikely(psRGXCreateHWRTDataSetOUT->eError != PVRSRV_OK))
			{
				UnlockHandle(psConnection->psHandleBase);
				goto RGXCreateHWRTDataSet_exit;
			}
		}
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXCreateHWRTDataSetOUT->eError =
	    RGXCreateHWRTDataSet(psConnection, OSGetDevNode(psConnection),
				 psRGXCreateHWRTDataSetIN->ssVHeapTableDevVAddr,
				 psRGXCreateHWRTDataSetIN->sPMMlistDevVAddr0,
				 psRGXCreateHWRTDataSetIN->sPMMlistDevVAddr1,
				 psapsFreeListsInt,
				 psRGXCreateHWRTDataSetIN->ui32PPPScreen,
				 psRGXCreateHWRTDataSetIN->ui64MultiSampleCtl,
				 psRGXCreateHWRTDataSetIN->ui64FlippedMultiSampleCtl,
				 psRGXCreateHWRTDataSetIN->ui32TPCStride,
				 psRGXCreateHWRTDataSetIN->sTailPtrsDevVAddr,
				 psRGXCreateHWRTDataSetIN->ui32TPCSize,
				 psRGXCreateHWRTDataSetIN->ui32TEScreen,
				 psRGXCreateHWRTDataSetIN->ui32TEAA,
				 psRGXCreateHWRTDataSetIN->ui32TEMTILE1,
				 psRGXCreateHWRTDataSetIN->ui32TEMTILE2,
				 psRGXCreateHWRTDataSetIN->ui32MTileStride,
				 psRGXCreateHWRTDataSetIN->ui32ui32ISPMergeLowerX,
				 psRGXCreateHWRTDataSetIN->ui32ui32ISPMergeLowerY,
				 psRGXCreateHWRTDataSetIN->ui32ui32ISPMergeUpperX,
				 psRGXCreateHWRTDataSetIN->ui32ui32ISPMergeUpperY,
				 psRGXCreateHWRTDataSetIN->ui32ui32ISPMergeScaleX,
				 psRGXCreateHWRTDataSetIN->ui32ui32ISPMergeScaleY,
				 psRGXCreateHWRTDataSetIN->ssMacrotileArrayDevVAddr0,
				 psRGXCreateHWRTDataSetIN->ssMacrotileArrayDevVAddr1,
				 psRGXCreateHWRTDataSetIN->ssRgnHeaderDevVAddr0,
				 psRGXCreateHWRTDataSetIN->ssRgnHeaderDevVAddr1,
				 psRGXCreateHWRTDataSetIN->ssRTCDevVAddr,
				 psRGXCreateHWRTDataSetIN->ui64uiRgnHeaderSize,
				 psRGXCreateHWRTDataSetIN->ui32ui32ISPMtileSize,
				 psRGXCreateHWRTDataSetIN->ui16MaxRTs,
				 &psKmHwRTDataSet0Int, &psKmHwRTDataSet1Int);
	/* Exit early if bridged call fails */
	if (unlikely(psRGXCreateHWRTDataSetOUT->eError != PVRSRV_OK))
	{
		goto RGXCreateHWRTDataSet_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psRGXCreateHWRTDataSetOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
								      &psRGXCreateHWRTDataSetOUT->
								      hKmHwRTDataSet0,
								      (void *)psKmHwRTDataSet0Int,
								      PVRSRV_HANDLE_TYPE_RGX_KM_HW_RT_DATASET,
								      PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								      (PFN_HANDLE_RELEASE) &
								      _RGXCreateHWRTDataSetpsKmHwRTDataSet0IntRelease);
	if (unlikely(psRGXCreateHWRTDataSetOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXCreateHWRTDataSet_exit;
	}

	psRGXCreateHWRTDataSetOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
								      &psRGXCreateHWRTDataSetOUT->
								      hKmHwRTDataSet1,
								      (void *)psKmHwRTDataSet1Int,
								      PVRSRV_HANDLE_TYPE_RGX_KM_HW_RT_DATASET,
								      PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								      (PFN_HANDLE_RELEASE) &
								      _RGXCreateHWRTDataSetpsKmHwRTDataSet1IntRelease);
	if (unlikely(psRGXCreateHWRTDataSetOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXCreateHWRTDataSet_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

RGXCreateHWRTDataSet_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	if (hapsFreeListsInt2)
	{
		IMG_UINT32 i;

		for (i = 0; i < RGXFW_MAX_FREELISTS; i++)
		{

			/* Unreference the previously looked up handle */
			if (hapsFreeListsInt2[i])
			{
				PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
							    hapsFreeListsInt2[i],
							    PVRSRV_HANDLE_TYPE_RGX_FREELIST);
			}
		}
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	if (psRGXCreateHWRTDataSetOUT->eError != PVRSRV_OK)
	{
		if (psKmHwRTDataSet0Int)
		{
			RGXDestroyHWRTDataSet(psKmHwRTDataSet0Int);
		}
		if (psKmHwRTDataSet1Int)
		{
			RGXDestroyHWRTDataSet(psKmHwRTDataSet1Int);
		}
	}

	/* Allocated space should be equal to the last updated offset */
	PVR_ASSERT(ui32BufferSize == ui32NextOffset);

#if defined(INTEGRITY_OS)
	if (pArrayArgsBuffer)
#else
	if (!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXDestroyHWRTDataSet(IMG_UINT32 ui32DispatchTableEntry,
				  IMG_UINT8 * psRGXDestroyHWRTDataSetIN_UI8,
				  IMG_UINT8 * psRGXDestroyHWRTDataSetOUT_UI8,
				  CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXDESTROYHWRTDATASET *psRGXDestroyHWRTDataSetIN =
	    (PVRSRV_BRIDGE_IN_RGXDESTROYHWRTDATASET *)
	    IMG_OFFSET_ADDR(psRGXDestroyHWRTDataSetIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXDESTROYHWRTDATASET *psRGXDestroyHWRTDataSetOUT =
	    (PVRSRV_BRIDGE_OUT_RGXDESTROYHWRTDATASET *)
	    IMG_OFFSET_ADDR(psRGXDestroyHWRTDataSetOUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psRGXDestroyHWRTDataSetOUT->eError =
	    PVRSRVReleaseHandleStagedUnlock(psConnection->psHandleBase,
					    (IMG_HANDLE) psRGXDestroyHWRTDataSetIN->hKmHwRTDataSet,
					    PVRSRV_HANDLE_TYPE_RGX_KM_HW_RT_DATASET);
	if (unlikely((psRGXDestroyHWRTDataSetOUT->eError != PVRSRV_OK) &&
		     (psRGXDestroyHWRTDataSetOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s",
			 __func__, PVRSRVGetErrorString(psRGXDestroyHWRTDataSetOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto RGXDestroyHWRTDataSet_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

RGXDestroyHWRTDataSet_exit:

	return 0;
}

static PVRSRV_ERROR _RGXCreateZSBufferpssZSBufferKMIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = RGXDestroyZSBufferKM((RGX_ZSBUFFER_DATA *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgeRGXCreateZSBuffer(IMG_UINT32 ui32DispatchTableEntry,
			      IMG_UINT8 * psRGXCreateZSBufferIN_UI8,
			      IMG_UINT8 * psRGXCreateZSBufferOUT_UI8,
			      CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXCREATEZSBUFFER *psRGXCreateZSBufferIN =
	    (PVRSRV_BRIDGE_IN_RGXCREATEZSBUFFER *) IMG_OFFSET_ADDR(psRGXCreateZSBufferIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXCREATEZSBUFFER *psRGXCreateZSBufferOUT =
	    (PVRSRV_BRIDGE_OUT_RGXCREATEZSBUFFER *) IMG_OFFSET_ADDR(psRGXCreateZSBufferOUT_UI8, 0);

	IMG_HANDLE hReservation = psRGXCreateZSBufferIN->hReservation;
	DEVMEMINT_RESERVATION *psReservationInt = NULL;
	IMG_HANDLE hPMR = psRGXCreateZSBufferIN->hPMR;
	PMR *psPMRInt = NULL;
	RGX_ZSBUFFER_DATA *pssZSBufferKMInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXCreateZSBufferOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psReservationInt,
				       hReservation,
				       PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION, IMG_TRUE);
	if (unlikely(psRGXCreateZSBufferOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXCreateZSBuffer_exit;
	}

	/* Look up the address from the handle */
	psRGXCreateZSBufferOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psPMRInt,
				       hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
	if (unlikely(psRGXCreateZSBufferOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXCreateZSBuffer_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXCreateZSBufferOUT->eError =
	    RGXCreateZSBufferKM(psConnection, OSGetDevNode(psConnection),
				psReservationInt,
				psPMRInt, psRGXCreateZSBufferIN->uiMapFlags, &pssZSBufferKMInt);
	/* Exit early if bridged call fails */
	if (unlikely(psRGXCreateZSBufferOUT->eError != PVRSRV_OK))
	{
		goto RGXCreateZSBuffer_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psRGXCreateZSBufferOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
								   &psRGXCreateZSBufferOUT->
								   hsZSBufferKM,
								   (void *)pssZSBufferKMInt,
								   PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER,
								   PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								   (PFN_HANDLE_RELEASE) &
								   _RGXCreateZSBufferpssZSBufferKMIntRelease);
	if (unlikely(psRGXCreateZSBufferOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXCreateZSBuffer_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

RGXCreateZSBuffer_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psReservationInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hReservation, PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION);
	}

	/* Unreference the previously looked up handle */
	if (psPMRInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	if (psRGXCreateZSBufferOUT->eError != PVRSRV_OK)
	{
		if (pssZSBufferKMInt)
		{
			RGXDestroyZSBufferKM(pssZSBufferKMInt);
		}
	}

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXDestroyZSBuffer(IMG_UINT32 ui32DispatchTableEntry,
			       IMG_UINT8 * psRGXDestroyZSBufferIN_UI8,
			       IMG_UINT8 * psRGXDestroyZSBufferOUT_UI8,
			       CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXDESTROYZSBUFFER *psRGXDestroyZSBufferIN =
	    (PVRSRV_BRIDGE_IN_RGXDESTROYZSBUFFER *) IMG_OFFSET_ADDR(psRGXDestroyZSBufferIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXDESTROYZSBUFFER *psRGXDestroyZSBufferOUT =
	    (PVRSRV_BRIDGE_OUT_RGXDESTROYZSBUFFER *) IMG_OFFSET_ADDR(psRGXDestroyZSBufferOUT_UI8,
								     0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psRGXDestroyZSBufferOUT->eError =
	    PVRSRVReleaseHandleStagedUnlock(psConnection->psHandleBase,
					    (IMG_HANDLE) psRGXDestroyZSBufferIN->hsZSBufferMemDesc,
					    PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER);
	if (unlikely((psRGXDestroyZSBufferOUT->eError != PVRSRV_OK) &&
		     (psRGXDestroyZSBufferOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s",
			 __func__, PVRSRVGetErrorString(psRGXDestroyZSBufferOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto RGXDestroyZSBuffer_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

RGXDestroyZSBuffer_exit:

	return 0;
}

static PVRSRV_ERROR _RGXPopulateZSBufferpssPopulationIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = RGXUnpopulateZSBufferKM((RGX_POPULATION *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgeRGXPopulateZSBuffer(IMG_UINT32 ui32DispatchTableEntry,
				IMG_UINT8 * psRGXPopulateZSBufferIN_UI8,
				IMG_UINT8 * psRGXPopulateZSBufferOUT_UI8,
				CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXPOPULATEZSBUFFER *psRGXPopulateZSBufferIN =
	    (PVRSRV_BRIDGE_IN_RGXPOPULATEZSBUFFER *) IMG_OFFSET_ADDR(psRGXPopulateZSBufferIN_UI8,
								     0);
	PVRSRV_BRIDGE_OUT_RGXPOPULATEZSBUFFER *psRGXPopulateZSBufferOUT =
	    (PVRSRV_BRIDGE_OUT_RGXPOPULATEZSBUFFER *) IMG_OFFSET_ADDR(psRGXPopulateZSBufferOUT_UI8,
								      0);

	IMG_HANDLE hsZSBufferKM = psRGXPopulateZSBufferIN->hsZSBufferKM;
	RGX_ZSBUFFER_DATA *pssZSBufferKMInt = NULL;
	RGX_POPULATION *pssPopulationInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXPopulateZSBufferOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&pssZSBufferKMInt,
				       hsZSBufferKM,
				       PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER, IMG_TRUE);
	if (unlikely(psRGXPopulateZSBufferOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXPopulateZSBuffer_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXPopulateZSBufferOUT->eError =
	    RGXPopulateZSBufferKM(pssZSBufferKMInt, &pssPopulationInt);
	/* Exit early if bridged call fails */
	if (unlikely(psRGXPopulateZSBufferOUT->eError != PVRSRV_OK))
	{
		goto RGXPopulateZSBuffer_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psRGXPopulateZSBufferOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
								     &psRGXPopulateZSBufferOUT->
								     hsPopulation,
								     (void *)pssPopulationInt,
								     PVRSRV_HANDLE_TYPE_RGX_POPULATION,
								     PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								     (PFN_HANDLE_RELEASE) &
								     _RGXPopulateZSBufferpssPopulationIntRelease);
	if (unlikely(psRGXPopulateZSBufferOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXPopulateZSBuffer_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

RGXPopulateZSBuffer_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (pssZSBufferKMInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hsZSBufferKM, PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	if (psRGXPopulateZSBufferOUT->eError != PVRSRV_OK)
	{
		if (pssPopulationInt)
		{
			RGXUnpopulateZSBufferKM(pssPopulationInt);
		}
	}

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXUnpopulateZSBuffer(IMG_UINT32 ui32DispatchTableEntry,
				  IMG_UINT8 * psRGXUnpopulateZSBufferIN_UI8,
				  IMG_UINT8 * psRGXUnpopulateZSBufferOUT_UI8,
				  CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXUNPOPULATEZSBUFFER *psRGXUnpopulateZSBufferIN =
	    (PVRSRV_BRIDGE_IN_RGXUNPOPULATEZSBUFFER *)
	    IMG_OFFSET_ADDR(psRGXUnpopulateZSBufferIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXUNPOPULATEZSBUFFER *psRGXUnpopulateZSBufferOUT =
	    (PVRSRV_BRIDGE_OUT_RGXUNPOPULATEZSBUFFER *)
	    IMG_OFFSET_ADDR(psRGXUnpopulateZSBufferOUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psRGXUnpopulateZSBufferOUT->eError =
	    PVRSRVReleaseHandleStagedUnlock(psConnection->psHandleBase,
					    (IMG_HANDLE) psRGXUnpopulateZSBufferIN->hsPopulation,
					    PVRSRV_HANDLE_TYPE_RGX_POPULATION);
	if (unlikely((psRGXUnpopulateZSBufferOUT->eError != PVRSRV_OK) &&
		     (psRGXUnpopulateZSBufferOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s",
			 __func__, PVRSRVGetErrorString(psRGXUnpopulateZSBufferOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto RGXUnpopulateZSBuffer_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

RGXUnpopulateZSBuffer_exit:

	return 0;
}

static PVRSRV_ERROR _RGXCreateFreeListpsCleanupCookieIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = RGXDestroyFreeList((RGX_FREELIST *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgeRGXCreateFreeList(IMG_UINT32 ui32DispatchTableEntry,
			      IMG_UINT8 * psRGXCreateFreeListIN_UI8,
			      IMG_UINT8 * psRGXCreateFreeListOUT_UI8,
			      CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXCREATEFREELIST *psRGXCreateFreeListIN =
	    (PVRSRV_BRIDGE_IN_RGXCREATEFREELIST *) IMG_OFFSET_ADDR(psRGXCreateFreeListIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXCREATEFREELIST *psRGXCreateFreeListOUT =
	    (PVRSRV_BRIDGE_OUT_RGXCREATEFREELIST *) IMG_OFFSET_ADDR(psRGXCreateFreeListOUT_UI8, 0);

	IMG_HANDLE hMemCtxPrivData = psRGXCreateFreeListIN->hMemCtxPrivData;
	IMG_HANDLE hMemCtxPrivDataInt = NULL;
	IMG_HANDLE hsGlobalFreeList = psRGXCreateFreeListIN->hsGlobalFreeList;
	RGX_FREELIST *pssGlobalFreeListInt = NULL;
	IMG_HANDLE hsFreeListPMR = psRGXCreateFreeListIN->hsFreeListPMR;
	PMR *pssFreeListPMRInt = NULL;
	RGX_FREELIST *psCleanupCookieInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXCreateFreeListOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&hMemCtxPrivDataInt,
				       hMemCtxPrivData, PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA, IMG_TRUE);
	if (unlikely(psRGXCreateFreeListOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXCreateFreeList_exit;
	}

	if (psRGXCreateFreeListIN->hsGlobalFreeList)
	{
		/* Look up the address from the handle */
		psRGXCreateFreeListOUT->eError =
		    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
					       (void **)&pssGlobalFreeListInt,
					       hsGlobalFreeList,
					       PVRSRV_HANDLE_TYPE_RGX_FREELIST, IMG_TRUE);
		if (unlikely(psRGXCreateFreeListOUT->eError != PVRSRV_OK))
		{
			UnlockHandle(psConnection->psHandleBase);
			goto RGXCreateFreeList_exit;
		}
	}

	/* Look up the address from the handle */
	psRGXCreateFreeListOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&pssFreeListPMRInt,
				       hsFreeListPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
	if (unlikely(psRGXCreateFreeListOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXCreateFreeList_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXCreateFreeListOUT->eError =
	    RGXCreateFreeList(psConnection, OSGetDevNode(psConnection),
			      hMemCtxPrivDataInt,
			      psRGXCreateFreeListIN->ui32MaxFLPages,
			      psRGXCreateFreeListIN->ui32InitFLPages,
			      psRGXCreateFreeListIN->ui32GrowFLPages,
			      psRGXCreateFreeListIN->ui32GrowParamThreshold,
			      pssGlobalFreeListInt,
			      psRGXCreateFreeListIN->bbFreeListCheck,
			      psRGXCreateFreeListIN->spsFreeListDevVAddr,
			      pssFreeListPMRInt,
			      psRGXCreateFreeListIN->uiPMROffset, &psCleanupCookieInt);
	/* Exit early if bridged call fails */
	if (unlikely(psRGXCreateFreeListOUT->eError != PVRSRV_OK))
	{
		goto RGXCreateFreeList_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psRGXCreateFreeListOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
								   &psRGXCreateFreeListOUT->
								   hCleanupCookie,
								   (void *)psCleanupCookieInt,
								   PVRSRV_HANDLE_TYPE_RGX_FREELIST,
								   PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								   (PFN_HANDLE_RELEASE) &
								   _RGXCreateFreeListpsCleanupCookieIntRelease);
	if (unlikely(psRGXCreateFreeListOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXCreateFreeList_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

RGXCreateFreeList_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (hMemCtxPrivDataInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hMemCtxPrivData, PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA);
	}

	if (psRGXCreateFreeListIN->hsGlobalFreeList)
	{

		/* Unreference the previously looked up handle */
		if (pssGlobalFreeListInt)
		{
			PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
						    hsGlobalFreeList,
						    PVRSRV_HANDLE_TYPE_RGX_FREELIST);
		}
	}

	/* Unreference the previously looked up handle */
	if (pssFreeListPMRInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hsFreeListPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	if (psRGXCreateFreeListOUT->eError != PVRSRV_OK)
	{
		if (psCleanupCookieInt)
		{
			RGXDestroyFreeList(psCleanupCookieInt);
		}
	}

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXDestroyFreeList(IMG_UINT32 ui32DispatchTableEntry,
			       IMG_UINT8 * psRGXDestroyFreeListIN_UI8,
			       IMG_UINT8 * psRGXDestroyFreeListOUT_UI8,
			       CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXDESTROYFREELIST *psRGXDestroyFreeListIN =
	    (PVRSRV_BRIDGE_IN_RGXDESTROYFREELIST *) IMG_OFFSET_ADDR(psRGXDestroyFreeListIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXDESTROYFREELIST *psRGXDestroyFreeListOUT =
	    (PVRSRV_BRIDGE_OUT_RGXDESTROYFREELIST *) IMG_OFFSET_ADDR(psRGXDestroyFreeListOUT_UI8,
								     0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psRGXDestroyFreeListOUT->eError =
	    PVRSRVReleaseHandleStagedUnlock(psConnection->psHandleBase,
					    (IMG_HANDLE) psRGXDestroyFreeListIN->hCleanupCookie,
					    PVRSRV_HANDLE_TYPE_RGX_FREELIST);
	if (unlikely((psRGXDestroyFreeListOUT->eError != PVRSRV_OK) &&
		     (psRGXDestroyFreeListOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s",
			 __func__, PVRSRVGetErrorString(psRGXDestroyFreeListOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto RGXDestroyFreeList_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

RGXDestroyFreeList_exit:

	return 0;
}

static PVRSRV_ERROR _RGXCreateRenderContextpsRenderContextIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = PVRSRVRGXDestroyRenderContextKM((RGX_SERVER_RENDER_CONTEXT *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgeRGXCreateRenderContext(IMG_UINT32 ui32DispatchTableEntry,
				   IMG_UINT8 * psRGXCreateRenderContextIN_UI8,
				   IMG_UINT8 * psRGXCreateRenderContextOUT_UI8,
				   CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXCREATERENDERCONTEXT *psRGXCreateRenderContextIN =
	    (PVRSRV_BRIDGE_IN_RGXCREATERENDERCONTEXT *)
	    IMG_OFFSET_ADDR(psRGXCreateRenderContextIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXCREATERENDERCONTEXT *psRGXCreateRenderContextOUT =
	    (PVRSRV_BRIDGE_OUT_RGXCREATERENDERCONTEXT *)
	    IMG_OFFSET_ADDR(psRGXCreateRenderContextOUT_UI8, 0);

	IMG_BYTE *ui8FrameworkCmdInt = NULL;
	IMG_HANDLE hPrivData = psRGXCreateRenderContextIN->hPrivData;
	IMG_HANDLE hPrivDataInt = NULL;
	IMG_BYTE *ui8StaticRenderContextStateInt = NULL;
	RGX_SERVER_RENDER_CONTEXT *psRenderContextInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize =
	    (psRGXCreateRenderContextIN->ui32FrameworkCmdSize * sizeof(IMG_BYTE)) +
	    (psRGXCreateRenderContextIN->ui32StaticRenderContextStateSize * sizeof(IMG_BYTE)) + 0;

	if (unlikely(psRGXCreateRenderContextIN->ui32FrameworkCmdSize > RGXFWIF_RF_CMD_SIZE))
	{
		psRGXCreateRenderContextOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RGXCreateRenderContext_exit;
	}

	if (unlikely
	    (psRGXCreateRenderContextIN->ui32StaticRenderContextStateSize >
	     RGXFWIF_STATIC_RENDERCONTEXT_SIZE))
	{
		psRGXCreateRenderContextOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RGXCreateRenderContext_exit;
	}

	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psRGXCreateRenderContextIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psRGXCreateRenderContextIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocZMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psRGXCreateRenderContextOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXCreateRenderContext_exit;
			}
		}
	}

	if (psRGXCreateRenderContextIN->ui32FrameworkCmdSize != 0)
	{
		ui8FrameworkCmdInt = (IMG_BYTE *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psRGXCreateRenderContextIN->ui32FrameworkCmdSize * sizeof(IMG_BYTE);
	}

	/* Copy the data over */
	if (psRGXCreateRenderContextIN->ui32FrameworkCmdSize * sizeof(IMG_BYTE) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui8FrameworkCmdInt,
		     (const void __user *)psRGXCreateRenderContextIN->pui8FrameworkCmd,
		     psRGXCreateRenderContextIN->ui32FrameworkCmdSize * sizeof(IMG_BYTE)) !=
		    PVRSRV_OK)
		{
			psRGXCreateRenderContextOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXCreateRenderContext_exit;
		}
	}
	if (psRGXCreateRenderContextIN->ui32StaticRenderContextStateSize != 0)
	{
		ui8StaticRenderContextStateInt =
		    (IMG_BYTE *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psRGXCreateRenderContextIN->ui32StaticRenderContextStateSize * sizeof(IMG_BYTE);
	}

	/* Copy the data over */
	if (psRGXCreateRenderContextIN->ui32StaticRenderContextStateSize * sizeof(IMG_BYTE) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui8StaticRenderContextStateInt,
		     (const void __user *)psRGXCreateRenderContextIN->pui8StaticRenderContextState,
		     psRGXCreateRenderContextIN->ui32StaticRenderContextStateSize *
		     sizeof(IMG_BYTE)) != PVRSRV_OK)
		{
			psRGXCreateRenderContextOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXCreateRenderContext_exit;
		}
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXCreateRenderContextOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&hPrivDataInt,
				       hPrivData, PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA, IMG_TRUE);
	if (unlikely(psRGXCreateRenderContextOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXCreateRenderContext_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXCreateRenderContextOUT->eError =
	    PVRSRVRGXCreateRenderContextKM(psConnection, OSGetDevNode(psConnection),
					   psRGXCreateRenderContextIN->ui32Priority,
					   psRGXCreateRenderContextIN->sVDMCallStackAddr,
					   psRGXCreateRenderContextIN->ui32FrameworkCmdSize,
					   ui8FrameworkCmdInt,
					   hPrivDataInt,
					   psRGXCreateRenderContextIN->
					   ui32StaticRenderContextStateSize,
					   ui8StaticRenderContextStateInt,
					   psRGXCreateRenderContextIN->ui32PackedCCBSizeU8888,
					   psRGXCreateRenderContextIN->ui32ContextFlags,
					   psRGXCreateRenderContextIN->ui64RobustnessAddress,
					   psRGXCreateRenderContextIN->ui32MaxTADeadlineMS,
					   psRGXCreateRenderContextIN->ui32Max3DDeadlineMS,
					   &psRenderContextInt);
	/* Exit early if bridged call fails */
	if (unlikely(psRGXCreateRenderContextOUT->eError != PVRSRV_OK))
	{
		goto RGXCreateRenderContext_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psRGXCreateRenderContextOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
									&psRGXCreateRenderContextOUT->
									hRenderContext,
									(void *)psRenderContextInt,
									PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT,
									PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
									(PFN_HANDLE_RELEASE) &
									_RGXCreateRenderContextpsRenderContextIntRelease);
	if (unlikely(psRGXCreateRenderContextOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXCreateRenderContext_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

RGXCreateRenderContext_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (hPrivDataInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hPrivData, PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	if (psRGXCreateRenderContextOUT->eError != PVRSRV_OK)
	{
		if (psRenderContextInt)
		{
			PVRSRVRGXDestroyRenderContextKM(psRenderContextInt);
		}
	}

	/* Allocated space should be equal to the last updated offset */
	PVR_ASSERT(ui32BufferSize == ui32NextOffset);

#if defined(INTEGRITY_OS)
	if (pArrayArgsBuffer)
#else
	if (!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXDestroyRenderContext(IMG_UINT32 ui32DispatchTableEntry,
				    IMG_UINT8 * psRGXDestroyRenderContextIN_UI8,
				    IMG_UINT8 * psRGXDestroyRenderContextOUT_UI8,
				    CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXDESTROYRENDERCONTEXT *psRGXDestroyRenderContextIN =
	    (PVRSRV_BRIDGE_IN_RGXDESTROYRENDERCONTEXT *)
	    IMG_OFFSET_ADDR(psRGXDestroyRenderContextIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXDESTROYRENDERCONTEXT *psRGXDestroyRenderContextOUT =
	    (PVRSRV_BRIDGE_OUT_RGXDESTROYRENDERCONTEXT *)
	    IMG_OFFSET_ADDR(psRGXDestroyRenderContextOUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psRGXDestroyRenderContextOUT->eError =
	    PVRSRVReleaseHandleStagedUnlock(psConnection->psHandleBase,
					    (IMG_HANDLE) psRGXDestroyRenderContextIN->
					    hCleanupCookie,
					    PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT);
	if (unlikely
	    ((psRGXDestroyRenderContextOUT->eError != PVRSRV_OK)
	     && (psRGXDestroyRenderContextOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s",
			 __func__, PVRSRVGetErrorString(psRGXDestroyRenderContextOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto RGXDestroyRenderContext_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

RGXDestroyRenderContext_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXSetRenderContextPriority(IMG_UINT32 ui32DispatchTableEntry,
					IMG_UINT8 * psRGXSetRenderContextPriorityIN_UI8,
					IMG_UINT8 * psRGXSetRenderContextPriorityOUT_UI8,
					CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXSETRENDERCONTEXTPRIORITY *psRGXSetRenderContextPriorityIN =
	    (PVRSRV_BRIDGE_IN_RGXSETRENDERCONTEXTPRIORITY *)
	    IMG_OFFSET_ADDR(psRGXSetRenderContextPriorityIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXSETRENDERCONTEXTPRIORITY *psRGXSetRenderContextPriorityOUT =
	    (PVRSRV_BRIDGE_OUT_RGXSETRENDERCONTEXTPRIORITY *)
	    IMG_OFFSET_ADDR(psRGXSetRenderContextPriorityOUT_UI8, 0);

	IMG_HANDLE hRenderContext = psRGXSetRenderContextPriorityIN->hRenderContext;
	RGX_SERVER_RENDER_CONTEXT *psRenderContextInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXSetRenderContextPriorityOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psRenderContextInt,
				       hRenderContext,
				       PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT, IMG_TRUE);
	if (unlikely(psRGXSetRenderContextPriorityOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXSetRenderContextPriority_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXSetRenderContextPriorityOUT->eError =
	    PVRSRVRGXSetRenderContextPriorityKM(psConnection, OSGetDevNode(psConnection),
						psRenderContextInt,
						psRGXSetRenderContextPriorityIN->ui32Priority);

RGXSetRenderContextPriority_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psRenderContextInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hRenderContext,
					    PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXRenderContextStalled(IMG_UINT32 ui32DispatchTableEntry,
				    IMG_UINT8 * psRGXRenderContextStalledIN_UI8,
				    IMG_UINT8 * psRGXRenderContextStalledOUT_UI8,
				    CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXRENDERCONTEXTSTALLED *psRGXRenderContextStalledIN =
	    (PVRSRV_BRIDGE_IN_RGXRENDERCONTEXTSTALLED *)
	    IMG_OFFSET_ADDR(psRGXRenderContextStalledIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXRENDERCONTEXTSTALLED *psRGXRenderContextStalledOUT =
	    (PVRSRV_BRIDGE_OUT_RGXRENDERCONTEXTSTALLED *)
	    IMG_OFFSET_ADDR(psRGXRenderContextStalledOUT_UI8, 0);

	IMG_HANDLE hRenderContext = psRGXRenderContextStalledIN->hRenderContext;
	RGX_SERVER_RENDER_CONTEXT *psRenderContextInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXRenderContextStalledOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psRenderContextInt,
				       hRenderContext,
				       PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT, IMG_TRUE);
	if (unlikely(psRGXRenderContextStalledOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXRenderContextStalled_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXRenderContextStalledOUT->eError = RGXRenderContextStalledKM(psRenderContextInt);

RGXRenderContextStalled_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psRenderContextInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hRenderContext,
					    PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXKickTA3D2(IMG_UINT32 ui32DispatchTableEntry,
			 IMG_UINT8 * psRGXKickTA3D2IN_UI8,
			 IMG_UINT8 * psRGXKickTA3D2OUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXKICKTA3D2 *psRGXKickTA3D2IN =
	    (PVRSRV_BRIDGE_IN_RGXKICKTA3D2 *) IMG_OFFSET_ADDR(psRGXKickTA3D2IN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXKICKTA3D2 *psRGXKickTA3D2OUT =
	    (PVRSRV_BRIDGE_OUT_RGXKICKTA3D2 *) IMG_OFFSET_ADDR(psRGXKickTA3D2OUT_UI8, 0);

	IMG_HANDLE hRenderContext = psRGXKickTA3D2IN->hRenderContext;
	RGX_SERVER_RENDER_CONTEXT *psRenderContextInt = NULL;
	SYNC_PRIMITIVE_BLOCK **psClientTAFenceSyncPrimBlockInt = NULL;
	IMG_HANDLE *hClientTAFenceSyncPrimBlockInt2 = NULL;
	IMG_UINT32 *ui32ClientTAFenceSyncOffsetInt = NULL;
	IMG_UINT32 *ui32ClientTAFenceValueInt = NULL;
	SYNC_PRIMITIVE_BLOCK **psClientTAUpdateSyncPrimBlockInt = NULL;
	IMG_HANDLE *hClientTAUpdateSyncPrimBlockInt2 = NULL;
	IMG_UINT32 *ui32ClientTAUpdateSyncOffsetInt = NULL;
	IMG_UINT32 *ui32ClientTAUpdateValueInt = NULL;
	SYNC_PRIMITIVE_BLOCK **psClient3DUpdateSyncPrimBlockInt = NULL;
	IMG_HANDLE *hClient3DUpdateSyncPrimBlockInt2 = NULL;
	IMG_UINT32 *ui32Client3DUpdateSyncOffsetInt = NULL;
	IMG_UINT32 *ui32Client3DUpdateValueInt = NULL;
	IMG_HANDLE hPRFenceUFOSyncPrimBlock = psRGXKickTA3D2IN->hPRFenceUFOSyncPrimBlock;
	SYNC_PRIMITIVE_BLOCK *psPRFenceUFOSyncPrimBlockInt = NULL;
	IMG_CHAR *uiUpdateFenceNameInt = NULL;
	IMG_CHAR *uiUpdateFenceName3DInt = NULL;
	IMG_BYTE *ui8TACmdInt = NULL;
	IMG_BYTE *ui83DPRCmdInt = NULL;
	IMG_BYTE *ui83DCmdInt = NULL;
	IMG_HANDLE hKMHWRTDataSet = psRGXKickTA3D2IN->hKMHWRTDataSet;
	RGX_KM_HW_RT_DATASET *psKMHWRTDataSetInt = NULL;
	IMG_HANDLE hZSBuffer = psRGXKickTA3D2IN->hZSBuffer;
	RGX_ZSBUFFER_DATA *psZSBufferInt = NULL;
	IMG_HANDLE hMSAAScratchBuffer = psRGXKickTA3D2IN->hMSAAScratchBuffer;
	RGX_ZSBUFFER_DATA *psMSAAScratchBufferInt = NULL;
	IMG_UINT32 *ui32SyncPMRFlagsInt = NULL;
	PMR **psSyncPMRsInt = NULL;
	IMG_HANDLE *hSyncPMRsInt2 = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize =
	    (psRGXKickTA3D2IN->ui32ClientTAFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *)) +
	    (psRGXKickTA3D2IN->ui32ClientTAFenceCount * sizeof(IMG_HANDLE)) +
	    (psRGXKickTA3D2IN->ui32ClientTAFenceCount * sizeof(IMG_UINT32)) +
	    (psRGXKickTA3D2IN->ui32ClientTAFenceCount * sizeof(IMG_UINT32)) +
	    (psRGXKickTA3D2IN->ui32ClientTAUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *)) +
	    (psRGXKickTA3D2IN->ui32ClientTAUpdateCount * sizeof(IMG_HANDLE)) +
	    (psRGXKickTA3D2IN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32)) +
	    (psRGXKickTA3D2IN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32)) +
	    (psRGXKickTA3D2IN->ui32Client3DUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *)) +
	    (psRGXKickTA3D2IN->ui32Client3DUpdateCount * sizeof(IMG_HANDLE)) +
	    (psRGXKickTA3D2IN->ui32Client3DUpdateCount * sizeof(IMG_UINT32)) +
	    (psRGXKickTA3D2IN->ui32Client3DUpdateCount * sizeof(IMG_UINT32)) +
	    (PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR)) +
	    (PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR)) +
	    (psRGXKickTA3D2IN->ui32TACmdSize * sizeof(IMG_BYTE)) +
	    (psRGXKickTA3D2IN->ui323DPRCmdSize * sizeof(IMG_BYTE)) +
	    (psRGXKickTA3D2IN->ui323DCmdSize * sizeof(IMG_BYTE)) +
	    (psRGXKickTA3D2IN->ui32SyncPMRCount * sizeof(IMG_UINT32)) +
	    (psRGXKickTA3D2IN->ui32SyncPMRCount * sizeof(PMR *)) +
	    (psRGXKickTA3D2IN->ui32SyncPMRCount * sizeof(IMG_HANDLE)) + 0;

	if (unlikely(psRGXKickTA3D2IN->ui32ClientTAFenceCount > PVRSRV_MAX_SYNCS))
	{
		psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RGXKickTA3D2_exit;
	}

	if (unlikely(psRGXKickTA3D2IN->ui32ClientTAUpdateCount > PVRSRV_MAX_SYNCS))
	{
		psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RGXKickTA3D2_exit;
	}

	if (unlikely(psRGXKickTA3D2IN->ui32Client3DUpdateCount > PVRSRV_MAX_SYNCS))
	{
		psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RGXKickTA3D2_exit;
	}

	if (unlikely(psRGXKickTA3D2IN->ui32TACmdSize > RGXFWIF_DM_INDEPENDENT_KICK_CMD_SIZE))
	{
		psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RGXKickTA3D2_exit;
	}

	if (unlikely(psRGXKickTA3D2IN->ui323DPRCmdSize > RGXFWIF_DM_INDEPENDENT_KICK_CMD_SIZE))
	{
		psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RGXKickTA3D2_exit;
	}

	if (unlikely(psRGXKickTA3D2IN->ui323DCmdSize > RGXFWIF_DM_INDEPENDENT_KICK_CMD_SIZE))
	{
		psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RGXKickTA3D2_exit;
	}

	if (unlikely(psRGXKickTA3D2IN->ui32SyncPMRCount > PVRSRV_MAX_SYNCS))
	{
		psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RGXKickTA3D2_exit;
	}

	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psRGXKickTA3D2IN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psRGXKickTA3D2IN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocZMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXKickTA3D2_exit;
			}
		}
	}

	if (psRGXKickTA3D2IN->ui32ClientTAFenceCount != 0)
	{
		psClientTAFenceSyncPrimBlockInt =
		    (SYNC_PRIMITIVE_BLOCK **) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psRGXKickTA3D2IN->ui32ClientTAFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *);
		hClientTAFenceSyncPrimBlockInt2 =
		    (IMG_HANDLE *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXKickTA3D2IN->ui32ClientTAFenceCount * sizeof(IMG_HANDLE);
	}

	/* Copy the data over */
	if (psRGXKickTA3D2IN->ui32ClientTAFenceCount * sizeof(IMG_HANDLE) > 0)
	{
		if (OSCopyFromUser
		    (NULL, hClientTAFenceSyncPrimBlockInt2,
		     (const void __user *)psRGXKickTA3D2IN->phClientTAFenceSyncPrimBlock,
		     psRGXKickTA3D2IN->ui32ClientTAFenceCount * sizeof(IMG_HANDLE)) != PVRSRV_OK)
		{
			psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXKickTA3D2_exit;
		}
	}
	if (psRGXKickTA3D2IN->ui32ClientTAFenceCount != 0)
	{
		ui32ClientTAFenceSyncOffsetInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXKickTA3D2IN->ui32ClientTAFenceCount * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psRGXKickTA3D2IN->ui32ClientTAFenceCount * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32ClientTAFenceSyncOffsetInt,
		     (const void __user *)psRGXKickTA3D2IN->pui32ClientTAFenceSyncOffset,
		     psRGXKickTA3D2IN->ui32ClientTAFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK)
		{
			psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXKickTA3D2_exit;
		}
	}
	if (psRGXKickTA3D2IN->ui32ClientTAFenceCount != 0)
	{
		ui32ClientTAFenceValueInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXKickTA3D2IN->ui32ClientTAFenceCount * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psRGXKickTA3D2IN->ui32ClientTAFenceCount * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32ClientTAFenceValueInt,
		     (const void __user *)psRGXKickTA3D2IN->pui32ClientTAFenceValue,
		     psRGXKickTA3D2IN->ui32ClientTAFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK)
		{
			psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXKickTA3D2_exit;
		}
	}
	if (psRGXKickTA3D2IN->ui32ClientTAUpdateCount != 0)
	{
		psClientTAUpdateSyncPrimBlockInt =
		    (SYNC_PRIMITIVE_BLOCK **) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psRGXKickTA3D2IN->ui32ClientTAUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *);
		hClientTAUpdateSyncPrimBlockInt2 =
		    (IMG_HANDLE *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXKickTA3D2IN->ui32ClientTAUpdateCount * sizeof(IMG_HANDLE);
	}

	/* Copy the data over */
	if (psRGXKickTA3D2IN->ui32ClientTAUpdateCount * sizeof(IMG_HANDLE) > 0)
	{
		if (OSCopyFromUser
		    (NULL, hClientTAUpdateSyncPrimBlockInt2,
		     (const void __user *)psRGXKickTA3D2IN->phClientTAUpdateSyncPrimBlock,
		     psRGXKickTA3D2IN->ui32ClientTAUpdateCount * sizeof(IMG_HANDLE)) != PVRSRV_OK)
		{
			psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXKickTA3D2_exit;
		}
	}
	if (psRGXKickTA3D2IN->ui32ClientTAUpdateCount != 0)
	{
		ui32ClientTAUpdateSyncOffsetInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXKickTA3D2IN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psRGXKickTA3D2IN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32ClientTAUpdateSyncOffsetInt,
		     (const void __user *)psRGXKickTA3D2IN->pui32ClientTAUpdateSyncOffset,
		     psRGXKickTA3D2IN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK)
		{
			psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXKickTA3D2_exit;
		}
	}
	if (psRGXKickTA3D2IN->ui32ClientTAUpdateCount != 0)
	{
		ui32ClientTAUpdateValueInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXKickTA3D2IN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psRGXKickTA3D2IN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32ClientTAUpdateValueInt,
		     (const void __user *)psRGXKickTA3D2IN->pui32ClientTAUpdateValue,
		     psRGXKickTA3D2IN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK)
		{
			psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXKickTA3D2_exit;
		}
	}
	if (psRGXKickTA3D2IN->ui32Client3DUpdateCount != 0)
	{
		psClient3DUpdateSyncPrimBlockInt =
		    (SYNC_PRIMITIVE_BLOCK **) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psRGXKickTA3D2IN->ui32Client3DUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *);
		hClient3DUpdateSyncPrimBlockInt2 =
		    (IMG_HANDLE *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXKickTA3D2IN->ui32Client3DUpdateCount * sizeof(IMG_HANDLE);
	}

	/* Copy the data over */
	if (psRGXKickTA3D2IN->ui32Client3DUpdateCount * sizeof(IMG_HANDLE) > 0)
	{
		if (OSCopyFromUser
		    (NULL, hClient3DUpdateSyncPrimBlockInt2,
		     (const void __user *)psRGXKickTA3D2IN->phClient3DUpdateSyncPrimBlock,
		     psRGXKickTA3D2IN->ui32Client3DUpdateCount * sizeof(IMG_HANDLE)) != PVRSRV_OK)
		{
			psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXKickTA3D2_exit;
		}
	}
	if (psRGXKickTA3D2IN->ui32Client3DUpdateCount != 0)
	{
		ui32Client3DUpdateSyncOffsetInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXKickTA3D2IN->ui32Client3DUpdateCount * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psRGXKickTA3D2IN->ui32Client3DUpdateCount * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32Client3DUpdateSyncOffsetInt,
		     (const void __user *)psRGXKickTA3D2IN->pui32Client3DUpdateSyncOffset,
		     psRGXKickTA3D2IN->ui32Client3DUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK)
		{
			psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXKickTA3D2_exit;
		}
	}
	if (psRGXKickTA3D2IN->ui32Client3DUpdateCount != 0)
	{
		ui32Client3DUpdateValueInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXKickTA3D2IN->ui32Client3DUpdateCount * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psRGXKickTA3D2IN->ui32Client3DUpdateCount * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32Client3DUpdateValueInt,
		     (const void __user *)psRGXKickTA3D2IN->pui32Client3DUpdateValue,
		     psRGXKickTA3D2IN->ui32Client3DUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK)
		{
			psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXKickTA3D2_exit;
		}
	}

	{
		uiUpdateFenceNameInt =
		    (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR);
	}

	/* Copy the data over */
	if (PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiUpdateFenceNameInt,
		     (const void __user *)psRGXKickTA3D2IN->puiUpdateFenceName,
		     PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXKickTA3D2_exit;
		}
		((IMG_CHAR *) uiUpdateFenceNameInt)[(PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR)) -
						    1] = '\0';
	}

	{
		uiUpdateFenceName3DInt =
		    (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR);
	}

	/* Copy the data over */
	if (PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiUpdateFenceName3DInt,
		     (const void __user *)psRGXKickTA3D2IN->puiUpdateFenceName3D,
		     PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXKickTA3D2_exit;
		}
		((IMG_CHAR *) uiUpdateFenceName3DInt)[(PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR)) -
						      1] = '\0';
	}
	if (psRGXKickTA3D2IN->ui32TACmdSize != 0)
	{
		ui8TACmdInt = (IMG_BYTE *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXKickTA3D2IN->ui32TACmdSize * sizeof(IMG_BYTE);
	}

	/* Copy the data over */
	if (psRGXKickTA3D2IN->ui32TACmdSize * sizeof(IMG_BYTE) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui8TACmdInt, (const void __user *)psRGXKickTA3D2IN->pui8TACmd,
		     psRGXKickTA3D2IN->ui32TACmdSize * sizeof(IMG_BYTE)) != PVRSRV_OK)
		{
			psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXKickTA3D2_exit;
		}
	}
	if (psRGXKickTA3D2IN->ui323DPRCmdSize != 0)
	{
		ui83DPRCmdInt = (IMG_BYTE *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXKickTA3D2IN->ui323DPRCmdSize * sizeof(IMG_BYTE);
	}

	/* Copy the data over */
	if (psRGXKickTA3D2IN->ui323DPRCmdSize * sizeof(IMG_BYTE) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui83DPRCmdInt, (const void __user *)psRGXKickTA3D2IN->pui83DPRCmd,
		     psRGXKickTA3D2IN->ui323DPRCmdSize * sizeof(IMG_BYTE)) != PVRSRV_OK)
		{
			psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXKickTA3D2_exit;
		}
	}
	if (psRGXKickTA3D2IN->ui323DCmdSize != 0)
	{
		ui83DCmdInt = (IMG_BYTE *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXKickTA3D2IN->ui323DCmdSize * sizeof(IMG_BYTE);
	}

	/* Copy the data over */
	if (psRGXKickTA3D2IN->ui323DCmdSize * sizeof(IMG_BYTE) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui83DCmdInt, (const void __user *)psRGXKickTA3D2IN->pui83DCmd,
		     psRGXKickTA3D2IN->ui323DCmdSize * sizeof(IMG_BYTE)) != PVRSRV_OK)
		{
			psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXKickTA3D2_exit;
		}
	}
	if (psRGXKickTA3D2IN->ui32SyncPMRCount != 0)
	{
		ui32SyncPMRFlagsInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXKickTA3D2IN->ui32SyncPMRCount * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psRGXKickTA3D2IN->ui32SyncPMRCount * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32SyncPMRFlagsInt,
		     (const void __user *)psRGXKickTA3D2IN->pui32SyncPMRFlags,
		     psRGXKickTA3D2IN->ui32SyncPMRCount * sizeof(IMG_UINT32)) != PVRSRV_OK)
		{
			psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXKickTA3D2_exit;
		}
	}
	if (psRGXKickTA3D2IN->ui32SyncPMRCount != 0)
	{
		psSyncPMRsInt = (PMR **) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXKickTA3D2IN->ui32SyncPMRCount * sizeof(PMR *);
		hSyncPMRsInt2 = (IMG_HANDLE *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXKickTA3D2IN->ui32SyncPMRCount * sizeof(IMG_HANDLE);
	}

	/* Copy the data over */
	if (psRGXKickTA3D2IN->ui32SyncPMRCount * sizeof(IMG_HANDLE) > 0)
	{
		if (OSCopyFromUser
		    (NULL, hSyncPMRsInt2, (const void __user *)psRGXKickTA3D2IN->phSyncPMRs,
		     psRGXKickTA3D2IN->ui32SyncPMRCount * sizeof(IMG_HANDLE)) != PVRSRV_OK)
		{
			psRGXKickTA3D2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXKickTA3D2_exit;
		}
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXKickTA3D2OUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psRenderContextInt,
				       hRenderContext,
				       PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT, IMG_TRUE);
	if (unlikely(psRGXKickTA3D2OUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXKickTA3D2_exit;
	}

	{
		IMG_UINT32 i;

		for (i = 0; i < psRGXKickTA3D2IN->ui32ClientTAFenceCount; i++)
		{
			/* Look up the address from the handle */
			psRGXKickTA3D2OUT->eError =
			    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
						       (void **)&psClientTAFenceSyncPrimBlockInt[i],
						       hClientTAFenceSyncPrimBlockInt2[i],
						       PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
						       IMG_TRUE);
			if (unlikely(psRGXKickTA3D2OUT->eError != PVRSRV_OK))
			{
				UnlockHandle(psConnection->psHandleBase);
				goto RGXKickTA3D2_exit;
			}
		}
	}

	{
		IMG_UINT32 i;

		for (i = 0; i < psRGXKickTA3D2IN->ui32ClientTAUpdateCount; i++)
		{
			/* Look up the address from the handle */
			psRGXKickTA3D2OUT->eError =
			    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
						       (void **)
						       &psClientTAUpdateSyncPrimBlockInt[i],
						       hClientTAUpdateSyncPrimBlockInt2[i],
						       PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
						       IMG_TRUE);
			if (unlikely(psRGXKickTA3D2OUT->eError != PVRSRV_OK))
			{
				UnlockHandle(psConnection->psHandleBase);
				goto RGXKickTA3D2_exit;
			}
		}
	}

	{
		IMG_UINT32 i;

		for (i = 0; i < psRGXKickTA3D2IN->ui32Client3DUpdateCount; i++)
		{
			/* Look up the address from the handle */
			psRGXKickTA3D2OUT->eError =
			    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
						       (void **)
						       &psClient3DUpdateSyncPrimBlockInt[i],
						       hClient3DUpdateSyncPrimBlockInt2[i],
						       PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
						       IMG_TRUE);
			if (unlikely(psRGXKickTA3D2OUT->eError != PVRSRV_OK))
			{
				UnlockHandle(psConnection->psHandleBase);
				goto RGXKickTA3D2_exit;
			}
		}
	}

	/* Look up the address from the handle */
	psRGXKickTA3D2OUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psPRFenceUFOSyncPrimBlockInt,
				       hPRFenceUFOSyncPrimBlock,
				       PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK, IMG_TRUE);
	if (unlikely(psRGXKickTA3D2OUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXKickTA3D2_exit;
	}

	if (psRGXKickTA3D2IN->hKMHWRTDataSet)
	{
		/* Look up the address from the handle */
		psRGXKickTA3D2OUT->eError =
		    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
					       (void **)&psKMHWRTDataSetInt,
					       hKMHWRTDataSet,
					       PVRSRV_HANDLE_TYPE_RGX_KM_HW_RT_DATASET, IMG_TRUE);
		if (unlikely(psRGXKickTA3D2OUT->eError != PVRSRV_OK))
		{
			UnlockHandle(psConnection->psHandleBase);
			goto RGXKickTA3D2_exit;
		}
	}

	if (psRGXKickTA3D2IN->hZSBuffer)
	{
		/* Look up the address from the handle */
		psRGXKickTA3D2OUT->eError =
		    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
					       (void **)&psZSBufferInt,
					       hZSBuffer,
					       PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER, IMG_TRUE);
		if (unlikely(psRGXKickTA3D2OUT->eError != PVRSRV_OK))
		{
			UnlockHandle(psConnection->psHandleBase);
			goto RGXKickTA3D2_exit;
		}
	}

	if (psRGXKickTA3D2IN->hMSAAScratchBuffer)
	{
		/* Look up the address from the handle */
		psRGXKickTA3D2OUT->eError =
		    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
					       (void **)&psMSAAScratchBufferInt,
					       hMSAAScratchBuffer,
					       PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER, IMG_TRUE);
		if (unlikely(psRGXKickTA3D2OUT->eError != PVRSRV_OK))
		{
			UnlockHandle(psConnection->psHandleBase);
			goto RGXKickTA3D2_exit;
		}
	}

	{
		IMG_UINT32 i;

		for (i = 0; i < psRGXKickTA3D2IN->ui32SyncPMRCount; i++)
		{
			/* Look up the address from the handle */
			psRGXKickTA3D2OUT->eError =
			    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
						       (void **)&psSyncPMRsInt[i],
						       hSyncPMRsInt2[i],
						       PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
			if (unlikely(psRGXKickTA3D2OUT->eError != PVRSRV_OK))
			{
				UnlockHandle(psConnection->psHandleBase);
				goto RGXKickTA3D2_exit;
			}
		}
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXKickTA3D2OUT->eError =
	    PVRSRVRGXKickTA3DKM(psRenderContextInt,
				psRGXKickTA3D2IN->ui32ClientCacheOpSeqNum,
				psRGXKickTA3D2IN->ui32ClientTAFenceCount,
				psClientTAFenceSyncPrimBlockInt,
				ui32ClientTAFenceSyncOffsetInt,
				ui32ClientTAFenceValueInt,
				psRGXKickTA3D2IN->ui32ClientTAUpdateCount,
				psClientTAUpdateSyncPrimBlockInt,
				ui32ClientTAUpdateSyncOffsetInt,
				ui32ClientTAUpdateValueInt,
				psRGXKickTA3D2IN->ui32Client3DUpdateCount,
				psClient3DUpdateSyncPrimBlockInt,
				ui32Client3DUpdateSyncOffsetInt,
				ui32Client3DUpdateValueInt,
				psPRFenceUFOSyncPrimBlockInt,
				psRGXKickTA3D2IN->ui32FRFenceUFOSyncOffset,
				psRGXKickTA3D2IN->ui32FRFenceValue,
				psRGXKickTA3D2IN->hCheckFence,
				psRGXKickTA3D2IN->hUpdateTimeline,
				&psRGXKickTA3D2OUT->hUpdateFence,
				uiUpdateFenceNameInt,
				psRGXKickTA3D2IN->hCheckFence3D,
				psRGXKickTA3D2IN->hUpdateTimeline3D,
				&psRGXKickTA3D2OUT->hUpdateFence3D,
				uiUpdateFenceName3DInt,
				psRGXKickTA3D2IN->ui32TACmdSize,
				ui8TACmdInt,
				psRGXKickTA3D2IN->ui323DPRCmdSize,
				ui83DPRCmdInt,
				psRGXKickTA3D2IN->ui323DCmdSize,
				ui83DCmdInt,
				psRGXKickTA3D2IN->ui32ExtJobRef,
				psRGXKickTA3D2IN->bbKickTA,
				psRGXKickTA3D2IN->bbKickPR,
				psRGXKickTA3D2IN->bbKick3D,
				psRGXKickTA3D2IN->bbAbort,
				psRGXKickTA3D2IN->ui32PDumpFlags,
				psKMHWRTDataSetInt,
				psZSBufferInt,
				psMSAAScratchBufferInt,
				psRGXKickTA3D2IN->ui32SyncPMRCount,
				ui32SyncPMRFlagsInt,
				psSyncPMRsInt,
				psRGXKickTA3D2IN->ui32RenderTargetSize,
				psRGXKickTA3D2IN->ui32NumberOfDrawCalls,
				psRGXKickTA3D2IN->ui32NumberOfIndices,
				psRGXKickTA3D2IN->ui32NumberOfMRTs, psRGXKickTA3D2IN->ui64Deadline);

RGXKickTA3D2_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psRenderContextInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hRenderContext,
					    PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT);
	}

	if (hClientTAFenceSyncPrimBlockInt2)
	{
		IMG_UINT32 i;

		for (i = 0; i < psRGXKickTA3D2IN->ui32ClientTAFenceCount; i++)
		{

			/* Unreference the previously looked up handle */
			if (hClientTAFenceSyncPrimBlockInt2[i])
			{
				PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
							    hClientTAFenceSyncPrimBlockInt2[i],
							    PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
			}
		}
	}

	if (hClientTAUpdateSyncPrimBlockInt2)
	{
		IMG_UINT32 i;

		for (i = 0; i < psRGXKickTA3D2IN->ui32ClientTAUpdateCount; i++)
		{

			/* Unreference the previously looked up handle */
			if (hClientTAUpdateSyncPrimBlockInt2[i])
			{
				PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
							    hClientTAUpdateSyncPrimBlockInt2[i],
							    PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
			}
		}
	}

	if (hClient3DUpdateSyncPrimBlockInt2)
	{
		IMG_UINT32 i;

		for (i = 0; i < psRGXKickTA3D2IN->ui32Client3DUpdateCount; i++)
		{

			/* Unreference the previously looked up handle */
			if (hClient3DUpdateSyncPrimBlockInt2[i])
			{
				PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
							    hClient3DUpdateSyncPrimBlockInt2[i],
							    PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
			}
		}
	}

	/* Unreference the previously looked up handle */
	if (psPRFenceUFOSyncPrimBlockInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hPRFenceUFOSyncPrimBlock,
					    PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
	}

	if (psRGXKickTA3D2IN->hKMHWRTDataSet)
	{

		/* Unreference the previously looked up handle */
		if (psKMHWRTDataSetInt)
		{
			PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
						    hKMHWRTDataSet,
						    PVRSRV_HANDLE_TYPE_RGX_KM_HW_RT_DATASET);
		}
	}

	if (psRGXKickTA3D2IN->hZSBuffer)
	{

		/* Unreference the previously looked up handle */
		if (psZSBufferInt)
		{
			PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
						    hZSBuffer,
						    PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER);
		}
	}

	if (psRGXKickTA3D2IN->hMSAAScratchBuffer)
	{

		/* Unreference the previously looked up handle */
		if (psMSAAScratchBufferInt)
		{
			PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
						    hMSAAScratchBuffer,
						    PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER);
		}
	}

	if (hSyncPMRsInt2)
	{
		IMG_UINT32 i;

		for (i = 0; i < psRGXKickTA3D2IN->ui32SyncPMRCount; i++)
		{

			/* Unreference the previously looked up handle */
			if (hSyncPMRsInt2[i])
			{
				PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
							    hSyncPMRsInt2[i],
							    PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
			}
		}
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	/* Allocated space should be equal to the last updated offset */
	PVR_ASSERT(ui32BufferSize == ui32NextOffset);

#if defined(INTEGRITY_OS)
	if (pArrayArgsBuffer)
#else
	if (!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXSetRenderContextProperty(IMG_UINT32 ui32DispatchTableEntry,
					IMG_UINT8 * psRGXSetRenderContextPropertyIN_UI8,
					IMG_UINT8 * psRGXSetRenderContextPropertyOUT_UI8,
					CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXSETRENDERCONTEXTPROPERTY *psRGXSetRenderContextPropertyIN =
	    (PVRSRV_BRIDGE_IN_RGXSETRENDERCONTEXTPROPERTY *)
	    IMG_OFFSET_ADDR(psRGXSetRenderContextPropertyIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXSETRENDERCONTEXTPROPERTY *psRGXSetRenderContextPropertyOUT =
	    (PVRSRV_BRIDGE_OUT_RGXSETRENDERCONTEXTPROPERTY *)
	    IMG_OFFSET_ADDR(psRGXSetRenderContextPropertyOUT_UI8, 0);

	IMG_HANDLE hRenderContext = psRGXSetRenderContextPropertyIN->hRenderContext;
	RGX_SERVER_RENDER_CONTEXT *psRenderContextInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXSetRenderContextPropertyOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psRenderContextInt,
				       hRenderContext,
				       PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT, IMG_TRUE);
	if (unlikely(psRGXSetRenderContextPropertyOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXSetRenderContextProperty_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXSetRenderContextPropertyOUT->eError =
	    PVRSRVRGXSetRenderContextPropertyKM(psRenderContextInt,
						psRGXSetRenderContextPropertyIN->ui32Property,
						psRGXSetRenderContextPropertyIN->ui64Input,
						&psRGXSetRenderContextPropertyOUT->ui64Output);

RGXSetRenderContextProperty_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psRenderContextInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hRenderContext,
					    PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

PVRSRV_ERROR InitRGXTA3DBridge(void);
PVRSRV_ERROR DeinitRGXTA3DBridge(void);

/*
 * Register all RGXTA3D functions with services
 */
PVRSRV_ERROR InitRGXTA3DBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXCREATEHWRTDATASET,
			      PVRSRVBridgeRGXCreateHWRTDataSet, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYHWRTDATASET,
			      PVRSRVBridgeRGXDestroyHWRTDataSet, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXCREATEZSBUFFER,
			      PVRSRVBridgeRGXCreateZSBuffer, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYZSBUFFER,
			      PVRSRVBridgeRGXDestroyZSBuffer, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXPOPULATEZSBUFFER,
			      PVRSRVBridgeRGXPopulateZSBuffer, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXUNPOPULATEZSBUFFER,
			      PVRSRVBridgeRGXUnpopulateZSBuffer, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXCREATEFREELIST,
			      PVRSRVBridgeRGXCreateFreeList, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYFREELIST,
			      PVRSRVBridgeRGXDestroyFreeList, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXCREATERENDERCONTEXT,
			      PVRSRVBridgeRGXCreateRenderContext, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYRENDERCONTEXT,
			      PVRSRVBridgeRGXDestroyRenderContext, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D,
			      PVRSRV_BRIDGE_RGXTA3D_RGXSETRENDERCONTEXTPRIORITY,
			      PVRSRVBridgeRGXSetRenderContextPriority, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXRENDERCONTEXTSTALLED,
			      PVRSRVBridgeRGXRenderContextStalled, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXKICKTA3D2,
			      PVRSRVBridgeRGXKickTA3D2, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D,
			      PVRSRV_BRIDGE_RGXTA3D_RGXSETRENDERCONTEXTPROPERTY,
			      PVRSRVBridgeRGXSetRenderContextProperty, NULL);

	return PVRSRV_OK;
}

/*
 * Unregister all rgxta3d functions with services
 */
PVRSRV_ERROR DeinitRGXTA3DBridge(void)
{

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXCREATEHWRTDATASET);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYHWRTDATASET);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXCREATEZSBUFFER);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYZSBUFFER);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXPOPULATEZSBUFFER);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXUNPOPULATEZSBUFFER);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXCREATEFREELIST);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYFREELIST);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D,
				PVRSRV_BRIDGE_RGXTA3D_RGXCREATERENDERCONTEXT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D,
				PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYRENDERCONTEXT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D,
				PVRSRV_BRIDGE_RGXTA3D_RGXSETRENDERCONTEXTPRIORITY);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D,
				PVRSRV_BRIDGE_RGXTA3D_RGXRENDERCONTEXTSTALLED);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXKICKTA3D2);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D,
				PVRSRV_BRIDGE_RGXTA3D_RGXSETRENDERCONTEXTPROPERTY);

	return PVRSRV_OK;
}
