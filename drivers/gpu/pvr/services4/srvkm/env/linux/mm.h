/*************************************************************************/ /*!
@Title          Linux Memory Management.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares various memory management utility functions
                for Linux.
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
#ifndef __IMG_LINUX_MM_H__
#define __IMG_LINUX_MM_H__

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#endif

#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/list.h>

#include <asm/io.h>

#define	PHYS_TO_PFN(phys) ((phys) >> PAGE_SHIFT)
#define PFN_TO_PHYS(pfn) ((pfn) << PAGE_SHIFT)

#define RANGE_TO_PAGES(range) (((range) + (PAGE_SIZE - 1)) >> PAGE_SHIFT)

#define	ADDR_TO_PAGE_OFFSET(addr) (((unsigned long)(addr)) & (PAGE_SIZE - 1))

#define	PAGES_TO_BYTES(pages) ((pages) << PAGE_SHIFT)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,10))
#define	REMAP_PFN_RANGE(vma, addr, pfn, size, prot) remap_pfn_range(vma, addr, pfn, size, prot)
#else
#define	REMAP_PFN_RANGE(vma, addr, pfn, size, prot) remap_page_range(vma, addr, PFN_TO_PHYS(pfn), size, prot)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,12))
#define	IO_REMAP_PFN_RANGE(vma, addr, pfn, size, prot) io_remap_pfn_range(vma, addr, pfn, size, prot)
#else
#define	IO_REMAP_PFN_RANGE(vma, addr, pfn, size, prot) io_remap_page_range(vma, addr, PFN_TO_PHYS(pfn), size, prot)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15))
#define	VM_INSERT_PAGE(vma, addr, page) vm_insert_page(vma, addr, page)
#else
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,10))
#define VM_INSERT_PAGE(vma, addr, page) remap_pfn_range(vma, addr, page_to_pfn(page), PAGE_SIZE, vma->vm_page_prot);
#else
#define VM_INSERT_PAGE(vma, addr, page) remap_page_range(vma, addr, page_to_phys(page), PAGE_SIZE, vma->vm_page_prot);
#endif
#endif

static inline IMG_UINTPTR_T VMallocToPhys(IMG_VOID *pCpuVAddr)
{
	return (page_to_phys(vmalloc_to_page(pCpuVAddr)) + ADDR_TO_PAGE_OFFSET(pCpuVAddr));
		
}

typedef enum {
    LINUX_MEM_AREA_IOREMAP,
    LINUX_MEM_AREA_EXTERNAL_KV,
    LINUX_MEM_AREA_IO,
    LINUX_MEM_AREA_VMALLOC,
    LINUX_MEM_AREA_ALLOC_PAGES,
    LINUX_MEM_AREA_SUB_ALLOC,
    LINUX_MEM_AREA_ION,
#if defined(PVR_LINUX_MEM_AREA_USE_VMAP)
    LINUX_MEM_AREA_VMAP,
#endif
    LINUX_MEM_AREA_TYPE_COUNT
}LINUX_MEM_AREA_TYPE;

typedef struct _LinuxMemArea LinuxMemArea;


/* FIXME - describe this structure. */
struct _LinuxMemArea {
    LINUX_MEM_AREA_TYPE eAreaType;
    union _uData
    {
        struct _sIORemap
        {
            /* Note: The memory this represents is _not_ implicitly
             * page aligned, neither is its size */
            IMG_CPU_PHYADDR CPUPhysAddr;
            IMG_VOID *pvIORemapCookie;
        }sIORemap;
        struct _sExternalKV
        {
            /* Note: The memory this represents is _not_ implicitly
             * page aligned, neither is its size */
	    IMG_BOOL bPhysContig;
	    union {
		    /*
		     * SYSPhysAddr is valid if bPhysContig is true, else
		     * pSysPhysAddr is valid
		     */
		    IMG_SYS_PHYADDR SysPhysAddr;
		    IMG_SYS_PHYADDR *pSysPhysAddr;
	    } uPhysAddr;
            IMG_VOID *pvExternalKV;
        }sExternalKV;
        struct _sIO
        {
            /* Note: The memory this represents is _not_ implicitly
             * page aligned, neither is its size */
            IMG_CPU_PHYADDR CPUPhysAddr;
        }sIO;
        struct _sVmalloc
        {
            /* Note the memory this represents _is_ implicitly
             * page aligned _and_ so is its size */
            IMG_VOID *pvVmallocAddress;
#if defined(PVR_LINUX_MEM_AREA_USE_VMAP)
            struct page **ppsPageList;
	    IMG_HANDLE hBlockPageList;
#endif
        }sVmalloc;
        struct _sPageList
        {
            /* Note the memory this represents _is_ implicitly
             * page aligned _and_ so is its size */
            struct page **ppsPageList;
	    IMG_HANDLE hBlockPageList;
        }sPageList;
        struct _sIONTilerAlloc
        {
            /* Note the memory this represents _is_ implicitly
             * page aligned _and_ so is its size */
            IMG_CPU_PHYADDR *pCPUPhysAddrs;
            struct ion_handle *psIONHandle[3];
		/* S.LSI 12.07.24
		 * ionClient is needed for free ion handle*/
            struct ion_client *psIONClient;
        }sIONTilerAlloc;
        struct _sSubAlloc
        {
            /* Note: The memory this represents is _not_ implicitly
             * page aligned, neither is its size */
            LinuxMemArea *psParentLinuxMemArea;
            IMG_UINTPTR_T uiByteOffset;
        }sSubAlloc;
    }uData;

    IMG_SIZE_T uiByteSize;		    /* Size of memory area */

    IMG_UINT32 ui32AreaFlags;		/* Flags passed at creation time */

    IMG_BOOL bMMapRegistered;		/* Registered with mmap code */

    IMG_BOOL bNeedsCacheInvalidate;	/* Cache should be invalidated on first map? */

	IMG_HANDLE hBMHandle;			/* Handle back to BM for this allocation */

    /* List entry for global list of areas registered for mmap */
    struct list_head	sMMapItem;

    /*
     * Head of list of all mmap offset structures associated with this
     * memory area.
     */
    struct list_head	sMMapOffsetStructList;

    /* S.LSI 12.10.04
     * Add a variable for deferred free method */
    IMG_BOOL bDeferredFree;
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17))
typedef kmem_cache_t LinuxKMemCache;
#else
typedef struct kmem_cache LinuxKMemCache;
#endif


/*!
 *******************************************************************************
 * @Function	LinuxMMInit
 *
 * @Description
 *
 * Initialise linux memory management code.
 * This should be called during services initialisation.
 *
 * @Return none
******************************************************************************/
PVRSRV_ERROR LinuxMMInit(IMG_VOID);


/*!
 *******************************************************************************
 *
 * @Function	LinuxMMCleanup
 *
 * @Description
 *
 * Cleanup state for the linux memory management code.
 * This should be called at services cleanup.
 *
 * @Return none
******************************************************************************/
IMG_VOID LinuxMMCleanup(IMG_VOID);


/*!
 *******************************************************************************
 * @brief Wrappers for kmalloc/kfree with optional /proc/pvr/km tracking
 *        They can also be used as more concise replacements for OSAllocMem
 *        in Linux specific code.
 *
 * @param uByteSize  
 *
 * @return 
 ******************************************************************************/
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
#define KMallocWrapper(uByteSize, uFlags) _KMallocWrapper(uByteSize, uFlags, __FILE__, __LINE__)
#else
#define KMallocWrapper(uByteSize, uFlags) _KMallocWrapper(uByteSize, uFlags, NULL, 0)
#endif
IMG_VOID *_KMallocWrapper(IMG_SIZE_T uByteSize, gfp_t uFlags, IMG_CHAR *szFileName, IMG_UINT32 ui32Line);


/*!
 *******************************************************************************
 * @brief 
 *
 * @param pvCpuVAddr  
 *
 * @return 
 ******************************************************************************/
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
#define KFreeWrapper(pvCpuVAddr) _KFreeWrapper(pvCpuVAddr, __FILE__, __LINE__)
#else
#define KFreeWrapper(pvCpuVAddr) _KFreeWrapper(pvCpuVAddr, NULL, 0)
#endif
IMG_VOID _KFreeWrapper(IMG_VOID *pvCpuVAddr, IMG_CHAR *pszFileName, IMG_UINT32 ui32Line);


/*!
 *******************************************************************************
 * @brief 
 *
 * @param uBytes  
 * @param ui32AllocFlags  
 *
 * @return 
 ******************************************************************************/
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
#define VMallocWrapper(uiBytes, ui32AllocFlags) _VMallocWrapper(uiBytes, ui32AllocFlags, __FILE__, __LINE__)
#else
#define VMallocWrapper(uiBytes, ui32AllocFlags) _VMallocWrapper(uiBytes, ui32AllocFlags, NULL, 0)
#endif
IMG_VOID *_VMallocWrapper(IMG_SIZE_T uiBytes, IMG_UINT32 ui32AllocFlags, IMG_CHAR *pszFileName, IMG_UINT32 ui32Line);


/*!
 *******************************************************************************
 * @brief 
 *
 * @param pvCpuVAddr  
 *
 * @return 
 ******************************************************************************/
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
#define VFreeWrapper(pvCpuVAddr) _VFreeWrapper(pvCpuVAddr, __FILE__, __LINE__)
#else
#define VFreeWrapper(pvCpuVAddr) _VFreeWrapper(pvCpuVAddr, NULL, 0)
#endif
IMG_VOID _VFreeWrapper(IMG_VOID *pvCpuVAddr, IMG_CHAR *pszFileName, IMG_UINT32 ui32Line);


/*!
 *******************************************************************************
 * @brief Allocates virtually contiguous pages
 *
 * @param uBytes  number of bytes to reserve
 * @param ui32AreaFlags  Heap caching and mapping Flags
 *
 * @return Page-aligned address of virtual allocation or NULL on error
 ******************************************************************************/
LinuxMemArea *NewVMallocLinuxMemArea(IMG_SIZE_T uBytes, IMG_UINT32 ui32AreaFlags);


/*!
 *******************************************************************************
 * @brief Deallocates virtually contiguous pages
 *
 * @param LinuxMemArea from NewVMallocLinuxMemArea
 *
 ******************************************************************************/
IMG_VOID FreeVMallocLinuxMemArea(LinuxMemArea *psLinuxMemArea);


/*!
 *******************************************************************************
 * @brief Reserve physical IO memory and create a CPU virtual mapping for it
 *
 * @param BasePAddr 
 * @param uiBytes  
 * @param ui32MappingFlags  
 *
 * @return 
 ******************************************************************************/
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
#define IORemapWrapper(BasePAddr, uiBytes, ui32MappingFlags) \
    _IORemapWrapper(BasePAddr, uiBytes, ui32MappingFlags, __FILE__, __LINE__)
#else
#define IORemapWrapper(BasePAddr, uiBytes, ui32MappingFlags) \
    _IORemapWrapper(BasePAddr, uiBytes, ui32MappingFlags, NULL, 0)
#endif
IMG_VOID *_IORemapWrapper(IMG_CPU_PHYADDR BasePAddr,
                          IMG_SIZE_T uiBytes,
                          IMG_UINT32 ui32MappingFlags,
                          IMG_CHAR *pszFileName,
                          IMG_UINT32 ui32Line);


/*!
 *******************************************************************************
 * @brief Reserve physical IO memory and create a CPU virtual mapping for it
 *
 * @param BasePAddr  
 * @param uiBytes  
 * @param ui32AreaFlags  Heap caching and mapping Flags
 *
 * @return 
 ******************************************************************************/
LinuxMemArea *NewIORemapLinuxMemArea(IMG_CPU_PHYADDR BasePAddr, IMG_SIZE_T uiBytes, IMG_UINT32 ui32AreaFlags);


/*!
 *******************************************************************************
 * @brief 
 *
 * @param psLinuxMemArea  
 *
 * @return 
 ********************************************************************************/
IMG_VOID FreeIORemapLinuxMemArea(LinuxMemArea *psLinuxMemArea);

/*!
 *******************************************************************************
 * @brief Register physical memory which already has a CPU virtual mapping
 *
 * @param pBasePAddr  
 * @param pvCPUVAddr  
 * @param bPhysContig
 * @param uBytes  
 * @param ui32AreaFlags  Heap caching and mapping Flags
 *
 * @return 
 ******************************************************************************/
LinuxMemArea *NewExternalKVLinuxMemArea(IMG_SYS_PHYADDR *pBasePAddr, IMG_VOID *pvCPUVAddr, IMG_SIZE_T uBytes, IMG_BOOL bPhysContig, IMG_UINT32 ui32AreaFlags);


/*!
 *******************************************************************************
 * @brief 
 *
 * @param psLinuxMemArea  
 *
 * @return 
 ******************************************************************************/
IMG_VOID FreeExternalKVLinuxMemArea(LinuxMemArea *psLinuxMemArea);


/*!
 ******************************************************************************
 * @brief Unmaps an IO memory mapping created using IORemap
 *
 * @param pvIORemapCookie  
 *
 * @return 
 ******************************************************************************/
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
#define IOUnmapWrapper(pvIORemapCookie) \
    _IOUnmapWrapper(pvIORemapCookie, __FILE__, __LINE__)
#else
#define IOUnmapWrapper(pvIORemapCookie) \
    _IOUnmapWrapper(pvIORemapCookie, NULL, 0)
#endif
IMG_VOID _IOUnmapWrapper(IMG_VOID *pvIORemapCookie, IMG_CHAR *pszFileName, IMG_UINT32 ui32Line);


/*!
 *******************************************************************************
 * @brief 
 *
 * @param psLinuxMemArea  
 * @param uByteOffset  
 *
 * @return 
 ******************************************************************************/
struct page *LinuxMemAreaOffsetToPage(LinuxMemArea *psLinuxMemArea, IMG_UINTPTR_T uByteOffset);


/*!
 *******************************************************************************
 * @brief 
 *
 * @param pszName  
 * @param Size  
 * @param Align  
 * @param ui32Flags  
 *
 * @return 
 ******************************************************************************/
LinuxKMemCache *KMemCacheCreateWrapper(IMG_CHAR *pszName, size_t Size, size_t Align, IMG_UINT32 ui32Flags);


/*!
 *******************************************************************************
 * @brief 
 *
 * @param psCache  
 *
 * @return 
 ******************************************************************************/
IMG_VOID KMemCacheDestroyWrapper(LinuxKMemCache *psCache);


/*!
 *******************************************************************************
 * @brief 
 *
 * @param psCache  
 * @param Flags  
 *
 * @return 
 ******************************************************************************/
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
#define KMemCacheAllocWrapper(psCache, Flags) _KMemCacheAllocWrapper(psCache, Flags, __FILE__, __LINE__)
#else
#define KMemCacheAllocWrapper(psCache, Flags) _KMemCacheAllocWrapper(psCache, Flags, NULL, 0)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14))
IMG_VOID *_KMemCacheAllocWrapper(LinuxKMemCache *psCache, gfp_t Flags, IMG_CHAR *pszFileName, IMG_UINT32 ui32Line);
#else
IMG_VOID *_KMemCacheAllocWrapper(LinuxKMemCache *psCache, int Flags, IMG_CHAR *pszFileName, IMG_UINT32 ui32Line);
#endif

/*!
 *******************************************************************************
 * @brief 
 *
 * @param psCache  
 * @param pvObject  
 *
 * @return 
 ******************************************************************************/
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
#define KMemCacheFreeWrapper(psCache, pvObject) _KMemCacheFreeWrapper(psCache, pvObject, __FILE__, __LINE__)
#else
#define KMemCacheFreeWrapper(psCache, pvObject) _KMemCacheFreeWrapper(psCache, pvObject, NULL, 0)
#endif
IMG_VOID _KMemCacheFreeWrapper(LinuxKMemCache *psCache, IMG_VOID *pvObject, IMG_CHAR *pszFileName, IMG_UINT32 ui32Line);


/*!
 *******************************************************************************
 * @brief 
 *
 * @param psCache  
 *
 * @return 
 ******************************************************************************/
const IMG_CHAR *KMemCacheNameWrapper(LinuxKMemCache *psCache);


/*!
 *******************************************************************************
 * @brief 
 *
 * @param BasePAddr  
 * @param uiBytes  
 * @param ui32AreaFlags  Heap caching and mapping Flags
 *
 * @return 
 ******************************************************************************/
LinuxMemArea *NewIOLinuxMemArea(IMG_CPU_PHYADDR BasePAddr, IMG_SIZE_T uiBytes, IMG_UINT32 ui32AreaFlags);


/*!
 *******************************************************************************
 * @brief 
 *
 * @param psLinuxMemArea  
 *
 * @return 
 ******************************************************************************/
IMG_VOID FreeIOLinuxMemArea(LinuxMemArea *psLinuxMemArea);


/*!
 *******************************************************************************
 * @brief 
 *
 * @param uiBytes  
 * @param ui32AreaFlags  E.g Heap caching and mapping Flags
 *
 * @return 
 ******************************************************************************/
LinuxMemArea *NewAllocPagesLinuxMemArea(IMG_SIZE_T uiBytes, IMG_UINT32 ui32AreaFlags);


/*!
 *******************************************************************************
 * @brief 
 *
 * @param psLinuxMemArea  
 *
 * @return 
 ******************************************************************************/
IMG_VOID FreeAllocPagesLinuxMemArea(LinuxMemArea *psLinuxMemArea);

/* S.LSI 12.07.24
 * ION allocation feature for CONFIG_ION_EXYNOS is implemented*/
#if defined(CONFIG_ION_OMAP) || defined (CONFIG_ION_EXYNOS)

/*!
 *******************************************************************************
 * @brief 
 *
 * @param uiBytes  
 * @param ui32AreaFlags  E.g Heap caching and mapping Flags
 *
 * @return 
 ******************************************************************************/
LinuxMemArea *
NewIONLinuxMemArea(IMG_SIZE_T uiBytes, IMG_UINT32 ui32AreaFlags,
                   IMG_PVOID pvPrivData, IMG_SIZE_T uiPrivDataLength);


/*!
 *******************************************************************************
 * @brief 
 *
 * @param psLinuxMemArea  
 *
 * @return 
 ******************************************************************************/
IMG_VOID FreeIONLinuxMemArea(LinuxMemArea *psLinuxMemArea);

#else /* defined(CONFIG_ION_OMAP) */

static inline LinuxMemArea *
NewIONLinuxMemArea(IMG_SIZE_T uBytes, IMG_UINT32 ui32AreaFlags,
                   IMG_PVOID pvPrivData, IMG_SIZE_T uPrivDataLength)
{
    PVR_UNREFERENCED_PARAMETER(uBytes);
    PVR_UNREFERENCED_PARAMETER(ui32AreaFlags);
    PVR_UNREFERENCED_PARAMETER(pvPrivData);
    PVR_UNREFERENCED_PARAMETER(uPrivDataLength);
    BUG();
    return IMG_NULL;
}

static inline IMG_VOID FreeIONLinuxMemArea(LinuxMemArea *psLinuxMemArea)
{
    PVR_UNREFERENCED_PARAMETER(psLinuxMemArea);
    BUG();
}

#endif /* defined(CONFIG_ION_OMAP) */


/*!
 *******************************************************************************
 * @brief 
 *
 * @param psParentLinuxMemArea  
 * @param uByteOffset
 * @param uBytes
 *
 * @return 
 ******************************************************************************/
LinuxMemArea *NewSubLinuxMemArea(LinuxMemArea *psParentLinuxMemArea,
                                 IMG_UINTPTR_T uByteOffset,
                                 IMG_SIZE_T uBytes);


/*!
 *******************************************************************************
 * @brief 
 *
 * @param psLinuxMemArea  
 *
 * @return 
 ******************************************************************************/
IMG_VOID LinuxMemAreaDeepFree(LinuxMemArea *psLinuxMemArea);


/*!
 *******************************************************************************
 * @brief For debug builds, LinuxMemAreas are tracked in /proc
 *
 * @param psLinuxMemArea  
 *
 ******************************************************************************/
#if defined(LINUX_MEM_AREAS_DEBUG)
IMG_VOID LinuxMemAreaRegister(LinuxMemArea *psLinuxMemArea);
#else
#define LinuxMemAreaRegister(X)
#endif


/*!
 *******************************************************************************
 * @brief 
 *
 * @param psLinuxMemArea  
 *
 * @return 
 ******************************************************************************/
IMG_VOID *LinuxMemAreaToCpuVAddr(LinuxMemArea *psLinuxMemArea);


/*!
 *******************************************************************************
 * @brief 
 *
 * @param psLinuxMemArea  
 * @param uByteOffset  
 *
 * @return 
 ******************************************************************************/
IMG_CPU_PHYADDR LinuxMemAreaToCpuPAddr(LinuxMemArea *psLinuxMemArea, IMG_UINTPTR_T uByteOffset);


#define	 LinuxMemAreaToCpuPFN(psLinuxMemArea, uByteOffset) PHYS_TO_PFN(LinuxMemAreaToCpuPAddr(psLinuxMemArea, uByteOffset).uiAddr)

/*!
 *******************************************************************************
 * @brief Indicate whether a LinuxMemArea is physically contiguous
 *
 * @param psLinuxMemArea  
 *
 * @return IMG_TRUE if the physical address range is contiguous, else IMG_FALSE
 ******************************************************************************/
IMG_BOOL LinuxMemAreaPhysIsContig(LinuxMemArea *psLinuxMemArea);

/*!
 *******************************************************************************
 * @brief Return the real underlying LinuxMemArea
 *
 * @param psLinuxMemArea  
 *
 * @return The real underlying LinuxMemArea
 ******************************************************************************/
static inline LinuxMemArea *
LinuxMemAreaRoot(LinuxMemArea *psLinuxMemArea)
{
    if(psLinuxMemArea->eAreaType == LINUX_MEM_AREA_SUB_ALLOC)
    {
        return psLinuxMemArea->uData.sSubAlloc.psParentLinuxMemArea;
    }
    else
    {
        return psLinuxMemArea;
    }
}


/*!
 *******************************************************************************
 * @brief Return type of  real underlying LinuxMemArea
 *
 * @param psLinuxMemArea  
 *
 * @return The areas eAreaType or for SUB areas; return the parents eAreaType.
 ******************************************************************************/
static inline LINUX_MEM_AREA_TYPE
LinuxMemAreaRootType(LinuxMemArea *psLinuxMemArea)
{
    return LinuxMemAreaRoot(psLinuxMemArea)->eAreaType;
}


/*!
 *******************************************************************************
 * @brief Converts the enum type of a LinuxMemArea to a const string
 *
 * @param eMemAreaType  
 *
 * @return const string representation of type
 ******************************************************************************/
const IMG_CHAR *LinuxMemAreaTypeToString(LINUX_MEM_AREA_TYPE eMemAreaType);


/*!
 *******************************************************************************
 * @brief 
 *
 * @param ui32Flags  
 *
 * @return 
 ******************************************************************************/
#if defined(DEBUG) || defined(DEBUG_LINUX_MEM_AREAS)
const IMG_CHAR *HAPFlagsToString(IMG_UINT32 ui32Flags);
#endif

#endif /* __IMG_LINUX_MM_H__ */

