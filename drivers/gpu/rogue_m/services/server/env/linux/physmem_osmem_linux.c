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

/* include5/ */
#include "img_types.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"

/* services/server/include/ */
#include "allocmem.h"
#include "osfunc.h"
#include "pdump_physmem.h"
#include "pdump_km.h"
#include "pmr.h"
#include "pmr_impl.h"
#include "devicemem_server_utils.h"

/* ourselves */
#include "physmem_osmem.h"

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#include "process_stats.h"
#endif

#include <linux/version.h>

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 0, 0))
#include <linux/mm.h>
#define PHYSMEM_SUPPORTS_SHRINKER
#endif

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
#if defined(CONFIG_ARM) || defined(CONFIG_ARM64) || defined (CONFIG_METAG)
#include "osfunc.h"
#endif

#if defined(CONFIG_X86)
#define PMR_UNSET_PAGES_STACK_ALLOC 64
#endif

/* Provide SHRINK_STOP definition for kernel older than 3.12 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0))
#define SHRINK_STOP (~0UL)
#endif

#include "physmem_osmem_linux.h"

#if  (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
/* split_pages() not available on older-kernels */
#if (PVR_LINUX_PHYSMEM_MAX_ALLOC_ORDER > 0)
/* This includes bin (i.e. bucket) for order-0 */
#define ALLOC_ORDER_ARRAY_SIZE (PVR_LINUX_PHYSMEM_MAX_ALLOC_ORDER+1)
#define PHYSMEM_USING_HIGH_ALLOC_ORDER

static IMG_UINT32 g_uiCutOffOrder = PVR_LINUX_PHYSMEM_MAX_ALLOC_ORDER;
#endif
#endif

/* guard against too big a backlog of deferred freeing of pages */
#define MAX_OUTSTANDING_DEFERRED_FREE_PAGES 32768
static atomic_t g_uiNumDeferredFreePages = ATOMIC_INIT(0);

struct _PMR_OSPAGEARRAY_DATA_ {
    /*
      uiNumPages:

      number of "pages" (a.k.a. macro pages, compound pages, higher
      order pages, etc...)
    */
    IMG_UINT32 uiNumPages;

    /*
      uiLog2PageSize;

      size of each "page" -- this would normally be the same as
      PAGE_SHIFT, but we support the idea that we may allocate pages
      in larger chunks for better contiguity, using order>0 in the
      call to alloc_pages()
    */
    IMG_UINT32 uiLog2PageSize;

    /*
      the pages thusly allocated...  N.B.. One entry per compound page,
      where compound pages are used.
    */
    struct page **pagearray;

    /*
      for pdump...
    */
    IMG_BOOL bPDumpMalloced;
    IMG_HANDLE hPDumpAllocInfo;

    /*
      record at alloc time whether poisoning will be required when the
      PMR is freed.
    */
    IMG_BOOL bZero;
    IMG_BOOL bPoisonOnFree;
    IMG_BOOL bPoisonOnAlloc;
    IMG_BOOL bHasOSPages;
    IMG_BOOL bOnDemand;
    /*
	 The cache mode of the PMR (required at free time)
	 Boolean used to track if we need to revert the cache attributes
	 of the pages used in this allocation. Depends on OS/architecture.
	*/
    IMG_UINT32 ui32CPUCacheFlags;
	IMG_BOOL bUnsetMemoryType;

	/* Structure which is hooked into the cleanup thread work list */
	PVRSRV_CLEANUP_THREAD_WORK sCleanupThreadFn;
};

/***********************************
 * Page pooling for uncached pages *
 ***********************************/
 
static void
_FreeOSPage(IMG_UINT32 ui32CPUCacheFlags,
			IMG_UINT32 uiOrder,
			IMG_BOOL bUnsetMemoryType,
			IMG_BOOL bFreeToOS,
			struct page *psPage);
 
typedef	struct
{
	/* Linkage for page pool LRU list */
	struct list_head sPagePoolItem;

	struct page *psPage;
} LinuxPagePoolEntry;

static IMG_UINT32 g_ui32PagePoolEntryCount = 0;

#if defined(PVR_LINUX_PHYSMEM_MAX_POOL_PAGES)
static IMG_UINT32 g_ui32PagePoolMaxEntries = PVR_LINUX_PHYSMEM_MAX_POOL_PAGES;
#else
static IMG_UINT32 g_ui32PagePoolMaxEntries = 0;
#endif

/* Global structures we use to manage the page pool */
static DEFINE_MUTEX(g_sPagePoolMutex);

static struct kmem_cache *g_psLinuxPagePoolCache = IMG_NULL;

static LIST_HEAD(g_sPagePoolList);
static LIST_HEAD(g_sUncachedPagePoolList);

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

static LinuxPagePoolEntry *
_LinuxPagePoolEntryAlloc(void)
{
    return kmem_cache_zalloc(g_psLinuxPagePoolCache, GFP_KERNEL);
}

static inline IMG_BOOL _GetPoolListHead(IMG_UINT32 ui32CPUCacheFlags, struct list_head **ppsPoolHead)
{
	switch(ui32CPUCacheFlags)
	{
		case PVRSRV_MEMALLOCFLAG_CPU_UNCACHED:
/*
	For x86 we need to keep different lists for uncached
	and write-combined as we must always honour the PAT
	setting which cares about this difference.
*/
#if defined(CONFIG_X86)
			*ppsPoolHead = &g_sUncachedPagePoolList;
			break;
#else
			/* Fall-through */
#endif
		case PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE:
			*ppsPoolHead = &g_sPagePoolList;
			break;
		default:
			return IMG_FALSE;
	}
	return IMG_TRUE;
}

static void
_LinuxPagePoolEntryFree(LinuxPagePoolEntry *psPagePoolEntry)
{
	kmem_cache_free(g_psLinuxPagePoolCache, psPagePoolEntry);
}

static inline IMG_BOOL
_AddEntryToPool(struct page *psPage, IMG_UINT32 ui32CPUCacheFlags)
{
	LinuxPagePoolEntry *psEntry;
	struct list_head *psPoolHead = IMG_NULL;

	if (!_GetPoolListHead(ui32CPUCacheFlags, &psPoolHead))
	{
		return IMG_FALSE;
	}

	psEntry = _LinuxPagePoolEntryAlloc();
	if (psEntry == NULL)
	{
		return IMG_FALSE;
	}

	psEntry->psPage = psPage;
	_PagePoolLock();
	list_add_tail(&psEntry->sPagePoolItem, psPoolHead);
	g_ui32PagePoolEntryCount++;
	_PagePoolUnlock();

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	/* MemStats usually relies on having the bridge lock held, however
	 * the page pool code may call PVRSRVStatsIncrMemAllocPoolStat and
	 * PVRSRVStatsDecrMemAllocPoolStat without the bridge lock held, so
	 * the page pool lock is used to ensure these calls are mutually
	 * exclusive
	 */
	_PagePoolLock();
	PVRSRVStatsIncrMemAllocPoolStat(PAGE_SIZE);
	_PagePoolUnlock();
#endif

	return IMG_TRUE;
}

static inline void
_RemoveEntryFromPoolUnlocked(LinuxPagePoolEntry *psPagePoolEntry)
{
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	/* MemStats usually relies on having the bridge lock held, however
	 * the page pool code may call PVRSRVStatsIncrMemAllocPoolStat and
	 * PVRSRVStatsDecrMemAllocPoolStat without the bridge lock held, so
	 * the page pool lock is used to ensure these calls are mutually
	 * exclusive
	 */
	PVRSRVStatsDecrMemAllocPoolStat(PAGE_SIZE);
#endif

	list_del(&psPagePoolEntry->sPagePoolItem);
	g_ui32PagePoolEntryCount--;
}

static inline struct page *
_RemoveFirstEntryFromPool(IMG_UINT32 ui32CPUCacheFlags)
{
	LinuxPagePoolEntry *psPagePoolEntry;
	struct page *psPage;
	struct list_head *psPoolHead = IMG_NULL;

	if (!_GetPoolListHead(ui32CPUCacheFlags, &psPoolHead))
	{
		return NULL;
	}

	_PagePoolLock();
	if (list_empty(psPoolHead))
	{
		_PagePoolUnlock();
		return NULL;
	}

	PVR_ASSERT(g_ui32PagePoolEntryCount > 0);
	psPagePoolEntry = list_first_entry(psPoolHead, LinuxPagePoolEntry, sPagePoolItem);
	_RemoveEntryFromPoolUnlocked(psPagePoolEntry);

	psPage = psPagePoolEntry->psPage;
	_LinuxPagePoolEntryFree(psPagePoolEntry);
	_PagePoolUnlock();

	return psPage;
}

#if defined(PHYSMEM_SUPPORTS_SHRINKER)
static struct shrinker g_sShrinker;

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
	remain = g_ui32PagePoolEntryCount;
	_PagePoolUnlock();

	return remain;
}

static unsigned long
_ScanObjectsInPagePool(struct shrinker *psShrinker, struct shrink_control *psShrinkControl)
{
	unsigned long uNumToScan = psShrinkControl->nr_to_scan;
	LinuxPagePoolEntry *psPagePoolEntry, *psTempPoolEntry;
	int remain;

	PVR_ASSERT(psShrinker == &g_sShrinker);
	(void)psShrinker;

	/* In order to avoid possible deadlock use mutex_trylock in place of mutex_lock */
	if (_PagePoolTrylock() == 0)
		return SHRINK_STOP;
	list_for_each_entry_safe(psPagePoolEntry,
	                         psTempPoolEntry,
	                         &g_sPagePoolList,
	                         sPagePoolItem)
	{
		_RemoveEntryFromPoolUnlocked(psPagePoolEntry);

		/*
		  We don't want to save the cache type and is we need to unset the
		  memory type as it would double the page pool structure and the
		  values are always going to be the same anyway which is why the
		  page is in the pool (well the page could be UNCACHED or
		  WRITE_COMBINE but we don't even need the cache type for freeing
		  back to the OS).
		*/
		_FreeOSPage(PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE,
			    0,
			    IMG_TRUE,
			    IMG_TRUE,
			    psPagePoolEntry->psPage);
		_LinuxPagePoolEntryFree(psPagePoolEntry);

		if (--uNumToScan == 0)
		{
			break;
		}
	}

	/*
	  Note:
	  For anything other then x86 this list will be empty but we want to
	  keep differences between compiled code to a minimum and so
	  this isn't wrapped in #if defined(CONFIG_X86)
	*/
	list_for_each_entry_safe(psPagePoolEntry,
	                         psTempPoolEntry,
	                         &g_sUncachedPagePoolList,
	                         sPagePoolItem)
	{
		_RemoveEntryFromPoolUnlocked(psPagePoolEntry);

		/*
		  We don't want to save the cache type and is we need to unset the
		  memory type as it would double the page pool structure and the
		  values are always going to be the same anyway which is why the
		  page is in the pool (well the page could be UNCACHED or
		  WRITE_COMBINE but we don't even need the cache type for freeing
		  back to the OS).
		*/
		_FreeOSPage(PVRSRV_MEMALLOCFLAG_CPU_UNCACHED,
			    0,
			    IMG_TRUE,
			    IMG_TRUE,
			    psPagePoolEntry->psPage);
		_LinuxPagePoolEntryFree(psPagePoolEntry);

		if (--uNumToScan == 0)
		{
			break;
		}
	}

	if (list_empty(&g_sPagePoolList) && list_empty(&g_sUncachedPagePoolList))
	{
		PVR_ASSERT(g_ui32PagePoolEntryCount == 0);
	}
	remain = g_ui32PagePoolEntryCount;
	_PagePoolUnlock();

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0))
	return remain;
#else
	return psShrinkControl->nr_to_scan - uNumToScan;
#endif
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0))
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
#endif /* defined(PHYSMEM_SUPPORTS_SHRINKER) */

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

void LinuxInitPagePool(void)
{
	IMG_UINT32 ui32Flags = 0;

	_PagePoolLock();
#if defined(DEBUG_LINUX_SLAB_ALLOCATIONS)
	ui32Flags |= SLAB_POISON|SLAB_RED_ZONE;
#endif
	g_psLinuxPagePoolCache = kmem_cache_create("img-pp", sizeof(LinuxPagePoolEntry), 0, ui32Flags, NULL);

#if defined(PHYSMEM_SUPPORTS_SHRINKER)
	/* Only create the shrinker if we created the cache OK */
	if (g_psLinuxPagePoolCache)
	{
		register_shrinker(&g_sShrinker);
	}
#endif
	_PagePoolUnlock();
}

void LinuxDeinitPagePool(void)
{
	LinuxPagePoolEntry *psPagePoolEntry, *psTempPPEntry;

	_PagePoolLock();
	/* Evict all the pages from the pool */
	list_for_each_entry_safe(psPagePoolEntry,
	                         psTempPPEntry,
	                         &g_sPagePoolList,
	                         sPagePoolItem)
	{
		_RemoveEntryFromPoolUnlocked(psPagePoolEntry);

		/*
			We don't want to save the cache type and is we need to unset the
			memory type as it would double the page pool structure and the
			values are always going to be the same anyway which is why the
			page is in the pool (well the page could be UNCACHED or
			WRITE_COMBINE but we don't even need the cache type for freeing
			back to the OS).
		*/
		_FreeOSPage(PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE,
					0,
					IMG_TRUE,
					IMG_TRUE,
					psPagePoolEntry->psPage);
		_LinuxPagePoolEntryFree(psPagePoolEntry);
	}
	
	/*
		Note:
		For anything other then x86 this will be a no-op but we want to
		keep differences between compiled code to a minimum and so
		this isn't wrapped in #if defined(CONFIG_X86)
	*/
	list_for_each_entry_safe(psPagePoolEntry,
	                         psTempPPEntry,
	                         &g_sUncachedPagePoolList,
	                         sPagePoolItem)
	{
		_RemoveEntryFromPoolUnlocked(psPagePoolEntry);

		_FreeOSPage(PVRSRV_MEMALLOCFLAG_CPU_UNCACHED,
					0,
					IMG_TRUE,
					IMG_TRUE,
					psPagePoolEntry->psPage);
		_LinuxPagePoolEntryFree(psPagePoolEntry);
	}

	PVR_ASSERT(g_ui32PagePoolEntryCount == 0);

	/* Free the page cache */
	kmem_cache_destroy(g_psLinuxPagePoolCache);

#if defined(PHYSMEM_SUPPORTS_SHRINKER)
	unregister_shrinker(&g_sShrinker);
#endif
	_PagePoolUnlock();
}

static void EnableOOMKiller(void)
{
	current->flags &= ~PF_DUMPCORE;
}

static void
_PoisonPages(struct page *page,
             IMG_UINT32 uiOrder,
             const IMG_CHAR *pacPoisonData,
             IMG_SIZE_T uiPoisonSize)
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

static PVRSRV_ERROR
_AllocOSPageArray(PMR_SIZE_T uiSize,
        IMG_UINT32 uiLog2PageSize,
        IMG_BOOL bZero,
        IMG_BOOL bPoisonOnAlloc,
        IMG_BOOL bPoisonOnFree,
        IMG_BOOL bOnDemand,
        IMG_UINT32 ui32CPUCacheFlags,
		struct _PMR_OSPAGEARRAY_DATA_ **ppsPageArrayDataPtr)
{
    PVRSRV_ERROR eError;
    void *pvData;
    IMG_UINT32 uiNumPages;

    struct page **ppsPageArray;
    struct _PMR_OSPAGEARRAY_DATA_ *psPageArrayData;

    if (uiSize >= 0x1000000000ULL)
    {
        PVR_DPF((PVR_DBG_ERROR,
                 "physmem_osmem_linux.c: Do you really want 64GB of physical memory in one go?  This is likely a bug"));
        eError = PVRSRV_ERROR_INVALID_PARAMS;
        goto e_freed_pvdata;
    }

    PVR_ASSERT(PAGE_SHIFT <= uiLog2PageSize);
    if ((uiSize & ((1ULL << uiLog2PageSize) - 1)) != 0)
    {
    	PVR_DPF((PVR_DBG_ERROR,
    			"Allocation size "PMR_SIZE_FMTSPEC" is not multiple of page size 2^%u !",
    			uiSize,
    			uiLog2PageSize));

        eError = PVRSRV_ERROR_PMR_NOT_PAGE_MULTIPLE;
        goto e_freed_pvdata;
    }

    /* Use of cast below is justified by the assertion that follows to
       prove that no significant bits have been truncated */
    uiNumPages = (IMG_UINT32)(((uiSize-1)>>uiLog2PageSize) + 1);
    PVR_ASSERT(((PMR_SIZE_T)uiNumPages << uiLog2PageSize) == uiSize);

    pvData = OSAllocMem(sizeof(struct _PMR_OSPAGEARRAY_DATA_) +
                        sizeof(struct page *) * uiNumPages);
    if (pvData == IMG_NULL)
    {
        PVR_DPF((PVR_DBG_ERROR,
                 "physmem_osmem_linux.c: OS refused the memory allocation for the table of pages.  Did you ask for too much?"));
        eError = PVRSRV_ERROR_INVALID_PARAMS;
        goto e_freed_pvdata;
    }
    PVR_ASSERT(pvData != IMG_NULL);

    psPageArrayData = pvData;
    ppsPageArray = pvData + sizeof(struct _PMR_OSPAGEARRAY_DATA_);
    psPageArrayData->pagearray = ppsPageArray;
    psPageArrayData->uiLog2PageSize = uiLog2PageSize;
    psPageArrayData->uiNumPages = uiNumPages;
    psPageArrayData->bZero = bZero;
    psPageArrayData->ui32CPUCacheFlags = ui32CPUCacheFlags;
 	psPageArrayData->bPoisonOnAlloc = bPoisonOnAlloc;
 	psPageArrayData->bPoisonOnFree = bPoisonOnFree;
 	psPageArrayData->bHasOSPages = IMG_FALSE;
 	psPageArrayData->bOnDemand = bOnDemand;

    psPageArrayData->bPDumpMalloced = IMG_FALSE;

	psPageArrayData->bUnsetMemoryType = IMG_FALSE;

	*ppsPageArrayDataPtr = psPageArrayData;

	return PVRSRV_OK;

e_freed_pvdata:
   PVR_ASSERT(eError != PVRSRV_OK);
   return eError;

}

static inline PVRSRV_ERROR
_ApplyOSPagesAttribute(struct page **ppsPage, IMG_UINT32 uiNumPages, IMG_BOOL bFlush,
#if defined (CONFIG_X86)
					   struct page **ppsUnsetPages, IMG_UINT32 uiUnsetPagesIndex,
#endif
					   IMG_UINT32 ui32CPUCacheFlags)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	struct page **ppsPagesTemp = ppsPage;
	IMG_UINT32 uiPageArrayLength = uiNumPages; 
	
	if (ppsPagesTemp != IMG_NULL)
	{
#if defined (CONFIG_X86)
		
		/* In case our allocation is CPU cached 
		 * we have to flush our x86 specific page array */
		ppsPagesTemp = ppsUnsetPages;
		uiPageArrayLength = uiUnsetPagesIndex;
		
		/* On x86 we have to set page cache attributes for non-cached pages. 
		 * The call is implicitly taking care of all flushing/invalidating
		 * and therefore we can skip the usual cache maintainance after this. */
		if (PVRSRV_CPU_CACHE_MODE(ui32CPUCacheFlags) == PVRSRV_MEMALLOCFLAG_CPU_UNCACHED ||
			PVRSRV_CPU_CACHE_MODE(ui32CPUCacheFlags) == PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE)
		{
			/*  On X86 if we already have a mapping we need to change the mode of
				current mapping before we map it ourselves	*/
			int ret = IMG_FALSE;
			IMG_UINT32 uiPageIndex;
			PVR_UNREFERENCED_PARAMETER(bFlush);
	
			switch (ui32CPUCacheFlags)
			{
				case PVRSRV_MEMALLOCFLAG_CPU_UNCACHED:
					ret = set_pages_array_uc(ppsUnsetPages, uiUnsetPagesIndex);
					if (ret)
					{
						eError = PVRSRV_ERROR_UNABLE_TO_SET_CACHE_MODE;
						PVR_DPF((PVR_DBG_ERROR, "Setting Linux page caching mode to UC failed, returned %d", ret));
					}
					break;
	
				case PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE:
					ret = set_pages_array_wc(ppsUnsetPages, uiUnsetPagesIndex);
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
	
			if (ret)
			{
				for(uiPageIndex = 0; uiPageIndex < uiPageArrayLength; uiPageIndex++)
				{
					_FreeOSPage(ui32CPUCacheFlags,
								0,
								IMG_FALSE,
								IMG_FALSE,
								ppsPagesTemp[uiPageIndex]);
				}
				goto e_exit;
			}
		}
		else
#else
		PVR_UNREFERENCED_PARAMETER(ui32CPUCacheFlags);
#endif
		{
			/*  On ARM kernels we can be given pages which still remain in the cache.
				In order to make sure that the data we write through our mappings
				doesn't get over written by later cache evictions we invalidate the
				pages that get given to us.
		
				Note:
				This still seems to be true if we request cold pages, it's just less
				likely to be in the cache. */
			IMG_UINT32 ui32Idx;
	
			if (uiPageArrayLength < PVR_LINUX_ARM_PAGEALLOC_FLUSH_THRESHOLD)
			{
				for (ui32Idx = 0; ui32Idx < uiPageArrayLength;  ++ui32Idx)
				{
					IMG_CPU_PHYADDR sCPUPhysAddrStart, sCPUPhysAddrEnd;
					IMG_PVOID pvPageVAddr;
	
					pvPageVAddr = kmap(ppsPagesTemp[ui32Idx]);
					sCPUPhysAddrStart.uiAddr = page_to_phys(ppsPagesTemp[ui32Idx]);
					sCPUPhysAddrEnd.uiAddr = sCPUPhysAddrStart.uiAddr + PAGE_SIZE;
	
					/* If we're zeroing, we need to make sure the cleared memory is pushed out
					   of the cache before the cache lines are invalidated */
					if (bFlush)
					{
						OSFlushCPUCacheRangeKM(pvPageVAddr,
											   pvPageVAddr + PAGE_SIZE,
											   sCPUPhysAddrStart,
											   sCPUPhysAddrEnd);
					}
					else
					{
						OSInvalidateCPUCacheRangeKM(pvPageVAddr,
													pvPageVAddr + PAGE_SIZE,
													sCPUPhysAddrStart,
													sCPUPhysAddrEnd);
					}
	
					kunmap(ppsPagesTemp[ui32Idx]);
				}
			}
			else
			{
				OSCPUOperation(PVRSRV_CACHE_OP_FLUSH);
			}
		}
	}

#if defined (CONFIG_X86)
e_exit:	
#endif
	return eError;
}

static PVRSRV_ERROR
_AllocOSPage(IMG_UINT32 ui32CPUCacheFlags,
             unsigned int gfp_flags,
             IMG_UINT32 uiOrder,
             struct page **ppsPage,
             IMG_BOOL *pbPageFromPool)
{
	struct page *psPage = IMG_NULL;
	*pbPageFromPool = IMG_FALSE;

	/* Does the requested page contiguity match the CPU page size? */
	if (uiOrder == 0)
	{
		psPage = _RemoveFirstEntryFromPool(ui32CPUCacheFlags);
		if (psPage != IMG_NULL)
		{
			*pbPageFromPool = IMG_TRUE;
			if (gfp_flags & __GFP_ZERO)
			{
				/* The kernel will zero the page for us when we allocate it,
				   but if it comes from the pool then we must do this 
				   ourselves. */
				IMG_PVOID pvPageVAddr = kmap(psPage);
				memset(pvPageVAddr, 0, PAGE_SIZE);
				kunmap(psPage);
			}
		}
	}

	/*  Did we check the page pool and/or was it a page pool miss,
		either the pool was empty or it was for a cached page so we
		must ask the OS  */
	if (!*pbPageFromPool)
	{
		DisableOOMKiller();
		psPage = alloc_pages(gfp_flags, uiOrder);
		EnableOOMKiller();
	}

	if(IMG_NULL == (*ppsPage = psPage))
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY; 
	}
	
	return PVRSRV_OK;
}

static PVRSRV_ERROR
_AllocOSZeroOrderPages(struct _PMR_OSPAGEARRAY_DATA_ *psPageArrayData,
					   IMG_UINT32 ui32CPUCacheFlags,
					   unsigned int gfp_flags)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 uiPageIndex;
	IMG_BOOL bPageFromPool = IMG_FALSE;	
	struct page **ppsPageArray = psPageArrayData->pagearray;
#if defined(CONFIG_X86)
	/* On x86 we batch applying cache attributes by storing references to all
	   pages that are not from the page pool */
	struct page *apsUnsetPages[PMR_UNSET_PAGES_STACK_ALLOC];
	struct page **ppsUnsetPages = apsUnsetPages;
	IMG_UINT32 uiUnsetPagesIndex = 0;
	
	if (psPageArrayData->uiNumPages > PMR_UNSET_PAGES_STACK_ALLOC)
	{
		ppsUnsetPages = OSAllocMem(sizeof(struct page*) * psPageArrayData->uiNumPages);
		if (ppsUnsetPages == NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed alloc_pages metadata allocation", __FUNCTION__));
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}
	}
#endif

	/* Allocate pages one at a time.  Note that the _device_ memory
	   page size may be different from the _host_ cpu page size - we
	   have a concept of a minimum contiguity requirement, which must
	   be sufficient to meet the requirement of both device and host
	   page size (and possibly other devices or other external
	   constraints).  We are allocating ONE "minimum contiguity unit"
	   (in practice, generally a _device_ page, but not necessarily)
	   at a time, by asking the OS for 2**uiOrder _host_ pages at a
	   time. */
	for (uiPageIndex = 0;
		 uiPageIndex < psPageArrayData->uiNumPages;
		 uiPageIndex++)
	{
		eError = _AllocOSPage(ui32CPUCacheFlags, gfp_flags, 0,
							  &ppsPageArray[uiPageIndex], &bPageFromPool);
#if defined(CONFIG_X86)
		if (!bPageFromPool)
		{
			ppsUnsetPages[uiUnsetPagesIndex] = ppsPageArray[uiPageIndex];
			uiUnsetPagesIndex += 1;
		}
#endif
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: alloc_pages failed to honour request at %d of %d (%s)",
					 __FUNCTION__,
					 uiPageIndex,
					 psPageArrayData->uiNumPages,
					 PVRSRVGetErrorStringKM(eError)));
			for(--uiPageIndex;uiPageIndex < psPageArrayData->uiNumPages;--uiPageIndex)
			{
				_FreeOSPage(ui32CPUCacheFlags,
							0,
							IMG_TRUE,
							IMG_TRUE,
							ppsPageArray[uiPageIndex]);
			}
			eError = PVRSRV_ERROR_PMR_FAILED_TO_ALLOC_PAGES;
			goto e_freed_pages;
		}
    }

	/* Do the cache management as required */
	eError = _ApplyOSPagesAttribute (ppsPageArray,
									 psPageArrayData->uiNumPages,
									 psPageArrayData->bZero,
#if defined(CONFIG_X86)
									 ppsUnsetPages,
									 uiUnsetPagesIndex,
#endif
									 ui32CPUCacheFlags);
	
e_freed_pages:
#if defined(CONFIG_X86)
	if (psPageArrayData->uiNumPages > PMR_UNSET_PAGES_STACK_ALLOC)
	{
		OSFreeMem(ppsUnsetPages);
	}
#endif
	return eError;
}

#if defined(PHYSMEM_USING_HIGH_ALLOC_ORDER)
static PVRSRV_ERROR
_AllocOSHigherOrderPages(struct _PMR_OSPAGEARRAY_DATA_ *psPageArrayData,
						 IMG_UINT32 ui32CPUCacheFlags,
						 unsigned int gfp_flags)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 uiOrder;
	IMG_UINT32 uiPageIndex;	
	IMG_UINT32 uiMsbNumPages;
	IMG_BOOL bPageFromPool = IMG_FALSE;
	IMG_UINT32 uiCutOffOrder = g_uiCutOffOrder;
	IMG_INT32 aiOrderCount[ALLOC_ORDER_ARRAY_SIZE];	
	struct page **ppsPageArray = psPageArrayData->pagearray;
#if defined(CONFIG_X86)
	/* On x86 we batch applying cache attributes by storing references
	   to all pages that are not from the page pool */
	struct page *apsUnsetPages[PMR_UNSET_PAGES_STACK_ALLOC];
	struct page **ppsUnsetPages = apsUnsetPages;
	IMG_UINT32 uiUnsetPagesIndex = 0;
	
	if (psPageArrayData->uiNumPages > PMR_UNSET_PAGES_STACK_ALLOC)
	{
		ppsUnsetPages = OSAllocMem(sizeof(struct page*) * psPageArrayData->uiNumPages);
		if (ppsUnsetPages == NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed alloc_pages metadata allocation", __FUNCTION__));
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}
	}
#endif

	if (uiCutOffOrder)
	{
		/* Disable retry/wait at order > 0 */
		PVR_ASSERT(psPageArrayData->uiNumPages > 1);
		gfp_flags |= __GFP_NORETRY;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
		gfp_flags &= ~__GFP_WAIT;
#else
		gfp_flags &= ~__GFP_RECLAIM;
#endif
	}

	/* Re-express uiNumPages in multi-order up to cut-off order */
	for (uiOrder = 0; uiOrder <= uiCutOffOrder; ++uiOrder)
	{
		aiOrderCount[uiOrder] = psPageArrayData->uiNumPages & (1<<uiOrder) ? 1 : 0;
	}

	/* Accumulate top order bits into cut-off order bin */
	uiMsbNumPages =  psPageArrayData->uiNumPages >> (g_uiCutOffOrder+1);
	aiOrderCount[uiCutOffOrder] += uiMsbNumPages ? uiMsbNumPages << 1 : 0;

	/* Allocate variable higher-order pages as per order-array specification.
	   There's currently no support for compound pages, the "minimum contiguity unit" 
	   that is supported is equal to the _host_ cpu page size (i.e. PAGE_SHIFT) */
	for (uiOrder=uiCutOffOrder, uiPageIndex=0; uiPageIndex < psPageArrayData->uiNumPages; )
	{
		/* Has this order bucket been exhausted */
		for  ( ; ! aiOrderCount[uiOrder]; --uiOrder)
			;

		/* Alloc uiOrder pages at uiPageIndex */
		eError = _AllocOSPage(ui32CPUCacheFlags, gfp_flags, uiOrder,
							  &ppsPageArray[uiPageIndex], &bPageFromPool);
		if (eError == PVRSRV_OK)
		{
			IMG_UINT32 uiIdx;

			if (uiOrder) 
			{
				split_page(ppsPageArray[uiPageIndex], uiOrder); 
			}

			for (uiIdx=0; uiIdx < (1 << uiOrder); ++uiIdx)
			{
				/* For higher order allocations, we need to return not just the 1st
				   pointer but all pages in the order */
				ppsPageArray[uiPageIndex+uiIdx] = &ppsPageArray[uiPageIndex][uiIdx];
#if defined(CONFIG_X86)
				if (!bPageFromPool)
				{
					ppsUnsetPages[uiUnsetPagesIndex+uiIdx] = ppsPageArray[uiPageIndex+uiIdx];
				}
			}

			if (!bPageFromPool)
			{
				uiUnsetPagesIndex += (1 << uiOrder);
			}
#else
			}
#endif
			/* Less one order allocation */
			uiPageIndex += (1 << uiOrder);
			aiOrderCount[uiOrder] -= 1;
		}
		else
		{
			if (uiOrder)
			{
				/*
				  The strategy employed to cope with memory fragmentation is two fold:
				   - By speculating that it is _better_ to move any remaining failed order
				     allocations to a much lower order (i.e. failed-order/2) _most_ likely
				     to succeed (quick-failure-recovery).

				   - By ensuring that other allocations benefit from this knowledge (failure-rate)
				     by adjusting the global cut-off order variable used for such future 
				     allocations (throttling up/down).
				 */
				IMG_INT32 uiFailedOrder = uiOrder;
				IMG_INT32 uiLowOrder = uiFailedOrder >> 1;
				g_uiCutOffOrder = uiFailedOrder - 1;

				if (! uiLowOrder)
				{
					/* Enable retry/wait at order-0 */
					gfp_flags &= ~__GFP_NORETRY;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
					gfp_flags |= __GFP_WAIT;
#else
					gfp_flags |= __GFP_RECLAIM;
#endif
				}

				/* Accumulate remaining failed order into lower order */
				for  ( ; aiOrderCount[uiFailedOrder]; --aiOrderCount[uiFailedOrder])
				{
					aiOrderCount[uiLowOrder] += 1 << (uiFailedOrder - uiLowOrder);
				}
			}
			else
			{
				/* At order zero, there's nothing else left to do, so we must
					unwind the series of order allocations hitherto */
				PVR_DPF((PVR_DBG_ERROR,
					 "physmem_osmem_linux.c: alloc_pages failed to honour request at %d (order: %d) of %d (%s)",
					 uiPageIndex, uiOrder,
					 psPageArrayData->uiNumPages,
					 PVRSRVGetErrorStringKM(eError)));
				
				for(--uiPageIndex;uiPageIndex < psPageArrayData->uiNumPages;--uiPageIndex)
				{
					_FreeOSPage(ui32CPUCacheFlags,
								0,
								IMG_TRUE,
								IMG_TRUE,
								ppsPageArray[uiPageIndex]);
				}
				
				eError = PVRSRV_ERROR_PMR_FAILED_TO_ALLOC_PAGES;
				goto e_freed_pages;
			}
		}
	}

	/* Do the cache management as required */
	eError = _ApplyOSPagesAttribute (ppsPageArray,
									 psPageArrayData->uiNumPages,
									 psPageArrayData->bZero,
#if defined(CONFIG_X86)
									 ppsUnsetPages,
									 uiUnsetPagesIndex,
#endif
									 ui32CPUCacheFlags);

e_freed_pages:
#if defined(CONFIG_X86)
	if (psPageArrayData->uiNumPages > PMR_UNSET_PAGES_STACK_ALLOC)
	{
		OSFreeMem(ppsUnsetPages);
	}
#endif
	return eError;
}
#endif

static PVRSRV_ERROR
_AllocOSPages(struct _PMR_OSPAGEARRAY_DATA_ **ppsPageArrayDataPtr)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 uiOrder;
	IMG_UINT32 uiPageIndex;
	IMG_UINT32 ui32CPUCacheFlags;
#if defined(PHYSMEM_USING_HIGH_ALLOC_ORDER)
	IMG_UINT32 uiUseHighOrderFlag;
#endif
	struct _PMR_OSPAGEARRAY_DATA_ *psPageArrayData = *ppsPageArrayDataPtr;
	struct page **ppsPageArray = psPageArrayData->pagearray;
	unsigned int gfp_flags;

	PVR_ASSERT(!psPageArrayData->bHasOSPages);

	/* For now we don't support compound pages */
	uiOrder = psPageArrayData->uiLog2PageSize - PAGE_SHIFT;
	ui32CPUCacheFlags = psPageArrayData->ui32CPUCacheFlags;
	PVR_ASSERT(uiOrder == 0);

	gfp_flags = GFP_USER | __GFP_NOWARN | __GFP_NOMEMALLOC;

#if !defined(CONFIG_X86_64)
	gfp_flags |= __GFP_HIGHMEM;
#endif

	if (psPageArrayData->bZero)
	{
		gfp_flags |= __GFP_ZERO;
	}

	/*
		Unset memory type is set to true as although in the "normal" case
		(where we free the page back to the pool) we don't want to unset
		it, we _must_ unset it in the case where the page pool was full
		and thus we have to give the page back to the OS.
	*/
	if (ui32CPUCacheFlags == PVRSRV_MEMALLOCFLAG_CPU_UNCACHED
	    ||ui32CPUCacheFlags == PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE)
	{
		psPageArrayData->bUnsetMemoryType = IMG_TRUE;
	}
	else
	{
		psPageArrayData->bUnsetMemoryType = IMG_FALSE;
	}

#if defined(PHYSMEM_USING_HIGH_ALLOC_ORDER)
	/* Multi-order allocations should be considered when size request >= threshold */
	uiUseHighOrderFlag = psPageArrayData->uiNumPages >= PVR_LINUX_PHYSMEM_MIN_NUM_PAGES;
	if (uiUseHighOrderFlag && g_uiCutOffOrder)
	{
		/* We neeed to throttle up slowly towards the maximum allowed order */
		eError = _AllocOSHigherOrderPages(psPageArrayData, ui32CPUCacheFlags, gfp_flags);
		g_uiCutOffOrder += (g_uiCutOffOrder < PVR_LINUX_PHYSMEM_MAX_ALLOC_ORDER) ? 1:0;
	}
	else 
	{
		/* When to re-enable high-order is a trade-off (accuracy vs. simplicity) */
		eError = _AllocOSZeroOrderPages(psPageArrayData, ui32CPUCacheFlags, gfp_flags);
		g_uiCutOffOrder += uiUseHighOrderFlag;
	}
#else
	eError = _AllocOSZeroOrderPages(psPageArrayData, ui32CPUCacheFlags, gfp_flags);
#endif

	if (eError == PVRSRV_OK)
	{
		for (uiPageIndex = 0; uiPageIndex < psPageArrayData->uiNumPages; uiPageIndex++)
		{
			/* Can't ask us to zero it and poison it */
			PVR_ASSERT(!psPageArrayData->bZero || !psPageArrayData->bPoisonOnAlloc);

			if (psPageArrayData->bPoisonOnAlloc)
			{
				_PoisonPages(ppsPageArray[uiPageIndex],
							 uiOrder,
							 _AllocPoison,
							 _AllocPoisonSize);
			}
			
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
			/* Allocation is done a page at a time */
			PVRSRVStatsIncrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES, PAGE_SIZE);
#else
			{
				IMG_CPU_PHYADDR sCPUPhysAddr;
	
				sCPUPhysAddr.uiAddr = page_to_phys(ppsPageArray[uiPageIndex]);
				PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES,
											 IMG_NULL,
											 sCPUPhysAddr,
											 PAGE_SIZE,
											 IMG_NULL);
			}
#endif
#endif
		}

		
		/* OS Pages have been allocated */
		psPageArrayData->bHasOSPages = IMG_TRUE;

		PVR_DPF((PVR_DBG_MESSAGE, "physmem_osmem_linux.c: allocated OS memory for PMR @0x%p", psPageArrayData));
	}
	return eError;
}


/*
	Note:
	We must _only_ check bUnsetMemoryType in the case where we need to free
	the page back to the OS since we may have to revert the cache properties
	of the page to the default as given by the OS when it was allocated.
*/
static void
_FreeOSPage(IMG_UINT32 ui32CPUCacheFlags,
            IMG_UINT32 uiOrder,
            IMG_BOOL bUnsetMemoryType,
            IMG_BOOL bFreeToOS,
            struct page *psPage)
{
	IMG_BOOL bAddedToPool = IMG_FALSE;
#if defined (CONFIG_X86)
	IMG_PVOID pvPageVAddr;
#else
	PVR_UNREFERENCED_PARAMETER(bUnsetMemoryType);
#endif

	/* Only zero order pages can be managed in the pool */
	if ((uiOrder == 0) && (!bFreeToOS))
	{
		_PagePoolLock();
		bAddedToPool = g_ui32PagePoolEntryCount < g_ui32PagePoolMaxEntries;
		_PagePoolUnlock();

		if (bAddedToPool)
		{
			if (!_AddEntryToPool(psPage, ui32CPUCacheFlags))
			{
				bAddedToPool = IMG_FALSE;
			}
		}
	}

	if (!bAddedToPool)
	{
#if defined(CONFIG_X86)
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
#endif
		__free_pages(psPage, uiOrder);
	}
}

static PVRSRV_ERROR
_FreeOSPagesArray(struct _PMR_OSPAGEARRAY_DATA_ *psPageArrayData)
{
	PVR_DPF((PVR_DBG_MESSAGE, "physmem_osmem_linux.c: freed OS memory for PMR @0x%p", psPageArrayData));

	OSFreeMem(psPageArrayData);

	return PVRSRV_OK;
}

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
/* _FreeOSPages_MemStats: Depends on the bridge lock already being held */
static void
_FreeOSPages_MemStats(struct _PMR_OSPAGEARRAY_DATA_ *psPageArrayData)
{
	struct page **ppsPageArray;
	#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	IMG_UINT32 ui32PageIndex;
	#endif

	PVR_ASSERT(psPageArrayData->bHasOSPages);

	ppsPageArray = psPageArrayData->pagearray;

	#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
		PVRSRVStatsDecrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES, psPageArrayData->uiNumPages * PAGE_SIZE);
	#else
		for(ui32PageIndex = 0; ui32PageIndex < psPageArrayData->uiNumPages; ui32PageIndex++)
		{
			IMG_CPU_PHYADDR sCPUPhysAddr;

			sCPUPhysAddr.uiAddr = page_to_phys(ppsPageArray[ui32PageIndex]);
			PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES, sCPUPhysAddr.uiAddr);
		}
	#endif
	#endif
}
#endif /* PVRSRV_ENABLE_PROCESS_STATS */

static PVRSRV_ERROR
_FreeOSPages_FreePages(struct _PMR_OSPAGEARRAY_DATA_ *psPageArrayData)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 uiOrder;
	IMG_UINT32 uiPageIndex;
	struct page **ppsPageArray;
	IMG_BOOL bAddedToPool = IMG_FALSE;

#if defined (CONFIG_X86)
	struct page *apsUnsetPages[PMR_UNSET_PAGES_STACK_ALLOC];
	struct page **ppsUnsetPages = apsUnsetPages;
	IMG_UINT32 uiUnsetPagesIndex = 0;

	if (psPageArrayData->uiNumPages > PMR_UNSET_PAGES_STACK_ALLOC)
	{
		/* OSAllocMemstatMem required because this code may be run without the bridge lock held */
		ppsUnsetPages = OSAllocMemstatMem(sizeof(struct page*) * psPageArrayData->uiNumPages);
		if (ppsUnsetPages == NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed free_pages metadata allocation", __FUNCTION__));
			return PVRSRV_ERROR_OUT_OF_MEMORY;
			goto e_exit;
		}
	}
#endif

	PVR_ASSERT(psPageArrayData->bHasOSPages);

	ppsPageArray = psPageArrayData->pagearray;

	 /* For now we don't support compound pages */
	uiOrder = psPageArrayData->uiLog2PageSize - PAGE_SHIFT;
	PVR_ASSERT(uiOrder == 0);

	for (uiPageIndex = 0;
	     uiPageIndex < psPageArrayData->uiNumPages;
	     uiPageIndex++)
	{
		if (psPageArrayData->bPoisonOnFree)
		{
			_PoisonPages(ppsPageArray[uiPageIndex],
			             uiOrder,
			             _FreePoison,
			             _FreePoisonSize);
		}

		/* Only zero order pages can be managed in the pool */
		if (uiOrder == 0)
		{
			_PagePoolLock();
			bAddedToPool = g_ui32PagePoolEntryCount < g_ui32PagePoolMaxEntries;
			_PagePoolUnlock();

			if (bAddedToPool)
			{
				if (!_AddEntryToPool(ppsPageArray[uiPageIndex], psPageArrayData->ui32CPUCacheFlags))
				{
					bAddedToPool = IMG_FALSE;
				}
			}
		}

		if (!bAddedToPool)
		{
#if defined(CONFIG_X86)
			if (psPageArrayData->bUnsetMemoryType == IMG_TRUE)
			{
				/* Keeping track of the pages for which the caching needs to change */
				ppsUnsetPages[uiUnsetPagesIndex] = ppsPageArray[uiPageIndex];
				uiUnsetPagesIndex++;
			}
			else
#endif
			{
				_FreeOSPage(0,
				            uiOrder,
				            IMG_FALSE,
				            IMG_TRUE,
				            ppsPageArray[uiPageIndex]);
			}
		}
	}

#if defined(CONFIG_X86)
	if (uiUnsetPagesIndex != 0)
	{
		int ret;
		ret = set_pages_array_wb(ppsUnsetPages, uiUnsetPagesIndex);

		if (ret)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to reset page attributes", __FUNCTION__));
		}

		for (uiPageIndex = 0;
		     uiPageIndex < uiUnsetPagesIndex;
		     uiPageIndex++)
		{
			_FreeOSPage(0,
			            uiOrder,
			            IMG_FALSE,
			            IMG_TRUE,
			            ppsUnsetPages[uiPageIndex]);
		}
	}

	if (psPageArrayData->uiNumPages > PMR_UNSET_PAGES_STACK_ALLOC)
	{
		OSFreeMemstatMem(ppsUnsetPages);
	}
#endif

	eError = PVRSRV_OK;

	psPageArrayData->bHasOSPages = IMG_FALSE;

#if defined(CONFIG_X86)
e_exit:
#endif
	return eError;
}

static PVRSRV_ERROR
_CleanupThread_FreePagesAndPageArrayData(void *pvData)
{
	struct _PMR_OSPAGEARRAY_DATA_ *psPageArrayData = pvData;
	PVRSRV_ERROR eError;

	atomic_sub((int) psPageArrayData->uiNumPages, &g_uiNumDeferredFreePages);

	eError = _FreeOSPages_FreePages(psPageArrayData);

	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "_FreeOSPages_FreePages failed"));
		goto err_out;
	}

	eError = _FreeOSPagesArray(psPageArrayData);

err_out:
	return eError;
}

/* clone a PMR_OSPAGEARRAY_DATA structure, including making a copy of
 * the the list of physical pages it owns.
 * returns a pointer to the newly allocated PMR_OSPAGEARRAY_DATA structure.
 */
static struct _PMR_OSPAGEARRAY_DATA_ *_CloneOSPageArrayData(struct _PMR_OSPAGEARRAY_DATA_ *psPageArrayDataIn)
{
	struct _PMR_OSPAGEARRAY_DATA_ *psPageArrayDataOut;
	size_t uiStructureSize;

	uiStructureSize = sizeof(struct _PMR_OSPAGEARRAY_DATA_) +
				sizeof(struct page *) * psPageArrayDataIn->uiNumPages;

	psPageArrayDataOut = OSAllocMemstatMem(uiStructureSize);

	if(psPageArrayDataOut == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "_CloneOSPageArrayData: Failed to clone PMR_OSPAGEARRAY_DATA"));
		return NULL;
	}

	memcpy(psPageArrayDataOut, psPageArrayDataIn, uiStructureSize);

	psPageArrayDataOut->pagearray = (void *) ((char *) psPageArrayDataOut) +
	sizeof(struct _PMR_OSPAGEARRAY_DATA_);

	return psPageArrayDataOut;
}

static PVRSRV_ERROR
_FreeOSPages(struct _PMR_OSPAGEARRAY_DATA_ *psPageArrayData, IMG_BOOL bFreePageArray)
{
	PVRSRV_ERROR eError;

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
 	_FreeOSPages_MemStats(psPageArrayData);
#endif

	if((atomic_read(&g_uiNumDeferredFreePages) + psPageArrayData->uiNumPages) <= MAX_OUTSTANDING_DEFERRED_FREE_PAGES)
	{
		struct _PMR_OSPAGEARRAY_DATA_ *psPageArrayToFree;

		if(bFreePageArray)
		{
			/* the PMR_OSPAGEARRAY_DATA structure is also being
			 * freed, so the whole structure as-is can be pushed to
			 * the cleanup thread, which can free the PMR_OSPAGEARRAY_DATA
			 * structure as well as the phys pages
			 */
			psPageArrayToFree = psPageArrayData;
		}
		else
		{
			/* the caller does not want the PMR_OSPAGEARRAY_DATA
			 * structure to be freed, only the phys pages.
			 * to acommodate this, the PMR_OSPAGEARRAY_DATA structure
			 * is cloned and ownership of the pages is moved to the
			 * clone, then the clone is pushed onto the cleanup thread
			 * work list. Now psPageArrayData owns no pages, as the caller
			 * expects.
			 */

			 struct _PMR_OSPAGEARRAY_DATA_ *psClone;

			 psClone = _CloneOSPageArrayData(psPageArrayData);

			 if(psClone == NULL)
			 {
			 	/* cloning failed so go ahead and free the
				 * pages immediately
				 */
				 goto nodefer;
			 }

			 /* the clone takes ownership of the pages, so mark the
			  * original structure as not having any pages
			  */
			  psPageArrayData->bHasOSPages = IMG_FALSE;

			  psPageArrayToFree = psClone;
		}

		atomic_add((int) psPageArrayData->uiNumPages, &g_uiNumDeferredFreePages);
		psPageArrayToFree->sCleanupThreadFn.pfnFree = _CleanupThread_FreePagesAndPageArrayData;
		psPageArrayToFree->sCleanupThreadFn.pvData = psPageArrayToFree;
		psPageArrayToFree->sCleanupThreadFn.ui32RetryCount = CLEANUP_THREAD_RETRY_COUNT_DEFAULT;
		PVRSRVCleanupThreadAddWork(&psPageArrayToFree->sCleanupThreadFn);

		return PVRSRV_OK;
	}
nodefer:
	/* we are not deferring, so free the pages immediately */

 	eError = _FreeOSPages_FreePages(psPageArrayData);

	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "_FreeOSPages_FreePages failed"));
		goto err_out;
	}

	if(bFreePageArray)
	{
		eError = _FreeOSPagesArray(psPageArrayData);
	}

err_out:
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
PMRFinalizeOSMem(PMR_IMPL_PRIVDATA pvPriv
                 //struct _PMR_OSPAGEARRAY_DATA_ *psOSPageArrayData
                 )
{
    PVRSRV_ERROR eError;
    struct _PMR_OSPAGEARRAY_DATA_ *psOSPageArrayData;

    psOSPageArrayData = pvPriv;

    /* Conditionally do the PDump free, because if CreatePMR failed we
       won't have done the PDump MALLOC.  */
    if (psOSPageArrayData->bPDumpMalloced)
    {
        PDumpPMRFree(psOSPageArrayData->hPDumpAllocInfo);
    }

	/*  We can't free pages until now. */
	if (psOSPageArrayData->bHasOSPages)
	{
		eError = _FreeOSPages(psOSPageArrayData, IMG_TRUE);
		PVR_ASSERT (eError == PVRSRV_OK); /* can we do better? */
	}
	else
	{
	    eError = _FreeOSPagesArray(psOSPageArrayData);
	    PVR_ASSERT (eError == PVRSRV_OK); /* can we do better? */
	}

    return PVRSRV_OK;
}

/* callback function for locking the system physical page addresses.
   This function must be called before the lookup address func. */
static PVRSRV_ERROR
PMRLockSysPhysAddressesOSMem(PMR_IMPL_PRIVDATA pvPriv,
                             // struct _PMR_OSPAGEARRAY_DATA_ *psOSPageArrayData,
                             IMG_UINT32 uiLog2DevPageSize)
{
    PVRSRV_ERROR eError;
    struct _PMR_OSPAGEARRAY_DATA_ *psOSPageArrayData;

    psOSPageArrayData = pvPriv;

    if (psOSPageArrayData->bOnDemand)
    {
		/* Allocate Memory for deferred allocation */
    	eError = _AllocOSPages(&psOSPageArrayData);
    	if (eError != PVRSRV_OK)
    	{
    		return eError;
    	}
    }

    /* Physical page addresses are already locked down in this
       implementation, so there is no need to acquire physical
       addresses.  We do need to verify that the physical contiguity
       requested by the caller (i.e. page size of the device they
       intend to map this memory into) is compatible with (i.e. not of
       coarser granularity than) our already known physicial
       contiguity of the pages */
    if (uiLog2DevPageSize > psOSPageArrayData->uiLog2PageSize)
    {
        /* or NOT_MAPPABLE_TO_THIS_PAGE_SIZE ? */
        eError = PVRSRV_ERROR_PMR_INCOMPATIBLE_CONTIGUITY;
        return eError;
    }

    eError = PVRSRV_OK;
    return eError;

}

static PVRSRV_ERROR
PMRUnlockSysPhysAddressesOSMem(PMR_IMPL_PRIVDATA pvPriv
                               //struct _PMR_OSPAGEARRAY_DATA_ *psOSPageArrayData
                               )
{
    /* Just drops the refcount. */

    PVRSRV_ERROR eError = PVRSRV_OK;
    struct _PMR_OSPAGEARRAY_DATA_ *psOSPageArrayData;

    psOSPageArrayData = pvPriv;
    if (psOSPageArrayData->bOnDemand)
    {
		/* Free Memory for deferred allocation */
    	eError = _FreeOSPages(psOSPageArrayData, IMG_FALSE);
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
                    IMG_UINT32 ui32NumOfPages,
                    IMG_DEVMEM_OFFSET_T *puiOffset,
					IMG_BOOL *pbValid,
                    IMG_DEV_PHYADDR *psDevPAddr)
{
    const struct _PMR_OSPAGEARRAY_DATA_ *psOSPageArrayData = pvPriv;
    struct page **ppsPageArray = psOSPageArrayData->pagearray;
   	IMG_UINT32 uiPageSize = 1U << psOSPageArrayData->uiLog2PageSize;
	IMG_UINT32 uiInPageOffset;
    IMG_UINT32 uiPageIndex;
    IMG_UINT32 idx;

    for (idx=0; idx < ui32NumOfPages; idx++)
    {
		if (pbValid[idx])
		{	
			uiPageIndex = puiOffset[idx] >> psOSPageArrayData->uiLog2PageSize;
	    	uiInPageOffset = puiOffset[idx] - ((IMG_DEVMEM_OFFSET_T)uiPageIndex << psOSPageArrayData->uiLog2PageSize);

			PVR_ASSERT(uiPageIndex < psOSPageArrayData->uiNumPages);
			PVR_ASSERT(uiInPageOffset < uiPageSize);

			psDevPAddr[idx].uiAddr = page_to_phys(ppsPageArray[uiPageIndex]) + uiInPageOffset;
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
                                 IMG_SIZE_T uiOffset,
                                 IMG_SIZE_T uiSize,
                                 void **ppvKernelAddressOut,
                                 IMG_HANDLE *phHandleOut,
                                 PMR_FLAGS_T ulFlags)
{
    PVRSRV_ERROR eError;
    struct _PMR_OSPAGEARRAY_DATA_ *psOSPageArrayData;
    void *pvAddress;
    pgprot_t prot = PAGE_KERNEL;
    IMG_UINT32 ui32CPUCacheFlags;
    IMG_UINT32 ui32PageOffset;
    IMG_SIZE_T uiMapOffset;
    IMG_UINT32 ui32PageCount;
    PMR_OSPAGEARRAY_KERNMAP_DATA *psData;

    psOSPageArrayData = pvPriv;
	ui32CPUCacheFlags = DevmemCPUCacheMode(ulFlags);

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
		ui32PageCount = psOSPageArrayData->uiNumPages;
	}
	else
	{
		IMG_SIZE_T uiEndoffset;

		ui32PageOffset = uiOffset >> psOSPageArrayData->uiLog2PageSize;
		uiMapOffset = uiOffset - (ui32PageOffset << psOSPageArrayData->uiLog2PageSize);
		uiEndoffset = uiOffset + uiSize - 1;
		// Add one as we want the count, not the offset
		ui32PageCount = (uiEndoffset >> psOSPageArrayData->uiLog2PageSize) + 1;
		ui32PageCount -= ui32PageOffset;
	}

    if (psOSPageArrayData->uiLog2PageSize != PAGE_SHIFT)
    {
        /* we only know how to use vmap on allocations comprising
           individual pages.  Higher-order "pages" are not supported
           with this. */
        eError = PVRSRV_ERROR_PMR_INCOMPATIBLE_CONTIGUITY;
        goto e0;
    }

#if defined(CONFIG_ARM64)
    //zxl:ARM64 use noncached,which is setted MT_DEVICE,And Device type may unsupport some operations.
    //    Force to use pgprot_writecombine now.
    if(ui32CPUCacheFlags == PVRSRV_MEMALLOCFLAG_CPU_UNCACHED)
    {
        ui32CPUCacheFlags = PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE;
    }
#endif

	switch (ui32CPUCacheFlags)
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
	
	psData = OSAllocMem(sizeof(PMR_OSPAGEARRAY_KERNMAP_DATA));
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
	if (pvAddress == IMG_NULL)
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
    struct _PMR_OSPAGEARRAY_DATA_ *psOSPageArrayData;
    PMR_OSPAGEARRAY_KERNMAP_DATA *psData;

    psOSPageArrayData = pvPriv;
    psData = hHandle;
#if !defined(CONFIG_64BIT) || defined(PVRSRV_FORCE_SLOWER_VMAP_ON_64BIT_BUILDS)
    vunmap(psData->pvBase);
#else
    vm_unmap_ram(psData->pvBase, psData->ui32PageCount);
#endif
    OSFreeMem(psData);
}

static PMR_IMPL_FUNCTAB _sPMROSPFuncTab = {
    .pfnLockPhysAddresses = &PMRLockSysPhysAddressesOSMem,
    .pfnUnlockPhysAddresses = &PMRUnlockSysPhysAddressesOSMem,
    .pfnDevPhysAddr = &PMRSysPhysAddrOSMem,
    .pfnAcquireKernelMappingData = &PMRAcquireKernelMappingDataOSMem,
    .pfnReleaseKernelMappingData = &PMRReleaseKernelMappingDataOSMem,
    .pfnReadBytes = IMG_NULL,
    .pfnWriteBytes = IMG_NULL,
    .pfnFinalize = &PMRFinalizeOSMem
};

static PVRSRV_ERROR
_NewOSAllocPagesPMR(PVRSRV_DEVICE_NODE *psDevNode,
                    IMG_DEVMEM_SIZE_T uiSize,
					IMG_DEVMEM_SIZE_T uiChunkSize,
					IMG_UINT32 ui32NumPhysChunks,
					IMG_UINT32 ui32NumVirtChunks,
					IMG_BOOL *pabMappingTable,
                    IMG_UINT32 uiLog2PageSize,
                    PVRSRV_MEMALLOCFLAGS_T uiFlags,
                    PMR **ppsPMRPtr)
{
    PVRSRV_ERROR eError;
    PVRSRV_ERROR eError2;
    PMR *psPMR;
    struct _PMR_OSPAGEARRAY_DATA_ *psPrivData;
    IMG_HANDLE hPDumpAllocInfo = IMG_NULL;
    PMR_FLAGS_T uiPMRFlags;
    IMG_BOOL bZero;
    IMG_BOOL bPoisonOnAlloc;
    IMG_BOOL bPoisonOnFree;
    IMG_BOOL bOnDemand = ((uiFlags & PVRSRV_MEMALLOCFLAG_NO_OSPAGES_ON_ALLOC) > 0);
	IMG_BOOL bCpuLocal = ((uiFlags & PVRSRV_MEMALLOCFLAG_CPU_LOCAL) > 0);
	IMG_UINT32 ui32CPUCacheFlags = (IMG_UINT32) DevmemCPUCacheMode(uiFlags);


    if (uiFlags & PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC)
    {
        bZero = IMG_TRUE;
    }
    else
    {
        bZero = IMG_FALSE;
    }

    if (uiFlags & PVRSRV_MEMALLOCFLAG_POISON_ON_ALLOC)
    {
        bPoisonOnAlloc = IMG_TRUE;
    }
    else
    {
        bPoisonOnAlloc = IMG_FALSE;
    }

    if (uiFlags & PVRSRV_MEMALLOCFLAG_POISON_ON_FREE)
    {
        bPoisonOnFree = IMG_TRUE;
    }
    else
    {
        bPoisonOnFree = IMG_FALSE;
    }

    if ((uiFlags & PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC) &&
        (uiFlags & PVRSRV_MEMALLOCFLAG_POISON_ON_ALLOC))
    {
        /* Zero on Alloc and Poison on Alloc are mutually exclusive */
        eError = PVRSRV_ERROR_INVALID_PARAMS;
        goto errorOnParam;
    }

	/* Silently round up alignment/pagesize if request was less that
	   PAGE_SHIFT, because it would never be harmful for memory to be
	   _more_ contiguous that was desired */
	uiLog2PageSize = PAGE_SHIFT > uiLog2PageSize
		? PAGE_SHIFT
		: uiLog2PageSize;

	/* Create Array structure that hold the physical pages */
	eError = _AllocOSPageArray(uiChunkSize * ui32NumPhysChunks,
						   uiLog2PageSize,
						   bZero,
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
		/* Allocate the physical pages */
		eError = _AllocOSPages(&psPrivData);
		if (eError != PVRSRV_OK)
		{
			goto errorOnAllocPages;
		}
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
    	PDUMPCOMMENT("Deferred Allocation PMR (UMA)");
    }
    if (bCpuLocal)
    {
    	PDUMPCOMMENT("CPU_LOCAL allocation requested");
    }
    eError = PMRCreatePMR(psDevNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL],
                          uiSize,
                          uiChunkSize,
                          ui32NumPhysChunks,
                          ui32NumVirtChunks,
                          pabMappingTable,
                          uiLog2PageSize,
                          uiPMRFlags,
                          "PMROSAP",
                          &_sPMROSPFuncTab,
                          psPrivData,
                          &psPMR,
                          &hPDumpAllocInfo,
    					  IMG_FALSE);
    if (eError != PVRSRV_OK)
    {
        goto errorOnCreate;
    }

	psPrivData->hPDumpAllocInfo = hPDumpAllocInfo;
	psPrivData->bPDumpMalloced = IMG_TRUE;

    *ppsPMRPtr = psPMR;
    return PVRSRV_OK;

errorOnCreate:
	if (!bOnDemand)
	{
		eError2 = _FreeOSPages(psPrivData, IMG_FALSE);
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

PVRSRV_ERROR
PhysmemNewOSRamBackedPMR(PVRSRV_DEVICE_NODE *psDevNode,
                         IMG_DEVMEM_SIZE_T uiSize,
						 IMG_DEVMEM_SIZE_T uiChunkSize,
						 IMG_UINT32 ui32NumPhysChunks,
						 IMG_UINT32 ui32NumVirtChunks,
						 IMG_BOOL *pabMappingTable,
                         IMG_UINT32 uiLog2PageSize,
                         PVRSRV_MEMALLOCFLAGS_T uiFlags,
                         PMR **ppsPMRPtr)
{
    return _NewOSAllocPagesPMR(psDevNode,
                               uiSize,
                               uiChunkSize,
                               ui32NumPhysChunks,
                               ui32NumVirtChunks,
                               pabMappingTable,
                               uiLog2PageSize,
                               uiFlags,
                               ppsPMRPtr);
}
