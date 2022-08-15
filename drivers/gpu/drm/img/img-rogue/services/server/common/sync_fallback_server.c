/*************************************************************************/ /*!
@File           sync_fallback_server.c
@Title          Fallback implementation of server fence sync interface.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    The server implementation of software fallback synchronisation.
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

#if defined(DEBUG)
#define PVR_DPF_FUNCTION_TRACE_ON 1
#endif

#include "pvr_debug.h"
#include "img_types.h"
#include "img_defs.h"
#include "pvrsrv_error.h"
#include "pvrsrv_sync_km.h"
#include "pvrsrv_sync_server.h"
#include "sync_fallback_server.h"
#include "sync_checkpoint.h"
#include "sync_checkpoint_external.h"
#include "sync_checkpoint_internal.h"
#include "osfunc.h"
#include "lock.h"
#include "handle.h"
#include "pvrsrv.h"
#include "hash.h"
#include "rgxhwperf.h"
#include "pdump_km.h"
#include "allocmem.h"

#if defined(PVR_TESTING_UTILS)
#include "tutils_km.h"
#endif

#include "ossecure_export.h"

/* Refcounting debug.
 * Define SYNC_FB_REF_DEBUG to print out a reference count log. */
// #define SYNC_FB_REF_DEBUG 1

#if defined(SYNC_FB_REF_DEBUG)
#define PRINT_REF(type, opchr, pRef, ptr, name, info, num) \
	PVR_LOG(("        %s REF(%c) -> %d - %6s: %-5u - %-30s (0x%p)", type, opchr, pRef, info, num, name, ptr))
#else
#define PRINT_REF(type, opchr, pRef, ptr, name, info, num)
#endif

#define REF_SET(type, pRef, val, ptr, name, info, num) OSAtomicWrite(pRef, val); PRINT_REF(type, '=', val, ptr, name, info, num)
#define REF_INC(type, pRef,      ptr, name, info, num) OSAtomicIncrement(pRef); PRINT_REF(type, '+', OSAtomicRead(pRef), ptr, name, info, num)
#define REF_DEC(type, pRef,      ptr, name, info, num) OSAtomicDecrement(pRef); PRINT_REF(type, '-', OSAtomicRead(pRef), ptr, name, info, num)

/* Timelines */
#define TL_REF_SET(pRef, val, ptr) REF_SET("TL", pRef, val, ptr, ptr->pszName, "UID", (IMG_UINT64) ptr->iUID)
#define TL_REF_INC(pRef, ptr)      REF_INC("TL", pRef,      ptr, ptr->pszName, "UID", (IMG_UINT64) ptr->iUID)
#define TL_REF_DEC(pRef, ptr)      REF_DEC("TL", pRef,      ptr, ptr->pszName, "UID", (IMG_UINT64) ptr->iUID)

/* Fences */
#define FENCE_REF_SET(pRef, val, ptr) REF_SET("FE", pRef, val, ptr, ptr->pszName, "#Syncs", ptr->uiNumSyncs)
#define FENCE_REF_INC(pRef, ptr)      REF_INC("FE", pRef,      ptr, ptr->pszName, "#Syncs", ptr->uiNumSyncs)
#define FENCE_REF_DEC(pRef, ptr)      REF_DEC("FE", pRef,      ptr, ptr->pszName, "#Syncs", ptr->uiNumSyncs)

/* SyncPt */
#define PT_REF_SET(pRef, val, ptr) REF_SET("PT", pRef, val, ptr, ptr->psTl->pszName, "SeqNum" ,ptr->uiSeqNum)
#define PT_REF_INC(pRef, ptr)      REF_INC("PT", pRef,      ptr, ptr->psTl->pszName, "SeqNum" ,ptr->uiSeqNum)
#define PT_REF_DEC(pRef, ptr)      REF_DEC("PT", pRef,      ptr, ptr->psTl->pszName, "SeqNum" ,ptr->uiSeqNum)


/* Simple prints for error and warning */
#define ERR(msg, ...) PVR_DPF((PVR_DBG_ERROR, \
                          "%s: " msg, \
                          __func__, \
                          ##__VA_ARGS__));

#define WRN(msg, ...) PVR_DPF((PVR_DBG_WARNING, \
                          "%s: " msg, \
                          __func__, \
                          ##__VA_ARGS__));

// #define SYNC_FB_DEBUG 1
#if defined(SYNC_FB_DEBUG)
#define DBG(...) PVR_LOG(__VA_ARGS__)
#else
#define DBG(...)
#endif

/* Functions for timelines */
typedef IMG_BOOL (*PFN_SYNC_PT_HAS_SIGNALLED)(PVRSRV_SYNC_PT *psSyncPt);
typedef void (*PFN_SYNC_FREE) (IMG_HANDLE hAttachedSync);


/* The states a SyncPt can be in */
typedef enum
{
	PVRSRV_SYNC_NOT_SIGNALLED,     /*!< sync pt has not yet signalled */
	PVRSRV_SYNC_SIGNALLED,         /*!< sync pt has signalled */
	PVRSRV_SYNC_ERRORED            /*!< sync pt has errored*/
} PVRSRV_SYNC_STATE;

typedef enum
{
	PVRSRV_SYNC_HANDLE_UNKNOWN,
	PVRSRV_SYNC_HANDLE_PVR,
	PVRSRV_SYNC_HANDLE_SW
} PVRSRV_SYNC_HANDLE_TYPE;

typedef struct _PVRSRV_SYNC_SIGNAL_CB_
{
	DLLIST_NODE sCallbackNode;
	IMG_HANDLE  hAttachedSync;
	IMG_HANDLE  hPrivData;
	PVRSRV_ERROR (*pfnSignal)(IMG_HANDLE hAttachedSync,
	                          PVRSRV_SYNC_STATE eState);
	PFN_SYNC_FREE pfnSyncFree;

} PVRSRV_SYNC_SIGNAL_CB;

struct _PVRSRV_SYNC_PT_
{
	/* The timeline this sync pt is associated with */
	PVRSRV_TIMELINE_SERVER	*psTl;
	IMG_UINT32				uiSeqNum;
	/* Refcount */
	ATOMIC_T				iRef;
	/* Timeline list. Take TL lock! */
	DLLIST_NODE				sTlSyncList;
	/* Timeline active list. Take TL lock! */
	DLLIST_NODE				sTlSyncActiveList;

	/* List of callbacks to signal attached syncs.
	 *
	 * THE FIRST ITEM OF THIS LIST DEFINES THE FLAVOUR OF THE SYNC PT
	 * AND MUST BE CREATED TOGETHER WITH THE SYNC PT!
	 * Usually this is done when creating a fence.
	 * E.g. if PVR has been asked to create a fence we would
	 * create a sync pt for it with an attached sync checkpoint.
	 *
	 * In case someone waits for this sync pt who is not able
	 * to access the first item, a new foreign sync
	 * needs to be attached that can be read by the waiter.
	 * This might be the case if a second device is involved that cannot
	 * access sync checkpoints of another device or a device that needs
	 * to wait for a different sync type that it is not able to read
	 * e.g. a SW sync */
	DLLIST_NODE				sSignalCallbacks;
	/* Can have a PVRSRV_SYNC_STATE */
	ATOMIC_T				iStatus;
	/* PID of the sync pt creator, used for cleanup-unblocking */
	IMG_UINT32				uiPID;
};

/* Definition representing an attached SW sync pt.
 * This is the counterpart to the SYNC_CHECKPOINTS for syncs that get
 * signalled by the CPU. */
typedef struct _PVRSRV_SYNC_PT_SW_
{
	 IMG_BOOL bSignalled;
} PVRSRV_SYNC_PT_SW;

/*! Possible states for a PVRSRV_FENCE */
typedef enum
{
    PVRSRV_FENCE_NOT_SIGNALLED,             /*!< fence has not yet signalled (not all components have signalled) */
    PVRSRV_FENCE_SIGNALLED                  /*!< fence has signalled (all components have signalled/errored) */
} PVRSRV_FENCE_STATE;

struct _PVRSRV_FENCE_SERVER_
{
	IMG_UINT32			uiNumSyncs;
	PVRSRV_SYNC_PT		**apsFenceSyncList;
	ATOMIC_T			iRef;
	/* Only written to when waiter checks if fence is met */
	ATOMIC_T			iStatus;
	IMG_INT64			iUID;
	IMG_CHAR			pszName[SYNC_FB_FENCE_MAX_LENGTH];
	PVRSRV_DEVICE_NODE	*psDevNode;
	DLLIST_NODE			sFenceListNode;
};

struct _PVRSRV_FENCE_EXPORT_
{
	PVRSRV_FENCE_SERVER *psFence;
};

typedef struct _PVRSRV_TIMELINE_OPS_
{
	/* Supposed to be called when someone queries the TL
	 * to update its active generic syncs */
	PFN_SYNC_PT_HAS_SIGNALLED pfnSyncPtHasSignalled;
} PVRSRV_TIMELINE_OPS;

struct _PVRSRV_TIMELINE_SERVER_
{
	/* Never take the fence lock after this one */
	POS_LOCK			hTlLock;
	/* Timeline list. Contains all sync pts of the timeline that
	 * were not destroyed. Signalled or unsignalled. Take TL lock! */
	DLLIST_NODE			sSyncList;
	/* Timeline active list. Contains all sync pts of the timeline
	 * that were not signalled yet.
	 * Before removing node, check if it's still in list. Take TL lock! */
	DLLIST_NODE			sSyncActiveList;
	IMG_CHAR			pszName[SYNC_FB_TIMELINE_MAX_LENGTH];
	ATOMIC_T			iRef;
	PVRSRV_TIMELINE_OPS sTlOps;
	DLLIST_NODE			sTlList;
	/* This ID helps to order the sync pts in a fence when merging */
	IMG_INT64			iUID;
	/* The sequence number of the last sync pt created */
	ATOMIC_T			iSeqNum;
	/* The sequence number of the latest signalled sync pt */
	ATOMIC_T			iLastSignalledSeqNum;
	/* The PID of the process which created this timeline */
	IMG_PID				uiPID;
};

typedef struct _SYNC_FB_CONTEXT_DEVICE_LIST_
{
	DLLIST_NODE sDeviceListNode;
	IMG_HANDLE hDBGNotify;
	PVRSRV_DEVICE_NODE *psDevice;
} SYNC_FB_CONTEXT_DEVICE_LIST;

typedef struct _SYNC_FB_CONTEXT_
{
	IMG_HANDLE hSyncEventObject;
	IMG_HANDLE hCMDNotify;
	DLLIST_NODE sDeviceList;
	DLLIST_NODE sTlList;
	POS_LOCK hFbContextLock;
	DLLIST_NODE sFenceList; /* protected by hFbContextLock */
	HASH_TABLE *sCheckpointHashTable; /* protected by hFbContextLock */
	PFN_SYNC_CHECKPOINT_STRUCT sSyncCheckpointReg;
} SYNC_FB_CONTEXT;

/* GLOBALS */
static SYNC_FB_CONTEXT gsSyncFbContext;

/* Declarations */
static void _SyncFbTimelineAcquire(PVRSRV_TIMELINE_SERVER *psTl);
static void _SyncFbFenceAcquire(PVRSRV_FENCE_SERVER *psFence);
static PVRSRV_ERROR _SyncFbSyncPtSignalAttached(PVRSRV_SYNC_PT *psSyncPt,
                                        PVRSRV_SYNC_STATE eSignal);
static PVRSRV_ERROR SyncFbFenceRollbackPVR(PVRSRV_FENCE iFence, void *pvFenceData);
static PVRSRV_ERROR _SyncFbSyncPtSignalPVR(IMG_HANDLE hSync,
                                           PVRSRV_SYNC_STATE eState);
static PVRSRV_ERROR _SyncFbSyncPtSignalSW(IMG_HANDLE hSync,
                                          PVRSRV_SYNC_STATE eState);
static IMG_BOOL _SyncFbFenceSyncsHaveSignalled(PVRSRV_FENCE_SERVER *psFence);
static IMG_BOOL _SyncFbSyncPtHasSignalled(PVRSRV_SYNC_PT *psSyncPt);
static IMG_BOOL _SyncFbSyncPtHasSignalledPVR(PVRSRV_SYNC_PT *psSyncPt);
static IMG_BOOL _SyncFbSyncPtHasSignalledSW(PVRSRV_SYNC_PT *psSyncPt);
static IMG_BOOL _SyncFbFenceAddPt(PVRSRV_FENCE_SERVER *psFence,
                                  IMG_UINT32 *i,
                                  PVRSRV_SYNC_PT *psSyncPt);
static PVRSRV_ERROR _SyncFbSWTimelineFenceCreate(PVRSRV_DEVICE_NODE *psDeviceNode,
                                                 PVRSRV_TIMELINE_SERVER *psTl,
                                                 IMG_UINT32 uiFenceNameSize,
                                                 const IMG_CHAR *pszFenceName,
                                                 PVRSRV_FENCE_SERVER **ppsOutputFence,
                                                 IMG_UINT64 *pui64SyncPtIdx);
static PVRSRV_ERROR _SyncSWTimelineAdvanceSigErr(PVRSRV_TIMELINE_SERVER *psTl,
                                                 PVRSRV_SYNC_STATE eState,
                                                 IMG_UINT64 *pui64SyncPtIdx);
static void _SyncSWTimelineCheckForUnsignalledPts(PVRSRV_TIMELINE_SERVER *psTl);


/*****************************************************************************/
/*                                                                           */
/*                         GENERIC FUNCTIONS                                 */
/*                                                                           */
/*****************************************************************************/


/* Add a fence to the global fence list */
static inline void _SyncFbFenceListAdd(PVRSRV_FENCE_SERVER *psFence)
{
	OSLockAcquire(gsSyncFbContext.hFbContextLock);
	dllist_add_to_tail(&gsSyncFbContext.sFenceList, &psFence->sFenceListNode);
	OSLockRelease(gsSyncFbContext.hFbContextLock);
}

/* Remove a fence from the global fence list */
static inline void _SyncFbFenceListDel(PVRSRV_FENCE_SERVER *psFence)
{
	OSLockAcquire(gsSyncFbContext.hFbContextLock);
	dllist_remove_node(&psFence->sFenceListNode);
	OSLockRelease(gsSyncFbContext.hFbContextLock);
}

/* Add a timeline to the global timeline list */
static inline void _SyncFbFTimelineListAdd(PVRSRV_TIMELINE_SERVER *psTl)
{
	OSLockAcquire(gsSyncFbContext.hFbContextLock);
	dllist_add_to_tail(&gsSyncFbContext.sTlList, &psTl->sTlList);
	OSLockRelease(gsSyncFbContext.hFbContextLock);
}

/* Remove a timeline from the global timeline list */
static inline void _SyncFbTimelineListDel(PVRSRV_TIMELINE_SERVER *psTl)
{
	OSLockAcquire(gsSyncFbContext.hFbContextLock);
	dllist_remove_node(&psTl->sTlList);
	OSLockRelease(gsSyncFbContext.hFbContextLock);
}

/* Signal the sync event object to wake up waiters */
static PVRSRV_ERROR _SyncFbSignalEO(void)
{
	PVRSRV_ERROR eError;
	PVR_DPF_ENTERED;

	PVR_ASSERT(gsSyncFbContext.hSyncEventObject != NULL);

	eError = OSEventObjectSignal(gsSyncFbContext.hSyncEventObject);

	PVR_DPF_RETURN_RC(eError);
}

/* Retrieve the process handle base for the calling PID to look up sync objects */
static PVRSRV_ERROR _SyncFbGetProcHandleBase(PVRSRV_HANDLE_BASE **ppsHandleBase)
{
	*ppsHandleBase = PVRSRVRetrieveProcessHandleBase();

	if (*ppsHandleBase == NULL)
	{
		ERR("Failed to retrieve process handle base");
		return PVRSRV_ERROR_UNABLE_TO_RETRIEVE_HASH_VALUE;
	}

	return PVRSRV_OK;
}

/* Look up a handle in the process handle base of the calling PID */
static PVRSRV_ERROR _SyncFbLookupProcHandle(IMG_HANDLE hHandle,
                                            PVRSRV_HANDLE_TYPE eType,
                                            IMG_BOOL bRefHandle,
                                            void **ppvData,
                                            PVRSRV_HANDLE_BASE **ppsBase)
{
	PVRSRV_ERROR eError;
	PVRSRV_HANDLE_BASE *psHandleBase;

	eError = _SyncFbGetProcHandleBase(&psHandleBase);

	PVR_GOTO_IF_ERROR(eError, e1);

	DBG(("%s: Handle Base: %p", __func__, psHandleBase));

	eError = PVRSRVLookupHandle(psHandleBase,
	                            ppvData,
	                            hHandle,
	                            eType,
	                            bRefHandle);
	PVR_GOTO_IF_ERROR(eError, e1);

	*ppsBase = psHandleBase;

	return PVRSRV_OK;

e1:
	return eError;
}

/* Destroy a handle in case a resource has not been registered with
 * the resource manager */
static PVRSRV_ERROR _SyncFbDestroyHandle(IMG_HANDLE hHandle,
                                         PVRSRV_HANDLE_TYPE eType)
{
	PVRSRV_ERROR eError;
	PVRSRV_HANDLE_BASE *psHandleBase;

	eError = _SyncFbGetProcHandleBase(&psHandleBase);
	PVR_GOTO_IF_ERROR(eError, e1);

	eError = PVRSRVDestroyHandle(psHandleBase, hHandle, eType);
	PVR_LOG_GOTO_IF_ERROR(eError, "PVRSRVDestroyHandle", e1);

	return PVRSRV_OK;

e1:
	return eError;
}

/* Currently unused */
/*
static PVRSRV_ERROR _SyncFbFindProcHandle(void *pvData,
                                          PVRSRV_HANDLE_TYPE eType,
                                          IMG_HANDLE *phHandle,
                                          PVRSRV_HANDLE_BASE **ppsBase)
{
	PVRSRV_ERROR eError;
	PVRSRV_HANDLE_BASE *psHandleBase;

	PVR_DPF_ENTERED;

	eError = _SyncFbGetProcHandleBase(&psHandleBase);

	PVR_GOTO_IF_ERROR(eError, eExit);

	eError = PVRSRVFindHandle(psHandleBase,
	                          phHandle,
	                          pvData,
							  eType);
	PVR_GOTO_IF_ERROR(eError, eExit);

	*ppsBase = psHandleBase;

	PVR_DPF_RETURN_OK;

eExit:
	PVR_DPF_RETURN_RC(eError);
}
*/

/* Returns the type of a sync point determined by its registered
 * signalling callback. Type can be e.g. a PVR sync point containing
 * sync checkpoints or a software sync point*/
static PVRSRV_SYNC_HANDLE_TYPE _SyncFbSyncPtHandleType(PVRSRV_SYNC_SIGNAL_CB *psCb)
{
	if (psCb == NULL)
		return PVRSRV_SYNC_HANDLE_UNKNOWN;

	if (psCb->pfnSignal == &_SyncFbSyncPtSignalPVR)
		return PVRSRV_SYNC_HANDLE_PVR;

	if (psCb->pfnSignal == &_SyncFbSyncPtSignalSW)
		return PVRSRV_SYNC_HANDLE_SW;

	return PVRSRV_SYNC_HANDLE_UNKNOWN;
}

static PVRSRV_SYNC_HANDLE_TYPE _SyncFbTimelineHandleType(PVRSRV_TIMELINE_SERVER *psTl)
{
	if (psTl == NULL)
		return PVRSRV_SYNC_HANDLE_UNKNOWN;

	if (psTl->sTlOps.pfnSyncPtHasSignalled == &_SyncFbSyncPtHasSignalledPVR)
		return PVRSRV_SYNC_HANDLE_PVR;

	if (psTl->sTlOps.pfnSyncPtHasSignalled == &_SyncFbSyncPtHasSignalledSW)
		return PVRSRV_SYNC_HANDLE_SW;

	return PVRSRV_SYNC_HANDLE_UNKNOWN;
}

/* Print info about a sync point to the debug dump log */
static void _SyncFbDebugRequestPrintSyncPt(PVRSRV_SYNC_PT *psSyncPt,
                                           IMG_BOOL bPrintTl,
                                           DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                           void *pvDumpDebugFile)
{
	PDLLIST_NODE psCBNode, psNextCBNode;
	PVRSRV_SYNC_SIGNAL_CB *psCb;

	if (bPrintTl)
	{
		PVR_DUMPDEBUG_LOG(" - SyncPt: SeqNum: %u, Refs: %d, Timeline: %-9s <%#"IMG_UINT64_FMTSPECx">, %-9s - <0x%p>",
						  psSyncPt->uiSeqNum,
						  OSAtomicRead(&psSyncPt->iRef),
						  psSyncPt->psTl->pszName,
						  psSyncPt->psTl->iUID,
						  OSAtomicRead(&psSyncPt->iStatus) == PVRSRV_SYNC_SIGNALLED ? "Signalled" :
							OSAtomicRead(&psSyncPt->iStatus) == PVRSRV_SYNC_ERRORED ? "Errored" : "Active",
						  psSyncPt);
	}
	else
	{
		PVR_DUMPDEBUG_LOG(" - SyncPt: SeqNum: %u, Refs: %d, %-9s - <0x%p>",
						  psSyncPt->uiSeqNum,
						  OSAtomicRead(&psSyncPt->iRef),
						  OSAtomicRead(&psSyncPt->iStatus) == PVRSRV_SYNC_SIGNALLED ? "Signalled" :
							OSAtomicRead(&psSyncPt->iStatus) == PVRSRV_SYNC_ERRORED ? "Errored" : "Active",
						  psSyncPt);
	}

	/* ... all attached syncs to that sync point*/
	dllist_foreach_node(&psSyncPt->sSignalCallbacks,
	                    psCBNode,
	                    psNextCBNode)
	{
		psCb = IMG_CONTAINER_OF(psCBNode,
		                        PVRSRV_SYNC_SIGNAL_CB,
		                        sCallbackNode);

		switch (_SyncFbSyncPtHandleType(psCb))
		{
			case PVRSRV_SYNC_HANDLE_PVR:
			{
				PSYNC_CHECKPOINT pCP = psCb->hAttachedSync;
				PVR_DUMPDEBUG_LOG("    - CbType: PVR-Checkpoint, ID: %u, FWAddr: %#08x, Enq: %d, Ref: %d, %-9s - <0x%p>",
				                  SyncCheckpointGetId(pCP),
				                  SyncCheckpointGetFirmwareAddr(pCP),
				                  SyncCheckpointGetEnqueuedCount(pCP),
				                  SyncCheckpointGetReferenceCount(pCP),
				                  SyncCheckpointGetStateString(pCP),
				                  pCP);
				break;
			}
			case PVRSRV_SYNC_HANDLE_SW:
			{
				PVRSRV_SYNC_PT_SW *psSWPt = psCb->hAttachedSync;
				PVR_DUMPDEBUG_LOG("    - CbType: SW-Syncpoint, %-9s - <0x%p>",
				                  psSWPt->bSignalled ? "Signalled" : "Active",
				                  psSWPt);
				break;
			}
			case PVRSRV_SYNC_HANDLE_UNKNOWN:
			default:
				PVR_DUMPDEBUG_LOG("    - CbType: Unknown - <0x%p>",
				                  psCb->hAttachedSync);
		}
	}
}

/* Function registered with the debug dump mechanism. Prints out all timelines
 * with pending syncs. */
static void _SyncFbDebugRequest(IMG_HANDLE hDebugRequestHandle,
                                IMG_UINT32 ui32VerbLevel,
                                DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                void *pvDumpDebugFile)
{
	if (DD_VERB_LVL_ENABLED(ui32VerbLevel, DEBUG_REQUEST_VERBOSITY_MEDIUM))
	{
		IMG_UINT32 i;

		PDLLIST_NODE psTlNode, psNextTlNode;
		PVRSRV_TIMELINE_SERVER *psTl;

		PDLLIST_NODE psFenceNode, psNextFenceNode;
		PVRSRV_FENCE_SERVER *psFence;

		PDLLIST_NODE psPtNode, psNextPtNode;
		PVRSRV_SYNC_PT *psSyncPt;

		OSLockAcquire(gsSyncFbContext.hFbContextLock);

		PVR_DUMPDEBUG_LOG("------[ Fallback Fence Sync: timelines ]------");

		/* Iterate over all timelines */
		dllist_foreach_node(&gsSyncFbContext.sTlList, psTlNode, psNextTlNode)
		{

			psTl = IMG_CONTAINER_OF(psTlNode,
			                        PVRSRV_TIMELINE_SERVER,
			                        sTlList);

			OSLockAcquire(psTl->hTlLock);

			PVR_DUMPDEBUG_LOG("Timeline: %s, SeqNum: %d/%d - <%#"IMG_UINT64_FMTSPECx">",
			                  psTl->pszName,
			                  OSAtomicRead(&psTl->iLastSignalledSeqNum),
			                  OSAtomicRead(&psTl->iSeqNum),
			                  psTl->iUID);

			/* ... all active sync points in the timeline */
			dllist_foreach_node(&psTl->sSyncActiveList, psPtNode, psNextPtNode)
			{

				psSyncPt = IMG_CONTAINER_OF(psPtNode,
				                            PVRSRV_SYNC_PT,
				                            sTlSyncActiveList);

				_SyncFbDebugRequestPrintSyncPt(psSyncPt,
				                               IMG_FALSE,
				                               pfnDumpDebugPrintf,
				                               pvDumpDebugFile);

			}
			OSLockRelease(psTl->hTlLock);
		}

		PVR_DUMPDEBUG_LOG("------[ Fallback Fence Sync: fences ]------");

		/* Iterate over all fences */
		dllist_foreach_node(&gsSyncFbContext.sFenceList,
							psFenceNode,
							psNextFenceNode)
		{
			psFence = IMG_CONTAINER_OF(psFenceNode,
			                           PVRSRV_FENCE_SERVER,
			                           sFenceListNode);

			PVR_DUMPDEBUG_LOG("Fence: %s, %-9s - <%#"IMG_UINT64_FMTSPECx">",
			                  psFence->pszName,
			                  _SyncFbFenceSyncsHaveSignalled(psFence) ?
			                      "Signalled" : "Active",
				               psFence->iUID);

			/* ... all sync points in the fence */
			for (i = 0; i < psFence->uiNumSyncs; i++)
			{
				_SyncFbDebugRequestPrintSyncPt(psFence->apsFenceSyncList[i],
                                               IMG_TRUE,
				                               pfnDumpDebugPrintf,
				                               pvDumpDebugFile);
			}
		}

		OSLockRelease(gsSyncFbContext.hFbContextLock);
	}

}

/* Notify callback that is called as part of the RGX MISR e.g. after FW
 * signalled the host that work completed. */
static void _SyncFbTimelineUpdate_NotifyCMD(void *psSyncFbContext)
{
	PVRSRV_TIMELINE_SERVER *psTl;
	PVRSRV_SYNC_PT *psSyncPt;
	PDLLIST_NODE psTlList = &gsSyncFbContext.sTlList;
	PDLLIST_NODE psCurrentTl, psNextTl;
	PDLLIST_NODE psCurrentPt, psNextPt;
	IMG_BOOL bSignalled = IMG_FALSE, bSignal = IMG_FALSE;

	PVR_DPF_ENTERED;

	/* Outer loop over all timelines */
	OSLockAcquire(gsSyncFbContext.hFbContextLock);
	dllist_foreach_node(psTlList, psCurrentTl, psNextTl)
	{
		psTl = IMG_CONTAINER_OF(psCurrentTl,
		                        PVRSRV_TIMELINE_SERVER,
		                        sTlList);

		/* Inner loop over all SyncPts in the timeline.
		 * Check & Update all active SyncPts */
		OSLockAcquire(psTl->hTlLock);
		dllist_foreach_node(&psTl->sSyncActiveList, psCurrentPt, psNextPt)
		{
			psSyncPt = IMG_CONTAINER_OF(psCurrentPt,
			                            PVRSRV_SYNC_PT,
			                            sTlSyncActiveList);

			/* If the SyncPt has been signalled we have to
			 * update all attached syncs */
			bSignalled = psTl->sTlOps.pfnSyncPtHasSignalled(psSyncPt);
			if (bSignalled)
			{
				/* Wake up waiters after releasing the locks */
				bSignal = IMG_TRUE;

				/* Remove the SyncPt from the active list of the timeline. */
				dllist_remove_node(psCurrentPt);
			}
			else
			{
				/* No need to check further points on this timeline because
				 * this sync pt will be signalled first */
				break;
			}

		}/* End inner loop */
		OSLockRelease(psTl->hTlLock);

	} /* End outer loop */
	OSLockRelease(gsSyncFbContext.hFbContextLock);

	if (bSignal)
	{
		PVR_LOG_IF_ERROR(_SyncFbSignalEO(), "_SyncFbSignalEO");
	}

	PVR_DPF_RETURN;
}

static IMG_UINT32
_SyncCheckpointFWAddrHash(size_t uKeySize, void *pKey, IMG_UINT32 uHashTabLen)
{
	IMG_UINT32 *p = (IMG_UINT32 *)pKey;
	IMG_UINT32 uKeyLen = uKeySize / sizeof(IMG_UINT32);
	IMG_UINT32 ui;
	IMG_UINT32 uHashKey = 0;

	PVR_UNREFERENCED_PARAMETER(uHashTabLen);

	for (ui = 0; ui < uKeyLen; ui++)
	{
		IMG_UINT32 uHashPart = *p++;

		uHashPart += (uHashPart << 12);
		uHashPart ^= (uHashPart >> 22);
		uHashPart += (uHashPart << 4);
		uHashPart ^= (uHashPart >> 9);
		uHashPart += (uHashPart << 10);
		uHashPart ^= (uHashPart >> 2);
		uHashPart += (uHashPart << 7);
		uHashPart ^= (uHashPart >> 12);

		uHashKey += uHashPart;
	}

	return uHashKey;
}

static IMG_BOOL
_SyncCheckpointFWAddrCompare(size_t uKeySize, void *pKey1, void *pKey2)
{
	IMG_UINT32 *p1 = (IMG_UINT32 *)pKey1;
	IMG_UINT32 *p2 = (IMG_UINT32 *)pKey2;
	IMG_UINT32 uKeyLen = uKeySize / sizeof(IMG_UINT32);
	IMG_UINT32 ui;

	for (ui = 0; ui < uKeyLen; ui++)
	{
		if (*p1++ != *p2++)
			return IMG_FALSE;
	}

	return IMG_TRUE;
}

#if defined(PDUMP)
static PVRSRV_ERROR SyncFbFenceGetCheckpoints(PVRSRV_FENCE hFence, IMG_UINT32 *puiNumCheckpoints,
		                                PSYNC_CHECKPOINT **papsCheckpoints)
{
	PVRSRV_ERROR eError;
	PVRSRV_FENCE_SERVER *psFence;
	PVRSRV_HANDLE_BASE *psHBase;
	PSYNC_CHECKPOINT *apsCheckpoints;
	PSYNC_CHECKPOINT psCheckpoint;
	PVRSRV_SYNC_SIGNAL_CB *psSyncCB;
	PVRSRV_SYNC_PT *psSyncPt;
	PDLLIST_NODE psNode;
	IMG_UINT32 i, uiNumCheckpoints = 0;

	if (hFence == PVRSRV_NO_FENCE)
	{
		*puiNumCheckpoints = 0;
		eError = PVRSRV_OK;
		goto e0;
	}

	eError = _SyncFbLookupProcHandle((IMG_HANDLE) (uintptr_t) hFence,
	                                 PVRSRV_HANDLE_TYPE_PVRSRV_FENCE_SERVER,
	                                 IMG_TRUE,
	                                 (void**)&psFence,
	                                 &psHBase);
	PVR_GOTO_IF_ERROR(eError, e0);

	apsCheckpoints = OSAllocMem(sizeof(*apsCheckpoints) * psFence->uiNumSyncs);
	PVR_LOG_GOTO_IF_NOMEM(apsCheckpoints, eError, e1);

	OSLockAcquire(gsSyncFbContext.hFbContextLock);

	/* Increase refcount to make sure fence is not destroyed while waiting */
	_SyncFbFenceAcquire(psFence);

	/* Go through all syncs and add them to the list */
	for (i = 0; i < psFence->uiNumSyncs; i++)
	{
		psSyncPt = psFence->apsFenceSyncList[i];

		psNode = dllist_get_next_node(&psSyncPt->sSignalCallbacks);
		psSyncCB = IMG_CONTAINER_OF(psNode, PVRSRV_SYNC_SIGNAL_CB, sCallbackNode);

		if (_SyncFbSyncPtHandleType(psSyncCB) == PVRSRV_SYNC_HANDLE_PVR)
		{
			psCheckpoint = (PSYNC_CHECKPOINT) psSyncCB->hAttachedSync;
			apsCheckpoints[uiNumCheckpoints++] = psCheckpoint;
		}
	}

	SyncFbFenceRelease(psFence);

	OSLockRelease(gsSyncFbContext.hFbContextLock);

	*puiNumCheckpoints = uiNumCheckpoints;
	*papsCheckpoints = apsCheckpoints;

e1:
	PVRSRVReleaseHandle(psHBase,
	                    (IMG_HANDLE) (uintptr_t) hFence,
	                    PVRSRV_HANDLE_TYPE_PVRSRV_FENCE_SERVER);
e0:
	return eError;
}
#endif

PVRSRV_ERROR SyncFbRegisterSyncFunctions(void)
{
	/* Initialise struct and register with sync_checkpoint.c */
	gsSyncFbContext.sSyncCheckpointReg.pfnFenceResolve = &SyncFbFenceResolvePVR;
	gsSyncFbContext.sSyncCheckpointReg.pfnFenceCreate = &SyncFbFenceCreatePVR;
	gsSyncFbContext.sSyncCheckpointReg.pfnFenceDataRollback = &SyncFbFenceRollbackPVR;
	gsSyncFbContext.sSyncCheckpointReg.pfnFenceFinalise = NULL; /* no fence finalise function required */
	gsSyncFbContext.sSyncCheckpointReg.pfnNoHWUpdateTimelines = &_SyncFbTimelineUpdate_NotifyCMD;
	gsSyncFbContext.sSyncCheckpointReg.pfnFreeCheckpointListMem = OSFreeMem;
	gsSyncFbContext.sSyncCheckpointReg.pfnDumpInfoOnStalledUFOs = &SyncFbDumpInfoOnStalledUFOs;
	OSStringLCopy(gsSyncFbContext.sSyncCheckpointReg.pszImplName, "SyncFb", SYNC_CHECKPOINT_IMPL_MAX_STRLEN);
#if defined(PDUMP)
	gsSyncFbContext.sSyncCheckpointReg.pfnSyncFenceGetCheckpoints = &SyncFbFenceGetCheckpoints;
#endif

	return SyncCheckpointRegisterFunctions(&gsSyncFbContext.sSyncCheckpointReg);
}

PVRSRV_ERROR SyncFbRegisterDevice(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR eError;
	SYNC_FB_CONTEXT_DEVICE_LIST *psNewDeviceEntry;

	PVR_DPF_ENTERED;

	/* Initialise the sync fallback context */
	if (gsSyncFbContext.hSyncEventObject == NULL)
	{
		eError = OSEventObjectCreate("Sync event object",
		                             &gsSyncFbContext.hSyncEventObject);
		PVR_GOTO_IF_ERROR(eError, e1);

		dllist_init(&gsSyncFbContext.sTlList);
		dllist_init(&gsSyncFbContext.sFenceList);
		dllist_init(&gsSyncFbContext.sDeviceList);
		gsSyncFbContext.sCheckpointHashTable = HASH_Create_Extended(64, sizeof(IMG_UINT32), _SyncCheckpointFWAddrHash, _SyncCheckpointFWAddrCompare);

		eError = OSLockCreate(&gsSyncFbContext.hFbContextLock);
		PVR_GOTO_IF_ERROR(eError, e2);

		eError = PVRSRVRegisterCmdCompleteNotify(&gsSyncFbContext.hCMDNotify,
		                                         &_SyncFbTimelineUpdate_NotifyCMD,
		                                         &gsSyncFbContext);
		PVR_GOTO_IF_ERROR(eError, e3);
	}

	psNewDeviceEntry = OSAllocMem(sizeof(*psNewDeviceEntry));
	PVR_GOTO_IF_NOMEM(psNewDeviceEntry, eError, e4);

	OSLockAcquire(gsSyncFbContext.hFbContextLock);
	dllist_add_to_tail(&gsSyncFbContext.sDeviceList, &psNewDeviceEntry->sDeviceListNode);
	OSLockRelease(gsSyncFbContext.hFbContextLock);

	psNewDeviceEntry->psDevice = psDeviceNode;

	eError = PVRSRVRegisterDeviceDbgRequestNotify(&psNewDeviceEntry->hDBGNotify,
	                                              psDeviceNode,
	                                              _SyncFbDebugRequest,
	                                              DEBUG_REQUEST_FALLBACKSYNC,
	                                              NULL);
	PVR_GOTO_IF_ERROR(eError, e5);

	PVR_DPF_RETURN_RC(eError);


e5:
	OSLockAcquire(gsSyncFbContext.hFbContextLock);
	dllist_remove_node(&psNewDeviceEntry->sDeviceListNode);
	OSLockRelease(gsSyncFbContext.hFbContextLock);
	OSFreeMem(psNewDeviceEntry);
e4:
	PVRSRVUnregisterCmdCompleteNotify(gsSyncFbContext.hCMDNotify);
e3:
	OSLockDestroy(gsSyncFbContext.hFbContextLock);
e2:
	OSEventObjectDestroy(gsSyncFbContext.hSyncEventObject);
e1:
	PVR_DPF_RETURN_RC(eError);
}

PVRSRV_ERROR SyncFbDeregisterDevice(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	SYNC_FB_CONTEXT_DEVICE_LIST *psDeviceEntry;
	PDLLIST_NODE psNode, psNext;

	PVR_DPF_ENTERED;

	/* Return if Init was never called */
	if (gsSyncFbContext.hSyncEventObject == NULL)
		goto e1;

	/* Check device list for the given device and remove it */
	dllist_foreach_node(&gsSyncFbContext.sDeviceList, psNode, psNext)
	{
		psDeviceEntry = IMG_CONTAINER_OF(psNode,
		                                 SYNC_FB_CONTEXT_DEVICE_LIST,
		                                 sDeviceListNode);

		if (psDeviceEntry->psDevice == psDeviceNode)
		{
			PVRSRVUnregisterDeviceDbgRequestNotify(psDeviceEntry->hDBGNotify);

			OSLockAcquire(gsSyncFbContext.hFbContextLock);
			dllist_remove_node(psNode);
			OSLockRelease(gsSyncFbContext.hFbContextLock);

			OSFreeMem(psDeviceEntry);
			break;
		}
	}

	/* If there are still devices registered with us don't deinit module */
	if (!dllist_is_empty(&gsSyncFbContext.sDeviceList))
	{
		goto e1;
	}

	PVRSRVUnregisterCmdCompleteNotify(gsSyncFbContext.hCMDNotify);


	eError = OSEventObjectDestroy(gsSyncFbContext.hSyncEventObject);
	PVR_LOG_IF_ERROR(eError, "OSEventObjectDestroy");

	gsSyncFbContext.hSyncEventObject = NULL;

	OSLockDestroy(gsSyncFbContext.hFbContextLock);

e1:
	return eError;
}

/* HOLD TL LOCK!
 * Creates a new sync point on a timeline */
static PVRSRV_ERROR _SyncFbSyncPtCreate(PVRSRV_SYNC_PT **ppsSyncPt,
                                       PVRSRV_TIMELINE_SERVER *psTl,
                                       IMG_UINT32 uiSeqNumber)
{
	PVRSRV_ERROR eError;
	PVRSRV_SYNC_PT *psNewSyncPt;

	PVR_DPF_ENTERED;

	psNewSyncPt = OSAllocMem(sizeof(*psNewSyncPt));
	PVR_LOG_GOTO_IF_NOMEM(psNewSyncPt, eError, e1);

	psNewSyncPt->psTl = psTl;
	OSAtomicWrite(&psNewSyncPt->iStatus, PVRSRV_SYNC_NOT_SIGNALLED);

	psNewSyncPt->uiSeqNum = uiSeqNumber;
	psNewSyncPt->uiPID = OSGetCurrentClientProcessIDKM();
	PT_REF_SET(&psNewSyncPt->iRef, 1,psNewSyncPt);

	dllist_init(&psNewSyncPt->sTlSyncList);
	dllist_init(&psNewSyncPt->sTlSyncActiveList);
	dllist_init(&psNewSyncPt->sSignalCallbacks);

	/* Increment Tl ref due to new checkpoint*/
	_SyncFbTimelineAcquire(psTl);

	dllist_add_to_tail(&psTl->sSyncList, &psNewSyncPt->sTlSyncList);
	dllist_add_to_tail(&psTl->sSyncActiveList, &psNewSyncPt->sTlSyncActiveList);

	*ppsSyncPt = psNewSyncPt;

	PVR_DPF_RETURN_OK;

e1:
	PVR_DPF_RETURN_RC(eError);
}

/* Increment sync point refcount */
static void _SyncFbSyncPtAcquire(PVRSRV_SYNC_PT *psSyncPt)
{
	PT_REF_INC(&psSyncPt->iRef, psSyncPt);
}

/* Release and maybe destroy sync point if refcount is 0 */
static PVRSRV_ERROR _SyncFbSyncPtRelease(PVRSRV_SYNC_PT *psSyncPt,
                                         IMG_BOOL bError)
{
	PVRSRV_ERROR eError;
	PDLLIST_NODE psNode, psNext;
	PVRSRV_SYNC_SIGNAL_CB *psSyncCB;
	IMG_INT iRef;

	PVR_DPF_ENTERED1(psSyncPt);

	iRef = PT_REF_DEC(&psSyncPt->iRef, psSyncPt);
	if (iRef != 0)
	{
		eError = PVRSRV_OK;
		goto e1;
	}

	OSLockAcquire(gsSyncFbContext.hFbContextLock);
	/* Remove all checkpoints from the ufo lookup table.*/
	dllist_foreach_node(&psSyncPt->sSignalCallbacks, psNode, psNext)
	{
		psSyncCB = IMG_CONTAINER_OF(psNode,
		                            PVRSRV_SYNC_SIGNAL_CB,
		                            sCallbackNode);

		if (_SyncFbSyncPtHandleType(psSyncCB) == PVRSRV_SYNC_HANDLE_PVR)
		{
			IMG_UINT32 ui32FwAddr = SyncCheckpointGetFirmwareAddr((PSYNC_CHECKPOINT) psSyncCB->hAttachedSync);

			DBG(("%s: Removing key <%#08x>", __func__, ui32FwAddr));
			HASH_Remove_Extended(gsSyncFbContext.sCheckpointHashTable, &ui32FwAddr);
		}
	}
	OSLockRelease(gsSyncFbContext.hFbContextLock);

	OSLockAcquire(psSyncPt->psTl->hTlLock);
	if (dllist_node_is_in_list(&psSyncPt->sTlSyncActiveList))
		dllist_remove_node(&psSyncPt->sTlSyncActiveList);

	dllist_remove_node(&psSyncPt->sTlSyncList);

	if (bError)
	{
		_SyncFbSyncPtSignalAttached(psSyncPt, PVRSRV_SYNC_ERRORED);
	}
	OSLockRelease(psSyncPt->psTl->hTlLock);

	/* Remove all attached nodes and signal them.*/
	while (!dllist_is_empty(&psSyncPt->sSignalCallbacks))
	{
		psNode = dllist_get_next_node(&psSyncPt->sSignalCallbacks);
		psSyncCB = IMG_CONTAINER_OF(psNode,
		                            PVRSRV_SYNC_SIGNAL_CB,
		                            sCallbackNode);

		psSyncCB->pfnSyncFree(psSyncCB->hAttachedSync);
		dllist_remove_node(&psSyncCB->sCallbackNode);
		OSFreeMem(psSyncCB);
	}

	eError = SyncFbTimelineRelease(psSyncPt->psTl);
	PVR_LOG_IF_ERROR(eError, "SyncFbTimelineRelease");

	OSFreeMem(psSyncPt);

	PVR_DPF_RETURN_OK;

e1:
	PVR_DPF_RETURN_RC(eError);
}

/* HOLD TL LOCK!
 * Mark all attached syncs of a sync point with the state eSignal */
static PVRSRV_ERROR _SyncFbSyncPtSignalAttached(PVRSRV_SYNC_PT *psSyncPt,
                                                PVRSRV_SYNC_STATE eSignal)
{
	PVRSRV_ERROR eError = PVRSRV_OK, eRet;
	PDLLIST_NODE psCurrentCB, psNextCB;
	PVRSRV_SYNC_SIGNAL_CB *psCB;

	PVR_DPF_ENTERED1(psSyncPt);

	if (dllist_is_empty(&psSyncPt->sSignalCallbacks))
	{
		ERR("Sync pt has no attached syncs. Make sure to attach one "
		    "when creating a new sync pt to define its flavour");
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto e1;
	}

	dllist_foreach_node(&psSyncPt->sSignalCallbacks, psCurrentCB, psNextCB)
	{
		psCB = IMG_CONTAINER_OF(psCurrentCB,
		                        PVRSRV_SYNC_SIGNAL_CB,
		                        sCallbackNode);
		eRet = psCB->pfnSignal(psCB->hAttachedSync, eSignal);
		if (eRet != PVRSRV_OK)
		{
			ERR("Failed to signal an attached sync, system might block!");
			eError = eRet;
			/* Don't jump to exit but try to signal remaining syncs */
		}
	}

e1:
	PVR_DPF_RETURN_RC1(eError, psSyncPt);
}

/* HOLD TL LOCK!
 * Mark all a sync point with the state eSignal */
static PVRSRV_ERROR _SyncFbSyncPtSignal(PVRSRV_SYNC_PT *psSyncPt, PVRSRV_SYNC_STATE eSignal)
{
	PVR_DPF_ENTERED1(psSyncPt);

	OSAtomicWrite(&psSyncPt->iStatus, eSignal);

	if (psSyncPt->uiSeqNum >
	    OSAtomicRead(&psSyncPt->psTl->iLastSignalledSeqNum))
	{
		OSAtomicWrite(&psSyncPt->psTl->iLastSignalledSeqNum,
		              psSyncPt->uiSeqNum);
	}

	PVR_DPF_RETURN_OK;
}

/* Check whether all syncs in a fence were signalled */
static IMG_BOOL _SyncFbFenceSyncsHaveSignalled(PVRSRV_FENCE_SERVER *psFence)
{
	IMG_UINT32 i;

	PVR_DPF_ENTERED1(psFence);

	for (i = 0; i < psFence->uiNumSyncs; i++)
	{
		if (OSAtomicRead(&psFence->apsFenceSyncList[i]->iStatus) ==
			PVRSRV_SYNC_NOT_SIGNALLED)
		{
			PVR_DPF_RETURN_RC1(IMG_FALSE, psFence);
		}
	}

	OSAtomicWrite(&psFence->iStatus,
	              PVRSRV_FENCE_SIGNALLED);

	PVR_DPF_RETURN_RC1(IMG_TRUE, psFence);
}

/* Increment timeline refcount */
static void _SyncFbTimelineAcquire(PVRSRV_TIMELINE_SERVER *psTl)
{
	TL_REF_INC(&psTl->iRef, psTl);
}

PVRSRV_ERROR SyncFbTimelineRelease(PVRSRV_TIMELINE_SERVER *psTl)
{
	IMG_INT iRef;

	PVR_DPF_ENTERED1(psTl);

	iRef = TL_REF_DEC(&psTl->iRef, psTl);
	if (iRef != 0)
	{
		PVR_DPF_RETURN_OK;
	}

	if (_SyncFbTimelineHandleType(psTl) == PVRSRV_SYNC_HANDLE_SW)
	{
		_SyncSWTimelineCheckForUnsignalledPts(psTl);
	}

	_SyncFbTimelineListDel(psTl);

	OSLockDestroy(psTl->hTlLock);

#if defined(DEBUG)
	psTl->sTlOps.pfnSyncPtHasSignalled = NULL;
	psTl->hTlLock = NULL;
#endif

	OSFreeMem(psTl);

	PVR_DPF_RETURN_OK;
}

/* Increment fence refcount */
static void _SyncFbFenceAcquire(PVRSRV_FENCE_SERVER *psFence)
{
	FENCE_REF_INC(&psFence->iRef, psFence);
}

PVRSRV_ERROR SyncFbFenceRelease(PVRSRV_FENCE_SERVER *psFence)
{
	PVRSRV_ERROR eError = PVRSRV_OK, eRet;
	IMG_INT iRef;
	IMG_UINT32 i;
	IMG_BOOL bError = IMG_FALSE;
	IMG_BOOL bCleanup = IMG_FALSE;

	PVR_DPF_ENTERED1(psFence);

	/* If cleanup thread, force cleanup of SW fences, otherwise
	 * simply decrement psFence->iRef
	 */
	if (OSGetCurrentClientProcessIDKM() == PVRSRVGetPVRSRVData()->cleanupThreadPid)
	{
		IMG_UINT32 uiSyncPt;

		/* Force erroring of any SW sync pts this fence may contain */
		for (uiSyncPt=0; uiSyncPt < psFence->uiNumSyncs; uiSyncPt++)
		{
			PVRSRV_SYNC_PT *psSyncPt = psFence->apsFenceSyncList[uiSyncPt];

			/* If this is a SW sync point from a timeline which was created
			 * by the process being cleaned-up, then error it
			 */
			if ((_SyncFbTimelineHandleType(psSyncPt->psTl) == PVRSRV_SYNC_HANDLE_SW) &&
			    (PVRSRVGetPurgeConnectionPid() == psSyncPt->psTl->uiPID))
			{
				OSLockAcquire(psSyncPt->psTl->hTlLock);
				_SyncFbSyncPtSignalAttached(psSyncPt, PVRSRV_SYNC_ERRORED);
				OSLockRelease(psSyncPt->psTl->hTlLock);
			}
		}
	}

	iRef = FENCE_REF_DEC(&psFence->iRef, psFence);
	if (iRef != 0)
	{
		goto e1;
	}

	PDUMPCOMMENTWITHFLAGS(psFence->psDevNode, 0,
	                      "Destroy Fence %s (ID:%"IMG_UINT64_FMTSPEC")",
	                      psFence->pszName,
	                      psFence->iUID);

	if (OSGetCurrentClientProcessIDKM() ==
			PVRSRVGetPVRSRVData()->cleanupThreadPid)
	{
		bCleanup = IMG_TRUE;
	}

	_SyncFbFenceListDel(psFence);

	for (i = 0; i < psFence->uiNumSyncs; i++)
	{
		PVRSRV_SYNC_PT *psSyncPt = psFence->apsFenceSyncList[i];

		if (bCleanup &&
				_SyncFbTimelineHandleType(psSyncPt->psTl) == PVRSRV_SYNC_HANDLE_SW)
		{
			bError = IMG_TRUE;
		}

		eRet = _SyncFbSyncPtRelease(psSyncPt,
		                            bError);
		if (eRet != PVRSRV_OK)
		{
			ERR("Error when releasing SyncPt, this might leak memory")
			eError = eRet;
			/* Try to continue and release the other sync pts, return error */
		}
	}

#if defined(DEBUG)
	{
		for (i = 0; i < psFence->uiNumSyncs; i++)
		{
			psFence->apsFenceSyncList[i] = NULL;
		}
		psFence->uiNumSyncs = 0;
	}
#endif

	OSFreeMem(psFence->apsFenceSyncList);
	OSFreeMem(psFence);

e1:
	PVR_DPF_RETURN_RC(eError);
}

PVRSRV_ERROR SyncFbFenceDup(PVRSRV_FENCE_SERVER *psInFence,
                            PVRSRV_FENCE_SERVER **ppsOutFence)
{
	PVR_DPF_ENTERED1(psInFence);

	FENCE_REF_INC(&psInFence->iRef, psInFence);

	PDUMPCOMMENTWITHFLAGS(psInFence->psDevNode, 0,
	                      "Dup Fence %s (ID:%"IMG_UINT64_FMTSPEC").",
	                      psInFence->pszName,
	                      psInFence->iUID);

	*ppsOutFence = psInFence;

	PVR_DPF_RETURN_RC1(PVRSRV_OK, *ppsOutFence);
}

static IMG_BOOL _SyncFbSyncPtHasSignalled(PVRSRV_SYNC_PT *psSyncPt)
{
	return psSyncPt->psTl->sTlOps.pfnSyncPtHasSignalled(psSyncPt);
}

static IMG_BOOL _SyncFbFenceAddPt(PVRSRV_FENCE_SERVER *psFence,
                                  IMG_UINT32 *i,
                                  PVRSRV_SYNC_PT *psSyncPt)
{
	/*
	 * If the fence is signalled there is no need to add it to the fence.
	 * One exception is PDUMP drivers where we need to make sure we
	 * set up proper synchronisation in the pdump stream.
	 */
#if !defined(PDUMP)
	if (_SyncFbSyncPtHasSignalled(psSyncPt)) return IMG_FALSE;
#endif
	_SyncFbSyncPtAcquire(psSyncPt);
	psFence->apsFenceSyncList[*i] = psSyncPt;
	(*i)++;
	return IMG_TRUE;
}

PVRSRV_ERROR SyncFbFenceMerge(PVRSRV_FENCE_SERVER *psInFence1,
                              PVRSRV_FENCE_SERVER *psInFence2,
                              IMG_UINT32 uiFenceNameSize,
                              const IMG_CHAR *pszFenceName,
                              PVRSRV_FENCE_SERVER **ppsOutFence)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_FENCE_SERVER *psNewFence;
	IMG_UINT32 i, i1, i2;
	IMG_UINT32 uiFenceSyncListSize;

	PVR_DPF_ENTERED;

	psNewFence = OSAllocMem(sizeof(*psNewFence));
	PVR_LOG_GOTO_IF_NOMEM(psNewFence, eError, e1);

	psNewFence->psDevNode = psInFence1->psDevNode;

	uiFenceSyncListSize = sizeof(*(psNewFence->apsFenceSyncList)) *
			(psInFence1->uiNumSyncs + psInFence2->uiNumSyncs);

	psNewFence->apsFenceSyncList = OSAllocMem(uiFenceSyncListSize);
	PVR_LOG_GOTO_IF_NOMEM(psNewFence->apsFenceSyncList, eError, e2);

	if (uiFenceNameSize == 1)
	{
		OSSNPrintf(psNewFence->pszName,
		           SYNC_FB_FENCE_MAX_LENGTH,
		           "Fence-Merged");
	}
	else
	{
		if (pszFenceName)
		{
			OSStringLCopy(psNewFence->pszName,
						  pszFenceName,
						  SYNC_FB_FENCE_MAX_LENGTH);
		}
		else
		{
			psNewFence->pszName[0] = '\0';
		}
	}

	/* Add sync pts from input fence 1 & 2
	 * - no duplicates in one timeline
	 * - sync pts in one fence are ordered by timeline UID
	 *
	 * */
	for (i = 0, i1 = 0, i2 = 0;
	     i1 < psInFence1->uiNumSyncs && i2 < psInFence2->uiNumSyncs;)
	{
		PVRSRV_SYNC_PT *psSyncPt1 = psInFence1->apsFenceSyncList[i1];
		PVRSRV_SYNC_PT *psSyncPt2 = psInFence2->apsFenceSyncList[i2];

		/* Adding sync pts in order of their timeline UID, smaller ID first */
		if (psSyncPt1->psTl->iUID <
            psSyncPt2->psTl->iUID)
		{
			_SyncFbFenceAddPt(psNewFence, &i, psSyncPt1);
			i1++;
		}
		else if (psSyncPt1->psTl->iUID >
		         psSyncPt2->psTl->iUID)
		{
			_SyncFbFenceAddPt(psNewFence, &i, psSyncPt2);
			i2++;
		}
		/* In case the timeline UID is the same just add the point that is
		 * later on that timeline. */
		else
		{
			/* --> Some C magic to find out if 'a' is a point later in the
			 * timeline than 'b', wrap around is taken into account:
			 *			(a - b <= ((IMG_INT)(~0U>>1)) ) */
			if ( psSyncPt1->uiSeqNum - psSyncPt2->uiSeqNum <=
			    ((IMG_INT)(~0U>>1)) )
			{
				_SyncFbFenceAddPt(psNewFence, &i, psSyncPt1);
			}
			else
			{
				_SyncFbFenceAddPt(psNewFence, &i, psSyncPt2);
			}

			i1++;
			i2++;
		}
	}

	/* Add the remaining syncs pts to the fence. At this point we only enter
	 * either the first or the second loop because one fence has
	 * more sync pts than the other.
	 */
	for (; i1 < psInFence1->uiNumSyncs; i1++)
	{
		_SyncFbFenceAddPt(psNewFence, &i, psInFence1->apsFenceSyncList[i1]);
	}

	for (; i2 < psInFence2->uiNumSyncs; i2++)
	{
		_SyncFbFenceAddPt(psNewFence, &i, psInFence2->apsFenceSyncList[i2]);
	}

	/* Fill remaining fields */
	psNewFence->uiNumSyncs = i;
	psNewFence->iUID = (IMG_INT64)(uintptr_t) psNewFence;
	FENCE_REF_SET(&psNewFence->iRef, 1, psNewFence);

	OSAtomicWrite(&psNewFence->iStatus, PVRSRV_SYNC_NOT_SIGNALLED);

	_SyncFbFenceListAdd(psNewFence);

	PDUMPCOMMENTWITHFLAGS(psInFence1->psDevNode, 0,
	                      "Merge Fence1 %s (ID:%"IMG_UINT64_FMTSPEC"), Fence2 %s (ID:%"IMG_UINT64_FMTSPEC") "
	                      "to Fence %s (ID:%"IMG_UINT64_FMTSPEC")",
	                      psInFence1->pszName,
	                      psInFence1->iUID,
	                      psInFence2->pszName,
	                      psInFence2->iUID,
	                      psNewFence->pszName,
	                      psNewFence->iUID);

	*ppsOutFence = psNewFence;

	PVR_DPF_RETURN_RC1(eError, *ppsOutFence);

e2:
	OSFreeMem(psNewFence);
e1:
	PVR_DPF_RETURN_RC(eError);
}


PVRSRV_ERROR SyncFbFenceWait(PVRSRV_FENCE_SERVER *psFence,
                             IMG_UINT32 ui32TimeoutInMs)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_HANDLE hOSEvent;
	IMG_UINT32 t1 = 0, t2 = 0;

	PVR_DPF_ENTERED1(psFence);

	/* Increase refcount to make sure fence is not destroyed while waiting */
	_SyncFbFenceAcquire(psFence);

	if (OSAtomicRead(&psFence->iStatus) == PVRSRV_FENCE_NOT_SIGNALLED)
	{
		PVRSRV_ERROR eErrorClose;

		/* If the status of the fence is not signalled it could mean that
		 * there are actually syncs still pending or that we have not
		 * checked yet whether the syncs were met, therefore do the
		 * check now and return in case they are. If they are not, go
		 * to sleep and wait. */

		if (_SyncFbFenceSyncsHaveSignalled(psFence))
		{
			goto e1;
		}
		else if (ui32TimeoutInMs == 0)
		{
			eError = PVRSRV_ERROR_TIMEOUT;
			goto e1;
		}

		eError = OSEventObjectOpen(gsSyncFbContext.hSyncEventObject,
		                           &hOSEvent);
		PVR_GOTO_IF_ERROR(eError, e1);

		while (!_SyncFbFenceSyncsHaveSignalled(psFence) && ui32TimeoutInMs)
		{
			t1 = OSClockms();
			/* Wait for EO to be signalled */
			eError = OSEventObjectWaitTimeout(hOSEvent,
			                                  ui32TimeoutInMs * 1000);
			t2 = OSClockms();

			if (eError != PVRSRV_OK && eError != PVRSRV_ERROR_TIMEOUT)
			{
				break;
			}


			/* Reduce timeout by the time we have just waited */
			if (ui32TimeoutInMs < (t2-t1))
			{
				ui32TimeoutInMs = 0;
			}
			else
			{
				ui32TimeoutInMs -= (t2-t1);
			}
		}

		eErrorClose = OSEventObjectClose(hOSEvent);
		if (eErrorClose != PVRSRV_OK)
		{
			ERR("Unable to close Event Object");

			/* Do not overwrite previous error
			 * if it was something else than PVRSRV_OK */
			if (eError == PVRSRV_OK)
			{
				eError = eErrorClose;
			}
		}
	}
e1:

	SyncFbFenceRelease(psFence);

	PVR_DPF_RETURN_RC1(eError, psFence);
}

PVRSRV_ERROR SyncFbFenceDump(PVRSRV_FENCE_SERVER *psFence,
                             IMG_UINT32 uiLine,
                             IMG_UINT32 uiFileNameLength,
                             const IMG_CHAR *pszFile,
                             IMG_UINT32 uiModuleLength,
                             const IMG_CHAR *pszModule,
                             IMG_UINT32 uiDescLength,
                             const IMG_CHAR *pszDesc)
{

	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 i;


	PVR_DPF_ENTERED1(psFence);

	PVR_LOG(("  Fence dump request from:"));
#if defined(DEBUG)
	PVR_LOG(("    %s (%s:%u)", pszModule, pszFile, uiLine));
#else
	PVR_LOG(("    %s (location only available in debug build)", pszModule));
#endif
	PVR_LOG(("  Desc: %s", pszDesc));
	PVR_LOG(("---------------- FENCE ----------------"));
	PVR_LOG(("%s (UID: %"IMG_UINT64_FMTSPEC")", psFence->pszName, psFence->iUID));

	PVR_LOG(("  Signalled: %s",
	        _SyncFbFenceSyncsHaveSignalled(psFence)?"Yes":"No"));
	PVR_LOG(("  Ref: %d", OSAtomicRead(&psFence->iRef) ));

	PVR_LOG(("  Sync Points:"));
	for (i = 0; i < psFence->uiNumSyncs; i++)
	{
		PVRSRV_SYNC_PT *psSP = psFence->apsFenceSyncList[i];
		PVR_LOG(("    Point %u)", i));
		PVR_LOG(("      On timeline:     %s (UID: %"IMG_UINT64_FMTSPEC")",
		         psSP->psTl->pszName, psSP->psTl->iUID));
		PVR_LOG(("      Sequence number: %u", psSP->uiSeqNum));
		PVR_LOG(("      Signalled:       %s",
		        psSP->psTl->sTlOps.pfnSyncPtHasSignalled(psSP)? "Yes":"No"));
		PVR_LOG(("      Ref:             %d", OSAtomicRead(&psSP->iRef)));
	}
	PVR_LOG(("----------------------------------------"));

	PVR_DPF_RETURN_RC1(eError, psFence);
}

static PVRSRV_ERROR _SyncFbTimelineCreate(PFN_SYNC_PT_HAS_SIGNALLED pfnHasPtSignalled,
                                          IMG_UINT32 uiTimelineNameSize,
                                          const IMG_CHAR *pszTimelineName,
                                          PVRSRV_TIMELINE_SERVER **ppsTimeline)
{
	PVRSRV_ERROR eError;
	PVRSRV_TIMELINE_SERVER *psNewTl;

	PVR_DPF_ENTERED;

	PVR_LOG_GOTO_IF_INVALID_PARAM(ppsTimeline, eError, e1);

	psNewTl = OSAllocMem(sizeof(*psNewTl));
	PVR_LOG_GOTO_IF_NOMEM(psNewTl, eError, e2);

	eError = OSLockCreate(&psNewTl->hTlLock);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSLockCreate", e3);

	if (uiTimelineNameSize == 1)
	{
		OSSNPrintf(psNewTl->pszName,
		           SYNC_FB_TIMELINE_MAX_LENGTH,
		           "TL-%s-%d",
		           OSGetCurrentClientProcessNameKM(),
		           OSGetCurrentClientProcessIDKM());
	}
	else
	{
		if (pszTimelineName)
		{
			OSStringLCopy((IMG_CHAR*) psNewTl->pszName,
			              pszTimelineName,
			              SYNC_FB_TIMELINE_MAX_LENGTH);
		}
		else
		{
			psNewTl->pszName[0] = '\0';
		}
	}

	dllist_init(&psNewTl->sSyncList);
	dllist_init(&psNewTl->sSyncActiveList);
	dllist_init(&psNewTl->sTlList);

	_SyncFbFTimelineListAdd(psNewTl);

	psNewTl->sTlOps.pfnSyncPtHasSignalled = pfnHasPtSignalled;
	psNewTl->iUID = (IMG_INT64)(uintptr_t) psNewTl; /* Not unique throughout the driver lifetime */
	OSAtomicWrite(&psNewTl->iSeqNum, 0);
	OSAtomicWrite(&psNewTl->iLastSignalledSeqNum, 0);
	psNewTl->uiPID = OSGetCurrentClientProcessIDKM();

	/* Set initial refcount value */
	TL_REF_SET(&psNewTl->iRef, 1, psNewTl);

	*ppsTimeline = psNewTl;

	PVR_DPF_RETURN_RC1(PVRSRV_OK, psNewTl);

e3:
	OSFreeMem(psNewTl);
e2:
e1:
	PVR_DPF_RETURN_RC(eError);
}

/*****************************************************************************/
/*                                                                           */
/*                         PVR SPECIFIC FUNCTIONS                            */
/*                                                                           */
/*****************************************************************************/

/* Free a PVR sync point with its sync checkpoint */
static void _SyncFbSyncPtFreePVR(IMG_HANDLE hSync)
{
	PVR_DPF_ENTERED1(hSync);

	SyncCheckpointFree((PSYNC_CHECKPOINT) hSync);

	PVR_DPF_RETURN;
}


/* Mark a sync checkpoint with the given state.
 * MAKE SURE TO WAKE UP FW AFTER CALLING THIS */
static PVRSRV_ERROR _SyncFbSyncPtSignalPVR(IMG_HANDLE hSync,
                                           PVRSRV_SYNC_STATE eState)
{
	PSYNC_CHECKPOINT psSyncCheck = (PSYNC_CHECKPOINT) hSync;

	PVR_DPF_ENTERED1(hSync);

	if (!SyncCheckpointIsSignalled(psSyncCheck, PVRSRV_FENCE_FLAG_SUPPRESS_HWP_PKT))
	{
		switch (eState)
		{
			case PVRSRV_SYNC_SIGNALLED:
				SyncCheckpointSignal(psSyncCheck, PVRSRV_FENCE_FLAG_NONE);
				break;
			case PVRSRV_SYNC_ERRORED:
				SyncCheckpointError(psSyncCheck, PVRSRV_FENCE_FLAG_NONE);
				break;
			default:
				ERR("Passed unknown sync state, "
						"please use a valid one for signalling.");
				return PVRSRV_ERROR_INVALID_PARAMS;
		}
	}

	PVR_DPF_RETURN_RC1(PVRSRV_OK, hSync);
}

/* Check whether the native sync of the SyncPt has signalled.
 *
 * HOLD TL LOCK!
 */
static IMG_BOOL _SyncFbSyncPtHasSignalledPVR(PVRSRV_SYNC_PT *psSyncPt)
{
	PDLLIST_NODE psCBNode;
	PVRSRV_SYNC_SIGNAL_CB *psCB;
	PSYNC_CHECKPOINT psSyncCheck;
	IMG_BOOL bRet = IMG_FALSE;

	PVR_DPF_ENTERED1(psSyncPt);

	/* If the SyncPt is not signalled yet,
	 * check whether the first attached sync has.
	 *
	 * Change SyncPt state to signalled or errored if yes.
	 * Also notify other attached syncs.
	 */
	if (OSAtomicRead(&psSyncPt->iStatus) == PVRSRV_SYNC_NOT_SIGNALLED)
	{
		/* List must have at least the device sync attached if we are called */
		PVR_ASSERT(!dllist_is_empty(&psSyncPt->sSignalCallbacks));

		/* Retrieve the first sync checkpoint of that sync pt */
		psCBNode = dllist_get_next_node(&psSyncPt->sSignalCallbacks);
		psCB = IMG_CONTAINER_OF(psCBNode, PVRSRV_SYNC_SIGNAL_CB, sCallbackNode);
		psSyncCheck = (PSYNC_CHECKPOINT) psCB->hAttachedSync;

		if (SyncCheckpointIsSignalled(psSyncCheck, PVRSRV_FENCE_FLAG_SUPPRESS_HWP_PKT))
		{
			_SyncFbSyncPtSignal(psSyncPt, PVRSRV_SYNC_SIGNALLED);

			/* Signal all other attached syncs */
			PVR_LOG_IF_ERROR(_SyncFbSyncPtSignalAttached(psSyncPt, PVRSRV_SYNC_SIGNALLED),
			                 "_SyncFbSyncPtSignalAttached");

			bRet = IMG_TRUE;
		}

		PVR_DPF_RETURN_RC1(bRet, psSyncPt);
	}
	else
	{
		PVR_DPF_RETURN_RC1(IMG_TRUE, psSyncPt);
	}
}

PVRSRV_ERROR SyncFbTimelineCreatePVR(IMG_UINT32 uiTimelineNameSize,
                                     const IMG_CHAR *pszTimelineName,
                                     PVRSRV_TIMELINE_SERVER **ppsTimeline)
{
	return _SyncFbTimelineCreate(&_SyncFbSyncPtHasSignalledPVR,
	                             uiTimelineNameSize,
	                             pszTimelineName,
	                             ppsTimeline);
}

PVRSRV_ERROR SyncFbFenceCreatePVR(PPVRSRV_DEVICE_NODE psDeviceNode,
                                  const IMG_CHAR *pszName,
                                  PVRSRV_TIMELINE iTl,
                                  PSYNC_CHECKPOINT_CONTEXT psSyncCheckpointContext,
                                  PVRSRV_FENCE *piOutFence,
                                  IMG_UINT64 *puiFenceUID,
                                  void **ppvFenceFinaliseData,
                                  PSYNC_CHECKPOINT *ppsOutCheckpoint,
                                  void **ppvTimelineUpdateSync,
                                  IMG_UINT32 *puiTimelineUpdateValue)
{
	PVRSRV_ERROR eError;
	PVRSRV_FENCE_SERVER *psNewFence;
	PVRSRV_SYNC_PT *psNewSyncPt = NULL;
	PVRSRV_SYNC_SIGNAL_CB *psNewSyncSignalCB;
	PVRSRV_HANDLE_BASE	*psHandleBase;
	PVRSRV_TIMELINE_SERVER *psTl;
	IMG_HANDLE hOutFence;
	IMG_UINT32 ui32FwAddr;

	PVR_UNREFERENCED_PARAMETER(ppvTimelineUpdateSync);
	PVR_UNREFERENCED_PARAMETER(puiTimelineUpdateValue);

	PVR_DPF_ENTERED;

	/* The fallback implementation does not need to finalise
	 * the fence, so set the ppvFenceFinaliseData to NULL
	 * (if provided)
	 */
	if (ppvFenceFinaliseData != NULL )
	{
		*ppvFenceFinaliseData = NULL;
	}

	PVR_LOG_GOTO_IF_INVALID_PARAM(pszName, eError, e0);
	PVR_LOG_GOTO_IF_INVALID_PARAM(piOutFence, eError, e0);
	PVR_LOG_GOTO_IF_INVALID_PARAM(ppsOutCheckpoint, eError, e0);

	eError = _SyncFbLookupProcHandle((IMG_HANDLE) (uintptr_t) iTl,
	                                 PVRSRV_HANDLE_TYPE_PVRSRV_TIMELINE_SERVER,
	                                 IMG_TRUE,
	                                 (void**) &psTl,
	                                 &psHandleBase);
	PVR_GOTO_IF_ERROR(eError, e0);

	if (unlikely(_SyncFbTimelineHandleType(psTl) != PVRSRV_SYNC_HANDLE_PVR))
	{
		PVR_LOG_GOTO_WITH_ERROR("_SyncFbTimelineHandleType", eError, PVRSRV_ERROR_INVALID_PARAMS, e1);
	}

	/* Allocate:
	 *		Fence
	 *		Sync Signal CB
	 *		SyncPt List
	 *		Sync Checkpoint
	 *		SyncPt
	 *		Handle
	 *	Setup
	 */
	psNewFence = OSAllocMem(sizeof(*psNewFence));
	PVR_LOG_GOTO_IF_NOMEM(psNewFence, eError, e2);

	psNewSyncSignalCB = OSAllocMem(sizeof(*psNewSyncSignalCB));
	PVR_LOG_GOTO_IF_NOMEM(psNewSyncSignalCB, eError, e3);

	psNewFence->apsFenceSyncList = OSAllocMem(sizeof(*(psNewFence->apsFenceSyncList)));
	PVR_LOG_GOTO_IF_NOMEM(psNewFence->apsFenceSyncList, eError, e4);

	psNewFence->psDevNode = psDeviceNode;

	/* Lock down TL until new point is fully created and inserted */
	OSLockAcquire(psTl->hTlLock);

	eError = _SyncFbSyncPtCreate(&psNewSyncPt,
	                             psTl,
	                             OSAtomicIncrement(&psTl->iSeqNum));
	if (unlikely(eError != PVRSRV_OK))
	{
		OSLockRelease(psTl->hTlLock);
		ERR("Cannot allocate SyncPt.");
		goto e5;
	}

	eError = SyncCheckpointAlloc(psSyncCheckpointContext,
	                             iTl,
	                             PVRSRV_NO_FENCE,
	                             pszName,
	                             ppsOutCheckpoint);
	if (unlikely(eError != PVRSRV_OK))
	{
		OSLockRelease(psTl->hTlLock);
		ERR("Cannot allocate SyncCheckpoint.");
		goto e6;
	}

	ui32FwAddr = SyncCheckpointGetFirmwareAddr(*ppsOutCheckpoint);

	/* Init Sync Signal CB */
	psNewSyncSignalCB->hAttachedSync = (IMG_HANDLE) *ppsOutCheckpoint;
	psNewSyncSignalCB->pfnSignal = &_SyncFbSyncPtSignalPVR;
	psNewSyncSignalCB->pfnSyncFree = &_SyncFbSyncPtFreePVR;
	psNewSyncSignalCB->hPrivData = (IMG_HANDLE) psSyncCheckpointContext;

	dllist_add_to_tail(&psNewSyncPt->sSignalCallbacks,
	                   &psNewSyncSignalCB->sCallbackNode);

	OSLockRelease(psTl->hTlLock);

	DBG(("%s: Inserting key <%p> = %p", __func__, *ppsOutCheckpoint, psNewSyncPt));
	OSLockAcquire(gsSyncFbContext.hFbContextLock);
	HASH_Insert_Extended(gsSyncFbContext.sCheckpointHashTable, (void *)&ui32FwAddr, (uintptr_t) psNewSyncPt);
	OSLockRelease(gsSyncFbContext.hFbContextLock);

	/* Init Fence */
	OSStringLCopy(psNewFence->pszName,
	              pszName,
	              SYNC_FB_FENCE_MAX_LENGTH);

	psNewFence->apsFenceSyncList[0] = psNewSyncPt;
	psNewFence->uiNumSyncs = 1;
	FENCE_REF_SET(&psNewFence->iRef, 1, psNewFence);
	OSAtomicWrite(&psNewFence->iStatus, PVRSRV_SYNC_NOT_SIGNALLED);
	psNewFence->iUID = (IMG_INT64)(uintptr_t) psNewFence; /* Not unique throughout the driver lifetime */

	eError = PVRSRVAllocHandle(psHandleBase,
	                           &hOutFence,
	                           (void*) psNewFence,
	                           PVRSRV_HANDLE_TYPE_PVRSRV_FENCE_SERVER,
	                           PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
	                           (PFN_HANDLE_RELEASE) &SyncFbFenceRelease);
	PVR_LOG_GOTO_IF_ERROR(eError, "PVRSRVAllocHandle", e7);

	_SyncFbFenceListAdd(psNewFence);

	PDUMPCOMMENTWITHFLAGS(psNewFence->psDevNode, 0,
	                      "Allocated PVR Fence %s (ID:%"IMG_UINT64_FMTSPEC") with Checkpoint (ID:%d) "
	                      "on Timeline %s (ID:%"IMG_UINT64_FMTSPEC")",
	                      psNewFence->pszName,
	                      psNewFence->iUID,
	                      SyncCheckpointGetId(psNewSyncSignalCB->hAttachedSync),
	                      psTl->pszName,
	                      psTl->iUID);

	PVRSRVReleaseHandle(psHandleBase,
	                    (IMG_HANDLE) (uintptr_t) iTl,
	                    PVRSRV_HANDLE_TYPE_PVRSRV_TIMELINE_SERVER);

	*puiFenceUID = psNewFence->iUID;
	*piOutFence = (PVRSRV_FENCE) (uintptr_t) hOutFence;

	PVR_DPF_RETURN_RC1(PVRSRV_OK, psNewFence);

e7:
	SyncCheckpointFree(*ppsOutCheckpoint);
e6:
	_SyncFbSyncPtRelease(psNewSyncPt, IMG_FALSE);
e5:
	OSFreeMem(psNewFence->apsFenceSyncList);
e4:
	OSFreeMem(psNewSyncSignalCB);
e3:
	OSFreeMem(psNewFence);
e2:
e1:
	PVRSRVReleaseHandle(psHandleBase,
	                    (IMG_HANDLE) (uintptr_t) iTl,
	                    PVRSRV_HANDLE_TYPE_PVRSRV_TIMELINE_SERVER);
e0:
	PVR_DPF_RETURN_RC(eError);
}

/* Resolve caller has to free the sync checkpoints and free the
 * array that holds the pointers. */
PVRSRV_ERROR SyncFbFenceResolvePVR(PSYNC_CHECKPOINT_CONTEXT psContext,
                                   PVRSRV_FENCE iFence,
                                   IMG_UINT32 *puiNumCheckpoints,
                                   PSYNC_CHECKPOINT **papsCheckpoints,
                                   IMG_UINT64 *pui64FenceUID)
{
	PVRSRV_ERROR eError;
	PVRSRV_FENCE_SERVER *psFence;
	PVRSRV_HANDLE_BASE *psHBase;
	PSYNC_CHECKPOINT *apsCheckpoints;
	PSYNC_CHECKPOINT psCheckpoint;
	PVRSRV_SYNC_SIGNAL_CB *psSyncCB, *psNewSyncCB;
	PVRSRV_SYNC_PT *psSyncPt;
	PDLLIST_NODE psNode;
	IMG_UINT32 i, uiNumCheckpoints = 0;

	PVR_DPF_ENTERED;

	if (iFence == PVRSRV_NO_FENCE)
	{
		*puiNumCheckpoints = 0;
		eError = PVRSRV_OK;
		goto e0;
	}

	eError = _SyncFbLookupProcHandle((IMG_HANDLE) (uintptr_t) iFence,
	                                 PVRSRV_HANDLE_TYPE_PVRSRV_FENCE_SERVER,
	                                 IMG_TRUE,
	                                 (void**)&psFence,
	                                 &psHBase);
	PVR_GOTO_IF_ERROR(eError, e0);

	apsCheckpoints = OSAllocMem(sizeof(*apsCheckpoints) * psFence->uiNumSyncs);
	PVR_LOG_GOTO_IF_NOMEM(apsCheckpoints, eError, e1);

	/* Go through all syncs and add them to the list */
	for (i = 0; i < psFence->uiNumSyncs; i++)
	{
		psNewSyncCB = NULL;
		psSyncPt = psFence->apsFenceSyncList[i];

		/* Don't skip signalled fences on PDUMP to make sure we set up proper
		   synchronisation in the pdump stream. */
#if !defined(PDUMP)
		if (_SyncFbSyncPtHasSignalled(psSyncPt))
		{
			continue;
		}
#endif

		OSLockAcquire(gsSyncFbContext.hFbContextLock);
		OSLockAcquire(psSyncPt->psTl->hTlLock);
		psNode = dllist_get_next_node(&psSyncPt->sSignalCallbacks);
		psSyncCB = IMG_CONTAINER_OF(psNode, PVRSRV_SYNC_SIGNAL_CB, sCallbackNode);

		/* If we have a sync checkpoint AND
		 * it uses the same context as the given one,
		 * just add the checkpoint to the resolve list.*/
		if ((_SyncFbSyncPtHandleType(psSyncCB) == PVRSRV_SYNC_HANDLE_PVR) &&
		    (psContext == (PSYNC_CHECKPOINT_CONTEXT) psSyncCB->hPrivData))
		{
			psCheckpoint = (PSYNC_CHECKPOINT) psSyncCB->hAttachedSync;
		}
		/* Else create a new sync checkpoint in the given context */
		else
		{
			IMG_UINT32 ui32FwAddr;

			eError = SyncCheckpointAlloc(psContext,
			                             SYNC_CHECKPOINT_FOREIGN_CHECKPOINT,
			                             iFence,
			                             psFence->pszName,
			                             &psCheckpoint);
			if (eError != PVRSRV_OK)
			{
				OSLockRelease(psSyncPt->psTl->hTlLock);
				OSLockRelease(gsSyncFbContext.hFbContextLock);
				goto e2;
			}

			psNewSyncCB = OSAllocMem(sizeof(*psNewSyncCB));
			if (psNewSyncCB == NULL)
			{
				eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				OSLockRelease(psSyncPt->psTl->hTlLock);
				OSLockRelease(gsSyncFbContext.hFbContextLock);
				goto e3;
			}

			psNewSyncCB->hAttachedSync = (IMG_HANDLE) psCheckpoint;
			psNewSyncCB->hPrivData = (IMG_HANDLE) psContext;
			psNewSyncCB->pfnSignal = &_SyncFbSyncPtSignalPVR;
			psNewSyncCB->pfnSyncFree = &_SyncFbSyncPtFreePVR;
			dllist_add_to_tail(&psFence->apsFenceSyncList[i]->sSignalCallbacks,
			                   &psNewSyncCB->sCallbackNode);

			/* Insert the checkpoint into the firmware address lookup table,
			 * in case this checkpoint gets errored by the host driver. */
			ui32FwAddr = SyncCheckpointGetFirmwareAddr(psCheckpoint);

			DBG(("%s: Inserting key (fwAddr0x%x)<%p> = %p", __func__, ui32FwAddr, psCheckpoint, psSyncPt));
			HASH_Insert_Extended(gsSyncFbContext.sCheckpointHashTable, (void *)&ui32FwAddr, (uintptr_t) psSyncPt);

			/* If the existing sync pt has already been signalled, then signal
			 * this new sync too */
			if (_SyncFbSyncPtHasSignalled(psFence->apsFenceSyncList[i]))
			{
				_SyncFbSyncPtSignalPVR(psNewSyncCB->hAttachedSync, PVRSRV_SYNC_SIGNALLED);
			}
		}
		OSLockRelease(psSyncPt->psTl->hTlLock);
		OSLockRelease(gsSyncFbContext.hFbContextLock);

		/* Take a reference, resolve caller is responsible
		 * to drop it after use */
		eError = SyncCheckpointTakeRef(psCheckpoint);
		PVR_GOTO_IF_ERROR(eError, e4);

		apsCheckpoints[uiNumCheckpoints++] = psCheckpoint;
	}

	*pui64FenceUID = psFence->iUID;
	*puiNumCheckpoints = uiNumCheckpoints;
	*papsCheckpoints = apsCheckpoints;

	PVRSRVReleaseHandle(psHBase,
	                    (IMG_HANDLE) (uintptr_t) iFence,
	                    PVRSRV_HANDLE_TYPE_PVRSRV_FENCE_SERVER);

	PVR_DPF_RETURN_OK;

e4:
	if (psNewSyncCB)
		OSFreeMem(psNewSyncCB);
e3:
	SyncCheckpointFree(psCheckpoint);
e2:
	for (; i > 0; i--)
	{
		SyncCheckpointDropRef(apsCheckpoints[i-1]);
		SyncCheckpointFree(apsCheckpoints[i-1]);
	}

	OSFreeMem(apsCheckpoints);
e1:
	PVRSRVReleaseHandle(psHBase,
	                    (IMG_HANDLE) (uintptr_t) iFence,
	                    PVRSRV_HANDLE_TYPE_PVRSRV_FENCE_SERVER);
e0:
	PVR_DPF_RETURN_RC(eError);
}

/* In case something went wrong after FenceCreate we can roll back (destroy)
 * the fence in the server */
static PVRSRV_ERROR SyncFbFenceRollbackPVR(PVRSRV_FENCE iFence, void *pvFenceData)
{
	PVRSRV_ERROR eError;

	PVR_DPF_ENTERED;
	PVR_UNREFERENCED_PARAMETER(pvFenceData);

	PVR_GOTO_IF_INVALID_PARAM(iFence != PVRSRV_NO_FENCE, eError, e1);

	eError = _SyncFbDestroyHandle((IMG_HANDLE) (uintptr_t) iFence,
	                              PVRSRV_HANDLE_TYPE_PVRSRV_FENCE_SERVER);
	PVR_GOTO_IF_ERROR(eError, e1);

	PVR_DPF_RETURN_OK;

e1:
	PVR_DPF_RETURN_RC(eError);
}

/* Dump debug info on syncs relating to any of the FWAddrs in the
 * given array. This is called when rgx_ccb.c determines we have a
 * stalled CCB, so this debug will aid in debugging which sync(s)
 * have failed to signal.
 */
IMG_UINT32 SyncFbDumpInfoOnStalledUFOs(IMG_UINT32 nr_ufos, IMG_UINT32 *vaddrs)
{
	IMG_UINT32 ui32NumFallbackUfos = 0;
	PDLLIST_NODE psFenceNode, psNextFenceNode;
	PVRSRV_FENCE_SERVER *psFence;
	IMG_UINT32 *pui32NextFWAddr = vaddrs;
	IMG_UINT32 ui32CurrentUfo;

	OSLockAcquire(gsSyncFbContext.hFbContextLock);

	for (ui32CurrentUfo=0; ui32CurrentUfo<nr_ufos; ui32CurrentUfo++)
	{
		if (pui32NextFWAddr)
		{
			/* Iterate over all fences */
			dllist_foreach_node(&gsSyncFbContext.sFenceList,
								psFenceNode,
								psNextFenceNode)
			{
				IMG_UINT32 i;
				IMG_BOOL bFenceDumped = IMG_FALSE;
				IMG_UINT32			  ui32SyncPtBitmask = 0;
				IMG_UINT32			  ui32SyncCheckpointFWAddr = 0;
				PVRSRV_SYNC_PT		  *psSyncPt = NULL;

				psFence = IMG_CONTAINER_OF(psFenceNode,
										   PVRSRV_FENCE_SERVER,
										   sFenceListNode);

				/* ... all sync points in the fence */
				for (i = 0; i < psFence->uiNumSyncs; i++)
				{
					PDLLIST_NODE psCBNode, psNextCBNode;

					psSyncPt = psFence->apsFenceSyncList[i];

					dllist_foreach_node(&psSyncPt->sSignalCallbacks,
										psCBNode,
										psNextCBNode)
					{
						PVRSRV_SYNC_SIGNAL_CB *psCb = IMG_CONTAINER_OF(psCBNode,
						                                               PVRSRV_SYNC_SIGNAL_CB,
						                                               sCallbackNode);

						switch (_SyncFbSyncPtHandleType(psCb))
						{
							case PVRSRV_SYNC_HANDLE_PVR:
							{
								ui32SyncCheckpointFWAddr = SyncCheckpointGetFirmwareAddr(psCb->hAttachedSync);
								ui32SyncPtBitmask |= 1;
								break;
							}
							case PVRSRV_SYNC_HANDLE_SW:
							{
								ui32SyncPtBitmask |= 2;
								break;
							}
							default:
								break;
						}
					}
				}

				if ((ui32SyncPtBitmask == 0x3) &&
					(ui32SyncCheckpointFWAddr == *pui32NextFWAddr))
				{
					/* Print fence info (if not already done so) */
					if (!bFenceDumped)
					{
						PVR_LOG(("Fence: %s, ID: %"IMG_UINT64_FMTSPEC", %s - (0x%p)",
								  psFence->pszName,
								  psFence->iUID,
								  _SyncFbFenceSyncsHaveSignalled(psFence) ?
										  "Signalled" : "Pending  ",
								  psFence));
						bFenceDumped = IMG_TRUE;
					}
					_SyncFbDebugRequestPrintSyncPt(psSyncPt,
					                               IMG_TRUE,
												   NULL,
												   NULL);
					ui32NumFallbackUfos++;
				}
			}
			pui32NextFWAddr++;
		}
	}
	OSLockRelease(gsSyncFbContext.hFbContextLock);

	return ui32NumFallbackUfos;
}

static inline PVRSRV_SYNC_STATE _SyncFbCheckpointToSyncState(PVRSRV_SYNC_CHECKPOINT_STATE eState)
{
	switch (eState)
	{
		case PVRSRV_SYNC_CHECKPOINT_SIGNALLED:
			return PVRSRV_SYNC_SIGNALLED;
		case PVRSRV_SYNC_CHECKPOINT_ERRORED:
			return PVRSRV_SYNC_ERRORED;
		case PVRSRV_SYNC_CHECKPOINT_ACTIVE:
			return PVRSRV_SYNC_NOT_SIGNALLED;
		default:
			PVR_ASSERT("Unknown sync checkpoint state");
			return PVRSRV_SYNC_ERRORED;
	}
}

IMG_BOOL SyncFbCheckpointHasSignalled(IMG_UINT32 ui32FwAddr, IMG_UINT32 ui32Value)
{
	PVRSRV_SYNC_PT *psSyncPt;
	PVRSRV_SYNC_STATE eSignal;
	PVRSRV_SYNC_SIGNAL_CB *psCB;
	PDLLIST_NODE psCurrentCB, psNextCB;

	PVR_DPF_ENTERED1(ui32FwAddr);

	OSLockAcquire(gsSyncFbContext.hFbContextLock);

	DBG(("%s: Looking up key (fwAddr=0x%x)", __func__, ui32FwAddr));
	psSyncPt = (PVRSRV_SYNC_PT *) HASH_Retrieve_Extended(gsSyncFbContext.sCheckpointHashTable, (void *) &ui32FwAddr);
	DBG(("%s: Found sync pt <%p>", __func__, psSyncPt));

	if (!psSyncPt)
	{
		/* Sync Fallback does not know about this UFO, skip processing it. */
		goto err_release;
	}

	eSignal = _SyncFbCheckpointToSyncState(ui32Value);
	_SyncFbSyncPtSignal(psSyncPt, eSignal);

	/*
	 * Signal all attachments except for the one representing the signalled
	 * checkpoint.
	 */

	if (dllist_is_empty(&psSyncPt->sSignalCallbacks))
	{
		ERR("Sync pt has no attached syncs. Make sure to attach one "
		    "when creating a new sync pt to define its flavour");
	}

	dllist_foreach_node(&psSyncPt->sSignalCallbacks, psCurrentCB, psNextCB)
	{
		psCB = IMG_CONTAINER_OF(psCurrentCB,
		                        PVRSRV_SYNC_SIGNAL_CB,
		                        sCallbackNode);

		if (_SyncFbSyncPtHandleType(psCB) == PVRSRV_SYNC_HANDLE_PVR &&
		    SyncCheckpointGetFirmwareAddr(psCB->hAttachedSync) == ui32FwAddr)
		{
			continue;
		}

		if (psCB->pfnSignal(psCB->hAttachedSync, eSignal) != PVRSRV_OK)
		{
			ERR("Failed to signal an attached sync, system might block!");
			/* Don't jump to exit but try to signal remaining syncs */
		}
	}

	OSLockRelease(gsSyncFbContext.hFbContextLock);

	PVR_DPF_RETURN_RC(IMG_TRUE);

err_release:
	OSLockRelease(gsSyncFbContext.hFbContextLock);
	PVR_DPF_RETURN_RC(IMG_FALSE);
}

/*****************************************************************************/
/*                                                                           */
/*                         SW SPECIFIC FUNCTIONS                            */
/*                                                                           */
/*****************************************************************************/

/* Free a SW sync point with its sync checkpoint */
static void _SyncFbSyncPtFreeSW(IMG_HANDLE hSync)
{
	PVR_DPF_ENTERED1(hSync);

	OSFreeMem(hSync);

	PVR_DPF_RETURN;
}

static IMG_BOOL _SyncFbSyncPtHasSignalledSW(PVRSRV_SYNC_PT *psSyncPt)
{
	PDLLIST_NODE psCBNode;
	PVRSRV_SYNC_SIGNAL_CB *psCB;
	PVRSRV_SYNC_PT_SW *psSWSyncPt;
	IMG_BOOL bRet = IMG_FALSE;

	PVR_DPF_ENTERED1(psSyncPt);

	/* If the SyncPt has not been signalled yet,
	 * check whether the first attached sync has.
	 *
	 * Change SyncPt state to signalled or errored if yes.
	 * Also notify other attached syncs.
	 */
	if (OSAtomicRead(&psSyncPt->iStatus) == PVRSRV_SYNC_NOT_SIGNALLED)
	{
		/* List must have at least the device sync attached if we are called */
		PVR_ASSERT(!dllist_is_empty(&psSyncPt->sSignalCallbacks));

		/* Retrieve the first sync checkpoint of that sync pt */
		psCBNode = dllist_get_next_node(&psSyncPt->sSignalCallbacks);
		psCB = IMG_CONTAINER_OF(psCBNode, PVRSRV_SYNC_SIGNAL_CB, sCallbackNode);
		psSWSyncPt = (PVRSRV_SYNC_PT_SW*) psCB->hAttachedSync;

		if (psSWSyncPt->bSignalled)
		{
			_SyncFbSyncPtSignal(psSyncPt, PVRSRV_SYNC_SIGNALLED);

			/* Signal all other attached syncs */
			PVR_LOG_IF_ERROR(_SyncFbSyncPtSignalAttached(psSyncPt, PVRSRV_SYNC_SIGNALLED),
			                 "_SyncFbSyncPtSignalAttached");

			bRet = IMG_TRUE;
		}

		PVR_DPF_RETURN_RC1(bRet, psSyncPt);
	}
	else
	{
		PVR_DPF_RETURN_RC1(IMG_TRUE, psSyncPt);
	}
}

/* Mark an attached sw sync pt with the given state.
 * MAKE SURE TO WAKE UP FW AFTER CALLING THIS (if enqueued)*/
static PVRSRV_ERROR _SyncFbSyncPtSignalSW(IMG_HANDLE hSync,
                                          PVRSRV_SYNC_STATE eState)
{
	PVRSRV_SYNC_PT_SW *psSWSyncPt = (PVRSRV_SYNC_PT_SW*) hSync;

	PVR_DPF_ENTERED1(hSync);

	if (!psSWSyncPt->bSignalled)
	{
		switch (eState)
		{
			case PVRSRV_SYNC_SIGNALLED:
			case PVRSRV_SYNC_ERRORED:
				psSWSyncPt->bSignalled = IMG_TRUE;
				break;
			default:
				ERR("Passed unknown sync state (%d), "
						"please use a valid one for signalling.",
						eState);
				return PVRSRV_ERROR_INVALID_PARAMS;
		}
	}

	PVR_DPF_RETURN_RC1(PVRSRV_OK, hSync);
}

PVRSRV_ERROR SyncFbTimelineCreateSW(IMG_UINT32 uiTimelineNameSize,
                                    const IMG_CHAR *pszTimelineName,
                                    PVRSRV_TIMELINE_SERVER **ppsTimeline)
{
	return _SyncFbTimelineCreate(&_SyncFbSyncPtHasSignalledSW,
	                             uiTimelineNameSize,
	                             pszTimelineName,
	                             ppsTimeline);
}

/*****************************************************************************/
/*                                                                           */
/*                       SOFTWARE_TIMELINE FUNCTIONS                         */
/*                                                                           */
/*****************************************************************************/
static PVRSRV_ERROR _SyncFbSWTimelineFenceCreate(PVRSRV_DEVICE_NODE *psDeviceNode,
                                          PVRSRV_TIMELINE_SERVER *psTl,
                                          IMG_UINT32 uiFenceNameSize,
                                          const IMG_CHAR *pszFenceName,
                                          PVRSRV_FENCE_SERVER **ppsOutputFence,
                                          IMG_UINT64 *pui64SyncPtIdx)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_FENCE_SERVER *psNewFence;
	PVRSRV_SYNC_PT *psNewSyncPt = NULL;
	PVRSRV_SYNC_PT_SW *psNewSWSyncPt;
	PVRSRV_SYNC_SIGNAL_CB *psNewSyncSignalCB;
	IMG_INT iNextSeqNum;

	if (_SyncFbTimelineHandleType(psTl) != PVRSRV_SYNC_HANDLE_SW)
	{
		ERR("Passed timeline is not a SW timeline.");
		eError = PVRSRV_ERROR_NOT_SW_TIMELINE;
		goto e1;
	}

	/* Allocate:
	 *		Fence
	 *		Sync Signal CB
	 *		SyncPt List
	 *		SW Sync
	 *		SyncPt
	 *		Handle
	 *	Setup
	 */
	psNewFence = OSAllocMem(sizeof(*psNewFence));
	PVR_LOG_GOTO_IF_NOMEM(psNewFence, eError, e1);

	psNewFence->psDevNode = psDeviceNode;

	psNewSyncSignalCB = OSAllocMem(sizeof(*psNewSyncSignalCB));
	PVR_LOG_GOTO_IF_NOMEM(psNewSyncSignalCB, eError, e2);

	psNewFence->apsFenceSyncList = OSAllocMem(sizeof(*(psNewFence->apsFenceSyncList)));
	PVR_LOG_GOTO_IF_NOMEM(psNewFence->apsFenceSyncList, eError, e3);

	psNewSWSyncPt = OSAllocMem(sizeof(*psNewSWSyncPt));
	PVR_LOG_GOTO_IF_NOMEM(psNewSWSyncPt, eError, e4);

	/* Lock down TL until new point is fully created and inserted */
	OSLockAcquire(psTl->hTlLock);

	/* Sample our next sync pt value - we won't actually increment
	 * iSeqNum for this SW timeline until we know the fence has been
	 * successfully created.
	 */
	iNextSeqNum = OSAtomicRead(&psTl->iSeqNum) + 1;

	eError = _SyncFbSyncPtCreate(&psNewSyncPt, psTl, iNextSeqNum);
	if (eError != PVRSRV_OK)
	{
		OSLockRelease(psTl->hTlLock);
		goto e5;
	}

	if (OSAtomicRead(&psTl->iLastSignalledSeqNum) < psNewSyncPt->uiSeqNum)
	{
		psNewSWSyncPt->bSignalled = IMG_FALSE;
	}
	else
	{
		psNewSWSyncPt->bSignalled = IMG_TRUE;
		OSAtomicWrite(&psNewSyncPt->iStatus, PVRSRV_SYNC_SIGNALLED);
	}

	/* Init Sync Signal CB */
	psNewSyncSignalCB->hAttachedSync = (IMG_HANDLE) psNewSWSyncPt;
	psNewSyncSignalCB->pfnSignal = &_SyncFbSyncPtSignalSW;
	psNewSyncSignalCB->pfnSyncFree = &_SyncFbSyncPtFreeSW;
	psNewSyncSignalCB->hPrivData = NULL;

	dllist_add_to_tail(&psNewSyncPt->sSignalCallbacks,
					   &psNewSyncSignalCB->sCallbackNode);

	/* Now that the fence has been created, increment iSeqNum */
	OSAtomicIncrement(&psTl->iSeqNum);

	OSLockRelease(psTl->hTlLock);

	if (pszFenceName)
	{
		/* Init Fence */
		OSStringLCopy(psNewFence->pszName,
		              pszFenceName,
		              SYNC_FB_FENCE_MAX_LENGTH);
	}
	else
	{
		psNewFence->pszName[0] = '\0';
	}

	psNewFence->apsFenceSyncList[0] = psNewSyncPt;
	psNewFence->uiNumSyncs = 1;
	FENCE_REF_SET(&psNewFence->iRef, 1, psNewFence);
	OSAtomicWrite(&psNewFence->iStatus, PVRSRV_FENCE_NOT_SIGNALLED);
	psNewFence->iUID = (IMG_INT64)(uintptr_t) psNewFence;

	_SyncFbFenceListAdd(psNewFence);

	PDUMPCOMMENTWITHFLAGS(psDeviceNode, 0,
						  "Allocated SW Fence %s (ID:%"IMG_UINT64_FMTSPEC") with sequence number %u "
						  "on Timeline %s (ID:%"IMG_UINT64_FMTSPEC")",
						  psNewFence->pszName,
						  psNewFence->iUID,
						  psNewSyncPt->uiSeqNum,
						  psTl->pszName,
						  psTl->iUID);

	*ppsOutputFence = psNewFence;
	if (pui64SyncPtIdx != NULL)
	{
		*pui64SyncPtIdx = psNewSyncPt->uiSeqNum;
	}

	PVR_DPF_RETURN_RC1(PVRSRV_OK, psNewFence);

e5:
	OSFreeMem(psNewSWSyncPt);
e4:
	OSFreeMem(psNewFence->apsFenceSyncList);
e3:
	OSFreeMem(psNewSyncSignalCB);
e2:
	OSFreeMem(psNewFence);
e1:
	PVR_DPF_RETURN_RC(eError);
}

/* Kernel mode function (SyncFb implementation) to create fence on a SW timeline */
PVRSRV_ERROR SyncFbSWTimelineFenceCreateKM(PVRSRV_DEVICE_NODE *psDeviceNode,
                                           PVRSRV_TIMELINE iSWTimeline,
                                           const IMG_CHAR *pszFenceName,
                                           PVRSRV_FENCE *piOutputFence,
                                           IMG_UINT64 *pui64SyncPtIdx)
{

	PVRSRV_ERROR eError;
	PVRSRV_FENCE_SERVER *psNewFence;
	PVRSRV_HANDLE_BASE	*psHandleBase;
	PVRSRV_TIMELINE_SERVER *psTl;
	IMG_HANDLE hOutFence;

	PVR_DPF_ENTERED;

	PVR_LOG_GOTO_IF_INVALID_PARAM(piOutputFence, eError, e0);

	/* Lookup up the ST Timeline (and take a reference on it while
	 * we are creating the new sync pt and fence)
	 */
	eError = _SyncFbLookupProcHandle((IMG_HANDLE) (uintptr_t) iSWTimeline,
									 PVRSRV_HANDLE_TYPE_PVRSRV_TIMELINE_SERVER,
									 IMG_TRUE,
									 (void**) &psTl,
									 &psHandleBase);
	PVR_GOTO_IF_ERROR(eError, e0);

	eError = _SyncFbSWTimelineFenceCreate(psDeviceNode, psTl,
	                                      OSStringLength(pszFenceName),
	                                      pszFenceName,
	                                      &psNewFence,
	                                      pui64SyncPtIdx);
	PVR_GOTO_IF_ERROR(eError, e1);

	eError = PVRSRVAllocHandle(psHandleBase,
							   &hOutFence,
							   (void*) psNewFence,
							   PVRSRV_HANDLE_TYPE_PVRSRV_FENCE_SERVER,
							   PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
							   (PFN_HANDLE_RELEASE) &SyncFbFenceRelease);
	PVR_GOTO_IF_ERROR(eError, e2);

	/* Drop the reference we took on the timeline earlier */
	PVRSRVReleaseHandle(psHandleBase,
	                    (IMG_HANDLE) (uintptr_t) iSWTimeline,
	                    PVRSRV_HANDLE_TYPE_PVRSRV_TIMELINE_SERVER);

	*piOutputFence = (PVRSRV_FENCE) (uintptr_t) hOutFence;

	return PVRSRV_OK;

e2:
	/* Release the fence we created, as we failed to
	 * allocate a handle for it */
	SyncFbFenceRelease(psNewFence);

e1:
	/* Drop the reference we took on the timeline earlier */
	PVRSRVReleaseHandle(psHandleBase,
	                    (IMG_HANDLE) (uintptr_t) iSWTimeline,
	                    PVRSRV_HANDLE_TYPE_PVRSRV_TIMELINE_SERVER);
e0:
	PVR_DPF_RETURN_RC(eError);

}

/* Client (bridge) interface to the SyncSWTimelineFenceCreateKM() function */
PVRSRV_ERROR SyncFbFenceCreateSW(CONNECTION_DATA *psConnection,
                                 PVRSRV_DEVICE_NODE *psDeviceNode,
                                 PVRSRV_TIMELINE_SERVER *psTimeline,
                                 IMG_UINT32 uiFenceNameSize,
                                 const IMG_CHAR *pszFenceName,
                                 PVRSRV_FENCE_SERVER **ppsOutputFence,
                                 IMG_UINT64 *pui64SyncPtIdx)
{
	PVRSRV_ERROR eError;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	eError =  _SyncFbSWTimelineFenceCreate(psDeviceNode,
	                                       psTimeline,
	                                       0,
	                                       pszFenceName,
	                                       ppsOutputFence,
	                                       pui64SyncPtIdx);

	return eError;
}

static PVRSRV_ERROR _SyncSWTimelineAdvanceSigErr(PVRSRV_TIMELINE_SERVER *psTl,
                                                 PVRSRV_SYNC_STATE eState,
                                                 IMG_UINT64 *pui64SyncPtIdx)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PDLLIST_NODE psPtNode, psNextNode;
	PVRSRV_SYNC_PT *psSyncPt;
	IMG_INT32 uiTlSeqNum;

	PVR_DPF_ENTERED1(psTl);

	PVR_LOG_GOTO_IF_INVALID_PARAM(psTl, eError, errExit);

	if (_SyncFbTimelineHandleType(psTl) != PVRSRV_SYNC_HANDLE_SW)
	{
		ERR("Passed timeline is not a SW timeline.");
		eError = PVRSRV_ERROR_NOT_SW_TIMELINE;
		goto errExit;
	}

	OSLockAcquire(psTl->hTlLock);

	/* Don't allow incrementing of SW timeline beyond its last created pt */
	if (OSAtomicRead(&psTl->iLastSignalledSeqNum) == OSAtomicRead(&psTl->iSeqNum))
	{
		DBG(("%s: !! TL<%p> (%d->%d/%d) !!", __func__, (void*)psTl, OSAtomicRead(&psTl->iLastSignalledSeqNum), OSAtomicRead(&psTl->iLastSignalledSeqNum)+1, OSAtomicRead(&psTl->iSeqNum)));
		WRN("Attempt to advance SW timeline beyond last created point.");
		eError = PVRSRV_ERROR_SW_TIMELINE_AT_LATEST_POINT;
		goto errUnlockAndExit;
	}

	uiTlSeqNum = OSAtomicIncrement(&psTl->iLastSignalledSeqNum);

	/* Go through list of active sync pts and
	 * signal the points that are met now */
	dllist_foreach_node(&psTl->sSyncActiveList, psPtNode, psNextNode)
	{
		psSyncPt = IMG_CONTAINER_OF(psPtNode,
		                            PVRSRV_SYNC_PT,
		                            sTlSyncActiveList);
		if (psSyncPt->uiSeqNum <= uiTlSeqNum)
		{
			_SyncFbSyncPtSignal(psSyncPt, eState);

			/* Signal all other attached syncs */
			PVR_LOG_IF_ERROR(_SyncFbSyncPtSignalAttached(psSyncPt, eState),
			                 "_SyncFbSyncPtSignalAttached");

			dllist_remove_node(psPtNode);
		}
	}

	OSLockRelease(psTl->hTlLock);

	if (pui64SyncPtIdx != NULL)
	{
		*pui64SyncPtIdx = uiTlSeqNum;
	}

	/* A completed SW operation may un-block the GPU */
	PVRSRVCheckStatus(NULL);

	eError = _SyncFbSignalEO();
	PVR_LOG_GOTO_IF_ERROR(eError, "_SyncFbSignalEO", errExit);

	PVR_DPF_RETURN_OK;

errUnlockAndExit:
	OSLockRelease(psTl->hTlLock);
errExit:
	PVR_DPF_RETURN_RC(eError);
}

static void _SyncSWTimelineCheckForUnsignalledPts(PVRSRV_TIMELINE_SERVER *psTl)
{
	IMG_UINT32 ui32TlSeqNum = OSAtomicRead(&psTl->iSeqNum);
	IMG_UINT32 ui32TlLastSigSeqNum = OSAtomicRead(&psTl->iLastSignalledSeqNum);

	while (ui32TlLastSigSeqNum < ui32TlSeqNum)
	{
		ui32TlLastSigSeqNum++;
		PVR_DPF((PVR_DBG_WARNING,"%s: Found unsignalled SW timeline <%p> '%s' sync pt (%d/%d)",__func__, psTl, psTl->pszName, ui32TlLastSigSeqNum, ui32TlSeqNum));
		_SyncSWTimelineAdvanceSigErr(psTl, PVRSRV_SYNC_ERRORED, NULL);
	}
}

/* kernel mode function to advance a SW timeline */
PVRSRV_ERROR SyncFbSWTimelineAdvanceKM(void *pvSWTimelineObj, IMG_UINT64 *pui64SyncPtIdx)
{
	return _SyncSWTimelineAdvanceSigErr(pvSWTimelineObj,
	                                    PVRSRV_SYNC_SIGNALLED,
	                                    pui64SyncPtIdx);
}

/* Client (bridge) interface to the SyncSWTimelineAdvanceKM() function */
PVRSRV_ERROR SyncFbTimelineAdvanceSW(PVRSRV_TIMELINE_SERVER *psTimeline,
                                     IMG_UINT64 *pui64SyncPtIdx)
{
	return _SyncSWTimelineAdvanceSigErr(psTimeline,
	                                    PVRSRV_SYNC_SIGNALLED,
	                                    pui64SyncPtIdx);
}

PVRSRV_ERROR SyncFbFenceReleaseKM(void *pvFenceObj)
{
	PVRSRV_ERROR eError;

	PVR_DPF_ENTERED1(pvFenceObj);

	PVR_LOG_GOTO_IF_INVALID_PARAM(pvFenceObj, eError, e0);

	eError = SyncFbFenceRelease((PVRSRV_FENCE_SERVER*) pvFenceObj);

e0:
	PVR_DPF_RETURN_RC(eError);
}

PVRSRV_ERROR SyncFbSWGetTimelineObj(PVRSRV_TIMELINE iSWTimeline,
                                    void **ppvSWTimelineObj)
{
	PVRSRV_ERROR eError;
	PVRSRV_HANDLE_BASE *psHB;

	PVR_DPF_ENTERED1(iSWTimeline);

	PVR_LOG_GOTO_IF_INVALID_PARAM(iSWTimeline != PVRSRV_NO_TIMELINE, eError, err_out);

	eError = _SyncFbLookupProcHandle((IMG_HANDLE)(uintptr_t) iSWTimeline,
	                                 PVRSRV_HANDLE_TYPE_PVRSRV_TIMELINE_SERVER,
	                                 IMG_FALSE,
	                                 ppvSWTimelineObj,
	                                 &psHB);
	PVR_LOG_GOTO_IF_ERROR(eError, "_SyncFbLookupProcHandle", err_out);

	_SyncFbTimelineAcquire((PVRSRV_TIMELINE_SERVER*) *ppvSWTimelineObj);

err_out:
	PVR_DPF_RETURN_RC(eError);
}


PVRSRV_ERROR SyncFbGetFenceObj(PVRSRV_FENCE iFence,
                               void **ppvFenceObj)
{
	PVRSRV_ERROR eError;
	PVRSRV_HANDLE_BASE *psHB;

	PVR_DPF_ENTERED1(iFence);

	PVR_LOG_GOTO_IF_INVALID_PARAM(iFence != PVRSRV_NO_FENCE, eError, err_out);

	eError = _SyncFbLookupProcHandle((IMG_HANDLE)(uintptr_t) iFence,
	                                 PVRSRV_HANDLE_TYPE_PVRSRV_FENCE_SERVER,
	                                 IMG_FALSE,
	                                 ppvFenceObj,
	                                 &psHB);
	PVR_LOG_GOTO_IF_ERROR(eError, "_SyncFbLookupProcHandle", err_out);

	_SyncFbFenceAcquire((PVRSRV_FENCE_SERVER*) *ppvFenceObj);

err_out:
	PVR_DPF_RETURN_RC(eError);
}

PVRSRV_ERROR SyncFbDumpFenceKM(void *pvSWFenceObj,
	                           DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
	                           void *pvDumpDebugFile)
{
	PVRSRV_FENCE_SERVER *psFence = (PVRSRV_FENCE_SERVER *) pvSWFenceObj;
	IMG_UINT32 i;

	PVR_DUMPDEBUG_LOG("Fence: %s, %-9s - <%#"IMG_UINT64_FMTSPECx">",
	                  psFence->pszName,
	                  _SyncFbFenceSyncsHaveSignalled(psFence) ?
	                      "Signalled" : "Active",
	                  psFence->iUID);

	/* ... all sync points in the fence */
	for (i = 0; i < psFence->uiNumSyncs; i++)
	{
		_SyncFbDebugRequestPrintSyncPt(psFence->apsFenceSyncList[i],
		                               IMG_TRUE,
		                               pfnDumpDebugPrintf,
		                               pvDumpDebugFile);
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR SyncFbSWDumpTimelineKM(void *pvSWTimelineObj,
                                    DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                    void *pvDumpDebugFile)
{
	PVRSRV_TIMELINE_SERVER *psTl = (PVRSRV_TIMELINE_SERVER *) pvSWTimelineObj;

	OSLockAcquire(psTl->hTlLock);

	PVR_DUMPDEBUG_LOG("TL: %s, SeqNum: %d/%d - <%#"IMG_UINT64_FMTSPECx">",
	                  psTl->pszName,
	                  OSAtomicRead(&psTl->iLastSignalledSeqNum),
	                  OSAtomicRead(&psTl->iSeqNum),
	                  psTl->iUID);

	OSLockRelease(psTl->hTlLock);

	return PVRSRV_OK;
}

/*****************************************************************************/
/*                                                                           */
/*                       IMPORT/EXPORT FUNCTIONS                             */
/*                                                                           */
/*****************************************************************************/

static PVRSRV_ERROR _SyncFbFenceExport(PVRSRV_FENCE_SERVER *psFence,
                                       PVRSRV_FENCE_EXPORT **ppsExport)
{
	PVRSRV_FENCE_EXPORT *psExport;
	PVRSRV_ERROR eError;

	psExport = OSAllocMem(sizeof(*psExport));
	PVR_LOG_GOTO_IF_NOMEM(psExport, eError, err_out);

	_SyncFbFenceAcquire(psFence);

	psExport->psFence = psFence;
	*ppsExport = psExport;

	eError = PVRSRV_OK;
err_out:
	return eError;
}

static PVRSRV_ERROR _SyncFbFenceExportDestroy(PVRSRV_FENCE_EXPORT *psExport)
{
	PVRSRV_ERROR eError;

	eError = SyncFbFenceRelease(psExport->psFence);
	PVR_LOG_IF_ERROR(eError, "SyncFbFenceRelease");

	OSFreeMem(psExport);

	return eError;
}

static PVRSRV_ERROR _SyncFbFenceImport(PVRSRV_FENCE_EXPORT *psImport,
                                       PVRSRV_FENCE_SERVER **psFenceOut)
{
	PVRSRV_ERROR eError;
	PVRSRV_FENCE_SERVER *psFence;

	psFence = psImport->psFence;
	_SyncFbFenceAcquire(psFence);

	*psFenceOut = psFence;

	eError = PVRSRV_OK;
	return eError;
}

#if defined(SUPPORT_INSECURE_EXPORT)
PVRSRV_ERROR SyncFbFenceExportInsecure(PVRSRV_FENCE_SERVER *psFence,
                                       PVRSRV_FENCE_EXPORT **ppsExport)
{
	return _SyncFbFenceExport(psFence, ppsExport);
}

PVRSRV_ERROR SyncFbFenceExportDestroyInsecure(PVRSRV_FENCE_EXPORT *psExport)
{
	return _SyncFbFenceExportDestroy(psExport);
}

PVRSRV_ERROR SyncFbFenceImportInsecure(CONNECTION_DATA *psConnection,
                                       PVRSRV_DEVICE_NODE *psDevice,
                                       PVRSRV_FENCE_EXPORT *psImport,
                                       PVRSRV_FENCE_SERVER **psFenceOut)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psDevice);

	return _SyncFbFenceImport(psImport, psFenceOut);
}
#endif /* defined(SUPPORT_INSECURE_EXPORT) */

PVRSRV_ERROR SyncFbFenceExportDestroySecure(PVRSRV_FENCE_EXPORT *psExport)
{
	PVRSRV_ERROR eError;

	PVR_DPF_ENTERED1(psExport);

	eError = _SyncFbFenceExportDestroy(psExport);

	PVR_DPF_RETURN_RC(eError);
}

static PVRSRV_ERROR _SyncFbReleaseSecureExport(void *pvExport)
{
	return SyncFbFenceExportDestroySecure(pvExport);
}

PVRSRV_ERROR SyncFbFenceExportSecure(CONNECTION_DATA *psConnection,
                                     PVRSRV_DEVICE_NODE * psDevNode,
                                     PVRSRV_FENCE_SERVER *psFence,
                                     IMG_SECURE_TYPE *phSecure,
                                     PVRSRV_FENCE_EXPORT **ppsExport,
                                     CONNECTION_DATA **ppsSecureConnection)
{
	PVRSRV_ERROR eError;
	PVRSRV_FENCE_EXPORT *psExport;

	PVR_DPF_ENTERED1(psFence);

	PVR_UNREFERENCED_PARAMETER(ppsSecureConnection);

	eError = _SyncFbFenceExport(psFence, &psExport);
	PVR_LOG_GOTO_IF_ERROR(eError, "_SyncFbFenceExport", err_out);

	/* Transform it into a secure export */
	eError = OSSecureExport("fallback_fence",
	                        _SyncFbReleaseSecureExport,
	                        (void *) psExport,
	                        phSecure);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSSecureExport", err_export);

	*ppsExport = psExport;
	PVR_DPF_RETURN_OK;
err_export:
	_SyncFbFenceExportDestroy(psExport);
err_out:
	PVR_DPF_RETURN_RC(eError);
}

PVRSRV_ERROR SyncFbFenceImportSecure(CONNECTION_DATA *psConnection,
                                     PVRSRV_DEVICE_NODE *psDevice,
                                     IMG_SECURE_TYPE hSecure,
                                     PVRSRV_FENCE_SERVER **ppsFence)
{
	PVRSRV_ERROR eError;
	PVRSRV_FENCE_EXPORT *psImport;

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psDevice);

	PVR_DPF_ENTERED1(hSecure);

	eError = OSSecureImport(hSecure, (void **) &psImport);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSSecureImport", err_out);

	eError = _SyncFbFenceImport(psImport, ppsFence);

	PVR_DPF_RETURN_OK;
err_out:
	PVR_DPF_RETURN_RC(eError);
}

/*****************************************************************************/
/*                                                                           */
/*                            TESTING FUNCTIONS                              */
/*                                                                           */
/*****************************************************************************/
#if defined(PVR_TESTING_UTILS)

static void _GetCheckContext(PVRSRV_DEVICE_NODE *psDevNode,
                             PSYNC_CHECKPOINT_CONTEXT *ppsSyncCheckpointContext)
{
	*ppsSyncCheckpointContext = psDevNode->hSyncCheckpointContext;
}

PVRSRV_ERROR TestIOCTLSyncFbFenceSignalPVR(CONNECTION_DATA *psConnection,
                                           PVRSRV_DEVICE_NODE *psDevNode,
                                           void *psFenceIn)
{
	PVRSRV_ERROR eError;
	PVRSRV_SYNC_PT *psSyncPt;
	IMG_UINT32 i;
	PVRSRV_FENCE_SERVER *psFence = psFenceIn;

	PVR_DPF_ENTERED;

	for (i = 0; i < psFence->uiNumSyncs; i++)
	{
		psSyncPt = psFence->apsFenceSyncList[i];
		OSAtomicWrite(&psSyncPt->iStatus, PVRSRV_SYNC_SIGNALLED);

		OSLockAcquire(psSyncPt->psTl->hTlLock);
		eError = _SyncFbSyncPtSignalAttached(psSyncPt, PVRSRV_SYNC_SIGNALLED);
		PVR_LOG_GOTO_IF_ERROR(eError, "_SyncFbSyncPtSignalAttached", eSignal);

		OSLockRelease(psSyncPt->psTl->hTlLock);
	}

	eError = _SyncFbSignalEO();
	PVR_LOG_GOTO_IF_ERROR(eError, "_SyncFbSignalEO", eExit);

	PVR_DPF_RETURN_OK;

eSignal:
	OSLockRelease(psSyncPt->psTl->hTlLock);
eExit:
	PVR_DPF_RETURN_RC(eError);
}


PVRSRV_ERROR TestIOCTLSyncFbFenceCreatePVR(CONNECTION_DATA *psConnection,
                                           PVRSRV_DEVICE_NODE *psDevNode,
                                           IMG_UINT32 uiNameLength,
                                           const IMG_CHAR *pszName,
                                           PVRSRV_TIMELINE iTL,
                                           PVRSRV_FENCE *piOutFence)
{
	PSYNC_CHECKPOINT_CONTEXT psContext = NULL;
	PSYNC_CHECKPOINT psCheckpoint;
	PVRSRV_FENCE iFence;
	PVRSRV_ERROR eError;
	IMG_UINT64 uiFenceUID;

	PVR_DPF_ENTERED;

	if (iTL == PVRSRV_NO_TIMELINE)
	{
		WRN("Supplied invalid timeline, returning invalid fence!");
		*piOutFence = PVRSRV_NO_FENCE;

		eError = PVRSRV_OK;
		goto e1;
	}

	_GetCheckContext(psDevNode,
	                 &psContext);

	eError = SyncFbFenceCreatePVR(psDevNode,
	                              pszName,
	                              iTL,
	                              psContext,
	                              &iFence,
	                              &uiFenceUID,
	                              NULL,
	                              &psCheckpoint,
	                              NULL,
	                              NULL);
	PVR_LOG_GOTO_IF_ERROR(eError, "SyncFbFenceCreatePVR", e1);

	*piOutFence = iFence;

	PVR_DPF_RETURN_OK;

e1:
	PVR_DPF_RETURN_RC(eError);
}

PVRSRV_ERROR TestIOCTLSyncFbFenceResolvePVR(CONNECTION_DATA *psConnection,
                                            PVRSRV_DEVICE_NODE *psDevNode,
                                            PVRSRV_FENCE iFence)
{
	PSYNC_CHECKPOINT_CONTEXT psContext = NULL;
	PVRSRV_ERROR eError;
	PSYNC_CHECKPOINT *apsChecks = NULL;
	IMG_UINT32 uiNumChecks, i;
	IMG_UINT64 uiFenceUID;

	PVR_DPF_ENTERED;

	_GetCheckContext(psDevNode,
	                 &psContext);

	eError = SyncFbFenceResolvePVR(psContext,
	                               iFence,
	                               &uiNumChecks,
	                               &apsChecks,
	                               &uiFenceUID);
	PVR_LOG_GOTO_IF_ERROR(eError, "SyncFbFenceResolvePVR", eExit);

	/* Close Checkpoints */
	for (i = 0; i < uiNumChecks; i++)
	{
		SyncCheckpointFree(apsChecks[i]);
	}

	OSFreeMem(apsChecks);

	PVR_DPF_RETURN_OK;

eExit:
	PVR_DPF_RETURN_RC(eError);
}

PVRSRV_ERROR TestIOCTLSyncFbSWTimelineAdvance(CONNECTION_DATA * psConnection,
                                              PVRSRV_DEVICE_NODE *psDevNode,
                                              PVRSRV_TIMELINE iSWTl)
{
	PVRSRV_ERROR eError;
	PVRSRV_TIMELINE_SERVER *psSWTl;
	PVRSRV_HANDLE_BASE *psHB;
	SYNC_TIMELINE_OBJ sSWTimelineObj;

	PVR_DPF_ENTERED;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	eError = _SyncFbLookupProcHandle((IMG_HANDLE) (uintptr_t) iSWTl,
	                                 PVRSRV_HANDLE_TYPE_PVRSRV_TIMELINE_SERVER,
	                                 IMG_FALSE,
	                                 (void**) &psSWTl,
	                                 &psHB);
	PVR_LOG_GOTO_IF_ERROR(eError, "_SyncFbLookupProcHandle", e0);

	sSWTimelineObj.pvTlObj = psSWTl;
	sSWTimelineObj.hTimeline = iSWTl;

	eError = SyncSWTimelineAdvanceKM(psDevNode, &sSWTimelineObj);
	PVR_LOG_GOTO_IF_ERROR(eError, "SyncSWTimelineAdvanceKM", e0);

	PVR_DPF_RETURN_OK;

e0:
	PVR_DPF_RETURN_RC(eError);
}

PVRSRV_ERROR TestIOCTLSyncFbSWFenceCreate(CONNECTION_DATA * psConnection,
                                          PVRSRV_DEVICE_NODE *psDevNode,
                                          PVRSRV_TIMELINE iTl,
                                          IMG_UINT32 uiFenceNameLength,
                                          const IMG_CHAR *pszFenceName,
                                          PVRSRV_FENCE *piFence)
{
	PVRSRV_ERROR eError;

	PVR_DPF_ENTERED;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	eError = SyncSWTimelineFenceCreateKM(psDevNode,
	                                     iTl,
	                                     pszFenceName,
	                                     piFence);
	PVR_LOG_GOTO_IF_ERROR(eError, "SyncSWTimelineFenceCreateKM", e0);

	PVR_DPF_RETURN_OK;

e0:
	PVR_DPF_RETURN_RC(eError);
}

#endif /* PVR_TESTING_UTILS */
