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
#include <linux/platform_device.h>

#define _GC_OBJ_ZONE    gcvZONE_OS

typedef struct _gcsDMA_PRIV * gcsDMA_PRIV_PTR;
typedef struct _gcsDMA_PRIV {
    gctUINT32 size;
}
gcsDMA_PRIV;

struct mdl_dma_priv {
    gctPOINTER kvaddr;
    dma_addr_t dmaHandle;
};

/*
* Debugfs support.
*/
int gc_dma_usage_show(struct seq_file* m, void* data)
{
    gcsINFO_NODE *node = m->private;
    gckALLOCATOR Allocator = node->device;
    gcsDMA_PRIV_PTR priv = Allocator->privateData;

    seq_printf(m, "dma:  %u bytes\n", priv->size);

    return 0;
}

static gcsINFO InfoList[] =
{
    {"dmausage", gc_dma_usage_show},
};

static void
_DebugfsInit(
    IN gckALLOCATOR Allocator,
    IN gckDEBUGFS_DIR Root
    )
{
    gcmkVERIFY_OK(
        gckDEBUGFS_DIR_Init(&Allocator->debugfsDir, Root->root, "dma"));

    gcmkVERIFY_OK(gckDEBUGFS_DIR_CreateFiles(
        &Allocator->debugfsDir,
        InfoList,
        gcmCOUNTOF(InfoList),
        Allocator
        ));
}

static void
_DebugfsCleanup(
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

#ifdef CONFIG_ARM64
static struct device *
_GetDevice(
    IN gckOS Os
    )
{
    gcsPLATFORM *platform;

    platform = Os->device->platform;

    if (!platform)
    {
        return gcvNULL;
    }

    return &platform->device->dev;
}
#endif

static gceSTATUS
_DmaAlloc(
    IN gckALLOCATOR Allocator,
    INOUT PLINUX_MDL Mdl,
    IN gctSIZE_T NumPages,
    IN gctUINT32 Flags
    )
{
    gceSTATUS status;
    gcsDMA_PRIV_PTR allocatorPriv = (gcsDMA_PRIV_PTR)Allocator->privateData;

    struct mdl_dma_priv *mdlPriv=gcvNULL;
    gckOS os = Allocator->os;

#if defined CONFIG_ARM64
    struct device *dev = _GetDevice(os);
#endif

    gcmkHEADER_ARG("Mdl=%p NumPages=%d", Mdl, NumPages);

#if defined CONFIG_ARM64
    gcmkVERIFY_ARGUMENT(dev);
#endif

    gcmkONERROR(gckOS_Allocate(os, sizeof(struct mdl_dma_priv), (gctPOINTER *)&mdlPriv));

    mdlPriv->kvaddr
#if defined CONFIG_ARM64
        = dma_alloc_coherent(dev, NumPages * PAGE_SIZE, &mdlPriv->dmaHandle, GFP_KERNEL | gcdNOWARN);
#elif defined CONFIG_MIPS
        = dma_alloc_coherent(gcvNULL, NumPages * PAGE_SIZE, &mdlPriv->dmaHandle, GFP_KERNEL | gcdNOWARN);
#else
        = dma_alloc_writecombine(gcvNULL, NumPages * PAGE_SIZE,  &mdlPriv->dmaHandle, GFP_KERNEL | gcdNOWARN);
#endif

#ifdef CONFLICT_BETWEEN_BASE_AND_PHYS
    if ((os->device->baseAddress & 0x80000000) != (mdlPriv->dmaHandle & 0x80000000))
    {
        mdlPriv->dmaHandle = (mdlPriv->dmaHandle & ~0x80000000)
                            | (os->device->baseAddress & 0x80000000);
    }
#endif

    if (mdlPriv->kvaddr == gcvNULL)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    Mdl->priv = mdlPriv;

    Mdl->dmaHandle = mdlPriv->dmaHandle;

    /* Statistic. */
    allocatorPriv->size += NumPages * PAGE_SIZE;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if(mdlPriv)
    {
        gckOS_Free(os, mdlPriv);
    }

    gcmkFOOTER();
    return status;
}

static void
_DmaFree(
    IN gckALLOCATOR Allocator,
    IN OUT PLINUX_MDL Mdl
    )
{
    gckOS os = Allocator->os;
    struct mdl_dma_priv *mdlPriv=(struct mdl_dma_priv *)Mdl->priv;
    gcsDMA_PRIV_PTR allocatorPriv = (gcsDMA_PRIV_PTR)Allocator->privateData;

#if defined CONFIG_ARM64 || defined CONFIG_MIPS
    dma_free_coherent(gcvNULL, Mdl->numPages * PAGE_SIZE, mdlPriv->kvaddr, mdlPriv->dmaHandle);
#else
    dma_free_writecombine(gcvNULL, Mdl->numPages * PAGE_SIZE, mdlPriv->kvaddr, mdlPriv->dmaHandle);
#endif

    gckOS_Free(os, mdlPriv);

    /* Statistic. */
    allocatorPriv->size -= Mdl->numPages * PAGE_SIZE;
}

gctINT
_DmaMapUser(
    gckALLOCATOR Allocator,
    PLINUX_MDL Mdl,
    gctBOOL Cacheable,
    OUT gctPOINTER * UserLogical
    )
{

    PLINUX_MDL      mdl = Mdl;
    struct mdl_dma_priv *mdlPriv=(struct mdl_dma_priv *)Mdl->priv;
    struct vm_area_struct * vma;
    gctPOINTER      userLogical = gcvNULL;

    gcmkHEADER_ARG("Allocator=%p Mdl=%p gctBOOL=%d", Allocator, Mdl, Cacheable);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
    userLogical = (gctSTRING)vm_mmap(gcvNULL,
                    0L,
                    mdl->numPages * PAGE_SIZE,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED,
                    0);
#else
    down_write(&current->mm->mmap_sem);

    userLogical = (gctSTRING)do_mmap_pgoff(gcvNULL,
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

        gcmkFOOTER_ARG("*status=%d", gcvSTATUS_OUT_OF_MEMORY);
        return gcvSTATUS_OUT_OF_MEMORY;
    }

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

    /* map kernel memory to user space.. */
#if defined CONFIG_MIPS
    if (remap_pfn_range(
            vma,
            vma->vm_start,
            mdlPriv->dmaHandle >> PAGE_SHIFT,
            mdl->numPages * PAGE_SIZE,
            gcmkNONPAGED_MEMROY_PROT(vma->vm_page_prot)) < 0)
#else
    /* map kernel memory to user space.. */
    if (dma_mmap_writecombine(gcvNULL,
            vma,
            mdlPriv->kvaddr,
            mdlPriv->dmaHandle,
            mdl->numPages * PAGE_SIZE) < 0)
#endif
    {
        up_write(&current->mm->mmap_sem);

        gcmkTRACE_ZONE(
            gcvLEVEL_WARNING, gcvZONE_OS,
            "%s(%d): dma_mmap_attrs error",
            __FUNCTION__, __LINE__
            );

        gcmkFOOTER_ARG("*status=%d", gcvSTATUS_OUT_OF_MEMORY);
        return gcvSTATUS_OUT_OF_MEMORY;
    }

    up_write(&current->mm->mmap_sem);

    *UserLogical = userLogical;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

void
_DmaUnmapUser(
    IN gckALLOCATOR Allocator,
    IN gctPOINTER Logical,
    IN gctUINT32 Size
    )
{
    if (unlikely(current->mm == gcvNULL))
    {
        /* Do nothing if process is exiting. */
        return;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
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

gceSTATUS
_DmaMapKernel(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl,
    OUT gctPOINTER *Logical
    )
{
    struct mdl_dma_priv *mdlPriv=(struct mdl_dma_priv *)Mdl->priv;
    *Logical =mdlPriv->kvaddr;
    return gcvSTATUS_OK;
}

gceSTATUS
_DmaUnmapKernel(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl,
    IN gctPOINTER Logical
    )
{
    return gcvSTATUS_OK;
}

extern gceSTATUS
_DefaultCache(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl,
    IN gctPOINTER Logical,
    IN gctUINT32 Physical,
    IN gctUINT32 Bytes,
    IN gceCACHEOPERATION Operation
    );

gceSTATUS
_DmaPhysical(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl,
    IN gctUINT32 Offset,
    OUT gctPHYS_ADDR_T * Physical
    )
{
    struct mdl_dma_priv *mdlPriv=(struct mdl_dma_priv *)Mdl->priv;

    *Physical = mdlPriv->dmaHandle + Offset;

    return gcvSTATUS_OK;
}

extern void
_DefaultAllocatorDestructor(
    IN void* PrivateData
    );

/* Default allocator operations. */
gcsALLOCATOR_OPERATIONS DmaAllocatorOperations = {
    .Alloc              = _DmaAlloc,
    .Free               = _DmaFree,
    .MapUser            = _DmaMapUser,
    .UnmapUser          = _DmaUnmapUser,
    .MapKernel          = _DmaMapKernel,
    .UnmapKernel        = _DmaUnmapKernel,
    .Cache              = _DefaultCache,
    .Physical           = _DmaPhysical,
};

/* Default allocator entry. */
gceSTATUS
_DmaAlloctorInit(
    IN gckOS Os,
    OUT gckALLOCATOR * Allocator
    )
{
    gceSTATUS status;
    gckALLOCATOR allocator = gcvNULL;
    gcsDMA_PRIV_PTR priv = gcvNULL;

    gcmkONERROR(gckALLOCATOR_Construct(Os, &DmaAllocatorOperations, &allocator));

    priv = kzalloc(gcmSIZEOF(gcsDMA_PRIV), GFP_KERNEL | gcdNOWARN);

    if (!priv)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    /* Register private data. */
    allocator->privateData = priv;
    allocator->privateDataDestructor = _DefaultAllocatorDestructor;

    /* Register debugfs callbacks. */
    allocator->debugfsInit = _DebugfsInit;
    allocator->debugfsCleanup = _DebugfsCleanup;

    /* dma allocator is only used when NO_DMA_COHERENT is not defined. */
    allocator->capability = gcvALLOC_FLAG_NONE;

    *Allocator = allocator;

    return gcvSTATUS_OK;

OnError:
    if(allocator != gcvNULL)
        gckOS_Free(Os, (gctPOINTER)allocator);
    return status;
}

