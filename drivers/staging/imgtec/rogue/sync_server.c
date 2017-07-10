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
#include "sync_server.h"
#include "sync_server_internal.h"
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
#include "pvrsrv.h"
#include "connection_server.h"
#include "htbuffer.h"
#include "rgxhwperf.h"

#if defined(SUPPORT_SECURE_EXPORT)
#include "ossecure_export.h"
#endif

#if defined(SUPPORT_EXTRA_METASP_DEBUG)
#include "rgxdebug.h"
#endif

struct _SYNC_PRIMITIVE_BLOCK_
{
	PVRSRV_DEVICE_NODE	*psDevNode;
	DEVMEM_MEMDESC		*psMemDesc;
	IMG_UINT32			*pui32LinAddr;
	IMG_UINT32			ui32BlockSize;		/*!< Size of the Sync Primitive Block */
	IMG_UINT32			ui32RefCount;
	POS_LOCK			hLock;
	DLLIST_NODE			sConnectionNode;
	SYNC_CONNECTION_DATA *psSyncConnectionData;	/*!< Link back to the sync connection data if there is one */
	PRGXFWIF_UFO_ADDR		uiFWAddr;	/*!< The firmware address of the sync prim block */
};

struct _SERVER_SYNC_PRIMITIVE_
{
	PVRSRV_DEVICE_NODE		*psDevNode;
	PVRSRV_CLIENT_SYNC_PRIM *psSync;
	IMG_UINT32				ui32NextOp;
	IMG_UINT32				ui32RefCount;
	IMG_UINT32				ui32UID;
	IMG_UINT32				ui32LastSyncRequesterID;
	DLLIST_NODE				sNode;
	/* PDump only data */
	IMG_BOOL				bSWOperation;
	IMG_BOOL				bSWOpStartedInCaptRange;
	IMG_UINT32				ui32LastHWUpdate;
	IMG_BOOL				bPDumped;
	POS_LOCK				hLock;
	IMG_CHAR				szClassName[SYNC_MAX_CLASS_NAME_LEN];
};

struct _SERVER_SYNC_EXPORT_
{
	SERVER_SYNC_PRIMITIVE *psSync;
};

struct _SERVER_OP_COOKIE_
{
	IMG_BOOL				bActive;
	/*
		Client syncblock(s) info.
		If this changes update the calculation of ui32BlockAllocSize
	*/
	IMG_UINT32				ui32SyncBlockCount;
	SYNC_PRIMITIVE_BLOCK	**papsSyncPrimBlock;

	/*
		Client sync(s) info.
		If this changes update the calculation of ui32ClientAllocSize
	*/
	IMG_UINT32				ui32ClientSyncCount;
	IMG_UINT32				*paui32SyncBlockIndex;
	IMG_UINT32				*paui32Index;
	IMG_UINT32				*paui32Flags;
	IMG_UINT32				*paui32FenceValue;
	IMG_UINT32				*paui32UpdateValue;

	/*
		Server sync(s) info
		If this changes update the calculation of ui32ServerAllocSize
	*/
	IMG_UINT32				ui32ServerSyncCount;
	SERVER_SYNC_PRIMITIVE	**papsServerSync;
	IMG_UINT32				*paui32ServerFenceValue;
	IMG_UINT32				*paui32ServerUpdateValue;

};

struct _SYNC_CONNECTION_DATA_
{
	DLLIST_NODE	sListHead;
	IMG_UINT32	ui32RefCount;
	POS_LOCK	hLock;
};

#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
#define DECREMENT_WITH_WRAP(value, sz) ((value) ? ((value) - 1) : ((sz) - 1))

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
	IMG_UINT32				ui32SyncOffset; 		/*!< offset to sync in block */
	IMG_UINT32				ui32FwBlockAddr;
	IMG_PID					uiPID;
	IMG_UINT64				ui64OSTime;
	enum SYNC_RECORD_TYPE	eRecordType;
	DLLIST_NODE				sNode;
	IMG_CHAR				szClassName[SYNC_MAX_CLASS_NAME_LEN];
};
#endif /* #if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING) */

static IMG_UINT32 g_ServerSyncUID = 0;

#define SYNC_REQUESTOR_UNKNOWN 0
static IMG_UINT32 g_ui32NextSyncRequestorID = 1;

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
	if((ui32Offset >= psSyncPrimBlock->ui32BlockSize) ||
						(ui32Offset % sizeof(IMG_UINT32)))
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVSyncPrimitiveBlockToFWAddr: parameters check failed"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psAddrOut->ui32Addr = psSyncPrimBlock->uiFWAddr.ui32Addr + ui32Offset;
	return PVRSRV_OK;
}

/*!
*****************************************************************************
 @Function      : SyncAddrListGrow

 @Description   : Grow the SYNC_ADDR_LIST so it can accommodate the given
                  number of syncs

 @Input           psList       : The SYNC_ADDR_LIST to grow
 @Input           ui32NumSyncs : The number of sync addresses to be able to hold
 @Return :        PVRSRV_OK on success
*****************************************************************************/

static PVRSRV_ERROR SyncAddrListGrow(SYNC_ADDR_LIST *psList, IMG_UINT32 ui32NumSyncs)
{
	PVR_ASSERT(ui32NumSyncs <= PVRSRV_MAX_SYNC_PRIMS);
	
	if(ui32NumSyncs > psList->ui32NumSyncs)
	{
		if(psList->pasFWAddrs == NULL)
		{
			psList->pasFWAddrs = OSAllocMem(sizeof(PRGXFWIF_UFO_ADDR) * PVRSRV_MAX_SYNC_PRIMS);
			if(psList->pasFWAddrs == NULL)
			{
				return PVRSRV_ERROR_OUT_OF_MEMORY;
			}
		}

		psList->ui32NumSyncs = ui32NumSyncs;
	}

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
	if(psList->pasFWAddrs != NULL)
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

	if(ui32NumSyncs > psList->ui32NumSyncs)
	{
		eError = SyncAddrListGrow(psList, ui32NumSyncs);

		if(eError != PVRSRV_OK)
		{
			return eError;
		}
	}

	for(i = 0; i < ui32NumSyncs; i++)
	{
		eError = SyncPrimitiveBlockToFWAddr(apsSyncPrimBlock[i],
								paui32SyncOffset[i],
								&psList->pasFWAddrs[i]);

		if(eError != PVRSRV_OK)
		{
			return eError;
		}
	}

	return PVRSRV_OK;
}

#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
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

	RGX_HWPERF_HOST_ALLOC(SYNC,
	                      ui32FwBlockAddr + ui32SyncOffset,
	                      pszClassName,
	                      ui32ClassNameSize);

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

	psSyncRec->psDevNode = psDevNode;
	psSyncRec->psServerSyncPrimBlock = hServerSyncPrimBlock;
	psSyncRec->ui32SyncOffset = ui32SyncOffset;
	psSyncRec->ui32FwBlockAddr = ui32FwBlockAddr;
	psSyncRec->ui64OSTime = OSClockns64();
	psSyncRec->uiPID = OSGetCurrentProcessID();
	psSyncRec->eRecordType = bServerSync? SYNC_RECORD_TYPE_SERVER: SYNC_RECORD_TYPE_CLIENT;

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

	OSLockAcquire(psDevNode->hSyncServerRecordLock);
	dllist_add_to_head(&psDevNode->sSyncServerRecordList, &psSyncRec->sNode);
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
	PVRSRV_DEVICE_NODE *psDevNode = pSync->psDevNode;

	if (!hRecord)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	OSLockAcquire(psDevNode->hSyncServerRecordLock);

	RGX_HWPERF_HOST_FREE(SYNC, pSync->ui32FwBlockAddr + pSync->ui32SyncOffset);

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

	OSLockRelease(psDevNode->hSyncServerRecordLock);

	return PVRSRV_OK;
}
#else
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
	*phRecord = NULL;
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psDevNode);
	PVR_UNREFERENCED_PARAMETER(phRecord);
	PVR_UNREFERENCED_PARAMETER(hServerSyncPrimBlock);
	PVR_UNREFERENCED_PARAMETER(ui32FwBlockAddr);
	PVR_UNREFERENCED_PARAMETER(ui32SyncOffset);
	PVR_UNREFERENCED_PARAMETER(bServerSync);
	PVR_UNREFERENCED_PARAMETER(ui32ClassNameSize);
	PVR_UNREFERENCED_PARAMETER(pszClassName);
	return PVRSRV_OK;
}
PVRSRV_ERROR
PVRSRVSyncRecordRemoveByHandleKM(
			SYNC_RECORD_HANDLE hRecord)
{
	PVR_UNREFERENCED_PARAMETER(hRecord);
	return PVRSRV_OK;
}
#endif /* #if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING) */

PVRSRV_ERROR
PVRSRVSyncAllocEventKM(
			IMG_BOOL bServerSync,
			IMG_UINT32 ui32FWAddr,
			IMG_UINT32 ui32ClassNameSize,
			const IMG_CHAR *pszClassName)
{
	RGX_HWPERF_HOST_ALLOC(SYNC, ui32FWAddr, pszClassName, ui32ClassNameSize);

	return PVRSRV_OK;
}

PVRSRV_ERROR
PVRSRVSyncFreeEventKM(IMG_UINT32 ui32FWAddr)
{
	RGX_HWPERF_HOST_FREE(SYNC, ui32FWAddr);

	return PVRSRV_OK;
}

static
void _SyncConnectionRef(SYNC_CONNECTION_DATA *psSyncConnectionData)
{
	IMG_UINT32 ui32RefCount;

	OSLockAcquire(psSyncConnectionData->hLock);
	ui32RefCount = ++psSyncConnectionData->ui32RefCount;
	OSLockRelease(psSyncConnectionData->hLock);	

	SYNC_REFCOUNT_PRINT("%s: Sync connection %p, refcount = %d",
						__FUNCTION__, psSyncConnectionData, ui32RefCount);
}

static
void _SyncConnectionUnref(SYNC_CONNECTION_DATA *psSyncConnectionData)
{
	IMG_UINT32 ui32RefCount;

	OSLockAcquire(psSyncConnectionData->hLock);
	ui32RefCount = --psSyncConnectionData->ui32RefCount;
	OSLockRelease(psSyncConnectionData->hLock);

	if (ui32RefCount == 0)
	{
		SYNC_REFCOUNT_PRINT("%s: Sync connection %p, refcount = %d",
							__FUNCTION__, psSyncConnectionData, ui32RefCount);

		PVR_ASSERT(dllist_is_empty(&psSyncConnectionData->sListHead));
		OSLockDestroy(psSyncConnectionData->hLock);
		OSFreeMem(psSyncConnectionData);
	}
	else
	{
		SYNC_REFCOUNT_PRINT("%s: Sync connection %p, refcount = %d",
							__FUNCTION__, psSyncConnectionData, ui32RefCount);
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
		if (psConnection != NULL)
		{
			dllist_add_to_head(&psSyncConnectionData->sListHead, &psBlock->sConnectionNode);
		}
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

static
void _SyncPrimitiveBlockRef(SYNC_PRIMITIVE_BLOCK *psSyncBlk)
{
	IMG_UINT32 ui32RefCount;

	OSLockAcquire(psSyncBlk->hLock);
	ui32RefCount = ++psSyncBlk->ui32RefCount;
	OSLockRelease(psSyncBlk->hLock);

	SYNC_REFCOUNT_PRINT("%s: Sync block %p, refcount = %d",
						__FUNCTION__, psSyncBlk, ui32RefCount);
}

static
void _SyncPrimitiveBlockUnref(SYNC_PRIMITIVE_BLOCK *psSyncBlk)
{
	IMG_UINT32 ui32RefCount;

	OSLockAcquire(psSyncBlk->hLock);
	ui32RefCount = --psSyncBlk->ui32RefCount;
	OSLockRelease(psSyncBlk->hLock);

	if (ui32RefCount == 0)
	{
		PVRSRV_DEVICE_NODE *psDevNode = psSyncBlk->psDevNode;

		SYNC_REFCOUNT_PRINT("%s: Sync block %p, refcount = %d (remove)",
							__FUNCTION__, psSyncBlk, ui32RefCount);

		_SyncConnectionRemoveBlock(psSyncBlk);
		OSLockDestroy(psSyncBlk->hLock);
		DevmemReleaseCpuVirtAddr(psSyncBlk->psMemDesc);
		psDevNode->pfnFreeUFOBlock(psDevNode, psSyncBlk->psMemDesc);
		OSFreeMem(psSyncBlk);
	}
	else
	{
		SYNC_REFCOUNT_PRINT("%s: Sync block %p, refcount = %d",
							__FUNCTION__, psSyncBlk, ui32RefCount);
	}
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
	if (psNewSyncBlk == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}
	psNewSyncBlk->psDevNode = psDevNode;

	PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "Allocate UFO block");

	eError = psDevNode->pfnAllocUFOBlock(psDevNode,
										 &psNewSyncBlk->psMemDesc,
										 &psNewSyncBlk->uiFWAddr.ui32Addr,
										 &psNewSyncBlk->ui32BlockSize);
	if (eError != PVRSRV_OK)
	{
		goto e1;
	}

	*puiSyncPrimVAddr = psNewSyncBlk->uiFWAddr.ui32Addr;

	eError = DevmemAcquireCpuVirtAddr(psNewSyncBlk->psMemDesc,
									  (void **) &psNewSyncBlk->pui32LinAddr);
	if (eError != PVRSRV_OK)
	{
		goto e2;
	}

	eError = DevmemLocalGetImportHandle(psNewSyncBlk->psMemDesc, (void **) ppsSyncPMR);

	if (eError != PVRSRV_OK)
	{
		goto e3;
	}

	eError = OSLockCreate(&psNewSyncBlk->hLock, LOCK_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		goto e3;
	}

	psNewSyncBlk->ui32RefCount = 1;

	/* If there is a connection pointer then add the new block onto it's list */
	_SyncConnectionAddBlock(psConnection, psNewSyncBlk);

	*ppsSyncBlk = psNewSyncBlk;
	*puiSyncPrimBlockSize = psNewSyncBlk->ui32BlockSize;

	PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS,
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
	_SyncPrimitiveBlockUnref(psSyncBlk);

	return PVRSRV_OK;
}

PVRSRV_ERROR
PVRSRVSyncPrimSetKM(SYNC_PRIMITIVE_BLOCK *psSyncBlk, IMG_UINT32 ui32Index,
					IMG_UINT32 ui32Value)
{
	if((ui32Index * sizeof(IMG_UINT32)) < psSyncBlk->ui32BlockSize)
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

PVRSRV_ERROR
PVRSRVServerSyncPrimSetKM(SERVER_SYNC_PRIMITIVE *psServerSync, IMG_UINT32 ui32Value)
{
	*psServerSync->psSync->pui32LinAddr = ui32Value;

	return PVRSRV_OK;
}

static void
_ServerSyncRef(SERVER_SYNC_PRIMITIVE *psSync)
{
	IMG_UINT32 ui32RefCount;

	OSLockAcquire(psSync->hLock);
	ui32RefCount = ++psSync->ui32RefCount;
	OSLockRelease(psSync->hLock);

	SYNC_REFCOUNT_PRINT("%s: Server sync %p, refcount = %d",
						__FUNCTION__, psSync, ui32RefCount);
}

static void
_ServerSyncUnref(SERVER_SYNC_PRIMITIVE *psSync)
{
	PVRSRV_DEVICE_NODE *psDevNode = psSync->psDevNode;
	IMG_UINT32 ui32RefCount;

	OSLockAcquire(psSync->hLock);
	ui32RefCount = --psSync->ui32RefCount;
	OSLockRelease(psSync->hLock);

	if (ui32RefCount == 0)
	{
		IMG_UINT32 ui32SyncAddr;

		(void)SyncPrimGetFirmwareAddr(psSync->psSync, &ui32SyncAddr);
		SYNC_REFCOUNT_PRINT("%s: Server sync %p, refcount = %d",
			__FUNCTION__, psSync, ui32RefCount);
		HTBLOGK(HTB_SF_SYNC_SERVER_UNREF, ui32SyncAddr);

		/* Remove the sync from the global list */
		OSLockAcquire(psDevNode->hSyncServerListLock);
		dllist_remove_node(&psSync->sNode);
		OSLockRelease(psDevNode->hSyncServerListLock);

		OSLockDestroy(psSync->hLock);
		/* safe to ignore return value as an error indicates
		 * the sync is either already freed or not a sync
		 */
		(void)SyncPrimFree(psSync->psSync);
		OSFreeMem(psSync);
	}
	else
	{
		SYNC_REFCOUNT_PRINT("%s: Server sync %p, refcount = %d",
							__FUNCTION__, psSync, ui32RefCount);
	}
}

PVRSRV_ERROR
PVRSRVServerSyncAllocKM(CONNECTION_DATA * psConnection,
			PVRSRV_DEVICE_NODE *psDevNode,
			SERVER_SYNC_PRIMITIVE **ppsSync,
			IMG_UINT32 *pui32SyncPrimVAddr,
			IMG_UINT32 ui32ClassNameSize,
			const IMG_CHAR *pszClassName)
{
	SERVER_SYNC_PRIMITIVE *psNewSync;
	PVRSRV_ERROR eError;

	PVR_UNREFERENCED_PARAMETER(psConnection);
	
	psNewSync = OSAllocMem(sizeof(SERVER_SYNC_PRIMITIVE));
	if (psNewSync == NULL)
	{
			return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/* szClassName must be setup now and used for the SyncPrimAlloc call because
	 * pszClassName is allocated in the bridge code is not NULL terminated 
	 */
	if(pszClassName)
	{
		if (ui32ClassNameSize >= SYNC_MAX_CLASS_NAME_LEN)
			ui32ClassNameSize = SYNC_MAX_CLASS_NAME_LEN - 1;
		/* Copy over the class name annotation */
		OSStringNCopy(psNewSync->szClassName, pszClassName, ui32ClassNameSize);
		psNewSync->szClassName[ui32ClassNameSize] = 0;
	}
	else
	{
		/* No class name annotation */
		psNewSync->szClassName[0] = 0;
	}

	eError = SyncPrimAllocForServerSync(psDevNode->hSyncPrimContext,
						   &psNewSync->psSync,
						   psNewSync->szClassName);

	if (eError != PVRSRV_OK)
	{
		goto fail_sync_alloc;
	}

	eError = OSLockCreate(&psNewSync->hLock, LOCK_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		goto fail_lock_create;
	}

	eError = SyncPrimSet(psNewSync->psSync, 0);
	if (eError != PVRSRV_OK)
	{
		goto fail_sync_op;
	}

	psNewSync->psDevNode = psDevNode;
	psNewSync->ui32NextOp = 0;
	psNewSync->ui32RefCount = 1;
	psNewSync->ui32UID = g_ServerSyncUID++;
	psNewSync->ui32LastSyncRequesterID = SYNC_REQUESTOR_UNKNOWN;
	psNewSync->bSWOperation = IMG_FALSE;
	psNewSync->ui32LastHWUpdate = 0x0bad592c;
	psNewSync->bPDumped = IMG_FALSE;

	eError = SyncPrimGetFirmwareAddr(psNewSync->psSync, pui32SyncPrimVAddr);
	if (PVRSRV_OK != eError)
	{
		goto fail_sync_op;
	}

	/* Add the sync to the global list */
	OSLockAcquire(psDevNode->hSyncServerListLock);
	dllist_add_to_head(&psDevNode->sSyncServerSyncsList, &psNewSync->sNode);
	OSLockRelease(psDevNode->hSyncServerListLock);

	HTBLOGK(HTB_SF_SYNC_SERVER_ALLOC, *pui32SyncPrimVAddr);
	SYNC_UPDATES_PRINT("%s: sync: %p, fwaddr: %8.8X", __FUNCTION__, psNewSync, *pui32SyncPrimVAddr);
	*ppsSync = psNewSync;
	return PVRSRV_OK;

fail_sync_op:
	OSLockDestroy(psNewSync->hLock);

fail_lock_create:
	SyncPrimFree(psNewSync->psSync);

fail_sync_alloc:
	OSFreeMem(psNewSync);
	return eError;
}

PVRSRV_ERROR
PVRSRVServerSyncFreeKM(SERVER_SYNC_PRIMITIVE *psSync)
{
	_ServerSyncUnref(psSync);
	return PVRSRV_OK;
}

PVRSRV_ERROR
PVRSRVServerSyncGetStatusKM(IMG_UINT32 ui32SyncCount,
			SERVER_SYNC_PRIMITIVE **papsSyncs,
			IMG_UINT32 *pui32UID,
			IMG_UINT32 *pui32FWAddr,
			IMG_UINT32 *pui32CurrentOp,
			IMG_UINT32 *pui32NextOp)
{
	IMG_UINT32 i, ui32SyncAddr;
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_ERROR eReturn = PVRSRV_OK;

	for (i=0;i<ui32SyncCount;i++)
	{
		PVRSRV_CLIENT_SYNC_PRIM *psClientSync = papsSyncs[i]->psSync;

		eError = SyncPrimGetFirmwareAddr(psClientSync, &ui32SyncAddr);
		if (PVRSRV_OK != eError)
		{
			pui32FWAddr[i] = 0;
			pui32CurrentOp[i] = 0;
			eReturn = eError;
		}
		else
		{
			pui32FWAddr[i] = ui32SyncAddr;
			pui32CurrentOp[i] = *psClientSync->pui32LinAddr;
		}
		pui32NextOp[i] = papsSyncs[i]->ui32NextOp;
		pui32UID[i] = papsSyncs[i]->ui32UID;
	}
	return eReturn;
}

#if defined(SUPPORT_INSECURE_EXPORT) || defined(SUPPORT_SECURE_EXPORT)
static PVRSRV_ERROR
_PVRSRVSyncPrimServerExportKM(SERVER_SYNC_PRIMITIVE *psSync,
							  SERVER_SYNC_EXPORT **ppsExport)
{
	SERVER_SYNC_EXPORT *psNewExport;
	PVRSRV_ERROR eError;

	psNewExport = OSAllocMem(sizeof(SERVER_SYNC_EXPORT));
	if (!psNewExport)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}

	_ServerSyncRef(psSync);

	psNewExport->psSync = psSync;
	*ppsExport = psNewExport;

	return PVRSRV_OK;
e0:
	return eError;
}

static PVRSRV_ERROR
_PVRSRVSyncPrimServerUnexportKM(SERVER_SYNC_EXPORT *psExport)
{
	_ServerSyncUnref(psExport->psSync);

	OSFreeMem(psExport);

	return PVRSRV_OK;
}

static PVRSRV_ERROR
_PVRSRVSyncPrimServerImportKM(PVRSRV_DEVICE_NODE *psDevNode,
							  SERVER_SYNC_EXPORT *psExport,
							  SERVER_SYNC_PRIMITIVE **ppsSync,
							  IMG_UINT32 *pui32SyncPrimVAddr)
{
	SERVER_SYNC_PRIMITIVE *psSync = psExport->psSync;
	PVRSRV_ERROR eError;

	if (psSync->psDevNode != psDevNode)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: server sync invalid for this device\n",
				 __func__));
		return PVRSRV_ERROR_PMR_NOT_PERMITTED;
	}

	_ServerSyncRef(psSync);

	*ppsSync = psSync;
	eError = SyncPrimGetFirmwareAddr(psSync->psSync,
			pui32SyncPrimVAddr);
	return eError;
}
#endif /* defined(SUPPORT_INSECURE_EXPORT) || defined(SUPPORT_SECURE_EXPORT) */

#if defined(SUPPORT_INSECURE_EXPORT)
PVRSRV_ERROR
PVRSRVSyncPrimServerExportKM(SERVER_SYNC_PRIMITIVE *psSync,
				SERVER_SYNC_EXPORT **ppsExport)
{
	return _PVRSRVSyncPrimServerExportKM(psSync, ppsExport);
}

PVRSRV_ERROR
PVRSRVSyncPrimServerUnexportKM(SERVER_SYNC_EXPORT *psExport)
{
	return _PVRSRVSyncPrimServerUnexportKM(psExport);
}

PVRSRV_ERROR
PVRSRVSyncPrimServerImportKM(CONNECTION_DATA *psConnection,
							 PVRSRV_DEVICE_NODE *psDevNode,
							 SERVER_SYNC_EXPORT *psExport,
							 SERVER_SYNC_PRIMITIVE **ppsSync,
							 IMG_UINT32 *pui32SyncPrimVAddr)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);

	return _PVRSRVSyncPrimServerImportKM(psDevNode, psExport, ppsSync,
										 pui32SyncPrimVAddr);
}
#endif /* defined(SUPPORT_INSECURE_EXPORT) */

#if defined(SUPPORT_SECURE_EXPORT)
PVRSRV_ERROR
PVRSRVSyncPrimServerSecureExportKM(CONNECTION_DATA *psConnection,
                                   PVRSRV_DEVICE_NODE * psDevNode,
								   SERVER_SYNC_PRIMITIVE *psSync,
								   IMG_SECURE_TYPE *phSecure,
								   SERVER_SYNC_EXPORT **ppsExport,
								   CONNECTION_DATA **ppsSecureConnection)
{
	SERVER_SYNC_EXPORT *psNewExport;
	PVRSRV_ERROR eError;

	/* Create an export server sync */
	eError = _PVRSRVSyncPrimServerExportKM(psSync,
										   &psNewExport);

	if (eError != PVRSRV_OK)
	{
		goto e0;
	}

	/* Transform it into a secure export */
	eError = OSSecureExport(psConnection,
							(void *) psNewExport,
							phSecure,
							ppsSecureConnection);
	if (eError != PVRSRV_OK)
	{
		goto e1;
	}

	*ppsExport = psNewExport;
	return PVRSRV_OK;
e1:
	_PVRSRVSyncPrimServerUnexportKM(psNewExport);
e0:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

/* FIXME: This is the same as the non-secure version. */
PVRSRV_ERROR
PVRSRVSyncPrimServerSecureUnexportKM(SERVER_SYNC_EXPORT *psExport)
{
	_PVRSRVSyncPrimServerUnexportKM(psExport);
	return PVRSRV_OK;
}

PVRSRV_ERROR
PVRSRVSyncPrimServerSecureImportKM(CONNECTION_DATA *psConnection,
								   PVRSRV_DEVICE_NODE *psDevNode,
								   IMG_SECURE_TYPE hSecure,
								   SERVER_SYNC_PRIMITIVE **ppsSync,
								   IMG_UINT32 *pui32SyncPrimVAddr)
{
	PVRSRV_ERROR eError;
	SERVER_SYNC_EXPORT *psImport;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	/* Retrieve the data from the secure import */
	eError = OSSecureImport(hSecure, (void **) &psImport);
	if (eError != PVRSRV_OK)
	{
		goto e0;
	}

	eError = _PVRSRVSyncPrimServerImportKM(psDevNode, psImport, ppsSync,
										   pui32SyncPrimVAddr);
e0:
	return eError;
}
#endif /* defined(SUPPORT_SECURE_EXPORT) */

IMG_UINT32 PVRSRVServerSyncRequesterRegisterKM(IMG_UINT32 *pui32SyncRequesterID)
{
	*pui32SyncRequesterID = g_ui32NextSyncRequestorID++;

	return PVRSRV_OK;
}

void PVRSRVServerSyncRequesterUnregisterKM(IMG_UINT32 ui32SyncRequesterID)
{
	PVR_UNREFERENCED_PARAMETER(ui32SyncRequesterID);
}

static void
_ServerSyncTakeOperation(SERVER_SYNC_PRIMITIVE *psSync,
						  IMG_BOOL bUpdate,
						  IMG_UINT32 *pui32FenceValue,
						  IMG_UINT32 *pui32UpdateValue)
{
	IMG_BOOL bInCaptureRange;

	/* Only advance the pending if an update is required */
	if (bUpdate)
	{
		*pui32FenceValue = psSync->ui32NextOp++;
	}
	else
	{
		*pui32FenceValue = psSync->ui32NextOp;
	}

	*pui32UpdateValue = psSync->ui32NextOp;

	PDumpIsCaptureFrameKM(&bInCaptureRange);
	/*
		If this is the 1st operation (in this capture range) then PDump
		this sync
	*/
	if (!psSync->bPDumped && bInCaptureRange)
	{
#if defined(PDUMP)
		{
			IMG_UINT32 ui32SyncAddr;
			(void)SyncPrimGetFirmwareAddr(psSync->psSync, &ui32SyncAddr);
			PDumpCommentWithFlags(0,
				"Dump initial sync state (0x%p, FW VAddr = 0x%08x) = 0x%08x\n",
				psSync,
				ui32SyncAddr,
				*psSync->psSync->pui32LinAddr);
		}
#endif

		SyncPrimPDump(psSync->psSync);
		psSync->bPDumped = IMG_TRUE;
	}

	/*
		When exiting capture range clear down bPDumped as we might re-enter
		capture range and thus need to PDump this sync again
	*/
	if (!bInCaptureRange)
	{
		psSync->bPDumped = IMG_FALSE;
	}
}

PVRSRV_ERROR
PVRSRVServerSyncQueueSWOpKM(SERVER_SYNC_PRIMITIVE *psSync,
						  IMG_UINT32 *pui32FenceValue,
						  IMG_UINT32 *pui32UpdateValue,
						  IMG_UINT32 ui32SyncRequesterID,
						  IMG_BOOL bUpdate,
						  IMG_BOOL *pbFenceRequired)
{

	_ServerSyncRef(psSync);

	/*
		_ServerSyncRef will acquire and release the lock but we need to
		reacquire here to ensure the state that we're modifying below
		will be consistent with itself. But it doesn't matter if another
		thread acquires the lock in between as we've ensured the sync
		won't go away
	*/
	OSLockAcquire(psSync->hLock);
	_ServerSyncTakeOperation(psSync,
							 bUpdate,
							 pui32FenceValue,
							 pui32UpdateValue);

	/*
		The caller want to know if a fence command is required
		i.e. was the last operation done on this sync done by
		the same sync requester
	*/
	if (pbFenceRequired)
	{
		if (ui32SyncRequesterID == psSync->ui32LastSyncRequesterID)
		{
			*pbFenceRequired = IMG_FALSE;
		}
		else
		{
			*pbFenceRequired = IMG_TRUE;
		}
	}
	/*
		If we're transitioning from a HW operation to a SW operation we
		need to save the last update the HW will do so that when we PDump
		we can issue a POL for it before the next HW operation and then
		LDB in the last SW fence update
	*/
	if (psSync->bSWOperation == IMG_FALSE)
	{
		psSync->bSWOperation = IMG_TRUE;
		psSync->ui32LastHWUpdate = *pui32FenceValue;
		PDumpIsCaptureFrameKM(&psSync->bSWOpStartedInCaptRange);
	}

	if (pbFenceRequired)
	{
		if (*pbFenceRequired)
		{
			SYNC_UPDATES_PRINT("%s: sync: %p, fence: %d, value: %d", __FUNCTION__, psSync, *pui32FenceValue, *pui32UpdateValue);
		}
	}

	/* Only update the last requester id if we are make changes to this sync
	 * object. */
	if (bUpdate)
		psSync->ui32LastSyncRequesterID = ui32SyncRequesterID;

	OSLockRelease(psSync->hLock);

	return PVRSRV_OK;
}

PVRSRV_ERROR
PVRSRVServerSyncQueueHWOpKM(SERVER_SYNC_PRIMITIVE *psSync,
						       IMG_BOOL bUpdate,
						       IMG_UINT32 *pui32FenceValue,
						       IMG_UINT32 *pui32UpdateValue)
{
	/*
		For HW operations the client is required to ensure the
		operation has completed before freeing the sync as we
		no way of dropping the refcount if we where to acquire it
		here.

		Take the lock to ensure the state that we're modifying below
		will be consistent with itself.
	*/
	OSLockAcquire(psSync->hLock);
	_ServerSyncTakeOperation(psSync,
							 bUpdate,
							 pui32FenceValue,
							 pui32UpdateValue);

	/*
		Note:

		We might want to consider optimising the fences that we write for
		HW operations but for now just clear it back to unknown
	*/
	psSync->ui32LastSyncRequesterID = SYNC_REQUESTOR_UNKNOWN;

	if (psSync->bSWOperation)
	{
#if defined(PDUMP)
		{
			IMG_UINT32 ui32SyncAddr;
			(void)SyncPrimGetFirmwareAddr(psSync->psSync, &ui32SyncAddr);
			PDumpCommentWithFlags(0,
				"Wait for HW ops and dummy update for SW ops (0x%p, FW VAddr = 0x%08x, value = 0x%08x)\n",
				psSync,
				ui32SyncAddr,
				*pui32FenceValue);
		}
#endif

		if (psSync->bSWOpStartedInCaptRange)
		{
			/* Dump a POL for the previous HW operation */
			SyncPrimPDumpPol(psSync->psSync,
								psSync->ui32LastHWUpdate,
								0xffffffff,
								PDUMP_POLL_OPERATOR_EQUAL,
								0);
		}

		/* Dump the expected value (i.e. the value after all the SW operations) */
		SyncPrimPDumpValue(psSync->psSync, *pui32FenceValue);

		/* Reset the state as we've just done a HW operation */
		psSync->bSWOperation = IMG_FALSE;
	}
	OSLockRelease(psSync->hLock);

	SYNC_UPDATES_PRINT("%s: sync: %p, fence: %d, value: %d", __FUNCTION__, psSync, *pui32FenceValue, *pui32UpdateValue);

	return PVRSRV_OK;
}

IMG_BOOL ServerSyncFenceIsMet(SERVER_SYNC_PRIMITIVE *psSync,
							   IMG_UINT32 ui32FenceValue)
{
	SYNC_UPDATES_PRINT("%s: sync: %p, value(%d) == fence(%d)?", __FUNCTION__, psSync, *psSync->psSync->pui32LinAddr, ui32FenceValue);
	return (*psSync->psSync->pui32LinAddr == ui32FenceValue);
}

void
ServerSyncCompleteOp(SERVER_SYNC_PRIMITIVE *psSync,
					 IMG_BOOL bDoUpdate,
					 IMG_UINT32 ui32UpdateValue)
{
	if (bDoUpdate)
	{
		SYNC_UPDATES_PRINT("%s: sync: %p (%d) = %d", __FUNCTION__, psSync, *psSync->psSync->pui32LinAddr, ui32UpdateValue);

		*psSync->psSync->pui32LinAddr = ui32UpdateValue;
	}

	_ServerSyncUnref(psSync);
}

IMG_UINT32 ServerSyncGetId(SERVER_SYNC_PRIMITIVE *psSync)
{
	return psSync->ui32UID;
}

PVRSRV_ERROR
ServerSyncGetFWAddr(SERVER_SYNC_PRIMITIVE *psSync, IMG_UINT32 *pui32SyncAddr)
{
	return SyncPrimGetFirmwareAddr(psSync->psSync, pui32SyncAddr);
}

IMG_UINT32 ServerSyncGetValue(SERVER_SYNC_PRIMITIVE *psSync)
{
	return *psSync->psSync->pui32LinAddr;
}

IMG_UINT32 ServerSyncGetNextValue(SERVER_SYNC_PRIMITIVE *psSync)
{
	return psSync->ui32NextOp;
}

static void _ServerSyncState(PDLLIST_NODE psNode,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile)
{
	SERVER_SYNC_PRIMITIVE *psSync = IMG_CONTAINER_OF(psNode, SERVER_SYNC_PRIMITIVE, sNode);

	if (*psSync->psSync->pui32LinAddr != psSync->ui32NextOp)
	{
		IMG_UINT32 ui32SyncAddr;

		(void)ServerSyncGetFWAddr(psSync, &ui32SyncAddr);
#if !defined(SUPPORT_EXTRA_METASP_DEBUG)
		PVR_DUMPDEBUG_LOG("\tPending server sync (ID = %d, FWAddr = 0x%08x): Current = 0x%08x, NextOp = 0x%08x (%s)",
				psSync->ui32UID,
				ui32SyncAddr,
		                ServerSyncGetValue(psSync),
		                psSync->ui32NextOp,
		                psSync->szClassName);
#else
		PVR_DUMPDEBUG_LOG("\tPending server sync (ID = %d, FWAddr = 0x%08x): Value (Host) = 0x%08x, Value (FW) = 0x%08x, NextOp = 0x%08x (%s)",
		                   psSync->ui32UID,
				   ui32SyncAddr,
		                   ServerSyncGetValue(psSync),
		                   RGXReadWithSP(ui32SyncAddr),
		                   psSync->ui32NextOp,
		                   psSync->szClassName);
#endif
	}
}

static void _ServerSyncDebugRequest(PVRSRV_DBGREQ_HANDLE hDebugRequestHandle,
					IMG_UINT32 ui32VerbLevel,
					DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile)
{
	PVRSRV_DEVICE_NODE *psDevNode = (PVRSRV_DEVICE_NODE *)hDebugRequestHandle;
	DLLIST_NODE *psNode, *psNext;

	if (ui32VerbLevel == DEBUG_REQUEST_VERBOSITY_HIGH)
	{
		PVR_DUMPDEBUG_LOG("------[ Pending Server Syncs ]------");
		OSLockAcquire(psDevNode->hSyncServerListLock);
		dllist_foreach_node(&psDevNode->sSyncServerSyncsList, psNode, psNext)
		{
			_ServerSyncState(psNode, pfnDumpDebugPrintf, pvDumpDebugFile);
		}
		OSLockRelease(psDevNode->hSyncServerListLock);
	}
}

PVRSRV_ERROR
PVRSRVSyncPrimOpCreateKM(IMG_UINT32 ui32SyncBlockCount,
						 SYNC_PRIMITIVE_BLOCK **papsSyncPrimBlock,
						 IMG_UINT32 ui32ClientSyncCount,
						 IMG_UINT32 *paui32SyncBlockIndex,
						 IMG_UINT32 *paui32Index,
						 IMG_UINT32 ui32ServerSyncCount,
						 SERVER_SYNC_PRIMITIVE **papsServerSync,
						 SERVER_OP_COOKIE **ppsServerCookie)
{
	SERVER_OP_COOKIE *psNewCookie;
	IMG_UINT32 ui32BlockAllocSize;
	IMG_UINT32 ui32ServerAllocSize;
	IMG_UINT32 ui32ClientAllocSize;
	IMG_UINT32 ui32TotalAllocSize;
	IMG_UINT32 i;
	IMG_CHAR *pcPtr;
	PVRSRV_ERROR eError;

	/* Allocate space for all the sync block list */
	ui32BlockAllocSize = ui32SyncBlockCount * (sizeof(SYNC_PRIMITIVE_BLOCK *));

	/* Allocate space for all the client sync size elements */
	ui32ClientAllocSize = ui32ClientSyncCount * (5 * sizeof(IMG_UINT32));

	/* Allocate space for all the server sync size elements */
	ui32ServerAllocSize = ui32ServerSyncCount * (sizeof(SERVER_SYNC_PRIMITIVE *)
							+ (2 * sizeof(IMG_UINT32)));

	ui32TotalAllocSize = sizeof(SERVER_OP_COOKIE) +
							 ui32BlockAllocSize +
							 ui32ServerAllocSize +
							 ui32ClientAllocSize;

	psNewCookie = OSAllocZMem(ui32TotalAllocSize);
	pcPtr = (IMG_CHAR *) psNewCookie;

	if (!psNewCookie)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}

	/* Setup the pointers */
	pcPtr += sizeof(SERVER_OP_COOKIE);
	psNewCookie->papsSyncPrimBlock = (SYNC_PRIMITIVE_BLOCK **) pcPtr;

	pcPtr += sizeof(SYNC_PRIMITIVE_BLOCK *) * ui32SyncBlockCount;
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
	psNewCookie->papsServerSync =(SERVER_SYNC_PRIMITIVE **) pcPtr;

	pcPtr += sizeof(SERVER_SYNC_PRIMITIVE *) * ui32ServerSyncCount;
	psNewCookie->paui32ServerFenceValue = (IMG_UINT32 *) pcPtr;

	pcPtr += sizeof(IMG_UINT32) * ui32ServerSyncCount;
	psNewCookie->paui32ServerUpdateValue = (IMG_UINT32 *) pcPtr;

	pcPtr += sizeof(IMG_UINT32) * ui32ServerSyncCount;

	/* Check the pointer setup went ok */
	PVR_ASSERT(pcPtr == (((IMG_CHAR *) psNewCookie) + ui32TotalAllocSize));

	psNewCookie->ui32SyncBlockCount= ui32SyncBlockCount;
	psNewCookie->ui32ServerSyncCount = ui32ServerSyncCount;
	psNewCookie->ui32ClientSyncCount = ui32ClientSyncCount;
	psNewCookie->bActive = IMG_FALSE;
	HTBLOGK(HTB_SF_SYNC_PRIM_OP_CREATE, psNewCookie, ui32SyncBlockCount,
			ui32ServerSyncCount, ui32ClientSyncCount);

	/* Copy all the data into our server cookie */
	OSCachedMemCopy(psNewCookie->papsSyncPrimBlock,
			  papsSyncPrimBlock,
			  sizeof(SYNC_PRIMITIVE_BLOCK *) * ui32SyncBlockCount);

	OSCachedMemCopy(psNewCookie->paui32SyncBlockIndex,
			  paui32SyncBlockIndex,
			  sizeof(IMG_UINT32) * ui32ClientSyncCount);
	OSCachedMemCopy(psNewCookie->paui32Index,
			  paui32Index,
			  sizeof(IMG_UINT32) * ui32ClientSyncCount);

	OSCachedMemCopy(psNewCookie->papsServerSync,
			  papsServerSync,
			  sizeof(SERVER_SYNC_PRIMITIVE *) *ui32ServerSyncCount);

	/*
		Take a reference on all the sync blocks and server syncs so they can't
		be freed while we're using them
	*/
	for (i=0;i<ui32SyncBlockCount;i++)
	{
		_SyncPrimitiveBlockRef(psNewCookie->papsSyncPrimBlock[i]);
	}

	for (i=0;i<ui32ServerSyncCount;i++)
	{
		_ServerSyncRef(psNewCookie->papsServerSync[i]);
	}

	*ppsServerCookie = psNewCookie;
	return PVRSRV_OK;

e0:
	return eError;
}

PVRSRV_ERROR
PVRSRVSyncPrimOpTakeKM(SERVER_OP_COOKIE *psServerCookie,
					       IMG_UINT32 ui32ClientSyncCount,
					       IMG_UINT32 *paui32Flags,
					       IMG_UINT32 *paui32FenceValue,
					       IMG_UINT32 *paui32UpdateValue,
					       IMG_UINT32 ui32ServerSyncCount,
						   IMG_UINT32 *paui32ServerFlags)
{
	IMG_UINT32 i;

	if ((ui32ClientSyncCount != psServerCookie->ui32ClientSyncCount) ||
		(ui32ServerSyncCount != psServerCookie->ui32ServerSyncCount))
	{
		/* The bridge layer should have stopped us getting here but check in case */
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid sync counts", __FUNCTION__));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	for (i=0;i<ui32ServerSyncCount;i++)
	{
		/* Server syncs must fence */
		if ((paui32ServerFlags[i] & PVRSRV_CLIENT_SYNC_PRIM_OP_CHECK) == 0)
		{
			return PVRSRV_ERROR_INVALID_SYNC_PRIM_OP;
		}
	}

	/*
		For client syncs all we need to do is save the values
		that we've been passed
	*/
	OSCachedMemCopy(psServerCookie->paui32Flags,
			  paui32Flags,
			  sizeof(IMG_UINT32) * ui32ClientSyncCount);
	OSCachedMemCopy(psServerCookie->paui32FenceValue,
			  paui32FenceValue,
			  sizeof(IMG_UINT32) * ui32ClientSyncCount);
	OSCachedMemCopy(psServerCookie->paui32UpdateValue,
			  paui32UpdateValue,
			  sizeof(IMG_UINT32) * ui32ClientSyncCount);

	/*
		For server syncs we just take an operation
	*/
	for (i=0;i<ui32ServerSyncCount;i++)
	{
		/*
			Take op can only take one operation at a time so we can't
			optimise away fences so just report the requester as unknown
		*/
		PVRSRVServerSyncQueueSWOpKM(psServerCookie->papsServerSync[i],
								  &psServerCookie->paui32ServerFenceValue[i],
								  &psServerCookie->paui32ServerUpdateValue[i],
								  SYNC_REQUESTOR_UNKNOWN,
								  (paui32ServerFlags[i] & PVRSRV_CLIENT_SYNC_PRIM_OP_UPDATE) ? IMG_TRUE:IMG_FALSE,
								  NULL);
	}

	HTBLOGK(HTB_SF_SYNC_PRIM_OP_TAKE, psServerCookie,
			ui32ServerSyncCount, ui32ClientSyncCount);
	psServerCookie->bActive = IMG_TRUE;
	return PVRSRV_OK;
}

PVRSRV_ERROR
PVRSRVSyncPrimOpReadyKM(SERVER_OP_COOKIE *psServerCookie,
						IMG_BOOL *pbReady)
{
	IMG_UINT32 i;
	IMG_BOOL bReady = IMG_TRUE;
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (!psServerCookie->bActive)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Operation cookie not active (no take operation performed)", __FUNCTION__));

		bReady = IMG_FALSE;
		eError = PVRSRV_ERROR_BAD_SYNC_STATE;
		goto e0;
	}

	/* Check the client syncs */
	for (i=0;i<psServerCookie->ui32ClientSyncCount;i++)
	{
		if (psServerCookie->paui32Flags[i] & PVRSRV_CLIENT_SYNC_PRIM_OP_CHECK)
		{
			IMG_UINT32 ui32BlockIndex = psServerCookie->paui32SyncBlockIndex[i];
			IMG_UINT32 ui32Index = psServerCookie->paui32Index[i];
			SYNC_PRIMITIVE_BLOCK *psSyncBlock = psServerCookie->papsSyncPrimBlock[ui32BlockIndex];

			if (psSyncBlock->pui32LinAddr[ui32Index] !=
					psServerCookie->paui32FenceValue[i])
			{
				bReady = IMG_FALSE;
				goto e0;
			}
		}
	}

	for (i=0;i<psServerCookie->ui32ServerSyncCount;i++)
	{
		bReady = ServerSyncFenceIsMet(psServerCookie->papsServerSync[i],
									  psServerCookie->paui32ServerFenceValue[i]);
		if (!bReady)
		{
			break;
		}
	}

e0:
	*pbReady = bReady;
	return eError;
}

static
PVRSRV_ERROR _SyncPrimOpComplete(SERVER_OP_COOKIE *psServerCookie)
{
	RGX_HWPERF_UFO_DATA_ELEMENT asUFOData[PVRSRV_MAX_SYNC_PRIMS];
	IMG_UINT32 i, ui32UFOIdx = 0;

	for (i=0;i<psServerCookie->ui32ClientSyncCount;i++)
	{
		if (psServerCookie->paui32Flags[i] & PVRSRV_CLIENT_SYNC_PRIM_OP_UPDATE)
		{
			IMG_UINT32 ui32BlockIndex = psServerCookie->paui32SyncBlockIndex[i];
			IMG_UINT32 ui32Index = psServerCookie->paui32Index[i];
			SYNC_PRIMITIVE_BLOCK *psSyncBlock = psServerCookie->papsSyncPrimBlock[ui32BlockIndex];

			asUFOData[ui32UFOIdx].sUpdate.ui32FWAddr = psSyncBlock->uiFWAddr.ui32Addr + ui32Index * sizeof(IMG_UINT32);
			asUFOData[ui32UFOIdx].sUpdate.ui32OldValue = psSyncBlock->pui32LinAddr[ui32Index];
			asUFOData[ui32UFOIdx].sUpdate.ui32NewValue = psServerCookie->paui32UpdateValue[i];
			ui32UFOIdx++;

			psSyncBlock->pui32LinAddr[ui32Index] = psServerCookie->paui32UpdateValue[i];
		}
	}

	for (i=0;i<psServerCookie->ui32ServerSyncCount;i++)
	{
		IMG_BOOL bUpdate = psServerCookie->paui32ServerFenceValue[i] != psServerCookie->paui32ServerUpdateValue[i];

		if (bUpdate)
		{
			IMG_UINT32 ui32SyncAddr;

			(void)ServerSyncGetFWAddr(psServerCookie->papsServerSync[i], &ui32SyncAddr);
			asUFOData[ui32UFOIdx].sUpdate.ui32FWAddr = ui32SyncAddr;
			asUFOData[ui32UFOIdx].sUpdate.ui32OldValue = ServerSyncGetValue(psServerCookie->papsServerSync[i]);
			asUFOData[ui32UFOIdx].sUpdate.ui32NewValue = psServerCookie->paui32ServerUpdateValue[i];
			ui32UFOIdx++;
		}

		ServerSyncCompleteOp(psServerCookie->papsServerSync[i],
							 bUpdate,
							 psServerCookie->paui32ServerUpdateValue[i]);
	}

	if (ui32UFOIdx > 0)
	{
		RGX_HWPERF_HOST_UFO(RGX_HWPERF_UFO_EV_UPDATE, asUFOData, ui32UFOIdx);
	}

	psServerCookie->bActive = IMG_FALSE;
	return PVRSRV_OK;
}

PVRSRV_ERROR
PVRSRVSyncPrimOpCompleteKM(SERVER_OP_COOKIE *psServerCookie)
{
	IMG_BOOL bReady;

	PVRSRVSyncPrimOpReadyKM(psServerCookie, &bReady);

	/* Check the client is playing ball */
	if (!bReady)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: sync op still not ready", __FUNCTION__));

		return PVRSRV_ERROR_BAD_SYNC_STATE;
	}

	HTBLOGK(HTB_SF_SYNC_PRIM_OP_COMPLETE, psServerCookie);
	return _SyncPrimOpComplete(psServerCookie);
}

PVRSRV_ERROR
PVRSRVSyncPrimOpDestroyKM(SERVER_OP_COOKIE *psServerCookie)
{
	IMG_UINT32 i;

	/* If the operation is still active then check if it's finished yet */
	if (psServerCookie->bActive)
	{
		if (PVRSRVSyncPrimOpCompleteKM(psServerCookie) == PVRSRV_ERROR_BAD_SYNC_STATE)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Not ready, ask for retry", __FUNCTION__));
			return PVRSRV_ERROR_RETRY;
		}
	}

	/* Drop our references on the sync blocks and server syncs*/
	for (i = 0; i < psServerCookie->ui32SyncBlockCount; i++)
	{
		_SyncPrimitiveBlockUnref(psServerCookie->papsSyncPrimBlock[i]);
	}

	for (i = 0; i < psServerCookie->ui32ServerSyncCount; i++)
	{
		_ServerSyncUnref(psServerCookie->papsServerSync[i]);
	}

	HTBLOGK(HTB_SF_SYNC_PRIM_OP_DESTROY, psServerCookie);
	OSFreeMem(psServerCookie);
	return PVRSRV_OK;
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
PVRSRVSyncPrimOpPDumpPolKM(SERVER_OP_COOKIE *psServerCookie,
						 PDUMP_POLL_OPERATOR eOperator,
						 PDUMP_FLAGS_T ui32PDumpFlags)
{
	IMG_UINT32 i;
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (!psServerCookie->bActive)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Operation cookie not active (no take operation performed)", __FUNCTION__));

		eError = PVRSRV_ERROR_BAD_SYNC_STATE;
		goto e0;
	}

	/* PDump POL on the client syncs */
	for (i = 0; i < psServerCookie->ui32ClientSyncCount; i++)
	{
		if (psServerCookie->paui32Flags[i] & PVRSRV_CLIENT_SYNC_PRIM_OP_CHECK)
		{
			IMG_UINT32 ui32BlockIndex = psServerCookie->paui32SyncBlockIndex[i];
			IMG_UINT32 ui32Index = psServerCookie->paui32Index[i];
			SYNC_PRIMITIVE_BLOCK *psSyncBlock = psServerCookie->papsSyncPrimBlock[ui32BlockIndex];

			PVRSRVSyncPrimPDumpPolKM(psSyncBlock,
									ui32Index*sizeof(IMG_UINT32),
									psServerCookie->paui32FenceValue[i],
									0xFFFFFFFFU,
									eOperator,
									ui32PDumpFlags);
		}
	}

	/* PDump POL on the server syncs */
	for (i = 0; i < psServerCookie->ui32ServerSyncCount; i++)
	{
		SERVER_SYNC_PRIMITIVE *psServerSync = psServerCookie->papsServerSync[i];
		IMG_UINT32 ui32FenceValue = psServerCookie->paui32ServerFenceValue[i];

		SyncPrimPDumpPol(psServerSync->psSync,
						ui32FenceValue,
						0xFFFFFFFFU,
						PDUMP_POLL_OPERATOR_EQUAL,
						ui32PDumpFlags);
	}

e0:
	return eError;
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

	eError = OSLockCreate(&psSyncConnectionData->hLock, LOCK_TYPE_PASSIVE);
	if (eError != PVRSRV_OK)
	{
		goto fail_lockcreate;
	}
	dllist_init(&psSyncConnectionData->sListHead);
	psSyncConnectionData->ui32RefCount = 1;

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

void SyncConnectionPDumpSyncBlocks(SYNC_CONNECTION_DATA *psSyncConnectionData)
{
	DLLIST_NODE *psNode, *psNext;

	OSLockAcquire(psSyncConnectionData->hLock);

	PDUMPCOMMENT("Dump client Sync Prim state");
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

#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
void SyncRecordLookup(PVRSRV_DEVICE_NODE *psDevNode, IMG_UINT32 ui32FwAddr,
					  IMG_CHAR * pszSyncInfo, size_t len)
{
	DLLIST_NODE *psNode, *psNext;
	IMG_INT iEnd;

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
			break;
		}
	}

	OSLockRelease(psDevNode->hSyncServerRecordLock);
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

			PVR_DUMPDEBUG_LOG("\t%s %05u %05llu.%09u FWAddr=0x%08x Val=0x%08x (%s)",
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
			PVR_DUMPDEBUG_LOG("\t%s %05u %05llu.%09u FWAddr=0x%08x Val=<null_ptr> (%s)",
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

	if (ui32VerbLevel == DEBUG_REQUEST_VERBOSITY_HIGH)
	{
		IMG_UINT32 i;
		OSLockAcquire(psDevNode->hSyncServerRecordLock);

		PVR_DUMPDEBUG_LOG("Dumping all allocated syncs @ %05llu.%09u", ui64TimeNowS, ui32TimeNowF);
		PVR_DUMPDEBUG_LOG("\t%-6s %-5s %-15s %-17s %-14s (%s)",
					"Type", "PID", "Time Delta (s)", "Address", "Value", "Annotation");

		dllist_foreach_node(&psDevNode->sSyncServerRecordList, psNode, psNext)
		{
			struct SYNC_RECORD *psSyncRec =
				IMG_CONTAINER_OF(psNode, struct SYNC_RECORD, sNode);
			_SyncRecordPrint(psSyncRec, ui64TimeNow, pfnDumpDebugPrintf, pvDumpDebugFile);
		}

		PVR_DUMPDEBUG_LOG("Dumping all recently freed syncs @ %05llu.%09u", ui64TimeNowS, ui32TimeNowF);
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

	eError = OSLockCreate(&psDevNode->hSyncServerRecordLock, LOCK_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		goto fail_lock_create;
	}
	dllist_init(&psDevNode->sSyncServerRecordList);

	eError = PVRSRVRegisterDbgRequestNotify(&psDevNode->hSyncServerRecordNotify,
											psDevNode,
											_SyncRecordRequest,
											DEBUG_REQUEST_SERVERSYNC,
											psDevNode);

	if (eError != PVRSRV_OK)
	{
		goto fail_dbg_register;
	}

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

	PVRSRVUnregisterDbgRequestNotify(psDevNode->hSyncServerRecordNotify);
	OSLockDestroy(psDevNode->hSyncServerRecordLock);
}
#endif /* #if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING) */

PVRSRV_ERROR ServerSyncInit(PVRSRV_DEVICE_NODE *psDevNode)
{
	PVRSRV_ERROR eError;

	eError = OSLockCreate(&psDevNode->hSyncServerListLock, LOCK_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		goto fail_lock_create;
	}
	dllist_init(&psDevNode->sSyncServerSyncsList);

	eError = PVRSRVRegisterDbgRequestNotify(&psDevNode->hSyncServerNotify,
											psDevNode,
											_ServerSyncDebugRequest,
											DEBUG_REQUEST_SERVERSYNC,
											psDevNode);
	if (eError != PVRSRV_OK)
	{
		goto fail_dbg_register;
	}

#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
	eError = SyncRecordListInit(psDevNode);
	if (eError != PVRSRV_OK)
	{
		goto fail_record_list;
	}
#endif

	return PVRSRV_OK;

#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
fail_record_list:
	PVRSRVUnregisterDbgRequestNotify(psDevNode->hSyncServerNotify);
#endif
fail_dbg_register:
	OSLockDestroy(psDevNode->hSyncServerListLock);
fail_lock_create:
	return eError;
}

void ServerSyncDeinit(PVRSRV_DEVICE_NODE *psDevNode)
{
	PVRSRVUnregisterDbgRequestNotify(psDevNode->hSyncServerNotify);
	psDevNode->hSyncServerNotify = NULL;

	OSLockDestroy(psDevNode->hSyncServerListLock);
	psDevNode->hSyncServerListLock = NULL;

#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
	SyncRecordListDeinit(psDevNode);
#endif
}
