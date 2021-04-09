/*************************************************************************/ /*!
@File
@Title          Server bridge for rgxray
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for rgxray
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

#include "rgxray.h"
#include "devicemem_server.h"


#include "common_rgxray_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#include "rgx_bridge.h"
#include "srvcore.h"
#include "handle.h"

#include <linux/slab.h>


#include "rgx_bvnc_defs_km.h"




/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeRGXCreateRPMFreeList(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCREATERPMFREELIST *psRGXCreateRPMFreeListIN,
					  PVRSRV_BRIDGE_OUT_RGXCREATERPMFREELIST *psRGXCreateRPMFreeListOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hRPMContext = psRGXCreateRPMFreeListIN->hRPMContext;
	RGX_SERVER_RPM_CONTEXT * psRPMContextInt = NULL;
	RGX_RPM_FREELIST * psCleanupCookieInt = NULL;


	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevData(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
			!psDeviceNode->pfnCheckDeviceFeature(psDeviceNode, RGX_FEATURE_RAY_TRACING_BIT_MASK))
		{
			psRGXCreateRPMFreeListOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXCreateRPMFreeList_exit;
		}
	}





	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXCreateRPMFreeListOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psRPMContextInt,
											hRPMContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RPM_CONTEXT,
											IMG_TRUE);
					if(psRGXCreateRPMFreeListOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXCreateRPMFreeList_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXCreateRPMFreeListOUT->eError =
		RGXCreateRPMFreeList(psConnection, OSGetDevData(psConnection),
					psRPMContextInt,
					psRGXCreateRPMFreeListIN->ui32InitFLPages,
					psRGXCreateRPMFreeListIN->ui32GrowFLPages,
					psRGXCreateRPMFreeListIN->sFreeListDevVAddr,
					&psCleanupCookieInt,
					&psRGXCreateRPMFreeListOUT->ui32HWFreeList,
					psRGXCreateRPMFreeListIN->bIsExternal);
	/* Exit early if bridged call fails */
	if(psRGXCreateRPMFreeListOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateRPMFreeList_exit;
	}

	/* Lock over handle creation. */
	LockHandle();





	psRGXCreateRPMFreeListOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,

							&psRGXCreateRPMFreeListOUT->hCleanupCookie,
							(void *) psCleanupCookieInt,
							PVRSRV_HANDLE_TYPE_RGX_RPM_FREELIST,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,(PFN_HANDLE_RELEASE)&RGXDestroyRPMFreeList);
	if (psRGXCreateRPMFreeListOUT->eError != PVRSRV_OK)
	{
		UnlockHandle();
		goto RGXCreateRPMFreeList_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle();



RGXCreateRPMFreeList_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psRPMContextInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hRPMContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RPM_CONTEXT);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();

	if (psRGXCreateRPMFreeListOUT->eError != PVRSRV_OK)
	{
		if (psCleanupCookieInt)
		{
			RGXDestroyRPMFreeList(psCleanupCookieInt);
		}
	}


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXDestroyRPMFreeList(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXDESTROYRPMFREELIST *psRGXDestroyRPMFreeListIN,
					  PVRSRV_BRIDGE_OUT_RGXDESTROYRPMFREELIST *psRGXDestroyRPMFreeListOUT,
					 CONNECTION_DATA *psConnection)
{


	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevData(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
			!psDeviceNode->pfnCheckDeviceFeature(psDeviceNode, RGX_FEATURE_RAY_TRACING_BIT_MASK))
		{
			psRGXDestroyRPMFreeListOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXDestroyRPMFreeList_exit;
		}
	}







	/* Lock over handle destruction. */
	LockHandle();





	psRGXDestroyRPMFreeListOUT->eError =
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyRPMFreeListIN->hCleanupCookie,
					PVRSRV_HANDLE_TYPE_RGX_RPM_FREELIST);
	if ((psRGXDestroyRPMFreeListOUT->eError != PVRSRV_OK) &&
	    (psRGXDestroyRPMFreeListOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "PVRSRVBridgeRGXDestroyRPMFreeList: %s",
		        PVRSRVGetErrorStringKM(psRGXDestroyRPMFreeListOUT->eError)));
		PVR_ASSERT(0);
		UnlockHandle();
		goto RGXDestroyRPMFreeList_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle();



RGXDestroyRPMFreeList_exit:




	return 0;
}


static IMG_INT
PVRSRVBridgeRGXCreateRPMContext(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCREATERPMCONTEXT *psRGXCreateRPMContextIN,
					  PVRSRV_BRIDGE_OUT_RGXCREATERPMCONTEXT *psRGXCreateRPMContextOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_SERVER_RPM_CONTEXT * psCleanupCookieInt = NULL;
	IMG_HANDLE hSceneHeap = psRGXCreateRPMContextIN->hSceneHeap;
	DEVMEMINT_HEAP * psSceneHeapInt = NULL;
	IMG_HANDLE hRPMPageTableHeap = psRGXCreateRPMContextIN->hRPMPageTableHeap;
	DEVMEMINT_HEAP * psRPMPageTableHeapInt = NULL;
	DEVMEM_MEMDESC * psHWMemDescInt = NULL;


	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevData(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
			!psDeviceNode->pfnCheckDeviceFeature(psDeviceNode, RGX_FEATURE_RAY_TRACING_BIT_MASK))
		{
			psRGXCreateRPMContextOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXCreateRPMContext_exit;
		}
	}



	psRGXCreateRPMContextOUT->hCleanupCookie = NULL;


	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXCreateRPMContextOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psSceneHeapInt,
											hSceneHeap,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP,
											IMG_TRUE);
					if(psRGXCreateRPMContextOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXCreateRPMContext_exit;
					}
				}





				{
					/* Look up the address from the handle */
					psRGXCreateRPMContextOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psRPMPageTableHeapInt,
											hRPMPageTableHeap,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP,
											IMG_TRUE);
					if(psRGXCreateRPMContextOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXCreateRPMContext_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXCreateRPMContextOUT->eError =
		RGXCreateRPMContext(psConnection, OSGetDevData(psConnection),
					&psCleanupCookieInt,
					psRGXCreateRPMContextIN->ui32TotalRPMPages,
					psRGXCreateRPMContextIN->ui32Log2DopplerPageSize,
					psRGXCreateRPMContextIN->sSceneMemoryBaseAddr,
					psRGXCreateRPMContextIN->sDopplerHeapBaseAddr,
					psSceneHeapInt,
					psRGXCreateRPMContextIN->sRPMPageTableBaseAddr,
					psRPMPageTableHeapInt,
					&psHWMemDescInt,
					&psRGXCreateRPMContextOUT->ui32HWFrameData);
	/* Exit early if bridged call fails */
	if(psRGXCreateRPMContextOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateRPMContext_exit;
	}

	/* Lock over handle creation. */
	LockHandle();





	psRGXCreateRPMContextOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,

							&psRGXCreateRPMContextOUT->hCleanupCookie,
							(void *) psCleanupCookieInt,
							PVRSRV_HANDLE_TYPE_RGX_SERVER_RPM_CONTEXT,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,(PFN_HANDLE_RELEASE)&RGXDestroyRPMContext);
	if (psRGXCreateRPMContextOUT->eError != PVRSRV_OK)
	{
		UnlockHandle();
		goto RGXCreateRPMContext_exit;
	}






	psRGXCreateRPMContextOUT->eError = PVRSRVAllocSubHandleUnlocked(psConnection->psHandleBase,

							&psRGXCreateRPMContextOUT->hHWMemDesc,
							(void *) psHWMemDescInt,
							PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,psRGXCreateRPMContextOUT->hCleanupCookie);
	if (psRGXCreateRPMContextOUT->eError != PVRSRV_OK)
	{
		UnlockHandle();
		goto RGXCreateRPMContext_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle();



RGXCreateRPMContext_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psSceneHeapInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hSceneHeap,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP);
						}
				}





				{
					/* Unreference the previously looked up handle */
						if(psRPMPageTableHeapInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hRPMPageTableHeap,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();

	if (psRGXCreateRPMContextOUT->eError != PVRSRV_OK)
	{
		/* Lock over handle creation cleanup. */
		LockHandle();
		if (psRGXCreateRPMContextOUT->hCleanupCookie)
		{


			PVRSRV_ERROR eError = PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
						(IMG_HANDLE) psRGXCreateRPMContextOUT->hCleanupCookie,
						PVRSRV_HANDLE_TYPE_RGX_SERVER_RPM_CONTEXT);
			if ((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY))
			{
				PVR_DPF((PVR_DBG_ERROR,
				        "PVRSRVBridgeRGXCreateRPMContext: %s",
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
			RGXDestroyRPMContext(psCleanupCookieInt);
		}
	}


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXDestroyRPMContext(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXDESTROYRPMCONTEXT *psRGXDestroyRPMContextIN,
					  PVRSRV_BRIDGE_OUT_RGXDESTROYRPMCONTEXT *psRGXDestroyRPMContextOUT,
					 CONNECTION_DATA *psConnection)
{


	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevData(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
			!psDeviceNode->pfnCheckDeviceFeature(psDeviceNode, RGX_FEATURE_RAY_TRACING_BIT_MASK))
		{
			psRGXDestroyRPMContextOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXDestroyRPMContext_exit;
		}
	}







	/* Lock over handle destruction. */
	LockHandle();





	psRGXDestroyRPMContextOUT->eError =
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyRPMContextIN->hCleanupCookie,
					PVRSRV_HANDLE_TYPE_RGX_SERVER_RPM_CONTEXT);
	if ((psRGXDestroyRPMContextOUT->eError != PVRSRV_OK) &&
	    (psRGXDestroyRPMContextOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "PVRSRVBridgeRGXDestroyRPMContext: %s",
		        PVRSRVGetErrorStringKM(psRGXDestroyRPMContextOUT->eError)));
		PVR_ASSERT(0);
		UnlockHandle();
		goto RGXDestroyRPMContext_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle();



RGXDestroyRPMContext_exit:




	return 0;
}


static IMG_INT
PVRSRVBridgeRGXKickRS(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXKICKRS *psRGXKickRSIN,
					  PVRSRV_BRIDGE_OUT_RGXKICKRS *psRGXKickRSOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hRayContext = psRGXKickRSIN->hRayContext;
	RGX_SERVER_RAY_CONTEXT * psRayContextInt = NULL;
	SYNC_PRIMITIVE_BLOCK * *psClientFenceUFOSyncPrimBlockInt = NULL;
	IMG_HANDLE *hClientFenceUFOSyncPrimBlockInt2 = NULL;
	IMG_UINT32 *ui32ClientFenceSyncOffsetInt = NULL;
	IMG_UINT32 *ui32ClientFenceValueInt = NULL;
	SYNC_PRIMITIVE_BLOCK * *psClientUpdateUFOSyncPrimBlockInt = NULL;
	IMG_HANDLE *hClientUpdateUFOSyncPrimBlockInt2 = NULL;
	IMG_UINT32 *ui32ClientUpdateSyncOffsetInt = NULL;
	IMG_UINT32 *ui32ClientUpdateValueInt = NULL;
	IMG_UINT32 *ui32ServerSyncFlagsInt = NULL;
	SERVER_SYNC_PRIMITIVE * *psServerSyncsInt = NULL;
	IMG_HANDLE *hServerSyncsInt2 = NULL;
	IMG_BYTE *psDMCmdInt = NULL;
	IMG_BYTE *psFCDMCmdInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psRGXKickRSIN->ui32ClientFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *)) +
			(psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_HANDLE)) +
			(psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) +
			(psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) +
			(psRGXKickRSIN->ui32ClientUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *)) +
			(psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE)) +
			(psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) +
			(psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) +
			(psRGXKickRSIN->ui32ServerSyncCount * sizeof(IMG_UINT32)) +
			(psRGXKickRSIN->ui32ServerSyncCount * sizeof(SERVER_SYNC_PRIMITIVE *)) +
			(psRGXKickRSIN->ui32ServerSyncCount * sizeof(IMG_HANDLE)) +
			(psRGXKickRSIN->ui32CmdSize * sizeof(IMG_BYTE)) +
			(psRGXKickRSIN->ui32FCCmdSize * sizeof(IMG_BYTE)) +
			0;

	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevData(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
			!psDeviceNode->pfnCheckDeviceFeature(psDeviceNode, RGX_FEATURE_RAY_TRACING_BIT_MASK))
		{
			psRGXKickRSOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXKickRS_exit;
		}
	}




	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psRGXKickRSIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psRGXKickRSIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psRGXKickRSOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXKickRS_exit;
			}
		}
	}

	if (psRGXKickRSIN->ui32ClientFenceCount != 0)
	{
		psClientFenceUFOSyncPrimBlockInt = (SYNC_PRIMITIVE_BLOCK **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickRSIN->ui32ClientFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *);
		hClientFenceUFOSyncPrimBlockInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hClientFenceUFOSyncPrimBlockInt2, psRGXKickRSIN->phClientFenceUFOSyncPrimBlock, psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXKickRSOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickRS_exit;
				}
			}
	if (psRGXKickRSIN->ui32ClientFenceCount != 0)
	{
		ui32ClientFenceSyncOffsetInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32ClientFenceSyncOffsetInt, psRGXKickRSIN->pui32ClientFenceSyncOffset, psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickRSOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickRS_exit;
				}
			}
	if (psRGXKickRSIN->ui32ClientFenceCount != 0)
	{
		ui32ClientFenceValueInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32ClientFenceValueInt, psRGXKickRSIN->pui32ClientFenceValue, psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickRSOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickRS_exit;
				}
			}
	if (psRGXKickRSIN->ui32ClientUpdateCount != 0)
	{
		psClientUpdateUFOSyncPrimBlockInt = (SYNC_PRIMITIVE_BLOCK **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickRSIN->ui32ClientUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *);
		hClientUpdateUFOSyncPrimBlockInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hClientUpdateUFOSyncPrimBlockInt2, psRGXKickRSIN->phClientUpdateUFOSyncPrimBlock, psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXKickRSOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickRS_exit;
				}
			}
	if (psRGXKickRSIN->ui32ClientUpdateCount != 0)
	{
		ui32ClientUpdateSyncOffsetInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32ClientUpdateSyncOffsetInt, psRGXKickRSIN->pui32ClientUpdateSyncOffset, psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickRSOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickRS_exit;
				}
			}
	if (psRGXKickRSIN->ui32ClientUpdateCount != 0)
	{
		ui32ClientUpdateValueInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32ClientUpdateValueInt, psRGXKickRSIN->pui32ClientUpdateValue, psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickRSOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickRS_exit;
				}
			}
	if (psRGXKickRSIN->ui32ServerSyncCount != 0)
	{
		ui32ServerSyncFlagsInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickRSIN->ui32ServerSyncCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickRSIN->ui32ServerSyncCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32ServerSyncFlagsInt, psRGXKickRSIN->pui32ServerSyncFlags, psRGXKickRSIN->ui32ServerSyncCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickRSOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickRS_exit;
				}
			}
	if (psRGXKickRSIN->ui32ServerSyncCount != 0)
	{
		psServerSyncsInt = (SERVER_SYNC_PRIMITIVE **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickRSIN->ui32ServerSyncCount * sizeof(SERVER_SYNC_PRIMITIVE *);
		hServerSyncsInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psRGXKickRSIN->ui32ServerSyncCount * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psRGXKickRSIN->ui32ServerSyncCount * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hServerSyncsInt2, psRGXKickRSIN->phServerSyncs, psRGXKickRSIN->ui32ServerSyncCount * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXKickRSOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickRS_exit;
				}
			}
	if (psRGXKickRSIN->ui32CmdSize != 0)
	{
		psDMCmdInt = (IMG_BYTE*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickRSIN->ui32CmdSize * sizeof(IMG_BYTE);
	}

			/* Copy the data over */
			if (psRGXKickRSIN->ui32CmdSize * sizeof(IMG_BYTE) > 0)
			{
				if ( OSCopyFromUser(NULL, psDMCmdInt, psRGXKickRSIN->psDMCmd, psRGXKickRSIN->ui32CmdSize * sizeof(IMG_BYTE)) != PVRSRV_OK )
				{
					psRGXKickRSOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickRS_exit;
				}
			}
	if (psRGXKickRSIN->ui32FCCmdSize != 0)
	{
		psFCDMCmdInt = (IMG_BYTE*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickRSIN->ui32FCCmdSize * sizeof(IMG_BYTE);
	}

			/* Copy the data over */
			if (psRGXKickRSIN->ui32FCCmdSize * sizeof(IMG_BYTE) > 0)
			{
				if ( OSCopyFromUser(NULL, psFCDMCmdInt, psRGXKickRSIN->psFCDMCmd, psRGXKickRSIN->ui32FCCmdSize * sizeof(IMG_BYTE)) != PVRSRV_OK )
				{
					psRGXKickRSOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickRS_exit;
				}
			}

	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXKickRSOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psRayContextInt,
											hRayContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RAY_CONTEXT,
											IMG_TRUE);
					if(psRGXKickRSOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickRS_exit;
					}
				}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickRSIN->ui32ClientFenceCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickRSOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psClientFenceUFOSyncPrimBlockInt[i],
											hClientFenceUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
											IMG_TRUE);
					if(psRGXKickRSOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickRS_exit;
					}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickRSIN->ui32ClientUpdateCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickRSOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psClientUpdateUFOSyncPrimBlockInt[i],
											hClientUpdateUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
											IMG_TRUE);
					if(psRGXKickRSOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickRS_exit;
					}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickRSIN->ui32ServerSyncCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickRSOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psServerSyncsInt[i],
											hServerSyncsInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE,
											IMG_TRUE);
					if(psRGXKickRSOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickRS_exit;
					}
				}
		}
	}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXKickRSOUT->eError =
		PVRSRVRGXKickRSKM(
					psRayContextInt,
					psRGXKickRSIN->ui32ClientCacheOpSeqNum,
					psRGXKickRSIN->ui32ClientFenceCount,
					psClientFenceUFOSyncPrimBlockInt,
					ui32ClientFenceSyncOffsetInt,
					ui32ClientFenceValueInt,
					psRGXKickRSIN->ui32ClientUpdateCount,
					psClientUpdateUFOSyncPrimBlockInt,
					ui32ClientUpdateSyncOffsetInt,
					ui32ClientUpdateValueInt,
					psRGXKickRSIN->ui32ServerSyncCount,
					ui32ServerSyncFlagsInt,
					psServerSyncsInt,
					psRGXKickRSIN->ui32CmdSize,
					psDMCmdInt,
					psRGXKickRSIN->ui32FCCmdSize,
					psFCDMCmdInt,
					psRGXKickRSIN->ui32FrameContext,
					psRGXKickRSIN->ui32PDumpFlags,
					psRGXKickRSIN->ui32ExtJobRef);




RGXKickRS_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psRayContextInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hRayContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RAY_CONTEXT);
						}
				}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickRSIN->ui32ClientFenceCount;i++)
		{
				{
					/* Unreference the previously looked up handle */
						if(psClientFenceUFOSyncPrimBlockInt[i])
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hClientFenceUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
						}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickRSIN->ui32ClientUpdateCount;i++)
		{
				{
					/* Unreference the previously looked up handle */
						if(psClientUpdateUFOSyncPrimBlockInt[i])
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hClientUpdateUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
						}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickRSIN->ui32ServerSyncCount;i++)
		{
				{
					/* Unreference the previously looked up handle */
						if(psServerSyncsInt[i])
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hServerSyncsInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
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
PVRSRVBridgeRGXKickVRDM(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXKICKVRDM *psRGXKickVRDMIN,
					  PVRSRV_BRIDGE_OUT_RGXKICKVRDM *psRGXKickVRDMOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hRayContext = psRGXKickVRDMIN->hRayContext;
	RGX_SERVER_RAY_CONTEXT * psRayContextInt = NULL;
	SYNC_PRIMITIVE_BLOCK * *psClientFenceUFOSyncPrimBlockInt = NULL;
	IMG_HANDLE *hClientFenceUFOSyncPrimBlockInt2 = NULL;
	IMG_UINT32 *ui32ClientFenceSyncOffsetInt = NULL;
	IMG_UINT32 *ui32ClientFenceValueInt = NULL;
	SYNC_PRIMITIVE_BLOCK * *psClientUpdateUFOSyncPrimBlockInt = NULL;
	IMG_HANDLE *hClientUpdateUFOSyncPrimBlockInt2 = NULL;
	IMG_UINT32 *ui32ClientUpdateSyncOffsetInt = NULL;
	IMG_UINT32 *ui32ClientUpdateValueInt = NULL;
	IMG_UINT32 *ui32ServerSyncFlagsInt = NULL;
	SERVER_SYNC_PRIMITIVE * *psServerSyncsInt = NULL;
	IMG_HANDLE *hServerSyncsInt2 = NULL;
	IMG_BYTE *psDMCmdInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *)) +
			(psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_HANDLE)) +
			(psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) +
			(psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) +
			(psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *)) +
			(psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE)) +
			(psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) +
			(psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) +
			(psRGXKickVRDMIN->ui32ServerSyncCount * sizeof(IMG_UINT32)) +
			(psRGXKickVRDMIN->ui32ServerSyncCount * sizeof(SERVER_SYNC_PRIMITIVE *)) +
			(psRGXKickVRDMIN->ui32ServerSyncCount * sizeof(IMG_HANDLE)) +
			(psRGXKickVRDMIN->ui32CmdSize * sizeof(IMG_BYTE)) +
			0;

	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevData(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
			!psDeviceNode->pfnCheckDeviceFeature(psDeviceNode, RGX_FEATURE_RAY_TRACING_BIT_MASK))
		{
			psRGXKickVRDMOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXKickVRDM_exit;
		}
	}




	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psRGXKickVRDMIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psRGXKickVRDMIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psRGXKickVRDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXKickVRDM_exit;
			}
		}
	}

	if (psRGXKickVRDMIN->ui32ClientFenceCount != 0)
	{
		psClientFenceUFOSyncPrimBlockInt = (SYNC_PRIMITIVE_BLOCK **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *);
		hClientFenceUFOSyncPrimBlockInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hClientFenceUFOSyncPrimBlockInt2, psRGXKickVRDMIN->phClientFenceUFOSyncPrimBlock, psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXKickVRDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickVRDM_exit;
				}
			}
	if (psRGXKickVRDMIN->ui32ClientFenceCount != 0)
	{
		ui32ClientFenceSyncOffsetInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32ClientFenceSyncOffsetInt, psRGXKickVRDMIN->pui32ClientFenceSyncOffset, psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickVRDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickVRDM_exit;
				}
			}
	if (psRGXKickVRDMIN->ui32ClientFenceCount != 0)
	{
		ui32ClientFenceValueInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32ClientFenceValueInt, psRGXKickVRDMIN->pui32ClientFenceValue, psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickVRDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickVRDM_exit;
				}
			}
	if (psRGXKickVRDMIN->ui32ClientUpdateCount != 0)
	{
		psClientUpdateUFOSyncPrimBlockInt = (SYNC_PRIMITIVE_BLOCK **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *);
		hClientUpdateUFOSyncPrimBlockInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hClientUpdateUFOSyncPrimBlockInt2, psRGXKickVRDMIN->phClientUpdateUFOSyncPrimBlock, psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXKickVRDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickVRDM_exit;
				}
			}
	if (psRGXKickVRDMIN->ui32ClientUpdateCount != 0)
	{
		ui32ClientUpdateSyncOffsetInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32ClientUpdateSyncOffsetInt, psRGXKickVRDMIN->pui32ClientUpdateSyncOffset, psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickVRDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickVRDM_exit;
				}
			}
	if (psRGXKickVRDMIN->ui32ClientUpdateCount != 0)
	{
		ui32ClientUpdateValueInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32ClientUpdateValueInt, psRGXKickVRDMIN->pui32ClientUpdateValue, psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickVRDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickVRDM_exit;
				}
			}
	if (psRGXKickVRDMIN->ui32ServerSyncCount != 0)
	{
		ui32ServerSyncFlagsInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickVRDMIN->ui32ServerSyncCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickVRDMIN->ui32ServerSyncCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32ServerSyncFlagsInt, psRGXKickVRDMIN->pui32ServerSyncFlags, psRGXKickVRDMIN->ui32ServerSyncCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickVRDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickVRDM_exit;
				}
			}
	if (psRGXKickVRDMIN->ui32ServerSyncCount != 0)
	{
		psServerSyncsInt = (SERVER_SYNC_PRIMITIVE **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickVRDMIN->ui32ServerSyncCount * sizeof(SERVER_SYNC_PRIMITIVE *);
		hServerSyncsInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psRGXKickVRDMIN->ui32ServerSyncCount * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psRGXKickVRDMIN->ui32ServerSyncCount * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hServerSyncsInt2, psRGXKickVRDMIN->phServerSyncs, psRGXKickVRDMIN->ui32ServerSyncCount * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXKickVRDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickVRDM_exit;
				}
			}
	if (psRGXKickVRDMIN->ui32CmdSize != 0)
	{
		psDMCmdInt = (IMG_BYTE*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickVRDMIN->ui32CmdSize * sizeof(IMG_BYTE);
	}

			/* Copy the data over */
			if (psRGXKickVRDMIN->ui32CmdSize * sizeof(IMG_BYTE) > 0)
			{
				if ( OSCopyFromUser(NULL, psDMCmdInt, psRGXKickVRDMIN->psDMCmd, psRGXKickVRDMIN->ui32CmdSize * sizeof(IMG_BYTE)) != PVRSRV_OK )
				{
					psRGXKickVRDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickVRDM_exit;
				}
			}

	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXKickVRDMOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psRayContextInt,
											hRayContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RAY_CONTEXT,
											IMG_TRUE);
					if(psRGXKickVRDMOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickVRDM_exit;
					}
				}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickVRDMIN->ui32ClientFenceCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickVRDMOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psClientFenceUFOSyncPrimBlockInt[i],
											hClientFenceUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
											IMG_TRUE);
					if(psRGXKickVRDMOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickVRDM_exit;
					}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickVRDMIN->ui32ClientUpdateCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickVRDMOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psClientUpdateUFOSyncPrimBlockInt[i],
											hClientUpdateUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
											IMG_TRUE);
					if(psRGXKickVRDMOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickVRDM_exit;
					}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickVRDMIN->ui32ServerSyncCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickVRDMOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psServerSyncsInt[i],
											hServerSyncsInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE,
											IMG_TRUE);
					if(psRGXKickVRDMOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickVRDM_exit;
					}
				}
		}
	}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXKickVRDMOUT->eError =
		PVRSRVRGXKickVRDMKM(
					psRayContextInt,
					psRGXKickVRDMIN->ui32ClientCacheOpSeqNum,
					psRGXKickVRDMIN->ui32ClientFenceCount,
					psClientFenceUFOSyncPrimBlockInt,
					ui32ClientFenceSyncOffsetInt,
					ui32ClientFenceValueInt,
					psRGXKickVRDMIN->ui32ClientUpdateCount,
					psClientUpdateUFOSyncPrimBlockInt,
					ui32ClientUpdateSyncOffsetInt,
					ui32ClientUpdateValueInt,
					psRGXKickVRDMIN->ui32ServerSyncCount,
					ui32ServerSyncFlagsInt,
					psServerSyncsInt,
					psRGXKickVRDMIN->ui32CmdSize,
					psDMCmdInt,
					psRGXKickVRDMIN->ui32PDumpFlags,
					psRGXKickVRDMIN->ui32ExtJobRef);




RGXKickVRDM_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psRayContextInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hRayContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RAY_CONTEXT);
						}
				}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickVRDMIN->ui32ClientFenceCount;i++)
		{
				{
					/* Unreference the previously looked up handle */
						if(psClientFenceUFOSyncPrimBlockInt[i])
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hClientFenceUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
						}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickVRDMIN->ui32ClientUpdateCount;i++)
		{
				{
					/* Unreference the previously looked up handle */
						if(psClientUpdateUFOSyncPrimBlockInt[i])
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hClientUpdateUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
						}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickVRDMIN->ui32ServerSyncCount;i++)
		{
				{
					/* Unreference the previously looked up handle */
						if(psServerSyncsInt[i])
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hServerSyncsInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
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
PVRSRVBridgeRGXCreateRayContext(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCREATERAYCONTEXT *psRGXCreateRayContextIN,
					  PVRSRV_BRIDGE_OUT_RGXCREATERAYCONTEXT *psRGXCreateRayContextOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_BYTE *psFrameworkCmdInt = NULL;
	IMG_HANDLE hPrivData = psRGXCreateRayContextIN->hPrivData;
	IMG_HANDLE hPrivDataInt = NULL;
	RGX_SERVER_RAY_CONTEXT * psRayContextInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psRGXCreateRayContextIN->ui32FrameworkCmdSize * sizeof(IMG_BYTE)) +
			0;

	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevData(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
			!psDeviceNode->pfnCheckDeviceFeature(psDeviceNode, RGX_FEATURE_RAY_TRACING_BIT_MASK))
		{
			psRGXCreateRayContextOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXCreateRayContext_exit;
		}
	}




	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psRGXCreateRayContextIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psRGXCreateRayContextIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psRGXCreateRayContextOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXCreateRayContext_exit;
			}
		}
	}

	if (psRGXCreateRayContextIN->ui32FrameworkCmdSize != 0)
	{
		psFrameworkCmdInt = (IMG_BYTE*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXCreateRayContextIN->ui32FrameworkCmdSize * sizeof(IMG_BYTE);
	}

			/* Copy the data over */
			if (psRGXCreateRayContextIN->ui32FrameworkCmdSize * sizeof(IMG_BYTE) > 0)
			{
				if ( OSCopyFromUser(NULL, psFrameworkCmdInt, psRGXCreateRayContextIN->psFrameworkCmd, psRGXCreateRayContextIN->ui32FrameworkCmdSize * sizeof(IMG_BYTE)) != PVRSRV_OK )
				{
					psRGXCreateRayContextOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXCreateRayContext_exit;
				}
			}

	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXCreateRayContextOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &hPrivDataInt,
											hPrivData,
											PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA,
											IMG_TRUE);
					if(psRGXCreateRayContextOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXCreateRayContext_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXCreateRayContextOUT->eError =
		PVRSRVRGXCreateRayContextKM(psConnection, OSGetDevData(psConnection),
					psRGXCreateRayContextIN->ui32Priority,
					psRGXCreateRayContextIN->sMCUFenceAddr,
					psRGXCreateRayContextIN->sVRMCallStackAddr,
					psRGXCreateRayContextIN->ui32FrameworkCmdSize,
					psFrameworkCmdInt,
					hPrivDataInt,
					&psRayContextInt);
	/* Exit early if bridged call fails */
	if(psRGXCreateRayContextOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateRayContext_exit;
	}

	/* Lock over handle creation. */
	LockHandle();





	psRGXCreateRayContextOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,

							&psRGXCreateRayContextOUT->hRayContext,
							(void *) psRayContextInt,
							PVRSRV_HANDLE_TYPE_RGX_SERVER_RAY_CONTEXT,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&PVRSRVRGXDestroyRayContextKM);
	if (psRGXCreateRayContextOUT->eError != PVRSRV_OK)
	{
		UnlockHandle();
		goto RGXCreateRayContext_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle();



RGXCreateRayContext_exit:

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

	if (psRGXCreateRayContextOUT->eError != PVRSRV_OK)
	{
		if (psRayContextInt)
		{
			PVRSRVRGXDestroyRayContextKM(psRayContextInt);
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
PVRSRVBridgeRGXDestroyRayContext(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXDESTROYRAYCONTEXT *psRGXDestroyRayContextIN,
					  PVRSRV_BRIDGE_OUT_RGXDESTROYRAYCONTEXT *psRGXDestroyRayContextOUT,
					 CONNECTION_DATA *psConnection)
{


	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevData(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
			!psDeviceNode->pfnCheckDeviceFeature(psDeviceNode, RGX_FEATURE_RAY_TRACING_BIT_MASK))
		{
			psRGXDestroyRayContextOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXDestroyRayContext_exit;
		}
	}







	/* Lock over handle destruction. */
	LockHandle();





	psRGXDestroyRayContextOUT->eError =
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyRayContextIN->hRayContext,
					PVRSRV_HANDLE_TYPE_RGX_SERVER_RAY_CONTEXT);
	if ((psRGXDestroyRayContextOUT->eError != PVRSRV_OK) &&
	    (psRGXDestroyRayContextOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "PVRSRVBridgeRGXDestroyRayContext: %s",
		        PVRSRVGetErrorStringKM(psRGXDestroyRayContextOUT->eError)));
		PVR_ASSERT(0);
		UnlockHandle();
		goto RGXDestroyRayContext_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle();



RGXDestroyRayContext_exit:




	return 0;
}


static IMG_INT
PVRSRVBridgeRGXSetRayContextPriority(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXSETRAYCONTEXTPRIORITY *psRGXSetRayContextPriorityIN,
					  PVRSRV_BRIDGE_OUT_RGXSETRAYCONTEXTPRIORITY *psRGXSetRayContextPriorityOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hRayContext = psRGXSetRayContextPriorityIN->hRayContext;
	RGX_SERVER_RAY_CONTEXT * psRayContextInt = NULL;


	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevData(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
			!psDeviceNode->pfnCheckDeviceFeature(psDeviceNode, RGX_FEATURE_RAY_TRACING_BIT_MASK))
		{
			psRGXSetRayContextPriorityOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXSetRayContextPriority_exit;
		}
	}





	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXSetRayContextPriorityOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psRayContextInt,
											hRayContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RAY_CONTEXT,
											IMG_TRUE);
					if(psRGXSetRayContextPriorityOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXSetRayContextPriority_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXSetRayContextPriorityOUT->eError =
		PVRSRVRGXSetRayContextPriorityKM(psConnection, OSGetDevData(psConnection),
					psRayContextInt,
					psRGXSetRayContextPriorityIN->ui32Priority);




RGXSetRayContextPriority_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psRayContextInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hRayContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RAY_CONTEXT);
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

PVRSRV_ERROR InitRGXRAYBridge(void);
PVRSRV_ERROR DeinitRGXRAYBridge(void);

/*
 * Register all RGXRAY functions with services
 */
PVRSRV_ERROR InitRGXRAYBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXRAY, PVRSRV_BRIDGE_RGXRAY_RGXCREATERPMFREELIST, PVRSRVBridgeRGXCreateRPMFreeList,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXRAY, PVRSRV_BRIDGE_RGXRAY_RGXDESTROYRPMFREELIST, PVRSRVBridgeRGXDestroyRPMFreeList,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXRAY, PVRSRV_BRIDGE_RGXRAY_RGXCREATERPMCONTEXT, PVRSRVBridgeRGXCreateRPMContext,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXRAY, PVRSRV_BRIDGE_RGXRAY_RGXDESTROYRPMCONTEXT, PVRSRVBridgeRGXDestroyRPMContext,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXRAY, PVRSRV_BRIDGE_RGXRAY_RGXKICKRS, PVRSRVBridgeRGXKickRS,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXRAY, PVRSRV_BRIDGE_RGXRAY_RGXKICKVRDM, PVRSRVBridgeRGXKickVRDM,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXRAY, PVRSRV_BRIDGE_RGXRAY_RGXCREATERAYCONTEXT, PVRSRVBridgeRGXCreateRayContext,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXRAY, PVRSRV_BRIDGE_RGXRAY_RGXDESTROYRAYCONTEXT, PVRSRVBridgeRGXDestroyRayContext,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXRAY, PVRSRV_BRIDGE_RGXRAY_RGXSETRAYCONTEXTPRIORITY, PVRSRVBridgeRGXSetRayContextPriority,
					NULL, bUseLock);


	return PVRSRV_OK;
}

/*
 * Unregister all rgxray functions with services
 */
PVRSRV_ERROR DeinitRGXRAYBridge(void)
{
	return PVRSRV_OK;
}
