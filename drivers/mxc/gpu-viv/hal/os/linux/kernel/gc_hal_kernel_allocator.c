/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2016 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2016 Vivante Corporation
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/


#include "gc_hal_kernel_linux.h"
#include "gc_hal_kernel_allocator.h"
#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/mman.h>
#include <asm/atomic.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

#include "gc_hal_kernel_allocator_array.h"
#include "gc_hal_kernel_platform.h"

#define _GC_OBJ_ZONE    gcvZONE_OS

typedef struct _gcsDEFAULT_PRIV * gcsDEFAULT_PRIV_PTR;
typedef struct _gcsDEFAULT_PRIV {
    gctUINT32 low;
    gctUINT32 high;
}
gcsDEFAULT_PRIV;

/******************************************************************************\
************************** Default Allocator Debugfs ***************************
\******************************************************************************/

int gc_usage_show(struct seq_file* m, void* data)
{
    gcsINFO_NODE *node = m->private;
    gckALLOCATOR Allocator = node->device;
    gcsDEFAULT_PRIV_PTR priv = Allocator->privateData;

    seq_printf(m, "low:  %u bytes\n", priv->low);
    seq_printf(m, "high: %u bytes\n", priv->high);

    return 0;
}

static gcsINFO InfoList[] =
{
    {"lowHighUsage", gc_usage_show},
};

static void
_DefaultAllocatorDebugfsInit(
    IN gckALLOCATOR Allocator,
    IN gckDEBUGFS_DIR Root
    )
{
    gcmkVERIFY_OK(
        gckDEBUGFS_DIR_Init(&Allocator->debugfsDir, Root->root, "default"));

    gcmkVERIFY_OK(gckDEBUGFS_DIR_CreateFiles(
        &Allocator->debugfsDir,
        InfoList,
        gcmCOUNTOF(InfoList),
        Allocator
        ));
}

static void
_DefaultAllocatorDebugfsCleanup(
    IN gckALLOCATOR Allocator
    )
{
    gcmkVERIFY_OK(gckDEBUGFS_DIR_RemoveFiles(
        &Allocator->debugfsDir,
        InfoList,
        gcmCOUNTOF(InfoList)
        ));

    gckDEBUGFS_DIR_Deinit(&Allocator->debugfsDir);
}


static void
_NonContiguousFree(
    IN struct page ** Pages,
    IN gctUINT32 NumPages
    )
{
    gctINT i;

    gcmkHEADER_ARG("Pages=0x%X, NumPages=%d", Pages, NumPages);

    gcmkASSERT(Pages != gcvNULL);

    for (i = 0; i < NumPages; i++)
    {
        __free_page(Pages[i]);
    }

    if (is_vmalloc_addr(Pages))
    {
        vfree(Pages);
    }
    else
    {
        kfree(Pages);
    }

    gcmkFOOTER_NO();
}

static struct page **
_NonContiguousAlloc(
    IN gctUINT32 NumPages
    )
{
    struct page ** pages;
    struct page *p;
    gctINT i, size;

    gcmkHEADER_ARG("NumPages=%lu", NumPages);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)
    if (NumPages > totalram_pages)
#else
    if (NumPages > num_physpages)
#endif
    {
        gcmkFOOTER_NO();
        return gcvNULL;
    }

    size = NumPages * sizeof(struct page *);

    pages = kmalloc(size, GFP_KERNEL | gcdNOWARN);

    if (!pages)
    {
        pages = vmalloc(size);

        if (!pages)
        {
            gcmkFOOTER_NO();
            return gcvNULL;
        }
    }

    for (i = 0; i < NumPages; i++)
    {
        p = alloc_page(GFP_KERNEL | __GFP_HIGHMEM | gcdNOWARN);

        if (!p)
        {
            _NonContiguousFree(pages, i);
            gcmkFOOTER_NO();
            return gcvNULL;
        }

        pages[i] = p;
    }

    gcmkFOOTER_ARG("pages=0x%X", pages);
    return pages;
}

gctSTRING
_CreateKernelVirtualMapping(
    IN PLINUX_MDL Mdl
    )
{
    gctSTRING addr = 0;
    gctINT numPages = Mdl->numPages;

#if gcdNONPAGED_MEMORY_CACHEABLE
    if (Mdl->contiguous)
    {
        addr = page_address(Mdl->u.contiguousPages);
    }
    else
    {
        addr = vmap(Mdl->u.nonContiguousPages,
                    numPages,
                    0,
                    PAGE_KERNEL);

        /* Trigger a page fault. */
        memset(addr, 0, numPages * PAGE_SIZE);
    }
#else
    struct page ** pages;
    gctBOOL free = gcvFALSE;
    gctINT i;

    if (Mdl->contiguous)
    {
        pages = kmalloc(sizeof(struct page *) * numPages, GFP_KERNEL | gcdNOWARN);

        if (!pages)
        {
            return gcvNULL;
        }

        for (i = 0; i < numPages; i++)
        {
            pages[i] = nth_page(Mdl->u.contiguousPages, i);
        }

        free = gcvTRUE;
    }
    else
    {
        pages = Mdl->u.nonContiguousPages;
    }

    /* ioremap() can't work on system memory since 2.6.38. */
    addr = vmap(pages, numPages, 0, gcmkNONPAGED_MEMROY_PROT(PAGE_KERNEL));

    if (free)
    {
        kfree(pages);
    }

#endif

    return addr;
}

void
_DestoryKernelVirtualMapping(
    IN gctSTRING Addr
    )
{
#if !gcdNONPAGED_MEMORY_CACHEABLE
    vunmap(Addr);
#endif
}

void
_UnmapUserLogical(
    IN gctPOINTER Logical,
    IN gctUINT32  Size
)
{
    if (unlikely(current->mm == gcvNULL))
    {
        /* Do nothing if process is exiting. */
        return;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
    if (vm_munmap((unsigned long)Logical, Size) < 0)
    {
        gcmkTRACE_ZONE(
                gcvLEVEL_WARNING, gcvZONE_OS,
                "%s(%d): vm_munmap failed",
                __FUNCTION__, __LINE__
                );
    }
#else
    down_write(&current->mm->mmap_sem);
    if (do_munmap(current->mm, (unsigned long)Logical, Size) < 0)
    {
        gcmkTRACE_ZONE(
                gcvLEVEL_WARNING, gcvZONE_OS,
                "%s(%d): do_munmap failed",
                __FUNCTION__, __LINE__
                );
    }
    up_write(&current->mm->mmap_sem);
#endif
}

/***************************************************************************\
************************ Default Allocator **********************************
\***************************************************************************/
#define C_MAX_PAGENUM  (50*1024)
static gceSTATUS
_DefaultAlloc(
    IN gckALLOCATOR Allocator,
    INOUT PLINUX_MDL Mdl,
    IN gctSIZE_T NumPages,
    IN gctUINT32 Flags
    )
{
    gceSTATUS status;
    gctUINT32 order;
    gctSIZE_T bytes;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
    gctPOINTER addr = gcvNULL;
#endif
    gctUINT32 numPages;
    gctUINT i = 0;
    gctBOOL contiguous = Flags & gcvALLOC_FLAG_CONTIGUOUS;
    struct sysinfo temsysinfo;
    gcsDEFAULT_PRIV_PTR priv = (gcsDEFAULT_PRIV_PTR)Allocator->privateData;

    gcmkHEADER_ARG("Mdl=%p NumPages=%d", Mdl, NumPages);

    numPages = NumPages;
    bytes = NumPages * PAGE_SIZE;
    order = get_order(bytes);

    si_meminfo(&temsysinfo);

    if (Flags & gcvALLOC_FLAG_MEMLIMIT)
    {
        if ( (temsysinfo.freeram < NumPages) || ((temsysinfo.freeram-NumPages) < C_MAX_PAGENUM) )
        {
            gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
        }
    }

    if (contiguous)
    {
        if (order >= MAX_ORDER)
        {
            gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
        }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
        addr =
            alloc_pages_exact(bytes, GFP_KERNEL | gcdNOWARN | __GFP_NORETRY);

        Mdl->u.contiguousPages = addr
                               ? virt_to_page(addr)
                               : gcvNULL;

        Mdl->exact = gcvTRUE;
#else
        Mdl->u.contiguousPages =
            alloc_pages(GFP_KERNEL | gcdNOWARN | __GFP_NORETRY, order);
#endif

        if (Mdl->u.contiguousPages == gcvNULL)
        {
            Mdl->u.contiguousPages =
                alloc_pages(GFP_KERNEL | __GFP_HIGHMEM | gcdNOWARN, order);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
            Mdl->exact = gcvFALSE;
#endif
        }
    }
    else
    {
        Mdl->u.nonContiguousPages = _NonContiguousAlloc(numPages);
    }

    if (Mdl->u.contiguousPages == gcvNULL && Mdl->u.nonContiguousPages == gcvNULL)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    for (i = 0; i < numPages; i++)
    {
        struct page *page;

        if (contiguous)
        {
            page = nth_page(Mdl->u.contiguousPages, i);
        }
        else
        {
            page = _NonContiguousToPage(Mdl->u.nonContiguousPages, i);
        }

        SetPageReserved(page);

        if (!PageHighMem(page) && page_to_phys(page))
        {
            gcmkVERIFY_OK(
                gckOS_CacheFlush(Allocator->os, _GetProcessID(), gcvNULL,
                                 page_to_phys(page),
                                 page_address(page),
                                 PAGE_SIZE));

            priv->low += PAGE_SIZE;
        }
        else
        {
            flush_dcache_page(page);

#if !gcdCACHE_FUNCTION_UNIMPLEMENTED && defined(CONFIG_OUTER_CACHE) && gcdENABLE_OUTER_CACHE_PATCH
            if (page_to_phys(page))
            {
                _HandleOuterCache(
                    Allocator->os,
                    page_to_phys(page),
                    gcvNULL,
                    PAGE_SIZE,
                    gcvCACHE_FLUSH
                    );
            }
#endif

            priv->high += PAGE_SIZE;
        }
    }

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

static void
_DefaultFree(
    IN gckALLOCATOR Allocator,
    IN OUT PLINUX_MDL Mdl
    )
{
    gctINT i;
    struct page * page;
    gcsDEFAULT_PRIV_PTR priv = (gcsDEFAULT_PRIV_PTR)Allocator->privateData;

    for (i = 0; i < Mdl->numPages; i++)
    {
        if (Mdl->contiguous)
        {
            page = nth_page(Mdl->u.contiguousPages, i);
        }
        else
        {
            page = _NonContiguousToPage(Mdl->u.nonContiguousPages, i);
        }

        ClearPageReserved(page);

        if (PageHighMem(page))
        {
            priv->high -= PAGE_SIZE;
        }
        else
        {
            priv->low -= PAGE_SIZE;
        }
    }

    if (Mdl->contiguous)
    {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
        if (Mdl->exact == gcvTRUE)
        {
            free_pages_exact(page_address(Mdl->u.contiguousPages), Mdl->numPages * PAGE_SIZE);
        }
        else
#endif
        {
            __free_pages(Mdl->u.contiguousPages, get_order(Mdl->numPages * PAGE_SIZE));
        }
    }
    else
    {
        _NonContiguousFree(Mdl->u.nonContiguousPages, Mdl->numPages);
    }
}

gctINT
_DefaultMapUser(
    gckALLOCATOR Allocator,
    PLINUX_MDL Mdl,
    PLINUX_MDL_MAP MdlMap,
    gctBOOL Cacheable
    )
{

    gctSTRING       addr;
    unsigned long   start;
    unsigned long   pfn;
    gctINT i;
    gckOS           os = Allocator->os;
    gcsPLATFORM *   platform = os->device->platform;

    PLINUX_MDL      mdl = Mdl;
    PLINUX_MDL_MAP  mdlMap = MdlMap;

    gcmkHEADER_ARG("Allocator=%p Mdl=%p MdlMap=%p gctBOOL=%d", Allocator, Mdl, MdlMap, Cacheable);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
    mdlMap->vmaAddr = (gctSTRING)vm_mmap(gcvNULL,
                    0L,
                    mdl->numPages * PAGE_SIZE,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED,
                    0);
#else
    down_write(&current->mm->mmap_sem);

    mdlMap->vmaAddr = (gctSTRING)do_mmap_pgoff(gcvNULL,
                    0L,
                    mdl->numPages * PAGE_SIZE,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED,
                    0);

    up_write(&current->mm->mmap_sem);
#endif

    gcmkTRACE_ZONE(
        gcvLEVEL_INFO, gcvZONE_OS,
        "%s(%d): vmaAddr->0x%X for phys_addr->0x%X",
        __FUNCTION__, __LINE__,
        (gctUINT32)(gctUINTPTR_T)mdlMap->vmaAddr,
        (gctUINT32)(gctUINTPTR_T)mdl
        );

    if (IS_ERR(mdlMap->vmaAddr))
    {
        gcmkTRACE_ZONE(
            gcvLEVEL_INFO, gcvZONE_OS,
            "%s(%d): do_mmap_pgoff error",
            __FUNCTION__, __LINE__
            );

        mdlMap->vmaAddr = gcvNULL;

        gcmkFOOTER_ARG("*status=%d", gcvSTATUS_OUT_OF_MEMORY);
        return gcvSTATUS_OUT_OF_MEMORY;
    }

    down_write(&current->mm->mmap_sem);

    mdlMap->vma = find_vma(current->mm, (unsigned long)mdlMap->vmaAddr);

    if (mdlMap->vma == gcvNULL)
    {
        up_write(&current->mm->mmap_sem);

        gcmkTRACE_ZONE(
            gcvLEVEL_INFO, gcvZONE_OS,
            "%s(%d): find_vma error",
            __FUNCTION__, __LINE__
            );

        mdlMap->vmaAddr = gcvNULL;

        gcmkFOOTER_ARG("*status=%d", gcvSTATUS_OUT_OF_RESOURCES);
        return gcvSTATUS_OUT_OF_RESOURCES;
    }

    mdlMap->vma->vm_flags |= gcdVM_FLAGS;

    if (Cacheable == gcvFALSE)
    {
        /* Make this mapping non-cached. */
        mdlMap->vma->vm_page_prot = gcmkPAGED_MEMROY_PROT(mdlMap->vma->vm_page_prot);
    }

    if (platform && platform->ops->adjustProt)
    {
        platform->ops->adjustProt(mdlMap->vma);
    }

    addr = mdl->addr;

    /* Now map all the vmalloc pages to this user address. */
    if (mdl->contiguous)
    {
        /* map kernel memory to user space.. */
        if (remap_pfn_range(mdlMap->vma,
                            mdlMap->vma->vm_start,
                            page_to_pfn(mdl->u.contiguousPages),
                            mdlMap->vma->vm_end - mdlMap->vma->vm_start,
                            mdlMap->vma->vm_page_prot) < 0)
        {
            up_write(&current->mm->mmap_sem);

            gcmkTRACE_ZONE(
                gcvLEVEL_INFO, gcvZONE_OS,
                "%s(%d): unable to mmap ret",
                __FUNCTION__, __LINE__
                );

            mdlMap->vmaAddr = gcvNULL;

            gcmkFOOTER_ARG("*status=%d", gcvSTATUS_OUT_OF_MEMORY);
            return gcvSTATUS_OUT_OF_MEMORY;
        }
    }
    else
    {
        start = mdlMap->vma->vm_start;

        for (i = 0; i < mdl->numPages; i++)
        {
            pfn = _NonContiguousToPfn(mdl->u.nonContiguousPages, i);

            if (remap_pfn_range(mdlMap->vma,
                                start,
                                pfn,
                                PAGE_SIZE,
                                mdlMap->vma->vm_page_prot) < 0)
            {
                up_write(&current->mm->mmap_sem);

                mdlMap->vmaAddr = gcvNULL;

                gcmkFOOTER_ARG("*status=%d", gcvSTATUS_OUT_OF_MEMORY);
                return gcvSTATUS_OUT_OF_MEMORY;
            }

            start += PAGE_SIZE;
            addr += PAGE_SIZE;
        }
    }

    up_write(&current->mm->mmap_sem);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

void
_DefaultUnmapUser(
    IN gckALLOCATOR Allocator,
    IN gctPOINTER Logical,
    IN gctUINT32 Size
    )
{
    _UnmapUserLogical(Logical, Size);
}

gceSTATUS
_DefaultMapKernel(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl,
    OUT gctPOINTER *Logical
    )
{
    *Logical = _CreateKernelVirtualMapping(Mdl);
    return gcvSTATUS_OK;
}

gceSTATUS
_DefaultUnmapKernel(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl,
    IN gctPOINTER Logical
    )
{
    _DestoryKernelVirtualMapping(Logical);
    return gcvSTATUS_OK;
}

gceSTATUS
_DefaultLogicalToPhysical(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl,
    IN gctPOINTER Logical,
    IN gctUINT32 ProcessID,
    OUT gctPHYS_ADDR_T * Physical
    )
{
    return _ConvertLogical2Physical(
                Allocator->os, Logical, ProcessID, Mdl, Physical);
}

gceSTATUS
_DefaultCache(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl,
    IN gctPOINTER Logical,
    IN gctUINT32 Physical,
    IN gctUINT32 Bytes,
    IN gceCACHEOPERATION Operation
    )
{
    return gcvSTATUS_OK;
}

gceSTATUS
_DefaultPhysical(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl,
    IN gctUINT32 Offset,
    OUT gctPHYS_ADDR_T * Physical
    )
{
    gcmkASSERT(Mdl->pagedMem);

    if (Mdl->contiguous)
    {
        *Physical = page_to_phys(nth_page(Mdl->u.contiguousPages, Offset));
    }
    else
    {
        *Physical = _NonContiguousToPhys(Mdl->u.nonContiguousPages, Offset);
    }

    return gcvSTATUS_OK;
}

void
_DefaultAllocatorDestructor(
    IN void* PrivateData
    )
{
    kfree(PrivateData);
}

/* Default allocator operations. */
gcsALLOCATOR_OPERATIONS DefaultAllocatorOperations = {
    .Alloc              = _DefaultAlloc,
    .Free               = _DefaultFree,
    .MapUser            = _DefaultMapUser,
    .UnmapUser          = _DefaultUnmapUser,
    .MapKernel          = _DefaultMapKernel,
    .UnmapKernel        = _DefaultUnmapKernel,
    .LogicalToPhysical  = _DefaultLogicalToPhysical,
    .Cache              = _DefaultCache,
    .Physical           = _DefaultPhysical,
};

/* Default allocator entry. */
gceSTATUS
_DefaultAlloctorInit(
    IN gckOS Os,
    OUT gckALLOCATOR * Allocator
    )
{
    gceSTATUS status;
    gckALLOCATOR allocator;
    gcsDEFAULT_PRIV_PTR priv = gcvNULL;

    gcmkONERROR(
        gckALLOCATOR_Construct(Os, &DefaultAllocatorOperations, &allocator));

    priv = kzalloc(gcmSIZEOF(gcsDEFAULT_PRIV), GFP_KERNEL | gcdNOWARN);

    if (!priv)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    /* Register private data. */
    allocator->privateData = priv;
    allocator->privateDataDestructor = _DefaultAllocatorDestructor;

    allocator->debugfsInit = _DefaultAllocatorDebugfsInit;
    allocator->debugfsCleanup = _DefaultAllocatorDebugfsCleanup;

    allocator->capability = gcvALLOC_FLAG_CONTIGUOUS
                          | gcvALLOC_FLAG_NON_CONTIGUOUS
                          | gcvALLOC_FLAG_CACHEABLE
                          | gcvALLOC_FLAG_MEMLIMIT
                          ;

    *Allocator = allocator;

    return gcvSTATUS_OK;

OnError:
    return status;
}

/***************************************************************************\
************************ Allocator helper ***********************************
\***************************************************************************/

gceSTATUS
gckALLOCATOR_Construct(
    IN gckOS Os,
    IN gcsALLOCATOR_OPERATIONS * Operations,
    OUT gckALLOCATOR * Allocator
    )
{
    gceSTATUS status;
    gckALLOCATOR allocator;

    gcmkHEADER_ARG("Os=%p, Operations=%p, Allocator=%p",
                   Os, Operations, Allocator);

    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Allocator != gcvNULL);
    gcmkVERIFY_ARGUMENT
        (  Operations
        && (Operations->Alloc || Operations->Attach)
        && (Operations->Free)
        && Operations->MapUser
        && Operations->UnmapUser
        && Operations->MapKernel
        && Operations->UnmapKernel
        && Operations->LogicalToPhysical
        && Operations->Cache
        && Operations->Physical
        );

    gcmkONERROR(
        gckOS_Allocate(Os, gcmSIZEOF(gcsALLOCATOR), (gctPOINTER *)&allocator));

    gckOS_ZeroMemory(allocator, gcmSIZEOF(gcsALLOCATOR));

    /* Record os. */
    allocator->os = Os;

    /* Set operations. */
    allocator->ops = Operations;

    *Allocator = allocator;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

/******************************************************************************\
******************************** Debugfs Support *******************************
\******************************************************************************/

static gceSTATUS
_AllocatorDebugfsInit(
    IN gckOS Os
    )
{
    gceSTATUS status;
    gckGALDEVICE device = Os->device;

    gckDEBUGFS_DIR dir = &Os->allocatorDebugfsDir;

    gcmkONERROR(gckDEBUGFS_DIR_Init(dir, device->debugfsDir.root, "allocators"));

    return gcvSTATUS_OK;

OnError:
    return status;
}

static void
_AllocatorDebugfsCleanup(
    IN gckOS Os
    )
{
    gckDEBUGFS_DIR dir = &Os->allocatorDebugfsDir;

    gckDEBUGFS_DIR_Deinit(dir);
}

/***************************************************************************\
************************ Allocator management *******************************
\***************************************************************************/

gceSTATUS
gckOS_ImportAllocators(
    gckOS Os
    )
{
    gceSTATUS status;
    gctUINT i;
    gckALLOCATOR allocator;

    _AllocatorDebugfsInit(Os);

    INIT_LIST_HEAD(&Os->allocatorList);

    for (i = 0; i < gcmCOUNTOF(allocatorArray); i++)
    {
        if (allocatorArray[i].construct)
        {
            /* Construct allocator. */
            status = allocatorArray[i].construct(Os, &allocator);

            if (gcmIS_ERROR(status))
            {
                gcmkPRINT("["DEVICE_NAME"]: Can't construct allocator(%s)",
                          allocatorArray[i].name);

                continue;
            }

            allocator->name = allocatorArray[i].name;

            if (allocator->debugfsInit)
            {
                /* Init allocator's debugfs. */
                allocator->debugfsInit(allocator, &Os->allocatorDebugfsDir);
            }

            list_add_tail(&allocator->head, &Os->allocatorList);
        }
    }

#if gcdDEBUG
    list_for_each_entry(allocator, &Os->allocatorList, head)
    {
        gcmkTRACE_ZONE(
            gcvLEVEL_WARNING, gcvZONE_OS,
            "%s(%d) Allocator: %s",
            __FUNCTION__, __LINE__,
            allocator->name
            );
    }
#endif

    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_FreeAllocators(
    gckOS Os
    )
{
    gckALLOCATOR allocator;
    gckALLOCATOR temp;

    list_for_each_entry_safe(allocator, temp, &Os->allocatorList, head)
    {
        list_del(&allocator->head);

        if (allocator->debugfsCleanup)
        {
            /* Clean up allocator's debugfs. */
            allocator->debugfsCleanup(allocator);
        }

        /* Free private data. */
        if (allocator->privateDataDestructor && allocator->privateData)
        {
            allocator->privateDataDestructor(allocator->privateData);
        }

        gckOS_Free(Os, allocator);
    }

    _AllocatorDebugfsCleanup(Os);

    return gcvSTATUS_OK;
}

