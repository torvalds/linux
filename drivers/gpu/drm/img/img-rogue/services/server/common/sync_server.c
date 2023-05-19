/*************************************************************************/ /*!
@File           sync_server.c
@Title          Server side synchronisation functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side functions that for synchronisation
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
#include "sync_server.h"
#include "allocmem.h"
#include "device.h"
#include "devicemem.h"
#include "devicemem_pdump.h"
#include "osfunc.h"
#include "pdump.h"
#include "pvr_debug.h"
#include "pvr_notifier.h"
#include "pdump_km.h"
#include "sync.h"
#include "sync_internal.h"
#include "connection_server.h"
#include "htbserver.h"
#include "rgxhwperf.h"
#include "info_page.h"

#include "sync_checkpoint_internal.h"
#include "sync_checkpoint.h"

/* Include this to obtain MAX_SYNC_CHECKPOINTS_PER_FENCE */
#include "sync_checkpoint_external.h"

/* Include this to obtain PVRSRV_MAX_DEV_VARS */
#include "pvrsrv_devvar.h"

#if defined(SUPPORT_SECURE_EXPORT)
#include "ossecure_export.h"
#endif

/* Set this to enable debug relating to the construction and maintenance of the sync address list */
#define SYNC_ADDR_LIST_DEBUG 0

/* Set maximum number of FWAddrs that can be accommodated in a SYNC_ADDR_LIST.
 * This should allow for PVRSRV_MAX_DEV_VARS dev vars plus
 * MAX_SYNC_CHECKPOINTS_PER_FENCE sync checkpoints for check fences.
 * The same SYNC_ADDR_LIST is also used to hold UFOs for updates. While this
 * may need to accommodate the additional sync prim update returned by Native
 * sync implementation (used for timeline debug), the size calculated from
 * PVRSRV_MAX_DEV_VARS+MAX_SYNC_CHECKPOINTS_PER_FENCE should be ample.
 */
#define PVRSRV_MAX_SYNC_ADDR_LIST_SIZE (PVRSRV_MAX_DEV_VARS+MAX_SYNC_CHECKPOINTS_PER_FENCE)
/* Check that helper functions will not be preparing longer lists of
 * UFOs than the FW can handle.
 */
static_assert(PVRSRV_MAX_SYNC_ADDR_LIST_SIZE <= RGXFWIF_CCB_CMD_MAX_UFOS,
              "PVRSRV_MAX_SYNC_ADDR_LIST_SIZE > RGXFWIF_CCB_CMD_MAX_UFOS.");

/* Max number of syncs allowed in a sync prim op */
#define SYNC_PRIM_OP_MAX_SYNCS 1024

struct _SYNC_PRIMITIVE_BLOCK_
{
	PVRSRV_DEVICE_NODE	*psDevNode;
	DEVMEM_MEMDESC		*psMemDesc;
	IMG_UINT32			*pui32LinAddr;
	IMG_UINT32			ui32BlockSize;		/*!< Size of the Sync Primitive Block */
	ATOMIC_T			sRefCount;
	DLLIST_NODE			sConnectionNode;
	SYNC_CONNECTION_DATA *psSyncConnectionData;	/*!< Link back to the sync connection data if there is one */
	PRGXFWIF_UFO_ADDR		uiFWAddr;	/*!< The firmware address of the sync prim block */
};

struct _SYNC_CONNECTION_DATA_
{
	DLLIST_NODE	sListHead;  /*!< list of sync block associated with / created against this connection */
	ATOMIC_T	sRefCount;  /*!< number of references to this object */
	POS_LOCK	hLock;      /*!< lock protecting the list of sync blocks */
};

#define DECREMENT_WITH_WRAP(value, sz) ((value) ? ((value) - 1) : ((sz) - 1))

/* this is the max number of syncs we will search or dump
 * at any time.
 */
#define SYNC_RECORD_LIMIT 20000

enum SYNC_RECORD_TYPE
{
	SYNC_RECORD_TYPE_UNKNOWN = 0,
	SYNC_RECORD_TYPE_CLIENT,
	SYNC_RECORD_TYPE_SERVER,
};

struct SYNC_RECORD
{
	PVRSRV_DEVICE_NODE		*psDevNode;
	SYNC_PRIMITIVE_BLOCK	*psServerSyncPrimBlock;	/*!< handle to _SYNC_PRIMITIVE_BLOCK_ */
	IMG_UINT32				ui32SyncOffset;			/*!< offset to sync in block */
	IMG_UINT32				ui32FwBlockAddr;
	IMG_PID					uiPID;
	IMG_UINT64				ui64OSTime;
	enum SYNC_RECORD_TYPE	eRecordType;
	DLLIST_NODE				sNode;
	IMG_CHAR				szClassName[PVRSRV_SYNC_NAME_LENGTH];
};

#if defined(SYNC_DEBUG) || defined(REFCOUNT_DEBUG)
#define SYNC_REFCOUNT_PRINT(fmt, ...) PVRSRVDebugPrintf(PVR_DBG_WARNING, __FILE__, __LINE__, fmt, __VA_ARGS__)
#else
#define SYNC_REFCOUNT_PRINT(fmt, ...)
#endif

#if defined(SYNC_DEBUG)
#define SYNC_UPDATES_PRINT(fmt, ...) PVRSRVDebugPrintf(PVR_DBG_WARNING, __FILE__, __LINE__, fmt, __VA_ARGS__)
#else
#define SYNC_UPDATES_PRINT(fmt, ...)
#endif

/*!
*****************************************************************************
 @Function      : SyncPrimitiveBlockToFWAddr

 @Description   : Given a pointer to a sync primitive block and an offset,
                  returns the firmware address of the sync.

 @Input           psSyncPrimBlock : Sync primitive block which contains the sync
 @Input           ui32Offset      : Offset of sync within the sync primitive block
 @Output          psAddrOut       : Absolute FW address of the sync is written out through
                                    this pointer
 @Return :        PVRSRV_OK on success. PVRSRV_ERROR_INVALID_PARAMS if input
                  parameters are invalid.
*****************************************************************************/

PVRSRV_ERROR
SyncPrimitiveBlockToFWAddr(SYNC_PRIMITIVE_BLOCK *psSyncPrimBlock,
							IMG_UINT32 ui32Offset,
						PRGXFWIF_UFO_ADDR *psAddrOut)
{
	/* check offset is legal */
	if (unlikely((ui32Offset >= psSyncPrimBlock->ui32BlockSize) ||
		(ui32Offset % sizeof(IMG_UINT32))))
	{
		PVR_DPF((PVR_DBG_ERROR, "SyncPrimitiveBlockToFWAddr: parameters check failed"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psAddrOut->ui32Addr = psSyncPrimBlock->uiFWAddr.ui32Addr + ui32Offset;
	return PVRSRV_OK;
}

/*!
*****************************************************************************
 @Function      : SyncAddrListGrow

 @Description   : Grow the SYNC_ADDR_LIST so it can accommodate the given
                  number of syncs, up to a maximum of PVRSRV_MAX_SYNC_PRIMS.

 @Input           psList       : The SYNC_ADDR_LIST to grow
 @Input           ui32NumSyncs : The number of sync addresses to be able to hold
 @Return :        PVRSRV_OK on success
*****************************************************************************/

static PVRSRV_ERROR SyncAddrListGrow(SYNC_ADDR_LIST *psList, IMG_UINT32 ui32NumSyncs)
{
	if (unlikely(ui32NumSyncs > PVRSRV_MAX_SYNC_ADDR_LIST_SIZE))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: ui32NumSyncs=%u > PVRSRV_MAX_SYNC_ADDR_LIST_SIZE=%u", __func__, ui32NumSyncs, PVRSRV_MAX_SYNC_ADDR_LIST_SIZE));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

#if (SYNC_ADDR_LIST_DEBUG == 1)
	PVR_DPF((PVR_DBG_ERROR, "%s:     Entry psList=<%p>, psList->ui32NumSyncs=%d, ui32NumSyncs=%d)", __func__, (void*)psList, psList->ui32NumSyncs, ui32NumSyncs));
#endif
	if (ui32NumSyncs > psList->ui32NumSyncs)
	{
		if (psList->pasFWAddrs == NULL)
		{
			psList->pasFWAddrs = OSAllocMem(sizeof(PRGXFWIF_UFO_ADDR) * PVRSRV_MAX_SYNC_ADDR_LIST_SIZE);
			PVR_RETURN_IF_NOMEM(psList->pasFWAddrs);
		}

		psList->ui32NumSyncs = ui32NumSyncs;
	}

#if (SYNC_ADDR_LIST_DEBUG == 1)
	PVR_DPF((PVR_DBG_ERROR, "%s:     Exit psList=<%p>, psList->ui32NumSyncs=%d, ui32NumSyncs=%d)", __func__, (void*)psList, psList->ui32NumSyncs, ui32NumSyncs));
#endif
	return PVRSRV_OK;
}

/*!
*****************************************************************************
 @Function      : SyncAddrListInit

 @Description   : Initialise a SYNC_ADDR_LIST structure ready for use

 @Input           psList        : The SYNC_ADDR_LIST structure to initialise
 @Return        : None
*****************************************************************************/

void
SyncAddrListInit(SYNC_ADDR_LIST *psList)
{
	psList->ui32NumSyncs = 0;
	psList->pasFWAddrs   = NULL;
}

/*!
*****************************************************************************
 @Function      : SyncAddrListDeinit

 @Description   : Frees any resources associated with the given SYNC_ADDR_LIST

 @Input           psList        : The SYNC_ADDR_LIST structure to deinitialise
 @Return        : None
*****************************************************************************/

void
SyncAddrListDeinit(SYNC_ADDR_LIST *psList)
{
	if (psList->pasFWAddrs != NULL)
	{
		OSFreeMem(psList->pasFWAddrs);
	}
}

/*!
*****************************************************************************
 @Function      : SyncAddrListPopulate

 @Description   : Populate the given SYNC_ADDR_LIST with the FW addresses
                  of the syncs given by the SYNC_PRIMITIVE_BLOCKs and sync offsets

 @Input           ui32NumSyncs    : The number of syncs being passed in
 @Input           apsSyncPrimBlock: Array of pointers to SYNC_PRIMITIVE_BLOCK structures
                                    in which the syncs are based
 @Input           paui32SyncOffset: Array of offsets within each of the sync primitive blocks
                                    where the syncs are located
 @Return :        PVRSRV_OK on success. PVRSRV_ERROR_INVALID_PARAMS if input
                  parameters are invalid.
*****************************************************************************/

PVRSRV_ERROR
SyncAddrListPopulate(SYNC_ADDR_LIST *psList,
						IMG_UINT32 ui32NumSyncs,
						SYNC_PRIMITIVE_BLOCK **apsSyncPrimBlock,
						IMG_UINT32 *paui32SyncOffset)
{
	IMG_UINT32 i;
	PVRSRV_ERROR eError;

#if (SYNC_ADDR_LIST_DEBUG == 1)
	PVR_DPF((PVR_DBG_ERROR, "%s: Entry psList=<%p>, psList->ui32NumSyncs=%d, ui32NumSyncs=%d)", __func__, (void*)psList, psList->ui32NumSyncs, ui32NumSyncs));
#endif
	if (ui32NumSyncs > psList->ui32NumSyncs)
	{
		eError = SyncAddrListGrow(psList, ui32NumSyncs);

		PVR_RETURN_IF_ERROR(eError);
	}

	psList->ui32NumSyncs = ui32NumSyncs;

	for (i = 0; i < ui32NumSyncs; i++)
	{
		eError = SyncPrimitiveBlockToFWAddr(apsSyncPrimBlock[i],
								paui32SyncOffset[i],
								&psList->pasFWAddrs[i]);

		PVR_RETURN_IF_ERROR(eError);
	}

#if (SYNC_ADDR_LIST_DEBUG == 1)
	PVR_DPF((PVR_DBG_ERROR, "%s: Exit psList=<%p>, psList->ui32NumSyncs=%d, ui32NumSyncs=%d)", __func__, (void*)psList, psList->ui32NumSyncs, ui32NumSyncs));
#endif
	return PVRSRV_OK;
}

PVRSRV_ERROR
SyncAddrListAppendSyncPrim(SYNC_ADDR_LIST          *psList,
						   PVRSRV_CLIENT_SYNC_PRIM *psSyncPrim)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 ui32FwAddr = 0;

#if (SYNC_ADDR_LIST_DEBUG == 1)
	PVR_DPF((PVR_DBG_ERROR, "%s: Entry psList=<%p>, psList->ui32NumSyncs=%d)", __func__, (void*)psList, psList->ui32NumSyncs));
#endif
	/* Ensure there's room in psList for the additional sync prim update */
	eError = SyncAddrListGrow(psList, psList->ui32NumSyncs + 1);
	PVR_GOTO_IF_ERROR(eError, e0);

	SyncPrimGetFirmwareAddr(psSyncPrim, &ui32FwAddr);
#if (SYNC_ADDR_LIST_DEBUG == 1)
	PVR_DPF((PVR_DBG_ERROR, "%s: Appending sync prim <%p> UFO addr (0x%x) to psList[->pasFWAddrss[%d]", __func__, (void*)psSyncPrim, ui32FwAddr, psList->ui32NumSyncs-1));
#endif
	psList->pasFWAddrs[psList->ui32NumSyncs-1].ui32Addr = ui32FwAddr;

#if (SYNC_ADDR_LIST_DEBUG == 1)
	{
		IMG_UINT32 iii;

		PVR_DPF((PVR_DBG_ERROR, "%s: psList->ui32NumSyncs=%d", __func__, psList->ui32NumSyncs));
		for (iii=0; iii<psList->ui32NumSyncs; iii++)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: psList->pasFWAddrs[%d].ui32Addr=0x%x", __func__, iii, psList->pasFWAddrs[iii].ui32Addr));
		}
	}
#endif
e0:
#if (SYNC_ADDR_LIST_DEBUG == 1)
	PVR_DPF((PVR_DBG_ERROR, "%s: Exit psList=<%p>, psList->ui32NumSyncs=%d", __func__, (void*)psList, psList->ui32NumSyncs));
#endif
	return eError;
}


static PVRSRV_ERROR
_AppendCheckpoints(SYNC_ADDR_LIST *psList,
				   IMG_UINT32 ui32NumCheckpoints,
				   PSYNC_CHECKPOINT *apsSyncCheckpoint,
				   IMG_BOOL bDeRefCheckpoints)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 ui32SyncCheckpointIndex;
	IMG_UINT32 ui32RollbackSize = psList->ui32NumSyncs;

#if (SYNC_ADDR_LIST_DEBUG == 1)
	PVR_DPF((PVR_DBG_ERROR, "%s: Entry psList=<%p>, psList->ui32NumSyncs=%d, ui32NumCheckpoints=%d)", __func__, (void*)psList, psList->ui32NumSyncs, ui32NumCheckpoints));
#endif
	/* Ensure there's room in psList for the sync checkpoints */
	eError = SyncAddrListGrow(psList, psList->ui32NumSyncs + ui32NumCheckpoints);
	if (unlikely(eError != PVRSRV_OK))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: * * * * ERROR * * * * Trying to SyncAddrListGrow(psList=<%p>, psList->ui32NumSyncs=%d, ui32NumCheckpoints=%d)", __func__, (void*)psList, psList->ui32NumSyncs, ui32NumCheckpoints));
		goto e0;
	}

#if (SYNC_ADDR_LIST_DEBUG == 1)
	PVR_DPF((PVR_DBG_ERROR, "%s: (ui32NumCheckpoints=%d) (psList->ui32NumSyncs is now %d) array already contains %d FWAddrs:", __func__, ui32NumCheckpoints, psList->ui32NumSyncs, ui32RollbackSize));
	if (ui32RollbackSize > 0)
	{
		{
			IMG_UINT32 kk;
			for (kk=0; kk<ui32RollbackSize; kk++)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s:    <%p>psList->pasFWAddrs[%d].ui32Addr = %u(0x%x)", __func__,
						 (void*)&psList->pasFWAddrs[kk], kk,
						 psList->pasFWAddrs[kk].ui32Addr, psList->pasFWAddrs[kk].ui32Addr));
			}
		}
	}
	PVR_DPF((PVR_DBG_ERROR, "%s: apsSyncCheckpoint=<%p>, apsSyncCheckpoint[0] = <%p>", __func__, (void*)apsSyncCheckpoint, (void*)apsSyncCheckpoint[0]));
#endif
	for (ui32SyncCheckpointIndex=0; ui32SyncCheckpointIndex<ui32NumCheckpoints; ui32SyncCheckpointIndex++)
	{
		psList->pasFWAddrs[ui32RollbackSize + ui32SyncCheckpointIndex].ui32Addr = SyncCheckpointGetFirmwareAddr(apsSyncCheckpoint[ui32SyncCheckpointIndex]);
#if (SYNC_ADDR_LIST_DEBUG == 1)
		PVR_DPF((PVR_DBG_ERROR, "%s:  SyncCheckpointCCBEnqueued(<%p>)", __func__, (void*)apsSyncCheckpoint[ui32SyncCheckpointIndex]));
		PVR_DPF((PVR_DBG_ERROR, "%s:                           ID:%d", __func__, SyncCheckpointGetId((PSYNC_CHECKPOINT)apsSyncCheckpoint[ui32SyncCheckpointIndex])));
#endif
		SyncCheckpointCCBEnqueued((PSYNC_CHECKPOINT)apsSyncCheckpoint[ui32SyncCheckpointIndex]);
		if (bDeRefCheckpoints)
		{
			/* Drop the reference that was taken internally by the OS implementation of resolve_fence() */
			SyncCheckpointDropRef((PSYNC_CHECKPOINT)apsSyncCheckpoint[ui32SyncCheckpointIndex]);
		}
	}
#if (SYNC_ADDR_LIST_DEBUG == 1)
	if (psList->ui32NumSyncs > 0)
	{
		IMG_UINT32 kk;
		for (kk=0; kk<psList->ui32NumSyncs; kk++)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s:    <%p>psList->pasFWAddrs[%d].ui32Addr = %u(0x%x)", __func__,
			         (void*)&psList->pasFWAddrs[kk], kk,
			         psList->pasFWAddrs[kk].ui32Addr, psList->pasFWAddrs[kk].ui32Addr));
		}
	}
#endif
	return eError;

e0:
	for (ui32SyncCheckpointIndex=0; ui32SyncCheckpointIndex<ui32NumCheckpoints; ui32SyncCheckpointIndex++)
	{
		if (bDeRefCheckpoints)
		{
			/* Drop the reference that was taken internally by the OS implementation of resolve_fence() */
			SyncCheckpointDropRef((PSYNC_CHECKPOINT)apsSyncCheckpoint[ui32SyncCheckpointIndex]);
		}
	}
#if (SYNC_ADDR_LIST_DEBUG == 1)
	PVR_DPF((PVR_DBG_ERROR, "%s: Exit psList=<%p>, psList->ui32NumSyncs=%d, ui32NumCheckpoints=%d)", __func__, (void*)psList, psList->ui32NumSyncs, ui32NumCheckpoints));
#endif
	return eError;
}

/*!
*****************************************************************************
 @Function      : SyncAddrListAppendCheckpoints

 @Description   : Append the FW addresses of the sync checkpoints given in
                  the PSYNC_CHECKPOINTs array to the given SYNC_ADDR_LIST

 @Input           ui32NumSyncCheckpoints : The number of sync checkpoints
                                           being passed in
 @Input           apsSyncCheckpoint : Array of PSYNC_CHECKPOINTs whose details
                                      are to be appended to the SYNC_ADDR_LIST
 @Return :        PVRSRV_OK on success. PVRSRV_ERROR_INVALID_PARAMS if input
                  parameters are invalid.
*****************************************************************************/
PVRSRV_ERROR
SyncAddrListAppendCheckpoints(SYNC_ADDR_LIST *psList,
								IMG_UINT32 ui32NumCheckpoints,
								PSYNC_CHECKPOINT *apsSyncCheckpoint)
{
	return _AppendCheckpoints(psList, ui32NumCheckpoints, apsSyncCheckpoint, IMG_FALSE);
}

/*!
*****************************************************************************
 @Function      : SyncAddrListAppendAndDeRefCheckpoints

 @Description   : Append the FW addresses of the sync checkpoints given in
                  the PSYNC_CHECKPOINTs array to the given SYNC_ADDR_LIST.
                  A reference is dropped for each of the checkpoints.

 @Input           ui32NumSyncCheckpoints : The number of sync checkpoints
                                           being passed in
 @Input           apsSyncCheckpoint : Array of PSYNC_CHECKPOINTs whose details
                                      are to be appended to the SYNC_ADDR_LIST
 @Return :        PVRSRV_OK on success. PVRSRV_ERROR_INVALID_PARAMS if input
                  parameters are invalid.
*****************************************************************************/
PVRSRV_ERROR
SyncAddrListAppendAndDeRefCheckpoints(SYNC_ADDR_LIST *psList,
									  IMG_UINT32 ui32NumCheckpoints,
									  PSYNC_CHECKPOINT *apsSyncCheckpoint)
{
	return _AppendCheckpoints(psList, ui32NumCheckpoints, apsSyncCheckpoint, IMG_TRUE);
}

void
SyncAddrListDeRefCheckpoints(IMG_UINT32 ui32NumCheckpoints,
							 PSYNC_CHECKPOINT *apsSyncCheckpoint)
{
	IMG_UINT32 ui32SyncCheckpointIndex;

	for (ui32SyncCheckpointIndex=0; ui32SyncCheckpointIndex<ui32NumCheckpoints; ui32SyncCheckpointIndex++)
	{
		/* Drop the reference that was taken internally by the OS implementation of resolve_fence() */
		SyncCheckpointDropRef((PSYNC_CHECKPOINT)apsSyncCheckpoint[ui32SyncCheckpointIndex]);
	}
}

/*!
*****************************************************************************
 @Function      : SyncAddrListRollbackCheckpoints

 @Description   : Rollback the enqueued count of each sync checkpoint in
                  the given SYNC_ADDR_LIST. This needs to be done in the
                  event of the kick call failing, so that the reference
                  taken on each sync checkpoint on the firmware's behalf
                  is dropped.

 @Input           psList        : The SYNC_ADDR_LIST structure containing
                                  sync checkpoints to be rolled back

 @Return :        PVRSRV_OK on success. PVRSRV_ERROR_INVALID_PARAMS if input
                  parameters are invalid.
*****************************************************************************/

PVRSRV_ERROR
SyncAddrListRollbackCheckpoints(PVRSRV_DEVICE_NODE *psDevNode, SYNC_ADDR_LIST *psList)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 ui32SyncIndex;

#if (SYNC_ADDR_LIST_DEBUG == 1)
	PVR_DPF((PVR_DBG_ERROR, "%s: called (psList=<%p>)", __func__, (void*)psList));
#endif
	if (psList)
	{
#if (SYNC_ADDR_LIST_DEBUG == 1)
		PVR_DPF((PVR_DBG_ERROR, "%s: psList->ui32NumSyncs=%d", __func__, psList->ui32NumSyncs));
#endif
		for (ui32SyncIndex=0; ui32SyncIndex<psList->ui32NumSyncs; ui32SyncIndex++)
		{
			if (psList->pasFWAddrs[ui32SyncIndex].ui32Addr & 0x1)
			{
				SyncCheckpointRollbackFromUFO(psDevNode, psList->pasFWAddrs[ui32SyncIndex].ui32Addr);
			}
		}
	}
	return eError;
}

PVRSRV_ERROR
PVRSRVSyncRecordAddKM(CONNECTION_DATA *psConnection,
					  PVRSRV_DEVICE_NODE *psDevNode,
					  SYNC_RECORD_HANDLE *phRecord,
					  SYNC_PRIMITIVE_BLOCK *hServerSyncPrimBlock,
					  IMG_UINT32 ui32FwBlockAddr,
					  IMG_UINT32 ui32SyncOffset,
					  IMG_BOOL bServerSync,
					  IMG_UINT32 ui32ClassNameSize,
					  const IMG_CHAR *pszClassName)
{
	struct SYNC_RECORD * psSyncRec;
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	RGXSRV_HWPERF_ALLOC(psDevNode, SYNC,
	                    ui32FwBlockAddr + ui32SyncOffset,
	                    pszClassName,
	                    ui32ClassNameSize);

	PVR_RETURN_IF_INVALID_PARAM(phRecord);

	*phRecord = NULL;

	psSyncRec = OSAllocMem(sizeof(*psSyncRec));
	PVR_GOTO_IF_NOMEM(psSyncRec, eError, fail_alloc);

	psSyncRec->psDevNode = psDevNode;
	psSyncRec->psServerSyncPrimBlock = hServerSyncPrimBlock;
	psSyncRec->ui32SyncOffset = ui32SyncOffset;
	psSyncRec->ui32FwBlockAddr = ui32FwBlockAddr;
	psSyncRec->ui64OSTime = OSClockns64();
	psSyncRec->uiPID = OSGetCurrentProcessID();
	psSyncRec->eRecordType = bServerSync? SYNC_RECORD_TYPE_SERVER: SYNC_RECORD_TYPE_CLIENT;

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

	OSLockAcquire(psDevNode->hSyncServerRecordLock);
	if (psDevNode->ui32SyncServerRecordCount < SYNC_RECORD_LIMIT)
	{
		dllist_add_to_head(&psDevNode->sSyncServerRecordList, &psSyncRec->sNode);
		psDevNode->ui32SyncServerRecordCount++;

		if (psDevNode->ui32SyncServerRecordCount > psDevNode->ui32SyncServerRecordCountHighWatermark)
		{
			psDevNode->ui32SyncServerRecordCountHighWatermark = psDevNode->ui32SyncServerRecordCount;
		}
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to add sync record \"%s\". %u records already exist.",
											__func__,
											pszClassName,
											psDevNode->ui32SyncServerRecordCount));
		OSFreeMem(psSyncRec);
		psSyncRec = NULL;
		eError = PVRSRV_ERROR_TOOMANYBUFFERS;
	}
	OSLockRelease(psDevNode->hSyncServerRecordLock);

	*phRecord = (SYNC_RECORD_HANDLE)psSyncRec;

fail_alloc:
	return eError;
}

PVRSRV_ERROR
PVRSRVSyncRecordRemoveByHandleKM(
			SYNC_RECORD_HANDLE hRecord)
{
	struct SYNC_RECORD **ppFreedSync;
	struct SYNC_RECORD *pSync = (struct SYNC_RECORD*)hRecord;
	PVRSRV_DEVICE_NODE *psDevNode;

	PVR_RETURN_IF_INVALID_PARAM(hRecord);

	psDevNode = pSync->psDevNode;

	OSLockAcquire(psDevNode->hSyncServerRecordLock);

	RGXSRV_HWPERF_FREE(psDevNode, SYNC, pSync->ui32FwBlockAddr + pSync->ui32SyncOffset);

	dllist_remove_node(&pSync->sNode);

	if (psDevNode->uiSyncServerRecordFreeIdx >= PVRSRV_FULL_SYNC_TRACKING_HISTORY_LEN)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: freed sync record index out of range",
				 __func__));
		psDevNode->uiSyncServerRecordFreeIdx = 0;
	}
	ppFreedSync = &psDevNode->apsSyncServerRecordsFreed[psDevNode->uiSyncServerRecordFreeIdx];
	psDevNode->uiSyncServerRecordFreeIdx =
		(psDevNode->uiSyncServerRecordFreeIdx + 1) % PVRSRV_FULL_SYNC_TRACKING_HISTORY_LEN;

	if (*ppFreedSync)
	{
		OSFreeMem(*ppFreedSync);
	}
	pSync->psServerSyncPrimBlock = NULL;
	pSync->ui64OSTime = OSClockns64();
	*ppFreedSync = pSync;

	psDevNode->ui32SyncServerRecordCount--;

	OSLockRelease(psDevNode->hSyncServerRecordLock);

	return PVRSRV_OK;
}

PVRSRV_ERROR
PVRSRVSyncAllocEventKM(CONNECTION_DATA *psConnection,
			PVRSRV_DEVICE_NODE *psDevNode,
			IMG_BOOL bServerSync,
			IMG_UINT32 ui32FWAddr,
			IMG_UINT32 ui32ClassNameSize,
			const IMG_CHAR *pszClassName)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	RGXSRV_HWPERF_ALLOC(psDevNode, SYNC, ui32FWAddr, pszClassName, ui32ClassNameSize);

	return PVRSRV_OK;
}

PVRSRV_ERROR
PVRSRVSyncFreeEventKM(CONNECTION_DATA *psConnection,
			PVRSRV_DEVICE_NODE *psDevNode,
			IMG_UINT32 ui32FWAddr)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	RGXSRV_HWPERF_FREE(psDevNode, SYNC, ui32FWAddr);

	return PVRSRV_OK;
}

static
void _SyncConnectionRef(SYNC_CONNECTION_DATA *psSyncConnectionData)
{
	IMG_INT iRefCount = OSAtomicIncrement(&psSyncConnectionData->sRefCount);

	SYNC_REFCOUNT_PRINT("%s: Sync connection %p, refcount = %d",
						__func__, psSyncConnectionData, iRefCount);
	PVR_UNREFERENCED_PARAMETER(iRefCount);
}

static
void _SyncConnectionUnref(SYNC_CONNECTION_DATA *psSyncConnectionData)
{
	IMG_INT iRefCount = OSAtomicDecrement(&psSyncConnectionData->sRefCount);
	if (iRefCount == 0)
	{
		SYNC_REFCOUNT_PRINT("%s: Sync connection %p, refcount = %d",
		                    __func__, psSyncConnectionData, iRefCount);

		PVR_ASSERT(dllist_is_empty(&psSyncConnectionData->sListHead));
		OSLockDestroy(psSyncConnectionData->hLock);
		OSFreeMem(psSyncConnectionData);
	}
	else
	{
		SYNC_REFCOUNT_PRINT("%s: Sync connection %p, refcount = %d",
		                    __func__, psSyncConnectionData, iRefCount);
		PVR_ASSERT(iRefCount > 0);
	}
}

static
void _SyncConnectionAddBlock(CONNECTION_DATA *psConnection, SYNC_PRIMITIVE_BLOCK *psBlock)
{
	if (psConnection)
	{
		SYNC_CONNECTION_DATA *psSyncConnectionData = psConnection->psSyncConnectionData;

		/*
			Make sure the connection doesn't go away. It doesn't matter that we will release
			the lock between as the refcount and list don't have to be atomic w.r.t. to each other
		*/
		_SyncConnectionRef(psSyncConnectionData);

		OSLockAcquire(psSyncConnectionData->hLock);
		dllist_add_to_head(&psSyncConnectionData->sListHead, &psBlock->sConnectionNode);
		OSLockRelease(psSyncConnectionData->hLock);
		psBlock->psSyncConnectionData = psSyncConnectionData;
	}
	else
	{
		psBlock->psSyncConnectionData = NULL;
	}
}

static
void _SyncConnectionRemoveBlock(SYNC_PRIMITIVE_BLOCK *psBlock)
{
	SYNC_CONNECTION_DATA *psSyncConnectionData = psBlock->psSyncConnectionData;

	if (psBlock->psSyncConnectionData)
	{
		OSLockAcquire(psSyncConnectionData->hLock);
		dllist_remove_node(&psBlock->sConnectionNode);
		OSLockRelease(psSyncConnectionData->hLock);

		_SyncConnectionUnref(psBlock->psSyncConnectionData);
	}
}

static inline
void _DoPrimBlockFree(SYNC_PRIMITIVE_BLOCK *psSyncBlk)
{
	PVRSRV_DEVICE_NODE *psDevNode = psSyncBlk->psDevNode;

	SYNC_REFCOUNT_PRINT("%s: Sync block %p, refcount = %d (remove)",
	                    __func__, psSyncBlk, OSAtomicRead(&psSyncBlk->sRefCount));

	PVR_ASSERT(OSAtomicRead(&psSyncBlk->sRefCount) == 1);

	_SyncConnectionRemoveBlock(psSyncBlk);
	DevmemReleaseCpuVirtAddr(psSyncBlk->psMemDesc);
	psDevNode->pfnFreeUFOBlock(psDevNode, psSyncBlk->psMemDesc);
	OSFreeMem(psSyncBlk);
}

PVRSRV_ERROR
PVRSRVAllocSyncPrimitiveBlockKM(CONNECTION_DATA *psConnection,
                                PVRSRV_DEVICE_NODE * psDevNode,
								SYNC_PRIMITIVE_BLOCK **ppsSyncBlk,
								IMG_UINT32 *puiSyncPrimVAddr,
								IMG_UINT32 *puiSyncPrimBlockSize,
								PMR        **ppsSyncPMR)
{
	SYNC_PRIMITIVE_BLOCK *psNewSyncBlk;
	PVRSRV_ERROR eError;

	psNewSyncBlk = OSAllocMem(sizeof(SYNC_PRIMITIVE_BLOCK));
	PVR_GOTO_IF_NOMEM(psNewSyncBlk, eError, e0);

	psNewSyncBlk->psDevNode = psDevNode;

	PDUMPCOMMENTWITHFLAGS(psDevNode, PDUMP_FLAGS_CONTINUOUS, "Allocate UFO block");

	eError = psDevNode->pfnAllocUFOBlock(psDevNode,
										 &psNewSyncBlk->psMemDesc,
										 &psNewSyncBlk->uiFWAddr.ui32Addr,
										 &psNewSyncBlk->ui32BlockSize);
	PVR_GOTO_IF_ERROR(eError, e1);

	*puiSyncPrimVAddr = psNewSyncBlk->uiFWAddr.ui32Addr;

	eError = DevmemAcquireCpuVirtAddr(psNewSyncBlk->psMemDesc,
									  (void **) &psNewSyncBlk->pui32LinAddr);
	PVR_GOTO_IF_ERROR(eError, e2);

	eError = DevmemLocalGetImportHandle(psNewSyncBlk->psMemDesc, (void **) ppsSyncPMR);

	PVR_GOTO_IF_ERROR(eError, e3);

	OSAtomicWrite(&psNewSyncBlk->sRefCount, 1);

	/* If there is a connection pointer then add the new block onto it's list */
	_SyncConnectionAddBlock(psConnection, psNewSyncBlk);

	*ppsSyncBlk = psNewSyncBlk;
	*puiSyncPrimBlockSize = psNewSyncBlk->ui32BlockSize;

	PDUMPCOMMENTWITHFLAGS(psDevNode, PDUMP_FLAGS_CONTINUOUS,
						  "Allocated UFO block (FirmwareVAddr = 0x%08x)",
						  *puiSyncPrimVAddr);

	return PVRSRV_OK;

e3:
	DevmemReleaseCpuVirtAddr(psNewSyncBlk->psMemDesc);
e2:
	psDevNode->pfnFreeUFOBlock(psDevNode, psNewSyncBlk->psMemDesc);
e1:
	OSFreeMem(psNewSyncBlk);
e0:
	return eError;
}

PVRSRV_ERROR
PVRSRVFreeSyncPrimitiveBlockKM(SYNC_PRIMITIVE_BLOCK *psSyncBlk)
{

	/* This function is an alternative to the above without reference counting.
	 * With the removal of sync prim ops for server syncs we no longer have to
	 * reference count prim blocks as the reference will never be incremented /
	 * decremented by a prim op */
	_DoPrimBlockFree(psSyncBlk);
	return PVRSRV_OK;
}

static INLINE IMG_BOOL _CheckSyncIndex(SYNC_PRIMITIVE_BLOCK *psSyncBlk,
							IMG_UINT32 ui32Index)
{
	return ((ui32Index * sizeof(IMG_UINT32)) < psSyncBlk->ui32BlockSize);
}

PVRSRV_ERROR
PVRSRVSyncPrimSetKM(SYNC_PRIMITIVE_BLOCK *psSyncBlk, IMG_UINT32 ui32Index,
					IMG_UINT32 ui32Value)
{
	if (_CheckSyncIndex(psSyncBlk, ui32Index))
	{
		psSyncBlk->pui32LinAddr[ui32Index] = ui32Value;
		return PVRSRV_OK;
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVSyncPrimSetKM: Index %u out of range for "
							"0x%08X byte sync block (value 0x%08X)",
							ui32Index,
							psSyncBlk->ui32BlockSize,
							ui32Value));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
}

#if defined(PDUMP)
PVRSRV_ERROR
PVRSRVSyncPrimPDumpValueKM(SYNC_PRIMITIVE_BLOCK *psSyncBlk, IMG_UINT32 ui32Offset, IMG_UINT32 ui32Value)
{
	/*
		We might be ask to PDump sync state outside of capture range
		(e.g. texture uploads) so make this continuous.
	*/
	DevmemPDumpLoadMemValue32(psSyncBlk->psMemDesc,
					   ui32Offset,
					   ui32Value,
					   PDUMP_FLAGS_CONTINUOUS);

	return PVRSRV_OK;
}

PVRSRV_ERROR
PVRSRVSyncPrimPDumpKM(SYNC_PRIMITIVE_BLOCK *psSyncBlk, IMG_UINT32 ui32Offset)
{
	/*
		We might be ask to PDump sync state outside of capture range
		(e.g. texture uploads) so make this continuous.
	*/
	DevmemPDumpLoadMem(psSyncBlk->psMemDesc,
					   ui32Offset,
					   sizeof(IMG_UINT32),
					   PDUMP_FLAGS_CONTINUOUS);

	return PVRSRV_OK;
}

PVRSRV_ERROR
PVRSRVSyncPrimPDumpPolKM(SYNC_PRIMITIVE_BLOCK *psSyncBlk, IMG_UINT32 ui32Offset,
						 IMG_UINT32 ui32Value, IMG_UINT32 ui32Mask,
						 PDUMP_POLL_OPERATOR eOperator,
						 PDUMP_FLAGS_T ui32PDumpFlags)
{
	DevmemPDumpDevmemPol32(psSyncBlk->psMemDesc,
						   ui32Offset,
						   ui32Value,
						   ui32Mask,
						   eOperator,
						   ui32PDumpFlags);

	return PVRSRV_OK;
}

PVRSRV_ERROR
PVRSRVSyncPrimPDumpCBPKM(SYNC_PRIMITIVE_BLOCK *psSyncBlk, IMG_UINT64 ui32Offset,
						 IMG_UINT64 uiWriteOffset, IMG_UINT64 uiPacketSize,
						 IMG_UINT64 uiBufferSize)
{
	DevmemPDumpCBP(psSyncBlk->psMemDesc,
				   ui32Offset,
				   uiWriteOffset,
				   uiPacketSize,
				   uiBufferSize);
	return PVRSRV_OK;
}
#endif

/* SyncRegisterConnection */
PVRSRV_ERROR SyncRegisterConnection(SYNC_CONNECTION_DATA **ppsSyncConnectionData)
{
	SYNC_CONNECTION_DATA *psSyncConnectionData;
	PVRSRV_ERROR eError;

	psSyncConnectionData = OSAllocMem(sizeof(SYNC_CONNECTION_DATA));
	if (psSyncConnectionData == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc;
	}

	eError = OSLockCreate(&psSyncConnectionData->hLock);
	PVR_GOTO_IF_ERROR(eError, fail_lockcreate);
	dllist_init(&psSyncConnectionData->sListHead);
	OSAtomicWrite(&psSyncConnectionData->sRefCount, 1);

	*ppsSyncConnectionData = psSyncConnectionData;
	return PVRSRV_OK;

fail_lockcreate:
	OSFreeMem(psSyncConnectionData);
fail_alloc:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

/* SyncUnregisterConnection */
void SyncUnregisterConnection(SYNC_CONNECTION_DATA *psSyncConnectionData)
{
	_SyncConnectionUnref(psSyncConnectionData);
}

void SyncConnectionPDumpSyncBlocks(PVRSRV_DEVICE_NODE *psDevNode, void *hSyncPrivData, PDUMP_TRANSITION_EVENT eEvent)
{
	if ((eEvent == PDUMP_TRANSITION_EVENT_RANGE_ENTERED) || (eEvent == PDUMP_TRANSITION_EVENT_BLOCK_STARTED))
	{
		SYNC_CONNECTION_DATA *psSyncConnectionData = hSyncPrivData;
		DLLIST_NODE *psNode, *psNext;

		OSLockAcquire(psSyncConnectionData->hLock);

		PDUMPCOMMENT(psDevNode, "Dump client Sync Prim state");
		dllist_foreach_node(&psSyncConnectionData->sListHead, psNode, psNext)
		{
			SYNC_PRIMITIVE_BLOCK *psSyncBlock =
				IMG_CONTAINER_OF(psNode, SYNC_PRIMITIVE_BLOCK, sConnectionNode);

			DevmemPDumpLoadMem(psSyncBlock->psMemDesc,
					0,
					psSyncBlock->ui32BlockSize,
					PDUMP_FLAGS_CONTINUOUS);
		}

		OSLockRelease(psSyncConnectionData->hLock);
	}
}

void SyncRecordLookup(PVRSRV_DEVICE_NODE *psDevNode, IMG_UINT32 ui32FwAddr,
					  IMG_CHAR * pszSyncInfo, size_t len)
{
	DLLIST_NODE *psNode, *psNext;
	IMG_INT iEnd;
	IMG_BOOL bFound = IMG_FALSE;

	if (!pszSyncInfo)
	{
		return;
	}

	OSLockAcquire(psDevNode->hSyncServerRecordLock);
	pszSyncInfo[0] = '\0';

	dllist_foreach_node(&psDevNode->sSyncServerRecordList, psNode, psNext)
	{
		struct SYNC_RECORD *psSyncRec =
			IMG_CONTAINER_OF(psNode, struct SYNC_RECORD, sNode);
		if ((psSyncRec->ui32FwBlockAddr+psSyncRec->ui32SyncOffset) == ui32FwAddr
			&& SYNC_RECORD_TYPE_UNKNOWN != psSyncRec->eRecordType
			&& psSyncRec->psServerSyncPrimBlock
			&& psSyncRec->psServerSyncPrimBlock->pui32LinAddr
			)
		{
			IMG_UINT32 *pui32SyncAddr;
			pui32SyncAddr = psSyncRec->psServerSyncPrimBlock->pui32LinAddr
				+ (psSyncRec->ui32SyncOffset/sizeof(IMG_UINT32));
			iEnd = OSSNPrintf(pszSyncInfo, len, "Cur=0x%08x %s:%05u (%s)",
				*pui32SyncAddr,
				((SYNC_RECORD_TYPE_SERVER==psSyncRec->eRecordType)?"Server":"Client"),
				psSyncRec->uiPID,
				psSyncRec->szClassName
				);
			if (iEnd >= 0 && iEnd < len)
			{
				pszSyncInfo[iEnd] = '\0';
			}
			bFound = IMG_TRUE;
			break;
		}
	}

	OSLockRelease(psDevNode->hSyncServerRecordLock);

	if (!bFound && (psDevNode->ui32SyncServerRecordCountHighWatermark == SYNC_RECORD_LIMIT))
	{
		OSSNPrintf(pszSyncInfo, len, "(Record may be lost)");
	}
}

#define NS_IN_S (1000000000UL)
static void _SyncRecordPrint(struct SYNC_RECORD *psSyncRec,
					IMG_UINT64 ui64TimeNow,
					DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile)
{
	SYNC_PRIMITIVE_BLOCK *psSyncBlock = psSyncRec->psServerSyncPrimBlock;

	if (SYNC_RECORD_TYPE_UNKNOWN != psSyncRec->eRecordType)
	{
		IMG_UINT64 ui64DeltaS;
		IMG_UINT32 ui32DeltaF;
		IMG_UINT64 ui64Delta = ui64TimeNow - psSyncRec->ui64OSTime;
		ui64DeltaS = OSDivide64(ui64Delta, NS_IN_S, &ui32DeltaF);

		if (psSyncBlock && psSyncBlock->pui32LinAddr)
		{
			IMG_UINT32 *pui32SyncAddr;
			pui32SyncAddr = psSyncBlock->pui32LinAddr
				+ (psSyncRec->ui32SyncOffset/sizeof(IMG_UINT32));

			PVR_DUMPDEBUG_LOG("\t%s %05u %05" IMG_UINT64_FMTSPEC ".%09u FWAddr=0x%08x Val=0x%08x (%s)",
				((SYNC_RECORD_TYPE_SERVER==psSyncRec->eRecordType)?"Server":"Client"),
				psSyncRec->uiPID,
				ui64DeltaS, ui32DeltaF,
				(psSyncRec->ui32FwBlockAddr+psSyncRec->ui32SyncOffset),
				*pui32SyncAddr,
				psSyncRec->szClassName
				);
		}
		else
		{
			PVR_DUMPDEBUG_LOG("\t%s %05u %05" IMG_UINT64_FMTSPEC ".%09u FWAddr=0x%08x Val=<null_ptr> (%s)",
				((SYNC_RECORD_TYPE_SERVER==psSyncRec->eRecordType)?"Server":"Client"),
				psSyncRec->uiPID,
				ui64DeltaS, ui32DeltaF,
				(psSyncRec->ui32FwBlockAddr+psSyncRec->ui32SyncOffset),
				psSyncRec->szClassName
				);
		}
	}
}

static void _SyncRecordRequest(PVRSRV_DBGREQ_HANDLE hDebugRequestHandle,
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
		OSLockAcquire(psDevNode->hSyncServerRecordLock);

		PVR_DUMPDEBUG_LOG("Dumping all allocated syncs. Allocated: %u High watermark: %u @ %05" IMG_UINT64_FMTSPEC ".%09u",
										psDevNode->ui32SyncServerRecordCount,
										psDevNode->ui32SyncServerRecordCountHighWatermark,
										ui64TimeNowS,
										ui32TimeNowF);
		if (psDevNode->ui32SyncServerRecordCountHighWatermark == SYNC_RECORD_LIMIT)
		{
			PVR_DUMPDEBUG_LOG("Warning: Record limit (%u) was reached. Some sync checkpoints may not have been recorded in the debug information.",
                                                                                                                SYNC_RECORD_LIMIT);
		}

		PVR_DUMPDEBUG_LOG("\t%-6s %-5s %-15s %-17s %-14s (%s)",
					"Type", "PID", "Time Delta (s)", "Address", "Value", "Annotation");

		dllist_foreach_node(&psDevNode->sSyncServerRecordList, psNode, psNext)
		{
			struct SYNC_RECORD *psSyncRec =
				IMG_CONTAINER_OF(psNode, struct SYNC_RECORD, sNode);
			_SyncRecordPrint(psSyncRec, ui64TimeNow, pfnDumpDebugPrintf, pvDumpDebugFile);
			}

		PVR_DUMPDEBUG_LOG("Dumping all recently freed syncs @ %05" IMG_UINT64_FMTSPEC ".%09u",
						  ui64TimeNowS, ui32TimeNowF);
		PVR_DUMPDEBUG_LOG("\t%-6s %-5s %-15s %-17s %-14s (%s)",
					"Type", "PID", "Time Delta (s)", "Address", "Value", "Annotation");
		for (i = DECREMENT_WITH_WRAP(psDevNode->uiSyncServerRecordFreeIdx, PVRSRV_FULL_SYNC_TRACKING_HISTORY_LEN);
			 i != psDevNode->uiSyncServerRecordFreeIdx;
			 i = DECREMENT_WITH_WRAP(i, PVRSRV_FULL_SYNC_TRACKING_HISTORY_LEN))
		{
			if (psDevNode->apsSyncServerRecordsFreed[i])
			{
				_SyncRecordPrint(psDevNode->apsSyncServerRecordsFreed[i],
								 ui64TimeNow, pfnDumpDebugPrintf, pvDumpDebugFile);
			}
			else
			{
				break;
			}
		}

		OSLockRelease(psDevNode->hSyncServerRecordLock);
	}
}
#undef NS_IN_S

static PVRSRV_ERROR SyncRecordListInit(PVRSRV_DEVICE_NODE *psDevNode)
{
	PVRSRV_ERROR eError;

	psDevNode->ui32SyncServerRecordCount = 0;
	psDevNode->ui32SyncServerRecordCountHighWatermark = 0;

	eError = OSLockCreate(&psDevNode->hSyncServerRecordLock);
	PVR_GOTO_IF_ERROR(eError, fail_lock_create);
	dllist_init(&psDevNode->sSyncServerRecordList);

	eError = PVRSRVRegisterDeviceDbgRequestNotify(&psDevNode->hSyncServerRecordNotify,
		                                          psDevNode,
		                                          _SyncRecordRequest,
		                                          DEBUG_REQUEST_SYNCTRACKING,
		                                          psDevNode);

	PVR_GOTO_IF_ERROR(eError, fail_dbg_register);

	return PVRSRV_OK;

fail_dbg_register:
	OSLockDestroy(psDevNode->hSyncServerRecordLock);
fail_lock_create:
	return eError;
}

static void SyncRecordListDeinit(PVRSRV_DEVICE_NODE *psDevNode)
{
	DLLIST_NODE *psNode, *psNext;
	int i;

	OSLockAcquire(psDevNode->hSyncServerRecordLock);
	dllist_foreach_node(&psDevNode->sSyncServerRecordList, psNode, psNext)
	{
		struct SYNC_RECORD *pSyncRec =
			IMG_CONTAINER_OF(psNode, struct SYNC_RECORD, sNode);

		dllist_remove_node(psNode);
		OSFreeMem(pSyncRec);
	}

	for (i = 0; i < PVRSRV_FULL_SYNC_TRACKING_HISTORY_LEN; i++)
	{
		if (psDevNode->apsSyncServerRecordsFreed[i])
		{
			OSFreeMem(psDevNode->apsSyncServerRecordsFreed[i]);
			psDevNode->apsSyncServerRecordsFreed[i] = NULL;
		}
	}
	OSLockRelease(psDevNode->hSyncServerRecordLock);

	if (psDevNode->hSyncServerRecordNotify)
	{
		PVRSRVUnregisterDeviceDbgRequestNotify(psDevNode->hSyncServerRecordNotify);
	}
	OSLockDestroy(psDevNode->hSyncServerRecordLock);
}

PVRSRV_ERROR SyncServerInit(PVRSRV_DEVICE_NODE *psDevNode)
{
	PVRSRV_ERROR eError;

	if (GetInfoPageDebugFlagsKM() & DEBUG_FEATURE_FULL_SYNC_TRACKING_ENABLED)
	{
		eError = SyncRecordListInit(psDevNode);
		PVR_GOTO_IF_ERROR(eError, fail_record_list);
	}

	return PVRSRV_OK;

fail_record_list:
	return eError;
}

void SyncServerDeinit(PVRSRV_DEVICE_NODE *psDevNode)
{

	if (GetInfoPageDebugFlagsKM() & DEBUG_FEATURE_FULL_SYNC_TRACKING_ENABLED)
	{
		SyncRecordListDeinit(psDevNode);
	}
}
