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
#include "allocmem.h"
#include "osfunc.h"
#include "devicemem.h"
#include "devicemem_pdump.h"
#include "pvr_debug.h"
#include "dllist.h"
#include "sync.h"
#include "sync_internal.h"
#include "lock.h"
#include "pvr_debug.h"

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

/*
	Internal interfaces for management of synchronisation block memory
*/
static PVRSRV_ERROR
AllocSyncPrimitiveBlock(SYNC_PRIM_CONTEXT *psContext,
						SYNC_PRIM_BLOCK **ppsSyncBlock)
{
	SYNC_PRIM_BLOCK *psSyncBlk;
	DEVMEM_SERVER_EXPORTCOOKIE hServerExportCookie;
	DEVMEM_EXPORTCOOKIE sExportCookie;
	PVRSRV_ERROR eError;

	psSyncBlk = OSAllocMem(sizeof(SYNC_PRIM_BLOCK));
	if (psSyncBlk == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc;
	}
	psSyncBlk->psContext = psContext;

	/* Allocate sync prim block */
	eError = BridgeAllocSyncPrimitiveBlock(psContext->hBridge,
										   psContext->hDeviceNode,
										   &psSyncBlk->hServerSyncPrimBlock,
										   &psSyncBlk->ui32FirmwareAddr,
										   &psSyncBlk->ui32SyncBlockSize,
										   &hServerExportCookie);
	if (eError != PVRSRV_OK)
	{
		goto fail_blockalloc;
	}

	/* Make it mappable by the client */
	eError = DevmemMakeServerExportClientExport(psContext->hBridge,
												hServerExportCookie,
												&sExportCookie);
	if (eError != PVRSRV_OK)
	{
		goto fail_export;
	}

	/* Get CPU mapping of the memory block */
	eError = DevmemImport(psContext->hBridge,
						  &sExportCookie,
						  PVRSRV_MEMALLOCFLAG_CPU_READABLE,
						  &psSyncBlk->hMemDesc);

	/*
		Regardless of success or failure we "undo" the export
	*/
	DevmemUnmakeServerExportClientExport(psContext->hBridge,
										 &sExportCookie);

	if (eError != PVRSRV_OK)
	{
		goto fail_import;
	}

	eError = DevmemAcquireCpuVirtAddr(psSyncBlk->hMemDesc,
									  (IMG_PVOID *) &psSyncBlk->pui32LinAddr);
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
	BridgeFreeSyncPrimitiveBlock(psContext->hBridge,
								 psSyncBlk->hServerSyncPrimBlock);
fail_blockalloc:
	OSFreeMem(psSyncBlk);
fail_alloc:
	return eError;
}

static IMG_VOID
FreeSyncPrimitiveBlock(SYNC_PRIM_BLOCK *psSyncBlk)
{
	SYNC_PRIM_CONTEXT *psContext = psSyncBlk->psContext;

	DevmemReleaseCpuVirtAddr(psSyncBlk->hMemDesc);
	DevmemFree(psSyncBlk->hMemDesc);
	BridgeFreeSyncPrimitiveBlock(psContext->hBridge,
								 psSyncBlk->hServerSyncPrimBlock);
	OSFreeMem(psSyncBlk);
}

static IMG_BOOL
SyncPrimBlockImport(RA_PERARENA_HANDLE hArena,
					RA_LENGTH_T uSize,
					RA_FLAGS_T uFlags,
					RA_BASE_T *puiBase,
					RA_LENGTH_T *puiActualSize,
					RA_PERISPAN_HANDLE *phImport)
{
	SYNC_PRIM_CONTEXT *psContext = hArena;
	SYNC_PRIM_BLOCK *psSyncBlock = IMG_NULL;
	RA_LENGTH_T uiSpanSize;
	PVRSRV_ERROR eError;
	IMG_BOOL bRet;
	PVR_UNREFERENCED_PARAMETER(uFlags);

	PVR_ASSERT(hArena != IMG_NULL);

	/* Check we've not be called with an unexpected size */
	PVR_ASSERT(uSize == sizeof(IMG_UINT32));

	/*
		Ensure the synprim context doesn't go away while we have sync blocks
		attached to it
	*/
	OSLockAcquire(psContext->hLock);
	psContext->ui32RefCount++;
	OSLockRelease(psContext->hLock);

	/* Allocate the block of memory */
	eError = AllocSyncPrimitiveBlock(psContext, &psSyncBlock);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to allocation syncprim block (%d)", eError));
		goto fail_syncblockalloc;
	}

	/* Allocate a span for it */
	bRet = RA_Alloc(psContext->psSpanRA,
					psSyncBlock->ui32SyncBlockSize,
					0,
					psSyncBlock->ui32SyncBlockSize,
					&psSyncBlock->uiSpanBase,
					&uiSpanSize,
					IMG_NULL);
	if (bRet == IMG_FALSE)
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
	return IMG_TRUE;

fail_spanalloc:
	FreeSyncPrimitiveBlock(psSyncBlock);
fail_syncblockalloc:
	OSLockAcquire(psContext->hLock);
	psContext->ui32RefCount--;
	OSLockRelease(psContext->hLock);

	return IMG_FALSE;
}

static IMG_VOID
SyncPrimBlockUnimport(RA_PERARENA_HANDLE hArena,
					  RA_BASE_T uiBase,
					  RA_PERISPAN_HANDLE hImport)
{
	SYNC_PRIM_CONTEXT *psContext = hArena;
	SYNC_PRIM_BLOCK *psSyncBlock = hImport;

	PVR_ASSERT(psContext != IMG_NULL);
	PVR_ASSERT(psSyncBlock != IMG_NULL);

	PVR_ASSERT(uiBase == psSyncBlock->uiSpanBase);

	/* Free the span this import is using */
	RA_Free(psContext->psSpanRA, uiBase);

	/* Free the syncpim block */
	FreeSyncPrimitiveBlock(psSyncBlock);

	/*	Drop our reference to the syncprim context */
	OSLockAcquire(psContext->hLock);
	psContext->ui32RefCount--;
	OSLockRelease(psContext->hLock);
}

static INLINE IMG_UINT32 SyncPrimGetOffset(SYNC_PRIM *psSyncInt)
{
	IMG_UINT64 ui64Temp;
	
	PVR_ASSERT(psSyncInt->eType == SYNC_PRIM_TYPE_LOCAL);

	
	ui64Temp =  psSyncInt->u.sLocal.uiSpanAddr - psSyncInt->u.sLocal.psSyncBlock->uiSpanBase;
	PVR_ASSERT(ui64Temp<IMG_UINT32_MAX);
	return (IMG_UINT32)ui64Temp;
}

static IMG_VOID SyncPrimGetCPULinAddr(SYNC_PRIM *psSyncInt)
{
	SYNC_PRIM_BLOCK *psSyncBlock = psSyncInt->u.sLocal.psSyncBlock;

	psSyncInt->sCommon.pui32LinAddr = psSyncBlock->pui32LinAddr +
									  (SyncPrimGetOffset(psSyncInt)/sizeof(IMG_UINT32));
}

static IMG_VOID SyncPrimLocalFree(SYNC_PRIM *psSyncInt)
{
	SYNC_PRIM_BLOCK *psSyncBlock;
	SYNC_PRIM_CONTEXT *psContext;

	PVR_ASSERT(psSyncInt->eType == SYNC_PRIM_TYPE_LOCAL);
	psSyncBlock = psSyncInt->u.sLocal.psSyncBlock;
	psContext = psSyncBlock->psContext;

	RA_Free(psContext->psSubAllocRA, psSyncInt->u.sLocal.uiSpanAddr);
}

static IMG_VOID SyncPrimServerFree(SYNC_PRIM *psSyncInt)
{
	PVRSRV_ERROR eError;

	eError = BridgeServerSyncFree(psSyncInt->u.sServer.hBridge,
								  psSyncInt->u.sServer.hServerSync);
	if (eError != PVRSRV_OK)
	{
		/* Doesn't matter if the free fails as resman will cleanup */
		PVR_DPF((PVR_DBG_ERROR, "SyncPrimServerFree failed"));
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
	return psSyncInt->u.sLocal.psSyncBlock->psContext->hBridge;
}

static SYNC_BRIDGE_HANDLE _SyncPrimGetBridgeHandleServer(SYNC_PRIM *psSyncInt)
{
	return psSyncInt->u.sServer.hBridge;
}

static SYNC_BRIDGE_HANDLE _SyncPrimGetBridgeHandle(PVRSRV_CLIENT_SYNC_PRIM *psSync)
{
	SYNC_PRIM *psSyncInt;
	PVR_ASSERT(psSync != IMG_NULL);
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
		PVR_ASSERT(IMG_FALSE);
		return 0;
	}
}
#endif

/*
	Internal interfaces for management of syncprim block lists
*/
static SYNC_BLOCK_LIST *_SyncPrimBlockListCreate(IMG_VOID)
{
	SYNC_BLOCK_LIST *psBlockList;

	psBlockList = OSAllocMem(sizeof(SYNC_BLOCK_LIST) +
								(sizeof(SYNC_PRIM_BLOCK *)
								* SYNC_BLOCK_LIST_CHUNCK_SIZE));
	if (!psBlockList)
	{
		return IMG_NULL;
	}

	psBlockList->ui32BlockCount = 0;
	psBlockList->ui32BlockListSize = SYNC_BLOCK_LIST_CHUNCK_SIZE;

	psBlockList->papsSyncPrimBlock = OSAllocMem(sizeof(SYNC_PRIM_BLOCK *)
													* SYNC_BLOCK_LIST_CHUNCK_SIZE);
	if (!psBlockList->papsSyncPrimBlock)
	{
		OSFreeMem(psBlockList);
		return IMG_NULL;
	}

	OSMemSet(psBlockList->papsSyncPrimBlock,
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
											(psBlockList->ui32BlockCount +
											SYNC_BLOCK_LIST_CHUNCK_SIZE));
		if (!papsNewSyncPrimBlock)
		{
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}

		OSMemSet(psBlockList->papsSyncPrimBlock,
				 0,
				 sizeof(SYNC_PRIM_BLOCK *) * psBlockList->ui32BlockListSize);
		OSMemCopy(papsNewSyncPrimBlock,
				  psBlockList->papsSyncPrimBlock,
				  sizeof(SYNC_PRIM_CONTEXT *) *
				  psBlockList->ui32BlockCount);

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

static IMG_VOID _SyncPrimBlockListHandleArrayDestroy(IMG_HANDLE *pahHandleList)
{
	OSFreeMem(pahHandleList);
}

static IMG_UINT32 _SyncPrimBlockListGetClientValue(SYNC_BLOCK_LIST *psBlockList,
												   IMG_UINT32 ui32BlockIndex,
												   IMG_UINT32 ui32Index)
{
	return psBlockList->papsSyncPrimBlock[ui32BlockIndex]->pui32LinAddr[ui32Index];
}

static IMG_VOID _SyncPrimBlockListDestroy(SYNC_BLOCK_LIST *psBlockList)
{
	OSFreeMem(psBlockList->papsSyncPrimBlock);
	OSFreeMem(psBlockList);
}

static INLINE IMG_UINT32 _Log2(IMG_UINT32 ui32Align)
{
	IMG_UINT32 ui32Log2Align = 0;
	while (!(ui32Align & 1))
	{
		ui32Log2Align++;
		ui32Align = ui32Align >> 1;
	}
	PVR_ASSERT(ui32Align == 1);

	return ui32Log2Align;
}

/*
	External interfaces
*/

IMG_INTERNAL PVRSRV_ERROR
SyncPrimContextCreate(SYNC_BRIDGE_HANDLE hBridge,
					  IMG_HANDLE hDeviceNode,
					  PSYNC_PRIM_CONTEXT *phSyncPrimContext)
{
	SYNC_PRIM_CONTEXT *psContext;
	PVRSRV_ERROR eError;

	psContext = OSAllocMem(sizeof(SYNC_PRIM_CONTEXT));
	if (psContext == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc;
	}

	psContext->hBridge = hBridge;
	psContext->hDeviceNode = hDeviceNode;

	eError = OSLockCreate(&psContext->hLock, LOCK_TYPE_PASSIVE);
	if ( eError != PVRSRV_OK)
	{
		goto fail_lockcreate;
	}
	
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
										psContext);
	if (psContext->psSubAllocRA == IMG_NULL)
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
									IMG_NULL,
									IMG_NULL,
									IMG_NULL);
	if (psContext->psSpanRA == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_span;
	}

	if (!RA_Add(psContext->psSpanRA, 0, MAX_SYNC_MEM, 0, IMG_NULL))
	{
		RA_Delete(psContext->psSpanRA);
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_span;
	}

	psContext->ui32RefCount = 1;

	*phSyncPrimContext = psContext;
	return PVRSRV_OK;
fail_span:
	RA_Delete(psContext->psSubAllocRA);
fail_suballoc:
	OSLockDestroy(psContext->hLock);
fail_lockcreate:
	OSFreeMem(psContext);
fail_alloc:
	return eError;
}

IMG_INTERNAL IMG_VOID SyncPrimContextDestroy(PSYNC_PRIM_CONTEXT hSyncPrimContext)
{
	SYNC_PRIM_CONTEXT *psContext = hSyncPrimContext;
	IMG_BOOL bDoRefCheck = IMG_TRUE;


#if defined(__KERNEL__)
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	if (psPVRSRVData->eServicesState != PVRSRV_SERVICES_STATE_OK)
	{
		bDoRefCheck =  IMG_FALSE;
	}
#endif
	OSLockAcquire(psContext->hLock);
	if (--psContext->ui32RefCount != 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncPrimContextDestroy: Refcount non-zero: %d", psContext->ui32RefCount));

		if (bDoRefCheck)
		{
			PVR_ASSERT(0);
		}
		return;
	}
	/*
		If we fail above then we won't release the lock. However, if that
		happens things have already gone very wrong and we bail to save
		freeing memory which might still be in use and holding this lock
		will show up if anyone is trying to use this context after it has
		been destroyed.
	*/
	OSLockRelease(psContext->hLock);

	RA_Delete(psContext->psSpanRA);
	RA_Delete(psContext->psSubAllocRA);
	OSLockDestroy(psContext->hLock);
	OSFreeMem(psContext);
}

IMG_INTERNAL PVRSRV_ERROR SyncPrimAlloc(PSYNC_PRIM_CONTEXT hSyncPrimContext,
										PVRSRV_CLIENT_SYNC_PRIM **ppsSync,
										const IMG_CHAR *pszClassName)
{
	SYNC_PRIM_CONTEXT *psContext = hSyncPrimContext;
	SYNC_PRIM_BLOCK *psSyncBlock;
	SYNC_PRIM *psNewSync;
	PVRSRV_ERROR eError;
	RA_BASE_T uiSpanAddr;

	psNewSync = OSAllocMem(sizeof(SYNC_PRIM));
	if (psNewSync == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc;
	}

	if (!RA_Alloc(psContext->psSubAllocRA,
				  sizeof(IMG_UINT32),
				  0,
				  sizeof(IMG_UINT32),
				  &uiSpanAddr,
				  IMG_NULL,
				  (RA_PERISPAN_HANDLE *) &psSyncBlock))
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_raalloc;
	}
	psNewSync->eType = SYNC_PRIM_TYPE_LOCAL;
	psNewSync->u.sLocal.uiSpanAddr = uiSpanAddr;
	psNewSync->u.sLocal.psSyncBlock = psSyncBlock;
	SyncPrimGetCPULinAddr(psNewSync);
	*ppsSync = &psNewSync->sCommon;

#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
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
					psSyncBlock->psContext->hBridge,
					&psNewSync->u.sLocal.hRecord,
					psSyncBlock->hServerSyncPrimBlock,
					psSyncBlock->ui32FirmwareAddr,
					SyncPrimGetOffset(psNewSync),
#if defined(__KERNEL__)
					IMG_TRUE,
#else
					IMG_FALSE,
#endif
					OSStringNLength(szClassName, SYNC_MAX_CLASS_NAME_LEN),
					szClassName);
	}
#else
	PVR_UNREFERENCED_PARAMETER(pszClassName);
#endif /* if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING) */

	return PVRSRV_OK;

fail_raalloc:
	OSFreeMem(psNewSync);
fail_alloc:
	PVR_ASSERT(eError != PVRSRV_OK);

	return eError;
}

IMG_INTERNAL IMG_VOID SyncPrimFree(PVRSRV_CLIENT_SYNC_PRIM *psSync)
{
	SYNC_PRIM *psSyncInt;

	PVR_ASSERT(psSync != IMG_NULL);
	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);

	if (psSyncInt->eType == SYNC_PRIM_TYPE_LOCAL)
	{
#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
		PVRSRV_ERROR eError;
		/* remove this sync record */
		eError = BridgeSyncRecordRemoveByHandle(
						psSyncInt->u.sLocal.psSyncBlock->psContext->hBridge,
						psSyncInt->u.sLocal.hRecord);
		PVR_ASSERT(PVRSRV_OK == eError);
#endif
		SyncPrimLocalFree(psSyncInt);
	}
	else if (psSyncInt->eType == SYNC_PRIM_TYPE_SERVER)
	{
		SyncPrimServerFree(psSyncInt);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncPrimFree: Invalid sync type"));
		/*
			Either the client has given us a bad pointer or there is an
			error in this module
		*/
		PVR_ASSERT(IMG_FALSE);
		return;
	}

	OSFreeMem(psSyncInt);
}

static IMG_VOID
_SyncPrimSetValue(SYNC_PRIM *psSyncInt, IMG_UINT32 ui32Value)
{
	PVRSRV_ERROR eError;

	if (psSyncInt->eType == SYNC_PRIM_TYPE_LOCAL)
	{
		SYNC_PRIM_BLOCK *psSyncBlock;
		SYNC_PRIM_CONTEXT *psContext;

		psSyncBlock = psSyncInt->u.sLocal.psSyncBlock;
		psContext = psSyncBlock->psContext;

		eError = BridgeSyncPrimSet(psContext->hBridge,
									psSyncBlock->hServerSyncPrimBlock,
									SyncPrimGetOffset(psSyncInt)/sizeof(IMG_UINT32),
									ui32Value);
		PVR_ASSERT(eError == PVRSRV_OK);
	}
	else
	{
		eError = BridgeServerSyncPrimSet(psSyncInt->u.sServer.hBridge,
									psSyncInt->u.sServer.hServerSync,
									ui32Value);
		PVR_ASSERT(eError == PVRSRV_OK);

	}
}

#if defined(NO_HARDWARE)
IMG_INTERNAL IMG_VOID
SyncPrimNoHwUpdate(PVRSRV_CLIENT_SYNC_PRIM *psSync, IMG_UINT32 ui32Value)
{
	SYNC_PRIM *psSyncInt;

	PVR_ASSERT(psSync != IMG_NULL);
	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);

	/* There is no check for the psSyncInt to be LOCAL as this call
	   substitutes the Firmware updating a sync and that sync could
	   be a server one */

	_SyncPrimSetValue(psSyncInt, ui32Value);
}
#endif

IMG_INTERNAL IMG_VOID
SyncPrimSet(PVRSRV_CLIENT_SYNC_PRIM *psSync, IMG_UINT32 ui32Value)
{
	SYNC_PRIM *psSyncInt;

	PVR_ASSERT(psSync != IMG_NULL);
	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);

	if (psSyncInt->eType != SYNC_PRIM_TYPE_LOCAL)
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncPrimSet: Invalid sync type"));
		/*PVR_ASSERT(IMG_FALSE);*/
		return;
	}

	_SyncPrimSetValue(psSyncInt, ui32Value);

#if defined(PDUMP)
	SyncPrimPDump(psSync);
#endif

}

IMG_INTERNAL IMG_UINT32 SyncPrimGetFirmwareAddr(PVRSRV_CLIENT_SYNC_PRIM *psSync)
{
	SYNC_PRIM *psSyncInt;
	PVR_ASSERT(psSync != IMG_NULL);
	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);

	if (psSyncInt->eType == SYNC_PRIM_TYPE_LOCAL)
	{
		return SyncPrimGetFirmwareAddrLocal(psSyncInt);
	}
	else if (psSyncInt->eType == SYNC_PRIM_TYPE_SERVER)
	{
		return SyncPrimGetFirmwareAddrServer(psSyncInt);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncPrimGetFirmwareAddr: Invalid sync type"));
		/*
			Either the client has given us a bad pointer or there is an
			error in this module
		*/
		PVR_ASSERT(IMG_FALSE);
		return 0;
	}
}

#if !defined(__KERNEL__)
IMG_INTERNAL PVRSRV_ERROR SyncPrimDumpSyncs(IMG_UINT32 ui32SyncCount, PVRSRV_CLIENT_SYNC_PRIM **papsSync, const IMG_CHAR *pcszExtraInfo)
{
#if defined(PVRSRV_NEED_PVR_DPF)
	SYNC_PRIM *psSyncInt;
	PVRSRV_CLIENT_SYNC_PRIM **papsServerSync;
	IMG_UINT32 ui32ServerSyncs = 0;
	IMG_UINT32 *pui32UID = IMG_NULL;
	IMG_UINT32 *pui32FWAddr = IMG_NULL;
	IMG_UINT32 *pui32CurrentOp = IMG_NULL;
	IMG_UINT32 *pui32NextOp = IMG_NULL;
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
			PVR_ASSERT(IMG_FALSE);
			eError = PVRSRV_ERROR_INVALID_PARAMS;
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

	psSyncBlockList = _SyncPrimBlockListCreate();
	
	if (!psSyncBlockList)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}

	for (i=0;i<ui32SyncCount;i++)
	{
		if (SyncPrimIsServerSync(papsSyncPrim[i]))
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
	PVR_ASSERT(pcPtr == (((IMG_CHAR *) psNewCookie) + ui32TotalAllocSize));

	psNewCookie->ui32SyncCount = ui32SyncCount;
	psNewCookie->ui32ServerSyncCount = ui32ServerSyncCount;
	psNewCookie->ui32ClientSyncCount = ui32ClientSyncCount;
	psNewCookie->psSyncBlockList = psSyncBlockList;

	/*
		Get the bridge handle from the 1st sync.

		Note: We assume the all syncs have been created with the same
			  services connection.
	*/
	if (SyncPrimIsServerSync(papsSyncPrim[0]))
	{
		SYNC_PRIM *psSync = (SYNC_PRIM *) papsSyncPrim[0];

		hBridge = psSync->u.sServer.hBridge;
	}
	else
	{
		SYNC_PRIM *psSync = (SYNC_PRIM *) papsSyncPrim[0];

		hBridge = psSync->u.sLocal.psSyncBlock->psContext->hBridge;		
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

		if (SyncPrimIsServerSync(papsSyncPrim[i]))
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
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32ServerIndex = 0;
	IMG_UINT32 ui32ClientIndex = 0;
	IMG_UINT32 i;

	/* Copy client sync operations */
	for (i=0;i<ui32SyncCount;i++)
	{
		/*
			Sanity check the client passes in the same syncs as the
			ones we got at create time
		*/
		if (psCookie->papsSyncPrim[i] != pasSyncOp[i].psSync)
		{
			return PVRSRV_ERROR_INVALID_PARAMS;
		}

		if (SyncPrimIsServerSync(pasSyncOp[i].psSync))
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

	return eError;
}

IMG_INTERNAL
PVRSRV_ERROR SyncPrimOpReady(PSYNC_OP_COOKIE psCookie,
							 IMG_BOOL *pbReady)
{
	PVRSRV_ERROR eError;
	PVR_ASSERT(psCookie != IMG_NULL);

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
	PVR_ASSERT(eError != PVRSRV_OK);
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
IMG_VOID SyncPrimOpDestroy(PSYNC_OP_COOKIE psCookie)
{
	PVRSRV_ERROR eError;

	eError = BridgeSyncPrimOpDestroy(psCookie->hBridge,
									 psCookie->hServerCookie);
	PVR_ASSERT(eError == PVRSRV_OK);

	_SyncPrimBlockListDestroy(psCookie->psSyncBlockList);
	OSFreeMem(psCookie);
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

	psSyncOps = OSAllocMem(sizeof(PVRSRV_CLIENT_SYNC_PRIM_OP) * 
						   psCookie->ui32SyncCount);
	if (!psSyncOps)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	
	for (i=0; i<psCookie->ui32SyncCount; i++)
	{
		psSyncOps[i].psSync = psCookie->papsSyncPrim[i];
		if (SyncPrimIsServerSync(psCookie->papsSyncPrim[i]))
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

	return PVRSRV_OK;
}

#if !defined(__KERNEL__)
IMG_INTERNAL
PVRSRV_ERROR SyncPrimServerAlloc(SYNC_BRIDGE_HANDLE hBridge,
								 IMG_HANDLE hDeviceNode,
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
	if (psNewSync == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}
	OSMemSet(psNewSync, 0, sizeof(SYNC_PRIM));

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
								   hDeviceNode,
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
	SYNC_BRIDGE_HANDLE hBridge = _SyncPrimGetBridgeHandle(papsSync[0]);
	IMG_HANDLE *pahServerHandle;

	pahServerHandle = OSAllocMem(sizeof(IMG_HANDLE) * ui32SyncCount);
	if (pahServerHandle == IMG_NULL)
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

		if (!SyncPrimIsServerSync(papsSync[i]))
		{
			eError = PVRSRV_ERROR_INVALID_SYNC_PRIM;
			goto e1;
		}

		if (hBridge != _SyncPrimGetBridgeHandle(papsSync[i]))
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
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

#endif

IMG_INTERNAL
IMG_BOOL SyncPrimIsServerSync(PVRSRV_CLIENT_SYNC_PRIM *psSync)
{
	SYNC_PRIM *psSyncInt;

	PVR_ASSERT(psSync != IMG_NULL);
	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);
	if (psSyncInt->eType == SYNC_PRIM_TYPE_SERVER)
	{
		return IMG_TRUE;
	}

	return IMG_FALSE;
}

IMG_INTERNAL
IMG_HANDLE SyncPrimGetServerHandle(PVRSRV_CLIENT_SYNC_PRIM *psSync)
{
	SYNC_PRIM *psSyncInt;

	PVR_ASSERT(psSync != IMG_NULL);
	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);
	PVR_ASSERT(psSyncInt->eType == SYNC_PRIM_TYPE_SERVER);

	return psSyncInt->u.sServer.hServerSync;
}

IMG_INTERNAL
PVRSRV_ERROR SyncPrimServerQueueOp(PVRSRV_CLIENT_SYNC_PRIM_OP *psSyncOp)
{
	SYNC_PRIM *psSyncInt;
	IMG_BOOL bUpdate;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psSyncOp != IMG_NULL);
	psSyncInt = IMG_CONTAINER_OF(psSyncOp->psSync, SYNC_PRIM, sCommon);
	if (psSyncInt->eType != SYNC_PRIM_TYPE_SERVER)
	{
		return PVRSRV_ERROR_INVALID_SYNC_PRIM;
	}

	PVR_ASSERT(psSyncOp->ui32Flags != 0);
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
	return eError;
}

#if defined(PDUMP)
IMG_INTERNAL IMG_VOID SyncPrimPDump(PVRSRV_CLIENT_SYNC_PRIM *psSync)
{
	SYNC_PRIM *psSyncInt;
	SYNC_PRIM_BLOCK *psSyncBlock;
	SYNC_PRIM_CONTEXT *psContext;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psSync != IMG_NULL);
	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);

	if (psSyncInt->eType != SYNC_PRIM_TYPE_LOCAL)
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncPrimPDump: Invalid sync type"));
		PVR_ASSERT(IMG_FALSE);
		return;
	}

	psSyncBlock = psSyncInt->u.sLocal.psSyncBlock;
	psContext = psSyncBlock->psContext;

	eError = BridgeSyncPrimPDump(psContext->hBridge,
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

IMG_INTERNAL IMG_VOID SyncPrimPDumpValue(PVRSRV_CLIENT_SYNC_PRIM *psSync, IMG_UINT32 ui32Value)
{
	SYNC_PRIM *psSyncInt;
	SYNC_PRIM_BLOCK *psSyncBlock;
	SYNC_PRIM_CONTEXT *psContext;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psSync != IMG_NULL);
	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);

	if (psSyncInt->eType != SYNC_PRIM_TYPE_LOCAL)
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncPrimPDump: Invalid sync type"));
		PVR_ASSERT(IMG_FALSE);
		return;
	}

	psSyncBlock = psSyncInt->u.sLocal.psSyncBlock;
	psContext = psSyncBlock->psContext;

	eError = BridgeSyncPrimPDumpValue(psContext->hBridge,
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

IMG_INTERNAL IMG_VOID SyncPrimPDumpPol(PVRSRV_CLIENT_SYNC_PRIM *psSync,
									   IMG_UINT32 ui32Value,
									   IMG_UINT32 ui32Mask,
									   PDUMP_POLL_OPERATOR eOperator,
									   IMG_UINT32 ui32PDumpFlags)
{
	SYNC_PRIM *psSyncInt;
	SYNC_PRIM_BLOCK *psSyncBlock;
	SYNC_PRIM_CONTEXT *psContext;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psSync != IMG_NULL);
	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);

	if (psSyncInt->eType != SYNC_PRIM_TYPE_LOCAL)
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncPrimPDumpPol: Invalid sync type (expected SYNC_PRIM_TYPE_LOCAL)"));
		PVR_ASSERT(IMG_FALSE);
		return;
	}

	psSyncBlock = psSyncInt->u.sLocal.psSyncBlock;
	psContext = psSyncBlock->psContext;

	eError = BridgeSyncPrimPDumpPol(psContext->hBridge,
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

IMG_INTERNAL IMG_VOID SyncPrimOpPDumpPol(PSYNC_OP_COOKIE psCookie,
									   PDUMP_POLL_OPERATOR eOperator,
									   IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eError;

	PVR_ASSERT(psCookie != IMG_NULL);

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

IMG_INTERNAL IMG_VOID SyncPrimPDumpCBP(PVRSRV_CLIENT_SYNC_PRIM *psSync,
									   IMG_UINT64 uiWriteOffset,
									   IMG_UINT64 uiPacketSize,
									   IMG_UINT64 uiBufferSize)
{
	SYNC_PRIM *psSyncInt;
	SYNC_PRIM_BLOCK *psSyncBlock;
	SYNC_PRIM_CONTEXT *psContext;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psSync != IMG_NULL);
	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);

	if (psSyncInt->eType != SYNC_PRIM_TYPE_LOCAL)
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncPrimPDumpCBP: Invalid sync type"));
		PVR_ASSERT(IMG_FALSE);
		return;
	}

	psSyncBlock = psSyncInt->u.sLocal.psSyncBlock;
	psContext = psSyncBlock->psContext;

	
#if (defined(_WIN32) && !defined(_WIN64)) || (defined(LINUX) && defined(__i386__))
	PVR_ASSERT(uiWriteOffset<IMG_UINT32_MAX);
	PVR_ASSERT(uiPacketSize<IMG_UINT32_MAX);
	PVR_ASSERT(uiBufferSize<IMG_UINT32_MAX);
#endif
	eError = BridgeSyncPrimPDumpCBP(psContext->hBridge,
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

