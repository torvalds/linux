/*************************************************************************/ /*!
@File
@Title          Implementation of PMR functions for OS managed memory
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Part of the memory management.  This module is responsible for
                implementing the function callbacks for physical memory borrowed
                from that normally managed by the operating system.
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
#include <linux/version.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/mm_types.h>
#include <linux/vmalloc.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/atomic.h>
#include <asm/io.h>
#if defined(CONFIG_X86)
#include <asm/cacheflush.h>
#endif

/* include5/ */
#include "img_types.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"
#include "rgx_pdump_panics.h"
/* services/server/include/ */
#include "allocmem.h"
#include "osfunc.h"
#include "pdump_km.h"
#include "pmr.h"
#include "pmr_impl.h"
#include "devicemem_server_utils.h"

/* ourselves */
#include "physmem_osmem.h"
#include "physmem_osmem_linux.h"

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#include "process_stats.h"
#endif

#include "kernel_compatibility.h"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0))
static IMG_UINT32 g_uiMaxOrder = PVR_LINUX_PHYSMEM_MAX_ALLOC_ORDER_NUM;
#else
/* split_page not available on older kernels */
#undef PVR_LINUX_PHYSMEM_MAX_ALLOC_ORDER_NUM
#define PVR_LINUX_PHYSMEM_MAX_ALLOC_ORDER_NUM 0
static IMG_UINT32 g_uiMaxOrder = 0;
#endif

/* Get/Set/Mask out alloc_page/dma_alloc flag */
#define DMA_GET_ADDR(x)			(((x) >> 1) << 1)
#define DMA_SET_ALLOCPG_ADDR(x)	((x) | ((dma_addr_t)0x01))
#define DMA_IS_ALLOCPG_ADDR(x)	((x) & ((dma_addr_t)0x01))

typedef struct _PMR_OSPAGEARRAY_DATA_ {
	/* Device for which this allocation has been made */
	PVRSRV_DEVICE_NODE *psDevNode;

	/*
	 * iNumPagesAllocated:
	 * Number of pages allocated in this PMR so far.
	 * This allows for up to (2^31 - 1) pages. With 4KB pages, that's 8TB of memory for each PMR.
	 */
	IMG_INT32 iNumPagesAllocated;

	/*
	 * uiTotalNumPages:
	 * Total number of pages supported by this PMR. (Fixed as of now due the fixed Page table array size)
	 *  number of "pages" (a.k.a. macro pages, compound pages, higher order pages, etc...)
	 */
	IMG_UINT32 uiTotalNumPages;

	/*
	  uiLog2PageSize;
	
	  size of each "page" -- this would normally be the same as
	  PAGE_SHIFT, but we support the idea that we may allocate pages
	  in larger chunks for better contiguity, using order>0 in the
	  call to alloc_pages()
	*/
	IMG_UINT32 uiLog2DevPageSize;

	/*
	  For non DMA/CMA allocation, pagearray references the pages
	  thus allocated; one entry per compound page when compound
	  pages are used. In addition, for DMA/CMA allocations, we
	  track the returned cpu virtual and device bus address.
	*/
	struct page **pagearray;
	dma_addr_t *dmaphysarray;
	void **dmavirtarray;


	/*
	  record at alloc time whether poisoning will be required when the
	  PMR is freed.
	*/
	IMG_BOOL bZero;
	IMG_BOOL bPoisonOnFree;
	IMG_BOOL bPoisonOnAlloc;
	IMG_BOOL bOnDemand;
	IMG_BOOL bUnpinned; /* Should be protected by page pool lock */
	IMG_BOOL bIsCMA; /* Is CMA memory allocated via DMA framework */

	/*
	  The cache mode of the PMR. Additionally carrying the CPU-Cache-Clean
	  flag, advising us to do cache maintenance on behalf of the caller.
	  NOTE: For DMA/CMA allocations, memory is _always_ uncached.

	  Boolean used to track if we need to revert the cache attributes
	  of the pages used in this allocation. Depends on OS/architecture.
	*/
	IMG_UINT32 ui32CPUCacheFlags;
	IMG_BOOL bUnsetMemoryType;
} PMR_OSPAGEARRAY_DATA;

/***********************************
 * Page pooling for uncached pages *
 ***********************************/

static INLINE void
_FreeOSPage_CMA(struct device *dev,
				size_t alloc_size,
				IMG_UINT32 uiOrder,
				void *virt_addr,
				dma_addr_t dev_addr,
				struct page *psPage);

static void
_FreeOSPage(IMG_UINT32 uiOrder,
			IMG_BOOL bUnsetMemoryType,
			struct page *psPage);

static PVRSRV_ERROR
_FreeOSPages(PMR_OSPAGEARRAY_DATA *psPageArrayData,
			IMG_UINT32 *pai32FreeIndices,
			IMG_UINT32 ui32FreePageCount);

static PVRSRV_ERROR
_FreePagesFromPoolUnlocked(IMG_UINT32 uiMaxPagesToFree,
						   IMG_UINT32 *puiPagesFreed);

static inline void
_ApplyCacheMaintenance(PVRSRV_DEVICE_NODE *psDevNode,
					struct page **ppsPage,
					IMG_UINT32 uiNumPages,
					IMG_BOOL bFlush);

static inline PVRSRV_ERROR
_ApplyOSPagesAttribute(PVRSRV_DEVICE_NODE *psDevNode,
					struct page **ppsPage,
					IMG_UINT32 uiNumPages,
					IMG_BOOL bFlush,
					IMG_UINT32 ui32CPUCacheFlags);

/* A struct for our page pool holding an array of pages.
 * We always put units of page arrays to the pool but are
 * able to take individual pages */
typedef	struct
{
	/* Linkage for page pool LRU list */
	struct list_head sPagePoolItem;

	/* How many items are still in the page array */
	IMG_UINT32 uiItemsRemaining;
	struct page **ppsPageArray;

} LinuxPagePoolEntry;

/* A struct for the unpinned items */
typedef struct
{
	struct list_head sUnpinPoolItem;
	PMR_OSPAGEARRAY_DATA *psPageArrayDataPtr;
} LinuxUnpinEntry;

/* Caches to hold page pool and page array structures */
static struct kmem_cache *g_psLinuxPagePoolCache = NULL;
static struct kmem_cache *g_psLinuxPageArray = NULL;

/* Track what is live */
static IMG_UINT32 g_ui32UnpinPageCount = 0;
static IMG_UINT32 g_ui32PagePoolEntryCount = 0;

/* Pool entry limits */
#if defined(PVR_LINUX_PHYSMEM_MAX_POOL_PAGES)
static const IMG_UINT32 g_ui32PagePoolMaxEntries = PVR_LINUX_PHYSMEM_MAX_POOL_PAGES;
static const IMG_UINT32 g_ui32PagePoolMaxEntries_5Percent= PVR_LINUX_PHYSMEM_MAX_POOL_PAGES / 20;
#else
static const IMG_UINT32 g_ui32PagePoolMaxEntries = 0;
static const IMG_UINT32 g_ui32PagePoolMaxEntries_5Percent = 0;
#endif

#if defined(PVR_LINUX_PHYSMEM_MAX_EXCESS_POOL_PAGES)
static const IMG_UINT32 g_ui32PagePoolMaxExcessEntries = PVR_LINUX_PHYSMEM_MAX_EXCESS_POOL_PAGES;
#else
static const IMG_UINT32 g_ui32PagePoolMaxExcessEntries = 0;
#endif

#if defined(CONFIG_X86)
#define PHYSMEM_OSMEM_NUM_OF_POOLS 3
static const IMG_UINT32 g_aui32CPUCacheFlags[PHYSMEM_OSMEM_NUM_OF_POOLS] = {
	PVRSRV_MEMALLOCFLAG_CPU_CACHED,
	PVRSRV_MEMALLOCFLAG_CPU_UNCACHED,
	PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE
};
#else
#define PHYSMEM_OSMEM_NUM_OF_POOLS 2
static const IMG_UINT32 g_aui32CPUCacheFlags[PHYSMEM_OSMEM_NUM_OF_POOLS] = {
	PVRSRV_MEMALLOCFLAG_CPU_CACHED,
	PVRSRV_MEMALLOCFLAG_CPU_UNCACHED
};
#endif

/* Global structures we use to manage the page pool */
static DEFINE_MUTEX(g_sPagePoolMutex);

/* List holding the page array pointers: */
static LIST_HEAD(g_sPagePoolList_WB);
static LIST_HEAD(g_sPagePoolList_WC);
static LIST_HEAD(g_sPagePoolList_UC);
static LIST_HEAD(g_sUnpinList);

static inline void
_PagePoolLock(void)
{
	mutex_lock(&g_sPagePoolMutex);
}

static inline int
_PagePoolTrylock(void)
{
	return mutex_trylock(&g_sPagePoolMutex);
}

static inline void
_PagePoolUnlock(void)
{
	mutex_unlock(&g_sPagePoolMutex);
}

static PVRSRV_ERROR
_AddUnpinListEntryUnlocked(PMR_OSPAGEARRAY_DATA *psOSPageArrayData)
{
	LinuxUnpinEntry *psUnpinEntry;

	psUnpinEntry = OSAllocMem(sizeof(*psUnpinEntry));
	if (!psUnpinEntry)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: OSAllocMem failed. Cannot add entry to unpin list.",
				__func__));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psUnpinEntry->psPageArrayDataPtr = psOSPageArrayData;

	/* Add into pool that the shrinker can access easily*/
	list_add_tail(&psUnpinEntry->sUnpinPoolItem, &g_sUnpinList);

	g_ui32UnpinPageCount += psOSPageArrayData->iNumPagesAllocated;

	return PVRSRV_OK;
}

static void
_RemoveUnpinListEntryUnlocked(PMR_OSPAGEARRAY_DATA *psOSPageArrayData)
{
	LinuxUnpinEntry *psUnpinEntry, *psTempUnpinEntry;

	/* Remove from pool */
	list_for_each_entry_safe(psUnpinEntry,
	                         psTempUnpinEntry,
	                         &g_sUnpinList,
	                         sUnpinPoolItem)
	{
		if (psUnpinEntry->psPageArrayDataPtr == psOSPageArrayData)
		{
			list_del(&psUnpinEntry->sUnpinPoolItem);
			break;
		}
	}

	OSFreeMem(psUnpinEntry);

	g_ui32UnpinPageCount -= psOSPageArrayData->iNumPagesAllocated;
}

static inline IMG_BOOL
_GetPoolListHead(IMG_UINT32 ui32CPUCacheFlags,
				 struct list_head **ppsPoolHead)
{
	switch(PVRSRV_CPU_CACHE_MODE(ui32CPUCacheFlags))
	{
		case PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE:
#if defined(CONFIG_X86)
		/*
			For x86 we need to keep different lists for uncached
			and write-combined as we must always honour the PAT
			setting which cares about this difference.
		*/

			*ppsPoolHead = &g_sPagePoolList_WC;
			break;
#endif

		case PVRSRV_MEMALLOCFLAG_CPU_UNCACHED:
			*ppsPoolHead = &g_sPagePoolList_UC;
			break;

		case PVRSRV_MEMALLOCFLAG_CPU_CACHED:
			*ppsPoolHead = &g_sPagePoolList_WB;
			break;

		default:
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to get pages from pool, "
					 "unknown CPU caching mode.", __func__));
			return IMG_FALSE;
	}
	return IMG_TRUE;
}

static struct shrinker g_sShrinker;

/* Returning the number of pages that still reside in the page pool.
 * Do not count excess pages that will be freed by the defer free thread. */
static unsigned long
_GetNumberOfPagesInPoolUnlocked(void)
{
	unsigned int uiEntryCount;

	uiEntryCount = (g_ui32PagePoolEntryCount > g_ui32PagePoolMaxEntries) ? g_ui32PagePoolMaxEntries : g_ui32PagePoolEntryCount;
	return uiEntryCount + g_ui32UnpinPageCount;
}

/* Linux shrinker function that informs the OS about how many pages we are caching and
 * it is able to reclaim. */
static unsigned long
_CountObjectsInPagePool(struct shrinker *psShrinker, struct shrink_control *psShrinkControl)
{
	int remain;

	PVR_ASSERT(psShrinker == &g_sShrinker);
	(void)psShrinker;
	(void)psShrinkControl;

	/* In order to avoid possible deadlock use mutex_trylock in place of mutex_lock */
	if (_PagePoolTrylock() == 0)
		return 0;
	remain = _GetNumberOfPagesInPoolUnlocked();
	_PagePoolUnlock();

	return remain;
}

/* Linux shrinker function to reclaim the pages from our page pool */
static unsigned long
_ScanObjectsInPagePool(struct shrinker *psShrinker, struct shrink_control *psShrinkControl)
{
	unsigned long uNumToScan = psShrinkControl->nr_to_scan;
	unsigned long uSurplus = 0;
	LinuxUnpinEntry *psUnpinEntry, *psTempUnpinEntry;
	IMG_UINT32 uiPagesFreed;

	PVR_ASSERT(psShrinker == &g_sShrinker);
	(void)psShrinker;

	/* In order to avoid possible deadlock use mutex_trylock in place of mutex_lock */
	if (_PagePoolTrylock() == 0)
		return SHRINK_STOP;

	_FreePagesFromPoolUnlocked(uNumToScan,
							   &uiPagesFreed);
	uNumToScan -= uiPagesFreed;

	if (uNumToScan == 0)
	{
		goto e_exit;
	}

	/* Free unpinned memory, starting with LRU entries */
	list_for_each_entry_safe(psUnpinEntry,
							 psTempUnpinEntry,
							 &g_sUnpinList,
							 sUnpinPoolItem)
	{
		PMR_OSPAGEARRAY_DATA *psPageArrayDataPtr = psUnpinEntry->psPageArrayDataPtr;
		IMG_UINT32 uiNumPages = (psPageArrayDataPtr->uiTotalNumPages > psPageArrayDataPtr->iNumPagesAllocated)?
								psPageArrayDataPtr->iNumPagesAllocated:psPageArrayDataPtr->uiTotalNumPages;
		PVRSRV_ERROR eError;

		/* Free associated pages */
		eError = _FreeOSPages(psPageArrayDataPtr,
							  NULL,
							  0);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Shrinker is unable to free unpinned pages. Error: %s (%d)",
					 __FUNCTION__,
					 PVRSRVGetErrorStringKM(eError),
					 eError));
			goto e_exit;
		}

		/* Remove item from pool */
		list_del(&psUnpinEntry->sUnpinPoolItem);

		g_ui32UnpinPageCount -= uiNumPages;

		/* Check if there is more to free or if we already surpassed the limit */
		if (uiNumPages < uNumToScan)
		{
			uNumToScan -= uiNumPages;

		}
		else if (uiNumPages > uNumToScan)
		{
			uSurplus += uiNumPages - uNumToScan;
			uNumToScan = 0;
			goto e_exit;
		}
		else
		{
			uNumToScan -= uiNumPages;
			goto e_exit;
		}
	}

e_exit:
	if (list_empty(&g_sPagePoolList_WC) &&
		list_empty(&g_sPagePoolList_UC) &&
		list_empty(&g_sPagePoolList_WB))
	{
		PVR_ASSERT(g_ui32PagePoolEntryCount == 0);
	}
	if (list_empty(&g_sUnpinList))
	{
		PVR_ASSERT(g_ui32UnpinPageCount == 0);
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,12,0))
	{
		int remain;
		remain = _GetNumberOfPagesInPoolUnlocked();
		_PagePoolUnlock();
		return remain;
	}
#else
	/* Returning the  number of pages freed during the scan */
	_PagePoolUnlock();
	return psShrinkControl->nr_to_scan - uNumToScan + uSurplus;
#endif
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,12,0))
static int
_ShrinkPagePool(struct shrinker *psShrinker, struct shrink_control *psShrinkControl)
{
	if (psShrinkControl->nr_to_scan != 0)
	{
		return _ScanObjectsInPagePool(psShrinker, psShrinkControl);
	}
	else
	{
		/* No pages are being reclaimed so just return the page count */
		return _CountObjectsInPagePool(psShrinker, psShrinkControl);
	}
}

static struct shrinker g_sShrinker =
{
	.shrink = _ShrinkPagePool,
	.seeks = DEFAULT_SEEKS
};
#else
static struct shrinker g_sShrinker =
{
	.count_objects = _CountObjectsInPagePool,
	.scan_objects = _ScanObjectsInPagePool,
	.seeks = DEFAULT_SEEKS
};
#endif

/* Register the shrinker so Linux can reclaim cached pages */
void LinuxInitPhysmem(void)
{
	g_psLinuxPageArray = kmem_cache_create("pvr-pa", sizeof(PMR_OSPAGEARRAY_DATA), 0, 0, NULL);

	_PagePoolLock();
	g_psLinuxPagePoolCache = kmem_cache_create("pvr-pp", sizeof(LinuxPagePoolEntry), 0, 0, NULL);
	if (g_psLinuxPagePoolCache)
	{
		/* Only create the shrinker if we created the cache OK */
		register_shrinker(&g_sShrinker);
	}
	_PagePoolUnlock();
}

/* Unregister the shrinker and remove all pages from the pool that are still left */
void LinuxDeinitPhysmem(void)
{
	IMG_UINT32 uiPagesFreed;

	_PagePoolLock();
	if (_FreePagesFromPoolUnlocked(g_ui32PagePoolEntryCount, &uiPagesFreed) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Unable to free all pages from page pool when deinitialising."));
		PVR_ASSERT(0);
	}

	PVR_ASSERT(g_ui32PagePoolEntryCount == 0);

	/* Free the page cache */
	kmem_cache_destroy(g_psLinuxPagePoolCache);

	unregister_shrinker(&g_sShrinker);
	_PagePoolUnlock();

	kmem_cache_destroy(g_psLinuxPageArray);
}

static void EnableOOMKiller(void)
{
	current->flags &= ~PF_DUMPCORE;
}

static void DisableOOMKiller(void)
{
	/* PF_DUMPCORE is treated by the VM as if the OOM killer was disabled.
	 *
	 * As oom_killer_disable() is an inline, non-exported function, we
	 * can't use it from a modular driver. Furthermore, the OOM killer
	 * API doesn't look thread safe, which `current' is.
	 */
	WARN_ON(current->flags & PF_DUMPCORE);
	current->flags |= PF_DUMPCORE;
}

/* Prints out the addresses in a page array for debugging purposes
 * Define PHYSMEM_OSMEM_DEBUG_DUMP_PAGE_ARRAY locally to activate: */
/* #define PHYSMEM_OSMEM_DEBUG_DUMP_PAGE_ARRAY 1 */
static inline void
_DumpPageArray(struct page **pagearray, IMG_UINT32 uiPagesToPrint)
{
#if defined(PHYSMEM_OSMEM_DEBUG_DUMP_PAGE_ARRAY)
	IMG_UINT32 i;
	if (pagearray)
	{
		printk("Array %p:\n", pagearray);
		for (i = 0; i < uiPagesToPrint; i++)
		{
			printk("%p | ", (pagearray)[i]);
		}
		printk("\n");
	}
	else
	{
		printk("Array is NULL:\n");
	}
#else
	PVR_UNREFERENCED_PARAMETER(pagearray);
	PVR_UNREFERENCED_PARAMETER(uiPagesToPrint);
#endif
}

/* Debugging function that dumps out the number of pages for every
 * page array that is currently in the page pool.
 * Not defined by default. Define locally to activate feature: */
/* #define PHYSMEM_OSMEM_DEBUG_DUMP_PAGE_POOL 1 */
static void
_DumpPoolStructure(void)
{
#if defined(PHYSMEM_OSMEM_DEBUG_DUMP_PAGE_POOL)
	LinuxPagePoolEntry *psPagePoolEntry, *psTempPoolEntry;
	struct list_head *psPoolHead = NULL;
	IMG_UINT32  j;

	printk("\n");
	/* Empty all pools */
	for (j = 0; j < PHYSMEM_OSMEM_NUM_OF_POOLS; j++)
	{

		printk("pool = %u \n", j);

		/* Get the correct list for this caching mode */
		if (!_GetPoolListHead(g_aui32CPUCacheFlags[j], &psPoolHead))
		{
			break;
		}

		list_for_each_entry_safe(psPagePoolEntry,
								 psTempPoolEntry,
								 psPoolHead,
								 sPagePoolItem)
		{
			printk("%u | ", psPagePoolEntry->uiItemsRemaining);
		}
		printk("\n");
	}
#endif
}

/* Will take excess pages from the pool with acquired pool lock and then free
 * them without pool lock being held.
 * Designed to run in the deferred free thread. */
static PVRSRV_ERROR
_FreeExcessPagesFromPool(void)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	LIST_HEAD(sPagePoolFreeList);
	LinuxPagePoolEntry *psPagePoolEntry, *psTempPoolEntry;
	struct list_head *psPoolHead = NULL;
	IMG_UINT32 i, j, uiPoolIdx;
	static IMG_UINT8 uiPoolAccessRandomiser;
	IMG_BOOL bDone = IMG_FALSE;

	/* Make sure all pools are drained over time */
	uiPoolAccessRandomiser++;

	/* Empty all pools */
	for (j = 0; j < PHYSMEM_OSMEM_NUM_OF_POOLS; j++)
	{
		uiPoolIdx = (j + uiPoolAccessRandomiser) % PHYSMEM_OSMEM_NUM_OF_POOLS;

		/* Just lock down to collect pool entries and unlock again before freeing them */
		_PagePoolLock();

		/* Get the correct list for this caching mode */
		if (!_GetPoolListHead(g_aui32CPUCacheFlags[uiPoolIdx], &psPoolHead))
		{
			_PagePoolUnlock();
			break;
		}

		/* Traverse pool in reverse order to remove items that exceeded
		 * the pool size first */
		list_for_each_entry_safe_reverse(psPagePoolEntry,
										 psTempPoolEntry,
										 psPoolHead,
										 sPagePoolItem)
		{
			/* Go to free the pages if we collected enough */
			if (g_ui32PagePoolEntryCount <= g_ui32PagePoolMaxEntries)
			{
				bDone = IMG_TRUE;
				break;
			}

			/* Move item to free list so we can free it later without the pool lock */
			list_del(&psPagePoolEntry->sPagePoolItem);
			list_add(&psPagePoolEntry->sPagePoolItem, &sPagePoolFreeList);

			/* Update counters */
			g_ui32PagePoolEntryCount -= psPagePoolEntry->uiItemsRemaining;

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	/* MemStats usually relies on having the bridge lock held, however
	 * the page pool code may call PVRSRVStatsIncrMemAllocPoolStat and
	 * PVRSRVStatsDecrMemAllocPoolStat without the bridge lock held, so
	 * the page pool lock is used to ensure these calls are mutually
	 * exclusive
	 */
	PVRSRVStatsDecrMemAllocPoolStat(PAGE_SIZE * psPagePoolEntry->uiItemsRemaining);
#endif
		}

		_PagePoolUnlock();


		/* Free the pages that we removed from the pool */
		list_for_each_entry_safe(psPagePoolEntry,
								 psTempPoolEntry,
								 &sPagePoolFreeList,
								 sPagePoolItem)
		{
#if defined(CONFIG_X86)
			/* Set the correct page caching attributes on x86 */
			if (!PVRSRV_CHECK_CPU_CACHED(g_aui32CPUCacheFlags[uiPoolIdx]))
			{
				int ret;
				ret = set_pages_array_wb(psPagePoolEntry->ppsPageArray,
										 psPagePoolEntry->uiItemsRemaining);
				if (ret)
				{
					PVR_DPF((PVR_DBG_ERROR, "%s: Failed to reset page attributes", __FUNCTION__));
					eError = PVRSRV_ERROR_FAILED_TO_FREE_PAGES;
					goto e_exit;
				}
			}
#endif
			/* Free the actual pages */
			for (i = 0; i < psPagePoolEntry->uiItemsRemaining; i++)
			{
				__free_pages(psPagePoolEntry->ppsPageArray[i], 0);
				psPagePoolEntry->ppsPageArray[i] = NULL;
			}

			/* Free the pool entry and page array*/
			list_del(&psPagePoolEntry->sPagePoolItem);
			OSFreeMemNoStats(psPagePoolEntry->ppsPageArray);
			kmem_cache_free(g_psLinuxPagePoolCache, psPagePoolEntry);
		}

		/* Stop if all excess pages were removed */
		if (bDone)
		{
			eError = PVRSRV_OK;
			goto e_exit;
		}

	}

e_exit:
	_DumpPoolStructure();
	return eError;
}

/* Free a certain number of pages from the page pool.
 * Mainly used in error paths or at deinitialisation to
 * empty the whole pool. */
static PVRSRV_ERROR
_FreePagesFromPoolUnlocked(IMG_UINT32 uiMaxPagesToFree,
						   IMG_UINT32 *puiPagesFreed)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	LinuxPagePoolEntry *psPagePoolEntry, *psTempPoolEntry;
	struct list_head *psPoolHead = NULL;
	IMG_UINT32 i, j;

	*puiPagesFreed = uiMaxPagesToFree;

	/* Empty all pools */
	for (j = 0; j < PHYSMEM_OSMEM_NUM_OF_POOLS; j++)
	{

		/* Get the correct list for this caching mode */
		if (!_GetPoolListHead(g_aui32CPUCacheFlags[j], &psPoolHead))
		{
			break;
		}

		/* Free the pages and remove page arrays from the pool if they are exhausted */
		list_for_each_entry_safe(psPagePoolEntry,
								 psTempPoolEntry,
								 psPoolHead,
								 sPagePoolItem)
		{
			IMG_UINT32 uiItemsToFree;
			struct page **ppsPageArray;

			/* Check if we are going to free the whole page array or just parts */
			if (psPagePoolEntry->uiItemsRemaining <= uiMaxPagesToFree)
			{
				uiItemsToFree = psPagePoolEntry->uiItemsRemaining;
				ppsPageArray = psPagePoolEntry->ppsPageArray;
			}
			else
			{
				uiItemsToFree = uiMaxPagesToFree;
				ppsPageArray = &(psPagePoolEntry->ppsPageArray[psPagePoolEntry->uiItemsRemaining - uiItemsToFree]);
			}

#if defined(CONFIG_X86)
			/* Set the correct page caching attributes on x86 */
			if (!PVRSRV_CHECK_CPU_CACHED(g_aui32CPUCacheFlags[j]))
			{
				int ret;
				ret = set_pages_array_wb(ppsPageArray, uiItemsToFree);
				if (ret)
				{
					PVR_DPF((PVR_DBG_ERROR, "%s: Failed to reset page attributes", __FUNCTION__));
					eError = PVRSRV_ERROR_FAILED_TO_FREE_PAGES;
					goto e_exit;
				}
			}
#endif

			/* Free the actual pages */
			for (i = 0; i < uiItemsToFree; i++)
			{
				__free_pages(ppsPageArray[i], 0);
				ppsPageArray[i] = NULL;
			}

			/* Reduce counters */
			uiMaxPagesToFree -= uiItemsToFree;
			g_ui32PagePoolEntryCount -= uiItemsToFree;
			psPagePoolEntry->uiItemsRemaining -= uiItemsToFree;

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	/* MemStats usually relies on having the bridge lock held, however
	 * the page pool code may call PVRSRVStatsIncrMemAllocPoolStat and
	 * PVRSRVStatsDecrMemAllocPoolStat without the bridge lock held, so
	 * the page pool lock is used to ensure these calls are mutually
	 * exclusive
	 */
	PVRSRVStatsDecrMemAllocPoolStat(PAGE_SIZE * uiItemsToFree);
#endif

			/* Is this pool entry exhausted, delete it */
			if (psPagePoolEntry->uiItemsRemaining == 0)
			{
				OSFreeMemNoStats(psPagePoolEntry->ppsPageArray);
				list_del(&psPagePoolEntry->sPagePoolItem);
				kmem_cache_free(g_psLinuxPagePoolCache, psPagePoolEntry);
			}

			/* Return if we have all our pages */
			if (uiMaxPagesToFree == 0)
			{
				goto e_exit;
			}
		}
	}

e_exit:
	*puiPagesFreed -= uiMaxPagesToFree;
	_DumpPoolStructure();
	return eError;
}

/* Get a certain number of pages from the page pool and
 * copy them directly into a given page array. */
static void
_GetPagesFromPoolUnlocked(IMG_UINT32 ui32CPUCacheFlags,
						  IMG_UINT32 uiMaxNumPages,
						  struct page **ppsPageArray,
						  IMG_UINT32 *puiNumReceivedPages)
{
	LinuxPagePoolEntry *psPagePoolEntry, *psTempPoolEntry;
	struct list_head *psPoolHead = NULL;
	IMG_UINT32 i;

	*puiNumReceivedPages = 0;

	/* Get the correct list for this caching mode */
	if (!_GetPoolListHead(ui32CPUCacheFlags, &psPoolHead))
	{
		return;
	}

	/* Check if there are actually items in the list */
	if (list_empty(psPoolHead))
	{
		return;
	}

	PVR_ASSERT(g_ui32PagePoolEntryCount > 0);

	/* Receive pages from the pool */
	list_for_each_entry_safe(psPagePoolEntry,
							 psTempPoolEntry,
							 psPoolHead,
							 sPagePoolItem)
	{
		/* Get the pages from this pool entry */
		for (i = psPagePoolEntry->uiItemsRemaining; i != 0 && *puiNumReceivedPages < uiMaxNumPages; i--)
		{
			ppsPageArray[*puiNumReceivedPages] = psPagePoolEntry->ppsPageArray[i-1];
			(*puiNumReceivedPages)++;
			psPagePoolEntry->uiItemsRemaining--;
		}

		/* Is this pool entry exhausted, delete it */
		if (psPagePoolEntry->uiItemsRemaining == 0)
		{
			OSFreeMemNoStats(psPagePoolEntry->ppsPageArray);
			list_del(&psPagePoolEntry->sPagePoolItem);
			kmem_cache_free(g_psLinuxPagePoolCache, psPagePoolEntry);
		}

		/* Return if we have all our pages */
		if (*puiNumReceivedPages == uiMaxNumPages)
		{
			goto exit_ok;
		}
	}

exit_ok:

	/* Update counters */
	g_ui32PagePoolEntryCount -= *puiNumReceivedPages;

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	/* MemStats usually relies on having the bridge lock held, however
	 * the page pool code may call PVRSRVStatsIncrMemAllocPoolStat and
	 * PVRSRVStatsDecrMemAllocPoolStat without the bridge lock held, so
	 * the page pool lock is used to ensure these calls are mutually
	 * exclusive
	 */
	PVRSRVStatsDecrMemAllocPoolStat(PAGE_SIZE * (*puiNumReceivedPages));
#endif

	_DumpPoolStructure();
	return;
}

/* When is it worth waiting for the page pool? */
#define PVR_LINUX_PHYSMEM_MIN_PAGES_TO_WAIT_FOR_POOL 64

/* Same as _GetPagesFromPoolUnlocked but handles locking and
 * checks first whether pages from the pool are a valid option. */
static inline void
_GetPagesFromPoolLocked(PVRSRV_DEVICE_NODE *psDevNode,
						IMG_UINT32 ui32CPUCacheFlags,
						IMG_UINT32 uiPagesToAlloc,
						IMG_UINT32 uiOrder,
						IMG_BOOL bZero,
						struct page **ppsPageArray,
						IMG_UINT32 *puiPagesFromPool)
{
	/* The page pool stores only order 0 pages. If we need zeroed memory we
	 * directly allocate from the OS because it is faster than doing it ourselves. */
	if (uiOrder == 0 && !bZero)
	{
		if (uiPagesToAlloc < PVR_LINUX_PHYSMEM_MIN_PAGES_TO_WAIT_FOR_POOL)
		{
			/* In case the request is a few pages, just try to acquire the pool lock */
			if (_PagePoolTrylock() == 0)
			{
				return;
			}
		}
		else
		{
			/* It is worth waiting if many pages were requested.
			 * Freeing an item to the pool is very fast and
			 * the defer free thread will release the lock regularly. */
			_PagePoolLock();
		}

		_GetPagesFromPoolUnlocked(ui32CPUCacheFlags,
								  uiPagesToAlloc,
								  ppsPageArray,
								  puiPagesFromPool);
		_PagePoolUnlock();

		/* Do cache maintenance so allocations from the pool can be
		 * considered clean */
		if (PVRSRV_CHECK_CPU_CACHED(ui32CPUCacheFlags) &&
		    PVRSRV_CHECK_CPU_CACHE_CLEAN(ui32CPUCacheFlags))
		{
			_ApplyCacheMaintenance(psDevNode,
								   ppsPageArray,
								   *puiPagesFromPool,
								   IMG_FALSE);
		}
	}

	return;
}

/* Defer free function to remove excess pages from the page pool.
 * We do not need the bridge lock for this function */
static PVRSRV_ERROR
_CleanupThread_FreePoolPages(void *pvData)
{
	PVRSRV_ERROR eError;

	/* Free all that is necessary */
	eError = _FreeExcessPagesFromPool();
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: _FreeExcessPagesFromPool failed", __func__));
		goto e_exit;
	}

	OSFreeMem(pvData);

e_exit:
	return eError;
}

/* Signal the defer free thread that there are pages in the pool to be cleaned up.
 * MUST NOT HOLD THE PAGE POOL LOCK! */
static void
_SignalDeferFree(void)
{
	PVRSRV_CLEANUP_THREAD_WORK *psCleanupThreadFn;
	psCleanupThreadFn = OSAllocMem(sizeof(*psCleanupThreadFn));

	if(!psCleanupThreadFn)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Failed to get memory for deferred page pool cleanup. "
				 "Trying to free pages immediately",
				 __FUNCTION__));
		goto e_oom_exit;
	}

	psCleanupThreadFn->pfnFree = _CleanupThread_FreePoolPages;
	psCleanupThreadFn->pvData = psCleanupThreadFn;
	psCleanupThreadFn->ui32RetryCount = CLEANUP_THREAD_RETRY_COUNT_DEFAULT;
	psCleanupThreadFn->bDependsOnHW = IMG_FALSE;
	/* We must not hold the pool lock when calling AddWork because it might call us back to
	 * free pooled pages directly when unloading the driver	 */
	PVRSRVCleanupThreadAddWork(psCleanupThreadFn);

	return;

e_oom_exit:
	{
		/* In case we are not able to signal the defer free thread
		 * we have to cleanup the pool now. */
		IMG_UINT32 uiPagesFreed;

		_PagePoolLock();
		if (_FreePagesFromPoolUnlocked(g_ui32PagePoolEntryCount - g_ui32PagePoolMaxEntries,
									   &uiPagesFreed) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: Unable to free pooled pages!",
					 __FUNCTION__));
		}
		_PagePoolUnlock();

		return;
	}
}

/* Moves a page array to the page pool.
 *
 * If this function is successful the ppsPageArray is unusable and needs to be
 * reallocated in case the _PMR_OSPAGEARRAY_DATA_ will be reused.
 * This function expects cached pages to be not in the cache anymore,
 * invalidate them before, ideally without using the pool lock. */
static IMG_BOOL
_PutPagesToPoolUnlocked(IMG_UINT32 ui32CPUCacheFlags,
						struct page **ppsPageArray,
						IMG_UINT32 uiEntriesInArray)
{
	LinuxPagePoolEntry *psPagePoolEntry;
	struct list_head *psPoolHead = NULL;

	/* Check if there is still space in the pool */
	if ( (g_ui32PagePoolEntryCount + uiEntriesInArray) >=
		 (g_ui32PagePoolMaxEntries + g_ui32PagePoolMaxExcessEntries) )
	{
		return IMG_FALSE;
	}

	/* Get the correct list for this caching mode */
	if (!_GetPoolListHead(ui32CPUCacheFlags, &psPoolHead))
	{
		return IMG_FALSE;
	}

	/* Fill the new pool entry structure and add it to the pool list */
	psPagePoolEntry = kmem_cache_alloc(g_psLinuxPagePoolCache, GFP_KERNEL);
	psPagePoolEntry->ppsPageArray = ppsPageArray;
	psPagePoolEntry->uiItemsRemaining = uiEntriesInArray;

	list_add_tail(&psPagePoolEntry->sPagePoolItem, psPoolHead);

	/* Update counters */
	g_ui32PagePoolEntryCount += uiEntriesInArray;

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	/* MemStats usually relies on having the bridge lock held, however
	 * the page pool code may call PVRSRVStatsIncrMemAllocPoolStat and
	 * PVRSRVStatsDecrMemAllocPoolStat without the bridge lock held, so
	 * the page pool lock is used to ensure these calls are mutually
	 * exclusive
	 */
	PVRSRVStatsIncrMemAllocPoolStat(PAGE_SIZE * uiEntriesInArray);
#endif

	_DumpPoolStructure();
	return IMG_TRUE;
}

/* Minimal amount of pages that will go to the pool, everything below is freed directly */
#define PVR_LINUX_PHYSMEM_MIN_PAGES_TO_ADD_TO_POOL 16

/* Same as _PutPagesToPoolUnlocked but handles locking and checks whether the pages are
 * suitable to be stored in the page pool. */
static inline IMG_BOOL
_PutPagesToPoolLocked(IMG_UINT32 ui32CPUCacheFlags,
					  struct page **ppsPageArray,
					  IMG_BOOL bUnpinned,
					  IMG_UINT32 uiOrder,
					  IMG_UINT32 uiNumPages)
{
	if (uiOrder == 0 &&
		!bUnpinned &&
		uiNumPages >= PVR_LINUX_PHYSMEM_MIN_PAGES_TO_ADD_TO_POOL)
	{
		_PagePoolLock();

		/* Try to quickly move page array to the pool */
		if (_PutPagesToPoolUnlocked(ui32CPUCacheFlags,
									ppsPageArray,
									uiNumPages) )
		{
			if (g_ui32PagePoolEntryCount > (g_ui32PagePoolMaxEntries + g_ui32PagePoolMaxEntries_5Percent))
			{
				/* Signal defer free to clean up excess pages from pool.
				 * Allow a little excess before signalling to avoid oscillating behaviour */
				_PagePoolUnlock();
				_SignalDeferFree();
			}
			else
			{
				_PagePoolUnlock();
			}

			/* All done */
			return IMG_TRUE;
		}

		/* Could not move pages to pool, continue and free them now  */
		_PagePoolUnlock();
	}

	return IMG_FALSE;
}

/* Get the GFP flags that we pass to the page allocator */
static inline unsigned int
_GetGFPFlags(PMR_OSPAGEARRAY_DATA *psPageArrayData)
{
	struct device *psDev = psPageArrayData->psDevNode->psDevConfig->pvOSDevice;
	unsigned int gfp_flags = 0;
	gfp_flags = GFP_USER | __GFP_NOWARN | __GFP_NOMEMALLOC;

	if (*psDev->dma_mask == DMA_BIT_MASK(32))
	{
		/* Limit to 32 bit.
		 * Achieved by NOT setting __GFP_HIGHMEM for 32 bit systems and
         * setting __GFP_DMA32 for 64 bit systems */
		gfp_flags |= __GFP_DMA32;
	}
	else
	{
		/* If our system is able to handle large addresses use highmem */
		gfp_flags |= __GFP_HIGHMEM;
	}

	if (psPageArrayData->bZero)
	{
		gfp_flags |= __GFP_ZERO;
	}

	return gfp_flags;
}

/* Poison a page of order uiOrder with string taken from pacPoisonData*/
static void
_PoisonPages(struct page *page,
			 IMG_UINT32 uiOrder,
			 const IMG_CHAR *pacPoisonData,
			 size_t uiPoisonSize)
{
	void *kvaddr;
	IMG_UINT32 uiSrcByteIndex;
	IMG_UINT32 uiDestByteIndex;
	IMG_UINT32 uiSubPageIndex;
	IMG_CHAR *pcDest;

	uiSrcByteIndex = 0;
	for (uiSubPageIndex = 0; uiSubPageIndex < (1U << uiOrder); uiSubPageIndex++)
	{
		kvaddr = kmap(page + uiSubPageIndex);
		pcDest = kvaddr;

		for(uiDestByteIndex=0; uiDestByteIndex<PAGE_SIZE; uiDestByteIndex++)
		{
			pcDest[uiDestByteIndex] = pacPoisonData[uiSrcByteIndex];
			uiSrcByteIndex++;
			if (uiSrcByteIndex == uiPoisonSize)
			{
				uiSrcByteIndex = 0;
			}
		}

		flush_dcache_page(page);
		kunmap(page + uiSubPageIndex);
	}
}

static const IMG_CHAR _AllocPoison[] = "^PoIsOn";
static const IMG_UINT32 _AllocPoisonSize = 7;
static const IMG_CHAR _FreePoison[] = "<DEAD-BEEF>";
static const IMG_UINT32 _FreePoisonSize = 11;

/* Allocate and initialise the structure to hold the metadata of the allocation */
static PVRSRV_ERROR
_AllocOSPageArray(PVRSRV_DEVICE_NODE *psDevNode,
				  PMR_SIZE_T uiChunkSize,
				  IMG_UINT32 ui32NumPhysChunks,
				  IMG_UINT32 ui32NumVirtChunks,
				  IMG_UINT32 uiLog2DevPageSize,
				  IMG_BOOL bZero,
				  IMG_BOOL bIsCMA,
				  IMG_BOOL bPoisonOnAlloc,
				  IMG_BOOL bPoisonOnFree,
				  IMG_BOOL bOnDemand,
				  IMG_UINT32 ui32CPUCacheFlags,
				  PMR_OSPAGEARRAY_DATA **ppsPageArrayDataPtr)
{
	PVRSRV_ERROR eError;
	PMR_SIZE_T uiSize = uiChunkSize * ui32NumVirtChunks;
	IMG_UINT32 uiNumOSPageSizeVirtPages;
	IMG_UINT32 uiNumDevPageSizeVirtPages;
	PMR_OSPAGEARRAY_DATA *psPageArrayData;
	PVR_UNREFERENCED_PARAMETER(ui32NumPhysChunks);

	/* Sanity check of the alloc size */
	if (uiSize >= 0x1000000000ULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Do you really want 64GB of physical memory in one go? "
				 "This is likely a bug", __func__));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto e_freed_none;
	}

	/* Check that we allocate the correct contiguity */
	PVR_ASSERT(PAGE_SHIFT <= uiLog2DevPageSize);
	if ((uiSize & ((1ULL << uiLog2DevPageSize) - 1)) != 0)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"Allocation size " PMR_SIZE_FMTSPEC " is not multiple of page size 2^%u !",
				 uiSize,
				 uiLog2DevPageSize));

		eError = PVRSRV_ERROR_PMR_NOT_PAGE_MULTIPLE;
		goto e_freed_none;
	}

	/* Use of cast below is justified by the assertion that follows to
	   prove that no significant bits have been truncated */
	uiNumOSPageSizeVirtPages = (IMG_UINT32) (((uiSize - 1) >> PAGE_SHIFT) + 1);
	PVR_ASSERT(((PMR_SIZE_T) uiNumOSPageSizeVirtPages << PAGE_SHIFT) == uiSize);
	uiNumDevPageSizeVirtPages = uiNumOSPageSizeVirtPages >> (uiLog2DevPageSize - PAGE_SHIFT);

	/* Allocate the struct to hold the metadata */
	psPageArrayData = kmem_cache_alloc(g_psLinuxPageArray, GFP_KERNEL);
	if (psPageArrayData == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: OS refused the memory allocation for the private data.",
				 __func__));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e_freed_none;
	}

	/* 
	 * Allocate the page array
	 *
	 * We avoid tracking this memory because this structure might go into the page pool.
	 * The OS can drain the pool asynchronously and when doing that we have to avoid
	 * any potential deadlocks.
	 *
	 * In one scenario the process stats vmalloc hash table lock is held and then
	 * the oom-killer softirq is trying to call _ScanObjectsInPagePool(), it must not
	 * try to acquire the vmalloc hash table lock again.
	 */
	psPageArrayData->pagearray = OSAllocZMemNoStats(sizeof(struct page *) * uiNumDevPageSizeVirtPages);
	if (psPageArrayData->pagearray == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e_free_kmem_cache;
	}
	else
	{
		if (bIsCMA)
		{
			/* Allocate additional DMA/CMA cpu kernel virtual address & device bus address array state */
			psPageArrayData->dmavirtarray = OSAllocZMemNoStats(sizeof(void*) * uiNumDevPageSizeVirtPages);
			if (psPageArrayData->dmavirtarray == NULL)
			{
				eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto e_free_pagearray;
			}

			psPageArrayData->dmaphysarray = OSAllocZMemNoStats(sizeof(dma_addr_t) * uiNumDevPageSizeVirtPages);
			if (psPageArrayData->dmaphysarray == NULL)
			{
				eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto e_free_cpuvirtaddrarray;
			}
		}
	}

	/* Init metadata */
	psPageArrayData->psDevNode = psDevNode;
	psPageArrayData->iNumPagesAllocated = 0;
	psPageArrayData->uiTotalNumPages = uiNumOSPageSizeVirtPages;
	psPageArrayData->uiLog2DevPageSize = uiLog2DevPageSize;
	psPageArrayData->bZero = bZero;
	psPageArrayData->bIsCMA = bIsCMA;
	psPageArrayData->bOnDemand = bOnDemand;
	psPageArrayData->bUnpinned = IMG_FALSE;
	psPageArrayData->bPoisonOnFree = bPoisonOnFree;
	psPageArrayData->bPoisonOnAlloc = bPoisonOnAlloc;
	psPageArrayData->ui32CPUCacheFlags = ui32CPUCacheFlags;

	/* Indicate whether this is an allocation with default caching attribute (i.e cached) or not */
	if (PVRSRV_CHECK_CPU_UNCACHED(ui32CPUCacheFlags) ||
		PVRSRV_CHECK_CPU_WRITE_COMBINE(ui32CPUCacheFlags))
	{
		psPageArrayData->bUnsetMemoryType = IMG_TRUE;
	}
	else
	{
		psPageArrayData->bUnsetMemoryType = IMG_FALSE;
	}

	*ppsPageArrayDataPtr = psPageArrayData;
	return PVRSRV_OK;

/* Error path */
e_free_cpuvirtaddrarray:
	OSFreeMemNoStats(psPageArrayData->dmavirtarray);

e_free_pagearray:
	OSFreeMemNoStats(psPageArrayData->pagearray);

e_free_kmem_cache:
	kmem_cache_free(g_psLinuxPageArray, psPageArrayData);
	PVR_DPF((PVR_DBG_ERROR,
			 "%s: OS refused the memory allocation for the page pointer table. "
			 "Did you ask for too much?", 
			 __func__));

e_freed_none:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

static inline void
_ApplyCacheMaintenance(PVRSRV_DEVICE_NODE *psDevNode,
					   struct page **ppsPage,
					   IMG_UINT32 uiNumPages,
					   IMG_BOOL bFlush)
{
	PVRSRV_ERROR eError = PVRSRV_ERROR_RETRY;
	IMG_UINT32 ui32Idx;

	if ((uiNumPages << PAGE_SHIFT) >= PVR_DIRTY_BYTES_FLUSH_THRESHOLD)
	{
		/* May fail so fallback to range-based flush */
		eError = OSCPUOperation(PVRSRV_CACHE_OP_FLUSH);
	}

	if (eError != PVRSRV_OK)
	{
		for (ui32Idx = 0; ui32Idx < uiNumPages;  ++ui32Idx)
		{
			IMG_CPU_PHYADDR sCPUPhysAddrStart, sCPUPhysAddrEnd;
			void *pvPageVAddr;

			pvPageVAddr = kmap(ppsPage[ui32Idx]);
			sCPUPhysAddrStart.uiAddr = page_to_phys(ppsPage[ui32Idx]);
			sCPUPhysAddrEnd.uiAddr = sCPUPhysAddrStart.uiAddr + PAGE_SIZE;

			/* If we're zeroing, we need to make sure the cleared memory is pushed out
			   of the cache before the cache lines are invalidated */
			if (bFlush)
			{
				OSFlushCPUCacheRangeKM(psDevNode,
									   pvPageVAddr,
									   pvPageVAddr + PAGE_SIZE,
									   sCPUPhysAddrStart,
									   sCPUPhysAddrEnd);
			}
			else
			{
				OSInvalidateCPUCacheRangeKM(psDevNode,
											pvPageVAddr,
											pvPageVAddr + PAGE_SIZE,
											sCPUPhysAddrStart,
											sCPUPhysAddrEnd);
			}

			kunmap(ppsPage[ui32Idx]);
		}
	}
}

/* Change the caching attribute of pages on x86 systems and takes care of
 * cache maintenance. This function is supposed to be called once for pages that
 * came from alloc_pages().
 *
 * Flush/Invalidate pages in case the allocation is not cached. Necessary to
 * remove pages from the cache that might be flushed later and corrupt memory. */
static inline PVRSRV_ERROR
_ApplyOSPagesAttribute(PVRSRV_DEVICE_NODE *psDevNode,
					   struct page **ppsPage,
					   IMG_UINT32 uiNumPages,
					   IMG_BOOL bFlush,
					   IMG_UINT32 ui32CPUCacheFlags)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_BOOL bCPUCached = PVRSRV_CHECK_CPU_CACHED(ui32CPUCacheFlags);
	IMG_BOOL bCPUUncached = PVRSRV_CHECK_CPU_UNCACHED(ui32CPUCacheFlags);
	IMG_BOOL bCPUWriteCombine = PVRSRV_CHECK_CPU_WRITE_COMBINE(ui32CPUCacheFlags);

	if (ppsPage != NULL)
	{
#if defined (CONFIG_X86)
		/* On x86 we have to set page cache attributes for non-cached pages.
		 * The call is implicitly taking care of all flushing/invalidating
		 * and therefore we can skip the usual cache maintenance after this. */
		if (bCPUUncached || bCPUWriteCombine)
		{
			/*  On X86 if we already have a mapping (e.g. low memory) we need to change the mode of
				current mapping before we map it ourselves	*/
			int ret = IMG_FALSE;
			PVR_UNREFERENCED_PARAMETER(bFlush);

			switch (PVRSRV_CPU_CACHE_MODE(ui32CPUCacheFlags))
			{
				case PVRSRV_MEMALLOCFLAG_CPU_UNCACHED:
					ret = set_pages_array_uc(ppsPage, uiNumPages);
					if (ret)
					{
						eError = PVRSRV_ERROR_UNABLE_TO_SET_CACHE_MODE;
						PVR_DPF((PVR_DBG_ERROR, "Setting Linux page caching mode to UC failed, returned %d", ret));
					}
					break;

				case PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE:
					ret = set_pages_array_wc(ppsPage, uiNumPages);
					if (ret)
					{
						eError = PVRSRV_ERROR_UNABLE_TO_SET_CACHE_MODE;
						PVR_DPF((PVR_DBG_ERROR, "Setting Linux page caching mode to WC failed, returned %d", ret));
					}
					break;

				case PVRSRV_MEMALLOCFLAG_CPU_CACHED:
					break;

				default:
					break;
			}
		}
		else
#endif
		/* Cache maintenance if:
		 *     cached && (cleanFlag || bFlush)
		 * OR
		 *     uncached || write-combine
		 */
		if ( (bCPUCached && (PVRSRV_CHECK_CPU_CACHE_CLEAN(ui32CPUCacheFlags) || bFlush)) ||
		      bCPUUncached || bCPUWriteCombine )
		{
			/*  We can be given pages which still remain in the cache.
				In order to make sure that the data we write through our mappings
				doesn't get overwritten by later cache evictions we invalidate the
				pages that are given to us.

				Note:
				This still seems to be true if we request cold pages, it's just less
				likely to be in the cache. */
			_ApplyCacheMaintenance(psDevNode,
								   ppsPage,
								   uiNumPages,
								   bFlush);
		}
	}

	return eError;
}

/* Same as _AllocOSPage except it uses DMA framework to perform allocation */
static PVRSRV_ERROR
_AllocOSPage_CMA(PMR_OSPAGEARRAY_DATA *psPageArrayData,
				unsigned int gfp_flags,
				IMG_UINT32 ui32AllocOrder,
				IMG_UINT32 ui32MinOrder,
				IMG_UINT32 uiPageIndex)
{
	void *virt_addr;
	struct page *page;
	dma_addr_t bus_addr;
	size_t alloc_size = PAGE_SIZE << ui32MinOrder;
	PVR_UNREFERENCED_PARAMETER(ui32AllocOrder);
	PVR_ASSERT(ui32AllocOrder == ui32MinOrder);

	DisableOOMKiller();
	virt_addr = dma_alloc_coherent(psPageArrayData->psDevNode->psDevConfig->pvOSDevice,
								  alloc_size,
								  &bus_addr,
								  gfp_flags);
	if (virt_addr == NULL)
	{
		/* The idea here is primarily to support some older kernels with
		   broken or non-functioning DMA/CMA implementations (< Linux-3.4)
		   and to also handle DMA/CMA allocation failures by attempting a
		   normal page allocation though we expect dma_alloc_coherent()
		   already attempts this internally also before failing but
		   nonetheless it does no harm to retry allocation ourself */
		page = alloc_pages(gfp_flags, ui32AllocOrder);
		if (page)
		{
			/* Taint bus_addr as alloc_page, needed when freeing;
			   also acquire the low memory page address only, this
			   prevents mapping possible high memory pages into
			   kernel virtual address space which might exhaust
			   the VMALLOC address space */
			bus_addr = DMA_SET_ALLOCPG_ADDR(page_to_phys(page));
			virt_addr = page_address(page);
		}
		else
		{
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}
	}
	else
	{
		page = pfn_to_page(bus_addr >> PAGE_SHIFT);
	}
	EnableOOMKiller();

	/* Convert OSPageSize-based index into DevicePageSize-based index */
	psPageArrayData->dmavirtarray[uiPageIndex >> ui32MinOrder] = virt_addr;
	psPageArrayData->dmaphysarray[uiPageIndex >> ui32MinOrder] = bus_addr;
	psPageArrayData->pagearray[uiPageIndex >> ui32MinOrder] = page;

	return PVRSRV_OK;
}

/* Allocate a page of order uiAllocOrder and stores it in the page array ppsPage at
 * position uiPageIndex.
 *
 * If the order is higher than 0, it splits the page into multiples and
 * stores them at position uiPageIndex to uiPageIndex+(1<<uiAllocOrder). */
static PVRSRV_ERROR
_AllocOSPage(PMR_OSPAGEARRAY_DATA *psPageArrayData,
			unsigned int gfp_flags,
			IMG_UINT32 uiAllocOrder,
			IMG_UINT32 uiMinOrder,
			IMG_UINT32 uiPageIndex)
{
	struct page *psPage;
	IMG_UINT32 ui32Count;

	/* Allocate the page */
	DisableOOMKiller();
	psPage = alloc_pages(gfp_flags, uiAllocOrder);
	EnableOOMKiller();

	if (psPage == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0))
	/* In case we need to, split the higher order page;
	   this should only be used for order-0 allocations
	   as higher order allocations should use DMA/CMA */
	if (uiAllocOrder != 0)
	{
		split_page(psPage, uiAllocOrder);
	}
#endif

	/* Store the page (or multiple split pages) in the page array */
	for (ui32Count = 0; ui32Count < (1 << uiAllocOrder); ui32Count++)
	{
		psPageArrayData->pagearray[uiPageIndex + ui32Count] = &(psPage[ui32Count]);
	}

	return PVRSRV_OK;
}

/* Allocation of OS pages: We may allocate 2^N order pages at a time for two reasons.
 *
 * Firstly to support device pages which are larger than OS. By asking the OS for 2^N
 * order OS pages at a time we guarantee the device page is contiguous.
 *
 * Secondly for performance where we may ask for 2^N order pages to reduce the number
 * of calls to alloc_pages, and thus reduce time for huge allocations.
 *
 * Regardless of page order requested, we need to break them down to track _OS pages.
 * The maximum order requested is increased if all max order allocations were successful.
 * If any request fails we reduce the max order.
 */
static PVRSRV_ERROR
_AllocOSPages_Fast(PMR_OSPAGEARRAY_DATA *psPageArrayData)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 uiArrayIndex = 0;
	IMG_UINT32 ui32Order;
	IMG_UINT32 ui32MinOrder = psPageArrayData->uiLog2DevPageSize - PAGE_SHIFT;
	IMG_BOOL bIncreaseMaxOrder = IMG_TRUE;

	IMG_UINT32 ui32NumPageReq;
	IMG_UINT32 uiPagesToAlloc;
	IMG_UINT32 uiPagesFromPool = 0;

	unsigned int gfp_flags = _GetGFPFlags(psPageArrayData);
	IMG_UINT32 ui32GfpFlags;
	IMG_UINT32 ui32HighOrderGfpFlags = ((gfp_flags & ~__GFP_RECLAIM) | __GFP_NORETRY);

	struct page **ppsPageArray = psPageArrayData->pagearray;
	struct page **ppsPageAttributeArray = NULL;

	uiPagesToAlloc = psPageArrayData->uiTotalNumPages;

	/* Try to get pages from the pool since it is faster;
	   the page pool currently only supports zero-order pages
	   thus currently excludes all DMA/CMA allocated memory */
	_GetPagesFromPoolLocked(psPageArrayData->psDevNode,
							psPageArrayData->ui32CPUCacheFlags,
							uiPagesToAlloc,
							ui32MinOrder,
							psPageArrayData->bZero,
							ppsPageArray,
							&uiPagesFromPool);

	uiArrayIndex = uiPagesFromPool;

	if ((uiPagesToAlloc - uiPagesFromPool) < PVR_LINUX_HIGHORDER_ALLOCATION_THRESHOLD)
	{	/* Small allocations: Ask for one device page at a time */
		ui32Order = ui32MinOrder;
		bIncreaseMaxOrder = IMG_FALSE;
	}
	else
	{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0))
		/* Large zero-order or none zero-order allocations, ask for
		   MAX(max-order,min-order) order pages at a time; alloc
		   failures throttles this down to ZeroOrder allocations */
		ui32Order = MAX(g_uiMaxOrder, ui32MinOrder);
#else
		/* Because split_pages() is not available on older kernels
		   we cannot mix-and-match any-order pages in the PMR;
		   only same-order pages must be present in page array.
		   So we unconditionally force it to use ui32MinOrder on
		   these older kernels */
		ui32Order = ui32MinOrder;
#endif
	}

	/* Only if asking for more contiguity than we actually need, let it fail */
	ui32GfpFlags = (ui32Order > ui32MinOrder) ? ui32HighOrderGfpFlags : gfp_flags;
	ui32NumPageReq = (1 << ui32Order);

	while (uiArrayIndex < uiPagesToAlloc)
	{
		IMG_UINT32 ui32PageRemain = uiPagesToAlloc - uiArrayIndex;

		while (ui32NumPageReq > ui32PageRemain)
		{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0))
			/* Pages to request is larger than that remaining
			   so ask for less so never over allocate */
			ui32Order = MAX(ui32Order >> 1,ui32MinOrder);
#else
			/* Pages to request is larger than that remaining so
			   do nothing thus over allocate as we do not support
			   mix/match of any-order pages in PMR page-array in
			   older kernels (simplifies page free logic) */
			PVR_ASSERT(ui32Order == ui32MinOrder);
#endif
			ui32NumPageReq = (1 << ui32Order);
			ui32GfpFlags = (ui32Order > ui32MinOrder) ? ui32HighOrderGfpFlags : gfp_flags;
		}

		if (psPageArrayData->bIsCMA)
		{
			/* As the DMA/CMA framework rounds-up request to the
			   next power-of-two, we request multiple uiMinOrder
			   pages to satisfy allocation request in order to
			   minimise wasting memory */
			eError =  _AllocOSPage_CMA(psPageArrayData,
									   ui32GfpFlags,
									   ui32Order,
									   ui32MinOrder,
									   uiArrayIndex);
		}
		else
		{
			/* Allocate uiOrder pages at uiArrayIndex */
			eError = _AllocOSPage(psPageArrayData,
								  ui32GfpFlags,
								  ui32Order,
								  ui32MinOrder,
								  uiArrayIndex);
		}

		if (eError == PVRSRV_OK)
		{
			/* Successful request. Move onto next. */
			uiArrayIndex += ui32NumPageReq;
		}
		else
		{
			if (ui32Order > ui32MinOrder)
			{
				/* Last request failed. Let's ask for less next time */
				ui32Order = MAX(ui32Order >> 1,ui32MinOrder);
				bIncreaseMaxOrder = IMG_FALSE;
				ui32NumPageReq = (1 << ui32Order);
				ui32GfpFlags = (ui32Order > ui32MinOrder) ? ui32HighOrderGfpFlags : gfp_flags;
				g_uiMaxOrder = ui32Order;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0))
				/* We should not trigger this code path in older kernels,
				   this is enforced by ensuring ui32Order == ui32MinOrder */
				PVR_ASSERT(ui32Order == ui32MinOrder);
#endif
			}
			else
			{
				/* Failed to alloc pages at required contiguity. Failed allocation */
				PVR_DPF((PVR_DBG_ERROR, "%s: alloc_pages failed to honour request at %u of %u, flags = %x, order = %u (%s)",
								__FUNCTION__,
								uiArrayIndex,
								uiPagesToAlloc,
								ui32GfpFlags,
								ui32Order,
								PVRSRVGetErrorStringKM(eError)));
				eError = PVRSRV_ERROR_PMR_FAILED_TO_ALLOC_PAGES;
				goto e_free_pages;
			}
		}
	}

	if (bIncreaseMaxOrder && (g_uiMaxOrder < PVR_LINUX_PHYSMEM_MAX_ALLOC_ORDER_NUM))
	{	/* All successful allocations on max order. Let's ask for more next time */
		g_uiMaxOrder++;
	}

	/* Construct table of page pointers to apply attributes */
	ppsPageAttributeArray = &ppsPageArray[uiPagesFromPool];
	if (psPageArrayData->bIsCMA)
	{
		IMG_UINT32 uiIdx, uiIdy, uiIdz;

		ppsPageAttributeArray = OSAllocMem(sizeof(struct page *) * uiPagesToAlloc);
		if (ppsPageAttributeArray == NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "Failed OSAllocMem() for page attributes table"));
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto e_free_pages;
		}

		for (uiIdx = 0; uiIdx < uiPagesToAlloc; uiIdx += ui32NumPageReq)
		{
			uiIdy = uiIdx >> ui32Order;
			for (uiIdz = 0; uiIdz < ui32NumPageReq; uiIdz++)
			{
				ppsPageAttributeArray[uiIdx+uiIdz] = psPageArrayData->pagearray[uiIdy];
				ppsPageAttributeArray[uiIdx+uiIdz] += uiIdz;
			}
		}
	}

	/* Do the cache management as required */
	eError = _ApplyOSPagesAttribute(psPageArrayData->psDevNode,
									ppsPageAttributeArray,
									uiPagesToAlloc - uiPagesFromPool,
									psPageArrayData->bZero,
									psPageArrayData->ui32CPUCacheFlags);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to set page attributes"));
		goto e_free_pages;
	}
	else
	{
		if (psPageArrayData->bIsCMA)
		{
			OSFreeMem(ppsPageAttributeArray);
		}
	}

	/* Update metadata */
	psPageArrayData->iNumPagesAllocated = psPageArrayData->uiTotalNumPages;
	return PVRSRV_OK;

/* Error path */
e_free_pages:
	{
		IMG_UINT32 ui32PageToFree;

		if (psPageArrayData->bIsCMA)
		{
			IMG_UINT32 uiDevArrayIndex = uiArrayIndex >> ui32Order;
			IMG_UINT32 uiDevPageSize = PAGE_SIZE << ui32Order;
			PVR_ASSERT(ui32Order == ui32MinOrder);

			if (ppsPageAttributeArray)
			{
				OSFreeMem(ppsPageAttributeArray);
			}

			for (ui32PageToFree = 0; ui32PageToFree < uiDevArrayIndex; ui32PageToFree++)
			{
				_FreeOSPage_CMA(psPageArrayData->psDevNode->psDevConfig->pvOSDevice,
								uiDevPageSize,
								ui32MinOrder,
								psPageArrayData->dmavirtarray[ui32PageToFree],
								psPageArrayData->dmaphysarray[ui32PageToFree],
								ppsPageArray[ui32PageToFree]);
				psPageArrayData->dmaphysarray[ui32PageToFree]= (dma_addr_t)0;
				psPageArrayData->dmavirtarray[ui32PageToFree] = NULL;
				ppsPageArray[ui32PageToFree] = INVALID_PAGE;
			}
		}
		else
		{
			/* Free the pages we got from the pool */
			for(ui32PageToFree = 0; ui32PageToFree < uiPagesFromPool; ui32PageToFree++)
			{
				_FreeOSPage(ui32MinOrder,
							psPageArrayData->bUnsetMemoryType,
							ppsPageArray[ui32PageToFree]);
				ppsPageArray[ui32PageToFree] = INVALID_PAGE;
			}

			for (ui32PageToFree = uiPagesFromPool; ui32PageToFree < uiArrayIndex; ui32PageToFree++)
			{
				_FreeOSPage(ui32MinOrder, IMG_FALSE, ppsPageArray[ui32PageToFree]);	
				ppsPageArray[ui32PageToFree] = INVALID_PAGE;
			}
		}

		return eError;
	}
}

/* Allocation of OS pages: This function is used for sparse allocations.
 *
 * Sparse allocations provide only a proportion of sparse physical backing within the total
 * virtual range. Currently we only support sparse allocations on device pages that are OS
 * page sized.
*/
static PVRSRV_ERROR
_AllocOSPages_Sparse(PMR_OSPAGEARRAY_DATA *psPageArrayData,
					 IMG_UINT32 *puiAllocIndices,
					 IMG_UINT32 uiPagesToAlloc)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 i;
	struct page **ppsPageArray = psPageArrayData->pagearray;
	IMG_UINT32 uiOrder;
	IMG_UINT32 uiPagesFromPool = 0;
	unsigned int gfp_flags = _GetGFPFlags(psPageArrayData);

	 /* We use this page array to receive pages from the pool and then reuse it afterwards to
	 * store pages that need their cache attribute changed on x86*/
	struct page **ppsTempPageArray;
	IMG_UINT32 uiTempPageArrayIndex = 0;

	/* Allocate the temporary page array that we need here to receive pages
	 * from the pool and to store pages that need their caching attributes changed */
	ppsTempPageArray = OSAllocMem(sizeof(struct page*) * uiPagesToAlloc);
	if (ppsTempPageArray == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed metadata allocation", __FUNCTION__));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e_exit;
	}

	uiOrder = psPageArrayData->uiLog2DevPageSize - PAGE_SHIFT;

	/* Check the requested number of pages if they fit in the page array */
	if(psPageArrayData->uiTotalNumPages < \
				(psPageArrayData->iNumPagesAllocated + uiPagesToAlloc))
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Trying to allocate more pages than this buffer can handle, "
				 "Request + Allocated < Max! Request %u, Allocated %u, Max %u.",
				 __FUNCTION__,
				 uiPagesToAlloc,
				 psPageArrayData->iNumPagesAllocated,
				 psPageArrayData->uiTotalNumPages));
		eError = PVRSRV_ERROR_PMR_BAD_MAPPINGTABLE_SIZE;
		goto e_free_temp_array;
	}

	/* Try to get pages from the pool since it is faster */
	_GetPagesFromPoolLocked(psPageArrayData->psDevNode,
							psPageArrayData->ui32CPUCacheFlags,
							uiPagesToAlloc,
							uiOrder,
							psPageArrayData->bZero,
							ppsTempPageArray,
							&uiPagesFromPool);

	/* Allocate pages from the OS or move the pages that we got from the pool
	 * to the page array */
	DisableOOMKiller();
	for (i = 0; i < uiPagesToAlloc; i++)
	{
		/* Check if the indices we are allocating are in range */
		if (puiAllocIndices[i] >= psPageArrayData->uiTotalNumPages)
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: Given alloc index %u at %u is larger than page array %u.",
					 __FUNCTION__,
					 i,
					 puiAllocIndices[i],
					 psPageArrayData->uiTotalNumPages));
			eError = PVRSRV_ERROR_DEVICEMEM_OUT_OF_RANGE;
			goto e_free_pages;
		}

		/* Check if there is not already a page allocated at this position */
		if (INVALID_PAGE != ppsPageArray[puiAllocIndices[i]])
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: Mapping number %u at page array index %u already exists",
					 __func__,
					 i,
					 puiAllocIndices[i]));
			eError = PVRSRV_ERROR_PMR_MAPPING_ALREADY_EXISTS;
			goto e_free_pages;
		}

		/* Finally assign a page to the array.
		 * Either from the pool or allocate a new one. */
		if (uiPagesFromPool != 0)
		{
			uiPagesFromPool--;
			ppsPageArray[puiAllocIndices[i]] =  ppsTempPageArray[uiPagesFromPool];
		}
		else
		{
			ppsPageArray[puiAllocIndices[i]] = alloc_pages(gfp_flags, uiOrder);
			if(ppsPageArray[puiAllocIndices[i]] != NULL)
			{
				/* Reusing the temp page array if it has no pool pages anymore */
				ppsTempPageArray[uiTempPageArrayIndex] = ppsPageArray[puiAllocIndices[i]];
				uiTempPageArrayIndex++;
			}
			else
			{
				/* Failed to alloc pages at required contiguity. Failed allocation */
				PVR_DPF((PVR_DBG_ERROR,
						 "%s: alloc_pages failed to honour request at %u of %u, flags = %x, order = %u",
						 __FUNCTION__,
						 i,
						 uiPagesToAlloc,
						 gfp_flags,
						 uiOrder));
				eError = PVRSRV_ERROR_PMR_FAILED_TO_ALLOC_PAGES;
				goto e_free_pages;
			}
		}
	}
	EnableOOMKiller();

	/* Do the cache management as required */
	eError = _ApplyOSPagesAttribute(psPageArrayData->psDevNode,
									ppsTempPageArray,
									uiTempPageArrayIndex,
									psPageArrayData->bZero,
									psPageArrayData->ui32CPUCacheFlags);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to set page attributes"));
		goto e_free_pages;
	}

	/* Update metadata */
	psPageArrayData->iNumPagesAllocated += uiPagesToAlloc;

	/* Free temporary page array */
	OSFreeMem(ppsTempPageArray);
	return PVRSRV_OK;

/* Error path */
e_free_pages:
	{
		IMG_UINT32 ui32PageToFree;

		EnableOOMKiller();

		/* Free the pages we got from the pool */
		for(ui32PageToFree = 0; ui32PageToFree < uiPagesFromPool; ui32PageToFree++)
		{
			_FreeOSPage(0,
						psPageArrayData->bUnsetMemoryType,
						ppsTempPageArray[ui32PageToFree]);
		}

		/* Free the pages we just allocated from the OS */
		for(ui32PageToFree = uiPagesFromPool; ui32PageToFree < i; ui32PageToFree++)
		{
			_FreeOSPage(0,
						IMG_FALSE,
						ppsPageArray[puiAllocIndices[ui32PageToFree]]);

			ppsPageArray[puiAllocIndices[ui32PageToFree]] = (struct page *) INVALID_PAGE;
		}
	}

e_free_temp_array:
	OSFreeMem(ppsTempPageArray);

e_exit:
	return eError;
}

/* Allocate pages for a given page array.
 *
 * The executed allocation path depends whether an array with allocation
 * indices has been passed or not */
static PVRSRV_ERROR
_AllocOSPages(PMR_OSPAGEARRAY_DATA *psPageArrayData,
			  IMG_UINT32 *puiAllocIndices,
			  IMG_UINT32 uiPagesToAlloc)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 i;
	struct page **ppsPageArray;

	/* Sanity checks */
	PVR_ASSERT(NULL != psPageArrayData);
	if (psPageArrayData->bIsCMA)
	{
		PVR_ASSERT(psPageArrayData->dmaphysarray != NULL);
		PVR_ASSERT(psPageArrayData->dmavirtarray != NULL);
	}
	PVR_ASSERT(psPageArrayData->pagearray != NULL);
	PVR_ASSERT(0 <= psPageArrayData->iNumPagesAllocated);

	ppsPageArray = psPageArrayData->pagearray;

	/* Go the sparse alloc path if we have an array with alloc indices.*/
	if (puiAllocIndices != NULL)
	{
		eError =  _AllocOSPages_Sparse(psPageArrayData,
									   puiAllocIndices,
									   uiPagesToAlloc);
	}
	else
	{
		eError =  _AllocOSPages_Fast(psPageArrayData);
	}

	if (eError != PVRSRV_OK)
	{
		goto e_exit;
	}

	if (psPageArrayData->bPoisonOnAlloc)
	{
		for (i = 0; i < uiPagesToAlloc; i++)
		{
			IMG_UINT32 uiIdx = puiAllocIndices ? puiAllocIndices[i] : i;
			_PoisonPages(ppsPageArray[uiIdx],
						 0,
						 _AllocPoison,
						 _AllocPoisonSize);
		}
	}

	_DumpPageArray(ppsPageArray, psPageArrayData->uiTotalNumPages);

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	{
		for (i = 0; i < uiPagesToAlloc; i++)
		{
			IMG_CPU_PHYADDR sCPUPhysAddr;
			IMG_UINT32 uiIdx = puiAllocIndices ? puiAllocIndices[i] : i;

			sCPUPhysAddr.uiAddr = page_to_phys(ppsPageArray[uiIdx]);
			PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES,
										 NULL,
										 sCPUPhysAddr,
										 1 << psPageArrayData->uiLog2DevPageSize,
										 NULL);
		}
	}
#else
	PVRSRVStatsIncrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES, uiPagesToAlloc * PAGE_SIZE);
#endif
#endif

	PVR_DPF((PVR_DBG_MESSAGE, "physmem_osmem_linux.c: allocated OS memory for PMR @0x%p", psPageArrayData));
	return PVRSRV_OK;

e_exit:
	return eError;
}

/* Same as _FreeOSPage except free memory using DMA framework */
static INLINE void
_FreeOSPage_CMA(struct device *dev,
				size_t alloc_size,
				IMG_UINT32 uiOrder,
				void *virt_addr,
				dma_addr_t dev_addr,
				struct page *psPage)
{
	if (DMA_IS_ALLOCPG_ADDR(dev_addr))
	{
#if defined(CONFIG_X86)
		void *pvPageVAddr = page_address(psPage);
		if (pvPageVAddr)
		{
			int ret = set_memory_wb((unsigned long)pvPageVAddr, 1);
			if (ret)
			{
				PVR_DPF((PVR_DBG_ERROR, 
						"%s: Failed to reset page attribute",
						__FUNCTION__));
			}
		}
#endif
		__free_pages(psPage, uiOrder);
	}
	else
	{
		dma_free_coherent(dev, alloc_size, virt_addr, DMA_GET_ADDR(dev_addr));
	}
}

/* Free a single page back to the OS.
 * Make sure the cache type is set back to the default value.
 *
 * Note:
 * We must _only_ check bUnsetMemoryType in the case where we need to free
 * the page back to the OS since we may have to revert the cache properties
 * of the page to the default as given by the OS when it was allocated. */
static void
_FreeOSPage(IMG_UINT32 uiOrder,
			IMG_BOOL bUnsetMemoryType,
			struct page *psPage)
{

#if defined(CONFIG_X86)
	void *pvPageVAddr;
	pvPageVAddr = page_address(psPage);

	if (pvPageVAddr && bUnsetMemoryType == IMG_TRUE)
	{
		int ret;

		ret = set_memory_wb((unsigned long)pvPageVAddr, 1);
		if (ret)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to reset page attribute", __FUNCTION__));
		}
	}
#else
	PVR_UNREFERENCED_PARAMETER(bUnsetMemoryType);
#endif
	__free_pages(psPage, uiOrder);
}

/* Free the struct holding the metadata */
static PVRSRV_ERROR
_FreeOSPagesArray(PMR_OSPAGEARRAY_DATA *psPageArrayData)
{
	PVR_DPF((PVR_DBG_MESSAGE, "physmem_osmem_linux.c: freed OS memory for PMR @0x%p", psPageArrayData));

	/* Check if the page array actually still exists.
	 * It might be the case that has been moved to the page pool */
	if (psPageArrayData->pagearray != NULL)
	{
		OSFreeMemNoStats(psPageArrayData->pagearray);
	}

	kmem_cache_free(g_psLinuxPageArray, psPageArrayData);

	return PVRSRV_OK;
}

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
/* _FreeOSPages_MemStats: Depends on the bridge lock already being held */
static void
_FreeOSPages_MemStats(PMR_OSPAGEARRAY_DATA *psPageArrayData,
					IMG_UINT32 *pai32FreeIndices,
					IMG_UINT32 ui32NumPages)
{
	struct page **ppsPageArray;
	#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	IMG_UINT32 ui32PageIndex;
	#endif

	PVR_DPF((PVR_DBG_MESSAGE, "%s: psPageArrayData %p, ui32NumPages %u", __FUNCTION__, psPageArrayData, ui32NumPages));
	PVR_ASSERT(psPageArrayData->iNumPagesAllocated != 0);

	ppsPageArray = psPageArrayData->pagearray;

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
		PVRSRVStatsDecrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES, ui32NumPages * PAGE_SIZE);
#else
		for(ui32PageIndex = 0; ui32PageIndex < ui32NumPages; ui32PageIndex++)
		{
			IMG_CPU_PHYADDR sCPUPhysAddr;
			IMG_UINT32 uiArrayIndex = (pai32FreeIndices) ? pai32FreeIndices[ui32PageIndex] : ui32PageIndex;

			sCPUPhysAddr.uiAddr = page_to_phys(ppsPageArray[uiArrayIndex]);
			PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES, sCPUPhysAddr.uiAddr);
		}
#endif
#endif
}
#endif /* PVRSRV_ENABLE_PROCESS_STATS */

/* Free all or some pages from a sparse page array */
static PVRSRV_ERROR
_FreeOSPages_Sparse(PMR_OSPAGEARRAY_DATA *psPageArrayData,
					IMG_UINT32 *pai32FreeIndices,
					IMG_UINT32 ui32FreePageCount)
{
	IMG_BOOL bSuccess;
	IMG_UINT32 uiOrder;
	IMG_UINT32 uiPageIndex, i = 0, uiTempIdx;
	struct page **ppsPageArray;
	IMG_UINT32 uiNumPages;

	struct page **ppsTempPageArray;
	IMG_UINT32 uiTempArraySize;

	/* We really should have something to free before we call this */
	PVR_ASSERT(psPageArrayData->iNumPagesAllocated != 0);

	if(pai32FreeIndices == NULL)
	{
		uiNumPages = psPageArrayData->uiTotalNumPages;
		uiTempArraySize = psPageArrayData->iNumPagesAllocated;
	}
	else
	{
		uiNumPages = ui32FreePageCount;
		uiTempArraySize = ui32FreePageCount;
	}

	/* OSAllocMemNoStats required because this code may be run without the bridge lock held */
	ppsTempPageArray = OSAllocMemNoStats(sizeof(struct page*) * uiTempArraySize);
	if (ppsTempPageArray == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed free_pages metadata allocation", __FUNCTION__));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	ppsPageArray = psPageArrayData->pagearray;
	uiOrder = psPageArrayData->uiLog2DevPageSize - PAGE_SHIFT;

	/* Poison if necessary */
	if (psPageArrayData->bPoisonOnFree)
	{
		for (i  = 0; i  < uiNumPages; i ++)
		{
			uiPageIndex = pai32FreeIndices ? pai32FreeIndices[i] : i ;
			if(INVALID_PAGE != ppsPageArray[uiPageIndex])
			{
				_PoisonPages(ppsPageArray[uiPageIndex],
							 0,
							 _FreePoison,
							 _FreePoisonSize);
			}
		}
	}

	/* Put pages in a contiguous array so further processing is easier */
	uiTempIdx = 0;
	for (i = 0; i < uiNumPages; i++)
	{
		uiPageIndex = pai32FreeIndices ? pai32FreeIndices[i] : i;
		if(INVALID_PAGE != ppsPageArray[uiPageIndex])
		{
			ppsTempPageArray[uiTempIdx] = ppsPageArray[uiPageIndex];
			uiTempIdx++;
			ppsPageArray[uiPageIndex] = (struct page *) INVALID_PAGE;
		}
	}

	/* Try to move the temp page array to the pool */
	bSuccess = _PutPagesToPoolLocked(psPageArrayData->ui32CPUCacheFlags,
									 ppsTempPageArray,
									 psPageArrayData->bUnpinned,
									 uiOrder,
									 uiTempIdx);
	if (bSuccess)
	{
		goto exit_ok;
	}

	/* Free pages and reset page caching attributes on x86 */
#if defined(CONFIG_X86)
	if (uiTempIdx != 0 && psPageArrayData->bUnsetMemoryType == IMG_TRUE)
	{
		int iError;
		iError = set_pages_array_wb(ppsTempPageArray, uiTempIdx);

		if (iError)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to reset page attributes", __FUNCTION__));
		}
	}
#endif

	/* Free the pages */
	for (i = 0; i < uiTempIdx; i++)
	{
		__free_pages(ppsTempPageArray[i], uiOrder);
	}

	/* Free the temp page array here if it did not move to the pool */
	OSFreeMemNoStats(ppsTempPageArray);

exit_ok:
	/* Update metadata */
	psPageArrayData->iNumPagesAllocated -= uiTempIdx;
	PVR_ASSERT(0 <= psPageArrayData->iNumPagesAllocated);
	return PVRSRV_OK;
}

/* Free all the pages in a page array */
static PVRSRV_ERROR
_FreeOSPages_Fast(PMR_OSPAGEARRAY_DATA *psPageArrayData)
{
	IMG_BOOL bSuccess;
	IMG_UINT32 uiOrder;
	IMG_UINT32 i = 0;
	IMG_UINT32 uiNumPages = psPageArrayData->uiTotalNumPages;

	struct page **ppsPageArray = psPageArrayData->pagearray;
	uiOrder = psPageArrayData->uiLog2DevPageSize - PAGE_SHIFT;

	/* We really should have something to free before we call this */
	PVR_ASSERT(psPageArrayData->iNumPagesAllocated != 0);

	/* Poison pages if necessary */
	if (psPageArrayData->bPoisonOnFree)
	{
		for (i = 0; i < uiNumPages; i++)
		{
			_PoisonPages(ppsPageArray[i],
						 0,
						 _FreePoison,
						 _FreePoisonSize);
		}
	}

	/* Try to move the page array to the pool */
	bSuccess = _PutPagesToPoolLocked(psPageArrayData->ui32CPUCacheFlags,
									 ppsPageArray,
									 psPageArrayData->bUnpinned,
									 uiOrder,
									 uiNumPages);
	if (bSuccess)
	{
		psPageArrayData->pagearray = NULL;
		goto exit_ok;
	}

	if (psPageArrayData->bIsCMA)
	{
		IMG_UINT32 uiDevNumPages = uiNumPages >> uiOrder;
		IMG_UINT32 uiDevPageSize = PAGE_SIZE << uiOrder;

		for (i = 0; i < uiDevNumPages; i++)
		{
			_FreeOSPage_CMA(psPageArrayData->psDevNode->psDevConfig->pvOSDevice,
							uiDevPageSize,
							uiOrder,
							psPageArrayData->dmavirtarray[i],
							psPageArrayData->dmaphysarray[i],
							ppsPageArray[i]);
  			psPageArrayData->dmaphysarray[i] = (dma_addr_t)0;
			psPageArrayData->dmavirtarray[i] = NULL;
			ppsPageArray[i] = INVALID_PAGE;
		}
	}
	else
	{
#if defined(CONFIG_X86)
		if (psPageArrayData->bUnsetMemoryType == IMG_TRUE)
		{
			int ret;

			ret = set_pages_array_wb(ppsPageArray, uiNumPages);
			if (ret)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Failed to reset page attributes", __FUNCTION__));
			}
		}
#endif

		for (i = 0; i < uiNumPages; i++)
		{
			_FreeOSPage(uiOrder, IMG_FALSE, ppsPageArray[i]);
			ppsPageArray[i] = INVALID_PAGE;
		}
	}

exit_ok:
	/* Update metadata */
	psPageArrayData->iNumPagesAllocated = 0;
	return PVRSRV_OK;
}

/* Free pages from a page array.
 * Takes care of mem stats and chooses correct free path depending on parameters. */
static PVRSRV_ERROR
_FreeOSPages(PMR_OSPAGEARRAY_DATA *psPageArrayData,
			 IMG_UINT32 *pai32FreeIndices,
			 IMG_UINT32 ui32FreePageCount)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 uiNumPages;

	/* Check how many pages do we have to free */
	if(pai32FreeIndices == NULL)
	{
		uiNumPages = psPageArrayData->iNumPagesAllocated;
	}
	else
	{
		uiNumPages = ui32FreePageCount;
	}

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
 	_FreeOSPages_MemStats(psPageArrayData, pai32FreeIndices, uiNumPages);
#endif

	/* Go the sparse or non-sparse path */
	if (psPageArrayData->iNumPagesAllocated != psPageArrayData->uiTotalNumPages
		|| pai32FreeIndices != NULL)
	{
		eError = _FreeOSPages_Sparse(psPageArrayData,
									 pai32FreeIndices,
									 uiNumPages);
	}
	else
	{
		eError = _FreeOSPages_Fast(psPageArrayData);
	}

	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "_FreeOSPages_FreePages failed"));
	}

	_DumpPageArray(psPageArrayData->pagearray, psPageArrayData->uiTotalNumPages);

	return eError;
}

/*
 *
 * Implementation of callback functions
 *
 */

/* destructor func is called after last reference disappears, but
   before PMR itself is freed. */
static PVRSRV_ERROR
PMRFinalizeOSMem(PMR_IMPL_PRIVDATA pvPriv)
{
	PVRSRV_ERROR eError;
	PMR_OSPAGEARRAY_DATA *psOSPageArrayData = pvPriv;


	/*  We can't free pages until now. */
	if (psOSPageArrayData->iNumPagesAllocated != 0)
	{
		_PagePoolLock();
		if (psOSPageArrayData->bUnpinned == IMG_TRUE)
		{
			_RemoveUnpinListEntryUnlocked(psOSPageArrayData);
		}
		_PagePoolUnlock();

		eError = _FreeOSPages(psOSPageArrayData,
							  NULL,
							  0);
		PVR_ASSERT (eError == PVRSRV_OK); /* can we do better? */
	}

	eError = _FreeOSPagesArray(psOSPageArrayData);
	PVR_ASSERT (eError == PVRSRV_OK); /* can we do better? */
	return PVRSRV_OK;
}

/* callback function for locking the system physical page addresses.
   This function must be called before the lookup address func. */
static PVRSRV_ERROR
PMRLockSysPhysAddressesOSMem(PMR_IMPL_PRIVDATA pvPriv)
{
	PVRSRV_ERROR eError;
	PMR_OSPAGEARRAY_DATA *psOSPageArrayData = pvPriv;

	if (psOSPageArrayData->bOnDemand)
	{
		/* Allocate Memory for deferred allocation */
		eError = _AllocOSPages(psOSPageArrayData, NULL, psOSPageArrayData->uiTotalNumPages);
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

	eError = PVRSRV_OK;
	return eError;
}

static PVRSRV_ERROR
PMRUnlockSysPhysAddressesOSMem(PMR_IMPL_PRIVDATA pvPriv)
{
	/* Just drops the refcount. */
	PVRSRV_ERROR eError = PVRSRV_OK;
	PMR_OSPAGEARRAY_DATA *psOSPageArrayData = pvPriv;

	if (psOSPageArrayData->bOnDemand)
	{
		/* Free Memory for deferred allocation */
		eError = _FreeOSPages(psOSPageArrayData,
							  NULL,
							  0);
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

	PVR_ASSERT (eError == PVRSRV_OK);
	return eError;
}

/* N.B.  It is assumed that PMRLockSysPhysAddressesOSMem() is called _before_ this function! */
static PVRSRV_ERROR
PMRSysPhysAddrOSMem(PMR_IMPL_PRIVDATA pvPriv,
					IMG_UINT32 ui32Log2PageSize,
					IMG_UINT32 ui32NumOfPages,
					IMG_DEVMEM_OFFSET_T *puiOffset,
					IMG_BOOL *pbValid,
					IMG_DEV_PHYADDR *psDevPAddr)
{
	const PMR_OSPAGEARRAY_DATA *psOSPageArrayData = pvPriv;
	IMG_UINT32 uiPageSize = 1U << psOSPageArrayData->uiLog2DevPageSize;
	IMG_UINT32 uiInPageOffset;
	IMG_UINT32 uiPageIndex;
	IMG_UINT32 uiIdx;

	if (psOSPageArrayData->uiLog2DevPageSize < ui32Log2PageSize)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Requested physical addresses from PMR "
		         "for incompatible contiguity %u!",
		         __FUNCTION__,
		         ui32Log2PageSize));
		return PVRSRV_ERROR_PMR_INCOMPATIBLE_CONTIGUITY;
	}

	for (uiIdx=0; uiIdx < ui32NumOfPages; uiIdx++)
	{
		if (pbValid[uiIdx])
		{
			uiPageIndex = puiOffset[uiIdx] >> psOSPageArrayData->uiLog2DevPageSize;
			uiInPageOffset = puiOffset[uiIdx] - ((IMG_DEVMEM_OFFSET_T)uiPageIndex << psOSPageArrayData->uiLog2DevPageSize);

			PVR_ASSERT(uiPageIndex < psOSPageArrayData->uiTotalNumPages);
			PVR_ASSERT(uiInPageOffset < uiPageSize);

			psDevPAddr[uiIdx].uiAddr = page_to_phys(psOSPageArrayData->pagearray[uiPageIndex]);
			psDevPAddr[uiIdx].uiAddr += uiInPageOffset;
		}
	}

	return PVRSRV_OK;
}

typedef struct _PMR_OSPAGEARRAY_KERNMAP_DATA_ {
	void *pvBase;
	IMG_UINT32 ui32PageCount;
} PMR_OSPAGEARRAY_KERNMAP_DATA;

static PVRSRV_ERROR
PMRAcquireKernelMappingDataOSMem(PMR_IMPL_PRIVDATA pvPriv,
								 size_t uiOffset,
								 size_t uiSize,
								 void **ppvKernelAddressOut,
								 IMG_HANDLE *phHandleOut,
								 PMR_FLAGS_T ulFlags)
{
	PVRSRV_ERROR eError;
	PMR_OSPAGEARRAY_DATA *psOSPageArrayData = pvPriv;
	void *pvAddress;
	pgprot_t prot = PAGE_KERNEL;
	IMG_UINT32 ui32PageOffset;
	size_t uiMapOffset;
	IMG_UINT32 ui32PageCount;
	IMG_UINT32 uiLog2DevPageSize = psOSPageArrayData->uiLog2DevPageSize;
	PMR_OSPAGEARRAY_KERNMAP_DATA *psData;

	/*
		Zero offset and size as a special meaning which means map in the
		whole of the PMR, this is due to fact that the places that call
		this callback might not have access to be able to determine the
		physical size
	*/
	if ((uiOffset == 0) && (uiSize == 0))
	{
		ui32PageOffset = 0;
		uiMapOffset = 0;
		ui32PageCount = psOSPageArrayData->iNumPagesAllocated;
	}
	else
	{
		size_t uiEndoffset;

		ui32PageOffset = uiOffset >> uiLog2DevPageSize;
		uiMapOffset = uiOffset - (ui32PageOffset << uiLog2DevPageSize);
		uiEndoffset = uiOffset + uiSize - 1;
		// Add one as we want the count, not the offset
		ui32PageCount = (uiEndoffset >> uiLog2DevPageSize) + 1;
		ui32PageCount -= ui32PageOffset;
	}

	if (psOSPageArrayData->bIsCMA)
	{
		prot = pgprot_noncached(prot);
	}
	else
	{
		switch (PVRSRV_CPU_CACHE_MODE(psOSPageArrayData->ui32CPUCacheFlags))
		{
			case PVRSRV_MEMALLOCFLAG_CPU_UNCACHED:
					prot = pgprot_noncached(prot);
					break;

			case PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE:
					prot = pgprot_writecombine(prot);
					break;

			case PVRSRV_MEMALLOCFLAG_CPU_CACHED:
					break;

			default:
					eError = PVRSRV_ERROR_INVALID_PARAMS;
					goto e0;
		}
	}

	psData = OSAllocMem(sizeof(*psData));
	if (psData == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}

#if !defined(CONFIG_64BIT) || defined(PVRSRV_FORCE_SLOWER_VMAP_ON_64BIT_BUILDS)
	pvAddress = vmap(&psOSPageArrayData->pagearray[ui32PageOffset],
					 ui32PageCount,
					 VM_READ | VM_WRITE,
					 prot);
#else
	pvAddress = vm_map_ram(&psOSPageArrayData->pagearray[ui32PageOffset],
						   ui32PageCount,
						   -1,
						   prot);
#endif
	if (pvAddress == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e1;
	}

	*ppvKernelAddressOut = pvAddress + uiMapOffset;
	psData->pvBase = pvAddress;
	psData->ui32PageCount = ui32PageCount;
	*phHandleOut = psData;

	return PVRSRV_OK;

	/*
	  error exit paths follow
	*/
 e1:
	OSFreeMem(psData);
 e0:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

static void PMRReleaseKernelMappingDataOSMem(PMR_IMPL_PRIVDATA pvPriv,
											 IMG_HANDLE hHandle)
{
    PMR_OSPAGEARRAY_KERNMAP_DATA *psData = hHandle;
	PVR_UNREFERENCED_PARAMETER(pvPriv);

#if !defined(CONFIG_64BIT) || defined(PVRSRV_FORCE_SLOWER_VMAP_ON_64BIT_BUILDS)
	vunmap(psData->pvBase);
#else
	vm_unmap_ram(psData->pvBase, psData->ui32PageCount);
#endif
	OSFreeMem(psData);
}

static
PVRSRV_ERROR PMRUnpinOSMem(PMR_IMPL_PRIVDATA pPriv)
{
	PMR_OSPAGEARRAY_DATA *psOSPageArrayData = pPriv;
	PVRSRV_ERROR eError = PVRSRV_OK;

	/* Lock down the pool and add the array to the unpin list */
	_PagePoolLock();

	/* Sanity check */
	PVR_ASSERT(psOSPageArrayData->bUnpinned == IMG_FALSE);
	PVR_ASSERT(psOSPageArrayData->bOnDemand == IMG_FALSE);

	eError = _AddUnpinListEntryUnlocked(psOSPageArrayData);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Not able to add allocation to unpinned list (%d).",
		         __FUNCTION__,
		         eError));

		goto e_exit;
	}

	psOSPageArrayData->bUnpinned = IMG_TRUE;

e_exit:
	_PagePoolUnlock();
	return eError;
}

static
PVRSRV_ERROR PMRPinOSMem(PMR_IMPL_PRIVDATA pPriv,
						PMR_MAPPING_TABLE *psMappingTable)
{
	PVRSRV_ERROR eError;
	PMR_OSPAGEARRAY_DATA *psOSPageArrayData = pPriv;
	IMG_UINT32  *pui32MapTable = NULL;
	IMG_UINT32 i,j=0, ui32Temp=0;

	_PagePoolLock();

	/* Sanity check */
	PVR_ASSERT(psOSPageArrayData->bUnpinned == IMG_TRUE);

	psOSPageArrayData->bUnpinned = IMG_FALSE;

	/* If there are still pages in the array remove entries from the pool */
	if (psOSPageArrayData->iNumPagesAllocated != 0)
	{
		_RemoveUnpinListEntryUnlocked(psOSPageArrayData);
		_PagePoolUnlock();

		eError = PVRSRV_OK;
		goto e_exit_mapalloc_failure;
	}
	_PagePoolUnlock();

	/* If pages were reclaimed we allocate new ones and
	 * return PVRSRV_ERROR_PMR_NEW_MEMORY  */
	if (psMappingTable->ui32NumVirtChunks == 1)
	{
		eError = _AllocOSPages(psOSPageArrayData, NULL, psOSPageArrayData->uiTotalNumPages);
	}
	else
	{
		pui32MapTable = (IMG_UINT32 *)OSAllocMem(sizeof(*pui32MapTable) * psMappingTable->ui32NumPhysChunks);
		if(NULL == pui32MapTable)
		{
			eError = PVRSRV_ERROR_PMR_FAILED_TO_ALLOC_PAGES;
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: Not able to Alloc Map Table.",
					 __FUNCTION__));
			goto e_exit_mapalloc_failure;
		}

		for (i = 0,j=0; i < psMappingTable->ui32NumVirtChunks; i++)
		{
			ui32Temp = psMappingTable->aui32Translation[i];
			if (TRANSLATION_INVALID != ui32Temp)
			{
				pui32MapTable[j++] = ui32Temp;
			}
		}
		eError = _AllocOSPages(psOSPageArrayData, pui32MapTable, psMappingTable->ui32NumPhysChunks);
	}

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Not able to get new pages for unpinned allocation.",
				 __FUNCTION__));

		eError = PVRSRV_ERROR_PMR_FAILED_TO_ALLOC_PAGES;
		goto e_exit;
	}

	PVR_DPF((PVR_DBG_MESSAGE,
			 "%s: Allocating new pages for unpinned allocation. "
			 "Old content is lost!",
			 __FUNCTION__));

	eError = PVRSRV_ERROR_PMR_NEW_MEMORY;

e_exit:
	OSFreeMem(pui32MapTable);
e_exit_mapalloc_failure:
	return eError;
}

/*************************************************************************/ /*!
@Function       PMRChangeSparseMemOSMem
@Description    This function Changes the sparse mapping by allocating & freeing
				of pages. It does also change the GPU and CPU maps accordingly
@Return         PVRSRV_ERROR failure code
*/ /**************************************************************************/
static PVRSRV_ERROR
PMRChangeSparseMemOSMem(PMR_IMPL_PRIVDATA pPriv,
						const PMR *psPMR,
						IMG_UINT32 ui32AllocPageCount,
						IMG_UINT32 *pai32AllocIndices,
						IMG_UINT32 ui32FreePageCount,
						IMG_UINT32 *pai32FreeIndices,
						IMG_UINT32 uiFlags)
{
	PVRSRV_ERROR eError;

	PMR_MAPPING_TABLE *psPMRMapTable = PMR_GetMappigTable(psPMR);
	PMR_OSPAGEARRAY_DATA *psPMRPageArrayData = (PMR_OSPAGEARRAY_DATA *)pPriv;
	struct page **psPageArray = psPMRPageArrayData->pagearray;
	struct page *psPage;

	IMG_UINT32 ui32AdtnlAllocPages = 0; /*<! Number of pages to alloc from the OS */
	IMG_UINT32 ui32AdtnlFreePages = 0; /*<! Number of pages to free back to the OS */
	IMG_UINT32 ui32CommonRequestCount = 0; /*<! Number of pages to move position in the page array */
	IMG_UINT32 ui32Loop = 0;
	IMG_UINT32 ui32Index = 0;
	IMG_UINT32 uiAllocpgidx ;
	IMG_UINT32 uiFreepgidx;
	IMG_UINT32 ui32Order =  psPMRPageArrayData->uiLog2DevPageSize - PAGE_SHIFT;

	/* Check SPARSE flags and calculate pages to allocate and free */
	if (SPARSE_RESIZE_BOTH == (uiFlags & SPARSE_RESIZE_BOTH))
	{
		ui32CommonRequestCount = (ui32AllocPageCount > ui32FreePageCount) ?
				ui32FreePageCount : ui32AllocPageCount;

		PDUMP_PANIC(SPARSEMEM_SWAP, "Request to swap alloc & free pages not supported");
	}

	if (SPARSE_RESIZE_ALLOC == (uiFlags & SPARSE_RESIZE_ALLOC))
	{
		ui32AdtnlAllocPages = ui32AllocPageCount - ui32CommonRequestCount;
	}
	else
	{
		ui32AllocPageCount = 0;
	}

	if (SPARSE_RESIZE_FREE == (uiFlags & SPARSE_RESIZE_FREE))
	{
		ui32AdtnlFreePages = ui32FreePageCount - ui32CommonRequestCount;
	}
	else
	{
		ui32FreePageCount = 0;
	}

	if (0 == (ui32CommonRequestCount || ui32AdtnlAllocPages || ui32AdtnlFreePages))
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		return eError;
	}

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

	/* Validate the free indices */
	if (ui32FreePageCount)
	{
		if (NULL != pai32FreeIndices){

			for (ui32Loop = 0; ui32Loop < ui32FreePageCount; ui32Loop++)
			{
				uiFreepgidx = pai32FreeIndices[ui32Loop];

				if (uiFreepgidx > psPMRPageArrayData->uiTotalNumPages)
				{
					eError = PVRSRV_ERROR_DEVICEMEM_OUT_OF_RANGE;
					goto e0;
				}

				if (INVALID_PAGE == psPageArray[uiFreepgidx])
				{
					eError = PVRSRV_ERROR_INVALID_PARAMS;
					goto e0;
				}
			}
		}
		else
		{
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			return eError;
		}
	}

	/* Validate the alloc indices */
	for (ui32Loop = ui32AdtnlAllocPages; ui32Loop < ui32AllocPageCount; ui32Loop++)
	{
		uiAllocpgidx = pai32AllocIndices[ui32Loop];

		if (uiAllocpgidx > psPMRPageArrayData->uiTotalNumPages)
		{
			eError = PVRSRV_ERROR_DEVICEMEM_OUT_OF_RANGE;
			goto e0;
		}

		if (SPARSE_REMAP_MEM != (uiFlags & SPARSE_REMAP_MEM))
		{
			if ((INVALID_PAGE !=  psPageArray[uiAllocpgidx]) ||
			    (TRANSLATION_INVALID != psPMRMapTable->aui32Translation[uiAllocpgidx]))
			{
				eError = PVRSRV_ERROR_INVALID_PARAMS;
				goto e0;
			}
		}
		else
		{
			if ((INVALID_PAGE ==  psPageArray[uiAllocpgidx]) ||
			    (TRANSLATION_INVALID == psPMRMapTable->aui32Translation[uiAllocpgidx]) )
			{
				eError = PVRSRV_ERROR_INVALID_PARAMS;
				goto e0;
			}
		}
	}

	ui32Loop = 0;

	/* Allocate new pages from the OS */
	if (0 != ui32AdtnlAllocPages)
	{
			eError = _AllocOSPages(psPMRPageArrayData, pai32AllocIndices, ui32AdtnlAllocPages);
			if (PVRSRV_OK != eError)
			{
				PVR_DPF((PVR_DBG_MESSAGE,
				         "%s: New Addtl Allocation of pages failed",
				         __FUNCTION__));
				goto e0;
			}

			/*Mark the corresponding pages of translation table as valid */
			for (ui32Loop = 0; ui32Loop < ui32AdtnlAllocPages; ui32Loop++)
			{
				psPMRMapTable->aui32Translation[pai32AllocIndices[ui32Loop]] = pai32AllocIndices[ui32Loop];
			}
	}


	ui32Index = ui32Loop;

	/* Move the corresponding free pages to alloc request */
	for (ui32Loop = 0; ui32Loop < ui32CommonRequestCount; ui32Loop++, ui32Index++)
	{
		uiAllocpgidx = pai32AllocIndices[ui32Index];
		uiFreepgidx =  pai32FreeIndices[ui32Loop];
		psPage = psPageArray[uiAllocpgidx];
		psPageArray[uiAllocpgidx] = psPageArray[uiFreepgidx];

		/* Is remap mem used in real world scenario? Should it be turned to a
		 *  debug feature? The condition check needs to be out of loop, will be
		 *  done at later point though after some analysis */
		if (SPARSE_REMAP_MEM != (uiFlags & SPARSE_REMAP_MEM))
		{
			psPMRMapTable->aui32Translation[uiFreepgidx] = TRANSLATION_INVALID;
			psPMRMapTable->aui32Translation[uiAllocpgidx] = uiAllocpgidx;
			psPageArray[uiFreepgidx] = (struct page *)INVALID_PAGE;
		}
		else
		{
			psPageArray[uiFreepgidx] = psPage;
			psPMRMapTable->aui32Translation[uiFreepgidx] = uiFreepgidx;
			psPMRMapTable->aui32Translation[uiAllocpgidx] = uiAllocpgidx;
		}

		/* Be sure to honour the attributes associated with the allocation
		 * such as zeroing, poisoning etc. */
		if (psPMRPageArrayData->bPoisonOnAlloc)
		{
			_PoisonPages(psPageArray[uiAllocpgidx],
			             ui32Order,
			             _AllocPoison,
			             _AllocPoisonSize);
		}
		else
		{
			if (psPMRPageArrayData->bZero)
			{
				char a = 0;
				_PoisonPages(psPageArray[uiAllocpgidx],
				             ui32Order,
				             &a,
				             1);
			}
		}
	}

	/* Free the additional free pages */
	if (0 != ui32AdtnlFreePages)
	{
		eError = _FreeOSPages(psPMRPageArrayData,
		                      &pai32FreeIndices[ui32Loop],
		                      ui32AdtnlFreePages);
		if (eError != PVRSRV_OK)
		{
			goto e0;
		}
		while (ui32Loop < ui32FreePageCount)
		{
			psPMRMapTable->aui32Translation[pai32FreeIndices[ui32Loop]] = TRANSLATION_INVALID;
			ui32Loop++;
		}
	}

	eError = PVRSRV_OK;

e0:
	return eError;
}

/*************************************************************************/ /*!
@Function       PMRChangeSparseMemCPUMapOSMem
@Description    This function Changes CPU maps accordingly
@Return         PVRSRV_ERROR failure code
*/ /**************************************************************************/
static
PVRSRV_ERROR PMRChangeSparseMemCPUMapOSMem(PMR_IMPL_PRIVDATA pPriv,
                                           const PMR *psPMR,
                                           IMG_UINT64 sCpuVAddrBase,
                                           IMG_UINT32 ui32AllocPageCount,
                                           IMG_UINT32 *pai32AllocIndices,
                                           IMG_UINT32 ui32FreePageCount,
                                           IMG_UINT32 *pai32FreeIndices)
{
	struct page **psPageArray;
	PMR_OSPAGEARRAY_DATA *psPMRPageArrayData = (PMR_OSPAGEARRAY_DATA *)pPriv;
	IMG_CPU_PHYADDR sCPUPAddr;

	sCPUPAddr.uiAddr = 0;
	psPageArray = psPMRPageArrayData->pagearray;

	return OSChangeSparseMemCPUAddrMap((void **)psPageArray,
	                                   sCpuVAddrBase,
	                                   sCPUPAddr,
	                                   ui32AllocPageCount,
	                                   pai32AllocIndices,
	                                   ui32FreePageCount,
	                                   pai32FreeIndices,
	                                   IMG_FALSE);
}

static PMR_IMPL_FUNCTAB _sPMROSPFuncTab = {
    .pfnLockPhysAddresses = &PMRLockSysPhysAddressesOSMem,
    .pfnUnlockPhysAddresses = &PMRUnlockSysPhysAddressesOSMem,
    .pfnDevPhysAddr = &PMRSysPhysAddrOSMem,
    .pfnAcquireKernelMappingData = &PMRAcquireKernelMappingDataOSMem,
    .pfnReleaseKernelMappingData = &PMRReleaseKernelMappingDataOSMem,
    .pfnReadBytes = NULL,
    .pfnWriteBytes = NULL,
    .pfnUnpinMem = &PMRUnpinOSMem,
    .pfnPinMem = &PMRPinOSMem,
    .pfnChangeSparseMem = &PMRChangeSparseMemOSMem,
    .pfnChangeSparseMemCPUMap = &PMRChangeSparseMemCPUMapOSMem,
    .pfnFinalize = &PMRFinalizeOSMem,
};

PVRSRV_ERROR
PhysmemNewOSRamBackedPMR(PVRSRV_DEVICE_NODE *psDevNode,
						 IMG_DEVMEM_SIZE_T uiSize,
						 IMG_DEVMEM_SIZE_T uiChunkSize,
						 IMG_UINT32 ui32NumPhysChunks,
						 IMG_UINT32 ui32NumVirtChunks,
						 IMG_UINT32 *puiAllocIndices,
						 IMG_UINT32 uiLog2PageSize,
						 PVRSRV_MEMALLOCFLAGS_T uiFlags,
						 const IMG_CHAR *pszAnnotation,
						 PMR **ppsPMRPtr)
{
	PVRSRV_ERROR eError;
	PVRSRV_ERROR eError2;
	PMR *psPMR;
	struct _PMR_OSPAGEARRAY_DATA_ *psPrivData;
	PMR_FLAGS_T uiPMRFlags;
	PHYS_HEAP *psPhysHeap;
	IMG_BOOL bZero;
	IMG_BOOL bIsCMA;
	IMG_BOOL bPoisonOnAlloc;
	IMG_BOOL bPoisonOnFree;
	IMG_BOOL bOnDemand;
	IMG_BOOL bCpuLocal;
	IMG_BOOL bFwLocal;
	IMG_UINT32 ui32CPUCacheFlags = DevmemCPUCacheMode(psDevNode, uiFlags);
	if (PVRSRV_CHECK_CPU_CACHE_CLEAN(uiFlags))
	{
		ui32CPUCacheFlags |= PVRSRV_MEMALLOCFLAG_CPU_CACHE_CLEAN;
	}

#if defined(PVRSRV_GPUVIRT_GUESTDRV)
	/*
	 * The host driver (but not guest) can still use this factory for firmware
	 * allocations
	 */
	PVR_ASSERT(!PVRSRV_CHECK_FW_LOCAL(uiFlags));
#endif

	/*
	 * Silently round up alignment/pagesize if request was less that PAGE_SHIFT
	 * because it would never be harmful for memory to be _more_ contiguous that
	 * was desired.
	 */
	uiLog2PageSize = PAGE_SHIFT > uiLog2PageSize ? PAGE_SHIFT : uiLog2PageSize;

	/* In case we have a non-sparse allocation tolerate bad requests and round up.
	 * For sparse allocations the users have to make sure to meet the right
	 * requirements. */
	if (ui32NumPhysChunks == ui32NumVirtChunks &&
		ui32NumVirtChunks == 1)
	{
		/* Round up allocation size to at least a full PAGE_SIZE */
		uiSize = PVR_ALIGN(uiSize, PAGE_SIZE);
		uiChunkSize = uiSize;
	}

	/* 
	 * Use CMA framework if order is greater than OS page size; please note
	 * that OSMMapPMRGeneric() has the same expectation as well.
	 */
	bIsCMA = uiLog2PageSize > PAGE_SHIFT ? IMG_TRUE : IMG_FALSE;
	bOnDemand = PVRSRV_CHECK_ON_DEMAND(uiFlags) ? IMG_TRUE : IMG_FALSE;
	bCpuLocal = PVRSRV_CHECK_CPU_LOCAL(uiFlags) ? IMG_TRUE : IMG_FALSE;
	bFwLocal = PVRSRV_CHECK_FW_LOCAL(uiFlags) ? IMG_TRUE : IMG_FALSE;
	bZero = PVRSRV_CHECK_ZERO_ON_ALLOC(uiFlags) ? IMG_TRUE : IMG_FALSE;
	bPoisonOnAlloc = PVRSRV_CHECK_POISON_ON_ALLOC(uiFlags) ? IMG_TRUE : IMG_FALSE;
	bPoisonOnFree = PVRSRV_CHECK_POISON_ON_FREE(uiFlags) ? IMG_TRUE : IMG_FALSE;

	if (bZero && bPoisonOnAlloc)
	{
		/* Zero on Alloc and Poison on Alloc are mutually exclusive */
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto errorOnParam;
	}

	/* Create Array structure that hold the physical pages */
	eError = _AllocOSPageArray(psDevNode,
							   uiChunkSize,
							   ui32NumPhysChunks,
							   ui32NumVirtChunks,
							   uiLog2PageSize,
							   bZero,
							   bIsCMA,
							   bPoisonOnAlloc,
							   bPoisonOnFree,
							   bOnDemand,
							   ui32CPUCacheFlags,
							   &psPrivData);
	if (eError != PVRSRV_OK)
	{
		goto errorOnAllocPageArray;
	}

	if (!bOnDemand)
	{
		/* Do we fill the whole page array or just parts (sparse)? */
		if (ui32NumPhysChunks == ui32NumVirtChunks)
		{
			/* Allocate the physical pages */
			eError = _AllocOSPages(psPrivData, NULL, psPrivData->uiTotalNumPages);
		}
		else
		{
			if (ui32NumPhysChunks != 0)
			{
				/* Calculate the number of pages we want to allocate */
				IMG_UINT32 uiPagesToAlloc =
					(IMG_UINT32) ((((ui32NumPhysChunks * uiChunkSize) - 1) >> uiLog2PageSize) + 1);

				/* Make sure calculation is correct */
				PVR_ASSERT(((PMR_SIZE_T) uiPagesToAlloc << uiLog2PageSize) ==
						   (ui32NumPhysChunks * uiChunkSize) );

				/* Allocate the physical pages */
				eError = _AllocOSPages(psPrivData, puiAllocIndices,
									   uiPagesToAlloc);
			}
		}

		if (eError != PVRSRV_OK)
		{
			goto errorOnAllocPages;
		}
	}

	/*
	 * In this instance, we simply pass flags straight through.
	 *
	 * Generically, uiFlags can include things that control the PMR factory, but
	 * we don't need any such thing (at the time of writing!), and our caller
	 * specifies all PMR flags so we don't need to meddle with what was given to
	 * us.
	 */
	uiPMRFlags = (PMR_FLAGS_T)(uiFlags & PVRSRV_MEMALLOCFLAGS_PMRFLAGSMASK);

	/*
	 * Check no significant bits were lost in cast due to different bit widths
	 * for flags
	 */
	PVR_ASSERT(uiPMRFlags == (uiFlags & PVRSRV_MEMALLOCFLAGS_PMRFLAGSMASK));

	if (bOnDemand)
	{
		PDUMPCOMMENT("Deferred Allocation PMR (UMA)");
	}

	if (bFwLocal)
	{
		PDUMPCOMMENT("FW_LOCAL allocation requested");
		psPhysHeap = psDevNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL];
	}
	else if (bCpuLocal)
	{
		PDUMPCOMMENT("CPU_LOCAL allocation requested");
		psPhysHeap = psDevNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL];
	}
	else
	{
		psPhysHeap = psDevNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL];
	}

	eError = PMRCreatePMR(psDevNode,
						  psPhysHeap,
						  uiSize,
						  uiChunkSize,
						  ui32NumPhysChunks,
						  ui32NumVirtChunks,
						  puiAllocIndices,
						  uiLog2PageSize,
						  uiPMRFlags,
						  pszAnnotation,
						  &_sPMROSPFuncTab,
						  psPrivData,
						  PMR_TYPE_OSMEM,
						  &psPMR,
						  IMG_FALSE);
	if (eError != PVRSRV_OK)
	{
		goto errorOnCreate;
	}

	*ppsPMRPtr = psPMR;

	return PVRSRV_OK;

errorOnCreate:
	if (!bOnDemand)
	{
		eError2 = _FreeOSPages(psPrivData, NULL, 0);
		PVR_ASSERT(eError2 == PVRSRV_OK);
	}

errorOnAllocPages:
	eError2 = _FreeOSPagesArray(psPrivData);
	PVR_ASSERT(eError2 == PVRSRV_OK);

errorOnAllocPageArray:
errorOnParam:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}
