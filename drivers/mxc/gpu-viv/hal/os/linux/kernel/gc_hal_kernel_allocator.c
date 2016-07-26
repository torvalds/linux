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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)
#include <linux/anon_inodes.h>
#endif
#include <linux/file.h>

#include "gc_hal_kernel_allocator_array.h"
#include "gc_hal_kernel_platform.h"

#define _GC_OBJ_ZONE    gcvZONE_OS

#define gcdDISCRETE_PAGES 0

typedef struct _gcsDEFAULT_PRIV * gcsDEFAULT_PRIV_PTR;
typedef struct _gcsDEFAULT_PRIV {
    gctUINT32 low;
    gctUINT32 high;
}
gcsDEFAULT_PRIV;

typedef struct _gcsDEFAULT_MDL_PRIV *gcsDEFAULT_MDL_PRIV_PTR;
typedef struct _gcsDEFAULT_MDL_PRIV {
    union _pages
    {
        /* Pointer to a array of pages. */
        struct page *       contiguousPages;
        /* Pointer to a array of pointers to page. */
        struct page **      nonContiguousPages;
    }
    u;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
    gctBOOL                 exact;
#endif

    struct file *           file;

    gctBOOL                 cacheable;

    gcsPLATFORM *           platform;

    gctBOOL                 contiguous;
}
gcsDEFAULT_MDL_PRIV;

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

static inline struct page *
_NonContiguousToPage(
    IN struct page ** Pages,
    IN gctUINT32 Index
    )
{
    gcmkASSERT(Pages != gcvNULL);
    return Pages[Index];
}

static inline unsigned long
_NonContiguousToPfn(
    IN struct page ** Pages,
    IN gctUINT32 Index
    )
{
    gcmkASSERT(Pages != gcvNULL);
    return page_to_pfn(_NonContiguousToPage(Pages, Index));
}

static inline unsigned long
_NonContiguousToPhys(
    IN struct page ** Pages,
    IN gctUINT32 Index
    )
{
    gcmkASSERT(Pages != gcvNULL);
    return page_to_phys(_NonContiguousToPage(Pages, Index));
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
#if gcdDISCRETE_PAGES
    struct page *l;
#endif
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

#if gcdDISCRETE_PAGES
        if (i != 0)
        {
            if (page_to_pfn(pages[i-1]) == page_to_pfn(p)-1)
            {
                /* Replaced page. */
                l = p;

                /* Allocate a page which is not contiguous to previous one. */
                p = alloc_page(GFP_KERNEL | __GFP_HIGHMEM | __GFP_NOWARN);

                /* Give replaced page back. */
                __free_page(l);

                if (!p)
                {
                    _NonContiguousFree(pages, i);
                    gcmkFOOTER_NO();
                    return gcvNULL;
                }
            }
        }
#endif

        pages[i] = p;
    }

    gcmkFOOTER_ARG("pages=0x%X", pages);
    return pages;
}

static gctSTRING
_CreateKernelVirtualMapping(
    IN PLINUX_MDL Mdl
    )
{
    gctSTRING addr = 0;
    gctINT numPages = Mdl->numPages;
    gcsDEFAULT_MDL_PRIV_PTR mdlPriv = Mdl->priv;

#if gcdNONPAGED_MEMORY_CACHEABLE
    if (Mdl->contiguous)
    {
        addr = page_address(mdlPriv->u.contiguousPages);
    }
    else
    {
        addr = vmap(mdlPriv->u.nonContiguousPages,
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
            pages[i] = nth_page(mdlPriv->u.contiguousPages, i);
        }

        free = gcvTRUE;
    }
    else
    {
        pages = mdlPriv->u.nonContiguousPages;
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

static void
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

static gceSTATUS
default_mmap_internal(
    IN gcsDEFAULT_MDL_PRIV_PTR MdlPriv,
    IN struct vm_area_struct *vma
    )
{
    gcsPLATFORM * platform = MdlPriv->platform;
    gctUINT32 numPages;
    unsigned long start;
    unsigned long pfn;
    gctINT i;

    gcmkHEADER();

    vma->vm_flags |= gcdVM_FLAGS;

    if (MdlPriv->cacheable == gcvFALSE)
    {
        /* Make this mapping non-cached. */
        vma->vm_page_prot = gcmkPAGED_MEMROY_PROT(vma->vm_page_prot);
    }

    if (platform && platform->ops->adjustProt)
    {
        platform->ops->adjustProt(vma);
    }

    /* Now map all the vmalloc pages to this user address. */
    if (MdlPriv->contiguous)
    {
        /* map kernel memory to user space.. */
        if (remap_pfn_range(vma,
                            vma->vm_start,
                            page_to_pfn(MdlPriv->u.contiguousPages),
                            vma->vm_end - vma->vm_start,
                            vma->vm_page_prot) < 0)
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_INFO, gcvZONE_OS,
                "%s(%d): unable to mmap ret",
                __FUNCTION__, __LINE__
                );

            gcmkFOOTER_ARG("*status=%d", gcvSTATUS_OUT_OF_MEMORY);
            return gcvSTATUS_OUT_OF_MEMORY;
        }
    }
    else
    {
        numPages = (vma->vm_end - vma->vm_start) / PAGE_SIZE;
        start = vma->vm_start;

        for (i = 0; i < numPages; i++)
        {
            pfn = _NonContiguousToPfn(MdlPriv->u.nonContiguousPages, i);

            if (remap_pfn_range(vma,
                                start,
                                pfn,
                                PAGE_SIZE,
                                vma->vm_page_prot) < 0)
            {
                gcmkFOOTER_ARG("*status=%d", gcvSTATUS_OUT_OF_MEMORY);
                return gcvSTATUS_OUT_OF_MEMORY;
            }

            start += PAGE_SIZE;
        }
    }

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)
static int default_mmap(struct file *file, struct vm_area_struct *vma)
{
    gcsDEFAULT_MDL_PRIV_PTR mdlPriv = file->private_data;

    if (gcmIS_ERROR(default_mmap_internal(mdlPriv, vma)))
    {
        return -EINVAL;
    }

    return 0;
}

static const struct file_operations default_fops = {
    .mmap = default_mmap,
};
#endif

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
    gcsDEFAULT_MDL_PRIV_PTR mdlPriv = gcvNULL;

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

    gcmkONERROR(gckOS_Allocate(Allocator->os, gcmSIZEOF(gcsDEFAULT_MDL_PRIV), (gctPOINTER *)&mdlPriv));

    gckOS_ZeroMemory(mdlPriv, gcmSIZEOF(gcsDEFAULT_MDL_PRIV));

    if (contiguous)
    {
        if (order >= MAX_ORDER)
        {
            gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
        }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
        addr =
            alloc_pages_exact(bytes, GFP_KERNEL | gcdNOWARN | __GFP_NORETRY);

        mdlPriv->u.contiguousPages = addr
                               ? virt_to_page(addr)
                               : gcvNULL;

        mdlPriv->exact = gcvTRUE;
#else
        mdlPriv->u.contiguousPages =
            alloc_pages(GFP_KERNEL | gcdNOWARN | __GFP_NORETRY, order);
#endif

        if (mdlPriv->u.contiguousPages == gcvNULL)
        {
            mdlPriv->u.contiguousPages =
                alloc_pages(GFP_KERNEL | __GFP_HIGHMEM | gcdNOWARN, order);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
            mdlPriv->exact = gcvFALSE;
#endif
        }
    }
    else
    {
        mdlPriv->u.nonContiguousPages = _NonContiguousAlloc(numPages);
    }

    if (mdlPriv->u.contiguousPages == gcvNULL && mdlPriv->u.nonContiguousPages == gcvNULL)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    for (i = 0; i < numPages; i++)
    {
        struct page *page;
        gctPHYS_ADDR_T phys = 0U;

        if (contiguous)
        {
            page = nth_page(mdlPriv->u.contiguousPages, i);
        }
        else
        {
            page = _NonContiguousToPage(mdlPriv->u.nonContiguousPages, i);
        }

        SetPageReserved(page);

        phys = page_to_phys(page);

        BUG_ON(!phys);

        if (PageHighMem(page))
        {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
            void *vaddr = kmap_atomic(page);
#else
            void *vaddr = kmap_atomic(page, KM_USER0);
#endif

            gcmkVERIFY_OK(gckOS_CacheFlush(
                Allocator->os, _GetProcessID(), gcvNULL, phys, vaddr, PAGE_SIZE
                ));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
            kunmap_atomic(vaddr);
#else
            kunmap_atomic(vaddr, KM_USER0);
#endif

            priv->high += PAGE_SIZE;
        }
        else
        {
            gcmkVERIFY_OK(gckOS_CacheFlush(
                Allocator->os, _GetProcessID(), gcvNULL, phys, page_address(page), PAGE_SIZE
                ));

            priv->low += PAGE_SIZE;
        }
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)
    mdlPriv->file = anon_inode_getfile("default", &default_fops, mdlPriv, O_RDWR);

    if (IS_ERR(mdlPriv->file))
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }
#endif

    mdlPriv->platform = Allocator->os->device->platform;
    mdlPriv->contiguous = contiguous;

    Mdl->priv = mdlPriv;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:

    if (mdlPriv)
    {
        gcmkOS_SAFE_FREE(Allocator->os, mdlPriv);
    }

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
    gcsDEFAULT_MDL_PRIV_PTR mdlPriv = Mdl->priv;

    for (i = 0; i < Mdl->numPages; i++)
    {
        if (Mdl->contiguous)
        {
            page = nth_page(mdlPriv->u.contiguousPages, i);
        }
        else
        {
            page = _NonContiguousToPage(mdlPriv->u.nonContiguousPages, i);
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
        if (mdlPriv->exact == gcvTRUE)
        {
            free_pages_exact(page_address(mdlPriv->u.contiguousPages), Mdl->numPages * PAGE_SIZE);
        }
        else
#endif
        {
            __free_pages(mdlPriv->u.contiguousPages, get_order(Mdl->numPages * PAGE_SIZE));
        }
    }
    else
    {
        _NonContiguousFree(mdlPriv->u.nonContiguousPages, Mdl->numPages);
    }

    if (mdlPriv->file != gcvNULL)
    {
        fput(mdlPriv->file);
    }

    gcmkOS_SAFE_FREE(Allocator->os, Mdl->priv);
}

gctINT
_DefaultMapUser(
    gckALLOCATOR Allocator,
    PLINUX_MDL Mdl,
    gctBOOL Cacheable,
    OUT gctPOINTER * UserLogical
    )
{
    gckOS           os = Allocator->os;

    PLINUX_MDL      mdl = Mdl;
    gctPOINTER      userLogical = gcvNULL;
    struct vm_area_struct * vma;
    gcsDEFAULT_MDL_PRIV_PTR mdlPriv = Mdl->priv;

    gcmkHEADER_ARG("Allocator=%p Mdl=%p gctBOOL=%d", Allocator, Mdl, Cacheable);

    /* mdlPriv->cacheable must be used under protection of mdl->mapMutex. */
    mdlPriv->cacheable = Cacheable;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
    userLogical = (gctSTRING)vm_mmap(mdlPriv->file,
                    0L,
                    mdl->numPages * PAGE_SIZE,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED,
                    0);
#else
    down_write(&current->mm->mmap_sem);

    userLogical = (gctSTRING)do_mmap_pgoff(mdlPriv->file,
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
        (gctUINT32)(gctUINTPTR_T)userLogical,
        (gctUINT32)(gctUINTPTR_T)mdl
        );

    if (IS_ERR(userLogical))
    {
        gcmkTRACE_ZONE(
            gcvLEVEL_INFO, gcvZONE_OS,
            "%s(%d): do_mmap_pgoff error",
            __FUNCTION__, __LINE__
            );

        userLogical = gcvNULL;

        gcmkFOOTER_ARG("*status=%d", gcvSTATUS_OUT_OF_MEMORY);
        return gcvSTATUS_OUT_OF_MEMORY;
    }

    if (mdlPriv->file == gcvNULL)
    {
        /* Remap here since there is no file and ops->mmap(). */
        down_write(&current->mm->mmap_sem);

        vma = find_vma(current->mm, (unsigned long)userLogical);

        if (vma == gcvNULL)
        {
            up_write(&current->mm->mmap_sem);

            gcmkTRACE_ZONE(
                gcvLEVEL_INFO, gcvZONE_OS,
                "%s(%d): find_vma error",
                __FUNCTION__, __LINE__
                );

            gcmkFOOTER_ARG("*status=%d", gcvSTATUS_OUT_OF_RESOURCES);
            return gcvSTATUS_OUT_OF_RESOURCES;
        }

        if (gcmIS_ERROR(default_mmap_internal(mdlPriv, vma)))
        {
            up_write(&current->mm->mmap_sem);

            gcmkFOOTER_ARG("*status=%d", gcvSTATUS_OUT_OF_MEMORY);
            return gcvSTATUS_OUT_OF_MEMORY;
        }

        up_write(&current->mm->mmap_sem);
    }

    gcmkVERIFY_OK(gckOS_CacheFlush(
        os,
        _GetProcessID(),
        mdl,
        gcvINVALID_ADDRESS,
        userLogical,
        mdl->numPages * PAGE_SIZE
        ));

    *UserLogical = userLogical;

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
    gcsDEFAULT_MDL_PRIV_PTR mdlPriv = Mdl->priv;
    gctUINT32 offsetInPage = Offset & ~PAGE_MASK;
    gctUINT32 index = Offset / PAGE_SIZE;

    if (Mdl->contiguous)
    {
        *Physical = page_to_phys(nth_page(mdlPriv->u.contiguousPages, index));
    }
    else
    {
        *Physical = _NonContiguousToPhys(mdlPriv->u.nonContiguousPages, index);
    }

    *Physical += offsetInPage;

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

#if defined(gcdEMULATE_SECURE_ALLOCATOR)
    allocator->capability |= gcvALLOC_FLAG_SECURITY;
#endif

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

