/*************************************************************************/ /*!
@File
@Title          Services synchronisation checkpoint interface
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements server side code for services synchronisation
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
#include "lock.h"
#include "log2.h"
#include "pvrsrv.h"
#include "pdump_km.h"

#include "pvrsrv_sync_km.h"

#define SYNC_CHECKPOINT_BLOCK_LIST_CHUNK_SIZE  10
#define LOCAL_SYNC_CHECKPOINT_RESET_VALUE      PVRSRV_SYNC_CHECKPOINT_NOT_SIGNALLED

/*
	This defines the maximum amount of synchronisation memory
	that can be allocated per sync checkpoint context.
	In reality this number is meaningless as we would run out
	of synchronisation memory before we reach this limit, but
	we need to provide a size to the span RA.
*/
#define MAX_SYNC_CHECKPOINT_MEM  (4 * 1024 * 1024)

/* definitions for functions to be implemented by OS-specific sync - the OS-specific sync code
   will call x when initialised, in order to register functions we can then call */
typedef PVRSRV_ERROR (*PFN_SYNC_CHECKPOINT_FENCE_RESOLVE_FN)(PVRSRV_FENCE_KM fence, IMG_UINT32 *nr_checkpoints, PSYNC_CHECKPOINT *checkpoint_handles);
typedef PVRSRV_ERROR (*PFN_SYNC_CHECKPOINT_FENCE_CREATE_FN)(const IMG_CHAR *fence_name, PVRSRV_TIMELINE_KM timeline, PVRSRV_FENCE_KM *new_fence, PSYNC_CHECKPOINT *new_checkpoint_handle);


typedef struct _SYNC_CHECKPOINT_BLOCK_LIST_
{
	IMG_UINT32            ui32BlockCount;            /*!< Number of contexts in the list */
	IMG_UINT32            ui32BlockListSize;         /*!< Size of the array contexts */
	SYNC_CHECKPOINT_BLOCK **papsSyncCheckpointBlock; /*!< Array of sync checkpoint blocks */
} SYNC_CHECKPOINT_BLOCK_LIST;

#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
#define DECREMENT_WITH_WRAP(value, sz) ((value) ? ((value) - 1) : ((sz) - 1))

struct SYNC_CHECKPOINT_RECORD
{
	SYNC_CHECKPOINT_BLOCK	*psSyncCheckpointBlock;	/*!< handle to SYNC_CHECKPOINT_BLOCK */
	IMG_UINT32				ui32SyncOffset; 		/*!< offset to sync in block */
	IMG_UINT32				ui32FwBlockAddr;
	IMG_PID					uiPID;
	IMG_UINT64				ui64OSTime;
	DLLIST_NODE				sNode;
	IMG_CHAR				szClassName[SYNC_MAX_CLASS_NAME_LEN];
};
#endif /* defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING) */

static PFN_SYNC_CHECKPOINT_FENCE_RESOLVE_FN g_pfnFenceResolve = NULL;
static PFN_SYNC_CHECKPOINT_FENCE_CREATE_FN g_pfnFenceCreate = NULL;

PVRSRV_ERROR
SyncCheckpointRecordAdd(PSYNC_CHECKPOINT_RECORD_HANDLE *phRecord,
                        SYNC_CHECKPOINT_BLOCK *hSyncCheckpointBlock,
                        IMG_UINT32 ui32FwBlockAddr,
                        IMG_UINT32 ui32SyncOffset,
                        IMG_UINT32 ui32ClassNameSize,
                        const IMG_CHAR *pszClassName);
PVRSRV_ERROR
SyncCheckpointRecordRemove(PSYNC_CHECKPOINT_RECORD_HANDLE hRecord);
static void _SyncCheckpointState(PDLLIST_NODE psNode,
					DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile);
static void _SyncCheckpointDebugRequest(PVRSRV_DBGREQ_HANDLE hDebugRequestHandle,
					IMG_UINT32 ui32VerbLevel,
					DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile);
static PVRSRV_ERROR _SyncCheckpointRecordListInit(_SYNC_CHECKPOINT_CONTEXT *psContext);
static void _SyncCheckpointRecordListDeinit(_SYNC_CHECKPOINT_CONTEXT *psContext);

PVRSRV_ERROR SyncCheckpointSignalPDump(_SYNC_CHECKPOINT *psSyncCheckpoint);
PVRSRV_ERROR SyncCheckpointErrorPDump(_SYNC_CHECKPOINT *psSyncCheckpoint);

PVRSRV_ERROR SyncCheckpointRegisterFunctions(PFN_SYNC_CHECKPOINT_FENCE_RESOLVE_FN pfnFenceResolve,
                                             PFN_SYNC_CHECKPOINT_FENCE_CREATE_FN pfnFenceCreate);

/* Unique incremental ID assigned to sync checkpoints when allocated */
static IMG_UINT32 g_SyncCheckpointUID = 0;

/*
	Internal interfaces for management of _SYNC_CHECKPOINT_CONTEXT
*/
static void
_SyncCheckpointContextUnref(_SYNC_CHECKPOINT_CONTEXT *psContext)
{
	if (!OSAtomicRead(&psContext->hRefCount))
	{
		PVR_LOG_ERROR(PVRSRV_ERROR_INVALID_CONTEXT, "_SyncCheckpointContextUnref context already freed");
	}
	else if (0 == OSAtomicDecrement(&psContext->hRefCount))
	{
		/* SyncCheckpointContextDestroy only when no longer referenced */
		_SyncCheckpointRecordListDeinit(psContext);
		PVRSRVUnregisterDbgRequestNotify(psContext->hCheckpointNotify);
		OSLockDestroy(psContext->hCheckpointListLock);
		RA_Delete(psContext->psSpanRA);
		RA_Delete(psContext->psSubAllocRA);
		OSFreeMem(psContext);
	}
}

static void
_SyncCheckpointContextRef(_SYNC_CHECKPOINT_CONTEXT *psContext)
{
	if (!OSAtomicRead(&psContext->hRefCount))
	{
		PVR_LOG_ERROR(PVRSRV_ERROR_INVALID_CONTEXT, "_SyncCheckpointContextRef context use after free");
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
_AllocSyncCheckpointBlock(_SYNC_CHECKPOINT_CONTEXT *psContext,
						  SYNC_CHECKPOINT_BLOCK    **ppsSyncBlock)
{
	PVRSRV_DEVICE_NODE *psDevNode;
	SYNC_CHECKPOINT_BLOCK *psSyncBlk;
	PVRSRV_ERROR eError;

	psSyncBlk = OSAllocMem(sizeof(SYNC_CHECKPOINT_BLOCK));
	if (psSyncBlk == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		PVR_LOG_ERROR(eError, "OSAllocMem");
		goto fail_alloc;
	}
	psSyncBlk->psContext = psContext;

	/* Allocate sync checkpoint block */
	psDevNode = psContext->psDevNode;
	if (!psDevNode)
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		PVR_LOG_ERROR(eError, "context device node invalid");
		goto fail_alloc_ufo_block;
	}
	psSyncBlk->psDevNode = psDevNode;

	eError = psDevNode->pfnAllocUFOBlock(psDevNode,
										 &psSyncBlk->hMemDesc,
										 &psSyncBlk->ui32FirmwareAddr,
										 &psSyncBlk->ui32SyncBlockSize);
	if (eError != PVRSRV_OK)
	{
		PVR_LOG_ERROR(eError, "failed to allocate ufo block");
		goto fail_alloc_ufo_block;
	}

	eError = DevmemAcquireCpuVirtAddr(psSyncBlk->hMemDesc,
									  (void **) &psSyncBlk->pui32LinAddr);
	if (eError != PVRSRV_OK)
	{
		PVR_LOG_ERROR(eError, "DevmemAcquireCpuVirtAddr");
		goto fail_devmem_acquire;
	}

	OSAtomicWrite(&psSyncBlk->hRefCount, 1);

	PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS,
						  "Allocated Sync Checkpoint UFO block (FirmwareVAddr = 0x%08x)",
						  psSyncBlk->ui32FirmwareAddr);

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
	if (0 == OSAtomicDecrement(&psSyncBlk->hRefCount))
	{
		PVRSRV_DEVICE_NODE *psDevNode = psSyncBlk->psDevNode;

		DevmemReleaseCpuVirtAddr(psSyncBlk->hMemDesc);
		psDevNode->pfnFreeUFOBlock(psDevNode, psSyncBlk->hMemDesc);
		OSFreeMem(psSyncBlk);
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

	PVR_LOG_IF_FALSE((hArena != NULL), "hArena is NULL");

	/* Check we've not be called with an unexpected size */
	PVR_LOG_IF_FALSE((uSize == sizeof(_SYNC_CHECKPOINT_FW_OBJ)), "uiSize is not the size of _SYNC_CHECKPOINT_FW_OBJ");

	/*
		Ensure the sync checkpoint context doesn't go away while we have sync blocks
		attached to it
	*/
	_SyncCheckpointContextRef(psContext);

	/* Allocate the block of memory */
	eError = _AllocSyncCheckpointBlock(psContext, &psSyncBlock);
	if (eError != PVRSRV_OK)
	{
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
	PVR_LOG_IF_FALSE((uiSpanSize == psSyncBlock->ui32SyncBlockSize), "uiSpanSize invalid");

	*puiBase = psSyncBlock->uiSpanBase;
	*puiActualSize = psSyncBlock->ui32SyncBlockSize;
	*phImport = psSyncBlock;
	return PVRSRV_OK;

fail_spanalloc:
	_FreeSyncCheckpointBlock(psSyncBlock);
fail_syncblockalloc:
	_SyncCheckpointContextUnref(psContext);

	return eError;
}

static void
_SyncCheckpointBlockUnimport(RA_PERARENA_HANDLE hArena,
                             RA_BASE_T uiBase,
                             RA_PERISPAN_HANDLE hImport)
{
	_SYNC_CHECKPOINT_CONTEXT *psContext = hArena;
	SYNC_CHECKPOINT_BLOCK   *psSyncBlock = hImport;

	PVR_LOG_IF_FALSE((psContext != NULL), "hArena invalid");
	PVR_LOG_IF_FALSE((psSyncBlock != NULL), "hImport invalid");
	PVR_LOG_IF_FALSE((uiBase == psSyncBlock->uiSpanBase), "uiBase invalid");

	/* Free the span this import is using */
	RA_Free(psContext->psSpanRA, uiBase);

	/* Free the sync checkpoint block */
	_FreeSyncCheckpointBlock(psSyncBlock);

	/*	Drop our reference to the sync checkpoint context */
	_SyncCheckpointContextUnref(psContext);
}

static INLINE IMG_UINT32 _SyncCheckpointGetOffset(_SYNC_CHECKPOINT *psSyncInt)
{
	IMG_UINT64 ui64Temp;
	
	ui64Temp =  psSyncInt->uiSpanAddr - psSyncInt->psSyncCheckpointBlock->uiSpanBase;
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

IMG_INTERNAL PVRSRV_ERROR
SyncCheckpointRegisterFunctions(PFN_SYNC_CHECKPOINT_FENCE_RESOLVE_FN pfnFenceResolve, PFN_SYNC_CHECKPOINT_FENCE_CREATE_FN pfnFenceCreate)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	g_pfnFenceResolve = pfnFenceResolve;
	g_pfnFenceCreate = pfnFenceCreate;

	return eError;
}
IMG_INTERNAL PVRSRV_ERROR
SyncCheckpointResolveFence(PVRSRV_FENCE_KM hFence, IMG_UINT32 *pui32NumSyncCheckpoints, PSYNC_CHECKPOINT *psSyncCheckpoints)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (!g_pfnFenceResolve)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: ERROR (eError=PVRSRV_ERROR_SYNC_NATIVESYNC_NOT_REGISTERED)", __FUNCTION__));
		PVR_LOG_ERROR(eError, "g_pfnFenceResolve is NULL");
		eError = PVRSRV_ERROR_SYNC_NATIVESYNC_NOT_REGISTERED;
	}
	else
	{
		eError = g_pfnFenceResolve(hFence, pui32NumSyncCheckpoints, psSyncCheckpoints);
	}
	return eError;
}
IMG_INTERNAL PVRSRV_ERROR
SyncCheckpointCreateFence(const IMG_CHAR *pszFenceName, PVRSRV_TIMELINE_KM hTimeline, PVRSRV_FENCE_KM *phNewFence, PSYNC_CHECKPOINT *psNewSyncCheckpoint)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (!g_pfnFenceCreate)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: ERROR (eError=PVRSRV_ERROR_SYNC_NATIVESYNC_NOT_REGISTERED)", __FUNCTION__));
		PVR_LOG_ERROR(eError, "g_pfnFenceCreate is NULL");
		eError = PVRSRV_ERROR_SYNC_NATIVESYNC_NOT_REGISTERED;
	}
	else
	{
		eError = g_pfnFenceCreate(pszFenceName, hTimeline, phNewFence, psNewSyncCheckpoint);
	}
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR
SyncCheckpointContextCreate(PVRSRV_DEVICE_NODE *psDevNode,
							PSYNC_CHECKPOINT_CONTEXT *ppsSyncCheckpointContext)
{
	_SYNC_CHECKPOINT_CONTEXT *psContext = NULL;
	PVRSRV_ERROR eError;

	PVR_LOGR_IF_FALSE((ppsSyncCheckpointContext != NULL), "ppsSyncCheckpointContext invalid", PVRSRV_ERROR_INVALID_PARAMS);

	psContext = OSAllocZMem(sizeof(_SYNC_CHECKPOINT_CONTEXT));
	if (psContext == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc;
	}

	psContext->psDevNode = psDevNode;

	OSSNPrintf(psContext->azName, SYNC_CHECKPOINT_NAME_SIZE, "Sync Prim RA-%p", psContext);
	OSSNPrintf(psContext->azSpanName, SYNC_CHECKPOINT_NAME_SIZE, "Sync Prim span RA-%p", psContext);

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

	if (!RA_Add(psContext->psSpanRA, 0, MAX_SYNC_CHECKPOINT_MEM, 0, NULL))
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_span_add;
	}

	OSAtomicWrite(&psContext->hRefCount, 1);
	OSAtomicWrite(&psContext->hCheckpointCount, 0);

	eError = OSLockCreate(&psContext->hCheckpointListLock, LOCK_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		goto fail_span;
	}

	dllist_init(&psContext->sCheckpointList);

	eError = PVRSRVRegisterDbgRequestNotify(&psContext->hCheckpointNotify,
											psDevNode,
											_SyncCheckpointDebugRequest,
											DEBUG_REQUEST_SYNCCHECKPOINT,
											psContext);
	if (eError != PVRSRV_OK)
	{
		goto fail_register_dbg_request;
	}

	eError = _SyncCheckpointRecordListInit(psContext);
	if (eError != PVRSRV_OK)
	{
		goto fail_record_list_init;
	}

	*ppsSyncCheckpointContext = (PSYNC_CHECKPOINT_CONTEXT)psContext;
	return PVRSRV_OK;

fail_record_list_init:
	PVRSRVUnregisterDbgRequestNotify(psContext->hCheckpointNotify);
fail_register_dbg_request:
	OSLockDestroy(psContext->hCheckpointListLock);
fail_span_add:
	RA_Delete(psContext->psSpanRA);
fail_span:
	RA_Delete(psContext->psSubAllocRA);
fail_suballoc:
	OSFreeMem(psContext);
fail_alloc:
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR SyncCheckpointContextDestroy(PSYNC_CHECKPOINT_CONTEXT psSyncCheckpointContext)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	_SYNC_CHECKPOINT_CONTEXT *psContext = (_SYNC_CHECKPOINT_CONTEXT*)psSyncCheckpointContext;
	IMG_INT iRf = 0;

	PVR_LOG_IF_FALSE((psSyncCheckpointContext != NULL), "psSyncCheckpointContext invalid");

	iRf = OSAtomicRead(&psContext->hCheckpointCount);
	if (iRf != 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s <%p> attempted with active references (iRf=%d), may be the result of a race", __FUNCTION__, (void*)psContext, iRf));
		eError = PVRSRV_ERROR_UNABLE_TO_DESTROY_CONTEXT;
	}
	else
	{
		IMG_INT iRf2 = 0;

		iRf2 = OSAtomicRead(&psContext->hRefCount);
		_SyncCheckpointContextUnref(psContext);
	}
	return eError;
}

IMG_INTERNAL
PVRSRV_ERROR
SyncCheckpointAlloc(PSYNC_CHECKPOINT_CONTEXT psSyncContext,
                    const IMG_CHAR *pszCheckpointName,
                    PSYNC_CHECKPOINT *ppsSyncCheckpoint)
{
	_SYNC_CHECKPOINT *psNewSyncCheckpoint = NULL;
	_SYNC_CHECKPOINT_CONTEXT *psSyncContextInt = (_SYNC_CHECKPOINT_CONTEXT*)psSyncContext;
	PVRSRV_ERROR eError;

	PVR_LOGR_IF_FALSE((psSyncContext != NULL), "psSyncContext invalid", PVRSRV_ERROR_INVALID_PARAMS);
	PVR_LOGR_IF_FALSE((ppsSyncCheckpoint != NULL), "ppsSyncCheckpoint invalid", PVRSRV_ERROR_INVALID_PARAMS);

	psNewSyncCheckpoint = OSAllocMem(sizeof(*psNewSyncCheckpoint));

	if (psNewSyncCheckpoint == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		PVR_LOG_ERROR(eError, "OSAllocMem");
		goto fail_alloc;
	}

	eError = RA_Alloc(psSyncContextInt->psSubAllocRA,
	                  sizeof(_SYNC_CHECKPOINT_FW_OBJ),
	                  RA_NO_IMPORT_MULTIPLIER,
	                  0,
	                  sizeof(IMG_UINT32),
	                  (IMG_CHAR*)pszCheckpointName,
	                  &psNewSyncCheckpoint->uiSpanAddr,
	                  NULL,
	                  (RA_PERISPAN_HANDLE *) &psNewSyncCheckpoint->psSyncCheckpointBlock);
	if (PVRSRV_OK != eError)
	{
		PVR_LOG_ERROR(eError, "RA_Alloc");
		goto fail_raalloc;
	}
	psNewSyncCheckpoint->psSyncCheckpointFwObj = (volatile _SYNC_CHECKPOINT_FW_OBJ*)(psNewSyncCheckpoint->psSyncCheckpointBlock->pui32LinAddr +
	                                                                                 (_SyncCheckpointGetOffset(psNewSyncCheckpoint)/sizeof(IMG_UINT32)));

	/* the allocation of the backing memory for the sync prim block
	* is done with ZERO_ON_ALLOC so the memory is initially all zero.
	* States are also reset to unsignalled on free, so no need to set here
	*/
	OSAtomicWrite(&psNewSyncCheckpoint->hRefCount, 1);
	OSAtomicWrite(&psNewSyncCheckpoint->hEnqueuedCCBCount, 0);

	if(pszCheckpointName)
	{
		/* Copy over the checkpoint name annotation */
		OSStringNCopy(psNewSyncCheckpoint->azName, pszCheckpointName, SYNC_CHECKPOINT_NAME_SIZE);
		psNewSyncCheckpoint->azName[SYNC_CHECKPOINT_NAME_SIZE-1] = 0;
	}
	else
	{
		/* No sync checkpoint name annotation */
		psNewSyncCheckpoint->azName[0] = '\0';
	}

#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
	{
		IMG_CHAR szChkptName[SYNC_CHECKPOINT_MAX_CLASS_NAME_LEN];

		if(pszCheckpointName)
		{
			/* Copy the checkpoint name annotation into a fixed-size array */
			OSStringNCopy(szChkptName, pszCheckpointName, SYNC_CHECKPOINT_MAX_CLASS_NAME_LEN - 1);
			szChkptName[SYNC_MAX_CLASS_NAME_LEN - 1] = 0;
		}
		else
		{
			/* No checkpoint name annotation */
			szChkptName[0] = 0;
		}
		/* record this sync */
		eError = SyncCheckpointRecordAdd(&psNewSyncCheckpoint->hRecord,
		                                 psNewSyncCheckpoint->psSyncCheckpointBlock,
		                                 psNewSyncCheckpoint->psSyncCheckpointBlock->ui32FirmwareAddr,
		                                 _SyncCheckpointGetOffset(psNewSyncCheckpoint),
		                                 OSStringNLength(szChkptName, SYNC_CHECKPOINT_MAX_CLASS_NAME_LEN),
		                                 szChkptName);
		if (eError != PVRSRV_OK)
		{
			PVR_LOG_ERROR(eError, "SyncCheckpointRecordAdd");
		}
	}
#else
	PVR_UNREFERENCED_PARAMETER(pszCheckpointName);
#endif /* if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING) */

	OSAtomicIncrement(&psNewSyncCheckpoint->psSyncCheckpointBlock->psContext->hCheckpointCount);

	/* Assign unique ID to this sync checkpoint */
	psNewSyncCheckpoint->ui32UID = g_SyncCheckpointUID++;

	/* Add the sync checkpoint to the context list */
	OSLockAcquire(psSyncContextInt->hCheckpointListLock);
	dllist_add_to_head(&psSyncContextInt->sCheckpointList,
	                   &psNewSyncCheckpoint->sListNode);
	OSLockRelease(psSyncContextInt->hCheckpointListLock);

	*ppsSyncCheckpoint = (PSYNC_CHECKPOINT)psNewSyncCheckpoint;

	return PVRSRV_OK;

fail_raalloc:
	OSFreeMem(psNewSyncCheckpoint);
fail_alloc:

	return eError;
}

IMG_INTERNAL void SyncCheckpointFree(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	_SYNC_CHECKPOINT *psSyncCheckpointInt = (_SYNC_CHECKPOINT*)psSyncCheckpoint;
	_SYNC_CHECKPOINT_CONTEXT *psContext = psSyncCheckpointInt->psSyncCheckpointBlock->psContext;

	PVR_LOG_IF_FALSE((psSyncCheckpoint != NULL), "psSyncCheckpoint invalid");

	if (!OSAtomicRead(&psSyncCheckpointInt->hRefCount))
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncCheckpointUnref sync checkpoint already freed"));
	}
	else if (0 == OSAtomicDecrement(&psSyncCheckpointInt->hRefCount))
	{
		/* If the firmware has serviced all enqueued references to the sync checkpoint, free it */
		if (psSyncCheckpointInt->psSyncCheckpointFwObj->ui32FwRefCount == (IMG_UINT32)(OSAtomicRead(&psSyncCheckpointInt->hEnqueuedCCBCount)))
		{
#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
			{
				PVRSRV_ERROR eError;
				/* remove this sync record */
				eError = SyncCheckpointRecordRemove(psSyncCheckpointInt->hRecord);
				PVR_LOG_IF_ERROR(eError, "SyncCheckpointRecordRemove");
			}
#endif
			/* Remove the sync checkpoint from the global list */
			OSLockAcquire(psContext->hCheckpointListLock);
			dllist_remove_node(&psSyncCheckpointInt->sListNode);
			OSLockRelease(psContext->hCheckpointListLock);

			OSAtomicDecrement(&psSyncCheckpointInt->psSyncCheckpointBlock->psContext->hCheckpointCount);
			RA_Free(psSyncCheckpointInt->psSyncCheckpointBlock->psContext->psSubAllocRA, psSyncCheckpointInt->uiSpanAddr);
		}
	}
}

IMG_INTERNAL void
SyncCheckpointSignal(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	_SYNC_CHECKPOINT *psSyncCheckpointInt = (_SYNC_CHECKPOINT*)psSyncCheckpoint;

	PVR_LOG_IF_FALSE((psSyncCheckpoint != NULL), "psSyncCheckpoint invalid");

	if(psSyncCheckpointInt)
	{
		PVR_LOG_IF_FALSE((psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_NOT_SIGNALLED), "psSyncCheckpoint already signalled");

		if (psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_NOT_SIGNALLED)
		{
			psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State = PVRSRV_SYNC_CHECKPOINT_SIGNALLED;
#if defined(PDUMP)
			PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS,
								  "Signalled Sync Checkpoint (FirmwareVAddr = 0x%08x)",
			                      (psSyncCheckpointInt->psSyncCheckpointBlock->ui32FirmwareAddr + _SyncCheckpointGetOffset(psSyncCheckpointInt)));
			SyncCheckpointSignalPDump(psSyncCheckpointInt);
#endif
		}
	}
}

IMG_INTERNAL void
SyncCheckpointError(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	_SYNC_CHECKPOINT *psSyncCheckpointInt = (_SYNC_CHECKPOINT*)psSyncCheckpoint;

	PVR_LOG_IF_FALSE((psSyncCheckpoint != NULL), "psSyncCheckpoint invalid");

	if(psSyncCheckpointInt)
	{
		PVR_LOG_IF_FALSE((psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_NOT_SIGNALLED), "psSyncCheckpoint already signalled");

		if (psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_NOT_SIGNALLED)
		{
			psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State = PVRSRV_SYNC_CHECKPOINT_ERRORED;
#if defined(PDUMP)
			PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS,
								  "Errored Sync Checkpoint (FirmwareVAddr = 0x%08x)",
								  (psSyncCheckpointInt->psSyncCheckpointBlock->ui32FirmwareAddr + _SyncCheckpointGetOffset(psSyncCheckpointInt)));
			SyncCheckpointErrorPDump(psSyncCheckpointInt);
#endif
		}
	}
}

IMG_BOOL SyncCheckpointIsSignalled(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	IMG_BOOL bRet = IMG_FALSE;
	_SYNC_CHECKPOINT *psSyncCheckpointInt = (_SYNC_CHECKPOINT*)psSyncCheckpoint;

	PVR_LOG_IF_FALSE((psSyncCheckpoint != NULL), "psSyncCheckpoint invalid");

	if (psSyncCheckpointInt)
	{
		bRet = ((psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_SIGNALLED) ||
		        (psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_ERRORED));
	}
	return bRet;
}

IMG_INTERNAL IMG_BOOL
SyncCheckpointIsErrored(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	IMG_BOOL bRet = IMG_FALSE;
	_SYNC_CHECKPOINT *psSyncCheckpointInt = (_SYNC_CHECKPOINT*)psSyncCheckpoint;

	PVR_LOG_IF_FALSE((psSyncCheckpoint != NULL), "psSyncCheckpoint invalid");

	if (psSyncCheckpointInt)
	{
		bRet = (psSyncCheckpointInt->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_ERRORED);
	}
	return bRet;
}

IMG_INTERNAL PVRSRV_ERROR
SyncCheckpointTakeRef(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	PVRSRV_ERROR eRet = PVRSRV_OK;
	_SYNC_CHECKPOINT *psSyncCheckpointInt = (_SYNC_CHECKPOINT*)psSyncCheckpoint;

	PVR_LOGR_IF_FALSE((psSyncCheckpoint != NULL), "psSyncCheckpoint invalid", PVRSRV_ERROR_INVALID_PARAMS);

	OSAtomicIncrement(&psSyncCheckpointInt->hRefCount);

	return eRet;
}

IMG_INTERNAL PVRSRV_ERROR
SyncCheckpointDropRef(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	PVRSRV_ERROR eRet = PVRSRV_OK;
	_SYNC_CHECKPOINT *psSyncCheckpointInt = (_SYNC_CHECKPOINT*)psSyncCheckpoint;

	PVR_LOGR_IF_FALSE((psSyncCheckpoint != NULL), "psSyncCheckpoint invalid", PVRSRV_ERROR_INVALID_PARAMS);

	OSAtomicDecrement(&psSyncCheckpointInt->hRefCount);

	return eRet;
}

IMG_INTERNAL void
SyncCheckpointCCBEnqueued(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	_SYNC_CHECKPOINT *psSyncCheckpointInt = (_SYNC_CHECKPOINT*)psSyncCheckpoint;

	PVR_LOG_IF_FALSE((psSyncCheckpoint != NULL), "psSyncCheckpoint invalid");

	if (psSyncCheckpointInt)
	{
		OSAtomicIncrement(&psSyncCheckpointInt->hEnqueuedCCBCount);
	}
}

IMG_INTERNAL IMG_UINT32
SyncCheckpointGetFirmwareAddr(PSYNC_CHECKPOINT psSyncCheckpoint)
{
	_SYNC_CHECKPOINT *psSyncCheckpointInt = (_SYNC_CHECKPOINT*)psSyncCheckpoint;
	SYNC_CHECKPOINT_BLOCK *psSyncBlock;

	PVR_LOGG_IF_FALSE((psSyncCheckpoint != NULL), "psSyncCheckpoint invalid", invalid_chkpt);

	psSyncBlock = psSyncCheckpointInt->psSyncCheckpointBlock;
	/* add 1 to addr to indicate this FW addr is a sync checkpoint (not a sync prim) */
	return psSyncBlock->ui32FirmwareAddr + _SyncCheckpointGetOffset(psSyncCheckpointInt) + 1;

invalid_chkpt:
	return 0;
}

void SyncCheckpointErrorFromUFO(PSYNC_CHECKPOINT_CONTEXT psSyncContext,
								IMG_UINT32 ui32FwAddr)
{
	_SYNC_CHECKPOINT_CONTEXT *psContext = (_SYNC_CHECKPOINT_CONTEXT *)psSyncContext;
	_SYNC_CHECKPOINT *psSyncCheckpointInt;
	IMG_BOOL bFound = IMG_FALSE;
	PDLLIST_NODE psNode;

	PVR_DPF((PVR_DBG_ERROR, "%s: Entry (ui32FWAddr=%d) >",__FUNCTION__, ui32FwAddr));

	OSLockAcquire(psContext->hCheckpointListLock);
	psNode = dllist_get_next_node(&psContext->sCheckpointList);
	while ((psNode != NULL) && !bFound)
	{
		psSyncCheckpointInt = IMG_CONTAINER_OF(psNode, _SYNC_CHECKPOINT, sListNode);
		if (ui32FwAddr == SyncCheckpointGetFirmwareAddr((PSYNC_CHECKPOINT)psSyncCheckpointInt))
		{
			bFound = IMG_TRUE;
			/* Mark as errored */
			SyncCheckpointError((PSYNC_CHECKPOINT)psSyncCheckpointInt);
		}
		psNode = dllist_get_next_node(psNode);
	}
	OSLockRelease(psContext->hCheckpointListLock);

	PVR_DPF((PVR_DBG_ERROR, "%s: Exit <",__FUNCTION__));
}

static void _SyncCheckpointState(PDLLIST_NODE psNode,
					DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile)
{
	_SYNC_CHECKPOINT *psSyncCheckpoint = IMG_CONTAINER_OF(psNode, _SYNC_CHECKPOINT, sListNode);

	if (psSyncCheckpoint->psSyncCheckpointFwObj->ui32State == PVRSRV_SYNC_CHECKPOINT_NOT_SIGNALLED)
	{
		PVR_DUMPDEBUG_LOG("\tPending sync checkpoint(ID = %d, FWAddr = 0x%08x): (%s)",
		                   psSyncCheckpoint->ui32UID,
		                   psSyncCheckpoint->psSyncCheckpointBlock->ui32FirmwareAddr + _SyncCheckpointGetOffset(psSyncCheckpoint),
		                   psSyncCheckpoint->azName);
	}
}

static void _SyncCheckpointDebugRequest(PVRSRV_DBGREQ_HANDLE hDebugRequestHandle,
					IMG_UINT32 ui32VerbLevel,
					DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile)
{
	_SYNC_CHECKPOINT_CONTEXT *psContext = (_SYNC_CHECKPOINT_CONTEXT *)hDebugRequestHandle;
	DLLIST_NODE *psNode, *psNext;

	if (ui32VerbLevel == DEBUG_REQUEST_VERBOSITY_HIGH)
	{
		PVR_DUMPDEBUG_LOG("Dumping all pending sync checkpoints");
		OSLockAcquire(psContext->hCheckpointListLock);
		dllist_foreach_node(&psContext->sCheckpointList, psNode, psNext)
		{
			_SyncCheckpointState(psNode, pfnDumpDebugPrintf, pvDumpDebugFile);
		}
		OSLockRelease(psContext->hCheckpointListLock);
	}
}

#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
PVRSRV_ERROR
SyncCheckpointRecordAdd(
			PSYNC_CHECKPOINT_RECORD_HANDLE * phRecord,
			SYNC_CHECKPOINT_BLOCK * hSyncCheckpointBlock,
			IMG_UINT32 ui32FwBlockAddr,
			IMG_UINT32 ui32SyncOffset,
			IMG_UINT32 ui32ClassNameSize,
			const IMG_CHAR *pszClassName)
{
	_SYNC_CHECKPOINT_CONTEXT *psContext = hSyncCheckpointBlock->psContext;
	struct SYNC_CHECKPOINT_RECORD * psSyncRec;
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (!phRecord)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	*phRecord = NULL;

	psSyncRec = OSAllocMem(sizeof(*psSyncRec));
	if (!psSyncRec)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc;
	}

	psSyncRec->psSyncCheckpointBlock = hSyncCheckpointBlock;
	psSyncRec->ui32SyncOffset = ui32SyncOffset;
	psSyncRec->ui32FwBlockAddr = ui32FwBlockAddr;
	psSyncRec->ui64OSTime = OSClockns64();
	psSyncRec->uiPID = OSGetCurrentProcessID();

	if(pszClassName)
	{
		if (ui32ClassNameSize >= SYNC_MAX_CLASS_NAME_LEN)
			ui32ClassNameSize = SYNC_MAX_CLASS_NAME_LEN - 1;
		/* Copy over the class name annotation */
		OSStringNCopy(psSyncRec->szClassName, pszClassName, ui32ClassNameSize);
		psSyncRec->szClassName[ui32ClassNameSize] = 0;
	}
	else
	{
		/* No class name annotation */
		psSyncRec->szClassName[0] = 0;
	}

	OSLockAcquire(psContext->hCheckpointRecordLock);
	dllist_add_to_head(&psContext->sCheckpointRecordList, &psSyncRec->sNode);
	OSLockRelease(psContext->hCheckpointRecordLock);

	*phRecord = (PSYNC_CHECKPOINT_RECORD_HANDLE)psSyncRec;

fail_alloc:
	return eError;
}

PVRSRV_ERROR
SyncCheckpointRecordRemove(PSYNC_CHECKPOINT_RECORD_HANDLE hRecord)
{
	struct SYNC_CHECKPOINT_RECORD **ppFreedSync;
	struct SYNC_CHECKPOINT_RECORD *pSync = (struct SYNC_CHECKPOINT_RECORD*)hRecord;
	_SYNC_CHECKPOINT_CONTEXT *psContext;

	if (!hRecord)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psContext = pSync->psSyncCheckpointBlock->psContext;

	OSLockAcquire(psContext->hCheckpointRecordLock);

	dllist_remove_node(&pSync->sNode);

	if (psContext->uiCheckpointRecordFreeIdx >= PVRSRV_FULL_SYNC_TRACKING_HISTORY_LEN)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: freed sync record index out of range", __FUNCTION__));
		psContext->uiCheckpointRecordFreeIdx = 0;
	}
	ppFreedSync = &psContext->apsCheckpointRecordsFreed[psContext->uiCheckpointRecordFreeIdx];
	psContext->uiCheckpointRecordFreeIdx =
		(psContext->uiCheckpointRecordFreeIdx + 1) % PVRSRV_FULL_SYNC_TRACKING_HISTORY_LEN;

	if (*ppFreedSync)
	{
		OSFreeMem(*ppFreedSync);
	}
	pSync->psSyncCheckpointBlock = NULL;
	pSync->ui64OSTime = OSClockns64();
	*ppFreedSync = pSync;

	OSLockRelease(psContext->hCheckpointRecordLock);

	return PVRSRV_OK;
}

#define NS_IN_S (1000000000UL)
static void _SyncCheckpointRecordPrint(struct SYNC_CHECKPOINT_RECORD *psSyncCheckpointRec,
					IMG_UINT64 ui64TimeNow,
					DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile)
{
	SYNC_CHECKPOINT_BLOCK *psSyncCheckpointBlock = psSyncCheckpointRec->psSyncCheckpointBlock;
	IMG_UINT64 ui64DeltaS;
	IMG_UINT32 ui32DeltaF;
	IMG_UINT64 ui64Delta = ui64TimeNow - psSyncCheckpointRec->ui64OSTime;
	ui64DeltaS = OSDivide64(ui64Delta, NS_IN_S, &ui32DeltaF);

	if (psSyncCheckpointBlock && psSyncCheckpointBlock->pui32LinAddr)
	{
		void *pSyncCheckpointAddr;
		pSyncCheckpointAddr = (void*)(psSyncCheckpointBlock->pui32LinAddr + psSyncCheckpointRec->ui32SyncOffset);

		PVR_DUMPDEBUG_LOG("\t%05u %05llu.%09u FWAddr=0x%08x State=%s (%s)",
			psSyncCheckpointRec->uiPID,
			ui64DeltaS, ui32DeltaF,
			(psSyncCheckpointRec->ui32FwBlockAddr+psSyncCheckpointRec->ui32SyncOffset),
			(*(IMG_UINT32*)pSyncCheckpointAddr == PVRSRV_SYNC_CHECKPOINT_SIGNALLED) ? "SIGNALLED" : ((*(IMG_UINT32*)pSyncCheckpointAddr == PVRSRV_SYNC_CHECKPOINT_ERRORED) ? "ERRORED" : "NOT_SIGNALLED"),
			psSyncCheckpointRec->szClassName
			);
	}
	else
	{
		PVR_DUMPDEBUG_LOG("\t%05u %05llu.%09u FWAddr=0x%08x State=<null_ptr> (%s)",
			psSyncCheckpointRec->uiPID,
			ui64DeltaS, ui32DeltaF,
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
	_SYNC_CHECKPOINT_CONTEXT *psContext = (_SYNC_CHECKPOINT_CONTEXT *)hDebugRequestHandle;
	IMG_UINT64 ui64TimeNowS;
	IMG_UINT32 ui32TimeNowF;
	IMG_UINT64 ui64TimeNow = OSClockns64();
	DLLIST_NODE *psNode, *psNext;

	ui64TimeNowS = OSDivide64(ui64TimeNow, NS_IN_S, &ui32TimeNowF);

	if (ui32VerbLevel == DEBUG_REQUEST_VERBOSITY_HIGH)
	{
		IMG_UINT32 i;

		OSLockAcquire(psContext->hCheckpointRecordLock);

		PVR_DUMPDEBUG_LOG("Dumping all allocated sync checkpoints @ %05llu.%09u", ui64TimeNowS, ui32TimeNowF);
		PVR_DUMPDEBUG_LOG("\t%-5s %-15s %-17s %-14s (%s)",
					"PID", "Time Delta (s)", "Address", "State", "Annotation");

		dllist_foreach_node(&psContext->sCheckpointRecordList, psNode, psNext)
		{
			struct SYNC_CHECKPOINT_RECORD *psSyncCheckpointRec =
				IMG_CONTAINER_OF(psNode, struct SYNC_CHECKPOINT_RECORD, sNode);
			_SyncCheckpointRecordPrint(psSyncCheckpointRec, ui64TimeNow,
							pfnDumpDebugPrintf, pvDumpDebugFile);
		}

		PVR_DUMPDEBUG_LOG("Dumping all recently freed sync checkpoints @ %05llu.%09u", ui64TimeNowS, ui32TimeNowF);
		PVR_DUMPDEBUG_LOG("\t%-5s %-15s %-17s %-14s (%s)",
					"PID", "Time Delta (s)", "Address", "State", "Annotation");
		for (i = DECREMENT_WITH_WRAP(psContext->uiCheckpointRecordFreeIdx, PVRSRV_FULL_SYNC_TRACKING_HISTORY_LEN);
			 i != psContext->uiCheckpointRecordFreeIdx;
			 i = DECREMENT_WITH_WRAP(i, PVRSRV_FULL_SYNC_TRACKING_HISTORY_LEN))
		{
			if (psContext->apsCheckpointRecordsFreed[i])
			{
				_SyncCheckpointRecordPrint(psContext->apsCheckpointRecordsFreed[i],
								ui64TimeNow, pfnDumpDebugPrintf, pvDumpDebugFile);
			}
			else
			{
				break;
			}
		}
		OSLockRelease(psContext->hCheckpointRecordLock);
	}
}
#undef NS_IN_S
static PVRSRV_ERROR _SyncCheckpointRecordListInit(_SYNC_CHECKPOINT_CONTEXT *psContext)
{
	PVRSRV_ERROR eError;

	eError = OSLockCreate(&psContext->hCheckpointRecordLock, LOCK_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		goto fail_lock_create;
	}
	dllist_init(&psContext->sCheckpointRecordList);

	eError = PVRSRVRegisterDbgRequestNotify(&psContext->hCheckpointRecordNotify,
											psContext->psDevNode,
											_SyncCheckpointRecordRequest,
											DEBUG_REQUEST_SYNCCHECKPOINT,
											psContext);

	if (eError != PVRSRV_OK)
	{
		goto fail_dbg_register;
	}

	return PVRSRV_OK;

fail_dbg_register:
	OSLockDestroy(psContext->hCheckpointRecordLock);
fail_lock_create:
	return eError;
}

static void _SyncCheckpointRecordListDeinit(_SYNC_CHECKPOINT_CONTEXT *psContext)
{
	int i;
	DLLIST_NODE *psNode, *psNext;

	OSLockAcquire(psContext->hCheckpointRecordLock);
	dllist_foreach_node(&psContext->sCheckpointRecordList, psNode, psNext)
	{
		struct SYNC_CHECKPOINT_RECORD *pSyncCheckpointRec =
			IMG_CONTAINER_OF(psNode, struct SYNC_CHECKPOINT_RECORD, sNode);

		dllist_remove_node(psNode);
		OSFreeMem(pSyncCheckpointRec);
	}

	for (i = 0; i < PVRSRV_FULL_SYNC_TRACKING_HISTORY_LEN; i++)
	{
		if (psContext->apsCheckpointRecordsFreed[i])
		{
			OSFreeMem(psContext->apsCheckpointRecordsFreed[i]);
			psContext->apsCheckpointRecordsFreed[i] = NULL;
		}
	}
	OSLockRelease(psContext->hCheckpointRecordLock);

	PVRSRVUnregisterDbgRequestNotify(psContext->hCheckpointRecordNotify);
	psContext->hCheckpointRecordNotify = NULL;

	OSLockDestroy(psContext->hCheckpointRecordLock);
	psContext->hCheckpointRecordLock = NULL;
}
#else /* defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING) */
PVRSRV_ERROR
SyncCheckpointRecordAdd(
			PSYNC_CHECKPOINT_RECORD_HANDLE * phRecord,
			SYNC_CHECKPOINT_BLOCK * hSyncCheckpointBlock,
			IMG_UINT32 ui32FwBlockAddr,
			IMG_UINT32 ui32SyncOffset,
			IMG_UINT32 ui32ClassNameSize,
			const IMG_CHAR *pszClassName)
{
	PVR_UNREFERENCED_PARAMETER(phRecord);
	PVR_UNREFERENCED_PARAMETER(hSyncCheckpointBlock);
	PVR_UNREFERENCED_PARAMETER(ui32FwBlockAddr);
	PVR_UNREFERENCED_PARAMETER(ui32SyncOffset);
	PVR_UNREFERENCED_PARAMETER(ui32ClassNameSize);
	PVR_UNREFERENCED_PARAMETER(pszClassName);
	return PVRSRV_OK;
}
PVRSRV_ERROR
SyncCheckpointRecordRemove(PSYNC_CHECKPOINT_RECORD_HANDLE hRecord)
{
	PVR_UNREFERENCED_PARAMETER(hRecord);
	return PVRSRV_OK;
}

static PVRSRV_ERROR _SyncCheckpointRecordListInit(_SYNC_CHECKPOINT_CONTEXT *psContext)
{
	PVR_UNREFERENCED_PARAMETER(psContext);
	return PVRSRV_OK;
}
static void _SyncCheckpointRecordListDeinit(_SYNC_CHECKPOINT_CONTEXT *psContext)
{
	PVR_UNREFERENCED_PARAMETER(psContext);
}
#endif /* defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING) */
#if defined(PDUMP)
PVRSRV_ERROR
SyncCheckpointSignalPDump(_SYNC_CHECKPOINT *psSyncCheckpoint)
{
	/*
		We might be ask to PDump sync state outside of capture range
		(e.g. texture uploads) so make this continuous.
	*/
	DevmemPDumpLoadMemValue32(psSyncCheckpoint->psSyncCheckpointBlock->hMemDesc,
					   _SyncCheckpointGetOffset(psSyncCheckpoint),
					   PVRSRV_SYNC_CHECKPOINT_SIGNALLED,
					   PDUMP_FLAGS_CONTINUOUS);

	return PVRSRV_OK;
}

PVRSRV_ERROR
SyncCheckpointErrorPDump(_SYNC_CHECKPOINT *psSyncCheckpoint)
{
	/*
		We might be ask to PDump sync state outside of capture range
		(e.g. texture uploads) so make this continuous.
	*/
	DevmemPDumpLoadMemValue32(psSyncCheckpoint->psSyncCheckpointBlock->hMemDesc,
					   _SyncCheckpointGetOffset(psSyncCheckpoint),
					   PVRSRV_SYNC_CHECKPOINT_ERRORED,
					   PDUMP_FLAGS_CONTINUOUS);

	return PVRSRV_OK;
}

#endif
