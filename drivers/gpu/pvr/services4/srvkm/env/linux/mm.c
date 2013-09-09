/*************************************************************************/ /*!
@Title          Misc memory management utility functions for Linux
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#endif

#if !defined(PVR_LINUX_MEM_AREA_POOL_MAX_PAGES)
#define PVR_LINUX_MEM_AREA_POOL_MAX_PAGES 0
#endif

#include <linux/kernel.h>
#include <asm/atomic.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <asm/io.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0))
#include <linux/wrapper.h>
#endif
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/sched.h>

#if defined(PVR_LINUX_MEM_AREA_POOL_ALLOW_SHRINK)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0))
#include <linux/shrinker.h>
#endif
#endif

#include "img_defs.h"
#include "services.h"
#include "servicesint.h"
#include "syscommon.h"
#include "mutils.h"
#include "mm.h"
#include "pvrmmap.h"
#include "mmap.h"
#include "osfunc.h"
#include "pvr_debug.h"
#include "proc.h"
#include "mutex.h"
#include "lock.h"

#if defined(DEBUG_LINUX_MEM_AREAS) || defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	#include "lists.h"
#endif

/* Decide whether or not DevMem allocs need __GFP_DMA32 */
#ifndef SGX_FEATURE_36BIT_MMU
#	ifdef CONFIG_ZONE_DMA32
#		if defined CONFIG_X86_PAE || defined CONFIG_ARM_LPAE || defined CONFIG_64BIT
#			define PVR_USE_DMA32_FOR_DEVMEM_ALLOCS
#		endif
#	endif
#endif

/*
 * The page pool entry count is an atomic int so that the shrinker function
 * can return it even when we can't take the lock that protects the page pool
 * list.
 */
static atomic_t g_sPagePoolEntryCount = ATOMIC_INIT(0);

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
typedef enum {
    DEBUG_MEM_ALLOC_TYPE_KMALLOC,
    DEBUG_MEM_ALLOC_TYPE_VMALLOC,
    DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES,
    DEBUG_MEM_ALLOC_TYPE_IOREMAP,
    DEBUG_MEM_ALLOC_TYPE_IO,
    DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE,
    DEBUG_MEM_ALLOC_TYPE_ION,
#if defined(PVR_LINUX_MEM_AREA_USE_VMAP)
    DEBUG_MEM_ALLOC_TYPE_VMAP,
#endif
    DEBUG_MEM_ALLOC_TYPE_COUNT
} DEBUG_MEM_ALLOC_TYPE;

typedef struct _DEBUG_MEM_ALLOC_REC
{
    DEBUG_MEM_ALLOC_TYPE    eAllocType;
	IMG_UINTPTR_T			uiKey; /* Some unique value (private to the eAllocType) */
    IMG_VOID                *pvCpuVAddr;
    IMG_CPU_PHYADDR         sCpuPAddr;
    IMG_VOID                *pvPrivateData;
	IMG_SIZE_T				uiBytes;
	pid_t					pid;
    IMG_CHAR                *pszFileName;
    IMG_UINT32              ui32Line;
    
    struct _DEBUG_MEM_ALLOC_REC   *psNext;
	struct _DEBUG_MEM_ALLOC_REC   **ppsThis;
} DEBUG_MEM_ALLOC_REC;

static IMPLEMENT_LIST_ANY_VA_2(DEBUG_MEM_ALLOC_REC, IMG_BOOL, IMG_FALSE)
static IMPLEMENT_LIST_ANY_VA(DEBUG_MEM_ALLOC_REC)
static IMPLEMENT_LIST_FOR_EACH(DEBUG_MEM_ALLOC_REC)
static IMPLEMENT_LIST_INSERT(DEBUG_MEM_ALLOC_REC)
static IMPLEMENT_LIST_REMOVE(DEBUG_MEM_ALLOC_REC)


static DEBUG_MEM_ALLOC_REC *g_MemoryRecords;

static IMG_UINT32 g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_COUNT];
static IMG_UINT32 g_HighWaterMarkData[DEBUG_MEM_ALLOC_TYPE_COUNT];

/* vmalloc + kmalloc + alloc_pages + kmem_cache */
static IMG_UINT32 g_SysRAMWaterMark;		/* Doesn't include page pool */
static IMG_UINT32 g_SysRAMHighWaterMark;	/* *DOES* include page pool */

static inline IMG_UINT32
SysRAMTrueWaterMark(void)
{
	return g_SysRAMWaterMark + PAGES_TO_BYTES(atomic_read(&g_sPagePoolEntryCount));
}

/* ioremap + io */
static IMG_UINT32 g_IOMemWaterMark;
static IMG_UINT32 g_IOMemHighWaterMark;

static IMG_VOID DebugMemAllocRecordAdd(DEBUG_MEM_ALLOC_TYPE eAllocType,
                                       IMG_UINTPTR_T uiKey,
                                       IMG_VOID *pvCpuVAddr,
                                       IMG_CPU_PHYADDR sCpuPAddr,
                                       IMG_VOID *pvPrivateData,
                                       IMG_SIZE_T uiBytes,
                                       IMG_CHAR *pszFileName,
                                       IMG_UINT32 ui32Line);

static IMG_VOID DebugMemAllocRecordRemove(DEBUG_MEM_ALLOC_TYPE eAllocType, IMG_UINTPTR_T uiKey, IMG_CHAR *pszFileName, IMG_UINT32 ui32Line);

static IMG_CHAR *DebugMemAllocRecordTypeToString(DEBUG_MEM_ALLOC_TYPE eAllocType);


static struct proc_dir_entry *g_SeqFileMemoryRecords;
static void* ProcSeqNextMemoryRecords(struct seq_file *sfile,void* el,loff_t off);
static void ProcSeqShowMemoryRecords(struct seq_file *sfile,void* el);
static void* ProcSeqOff2ElementMemoryRecords(struct seq_file * sfile, loff_t off);

#endif


#if defined(DEBUG_LINUX_MEM_AREAS)
typedef struct _DEBUG_LINUX_MEM_AREA_REC
{
	LinuxMemArea                *psLinuxMemArea;
    IMG_UINT32                  ui32Flags;
	pid_t					    pid;

	struct _DEBUG_LINUX_MEM_AREA_REC  *psNext;
	struct _DEBUG_LINUX_MEM_AREA_REC  **ppsThis;
}DEBUG_LINUX_MEM_AREA_REC;


static IMPLEMENT_LIST_ANY_VA(DEBUG_LINUX_MEM_AREA_REC)
static IMPLEMENT_LIST_FOR_EACH(DEBUG_LINUX_MEM_AREA_REC)
static IMPLEMENT_LIST_INSERT(DEBUG_LINUX_MEM_AREA_REC)
static IMPLEMENT_LIST_REMOVE(DEBUG_LINUX_MEM_AREA_REC)




static DEBUG_LINUX_MEM_AREA_REC *g_LinuxMemAreaRecords;
static IMG_UINT32 g_LinuxMemAreaCount;
static IMG_UINT32 g_LinuxMemAreaWaterMark;
static IMG_UINT32 g_LinuxMemAreaHighWaterMark;


static struct proc_dir_entry *g_SeqFileMemArea;

static void* ProcSeqNextMemArea(struct seq_file *sfile,void* el,loff_t off);
static void ProcSeqShowMemArea(struct seq_file *sfile,void* el);
static void* ProcSeqOff2ElementMemArea(struct seq_file *sfile, loff_t off);

#endif

#if defined(DEBUG_LINUX_MEM_AREAS) || defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
static PVRSRV_LINUX_MUTEX g_sDebugMutex;
#endif

#if (defined(DEBUG_LINUX_MEM_AREAS) || defined(DEBUG_LINUX_MEMORY_ALLOCATIONS))
static void ProcSeqStartstopDebugMutex(struct seq_file *sfile,IMG_BOOL start);
#endif

typedef	struct
{
	/* Linkage for page pool LRU list */
	struct list_head sPagePoolItem;

	struct page *psPage;
} LinuxPagePoolEntry;

static LinuxKMemCache *g_PsLinuxMemAreaCache;
static LinuxKMemCache *g_PsLinuxPagePoolCache;

static LIST_HEAD(g_sPagePoolList);
static int g_iPagePoolMaxEntries;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15))
static IMG_VOID ReservePages(IMG_VOID *pvAddress, IMG_SIZE_T uiLength);
static IMG_VOID UnreservePages(IMG_VOID *pvAddress, IMG_SIZE_T uiLength);
#endif

static LinuxMemArea *LinuxMemAreaStructAlloc(IMG_VOID);
static IMG_VOID LinuxMemAreaStructFree(LinuxMemArea *psLinuxMemArea);
#if defined(DEBUG_LINUX_MEM_AREAS)
static IMG_VOID DebugLinuxMemAreaRecordAdd(LinuxMemArea *psLinuxMemArea, IMG_UINT32 ui32Flags);
static DEBUG_LINUX_MEM_AREA_REC *DebugLinuxMemAreaRecordFind(LinuxMemArea *psLinuxMemArea);
static IMG_VOID DebugLinuxMemAreaRecordRemove(LinuxMemArea *psLinuxMemArea);
#endif


static inline IMG_BOOL
AreaIsUncached(IMG_UINT32 ui32AreaFlags)
{
	return (ui32AreaFlags & (PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_UNCACHED)) != 0;
}

static inline IMG_BOOL
CanFreeToPool(LinuxMemArea *psLinuxMemArea)
{
	return AreaIsUncached(psLinuxMemArea->ui32AreaFlags) && !psLinuxMemArea->bNeedsCacheInvalidate;
}

IMG_VOID *
_KMallocWrapper(IMG_SIZE_T uiByteSize, gfp_t uFlags, IMG_CHAR *pszFileName, IMG_UINT32 ui32Line)
{
    IMG_VOID *pvRet;
    pvRet = kmalloc(uiByteSize, uFlags);
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
    if (pvRet)
    {
		IMG_CPU_PHYADDR sCpuPAddr;
		sCpuPAddr.uiAddr = 0;

        DebugMemAllocRecordAdd(DEBUG_MEM_ALLOC_TYPE_KMALLOC,
                               (IMG_UINTPTR_T)pvRet,
                               pvRet,
                               sCpuPAddr,
                               NULL,
                               uiByteSize,
                               pszFileName,
                               ui32Line
                              );
    }
#else
    PVR_UNREFERENCED_PARAMETER(pszFileName);
    PVR_UNREFERENCED_PARAMETER(ui32Line);
#endif
    return pvRet;
}


IMG_VOID
_KFreeWrapper(IMG_VOID *pvCpuVAddr, IMG_CHAR *pszFileName, IMG_UINT32 ui32Line)
{
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
    DebugMemAllocRecordRemove(
			DEBUG_MEM_ALLOC_TYPE_KMALLOC, 
			(IMG_UINTPTR_T)pvCpuVAddr, 
			pszFileName,  
			ui32Line);
#else
    PVR_UNREFERENCED_PARAMETER(pszFileName);
    PVR_UNREFERENCED_PARAMETER(ui32Line);
#endif
    kfree(pvCpuVAddr);
}


#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
static IMG_VOID
DebugMemAllocRecordAdd(DEBUG_MEM_ALLOC_TYPE eAllocType,
                       IMG_UINTPTR_T uiKey,
                       IMG_VOID *pvCpuVAddr,
                       IMG_CPU_PHYADDR sCpuPAddr,
                       IMG_VOID *pvPrivateData,
                       IMG_SIZE_T uiBytes,
                       IMG_CHAR *pszFileName,
                       IMG_UINT32 ui32Line)
{
    DEBUG_MEM_ALLOC_REC *psRecord;

    LinuxLockMutex(&g_sDebugMutex);

    psRecord = kmalloc(sizeof(DEBUG_MEM_ALLOC_REC), GFP_KERNEL);
	if (!psRecord)
		return;

    psRecord->eAllocType = eAllocType;
    psRecord->uiKey = uiKey;
    psRecord->pvCpuVAddr = pvCpuVAddr;
    psRecord->sCpuPAddr.uiAddr = sCpuPAddr.uiAddr;
    psRecord->pvPrivateData = pvPrivateData;
    psRecord->pid = OSGetCurrentProcessIDKM();
    psRecord->uiBytes = uiBytes;
    psRecord->pszFileName = pszFileName;
    psRecord->ui32Line = ui32Line;
    
	List_DEBUG_MEM_ALLOC_REC_Insert(&g_MemoryRecords, psRecord);
    
    g_WaterMarkData[eAllocType] += uiBytes;
    if (g_WaterMarkData[eAllocType] > g_HighWaterMarkData[eAllocType])
    {
        g_HighWaterMarkData[eAllocType] = g_WaterMarkData[eAllocType];
    }

    if (eAllocType == DEBUG_MEM_ALLOC_TYPE_KMALLOC
       || eAllocType == DEBUG_MEM_ALLOC_TYPE_VMALLOC
       || eAllocType == DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES
       || eAllocType == DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE)
    {
		IMG_SIZE_T uSysRAMTrueWaterMark;

        g_SysRAMWaterMark += uiBytes;
		uSysRAMTrueWaterMark = SysRAMTrueWaterMark();

        if (uSysRAMTrueWaterMark > g_SysRAMHighWaterMark)
        {
            g_SysRAMHighWaterMark = uSysRAMTrueWaterMark;
        }
    }
    else if (eAllocType == DEBUG_MEM_ALLOC_TYPE_IOREMAP
            || eAllocType == DEBUG_MEM_ALLOC_TYPE_IO)
    {
        g_IOMemWaterMark += uiBytes;
        if (g_IOMemWaterMark > g_IOMemHighWaterMark)
        {
            g_IOMemHighWaterMark = g_IOMemWaterMark;
        }
    }

    LinuxUnLockMutex(&g_sDebugMutex);
}


static IMG_BOOL DebugMemAllocRecordRemove_AnyVaCb(DEBUG_MEM_ALLOC_REC *psCurrentRecord, va_list va)
{
	DEBUG_MEM_ALLOC_TYPE eAllocType;
	IMG_UINTPTR_T uiKey;
	
	eAllocType = va_arg(va, DEBUG_MEM_ALLOC_TYPE);
	uiKey = va_arg(va, IMG_UINTPTR_T);

	if (psCurrentRecord->eAllocType == eAllocType
		&& psCurrentRecord->uiKey == uiKey)
	{
		eAllocType = psCurrentRecord->eAllocType;
		g_WaterMarkData[eAllocType] -= psCurrentRecord->uiBytes;
		
		if (eAllocType == DEBUG_MEM_ALLOC_TYPE_KMALLOC
		   || eAllocType == DEBUG_MEM_ALLOC_TYPE_VMALLOC
		   || eAllocType == DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES
		   || eAllocType == DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE)
		{
			g_SysRAMWaterMark -= psCurrentRecord->uiBytes;
		}
		else if (eAllocType == DEBUG_MEM_ALLOC_TYPE_IOREMAP
				|| eAllocType == DEBUG_MEM_ALLOC_TYPE_IO)
		{
			g_IOMemWaterMark -= psCurrentRecord->uiBytes;
		}
		
		List_DEBUG_MEM_ALLOC_REC_Remove(psCurrentRecord);
		kfree(psCurrentRecord);

		return IMG_TRUE;
	}
	else
	{
		return IMG_FALSE;
	}
}


static IMG_VOID
DebugMemAllocRecordRemove(DEBUG_MEM_ALLOC_TYPE eAllocType, IMG_UINTPTR_T uiKey, IMG_CHAR *pszFileName, IMG_UINT32 ui32Line)
{
/*    DEBUG_MEM_ALLOC_REC **ppsCurrentRecord;*/

    LinuxLockMutex(&g_sDebugMutex);

    /* Locate the corresponding allocation entry */
	if (!List_DEBUG_MEM_ALLOC_REC_IMG_BOOL_Any_va(g_MemoryRecords,
												DebugMemAllocRecordRemove_AnyVaCb,
												eAllocType,
												uiKey))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: couldn't find an entry for type=%s with uiKey=" UINTPTR_FMT " (called from %s, line %d\n",
		__FUNCTION__, DebugMemAllocRecordTypeToString(eAllocType), uiKey,
		pszFileName, ui32Line));
	}

    LinuxUnLockMutex(&g_sDebugMutex);
}


static IMG_CHAR *
DebugMemAllocRecordTypeToString(DEBUG_MEM_ALLOC_TYPE eAllocType)
{
    IMG_CHAR *apszDebugMemoryRecordTypes[] = {
        "KMALLOC",
        "VMALLOC",
        "ALLOC_PAGES",
        "IOREMAP",
        "IO",
        "KMEM_CACHE_ALLOC",
#if defined(PVR_LINUX_MEM_AREA_USE_VMAP)
	"VMAP"
#endif
    };
    return apszDebugMemoryRecordTypes[eAllocType];
}
#endif


static IMG_BOOL
AllocFlagsToPGProt(pgprot_t *pPGProtFlags, IMG_UINT32 ui32AllocFlags)
{
    pgprot_t PGProtFlags;

    switch (ui32AllocFlags & PVRSRV_HAP_CACHETYPE_MASK)
    {
        case PVRSRV_HAP_CACHED:
            PGProtFlags = PAGE_KERNEL;
            break;
        case PVRSRV_HAP_WRITECOMBINE:
            PGProtFlags = PGPROT_WC(PAGE_KERNEL);
            break;
        case PVRSRV_HAP_UNCACHED:
            PGProtFlags = PGPROT_UC(PAGE_KERNEL);
            break;
        default:
            PVR_DPF((PVR_DBG_ERROR,
                     "%s: Unknown mapping flags=0x%08x",
                     __FUNCTION__, ui32AllocFlags));
            dump_stack();
            return IMG_FALSE;
    }

    *pPGProtFlags = PGProtFlags;

    return IMG_TRUE;
}

IMG_VOID *
_VMallocWrapper(IMG_SIZE_T uiBytes,
                IMG_UINT32 ui32AllocFlags,
                IMG_CHAR *pszFileName,
                IMG_UINT32 ui32Line)
{
    pgprot_t PGProtFlags;
    IMG_VOID *pvRet;
	gfp_t gfp_mask; 
    
    if (!AllocFlagsToPGProt(&PGProtFlags, ui32AllocFlags))
    {
            return NULL;
    }

	gfp_mask = GFP_KERNEL;

#if defined(PVR_USE_DMA32_FOR_DEVMEM_ALLOCS)
	gfp_mask |= __GFP_DMA32;
#else
	gfp_mask |= __GFP_HIGHMEM;
#endif

	/* Allocate virtually contiguous pages */
    pvRet = __vmalloc(uiBytes, gfp_mask, PGProtFlags);
    
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
    if (pvRet)
    {
		IMG_CPU_PHYADDR sCpuPAddr;
		sCpuPAddr.uiAddr = 0;

        DebugMemAllocRecordAdd(DEBUG_MEM_ALLOC_TYPE_VMALLOC,
                               (IMG_UINTPTR_T)pvRet,
                               pvRet,
                               sCpuPAddr,
                               NULL,
                               PAGE_ALIGN(uiBytes),
                               pszFileName,
                               ui32Line
                              );
    }
#else
    PVR_UNREFERENCED_PARAMETER(pszFileName);
    PVR_UNREFERENCED_PARAMETER(ui32Line);
#endif

    return pvRet;
}


IMG_VOID
_VFreeWrapper(IMG_VOID *pvCpuVAddr, IMG_CHAR *pszFileName, IMG_UINT32 ui32Line)
{
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
    DebugMemAllocRecordRemove(
			DEBUG_MEM_ALLOC_TYPE_VMALLOC, 
			(IMG_UINTPTR_T)pvCpuVAddr, 
			pszFileName, 
			ui32Line);
#else
    PVR_UNREFERENCED_PARAMETER(pszFileName);
    PVR_UNREFERENCED_PARAMETER(ui32Line);
#endif
    vfree(pvCpuVAddr);
}


#if defined(PVR_LINUX_MEM_AREA_USE_VMAP)
static IMG_VOID *
_VMapWrapper(struct page **ppsPageList, IMG_UINT32 ui32NumPages, IMG_UINT32 ui32AllocFlags, IMG_CHAR *pszFileName, IMG_UINT32 ui32Line)
{
    pgprot_t PGProtFlags;
    IMG_VOID *pvRet;

    if (!AllocFlagsToPGProt(&PGProtFlags, ui32AllocFlags))
    {
            return NULL;
    }

    pvRet = vmap(ppsPageList, ui32NumPages, GFP_KERNEL | __GFP_HIGHMEM, PGProtFlags);

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
    if (pvRet)
    {
		IMG_CPU_PHYADDR sCpuPAddr;
		sCpuPAddr.uiAddr = 0;

        DebugMemAllocRecordAdd(DEBUG_MEM_ALLOC_TYPE_VMAP,
                               (IMG_UINTPTR_T)pvRet,
                               pvRet,
                               sCpuPAddr,
                               NULL,
                               PAGES_TO_BYTES(ui32NumPages),
                               pszFileName,
                               ui32Line
                              );
    }
#else
    PVR_UNREFERENCED_PARAMETER(pszFileName);
    PVR_UNREFERENCED_PARAMETER(ui32Line);
#endif

    return pvRet;
}

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
#define VMapWrapper(ppsPageList, uBytes, ui32AllocFlags) _VMapWrapper(ppsPageList, uBytes, ui32AllocFlags, __FILE__, __LINE__)
#else
#define VMapWrapper(ppsPageList, uBytes, ui32AllocFlags) _VMapWrapper(ppsPageList, uBytes, ui32AllocFlags, NULL, 0)
#endif


static IMG_VOID
_VUnmapWrapper(IMG_VOID *pvCpuVAddr, IMG_CHAR *pszFileName, IMG_UINT32 ui32Line)
{
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
    DebugMemAllocRecordRemove(DEBUG_MEM_ALLOC_TYPE_VMAP,
							  (IMG_UINTPTR_T)pvCpuVAddr, pszFileName, ui32Line);
#else
    PVR_UNREFERENCED_PARAMETER(pszFileName);
    PVR_UNREFERENCED_PARAMETER(ui32Line);
#endif
    vunmap(pvCpuVAddr);
}

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
#define VUnmapWrapper(pvCpuVAddr) _VUnmapWrapper(pvCpuVAddr, __FILE__, __LINE__)
#else
#define VUnmapWrapper(pvCpuVAddr) _VUnmapWrapper(pvCpuVAddr, NULL, 0)
#endif

#endif /* defined(PVR_LINUX_MEM_AREA_USE_VMAP) */


IMG_VOID
_KMemCacheFreeWrapper(LinuxKMemCache *psCache, IMG_VOID *pvObject, IMG_CHAR *pszFileName, IMG_UINT32 ui32Line)
{
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
    DebugMemAllocRecordRemove(
			DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE, 
			(IMG_UINTPTR_T)pvObject, 
			pszFileName, 
			ui32Line);
#else
    PVR_UNREFERENCED_PARAMETER(pszFileName);
    PVR_UNREFERENCED_PARAMETER(ui32Line);
#endif

    kmem_cache_free(psCache, pvObject);
}


const IMG_CHAR *
KMemCacheNameWrapper(LinuxKMemCache *psCache)
{
    PVR_UNREFERENCED_PARAMETER(psCache);

    /* In this case kmem_cache_t is an incomplete typedef,
     * so we can't even de-reference to get the name member. It is also a GPL export symbol */
    return "";
}


static LinuxPagePoolEntry *
LinuxPagePoolEntryAlloc(IMG_VOID)
{
    return KMemCacheAllocWrapper(g_PsLinuxPagePoolCache, GFP_KERNEL);
}

static IMG_VOID
LinuxPagePoolEntryFree(LinuxPagePoolEntry *psPagePoolEntry)
{
	KMemCacheFreeWrapper(g_PsLinuxPagePoolCache, psPagePoolEntry);
}


static struct page *
AllocPageFromLinux(void)
{
	struct page *psPage;
	gfp_t gfp_mask; 

	gfp_mask = GFP_KERNEL;

#if defined(PVR_USE_DMA32_FOR_DEVMEM_ALLOCS)
	gfp_mask |= __GFP_DMA32;
#else
	gfp_mask |= __GFP_HIGHMEM;
#endif

	psPage = alloc_pages(gfp_mask, 0);
	if (!psPage)
	{
		return NULL;

	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15))
    	/* Reserve those pages to allow them to be re-mapped to user space */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0))		
    	SetPageReserved(psPage);
#else
        mem_map_reserve(psPage);
#endif
#endif
	return psPage;
}


static IMG_VOID
FreePageToLinux(struct page *psPage)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15))		
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0))		
        ClearPageReserved(psPage);
#else
        mem_map_reserve(psPage);
#endif		
#endif	
        __free_pages(psPage, 0);
}


#if (PVR_LINUX_MEM_AREA_POOL_MAX_PAGES != 0)
static DEFINE_MUTEX(g_sPagePoolMutex);

static inline void
PagePoolLock(void)
{
	mutex_lock(&g_sPagePoolMutex);
}

static inline void
PagePoolUnlock(void)
{
	mutex_unlock(&g_sPagePoolMutex);
}

static inline int
PagePoolTrylock(void)
{
	return mutex_trylock(&g_sPagePoolMutex);
}

#else	/* (PVR_LINUX_MEM_AREA_POOL_MAX_PAGES != 0) */
static inline void
PagePoolLock(void)
{
}

static inline void
PagePoolUnlock(void)
{
}

static inline int
PagePoolTrylock(void)
{
	return 1;
}
#endif	/* (PVR_LINUX_MEM_AREA_POOL_MAX_PAGES != 0) */


static inline void
AddEntryToPool(LinuxPagePoolEntry *psPagePoolEntry)
{
	list_add_tail(&psPagePoolEntry->sPagePoolItem, &g_sPagePoolList);
	atomic_inc(&g_sPagePoolEntryCount);
}

static inline void
RemoveEntryFromPool(LinuxPagePoolEntry *psPagePoolEntry)
{
	list_del(&psPagePoolEntry->sPagePoolItem);
	atomic_dec(&g_sPagePoolEntryCount);
}

static inline LinuxPagePoolEntry *
RemoveFirstEntryFromPool(void)
{
	LinuxPagePoolEntry *psPagePoolEntry;

	if (list_empty(&g_sPagePoolList))
	{
		PVR_ASSERT(atomic_read(&g_sPagePoolEntryCount) == 0);

		return NULL;
	}

	PVR_ASSERT(atomic_read(&g_sPagePoolEntryCount) > 0);

	psPagePoolEntry = list_first_entry(&g_sPagePoolList, LinuxPagePoolEntry, sPagePoolItem);

	RemoveEntryFromPool(psPagePoolEntry);

	return psPagePoolEntry;
}

static struct page *
AllocPage(IMG_UINT32 ui32AreaFlags, IMG_BOOL *pbFromPagePool)
{
	struct page *psPage = NULL;

	/*
	 * Only uncached allocations can come from the page pool.
	 * The page pool is currently used to reduce the cost of
	 * invalidating the CPU cache when uncached memory is allocated.
	 */
	if (AreaIsUncached(ui32AreaFlags) && atomic_read(&g_sPagePoolEntryCount) != 0)
	{
		LinuxPagePoolEntry *psPagePoolEntry;

		PagePoolLock();
		psPagePoolEntry = RemoveFirstEntryFromPool();
		PagePoolUnlock();

		/* List may have changed since we checked the counter */
		if (psPagePoolEntry)
		{
			psPage = psPagePoolEntry->psPage;
			LinuxPagePoolEntryFree(psPagePoolEntry);
			*pbFromPagePool = IMG_TRUE;
		}
	}

	if (!psPage)
	{
		psPage = AllocPageFromLinux();
		if (psPage)
		{
			*pbFromPagePool = IMG_FALSE;
		}
	}

	return psPage;

}

static IMG_VOID
FreePage(IMG_BOOL bToPagePool, struct page *psPage)
{
	/* Only uncached allocations can be freed to the page pool */
	if (bToPagePool && atomic_read(&g_sPagePoolEntryCount) < g_iPagePoolMaxEntries)
	{
		LinuxPagePoolEntry *psPagePoolEntry = LinuxPagePoolEntryAlloc();
		if (psPagePoolEntry)
		{
			psPagePoolEntry->psPage = psPage;

			PagePoolLock();
			AddEntryToPool(psPagePoolEntry);
			PagePoolUnlock();

			return;
		}
	}

	FreePageToLinux(psPage);
}

static IMG_VOID
FreePagePool(IMG_VOID)
{
	LinuxPagePoolEntry *psPagePoolEntry, *psTempPoolEntry;

	PagePoolLock();

#if (PVR_LINUX_MEM_AREA_POOL_MAX_PAGES != 0)
	PVR_TRACE(("%s: Freeing %d pages from pool", __FUNCTION__, atomic_read(&g_sPagePoolEntryCount)));
#else
	PVR_ASSERT(atomic_read(&g_sPagePoolEntryCount) == 0);
	PVR_ASSERT(list_empty(&g_sPagePoolList));
#endif

	list_for_each_entry_safe(psPagePoolEntry, psTempPoolEntry, &g_sPagePoolList, sPagePoolItem)
	{
		RemoveEntryFromPool(psPagePoolEntry);

		FreePageToLinux(psPagePoolEntry->psPage);
		LinuxPagePoolEntryFree(psPagePoolEntry);
	}

	PVR_ASSERT(atomic_read(&g_sPagePoolEntryCount) == 0);

	PagePoolUnlock();
}

#if defined(PVR_LINUX_MEM_AREA_POOL_ALLOW_SHRINK)
#if defined(PVRSRV_NEED_PVR_ASSERT)
static struct shrinker g_sShrinker;
#endif

static int
ShrinkPagePool(struct shrinker *psShrinker, struct shrink_control *psShrinkControl)
{
	unsigned long uNumToScan = psShrinkControl->nr_to_scan;

	PVR_ASSERT(psShrinker == &g_sShrinker);
	(void)psShrinker;

	if (uNumToScan != 0)
	{
		LinuxPagePoolEntry *psPagePoolEntry, *psTempPoolEntry;

		PVR_TRACE(("%s: Number to scan: %ld", __FUNCTION__, uNumToScan));
		PVR_TRACE(("%s: Pages in pool before scan: %d", __FUNCTION__, atomic_read(&g_sPagePoolEntryCount)));

		if (!PagePoolTrylock())
		{
			PVR_TRACE(("%s: Couldn't get page pool lock", __FUNCTION__));
			return -1;
		}

		list_for_each_entry_safe(psPagePoolEntry, psTempPoolEntry, &g_sPagePoolList, sPagePoolItem)
		{
			RemoveEntryFromPool(psPagePoolEntry);

			FreePageToLinux(psPagePoolEntry->psPage);
			LinuxPagePoolEntryFree(psPagePoolEntry);

			if (--uNumToScan == 0)
			{
				break;
			}
		}

		if (list_empty(&g_sPagePoolList))
		{
			PVR_ASSERT(atomic_read(&g_sPagePoolEntryCount) == 0);
		}

		PagePoolUnlock();

		PVR_TRACE(("%s: Pages in pool after scan: %d", __FUNCTION__, atomic_read(&g_sPagePoolEntryCount)));
	}

	return atomic_read(&g_sPagePoolEntryCount);
}
#endif

static IMG_BOOL
AllocPages(IMG_UINT32 ui32AreaFlags, struct page ***pppsPageList, IMG_HANDLE *phBlockPageList, IMG_UINT32 ui32NumPages, IMG_BOOL *pbFromPagePool)
{
    struct page **ppsPageList;
    IMG_HANDLE hBlockPageList;
    IMG_INT32 i;		/* Must be signed; see "for" loop conditions */
    PVRSRV_ERROR eError;
    IMG_BOOL bFromPagePool = IMG_FALSE;

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	IMG_CPU_PHYADDR sCpuPAddr;
#endif

    eError = OSAllocMem(0, sizeof(*ppsPageList) * ui32NumPages, (IMG_VOID **)&ppsPageList, &hBlockPageList,
							"Array of pages");
    if (eError != PVRSRV_OK)
    {
        goto failed_page_list_alloc;
    }
    
    *pbFromPagePool = IMG_TRUE;
    for(i = 0; i < (IMG_INT32)ui32NumPages; i++)
    {
        ppsPageList[i] = AllocPage(ui32AreaFlags, &bFromPagePool);
        if (!ppsPageList[i])
        {
            goto failed_alloc_pages;
        }
	*pbFromPagePool &= bFromPagePool;
    }

    *pppsPageList = ppsPageList;
    *phBlockPageList = hBlockPageList;

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	sCpuPAddr.uiAddr = 0;

    DebugMemAllocRecordAdd(DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES,
                           (IMG_UINTPTR_T)ppsPageList,
                           0,
                           sCpuPAddr,
                           NULL,
                           PAGES_TO_BYTES(ui32NumPages),
                           "unknown",
                           0
                          );
#endif

    return IMG_TRUE;
    
failed_alloc_pages:
    for(i--; i >= 0; i--)
    {
        FreePage(*pbFromPagePool, ppsPageList[i]);
    }
    (IMG_VOID) OSFreeMem(0, sizeof(*ppsPageList) * ui32NumPages, ppsPageList, hBlockPageList);

failed_page_list_alloc:
    return IMG_FALSE;
}


static IMG_VOID
FreePages(IMG_BOOL bToPagePool, struct page **ppsPageList, IMG_HANDLE hBlockPageList, IMG_UINT32 ui32NumPages)
{
    IMG_INT32 i;

    for(i = 0; i < (IMG_INT32)ui32NumPages; i++)
    {
        FreePage(bToPagePool, ppsPageList[i]);
    }

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
    DebugMemAllocRecordRemove(
			DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES, 
			(IMG_UINTPTR_T)ppsPageList, 
			__FILE__, 
			__LINE__);
#endif

    (IMG_VOID) OSFreeMem(0, sizeof(*ppsPageList) * ui32NumPages, ppsPageList, hBlockPageList);
}


LinuxMemArea *
NewVMallocLinuxMemArea(IMG_SIZE_T uBytes, IMG_UINT32 ui32AreaFlags)
{
    LinuxMemArea *psLinuxMemArea = NULL;
    IMG_VOID *pvCpuVAddr;
#if defined(PVR_LINUX_MEM_AREA_USE_VMAP)
    IMG_UINT32 ui32NumPages = 0;
    struct page **ppsPageList = NULL;
    IMG_HANDLE hBlockPageList;
#endif
    IMG_BOOL bFromPagePool = IMG_FALSE;

    psLinuxMemArea = LinuxMemAreaStructAlloc();
    if (!psLinuxMemArea)
    {
        goto failed;
    }

#if defined(PVR_LINUX_MEM_AREA_USE_VMAP)
    ui32NumPages = RANGE_TO_PAGES(uBytes);

    if (!AllocPages(ui32AreaFlags, &ppsPageList, &hBlockPageList, ui32NumPages, &bFromPagePool))
    {
	goto failed;
    }

    pvCpuVAddr = VMapWrapper(ppsPageList, ui32NumPages, ui32AreaFlags);
#else	/* defined(PVR_LINUX_MEM_AREA_USE_VMAP) */
    pvCpuVAddr = VMallocWrapper(uBytes, ui32AreaFlags);
    if (!pvCpuVAddr)
    {
        goto failed;
    }
/* PG_reserved was deprecated in linux-2.6.15 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15))
    /* Reserve those pages to allow them to be re-mapped to user space */
    ReservePages(pvCpuVAddr, uBytes);
#endif
#endif	/* defined(PVR_LINUX_MEM_AREA_USE_VMAP) */ 

    psLinuxMemArea->eAreaType = LINUX_MEM_AREA_VMALLOC;
    psLinuxMemArea->uData.sVmalloc.pvVmallocAddress = pvCpuVAddr;
#if defined(PVR_LINUX_MEM_AREA_USE_VMAP)
    psLinuxMemArea->uData.sVmalloc.ppsPageList = ppsPageList;
    psLinuxMemArea->uData.sVmalloc.hBlockPageList = hBlockPageList;
#endif
    psLinuxMemArea->uiByteSize = uBytes;
    psLinuxMemArea->ui32AreaFlags = ui32AreaFlags;
    INIT_LIST_HEAD(&psLinuxMemArea->sMMapOffsetStructList);

#if defined(DEBUG_LINUX_MEM_AREAS)
    DebugLinuxMemAreaRecordAdd(psLinuxMemArea, ui32AreaFlags);
#endif

    /* This works around a problem where Linux will not invalidate
     * the cache for physical memory it frees that is direct mapped.
     *
     * As a result, cache entries remain that may be subsequently flushed
     * to these physical pages after they have been allocated for another
     * purpose. For a subsequent cached use of this memory, that is not a
     * problem, but if we are allocating uncached or write-combined memory,
     * and bypassing the cache, it can cause subsequent uncached writes to
     * the memory to be replaced with junk from the cache.
     *
     * If the pages are from our page cache, no cache invalidate is needed.
     *
     * This just handles the __vmalloc() case (when we have a kernel virtual
     * address range). The alloc_pages() path is handled in mmap.c.
     */
    if (AreaIsUncached(ui32AreaFlags) && !bFromPagePool)
    {
        OSInvalidateCPUCacheRangeKM(psLinuxMemArea, 0, pvCpuVAddr, uBytes);
    }

    return psLinuxMemArea;

failed:
    PVR_DPF((PVR_DBG_ERROR, "%s: failed!", __FUNCTION__));
#if defined(PVR_LINUX_MEM_AREA_USE_VMAP)
    if (ppsPageList)
    {
	FreePages(bFromPagePool, ppsPageList, hBlockPageList, ui32NumPages);
    }
#endif
    if (psLinuxMemArea)
    {
        LinuxMemAreaStructFree(psLinuxMemArea);
    }

    return NULL;
}


IMG_VOID
FreeVMallocLinuxMemArea(LinuxMemArea *psLinuxMemArea)
{
#if defined(PVR_LINUX_MEM_AREA_USE_VMAP)
    IMG_UINT32 ui32NumPages;
    struct page **ppsPageList;
    IMG_HANDLE hBlockPageList;
#endif

    PVR_ASSERT(psLinuxMemArea);
    PVR_ASSERT(psLinuxMemArea->eAreaType == LINUX_MEM_AREA_VMALLOC);
    PVR_ASSERT(psLinuxMemArea->uData.sVmalloc.pvVmallocAddress);

#if defined(DEBUG_LINUX_MEM_AREAS)
    DebugLinuxMemAreaRecordRemove(psLinuxMemArea);
#endif

    PVR_DPF((PVR_DBG_MESSAGE,"%s: pvCpuVAddr: %p",
             __FUNCTION__, psLinuxMemArea->uData.sVmalloc.pvVmallocAddress));

#if defined(PVR_LINUX_MEM_AREA_USE_VMAP)
    VUnmapWrapper(psLinuxMemArea->uData.sVmalloc.pvVmallocAddress);

    ui32NumPages = RANGE_TO_PAGES(psLinuxMemArea->uiByteSize);
    ppsPageList = psLinuxMemArea->uData.sVmalloc.ppsPageList;
    hBlockPageList = psLinuxMemArea->uData.sVmalloc.hBlockPageList;
    
    FreePages(CanFreeToPool(psLinuxMemArea), ppsPageList, hBlockPageList, ui32NumPages);
#else
/* PG_reserved was deprecated in linux-2.6.15 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15))
    UnreservePages(psLinuxMemArea->uData.sVmalloc.pvVmallocAddress,
                    psLinuxMemArea->uiByteSize);
#endif

    VFreeWrapper(psLinuxMemArea->uData.sVmalloc.pvVmallocAddress);
#endif	/* defined(PVR_LINUX_MEM_AREA_USE_VMAP) */ 

    LinuxMemAreaStructFree(psLinuxMemArea);
}


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15))
/* Reserve pages of memory in order that they're not automatically
   deallocated after the last user reference dies. */
static IMG_VOID
ReservePages(IMG_VOID *pvAddress, IMG_SIZE_T uLength)
{
	IMG_VOID *pvPage;
	IMG_VOID *pvEnd = pvAddress + uLength;

	for(pvPage = pvAddress; pvPage < pvEnd;  pvPage += PAGE_SIZE)
	{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0))
		SetPageReserved(vmalloc_to_page(pvPage));
#else
		mem_map_reserve(vmalloc_to_page(pvPage));
#endif
	}
}


/* Un-reserve pages of memory in order that they can be freed. */
static IMG_VOID
UnreservePages(IMG_VOID *pvAddress, IMG_SIZE_T uLength)
{
	IMG_VOID *pvPage;
	IMG_VOID *pvEnd = pvAddress + uLength;

	for(pvPage = pvAddress; pvPage < pvEnd;  pvPage += PAGE_SIZE)
	{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0))
		ClearPageReserved(vmalloc_to_page(pvPage));
#else
		mem_map_unreserve(vmalloc_to_page(pvPage));
#endif
	}
}
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)) */


IMG_VOID *
_IORemapWrapper(IMG_CPU_PHYADDR BasePAddr,
               IMG_SIZE_T uBytes,
               IMG_UINT32 ui32MappingFlags,
               IMG_CHAR *pszFileName,
               IMG_UINT32 ui32Line)
{
    IMG_VOID *pvIORemapCookie;
    
    switch (ui32MappingFlags & PVRSRV_HAP_CACHETYPE_MASK)
    {
        case PVRSRV_HAP_CACHED:
	    pvIORemapCookie = (IMG_VOID *)IOREMAP(BasePAddr.uiAddr, uBytes);
            break;
        case PVRSRV_HAP_WRITECOMBINE:
	    pvIORemapCookie = (IMG_VOID *)IOREMAP_WC(BasePAddr.uiAddr, uBytes);
            break;
        case PVRSRV_HAP_UNCACHED:
            pvIORemapCookie = (IMG_VOID *)IOREMAP_UC(BasePAddr.uiAddr, uBytes);
            break;
        default:
            PVR_DPF((PVR_DBG_ERROR, "IORemapWrapper: unknown mapping flags"));
            return NULL;
    }
    
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
    if (pvIORemapCookie)
    {
        DebugMemAllocRecordAdd(DEBUG_MEM_ALLOC_TYPE_IOREMAP,
                               (IMG_UINTPTR_T)pvIORemapCookie,
                               pvIORemapCookie,
                               BasePAddr,
                               NULL,
                               uBytes,
                               pszFileName,
                               ui32Line
                              );
    }
#else
    PVR_UNREFERENCED_PARAMETER(pszFileName);
    PVR_UNREFERENCED_PARAMETER(ui32Line);
#endif

    return pvIORemapCookie;
}


IMG_VOID
_IOUnmapWrapper(IMG_VOID *pvIORemapCookie, IMG_CHAR *pszFileName, IMG_UINT32 ui32Line)
{
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
    DebugMemAllocRecordRemove(
			DEBUG_MEM_ALLOC_TYPE_IOREMAP, 
			(IMG_UINTPTR_T)pvIORemapCookie, 
			pszFileName, 
			ui32Line);
#else
    PVR_UNREFERENCED_PARAMETER(pszFileName);
    PVR_UNREFERENCED_PARAMETER(ui32Line);
#endif
    iounmap(pvIORemapCookie);
}


LinuxMemArea *
NewIORemapLinuxMemArea(IMG_CPU_PHYADDR BasePAddr,
                       IMG_SIZE_T uBytes,
                       IMG_UINT32 ui32AreaFlags)
{
    LinuxMemArea *psLinuxMemArea;
    IMG_VOID *pvIORemapCookie;

    psLinuxMemArea = LinuxMemAreaStructAlloc();
    if (!psLinuxMemArea)
    {
        return NULL;
    }

    pvIORemapCookie = IORemapWrapper(BasePAddr, uBytes, ui32AreaFlags);
    if (!pvIORemapCookie)
    {
        LinuxMemAreaStructFree(psLinuxMemArea);
        return NULL;
    }

    psLinuxMemArea->eAreaType = LINUX_MEM_AREA_IOREMAP;
    psLinuxMemArea->uData.sIORemap.pvIORemapCookie = pvIORemapCookie;
    psLinuxMemArea->uData.sIORemap.CPUPhysAddr = BasePAddr;
    psLinuxMemArea->uiByteSize = uBytes;
    psLinuxMemArea->ui32AreaFlags = ui32AreaFlags;
    INIT_LIST_HEAD(&psLinuxMemArea->sMMapOffsetStructList);

#if defined(DEBUG_LINUX_MEM_AREAS)
    DebugLinuxMemAreaRecordAdd(psLinuxMemArea, ui32AreaFlags);
#endif

    return psLinuxMemArea;
}


IMG_VOID
FreeIORemapLinuxMemArea(LinuxMemArea *psLinuxMemArea)
{
    PVR_ASSERT(psLinuxMemArea->eAreaType == LINUX_MEM_AREA_IOREMAP);

#if defined(DEBUG_LINUX_MEM_AREAS)
    DebugLinuxMemAreaRecordRemove(psLinuxMemArea);
#endif
    
    IOUnmapWrapper(psLinuxMemArea->uData.sIORemap.pvIORemapCookie);

    LinuxMemAreaStructFree(psLinuxMemArea);
}


#if !defined(PVR_MAKE_ALL_PFNS_SPECIAL)
/*
 * Avoid using remap_pfn_range on RAM, if possible.  On x86 systems, with
 * PAT enabled, remap_pfn_range checks the page attributes requested by 
 * remap_pfn_range against those of the direct kernel mapping for those
 * pages (if any).  This is rather annoying if the pages have been obtained
 * with alloc_pages, where we just ask for raw pages; we don't care about
 * the direct mapping.  This latter issue arises when device memory is
 * exported from one process to another.  Services implements this
 * using memory wrapping, which ends up creating an external KV memory area.
 */
static IMG_BOOL
TreatExternalPagesAsContiguous(IMG_SYS_PHYADDR *psSysPhysAddr, IMG_SIZE_T uBytes, IMG_BOOL bPhysContig)
{
	IMG_UINT32 ui32;
	IMG_UINT32 ui32AddrChk;
	IMG_UINT32 ui32NumPages = RANGE_TO_PAGES(uBytes);

	/*
	 * If bPhysContig is IMG_TRUE, we must assume psSysPhysAddr points
	 * to the address of the first page, not an array of page addresses.
	 */
	for (ui32 = 0, ui32AddrChk = psSysPhysAddr[0].uiAddr;
		ui32 < ui32NumPages;
		ui32++, ui32AddrChk = (bPhysContig) ? (ui32AddrChk + PAGE_SIZE) : psSysPhysAddr[ui32].uiAddr)
	{
		if (!pfn_valid(PHYS_TO_PFN(ui32AddrChk)))
		{
			break;
		}
	}
	if (ui32 == ui32NumPages)
	{
		return IMG_FALSE;
	}

	if (!bPhysContig)
	{
		for (ui32 = 0, ui32AddrChk = psSysPhysAddr[0].uiAddr;
			ui32 < ui32NumPages;
			ui32++, ui32AddrChk += PAGE_SIZE)
		{
			if (psSysPhysAddr[ui32].uiAddr != ui32AddrChk)
			{
				return IMG_FALSE;
			}
		}
	}

	return IMG_TRUE;
}
#endif

LinuxMemArea *NewExternalKVLinuxMemArea(IMG_SYS_PHYADDR *pBasePAddr, IMG_VOID *pvCPUVAddr, IMG_SIZE_T uBytes, IMG_BOOL bPhysContig, IMG_UINT32 ui32AreaFlags)
{
    LinuxMemArea *psLinuxMemArea;

    psLinuxMemArea = LinuxMemAreaStructAlloc();
    if (!psLinuxMemArea)
    {
        return NULL;
    }

    psLinuxMemArea->eAreaType = LINUX_MEM_AREA_EXTERNAL_KV;
    psLinuxMemArea->uData.sExternalKV.pvExternalKV = pvCPUVAddr;
    psLinuxMemArea->uData.sExternalKV.bPhysContig =
#if !defined(PVR_MAKE_ALL_PFNS_SPECIAL)
	(bPhysContig || TreatExternalPagesAsContiguous(pBasePAddr, uiBytes, bPhysContig))
                                                    ? IMG_TRUE : IMG_FALSE;
#else
	bPhysContig;
#endif
    if (psLinuxMemArea->uData.sExternalKV.bPhysContig)
    {
	psLinuxMemArea->uData.sExternalKV.uPhysAddr.SysPhysAddr = *pBasePAddr;
    }
    else
    {
	psLinuxMemArea->uData.sExternalKV.uPhysAddr.pSysPhysAddr = pBasePAddr;
    }
    psLinuxMemArea->uiByteSize = uBytes;
    psLinuxMemArea->ui32AreaFlags = ui32AreaFlags;
    INIT_LIST_HEAD(&psLinuxMemArea->sMMapOffsetStructList);

#if defined(DEBUG_LINUX_MEM_AREAS)
    DebugLinuxMemAreaRecordAdd(psLinuxMemArea, ui32AreaFlags);
#endif

    return psLinuxMemArea;
}


IMG_VOID
FreeExternalKVLinuxMemArea(LinuxMemArea *psLinuxMemArea)
{
    PVR_ASSERT(psLinuxMemArea->eAreaType == LINUX_MEM_AREA_EXTERNAL_KV);

#if defined(DEBUG_LINUX_MEM_AREAS)
    DebugLinuxMemAreaRecordRemove(psLinuxMemArea);
#endif
    
    LinuxMemAreaStructFree(psLinuxMemArea);
}


LinuxMemArea *
NewIOLinuxMemArea(IMG_CPU_PHYADDR BasePAddr,
                  IMG_SIZE_T uBytes,
                  IMG_UINT32 ui32AreaFlags)
{
    LinuxMemArea *psLinuxMemArea = LinuxMemAreaStructAlloc();
    if (!psLinuxMemArea)
    {
        return NULL;
    }

    /* Nothing to activly do. We just keep a record of the physical range. */
    psLinuxMemArea->eAreaType = LINUX_MEM_AREA_IO;
    psLinuxMemArea->uData.sIO.CPUPhysAddr.uiAddr = BasePAddr.uiAddr;
    psLinuxMemArea->uiByteSize = uBytes;
    psLinuxMemArea->ui32AreaFlags = ui32AreaFlags;
    INIT_LIST_HEAD(&psLinuxMemArea->sMMapOffsetStructList);

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
    DebugMemAllocRecordAdd(DEBUG_MEM_ALLOC_TYPE_IO,
                           BasePAddr.uiAddr,
                           0,
                           BasePAddr,
                           NULL,
                           uBytes,
                           "unknown",
                           0
                          );
#endif
   
#if defined(DEBUG_LINUX_MEM_AREAS)
    DebugLinuxMemAreaRecordAdd(psLinuxMemArea, ui32AreaFlags);
#endif

    return psLinuxMemArea;
}


IMG_VOID
FreeIOLinuxMemArea(LinuxMemArea *psLinuxMemArea)
{
    PVR_ASSERT(psLinuxMemArea->eAreaType == LINUX_MEM_AREA_IO);
    
#if defined(DEBUG_LINUX_MEM_AREAS)
    DebugLinuxMemAreaRecordRemove(psLinuxMemArea);
#endif

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
    DebugMemAllocRecordRemove(DEBUG_MEM_ALLOC_TYPE_IO,
                              psLinuxMemArea->uData.sIO.CPUPhysAddr.uiAddr,
							  __FILE__, 
							  __LINE__);
#endif

    /* Nothing more to do than free the LinuxMemArea struct */

    LinuxMemAreaStructFree(psLinuxMemArea);
}


LinuxMemArea *
NewAllocPagesLinuxMemArea(IMG_SIZE_T uBytes, IMG_UINT32 ui32AreaFlags)
{
    LinuxMemArea *psLinuxMemArea;
    IMG_UINT32 ui32NumPages;
    struct page **ppsPageList;
    IMG_HANDLE hBlockPageList;
    IMG_BOOL bFromPagePool;

    psLinuxMemArea = LinuxMemAreaStructAlloc();
    if (!psLinuxMemArea)
    {
        goto failed_area_alloc;
    }
    
    ui32NumPages = RANGE_TO_PAGES(uBytes);

    if (!AllocPages(ui32AreaFlags, &ppsPageList, &hBlockPageList, ui32NumPages, &bFromPagePool))
    {
	goto failed_alloc_pages;
    }

    psLinuxMemArea->eAreaType = LINUX_MEM_AREA_ALLOC_PAGES;
    psLinuxMemArea->uData.sPageList.ppsPageList = ppsPageList;
    psLinuxMemArea->uData.sPageList.hBlockPageList = hBlockPageList;
    psLinuxMemArea->uiByteSize = uBytes;
    psLinuxMemArea->ui32AreaFlags = ui32AreaFlags;
    INIT_LIST_HEAD(&psLinuxMemArea->sMMapOffsetStructList);

    /* We defer the cache flush to the first user mapping of this memory */
    psLinuxMemArea->bNeedsCacheInvalidate = AreaIsUncached(ui32AreaFlags) && !bFromPagePool;

#if defined(DEBUG_LINUX_MEM_AREAS)
    DebugLinuxMemAreaRecordAdd(psLinuxMemArea, ui32AreaFlags);
#endif

    return psLinuxMemArea;
    
failed_alloc_pages:
    LinuxMemAreaStructFree(psLinuxMemArea);
failed_area_alloc:
    PVR_DPF((PVR_DBG_ERROR, "%s: failed", __FUNCTION__));
    
    return NULL;
}


IMG_VOID
FreeAllocPagesLinuxMemArea(LinuxMemArea *psLinuxMemArea)
{
    IMG_UINT32 ui32NumPages;
    struct page **ppsPageList;
    IMG_HANDLE hBlockPageList;

    PVR_ASSERT(psLinuxMemArea);
    PVR_ASSERT(psLinuxMemArea->eAreaType == LINUX_MEM_AREA_ALLOC_PAGES);

#if defined(DEBUG_LINUX_MEM_AREAS)
    DebugLinuxMemAreaRecordRemove(psLinuxMemArea);
#endif
    
    ui32NumPages = RANGE_TO_PAGES(psLinuxMemArea->uiByteSize);
    ppsPageList = psLinuxMemArea->uData.sPageList.ppsPageList;
    hBlockPageList = psLinuxMemArea->uData.sPageList.hBlockPageList;
    
    FreePages(CanFreeToPool(psLinuxMemArea), ppsPageList, hBlockPageList, ui32NumPages);
  
    LinuxMemAreaStructFree(psLinuxMemArea);
}

#if defined(CONFIG_ION_OMAP)

#include "env_perproc.h"

#include <linux/ion.h>
#include <linux/omap_ion.h>

extern struct ion_client *gpsIONClient;

LinuxMemArea *
NewIONLinuxMemArea(IMG_SIZE_T uBytes, IMG_UINT32 ui32AreaFlags,
                   IMG_PVOID pvPrivData, IMG_SIZE_T uiPrivDataLength)
{
    const IMG_SIZE_T uiAllocDataLen =
        offsetof(struct omap_ion_tiler_alloc_data, handle);
    struct omap_ion_tiler_alloc_data asAllocData[2] = {};
    u32 *pu32PageAddrs[2] = { NULL, NULL };
    IMG_UINT32 i, ui32NumHandlesPerFd;
    IMG_BYTE *pbPrivData = pvPrivData;
	IMG_CPU_PHYADDR *pCPUPhysAddrs;
    int iNumPages[2] = { 0, 0 };
    LinuxMemArea *psLinuxMemArea;

    psLinuxMemArea = LinuxMemAreaStructAlloc();
    if (!psLinuxMemArea)
    {
        PVR_DPF((PVR_DBG_ERROR, "%s: Failed to allocate LinuxMemArea struct", __func__));
        goto err_out;
    }

    /* Depending on the UM config, userspace might give us info for
     * one or two ION allocations. Divide the total size of data we
     * were given by this ui32AllocDataLen, and check it's 1 or 2.
     * Otherwise abort.
     */
    BUG_ON(uiPrivDataLength != uiAllocDataLen &&
           uiPrivDataLength != uiAllocDataLen * 2);
    ui32NumHandlesPerFd = uiPrivDataLength / uiAllocDataLen;

    /* Shuffle the alloc data into separate Y & UV bits and
     * make two separate allocations via the tiler.
     */
    for(i = 0; i < ui32NumHandlesPerFd; i++)
    {
   	    memcpy(&asAllocData[i], &pbPrivData[i * uiAllocDataLen], uiAllocDataLen);

        if (omap_ion_tiler_alloc(gpsIONClient, &asAllocData[i]) < 0)
        {
            PVR_DPF((PVR_DBG_ERROR, "%s: Failed to allocate via ion_tiler", __func__));
            goto err_free;
        }

        if (omap_tiler_pages(gpsIONClient, asAllocData[i].handle, &iNumPages[i],
                            &pu32PageAddrs[i]) < 0)
        {
            PVR_DPF((PVR_DBG_ERROR, "%s: Failed to compute tiler pages", __func__));
            goto err_free;
        }
    }

    /* Assume the user-allocator has already done the tiler math and that the
     * number of tiler pages allocated matches any other allocation type.
     */
    BUG_ON(uBytes != (iNumPages[0] + iNumPages[1]) * PAGE_SIZE);
    BUG_ON(sizeof(IMG_CPU_PHYADDR) != sizeof(int));

    /* Glue the page lists together */
    pCPUPhysAddrs = vmalloc(sizeof(IMG_CPU_PHYADDR) * (iNumPages[0] + iNumPages[1]));
    if (!pCPUPhysAddrs)
    {
        PVR_DPF((PVR_DBG_ERROR, "%s: Failed to allocate page list", __func__));
        goto err_free;
    }
    for(i = 0; i < iNumPages[0]; i++)
        pCPUPhysAddrs[i].uiAddr = pu32PageAddrs[0][i];
    for(i = 0; i < iNumPages[1]; i++)
        pCPUPhysAddrs[iNumPages[0] + i].uiAddr = pu32PageAddrs[1][i];

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
    DebugMemAllocRecordAdd(DEBUG_MEM_ALLOC_TYPE_ION,
                           (IMG_UINTPTR_T)asAllocData[0].handle,
                           0,
                           (IMG_CPU_PHYADDR){0},
                           NULL,
                           PAGE_ALIGN(uBytes),
                           "unknown",
                           0
                          );
#endif

    for(i = 0; i < 2; i++)
        psLinuxMemArea->uData.sIONTilerAlloc.psIONHandle[i] = asAllocData[i].handle;

    psLinuxMemArea->eAreaType = LINUX_MEM_AREA_ION;
    psLinuxMemArea->uData.sIONTilerAlloc.pCPUPhysAddrs = pCPUPhysAddrs;
    psLinuxMemArea->uiByteSize = uBytes;
    psLinuxMemArea->ui32AreaFlags = ui32AreaFlags;
    INIT_LIST_HEAD(&psLinuxMemArea->sMMapOffsetStructList);

    /* We defer the cache flush to the first user mapping of this memory */
    psLinuxMemArea->bNeedsCacheInvalidate = AreaIsUncached(ui32AreaFlags);

#if defined(DEBUG_LINUX_MEM_AREAS)
    DebugLinuxMemAreaRecordAdd(psLinuxMemArea, ui32AreaFlags);
#endif

err_out:
    return psLinuxMemArea;

err_free:
    LinuxMemAreaStructFree(psLinuxMemArea);
    psLinuxMemArea = IMG_NULL;
    goto err_out;
}


IMG_VOID
FreeIONLinuxMemArea(LinuxMemArea *psLinuxMemArea)
{
    IMG_UINT32 i;

#if defined(DEBUG_LINUX_MEM_AREAS)
    DebugLinuxMemAreaRecordRemove(psLinuxMemArea);
#endif

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
    DebugMemAllocRecordRemove(DEBUG_MEM_ALLOC_TYPE_ION,
                              (IMG_UINTPTR_T)psLinuxMemArea->uData.sIONTilerAlloc.psIONHandle[0],
                              __FILE__, __LINE__);
#endif

    for(i = 0; i < 2; i++)
    {
        if (!psLinuxMemArea->uData.sIONTilerAlloc.psIONHandle[i])
            break;
        ion_free(gpsIONClient, psLinuxMemArea->uData.sIONTilerAlloc.psIONHandle[i]);
        psLinuxMemArea->uData.sIONTilerAlloc.psIONHandle[i] = IMG_NULL;
    }

    /* free copy of page list, originals are freed by ion_free */
    vfree(psLinuxMemArea->uData.sIONTilerAlloc.pCPUPhysAddrs);
    psLinuxMemArea->uData.sIONTilerAlloc.pCPUPhysAddrs = IMG_NULL;

    LinuxMemAreaStructFree(psLinuxMemArea);
}

#elif defined(CONFIG_ION_EXYNOS)
/*
	S.LSI 12.07.24
	After alloc ION memory in gralloc, Pass the ion fd though private data
	Create PVR MemInfo with extracted physical address from ion
*/
#include "srvkm.h"
#include "env_perproc.h"
#include <linux/ion.h>
#include <linux/virtio.h>

static PVRSRV_ERROR SLSIIonAquirePhysAddr(IMG_HANDLE hIonDev,
						  IMG_HANDLE hIonFD,
						  IMG_UINT32 *pui32PageCount,
						  IMG_SYS_PHYADDR **ppasSysPhysAddr,
						  struct ion_handle **ppretIonHandle)
{
	IMG_UINT32 i;
	PVRSRV_ERROR eError;
	struct scatterlist *psScatterList;
	struct scatterlist *psTemp;
	struct ion_handle *psIonHandle;
	IMG_SYS_PHYADDR *pasSysPhysAddr = NULL;
	IMG_UINT32 ui32PageCount = 0;
	struct ion_client *psIonClient = hIonDev;
	int fd = (int) hIonFD;
#if defined (EXYNOS_ION_DMA_BUFFER_FD)
	struct sg_table *pSgtable;
	/* Get the buffer handle */
	psIonHandle = ion_import_dma_buf(psIonClient, fd);
	if (IS_ERR_OR_NULL(psIonHandle))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to get ion_import_dma_buf - ionfd:%d ", __func__, fd));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto exitFailImport;
	}

#else
	/* Get the buffer handle */
	psIonHandle = ion_import_fd(psIonClient, fd);
#endif
	*ppretIonHandle = psIonHandle;

#if defined (EXYNOS_ION_DMA_BUFFER_FD)
	pSgtable = ion_sg_table(psIonClient, psIonHandle);
	if (IS_ERR_OR_NULL(pSgtable))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to get ion_sg_table - ionfd:%d ", __func__, fd));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto exitFailMap;
	}
	psScatterList = pSgtable->sgl;
#else
	psScatterList = ion_map_dma(psIonClient, psIonHandle);
#endif

	if (psScatterList == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to get psScatterList ", __func__));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto exitFailMap;
	}

	/*
		We do a two pass process, 1st workout how many pages there
		are, 2nd fill in the data.
	*/
	for (i=0;i<2;i++)
	{
		psTemp = psScatterList;
		if (i == 1)
		{
			pasSysPhysAddr = vmalloc(sizeof(IMG_SYS_PHYADDR) * ui32PageCount);
			if (pasSysPhysAddr == NULL)
			{
				eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto exitFailAlloc;
			}
			ui32PageCount = 0;	/* Reset the page count a we use if for the index */
		}

		while(psTemp)
		{
			IMG_UINT32 j;

			for (j=0;j<psTemp->length;j+=PAGE_SIZE)
			{
				if (i == 1)
				{
					/* Pass 2: Get the page data */
					pasSysPhysAddr[ui32PageCount].uiAddr = sg_phys(psTemp) + j;
				}
				ui32PageCount++;
			}
			psTemp = sg_next(psTemp);
		}
	}

	*pui32PageCount = ui32PageCount;
	*ppasSysPhysAddr = pasSysPhysAddr;

	return PVRSRV_OK;

exitFailAlloc:
#if defined (EXYNOS_ION_DMA_BUFFER_FD)
#else
	ion_unmap_dma(psIonClient, psIonHandle);
#endif
exitFailMap:
	ion_free(psIonClient, psIonHandle);
exitFailImport:
	return eError;
}


static IMG_VOID SLSIIonReleasePhysAddr(struct ion_client *psIonClient,
				struct ion_handle *psIonHandle)
{
#if defined (EXYNOS_ION_DMA_BUFFER_FD)
#else
	ion_unmap_dma(psIonClient, psIonHandle);
#endif
	ion_free(psIonClient, psIonHandle);
}

LinuxMemArea *
NewIONLinuxMemArea(IMG_UINT32 ui32Bytes, IMG_UINT32 ui32AreaFlags,
                   IMG_PVOID pvPrivData, IMG_UINT32 ui32PrivDataLength)
{
	IMG_UINT32 i;
	const IMG_UINT32 ui32AllocDataLen = sizeof(EXYNOS_ION_ALLOC_DATA);
	EXYNOS_ION_ALLOC_DATA asAllocData[EXYNOS_MAX_YUV_PER_PLANE_ION];
	IMG_SYS_PHYADDR *pu32PageAddrs[EXYNOS_MAX_YUV_PER_PLANE_ION] = { NULL, NULL, NULL};
	int iNumPages[EXYNOS_MAX_YUV_PER_PLANE_ION] = { 0, 0, 0 };
	struct ion_handle *pIonHandle[EXYNOS_MAX_YUV_PER_PLANE_ION] = { NULL, NULL, NULL};
	LinuxMemArea *psLinuxMemArea;
	IMG_CPU_PHYADDR *pCPUPhysAddrs;
	IMG_BYTE *pbPrivData = pvPrivData;
	PVRSRV_ENV_PER_PROCESS_DATA *psPerProcEnv = PVRSRVFindPerProcessPrivateData();
	int iNumPagesForHWC = 0;
	int iCur = 0;

	psLinuxMemArea = LinuxMemAreaStructAlloc();
	if (!psLinuxMemArea)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to allocate LinuxMemArea struct", __func__));
		goto err_out;
	}

	/* Depending on the UM config, userspace might give us info for
	* one or EXYNOS_MAX_YUV_PER_PLANE_ION ION allocations.
	* Divide the total size of data we * were given by this ui32AllocDataLen
	* Otherwise abort.
	*/
	BUG_ON(ui32PrivDataLength != ui32AllocDataLen * EXYNOS_MAX_YUV_PER_PLANE_ION);

	/* map separate allocations*/

	for(i = 0; i < EXYNOS_MAX_YUV_PER_PLANE_ION; i++)
	{
		memcpy(&asAllocData[i], &pbPrivData[i * ui32AllocDataLen], ui32AllocDataLen);
		if (asAllocData[i].i32IonFd < 0)
			continue;

		if (SLSIIonAquirePhysAddr(psPerProcEnv->psIONClient,
				(IMG_HANDLE)asAllocData[i].i32IonFd, &iNumPages[i],
				&pu32PageAddrs[i], &pIonHandle[i]) != PVRSRV_OK)
		{
			PVR_LOG(("%s: Failed to extract physical address from ion fd", __func__));
			goto err_free;
		}

		if (i == 0)
		{
			/* EXYNOS_ION_CLIP_FOR_HSTRIDE flag can be used for the first plane only */
			if (asAllocData[i].ui32Flags & EXYNOS_ION_CLIP_FOR_HSTRIDE)
			{
				/*
				* Calculate the exact size of Plane of Y to know padding size
				* We will cut the padding memory of Y plane and paste it to end of memory
				*/
				iNumPagesForHWC = ALIGN(asAllocData[i].ui32WStride *
						asAllocData[i].ui32Height, PAGE_SIZE) / PAGE_SIZE;
			}
			else
			{
				iNumPagesForHWC = iNumPages[i];
			}
		}
	}

	/* Assume the user-allocator has already done the tiler math and that the
	* number of tiler pages allocated matches any other allocation type.
	*/
	PVR_DPF((PVR_DBG_MESSAGE, "ui32Bytes %d 1: %d, 2: %d, , 3: %d iNumPagesForHWC %d",
			ui32Bytes, iNumPages[0], iNumPages[1], iNumPages[2], iNumPagesForHWC));

	BUG_ON(ui32Bytes != (iNumPages[0] + iNumPages[1] + iNumPages[2]) * PAGE_SIZE);
	BUG_ON(sizeof(IMG_CPU_PHYADDR) != sizeof(int));
	BUG_ON(iNumPagesForHWC == 0 || iNumPages[0] < iNumPagesForHWC);

	/* Glue the page lists together */
	pCPUPhysAddrs = vmalloc(sizeof(IMG_CPU_PHYADDR) * (iNumPages[0] + iNumPages[1] + iNumPages[2]));

	if (!pCPUPhysAddrs)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to allocate page list", __func__));
		goto err_free;
	}

	/* Append the physical address to iNumPageForHWC */
	for(i = 0; i < iNumPagesForHWC; i++)
		pCPUPhysAddrs[iCur++].uiAddr = pu32PageAddrs[0][i].uiAddr;

	for(i = 0; i < iNumPages[1]; i++)
		pCPUPhysAddrs[iCur++].uiAddr = pu32PageAddrs[1][i].uiAddr;

	for(i = 0; i < iNumPages[2]; i++)
		pCPUPhysAddrs[iCur++].uiAddr = pu32PageAddrs[2][i].uiAddr;

	/*
	 * Append the clipped pages to the last, EXYNOS_ION_CLIP_FOR_HSTRIDE
	 *
	 * EXYNOS_ION_CLIP_FOR_HSTRIDE flag is used for SGX HW Converter,
	 * Only NV12, NV21 format buffers set this flag.
	 * So EXYNOS_ION_CLIP_FOR_HSTRIDE flag can be used for the first plane
	 * and clipped pages to append exist in first page only.
	 */
	for(i = 0; i < (iNumPages[0] - iNumPagesForHWC); i++)
		pCPUPhysAddrs[iCur++].uiAddr = pu32PageAddrs[0][i].uiAddr;

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
/*
	DebugMemAllocRecordAdd(DEBUG_MEM_ALLOC_TYPE_ION,
							pIonHandle[0],
							0,
							0,
							NULL,
							PAGE_ALIGN(ui32Bytes),
							"unknown",
							0
							);
*/
#endif

	psLinuxMemArea->uData.sIONTilerAlloc.psIONClient = psPerProcEnv->psIONClient;
	for(i = 0; i < EXYNOS_MAX_YUV_PER_PLANE_ION; i++)
	{
		psLinuxMemArea->uData.sIONTilerAlloc.psIONHandle[i] = pIonHandle[i];
		PVR_DPF((PVR_DBG_MESSAGE, "alloc IonClient %p IonHandl %p",
			psLinuxMemArea->uData.sIONTilerAlloc.psIONClient,
			psLinuxMemArea->uData.sIONTilerAlloc.psIONHandle[i]));
	}

	psLinuxMemArea->eAreaType = LINUX_MEM_AREA_ION;
	psLinuxMemArea->uData.sIONTilerAlloc.pCPUPhysAddrs = pCPUPhysAddrs;
	psLinuxMemArea->uiByteSize = ui32Bytes;
	psLinuxMemArea->ui32AreaFlags = ui32AreaFlags;
	INIT_LIST_HEAD(&psLinuxMemArea->sMMapOffsetStructList);

	/* We defer the cache flush to the first user mapping of this memory */
	psLinuxMemArea->bNeedsCacheInvalidate = AreaIsUncached(ui32AreaFlags);

#if defined(DEBUG_LINUX_MEM_AREAS)
	DebugLinuxMemAreaRecordAdd(psLinuxMemArea, ui32AreaFlags);
#endif

err_out:
	for(i = 0; i < EXYNOS_MAX_YUV_PER_PLANE_ION; i++)
		vfree(pu32PageAddrs[i]);
	return psLinuxMemArea;

err_free:

	LinuxMemAreaStructFree(psLinuxMemArea);
	psLinuxMemArea = IMG_NULL;

	goto err_out;
}


IMG_VOID
FreeIONLinuxMemArea(LinuxMemArea *psLinuxMemArea)
{
	IMG_UINT32 i;

#if defined(DEBUG_LINUX_MEM_AREAS)
	DebugLinuxMemAreaRecordRemove(psLinuxMemArea);
#endif

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
/*
	DebugMemAllocRecordRemove(DEBUG_MEM_ALLOC_TYPE_ION,
				psLinuxMemArea->uData.sIONTilerAlloc.psIONHandle[0],
				__FILE__, __LINE__);
*/
#endif

	for(i = 0; i < EXYNOS_MAX_YUV_PER_PLANE_ION; i++)
	{
		if (!psLinuxMemArea->uData.sIONTilerAlloc.psIONHandle[i])
			continue;

		PVR_DPF((PVR_DBG_MESSAGE, "free IonClient %p IonHandl %p",
			psLinuxMemArea->uData.sIONTilerAlloc.psIONClient,
			psLinuxMemArea->uData.sIONTilerAlloc.psIONHandle[i]));

		SLSIIonReleasePhysAddr(psLinuxMemArea->uData.sIONTilerAlloc.psIONClient,
			psLinuxMemArea->uData.sIONTilerAlloc.psIONHandle[i]);

		psLinuxMemArea->uData.sIONTilerAlloc.psIONHandle[i] = IMG_NULL;
	}
	/* free copy of page list, originals are freed by ion_free */
	vfree(psLinuxMemArea->uData.sIONTilerAlloc.pCPUPhysAddrs);

	psLinuxMemArea->uData.sIONTilerAlloc.pCPUPhysAddrs = IMG_NULL;
	psLinuxMemArea->uData.sIONTilerAlloc.psIONClient = IMG_NULL;

	LinuxMemAreaStructFree(psLinuxMemArea);
}

#endif


struct page*
LinuxMemAreaOffsetToPage(LinuxMemArea *psLinuxMemArea,
                         IMG_UINTPTR_T uByteOffset)
{
    IMG_UINTPTR_T uPageIndex;
    IMG_CHAR *pui8Addr;

    switch (psLinuxMemArea->eAreaType)
    {
        case LINUX_MEM_AREA_ALLOC_PAGES:
            uPageIndex = PHYS_TO_PFN(uByteOffset);
            return psLinuxMemArea->uData.sPageList.ppsPageList[uPageIndex];
 
        case LINUX_MEM_AREA_VMALLOC:
            pui8Addr = psLinuxMemArea->uData.sVmalloc.pvVmallocAddress;
            pui8Addr += uByteOffset;
            return vmalloc_to_page(pui8Addr);
 
        case LINUX_MEM_AREA_SUB_ALLOC:
            /* PRQA S 3670 3 */ /* ignore recursive warning */
            return LinuxMemAreaOffsetToPage(psLinuxMemArea->uData.sSubAlloc.psParentLinuxMemArea,
                                            psLinuxMemArea->uData.sSubAlloc.uiByteOffset
                                             + uByteOffset);
        default:
            PVR_DPF((PVR_DBG_ERROR,
                    "%s: Unsupported request for struct page from LinuxMemArea with type=%s",
                    __FUNCTION__, LinuxMemAreaTypeToString(psLinuxMemArea->eAreaType)));
            return NULL;
    }
}


LinuxKMemCache *
KMemCacheCreateWrapper(IMG_CHAR *pszName,
                       size_t Size,
                       size_t Align,
                       IMG_UINT32 ui32Flags)
{
#if defined(DEBUG_LINUX_SLAB_ALLOCATIONS)
    ui32Flags |= SLAB_POISON|SLAB_RED_ZONE;
#endif
    return kmem_cache_create(pszName, Size, Align, ui32Flags, NULL
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,22))
				, NULL
#endif	/* (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,22) */
			   );
}


IMG_VOID
KMemCacheDestroyWrapper(LinuxKMemCache *psCache)
{
    kmem_cache_destroy(psCache);
}


IMG_VOID *
_KMemCacheAllocWrapper(LinuxKMemCache *psCache,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14))
                      gfp_t Flags,
#else
                      IMG_INT Flags,
#endif
                      IMG_CHAR *pszFileName,
                      IMG_UINT32 ui32Line)
{
    IMG_VOID *pvRet;

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
   	IMG_CPU_PHYADDR sCpuPAddr;
#endif

    pvRet = kmem_cache_zalloc(psCache, Flags);

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	sCpuPAddr.uiAddr = 0;
    DebugMemAllocRecordAdd(DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE,
                           (IMG_UINTPTR_T)pvRet,
                           pvRet,
                           sCpuPAddr,
                           psCache,
                           kmem_cache_size(psCache),
                           pszFileName,
                           ui32Line
                          );
#else
    PVR_UNREFERENCED_PARAMETER(pszFileName);
    PVR_UNREFERENCED_PARAMETER(ui32Line);
#endif
    
    return pvRet;
}


LinuxMemArea *
NewSubLinuxMemArea(LinuxMemArea *psParentLinuxMemArea,
                   IMG_UINTPTR_T uiByteOffset,
                   IMG_SIZE_T uBytes)
{
    LinuxMemArea *psLinuxMemArea;
    
    PVR_ASSERT((uiByteOffset + uBytes) <= psParentLinuxMemArea->uiByteSize);
    
    psLinuxMemArea = LinuxMemAreaStructAlloc();
    if (!psLinuxMemArea)
    {
        return NULL;
    }
    
    psLinuxMemArea->eAreaType = LINUX_MEM_AREA_SUB_ALLOC;
    psLinuxMemArea->uData.sSubAlloc.psParentLinuxMemArea = psParentLinuxMemArea;
    psLinuxMemArea->uData.sSubAlloc.uiByteOffset = uiByteOffset;
    psLinuxMemArea->uiByteSize = uBytes;
    psLinuxMemArea->ui32AreaFlags = psParentLinuxMemArea->ui32AreaFlags;
    psLinuxMemArea->bNeedsCacheInvalidate = psParentLinuxMemArea->bNeedsCacheInvalidate;
    INIT_LIST_HEAD(&psLinuxMemArea->sMMapOffsetStructList);
    
#if defined(DEBUG_LINUX_MEM_AREAS)
    {
        DEBUG_LINUX_MEM_AREA_REC *psParentRecord;
        psParentRecord = DebugLinuxMemAreaRecordFind(psParentLinuxMemArea);
        DebugLinuxMemAreaRecordAdd(psLinuxMemArea, psParentRecord->ui32Flags);
    }
#endif
    
    return psLinuxMemArea;
}


static IMG_VOID
FreeSubLinuxMemArea(LinuxMemArea *psLinuxMemArea)
{
    PVR_ASSERT(psLinuxMemArea->eAreaType == LINUX_MEM_AREA_SUB_ALLOC);

#if defined(DEBUG_LINUX_MEM_AREAS)
    DebugLinuxMemAreaRecordRemove(psLinuxMemArea);
#endif
    
    /* Nothing more to do than free the LinuxMemArea structure */

    LinuxMemAreaStructFree(psLinuxMemArea);
}


static LinuxMemArea *
LinuxMemAreaStructAlloc(IMG_VOID)
{
/* debug */
#if 0
    LinuxMemArea *psLinuxMemArea;
    psLinuxMemArea = kmem_cache_alloc(g_PsLinuxMemAreaCache, GFP_KERNEL);
    printk(KERN_ERR "%s: psLinuxMemArea=%p\n", __FUNCTION__, psLinuxMemArea);
    dump_stack();
    return psLinuxMemArea;
#else
    return KMemCacheAllocWrapper(g_PsLinuxMemAreaCache, GFP_KERNEL);
#endif
}


static IMG_VOID
LinuxMemAreaStructFree(LinuxMemArea *psLinuxMemArea)
{
    KMemCacheFreeWrapper(g_PsLinuxMemAreaCache, psLinuxMemArea);
    /* debug */
    //printk(KERN_ERR "%s(%p)\n", __FUNCTION__, psLinuxMemArea);
}


IMG_VOID
LinuxMemAreaDeepFree(LinuxMemArea *psLinuxMemArea)
{
    switch (psLinuxMemArea->eAreaType)
    {
        case LINUX_MEM_AREA_VMALLOC:
            FreeVMallocLinuxMemArea(psLinuxMemArea);
            break;
        case LINUX_MEM_AREA_ALLOC_PAGES:
            FreeAllocPagesLinuxMemArea(psLinuxMemArea);
            break;
        case LINUX_MEM_AREA_IOREMAP:
            FreeIORemapLinuxMemArea(psLinuxMemArea);
            break;
        case LINUX_MEM_AREA_EXTERNAL_KV:
            FreeExternalKVLinuxMemArea(psLinuxMemArea);
            break;
        case LINUX_MEM_AREA_IO:
            FreeIOLinuxMemArea(psLinuxMemArea);
            break;
        case LINUX_MEM_AREA_SUB_ALLOC:
            FreeSubLinuxMemArea(psLinuxMemArea);
            break;
        case LINUX_MEM_AREA_ION:
            FreeIONLinuxMemArea(psLinuxMemArea);
            break;
        default:
            PVR_DPF((PVR_DBG_ERROR, "%s: Unknown are type (%d)\n",
                     __FUNCTION__, psLinuxMemArea->eAreaType));
            break;
    }
}


#if defined(DEBUG_LINUX_MEM_AREAS)
static IMG_VOID
DebugLinuxMemAreaRecordAdd(LinuxMemArea *psLinuxMemArea, IMG_UINT32 ui32Flags)
{
    DEBUG_LINUX_MEM_AREA_REC *psNewRecord;
    const IMG_CHAR *pi8FlagsString;
    
    LinuxLockMutex(&g_sDebugMutex);

    if (psLinuxMemArea->eAreaType != LINUX_MEM_AREA_SUB_ALLOC)
    {
        g_LinuxMemAreaWaterMark += psLinuxMemArea->uiByteSize;
        if (g_LinuxMemAreaWaterMark > g_LinuxMemAreaHighWaterMark)
        {
            g_LinuxMemAreaHighWaterMark = g_LinuxMemAreaWaterMark;
        }
    }
    g_LinuxMemAreaCount++;
    
    /* Create a new memory allocation record */
    psNewRecord = kmalloc(sizeof(DEBUG_LINUX_MEM_AREA_REC), GFP_KERNEL);
    if (psNewRecord)
    {
        /* Record the allocation */
        psNewRecord->psLinuxMemArea = psLinuxMemArea;
        psNewRecord->ui32Flags = ui32Flags;
        psNewRecord->pid = OSGetCurrentProcessIDKM();
		
		List_DEBUG_LINUX_MEM_AREA_REC_Insert(&g_LinuxMemAreaRecords, psNewRecord);
    }
    else
    {
        PVR_DPF((PVR_DBG_ERROR,
                 "%s: failed to allocate linux memory area record.",
                 __FUNCTION__));
    }
    
    /* Sanity check the flags */
    pi8FlagsString = HAPFlagsToString(ui32Flags);
    if (strstr(pi8FlagsString, "UNKNOWN"))
    {
        PVR_DPF((PVR_DBG_ERROR,
                 "%s: Unexpected flags (0x%08x) associated with psLinuxMemArea @ %p",
                 __FUNCTION__,
                 ui32Flags,
                 psLinuxMemArea));
        //dump_stack();
    }

    LinuxUnLockMutex(&g_sDebugMutex);
}



static IMG_VOID* MatchLinuxMemArea_AnyVaCb(DEBUG_LINUX_MEM_AREA_REC *psCurrentRecord,
										   va_list va)
{
	LinuxMemArea *psLinuxMemArea;
	
	psLinuxMemArea = va_arg(va, LinuxMemArea*);
	if (psCurrentRecord->psLinuxMemArea == psLinuxMemArea)
	{
		return psCurrentRecord;
	}
	else
	{
		return IMG_NULL;
	}
}


static DEBUG_LINUX_MEM_AREA_REC *
DebugLinuxMemAreaRecordFind(LinuxMemArea *psLinuxMemArea)
{
    DEBUG_LINUX_MEM_AREA_REC *psCurrentRecord;

    LinuxLockMutex(&g_sDebugMutex);
	psCurrentRecord = List_DEBUG_LINUX_MEM_AREA_REC_Any_va(g_LinuxMemAreaRecords,
														MatchLinuxMemArea_AnyVaCb,
														psLinuxMemArea);
	
/*exit_unlock:*/
    LinuxUnLockMutex(&g_sDebugMutex);

    return psCurrentRecord;
}


static IMG_VOID
DebugLinuxMemAreaRecordRemove(LinuxMemArea *psLinuxMemArea)
{
    DEBUG_LINUX_MEM_AREA_REC *psCurrentRecord;

    LinuxLockMutex(&g_sDebugMutex);

    if (psLinuxMemArea->eAreaType != LINUX_MEM_AREA_SUB_ALLOC)
    {
        g_LinuxMemAreaWaterMark -= psLinuxMemArea->uiByteSize;
    }
    g_LinuxMemAreaCount--;

    /* Locate the corresponding allocation entry */
	psCurrentRecord = List_DEBUG_LINUX_MEM_AREA_REC_Any_va(g_LinuxMemAreaRecords,
														MatchLinuxMemArea_AnyVaCb,
														psLinuxMemArea);
	if (psCurrentRecord)
	{
		/* Unlink the allocation record */
		List_DEBUG_LINUX_MEM_AREA_REC_Remove(psCurrentRecord);
		kfree(psCurrentRecord);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: couldn't find an entry for psLinuxMemArea=%p\n",
        	     __FUNCTION__, psLinuxMemArea));
	}

    LinuxUnLockMutex(&g_sDebugMutex);
}
#endif


IMG_VOID *
LinuxMemAreaToCpuVAddr(LinuxMemArea *psLinuxMemArea)
{
    switch (psLinuxMemArea->eAreaType)
    {
        case LINUX_MEM_AREA_VMALLOC:
            return psLinuxMemArea->uData.sVmalloc.pvVmallocAddress;
        case LINUX_MEM_AREA_IOREMAP:
            return psLinuxMemArea->uData.sIORemap.pvIORemapCookie;
	case LINUX_MEM_AREA_EXTERNAL_KV:
	    return psLinuxMemArea->uData.sExternalKV.pvExternalKV;
        case LINUX_MEM_AREA_SUB_ALLOC:
        {
            IMG_CHAR *pAddr =
                LinuxMemAreaToCpuVAddr(psLinuxMemArea->uData.sSubAlloc.psParentLinuxMemArea); /* PRQA S 3670 */ /* ignore recursive warning */
            if (!pAddr)
            {
                return NULL;
            }
            return pAddr + psLinuxMemArea->uData.sSubAlloc.uiByteOffset;
        }
        default:
            return NULL;
    }
}


IMG_CPU_PHYADDR
LinuxMemAreaToCpuPAddr(LinuxMemArea *psLinuxMemArea, IMG_UINTPTR_T uiByteOffset)
{
    IMG_CPU_PHYADDR CpuPAddr;
    
    CpuPAddr.uiAddr = 0;

    switch (psLinuxMemArea->eAreaType)
    {
        case LINUX_MEM_AREA_IOREMAP:
        {
            CpuPAddr = psLinuxMemArea->uData.sIORemap.CPUPhysAddr;
            CpuPAddr.uiAddr += uiByteOffset;
            break;
        }
	case LINUX_MEM_AREA_EXTERNAL_KV:
	{
	    if (psLinuxMemArea->uData.sExternalKV.bPhysContig)
	    {
		CpuPAddr = SysSysPAddrToCpuPAddr(psLinuxMemArea->uData.sExternalKV.uPhysAddr.SysPhysAddr);
		CpuPAddr.uiAddr += uiByteOffset;
	    }
	    else
	    {
		IMG_UINTPTR_T uiPageIndex = PHYS_TO_PFN(uiByteOffset);
		IMG_SYS_PHYADDR SysPAddr = psLinuxMemArea->uData.sExternalKV.uPhysAddr.pSysPhysAddr[uiPageIndex];

		CpuPAddr = SysSysPAddrToCpuPAddr(SysPAddr);
                CpuPAddr.uiAddr += ADDR_TO_PAGE_OFFSET(uiByteOffset);
	    }
            break;
	}
        case LINUX_MEM_AREA_IO:
        {
            CpuPAddr = psLinuxMemArea->uData.sIO.CPUPhysAddr;
            CpuPAddr.uiAddr += uiByteOffset;
            break;
        }
        case LINUX_MEM_AREA_VMALLOC:
        {
            IMG_CHAR *pCpuVAddr;
            pCpuVAddr =
                (IMG_CHAR *)psLinuxMemArea->uData.sVmalloc.pvVmallocAddress;
            pCpuVAddr += uiByteOffset;
            CpuPAddr.uiAddr = VMallocToPhys(pCpuVAddr);
            break;
        }
        case LINUX_MEM_AREA_ION:
        {
            IMG_UINTPTR_T uiPageIndex = PHYS_TO_PFN(uiByteOffset);
            CpuPAddr = psLinuxMemArea->uData.sIONTilerAlloc.pCPUPhysAddrs[uiPageIndex];
            CpuPAddr.uiAddr += ADDR_TO_PAGE_OFFSET(uiByteOffset);
            break;
        }
        case LINUX_MEM_AREA_ALLOC_PAGES:
        {
            struct page *page;
            IMG_UINTPTR_T uiPageIndex = PHYS_TO_PFN(uiByteOffset);
            page = psLinuxMemArea->uData.sPageList.ppsPageList[uiPageIndex];
            CpuPAddr.uiAddr = page_to_phys(page);
            CpuPAddr.uiAddr += ADDR_TO_PAGE_OFFSET(uiByteOffset);
            break;
        }
        case LINUX_MEM_AREA_SUB_ALLOC:
        {
            CpuPAddr =
                OSMemHandleToCpuPAddr(psLinuxMemArea->uData.sSubAlloc.psParentLinuxMemArea,
                                      psLinuxMemArea->uData.sSubAlloc.uiByteOffset
                                        + uiByteOffset);
            break;
        }
        default:
        {
            PVR_DPF((PVR_DBG_ERROR, "%s: Unknown LinuxMemArea type (%d)\n",
                     __FUNCTION__, psLinuxMemArea->eAreaType));
            PVR_ASSERT(CpuPAddr.uiAddr);
           break;
        }
   }
    
    return CpuPAddr;
}


IMG_BOOL
LinuxMemAreaPhysIsContig(LinuxMemArea *psLinuxMemArea)
{
    switch (psLinuxMemArea->eAreaType)
    {
        case LINUX_MEM_AREA_IOREMAP:
        case LINUX_MEM_AREA_IO:
            return IMG_TRUE;

        case LINUX_MEM_AREA_EXTERNAL_KV:
            return psLinuxMemArea->uData.sExternalKV.bPhysContig;

        case LINUX_MEM_AREA_ION:
        case LINUX_MEM_AREA_VMALLOC:
        case LINUX_MEM_AREA_ALLOC_PAGES:
            return IMG_FALSE;

        case LINUX_MEM_AREA_SUB_ALLOC:
            /* PRQA S 3670 1 */ /* ignore recursive warning */
            return LinuxMemAreaPhysIsContig(psLinuxMemArea->uData.sSubAlloc.psParentLinuxMemArea);

        default:
            PVR_DPF((PVR_DBG_ERROR, "%s: Unknown LinuxMemArea type (%d)\n",
                     __FUNCTION__, psLinuxMemArea->eAreaType));
	    break;
    }
    return IMG_FALSE;
}


const IMG_CHAR *
LinuxMemAreaTypeToString(LINUX_MEM_AREA_TYPE eMemAreaType)
{
    /* Note we explicitly check the types instead of e.g.
     * using the type to index an array of strings so
     * we remain orthogonal to enum changes */
    switch (eMemAreaType)
    {
        case LINUX_MEM_AREA_IOREMAP:
            return "LINUX_MEM_AREA_IOREMAP";
	case LINUX_MEM_AREA_EXTERNAL_KV:
	    return "LINUX_MEM_AREA_EXTERNAL_KV";
        case LINUX_MEM_AREA_IO:
            return "LINUX_MEM_AREA_IO";
        case LINUX_MEM_AREA_VMALLOC:
            return "LINUX_MEM_AREA_VMALLOC";
        case LINUX_MEM_AREA_SUB_ALLOC:
            return "LINUX_MEM_AREA_SUB_ALLOC";
        case LINUX_MEM_AREA_ALLOC_PAGES:
            return "LINUX_MEM_AREA_ALLOC_PAGES";
        case LINUX_MEM_AREA_ION:
            return "LINUX_MEM_AREA_ION";
        default:
            PVR_ASSERT(0);
    }

    return "";
}


#if defined(DEBUG_LINUX_MEM_AREAS) || defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
static void ProcSeqStartstopDebugMutex(struct seq_file *sfile, IMG_BOOL start) 
{
	if (start) 
	{
	    LinuxLockMutex(&g_sDebugMutex);		
	}
	else
	{
	    LinuxUnLockMutex(&g_sDebugMutex);
	}
}
#endif /* defined(DEBUG_LINUX_MEM_AREAS) || defined(DEBUG_LINUX_MEMORY_ALLOCATIONS) */

#if defined(DEBUG_LINUX_MEM_AREAS)

static IMG_VOID* DecOffMemAreaRec_AnyVaCb(DEBUG_LINUX_MEM_AREA_REC *psNode, va_list va)
{
	off_t *pOff = va_arg(va, off_t*);
	if (--(*pOff))
	{
		return IMG_NULL;
	}
	else
	{
		return psNode;
	}
}

/* seq_file version of generating output, for reference check proc.c:CreateProcReadEntrySeq */ 
static void* ProcSeqNextMemArea(struct seq_file *sfile,void* el,loff_t off) 
{
    DEBUG_LINUX_MEM_AREA_REC *psRecord;
	psRecord = (DEBUG_LINUX_MEM_AREA_REC*)
				List_DEBUG_LINUX_MEM_AREA_REC_Any_va(g_LinuxMemAreaRecords,
													DecOffMemAreaRec_AnyVaCb,
													&off);
	return (void*)psRecord;
}

static void* ProcSeqOff2ElementMemArea(struct seq_file * sfile, loff_t off)
{
    DEBUG_LINUX_MEM_AREA_REC *psRecord;
	if (!off) 
	{
		return PVR_PROC_SEQ_START_TOKEN;
	}

	psRecord = (DEBUG_LINUX_MEM_AREA_REC*)
				List_DEBUG_LINUX_MEM_AREA_REC_Any_va(g_LinuxMemAreaRecords,
													DecOffMemAreaRec_AnyVaCb,
													&off);
	return (void*)psRecord;
}


static void ProcSeqShowMemArea(struct seq_file *sfile,void* el)
{
    DEBUG_LINUX_MEM_AREA_REC *psRecord = (DEBUG_LINUX_MEM_AREA_REC*)el; 
	if (el == PVR_PROC_SEQ_START_TOKEN) 
	{

#if !defined(DEBUG_LINUX_XML_PROC_FILES)
        seq_printf(sfile,
              			  "Number of Linux Memory Areas: %u\n"
                          "At the current water mark these areas correspond to %u bytes (excluding SUB areas)\n"
                          "At the highest water mark these areas corresponded to %u bytes (excluding SUB areas)\n"
                          "\nDetails for all Linux Memory Areas:\n"
                          "%s %-24s %s %s %-8s %-5s %s\n",
                          g_LinuxMemAreaCount,
                          g_LinuxMemAreaWaterMark,
                          g_LinuxMemAreaHighWaterMark,
                          "psLinuxMemArea",
                          "LinuxMemType",
                          "CpuVAddr",
                          "CpuPAddr",
                          "Bytes",
                          "Pid",
                          "Flags"
                        );
#else
        seq_printf(sfile,
                          "<mem_areas_header>\n"
                          "\t<count>%u</count>\n"
                          "\t<watermark key=\"mar0\" description=\"current\" bytes=\"%u\"/>\n" /* (excluding SUB areas) */
                          "\t<watermark key=\"mar1\" description=\"high\" bytes=\"%u\"/>\n" /* (excluding SUB areas) */
                          "</mem_areas_header>\n",
                          g_LinuxMemAreaCount,
                          g_LinuxMemAreaWaterMark,
                          g_LinuxMemAreaHighWaterMark
                        );
#endif
		return;
	}

        seq_printf(sfile,
#if !defined(DEBUG_LINUX_XML_PROC_FILES)
                       "%p       %-24s %p " CPUPADDR_FMT " %u %-5u %08x=(%s)\n",
#else
                       "<linux_mem_area>\n"
                       "\t<pointer>%p</pointer>\n"
                       "\t<type>%s</type>\n"
                       "\t<cpu_virtual>%p</cpu_virtual>\n"
                       "\t<cpu_physical>" CPUPADDR_FMT "</cpu_physical>\n"
                       "\t<bytes>%ld</bytes>\n"
                       "\t<pid>%u</pid>\n"
                       "\t<flags>%08x</flags>\n"
                       "\t<flags_string>%s</flags_string>\n"
                       "</linux_mem_area>\n",
#endif
                       psRecord->psLinuxMemArea,
                       LinuxMemAreaTypeToString(psRecord->psLinuxMemArea->eAreaType),
                       LinuxMemAreaToCpuVAddr(psRecord->psLinuxMemArea),
                       LinuxMemAreaToCpuPAddr(psRecord->psLinuxMemArea,0).uiAddr,
                       psRecord->psLinuxMemArea->uiByteSize,
                       psRecord->pid,
                       psRecord->ui32Flags,
                       HAPFlagsToString(psRecord->ui32Flags)
                     );

}

#endif /* DEBUG_LINUX_MEM_AREAS */


#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)

static IMG_VOID* DecOffMemAllocRec_AnyVaCb(DEBUG_MEM_ALLOC_REC *psNode, va_list va)
{
	off_t *pOff = va_arg(va, off_t*);
	if (--(*pOff))
	{
		return IMG_NULL;
	}
	else
	{
		return psNode;
	}
}


/* seq_file version of generating output, for reference check proc.c:CreateProcReadEntrySeq */ 
static void* ProcSeqNextMemoryRecords(struct seq_file *sfile,void* el,loff_t off) 
{
    DEBUG_MEM_ALLOC_REC *psRecord;
	psRecord = (DEBUG_MEM_ALLOC_REC*)
		List_DEBUG_MEM_ALLOC_REC_Any_va(g_MemoryRecords,
										DecOffMemAllocRec_AnyVaCb,
										&off);
#if defined(DEBUG_LINUX_XML_PROC_FILES)
	if (!psRecord) 
	{
		seq_printf(sfile, "</meminfo>\n");
	}
#endif

	return (void*)psRecord;
}

static void* ProcSeqOff2ElementMemoryRecords(struct seq_file *sfile, loff_t off)
{
    DEBUG_MEM_ALLOC_REC *psRecord;
	if (!off) 
	{
		return PVR_PROC_SEQ_START_TOKEN;
	}

	psRecord = (DEBUG_MEM_ALLOC_REC*)
		List_DEBUG_MEM_ALLOC_REC_Any_va(g_MemoryRecords,
										DecOffMemAllocRec_AnyVaCb,
										&off);

#if defined(DEBUG_LINUX_XML_PROC_FILES)
	if (!psRecord) 
	{
		seq_printf(sfile, "</meminfo>\n");
	}
#endif

	return (void*)psRecord;
}

static void ProcSeqShowMemoryRecords(struct seq_file *sfile,void* el)
{
    DEBUG_MEM_ALLOC_REC *psRecord = (DEBUG_MEM_ALLOC_REC*)el;
	if (el == PVR_PROC_SEQ_START_TOKEN) 
	{
#if !defined(DEBUG_LINUX_XML_PROC_FILES)
        /* NOTE: If you update this code, please also update the XML varient below
         * too! */

        seq_printf(sfile, "%-60s: %d bytes\n",
                           "Current Water Mark of bytes allocated via kmalloc",
                           g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_KMALLOC]);
        seq_printf(sfile, "%-60s: %d bytes\n",
                           "Highest Water Mark of bytes allocated via kmalloc",
                           g_HighWaterMarkData[DEBUG_MEM_ALLOC_TYPE_KMALLOC]);
        seq_printf(sfile, "%-60s: %d bytes\n",
                           "Current Water Mark of bytes allocated via vmalloc",
                           g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_VMALLOC]);
        seq_printf(sfile, "%-60s: %d bytes\n",
                           "Highest Water Mark of bytes allocated via vmalloc",
                           g_HighWaterMarkData[DEBUG_MEM_ALLOC_TYPE_VMALLOC]);
        seq_printf(sfile, "%-60s: %d bytes\n",
                           "Current Water Mark of bytes allocated via alloc_pages",
                           g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES]);
        seq_printf(sfile, "%-60s: %d bytes\n",
                           "Highest Water Mark of bytes allocated via alloc_pages",
                           g_HighWaterMarkData[DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES]);
        seq_printf(sfile, "%-60s: %d bytes\n",
                           "Current Water Mark of bytes allocated via ioremap",
                           g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_IOREMAP]);
        seq_printf(sfile, "%-60s: %d bytes\n",
                           "Highest Water Mark of bytes allocated via ioremap",
                           g_HighWaterMarkData[DEBUG_MEM_ALLOC_TYPE_IOREMAP]);
        seq_printf(sfile, "%-60s: %d bytes\n",
                           "Current Water Mark of bytes reserved for \"IO\" memory areas",
                           g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_IO]);
        seq_printf(sfile, "%-60s: %d bytes\n",
                           "Highest Water Mark of bytes allocated for \"IO\" memory areas",
                           g_HighWaterMarkData[DEBUG_MEM_ALLOC_TYPE_IO]);
        seq_printf(sfile, "%-60s: %d bytes\n",
                           "Current Water Mark of bytes allocated via kmem_cache_alloc",
                           g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE]);
        seq_printf(sfile, "%-60s: %d bytes\n",
                           "Highest Water Mark of bytes allocated via kmem_cache_alloc",
                           g_HighWaterMarkData[DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE]);
#if defined(PVR_LINUX_MEM_AREA_USE_VMAP)
        seq_printf(sfile, "%-60s: %d bytes\n",
                           "Current Water Mark of bytes mapped via vmap",
                           g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_VMAP]);
        seq_printf(sfile, "%-60s: %d bytes\n",
                           "Highest Water Mark of bytes mapped via vmap",
                           g_HighWaterMarkData[DEBUG_MEM_ALLOC_TYPE_VMAP]);
#endif
#if (PVR_LINUX_MEM_AREA_POOL_MAX_PAGES != 0)
        seq_printf(sfile, "%-60s: %d pages\n",
                           "Number of pages in page pool",
                           atomic_read(&g_sPagePoolEntryCount));
#endif
        seq_printf( sfile, "\n");
        seq_printf(sfile, "%-60s: %d bytes\n",
                           "The Current Water Mark for memory allocated from system RAM",
                           SysRAMTrueWaterMark());
        seq_printf(sfile, "%-60s: %d bytes\n",
                           "The Highest Water Mark for memory allocated from system RAM",
                           g_SysRAMHighWaterMark);
        seq_printf(sfile, "%-60s: %d bytes\n",
                           "The Current Water Mark for memory allocated from IO memory",
                           g_IOMemWaterMark);
        seq_printf(sfile, "%-60s: %d bytes\n",
                           "The Highest Water Mark for memory allocated from IO memory",
                           g_IOMemHighWaterMark);

        seq_printf( sfile, "\n");

		seq_printf(sfile, "Details for all known allocations:\n"
                           "%-16s %-8s %-8s %-10s %-5s %-10s %s\n",
                           "Type",
                           "CpuVAddr",
                           "CpuPAddr",
                           "Bytes",
                           "PID",
                           "PrivateData",
                           "Filename:Line");

#else /* DEBUG_LINUX_XML_PROC_FILES */
		
		/* Note: If you want to update the description property of a watermark
		 * ensure that the key property remains unchanged so that watermark data
		 * logged over time from different driver revisions may remain comparable
		 */
		seq_printf(sfile, "<meminfo>\n<meminfo_header>\n");
		seq_printf(sfile,
                           "<watermark key=\"mr0\" description=\"kmalloc_current\" bytes=\"%d\"/>\n",
                           g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_KMALLOC]);
		seq_printf(sfile,
                           "<watermark key=\"mr1\" description=\"kmalloc_high\" bytes=\"%d\"/>\n",
                           g_HighWaterMarkData[DEBUG_MEM_ALLOC_TYPE_KMALLOC]);
		seq_printf(sfile,
                           "<watermark key=\"mr2\" description=\"vmalloc_current\" bytes=\"%d\"/>\n",
                           g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_VMALLOC]);
		seq_printf(sfile,
                           "<watermark key=\"mr3\" description=\"vmalloc_high\" bytes=\"%d\"/>\n",
                           g_HighWaterMarkData[DEBUG_MEM_ALLOC_TYPE_VMALLOC]);
		seq_printf(sfile,
                           "<watermark key=\"mr4\" description=\"alloc_pages_current\" bytes=\"%d\"/>\n",
                           g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES]);
		seq_printf(sfile,
                           "<watermark key=\"mr5\" description=\"alloc_pages_high\" bytes=\"%d\"/>\n",
                           g_HighWaterMarkData[DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES]);
		seq_printf(sfile,
                           "<watermark key=\"mr6\" description=\"ioremap_current\" bytes=\"%d\"/>\n",
                           g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_IOREMAP]);
		seq_printf(sfile,
                           "<watermark key=\"mr7\" description=\"ioremap_high\" bytes=\"%d\"/>\n",
                           g_HighWaterMarkData[DEBUG_MEM_ALLOC_TYPE_IOREMAP]);
		seq_printf(sfile,
                           "<watermark key=\"mr8\" description=\"io_current\" bytes=\"%d\"/>\n",
                           g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_IO]);
		seq_printf(sfile,
                           "<watermark key=\"mr9\" description=\"io_high\" bytes=\"%d\"/>\n",
                           g_HighWaterMarkData[DEBUG_MEM_ALLOC_TYPE_IO]);
		seq_printf(sfile,
                           "<watermark key=\"mr10\" description=\"kmem_cache_current\" bytes=\"%d\"/>\n",
                           g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE]);
		seq_printf(sfile,
                           "<watermark key=\"mr11\" description=\"kmem_cache_high\" bytes=\"%d\"/>\n",
                           g_HighWaterMarkData[DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE]);
#if defined(PVR_LINUX_MEM_AREA_USE_VMAP)
		seq_printf(sfile,
                           "<watermark key=\"mr12\" description=\"vmap_current\" bytes=\"%d\"/>\n",
                           g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_VMAP]);
		seq_printf(sfile,
                           "<watermark key=\"mr13\" description=\"vmap_high\" bytes=\"%d\"/>\n",
                           g_HighWaterMarkData[DEBUG_MEM_ALLOC_TYPE_VMAP]);
#endif
		seq_printf(sfile,
                           "<watermark key=\"mr14\" description=\"system_ram_current\" bytes=\"%d\"/>\n",
                           SysRAMTrueWaterMark());
		seq_printf(sfile,
                           "<watermark key=\"mr15\" description=\"system_ram_high\" bytes=\"%d\"/>\n",
                           g_SysRAMHighWaterMark);
		seq_printf(sfile,
                           "<watermark key=\"mr16\" description=\"system_io_current\" bytes=\"%d\"/>\n",
                           g_IOMemWaterMark);
		seq_printf(sfile,
                           "<watermark key=\"mr17\" description=\"system_io_high\" bytes=\"%d\"/>\n",
                           g_IOMemHighWaterMark);

#if (PVR_LINUX_MEM_AREA_POOL_MAX_PAGES != 0)
		seq_printf(sfile,
                           "<watermark key=\"mr18\" description=\"page_pool_current\" bytes=\"%d\"/>\n",
                           PAGES_TO_BYTES(atomic_read(&g_sPagePoolEntryCount)));
#endif
		seq_printf(sfile, "</meminfo_header>\n");

#endif /* DEBUG_LINUX_XML_PROC_FILES */
		return;
	}

    if (psRecord->eAllocType != DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE)
    {
		seq_printf(sfile,
#if !defined(DEBUG_LINUX_XML_PROC_FILES)
                           "%-16s %p " CPUPADDR_FMT " %u %-5d %-10s %s:%d\n",
#else
                           "<allocation>\n"
                           "\t<type>%s</type>\n"
                           "\t<cpu_virtual>%p</cpu_virtual>\n"
                           "\t<cpu_physical>" CPUPADDR_FMT "</cpu_physical>\n"
                           "\t<bytes>%u</bytes>\n"
                           "\t<pid>%d</pid>\n"
                           "\t<private>%s</private>\n"
                           "\t<filename>%s</filename>\n"
                           "\t<line>%d</line>\n"
                           "</allocation>\n",
#endif
                           DebugMemAllocRecordTypeToString(psRecord->eAllocType),
                           psRecord->pvCpuVAddr,
                           psRecord->sCpuPAddr.uiAddr,
                           psRecord->uiBytes,
                           psRecord->pid,
                           "NULL",
                           psRecord->pszFileName,
                           psRecord->ui32Line);
    }
    else
    {
		seq_printf(sfile,
#if !defined(DEBUG_LINUX_XML_PROC_FILES)
                           "%-16s %p " CPUPADDR_FMT " %u %-5d %-10s %s:%d\n",
#else
                           "<allocation>\n"
                           "\t<type>%s</type>\n"
                           "\t<cpu_virtual>%p</cpu_virtual>\n"
                           "\t<cpu_physical>" CPUPADDR_FMT "</cpu_physical>\n"
                           "\t<bytes>%u</bytes>\n"
                           "\t<pid>%d</pid>\n"
                           "\t<private>%s</private>\n"
                           "\t<filename>%s</filename>\n"
                           "\t<line>%d</line>\n"
                           "</allocation>\n",
#endif
                           DebugMemAllocRecordTypeToString(psRecord->eAllocType),
                           psRecord->pvCpuVAddr,
                           psRecord->sCpuPAddr.uiAddr,
                           psRecord->uiBytes,
                           psRecord->pid,
                           KMemCacheNameWrapper(psRecord->pvPrivateData),
                           psRecord->pszFileName,
                           psRecord->ui32Line);
    }
}

#endif /*  defined(DEBUG_LINUX_MEMORY_ALLOCATIONS) */


#if defined(DEBUG_LINUX_MEM_AREAS) || defined(DEBUG_LINUX_MMAP_AREAS)
/* This could be moved somewhere more general */
const IMG_CHAR *
HAPFlagsToString(IMG_UINT32 ui32Flags)
{
    static IMG_CHAR szFlags[50];
    IMG_INT32 i32Pos = 0;
    IMG_UINT32 ui32CacheTypeIndex, ui32MapTypeIndex;
    IMG_CHAR *apszCacheTypes[] = {
        "UNCACHED",
        "CACHED",
        "WRITECOMBINE",
        "UNKNOWN"
    };
    IMG_CHAR *apszMapType[] = {
        "KERNEL_ONLY",
        "SINGLE_PROCESS",
        "MULTI_PROCESS",
        "FROM_EXISTING_PROCESS",
        "NO_CPU_VIRTUAL",
        "UNKNOWN"
    };
    
    /* FIXME create an enum for the cache type that we can
     * cast and select so we get compiler warnings when
     * when this code isn't complete due to new flags */
    if (ui32Flags & PVRSRV_HAP_UNCACHED) {
        ui32CacheTypeIndex = 0;
    } else if (ui32Flags & PVRSRV_HAP_CACHED) {
        ui32CacheTypeIndex = 1;
    } else if (ui32Flags & PVRSRV_HAP_WRITECOMBINE) {
        ui32CacheTypeIndex = 2;
    } else {
        ui32CacheTypeIndex = 3;
        PVR_DPF((PVR_DBG_ERROR, "%s: unknown cache type (%u)",
                 __FUNCTION__, (ui32Flags & PVRSRV_HAP_CACHETYPE_MASK)));
    }

    /* FIXME create an enum for the map type that we can
     * cast and select so we get compiler warnings when
     * when this code isn't complete due to new flags */
    if (ui32Flags & PVRSRV_HAP_KERNEL_ONLY) {
        ui32MapTypeIndex = 0;
    } else if (ui32Flags & PVRSRV_HAP_SINGLE_PROCESS) {
        ui32MapTypeIndex = 1;
    } else if (ui32Flags & PVRSRV_HAP_MULTI_PROCESS) {
        ui32MapTypeIndex = 2;
    } else if (ui32Flags & PVRSRV_HAP_FROM_EXISTING_PROCESS) {
        ui32MapTypeIndex = 3;
    } else if (ui32Flags & PVRSRV_HAP_NO_CPU_VIRTUAL) {
        ui32MapTypeIndex = 4;
    } else {
        ui32MapTypeIndex = 5;
        PVR_DPF((PVR_DBG_ERROR, "%s: unknown map type (%u)",
                 __FUNCTION__, (ui32Flags & PVRSRV_HAP_MAPTYPE_MASK)));
    }

    i32Pos = sprintf(szFlags, "%s|", apszCacheTypes[ui32CacheTypeIndex]);
    if (i32Pos <= 0)
    {
	PVR_DPF((PVR_DBG_ERROR, "%s: sprintf for cache type %u failed (%d)",
		__FUNCTION__, ui32CacheTypeIndex, i32Pos));
	szFlags[0] = 0;
    }
    else
    {
        sprintf(szFlags + i32Pos, "%s", apszMapType[ui32MapTypeIndex]);
    }

    return szFlags;
}
#endif

#if defined(DEBUG_LINUX_MEM_AREAS)
static IMG_VOID LinuxMMCleanup_MemAreas_ForEachCb(DEBUG_LINUX_MEM_AREA_REC *psCurrentRecord)
{
	LinuxMemArea *psLinuxMemArea;

	psLinuxMemArea = psCurrentRecord->psLinuxMemArea;
	PVR_DPF((PVR_DBG_ERROR, "%s: BUG!: Cleaning up Linux memory area (%p), type=%s, size=%d bytes",
				__FUNCTION__,
				psCurrentRecord->psLinuxMemArea,
				LinuxMemAreaTypeToString(psCurrentRecord->psLinuxMemArea->eAreaType),
				psCurrentRecord->psLinuxMemArea->uiByteSize));
	/* Note this will also remove psCurrentRecord from g_LinuxMemAreaRecords
	 * but that's ok since we have already got a pointer to the next area. */
	LinuxMemAreaDeepFree(psLinuxMemArea);
}
#endif

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
static IMG_VOID LinuxMMCleanup_MemRecords_ForEachVa(DEBUG_MEM_ALLOC_REC *psCurrentRecord)

{
	
/* It's a bug if anything remains allocated at this point. We
 * report an error, and simply brute force free anything we find. */
	PVR_DPF((PVR_DBG_ERROR, "%s: BUG!: Cleaning up memory: "
							"type=%s "
							"CpuVAddr=%p "
							"CpuPAddr=0x" CPUPADDR_FMT ", "
							"allocated @ file=%s,line=%d",
			__FUNCTION__,
			DebugMemAllocRecordTypeToString(psCurrentRecord->eAllocType),
			psCurrentRecord->pvCpuVAddr,
			psCurrentRecord->sCpuPAddr.uiAddr,
			psCurrentRecord->pszFileName,
			psCurrentRecord->ui32Line));
	switch (psCurrentRecord->eAllocType)
	{
		case DEBUG_MEM_ALLOC_TYPE_KMALLOC:
			KFreeWrapper(psCurrentRecord->pvCpuVAddr);
			break;
		case DEBUG_MEM_ALLOC_TYPE_IOREMAP:
			IOUnmapWrapper(psCurrentRecord->pvCpuVAddr);
			break;
		case DEBUG_MEM_ALLOC_TYPE_IO:
			/* Nothing needed except to free the record */
			DebugMemAllocRecordRemove(DEBUG_MEM_ALLOC_TYPE_IO, psCurrentRecord->uiKey, __FILE__, __LINE__);
			break;
		case DEBUG_MEM_ALLOC_TYPE_VMALLOC:
			VFreeWrapper(psCurrentRecord->pvCpuVAddr);
			break;
		case DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES:
			DebugMemAllocRecordRemove(DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES, psCurrentRecord->uiKey, __FILE__, __LINE__);
			break;
		case DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE:
			KMemCacheFreeWrapper(psCurrentRecord->pvPrivateData, psCurrentRecord->pvCpuVAddr);
			break;
#if defined(PVR_LINUX_MEM_AREA_USE_VMAP)
		case DEBUG_MEM_ALLOC_TYPE_VMAP:
			VUnmapWrapper(psCurrentRecord->pvCpuVAddr);
			break;
#endif
		default:
			PVR_ASSERT(0);
	}
}
#endif


#if defined(PVR_LINUX_MEM_AREA_POOL_ALLOW_SHRINK)
static struct shrinker g_sShrinker =
{
	.shrink = ShrinkPagePool,
	.seeks = DEFAULT_SEEKS
};

static IMG_BOOL g_bShrinkerRegistered;
#endif

IMG_VOID
LinuxMMCleanup(IMG_VOID)
{
#if defined(DEBUG_LINUX_MEM_AREAS)
    {
        if (g_LinuxMemAreaCount)
        {
            PVR_DPF((PVR_DBG_ERROR, "%s: BUG!: There are %d LinuxMemArea allocation unfreed (%d bytes)",
                    __FUNCTION__, g_LinuxMemAreaCount, g_LinuxMemAreaWaterMark));
        }
		
	List_DEBUG_LINUX_MEM_AREA_REC_ForEach(g_LinuxMemAreaRecords, LinuxMMCleanup_MemAreas_ForEachCb);

	if (g_SeqFileMemArea)
	{
	    RemoveProcEntrySeq(g_SeqFileMemArea);
	}
    }
#endif

#if defined(PVR_LINUX_MEM_AREA_POOL_ALLOW_SHRINK)
	if (g_bShrinkerRegistered)
	{
		unregister_shrinker(&g_sShrinker);
	}
#endif

    /*
     * The page pool must be freed after any remaining mem areas, but before
     * the remaining memory resources.
     */
    FreePagePool();

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
    {
        
        /*
	 * It's a bug if anything remains allocated at this point. We
         * report an error, and simply brute force free anything we find.
	 */
		List_DEBUG_MEM_ALLOC_REC_ForEach(g_MemoryRecords, LinuxMMCleanup_MemRecords_ForEachVa);

		if (g_SeqFileMemoryRecords)
		{
			RemoveProcEntrySeq(g_SeqFileMemoryRecords);
		}
    }
#endif

    if (g_PsLinuxMemAreaCache)
    {
        KMemCacheDestroyWrapper(g_PsLinuxMemAreaCache); 
    }

    if (g_PsLinuxPagePoolCache)
    {
        KMemCacheDestroyWrapper(g_PsLinuxPagePoolCache); 
    }
}

PVRSRV_ERROR
LinuxMMInit(IMG_VOID)
{
#if defined(DEBUG_LINUX_MEM_AREAS) || defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	LinuxInitMutex(&g_sDebugMutex);
#endif

#if defined(DEBUG_LINUX_MEM_AREAS)
    {
		g_SeqFileMemArea = CreateProcReadEntrySeq(
									"mem_areas", 
									NULL, 
									ProcSeqNextMemArea,
									ProcSeqShowMemArea,
									ProcSeqOff2ElementMemArea,
									ProcSeqStartstopDebugMutex
								  );
		if (!g_SeqFileMemArea)
		{
		    goto failed;
		}
    }
#endif


#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
    {
		g_SeqFileMemoryRecords = CreateProcReadEntrySeq(
									"meminfo", 
									NULL, 
									ProcSeqNextMemoryRecords,
									ProcSeqShowMemoryRecords, 
									ProcSeqOff2ElementMemoryRecords,
									ProcSeqStartstopDebugMutex
								  );
        	if (!g_SeqFileMemoryRecords)
		{
		    goto failed;
		}
    }
#endif

    g_PsLinuxMemAreaCache = KMemCacheCreateWrapper("img-mm", sizeof(LinuxMemArea), 0, 0);
    if (!g_PsLinuxMemAreaCache)
    {
        PVR_DPF((PVR_DBG_ERROR,"%s: failed to allocate mem area kmem_cache", __FUNCTION__));
        goto failed;
    }

#if (PVR_LINUX_MEM_AREA_POOL_MAX_PAGES != 0)
    g_iPagePoolMaxEntries = PVR_LINUX_MEM_AREA_POOL_MAX_PAGES;
    if (g_iPagePoolMaxEntries <= 0 || g_iPagePoolMaxEntries > INT_MAX/2)
    {
	g_iPagePoolMaxEntries = INT_MAX/2;
	PVR_TRACE(("%s: No limit set for page pool size", __FUNCTION__));
    }
    else
    {
	PVR_TRACE(("%s: Maximum page pool size: %d", __FUNCTION__, g_iPagePoolMaxEntries));
    }

    g_PsLinuxPagePoolCache = KMemCacheCreateWrapper("img-mm-pool", sizeof(LinuxPagePoolEntry), 0, 0);
    if (!g_PsLinuxPagePoolCache)
    {
        PVR_DPF((PVR_DBG_ERROR,"%s: failed to allocate page pool kmem_cache", __FUNCTION__));
        goto failed;
    }
#endif

#if defined(PVR_LINUX_MEM_AREA_POOL_ALLOW_SHRINK)
	register_shrinker(&g_sShrinker);
	g_bShrinkerRegistered = IMG_TRUE;
#endif

    return PVRSRV_OK;

failed:
    LinuxMMCleanup();
    return PVRSRV_ERROR_OUT_OF_MEMORY;
}

