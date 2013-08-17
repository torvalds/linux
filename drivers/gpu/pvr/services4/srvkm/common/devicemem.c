/*************************************************************************/ /*!
@Title          Device addressable memory functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Device addressable memory APIs
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

#include "services_headers.h"
#include "buffer_manager.h"
#include "pdump_km.h"
#include "pvr_bridge_km.h"
#include "osfunc.h"
#include "devicemem.h"

#if defined(SUPPORT_ION)
#include "ion.h"
#include "env_perproc.h"
#include "ion_sync.h"

/* Start size of the g_IonSyncHash hash table */
#define ION_SYNC_HASH_SIZE 20
HASH_TABLE *g_psIonSyncHash = IMG_NULL;
#endif

/* local function prototypes */
static PVRSRV_ERROR AllocDeviceMem(IMG_HANDLE		hDevCookie,
								   IMG_HANDLE		hDevMemHeap,
								   IMG_UINT32		ui32Flags,
								   IMG_SIZE_T		ui32Size,
								   IMG_SIZE_T		ui32Alignment,
								   IMG_PVOID		pvPrivData,
								   IMG_UINT32		ui32PrivDataLength,
								   IMG_UINT32		ui32ChunkSize,
								   IMG_UINT32		ui32NumVirtChunks,
								   IMG_UINT32		ui32NumPhysChunks,
								   IMG_BOOL			*pabMapChunk,
								   PVRSRV_KERNEL_MEM_INFO **ppsMemInfo);

/* local structures */

/*
	structure stored in resman to store references
	to the SRC and DST meminfo
*/
typedef struct _RESMAN_MAP_DEVICE_MEM_DATA_
{
	/* the DST meminfo created by the map */
	PVRSRV_KERNEL_MEM_INFO	*psMemInfo;
	/* SRC meminfo */
	PVRSRV_KERNEL_MEM_INFO	*psSrcMemInfo;
} RESMAN_MAP_DEVICE_MEM_DATA;

/*
	map device class resman memory storage structure
*/
typedef struct _PVRSRV_DC_MAPINFO_
{
	PVRSRV_KERNEL_MEM_INFO	*psMemInfo;
	PVRSRV_DEVICE_NODE		*psDeviceNode;
	IMG_UINT32				ui32RangeIndex;
	IMG_UINT32				ui32TilingStride;
	PVRSRV_DEVICECLASS_BUFFER	*psDeviceClassBuffer;
} PVRSRV_DC_MAPINFO;

static IMG_UINT32 g_ui32SyncUID = 0;

/*!
******************************************************************************

 @Function	PVRSRVGetDeviceMemHeapsKM

 @Description

 Gets the device shared memory heaps

 @Input	   hDevCookie :
 @Output   phDevMemContext : ptr to handle to memory context
 @Output   psHeapInfo : ptr to array of heap info

 @Return   PVRSRV_DEVICE_NODE, valid devnode or IMG_NULL

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetDeviceMemHeapsKM(IMG_HANDLE hDevCookie,
													PVRSRV_HEAP_INFO *psHeapInfo)
{
	PVRSRV_DEVICE_NODE *psDeviceNode;
	IMG_UINT32 ui32HeapCount;
	DEVICE_MEMORY_HEAP_INFO *psDeviceMemoryHeap;
	IMG_UINT32 i;

	if (hDevCookie == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVGetDeviceMemHeapsKM: hDevCookie invalid"));
		PVR_DBG_BREAK;
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDeviceNode = (PVRSRV_DEVICE_NODE *)hDevCookie;

	/* Setup useful pointers */
	ui32HeapCount = psDeviceNode->sDevMemoryInfo.ui32HeapCount;
	psDeviceMemoryHeap = psDeviceNode->sDevMemoryInfo.psDeviceMemoryHeap;

	/* check we don't exceed the max number of heaps */
	PVR_ASSERT(ui32HeapCount <= PVRSRV_MAX_CLIENT_HEAPS);

	/* retrieve heap information */
	for(i=0; i<ui32HeapCount; i++)
	{
		/* return information about the heap */
		psHeapInfo[i].ui32HeapID = psDeviceMemoryHeap[i].ui32HeapID;
		psHeapInfo[i].hDevMemHeap = psDeviceMemoryHeap[i].hDevMemHeap;
		psHeapInfo[i].sDevVAddrBase = psDeviceMemoryHeap[i].sDevVAddrBase;
		psHeapInfo[i].ui32HeapByteSize = psDeviceMemoryHeap[i].ui32HeapSize;
		psHeapInfo[i].ui32Attribs = psDeviceMemoryHeap[i].ui32Attribs;
		/* (XTileStride > 0) denotes a tiled heap */
		psHeapInfo[i].ui32XTileStride = psDeviceMemoryHeap[i].ui32XTileStride;
	}

	for(; i < PVRSRV_MAX_CLIENT_HEAPS; i++)
	{
		OSMemSet(psHeapInfo + i, 0, sizeof(*psHeapInfo));
		psHeapInfo[i].ui32HeapID = (IMG_UINT32)PVRSRV_UNDEFINED_HEAP_ID;
	}

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVCreateDeviceMemContextKM

 @Description

 Creates a device memory context

 @Input	   hDevCookie :
 @Input	   psPerProc : Per-process data
 @Output   phDevMemContext : ptr to handle to memory context
 @Output   pui32ClientHeapCount : ptr to heap count
 @Output   psHeapInfo : ptr to array of heap info

 @Return   PVRSRV_DEVICE_NODE, valid devnode or IMG_NULL

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVCreateDeviceMemContextKM(IMG_HANDLE					hDevCookie,
														 PVRSRV_PER_PROCESS_DATA	*psPerProc,
														 IMG_HANDLE 				*phDevMemContext,
														 IMG_UINT32 				*pui32ClientHeapCount,
														 PVRSRV_HEAP_INFO			*psHeapInfo,
														 IMG_BOOL					*pbCreated,
														 IMG_BOOL 					*pbShared)
{
	PVRSRV_DEVICE_NODE *psDeviceNode;
	IMG_UINT32 ui32HeapCount, ui32ClientHeapCount=0;
	DEVICE_MEMORY_HEAP_INFO *psDeviceMemoryHeap;
	IMG_HANDLE hDevMemContext;
	IMG_HANDLE hDevMemHeap;
	IMG_DEV_PHYADDR sPDDevPAddr;
	IMG_UINT32 i;

#if !defined(PVR_SECURE_HANDLES)
	PVR_UNREFERENCED_PARAMETER(pbShared);
#endif

	if (hDevCookie == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVCreateDeviceMemContextKM: hDevCookie invalid"));
		PVR_DBG_BREAK;
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDeviceNode = (PVRSRV_DEVICE_NODE *)hDevCookie;

	/*
		Setup useful pointers
	*/
	ui32HeapCount = psDeviceNode->sDevMemoryInfo.ui32HeapCount;
	psDeviceMemoryHeap = psDeviceNode->sDevMemoryInfo.psDeviceMemoryHeap;

	/*
		check we don't exceed the max number of heaps
	*/
	PVR_ASSERT(ui32HeapCount <= PVRSRV_MAX_CLIENT_HEAPS);

	/*
		Create a memory context for the caller
	*/
	hDevMemContext = BM_CreateContext(psDeviceNode,
									  &sPDDevPAddr,
									  psPerProc,
									  pbCreated);
	if (hDevMemContext == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDeviceMemContextKM: Failed BM_CreateContext"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/* create the per context heaps */
	for(i=0; i<ui32HeapCount; i++)
	{
		switch(psDeviceMemoryHeap[i].DevMemHeapType)
		{
			case DEVICE_MEMORY_HEAP_SHARED_EXPORTED:
			{
				/* return information about the heap */
				psHeapInfo[ui32ClientHeapCount].ui32HeapID = psDeviceMemoryHeap[i].ui32HeapID;
				psHeapInfo[ui32ClientHeapCount].hDevMemHeap = psDeviceMemoryHeap[i].hDevMemHeap;
				psHeapInfo[ui32ClientHeapCount].sDevVAddrBase = psDeviceMemoryHeap[i].sDevVAddrBase;
				psHeapInfo[ui32ClientHeapCount].ui32HeapByteSize = psDeviceMemoryHeap[i].ui32HeapSize;
				psHeapInfo[ui32ClientHeapCount].ui32Attribs = psDeviceMemoryHeap[i].ui32Attribs;
				#if defined(SUPPORT_MEMORY_TILING)
				psHeapInfo[ui32ClientHeapCount].ui32XTileStride = psDeviceMemoryHeap[i].ui32XTileStride;
				#else
				psHeapInfo[ui32ClientHeapCount].ui32XTileStride = 0;
				#endif

#if defined(PVR_SECURE_HANDLES)
				pbShared[ui32ClientHeapCount] = IMG_TRUE;
#endif
				ui32ClientHeapCount++;
				break;
			}
			case DEVICE_MEMORY_HEAP_PERCONTEXT:
			{
				if (psDeviceMemoryHeap[i].ui32HeapSize > 0)
				{
					hDevMemHeap = BM_CreateHeap(hDevMemContext,
												&psDeviceMemoryHeap[i]);
					if (hDevMemHeap == IMG_NULL)
					{
						BM_DestroyContext(hDevMemContext, IMG_NULL);
						return PVRSRV_ERROR_OUT_OF_MEMORY;
					}
				}
				else
				{
					hDevMemHeap = IMG_NULL;
				}

				/* return information about the heap */
				psHeapInfo[ui32ClientHeapCount].ui32HeapID = psDeviceMemoryHeap[i].ui32HeapID;
				psHeapInfo[ui32ClientHeapCount].hDevMemHeap = hDevMemHeap;
				psHeapInfo[ui32ClientHeapCount].sDevVAddrBase = psDeviceMemoryHeap[i].sDevVAddrBase;
				psHeapInfo[ui32ClientHeapCount].ui32HeapByteSize = psDeviceMemoryHeap[i].ui32HeapSize;
				psHeapInfo[ui32ClientHeapCount].ui32Attribs = psDeviceMemoryHeap[i].ui32Attribs;
				#if defined(SUPPORT_MEMORY_TILING)
				psHeapInfo[ui32ClientHeapCount].ui32XTileStride = psDeviceMemoryHeap[i].ui32XTileStride;
				#else
				psHeapInfo[ui32ClientHeapCount].ui32XTileStride = 0;
				#endif
#if defined(PVR_SECURE_HANDLES)
				pbShared[ui32ClientHeapCount] = IMG_FALSE;
#endif

				ui32ClientHeapCount++;
				break;
			}
		}
	}

	/* return shared_exported and per context heap information to the caller */
	*pui32ClientHeapCount = ui32ClientHeapCount;
	*phDevMemContext = hDevMemContext;

	return PVRSRV_OK;
}

IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVDestroyDeviceMemContextKM(IMG_HANDLE hDevCookie,
														  IMG_HANDLE hDevMemContext,
														  IMG_BOOL *pbDestroyed)
{
	PVR_UNREFERENCED_PARAMETER(hDevCookie);

	return BM_DestroyContext(hDevMemContext, pbDestroyed);
}




/*!
******************************************************************************

 @Function	PVRSRVGetDeviceMemHeapInfoKM

 @Description

 gets heap info

 @Input	   hDevCookie :
 @Input    hDevMemContext : ptr to handle to memory context
 @Output   pui32ClientHeapCount : ptr to heap count
 @Output   psHeapInfo : ptr to array of heap info

 @Return

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetDeviceMemHeapInfoKM(IMG_HANDLE					hDevCookie,
														 IMG_HANDLE 				hDevMemContext,
														 IMG_UINT32 				*pui32ClientHeapCount,
														 PVRSRV_HEAP_INFO			*psHeapInfo,
														 IMG_BOOL 					*pbShared)
{
	PVRSRV_DEVICE_NODE *psDeviceNode;
	IMG_UINT32 ui32HeapCount, ui32ClientHeapCount=0;
	DEVICE_MEMORY_HEAP_INFO *psDeviceMemoryHeap;
	IMG_HANDLE hDevMemHeap;
	IMG_UINT32 i;

#if !defined(PVR_SECURE_HANDLES)
	PVR_UNREFERENCED_PARAMETER(pbShared);
#endif

	if (hDevCookie == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVGetDeviceMemHeapInfoKM: hDevCookie invalid"));
		PVR_DBG_BREAK;
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDeviceNode = (PVRSRV_DEVICE_NODE *)hDevCookie;

	/*
		Setup useful pointers
	*/
	ui32HeapCount = psDeviceNode->sDevMemoryInfo.ui32HeapCount;
	psDeviceMemoryHeap = psDeviceNode->sDevMemoryInfo.psDeviceMemoryHeap;

	/*
		check we don't exceed the max number of heaps
	*/
	PVR_ASSERT(ui32HeapCount <= PVRSRV_MAX_CLIENT_HEAPS);

	/* create the per context heaps */
	for(i=0; i<ui32HeapCount; i++)
	{
		switch(psDeviceMemoryHeap[i].DevMemHeapType)
		{
			case DEVICE_MEMORY_HEAP_SHARED_EXPORTED:
			{
				/* return information about the heap */
				psHeapInfo[ui32ClientHeapCount].ui32HeapID = psDeviceMemoryHeap[i].ui32HeapID;
				psHeapInfo[ui32ClientHeapCount].hDevMemHeap = psDeviceMemoryHeap[i].hDevMemHeap;
				psHeapInfo[ui32ClientHeapCount].sDevVAddrBase = psDeviceMemoryHeap[i].sDevVAddrBase;
				psHeapInfo[ui32ClientHeapCount].ui32HeapByteSize = psDeviceMemoryHeap[i].ui32HeapSize;
				psHeapInfo[ui32ClientHeapCount].ui32Attribs = psDeviceMemoryHeap[i].ui32Attribs;
				psHeapInfo[ui32ClientHeapCount].ui32XTileStride = psDeviceMemoryHeap[i].ui32XTileStride;
#if defined(PVR_SECURE_HANDLES)
				pbShared[ui32ClientHeapCount] = IMG_TRUE;
#endif
				ui32ClientHeapCount++;
				break;
			}
			case DEVICE_MEMORY_HEAP_PERCONTEXT:
			{
				if (psDeviceMemoryHeap[i].ui32HeapSize > 0)
				{
					hDevMemHeap = BM_CreateHeap(hDevMemContext,
												&psDeviceMemoryHeap[i]);
				
					if (hDevMemHeap == IMG_NULL)
					{
						return PVRSRV_ERROR_OUT_OF_MEMORY;
					}
				}
				else
				{
					hDevMemHeap = IMG_NULL;
				}

				/* return information about the heap */
				psHeapInfo[ui32ClientHeapCount].ui32HeapID = psDeviceMemoryHeap[i].ui32HeapID;
				psHeapInfo[ui32ClientHeapCount].hDevMemHeap = hDevMemHeap;
				psHeapInfo[ui32ClientHeapCount].sDevVAddrBase = psDeviceMemoryHeap[i].sDevVAddrBase;
				psHeapInfo[ui32ClientHeapCount].ui32HeapByteSize = psDeviceMemoryHeap[i].ui32HeapSize;
				psHeapInfo[ui32ClientHeapCount].ui32Attribs = psDeviceMemoryHeap[i].ui32Attribs;
				psHeapInfo[ui32ClientHeapCount].ui32XTileStride = psDeviceMemoryHeap[i].ui32XTileStride;
#if defined(PVR_SECURE_HANDLES)
				pbShared[ui32ClientHeapCount] = IMG_FALSE;
#endif

				ui32ClientHeapCount++;
				break;
			}
		}
	}

	/* return shared_exported and per context heap information to the caller */
	*pui32ClientHeapCount = ui32ClientHeapCount;

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	AllocDeviceMem

 @Description

 Allocates device memory

 @Input	   hDevCookie :

 @Input	   hDevMemHeap

 @Input	   ui32Flags : Some combination of PVRSRV_MEM_ flags

 @Input	   ui32Size :  Number of bytes to allocate

 @Input	   ui32Alignment : Alignment of allocation

 @Input    pvPrivData : Opaque private data passed through to allocator

 @Input    ui32PrivDataLength : Length of opaque private data

 @Output   **ppsMemInfo : On success, receives a pointer to the created MEM_INFO structure

 @Return   PVRSRV_ERROR :

******************************************************************************/
static PVRSRV_ERROR AllocDeviceMem(IMG_HANDLE		hDevCookie,
								   IMG_HANDLE		hDevMemHeap,
								   IMG_UINT32		ui32Flags,
								   IMG_SIZE_T		ui32Size,
								   IMG_SIZE_T		ui32Alignment,
								   IMG_PVOID		pvPrivData,
								   IMG_UINT32		ui32PrivDataLength,
								   IMG_UINT32		ui32ChunkSize,
								   IMG_UINT32		ui32NumVirtChunks,
								   IMG_UINT32		ui32NumPhysChunks,
								   IMG_BOOL			*pabMapChunk,
								   PVRSRV_KERNEL_MEM_INFO **ppsMemInfo)
{
 	PVRSRV_KERNEL_MEM_INFO	*psMemInfo;
	BM_HANDLE 		hBuffer;
	/* Pointer to implementation details within the mem_info */
	PVRSRV_MEMBLK	*psMemBlock;
	IMG_BOOL		bBMError;

	PVR_UNREFERENCED_PARAMETER(hDevCookie);

	*ppsMemInfo = IMG_NULL;

	if(OSAllocMem(PVRSRV_PAGEABLE_SELECT,
					sizeof(PVRSRV_KERNEL_MEM_INFO),
					(IMG_VOID **)&psMemInfo, IMG_NULL,
					"Kernel Memory Info") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"AllocDeviceMem: Failed to alloc memory for block"));
		return (PVRSRV_ERROR_OUT_OF_MEMORY);
	}

	OSMemSet(psMemInfo, 0, sizeof(*psMemInfo));

	psMemBlock = &(psMemInfo->sMemBlk);

	/* BM supplied Device Virtual Address with physical backing RAM */
	psMemInfo->ui32Flags = ui32Flags | PVRSRV_MEM_RAM_BACKED_ALLOCATION;

	bBMError = BM_Alloc (hDevMemHeap,
							IMG_NULL,
							ui32Size,
							&psMemInfo->ui32Flags,
							IMG_CAST_TO_DEVVADDR_UINT(ui32Alignment),
							pvPrivData,
							ui32PrivDataLength,
							ui32ChunkSize,
							ui32NumVirtChunks,
							ui32NumPhysChunks,
							pabMapChunk,
							&hBuffer);

	if (!bBMError)
	{
		PVR_DPF((PVR_DBG_ERROR,"AllocDeviceMem: BM_Alloc Failed"));
		OSFreeMem(PVRSRV_PAGEABLE_SELECT, sizeof(PVRSRV_KERNEL_MEM_INFO), psMemInfo, IMG_NULL);
		/*not nulling pointer, out of scope*/
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/* Fill in "Implementation dependant" section of mem info */
	psMemBlock->sDevVirtAddr = BM_HandleToDevVaddr(hBuffer);
	psMemBlock->hOSMemHandle = BM_HandleToOSMemHandle(hBuffer);

	/* Convert from BM_HANDLE to external IMG_HANDLE */
	psMemBlock->hBuffer = (IMG_HANDLE)hBuffer;

	/* Fill in the public fields of the MEM_INFO structure */

	psMemInfo->pvLinAddrKM = BM_HandleToCpuVaddr(hBuffer);

	psMemInfo->sDevVAddr = psMemBlock->sDevVirtAddr;

	if (ui32Flags & PVRSRV_MEM_SPARSE)
	{
		psMemInfo->uAllocSize = ui32ChunkSize * ui32NumVirtChunks;
	}
	else
	{
		psMemInfo->uAllocSize = ui32Size;
	}

	/* Clear the Backup buffer pointer as we do not have one at this point. We only allocate this as we are going up/down */
	psMemInfo->pvSysBackupBuffer = IMG_NULL;

	/*
	 * Setup the output.
	 */
	*ppsMemInfo = psMemInfo;

	/*
	 * And I think we're done for now....
	 */
	return (PVRSRV_OK);
}

static PVRSRV_ERROR FreeDeviceMem2(PVRSRV_KERNEL_MEM_INFO *psMemInfo, PVRSRV_FREE_CALLBACK_ORIGIN eCallbackOrigin)
{
	BM_HANDLE		hBuffer;

	if (!psMemInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	hBuffer = psMemInfo->sMemBlk.hBuffer;

	switch(eCallbackOrigin)
	{
		case PVRSRV_FREE_CALLBACK_ORIGIN_ALLOCATOR:
			BM_Free(hBuffer, psMemInfo->ui32Flags);
			break;
		case PVRSRV_FREE_CALLBACK_ORIGIN_IMPORTER:
			BM_FreeExport(hBuffer, psMemInfo->ui32Flags);
			break;
		default:
			break;
	}

	if (psMemInfo->pvSysBackupBuffer &&
		eCallbackOrigin == PVRSRV_FREE_CALLBACK_ORIGIN_ALLOCATOR)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, psMemInfo->uAllocSize, psMemInfo->pvSysBackupBuffer, IMG_NULL);
		psMemInfo->pvSysBackupBuffer = IMG_NULL;
	}

	if (psMemInfo->ui32RefCount == 0)
		OSFreeMem(PVRSRV_PAGEABLE_SELECT, sizeof(PVRSRV_KERNEL_MEM_INFO), psMemInfo, IMG_NULL);

	return(PVRSRV_OK);
}

static PVRSRV_ERROR FreeDeviceMem(PVRSRV_KERNEL_MEM_INFO *psMemInfo)
{
	BM_HANDLE		hBuffer;

	if (!psMemInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	hBuffer = psMemInfo->sMemBlk.hBuffer;

	BM_Free(hBuffer, psMemInfo->ui32Flags);

	if(psMemInfo->pvSysBackupBuffer)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, psMemInfo->uAllocSize, psMemInfo->pvSysBackupBuffer, IMG_NULL);
		psMemInfo->pvSysBackupBuffer = IMG_NULL;
	}

	OSFreeMem(PVRSRV_PAGEABLE_SELECT, sizeof(PVRSRV_KERNEL_MEM_INFO), psMemInfo, IMG_NULL);

	return(PVRSRV_OK);
}


/*!
******************************************************************************

 @Function	PVRSRVAllocSyncInfoKM

 @Description

 Allocates a sync info

 @Return   PVRSRV_ERROR :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVAllocSyncInfoKM(IMG_HANDLE					hDevCookie,
												IMG_HANDLE					hDevMemContext,
												PVRSRV_KERNEL_SYNC_INFO		**ppsKernelSyncInfo)
{
	IMG_HANDLE hSyncDevMemHeap;
	DEVICE_MEMORY_INFO *psDevMemoryInfo;
	BM_CONTEXT *pBMContext;
	PVRSRV_ERROR eError;
	PVRSRV_KERNEL_SYNC_INFO	*psKernelSyncInfo;
	PVRSRV_SYNC_DATA *psSyncData;

	eError = OSAllocMem(PVRSRV_PAGEABLE_SELECT,
						sizeof(PVRSRV_KERNEL_SYNC_INFO),
						(IMG_VOID **)&psKernelSyncInfo, IMG_NULL,
						"Kernel Synchronization Info");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVAllocSyncInfoKM: Failed to alloc memory"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	eError = OSAtomicAlloc(&psKernelSyncInfo->pvRefCount);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVAllocSyncInfoKM: Failed to allocate atomic"));
		OSFreeMem(PVRSRV_PAGEABLE_SELECT, sizeof(PVRSRV_KERNEL_SYNC_INFO), psKernelSyncInfo, IMG_NULL);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	

	/* Get the devnode from the devheap */
	pBMContext = (BM_CONTEXT*)hDevMemContext;
	psDevMemoryInfo = &pBMContext->psDeviceNode->sDevMemoryInfo;

	/* and choose a heap for the syncinfo */
	hSyncDevMemHeap = psDevMemoryInfo->psDeviceMemoryHeap[psDevMemoryInfo->ui32SyncHeapID].hDevMemHeap;

	/*
		Cache consistent flag would be unnecessary if the heap attributes were
		changed to specify it.
	*/
	eError = AllocDeviceMem(hDevCookie,
							hSyncDevMemHeap,
							PVRSRV_MEM_CACHE_CONSISTENT,
							sizeof(PVRSRV_SYNC_DATA),
							sizeof(IMG_UINT32),
							IMG_NULL,
							0,
							0, 0, 0, IMG_NULL, /* Sparse mapping args, not required */
							&psKernelSyncInfo->psSyncDataMemInfoKM);

	if (eError != PVRSRV_OK)
	{

		PVR_DPF((PVR_DBG_ERROR,"PVRSRVAllocSyncInfoKM: Failed to alloc memory"));
		OSAtomicFree(psKernelSyncInfo->pvRefCount);
		OSFreeMem(PVRSRV_PAGEABLE_SELECT, sizeof(PVRSRV_KERNEL_SYNC_INFO), psKernelSyncInfo, IMG_NULL);
		/*not nulling pointer, out of scope*/
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/* init sync data */
	psKernelSyncInfo->psSyncData = psKernelSyncInfo->psSyncDataMemInfoKM->pvLinAddrKM;
	psSyncData = psKernelSyncInfo->psSyncData;

	psSyncData->ui32WriteOpsPending = 0;
	psSyncData->ui32WriteOpsComplete = 0;
	psSyncData->ui32ReadOpsPending = 0;
	psSyncData->ui32ReadOpsComplete = 0;
	psSyncData->ui32ReadOps2Pending = 0;
	psSyncData->ui32ReadOps2Complete = 0;
	psSyncData->ui32LastOpDumpVal = 0;
	psSyncData->ui32LastReadOpDumpVal = 0;
	psSyncData->ui64LastWrite = 0;

	/*
		Note:
		PDumping here means that we PDump syncs that we might not
		need to know about for the multi-process but this
		unavoidable as there is no point where we can PDump
		that guarantees it will be initialised before we us it
		(e.g. kick time is too late as the client might have
		issued a POL on it before that point)
	*/
#if defined(PDUMP)
	PDUMPCOMMENT("Allocating kernel sync object");
	PDUMPMEM(psKernelSyncInfo->psSyncDataMemInfoKM->pvLinAddrKM,
			psKernelSyncInfo->psSyncDataMemInfoKM,
			0,
			(IMG_UINT32)psKernelSyncInfo->psSyncDataMemInfoKM->uAllocSize,
#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
			PDUMP_FLAGS_PERSISTENT,
#else
			PDUMP_FLAGS_CONTINUOUS,
#endif
			MAKEUNIQUETAG(psKernelSyncInfo->psSyncDataMemInfoKM));
#endif

	psKernelSyncInfo->sWriteOpsCompleteDevVAddr.uiAddr = psKernelSyncInfo->psSyncDataMemInfoKM->sDevVAddr.uiAddr + offsetof(PVRSRV_SYNC_DATA, ui32WriteOpsComplete);
	psKernelSyncInfo->sReadOpsCompleteDevVAddr.uiAddr = psKernelSyncInfo->psSyncDataMemInfoKM->sDevVAddr.uiAddr + offsetof(PVRSRV_SYNC_DATA, ui32ReadOpsComplete);
	psKernelSyncInfo->sReadOps2CompleteDevVAddr.uiAddr = psKernelSyncInfo->psSyncDataMemInfoKM->sDevVAddr.uiAddr + offsetof(PVRSRV_SYNC_DATA, ui32ReadOps2Complete);
	psKernelSyncInfo->ui32UID = g_ui32SyncUID++;

	/* syncinfo meminfo has no syncinfo! */
	psKernelSyncInfo->psSyncDataMemInfoKM->psKernelSyncInfo = IMG_NULL;

	OSAtomicInc(psKernelSyncInfo->pvRefCount);

	/* return result */
	*ppsKernelSyncInfo = psKernelSyncInfo;

	return PVRSRV_OK;
}

IMG_EXPORT
IMG_VOID PVRSRVAcquireSyncInfoKM(PVRSRV_KERNEL_SYNC_INFO *psKernelSyncInfo)
{
	OSAtomicInc(psKernelSyncInfo->pvRefCount);
}

/*!
******************************************************************************

 @Function	PVRSRVFreeSyncInfoKM

 @Description

 Frees a sync info

 @Return   PVRSRV_ERROR :

******************************************************************************/
IMG_EXPORT
IMG_VOID IMG_CALLCONV PVRSRVReleaseSyncInfoKM(PVRSRV_KERNEL_SYNC_INFO	*psKernelSyncInfo)
{
	if (OSAtomicDecAndTest(psKernelSyncInfo->pvRefCount))
	{
		FreeDeviceMem(psKernelSyncInfo->psSyncDataMemInfoKM);
	
		/* Catch anyone who is trying to access the freed structure */
		psKernelSyncInfo->psSyncDataMemInfoKM = IMG_NULL;
		psKernelSyncInfo->psSyncData = IMG_NULL;
		OSAtomicFree(psKernelSyncInfo->pvRefCount);
		(IMG_VOID)OSFreeMem(PVRSRV_PAGEABLE_SELECT, sizeof(PVRSRV_KERNEL_SYNC_INFO), psKernelSyncInfo, IMG_NULL);
		/*not nulling pointer, copy on stack*/
	}
}

/*!
******************************************************************************

 @Function	freeExternal

 @Description

 Code for freeing meminfo elements that are specific to external types memory

 @Input	   psMemInfo : Kernel meminfo

 @Return   PVRSRV_ERROR :

******************************************************************************/

static IMG_VOID freeExternal(PVRSRV_KERNEL_MEM_INFO *psMemInfo)
{
	IMG_HANDLE hOSWrapMem = psMemInfo->sMemBlk.hOSWrapMem;

	/* free the page addr array if req'd */
	if(psMemInfo->sMemBlk.psIntSysPAddr)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(IMG_SYS_PHYADDR), psMemInfo->sMemBlk.psIntSysPAddr, IMG_NULL);
		psMemInfo->sMemBlk.psIntSysPAddr = IMG_NULL;
	}

	/* Mem type dependent stuff */
	if (psMemInfo->memType == PVRSRV_MEMTYPE_WRAPPED)
	{
		if(hOSWrapMem)
		{
			OSReleasePhysPageAddr(hOSWrapMem);
		}
	}
#if defined(SUPPORT_ION)
	else if (psMemInfo->memType == PVRSRV_MEMTYPE_ION)
	{
		if (hOSWrapMem)
		{
			IonUnimportBufferAndReleasePhysAddr(hOSWrapMem);
		}
	}
#endif
}

/*!
******************************************************************************

 @Function	FreeMemCallBackCommon

 @Description

 Common code for freeing device mem (called for freeing, unwrapping and unmapping)

 @Input	   psMemInfo : Kernel meminfo
 @Input	   ui32Param :  packet size
 @Input	   uibFromAllocatorParam :  Are we being called by the original allocator?

 @Return   PVRSRV_ERROR :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR FreeMemCallBackCommon(PVRSRV_KERNEL_MEM_INFO *psMemInfo,
								   IMG_UINT32	ui32Param,
								   PVRSRV_FREE_CALLBACK_ORIGIN eCallbackOrigin)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_UNREFERENCED_PARAMETER(ui32Param);

	/* decrement the refcount */
	PVRSRVKernelMemInfoDecRef(psMemInfo);

	/* check no other processes has this meminfo mapped */
	if (psMemInfo->ui32RefCount == 0)
	{
		if((psMemInfo->ui32Flags & PVRSRV_MEM_EXPORTED) != 0)
		{
			IMG_HANDLE hMemInfo = IMG_NULL;

			/* find the handle */
			eError = PVRSRVFindHandle(KERNEL_HANDLE_BASE,
									 &hMemInfo,
									 psMemInfo,
									 PVRSRV_HANDLE_TYPE_MEM_INFO);
			if(eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "FreeMemCallBackCommon: can't find exported meminfo in the global handle list"));
				return eError;
			}

			/* release the handle */
			eError = PVRSRVReleaseHandle(KERNEL_HANDLE_BASE,
										hMemInfo,
										PVRSRV_HANDLE_TYPE_MEM_INFO);
			if(eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "FreeMemCallBackCommon: PVRSRVReleaseHandle failed for exported meminfo"));
				return eError;
			}
		}

		switch(psMemInfo->memType)
		{
			/* Fall through: Free only what we should for each memory type */
			case PVRSRV_MEMTYPE_WRAPPED:
			case PVRSRV_MEMTYPE_ION:
				freeExternal(psMemInfo);
			case PVRSRV_MEMTYPE_DEVICE:
			case PVRSRV_MEMTYPE_DEVICECLASS:
#if defined(SUPPORT_ION)
				if (psMemInfo->hIonSyncInfo)
				{
					/*
						For syncs attached to Ion imported buffers we handle
						things a little differently
					*/
					PVRSRVIonBufferSyncInfoDecRef(psMemInfo->hIonSyncInfo, psMemInfo);
				}
				else
#endif
				{
					if (psMemInfo->psKernelSyncInfo)
					{
						PVRSRVKernelSyncInfoDecRef(psMemInfo->psKernelSyncInfo, psMemInfo);
					}
				}
				break;
			default:
				PVR_DPF((PVR_DBG_ERROR, "FreeMemCallBackCommon: Unknown memType"));
				eError = PVRSRV_ERROR_INVALID_MEMINFO;
		}
	}

	/*
	 * FreeDeviceMem2 will do the right thing, freeing
	 * the virtual memory info when the allocator calls
	 * but only releaseing the physical pages when everyone
	 * is done.
	 */

	if (eError == PVRSRV_OK)
	{
		eError = FreeDeviceMem2(psMemInfo, eCallbackOrigin);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "FreeMemCallBackCommon: FreeDeviceMem2 Failed %d", eError));
			PVR_DPF((PVR_DBG_ERROR, "FreeMemCallBackCommon: psMemInfo: 0x%x, eCallbackOrigin: 0x%x", (unsigned int)psMemInfo, eCallbackOrigin));
		}
	}

	return eError;
}

/*!
******************************************************************************

 @Function	FreeDeviceMemCallBack

 @Description

 ResMan call back to free device memory

 @Input	   pvParam : data packet
 @Input	   ui32Param :  packet size

 @Return   PVRSRV_ERROR :

******************************************************************************/
static PVRSRV_ERROR FreeDeviceMemCallBack(IMG_PVOID  pvParam,
										  IMG_UINT32 ui32Param,
										  IMG_BOOL   bDummy)
{
	PVRSRV_KERNEL_MEM_INFO	*psMemInfo = (PVRSRV_KERNEL_MEM_INFO *)pvParam;
	
	PVR_UNREFERENCED_PARAMETER(bDummy);

	return FreeMemCallBackCommon(psMemInfo, ui32Param,
								 PVRSRV_FREE_CALLBACK_ORIGIN_ALLOCATOR);
}


/*!
******************************************************************************

 @Function	PVRSRVFreeDeviceMemKM

 @Description

 Frees memory allocated with PVRAllocDeviceMem, including the mem_info structure

 @Input	   psMemInfo :

 @Return   PVRSRV_ERROR  :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVFreeDeviceMemKM(IMG_HANDLE				hDevCookie,
												PVRSRV_KERNEL_MEM_INFO	*psMemInfo)
{
	PVRSRV_ERROR eError;

	PVR_UNREFERENCED_PARAMETER(hDevCookie);

	if (!psMemInfo)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVFreeDeviceMemKM: psMemInfo is null"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (psMemInfo->sMemBlk.hResItem != IMG_NULL)
	{
		eError = ResManFreeResByPtr(psMemInfo->sMemBlk.hResItem, CLEANUP_WITH_POLL);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVFreeDeviceMemKM: ResManFreeResByPtr failed %d", eError));
			PVR_DPF((PVR_DBG_ERROR, "ResManFreeResByPtr: hResItem 0x%x", (unsigned int)psMemInfo->sMemBlk.hResItem));
		}
	}
	else
	{
		/* PVRSRV_MEM_NO_RESMAN */
		eError = FreeDeviceMemCallBack(psMemInfo, 0, CLEANUP_WITH_POLL);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVFreeDeviceMemKM: FreeDeviceMemCallBack failed %d", eError));
			PVR_DPF((PVR_DBG_ERROR, "FreeDeviceMemCallBack: psMemInfo: 0x%x", (unsigned int)psMemInfo));
		}
	}

	return eError;
}


/*!
******************************************************************************

 @Function	PVRSRVAllocDeviceMemKM

 @Description

 Allocates device memory

 @Input	   hDevCookie :
 @Input	   psPerProc : Per-process data
 @Input	   hDevMemHeap
 @Input	   ui32Flags : Some combination of PVRSRV_MEM_ flags
 @Input	   ui32Size :  Number of bytes to allocate
 @Input	   ui32Alignment :
 @Output   **ppsMemInfo : On success, receives a pointer to the created MEM_INFO structure

 @Return   PVRSRV_ERROR :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV _PVRSRVAllocDeviceMemKM(IMG_HANDLE				hDevCookie,
												  PVRSRV_PER_PROCESS_DATA	*psPerProc,
												  IMG_HANDLE				hDevMemHeap,
												  IMG_UINT32				ui32Flags,
												  IMG_SIZE_T				ui32Size,
												  IMG_SIZE_T				ui32Alignment,
												  IMG_PVOID					pvPrivData,
												  IMG_UINT32				ui32PrivDataLength,
												  IMG_UINT32				ui32ChunkSize,
												  IMG_UINT32				ui32NumVirtChunks,
												  IMG_UINT32				ui32NumPhysChunks,
												  IMG_BOOL					*pabMapChunk,
												  PVRSRV_KERNEL_MEM_INFO	**ppsMemInfo)
{
	PVRSRV_KERNEL_MEM_INFO	*psMemInfo;
	PVRSRV_ERROR 			eError;
	BM_HEAP					*psBMHeap;
	IMG_HANDLE				hDevMemContext;

	if (!hDevMemHeap ||
		((ui32Size == 0) && ((ui32Flags & PVRSRV_MEM_SPARSE) == 0)) ||
		(((ui32ChunkSize == 0) || (ui32NumVirtChunks == 0) || (ui32NumPhysChunks == 0) ||
		(pabMapChunk == IMG_NULL )) && (ui32Flags & PVRSRV_MEM_SPARSE)))
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Sprase alloc input validation */
	if (ui32Flags & PVRSRV_MEM_SPARSE)
	{
		IMG_UINT32 i;
		IMG_UINT32 ui32Check = 0;

		if (ui32NumVirtChunks < ui32NumPhysChunks)
		{
			return PVRSRV_ERROR_INVALID_PARAMS;
		}

		for (i=0;i<ui32NumVirtChunks;i++)
		{
			if (pabMapChunk[i])
			{
				ui32Check++;
			}
		}
		if (ui32NumPhysChunks != ui32Check)
		{
			return PVRSRV_ERROR_INVALID_PARAMS;
		}
	}

	/* FIXME: At the moment we force CACHETYPE override allocations to
	 *        be multiples of PAGE_SIZE and page aligned. If the RA/BM
	 *        is fixed, this limitation can be removed.
	 *
	 * INTEGRATION_POINT: HOST_PAGESIZE() is not correct, should be device-specific.
	 */
	if (ui32Flags & PVRSRV_HAP_CACHETYPE_MASK)
	{
		/* PRQA S 3415 1 */ /* order of evaluation is not important */
		if (((ui32Size % HOST_PAGESIZE()) != 0) ||
			((ui32Alignment % HOST_PAGESIZE()) != 0))
		{
			return PVRSRV_ERROR_INVALID_PARAMS;
		}
	}

	eError = AllocDeviceMem(hDevCookie,
							hDevMemHeap,
							ui32Flags,
							ui32Size,
							ui32Alignment,
							pvPrivData,
							ui32PrivDataLength,
							ui32ChunkSize,
							ui32NumVirtChunks,
							ui32NumPhysChunks,
							pabMapChunk,
							&psMemInfo);

	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	if (ui32Flags & PVRSRV_MEM_NO_SYNCOBJ)
	{
		psMemInfo->psKernelSyncInfo = IMG_NULL;
	}
	else
	{
		/*
			allocate a syncinfo but don't register with resman
			because the holding devicemem will handle the syncinfo
		*/
		psBMHeap = (BM_HEAP*)hDevMemHeap;
		hDevMemContext = (IMG_HANDLE)psBMHeap->pBMContext;
		eError = PVRSRVAllocSyncInfoKM(hDevCookie,
									   hDevMemContext,
									   &psMemInfo->psKernelSyncInfo);
		if(eError != PVRSRV_OK)
		{
			goto free_mainalloc;
		}
	}

	/*
	 * Setup the output.
	 */
	*ppsMemInfo = psMemInfo;

	if (ui32Flags & PVRSRV_MEM_NO_RESMAN)
	{
		psMemInfo->sMemBlk.hResItem = IMG_NULL;
	}
	else
	{
		/* register with the resman */
		psMemInfo->sMemBlk.hResItem = ResManRegisterRes(psPerProc->hResManContext,
														RESMAN_TYPE_DEVICEMEM_ALLOCATION,
														psMemInfo,
														0,
														&FreeDeviceMemCallBack);
		if (psMemInfo->sMemBlk.hResItem == IMG_NULL)
		{
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			PVR_DPF ((PVR_DBG_ERROR, "_PVRSRVAllocDeviceMemKM: ResManRegisterRes failed %d", eError));
			goto free_mainalloc;
		}
	}

	/* increment the refcount */
	PVRSRVKernelMemInfoIncRef(psMemInfo);

	psMemInfo->memType = PVRSRV_MEMTYPE_DEVICE;

	/*
	 * And I think we're done for now....
	 */
	return (PVRSRV_OK);

free_mainalloc:
	if (psMemInfo->psKernelSyncInfo)
	{
		PVRSRVKernelSyncInfoDecRef(psMemInfo->psKernelSyncInfo, psMemInfo);
	}
	FreeDeviceMem(psMemInfo);

	return eError;
}

#if defined(SUPPORT_ION)
static PVRSRV_ERROR IonUnmapCallback(IMG_PVOID  pvParam,
									 IMG_UINT32 ui32Param,
									 IMG_BOOL   bDummy)
{
	PVRSRV_KERNEL_MEM_INFO	*psMemInfo = (PVRSRV_KERNEL_MEM_INFO *)pvParam;
	
	PVR_UNREFERENCED_PARAMETER(bDummy);

	return FreeMemCallBackCommon(psMemInfo, ui32Param, PVRSRV_FREE_CALLBACK_ORIGIN_ALLOCATOR);
}

PVRSRV_ERROR PVRSRVIonBufferSyncAcquire(IMG_HANDLE hUnique,
										IMG_HANDLE hDevCookie,
										IMG_HANDLE hDevMemContext,
										PVRSRV_ION_SYNC_INFO **ppsIonSyncInfo)
{
	PVRSRV_ION_SYNC_INFO *psIonSyncInfo;
	PVRSRV_ERROR eError;
	IMG_BOOL bRet;

	/* Check the hash to see if we already have a sync for this buffer */
	psIonSyncInfo = (PVRSRV_ION_SYNC_INFO *) HASH_Retrieve(g_psIonSyncHash, (IMG_UINTPTR_T) hUnique);
	if (psIonSyncInfo == 0)
	{
		/* This buffer is new to us, create the syncinfo for it */
		eError = OSAllocMem(PVRSRV_PAGEABLE_SELECT,
							sizeof(PVRSRV_ION_SYNC_INFO),
							(IMG_VOID **)&psIonSyncInfo, IMG_NULL,
							"Ion Synchronization Info");
		if (eError != PVRSRV_OK)
		{
			return eError;
		}

		eError = PVRSRVAllocSyncInfoKM(hDevCookie,
									   hDevMemContext,
									   &psIonSyncInfo->psSyncInfo);
		if (eError != PVRSRV_OK)
		{
			OSFreeMem(PVRSRV_PAGEABLE_SELECT,
					  sizeof(PVRSRV_ION_SYNC_INFO),
					  psIonSyncInfo,
					  IMG_NULL);

			return eError;
		}
#if defined(SUPPORT_MEMINFO_IDS)
		psIonSyncInfo->ui64Stamp = ++g_ui64MemInfoID;
#else
		psIonSyncInfo->ui64Stamp = 0;
#endif
		bRet = HASH_Insert(g_psIonSyncHash, (IMG_UINTPTR_T) hUnique, (IMG_UINTPTR_T) psIonSyncInfo);
		if (!bRet)
		{
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;

			PVRSRVKernelSyncInfoDecRef(psIonSyncInfo->psSyncInfo, IMG_NULL);
			OSFreeMem(PVRSRV_PAGEABLE_SELECT,
					  sizeof(PVRSRV_ION_SYNC_INFO),
					  psIonSyncInfo,
					  IMG_NULL);

			return eError;
		}

		psIonSyncInfo->ui32RefCount = 0;
		psIonSyncInfo->hUnique = hUnique;
	}

	psIonSyncInfo->ui32RefCount++;
	*ppsIonSyncInfo = psIonSyncInfo;
	return PVRSRV_OK;
}

IMG_VOID PVRSRVIonBufferSyncRelease(PVRSRV_ION_SYNC_INFO *psIonSyncInfo)
{
	psIonSyncInfo->ui32RefCount--;

	if (psIonSyncInfo->ui32RefCount == 0)
	{
		PVRSRV_ION_SYNC_INFO *psLookup;
		/*
			If we're holding the last reference to the syncinfo
			then free it
		*/
		psLookup = (PVRSRV_ION_SYNC_INFO *) HASH_Remove(g_psIonSyncHash, (IMG_UINTPTR_T) psIonSyncInfo->hUnique);
		PVR_ASSERT(psLookup == psIonSyncInfo);
		PVRSRVKernelSyncInfoDecRef(psIonSyncInfo->psSyncInfo, IMG_NULL);
		OSFreeMem(PVRSRV_PAGEABLE_SELECT,
				  sizeof(PVRSRV_ION_SYNC_INFO),
				  psIonSyncInfo,
				  IMG_NULL);
	}
}

/*!
******************************************************************************

 @Function	PVRSRVMapIonHandleKM

 @Description

 Map an ION buffer into the specified device memory context

 @Input	   psPerProc : PerProcess data
 @Input    hDevCookie : Device node cookie
 @Input    hDevMemContext : Device memory context cookie
 @Input    hIon : Handle to ION buffer
 @Input    ui32Flags : Mapping flags
 @Input    ui32Size : Mapping size
 @Output   ppsKernelMemInfo: Output kernel meminfo if successful

 @Return   PVRSRV_ERROR  :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVMapIonHandleKM(PVRSRV_PER_PROCESS_DATA *psPerProc,
								  IMG_HANDLE hDevCookie,
								  IMG_HANDLE hDevMemHeap,
								  IMG_HANDLE hIon,
								  IMG_UINT32 ui32Flags,
								  IMG_UINT32 ui32ChunkCount,
								  IMG_SIZE_T *pauiOffset,
								  IMG_SIZE_T *pauiSize,
								  IMG_SIZE_T *puiIonBufferSize,
								  PVRSRV_KERNEL_MEM_INFO **ppsKernelMemInfo,
								  IMG_UINT64 *pui64Stamp)
{
	PVRSRV_ENV_PER_PROCESS_DATA *psPerProcEnv = PVRSRVProcessPrivateData(psPerProc);
	PVRSRV_DEVICE_NODE *psDeviceNode; 
	PVRSRV_KERNEL_MEM_INFO *psNewKernelMemInfo;
	IMG_SYS_PHYADDR *pasSysPhysAddr;
	IMG_SYS_PHYADDR *pasAdjustedSysPhysAddr;
	PVRSRV_MEMBLK *psMemBlock;
	PVRSRV_ERROR eError;
	IMG_HANDLE hPriv;
	IMG_HANDLE hUnique;
	BM_HANDLE hBuffer;
	IMG_SIZE_T uiMapSize = 0;
	IMG_SIZE_T uiAdjustOffset = 0;
	IMG_UINT32 ui32PageCount;
	IMG_UINT32 i;
	IMG_BOOL bAllocSync = (ui32Flags & PVRSRV_MEM_NO_SYNCOBJ)?IMG_FALSE:IMG_TRUE;

	if ((hDevCookie == IMG_NULL) || (ui32ChunkCount == 0)
		 || (hDevMemHeap == IMG_NULL) || (ppsKernelMemInfo == IMG_NULL))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid params", __FUNCTION__));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	
	for (i=0;i<ui32ChunkCount;i++)
	{
		if ((pauiOffset[i] & HOST_PAGEMASK) != 0)
		{
			PVR_DPF((PVR_DBG_ERROR,"%s: Chunk offset is not page aligned", __FUNCTION__));
			return PVRSRV_ERROR_INVALID_PARAMS;
		}

		if ((pauiSize[i] & HOST_PAGEMASK) != 0)
		{
			PVR_DPF((PVR_DBG_ERROR,"%s: Chunk size is not page aligned", __FUNCTION__));
			return PVRSRV_ERROR_INVALID_PARAMS;
		}
		uiMapSize += pauiSize[i];
	}

	psDeviceNode = (PVRSRV_DEVICE_NODE *)hDevCookie;

	if(OSAllocMem(PVRSRV_PAGEABLE_SELECT,
					sizeof(PVRSRV_KERNEL_MEM_INFO),
					(IMG_VOID **)&psNewKernelMemInfo, IMG_NULL,
					"Kernel Memory Info") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"%s: Failed to alloc memory for block", __FUNCTION__));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	OSMemSet(psNewKernelMemInfo, 0, sizeof(PVRSRV_KERNEL_MEM_INFO));

	/* Import the ION buffer into our ion_client and DMA map it */
	eError = IonImportBufferAndAquirePhysAddr(psPerProcEnv->psIONClient,
											  hIon,
											  &ui32PageCount,
											  &pasSysPhysAddr,
											  &psNewKernelMemInfo->pvLinAddrKM,
											  &hPriv,
											  &hUnique);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to get ion buffer/buffer phys addr", __FUNCTION__));
		goto exitFailedHeap;
	}

	/*
		An Ion buffer might have a number of "chunks" in it which need to be
		mapped virtually continuous so we need to create a new array of
		addresses based on this chunk data for the actual wrap
	*/
	if(OSAllocMem(PVRSRV_PAGEABLE_SELECT,
					sizeof(IMG_SYS_PHYADDR) * (uiMapSize/HOST_PAGESIZE()),
					(IMG_VOID **)&pasAdjustedSysPhysAddr, IMG_NULL,
					"Ion adjusted system address array") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"%s: Failed to alloc memory for adjusted array", __FUNCTION__));
		goto exitFailedAdjustedAlloc;
	}
	OSMemSet(pasAdjustedSysPhysAddr, 0, sizeof(IMG_SYS_PHYADDR) * (uiMapSize/HOST_PAGESIZE()));

	for (i=0;i<ui32ChunkCount;i++)
	{
		OSMemCopy(&pasAdjustedSysPhysAddr[uiAdjustOffset],
				  &pasSysPhysAddr[pauiOffset[i]/HOST_PAGESIZE()],
				  (pauiSize[i]/HOST_PAGESIZE()) * sizeof(IMG_SYS_PHYADDR));
		
		uiAdjustOffset += pauiSize[i]/HOST_PAGESIZE();
	}

	/* Wrap the returned addresses into our memory context */
	if (!BM_Wrap(hDevMemHeap,
				 uiMapSize,
				 0,
				 IMG_FALSE,
				 pasAdjustedSysPhysAddr,
				 IMG_NULL,
				 &ui32Flags,	/* This function clobbers our bits in ui32Flags */
				 &hBuffer))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to wrap ion buffer", __FUNCTION__));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto exitFailedWrap;
	}

	/* Fill in "Implementation dependant" section of mem info */
	psMemBlock = &psNewKernelMemInfo->sMemBlk;
	psMemBlock->sDevVirtAddr = BM_HandleToDevVaddr(hBuffer);
	psMemBlock->hOSMemHandle = BM_HandleToOSMemHandle(hBuffer);
	psMemBlock->hBuffer = (IMG_HANDLE) hBuffer;
	psMemBlock->hOSWrapMem = hPriv;			/* Saves creating a new element as we know hOSWrapMem will not be used */
	psMemBlock->psIntSysPAddr = pasAdjustedSysPhysAddr;

	psNewKernelMemInfo->ui32Flags = ui32Flags;
	psNewKernelMemInfo->sDevVAddr = psMemBlock->sDevVirtAddr;
	psNewKernelMemInfo->uAllocSize = uiMapSize;
	psNewKernelMemInfo->memType = PVRSRV_MEMTYPE_ION;
	PVRSRVKernelMemInfoIncRef(psNewKernelMemInfo);

	/* Clear the Backup buffer pointer as we do not have one at this point. We only allocate this as we are going up/down */
	psNewKernelMemInfo->pvSysBackupBuffer = IMG_NULL;

	if (!bAllocSync)
	{
		psNewKernelMemInfo->psKernelSyncInfo = IMG_NULL;
	}
	else
	{
		PVRSRV_ION_SYNC_INFO *psIonSyncInfo;
		BM_HEAP *psBMHeap;
		IMG_HANDLE hDevMemContext;

		psBMHeap = (BM_HEAP*)hDevMemHeap;
		hDevMemContext = (IMG_HANDLE)psBMHeap->pBMContext;

		eError = PVRSRVIonBufferSyncInfoIncRef(hUnique,
											   hDevCookie,
											   hDevMemContext,
											   &psIonSyncInfo,
											   psNewKernelMemInfo);
		if(eError != PVRSRV_OK)
		{
			goto exitFailedSync;
		}
		psNewKernelMemInfo->hIonSyncInfo = psIonSyncInfo;
		psNewKernelMemInfo->psKernelSyncInfo = IonBufferSyncGetKernelSyncInfo(psIonSyncInfo);
		*pui64Stamp = IonBufferSyncGetStamp(psIonSyncInfo);
	}

	/* register with the resman */
	psNewKernelMemInfo->sMemBlk.hResItem = ResManRegisterRes(psPerProc->hResManContext,
															 RESMAN_TYPE_DEVICEMEM_ION,
															 psNewKernelMemInfo,
															 0,
															 &IonUnmapCallback);
	if (psNewKernelMemInfo->sMemBlk.hResItem == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		PVR_DPF ((PVR_DBG_ERROR, "PVRSRVMapIonHandleKM: ResManRegisterRes failed %d", eError));
		goto exitFailedResman;
	}

	psNewKernelMemInfo->memType = PVRSRV_MEMTYPE_ION;

	/*
		As the user doesn't tell us the size, just the "chunk" information
		return actual size of the Ion buffer so we can mmap it.
	*/
	*puiIonBufferSize = ui32PageCount * HOST_PAGESIZE();
	*ppsKernelMemInfo = psNewKernelMemInfo;
	return PVRSRV_OK;

exitFailedResman:
	if (psNewKernelMemInfo->psKernelSyncInfo)
	{
		PVRSRVIonBufferSyncInfoDecRef(psNewKernelMemInfo->hIonSyncInfo, psNewKernelMemInfo);
	}
exitFailedSync:
	BM_Free(hBuffer, ui32Flags);
exitFailedWrap:
	OSFreeMem(PVRSRV_PAGEABLE_SELECT,
			  sizeof(IMG_SYS_PHYADDR) * uiAdjustOffset,
			  pasAdjustedSysPhysAddr,
			  IMG_NULL);
exitFailedAdjustedAlloc:
	IonUnimportBufferAndReleasePhysAddr(hPriv);
exitFailedHeap:
	OSFreeMem(PVRSRV_PAGEABLE_SELECT,
			  sizeof(PVRSRV_KERNEL_MEM_INFO),
			  psNewKernelMemInfo,
			  IMG_NULL);

	return eError;
}

/*!
******************************************************************************

 @Function	PVRSRVUnmapIonHandleKM

 @Description

 Frees an ion buffer mapped with PVRSRVMapIonHandleKM, including the mem_info structure

 @Input	   psMemInfo :

 @Return   PVRSRV_ERROR  :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnmapIonHandleKM(PVRSRV_KERNEL_MEM_INFO *psMemInfo)
{
	if (!psMemInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	return ResManFreeResByPtr(psMemInfo->sMemBlk.hResItem, CLEANUP_WITH_POLL);
}
#endif	/* SUPPORT_ION */

/*!
******************************************************************************

 @Function	PVRSRVDissociateDeviceMemKM

 @Description

 Dissociates memory from the process that allocates it.  Intended for
 transfering the ownership of device memory from a particular process
 to the kernel.

 @Input	   psMemInfo :

 @Return   PVRSRV_ERROR  :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVDissociateDeviceMemKM(IMG_HANDLE              hDevCookie,
													  PVRSRV_KERNEL_MEM_INFO *psMemInfo)
{
	PVRSRV_ERROR		eError;
	PVRSRV_DEVICE_NODE	*psDeviceNode = hDevCookie;

	PVR_UNREFERENCED_PARAMETER(hDevCookie);

	if (!psMemInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = ResManDissociateRes(psMemInfo->sMemBlk.hResItem, psDeviceNode->hResManContext);

	PVR_ASSERT(eError == PVRSRV_OK);

	return eError;
}


/*!
******************************************************************************

 @Function	PVRSRVGetFreeDeviceMemKM

 @Description

 Determines how much memory remains available in the system with the specified
 capabilities.

 @Input	   ui32Flags :

 @Output   pui32Total :

 @Output   pui32Free :

 @Output   pui32LargestBlock :

 @Return   PVRSRV_ERROR  :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetFreeDeviceMemKM(IMG_UINT32 ui32Flags,
												   IMG_SIZE_T *pui32Total,
												   IMG_SIZE_T *pui32Free,
												   IMG_SIZE_T *pui32LargestBlock)
{
	/* TO BE IMPLEMENTED */

	PVR_UNREFERENCED_PARAMETER(ui32Flags);
	PVR_UNREFERENCED_PARAMETER(pui32Total);
	PVR_UNREFERENCED_PARAMETER(pui32Free);
	PVR_UNREFERENCED_PARAMETER(pui32LargestBlock);

	return PVRSRV_OK;
}




/*!
******************************************************************************
	@Function   PVRSRVUnwrapExtMemoryKM

	@Description  On last unwrap of a given meminfo, unmaps physical pages from a
				wrapped allocation, and frees the associated device address space.
				Note: this can only unmap memory mapped by PVRSRVWrapExtMemory

	@Input	    psMemInfo - mem info describing the wrapped allocation
	@Return     None
******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnwrapExtMemoryKM (PVRSRV_KERNEL_MEM_INFO	*psMemInfo)
{
	if (!psMemInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	return ResManFreeResByPtr(psMemInfo->sMemBlk.hResItem, CLEANUP_WITH_POLL);
}


/*!
******************************************************************************
	@Function   UnwrapExtMemoryCallBack

	@Description Resman callback to unwrap memory

	@Input	    pvParam - opaque void ptr param
	@Input	    ui32Param - opaque unsigned long param
	@Return     PVRSRV_ERROR
******************************************************************************/
static PVRSRV_ERROR UnwrapExtMemoryCallBack(IMG_PVOID  pvParam,
											IMG_UINT32 ui32Param,
											IMG_BOOL   bDummy)
{
	PVRSRV_KERNEL_MEM_INFO	*psMemInfo = (PVRSRV_KERNEL_MEM_INFO *)pvParam;
	
	PVR_UNREFERENCED_PARAMETER(bDummy);

	return FreeMemCallBackCommon(psMemInfo, ui32Param,
								 PVRSRV_FREE_CALLBACK_ORIGIN_ALLOCATOR);
}


/*!
******************************************************************************
	@Function   PVRSRVWrapExtMemoryKM

	@Description  Allocates a Device Virtual Address in the shared mapping heap
				and maps physical pages into that allocation. Note, if the pages are
				already mapped into the heap, the existing allocation is returned.

	@Input	    hDevCookie - Device cookie
	@Input	    psPerProc - Per-process data
	@Input	    hDevMemContext - device memory context
	@Input	    uByteSize - Size of allocation
	@Input	    uPageOffset - Offset into the first page of the memory to be wrapped
	@Input	    bPhysContig - whether the underlying memory is physically contiguous
	@Input	    psExtSysPAddr - The list of Device Physical page addresses
	@Input	    pvLinAddr - ptr to buffer to wrap
	@Output     ppsMemInfo - mem info describing the wrapped allocation
	@Return     None
******************************************************************************/

IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVWrapExtMemoryKM(IMG_HANDLE				hDevCookie,
												PVRSRV_PER_PROCESS_DATA	*psPerProc,
												IMG_HANDLE				hDevMemContext,
												IMG_SIZE_T 				uByteSize,
												IMG_SIZE_T				uPageOffset,
												IMG_BOOL				bPhysContig,
												IMG_SYS_PHYADDR	 		*psExtSysPAddr,
												IMG_VOID 				*pvLinAddr,
												IMG_UINT32				ui32Flags,
												PVRSRV_KERNEL_MEM_INFO	**ppsMemInfo)
{
	PVRSRV_KERNEL_MEM_INFO *psMemInfo = IMG_NULL;
	DEVICE_MEMORY_INFO  *psDevMemoryInfo;
	IMG_SIZE_T			ui32HostPageSize = HOST_PAGESIZE();
	IMG_HANDLE			hDevMemHeap = IMG_NULL;
	PVRSRV_DEVICE_NODE* psDeviceNode;
	BM_HANDLE 			hBuffer;
	PVRSRV_MEMBLK		*psMemBlock;
	IMG_BOOL			bBMError;
	BM_HEAP				*psBMHeap;
	PVRSRV_ERROR		eError;
	IMG_VOID 			*pvPageAlignedCPUVAddr;
	IMG_SYS_PHYADDR	 	*psIntSysPAddr = IMG_NULL;
	IMG_HANDLE			hOSWrapMem = IMG_NULL;
	DEVICE_MEMORY_HEAP_INFO *psDeviceMemoryHeap;
	IMG_UINT32		i;
	IMG_SIZE_T          uPageCount = 0;

	PVR_DPF ((PVR_DBG_MESSAGE,
			"PVRSRVWrapExtMemoryKM (uSize=0x%x, uPageOffset=0x%x, bPhysContig=%d, extSysPAddr=" SYSPADDR_FMT
			", pvLinAddr=%p, ui32Flags=%u)",
			uByteSize,
			uPageOffset,
			bPhysContig,
			psExtSysPAddr->uiAddr,
			pvLinAddr,
			ui32Flags));

	psDeviceNode = (PVRSRV_DEVICE_NODE*)hDevCookie;
	PVR_ASSERT(psDeviceNode != IMG_NULL);

	if (psDeviceNode == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVWrapExtMemoryKM: invalid parameter"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if(pvLinAddr)
	{
		/* derive the page offset from the cpu ptr (in case it's not supplied) */
		uPageOffset = (IMG_UINTPTR_T)pvLinAddr & (ui32HostPageSize - 1);

		/* get the pagecount and the page aligned base ptr */
		uPageCount = HOST_PAGEALIGN(uByteSize + uPageOffset) / ui32HostPageSize;
		pvPageAlignedCPUVAddr = (IMG_VOID *)((IMG_UINTPTR_T)pvLinAddr - uPageOffset);

		/* allocate array of SysPAddr to hold page addresses */
		if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
						uPageCount * sizeof(IMG_SYS_PHYADDR),
						(IMG_VOID **)&psIntSysPAddr, IMG_NULL,
						"Array of Page Addresses") != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVWrapExtMemoryKM: Failed to alloc memory for block"));
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}

		eError = OSAcquirePhysPageAddr(pvPageAlignedCPUVAddr,
										uPageCount * ui32HostPageSize,
										psIntSysPAddr,
										&hOSWrapMem);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVWrapExtMemoryKM: Failed to alloc memory for block"));
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;//FIXME: need better error code
			goto ErrorExitPhase1;
		}

		/* replace the supplied page address list */
		psExtSysPAddr = psIntSysPAddr;

		/* assume memory is not physically contiguous;
  		   we shouldn't trust what the user says here
  		*/
		bPhysContig = IMG_FALSE;
	}

	/* Choose the heap to map to */
	psDevMemoryInfo = &((BM_CONTEXT*)hDevMemContext)->psDeviceNode->sDevMemoryInfo;
	psDeviceMemoryHeap = psDevMemoryInfo->psDeviceMemoryHeap;
	for(i=0; i<PVRSRV_MAX_CLIENT_HEAPS; i++)
	{
		if(HEAP_IDX(psDeviceMemoryHeap[i].ui32HeapID) == psDevMemoryInfo->ui32MappingHeapID)
		{
			if(psDeviceMemoryHeap[i].DevMemHeapType == DEVICE_MEMORY_HEAP_PERCONTEXT)
			{
				if (psDeviceMemoryHeap[i].ui32HeapSize > 0)
				{
					hDevMemHeap = BM_CreateHeap(hDevMemContext, &psDeviceMemoryHeap[i]);
				}
				else
				{
					hDevMemHeap = IMG_NULL;
				}
			}
			else
			{
				hDevMemHeap = psDevMemoryInfo->psDeviceMemoryHeap[i].hDevMemHeap;
			}
			break;
		}
	}

	if(hDevMemHeap == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVWrapExtMemoryKM: unable to find mapping heap"));
		eError = PVRSRV_ERROR_UNABLE_TO_FIND_MAPPING_HEAP;
		goto ErrorExitPhase2;
	}

	if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
					sizeof(PVRSRV_KERNEL_MEM_INFO),
					(IMG_VOID **)&psMemInfo, IMG_NULL,
					"Kernel Memory Info") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVWrapExtMemoryKM: Failed to alloc memory for block"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorExitPhase2;
	}

	OSMemSet(psMemInfo, 0, sizeof(*psMemInfo));
	/*
		Force the memory to be read/write. This used to be done in the BM, but
		ion imports don't want this behaviour
	*/
	psMemInfo->ui32Flags = ui32Flags | PVRSRV_MEM_READ | PVRSRV_MEM_WRITE;

	psMemBlock = &(psMemInfo->sMemBlk);

	bBMError = BM_Wrap(hDevMemHeap,
					   uByteSize,
					   uPageOffset,
					   bPhysContig,
					   psExtSysPAddr,
					   IMG_NULL,
					   &psMemInfo->ui32Flags,
					   &hBuffer);
	if (!bBMError)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVWrapExtMemoryKM: BM_Wrap Failed"));
		eError = PVRSRV_ERROR_BAD_MAPPING;
		goto ErrorExitPhase3;
	}

	/* Fill in "Implementation dependant" section of mem info */
	psMemBlock->sDevVirtAddr = BM_HandleToDevVaddr(hBuffer);
	psMemBlock->hOSMemHandle = BM_HandleToOSMemHandle(hBuffer);
	psMemBlock->hOSWrapMem = hOSWrapMem;
	psMemBlock->psIntSysPAddr = psIntSysPAddr;

	/* Convert from BM_HANDLE to external IMG_HANDLE */
	psMemBlock->hBuffer = (IMG_HANDLE)hBuffer;

	/* Fill in the public fields of the MEM_INFO structure */
	psMemInfo->pvLinAddrKM = BM_HandleToCpuVaddr(hBuffer);
	psMemInfo->sDevVAddr = psMemBlock->sDevVirtAddr;
	psMemInfo->uAllocSize = uByteSize;

	/* Clear the Backup buffer pointer as we do not have one at this point.
	   We only allocate this as we are going up/down
	 */
	psMemInfo->pvSysBackupBuffer = IMG_NULL;

	/*
		allocate a syncinfo but don't register with resman
		because the holding devicemem will handle the syncinfo
	*/
	psBMHeap = (BM_HEAP*)hDevMemHeap;
	hDevMemContext = (IMG_HANDLE)psBMHeap->pBMContext;
	eError = PVRSRVAllocSyncInfoKM(hDevCookie,
									hDevMemContext,
									&psMemInfo->psKernelSyncInfo);
	if(eError != PVRSRV_OK)
	{
		goto ErrorExitPhase4;
	}

	/* increment the refcount */
	PVRSRVKernelMemInfoIncRef(psMemInfo);

	psMemInfo->memType = PVRSRV_MEMTYPE_WRAPPED;

	/* Register Resource */
	psMemInfo->sMemBlk.hResItem = ResManRegisterRes(psPerProc->hResManContext,
													RESMAN_TYPE_DEVICEMEM_WRAP,
													psMemInfo,
													0,
													&UnwrapExtMemoryCallBack);

	/* return the meminfo */
	*ppsMemInfo = psMemInfo;

	return PVRSRV_OK;

	/* error handling: */

ErrorExitPhase4:
	if(psMemInfo)
	{
		FreeDeviceMem(psMemInfo);
		/*
			FreeDeviceMem will free the meminfo so set
			it to NULL to avoid double free below
		*/
		psMemInfo = IMG_NULL;
	}

ErrorExitPhase3:
	if(psMemInfo)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_KERNEL_MEM_INFO), psMemInfo, IMG_NULL);
		/*not nulling pointer, out of scope*/
	}

ErrorExitPhase2:
	if(psIntSysPAddr)
	{
		OSReleasePhysPageAddr(hOSWrapMem);
	}

ErrorExitPhase1:
	if(psIntSysPAddr)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, uPageCount * sizeof(IMG_SYS_PHYADDR), psIntSysPAddr, IMG_NULL);
		/*not nulling shared pointer, uninitialized to this point*/
	}

	return eError;
}


/*!
******************************************************************************

 @Function	PVRSRVUnmapDeviceMemoryKM

 @Description
 		Unmaps an existing allocation previously mapped by PVRSRVMapDeviceMemory

 @Input    psMemInfo

 @Return   PVRSRV_ERROR :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnmapDeviceMemoryKM (PVRSRV_KERNEL_MEM_INFO *psMemInfo)
{
	if (!psMemInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	return ResManFreeResByPtr(psMemInfo->sMemBlk.hResItem, CLEANUP_WITH_POLL);
}


/*!
******************************************************************************
	@Function   UnmapDeviceMemoryCallBack

	@Description Resman callback to unmap memory memory previously mapped
				from one allocation to another

	@Input	    pvParam - opaque void ptr param
	@Input	    ui32Param - opaque unsigned long param
	@Return     PVRSRV_ERROR
******************************************************************************/
static PVRSRV_ERROR UnmapDeviceMemoryCallBack(IMG_PVOID  pvParam,
											  IMG_UINT32 ui32Param,
											  IMG_BOOL   bDummy)
{
	PVRSRV_ERROR				eError;
	RESMAN_MAP_DEVICE_MEM_DATA	*psMapData = pvParam;

	PVR_UNREFERENCED_PARAMETER(ui32Param);
	PVR_UNREFERENCED_PARAMETER(bDummy);

	if(psMapData->psMemInfo->sMemBlk.psIntSysPAddr)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(IMG_SYS_PHYADDR), psMapData->psMemInfo->sMemBlk.psIntSysPAddr, IMG_NULL);
		psMapData->psMemInfo->sMemBlk.psIntSysPAddr = IMG_NULL;
	}

	if( psMapData->psMemInfo->psKernelSyncInfo )
	{
		PVRSRVKernelSyncInfoDecRef(psMapData->psMemInfo->psKernelSyncInfo, psMapData->psMemInfo);
	}

	eError = FreeDeviceMem(psMapData->psMemInfo);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"UnmapDeviceMemoryCallBack: Failed to free DST meminfo"));
		return eError;
	}

	/* This will only free the src psMemInfo if we hold the last reference */
	eError = FreeMemCallBackCommon(psMapData->psSrcMemInfo, 0,
								   PVRSRV_FREE_CALLBACK_ORIGIN_IMPORTER);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "UnmapDeviceMemoryCallBack: FreeMemCallBackCommon Failed %d", eError));
		PVR_DPF((PVR_DBG_ERROR, "UnmapDeviceMemoryCallBack: psSrcMemInfo: 0x%x", (unsigned int)psMapData->psSrcMemInfo));
	}

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(RESMAN_MAP_DEVICE_MEM_DATA), psMapData, IMG_NULL);
	/*not nulling pointer, copy on stack*/

	return eError;
}


/*!
******************************************************************************

 @Function	PVRSRVMapDeviceMemoryKM

 @Description
 		Maps an existing allocation to a specific device address space and heap
 		Note: it's valid to map from one physical device to another

 @Input	   psPerProc : Per-process data
 @Input    psSrcMemInfo
 @Input    hDstDevMemHeap
 @Input    ppsDstMemInfo

 @Return   PVRSRV_ERROR :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVMapDeviceMemoryKM(PVRSRV_PER_PROCESS_DATA	*psPerProc,
												  PVRSRV_KERNEL_MEM_INFO	*psSrcMemInfo,
												  IMG_HANDLE				hDstDevMemHeap,
												  PVRSRV_KERNEL_MEM_INFO	**ppsDstMemInfo)
{
	PVRSRV_ERROR				eError;
	IMG_UINT32					i;
	IMG_SIZE_T					uPageCount, uPageOffset;
	IMG_SIZE_T					ui32HostPageSize = HOST_PAGESIZE();
	IMG_SYS_PHYADDR				*psSysPAddr = IMG_NULL;
	IMG_DEV_PHYADDR				sDevPAddr;
	BM_BUF						*psBuf;
	IMG_DEV_VIRTADDR			sDevVAddr;
	PVRSRV_KERNEL_MEM_INFO		*psMemInfo = IMG_NULL;
	BM_HANDLE 					hBuffer;
	PVRSRV_MEMBLK				*psMemBlock;
	IMG_BOOL					bBMError;
	PVRSRV_DEVICE_NODE			*psDeviceNode;
	IMG_VOID 					*pvPageAlignedCPUVAddr;
	RESMAN_MAP_DEVICE_MEM_DATA	*psMapData = IMG_NULL;

	/* check params */
	if(!psSrcMemInfo || !hDstDevMemHeap || !ppsDstMemInfo)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceMemoryKM: invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* initialise the Dst Meminfo to NULL*/
	*ppsDstMemInfo = IMG_NULL;

	uPageOffset = psSrcMemInfo->sDevVAddr.uiAddr & (ui32HostPageSize - 1);
	uPageCount = HOST_PAGEALIGN(psSrcMemInfo->uAllocSize + uPageOffset) / ui32HostPageSize;
	pvPageAlignedCPUVAddr = (IMG_VOID *)((IMG_UINTPTR_T)psSrcMemInfo->pvLinAddrKM - uPageOffset);

	/*
		allocate array of SysPAddr to hold SRC allocation page addresses
	*/
	if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
					uPageCount*sizeof(IMG_SYS_PHYADDR),
					(IMG_VOID **)&psSysPAddr, IMG_NULL,
					"Array of Page Addresses") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceMemoryKM: Failed to alloc memory for block"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psBuf = psSrcMemInfo->sMemBlk.hBuffer;

	/* get the device node */
	psDeviceNode = psBuf->pMapping->pBMHeap->pBMContext->psDeviceNode;

	/* build a list of physical page addresses */
	sDevVAddr.uiAddr = psSrcMemInfo->sDevVAddr.uiAddr - IMG_CAST_TO_DEVVADDR_UINT(uPageOffset);
	for(i=0; i<uPageCount; i++)
	{
		BM_GetPhysPageAddr(psSrcMemInfo, sDevVAddr, &sDevPAddr);

		/* save the address */
		psSysPAddr[i] = SysDevPAddrToSysPAddr (psDeviceNode->sDevId.eDeviceType, sDevPAddr);

		/* advance the DevVaddr one page */
		sDevVAddr.uiAddr += IMG_CAST_TO_DEVVADDR_UINT(ui32HostPageSize);
	}

	/* allocate the resman map data */
	if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
					sizeof(RESMAN_MAP_DEVICE_MEM_DATA),
					(IMG_VOID **)&psMapData, IMG_NULL,
					"Resource Manager Map Data") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceMemoryKM: Failed to alloc resman map data"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorExit;
	}

	if(OSAllocMem(PVRSRV_PAGEABLE_SELECT,
					sizeof(PVRSRV_KERNEL_MEM_INFO),
					(IMG_VOID **)&psMemInfo, IMG_NULL,
					"Kernel Memory Info") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceMemoryKM: Failed to alloc memory for block"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorExit;
	}

	OSMemSet(psMemInfo, 0, sizeof(*psMemInfo));

	/*
		Force the memory to be read/write. This used to be done in the BM, but
		ion imports don't want this behaviour
	*/
	psMemInfo->ui32Flags = psSrcMemInfo->ui32Flags | PVRSRV_MEM_READ | PVRSRV_MEM_WRITE;

	psMemBlock = &(psMemInfo->sMemBlk);

	bBMError = BM_Wrap(hDstDevMemHeap,
					   psSrcMemInfo->uAllocSize,
					   uPageOffset,
					   IMG_FALSE,
					   psSysPAddr,
					   pvPageAlignedCPUVAddr,
					   &psMemInfo->ui32Flags,
					   &hBuffer);

	if (!bBMError)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceMemoryKM: BM_Wrap Failed"));
		eError = PVRSRV_ERROR_BAD_MAPPING;
		goto ErrorExit;
	}

	/* Fill in "Implementation dependant" section of mem info */
	psMemBlock->sDevVirtAddr = BM_HandleToDevVaddr(hBuffer);
	psMemBlock->hOSMemHandle = BM_HandleToOSMemHandle(hBuffer);

	/* Convert from BM_HANDLE to external IMG_HANDLE */
	psMemBlock->hBuffer = (IMG_HANDLE)hBuffer;

	/* Store page list */
	psMemBlock->psIntSysPAddr = psSysPAddr;

	/* patch up the CPU VAddr into the meminfo */
	psMemInfo->pvLinAddrKM = psSrcMemInfo->pvLinAddrKM;

	/* Fill in the public fields of the MEM_INFO structure */
	psMemInfo->sDevVAddr = psMemBlock->sDevVirtAddr;
	psMemInfo->uAllocSize = psSrcMemInfo->uAllocSize;
	psMemInfo->psKernelSyncInfo = psSrcMemInfo->psKernelSyncInfo;

	/* reference the same ksi that the original meminfo referenced */
	if(psMemInfo->psKernelSyncInfo)
	{
		PVRSRVKernelSyncInfoIncRef(psMemInfo->psKernelSyncInfo, psMemInfo);
	}

	/* Clear the Backup buffer pointer as we do not have one at this point.
	   We only allocate this as we are going up/down
	 */
	psMemInfo->pvSysBackupBuffer = IMG_NULL;

	/* increment our refcount */
	PVRSRVKernelMemInfoIncRef(psMemInfo);

	/* increment the src refcount */
	PVRSRVKernelMemInfoIncRef(psSrcMemInfo);

	/* Tell the buffer manager about the export */
	BM_Export(psSrcMemInfo->sMemBlk.hBuffer);

	psMemInfo->memType = PVRSRV_MEMTYPE_MAPPED;

	/* setup the resman map data */
	psMapData->psMemInfo = psMemInfo;
	psMapData->psSrcMemInfo = psSrcMemInfo;

	/* Register Resource */
	psMemInfo->sMemBlk.hResItem = ResManRegisterRes(psPerProc->hResManContext,
													RESMAN_TYPE_DEVICEMEM_MAPPING,
													psMapData,
													0,
													&UnmapDeviceMemoryCallBack);

	*ppsDstMemInfo = psMemInfo;

	return PVRSRV_OK;

	/* error handling: */

ErrorExit:

	if(psSysPAddr)
	{
		/* Free the page address list */
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(IMG_SYS_PHYADDR), psSysPAddr, IMG_NULL);
		/*not nulling shared pointer, holding structure could be not initialized*/
	}

	if(psMemInfo)
	{
		/* Free the page address list */
		OSFreeMem(PVRSRV_PAGEABLE_SELECT, sizeof(PVRSRV_KERNEL_MEM_INFO), psMemInfo, IMG_NULL);
		/*not nulling shared pointer, holding structure could be not initialized*/
	}

	if(psMapData)
	{
		/* Free the resman map data */
		OSFreeMem(PVRSRV_PAGEABLE_SELECT, sizeof(RESMAN_MAP_DEVICE_MEM_DATA), psMapData, IMG_NULL);
		/*not nulling pointer, out of scope*/
	}

	return eError;
}


/*!
******************************************************************************
	@Function   PVRSRVUnmapDeviceClassMemoryKM

	@Description  unmaps physical pages from devices address space at a specified
				Device Virtual Address.
				Note: this can only unmap memory mapped by
				PVRSRVMapDeviceClassMemoryKM

	@Input	    psMemInfo - mem info describing the device virtual address
									to unmap RAM from
	@Return     None
******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnmapDeviceClassMemoryKM(PVRSRV_KERNEL_MEM_INFO *psMemInfo)
{
	if (!psMemInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	return ResManFreeResByPtr(psMemInfo->sMemBlk.hResItem, CLEANUP_WITH_POLL);
}


/*!
******************************************************************************
	@Function   UnmapDeviceClassMemoryCallBack

	@Description Resman callback to unmap device class memory

	@Input	    pvParam - opaque void ptr param
	@Input	    ui32Param - opaque unsigned long param
	@Return     PVRSRV_ERROR
******************************************************************************/
static PVRSRV_ERROR UnmapDeviceClassMemoryCallBack(IMG_PVOID  pvParam,
												   IMG_UINT32 ui32Param,
												   IMG_BOOL   bDummy)
{
	PVRSRV_DC_MAPINFO *psDCMapInfo = pvParam;
	PVRSRV_KERNEL_MEM_INFO *psMemInfo;

	PVR_UNREFERENCED_PARAMETER(ui32Param);
	PVR_UNREFERENCED_PARAMETER(bDummy);

	psMemInfo = psDCMapInfo->psMemInfo;

#if defined(SUPPORT_MEMORY_TILING)
	if(psDCMapInfo->ui32TilingStride > 0)
	{
		PVRSRV_DEVICE_NODE *psDeviceNode = psDCMapInfo->psDeviceNode;

		if (psDeviceNode->pfnFreeMemTilingRange(psDeviceNode,
												psDCMapInfo->ui32RangeIndex) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"UnmapDeviceClassMemoryCallBack: FreeMemTilingRange failed"));
		}
	}
#endif

	(psDCMapInfo->psDeviceClassBuffer->ui32MemMapRefCount)--;

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_DC_MAPINFO), psDCMapInfo, IMG_NULL);

	return FreeMemCallBackCommon(psMemInfo, ui32Param,
								 PVRSRV_FREE_CALLBACK_ORIGIN_ALLOCATOR);
}


/*!
******************************************************************************
	@Function   PVRSRVMapDeviceClassMemoryKM

	@Description  maps physical pages for DeviceClass buffers into a devices
				address space at a specified and pre-allocated Device
				Virtual Address

	@Input	    psPerProc - Per-process data
	@Input	    hDevMemContext - Device memory context
	@Input	    hDeviceClassBuffer - Device Class Buffer (Surface) handle
	@Input	    hDevMemContext - device memory context to which mapping
										is made
	@Output     ppsMemInfo - mem info describing the mapped memory
	@Output     phOSMapInfo - OS specific mapping information
	@Return     None
******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVMapDeviceClassMemoryKM(PVRSRV_PER_PROCESS_DATA	*psPerProc,
													   IMG_HANDLE				hDevMemContext,
													   IMG_HANDLE				hDeviceClassBuffer,
													   PVRSRV_KERNEL_MEM_INFO	**ppsMemInfo,
													   IMG_HANDLE				*phOSMapInfo)
{
	PVRSRV_ERROR eError;
	PVRSRV_DEVICE_NODE* psDeviceNode;
	PVRSRV_KERNEL_MEM_INFO *psMemInfo = IMG_NULL;
	PVRSRV_DEVICECLASS_BUFFER *psDeviceClassBuffer;
	IMG_SYS_PHYADDR *psSysPAddr;
	IMG_VOID *pvCPUVAddr, *pvPageAlignedCPUVAddr;
	IMG_BOOL bPhysContig;
	BM_CONTEXT *psBMContext;
	DEVICE_MEMORY_INFO *psDevMemoryInfo;
	DEVICE_MEMORY_HEAP_INFO *psDeviceMemoryHeap;
	IMG_HANDLE hDevMemHeap = IMG_NULL;
	IMG_UINT32 ui32ByteSize;
	IMG_SIZE_T uOffset;
	IMG_SIZE_T uPageSize = HOST_PAGESIZE();
	BM_HANDLE		hBuffer;
	PVRSRV_MEMBLK	*psMemBlock;
	IMG_BOOL		bBMError;
	IMG_UINT32 i;
	PVRSRV_DC_MAPINFO *psDCMapInfo = IMG_NULL;

	if(!hDeviceClassBuffer || !ppsMemInfo || !phOSMapInfo || !hDevMemContext)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceClassMemoryKM: invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* allocate resman storage structure */
	if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
					sizeof(PVRSRV_DC_MAPINFO),
					(IMG_VOID **)&psDCMapInfo, IMG_NULL,
					"PVRSRV_DC_MAPINFO") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceClassMemoryKM: Failed to alloc memory for psDCMapInfo"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	OSMemSet(psDCMapInfo, 0, sizeof(PVRSRV_DC_MAPINFO));

	psDeviceClassBuffer = (PVRSRV_DEVICECLASS_BUFFER*)hDeviceClassBuffer;

	/*
		call into external driver to get info so we can map a meminfo
		Notes:
		It's expected that third party displays will only support
		physically contiguous display surfaces.  However, it's possible
		a given display may have an MMU and therefore support non-contig'
		display surfaces.

		If surfaces are contiguous, ext driver should return:
		 - a CPU virtual address, or IMG_NULL where the surface is not mapped to CPU
		 - (optional) an OS Mapping handle for KM->UM surface mapping
		 - the size in bytes
		 - a single system physical address

		If surfaces are non-contiguous, ext driver should return:
		 - a CPU virtual address
		 - (optional) an OS Mapping handle for KM->UM surface mapping
		 - the size in bytes (must be multiple of 4kB)
		 - a list of system physical addresses (at 4kB intervals)
	*/
	eError = psDeviceClassBuffer->pfnGetBufferAddr(psDeviceClassBuffer->hExtDevice,
												   psDeviceClassBuffer->hExtBuffer,
												   &psSysPAddr,
												   &ui32ByteSize,
												   &pvCPUVAddr,
												   phOSMapInfo,
												   &bPhysContig,
												   &psDCMapInfo->ui32TilingStride);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceClassMemoryKM: unable to get buffer address"));
		goto ErrorExitPhase1;
	}

	/* Choose the heap to map to */
	psBMContext = (BM_CONTEXT*)psDeviceClassBuffer->hDevMemContext;
	psDeviceNode = psBMContext->psDeviceNode;
	psDevMemoryInfo = &psDeviceNode->sDevMemoryInfo;
	psDeviceMemoryHeap = psDevMemoryInfo->psDeviceMemoryHeap;
	for(i=0; i<PVRSRV_MAX_CLIENT_HEAPS; i++)
	{
		if(HEAP_IDX(psDeviceMemoryHeap[i].ui32HeapID) == psDevMemoryInfo->ui32MappingHeapID)
		{
			if(psDeviceMemoryHeap[i].DevMemHeapType == DEVICE_MEMORY_HEAP_PERCONTEXT)
			{
				if (psDeviceMemoryHeap[i].ui32HeapSize > 0)
				{
					hDevMemHeap = BM_CreateHeap(hDevMemContext, &psDeviceMemoryHeap[i]);
				}
				else
				{
					hDevMemHeap = IMG_NULL;
				}
			}
			else
			{
				hDevMemHeap = psDevMemoryInfo->psDeviceMemoryHeap[i].hDevMemHeap;
			}
			break;
		}
	}

	if(hDevMemHeap == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceClassMemoryKM: unable to find mapping heap"));
		eError = PVRSRV_ERROR_UNABLE_TO_FIND_RESOURCE;
		goto ErrorExitPhase1;
	}

	/* Only need lower 12 bits of the cpu addr - don't care what size a void* is */
	uOffset = ((IMG_UINTPTR_T)pvCPUVAddr) & (uPageSize - 1);
	pvPageAlignedCPUVAddr = (IMG_VOID *)((IMG_UINTPTR_T)pvCPUVAddr - uOffset);

	eError = OSAllocMem(PVRSRV_PAGEABLE_SELECT,
					sizeof(PVRSRV_KERNEL_MEM_INFO),
					(IMG_VOID **)&psMemInfo, IMG_NULL,
					"Kernel Memory Info");
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceClassMemoryKM: Failed to alloc memory for block"));
		goto ErrorExitPhase1;
	}

	OSMemSet(psMemInfo, 0, sizeof(*psMemInfo));

	/*
		Force the memory to be read/write. This used to be done in the BM, but
		ion imports don't want this behaviour
	*/
	psMemInfo->ui32Flags |= PVRSRV_MEM_READ | PVRSRV_MEM_WRITE;

	psMemBlock = &(psMemInfo->sMemBlk);

	bBMError = BM_Wrap(hDevMemHeap,
					   ui32ByteSize,
					   uOffset,
					   bPhysContig,
					   psSysPAddr,
					   pvPageAlignedCPUVAddr,
					   &psMemInfo->ui32Flags,
					   &hBuffer);

	if (!bBMError)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceClassMemoryKM: BM_Wrap Failed"));
		/*not nulling pointer, out of scope*/
		eError = PVRSRV_ERROR_BAD_MAPPING;
		goto ErrorExitPhase2;
	}

	/* Fill in "Implementation dependant" section of mem info */
	psMemBlock->sDevVirtAddr = BM_HandleToDevVaddr(hBuffer);
	psMemBlock->hOSMemHandle = BM_HandleToOSMemHandle(hBuffer);

	/* Convert from BM_HANDLE to external IMG_HANDLE */
	psMemBlock->hBuffer = (IMG_HANDLE)hBuffer;

	/* patch up the CPU VAddr into the meminfo - use the address from the BM, not the one from the deviceclass
	   api, to ensure user mode mapping is possible
	 */
	psMemInfo->pvLinAddrKM = BM_HandleToCpuVaddr(hBuffer);

	/* Fill in the public fields of the MEM_INFO structure */
	psMemInfo->sDevVAddr = psMemBlock->sDevVirtAddr;
	psMemInfo->uAllocSize = ui32ByteSize;
	psMemInfo->psKernelSyncInfo = psDeviceClassBuffer->psKernelSyncInfo;

	PVR_ASSERT(psMemInfo->psKernelSyncInfo != IMG_NULL);
	if (psMemInfo->psKernelSyncInfo)
	{
		PVRSRVKernelSyncInfoIncRef(psMemInfo->psKernelSyncInfo, psMemInfo);
	}

	/* Clear the Backup buffer pointer as we do not have one at this point.
	   We only allocate this as we are going up/down
	 */
	psMemInfo->pvSysBackupBuffer = IMG_NULL;

	/* setup DCMapInfo */
	psDCMapInfo->psMemInfo = psMemInfo;
	psDCMapInfo->psDeviceClassBuffer = psDeviceClassBuffer;

#if defined(SUPPORT_MEMORY_TILING)
	psDCMapInfo->psDeviceNode = psDeviceNode;

	if(psDCMapInfo->ui32TilingStride > 0)
	{
		/* try to acquire a tiling range on this device */
		eError = psDeviceNode->pfnAllocMemTilingRange(psDeviceNode,
														psMemInfo,
														psDCMapInfo->ui32TilingStride,
														&psDCMapInfo->ui32RangeIndex);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceClassMemoryKM: AllocMemTilingRange failed"));
			goto ErrorExitPhase3;
		}
	}
#endif

	/* Register Resource */
	psMemInfo->sMemBlk.hResItem = ResManRegisterRes(psPerProc->hResManContext,
													RESMAN_TYPE_DEVICECLASSMEM_MAPPING,
													psDCMapInfo,
													0,
													&UnmapDeviceClassMemoryCallBack);

	(psDeviceClassBuffer->ui32MemMapRefCount)++;
	PVRSRVKernelMemInfoIncRef(psMemInfo);

	psMemInfo->memType = PVRSRV_MEMTYPE_DEVICECLASS;

	/* return the meminfo */
	*ppsMemInfo = psMemInfo;

#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
	/* If the 3PDD supplies a kernel virtual address, we can PDUMP it */
	if(psMemInfo->pvLinAddrKM)
	{
		/* FIXME:
		 *	Initialise the display surface here when it is mapped into Services.
		 *	Otherwise there is a risk that pdump toolchain will assign previously
		 *	used physical pages, leading to visual artefacts on the unrendered surface
		 *	(e.g. during LLS rendering).
		 *
		 *	A better method is to pdump the allocation from the DC driver, so the
		 *	BM_Wrap pdumps only the virtual memory which better represents the driver
		 *	behaviour.	
		 */
		PDUMPCOMMENT("Dump display surface");
		PDUMPMEM(IMG_NULL, psMemInfo, uOffset, psMemInfo->uAllocSize, PDUMP_FLAGS_CONTINUOUS, ((BM_BUF*)psMemInfo->sMemBlk.hBuffer)->pMapping);
	}
#endif
	return PVRSRV_OK;

#if defined(SUPPORT_MEMORY_TILING)
ErrorExitPhase3:
	if(psMemInfo)
	{
		if (psMemInfo->psKernelSyncInfo)
		{
			PVRSRVKernelSyncInfoDecRef(psMemInfo->psKernelSyncInfo, psMemInfo);
		}

		FreeDeviceMem(psMemInfo);
		/*
			FreeDeviceMem will free the meminfo so set
			it to NULL to avoid double free below
		*/
		psMemInfo = IMG_NULL;
	}
#endif

ErrorExitPhase2:
	if(psMemInfo)
	{
		OSFreeMem(PVRSRV_PAGEABLE_SELECT, sizeof(PVRSRV_KERNEL_MEM_INFO), psMemInfo, IMG_NULL);
	}

ErrorExitPhase1:
	if(psDCMapInfo)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_KERNEL_MEM_INFO), psDCMapInfo, IMG_NULL);
	}

	return eError;
}


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVChangeDeviceMemoryAttributesKM(IMG_HANDLE hKernelMemInfo, IMG_UINT32 ui32Attribs)
{
	PVRSRV_KERNEL_MEM_INFO		*psKMMemInfo;

	if (hKernelMemInfo == IMG_NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psKMMemInfo = (PVRSRV_KERNEL_MEM_INFO *)hKernelMemInfo;

	if (ui32Attribs & PVRSRV_CHANGEDEVMEM_ATTRIBS_CACHECOHERENT)
	{
		psKMMemInfo->ui32Flags |= PVRSRV_MEM_CACHE_CONSISTENT;
	}
	else
	{
		psKMMemInfo->ui32Flags &= ~PVRSRV_MEM_CACHE_CONSISTENT;
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR IMG_CALLCONV PVRSRVInitDeviceMem(IMG_VOID)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

#if defined(SUPPORT_ION)
	/*
		For Ion buffers we need to store which ones we know about so
		we don't give the same buffer a different sync
	*/
	g_psIonSyncHash = HASH_Create(ION_SYNC_HASH_SIZE);
	if (g_psIonSyncHash == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	}
#endif

	return eError;
}

IMG_VOID IMG_CALLCONV PVRSRVDeInitDeviceMem(IMG_VOID)
{
#if defined(SUPPORT_ION)
	HASH_Delete(g_psIonSyncHash);
#endif
}



/******************************************************************************
 End of file (devicemem.c)
******************************************************************************/

