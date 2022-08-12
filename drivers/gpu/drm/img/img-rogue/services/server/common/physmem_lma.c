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

/* Since 0x0 is a valid DevPAddr, we rely on max 64-bit value to be an invalid
 * page address */
#define INVALID_PAGE_ADDR ~((IMG_UINT64)0x0)

typedef struct _PMR_LMALLOCARRAY_DATA_ {
	IMG_PID uiPid;
	IMG_INT32 iNumPagesAllocated;
	/*
	 * uiTotalNumPages:
	 * Total number of pages supported by this PMR.
	 * (Fixed as of now due the fixed Page table array size)
	 */
	IMG_UINT32 uiTotalNumPages;
	IMG_UINT32 uiPagesToAlloc;

	IMG_UINT32 uiLog2AllocSize;
	IMG_UINT32 uiContigAllocSize;
	IMG_DEV_PHYADDR *pasDevPAddr;

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
} PMR_LMALLOCARRAY_DATA;

#if defined(DEBUG) && defined(SUPPORT_VALIDATION) && defined(__linux__)
/* Global structure to manage GPU memory leak */
static DEFINE_MUTEX(g_sLMALeakMutex);
static IMG_UINT32 g_ui32LMALeakCounter = 0;
#endif

typedef struct PHYSMEM_LMA_DATA_TAG {
	RA_ARENA			*psRA;

	IMG_CPU_PHYADDR		sStartAddr;
	IMG_DEV_PHYADDR		sCardBase;
	IMG_UINT64			uiSize;
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

static PVRSRV_ERROR
PhysmemGetArenaLMA(PHYS_HEAP *psPhysHeap,
				   RA_ARENA **ppsArena)
{
	PHYSMEM_LMA_DATA *psLMAData = (PHYSMEM_LMA_DATA*)PhysHeapGetImplData(psPhysHeap);

	PVR_LOG_RETURN_IF_FALSE(psLMAData != NULL, "psLMAData", PVRSRV_ERROR_NOT_IMPLEMENTED);

	*ppsArena = psLMAData->psRA;

	return PVRSRV_OK;
}

static PVRSRV_ERROR
_CreateArenas(PHEAP_IMPL_DATA pvImplData, IMG_CHAR *pszLabel)
{
	PHYSMEM_LMA_DATA *psLMAData = (PHYSMEM_LMA_DATA*)pvImplData;

	psLMAData->psRA = RA_Create_With_Span(pszLabel,
	                             OSGetPageShift(),
	                             psLMAData->sStartAddr.uiAddr,
	                             psLMAData->sCardBase.uiAddr,
	                             psLMAData->uiSize);
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
		OSFreeMem(psLMAData->psRA);
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
                                    PHYS_HEAP_USAGE_FLAGS ui32Flags,
                                    PHYS_HEAP_ITERATOR **ppsIter)
{
	PVRSRV_ERROR eError;
	PHYSMEM_LMA_DATA *psLMAData;
	PHYS_HEAP_ITERATOR *psHeapIter;
	PHYS_HEAP *psPhysHeap = NULL;
	RA_USAGE_STATS sStats;

	PVR_LOG_RETURN_IF_INVALID_PARAM(ppsIter != NULL, "ppsIter");
	PVR_LOG_RETURN_IF_INVALID_PARAM(psDevNode != NULL, "psDevNode");
	PVR_LOG_RETURN_IF_INVALID_PARAM(ui32Flags != 0, "ui32Flags");

	eError = PhysHeapAcquireByUsage(ui32Flags, psDevNode, &psPhysHeap);
	PVR_LOG_RETURN_IF_ERROR(eError, "PhysHeapAcquireByUsage");

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
#if defined(DEBUG)
	static IMG_UINT32	ui32MaxLog2NumPages = 4;	/* 16 pages => 64KB */
#endif	/* defined(DEBUG) */

	IMG_UINT32 ui32Log2NumPages = 0;

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

	PVR_ASSERT(uiSize == uiActualSize);

	psMemHandle->u.ui64Handle = uiCardAddr;
	psDevPAddr->uiAddr = (IMG_UINT64) uiCardAddr;

	if (PVRSRV_OK == eError)
	{
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
		PVRSRVStatsIncrMemAllocStatAndTrack(PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA,
		                                    uiSize,
		                                    uiCardAddr,
		                                    uiPid);
#else
		IMG_CPU_PHYADDR sCpuPAddr;
		sCpuPAddr.uiAddr = psDevPAddr->uiAddr;

		PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA,
		                             NULL,
		                             sCpuPAddr,
		                             uiSize,
		                             NULL,
		                             uiPid
		                             DEBUG_MEMSTATS_VALUES);
#endif
#endif
#if defined(SUPPORT_GPUVIRT_VALIDATION)
		PVR_DPF((PVR_DBG_MESSAGE,
		        "%s: (GPU Virtualisation) Allocated 0x" IMG_SIZE_FMTSPECX " at 0x%" IMG_UINT64_FMTSPECX ", Arena ID %u",
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
	}

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
									 NULL,
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
							(PHEAP_IMPL_DATA)psLMAData,
							&_sPHEAPImplFuncs,
							ppsPhysHeap);
	if (eError != PVRSRV_OK)
	{
		OSFreeMem(psLMAData);
		return eError;
	}

	eError = _CreateArenas(psLMAData, pszLabel);
	PVR_LOG_RETURN_IF_ERROR(eError, "_CreateArenas");


	return eError;
}

static PVRSRV_ERROR _MapAlloc(PHYS_HEAP *psPhysHeap,
							  IMG_DEV_PHYADDR *psDevPAddr,
							  size_t uiSize,
							  PMR_FLAGS_T ulFlags,
							  void **pvPtr)
{
	IMG_UINT32 ui32CPUCacheFlags;
	IMG_CPU_PHYADDR sCpuPAddr;
	PVRSRV_ERROR eError;

	eError = DevmemCPUCacheMode(PhysHeapDeviceNode(psPhysHeap), ulFlags, &ui32CPUCacheFlags);
	PVR_RETURN_IF_ERROR(eError);

	PhysHeapDevPAddrToCpuPAddr(psPhysHeap, 1, &sCpuPAddr, psDevPAddr);

	*pvPtr = OSMapPhysToLin(sCpuPAddr, uiSize, ui32CPUCacheFlags);
	PVR_RETURN_IF_NOMEM(*pvPtr);

	return PVRSRV_OK;
}

static void _UnMapAlloc(size_t uiSize,
						void *pvPtr)
{
	OSUnMapPhysToLin(pvPtr, uiSize);
}

static PVRSRV_ERROR
_PoisonAlloc(PHYS_HEAP *psPhysHeap,
			 IMG_DEV_PHYADDR *psDevPAddr,
			 IMG_UINT32 uiContigAllocSize,
			 IMG_BYTE ui8PoisonValue)
{
	PVRSRV_ERROR eError;
	void *pvKernLin = NULL;

	eError = _MapAlloc(psPhysHeap,
					   psDevPAddr,
					   uiContigAllocSize,
					   PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC,
					   &pvKernLin);
	PVR_GOTO_IF_ERROR(eError, map_failed);

	OSCachedMemSetWMB(pvKernLin, ui8PoisonValue, uiContigAllocSize);

	_UnMapAlloc(uiContigAllocSize, pvKernLin);

	return PVRSRV_OK;

map_failed:
	PVR_DPF((PVR_DBG_ERROR, "Failed to poison allocation"));
	return eError;
}

static PVRSRV_ERROR
_ZeroAlloc(PHYS_HEAP *psPhysHeap,
		   IMG_DEV_PHYADDR *psDevPAddr,
		   IMG_UINT32 uiContigAllocSize)
{
	void *pvKernLin = NULL;
	PVRSRV_ERROR eError;

	eError = _MapAlloc(psPhysHeap,
					   psDevPAddr,
					   uiContigAllocSize,
					   PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC,
					   &pvKernLin);
	PVR_GOTO_IF_ERROR(eError, map_failed);

	OSCachedMemSetWMB(pvKernLin, 0, uiContigAllocSize);

	_UnMapAlloc(uiContigAllocSize, pvKernLin);

	return PVRSRV_OK;

map_failed:
	PVR_DPF((PVR_DBG_ERROR, "Failed to zero allocation"));
	return eError;
}

static PVRSRV_ERROR
_AllocLMPageArray(PMR_SIZE_T uiSize,
                  IMG_UINT32 ui32NumPhysChunks,
                  IMG_UINT32 ui32NumVirtChunks,
                  IMG_UINT32 *pabMappingTable,
                  IMG_UINT32 uiLog2AllocPageSize,
                  IMG_BOOL bZero,
                  IMG_BOOL bPoisonOnAlloc,
                  IMG_BOOL bPoisonOnFree,
                  IMG_BOOL bContig,
                  IMG_BOOL bOnDemand,
                  PHYS_HEAP* psPhysHeap,
                  PVRSRV_MEMALLOCFLAGS_T uiAllocFlags,
                  IMG_PID uiPid,
                  PMR_LMALLOCARRAY_DATA **ppsPageArrayDataPtr,
                  CONNECTION_DATA *psConnection
                  )
{
	PMR_LMALLOCARRAY_DATA *psPageArrayData = NULL;
	IMG_UINT32 ui32Index;
	PVRSRV_ERROR eError;

	PVR_ASSERT(!bZero || !bPoisonOnAlloc);
	PVR_ASSERT(OSGetPageShift() <= uiLog2AllocPageSize);

	psPageArrayData = OSAllocZMem(sizeof(PMR_LMALLOCARRAY_DATA));
	PVR_GOTO_IF_NOMEM(psPageArrayData, eError, errorOnAllocArray);

	if (bContig)
	{
		/*
			Some allocations require kernel mappings in which case in order
			to be virtually contiguous we also have to be physically contiguous.
		*/
		psPageArrayData->uiTotalNumPages = 1;
		psPageArrayData->uiPagesToAlloc = psPageArrayData->uiTotalNumPages;
		psPageArrayData->uiContigAllocSize = TRUNCATE_64BITS_TO_32BITS(uiSize);
		psPageArrayData->uiLog2AllocSize = uiLog2AllocPageSize;
	}
	else
	{
		IMG_UINT32 uiNumPages;

		/* Use of cast below is justified by the assertion that follows to
		prove that no significant bits have been truncated */
		uiNumPages = (IMG_UINT32)(((uiSize - 1) >> uiLog2AllocPageSize) + 1);
		PVR_ASSERT(((PMR_SIZE_T)uiNumPages << uiLog2AllocPageSize) == uiSize);

		psPageArrayData->uiTotalNumPages = uiNumPages;

		if ((ui32NumVirtChunks != ui32NumPhysChunks) || (1 < ui32NumVirtChunks))
		{
			psPageArrayData->uiPagesToAlloc = ui32NumPhysChunks;
		}
		else
		{
			psPageArrayData->uiPagesToAlloc = uiNumPages;
		}
		psPageArrayData->uiContigAllocSize = 1 << uiLog2AllocPageSize;
		psPageArrayData->uiLog2AllocSize = uiLog2AllocPageSize;
	}
	psPageArrayData->psConnection = psConnection;
	psPageArrayData->uiPid = uiPid;
	psPageArrayData->pasDevPAddr = OSAllocMem(sizeof(IMG_DEV_PHYADDR) *
												psPageArrayData->uiTotalNumPages);
	PVR_GOTO_IF_NOMEM(psPageArrayData->pasDevPAddr, eError, errorOnAllocAddr);

	/* Since no pages are allocated yet, initialise page addresses to INVALID_PAGE_ADDR */
	for (ui32Index = 0; ui32Index < psPageArrayData->uiTotalNumPages; ui32Index++)
	{
		psPageArrayData->pasDevPAddr[ui32Index].uiAddr = INVALID_PAGE_ADDR;
	}

	psPageArrayData->iNumPagesAllocated = 0;
	psPageArrayData->bZeroOnAlloc = bZero;
	psPageArrayData->bPoisonOnAlloc = bPoisonOnAlloc;
	psPageArrayData->bPoisonOnFree = bPoisonOnFree;
	psPageArrayData->bOnDemand = bOnDemand;
	psPageArrayData->psPhysHeap = psPhysHeap;
	psPageArrayData->uiAllocFlags = uiAllocFlags;

	*ppsPageArrayDataPtr = psPageArrayData;

	return PVRSRV_OK;

	/*
	  error exit paths follow:
	*/
errorOnAllocAddr:
	OSFreeMem(psPageArrayData);

errorOnAllocArray:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}


static PVRSRV_ERROR
_AllocLMPages(PMR_LMALLOCARRAY_DATA *psPageArrayData, IMG_UINT32 *pui32MapTable)
{
	PVRSRV_ERROR eError;
	RA_BASE_T uiCardAddr;
	RA_LENGTH_T uiActualSize;
	IMG_UINT32 i, ui32Index = 0;
	IMG_UINT32 uiContigAllocSize;
	IMG_UINT32 uiLog2AllocSize;
	PVRSRV_DEVICE_NODE *psDevNode;
	IMG_BOOL bPoisonOnAlloc;
	IMG_BOOL bZeroOnAlloc;
	RA_ARENA *pArena;

	PVR_ASSERT(NULL != psPageArrayData);
	PVR_ASSERT(0 <= psPageArrayData->iNumPagesAllocated);

	psDevNode = PhysHeapDeviceNode(psPageArrayData->psPhysHeap);
	uiContigAllocSize = psPageArrayData->uiContigAllocSize;
	uiLog2AllocSize = psPageArrayData->uiLog2AllocSize;
	bPoisonOnAlloc = psPageArrayData->bPoisonOnAlloc;
	bZeroOnAlloc = psPageArrayData->bZeroOnAlloc;

	/* Get suitable local memory region for this GPU physheap allocation */
	eError = PhysmemGetArenaLMA(psPageArrayData->psPhysHeap, &pArena);
	PVR_LOG_RETURN_IF_ERROR(eError, "PhysmemGetArenaLMA");

	if (psPageArrayData->uiTotalNumPages <
			(psPageArrayData->iNumPagesAllocated + psPageArrayData->uiPagesToAlloc))
	{
		PVR_DPF((PVR_DBG_ERROR, "Pages requested to allocate don't fit PMR alloc Size. "
				"Allocated: %u + Requested: %u > Total Allowed: %u",
				psPageArrayData->iNumPagesAllocated,
				psPageArrayData->uiPagesToAlloc,
				psPageArrayData->uiTotalNumPages));
		return PVRSRV_ERROR_PMR_BAD_MAPPINGTABLE_SIZE;
	}


#if defined(SUPPORT_GPUVIRT_VALIDATION)
	{
		IMG_UINT32 ui32OSid=0;

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
#endif

	psPageArrayData->psArena = pArena;

	for (i = 0; i < psPageArrayData->uiPagesToAlloc; i++)
	{
		/* This part of index finding should happen before allocating the page.
		 * Just avoiding intricate paths */
		if (psPageArrayData->uiTotalNumPages == psPageArrayData->uiPagesToAlloc)
		{
			ui32Index = i;
		}
		else
		{
			if (NULL == pui32MapTable)
			{
				PVR_LOG_GOTO_WITH_ERROR("pui32MapTable", eError, PVRSRV_ERROR_PMR_INVALID_MAP_INDEX_ARRAY, errorOnRAAlloc);
			}

			ui32Index = pui32MapTable[i];
			if (ui32Index >= psPageArrayData->uiTotalNumPages)
			{
				PVR_DPF((PVR_DBG_ERROR,
						"%s: Page alloc request Index out of bounds for PMR @0x%p",
						__func__,
						psPageArrayData));
				PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_DEVICEMEM_OUT_OF_RANGE, errorOnRAAlloc);
			}

			if (INVALID_PAGE_ADDR != psPageArrayData->pasDevPAddr[ui32Index].uiAddr)
			{
				PVR_LOG_GOTO_WITH_ERROR("Mapping already exists", eError, PVRSRV_ERROR_PMR_MAPPING_ALREADY_EXISTS, errorOnRAAlloc);
			}
		}

		eError = RA_Alloc(pArena,
		                  uiContigAllocSize,
		                  RA_NO_IMPORT_MULTIPLIER,
		                  0,                       /* No flags */
		                  1ULL << uiLog2AllocSize,
		                  "LMA_Page_Alloc",
		                  &uiCardAddr,
		                  &uiActualSize,
		                  NULL);                   /* No private handle */
		if (PVRSRV_OK != eError)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"Failed to Allocate the page @index:%d, size = 0x%llx",
					ui32Index, 1ULL << uiLog2AllocSize));
			PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_PMR_FAILED_TO_ALLOC_PAGES, errorOnRAAlloc);
		}

#if defined(SUPPORT_GPUVIRT_VALIDATION)
{
		PVR_DPF((PVR_DBG_MESSAGE,
				"(GPU Virtualization Validation): Address: 0x%"IMG_UINT64_FMTSPECX,
				uiCardAddr));
}
#endif

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
		/* Allocation is done a page at a time */
		PVRSRVStatsIncrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES, uiActualSize, psPageArrayData->uiPid);
#else
		{
			IMG_CPU_PHYADDR sLocalCpuPAddr;

			sLocalCpuPAddr.uiAddr = (IMG_UINT64)uiCardAddr;
			PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES,
									 NULL,
									 sLocalCpuPAddr,
									 uiActualSize,
									 NULL,
									 psPageArrayData->uiPid
									 DEBUG_MEMSTATS_VALUES);
		}
#endif
#endif

		psPageArrayData->pasDevPAddr[ui32Index].uiAddr = uiCardAddr;
		if (bPoisonOnAlloc)
		{
			eError = _PoisonAlloc(psPageArrayData->psPhysHeap,
								  &psPageArrayData->pasDevPAddr[ui32Index],
								  uiContigAllocSize,
								  PVRSRV_POISON_ON_ALLOC_VALUE);
			PVR_LOG_GOTO_IF_ERROR(eError, "_PoisonAlloc", errorOnPoison);
		}

		if (bZeroOnAlloc)
		{
			eError = _ZeroAlloc(psPageArrayData->psPhysHeap,
								&psPageArrayData->pasDevPAddr[ui32Index],
								uiContigAllocSize);
			PVR_LOG_GOTO_IF_ERROR(eError, "_ZeroAlloc", errorOnZero);
		}
	}
	psPageArrayData->iNumPagesAllocated += psPageArrayData->uiPagesToAlloc;

	return PVRSRV_OK;

	/*
	  error exit paths follow:
	*/
errorOnZero:
errorOnPoison:
	eError = PVRSRV_ERROR_PMR_FAILED_TO_ALLOC_PAGES;
errorOnRAAlloc:
	PVR_DPF((PVR_DBG_ERROR,
			"%s: alloc_pages failed to honour request %d @index: %d of %d pages: (%s)",
			__func__,
			ui32Index,
			i,
			psPageArrayData->uiPagesToAlloc,
			PVRSRVGetErrorString(eError)));
	while (--i < psPageArrayData->uiPagesToAlloc)
	{
		if (psPageArrayData->uiTotalNumPages == psPageArrayData->uiPagesToAlloc)
		{
			ui32Index = i;
		}
		else
		{
			if (NULL == pui32MapTable)
			{
				break;
			}

			ui32Index = pui32MapTable[i];
		}

		if (ui32Index < psPageArrayData->uiTotalNumPages)
		{
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
			/* Allocation is done a page at a time */
			PVRSRVStatsDecrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES,
			                            uiContigAllocSize,
			                            psPageArrayData->uiPid);
#else
			{
				PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES,
				                                psPageArrayData->pasDevPAddr[ui32Index].uiAddr,
				                                psPageArrayData->uiPid);
			}
#endif
#endif
			RA_Free(pArena, psPageArrayData->pasDevPAddr[ui32Index].uiAddr);
			psPageArrayData->pasDevPAddr[ui32Index].uiAddr = INVALID_PAGE_ADDR;
		}
	}
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

static PVRSRV_ERROR
_FreeLMPageArray(PMR_LMALLOCARRAY_DATA *psPageArrayData)
{
	OSFreeMem(psPageArrayData->pasDevPAddr);

	PVR_DPF((PVR_DBG_MESSAGE,
			"physmem_lma.c: freed local memory array structure for PMR @0x%p",
			psPageArrayData));

	OSFreeMem(psPageArrayData);

	return PVRSRV_OK;
}

static PVRSRV_ERROR
_FreeLMPages(PMR_LMALLOCARRAY_DATA *psPageArrayData,
             IMG_UINT32 *pui32FreeIndices,
             IMG_UINT32 ui32FreePageCount)
{
	IMG_UINT32 uiContigAllocSize;
	IMG_UINT32 i, ui32PagesToFree=0, ui32PagesFreed=0, ui32Index=0;
	RA_ARENA *pArena = psPageArrayData->psArena;

	PVR_ASSERT(psPageArrayData->iNumPagesAllocated != 0);

	uiContigAllocSize = psPageArrayData->uiContigAllocSize;

	ui32PagesToFree = (NULL == pui32FreeIndices) ?
			psPageArrayData->uiTotalNumPages : ui32FreePageCount;

	for (i = 0; i < ui32PagesToFree; i++)
	{
		if (NULL == pui32FreeIndices)
		{
			ui32Index = i;
		}
		else
		{
			ui32Index = pui32FreeIndices[i];
		}

		if (INVALID_PAGE_ADDR != psPageArrayData->pasDevPAddr[ui32Index].uiAddr)
		{
			ui32PagesFreed++;
			if (psPageArrayData->bPoisonOnFree)
			{
				_PoisonAlloc(psPageArrayData->psPhysHeap,
							 &psPageArrayData->pasDevPAddr[ui32Index],
							 uiContigAllocSize,
							 PVRSRV_POISON_ON_FREE_VALUE);
			}

			RA_Free(pArena,	psPageArrayData->pasDevPAddr[ui32Index].uiAddr);

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
			/* Allocation is done a page at a time */
			PVRSRVStatsDecrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES,
			                            uiContigAllocSize,
			                            psPageArrayData->uiPid);
#else
			{
				PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES,
				                                psPageArrayData->pasDevPAddr[ui32Index].uiAddr,
				                                psPageArrayData->uiPid);
			}
#endif
#endif
			psPageArrayData->pasDevPAddr[ui32Index].uiAddr = INVALID_PAGE_ADDR;
		}
	}
	psPageArrayData->iNumPagesAllocated -= ui32PagesFreed;

	PVR_ASSERT(0 <= psPageArrayData->iNumPagesAllocated);

	PVR_DPF((PVR_DBG_MESSAGE,
			"%s: freed %d local memory for PMR @0x%p",
			__func__,
			(ui32PagesFreed * uiContigAllocSize),
			psPageArrayData));

	return PVRSRV_OK;
}

/*
 *
 * Implementation of callback functions
 *
 */

/* destructor func is called after last reference disappears, but
   before PMR itself is freed. */
static PVRSRV_ERROR
PMRFinalizeLocalMem(PMR_IMPL_PRIVDATA pvPriv)
{
	PVRSRV_ERROR eError;
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData = NULL;

	psLMAllocArrayData = pvPriv;

	/* We can't free pages until now. */
	if (psLMAllocArrayData->iNumPagesAllocated != 0)
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
			return PVRSRV_OK;
		}

		mutex_unlock(&g_sLMALeakMutex);
#endif
		eError = _FreeLMPages(psLMAllocArrayData, NULL, 0);
		PVR_ASSERT (eError == PVRSRV_OK); /* can we do better? */
	}

	eError = _FreeLMPageArray(psLMAllocArrayData);
	PVR_ASSERT (eError == PVRSRV_OK); /* can we do better? */

	return PVRSRV_OK;
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
	IMG_UINT32 idx;
	IMG_UINT32 uiLog2AllocSize;
	IMG_UINT32 uiNumAllocs;
	IMG_UINT64 uiAllocIndex;
	IMG_DEVMEM_OFFSET_T uiInAllocOffset;
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData = pvPriv;

	if (psLMAllocArrayData->uiLog2AllocSize < ui32Log2PageSize)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Requested physical addresses from PMR "
		         "for incompatible contiguity %u!",
		         __func__,
		         ui32Log2PageSize));
		return PVRSRV_ERROR_PMR_INCOMPATIBLE_CONTIGUITY;
	}

	uiNumAllocs = psLMAllocArrayData->uiTotalNumPages;
	if (uiNumAllocs > 1)
	{
		PVR_ASSERT(psLMAllocArrayData->uiLog2AllocSize != 0);
		uiLog2AllocSize = psLMAllocArrayData->uiLog2AllocSize;

		for (idx=0; idx < ui32NumOfPages; idx++)
		{
			if (pbValid[idx])
			{
				uiAllocIndex = puiOffset[idx] >> uiLog2AllocSize;
				uiInAllocOffset = puiOffset[idx] - (uiAllocIndex << uiLog2AllocSize);

				PVR_LOG_RETURN_IF_FALSE(uiAllocIndex < uiNumAllocs,
				                        "puiOffset out of range", PVRSRV_ERROR_OUT_OF_RANGE);

				PVR_ASSERT(uiInAllocOffset < (1ULL << uiLog2AllocSize));

				psDevPAddr[idx].uiAddr = psLMAllocArrayData->pasDevPAddr[uiAllocIndex].uiAddr + uiInAllocOffset;
			}
		}
	}
	else
	{
		for (idx=0; idx < ui32NumOfPages; idx++)
		{
			if (pbValid[idx])
			{
				psDevPAddr[idx].uiAddr = psLMAllocArrayData->pasDevPAddr[0].uiAddr + puiOffset[idx];
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
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData = NULL;
	void *pvKernLinAddr = NULL;
	IMG_UINT32 ui32PageIndex = 0;
	size_t uiOffsetMask = uiOffset;

	psLMAllocArrayData = pvPriv;

	/* Check that we can map this in contiguously */
	if (psLMAllocArrayData->uiTotalNumPages != 1)
	{
		size_t uiStart = uiOffset;
		size_t uiEnd = uiOffset + uiSize - 1;
		size_t uiPageMask = ~((1 << psLMAllocArrayData->uiLog2AllocSize) - 1);

		/* We can still map if only one page is required */
		if ((uiStart & uiPageMask) != (uiEnd & uiPageMask))
		{
			PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_PMR_INCOMPATIBLE_CONTIGUITY, e0);
		}

		/* Locate the desired physical page to map in */
		ui32PageIndex = uiOffset >> psLMAllocArrayData->uiLog2AllocSize;
		uiOffsetMask = (1U << psLMAllocArrayData->uiLog2AllocSize) - 1;
	}

	PVR_ASSERT(ui32PageIndex < psLMAllocArrayData->uiTotalNumPages);

	eError = _MapAlloc(psLMAllocArrayData->psPhysHeap,
						&psLMAllocArrayData->pasDevPAddr[ui32PageIndex],
						psLMAllocArrayData->uiContigAllocSize,
						ulFlags,
						&pvKernLinAddr);

	*ppvKernelAddressOut = ((IMG_CHAR *) pvKernLinAddr) + (uiOffset & uiOffsetMask);
	*phHandleOut = pvKernLinAddr;

	return eError;

	/*
	  error exit paths follow:
	*/
e0:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

static void PMRReleaseKernelMappingDataLocalMem(PMR_IMPL_PRIVDATA pvPriv,
												 IMG_HANDLE hHandle)
{
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData = NULL;
	void *pvKernLinAddr = NULL;

	psLMAllocArrayData = (PMR_LMALLOCARRAY_DATA *) pvPriv;
	pvKernLinAddr = (void *) hHandle;

	_UnMapAlloc(psLMAllocArrayData->uiContigAllocSize,
				pvKernLinAddr);
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
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData = NULL;
	size_t uiBytesCopied;
	size_t uiBytesToCopy;
	size_t uiBytesCopyableFromAlloc;
	void *pvMapping = NULL;
	IMG_UINT8 *pcKernelPointer = NULL;
	size_t uiBufferOffset;
	IMG_UINT64 uiAllocIndex;
	IMG_DEVMEM_OFFSET_T uiInAllocOffset;
	PVRSRV_ERROR eError;

	psLMAllocArrayData = pvPriv;

	uiBytesCopied = 0;
	uiBytesToCopy = uiBufSz;
	uiBufferOffset = 0;

	if (psLMAllocArrayData->uiTotalNumPages > 1)
	{
		while (uiBytesToCopy > 0)
		{
			/* we have to map one alloc in at a time */
			PVR_ASSERT(psLMAllocArrayData->uiLog2AllocSize != 0);
			uiAllocIndex = uiOffset >> psLMAllocArrayData->uiLog2AllocSize;
			uiInAllocOffset = uiOffset - (uiAllocIndex << psLMAllocArrayData->uiLog2AllocSize);
			uiBytesCopyableFromAlloc = uiBytesToCopy;
			if (uiBytesCopyableFromAlloc + uiInAllocOffset > (1ULL << psLMAllocArrayData->uiLog2AllocSize))
			{
				uiBytesCopyableFromAlloc = TRUNCATE_64BITS_TO_SIZE_T((1ULL << psLMAllocArrayData->uiLog2AllocSize)-uiInAllocOffset);
			}

			PVR_ASSERT(uiBytesCopyableFromAlloc != 0);
			PVR_ASSERT(uiAllocIndex < psLMAllocArrayData->uiTotalNumPages);
			PVR_ASSERT(uiInAllocOffset < (1ULL << psLMAllocArrayData->uiLog2AllocSize));

			eError = _MapAlloc(psLMAllocArrayData->psPhysHeap,
								&psLMAllocArrayData->pasDevPAddr[uiAllocIndex],
								psLMAllocArrayData->uiContigAllocSize,
								PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC,
								&pvMapping);
			PVR_GOTO_IF_ERROR(eError, e0);
			pcKernelPointer = pvMapping;
			pfnCopyBytes(&pcBuffer[uiBufferOffset], &pcKernelPointer[uiInAllocOffset], uiBytesCopyableFromAlloc);

			_UnMapAlloc(psLMAllocArrayData->uiContigAllocSize,
						pvMapping);

			uiBufferOffset += uiBytesCopyableFromAlloc;
			uiBytesToCopy -= uiBytesCopyableFromAlloc;
			uiOffset += uiBytesCopyableFromAlloc;
			uiBytesCopied += uiBytesCopyableFromAlloc;
		}
	}
	else
	{
			PVR_ASSERT((uiOffset + uiBufSz) <= psLMAllocArrayData->uiContigAllocSize);
			PVR_ASSERT(psLMAllocArrayData->uiContigAllocSize != 0);
			eError = _MapAlloc(psLMAllocArrayData->psPhysHeap,
								&psLMAllocArrayData->pasDevPAddr[0],
								psLMAllocArrayData->uiContigAllocSize,
								PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC,
								&pvMapping);
			PVR_GOTO_IF_ERROR(eError, e0);
			pcKernelPointer = pvMapping;
			pfnCopyBytes(pcBuffer, &pcKernelPointer[uiOffset], uiBufSz);

			_UnMapAlloc(psLMAllocArrayData->uiContigAllocSize,
						pvMapping);

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
	IMG_DEV_PHYADDR sPhyAddr;

#if defined(DEBUG)
	IMG_BOOL bPoisonFail = IMG_FALSE;
	IMG_BOOL bZeroFail = IMG_FALSE;
#endif

	/* Fetch the Page table array represented by the PMR */
	IMG_DEV_PHYADDR *psPageArray = psPMRPageArrayData->pasDevPAddr;
	PMR_MAPPING_TABLE *psPMRMapTable = PMR_GetMappingTable(psPMR);

	/* The incoming request is classified into two operations independent of
	 * each other: alloc & free pages.
	 * These operations can be combined with two mapping operations as well
	 * which are GPU & CPU space mappings.
	 *
	 * From the alloc and free page requests, the net amount of pages to be
	 * allocated or freed is computed. Pages that were requested to be freed
	 * will be reused to fulfil alloc requests.
	 *
	 * The order of operations is:
	 * 1. Allocate new pages from the OS
	 * 2. Move the free pages from free request to alloc positions.
	 * 3. Free the rest of the pages not used for alloc
	 *
	 * Alloc parameters are validated at the time of allocation
	 * and any error will be handled then. */

	if (SPARSE_RESIZE_BOTH == (uiFlags & SPARSE_RESIZE_BOTH))
	{
		ui32CommonRequstCount = (ui32AllocPageCount > ui32FreePageCount) ?
				ui32FreePageCount : ui32AllocPageCount;

		PDUMP_PANIC(PMR_DeviceNode(psPMR), SPARSEMEM_SWAP, "Request to swap alloc & free pages not supported");
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
			if (NULL != pai32FreeIndices)
			{
				for (ui32Loop = 0; ui32Loop < ui32FreePageCount; ui32Loop++)
				{
					uiFreepgidx = pai32FreeIndices[ui32Loop];

					if (uiFreepgidx > psPMRPageArrayData->uiTotalNumPages)
					{
						PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_DEVICEMEM_OUT_OF_RANGE, e0);
					}

					if (INVALID_PAGE_ADDR == psPageArray[uiFreepgidx].uiAddr)
					{
						PVR_LOG_GOTO_WITH_ERROR("psPageArray[uiFreepgidx].uiAddr", eError, PVRSRV_ERROR_INVALID_PARAMS, e0);
					}
				}
			}else
			{
				PVR_DPF((PVR_DBG_ERROR,
				         "%s: Given non-zero free count but missing indices array",
				         __func__));
				return PVRSRV_ERROR_INVALID_PARAMS;
			}
		}

		/*The following block of code verifies any issues with common alloc page indices */
		for (ui32Loop = ui32AdtnlAllocPages; ui32Loop < ui32AllocPageCount; ui32Loop++)
		{
			uiAllocpgidx = pai32AllocIndices[ui32Loop];
			if (uiAllocpgidx > psPMRPageArrayData->uiTotalNumPages)
			{
				PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_DEVICEMEM_OUT_OF_RANGE, e0);
			}

			if (SPARSE_REMAP_MEM != (uiFlags & SPARSE_REMAP_MEM))
			{
				if ((INVALID_PAGE_ADDR != psPageArray[uiAllocpgidx].uiAddr) ||
						(TRANSLATION_INVALID != psPMRMapTable->aui32Translation[uiAllocpgidx]))
				{
					PVR_LOG_GOTO_WITH_ERROR("Trying to allocate already allocated page again", eError, PVRSRV_ERROR_INVALID_PARAMS, e0);
				}
			}
			else
			{
				if ((INVALID_PAGE_ADDR == psPageArray[uiAllocpgidx].uiAddr) ||
				    (TRANSLATION_INVALID == psPMRMapTable->aui32Translation[uiAllocpgidx]))
				{
					PVR_LOG_GOTO_WITH_ERROR("Unable to remap memory due to missing page", eError, PVRSRV_ERROR_INVALID_PARAMS, e0);
				}
			}
		}


		ui32Loop = 0;

		/* Allocate new pages */
		if (0 != ui32AdtnlAllocPages)
		{
			/* Say how many pages to allocate */
			psPMRPageArrayData->uiPagesToAlloc = ui32AdtnlAllocPages;

			eError = _AllocLMPages(psPMRPageArrayData, pai32AllocIndices);
			PVR_LOG_GOTO_IF_ERROR(eError, "_AllocLMPages", e0);

			/* Mark the corresponding pages of translation table as valid */
			for (ui32Loop = 0; ui32Loop < ui32AdtnlAllocPages; ui32Loop++)
			{
				psPMRMapTable->aui32Translation[pai32AllocIndices[ui32Loop]] = pai32AllocIndices[ui32Loop];
			}

			psPMRMapTable->ui32NumPhysChunks += ui32AdtnlAllocPages;
		}

		ui32Index = ui32Loop;

		/* Move the corresponding free pages to alloc request */
		for (ui32Loop = 0; ui32Loop < ui32CommonRequstCount; ui32Loop++, ui32Index++)
		{

			uiAllocpgidx = pai32AllocIndices[ui32Index];
			uiFreepgidx  = pai32FreeIndices[ui32Loop];
			sPhyAddr = psPageArray[uiAllocpgidx];
			psPageArray[uiAllocpgidx] = psPageArray[uiFreepgidx];

			/* Is remap mem used in real world scenario? Should it be turned to a
			 *  debug feature? The condition check needs to be out of loop, will be
			 *  done at later point though after some analysis */
			if (SPARSE_REMAP_MEM != (uiFlags & SPARSE_REMAP_MEM))
			{
				psPMRMapTable->aui32Translation[uiFreepgidx] = TRANSLATION_INVALID;
				psPMRMapTable->aui32Translation[uiAllocpgidx] = uiAllocpgidx;
				psPageArray[uiFreepgidx].uiAddr = INVALID_PAGE_ADDR;
			}
			else
			{
				psPageArray[uiFreepgidx] = sPhyAddr;
				psPMRMapTable->aui32Translation[uiFreepgidx] = uiFreepgidx;
				psPMRMapTable->aui32Translation[uiAllocpgidx] = uiAllocpgidx;
			}

			/* Be sure to honour the attributes associated with the allocation
			 * such as zeroing, poisoning etc. */
			if (psPMRPageArrayData->bPoisonOnAlloc)
			{
				eError = _PoisonAlloc(psPMRPageArrayData->psPhysHeap,
				                      &psPMRPageArrayData->pasDevPAddr[uiAllocpgidx],
				                      psPMRPageArrayData->uiContigAllocSize,
				                      PVRSRV_POISON_ON_ALLOC_VALUE);

				/* Consider this as a soft failure and go ahead but log error to kernel log */
				if (eError != PVRSRV_OK)
				{
#if defined(DEBUG)
					bPoisonFail = IMG_TRUE;
#endif
				}
			}
			else
			{
				if (psPMRPageArrayData->bZeroOnAlloc)
				{
					eError = _ZeroAlloc(psPMRPageArrayData->psPhysHeap,
					                    &psPMRPageArrayData->pasDevPAddr[uiAllocpgidx],
					                    psPMRPageArrayData->uiContigAllocSize);
					/* Consider this as a soft failure and go ahead but log error to kernel log */
					if (eError != PVRSRV_OK)
					{
#if defined(DEBUG)
						/*Don't think we need to zero any pages further*/
						bZeroFail = IMG_TRUE;
#endif
					}
				}
			}
		}

		/*Free the additional free pages */
		if (0 != ui32AdtnlFreePages)
		{
			ui32Index = ui32Loop;
			_FreeLMPages(psPMRPageArrayData, &pai32FreeIndices[ui32Loop], ui32AdtnlFreePages);
			ui32Loop = 0;

			while (ui32Loop++ < ui32AdtnlFreePages)
			{
				/*Set the corresponding mapping table entry to invalid address */
				psPMRMapTable->aui32Translation[pai32FreeIndices[ui32Index++]] = TRANSLATION_INVALID;
			}

			psPMRMapTable->ui32NumPhysChunks -= ui32AdtnlFreePages;
		}

	}

#if defined(DEBUG)
	if (IMG_TRUE == bPoisonFail)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Error in poisoning the page", __func__));
	}

	if (IMG_TRUE == bZeroFail)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Error in zeroing the page", __func__));
	}
#endif

	/* Update the PMR memory holding information */
	eError = PVRSRV_OK;

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

	/*Get the base address of the heap */
	eError = PMR_CpuPhysAddr(psPMR,
	                         psPMRPageArrayData->uiLog2AllocSize,
	                         1,
	                         0,	/* offset zero here mean first page in the PMR */
	                         &sCpuAddrPtr,
	                         &bValid);
	PVR_LOG_RETURN_IF_ERROR(eError, "PMR_CpuPhysAddr");

	/* Phys address of heap is computed here by subtracting the offset of this page
	 * basically phys address of any page = Base address of heap + offset of the page */
	sCpuAddrPtr.uiAddr -= psPMRPageArrayData->pasDevPAddr[0].uiAddr;
	psPageArray = psPMRPageArrayData->pasDevPAddr;

	return OSChangeSparseMemCPUAddrMap((void **)psPageArray,
	                                   sCpuVABase,
	                                   sCpuAddrPtr,
	                                   ui32AllocPageCount,
	                                   pai32AllocIndices,
	                                   ui32FreePageCount,
	                                   pai32FreeIndices,
	                                   IMG_TRUE);
}

static PMR_IMPL_FUNCTAB _sPMRLMAFuncTab = {
	/* pfnLockPhysAddresses */
	&PMRLockSysPhysAddressesLocalMem,
	/* pfnUnlockPhysAddresses */
	&PMRUnlockSysPhysAddressesLocalMem,
	/* pfnDevPhysAddr */
	&PMRSysPhysAddrLocalMem,
	/* pfnAcquireKernelMappingData */
	&PMRAcquireKernelMappingDataLocalMem,
	/* pfnReleaseKernelMappingData */
	&PMRReleaseKernelMappingDataLocalMem,
	/* pfnReadBytes */
	&PMRReadBytesLocalMem,
	/* pfnWriteBytes */
	&PMRWriteBytesLocalMem,
	/* pfnUnpinMem */
	NULL,
	/* pfnPinMem */
	NULL,
	/* pfnChangeSparseMem*/
	&PMRChangeSparseMemLocalMem,
	/* pfnChangeSparseMemCPUMap */
	&PMRChangeSparseMemCPUMapLocalMem,
	/* pfnMMap */
	NULL,
	/* pfnFinalize */
	&PMRFinalizeLocalMem
};

PVRSRV_ERROR
PhysmemNewLocalRamBackedPMR(PHYS_HEAP *psPhysHeap,
							CONNECTION_DATA *psConnection,
                            IMG_DEVMEM_SIZE_T uiSize,
                            IMG_DEVMEM_SIZE_T uiChunkSize,
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
	IMG_BOOL bContig;

	/* For sparse requests we have to do the allocation
	 * in chunks rather than requesting one contiguous block */
	if (ui32NumPhysChunks != ui32NumVirtChunks || ui32NumVirtChunks > 1)
	{
		if (PVRSRV_CHECK_KERNEL_CPU_MAPPABLE(uiFlags))
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: LMA kernel mapping functions currently "
					"don't work with discontiguous memory.",
					__func__));
			PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_INVALID_PARAMS, errorOnParam);
		}
		bContig = IMG_FALSE;
	}
	else
	{
		bContig = IMG_TRUE;
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
	eError = _AllocLMPageArray(uiChunkSize * ui32NumVirtChunks,
	                           ui32NumPhysChunks,
	                           ui32NumVirtChunks,
	                           pui32MappingTable,
	                           uiLog2AllocPageSize,
	                           bZero,
	                           bPoisonOnAlloc,
	                           bPoisonOnFree,
	                           bContig,
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
						  uiChunkSize,
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
	if (!bOnDemand && psPrivData->iNumPagesAllocated)
	{
		eError2 = _FreeLMPages(psPrivData, NULL, 0);
		PVR_ASSERT(eError2 == PVRSRV_OK);
	}

errorOnAllocPages:
	eError2 = _FreeLMPageArray(psPrivData);
	PVR_ASSERT(eError2 == PVRSRV_OK);

errorOnAllocPageArray:
errorOnParam:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}
