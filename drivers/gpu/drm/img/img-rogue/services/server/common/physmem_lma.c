/*************************************************************************/ /*!
@File           physmem_lma.c
@Title          Local card memory allocator
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Part of the memory management. This module is responsible for
                implementing the function callbacks for local card memory.
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
#include "pvr_debug.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"
#include "rgx_pdump_panics.h"
#include "allocmem.h"
#include "osfunc.h"
#include "pvrsrv.h"
#include "devicemem_server_utils.h"
#include "physmem_lma.h"
#include "pdump_km.h"
#include "pmr.h"
#include "pmr_impl.h"
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#include "process_stats.h"
#endif

#if defined(SUPPORT_GPUVIRT_VALIDATION)
#include "rgxutils.h"
#endif

#if defined(INTEGRITY_OS)
#include "mm.h"
#include "integrity_memobject.h"
#endif

/* Assert that the conversions between the RA base type and the device
 * physical address are safe.
 */
static_assert(sizeof(IMG_DEV_PHYADDR) == sizeof(RA_BASE_T),
              "Size IMG_DEV_PHYADDR != RA_BASE_T");

/* Since 0x0 is a valid DevPAddr, we rely on max 64-bit value to be an invalid
 * page address */
#define INVALID_PAGE_ADDR ~((IMG_UINT64)0x0)
#define ZERO_PAGE_VALUE 0

typedef struct _PMR_KERNEL_MAP_HANDLE_ {
	void *vma;
	void *pvKernelAddress;
	/* uiSize has 2 uses:
	 * In Physically contiguous case it is used to track size of the mapping
	 * for free.
	 * In Physically sparse case it is used to determine free path to use, single page
	 * sparse mapping or multi page
	 */
	size_t uiSize;
} PMR_KERNEL_MAPPING;

typedef struct _PMR_LMALLOCARRAY_DATA_ {
	IMG_PID uiPid;

	/*
	 * N.B Chunks referenced in this struct commonly are
	 * to OS page sized. But in reality it is dependent on
	 * the uiLog2ChunkSize.
	 * Chunks will always be one 1 << uiLog2ChunkSize in size.
	 * */

	/*
	 * The number of chunks currently allocated in the PMR.
	 */
	IMG_INT32 iNumChunksAllocated;

	/*
	 * Total number of (Virtual) chunks supported by this PMR.
	 */
	IMG_UINT32 uiTotalNumChunks;

	/* The number of chunks to next be allocated for the PMR.
	 * This will initially be the number allocated at first alloc
	 * but may be changed in later calls to change sparse.
	 * It represents the number of chunks to next be allocated.
	 * This is used to store this value because we have the ability to
	 * defer allocation.
	 */
	IMG_UINT32 uiChunksToAlloc;

	/*
	 * Log2 representation of the chunksize.
	 */
	IMG_UINT32 uiLog2ChunkSize;

	IMG_BOOL bIsSparse; /* Is the PMR sparse */
	IMG_BOOL bPhysContig; /* Is the alloc Physically contiguous */

	IMG_BOOL bZeroOnAlloc;
	IMG_BOOL bPoisonOnAlloc;

	IMG_BOOL bOnDemand;

	/*
	  Record at alloc time whether poisoning will be required when the
	  PMR is freed.
	*/
	IMG_BOOL bPoisonOnFree;

	/* Physical heap and arena pointers for this allocation */
	PHYS_HEAP* psPhysHeap;
	RA_ARENA* psArena;
	PVRSRV_MEMALLOCFLAGS_T uiAllocFlags;

	/*
	   Connection data for this requests' originating process. NULL for
	   direct-bridge originating calls
	 */
	CONNECTION_DATA *psConnection;

	RA_BASE_ARRAY_T aBaseArray; /* Array of RA Bases */

} PMR_LMALLOCARRAY_DATA;

#if defined(DEBUG) && defined(SUPPORT_VALIDATION) && defined(__linux__)
/* Global structure to manage GPU memory leak */
static DEFINE_MUTEX(g_sLMALeakMutex);
static IMG_UINT32 g_ui32LMALeakCounter = 0;
#endif

typedef struct PHYSMEM_LMA_DATA_TAG {
	RA_ARENA           *psRA;
	IMG_CPU_PHYADDR    sStartAddr;
	IMG_DEV_PHYADDR    sCardBase;
	IMG_UINT64         uiSize;
} PHYSMEM_LMA_DATA;

/*
 * This function will set the psDevPAddr to whatever the system layer
 * has set it for the referenced heap.
 * It will not fail if the psDevPAddr is invalid.
 */
static PVRSRV_ERROR
_GetDevPAddr(PHEAP_IMPL_DATA pvImplData,
			 IMG_DEV_PHYADDR *psDevPAddr)
{
	PHYSMEM_LMA_DATA *psLMAData = (PHYSMEM_LMA_DATA*)pvImplData;

	*psDevPAddr = psLMAData->sCardBase;

	return PVRSRV_OK;
}

/*
 * This function will set the psCpuPAddr to whatever the system layer
 * has set it for the referenced heap.
 * It will not fail if the psCpuPAddr is invalid.
 */
static PVRSRV_ERROR
_GetCPUPAddr(PHEAP_IMPL_DATA pvImplData,
			 IMG_CPU_PHYADDR *psCpuPAddr)
{
	PHYSMEM_LMA_DATA *psLMAData = (PHYSMEM_LMA_DATA*)pvImplData;

	*psCpuPAddr = psLMAData->sStartAddr;

	return PVRSRV_OK;
}

static PVRSRV_ERROR
_GetSize(PHEAP_IMPL_DATA pvImplData,
		 IMG_UINT64 *puiSize)
{
	PHYSMEM_LMA_DATA *psLMAData = (PHYSMEM_LMA_DATA*)pvImplData;

	*puiSize = psLMAData->uiSize;

	return PVRSRV_OK;
}

static IMG_UINT32
_GetPageShift(void)
{
	return PVRSRV_4K_PAGE_SIZE_ALIGNSHIFT;
}

static void PhysmemGetLocalRamMemStats(PHEAP_IMPL_DATA pvImplData,
		 IMG_UINT64 *pui64TotalSize,
		 IMG_UINT64 *pui64FreeSize)
{
	PHYSMEM_LMA_DATA *psLMAData = (PHYSMEM_LMA_DATA*)pvImplData;
	RA_USAGE_STATS sRAUsageStats;

	RA_Get_Usage_Stats(psLMAData->psRA, &sRAUsageStats);

	*pui64TotalSize = sRAUsageStats.ui64TotalArenaSize;
	*pui64FreeSize = sRAUsageStats.ui64FreeArenaSize;
}

#if !defined(SUPPORT_GPUVIRT_VALIDATION)
static PVRSRV_ERROR
PhysmemGetArenaLMA(PHYS_HEAP *psPhysHeap,
				   RA_ARENA **ppsArena)
{
	PHYSMEM_LMA_DATA *psLMAData = (PHYSMEM_LMA_DATA*)PhysHeapGetImplData(psPhysHeap);

	PVR_LOG_RETURN_IF_FALSE(psLMAData != NULL, "psLMAData", PVRSRV_ERROR_NOT_IMPLEMENTED);

	*ppsArena = psLMAData->psRA;

	return PVRSRV_OK;
}
#endif

static PVRSRV_ERROR
_CreateArenas(PHEAP_IMPL_DATA pvImplData, IMG_CHAR *pszLabel, PHYS_HEAP_POLICY uiPolicy)
{
	PHYSMEM_LMA_DATA *psLMAData = (PHYSMEM_LMA_DATA*)pvImplData;

	IMG_UINT32 ui32RAPolicy =
	    ((uiPolicy & PHYS_HEAP_POLOCY_ALLOC_ALLOW_NONCONTIG_MASK) == PHYS_HEAP_POLICY_ALLOC_ALLOW_NONCONTIG)
	    ? RA_POLICY_ALLOC_ALLOW_NONCONTIG : RA_POLICY_DEFAULT;

	psLMAData->psRA = RA_Create_With_Span(pszLabel,
	                             OSGetPageShift(),
	                             psLMAData->sStartAddr.uiAddr,
	                             psLMAData->sCardBase.uiAddr,
	                             psLMAData->uiSize,
	                             ui32RAPolicy);
	PVR_LOG_RETURN_IF_NOMEM(psLMAData->psRA, "RA_Create_With_Span");

	return PVRSRV_OK;
}

static void
_DestroyArenas(PHEAP_IMPL_DATA pvImplData)
{
	PHYSMEM_LMA_DATA *psLMAData = (PHYSMEM_LMA_DATA*)pvImplData;

	/* Remove RAs and RA names for local card memory */
	if (psLMAData->psRA)
	{
		RA_Delete(psLMAData->psRA);
		psLMAData->psRA = NULL;
	}
}

static void
_DestroyImplData(PHEAP_IMPL_DATA pvImplData)
{
	PHYSMEM_LMA_DATA *psLMAData = (PHYSMEM_LMA_DATA*)pvImplData;

	_DestroyArenas(pvImplData);

	OSFreeMem(psLMAData);
}

struct _PHYS_HEAP_ITERATOR_ {
	PHYS_HEAP *psPhysHeap;
	RA_ARENA_ITERATOR *psRAIter;

	IMG_UINT64 uiTotalSize;
	IMG_UINT64 uiInUseSize;
};

PVRSRV_ERROR LMA_HeapIteratorCreate(PVRSRV_DEVICE_NODE *psDevNode,
                                    PVRSRV_PHYS_HEAP ePhysHeap,
                                    PHYS_HEAP_ITERATOR **ppsIter)
{
	PVRSRV_ERROR eError;
	PHYSMEM_LMA_DATA *psLMAData;
	PHYS_HEAP_ITERATOR *psHeapIter;
	PHYS_HEAP *psPhysHeap = NULL;
	RA_USAGE_STATS sStats;

	PVR_LOG_RETURN_IF_INVALID_PARAM(ppsIter != NULL, "ppsIter");
	PVR_LOG_RETURN_IF_INVALID_PARAM(psDevNode != NULL, "psDevNode");

	eError = PhysHeapAcquireByID(ePhysHeap, psDevNode, &psPhysHeap);
	PVR_LOG_RETURN_IF_ERROR(eError, "PhysHeapAcquireByID");

	PVR_LOG_GOTO_IF_FALSE(PhysHeapGetType(psPhysHeap) == PHYS_HEAP_TYPE_LMA,
	                      "PhysHeap must be of LMA type", release_heap);

	psLMAData = (PHYSMEM_LMA_DATA *) PhysHeapGetImplData(psPhysHeap);

	psHeapIter = OSAllocMem(sizeof(*psHeapIter));
	PVR_LOG_GOTO_IF_NOMEM(psHeapIter, eError, release_heap);

	psHeapIter->psPhysHeap = psPhysHeap;
	psHeapIter->psRAIter = RA_IteratorAcquire(psLMAData->psRA, IMG_FALSE);
	PVR_LOG_GOTO_IF_NOMEM(psHeapIter->psRAIter, eError, free_heap_iter);

	/* get heap usage */
	RA_Get_Usage_Stats(psLMAData->psRA, &sStats);

	psHeapIter->uiTotalSize = sStats.ui64TotalArenaSize;
	psHeapIter->uiInUseSize = sStats.ui64TotalArenaSize - sStats.ui64FreeArenaSize;

	*ppsIter = psHeapIter;

	return PVRSRV_OK;

free_heap_iter:
	OSFreeMem(psHeapIter);

release_heap:
	PhysHeapRelease(psPhysHeap);

	return eError;
}

void LMA_HeapIteratorDestroy(PHYS_HEAP_ITERATOR *psIter)
{
	PHYS_HEAP_ITERATOR *psHeapIter = psIter;

	PVR_LOG_RETURN_VOID_IF_FALSE(psHeapIter != NULL, "psHeapIter is NULL");

	PhysHeapRelease(psHeapIter->psPhysHeap);
	RA_IteratorRelease(psHeapIter->psRAIter);
	OSFreeMem(psHeapIter);
}

PVRSRV_ERROR LMA_HeapIteratorReset(PHYS_HEAP_ITERATOR *psIter)
{
	PHYS_HEAP_ITERATOR *psHeapIter = psIter;

	PVR_LOG_RETURN_IF_INVALID_PARAM(psHeapIter != NULL, "ppsIter");

	RA_IteratorReset(psHeapIter->psRAIter);

	return PVRSRV_OK;
}

IMG_BOOL LMA_HeapIteratorNext(PHYS_HEAP_ITERATOR *psIter,
                              IMG_DEV_PHYADDR *psDevPAddr,
                              IMG_UINT64 *puiSize)
{
	PHYS_HEAP_ITERATOR *psHeapIter = psIter;
	RA_ITERATOR_DATA sData = {0};

	if (psHeapIter == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "psHeapIter in %s() is NULL", __func__));
		return IMG_FALSE;
	}

	if (!RA_IteratorNext(psHeapIter->psRAIter, &sData))
	{
		return IMG_FALSE;
	}

	PVR_ASSERT(sData.uiSize != 0);

	psDevPAddr->uiAddr = sData.uiAddr;
	*puiSize = sData.uiSize;

	return IMG_TRUE;
}

PVRSRV_ERROR LMA_HeapIteratorGetHeapStats(PHYS_HEAP_ITERATOR *psIter,
                                          IMG_UINT64 *puiTotalSize,
                                          IMG_UINT64 *puiInUseSize)
{
	PHYS_HEAP_ITERATOR *psHeapIter = psIter;

	PVR_LOG_RETURN_IF_INVALID_PARAM(psHeapIter != NULL, "psHeapIter");

	*puiTotalSize = psHeapIter->uiTotalSize;
	*puiInUseSize = psHeapIter->uiInUseSize;

	return PVRSRV_OK;
}


static PVRSRV_ERROR
_LMA_DoPhyContigPagesAlloc(RA_ARENA *pArena,
                           size_t uiSize,
                           PG_HANDLE *psMemHandle,
                           IMG_DEV_PHYADDR *psDevPAddr,
                           IMG_PID uiPid)
{
	RA_BASE_T uiCardAddr = 0;
	RA_LENGTH_T uiActualSize;
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32Log2NumPages;

#if defined(DEBUG)
#if defined(RGX_NUM_DRIVERS_SUPPORTED) && (RGX_NUM_DRIVERS_SUPPORTED > 1)
	static IMG_UINT32	ui32MaxLog2NumPages = 7;	/* 128 pages => 512KB */
#else
	static IMG_UINT32	ui32MaxLog2NumPages = 4;	/*  16 pages =>  64KB */
#endif
#endif	/* defined(DEBUG) */

	PVR_ASSERT(uiSize != 0);
	ui32Log2NumPages = OSGetOrder(uiSize);
	uiSize = (1 << ui32Log2NumPages) * OSGetPageSize();

	eError = RA_Alloc(pArena,
	                  uiSize,
	                  RA_NO_IMPORT_MULTIPLIER,
	                  0,                         /* No flags */
	                  uiSize,
	                  "LMA_PhyContigPagesAlloc",
	                  &uiCardAddr,
	                  &uiActualSize,
	                  NULL);                     /* No private handle */

	if (eError != PVRSRV_OK)
	{
		RA_USAGE_STATS sRAStats;
		RA_Get_Usage_Stats(pArena, &sRAStats);

		PVR_DPF((PVR_DBG_ERROR,
				"Failed to Allocate size = 0x"IMG_SIZE_FMTSPECX", align = 0x"
				IMG_SIZE_FMTSPECX" Arena Free Space 0x%"IMG_UINT64_FMTSPECX,
				uiSize, uiSize,	sRAStats.ui64FreeArenaSize));
		return eError;
	}

	PVR_ASSERT(uiSize == uiActualSize);

	psMemHandle->u.ui64Handle = uiCardAddr;
	psDevPAddr->uiAddr = (IMG_UINT64) uiCardAddr;

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
	PVRSRVStatsIncrMemAllocStatAndTrack(PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA,
	                                    uiSize,
	                                    uiCardAddr,
	                                    uiPid);
#else
	{
		IMG_CPU_PHYADDR sCpuPAddr;
		sCpuPAddr.uiAddr = psDevPAddr->uiAddr;

		PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA,
									 NULL,
									 sCpuPAddr,
									 uiSize,
									 uiPid
									 DEBUG_MEMSTATS_VALUES);
	}
#endif
#endif
#if defined(SUPPORT_GPUVIRT_VALIDATION)
	PVR_DPF((PVR_DBG_MESSAGE,
	        "%s: (GPU Virtualisation) Allocated 0x" IMG_SIZE_FMTSPECX " at 0x%"
	        IMG_UINT64_FMTSPECX ", Arena ID %u",
	        __func__, uiSize, psDevPAddr->uiAddr, psMemHandle->uiOSid));
#endif

#if defined(DEBUG)
	PVR_ASSERT((ui32Log2NumPages <= ui32MaxLog2NumPages));
	if (ui32Log2NumPages > ui32MaxLog2NumPages)
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "%s: ui32MaxLog2NumPages = %u, increasing to %u", __func__,
		        ui32MaxLog2NumPages, ui32Log2NumPages ));
		ui32MaxLog2NumPages = ui32Log2NumPages;
	}
#endif	/* defined(DEBUG) */
	psMemHandle->uiOrder = ui32Log2NumPages;

	return eError;
}

#if defined(SUPPORT_GPUVIRT_VALIDATION)
static PVRSRV_ERROR
LMA_PhyContigPagesAllocGPV(PHYS_HEAP *psPhysHeap,
                           size_t uiSize,
                           PG_HANDLE *psMemHandle,
                           IMG_DEV_PHYADDR *psDevPAddr,
                           IMG_UINT32 ui32OSid,
                           IMG_PID uiPid)
{
	PVRSRV_DEVICE_NODE *psDevNode = PhysHeapDeviceNode(psPhysHeap);
	RA_ARENA *pArena;
	IMG_UINT32 ui32Log2NumPages = 0;
	PVRSRV_ERROR eError;

	PVR_ASSERT(uiSize != 0);
	ui32Log2NumPages = OSGetOrder(uiSize);
	uiSize = (1 << ui32Log2NumPages) * OSGetPageSize();

	PVR_ASSERT(ui32OSid < GPUVIRT_VALIDATION_NUM_OS);
	if (ui32OSid >= GPUVIRT_VALIDATION_NUM_OS)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid Arena index %u defaulting to 0",
		        __func__, ui32OSid));
		ui32OSid = 0;
	}

	pArena = psDevNode->psOSidSubArena[ui32OSid];

	if (psMemHandle->uiOSid != ui32OSid)
	{
		PVR_LOG(("%s: Unexpected OSid value %u - expecting %u", __func__,
		        psMemHandle->uiOSid, ui32OSid));
	}

	psMemHandle->uiOSid = ui32OSid;		/* For Free() use */

	eError =  _LMA_DoPhyContigPagesAlloc(pArena, uiSize, psMemHandle,
	                                     psDevPAddr, uiPid);
	PVR_LOG_IF_ERROR(eError, "_LMA_DoPhyContigPagesAlloc");

	return eError;
}
#endif

static PVRSRV_ERROR
LMA_PhyContigPagesAlloc(PHYS_HEAP *psPhysHeap,
                        size_t uiSize,
                        PG_HANDLE *psMemHandle,
                        IMG_DEV_PHYADDR *psDevPAddr,
                        IMG_PID uiPid)
{
#if defined(SUPPORT_GPUVIRT_VALIDATION)
	IMG_UINT32 ui32OSid = 0;
	return LMA_PhyContigPagesAllocGPV(psPhysHeap, uiSize, psMemHandle, psDevPAddr,
									  ui32OSid, uiPid);
#else
	PVRSRV_ERROR eError;

	RA_ARENA *pArena;
	IMG_UINT32 ui32Log2NumPages = 0;

	eError = PhysmemGetArenaLMA(psPhysHeap, &pArena);
	PVR_LOG_RETURN_IF_ERROR(eError, "PhysmemGetArenaLMA");

	PVR_ASSERT(uiSize != 0);
	ui32Log2NumPages = OSGetOrder(uiSize);
	uiSize = (1 << ui32Log2NumPages) * OSGetPageSize();

	eError = _LMA_DoPhyContigPagesAlloc(pArena, uiSize, psMemHandle,
	                                    psDevPAddr, uiPid);
	PVR_LOG_IF_ERROR(eError, "_LMA_DoPhyContigPagesAlloc");

	return eError;
#endif
}

static void
LMA_PhyContigPagesFree(PHYS_HEAP *psPhysHeap,
					   PG_HANDLE *psMemHandle)
{
	RA_BASE_T uiCardAddr = (RA_BASE_T) psMemHandle->u.ui64Handle;
	RA_ARENA	*pArena;

#if defined(SUPPORT_GPUVIRT_VALIDATION)
	PVRSRV_DEVICE_NODE *psDevNode = PhysHeapDeviceNode(psPhysHeap);
	IMG_UINT32	ui32OSid = psMemHandle->uiOSid;

	/*
	 * The Arena ID is set by the originating allocation, and maintained via
	 * the call stacks into this function. We have a limited range of IDs
	 * and if the passed value falls outside this we simply treat it as a
	 * 'global' arena ID of 0. This is where all default OS-specific allocations
	 * are created.
	 */
	PVR_ASSERT(ui32OSid < GPUVIRT_VALIDATION_NUM_OS);
	if (ui32OSid >= GPUVIRT_VALIDATION_NUM_OS)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid Arena index %u PhysAddr 0x%"
		         IMG_UINT64_FMTSPECx " Reverting to Arena 0", __func__,
		         ui32OSid, uiCardAddr));
		/*
		 * No way of determining what we're trying to free so default to the
		 * global default arena index 0.
		 */
		ui32OSid = 0;
	}

	pArena = psDevNode->psOSidSubArena[ui32OSid];

	PVR_DPF((PVR_DBG_MESSAGE, "%s: (GPU Virtualisation) Freeing 0x%"
	        IMG_UINT64_FMTSPECx ", Arena %u", __func__,
	        uiCardAddr, ui32OSid));

#else
	PhysmemGetArenaLMA(psPhysHeap, &pArena);
#endif

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
	PVRSRVStatsDecrMemAllocStatAndUntrack(PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA,
	                                      (IMG_UINT64)uiCardAddr);
#else
	PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA,
									(IMG_UINT64)uiCardAddr,
									OSGetCurrentClientProcessIDKM());
#endif
#endif

	RA_Free(pArena, uiCardAddr);
	psMemHandle->uiOrder = 0;
}

static PVRSRV_ERROR
LMA_PhyContigPagesMap(PHYS_HEAP *psPhysHeap,
                      PG_HANDLE *psMemHandle,
                      size_t uiSize, IMG_DEV_PHYADDR *psDevPAddr,
                      void **pvPtr)
{
	IMG_CPU_PHYADDR sCpuPAddr;
	IMG_UINT32 ui32NumPages = (1 << psMemHandle->uiOrder);
	PVR_UNREFERENCED_PARAMETER(uiSize);

	PhysHeapDevPAddrToCpuPAddr(psPhysHeap, 1, &sCpuPAddr, psDevPAddr);
	*pvPtr = OSMapPhysToLin(sCpuPAddr,
							ui32NumPages * OSGetPageSize(),
							PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC);
	PVR_RETURN_IF_NOMEM(*pvPtr);

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
	PVRSRVStatsIncrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA,
	                            ui32NumPages * OSGetPageSize(),
	                            OSGetCurrentClientProcessIDKM());
#else
	{
		PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA,
									 *pvPtr,
									 sCpuPAddr,
									 ui32NumPages * OSGetPageSize(),
									 OSGetCurrentClientProcessIDKM()
									 DEBUG_MEMSTATS_VALUES);
	}
#endif
#endif
	return PVRSRV_OK;
}

static void
LMA_PhyContigPagesUnmap(PHYS_HEAP *psPhysHeap,
                        PG_HANDLE *psMemHandle,
                        void *pvPtr)
{
	IMG_UINT32 ui32NumPages = (1 << psMemHandle->uiOrder);
	PVR_UNREFERENCED_PARAMETER(psPhysHeap);

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
	PVRSRVStatsDecrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA,
		                            ui32NumPages * OSGetPageSize(),
		                            OSGetCurrentClientProcessIDKM());
#else
	PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA,
	                                (IMG_UINT64)(uintptr_t)pvPtr,
	                                OSGetCurrentClientProcessIDKM());
#endif
#endif

	OSUnMapPhysToLin(pvPtr, ui32NumPages * OSGetPageSize());
}

static PVRSRV_ERROR
LMA_PhyContigPagesClean(PHYS_HEAP *psPhysHeap,
						PG_HANDLE *psMemHandle,
						IMG_UINT32 uiOffset,
						IMG_UINT32 uiLength)
{
	/* No need to flush because we map as uncached */
	PVR_UNREFERENCED_PARAMETER(psPhysHeap);
	PVR_UNREFERENCED_PARAMETER(psMemHandle);
	PVR_UNREFERENCED_PARAMETER(uiOffset);
	PVR_UNREFERENCED_PARAMETER(uiLength);

	return PVRSRV_OK;
}

static PHEAP_IMPL_FUNCS _sPHEAPImplFuncs =
{
	.pfnDestroyData = &_DestroyImplData,
	.pfnGetDevPAddr = &_GetDevPAddr,
	.pfnGetCPUPAddr = &_GetCPUPAddr,
	.pfnGetSize = &_GetSize,
	.pfnGetPageShift = &_GetPageShift,
	.pfnGetPMRFactoryMemStats = &PhysmemGetLocalRamMemStats,
	.pfnCreatePMR = &PhysmemNewLocalRamBackedPMR,
#if defined(SUPPORT_GPUVIRT_VALIDATION)
	.pfnPagesAllocGPV = &LMA_PhyContigPagesAllocGPV,
#endif
	.pfnPagesAlloc = &LMA_PhyContigPagesAlloc,
	.pfnPagesFree = &LMA_PhyContigPagesFree,
	.pfnPagesMap = &LMA_PhyContigPagesMap,
	.pfnPagesUnMap = &LMA_PhyContigPagesUnmap,
	.pfnPagesClean = &LMA_PhyContigPagesClean,
};

PVRSRV_ERROR
PhysmemCreateHeapLMA(PVRSRV_DEVICE_NODE *psDevNode,
                     PHYS_HEAP_POLICY uiPolicy,
                     PHYS_HEAP_CONFIG *psConfig,
                     IMG_CHAR *pszLabel,
                     PHYS_HEAP **ppsPhysHeap)
{
	PHYSMEM_LMA_DATA *psLMAData;
	PVRSRV_ERROR eError;

	PVR_LOG_RETURN_IF_INVALID_PARAM(pszLabel != NULL, "pszLabel");

	psLMAData = OSAllocMem(sizeof(*psLMAData));
	PVR_LOG_RETURN_IF_NOMEM(psLMAData, "OSAllocMem");

	psLMAData->sStartAddr = psConfig->sStartAddr;
	psLMAData->sCardBase = psConfig->sCardBase;
	psLMAData->uiSize = psConfig->uiSize;

	eError = PhysHeapCreate(psDevNode,
							psConfig,
							uiPolicy,
							(PHEAP_IMPL_DATA)psLMAData,
							&_sPHEAPImplFuncs,
							ppsPhysHeap);
	if (eError != PVRSRV_OK)
	{
		OSFreeMem(psLMAData);
		return eError;
	}

	eError = _CreateArenas(psLMAData, pszLabel, uiPolicy);
	PVR_LOG_RETURN_IF_ERROR(eError, "_CreateArenas");


	return eError;
}

static PVRSRV_ERROR _MapPhysicalContigAlloc(PHYS_HEAP *psPhysHeap,
                                            RA_BASE_ARRAY_T paBaseArray,
                                            size_t uiSize,
                                            PMR_FLAGS_T ulFlags,
                                            PMR_KERNEL_MAPPING *psMapping)
{
	IMG_UINT32 ui32CPUCacheFlags;
	PVRSRV_ERROR eError;
	IMG_CPU_PHYADDR sCpuPAddr;
	IMG_DEV_PHYADDR sDevPAddr;
	sDevPAddr.uiAddr = RA_BASE_STRIP_GHOST_BIT(*paBaseArray);

	eError = DevmemCPUCacheMode(PhysHeapDeviceNode(psPhysHeap), ulFlags, &ui32CPUCacheFlags);
	PVR_RETURN_IF_ERROR(eError);

	PhysHeapDevPAddrToCpuPAddr(psPhysHeap,
	                           1,
	                           &sCpuPAddr,
	                           &sDevPAddr);

	psMapping->pvKernelAddress = OSMapPhysToLin(sCpuPAddr, uiSize, ui32CPUCacheFlags);
	PVR_LOG_RETURN_IF_FALSE(psMapping->pvKernelAddress,
	                        "OSMapPhyToLin: out of VM Mem",
	                        PVRSRV_ERROR_PMR_NO_KERNEL_MAPPING);
	psMapping->vma = NULL;
	psMapping->uiSize = uiSize;

	return PVRSRV_OK;
}

static PVRSRV_ERROR _MapPhysicalSparseAlloc(PMR_LMALLOCARRAY_DATA *psLMAllocArrayData,
                                            RA_BASE_ARRAY_T paBaseArray,
                                            size_t uiSize,
                                            PMR_FLAGS_T ulFlags,
                                            PMR_KERNEL_MAPPING *psMapping)
{
	IMG_UINT32 uiChunkCount = uiSize >> psLMAllocArrayData->uiLog2ChunkSize;
	IMG_CPU_PHYADDR uiPages[PMR_MAX_TRANSLATION_STACK_ALLOC], *puiPages;
	PVRSRV_ERROR eError;
	size_t uiPageShift = OSGetPageShift();
	IMG_UINT32 uiOSPageCnt = psLMAllocArrayData->uiLog2ChunkSize - uiPageShift;

	if ((uiChunkCount << uiOSPageCnt) > PMR_MAX_TRANSLATION_STACK_ALLOC)
	{
		puiPages = OSAllocZMem(sizeof(IMG_CPU_PHYADDR) * (uiChunkCount << uiOSPageCnt));
		PVR_RETURN_IF_NOMEM(puiPages);
	}
	else
	{
		puiPages = &uiPages[0];
	}

	if (uiOSPageCnt == 0)
	{
		IMG_UINT32 i;
		PhysHeapDevPAddrToCpuPAddr(psLMAllocArrayData->psPhysHeap,
								   uiChunkCount,
								   puiPages,
								   (IMG_DEV_PHYADDR *)paBaseArray);

		/* If the ghost bit is present then the addrs returned will be off by 1
		 * Strip the ghost bit to correct to real page aligned addresses.
		 * */
		for (i = 0; i < uiChunkCount; i++)
		{
			puiPages[i].uiAddr = RA_BASE_STRIP_GHOST_BIT(puiPages[i].uiAddr);
		}
	}
	else
	{
		IMG_UINT32 i = 0, j = 0, index = 0;
		for (i = 0; i < uiChunkCount; i++)
		{
			IMG_UINT32 ui32OSPagesPerDeviceChunk = (1 << uiOSPageCnt);
			IMG_DEV_PHYADDR uiDevAddr;
			uiDevAddr.uiAddr = RA_BASE_STRIP_GHOST_BIT(paBaseArray[i]);
			for (j = 0; j < ui32OSPagesPerDeviceChunk; j++)
			{
				uiDevAddr.uiAddr += (1ULL << uiPageShift);
				PhysHeapDevPAddrToCpuPAddr(psLMAllocArrayData->psPhysHeap,
										   1,
										   &puiPages[index],
										   &uiDevAddr);
				index++;
			}
		}
	}

	eError = OSMapPhysArrayToLin(puiPages,
	                             uiChunkCount,
	                             &psMapping->pvKernelAddress,
	                             &psMapping->vma);
	if (eError == PVRSRV_OK)
	{
		psMapping->uiSize = uiSize;
	}

	if (puiPages != &uiPages[0])
	{
		OSFreeMem(puiPages);
	}

	return eError;
}

static PVRSRV_ERROR _MapPMRKernel(PMR_LMALLOCARRAY_DATA *psLMAllocArrayData,
                                  RA_BASE_ARRAY_T paBaseArray,
                                  size_t uiSize,
                                  PMR_FLAGS_T ulFlags,
                                  PMR_KERNEL_MAPPING *psMapping)
{
	PVRSRV_ERROR eError;
	PHYS_HEAP *psPhysHeap = psLMAllocArrayData->psPhysHeap;
	if (!psLMAllocArrayData->bIsSparse)
	{
		/* Physically Contig */
		if (psLMAllocArrayData->bPhysContig)
		{
			eError = _MapPhysicalContigAlloc(psPhysHeap,
			                                 paBaseArray,
			                                 uiSize,
			                                 ulFlags,
			                                 psMapping);
		}
		/* Physically Sparse */
		else
		{
			eError = _MapPhysicalSparseAlloc(psLMAllocArrayData,
			                                 paBaseArray,
			                                 uiSize,
			                                 ulFlags,
			                                 psMapping);
		}
	}
	else
	{
		/* Sparse Alloc Single Chunk */
		if (uiSize == (1 << psLMAllocArrayData->uiLog2ChunkSize))
		{
			eError = _MapPhysicalContigAlloc(psPhysHeap,
			                                 paBaseArray,
			                                 uiSize,
			                                 ulFlags,
			                                 psMapping);
		}
		/* Sparse Alloc Multi Chunk */
		else
		{
			eError = _MapPhysicalSparseAlloc(psLMAllocArrayData,
			                                 paBaseArray,
			                                 uiSize,
			                                 ulFlags,
			                                 psMapping);
		}
	}

	return eError;
}

static void _UnMapPhysicalContigAlloc(PMR_KERNEL_MAPPING *psKernelMapping)
{
	OSUnMapPhysToLin(psKernelMapping->pvKernelAddress, psKernelMapping->uiSize);
}

static void _UnMapPhysicalSparseAlloc(PMR_KERNEL_MAPPING *psKernelMapping)
{
	OSUnMapPhysArrayToLin(psKernelMapping->pvKernelAddress,
	                   psKernelMapping->vma);
}

static void _UnMapPMRKernel(PMR_LMALLOCARRAY_DATA *psLMAllocArrayData,
                            PMR_KERNEL_MAPPING *psKernelMapping)
{
	if (!psLMAllocArrayData->bIsSparse)
	{
		/* Physically Contig */
		if (psLMAllocArrayData->bPhysContig)
		{
			_UnMapPhysicalContigAlloc(psKernelMapping);
		}
		/* Physically Sparse */
		else
		{
			_UnMapPhysicalSparseAlloc(psKernelMapping);
		}
	}
	else
	{
		/* Sparse Alloc Single Chunk */
		if (psKernelMapping->uiSize == (1 << psLMAllocArrayData->uiLog2ChunkSize))
		{
			_UnMapPhysicalContigAlloc(psKernelMapping);
		}
		/* Sparse Alloc Multi Chunk */
		else
		{
			_UnMapPhysicalSparseAlloc(psKernelMapping);
		}
	}
}

static PVRSRV_ERROR
_PhysPgMemSet(PMR_LMALLOCARRAY_DATA *psLMAllocArrayData,
              RA_BASE_ARRAY_T paBaseArray,
              size_t uiSize,
              IMG_BYTE ui8SetValue)
{
	PVRSRV_ERROR eError;
	PMR_KERNEL_MAPPING sKernelMapping;

	eError = _MapPMRKernel(psLMAllocArrayData,
	                       paBaseArray,
	                       uiSize,
	                       PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC,
	                       &sKernelMapping);
	PVR_GOTO_IF_ERROR(eError, map_failed);

	OSCachedMemSetWMB(sKernelMapping.pvKernelAddress, ui8SetValue, uiSize);

	_UnMapPMRKernel(psLMAllocArrayData, &sKernelMapping);

	return PVRSRV_OK;

map_failed:
	PVR_DPF((PVR_DBG_ERROR, "Failed to poison/zero allocation"));
	return eError;
}

static PVRSRV_ERROR
_AllocLMPageArray(PMR_SIZE_T uiSize,
                  IMG_UINT32 ui32NumPhysChunks,
                  IMG_UINT32 ui32NumVirtChunks,
                  IMG_UINT32 uiLog2AllocPageSize,
                  IMG_BOOL bZero,
                  IMG_BOOL bPoisonOnAlloc,
                  IMG_BOOL bPoisonOnFree,
                  IMG_BOOL bIsSparse,
                  IMG_BOOL bOnDemand,
                  PHYS_HEAP* psPhysHeap,
                  PVRSRV_MEMALLOCFLAGS_T uiAllocFlags,
                  IMG_PID uiPid,
                  PMR_LMALLOCARRAY_DATA **ppsPageArrayDataPtr,
                  CONNECTION_DATA *psConnection)
{
	PMR_LMALLOCARRAY_DATA *psPageArrayData = NULL;
	PVRSRV_ERROR eError;
	IMG_UINT32 uiNumPages;

	PVR_ASSERT(!bZero || !bPoisonOnAlloc);
	PVR_ASSERT(OSGetPageShift() <= uiLog2AllocPageSize);

	/* Use of cast below is justified by the assertion that follows to
	prove that no significant bits have been truncated */
	uiNumPages = (IMG_UINT32)(((uiSize - 1) >> uiLog2AllocPageSize) + 1);
	PVR_ASSERT(((PMR_SIZE_T)uiNumPages << uiLog2AllocPageSize) == uiSize);

	psPageArrayData = OSAllocMem(sizeof(PMR_LMALLOCARRAY_DATA) + (sizeof(RA_BASE_T) * uiNumPages));
	PVR_GOTO_IF_NOMEM(psPageArrayData, eError, errorOnAllocArray);

	if (bIsSparse)
	{
		/* Since no pages are allocated yet, initialise page addresses to INVALID_PAGE_ADDR */
		OSCachedMemSet(psPageArrayData->aBaseArray,
					   0xFF,
					   sizeof(RA_BASE_T) *
					   uiNumPages);
	}
	else
	{
		/* Base pointers have been allocated for the full PMR in case we require a non
		 * physically contiguous backing for the virtually contiguous allocation but the most
		 * common case will be contiguous and so only require the first Base to be present
		 */
		psPageArrayData->aBaseArray[0] = INVALID_BASE_ADDR;
	}

	psPageArrayData->uiTotalNumChunks = uiNumPages;
	psPageArrayData->uiChunksToAlloc = bIsSparse ? ui32NumPhysChunks : uiNumPages;
	psPageArrayData->uiLog2ChunkSize = uiLog2AllocPageSize;

	psPageArrayData->psConnection = psConnection;
	psPageArrayData->uiPid = uiPid;
	psPageArrayData->iNumChunksAllocated = 0;
	psPageArrayData->bIsSparse = bIsSparse;
	psPageArrayData->bPhysContig = IMG_FALSE;
	psPageArrayData->bZeroOnAlloc = bZero;
	psPageArrayData->bPoisonOnAlloc = bPoisonOnAlloc;
	psPageArrayData->bPoisonOnFree = bPoisonOnFree;
	psPageArrayData->bOnDemand = bOnDemand;
	psPageArrayData->psPhysHeap = psPhysHeap;
	psPageArrayData->uiAllocFlags = uiAllocFlags;

	*ppsPageArrayDataPtr = psPageArrayData;

	return PVRSRV_OK;

/*
  error exit path follows:
*/

errorOnAllocArray:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

static PVRSRV_ERROR
_AllocLMPagesContig(PMR_LMALLOCARRAY_DATA *psPageArrayData)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 uiLog2ChunkSize = psPageArrayData->uiLog2ChunkSize;
	IMG_UINT64 uiPhysSize = (IMG_UINT64) psPageArrayData->uiChunksToAlloc << uiLog2ChunkSize;
	IMG_BOOL bPoisonOnAlloc = psPageArrayData->bPoisonOnAlloc;
	IMG_BOOL bZeroOnAlloc = psPageArrayData->bZeroOnAlloc;


	eError = RA_AllocMulti(psPageArrayData->psArena,
	                  uiPhysSize,
	                  uiLog2ChunkSize,
	                  RA_NO_IMPORT_MULTIPLIER,
	                  0,                       /* No flags */
	                  "LMA_Page_Alloc",
	                  psPageArrayData->aBaseArray,
	                  psPageArrayData->uiTotalNumChunks,
	                  &psPageArrayData->bPhysContig);
	if (PVRSRV_OK != eError)
	{
		RA_USAGE_STATS sRAStats;
		IMG_CHAR *pszArenaName;
		RA_Get_Usage_Stats(psPageArrayData->psArena, &sRAStats);
		pszArenaName = RA_GetArenaName(psPageArrayData->psArena);

		PVR_DPF((PVR_DBG_ERROR,
				"Contig: Failed to Allocate size = 0x%llx, align = 0x%llx"
				" Arena Free Space 0x%"IMG_UINT64_FMTSPECX""
				" Arena Name: '%s'",
				(unsigned long long)uiPhysSize,
				1ULL << uiLog2ChunkSize,
				sRAStats.ui64FreeArenaSize,
				pszArenaName));
		PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_PMR_FAILED_TO_ALLOC_PAGES, errorOnRAAlloc);
	}

#if defined(SUPPORT_GPUVIRT_VALIDATION)
{
	PVR_DPF((PVR_DBG_MESSAGE,
			"(GPU Virtualization Validation): First RealBase: %"IMG_UINT64_FMTSPECX,
			psPageArrayData->aBaseArray[0]));
}
#endif

	if (bPoisonOnAlloc)
	{
		eError = _PhysPgMemSet(psPageArrayData,
		                       psPageArrayData->aBaseArray,
		                       uiPhysSize,
		                       PVRSRV_POISON_ON_ALLOC_VALUE);
		PVR_LOG_GOTO_IF_ERROR(eError, "_PhysPgMemSet", errorOnPoison);
	}

	if (bZeroOnAlloc)
	{
		eError = _PhysPgMemSet(psPageArrayData,
		                       psPageArrayData->aBaseArray,
		                       uiPhysSize,
		                       ZERO_PAGE_VALUE);
		PVR_LOG_GOTO_IF_ERROR(eError, "_PhysPgMemSet", errorOnZero);
	}

	psPageArrayData->iNumChunksAllocated += psPageArrayData->uiChunksToAlloc;

	/* We have alloc'd the previous request, set 0 for book keeping */
	psPageArrayData->uiChunksToAlloc = 0;


#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
	PVRSRVStatsIncrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES, uiPhysSize, psPageArrayData->uiPid);
#else
	if (psPageArrayData->bPhysContig)
	{
		IMG_CPU_PHYADDR sLocalCpuPAddr;
		sLocalCpuPAddr.uiAddr = (IMG_UINT64) psPageArrayData->aBaseArray[0];
		PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES,
								 NULL,
								 sLocalCpuPAddr,
								 psPageArrayData->uiTotalNumChunks << uiLog2ChunkSize,
								 psPageArrayData->uiPid
								 DEBUG_MEMSTATS_VALUES);
	}
	else
	{
		IMG_UINT32 i, j;
		IMG_CPU_PHYADDR sLocalCpuPAddr;

		for (i = 0; i < psPageArrayData->uiTotalNumChunks;)
		{
			IMG_UINT32 ui32AllocSizeInChunks = 1;

			for (j = i;
			     j + 1 != psPageArrayData->uiTotalNumChunks &&
			     RA_BASE_IS_GHOST(psPageArrayData->aBaseArray[j + 1]);
			     j++)
			{
				ui32AllocSizeInChunks++;
			}

			sLocalCpuPAddr.uiAddr = (IMG_UINT64) psPageArrayData->aBaseArray[i];
			PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES,
									 NULL,
									 sLocalCpuPAddr,
									 ui32AllocSizeInChunks << uiLog2ChunkSize,
									 psPageArrayData->uiPid
									 DEBUG_MEMSTATS_VALUES);

			i += ui32AllocSizeInChunks;
		}
	}
#endif
#endif

	return PVRSRV_OK;

	/*
	  error exit paths follow:
	*/
errorOnZero:
errorOnPoison:
	eError = PVRSRV_ERROR_PMR_FAILED_TO_ALLOC_PAGES;

	RA_FreeMulti(psPageArrayData->psArena,
	              psPageArrayData->aBaseArray,
	              psPageArrayData->uiTotalNumChunks);

errorOnRAAlloc:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

/*
 * Fully allocated variant of sparse allocation does not take in as argument an
 * array of indices. It is used in cases where the amount of chunks to allocate is
 * the same as the total the PMR can represent. I.E when we want to fully populate
 * a sparse PMR.
 */
static PVRSRV_ERROR
_AllocLMPagesSparseFull(PMR_LMALLOCARRAY_DATA *psPageArrayData)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 uiLog2ChunkSize = psPageArrayData->uiLog2ChunkSize;
	IMG_UINT64 uiPhysSize = (IMG_UINT64) psPageArrayData->uiChunksToAlloc << uiLog2ChunkSize;
	IMG_BOOL bPoisonOnAlloc = psPageArrayData->bPoisonOnAlloc;
	IMG_BOOL bZeroOnAlloc = psPageArrayData->bZeroOnAlloc;


	eError = RA_AllocMultiSparse(psPageArrayData->psArena,
	                  uiLog2ChunkSize,
	                  RA_NO_IMPORT_MULTIPLIER,
	                  0,                       /* No flags */
	                  "LMA_Page_Alloc",
	                  psPageArrayData->aBaseArray,
	                  psPageArrayData->uiTotalNumChunks,
	                  NULL, /* No indices given meaning allocate full base array using chunk count below */
	                  psPageArrayData->uiChunksToAlloc);
	if (PVRSRV_OK != eError)
	{
		RA_USAGE_STATS sRAStats;
		IMG_CHAR *pszArenaName;
		RA_Get_Usage_Stats(psPageArrayData->psArena, &sRAStats);
		pszArenaName = RA_GetArenaName(psPageArrayData->psArena);

		PVR_DPF((PVR_DBG_ERROR,
				"SparseFull: Failed to Allocate size = 0x%llx, align = 0x%llx"
				" Arena Free Space 0x%"IMG_UINT64_FMTSPECX""
				" Arena Name: '%s'",
				(unsigned long long)uiPhysSize,
				1ULL << uiLog2ChunkSize,
				sRAStats.ui64FreeArenaSize,
				pszArenaName));
		PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_PMR_FAILED_TO_ALLOC_PAGES, errorOnRAAlloc);
	}

#if defined(SUPPORT_GPUVIRT_VALIDATION)
{
	PVR_DPF((PVR_DBG_MESSAGE,
		"(GPU Virtualization Validation): First RealBase: %"IMG_UINT64_FMTSPECX,
		psPageArrayData->aBaseArray[0]));
}
#endif

	if (bPoisonOnAlloc)
	{
		eError = _PhysPgMemSet(psPageArrayData,
		                       psPageArrayData->aBaseArray,
		                       uiPhysSize,
		                       PVRSRV_POISON_ON_ALLOC_VALUE);
		PVR_LOG_GOTO_IF_ERROR(eError, "_PhysPgMemSet", errorOnPoison);
	}

	if (bZeroOnAlloc)
	{
		eError = _PhysPgMemSet(psPageArrayData,
		                       psPageArrayData->aBaseArray,
		                       uiPhysSize,
		                       ZERO_PAGE_VALUE);
		PVR_LOG_GOTO_IF_ERROR(eError, "_PhysPgMemSet", errorOnZero);
	}

	psPageArrayData->iNumChunksAllocated += psPageArrayData->uiChunksToAlloc;

	/* We have alloc'd the previous request, set 0 for book keeping */
	psPageArrayData->uiChunksToAlloc = 0;

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
	PVRSRVStatsIncrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES, uiPhysSize, psPageArrayData->uiPid);
#else
	{
		IMG_UINT32 i;

		for (i = 0; i < psPageArrayData->uiTotalNumChunks; i++)
		{
			IMG_CPU_PHYADDR sLocalCpuPAddr;
			sLocalCpuPAddr.uiAddr =
				(IMG_UINT64) RA_BASE_STRIP_GHOST_BIT(psPageArrayData->aBaseArray[i]);
			PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES,
									 NULL,
									 sLocalCpuPAddr,
									 1 << uiLog2ChunkSize,
									 psPageArrayData->uiPid
									 DEBUG_MEMSTATS_VALUES);
		}
	}
#endif
#endif

	return PVRSRV_OK;

	/*
	  error exit paths follow:
	*/
errorOnZero:
errorOnPoison:
	eError = PVRSRV_ERROR_PMR_FAILED_TO_ALLOC_PAGES;

	RA_FreeMulti(psPageArrayData->psArena,
	              psPageArrayData->aBaseArray,
	              psPageArrayData->uiTotalNumChunks);

errorOnRAAlloc:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

static PVRSRV_ERROR
_AllocLMPagesSparse(PMR_LMALLOCARRAY_DATA *psPageArrayData, IMG_UINT32 *pui32MapTable)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 uiLog2ChunkSize = psPageArrayData->uiLog2ChunkSize;
	IMG_UINT32 uiChunkSize = 1ULL << uiLog2ChunkSize;
	IMG_UINT32 uiChunksToAlloc = psPageArrayData->uiChunksToAlloc;
	IMG_BOOL bPoisonOnAlloc = psPageArrayData->bPoisonOnAlloc;
	IMG_BOOL bZeroOnAlloc = psPageArrayData->bZeroOnAlloc;

	if (!pui32MapTable)
	{
		PVR_LOG_GOTO_WITH_ERROR("pui32MapTable", eError, PVRSRV_ERROR_PMR_INVALID_MAP_INDEX_ARRAY, errorOnRAAlloc);
	}

#if defined(DEBUG)
	/*
	 * This block performs validation of the mapping table input in the following ways:
	 * Check that each index in the mapping table does not exceed the number of the chunks
	 * the whole PMR supports.
	 * Check that each index given by the mapping table is not already allocated.
	 * Check that there are no duplicated indices given in the mapping table.
	 */
	{
		IMG_UINT32 i;
		IMG_BOOL bIssueDetected = IMG_FALSE;
		PVRSRV_ERROR eMapCheckError;

		for (i = 0; i < uiChunksToAlloc; i++)
		{
			if (pui32MapTable[i] >= psPageArrayData->uiTotalNumChunks)
			{
				PVR_DPF((PVR_DBG_ERROR,
						"%s: Page alloc request Index out of bounds for PMR @0x%p",
						__func__,
						psPageArrayData));
				eMapCheckError = PVRSRV_ERROR_DEVICEMEM_OUT_OF_RANGE;
				bIssueDetected = IMG_TRUE;
				break;
			}

			if (!RA_BASE_IS_INVALID(psPageArrayData->aBaseArray[pui32MapTable[i]]))
			{
				PVR_DPF((PVR_DBG_ERROR,
						"%s: Mapping already exists Index %u Mapping index %u",
						__func__,
						i,
						pui32MapTable[i]));
				eMapCheckError = PVRSRV_ERROR_PMR_MAPPING_ALREADY_EXISTS;
				bIssueDetected = IMG_TRUE;
				break;
			}

			if (RA_BASE_IS_SPARSE_PREP(psPageArrayData->aBaseArray[pui32MapTable[i]]))
			{
				PVR_DPF((PVR_DBG_ERROR,
						"%s: Mapping already exists in mapping table given Index %u Mapping index %u",
						__func__,
						i,
						pui32MapTable[i]));
				eMapCheckError = PVRSRV_ERROR_PMR_MAPPING_ALREADY_EXISTS;
				bIssueDetected = IMG_TRUE;
				break;
			}
			else
			{
				/* Set the To Prep value so we can detect duplicated map indices */
				psPageArrayData->aBaseArray[pui32MapTable[i]] = RA_BASE_SPARSE_PREP_ALLOC_ADDR;
			}
		}
		/* Unwind the Alloc Prep Values */
		if (bIssueDetected)
		{
			/* We don't want to affect the index of the issue seen
			 * as it could be a valid mapping. If it is a duplicated
			 * mapping in the given table then we will clean-up the
			 * previous instance anyway.
			 */
			IMG_UINT32 uiUnwind = i;

			for (i = 0; i < uiUnwind; i++)
			{
				psPageArrayData->aBaseArray[pui32MapTable[i]] = INVALID_BASE_ADDR;
			}

			PVR_GOTO_WITH_ERROR(eError, eMapCheckError, errorOnRAAlloc);
		}
	}
#endif

	eError = RA_AllocMultiSparse(psPageArrayData->psArena,
	                              psPageArrayData->uiLog2ChunkSize,
	                              RA_NO_IMPORT_MULTIPLIER,
	                              0,
	                              "LMA_Page_Alloc",
	                              psPageArrayData->aBaseArray,
	                              psPageArrayData->uiTotalNumChunks,
	                              pui32MapTable,
	                              uiChunksToAlloc);
	if (PVRSRV_OK != eError)
	{
		RA_USAGE_STATS sRAStats;
		IMG_CHAR *pszArenaName;
		RA_Get_Usage_Stats(psPageArrayData->psArena, &sRAStats);
		pszArenaName = RA_GetArenaName(psPageArrayData->psArena);

		PVR_DPF((PVR_DBG_ERROR,
				"Sparse: Failed to Allocate size = 0x%llx, align = 0x%llx"
				" Arena Free Space 0x%"IMG_UINT64_FMTSPECX""
				" Arena Name: '%s'",
				(unsigned long long) uiChunksToAlloc << uiLog2ChunkSize,
				1ULL << uiLog2ChunkSize,
				sRAStats.ui64FreeArenaSize,
				pszArenaName));
		PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_PMR_FAILED_TO_ALLOC_PAGES, errorOnRAAlloc);
	}

#if defined(SUPPORT_GPUVIRT_VALIDATION)
{
	PVR_DPF((PVR_DBG_MESSAGE,
	        "(GPU Virtualization Validation): First RealBase: %"IMG_UINT64_FMTSPECX,
	        psPageArrayData->aBaseArray[pui32MapTable[0]]));
}
#endif

	if (bPoisonOnAlloc || bZeroOnAlloc)
	{
		IMG_UINT32 i, ui32Index = 0;
		for (i = 0; i < uiChunksToAlloc; i++)
		{
			ui32Index = pui32MapTable[i];

			eError = _PhysPgMemSet(psPageArrayData,
								   &psPageArrayData->aBaseArray[ui32Index],
								   uiChunkSize,
								   bPoisonOnAlloc ? PVRSRV_POISON_ON_ALLOC_VALUE :
								                    ZERO_PAGE_VALUE);
			PVR_LOG_GOTO_IF_ERROR(eError, "_PhysPgMemSet", errorOnPoisonZero);
		}
	}

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
	PVRSRVStatsIncrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES,
	                            uiChunksToAlloc << uiLog2ChunkSize,
	                            psPageArrayData->uiPid);
#else
	{
		IMG_UINT32 i;

		for (i = 0; i < psPageArrayData->uiChunksToAlloc; i++)
		{
			IMG_UINT32 ui32Index = pui32MapTable[i];
			IMG_CPU_PHYADDR sLocalCpuPAddr;
			sLocalCpuPAddr.uiAddr =
				(IMG_UINT64) RA_BASE_STRIP_GHOST_BIT(psPageArrayData->aBaseArray[ui32Index]);
			PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES,
									 NULL,
									 sLocalCpuPAddr,
									 uiChunkSize,
									 psPageArrayData->uiPid
									 DEBUG_MEMSTATS_VALUES);
		}
	}
#endif
#endif

	psPageArrayData->iNumChunksAllocated += uiChunksToAlloc;

	/* We have alloc'd the previous request, set 0 for book keeping */
	psPageArrayData->uiChunksToAlloc = 0;

	return PVRSRV_OK;

	/*
	  error exit paths follow:
	*/
errorOnPoisonZero:
	eError = PVRSRV_ERROR_PMR_FAILED_TO_ALLOC_PAGES;

	RA_FreeMultiSparse(psPageArrayData->psArena,
	                    psPageArrayData->aBaseArray,
	                    psPageArrayData->uiTotalNumChunks,
	                    psPageArrayData->uiLog2ChunkSize,
	                    pui32MapTable,
	                    &uiChunksToAlloc);

errorOnRAAlloc:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;

}

static PVRSRV_ERROR
_AllocLMPages(PMR_LMALLOCARRAY_DATA *psPageArrayData, IMG_UINT32 *pui32MapTable)
{
	PVRSRV_ERROR eError;
	RA_ARENA *pArena;

	PVR_ASSERT(NULL != psPageArrayData);
	PVR_ASSERT(0 <= psPageArrayData->iNumChunksAllocated);

	if (psPageArrayData->uiTotalNumChunks <
			(psPageArrayData->iNumChunksAllocated + psPageArrayData->uiChunksToAlloc))
	{
		PVR_DPF((PVR_DBG_ERROR, "Pages requested to allocate don't fit PMR alloc Size. "
				"Allocated: %u + Requested: %u > Total Allowed: %u",
				psPageArrayData->iNumChunksAllocated,
				psPageArrayData->uiChunksToAlloc,
				psPageArrayData->uiTotalNumChunks));
		return PVRSRV_ERROR_PMR_BAD_MAPPINGTABLE_SIZE;
	}

	/* If we have a non-backed sparse PMR then we can just return */
	if (psPageArrayData->uiChunksToAlloc == 0)
	{
		PVR_DPF((PVR_DBG_MESSAGE,
							"%s: Non-Backed Sparse PMR Created: %p.",
							__func__,
							psPageArrayData));
		return PVRSRV_OK;
	}

#if defined(SUPPORT_GPUVIRT_VALIDATION)
	{
		IMG_UINT32 ui32OSid=0;
		PVRSRV_DEVICE_NODE *psDevNode = PhysHeapDeviceNode(psPageArrayData->psPhysHeap);

		/* Obtain the OSid specific data from our connection handle */
		if (psPageArrayData->psConnection != NULL)
		{
			ui32OSid = psPageArrayData->psConnection->ui32OSid;
		}

		if (PVRSRV_CHECK_SHARED_BUFFER(psPageArrayData->uiAllocFlags))
		{
			pArena=psDevNode->psOSSharedArena;
			PVR_DPF((PVR_DBG_MESSAGE,
					 "(GPU Virtualization Validation): Giving from shared mem"));
		}
		else
		{
			pArena=psDevNode->psOSidSubArena[ui32OSid];
			PVR_DPF((PVR_DBG_MESSAGE,
					 "(GPU Virtualization Validation): Giving from OS slot %d",
					 ui32OSid));
		}
	}
#else
	/* Get suitable local memory region for this GPU physheap allocation */
	eError = PhysmemGetArenaLMA(psPageArrayData->psPhysHeap, &pArena);
	PVR_LOG_RETURN_IF_ERROR(eError, "PhysmemGetArenaLMA");
#endif

	psPageArrayData->psArena = pArena;

	/*
	 * 3 cases:
	 * Sparse allocation populating the whole PMR.
	 * [**********]
	 * Sparse allocation partially populating the PMR at given indices.
	 * [*** *** **]
	 * Contiguous allocation.
	 * [**********]
	 *
	 * Note: Separate cases are required for 1 and 3 due to memstats tracking.
	 * In Contiguous case we can track the block as a single memstat record as we know
	 * we will also free in that size record.
	 * Sparse allocations require a memstat record per chunk as they can be arbitrarily
	 * free'd.
	 */
	if (psPageArrayData->bIsSparse)
	{
		if (psPageArrayData->uiTotalNumChunks == psPageArrayData->uiChunksToAlloc &&
		    !pui32MapTable)
		{
			eError = _AllocLMPagesSparseFull(psPageArrayData);
		}
		else
		{
			eError = _AllocLMPagesSparse(psPageArrayData, pui32MapTable);
		}
	}
	else
	{
		eError = _AllocLMPagesContig(psPageArrayData);
	}

	return eError;
}

static void
_FreeLMPageArray(PMR_LMALLOCARRAY_DATA *psPageArrayData)
{
	PVR_DPF((PVR_DBG_MESSAGE,
			"physmem_lma.c: freed local memory array structure for PMR @0x%p",
			psPageArrayData));

	OSFreeMem(psPageArrayData);
}

static PVRSRV_ERROR
_FreeLMPagesContig(PMR_LMALLOCARRAY_DATA *psPageArrayData)
{
	RA_ARENA *pArena = psPageArrayData->psArena;
	IMG_UINT64 uiPhysSize =
		(IMG_UINT64) psPageArrayData->uiTotalNumChunks << psPageArrayData->uiLog2ChunkSize;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psPageArrayData->iNumChunksAllocated != 0);
	PVR_ASSERT(psPageArrayData->iNumChunksAllocated ==
	           psPageArrayData->uiTotalNumChunks);

	if (psPageArrayData->bPoisonOnFree)
	{
		eError = _PhysPgMemSet(psPageArrayData,
							   psPageArrayData->aBaseArray,
							   uiPhysSize,
							   PVRSRV_POISON_ON_FREE_VALUE);
		PVR_LOG_IF_ERROR(eError, "_PhysPgMemSet");
	}

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
	PVRSRVStatsDecrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES,
	                            uiPhysSize,
	                            psPageArrayData->uiPid);
#else
	if (psPageArrayData->bPhysContig)
	{
		PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES,
		                                (IMG_UINT64) psPageArrayData->aBaseArray[0],
		                                psPageArrayData->uiPid);
	}
	else
	{
		IMG_UINT32 i;

		for (i = 0; i < psPageArrayData->uiTotalNumChunks; i++)
		{
			if (RA_BASE_IS_REAL(psPageArrayData->aBaseArray[i]))
			{
				PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES,
				                                (IMG_UINT64) psPageArrayData->aBaseArray[i],
												psPageArrayData->uiPid);
			}
		}

	}
#endif
#endif

	if (psPageArrayData->bPhysContig)
	{
		eError = RA_FreeMulti(pArena,
							  psPageArrayData->aBaseArray,
							  1);
		PVR_LOG_RETURN_IF_ERROR(eError, "RA_FreeMulti");
	}
	else
	{
		eError = RA_FreeMulti(pArena,
							  psPageArrayData->aBaseArray,
							  psPageArrayData->iNumChunksAllocated);
		PVR_LOG_RETURN_IF_ERROR(eError, "RA_FreeMulti");
	}

	psPageArrayData->iNumChunksAllocated = 0;

	PVR_ASSERT(0 <= psPageArrayData->iNumChunksAllocated);

	PVR_DPF((PVR_DBG_MESSAGE,
			"%s: freed %"IMG_UINT64_FMTSPEC" local memory for PMR @0x%p",
			__func__,
			uiPhysSize,
			psPageArrayData));

	return eError;
}

static PVRSRV_ERROR
_FreeLMPagesRemainingSparse(PMR_LMALLOCARRAY_DATA *psPageArrayData)
{
	IMG_UINT32 i;
	PVRSRV_ERROR eError;
	IMG_UINT32 uiChunkSize = 1ULL << psPageArrayData->uiLog2ChunkSize;
	IMG_BOOL bPoisonOnFree = psPageArrayData->bPoisonOnFree;

#if defined(PVRSRV_ENABLE_PROCESS_STATS) && !defined(PVRSRV_ENABLE_MEMORY_STATS)
	PVRSRVStatsDecrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES,
	                            psPageArrayData->iNumChunksAllocated << psPageArrayData->uiLog2ChunkSize,
	                            psPageArrayData->uiPid);
#endif

	for (i = 0; i < psPageArrayData->uiTotalNumChunks;)
	{
		if (RA_BASE_IS_REAL(psPageArrayData->aBaseArray[i]))
		{
			IMG_UINT32 j;
			IMG_UINT32 ui32AccumulatedChunks = 1;

			for (j = i;
				 j + 1 != psPageArrayData->uiTotalNumChunks &&
				 RA_BASE_IS_GHOST(psPageArrayData->aBaseArray[j + 1]);
				 j++)
			{
				ui32AccumulatedChunks++;
			}

#if defined(PVRSRV_ENABLE_PROCESS_STATS) && defined(PVRSRV_ENABLE_MEMORY_STATS)
			for (j = i; j < (i + ui32AccumulatedChunks); j++)
			{
				PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES,
												RA_BASE_STRIP_GHOST_BIT(psPageArrayData->aBaseArray[j]),
												psPageArrayData->uiPid);
				if (bPoisonOnFree)
#else
			for (j = i; j < (i + ui32AccumulatedChunks) && bPoisonOnFree; j++)
			{
#endif
				{
					eError = _PhysPgMemSet(psPageArrayData,
										   &psPageArrayData->aBaseArray[j],
										   uiChunkSize,
										   PVRSRV_POISON_ON_FREE_VALUE);
					PVR_LOG_IF_ERROR(eError, "_PhysPgMemSet");
				}
			}

			eError = RA_FreeMulti(psPageArrayData->psArena,
			                       &psPageArrayData->aBaseArray[i],
			                       ui32AccumulatedChunks);
			PVR_LOG_RETURN_IF_ERROR(eError, "RA_FreeMulti");

			psPageArrayData->iNumChunksAllocated -= ui32AccumulatedChunks;
			i += ui32AccumulatedChunks;
		}
		else if (RA_BASE_IS_INVALID(psPageArrayData->aBaseArray[i]))
		{
			i++;
		}
	}

	/* We have freed all allocations in the previous loop */
	PVR_ASSERT(0 <= psPageArrayData->iNumChunksAllocated);

	return PVRSRV_OK;
}

static PVRSRV_ERROR
_FreeLMPagesSparse(PMR_LMALLOCARRAY_DATA *psPageArrayData,
                   IMG_UINT32 *pui32FreeIndices,
                   IMG_UINT32 ui32FreeChunkCount)
{
	RA_ARENA *pArena = psPageArrayData->psArena;
	IMG_UINT32 uiLog2ChunkSize = psPageArrayData->uiLog2ChunkSize;
	IMG_UINT32 uiChunkSize = 1ULL << uiLog2ChunkSize;
	IMG_BOOL bPoisonOnFree = psPageArrayData->bPoisonOnFree;
	IMG_UINT32 uiActualFreeCount = ui32FreeChunkCount;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psPageArrayData->iNumChunksAllocated != 0);

#if defined(PVRSRV_ENABLE_PROCESS_STATS) && defined(PVRSRV_ENABLE_MEMORY_STATS)
	{
		IMG_UINT32 i;
		for (i = 0; i < ui32FreeChunkCount; i++)
		{
			IMG_UINT32 ui32Index = pui32FreeIndices[i];

			PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES,
			                                (IMG_UINT64) RA_BASE_STRIP_GHOST_BIT(
			                                psPageArrayData->aBaseArray[ui32Index]),
			                                psPageArrayData->uiPid);
		}
	}
#endif

	if (bPoisonOnFree)
	{
		IMG_UINT32 i, ui32Index = 0;
		for (i = 0; i < ui32FreeChunkCount; i++)
		{
			ui32Index = pui32FreeIndices[i];

			eError = _PhysPgMemSet(psPageArrayData,
								   &psPageArrayData->aBaseArray[ui32Index],
								   uiChunkSize,
								   PVRSRV_POISON_ON_FREE_VALUE);
			PVR_LOG_IF_ERROR(eError, "_PhysPgMemSet");
		}
	}

	eError = RA_FreeMultiSparse(pArena,
	                             psPageArrayData->aBaseArray,
	                             psPageArrayData->uiTotalNumChunks,
	                             uiLog2ChunkSize,
	                             pui32FreeIndices,
	                             &uiActualFreeCount);
	psPageArrayData->iNumChunksAllocated -= uiActualFreeCount;
#if defined(PVRSRV_ENABLE_PROCESS_STATS) && !defined(PVRSRV_ENABLE_MEMORY_STATS)
	PVRSRVStatsDecrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES,
								uiActualFreeCount << psPageArrayData->uiLog2ChunkSize,
								psPageArrayData->uiPid);
#endif
	if (eError == PVRSRV_ERROR_RA_FREE_INVALID_CHUNK)
	{
		/* Log the RA error but convert it to PMR level to match the interface,
		 * this is important because other PMR factories may not use the RA but
		 * still return error, returning a PMR based error
		 * keeps the interface agnostic to implementation behaviour.
		 */
		PVR_LOG_IF_ERROR(eError, "RA_FreeMultiSparse");
		return PVRSRV_ERROR_PMR_FREE_INVALID_CHUNK;
	}
	PVR_LOG_RETURN_IF_ERROR(eError, "RA_FreeMultiSparse");

	PVR_ASSERT(0 <= psPageArrayData->iNumChunksAllocated);


	PVR_DPF((PVR_DBG_MESSAGE,
			"%s: freed %d local memory for PMR @0x%p",
			__func__,
			(uiActualFreeCount * uiChunkSize),
			psPageArrayData));

	return PVRSRV_OK;
}

static PVRSRV_ERROR
_FreeLMPages(PMR_LMALLOCARRAY_DATA *psPageArrayData,
             IMG_UINT32 *pui32FreeIndices,
             IMG_UINT32 ui32FreeChunkCount)
{
	PVRSRV_ERROR eError;

	if (psPageArrayData->bIsSparse)
	{
		if (!pui32FreeIndices)
		{
			eError =  _FreeLMPagesRemainingSparse(psPageArrayData);
		}
		else
		{
			eError = _FreeLMPagesSparse(psPageArrayData, pui32FreeIndices, ui32FreeChunkCount);
		}
	}
	else
	{
		eError = _FreeLMPagesContig(psPageArrayData);
	}

	return eError;
}

/*
 *
 * Implementation of callback functions
 *
 */

/* destructor func is called after last reference disappears, but
   before PMR itself is freed. */
static void
PMRFinalizeLocalMem(PMR_IMPL_PRIVDATA pvPriv)
{
	PVRSRV_ERROR eError;
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData = NULL;

	psLMAllocArrayData = pvPriv;

	/* We can't free pages until now. */
	if (psLMAllocArrayData->iNumChunksAllocated != 0)
	{
#if defined(DEBUG) && defined(SUPPORT_VALIDATION) && defined(__linux__)
		PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
		IMG_UINT32 ui32LMALeakMax = psPVRSRVData->sMemLeakIntervals.ui32GPU;

		mutex_lock(&g_sLMALeakMutex);

		g_ui32LMALeakCounter++;
		if (ui32LMALeakMax && g_ui32LMALeakCounter >= ui32LMALeakMax)
		{
			g_ui32LMALeakCounter = 0;
			mutex_unlock(&g_sLMALeakMutex);

			PVR_DPF((PVR_DBG_WARNING, "%s: Skipped freeing of PMR 0x%p to trigger memory leak.", __func__, pvPriv));
			return;
		}

		mutex_unlock(&g_sLMALeakMutex);
#endif
		eError = _FreeLMPages(psLMAllocArrayData, NULL, 0);
		PVR_LOG_IF_ERROR(eError, "_FreeLMPages");
		PVR_ASSERT (eError == PVRSRV_OK);
	}

	_FreeLMPageArray(psLMAllocArrayData);
}

/* callback function for locking the system physical page addresses.
   As we are LMA there is nothing to do as we control physical memory. */
static PVRSRV_ERROR
PMRLockSysPhysAddressesLocalMem(PMR_IMPL_PRIVDATA pvPriv)
{

	PVRSRV_ERROR eError;
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData;

	psLMAllocArrayData = pvPriv;

	if (psLMAllocArrayData->bOnDemand)
	{
		/* Allocate Memory for deferred allocation */
		eError = _AllocLMPages(psLMAllocArrayData, NULL);
		PVR_RETURN_IF_ERROR(eError);
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR
PMRUnlockSysPhysAddressesLocalMem(PMR_IMPL_PRIVDATA pvPriv)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData;

	psLMAllocArrayData = pvPriv;

	if (psLMAllocArrayData->bOnDemand)
	{
		/* Free Memory for deferred allocation */
		eError = _FreeLMPages(psLMAllocArrayData, NULL, 0);
		PVR_RETURN_IF_ERROR(eError);
	}

	PVR_ASSERT(eError == PVRSRV_OK);
	return eError;
}

/* N.B. It is assumed that PMRLockSysPhysAddressesLocalMem() is called _before_ this function! */
static PVRSRV_ERROR
PMRSysPhysAddrLocalMem(PMR_IMPL_PRIVDATA pvPriv,
					   IMG_UINT32 ui32Log2PageSize,
					   IMG_UINT32 ui32NumOfPages,
					   IMG_DEVMEM_OFFSET_T *puiOffset,
					   IMG_BOOL *pbValid,
					   IMG_DEV_PHYADDR *psDevPAddr)
{
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData = pvPriv;
	IMG_UINT32 idx;
	IMG_UINT32 uiLog2AllocSize;
	IMG_UINT64 uiAllocIndex;
	IMG_DEVMEM_OFFSET_T uiInAllocOffset;
	IMG_UINT32 uiNumAllocs = psLMAllocArrayData->uiTotalNumChunks;

	if (psLMAllocArrayData->uiLog2ChunkSize < ui32Log2PageSize)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Requested physical addresses from PMR "
		         "for incompatible contiguity %u!",
		         __func__,
		         ui32Log2PageSize));
		return PVRSRV_ERROR_PMR_INCOMPATIBLE_CONTIGUITY;
	}

	PVR_ASSERT(psLMAllocArrayData->uiLog2ChunkSize != 0);
	PVR_ASSERT(ui32Log2PageSize >= RA_BASE_FLAGS_LOG2);

	if (psLMAllocArrayData->bPhysContig)
	{
		for (idx=0; idx < ui32NumOfPages; idx++)
		{
			if (pbValid[idx])
			{
				psDevPAddr[idx].uiAddr = psLMAllocArrayData->aBaseArray[0] + puiOffset[idx];
			}
		}
	}
	else
	{
		uiLog2AllocSize = psLMAllocArrayData->uiLog2ChunkSize;

		for (idx=0; idx < ui32NumOfPages; idx++)
		{
			if (pbValid[idx])
			{
				uiAllocIndex = puiOffset[idx] >> uiLog2AllocSize;
				uiInAllocOffset = puiOffset[idx] - (uiAllocIndex << uiLog2AllocSize);

				PVR_LOG_RETURN_IF_FALSE(uiAllocIndex < uiNumAllocs,
										"puiOffset out of range", PVRSRV_ERROR_OUT_OF_RANGE);

				PVR_ASSERT(uiInAllocOffset < (1ULL << uiLog2AllocSize));

				/* The base may or may not be a ghost base, but we don't care,
				 * we just need the real representation of the base.
				 */
				psDevPAddr[idx].uiAddr = RA_BASE_STRIP_GHOST_BIT(
					psLMAllocArrayData->aBaseArray[uiAllocIndex]) + uiInAllocOffset;
			}
		}
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR
PMRAcquireKernelMappingDataLocalMem(PMR_IMPL_PRIVDATA pvPriv,
								 size_t uiOffset,
								 size_t uiSize,
								 void **ppvKernelAddressOut,
								 IMG_HANDLE *phHandleOut,
								 PMR_FLAGS_T ulFlags)
{
	PVRSRV_ERROR eError;
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData = pvPriv;
	PMR_KERNEL_MAPPING *psKernelMapping;
	RA_BASE_T *paBaseArray;
	IMG_UINT32 ui32ChunkIndex = 0;
	size_t uiOffsetMask = uiOffset;

	IMG_UINT32 uiLog2ChunkSize = psLMAllocArrayData->uiLog2ChunkSize;
	IMG_UINT64 uiChunkSize = 1ULL << uiLog2ChunkSize;
	IMG_UINT64 uiPhysSize;

	PVR_ASSERT(psLMAllocArrayData);
	PVR_ASSERT(ppvKernelAddressOut);
	PVR_ASSERT(phHandleOut);

	if (psLMAllocArrayData->bIsSparse)
	{
		IMG_UINT32 i;
		/* Locate the desired physical chunk to map in */
		ui32ChunkIndex = uiOffset >> psLMAllocArrayData->uiLog2ChunkSize;

		if (OSIsMapPhysNonContigSupported())
		{
			/* If a size hasn't been supplied assume we are mapping a single page */
			IMG_UINT32 uiNumChunksToMap;

			/* This is to support OSMapPMR originated parameters */
			if (uiOffset == 0 && uiSize == 0)
			{
				uiNumChunksToMap = psLMAllocArrayData->iNumChunksAllocated;
			}
			else
			{
				uiNumChunksToMap = uiSize >> psLMAllocArrayData->uiLog2ChunkSize;
			}

			/* Check we are attempting to map at least a chunk in size */
			if (uiNumChunksToMap < 1)
			{
				PVR_LOG_RETURN_IF_ERROR(PVRSRV_ERROR_INVALID_PARAMS, "uiNumChunksToMap < 1");
			}

			/* Check contiguous region doesn't exceed size of PMR */
			if (ui32ChunkIndex + (uiNumChunksToMap - 1) > psLMAllocArrayData->uiTotalNumChunks)
			{
				PVR_LOG_RETURN_IF_ERROR(PVRSRV_ERROR_INVALID_PARAMS,
				                        "Mapping range exceeds total num chunks in PMR");
			}

			/* Check the virtually contiguous region given is physically backed */
			for (i = ui32ChunkIndex; i < ui32ChunkIndex + uiNumChunksToMap; i++)
			{
				if (RA_BASE_IS_INVALID(psLMAllocArrayData->aBaseArray[i]))
				{
					PVR_LOG_RETURN_IF_ERROR(PVRSRV_ERROR_PMR_INCOMPATIBLE_CONTIGUITY, "Sparse contiguity check");
				}
			}
			/* Size of virtually contiguous sparse alloc */
			uiPhysSize = (IMG_UINT64) uiNumChunksToMap << psLMAllocArrayData->uiLog2ChunkSize;
		}
		else
		{
			size_t uiStart = uiOffset;
			size_t uiEnd = uiOffset + uiSize - 1;
			size_t uiChunkMask = ~((1 << psLMAllocArrayData->uiLog2ChunkSize) - 1);

			/* We can still map if only one chunk is required */
			if ((uiStart & uiChunkMask) != (uiEnd & uiChunkMask))
			{
				PVR_LOG_RETURN_IF_ERROR(PVRSRV_ERROR_PMR_INCOMPATIBLE_CONTIGUITY, "Sparse contiguity check");
			}
			/* Map a single chunk */
			uiPhysSize = uiChunkSize;
		}

		paBaseArray = &psLMAllocArrayData->aBaseArray[ui32ChunkIndex];

		/* Offset mask to be used for address offsets within a chunk */
		uiOffsetMask = (1U << psLMAllocArrayData->uiLog2ChunkSize) - 1;
	}
	else
	{
		paBaseArray = psLMAllocArrayData->aBaseArray;
		uiPhysSize = (IMG_UINT64) psLMAllocArrayData->uiTotalNumChunks << uiLog2ChunkSize;
	}

	PVR_ASSERT(ui32ChunkIndex < psLMAllocArrayData->uiTotalNumChunks);

	psKernelMapping = OSAllocMem(sizeof(*psKernelMapping));
	PVR_RETURN_IF_NOMEM(psKernelMapping);

	eError = _MapPMRKernel(psLMAllocArrayData,
	                       paBaseArray,
	                       uiPhysSize,
	                       ulFlags,
	                       psKernelMapping);
	if (eError == PVRSRV_OK)
	{
		/* uiOffset & uiOffsetMask is used to get the kernel addr within the page */
		*ppvKernelAddressOut = ((IMG_CHAR *) psKernelMapping->pvKernelAddress) + (uiOffset & uiOffsetMask);
		*phHandleOut = psKernelMapping;
	}
	else
	{
		OSFreeMem(psKernelMapping);
		PVR_LOG_ERROR(eError, "_MapPMRKernel");
	}

	return eError;
}

static void PMRReleaseKernelMappingDataLocalMem(PMR_IMPL_PRIVDATA pvPriv,
                                                IMG_HANDLE hHandle)
{
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData = (PMR_LMALLOCARRAY_DATA *) pvPriv;
	PMR_KERNEL_MAPPING *psKernelMapping = (PMR_KERNEL_MAPPING *) hHandle;

	PVR_ASSERT(psLMAllocArrayData);
	PVR_ASSERT(psKernelMapping);

	_UnMapPMRKernel(psLMAllocArrayData,
	                psKernelMapping);

	OSFreeMem(psKernelMapping);
}

static PVRSRV_ERROR
CopyBytesLocalMem(PMR_IMPL_PRIVDATA pvPriv,
				  IMG_DEVMEM_OFFSET_T uiOffset,
				  IMG_UINT8 *pcBuffer,
				  size_t uiBufSz,
				  size_t *puiNumBytes,
				  void (*pfnCopyBytes)(IMG_UINT8 *pcBuffer,
									   IMG_UINT8 *pcPMR,
									   size_t uiSize))
{
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData = pvPriv;
	size_t uiBytesCopied;
	size_t uiBytesToCopy;
	size_t uiBytesCopyableFromAlloc;
	PMR_KERNEL_MAPPING sMapping;
	IMG_UINT8 *pcKernelPointer = NULL;
	size_t uiBufferOffset;
	IMG_UINT64 uiAllocIndex;
	IMG_DEVMEM_OFFSET_T uiInAllocOffset;
	IMG_UINT32 uiLog2ChunkSize = psLMAllocArrayData->uiLog2ChunkSize;
	IMG_UINT64 uiChunkSize = 1ULL << uiLog2ChunkSize;
	IMG_UINT64 uiPhysSize;
	PVRSRV_ERROR eError;

	uiBytesCopied = 0;
	uiBytesToCopy = uiBufSz;
	uiBufferOffset = 0;

	if (psLMAllocArrayData->bIsSparse)
	{
		while (uiBytesToCopy > 0)
		{
			/* we have to map one alloc in at a time */
			PVR_ASSERT(psLMAllocArrayData->uiLog2ChunkSize != 0);
			uiAllocIndex = uiOffset >> psLMAllocArrayData->uiLog2ChunkSize;
			uiInAllocOffset = uiOffset - (uiAllocIndex << psLMAllocArrayData->uiLog2ChunkSize);
			uiBytesCopyableFromAlloc = uiBytesToCopy;
			if (uiBytesCopyableFromAlloc + uiInAllocOffset > (1ULL << psLMAllocArrayData->uiLog2ChunkSize))
			{
				uiBytesCopyableFromAlloc = TRUNCATE_64BITS_TO_SIZE_T((1ULL << psLMAllocArrayData->uiLog2ChunkSize)-uiInAllocOffset);
			}
			/* Mapping a single chunk at a time */
			uiPhysSize = uiChunkSize;

			PVR_ASSERT(uiBytesCopyableFromAlloc != 0);
			PVR_ASSERT(uiAllocIndex < psLMAllocArrayData->uiTotalNumChunks);
			PVR_ASSERT(uiInAllocOffset < (1ULL << uiLog2ChunkSize));

			eError = _MapPMRKernel(psLMAllocArrayData,
			                       &psLMAllocArrayData->aBaseArray[uiAllocIndex],
			                       uiPhysSize,
			                       PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC,
			                       &sMapping);
			PVR_GOTO_IF_ERROR(eError, e0);
			pcKernelPointer = sMapping.pvKernelAddress;
			pfnCopyBytes(&pcBuffer[uiBufferOffset], &pcKernelPointer[uiInAllocOffset], uiBytesCopyableFromAlloc);

			_UnMapPMRKernel(psLMAllocArrayData,
			                &sMapping);

			uiBufferOffset += uiBytesCopyableFromAlloc;
			uiBytesToCopy -= uiBytesCopyableFromAlloc;
			uiOffset += uiBytesCopyableFromAlloc;
			uiBytesCopied += uiBytesCopyableFromAlloc;
		}
	}
	else
	{
		uiPhysSize = (IMG_UINT64) psLMAllocArrayData->uiTotalNumChunks << uiLog2ChunkSize;
		PVR_ASSERT((uiOffset + uiBufSz) <= uiPhysSize);
		PVR_ASSERT(uiChunkSize != 0);
		eError = _MapPMRKernel(psLMAllocArrayData,
		                       psLMAllocArrayData->aBaseArray,
		                       uiPhysSize,
		                       PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC,
		                       &sMapping);
		PVR_GOTO_IF_ERROR(eError, e0);
		pcKernelPointer = sMapping.pvKernelAddress;
		pfnCopyBytes(pcBuffer, &pcKernelPointer[uiOffset], uiBufSz);

		_UnMapPMRKernel(psLMAllocArrayData,
		                &sMapping);

		uiBytesCopied = uiBufSz;
	}
	*puiNumBytes = uiBytesCopied;
	return PVRSRV_OK;
e0:
	*puiNumBytes = uiBytesCopied;
	return eError;
}

static void ReadLocalMem(IMG_UINT8 *pcBuffer,
						 IMG_UINT8 *pcPMR,
						 size_t uiSize)
{
	/* the memory is mapped as WC (and also aligned to page size) so we can
	 * safely call "Cached" memcpy */
	OSCachedMemCopy(pcBuffer, pcPMR, uiSize);
}

static PVRSRV_ERROR
PMRReadBytesLocalMem(PMR_IMPL_PRIVDATA pvPriv,
				  IMG_DEVMEM_OFFSET_T uiOffset,
				  IMG_UINT8 *pcBuffer,
				  size_t uiBufSz,
				  size_t *puiNumBytes)
{
	return CopyBytesLocalMem(pvPriv,
							 uiOffset,
							 pcBuffer,
							 uiBufSz,
							 puiNumBytes,
							 ReadLocalMem);
}

static void WriteLocalMem(IMG_UINT8 *pcBuffer,
						  IMG_UINT8 *pcPMR,
						  size_t uiSize)
{
	/* the memory is mapped as WC (and also aligned to page size) so we can
	 * safely call "Cached" memcpy but need to issue a write memory barrier
	 * to flush the write buffers after */
	OSCachedMemCopyWMB(pcPMR, pcBuffer, uiSize);
}

static PVRSRV_ERROR
PMRWriteBytesLocalMem(PMR_IMPL_PRIVDATA pvPriv,
					  IMG_DEVMEM_OFFSET_T uiOffset,
					  IMG_UINT8 *pcBuffer,
					  size_t uiBufSz,
					  size_t *puiNumBytes)
{
	return CopyBytesLocalMem(pvPriv,
							 uiOffset,
							 pcBuffer,
							 uiBufSz,
							 puiNumBytes,
							 WriteLocalMem);
}

/*************************************************************************/ /*!
@Function       PMRChangeSparseMemLocalMem
@Description    This function Changes the sparse mapping by allocating and
                freeing of pages. It also changes the GPU maps accordingly.
@Return         PVRSRV_ERROR failure code
*/ /**************************************************************************/
static PVRSRV_ERROR
PMRChangeSparseMemLocalMem(PMR_IMPL_PRIVDATA pPriv,
                           const PMR *psPMR,
                           IMG_UINT32 ui32AllocPageCount,
                           IMG_UINT32 *pai32AllocIndices,
                           IMG_UINT32 ui32FreePageCount,
                           IMG_UINT32 *pai32FreeIndices,
                           IMG_UINT32 uiFlags)
{
	PVRSRV_ERROR eError = PVRSRV_ERROR_INVALID_PARAMS;

	IMG_UINT32 ui32AdtnlAllocPages = 0;
	IMG_UINT32 ui32AdtnlFreePages = 0;
	IMG_UINT32 ui32CommonRequstCount = 0;
	IMG_UINT32 ui32Loop = 0;
	IMG_UINT32 ui32Index = 0;
	IMG_UINT32 uiAllocpgidx;
	IMG_UINT32 uiFreepgidx;

	PMR_LMALLOCARRAY_DATA *psPMRPageArrayData = (PMR_LMALLOCARRAY_DATA *)pPriv;
	IMG_UINT32 uiLog2ChunkSize = psPMRPageArrayData->uiLog2ChunkSize;
	IMG_UINT32 uiChunkSize = 1ULL << uiLog2ChunkSize;

#if defined(DEBUG)
	IMG_BOOL bPoisonFail = IMG_FALSE;
	IMG_BOOL bZeroFail = IMG_FALSE;
#endif

	/* Fetch the Page table array represented by the PMR */
	RA_BASE_T *paBaseArray = psPMRPageArrayData->aBaseArray;
	PMR_MAPPING_TABLE *psPMRMapTable = PMR_GetMappingTable(psPMR);

	/* The incoming request is classified into two operations independent of
	 * each other: alloc & free chunks.
	 * These operations can be combined with two mapping operations as well
	 * which are GPU & CPU space mappings.
	 *
	 * From the alloc and free chunk requests, the net amount of chunks to be
	 * allocated or freed is computed. Chunks that were requested to be freed
	 * will be reused to fulfil alloc requests.
	 *
	 * The order of operations is:
	 * 1. Allocate new Chunks.
	 * 2. Move the free chunks from free request to alloc positions.
	 * 3. Free the rest of the chunks not used for alloc
	 *
	 * Alloc parameters are validated at the time of allocation
	 * and any error will be handled then. */

	if (SPARSE_RESIZE_BOTH == (uiFlags & SPARSE_RESIZE_BOTH))
	{
		ui32CommonRequstCount = (ui32AllocPageCount > ui32FreePageCount) ?
				ui32FreePageCount : ui32AllocPageCount;

		PDUMP_PANIC(PMR_DeviceNode(psPMR), SPARSEMEM_SWAP, "Request to swap alloc & free chunks not supported");
	}

	if (SPARSE_RESIZE_ALLOC == (uiFlags & SPARSE_RESIZE_ALLOC))
	{
		ui32AdtnlAllocPages = ui32AllocPageCount - ui32CommonRequstCount;
	}
	else
	{
		ui32AllocPageCount = 0;
	}

	if (SPARSE_RESIZE_FREE == (uiFlags & SPARSE_RESIZE_FREE))
	{
		ui32AdtnlFreePages = ui32FreePageCount - ui32CommonRequstCount;
	}
	else
	{
		ui32FreePageCount = 0;
	}

	PVR_LOG_RETURN_IF_FALSE(
	    (ui32CommonRequstCount | ui32AdtnlAllocPages | ui32AdtnlFreePages) != 0,
	    "Invalid combination of parameters: ui32CommonRequstCount,"
	    " ui32AdtnlAllocPages and ui32AdtnlFreePages.",
	    PVRSRV_ERROR_INVALID_PARAMS
	);

	{
		/* Validate the free page indices */
		if (ui32FreePageCount)
		{
			if (pai32FreeIndices != NULL)
			{
				for (ui32Loop = 0; ui32Loop < ui32FreePageCount; ui32Loop++)
				{
					uiFreepgidx = pai32FreeIndices[ui32Loop];

					if (uiFreepgidx > psPMRPageArrayData->uiTotalNumChunks)
					{
						PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_DEVICEMEM_OUT_OF_RANGE, e0);
					}

					if (RA_BASE_IS_INVALID(paBaseArray[uiFreepgidx]))
					{
						PVR_LOG_GOTO_WITH_ERROR("paBaseArray[uiFreepgidx]", eError, PVRSRV_ERROR_INVALID_PARAMS, e0);
					}
				}
			}
			else
			{
				PVR_DPF((PVR_DBG_ERROR,
				         "%s: Given non-zero free count but missing indices array",
				         __func__));
				return PVRSRV_ERROR_INVALID_PARAMS;
			}
		}

		/* The following block of code verifies any issues with common alloc chunk indices */
		for (ui32Loop = ui32AdtnlAllocPages; ui32Loop < ui32AllocPageCount; ui32Loop++)
		{
			uiAllocpgidx = pai32AllocIndices[ui32Loop];
			if (uiAllocpgidx > psPMRPageArrayData->uiTotalNumChunks)
			{
				PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_DEVICEMEM_OUT_OF_RANGE, e0);
			}

			if (SPARSE_REMAP_MEM != (uiFlags & SPARSE_REMAP_MEM))
			{
				if ((!RA_BASE_IS_INVALID(paBaseArray[uiAllocpgidx])) ||
						(psPMRMapTable->aui32Translation[uiAllocpgidx] != TRANSLATION_INVALID))
				{
					PVR_LOG_GOTO_WITH_ERROR("Trying to allocate already allocated page again", eError, PVRSRV_ERROR_INVALID_PARAMS, e0);
				}
			}
			else
			{
				if ((RA_BASE_IS_INVALID(paBaseArray[uiAllocpgidx])) ||
				    (psPMRMapTable->aui32Translation[uiAllocpgidx] == TRANSLATION_INVALID))
				{
					PVR_LOG_GOTO_WITH_ERROR("Unable to remap memory due to missing page", eError, PVRSRV_ERROR_INVALID_PARAMS, e0);
				}
			}
		}

		ui32Loop = 0;

		/* Allocate new chunks */
		if (0 != ui32AdtnlAllocPages)
		{
			/* Say how many chunks to allocate */
			psPMRPageArrayData->uiChunksToAlloc = ui32AdtnlAllocPages;

			eError = _AllocLMPages(psPMRPageArrayData, pai32AllocIndices);
			PVR_LOG_GOTO_IF_ERROR(eError, "_AllocLMPages", e0);

			/* Mark the corresponding chunks of translation table as valid */
			for (ui32Loop = 0; ui32Loop < ui32AdtnlAllocPages; ui32Loop++)
			{
				psPMRMapTable->aui32Translation[pai32AllocIndices[ui32Loop]] = pai32AllocIndices[ui32Loop];
			}

			psPMRMapTable->ui32NumPhysChunks += ui32AdtnlAllocPages;
		}

		ui32Index = ui32Loop;
		ui32Loop = 0;

		/* Move the corresponding free chunks to alloc request */
		eError = RA_SwapSparseMem(psPMRPageArrayData->psArena,
		                           paBaseArray,
		                           psPMRPageArrayData->uiTotalNumChunks,
		                           psPMRPageArrayData->uiLog2ChunkSize,
		                           &pai32AllocIndices[ui32Index],
		                           &pai32FreeIndices[ui32Loop],
		                           ui32CommonRequstCount);
		PVR_LOG_GOTO_IF_ERROR(eError, "RA_SwapSparseMem", unwind_alloc);

		for (ui32Loop = 0; ui32Loop < ui32CommonRequstCount; ui32Loop++, ui32Index++)
		{
			uiAllocpgidx = pai32AllocIndices[ui32Index];
			uiFreepgidx  = pai32FreeIndices[ui32Loop];

			/* Is remap mem used in real world scenario? Should it be turned to a
			 *  debug feature? The condition check needs to be out of loop, will be
			 *  done at later point though after some analysis */
			if ((uiFlags & SPARSE_REMAP_MEM) != SPARSE_REMAP_MEM)
			{
				psPMRMapTable->aui32Translation[uiFreepgidx] = TRANSLATION_INVALID;
				psPMRMapTable->aui32Translation[uiAllocpgidx] = uiAllocpgidx;
			}
			else
			{
				psPMRMapTable->aui32Translation[uiFreepgidx] = uiFreepgidx;
				psPMRMapTable->aui32Translation[uiAllocpgidx] = uiAllocpgidx;
			}

			/* Be sure to honour the attributes associated with the allocation
			 * such as zeroing, poisoning etc. */
			if (psPMRPageArrayData->bPoisonOnAlloc)
			{
				eError = _PhysPgMemSet(psPMRPageArrayData,
				                       &psPMRPageArrayData->aBaseArray[uiAllocpgidx],
				                       uiChunkSize,
				                       PVRSRV_POISON_ON_ALLOC_VALUE);

				/* Consider this as a soft failure and go ahead but log error to kernel log */
				if (eError != PVRSRV_OK)
				{
#if defined(DEBUG)
					bPoisonFail = IMG_TRUE;
#endif
				}
			}

			if (psPMRPageArrayData->bZeroOnAlloc)
			{
				eError = _PhysPgMemSet(psPMRPageArrayData,
									   &psPMRPageArrayData->aBaseArray[uiAllocpgidx],
									   uiChunkSize,
									   ZERO_PAGE_VALUE);
				/* Consider this as a soft failure and go ahead but log error to kernel log */
				if (eError != PVRSRV_OK)
				{
#if defined(DEBUG)
					/* Don't think we need to zero any chunks further */
					bZeroFail = IMG_TRUE;
#endif
				}
			}
		}

		/* Free the additional free chunks */
		if (0 != ui32AdtnlFreePages)
		{
			ui32Index = ui32Loop;
			eError = _FreeLMPages(psPMRPageArrayData, &pai32FreeIndices[ui32Loop], ui32AdtnlFreePages);
			PVR_LOG_GOTO_IF_ERROR(eError, "_FreeLMPages", e0);

			ui32Loop = 0;

			while (ui32Loop++ < ui32AdtnlFreePages)
			{
				/* Set the corresponding mapping table entry to invalid address */
				psPMRMapTable->aui32Translation[pai32FreeIndices[ui32Index++]] = TRANSLATION_INVALID;
			}

			psPMRMapTable->ui32NumPhysChunks -= ui32AdtnlFreePages;
		}
	}

#if defined(DEBUG)
	if (IMG_TRUE == bPoisonFail)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Error in poisoning the chunk", __func__));
	}

	if (IMG_TRUE == bZeroFail)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Error in zeroing the chunk", __func__));
	}
#endif

	return PVRSRV_OK;

unwind_alloc:
	_FreeLMPages(psPMRPageArrayData, pai32AllocIndices, ui32Index);

	for (ui32Loop = 0; ui32Loop < ui32Index; ui32Loop++)
	{
		psPMRMapTable->aui32Translation[pai32AllocIndices[ui32Loop]] = TRANSLATION_INVALID;
	}

e0:
	return eError;
}

/*************************************************************************/ /*!
@Function       PMRChangeSparseMemCPUMapLocalMem
@Description    This function Changes CPU maps accordingly
@Return         PVRSRV_ERROR failure code
*/ /**************************************************************************/
static
PVRSRV_ERROR PMRChangeSparseMemCPUMapLocalMem(PMR_IMPL_PRIVDATA pPriv,
                                              const PMR *psPMR,
                                              IMG_UINT64 sCpuVAddrBase,
                                              IMG_UINT32 ui32AllocPageCount,
                                              IMG_UINT32 *pai32AllocIndices,
                                              IMG_UINT32 ui32FreePageCount,
                                              IMG_UINT32 *pai32FreeIndices)
{
	PVRSRV_ERROR eError;
	IMG_DEV_PHYADDR *psPageArray;
	PMR_LMALLOCARRAY_DATA *psPMRPageArrayData = (PMR_LMALLOCARRAY_DATA *)pPriv;
	uintptr_t sCpuVABase = sCpuVAddrBase;
	IMG_CPU_PHYADDR sCpuAddrPtr;
	IMG_BOOL bValid = IMG_FALSE;
	IMG_UINT32 i;

	/* Get the base address of the heap */
	eError = PMR_CpuPhysAddr(psPMR,
	                         psPMRPageArrayData->uiLog2ChunkSize,
	                         1,
	                         0,	/* offset zero here mean first page in the PMR */
	                         &sCpuAddrPtr,
	                         &bValid);
	PVR_LOG_RETURN_IF_ERROR(eError, "PMR_CpuPhysAddr");

	/* Phys address of heap is computed here by subtracting the offset of this page
	 * basically phys address of any page = Base address of heap + offset of the page */
	sCpuAddrPtr.uiAddr -= RA_BASE_STRIP_GHOST_BIT(psPMRPageArrayData->aBaseArray[0]);

	/* We still have ghost bits in the base array, this interface expects true page
	 * addresses so we need to pre mask / translate the base array
	 */
	psPageArray = OSAllocMem(sizeof(IMG_DEV_PHYADDR)*
                             psPMRPageArrayData->uiTotalNumChunks);
	PVR_LOG_RETURN_IF_NOMEM(psPageArray, "Page translation array");

	for (i = 0; i <  psPMRPageArrayData->uiTotalNumChunks; i++)
	{
		psPageArray[i].uiAddr = RA_BASE_STRIP_GHOST_BIT(psPMRPageArrayData->aBaseArray[i]);
	}

	eError = OSChangeSparseMemCPUAddrMap((void**) psPageArray,
	                                     sCpuVABase,
	                                     sCpuAddrPtr,
	                                     ui32AllocPageCount,
	                                     pai32AllocIndices,
	                                     ui32FreePageCount,
	                                     pai32FreeIndices,
	                                     IMG_TRUE);

	OSFreeMem(psPageArray);

	return eError;
}

static PMR_IMPL_FUNCTAB _sPMRLMAFuncTab = {
	.pfnLockPhysAddresses = &PMRLockSysPhysAddressesLocalMem,
	.pfnUnlockPhysAddresses = &PMRUnlockSysPhysAddressesLocalMem,
	.pfnDevPhysAddr = &PMRSysPhysAddrLocalMem,
	.pfnAcquireKernelMappingData = &PMRAcquireKernelMappingDataLocalMem,
	.pfnReleaseKernelMappingData = &PMRReleaseKernelMappingDataLocalMem,
	.pfnReadBytes = &PMRReadBytesLocalMem,
	.pfnWriteBytes = &PMRWriteBytesLocalMem,
	.pfnChangeSparseMem = &PMRChangeSparseMemLocalMem,
	.pfnChangeSparseMemCPUMap = &PMRChangeSparseMemCPUMapLocalMem,
	.pfnMMap = NULL,
	.pfnFinalize = &PMRFinalizeLocalMem
};

PVRSRV_ERROR
PhysmemNewLocalRamBackedPMR(PHYS_HEAP *psPhysHeap,
                            CONNECTION_DATA *psConnection,
                            IMG_DEVMEM_SIZE_T uiSize,
                            IMG_UINT32 ui32NumPhysChunks,
                            IMG_UINT32 ui32NumVirtChunks,
                            IMG_UINT32 *pui32MappingTable,
                            IMG_UINT32 uiLog2AllocPageSize,
                            PVRSRV_MEMALLOCFLAGS_T uiFlags,
                            const IMG_CHAR *pszAnnotation,
                            IMG_PID uiPid,
                            PMR **ppsPMRPtr,
                            IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eError;
	PVRSRV_ERROR eError2;
	PMR *psPMR = NULL;
	PMR_LMALLOCARRAY_DATA *psPrivData = NULL;
	PMR_FLAGS_T uiPMRFlags;
	IMG_BOOL bZero;
	IMG_BOOL bPoisonOnAlloc;
	IMG_BOOL bPoisonOnFree;
	IMG_BOOL bOnDemand;
	IMG_BOOL bIsSparse;

	/* This path is checking for the type of PMR to create, if sparse we
	 * have to perform additional validation as we can only map sparse ranges
	 * if the os functionality to do so is present. We can also only map virtually
	 * contiguous sparse regions. Non backed gaps in a range cannot be mapped.
	 */
	if (ui32NumPhysChunks != ui32NumVirtChunks || ui32NumVirtChunks > 1)
	{
		if (PVRSRV_CHECK_KERNEL_CPU_MAPPABLE(uiFlags) &&
		   !OSIsMapPhysNonContigSupported())
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: LMA kernel mapping functions not available "
					"for physically discontiguous memory.",
					__func__));
			PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_INVALID_PARAMS, errorOnParam);
		}
		bIsSparse = IMG_TRUE;
	}
	else
	{
		bIsSparse = IMG_FALSE;
	}

	bOnDemand = PVRSRV_CHECK_ON_DEMAND(uiFlags) ? IMG_TRUE : IMG_FALSE;
	bZero = PVRSRV_CHECK_ZERO_ON_ALLOC(uiFlags) ? IMG_TRUE : IMG_FALSE;
	bPoisonOnAlloc = PVRSRV_CHECK_POISON_ON_ALLOC(uiFlags) ? IMG_TRUE : IMG_FALSE;
#if defined(DEBUG)
	bPoisonOnFree = PVRSRV_CHECK_POISON_ON_FREE(uiFlags) ? IMG_TRUE : IMG_FALSE;
#else
	bPoisonOnFree = IMG_FALSE;
#endif

	/* Create Array structure that holds the physical pages */
	eError = _AllocLMPageArray(uiSize,
	                           ui32NumPhysChunks,
	                           ui32NumVirtChunks,
	                           uiLog2AllocPageSize,
	                           bZero,
	                           bPoisonOnAlloc,
	                           bPoisonOnFree,
	                           bIsSparse,
	                           bOnDemand,
	                           psPhysHeap,
	                           uiFlags,
	                           uiPid,
	                           &psPrivData,
	                           psConnection);
	PVR_GOTO_IF_ERROR(eError, errorOnAllocPageArray);

	if (!bOnDemand)
	{
		/* Allocate the physical pages */
		eError = _AllocLMPages(psPrivData, pui32MappingTable);
		PVR_GOTO_IF_ERROR(eError, errorOnAllocPages);
	}

	/* In this instance, we simply pass flags straight through.

	   Generically, uiFlags can include things that control the PMR
	   factory, but we don't need any such thing (at the time of
	   writing!), and our caller specifies all PMR flags so we don't
	   need to meddle with what was given to us.
	*/
	uiPMRFlags = (PMR_FLAGS_T)(uiFlags & PVRSRV_MEMALLOCFLAGS_PMRFLAGSMASK);
	/* check no significant bits were lost in cast due to different
	   bit widths for flags */
	PVR_ASSERT(uiPMRFlags == (uiFlags & PVRSRV_MEMALLOCFLAGS_PMRFLAGSMASK));

	if (bOnDemand)
	{
		PDUMPCOMMENT(PhysHeapDeviceNode(psPhysHeap), "Deferred Allocation PMR (LMA)");
	}

	eError = PMRCreatePMR(psPhysHeap,
						  uiSize,
						  ui32NumPhysChunks,
						  ui32NumVirtChunks,
						  pui32MappingTable,
						  uiLog2AllocPageSize,
						  uiPMRFlags,
						  pszAnnotation,
						  &_sPMRLMAFuncTab,
						  psPrivData,
						  PMR_TYPE_LMA,
						  &psPMR,
						  ui32PDumpFlags);
	PVR_LOG_GOTO_IF_ERROR(eError, "PMRCreatePMR", errorOnCreate);

	*ppsPMRPtr = psPMR;
	return PVRSRV_OK;

errorOnCreate:
	if (!bOnDemand && psPrivData->iNumChunksAllocated)
	{
		eError2 = _FreeLMPages(psPrivData, NULL, 0);
		PVR_ASSERT(eError2 == PVRSRV_OK);
	}

errorOnAllocPages:
	_FreeLMPageArray(psPrivData);

errorOnAllocPageArray:
errorOnParam:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}
