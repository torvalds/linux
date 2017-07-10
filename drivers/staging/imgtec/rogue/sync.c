/*************************************************************************/ /*!
@File
@Title          Services synchronisation interface
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements client side code for services synchronisation
                interface
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

#include "img_types.h"
#include "client_sync_bridge.h"
#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
#include "client_synctracking_bridge.h"
#endif
#include "pvr_bridge.h"
#include "allocmem.h"
#include "osfunc.h"
#include "devicemem.h"
#include "devicemem_pdump.h"
#include "pvr_debug.h"
#include "dllist.h"
#include "sync.h"
#include "sync_internal.h"
#include "lock.h"
#include "log2.h"
/* FIXME */
#if defined(__KERNEL__)
#include "pvrsrv.h"
#endif


#define SYNC_BLOCK_LIST_CHUNCK_SIZE	10

/*
	This defines the maximum amount of synchronisation memory
	that can be allocated per SyncPrim context.
	In reality this number is meaningless as we would run out
	of synchronisation memory before we reach this limit, but
	we need to provide a size to the span RA.
*/
#define MAX_SYNC_MEM				(4 * 1024 * 1024)

typedef struct _SYNC_BLOCK_LIST_
{
	IMG_UINT32			ui32BlockCount;			/*!< Number of contexts in the list */
	IMG_UINT32			ui32BlockListSize;		/*!< Size of the array contexts */
	SYNC_PRIM_BLOCK		**papsSyncPrimBlock;	/*!< Array of syncprim blocks */
} SYNC_BLOCK_LIST;

typedef struct _SYNC_OP_COOKIE_
{
	IMG_UINT32				ui32SyncCount;
	IMG_UINT32				ui32ClientSyncCount;
	IMG_UINT32				ui32ServerSyncCount;
	IMG_BOOL				bHaveServerSync;
	IMG_HANDLE				hBridge;
	IMG_HANDLE				hServerCookie;

	SYNC_BLOCK_LIST			*psSyncBlockList;
	PVRSRV_CLIENT_SYNC_PRIM	**papsSyncPrim;
	/*
		Client sync(s) info.
		If this changes update the calculation of ui32ClientAllocSize
	*/
	IMG_UINT32				*paui32SyncBlockIndex;
	IMG_UINT32				*paui32Index;
	IMG_UINT32				*paui32Flags;
	IMG_UINT32				*paui32FenceValue;
	IMG_UINT32				*paui32UpdateValue;

	/*
		Server sync(s) info
		If this changes update the calculation of ui32ServerAllocSize
	*/
	IMG_HANDLE				*pahServerSync;
	IMG_UINT32              *paui32ServerFlags;
} SYNC_OP_COOKIE;

/* forward declaration */
static PVRSRV_ERROR
_SyncPrimSetValue(SYNC_PRIM *psSyncInt, IMG_UINT32 ui32Value);

/*
	Internal interfaces for management of SYNC_PRIM_CONTEXT
*/
static void
_SyncPrimContextUnref(SYNC_PRIM_CONTEXT *psContext)
{
	if (!OSAtomicRead(&psContext->hRefCount))
	{
		PVR_DPF((PVR_DBG_ERROR, "_SyncPrimContextUnref context already freed"));
	}
	else if (0 == OSAtomicDecrement(&psContext->hRefCount))
	{
		/* SyncPrimContextDestroy only when no longer referenced */
		RA_Delete(psContext->psSpanRA);
		RA_Delete(psContext->psSubAllocRA);
		OSFreeMem(psContext);
	}
}

static void
_SyncPrimContextRef(SYNC_PRIM_CONTEXT *psContext)
{
	if (!OSAtomicRead(&psContext->hRefCount))
	{
		PVR_DPF((PVR_DBG_ERROR, "_SyncPrimContextRef context use after free"));
	}
	else
	{
		OSAtomicIncrement(&psContext->hRefCount);
	}
}

/*
	Internal interfaces for management of synchronisation block memory
*/
static PVRSRV_ERROR
AllocSyncPrimitiveBlock(SYNC_PRIM_CONTEXT *psContext,
						SYNC_PRIM_BLOCK **ppsSyncBlock)
{
	SYNC_PRIM_BLOCK *psSyncBlk;
	IMG_HANDLE hSyncPMR;
	IMG_HANDLE hSyncImportHandle;
	IMG_DEVMEM_SIZE_T uiImportSize;
	PVRSRV_ERROR eError;

	psSyncBlk = OSAllocMem(sizeof(SYNC_PRIM_BLOCK));
	if (psSyncBlk == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc;
	}
	psSyncBlk->psContext = psContext;

	/* Allocate sync prim block */
	eError = BridgeAllocSyncPrimitiveBlock(psContext->hDevConnection,
	                                       &psSyncBlk->hServerSyncPrimBlock,
										   &psSyncBlk->ui32FirmwareAddr,
										   &psSyncBlk->ui32SyncBlockSize,
										   &hSyncPMR);
	if (eError != PVRSRV_OK)
	{
		goto fail_blockalloc;
	}

	/* Make it mappable by the client */
	eError = DevmemMakeLocalImportHandle(psContext->hDevConnection,
										hSyncPMR,
										&hSyncImportHandle);
	if (eError != PVRSRV_OK)
	{
		goto fail_export;
	}

	/* Get CPU mapping of the memory block */
	eError = DevmemLocalImport(psContext->hDevConnection,
	                           hSyncImportHandle,
	                           PVRSRV_MEMALLOCFLAG_CPU_READABLE,
	                           &psSyncBlk->hMemDesc,
	                           &uiImportSize,
	                           "SyncPrimitiveBlock");

	/*
		Regardless of success or failure we "undo" the export
	*/
	DevmemUnmakeLocalImportHandle(psContext->hDevConnection,
								 hSyncImportHandle);

	if (eError != PVRSRV_OK)
	{
		goto fail_import;
	}

	eError = DevmemAcquireCpuVirtAddr(psSyncBlk->hMemDesc,
									  (void **) &psSyncBlk->pui32LinAddr);
	if (eError != PVRSRV_OK)
	{
		goto fail_cpuvaddr;
	}

	*ppsSyncBlock = psSyncBlk;
	return PVRSRV_OK;

fail_cpuvaddr:
	DevmemFree(psSyncBlk->hMemDesc);
fail_import:
fail_export:
	BridgeFreeSyncPrimitiveBlock(psContext->hDevConnection,
								 psSyncBlk->hServerSyncPrimBlock);
fail_blockalloc:
	OSFreeMem(psSyncBlk);
fail_alloc:
	return eError;
}

static void
FreeSyncPrimitiveBlock(SYNC_PRIM_BLOCK *psSyncBlk)
{
	SYNC_PRIM_CONTEXT *psContext = psSyncBlk->psContext;

	DevmemReleaseCpuVirtAddr(psSyncBlk->hMemDesc);
	DevmemFree(psSyncBlk->hMemDesc);
	BridgeFreeSyncPrimitiveBlock(psContext->hDevConnection,
								 psSyncBlk->hServerSyncPrimBlock);
	OSFreeMem(psSyncBlk);
}

static PVRSRV_ERROR
SyncPrimBlockImport(RA_PERARENA_HANDLE hArena,
					RA_LENGTH_T uSize,
					RA_FLAGS_T uFlags,
					const IMG_CHAR *pszAnnotation,
					RA_BASE_T *puiBase,
					RA_LENGTH_T *puiActualSize,
					RA_PERISPAN_HANDLE *phImport)
{
	SYNC_PRIM_CONTEXT *psContext = hArena;
	SYNC_PRIM_BLOCK *psSyncBlock = NULL;
	RA_LENGTH_T uiSpanSize;
	PVRSRV_ERROR eError;
	PVR_UNREFERENCED_PARAMETER(uFlags);

	/* Check we've not be called with an unexpected size */
	if (!hArena || sizeof(IMG_UINT32) != uSize)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: invalid input params", __FUNCTION__));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto e0;
	}

	/*
		Ensure the synprim context doesn't go away while we have sync blocks
		attached to it
	*/
	_SyncPrimContextRef(psContext);

	/* Allocate the block of memory */
	eError = AllocSyncPrimitiveBlock(psContext, &psSyncBlock);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to allocate syncprim block (%d)", eError));
		goto fail_syncblockalloc;
	}

	/* Allocate a span for it */
	eError = RA_Alloc(psContext->psSpanRA,
					psSyncBlock->ui32SyncBlockSize,
					RA_NO_IMPORT_MULTIPLIER,
					0,
					psSyncBlock->ui32SyncBlockSize,
					pszAnnotation,
					&psSyncBlock->uiSpanBase,
					&uiSpanSize,
					NULL);
	if (eError != PVRSRV_OK)
	{
		goto fail_spanalloc;
	}

	/*
		There is no reason the span RA should return an allocation larger
		then we request
	*/
	PVR_ASSERT(uiSpanSize == psSyncBlock->ui32SyncBlockSize);

	*puiBase = psSyncBlock->uiSpanBase;
	*puiActualSize = psSyncBlock->ui32SyncBlockSize;
	*phImport = psSyncBlock;
	return PVRSRV_OK;

fail_spanalloc:
	FreeSyncPrimitiveBlock(psSyncBlock);
fail_syncblockalloc:
	_SyncPrimContextUnref(psContext);
e0:
	return eError;
}

static void
SyncPrimBlockUnimport(RA_PERARENA_HANDLE hArena,
					  RA_BASE_T uiBase,
					  RA_PERISPAN_HANDLE hImport)
{
	SYNC_PRIM_CONTEXT *psContext = hArena;
	SYNC_PRIM_BLOCK *psSyncBlock = hImport;

	if (!psContext || !psSyncBlock || uiBase != psSyncBlock->uiSpanBase)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: invalid input params", __FUNCTION__));
		return;
	}

	/* Free the span this import is using */
	RA_Free(psContext->psSpanRA, uiBase);

	/* Free the syncpim block */
	FreeSyncPrimitiveBlock(psSyncBlock);

	/*	Drop our reference to the syncprim context */
	_SyncPrimContextUnref(psContext);
}

static INLINE IMG_UINT32 SyncPrimGetOffset(SYNC_PRIM *psSyncInt)
{
	IMG_UINT64 ui64Temp;
	
	PVR_ASSERT(psSyncInt->eType == SYNC_PRIM_TYPE_LOCAL);

	/* FIXME: Subtracting a 64-bit address from another and then implicit
	 * cast to 32-bit number. Need to review all call sequences that use this
	 * function, added explicit casting for now.
	 */
	ui64Temp =  psSyncInt->u.sLocal.uiSpanAddr - psSyncInt->u.sLocal.psSyncBlock->uiSpanBase;
	PVR_ASSERT(ui64Temp<IMG_UINT32_MAX);
	return (IMG_UINT32)ui64Temp;
}

static void SyncPrimGetCPULinAddr(SYNC_PRIM *psSyncInt)
{
	SYNC_PRIM_BLOCK *psSyncBlock = psSyncInt->u.sLocal.psSyncBlock;

	psSyncInt->sCommon.pui32LinAddr = psSyncBlock->pui32LinAddr +
									  (SyncPrimGetOffset(psSyncInt)/sizeof(IMG_UINT32));
}

static void SyncPrimLocalFree(SYNC_PRIM *psSyncInt)
{
	SYNC_PRIM_BLOCK *psSyncBlock;
	SYNC_PRIM_CONTEXT *psContext;

	psSyncBlock = psSyncInt->u.sLocal.psSyncBlock;
	psContext = psSyncBlock->psContext;

	{
		PVRSRV_ERROR eError;
		IMG_HANDLE hConn =
				psSyncInt->u.sLocal.psSyncBlock->psContext->hDevConnection;

#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
		if(PVRSRVIsBridgeEnabled(hConn, PVRSRV_BRIDGE_SYNCTRACKING))
		{
			/* remove this sync record */
			eError = BridgeSyncRecordRemoveByHandle(hConn,
			                                        psSyncInt->u.sLocal.hRecord);
			if (PVRSRV_OK != eError)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: failed to remove SyncRecord", __FUNCTION__));
			}
		}
		else
#endif /* if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING) */
		{
			IMG_UINT32 ui32FWAddr = psSyncBlock->ui32FirmwareAddr +
					SyncPrimGetOffset(psSyncInt);

			eError = BridgeSyncFreeEvent(hConn, ui32FWAddr);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_WARNING, "BridgeSyncAllocEvent failed with error:"
				        " %d", eError));
			}
		}
	}
	/* reset the sync prim value as it is freed.
	 * this guarantees the client sync allocated to the client will
	 * have a value of zero and the client does not need to
	 * explicitly initialise the sync value to zero.
	 * the allocation of the backing memory for the sync prim block
	 * is done with ZERO_ON_ALLOC so the memory is initially all zero.
	 */
	(void) _SyncPrimSetValue(psSyncInt, LOCAL_SYNC_PRIM_RESET_VALUE);

	RA_Free(psContext->psSubAllocRA, psSyncInt->u.sLocal.uiSpanAddr);
	OSFreeMem(psSyncInt);
	_SyncPrimContextUnref(psContext);
}

static void SyncPrimServerFree(SYNC_PRIM *psSyncInt)
{
	PVRSRV_ERROR eError;

	eError = BridgeServerSyncFree(psSyncInt->u.sServer.hBridge,
								  psSyncInt->u.sServer.hServerSync);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncPrimServerFree failed"));
	}
	OSFreeMem(psSyncInt);
}

static void SyncPrimLocalUnref(SYNC_PRIM *psSyncInt)
{
	if (!OSAtomicRead(&psSyncInt->u.sLocal.hRefCount))
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncPrimLocalUnref sync already freed"));
	}
	else if (0 == OSAtomicDecrement(&psSyncInt->u.sLocal.hRefCount))
	{
		SyncPrimLocalFree(psSyncInt);
	}
}

static void SyncPrimLocalRef(SYNC_PRIM *psSyncInt)
{
	if (!OSAtomicRead(&psSyncInt->u.sLocal.hRefCount))
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncPrimLocalRef sync use after free"));
	}
	else
	{
		OSAtomicIncrement(&psSyncInt->u.sLocal.hRefCount);
	}
}

static IMG_UINT32 SyncPrimGetFirmwareAddrLocal(SYNC_PRIM *psSyncInt)
{
	SYNC_PRIM_BLOCK *psSyncBlock;

	psSyncBlock = psSyncInt->u.sLocal.psSyncBlock;
	return psSyncBlock->ui32FirmwareAddr + SyncPrimGetOffset(psSyncInt);	
}

static IMG_UINT32 SyncPrimGetFirmwareAddrServer(SYNC_PRIM *psSyncInt)
{
	return psSyncInt->u.sServer.ui32FirmwareAddr;
}

#if !defined(__KERNEL__)
static SYNC_BRIDGE_HANDLE _SyncPrimGetBridgeHandleLocal(SYNC_PRIM *psSyncInt)
{
	return psSyncInt->u.sLocal.psSyncBlock->psContext->hDevConnection;
}

static SYNC_BRIDGE_HANDLE _SyncPrimGetBridgeHandleServer(SYNC_PRIM *psSyncInt)
{
	return psSyncInt->u.sServer.hBridge;
}

static SYNC_BRIDGE_HANDLE _SyncPrimGetBridgeHandle(PVRSRV_CLIENT_SYNC_PRIM *psSync)
{
	SYNC_PRIM *psSyncInt;

	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);
	if (psSyncInt->eType == SYNC_PRIM_TYPE_LOCAL)
	{
		return _SyncPrimGetBridgeHandleLocal(psSyncInt);
	}
	else if (psSyncInt->eType == SYNC_PRIM_TYPE_SERVER)
	{
		return _SyncPrimGetBridgeHandleServer(psSyncInt);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "_SyncPrimGetBridgeHandle: Invalid sync type"));
		/*
			Either the client has given us a bad pointer or there is an
			error in this module
		*/
		return 0;
	}
}
#endif

/*
	Internal interfaces for management of syncprim block lists
*/
static SYNC_BLOCK_LIST *_SyncPrimBlockListCreate(void)
{
	SYNC_BLOCK_LIST *psBlockList;

	psBlockList = OSAllocMem(sizeof(SYNC_BLOCK_LIST));
	if (!psBlockList)
	{
		return NULL;
	}

	psBlockList->ui32BlockCount = 0;
	psBlockList->ui32BlockListSize = SYNC_BLOCK_LIST_CHUNCK_SIZE;

	psBlockList->papsSyncPrimBlock = OSAllocMem(sizeof(SYNC_PRIM_BLOCK *)
													* SYNC_BLOCK_LIST_CHUNCK_SIZE);
	if (!psBlockList->papsSyncPrimBlock)
	{
		OSFreeMem(psBlockList);
		return NULL;
	}

	OSCachedMemSet(psBlockList->papsSyncPrimBlock,
			 0,
			 sizeof(SYNC_PRIM_BLOCK *) * psBlockList->ui32BlockListSize);

	return psBlockList;
}

static PVRSRV_ERROR _SyncPrimBlockListAdd(SYNC_BLOCK_LIST *psBlockList,
											SYNC_PRIM_BLOCK *psSyncPrimBlock)
{
	IMG_UINT32 i;

	/* Check the context isn't already on the list */
	for (i=0;i<psBlockList->ui32BlockCount;i++)
	{
		if (psBlockList->papsSyncPrimBlock[i] == psSyncPrimBlock)
		{
			return PVRSRV_OK;
		}
	}

	/* Check we have space for a new item */
	if (psBlockList->ui32BlockCount == psBlockList->ui32BlockListSize)
	{
		SYNC_PRIM_BLOCK	**papsNewSyncPrimBlock;

		papsNewSyncPrimBlock = OSAllocMem(sizeof(SYNC_PRIM_BLOCK *) *
											(psBlockList->ui32BlockListSize +
											SYNC_BLOCK_LIST_CHUNCK_SIZE));
		if (!papsNewSyncPrimBlock)
		{
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}

		OSCachedMemCopy(papsNewSyncPrimBlock,
				  psBlockList->papsSyncPrimBlock,
				  sizeof(SYNC_PRIM_CONTEXT *) *
				  psBlockList->ui32BlockListSize);

		OSFreeMem(psBlockList->papsSyncPrimBlock);

		psBlockList->papsSyncPrimBlock = papsNewSyncPrimBlock;
		psBlockList->ui32BlockListSize += SYNC_BLOCK_LIST_CHUNCK_SIZE;
	}

	/* Add the context to the list */
	psBlockList->papsSyncPrimBlock[psBlockList->ui32BlockCount++] = psSyncPrimBlock;
	return PVRSRV_OK;
}

static PVRSRV_ERROR _SyncPrimBlockListBlockToIndex(SYNC_BLOCK_LIST *psBlockList,
												   SYNC_PRIM_BLOCK *psSyncPrimBlock,
												   IMG_UINT32 *pui32Index)
{
	IMG_UINT32 i;

	for (i=0;i<psBlockList->ui32BlockCount;i++)
	{
		if (psBlockList->papsSyncPrimBlock[i] == psSyncPrimBlock)
		{
			*pui32Index = i;
			return PVRSRV_OK;
		}
	}

	return PVRSRV_ERROR_INVALID_PARAMS;
}

static PVRSRV_ERROR _SyncPrimBlockListHandleArrayCreate(SYNC_BLOCK_LIST *psBlockList,
														IMG_UINT32 *pui32BlockHandleCount,
														IMG_HANDLE **ppahHandleList)
{
	IMG_HANDLE *pahHandleList;
	IMG_UINT32 i;

	pahHandleList = OSAllocMem(sizeof(IMG_HANDLE) *
							   psBlockList->ui32BlockCount);
	if (!pahHandleList)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	for (i=0;i<psBlockList->ui32BlockCount;i++)
	{
		pahHandleList[i] = psBlockList->papsSyncPrimBlock[i]->hServerSyncPrimBlock;
	}

	*ppahHandleList = pahHandleList;
	*pui32BlockHandleCount = psBlockList->ui32BlockCount;

	return PVRSRV_OK;
}

static void _SyncPrimBlockListHandleArrayDestroy(IMG_HANDLE *pahHandleList)
{
	OSFreeMem(pahHandleList);
}

static IMG_UINT32 _SyncPrimBlockListGetClientValue(SYNC_BLOCK_LIST *psBlockList,
												   IMG_UINT32 ui32BlockIndex,
												   IMG_UINT32 ui32Index)
{
	return psBlockList->papsSyncPrimBlock[ui32BlockIndex]->pui32LinAddr[ui32Index];
}

static void _SyncPrimBlockListDestroy(SYNC_BLOCK_LIST *psBlockList)
{
	OSFreeMem(psBlockList->papsSyncPrimBlock);
	OSFreeMem(psBlockList);
}


static INLINE IMG_UINT32 _Log2(IMG_UINT32 ui32Align)
{
	PVR_ASSERT(IsPower2(ui32Align));
	return ExactLog2(ui32Align);
}

/*
	External interfaces
*/

IMG_INTERNAL PVRSRV_ERROR
SyncPrimContextCreate(SHARED_DEV_CONNECTION hDevConnection,
                      PSYNC_PRIM_CONTEXT *phSyncPrimContext)
{
	SYNC_PRIM_CONTEXT *psContext;
	PVRSRV_ERROR eError;

	psContext = OSAllocMem(sizeof(SYNC_PRIM_CONTEXT));
	if (psContext == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc;
	}

	psContext->hDevConnection = hDevConnection;

	OSSNPrintf(psContext->azName, SYNC_PRIM_NAME_SIZE, "Sync Prim RA-%p", psContext);
	OSSNPrintf(psContext->azSpanName, SYNC_PRIM_NAME_SIZE, "Sync Prim span RA-%p", psContext);

	/*
		Create the RA for sub-allocations of the SynPrim's

		Note:
		The import size doesn't matter here as the server will pass
		back the blocksize when does the import which overrides
		what we specify here.
	*/

	psContext->psSubAllocRA = RA_Create(psContext->azName,
										/* Params for imports */
										_Log2(sizeof(IMG_UINT32)),
										RA_LOCKCLASS_2,
										SyncPrimBlockImport,
										SyncPrimBlockUnimport,
										psContext,
										IMG_FALSE);
	if (psContext->psSubAllocRA == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_suballoc;
	}

	/*
		Create the span-management RA

		The RA requires that we work with linear spans. For our use
		here we don't require this behaviour as we're always working
		within offsets of blocks (imports). However, we need to keep
		the RA happy so we create the "span" management RA which
		ensures that all are imports are added to the RA in a linear
		fashion
	*/
	psContext->psSpanRA = RA_Create(psContext->azSpanName,
									/* Params for imports */
									0,
									RA_LOCKCLASS_1,
									NULL,
									NULL,
									NULL,
									IMG_FALSE);
	if (psContext->psSpanRA == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_span;
	}

	if (!RA_Add(psContext->psSpanRA, 0, MAX_SYNC_MEM, 0, NULL))
	{
		RA_Delete(psContext->psSpanRA);
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_span;
	}

	OSAtomicWrite(&psContext->hRefCount, 1);

	*phSyncPrimContext = psContext;
	return PVRSRV_OK;
fail_span:
	RA_Delete(psContext->psSubAllocRA);
fail_suballoc:
	OSFreeMem(psContext);
fail_alloc:
	return eError;
}

IMG_INTERNAL void SyncPrimContextDestroy(PSYNC_PRIM_CONTEXT hSyncPrimContext)
{
	SYNC_PRIM_CONTEXT *psContext = hSyncPrimContext;
	if (1 != OSAtomicRead(&psContext->hRefCount))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s attempted with active references, may be the result of a race", __FUNCTION__));
	}
#if defined(PVRSRV_FORCE_UNLOAD_IF_BAD_STATE)
#if defined(__KERNEL__)
	if (PVRSRVGetPVRSRVData()->eServicesState != PVRSRV_SERVICES_STATE_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Forcing context destruction due to bad driver state.", __FUNCTION__));
		OSAtomicWrite(&psContext->hRefCount, 1);
	}
#endif
#endif
	_SyncPrimContextUnref(psContext);
}

static PVRSRV_ERROR _SyncPrimAlloc(PSYNC_PRIM_CONTEXT hSyncPrimContext,
                                   PVRSRV_CLIENT_SYNC_PRIM **ppsSync,
                                   const IMG_CHAR *pszClassName,
                                   IMG_BOOL bServerSync)
{
	SYNC_PRIM_CONTEXT *psContext = hSyncPrimContext;
	SYNC_PRIM_BLOCK *psSyncBlock;
	SYNC_PRIM *psNewSync;
	PVRSRV_ERROR eError;
	RA_BASE_T uiSpanAddr;

	if (!hSyncPrimContext)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: invalid context", __func__));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psNewSync = OSAllocMem(sizeof(SYNC_PRIM));
	if (psNewSync == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc;
	}

	eError = RA_Alloc(psContext->psSubAllocRA,
	                  sizeof(IMG_UINT32),
	                  RA_NO_IMPORT_MULTIPLIER,
	                  0,
	                  sizeof(IMG_UINT32),
	                  "Sync_Prim",
	                  &uiSpanAddr,
	                  NULL,
	                  (RA_PERISPAN_HANDLE *) &psSyncBlock);
	if (PVRSRV_OK != eError)
	{
		goto fail_raalloc;
	}
	psNewSync->eType = SYNC_PRIM_TYPE_LOCAL;
	OSAtomicWrite(&psNewSync->u.sLocal.hRefCount, 1);
	psNewSync->u.sLocal.uiSpanAddr = uiSpanAddr;
	psNewSync->u.sLocal.psSyncBlock = psSyncBlock;
	SyncPrimGetCPULinAddr(psNewSync);
	*ppsSync = &psNewSync->sCommon;
	_SyncPrimContextRef(psContext);

#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
	if(PVRSRVIsBridgeEnabled(psSyncBlock->psContext->hDevConnection, PVRSRV_BRIDGE_SYNCTRACKING))
	{
		IMG_CHAR szClassName[SYNC_MAX_CLASS_NAME_LEN];
		if(pszClassName)
		{
			/* Copy the class name annotation into a fixed-size array */
			OSStringNCopy(szClassName, pszClassName, SYNC_MAX_CLASS_NAME_LEN - 1);
			szClassName[SYNC_MAX_CLASS_NAME_LEN - 1] = 0;
		}
		else
		{
			/* No class name annotation */
			szClassName[0] = 0;
		}
		/* record this sync */
		eError = BridgeSyncRecordAdd(
					psSyncBlock->psContext->hDevConnection,
					&psNewSync->u.sLocal.hRecord,
					psSyncBlock->hServerSyncPrimBlock,
					psSyncBlock->ui32FirmwareAddr,
					SyncPrimGetOffset(psNewSync),
					bServerSync,
					OSStringNLength(szClassName, SYNC_MAX_CLASS_NAME_LEN),
					szClassName);
		if (PVRSRV_OK != eError)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: failed to add SyncRecord", __FUNCTION__));
		}
	}
	else
#endif /* if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING) */
	{
		eError = BridgeSyncAllocEvent(hSyncPrimContext->hDevConnection,
		                              bServerSync,
		                              psSyncBlock->ui32FirmwareAddr + SyncPrimGetOffset(psNewSync),
		                              OSStringNLength(pszClassName, SYNC_MAX_CLASS_NAME_LEN),
		                              pszClassName);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_WARNING, "BridgeSyncAllocEvent failed with error: %d",
			        eError));
		}
	}

	return PVRSRV_OK;

fail_raalloc:
	OSFreeMem(psNewSync);
fail_alloc:
	return eError;
}

#if defined(__KERNEL__)
IMG_INTERNAL PVRSRV_ERROR SyncPrimAllocForServerSync(PSYNC_PRIM_CONTEXT hSyncPrimContext,
										PVRSRV_CLIENT_SYNC_PRIM **ppsSync,
										const IMG_CHAR *pszClassName)
{
	return _SyncPrimAlloc(hSyncPrimContext,
					  ppsSync,
					  pszClassName,
					  IMG_TRUE);
}
#endif

IMG_INTERNAL PVRSRV_ERROR SyncPrimAlloc(PSYNC_PRIM_CONTEXT hSyncPrimContext,
										PVRSRV_CLIENT_SYNC_PRIM **ppsSync,
										const IMG_CHAR *pszClassName)
{
	return _SyncPrimAlloc(hSyncPrimContext,
	                      ppsSync,
	                      pszClassName,
	                      IMG_FALSE);
}

static PVRSRV_ERROR
_SyncPrimSetValue(SYNC_PRIM *psSyncInt, IMG_UINT32 ui32Value)
{
	PVRSRV_ERROR eError;

	if (psSyncInt->eType == SYNC_PRIM_TYPE_LOCAL)
	{
		SYNC_PRIM_BLOCK *psSyncBlock;
		SYNC_PRIM_CONTEXT *psContext;

		psSyncBlock = psSyncInt->u.sLocal.psSyncBlock;
		psContext = psSyncBlock->psContext;

		eError = BridgeSyncPrimSet(psContext->hDevConnection,
									psSyncBlock->hServerSyncPrimBlock,
									SyncPrimGetOffset(psSyncInt)/sizeof(IMG_UINT32),
									ui32Value);
	}
	else
	{
		eError = BridgeServerSyncPrimSet(psSyncInt->u.sServer.hBridge,
									psSyncInt->u.sServer.hServerSync,
									ui32Value);
	}
	/* These functions don't actually fail */
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR SyncPrimFree(PVRSRV_CLIENT_SYNC_PRIM *psSync)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	SYNC_PRIM *psSyncInt;

	if (!psSync)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: null sync pointer", __FUNCTION__));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto err_out;
	}

	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);
	if (psSyncInt->eType == SYNC_PRIM_TYPE_LOCAL)
	{
		SyncPrimLocalUnref(psSyncInt);
	}
	else if (psSyncInt->eType == SYNC_PRIM_TYPE_SERVER)
	{
		SyncPrimServerFree(psSyncInt);
	}
	else
	{
		/*
			Either the client has given us a bad pointer or there is an
			error in this module
		*/
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid sync type", __FUNCTION__));
		eError = PVRSRV_ERROR_INVALID_SYNC_PRIM;
		goto err_out;
	}

err_out:
	return eError;
}

#if defined(NO_HARDWARE)
IMG_INTERNAL PVRSRV_ERROR
SyncPrimNoHwUpdate(PVRSRV_CLIENT_SYNC_PRIM *psSync, IMG_UINT32 ui32Value)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	SYNC_PRIM *psSyncInt;

	if (!psSync)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: null sync pointer", __FUNCTION__));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto err_out;
	}
	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);

	/* There is no check for the psSyncInt to be LOCAL as this call
	   substitutes the Firmware updating a sync and that sync could
	   be a server one */

	eError =  _SyncPrimSetValue(psSyncInt, ui32Value);

err_out:
	return eError;
}
#endif

IMG_INTERNAL PVRSRV_ERROR
SyncPrimSet(PVRSRV_CLIENT_SYNC_PRIM *psSync, IMG_UINT32 ui32Value)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	SYNC_PRIM *psSyncInt;

	if (!psSync)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: null sync pointer", __FUNCTION__));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto err_out;
	}

	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);
	if (psSyncInt->eType != SYNC_PRIM_TYPE_LOCAL)
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncPrimSet: Invalid sync type"));
		eError = PVRSRV_ERROR_INVALID_SYNC_PRIM;
		goto err_out;
	}

	eError = _SyncPrimSetValue(psSyncInt, ui32Value);

#if defined(PDUMP)
	SyncPrimPDump(psSync);
#endif
err_out:
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR SyncPrimLocalGetHandleAndOffset(PVRSRV_CLIENT_SYNC_PRIM *psSync,
							IMG_HANDLE *phBlock,
							IMG_UINT32 *pui32Offset)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	SYNC_PRIM *psSyncInt;

	if(!psSync || !phBlock || !pui32Offset)
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncPrimGetHandleAndOffset: invalid input pointer"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto err_out;
	}

	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);

	if (psSyncInt->eType == SYNC_PRIM_TYPE_LOCAL)
	{
		*phBlock = psSyncInt->u.sLocal.psSyncBlock->hServerSyncPrimBlock;
		*pui32Offset = psSyncInt->u.sLocal.uiSpanAddr - psSyncInt->u.sLocal.psSyncBlock->uiSpanBase;
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: psSync not a Local sync prim (%d)",
			__FUNCTION__, psSyncInt->eType));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto err_out;
	}

err_out:
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR
SyncPrimGetFirmwareAddr(PVRSRV_CLIENT_SYNC_PRIM *psSync, IMG_UINT32 *pui32FwAddr)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	SYNC_PRIM *psSyncInt;

	*pui32FwAddr = 0;
	if (!psSync)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: invalid input pointer", __FUNCTION__));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto err_out;
	}

	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);
	if (psSyncInt->eType == SYNC_PRIM_TYPE_LOCAL)
	{
		*pui32FwAddr = SyncPrimGetFirmwareAddrLocal(psSyncInt);
	}
	else if (psSyncInt->eType == SYNC_PRIM_TYPE_SERVER)
	{
		*pui32FwAddr = SyncPrimGetFirmwareAddrServer(psSyncInt);
	}
	else
	{
		/* Either the client has given us a bad pointer or there is an
		 * error in this module
		 */
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid sync type", __FUNCTION__));
		eError = PVRSRV_ERROR_INVALID_SYNC_PRIM;
		goto err_out;
	}

err_out:
	return eError;
}

#if !defined(__KERNEL__)
IMG_INTERNAL PVRSRV_ERROR SyncPrimDumpSyncs(IMG_UINT32 ui32SyncCount, PVRSRV_CLIENT_SYNC_PRIM **papsSync, const IMG_CHAR *pcszExtraInfo)
{
#if defined(PVRSRV_NEED_PVR_DPF)
	SYNC_PRIM *psSyncInt;
	PVRSRV_CLIENT_SYNC_PRIM **papsServerSync;
	IMG_UINT32 ui32ServerSyncs = 0;
	IMG_UINT32 *pui32UID = NULL;
	IMG_UINT32 *pui32FWAddr = NULL;
	IMG_UINT32 *pui32CurrentOp = NULL;
	IMG_UINT32 *pui32NextOp = NULL;
	IMG_UINT32 i;
	PVRSRV_ERROR eError = PVRSRV_OK;

	papsServerSync = OSAllocMem(ui32SyncCount * sizeof(PVRSRV_CLIENT_SYNC_PRIM *));
	if (!papsServerSync)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	for (i = 0; i < ui32SyncCount; i++)
	{
		psSyncInt = IMG_CONTAINER_OF(papsSync[i], SYNC_PRIM, sCommon);
		if (psSyncInt->eType == SYNC_PRIM_TYPE_LOCAL)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: sync=local  fw=0x%x curr=0x%04x",
					 pcszExtraInfo,
					 SyncPrimGetFirmwareAddrLocal(psSyncInt),
					 *psSyncInt->sCommon.pui32LinAddr));
		}
		else if (psSyncInt->eType == SYNC_PRIM_TYPE_SERVER)
		{
			papsServerSync[ui32ServerSyncs++] = papsSync[i];
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "SyncPrimDumpSyncs: Invalid sync type"));
			/*
			   Either the client has given us a bad pointer or there is an
			   error in this module
			   */
			eError = PVRSRV_ERROR_INVALID_SYNC_PRIM;
			goto err_free;
		}
	}

	if (ui32ServerSyncs > 0)
	{
		pui32UID = OSAllocMem(ui32ServerSyncs * sizeof(IMG_UINT32));
		if (!pui32UID)
		{
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto err_free;
		}
		pui32FWAddr = OSAllocMem(ui32ServerSyncs * sizeof(IMG_UINT32));
		if (!pui32FWAddr)
		{
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto err_free;
		}
		pui32CurrentOp = OSAllocMem(ui32ServerSyncs * sizeof(IMG_UINT32));
		if (!pui32CurrentOp)
		{
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto err_free;
		}
		pui32NextOp = OSAllocMem(ui32ServerSyncs * sizeof(IMG_UINT32));
		if (!pui32NextOp)
		{
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto err_free;
		}
		eError = SyncPrimServerGetStatus(ui32ServerSyncs, papsServerSync,
										 pui32UID,
										 pui32FWAddr,
										 pui32CurrentOp,
										 pui32NextOp);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "SyncPrimDumpSyncs: Error querying server sync status (%d)",
					 eError));
			goto err_free;
		}
		for (i = 0; i < ui32ServerSyncs; i++)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: sync=server fw=0x%x curr=0x%04x next=0x%04x id=%u%s",
					 pcszExtraInfo,
					 pui32FWAddr[i],
					 pui32CurrentOp[i],
					 pui32NextOp[i],
					 pui32UID[i],
					 (pui32NextOp[i] - pui32CurrentOp[i] == 1) ? " *" : 
					 (pui32NextOp[i] - pui32CurrentOp[i] >  1) ? " **" : 
					 ""));
		}
	}

err_free:
	OSFreeMem(papsServerSync);
	if (pui32UID)
	{
		OSFreeMem(pui32UID);
	}
	if (pui32FWAddr)
	{
		OSFreeMem(pui32FWAddr);
	}
	if (pui32CurrentOp)
	{
		OSFreeMem(pui32CurrentOp);
	}
	if (pui32NextOp)
	{
		OSFreeMem(pui32NextOp);
	}
	return eError;
#else
	PVR_UNREFERENCED_PARAMETER(ui32SyncCount);
	PVR_UNREFERENCED_PARAMETER(papsSync);
	PVR_UNREFERENCED_PARAMETER(pcszExtraInfo);
	return PVRSRV_OK;
#endif
}
#endif

IMG_INTERNAL
PVRSRV_ERROR SyncPrimOpCreate(IMG_UINT32 ui32SyncCount,
			  PVRSRV_CLIENT_SYNC_PRIM **papsSyncPrim,
			  PSYNC_OP_COOKIE *ppsCookie)
{
	SYNC_OP_COOKIE *psNewCookie;
	SYNC_BLOCK_LIST *psSyncBlockList;
	IMG_UINT32 ui32ServerSyncCount = 0;
	IMG_UINT32 ui32ClientSyncCount = 0;
	IMG_UINT32 ui32ServerAllocSize;
	IMG_UINT32 ui32ClientAllocSize;
	IMG_UINT32 ui32TotalAllocSize;
	IMG_UINT32 ui32ServerIndex = 0;
	IMG_UINT32 ui32ClientIndex = 0;
	IMG_UINT32 i;
	IMG_UINT32 ui32SyncBlockCount;
	IMG_HANDLE hBridge;
	IMG_HANDLE *pahHandleList;
	IMG_CHAR *pcPtr;
	PVRSRV_ERROR eError;
	IMG_BOOL bServerSync;

	psSyncBlockList = _SyncPrimBlockListCreate();

	if (!psSyncBlockList)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}

	for (i=0;i<ui32SyncCount;i++)
	{
		eError = SyncPrimIsServerSync(papsSyncPrim[i], &bServerSync);
		if (PVRSRV_OK != eError) goto e1;
		if (bServerSync)
		{
			ui32ServerSyncCount++;
		}
		else
		{
			SYNC_PRIM *psSync = (SYNC_PRIM *) papsSyncPrim[i];

			ui32ClientSyncCount++;
			eError = _SyncPrimBlockListAdd(psSyncBlockList, psSync->u.sLocal.psSyncBlock);
			if (eError != PVRSRV_OK)
			{
				goto e1;
			}
		}
	}

	ui32ServerAllocSize = ui32ServerSyncCount * (sizeof(IMG_HANDLE) + sizeof(IMG_UINT32));
	ui32ClientAllocSize = ui32ClientSyncCount * (5 * sizeof(IMG_UINT32));
	ui32TotalAllocSize = sizeof(SYNC_OP_COOKIE) +
							 (sizeof(PVRSRV_CLIENT_SYNC_PRIM *) * ui32SyncCount) +
							 ui32ServerAllocSize + 
							 ui32ClientAllocSize;

	psNewCookie = OSAllocMem(ui32TotalAllocSize);
	pcPtr = (IMG_CHAR *) psNewCookie;

	if (!psNewCookie)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e1;
	}

	/* Setup the pointers */
	pcPtr += sizeof(SYNC_OP_COOKIE);
	psNewCookie->papsSyncPrim = (PVRSRV_CLIENT_SYNC_PRIM **) pcPtr;

	pcPtr += sizeof(PVRSRV_CLIENT_SYNC_PRIM *) * ui32SyncCount;
	psNewCookie->paui32SyncBlockIndex = (IMG_UINT32 *) pcPtr;

	pcPtr += sizeof(IMG_UINT32) * ui32ClientSyncCount;
	psNewCookie->paui32Index = (IMG_UINT32 *) pcPtr;

	pcPtr += sizeof(IMG_UINT32) * ui32ClientSyncCount;
	psNewCookie->paui32Flags = (IMG_UINT32 *) pcPtr;

	pcPtr += sizeof(IMG_UINT32) * ui32ClientSyncCount;
	psNewCookie->paui32FenceValue = (IMG_UINT32 *) pcPtr;

	pcPtr += sizeof(IMG_UINT32) * ui32ClientSyncCount;
	psNewCookie->paui32UpdateValue = (IMG_UINT32 *) pcPtr;

	pcPtr += sizeof(IMG_UINT32) * ui32ClientSyncCount;
	psNewCookie->pahServerSync =(IMG_HANDLE *) pcPtr;
	pcPtr += sizeof(IMG_HANDLE) * ui32ServerSyncCount;

	psNewCookie->paui32ServerFlags =(IMG_UINT32 *) pcPtr;
	pcPtr += sizeof(IMG_UINT32) * ui32ServerSyncCount;

	/* Check the pointer setup went ok */
	if (!(pcPtr == (((IMG_CHAR *) psNewCookie) + ui32TotalAllocSize)))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: cookie setup failed", __FUNCTION__));
		eError = PVRSRV_ERROR_INTERNAL_ERROR;
		goto e2;
	}

	psNewCookie->ui32SyncCount = ui32SyncCount;
	psNewCookie->ui32ServerSyncCount = ui32ServerSyncCount;
	psNewCookie->ui32ClientSyncCount = ui32ClientSyncCount;
	psNewCookie->psSyncBlockList = psSyncBlockList;

	/*
		Get the bridge handle from the 1st sync.

		Note: We assume the all syncs have been created with the same
			  services connection.
	*/
	eError = SyncPrimIsServerSync(papsSyncPrim[0], &bServerSync);
	if (PVRSRV_OK != eError) goto e2;
	if (bServerSync)
	{
		SYNC_PRIM *psSync = (SYNC_PRIM *) papsSyncPrim[0];

		hBridge = psSync->u.sServer.hBridge;
	}
	else
	{
		SYNC_PRIM *psSync = (SYNC_PRIM *) papsSyncPrim[0];

		hBridge = psSync->u.sLocal.psSyncBlock->psContext->hDevConnection;
	}

	psNewCookie->hBridge = hBridge;

	if (ui32ServerSyncCount)
	{
		psNewCookie->bHaveServerSync = IMG_TRUE;
	}
	else
	{
		psNewCookie->bHaveServerSync = IMG_FALSE;
	}

	/* Fill in the server and client sync data */
	for (i=0;i<ui32SyncCount;i++)
	{
		SYNC_PRIM *psSync = (SYNC_PRIM *) papsSyncPrim[i];

		eError = SyncPrimIsServerSync(papsSyncPrim[i], &bServerSync);
		if (PVRSRV_OK != eError) goto e2;
		if (bServerSync)
		{
			psNewCookie->pahServerSync[ui32ServerIndex] = psSync->u.sServer.hServerSync;

			ui32ServerIndex++;
		}
		else
		{
			/* Location of sync */
			eError = _SyncPrimBlockListBlockToIndex(psSyncBlockList,
													psSync->u.sLocal.psSyncBlock,
													&psNewCookie->paui32SyncBlockIndex[ui32ClientIndex]);
			if (eError != PVRSRV_OK)
			{
				goto e2;
			}

			/* Workout the index to sync */
			psNewCookie->paui32Index[ui32ClientIndex] =
					SyncPrimGetOffset(psSync)/sizeof(IMG_UINT32);

			ui32ClientIndex++;
		}

		psNewCookie->papsSyncPrim[i] = papsSyncPrim[i];
	}

	eError = _SyncPrimBlockListHandleArrayCreate(psSyncBlockList,
												 &ui32SyncBlockCount,
												 &pahHandleList);
	if (eError !=PVRSRV_OK)
	{
		goto e2;
	}

	/*
		Create the server side cookie. Here we pass in all the unchanging
		data so we only need to pass in the minimum at takeop time
	*/
	eError = BridgeSyncPrimOpCreate(hBridge,
									ui32SyncBlockCount,
									pahHandleList,
									psNewCookie->ui32ClientSyncCount,
									psNewCookie->paui32SyncBlockIndex,
									psNewCookie->paui32Index,
									psNewCookie->ui32ServerSyncCount,
									psNewCookie->pahServerSync,
									&psNewCookie->hServerCookie);

	/* Free the handle list regardless of error */
	_SyncPrimBlockListHandleArrayDestroy(pahHandleList);

	if (eError != PVRSRV_OK)
	{
		goto e2;
	}

	/* Increase the reference count on all referenced local sync prims
	 * so that they cannot be freed until this Op is finished with
	 */
	for (i=0;i<ui32SyncCount;i++)
	{
		SYNC_PRIM *psSyncInt;
		psSyncInt = IMG_CONTAINER_OF(papsSyncPrim[i], SYNC_PRIM, sCommon);
		if (SYNC_PRIM_TYPE_LOCAL == psSyncInt->eType)
		{
			SyncPrimLocalRef(psSyncInt);
		}
	}

	*ppsCookie = psNewCookie;
	return PVRSRV_OK;

e2:
	OSFreeMem(psNewCookie);
e1:
	_SyncPrimBlockListDestroy(psSyncBlockList);
e0:
	return eError;
}

IMG_INTERNAL
PVRSRV_ERROR SyncPrimOpTake(PSYNC_OP_COOKIE psCookie,
							IMG_UINT32 ui32SyncCount,
							PVRSRV_CLIENT_SYNC_PRIM_OP *pasSyncOp)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 ui32ServerIndex = 0;
	IMG_UINT32 ui32ClientIndex = 0;
	IMG_UINT32 i;
	IMG_BOOL bServerSync;

	/* Copy client sync operations */
	for (i=0;i<ui32SyncCount;i++)
	{
		/*
			Sanity check the client passes in the same syncs as the
			ones we got at create time
		*/
		if (psCookie->papsSyncPrim[i] != pasSyncOp[i].psSync)
		{
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			goto e0;
		}

		eError = SyncPrimIsServerSync(pasSyncOp[i].psSync, &bServerSync);
		if (PVRSRV_OK != eError) goto e0;
		if (bServerSync)
		{
			psCookie->paui32ServerFlags[ui32ServerIndex] =
					pasSyncOp[i].ui32Flags;

			ui32ServerIndex++;
		}
		else
		{
			/* Client operation information */
			psCookie->paui32Flags[ui32ClientIndex] =
					pasSyncOp[i].ui32Flags;
			psCookie->paui32FenceValue[ui32ClientIndex] =
					pasSyncOp[i].ui32FenceValue;
			psCookie->paui32UpdateValue[ui32ClientIndex] =
					pasSyncOp[i].ui32UpdateValue;

			ui32ClientIndex++;
		}
	}

	eError = BridgeSyncPrimOpTake(psCookie->hBridge,
								  psCookie->hServerCookie,
								  psCookie->ui32ClientSyncCount,
								  psCookie->paui32Flags,
								  psCookie->paui32FenceValue,
								  psCookie->paui32UpdateValue,
								  psCookie->ui32ServerSyncCount,
								  psCookie->paui32ServerFlags);

e0:
	return eError;
}

IMG_INTERNAL
PVRSRV_ERROR SyncPrimOpReady(PSYNC_OP_COOKIE psCookie,
							 IMG_BOOL *pbReady)
{
	PVRSRV_ERROR eError;
	if (!psCookie)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: invalid input pointer", __FUNCTION__));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto e0;
	}

	/*
		If we have a server sync we have no choice
		but to do the check in the server
	*/
	if (psCookie->bHaveServerSync)
	{
		eError = BridgeSyncPrimOpReady(psCookie->hBridge,
									   psCookie->hServerCookie,
									   pbReady);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: Failed to do sync check in server (Error = %d)",
					 __FUNCTION__, eError));
			goto e0;
		}
	}
	else
	{
		IMG_UINT32 i;
		IMG_UINT32 ui32SnapShot;
		IMG_BOOL bReady = IMG_TRUE;

		for (i=0;i<psCookie->ui32ClientSyncCount;i++)
		{
			if ((psCookie->paui32Flags[i] & PVRSRV_CLIENT_SYNC_PRIM_OP_CHECK) == 0)
			{
				continue;
			}

			ui32SnapShot = _SyncPrimBlockListGetClientValue(psCookie->psSyncBlockList,
															psCookie->paui32SyncBlockIndex[i],
															psCookie->paui32Index[i]);
			if (ui32SnapShot != psCookie->paui32FenceValue[i])
			{
				bReady = IMG_FALSE;
				break;
			}
		}

		*pbReady = bReady;
	}

	return PVRSRV_OK;
e0:
	return eError;
}

IMG_INTERNAL
PVRSRV_ERROR SyncPrimOpComplete(PSYNC_OP_COOKIE psCookie)
{
	PVRSRV_ERROR eError;

	eError = BridgeSyncPrimOpComplete(psCookie->hBridge,
									  psCookie->hServerCookie);

	return eError;
}

IMG_INTERNAL
PVRSRV_ERROR SyncPrimOpDestroy(PSYNC_OP_COOKIE psCookie)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 i;

	eError = BridgeSyncPrimOpDestroy(psCookie->hBridge, psCookie->hServerCookie);
	if (PVRSRV_OK != eError)
	{
		PVR_DPF((PVR_DBG_ERROR,
			"%s: Failed to destroy SyncPrimOp (Error = %d)",
			 __FUNCTION__, eError));
		goto err_out;
	}

	/* Decrease the reference count on all referenced local sync prims
	 * so that they can be freed now this Op is finished with
	 */
	for (i=0;i<psCookie->ui32SyncCount;i++)
	{
		SYNC_PRIM *psSyncInt;
		psSyncInt = IMG_CONTAINER_OF(psCookie->papsSyncPrim[i], SYNC_PRIM, sCommon);
		if (SYNC_PRIM_TYPE_LOCAL == psSyncInt->eType)
		{
			SyncPrimLocalUnref(psSyncInt);
		}
	}

	_SyncPrimBlockListDestroy(psCookie->psSyncBlockList);
	OSFreeMem(psCookie);

err_out:
	return eError;
}

IMG_INTERNAL
PVRSRV_ERROR SyncPrimOpResolve(PSYNC_OP_COOKIE psCookie,
							   IMG_UINT32 *pui32SyncCount,
							   PVRSRV_CLIENT_SYNC_PRIM_OP **ppsSyncOp)
{
	IMG_UINT32 ui32ServerIndex = 0;
	IMG_UINT32 ui32ClientIndex = 0;
	PVRSRV_CLIENT_SYNC_PRIM_OP *psSyncOps;
	IMG_UINT32 i;
	IMG_BOOL bServerSync;
	PVRSRV_ERROR eError = PVRSRV_OK;

	psSyncOps = OSAllocMem(sizeof(PVRSRV_CLIENT_SYNC_PRIM_OP) * 
						   psCookie->ui32SyncCount);
	if (!psSyncOps)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}

	for (i=0; i<psCookie->ui32SyncCount; i++)
	{
		psSyncOps[i].psSync = psCookie->papsSyncPrim[i];
		eError = SyncPrimIsServerSync(psCookie->papsSyncPrim[i], &bServerSync);
		if (PVRSRV_OK != eError) goto e1;
		if (bServerSync)
		{
			psSyncOps[i].ui32FenceValue = 0;
			psSyncOps[i].ui32UpdateValue = 0;
			psSyncOps[i].ui32Flags = psCookie->paui32ServerFlags[ui32ServerIndex];
			ui32ServerIndex++;
		}
		else
		{
			psSyncOps[i].ui32FenceValue = psCookie->paui32FenceValue[ui32ClientIndex]; 
			psSyncOps[i].ui32UpdateValue = psCookie->paui32UpdateValue[ui32ClientIndex]; 
			psSyncOps[i].ui32Flags = psCookie->paui32Flags[ui32ClientIndex];
			ui32ClientIndex++;
		}
	}

	*ppsSyncOp = psSyncOps;
	*pui32SyncCount = psCookie->ui32SyncCount;

e1:
	OSFreeMem(psSyncOps);
e0:
	return eError;
}

#if !defined(__KERNEL__)
IMG_INTERNAL
PVRSRV_ERROR SyncPrimServerAlloc(SYNC_BRIDGE_HANDLE hBridge,
								 PVRSRV_CLIENT_SYNC_PRIM **ppsSync,
								 const IMG_CHAR *pszClassName
								 PVR_DBG_FILELINE_PARAM)
{
	IMG_CHAR szClassName[SYNC_MAX_CLASS_NAME_LEN];
	SYNC_PRIM *psNewSync;
	PVRSRV_ERROR eError;

#if !defined(PVR_SYNC_PRIM_ALLOC_TRACE)
	PVR_DBG_FILELINE_UNREF();
#endif
	psNewSync = OSAllocMem(sizeof(SYNC_PRIM));
	if (psNewSync == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}
	OSCachedMemSet(psNewSync, 0, sizeof(SYNC_PRIM));

	if(pszClassName)
	{
		/* Copy the class name annotation into a fixed-size array */
		OSStringNCopy(szClassName, pszClassName, SYNC_MAX_CLASS_NAME_LEN - 1);
		szClassName[SYNC_MAX_CLASS_NAME_LEN - 1] = 0;
	}
	else
	{
		/* No class name annotation */
		szClassName[0] = 0;
	}

	eError = BridgeServerSyncAlloc(hBridge,
								   &psNewSync->u.sServer.hServerSync,
								   &psNewSync->u.sServer.ui32FirmwareAddr,
								   OSStringNLength(szClassName, SYNC_MAX_CLASS_NAME_LEN),
								   szClassName);

	if (eError != PVRSRV_OK)
	{
		goto e1;
	}

#if defined(PVR_SYNC_PRIM_ALLOC_TRACE)
	PVR_DPF((PVR_DBG_WARNING, "Allocated sync=server fw=0x%x [%p]" PVR_DBG_FILELINE_FMT,
			 psNewSync->u.sServer.ui32FirmwareAddr, &psNewSync->sCommon PVR_DBG_FILELINE_ARG));
#endif

	psNewSync->eType = SYNC_PRIM_TYPE_SERVER;
	psNewSync->u.sServer.hBridge = hBridge;
	*ppsSync = &psNewSync->sCommon;

	return PVRSRV_OK;
e1:
	OSFreeMem(psNewSync);
e0:
	return eError;
}

IMG_INTERNAL
PVRSRV_ERROR SyncPrimServerGetStatus(IMG_UINT32 ui32SyncCount,
									 PVRSRV_CLIENT_SYNC_PRIM **papsSync,
									 IMG_UINT32 *pui32UID,
									 IMG_UINT32 *pui32FWAddr,
									 IMG_UINT32 *pui32CurrentOp,
									 IMG_UINT32 *pui32NextOp)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 i;
	SYNC_BRIDGE_HANDLE hBridge = NULL;
	IMG_HANDLE *pahServerHandle;
	IMG_BOOL bServerSync;

	if (papsSync[0])
	{
		hBridge = _SyncPrimGetBridgeHandle(papsSync[0]);
	}
	if (!hBridge)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: invalid Sync connection\n", __FUNCTION__));
		eError = PVRSRV_ERROR_INVALID_SYNC_PRIM;
		goto e0;
	}

	pahServerHandle = OSAllocMem(sizeof(IMG_HANDLE) * ui32SyncCount);
	if (pahServerHandle == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}

	/*
		Check that all the sync we've been passed are server syncs
		and that they all are on the same connection.
	*/
	for (i=0;i<ui32SyncCount;i++)
	{
		SYNC_PRIM *psIntSync = IMG_CONTAINER_OF(papsSync[i], SYNC_PRIM, sCommon);

		eError = SyncPrimIsServerSync(papsSync[i], &bServerSync);
		if (PVRSRV_OK != eError) goto e1;
		if (!bServerSync)
		{
			eError = PVRSRV_ERROR_INVALID_SYNC_PRIM;
			goto e1;
		}

		if (!papsSync[i] || hBridge != _SyncPrimGetBridgeHandle(papsSync[i]))
		{
			PVR_DPF((PVR_DBG_ERROR, "SyncServerGetStatus: Sync connection is different\n"));
			eError = PVRSRV_ERROR_INVALID_SYNC_PRIM;
			goto e1;
		}

		pahServerHandle[i] = psIntSync->u.sServer.hServerSync;
	}

	eError = BridgeServerSyncGetStatus(hBridge,
									   ui32SyncCount,
									   pahServerHandle,
									   pui32UID,
									   pui32FWAddr,
									   pui32CurrentOp,
									   pui32NextOp);
	OSFreeMem(pahServerHandle);

	if (eError != PVRSRV_OK)
	{
		goto e0;
	}
	return PVRSRV_OK;

e1:
	OSFreeMem(pahServerHandle);
e0:
	return eError;
}

#endif

IMG_INTERNAL PVRSRV_ERROR
SyncPrimIsServerSync(PVRSRV_CLIENT_SYNC_PRIM *psSync, IMG_BOOL *pbServerSync)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	SYNC_PRIM *psSyncInt;

	if (!psSync)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: invalid input pointer", __FUNCTION__));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto e0;
	}
	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);
	if (psSyncInt->eType == SYNC_PRIM_TYPE_LOCAL)
	{
		*pbServerSync = IMG_FALSE;
	}
	else if (psSyncInt->eType == SYNC_PRIM_TYPE_SERVER)
	{
		*pbServerSync = IMG_TRUE;
	}
	else
	{
		/* Either the client has given us a bad pointer or there is an
		 * error in this module
		 */
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid sync type", __FUNCTION__));
		eError = PVRSRV_ERROR_INVALID_SYNC_PRIM;
		goto e0;
	}

e0:
	return eError;
}

IMG_INTERNAL
IMG_HANDLE SyncPrimGetServerHandle(PVRSRV_CLIENT_SYNC_PRIM *psSync)
{
	SYNC_PRIM *psSyncInt;

	if (!psSync)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: invalid input pointer", __FUNCTION__));
		goto e0;
	}
	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);
	if (psSyncInt->eType == SYNC_PRIM_TYPE_SERVER)
	{
		return psSyncInt->u.sServer.hServerSync;
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: invalid sync type (%d)",
			__FUNCTION__, psSyncInt->eType));
		goto e0;
	}
e0:
	return 0;
}

IMG_INTERNAL
PVRSRV_ERROR SyncPrimServerQueueOp(PVRSRV_CLIENT_SYNC_PRIM_OP *psSyncOp)
{
	SYNC_PRIM *psSyncInt;
	IMG_BOOL bUpdate;
	PVRSRV_ERROR eError;

	if (!psSyncOp)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: invalid input pointer", __FUNCTION__));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto e0;
	}

	psSyncInt = IMG_CONTAINER_OF(psSyncOp->psSync, SYNC_PRIM, sCommon);
	if (psSyncInt->eType != SYNC_PRIM_TYPE_SERVER)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: invalid sync type (%d)",
			__FUNCTION__, psSyncInt->eType));
		eError = PVRSRV_ERROR_INVALID_SYNC_PRIM;
		goto e0;
	}
	if (0 == psSyncOp->ui32Flags)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: no sync flags", __FUNCTION__));
		eError = PVRSRV_ERROR_INVALID_SYNC_PRIM;
		goto e0;
	}

	if (psSyncOp->ui32Flags & PVRSRV_CLIENT_SYNC_PRIM_OP_UPDATE)
	{
		bUpdate = IMG_TRUE;
	}else
	{
		bUpdate = IMG_FALSE;
	}

	eError = BridgeServerSyncQueueHWOp(psSyncInt->u.sServer.hBridge,
									      psSyncInt->u.sServer.hServerSync,
										  bUpdate,
									      &psSyncOp->ui32FenceValue,
									      &psSyncOp->ui32UpdateValue);
e0:
	return eError;
}

#if defined(PDUMP)
IMG_INTERNAL void SyncPrimPDump(PVRSRV_CLIENT_SYNC_PRIM *psSync)
{
	SYNC_PRIM *psSyncInt;
	SYNC_PRIM_BLOCK *psSyncBlock;
	SYNC_PRIM_CONTEXT *psContext;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psSync != NULL);
	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);

	if (psSyncInt->eType != SYNC_PRIM_TYPE_LOCAL)
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncPrimPDump: Invalid sync type"));
		PVR_ASSERT(IMG_FALSE);
		return;
	}

	psSyncBlock = psSyncInt->u.sLocal.psSyncBlock;
	psContext = psSyncBlock->psContext;

	eError = BridgeSyncPrimPDump(psContext->hDevConnection,
								 psSyncBlock->hServerSyncPrimBlock,
								 SyncPrimGetOffset(psSyncInt));

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: failed with error %d",
				__FUNCTION__, eError));
	}
    PVR_ASSERT(eError == PVRSRV_OK);
}

IMG_INTERNAL void SyncPrimPDumpValue(PVRSRV_CLIENT_SYNC_PRIM *psSync, IMG_UINT32 ui32Value)
{
	SYNC_PRIM *psSyncInt;
	SYNC_PRIM_BLOCK *psSyncBlock;
	SYNC_PRIM_CONTEXT *psContext;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psSync != NULL);
	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);

	if (psSyncInt->eType != SYNC_PRIM_TYPE_LOCAL)
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncPrimPDump: Invalid sync type"));
		PVR_ASSERT(IMG_FALSE);
		return;
	}

	psSyncBlock = psSyncInt->u.sLocal.psSyncBlock;
	psContext = psSyncBlock->psContext;

	eError = BridgeSyncPrimPDumpValue(psContext->hDevConnection,
								 psSyncBlock->hServerSyncPrimBlock,
								 SyncPrimGetOffset(psSyncInt),
								 ui32Value);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: failed with error %d",
				__FUNCTION__, eError));
	}
    PVR_ASSERT(eError == PVRSRV_OK);
}

IMG_INTERNAL void SyncPrimPDumpPol(PVRSRV_CLIENT_SYNC_PRIM *psSync,
								   IMG_UINT32 ui32Value,
								   IMG_UINT32 ui32Mask,
								   PDUMP_POLL_OPERATOR eOperator,
								   IMG_UINT32 ui32PDumpFlags)
{
	SYNC_PRIM *psSyncInt;
	SYNC_PRIM_BLOCK *psSyncBlock;
	SYNC_PRIM_CONTEXT *psContext;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psSync != NULL);
	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);

	if (psSyncInt->eType != SYNC_PRIM_TYPE_LOCAL)
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncPrimPDumpPol: Invalid sync type (expected SYNC_PRIM_TYPE_LOCAL)"));
		PVR_ASSERT(IMG_FALSE);
		return;
	}

	psSyncBlock = psSyncInt->u.sLocal.psSyncBlock;
	psContext = psSyncBlock->psContext;

	eError = BridgeSyncPrimPDumpPol(psContext->hDevConnection,
									psSyncBlock->hServerSyncPrimBlock,
									SyncPrimGetOffset(psSyncInt),
									ui32Value,
									ui32Mask,
									eOperator,
									ui32PDumpFlags);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: failed with error %d",
				__FUNCTION__, eError));
	}
    PVR_ASSERT(eError == PVRSRV_OK);
}

IMG_INTERNAL void SyncPrimOpPDumpPol(PSYNC_OP_COOKIE psCookie,
									 PDUMP_POLL_OPERATOR eOperator,
									 IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eError;

	PVR_ASSERT(psCookie != NULL);

	eError = BridgeSyncPrimOpPDumpPol(psCookie->hBridge,
									psCookie->hServerCookie,
									eOperator,
									ui32PDumpFlags);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: failed with error %d",
				__FUNCTION__, eError));
	}
	
    PVR_ASSERT(eError == PVRSRV_OK);
}

IMG_INTERNAL void SyncPrimPDumpCBP(PVRSRV_CLIENT_SYNC_PRIM *psSync,
								   IMG_UINT64 uiWriteOffset,
								   IMG_UINT64 uiPacketSize,
								   IMG_UINT64 uiBufferSize)
{
	SYNC_PRIM *psSyncInt;
	SYNC_PRIM_BLOCK *psSyncBlock;
	SYNC_PRIM_CONTEXT *psContext;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psSync != NULL);
	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);

	if (psSyncInt->eType != SYNC_PRIM_TYPE_LOCAL)
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncPrimPDumpCBP: Invalid sync type"));
		PVR_ASSERT(IMG_FALSE);
		return;
	}

	psSyncBlock = psSyncInt->u.sLocal.psSyncBlock;
	psContext = psSyncBlock->psContext;

	/* FIXME: uiWriteOffset, uiPacketSize, uiBufferSize were changed to
	 * 64-bit quantities to resolve Windows compiler warnings.
	 * However the bridge is only 32-bit hence compiler warnings
	 * of implicit cast and loss of data.
	 * Added explicit cast and assert to remove warning.
	 */
#if (defined(_WIN32) && !defined(_WIN64)) || (defined(LINUX) && defined(__i386__))
	PVR_ASSERT(uiWriteOffset<IMG_UINT32_MAX);
	PVR_ASSERT(uiPacketSize<IMG_UINT32_MAX);
	PVR_ASSERT(uiBufferSize<IMG_UINT32_MAX);
#endif
	eError = BridgeSyncPrimPDumpCBP(psContext->hDevConnection,
									psSyncBlock->hServerSyncPrimBlock,
									SyncPrimGetOffset(psSyncInt),
									(IMG_UINT32)uiWriteOffset,
									(IMG_UINT32)uiPacketSize,
									(IMG_UINT32)uiBufferSize);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: failed with error %d",
				__FUNCTION__, eError));
	}
    PVR_ASSERT(eError == PVRSRV_OK);
}

#endif

