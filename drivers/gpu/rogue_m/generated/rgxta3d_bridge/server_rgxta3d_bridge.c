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
#include <asm/uaccess.h>

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

#if defined (SUPPORT_AUTH)
#include "osauth.h"
#endif

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
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	RGX_FREELIST * *psapsFreeListsInt = IMG_NULL;
	IMG_HANDLE *hapsFreeListsInt2 = IMG_NULL;
	RGX_RTDATA_CLEANUP_DATA * psCleanupCookieInt = IMG_NULL;
	DEVMEM_MEMDESC * psRTACtlMemDescInt = IMG_NULL;
	DEVMEM_MEMDESC * pssHWRTDataMemDescInt = IMG_NULL;



	psRGXCreateHWRTDataOUT->hCleanupCookie = IMG_NULL;

	
	{
		psapsFreeListsInt = OSAllocMem(RGXFW_MAX_FREELISTS * sizeof(RGX_FREELIST *));
		if (!psapsFreeListsInt)
		{
			psRGXCreateHWRTDataOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXCreateHWRTData_exit;
		}
		hapsFreeListsInt2 = OSAllocMem(RGXFW_MAX_FREELISTS * sizeof(IMG_HANDLE));
		if (!hapsFreeListsInt2)
		{
			psRGXCreateHWRTDataOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXCreateHWRTData_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXCreateHWRTDataIN->phapsFreeLists, RGXFW_MAX_FREELISTS * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hapsFreeListsInt2, psRGXCreateHWRTDataIN->phapsFreeLists,
				RGXFW_MAX_FREELISTS * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXCreateHWRTDataOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXCreateHWRTData_exit;
			}



				{
					/* Look up the address from the handle */
					psRGXCreateHWRTDataOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &hDevNodeInt,
											psRGXCreateHWRTDataIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXCreateHWRTDataOUT->eError != PVRSRV_OK)
					{
						goto RGXCreateHWRTData_exit;
					}
				}


	{
		IMG_UINT32 i;

		for (i=0;i<RGXFW_MAX_FREELISTS;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXCreateHWRTDataOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psapsFreeListsInt[i],
											hapsFreeListsInt2[i],
											PVRSRV_HANDLE_TYPE_RGX_FREELIST);
					if(psRGXCreateHWRTDataOUT->eError != PVRSRV_OK)
					{
						goto RGXCreateHWRTData_exit;
					}
				}

		}
	}

	psRGXCreateHWRTDataOUT->eError =
		RGXCreateHWRTData(
					hDevNodeInt,
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


	psRGXCreateHWRTDataOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psRGXCreateHWRTDataOUT->hCleanupCookie,
							(IMG_VOID *) psCleanupCookieInt,
							PVRSRV_HANDLE_TYPE_RGX_RTDATA_CLEANUP,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&RGXDestroyHWRTData);
	if (psRGXCreateHWRTDataOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateHWRTData_exit;
	}


	psRGXCreateHWRTDataOUT->eError = PVRSRVAllocSubHandle(psConnection->psHandleBase,
							&psRGXCreateHWRTDataOUT->hRTACtlMemDesc,
							(IMG_VOID *) psRTACtlMemDescInt,
							PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,psRGXCreateHWRTDataOUT->hCleanupCookie);
	if (psRGXCreateHWRTDataOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateHWRTData_exit;
	}


	psRGXCreateHWRTDataOUT->eError = PVRSRVAllocSubHandle(psConnection->psHandleBase,
							&psRGXCreateHWRTDataOUT->hsHWRTDataMemDesc,
							(IMG_VOID *) pssHWRTDataMemDescInt,
							PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,psRGXCreateHWRTDataOUT->hCleanupCookie);
	if (psRGXCreateHWRTDataOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateHWRTData_exit;
	}




RGXCreateHWRTData_exit:
	if (psRGXCreateHWRTDataOUT->eError != PVRSRV_OK)
	{
		if (psRGXCreateHWRTDataOUT->hCleanupCookie)
		{
			PVRSRV_ERROR eError = PVRSRVReleaseHandle(psConnection->psHandleBase,
						(IMG_HANDLE) psRGXCreateHWRTDataOUT->hCleanupCookie,
						PVRSRV_HANDLE_TYPE_RGX_RTDATA_CLEANUP);

			/* Releasing the handle should free/destroy/release the resource. This should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

			/* Avoid freeing/destroying/releasing the resource a second time below */
			psCleanupCookieInt = IMG_NULL;
		}


		if (psCleanupCookieInt)
		{
			RGXDestroyHWRTData(psCleanupCookieInt);
		}
	}

	if (psapsFreeListsInt)
		OSFreeMem(psapsFreeListsInt);
	if (hapsFreeListsInt2)
		OSFreeMem(hapsFreeListsInt2);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXDestroyHWRTData(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXDESTROYHWRTDATA *psRGXDestroyHWRTDataIN,
					  PVRSRV_BRIDGE_OUT_RGXDESTROYHWRTDATA *psRGXDestroyHWRTDataOUT,
					 CONNECTION_DATA *psConnection)
{









	psRGXDestroyHWRTDataOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyHWRTDataIN->hCleanupCookie,
					PVRSRV_HANDLE_TYPE_RGX_RTDATA_CLEANUP);
	if ((psRGXDestroyHWRTDataOUT->eError != PVRSRV_OK) && (psRGXDestroyHWRTDataOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		goto RGXDestroyHWRTData_exit;
	}



RGXDestroyHWRTData_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXCreateRenderTarget(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCREATERENDERTARGET *psRGXCreateRenderTargetIN,
					  PVRSRV_BRIDGE_OUT_RGXCREATERENDERTARGET *psRGXCreateRenderTargetOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	RGX_RT_CLEANUP_DATA * pssRenderTargetMemDescInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psRGXCreateRenderTargetOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &hDevNodeInt,
											psRGXCreateRenderTargetIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXCreateRenderTargetOUT->eError != PVRSRV_OK)
					{
						goto RGXCreateRenderTarget_exit;
					}
				}


	psRGXCreateRenderTargetOUT->eError =
		RGXCreateRenderTarget(
					hDevNodeInt,
					psRGXCreateRenderTargetIN->spsVHeapTableDevVAddr,
					&pssRenderTargetMemDescInt,
					&psRGXCreateRenderTargetOUT->ui32sRenderTargetFWDevVAddr);
	/* Exit early if bridged call fails */
	if(psRGXCreateRenderTargetOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateRenderTarget_exit;
	}


	psRGXCreateRenderTargetOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psRGXCreateRenderTargetOUT->hsRenderTargetMemDesc,
							(IMG_VOID *) pssRenderTargetMemDescInt,
							PVRSRV_HANDLE_TYPE_RGX_FWIF_RENDERTARGET,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&RGXDestroyRenderTarget);
	if (psRGXCreateRenderTargetOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateRenderTarget_exit;
	}




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









	psRGXDestroyRenderTargetOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyRenderTargetIN->hsRenderTargetMemDesc,
					PVRSRV_HANDLE_TYPE_RGX_FWIF_RENDERTARGET);
	if ((psRGXDestroyRenderTargetOUT->eError != PVRSRV_OK) && (psRGXDestroyRenderTargetOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		goto RGXDestroyRenderTarget_exit;
	}



RGXDestroyRenderTarget_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXCreateZSBuffer(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCREATEZSBUFFER *psRGXCreateZSBufferIN,
					  PVRSRV_BRIDGE_OUT_RGXCREATEZSBUFFER *psRGXCreateZSBufferOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	DEVMEMINT_RESERVATION * psReservationInt = IMG_NULL;
	PMR * psPMRInt = IMG_NULL;
	RGX_ZSBUFFER_DATA * pssZSBufferKMInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psRGXCreateZSBufferOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &hDevNodeInt,
											psRGXCreateZSBufferIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXCreateZSBufferOUT->eError != PVRSRV_OK)
					{
						goto RGXCreateZSBuffer_exit;
					}
				}


				{
					/* Look up the address from the handle */
					psRGXCreateZSBufferOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psReservationInt,
											psRGXCreateZSBufferIN->hReservation,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION);
					if(psRGXCreateZSBufferOUT->eError != PVRSRV_OK)
					{
						goto RGXCreateZSBuffer_exit;
					}
				}


				{
					/* Look up the address from the handle */
					psRGXCreateZSBufferOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psPMRInt,
											psRGXCreateZSBufferIN->hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psRGXCreateZSBufferOUT->eError != PVRSRV_OK)
					{
						goto RGXCreateZSBuffer_exit;
					}
				}


	psRGXCreateZSBufferOUT->eError =
		RGXCreateZSBufferKM(
					hDevNodeInt,
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


	psRGXCreateZSBufferOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psRGXCreateZSBufferOUT->hsZSBufferKM,
							(IMG_VOID *) pssZSBufferKMInt,
							PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&RGXDestroyZSBufferKM);
	if (psRGXCreateZSBufferOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateZSBuffer_exit;
	}




RGXCreateZSBuffer_exit:
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









	psRGXDestroyZSBufferOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyZSBufferIN->hsZSBufferMemDesc,
					PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER);
	if ((psRGXDestroyZSBufferOUT->eError != PVRSRV_OK) && (psRGXDestroyZSBufferOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		goto RGXDestroyZSBuffer_exit;
	}



RGXDestroyZSBuffer_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXPopulateZSBuffer(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXPOPULATEZSBUFFER *psRGXPopulateZSBufferIN,
					  PVRSRV_BRIDGE_OUT_RGXPOPULATEZSBUFFER *psRGXPopulateZSBufferOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_ZSBUFFER_DATA * pssZSBufferKMInt = IMG_NULL;
	RGX_POPULATION * pssPopulationInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psRGXPopulateZSBufferOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &pssZSBufferKMInt,
											psRGXPopulateZSBufferIN->hsZSBufferKM,
											PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER);
					if(psRGXPopulateZSBufferOUT->eError != PVRSRV_OK)
					{
						goto RGXPopulateZSBuffer_exit;
					}
				}


	psRGXPopulateZSBufferOUT->eError =
		RGXPopulateZSBufferKM(
					pssZSBufferKMInt,
					&pssPopulationInt);
	/* Exit early if bridged call fails */
	if(psRGXPopulateZSBufferOUT->eError != PVRSRV_OK)
	{
		goto RGXPopulateZSBuffer_exit;
	}


	psRGXPopulateZSBufferOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psRGXPopulateZSBufferOUT->hsPopulation,
							(IMG_VOID *) pssPopulationInt,
							PVRSRV_HANDLE_TYPE_RGX_POPULATION,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&RGXUnpopulateZSBufferKM);
	if (psRGXPopulateZSBufferOUT->eError != PVRSRV_OK)
	{
		goto RGXPopulateZSBuffer_exit;
	}




RGXPopulateZSBuffer_exit:
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









	psRGXUnpopulateZSBufferOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXUnpopulateZSBufferIN->hsPopulation,
					PVRSRV_HANDLE_TYPE_RGX_POPULATION);
	if ((psRGXUnpopulateZSBufferOUT->eError != PVRSRV_OK) && (psRGXUnpopulateZSBufferOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		goto RGXUnpopulateZSBuffer_exit;
	}



RGXUnpopulateZSBuffer_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXCreateFreeList(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCREATEFREELIST *psRGXCreateFreeListIN,
					  PVRSRV_BRIDGE_OUT_RGXCREATEFREELIST *psRGXCreateFreeListOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	PMR * pssFreeListPMRInt = IMG_NULL;
	RGX_FREELIST * psCleanupCookieInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psRGXCreateFreeListOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &hDevNodeInt,
											psRGXCreateFreeListIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXCreateFreeListOUT->eError != PVRSRV_OK)
					{
						goto RGXCreateFreeList_exit;
					}
				}


				{
					/* Look up the address from the handle */
					psRGXCreateFreeListOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &pssFreeListPMRInt,
											psRGXCreateFreeListIN->hsFreeListPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psRGXCreateFreeListOUT->eError != PVRSRV_OK)
					{
						goto RGXCreateFreeList_exit;
					}
				}


	psRGXCreateFreeListOUT->eError =
		RGXCreateFreeList(
					hDevNodeInt,
					psRGXCreateFreeListIN->ui32ui32MaxFLPages,
					psRGXCreateFreeListIN->ui32ui32InitFLPages,
					psRGXCreateFreeListIN->ui32ui32GrowFLPages,
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


	psRGXCreateFreeListOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psRGXCreateFreeListOUT->hCleanupCookie,
							(IMG_VOID *) psCleanupCookieInt,
							PVRSRV_HANDLE_TYPE_RGX_FREELIST,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&RGXDestroyFreeList);
	if (psRGXCreateFreeListOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateFreeList_exit;
	}




RGXCreateFreeList_exit:
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









	psRGXDestroyFreeListOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyFreeListIN->hCleanupCookie,
					PVRSRV_HANDLE_TYPE_RGX_FREELIST);
	if ((psRGXDestroyFreeListOUT->eError != PVRSRV_OK) && (psRGXDestroyFreeListOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		goto RGXDestroyFreeList_exit;
	}



RGXDestroyFreeList_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXAddBlockToFreeList(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXADDBLOCKTOFREELIST *psRGXAddBlockToFreeListIN,
					  PVRSRV_BRIDGE_OUT_RGXADDBLOCKTOFREELIST *psRGXAddBlockToFreeListOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_FREELIST * pssFreeListInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psRGXAddBlockToFreeListOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &pssFreeListInt,
											psRGXAddBlockToFreeListIN->hsFreeList,
											PVRSRV_HANDLE_TYPE_RGX_FREELIST);
					if(psRGXAddBlockToFreeListOUT->eError != PVRSRV_OK)
					{
						goto RGXAddBlockToFreeList_exit;
					}
				}


	psRGXAddBlockToFreeListOUT->eError =
		RGXAddBlockToFreeListKM(
					pssFreeListInt,
					psRGXAddBlockToFreeListIN->ui3232NumPages);




RGXAddBlockToFreeList_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXRemoveBlockFromFreeList(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXREMOVEBLOCKFROMFREELIST *psRGXRemoveBlockFromFreeListIN,
					  PVRSRV_BRIDGE_OUT_RGXREMOVEBLOCKFROMFREELIST *psRGXRemoveBlockFromFreeListOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_FREELIST * pssFreeListInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psRGXRemoveBlockFromFreeListOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &pssFreeListInt,
											psRGXRemoveBlockFromFreeListIN->hsFreeList,
											PVRSRV_HANDLE_TYPE_RGX_FREELIST);
					if(psRGXRemoveBlockFromFreeListOUT->eError != PVRSRV_OK)
					{
						goto RGXRemoveBlockFromFreeList_exit;
					}
				}


	psRGXRemoveBlockFromFreeListOUT->eError =
		RGXRemoveBlockFromFreeListKM(
					pssFreeListInt);




RGXRemoveBlockFromFreeList_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXCreateRenderContext(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCREATERENDERCONTEXT *psRGXCreateRenderContextIN,
					  PVRSRV_BRIDGE_OUT_RGXCREATERENDERCONTEXT *psRGXCreateRenderContextOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	IMG_BYTE *psFrameworkCmdInt = IMG_NULL;
	IMG_HANDLE hPrivDataInt = IMG_NULL;
	RGX_SERVER_RENDER_CONTEXT * psRenderContextInt = IMG_NULL;




	if (psRGXCreateRenderContextIN->ui32FrameworkCmdize != 0)
	{
		psFrameworkCmdInt = OSAllocMem(psRGXCreateRenderContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE));
		if (!psFrameworkCmdInt)
		{
			psRGXCreateRenderContextOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXCreateRenderContext_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXCreateRenderContextIN->psFrameworkCmd, psRGXCreateRenderContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE))
				|| (OSCopyFromUser(NULL, psFrameworkCmdInt, psRGXCreateRenderContextIN->psFrameworkCmd,
				psRGXCreateRenderContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE)) != PVRSRV_OK) )
			{
				psRGXCreateRenderContextOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXCreateRenderContext_exit;
			}



				{
					/* Look up the address from the handle */
					psRGXCreateRenderContextOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &hDevNodeInt,
											psRGXCreateRenderContextIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXCreateRenderContextOUT->eError != PVRSRV_OK)
					{
						goto RGXCreateRenderContext_exit;
					}
				}


				{
					/* Look up the address from the handle */
					psRGXCreateRenderContextOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &hPrivDataInt,
											psRGXCreateRenderContextIN->hPrivData,
											PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA);
					if(psRGXCreateRenderContextOUT->eError != PVRSRV_OK)
					{
						goto RGXCreateRenderContext_exit;
					}
				}


	psRGXCreateRenderContextOUT->eError =
		PVRSRVRGXCreateRenderContextKM(psConnection,
					hDevNodeInt,
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


	psRGXCreateRenderContextOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psRGXCreateRenderContextOUT->hRenderContext,
							(IMG_VOID *) psRenderContextInt,
							PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&PVRSRVRGXDestroyRenderContextKM);
	if (psRGXCreateRenderContextOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateRenderContext_exit;
	}




RGXCreateRenderContext_exit:
	if (psRGXCreateRenderContextOUT->eError != PVRSRV_OK)
	{
		if (psRenderContextInt)
		{
			PVRSRVRGXDestroyRenderContextKM(psRenderContextInt);
		}
	}

	if (psFrameworkCmdInt)
		OSFreeMem(psFrameworkCmdInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXDestroyRenderContext(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXDESTROYRENDERCONTEXT *psRGXDestroyRenderContextIN,
					  PVRSRV_BRIDGE_OUT_RGXDESTROYRENDERCONTEXT *psRGXDestroyRenderContextOUT,
					 CONNECTION_DATA *psConnection)
{









	psRGXDestroyRenderContextOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyRenderContextIN->hCleanupCookie,
					PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT);
	if ((psRGXDestroyRenderContextOUT->eError != PVRSRV_OK) && (psRGXDestroyRenderContextOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		goto RGXDestroyRenderContext_exit;
	}



RGXDestroyRenderContext_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXKickTA3D(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXKICKTA3D *psRGXKickTA3DIN,
					  PVRSRV_BRIDGE_OUT_RGXKICKTA3D *psRGXKickTA3DOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_SERVER_RENDER_CONTEXT * psRenderContextInt = IMG_NULL;
	SYNC_PRIMITIVE_BLOCK * *psClientTAFenceSyncPrimBlockInt = IMG_NULL;
	IMG_HANDLE *hClientTAFenceSyncPrimBlockInt2 = IMG_NULL;
	IMG_UINT32 *ui32ClientTAFenceSyncOffsetInt = IMG_NULL;
	IMG_UINT32 *ui32ClientTAFenceValueInt = IMG_NULL;
	SYNC_PRIMITIVE_BLOCK * *psClientTAUpdateSyncPrimBlockInt = IMG_NULL;
	IMG_HANDLE *hClientTAUpdateSyncPrimBlockInt2 = IMG_NULL;
	IMG_UINT32 *ui32ClientTAUpdateSyncOffsetInt = IMG_NULL;
	IMG_UINT32 *ui32ClientTAUpdateValueInt = IMG_NULL;
	IMG_UINT32 *ui32ServerTASyncFlagsInt = IMG_NULL;
	SERVER_SYNC_PRIMITIVE * *psServerTASyncsInt = IMG_NULL;
	IMG_HANDLE *hServerTASyncsInt2 = IMG_NULL;
	SYNC_PRIMITIVE_BLOCK * *psClient3DFenceSyncPrimBlockInt = IMG_NULL;
	IMG_HANDLE *hClient3DFenceSyncPrimBlockInt2 = IMG_NULL;
	IMG_UINT32 *ui32Client3DFenceSyncOffsetInt = IMG_NULL;
	IMG_UINT32 *ui32Client3DFenceValueInt = IMG_NULL;
	SYNC_PRIMITIVE_BLOCK * *psClient3DUpdateSyncPrimBlockInt = IMG_NULL;
	IMG_HANDLE *hClient3DUpdateSyncPrimBlockInt2 = IMG_NULL;
	IMG_UINT32 *ui32Client3DUpdateSyncOffsetInt = IMG_NULL;
	IMG_UINT32 *ui32Client3DUpdateValueInt = IMG_NULL;
	IMG_UINT32 *ui32Server3DSyncFlagsInt = IMG_NULL;
	SERVER_SYNC_PRIMITIVE * *psServer3DSyncsInt = IMG_NULL;
	IMG_HANDLE *hServer3DSyncsInt2 = IMG_NULL;
	SYNC_PRIMITIVE_BLOCK * psPRFenceUFOSyncPrimBlockInt = IMG_NULL;
	IMG_INT32 *i32CheckFenceFDsInt = IMG_NULL;
	IMG_BYTE *psTACmdInt = IMG_NULL;
	IMG_BYTE *ps3DPRCmdInt = IMG_NULL;
	IMG_BYTE *ps3DCmdInt = IMG_NULL;
	RGX_RTDATA_CLEANUP_DATA * psRTDataCleanupInt = IMG_NULL;
	RGX_ZSBUFFER_DATA * psZBufferInt = IMG_NULL;
	RGX_ZSBUFFER_DATA * psSBufferInt = IMG_NULL;




	if (psRGXKickTA3DIN->ui32ClientTAFenceCount != 0)
	{
		psClientTAFenceSyncPrimBlockInt = OSAllocMem(psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *));
		if (!psClientTAFenceSyncPrimBlockInt)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
		hClientTAFenceSyncPrimBlockInt2 = OSAllocMem(psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_HANDLE));
		if (!hClientTAFenceSyncPrimBlockInt2)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickTA3DIN->phClientTAFenceSyncPrimBlock, psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hClientTAFenceSyncPrimBlockInt2, psRGXKickTA3DIN->phClientTAFenceSyncPrimBlock,
				psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickTA3D_exit;
			}
	if (psRGXKickTA3DIN->ui32ClientTAFenceCount != 0)
	{
		ui32ClientTAFenceSyncOffsetInt = OSAllocMem(psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_UINT32));
		if (!ui32ClientTAFenceSyncOffsetInt)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickTA3DIN->pui32ClientTAFenceSyncOffset, psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientTAFenceSyncOffsetInt, psRGXKickTA3DIN->pui32ClientTAFenceSyncOffset,
				psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickTA3D_exit;
			}
	if (psRGXKickTA3DIN->ui32ClientTAFenceCount != 0)
	{
		ui32ClientTAFenceValueInt = OSAllocMem(psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_UINT32));
		if (!ui32ClientTAFenceValueInt)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickTA3DIN->pui32ClientTAFenceValue, psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientTAFenceValueInt, psRGXKickTA3DIN->pui32ClientTAFenceValue,
				psRGXKickTA3DIN->ui32ClientTAFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickTA3D_exit;
			}
	if (psRGXKickTA3DIN->ui32ClientTAUpdateCount != 0)
	{
		psClientTAUpdateSyncPrimBlockInt = OSAllocMem(psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *));
		if (!psClientTAUpdateSyncPrimBlockInt)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
		hClientTAUpdateSyncPrimBlockInt2 = OSAllocMem(psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_HANDLE));
		if (!hClientTAUpdateSyncPrimBlockInt2)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickTA3DIN->phClientTAUpdateSyncPrimBlock, psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hClientTAUpdateSyncPrimBlockInt2, psRGXKickTA3DIN->phClientTAUpdateSyncPrimBlock,
				psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickTA3D_exit;
			}
	if (psRGXKickTA3DIN->ui32ClientTAUpdateCount != 0)
	{
		ui32ClientTAUpdateSyncOffsetInt = OSAllocMem(psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32));
		if (!ui32ClientTAUpdateSyncOffsetInt)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickTA3DIN->pui32ClientTAUpdateSyncOffset, psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientTAUpdateSyncOffsetInt, psRGXKickTA3DIN->pui32ClientTAUpdateSyncOffset,
				psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickTA3D_exit;
			}
	if (psRGXKickTA3DIN->ui32ClientTAUpdateCount != 0)
	{
		ui32ClientTAUpdateValueInt = OSAllocMem(psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32));
		if (!ui32ClientTAUpdateValueInt)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickTA3DIN->pui32ClientTAUpdateValue, psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientTAUpdateValueInt, psRGXKickTA3DIN->pui32ClientTAUpdateValue,
				psRGXKickTA3DIN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickTA3D_exit;
			}
	if (psRGXKickTA3DIN->ui32ServerTASyncPrims != 0)
	{
		ui32ServerTASyncFlagsInt = OSAllocMem(psRGXKickTA3DIN->ui32ServerTASyncPrims * sizeof(IMG_UINT32));
		if (!ui32ServerTASyncFlagsInt)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickTA3DIN->pui32ServerTASyncFlags, psRGXKickTA3DIN->ui32ServerTASyncPrims * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ServerTASyncFlagsInt, psRGXKickTA3DIN->pui32ServerTASyncFlags,
				psRGXKickTA3DIN->ui32ServerTASyncPrims * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickTA3D_exit;
			}
	if (psRGXKickTA3DIN->ui32ServerTASyncPrims != 0)
	{
		psServerTASyncsInt = OSAllocMem(psRGXKickTA3DIN->ui32ServerTASyncPrims * sizeof(SERVER_SYNC_PRIMITIVE *));
		if (!psServerTASyncsInt)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
		hServerTASyncsInt2 = OSAllocMem(psRGXKickTA3DIN->ui32ServerTASyncPrims * sizeof(IMG_HANDLE));
		if (!hServerTASyncsInt2)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickTA3DIN->phServerTASyncs, psRGXKickTA3DIN->ui32ServerTASyncPrims * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hServerTASyncsInt2, psRGXKickTA3DIN->phServerTASyncs,
				psRGXKickTA3DIN->ui32ServerTASyncPrims * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickTA3D_exit;
			}
	if (psRGXKickTA3DIN->ui32Client3DFenceCount != 0)
	{
		psClient3DFenceSyncPrimBlockInt = OSAllocMem(psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *));
		if (!psClient3DFenceSyncPrimBlockInt)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
		hClient3DFenceSyncPrimBlockInt2 = OSAllocMem(psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_HANDLE));
		if (!hClient3DFenceSyncPrimBlockInt2)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickTA3DIN->phClient3DFenceSyncPrimBlock, psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hClient3DFenceSyncPrimBlockInt2, psRGXKickTA3DIN->phClient3DFenceSyncPrimBlock,
				psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickTA3D_exit;
			}
	if (psRGXKickTA3DIN->ui32Client3DFenceCount != 0)
	{
		ui32Client3DFenceSyncOffsetInt = OSAllocMem(psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_UINT32));
		if (!ui32Client3DFenceSyncOffsetInt)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickTA3DIN->pui32Client3DFenceSyncOffset, psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32Client3DFenceSyncOffsetInt, psRGXKickTA3DIN->pui32Client3DFenceSyncOffset,
				psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickTA3D_exit;
			}
	if (psRGXKickTA3DIN->ui32Client3DFenceCount != 0)
	{
		ui32Client3DFenceValueInt = OSAllocMem(psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_UINT32));
		if (!ui32Client3DFenceValueInt)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickTA3DIN->pui32Client3DFenceValue, psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32Client3DFenceValueInt, psRGXKickTA3DIN->pui32Client3DFenceValue,
				psRGXKickTA3DIN->ui32Client3DFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickTA3D_exit;
			}
	if (psRGXKickTA3DIN->ui32Client3DUpdateCount != 0)
	{
		psClient3DUpdateSyncPrimBlockInt = OSAllocMem(psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *));
		if (!psClient3DUpdateSyncPrimBlockInt)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
		hClient3DUpdateSyncPrimBlockInt2 = OSAllocMem(psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_HANDLE));
		if (!hClient3DUpdateSyncPrimBlockInt2)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickTA3DIN->phClient3DUpdateSyncPrimBlock, psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hClient3DUpdateSyncPrimBlockInt2, psRGXKickTA3DIN->phClient3DUpdateSyncPrimBlock,
				psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickTA3D_exit;
			}
	if (psRGXKickTA3DIN->ui32Client3DUpdateCount != 0)
	{
		ui32Client3DUpdateSyncOffsetInt = OSAllocMem(psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_UINT32));
		if (!ui32Client3DUpdateSyncOffsetInt)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickTA3DIN->pui32Client3DUpdateSyncOffset, psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32Client3DUpdateSyncOffsetInt, psRGXKickTA3DIN->pui32Client3DUpdateSyncOffset,
				psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickTA3D_exit;
			}
	if (psRGXKickTA3DIN->ui32Client3DUpdateCount != 0)
	{
		ui32Client3DUpdateValueInt = OSAllocMem(psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_UINT32));
		if (!ui32Client3DUpdateValueInt)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickTA3DIN->pui32Client3DUpdateValue, psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32Client3DUpdateValueInt, psRGXKickTA3DIN->pui32Client3DUpdateValue,
				psRGXKickTA3DIN->ui32Client3DUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickTA3D_exit;
			}
	if (psRGXKickTA3DIN->ui32Server3DSyncPrims != 0)
	{
		ui32Server3DSyncFlagsInt = OSAllocMem(psRGXKickTA3DIN->ui32Server3DSyncPrims * sizeof(IMG_UINT32));
		if (!ui32Server3DSyncFlagsInt)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickTA3DIN->pui32Server3DSyncFlags, psRGXKickTA3DIN->ui32Server3DSyncPrims * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32Server3DSyncFlagsInt, psRGXKickTA3DIN->pui32Server3DSyncFlags,
				psRGXKickTA3DIN->ui32Server3DSyncPrims * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickTA3D_exit;
			}
	if (psRGXKickTA3DIN->ui32Server3DSyncPrims != 0)
	{
		psServer3DSyncsInt = OSAllocMem(psRGXKickTA3DIN->ui32Server3DSyncPrims * sizeof(SERVER_SYNC_PRIMITIVE *));
		if (!psServer3DSyncsInt)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
		hServer3DSyncsInt2 = OSAllocMem(psRGXKickTA3DIN->ui32Server3DSyncPrims * sizeof(IMG_HANDLE));
		if (!hServer3DSyncsInt2)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickTA3DIN->phServer3DSyncs, psRGXKickTA3DIN->ui32Server3DSyncPrims * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hServer3DSyncsInt2, psRGXKickTA3DIN->phServer3DSyncs,
				psRGXKickTA3DIN->ui32Server3DSyncPrims * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickTA3D_exit;
			}
	if (psRGXKickTA3DIN->ui32NumCheckFenceFDs != 0)
	{
		i32CheckFenceFDsInt = OSAllocMem(psRGXKickTA3DIN->ui32NumCheckFenceFDs * sizeof(IMG_INT32));
		if (!i32CheckFenceFDsInt)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickTA3DIN->pi32CheckFenceFDs, psRGXKickTA3DIN->ui32NumCheckFenceFDs * sizeof(IMG_INT32))
				|| (OSCopyFromUser(NULL, i32CheckFenceFDsInt, psRGXKickTA3DIN->pi32CheckFenceFDs,
				psRGXKickTA3DIN->ui32NumCheckFenceFDs * sizeof(IMG_INT32)) != PVRSRV_OK) )
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickTA3D_exit;
			}
	if (psRGXKickTA3DIN->ui32TACmdSize != 0)
	{
		psTACmdInt = OSAllocMem(psRGXKickTA3DIN->ui32TACmdSize * sizeof(IMG_BYTE));
		if (!psTACmdInt)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickTA3DIN->psTACmd, psRGXKickTA3DIN->ui32TACmdSize * sizeof(IMG_BYTE))
				|| (OSCopyFromUser(NULL, psTACmdInt, psRGXKickTA3DIN->psTACmd,
				psRGXKickTA3DIN->ui32TACmdSize * sizeof(IMG_BYTE)) != PVRSRV_OK) )
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickTA3D_exit;
			}
	if (psRGXKickTA3DIN->ui323DPRCmdSize != 0)
	{
		ps3DPRCmdInt = OSAllocMem(psRGXKickTA3DIN->ui323DPRCmdSize * sizeof(IMG_BYTE));
		if (!ps3DPRCmdInt)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickTA3DIN->ps3DPRCmd, psRGXKickTA3DIN->ui323DPRCmdSize * sizeof(IMG_BYTE))
				|| (OSCopyFromUser(NULL, ps3DPRCmdInt, psRGXKickTA3DIN->ps3DPRCmd,
				psRGXKickTA3DIN->ui323DPRCmdSize * sizeof(IMG_BYTE)) != PVRSRV_OK) )
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickTA3D_exit;
			}
	if (psRGXKickTA3DIN->ui323DCmdSize != 0)
	{
		ps3DCmdInt = OSAllocMem(psRGXKickTA3DIN->ui323DCmdSize * sizeof(IMG_BYTE));
		if (!ps3DCmdInt)
		{
			psRGXKickTA3DOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickTA3D_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickTA3DIN->ps3DCmd, psRGXKickTA3DIN->ui323DCmdSize * sizeof(IMG_BYTE))
				|| (OSCopyFromUser(NULL, ps3DCmdInt, psRGXKickTA3DIN->ps3DCmd,
				psRGXKickTA3DIN->ui323DCmdSize * sizeof(IMG_BYTE)) != PVRSRV_OK) )
			{
				psRGXKickTA3DOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickTA3D_exit;
			}



				{
					/* Look up the address from the handle */
					psRGXKickTA3DOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psRenderContextInt,
											psRGXKickTA3DIN->hRenderContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
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
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psClientTAFenceSyncPrimBlockInt[i],
											hClientTAFenceSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
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
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psClientTAUpdateSyncPrimBlockInt[i],
											hClientTAUpdateSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
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
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psServerTASyncsInt[i],
											hServerTASyncsInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
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
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psClient3DFenceSyncPrimBlockInt[i],
											hClient3DFenceSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
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
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psClient3DUpdateSyncPrimBlockInt[i],
											hClient3DUpdateSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
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
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psServer3DSyncsInt[i],
											hServer3DSyncsInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
						goto RGXKickTA3D_exit;
					}
				}

		}
	}

				{
					/* Look up the address from the handle */
					psRGXKickTA3DOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psPRFenceUFOSyncPrimBlockInt,
											psRGXKickTA3DIN->hPRFenceUFOSyncPrimBlock,
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
						goto RGXKickTA3D_exit;
					}
				}


				if (psRGXKickTA3DIN->hRTDataCleanup)
				{
					/* Look up the address from the handle */
					psRGXKickTA3DOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psRTDataCleanupInt,
											psRGXKickTA3DIN->hRTDataCleanup,
											PVRSRV_HANDLE_TYPE_RGX_RTDATA_CLEANUP);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
						goto RGXKickTA3D_exit;
					}
				}


				if (psRGXKickTA3DIN->hZBuffer)
				{
					/* Look up the address from the handle */
					psRGXKickTA3DOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psZBufferInt,
											psRGXKickTA3DIN->hZBuffer,
											PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
						goto RGXKickTA3D_exit;
					}
				}


				if (psRGXKickTA3DIN->hSBuffer)
				{
					/* Look up the address from the handle */
					psRGXKickTA3DOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psSBufferInt,
											psRGXKickTA3DIN->hSBuffer,
											PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER);
					if(psRGXKickTA3DOUT->eError != PVRSRV_OK)
					{
						goto RGXKickTA3D_exit;
					}
				}


	psRGXKickTA3DOUT->eError =
		PVRSRVRGXKickTA3DKM(
					psRenderContextInt,
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
					psRGXKickTA3DIN->ui32NumCheckFenceFDs,
					i32CheckFenceFDsInt,
					psRGXKickTA3DIN->i32UpdateFenceFD,
					psRGXKickTA3DIN->ui32TACmdSize,
					psTACmdInt,
					psRGXKickTA3DIN->ui323DPRCmdSize,
					ps3DPRCmdInt,
					psRGXKickTA3DIN->ui323DCmdSize,
					ps3DCmdInt,
					psRGXKickTA3DIN->ui32ExternalJobReference,
					psRGXKickTA3DIN->ui32InternalJobReference,
					psRGXKickTA3DIN->bbLastTAInScene,
					psRGXKickTA3DIN->bbKickTA,
					psRGXKickTA3DIN->bbKickPR,
					psRGXKickTA3DIN->bbKick3D,
					psRGXKickTA3DIN->bbAbort,
					psRGXKickTA3DIN->bbPDumpContinuous,
					psRTDataCleanupInt,
					psZBufferInt,
					psSBufferInt,
					psRGXKickTA3DIN->bbCommitRefCountsTA,
					psRGXKickTA3DIN->bbCommitRefCounts3D,
					&psRGXKickTA3DOUT->bbCommittedRefCountsTA,
					&psRGXKickTA3DOUT->bbCommittedRefCounts3D);




RGXKickTA3D_exit:
	if (psClientTAFenceSyncPrimBlockInt)
		OSFreeMem(psClientTAFenceSyncPrimBlockInt);
	if (hClientTAFenceSyncPrimBlockInt2)
		OSFreeMem(hClientTAFenceSyncPrimBlockInt2);
	if (ui32ClientTAFenceSyncOffsetInt)
		OSFreeMem(ui32ClientTAFenceSyncOffsetInt);
	if (ui32ClientTAFenceValueInt)
		OSFreeMem(ui32ClientTAFenceValueInt);
	if (psClientTAUpdateSyncPrimBlockInt)
		OSFreeMem(psClientTAUpdateSyncPrimBlockInt);
	if (hClientTAUpdateSyncPrimBlockInt2)
		OSFreeMem(hClientTAUpdateSyncPrimBlockInt2);
	if (ui32ClientTAUpdateSyncOffsetInt)
		OSFreeMem(ui32ClientTAUpdateSyncOffsetInt);
	if (ui32ClientTAUpdateValueInt)
		OSFreeMem(ui32ClientTAUpdateValueInt);
	if (ui32ServerTASyncFlagsInt)
		OSFreeMem(ui32ServerTASyncFlagsInt);
	if (psServerTASyncsInt)
		OSFreeMem(psServerTASyncsInt);
	if (hServerTASyncsInt2)
		OSFreeMem(hServerTASyncsInt2);
	if (psClient3DFenceSyncPrimBlockInt)
		OSFreeMem(psClient3DFenceSyncPrimBlockInt);
	if (hClient3DFenceSyncPrimBlockInt2)
		OSFreeMem(hClient3DFenceSyncPrimBlockInt2);
	if (ui32Client3DFenceSyncOffsetInt)
		OSFreeMem(ui32Client3DFenceSyncOffsetInt);
	if (ui32Client3DFenceValueInt)
		OSFreeMem(ui32Client3DFenceValueInt);
	if (psClient3DUpdateSyncPrimBlockInt)
		OSFreeMem(psClient3DUpdateSyncPrimBlockInt);
	if (hClient3DUpdateSyncPrimBlockInt2)
		OSFreeMem(hClient3DUpdateSyncPrimBlockInt2);
	if (ui32Client3DUpdateSyncOffsetInt)
		OSFreeMem(ui32Client3DUpdateSyncOffsetInt);
	if (ui32Client3DUpdateValueInt)
		OSFreeMem(ui32Client3DUpdateValueInt);
	if (ui32Server3DSyncFlagsInt)
		OSFreeMem(ui32Server3DSyncFlagsInt);
	if (psServer3DSyncsInt)
		OSFreeMem(psServer3DSyncsInt);
	if (hServer3DSyncsInt2)
		OSFreeMem(hServer3DSyncsInt2);
	if (i32CheckFenceFDsInt)
		OSFreeMem(i32CheckFenceFDsInt);
	if (psTACmdInt)
		OSFreeMem(psTACmdInt);
	if (ps3DPRCmdInt)
		OSFreeMem(ps3DPRCmdInt);
	if (ps3DCmdInt)
		OSFreeMem(ps3DCmdInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXSetRenderContextPriority(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXSETRENDERCONTEXTPRIORITY *psRGXSetRenderContextPriorityIN,
					  PVRSRV_BRIDGE_OUT_RGXSETRENDERCONTEXTPRIORITY *psRGXSetRenderContextPriorityOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_SERVER_RENDER_CONTEXT * psRenderContextInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psRGXSetRenderContextPriorityOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psRenderContextInt,
											psRGXSetRenderContextPriorityIN->hRenderContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT);
					if(psRGXSetRenderContextPriorityOUT->eError != PVRSRV_OK)
					{
						goto RGXSetRenderContextPriority_exit;
					}
				}


	psRGXSetRenderContextPriorityOUT->eError =
		PVRSRVRGXSetRenderContextPriorityKM(psConnection,
					psRenderContextInt,
					psRGXSetRenderContextPriorityIN->ui32Priority);




RGXSetRenderContextPriority_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXGetLastRenderContextResetReason(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXGETLASTRENDERCONTEXTRESETREASON *psRGXGetLastRenderContextResetReasonIN,
					  PVRSRV_BRIDGE_OUT_RGXGETLASTRENDERCONTEXTRESETREASON *psRGXGetLastRenderContextResetReasonOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_SERVER_RENDER_CONTEXT * psRenderContextInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psRGXGetLastRenderContextResetReasonOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psRenderContextInt,
											psRGXGetLastRenderContextResetReasonIN->hRenderContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT);
					if(psRGXGetLastRenderContextResetReasonOUT->eError != PVRSRV_OK)
					{
						goto RGXGetLastRenderContextResetReason_exit;
					}
				}


	psRGXGetLastRenderContextResetReasonOUT->eError =
		PVRSRVRGXGetLastRenderContextResetReasonKM(
					psRenderContextInt,
					&psRGXGetLastRenderContextResetReasonOUT->ui32LastResetReason);




RGXGetLastRenderContextResetReason_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXGetPartialRenderCount(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXGETPARTIALRENDERCOUNT *psRGXGetPartialRenderCountIN,
					  PVRSRV_BRIDGE_OUT_RGXGETPARTIALRENDERCOUNT *psRGXGetPartialRenderCountOUT,
					 CONNECTION_DATA *psConnection)
{
	DEVMEM_MEMDESC * psHWRTDataMemDescInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psRGXGetPartialRenderCountOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psHWRTDataMemDescInt,
											psRGXGetPartialRenderCountIN->hHWRTDataMemDesc,
											PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC);
					if(psRGXGetPartialRenderCountOUT->eError != PVRSRV_OK)
					{
						goto RGXGetPartialRenderCount_exit;
					}
				}


	psRGXGetPartialRenderCountOUT->eError =
		PVRSRVRGXGetPartialRenderCountKM(
					psHWRTDataMemDescInt,
					&psRGXGetPartialRenderCountOUT->ui32NumPartialRenders);




RGXGetPartialRenderCount_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXKickSyncTA(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXKICKSYNCTA *psRGXKickSyncTAIN,
					  PVRSRV_BRIDGE_OUT_RGXKICKSYNCTA *psRGXKickSyncTAOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_SERVER_RENDER_CONTEXT * psRenderContextInt = IMG_NULL;
	SYNC_PRIMITIVE_BLOCK * *psClientTAFenceUFOSyncPrimBlockInt = IMG_NULL;
	IMG_HANDLE *hClientTAFenceUFOSyncPrimBlockInt2 = IMG_NULL;
	IMG_UINT32 *ui32ClientTAFenceOffsetInt = IMG_NULL;
	IMG_UINT32 *ui32ClientTAFenceValueInt = IMG_NULL;
	SYNC_PRIMITIVE_BLOCK * *psClientTAUpdateUFOSyncPrimBlockInt = IMG_NULL;
	IMG_HANDLE *hClientTAUpdateUFOSyncPrimBlockInt2 = IMG_NULL;
	IMG_UINT32 *ui32ClientTAUpdateOffsetInt = IMG_NULL;
	IMG_UINT32 *ui32ClientTAUpdateValueInt = IMG_NULL;
	IMG_UINT32 *ui32ServerTASyncFlagsInt = IMG_NULL;
	SERVER_SYNC_PRIMITIVE * *psServerTASyncsInt = IMG_NULL;
	IMG_HANDLE *hServerTASyncsInt2 = IMG_NULL;
	SYNC_PRIMITIVE_BLOCK * *psClient3DFenceUFOSyncPrimBlockInt = IMG_NULL;
	IMG_HANDLE *hClient3DFenceUFOSyncPrimBlockInt2 = IMG_NULL;
	IMG_UINT32 *ui32Client3DFenceOffsetInt = IMG_NULL;
	IMG_UINT32 *ui32Client3DFenceValueInt = IMG_NULL;
	SYNC_PRIMITIVE_BLOCK * *psClient3DUpdateUFOSyncPrimBlockInt = IMG_NULL;
	IMG_HANDLE *hClient3DUpdateUFOSyncPrimBlockInt2 = IMG_NULL;
	IMG_UINT32 *ui32Client3DUpdateOffsetInt = IMG_NULL;
	IMG_UINT32 *ui32Client3DUpdateValueInt = IMG_NULL;
	IMG_UINT32 *ui32Server3DSyncFlagsInt = IMG_NULL;
	SERVER_SYNC_PRIMITIVE * *psServer3DSyncsInt = IMG_NULL;
	IMG_HANDLE *hServer3DSyncsInt2 = IMG_NULL;
	IMG_INT32 *i32CheckFenceFDsInt = IMG_NULL;




	if (psRGXKickSyncTAIN->ui32ClientTAFenceCount != 0)
	{
		psClientTAFenceUFOSyncPrimBlockInt = OSAllocMem(psRGXKickSyncTAIN->ui32ClientTAFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *));
		if (!psClientTAFenceUFOSyncPrimBlockInt)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
		hClientTAFenceUFOSyncPrimBlockInt2 = OSAllocMem(psRGXKickSyncTAIN->ui32ClientTAFenceCount * sizeof(IMG_HANDLE));
		if (!hClientTAFenceUFOSyncPrimBlockInt2)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTAIN->phClientTAFenceUFOSyncPrimBlock, psRGXKickSyncTAIN->ui32ClientTAFenceCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hClientTAFenceUFOSyncPrimBlockInt2, psRGXKickSyncTAIN->phClientTAFenceUFOSyncPrimBlock,
				psRGXKickSyncTAIN->ui32ClientTAFenceCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTA_exit;
			}
	if (psRGXKickSyncTAIN->ui32ClientTAFenceCount != 0)
	{
		ui32ClientTAFenceOffsetInt = OSAllocMem(psRGXKickSyncTAIN->ui32ClientTAFenceCount * sizeof(IMG_UINT32));
		if (!ui32ClientTAFenceOffsetInt)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTAIN->pui32ClientTAFenceOffset, psRGXKickSyncTAIN->ui32ClientTAFenceCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientTAFenceOffsetInt, psRGXKickSyncTAIN->pui32ClientTAFenceOffset,
				psRGXKickSyncTAIN->ui32ClientTAFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTA_exit;
			}
	if (psRGXKickSyncTAIN->ui32ClientTAFenceCount != 0)
	{
		ui32ClientTAFenceValueInt = OSAllocMem(psRGXKickSyncTAIN->ui32ClientTAFenceCount * sizeof(IMG_UINT32));
		if (!ui32ClientTAFenceValueInt)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTAIN->pui32ClientTAFenceValue, psRGXKickSyncTAIN->ui32ClientTAFenceCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientTAFenceValueInt, psRGXKickSyncTAIN->pui32ClientTAFenceValue,
				psRGXKickSyncTAIN->ui32ClientTAFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTA_exit;
			}
	if (psRGXKickSyncTAIN->ui32ClientTAUpdateCount != 0)
	{
		psClientTAUpdateUFOSyncPrimBlockInt = OSAllocMem(psRGXKickSyncTAIN->ui32ClientTAUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *));
		if (!psClientTAUpdateUFOSyncPrimBlockInt)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
		hClientTAUpdateUFOSyncPrimBlockInt2 = OSAllocMem(psRGXKickSyncTAIN->ui32ClientTAUpdateCount * sizeof(IMG_HANDLE));
		if (!hClientTAUpdateUFOSyncPrimBlockInt2)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTAIN->phClientTAUpdateUFOSyncPrimBlock, psRGXKickSyncTAIN->ui32ClientTAUpdateCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hClientTAUpdateUFOSyncPrimBlockInt2, psRGXKickSyncTAIN->phClientTAUpdateUFOSyncPrimBlock,
				psRGXKickSyncTAIN->ui32ClientTAUpdateCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTA_exit;
			}
	if (psRGXKickSyncTAIN->ui32ClientTAUpdateCount != 0)
	{
		ui32ClientTAUpdateOffsetInt = OSAllocMem(psRGXKickSyncTAIN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32));
		if (!ui32ClientTAUpdateOffsetInt)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTAIN->pui32ClientTAUpdateOffset, psRGXKickSyncTAIN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientTAUpdateOffsetInt, psRGXKickSyncTAIN->pui32ClientTAUpdateOffset,
				psRGXKickSyncTAIN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTA_exit;
			}
	if (psRGXKickSyncTAIN->ui32ClientTAUpdateCount != 0)
	{
		ui32ClientTAUpdateValueInt = OSAllocMem(psRGXKickSyncTAIN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32));
		if (!ui32ClientTAUpdateValueInt)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTAIN->pui32ClientTAUpdateValue, psRGXKickSyncTAIN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientTAUpdateValueInt, psRGXKickSyncTAIN->pui32ClientTAUpdateValue,
				psRGXKickSyncTAIN->ui32ClientTAUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTA_exit;
			}
	if (psRGXKickSyncTAIN->ui32ServerTASyncPrims != 0)
	{
		ui32ServerTASyncFlagsInt = OSAllocMem(psRGXKickSyncTAIN->ui32ServerTASyncPrims * sizeof(IMG_UINT32));
		if (!ui32ServerTASyncFlagsInt)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTAIN->pui32ServerTASyncFlags, psRGXKickSyncTAIN->ui32ServerTASyncPrims * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ServerTASyncFlagsInt, psRGXKickSyncTAIN->pui32ServerTASyncFlags,
				psRGXKickSyncTAIN->ui32ServerTASyncPrims * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTA_exit;
			}
	if (psRGXKickSyncTAIN->ui32ServerTASyncPrims != 0)
	{
		psServerTASyncsInt = OSAllocMem(psRGXKickSyncTAIN->ui32ServerTASyncPrims * sizeof(SERVER_SYNC_PRIMITIVE *));
		if (!psServerTASyncsInt)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
		hServerTASyncsInt2 = OSAllocMem(psRGXKickSyncTAIN->ui32ServerTASyncPrims * sizeof(IMG_HANDLE));
		if (!hServerTASyncsInt2)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTAIN->phServerTASyncs, psRGXKickSyncTAIN->ui32ServerTASyncPrims * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hServerTASyncsInt2, psRGXKickSyncTAIN->phServerTASyncs,
				psRGXKickSyncTAIN->ui32ServerTASyncPrims * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTA_exit;
			}
	if (psRGXKickSyncTAIN->ui32Client3DFenceCount != 0)
	{
		psClient3DFenceUFOSyncPrimBlockInt = OSAllocMem(psRGXKickSyncTAIN->ui32Client3DFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *));
		if (!psClient3DFenceUFOSyncPrimBlockInt)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
		hClient3DFenceUFOSyncPrimBlockInt2 = OSAllocMem(psRGXKickSyncTAIN->ui32Client3DFenceCount * sizeof(IMG_HANDLE));
		if (!hClient3DFenceUFOSyncPrimBlockInt2)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTAIN->phClient3DFenceUFOSyncPrimBlock, psRGXKickSyncTAIN->ui32Client3DFenceCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hClient3DFenceUFOSyncPrimBlockInt2, psRGXKickSyncTAIN->phClient3DFenceUFOSyncPrimBlock,
				psRGXKickSyncTAIN->ui32Client3DFenceCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTA_exit;
			}
	if (psRGXKickSyncTAIN->ui32Client3DFenceCount != 0)
	{
		ui32Client3DFenceOffsetInt = OSAllocMem(psRGXKickSyncTAIN->ui32Client3DFenceCount * sizeof(IMG_UINT32));
		if (!ui32Client3DFenceOffsetInt)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTAIN->pui32Client3DFenceOffset, psRGXKickSyncTAIN->ui32Client3DFenceCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32Client3DFenceOffsetInt, psRGXKickSyncTAIN->pui32Client3DFenceOffset,
				psRGXKickSyncTAIN->ui32Client3DFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTA_exit;
			}
	if (psRGXKickSyncTAIN->ui32Client3DFenceCount != 0)
	{
		ui32Client3DFenceValueInt = OSAllocMem(psRGXKickSyncTAIN->ui32Client3DFenceCount * sizeof(IMG_UINT32));
		if (!ui32Client3DFenceValueInt)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTAIN->pui32Client3DFenceValue, psRGXKickSyncTAIN->ui32Client3DFenceCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32Client3DFenceValueInt, psRGXKickSyncTAIN->pui32Client3DFenceValue,
				psRGXKickSyncTAIN->ui32Client3DFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTA_exit;
			}
	if (psRGXKickSyncTAIN->ui32Client3DUpdateCount != 0)
	{
		psClient3DUpdateUFOSyncPrimBlockInt = OSAllocMem(psRGXKickSyncTAIN->ui32Client3DUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *));
		if (!psClient3DUpdateUFOSyncPrimBlockInt)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
		hClient3DUpdateUFOSyncPrimBlockInt2 = OSAllocMem(psRGXKickSyncTAIN->ui32Client3DUpdateCount * sizeof(IMG_HANDLE));
		if (!hClient3DUpdateUFOSyncPrimBlockInt2)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTAIN->phClient3DUpdateUFOSyncPrimBlock, psRGXKickSyncTAIN->ui32Client3DUpdateCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hClient3DUpdateUFOSyncPrimBlockInt2, psRGXKickSyncTAIN->phClient3DUpdateUFOSyncPrimBlock,
				psRGXKickSyncTAIN->ui32Client3DUpdateCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTA_exit;
			}
	if (psRGXKickSyncTAIN->ui32Client3DUpdateCount != 0)
	{
		ui32Client3DUpdateOffsetInt = OSAllocMem(psRGXKickSyncTAIN->ui32Client3DUpdateCount * sizeof(IMG_UINT32));
		if (!ui32Client3DUpdateOffsetInt)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTAIN->pui32Client3DUpdateOffset, psRGXKickSyncTAIN->ui32Client3DUpdateCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32Client3DUpdateOffsetInt, psRGXKickSyncTAIN->pui32Client3DUpdateOffset,
				psRGXKickSyncTAIN->ui32Client3DUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTA_exit;
			}
	if (psRGXKickSyncTAIN->ui32Client3DUpdateCount != 0)
	{
		ui32Client3DUpdateValueInt = OSAllocMem(psRGXKickSyncTAIN->ui32Client3DUpdateCount * sizeof(IMG_UINT32));
		if (!ui32Client3DUpdateValueInt)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTAIN->pui32Client3DUpdateValue, psRGXKickSyncTAIN->ui32Client3DUpdateCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32Client3DUpdateValueInt, psRGXKickSyncTAIN->pui32Client3DUpdateValue,
				psRGXKickSyncTAIN->ui32Client3DUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTA_exit;
			}
	if (psRGXKickSyncTAIN->ui32Server3DSyncPrims != 0)
	{
		ui32Server3DSyncFlagsInt = OSAllocMem(psRGXKickSyncTAIN->ui32Server3DSyncPrims * sizeof(IMG_UINT32));
		if (!ui32Server3DSyncFlagsInt)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTAIN->pui32Server3DSyncFlags, psRGXKickSyncTAIN->ui32Server3DSyncPrims * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32Server3DSyncFlagsInt, psRGXKickSyncTAIN->pui32Server3DSyncFlags,
				psRGXKickSyncTAIN->ui32Server3DSyncPrims * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTA_exit;
			}
	if (psRGXKickSyncTAIN->ui32Server3DSyncPrims != 0)
	{
		psServer3DSyncsInt = OSAllocMem(psRGXKickSyncTAIN->ui32Server3DSyncPrims * sizeof(SERVER_SYNC_PRIMITIVE *));
		if (!psServer3DSyncsInt)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
		hServer3DSyncsInt2 = OSAllocMem(psRGXKickSyncTAIN->ui32Server3DSyncPrims * sizeof(IMG_HANDLE));
		if (!hServer3DSyncsInt2)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTAIN->phServer3DSyncs, psRGXKickSyncTAIN->ui32Server3DSyncPrims * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hServer3DSyncsInt2, psRGXKickSyncTAIN->phServer3DSyncs,
				psRGXKickSyncTAIN->ui32Server3DSyncPrims * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTA_exit;
			}
	if (psRGXKickSyncTAIN->ui32NumCheckFenceFDs != 0)
	{
		i32CheckFenceFDsInt = OSAllocMem(psRGXKickSyncTAIN->ui32NumCheckFenceFDs * sizeof(IMG_INT32));
		if (!i32CheckFenceFDsInt)
		{
			psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTA_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTAIN->pi32CheckFenceFDs, psRGXKickSyncTAIN->ui32NumCheckFenceFDs * sizeof(IMG_INT32))
				|| (OSCopyFromUser(NULL, i32CheckFenceFDsInt, psRGXKickSyncTAIN->pi32CheckFenceFDs,
				psRGXKickSyncTAIN->ui32NumCheckFenceFDs * sizeof(IMG_INT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncTAOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTA_exit;
			}



				{
					/* Look up the address from the handle */
					psRGXKickSyncTAOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psRenderContextInt,
											psRGXKickSyncTAIN->hRenderContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT);
					if(psRGXKickSyncTAOUT->eError != PVRSRV_OK)
					{
						goto RGXKickSyncTA_exit;
					}
				}


	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickSyncTAIN->ui32ClientTAFenceCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickSyncTAOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psClientTAFenceUFOSyncPrimBlockInt[i],
											hClientTAFenceUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXKickSyncTAOUT->eError != PVRSRV_OK)
					{
						goto RGXKickSyncTA_exit;
					}
				}

		}
	}

	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickSyncTAIN->ui32ClientTAUpdateCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickSyncTAOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psClientTAUpdateUFOSyncPrimBlockInt[i],
											hClientTAUpdateUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXKickSyncTAOUT->eError != PVRSRV_OK)
					{
						goto RGXKickSyncTA_exit;
					}
				}

		}
	}

	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickSyncTAIN->ui32ServerTASyncPrims;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickSyncTAOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psServerTASyncsInt[i],
											hServerTASyncsInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
					if(psRGXKickSyncTAOUT->eError != PVRSRV_OK)
					{
						goto RGXKickSyncTA_exit;
					}
				}

		}
	}

	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickSyncTAIN->ui32Client3DFenceCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickSyncTAOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psClient3DFenceUFOSyncPrimBlockInt[i],
											hClient3DFenceUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXKickSyncTAOUT->eError != PVRSRV_OK)
					{
						goto RGXKickSyncTA_exit;
					}
				}

		}
	}

	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickSyncTAIN->ui32Client3DUpdateCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickSyncTAOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psClient3DUpdateUFOSyncPrimBlockInt[i],
											hClient3DUpdateUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXKickSyncTAOUT->eError != PVRSRV_OK)
					{
						goto RGXKickSyncTA_exit;
					}
				}

		}
	}

	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickSyncTAIN->ui32Server3DSyncPrims;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickSyncTAOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psServer3DSyncsInt[i],
											hServer3DSyncsInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
					if(psRGXKickSyncTAOUT->eError != PVRSRV_OK)
					{
						goto RGXKickSyncTA_exit;
					}
				}

		}
	}

	psRGXKickSyncTAOUT->eError =
		PVRSRVRGXKickSyncTAKM(
					psRenderContextInt,
					psRGXKickSyncTAIN->ui32ClientTAFenceCount,
					psClientTAFenceUFOSyncPrimBlockInt,
					ui32ClientTAFenceOffsetInt,
					ui32ClientTAFenceValueInt,
					psRGXKickSyncTAIN->ui32ClientTAUpdateCount,
					psClientTAUpdateUFOSyncPrimBlockInt,
					ui32ClientTAUpdateOffsetInt,
					ui32ClientTAUpdateValueInt,
					psRGXKickSyncTAIN->ui32ServerTASyncPrims,
					ui32ServerTASyncFlagsInt,
					psServerTASyncsInt,
					psRGXKickSyncTAIN->ui32Client3DFenceCount,
					psClient3DFenceUFOSyncPrimBlockInt,
					ui32Client3DFenceOffsetInt,
					ui32Client3DFenceValueInt,
					psRGXKickSyncTAIN->ui32Client3DUpdateCount,
					psClient3DUpdateUFOSyncPrimBlockInt,
					ui32Client3DUpdateOffsetInt,
					ui32Client3DUpdateValueInt,
					psRGXKickSyncTAIN->ui32Server3DSyncPrims,
					ui32Server3DSyncFlagsInt,
					psServer3DSyncsInt,
					psRGXKickSyncTAIN->ui32NumCheckFenceFDs,
					i32CheckFenceFDsInt,
					psRGXKickSyncTAIN->i32UpdateFenceFD,
					psRGXKickSyncTAIN->bbPDumpContinuous);




RGXKickSyncTA_exit:
	if (psClientTAFenceUFOSyncPrimBlockInt)
		OSFreeMem(psClientTAFenceUFOSyncPrimBlockInt);
	if (hClientTAFenceUFOSyncPrimBlockInt2)
		OSFreeMem(hClientTAFenceUFOSyncPrimBlockInt2);
	if (ui32ClientTAFenceOffsetInt)
		OSFreeMem(ui32ClientTAFenceOffsetInt);
	if (ui32ClientTAFenceValueInt)
		OSFreeMem(ui32ClientTAFenceValueInt);
	if (psClientTAUpdateUFOSyncPrimBlockInt)
		OSFreeMem(psClientTAUpdateUFOSyncPrimBlockInt);
	if (hClientTAUpdateUFOSyncPrimBlockInt2)
		OSFreeMem(hClientTAUpdateUFOSyncPrimBlockInt2);
	if (ui32ClientTAUpdateOffsetInt)
		OSFreeMem(ui32ClientTAUpdateOffsetInt);
	if (ui32ClientTAUpdateValueInt)
		OSFreeMem(ui32ClientTAUpdateValueInt);
	if (ui32ServerTASyncFlagsInt)
		OSFreeMem(ui32ServerTASyncFlagsInt);
	if (psServerTASyncsInt)
		OSFreeMem(psServerTASyncsInt);
	if (hServerTASyncsInt2)
		OSFreeMem(hServerTASyncsInt2);
	if (psClient3DFenceUFOSyncPrimBlockInt)
		OSFreeMem(psClient3DFenceUFOSyncPrimBlockInt);
	if (hClient3DFenceUFOSyncPrimBlockInt2)
		OSFreeMem(hClient3DFenceUFOSyncPrimBlockInt2);
	if (ui32Client3DFenceOffsetInt)
		OSFreeMem(ui32Client3DFenceOffsetInt);
	if (ui32Client3DFenceValueInt)
		OSFreeMem(ui32Client3DFenceValueInt);
	if (psClient3DUpdateUFOSyncPrimBlockInt)
		OSFreeMem(psClient3DUpdateUFOSyncPrimBlockInt);
	if (hClient3DUpdateUFOSyncPrimBlockInt2)
		OSFreeMem(hClient3DUpdateUFOSyncPrimBlockInt2);
	if (ui32Client3DUpdateOffsetInt)
		OSFreeMem(ui32Client3DUpdateOffsetInt);
	if (ui32Client3DUpdateValueInt)
		OSFreeMem(ui32Client3DUpdateValueInt);
	if (ui32Server3DSyncFlagsInt)
		OSFreeMem(ui32Server3DSyncFlagsInt);
	if (psServer3DSyncsInt)
		OSFreeMem(psServer3DSyncsInt);
	if (hServer3DSyncsInt2)
		OSFreeMem(hServer3DSyncsInt2);
	if (i32CheckFenceFDsInt)
		OSFreeMem(i32CheckFenceFDsInt);

	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */


PVRSRV_ERROR InitRGXTA3DBridge(IMG_VOID);
PVRSRV_ERROR DeinitRGXTA3DBridge(IMG_VOID);

/*
 * Register all RGXTA3D functions with services
 */
PVRSRV_ERROR InitRGXTA3DBridge(IMG_VOID)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXCREATEHWRTDATA, PVRSRVBridgeRGXCreateHWRTData,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYHWRTDATA, PVRSRVBridgeRGXDestroyHWRTData,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXCREATERENDERTARGET, PVRSRVBridgeRGXCreateRenderTarget,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYRENDERTARGET, PVRSRVBridgeRGXDestroyRenderTarget,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXCREATEZSBUFFER, PVRSRVBridgeRGXCreateZSBuffer,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYZSBUFFER, PVRSRVBridgeRGXDestroyZSBuffer,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXPOPULATEZSBUFFER, PVRSRVBridgeRGXPopulateZSBuffer,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXUNPOPULATEZSBUFFER, PVRSRVBridgeRGXUnpopulateZSBuffer,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXCREATEFREELIST, PVRSRVBridgeRGXCreateFreeList,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYFREELIST, PVRSRVBridgeRGXDestroyFreeList,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXADDBLOCKTOFREELIST, PVRSRVBridgeRGXAddBlockToFreeList,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXREMOVEBLOCKFROMFREELIST, PVRSRVBridgeRGXRemoveBlockFromFreeList,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXCREATERENDERCONTEXT, PVRSRVBridgeRGXCreateRenderContext,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYRENDERCONTEXT, PVRSRVBridgeRGXDestroyRenderContext,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXKICKTA3D, PVRSRVBridgeRGXKickTA3D,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXSETRENDERCONTEXTPRIORITY, PVRSRVBridgeRGXSetRenderContextPriority,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXGETLASTRENDERCONTEXTRESETREASON, PVRSRVBridgeRGXGetLastRenderContextResetReason,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXGETPARTIALRENDERCOUNT, PVRSRVBridgeRGXGetPartialRenderCount,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTA3D, PVRSRV_BRIDGE_RGXTA3D_RGXKICKSYNCTA, PVRSRVBridgeRGXKickSyncTA,
					IMG_NULL, IMG_NULL,
					0, 0);


	return PVRSRV_OK;
}

/*
 * Unregister all rgxta3d functions with services
 */
PVRSRV_ERROR DeinitRGXTA3DBridge(IMG_VOID)
{
	return PVRSRV_OK;
}

