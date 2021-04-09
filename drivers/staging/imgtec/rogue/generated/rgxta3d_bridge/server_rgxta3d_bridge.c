/*************************************************************************/ /*!
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
*/ /**************************************************************************/

#include <stddef.h>
#include <linux/uaccess.h>

#include "osfunc.h"
#include "img_defs.h"

#include "rgxta3d.h"


#include "common_rgxta3d_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#include "rgx_bridge.h"
#include "srvcore.h"
#include "handle.h"

#include <linux/slab.h>






/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeRGXCreateHWRTData(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCREATEHWRTDATA *psRGXCreateHWRTDataIN,
					  PVRSRV_BRIDGE_OUT_RGXCREATEHWRTDATA *psRGXCreateHWRTDataOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_FREELIST * *psapsFreeListsInt = NULL;
	IMG_HANDLE *hapsFreeListsInt2 = NULL;
	RGX_RTDATA_CLEANUP_DATA * psCleanupCookieInt = NULL;
	DEVMEM_MEMDESC * psRTACtlMemDescInt = NULL;
	DEVMEM_MEMDESC * pssHWRTDataMemDescInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(RGXFW_MAX_FREELISTS * sizeof(RGX_FREELIST *)) +
			(RGXFW_MAX_FREELISTS * sizeof(IMG_HANDLE)) +
			0;




	psRGXCreateHWRTDataOUT->hCleanupCookie = NULL;

	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psRGXCreateHWRTDataIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psRGXCreateHWRTDataIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psRGXCreateHWRTDataOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXCreateHWRTData_exit;
			}
		}
	}

	
	{
		psapsFreeListsInt = (RGX_FREELIST **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += RGXFW_MAX_FREELISTS * sizeof(RGX_FREELIST *);
		hapsFreeListsInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += RGXFW_MAX_FREELISTS * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (RGXFW_MAX_FREELISTS * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hapsFreeListsInt2, psRGXCreateHWRTDataIN->phapsFreeLists, RGXFW_MAX_FREELISTS * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXCreateHWRTDataOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXCreateHWRTData_exit;
				}
			}

	/* Lock over handle lookup. */
	LockHandle();





	{
		IMG_UINT32 i;

		for (i=0;i<RGXFW_MAX_FREELISTS;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXCreateHWRTDataOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psapsFreeListsInt[i],
											hapsFreeListsInt2[i],
											PVRSRV_HANDLE_TYPE_RGX_FREELIST,
											IMG_TRUE);
					if(psRGXCreateHWRTDataOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXCreateHWRTData_exit;
					}
				}
		}
	}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXCreateHWRTDataOUT->eError =
		RGXCreateHWRTData(psConnection, OSGetDevData(psConnection),
					psRGXCreateHWRTDataIN->ui32RenderTarget,
					psRGXCreateHWRTDataIN->sPMMlistDevVAddr,
					psRGXCreateHWRTDataIN->sVFPPageTableAddr,
					psapsFreeListsInt,
					&psCleanupCookieInt,
					&psRTACtlMemDescInt,
					psRGXCreateHWRTDataIN->ui32PPPScreen,
					psRGXCreateHWRTDataIN->ui32PPPGridOffset,
					psRGXCreateHWRTDataIN->ui64PPPMultiSampleCtl,
					psRGXCreateHWRTDataIN->ui32TPCStride,
					psRGXCreateHWRTDataIN->sTailPtrsDevVAddr,
					psRGXCreateHWRTDataIN->ui32TPCSize,
					psRGXCreateHWRTDataIN->ui32TEScreen,
					psRGXCreateHWRTDataIN->ui32TEAA,
					psRGXCreateHWRTDataIN->ui32TEMTILE1,
					psRGXCreateHWRTDataIN->ui32TEMTILE2,
					psRGXCreateHWRTDataIN->ui32MTileStride,
					psRGXCreateHWRTDataIN->ui32ui32ISPMergeLowerX,
					psRGXCreateHWRTDataIN->ui32ui32ISPMergeLowerY,
					psRGXCreateHWRTDataIN->ui32ui32ISPMergeUpperX,
					psRGXCreateHWRTDataIN->ui32ui32ISPMergeUpperY,
					psRGXCreateHWRTDataIN->ui32ui32ISPMergeScaleX,
					psRGXCreateHWRTDataIN->ui32ui32ISPMergeScaleY,
					psRGXCreateHWRTDataIN->ui16MaxRTs,
					&pssHWRTDataMemDescInt,
					&psRGXCreateHWRTDataOUT->ui32FWHWRTData);
	/* Exit early if bridged call fails */
	if(psRGXCreateHWRTDataOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateHWRTData_exit;
	}

	/* Lock over handle creation. */
	LockHandle();





	psRGXCreateHWRTDataOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,

							&psRGXCreateHWRTDataOUT->hCleanupCookie,
							(void *) psCleanupCookieInt,
							PVRSRV_HANDLE_TYPE_RGX_RTDATA_CLEANUP,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&RGXDestroyHWRTData);
	if (psRGXCreateHWRTDataOUT->eError != PVRSRV_OK)
	{
		UnlockHandle();
		goto RGXCreateHWRTData_exit;
	}






	psRGXCreateHWRTDataOUT->eError = PVRSRVAllocSubHandleUnlocked(psConnection->psHandleBase,

							&psRGXCreateHWRTDataOUT->hRTACtlMemDesc,
							(void *) psRTACtlMemDescInt,
							PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,psRGXCreateHWRTDataOUT->hCleanupCookie);
	if (psRGXCreateHWRTDataOUT->eError != PVRSRV_OK)
	{
		UnlockHandle();
		goto RGXCreateHWRTData_exit;
	}






	psRGXCreateHWRTDataOUT->eError = PVRSRVAllocSubHandleUnlocked(psConnection->psHandleBase,

							&psRGXCreateHWRTDataOUT->hsHWRTDataMemDesc,
							(void *) pssHWRTDataMemDescInt,
							PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,psRGXCreateHWRTDataOUT->hCleanupCookie);
	if (psRGXCreateHWRTDataOUT->eError != PVRSRV_OK)
	{
		UnlockHandle();
		goto RGXCreateHWRTData_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle();



RGXCreateHWRTData_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






	{
		IMG_UINT32 i;

		for (i=0;i<RGXFW_MAX_FREELISTS;i++)
		{
				{
					/* Unreference the previously looked up handle */
						if(psapsFreeListsInt[i])
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hapsFreeListsInt2[i],
											PVRSRV_HANDLE_TYPE_RGX_FREELIST);
						}
				}
		}
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();

	if (psRGXCreateHWRTDataOUT->eError != PVRSRV_OK)
	{
		/* Lock over handle creation cleanup. */
		LockHandle();
		if (psRGXCreateHWRTDataOUT->hCleanupCookie)
		{


			PVRSRV_ERROR eError = PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
						(IMG_HANDLE) psRGXCreateHWRTDataOUT->hCleanupCookie,
						PVRSRV_HANDLE_TYPE_RGX_RTDATA_CLEANUP);
			if ((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY))
			{
				PVR_DPF((PVR_DBG_ERROR,
				        "PVRSRVBridgeRGXCreateHWRTData: %s",
				        PVRSRVGetErrorStringKM(eError)));
			}
			/* Releasing the handle should free/destroy/release the resource.
			 * This should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

			/* Avoid freeing/destroying/releasing the resource a second time below */
			psCleanupCookieInt = NULL;
		}


		/* Release now we have cleaned up creation handles. */
		UnlockHandle();
		if (psCleanupCookieInt)
		{
			RGXDestroyHWRTData(psCleanupCookieInt);
		}
	}

	/* Allocated space should be equal to the last updated offset */
	PVR_ASSERT(ui32BufferSize == ui32NextOffset);

#if defined(INTEGRITY_OS)
	if(pArrayArgsBuffer)
#else
	if(!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXDestroyHWRTData(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXDESTROYHWRTDATA *psRGXDestroyHWRTDataIN,
					  PVRSRV_BRIDGE_OUT_RGXDESTROYHWRTDATA *psRGXDestroyHWRTDataOUT,
					 CONNECTION_DATA *psConnection)
{









	/* Lock over handle destruction. */
	LockHandle();





	psRGXDestroyHWRTDataOUT->eError =
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyHWRTDataIN->hCleanupCookie,
					PVRSRV_HANDLE_TYPE_RGX_RTDATA_CLEANUP);
	if ((psRGXDestroyHWRTDataOUT->eError != PVRSRV_OK) &&
	    (psRGXDestroyHWRTDataOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "PVRSRVBridgeRGXDestroyHWRTData: %s",
		        PVRSRVGetErrorStringKM(psRGXDestroyHWRTDataOUT->eError)));
		PVR_ASSERT(0);
		UnlockHandle();
		goto RGXDestroyHWRTData_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle();



RGXDestroyHWRTData_exit:




	return 0;
}


static IMG_INT
PVRSRVBridgeRGXCreateRenderTarget(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCREATERENDERTARGET *psRGXCreateRenderTargetIN,
					  PVRSRV_BRIDGE_OUT_RGXCREATERENDERTARGET *psRGXCreateRenderTargetOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_RT_CLEANUP_DATA * pssRenderTargetMemDescInt = NULL;








	psRGXCreateRenderTargetOUT->eError =
		RGXCreateRenderTarget(psConnection, OSGetDevData(psConnection),
					psRGXCreateRenderTargetIN->spsVHeapTableDevVAddr,
					&pssRenderTargetMemDescInt,
					&psRGXCreateRenderTargetOUT->ui32sRenderTargetFWDevVAddr);
	/* Exit early if bridged call fails */
	if(psRGXCreateRenderTargetOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateRenderTarget_exit;
	}

	/* Lock over handle creation. */
	LockHandle();





	psRGXCreateRenderTargetOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,

							&psRGXCreateRenderTargetOUT->hsRenderTargetMemDesc,
							(void *) pssRenderTargetMemDescInt,
							PVRSRV_HANDLE_TYPE_RGX_FWIF_RENDERTARGET,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&RGXDestroyRenderTarget);
	if (psRGXCreateRenderTargetOUT->eError != PVRSRV_OK)
	{
		UnlockHandle();
		goto RGXCreateRenderTarget_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle();



RGXCreateRenderTarget_exit:



	if (psRGXCreateRenderTargetOUT->eError != PVRSRV_OK)
	{
		if (pssRenderTargetMemDescInt)
		{
			RGXDestroyRenderTarget(pssRenderTargetMemDescInt);
		}
	}


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXDestroyRenderTarget(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXDESTROYRENDERTARGET *psRGXDestroyRenderTargetIN,
					  PVRSRV_BRIDGE_OUT_RGXDESTROYRENDERTARGET *psRGXDestroyRenderTargetOUT,
					 CONNECTION_DATA *psConnection)
{









	/* Lock over handle destruction. */
	LockHandle();





	psRGXDestroyRenderTargetOUT->eError =
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyRenderTargetIN->hsRenderTargetMemDesc,
					PVRSRV_HANDLE_TYPE_RGX_FWIF_RENDERTARGET);
	if ((psRGXDestroyRenderTargetOUT->eError != PVRSRV_OK) &&
	    (psRGXDestroyRenderTargetOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "PVRSRVBridgeRGXDestroyRenderTarget: %s",
		        PVRSRVGetErrorStringKM(psRGXDestroyRenderTargetOUT->eError)));
		PVR_ASSERT(0);
		UnlockHandle();
		goto RGXDestroyRenderTarget_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle();



RGXDestroyRenderTarget_exit:




	return 0;
}


static IMG_INT
PVRSRVBridgeRGXCreateZSBuffer(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCREATEZSBUFFER *psRGXCreateZSBufferIN,
					  PVRSRV_BRIDGE_OUT_RGXCREATEZSBUFFER *psRGXCreateZSBufferOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hReservation = psRGXCreateZSBufferIN->hReservation;
	DEVMEMINT_RESERVATION * psReservationInt = NULL;
	IMG_HANDLE hPMR = psRGXCreateZSBufferIN->hPMR;
	PMR * psPMRInt = NULL;
	RGX_ZSBUFFER_DATA * pssZSBufferKMInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXCreateZSBufferOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psReservationInt,
											hReservation,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION,
											IMG_TRUE);
					if(psRGXCreateZSBufferOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXCreateZSBuffer_exit;
					}
				}





				{
					/* Look up the address from the handle */
					psRGXCreateZSBufferOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psPMRInt,
											hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
											IMG_TRUE);
					if(psRGXCreateZSBufferOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXCreateZSBuffer_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXCreateZSBufferOUT->eError =
		RGXCreateZSBufferKM(psConnection, OSGetDevData(psConnection),
					psReservationInt,
					psPMRInt,
					psRGXCreateZSBufferIN->uiMapFlags,
					&pssZSBufferKMInt,
					&psRGXCreateZSBufferOUT->ui32sZSBufferFWDevVAddr);
	/* Exit early if bridged call fails */
	if(psRGXCreateZSBufferOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateZSBuffer_exit;
	}

	/* Lock over handle creation. */
	LockHandle();





	psRGXCreateZSBufferOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,

							&psRGXCreateZSBufferOUT->hsZSBufferKM,
							(void *) pssZSBufferKMInt,
							PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&RGXDestroyZSBufferKM);
	if (psRGXCreateZSBufferOUT->eError != PVRSRV_OK)
	{
		UnlockHandle();
		goto RGXCreateZSBuffer_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle();



RGXCreateZSBuffer_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psReservationInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hReservation,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION);
						}
				}





				{
					/* Unreference the previously looked up handle */
						if(psPMRInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();

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
					  PVRSRV_BRIDGE_IN_RGXDESTROYZSBUFFER *psRGXDestroyZSBufferIN,
					  PVRSRV_BRIDGE_OUT_RGXDESTROYZSBUFFER *psRGXDestroyZSBufferOUT,
					 CONNECTION_DATA *psConnection)
{









	/* Lock over handle destruction. */
	LockHandle();





	psRGXDestroyZSBufferOUT->eError =
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyZSBufferIN->hsZSBufferMemDesc,
					PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER);
	if ((psRGXDestroyZSBufferOUT->eError != PVRSRV_OK) &&
	    (psRGXDestroyZSBufferOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "PVRSRVBridgeRGXDestroyZSBuffer: %s",
		        PVRSRVGetErrorStringKM(psRGXDestroyZSBufferOUT->eError)));
		PVR_ASSERT(0);
		UnlockHandle();
		goto RGXDestroyZSBuffer_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle();



RGXDestroyZSBuffer_exit:




	return 0;
}


static IMG_INT
PVRSRVBridgeRGXPopulateZSBuffer(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXPOPULATEZSBUFFER *psRGXPopulateZSBufferIN,
					  PVRSRV_BRIDGE_OUT_RGXPOPULATEZSBUFFER *psRGXPopulateZSBufferOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hsZSBufferKM = psRGXPopulateZSBufferIN->hsZSBufferKM;
	RGX_ZSBUFFER_DATA * pssZSBufferKMInt = NULL;
	RGX_POPULATION * pssPopulationInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXPopulateZSBufferOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &pssZSBufferKMInt,
											hsZSBufferKM,
											PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER,
											IMG_TRUE);
					if(psRGXPopulateZSBufferOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXPopulateZSBuffer_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXPopulateZSBufferOUT->eError =
		RGXPopulateZSBufferKM(
					pssZSBufferKMInt,
					&pssPopulationInt);
	/* Exit early if bridged call fails */
	if(psRGXPopulateZSBufferOUT->eError != PVRSRV_OK)
	{
		goto RGXPopulateZSBuffer_exit;
	}

	/* Lock over handle creation. */
	LockHandle();





	psRGXPopulateZSBufferOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,

							&psRGXPopulateZSBufferOUT->hsPopulation,
							(void *) pssPopulationInt,
							PVRSRV_HANDLE_TYPE_RGX_POPULATION,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&RGXUnpopulateZSBufferKM);
	if (psRGXPopulateZSBufferOUT->eError != PVRSRV_OK)
	{
		UnlockHandle();
		goto RGXPopulateZSBuffer_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle();



RGXPopulateZSBuffer_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(pssZSBufferKMInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hsZSBufferKM,
											PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();

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
					  PVRSRV_BRIDGE_IN_RGXUNPOPULATEZSBUFFER *psRGXUnpopulateZSBufferIN,
					  PVRSRV_BRIDGE_OUT_RGXUNPOPULATEZSBUFFER *psRGXUnpopulateZSBufferOUT,
					 CONNECTION_DATA *psConnection)
{









	/* Lock over handle destruction. */
	LockHandle();





	psRGXUnpopulateZSBufferOUT->eError =
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXUnpopulateZSBufferIN->hsPopulation,
					PVRSRV_HANDLE_TYPE_RGX_POPULATION);
	if ((psRGXUnpopulateZSBufferOUT->eError != PVRSRV_OK) &&
	    (psRGXUnpopulateZSBufferOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "PVRSRVBridgeRGXUnpopulateZSBuffer: %s",
		        PVRSRVGetErrorStringKM(psRGXUnpopulateZSBufferOUT->eError)));
		PVR_ASSERT(0);
		UnlockHandle();
		goto RGXUnpopulateZSBuffer_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle();



RGXUnpopulateZSBuffer_exit:




	return 0;
}


static IMG_INT
PVRSRVBridgeRGXCreateFreeList(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCREATEFREELIST *psRGXCreateFreeListIN,
					  PVRSRV_BRIDGE_OUT_RGXCREATEFREELIST *psRGXCreateFreeListOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hsGlobalFreeList = psRGXCreateFreeListIN->hsGlobalFreeList;
	RGX_FREELIST * pssGlobalFreeListInt = NULL;
	IMG_HANDLE hsFreeListPMR = psRGXCreateFreeListIN->hsFreeListPMR;
	PMR * pssFreeListPMRInt = NULL;
	RGX_FREELIST * psCleanupCookieInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				if (psRGXCreateFreeListIN->hsGlobalFreeList)
				{
					/* Look up the address from the handle */
					psRGXCreateFreeListOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &pssGlobalFreeListInt,
											hsGlobalFreeList,
											PVRSRV_HANDLE_TYPE_RGX_FREELIST,
											IMG_TRUE);
					if(psRGXCreateFreeListOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXCreateFreeList_exit;
					}
				}





				{
					/* Look up the address from the handle */
					psRGXCreateFreeListOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &pssFreeListPMRInt,
											hsFreeListPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
											IMG_TRUE);
					if(psRGXCreateFreeListOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXCreateFreeList_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXCreateFreeListOUT->eError =
		RGXCreateFreeList(psConnection, OSGetDevData(psConnection),
					psRGXCreateFreeListIN->ui32ui32MaxFLPages,
					psRGXCreateFreeListIN->ui32ui32InitFLPages,
					psRGXCreateFreeListIN->ui32ui32GrowFLPages,
					pssGlobalFreeListInt,
					psRGXCreateFreeListIN->bbFreeListCheck,
					psRGXCreateFreeListIN->spsFreeListDevVAddr,
					pssFreeListPMRInt,
					psRGXCreateFreeListIN->uiPMROffset,
					&psCleanupCookieInt);
	/* Exit early if bridged call fails */
	if(psRGXCreateFreeListOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateFreeList_exit;
	}

	/* Lock over handle creation. */
	LockHandle();





	psRGXCreateFreeListOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,

							&psRGXCreateFreeListOUT->hCleanupCookie,
							(void *) psCleanupCookieInt,
							PVRSRV_HANDLE_TYPE_RGX_FREELIST,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&RGXDestroyFreeList);
	if (psRGXCreateFreeListOUT->eError != PVRSRV_OK)
	{
		UnlockHandle();
		goto RGXCreateFreeList_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle();



RGXCreateFreeList_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				if (psRGXCreateFreeListIN->hsGlobalFreeList)
				{
					/* Unreference the previously looked up handle */
						if(pssGlobalFreeListInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hsGlobalFreeList,
											PVRSRV_HANDLE_TYPE_RGX_FREELIST);
						}
				}





				{
					/* Unreference the previously looked up handle */
						if(pssFreeListPMRInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hsFreeListPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();

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
					  PVRSRV_BRIDGE_IN_RGXDESTROYFREELIST *psRGXDestroyFreeListIN,
					  PVRSRV_BRIDGE_OUT_RGXDESTROYFREELIST *psRGXDestroyFreeListOUT,
					 CONNECTION_DATA *psConnection)
{









	/* Lock over handle destruction. */
	LockHandle();





	psRGXDestroyFreeListOUT->eError =
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyFreeListIN->hCleanupCookie,
					PVRSRV_HANDLE_TYPE_RGX_FREELIST);
	if ((psRGXDestroyFreeListOUT->eError != PVRSRV_OK) &&
	    (psRGXDestroyFreeListOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "PVRSRVBridgeRGXDestroyFreeList: %s",
		        PVRSRVGetErrorStringKM(psRGXDestroyFreeListOUT->eError)));
		PVR_ASSERT(0);
		UnlockHandle();
		goto RGXDestroyFreeList_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle();



RGXDestroyFreeList_exit:




	return 0;
}


static IMG_INT
PVRSRVBridgeRGXAddBlockToFreeList(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXADDBLOCKTOFREELIST *psRGXAddBlockToFreeListIN,
					  PVRSRV_BRIDGE_OUT_RGXADDBLOCKTOFREELIST *psRGXAddBlockToFreeListOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hsFreeList = psRGXAddBlockToFreeListIN->hsFreeList;
	RGX_FREELIST * pssFreeListInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXAddBlockToFreeListOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &pssFreeListInt,
											hsFreeList,
											PVRSRV_HANDLE_TYPE_RGX_FREELIST,
											IMG_TRUE);
					if(psRGXAddBlockToFreeListOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXAddBlockToFreeList_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXAddBlockToFreeListOUT->eError =
		RGXAddBlockToFreeListKM(
					pssFreeListInt,
					psRGXAddBlockToFreeListIN->ui3232NumPages);




RGXAddBlockToFreeList_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(pssFreeListInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hsFreeList,
											PVRSRV_HANDLE_TYPE_RGX_FREELIST);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXRemoveBlockFromFreeList(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXREMOVEBLOCKFROMFREELIST *psRGXRemoveBlockFromFreeListIN,
					  PVRSRV_BRIDGE_OUT_RGXREMOVEBLOCKFROMFREELIST *psRGXRemoveBlockFromFreeListOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hsFreeList = psRGXRemoveBlockFromFreeListIN->hsFreeList;
	RGX_FREELIST * pssFreeListInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXRemoveBlockFromFreeListOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &pssFreeListInt,
											hsFreeList,
											PVRSRV_HANDLE_TYPE_RGX_FREELIST,
											IMG_TRUE);
					if(psRGXRemoveBlockFromFreeListOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXRemoveBlockFromFreeList_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXRemoveBlockFromFreeListOUT->eError =
		RGXRemoveBlockFromFreeListKM(
					pssFreeListInt);




RGXRemoveBlockFromFreeList_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(pssFreeListInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hsFreeList,
											PVRSRV_HANDLE_TYPE_RGX_FREELIST);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXCreateRenderContext(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCREATERENDERCONTEXT *psRGXCreateRenderContextIN,
					  PVRSRV_BRIDGE_OUT_RGXCREATERENDERCONTEXT *psRGXCreateRenderContextOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_BYTE *psFrameworkCmdInt = NULL;
	IMG_HANDLE hPrivData = psRGXCreateRenderContextIN->hPrivData;
	IMG_HANDLE hPrivDataInt = NULL;
	RGX_SERVER_RENDER_CONTEXT * psRenderContextInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psRGXCreateRenderContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE)) +
			0;





	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psRGXCreateRenderContextIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psRGXCreateRenderContextIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psRGXCreateRenderContextOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXCreateRenderContext_exit;
			}
		}
	}

	if (psRGXCreateRenderContextIN->ui32FrameworkCmdize != 0)
	{
		psFrameworkCmdInt = (IMG_BYTE*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXCreateRenderContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE);
	}

			/* Copy the data over */
			if (psRGXCreateRenderContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE) > 0)
			{
				if ( OSCopyFromUser(NULL, psFrameworkCmdInt, psRGXCreateRenderContextIN->psFrameworkCmd, psRGXCreateRenderContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE)) != PVRSRV_OK )
				{
					psRGXCreateRenderContextOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXCreateRenderContext_exit;
				}
			}

	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXCreateRenderContextOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &hPrivDataInt,
											hPrivData,
											PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA,
											IMG_TRUE);
					if(psRGXCreateRenderContextOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXCreateRenderContext_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXCreateRenderContextOUT->eError =
		PVRSRVRGXCreateRenderContextKM(psConnection, OSGetDevData(psConnection),
					psRGXCreateRenderContextIN->ui32Priority,
					psRGXCreateRenderContextIN->sMCUFenceAddr,
					psRGXCreateRenderContextIN->sVDMCallStackAddr,
					psRGXCreateRenderContextIN->ui32FrameworkCmdize,
					psFrameworkCmdInt,
					hPrivDataInt,
					&psRenderContextInt);
	/* Exit early if bridged call fails */
	if(psRGXCreateRenderContextOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateRenderContext_exit;
	}

	/* Lock over handle creation. */
	LockHandle();





	psRGXCreateRenderContextOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,

							&psRGXCreateRenderContextOUT->hRenderContext,
							(void *) psRenderContextInt,
							PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&PVRSRVRGXDestroyRenderContextKM);
	if (psRGXCreateRenderContextOUT->eError != PVRSRV_OK)
	{
		UnlockHandle();
		goto RGXCreateRenderContext_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle();



RGXCreateRenderContext_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(hPrivDataInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hPrivData,
											PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();

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
	if(pArrayArgsBuffer)
#else
	if(!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXDestroyRenderContext(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXDESTROYRENDERCONTEXT *psRGXDestroyRenderContextIN,
					  PVRSRV_BRIDGE_OUT_RGXDESTROYRENDERCONTEXT *psRGXDestroyRenderContextOUT,
					 CONNECTION_DATA *psConnection)
{









	/* Lock over handle destruction. */
	LockHandle();





	psRGXDestroyRenderContextOUT->eError =
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyRenderContextIN->hCleanupCookie,
					PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT);
	if ((psRGXDestroyRenderContextOUT->eError != PVRSRV_OK) &&
	    (psRGXDestroyRenderContextOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "PVRSRVBridgeRGXDestroyRenderContext: %s",
		        PVRSRVGetErrorStringKM(psRGXDestroyRenderContextOUT->eError)));
		PVR_ASSERT(0);
		UnlockHandle();
		goto RGXDestroyRenderContext_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle();



RGXDestroyRenderContext_exit:




	return 0;
}


static IMG_INT
PVRSRVBridgeRGXKickTA3D(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXKICKTA3D *psRGXKickTA3DIN,
					  PVRSRV_BRIDGE_OUT_RGXKICKTA3D *psRGXKickTA3DOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hRenderContext = psRGXKickTA3DIN->hRenderContext;
	RGX_SERVER_RENDER_CONTEXT * psRenderContextInt = NULL;
	SYNC_PRIMITIVE_BLOCK * *psClientTAFenceSyncPrimBlockInt = NULL;
	IMG_HANDLE *hClientTAFenceSyncPrimBlockInt2 = NULL;
	IMG_UINT32 *ui32ClientTAFenceSyncOffsetInt = NULL;
	IMG_UINT32 *ui32ClientTAFenceValueInt = NULL;
	SYNC_PRIMITIVE_BLOCK * *psClientTAUpdateSyncPrimBlockInt = NULL;
	IMG_HANDLE *hClientTAUpdateSyncPrimBlockInt2 = NULL;
	IMG_UINT32 *ui32ClientTAUpdateSyncOffsetInt = NULL;
	IMG_UINT32 *ui32ClientTAUpdateValueInt = NULL;
	IMG_UINT32 *ui32ServerTASyncFlagsInt = NULL;
	SERVER_SYNC_PRIMITIVE * *psServerTASyncsInt = NULL;
	IMG_HANDLE *hServerTASyncsInt2 = NULL;
	SYNC_PRIMITIVE_BLOCK * *psClient3DFenceSyncPrimBlockInt = NULL;
	IMG_HANDLE *hClient3DFenceSyncPrimBlockInt2 = NULL;
	IMG_UINT32 *ui32Client3DFenceSyncOffsetInt = NULL;
	IMG_UINT32 *ui32Client3DFenceValueInt = NULL;
	SYNC_PRIMITIVE_BLOCK * *psClient3DUpdateSyncPrimBlockInt = NULL;
	IMG_HANDLE *hClient3DUpdateSyncPrimBlockInt2 = NULL;
	IMG_UINT32 *ui32Client3DUpdateSyncOffsetInt = NULL;
	IMG_UINT32 *ui32Client3DUpdateValueInt = NULL;
	IMG_UINT32 *ui32Server3DSyncFlagsInt = NULL;
	SERVER_SYNC_PRIMITIVE * *psServer3DSyncsInt = NULL;
	IMG_HANDLE *hServer3DSyncsInt2 = NULL;
	IMG_HANDLE hPRFenceUFOSyncPrimBlock = psRGXKickTA3DIN->hPRFenceUFOSyncPrimBlock;
	SYNC_PRIMITIVE_BLOCK * psPRFenceUFOSyncPrimBlockInt = NULL;
	IMG_CHAR *uiUpdateFenceNameInt = NULL;
	IMG_BYTE *psTACmdInt = NULL;
	IMG_BYTE *ps3DPRCmdInt = NULL;
	IMG_BYTE *ps3DCmdInt = NULL;
	IMG_HANDLE hRTDataCleanup = psRGXKickTA3DIN->hRTDataCleanup;
	RGX_RTDATA_CLEANUP_DATA * psRTDataCleanupInt = NULL;
	IMG_HANDLE hZBuffer = psRGXKickTA3DIN->hZBuffer;
	RGX_ZSBUFFER_DATA * psZBufferInt = NULL;
	IMG_HANDLE hSBuffer = psRGXKickTA3DIN->hSBuffer;
	RGX_ZSBUFFER_DATA * psSBufferInt = NULL;
	IMG_UINT32 *ui32SyncPMRFlagsInt = NULL;
	PMR * *psSyncPMRsInt = NULL;
	IMG_HANDLE *hSyncPMRsInt2 = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *)) +
			(psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_HANDLE)) +
			(psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_UINT32)) +
			(psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_UINT32)) +
			(psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *)) +
			(psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_HANDLE)) +
			(psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32)) +
			(psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32)) +
			(psRGXKickTA3DIN->ui32ServerTASyncPrims * sizeof(IMG_UINT32)) +
			(psRGXKickTA3DIN->ui32ServerTASyncPrims * sizeof(SERVER_SYNC_PRIMITIVE *)) +
			(psRGXKickTA3DIN->ui32ServerTASyncPrims * sizeof(IMG_HANDLE)) +
			(psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *)) +
			(psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_HANDLE)) +
			(psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_UINT32)) +
			(psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_UINT32)) +
			(psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *)) +
			(psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_HANDLE)) +
			(psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_UINT32)) +
			(psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_UINT32)) +
			(psRGXKickTA3DIN->ui32Server3DSyncPrims * sizeof(IMG_UINT32)) +
			(psRGXKickTA3DIN->ui32Server3DSyncPrims * sizeof(SERVER_SYNC_PRIMITIVE *)) +
			(psRGXKickTA3DIN->ui32Server3DSyncPrims * sizeof(IMG_HANDLE)) +
			(32 * sizeof(IMG_CHAR)) +
			(psRGXKickTA3DIN->ui32TACmdSize * sizeof(IMG_BYTE)) +
			(psRGXKickTA3DIN->ui323DPRCmdSize * sizeof(IMG_BYTE)) +
			(psRGXKickTA3DIN->ui323DCmdSize * sizeof(IMG_BYTE)) +
			(psRGXKickTA3DIN->ui32SyncPMRCount * sizeof(IMG_UINT32)) +
			(psRGXKickTA3DIN->ui32SyncPMRCount * sizeof(PMR *)) +
			(psRGXKickTA3DIN->ui32SyncPMRCount * sizeof(IMG_HANDLE)) +
			0;





	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psRGXKickTA3DIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psRGXKickTA3DIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXKickTA3D_exit;
			}
		}
	}

	if (psRGXKickTA3DIN->ui32ClientTAFenceCount != 0)
	{
		psClientTAFenceSyncPrimBlockInt = (SYNC_PRIMITIVE_BLOCK **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *);
		hClientTAFenceSyncPrimBlockInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hClientTAFenceSyncPrimBlockInt2, psRGXKickTA3DIN->phClientTAFenceSyncPrimBlock, psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	if (psRGXKickTA3DIN->ui32ClientTAFenceCount != 0)
	{
		ui32ClientTAFenceSyncOffsetInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32ClientTAFenceSyncOffsetInt, psRGXKickTA3DIN->pui32ClientTAFenceSyncOffset, psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	if (psRGXKickTA3DIN->ui32ClientTAFenceCount != 0)
	{
		ui32ClientTAFenceValueInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32ClientTAFenceValueInt, psRGXKickTA3DIN->pui32ClientTAFenceValue, psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	if (psRGXKickTA3DIN->ui32ClientTAUpdateCount != 0)
	{
		psClientTAUpdateSyncPrimBlockInt = (SYNC_PRIMITIVE_BLOCK **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *);
		hClientTAUpdateSyncPrimBlockInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hClientTAUpdateSyncPrimBlockInt2, psRGXKickTA3DIN->phClientTAUpdateSyncPrimBlock, psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	if (psRGXKickTA3DIN->ui32ClientTAUpdateCount != 0)
	{
		ui32ClientTAUpdateSyncOffsetInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32ClientTAUpdateSyncOffsetInt, psRGXKickTA3DIN->pui32ClientTAUpdateSyncOffset, psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	if (psRGXKickTA3DIN->ui32ClientTAUpdateCount != 0)
	{
		ui32ClientTAUpdateValueInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32ClientTAUpdateValueInt, psRGXKickTA3DIN->pui32ClientTAUpdateValue, psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	if (psRGXKickTA3DIN->ui32ServerTASyncPrims != 0)
	{
		ui32ServerTASyncFlagsInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui32ServerTASyncPrims * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui32ServerTASyncPrims * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32ServerTASyncFlagsInt, psRGXKickTA3DIN->pui32ServerTASyncFlags, psRGXKickTA3DIN->ui32ServerTASyncPrims * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	if (psRGXKickTA3DIN->ui32ServerTASyncPrims != 0)
	{
		psServerTASyncsInt = (SERVER_SYNC_PRIMITIVE **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui32ServerTASyncPrims * sizeof(SERVER_SYNC_PRIMITIVE *);
		hServerTASyncsInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psRGXKickTA3DIN->ui32ServerTASyncPrims * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui32ServerTASyncPrims * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hServerTASyncsInt2, psRGXKickTA3DIN->phServerTASyncs, psRGXKickTA3DIN->ui32ServerTASyncPrims * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	if (psRGXKickTA3DIN->ui32Client3DFenceCount != 0)
	{
		psClient3DFenceSyncPrimBlockInt = (SYNC_PRIMITIVE_BLOCK **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *);
		hClient3DFenceSyncPrimBlockInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hClient3DFenceSyncPrimBlockInt2, psRGXKickTA3DIN->phClient3DFenceSyncPrimBlock, psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	if (psRGXKickTA3DIN->ui32Client3DFenceCount != 0)
	{
		ui32Client3DFenceSyncOffsetInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32Client3DFenceSyncOffsetInt, psRGXKickTA3DIN->pui32Client3DFenceSyncOffset, psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	if (psRGXKickTA3DIN->ui32Client3DFenceCount != 0)
	{
		ui32Client3DFenceValueInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32Client3DFenceValueInt, psRGXKickTA3DIN->pui32Client3DFenceValue, psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	if (psRGXKickTA3DIN->ui32Client3DUpdateCount != 0)
	{
		psClient3DUpdateSyncPrimBlockInt = (SYNC_PRIMITIVE_BLOCK **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *);
		hClient3DUpdateSyncPrimBlockInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hClient3DUpdateSyncPrimBlockInt2, psRGXKickTA3DIN->phClient3DUpdateSyncPrimBlock, psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	if (psRGXKickTA3DIN->ui32Client3DUpdateCount != 0)
	{
		ui32Client3DUpdateSyncOffsetInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32Client3DUpdateSyncOffsetInt, psRGXKickTA3DIN->pui32Client3DUpdateSyncOffset, psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	if (psRGXKickTA3DIN->ui32Client3DUpdateCount != 0)
	{
		ui32Client3DUpdateValueInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32Client3DUpdateValueInt, psRGXKickTA3DIN->pui32Client3DUpdateValue, psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	if (psRGXKickTA3DIN->ui32Server3DSyncPrims != 0)
	{
		ui32Server3DSyncFlagsInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui32Server3DSyncPrims * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui32Server3DSyncPrims * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32Server3DSyncFlagsInt, psRGXKickTA3DIN->pui32Server3DSyncFlags, psRGXKickTA3DIN->ui32Server3DSyncPrims * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	if (psRGXKickTA3DIN->ui32Server3DSyncPrims != 0)
	{
		psServer3DSyncsInt = (SERVER_SYNC_PRIMITIVE **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui32Server3DSyncPrims * sizeof(SERVER_SYNC_PRIMITIVE *);
		hServer3DSyncsInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psRGXKickTA3DIN->ui32Server3DSyncPrims * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui32Server3DSyncPrims * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hServer3DSyncsInt2, psRGXKickTA3DIN->phServer3DSyncs, psRGXKickTA3DIN->ui32Server3DSyncPrims * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	
	{
		uiUpdateFenceNameInt = (IMG_CHAR*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += 32 * sizeof(IMG_CHAR);
	}

			/* Copy the data over */
			if (32 * sizeof(IMG_CHAR) > 0)
			{
				if ( OSCopyFromUser(NULL, uiUpdateFenceNameInt, psRGXKickTA3DIN->puiUpdateFenceName, 32 * sizeof(IMG_CHAR)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	if (psRGXKickTA3DIN->ui32TACmdSize != 0)
	{
		psTACmdInt = (IMG_BYTE*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui32TACmdSize * sizeof(IMG_BYTE);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui32TACmdSize * sizeof(IMG_BYTE) > 0)
			{
				if ( OSCopyFromUser(NULL, psTACmdInt, psRGXKickTA3DIN->psTACmd, psRGXKickTA3DIN->ui32TACmdSize * sizeof(IMG_BYTE)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	if (psRGXKickTA3DIN->ui323DPRCmdSize != 0)
	{
		ps3DPRCmdInt = (IMG_BYTE*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui323DPRCmdSize * sizeof(IMG_BYTE);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui323DPRCmdSize * sizeof(IMG_BYTE) > 0)
			{
				if ( OSCopyFromUser(NULL, ps3DPRCmdInt, psRGXKickTA3DIN->ps3DPRCmd, psRGXKickTA3DIN->ui323DPRCmdSize * sizeof(IMG_BYTE)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	if (psRGXKickTA3DIN->ui323DCmdSize != 0)
	{
		ps3DCmdInt = (IMG_BYTE*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui323DCmdSize * sizeof(IMG_BYTE);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui323DCmdSize * sizeof(IMG_BYTE) > 0)
			{
				if ( OSCopyFromUser(NULL, ps3DCmdInt, psRGXKickTA3DIN->ps3DCmd, psRGXKickTA3DIN->ui323DCmdSize * sizeof(IMG_BYTE)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	if (psRGXKickTA3DIN->ui32SyncPMRCount != 0)
	{
		ui32SyncPMRFlagsInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui32SyncPMRCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui32SyncPMRCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32SyncPMRFlagsInt, psRGXKickTA3DIN->pui32SyncPMRFlags, psRGXKickTA3DIN->ui32SyncPMRCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}
	if (psRGXKickTA3DIN->ui32SyncPMRCount != 0)
	{
		psSyncPMRsInt = (PMR **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickTA3DIN->ui32SyncPMRCount * sizeof(PMR *);
		hSyncPMRsInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psRGXKickTA3DIN->ui32SyncPMRCount * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psRGXKickTA3DIN->ui32SyncPMRCount * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hSyncPMRsInt2, psRGXKickTA3DIN->phSyncPMRs, psRGXKickTA3DIN->ui32SyncPMRCount * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickTA3D_exit;
				}
			}

	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXKickTA3DOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psRenderContextInt,
											hRenderContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT,
											IMG_TRUE);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickTA3D_exit;
					}
				}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickTA3DIN->ui32ClientTAFenceCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickTA3DOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psClientTAFenceSyncPrimBlockInt[i],
											hClientTAFenceSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
											IMG_TRUE);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickTA3D_exit;
					}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickTA3DIN->ui32ClientTAUpdateCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickTA3DOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psClientTAUpdateSyncPrimBlockInt[i],
											hClientTAUpdateSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
											IMG_TRUE);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickTA3D_exit;
					}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickTA3DIN->ui32ServerTASyncPrims;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickTA3DOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psServerTASyncsInt[i],
											hServerTASyncsInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE,
											IMG_TRUE);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickTA3D_exit;
					}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickTA3DIN->ui32Client3DFenceCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickTA3DOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psClient3DFenceSyncPrimBlockInt[i],
											hClient3DFenceSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
											IMG_TRUE);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickTA3D_exit;
					}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickTA3DIN->ui32Client3DUpdateCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickTA3DOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psClient3DUpdateSyncPrimBlockInt[i],
											hClient3DUpdateSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
											IMG_TRUE);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickTA3D_exit;
					}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickTA3DIN->ui32Server3DSyncPrims;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickTA3DOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psServer3DSyncsInt[i],
											hServer3DSyncsInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE,
											IMG_TRUE);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickTA3D_exit;
					}
				}
		}
	}





				{
					/* Look up the address from the handle */
					psRGXKickTA3DOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psPRFenceUFOSyncPrimBlockInt,
											hPRFenceUFOSyncPrimBlock,
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
											IMG_TRUE);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickTA3D_exit;
					}
				}





				if (psRGXKickTA3DIN->hRTDataCleanup)
				{
					/* Look up the address from the handle */
					psRGXKickTA3DOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psRTDataCleanupInt,
											hRTDataCleanup,
											PVRSRV_HANDLE_TYPE_RGX_RTDATA_CLEANUP,
											IMG_TRUE);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickTA3D_exit;
					}
				}





				if (psRGXKickTA3DIN->hZBuffer)
				{
					/* Look up the address from the handle */
					psRGXKickTA3DOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psZBufferInt,
											hZBuffer,
											PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER,
											IMG_TRUE);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickTA3D_exit;
					}
				}





				if (psRGXKickTA3DIN->hSBuffer)
				{
					/* Look up the address from the handle */
					psRGXKickTA3DOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psSBufferInt,
											hSBuffer,
											PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER,
											IMG_TRUE);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickTA3D_exit;
					}
				}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickTA3DIN->ui32SyncPMRCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickTA3DOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psSyncPMRsInt[i],
											hSyncPMRsInt2[i],
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
											IMG_TRUE);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickTA3D_exit;
					}
				}
		}
	}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXKickTA3DOUT->eError =
		PVRSRVRGXKickTA3DKM(
					psRenderContextInt,
					psRGXKickTA3DIN->ui32ClientCacheOpSeqNum,
					psRGXKickTA3DIN->ui32ClientTAFenceCount,
					psClientTAFenceSyncPrimBlockInt,
					ui32ClientTAFenceSyncOffsetInt,
					ui32ClientTAFenceValueInt,
					psRGXKickTA3DIN->ui32ClientTAUpdateCount,
					psClientTAUpdateSyncPrimBlockInt,
					ui32ClientTAUpdateSyncOffsetInt,
					ui32ClientTAUpdateValueInt,
					psRGXKickTA3DIN->ui32ServerTASyncPrims,
					ui32ServerTASyncFlagsInt,
					psServerTASyncsInt,
					psRGXKickTA3DIN->ui32Client3DFenceCount,
					psClient3DFenceSyncPrimBlockInt,
					ui32Client3DFenceSyncOffsetInt,
					ui32Client3DFenceValueInt,
					psRGXKickTA3DIN->ui32Client3DUpdateCount,
					psClient3DUpdateSyncPrimBlockInt,
					ui32Client3DUpdateSyncOffsetInt,
					ui32Client3DUpdateValueInt,
					psRGXKickTA3DIN->ui32Server3DSyncPrims,
					ui32Server3DSyncFlagsInt,
					psServer3DSyncsInt,
					psPRFenceUFOSyncPrimBlockInt,
					psRGXKickTA3DIN->ui32FRFenceUFOSyncOffset,
					psRGXKickTA3DIN->ui32FRFenceValue,
					psRGXKickTA3DIN->i32CheckFenceFD,
					psRGXKickTA3DIN->i32UpdateTimelineFD,
					&psRGXKickTA3DOUT->i32UpdateFenceFD,
					uiUpdateFenceNameInt,
					psRGXKickTA3DIN->ui32TACmdSize,
					psTACmdInt,
					psRGXKickTA3DIN->ui323DPRCmdSize,
					ps3DPRCmdInt,
					psRGXKickTA3DIN->ui323DCmdSize,
					ps3DCmdInt,
					psRGXKickTA3DIN->ui32ExtJobRef,
					psRGXKickTA3DIN->bbLastTAInScene,
					psRGXKickTA3DIN->bbKickTA,
					psRGXKickTA3DIN->bbKickPR,
					psRGXKickTA3DIN->bbKick3D,
					psRGXKickTA3DIN->bbAbort,
					psRGXKickTA3DIN->ui32PDumpFlags,
					psRTDataCleanupInt,
					psZBufferInt,
					psSBufferInt,
					psRGXKickTA3DIN->bbCommitRefCountsTA,
					psRGXKickTA3DIN->bbCommitRefCounts3D,
					&psRGXKickTA3DOUT->bbCommittedRefCountsTA,
					&psRGXKickTA3DOUT->bbCommittedRefCounts3D,
					psRGXKickTA3DIN->ui32SyncPMRCount,
					ui32SyncPMRFlagsInt,
					psSyncPMRsInt,
					psRGXKickTA3DIN->ui32RenderTargetSize,
					psRGXKickTA3DIN->ui32NumberOfDrawCalls,
					psRGXKickTA3DIN->ui32NumberOfIndices,
					psRGXKickTA3DIN->ui32NumberOfMRTs,
					psRGXKickTA3DIN->ui64Deadline);




RGXKickTA3D_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psRenderContextInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hRenderContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT);
						}
				}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickTA3DIN->ui32ClientTAFenceCount;i++)
		{
				{
					/* Unreference the previously looked up handle */
						if(psClientTAFenceSyncPrimBlockInt[i])
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hClientTAFenceSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
						}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickTA3DIN->ui32ClientTAUpdateCount;i++)
		{
				{
					/* Unreference the previously looked up handle */
						if(psClientTAUpdateSyncPrimBlockInt[i])
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hClientTAUpdateSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
						}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickTA3DIN->ui32ServerTASyncPrims;i++)
		{
				{
					/* Unreference the previously looked up handle */
						if(psServerTASyncsInt[i])
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hServerTASyncsInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
						}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickTA3DIN->ui32Client3DFenceCount;i++)
		{
				{
					/* Unreference the previously looked up handle */
						if(psClient3DFenceSyncPrimBlockInt[i])
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hClient3DFenceSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
						}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickTA3DIN->ui32Client3DUpdateCount;i++)
		{
				{
					/* Unreference the previously looked up handle */
						if(psClient3DUpdateSyncPrimBlockInt[i])
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hClient3DUpdateSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
						}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickTA3DIN->ui32Server3DSyncPrims;i++)
		{
				{
					/* Unreference the previously looked up handle */
						if(psServer3DSyncsInt[i])
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hServer3DSyncsInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
						}
				}
		}
	}





				{
					/* Unreference the previously looked up handle */
						if(psPRFenceUFOSyncPrimBlockInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hPRFenceUFOSyncPrimBlock,
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
						}
				}





				if (psRGXKickTA3DIN->hRTDataCleanup)
				{
					/* Unreference the previously looked up handle */
						if(psRTDataCleanupInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hRTDataCleanup,
											PVRSRV_HANDLE_TYPE_RGX_RTDATA_CLEANUP);
						}
				}





				if (psRGXKickTA3DIN->hZBuffer)
				{
					/* Unreference the previously looked up handle */
						if(psZBufferInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hZBuffer,
											PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER);
						}
				}





				if (psRGXKickTA3DIN->hSBuffer)
				{
					/* Unreference the previously looked up handle */
						if(psSBufferInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hSBuffer,
											PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER);
						}
				}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickTA3DIN->ui32SyncPMRCount;i++)
		{
				{
					/* Unreference the previously looked up handle */
						if(psSyncPMRsInt[i])
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hSyncPMRsInt2[i],
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
						}
				}
		}
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();

	/* Allocated space should be equal to the last updated offset */
	PVR_ASSERT(ui32BufferSize == ui32NextOffset);

#if defined(INTEGRITY_OS)
	if(pArrayArgsBuffer)
#else
	if(!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXSetRenderContextPriority(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXSETRENDERCONTEXTPRIORITY *psRGXSetRenderContextPriorityIN,
					  PVRSRV_BRIDGE_OUT_RGXSETRENDERCONTEXTPRIORITY *psRGXSetRenderContextPriorityOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hRenderContext = psRGXSetRenderContextPriorityIN->hRenderContext;
	RGX_SERVER_RENDER_CONTEXT * psRenderContextInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXSetRenderContextPriorityOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psRenderContextInt,
											hRenderContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT,
											IMG_TRUE);
					if(psRGXSetRenderContextPriorityOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXSetRenderContextPriority_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXSetRenderContextPriorityOUT->eError =
		PVRSRVRGXSetRenderContextPriorityKM(psConnection, OSGetDevData(psConnection),
					psRenderContextInt,
					psRGXSetRenderContextPriorityIN->ui32Priority);




RGXSetRenderContextPriority_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psRenderContextInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hRenderContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXGetLastRenderContextResetReason(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXGETLASTRENDERCONTEXTRESETREASON *psRGXGetLastRenderContextResetReasonIN,
					  PVRSRV_BRIDGE_OUT_RGXGETLASTRENDERCONTEXTRESETREASON *psRGXGetLastRenderContextResetReasonOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hRenderContext = psRGXGetLastRenderContextResetReasonIN->hRenderContext;
	RGX_SERVER_RENDER_CONTEXT * psRenderContextInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXGetLastRenderContextResetReasonOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psRenderContextInt,
											hRenderContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT,
											IMG_TRUE);
					if(psRGXGetLastRenderContextResetReasonOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXGetLastRenderContextResetReason_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXGetLastRenderContextResetReasonOUT->eError =
		PVRSRVRGXGetLastRenderContextResetReasonKM(
					psRenderContextInt,
					&psRGXGetLastRenderContextResetReasonOUT->ui32LastResetReason,
					&psRGXGetLastRenderContextResetReasonOUT->ui32LastResetJobRef);




RGXGetLastRenderContextResetReason_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psRenderContextInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hRenderContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXGetPartialRenderCount(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXGETPARTIALRENDERCOUNT *psRGXGetPartialRenderCountIN,
					  PVRSRV_BRIDGE_OUT_RGXGETPARTIALRENDERCOUNT *psRGXGetPartialRenderCountOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hHWRTDataMemDesc = psRGXGetPartialRenderCountIN->hHWRTDataMemDesc;
	DEVMEM_MEMDESC * psHWRTDataMemDescInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXGetPartialRenderCountOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psHWRTDataMemDescInt,
											hHWRTDataMemDesc,
											PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC,
											IMG_TRUE);
					if(psRGXGetPartialRenderCountOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXGetPartialRenderCount_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXGetPartialRenderCountOUT->eError =
		PVRSRVRGXGetPartialRenderCountKM(
					psHWRTDataMemDescInt,
					&psRGXGetPartialRenderCountOUT->ui32NumPartialRenders);




RGXGetPartialRenderCount_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psHWRTDataMemDescInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hHWRTDataMemDesc,
											PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();


	return 0;
}




/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */

static IMG_BOOL bUseLock = IMG_TRUE;

PVRSRV_ERROR InitRGXTA3DBridge(void);
PVRSRV_ERROR DeinitRGXTA3DBridge(void);

/*
 * Register all RGXTA3D functions with services
 */
PVRSRV_ERROR InitRGXTA3DBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXCREATEHWRTDATA, PVRSRVBridgeRGXCreateHWRTData,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYHWRTDATA, PVRSRVBridgeRGXDestroyHWRTData,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXCREATERENDERTARGET, PVRSRVBridgeRGXCreateRenderTarget,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYRENDERTARGET, PVRSRVBridgeRGXDestroyRenderTarget,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXCREATEZSBUFFER, PVRSRVBridgeRGXCreateZSBuffer,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYZSBUFFER, PVRSRVBridgeRGXDestroyZSBuffer,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXPOPULATEZSBUFFER, PVRSRVBridgeRGXPopulateZSBuffer,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXUNPOPULATEZSBUFFER, PVRSRVBridgeRGXUnpopulateZSBuffer,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXCREATEFREELIST, PVRSRVBridgeRGXCreateFreeList,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYFREELIST, PVRSRVBridgeRGXDestroyFreeList,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXADDBLOCKTOFREELIST, PVRSRVBridgeRGXAddBlockToFreeList,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXREMOVEBLOCKFROMFREELIST, PVRSRVBridgeRGXRemoveBlockFromFreeList,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXCREATERENDERCONTEXT, PVRSRVBridgeRGXCreateRenderContext,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYRENDERCONTEXT, PVRSRVBridgeRGXDestroyRenderContext,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXKICKTA3D, PVRSRVBridgeRGXKickTA3D,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXSETRENDERCONTEXTPRIORITY, PVRSRVBridgeRGXSetRenderContextPriority,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXGETLASTRENDERCONTEXTRESETREASON, PVRSRVBridgeRGXGetLastRenderContextResetReason,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXGETPARTIALRENDERCOUNT, PVRSRVBridgeRGXGetPartialRenderCount,
					NULL, bUseLock);


	return PVRSRV_OK;
}

/*
 * Unregister all rgxta3d functions with services
 */
PVRSRV_ERROR DeinitRGXTA3DBridge(void)
{
	return PVRSRV_OK;
}
