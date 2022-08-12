/*************************************************************************/ /*!
@File
@Title          Services synchronisation checkpoint interface
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Server side code for services synchronisation interface
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

#include "img_defs.h"
#include "img_types.h"
#include "allocmem.h"
#include "devicemem.h"
#include "devicemem_pdump.h"
#include "pvr_debug.h"
#include "pvr_notifier.h"
#include "osfunc.h"
#include "dllist.h"
#include "sync.h"
#include "sync_checkpoint_external.h"
#include "sync_checkpoint.h"
#include "sync_checkpoint_internal.h"
#include "sync_checkpoint_init.h"
#include "lock.h"
#include "log2.h"
#include "pvrsrv.h"
#include "pdump_km.h"
#include "info_page.h"

#include "pvrsrv_sync_km.h"
#include "rgxhwperf.h"

#if defined(SUPPORT_VALIDATION) && defined(SUPPORT_SOC_TIMER)
#include "rgxsoctimer.h"
#endif

#if defined(PVRSRV_NEED_PVR_DPF)

/* Enable this to turn on debug relating to the creation and
   resolution of contexts */
#define ENABLE_SYNC_CHECKPOINT_CONTEXT_DEBUG 0

/* Enable this to turn on debug relating to the creation and
   resolution of fences */
#define ENABLE_SYNC_CHECKPOINT_FENCE_DEBUG 0

/* Enable this to turn on debug relating to the sync checkpoint
   allocation and freeing */
#define ENABLE_SYNC_CHECKPOINT_ALLOC_AND_FREE_DEBUG 0

/* Enable this to turn on debug relating to the sync checkpoint
   enqueuing and signalling */
#define ENABLE_SYNC_CHECKPOINT_ENQ_AND_SIGNAL_DEBUG 0

/* Enable this to turn on debug relating to the sync checkpoint pool */
#define ENABLE_SYNC_CHECKPOINT_POOL_DEBUG 0

/* Enable this to turn on debug relating to sync checkpoint UFO
   lookup */
#define ENABLE_SYNC_CHECKPOINT_UFO_DEBUG 0

/* Enable this to turn on sync checkpoint deferred cleanup debug
 * (for syncs we have been told to free but which have some
 * outstanding FW operations remaining (enqueued in CCBs)
 */
#define ENABLE_SYNC_CHECKPOINT_DEFERRED_CLEANUP_DEBUG 0

#else

#define ENABLE_SYNC_CHECKPOINT_CONTEXT_DEBUG 0
#define ENABLE_SYNC_CHECKPOINT_FENCE_DEBUG 0
#define ENABLE_SYNC_CHECKPOINT_ALLOC_AND_FREE_DEBUG 0
#define ENABLE_SYNC_CHECKPOINT_ENQ_AND_SIGNAL_DEBUG 0
#define ENABLE_SYNC_CHECKPOINT_POOL_DEBUG 0
#define ENABLE_SYNC_CHECKPOINT_UFO_DEBUG 0
#define ENABLE_SYNC_CHECKPOINT_DEFERRED_CLEANUP_DEBUG 0

#endif

/* Maximum number of deferred sync checkpoint signal/error received for atomic context */
#define SYNC_CHECKPOINT_MAX_DEFERRED_SIGNAL 500

/* Set the size of the sync checkpoint pool (not used if 0).
 * A pool will be maintained for each sync checkpoint context.
 */
#if defined(PDUMP)
#define SYNC_CHECKPOINT_POOL_SIZE	0
#else
#define SYNC_CHECKPOINT_POOL_SIZE	128
#define SYNC_CHECKPOINT_POOL_MASK (SYNC_CHECKPOINT_POOL_SIZE - 1)
#endif

/* The 'sediment' value represents the minimum number of
 * sync checkpoints which must be in the pool before one
 * will be allocated from the pool rather than from memory.
 * This effectively helps avoid re-use of a sync checkpoint
 * just after it has been returned to the pool, making
 * debugging somewhat easier to understand.
 */
#define SYNC_CHECKPOINT_POOL_SEDIMENT 20

#if (SYNC_CHECKPOINT_POOL_SIZE & (SYNC_CHECKPOINT_POOL_SIZE - 1)) != 0
#error "SYNC_CHECKPOINT_POOL_SIZE must be power of 2."
#endif

#define SYNC_CHECKPOINT_BLOCK_LIST_CHUNK_SIZE  10

/*
	This defines the maximum amount of synchronisation memory
	that can be allocated per sync checkpoint context.
	In reality this number is meaningless as we would run out
	of synchronisation memory before we reach this limit, but
	we need to provide a size to the span RA.
 */
#define MAX_SYNC_CHECKPOINT_MEM  (4 * 1024 * 1024)


typedef struct _SYNC_CHECKPOINT_BLOCK_LIST_
{
	IMG_UINT32            ui32BlockCount;            /*!< Number of contexts in the list */
	IMG_UINT32            ui32BlockListSize;         /*!< Size of the array contexts */
	SYNC_CHECKPOINT_BLOCK **papsSyncCheckpointBlock; /*!< Array of sync checkpoint blocks */
} SYNC_CHECKPOINT_BLOCK_LIST;

struct _SYNC_CHECKPOINT_CONTEXT_CTL_
{
	SHARED_DEV_CONNECTION					psDeviceNode;
	PFN_SYNC_CHECKPOINT_FENCE_RESOLVE_FN	pfnFenceResolve;
	PFN_SYNC_CHECKPOINT_FENCE_CREATE_FN		pfnFenceCreate;
	/*
	 *  Used as head of linked-list of sync checkpoints for which
	 *  SyncCheckpointFree() has been called, but have outstanding
	 *  FW operations (enqueued in CCBs)
	 *  This list will be check whenever a SyncCheckpointFree() is
	 *  called, and when SyncCheckpointContextDestroy() is called.
	 */
	DLLIST_NODE								sDeferredCleanupListHead;
	/* Lock to protect the deferred cleanup list */
	POS_SPINLOCK							hDeferredCleanupListLock;

#if (SYNC_CHECKPOINT_POOL_SIZE > 0)
	SYNC_CHECKPOINT						*psSyncCheckpointPool[SYNC_CHECKPOINT_POOL_SIZE];
	IMG_BOOL								bSyncCheckpointPoolFull;
	IMG_BOOL								bSyncCheckpointPoolValid;
	IMG_UINT32								ui32SyncCheckpointPoolCount;
	IMG_UINT32								ui32SyncCheckpointPoolWp;
	IMG_UINT32								ui32SyncCheckpointPoolRp;
	POS_SPINLOCK							hSyncCheckpointPoolLock; /*! Protects access to the checkpoint pool control data. */
#endif
}; /*_SYNC_CHECKPOINT_CONTEXT_CTL is already typedef-ed in sync_checkpoint_internal.h */

/* this is the max number of sync checkpoint records we will search or dump
 * at any time.
 */
#define SYNC_CHECKPOINT_RECORD_LIMIT 20000

#define DECREMENT_WITH_WRAP(value, sz) ((value) ? ((value) - 1) : ((sz) - 1))

struct SYNC_CHECKPOINT_RECORD
{
	PVRSRV_DEVICE_NODE		*psDevNode;
	SYNC_CHECKPOINT_BLOCK	*psSyncCheckpointBlock;	/*!< handle to SYNC_CHECKPOINT_BLOCK */
	IMG_UINT32				ui32SyncOffset;			/*!< offset to sync in block */
	IMG_UINT32				ui32FwBlockAddr;
	IMG_PID					uiPID;
	IMG_UINT32				ui32UID;
	IMG_UINT64				ui64OSTime;
	DLLIST_NODE				sNode;
	IMG_CHAR				szClassName[PVRSRV_SYNC_NAME_LENGTH];
	PSYNC_CHECKPOINT		pSyncCheckpt;
};

static PFN_SYNC_CHECKPOINT_STRUCT *g_psSyncCheckpointPfnStruct = NULL;

#if (SYNC_CHECKPOINT_POOL_SIZE > 0)
static SYNC_CHECKPOINT *_GetCheckpointFromPool(_SYNC_CHECKPOINT_CONTEXT *psContext);
static IMG_BOOL _PutCheckpointInPool(SYNC_CHECKPOINT *psSyncCheckpoint);
static IMG_UINT32 _CleanCheckpointPool(_SYNC_CHECKPOINT_CONTEXT *psContext);
#endif

#if (ENABLE_SYNC_CHECKPOINT_CONTEXT_DEBUG == 1)
static IMG_UINT32 gui32NumSyncCheckpointContexts = 0;
#endif

/* Defined values to indicate status of sync checkpoint, which is
 * stored in the memory of the structure */
#define SYNC_CHECKPOINT_PATTERN_IN_USE 0x1a1aa
#define SYNC_CHECKPOINT_PATTERN_IN_POOL 0x2b2bb
#define SYNC_CHECKPOINT_PATTERN_FREED 0x3c3cc

#if defined(SUPPORT_RGX)
static inline void RGXSRVHWPerfSyncCheckpointUFOIsSignalled(PVRSRV_RGXDEV_INFO *psDevInfo,
                               SYNC_CHECKPOINT *psSyncCheckpointInt, IMG_UINT32 ui32FenceSyncFlags)
{
	if (RGXHWPerfHostIsEventEnabled(psDevInfo, RGX_HWPERF_HOST_UFO)
	    && !(ui32FenceSyncFlags & PVRSRV_FENCE_FLAG_SUPPRESS_HWP_PKT))
	{
		RGX_HWPERF_UFO_EV eEv;
		RGX_HWPERF_UFO_DATA_ELEMENT sSyncData;

		if (psSyncCheckpointInt)
		{
			if ((psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_SIGNALLED) ||
				(psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_ERRORED))
			{
				sSyncData.sCheckSuccess.ui32FWAddr = SyncCheckpointGetFirmwareAddr((PSYNC_CHECKPOINT)psSyncCheckpointInt);
				sSyncData.sCheckSuccess.ui32Value = psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State;
				eEv = RGX_HWPERF_UFO_EV_CHECK_SUCCESS;
			}
			else
			{
				sSyncData.sCheckFail.ui32FWAddr = SyncCheckpointGetFirmwareAddr((PSYNC_CHECKPOINT)psSyncCheckpointInt);
				sSyncData.sCheckFail.ui32Value = psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State;
				sSyncData.sCheckFail.ui32Required = PVRSRV_SYNC_CHECKPOINT_SIGNALLED;
				eEv = RGX_HWPERF_UFO_EV_CHECK_FAIL;
			}
			RGXHWPerfHostPostUfoEvent(psDevInfo, eEv, &sSyncData,
			    (ui32FenceSyncFlags & PVRSRV_FENCE_FLAG_CTX_ATOMIC) ? IMG_FALSE : IMG_TRUE);
		}
	}
}

static inline void RGXSRVHWPerfSyncCheckpointUFOUpdate(PVRSRV_RGXDEV_INFO *psDevInfo,
                               SYNC_CHECKPOINT *psSyncCheckpointInt, IMG_UINT32 ui32FenceSyncFlags)
{
	if (RGXHWPerfHostIsEventEnabled(psDevInfo, RGX_HWPERF_HOST_UFO)
	    && !(ui32FenceSyncFlags & PVRSRV_FENCE_FLAG_SUPPRESS_HWP_PKT))
	{
		RGX_HWPERF_UFO_DATA_ELEMENT sSyncData;

		if (psSyncCheckpointInt)
		{
			sSyncData.sUpdate.ui32FWAddr = SyncCheckpointGetFirmwareAddr((PSYNC_CHECKPOINT)psSyncCheckpointInt);
			sSyncData.sUpdate.ui32OldValue = psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State;
			sSyncData.sUpdate.ui32NewValue = PVRSRV_SYNC_CHECKPOINT_SIGNALLED;
			RGXHWPerfHostPostUfoEvent(psDevInfo, RGX_HWPERF_UFO_EV_UPDATE, &sSyncData,
			    (ui32FenceSyncFlags & PVRSRV_FENCE_FLAG_CTX_ATOMIC) ? IMG_FALSE : IMG_TRUE);
		}
	}
}
#endif

static PVRSRV_ERROR
_SyncCheckpointRecordAdd(PSYNC_CHECKPOINT_RECORD_HANDLE *phRecord,
	                    SYNC_CHECKPOINT_BLOCK *hSyncCheckpointBlock,
	                    IMG_UINT32 ui32FwBlockAddr,
	                    IMG_UINT32 ui32SyncOffset,
	                    IMG_UINT32 ui32UID,
	                    IMG_UINT32 ui32ClassNameSize,
	                    const IMG_CHAR *pszClassName, PSYNC_CHECKPOINT pSyncCheckpt);
static PVRSRV_ERROR
_SyncCheckpointRecordRemove(PSYNC_CHECKPOINT_RECORD_HANDLE hRecord);
static void _SyncCheckpointState(PDLLIST_NODE psNode,
                                 DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                 void *pvDumpDebugFile);
static void _SyncCheckpointDebugRequest(PVRSRV_DBGREQ_HANDLE hDebugRequestHandle,
                                        IMG_UINT32 ui32VerbLevel,
                                        DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                        void *pvDumpDebugFile);
static PVRSRV_ERROR _SyncCheckpointRecordListInit(PVRSRV_DEVICE_NODE *psDevNode);
static void _SyncCheckpointRecordListDeinit(PVRSRV_DEVICE_NODE *psDevNode);

#if defined(PDUMP)
static void
MISRHandler_PdumpDeferredSyncSignalPoster(void *pvData);
static PVRSRV_ERROR _SyncCheckpointAllocPDump(PVRSRV_DEVICE_NODE *psDevNode, SYNC_CHECKPOINT *psSyncCheckpoint);
static PVRSRV_ERROR _SyncCheckpointUpdatePDump(PPVRSRV_DEVICE_NODE psDevNode, SYNC_CHECKPOINT *psSyncCheckpoint, IMG_UINT32 ui32Status, IMG_UINT32 ui32FenceSyncFlags);
static PVRSRV_ERROR _SyncCheckpointPDumpTransition(void *pvData, PDUMP_TRANSITION_EVENT eEvent);
#endif

/* Unique incremental ID assigned to sync checkpoints when allocated */
static IMG_UINT32 g_SyncCheckpointUID;

static void _CheckDeferredCleanupList(_SYNC_CHECKPOINT_CONTEXT *psContext);

void SyncCheckpointContextUnref(PSYNC_CHECKPOINT_CONTEXT psContext)
{
	_SYNC_CHECKPOINT_CONTEXT *psContextInt = (_SYNC_CHECKPOINT_CONTEXT *) psContext;
	_SYNC_CHECKPOINT_CONTEXT_CTL *const psCtxCtl = psContextInt->psContextCtl;
	IMG_UINT32 ui32RefCt = OSAtomicRead(&psContextInt->hRefCount);

	if (ui32RefCt == 0)
	{
		PVR_LOG_ERROR(PVRSRV_ERROR_INVALID_CONTEXT,
		              "SyncCheckpointContextUnref context already freed");
	}
	else if (OSAtomicDecrement(&psContextInt->hRefCount) == 0)
	{
		/* SyncCheckpointContextDestroy only when no longer referenced */
		OSSpinLockDestroy(psCtxCtl->hDeferredCleanupListLock);
		psCtxCtl->hDeferredCleanupListLock = NULL;
#if (SYNC_CHECKPOINT_POOL_SIZE > 0)
		if (psCtxCtl->ui32SyncCheckpointPoolCount)
		{
			PVR_DPF((PVR_DBG_WARNING,
			        "%s called for context<%p> with %d sync checkpoints still"
			        " in the pool",
			        __func__,
			        (void *) psContext,
			        psCtxCtl->ui32SyncCheckpointPoolCount));
		}
		psCtxCtl->bSyncCheckpointPoolValid = IMG_FALSE;
		OSSpinLockDestroy(psCtxCtl->hSyncCheckpointPoolLock);
		psCtxCtl->hSyncCheckpointPoolLock = NULL;
#endif
		OSFreeMem(psContextInt->psContextCtl);
		RA_Delete(psContextInt->psSpanRA);
		RA_Delete(psContextInt->psSubAllocRA);
		OSLockDestroy(psContextInt->hLock);
		psContextInt->hLock = NULL;
		OSFreeMem(psContext);
	}
}

void SyncCheckpointContextRef(PSYNC_CHECKPOINT_CONTEXT psContext)
{
	_SYNC_CHECKPOINT_CONTEXT *psContextInt = (_SYNC_CHECKPOINT_CONTEXT *)psContext;
	IMG_UINT32 ui32RefCt = OSAtomicRead(&psContextInt->hRefCount);

	if (ui32RefCt == 0)
	{
		PVR_LOG_ERROR(PVRSRV_ERROR_INVALID_CONTEXT,
		              "SyncCheckpointContextRef context use after free");
	}
	else
	{
		OSAtomicIncrement(&psContextInt->hRefCount);
	}
}

/*
	Internal interfaces for management of synchronisation block memory
 */
static PVRSRV_ERROR
_AllocSyncCheckpointBlock(_SYNC_CHECKPOINT_CONTEXT *psContext,
                          SYNC_CHECKPOINT_BLOCK    **ppsSyncBlock)
{
	PVRSRV_DEVICE_NODE *psDevNode;
	SYNC_CHECKPOINT_BLOCK *psSyncBlk;
	PVRSRV_ERROR eError;

	psSyncBlk = OSAllocMem(sizeof(*psSyncBlk));
	PVR_LOG_GOTO_IF_NOMEM(psSyncBlk, eError, fail_alloc);

	psSyncBlk->psContext = psContext;

	/* Allocate sync checkpoint block */
	psDevNode = psContext->psDevNode;
	PVR_LOG_GOTO_IF_INVALID_PARAM(psDevNode, eError, fail_alloc_ufo_block);

	psSyncBlk->psDevNode = psDevNode;

	eError = psDevNode->pfnAllocUFOBlock(psDevNode,
	                                     &psSyncBlk->hMemDesc,
	                                     &psSyncBlk->ui32FirmwareAddr,
	                                     &psSyncBlk->ui32SyncBlockSize);
	PVR_LOG_GOTO_IF_ERROR(eError, "pfnAllocUFOBlock", fail_alloc_ufo_block);

	eError = DevmemAcquireCpuVirtAddr(psSyncBlk->hMemDesc,
	                                  (void **) &psSyncBlk->pui32LinAddr);
	PVR_LOG_GOTO_IF_ERROR(eError, "DevmemAcquireCpuVirtAddr", fail_devmem_acquire);

	OSAtomicWrite(&psSyncBlk->hRefCount, 1);

	OSLockCreate(&psSyncBlk->hLock);

	PDUMPCOMMENTWITHFLAGS(psDevNode, PDUMP_FLAGS_CONTINUOUS,
	                      "Allocated Sync Checkpoint UFO block (FirmwareVAddr = 0x%08x)",
	                      psSyncBlk->ui32FirmwareAddr);
#if defined(PDUMP)
	OSLockAcquire(psContext->hSyncCheckpointBlockListLock);
	dllist_add_to_tail(&psContext->sSyncCheckpointBlockListHead, &psSyncBlk->sListNode);
	OSLockRelease(psContext->hSyncCheckpointBlockListLock);
#endif

	*ppsSyncBlock = psSyncBlk;
	return PVRSRV_OK;

fail_devmem_acquire:
	psDevNode->pfnFreeUFOBlock(psDevNode, psSyncBlk->hMemDesc);
fail_alloc_ufo_block:
	OSFreeMem(psSyncBlk);
fail_alloc:
	return eError;
}

static void
_FreeSyncCheckpointBlock(SYNC_CHECKPOINT_BLOCK *psSyncBlk)
{
	OSLockAcquire(psSyncBlk->hLock);
	if (0 == OSAtomicDecrement(&psSyncBlk->hRefCount))
	{
		PVRSRV_DEVICE_NODE *psDevNode = psSyncBlk->psDevNode;

#if defined(PDUMP)
		OSLockAcquire(psSyncBlk->psContext->hSyncCheckpointBlockListLock);
		dllist_remove_node(&psSyncBlk->sListNode);
		OSLockRelease(psSyncBlk->psContext->hSyncCheckpointBlockListLock);
#endif
		DevmemReleaseCpuVirtAddr(psSyncBlk->hMemDesc);
		psDevNode->pfnFreeUFOBlock(psDevNode, psSyncBlk->hMemDesc);
		OSLockRelease(psSyncBlk->hLock);
		OSLockDestroy(psSyncBlk->hLock);
		psSyncBlk->hLock = NULL;
		OSFreeMem(psSyncBlk);
	}
	else
	{
		OSLockRelease(psSyncBlk->hLock);
	}
}

static PVRSRV_ERROR
_SyncCheckpointBlockImport(RA_PERARENA_HANDLE hArena,
                           RA_LENGTH_T uSize,
                           RA_FLAGS_T uFlags,
                           const IMG_CHAR *pszAnnotation,
                           RA_BASE_T *puiBase,
                           RA_LENGTH_T *puiActualSize,
                           RA_PERISPAN_HANDLE *phImport)
{
	_SYNC_CHECKPOINT_CONTEXT *psContext = hArena;
	SYNC_CHECKPOINT_BLOCK *psSyncBlock = NULL;
	RA_LENGTH_T uiSpanSize;
	PVRSRV_ERROR eError;
	PVR_UNREFERENCED_PARAMETER(uFlags);

	PVR_LOG_RETURN_IF_INVALID_PARAM((hArena != NULL), "hArena");

	/* Check we've not be called with an unexpected size */
	PVR_LOG_RETURN_IF_INVALID_PARAM((uSize == sizeof(SYNC_CHECKPOINT_FW_OBJ)), "uSize");

	/*
		Ensure the sync checkpoint context doesn't go away while we have
		sync blocks attached to it.
	 */
	SyncCheckpointContextRef((PSYNC_CHECKPOINT_CONTEXT)psContext);

	/* Allocate the block of memory */
	eError = _AllocSyncCheckpointBlock(psContext, &psSyncBlock);
	PVR_GOTO_IF_ERROR(eError, fail_syncblockalloc);

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
	PVR_LOG_IF_FALSE((uiSpanSize == psSyncBlock->ui32SyncBlockSize),
	                 "uiSpanSize invalid");

	*puiBase = psSyncBlock->uiSpanBase;
	*puiActualSize = psSyncBlock->ui32SyncBlockSize;
	*phImport = psSyncBlock;
	return PVRSRV_OK;

fail_spanalloc:
	_FreeSyncCheckpointBlock(psSyncBlock);
fail_syncblockalloc:
	SyncCheckpointContextUnref((PSYNC_CHECKPOINT_CONTEXT)psContext);

	return eError;
}

static void
_SyncCheckpointBlockUnimport(RA_PERARENA_HANDLE hArena,
                             RA_BASE_T uiBase,
                             RA_PERISPAN_HANDLE hImport)
{
	_SYNC_CHECKPOINT_CONTEXT *psContext = hArena;
	SYNC_CHECKPOINT_BLOCK   *psSyncBlock = hImport;

	PVR_LOG_RETURN_VOID_IF_FALSE((psContext != NULL), "hArena invalid");
	PVR_LOG_RETURN_VOID_IF_FALSE((psSyncBlock != NULL), "hImport invalid");
	PVR_LOG_RETURN_VOID_IF_FALSE((uiBase == psSyncBlock->uiSpanBase), "uiBase invalid");

	/* Free the span this import is using */
	RA_Free(psContext->psSpanRA, uiBase);

	/* Free the sync checkpoint block */
	_FreeSyncCheckpointBlock(psSyncBlock);

	/*	Drop our reference to the sync checkpoint context */
	SyncCheckpointContextUnref((PSYNC_CHECKPOINT_CONTEXT)psContext);
}

static INLINE IMG_UINT32 _SyncCheckpointGetOffset(SYNC_CHECKPOINT *psSyncInt)
{
	IMG_UINT64 ui64Temp;

	ui64Temp = psSyncInt->uiSpanAddr - psSyncInt->psSyncCheckpointBlock->uiSpanBase;
	PVR_ASSERT(ui64Temp<IMG_UINT32_MAX);
	return (IMG_UINT32)ui64Temp;
}

/* Used by SyncCheckpointContextCreate() below */
static INLINE IMG_UINT32 _Log2(IMG_UINT32 ui32Align)
{
	PVR_ASSERT(IsPower2(ui32Align));
	return ExactLog2(ui32Align);
}

/*
	External interfaces
 */

PVRSRV_ERROR
SyncCheckpointRegisterFunctions(PFN_SYNC_CHECKPOINT_STRUCT *psSyncCheckpointPfns)
{
	PVR_ASSERT(psSyncCheckpointPfns != NULL);

	if (g_psSyncCheckpointPfnStruct)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s called but already initialised", __func__));
		return PVRSRV_ERROR_INIT_FAILURE;
	}

	g_psSyncCheckpointPfnStruct = psSyncCheckpointPfns;

	return PVRSRV_OK;
}

PVRSRV_ERROR
SyncCheckpointResolveFence(PSYNC_CHECKPOINT_CONTEXT psSyncCheckpointContext,
                           PVRSRV_FENCE hFence, IMG_UINT32 *pui32NumSyncCheckpoints,
                           PSYNC_CHECKPOINT **papsSyncCheckpoints,
                           IMG_UINT64 *pui64FenceUID,
                           PDUMP_FLAGS_T ui32PDumpFlags)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 i;
#if defined(PDUMP)
	SYNC_CHECKPOINT *psSyncCheckpoint = NULL;
#endif

	if (unlikely(!g_psSyncCheckpointPfnStruct || !g_psSyncCheckpointPfnStruct->pfnFenceResolve))
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: ERROR (eError=PVRSRV_ERROR_SYNC_NATIVESYNC_NOT_REGISTERED)",
				__func__));
		eError = PVRSRV_ERROR_SYNC_NATIVESYNC_NOT_REGISTERED;
		PVR_LOG_ERROR(eError, "g_pfnFenceResolve is NULL");
		return eError;
	}

	if (papsSyncCheckpoints)
	{
		eError = g_psSyncCheckpointPfnStruct->pfnFenceResolve(
		                           psSyncCheckpointContext,
		                           hFence,
		                           pui32NumSyncCheckpoints,
		                           papsSyncCheckpoints,
		                           pui64FenceUID);
	}
	else
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
	}

	PVR_LOG_RETURN_IF_ERROR(eError, "g_psSyncCheckpointPfnStruct->pfnFenceResolve");

#if defined(PDUMP)
	if (*papsSyncCheckpoints)
	{
		for (i = 0; i < *pui32NumSyncCheckpoints; i++)
		{
			psSyncCheckpoint = (SYNC_CHECKPOINT *)(*papsSyncCheckpoints)[i];
			psSyncCheckpoint->ui32PDumpFlags = ui32PDumpFlags;
		}
	}
#endif

	if (*pui32NumSyncCheckpoints > MAX_SYNC_CHECKPOINTS_PER_FENCE)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: g_psSyncCheckpointPfnStruct->pfnFenceResolve() returned too many checkpoints (%u > MAX_SYNC_CHECKPOINTS_PER_FENCE=%u)",
				__func__, *pui32NumSyncCheckpoints, MAX_SYNC_CHECKPOINTS_PER_FENCE));

		/* Free resources after error */
		if (*papsSyncCheckpoints)
		{
			for (i = 0; i < *pui32NumSyncCheckpoints; i++)
			{
				SyncCheckpointDropRef((*papsSyncCheckpoints)[i]);
			}

			SyncCheckpointFreeCheckpointListMem(*papsSyncCheckpoints);
		}

		return PVRSRV_ERROR_INVALID_PARAMS;
	}

#if (ENABLE_SYNC_CHECKPOINT_FENCE_DEBUG == 1)
	{
		IMG_UINT32 ii;

		PVR_DPF((PVR_DBG_WARNING,
				"%s: g_psSyncCheckpointPfnStruct->pfnFenceResolve() for fence %d returned the following %d checkpoints:",
				__func__,
				hFence,
				*pui32NumSyncCheckpoints));

		for (ii=0; ii<*pui32NumSyncCheckpoints; ii++)
		{
			PSYNC_CHECKPOINT psNextCheckpoint = *(*papsSyncCheckpoints +  ii);
			PVR_DPF((PVR_DBG_WARNING,
					"%s:   *papsSyncCheckpoints[%d]:<%p>",
					__func__,
					ii,
					(void*)psNextCheckpoint));
		}
	}
#endif

	return eError;
}

PVRSRV_ERROR
SyncCheckpointCreateFence(PVRSRV_DEVICE_NODE *psDevNode,
                          const IMG_CHAR *pszFenceName,
                          PVRSRV_TIMELINE hTimeline,
                          PSYNC_CHECKPOINT_CONTEXT psSyncCheckpointContext,
                          PVRSRV_FENCE *phNewFence,
                          IMG_UINT64 *puiUpdateFenceUID,
                          void **ppvFenceFinaliseData,
                          PSYNC_CHECKPOINT *psNewSyncCheckpoint,
                          void **ppvTimelineUpdateSyncPrim,
                          IMG_UINT32 *pui32TimelineUpdateValue,
                          PDUMP_FLAGS_T ui32PDumpFlags)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_UNREFERENCED_PARAMETER(psDevNode);

	if (unlikely(!g_psSyncCheckpointPfnStruct || !g_psSyncCheckpointPfnStruct->pfnFenceCreate))
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: ERROR (eError=PVRSRV_ERROR_SYNC_NATIVESYNC_NOT_REGISTERED)",
				__func__));
		eError = PVRSRV_ERROR_SYNC_NATIVESYNC_NOT_REGISTERED;
		PVR_LOG_ERROR(eError, "g_psSyncCheckpointPfnStruct->pfnFenceCreate is NULL");
	}
	else
	{
		eError = g_psSyncCheckpointPfnStruct->pfnFenceCreate(
		                          psDevNode,
		                          pszFenceName,
		                          hTimeline,
		                          psSyncCheckpointContext,
		                          phNewFence,
		                          puiUpdateFenceUID,
		                          ppvFenceFinaliseData,
		                          psNewSyncCheckpoint,
		                          ppvTimelineUpdateSyncPrim,
		                          pui32TimelineUpdateValue);
		if (unlikely(eError != PVRSRV_OK))
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s failed to create new fence<%p> for timeline<%d> using "
					"sync checkpoint context<%p>, psNewSyncCheckpoint=<%p>, eError=%s",
					__func__,
					(void*)phNewFence,
					hTimeline,
					(void*)psSyncCheckpointContext,
					(void*)psNewSyncCheckpoint,
					PVRSRVGetErrorString(eError)));
		}
#if (ENABLE_SYNC_CHECKPOINT_FENCE_DEBUG == 1)
		else
		{
			PVR_DPF((PVR_DBG_WARNING,
					"%s created new fence<%d> for timeline<%d> using "
					"sync checkpoint context<%p>, new sync_checkpoint=<%p>",
					__func__,
					*phNewFence,
					hTimeline,
					(void*)psSyncCheckpointContext,
					(void*)*psNewSyncCheckpoint));
		}
#endif

#if defined(PDUMP)
		if (eError == PVRSRV_OK)
		{
			SYNC_CHECKPOINT *psSyncCheckpoint = (SYNC_CHECKPOINT*)(*psNewSyncCheckpoint);
			if (psSyncCheckpoint)
			{
				psSyncCheckpoint->ui32PDumpFlags = ui32PDumpFlags;
			}
		}
#endif
	}
	return eError;
}

PVRSRV_ERROR
SyncCheckpointRollbackFenceData(PVRSRV_FENCE hFence, void *pvFinaliseData)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (!g_psSyncCheckpointPfnStruct || !g_psSyncCheckpointPfnStruct->pfnFenceDataRollback)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: ERROR (eError=PVRSRV_ERROR_SYNC_NATIVESYNC_NOT_REGISTERED)",
				__func__));
		eError = PVRSRV_ERROR_SYNC_NATIVESYNC_NOT_REGISTERED;
		PVR_LOG_ERROR(eError, "g_psSyncCheckpointPfnStruct->pfnFenceDataRollback is NULL");
	}
	else
	{
#if (ENABLE_SYNC_CHECKPOINT_FENCE_DEBUG == 1)
		PVR_DPF((PVR_DBG_WARNING,
				"%s: called to rollback fence data <%p>",
				__func__,
				pvFinaliseData));
#endif
		eError = g_psSyncCheckpointPfnStruct->pfnFenceDataRollback(
		            hFence, pvFinaliseData);
		PVR_LOG_IF_ERROR(eError,
		                 "g_psSyncCheckpointPfnStruct->pfnFenceDataRollback returned error");
	}
	return eError;
}

PVRSRV_ERROR
SyncCheckpointFinaliseFence(PPVRSRV_DEVICE_NODE psDevNode,
                            PVRSRV_FENCE hFence,
                            void *pvFinaliseData,
                            PSYNC_CHECKPOINT psSyncCheckpoint,
                            const IMG_CHAR *pszName)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (!g_psSyncCheckpointPfnStruct || !g_psSyncCheckpointPfnStruct->pfnFenceFinalise)
	{
		PVR_DPF((PVR_DBG_WARNING,
				"%s: Warning (eError=PVRSRV_ERROR_SYNC_NATIVESYNC_NOT_REGISTERED) (this is permitted)",
				__func__));
		eError = PVRSRV_ERROR_SYNC_NATIVESYNC_NOT_REGISTERED;
	}
	else
	{
#if (ENABLE_SYNC_CHECKPOINT_FENCE_DEBUG == 1)
		PVR_DPF((PVR_DBG_WARNING,
				"%s: called to finalise fence <%d>",
				__func__,
				hFence));
#endif
		eError = g_psSyncCheckpointPfnStruct->pfnFenceFinalise(hFence, pvFinaliseData);
		PVR_LOG_IF_ERROR(eError, "g_psSyncCheckpointPfnStruct->pfnFenceFinalise returned error");

		RGXSRV_HWPERF_ALLOC_FENCE(psDevNode, OSGetCurrentClientProcessIDKM(), hFence,
		                          SyncCheckpointGetFirmwareAddr(psSyncCheckpoint),
		                          pszName, OSStringLength(pszName));
	}
	return eError;
}

void
SyncCheckpointFreeCheckpointListMem(void *pvCheckpointListMem)
{
	if (g_psSyncCheckpointPfnStruct->pfnFreeCheckpointListMem)
	{
		g_psSyncCheckpointPfnStruct->pfnFreeCheckpointListMem(pvCheckpointListMem);
	}
}

PVRSRV_ERROR
SyncCheckpointNoHWUpdateTimelines(void *pvPrivateData)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (!g_psSyncCheckpointPfnStruct || !g_psSyncCheckpointPfnStruct->pfnNoHWUpdateTimelines)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: ERROR (eError=PVRSRV_ERROR_SYNC_NATIVESYNC_NOT_REGISTERED)",
				__func__));
		eError = PVRSRV_ERROR_SYNC_NATIVESYNC_NOT_REGISTERED;
		PVR_LOG_ERROR(eError, "g_psSyncCheckpointPfnStruct->pfnNoHWUpdateTimelines is NULL");
	}
	else
	{
		g_psSyncCheckpointPfnStruct->pfnNoHWUpdateTimelines(pvPrivateData);
	}
	return eError;

}

PVRSRV_ERROR
SyncCheckpointDumpInfoOnStalledUFOs(IMG_UINT32 ui32NumUFOs, IMG_UINT32 *pui32Vaddrs, IMG_UINT32 *pui32NumSyncOwnedUFOs)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_LOG_RETURN_IF_FALSE((pui32NumSyncOwnedUFOs != NULL), "pui32NumSyncOwnedUFOs invalid", PVRSRV_ERROR_INVALID_PARAMS);

	if (!g_psSyncCheckpointPfnStruct || !g_psSyncCheckpointPfnStruct->pfnDumpInfoOnStalledUFOs)
	{
		*pui32NumSyncOwnedUFOs = 0;
		PVR_DPF((PVR_DBG_ERROR,
				"%s: ERROR (eError=PVRSRV_ERROR_SYNC_NATIVESYNC_NOT_REGISTERED)",
				__func__));
		eError = PVRSRV_ERROR_SYNC_NATIVESYNC_NOT_REGISTERED;
		PVR_LOG_ERROR(eError, "g_psSyncCheckpointPfnStruct->pfnDumpInfoOnStalledUFOs is NULL");
	}
	else
	{
		*pui32NumSyncOwnedUFOs = g_psSyncCheckpointPfnStruct->pfnDumpInfoOnStalledUFOs(ui32NumUFOs, pui32Vaddrs);
		PVR_LOG(("%d sync checkpoint%s owned by %s in stalled context",
		         *pui32NumSyncOwnedUFOs, *pui32NumSyncOwnedUFOs==1 ? "" : "s",
		         g_psSyncCheckpointPfnStruct->pszImplName));
	}
	return eError;
}

PVRSRV_ERROR
SyncCheckpointContextCreate(PPVRSRV_DEVICE_NODE psDevNode,
                            PSYNC_CHECKPOINT_CONTEXT *ppsSyncCheckpointContext)
{
	_SYNC_CHECKPOINT_CONTEXT *psContext = NULL;
	_SYNC_CHECKPOINT_CONTEXT_CTL *psContextCtl = NULL;
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_LOG_RETURN_IF_FALSE((ppsSyncCheckpointContext != NULL),
	                  "ppsSyncCheckpointContext invalid",
	                  PVRSRV_ERROR_INVALID_PARAMS);

	psContext = OSAllocMem(sizeof(*psContext));
	PVR_LOG_GOTO_IF_NOMEM(psContext, eError, fail_alloc); /* Sets OOM error code */

	psContextCtl = OSAllocMem(sizeof(*psContextCtl));
	PVR_LOG_GOTO_IF_NOMEM(psContextCtl, eError, fail_alloc2); /* Sets OOM error code */

	eError = OSLockCreate(&psContext->hLock);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSLockCreate:1", fail_create_context_lock);

	eError = OSSpinLockCreate(&psContextCtl->hDeferredCleanupListLock);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSSpinLockCreate:1", fail_create_deferred_cleanup_lock);

#if (SYNC_CHECKPOINT_POOL_SIZE > 0)
	eError = OSSpinLockCreate(&psContextCtl->hSyncCheckpointPoolLock);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSSpinLockCreate:2", fail_create_pool_lock);
#endif

	dllist_init(&psContextCtl->sDeferredCleanupListHead);
#if (SYNC_CHECKPOINT_POOL_SIZE > 0)
	psContextCtl->ui32SyncCheckpointPoolCount = 0;
	psContextCtl->ui32SyncCheckpointPoolWp = 0;
	psContextCtl->ui32SyncCheckpointPoolRp = 0;
	psContextCtl->bSyncCheckpointPoolFull = IMG_FALSE;
	psContextCtl->bSyncCheckpointPoolValid = IMG_TRUE;
#endif
	psContext->psDevNode = psDevNode;

	OSSNPrintf(psContext->azName, PVRSRV_SYNC_NAME_LENGTH, "Sync Prim RA-%p", psContext);
	OSSNPrintf(psContext->azSpanName, PVRSRV_SYNC_NAME_LENGTH, "Sync Prim span RA-%p", psContext);

	/*
		Create the RA for sub-allocations of the sync checkpoints

		Note:
		The import size doesn't matter here as the server will pass
		back the blocksize when it does the import which overrides
		what we specify here.
	 */
	psContext->psSubAllocRA = RA_Create(psContext->azName,
	                                    /* Params for imports */
	                                    _Log2(sizeof(IMG_UINT32)),
	                                    RA_LOCKCLASS_2,
	                                    _SyncCheckpointBlockImport,
	                                    _SyncCheckpointBlockUnimport,
	                                    psContext,
	                                    RA_POLICY_DEFAULT);
	PVR_LOG_GOTO_IF_NOMEM(psContext->psSubAllocRA, eError, fail_suballoc);

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
	PVR_LOG_GOTO_IF_NOMEM(psContext->psSpanRA, eError, fail_span);

	if (!RA_Add(psContext->psSpanRA, 0, MAX_SYNC_CHECKPOINT_MEM, 0, NULL))
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		PVR_LOG_ERROR(eError, "SyncCheckpointContextCreate call to RA_Add(span) failed");
		goto fail_span_add;
	}

	OSAtomicWrite(&psContext->hRefCount, 1);
	OSAtomicWrite(&psContext->hCheckpointCount, 0);

	psContext->psContextCtl = psContextCtl;

	*ppsSyncCheckpointContext = (PSYNC_CHECKPOINT_CONTEXT)psContext;
#if (ENABLE_SYNC_CHECKPOINT_CONTEXT_DEBUG == 1)
	PVR_DPF((PVR_DBG_WARNING,
			"%s: created psSyncCheckpointContext=<%p> (%d contexts exist)",
			__func__,
			(void*)*ppsSyncCheckpointContext,
			++gui32NumSyncCheckpointContexts));
#endif

#if defined(PDUMP)
	dllist_init(&psContext->sSyncCheckpointBlockListHead);

	eError = OSLockCreate(&psContext->hSyncCheckpointBlockListLock);
	PVR_GOTO_IF_ERROR(eError, fail_span_add);

	OSLockAcquire(psDevNode->hSyncCheckpointContextListLock);
	dllist_add_to_tail(&psDevNode->sSyncCheckpointContextListHead, &psContext->sListNode);
	OSLockRelease(psDevNode->hSyncCheckpointContextListLock);

#endif

	return PVRSRV_OK;

fail_span_add:
	RA_Delete(psContext->psSpanRA);
fail_span:
	RA_Delete(psContext->psSubAllocRA);
fail_suballoc:
#if (SYNC_CHECKPOINT_POOL_SIZE > 0)
	OSSpinLockDestroy(psContextCtl->hSyncCheckpointPoolLock);
	psContextCtl->hSyncCheckpointPoolLock = NULL;
fail_create_pool_lock:
#endif
	OSSpinLockDestroy(psContextCtl->hDeferredCleanupListLock);
	psContextCtl->hDeferredCleanupListLock = NULL;
fail_create_deferred_cleanup_lock:
	OSLockDestroy(psContext->hLock);
	psContext->hLock = NULL;
fail_create_context_lock:
	OSFreeMem(psContextCtl);
fail_alloc2:
	OSFreeMem(psContext);
fail_alloc:
	return eError;
}

/* Poisons and frees the checkpoint
 * Decrements context refcount. */
static void _FreeSyncCheckpoint(SYNC_CHECKPOINT *psSyncCheckpoint)
{
	_SYNC_CHECKPOINT_CONTEXT *psContext = psSyncCheckpoint->psSyncCheckpointBlock->psContext;

	psSyncCheckpoint->sCheckpointUFOAddr.ui32Addr = 0;
	psSyncCheckpoint->psSyncCheckpointFwObj = NULL;
	psSyncCheckpoint->ui32ValidationCheck = SYNC_CHECKPOINT_PATTERN_FREED;

	RA_Free(psSyncCheckpoint->psSyncCheckpointBlock->psContext->psSubAllocRA,
	        psSyncCheckpoint->uiSpanAddr);
	psSyncCheckpoint->psSyncCheckpointBlock = NULL;

	OSFreeMem(psSyncCheckpoint);

	OSAtomicDecrement(&psContext->hCheckpointCount);
}

PVRSRV_ERROR SyncCheckpointContextDestroy(PSYNC_CHECKPOINT_CONTEXT psSyncCheckpointContext)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	_SYNC_CHECKPOINT_CONTEXT *psContext = (_SYNC_CHECKPOINT_CONTEXT*)psSyncCheckpointContext;
	PVRSRV_DEVICE_NODE *psDevNode;
	IMG_INT iRf = 0;

	PVR_LOG_RETURN_IF_FALSE((psSyncCheckpointContext != NULL),
	                  "psSyncCheckpointContext invalid",
	                  PVRSRV_ERROR_INVALID_PARAMS);

	psDevNode = (PVRSRV_DEVICE_NODE *)psContext->psDevNode;

#if (ENABLE_SYNC_CHECKPOINT_CONTEXT_DEBUG == 1)
	PVR_DPF((PVR_DBG_WARNING,
			"%s: destroying psSyncCheckpointContext=<%p> (now have %d contexts)",
			__func__,
			(void*)psSyncCheckpointContext,
			--gui32NumSyncCheckpointContexts));
#endif

	_CheckDeferredCleanupList(psContext);

#if (SYNC_CHECKPOINT_POOL_SIZE > 0)
	if (psContext->psContextCtl->ui32SyncCheckpointPoolCount > 0)
	{
		IMG_UINT32 ui32NumFreedFromPool = _CleanCheckpointPool(psContext);

#if (ENABLE_SYNC_CHECKPOINT_POOL_DEBUG == 1)
		PVR_DPF((PVR_DBG_WARNING,
				"%s freed %d sync checkpoints that were still in the pool for context<%p>",
				__func__,
				ui32NumFreedFromPool,
				(void*)psContext));
#else
		PVR_UNREFERENCED_PARAMETER(ui32NumFreedFromPool);
#endif
	}
#endif

	iRf = OSAtomicRead(&psContext->hCheckpointCount);

	if (iRf != 0)
	{
		OS_SPINLOCK_FLAGS uiFlags;

		/* Note, this is not a permanent error as the caller may retry later */
		PVR_DPF((PVR_DBG_WARNING,
				"%s <%p> attempted with active references (iRf=%d), "
				"may be the result of a race",
				__func__,
				(void*)psContext,
				iRf));

		eError = PVRSRV_ERROR_UNABLE_TO_DESTROY_CONTEXT;

		OSSpinLockAcquire(psDevNode->hSyncCheckpointListLock, uiFlags);
		{
			DLLIST_NODE *psNode, *psNext;

			dllist_foreach_node(&psDevNode->sSyncCheckpointSyncsList, psNode, psNext)
			{
				SYNC_CHECKPOINT *psSyncCheckpoint = IMG_CONTAINER_OF(psNode, SYNC_CHECKPOINT, sListNode);
				bool bDeferredFree = dllist_node_is_in_list(&psSyncCheckpoint->sDeferredFreeListNode);

				/* Line below avoids build error in release builds (where PVR_DPF is not defined) */
				PVR_UNREFERENCED_PARAMETER(bDeferredFree);
				PVR_DPF((PVR_DBG_WARNING,
						"%s syncCheckpoint<%p> ID=%d, %s, refs=%d, state=%s, fwaddr=%#08x, enqCount:%d, FWCount:%d %s",
						__func__,
						(void*)psSyncCheckpoint,
						psSyncCheckpoint->ui32UID,
						psSyncCheckpoint->azName,
						OSAtomicRead(&psSyncCheckpoint->hRefCount),
						psSyncCheckpoint->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_SIGNALLED ?
								"PVRSRV_SYNC_CHECKPOINT_SIGNALLED" :
								psSyncCheckpoint->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_ACTIVE ?
										"PVRSRV_SYNC_CHECKPOINT_ACTIVE" : "PVRSRV_SYNC_CHECKPOINT_ERRORED",
						psSyncCheckpoint->ui32FWAddr,
						OSAtomicRead(&psSyncCheckpoint->hEnqueuedCCBCount),
						psSyncCheckpoint->psSyncCheckpointFwObj->ui32FwRefCount,
						bDeferredFree ? "(deferred free)" : ""));

#if (ENABLE_SYNC_CHECKPOINT_CONTEXT_DEBUG == 1)
				gui32NumSyncCheckpointContexts++;
#endif
			}
		}
		OSSpinLockRelease(psDevNode->hSyncCheckpointListLock, uiFlags);
	}
	else
	{
		IMG_INT iRf2 = 0;

		iRf2 = OSAtomicRead(&psContext->hRefCount);
		SyncCheckpointContextUnref(psSyncCheckpointContext);
	}

#if defined(PDUMP)
	if (dllist_is_empty(&psContext->sSyncCheckpointBlockListHead))
	{
		OSLockDestroy(psContext->hSyncCheckpointBlockListLock);
		psContext->hSyncCheckpointBlockListLock = NULL;

		OSLockAcquire(psDevNode->hSyncCheckpointContextListLock);
		dllist_remove_node(&psContext->sListNode);
		OSLockRelease(psDevNode->hSyncCheckpointContextListLock);
	}
#endif

	return eError;
}

PVRSRV_ERROR
SyncCheckpointAlloc(PSYNC_CHECKPOINT_CONTEXT psSyncContext,
                    PVRSRV_TIMELINE hTimeline,
                    PVRSRV_FENCE hFence,
                    const IMG_CHAR *pszCheckpointName,
                    PSYNC_CHECKPOINT *ppsSyncCheckpoint)
{
	SYNC_CHECKPOINT *psNewSyncCheckpoint = NULL;
	_SYNC_CHECKPOINT_CONTEXT *psSyncContextInt = (_SYNC_CHECKPOINT_CONTEXT*)psSyncContext;
	PVRSRV_DEVICE_NODE *psDevNode;
	PVRSRV_ERROR eError;

	PVR_LOG_RETURN_IF_FALSE((psSyncContext != NULL), "psSyncContext invalid", PVRSRV_ERROR_INVALID_PARAMS);
	PVR_LOG_RETURN_IF_FALSE((ppsSyncCheckpoint != NULL), "ppsSyncCheckpoint invalid", PVRSRV_ERROR_INVALID_PARAMS);

	psDevNode = (PVRSRV_DEVICE_NODE *)psSyncContextInt->psDevNode;

#if (SYNC_CHECKPOINT_POOL_SIZE > 0)
#if ((ENABLE_SYNC_CHECKPOINT_POOL_DEBUG == 1) || (ENABLE_SYNC_CHECKPOINT_ALLOC_AND_FREE_DEBUG == 1))
	PVR_DPF((PVR_DBG_WARNING, "%s Entry, Getting checkpoint from pool",
			 __func__));
#endif
	psNewSyncCheckpoint = _GetCheckpointFromPool(psSyncContextInt);
	if (!psNewSyncCheckpoint)
	{
#if ((ENABLE_SYNC_CHECKPOINT_POOL_DEBUG == 1) || (ENABLE_SYNC_CHECKPOINT_ALLOC_AND_FREE_DEBUG == 1))
		PVR_DPF((PVR_DBG_WARNING,
				 "%s     checkpoint pool empty - will have to allocate",
				 __func__));
#endif
	}
#endif
	/* If pool is empty (or not defined) alloc the new sync checkpoint */
	if (!psNewSyncCheckpoint)
	{
		psNewSyncCheckpoint = OSAllocMem(sizeof(*psNewSyncCheckpoint));
		PVR_LOG_GOTO_IF_NOMEM(psNewSyncCheckpoint, eError, fail_alloc); /* Sets OOM error code */

		eError = RA_Alloc(psSyncContextInt->psSubAllocRA,
		                  sizeof(*psNewSyncCheckpoint->psSyncCheckpointFwObj),
		                  RA_NO_IMPORT_MULTIPLIER,
		                  0,
		                  sizeof(IMG_UINT32),
		                  (IMG_CHAR*)pszCheckpointName,
		                  &psNewSyncCheckpoint->uiSpanAddr,
		                  NULL,
		                  (RA_PERISPAN_HANDLE *) &psNewSyncCheckpoint->psSyncCheckpointBlock);
		PVR_LOG_GOTO_IF_ERROR(eError, "RA_Alloc", fail_raalloc);

#if (ENABLE_SYNC_CHECKPOINT_ALLOC_AND_FREE_DEBUG == 1)
		PVR_DPF((PVR_DBG_WARNING,
				"%s CALLED RA_Alloc(), psSubAllocRA=<%p>, ui32SpanAddr=0x%llx",
				__func__,
				(void*)psSyncContextInt->psSubAllocRA,
				psNewSyncCheckpoint->uiSpanAddr));
#endif
		psNewSyncCheckpoint->psSyncCheckpointFwObj =
				(volatile SYNC_CHECKPOINT_FW_OBJ*)(void *)(psNewSyncCheckpoint->psSyncCheckpointBlock->pui32LinAddr +
						(_SyncCheckpointGetOffset(psNewSyncCheckpoint)/sizeof(IMG_UINT32)));
		psNewSyncCheckpoint->ui32FWAddr = psNewSyncCheckpoint->psSyncCheckpointBlock->ui32FirmwareAddr +
		                                  _SyncCheckpointGetOffset(psNewSyncCheckpoint) + 1;
		OSAtomicIncrement(&psNewSyncCheckpoint->psSyncCheckpointBlock->psContext->hCheckpointCount);
		psNewSyncCheckpoint->ui32ValidationCheck = SYNC_CHECKPOINT_PATTERN_IN_USE;
#if (ENABLE_SYNC_CHECKPOINT_ALLOC_AND_FREE_DEBUG == 1)
		PVR_DPF((PVR_DBG_WARNING,
				 "%s called to allocate new sync checkpoint<%p> for context<%p>",
				 __func__, (void*)psNewSyncCheckpoint, (void*)psSyncContext));
		PVR_DPF((PVR_DBG_WARNING,
				 "%s                    psSyncCheckpointFwObj<%p>",
				 __func__, (void*)psNewSyncCheckpoint->psSyncCheckpointFwObj));
		PVR_DPF((PVR_DBG_WARNING,
				 "%s                    psSyncCheckpoint FwAddr=0x%x",
				 __func__, SyncCheckpointGetFirmwareAddr((PSYNC_CHECKPOINT)psNewSyncCheckpoint)));
		PVR_DPF((PVR_DBG_WARNING,
				 "%s                    pszCheckpointName = %s",
				 __func__, pszCheckpointName));
		PVR_DPF((PVR_DBG_WARNING,
				 "%s                    psSyncCheckpoint Timeline=%d",
				 __func__, hTimeline));
#endif
	}

	psNewSyncCheckpoint->hTimeline = hTimeline;
	OSAtomicWrite(&psNewSyncCheckpoint->hRefCount, 1);
	OSAtomicWrite(&psNewSyncCheckpoint->hEnqueuedCCBCount, 0);
	psNewSyncCheckpoint->psSyncCheckpointFwObj->ui32FwRefCount = 0;
	psNewSyncCheckpoint->psSyncCheckpointFwObj->ui32State = PVRSRV_SYNC_CHECKPOINT_ACTIVE;
	psNewSyncCheckpoint->uiProcess = OSGetCurrentClientProcessIDKM();
	OSCachedMemSet(&psNewSyncCheckpoint->sDeferredFreeListNode, 0, sizeof(psNewSyncCheckpoint->sDeferredFreeListNode));

	if (pszCheckpointName)
	{
		/* Copy over the checkpoint name annotation */
		OSStringLCopy(psNewSyncCheckpoint->azName, pszCheckpointName, PVRSRV_SYNC_NAME_LENGTH);
	}
	else
	{
		/* No sync checkpoint name annotation */
		psNewSyncCheckpoint->azName[0] = '\0';
	}

	/* Store sync checkpoint FW address in PRGXFWIF_UFO_ADDR struct */
	psNewSyncCheckpoint->sCheckpointUFOAddr.ui32Addr = SyncCheckpointGetFirmwareAddr((PSYNC_CHECKPOINT)psNewSyncCheckpoint);

	/* Assign unique ID to this sync checkpoint */
	psNewSyncCheckpoint->ui32UID = g_SyncCheckpointUID++;

#if defined(PDUMP)
	/* Flushing deferred fence signals to pdump */
	MISRHandler_PdumpDeferredSyncSignalPoster(psDevNode);

	_SyncCheckpointAllocPDump(psDevNode, psNewSyncCheckpoint);
#endif

	RGXSRV_HWPERF_ALLOC_SYNC_CP(psDevNode, psNewSyncCheckpoint->hTimeline,
	                            OSGetCurrentClientProcessIDKM(),
	                            hFence,
	                            psNewSyncCheckpoint->ui32FWAddr,
	                            psNewSyncCheckpoint->azName,
	                            sizeof(psNewSyncCheckpoint->azName));

	if (GetInfoPageDebugFlagsKM() & DEBUG_FEATURE_FULL_SYNC_TRACKING_ENABLED)
	{
		IMG_CHAR szChkptName[PVRSRV_SYNC_NAME_LENGTH];

		if (pszCheckpointName)
		{
			/* Copy the checkpoint name annotation into a fixed-size array */
			OSStringLCopy(szChkptName, pszCheckpointName, PVRSRV_SYNC_NAME_LENGTH);
		}
		else
		{
			/* No checkpoint name annotation */
			szChkptName[0] = 0;
		}
		/* record this sync */
		eError = _SyncCheckpointRecordAdd(&psNewSyncCheckpoint->hRecord,
		                                 psNewSyncCheckpoint->psSyncCheckpointBlock,
		                                 psNewSyncCheckpoint->psSyncCheckpointBlock->ui32FirmwareAddr,
		                                 _SyncCheckpointGetOffset(psNewSyncCheckpoint),
		                                 psNewSyncCheckpoint->ui32UID,
		                                 OSStringNLength(szChkptName, PVRSRV_SYNC_NAME_LENGTH),
		                                 szChkptName, (PSYNC_CHECKPOINT)psNewSyncCheckpoint);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to add sync checkpoint record \"%s\" (%s)",
					__func__,
					szChkptName,
					PVRSRVGetErrorString(eError)));
			psNewSyncCheckpoint->hRecord = NULL;
			/* note the error but continue without affecting driver operation */
		}
	}

	{
		OS_SPINLOCK_FLAGS uiFlags;
		/* Add the sync checkpoint to the device list */
		OSSpinLockAcquire(psDevNode->hSyncCheckpointListLock, uiFlags);
		dllist_add_to_head(&psDevNode->sSyncCheckpointSyncsList,
		                   &psNewSyncCheckpoint->sListNode);
		OSSpinLockRelease(psDevNode->hSyncCheckpointListLock, uiFlags);
	}

	*ppsSyncCheckpoint = (PSYNC_CHECKPOINT)psNewSyncCheckpoint;

#if (ENABLE_SYNC_CHECKPOINT_ALLOC_AND_FREE_DEBUG == 1)
	PVR_DPF((PVR_DBG_WARNING,
			"%s Exit(Ok), psNewSyncCheckpoint->ui32UID=%d <%p>",
			__func__,
			psNewSyncCheckpoint->ui32UID,
			(void*)psNewSyncCheckpoint));
#endif
	return PVRSRV_OK;

fail_raalloc:
	OSFreeMem(psNewSyncCheckpoint);
fail_alloc:
	return eError;
}

static void SyncCheckpointUnref(SYNC_CHECKPOINT *psSyncCheckpointInt)
{
	_SYNC_CHECKPOINT_CONTEXT *psContext;
	PVRSRV_DEVICE_NODE *psDevNode;

	psContext = psSyncCheckpointInt->psSyncCheckpointBlock->psContext;
	psDevNode = (PVRSRV_DEVICE_NODE *)psContext->psDevNode;

	/*
	 * Without this reference, the context may be destroyed as soon
	 * as _FreeSyncCheckpoint is called, but the context is still
	 * needed when _CheckDeferredCleanupList is called at the end
	 * of this function.
	 */
	SyncCheckpointContextRef((PSYNC_CHECKPOINT_CONTEXT)psContext);

	PVR_ASSERT(psSyncCheckpointInt->ui32ValidationCheck == SYNC_CHECKPOINT_PATTERN_IN_USE);
	if (!OSAtomicRead(&psSyncCheckpointInt->hRefCount))
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncCheckpointUnref sync checkpoint already freed"));
	}
	else if (0 == OSAtomicDecrement(&psSyncCheckpointInt->hRefCount))
	{
		/* If the firmware has serviced all enqueued references to the sync checkpoint, free it */
		if (psSyncCheckpointInt->psSyncCheckpointFwObj->ui32FwRefCount ==
				(IMG_UINT32)(OSAtomicRead(&psSyncCheckpointInt->hEnqueuedCCBCount)))
		{
#if (ENABLE_SYNC_CHECKPOINT_ALLOC_AND_FREE_DEBUG == 1)
			PVR_DPF((PVR_DBG_WARNING,
					"%s No outstanding FW ops and hRef is zero, deleting SyncCheckpoint..",
					__func__));
#endif
			if ((GetInfoPageDebugFlagsKM() & DEBUG_FEATURE_FULL_SYNC_TRACKING_ENABLED)
				&& psSyncCheckpointInt->hRecord)
			{
				PVRSRV_ERROR eError;
				/* remove this sync record */
				eError = _SyncCheckpointRecordRemove(psSyncCheckpointInt->hRecord);
				PVR_LOG_IF_ERROR(eError, "_SyncCheckpointRecordRemove");
			}

			{
				OS_SPINLOCK_FLAGS uiFlags;
				/* Remove the sync checkpoint from the global list */
				OSSpinLockAcquire(psDevNode->hSyncCheckpointListLock, uiFlags);
				dllist_remove_node(&psSyncCheckpointInt->sListNode);
				OSSpinLockRelease(psDevNode->hSyncCheckpointListLock, uiFlags);
			}

			RGXSRV_HWPERF_FREE(psDevNode, SYNC_CP, psSyncCheckpointInt->ui32FWAddr);

#if (SYNC_CHECKPOINT_POOL_SIZE > 0)
#if ((ENABLE_SYNC_CHECKPOINT_POOL_DEBUG == 1) || (ENABLE_SYNC_CHECKPOINT_ALLOC_AND_FREE_DEBUG == 1))
			PVR_DPF((PVR_DBG_WARNING,
					"%s attempting to return sync checkpoint to the pool",
					__func__));
#endif
			if (!_PutCheckpointInPool(psSyncCheckpointInt))
#endif
			{
#if (SYNC_CHECKPOINT_POOL_SIZE > 0)
#if ((ENABLE_SYNC_CHECKPOINT_POOL_DEBUG == 1) || (ENABLE_SYNC_CHECKPOINT_ALLOC_AND_FREE_DEBUG == 1))
				PVR_DPF((PVR_DBG_WARNING,
						"%s pool is full, so just free it",
						__func__));
#endif
#endif
#if (ENABLE_SYNC_CHECKPOINT_ALLOC_AND_FREE_DEBUG == 1)
				PVR_DPF((PVR_DBG_WARNING,
						"%s CALLING RA_Free(psSyncCheckpoint(ID:%d)<%p>), psSubAllocRA=<%p>, ui32SpanAddr=0x%llx",
						__func__,
						psSyncCheckpointInt->ui32UID,
						(void*)psSyncCheckpointInt,
						(void*)psSyncCheckpointInt->psSyncCheckpointBlock->psContext->psSubAllocRA,
						psSyncCheckpointInt->uiSpanAddr));
#endif
				_FreeSyncCheckpoint(psSyncCheckpointInt);
			}
		}
		else
		{
			OS_SPINLOCK_FLAGS uiFlags;
#if ((ENABLE_SYNC_CHECKPOINT_DEFERRED_CLEANUP_DEBUG == 1) || (ENABLE_SYNC_CHECKPOINT_ALLOC_AND_FREE_DEBUG == 1))
			PVR_DPF((PVR_DBG_WARNING,
					"%s Outstanding FW ops hEnqueuedCCBCount=%d != FwObj->ui32FwRefCount=%d "
					"- DEFERRING CLEANUP psSyncCheckpoint(ID:%d)<%p>",
					__func__,
					OSAtomicRead(&psSyncCheckpointInt->hEnqueuedCCBCount),
					psSyncCheckpointInt->psSyncCheckpointFwObj->ui32FwRefCount,
					psSyncCheckpointInt->ui32UID,
					(void*)psSyncCheckpointInt));
#endif
			/* Add the sync checkpoint to the deferred free list */
			OSSpinLockAcquire(psContext->psContextCtl->hDeferredCleanupListLock, uiFlags);
			dllist_add_to_tail(&psContext->psContextCtl->sDeferredCleanupListHead,
			                   &psSyncCheckpointInt->sDeferredFreeListNode);
			OSSpinLockRelease(psContext->psContextCtl->hDeferredCleanupListLock, uiFlags);
		}
	}
	else
	{
#if (ENABLE_SYNC_CHECKPOINT_ALLOC_AND_FREE_DEBUG == 1)
		PVR_DPF((PVR_DBG_WARNING,
				"%s psSyncCheckpoint(ID:%d)<%p>, hRefCount decremented to %d",
				__func__,
				psSyncCheckpointInt->ui32UID,
				(void*)psSyncCheckpointInt,
				(IMG_UINT32)(OSAtomicRead(&psSyncCheckpointInt->hRefCount))));
#endif
	}

	/* See if any sync checkpoints in the deferred cleanup list can be freed */
	_CheckDeferredCleanupList(psContext);

	SyncCheckpointContextUnref((PSYNC_CHECKPOINT_CONTEXT)psContext);
}

void SyncCheckpointFree(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	SYNC_CHECKPOINT *psSyncCheckpointInt = (SYNC_CHECKPOINT*)psSyncCheckpoint;

	PVR_LOG_RETURN_VOID_IF_FALSE((psSyncCheckpoint != NULL), "psSyncCheckpoint invalid");

#if (ENABLE_SYNC_CHECKPOINT_ALLOC_AND_FREE_DEBUG == 1)
	PVR_DPF((PVR_DBG_WARNING,
			"%s Entry,  psSyncCheckpoint(ID:%d)<%p>, hRefCount=%d, psSyncCheckpoint->ui32ValidationCheck=0x%x",
			__func__,
			psSyncCheckpointInt->ui32UID,
			(void*)psSyncCheckpoint,
			(IMG_UINT32)(OSAtomicRead(&psSyncCheckpointInt->hRefCount)),
			psSyncCheckpointInt->ui32ValidationCheck));
#endif
	SyncCheckpointUnref(psSyncCheckpointInt);
}

void
SyncCheckpointSignal(PSYNC_CHECKPOINT psSyncCheckpoint, IMG_UINT32 ui32FenceSyncFlags)
{
	SYNC_CHECKPOINT *psSyncCheckpointInt = (SYNC_CHECKPOINT*)psSyncCheckpoint;

	PVR_LOG_IF_FALSE((psSyncCheckpoint != NULL), "psSyncCheckpoint invalid");

	if (psSyncCheckpointInt)
	{
		PVR_LOG_IF_FALSE((psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_ACTIVE),
		                 "psSyncCheckpoint already signalled");

		if (psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_ACTIVE)
		{
#if defined(SUPPORT_RGX)
			PVRSRV_RGXDEV_INFO *psDevInfo = psSyncCheckpointInt->psSyncCheckpointBlock->psDevNode->pvDevice;

			RGXSRVHWPerfSyncCheckpointUFOUpdate(psDevInfo, psSyncCheckpointInt, ui32FenceSyncFlags);
#endif
			psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State = PVRSRV_SYNC_CHECKPOINT_SIGNALLED;

#if defined(PDUMP)
			_SyncCheckpointUpdatePDump(psSyncCheckpointInt->psSyncCheckpointBlock->psDevNode, psSyncCheckpointInt, PVRSRV_SYNC_CHECKPOINT_SIGNALLED, ui32FenceSyncFlags);
#endif
		}
		else
		{
			PVR_DPF((PVR_DBG_WARNING,
					 "%s asked to set PVRSRV_SYNC_CHECKPOINT_SIGNALLED(%d) for (psSyncCheckpointInt->ui32UID=%d), "
					 "when value is already %d",
					 __func__,
					 PVRSRV_SYNC_CHECKPOINT_SIGNALLED,
					 psSyncCheckpointInt->ui32UID,
					 psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State));
		}
	}
}

void
SyncCheckpointSignalNoHW(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	SYNC_CHECKPOINT *psSyncCheckpointInt = (SYNC_CHECKPOINT*)psSyncCheckpoint;

	PVR_LOG_IF_FALSE((psSyncCheckpoint != NULL), "psSyncCheckpoint invalid");

	if (psSyncCheckpointInt)
	{
		PVR_LOG_IF_FALSE((psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_ACTIVE),
		                 "psSyncCheckpoint already signalled");

		if (psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_ACTIVE)
		{
#if defined(SUPPORT_RGX)
			PVRSRV_RGXDEV_INFO *psDevInfo = psSyncCheckpointInt->psSyncCheckpointBlock->psDevNode->pvDevice;

			RGXSRVHWPerfSyncCheckpointUFOUpdate(psDevInfo, psSyncCheckpointInt, PVRSRV_FENCE_FLAG_NONE);
#endif
			psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State = PVRSRV_SYNC_CHECKPOINT_SIGNALLED;
		}
		else
		{
#if (ENABLE_SYNC_CHECKPOINT_ENQ_AND_SIGNAL_DEBUG == 1)
			PVR_DPF((PVR_DBG_WARNING,
					"%s asked to set PVRSRV_SYNC_CHECKPOINT_SIGNALLED(%d) for (psSyncCheckpointInt->ui32UID=%d), "
					"when value is already %d",
					__func__,
					PVRSRV_SYNC_CHECKPOINT_SIGNALLED,
					psSyncCheckpointInt->ui32UID,
					psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State));
#endif
		}
	}
}

void
SyncCheckpointError(PSYNC_CHECKPOINT psSyncCheckpoint, IMG_UINT32 ui32FenceSyncFlags)
{
	SYNC_CHECKPOINT *psSyncCheckpointInt = (SYNC_CHECKPOINT*)psSyncCheckpoint;

	PVR_LOG_IF_FALSE((psSyncCheckpoint != NULL), "psSyncCheckpoint invalid");

	if (psSyncCheckpointInt)
	{
		PVR_LOG_IF_FALSE((psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_ACTIVE),
		                 "psSyncCheckpoint already signalled");

		if (psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_ACTIVE)
		{
#if defined(SUPPORT_RGX)
			PVRSRV_RGXDEV_INFO *psDevInfo = psSyncCheckpointInt->psSyncCheckpointBlock->psDevNode->pvDevice;
			if (!(ui32FenceSyncFlags & PVRSRV_FENCE_FLAG_SUPPRESS_HWP_PKT))
			{
				RGX_HWPERF_UFO_DATA_ELEMENT sSyncData;

				sSyncData.sUpdate.ui32FWAddr = SyncCheckpointGetFirmwareAddr(psSyncCheckpoint);
				sSyncData.sUpdate.ui32OldValue = psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State;
				sSyncData.sUpdate.ui32NewValue = PVRSRV_SYNC_CHECKPOINT_ERRORED;

				RGXSRV_HWPERF_UFO(psDevInfo, RGX_HWPERF_UFO_EV_UPDATE, &sSyncData,
				                  (ui32FenceSyncFlags & PVRSRV_FENCE_FLAG_CTX_ATOMIC) ? IMG_FALSE : IMG_TRUE);
			}
#endif

			psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State = PVRSRV_SYNC_CHECKPOINT_ERRORED;

#if defined(PDUMP)
			_SyncCheckpointUpdatePDump(psSyncCheckpointInt->psSyncCheckpointBlock->psDevNode, psSyncCheckpointInt, PVRSRV_SYNC_CHECKPOINT_ERRORED, ui32FenceSyncFlags);
#endif
		}
	}
}

IMG_BOOL SyncCheckpointIsSignalled(PSYNC_CHECKPOINT psSyncCheckpoint, IMG_UINT32 ui32FenceSyncFlags)
{
	IMG_BOOL bRet = IMG_FALSE;
	SYNC_CHECKPOINT *psSyncCheckpointInt = (SYNC_CHECKPOINT*)psSyncCheckpoint;

	PVR_LOG_IF_FALSE((psSyncCheckpoint != NULL), "psSyncCheckpoint invalid");

	if (psSyncCheckpointInt)
	{
#if defined(SUPPORT_RGX)
		PVRSRV_RGXDEV_INFO *psDevInfo = psSyncCheckpointInt->psSyncCheckpointBlock->psDevNode->pvDevice;

		RGXSRVHWPerfSyncCheckpointUFOIsSignalled(psDevInfo, psSyncCheckpointInt, ui32FenceSyncFlags);
#endif
		bRet = ((psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_SIGNALLED) ||
				(psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_ERRORED));

#if (ENABLE_SYNC_CHECKPOINT_ENQ_AND_SIGNAL_DEBUG == 1)
		PVR_DPF((PVR_DBG_WARNING,
				"%s called for psSyncCheckpoint<%p>, returning %d",
				__func__,
				(void*)psSyncCheckpoint,
				bRet));
#endif
	}
	return bRet;
}

IMG_BOOL
SyncCheckpointIsErrored(PSYNC_CHECKPOINT psSyncCheckpoint, IMG_UINT32 ui32FenceSyncFlags)
{
	IMG_BOOL bRet = IMG_FALSE;
	SYNC_CHECKPOINT *psSyncCheckpointInt = (SYNC_CHECKPOINT*)psSyncCheckpoint;

	PVR_LOG_IF_FALSE((psSyncCheckpoint != NULL), "psSyncCheckpoint invalid");

	if (psSyncCheckpointInt)
	{
#if defined(SUPPORT_RGX)
		PVRSRV_RGXDEV_INFO *psDevInfo = psSyncCheckpointInt->psSyncCheckpointBlock->psDevNode->pvDevice;

		RGXSRVHWPerfSyncCheckpointUFOIsSignalled(psDevInfo, psSyncCheckpointInt, ui32FenceSyncFlags);
#endif
		bRet = (psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_ERRORED);

#if (ENABLE_SYNC_CHECKPOINT_ENQ_AND_SIGNAL_DEBUG == 1)
		PVR_DPF((PVR_DBG_WARNING,
				"%s called for psSyncCheckpoint<%p>, returning %d",
				__func__,
				(void*)psSyncCheckpoint,
				bRet));
#endif
	}
	return bRet;
}

const IMG_CHAR *
SyncCheckpointGetStateString(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	SYNC_CHECKPOINT *psSyncCheckpointInt = (SYNC_CHECKPOINT*)psSyncCheckpoint;

	PVR_LOG_RETURN_IF_FALSE((psSyncCheckpoint != NULL), "psSyncCheckpoint invalid", "Null");

	switch (psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State)
	{
		case PVRSRV_SYNC_CHECKPOINT_SIGNALLED:
			return "Signalled";
		case PVRSRV_SYNC_CHECKPOINT_ACTIVE:
			return "Active";
		case PVRSRV_SYNC_CHECKPOINT_ERRORED:
			return "Errored";
		case PVRSRV_SYNC_CHECKPOINT_UNDEF:
			return "Undefined";
		default:
			return "Unknown";
	}
}

PVRSRV_ERROR
SyncCheckpointTakeRef(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	PVRSRV_ERROR eRet = PVRSRV_OK;
	SYNC_CHECKPOINT *psSyncCheckpointInt = (SYNC_CHECKPOINT*)psSyncCheckpoint;

	PVR_LOG_RETURN_IF_INVALID_PARAM(psSyncCheckpoint, "psSyncCheckpoint");

#if (ENABLE_SYNC_CHECKPOINT_ALLOC_AND_FREE_DEBUG == 1)
	PVR_DPF((PVR_DBG_WARNING, "%s called for psSyncCheckpoint<%p> %d->%d (FWRef %u)",
			__func__,
			psSyncCheckpointInt,
			OSAtomicRead(&psSyncCheckpointInt->hRefCount),
			OSAtomicRead(&psSyncCheckpointInt->hRefCount)+1,
			psSyncCheckpointInt->psSyncCheckpointFwObj->ui32FwRefCount));
#endif
	OSAtomicIncrement(&psSyncCheckpointInt->hRefCount);

	return eRet;
}

PVRSRV_ERROR
SyncCheckpointDropRef(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	PVRSRV_ERROR eRet = PVRSRV_OK;
	SYNC_CHECKPOINT *psSyncCheckpointInt = (SYNC_CHECKPOINT*)psSyncCheckpoint;

	PVR_LOG_RETURN_IF_INVALID_PARAM(psSyncCheckpoint, "psSyncCheckpoint");

#if (ENABLE_SYNC_CHECKPOINT_ALLOC_AND_FREE_DEBUG == 1)
	PVR_DPF((PVR_DBG_WARNING, "%s called for psSyncCheckpoint<%p> %d->%d (FWRef %u)",
			__func__,
			psSyncCheckpointInt,
			OSAtomicRead(&psSyncCheckpointInt->hRefCount),
			OSAtomicRead(&psSyncCheckpointInt->hRefCount)-1,
			psSyncCheckpointInt->psSyncCheckpointFwObj->ui32FwRefCount));
#endif
	SyncCheckpointUnref(psSyncCheckpointInt);

	return eRet;
}

void
SyncCheckpointCCBEnqueued(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	SYNC_CHECKPOINT *psSyncCheckpointInt = (SYNC_CHECKPOINT*)psSyncCheckpoint;

	PVR_LOG_RETURN_VOID_IF_FALSE(psSyncCheckpoint != NULL, "psSyncCheckpoint");

	if (psSyncCheckpointInt)
	{
#if !defined(NO_HARDWARE)
#if (ENABLE_SYNC_CHECKPOINT_ENQ_AND_SIGNAL_DEBUG == 1)
		PVR_DPF((PVR_DBG_WARNING, "%s called for psSyncCheckpoint<%p> %d->%d (FWRef %u)",
				__func__,
				(void*)psSyncCheckpoint,
				OSAtomicRead(&psSyncCheckpointInt->hEnqueuedCCBCount),
				OSAtomicRead(&psSyncCheckpointInt->hEnqueuedCCBCount)+1,
				psSyncCheckpointInt->psSyncCheckpointFwObj->ui32FwRefCount));
#endif
		OSAtomicIncrement(&psSyncCheckpointInt->hEnqueuedCCBCount);
#endif
	}
}

PRGXFWIF_UFO_ADDR*
SyncCheckpointGetRGXFWIFUFOAddr(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	SYNC_CHECKPOINT *psSyncCheckpointInt = (SYNC_CHECKPOINT*)psSyncCheckpoint;

	PVR_LOG_GOTO_IF_FALSE((psSyncCheckpoint != NULL), "psSyncCheckpoint invalid", invalid_chkpt);

	if (psSyncCheckpointInt)
	{
		if (psSyncCheckpointInt->ui32ValidationCheck == SYNC_CHECKPOINT_PATTERN_IN_USE)
		{
			return &psSyncCheckpointInt->sCheckpointUFOAddr;
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s called for psSyncCheckpoint<%p>, but ui32ValidationCheck=0x%x",
					__func__,
					(void*)psSyncCheckpoint,
					psSyncCheckpointInt->ui32ValidationCheck));
		}
	}

invalid_chkpt:
	return NULL;
}

IMG_UINT32
SyncCheckpointGetFirmwareAddr(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	SYNC_CHECKPOINT *psSyncCheckpointInt = (SYNC_CHECKPOINT*)psSyncCheckpoint;
	IMG_UINT32 ui32Ret = 0;

	PVR_LOG_GOTO_IF_FALSE((psSyncCheckpoint != NULL), "psSyncCheckpoint invalid", invalid_chkpt);

	if (psSyncCheckpointInt)
	{
		if (psSyncCheckpointInt->ui32ValidationCheck == SYNC_CHECKPOINT_PATTERN_IN_USE)
		{
			ui32Ret = psSyncCheckpointInt->ui32FWAddr;
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s called for psSyncCheckpoint<%p>, but ui32ValidationCheck=0x%x",
					__func__,
					(void*)psSyncCheckpoint,
					psSyncCheckpointInt->ui32ValidationCheck));
		}
	}

invalid_chkpt:
	return ui32Ret;
}

IMG_UINT32
SyncCheckpointGetId(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	SYNC_CHECKPOINT *psSyncCheckpointInt = (SYNC_CHECKPOINT*)psSyncCheckpoint;
	IMG_UINT32 ui32Ret = 0;

	PVR_LOG_GOTO_IF_FALSE((psSyncCheckpoint != NULL), "psSyncCheckpoint invalid", invalid_chkpt);

	if (psSyncCheckpointInt)
	{
#if (ENABLE_SYNC_CHECKPOINT_UFO_DEBUG == 1)
		PVR_DPF((PVR_DBG_WARNING,
				"%s returning ID for sync checkpoint<%p>",
				__func__,
				(void*)psSyncCheckpointInt));
		PVR_DPF((PVR_DBG_WARNING,
				"%s (validationCheck=0x%x)",
				__func__,
				psSyncCheckpointInt->ui32ValidationCheck));
#endif
		ui32Ret = psSyncCheckpointInt->ui32UID;
#if (ENABLE_SYNC_CHECKPOINT_UFO_DEBUG == 1)
		PVR_DPF((PVR_DBG_WARNING,
				"%s (ui32UID=0x%x)",
				__func__,
				psSyncCheckpointInt->ui32UID));
#endif
	}
	return ui32Ret;

invalid_chkpt:
	return 0;
}

PVRSRV_TIMELINE
SyncCheckpointGetTimeline(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	SYNC_CHECKPOINT *psSyncCheckpointInt = (SYNC_CHECKPOINT*)psSyncCheckpoint;
	PVRSRV_TIMELINE i32Ret = PVRSRV_NO_TIMELINE;

	PVR_LOG_GOTO_IF_FALSE((psSyncCheckpoint != NULL), "psSyncCheckpoint invalid", invalid_chkpt);

	if (psSyncCheckpointInt)
	{
		i32Ret = psSyncCheckpointInt->hTimeline;
	}
	return i32Ret;

invalid_chkpt:
	return 0;
}


IMG_UINT32
SyncCheckpointGetEnqueuedCount(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	SYNC_CHECKPOINT *psSyncCheckpointInt = (SYNC_CHECKPOINT*)psSyncCheckpoint;
	PVR_LOG_RETURN_IF_FALSE(psSyncCheckpoint != NULL, "psSyncCheckpoint invalid", 0);

	return OSAtomicRead(&psSyncCheckpointInt->hEnqueuedCCBCount);
}

IMG_UINT32
SyncCheckpointGetReferenceCount(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	SYNC_CHECKPOINT *psSyncCheckpointInt = (SYNC_CHECKPOINT*)psSyncCheckpoint;
	PVR_LOG_RETURN_IF_FALSE(psSyncCheckpoint != NULL, "psSyncCheckpoint invalid", 0);

	return OSAtomicRead(&psSyncCheckpointInt->hRefCount);
}

IMG_PID
SyncCheckpointGetCreator(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	SYNC_CHECKPOINT *psSyncCheckpointInt = (SYNC_CHECKPOINT*)psSyncCheckpoint;
	PVR_LOG_RETURN_IF_FALSE(psSyncCheckpoint != NULL, "psSyncCheckpoint invalid", 0);

	return psSyncCheckpointInt->uiProcess;
}

IMG_UINT32 SyncCheckpointStateFromUFO(PPVRSRV_DEVICE_NODE psDevNode,
                                IMG_UINT32 ui32FwAddr)
{
	SYNC_CHECKPOINT *psSyncCheckpointInt;
	PDLLIST_NODE psNode, psNext;
	IMG_UINT32 ui32State = 0;
	OS_SPINLOCK_FLAGS uiFlags;

	OSSpinLockAcquire(psDevNode->hSyncCheckpointListLock, uiFlags);
	dllist_foreach_node(&psDevNode->sSyncCheckpointSyncsList, psNode, psNext)
	{
		psSyncCheckpointInt = IMG_CONTAINER_OF(psNode, SYNC_CHECKPOINT, sListNode);
		if (ui32FwAddr == SyncCheckpointGetFirmwareAddr((PSYNC_CHECKPOINT)psSyncCheckpointInt))
		{
			ui32State = psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State;
			break;
		}
	}
	OSSpinLockRelease(psDevNode->hSyncCheckpointListLock, uiFlags);
	return ui32State;
}

void SyncCheckpointErrorFromUFO(PPVRSRV_DEVICE_NODE psDevNode,
                                IMG_UINT32 ui32FwAddr)
{
	SYNC_CHECKPOINT *psSyncCheckpointInt;
	PDLLIST_NODE psNode, psNext;
	OS_SPINLOCK_FLAGS uiFlags;

#if (ENABLE_SYNC_CHECKPOINT_UFO_DEBUG == 1)
	PVR_DPF((PVR_DBG_WARNING,
			"%s called to error UFO with ui32FWAddr=%d",
			__func__,
			ui32FwAddr));
#endif

	OSSpinLockAcquire(psDevNode->hSyncCheckpointListLock, uiFlags);
	dllist_foreach_node(&psDevNode->sSyncCheckpointSyncsList, psNode, psNext)
	{
		psSyncCheckpointInt = IMG_CONTAINER_OF(psNode, SYNC_CHECKPOINT, sListNode);
		if (ui32FwAddr == SyncCheckpointGetFirmwareAddr((PSYNC_CHECKPOINT)psSyncCheckpointInt))
		{
#if (ENABLE_SYNC_CHECKPOINT_UFO_DEBUG == 1)
			PVR_DPF((PVR_DBG_WARNING,
					"%s calling SyncCheckpointError for sync checkpoint <%p>",
					__func__,
					(void*)psSyncCheckpointInt));
#endif
			/* Mark as errored */
			SyncCheckpointError((PSYNC_CHECKPOINT)psSyncCheckpointInt, IMG_TRUE);
			break;
		}
	}
	OSSpinLockRelease(psDevNode->hSyncCheckpointListLock, uiFlags);
}

void SyncCheckpointRollbackFromUFO(PPVRSRV_DEVICE_NODE psDevNode, IMG_UINT32 ui32FwAddr)
{
#if (ENABLE_SYNC_CHECKPOINT_UFO_DEBUG == 1)
	PVR_DPF((PVR_DBG_WARNING,
			"%s called to rollback UFO with ui32FWAddr=0x%x",
			__func__,
			ui32FwAddr));
#endif
#if !defined(NO_HARDWARE)
	{
		SYNC_CHECKPOINT *psSyncCheckpointInt = NULL;
		PDLLIST_NODE psNode = NULL, psNext = NULL;
		OS_SPINLOCK_FLAGS uiFlags;

		OSSpinLockAcquire(psDevNode->hSyncCheckpointListLock, uiFlags);
		dllist_foreach_node(&psDevNode->sSyncCheckpointSyncsList, psNode, psNext)
		{
			psSyncCheckpointInt = IMG_CONTAINER_OF(psNode, SYNC_CHECKPOINT, sListNode);
			if (ui32FwAddr == SyncCheckpointGetFirmwareAddr((PSYNC_CHECKPOINT)psSyncCheckpointInt))
			{
#if ((ENABLE_SYNC_CHECKPOINT_UFO_DEBUG == 1)) || (ENABLE_SYNC_CHECKPOINT_ENQ_AND_SIGNAL_DEBUG == 1)
				PVR_DPF((PVR_DBG_WARNING,
				        "%s called for psSyncCheckpointInt<%p> %d->%d",
				        __func__,
				        (void *) psSyncCheckpointInt,
				        OSAtomicRead(&psSyncCheckpointInt->hEnqueuedCCBCount),
				        OSAtomicRead(&psSyncCheckpointInt->hEnqueuedCCBCount) - 1));
#endif
				OSAtomicDecrement(&psSyncCheckpointInt->hEnqueuedCCBCount);
				break;
			}
		}
		OSSpinLockRelease(psDevNode->hSyncCheckpointListLock, uiFlags);
	}
#endif
}

static void _SyncCheckpointState(PDLLIST_NODE psNode,
					DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile)
{
	SYNC_CHECKPOINT *psSyncCheckpoint = IMG_CONTAINER_OF(psNode, SYNC_CHECKPOINT, sListNode);

	if (psSyncCheckpoint->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_ACTIVE)
	{
		PVR_DUMPDEBUG_LOG("\t- ID = %d, FWAddr = 0x%08x, r%d:e%d:f%d: %s",
		                   psSyncCheckpoint->ui32UID,
		                   psSyncCheckpoint->psSyncCheckpointBlock->ui32FirmwareAddr +
		                   _SyncCheckpointGetOffset(psSyncCheckpoint),
		                   OSAtomicRead(&psSyncCheckpoint->hRefCount),
		                   OSAtomicRead(&psSyncCheckpoint->hEnqueuedCCBCount),
		                   psSyncCheckpoint->psSyncCheckpointFwObj->ui32FwRefCount,
		                   psSyncCheckpoint->azName);
	}
}

static void _SyncCheckpointDebugRequest(PVRSRV_DBGREQ_HANDLE hDebugRequestHandle,
					IMG_UINT32 ui32VerbLevel,
					DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile)
{
	PVRSRV_DEVICE_NODE *psDevNode = (PVRSRV_DEVICE_NODE *)hDebugRequestHandle;
	DLLIST_NODE *psNode, *psNext;
	OS_SPINLOCK_FLAGS uiFlags;

	if (DD_VERB_LVL_ENABLED(ui32VerbLevel, DEBUG_REQUEST_VERBOSITY_MEDIUM))
	{
		PVR_DUMPDEBUG_LOG("------[ Active Sync Checkpoints ]------");
		OSSpinLockAcquire(psDevNode->hSyncCheckpointListLock, uiFlags);
		dllist_foreach_node(&psDevNode->sSyncCheckpointSyncsList, psNode, psNext)
		{
			_SyncCheckpointState(psNode, pfnDumpDebugPrintf, pvDumpDebugFile);
		}
		OSSpinLockRelease(psDevNode->hSyncCheckpointListLock, uiFlags);
	}
}

PVRSRV_ERROR
SyncCheckpointInit(PPVRSRV_DEVICE_NODE psDevNode)
{
	PVRSRV_ERROR eError;
#if defined(PDUMP)
	PVRSRV_RGXDEV_INFO *psDevInfo;

	psDevInfo = psDevNode->pvDevice;
#endif

	eError = OSSpinLockCreate(&psDevNode->hSyncCheckpointListLock);
	PVR_RETURN_IF_ERROR(eError);

	dllist_init(&psDevNode->sSyncCheckpointSyncsList);

	eError = PVRSRVRegisterDeviceDbgRequestNotify(&psDevNode->hSyncCheckpointNotify,
		                                          psDevNode,
		                                          _SyncCheckpointDebugRequest,
		                                          DEBUG_REQUEST_SYNCCHECKPOINT,
		                                          (PVRSRV_DBGREQ_HANDLE)psDevNode);
	PVR_GOTO_IF_ERROR(eError, e0);

	if (GetInfoPageDebugFlagsKM() & DEBUG_FEATURE_FULL_SYNC_TRACKING_ENABLED)
	{
		_SyncCheckpointRecordListInit(psDevNode);
	}

#if defined(PDUMP)
	eError = OSSpinLockCreate(&psDevInfo->hSyncCheckpointSignalSpinLock);
	if (eError != PVRSRV_OK)
	{
		psDevInfo->hSyncCheckpointSignalSpinLock = NULL;
		goto e1;
	}

	eError = OSLockCreate(&psDevNode->hSyncCheckpointSignalLock);
	if (eError != PVRSRV_OK)
	{
		psDevNode->hSyncCheckpointSignalLock = NULL;
		goto e2;
	}

	psDevNode->pui8DeferredSyncCPSignal = OSAllocMem(SYNC_CHECKPOINT_MAX_DEFERRED_SIGNAL
	                                                      * sizeof(_SYNC_CHECKPOINT_DEFERRED_SIGNAL));
	PVR_GOTO_IF_NOMEM(psDevNode->pui8DeferredSyncCPSignal, eError, e3);

	psDevNode->ui16SyncCPWriteIdx = 0;
	psDevNode->ui16SyncCPReadIdx = 0;

	eError = OSInstallMISR(&psDevNode->pvSyncCPMISR,
					MISRHandler_PdumpDeferredSyncSignalPoster,
					psDevNode,
					"RGX_PdumpDeferredSyncSignalPoster");
	PVR_GOTO_IF_ERROR(eError, e4);

	eError = OSLockCreate(&psDevNode->hSyncCheckpointContextListLock);
	if (eError != PVRSRV_OK)
	{
		psDevNode->hSyncCheckpointContextListLock = NULL;
		goto e5;
	}


	dllist_init(&psDevNode->sSyncCheckpointContextListHead);

	eError = PDumpRegisterTransitionCallbackFenceSync(psDevNode,
								_SyncCheckpointPDumpTransition,
								&psDevNode->hTransition);
	if (eError != PVRSRV_OK)
	{
		psDevNode->hTransition = NULL;
		goto e6;
	}
#endif

	return PVRSRV_OK;

#if defined(PDUMP)
e6:
	OSLockDestroy(psDevNode->hSyncCheckpointContextListLock);
	psDevNode->hSyncCheckpointContextListLock = NULL;
e5:
	(void) OSUninstallMISR(psDevNode->pvSyncCPMISR);
	psDevNode->pvSyncCPMISR = NULL;
e4:
	if (psDevNode->pui8DeferredSyncCPSignal)
	{
		OSFreeMem(psDevNode->pui8DeferredSyncCPSignal);
		psDevNode->pui8DeferredSyncCPSignal = NULL;
	}
e3:
	OSLockDestroy(psDevNode->hSyncCheckpointSignalLock);
	psDevNode->hSyncCheckpointSignalLock = NULL;
e2:
	OSSpinLockDestroy(psDevInfo->hSyncCheckpointSignalSpinLock);
	psDevInfo->hSyncCheckpointSignalSpinLock = NULL;
e1:
	_SyncCheckpointRecordListDeinit(psDevNode);
#endif
e0:
	OSSpinLockDestroy(psDevNode->hSyncCheckpointListLock);
	psDevNode->hSyncCheckpointListLock = NULL;

	return eError;
}

void SyncCheckpointDeinit(PPVRSRV_DEVICE_NODE psDevNode)
{
#if defined(PDUMP)
	PVRSRV_RGXDEV_INFO *psDevInfo;

	psDevInfo = psDevNode->pvDevice;
	PDumpUnregisterTransitionCallbackFenceSync(psDevNode->hTransition);
	psDevNode->hTransition = NULL;

	if (psDevNode->hSyncCheckpointContextListLock)
	{
		OSLockDestroy(psDevNode->hSyncCheckpointContextListLock);
		psDevNode->hSyncCheckpointContextListLock = NULL;
	}

	if (psDevNode->pvSyncCPMISR)
	{
		(void) OSUninstallMISR(psDevNode->pvSyncCPMISR);
		psDevNode->pvSyncCPMISR = NULL;
	}

	if (psDevNode->pui8DeferredSyncCPSignal)
	{
		OSFreeMem(psDevNode->pui8DeferredSyncCPSignal);
		psDevNode->pui8DeferredSyncCPSignal = NULL;
	}
	if (psDevNode->hSyncCheckpointSignalLock)
	{
		OSLockDestroy(psDevNode->hSyncCheckpointSignalLock);
		psDevNode->hSyncCheckpointSignalLock = NULL;
	}
	if (psDevInfo->hSyncCheckpointSignalSpinLock)
	{
		OSSpinLockDestroy(psDevInfo->hSyncCheckpointSignalSpinLock);
		psDevInfo->hSyncCheckpointSignalSpinLock = NULL;
	}
#endif

	PVRSRVUnregisterDeviceDbgRequestNotify(psDevNode->hSyncCheckpointNotify);
	psDevNode->hSyncCheckpointNotify = NULL;
	OSSpinLockDestroy(psDevNode->hSyncCheckpointListLock);
	psDevNode->hSyncCheckpointListLock = NULL;
	if (GetInfoPageDebugFlagsKM() & DEBUG_FEATURE_FULL_SYNC_TRACKING_ENABLED)
	{
		_SyncCheckpointRecordListDeinit(psDevNode);
	}
}

void SyncCheckpointRecordLookup(PPVRSRV_DEVICE_NODE psDevNode, IMG_UINT32 ui32FwAddr,
                                IMG_CHAR * pszSyncInfo, size_t len)
{
	DLLIST_NODE *psNode, *psNext;
	IMG_BOOL bFound = IMG_FALSE;

	if (!pszSyncInfo)
	{
		return;
	}

	pszSyncInfo[0] = '\0';

	OSLockAcquire(psDevNode->hSyncCheckpointRecordLock);
	dllist_foreach_node(&psDevNode->sSyncCheckpointRecordList, psNode, psNext)
	{
		struct SYNC_CHECKPOINT_RECORD *psSyncCheckpointRec =
				IMG_CONTAINER_OF(psNode, struct SYNC_CHECKPOINT_RECORD, sNode);
		if ((psSyncCheckpointRec->ui32FwBlockAddr + psSyncCheckpointRec->ui32SyncOffset + 1) == ui32FwAddr)
		{
			SYNC_CHECKPOINT_BLOCK *psSyncCheckpointBlock = psSyncCheckpointRec->psSyncCheckpointBlock;
			if (psSyncCheckpointBlock && psSyncCheckpointBlock->pui32LinAddr)
			{
				void *pSyncCheckpointAddr = IMG_OFFSET_ADDR(psSyncCheckpointBlock->pui32LinAddr,
													psSyncCheckpointRec->ui32SyncOffset);
				OSSNPrintf(pszSyncInfo, len, "%s Checkpoint:%05u (%s)",
				           (*(IMG_UINT32*)pSyncCheckpointAddr == PVRSRV_SYNC_CHECKPOINT_SIGNALLED) ?
				                   "SIGNALLED" :
				                   ((*(IMG_UINT32*)pSyncCheckpointAddr == PVRSRV_SYNC_CHECKPOINT_ERRORED) ?
				                           "ERRORED" : "ACTIVE"),
				                           psSyncCheckpointRec->uiPID,
				                           psSyncCheckpointRec->szClassName);
			}
			else
			{
				OSSNPrintf(pszSyncInfo, len, "Checkpoint:%05u (%s)",
				           psSyncCheckpointRec->uiPID,
				           psSyncCheckpointRec->szClassName);
			}

			bFound = IMG_TRUE;
			break;
		}
	}
	OSLockRelease(psDevNode->hSyncCheckpointRecordLock);

	if (!bFound && (psDevNode->ui32SyncCheckpointRecordCountHighWatermark == SYNC_CHECKPOINT_RECORD_LIMIT))
	{
		OSSNPrintf(pszSyncInfo, len, "(Record may be lost)");
	}
}

static PVRSRV_ERROR
_SyncCheckpointRecordAdd(
			PSYNC_CHECKPOINT_RECORD_HANDLE * phRecord,
			SYNC_CHECKPOINT_BLOCK *hSyncCheckpointBlock,
			IMG_UINT32 ui32FwBlockAddr,
			IMG_UINT32 ui32SyncOffset,
			IMG_UINT32 ui32UID,
			IMG_UINT32 ui32ClassNameSize,
			const IMG_CHAR *pszClassName, PSYNC_CHECKPOINT pSyncCheckpt)
{
	struct SYNC_CHECKPOINT_RECORD * psSyncRec;
	_SYNC_CHECKPOINT_CONTEXT *psContext = hSyncCheckpointBlock->psContext;
	PVRSRV_DEVICE_NODE *psDevNode = psContext->psDevNode;
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_RETURN_IF_INVALID_PARAM(phRecord);

	*phRecord = NULL;

	psSyncRec = OSAllocMem(sizeof(*psSyncRec));
	PVR_LOG_GOTO_IF_NOMEM(psSyncRec, eError, fail_alloc); /* Sets OOM error code */

	psSyncRec->psDevNode = psDevNode;
	psSyncRec->psSyncCheckpointBlock = hSyncCheckpointBlock;
	psSyncRec->ui32SyncOffset = ui32SyncOffset;
	psSyncRec->ui32FwBlockAddr = ui32FwBlockAddr;
	psSyncRec->ui64OSTime = OSClockns64();
	psSyncRec->uiPID = OSGetCurrentProcessID();
	psSyncRec->ui32UID = ui32UID;
	psSyncRec->pSyncCheckpt = pSyncCheckpt;
	if (pszClassName)
	{
		if (ui32ClassNameSize >= PVRSRV_SYNC_NAME_LENGTH)
			ui32ClassNameSize = PVRSRV_SYNC_NAME_LENGTH;
		/* Copy over the class name annotation */
		OSStringLCopy(psSyncRec->szClassName, pszClassName, ui32ClassNameSize);
	}
	else
	{
		/* No class name annotation */
		psSyncRec->szClassName[0] = 0;
	}

	OSLockAcquire(psDevNode->hSyncCheckpointRecordLock);
	if (psDevNode->ui32SyncCheckpointRecordCount < SYNC_CHECKPOINT_RECORD_LIMIT)
	{
		dllist_add_to_head(&psDevNode->sSyncCheckpointRecordList, &psSyncRec->sNode);
		psDevNode->ui32SyncCheckpointRecordCount++;

		if (psDevNode->ui32SyncCheckpointRecordCount > psDevNode->ui32SyncCheckpointRecordCountHighWatermark)
		{
			psDevNode->ui32SyncCheckpointRecordCountHighWatermark = psDevNode->ui32SyncCheckpointRecordCount;
		}
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to add sync checkpoint record \"%s\". %u records already exist.",
				__func__,
				pszClassName,
				psDevNode->ui32SyncCheckpointRecordCount));
		OSFreeMem(psSyncRec);
		psSyncRec = NULL;
		eError = PVRSRV_ERROR_TOOMANYBUFFERS;
	}
	OSLockRelease(psDevNode->hSyncCheckpointRecordLock);

	*phRecord = (PSYNC_CHECKPOINT_RECORD_HANDLE)psSyncRec;

fail_alloc:
	return eError;
}

static PVRSRV_ERROR
_SyncCheckpointRecordRemove(PSYNC_CHECKPOINT_RECORD_HANDLE hRecord)
{
	struct SYNC_CHECKPOINT_RECORD **ppFreedSync;
	struct SYNC_CHECKPOINT_RECORD *pSync = (struct SYNC_CHECKPOINT_RECORD*)hRecord;
	PVRSRV_DEVICE_NODE *psDevNode;

	PVR_RETURN_IF_INVALID_PARAM(hRecord);

	psDevNode = pSync->psDevNode;

	OSLockAcquire(psDevNode->hSyncCheckpointRecordLock);

	dllist_remove_node(&pSync->sNode);

	if (psDevNode->uiSyncCheckpointRecordFreeIdx >= PVRSRV_FULL_SYNC_TRACKING_HISTORY_LEN)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: psDevNode->uiSyncCheckpointRecordFreeIdx out of range",
				__func__));
		psDevNode->uiSyncCheckpointRecordFreeIdx = 0;
	}
	ppFreedSync = &psDevNode->apsSyncCheckpointRecordsFreed[psDevNode->uiSyncCheckpointRecordFreeIdx];
	psDevNode->uiSyncCheckpointRecordFreeIdx =
			(psDevNode->uiSyncCheckpointRecordFreeIdx + 1) % PVRSRV_FULL_SYNC_TRACKING_HISTORY_LEN;

	if (*ppFreedSync)
	{
		OSFreeMem(*ppFreedSync);
	}
	pSync->psSyncCheckpointBlock = NULL;
	pSync->ui64OSTime = OSClockns64();
	*ppFreedSync = pSync;

	psDevNode->ui32SyncCheckpointRecordCount--;

	OSLockRelease(psDevNode->hSyncCheckpointRecordLock);

	return PVRSRV_OK;
}

#define NS_IN_S (1000000000UL)
static void _SyncCheckpointRecordPrint(struct SYNC_CHECKPOINT_RECORD *psSyncCheckpointRec,
                                       IMG_UINT64 ui64TimeNow,
                                       DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                       void *pvDumpDebugFile)
{
	SYNC_CHECKPOINT *psSyncCheckpoint = (SYNC_CHECKPOINT *)psSyncCheckpointRec->pSyncCheckpt;
	SYNC_CHECKPOINT_BLOCK *psSyncCheckpointBlock = psSyncCheckpointRec->psSyncCheckpointBlock;
	IMG_UINT64 ui64DeltaS;
	IMG_UINT32 ui32DeltaF;
	IMG_UINT64 ui64Delta = ui64TimeNow - psSyncCheckpointRec->ui64OSTime;
	ui64DeltaS = OSDivide64(ui64Delta, NS_IN_S, &ui32DeltaF);

	if (psSyncCheckpointBlock && psSyncCheckpointBlock->pui32LinAddr)
	{
		void *pSyncCheckpointAddr;
		pSyncCheckpointAddr = IMG_OFFSET_ADDR(psSyncCheckpointBlock->pui32LinAddr,
											psSyncCheckpointRec->ui32SyncOffset);

		PVR_DUMPDEBUG_LOG("\t%05u %05" IMG_UINT64_FMTSPEC ".%09u %010u FWAddr=0x%08x (r%d:e%d:f%d) State=%s (%s)",
		                  psSyncCheckpointRec->uiPID,
		                  ui64DeltaS, ui32DeltaF, psSyncCheckpointRec->ui32UID,
		                  (psSyncCheckpointRec->ui32FwBlockAddr+psSyncCheckpointRec->ui32SyncOffset),
		                  OSAtomicRead(&psSyncCheckpoint->hRefCount),
	                      OSAtomicRead(&psSyncCheckpoint->hEnqueuedCCBCount),
	                      psSyncCheckpoint->psSyncCheckpointFwObj->ui32FwRefCount,
		                  (*(IMG_UINT32*)pSyncCheckpointAddr == PVRSRV_SYNC_CHECKPOINT_SIGNALLED) ?
		                          "SIGNALLED" :
		                          ((*(IMG_UINT32*)pSyncCheckpointAddr == PVRSRV_SYNC_CHECKPOINT_ERRORED) ?
		                                  "ERRORED" : "ACTIVE"),
		                                  psSyncCheckpointRec->szClassName);
	}
	else
	{
		PVR_DUMPDEBUG_LOG("\t%05u %05" IMG_UINT64_FMTSPEC ".%09u %010u FWAddr=0x%08x State=<null_ptr> (%s)",
		                  psSyncCheckpointRec->uiPID,
		                  ui64DeltaS, ui32DeltaF, psSyncCheckpointRec->ui32UID,
		                  (psSyncCheckpointRec->ui32FwBlockAddr+psSyncCheckpointRec->ui32SyncOffset),
		                  psSyncCheckpointRec->szClassName
		);
	}
}

static void _SyncCheckpointRecordRequest(PVRSRV_DBGREQ_HANDLE hDebugRequestHandle,
                                         IMG_UINT32 ui32VerbLevel,
                                         DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                         void *pvDumpDebugFile)
{
	PVRSRV_DEVICE_NODE *psDevNode = (PVRSRV_DEVICE_NODE *)hDebugRequestHandle;
	IMG_UINT64 ui64TimeNowS;
	IMG_UINT32 ui32TimeNowF;
	IMG_UINT64 ui64TimeNow = OSClockns64();
	DLLIST_NODE *psNode, *psNext;

	ui64TimeNowS = OSDivide64(ui64TimeNow, NS_IN_S, &ui32TimeNowF);

	if (DD_VERB_LVL_ENABLED(ui32VerbLevel, DEBUG_REQUEST_VERBOSITY_MEDIUM))
	{
		IMG_UINT32 i;

		OSLockAcquire(psDevNode->hSyncCheckpointRecordLock);

		PVR_DUMPDEBUG_LOG("Dumping allocated sync checkpoints. Allocated: %u High watermark: %u (time ref %05" IMG_UINT64_FMTSPEC ".%09u)",
		                  psDevNode->ui32SyncCheckpointRecordCount,
		                  psDevNode->ui32SyncCheckpointRecordCountHighWatermark,
		                  ui64TimeNowS,
		                  ui32TimeNowF);
		if (psDevNode->ui32SyncCheckpointRecordCountHighWatermark == SYNC_CHECKPOINT_RECORD_LIMIT)
		{
			PVR_DUMPDEBUG_LOG("Warning: Record limit (%u) was reached. Some sync checkpoints may not have been recorded in the debug information.",
			                  SYNC_CHECKPOINT_RECORD_LIMIT);
		}
		PVR_DUMPDEBUG_LOG("\t%-5s %-15s %-10s %-17s %-14s (%s)",
		                  "PID", "Time Delta (s)", "UID", "Address", "State", "Annotation");

		dllist_foreach_node(&psDevNode->sSyncCheckpointRecordList, psNode, psNext)
		{
			struct SYNC_CHECKPOINT_RECORD *psSyncCheckpointRec =
					IMG_CONTAINER_OF(psNode, struct SYNC_CHECKPOINT_RECORD, sNode);
			_SyncCheckpointRecordPrint(psSyncCheckpointRec, ui64TimeNow,
			                           pfnDumpDebugPrintf, pvDumpDebugFile);
		}

		PVR_DUMPDEBUG_LOG("Dumping all recently freed sync checkpoints @ %05" IMG_UINT64_FMTSPEC ".%09u",
		                  ui64TimeNowS,
		                  ui32TimeNowF);
		PVR_DUMPDEBUG_LOG("\t%-5s %-15s %-10s %-17s %-14s (%s)",
		                  "PID", "Time Delta (s)", "UID", "Address", "State", "Annotation");
		for (i = DECREMENT_WITH_WRAP(psDevNode->uiSyncCheckpointRecordFreeIdx, PVRSRV_FULL_SYNC_TRACKING_HISTORY_LEN);
				i != psDevNode->uiSyncCheckpointRecordFreeIdx;
				i = DECREMENT_WITH_WRAP(i, PVRSRV_FULL_SYNC_TRACKING_HISTORY_LEN))
		{
			if (psDevNode->apsSyncCheckpointRecordsFreed[i])
			{
				_SyncCheckpointRecordPrint(psDevNode->apsSyncCheckpointRecordsFreed[i],
				                           ui64TimeNow, pfnDumpDebugPrintf, pvDumpDebugFile);
			}
			else
			{
				break;
			}
		}
		OSLockRelease(psDevNode->hSyncCheckpointRecordLock);
	}
}
#undef NS_IN_S
static PVRSRV_ERROR _SyncCheckpointRecordListInit(PVRSRV_DEVICE_NODE *psDevNode)
{
	PVRSRV_ERROR eError;

	eError = OSLockCreate(&psDevNode->hSyncCheckpointRecordLock);
	PVR_GOTO_IF_ERROR(eError, fail_lock_create);
	dllist_init(&psDevNode->sSyncCheckpointRecordList);

	psDevNode->ui32SyncCheckpointRecordCount = 0;
	psDevNode->ui32SyncCheckpointRecordCountHighWatermark = 0;

	eError = PVRSRVRegisterDeviceDbgRequestNotify(&psDevNode->hSyncCheckpointRecordNotify,
	                                              psDevNode,
	                                              _SyncCheckpointRecordRequest,
	                                              DEBUG_REQUEST_SYNCCHECKPOINT,
	                                              (PVRSRV_DBGREQ_HANDLE)psDevNode);
	PVR_GOTO_IF_ERROR(eError, fail_dbg_register);

	return PVRSRV_OK;

fail_dbg_register:
	OSLockDestroy(psDevNode->hSyncCheckpointRecordLock);
fail_lock_create:
	return eError;
}

static void _SyncCheckpointRecordListDeinit(PVRSRV_DEVICE_NODE *psDevNode)
{
	DLLIST_NODE *psNode, *psNext;
	int i;

	OSLockAcquire(psDevNode->hSyncCheckpointRecordLock);
	dllist_foreach_node(&psDevNode->sSyncCheckpointRecordList, psNode, psNext)
	{
		struct SYNC_CHECKPOINT_RECORD *pSyncCheckpointRec =
				IMG_CONTAINER_OF(psNode, struct SYNC_CHECKPOINT_RECORD, sNode);

		dllist_remove_node(psNode);
		OSFreeMem(pSyncCheckpointRec);
	}

	for (i = 0; i < PVRSRV_FULL_SYNC_TRACKING_HISTORY_LEN; i++)
	{
		if (psDevNode->apsSyncCheckpointRecordsFreed[i])
		{
			OSFreeMem(psDevNode->apsSyncCheckpointRecordsFreed[i]);
			psDevNode->apsSyncCheckpointRecordsFreed[i] = NULL;
		}
	}
	OSLockRelease(psDevNode->hSyncCheckpointRecordLock);

	if (psDevNode->hSyncCheckpointRecordNotify)
	{
		PVRSRVUnregisterDeviceDbgRequestNotify(psDevNode->hSyncCheckpointRecordNotify);
	}
	OSLockDestroy(psDevNode->hSyncCheckpointRecordLock);
}

#if defined(PDUMP)

static PVRSRV_ERROR
_SyncCheckpointAllocPDump(PVRSRV_DEVICE_NODE *psDevNode, SYNC_CHECKPOINT *psSyncCheckpoint)
{
	PDUMPCOMMENTWITHFLAGS(psDevNode, PDUMP_FLAGS_CONTINUOUS,
	                      "Allocated Sync Checkpoint %s (ID:%d, TL:%d, FirmwareVAddr = 0x%08x)",
	                      psSyncCheckpoint->azName,
	                      psSyncCheckpoint->ui32UID, psSyncCheckpoint->hTimeline,
	                      psSyncCheckpoint->sCheckpointUFOAddr.ui32Addr);

	DevmemPDumpLoadMemValue32(psSyncCheckpoint->psSyncCheckpointBlock->hMemDesc,
	                          _SyncCheckpointGetOffset(psSyncCheckpoint),
	                          PVRSRV_SYNC_CHECKPOINT_ACTIVE,
	                          PDUMP_FLAGS_CONTINUOUS);

	return PVRSRV_OK;
}

static PVRSRV_ERROR
_SyncCheckpointUpdatePDump(PPVRSRV_DEVICE_NODE psDevNode, SYNC_CHECKPOINT *psSyncCheckpoint, IMG_UINT32 ui32Status, IMG_UINT32 ui32FenceSyncFlags)
{
	IMG_BOOL bSleepAllowed = (ui32FenceSyncFlags & PVRSRV_FENCE_FLAG_CTX_ATOMIC) ? IMG_FALSE : IMG_TRUE;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	psDevInfo = psDevNode->pvDevice;
	/*
		We might be ask to PDump sync state outside of capture range
		(e.g. texture uploads) so make this continuous.
	 */
	if (bSleepAllowed)
	{
		if (ui32Status == PVRSRV_SYNC_CHECKPOINT_ERRORED)
		{
			PDUMPCOMMENTWITHFLAGS(psDevNode, PDUMP_FLAGS_CONTINUOUS,
					"Errored Sync Checkpoint %s (ID:%d, TL:%d, FirmwareVAddr = 0x%08x)",
					psSyncCheckpoint->azName,
					psSyncCheckpoint->ui32UID, psSyncCheckpoint->hTimeline,
					(psSyncCheckpoint->psSyncCheckpointBlock->ui32FirmwareAddr +
					_SyncCheckpointGetOffset(psSyncCheckpoint)));
		}
		else
		{
			PDUMPCOMMENTWITHFLAGS(psDevNode, PDUMP_FLAGS_CONTINUOUS,
					"Signalled Sync Checkpoint %s (ID:%d, TL:%d, FirmwareVAddr = 0x%08x)",
					psSyncCheckpoint->azName,
					psSyncCheckpoint->ui32UID, psSyncCheckpoint->hTimeline,
					(psSyncCheckpoint->psSyncCheckpointBlock->ui32FirmwareAddr +
					_SyncCheckpointGetOffset(psSyncCheckpoint)));
		}

		DevmemPDumpLoadMemValue32(psSyncCheckpoint->psSyncCheckpointBlock->hMemDesc,
		                          _SyncCheckpointGetOffset(psSyncCheckpoint),
		                          ui32Status,
		                          PDUMP_FLAGS_CONTINUOUS);
	}
	else
	{
		_SYNC_CHECKPOINT_DEFERRED_SIGNAL *psSyncData;
		OS_SPINLOCK_FLAGS uiFlags;
		IMG_UINT16 ui16NewWriteIdx;

		OSSpinLockAcquire(psDevInfo->hSyncCheckpointSignalSpinLock, uiFlags);

		ui16NewWriteIdx = GET_CP_CB_NEXT_IDX(psDevNode->ui16SyncCPWriteIdx);
		if (ui16NewWriteIdx == psDevNode->ui16SyncCPReadIdx)
		{
			PVR_DPF((PVR_DBG_ERROR,
				"%s: ERROR Deferred SyncCheckpointSignal CB is full)",
				__func__));
		}
		else
		{
			psSyncData = GET_CP_CB_BASE(psDevNode->ui16SyncCPWriteIdx);
			psSyncData->asSyncCheckpoint = *psSyncCheckpoint;
			psSyncData->ui32Status = ui32Status;
			psDevNode->ui16SyncCPWriteIdx = ui16NewWriteIdx;
		}

		OSSpinLockRelease(psDevInfo->hSyncCheckpointSignalSpinLock, uiFlags);

		OSScheduleMISR(psDevNode->pvSyncCPMISR);
	}

	return PVRSRV_OK;
}

static void
MISRHandler_PdumpDeferredSyncSignalPoster(void *pvData)
{
	PPVRSRV_DEVICE_NODE psDevNode = (PPVRSRV_DEVICE_NODE) pvData;
	OS_SPINLOCK_FLAGS uiFlags;
	IMG_UINT16 ui16ReadIdx, ui16WriteIdx;
	_SYNC_CHECKPOINT_DEFERRED_SIGNAL *psSyncData;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	psDevInfo = psDevNode->pvDevice;

	OSLockAcquire(psDevNode->hSyncCheckpointSignalLock);

	OSSpinLockAcquire(psDevInfo->hSyncCheckpointSignalSpinLock, uiFlags);
	/* Snapshot current write and read offset of CB */
	ui16WriteIdx = psDevNode->ui16SyncCPWriteIdx;
	ui16ReadIdx = psDevNode->ui16SyncCPReadIdx;

	OSSpinLockRelease(psDevInfo->hSyncCheckpointSignalSpinLock, uiFlags);
	/* CB is empty */
	if (ui16WriteIdx == ui16ReadIdx)
	{
		OSLockRelease(psDevNode->hSyncCheckpointSignalLock);
		return;
	}
	do
	{
		/* Read item in the CB and flush it to pdump */
		psSyncData = GET_CP_CB_BASE(ui16ReadIdx);
		_SyncCheckpointUpdatePDump(psDevNode, &psSyncData->asSyncCheckpoint, psSyncData->ui32Status, PVRSRV_FENCE_FLAG_NONE);
		ui16ReadIdx = GET_CP_CB_NEXT_IDX(psDevNode->ui16SyncCPReadIdx);
		/* Increment read offset in CB as one item is flushed to pdump */
		OSSpinLockAcquire(psDevInfo->hSyncCheckpointSignalSpinLock, uiFlags);
		psDevNode->ui16SyncCPReadIdx = ui16ReadIdx;
		OSSpinLockRelease(psDevInfo->hSyncCheckpointSignalSpinLock, uiFlags);
		/* Call to this function will flush all the items present in CB
		 * when this function is called i.e. use snapshot of WriteOffset
		 * taken at the beginning in this function and iterate till Write != Read */
	} while (ui16WriteIdx != ui16ReadIdx);

	OSLockRelease(psDevNode->hSyncCheckpointSignalLock);
}

PVRSRV_ERROR PVRSRVSyncCheckpointSignalledPDumpPolKM(PVRSRV_FENCE hFence)
{
	PVRSRV_ERROR eError;
	PSYNC_CHECKPOINT *apsCheckpoints = NULL;
	SYNC_CHECKPOINT *psSyncCheckpoint = NULL;
	IMG_UINT32 i, uiNumCheckpoints = 0;
#if defined(SUPPORT_VALIDATION) && defined(SUPPORT_SOC_TIMER) && defined(NO_HARDWARE) && defined(PDUMP)
	PVRSRV_RGXDEV_INFO *psDevInfo;
#endif

	if (hFence != PVRSRV_NO_FENCE)
	{
		eError = g_psSyncCheckpointPfnStruct->pfnSyncFenceGetCheckpoints(hFence, &uiNumCheckpoints, &apsCheckpoints);
	}
	else
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
	}

	PVR_LOG_RETURN_IF_ERROR(eError, "g_pfnFenceGetCheckpoints");

	if (uiNumCheckpoints)
	{
		/* Flushing deferred fence signals to pdump */
		psSyncCheckpoint = (SYNC_CHECKPOINT *)apsCheckpoints[0];
		MISRHandler_PdumpDeferredSyncSignalPoster(psSyncCheckpoint->psSyncCheckpointBlock->psDevNode);
	}

	for (i=0; i < uiNumCheckpoints; i++)
	{
		psSyncCheckpoint = (SYNC_CHECKPOINT *)apsCheckpoints[i];
		if (psSyncCheckpoint->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_SIGNALLED)
		{
			PDUMPCOMMENTWITHFLAGS(psSyncCheckpoint->psSyncCheckpointBlock->psDevNode,
							psSyncCheckpoint->ui32PDumpFlags,
							"Wait for Fence %s (ID:%d)",
							psSyncCheckpoint->azName,
							psSyncCheckpoint->ui32UID);

			eError = DevmemPDumpDevmemPol32(psSyncCheckpoint->psSyncCheckpointBlock->hMemDesc,
								_SyncCheckpointGetOffset(psSyncCheckpoint),
								PVRSRV_SYNC_CHECKPOINT_SIGNALLED,
								0xFFFFFFFF,
								PDUMP_POLL_OPERATOR_EQUAL,
								psSyncCheckpoint->ui32PDumpFlags);
			PVR_LOG_IF_ERROR(eError, "DevmemPDumpDevmemPol32");
		}
	}

#if defined(SUPPORT_VALIDATION) && defined(SUPPORT_SOC_TIMER) && defined(NO_HARDWARE) && defined(PDUMP)
	/* Sampling of USC timers can only be done after synchronisation for a 3D kick is over */
	if (uiNumCheckpoints)
	{
		psSyncCheckpoint = (SYNC_CHECKPOINT *)apsCheckpoints[0];
		psDevInfo = psSyncCheckpoint->psSyncCheckpointBlock->psDevNode->pvDevice;
		if (psDevInfo->psRGXFWIfFwSysData->ui32ConfigFlags & RGXFWIF_INICFG_VALIDATE_SOCUSC_TIMER)
		{
			RGXValidateSOCUSCTimer(psDevInfo, PDUMP_CONT, 0, 0, NULL);
		}
	}
#endif

	/* Free the memory that was allocated for the sync checkpoint list returned */
	if (apsCheckpoints)
	{
		SyncCheckpointFreeCheckpointListMem(apsCheckpoints);
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR
_SyncCheckpointPDumpTransition(void *pvData, PDUMP_TRANSITION_EVENT eEvent)
{
	_SYNC_CHECKPOINT_CONTEXT *psContext;
	DLLIST_NODE *psNode, *psNext;
	DLLIST_NODE *psNode1, *psNext1;
	PPVRSRV_DEVICE_NODE psDevNode = (PPVRSRV_DEVICE_NODE) pvData;

	if ((eEvent == PDUMP_TRANSITION_EVENT_RANGE_ENTERED) || (eEvent == PDUMP_TRANSITION_EVENT_BLOCK_STARTED))
	{
		OSLockAcquire(psDevNode->hSyncCheckpointContextListLock);
		dllist_foreach_node(&psDevNode->sSyncCheckpointContextListHead, psNode, psNext)
		{
			psContext = IMG_CONTAINER_OF(psNode, _SYNC_CHECKPOINT_CONTEXT, sListNode);

			OSLockAcquire(psContext->hSyncCheckpointBlockListLock);
			dllist_foreach_node(&psContext->sSyncCheckpointBlockListHead, psNode1, psNext1)
			{
				SYNC_CHECKPOINT_BLOCK *psSyncBlk =
					IMG_CONTAINER_OF(psNode1, SYNC_CHECKPOINT_BLOCK, sListNode);
				DevmemPDumpLoadMem(psSyncBlk->hMemDesc,
							   0,
							   psSyncBlk->ui32SyncBlockSize,
							   PDUMP_FLAGS_CONTINUOUS);
			}
			OSLockRelease(psContext->hSyncCheckpointBlockListLock);
		}
		OSLockRelease(psDevNode->hSyncCheckpointContextListLock);
	}

	return PVRSRV_OK;
}
#endif

static void _CheckDeferredCleanupList(_SYNC_CHECKPOINT_CONTEXT *psContext)
{
	_SYNC_CHECKPOINT_CONTEXT_CTL *const psCtxCtl = psContext->psContextCtl;
	PVRSRV_DEVICE_NODE *psDevNode = (PVRSRV_DEVICE_NODE*)psContext->psDevNode;
	DECLARE_DLLIST(sCleanupList);
	DLLIST_NODE *psNode, *psNext;
	OS_SPINLOCK_FLAGS uiFlags;

#if (ENABLE_SYNC_CHECKPOINT_DEFERRED_CLEANUP_DEBUG == 1)
	PVR_DPF((PVR_DBG_WARNING, "%s called", __func__));
#endif

	/* Check the deferred cleanup list and free any sync checkpoints we can */
	OSSpinLockAcquire(psCtxCtl->hDeferredCleanupListLock, uiFlags);

	if (dllist_is_empty(&psCtxCtl->sDeferredCleanupListHead))
	{
		OSSpinLockRelease(psCtxCtl->hDeferredCleanupListLock, uiFlags);
#if (ENABLE_SYNC_CHECKPOINT_DEFERRED_CLEANUP_DEBUG == 1)
		PVR_DPF((PVR_DBG_WARNING, "%s: Defer free list is empty", __func__));
#endif
		/* if list is empty then we have nothing to do here */
		return;
	}

	dllist_foreach_node(&psCtxCtl->sDeferredCleanupListHead, psNode, psNext)
	{
		SYNC_CHECKPOINT *psSyncCheckpointInt =
				IMG_CONTAINER_OF(psNode, SYNC_CHECKPOINT, sDeferredFreeListNode);

		if (psSyncCheckpointInt->psSyncCheckpointFwObj->ui32FwRefCount ==
				(IMG_UINT32)(OSAtomicRead(&psSyncCheckpointInt->hEnqueuedCCBCount)))
		{
			if ((GetInfoPageDebugFlagsKM() & DEBUG_FEATURE_FULL_SYNC_TRACKING_ENABLED)
				&& psSyncCheckpointInt->hRecord)
			{
				PVRSRV_ERROR eError;
				/* remove this sync record */
				eError = _SyncCheckpointRecordRemove(psSyncCheckpointInt->hRecord);
				PVR_LOG_IF_ERROR(eError, "_SyncCheckpointRecordRemove");
			}

			/* Move the sync checkpoint from the deferred free list to local list */
			dllist_remove_node(&psSyncCheckpointInt->sDeferredFreeListNode);
			/* It's not an ideal solution to traverse list of checkpoints-to-free
			 * twice but it allows us to avoid holding the lock for too long */
			dllist_add_to_tail(&sCleanupList, &psSyncCheckpointInt->sDeferredFreeListNode);
		}
#if (ENABLE_SYNC_CHECKPOINT_DEFERRED_CLEANUP_DEBUG == 1)
		else
		{
			PVR_DPF((PVR_DBG_WARNING, "%s psSyncCheckpoint '%s'' (ID:%d)<%p>), "
			        "still pending (enq=%d,FWRef=%d)", __func__,
			        psSyncCheckpointInt->azName, psSyncCheckpointInt->ui32UID,
			        (void*)psSyncCheckpointInt,
			        (IMG_UINT32)(OSAtomicRead(&psSyncCheckpointInt->hEnqueuedCCBCount)),
			        psSyncCheckpointInt->psSyncCheckpointFwObj->ui32FwRefCount));
		}
#endif
	}

	OSSpinLockRelease(psCtxCtl->hDeferredCleanupListLock, uiFlags);

	dllist_foreach_node(&sCleanupList, psNode, psNext) {
		SYNC_CHECKPOINT *psSyncCheckpointInt =
				IMG_CONTAINER_OF(psNode, SYNC_CHECKPOINT, sDeferredFreeListNode);

		/* Remove the sync checkpoint from the global list */
		OSSpinLockAcquire(psDevNode->hSyncCheckpointListLock, uiFlags);
		dllist_remove_node(&psSyncCheckpointInt->sListNode);
		OSSpinLockRelease(psDevNode->hSyncCheckpointListLock, uiFlags);

		RGXSRV_HWPERF_FREE(psDevNode, SYNC_CP, psSyncCheckpointInt->ui32FWAddr);

#if (SYNC_CHECKPOINT_POOL_SIZE > 0)
#if (ENABLE_SYNC_CHECKPOINT_DEFERRED_CLEANUP_DEBUG == 1)
		PVR_DPF((PVR_DBG_WARNING,
		        "%s attempting to return sync(ID:%d),%p> to pool",
		        __func__,
		        psSyncCheckpointInt->ui32UID,
		        (void *) psSyncCheckpointInt));
#endif
		if (!_PutCheckpointInPool(psSyncCheckpointInt))
#endif
		{
#if (SYNC_CHECKPOINT_POOL_SIZE > 0)
#if (ENABLE_SYNC_CHECKPOINT_DEFERRED_CLEANUP_DEBUG == 1)
			PVR_DPF((PVR_DBG_WARNING, "%s pool is full, so just free it",
			        __func__));
#endif
#endif
#if (ENABLE_SYNC_CHECKPOINT_DEFERRED_CLEANUP_DEBUG == 1)
		else
		{
			PVR_DPF((PVR_DBG_WARNING,
			       "%s psSyncCheckpoint '%s'' (ID:%d)<%p>), still pending (enq=%d,FWRef=%d)",
			        __func__,
			        psSyncCheckpointInt->azName,
			        psSyncCheckpointInt->ui32UID,
			        (void*)psSyncCheckpointInt,
			        (IMG_UINT32)(OSAtomicRead(&psSyncCheckpointInt->hEnqueuedCCBCount)),
			        psSyncCheckpointInt->psSyncCheckpointFwObj->ui32FwRefCount));
#endif
			_FreeSyncCheckpoint(psSyncCheckpointInt);
		}
	}
}

#if (SYNC_CHECKPOINT_POOL_SIZE > 0)
static SYNC_CHECKPOINT *_GetCheckpointFromPool(_SYNC_CHECKPOINT_CONTEXT *psContext)
{
	_SYNC_CHECKPOINT_CONTEXT_CTL *const psCtxCtl = psContext->psContextCtl;
	SYNC_CHECKPOINT *psSyncCheckpoint = NULL;
	OS_SPINLOCK_FLAGS uiFlags;

	/* Acquire sync checkpoint pool lock */
	OSSpinLockAcquire(psCtxCtl->hSyncCheckpointPoolLock, uiFlags);

	/* Check if we can allocate from the pool */
	if (psCtxCtl->bSyncCheckpointPoolValid &&
	    (psCtxCtl->ui32SyncCheckpointPoolCount > SYNC_CHECKPOINT_POOL_SEDIMENT) &&
	    (psCtxCtl->ui32SyncCheckpointPoolWp != psCtxCtl->ui32SyncCheckpointPoolRp))
	{
		/* Get the next sync checkpoint from the pool */
		psSyncCheckpoint = psCtxCtl->psSyncCheckpointPool[psCtxCtl->ui32SyncCheckpointPoolRp];
		psCtxCtl->ui32SyncCheckpointPoolRp =
		        (psCtxCtl->ui32SyncCheckpointPoolRp + 1) & SYNC_CHECKPOINT_POOL_MASK;
		psCtxCtl->ui32SyncCheckpointPoolCount--;
		psCtxCtl->bSyncCheckpointPoolFull = IMG_FALSE;
		psSyncCheckpoint->ui32ValidationCheck = SYNC_CHECKPOINT_PATTERN_IN_USE;
#if (ENABLE_SYNC_CHECKPOINT_POOL_DEBUG == 1)
		PVR_DPF((PVR_DBG_WARNING,
		        "%s checkpoint(old ID:%d)<-POOL(%d/%d), psContext=<%p>, "
		        "poolRp=%d, poolWp=%d",
		        __func__,
		        psSyncCheckpoint->ui32UID,
		        psCtxCtl->ui32SyncCheckpointPoolCount,
		        SYNC_CHECKPOINT_POOL_SIZE,
		        (void *) psContext,
		        psCtxCtl->ui32SyncCheckpointPoolRp,
		        psCtxCtl->ui32SyncCheckpointPoolWp));
#endif
	}
	/* Release sync checkpoint pool lock */
	OSSpinLockRelease(psCtxCtl->hSyncCheckpointPoolLock, uiFlags);

	return psSyncCheckpoint;
}

static IMG_BOOL _PutCheckpointInPool(SYNC_CHECKPOINT *psSyncCheckpoint)
{
	_SYNC_CHECKPOINT_CONTEXT *psContext = psSyncCheckpoint->psSyncCheckpointBlock->psContext;
	_SYNC_CHECKPOINT_CONTEXT_CTL *const psCtxCtl = psContext->psContextCtl;
	IMG_BOOL bReturnedToPool = IMG_FALSE;
	OS_SPINLOCK_FLAGS uiFlags;

	/* Acquire sync checkpoint pool lock */
	OSSpinLockAcquire(psCtxCtl->hSyncCheckpointPoolLock, uiFlags);

	/* Check if pool has space */
	if (psCtxCtl->bSyncCheckpointPoolValid && !psCtxCtl->bSyncCheckpointPoolFull)
	{
		/* Put the sync checkpoint into the next write slot in the pool */
		psCtxCtl->psSyncCheckpointPool[psCtxCtl->ui32SyncCheckpointPoolWp] = psSyncCheckpoint;
		psCtxCtl->ui32SyncCheckpointPoolWp =
		        (psCtxCtl->ui32SyncCheckpointPoolWp + 1) & SYNC_CHECKPOINT_POOL_MASK;
		psCtxCtl->ui32SyncCheckpointPoolCount++;
		psCtxCtl->bSyncCheckpointPoolFull =
		        ((psCtxCtl->ui32SyncCheckpointPoolCount > 0) &&
		        (psCtxCtl->ui32SyncCheckpointPoolWp == psCtxCtl->ui32SyncCheckpointPoolRp));
		bReturnedToPool = IMG_TRUE;
		psSyncCheckpoint->psSyncCheckpointFwObj->ui32State = PVRSRV_SYNC_CHECKPOINT_UNDEF;
		psSyncCheckpoint->ui32ValidationCheck = SYNC_CHECKPOINT_PATTERN_IN_POOL;
#if (ENABLE_SYNC_CHECKPOINT_POOL_DEBUG == 1)
		PVR_DPF((PVR_DBG_WARNING,
		        "%s checkpoint(ID:%d)->POOL(%d/%d), poolRp=%d, poolWp=%d",
		        __func__,
		        psSyncCheckpoint->ui32UID,
		        psCtxCtl->ui32SyncCheckpointPoolCount,
		        SYNC_CHECKPOINT_POOL_SIZE,
		        psCtxCtl->ui32SyncCheckpointPoolRp,
		        psCtxCtl->ui32SyncCheckpointPoolWp));
#endif
	}
	/* Release sync checkpoint pool lock */
	OSSpinLockRelease(psCtxCtl->hSyncCheckpointPoolLock, uiFlags);

	return bReturnedToPool;
}

static IMG_UINT32 _CleanCheckpointPool(_SYNC_CHECKPOINT_CONTEXT *psContext)
{
	_SYNC_CHECKPOINT_CONTEXT_CTL *const psCtxCtl = psContext->psContextCtl;
	SYNC_CHECKPOINT *psCheckpoint = NULL;
	DECLARE_DLLIST(sCleanupList);
	DLLIST_NODE *psThis, *psNext;
	OS_SPINLOCK_FLAGS uiFlags;
	IMG_UINT32 ui32ItemsFreed = 0, ui32NullScpCount = 0, ui32PoolCount;
	IMG_BOOL bPoolValid;

	/* Acquire sync checkpoint pool lock */
	OSSpinLockAcquire(psCtxCtl->hSyncCheckpointPoolLock, uiFlags);

	bPoolValid = psCtxCtl->bSyncCheckpointPoolValid;
	ui32PoolCount = psCtxCtl->ui32SyncCheckpointPoolCount;

	/* While the pool still contains sync checkpoints, free them */
	while (bPoolValid && psCtxCtl->ui32SyncCheckpointPoolCount > 0)
	{
		/* Get the sync checkpoint from the next read slot in the pool */
		psCheckpoint = psCtxCtl->psSyncCheckpointPool[psCtxCtl->ui32SyncCheckpointPoolRp];
		psCtxCtl->ui32SyncCheckpointPoolRp =
		        (psCtxCtl->ui32SyncCheckpointPoolRp + 1) & SYNC_CHECKPOINT_POOL_MASK;
		psCtxCtl->ui32SyncCheckpointPoolCount--;
		psCtxCtl->bSyncCheckpointPoolFull =
		        ((psCtxCtl->ui32SyncCheckpointPoolCount > 0) &&
		        (psCtxCtl->ui32SyncCheckpointPoolWp == psCtxCtl->ui32SyncCheckpointPoolRp));

		if (psCheckpoint)
		{
			PVR_ASSERT(!dllist_node_is_in_list(&psCheckpoint->sListNode));
			/* before checkpoints are added to the pool they are removed
			 * from the list so it's safe to use sListNode here */
			dllist_add_to_head(&sCleanupList, &psCheckpoint->sListNode);
		}
		else
		{
			ui32NullScpCount++;
		}
	}

	/* Release sync checkpoint pool lock */
	OSSpinLockRelease(psCtxCtl->hSyncCheckpointPoolLock, uiFlags);

	/* go through the local list and free all of the sync checkpoints */

#if (ENABLE_SYNC_CHECKPOINT_POOL_DEBUG == 1)
	PVR_DPF((PVR_DBG_WARNING, "%s psContext=<%p>, bSyncCheckpointPoolValid=%d, "
	        "uiSyncCheckpointPoolCount=%d", __func__, (void *) psContext,
	        bPoolValid, ui32PoolCount));

	if (ui32NullScpCount > 0)
	{
		PVR_DPF((PVR_DBG_WARNING, "%s pool contained %u NULL entries", __func__,
		        ui32NullScpCount));
	}
#endif

	dllist_foreach_node(&sCleanupList, psThis, psNext)
	{
		psCheckpoint = IMG_CONTAINER_OF(psThis, SYNC_CHECKPOINT, sListNode);

#if (ENABLE_SYNC_CHECKPOINT_POOL_DEBUG == 1)
		if (psCheckpoint->ui32ValidationCheck != SYNC_CHECKPOINT_PATTERN_IN_POOL)
		{
			PVR_DPF((PVR_DBG_WARNING, "%s pool contains invalid entry "
			        "(ui32ValidationCheck=0x%x)", __func__,
			        psCheckpoint->ui32ValidationCheck));
		}

		PVR_DPF((PVR_DBG_WARNING,
		        "%s psSyncCheckpoint(ID:%d)",
		        __func__, psCheckpoint->ui32UID));
		PVR_DPF((PVR_DBG_WARNING,
		        "%s psSyncCheckpoint->ui32ValidationCheck=0x%x",
		        __func__, psCheckpoint->ui32ValidationCheck));
		PVR_DPF((PVR_DBG_WARNING,
		        "%s psSyncCheckpoint->uiSpanAddr=0x%llx",
		        __func__, psCheckpoint->uiSpanAddr));
		PVR_DPF((PVR_DBG_WARNING,
		        "%s psSyncCheckpoint->psSyncCheckpointBlock=<%p>",
		        __func__, (void *) psCheckpoint->psSyncCheckpointBlock));
		PVR_DPF((PVR_DBG_WARNING,
		        "%s psSyncCheckpoint->psSyncCheckpointBlock->psContext=<%p>",
		        __func__, (void *) psCheckpoint->psSyncCheckpointBlock->psContext));
		PVR_DPF((PVR_DBG_WARNING,
		        "%s psSyncCheckpoint->psSyncCheckpointBlock->psContext->psSubAllocRA=<%p>",
		        __func__, (void *) psCheckpoint->psSyncCheckpointBlock->psContext->psSubAllocRA));

		PVR_DPF((PVR_DBG_WARNING,
		        "%s CALLING RA_Free(psSyncCheckpoint(ID:%d)<%p>), "
		        "psSubAllocRA=<%p>, ui32SpanAddr=0x%llx",
		        __func__,
		        psCheckpoint->ui32UID,
		        (void *) psCheckpoint,
		        (void *) psCheckpoint->psSyncCheckpointBlock->psContext->psSubAllocRA,
		        psCheckpoint->uiSpanAddr));
#endif

		dllist_remove_node(psThis);

		_FreeSyncCheckpoint(psCheckpoint);
		ui32ItemsFreed++;
	}

	return ui32ItemsFreed;
}
#endif /* (SYNC_CHECKPOINT_POOL_SIZE > 0) */
