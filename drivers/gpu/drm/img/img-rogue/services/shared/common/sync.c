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
#include "img_defs.h"
#include "client_sync_bridge.h"
#include "client_synctracking_bridge.h"
#include "info_page_client.h"
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
#if defined(__KERNEL__)
#include "pvrsrv.h"
#include "srvcore.h"
#else
#include "srvcore_intern.h"
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
		PVR_DPF((PVR_DBG_ERROR, "%s: context already freed", __func__));
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
		PVR_DPF((PVR_DBG_ERROR, "%s: context use after free", __func__));
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
	PVR_GOTO_IF_NOMEM(psSyncBlk, eError, fail_alloc);

	psSyncBlk->psContext = psContext;

	/* Allocate sync prim block */
	eError = BridgeAllocSyncPrimitiveBlock(GetBridgeHandle(psContext->hDevConnection),
	                                       &psSyncBlk->hServerSyncPrimBlock,
	                                       &psSyncBlk->ui32FirmwareAddr,
	                                       &psSyncBlk->ui32SyncBlockSize,
	                                       &hSyncPMR);
	PVR_GOTO_IF_ERROR(eError, fail_blockalloc);

	/* Make it mappable by the client */
	eError = DevmemMakeLocalImportHandle(psContext->hDevConnection,
	                                     hSyncPMR,
	                                     &hSyncImportHandle);
	PVR_GOTO_IF_ERROR(eError, fail_export);

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

	PVR_GOTO_IF_ERROR(eError, fail_import);

	eError = DevmemAcquireCpuVirtAddr(psSyncBlk->hMemDesc,
	                                  (void **) &psSyncBlk->pui32LinAddr);
	PVR_GOTO_IF_ERROR(eError, fail_cpuvaddr);

	*ppsSyncBlock = psSyncBlk;
	return PVRSRV_OK;

fail_cpuvaddr:
	DevmemFree(psSyncBlk->hMemDesc);
fail_import:
fail_export:
	BridgeFreeSyncPrimitiveBlock(GetBridgeHandle(psContext->hDevConnection),
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
	(void) DestroyServerResource(psContext->hDevConnection,
	                             NULL,
	                             BridgeFreeSyncPrimitiveBlock,
	                             psSyncBlk->hServerSyncPrimBlock);
	OSFreeMem(psSyncBlk);
}

static PVRSRV_ERROR
SyncPrimBlockImport(RA_PERARENA_HANDLE hArena,
                    RA_LENGTH_T uSize,
                    RA_FLAGS_T uFlags,
                    RA_LENGTH_T uBaseAlignment,
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
	PVR_UNREFERENCED_PARAMETER(uBaseAlignment);

	/* Check we've not been called with an unexpected size */
	PVR_LOG_GOTO_IF_INVALID_PARAM(hArena, eError, e0);
	PVR_LOG_GOTO_IF_INVALID_PARAM(uSize == sizeof(IMG_UINT32), eError, e0);

	/*
		Ensure the synprim context doesn't go away while we have sync blocks
		attached to it
	 */
	_SyncPrimContextRef(psContext);

	/* Allocate the block of memory */
	eError = AllocSyncPrimitiveBlock(psContext, &psSyncBlock);
	PVR_LOG_GOTO_IF_ERROR(eError, "AllocSyncPrimitiveBlock", fail_syncblockalloc);

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
	PVR_GOTO_IF_ERROR(eError, fail_spanalloc);

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
		/* Invalid input params */
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
	IMG_UINT64 ui64Temp = psSyncInt->uiSpanAddr - psSyncInt->psSyncBlock->uiSpanBase;

	PVR_ASSERT(ui64Temp<IMG_UINT32_MAX);

	return TRUNCATE_64BITS_TO_32BITS(ui64Temp);
}

static void SyncPrimGetCPULinAddr(SYNC_PRIM *psSyncInt)
{
	SYNC_PRIM_BLOCK *psSyncBlock = psSyncInt->psSyncBlock;

	psSyncInt->sCommon.pui32LinAddr = psSyncBlock->pui32LinAddr +
			(SyncPrimGetOffset(psSyncInt)/sizeof(IMG_UINT32));
}

static void SyncPrimLocalFree(SYNC_PRIM *psSyncInt, IMG_BOOL bFreeFirstSyncPrim)
{
	SYNC_PRIM_BLOCK *psSyncBlock;
	SYNC_PRIM_CONTEXT *psContext;

	psSyncBlock = psSyncInt->psSyncBlock;
	psContext = psSyncBlock->psContext;

#if !defined(LOCAL_SYNC_BLOCK_RETAIN_FIRST)
	PVR_UNREFERENCED_PARAMETER(bFreeFirstSyncPrim);
#else
	/* Defer freeing the first allocated sync prim in the sync context */
	if (psSyncInt != psContext->hFirstSyncPrim || bFreeFirstSyncPrim)
#endif
	{
		PVRSRV_ERROR eError;
		SHARED_DEV_CONNECTION hDevConnection =
			psSyncInt->psSyncBlock->psContext->hDevConnection;

		if (GetInfoPageDebugFlags(hDevConnection) & DEBUG_FEATURE_FULL_SYNC_TRACKING_ENABLED)
		{
			if (psSyncInt->hRecord)
			{
				/* remove this sync record */
				eError = DestroyServerResource(hDevConnection,
				                               NULL,
				                               BridgeSyncRecordRemoveByHandle,
				                               psSyncInt->hRecord);
				PVR_LOG_IF_ERROR(eError, "BridgeSyncRecordRemoveByHandle");
			}
		}
		else
		{
			IMG_UINT32 ui32FWAddr = psSyncBlock->ui32FirmwareAddr +
					SyncPrimGetOffset(psSyncInt);

			eError = BridgeSyncFreeEvent(GetBridgeHandle(hDevConnection), ui32FWAddr);
			PVR_LOG_IF_ERROR(eError, "BridgeSyncFreeEvent");
		}
#if defined(PVRSRV_ENABLE_SYNC_POISONING)
		(void) _SyncPrimSetValue(psSyncInt, LOCAL_SYNC_PRIM_POISON_VALUE);
#else
		/* reset the sync prim value as it is freed.
		 * this guarantees the client sync allocated to the client will
		 * have a value of zero and the client does not need to
		 * explicitly initialise the sync value to zero.
		 * the allocation of the backing memory for the sync prim block
		 * is done with ZERO_ON_ALLOC so the memory is initially all zero.
		 */
		(void) _SyncPrimSetValue(psSyncInt, LOCAL_SYNC_PRIM_RESET_VALUE);
#endif

		RA_Free(psContext->psSubAllocRA, psSyncInt->uiSpanAddr);
		OSFreeMem(psSyncInt);
		_SyncPrimContextUnref(psContext);
	}
}

static void SyncPrimLocalUnref(SYNC_PRIM *psSyncInt)
{
	if (!OSAtomicRead(&psSyncInt->hRefCount))
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncPrimLocalUnref sync already freed"));
	}
	else if (0 == OSAtomicDecrement(&psSyncInt->hRefCount))
	{
		SyncPrimLocalFree(psSyncInt, IMG_FALSE);
	}
}

static IMG_UINT32 SyncPrimGetFirmwareAddrLocal(SYNC_PRIM *psSyncInt)
{
	SYNC_PRIM_BLOCK *psSyncBlock;

	psSyncBlock = psSyncInt->psSyncBlock;
	return psSyncBlock->ui32FirmwareAddr + SyncPrimGetOffset(psSyncInt);
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
	PVR_GOTO_IF_NOMEM(psContext, eError, fail_alloc);

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
	                                    RA_POLICY_DEFAULT);
	PVR_GOTO_IF_NOMEM(psContext->psSubAllocRA, eError, fail_suballoc);

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
	                                RA_POLICY_DEFAULT);
	PVR_GOTO_IF_NOMEM(psContext->psSpanRA, eError, fail_span);

	if (!RA_Add(psContext->psSpanRA, 0, MAX_SYNC_MEM, 0, NULL))
	{
		RA_Delete(psContext->psSpanRA);
		PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_OUT_OF_MEMORY, fail_span);
	}

#if defined(LOCAL_SYNC_BLOCK_RETAIN_FIRST)
	psContext->hFirstSyncPrim = NULL;
#endif

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

#if defined(LOCAL_SYNC_BLOCK_RETAIN_FIRST)
	/* Free the first sync prim that was allocated as part of this context */
	if (psContext->hFirstSyncPrim)
	{
		SyncPrimLocalFree((SYNC_PRIM *)psContext->hFirstSyncPrim, IMG_TRUE);
		psContext->hFirstSyncPrim = NULL;
	}
#endif

	if (1 != OSAtomicRead(&psContext->hRefCount))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s attempted with active references, may be the result of a race", __func__));
	}
#if defined(PVRSRV_FORCE_UNLOAD_IF_BAD_STATE)
#if defined(__KERNEL__)
	if (PVRSRVGetPVRSRVData()->eServicesState != PVRSRV_SERVICES_STATE_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Forcing context destruction due to bad driver state", __func__));
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

	PVR_LOG_RETURN_IF_INVALID_PARAM(hSyncPrimContext, "hSyncPrimeContext");

	psNewSync = OSAllocMem(sizeof(SYNC_PRIM));
	PVR_GOTO_IF_NOMEM(psNewSync, eError, fail_alloc);

	eError = RA_Alloc(psContext->psSubAllocRA,
	                  sizeof(IMG_UINT32),
	                  RA_NO_IMPORT_MULTIPLIER,
	                  0,
	                  sizeof(IMG_UINT32),
	                  "Sync_Prim",
	                  &uiSpanAddr,
	                  NULL,
	                  (RA_PERISPAN_HANDLE *) &psSyncBlock);
	PVR_GOTO_IF_ERROR(eError, fail_raalloc);

	OSAtomicWrite(&psNewSync->hRefCount, 1);
	psNewSync->uiSpanAddr = uiSpanAddr;
	psNewSync->psSyncBlock = psSyncBlock;
	SyncPrimGetCPULinAddr(psNewSync);
	*ppsSync = &psNewSync->sCommon;
	_SyncPrimContextRef(psContext);
#if defined(PVRSRV_ENABLE_SYNC_POISONING)
	(void) _SyncPrimSetValue(psNewSync, LOCAL_SYNC_PRIM_RESET_VALUE);
#endif

#if defined(LOCAL_SYNC_BLOCK_RETAIN_FIRST)
	/* If this is the first sync prim allocated in the context, keep a handle to it */
	if (psSyncBlock->uiSpanBase == 0 && psNewSync->uiSpanAddr == 0)
	{
		psContext->hFirstSyncPrim = psNewSync;
	}
#endif

	if (GetInfoPageDebugFlags(psSyncBlock->psContext->hDevConnection) & DEBUG_FEATURE_FULL_SYNC_TRACKING_ENABLED)
	{
		IMG_CHAR szClassName[PVRSRV_SYNC_NAME_LENGTH];
		size_t uiSize;

		if (pszClassName)
		{
			uiSize = OSStringNLength(pszClassName, PVRSRV_SYNC_NAME_LENGTH);
			/* Copy the class name annotation into a fixed-size array */
			OSCachedMemCopy(szClassName, pszClassName, uiSize);
			if (uiSize == PVRSRV_SYNC_NAME_LENGTH)
				szClassName[PVRSRV_SYNC_NAME_LENGTH-1] = '\0';
			else
				szClassName[uiSize++] = '\0';
		}
		else
		{
			/* No class name annotation */
			uiSize = 0;
			szClassName[0] = '\0';
		}

		/* record this sync */
		eError = BridgeSyncRecordAdd(
				GetBridgeHandle(psSyncBlock->psContext->hDevConnection),
				&psNewSync->hRecord,
				psSyncBlock->hServerSyncPrimBlock,
				psSyncBlock->ui32FirmwareAddr,
				SyncPrimGetOffset(psNewSync),
				bServerSync,
				uiSize,
				szClassName);
		if (PVRSRV_OK != eError)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: failed to add SyncRecord \"%s\" (%s)",
					__func__,
					szClassName,
					PVRSRVGETERRORSTRING(eError)));
			psNewSync->hRecord = NULL;
		}
	}
	else
	{
		size_t	uiSize;

		uiSize = OSStringNLength(pszClassName, PVRSRV_SYNC_NAME_LENGTH);

		if (uiSize < PVRSRV_SYNC_NAME_LENGTH)
			uiSize++;
		/* uiSize now reflects size used for pszClassName + NUL byte */

		eError = BridgeSyncAllocEvent(GetBridgeHandle(hSyncPrimContext->hDevConnection),
		                              bServerSync,
		                              psSyncBlock->ui32FirmwareAddr + SyncPrimGetOffset(psNewSync),
		                              uiSize,
		                              pszClassName);
		PVR_LOG_IF_ERROR(eError, "BridgeSyncAllocEvent");
	}

	return PVRSRV_OK;

fail_raalloc:
	OSFreeMem(psNewSync);
fail_alloc:
	return eError;
}

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

	SYNC_PRIM_BLOCK *psSyncBlock;
	SYNC_PRIM_CONTEXT *psContext;

	psSyncBlock = psSyncInt->psSyncBlock;
	psContext = psSyncBlock->psContext;

	eError = BridgeSyncPrimSet(GetBridgeHandle(psContext->hDevConnection),
								psSyncBlock->hServerSyncPrimBlock,
								SyncPrimGetOffset(psSyncInt)/sizeof(IMG_UINT32),
								ui32Value);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR SyncPrimFree(PVRSRV_CLIENT_SYNC_PRIM *psSync)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	SYNC_PRIM *psSyncInt;

	PVR_LOG_GOTO_IF_INVALID_PARAM(psSync, eError, err_out);

	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);

	SyncPrimLocalUnref(psSyncInt);


err_out:
	return eError;
}

#if defined(NO_HARDWARE)
IMG_INTERNAL PVRSRV_ERROR
SyncPrimNoHwUpdate(PVRSRV_CLIENT_SYNC_PRIM *psSync, IMG_UINT32 ui32Value)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	SYNC_PRIM *psSyncInt;

	PVR_LOG_GOTO_IF_INVALID_PARAM(psSync, eError, err_out);

	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);

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

	PVR_LOG_GOTO_IF_INVALID_PARAM(psSync, eError, err_out);

	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);

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

	PVR_LOG_GOTO_IF_INVALID_PARAM(psSync, eError, err_out);
	PVR_LOG_GOTO_IF_INVALID_PARAM(phBlock, eError, err_out);
	PVR_LOG_GOTO_IF_INVALID_PARAM(pui32Offset, eError, err_out);

	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);

	*phBlock = psSyncInt->psSyncBlock->hServerSyncPrimBlock;
	*pui32Offset = psSyncInt->uiSpanAddr - psSyncInt->psSyncBlock->uiSpanBase;

err_out:
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR
SyncPrimGetFirmwareAddr(PVRSRV_CLIENT_SYNC_PRIM *psSync, IMG_UINT32 *pui32FwAddr)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	SYNC_PRIM *psSyncInt;

	*pui32FwAddr = 0;
	PVR_LOG_GOTO_IF_INVALID_PARAM(psSync, eError, err_out);

	psSyncInt = IMG_CONTAINER_OF(psSync, SYNC_PRIM, sCommon);
	*pui32FwAddr = SyncPrimGetFirmwareAddrLocal(psSyncInt);

err_out:
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

	psSyncBlock = psSyncInt->psSyncBlock;
	psContext = psSyncBlock->psContext;

	eError = BridgeSyncPrimPDump(GetBridgeHandle(psContext->hDevConnection),
	                             psSyncBlock->hServerSyncPrimBlock,
	                             SyncPrimGetOffset(psSyncInt));
	PVR_LOG_IF_ERROR(eError, "BridgeSyncPrimPDump");
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

	psSyncBlock = psSyncInt->psSyncBlock;
	psContext = psSyncBlock->psContext;

	eError = BridgeSyncPrimPDumpValue(GetBridgeHandle(psContext->hDevConnection),
	                                  psSyncBlock->hServerSyncPrimBlock,
	                                  SyncPrimGetOffset(psSyncInt),
	                                  ui32Value);
	PVR_LOG_IF_ERROR(eError, "BridgeSyncPrimPDumpValue");
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

	psSyncBlock = psSyncInt->psSyncBlock;
	psContext = psSyncBlock->psContext;

	eError = BridgeSyncPrimPDumpPol(GetBridgeHandle(psContext->hDevConnection),
	                                psSyncBlock->hServerSyncPrimBlock,
	                                SyncPrimGetOffset(psSyncInt),
	                                ui32Value,
	                                ui32Mask,
	                                eOperator,
	                                ui32PDumpFlags);
	PVR_LOG_IF_ERROR(eError, "BridgeSyncPrimPDumpPol");
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

	psSyncBlock = psSyncInt->psSyncBlock;
	psContext = psSyncBlock->psContext;

#if defined(__linux__) && defined(__i386__)
	PVR_ASSERT(uiWriteOffset<IMG_UINT32_MAX);
	PVR_ASSERT(uiPacketSize<IMG_UINT32_MAX);
	PVR_ASSERT(uiBufferSize<IMG_UINT32_MAX);
#endif
	eError = BridgeSyncPrimPDumpCBP(GetBridgeHandle(psContext->hDevConnection),
	                                psSyncBlock->hServerSyncPrimBlock,
	                                SyncPrimGetOffset(psSyncInt),
	                                TRUNCATE_64BITS_TO_32BITS(uiWriteOffset),
	                                TRUNCATE_64BITS_TO_32BITS(uiPacketSize),
	                                TRUNCATE_64BITS_TO_32BITS(uiBufferSize));
	PVR_LOG_IF_ERROR(eError, "BridgeSyncPrimPDumpCBP");
	PVR_ASSERT(eError == PVRSRV_OK);
}

#endif
