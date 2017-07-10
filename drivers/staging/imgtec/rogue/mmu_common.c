/*************************************************************************/ /*!
@File
@Title          Common MMU Management
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements basic low level control of MMU.
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
*/ /***************************************************************************/

#include "devicemem_server_utils.h"

/* Our own interface */
#include "mmu_common.h"

#include "rgx_bvnc_defs_km.h"
/*
Interfaces to other modules:

Let's keep this graph up-to-date:

   +-----------+
   | devicemem |
   +-----------+
         |
   +============+
   | mmu_common |
   +============+
         |
         +-----------------+
         |                 |
    +---------+      +----------+
    |   pmr   |      |  device  |
    +---------+      +----------+
*/

#include "img_types.h"
#include "osfunc.h"
#include "allocmem.h"
#if defined(PDUMP)
#include "pdump_km.h"
#include "pdump_physmem.h"
#endif
#include "pmr.h"
/* include/ */
#include "pvr_debug.h"
#include "pvr_notifier.h"
#include "pvrsrv_error.h"
#include "pvrsrv.h"
#include "htbuffer.h"

#include "rgxdevice.h"

#if defined(SUPPORT_GPUVIRT_VALIDATION)
#include "physmem_lma.h"
#endif

#include "dllist.h"

// #define MMU_OBJECT_REFCOUNT_DEBUGING 1
#if defined (MMU_OBJECT_REFCOUNT_DEBUGING)
#define MMU_OBJ_DBG(x)	PVR_DPF(x);
#else
#define MMU_OBJ_DBG(x)
#endif

typedef IMG_UINT32 MMU_FLAGS_T;

typedef enum _MMU_MOD_
{
	MMU_MOD_UNKNOWN = 0,
	MMU_MOD_MAP,
	MMU_MOD_UNMAP,
} MMU_MOD;


/*!
 * Refcounted structure that is shared between the context and
 * the cleanup thread items.
 * It is used to keep track of all cleanup items and whether the creating
 * MMU context has been destroyed and therefore is not allowed to be
 * accessed anymore.
 *
 * The cleanup thread is used to defer the freeing of the page tables
 * because we have to make sure that the MMU cache has been invalidated.
 * If we don't take care of this the MMU might partially access cached
 * and uncached tables which might lead to inconsistencies and in the
 * worst case to MMU pending faults on random memory.
 */
typedef struct _MMU_CTX_CLEANUP_DATA_
{
	/*! Refcount to know when this structure can be destroyed */
	IMG_UINT32 uiRef;
	/*! Protect items in this structure, especially the refcount */
	POS_LOCK hCleanupLock;
	/*! List of all cleanup items currently in flight */
	DLLIST_NODE sMMUCtxCleanupItemsHead;
	/*! Was the MMU context destroyed and should not be accessed anymore? */
	IMG_BOOL bMMUContextExists;
} MMU_CTX_CLEANUP_DATA;


/*!
 * Structure holding one or more page tables that need to be
 * freed after the MMU cache has been flushed which is signalled when
 * the stored sync has a value that is <= the required value.
 */
typedef struct _MMU_CLEANUP_ITEM_
{
	/*! Cleanup thread data */
	PVRSRV_CLEANUP_THREAD_WORK sCleanupThreadFn;
	/*! List to hold all the MMU_MEMORY_MAPPINGs, i.e. page tables */
	DLLIST_NODE sMMUMappingHead;
	/*! Node of the cleanup item list for the context */
	DLLIST_NODE sMMUCtxCleanupItem;
	/* Pointer to the cleanup meta data */
	MMU_CTX_CLEANUP_DATA *psMMUCtxCleanupData;
	/* Sync to query if the MMU cache was flushed */
	PVRSRV_CLIENT_SYNC_PRIM *psSync;
	/*! The update value of the sync to signal that the cache was flushed */
	IMG_UINT32 uiRequiredSyncVal;
	/*! The device node needed to free the page tables */
	PVRSRV_DEVICE_NODE *psDevNode;
} MMU_CLEANUP_ITEM;

/*!
	All physical allocations and frees are relative to this context, so
	we would get all the allocations of PCs, PDs, and PTs from the same
	RA.

	We have one per MMU context in case we have mixed UMA/LMA devices
	within the same system.
*/
typedef struct _MMU_PHYSMEM_CONTEXT_
{
	/*! Parent device node */
	PVRSRV_DEVICE_NODE *psDevNode;

	/*! Refcount so we know when to free up the arena */
	IMG_UINT32 uiNumAllocations;

	/*! Arena from which physical memory is derived */
	RA_ARENA *psPhysMemRA;
	/*! Arena name */
	IMG_CHAR *pszPhysMemRAName;
	/*! Size of arena name string */
	size_t uiPhysMemRANameAllocSize;

	/*! Meta data for deferred cleanup */
	MMU_CTX_CLEANUP_DATA *psCleanupData;
	/*! Temporary list of all deferred MMU_MEMORY_MAPPINGs. */
	DLLIST_NODE sTmpMMUMappingHead;

} MMU_PHYSMEM_CONTEXT;

/*!
	Mapping structure for MMU memory allocation
*/
typedef struct _MMU_MEMORY_MAPPING_
{
	/*! Physmem context to allocate from */
	MMU_PHYSMEM_CONTEXT		*psContext;
	/*! OS/system Handle for this allocation */
	PG_HANDLE				sMemHandle;
	/*! CPU virtual address of this allocation */
	void					*pvCpuVAddr;
	/*! Device physical address of this allocation */
	IMG_DEV_PHYADDR			sDevPAddr;
	/*! Size of this allocation */
	size_t					uiSize;
	/*! Number of current mappings of this allocation */
	IMG_UINT32				uiCpuVAddrRefCount;
	/*! Node for the defer free list */
	DLLIST_NODE				sMMUMappingItem;
} MMU_MEMORY_MAPPING;

/*!
	Memory descriptor for MMU objects. There can be more than one memory
	descriptor per MMU memory allocation.
*/
typedef struct _MMU_MEMORY_DESC_
{
	/* NB: bValid is set if this descriptor describes physical
	   memory.  This allows "empty" descriptors to exist, such that we
	   can allocate them in batches.  */
	/*! Does this MMU object have physical backing */
	IMG_BOOL				bValid;
	/*! Device Physical address of physical backing */
	IMG_DEV_PHYADDR			sDevPAddr;
	/*! CPU virtual address of physical backing */
	void					*pvCpuVAddr;
	/*! Mapping data for this MMU object */
	MMU_MEMORY_MAPPING		*psMapping;
	/*! Memdesc offset into the psMapping */
	IMG_UINT32 uiOffset;
	/*! Size of the Memdesc */
	IMG_UINT32 uiSize;
} MMU_MEMORY_DESC;

/*!
	MMU levelx structure. This is generic and is used
	for all levels (PC, PD, PT).
*/
typedef struct _MMU_Levelx_INFO_
{
	/*! The Number of entries in this level */
	IMG_UINT32 ui32NumOfEntries;

	/*! Number of times this level has been reference. Note: For Level1 (PTE)
	    we still take/drop the reference when setting up the page tables rather
	    then at map/unmap time as this simplifies things */
	IMG_UINT32 ui32RefCount;

	/*! MemDesc for this level */
	MMU_MEMORY_DESC sMemDesc;

	/*! Array of infos for the next level. Must be last member in structure */
	struct _MMU_Levelx_INFO_ *apsNextLevel[1];
} MMU_Levelx_INFO;

/*!
	MMU context structure
*/
struct _MMU_CONTEXT_
{
	/*! Parent device node */
	PVRSRV_DEVICE_NODE *psDevNode;

	MMU_DEVICEATTRIBS *psDevAttrs;

	/*! For allocation and deallocation of the physical memory where
	    the pagetables live */
	struct _MMU_PHYSMEM_CONTEXT_ *psPhysMemCtx;

#if defined(PDUMP)
	/*! PDump context ID (required for PDump commands with virtual addresses) */
	IMG_UINT32 uiPDumpContextID;

	/*! The refcount of the PDump context ID */
	IMG_UINT32 ui32PDumpContextIDRefCount;
#endif

	/*! Data that is passed back during device specific callbacks */
	IMG_HANDLE hDevData;

#if defined(SUPPORT_GPUVIRT_VALIDATION)
    IMG_UINT32  ui32OSid;
	IMG_UINT32	ui32OSidReg;
    IMG_BOOL   bOSidAxiProt;
#endif

	/*! Lock to ensure exclusive access when manipulating the MMU context or
	 * reading and using its content
	 */
	POS_LOCK hLock;
	
	/*! Base level info structure. Must be last member in structure */
	MMU_Levelx_INFO sBaseLevelInfo;
	/* NO OTHER MEMBERS AFTER THIS STRUCTURE ! */
};

static const IMG_DEV_PHYADDR gsBadDevPhyAddr = {MMU_BAD_PHYS_ADDR};

#if defined(DEBUG)
#include "log2.h"
#endif


/*****************************************************************************
 *                          Utility functions                                *
 *****************************************************************************/

/*************************************************************************/ /*!
@Function       _FreeMMUMapping

@Description    Free a given dllist of MMU_MEMORY_MAPPINGs and the page tables
                they represent.

@Input          psDevNode           Device node

@Input          psTmpMMUMappingHead List of MMU_MEMORY_MAPPINGs to free
*/
/*****************************************************************************/
static void
_FreeMMUMapping(PVRSRV_DEVICE_NODE *psDevNode,
                PDLLIST_NODE psTmpMMUMappingHead)
{
	PDLLIST_NODE psNode, psNextNode;

	/* Free the current list unconditionally */
	dllist_foreach_node(psTmpMMUMappingHead,
						psNode,
						psNextNode)
	{
		MMU_MEMORY_MAPPING *psMapping = IMG_CONTAINER_OF(psNode,
														 MMU_MEMORY_MAPPING,
														 sMMUMappingItem);

		psDevNode->pfnDevPxFree(psDevNode, &psMapping->sMemHandle);
		dllist_remove_node(psNode);
		OSFreeMem(psMapping);
	}
}

/*************************************************************************/ /*!
@Function       _CleanupThread_FreeMMUMapping

@Description    Function to be executed by the cleanup thread to free
                MMU_MEMORY_MAPPINGs after the MMU cache has been invalidated.

                This function will request a MMU cache invalidate once and
                retry to free the MMU_MEMORY_MAPPINGs until the invalidate
                has been executed.

                If the memory context that created this cleanup item has been
                destroyed in the meantime this function will directly free the
                MMU_MEMORY_MAPPINGs without waiting for any MMU cache
                invalidation.

@Input          pvData           Cleanup data in form of a MMU_CLEANUP_ITEM

@Return         PVRSRV_OK if successful otherwise PVRSRV_ERROR_RETRY
*/
/*****************************************************************************/
static PVRSRV_ERROR
_CleanupThread_FreeMMUMapping(void* pvData)
{
	PVRSRV_ERROR eError;
	MMU_CLEANUP_ITEM *psCleanup = (MMU_CLEANUP_ITEM *) pvData;
	MMU_CTX_CLEANUP_DATA *psMMUCtxCleanupData = psCleanup->psMMUCtxCleanupData;
	PVRSRV_DEVICE_NODE *psDevNode = psCleanup->psDevNode;
	IMG_BOOL bFreeNow;
	IMG_UINT32 uiSyncCurrent;
	IMG_UINT32 uiSyncReq;

	OSLockAcquire(psMMUCtxCleanupData->hCleanupLock);

	/* Don't attempt to free anything when the context has been destroyed.
	 * Especially don't access any device specific structures anymore!*/
	if (!psMMUCtxCleanupData->bMMUContextExists)
	{
		OSFreeMem(psCleanup);
		eError = PVRSRV_OK;
		goto e0;
	}

	if (psCleanup->psSync == NULL)
	{
		/* Kick to invalidate the MMU caches and get sync info */
		psDevNode->pfnMMUCacheInvalidateKick(psDevNode,
											 &psCleanup->uiRequiredSyncVal,
											 IMG_TRUE);
		psCleanup->psSync = psDevNode->psMMUCacheSyncPrim;
	}

	uiSyncCurrent = *(psCleanup->psSync->pui32LinAddr);
	uiSyncReq = psCleanup->uiRequiredSyncVal;

	/* Either the invalidate has been executed ... */
	bFreeNow = (uiSyncCurrent >= uiSyncReq) ? IMG_TRUE :
			/* ... with the counter wrapped around ... */
			(uiSyncReq - uiSyncCurrent) > 0xEFFFFFFFUL ? IMG_TRUE :
			/* ... or are we still waiting for the invalidate? */
			IMG_FALSE;

#if defined(NO_HARDWARE)
	/* In NOHW the syncs will never be updated so just free the tables */
	bFreeNow = IMG_TRUE;
#endif

	if (bFreeNow)
	{
		_FreeMMUMapping(psDevNode, &psCleanup->sMMUMappingHead);

		dllist_remove_node(&psCleanup->sMMUCtxCleanupItem);
		OSFreeMem(psCleanup);

		eError = PVRSRV_OK;
	}
	else
	{
		eError = PVRSRV_ERROR_RETRY;
	}

e0:

	/* If this cleanup task has been successfully executed we can
	 * decrease the context cleanup data refcount. Successfully
	 * means here that the MMU_MEMORY_MAPPINGs have been freed by
	 * either this cleanup task of when the MMU context has been
	 * destroyed. */
	if (eError == PVRSRV_OK)
	{
		IMG_UINT32 uiRef;

		uiRef = --psMMUCtxCleanupData->uiRef;
		OSLockRelease(psMMUCtxCleanupData->hCleanupLock);

		if (uiRef == 0)
		{
			OSLockDestroy(psMMUCtxCleanupData->hCleanupLock);
			OSFreeMem(psMMUCtxCleanupData);
		}
	}
	else
	{
		OSLockRelease(psMMUCtxCleanupData->hCleanupLock);
	}


	return eError;
}

/*************************************************************************/ /*!
@Function       _SetupCleanup_FreeMMUMapping

@Description    Setup a cleanup item for the cleanup thread that will
                kick off a MMU invalidate request and free the associated
                MMU_MEMORY_MAPPINGs when the invalidate was successful.

@Input          psDevNode           Device node

@Input          psPhysMemCtx        The current MMU physmem context
*/
/*****************************************************************************/
static void
_SetupCleanup_FreeMMUMapping(PVRSRV_DEVICE_NODE *psDevNode,
                             MMU_PHYSMEM_CONTEXT *psPhysMemCtx)
{

	MMU_CLEANUP_ITEM *psCleanupItem;
	MMU_CTX_CLEANUP_DATA *psCleanupData = psPhysMemCtx->psCleanupData;

	if (dllist_is_empty(&psPhysMemCtx->sTmpMMUMappingHead))
	{
		goto e0;
	}

#if !defined(SUPPORT_MMU_PENDING_FAULT_PROTECTION)
	/* If users deactivated this we immediately free the page tables */
	goto e1;
#endif

	/* Don't defer the freeing if we are currently unloading the driver
	 * or if the sync has been destroyed */
	if (PVRSRVGetPVRSRVData()->bUnload ||
	    psDevNode->psMMUCacheSyncPrim == NULL)
	{
		goto e1;
	}

	/* Allocate a cleanup item */
	psCleanupItem = OSAllocMem(sizeof(*psCleanupItem));
	if(!psCleanupItem)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Failed to get memory for deferred page table cleanup. "
				 "Freeing tables immediately",
				 __FUNCTION__));
		goto e1;
	}

	/* Set sync to NULL to indicate we did not interact with
	 * the FW yet. Kicking off an MMU cache invalidate should
	 * be done in the cleanup thread to not waste time here. */
	psCleanupItem->psSync = NULL;
	psCleanupItem->uiRequiredSyncVal = 0;
	psCleanupItem->psDevNode = psDevNode;
	psCleanupItem->psMMUCtxCleanupData = psCleanupData;

	OSLockAcquire(psCleanupData->hCleanupLock);

	psCleanupData->uiRef++;

	/* Move the page tables to free to the cleanup item */
	dllist_replace_head(&psPhysMemCtx->sTmpMMUMappingHead,
	                    &psCleanupItem->sMMUMappingHead);

	/* Add the cleanup item itself to the context list */
	dllist_add_to_tail(&psCleanupData->sMMUCtxCleanupItemsHead,
	                   &psCleanupItem->sMMUCtxCleanupItem);

	OSLockRelease(psCleanupData->hCleanupLock);

	/* Setup the cleanup thread data and add the work item */
	psCleanupItem->sCleanupThreadFn.pfnFree = _CleanupThread_FreeMMUMapping;
	psCleanupItem->sCleanupThreadFn.pvData = psCleanupItem;
	psCleanupItem->sCleanupThreadFn.ui32RetryCount = CLEANUP_THREAD_RETRY_COUNT_DEFAULT;
	psCleanupItem->sCleanupThreadFn.bDependsOnHW = IMG_TRUE;

	PVRSRVCleanupThreadAddWork(&psCleanupItem->sCleanupThreadFn);

	return;

e1:
	/* Free the page tables now */
	_FreeMMUMapping(psDevNode, &psPhysMemCtx->sTmpMMUMappingHead);
e0:
	return;
}

/*************************************************************************/ /*!
@Function       _CalcPCEIdx

@Description    Calculate the page catalogue index

@Input          sDevVAddr           Device virtual address

@Input          psDevVAddrConfig    Configuration of the virtual address

@Input          bRoundUp            Round up the index

@Return         The page catalogue index
*/
/*****************************************************************************/
static IMG_UINT32 _CalcPCEIdx(IMG_DEV_VIRTADDR sDevVAddr,
                              const MMU_DEVVADDR_CONFIG *psDevVAddrConfig,
                              IMG_BOOL bRoundUp)
{
	IMG_DEV_VIRTADDR sTmpDevVAddr;
    IMG_UINT32 ui32RetVal;

    sTmpDevVAddr = sDevVAddr;

	if (bRoundUp)
	{
        sTmpDevVAddr.uiAddr --;
    }
    ui32RetVal = (IMG_UINT32) ((sTmpDevVAddr.uiAddr & psDevVAddrConfig->uiPCIndexMask)
        >> psDevVAddrConfig->uiPCIndexShift);

    if (bRoundUp)
    {
        ui32RetVal ++;
    }

    return ui32RetVal;
}


/*************************************************************************/ /*!
@Function       _CalcPCEIdx

@Description    Calculate the page directory index

@Input          sDevVAddr           Device virtual address

@Input          psDevVAddrConfig    Configuration of the virtual address

@Input          bRoundUp            Round up the index

@Return         The page directory index
*/
/*****************************************************************************/
static IMG_UINT32 _CalcPDEIdx(IMG_DEV_VIRTADDR sDevVAddr,
                              const MMU_DEVVADDR_CONFIG *psDevVAddrConfig,
                              IMG_BOOL bRoundUp)
{
	IMG_DEV_VIRTADDR sTmpDevVAddr;
    IMG_UINT32 ui32RetVal;

    sTmpDevVAddr = sDevVAddr;

	if (bRoundUp)
	{
        sTmpDevVAddr.uiAddr --;
    }
    ui32RetVal = (IMG_UINT32) ((sTmpDevVAddr.uiAddr & psDevVAddrConfig->uiPDIndexMask)
        >> psDevVAddrConfig->uiPDIndexShift);

    if (bRoundUp)
    {
        ui32RetVal ++;
    }

    return ui32RetVal;
}


/*************************************************************************/ /*!
@Function       _CalcPTEIdx

@Description    Calculate the page entry index

@Input          sDevVAddr           Device virtual address

@Input          psDevVAddrConfig    Configuration of the virtual address

@Input          bRoundUp            Round up the index

@Return         The page entry index
*/
/*****************************************************************************/
static IMG_UINT32 _CalcPTEIdx(IMG_DEV_VIRTADDR sDevVAddr,
                              const MMU_DEVVADDR_CONFIG *psDevVAddrConfig,
                              IMG_BOOL bRoundUp)
{
	IMG_DEV_VIRTADDR sTmpDevVAddr;
    IMG_UINT32 ui32RetVal;

    sTmpDevVAddr = sDevVAddr;
    sTmpDevVAddr.uiAddr -= psDevVAddrConfig->uiOffsetInBytes;
	if (bRoundUp)
	{
        sTmpDevVAddr.uiAddr --;
    }
    ui32RetVal = (IMG_UINT32) ((sTmpDevVAddr.uiAddr & psDevVAddrConfig->uiPTIndexMask)
        >> psDevVAddrConfig->uiPTIndexShift);

    if (bRoundUp)
    {
        ui32RetVal ++;
    }

    return ui32RetVal;
}

/*****************************************************************************
 *         MMU memory allocation/management functions (mem desc)             *
 *****************************************************************************/

/*************************************************************************/ /*!
@Function       _MMU_PhysMem_RAImportAlloc

@Description    Imports MMU Px memory into the RA. This is where the
                actual allocation of physical memory happens.

@Input          hArenaHandle    Handle that was passed in during the
                                creation of the RA

@Input          uiSize          Size of the memory to import

@Input          uiFlags         Flags that where passed in the allocation.

@Output         puiBase         The address of where to insert this import

@Output         puiActualSize   The actual size of the import

@Output         phPriv          Handle which will be passed back when
                                this import is freed

@Return         PVRSRV_OK if import alloc was successful
*/
/*****************************************************************************/
static PVRSRV_ERROR _MMU_PhysMem_RAImportAlloc(RA_PERARENA_HANDLE hArenaHandle,
                                           RA_LENGTH_T uiSize,
                                           RA_FLAGS_T uiFlags,
                                           const IMG_CHAR *pszAnnotation,
                                           RA_BASE_T *puiBase,
                                           RA_LENGTH_T *puiActualSize,
                                           RA_PERISPAN_HANDLE *phPriv)
{
	MMU_PHYSMEM_CONTEXT *psCtx = (MMU_PHYSMEM_CONTEXT *) hArenaHandle;
	PVRSRV_DEVICE_NODE *psDevNode = (PVRSRV_DEVICE_NODE *) psCtx->psDevNode;
	MMU_MEMORY_MAPPING *psMapping;
	PVRSRV_ERROR eError;

	PVR_UNREFERENCED_PARAMETER(pszAnnotation);
	PVR_UNREFERENCED_PARAMETER(uiFlags);

	psMapping = OSAllocMem(sizeof(MMU_MEMORY_MAPPING));
	if (psMapping == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}

	eError = psDevNode->pfnDevPxAlloc(psDevNode, TRUNCATE_64BITS_TO_SIZE_T(uiSize), &psMapping->sMemHandle,
										&psMapping->sDevPAddr);
	if (eError != PVRSRV_OK)
	{
		goto e1;
	}

	psMapping->psContext = psCtx;
	psMapping->uiSize = TRUNCATE_64BITS_TO_SIZE_T(uiSize);

	psMapping->uiCpuVAddrRefCount = 0;

	*phPriv = (RA_PERISPAN_HANDLE) psMapping;

	/* Note: This assumes this memory never gets paged out */
	*puiBase = (RA_BASE_T)psMapping->sDevPAddr.uiAddr;
	*puiActualSize = uiSize;

	return PVRSRV_OK;

e1:
	OSFreeMem(psMapping);
e0:
	return eError;
}

/*************************************************************************/ /*!
@Function       _MMU_PhysMem_RAImportFree

@Description    Imports MMU Px memory into the RA. This is where the
                actual free of physical memory happens.

@Input          hArenaHandle    Handle that was passed in during the
                                creation of the RA

@Input          puiBase         The address of where to insert this import

@Output         phPriv          Private data that the import alloc provided

@Return         None
*/
/*****************************************************************************/
static void _MMU_PhysMem_RAImportFree(RA_PERARENA_HANDLE hArenaHandle,
									  RA_BASE_T uiBase,
									  RA_PERISPAN_HANDLE hPriv)
{
	MMU_MEMORY_MAPPING *psMapping = (MMU_MEMORY_MAPPING *) hPriv;
	MMU_PHYSMEM_CONTEXT *psCtx = (MMU_PHYSMEM_CONTEXT *) hArenaHandle;

	PVR_UNREFERENCED_PARAMETER(uiBase);

	/* Check we have dropped all CPU mappings */
	PVR_ASSERT(psMapping->uiCpuVAddrRefCount == 0);

	/* Add mapping to defer free list */
	psMapping->psContext = NULL;
	dllist_add_to_tail(&psCtx->sTmpMMUMappingHead, &psMapping->sMMUMappingItem);
}

/*************************************************************************/ /*!
@Function       _MMU_PhysMemAlloc

@Description    Allocates physical memory for MMU objects

@Input          psCtx           Physmem context to do the allocation from

@Output         psMemDesc       Allocation description

@Input          uiBytes         Size of the allocation in bytes

@Input          uiAlignment     Alignment requirement of this allocation

@Return         PVRSRV_OK if allocation was successful
*/
/*****************************************************************************/

static PVRSRV_ERROR _MMU_PhysMemAlloc(MMU_PHYSMEM_CONTEXT *psCtx,
                                      MMU_MEMORY_DESC *psMemDesc,
                                      size_t uiBytes,
                                      size_t uiAlignment)
{
	PVRSRV_ERROR eError;
	RA_BASE_T uiPhysAddr;

	if (!psMemDesc || psMemDesc->bValid)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = RA_Alloc(psCtx->psPhysMemRA,
	                  uiBytes,
	                  RA_NO_IMPORT_MULTIPLIER,
	                  0, // flags
	                  uiAlignment,
	                  "",
	                  &uiPhysAddr,
	                  NULL,
	                  (RA_PERISPAN_HANDLE *) &psMemDesc->psMapping);
	if(PVRSRV_OK != eError)
	{
		PVR_DPF((PVR_DBG_ERROR, "_MMU_PhysMemAlloc: ERROR call to RA_Alloc() failed"));
		return eError;
	}

	psMemDesc->bValid = IMG_TRUE;
	psMemDesc->pvCpuVAddr = NULL;
	psMemDesc->sDevPAddr.uiAddr = (IMG_UINT64) uiPhysAddr;

	if (psMemDesc->psMapping->uiCpuVAddrRefCount == 0)
	{
		eError = psCtx->psDevNode->pfnDevPxMap(psCtx->psDevNode,
		                                       &psMemDesc->psMapping->sMemHandle,
		                                       psMemDesc->psMapping->uiSize,
		                                       &psMemDesc->psMapping->sDevPAddr,
		                                       &psMemDesc->psMapping->pvCpuVAddr);
		if (eError != PVRSRV_OK)
		{
			RA_Free(psCtx->psPhysMemRA, psMemDesc->sDevPAddr.uiAddr);
			return eError;
		}
	}

	psMemDesc->psMapping->uiCpuVAddrRefCount++;
	psMemDesc->pvCpuVAddr = (IMG_UINT8 *) psMemDesc->psMapping->pvCpuVAddr
	                        + (psMemDesc->sDevPAddr.uiAddr - psMemDesc->psMapping->sDevPAddr.uiAddr);
	psMemDesc->uiOffset = (psMemDesc->sDevPAddr.uiAddr - psMemDesc->psMapping->sDevPAddr.uiAddr);
	psMemDesc->uiSize = uiBytes;
	PVR_ASSERT(psMemDesc->pvCpuVAddr != NULL);

	return PVRSRV_OK;
}

/*************************************************************************/ /*!
@Function       _MMU_PhysMemFree

@Description    Allocates physical memory for MMU objects

@Input          psCtx           Physmem context to do the free on

@Input          psMemDesc       Allocation description

@Return         None
*/
/*****************************************************************************/
static void _MMU_PhysMemFree(MMU_PHYSMEM_CONTEXT *psCtx,
							 MMU_MEMORY_DESC *psMemDesc)
{
	RA_BASE_T uiPhysAddr;

	PVR_ASSERT(psMemDesc->bValid);

	if (--psMemDesc->psMapping->uiCpuVAddrRefCount == 0)
	{
		psCtx->psDevNode->pfnDevPxUnMap(psCtx->psDevNode, &psMemDesc->psMapping->sMemHandle,
								psMemDesc->psMapping->pvCpuVAddr);
	}

	psMemDesc->pvCpuVAddr = NULL;

	uiPhysAddr = psMemDesc->sDevPAddr.uiAddr;
	RA_Free(psCtx->psPhysMemRA, uiPhysAddr);

	psMemDesc->bValid = IMG_FALSE;
}


/*****************************************************************************
 *              MMU object allocation/management functions                   *
 *****************************************************************************/

static INLINE void _MMU_ConvertDevMemFlags(IMG_BOOL bInvalidate,
                                           PVRSRV_MEMALLOCFLAGS_T uiMappingFlags,
                                           MMU_PROTFLAGS_T *uiMMUProtFlags,
                                           MMU_CONTEXT *psMMUContext)
{
	/* Do flag conversion between devmem flags and MMU generic flags */
	if (bInvalidate == IMG_FALSE)
	{
		*uiMMUProtFlags |= ( (uiMappingFlags & PVRSRV_MEMALLOCFLAG_DEVICE_FLAGS_MASK)
							>> PVRSRV_MEMALLOCFLAG_DEVICE_FLAGS_OFFSET)
							<< MMU_PROTFLAGS_DEVICE_OFFSET;

		if (PVRSRV_CHECK_GPU_READABLE(uiMappingFlags))
		{
			*uiMMUProtFlags |= MMU_PROTFLAGS_READABLE;
		}
		if (PVRSRV_CHECK_GPU_WRITEABLE(uiMappingFlags))
		{
			*uiMMUProtFlags |= MMU_PROTFLAGS_WRITEABLE;
		}

		switch (DevmemDeviceCacheMode(psMMUContext->psDevNode, uiMappingFlags))
		{
			case PVRSRV_MEMALLOCFLAG_GPU_UNCACHED:
			case PVRSRV_MEMALLOCFLAG_GPU_WRITE_COMBINE:
					break;
			case PVRSRV_MEMALLOCFLAG_GPU_CACHED:
					*uiMMUProtFlags |= MMU_PROTFLAGS_CACHED;
					break;
			default:
					PVR_DPF((PVR_DBG_ERROR,"_MMU_DerivePTProtFlags: Wrong parameters"));
					return;
		}

		if (DevmemDeviceCacheCoherency(psMMUContext->psDevNode, uiMappingFlags))
		{
			*uiMMUProtFlags |= MMU_PROTFLAGS_CACHE_COHERENT;
		}

		if( (psMMUContext->psDevNode->pfnCheckDeviceFeature) && \
				psMMUContext->psDevNode->pfnCheckDeviceFeature(psMMUContext->psDevNode, RGX_FEATURE_MIPS_BIT_MASK))
		{
			/*
				If we are allocating on the MMU of the firmware processor, the cached/uncached attributes
				must depend on the FIRMWARE_CACHED allocation flag.
			 */
			if (psMMUContext->psDevAttrs == psMMUContext->psDevNode->psFirmwareMMUDevAttrs)
			{
				if (uiMappingFlags & PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(FIRMWARE_CACHED))
				{
					*uiMMUProtFlags |= MMU_PROTFLAGS_CACHED;
				}
				else
				{
					*uiMMUProtFlags &= ~MMU_PROTFLAGS_CACHED;

				}
				*uiMMUProtFlags &= ~MMU_PROTFLAGS_CACHE_COHERENT;
			}
		}
	}
	else
	{
		*uiMMUProtFlags |= MMU_PROTFLAGS_INVALID;
	}
}

/*************************************************************************/ /*!
@Function       _PxMemAlloc

@Description    Allocates physical memory for MMU objects, initialises
                and PDumps it.

@Input          psMMUContext    MMU context

@Input          uiNumEntries    Number of entries to allocate

@Input          psConfig        MMU Px config

@Input          eMMULevel       MMU level that that allocation is for

@Output         psMemDesc       Description of allocation

@Return         PVRSRV_OK if allocation was successful
*/
/*****************************************************************************/
static PVRSRV_ERROR _PxMemAlloc(MMU_CONTEXT *psMMUContext,
								IMG_UINT32 uiNumEntries,
								const MMU_PxE_CONFIG *psConfig,
								MMU_LEVEL eMMULevel,
								MMU_MEMORY_DESC *psMemDesc,
								IMG_UINT32 uiLog2Align)
{
	PVRSRV_ERROR eError;
	size_t uiBytes;
	size_t uiAlign;

	PVR_ASSERT(psConfig->uiBytesPerEntry != 0);

	uiBytes = uiNumEntries * psConfig->uiBytesPerEntry;
	/* We need here the alignment of the previous level because that is the entry for we generate here */
	uiAlign = 1 << uiLog2Align;

	/*  allocate the object */
	eError = _MMU_PhysMemAlloc(psMMUContext->psPhysMemCtx,
								psMemDesc, uiBytes, uiAlign);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "_PxMemAlloc: failed to allocate memory for the  MMU object"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}

	/*
		Clear the object
		Note: if any MMUs are cleared with non-zero values then will need a
		custom clear function
		Note: 'Cached' is wrong for the LMA + ARM64 combination, but this is
		unlikely
	*/
	OSCachedMemSet(psMemDesc->pvCpuVAddr, 0, uiBytes);

	eError = psMMUContext->psDevNode->pfnDevPxClean(psMMUContext->psDevNode,
	                                                &psMemDesc->psMapping->sMemHandle,
	                                                psMemDesc->uiOffset,
	                                                psMemDesc->uiSize);
	if(eError != PVRSRV_OK)
	{
		goto e1;
	}

#if defined(PDUMP)
	PDUMPCOMMENT("Alloc MMU object");

	PDumpMMUMalloc(psMMUContext->psDevAttrs->pszMMUPxPDumpMemSpaceName,
	               eMMULevel,
	               &psMemDesc->sDevPAddr,
	               uiBytes,
	               uiAlign,
	               psMMUContext->psDevAttrs->eMMUType);

	PDumpMMUDumpPxEntries(eMMULevel,
	                      psMMUContext->psDevAttrs->pszMMUPxPDumpMemSpaceName,
	                      psMemDesc->pvCpuVAddr,
	                      psMemDesc->sDevPAddr,
	                      0,
	                      uiNumEntries,
	                      NULL, NULL, 0, /* pdump symbolic info is irrelevant here */
	                      psConfig->uiBytesPerEntry,
	                      uiLog2Align,
	                      psConfig->uiAddrShift,
	                      psConfig->uiAddrMask,
	                      psConfig->uiProtMask,
	                      psConfig->uiValidEnMask,
	                      0,
	                      psMMUContext->psDevAttrs->eMMUType);
#endif

	return PVRSRV_OK;
e1:
	_MMU_PhysMemFree(psMMUContext->psPhysMemCtx,
	                 psMemDesc);
e0:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

/*************************************************************************/ /*!
@Function       _PxMemFree

@Description    Frees physical memory for MMU objects, de-initialises
                and PDumps it.

@Input          psMemDesc       Description of allocation

@Return         PVRSRV_OK if allocation was successful
*/
/*****************************************************************************/

static void _PxMemFree(MMU_CONTEXT *psMMUContext,
					   MMU_MEMORY_DESC *psMemDesc, MMU_LEVEL eMMULevel)
{
#if defined(MMU_CLEARMEM_ON_FREE)
	PVRSRV_ERROR eError;

	/*
		Clear the MMU object
		Note: if any MMUs are cleared with non-zero values then will need a
		custom clear function
		Note: 'Cached' is wrong for the LMA + ARM64 combination, but this is
		unlikely
	*/
	OSCachedMemSet(psMemDesc->pvCpuVAddr, 0, psMemDesc->ui32Bytes);

#if defined(PDUMP)
	PDUMPCOMMENT("Clear MMU object before freeing it");
#endif
#endif/* MMU_CLEARMEM_ON_FREE */

#if defined(PDUMP)
	PDUMPCOMMENT("Free MMU object");
	{
		PDumpMMUFree(psMMUContext->psDevAttrs->pszMMUPxPDumpMemSpaceName,
		             eMMULevel,
		             &psMemDesc->sDevPAddr,
		             psMMUContext->psDevAttrs->eMMUType);
	}
#else
	PVR_UNREFERENCED_PARAMETER(eMMULevel);
#endif
	/*  free the PC */
	_MMU_PhysMemFree(psMMUContext->psPhysMemCtx, psMemDesc);
}

static INLINE PVRSRV_ERROR _SetupPTE(MMU_CONTEXT *psMMUContext,
                              MMU_Levelx_INFO *psLevel,
                              IMG_UINT32 uiIndex,
                              const MMU_PxE_CONFIG *psConfig,
                              const IMG_DEV_PHYADDR *psDevPAddr,
                              IMG_BOOL bUnmap,
#if defined(PDUMP)
                              const IMG_CHAR *pszMemspaceName,
                              const IMG_CHAR *pszSymbolicAddr,
                              IMG_DEVMEM_OFFSET_T uiSymbolicAddrOffset,
#endif
                              IMG_UINT64 uiProtFlags)
{
	MMU_MEMORY_DESC *psMemDesc = &psLevel->sMemDesc;
	IMG_UINT64 ui64PxE64;
	IMG_UINT64 uiAddr = psDevPAddr->uiAddr;

	if(psMMUContext->psDevNode->pfnCheckDeviceFeature(psMMUContext->psDevNode, \
			RGX_FEATURE_MIPS_BIT_MASK))
	{
		/*
		 * If mapping for the MIPS FW context, check for sensitive PAs
		 */
		if (psMMUContext->psDevAttrs == psMMUContext->psDevNode->psFirmwareMMUDevAttrs
			&& RGXMIPSFW_SENSITIVE_ADDR(uiAddr))
		{
			PVRSRV_RGXDEV_INFO *psDevice = (PVRSRV_RGXDEV_INFO *)psMMUContext->psDevNode->pvDevice;

			uiAddr = psDevice->sTrampoline.sPhysAddr.uiAddr + RGXMIPSFW_TRAMPOLINE_OFFSET(uiAddr);
		}
	}

	/* Calculate Entry */
	ui64PxE64 =    uiAddr /* Calculate the offset to that base */
	            >> psConfig->uiAddrLog2Align /* Shift away the useless bits, because the alignment is very coarse and we address by alignment */
	            << psConfig->uiAddrShift /* Shift back to fit address in the Px entry */
	             & psConfig->uiAddrMask; /* Delete unused bits */
	ui64PxE64 |= uiProtFlags;

	/* Set the entry */
	if (psConfig->uiBytesPerEntry == 8)
	{
		IMG_UINT64 *pui64Px = psMemDesc->pvCpuVAddr; /* Give the virtual base address of Px */

		pui64Px[uiIndex] = ui64PxE64;
	}
	else if (psConfig->uiBytesPerEntry == 4)
	{
		IMG_UINT32 *pui32Px = psMemDesc->pvCpuVAddr; /* Give the virtual base address of Px */

		/* assert that the result fits into 32 bits before writing
		   it into the 32-bit array with a cast */
		PVR_ASSERT(ui64PxE64 == (ui64PxE64 & 0xffffffffU));

		pui32Px[uiIndex] = (IMG_UINT32) ui64PxE64;
	}
	else
	{
		return PVRSRV_ERROR_MMU_CONFIG_IS_WRONG;
	}


	/* Log modification */
	HTBLOGK(HTB_SF_MMU_PAGE_OP_TABLE,
		HTBLOG_PTR_BITS_HIGH(psLevel), HTBLOG_PTR_BITS_LOW(psLevel),
		uiIndex, MMU_LEVEL_1,
		HTBLOG_U64_BITS_HIGH(ui64PxE64), HTBLOG_U64_BITS_LOW(ui64PxE64),
		!bUnmap);

#if defined (PDUMP)
	PDumpMMUDumpPxEntries(MMU_LEVEL_1,
	                      psMMUContext->psDevAttrs->pszMMUPxPDumpMemSpaceName,
	                      psMemDesc->pvCpuVAddr,
	                      psMemDesc->sDevPAddr,
	                      uiIndex,
	                      1,
	                      pszMemspaceName,
	                      pszSymbolicAddr,
	                      uiSymbolicAddrOffset,
	                      psConfig->uiBytesPerEntry,
	                      psConfig->uiAddrLog2Align,
	                      psConfig->uiAddrShift,
	                      psConfig->uiAddrMask,
	                      psConfig->uiProtMask,
	                      psConfig->uiValidEnMask,
	                      0,
	                      psMMUContext->psDevAttrs->eMMUType);
#endif /*PDUMP*/

	return PVRSRV_OK;
}

/*************************************************************************/ /*!
@Function       _SetupPxE

@Description    Sets up an entry of an MMU object to point to the
                provided address

@Input          psMMUContext    MMU context to operate on

@Input          psLevel         Level info for MMU object

@Input          uiIndex         Index into the MMU object to setup

@Input          psConfig        MMU Px config

@Input          eMMULevel       Level of MMU object

@Input          psDevPAddr      Address to setup the MMU object to point to

@Input          pszMemspaceName Name of the PDump memory space that the entry
                                will point to

@Input          pszSymbolicAddr PDump symbolic address that the entry will
                                point to

@Input          uiProtFlags     MMU protection flags

@Return         PVRSRV_OK if the setup was successful
*/
/*****************************************************************************/
static PVRSRV_ERROR _SetupPxE(MMU_CONTEXT *psMMUContext,
								MMU_Levelx_INFO *psLevel,
								IMG_UINT32 uiIndex,
								const MMU_PxE_CONFIG *psConfig,
								MMU_LEVEL eMMULevel,
								const IMG_DEV_PHYADDR *psDevPAddr,
#if defined(PDUMP)
								const IMG_CHAR *pszMemspaceName,
								const IMG_CHAR *pszSymbolicAddr,
								IMG_DEVMEM_OFFSET_T uiSymbolicAddrOffset,
#endif
								MMU_FLAGS_T uiProtFlags,
								IMG_UINT32 uiLog2DataPageSize)
{
	PVRSRV_DEVICE_NODE *psDevNode = psMMUContext->psDevNode;
	MMU_MEMORY_DESC *psMemDesc = &psLevel->sMemDesc;

	IMG_UINT32 (*pfnDerivePxEProt4)(IMG_UINT32);
	IMG_UINT64 (*pfnDerivePxEProt8)(IMG_UINT32, IMG_UINT32);

	if (!psDevPAddr)
	{
		/* Invalidate entry */
		if (~uiProtFlags & MMU_PROTFLAGS_INVALID)
		{
			PVR_DPF((PVR_DBG_ERROR, "Error, no physical address specified, but not invalidating entry"));
			uiProtFlags |= MMU_PROTFLAGS_INVALID;
		}
		psDevPAddr = &gsBadDevPhyAddr;
	}
	else
	{
		if (uiProtFlags & MMU_PROTFLAGS_INVALID)
		{
			PVR_DPF((PVR_DBG_ERROR, "A physical address was specified when requesting invalidation of entry"));
			uiProtFlags |= MMU_PROTFLAGS_INVALID;
		}
	}

	switch(eMMULevel)
	{
		case MMU_LEVEL_3:
				pfnDerivePxEProt4 = psMMUContext->psDevAttrs->pfnDerivePCEProt4;
				pfnDerivePxEProt8 = psMMUContext->psDevAttrs->pfnDerivePCEProt8;
				break;

		case MMU_LEVEL_2:
				pfnDerivePxEProt4 = psMMUContext->psDevAttrs->pfnDerivePDEProt4;
				pfnDerivePxEProt8 = psMMUContext->psDevAttrs->pfnDerivePDEProt8;
				break;

		case MMU_LEVEL_1:
				pfnDerivePxEProt4 = psMMUContext->psDevAttrs->pfnDerivePTEProt4;
				pfnDerivePxEProt8 = psMMUContext->psDevAttrs->pfnDerivePTEProt8;
				break;

		default:
				PVR_DPF((PVR_DBG_ERROR, "%s: invalid MMU level", __func__));
				return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* How big is a PxE in bytes? */
	/* Filling the actual Px entry with an address */
	switch(psConfig->uiBytesPerEntry)
	{
		case 4:
		{
			IMG_UINT32 *pui32Px;
			IMG_UINT64 ui64PxE64;

			pui32Px = psMemDesc->pvCpuVAddr; /* Give the virtual base address of Px */

			ui64PxE64 = psDevPAddr->uiAddr               /* Calculate the offset to that base */
							>> psConfig->uiAddrLog2Align /* Shift away the unnecessary bits of the address */
							<< psConfig->uiAddrShift     /* Shift back to fit address in the Px entry */
							& psConfig->uiAddrMask;      /* Delete unused higher bits */

			ui64PxE64 |= (IMG_UINT64)pfnDerivePxEProt4(uiProtFlags);
			/* assert that the result fits into 32 bits before writing
			   it into the 32-bit array with a cast */
			PVR_ASSERT(ui64PxE64 == (ui64PxE64 & 0xffffffffU));

			/* We should never invalidate an invalid page */
			if (uiProtFlags & MMU_PROTFLAGS_INVALID)
			{
				PVR_ASSERT(pui32Px[uiIndex] != ui64PxE64);
			}
			pui32Px[uiIndex] = (IMG_UINT32) ui64PxE64;
			HTBLOGK(HTB_SF_MMU_PAGE_OP_TABLE,
				HTBLOG_PTR_BITS_HIGH(psLevel), HTBLOG_PTR_BITS_LOW(psLevel),
				uiIndex, eMMULevel,
				HTBLOG_U64_BITS_HIGH(ui64PxE64), HTBLOG_U64_BITS_LOW(ui64PxE64),
				(uiProtFlags & MMU_PROTFLAGS_INVALID)? 0: 1);
			break;
		}
		case 8:
		{
			IMG_UINT64 *pui64Px = psMemDesc->pvCpuVAddr; /* Give the virtual base address of Px */
			
			pui64Px[uiIndex] = psDevPAddr->uiAddr             /* Calculate the offset to that base */
								>> psConfig->uiAddrLog2Align  /* Shift away the unnecessary bits of the address */
								<< psConfig->uiAddrShift      /* Shift back to fit address in the Px entry */
								& psConfig->uiAddrMask;       /* Delete unused higher bits */
			pui64Px[uiIndex] |= pfnDerivePxEProt8(uiProtFlags, uiLog2DataPageSize);

			HTBLOGK(HTB_SF_MMU_PAGE_OP_TABLE,
				HTBLOG_PTR_BITS_HIGH(psLevel), HTBLOG_PTR_BITS_LOW(psLevel),
				uiIndex, eMMULevel,
				HTBLOG_U64_BITS_HIGH(pui64Px[uiIndex]), HTBLOG_U64_BITS_LOW(pui64Px[uiIndex]),
				(uiProtFlags & MMU_PROTFLAGS_INVALID)? 0: 1);
			break;
		}
		default:
			PVR_DPF((PVR_DBG_ERROR, "%s: PxE size not supported (%d) for level %d",
									__func__, psConfig->uiBytesPerEntry, eMMULevel));

			return PVRSRV_ERROR_MMU_CONFIG_IS_WRONG;
	}

#if defined (PDUMP)
	PDumpMMUDumpPxEntries(eMMULevel,
	                      psMMUContext->psDevAttrs->pszMMUPxPDumpMemSpaceName,
	                      psMemDesc->pvCpuVAddr,
	                      psMemDesc->sDevPAddr,
	                      uiIndex,
	                      1,
	                      pszMemspaceName,
	                      pszSymbolicAddr,
	                      uiSymbolicAddrOffset,
	                      psConfig->uiBytesPerEntry,
	                      psConfig->uiAddrLog2Align,
	                      psConfig->uiAddrShift,
	                      psConfig->uiAddrMask,
	                      psConfig->uiProtMask,
	                      psConfig->uiValidEnMask,
	                      0,
	                      psMMUContext->psDevAttrs->eMMUType);
#endif

	psDevNode->pfnMMUCacheInvalidate(psDevNode, psMMUContext->hDevData,
									 eMMULevel,
									 (uiProtFlags & MMU_PROTFLAGS_INVALID)?IMG_TRUE:IMG_FALSE);
	
	return PVRSRV_OK;
}

/*****************************************************************************
 *                   MMU host control functions (Level Info)                 *
 *****************************************************************************/


/*************************************************************************/ /*!
@Function       _MMU_FreeLevel

@Description    Recursively frees the specified range of Px entries. If any
                level has its last reference dropped then the MMU object
                memory and the MMU_Levelx_Info will be freed.

				At each level we might be crossing a boundary from one Px to
				another. The values for auiStartArray should be by used for
				the first call into each level and the values in auiEndArray
				should only be used in the last call for each level.
				In order to determine if this is the first/last call we pass
				in bFirst and bLast.
				When one level calls down to the next only if bFirst/bLast is set
				and it's the first/last iteration of the loop at its level will
				bFirst/bLast set for the next recursion.
				This means that each iteration has the knowledge of the previous
				level which is required.

@Input          psMMUContext    MMU context to operate on

@Input          psLevel                 Level info on which to free the
                                        specified range

@Input          auiStartArray           Array of start indexes (one for each level)

@Input          auiEndArray             Array of end indexes (one for each level)

@Input          auiEntriesPerPxArray    Array of number of entries for the Px
                                        (one for each level)

@Input          apsConfig               Array of PxE configs (one for each level)

@Input          aeMMULevel              Array of MMU levels (one for each level)

@Input          pui32CurrentLevel       Pointer to a variable which is set to our
                                        current level

@Input          uiStartIndex            Start index of the range to free

@Input          uiEndIndex              End index of the range to free

@Input			bFirst                  This is the first call for this level

@Input			bLast                   This is the last call for this level

@Return         IMG_TRUE if the last reference to psLevel was dropped
*/
/*****************************************************************************/
static IMG_BOOL _MMU_FreeLevel(MMU_CONTEXT *psMMUContext,
							   MMU_Levelx_INFO *psLevel,
							   IMG_UINT32 auiStartArray[],
							   IMG_UINT32 auiEndArray[],
							   IMG_UINT32 auiEntriesPerPxArray[],
							   const MMU_PxE_CONFIG *apsConfig[],
							   MMU_LEVEL aeMMULevel[],
							   IMG_UINT32 *pui32CurrentLevel,
							   IMG_UINT32 uiStartIndex,
							   IMG_UINT32 uiEndIndex,
							   IMG_BOOL bFirst,
							   IMG_BOOL bLast,
							   IMG_UINT32 uiLog2DataPageSize)
{
	IMG_UINT32 uiThisLevel = *pui32CurrentLevel;
	const MMU_PxE_CONFIG *psConfig = apsConfig[uiThisLevel];
	IMG_UINT32 i;
	IMG_BOOL bFreed = IMG_FALSE;

	/* Sanity check */
	PVR_ASSERT(*pui32CurrentLevel < MMU_MAX_LEVEL);
	PVR_ASSERT(psLevel != NULL);

	MMU_OBJ_DBG((PVR_DBG_ERROR, "_MMU_FreeLevel: level = %d, range %d - %d, refcount = %d",
				aeMMULevel[uiThisLevel], uiStartIndex,
				uiEndIndex, psLevel->ui32RefCount));

	for (i = uiStartIndex;(i < uiEndIndex) && (psLevel != NULL);i++)
	{
		if (aeMMULevel[uiThisLevel] != MMU_LEVEL_1)
		{
			MMU_Levelx_INFO *psNextLevel = psLevel->apsNextLevel[i];
			IMG_UINT32 uiNextStartIndex;
			IMG_UINT32 uiNextEndIndex;
			IMG_BOOL bNextFirst;
			IMG_BOOL bNextLast;

			/* If we're crossing a Px then the start index changes */
			if (bFirst && (i == uiStartIndex))
			{
				uiNextStartIndex = auiStartArray[uiThisLevel + 1];
				bNextFirst = IMG_TRUE;
			}
			else
			{
				uiNextStartIndex = 0;
				bNextFirst = IMG_FALSE;
			}

			/* If we're crossing a Px then the end index changes */
			if (bLast && (i == (uiEndIndex - 1)))
			{
				uiNextEndIndex = auiEndArray[uiThisLevel + 1];
				bNextLast = IMG_TRUE;
			}
			else
			{
				uiNextEndIndex = auiEntriesPerPxArray[uiThisLevel + 1];
				bNextLast = IMG_FALSE;
			}

			/* Recurse into the next level */
			(*pui32CurrentLevel)++;
			if (_MMU_FreeLevel(psMMUContext, psNextLevel, auiStartArray,
								auiEndArray, auiEntriesPerPxArray,
								apsConfig, aeMMULevel, pui32CurrentLevel,
								uiNextStartIndex, uiNextEndIndex,
								bNextFirst, bNextLast, uiLog2DataPageSize))
			{
				PVRSRV_ERROR eError;

				/* Un-wire the entry */
				eError = _SetupPxE(psMMUContext,
								psLevel,
								i,
								psConfig,
								aeMMULevel[uiThisLevel],
								NULL,
#if defined(PDUMP)
								NULL,	/* Only required for data page */
								NULL,	/* Only required for data page */
								0,      /* Only required for data page */
#endif
								MMU_PROTFLAGS_INVALID,
								uiLog2DataPageSize);

				PVR_ASSERT(eError == PVRSRV_OK);

				/* Free table of the level below, pointed to by this table entry.
				 * We don't destroy the table inside the above _MMU_FreeLevel call because we
				 * first have to set the table entry of the level above to invalid. */
				_PxMemFree(psMMUContext, &psNextLevel->sMemDesc, aeMMULevel[*pui32CurrentLevel]);
				OSFreeMem(psNextLevel);

				/* The level below us is empty, drop the refcount and clear the pointer */
				psLevel->ui32RefCount--;
				psLevel->apsNextLevel[i] = NULL;

				/* Check we haven't wrapped around */
				PVR_ASSERT(psLevel->ui32RefCount <= psLevel->ui32NumOfEntries);
			}
			(*pui32CurrentLevel)--;
		}
		else
		{
			psLevel->ui32RefCount--;
		}

		/*
		   Free this level if it is no longer referenced, unless it's the base
		   level in which case it's part of the MMU context and should be freed
		   when the MMU context is freed
		*/
		if ((psLevel->ui32RefCount == 0) && (psLevel != &psMMUContext->sBaseLevelInfo))
		{
			bFreed = IMG_TRUE;
		}
	}

	/* Level one flushing is done when we actually write the table entries */
	if (aeMMULevel[uiThisLevel] != MMU_LEVEL_1)
	{
		psMMUContext->psDevNode->pfnDevPxClean(psMMUContext->psDevNode,
		                                       &psLevel->sMemDesc.psMapping->sMemHandle,
		                                       uiStartIndex * psConfig->uiBytesPerEntry + psLevel->sMemDesc.uiOffset,
		                                       (uiEndIndex - uiStartIndex) * psConfig->uiBytesPerEntry);
	}

	MMU_OBJ_DBG((PVR_DBG_ERROR, "_MMU_FreeLevel end: level = %d, refcount = %d",
				aeMMULevel[uiThisLevel], bFreed?0:psLevel->ui32RefCount));

	return bFreed;
}

/*************************************************************************/ /*!
@Function       _MMU_AllocLevel

@Description    Recursively allocates the specified range of Px entries. If any
                level has its last reference dropped then the MMU object
                memory and the MMU_Levelx_Info will be freed.

				At each level we might be crossing a boundary from one Px to
				another. The values for auiStartArray should be by used for
				the first call into each level and the values in auiEndArray
				should only be used in the last call for each level.
				In order to determine if this is the first/last call we pass
				in bFirst and bLast.
				When one level calls down to the next only if bFirst/bLast is set
				and it's the first/last iteration of the loop at its level will
				bFirst/bLast set for the next recursion.
				This means that each iteration has the knowledge of the previous
				level which is required.

@Input          psMMUContext    MMU context to operate on

@Input          psLevel                 Level info on which to to free the
                                        specified range

@Input          auiStartArray           Array of start indexes (one for each level)

@Input          auiEndArray             Array of end indexes (one for each level)

@Input          auiEntriesPerPxArray    Array of number of entries for the Px
                                        (one for each level)

@Input          apsConfig               Array of PxE configs (one for each level)

@Input          aeMMULevel              Array of MMU levels (one for each level)

@Input          pui32CurrentLevel       Pointer to a variable which is set to our
                                        current level

@Input          uiStartIndex            Start index of the range to free

@Input          uiEndIndex              End index of the range to free

@Input			bFirst                  This is the first call for this level

@Input			bLast                   This is the last call for this level

@Return         IMG_TRUE if the last reference to psLevel was dropped
*/
/*****************************************************************************/
static PVRSRV_ERROR _MMU_AllocLevel(MMU_CONTEXT *psMMUContext,
									MMU_Levelx_INFO *psLevel,
									IMG_UINT32 auiStartArray[],
									IMG_UINT32 auiEndArray[],
									IMG_UINT32 auiEntriesPerPxArray[],
									const MMU_PxE_CONFIG *apsConfig[],
									MMU_LEVEL aeMMULevel[],
									IMG_UINT32 *pui32CurrentLevel,
									IMG_UINT32 uiStartIndex,
									IMG_UINT32 uiEndIndex,
									IMG_BOOL bFirst,
									IMG_BOOL bLast,
									IMG_UINT32 uiLog2DataPageSize)
{
	IMG_UINT32 uiThisLevel = *pui32CurrentLevel; /* Starting with 0 */
	const MMU_PxE_CONFIG *psConfig = apsConfig[uiThisLevel]; /* The table config for the current level */
	PVRSRV_ERROR eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	IMG_UINT32 uiAllocState = 99; /* Debug info to check what progress was made in the function. Updated during this function. */
	IMG_UINT32 i;

	/* Sanity check */
	PVR_ASSERT(*pui32CurrentLevel < MMU_MAX_LEVEL);

	MMU_OBJ_DBG((PVR_DBG_ERROR, "_MMU_AllocLevel: level = %d, range %d - %d, refcount = %d",
				aeMMULevel[uiThisLevel], uiStartIndex,
				uiEndIndex, psLevel->ui32RefCount));

	/* Go from uiStartIndex to uiEndIndex through the Px */
	for (i = uiStartIndex;i < uiEndIndex;i++)
	{
		/* Only try an allocation if this is not the last level */
		/*Because a PT allocation is already done while setting the entry in PD */
		if (aeMMULevel[uiThisLevel] != MMU_LEVEL_1)
		{
			IMG_UINT32 uiNextStartIndex;
			IMG_UINT32 uiNextEndIndex;
			IMG_BOOL bNextFirst;
			IMG_BOOL bNextLast;

			/* If there is already a next Px level existing, do not allocate it */
			if (!psLevel->apsNextLevel[i])
			{
				MMU_Levelx_INFO *psNextLevel;
				IMG_UINT32 ui32AllocSize;
				IMG_UINT32 uiNextEntries;

				/* Allocate and setup the next level */
				uiNextEntries = auiEntriesPerPxArray[uiThisLevel + 1];
				ui32AllocSize = sizeof(MMU_Levelx_INFO);
				if (aeMMULevel[uiThisLevel + 1] != MMU_LEVEL_1)
				{
					ui32AllocSize += sizeof(MMU_Levelx_INFO *) * (uiNextEntries - 1);
				}
				psNextLevel = OSAllocZMem(ui32AllocSize);
				if (psNextLevel == NULL)
				{
					uiAllocState = 0;
					goto e0;
				}

				/* Hook in this level for next time */
				psLevel->apsNextLevel[i] = psNextLevel;

				psNextLevel->ui32NumOfEntries = uiNextEntries;
				psNextLevel->ui32RefCount = 0;
				/* Allocate Px memory for a sub level*/
				eError = _PxMemAlloc(psMMUContext, uiNextEntries, apsConfig[uiThisLevel + 1],
										aeMMULevel[uiThisLevel + 1],
										&psNextLevel->sMemDesc,
										psConfig->uiAddrLog2Align);
				if (eError != PVRSRV_OK)
				{
					uiAllocState = 1;
					goto e0;
				}

				/* Wire up the entry */
				eError = _SetupPxE(psMMUContext,
									psLevel,
									i,
									psConfig,
									aeMMULevel[uiThisLevel],
									&psNextLevel->sMemDesc.sDevPAddr,
#if defined(PDUMP)
									NULL, /* Only required for data page */
									NULL, /* Only required for data page */
									0,    /* Only required for data page */
#endif
									0,
									uiLog2DataPageSize);

				if (eError != PVRSRV_OK)
				{
					uiAllocState = 2;
					goto e0;
				}

				psLevel->ui32RefCount++;
			}

			/* If we're crossing a Px then the start index changes */
			if (bFirst && (i == uiStartIndex))
			{
				uiNextStartIndex = auiStartArray[uiThisLevel + 1];
				bNextFirst = IMG_TRUE;
			}
			else
			{
				uiNextStartIndex = 0;
				bNextFirst = IMG_FALSE;
			}

			/* If we're crossing a Px then the end index changes */
			if (bLast && (i == (uiEndIndex - 1)))
			{
				uiNextEndIndex = auiEndArray[uiThisLevel + 1];
				bNextLast = IMG_TRUE;
			}
			else
			{
				uiNextEndIndex = auiEntriesPerPxArray[uiThisLevel + 1];
				bNextLast = IMG_FALSE;
			}

			/* Recurse into the next level */
			(*pui32CurrentLevel)++;
			eError = _MMU_AllocLevel(psMMUContext, psLevel->apsNextLevel[i],
									 auiStartArray,
									 auiEndArray,
									 auiEntriesPerPxArray,
									 apsConfig,
									 aeMMULevel,
									 pui32CurrentLevel,
									 uiNextStartIndex,
									 uiNextEndIndex,
									 bNextFirst,
									 bNextLast,
									 uiLog2DataPageSize);
			(*pui32CurrentLevel)--;
			if (eError != PVRSRV_OK)
			{
				uiAllocState = 2;
				goto e0;
			}
		}
		else
		{
			/* All we need to do for level 1 is bump the refcount */
			psLevel->ui32RefCount++;
		}
		PVR_ASSERT(psLevel->ui32RefCount <= psLevel->ui32NumOfEntries);
	}

	/* Level one flushing is done when we actually write the table entries */
	if (aeMMULevel[uiThisLevel] != MMU_LEVEL_1)
	{
		eError = psMMUContext->psDevNode->pfnDevPxClean(psMMUContext->psDevNode,
		                                                &psLevel->sMemDesc.psMapping->sMemHandle,
		                                                uiStartIndex * psConfig->uiBytesPerEntry + psLevel->sMemDesc.uiOffset,
		                                                (uiEndIndex - uiStartIndex) * psConfig->uiBytesPerEntry);
		if (eError != PVRSRV_OK)
			goto e0;
	}

	MMU_OBJ_DBG((PVR_DBG_ERROR, "_MMU_AllocLevel end: level = %d, refcount = %d",
				aeMMULevel[uiThisLevel], psLevel->ui32RefCount));
	return PVRSRV_OK;

e0:
	/* Sanity check that we've not come down this route unexpectedly */
	PVR_ASSERT(uiAllocState!=99);
	PVR_DPF((PVR_DBG_ERROR, "_MMU_AllocLevel: Error %d allocating Px for level %d in stage %d"
							,eError, aeMMULevel[uiThisLevel], uiAllocState));

	/* the start value of index variable i is nor initialised on purpose
	   indeed this for loop deinitialise what has already been initialised
	   just before failing in reverse order. So the i index has already the
	   right value. */
	for (/* i already set */ ; i>= uiStartIndex  &&  i< uiEndIndex; i--)
	{
		switch(uiAllocState)
		{
			IMG_UINT32 uiNextStartIndex;
			IMG_UINT32 uiNextEndIndex;
			IMG_BOOL bNextFirst;
			IMG_BOOL bNextLast;

			case 3:
					/* If we're crossing a Px then the start index changes */
					if (bFirst && (i == uiStartIndex))
					{
						uiNextStartIndex = auiStartArray[uiThisLevel + 1];
						bNextFirst = IMG_TRUE;
					}
					else
					{
						uiNextStartIndex = 0;
						bNextFirst = IMG_FALSE;
					}

					/* If we're crossing a Px then the end index changes */
					if (bLast && (i == (uiEndIndex - 1)))
					{
						uiNextEndIndex = auiEndArray[uiThisLevel + 1];
						bNextLast = IMG_TRUE;
					}
					else
					{
						uiNextEndIndex = auiEntriesPerPxArray[uiThisLevel + 1];
						bNextLast = IMG_FALSE;
					}

					if (aeMMULevel[uiThisLevel] != MMU_LEVEL_1)
					{
						(*pui32CurrentLevel)++;
						if (_MMU_FreeLevel(psMMUContext, psLevel->apsNextLevel[i],
											auiStartArray, auiEndArray,
											auiEntriesPerPxArray, apsConfig,
											aeMMULevel, pui32CurrentLevel,
											uiNextStartIndex, uiNextEndIndex,
											bNextFirst, bNextLast, uiLog2DataPageSize))
						{
							psLevel->ui32RefCount--;
							psLevel->apsNextLevel[i] = NULL;

							/* Check we haven't wrapped around */
							PVR_ASSERT(psLevel->ui32RefCount <= psLevel->ui32NumOfEntries);
						}
						(*pui32CurrentLevel)--;
					}
					else
					{
						/* We should never come down this path, but it's here
						   for completeness */
						psLevel->ui32RefCount--;

						/* Check we haven't wrapped around */
						PVR_ASSERT(psLevel->ui32RefCount <= psLevel->ui32NumOfEntries);
					}
			case 2:
					if (psLevel->apsNextLevel[i] != NULL  &&
					    psLevel->apsNextLevel[i]->ui32RefCount == 0)
					{
						_PxMemFree(psMMUContext, &psLevel->sMemDesc,
									aeMMULevel[uiThisLevel]);
					}
			case 1:
					if (psLevel->apsNextLevel[i] != NULL  &&
					    psLevel->apsNextLevel[i]->ui32RefCount == 0)
					{
						OSFreeMem(psLevel->apsNextLevel[i]);
						psLevel->apsNextLevel[i] = NULL;
					}
			case 0:
					uiAllocState = 3;
					break;
		}
	}
	return eError;
}

/*****************************************************************************
 *                   MMU page table functions                                *
 *****************************************************************************/

/*************************************************************************/ /*!
@Function       _MMU_GetLevelData

@Description    Get the all the level data and calculates the indexes for the
                specified address range

@Input          psMMUContext            MMU context to operate on

@Input          sDevVAddrStart          Start device virtual address

@Input          sDevVAddrEnd            End device virtual address

@Input          uiLog2DataPageSize      Log2 of the page size to use

@Input          auiStartArray           Array of start indexes (one for each level)

@Input          auiEndArray             Array of end indexes (one for each level)

@Input          uiEntriesPerPxArray     Array of number of entries for the Px
                                        (one for each level)

@Input          apsConfig               Array of PxE configs (one for each level)

@Input          aeMMULevel              Array of MMU levels (one for each level)

@Input          ppsMMUDevVAddrConfig    Device virtual address config

@Input			phPriv					Private data of page size config

@Return         IMG_TRUE if the last reference to psLevel was dropped
*/
/*****************************************************************************/
static void _MMU_GetLevelData(MMU_CONTEXT *psMMUContext,
									IMG_DEV_VIRTADDR sDevVAddrStart,
									IMG_DEV_VIRTADDR sDevVAddrEnd,
									IMG_UINT32 uiLog2DataPageSize,
									IMG_UINT32 auiStartArray[],
									IMG_UINT32 auiEndArray[],
									IMG_UINT32 auiEntriesPerPx[],
									const MMU_PxE_CONFIG *apsConfig[],
									MMU_LEVEL aeMMULevel[],
									const MMU_DEVVADDR_CONFIG **ppsMMUDevVAddrConfig,
									IMG_HANDLE *phPriv)
{
	const MMU_PxE_CONFIG *psMMUPDEConfig;
	const MMU_PxE_CONFIG *psMMUPTEConfig;
	const MMU_DEVVADDR_CONFIG *psDevVAddrConfig;
	MMU_DEVICEATTRIBS *psDevAttrs = psMMUContext->psDevAttrs;
	PVRSRV_ERROR eError;
	IMG_UINT32 i = 0;

	eError = psDevAttrs->pfnGetPageSizeConfiguration(uiLog2DataPageSize,
														&psMMUPDEConfig,
														&psMMUPTEConfig,
														ppsMMUDevVAddrConfig,
														phPriv);
	PVR_ASSERT(eError == PVRSRV_OK);
	
	psDevVAddrConfig = *ppsMMUDevVAddrConfig;

	if (psDevVAddrConfig->uiPCIndexMask != 0)
	{
		auiStartArray[i] = _CalcPCEIdx(sDevVAddrStart, psDevVAddrConfig, IMG_FALSE);
		auiEndArray[i] = _CalcPCEIdx(sDevVAddrEnd, psDevVAddrConfig, IMG_TRUE);
		auiEntriesPerPx[i] = psDevVAddrConfig->uiNumEntriesPC;
		apsConfig[i] = psDevAttrs->psBaseConfig;
		aeMMULevel[i] = MMU_LEVEL_3;
		i++;
	}

	if (psDevVAddrConfig->uiPDIndexMask != 0)
	{
		auiStartArray[i] = _CalcPDEIdx(sDevVAddrStart, psDevVAddrConfig, IMG_FALSE);
		auiEndArray[i] = _CalcPDEIdx(sDevVAddrEnd, psDevVAddrConfig, IMG_TRUE);
		auiEntriesPerPx[i] = psDevVAddrConfig->uiNumEntriesPD;
		if (i == 0)
		{
			apsConfig[i] = psDevAttrs->psBaseConfig;
		}
		else
		{
			apsConfig[i] = psMMUPDEConfig;
		}
		aeMMULevel[i] = MMU_LEVEL_2;
		i++;
	}

	/*
		There is always a PTE entry so we have a slightly different behaviour than above.
		E.g. for 2 MB RGX pages the uiPTIndexMask is 0x0000000000 but still there
		is a PT with one entry.

	*/
	auiStartArray[i] = _CalcPTEIdx(sDevVAddrStart, psDevVAddrConfig, IMG_FALSE);
	if (psDevVAddrConfig->uiPTIndexMask !=0)
	{
		auiEndArray[i] = _CalcPTEIdx(sDevVAddrEnd, psDevVAddrConfig, IMG_TRUE);
	}
	else
	{
		/*
			If the PTE mask is zero it means there is only 1 PTE and thus
			the start and end array are one in the same
		*/
		auiEndArray[i] = auiStartArray[i];
	}

	auiEntriesPerPx[i] = psDevVAddrConfig->uiNumEntriesPT;

	if (i == 0)
	{
		apsConfig[i] = psDevAttrs->psBaseConfig;
	}
	else
	{
		apsConfig[i] = psMMUPTEConfig;
	}
	aeMMULevel[i] = MMU_LEVEL_1;
}

static void _MMU_PutLevelData(MMU_CONTEXT *psMMUContext, IMG_HANDLE hPriv)
{
	MMU_DEVICEATTRIBS *psDevAttrs = psMMUContext->psDevAttrs;

	psDevAttrs->pfnPutPageSizeConfiguration(hPriv);
}

/*************************************************************************/ /*!
@Function       _AllocPageTables

@Description    Allocate page tables and any higher level MMU objects required
                for the specified virtual range

@Input          psMMUContext            MMU context to operate on

@Input          sDevVAddrStart          Start device virtual address

@Input          sDevVAddrEnd            End device virtual address

@Input          uiLog2DataPageSize      Page size of the data pages

@Return         PVRSRV_OK if the allocation was successful
*/
/*****************************************************************************/
static PVRSRV_ERROR
_AllocPageTables(MMU_CONTEXT *psMMUContext,
                 IMG_DEV_VIRTADDR sDevVAddrStart,
                 IMG_DEV_VIRTADDR sDevVAddrEnd,
                 IMG_UINT32 uiLog2DataPageSize)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 auiStartArray[MMU_MAX_LEVEL];
	IMG_UINT32 auiEndArray[MMU_MAX_LEVEL];
	IMG_UINT32 auiEntriesPerPx[MMU_MAX_LEVEL];
	MMU_LEVEL aeMMULevel[MMU_MAX_LEVEL];
	const MMU_PxE_CONFIG *apsConfig[MMU_MAX_LEVEL];
	const MMU_DEVVADDR_CONFIG	*psDevVAddrConfig;
	IMG_HANDLE hPriv;
	IMG_UINT32 ui32CurrentLevel = 0;


	PVR_DPF((PVR_DBG_ALLOC,
			 "_AllocPageTables: vaddr range: 0x%010llx:0x%010llx",
			 sDevVAddrStart.uiAddr,
			 sDevVAddrEnd.uiAddr
			 ));

#if defined(PDUMP)
	PDUMPCOMMENT("Allocating page tables for %llu bytes virtual range: 0x%010llX to 0x%010llX",
				(IMG_UINT64)sDevVAddrEnd.uiAddr - (IMG_UINT64)sDevVAddrStart.uiAddr,
                 (IMG_UINT64)sDevVAddrStart.uiAddr,
                 (IMG_UINT64)sDevVAddrEnd.uiAddr);
#endif

	_MMU_GetLevelData(psMMUContext, sDevVAddrStart, sDevVAddrEnd,
						(IMG_UINT32) uiLog2DataPageSize, auiStartArray, auiEndArray,
						auiEntriesPerPx, apsConfig, aeMMULevel,
						&psDevVAddrConfig, &hPriv);

	HTBLOGK(HTB_SF_MMU_PAGE_OP_ALLOC,
		HTBLOG_U64_BITS_HIGH(sDevVAddrStart.uiAddr), HTBLOG_U64_BITS_LOW(sDevVAddrStart.uiAddr),
		HTBLOG_U64_BITS_HIGH(sDevVAddrEnd.uiAddr), HTBLOG_U64_BITS_LOW(sDevVAddrEnd.uiAddr));

	eError = _MMU_AllocLevel(psMMUContext, &psMMUContext->sBaseLevelInfo,
								auiStartArray, auiEndArray, auiEntriesPerPx,
								apsConfig, aeMMULevel, &ui32CurrentLevel,
								auiStartArray[0], auiEndArray[0],
								IMG_TRUE, IMG_TRUE, uiLog2DataPageSize);

	_MMU_PutLevelData(psMMUContext, hPriv);

	return eError;
}

/*************************************************************************/ /*!
@Function       _FreePageTables

@Description    Free page tables and any higher level MMU objects at are no
                longer referenced for the specified virtual range.
                This will fill the temporary free list of the MMU context which
                needs cleanup after the call.

@Input          psMMUContext            MMU context to operate on

@Input          sDevVAddrStart          Start device virtual address

@Input          sDevVAddrEnd            End device virtual address

@Input          uiLog2DataPageSize      Page size of the data pages

@Return         None
*/
/*****************************************************************************/
static void _FreePageTables(MMU_CONTEXT *psMMUContext,
							IMG_DEV_VIRTADDR sDevVAddrStart,
							IMG_DEV_VIRTADDR sDevVAddrEnd,
							IMG_UINT32 uiLog2DataPageSize)
{
	IMG_UINT32 auiStartArray[MMU_MAX_LEVEL];
	IMG_UINT32 auiEndArray[MMU_MAX_LEVEL];
	IMG_UINT32 auiEntriesPerPx[MMU_MAX_LEVEL];
	MMU_LEVEL aeMMULevel[MMU_MAX_LEVEL];
	const MMU_PxE_CONFIG *apsConfig[MMU_MAX_LEVEL];
	const MMU_DEVVADDR_CONFIG	*psDevVAddrConfig;
	IMG_UINT32 ui32CurrentLevel = 0;
	IMG_HANDLE hPriv;


	PVR_DPF((PVR_DBG_ALLOC,
			 "_FreePageTables: vaddr range: 0x%010llx:0x%010llx",
			 sDevVAddrStart.uiAddr,
			 sDevVAddrEnd.uiAddr
			 ));

	_MMU_GetLevelData(psMMUContext, sDevVAddrStart, sDevVAddrEnd,
						uiLog2DataPageSize, auiStartArray, auiEndArray,
						auiEntriesPerPx, apsConfig, aeMMULevel,
						&psDevVAddrConfig, &hPriv);

	HTBLOGK(HTB_SF_MMU_PAGE_OP_FREE,
		HTBLOG_U64_BITS_HIGH(sDevVAddrStart.uiAddr), HTBLOG_U64_BITS_LOW(sDevVAddrStart.uiAddr),
		HTBLOG_U64_BITS_HIGH(sDevVAddrEnd.uiAddr), HTBLOG_U64_BITS_LOW(sDevVAddrEnd.uiAddr));

	_MMU_FreeLevel(psMMUContext, &psMMUContext->sBaseLevelInfo,
					auiStartArray, auiEndArray, auiEntriesPerPx,
					apsConfig, aeMMULevel, &ui32CurrentLevel,
					auiStartArray[0], auiEndArray[0],
					IMG_TRUE, IMG_TRUE, uiLog2DataPageSize);

	_MMU_PutLevelData(psMMUContext, hPriv);
}


/*************************************************************************/ /*!
@Function       _MMU_GetPTInfo

@Description    Get the PT level information and PT entry index for the specified
                virtual address

@Input          psMMUContext            MMU context to operate on

@Input          psDevVAddr              Device virtual address to get the PTE info
                                        from.

@Input          psDevVAddrConfig        The current virtual address config obtained
                                        by another function call before.

@Output         psLevel                 Level info of the PT

@Output         pui32PTEIndex           Index into the PT the address corresponds to

@Return         None
*/
/*****************************************************************************/
static INLINE void _MMU_GetPTInfo(MMU_CONTEXT                *psMMUContext,
								  IMG_DEV_VIRTADDR            sDevVAddr,
								  const MMU_DEVVADDR_CONFIG  *psDevVAddrConfig,
								  MMU_Levelx_INFO           **psLevel,
								  IMG_UINT32                 *pui32PTEIndex)
{
	MMU_Levelx_INFO *psLocalLevel = NULL;

	IMG_UINT32 uiPCEIndex;
	IMG_UINT32 uiPDEIndex;

	switch(psMMUContext->psDevAttrs->eTopLevel)
	{
		case MMU_LEVEL_3:
			/* find the page directory containing the PCE */
			uiPCEIndex = _CalcPCEIdx(sDevVAddr, psDevVAddrConfig, IMG_FALSE);
			psLocalLevel = psMMUContext->sBaseLevelInfo.apsNextLevel[uiPCEIndex];

		case MMU_LEVEL_2:
			/* find the page table containing the PDE */
			uiPDEIndex = _CalcPDEIdx(sDevVAddr, psDevVAddrConfig, IMG_FALSE);
			if (psLocalLevel != NULL)
			{
				psLocalLevel = psLocalLevel->apsNextLevel[uiPDEIndex];
			}
			else
			{
				psLocalLevel = psMMUContext->sBaseLevelInfo.apsNextLevel[uiPDEIndex];
			}

		case MMU_LEVEL_1:
			/* find PTE index into page table */
			*pui32PTEIndex = _CalcPTEIdx(sDevVAddr, psDevVAddrConfig, IMG_FALSE);
			if (psLocalLevel == NULL)
			{
				psLocalLevel = &psMMUContext->sBaseLevelInfo;
			}
			break;

		default:
			PVR_DPF((PVR_DBG_ERROR, "_MMU_GetPTEInfo: Invalid MMU level"));
			return;
	}

	*psLevel = psLocalLevel;
}

/*************************************************************************/ /*!
@Function       _MMU_GetPTConfig

@Description    Get the level config. Call _MMU_PutPTConfig after use!

@Input          psMMUContext            MMU context to operate on

@Input          uiLog2DataPageSize      Log 2 of the page size

@Output         ppsConfig               Config of the PTE

@Output         phPriv                  Private data handle to be passed back
                                        when the info is put

@Output         ppsDevVAddrConfig       Config of the device virtual addresses

@Return         None
*/
/*****************************************************************************/
static INLINE void _MMU_GetPTConfig(MMU_CONTEXT               *psMMUContext,
									IMG_UINT32                  uiLog2DataPageSize,
									const MMU_PxE_CONFIG      **ppsConfig,
									IMG_HANDLE                 *phPriv,
									const MMU_DEVVADDR_CONFIG **ppsDevVAddrConfig)
{
	MMU_DEVICEATTRIBS *psDevAttrs = psMMUContext->psDevAttrs;
	const MMU_DEVVADDR_CONFIG *psDevVAddrConfig;
	const MMU_PxE_CONFIG *psPDEConfig;
	const MMU_PxE_CONFIG *psPTEConfig;

	if (psDevAttrs->pfnGetPageSizeConfiguration(uiLog2DataPageSize,
	                                            &psPDEConfig,
	                                            &psPTEConfig,
	                                            &psDevVAddrConfig,
	                                            phPriv) != PVRSRV_OK)
	{
		/*
		   There should be no way we got here unless uiLog2DataPageSize
		   has changed after the MMU_Alloc call (in which case it's a bug in
		   the MM code)
		*/
		PVR_DPF((PVR_DBG_ERROR, "_MMU_GetPTConfig: Could not get valid page size config"));
		PVR_ASSERT(0);
	}

	*ppsConfig = psPTEConfig;
	*ppsDevVAddrConfig = psDevVAddrConfig;
}

/*************************************************************************/ /*!
@Function       _MMU_PutPTConfig

@Description    Put the level info. Has to be called after _MMU_GetPTConfig to
                ensure correct refcounting.

@Input          psMMUContext            MMU context to operate on

@Input          phPriv                  Private data handle created by
                                        _MMU_GetPTConfig.

@Return         None
*/
/*****************************************************************************/
static INLINE void _MMU_PutPTConfig(MMU_CONTEXT *psMMUContext,
                                 IMG_HANDLE hPriv)
{
	MMU_DEVICEATTRIBS *psDevAttrs = psMMUContext->psDevAttrs;

	if( psDevAttrs->pfnPutPageSizeConfiguration(hPriv) != PVRSRV_OK )
	{
		PVR_DPF((PVR_DBG_ERROR, "_MMU_GetPTConfig: Could not put page size config"));
		PVR_ASSERT(0);
	}

}


/*****************************************************************************
 *                     Public interface functions                            *
 *****************************************************************************/

/*
	MMU_ContextCreate
*/
PVRSRV_ERROR
MMU_ContextCreate(PVRSRV_DEVICE_NODE *psDevNode,
                  MMU_CONTEXT **ppsMMUContext,
                  MMU_DEVICEATTRIBS *psDevAttrs)
{
	MMU_CONTEXT *psMMUContext;
	const MMU_DEVVADDR_CONFIG *psDevVAddrConfig;
	const MMU_PxE_CONFIG *psConfig;
	MMU_PHYSMEM_CONTEXT *psCtx;
	IMG_UINT32 ui32BaseObjects;
	IMG_UINT32 ui32Size;
	IMG_CHAR sBuf[40];
	PVRSRV_ERROR eError = PVRSRV_OK;

	psConfig = psDevAttrs->psBaseConfig;
	psDevVAddrConfig = psDevAttrs->psTopLevelDevVAddrConfig;

	switch(psDevAttrs->eTopLevel)
	{
		case MMU_LEVEL_3:
			ui32BaseObjects = psDevVAddrConfig->uiNumEntriesPC;
			break;

		case MMU_LEVEL_2:
			ui32BaseObjects = psDevVAddrConfig->uiNumEntriesPD;
			break;

		case MMU_LEVEL_1:
			ui32BaseObjects = psDevVAddrConfig->uiNumEntriesPT;
			break;

		default:
			PVR_DPF((PVR_DBG_ERROR, "MMU_ContextCreate: Invalid MMU config"));
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			goto e0;
	}

	/* Allocate the MMU context with the Level 1 Px info's */
	ui32Size = sizeof(MMU_CONTEXT) + 
						((ui32BaseObjects - 1) * sizeof(MMU_Levelx_INFO *));

	psMMUContext = OSAllocZMem(ui32Size);
	if (psMMUContext == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_ContextCreate: ERROR call to OSAllocMem failed"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}

#if defined(PDUMP)
	/* Clear the refcount */
	psMMUContext->ui32PDumpContextIDRefCount = 0;
#endif
	/* Record Device specific attributes in the context for subsequent use */
	psMMUContext->psDevAttrs = psDevAttrs;
	psMMUContext->psDevNode = psDevNode;

#if defined(SUPPORT_GPUVIRT_VALIDATION)
{
	IMG_UINT32 ui32OSid, ui32OSidReg;
    IMG_BOOL bOSidAxiProt;

    RetrieveOSidsfromPidList(OSGetCurrentClientProcessIDKM(), &ui32OSid, &ui32OSidReg, &bOSidAxiProt);

    MMU_SetOSids(psMMUContext, ui32OSid, ui32OSidReg, bOSidAxiProt);
}
#endif

	/*
	  Allocate physmem context and set it up
	 */
	psCtx = OSAllocZMem(sizeof(MMU_PHYSMEM_CONTEXT));
	if (psCtx == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_ContextCreate: ERROR call to OSAllocMem failed"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e1;
	}
	psMMUContext->psPhysMemCtx = psCtx;

	psCtx->psDevNode = psDevNode;

	OSSNPrintf(sBuf, sizeof(sBuf)-1, "pgtables %p", psCtx);
	psCtx->uiPhysMemRANameAllocSize = OSStringLength(sBuf)+1;
	psCtx->pszPhysMemRAName = OSAllocMem(psCtx->uiPhysMemRANameAllocSize);
	if (psCtx->pszPhysMemRAName == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_ContextCreate: Out of memory"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e2;
	}

	OSStringCopy(psCtx->pszPhysMemRAName, sBuf);

	psCtx->psPhysMemRA = RA_Create(psCtx->pszPhysMemRAName,
									/* subsequent import */
									psDevNode->uiMMUPxLog2AllocGran,
									RA_LOCKCLASS_1,
									_MMU_PhysMem_RAImportAlloc,
									_MMU_PhysMem_RAImportFree,
									psCtx, /* priv */
									IMG_FALSE);
	if (psCtx->psPhysMemRA == NULL)
	{
		OSFreeMem(psCtx->pszPhysMemRAName);
		psCtx->pszPhysMemRAName = NULL;
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e3;
	}

	/* Setup cleanup meta data to check if a MMU context
	 * has been destroyed and should not be accessed anymore */
	psCtx->psCleanupData = OSAllocMem(sizeof(*(psCtx->psCleanupData)));
	if (psCtx->psCleanupData == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_ContextCreate: ERROR call to OSAllocMem failed"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e4;
	}

	OSLockCreate(&psCtx->psCleanupData->hCleanupLock, LOCK_TYPE_PASSIVE);
	psCtx->psCleanupData->bMMUContextExists = IMG_TRUE;
	dllist_init(&psCtx->psCleanupData->sMMUCtxCleanupItemsHead);
	psCtx->psCleanupData->uiRef = 1;

	/* allocate the base level object */
	/*
	   Note: Although this is not required by the this file until
	         the 1st allocation is made, a device specific callback
	         might request the base object address so we allocate
	         it up front.
	*/
	if (_PxMemAlloc(psMMUContext,
							ui32BaseObjects,
							psConfig,
							psDevAttrs->eTopLevel,
							&psMMUContext->sBaseLevelInfo.sMemDesc,
							psDevAttrs->ui32BaseAlign))
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_ContextCreate: Failed to alloc level 1 object"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e5;
	}

	dllist_init(&psMMUContext->psPhysMemCtx->sTmpMMUMappingHead);

	psMMUContext->sBaseLevelInfo.ui32NumOfEntries = ui32BaseObjects;
	psMMUContext->sBaseLevelInfo.ui32RefCount = 0;

	eError = OSLockCreate(&psMMUContext->hLock, LOCK_TYPE_PASSIVE);

	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_ContextCreate: Failed to create lock for MMU_CONTEXT"));
		goto e6;
	}

	/* return context */
	*ppsMMUContext = psMMUContext;

	return PVRSRV_OK;

e6:
	_PxMemFree(psMMUContext, &psMMUContext->sBaseLevelInfo.sMemDesc, psDevAttrs->eTopLevel);
e5:
	OSFreeMem(psCtx->psCleanupData);
e4:
	RA_Delete(psCtx->psPhysMemRA);
e3:
	OSFreeMem(psCtx->pszPhysMemRAName);
e2:
	OSFreeMem(psCtx);
e1:
	OSFreeMem(psMMUContext);
e0:
	return eError;
}

/*
	MMU_ContextDestroy
*/
void
MMU_ContextDestroy (MMU_CONTEXT *psMMUContext)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	PDLLIST_NODE psNode, psNextNode;

	PVRSRV_DEVICE_NODE *psDevNode = (PVRSRV_DEVICE_NODE *) psMMUContext->psDevNode;
	MMU_CTX_CLEANUP_DATA *psCleanupData = psMMUContext->psPhysMemCtx->psCleanupData;
	IMG_UINT32 uiRef;

	PVR_DPF ((PVR_DBG_MESSAGE, "MMU_ContextDestroy: Enter"));

	if (psPVRSRVData->eServicesState == PVRSRV_SERVICES_STATE_OK)
	{
		/* There should be no way to get here with live pages unless
		   there is a bug in this module or the MM code */
		PVR_ASSERT(psMMUContext->sBaseLevelInfo.ui32RefCount == 0);
	}

	OSLockAcquire(psMMUContext->hLock);

	/* Free the top level MMU object - will be put on defer free list.
	 * This has to be done before the step below that will empty the
	 * defer-free list. */
	_PxMemFree(psMMUContext,
	           &psMMUContext->sBaseLevelInfo.sMemDesc,
	           psMMUContext->psDevAttrs->eTopLevel);

	/* Empty the temporary defer-free list of Px */
	_FreeMMUMapping(psDevNode, &psMMUContext->psPhysMemCtx->sTmpMMUMappingHead);
	PVR_ASSERT(dllist_is_empty(&psMMUContext->psPhysMemCtx->sTmpMMUMappingHead));

	OSLockAcquire(psCleanupData->hCleanupLock);

	/* Empty the defer free list so the cleanup thread will
	 * not have to access any MMU context related structures anymore */
	dllist_foreach_node(&psCleanupData->sMMUCtxCleanupItemsHead,
	                    psNode,
	                    psNextNode)
	{
		MMU_CLEANUP_ITEM *psCleanup = IMG_CONTAINER_OF(psNode,
		                                               MMU_CLEANUP_ITEM,
		                                               sMMUCtxCleanupItem);

		_FreeMMUMapping(psDevNode, &psCleanup->sMMUMappingHead);

		dllist_remove_node(psNode);
	}
	PVR_ASSERT(dllist_is_empty(&psCleanupData->sMMUCtxCleanupItemsHead));

	psCleanupData->bMMUContextExists = IMG_FALSE;
	uiRef = --psCleanupData->uiRef;

	OSLockRelease(psCleanupData->hCleanupLock);

	if (uiRef == 0)
	{
		OSLockDestroy(psCleanupData->hCleanupLock);
		OSFreeMem(psCleanupData);
	}

	/* Free physmem context */
	RA_Delete(psMMUContext->psPhysMemCtx->psPhysMemRA);
	psMMUContext->psPhysMemCtx->psPhysMemRA = NULL;
	OSFreeMem(psMMUContext->psPhysMemCtx->pszPhysMemRAName);
	psMMUContext->psPhysMemCtx->pszPhysMemRAName = NULL;

	OSFreeMem(psMMUContext->psPhysMemCtx);

	OSLockRelease(psMMUContext->hLock);

#if defined(SUPPORT_GPUVIRT_VALIDATION)
	RemovePidOSidCoupling(OSGetCurrentClientProcessIDKM());
#endif

	OSLockDestroy(psMMUContext->hLock);

	/* free the context itself. */
	OSFreeMem(psMMUContext);
	/*not nulling pointer, copy on stack*/

	PVR_DPF ((PVR_DBG_MESSAGE, "MMU_ContextDestroy: Exit"));
}

/*
	MMU_Alloc
*/
PVRSRV_ERROR
MMU_Alloc (MMU_CONTEXT *psMMUContext,
		   IMG_DEVMEM_SIZE_T uSize,
		   IMG_DEVMEM_SIZE_T *puActualSize,
           IMG_UINT32 uiProtFlags,
		   IMG_DEVMEM_SIZE_T uDevVAddrAlignment,
		   IMG_DEV_VIRTADDR *psDevVAddr,
		   IMG_UINT32 uiLog2PageSize)
{
    PVRSRV_ERROR eError;
    IMG_DEV_VIRTADDR sDevVAddrEnd;


	const MMU_PxE_CONFIG *psPDEConfig;
	const MMU_PxE_CONFIG *psPTEConfig;
	const MMU_DEVVADDR_CONFIG *psDevVAddrConfig;

	MMU_DEVICEATTRIBS *psDevAttrs;
	IMG_HANDLE hPriv;
	
#if !defined (DEBUG)
	PVR_UNREFERENCED_PARAMETER(uDevVAddrAlignment);
#endif

	PVR_DPF ((PVR_DBG_MESSAGE, "MMU_Alloc: uSize=0x%010llx, uiProtFlags=0x%x, align=0x%010llx", uSize, uiProtFlags, uDevVAddrAlignment));

	/* check params */
	if (!psMMUContext || !psDevVAddr || !puActualSize)
	{
		PVR_DPF((PVR_DBG_ERROR,"MMU_Alloc: invalid params"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevAttrs = psMMUContext->psDevAttrs;

	eError = psDevAttrs->pfnGetPageSizeConfiguration(uiLog2PageSize,
													&psPDEConfig,
													&psPTEConfig,
													&psDevVAddrConfig,
													&hPriv);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"MMU_Alloc: Failed to get config info (%d)", eError));
		return eError;
	}

	/* size and alignment must be datapage granular */
	if(((psDevVAddr->uiAddr & psDevVAddrConfig->uiPageOffsetMask) != 0)
	|| ((uSize & psDevVAddrConfig->uiPageOffsetMask) != 0))
	{
		PVR_DPF((PVR_DBG_ERROR,"MMU_Alloc: invalid address or size granularity"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	sDevVAddrEnd = *psDevVAddr;
	sDevVAddrEnd.uiAddr += uSize;

	OSLockAcquire(psMMUContext->hLock);
	eError = _AllocPageTables(psMMUContext, *psDevVAddr, sDevVAddrEnd, uiLog2PageSize);
	OSLockRelease(psMMUContext->hLock);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"MMU_Alloc: _DeferredAllocPagetables failed"));
        return PVRSRV_ERROR_MMU_FAILED_TO_ALLOCATE_PAGETABLES;
	}

	psDevAttrs->pfnPutPageSizeConfiguration(hPriv);

	return PVRSRV_OK;
}

/*
	MMU_Free
*/
void
MMU_Free (MMU_CONTEXT *psMMUContext,
          IMG_DEV_VIRTADDR sDevVAddr,
          IMG_DEVMEM_SIZE_T uiSize,
          IMG_UINT32 uiLog2DataPageSize)
{
	IMG_DEV_VIRTADDR sDevVAddrEnd;

	if (psMMUContext == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Free: invalid parameter"));
		return;
	}

	PVR_DPF((PVR_DBG_MESSAGE, "MMU_Free: Freeing DevVAddr 0x%010llX",
			 sDevVAddr.uiAddr));

	/* ensure the address range to free is inside the heap */
	sDevVAddrEnd = sDevVAddr;
	sDevVAddrEnd.uiAddr += uiSize;

	OSLockAcquire(psMMUContext->hLock);

	_FreePageTables(psMMUContext,
	                sDevVAddr,
	                sDevVAddrEnd,
	                uiLog2DataPageSize);

	_SetupCleanup_FreeMMUMapping(psMMUContext->psDevNode,
	                             psMMUContext->psPhysMemCtx);

	OSLockRelease(psMMUContext->hLock);

	return;

}

PVRSRV_ERROR
MMU_MapPages(MMU_CONTEXT *psMMUContext,
             PVRSRV_MEMALLOCFLAGS_T uiMappingFlags,
             IMG_DEV_VIRTADDR sDevVAddrBase,
             PMR *psPMR,
             IMG_UINT32 ui32PhysPgOffset,
             IMG_UINT32 ui32MapPageCount,
             IMG_UINT32 *paui32MapIndices,
             IMG_UINT32 uiLog2PageSize)
{
	PVRSRV_ERROR eError;
	IMG_HANDLE hPriv;

	MMU_Levelx_INFO *psLevel = NULL;

	MMU_Levelx_INFO *psPrevLevel = NULL;

	IMG_UINT32 uiPTEIndex = 0;
	IMG_UINT32 uiPageSize = (1 << uiLog2PageSize);
	IMG_UINT32 uiLoop = 0;
	IMG_UINT32 ui32MappedCount = 0;
	IMG_UINT32 uiPgOffset = 0;
	IMG_UINT32 uiFlushEnd = 0, uiFlushStart = 0;

	IMG_UINT64 uiProtFlags = 0;
	MMU_PROTFLAGS_T uiMMUProtFlags = 0;

	const MMU_PxE_CONFIG *psConfig;
	const MMU_DEVVADDR_CONFIG *psDevVAddrConfig;

	IMG_DEV_VIRTADDR sDevVAddr = sDevVAddrBase;

	IMG_DEV_PHYADDR asDevPAddr[PMR_MAX_TRANSLATION_STACK_ALLOC];
	IMG_BOOL abValid[PMR_MAX_TRANSLATION_STACK_ALLOC];
	IMG_DEV_PHYADDR *psDevPAddr;
	IMG_DEV_PHYADDR sDevPAddr;
	IMG_BOOL *pbValid;
	IMG_BOOL bValid;
	IMG_BOOL bDummyBacking = IMG_FALSE;
	IMG_BOOL bNeedBacking = IMG_FALSE;

#if defined(PDUMP)
	IMG_CHAR aszMemspaceName[PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH];
	IMG_CHAR aszSymbolicAddress[PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH];
	IMG_DEVMEM_OFFSET_T uiSymbolicAddrOffset;

	PDUMPCOMMENT("Wire up Page Table entries to point to the Data Pages (%lld bytes)",
	              (IMG_UINT64)(ui32MapPageCount * uiPageSize));
#endif /*PDUMP*/


	/* Validate the most essential parameters */
	if((NULL == psMMUContext) || (0 == sDevVAddrBase.uiAddr) || (NULL == psPMR))
	{
		PVR_DPF((PVR_DBG_ERROR,"%s: Invalid mapping parameter issued", __func__));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto e0;
	}

	/* Allocate memory for page-frame-numbers and validity states,
	   N.B. assert could be triggered by an illegal uiSizeBytes */
	if (ui32MapPageCount > PMR_MAX_TRANSLATION_STACK_ALLOC)
	{
		psDevPAddr = OSAllocMem(ui32MapPageCount * sizeof(IMG_DEV_PHYADDR));
		if (psDevPAddr == NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "Failed to allocate PMR device PFN list"));
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto e0;
		}

		pbValid = OSAllocMem(ui32MapPageCount * sizeof(IMG_BOOL));
		if (pbValid == NULL)
		{
			/* Should allocation fail, clean-up here before exit */
			PVR_DPF((PVR_DBG_ERROR, "Failed to allocate PMR device PFN state"));
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			OSFreeMem(psDevPAddr);
			goto e0;
		}
	}
	else
	{
		psDevPAddr = asDevPAddr;
		pbValid	= abValid;
	}

	/* Get the Device physical addresses of the pages we are trying to map
	 * In the case of non indexed mapping we can get all addresses at once */
	if(NULL == paui32MapIndices)
	{
		eError = PMR_DevPhysAddr(psPMR,
		                         uiLog2PageSize,
		                         ui32MapPageCount,
		                         (ui32PhysPgOffset << uiLog2PageSize),
		                         psDevPAddr,
		                         pbValid);
		if (eError != PVRSRV_OK)
		{
			goto e1;
		}
	}

	/*Get the Page table level configuration */
	_MMU_GetPTConfig(psMMUContext,
	                 (IMG_UINT32) uiLog2PageSize,
	                 &psConfig,
	                 &hPriv,
	                 &psDevVAddrConfig);

	_MMU_ConvertDevMemFlags(IMG_FALSE,
                                uiMappingFlags,
                                &uiMMUProtFlags,
                                psMMUContext);
	/* Callback to get device specific protection flags */
	if (psConfig->uiBytesPerEntry == 8)
	{
		uiProtFlags = psMMUContext->psDevAttrs->pfnDerivePTEProt8(uiMMUProtFlags , uiLog2PageSize);
	}
	else if (psConfig->uiBytesPerEntry == 4)
	{
		uiProtFlags = psMMUContext->psDevAttrs->pfnDerivePTEProt4(uiMMUProtFlags);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,"%s: The page table entry byte length is not supported", __func__));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto e2;
	}

	if (PMR_IsSparse(psPMR))
	{
		/* We know there will not be 4G number of PMR's */
		bDummyBacking = PVRSRV_IS_SPARSE_DUMMY_BACKING_REQUIRED(uiMappingFlags);
	}

	OSLockAcquire(psMMUContext->hLock);

	for(uiLoop = 0; uiLoop < ui32MapPageCount; uiLoop++)
	{

#if defined(PDUMP)
		IMG_DEVMEM_OFFSET_T uiNextSymName;
#endif /*PDUMP*/

		if(NULL != paui32MapIndices)
		{
			uiPgOffset = paui32MapIndices[uiLoop];

			/*Calculate the Device Virtual Address of the page */
			sDevVAddr.uiAddr = sDevVAddrBase.uiAddr + (uiPgOffset * uiPageSize);
			/* Get the physical address to map */
			eError = PMR_DevPhysAddr(psPMR,
			                         uiLog2PageSize,
			                         1,
			                         uiPgOffset * uiPageSize,
			                         &sDevPAddr,
			                         &bValid);
			if (eError != PVRSRV_OK)
			{
				goto e3;
			}
		}
		else
		{
			uiPgOffset = uiLoop + ui32PhysPgOffset;
			sDevPAddr = psDevPAddr[uiLoop];
			bValid = pbValid[uiLoop];
		}

		/*
			The default value of the entry is invalid so we don't need to mark
			it as such if the page wasn't valid, we just advance pass that address
		*/
		if (bValid || bDummyBacking)
		{

			if(!bValid)
			{
				sDevPAddr.uiAddr = psMMUContext->psDevNode->sDummyPage.ui64DummyPgPhysAddr;
			}
			else
			{
				/* check the physical alignment of the memory to map */
				PVR_ASSERT((sDevPAddr.uiAddr & (uiPageSize-1)) == 0);
			}

#if defined(DEBUG)
{
			IMG_INT32	i32FeatureVal = 0;
			IMG_UINT32 ui32BitLength = FloorLog2(sDevPAddr.uiAddr);

			i32FeatureVal = psMMUContext->psDevNode->pfnGetDeviceFeatureValue(psMMUContext->psDevNode, \
														RGX_FEATURE_PHYS_BUS_WIDTH_BIT_MASK);
			do {
				/* i32FeatureVal can be negative for cases where this feature is undefined
				 * In that situation we need to bail out than go ahead with debug comparison */
				if(0 > i32FeatureVal)
					break;

				if (ui32BitLength > i32FeatureVal )
				{
					PVR_DPF((PVR_DBG_ERROR,"_MMU_MapPage Failed. The physical address bitlength (%d) "
							 "is greater than what the chip can handle (%d).",
							 ui32BitLength, i32FeatureVal));

					PVR_ASSERT(ui32BitLength <= i32FeatureVal );
					eError = PVRSRV_ERROR_INVALID_PARAMS;
					goto e3;
				}
			}while(0);
}
#endif /*DEBUG*/

#if defined(PDUMP)
			if(bValid)
			{
				eError = PMR_PDumpSymbolicAddr(psPMR, uiPgOffset * uiPageSize,
											   sizeof(aszMemspaceName), &aszMemspaceName[0],
											   sizeof(aszSymbolicAddress), &aszSymbolicAddress[0],
											   &uiSymbolicAddrOffset,
											   &uiNextSymName);
				PVR_ASSERT(eError == PVRSRV_OK);
			}
#endif /*PDUMP*/

			psPrevLevel = psLevel;
			/* Calculate PT index and get new table descriptor */
			_MMU_GetPTInfo(psMMUContext, sDevVAddr, psDevVAddrConfig,
						   &psLevel, &uiPTEIndex);

			if (psPrevLevel == psLevel)
			{
				uiFlushEnd = uiPTEIndex;
			}
			else
			{
				/* Flush if we moved to another psLevel, i.e. page table */
				if (psPrevLevel != NULL)
				{
					eError = psMMUContext->psDevNode->pfnDevPxClean(psMMUContext->psDevNode,
					                                                &psPrevLevel->sMemDesc.psMapping->sMemHandle,
					                                                uiFlushStart * psConfig->uiBytesPerEntry + psPrevLevel->sMemDesc.uiOffset,
					                                                (uiFlushEnd+1 - uiFlushStart) * psConfig->uiBytesPerEntry);
					if (eError != PVRSRV_OK)
						goto e3;
				}

				uiFlushStart = uiPTEIndex;
				uiFlushEnd = uiFlushStart;
			}

			HTBLOGK(HTB_SF_MMU_PAGE_OP_MAP,
				HTBLOG_U64_BITS_HIGH(sDevVAddr.uiAddr), HTBLOG_U64_BITS_LOW(sDevVAddr.uiAddr),
				HTBLOG_U64_BITS_HIGH(sDevPAddr.uiAddr), HTBLOG_U64_BITS_LOW(sDevPAddr.uiAddr));

				/* Set the PT entry with the specified address and protection flags */
			eError = _SetupPTE(psMMUContext,
			                   psLevel,
			                   uiPTEIndex,
			                   psConfig,
			                   &sDevPAddr,
			                   IMG_FALSE,
#if defined(PDUMP)
			                   (bValid)?aszMemspaceName:(psMMUContext->psDevAttrs->pszMMUPxPDumpMemSpaceName),
			                   (bValid)?aszSymbolicAddress:DUMMY_PAGE,
			                   (bValid)?uiSymbolicAddrOffset:0,
#endif /*PDUMP*/
			                   uiProtFlags);


			if(eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Mapping failed", __func__));
				goto e3;
			}

			if(bValid)
			{
				PVR_ASSERT(psLevel->ui32RefCount <= psLevel->ui32NumOfEntries);
				PVR_DPF ((PVR_DBG_MESSAGE,
						  "%s: devVAddr=%10llX, size=0x%x",
						  __func__,
						  sDevVAddr.uiAddr,
						  uiPgOffset * uiPageSize));

				ui32MappedCount++;
			}
		}

		sDevVAddr.uiAddr += uiPageSize;
	}

	/* Flush the last level we touched */
	if (psLevel != NULL)
	{
		eError = psMMUContext->psDevNode->pfnDevPxClean(psMMUContext->psDevNode,
		                                                &psLevel->sMemDesc.psMapping->sMemHandle,
		                                                uiFlushStart * psConfig->uiBytesPerEntry + psLevel->sMemDesc.uiOffset,
		                                                (uiFlushEnd+1 - uiFlushStart) * psConfig->uiBytesPerEntry);
		if (eError != PVRSRV_OK)
			goto e3;
	}

	OSLockRelease(psMMUContext->hLock);

	_MMU_PutPTConfig(psMMUContext, hPriv);

	if (psDevPAddr != asDevPAddr)
	{
		OSFreeMem(pbValid);
		OSFreeMem(psDevPAddr);
	}

	/* Flush TLB for PTs*/
	psMMUContext->psDevNode->pfnMMUCacheInvalidate(psMMUContext->psDevNode,
	                                               psMMUContext->hDevData,
	                                               MMU_LEVEL_1,
	                                               IMG_FALSE);

#if defined(PDUMP)
	PDUMPCOMMENT("Wired up %d Page Table entries (out of %d)", ui32MappedCount, ui32MapPageCount);
#endif /*PDUMP*/

	return PVRSRV_OK;

e3:
	OSLockRelease(psMMUContext->hLock);

	if(PMR_IsSparse(psPMR) && PVRSRV_IS_SPARSE_DUMMY_BACKING_REQUIRED(uiMappingFlags))
	{
		bNeedBacking = IMG_TRUE;
	}

	MMU_UnmapPages(psMMUContext,(bNeedBacking)?uiMappingFlags:0, sDevVAddrBase, uiLoop, paui32MapIndices, uiLog2PageSize, bNeedBacking);
e2:
	_MMU_PutPTConfig(psMMUContext, hPriv);
e1:
	if (psDevPAddr != asDevPAddr)
	{
		OSFreeMem(pbValid);
		OSFreeMem(psDevPAddr);
	}
e0:
	return eError;
}

/*
	MMU_UnmapPages
*/
void
MMU_UnmapPages (MMU_CONTEXT *psMMUContext,
				PVRSRV_MEMALLOCFLAGS_T uiMappingFlags,
                IMG_DEV_VIRTADDR sDevVAddrBase,
                IMG_UINT32 ui32PageCount,
                IMG_UINT32 *pai32FreeIndices,
                IMG_UINT32 uiLog2PageSize,
                IMG_BOOL bDummyBacking)
{
	IMG_UINT32 uiPTEIndex = 0, ui32Loop=0;
	IMG_UINT32 uiPageSize = 1 << uiLog2PageSize;
	IMG_UINT32 uiFlushEnd = 0, uiFlushStart = 0;
	MMU_Levelx_INFO *psLevel = NULL;
	MMU_Levelx_INFO *psPrevLevel = NULL;
	IMG_HANDLE hPriv;
	const MMU_PxE_CONFIG *psConfig;
	const MMU_DEVVADDR_CONFIG *psDevVAddrConfig;
	IMG_UINT64 uiProtFlags = 0;
	MMU_PROTFLAGS_T uiMMUProtFlags = 0;
	IMG_DEV_VIRTADDR sDevVAddr = sDevVAddrBase;
	IMG_DEV_PHYADDR sDummyPgDevPhysAddr;
	IMG_BOOL bUnmap = IMG_TRUE;

#if defined(PDUMP)
	PDUMPCOMMENT("Invalidate %d entries in page tables for virtual range: 0x%010llX to 0x%010llX",
	             ui32PageCount,
	             (IMG_UINT64)sDevVAddr.uiAddr,
	             ((IMG_UINT64)sDevVAddr.uiAddr) + (uiPageSize*ui32PageCount)-1);
#endif

	sDummyPgDevPhysAddr.uiAddr = psMMUContext->psDevNode->sDummyPage.ui64DummyPgPhysAddr;
	bUnmap = (bDummyBacking)?IMG_FALSE:IMG_TRUE;
	/* Get PT and address configs */
	_MMU_GetPTConfig(psMMUContext, (IMG_UINT32) uiLog2PageSize,
	                 &psConfig, &hPriv, &psDevVAddrConfig);

	_MMU_ConvertDevMemFlags(bUnmap,
								uiMappingFlags,
                                &uiMMUProtFlags,
                                psMMUContext);

	/* Callback to get device specific protection flags */
	if (psConfig->uiBytesPerEntry == 4)
	{
		uiProtFlags = psMMUContext->psDevAttrs->pfnDerivePTEProt4(uiMMUProtFlags);
	}
	else if (psConfig->uiBytesPerEntry == 8)
	{
		uiProtFlags = psMMUContext->psDevAttrs->pfnDerivePTEProt8(uiMMUProtFlags , uiLog2PageSize);
	}


	OSLockAcquire(psMMUContext->hLock);

	/* Unmap page by page */
	while (ui32Loop < ui32PageCount)
	{
		if(NULL != pai32FreeIndices)
		{
			/*Calculate the Device Virtual Address of the page */
			sDevVAddr.uiAddr = sDevVAddrBase.uiAddr +
										pai32FreeIndices[ui32Loop] * uiPageSize;
		}

		psPrevLevel = psLevel;
		/* Calculate PT index and get new table descriptor */
		_MMU_GetPTInfo(psMMUContext, sDevVAddr, psDevVAddrConfig,
					   &psLevel, &uiPTEIndex);

		if (psPrevLevel == psLevel)
		{
			uiFlushEnd = uiPTEIndex;
		}
		else
		{
			/* Flush if we moved to another psLevel, i.e. page table */
			if (psPrevLevel != NULL)
			{
				psMMUContext->psDevNode->pfnDevPxClean(psMMUContext->psDevNode,
				                                       &psPrevLevel->sMemDesc.psMapping->sMemHandle,
				                                       uiFlushStart * psConfig->uiBytesPerEntry + psPrevLevel->sMemDesc.uiOffset,
				                                       (uiFlushEnd+1 - uiFlushStart) * psConfig->uiBytesPerEntry);
			}

			uiFlushStart = uiPTEIndex;
			uiFlushEnd = uiFlushStart;
		}

		HTBLOGK(HTB_SF_MMU_PAGE_OP_UNMAP,
			HTBLOG_U64_BITS_HIGH(sDevVAddr.uiAddr), HTBLOG_U64_BITS_LOW(sDevVAddr.uiAddr));

		/* Set the PT entry to invalid and poison it with a bad address */
		if (_SetupPTE(psMMUContext,
		              psLevel,
		              uiPTEIndex,
		              psConfig,
		              (bDummyBacking)?&sDummyPgDevPhysAddr:&gsBadDevPhyAddr,
		              bUnmap,
#if defined(PDUMP)
		              (bDummyBacking)?(psMMUContext->psDevAttrs->pszMMUPxPDumpMemSpaceName):NULL,
		              (bDummyBacking)?DUMMY_PAGE:NULL,
		              0U,
#endif
		              uiProtFlags) != PVRSRV_OK )
		{
			goto e0;
		}

		/* Check we haven't wrapped around */
		PVR_ASSERT(psLevel->ui32RefCount <= psLevel->ui32NumOfEntries);
		ui32Loop++;
		sDevVAddr.uiAddr += uiPageSize;
	}

	/* Flush the last level we touched */
	if (psLevel != NULL)
	{
		psMMUContext->psDevNode->pfnDevPxClean(psMMUContext->psDevNode,
		                                       &psLevel->sMemDesc.psMapping->sMemHandle,
		                                       uiFlushStart * psConfig->uiBytesPerEntry + psLevel->sMemDesc.uiOffset,
		                                       (uiFlushEnd+1 - uiFlushStart) * psConfig->uiBytesPerEntry);
	}

	OSLockRelease(psMMUContext->hLock);

	_MMU_PutPTConfig(psMMUContext, hPriv);

	/* Flush TLB for PTs*/
	psMMUContext->psDevNode->pfnMMUCacheInvalidate(psMMUContext->psDevNode,
	                                               psMMUContext->hDevData,
	                                               MMU_LEVEL_1,
	                                               IMG_TRUE);

	return;

e0:
	_MMU_PutPTConfig(psMMUContext, hPriv);
	PVR_DPF((PVR_DBG_ERROR, "MMU_UnmapPages: Failed to map/unmap page table"));
	PVR_ASSERT(0);
	OSLockRelease(psMMUContext->hLock);
	return;
}

PVRSRV_ERROR
MMU_MapPMRFast (MMU_CONTEXT *psMMUContext,
            IMG_DEV_VIRTADDR sDevVAddrBase,
            const PMR *psPMR,
            IMG_DEVMEM_SIZE_T uiSizeBytes,
            PVRSRV_MEMALLOCFLAGS_T uiMappingFlags,
            IMG_UINT32 uiLog2PageSize)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 uiCount, i;
	IMG_UINT32 uiPageSize = 1 << uiLog2PageSize;
	IMG_UINT32 uiPTEIndex = 0;
	IMG_UINT64 uiProtFlags;
	MMU_PROTFLAGS_T uiMMUProtFlags = 0;
	MMU_Levelx_INFO *psLevel = NULL;
	IMG_HANDLE hPriv;
	const MMU_PxE_CONFIG *psConfig;
	const MMU_DEVVADDR_CONFIG *psDevVAddrConfig;
	IMG_DEV_VIRTADDR sDevVAddr = sDevVAddrBase;
	IMG_DEV_PHYADDR asDevPAddr[PMR_MAX_TRANSLATION_STACK_ALLOC];
	IMG_BOOL abValid[PMR_MAX_TRANSLATION_STACK_ALLOC];
	IMG_DEV_PHYADDR *psDevPAddr;
	IMG_BOOL *pbValid;
	IMG_UINT32 uiFlushStart = 0;

#if defined(PDUMP)
	IMG_CHAR aszMemspaceName[PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH];
	IMG_CHAR aszSymbolicAddress[PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH];
	IMG_DEVMEM_OFFSET_T uiSymbolicAddrOffset;
	IMG_UINT32 ui32MappedCount = 0;
	PDUMPCOMMENT("Wire up Page Table entries to point to the Data Pages (%lld bytes)", uiSizeBytes);
#endif /*PDUMP*/

	/* We should verify the size and contiguity when supporting variable page size */

	PVR_ASSERT (psMMUContext != NULL);
	PVR_ASSERT (psPMR != NULL);


	/* Allocate memory for page-frame-numbers and validity states,
	   N.B. assert could be triggered by an illegal uiSizeBytes */
	uiCount = uiSizeBytes >> uiLog2PageSize;
	PVR_ASSERT((IMG_DEVMEM_OFFSET_T)uiCount << uiLog2PageSize == uiSizeBytes);
    if (uiCount > PMR_MAX_TRANSLATION_STACK_ALLOC)
    {
		psDevPAddr = OSAllocMem(uiCount * sizeof(IMG_DEV_PHYADDR));
		if (psDevPAddr == NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "Failed to allocate PMR device PFN list"));
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto e0;
		}

		pbValid = OSAllocMem(uiCount * sizeof(IMG_BOOL));
		if (pbValid == NULL)
		{
			/* Should allocation fail, clean-up here before exit */
			PVR_DPF((PVR_DBG_ERROR, "Failed to allocate PMR device PFN state"));
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			OSFreeMem(psDevPAddr);
			goto e0;
		}
    }
	else
	{
		psDevPAddr = asDevPAddr;
		pbValid	= abValid;
	}

	/* Get general PT and address configs */
	_MMU_GetPTConfig(psMMUContext, (IMG_UINT32) uiLog2PageSize,
	                 &psConfig, &hPriv, &psDevVAddrConfig);

	_MMU_ConvertDevMemFlags(IMG_FALSE,
	                        uiMappingFlags,
	                        &uiMMUProtFlags,
	                        psMMUContext);

	/* Callback to get device specific protection flags */

	if (psConfig->uiBytesPerEntry == 8)
	{
		uiProtFlags = psMMUContext->psDevAttrs->pfnDerivePTEProt8(uiMMUProtFlags , uiLog2PageSize);
	}
	else if (psConfig->uiBytesPerEntry == 4)
	{
		uiProtFlags = psMMUContext->psDevAttrs->pfnDerivePTEProt4(uiMMUProtFlags);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,"%s: The page table entry byte length is not supported", __func__));
		eError = PVRSRV_ERROR_MMU_CONFIG_IS_WRONG;
		goto e1;
	}


	/* "uiSize" is the amount of contiguity in the underlying
	   page.  Normally this would be constant for the system, but,
	   that constant needs to be communicated, in case it's ever
	   different; caller guarantees that PMRLockSysPhysAddr() has
	   already been called */
	eError = PMR_DevPhysAddr(psPMR,
							 uiLog2PageSize,
							 uiCount,
							 0,
							 psDevPAddr,
							 pbValid);
	if (eError != PVRSRV_OK)
	{
		goto e1;
	}

	OSLockAcquire(psMMUContext->hLock);

	_MMU_GetPTInfo(psMMUContext, sDevVAddr, psDevVAddrConfig,
				   &psLevel, &uiPTEIndex);
	uiFlushStart = uiPTEIndex;

	/* Map in all pages of that PMR page by page*/
	for (i=0, uiCount=0; uiCount < uiSizeBytes; i++)
	{
#if defined(DEBUG)
{
	IMG_INT32	i32FeatureVal = 0;
	IMG_UINT32 ui32BitLength = FloorLog2(psDevPAddr[i].uiAddr);
	i32FeatureVal = psMMUContext->psDevNode->pfnGetDeviceFeatureValue(psMMUContext->psDevNode, \
			RGX_FEATURE_PHYS_BUS_WIDTH_BIT_MASK);
	do {
		if(0 > i32FeatureVal)
			break;

		if (ui32BitLength > i32FeatureVal )
		{
			PVR_DPF((PVR_DBG_ERROR,"_MMU_MapPage Failed. The physical address bitlength (%d) "
					"is greater than what the chip can handle (%d).",
					ui32BitLength, i32FeatureVal));

			PVR_ASSERT(ui32BitLength <= i32FeatureVal );
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			OSLockRelease(psMMUContext->hLock);
			goto e1;
		}
	}while(0);
}
#endif /*DEBUG*/
#if defined(PDUMP)
		{
			IMG_DEVMEM_OFFSET_T uiNextSymName;

			eError = PMR_PDumpSymbolicAddr(psPMR, uiCount,
										   sizeof(aszMemspaceName), &aszMemspaceName[0],
										   sizeof(aszSymbolicAddress), &aszSymbolicAddress[0],
										   &uiSymbolicAddrOffset,
										   &uiNextSymName);
			PVR_ASSERT(eError == PVRSRV_OK);
			ui32MappedCount++;
		}
#endif /*PDUMP*/

		HTBLOGK(HTB_SF_MMU_PAGE_OP_PMRMAP,
			HTBLOG_U64_BITS_HIGH(sDevVAddr.uiAddr), HTBLOG_U64_BITS_LOW(sDevVAddr.uiAddr),
			HTBLOG_U64_BITS_HIGH(psDevPAddr[i].uiAddr), HTBLOG_U64_BITS_LOW(psDevPAddr[i].uiAddr));

		/* Set the PT entry with the specified address and protection flags */
		eError = _SetupPTE(psMMUContext, psLevel, uiPTEIndex,
		                   psConfig, &psDevPAddr[i], IMG_FALSE,
#if defined(PDUMP)
		                   aszMemspaceName,
		                   aszSymbolicAddress,
		                   uiSymbolicAddrOffset,
#endif /*PDUMP*/
						   uiProtFlags);
		if (eError != PVRSRV_OK)
			goto e2;

		sDevVAddr.uiAddr += uiPageSize;
		uiCount += uiPageSize;

		/* Calculate PT index and get new table descriptor */
		if (uiPTEIndex < (psDevVAddrConfig->uiNumEntriesPT - 1) && (uiCount != uiSizeBytes))
		{
			uiPTEIndex++;
		}
		else
		{
			eError = psMMUContext->psDevNode->pfnDevPxClean(psMMUContext->psDevNode,
			                                                &psLevel->sMemDesc.psMapping->sMemHandle,
			                                                uiFlushStart * psConfig->uiBytesPerEntry + psLevel->sMemDesc.uiOffset,
			                                                (uiPTEIndex+1 - uiFlushStart) * psConfig->uiBytesPerEntry);
			if (eError != PVRSRV_OK)
				goto e2;


			_MMU_GetPTInfo(psMMUContext, sDevVAddr, psDevVAddrConfig,
						   &psLevel, &uiPTEIndex);
			uiFlushStart = uiPTEIndex;
		}
	}

	OSLockRelease(psMMUContext->hLock);


	_MMU_PutPTConfig(psMMUContext, hPriv);

	if (psDevPAddr != asDevPAddr)
	{
		OSFreeMem(pbValid);
		OSFreeMem(psDevPAddr);
	}

	/* Flush TLB for PTs*/
	psMMUContext->psDevNode->pfnMMUCacheInvalidate(psMMUContext->psDevNode,
	                                               psMMUContext->hDevData,
	                                               MMU_LEVEL_1,
	                                               IMG_FALSE);

#if defined(PDUMP)
	PDUMPCOMMENT("Wired up %d Page Table entries (out of %d)", ui32MappedCount, i);
#endif /*PDUMP*/

	return PVRSRV_OK;

e2:
	OSLockRelease(psMMUContext->hLock);
	MMU_UnmapPMRFast(psMMUContext,
	                 sDevVAddrBase,
	                 uiSizeBytes >> uiLog2PageSize,
	                 uiLog2PageSize);
e1:
	_MMU_PutPTConfig(psMMUContext, hPriv);

	if (psDevPAddr != asDevPAddr)
	{
		OSFreeMem(pbValid);
		OSFreeMem(psDevPAddr);
	}
e0:
	PVR_ASSERT(eError == PVRSRV_OK);
    return eError;
}

/*
    MMU_UnmapPages
*/
void
MMU_UnmapPMRFast(MMU_CONTEXT *psMMUContext,
                 IMG_DEV_VIRTADDR sDevVAddrBase,
                 IMG_UINT32 ui32PageCount,
                 IMG_UINT32 uiLog2PageSize)
{
	IMG_UINT32 uiPTEIndex = 0, ui32Loop=0;
	IMG_UINT32 uiPageSize = 1 << uiLog2PageSize;
	MMU_Levelx_INFO *psLevel = NULL;
	IMG_HANDLE hPriv;
	const MMU_PxE_CONFIG *psConfig;
	const MMU_DEVVADDR_CONFIG *psDevVAddrConfig;
	IMG_DEV_VIRTADDR sDevVAddr = sDevVAddrBase;
	IMG_UINT64 uiProtFlags = 0;
	MMU_PROTFLAGS_T uiMMUProtFlags = 0;
	IMG_UINT64 uiEntry = 0;
	IMG_UINT32 uiFlushStart = 0;

#if defined(PDUMP)
	PDUMPCOMMENT("Invalidate %d entries in page tables for virtual range: 0x%010llX to 0x%010llX",
				 ui32PageCount,
				 (IMG_UINT64)sDevVAddr.uiAddr,
				 ((IMG_UINT64)sDevVAddr.uiAddr) + (uiPageSize*ui32PageCount)-1);
#endif

	/* Get PT and address configs */
	_MMU_GetPTConfig(psMMUContext, (IMG_UINT32) uiLog2PageSize,
					 &psConfig, &hPriv, &psDevVAddrConfig);

	_MMU_ConvertDevMemFlags(IMG_TRUE,
							0,
							&uiMMUProtFlags,
							psMMUContext);

	/* Callback to get device specific protection flags */

	if (psConfig->uiBytesPerEntry == 8)
	{
		uiProtFlags = psMMUContext->psDevAttrs->pfnDerivePTEProt8(uiMMUProtFlags , uiLog2PageSize);

		/* Fill the entry with a bad address but leave space for protection flags */
		uiEntry = (gsBadDevPhyAddr.uiAddr & ~psConfig->uiProtMask) | uiProtFlags;
	}
	else if (psConfig->uiBytesPerEntry == 4)
	{
		uiProtFlags = psMMUContext->psDevAttrs->pfnDerivePTEProt4(uiMMUProtFlags);

		/* Fill the entry with a bad address but leave space for protection flags */
		uiEntry = (((IMG_UINT32) gsBadDevPhyAddr.uiAddr) & ~psConfig->uiProtMask) | (IMG_UINT32) uiProtFlags;
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,"%s: The page table entry byte length is not supported", __func__));
		goto e0;
	}

	OSLockAcquire(psMMUContext->hLock);

	_MMU_GetPTInfo(psMMUContext, sDevVAddr, psDevVAddrConfig,
				   &psLevel, &uiPTEIndex);
	uiFlushStart = uiPTEIndex;

	/* Unmap page by page and keep the loop as quick as possible.
	 * Only use parts of _SetupPTE that need to be executed. */
	while (ui32Loop < ui32PageCount)
	{

		/* Set the PT entry to invalid and poison it with a bad address */
		if (psConfig->uiBytesPerEntry == 8)
		{
			((IMG_UINT64*) psLevel->sMemDesc.pvCpuVAddr)[uiPTEIndex] = uiEntry;
		}
		else if (psConfig->uiBytesPerEntry == 4)
		{
			((IMG_UINT32*) psLevel->sMemDesc.pvCpuVAddr)[uiPTEIndex] = (IMG_UINT32) uiEntry;
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR,"%s: The page table entry byte length is not supported", __func__));
			goto e1;
		}

		/* Log modifications */
		HTBLOGK(HTB_SF_MMU_PAGE_OP_UNMAP,
			HTBLOG_U64_BITS_HIGH(sDevVAddr.uiAddr), HTBLOG_U64_BITS_LOW(sDevVAddr.uiAddr));

		HTBLOGK(HTB_SF_MMU_PAGE_OP_TABLE,
			HTBLOG_PTR_BITS_HIGH(psLevel), HTBLOG_PTR_BITS_LOW(psLevel),
			uiPTEIndex, MMU_LEVEL_1,
			HTBLOG_U64_BITS_HIGH(uiEntry), HTBLOG_U64_BITS_LOW(uiEntry),
			IMG_FALSE);

#if defined (PDUMP)
		PDumpMMUDumpPxEntries(MMU_LEVEL_1,
		                      psMMUContext->psDevAttrs->pszMMUPxPDumpMemSpaceName,
		                      psLevel->sMemDesc.pvCpuVAddr,
		                      psLevel->sMemDesc.sDevPAddr,
		                      uiPTEIndex,
		                      1,
		                      NULL,
		                      NULL,
		                      0,
		                      psConfig->uiBytesPerEntry,
		                      psConfig->uiAddrLog2Align,
		                      psConfig->uiAddrShift,
		                      psConfig->uiAddrMask,
		                      psConfig->uiProtMask,
		                      psConfig->uiValidEnMask,
		                      0,
		                      psMMUContext->psDevAttrs->eMMUType);
#endif /*PDUMP*/

		sDevVAddr.uiAddr += uiPageSize;
		ui32Loop++;

		/* Calculate PT index and get new table descriptor */
		if (uiPTEIndex < (psDevVAddrConfig->uiNumEntriesPT - 1) && (ui32Loop != ui32PageCount))
		{
			uiPTEIndex++;
		}
		else
		{
			psMMUContext->psDevNode->pfnDevPxClean(psMMUContext->psDevNode,
			                                       &psLevel->sMemDesc.psMapping->sMemHandle,
			                                       uiFlushStart * psConfig->uiBytesPerEntry + psLevel->sMemDesc.uiOffset,
			                                       (uiPTEIndex+1 - uiFlushStart) * psConfig->uiBytesPerEntry);

			_MMU_GetPTInfo(psMMUContext, sDevVAddr, psDevVAddrConfig,
						   &psLevel, &uiPTEIndex);
			uiFlushStart = uiPTEIndex;
		}
	}

	OSLockRelease(psMMUContext->hLock);

	_MMU_PutPTConfig(psMMUContext, hPriv);

	/* Flush TLB for PTs*/
	psMMUContext->psDevNode->pfnMMUCacheInvalidate(psMMUContext->psDevNode,
												   psMMUContext->hDevData,
												   MMU_LEVEL_1,
												   IMG_TRUE);

	return;

e1:
	OSLockRelease(psMMUContext->hLock);
	_MMU_PutPTConfig(psMMUContext, hPriv);
e0:
	PVR_DPF((PVR_DBG_ERROR, "MMU_UnmapPages: Failed to map/unmap page table"));
	PVR_ASSERT(0);
	return;
}

/*
	MMU_ChangeValidity
*/
PVRSRV_ERROR
MMU_ChangeValidity(MMU_CONTEXT *psMMUContext,
                   IMG_DEV_VIRTADDR sDevVAddr,
                   IMG_DEVMEM_SIZE_T uiNumPages,
                   IMG_UINT32 uiLog2PageSize,
                   IMG_BOOL bMakeValid,
                   PMR *psPMR)
{
    PVRSRV_ERROR eError = PVRSRV_OK;

	IMG_HANDLE hPriv;
	const MMU_DEVVADDR_CONFIG *psDevVAddrConfig;
	const MMU_PxE_CONFIG *psConfig;
	MMU_Levelx_INFO *psLevel = NULL;
	IMG_UINT32 uiFlushStart = 0;
	IMG_UINT32 uiPTIndex = 0;
	IMG_UINT32 i;
	IMG_UINT32 uiPageSize = 1 << uiLog2PageSize;
	IMG_BOOL bValid;

#if defined(PDUMP)
	IMG_CHAR aszMemspaceName[PHYSMEM_PDUMP_MEMSPACE_MAX_LENGTH];
	IMG_CHAR aszSymbolicAddress[PHYSMEM_PDUMP_SYMNAME_MAX_LENGTH];
	IMG_DEVMEM_OFFSET_T uiSymbolicAddrOffset;
	IMG_DEVMEM_OFFSET_T uiNextSymName;

	PDUMPCOMMENT("Change valid bit of the data pages to %d (0x%llX - 0x%llX)",
			bMakeValid,
			sDevVAddr.uiAddr,
			sDevVAddr.uiAddr + (uiNumPages<<uiLog2PageSize) - 1 );
#endif /*PDUMP*/

	/* We should verify the size and contiguity when supporting variable page size */
	PVR_ASSERT (psMMUContext != NULL);
	PVR_ASSERT (psPMR != NULL);

	/* Get general PT and address configs */
	_MMU_GetPTConfig(psMMUContext, (IMG_UINT32) uiLog2PageSize,
	                 &psConfig, &hPriv, &psDevVAddrConfig);

	_MMU_GetPTInfo(psMMUContext, sDevVAddr, psDevVAddrConfig,
					&psLevel, &uiPTIndex);
	uiFlushStart = uiPTIndex;

	/* Do a page table walk and change attribute for every page in range. */
	for (i=0; i < uiNumPages; )
	{

		/* Set the entry */
		if (bMakeValid == IMG_TRUE)
		{
			/* Only set valid if physical address exists (sparse allocs might have none)*/
			eError = PMR_IsOffsetValid(psPMR, uiLog2PageSize, 1, i<<uiLog2PageSize, &bValid);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "Cannot determine validity of page table entries page"));
				goto e_exit;
			}

			if (bValid)
			{
				if (psConfig->uiBytesPerEntry == 8)
				{
					((IMG_UINT64 *) psLevel->sMemDesc.pvCpuVAddr)[uiPTIndex] |= (psConfig->uiValidEnMask);
				}
				else if (psConfig->uiBytesPerEntry == 4)
				{
					((IMG_UINT32 *) psLevel->sMemDesc.pvCpuVAddr)[uiPTIndex] |= (psConfig->uiValidEnMask);
				}
				else
				{
					eError = PVRSRV_ERROR_MMU_CONFIG_IS_WRONG;
					PVR_DPF((PVR_DBG_ERROR, "Cannot change page table entries due to wrong configuration"));
					goto e_exit;
				}
			}
		}
		else
		{
			if (psConfig->uiBytesPerEntry == 8)
			{
				((IMG_UINT64 *) psLevel->sMemDesc.pvCpuVAddr)[uiPTIndex] &= ~(psConfig->uiValidEnMask);
			}
			else if (psConfig->uiBytesPerEntry == 4)
			{
				((IMG_UINT32 *) psLevel->sMemDesc.pvCpuVAddr)[uiPTIndex] &= ~(psConfig->uiValidEnMask);
			}
			else
			{
				eError = PVRSRV_ERROR_MMU_CONFIG_IS_WRONG;
				PVR_DPF((PVR_DBG_ERROR, "Cannot change page table entries due to wrong configuration"));
				goto e_exit;
			}
		}

#if defined(PDUMP)
		PMR_PDumpSymbolicAddr(psPMR, i<<uiLog2PageSize,
		                      sizeof(aszMemspaceName), &aszMemspaceName[0],
		                      sizeof(aszSymbolicAddress), &aszSymbolicAddress[0],
		                      &uiSymbolicAddrOffset,
		                      &uiNextSymName);

		PDumpMMUDumpPxEntries(MMU_LEVEL_1,
		                      psMMUContext->psDevAttrs->pszMMUPxPDumpMemSpaceName,
		                      psLevel->sMemDesc.pvCpuVAddr,
		                      psLevel->sMemDesc.sDevPAddr,
		                      uiPTIndex,
		                      1,
		                      aszMemspaceName,
		                      aszSymbolicAddress,
		                      uiSymbolicAddrOffset,
		                      psConfig->uiBytesPerEntry,
		                      psConfig->uiAddrLog2Align,
		                      psConfig->uiAddrShift,
		                      psConfig->uiAddrMask,
		                      psConfig->uiProtMask,
		                      psConfig->uiValidEnMask,
		                      0,
		                      psMMUContext->psDevAttrs->eMMUType);
#endif /*PDUMP*/

		sDevVAddr.uiAddr += uiPageSize;
		i++;

		/* Calculate PT index and get new table descriptor */
		if (uiPTIndex < (psDevVAddrConfig->uiNumEntriesPT - 1) && (i != uiNumPages))
		{
			uiPTIndex++;
		}
		else
		{

			eError = psMMUContext->psDevNode->pfnDevPxClean(psMMUContext->psDevNode,
			                                                &psLevel->sMemDesc.psMapping->sMemHandle,
			                                                uiFlushStart * psConfig->uiBytesPerEntry + psLevel->sMemDesc.uiOffset,
			                                                (uiPTIndex+1 - uiFlushStart) * psConfig->uiBytesPerEntry);
			if (eError != PVRSRV_OK)
				goto e_exit;

			_MMU_GetPTInfo(psMMUContext, sDevVAddr, psDevVAddrConfig,
						   &psLevel, &uiPTIndex);
			uiFlushStart = uiPTIndex;
		}
	}

e_exit:

	_MMU_PutPTConfig(psMMUContext, hPriv);

	/* Flush TLB for PTs*/
	psMMUContext->psDevNode->pfnMMUCacheInvalidate(psMMUContext->psDevNode,
	                                               psMMUContext->hDevData,
	                                               MMU_LEVEL_1,
	                                               !bMakeValid);

	PVR_ASSERT(eError == PVRSRV_OK);
    return eError;
}


/*
	MMU_AcquireBaseAddr
*/
PVRSRV_ERROR
MMU_AcquireBaseAddr(MMU_CONTEXT *psMMUContext, IMG_DEV_PHYADDR *psPhysAddr)
{
	if (!psMMUContext)
		return PVRSRV_ERROR_INVALID_PARAMS;

	*psPhysAddr = psMMUContext->sBaseLevelInfo.sMemDesc.sDevPAddr;
	return PVRSRV_OK;
}

/*
	MMU_ReleaseBaseAddr
*/
void
MMU_ReleaseBaseAddr(MMU_CONTEXT *psMMUContext)
{
	PVR_UNREFERENCED_PARAMETER(psMMUContext);
}

/*
	MMU_SetDeviceData
*/
void MMU_SetDeviceData(MMU_CONTEXT *psMMUContext, IMG_HANDLE hDevData)
{
	psMMUContext->hDevData = hDevData;
}

#if defined(SUPPORT_GPUVIRT_VALIDATION)
/*
    MMU_SetOSid, MMU_GetOSid
*/

void MMU_SetOSids(MMU_CONTEXT *psMMUContext, IMG_UINT32 ui32OSid, IMG_UINT32 ui32OSidReg, IMG_BOOL bOSidAxiProt)
{
    psMMUContext->ui32OSid     = ui32OSid;
    psMMUContext->ui32OSidReg  = ui32OSidReg;
    psMMUContext->bOSidAxiProt = bOSidAxiProt;

    return ;
}

void MMU_GetOSids(MMU_CONTEXT *psMMUContext, IMG_UINT32 *pui32OSid, IMG_UINT32 *pui32OSidReg, IMG_BOOL *pbOSidAxiProt)
{
    *pui32OSid     = psMMUContext->ui32OSid;
    *pui32OSidReg  = psMMUContext->ui32OSidReg;
    *pbOSidAxiProt = psMMUContext->bOSidAxiProt;

    return ;
}

#endif

/*
	MMU_CheckFaultAddress
*/
void MMU_CheckFaultAddress(MMU_CONTEXT *psMMUContext,
				IMG_DEV_VIRTADDR *psDevVAddr,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile)
{
	MMU_DEVICEATTRIBS *psDevAttrs = psMMUContext->psDevAttrs;
	const MMU_PxE_CONFIG *psConfig;
	const MMU_PxE_CONFIG *psMMUPDEConfig;
	const MMU_PxE_CONFIG *psMMUPTEConfig;
	const MMU_DEVVADDR_CONFIG *psMMUDevVAddrConfig;
	IMG_HANDLE hPriv;
	MMU_Levelx_INFO *psLevel = NULL;
	PVRSRV_ERROR eError;
	IMG_UINT64 uiIndex;
	IMG_UINT32 ui32PCIndex;
	IMG_UINT32 ui32PDIndex;
	IMG_UINT32 ui32PTIndex;
	IMG_UINT32 ui32Log2PageSize;

	OSLockAcquire(psMMUContext->hLock);

	/*
		At this point we don't know the page size so assume it's 4K.
		When we get the PD level (MMU_LEVEL_2) we can check to see
		if this assumption is correct.
	*/
	eError = psDevAttrs->pfnGetPageSizeConfiguration(12,
													 &psMMUPDEConfig,
													 &psMMUPTEConfig,
													 &psMMUDevVAddrConfig,
													 &hPriv);
	if (eError != PVRSRV_OK)
	{
		PVR_LOG(("Failed to get the page size info for log2 page sizeof 12"));
	}

	psLevel = &psMMUContext->sBaseLevelInfo;
	psConfig = psDevAttrs->psBaseConfig;

	switch(psMMUContext->psDevAttrs->eTopLevel)
	{
		case MMU_LEVEL_3:
			/* Determine the PC index */
			uiIndex = psDevVAddr->uiAddr & psDevAttrs->psTopLevelDevVAddrConfig->uiPCIndexMask;
			uiIndex = uiIndex >> psDevAttrs->psTopLevelDevVAddrConfig->uiPCIndexShift;
			ui32PCIndex = (IMG_UINT32) uiIndex;
			PVR_ASSERT(uiIndex == ((IMG_UINT64) ui32PCIndex));
			
			if (ui32PCIndex >= psLevel->ui32NumOfEntries)
			{
				PVR_DUMPDEBUG_LOG("PC index (%d) out of bounds (%d)", ui32PCIndex, psLevel->ui32NumOfEntries);
				break;
			}

			if (psConfig->uiBytesPerEntry == 4)
			{
				IMG_UINT32 *pui32Ptr = psLevel->sMemDesc.pvCpuVAddr;

				PVR_DUMPDEBUG_LOG("PCE for index %d = 0x%08x and %s be valid",
						 ui32PCIndex,
						 pui32Ptr[ui32PCIndex],
						 psLevel->apsNextLevel[ui32PCIndex]?"should":"should not");
			}
			else
			{
				IMG_UINT64 *pui64Ptr = psLevel->sMemDesc.pvCpuVAddr;

				PVR_DUMPDEBUG_LOG("PCE for index %d = 0x%016llx and %s be valid",
						 ui32PCIndex,
						 pui64Ptr[ui32PCIndex],
						 psLevel->apsNextLevel[ui32PCIndex]?"should":"should not");
			}

			psLevel = psLevel->apsNextLevel[ui32PCIndex];
			if (!psLevel)
			{
				break;
			}
			psConfig = psMMUPDEConfig;
			/* Fall through */

		case MMU_LEVEL_2:
			/* Determine the PD index */
			uiIndex = psDevVAddr->uiAddr & psDevAttrs->psTopLevelDevVAddrConfig->uiPDIndexMask;
			uiIndex = uiIndex >> psDevAttrs->psTopLevelDevVAddrConfig->uiPDIndexShift;
			ui32PDIndex = (IMG_UINT32) uiIndex;
			PVR_ASSERT(uiIndex == ((IMG_UINT64) ui32PDIndex));

			if (ui32PDIndex >= psLevel->ui32NumOfEntries)
			{
				PVR_DUMPDEBUG_LOG("PD index (%d) out of bounds (%d)", ui32PDIndex, psLevel->ui32NumOfEntries);
				break;
			}

			if (psConfig->uiBytesPerEntry == 4)
			{
				IMG_UINT32 *pui32Ptr = psLevel->sMemDesc.pvCpuVAddr;

				PVR_DUMPDEBUG_LOG("PDE for index %d = 0x%08x and %s be valid",
						 ui32PDIndex,
						 pui32Ptr[ui32PDIndex],
						 psLevel->apsNextLevel[ui32PDIndex]?"should":"should not");

				if (psDevAttrs->pfnGetPageSizeFromPDE4(pui32Ptr[ui32PDIndex], &ui32Log2PageSize) != PVRSRV_OK)
				{
					PVR_LOG(("Failed to get the page size from the PDE"));
				}
			}
			else
			{
				IMG_UINT64 *pui64Ptr = psLevel->sMemDesc.pvCpuVAddr;

				PVR_DUMPDEBUG_LOG("PDE for index %d = 0x%016llx and %s be valid",
						 ui32PDIndex,
						 pui64Ptr[ui32PDIndex],
						 psLevel->apsNextLevel[ui32PDIndex]?"should":"should not");

				if (psDevAttrs->pfnGetPageSizeFromPDE8(pui64Ptr[ui32PDIndex], &ui32Log2PageSize) != PVRSRV_OK)
				{
					PVR_LOG(("Failed to get the page size from the PDE"));
				}
			}

			/*
				We assumed the page size was 4K, now we have the actual size
				from the PDE we can confirm if our assumption was correct.
				Until now it hasn't mattered as the PC and PD are the same
				regardless of the page size
			*/
			if (ui32Log2PageSize != 12)
			{
				/* Put the 4K page size data */
				psDevAttrs->pfnPutPageSizeConfiguration(hPriv);

				/* Get the correct size data */
				eError = psDevAttrs->pfnGetPageSizeConfiguration(ui32Log2PageSize,
																 &psMMUPDEConfig,
																 &psMMUPTEConfig,
																 &psMMUDevVAddrConfig,
																 &hPriv);
				if (eError != PVRSRV_OK)
				{
					PVR_LOG(("Failed to get the page size info for log2 page sizeof %d", ui32Log2PageSize));
					break;
				}
			}
			psLevel = psLevel->apsNextLevel[ui32PDIndex];
			if (!psLevel)
			{
				break;
			}
			psConfig = psMMUPTEConfig;
			/* Fall through */

		case MMU_LEVEL_1:
			/* Determine the PT index */
			uiIndex = psDevVAddr->uiAddr & psMMUDevVAddrConfig->uiPTIndexMask;
			uiIndex = uiIndex >> psMMUDevVAddrConfig->uiPTIndexShift;
			ui32PTIndex = (IMG_UINT32) uiIndex;
			PVR_ASSERT(uiIndex == ((IMG_UINT64) ui32PTIndex));

			if (ui32PTIndex >= psLevel->ui32NumOfEntries)
			{
				PVR_DUMPDEBUG_LOG("PT index (%d) out of bounds (%d)", ui32PTIndex, psLevel->ui32NumOfEntries);
				break;
			}

			if (psConfig->uiBytesPerEntry == 4)
			{
				IMG_UINT32 *pui32Ptr = psLevel->sMemDesc.pvCpuVAddr;

				PVR_DUMPDEBUG_LOG("PTE for index %d = 0x%08x",
						 ui32PTIndex,
						 pui32Ptr[ui32PTIndex]);
			}
			else
			{
				IMG_UINT64 *pui64Ptr = psLevel->sMemDesc.pvCpuVAddr;

				PVR_DUMPDEBUG_LOG("PTE for index %d = 0x%016llx",
						 ui32PTIndex,
						 pui64Ptr[ui32PTIndex]);
			}

			break;
			default:
				PVR_LOG(("Unsupported MMU setup"));
				break;
	}

	OSLockRelease(psMMUContext->hLock);
}

IMG_BOOL MMU_IsVDevAddrValid(MMU_CONTEXT *psMMUContext,
                             IMG_UINT32 uiLog2PageSize,
                             IMG_DEV_VIRTADDR sDevVAddr)
{
    MMU_Levelx_INFO *psLevel = NULL;
    const MMU_PxE_CONFIG *psConfig;
    const MMU_DEVVADDR_CONFIG *psDevVAddrConfig;
    IMG_HANDLE hPriv;
    IMG_UINT32 uiIndex = 0;
    IMG_BOOL bStatus = IMG_FALSE;

    _MMU_GetPTConfig(psMMUContext, uiLog2PageSize, &psConfig, &hPriv, &psDevVAddrConfig);

    OSLockAcquire(psMMUContext->hLock);

    switch(psMMUContext->psDevAttrs->eTopLevel)
    {
        case MMU_LEVEL_3:
            uiIndex = _CalcPCEIdx(sDevVAddr, psDevVAddrConfig, IMG_FALSE);
            psLevel = psMMUContext->sBaseLevelInfo.apsNextLevel[uiIndex];
            if (psLevel == NULL)
                break;
            /* fall through */
        case MMU_LEVEL_2:
            uiIndex = _CalcPDEIdx(sDevVAddr, psDevVAddrConfig, IMG_FALSE);

            if (psLevel != NULL)
                psLevel = psLevel->apsNextLevel[uiIndex];
            else
                psLevel = psMMUContext->sBaseLevelInfo.apsNextLevel[uiIndex];

            if (psLevel == NULL)
                break;
            /* fall through */
        case MMU_LEVEL_1:
            uiIndex = _CalcPTEIdx(sDevVAddr, psDevVAddrConfig, IMG_FALSE);

            if (psLevel == NULL)
                psLevel = &psMMUContext->sBaseLevelInfo;

            bStatus = ((IMG_UINT64 *) psLevel->sMemDesc.pvCpuVAddr)[uiIndex]
                      & psConfig->uiValidEnMask;
            break;
        default:
            PVR_LOG(("MMU_IsVDevAddrValid: Unsupported MMU setup"));
            break;
    }

    OSLockRelease(psMMUContext->hLock);

    _MMU_PutPTConfig(psMMUContext, hPriv);

    return bStatus;
}

#if defined(PDUMP)
/*
	MMU_ContextDerivePCPDumpSymAddr
*/
PVRSRV_ERROR MMU_ContextDerivePCPDumpSymAddr(MMU_CONTEXT *psMMUContext,
                                             IMG_CHAR *pszPDumpSymbolicNameBuffer,
                                             size_t uiPDumpSymbolicNameBufferSize)
{
    size_t uiCount;
    IMG_UINT64 ui64PhysAddr;
	PVRSRV_DEVICE_IDENTIFIER *psDevId = &psMMUContext->psDevNode->sDevId;

    if (!psMMUContext->sBaseLevelInfo.sMemDesc.bValid)
    {
        /* We don't have any allocations.  You're not allowed to ask
           for the page catalogue base address until you've made at
           least one allocation */
        return PVRSRV_ERROR_MMU_API_PROTOCOL_ERROR;
    }

    ui64PhysAddr = (IMG_UINT64)psMMUContext->sBaseLevelInfo.sMemDesc.sDevPAddr.uiAddr;

    PVR_ASSERT(uiPDumpSymbolicNameBufferSize >= (IMG_UINT32)(21 + OSStringLength(psDevId->pszPDumpDevName)));

    /* Page table Symbolic Name is formed from page table phys addr
       prefixed with MMUPT_. */

    uiCount = OSSNPrintf(pszPDumpSymbolicNameBuffer,
                         uiPDumpSymbolicNameBufferSize,
                         ":%s:%s%016llX",
                         psDevId->pszPDumpDevName,
                         psMMUContext->sBaseLevelInfo.sMemDesc.bValid?"MMUPC_":"XXX",
                         ui64PhysAddr);

    if (uiCount + 1 > uiPDumpSymbolicNameBufferSize)
    {
        return PVRSRV_ERROR_INVALID_PARAMS;
    }

    return PVRSRV_OK;
}

/*
	MMU_PDumpWritePageCatBase
*/
PVRSRV_ERROR
MMU_PDumpWritePageCatBase(MMU_CONTEXT *psMMUContext,
                          const IMG_CHAR *pszSpaceName,
                          IMG_DEVMEM_OFFSET_T uiOffset,
                          IMG_UINT32 ui32WordSize,
                          IMG_UINT32 ui32AlignShift,
                          IMG_UINT32 ui32Shift,
                          PDUMP_FLAGS_T uiPdumpFlags)
{
	PVRSRV_ERROR eError;
	IMG_CHAR aszPageCatBaseSymbolicAddr[100];
	const IMG_CHAR *pszPDumpDevName = psMMUContext->psDevAttrs->pszMMUPxPDumpMemSpaceName;

	eError = MMU_ContextDerivePCPDumpSymAddr(psMMUContext,
                                             &aszPageCatBaseSymbolicAddr[0],
                                             sizeof(aszPageCatBaseSymbolicAddr));
	if (eError ==  PVRSRV_OK)
	{
		eError = PDumpWriteSymbAddress(pszSpaceName,
		                               uiOffset,
		                               aszPageCatBaseSymbolicAddr,
		                               0, /* offset -- Could be non-zero for var. pgsz */
		                               pszPDumpDevName,
		                               ui32WordSize,
		                               ui32AlignShift,
		                               ui32Shift,
		                               uiPdumpFlags | PDUMP_FLAGS_CONTINUOUS);
	}

    return eError;
}

/*
	MMU_AcquirePDumpMMUContext
*/
PVRSRV_ERROR MMU_AcquirePDumpMMUContext(MMU_CONTEXT *psMMUContext,
                                        IMG_UINT32 *pui32PDumpMMUContextID)
{
	PVRSRV_DEVICE_IDENTIFIER *psDevId = &psMMUContext->psDevNode->sDevId;

	if (!psMMUContext->ui32PDumpContextIDRefCount)
	{
		PDUMP_MMU_ALLOC_MMUCONTEXT(psDevId->pszPDumpDevName,
                                           psMMUContext->sBaseLevelInfo.sMemDesc.sDevPAddr,
                                           psMMUContext->psDevAttrs->eMMUType,
                                           &psMMUContext->uiPDumpContextID);
	}

	psMMUContext->ui32PDumpContextIDRefCount++;
	*pui32PDumpMMUContextID = psMMUContext->uiPDumpContextID;

	return PVRSRV_OK;
}

/*
	MMU_ReleasePDumpMMUContext
*/
PVRSRV_ERROR MMU_ReleasePDumpMMUContext(MMU_CONTEXT *psMMUContext)
{
	PVRSRV_DEVICE_IDENTIFIER *psDevId = &psMMUContext->psDevNode->sDevId;

	PVR_ASSERT(psMMUContext->ui32PDumpContextIDRefCount != 0);
	psMMUContext->ui32PDumpContextIDRefCount--;

	if (psMMUContext->ui32PDumpContextIDRefCount == 0)
	{
		PDUMP_MMU_FREE_MMUCONTEXT(psDevId->pszPDumpDevName,
									psMMUContext->uiPDumpContextID);
	}

	return PVRSRV_OK;
}
#endif

/******************************************************************************
 End of file (mmu_common.c)
******************************************************************************/


