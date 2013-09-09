/*************************************************************************/ /*!
@Title          PVR Common Bridge Module (kernel side)
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Receives calls from the user portion of services and
                despatches them to functions in the kernel portion.
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

#include "img_defs.h"
#include "services.h"
#include "pvr_bridge_km.h"
#include "pvr_debug.h"
#include "ra.h"
#include "pvr_bridge.h"
#if defined(SUPPORT_SGX)
#include "sgx_bridge.h"
#endif
#if defined(SUPPORT_VGX)
#include "vgx_bridge.h"
#endif
#if defined(SUPPORT_MSVDX)
#include "msvdx_bridge.h"
#endif
#include "perproc.h"
#include "device.h"
#include "buffer_manager.h"
#include "refcount.h"

#include "pdump_km.h"
#include "syscommon.h"

#include "bridged_pvr_bridge.h"
#if defined(SUPPORT_SGX)
#include "bridged_sgx_bridge.h"
#endif
#if defined(SUPPORT_VGX)
#include "bridged_vgx_bridge.h"
#endif
#if defined(SUPPORT_MSVDX)
#include "bridged_msvdx_bridge.h"
#endif

#include "env_data.h"

#if defined (__linux__) || defined(__QNXNTO__)
#include "mmap.h"
#endif


#include "srvkm.h"

/* FIXME: we should include an OS specific header here to allow configuration of
 * which functions should be excluded (like the shared srvclient bridge code)
 * so that ports may choose to override certain things. */

/* For the purpose of maintainability, it is intended that this file should not
 * contain large amounts of OS specific #ifdefs. Headers are fine, and perhaps
 * a few one liners, but for anything more, please find a way to add e.g.
 * an osfunc.c abstraction or override the entire function in question within
 * env,*,pvr_bridge_k.c
 */


PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY g_BridgeDispatchTable[BRIDGE_DISPATCH_TABLE_ENTRY_COUNT];

#if defined(DEBUG_BRIDGE_KM)
PVRSRV_BRIDGE_GLOBAL_STATS g_BridgeGlobalStats;
#endif

#if defined(PVR_SECURE_HANDLES)
static IMG_BOOL abSharedDeviceMemHeap[PVRSRV_MAX_CLIENT_HEAPS];
static IMG_BOOL *pbSharedDeviceMemHeap = abSharedDeviceMemHeap;
#else
static IMG_BOOL *pbSharedDeviceMemHeap = (IMG_BOOL*)IMG_NULL;
#endif


#if defined(DEBUG_BRIDGE_KM)
PVRSRV_ERROR
CopyFromUserWrapper(PVRSRV_PER_PROCESS_DATA *pProcData,
					IMG_UINT32 ui32BridgeID,
					IMG_VOID *pvDest,
					IMG_VOID *pvSrc,
					IMG_UINT32 ui32Size)
{
	g_BridgeDispatchTable[ui32BridgeID].ui32CopyFromUserTotalBytes+=ui32Size;
	g_BridgeGlobalStats.ui32TotalCopyFromUserBytes+=ui32Size;
	return OSCopyFromUser(pProcData, pvDest, pvSrc, ui32Size);
}
PVRSRV_ERROR
CopyToUserWrapper(PVRSRV_PER_PROCESS_DATA *pProcData,
				  IMG_UINT32 ui32BridgeID,
				  IMG_VOID *pvDest,
				  IMG_VOID *pvSrc,
				  IMG_UINT32 ui32Size)
{
	g_BridgeDispatchTable[ui32BridgeID].ui32CopyToUserTotalBytes+=ui32Size;
	g_BridgeGlobalStats.ui32TotalCopyToUserBytes+=ui32Size;
	return OSCopyToUser(pProcData, pvDest, pvSrc, ui32Size);
}
#endif


static IMG_INT
PVRSRVEnumerateDevicesBW(IMG_UINT32 ui32BridgeID,
						 IMG_VOID *psBridgeIn,
						 PVRSRV_BRIDGE_OUT_ENUMDEVICE *psEnumDeviceOUT,
						 PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_ENUM_DEVICES);

	PVR_UNREFERENCED_PARAMETER(psPerProc);
	PVR_UNREFERENCED_PARAMETER(psBridgeIn);

	psEnumDeviceOUT->eError =
		PVRSRVEnumerateDevicesKM(&psEnumDeviceOUT->ui32NumDevices,
								 psEnumDeviceOUT->asDeviceIdentifier);

	return 0;
}

static IMG_INT
PVRSRVAcquireDeviceDataBW(IMG_UINT32 ui32BridgeID,
						  PVRSRV_BRIDGE_IN_ACQUIRE_DEVICEINFO *psAcquireDevInfoIN,
						  PVRSRV_BRIDGE_OUT_ACQUIRE_DEVICEINFO *psAcquireDevInfoOUT,
						  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_HANDLE hDevCookieInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_ACQUIRE_DEVICEINFO);

	psAcquireDevInfoOUT->eError =
		PVRSRVAcquireDeviceDataKM(psAcquireDevInfoIN->uiDevIndex,
								  psAcquireDevInfoIN->eDeviceType,
								  &hDevCookieInt);
	if(psAcquireDevInfoOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	/*
	 * Handle is not allocated in batch mode, as there is no resource
	 * allocation to undo if the handle allocation fails.
	 */
		psAcquireDevInfoOUT->eError =
		PVRSRVAllocHandle(psPerProc->psHandleBase,
						  &psAcquireDevInfoOUT->hDevCookie,
						  hDevCookieInt,
						  PVRSRV_HANDLE_TYPE_DEV_NODE,
						  PVRSRV_HANDLE_ALLOC_FLAG_SHARED);

	return 0;
}


static IMG_INT
PVRSRVCreateDeviceMemContextBW(IMG_UINT32 ui32BridgeID,
							   PVRSRV_BRIDGE_IN_CREATE_DEVMEMCONTEXT *psCreateDevMemContextIN,
							   PVRSRV_BRIDGE_OUT_CREATE_DEVMEMCONTEXT *psCreateDevMemContextOUT,
							   PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_HANDLE hDevCookieInt;
	IMG_HANDLE hDevMemContextInt;
	IMG_UINT32 i;
	IMG_BOOL bCreated;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_CREATE_DEVMEMCONTEXT);

	/*
	 * We potentially need one handle for the device memory context,
	 * and one handle for each client heap.
	 */
	NEW_HANDLE_BATCH_OR_ERROR(psCreateDevMemContextOUT->eError, psPerProc, PVRSRV_MAX_CLIENT_HEAPS + 1)

	psCreateDevMemContextOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevCookieInt,
						   psCreateDevMemContextIN->hDevCookie,
						   PVRSRV_HANDLE_TYPE_DEV_NODE);

	if(psCreateDevMemContextOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psCreateDevMemContextOUT->eError =
		PVRSRVCreateDeviceMemContextKM(hDevCookieInt,
									   psPerProc,
									   &hDevMemContextInt,
									   &psCreateDevMemContextOUT->ui32ClientHeapCount,
									   &psCreateDevMemContextOUT->sHeapInfo[0],
									   &bCreated,
									   pbSharedDeviceMemHeap);

	if(psCreateDevMemContextOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	/*
	 * Only allocate a handle if the device memory context was created.
	 * If an existing context was returned, lookup the existing
	 * handle.
	 */
	if(bCreated)
	{
		PVRSRVAllocHandleNR(psPerProc->psHandleBase,
						  &psCreateDevMemContextOUT->hDevMemContext,
						  hDevMemContextInt,
						  PVRSRV_HANDLE_TYPE_DEV_MEM_CONTEXT,
						  PVRSRV_HANDLE_ALLOC_FLAG_NONE);
	}
	else
	{
		psCreateDevMemContextOUT->eError =
			PVRSRVFindHandle(psPerProc->psHandleBase,
							 &psCreateDevMemContextOUT->hDevMemContext,
							 hDevMemContextInt,
							 PVRSRV_HANDLE_TYPE_DEV_MEM_CONTEXT);
		if(psCreateDevMemContextOUT->eError != PVRSRV_OK)
		{
			return 0;
		}
	}

	for(i = 0; i < psCreateDevMemContextOUT->ui32ClientHeapCount; i++)
	{
		IMG_HANDLE hDevMemHeapExt;

#if defined(PVR_SECURE_HANDLES)
		if(abSharedDeviceMemHeap[i])
#endif
		{
			/*
			 * Heaps shared by everybody.  These heaps are not
			 * created as part of the device memory context
			 * creation, and exist for the lifetime of the
			 * driver, hence, we use shared handles for these
			 * heaps.
			 */
			PVRSRVAllocHandleNR(psPerProc->psHandleBase, &hDevMemHeapExt,
								psCreateDevMemContextOUT->sHeapInfo[i].hDevMemHeap,
								PVRSRV_HANDLE_TYPE_DEV_MEM_HEAP,
								PVRSRV_HANDLE_ALLOC_FLAG_SHARED);
		}
#if defined(PVR_SECURE_HANDLES)
		else
		{
			/*
			 * Heaps belonging to this context.  The handles for
			 * these are made subhandles of the memory context
			 * handle, so that they are automatically deallocated
			 * when the memory context handle is deallocated.
			 */
			if(bCreated)
			{
				PVRSRVAllocSubHandleNR(psPerProc->psHandleBase, &hDevMemHeapExt,
									 psCreateDevMemContextOUT->sHeapInfo[i].hDevMemHeap,
									 PVRSRV_HANDLE_TYPE_DEV_MEM_HEAP,
									 PVRSRV_HANDLE_ALLOC_FLAG_NONE,
									 psCreateDevMemContextOUT->hDevMemContext);
			}
			else
			{
				psCreateDevMemContextOUT->eError =
					PVRSRVFindHandle(psPerProc->psHandleBase,
									 &hDevMemHeapExt,
									 psCreateDevMemContextOUT->sHeapInfo[i].hDevMemHeap,
									 PVRSRV_HANDLE_TYPE_DEV_MEM_HEAP);
				if(psCreateDevMemContextOUT->eError != PVRSRV_OK)
				{
					return 0;
				}
			}
		}
#endif
		psCreateDevMemContextOUT->sHeapInfo[i].hDevMemHeap = hDevMemHeapExt;
	}

	COMMIT_HANDLE_BATCH_OR_ERROR(psCreateDevMemContextOUT->eError, psPerProc)

	return 0;
}

static IMG_INT
PVRSRVDestroyDeviceMemContextBW(IMG_UINT32 ui32BridgeID,
								PVRSRV_BRIDGE_IN_DESTROY_DEVMEMCONTEXT *psDestroyDevMemContextIN,
								PVRSRV_BRIDGE_RETURN *psRetOUT,
								PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_HANDLE hDevCookieInt;
	IMG_HANDLE hDevMemContextInt;
	IMG_BOOL bDestroyed;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DESTROY_DEVMEMCONTEXT);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevCookieInt,
						   psDestroyDevMemContextIN->hDevCookie,
						   PVRSRV_HANDLE_TYPE_DEV_NODE);

	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevMemContextInt,
						   psDestroyDevMemContextIN->hDevMemContext,
						   PVRSRV_HANDLE_TYPE_DEV_MEM_CONTEXT);

	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PVRSRVDestroyDeviceMemContextKM(hDevCookieInt, hDevMemContextInt, &bDestroyed);

	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	if(bDestroyed)
	{
		psRetOUT->eError =
			PVRSRVReleaseHandle(psPerProc->psHandleBase,
								psDestroyDevMemContextIN->hDevMemContext,
								PVRSRV_HANDLE_TYPE_DEV_MEM_CONTEXT);
	}

	return 0;
}


static IMG_INT
PVRSRVGetDeviceMemHeapInfoBW(IMG_UINT32 ui32BridgeID,
							   PVRSRV_BRIDGE_IN_GET_DEVMEM_HEAPINFO *psGetDevMemHeapInfoIN,
							   PVRSRV_BRIDGE_OUT_GET_DEVMEM_HEAPINFO *psGetDevMemHeapInfoOUT,
							   PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_HANDLE hDevCookieInt;
	IMG_HANDLE hDevMemContextInt;
	IMG_UINT32 i;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_GET_DEVMEM_HEAPINFO);

	NEW_HANDLE_BATCH_OR_ERROR(psGetDevMemHeapInfoOUT->eError, psPerProc, PVRSRV_MAX_CLIENT_HEAPS)

	psGetDevMemHeapInfoOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevCookieInt,
						   psGetDevMemHeapInfoIN->hDevCookie,
						   PVRSRV_HANDLE_TYPE_DEV_NODE);

	if(psGetDevMemHeapInfoOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psGetDevMemHeapInfoOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevMemContextInt,
						   psGetDevMemHeapInfoIN->hDevMemContext,
						   PVRSRV_HANDLE_TYPE_DEV_MEM_CONTEXT);

	if(psGetDevMemHeapInfoOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psGetDevMemHeapInfoOUT->eError =
		PVRSRVGetDeviceMemHeapInfoKM(hDevCookieInt,
									   hDevMemContextInt,
									   &psGetDevMemHeapInfoOUT->ui32ClientHeapCount,
									   &psGetDevMemHeapInfoOUT->sHeapInfo[0],
									   pbSharedDeviceMemHeap);

	if(psGetDevMemHeapInfoOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	for(i = 0; i < psGetDevMemHeapInfoOUT->ui32ClientHeapCount; i++)
	{
		IMG_HANDLE hDevMemHeapExt;

#if defined(PVR_SECURE_HANDLES)
		if(abSharedDeviceMemHeap[i])
#endif
		{
			/*
			 * Heaps shared by everybody.  These heaps are not
			 * created as part of the device memory context
			 * creation, and exist for the lifetime of the
			 * driver, hence, we use shared handles for these
			 * heaps.
			 */
			PVRSRVAllocHandleNR(psPerProc->psHandleBase, &hDevMemHeapExt,
							  psGetDevMemHeapInfoOUT->sHeapInfo[i].hDevMemHeap,
							  PVRSRV_HANDLE_TYPE_DEV_MEM_HEAP,
							  PVRSRV_HANDLE_ALLOC_FLAG_SHARED);
		}
#if defined(PVR_SECURE_HANDLES)
		else
		{
			/*
			 * Heaps belonging to this context.  The handles for
			 * these are made subhandles of the memory context
			 * handle, so that they are automatically deallocated
			 * when the memory context handle is deallocated.
			 */
			psGetDevMemHeapInfoOUT->eError =
				PVRSRVFindHandle(psPerProc->psHandleBase,
								 &hDevMemHeapExt,
								 psGetDevMemHeapInfoOUT->sHeapInfo[i].hDevMemHeap,
								 PVRSRV_HANDLE_TYPE_DEV_MEM_HEAP);
			if(psGetDevMemHeapInfoOUT->eError != PVRSRV_OK)
			{
				return 0;
			}
		}
#endif
		psGetDevMemHeapInfoOUT->sHeapInfo[i].hDevMemHeap = hDevMemHeapExt;
	}

	COMMIT_HANDLE_BATCH_OR_ERROR(psGetDevMemHeapInfoOUT->eError, psPerProc)

	return 0;
}


#if defined(OS_PVRSRV_ALLOC_DEVICE_MEM_BW)
/* customised version */
IMG_INT
PVRSRVAllocDeviceMemBW(IMG_UINT32 ui32BridgeID,
					   PVRSRV_BRIDGE_IN_ALLOCDEVICEMEM *psAllocDeviceMemIN,
					   PVRSRV_BRIDGE_OUT_ALLOCDEVICEMEM *psAllocDeviceMemOUT,
					   PVRSRV_PER_PROCESS_DATA *psPerProc);
#else
static IMG_INT
PVRSRVAllocDeviceMemBW(IMG_UINT32 ui32BridgeID,
					   PVRSRV_BRIDGE_IN_ALLOCDEVICEMEM *psAllocDeviceMemIN,
					   PVRSRV_BRIDGE_OUT_ALLOCDEVICEMEM *psAllocDeviceMemOUT,
					   PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_KERNEL_MEM_INFO *psMemInfo;
	IMG_HANDLE hDevCookieInt;
	IMG_HANDLE hDevMemHeapInt;
	IMG_UINT32 ui32ShareIndex;
	IMG_BOOL bUseShareMemWorkaround;
	IMG_BOOL *pabMapChunk = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_ALLOC_DEVICEMEM);

	NEW_HANDLE_BATCH_OR_ERROR(psAllocDeviceMemOUT->eError, psPerProc, 2)

	/* Do same sanity checking */
	if (psAllocDeviceMemIN->ui32Attribs & PVRSRV_MEM_SPARSE)
	{
		if (psAllocDeviceMemIN->ui32NumPhysChunks > psAllocDeviceMemIN->ui32NumVirtChunks)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocDeviceMemBW: more physical chunks then virtual space"));
			psAllocDeviceMemOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;
			return 0;
		}
	
		if (psAllocDeviceMemIN->pabMapChunk == IMG_NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocDeviceMemBW: Called in sparse mapping mode but without MapChunk array"));
			psAllocDeviceMemOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;
			return 0;
		}
	}

	psAllocDeviceMemOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevCookieInt,
						   psAllocDeviceMemIN->hDevCookie,
						   PVRSRV_HANDLE_TYPE_DEV_NODE);

	if(psAllocDeviceMemOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psAllocDeviceMemOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevMemHeapInt,
						   psAllocDeviceMemIN->hDevMemHeap,
						   PVRSRV_HANDLE_TYPE_DEV_MEM_HEAP);

	if(psAllocDeviceMemOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	/* Memory sharing workaround, version 2 */

	bUseShareMemWorkaround = ((psAllocDeviceMemIN->ui32Attribs & PVRSRV_MEM_XPROC) != 0) ? IMG_TRUE : IMG_FALSE;
	ui32ShareIndex = 7654321; /* stops MSVC compiler warning */

	if (bUseShareMemWorkaround)
	{
		/* allocate a shared-surface ID, prior to the call to AllocDeviceMem */
		/* We could plumb in an extra argument, but for now, we'll keep the
		   shared-surface ID as a piece of global state, and rely upon the
		   bridge mutex to make it safe for us */

		psAllocDeviceMemOUT->eError =
			BM_XProcWorkaroundFindNewBufferAndSetShareIndex(&ui32ShareIndex);
		if(psAllocDeviceMemOUT->eError != PVRSRV_OK)
		{
			return 0;
		}
	}

	/* Check access to private data, if provided */
	if(psAllocDeviceMemIN->pvPrivData)
	{
		if(!OSAccessOK(PVR_VERIFY_READ,
					   psAllocDeviceMemIN->pvPrivData,
					   psAllocDeviceMemIN->ui32PrivDataLength))
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocDeviceMemBW: Access check failed for pvPrivData"));
			return -EFAULT;
		}
	}

	if (psAllocDeviceMemIN->ui32Attribs & PVRSRV_MEM_SPARSE)
	{
		/* Check access to the sparse mapping table, if provided */
		if(!OSAccessOK(PVR_VERIFY_READ,
					   psAllocDeviceMemIN->pabMapChunk,
					   psAllocDeviceMemIN->ui32NumVirtChunks))
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocDeviceMemBW: Access check failed for pabMapChunk"));
			return -EFAULT;
		}

		psAllocDeviceMemOUT->eError = OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
												 sizeof(IMG_BOOL) * psAllocDeviceMemIN->ui32NumVirtChunks,
												 (IMG_VOID **) &pabMapChunk,
												 0,
												 "MapChunk kernel copy");
		if (psAllocDeviceMemOUT->eError != PVRSRV_OK)
		{
			return 0;
		}

		psAllocDeviceMemOUT->eError = OSCopyFromUser(psPerProc,
													 pabMapChunk,
													 psAllocDeviceMemIN->pabMapChunk,
													 sizeof(IMG_BOOL) * psAllocDeviceMemIN->ui32NumVirtChunks);
		if (psAllocDeviceMemOUT->eError != PVRSRV_OK)
		{
			OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
					  sizeof(IMG_BOOL) * psAllocDeviceMemIN->ui32NumVirtChunks,
					  pabMapChunk,
					  0);
			return 0;
		}
	}


	psAllocDeviceMemOUT->eError =
		PVRSRVAllocDeviceMemKM(hDevCookieInt,
							   psPerProc,
							   hDevMemHeapInt,
							   psAllocDeviceMemIN->ui32Attribs,
							   psAllocDeviceMemIN->uSize,
							   psAllocDeviceMemIN->uAlignment,
							   psAllocDeviceMemIN->pvPrivData,
							   psAllocDeviceMemIN->ui32PrivDataLength,
							   psAllocDeviceMemIN->ui32ChunkSize,
							   psAllocDeviceMemIN->ui32NumVirtChunks,
							   psAllocDeviceMemIN->ui32NumPhysChunks,
							   pabMapChunk,
							   &psMemInfo,
							   "" /*FIXME: add something meaningful*/);

	if (bUseShareMemWorkaround)
	{
        PVR_ASSERT(ui32ShareIndex != 7654321);
		BM_XProcWorkaroundUnsetShareIndex(ui32ShareIndex);
	}

	if(psAllocDeviceMemOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psMemInfo->sShareMemWorkaround.bInUse = bUseShareMemWorkaround;
	if (bUseShareMemWorkaround)
	{
		PVR_ASSERT(ui32ShareIndex != 7654321);
		psMemInfo->sShareMemWorkaround.ui32ShareIndex = ui32ShareIndex;
		psMemInfo->sShareMemWorkaround.hDevCookieInt = hDevCookieInt;
		psMemInfo->sShareMemWorkaround.ui32OrigReqAttribs = psAllocDeviceMemIN->ui32Attribs;
		psMemInfo->sShareMemWorkaround.ui32OrigReqSize = (IMG_UINT32)psAllocDeviceMemIN->uSize;
		psMemInfo->sShareMemWorkaround.ui32OrigReqAlignment = (IMG_UINT32)psAllocDeviceMemIN->uAlignment;
	}

	OSMemSet(&psAllocDeviceMemOUT->sClientMemInfo,
			 0,
			 sizeof(psAllocDeviceMemOUT->sClientMemInfo));

	psAllocDeviceMemOUT->sClientMemInfo.pvLinAddrKM =
			psMemInfo->pvLinAddrKM;

#if defined (__linux__)
	psAllocDeviceMemOUT->sClientMemInfo.pvLinAddr = 0;
#else
	psAllocDeviceMemOUT->sClientMemInfo.pvLinAddr = psMemInfo->pvLinAddrKM;
#endif
	psAllocDeviceMemOUT->sClientMemInfo.sDevVAddr = psMemInfo->sDevVAddr;
	psAllocDeviceMemOUT->sClientMemInfo.ui32Flags = psMemInfo->ui32Flags;
	psAllocDeviceMemOUT->sClientMemInfo.uAllocSize = psMemInfo->uAllocSize;
	psAllocDeviceMemOUT->sClientMemInfo.hMappingInfo = psMemInfo->sMemBlk.hOSMemHandle;

/* S.LSI */
#if defined (SUPPORT_ION)
	OSMemSet(psAllocDeviceMemOUT->sClientMemInfo.iIonFds, 0, sizeof(IMG_UINT32) * EXYNOS_MAX_YUV_PER_PLANE_ION);
	psAllocDeviceMemOUT->sClientMemInfo.iNumIonFds = 0;
#endif /* SUPPORT_ION */

	PVRSRVAllocHandleNR(psPerProc->psHandleBase,
					  &psAllocDeviceMemOUT->sClientMemInfo.hKernelMemInfo,
					  psMemInfo,
					  PVRSRV_HANDLE_TYPE_MEM_INFO,
					  PVRSRV_HANDLE_ALLOC_FLAG_NONE);


	if(psAllocDeviceMemIN->ui32Attribs & PVRSRV_MEM_NO_SYNCOBJ)
	{
		/* signal no syncinfo */
		OSMemSet(&psAllocDeviceMemOUT->sClientSyncInfo,
				 0,
				 sizeof (PVRSRV_CLIENT_SYNC_INFO));
		psAllocDeviceMemOUT->sClientMemInfo.psClientSyncInfo = IMG_NULL;
	}
	else
	{
		/* and setup the sync info */

#if !defined(PVRSRV_DISABLE_UM_SYNCOBJ_MAPPINGS)
		psAllocDeviceMemOUT->sClientSyncInfo.psSyncData =
			psMemInfo->psKernelSyncInfo->psSyncData;
		psAllocDeviceMemOUT->sClientSyncInfo.sWriteOpsCompleteDevVAddr =
			psMemInfo->psKernelSyncInfo->sWriteOpsCompleteDevVAddr;
		psAllocDeviceMemOUT->sClientSyncInfo.sReadOpsCompleteDevVAddr =
			psMemInfo->psKernelSyncInfo->sReadOpsCompleteDevVAddr;
		psAllocDeviceMemOUT->sClientSyncInfo.sReadOps2CompleteDevVAddr =
			psMemInfo->psKernelSyncInfo->sReadOps2CompleteDevVAddr;

		psAllocDeviceMemOUT->sClientSyncInfo.hMappingInfo =
			psMemInfo->psKernelSyncInfo->psSyncDataMemInfoKM->sMemBlk.hOSMemHandle;
#endif

		PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
							 &psAllocDeviceMemOUT->sClientSyncInfo.hKernelSyncInfo,
							 psMemInfo->psKernelSyncInfo,
							 PVRSRV_HANDLE_TYPE_SYNC_INFO,
							 PVRSRV_HANDLE_ALLOC_FLAG_NONE,
							 psAllocDeviceMemOUT->sClientMemInfo.hKernelMemInfo);

		psAllocDeviceMemOUT->sClientMemInfo.psClientSyncInfo =
			&psAllocDeviceMemOUT->sClientSyncInfo;
	}

	COMMIT_HANDLE_BATCH_OR_ERROR(psAllocDeviceMemOUT->eError, psPerProc)

	return 0;
}

#endif /* OS_PVRSRV_ALLOC_DEVICE_MEM_BW */

static IMG_INT
PVRSRVFreeDeviceMemBW(IMG_UINT32 ui32BridgeID,
					  PVRSRV_BRIDGE_IN_FREEDEVICEMEM *psFreeDeviceMemIN,
					  PVRSRV_BRIDGE_RETURN *psRetOUT,
					  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_HANDLE hDevCookieInt;
	IMG_VOID *pvKernelMemInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_FREE_DEVICEMEM);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevCookieInt,
						   psFreeDeviceMemIN->hDevCookie,
						   PVRSRV_HANDLE_TYPE_DEV_NODE);

	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
        PVRSRVLookupHandle(psPerProc->psHandleBase,
                           &pvKernelMemInfo,
						   psFreeDeviceMemIN->psKernelMemInfo,
						   PVRSRV_HANDLE_TYPE_MEM_INFO);

	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError = PVRSRVFreeDeviceMemKM(hDevCookieInt, pvKernelMemInfo);

	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PVRSRVReleaseHandle(psPerProc->psHandleBase,
							psFreeDeviceMemIN->psKernelMemInfo,
							PVRSRV_HANDLE_TYPE_MEM_INFO);

	return 0;
}


static IMG_INT
PVRSRVExportDeviceMemBW(IMG_UINT32 ui32BridgeID,
					  PVRSRV_BRIDGE_IN_EXPORTDEVICEMEM *psExportDeviceMemIN,
					  PVRSRV_BRIDGE_OUT_EXPORTDEVICEMEM *psExportDeviceMemOUT,
					  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_HANDLE hDevCookieInt;
	PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo;

	PVR_ASSERT(ui32BridgeID == PVRSRV_GET_BRIDGE_ID(PVRSRV_BRIDGE_EXPORT_DEVICEMEM) ||
			   ui32BridgeID == PVRSRV_GET_BRIDGE_ID(PVRSRV_BRIDGE_EXPORT_DEVICEMEM_2));
	PVR_UNREFERENCED_PARAMETER(ui32BridgeID);

	/* find the device cookie */
	psExportDeviceMemOUT->eError =
        PVRSRVLookupHandle(psPerProc->psHandleBase,
                           &hDevCookieInt,
						   psExportDeviceMemIN->hDevCookie,
						   PVRSRV_HANDLE_TYPE_DEV_NODE);

	if(psExportDeviceMemOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVExportDeviceMemBW: can't find devcookie"));
		return 0;
	}

	/* find the kernel meminfo from the process handle list */
	psExportDeviceMemOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   (IMG_PVOID *)&psKernelMemInfo,
						   psExportDeviceMemIN->psKernelMemInfo,
						   PVRSRV_HANDLE_TYPE_MEM_INFO);

	if(psExportDeviceMemOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVExportDeviceMemBW: can't find kernel meminfo"));
		return 0;
	}

	/* see if it's already exported */
	psExportDeviceMemOUT->eError =
		PVRSRVFindHandle(KERNEL_HANDLE_BASE,
							 &psExportDeviceMemOUT->hMemInfo,
							 psKernelMemInfo,
							 PVRSRV_HANDLE_TYPE_MEM_INFO);
	if(psExportDeviceMemOUT->eError == PVRSRV_OK)
	{
		/* it's already exported */
		PVR_DPF((PVR_DBG_MESSAGE, "PVRSRVExportDeviceMemBW: allocation is already exported"));
		return 0;
	}

	/* export the allocation */
	psExportDeviceMemOUT->eError = PVRSRVAllocHandle(KERNEL_HANDLE_BASE,
													&psExportDeviceMemOUT->hMemInfo,
													psKernelMemInfo,
													PVRSRV_HANDLE_TYPE_MEM_INFO,
													PVRSRV_HANDLE_ALLOC_FLAG_NONE);
	if (psExportDeviceMemOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVExportDeviceMemBW: failed to allocate handle from global handle list"));
		return 0;
	}

	/* mark the meminfo as 'exported' */
	psKernelMemInfo->ui32Flags |= PVRSRV_MEM_EXPORTED;

	return 0;
}


static IMG_INT
PVRSRVMapDeviceMemoryBW(IMG_UINT32 ui32BridgeID,
							 PVRSRV_BRIDGE_IN_MAP_DEV_MEMORY *psMapDevMemIN,
							 PVRSRV_BRIDGE_OUT_MAP_DEV_MEMORY *psMapDevMemOUT,
							 PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_KERNEL_MEM_INFO	*psSrcKernelMemInfo = IMG_NULL;
	PVRSRV_KERNEL_MEM_INFO	*psDstKernelMemInfo = IMG_NULL;
	IMG_HANDLE				hDstDevMemHeap = IMG_NULL;

	PVR_ASSERT(ui32BridgeID == PVRSRV_GET_BRIDGE_ID(PVRSRV_BRIDGE_MAP_DEV_MEMORY) ||
			   ui32BridgeID == PVRSRV_GET_BRIDGE_ID(PVRSRV_BRIDGE_MAP_DEV_MEMORY_2));
	PVR_UNREFERENCED_PARAMETER(ui32BridgeID);

	NEW_HANDLE_BATCH_OR_ERROR(psMapDevMemOUT->eError, psPerProc, 2)

	/* lookup srcmeminfo handle */
	psMapDevMemOUT->eError = PVRSRVLookupHandle(KERNEL_HANDLE_BASE,
												(IMG_VOID**)&psSrcKernelMemInfo,
												psMapDevMemIN->hKernelMemInfo,
												PVRSRV_HANDLE_TYPE_MEM_INFO);
	if(psMapDevMemOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	/* lookup dev mem heap handle */
	psMapDevMemOUT->eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
												&hDstDevMemHeap,
												psMapDevMemIN->hDstDevMemHeap,
												PVRSRV_HANDLE_TYPE_DEV_MEM_HEAP);
	if(psMapDevMemOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	/* check for workaround */
	if (psSrcKernelMemInfo->sShareMemWorkaround.bInUse)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "using the mem wrap workaround."));

		/* Ensure we get the same ID for this allocation, such that it
		   inherits the same physical block.  Rather than add a lot of
		   plumbing to several APIs, we call into buffer manager directly
		   to set "global" state.  This works only if we make
		   this allocation while holding the bridge mutex and don't
		   make any other allocations (because the state persists and
		   would affect other device memory allocations too).  It is
		   important that we bracket the PVRSRVAllocDeviceMemKM() call
		   with this Set/Unset pair. */
		psMapDevMemOUT->eError = BM_XProcWorkaroundSetShareIndex(psSrcKernelMemInfo->sShareMemWorkaround.ui32ShareIndex);
		if(psMapDevMemOUT->eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVMapDeviceMemoryBW(): failed to recycle shared buffer"));
			return 0;
		}

		psMapDevMemOUT->eError =
			PVRSRVAllocDeviceMemKM(psSrcKernelMemInfo->sShareMemWorkaround.hDevCookieInt,
								   psPerProc,
								   hDstDevMemHeap,
								   psSrcKernelMemInfo->sShareMemWorkaround.ui32OrigReqAttribs | PVRSRV_MEM_NO_SYNCOBJ,
								   psSrcKernelMemInfo->sShareMemWorkaround.ui32OrigReqSize,
								   psSrcKernelMemInfo->sShareMemWorkaround.ui32OrigReqAlignment,
								   IMG_NULL,
								   0,
								   /* FIXME: Do we need to be able to export sparse memory? */
								   0,0,0,IMG_NULL,	/* No sparse mapping data */
								   &psDstKernelMemInfo,
								   "" /*FIXME: add something meaningful*/);
		/* counterpart of the above "SetShareIndex".  NB: this must be
		   done in both the success and failure paths of the
		   AllocDeviceMemKM() call */
		BM_XProcWorkaroundUnsetShareIndex(psSrcKernelMemInfo->sShareMemWorkaround.ui32ShareIndex);
		if(psMapDevMemOUT->eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVMapDeviceMemoryBW: Failed to create allocation for cross-process memory map"));
			return 0;
		}

		if(psSrcKernelMemInfo->psKernelSyncInfo)
		{
			PVRSRVKernelSyncInfoIncRef(psSrcKernelMemInfo->psKernelSyncInfo, psSrcKernelMemInfo);
		}

		psDstKernelMemInfo->psKernelSyncInfo = psSrcKernelMemInfo->psKernelSyncInfo;
	}
	else
	{
		/* map the meminfo to the target heap and memory context */
		psMapDevMemOUT->eError = PVRSRVMapDeviceMemoryKM(psPerProc,
														 psSrcKernelMemInfo,
														 hDstDevMemHeap,
														 &psDstKernelMemInfo);
		if(psMapDevMemOUT->eError != PVRSRV_OK)
		{
			return 0;
		}
	}

	/* copy the workaround info */
	psDstKernelMemInfo->sShareMemWorkaround = psSrcKernelMemInfo->sShareMemWorkaround;

	OSMemSet(&psMapDevMemOUT->sDstClientMemInfo,
			 0,
			 sizeof(psMapDevMemOUT->sDstClientMemInfo));
	OSMemSet(&psMapDevMemOUT->sDstClientSyncInfo,
			 0,
			 sizeof(psMapDevMemOUT->sDstClientSyncInfo));

	psMapDevMemOUT->sDstClientMemInfo.pvLinAddrKM =
			psDstKernelMemInfo->pvLinAddrKM;

	psMapDevMemOUT->sDstClientMemInfo.pvLinAddr = 0;
	psMapDevMemOUT->sDstClientMemInfo.sDevVAddr = psDstKernelMemInfo->sDevVAddr;
	psMapDevMemOUT->sDstClientMemInfo.ui32Flags = psDstKernelMemInfo->ui32Flags;
    psMapDevMemOUT->sDstClientMemInfo.uAllocSize = psDstKernelMemInfo->uAllocSize;
	psMapDevMemOUT->sDstClientMemInfo.hMappingInfo = psDstKernelMemInfo->sMemBlk.hOSMemHandle;

	/* allocate handle to the DST kernel meminfo */
	PVRSRVAllocHandleNR(psPerProc->psHandleBase,
					  &psMapDevMemOUT->sDstClientMemInfo.hKernelMemInfo,
					  psDstKernelMemInfo,
					  PVRSRV_HANDLE_TYPE_MEM_INFO,
					  PVRSRV_HANDLE_ALLOC_FLAG_NONE);
	psMapDevMemOUT->sDstClientSyncInfo.hKernelSyncInfo = IMG_NULL;


	/* and setup the sync info */
	if(psDstKernelMemInfo->psKernelSyncInfo)
	{
#if !defined(PVRSRV_DISABLE_UM_SYNCOBJ_MAPPINGS)
		psMapDevMemOUT->sDstClientSyncInfo.psSyncData =
			psDstKernelMemInfo->psKernelSyncInfo->psSyncData;
		psMapDevMemOUT->sDstClientSyncInfo.sWriteOpsCompleteDevVAddr =
			psDstKernelMemInfo->psKernelSyncInfo->sWriteOpsCompleteDevVAddr;
		psMapDevMemOUT->sDstClientSyncInfo.sReadOpsCompleteDevVAddr =
			psDstKernelMemInfo->psKernelSyncInfo->sReadOpsCompleteDevVAddr;
		psMapDevMemOUT->sDstClientSyncInfo.sReadOps2CompleteDevVAddr =
			psDstKernelMemInfo->psKernelSyncInfo->sReadOps2CompleteDevVAddr;

		psMapDevMemOUT->sDstClientSyncInfo.hMappingInfo =
			psDstKernelMemInfo->psKernelSyncInfo->psSyncDataMemInfoKM->sMemBlk.hOSMemHandle;
#endif

		psMapDevMemOUT->sDstClientMemInfo.psClientSyncInfo = &psMapDevMemOUT->sDstClientSyncInfo;
		/*
		 * The sync info is associated with the device buffer,
		 * and not allocated here.  It isn't exported when created,
		 * hence the handle allocation rather than a lookup.
		 */
		PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
					  &psMapDevMemOUT->sDstClientSyncInfo.hKernelSyncInfo,
					  psDstKernelMemInfo->psKernelSyncInfo,
					  PVRSRV_HANDLE_TYPE_SYNC_INFO,
					  PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
					  psMapDevMemOUT->sDstClientMemInfo.hKernelMemInfo);
	}

	COMMIT_HANDLE_BATCH_OR_ERROR(psMapDevMemOUT->eError, psPerProc)

	return 0;
}


static IMG_INT
PVRSRVUnmapDeviceMemoryBW(IMG_UINT32 ui32BridgeID,
							 PVRSRV_BRIDGE_IN_UNMAP_DEV_MEMORY *psUnmapDevMemIN,
							 PVRSRV_BRIDGE_RETURN *psRetOUT,
							 PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_KERNEL_MEM_INFO	*psKernelMemInfo = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_UNMAP_DEV_MEMORY);

	psRetOUT->eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
											(IMG_VOID**)&psKernelMemInfo,
											psUnmapDevMemIN->psKernelMemInfo,
											PVRSRV_HANDLE_TYPE_MEM_INFO);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVUnmapDeviceMemoryBW: PVRSRVLookupHandle failed %d", psRetOUT->eError));
		return 0;
	}

	if (psKernelMemInfo->sShareMemWorkaround.bInUse)
	{
		psRetOUT->eError = PVRSRVFreeDeviceMemKM(psKernelMemInfo->sShareMemWorkaround.hDevCookieInt, psKernelMemInfo);
		if(psRetOUT->eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVUnmapDeviceMemoryBW: internal error, should expect FreeDeviceMem to fail"));
			return 0;
		}
	}
	else
	{
		psRetOUT->eError = PVRSRVUnmapDeviceMemoryKM(psKernelMemInfo);
		if(psRetOUT->eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVUnmapDeviceMemoryBW: PVRSRVUnmapDeviceMemoryKM failed %d", psRetOUT->eError));
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVUnmapDeviceMemoryKM: psKernelMemInfo: 0x%x", (unsigned int)psKernelMemInfo));
			return 0;
		}
	}

	psRetOUT->eError = PVRSRVReleaseHandle(psPerProc->psHandleBase,
							psUnmapDevMemIN->psKernelMemInfo,
							PVRSRV_HANDLE_TYPE_MEM_INFO);

	if(psRetOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVUnmapDeviceMemoryBW: PVRSRVReleaseHandle failed %d", psRetOUT->eError));
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVReleaseHandle: psHandleBase: 0x%x, psKernelMemInfo: 0x%x", (unsigned int)psPerProc->psHandleBase, (unsigned int)psUnmapDevMemIN->psKernelMemInfo));
		return 0;
	}
	return 0;
}



static IMG_INT
PVRSRVMapDeviceClassMemoryBW(IMG_UINT32 ui32BridgeID,
							 PVRSRV_BRIDGE_IN_MAP_DEVICECLASS_MEMORY *psMapDevClassMemIN,
							 PVRSRV_BRIDGE_OUT_MAP_DEVICECLASS_MEMORY *psMapDevClassMemOUT,
							 PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_KERNEL_MEM_INFO *psMemInfo;
	IMG_HANDLE hOSMapInfo;
	IMG_HANDLE hDeviceClassBufferInt;
	IMG_HANDLE hDevMemContextInt;
	PVRSRV_HANDLE_TYPE eHandleType;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MAP_DEVICECLASS_MEMORY);

	NEW_HANDLE_BATCH_OR_ERROR(psMapDevClassMemOUT->eError, psPerProc, 2)

	/*
	 * The buffer to be mapped can belong to a 3rd party display or
	 * buffer driver, and we don't know which type we have at this
	 * point.
	 */
	psMapDevClassMemOUT->eError =
        PVRSRVLookupHandleAnyType(psPerProc->psHandleBase,
								  &hDeviceClassBufferInt,
								  &eHandleType,
								  psMapDevClassMemIN->hDeviceClassBuffer);

	if(psMapDevClassMemOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	/* get the device memory context */
	psMapDevClassMemOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
				   &hDevMemContextInt,
				   psMapDevClassMemIN->hDevMemContext,
				   PVRSRV_HANDLE_TYPE_DEV_MEM_CONTEXT);

	if(psMapDevClassMemOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	/* Having looked up the handle, now check its type */
	switch(eHandleType)
	{
#if defined(PVR_SECURE_HANDLES)
		case PVRSRV_HANDLE_TYPE_DISP_BUFFER:
		case PVRSRV_HANDLE_TYPE_BUF_BUFFER:
#else
		case PVRSRV_HANDLE_TYPE_NONE:
#endif
			break;
		default:
			psMapDevClassMemOUT->eError = PVRSRV_ERROR_INVALID_HANDLE_TYPE;
			return 0;
	}

	psMapDevClassMemOUT->eError =
		PVRSRVMapDeviceClassMemoryKM(psPerProc,
									 hDevMemContextInt,
									 hDeviceClassBufferInt,
									 &psMemInfo,
									 &hOSMapInfo);
	if(psMapDevClassMemOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	OSMemSet(&psMapDevClassMemOUT->sClientMemInfo,
			 0,
			 sizeof(psMapDevClassMemOUT->sClientMemInfo));
	OSMemSet(&psMapDevClassMemOUT->sClientSyncInfo,
			 0,
			 sizeof(psMapDevClassMemOUT->sClientSyncInfo));

	psMapDevClassMemOUT->sClientMemInfo.pvLinAddrKM =
			psMemInfo->pvLinAddrKM;

	psMapDevClassMemOUT->sClientMemInfo.pvLinAddr = 0;
	psMapDevClassMemOUT->sClientMemInfo.sDevVAddr = psMemInfo->sDevVAddr;
	psMapDevClassMemOUT->sClientMemInfo.ui32Flags = psMemInfo->ui32Flags;
    psMapDevClassMemOUT->sClientMemInfo.uAllocSize = psMemInfo->uAllocSize;
	psMapDevClassMemOUT->sClientMemInfo.hMappingInfo = psMemInfo->sMemBlk.hOSMemHandle;

	PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
					  &psMapDevClassMemOUT->sClientMemInfo.hKernelMemInfo,
					  psMemInfo,
					  PVRSRV_HANDLE_TYPE_MEM_INFO,
					  PVRSRV_HANDLE_ALLOC_FLAG_NONE,
					  psMapDevClassMemIN->hDeviceClassBuffer);

	psMapDevClassMemOUT->sClientSyncInfo.hKernelSyncInfo = IMG_NULL;

	/* and setup the sync info */
	if(psMemInfo->psKernelSyncInfo)
	{
#if !defined(PVRSRV_DISABLE_UM_SYNCOBJ_MAPPINGS)
		psMapDevClassMemOUT->sClientSyncInfo.psSyncData =
			psMemInfo->psKernelSyncInfo->psSyncData;
		psMapDevClassMemOUT->sClientSyncInfo.sWriteOpsCompleteDevVAddr =
			psMemInfo->psKernelSyncInfo->sWriteOpsCompleteDevVAddr;
		psMapDevClassMemOUT->sClientSyncInfo.sReadOpsCompleteDevVAddr =
			psMemInfo->psKernelSyncInfo->sReadOpsCompleteDevVAddr;
		psMapDevClassMemOUT->sClientSyncInfo.sReadOps2CompleteDevVAddr =
			psMemInfo->psKernelSyncInfo->sReadOps2CompleteDevVAddr;

		psMapDevClassMemOUT->sClientSyncInfo.hMappingInfo =
			psMemInfo->psKernelSyncInfo->psSyncDataMemInfoKM->sMemBlk.hOSMemHandle;
#endif

		psMapDevClassMemOUT->sClientMemInfo.psClientSyncInfo = &psMapDevClassMemOUT->sClientSyncInfo;
		/*
		 * The sync info is associated with the device buffer,
		 * and not allocated here.  It isn't exported when
		 * created, hence the handle allocation rather than a
		 * lookup.
		 */
		PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
						  &psMapDevClassMemOUT->sClientSyncInfo.hKernelSyncInfo,
						  psMemInfo->psKernelSyncInfo,
						  PVRSRV_HANDLE_TYPE_SYNC_INFO,
						  PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
						  psMapDevClassMemOUT->sClientMemInfo.hKernelMemInfo);
	}

	COMMIT_HANDLE_BATCH_OR_ERROR(psMapDevClassMemOUT->eError, psPerProc)

	return 0;
}

static IMG_INT
PVRSRVUnmapDeviceClassMemoryBW(IMG_UINT32 ui32BridgeID,
							   PVRSRV_BRIDGE_IN_UNMAP_DEVICECLASS_MEMORY *psUnmapDevClassMemIN,
							   PVRSRV_BRIDGE_RETURN *psRetOUT,
							   PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvKernelMemInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_UNMAP_DEVICECLASS_MEMORY);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase, &pvKernelMemInfo,
						   psUnmapDevClassMemIN->psKernelMemInfo,
						   PVRSRV_HANDLE_TYPE_MEM_INFO);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVUnmapDeviceClassMemoryBW: PVRSRVLookupHandle failed %d", psRetOUT->eError));
		return 0;
	}

	psRetOUT->eError = PVRSRVUnmapDeviceClassMemoryKM(pvKernelMemInfo);

	if(psRetOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVUnmapDeviceClassMemoryBW: PVRSRVUnmapDeviceClassMemoryKM failed %d", psRetOUT->eError));
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVUnmapDeviceClassMemoryKM: pvKernelMemInfo: 0x%x", (unsigned int)pvKernelMemInfo));
		return 0;
	}

	psRetOUT->eError =
		PVRSRVReleaseHandle(psPerProc->psHandleBase,
							psUnmapDevClassMemIN->psKernelMemInfo,
							PVRSRV_HANDLE_TYPE_MEM_INFO);

	if(psRetOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVUnmapDeviceClassMemoryBW: PVRSRVReleaseHandle failed %d", psRetOUT->eError));
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVReleaseHandle: psHandleBase: 0x%x, psKernelMemInfo: 0x%x", (unsigned int)psPerProc->psHandleBase, (unsigned int)psUnmapDevClassMemIN->psKernelMemInfo));
		return 0;
	}
	return 0;
}
/* S.LSI does not use wrap function */
#if defined(PVR_USING_EXT_WRAP_MEMORY)
#if defined(OS_PVRSRV_WRAP_EXT_MEM_BW)
IMG_INT
PVRSRVWrapExtMemoryBW(IMG_UINT32 ui32BridgeID,
					  PVRSRV_BRIDGE_IN_WRAP_EXT_MEMORY *psWrapExtMemIN,
					  PVRSRV_BRIDGE_OUT_WRAP_EXT_MEMORY *psWrapExtMemOUT,
					  PVRSRV_PER_PROCESS_DATA *psPerProc);
#else /* OS_PVRSRV_WRAP_EXT_MEM_BW */
static IMG_INT
PVRSRVWrapExtMemoryBW(IMG_UINT32 ui32BridgeID,
					  PVRSRV_BRIDGE_IN_WRAP_EXT_MEMORY *psWrapExtMemIN,
					  PVRSRV_BRIDGE_OUT_WRAP_EXT_MEMORY *psWrapExtMemOUT,
					  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_HANDLE hDevCookieInt;
	IMG_HANDLE hDevMemContextInt;
	PVRSRV_KERNEL_MEM_INFO *psMemInfo;
	IMG_SYS_PHYADDR *psSysPAddr = IMG_NULL;
        IMG_UINT32 ui32PageTableSize = 0;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_WRAP_EXT_MEMORY);

	NEW_HANDLE_BATCH_OR_ERROR(psWrapExtMemOUT->eError, psPerProc, 2)

	/*
	 * FIXME: This needs reworking - don't use the user supplied page
	 * table list, get the list from the OS.
	 */
	psWrapExtMemOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevCookieInt,
						   psWrapExtMemIN->hDevCookie,
						   PVRSRV_HANDLE_TYPE_DEV_NODE);
	if(psWrapExtMemOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	/* get the device memory context */
	psWrapExtMemOUT->eError =
	PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevMemContextInt,
				   psWrapExtMemIN->hDevMemContext,
				   PVRSRV_HANDLE_TYPE_DEV_MEM_CONTEXT);

	if(psWrapExtMemOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	if(psWrapExtMemIN->ui32NumPageTableEntries)
	{
		ui32PageTableSize = psWrapExtMemIN->ui32NumPageTableEntries
						* sizeof(IMG_SYS_PHYADDR);

		ASSIGN_AND_EXIT_ON_ERROR(psWrapExtMemOUT->eError,
				  OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
				  ui32PageTableSize,
				  (IMG_VOID **)&psSysPAddr, 0,
				  "Page Table"));

		if(CopyFromUserWrapper(psPerProc,
							   ui32BridgeID,
							   psSysPAddr,
							   psWrapExtMemIN->psSysPAddr,
							   ui32PageTableSize) != PVRSRV_OK)
		{
			OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, 	ui32PageTableSize, (IMG_VOID *)psSysPAddr, 0);
			/*not nulling pointer, out of scope*/
			return -EFAULT;
		}
	}

	psWrapExtMemOUT->eError =
		PVRSRVWrapExtMemoryKM(hDevCookieInt,
							  psPerProc,
							  hDevMemContextInt,
							  psWrapExtMemIN->uByteSize,
							  psWrapExtMemIN->uPageOffset,
							  psWrapExtMemIN->bPhysContig,
							  psSysPAddr,
							  psWrapExtMemIN->pvLinAddr,
							  psWrapExtMemIN->ui32Flags,
							  &psMemInfo);

	if(psWrapExtMemIN->ui32NumPageTableEntries)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
			  ui32PageTableSize,
			  (IMG_VOID *)psSysPAddr, 0);
		/*not nulling pointer, out of scope*/
	}

	if(psWrapExtMemOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psWrapExtMemOUT->sClientMemInfo.pvLinAddrKM =
			psMemInfo->pvLinAddrKM;

	/* setup the mem info */
	psWrapExtMemOUT->sClientMemInfo.pvLinAddr = 0;
	psWrapExtMemOUT->sClientMemInfo.sDevVAddr = psMemInfo->sDevVAddr;
	psWrapExtMemOUT->sClientMemInfo.ui32Flags = psMemInfo->ui32Flags;
    psWrapExtMemOUT->sClientMemInfo.uAllocSize = psMemInfo->uAllocSize;
	psWrapExtMemOUT->sClientMemInfo.hMappingInfo = psMemInfo->sMemBlk.hOSMemHandle;

	PVRSRVAllocHandleNR(psPerProc->psHandleBase,
					  &psWrapExtMemOUT->sClientMemInfo.hKernelMemInfo,
					  psMemInfo,
					  PVRSRV_HANDLE_TYPE_MEM_INFO,
					  PVRSRV_HANDLE_ALLOC_FLAG_NONE);


	/* setup the sync info */
#if !defined(PVRSRV_DISABLE_UM_SYNCOBJ_MAPPINGS)
	psWrapExtMemOUT->sClientSyncInfo.psSyncData =
		psMemInfo->psKernelSyncInfo->psSyncData;
	psWrapExtMemOUT->sClientSyncInfo.sWriteOpsCompleteDevVAddr =
		psMemInfo->psKernelSyncInfo->sWriteOpsCompleteDevVAddr;
	psWrapExtMemOUT->sClientSyncInfo.sReadOpsCompleteDevVAddr =
		psMemInfo->psKernelSyncInfo->sReadOpsCompleteDevVAddr;
	psWrapExtMemOUT->sClientSyncInfo.sReadOps2CompleteDevVAddr =
		psMemInfo->psKernelSyncInfo->sReadOps2CompleteDevVAddr;

	psWrapExtMemOUT->sClientSyncInfo.hMappingInfo =
		psMemInfo->psKernelSyncInfo->psSyncDataMemInfoKM->sMemBlk.hOSMemHandle;
#endif

	psWrapExtMemOUT->sClientMemInfo.psClientSyncInfo = &psWrapExtMemOUT->sClientSyncInfo;

	PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
					  &psWrapExtMemOUT->sClientSyncInfo.hKernelSyncInfo,
					  (IMG_HANDLE)psMemInfo->psKernelSyncInfo,
					  PVRSRV_HANDLE_TYPE_SYNC_INFO,
					  PVRSRV_HANDLE_ALLOC_FLAG_NONE,
					  psWrapExtMemOUT->sClientMemInfo.hKernelMemInfo);

	COMMIT_HANDLE_BATCH_OR_ERROR(psWrapExtMemOUT->eError, psPerProc)

	return 0;
}
#endif /* OS_PVRSRV_WRAP_EXT_MEM_BW */

static IMG_INT
PVRSRVUnwrapExtMemoryBW(IMG_UINT32 ui32BridgeID,
						PVRSRV_BRIDGE_IN_UNWRAP_EXT_MEMORY *psUnwrapExtMemIN,
						PVRSRV_BRIDGE_RETURN *psRetOUT,
						PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvMemInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_UNWRAP_EXT_MEMORY);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvMemInfo,
						   psUnwrapExtMemIN->hKernelMemInfo,
						   PVRSRV_HANDLE_TYPE_MEM_INFO);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVUnwrapExtMemoryBW: PVRSRVLookupHandle failed %d", psRetOUT->eError));
		return 0;
	}

	psRetOUT->eError =
		PVRSRVUnwrapExtMemoryKM((PVRSRV_KERNEL_MEM_INFO *)pvMemInfo);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVUnwrapExtMemoryBW: PVRSRVUnwrapExtMemoryKM failed %d", psRetOUT->eError));
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVUnwrapExtMemoryKM: pvMemInfo: 0x%x", (unsigned int)pvMemInfo));
		return 0;
	}

	psRetOUT->eError =
		PVRSRVReleaseHandle(psPerProc->psHandleBase,
						   psUnwrapExtMemIN->hKernelMemInfo,
						   PVRSRV_HANDLE_TYPE_MEM_INFO);

	if(psRetOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVUnwrapExtMemoryBW: PVRSRVReleaseHandle failed %d", psRetOUT->eError));
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVReleaseHandle: psHandleBase: 0x%x, hKernelMemInfo: 0x%x", (unsigned int)psPerProc->psHandleBase, (unsigned int)psUnwrapExtMemIN->hKernelMemInfo));
		return 0;
	}

	return 0;
}
#endif

#if defined(SUPPORT_ION)
static IMG_INT
PVRSRVMapIonHandleBW(IMG_UINT32 ui32BridgeID,
					   PVRSRV_BRIDGE_IN_MAP_ION_HANDLE *psMapIonIN,
					   PVRSRV_BRIDGE_OUT_MAP_ION_HANDLE *psMapIonOUT,
					   PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_KERNEL_MEM_INFO  *psKernelMemInfo;
	IMG_UINT64 ui64Stamp;

	psMapIonOUT->eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
											 &psMapIonIN->hDevCookie,
											 psMapIonIN->hDevCookie,
											 PVRSRV_HANDLE_TYPE_DEV_NODE);
	if (psMapIonOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to lookup device node handle", __FUNCTION__));
		return 0;
	}

	psMapIonOUT->eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
											 &psMapIonIN->hDevMemHeap,
											 psMapIonIN->hDevMemHeap,
											 PVRSRV_HANDLE_TYPE_DEV_MEM_HEAP);
	if (psMapIonOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to lookup memory context handle", __FUNCTION__));
		return 0;
	}

	psMapIonOUT->eError = PVRSRVMapIonHandleKM(psPerProc,
											   psMapIonIN->hDevCookie,
											   psMapIonIN->hDevMemHeap,
											   psMapIonIN->handle,
											   psMapIonIN->ui32Attribs,
											   psMapIonIN->ui32ChunkCount,
											   psMapIonIN->auiOffset,
											   psMapIonIN->auiSize,
											   &psMapIonOUT->uiIonBufferSize,
											   &psKernelMemInfo,
											   &ui64Stamp);
	if (psMapIonOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to map ion handle", __FUNCTION__));
		return 0;
	}

	OSMemSet(&psMapIonOUT->sClientMemInfo,
			 0,
			 sizeof(psMapIonOUT->sClientMemInfo));

	psMapIonOUT->sClientMemInfo.pvLinAddrKM =
			psKernelMemInfo->pvLinAddrKM;

	psMapIonOUT->sClientMemInfo.pvLinAddr = 0;
	psMapIonOUT->sClientMemInfo.sDevVAddr = psKernelMemInfo->sDevVAddr;
	psMapIonOUT->sClientMemInfo.ui32Flags = psKernelMemInfo->ui32Flags;
	psMapIonOUT->sClientMemInfo.uAllocSize = psKernelMemInfo->uAllocSize;

	/* No mapping info, we map through ion */
	psMapIonOUT->sClientMemInfo.hMappingInfo = IMG_NULL;

#if defined(SUPPORT_MEMINFO_IDS)
	psMapIonOUT->sClientMemInfo.ui64Stamp = ui64Stamp;
#endif

	PVRSRVAllocHandleNR(psPerProc->psHandleBase,
						&psMapIonOUT->sClientMemInfo.hKernelMemInfo,
						psKernelMemInfo,
						PVRSRV_HANDLE_TYPE_MEM_INFO,
						PVRSRV_HANDLE_ALLOC_FLAG_NONE);

	if(psMapIonIN->ui32Attribs & PVRSRV_MEM_NO_SYNCOBJ)
	{
		/* signal no syncinfo */
		OSMemSet(&psMapIonOUT->sClientSyncInfo,
				 0,
				 sizeof (PVRSRV_CLIENT_SYNC_INFO));
		psMapIonOUT->sClientMemInfo.psClientSyncInfo = IMG_NULL;
	}
	else
	{
		/* and setup the sync info */
#if !defined(PVRSRV_DISABLE_UM_SYNCOBJ_MAPPINGS)
		psMapIonOUT->sClientSyncInfo.psSyncData =
			psKernelMemInfo->psKernelSyncInfo->psSyncData;
		psMapIonOUT->sClientSyncInfo.sWriteOpsCompleteDevVAddr =
			psKernelMemInfo->psKernelSyncInfo->sWriteOpsCompleteDevVAddr;
		psMapIonOUT->sClientSyncInfo.sReadOpsCompleteDevVAddr =
			psKernelMemInfo->psKernelSyncInfo->sReadOpsCompleteDevVAddr;
		psMapIonOUT->sClientSyncInfo.sReadOps2CompleteDevVAddr =
			psKernelMemInfo->psKernelSyncInfo->sReadOps2CompleteDevVAddr;

		psMapIonOUT->sClientSyncInfo.hMappingInfo =
			psKernelMemInfo->psKernelSyncInfo->psSyncDataMemInfoKM->sMemBlk.hOSMemHandle;
#endif

		PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
							   &psMapIonOUT->sClientSyncInfo.hKernelSyncInfo,
							   psKernelMemInfo->psKernelSyncInfo,
							   PVRSRV_HANDLE_TYPE_SYNC_INFO,
							   PVRSRV_HANDLE_ALLOC_FLAG_NONE,
							   psMapIonOUT->sClientMemInfo.hKernelMemInfo);

		psMapIonOUT->sClientMemInfo.psClientSyncInfo =
			&psMapIonOUT->sClientSyncInfo;
	}
	return 0;
}

static IMG_INT
PVRSRVUnmapIonHandleBW(IMG_UINT32 ui32BridgeID,
					   PVRSRV_BRIDGE_IN_UNMAP_ION_HANDLE *psUnmapIonIN,
					   PVRSRV_BRIDGE_RETURN *psUnmapIonOUT,
					   PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvKernelMemInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_UNMAP_ION_HANDLE);

	psUnmapIonOUT->eError =
        PVRSRVLookupHandle(psPerProc->psHandleBase,
                           &pvKernelMemInfo,
						   psUnmapIonIN->psKernelMemInfo,
						   PVRSRV_HANDLE_TYPE_MEM_INFO);

	if(psUnmapIonOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVUnmapIonHandleBW: PVRSRVLookupHandle failed %d", psUnmapIonOUT->eError));
		return 0;
	}

	psUnmapIonOUT->eError = PVRSRVUnmapIonHandleKM(pvKernelMemInfo);

	if(psUnmapIonOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVUnmapIonHandleBW: PVRSRVUnmapIonHandleKM failed %d", psUnmapIonOUT->eError));
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVUnmapIonHandleKM: pvKernelMemInfo: 0x%x", (unsigned int)pvKernelMemInfo));
		return 0;
	}

	psUnmapIonOUT->eError =
		PVRSRVReleaseHandle(psPerProc->psHandleBase,
							psUnmapIonIN->psKernelMemInfo,
							PVRSRV_HANDLE_TYPE_MEM_INFO);
	if(psUnmapIonOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVUnmapIonHandleBW: PVRSRVReleaseHandle failed %d", psUnmapIonOUT->eError));
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVReleaseHandle: psHandleBase: 0x%x, psKernelMemInfo: 0x%x", (unsigned int)psPerProc->psHandleBase, (unsigned int)psUnmapIonIN->psKernelMemInfo));
		return 0;
	}

	return 0;
}
#endif	/* SUPPORT_ION */

static IMG_INT
PVRSRVGetFreeDeviceMemBW(IMG_UINT32 ui32BridgeID,
						 PVRSRV_BRIDGE_IN_GETFREEDEVICEMEM *psGetFreeDeviceMemIN,
						 PVRSRV_BRIDGE_OUT_GETFREEDEVICEMEM *psGetFreeDeviceMemOUT,
						 PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_GETFREE_DEVICEMEM);

	PVR_UNREFERENCED_PARAMETER(psPerProc);

	psGetFreeDeviceMemOUT->eError =
		PVRSRVGetFreeDeviceMemKM(psGetFreeDeviceMemIN->ui32Flags,
								 &psGetFreeDeviceMemOUT->uTotal,
								 &psGetFreeDeviceMemOUT->uFree,
								 &psGetFreeDeviceMemOUT->uLargestBlock);

	return 0;
}

static IMG_INT
PVRMMapOSMemHandleToMMapDataBW(IMG_UINT32 ui32BridgeID,
								  PVRSRV_BRIDGE_IN_MHANDLE_TO_MMAP_DATA *psMMapDataIN,
								  PVRSRV_BRIDGE_OUT_MHANDLE_TO_MMAP_DATA *psMMapDataOUT,
								  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MHANDLE_TO_MMAP_DATA);

#if defined (__linux__) || defined (__QNXNTO__)
	psMMapDataOUT->eError =
		PVRMMapOSMemHandleToMMapData(psPerProc,
										psMMapDataIN->hMHandle,
										&psMMapDataOUT->uiMMapOffset,
										&psMMapDataOUT->uiByteOffset,
										&psMMapDataOUT->uiRealByteSize,
										&psMMapDataOUT->uiUserVAddr);
#else
	PVR_UNREFERENCED_PARAMETER(psPerProc);
	PVR_UNREFERENCED_PARAMETER(psMMapDataIN);

	psMMapDataOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;
#endif
	return 0;
}


static IMG_INT
PVRMMapReleaseMMapDataBW(IMG_UINT32 ui32BridgeID,
								  PVRSRV_BRIDGE_IN_RELEASE_MMAP_DATA *psMMapDataIN,
								  PVRSRV_BRIDGE_OUT_RELEASE_MMAP_DATA *psMMapDataOUT,
								  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RELEASE_MMAP_DATA);

#if defined (__linux__) || defined (__QNXNTO__)
	psMMapDataOUT->eError =
		PVRMMapReleaseMMapData(psPerProc,
										psMMapDataIN->hMHandle,
										&psMMapDataOUT->bMUnmap,
										&psMMapDataOUT->uiRealByteSize,
										&psMMapDataOUT->uiUserVAddr);
#else

	PVR_UNREFERENCED_PARAMETER(psPerProc);
	PVR_UNREFERENCED_PARAMETER(psMMapDataIN);

	psMMapDataOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;
#endif
	return 0;
}


static IMG_INT
PVRSRVChangeDeviceMemoryAttributesBW(IMG_UINT32 ui32BridgeID,
                                     PVRSRV_BRIDGE_IN_CHG_DEV_MEM_ATTRIBS *psChgMemAttribIN,
                                     PVRSRV_BRIDGE_RETURN *psRetOUT,
                                     PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVR_UNREFERENCED_PARAMETER(ui32BridgeID);
	PVR_UNREFERENCED_PARAMETER(psChgMemAttribIN);
	PVR_UNREFERENCED_PARAMETER(psRetOUT);
	PVR_UNREFERENCED_PARAMETER(psPerProc);

	return 0;
}

#ifdef PDUMP
static IMG_INT
PDumpIsCaptureFrameBW(IMG_UINT32 ui32BridgeID,
					  IMG_VOID *psBridgeIn,
					  PVRSRV_BRIDGE_OUT_PDUMP_ISCAPTURING *psPDumpIsCapturingOUT,
					  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_ISCAPTURING);
	PVR_UNREFERENCED_PARAMETER(psBridgeIn);
	PVR_UNREFERENCED_PARAMETER(psPerProc);

	psPDumpIsCapturingOUT->bIsCapturing = PDumpIsCaptureFrameKM();
	psPDumpIsCapturingOUT->eError = PVRSRV_OK;

	return 0;
}

static IMG_INT
PDumpCommentBW(IMG_UINT32 ui32BridgeID,
			   PVRSRV_BRIDGE_IN_PDUMP_COMMENT *psPDumpCommentIN,
			   PVRSRV_BRIDGE_RETURN *psRetOUT,
			   PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_COMMENT);
	PVR_UNREFERENCED_PARAMETER(psPerProc);

	psRetOUT->eError = PDumpCommentKM(&psPDumpCommentIN->szComment[0],
									  psPDumpCommentIN->ui32Flags);
	return 0;
}

static IMG_INT
PDumpSetFrameBW(IMG_UINT32 ui32BridgeID,
				PVRSRV_BRIDGE_IN_PDUMP_SETFRAME *psPDumpSetFrameIN,
				PVRSRV_BRIDGE_RETURN *psRetOUT,
				PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_SETFRAME);
	PVR_UNREFERENCED_PARAMETER(psPerProc);

	psRetOUT->eError = PDumpSetFrameKM(psPDumpSetFrameIN->ui32Frame);

	return 0;
}

static IMG_INT
PDumpRegWithFlagsBW(IMG_UINT32 ui32BridgeID,
					PVRSRV_BRIDGE_IN_PDUMP_DUMPREG *psPDumpRegDumpIN,
					PVRSRV_BRIDGE_RETURN *psRetOUT,
					PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_DEVICE_NODE *psDeviceNode;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_REG);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   (IMG_VOID **)&psDeviceNode,
						   psPDumpRegDumpIN->hDevCookie,
						   PVRSRV_HANDLE_TYPE_DEV_NODE);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError = PDumpRegWithFlagsKM (psPDumpRegDumpIN->szRegRegion,
											psPDumpRegDumpIN->sHWReg.ui32RegAddr,
											psPDumpRegDumpIN->sHWReg.ui32RegVal,
											psPDumpRegDumpIN->ui32Flags);

	return 0;
}

static IMG_INT
PDumpRegPolBW(IMG_UINT32 ui32BridgeID,
			  PVRSRV_BRIDGE_IN_PDUMP_REGPOL *psPDumpRegPolIN,
			  PVRSRV_BRIDGE_RETURN *psRetOUT,
			  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_DEVICE_NODE *psDeviceNode;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_REGPOL);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   (IMG_VOID **)&psDeviceNode,
						   psPDumpRegPolIN->hDevCookie,
						   PVRSRV_HANDLE_TYPE_DEV_NODE);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}


	psRetOUT->eError =
		PDumpRegPolWithFlagsKM(psPDumpRegPolIN->szRegRegion,
							   psPDumpRegPolIN->sHWReg.ui32RegAddr,
							   psPDumpRegPolIN->sHWReg.ui32RegVal,
							   psPDumpRegPolIN->ui32Mask,
							   psPDumpRegPolIN->ui32Flags,
							   PDUMP_POLL_OPERATOR_EQUAL);

	return 0;
}

static IMG_INT
PDumpMemPolBW(IMG_UINT32 ui32BridgeID,
			  PVRSRV_BRIDGE_IN_PDUMP_MEMPOL *psPDumpMemPolIN,
			  PVRSRV_BRIDGE_RETURN *psRetOUT,
			  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvMemInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_MEMPOL);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvMemInfo,
						   psPDumpMemPolIN->psKernelMemInfo,
						   PVRSRV_HANDLE_TYPE_MEM_INFO);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PDumpMemPolKM(((PVRSRV_KERNEL_MEM_INFO *)pvMemInfo),
					  psPDumpMemPolIN->ui32Offset,
					  psPDumpMemPolIN->ui32Value,
					  psPDumpMemPolIN->ui32Mask,
					  psPDumpMemPolIN->eOperator,
					  psPDumpMemPolIN->ui32Flags,
					  MAKEUNIQUETAG(pvMemInfo));

	return 0;
}

static IMG_INT
PDumpMemBW(IMG_UINT32 ui32BridgeID,
		   PVRSRV_BRIDGE_IN_PDUMP_DUMPMEM *psPDumpMemDumpIN,
		   PVRSRV_BRIDGE_RETURN *psRetOUT,
		   PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvMemInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_DUMPMEM);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvMemInfo,
						   psPDumpMemDumpIN->psKernelMemInfo,
						   PVRSRV_HANDLE_TYPE_MEM_INFO);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PDumpMemUM(psPerProc,
				   psPDumpMemDumpIN->pvAltLinAddr,
				   psPDumpMemDumpIN->pvLinAddr,
				   pvMemInfo,
				   psPDumpMemDumpIN->ui32Offset,
				   psPDumpMemDumpIN->ui32Bytes,
				   psPDumpMemDumpIN->ui32Flags,
				   MAKEUNIQUETAG(pvMemInfo));

	return 0;
}

static IMG_INT
PDumpBitmapBW(IMG_UINT32 ui32BridgeID,
			  PVRSRV_BRIDGE_IN_PDUMP_BITMAP *psPDumpBitmapIN,
			  PVRSRV_BRIDGE_RETURN *psRetOUT,
			  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_DEVICE_NODE *psDeviceNode;
	IMG_HANDLE hDevMemContextInt;

	PVR_UNREFERENCED_PARAMETER(ui32BridgeID);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase, (IMG_VOID **)&psDeviceNode,
						   psPDumpBitmapIN->hDevCookie,
						   PVRSRV_HANDLE_TYPE_DEV_NODE);

	psRetOUT->eError =
		PVRSRVLookupHandle(	psPerProc->psHandleBase,
							&hDevMemContextInt,
							psPDumpBitmapIN->hDevMemContext,
							PVRSRV_HANDLE_TYPE_DEV_MEM_CONTEXT);

	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PDumpBitmapKM(psDeviceNode,
					  &psPDumpBitmapIN->szFileName[0],
					  psPDumpBitmapIN->ui32FileOffset,
					  psPDumpBitmapIN->ui32Width,
					  psPDumpBitmapIN->ui32Height,
					  psPDumpBitmapIN->ui32StrideInBytes,
					  psPDumpBitmapIN->sDevBaseAddr,
					  hDevMemContextInt,
					  psPDumpBitmapIN->ui32Size,
					  psPDumpBitmapIN->ePixelFormat,
					  psPDumpBitmapIN->eMemFormat,
					  psPDumpBitmapIN->ui32Flags);

	return 0;
}

static IMG_INT
PDumpReadRegBW(IMG_UINT32 ui32BridgeID,
			   PVRSRV_BRIDGE_IN_PDUMP_READREG *psPDumpReadRegIN,
			   PVRSRV_BRIDGE_RETURN *psRetOUT,
			   PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_DEVICE_NODE *psDeviceNode;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_DUMPREADREG);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase, (IMG_VOID **)&psDeviceNode,
						   psPDumpReadRegIN->hDevCookie,
						   PVRSRV_HANDLE_TYPE_DEV_NODE);

	psRetOUT->eError =
		PDumpReadRegKM(&psPDumpReadRegIN->szRegRegion[0],
					   &psPDumpReadRegIN->szFileName[0],
					   psPDumpReadRegIN->ui32FileOffset,
					   psPDumpReadRegIN->ui32Address,
					   psPDumpReadRegIN->ui32Size,
					   psPDumpReadRegIN->ui32Flags);

	return 0;
}

static IMG_INT
PDumpMemPagesBW(IMG_UINT32 ui32BridgeID,
				  PVRSRV_BRIDGE_IN_PDUMP_MEMPAGES *psPDumpMemPagesIN,
				  PVRSRV_BRIDGE_RETURN *psRetOUT,
				  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_DEVICE_NODE *psDeviceNode;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_MEMPAGES);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   (IMG_VOID **)&psDeviceNode,
						   psPDumpMemPagesIN->hDevCookie,
						   PVRSRV_HANDLE_TYPE_DEV_NODE);

	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}


	return 0;
}

static IMG_INT
PDumpDriverInfoBW(IMG_UINT32 ui32BridgeID,
				  PVRSRV_BRIDGE_IN_PDUMP_DRIVERINFO *psPDumpDriverInfoIN,
				  PVRSRV_BRIDGE_RETURN *psRetOUT,
				  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_UINT32 ui32PDumpFlags;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_DRIVERINFO);
	PVR_UNREFERENCED_PARAMETER(psPerProc);

	ui32PDumpFlags = 0;
	if(psPDumpDriverInfoIN->bContinuous)
	{
		ui32PDumpFlags |= PDUMP_FLAGS_CONTINUOUS;
	}
	psRetOUT->eError =
		PDumpDriverInfoKM(&psPDumpDriverInfoIN->szString[0],
						  ui32PDumpFlags);

	return 0;
}

static IMG_INT
PDumpSyncDumpBW(IMG_UINT32 ui32BridgeID,
				PVRSRV_BRIDGE_IN_PDUMP_DUMPSYNC *psPDumpSyncDumpIN,
				PVRSRV_BRIDGE_RETURN *psRetOUT,
				PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_UINT32 ui32Bytes = psPDumpSyncDumpIN->ui32Bytes;
	IMG_VOID *pvSyncInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_DUMPSYNC);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase, &pvSyncInfo,
						   psPDumpSyncDumpIN->psKernelSyncInfo,
						   PVRSRV_HANDLE_TYPE_SYNC_INFO);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PDumpMemUM(psPerProc,
				   psPDumpSyncDumpIN->pvAltLinAddr,
				   IMG_NULL,
				   ((PVRSRV_KERNEL_SYNC_INFO *)pvSyncInfo)->psSyncDataMemInfoKM,
				   psPDumpSyncDumpIN->ui32Offset,
				   ui32Bytes,
				   0,
				   MAKEUNIQUETAG(((PVRSRV_KERNEL_SYNC_INFO *)pvSyncInfo)->psSyncDataMemInfoKM));

	return 0;
}

static IMG_INT
PDumpSyncPolBW(IMG_UINT32 ui32BridgeID,
			   PVRSRV_BRIDGE_IN_PDUMP_SYNCPOL *psPDumpSyncPolIN,
			   PVRSRV_BRIDGE_RETURN *psRetOUT,
			   PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_UINT32 ui32Offset;
	IMG_VOID *pvSyncInfo;
	IMG_UINT32 ui32Value;
	IMG_UINT32 ui32Mask;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_SYNCPOL);

	psRetOUT->eError =
        PVRSRVLookupHandle(psPerProc->psHandleBase,
                           &pvSyncInfo,
						   psPDumpSyncPolIN->psKernelSyncInfo,
						   PVRSRV_HANDLE_TYPE_SYNC_INFO);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	if(psPDumpSyncPolIN->bIsRead)
	{
		ui32Offset = offsetof(PVRSRV_SYNC_DATA, ui32ReadOpsComplete);
	}
	else
	{
		ui32Offset = offsetof(PVRSRV_SYNC_DATA, ui32WriteOpsComplete);
	}

	/* FIXME:  Move this code to somewhere outside of the bridge */
	if (psPDumpSyncPolIN->bUseLastOpDumpVal)
	{
		if(psPDumpSyncPolIN->bIsRead)
		{
			ui32Value = ((PVRSRV_KERNEL_SYNC_INFO *)pvSyncInfo)->psSyncData->ui32LastReadOpDumpVal;
		}
		else
		{
			ui32Value = ((PVRSRV_KERNEL_SYNC_INFO *)pvSyncInfo)->psSyncData->ui32LastOpDumpVal;
		}
		ui32Mask = 0xffffffff;
	}
	else
	{
		ui32Value = psPDumpSyncPolIN->ui32Value;
		ui32Mask =  psPDumpSyncPolIN->ui32Mask;
	}

	psRetOUT->eError =
		PDumpMemPolKM(((PVRSRV_KERNEL_SYNC_INFO *)pvSyncInfo)->psSyncDataMemInfoKM,
					  ui32Offset,
					  ui32Value,
					  ui32Mask,
					  PDUMP_POLL_OPERATOR_EQUAL,
					  0,
					  MAKEUNIQUETAG(((PVRSRV_KERNEL_SYNC_INFO *)pvSyncInfo)->psSyncDataMemInfoKM));

	return 0;
}


static IMG_INT
PDumpCycleCountRegReadBW(IMG_UINT32 ui32BridgeID,
						 PVRSRV_BRIDGE_IN_PDUMP_CYCLE_COUNT_REG_READ *psPDumpCycleCountRegReadIN,
						 PVRSRV_BRIDGE_RETURN *psRetOUT,
						 PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_DEVICE_NODE *psDeviceNode;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_CYCLE_COUNT_REG_READ);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   (IMG_VOID **)&psDeviceNode,
						   psPDumpCycleCountRegReadIN->hDevCookie,
						   PVRSRV_HANDLE_TYPE_DEV_NODE);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	PDumpCycleCountRegRead(&psDeviceNode->sDevId,
						   psPDumpCycleCountRegReadIN->ui32RegOffset,
						   psPDumpCycleCountRegReadIN->bLastFrame);

	psRetOUT->eError = PVRSRV_OK;

	return 0;
}

static IMG_INT
PDumpPDDevPAddrBW(IMG_UINT32 ui32BridgeID,
				  PVRSRV_BRIDGE_IN_PDUMP_DUMPPDDEVPADDR *psPDumpPDDevPAddrIN,
				  PVRSRV_BRIDGE_RETURN *psRetOUT,
				  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvMemInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_DUMPPDDEVPADDR);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase, &pvMemInfo,
						   psPDumpPDDevPAddrIN->hKernelMemInfo,
						   PVRSRV_HANDLE_TYPE_MEM_INFO);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PDumpPDDevPAddrKM((PVRSRV_KERNEL_MEM_INFO *)pvMemInfo,
						  psPDumpPDDevPAddrIN->ui32Offset,
						  psPDumpPDDevPAddrIN->sPDDevPAddr,
						  MAKEUNIQUETAG(pvMemInfo),
						  PDUMP_PD_UNIQUETAG);
	return 0;
}

static IMG_INT
PDumpStartInitPhaseBW(IMG_UINT32 ui32BridgeID,
					  IMG_VOID *psBridgeIn,
					  PVRSRV_BRIDGE_RETURN *psRetOUT,
					  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_STARTINITPHASE);
	PVR_UNREFERENCED_PARAMETER(psBridgeIn);
	PVR_UNREFERENCED_PARAMETER(psPerProc);

	psRetOUT->eError = PDumpStartInitPhaseKM();

	return 0;
}

static IMG_INT
PDumpStopInitPhaseBW(IMG_UINT32 ui32BridgeID,
					  IMG_VOID *psBridgeIn,
					  PVRSRV_BRIDGE_RETURN *psRetOUT,
					  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_STOPINITPHASE);
	PVR_UNREFERENCED_PARAMETER(psBridgeIn);
	PVR_UNREFERENCED_PARAMETER(psPerProc);

	psRetOUT->eError = PDumpStopInitPhaseKM();

	return 0;
}

#endif /* PDUMP */


static IMG_INT
PVRSRVGetMiscInfoBW(IMG_UINT32 ui32BridgeID,
					PVRSRV_BRIDGE_IN_GET_MISC_INFO *psGetMiscInfoIN,
					PVRSRV_BRIDGE_OUT_GET_MISC_INFO *psGetMiscInfoOUT,
					PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_ERROR eError;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_GET_MISC_INFO);

	OSMemCopy(&psGetMiscInfoOUT->sMiscInfo,
			  &psGetMiscInfoIN->sMiscInfo,
			  sizeof(PVRSRV_MISC_INFO));

	if (((psGetMiscInfoIN->sMiscInfo.ui32StateRequest & PVRSRV_MISC_INFO_MEMSTATS_PRESENT) != 0) &&
	    ((psGetMiscInfoIN->sMiscInfo.ui32StateRequest & PVRSRV_MISC_INFO_DDKVERSION_PRESENT) != 0) &&
	    ((psGetMiscInfoIN->sMiscInfo.ui32StateRequest & PVRSRV_MISC_INFO_FREEMEM_PRESENT) != 0))
	{
		/* Client must choose which of memstats and DDK version will be written to
		 * kernel side buffer */
		psGetMiscInfoOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;
		return 0;
	}

	if (((psGetMiscInfoIN->sMiscInfo.ui32StateRequest & PVRSRV_MISC_INFO_MEMSTATS_PRESENT) != 0) ||
	    ((psGetMiscInfoIN->sMiscInfo.ui32StateRequest & PVRSRV_MISC_INFO_DDKVERSION_PRESENT) != 0) ||
	    ((psGetMiscInfoIN->sMiscInfo.ui32StateRequest & PVRSRV_MISC_INFO_FREEMEM_PRESENT) != 0))
	{
		/* Alloc kernel side buffer to write into */
		ASSIGN_AND_EXIT_ON_ERROR(psGetMiscInfoOUT->eError,
				    OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
		                    psGetMiscInfoOUT->sMiscInfo.ui32MemoryStrLen,
		                    (IMG_VOID **)&psGetMiscInfoOUT->sMiscInfo.pszMemoryStr, 0,
							"Output string buffer"));

		psGetMiscInfoOUT->eError = PVRSRVGetMiscInfoKM(&psGetMiscInfoOUT->sMiscInfo);

		/* Copy result to user */
		eError = CopyToUserWrapper(psPerProc, ui32BridgeID,
		                           psGetMiscInfoIN->sMiscInfo.pszMemoryStr,
		                           psGetMiscInfoOUT->sMiscInfo.pszMemoryStr,
		                           psGetMiscInfoOUT->sMiscInfo.ui32MemoryStrLen);

		/* Free kernel side buffer again */
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
		          psGetMiscInfoOUT->sMiscInfo.ui32MemoryStrLen,
		         (IMG_VOID *)psGetMiscInfoOUT->sMiscInfo.pszMemoryStr, 0);

		/* Replace output buffer pointer with input pointer, as both are expected
		 * to point to the same userspace memory.
		 */
		psGetMiscInfoOUT->sMiscInfo.pszMemoryStr = psGetMiscInfoIN->sMiscInfo.pszMemoryStr;

		if(eError != PVRSRV_OK)
		{
			/* Do error check at the end as we always have to free and reset the
			 * pointer.
			 */
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVGetMiscInfoBW Error copy to user"));
			return -EFAULT;
		}
	}
	else
	{
		psGetMiscInfoOUT->eError = PVRSRVGetMiscInfoKM(&psGetMiscInfoOUT->sMiscInfo);
	}

	/* Return on error so exit status of PVRSRVGetMiscInfoKM is propagated to client.
	 * Don't alloc handles for event object or timer; if error exit status is returned
	 * the handles should not be used (even if not null) */
	if (psGetMiscInfoOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	/*
	 * The handles are not allocated in batch mode as they are shared
	 * (a shared handle is allocated at most once), and there is no
	 * resource allocation to undo if the handle allocation fails.
	 */
	if (psGetMiscInfoIN->sMiscInfo.ui32StateRequest & PVRSRV_MISC_INFO_GLOBALEVENTOBJECT_PRESENT)
	{
		psGetMiscInfoOUT->eError = PVRSRVAllocHandle(psPerProc->psHandleBase,
													&psGetMiscInfoOUT->sMiscInfo.sGlobalEventObject.hOSEventKM,
													psGetMiscInfoOUT->sMiscInfo.sGlobalEventObject.hOSEventKM,
													PVRSRV_HANDLE_TYPE_SHARED_EVENT_OBJECT,
													PVRSRV_HANDLE_ALLOC_FLAG_SHARED);

		if (psGetMiscInfoOUT->eError != PVRSRV_OK)
		{
			return 0;
		}

	}

	if (psGetMiscInfoOUT->sMiscInfo.hSOCTimerRegisterOSMemHandle)
	{
		/* Allocate handle for SOC OSMemHandle */
		psGetMiscInfoOUT->eError = PVRSRVAllocHandle(psPerProc->psHandleBase,
						  &psGetMiscInfoOUT->sMiscInfo.hSOCTimerRegisterOSMemHandle,
						  psGetMiscInfoOUT->sMiscInfo.hSOCTimerRegisterOSMemHandle,
						  PVRSRV_HANDLE_TYPE_SOC_TIMER,
						  PVRSRV_HANDLE_ALLOC_FLAG_SHARED);

		if (psGetMiscInfoOUT->eError != PVRSRV_OK)
		{
			return 0;
		}
	}

	return 0;
}

static IMG_INT
PVRSRVConnectBW(IMG_UINT32 ui32BridgeID,
				PVRSRV_BRIDGE_IN_CONNECT_SERVICES *psConnectServicesIN,
				PVRSRV_BRIDGE_OUT_CONNECT_SERVICES *psConnectServicesOUT,
				PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_CONNECT_SERVICES);

#if defined(PDUMP)
	/* Store the per process connection info.
	 * The Xserver initially connects via PVR2D which sets the persistent flag.
	 * But, later the Xserver may connect via SGL which doesn't carry the flag (in
	 * general SGL clients aren't persistent). So we OR in the flag so if it was set
	 * before it remains set.
	 */
	if ((psConnectServicesIN->ui32Flags & SRV_FLAGS_PERSIST) != 0)
	{
    	psPerProc->bPDumpPersistent = IMG_TRUE;
	}

#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
	/* Select whether this client is our 'active' target for pdumping in a
	 * multi-process environment.
	 * NOTE: only 1 active target is supported at present.
	 */
	if ((psConnectServicesIN->ui32Flags & SRV_FLAGS_PDUMP_ACTIVE) != 0)
	{
    	psPerProc->bPDumpActive = IMG_TRUE;
	}
#endif /* SUPPORT_PDUMP_MULTI_PROCESS */
#else
	PVR_UNREFERENCED_PARAMETER(psConnectServicesIN);
#endif
	psConnectServicesOUT->hKernelServices = psPerProc->hPerProcData;
	psConnectServicesOUT->eError = PVRSRV_OK;

	return 0;
}

static IMG_INT
PVRSRVDisconnectBW(IMG_UINT32 ui32BridgeID,
				   IMG_VOID *psBridgeIn,
				   PVRSRV_BRIDGE_RETURN *psRetOUT,
				   PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVR_UNREFERENCED_PARAMETER(psPerProc);
	PVR_UNREFERENCED_PARAMETER(psBridgeIn);

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DISCONNECT_SERVICES);

	/* just return OK, per-process data is cleaned up by resmgr */
	psRetOUT->eError = PVRSRV_OK;

	return 0;
}

static IMG_INT
PVRSRVEnumerateDCBW(IMG_UINT32 ui32BridgeID,
					PVRSRV_BRIDGE_IN_ENUMCLASS *psEnumDispClassIN,
					PVRSRV_BRIDGE_OUT_ENUMCLASS *psEnumDispClassOUT,
					PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVR_UNREFERENCED_PARAMETER(psPerProc);

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_ENUM_CLASS);

	psEnumDispClassOUT->eError =
		PVRSRVEnumerateDCKM(psEnumDispClassIN->sDeviceClass,
							&psEnumDispClassOUT->ui32NumDevices,
							&psEnumDispClassOUT->ui32DevID[0]);

	return 0;
}

static IMG_INT
PVRSRVOpenDCDeviceBW(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_OPEN_DISPCLASS_DEVICE *psOpenDispClassDeviceIN,
					 PVRSRV_BRIDGE_OUT_OPEN_DISPCLASS_DEVICE *psOpenDispClassDeviceOUT,
					 PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_HANDLE hDevCookieInt;
	IMG_HANDLE hDispClassInfoInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_OPEN_DISPCLASS_DEVICE);

	NEW_HANDLE_BATCH_OR_ERROR(psOpenDispClassDeviceOUT->eError, psPerProc, 1)

	psOpenDispClassDeviceOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &hDevCookieInt,
						   psOpenDispClassDeviceIN->hDevCookie,
						   PVRSRV_HANDLE_TYPE_DEV_NODE);
	if(psOpenDispClassDeviceOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psOpenDispClassDeviceOUT->eError =
		PVRSRVOpenDCDeviceKM(psPerProc,
							 psOpenDispClassDeviceIN->ui32DeviceID,
							 hDevCookieInt,
							 &hDispClassInfoInt);

	if(psOpenDispClassDeviceOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	PVRSRVAllocHandleNR(psPerProc->psHandleBase,
					  &psOpenDispClassDeviceOUT->hDeviceKM,
					  hDispClassInfoInt,
					  PVRSRV_HANDLE_TYPE_DISP_INFO,
					  PVRSRV_HANDLE_ALLOC_FLAG_NONE);
	COMMIT_HANDLE_BATCH_OR_ERROR(psOpenDispClassDeviceOUT->eError, psPerProc)

	return 0;
}

static IMG_INT
PVRSRVCloseDCDeviceBW(IMG_UINT32 ui32BridgeID,
					  PVRSRV_BRIDGE_IN_CLOSE_DISPCLASS_DEVICE *psCloseDispClassDeviceIN,
					  PVRSRV_BRIDGE_RETURN *psRetOUT,
					  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvDispClassInfoInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_CLOSE_DISPCLASS_DEVICE);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvDispClassInfoInt,
						   psCloseDispClassDeviceIN->hDeviceKM,
						   PVRSRV_HANDLE_TYPE_DISP_INFO);

	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError = PVRSRVCloseDCDeviceKM(pvDispClassInfoInt);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PVRSRVReleaseHandle(psPerProc->psHandleBase,
							psCloseDispClassDeviceIN->hDeviceKM,
							PVRSRV_HANDLE_TYPE_DISP_INFO);
	return 0;
}

static IMG_INT
PVRSRVEnumDCFormatsBW(IMG_UINT32 ui32BridgeID,
					  PVRSRV_BRIDGE_IN_ENUM_DISPCLASS_FORMATS *psEnumDispClassFormatsIN,
					  PVRSRV_BRIDGE_OUT_ENUM_DISPCLASS_FORMATS *psEnumDispClassFormatsOUT,
					  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvDispClassInfoInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_ENUM_DISPCLASS_FORMATS);

	psEnumDispClassFormatsOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvDispClassInfoInt,
						   psEnumDispClassFormatsIN->hDeviceKM,
						   PVRSRV_HANDLE_TYPE_DISP_INFO);
	if(psEnumDispClassFormatsOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psEnumDispClassFormatsOUT->eError =
		PVRSRVEnumDCFormatsKM(pvDispClassInfoInt,
							  &psEnumDispClassFormatsOUT->ui32Count,
							  psEnumDispClassFormatsOUT->asFormat);

	return 0;
}

static IMG_INT
PVRSRVEnumDCDimsBW(IMG_UINT32 ui32BridgeID,
				   PVRSRV_BRIDGE_IN_ENUM_DISPCLASS_DIMS *psEnumDispClassDimsIN,
				   PVRSRV_BRIDGE_OUT_ENUM_DISPCLASS_DIMS *psEnumDispClassDimsOUT,
				   PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvDispClassInfoInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_ENUM_DISPCLASS_DIMS);

	psEnumDispClassDimsOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvDispClassInfoInt,
						   psEnumDispClassDimsIN->hDeviceKM,
						   PVRSRV_HANDLE_TYPE_DISP_INFO);

	if(psEnumDispClassDimsOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psEnumDispClassDimsOUT->eError =
		PVRSRVEnumDCDimsKM(pvDispClassInfoInt,
						   &psEnumDispClassDimsIN->sFormat,
						   &psEnumDispClassDimsOUT->ui32Count,
						   psEnumDispClassDimsOUT->asDim);

	return 0;
}

#if defined(SUPPORT_PVRSRV_GET_DC_SYSTEM_BUFFER)
static IMG_INT
PVRSRVGetDCSystemBufferBW(IMG_UINT32 ui32BridgeID,
						  PVRSRV_BRIDGE_IN_GET_DISPCLASS_SYSBUFFER *psGetDispClassSysBufferIN,  //IMG_HANDLE *phGetDispClassSysBufferIN,
						  PVRSRV_BRIDGE_OUT_GET_DISPCLASS_SYSBUFFER *psGetDispClassSysBufferOUT,
						  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_HANDLE hBufferInt;
	IMG_VOID *pvDispClassInfoInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_GET_DISPCLASS_SYSBUFFER);

	NEW_HANDLE_BATCH_OR_ERROR(psGetDispClassSysBufferOUT->eError, psPerProc, 1)

	psGetDispClassSysBufferOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvDispClassInfoInt,
						   psGetDispClassSysBufferIN->hDeviceKM,
						   PVRSRV_HANDLE_TYPE_DISP_INFO);
	if(psGetDispClassSysBufferOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psGetDispClassSysBufferOUT->eError =
		PVRSRVGetDCSystemBufferKM(pvDispClassInfoInt,
								  &hBufferInt);

	if(psGetDispClassSysBufferOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	/* PRQA S 1461 6 */ /* ignore warning about enum type being converted */
	PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
						 &psGetDispClassSysBufferOUT->hBuffer,
						 hBufferInt,
						 PVRSRV_HANDLE_TYPE_DISP_BUFFER,
						 (PVRSRV_HANDLE_ALLOC_FLAG)(PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE | PVRSRV_HANDLE_ALLOC_FLAG_SHARED),
						 psGetDispClassSysBufferIN->hDeviceKM);

	COMMIT_HANDLE_BATCH_OR_ERROR(psGetDispClassSysBufferOUT->eError, psPerProc)

	return 0;
}
#endif

static IMG_INT
PVRSRVGetDCInfoBW(IMG_UINT32 ui32BridgeID,
				  PVRSRV_BRIDGE_IN_GET_DISPCLASS_INFO *psGetDispClassInfoIN,
				  PVRSRV_BRIDGE_OUT_GET_DISPCLASS_INFO *psGetDispClassInfoOUT,
				  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvDispClassInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_GET_DISPCLASS_INFO);

	psGetDispClassInfoOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvDispClassInfo,
						   psGetDispClassInfoIN->hDeviceKM,
						   PVRSRV_HANDLE_TYPE_DISP_INFO);
	if(psGetDispClassInfoOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psGetDispClassInfoOUT->eError =
		PVRSRVGetDCInfoKM(pvDispClassInfo,
						  &psGetDispClassInfoOUT->sDisplayInfo);

	return 0;
}

static IMG_INT
PVRSRVCreateDCSwapChainBW(IMG_UINT32 ui32BridgeID,
						  PVRSRV_BRIDGE_IN_CREATE_DISPCLASS_SWAPCHAIN *psCreateDispClassSwapChainIN,
						  PVRSRV_BRIDGE_OUT_CREATE_DISPCLASS_SWAPCHAIN *psCreateDispClassSwapChainOUT,
						  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvDispClassInfo;
	IMG_HANDLE hSwapChainInt;
	IMG_UINT32	ui32SwapChainID;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_CREATE_DISPCLASS_SWAPCHAIN);

	NEW_HANDLE_BATCH_OR_ERROR(psCreateDispClassSwapChainOUT->eError, psPerProc, 1)

	psCreateDispClassSwapChainOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvDispClassInfo,
						   psCreateDispClassSwapChainIN->hDeviceKM,
						   PVRSRV_HANDLE_TYPE_DISP_INFO);

	if(psCreateDispClassSwapChainOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	/* Get ui32SwapChainID from input */
	ui32SwapChainID = psCreateDispClassSwapChainIN->ui32SwapChainID;

	psCreateDispClassSwapChainOUT->eError =
		PVRSRVCreateDCSwapChainKM(psPerProc, pvDispClassInfo,
								  psCreateDispClassSwapChainIN->ui32Flags,
								  &psCreateDispClassSwapChainIN->sDstSurfAttrib,
								  &psCreateDispClassSwapChainIN->sSrcSurfAttrib,
								  psCreateDispClassSwapChainIN->ui32BufferCount,
								  psCreateDispClassSwapChainIN->ui32OEMFlags,
								  &hSwapChainInt,
								  &ui32SwapChainID);

	if(psCreateDispClassSwapChainOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	/* Pass ui32SwapChainID to output */
	psCreateDispClassSwapChainOUT->ui32SwapChainID = ui32SwapChainID;

	PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
					  &psCreateDispClassSwapChainOUT->hSwapChain,
					  hSwapChainInt,
					  PVRSRV_HANDLE_TYPE_DISP_SWAP_CHAIN,
					  PVRSRV_HANDLE_ALLOC_FLAG_NONE,
					  psCreateDispClassSwapChainIN->hDeviceKM);

	COMMIT_HANDLE_BATCH_OR_ERROR(psCreateDispClassSwapChainOUT->eError, psPerProc)

	return 0;
}

static IMG_INT
PVRSRVDestroyDCSwapChainBW(IMG_UINT32 ui32BridgeID,
						   PVRSRV_BRIDGE_IN_DESTROY_DISPCLASS_SWAPCHAIN *psDestroyDispClassSwapChainIN,
						   PVRSRV_BRIDGE_RETURN *psRetOUT,
						   PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvSwapChain;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DESTROY_DISPCLASS_SWAPCHAIN);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase, &pvSwapChain,
						   psDestroyDispClassSwapChainIN->hSwapChain,
						   PVRSRV_HANDLE_TYPE_DISP_SWAP_CHAIN);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PVRSRVDestroyDCSwapChainKM(pvSwapChain);

	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PVRSRVReleaseHandle(psPerProc->psHandleBase,
							psDestroyDispClassSwapChainIN->hSwapChain,
							PVRSRV_HANDLE_TYPE_DISP_SWAP_CHAIN);

	return 0;
}

static IMG_INT
PVRSRVSetDCDstRectBW(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SET_DISPCLASS_RECT *psSetDispClassDstRectIN,
					 PVRSRV_BRIDGE_RETURN *psRetOUT,
					 PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvDispClassInfo;
	IMG_VOID *pvSwapChain;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SET_DISPCLASS_DSTRECT);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvDispClassInfo,
						   psSetDispClassDstRectIN->hDeviceKM,
						   PVRSRV_HANDLE_TYPE_DISP_INFO);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvSwapChain,
						   psSetDispClassDstRectIN->hSwapChain,
						   PVRSRV_HANDLE_TYPE_DISP_SWAP_CHAIN);

	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PVRSRVSetDCDstRectKM(pvDispClassInfo,
							 pvSwapChain,
							 &psSetDispClassDstRectIN->sRect);

	return 0;
}

static IMG_INT
PVRSRVSetDCSrcRectBW(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SET_DISPCLASS_RECT *psSetDispClassSrcRectIN,
					 PVRSRV_BRIDGE_RETURN *psRetOUT,
					 PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvDispClassInfo;
	IMG_VOID *pvSwapChain;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SET_DISPCLASS_SRCRECT);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvDispClassInfo,
						   psSetDispClassSrcRectIN->hDeviceKM,
						   PVRSRV_HANDLE_TYPE_DISP_INFO);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvSwapChain,
						   psSetDispClassSrcRectIN->hSwapChain,
						   PVRSRV_HANDLE_TYPE_DISP_SWAP_CHAIN);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PVRSRVSetDCSrcRectKM(pvDispClassInfo,
							 pvSwapChain,
							 &psSetDispClassSrcRectIN->sRect);

	return 0;
}

static IMG_INT
PVRSRVSetDCDstColourKeyBW(IMG_UINT32 ui32BridgeID,
						  PVRSRV_BRIDGE_IN_SET_DISPCLASS_COLOURKEY *psSetDispClassColKeyIN,
						  PVRSRV_BRIDGE_RETURN *psRetOUT,
						  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvDispClassInfo;
	IMG_VOID *pvSwapChain;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SET_DISPCLASS_DSTCOLOURKEY);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvDispClassInfo,
						   psSetDispClassColKeyIN->hDeviceKM,
						   PVRSRV_HANDLE_TYPE_DISP_INFO);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvSwapChain,
						   psSetDispClassColKeyIN->hSwapChain,
						   PVRSRV_HANDLE_TYPE_DISP_SWAP_CHAIN);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PVRSRVSetDCDstColourKeyKM(pvDispClassInfo,
								  pvSwapChain,
								  psSetDispClassColKeyIN->ui32CKColour);

	return 0;
}

static IMG_INT
PVRSRVSetDCSrcColourKeyBW(IMG_UINT32 ui32BridgeID,
						  PVRSRV_BRIDGE_IN_SET_DISPCLASS_COLOURKEY *psSetDispClassColKeyIN,
						  PVRSRV_BRIDGE_RETURN *psRetOUT,
						  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvDispClassInfo;
	IMG_VOID *pvSwapChain;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SET_DISPCLASS_SRCCOLOURKEY);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvDispClassInfo,
						   psSetDispClassColKeyIN->hDeviceKM,
						   PVRSRV_HANDLE_TYPE_DISP_INFO);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvSwapChain,
						   psSetDispClassColKeyIN->hSwapChain,
						   PVRSRV_HANDLE_TYPE_DISP_SWAP_CHAIN);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PVRSRVSetDCSrcColourKeyKM(pvDispClassInfo,
								  pvSwapChain,
								  psSetDispClassColKeyIN->ui32CKColour);

	return 0;
}

static IMG_INT
PVRSRVGetDCBuffersBW(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_GET_DISPCLASS_BUFFERS *psGetDispClassBuffersIN,
					 PVRSRV_BRIDGE_OUT_GET_DISPCLASS_BUFFERS *psGetDispClassBuffersOUT,
					 PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvDispClassInfo;
	IMG_VOID *pvSwapChain;
	IMG_UINT32 i;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_GET_DISPCLASS_BUFFERS);

	NEW_HANDLE_BATCH_OR_ERROR(psGetDispClassBuffersOUT->eError, psPerProc, PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS)

	psGetDispClassBuffersOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvDispClassInfo,
						   psGetDispClassBuffersIN->hDeviceKM,
						   PVRSRV_HANDLE_TYPE_DISP_INFO);
	if(psGetDispClassBuffersOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psGetDispClassBuffersOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvSwapChain,
						   psGetDispClassBuffersIN->hSwapChain,
						   PVRSRV_HANDLE_TYPE_DISP_SWAP_CHAIN);
	if(psGetDispClassBuffersOUT->eError != PVRSRV_OK)
	{
		return 0;
	}


	psGetDispClassBuffersOUT->eError =
		PVRSRVGetDCBuffersKM(pvDispClassInfo,
							 pvSwapChain,
							 &psGetDispClassBuffersOUT->ui32BufferCount,
							 psGetDispClassBuffersOUT->ahBuffer,
							 psGetDispClassBuffersOUT->asPhyAddr);
	if (psGetDispClassBuffersOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	PVR_ASSERT(psGetDispClassBuffersOUT->ui32BufferCount <= PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS);

	for(i = 0; i < psGetDispClassBuffersOUT->ui32BufferCount; i++)
	{
		IMG_HANDLE hBufferExt;

	/* PRQA S 1461 15 */ /* ignore warning about enum type being converted */
		PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
							 &hBufferExt,
							 psGetDispClassBuffersOUT->ahBuffer[i],
							 PVRSRV_HANDLE_TYPE_DISP_BUFFER,
							 (PVRSRV_HANDLE_ALLOC_FLAG)(PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE | PVRSRV_HANDLE_ALLOC_FLAG_SHARED),
							 psGetDispClassBuffersIN->hSwapChain);

		psGetDispClassBuffersOUT->ahBuffer[i] = hBufferExt;
	}


	COMMIT_HANDLE_BATCH_OR_ERROR(psGetDispClassBuffersOUT->eError, psPerProc)

	return 0;
}

static IMG_INT
PVRSRVSwapToDCBufferBW(IMG_UINT32 ui32BridgeID,
					   PVRSRV_BRIDGE_IN_SWAP_DISPCLASS_TO_BUFFER *psSwapDispClassBufferIN,
					   PVRSRV_BRIDGE_RETURN *psRetOUT,
					   PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvDispClassInfo;
	IMG_VOID *pvSwapChainBuf;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SWAP_DISPCLASS_TO_BUFFER);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvDispClassInfo,
						   psSwapDispClassBufferIN->hDeviceKM,
						   PVRSRV_HANDLE_TYPE_DISP_INFO);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PVRSRVLookupSubHandle(psPerProc->psHandleBase,
						   &pvSwapChainBuf,
						   psSwapDispClassBufferIN->hBuffer,
						   PVRSRV_HANDLE_TYPE_DISP_BUFFER,
						   psSwapDispClassBufferIN->hDeviceKM);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}


	psRetOUT->eError =
		PVRSRVSwapToDCBufferKM(pvDispClassInfo,
							   pvSwapChainBuf,
							   psSwapDispClassBufferIN->ui32SwapInterval,
							   psSwapDispClassBufferIN->hPrivateTag,
							   psSwapDispClassBufferIN->ui32ClipRectCount,
							   psSwapDispClassBufferIN->sClipRect);

	return 0;
}

static IMG_INT
PVRSRVSwapToDCBuffer2BW(IMG_UINT32 ui32BridgeID,
						PVRSRV_BRIDGE_IN_SWAP_DISPCLASS_TO_BUFFER2 *psSwapDispClassBufferIN,
						PVRSRV_BRIDGE_RETURN *psRetOUT,
						PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvPrivData = IMG_NULL;
	IMG_VOID *pvDispClassInfo;
	IMG_VOID *pvSwapChain;
	IMG_UINT32 i;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SWAP_DISPCLASS_TO_BUFFER2);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvDispClassInfo,
						   psSwapDispClassBufferIN->hDeviceKM,
						   PVRSRV_HANDLE_TYPE_DISP_INFO);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVSwapToDCBuffer2BW: Failed to look up DISP_INFO handle"));
		return 0;
	}

	psRetOUT->eError =
		PVRSRVLookupSubHandle(psPerProc->psHandleBase,
							  &pvSwapChain,
							  psSwapDispClassBufferIN->hSwapChain,
							  PVRSRV_HANDLE_TYPE_DISP_SWAP_CHAIN,
							  psSwapDispClassBufferIN->hDeviceKM);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVSwapToDCBuffer2BW: Failed to look up DISP_BUFFER handle"));
		return 0;
	}

	if(!OSAccessOK(PVR_VERIFY_WRITE,
				   psSwapDispClassBufferIN->ppsKernelMemInfos,
				   sizeof(IMG_HANDLE) * psSwapDispClassBufferIN->ui32NumMemInfos))
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVSwapToDCBuffer2BW: Access check failed for ppsKernelMemInfos"));
		return -EFAULT;
	}

	if(!OSAccessOK(PVR_VERIFY_WRITE,
				   psSwapDispClassBufferIN->ppsKernelSyncInfos,
				   sizeof(IMG_HANDLE) * psSwapDispClassBufferIN->ui32NumMemInfos))
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVSwapToDCBuffer2BW: Access check failed for ppsKernelSyncInfos"));
		return -EFAULT;
	}

	for (i = 0; i < psSwapDispClassBufferIN->ui32NumMemInfos; i++)
	{
		PVRSRV_KERNEL_SYNC_INFO *psKernelSyncInfo;
		PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo;

		psRetOUT->eError =
			PVRSRVLookupHandle(psPerProc->psHandleBase,
							   (IMG_PVOID *)&psKernelMemInfo,
							   psSwapDispClassBufferIN->ppsKernelMemInfos[i],
							   PVRSRV_HANDLE_TYPE_MEM_INFO);
		if(psRetOUT->eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVSwapToDCBuffer2BW: Failed to look up MEM_INFO handle"));
			return 0;
		}

		psRetOUT->eError =
			PVRSRVLookupHandle(psPerProc->psHandleBase,
							   (IMG_PVOID *)&psKernelSyncInfo,
							   psSwapDispClassBufferIN->ppsKernelSyncInfos[i],
							   PVRSRV_HANDLE_TYPE_SYNC_INFO);
		if(psRetOUT->eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVSwapToDCBuffer2BW: Failed to look up SYNC_INFO handle"));
			return 0;
		}

		psSwapDispClassBufferIN->ppsKernelMemInfos[i] = psKernelMemInfo;
		psSwapDispClassBufferIN->ppsKernelSyncInfos[i] = psKernelSyncInfo;
	}

	if(psSwapDispClassBufferIN->ui32PrivDataLength > 0)
	{
		if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
					  psSwapDispClassBufferIN->ui32PrivDataLength,
					  (IMG_VOID **)&pvPrivData, IMG_NULL,
					  "Swap Command Private Data") != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCBuffer2BW: Failed to allocate private data space"));
			return -ENOMEM;
		}

		if(CopyFromUserWrapper(psPerProc,
							   ui32BridgeID,
							   pvPrivData,
							   psSwapDispClassBufferIN->pvPrivData,
							   psSwapDispClassBufferIN->ui32PrivDataLength) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVSwapToDCBuffer2BW: Failed to copy private data"));
			OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
					  psSwapDispClassBufferIN->ui32PrivDataLength,
					  pvPrivData, IMG_NULL);
	        return -EFAULT;
		}
	}

	psRetOUT->eError =
		PVRSRVSwapToDCBuffer2KM(pvDispClassInfo,
								pvSwapChain,
								psSwapDispClassBufferIN->ui32SwapInterval,
								psSwapDispClassBufferIN->ppsKernelMemInfos,
								psSwapDispClassBufferIN->ppsKernelSyncInfos,
								psSwapDispClassBufferIN->ui32NumMemInfos,
								pvPrivData,
								psSwapDispClassBufferIN->ui32PrivDataLength);

	if(psRetOUT->eError != PVRSRV_OK)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
				  psSwapDispClassBufferIN->ui32PrivDataLength,
				  pvPrivData, IMG_NULL);
	}

    return 0;
}



static IMG_INT
PVRSRVSwapToDCSystemBW(IMG_UINT32 ui32BridgeID,
					   PVRSRV_BRIDGE_IN_SWAP_DISPCLASS_TO_SYSTEM *psSwapDispClassSystemIN,
					   PVRSRV_BRIDGE_RETURN *psRetOUT,
					   PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvDispClassInfo;
	IMG_VOID *pvSwapChain;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SWAP_DISPCLASS_TO_SYSTEM);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvDispClassInfo,
						   psSwapDispClassSystemIN->hDeviceKM,
						   PVRSRV_HANDLE_TYPE_DISP_INFO);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PVRSRVLookupSubHandle(psPerProc->psHandleBase,
						   &pvSwapChain,
						   psSwapDispClassSystemIN->hSwapChain,
						   PVRSRV_HANDLE_TYPE_DISP_SWAP_CHAIN,
						   psSwapDispClassSystemIN->hDeviceKM);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}
	psRetOUT->eError =
		PVRSRVSwapToDCSystemKM(pvDispClassInfo,
							   pvSwapChain);

	return 0;
}

static IMG_INT
PVRSRVOpenBCDeviceBW(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_OPEN_BUFFERCLASS_DEVICE *psOpenBufferClassDeviceIN,
					 PVRSRV_BRIDGE_OUT_OPEN_BUFFERCLASS_DEVICE *psOpenBufferClassDeviceOUT,
					 PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_HANDLE hDevCookieInt;
	IMG_HANDLE hBufClassInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_OPEN_BUFFERCLASS_DEVICE);

	NEW_HANDLE_BATCH_OR_ERROR(psOpenBufferClassDeviceOUT->eError, psPerProc, 1)

	psOpenBufferClassDeviceOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &hDevCookieInt,
						   psOpenBufferClassDeviceIN->hDevCookie,
						   PVRSRV_HANDLE_TYPE_DEV_NODE);
	if(psOpenBufferClassDeviceOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psOpenBufferClassDeviceOUT->eError =
		PVRSRVOpenBCDeviceKM(psPerProc,
							 psOpenBufferClassDeviceIN->ui32DeviceID,
							 hDevCookieInt,
							 &hBufClassInfo);
	if(psOpenBufferClassDeviceOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	PVRSRVAllocHandleNR(psPerProc->psHandleBase,
					  &psOpenBufferClassDeviceOUT->hDeviceKM,
					  hBufClassInfo,
					  PVRSRV_HANDLE_TYPE_BUF_INFO,
					  PVRSRV_HANDLE_ALLOC_FLAG_NONE);

	COMMIT_HANDLE_BATCH_OR_ERROR(psOpenBufferClassDeviceOUT->eError, psPerProc)

	return 0;
}

static IMG_INT
PVRSRVCloseBCDeviceBW(IMG_UINT32 ui32BridgeID,
					  PVRSRV_BRIDGE_IN_CLOSE_BUFFERCLASS_DEVICE *psCloseBufferClassDeviceIN,
					  PVRSRV_BRIDGE_RETURN *psRetOUT,
					  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvBufClassInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_CLOSE_BUFFERCLASS_DEVICE);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvBufClassInfo,
						   psCloseBufferClassDeviceIN->hDeviceKM,
						   PVRSRV_HANDLE_TYPE_BUF_INFO);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError =
		PVRSRVCloseBCDeviceKM(pvBufClassInfo);

	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError = PVRSRVReleaseHandle(psPerProc->psHandleBase,
										   psCloseBufferClassDeviceIN->hDeviceKM,
										   PVRSRV_HANDLE_TYPE_BUF_INFO);

	return 0;
}

static IMG_INT
PVRSRVGetBCInfoBW(IMG_UINT32 ui32BridgeID,
				  PVRSRV_BRIDGE_IN_GET_BUFFERCLASS_INFO *psGetBufferClassInfoIN,
				  PVRSRV_BRIDGE_OUT_GET_BUFFERCLASS_INFO *psGetBufferClassInfoOUT,
				  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvBufClassInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_GET_BUFFERCLASS_INFO);

	psGetBufferClassInfoOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvBufClassInfo,
						   psGetBufferClassInfoIN->hDeviceKM,
						   PVRSRV_HANDLE_TYPE_BUF_INFO);
	if(psGetBufferClassInfoOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psGetBufferClassInfoOUT->eError =
		PVRSRVGetBCInfoKM(pvBufClassInfo,
						  &psGetBufferClassInfoOUT->sBufferInfo);
	return 0;
}

static IMG_INT
PVRSRVGetBCBufferBW(IMG_UINT32 ui32BridgeID,
					PVRSRV_BRIDGE_IN_GET_BUFFERCLASS_BUFFER *psGetBufferClassBufferIN,
					PVRSRV_BRIDGE_OUT_GET_BUFFERCLASS_BUFFER *psGetBufferClassBufferOUT,
					PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_VOID *pvBufClassInfo;
	IMG_HANDLE hBufferInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_GET_BUFFERCLASS_BUFFER);

	NEW_HANDLE_BATCH_OR_ERROR(psGetBufferClassBufferOUT->eError, psPerProc, 1)

	psGetBufferClassBufferOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &pvBufClassInfo,
						   psGetBufferClassBufferIN->hDeviceKM,
						   PVRSRV_HANDLE_TYPE_BUF_INFO);
	if(psGetBufferClassBufferOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psGetBufferClassBufferOUT->eError =
		PVRSRVGetBCBufferKM(pvBufClassInfo,
							psGetBufferClassBufferIN->ui32BufferIndex,
							&hBufferInt);

	if(psGetBufferClassBufferOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	/* PRQA S 1461 6 */ /* ignore warning about enum type being converted */
	PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
						 &psGetBufferClassBufferOUT->hBuffer,
						 hBufferInt,
						 PVRSRV_HANDLE_TYPE_BUF_BUFFER,
						 (PVRSRV_HANDLE_ALLOC_FLAG)(PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE |  PVRSRV_HANDLE_ALLOC_FLAG_SHARED),
						 psGetBufferClassBufferIN->hDeviceKM);

	COMMIT_HANDLE_BATCH_OR_ERROR(psGetBufferClassBufferOUT->eError, psPerProc)

	return 0;
}


static IMG_INT
PVRSRVAllocSharedSysMemoryBW(IMG_UINT32 ui32BridgeID,
							 PVRSRV_BRIDGE_IN_ALLOC_SHARED_SYS_MEM *psAllocSharedSysMemIN,
							 PVRSRV_BRIDGE_OUT_ALLOC_SHARED_SYS_MEM *psAllocSharedSysMemOUT,
							 PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_ALLOC_SHARED_SYS_MEM);

	NEW_HANDLE_BATCH_OR_ERROR(psAllocSharedSysMemOUT->eError, psPerProc, 1)

	psAllocSharedSysMemOUT->eError =
		PVRSRVAllocSharedSysMemoryKM(psPerProc,
									 psAllocSharedSysMemIN->ui32Flags,
									 psAllocSharedSysMemIN->uSize,
									 &psKernelMemInfo);
	if(psAllocSharedSysMemOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	OSMemSet(&psAllocSharedSysMemOUT->sClientMemInfo,
			 0,
			 sizeof(psAllocSharedSysMemOUT->sClientMemInfo));

	psAllocSharedSysMemOUT->sClientMemInfo.pvLinAddrKM =
			psKernelMemInfo->pvLinAddrKM;

	psAllocSharedSysMemOUT->sClientMemInfo.pvLinAddr = 0;
	psAllocSharedSysMemOUT->sClientMemInfo.ui32Flags =
		psKernelMemInfo->ui32Flags;
    psAllocSharedSysMemOUT->sClientMemInfo.uAllocSize =
        psKernelMemInfo->uAllocSize;
	psAllocSharedSysMemOUT->sClientMemInfo.hMappingInfo = psKernelMemInfo->sMemBlk.hOSMemHandle;

	PVRSRVAllocHandleNR(psPerProc->psHandleBase,
					  &psAllocSharedSysMemOUT->sClientMemInfo.hKernelMemInfo,
					  psKernelMemInfo,
					  PVRSRV_HANDLE_TYPE_SHARED_SYS_MEM_INFO,
					  PVRSRV_HANDLE_ALLOC_FLAG_NONE);

	COMMIT_HANDLE_BATCH_OR_ERROR(psAllocSharedSysMemOUT->eError, psPerProc)

	return 0;
}

static IMG_INT
PVRSRVFreeSharedSysMemoryBW(IMG_UINT32 ui32BridgeID,
							PVRSRV_BRIDGE_IN_FREE_SHARED_SYS_MEM *psFreeSharedSysMemIN,
							PVRSRV_BRIDGE_OUT_FREE_SHARED_SYS_MEM *psFreeSharedSysMemOUT,
							PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_FREE_SHARED_SYS_MEM);

	psFreeSharedSysMemOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   (IMG_VOID **)&psKernelMemInfo,
						   psFreeSharedSysMemIN->psKernelMemInfo,
						   PVRSRV_HANDLE_TYPE_SHARED_SYS_MEM_INFO);

	if(psFreeSharedSysMemOUT->eError != PVRSRV_OK)
		return 0;

	psFreeSharedSysMemOUT->eError =
		PVRSRVFreeSharedSysMemoryKM(psKernelMemInfo);
	if(psFreeSharedSysMemOUT->eError != PVRSRV_OK)
		return 0;

	psFreeSharedSysMemOUT->eError =
		PVRSRVReleaseHandle(psPerProc->psHandleBase,
							psFreeSharedSysMemIN->psKernelMemInfo,
							PVRSRV_HANDLE_TYPE_SHARED_SYS_MEM_INFO);
	return 0;
}

static IMG_INT
PVRSRVMapMemInfoMemBW(IMG_UINT32 ui32BridgeID,
					  PVRSRV_BRIDGE_IN_MAP_MEMINFO_MEM *psMapMemInfoMemIN,
					  PVRSRV_BRIDGE_OUT_MAP_MEMINFO_MEM *psMapMemInfoMemOUT,
					  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo;
	PVRSRV_HANDLE_TYPE eHandleType;
	IMG_HANDLE	hParent;
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MAP_MEMINFO_MEM);

	NEW_HANDLE_BATCH_OR_ERROR(psMapMemInfoMemOUT->eError, psPerProc, 2)

	psMapMemInfoMemOUT->eError =
		PVRSRVLookupHandleAnyType(psPerProc->psHandleBase,
						   (IMG_VOID **)&psKernelMemInfo,
						   &eHandleType,
						   psMapMemInfoMemIN->hKernelMemInfo);
	if(psMapMemInfoMemOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	switch (eHandleType)
	{
#if defined(PVR_SECURE_HANDLES)
		case PVRSRV_HANDLE_TYPE_MEM_INFO:
		case PVRSRV_HANDLE_TYPE_MEM_INFO_REF:
		case PVRSRV_HANDLE_TYPE_SHARED_SYS_MEM_INFO:
#else
		case PVRSRV_HANDLE_TYPE_NONE:
#endif
			break;
		default:
			psMapMemInfoMemOUT->eError = PVRSRV_ERROR_INVALID_HANDLE_TYPE;
			return 0;
	}

	/*
	 * To prevent the building up of deep chains of subhandles, parent
	 * the new meminfo off the parent of the input meminfo, if it has
	 * a parent.
	 */
	psMapMemInfoMemOUT->eError =
		PVRSRVGetParentHandle(psPerProc->psHandleBase,
					&hParent,
					psMapMemInfoMemIN->hKernelMemInfo,
					eHandleType);
	if (psMapMemInfoMemOUT->eError != PVRSRV_OK)
	{
		return 0;
	}
	if (hParent == IMG_NULL)
	{
		hParent = psMapMemInfoMemIN->hKernelMemInfo;
	}

	OSMemSet(&psMapMemInfoMemOUT->sClientMemInfo,
			 0,
			 sizeof(psMapMemInfoMemOUT->sClientMemInfo));

	psMapMemInfoMemOUT->sClientMemInfo.pvLinAddrKM =
			psKernelMemInfo->pvLinAddrKM;

	psMapMemInfoMemOUT->sClientMemInfo.pvLinAddr = 0;
	psMapMemInfoMemOUT->sClientMemInfo.sDevVAddr =
		psKernelMemInfo->sDevVAddr;
	psMapMemInfoMemOUT->sClientMemInfo.ui32Flags =
		psKernelMemInfo->ui32Flags;
    psMapMemInfoMemOUT->sClientMemInfo.uAllocSize =
        psKernelMemInfo->uAllocSize;
	psMapMemInfoMemOUT->sClientMemInfo.hMappingInfo = psKernelMemInfo->sMemBlk.hOSMemHandle;

	PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
					  &psMapMemInfoMemOUT->sClientMemInfo.hKernelMemInfo,
					  psKernelMemInfo,
					  PVRSRV_HANDLE_TYPE_MEM_INFO_REF,
					  PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
					  hParent);

	if(psKernelMemInfo->ui32Flags & PVRSRV_MEM_NO_SYNCOBJ)
	{
		/* signal no syncinfo */
		OSMemSet(&psMapMemInfoMemOUT->sClientSyncInfo,
				 0,
				 sizeof (PVRSRV_CLIENT_SYNC_INFO));
	}
	else
	{
		/* and setup the sync info */
#if !defined(PVRSRV_DISABLE_UM_SYNCOBJ_MAPPINGS)
		psMapMemInfoMemOUT->sClientSyncInfo.psSyncData =
			psKernelMemInfo->psKernelSyncInfo->psSyncData;
		psMapMemInfoMemOUT->sClientSyncInfo.sWriteOpsCompleteDevVAddr =
			psKernelMemInfo->psKernelSyncInfo->sWriteOpsCompleteDevVAddr;
		psMapMemInfoMemOUT->sClientSyncInfo.sReadOpsCompleteDevVAddr =
			psKernelMemInfo->psKernelSyncInfo->sReadOpsCompleteDevVAddr;
		psMapMemInfoMemOUT->sClientSyncInfo.sReadOps2CompleteDevVAddr =
			psKernelMemInfo->psKernelSyncInfo->sReadOps2CompleteDevVAddr;

		psMapMemInfoMemOUT->sClientSyncInfo.hMappingInfo =
			psKernelMemInfo->psKernelSyncInfo->psSyncDataMemInfoKM->sMemBlk.hOSMemHandle;
#endif

		psMapMemInfoMemOUT->sClientMemInfo.psClientSyncInfo = &psMapMemInfoMemOUT->sClientSyncInfo;

		PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
							 &psMapMemInfoMemOUT->sClientSyncInfo.hKernelSyncInfo,
							 psKernelMemInfo->psKernelSyncInfo,
							 PVRSRV_HANDLE_TYPE_SYNC_INFO,
							 PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
							 psMapMemInfoMemOUT->sClientMemInfo.hKernelMemInfo);
	}

	COMMIT_HANDLE_BATCH_OR_ERROR(psMapMemInfoMemOUT->eError, psPerProc)

	return 0;
}



IMG_INT
DummyBW(IMG_UINT32 ui32BridgeID,
		IMG_VOID *psBridgeIn,
		IMG_VOID *psBridgeOut,
		PVRSRV_PER_PROCESS_DATA *psPerProc)
{
#if !defined(DEBUG)
	PVR_UNREFERENCED_PARAMETER(ui32BridgeID);
#endif
	PVR_UNREFERENCED_PARAMETER(psBridgeIn);
	PVR_UNREFERENCED_PARAMETER(psBridgeOut);
	PVR_UNREFERENCED_PARAMETER(psPerProc);

#if defined(DEBUG_BRIDGE_KM)
	PVR_DPF((PVR_DBG_ERROR, "%s: BRIDGE ERROR: BridgeID %u (%s) mapped to "
			 "Dummy Wrapper (probably not what you want!)",
			 __FUNCTION__, ui32BridgeID, g_BridgeDispatchTable[ui32BridgeID].pszIOCName));
#else
	PVR_DPF((PVR_DBG_ERROR, "%s: BRIDGE ERROR: BridgeID %u mapped to "
			 "Dummy Wrapper (probably not what you want!)",
			 __FUNCTION__, ui32BridgeID));
#endif
	return -ENOTTY;
}


/*!
 * *****************************************************************************
 * @brief A wrapper for filling in the g_BridgeDispatchTable array that does
 * 		  error checking.
 *
 * @param ui32Index
 * @param pszIOCName
 * @param pfFunction
 * @param pszFunctionName
 *
 * @return
 ********************************************************************************/
IMG_VOID
_SetDispatchTableEntry(IMG_UINT32 ui32Index,
					   const IMG_CHAR *pszIOCName,
					   BridgeWrapperFunction pfFunction,
					   const IMG_CHAR *pszFunctionName)
{
	static IMG_UINT uiPrevIndex = ~0U;		/* -1 */
#if !defined(DEBUG)
	PVR_UNREFERENCED_PARAMETER(pszIOCName);
#endif
#if !defined(DEBUG_BRIDGE_KM_DISPATCH_TABLE) && !defined(DEBUG_BRIDGE_KM)
	PVR_UNREFERENCED_PARAMETER(pszFunctionName);
#endif

#if defined(DEBUG_BRIDGE_KM_DISPATCH_TABLE)
	/* INTEGRATION_POINT: Enable this to dump out the dispatch table entries */
	PVR_DPF((PVR_DBG_WARNING, "%s: %d %s %s", __FUNCTION__, ui32Index, pszIOCName, pszFunctionName));
#endif

	/* We should never be over-writing a previous entry.
	 * If we are, tell the world about it.
	 * NOTE: This shouldn't be debug only since switching from debug->release
	 * etc is likly to modify the available ioctls and thus be a point where
	 * mistakes are exposed. This isn't run at at a performance critical time.
	 */
	if(g_BridgeDispatchTable[ui32Index].pfFunction)
	{
#if defined(DEBUG_BRIDGE_KM)
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: BUG!: Adding dispatch table entry for %s clobbers an existing entry for %s",
				 __FUNCTION__, pszIOCName, g_BridgeDispatchTable[ui32Index].pszIOCName));
#else
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: BUG!: Adding dispatch table entry for %s clobbers an existing entry (index=%u)",
				 __FUNCTION__, pszIOCName, ui32Index));
#endif
		PVR_DPF((PVR_DBG_ERROR, "NOTE: Enabling DEBUG_BRIDGE_KM_DISPATCH_TABLE may help debug this issue."));
	}

	/* Any gaps are sub-optimal in-terms of memory usage, but we are mainly
	 * interested in spotting any large gap of wasted memory that could be
	 * accidentally introduced.
	 *
	 * This will currently flag up any gaps > 5 entries.
	 *
	 * NOTE: This shouldn't be debug only since switching from debug->release
	 * etc is likly to modify the available ioctls and thus be a point where
	 * mistakes are exposed. This isn't run at at a performance critical time.
	 */
//	if((uiPrevIndex != (IMG_UINT)-1) &&
	if((uiPrevIndex != ~0U) &&
	   ((ui32Index >= uiPrevIndex + DISPATCH_TABLE_GAP_THRESHOLD) ||
		(ui32Index <= uiPrevIndex)))
	{
#if defined(DEBUG_BRIDGE_KM)
		PVR_DPF((PVR_DBG_WARNING,
				 "%s: There is a gap in the dispatch table between indices %u (%s) and %u (%s)",
				 __FUNCTION__, uiPrevIndex, g_BridgeDispatchTable[uiPrevIndex].pszIOCName,
				 ui32Index, pszIOCName));
#else
		PVR_DPF((PVR_DBG_WARNING,
				 "%s: There is a gap in the dispatch table between indices %u and %u (%s)",
				 __FUNCTION__, (IMG_UINT)uiPrevIndex, (IMG_UINT)ui32Index, pszIOCName));
#endif
		PVR_DPF((PVR_DBG_ERROR, "NOTE: Enabling DEBUG_BRIDGE_KM_DISPATCH_TABLE may help debug this issue."));
	}

	g_BridgeDispatchTable[ui32Index].pfFunction = pfFunction;
#if defined(DEBUG_BRIDGE_KM)
	g_BridgeDispatchTable[ui32Index].pszIOCName = pszIOCName;
	g_BridgeDispatchTable[ui32Index].pszFunctionName = pszFunctionName;
	g_BridgeDispatchTable[ui32Index].ui32CallCount = 0;
	g_BridgeDispatchTable[ui32Index].ui32CopyFromUserTotalBytes = 0;
#endif

	uiPrevIndex = ui32Index;
}

static IMG_INT
PVRSRVInitSrvConnectBW(IMG_UINT32 ui32BridgeID,
					   IMG_VOID *psBridgeIn,
					   PVRSRV_BRIDGE_RETURN *psRetOUT,
					   PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVR_UNREFERENCED_PARAMETER(psBridgeIn);

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_INITSRV_CONNECT);
	PVR_UNREFERENCED_PARAMETER(psBridgeIn);

	/* PRQA S 3415 1 */ /* side effects needed - if any step fails */
	if((OSProcHasPrivSrvInit() == IMG_FALSE) || PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_RUNNING) || PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_RAN))
	{
		psRetOUT->eError = PVRSRV_ERROR_SRV_CONNECT_FAILED;
		return 0;
	}

#if defined (__linux__) || defined (__QNXNTO__)
	PVRSRVSetInitServerState(PVRSRV_INIT_SERVER_RUNNING, IMG_TRUE);
#endif
	psPerProc->bInitProcess = IMG_TRUE;

	psRetOUT->eError = PVRSRV_OK;

	return 0;
}


static IMG_INT
PVRSRVInitSrvDisconnectBW(IMG_UINT32 ui32BridgeID,
						  PVRSRV_BRIDGE_IN_INITSRV_DISCONNECT *psInitSrvDisconnectIN,
						  PVRSRV_BRIDGE_RETURN *psRetOUT,
						  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_INITSRV_DISCONNECT);

	if(!psPerProc->bInitProcess)
	{
		psRetOUT->eError = PVRSRV_ERROR_SRV_DISCONNECT_FAILED;
		return 0;
	}

	psPerProc->bInitProcess = IMG_FALSE;

#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
    psPerProc->bPDumpActive = IMG_FALSE;
#endif

	PVRSRVSetInitServerState(PVRSRV_INIT_SERVER_RUNNING, IMG_FALSE);
	PVRSRVSetInitServerState(PVRSRV_INIT_SERVER_RAN, IMG_TRUE);

	psRetOUT->eError = PVRSRVFinaliseSystem(psInitSrvDisconnectIN->bInitSuccesful);

	PVRSRVSetInitServerState( PVRSRV_INIT_SERVER_SUCCESSFUL ,
				((psRetOUT->eError == PVRSRV_OK) && (psInitSrvDisconnectIN->bInitSuccesful))
				? IMG_TRUE : IMG_FALSE);

	return 0;
}


static IMG_INT
PVRSRVEventObjectWaitBW(IMG_UINT32 ui32BridgeID,
						  PVRSRV_BRIDGE_IN_EVENT_OBJECT_WAIT *psEventObjectWaitIN,
						  PVRSRV_BRIDGE_RETURN *psRetOUT,
						  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_HANDLE hOSEventKM;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_EVENT_OBJECT_WAIT);

	psRetOUT->eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &hOSEventKM,
						   psEventObjectWaitIN->hOSEventKM,
						   PVRSRV_HANDLE_TYPE_EVENT_OBJECT_CONNECT);

	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError = OSEventObjectWaitKM(hOSEventKM);

	return 0;
}


static IMG_INT
PVRSRVEventObjectOpenBW(IMG_UINT32 ui32BridgeID,
						  PVRSRV_BRIDGE_IN_EVENT_OBJECT_OPEN *psEventObjectOpenIN,
						  PVRSRV_BRIDGE_OUT_EVENT_OBJECT_OPEN *psEventObjectOpenOUT,
						  PVRSRV_PER_PROCESS_DATA *psPerProc)
{

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_EVENT_OBJECT_OPEN);

	NEW_HANDLE_BATCH_OR_ERROR(psEventObjectOpenOUT->eError, psPerProc, 1)

	psEventObjectOpenOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &psEventObjectOpenIN->sEventObject.hOSEventKM,
						   psEventObjectOpenIN->sEventObject.hOSEventKM,
						   PVRSRV_HANDLE_TYPE_SHARED_EVENT_OBJECT);

	if(psEventObjectOpenOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psEventObjectOpenOUT->eError = OSEventObjectOpenKM(&psEventObjectOpenIN->sEventObject, &psEventObjectOpenOUT->hOSEvent);

	if(psEventObjectOpenOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	PVRSRVAllocHandleNR(psPerProc->psHandleBase,
					  &psEventObjectOpenOUT->hOSEvent,
					  psEventObjectOpenOUT->hOSEvent,
					  PVRSRV_HANDLE_TYPE_EVENT_OBJECT_CONNECT,
					  PVRSRV_HANDLE_ALLOC_FLAG_MULTI);

	COMMIT_HANDLE_BATCH_OR_ERROR(psEventObjectOpenOUT->eError, psPerProc)

	return 0;
}


static IMG_INT
PVRSRVEventObjectCloseBW(IMG_UINT32 ui32BridgeID,
						  PVRSRV_BRIDGE_IN_EVENT_OBJECT_CLOSE *psEventObjectCloseIN,
						  PVRSRV_BRIDGE_RETURN *psRetOUT,
						  PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	IMG_HANDLE hOSEventKM;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_EVENT_OBJECT_CLOSE);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase,
						   &psEventObjectCloseIN->sEventObject.hOSEventKM,
						   psEventObjectCloseIN->sEventObject.hOSEventKM,
						   PVRSRV_HANDLE_TYPE_SHARED_EVENT_OBJECT);
	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError = PVRSRVLookupAndReleaseHandle(psPerProc->psHandleBase,
						   &hOSEventKM,
						   psEventObjectCloseIN->hOSEventKM,
						   PVRSRV_HANDLE_TYPE_EVENT_OBJECT_CONNECT);

	if(psRetOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psRetOUT->eError = OSEventObjectCloseKM(&psEventObjectCloseIN->sEventObject, hOSEventKM);

	return 0;
}


typedef struct _MODIFY_SYNC_OP_INFO
{
	IMG_HANDLE  hResItem;
	PVRSRV_KERNEL_SYNC_INFO *psKernelSyncInfo;
	IMG_UINT32 	ui32ModifyFlags;
	IMG_UINT32	ui32ReadOpsPendingSnapShot;
	IMG_UINT32	ui32WriteOpsPendingSnapShot;
	IMG_UINT32	ui32ReadOps2PendingSnapShot;
} MODIFY_SYNC_OP_INFO;


static PVRSRV_ERROR DoQuerySyncOpsSatisfied(PVRSRV_KERNEL_SYNC_INFO *psKernelSyncInfo,
											IMG_UINT32 ui32ReadOpsPendingSnapShot,
											IMG_UINT32 ui32WriteOpsPendingSnapShot,
											IMG_UINT32 ui32ReadOps2PendingSnapShot)
{
	IMG_UINT32 ui32WriteOpsPending;
	IMG_UINT32 ui32ReadOpsPending;
	IMG_UINT32 ui32ReadOps2Pending;

	/*
	 *
	 * We wait until the complete count reaches _or_moves_past_ the
	 * snapshot value.
	 *
	 */

	if (!psKernelSyncInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/*
	 let p be the pending ops count
	 let c be the complete ops count
	 let p' be the previously taken snapshot

	 if p exceeds c by an amount greater than that by which
	 p exceeds p', then the condition is not yet satisfied.

	 Note that (p - c) can never be negative, and neither can (p - p')
	 so we can do the comparison using unsigned arithmetic
	*/
	ui32WriteOpsPending = psKernelSyncInfo->psSyncData->ui32WriteOpsPending;
	ui32ReadOpsPending = psKernelSyncInfo->psSyncData->ui32ReadOpsPending;
	ui32ReadOps2Pending = psKernelSyncInfo->psSyncData->ui32ReadOps2Pending;

	if((ui32WriteOpsPending - ui32WriteOpsPendingSnapShot >=
		ui32WriteOpsPending - psKernelSyncInfo->psSyncData->ui32WriteOpsComplete) &&
	   (ui32ReadOpsPending - ui32ReadOpsPendingSnapShot >=
		ui32ReadOpsPending - psKernelSyncInfo->psSyncData->ui32ReadOpsComplete) &&
		(ui32ReadOps2Pending - ui32ReadOps2PendingSnapShot >=
		ui32ReadOps2Pending - psKernelSyncInfo->psSyncData->ui32ReadOps2Complete))
	{
#if defined(PDUMP) && !defined(SUPPORT_VGX)
		/* pdump the sync pol: reads */
		PDumpComment("Poll for read ops complete to reach value (pdump: %u, actual snapshot: %u)",
					 psKernelSyncInfo->psSyncData->ui32LastReadOpDumpVal,
					 ui32ReadOpsPendingSnapShot);
		PDumpMemPolKM(psKernelSyncInfo->psSyncDataMemInfoKM,
					  offsetof(PVRSRV_SYNC_DATA, ui32ReadOpsComplete),
					  psKernelSyncInfo->psSyncData->ui32LastReadOpDumpVal,
					  0xFFFFFFFF,
					  PDUMP_POLL_OPERATOR_EQUAL, /*  * see "NB" below */
					  0,
					  MAKEUNIQUETAG(psKernelSyncInfo->psSyncDataMemInfoKM));

		/* pdump the sync pol: writes */
		PDumpComment("Poll for write ops complete to reach value (pdump: %u, actual snapshot: %u)",
					 psKernelSyncInfo->psSyncData->ui32LastOpDumpVal,
					 ui32WriteOpsPendingSnapShot);
		PDumpMemPolKM(psKernelSyncInfo->psSyncDataMemInfoKM,
					  offsetof(PVRSRV_SYNC_DATA, ui32WriteOpsComplete),
					  psKernelSyncInfo->psSyncData->ui32LastOpDumpVal,
					  0xFFFFFFFF,
					  PDUMP_POLL_OPERATOR_EQUAL, /*  * see "NB" below */
					  0,
					  MAKEUNIQUETAG(psKernelSyncInfo->psSyncDataMemInfoKM));
		/* NB: FIXME -- really need to POL on an expression to
		   accurately reflect the condition we need to check.  How to
		   do this in PDUMP? */
#endif
		return PVRSRV_OK;
	}
	else
	{
		return PVRSRV_ERROR_RETRY;
	}
}


static PVRSRV_ERROR DoModifyCompleteSyncOps(MODIFY_SYNC_OP_INFO *psModSyncOpInfo)
{
	PVRSRV_KERNEL_SYNC_INFO *psKernelSyncInfo;

	psKernelSyncInfo = psModSyncOpInfo->psKernelSyncInfo;

	if (!psKernelSyncInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* If user has used the API correctly, we will always have reached the pending snapshot.
	   We should catch this error on the client side of the bridge and report it in an obvious way */
	if((psModSyncOpInfo->ui32WriteOpsPendingSnapShot != psKernelSyncInfo->psSyncData->ui32WriteOpsComplete)
	   || (psModSyncOpInfo->ui32ReadOpsPendingSnapShot != psKernelSyncInfo->psSyncData->ui32ReadOpsComplete))
	{
		return PVRSRV_ERROR_BAD_SYNC_STATE;
	}

	/* update the WOpComplete */
	if(psModSyncOpInfo->ui32ModifyFlags & PVRSRV_MODIFYSYNCOPS_FLAGS_WO_INC)
	{
		psKernelSyncInfo->psSyncData->ui32WriteOpsComplete++;
	}

	/* update the ROpComplete */
	if(psModSyncOpInfo->ui32ModifyFlags & PVRSRV_MODIFYSYNCOPS_FLAGS_RO_INC)
	{
		psKernelSyncInfo->psSyncData->ui32ReadOpsComplete++;
	}

	return PVRSRV_OK;
}


static PVRSRV_ERROR ModifyCompleteSyncOpsCallBack(IMG_PVOID		pvParam,
                                                    IMG_UINT32  ui32Param,
                                                    IMG_BOOL    bDummy)
{
	MODIFY_SYNC_OP_INFO		*psModSyncOpInfo;

	PVR_UNREFERENCED_PARAMETER(ui32Param);
	PVR_UNREFERENCED_PARAMETER(bDummy);

	if (!pvParam)
	{
		PVR_DPF((PVR_DBG_ERROR, "ModifyCompleteSyncOpsCallBack: invalid parameter"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psModSyncOpInfo = (MODIFY_SYNC_OP_INFO*)pvParam;

	if (psModSyncOpInfo->psKernelSyncInfo)
	{
		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			if (DoQuerySyncOpsSatisfied(psModSyncOpInfo->psKernelSyncInfo,
										psModSyncOpInfo->ui32ReadOpsPendingSnapShot,
										psModSyncOpInfo->ui32WriteOpsPendingSnapShot,
										psModSyncOpInfo->ui32ReadOps2PendingSnapShot) == PVRSRV_OK)
			{
				goto OpFlushedComplete;
			}
			PVR_DPF((PVR_DBG_WARNING, "ModifyCompleteSyncOpsCallBack: waiting for current Ops to flush"));
            OSSleepms(1);
		} END_LOOP_UNTIL_TIMEOUT();

		PVR_DPF((PVR_DBG_ERROR, "ModifyCompleteSyncOpsCallBack: timeout whilst waiting for current Ops to flush."));
		PVR_DPF((PVR_DBG_ERROR, "  Write ops pending snapshot = %d, write ops complete = %d",
				 psModSyncOpInfo->ui32WriteOpsPendingSnapShot,
				 psModSyncOpInfo->psKernelSyncInfo->psSyncData->ui32WriteOpsComplete));
		PVR_DPF((PVR_DBG_ERROR, "  Read ops pending snapshot = %d, read ops complete = %d",
				 psModSyncOpInfo->ui32ReadOpsPendingSnapShot,
				 psModSyncOpInfo->psKernelSyncInfo->psSyncData->ui32ReadOpsComplete));
		PVR_DPF((PVR_DBG_ERROR, "  Read ops pending snapshot = %d, read ops2 complete = %d",
				 psModSyncOpInfo->ui32ReadOps2PendingSnapShot,
				 psModSyncOpInfo->psKernelSyncInfo->psSyncData->ui32ReadOps2Complete));
		return PVRSRV_ERROR_TIMEOUT;

OpFlushedComplete:
		DoModifyCompleteSyncOps(psModSyncOpInfo);
		PVRSRVKernelSyncInfoDecRef(psModSyncOpInfo->psKernelSyncInfo, IMG_NULL);
	}

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, 	sizeof(MODIFY_SYNC_OP_INFO), (IMG_VOID *)psModSyncOpInfo, 0);

	/* re-kick all services managed devices */
	PVRSRVScheduleDeviceCallbacks();

	return PVRSRV_OK;
}


static IMG_INT
PVRSRVCreateSyncInfoModObjBW(IMG_UINT32                                         ui32BridgeID,
									 IMG_VOID                                           *psBridgeIn,
									 PVRSRV_BRIDGE_OUT_CREATE_SYNC_INFO_MOD_OBJ  *psCreateSyncInfoModObjOUT,
									 PVRSRV_PER_PROCESS_DATA		   		            *psPerProc)
{
	MODIFY_SYNC_OP_INFO		*psModSyncOpInfo;

	PVR_UNREFERENCED_PARAMETER(psBridgeIn);

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_CREATE_SYNC_INFO_MOD_OBJ);

	NEW_HANDLE_BATCH_OR_ERROR(psCreateSyncInfoModObjOUT->eError, psPerProc, 1)

	ASSIGN_AND_EXIT_ON_ERROR(psCreateSyncInfoModObjOUT->eError,
			  OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
			  sizeof(MODIFY_SYNC_OP_INFO),
			  (IMG_VOID **)&psModSyncOpInfo, 0,
			  "ModSyncOpInfo (MODIFY_SYNC_OP_INFO)"));

	psModSyncOpInfo->psKernelSyncInfo = IMG_NULL; /* mark it as empty */

	psCreateSyncInfoModObjOUT->eError = PVRSRVAllocHandle(psPerProc->psHandleBase,
																  &psCreateSyncInfoModObjOUT->hKernelSyncInfoModObj,
																  psModSyncOpInfo,
																  PVRSRV_HANDLE_TYPE_SYNC_INFO_MOD_OBJ,
																  PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE);

	if (psCreateSyncInfoModObjOUT->eError != PVRSRV_OK)
	{
		return 0;
	}

	psModSyncOpInfo->hResItem = ResManRegisterRes(psPerProc->hResManContext,
												  RESMAN_TYPE_MODIFY_SYNC_OPS,
												  psModSyncOpInfo,
												  0,
												  &ModifyCompleteSyncOpsCallBack);

	COMMIT_HANDLE_BATCH_OR_ERROR(psCreateSyncInfoModObjOUT->eError, psPerProc)

	return 0;
}


static IMG_INT
PVRSRVDestroySyncInfoModObjBW(IMG_UINT32                                          ui32BridgeID,
							  PVRSRV_BRIDGE_IN_DESTROY_SYNC_INFO_MOD_OBJ          *psDestroySyncInfoModObjIN,
							  PVRSRV_BRIDGE_RETURN                                *psDestroySyncInfoModObjOUT,
							  PVRSRV_PER_PROCESS_DATA		   		              *psPerProc)
{
	MODIFY_SYNC_OP_INFO		*psModSyncOpInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DESTROY_SYNC_INFO_MOD_OBJ);

	psDestroySyncInfoModObjOUT->eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
																	(IMG_VOID**)&psModSyncOpInfo,
																	psDestroySyncInfoModObjIN->hKernelSyncInfoModObj,
																	PVRSRV_HANDLE_TYPE_SYNC_INFO_MOD_OBJ);
	if (psDestroySyncInfoModObjOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVDestroySyncInfoModObjBW: PVRSRVLookupHandle failed"));
		return 0;
	}

	if(psModSyncOpInfo->psKernelSyncInfo != IMG_NULL)
	{
		/* Not empty */
		psDestroySyncInfoModObjOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;
		return 0;
	}

	PVRSRVKernelSyncInfoDecRef(psModSyncOpInfo->psKernelSyncInfo, IMG_NULL);

	psDestroySyncInfoModObjOUT->eError = PVRSRVReleaseHandle(psPerProc->psHandleBase,
																	 psDestroySyncInfoModObjIN->hKernelSyncInfoModObj,
																	 PVRSRV_HANDLE_TYPE_SYNC_INFO_MOD_OBJ);

	if (psDestroySyncInfoModObjOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVDestroySyncInfoModObjBW: PVRSRVReleaseHandle failed"));
		return 0;
	}

    psDestroySyncInfoModObjOUT->eError = ResManFreeResByPtr(psModSyncOpInfo->hResItem, CLEANUP_WITH_POLL);
	if (psDestroySyncInfoModObjOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVDestroySyncInfoModObjBW: ResManFreeResByPtr failed"));
		return 0;
	}

	return 0;
}


static IMG_INT
PVRSRVModifyPendingSyncOpsBW(IMG_UINT32									ui32BridgeID,
						      PVRSRV_BRIDGE_IN_MODIFY_PENDING_SYNC_OPS	*psModifySyncOpsIN,
							  PVRSRV_BRIDGE_OUT_MODIFY_PENDING_SYNC_OPS	*psModifySyncOpsOUT,
							  PVRSRV_PER_PROCESS_DATA					*psPerProc)
{
	PVRSRV_KERNEL_SYNC_INFO *psKernelSyncInfo;
	MODIFY_SYNC_OP_INFO		*psModSyncOpInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MODIFY_PENDING_SYNC_OPS);

	psModifySyncOpsOUT->eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
													(IMG_VOID**)&psModSyncOpInfo,
													psModifySyncOpsIN->hKernelSyncInfoModObj,
													PVRSRV_HANDLE_TYPE_SYNC_INFO_MOD_OBJ);
	if (psModifySyncOpsOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVModifyPendingSyncOpsBW: PVRSRVLookupHandle failed"));
		return 0;
	}

	psModifySyncOpsOUT->eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
													(IMG_VOID**)&psKernelSyncInfo,
													psModifySyncOpsIN->hKernelSyncInfo,
													PVRSRV_HANDLE_TYPE_SYNC_INFO);
	if (psModifySyncOpsOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVModifyPendingSyncOpsBW: PVRSRVLookupHandle failed"));
		return 0;
	}

	if(psModSyncOpInfo->psKernelSyncInfo)
	{
		/* SyncInfoModification is not empty */
		psModifySyncOpsOUT->eError = PVRSRV_ERROR_RETRY;
		PVR_DPF((PVR_DBG_VERBOSE, "PVRSRVModifyPendingSyncOpsBW: SyncInfo Modification object is not empty"));
		return 0;
	}

	/* Should never happen, but check to be sure */
	if (psKernelSyncInfo == IMG_NULL)
	{
		psModifySyncOpsOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;
		PVR_DPF((PVR_DBG_VERBOSE, "PVRSRVModifyPendingSyncOpsBW: SyncInfo bad handle"));
		return 0;
	}

	PVRSRVKernelSyncInfoIncRef(psKernelSyncInfo, IMG_NULL);
	/* setup info to store in resman */
	psModSyncOpInfo->psKernelSyncInfo = psKernelSyncInfo;
	psModSyncOpInfo->ui32ModifyFlags = psModifySyncOpsIN->ui32ModifyFlags;
	psModSyncOpInfo->ui32ReadOpsPendingSnapShot = psKernelSyncInfo->psSyncData->ui32ReadOpsPending;
	psModSyncOpInfo->ui32WriteOpsPendingSnapShot = psKernelSyncInfo->psSyncData->ui32WriteOpsPending;
	psModSyncOpInfo->ui32ReadOps2PendingSnapShot = psKernelSyncInfo->psSyncData->ui32ReadOps2Pending;

	/* We return PRE-INCREMENTED versions of all sync Op Values */

	psModifySyncOpsOUT->ui32ReadOpsPending = psKernelSyncInfo->psSyncData->ui32ReadOpsPending;
	psModifySyncOpsOUT->ui32WriteOpsPending = psKernelSyncInfo->psSyncData->ui32WriteOpsPending;
	psModifySyncOpsOUT->ui32ReadOps2Pending = psKernelSyncInfo->psSyncData->ui32ReadOps2Pending;

	if(psModifySyncOpsIN->ui32ModifyFlags & PVRSRV_MODIFYSYNCOPS_FLAGS_WO_INC)
	{
		psKernelSyncInfo->psSyncData->ui32WriteOpsPending++;
	}

	if(psModifySyncOpsIN->ui32ModifyFlags & PVRSRV_MODIFYSYNCOPS_FLAGS_RO_INC)
	{
		psKernelSyncInfo->psSyncData->ui32ReadOpsPending++;
	}

	/* pull the resman item to the front of the list */
	psModifySyncOpsOUT->eError = ResManDissociateRes(psModSyncOpInfo->hResItem,
													 psPerProc->hResManContext);

	if (psModifySyncOpsOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVModifyPendingSyncOpsBW: PVRSRVLookupHandle failed"));
		return 0;
	}

	return 0;
}


static IMG_INT
PVRSRVModifyCompleteSyncOpsBW(IMG_UINT32							ui32BridgeID,
				      PVRSRV_BRIDGE_IN_MODIFY_COMPLETE_SYNC_OPS		*psModifySyncOpsIN,
					  PVRSRV_BRIDGE_RETURN							*psModifySyncOpsOUT,
					  PVRSRV_PER_PROCESS_DATA						*psPerProc)
{
	MODIFY_SYNC_OP_INFO		*psModSyncOpInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MODIFY_COMPLETE_SYNC_OPS);

	psModifySyncOpsOUT->eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
													(IMG_VOID**)&psModSyncOpInfo,
													psModifySyncOpsIN->hKernelSyncInfoModObj,
													PVRSRV_HANDLE_TYPE_SYNC_INFO_MOD_OBJ);
	if (psModifySyncOpsOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVModifyCompleteSyncOpsBW: PVRSRVLookupHandle failed"));
		return 0;
	}

	if(psModSyncOpInfo->psKernelSyncInfo == IMG_NULL)
	{
		/* Empty */
		psModifySyncOpsOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;
		return 0;
	}

	psModifySyncOpsOUT->eError = DoModifyCompleteSyncOps(psModSyncOpInfo);

	if (psModifySyncOpsOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVModifyCompleteSyncOpsBW: DoModifyCompleteSyncOps failed"));
		return 0;
	}

	PVRSRVKernelSyncInfoDecRef(psModSyncOpInfo->psKernelSyncInfo, IMG_NULL);
	psModSyncOpInfo->psKernelSyncInfo = IMG_NULL;

	/* re-kick all services managed devices */
	PVRSRVScheduleDeviceCallbacks();

	return 0;
}


static IMG_INT
PVRSRVSyncOpsTakeTokenBW(IMG_UINT32									ui32BridgeID,
						 PVRSRV_BRIDGE_IN_SYNC_OPS_TAKE_TOKEN       *psSyncOpsTakeTokenIN,
						 PVRSRV_BRIDGE_OUT_SYNC_OPS_TAKE_TOKEN      *psSyncOpsTakeTokenOUT,
						 PVRSRV_PER_PROCESS_DATA					*psPerProc)
{
	PVRSRV_KERNEL_SYNC_INFO *psKernelSyncInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_OPS_TAKE_TOKEN);

	psSyncOpsTakeTokenOUT->eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
													   (IMG_VOID**)&psKernelSyncInfo,
													   psSyncOpsTakeTokenIN->hKernelSyncInfo,
													   PVRSRV_HANDLE_TYPE_SYNC_INFO);
	if (psSyncOpsTakeTokenOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVSyncOpsTakeTokenBW: PVRSRVLookupHandle failed"));
		return 0;
	}

	/* We return PRE-INCREMENTED versions of all sync Op Values */

	psSyncOpsTakeTokenOUT->ui32ReadOpsPending = psKernelSyncInfo->psSyncData->ui32ReadOpsPending;
	psSyncOpsTakeTokenOUT->ui32WriteOpsPending = psKernelSyncInfo->psSyncData->ui32WriteOpsPending;
	psSyncOpsTakeTokenOUT->ui32ReadOps2Pending = psKernelSyncInfo->psSyncData->ui32ReadOps2Pending;

	return 0;
}


static IMG_INT
PVRSRVSyncOpsFlushToTokenBW(IMG_UINT32                                         ui32BridgeID,
							PVRSRV_BRIDGE_IN_SYNC_OPS_FLUSH_TO_TOKEN		   *psSyncOpsFlushToTokenIN,
							PVRSRV_BRIDGE_RETURN						       *psSyncOpsFlushToTokenOUT,
							PVRSRV_PER_PROCESS_DATA		   		               *psPerProc)
{
	PVRSRV_KERNEL_SYNC_INFO *psKernelSyncInfo;
	IMG_UINT32 ui32ReadOpsPendingSnapshot;
	IMG_UINT32 ui32WriteOpsPendingSnapshot;
	IMG_UINT32 ui32ReadOps2PendingSnapshot;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_OPS_FLUSH_TO_TOKEN);

	psSyncOpsFlushToTokenOUT->eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
														  (IMG_VOID**)&psKernelSyncInfo,
														  psSyncOpsFlushToTokenIN->hKernelSyncInfo,
														  PVRSRV_HANDLE_TYPE_SYNC_INFO);
	if (psSyncOpsFlushToTokenOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVSyncOpsFlushToTokenBW: PVRSRVLookupHandle failed"));
		return 0;
	}

	ui32ReadOpsPendingSnapshot = psSyncOpsFlushToTokenIN->ui32ReadOpsPendingSnapshot;
	ui32WriteOpsPendingSnapshot = psSyncOpsFlushToTokenIN->ui32WriteOpsPendingSnapshot;
	ui32ReadOps2PendingSnapshot = psSyncOpsFlushToTokenIN->ui32ReadOps2PendingSnapshot;

	psSyncOpsFlushToTokenOUT->eError = DoQuerySyncOpsSatisfied(psKernelSyncInfo,
															   ui32ReadOpsPendingSnapshot,
															   ui32WriteOpsPendingSnapshot,
															   ui32ReadOps2PendingSnapshot);

	if (psSyncOpsFlushToTokenOUT->eError != PVRSRV_OK && psSyncOpsFlushToTokenOUT->eError != PVRSRV_ERROR_RETRY)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVSyncOpsFlushToTokenBW: DoQuerySyncOpsSatisfied failed"));
		return 0;
	}

	return 0;
}


static IMG_INT
PVRSRVSyncOpsFlushToModObjBW(IMG_UINT32                                         ui32BridgeID,
							 PVRSRV_BRIDGE_IN_SYNC_OPS_FLUSH_TO_MOD_OBJ		    *psSyncOpsFlushToModObjIN,
							 PVRSRV_BRIDGE_RETURN						        *psSyncOpsFlushToModObjOUT,
							 PVRSRV_PER_PROCESS_DATA		   		            *psPerProc)
{
	MODIFY_SYNC_OP_INFO		*psModSyncOpInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_OPS_FLUSH_TO_MOD_OBJ);

	psSyncOpsFlushToModObjOUT->eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
														   (IMG_VOID**)&psModSyncOpInfo,
														   psSyncOpsFlushToModObjIN->hKernelSyncInfoModObj,
														   PVRSRV_HANDLE_TYPE_SYNC_INFO_MOD_OBJ);
	if (psSyncOpsFlushToModObjOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVSyncOpsFlushToModObjBW: PVRSRVLookupHandle failed"));
		return 0;
	}

	if(psModSyncOpInfo->psKernelSyncInfo == IMG_NULL)
	{
		/* Empty */
		psSyncOpsFlushToModObjOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;
		return 0;
	}

	psSyncOpsFlushToModObjOUT->eError = DoQuerySyncOpsSatisfied(psModSyncOpInfo->psKernelSyncInfo,
																psModSyncOpInfo->ui32ReadOpsPendingSnapShot,
																psModSyncOpInfo->ui32WriteOpsPendingSnapShot,
																psModSyncOpInfo->ui32ReadOps2PendingSnapShot);

	if (psSyncOpsFlushToModObjOUT->eError != PVRSRV_OK && psSyncOpsFlushToModObjOUT->eError != PVRSRV_ERROR_RETRY)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVSyncOpsFlushToModObjBW: DoQuerySyncOpsSatisfied failed"));
		return 0;
	}

	return 0;
}


static IMG_INT
PVRSRVSyncOpsFlushToDeltaBW(IMG_UINT32                                         ui32BridgeID,
							PVRSRV_BRIDGE_IN_SYNC_OPS_FLUSH_TO_DELTA		   *psSyncOpsFlushToDeltaIN,
							PVRSRV_BRIDGE_RETURN						       *psSyncOpsFlushToDeltaOUT,
							PVRSRV_PER_PROCESS_DATA		   		               *psPerProc)
{
	PVRSRV_KERNEL_SYNC_INFO		*psSyncInfo;
	IMG_UINT32 ui32DeltaRead;
	IMG_UINT32 ui32DeltaWrite;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_OPS_FLUSH_TO_DELTA);

	psSyncOpsFlushToDeltaOUT->eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
														  (IMG_VOID**)&psSyncInfo,
														  psSyncOpsFlushToDeltaIN->hKernelSyncInfo,
														  PVRSRV_HANDLE_TYPE_SYNC_INFO);
	if (psSyncOpsFlushToDeltaOUT->eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVSyncOpsFlushToDeltaBW: PVRSRVLookupHandle failed"));
		return 0;
	}

	/* FIXME:  there's logic here in the bridge-wrapper - this needs to be moved to
	   a better place */

	ui32DeltaRead = psSyncInfo->psSyncData->ui32ReadOpsPending - psSyncInfo->psSyncData->ui32ReadOpsComplete;
	ui32DeltaWrite = psSyncInfo->psSyncData->ui32WriteOpsPending - psSyncInfo->psSyncData->ui32WriteOpsComplete;

	if (ui32DeltaRead <= psSyncOpsFlushToDeltaIN->ui32Delta && ui32DeltaWrite <= psSyncOpsFlushToDeltaIN->ui32Delta)
	{
#if defined(PDUMP) && !defined(SUPPORT_VGX)
		/* pdump the sync pol: reads */
		PDumpComment("Poll for read ops complete to delta (%u)",
					 psSyncOpsFlushToDeltaIN->ui32Delta);
		psSyncOpsFlushToDeltaOUT->eError =
			PDumpMemPolKM(psSyncInfo->psSyncDataMemInfoKM,
						  offsetof(PVRSRV_SYNC_DATA, ui32ReadOpsComplete),
						  psSyncInfo->psSyncData->ui32LastReadOpDumpVal,
						  0xFFFFFFFF,
						  PDUMP_POLL_OPERATOR_GREATEREQUAL,
						  0,
						  MAKEUNIQUETAG(psSyncInfo->psSyncDataMemInfoKM));

		/* pdump the sync pol: writes */
		PDumpComment("Poll for write ops complete to delta (%u)",
					 psSyncOpsFlushToDeltaIN->ui32Delta);
		psSyncOpsFlushToDeltaOUT->eError =
			PDumpMemPolKM(psSyncInfo->psSyncDataMemInfoKM,
						  offsetof(PVRSRV_SYNC_DATA, ui32WriteOpsComplete),
						  psSyncInfo->psSyncData->ui32LastOpDumpVal,
						  0xFFFFFFFF,
						  PDUMP_POLL_OPERATOR_GREATEREQUAL,
						  0,
						  MAKEUNIQUETAG(psSyncInfo->psSyncDataMemInfoKM));
#endif

		psSyncOpsFlushToDeltaOUT->eError = PVRSRV_OK;
	}
	else
	{
		psSyncOpsFlushToDeltaOUT->eError = PVRSRV_ERROR_RETRY;
	}

	return 0;
}


static PVRSRV_ERROR
FreeSyncInfoCallback(IMG_PVOID	pvParam,
                     IMG_UINT32 ui32Param,
                     IMG_BOOL	bDummy)
{
	PVRSRV_KERNEL_SYNC_INFO *psSyncInfo;

	PVR_UNREFERENCED_PARAMETER(ui32Param);
    PVR_UNREFERENCED_PARAMETER(bDummy);

	psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)pvParam;

	PVRSRVKernelSyncInfoDecRef(psSyncInfo, IMG_NULL);

	return PVRSRV_OK;
}


static IMG_INT
PVRSRVAllocSyncInfoBW(IMG_UINT32                                         ui32BridgeID,
					  PVRSRV_BRIDGE_IN_ALLOC_SYNC_INFO                  *psAllocSyncInfoIN,
					  PVRSRV_BRIDGE_OUT_ALLOC_SYNC_INFO                 *psAllocSyncInfoOUT,
					  PVRSRV_PER_PROCESS_DATA		   		            *psPerProc)
{
	PVRSRV_KERNEL_SYNC_INFO		*psSyncInfo;
	PVRSRV_ERROR eError;
	PVRSRV_DEVICE_NODE *psDeviceNode;
	IMG_HANDLE hDevMemContext;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_ALLOC_SYNC_INFO);

	NEW_HANDLE_BATCH_OR_ERROR(psAllocSyncInfoOUT->eError, psPerProc, 1)

	eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
								(IMG_HANDLE *)&psDeviceNode,
								psAllocSyncInfoIN->hDevCookie,
								PVRSRV_HANDLE_TYPE_DEV_NODE);
	if(eError != PVRSRV_OK)
	{
		goto allocsyncinfo_errorexit;
	}

	hDevMemContext = psDeviceNode->sDevMemoryInfo.pBMKernelContext;

	eError = PVRSRVAllocSyncInfoKM(psDeviceNode,
								   hDevMemContext,
								   &psSyncInfo);

	if (eError != PVRSRV_OK)
	{
		goto allocsyncinfo_errorexit;
	}

	eError = PVRSRVAllocHandle(psPerProc->psHandleBase,
							   &psAllocSyncInfoOUT->hKernelSyncInfo,
							   psSyncInfo,
							   PVRSRV_HANDLE_TYPE_SYNC_INFO,
							   PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE);

	if(eError != PVRSRV_OK)
	{
		goto allocsyncinfo_errorexit_freesyncinfo;
	}

	psSyncInfo->hResItem = ResManRegisterRes(psPerProc->hResManContext,
											   RESMAN_TYPE_SYNC_INFO,
											   psSyncInfo,
											   0,
											   &FreeSyncInfoCallback);

	/* Success */
	goto allocsyncinfo_commit;

	/* Error handling */
 allocsyncinfo_errorexit_freesyncinfo:
	PVRSRVKernelSyncInfoDecRef(psSyncInfo, IMG_NULL);

 allocsyncinfo_errorexit:

	/* Common exit */
 allocsyncinfo_commit:
	psAllocSyncInfoOUT->eError = eError;
	COMMIT_HANDLE_BATCH_OR_ERROR(eError, psPerProc);

	return 0;
}


static IMG_INT
PVRSRVFreeSyncInfoBW(IMG_UINT32                                          ui32BridgeID,
					 PVRSRV_BRIDGE_IN_FREE_SYNC_INFO                     *psFreeSyncInfoIN,
					 PVRSRV_BRIDGE_RETURN                                *psFreeSyncInfoOUT,
					 PVRSRV_PER_PROCESS_DATA		   		             *psPerProc)
{
	PVRSRV_KERNEL_SYNC_INFO *psSyncInfo;
	PVRSRV_ERROR eError;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_FREE_SYNC_INFO);

	eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
								(IMG_VOID**)&psSyncInfo,
								psFreeSyncInfoIN->hKernelSyncInfo,
								PVRSRV_HANDLE_TYPE_SYNC_INFO);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVFreeSyncInfoBW: PVRSRVLookupHandle failed"));
		psFreeSyncInfoOUT->eError = eError;
		return 0;
	}

	eError = PVRSRVReleaseHandle(psPerProc->psHandleBase,
								 psFreeSyncInfoIN->hKernelSyncInfo,
								 PVRSRV_HANDLE_TYPE_SYNC_INFO);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVFreeSyncInfoBW: PVRSRVReleaseHandle failed"));
		psFreeSyncInfoOUT->eError = eError;
		return 0;
	}

    eError = ResManFreeResByPtr(psSyncInfo->hResItem, CLEANUP_WITH_POLL);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVFreeSyncInfoBW: ResManFreeResByPtr failed"));
		psFreeSyncInfoOUT->eError = eError;
		return 0;
	}

	return 0;
}


PVRSRV_ERROR
CommonBridgeInit(IMG_VOID)
{
	IMG_UINT32 i;

	SetDispatchTableEntry(PVRSRV_BRIDGE_ENUM_DEVICES, PVRSRVEnumerateDevicesBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_ACQUIRE_DEVICEINFO, PVRSRVAcquireDeviceDataBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RELEASE_DEVICEINFO, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_CREATE_DEVMEMCONTEXT, PVRSRVCreateDeviceMemContextBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DESTROY_DEVMEMCONTEXT, PVRSRVDestroyDeviceMemContextBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_DEVMEM_HEAPINFO, PVRSRVGetDeviceMemHeapInfoBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_ALLOC_DEVICEMEM, PVRSRVAllocDeviceMemBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_FREE_DEVICEMEM, PVRSRVFreeDeviceMemBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_GETFREE_DEVICEMEM, PVRSRVGetFreeDeviceMemBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_CREATE_COMMANDQUEUE, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DESTROY_COMMANDQUEUE, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MHANDLE_TO_MMAP_DATA, PVRMMapOSMemHandleToMMapDataBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_CONNECT_SERVICES, PVRSRVConnectBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DISCONNECT_SERVICES, PVRSRVDisconnectBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_WRAP_DEVICE_MEM, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_DEVICEMEMINFO, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RESERVE_DEV_VIRTMEM	, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_FREE_DEV_VIRTMEM, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MAP_EXT_MEMORY, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_UNMAP_EXT_MEMORY, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MAP_DEV_MEMORY, PVRSRVMapDeviceMemoryBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_UNMAP_DEV_MEMORY, PVRSRVUnmapDeviceMemoryBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MAP_DEVICECLASS_MEMORY, PVRSRVMapDeviceClassMemoryBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_UNMAP_DEVICECLASS_MEMORY, PVRSRVUnmapDeviceClassMemoryBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MAP_MEM_INFO_TO_USER, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_UNMAP_MEM_INFO_FROM_USER, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_EXPORT_DEVICEMEM, PVRSRVExportDeviceMemBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RELEASE_MMAP_DATA, PVRMMapReleaseMMapDataBW);
    SetDispatchTableEntry(PVRSRV_BRIDGE_CHG_DEV_MEM_ATTRIBS, PVRSRVChangeDeviceMemoryAttributesBW);
    SetDispatchTableEntry(PVRSRV_BRIDGE_MAP_DEV_MEMORY_2, PVRSRVMapDeviceMemoryBW);
    SetDispatchTableEntry(PVRSRV_BRIDGE_EXPORT_DEVICEMEM_2, PVRSRVExportDeviceMemBW);
#if defined(SUPPORT_ION)
	SetDispatchTableEntry(PVRSRV_BRIDGE_MAP_ION_HANDLE, PVRSRVMapIonHandleBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_UNMAP_ION_HANDLE, PVRSRVUnmapIonHandleBW);
#endif

	/* SIM */
	SetDispatchTableEntry(PVRSRV_BRIDGE_PROCESS_SIMISR_EVENT, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_REGISTER_SIM_PROCESS, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_UNREGISTER_SIM_PROCESS, DummyBW);

	/* User Mapping */
	SetDispatchTableEntry(PVRSRV_BRIDGE_MAPPHYSTOUSERSPACE, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_UNMAPPHYSTOUSERSPACE, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_GETPHYSTOUSERSPACEMAP, DummyBW);

	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_FB_STATS, DummyBW);

	/* API to retrieve misc. info. from services */
	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_MISC_INFO, PVRSRVGetMiscInfoBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RELEASE_MISC_INFO, DummyBW);

	/* Overlay ioctls */
#if defined (SUPPORT_OVERLAY_ROTATE_BLIT)
	SetDispatchTableEntry(PVRSRV_BRIDGE_INIT_3D_OVL_BLT_RES, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DEINIT_3D_OVL_BLT_RES, DummyBW);
#endif


	/* PDUMP */
#if defined(PDUMP)
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_INIT, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_MEMPOL, PDumpMemPolBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_DUMPMEM, PDumpMemBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_REG, PDumpRegWithFlagsBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_REGPOL, PDumpRegPolBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_COMMENT, PDumpCommentBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_SETFRAME, PDumpSetFrameBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_ISCAPTURING, PDumpIsCaptureFrameBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_DUMPBITMAP, PDumpBitmapBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_DUMPREADREG, PDumpReadRegBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_SYNCPOL, PDumpSyncPolBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_DUMPSYNC, PDumpSyncDumpBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_MEMPAGES, PDumpMemPagesBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_DRIVERINFO, PDumpDriverInfoBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_DUMPPDDEVPADDR, PDumpPDDevPAddrBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_CYCLE_COUNT_REG_READ, PDumpCycleCountRegReadBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_STARTINITPHASE, PDumpStartInitPhaseBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_STOPINITPHASE, PDumpStopInitPhaseBW);
#endif /* defined(PDUMP) */

	/* DisplayClass APIs */
	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_OEMJTABLE, DummyBW);

	/* device class enum */
	SetDispatchTableEntry(PVRSRV_BRIDGE_ENUM_CLASS, PVRSRVEnumerateDCBW);

	/* display class API */
	SetDispatchTableEntry(PVRSRV_BRIDGE_OPEN_DISPCLASS_DEVICE, PVRSRVOpenDCDeviceBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_CLOSE_DISPCLASS_DEVICE, PVRSRVCloseDCDeviceBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_ENUM_DISPCLASS_FORMATS, PVRSRVEnumDCFormatsBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_ENUM_DISPCLASS_DIMS, PVRSRVEnumDCDimsBW);
#if defined(SUPPORT_PVRSRV_GET_DC_SYSTEM_BUFFER)
	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_DISPCLASS_SYSBUFFER, PVRSRVGetDCSystemBufferBW);
#else
	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_DISPCLASS_SYSBUFFER, DummyBW);
#endif
	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_DISPCLASS_INFO, PVRSRVGetDCInfoBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_CREATE_DISPCLASS_SWAPCHAIN, PVRSRVCreateDCSwapChainBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DESTROY_DISPCLASS_SWAPCHAIN, PVRSRVDestroyDCSwapChainBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SET_DISPCLASS_DSTRECT, PVRSRVSetDCDstRectBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SET_DISPCLASS_SRCRECT, PVRSRVSetDCSrcRectBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SET_DISPCLASS_DSTCOLOURKEY, PVRSRVSetDCDstColourKeyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SET_DISPCLASS_SRCCOLOURKEY, PVRSRVSetDCSrcColourKeyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_DISPCLASS_BUFFERS, PVRSRVGetDCBuffersBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SWAP_DISPCLASS_TO_BUFFER, PVRSRVSwapToDCBufferBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SWAP_DISPCLASS_TO_BUFFER2, PVRSRVSwapToDCBuffer2BW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SWAP_DISPCLASS_TO_SYSTEM, PVRSRVSwapToDCSystemBW);

	/* buffer class API */
	SetDispatchTableEntry(PVRSRV_BRIDGE_OPEN_BUFFERCLASS_DEVICE, PVRSRVOpenBCDeviceBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_CLOSE_BUFFERCLASS_DEVICE, PVRSRVCloseBCDeviceBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_BUFFERCLASS_INFO, PVRSRVGetBCInfoBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_BUFFERCLASS_BUFFER, PVRSRVGetBCBufferBW);

	/* Wrap/Unwrap external memory */
	/* S.LSI does not use wrap function */
	SetDispatchTableEntry(PVRSRV_BRIDGE_WRAP_EXT_MEMORY, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_UNWRAP_EXT_MEMORY, DummyBW);

	/* Shared memory */
	SetDispatchTableEntry(PVRSRV_BRIDGE_ALLOC_SHARED_SYS_MEM, PVRSRVAllocSharedSysMemoryBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_FREE_SHARED_SYS_MEM, PVRSRVFreeSharedSysMemoryBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MAP_MEMINFO_MEM, PVRSRVMapMemInfoMemBW);

	/* Intialisation Service support */
	SetDispatchTableEntry(PVRSRV_BRIDGE_INITSRV_CONNECT,	&PVRSRVInitSrvConnectBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_INITSRV_DISCONNECT, &PVRSRVInitSrvDisconnectBW);

	/* Event Object */
	SetDispatchTableEntry(PVRSRV_BRIDGE_EVENT_OBJECT_WAIT,	&PVRSRVEventObjectWaitBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_EVENT_OBJECT_OPEN,	&PVRSRVEventObjectOpenBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_EVENT_OBJECT_CLOSE, &PVRSRVEventObjectCloseBW);

	SetDispatchTableEntry(PVRSRV_BRIDGE_CREATE_SYNC_INFO_MOD_OBJ, PVRSRVCreateSyncInfoModObjBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DESTROY_SYNC_INFO_MOD_OBJ, PVRSRVDestroySyncInfoModObjBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MODIFY_PENDING_SYNC_OPS, PVRSRVModifyPendingSyncOpsBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MODIFY_COMPLETE_SYNC_OPS, PVRSRVModifyCompleteSyncOpsBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_OPS_TAKE_TOKEN, PVRSRVSyncOpsTakeTokenBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_OPS_FLUSH_TO_TOKEN, PVRSRVSyncOpsFlushToTokenBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_OPS_FLUSH_TO_MOD_OBJ, PVRSRVSyncOpsFlushToModObjBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_OPS_FLUSH_TO_DELTA, PVRSRVSyncOpsFlushToDeltaBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_ALLOC_SYNC_INFO, PVRSRVAllocSyncInfoBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_FREE_SYNC_INFO, PVRSRVFreeSyncInfoBW);

#if defined (SUPPORT_SGX)
	SetSGXDispatchTableEntry();
#endif
#if defined (SUPPORT_VGX)
	SetVGXDispatchTableEntry();
#endif
#if defined (SUPPORT_MSVDX)
	SetMSVDXDispatchTableEntry();
#endif

	/* A safety net to help ensure there won't be any un-initialised dispatch
	 * table entries... */
	/* Note: This is specifically done _after_ setting all the dispatch entries
	 * so that SetDispatchTableEntry can detect mistakes where entries
	 * overlap */
	for(i=0;i<BRIDGE_DISPATCH_TABLE_ENTRY_COUNT;i++)
	{
		if(!g_BridgeDispatchTable[i].pfFunction)
		{
			g_BridgeDispatchTable[i].pfFunction = &DummyBW;
#if defined(DEBUG_BRIDGE_KM)
			g_BridgeDispatchTable[i].pszIOCName = "_PVRSRV_BRIDGE_DUMMY";
			g_BridgeDispatchTable[i].pszFunctionName = "DummyBW";
			g_BridgeDispatchTable[i].ui32CallCount = 0;
			g_BridgeDispatchTable[i].ui32CopyFromUserTotalBytes = 0;
			g_BridgeDispatchTable[i].ui32CopyToUserTotalBytes = 0;
#endif
		}
	}

	return PVRSRV_OK;
}

IMG_INT BridgedDispatchKM(PVRSRV_PER_PROCESS_DATA * psPerProc,
					  PVRSRV_BRIDGE_PACKAGE   * psBridgePackageKM)
{
	IMG_VOID   * psBridgeIn;
	IMG_VOID   * psBridgeOut;
	BridgeWrapperFunction pfBridgeHandler;
	IMG_UINT32   ui32BridgeID = psBridgePackageKM->ui32BridgeID;
	IMG_INT      err          = -EFAULT;

#if defined(DEBUG_TRACE_BRIDGE_KM)
	PVR_DPF((PVR_DBG_ERROR, "%s: %s",
			 __FUNCTION__,
			 g_BridgeDispatchTable[ui32BridgeID].pszIOCName));
#endif

#if defined(DEBUG_BRIDGE_KM)
	g_BridgeDispatchTable[ui32BridgeID].ui32CallCount++;
	g_BridgeGlobalStats.ui32IOCTLCount++;
#endif

	if(!psPerProc->bInitProcess)
	{
		if(PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_RAN))
		{
			if(!PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_SUCCESSFUL))
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Initialisation failed.  Driver unusable.",
						 __FUNCTION__));
				goto return_fault;
			}
		}
		else
		{
			if(PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_RUNNING))
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Initialisation is in progress",
						 __FUNCTION__));
				goto return_fault;
			}
			else
			{
				/* Only certain operations are allowed */
				switch(ui32BridgeID)
				{
					case PVRSRV_GET_BRIDGE_ID(PVRSRV_BRIDGE_CONNECT_SERVICES):
					case PVRSRV_GET_BRIDGE_ID(PVRSRV_BRIDGE_DISCONNECT_SERVICES):
					case PVRSRV_GET_BRIDGE_ID(PVRSRV_BRIDGE_INITSRV_CONNECT):
					case PVRSRV_GET_BRIDGE_ID(PVRSRV_BRIDGE_INITSRV_DISCONNECT):
						break;
					default:
						PVR_DPF((PVR_DBG_ERROR, "%s: Driver initialisation not completed yet.",
								 __FUNCTION__));
						goto return_fault;
				}
			}
		}
	}

#if defined(__linux__)
	{
		/* This should be moved into the linux specific code */
		SYS_DATA *psSysData;

		SysAcquireData(&psSysData);

		/* We have already set up some static buffers to store our ioctl data... */
		psBridgeIn = ((ENV_DATA *)psSysData->pvEnvSpecificData)->pvBridgeData;
		psBridgeOut = (IMG_PVOID)((IMG_PBYTE)psBridgeIn + PVRSRV_MAX_BRIDGE_IN_SIZE);

		/* check we are not using a bigger bridge than allocated */
		if((psBridgePackageKM->ui32InBufferSize > PVRSRV_MAX_BRIDGE_IN_SIZE) || 
			(psBridgePackageKM->ui32OutBufferSize > PVRSRV_MAX_BRIDGE_OUT_SIZE))
		{
			goto return_fault;
		}


		if(psBridgePackageKM->ui32InBufferSize > 0)
		{
			if(!OSAccessOK(PVR_VERIFY_READ,
							psBridgePackageKM->pvParamIn,
							psBridgePackageKM->ui32InBufferSize))
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Invalid pvParamIn pointer", __FUNCTION__));
			}

			if(CopyFromUserWrapper(psPerProc,
					               ui32BridgeID,
								   psBridgeIn,
								   psBridgePackageKM->pvParamIn,
								   psBridgePackageKM->ui32InBufferSize)
			  != PVRSRV_OK)
			{
				goto return_fault;
			}
		}
	}
#else
	psBridgeIn  = psBridgePackageKM->pvParamIn;
	psBridgeOut = psBridgePackageKM->pvParamOut;
#endif

	if(ui32BridgeID >= (BRIDGE_DISPATCH_TABLE_ENTRY_COUNT))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: ui32BridgeID = %d is out if range!",
				 __FUNCTION__, ui32BridgeID));
		goto return_fault;
	}
	pfBridgeHandler =
		(BridgeWrapperFunction)g_BridgeDispatchTable[ui32BridgeID].pfFunction;
	err = pfBridgeHandler(ui32BridgeID,
						  psBridgeIn,
						  psBridgeOut,
						  psPerProc);
	if(err < 0)
	{
		goto return_fault;
	}

#if defined(__linux__)
	/* This should be moved into the linux specific code */
	if(CopyToUserWrapper(psPerProc,
						 ui32BridgeID,
						 psBridgePackageKM->pvParamOut,
						 psBridgeOut,
						 psBridgePackageKM->ui32OutBufferSize)
	   != PVRSRV_OK)
	{
		goto return_fault;
	}
#endif

	err = 0;
return_fault:

	ReleaseHandleBatch(psPerProc);
	return err;
}

/******************************************************************************
 End of file (bridged_pvr_bridge.c)
******************************************************************************/
