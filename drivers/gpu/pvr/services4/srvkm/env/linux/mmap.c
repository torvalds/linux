/*************************************************************************/ /*!
@Title          Linux mmap interface
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

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0))
#include <linux/wrapper.h>
#endif
#include <linux/slab.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26))
#include <linux/highmem.h>
#endif
#include <asm/io.h>
#include <asm/page.h>
#include <asm/shmparam.h>
#include <asm/pgtable.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22))
#include <linux/sched.h>
#include <asm/current.h>
#endif
#if defined(SUPPORT_DRI_DRM)
#include <drm/drmP.h>
#endif

#ifdef CONFIG_ARCH_OMAP5
#ifdef CONFIG_DSSCOMP
#include <../drivers/staging/omapdrm/omap_dmm_tiler.h>
#endif
#endif

#include "services_headers.h"

#include "pvrmmap.h"
#include "mutils.h"
#include "mmap.h"
#include "mm.h"
#include "proc.h"
#include "mutex.h"
#include "handle.h"
#include "perproc.h"
#include "env_perproc.h"
#include "bridged_support.h"
#if defined(SUPPORT_DRI_DRM)
#include "pvr_drm.h"
#endif

#if !defined(PVR_SECURE_HANDLES)
#error "The mmap code requires PVR_SECURE_HANDLES"
#endif

#define USING_REMAP_PFN_RANGE

/* WARNING:
 * The mmap code has its own mutex, to prevent a possible deadlock,
 * when using gPVRSRVLock.
 * The Linux kernel takes the mm->mmap_sem before calling the mmap
 * entry points (PVRMMap, MMapVOpen, MMapVClose), but the ioctl
 * entry point may take mm->mmap_sem during fault handling, or 
 * before calling get_user_pages.  If gPVRSRVLock was used in the
 * mmap entry points, a deadlock could result, due to the ioctl
 * and mmap code taking the two locks in different orders.
 * As a corollary to this, the mmap entry points must not call
 * any driver code that relies on gPVRSRVLock is held.
 */
PVRSRV_LINUX_MUTEX g_sMMapMutex;

static LinuxKMemCache *g_psMemmapCache = NULL;
static LIST_HEAD(g_sMMapAreaList);
static LIST_HEAD(g_sMMapOffsetStructList);
#if defined(DEBUG_LINUX_MMAP_AREAS)
static IMG_UINT32 g_ui32RegisteredAreas = 0;
static IMG_SIZE_T g_uiTotalByteSize = 0;
#endif


#if defined(DEBUG_LINUX_MMAP_AREAS)
static struct proc_dir_entry *g_ProcMMap;
#endif /* defined(DEBUG_LINUX_MMAP_AREAS) */

#if !defined(PVR_MAKE_ALL_PFNS_SPECIAL)
/*
 * Now that we are using mmap2 in srvclient, almost (*) the full 32
 * bit offset is available.  The range of values is divided into two.
 * The first part of the range, from FIRST_PHYSICAL_PFN to
 * LAST_PHYSICAL_PFN, is for raw page mappings (VM_PFNMAP).  The
 * resulting 43 bit (*) physical address range should be enough for
 * the current range of processors we support.
 *
 * NB: (*) -- the above figures assume 4KB page size.  The offset
 * argument to mmap2() is in units of 4,096 bytes regardless of page
 * size.  Thus, we lose (PAGE_SHIFT-12) bits of resolution on other
 * architectures.
 *
 * The second part of the range, from FIRST_SPECIAL_PFN to LAST_SPECIAL_PFN,
 * is used for all other mappings.  These other mappings will always
 * consist of pages with associated page structures, and need not
 * represent a contiguous range of physical addresses.
 *
 */
#define MMAP2_PGOFF_RESOLUTION (32-PAGE_SHIFT+12)
#define RESERVED_PGOFF_BITS 1
#define	MAX_MMAP_HANDLE		((1UL<<(MMAP2_PGOFF_RESOLUTION-RESERVED_PGOFF_BITS))-1)

#define	FIRST_PHYSICAL_PFN	0
#define	LAST_PHYSICAL_PFN	(FIRST_PHYSICAL_PFN + MAX_MMAP_HANDLE)
#define	FIRST_SPECIAL_PFN	(LAST_PHYSICAL_PFN + 1)
#define	LAST_SPECIAL_PFN	(FIRST_SPECIAL_PFN + MAX_MMAP_HANDLE)

#else	/* !defined(PVR_MAKE_ALL_PFNS_SPECIAL) */

#if PAGE_SHIFT != 12
#error This build variant has not yet been made non-4KB page-size aware
#endif

/*
 * Since we no longer have to worry about clashes with the mmap
 * offsets used for pure PFN mappings (VM_PFNMAP), there is greater
 * freedom in choosing the mmap handles.  This is useful if the
 * mmap offset space has to be shared with another driver component.
 */

#if defined(PVR_MMAP_OFFSET_BASE)
#define	FIRST_SPECIAL_PFN 	PVR_MMAP_OFFSET_BASE
#else
#define	FIRST_SPECIAL_PFN	0x80000000UL
#endif

#if defined(PVR_NUM_MMAP_HANDLES)
#define	MAX_MMAP_HANDLE		PVR_NUM_MMAP_HANDLES
#else
#define	MAX_MMAP_HANDLE		0x7fffffffUL
#endif

#endif	/* !defined(PVR_MAKE_ALL_PFNS_SPECIAL) */

#if !defined(PVR_MAKE_ALL_PFNS_SPECIAL)
static inline IMG_BOOL
PFNIsPhysical(IMG_UINT32 pfn)
{
	/* Unsigned, no need to compare >=0 */
	return (/*(pfn >= FIRST_PHYSICAL_PFN) &&*/ (pfn <= LAST_PHYSICAL_PFN)) ? IMG_TRUE : IMG_FALSE;
}

static inline IMG_BOOL
PFNIsSpecial(IMG_UINT32 pfn)
{
	/* Unsigned, no need to compare <=MAX_UINT */
	return ((pfn >= FIRST_SPECIAL_PFN) /*&& (pfn <= LAST_SPECIAL_PFN)*/) ? IMG_TRUE : IMG_FALSE;
}
#endif

#if !defined(PVR_MAKE_ALL_PFNS_SPECIAL)
static inline IMG_HANDLE
MMapOffsetToHandle(IMG_UINT32 pfn)
{
	if (PFNIsPhysical(pfn))
	{
		PVR_ASSERT(PFNIsPhysical(pfn));
		return IMG_NULL;
	}
	return (IMG_HANDLE)(pfn - FIRST_SPECIAL_PFN);
}
#endif

static inline IMG_UINTPTR_T
HandleToMMapOffset(IMG_HANDLE hHandle)
{
	IMG_UINTPTR_T ulHandle = (IMG_UINTPTR_T)hHandle;

#if !defined(PVR_MAKE_ALL_PFNS_SPECIAL)
	if (PFNIsSpecial(ulHandle))
	{
		PVR_ASSERT(PFNIsSpecial(ulHandle));
		return 0;
	}
#endif
	return ulHandle + FIRST_SPECIAL_PFN;
}

#if !defined(PVR_MAKE_ALL_PFNS_SPECIAL)
/*
 * Determine whether physical or special mappings will be used for
 * a given memory area.  At present, this decision is made on
 * whether the mapping represents a contiguous range of physical
 * addresses, which is a requirement for raw page mappings (VM_PFNMAP).
 * In the VMA structure for such a mapping, vm_pgoff is the PFN
 * (page frame number, the physical address divided by the page size)
 * of the first page in the VMA.  The second page is assumed to have
 * PFN (vm_pgoff + 1), the third (vm_pgoff + 2) and so on.
 */
static inline IMG_BOOL
LinuxMemAreaUsesPhysicalMap(LinuxMemArea *psLinuxMemArea)
{
    return LinuxMemAreaPhysIsContig(psLinuxMemArea);
}
#endif

#if !defined(PVR_MAKE_ALL_PFNS_SPECIAL)
static inline IMG_UINT32
GetCurrentThreadID(IMG_VOID)
{
	/*
 	 * The PID is the thread ID, as each thread is a
 	 * seperate process.
 	 */
	return (IMG_UINT32)current->pid;
}
#endif

/*
 * Create an offset structure, which is used to hold per-process
 * mmap data.
 */
static PKV_OFFSET_STRUCT
CreateOffsetStruct(LinuxMemArea *psLinuxMemArea, IMG_UINTPTR_T uiOffset, IMG_SIZE_T uiRealByteSize)
{
    PKV_OFFSET_STRUCT psOffsetStruct;
#if defined(DEBUG) || defined(DEBUG_LINUX_MMAP_AREAS)
    const IMG_CHAR *pszName = LinuxMemAreaTypeToString(LinuxMemAreaRootType(psLinuxMemArea));
#endif

#if defined(DEBUG) || defined(DEBUG_LINUX_MMAP_AREAS)
    PVR_DPF((PVR_DBG_MESSAGE,
             "%s(%s, psLinuxMemArea: 0x%p, ui32AllocFlags: 0x%8x)",
             __FUNCTION__, pszName, psLinuxMemArea, psLinuxMemArea->ui32AreaFlags));
#endif

    PVR_ASSERT(psLinuxMemArea->eAreaType != LINUX_MEM_AREA_SUB_ALLOC || LinuxMemAreaRoot(psLinuxMemArea)->eAreaType != LINUX_MEM_AREA_SUB_ALLOC);

    PVR_ASSERT(psLinuxMemArea->bMMapRegistered);

    psOffsetStruct = KMemCacheAllocWrapper(g_psMemmapCache, GFP_KERNEL);
    if(psOffsetStruct == IMG_NULL)
    {
        PVR_DPF((PVR_DBG_ERROR,"PVRMMapRegisterArea: Couldn't alloc another mapping record from cache"));
        return IMG_NULL;
    }
    
    psOffsetStruct->uiMMapOffset = uiOffset;

    psOffsetStruct->psLinuxMemArea = psLinuxMemArea;

    psOffsetStruct->uiRealByteSize = uiRealByteSize;

    /*
     * We store the TID in case two threads within a process
     * generate the same offset structure, and both end up on the
     * list of structures waiting to be mapped, at the same time.
     * This could happen if two sub areas within the same page are
     * being mapped at the same time.
     * The TID allows the mmap entry point to distinguish which
     * mapping is being done by which thread.
     */
#if !defined(PVR_MAKE_ALL_PFNS_SPECIAL)
    psOffsetStruct->ui32TID = GetCurrentThreadID();
#endif
    psOffsetStruct->ui32PID = OSGetCurrentProcessIDKM();

#if defined(DEBUG_LINUX_MMAP_AREAS)
    /* Extra entries to support proc filesystem debug info */
    psOffsetStruct->pszName = pszName;
#endif

    list_add_tail(&psOffsetStruct->sAreaItem, &psLinuxMemArea->sMMapOffsetStructList);

    return psOffsetStruct;
}


static IMG_VOID
DestroyOffsetStruct(PKV_OFFSET_STRUCT psOffsetStruct)
{
#ifdef DEBUG
    IMG_CPU_PHYADDR CpuPAddr;
    CpuPAddr = LinuxMemAreaToCpuPAddr(psOffsetStruct->psLinuxMemArea, 0);
#endif

    list_del(&psOffsetStruct->sAreaItem);

    if (psOffsetStruct->bOnMMapList)
    {
        list_del(&psOffsetStruct->sMMapItem);
    }

#ifdef DEBUG
    PVR_DPF((PVR_DBG_MESSAGE, "%s: Table entry: "
             "psLinuxMemArea=%p, CpuPAddr=0x" CPUPADDR_FMT, 
			 __FUNCTION__,
             psOffsetStruct->psLinuxMemArea,
             CpuPAddr.uiAddr));
#endif
    
    KMemCacheFreeWrapper(g_psMemmapCache, psOffsetStruct);
}


/*
 * There are no alignment constraints for mapping requests made by user
 * mode Services.  For this, and potentially other reasons, the
 * mapping created for a users request may look different to the
 * original request in terms of size and alignment.
 *
 * This function determines an offset that the user can add to the mapping
 * that is _actually_ created which will point to the memory they are
 * _really_ interested in.
 *
 */
static inline IMG_VOID
DetermineUsersSizeAndByteOffset(LinuxMemArea *psLinuxMemArea,
                               IMG_SIZE_T *puiRealByteSize,
                               IMG_UINTPTR_T *puiByteOffset)
{
    IMG_UINTPTR_T uiPageAlignmentOffset;
    IMG_CPU_PHYADDR CpuPAddr;
    
    CpuPAddr = LinuxMemAreaToCpuPAddr(psLinuxMemArea, 0);
    uiPageAlignmentOffset = ADDR_TO_PAGE_OFFSET(CpuPAddr.uiAddr);
    
    *puiByteOffset = uiPageAlignmentOffset;

    *puiRealByteSize = PAGE_ALIGN(psLinuxMemArea->uiByteSize + uiPageAlignmentOffset);
}


/*!
 *******************************************************************************

 @Function  PVRMMapOSMemHandleToMMapData

 @Description

 Determine various parameters needed to mmap a memory area, and to
 locate the memory within the mapped area.

 @input psPerProc : Per-process data.
 @input hMHandle : Memory handle.
 @input puiMMapOffset : pointer to location for returned mmap offset.
 @input puiByteOffset : pointer to location for returned byte offset.
 @input puiRealByteSize : pointer to location for returned real byte size.
 @input puiUserVaddr : pointer to location for returned user mode address.

 @output puiMMapOffset : points to mmap offset to be used in mmap2 sys call.
 @output puiByteOffset : points to byte offset of start of memory
			   within mapped area returned by mmap2.
 @output puiRealByteSize : points to size of area to be mapped.
 @output puiUserVAddr : points to user mode address of start of
			  mapping, or 0 if it hasn't been mapped yet.

 @Return PVRSRV_ERROR : PVRSRV_OK, or error code.

 ******************************************************************************/
PVRSRV_ERROR
PVRMMapOSMemHandleToMMapData(PVRSRV_PER_PROCESS_DATA *psPerProc,
                             IMG_HANDLE hMHandle,
                             IMG_UINTPTR_T *puiMMapOffset,
                             IMG_UINTPTR_T *puiByteOffset,
                             IMG_SIZE_T *puiRealByteSize,
                             IMG_UINTPTR_T *puiUserVAddr)
{
    LinuxMemArea *psLinuxMemArea;
    PKV_OFFSET_STRUCT psOffsetStruct;
    IMG_HANDLE hOSMemHandle;
    PVRSRV_ERROR eError;

    LinuxLockMutex(&g_sMMapMutex);

    PVR_ASSERT(PVRSRVGetMaxHandle(psPerProc->psHandleBase) <= MAX_MMAP_HANDLE);

    eError = PVRSRVLookupOSMemHandle(psPerProc->psHandleBase, &hOSMemHandle, hMHandle);
    if (eError != PVRSRV_OK)
    {
        PVR_DPF((PVR_DBG_ERROR, "%s: Lookup of handle %p failed", __FUNCTION__, hMHandle));

        goto exit_unlock;
    }

    psLinuxMemArea = (LinuxMemArea *)hOSMemHandle;

	/* Sparse mappings have to ask the BM for the virtual size */
	if (psLinuxMemArea->hBMHandle)
	{
		*puiRealByteSize = BM_GetVirtualSize(psLinuxMemArea->hBMHandle);
		*puiByteOffset = 0;
	}
	else
	{
		DetermineUsersSizeAndByteOffset(psLinuxMemArea,
										puiRealByteSize,
										puiByteOffset);
	}

    /* Check whether this memory area has already been mapped */
    list_for_each_entry(psOffsetStruct, &psLinuxMemArea->sMMapOffsetStructList, sAreaItem)
    {
        if (psPerProc->ui32PID == psOffsetStruct->ui32PID)
        {
			if (!psLinuxMemArea->hBMHandle)
			{
				PVR_ASSERT(*puiRealByteSize == psOffsetStruct->uiRealByteSize);
			}
	   /*
	    * User mode locking is required to stop two threads racing to
	    * map the same memory area.  The lock should prevent a
	    * second thread retrieving mmap data for a given handle,
	    * before the first thread has done the mmap.
	    * Without locking, both threads may attempt the mmap,
	    * and one of them will fail.
	    */
	   *puiMMapOffset = psOffsetStruct->uiMMapOffset;
	   *puiUserVAddr = psOffsetStruct->uiUserVAddr;
	   PVRSRVOffsetStructIncRef(psOffsetStruct);

	   eError = PVRSRV_OK;
	   goto exit_unlock;
        }
    }

    /* Memory area won't have been mapped yet */
    *puiUserVAddr = 0;

#if !defined(PVR_MAKE_ALL_PFNS_SPECIAL)
    if (LinuxMemAreaUsesPhysicalMap(psLinuxMemArea))
    {
        *puiMMapOffset = LinuxMemAreaToCpuPFN(psLinuxMemArea, 0);
        PVR_ASSERT(PFNIsPhysical(*puiMMapOffset));
    }
    else
#endif
    {
        *puiMMapOffset = HandleToMMapOffset(hMHandle);
#if !defined(PVR_MAKE_ALL_PFNS_SPECIAL)
        PVR_ASSERT(PFNIsSpecial(*puiMMapOffset));
#endif
    }

    psOffsetStruct = CreateOffsetStruct(psLinuxMemArea, *puiMMapOffset, *puiRealByteSize);
    if (psOffsetStruct == IMG_NULL)
    {
        eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	goto exit_unlock;
    }

    /*
    * Offset structures representing physical mappings are added to
    * a list, so that they can be located when the memory area is mapped.
    */
    list_add_tail(&psOffsetStruct->sMMapItem, &g_sMMapOffsetStructList);

    psOffsetStruct->bOnMMapList = IMG_TRUE;

    PVRSRVOffsetStructIncRef(psOffsetStruct);

    eError = PVRSRV_OK;

	/* Need to scale up the offset to counter the shifting that
	   is done in the mmap2() syscall, as it expects the pgoff
	   argument to be in units of 4,096 bytes irrespective of
	   page size */
	*puiMMapOffset = *puiMMapOffset << (PAGE_SHIFT - 12);

exit_unlock:
    LinuxUnLockMutex(&g_sMMapMutex);

    return eError;
}


/*!
 *******************************************************************************

 @Function  PVRMMapReleaseMMapData

 @Description

 Release mmap data.

 @input psPerProc : Per-process data.
 @input hMHandle : Memory handle.
 @input pbMUnmap : pointer to location for munmap flag.
 @input puiUserVAddr : pointer to location for user mode address of mapping.
 @input puiByteSize : pointer to location for size of mapping.

 @Output pbMUnmap : points to flag that indicates whether an munmap is
		    required.
 @output puiUserVAddr : points to user mode address to munmap.

 @Return PVRSRV_ERROR : PVRSRV_OK, or error code.

 ******************************************************************************/
PVRSRV_ERROR
PVRMMapReleaseMMapData(PVRSRV_PER_PROCESS_DATA *psPerProc,
				IMG_HANDLE hMHandle,
				IMG_BOOL *pbMUnmap,
				IMG_SIZE_T *puiRealByteSize,
                IMG_UINTPTR_T *puiUserVAddr)
{
    LinuxMemArea *psLinuxMemArea;
    PKV_OFFSET_STRUCT psOffsetStruct;
    IMG_HANDLE hOSMemHandle;
    PVRSRV_ERROR eError;
    IMG_UINT32 ui32PID = OSGetCurrentProcessIDKM();

    LinuxLockMutex(&g_sMMapMutex);

    PVR_ASSERT(PVRSRVGetMaxHandle(psPerProc->psHandleBase) <= MAX_MMAP_HANDLE);

    eError = PVRSRVLookupOSMemHandle(psPerProc->psHandleBase, &hOSMemHandle, hMHandle);
    if (eError != PVRSRV_OK)
    {
	PVR_DPF((PVR_DBG_ERROR, "%s: Lookup of handle %p failed", __FUNCTION__, hMHandle));

	goto exit_unlock;
    }

    psLinuxMemArea = (LinuxMemArea *)hOSMemHandle;

    /* Find the offset structure */
    list_for_each_entry(psOffsetStruct, &psLinuxMemArea->sMMapOffsetStructList, sAreaItem)
    {
        if (psOffsetStruct->ui32PID == ui32PID)
        {
	    if (psOffsetStruct->ui32RefCount == 0)
	    {
		PVR_DPF((PVR_DBG_ERROR, "%s: Attempt to release mmap data with zero reference count for offset struct 0x%p, memory area %p", __FUNCTION__, psOffsetStruct, psLinuxMemArea));
		eError = PVRSRV_ERROR_STILL_MAPPED;
		goto exit_unlock;
	    }

	    PVRSRVOffsetStructDecRef(psOffsetStruct);

	    *pbMUnmap = (IMG_BOOL)((psOffsetStruct->ui32RefCount == 0) && (psOffsetStruct->uiUserVAddr != 0));

	    *puiUserVAddr = (*pbMUnmap) ? psOffsetStruct->uiUserVAddr : 0;
	    *puiRealByteSize = (*pbMUnmap) ? psOffsetStruct->uiRealByteSize : 0;

	    eError = PVRSRV_OK;
	    goto exit_unlock;
        }
    }

    /* MMap data not found */
    PVR_DPF((PVR_DBG_ERROR, "%s: Mapping data not found for handle %p (memory area %p)", __FUNCTION__, hMHandle, psLinuxMemArea));

    eError =  PVRSRV_ERROR_MAPPING_NOT_FOUND;

exit_unlock:
    LinuxUnLockMutex(&g_sMMapMutex);

    return eError;
}

static inline PKV_OFFSET_STRUCT
FindOffsetStructByOffset(IMG_UINTPTR_T uiOffset, IMG_SIZE_T uiRealByteSize)
{
    PKV_OFFSET_STRUCT psOffsetStruct;
#if !defined(PVR_MAKE_ALL_PFNS_SPECIAL)
    IMG_UINT32 ui32TID = GetCurrentThreadID();
#endif
    IMG_UINT32 ui32PID = OSGetCurrentProcessIDKM();

    list_for_each_entry(psOffsetStruct, &g_sMMapOffsetStructList, sMMapItem)
    {
        if (uiOffset == psOffsetStruct->uiMMapOffset && uiRealByteSize == psOffsetStruct->uiRealByteSize && psOffsetStruct->ui32PID == ui32PID)
        {
#if !defined(PVR_MAKE_ALL_PFNS_SPECIAL)
	    /*
	     * If the offset is physical, make sure the thread IDs match,
	     * as different threads may be mapping different memory areas
	     * with the same offset.
	     */
	    if (!PFNIsPhysical(uiOffset) || psOffsetStruct->ui32TID == ui32TID)
#endif
	    {
	        return psOffsetStruct;
	    }
        }
    }

    return IMG_NULL;
}


/*
 * Map a memory area into user space.
 * Note, the ui32ByteOffset is _not_ implicitly page aligned since
 * LINUX_MEM_AREA_SUB_ALLOC LinuxMemAreas have no alignment constraints.
 */
static IMG_BOOL
DoMapToUser(LinuxMemArea *psLinuxMemArea,
            struct vm_area_struct* ps_vma,
            IMG_UINTPTR_T uiByteOffset)
{
    IMG_SIZE_T uiByteSize;

	if ((psLinuxMemArea->hBMHandle) && (uiByteOffset != 0))
	{
		/* Partial mapping of sparse allocations should never happen */
		return IMG_FALSE;
	}

    if (psLinuxMemArea->eAreaType == LINUX_MEM_AREA_SUB_ALLOC)
    {
        return DoMapToUser(LinuxMemAreaRoot(psLinuxMemArea),		/* PRQA S 3670 */ /* allow recursion */
                    ps_vma,
                    psLinuxMemArea->uData.sSubAlloc.uiByteOffset + uiByteOffset);
    }

    /*
     * Note that ui32ByteSize may be larger than the size of the memory
     * area being mapped, as the former is a multiple of the page size.
     */
    uiByteSize = ps_vma->vm_end - ps_vma->vm_start;
    PVR_ASSERT(ADDR_TO_PAGE_OFFSET(uiByteSize) == 0);

#if defined (__sparc__)
    /*
     * For LINUX_MEM_AREA_EXTERNAL_KV, we don't know where the address range
     * we are being asked to map has come from, that is, whether it is memory
     * or I/O.  For all architectures other than SPARC, there is no distinction.
     * Since we don't currently support SPARC, we won't worry about it.
     */
#error "SPARC not supported"
#endif

#if !defined(PVR_MAKE_ALL_PFNS_SPECIAL)
    if (PFNIsPhysical(ps_vma->vm_pgoff))
    {
		IMG_INT result;

		PVR_ASSERT(LinuxMemAreaPhysIsContig(psLinuxMemArea));
		PVR_ASSERT(LinuxMemAreaToCpuPFN(psLinuxMemArea, ui32ByteOffset) == ps_vma->vm_pgoff);
        /*
	 * Since the memory is contiguous, we can map the whole range in one
	 * go .
	 */

	PVR_ASSERT(psLinuxMemArea->hBMHandle == IMG_NULL);

	result = IO_REMAP_PFN_RANGE(ps_vma, ps_vma->vm_start, ps_vma->vm_pgoff, uiByteSize, ps_vma->vm_page_prot);

        if(result == 0)
        {
            return IMG_TRUE;
        }

        PVR_DPF((PVR_DBG_MESSAGE, "%s: Failed to map contiguous physical address range (%d), trying non-contiguous path", __FUNCTION__, result));
    }
#endif

    {
	/*
         * Memory may be non-contiguous, so we map the range page,
	 * by page.  Since VM_PFNMAP mappings are assumed to be physically
	 * contiguous, we can't legally use REMAP_PFN_RANGE (that is, we
	 * could, but the resulting VMA may confuse other bits of the kernel
	 * that attempt to interpret it).
	 * The only alternative is to use VM_INSERT_PAGE, which requires
	 * finding the page structure corresponding to each page, or
	 * if mixed maps are supported (VM_MIXEDMAP), vm_insert_mixed.
	 */
        IMG_UINTPTR_T ulVMAPos;
	IMG_UINTPTR_T uiByteEnd = uiByteOffset + uiByteSize;
	IMG_UINTPTR_T uiPA;
	IMG_UINTPTR_T uiAdjustedPA = uiByteOffset;
#if defined(PVR_MAKE_ALL_PFNS_SPECIAL)
		IMG_BOOL bMixedMap = IMG_FALSE;
#endif
	/* First pass, validate the page frame numbers */
	for(uiPA = uiByteOffset; uiPA < uiByteEnd; uiPA += PAGE_SIZE)
	{
		IMG_UINTPTR_T pfn;
	    IMG_BOOL bMapPage = IMG_TRUE;

		if (psLinuxMemArea->hBMHandle)
		{
			if (!BM_MapPageAtOffset(psLinuxMemArea->hBMHandle, uiPA))
			{
				bMapPage = IMG_FALSE;
			}
		}

		if (bMapPage)
		{
			pfn =  LinuxMemAreaToCpuPFN(psLinuxMemArea, uiAdjustedPA);
			if (!pfn_valid(pfn))
			{
#if !defined(PVR_MAKE_ALL_PFNS_SPECIAL)
					PVR_DPF((PVR_DBG_ERROR,"%s: Error - PFN invalid: 0x" UINTPTR_FMT, __FUNCTION__, pfn));
					return IMG_FALSE;
#else
			bMixedMap = IMG_TRUE;
#endif
			}
		    else if (0 == page_count(pfn_to_page(pfn)))
		    {
		        bMixedMap = IMG_TRUE;
		    }
			uiAdjustedPA += PAGE_SIZE;
		}
	}

#if defined(PVR_MAKE_ALL_PFNS_SPECIAL)
		if (bMixedMap)
		{
#if !defined(USING_REMAP_PFN_RANGE)
			ps_vma->vm_flags |= VM_MIXEDMAP;
#endif
		}
#endif
	/* Second pass, get the page structures and insert the pages */
        ulVMAPos = ps_vma->vm_start;
        uiAdjustedPA = uiByteOffset;
	for(uiPA = uiByteOffset; uiPA < uiByteEnd; uiPA += PAGE_SIZE)
	{
	    IMG_UINTPTR_T pfn;
	    IMG_INT result;
	    IMG_BOOL bMapPage = IMG_TRUE;

		if (psLinuxMemArea->hBMHandle)
		{
			/* We have a sparse allocation, check if this page should be mapped */
			if (!BM_MapPageAtOffset(psLinuxMemArea->hBMHandle, uiPA))
			{
				bMapPage = IMG_FALSE;
			}
		}

		if (bMapPage)
		{
			pfn =  LinuxMemAreaToCpuPFN(psLinuxMemArea, uiAdjustedPA);

#if defined(PVR_MAKE_ALL_PFNS_SPECIAL)
		    if (bMixedMap)
		    {
#if defined(USING_REMAP_PFN_RANGE)
			result = IO_REMAP_PFN_RANGE(ps_vma, ulVMAPos, pfn, PAGE_SIZE, ps_vma->vm_page_prot);
#else
			result = vm_insert_mixed(ps_vma, ulVMAPos, pfn);
#endif
	                if(result != 0)
	                {
	                    PVR_DPF((PVR_DBG_ERROR,"%s: Error - vm_insert_mixed failed (%d)", __FUNCTION__, result));
	                    return IMG_FALSE;
	                }
		    }
		    else
#endif
		    {
				struct page *psPage;
	
		        PVR_ASSERT(pfn_valid(pfn));
	
		        psPage = pfn_to_page(pfn);

#if defined(USING_REMAP_PFN_RANGE)
			result = VM_INSERT_PAGE(ps_vma,  ulVMAPos, psPage);
#else
			result = IO_REMAP_PFN_RANGE(ps_vma, ulVMAPos, pfn, PAGE_SIZE, ps_vma->vm_page_prot);
#endif
	                if(result != 0)
	                {
	                    PVR_DPF((PVR_DBG_ERROR,"%s: Error - VM_INSERT_PAGE failed (%d)", __FUNCTION__, result));
	                    return IMG_FALSE;
	                }
		    }
		    uiAdjustedPA += PAGE_SIZE;
			}
	        ulVMAPos += PAGE_SIZE;
		}
	}

    return IMG_TRUE;
}


static IMG_VOID
MMapVOpenNoLock(struct vm_area_struct* ps_vma)
{
    PKV_OFFSET_STRUCT psOffsetStruct = (PKV_OFFSET_STRUCT)ps_vma->vm_private_data;

    PVR_ASSERT(psOffsetStruct != IMG_NULL);
    PVR_ASSERT(!psOffsetStruct->bOnMMapList);

    PVRSRVOffsetStructIncMapped(psOffsetStruct);

/*
* S.LSI: we need to map twice in a process to get virtual address for ion memory
*/
#if !defined(SUPPORT_ION)
    if (psOffsetStruct->ui32Mapped > 1)
    {
	PVR_DPF((PVR_DBG_WARNING, "%s: Offset structure 0x%p is being shared across processes (psOffsetStruct->ui32Mapped: %u)", __FUNCTION__, psOffsetStruct, psOffsetStruct->ui32Mapped));
        PVR_ASSERT((ps_vma->vm_flags & VM_DONTCOPY) == 0);
    }
#endif /*SUPPORT_ION*/

#if defined(DEBUG_LINUX_MMAP_AREAS)

    PVR_DPF((PVR_DBG_MESSAGE,
             "%s: psLinuxMemArea 0x%p, KVAddress 0x%p MMapOffset " UINTPTR_FMT ", ui32Mapped %d",
             __FUNCTION__,
             psOffsetStruct->psLinuxMemArea,
             LinuxMemAreaToCpuVAddr(psOffsetStruct->psLinuxMemArea),
             psOffsetStruct->uiMMapOffset,
             psOffsetStruct->ui32Mapped));
#endif
}


/*
 * Linux mmap open entry point.
 */
static void
MMapVOpen(struct vm_area_struct* ps_vma)
{
    LinuxLockMutex(&g_sMMapMutex);

    MMapVOpenNoLock(ps_vma);

    LinuxUnLockMutex(&g_sMMapMutex);
}


static IMG_VOID
MMapVCloseNoLock(struct vm_area_struct* ps_vma)
{
    PKV_OFFSET_STRUCT psOffsetStruct = (PKV_OFFSET_STRUCT)ps_vma->vm_private_data;
/*S.LSI*/
    LinuxMemArea *psLinuxMemArea = psOffsetStruct->psLinuxMemArea;
    PVR_ASSERT(psOffsetStruct != IMG_NULL);

#if defined(DEBUG_LINUX_MMAP_AREAS)
    PVR_DPF((PVR_DBG_MESSAGE,
             "%s: psLinuxMemArea %p, CpuVAddr %p uiMMapOffset " UINTPTR_FMT ", ui32Mapped %d",
             __FUNCTION__,
             psOffsetStruct->psLinuxMemArea,
             LinuxMemAreaToCpuVAddr(psOffsetStruct->psLinuxMemArea),
             psOffsetStruct->uiMMapOffset,
             psOffsetStruct->ui32Mapped));
#endif

    PVR_ASSERT(!psOffsetStruct->bOnMMapList);
    PVRSRVOffsetStructDecMapped(psOffsetStruct);
    if (psOffsetStruct->ui32Mapped == 0)
    {
	if (psOffsetStruct->ui32RefCount != 0)
	{
	        PVR_DPF((
              PVR_DBG_MESSAGE, 
              "%s: psOffsetStruct %p has non-zero reference count (ui32RefCount = %u). User mode address of start of mapping: 0x" UINTPTR_FMT, 
              __FUNCTION__, 
              psOffsetStruct, 
              psOffsetStruct->ui32RefCount, 
              psOffsetStruct->uiUserVAddr));
	}

        if (psLinuxMemArea->bDeferredFree )
        {
            PVRSRV_ERROR ret;
            LinuxUnLockMutex(&g_sMMapMutex);
            ret = PVRMMapRemoveRegisteredArea(psLinuxMemArea);
            if (PVRSRV_OK == ret)
                LinuxMemAreaDeepFree(psLinuxMemArea);
            LinuxLockMutex(&g_sMMapMutex);
        }
        else
        {
            DestroyOffsetStruct(psOffsetStruct);
        }
    }

    ps_vma->vm_private_data = NULL;
}

/*
 * Linux mmap close entry point.
 */
static void
MMapVClose(struct vm_area_struct* ps_vma)
{
    LinuxLockMutex(&g_sMMapMutex);

    MMapVCloseNoLock(ps_vma);

    LinuxUnLockMutex(&g_sMMapMutex);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26))
/*
 * This vma operation is used to read data from mmap regions. It is called
 * by access_process_vm, which is called to handle PTRACE_PEEKDATA ptrace
 * requests and reads from /proc/<pid>/mem.
 */
static int MMapVAccess(struct vm_area_struct *ps_vma, unsigned long addr,
					   void *buf, int len, int write)
{
    PKV_OFFSET_STRUCT psOffsetStruct;
    LinuxMemArea *psLinuxMemArea;
    unsigned long ulOffset;
	int iRetVal = -EINVAL;
	IMG_VOID *pvKernelAddr;

	LinuxLockMutex(&g_sMMapMutex);

	psOffsetStruct = (PKV_OFFSET_STRUCT)ps_vma->vm_private_data;
	psLinuxMemArea = psOffsetStruct->psLinuxMemArea;
	ulOffset = addr - ps_vma->vm_start;

    if (ulOffset+len > psLinuxMemArea->uiByteSize)
		/* Out of range. We shouldn't get here, because the kernel will do
		   the necessary checks before calling access_process_vm. */
		goto exit_unlock;

	pvKernelAddr = LinuxMemAreaToCpuVAddr(psLinuxMemArea);

	if (pvKernelAddr)
	{
		memcpy(buf, pvKernelAddr+ulOffset, len);
		iRetVal = len;
	}
	else
	{
		IMG_UINTPTR_T pfn, uiOffsetInPage;
		struct page *page;

		pfn = LinuxMemAreaToCpuPFN(psLinuxMemArea, ulOffset);

		if (!pfn_valid(pfn))
			goto exit_unlock;

		page = pfn_to_page(pfn);
		uiOffsetInPage = ADDR_TO_PAGE_OFFSET(ulOffset);

		if (uiOffsetInPage + len > PAGE_SIZE)
			/* The region crosses a page boundary */
			goto exit_unlock;

		pvKernelAddr = kmap(page);
		memcpy(buf, pvKernelAddr + uiOffsetInPage, len);
		kunmap(page);

		iRetVal = len;
	}

exit_unlock:
	LinuxUnLockMutex(&g_sMMapMutex);
    return iRetVal;
}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26) */

static struct vm_operations_struct MMapIOOps =
{
	.open=MMapVOpen,
	.close=MMapVClose,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26))
	.access=MMapVAccess,
#endif
};


/*!
 *******************************************************************************

 @Function  PVRMMap

 @Description

 Driver mmap entry point.

 @input pFile : unused.
 @input ps_vma : pointer to linux memory area descriptor.

 @Return 0, or Linux error code.

 ******************************************************************************/
int
PVRMMap(struct file* pFile, struct vm_area_struct* ps_vma)
{
    LinuxMemArea *psFlushMemArea = IMG_NULL;
    PKV_OFFSET_STRUCT psOffsetStruct;
    IMG_SIZE_T uiByteSize;
    IMG_VOID *pvBase = IMG_NULL;
    int iRetVal = 0;
    IMG_UINTPTR_T uiByteOffset = 0;	/* Keep compiler happy */
    IMG_SIZE_T uiFlushSize = 0;

    PVR_UNREFERENCED_PARAMETER(pFile);

    LinuxLockMutex(&g_sMMapMutex);
    
    uiByteSize = ps_vma->vm_end - ps_vma->vm_start;
    
    PVR_DPF((PVR_DBG_MESSAGE, "%s: Received mmap(2) request with ui32MMapOffset 0x" UINTPTR_FMT ","
                              " and uiByteSize %u(0x%x)",
            __FUNCTION__,
            ps_vma->vm_pgoff,
            uiByteSize, 
            uiByteSize));
   
    psOffsetStruct = FindOffsetStructByOffset(ps_vma->vm_pgoff, uiByteSize);

    if (psOffsetStruct == IMG_NULL)
    {
#if defined(SUPPORT_DRI_DRM)
		LinuxUnLockMutex(&g_sMMapMutex);

#if !defined(SUPPORT_DRI_DRM_EXT)
		/* Pass unknown requests onto the DRM module */
		return drm_mmap(pFile, ps_vma);
#else
        /*
         * Indicate to caller that the request is not for us.
         * Do not return this error elsewhere in this function, as the
         * caller may use it as a clue as to whether the mmap request
         * should be passed on to another component (e.g. drm_mmap).
         */
        return -ENOENT;
#endif
#else
        PVR_UNREFERENCED_PARAMETER(pFile);

        PVR_DPF((PVR_DBG_ERROR,
             "%s: Attempted to mmap unregistered area at vm_pgoff 0x%lx",
             __FUNCTION__, ps_vma->vm_pgoff));
        iRetVal = -EINVAL;
#endif
        goto unlock_and_return;
    }

    list_del(&psOffsetStruct->sMMapItem);
    psOffsetStruct->bOnMMapList = IMG_FALSE;

    /* Only support shared writeable mappings */
    if (((ps_vma->vm_flags & VM_WRITE) != 0) &&
        ((ps_vma->vm_flags & VM_SHARED) == 0))
    {
        PVR_DPF((PVR_DBG_ERROR, "%s: Cannot mmap non-shareable writable areas", __FUNCTION__));
        iRetVal = -EINVAL;
        goto unlock_and_return;
    }
   
    PVR_DPF((PVR_DBG_MESSAGE, "%s: Mapped psLinuxMemArea 0x%p\n",
         __FUNCTION__, psOffsetStruct->psLinuxMemArea));

    ps_vma->vm_flags |= VM_RESERVED;
    ps_vma->vm_flags |= VM_IO;

    /*
     * Disable mremap because our nopage handler assumes all
     * page requests have already been validated.
     */
    ps_vma->vm_flags |= VM_DONTEXPAND;
    
    /* Don't allow mapping to be inherited across a process fork */
    ps_vma->vm_flags |= VM_DONTCOPY;

    ps_vma->vm_private_data = (void *)psOffsetStruct;
    
    switch(psOffsetStruct->psLinuxMemArea->ui32AreaFlags & PVRSRV_HAP_CACHETYPE_MASK)
    {
        case PVRSRV_HAP_CACHED:
            /* This is the default, do nothing. */
            break;
        case PVRSRV_HAP_WRITECOMBINE:
            ps_vma->vm_page_prot = PGPROT_WC(ps_vma->vm_page_prot);
            break;
        case PVRSRV_HAP_UNCACHED:
            ps_vma->vm_page_prot = PGPROT_UC(ps_vma->vm_page_prot);
            break;
        default:
            PVR_DPF((PVR_DBG_ERROR, "%s: unknown cache type", __FUNCTION__));
            iRetVal = -EINVAL;
	    goto unlock_and_return;
    }

#ifdef CONFIG_ARCH_OMAP5
    {
	IMG_BOOL bModPageProt = IMG_FALSE;

	/* In OMAP5, the Cortex A15 no longer masks an issue with the L2
	 * interconnect. Write-combined access to the TILER aperture will
	 * generate SIGBUS / "non-line fetch abort" errors due to L2
	 * interconnect bus accesses. The workaround is to use a shared
	 * device access.
	 */

	bModPageProt |= (psOffsetStruct->psLinuxMemArea->eAreaType == LINUX_MEM_AREA_ION);

#ifdef CONFIG_DSSCOMP
	bModPageProt |= is_tiler_addr(LinuxMemAreaToCpuPAddr(psOffsetStruct->psLinuxMemArea, 0).uiAddr);
#endif /* CONFIG_DSSCOMP */ 

	if (bModPageProt)
	{
		ps_vma->vm_page_prot = __pgprot_modify(ps_vma->vm_page_prot,
					       L_PTE_MT_MASK,
					       L_PTE_MT_DEV_SHARED);
	}
    }
#endif /* CONFIG_ARCH_OMAP5 */

    /* Install open and close handlers for ref-counting */
    ps_vma->vm_ops = &MMapIOOps;
    
    if(!DoMapToUser(psOffsetStruct->psLinuxMemArea, ps_vma, 0))
    {
        iRetVal = -EAGAIN;
        goto unlock_and_return;
    }
    
    PVR_ASSERT(psOffsetStruct->uiUserVAddr == 0);

    psOffsetStruct->uiUserVAddr = ps_vma->vm_start;

    /* Compute the flush region (if necessary) inside the mmap mutex */
    if(psOffsetStruct->psLinuxMemArea->bNeedsCacheInvalidate)
    {
        psFlushMemArea = psOffsetStruct->psLinuxMemArea;

		/* Sparse mappings have to ask the BM for the virtual size */
		if (psFlushMemArea->hBMHandle)
		{
			pvBase = (IMG_VOID *)ps_vma->vm_start;
			uiByteOffset = 0;
			uiFlushSize = BM_GetVirtualSize(psFlushMemArea->hBMHandle);
		}
		else
		{
			IMG_SIZE_T uiDummyByteSize;

	        DetermineUsersSizeAndByteOffset(psFlushMemArea,
	                                        &uiDummyByteSize,
	                                        &uiByteOffset);
	
	        pvBase = (IMG_VOID *)ps_vma->vm_start + uiByteOffset;
	        uiFlushSize = psFlushMemArea->uiByteSize;
		}

        psFlushMemArea->bNeedsCacheInvalidate = IMG_FALSE;
    }

    /* Call the open routine to increment the usage count */
    MMapVOpenNoLock(ps_vma);

    PVR_DPF((PVR_DBG_MESSAGE, "%s: Mapped area at offset 0x" UINTPTR_FMT "\n",
             __FUNCTION__, (IMG_UINTPTR_T)ps_vma->vm_pgoff));

unlock_and_return:
    if (iRetVal != 0 && psOffsetStruct != IMG_NULL)
    {
        DestroyOffsetStruct(psOffsetStruct);
    }

    LinuxUnLockMutex(&g_sMMapMutex);

    if(psFlushMemArea)
    {
        OSInvalidateCPUCacheRangeKM(psFlushMemArea, uiByteOffset, pvBase,
									uiFlushSize);
    }

    return iRetVal;
}


#if defined(DEBUG_LINUX_MMAP_AREAS)

/*
 * Lock MMap regions list (called on page start/stop while reading /proc/mmap)

 * sfile : seq_file that handles /proc file
 * start : TRUE if it's start, FALSE if it's stop
 *  
*/
static void ProcSeqStartstopMMapRegistations(struct seq_file *sfile,IMG_BOOL start) 
{
	if(start) 
	{
	    LinuxLockMutex(&g_sMMapMutex);		
	}
	else
	{
	    LinuxUnLockMutex(&g_sMMapMutex);
	}
}


/*
 * Convert offset (index from KVOffsetTable) to element 
 * (called when reading /proc/mmap file)

 * sfile : seq_file that handles /proc file
 * off : index into the KVOffsetTable from which to print
 *  
 * returns void* : Pointer to element that will be dumped
 *  
*/
static void* ProcSeqOff2ElementMMapRegistrations(struct seq_file *sfile, loff_t off)
{
    LinuxMemArea *psLinuxMemArea;
	if(!off) 
	{
		return PVR_PROC_SEQ_START_TOKEN;
	}

    list_for_each_entry(psLinuxMemArea, &g_sMMapAreaList, sMMapItem)
    {
        PKV_OFFSET_STRUCT psOffsetStruct;

	 	list_for_each_entry(psOffsetStruct, &psLinuxMemArea->sMMapOffsetStructList, sAreaItem)
        {
	    	off--;
	    	if (off == 0)
	    	{				
				PVR_ASSERT(psOffsetStruct->psLinuxMemArea == psLinuxMemArea);
				return (void*)psOffsetStruct;
		    }
        }
    }
	return (void*)0;
}

/*
 * Gets next MMap element to show. (called when reading /proc/mmap file)

 * sfile : seq_file that handles /proc file
 * el : actual element
 * off : index into the KVOffsetTable from which to print
 *  
 * returns void* : Pointer to element to show (0 ends iteration)
*/
static void* ProcSeqNextMMapRegistrations(struct seq_file *sfile,void* el,loff_t off)
{
	return ProcSeqOff2ElementMMapRegistrations(sfile,off);
}


/*
 * Show MMap element (called when reading /proc/mmap file)

 * sfile : seq_file that handles /proc file
 * el : actual element
 *  
*/
static void ProcSeqShowMMapRegistrations(struct seq_file *sfile, void *el)
{
	KV_OFFSET_STRUCT *psOffsetStruct = (KV_OFFSET_STRUCT*)el;
    LinuxMemArea *psLinuxMemArea;
	IMG_SIZE_T uiRealByteSize;
	IMG_UINTPTR_T uiByteOffset;

	if(el == PVR_PROC_SEQ_START_TOKEN) 
	{
        seq_printf( sfile,
#if !defined(DEBUG_LINUX_XML_PROC_FILES)
						  "Allocations registered for mmap: %u\n"
                          "In total these areas correspond to %u bytes\n"
                          "psLinuxMemArea "
						  "UserVAddr "
						  "KernelVAddr "
						  "CpuPAddr "
                          "MMapOffset "
                          "ByteLength "
                          "LinuxMemType             "
						  "Pid   Name     Flags\n",
#else
                          "<mmap_header>\n"
                          "\t<count>%u</count>\n"
                          "\t<bytes>%u</bytes>\n"
                          "</mmap_header>\n",
#endif
						  g_ui32RegisteredAreas,
                          g_uiTotalByteSize
                          );
		return;
	}

   	psLinuxMemArea = psOffsetStruct->psLinuxMemArea;

	DetermineUsersSizeAndByteOffset(psLinuxMemArea,
									&uiRealByteSize,
									&uiByteOffset);

	seq_printf( sfile,
#if !defined(DEBUG_LINUX_XML_PROC_FILES)
						"%p       %p %p " CPUPADDR_FMT " " UINTPTR_FMT "   %u   %-24s %-5u %-8s %08x(%s)\n",
#else
                        "<mmap_record>\n"
						"\t<pointer>%p</pointer>\n"
                        "\t<user_virtual>%p</user_virtual>\n"
                        "\t<kernel_virtual>%p</kernel_virtual>\n"
                        "\t<cpu_physical>" CPUPADDR_FMT "</cpu_physical>\n"
                        "\t<mmap_offset>" UINTPTR_FMT "</mmap_offset>\n"
                        "\t<bytes>%u</bytes>\n"
                        "\t<linux_mem_area_type>%-24s</linux_mem_area_type>\n"
                        "\t<pid>%-5u</pid>\n"
                        "\t<name>%-8s</name>\n"
                        "\t<flags>%08x</flags>\n"
                        "\t<flags_string>%s</flags_string>\n"
                        "</mmap_record>\n",
#endif
                        psLinuxMemArea,
						(IMG_PVOID)(psOffsetStruct->uiUserVAddr + uiByteOffset),
						LinuxMemAreaToCpuVAddr(psLinuxMemArea),
                        LinuxMemAreaToCpuPAddr(psLinuxMemArea,0).uiAddr,
						(IMG_UINTPTR_T)psOffsetStruct->uiMMapOffset,
						psLinuxMemArea->uiByteSize,
                        LinuxMemAreaTypeToString(psLinuxMemArea->eAreaType),
						psOffsetStruct->ui32PID,
						psOffsetStruct->pszName,
						psLinuxMemArea->ui32AreaFlags,
                        HAPFlagsToString(psLinuxMemArea->ui32AreaFlags));
}

#endif


/*!
 *******************************************************************************

 @Function  PVRMMapRegisterArea

 @Description

 Register a memory area with the mmap code.

 @input psLinuxMemArea : pointer to memory area.

 @Return PVRSRV_OK, or PVRSRV_ERROR.

 ******************************************************************************/
PVRSRV_ERROR
PVRMMapRegisterArea(LinuxMemArea *psLinuxMemArea)
{
    PVRSRV_ERROR eError;
#if defined(DEBUG) || defined(DEBUG_LINUX_MMAP_AREAS)
    const IMG_CHAR *pszName = LinuxMemAreaTypeToString(LinuxMemAreaRootType(psLinuxMemArea));
#endif

    LinuxLockMutex(&g_sMMapMutex);

#if defined(DEBUG) || defined(DEBUG_LINUX_MMAP_AREAS)
    PVR_DPF((PVR_DBG_MESSAGE,
             "%s(%s, psLinuxMemArea 0x%p, ui32AllocFlags 0x%8x)",
             __FUNCTION__, pszName, psLinuxMemArea,  psLinuxMemArea->ui32AreaFlags));
#endif

    PVR_ASSERT(psLinuxMemArea->eAreaType != LINUX_MEM_AREA_SUB_ALLOC || LinuxMemAreaRoot(psLinuxMemArea)->eAreaType != LINUX_MEM_AREA_SUB_ALLOC);

    /* Check this mem area hasn't already been registered */
    if(psLinuxMemArea->bMMapRegistered)
    {
        PVR_DPF((PVR_DBG_ERROR, "%s: psLinuxMemArea 0x%p is already registered",
                __FUNCTION__, psLinuxMemArea));
        eError = PVRSRV_ERROR_INVALID_PARAMS;
	goto exit_unlock;
    }

    list_add_tail(&psLinuxMemArea->sMMapItem, &g_sMMapAreaList);

    psLinuxMemArea->bMMapRegistered = IMG_TRUE;

#if defined(DEBUG_LINUX_MMAP_AREAS)
    g_ui32RegisteredAreas++;
    /*
     * Sub memory areas are excluded from g_ui32TotalByteSize so that we
     * don't count memory twice, once for the parent and again for sub
     * allocationis.
     */
    if (psLinuxMemArea->eAreaType != LINUX_MEM_AREA_SUB_ALLOC)
    {
        g_uiTotalByteSize += psLinuxMemArea->uiByteSize;
    }
#endif

    eError = PVRSRV_OK;

exit_unlock:
    LinuxUnLockMutex(&g_sMMapMutex);

    return eError;
}


/*!
 *******************************************************************************

 @Function  PVRMMapRemoveRegisterArea

 @Description

 Unregister a memory area with the mmap code.

 @input psLinuxMemArea : pointer to memory area.

 @Return PVRSRV_OK, or PVRSRV_ERROR.

 ******************************************************************************/
PVRSRV_ERROR
PVRMMapRemoveRegisteredArea(LinuxMemArea *psLinuxMemArea)
{
    PVRSRV_ERROR eError;
    PKV_OFFSET_STRUCT psOffsetStruct, psTmpOffsetStruct;

    LinuxLockMutex(&g_sMMapMutex);

    PVR_ASSERT(psLinuxMemArea->bMMapRegistered);

    list_for_each_entry_safe(psOffsetStruct, psTmpOffsetStruct, &psLinuxMemArea->sMMapOffsetStructList, sAreaItem)
    {
	if (psOffsetStruct->ui32Mapped != 0)
	{
/* S.LSI 12.07.24
 * LSI MM IPs use multi vma open operation for one buffer
 * The remain resources freed during the unmap memory*/
#if defined(DONOT_ALLOW_MULTI_VMA_OPEN)
	     PVR_DPF((PVR_DBG_ERROR, "%s: psOffsetStruct 0x%p for memory area 0x0x%p is still mapped; psOffsetStruct->ui32Mapped %u",  __FUNCTION__, psOffsetStruct, psLinuxMemArea, psOffsetStruct->ui32Mapped));
		dump_stack();
		PVRSRVDumpRefCountCCB();
#endif
		eError = PVRSRV_ERROR_STILL_MAPPED;
		goto exit_unlock;
	}
	else
	{
	      /*
	      * An offset structure is created when a call is made to get
	      * the mmap data for a physical mapping.  If the data is never
	      * used for mmap, we will be left with an umapped offset
	      * structure.
	      */
	     PVR_DPF((PVR_DBG_WARNING, "%s: psOffsetStruct 0x%p was never mapped",  __FUNCTION__, psOffsetStruct));
	}

#if defined(DONOT_ALLOW_MULTI_VMA_OPEN)
	PVR_ASSERT((psOffsetStruct->ui32Mapped == 0) && psOffsetStruct->bOnMMapList);
#endif

	DestroyOffsetStruct(psOffsetStruct);
    }

    list_del(&psLinuxMemArea->sMMapItem);

    psLinuxMemArea->bMMapRegistered = IMG_FALSE;

#if defined(DEBUG_LINUX_MMAP_AREAS)
    g_ui32RegisteredAreas--;
    if (psLinuxMemArea->eAreaType != LINUX_MEM_AREA_SUB_ALLOC)
    {
        g_uiTotalByteSize -= psLinuxMemArea->uiByteSize;
    }
#endif

    eError = PVRSRV_OK;

exit_unlock:
    LinuxUnLockMutex(&g_sMMapMutex);
    return eError;
}


/*!
 *******************************************************************************

 @Function  LinuxMMapPerProcessConnect

 @Description

 Per-process mmap initialisation code.

 @input psEnvPerProc : pointer to OS specific per-process data.

 @Return PVRSRV_OK, or PVRSRV_ERROR.

 ******************************************************************************/
PVRSRV_ERROR
LinuxMMapPerProcessConnect(PVRSRV_ENV_PER_PROCESS_DATA *psEnvPerProc)
{
    PVR_UNREFERENCED_PARAMETER(psEnvPerProc);

    return PVRSRV_OK;
}

/*!
 *******************************************************************************

 @Function  LinuxMMapPerProcessDisconnect

 @Description

 Per-process mmap deinitialisation code.

 @input psEnvPerProc : pointer to OS specific per-process data.

 ******************************************************************************/
IMG_VOID
LinuxMMapPerProcessDisconnect(PVRSRV_ENV_PER_PROCESS_DATA *psEnvPerProc)
{
    PKV_OFFSET_STRUCT psOffsetStruct, psTmpOffsetStruct;
    IMG_BOOL bWarn = IMG_FALSE;
    IMG_UINT32 ui32PID = OSGetCurrentProcessIDKM();

    PVR_UNREFERENCED_PARAMETER(psEnvPerProc);

    LinuxLockMutex(&g_sMMapMutex);

    list_for_each_entry_safe(psOffsetStruct, psTmpOffsetStruct, &g_sMMapOffsetStructList, sMMapItem)
    {
	if (psOffsetStruct->ui32PID == ui32PID)
	{
	    if (!bWarn)
	    {
		PVR_DPF((PVR_DBG_WARNING, "%s: process has unmapped offset structures. Removing them", __FUNCTION__));
		bWarn = IMG_TRUE;
	    }
	    PVR_ASSERT(psOffsetStruct->ui32Mapped == 0);
	    PVR_ASSERT(psOffsetStruct->bOnMMapList);

	    DestroyOffsetStruct(psOffsetStruct);
	}
    }

    LinuxUnLockMutex(&g_sMMapMutex);
}


/*!
 *******************************************************************************

 @Function  LinuxMMapPerProcessHandleOptions

 @Description

 Set secure handle options required by mmap code.

 @input psHandleBase : pointer to handle base.

 @Return PVRSRV_OK, or PVRSRV_ERROR.

 ******************************************************************************/
PVRSRV_ERROR LinuxMMapPerProcessHandleOptions(PVRSRV_HANDLE_BASE *psHandleBase)
{
    PVRSRV_ERROR eError;

    eError = PVRSRVSetMaxHandle(psHandleBase, MAX_MMAP_HANDLE);
    if (eError != PVRSRV_OK)
    {
	PVR_DPF((PVR_DBG_ERROR,"%s: failed to set handle limit (%d)", __FUNCTION__, eError));
	return eError;
    }

    return eError;
}


/*!
 *******************************************************************************

 @Function  PVRMMapInit

 @Description

 MMap initialisation code

 ******************************************************************************/
IMG_VOID
PVRMMapInit(IMG_VOID)
{
    LinuxInitMutex(&g_sMMapMutex);

    g_psMemmapCache = KMemCacheCreateWrapper("img-mmap", sizeof(KV_OFFSET_STRUCT), 0, 0);
    if (!g_psMemmapCache)
    {
        PVR_DPF((PVR_DBG_ERROR,"%s: failed to allocate kmem_cache", __FUNCTION__));
	goto error;
    }

#if defined(DEBUG_LINUX_MMAP_AREAS)
	g_ProcMMap = CreateProcReadEntrySeq("mmap", NULL, 
						  ProcSeqNextMMapRegistrations,
						  ProcSeqShowMMapRegistrations,
						  ProcSeqOff2ElementMMapRegistrations,
						  ProcSeqStartstopMMapRegistations
						 );
#endif  /* defined(DEBUG_LINUX_MMAP_AREAS) */
    return;

error:
    PVRMMapCleanup();
    return;
}


/*!
 *******************************************************************************

 @Function  PVRMMapCleanup

 @Description

 Mmap deinitialisation code

 ******************************************************************************/
IMG_VOID
PVRMMapCleanup(IMG_VOID)
{
    PVRSRV_ERROR eError;

    if (!list_empty(&g_sMMapAreaList))
    {
	LinuxMemArea *psLinuxMemArea, *psTmpMemArea;

	PVR_DPF((PVR_DBG_ERROR, "%s: Memory areas are still registered with MMap", __FUNCTION__));
	
	PVR_TRACE(("%s: Unregistering memory areas", __FUNCTION__));
 	list_for_each_entry_safe(psLinuxMemArea, psTmpMemArea, &g_sMMapAreaList, sMMapItem)
	{
		eError = PVRMMapRemoveRegisteredArea(psLinuxMemArea);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: PVRMMapRemoveRegisteredArea failed (%d)", __FUNCTION__, eError));
		}
		PVR_ASSERT(eError == PVRSRV_OK);

		LinuxMemAreaDeepFree(psLinuxMemArea);
	}
    }
    PVR_ASSERT(list_empty((&g_sMMapAreaList)));

#if defined(DEBUG_LINUX_MMAP_AREAS)
    RemoveProcEntrySeq(g_ProcMMap);
#endif /* defined(DEBUG_LINUX_MMAP_AREAS) */

    if(g_psMemmapCache)
    {
        KMemCacheDestroyWrapper(g_psMemmapCache);
        g_psMemmapCache = NULL;
    }
}
