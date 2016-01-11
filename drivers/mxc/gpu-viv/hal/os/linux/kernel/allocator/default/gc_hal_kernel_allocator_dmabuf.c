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

#include <linux/dma-buf.h>
#include <linux/platform_device.h>

#define _GC_OBJ_ZONE gcvZONE_OS

/* Descriptor of a dma_buf imported. */
typedef struct _gcsDMABUF
{
    struct dma_buf            *dmabuf;
    struct dma_buf_attachment *attachment;
    struct sg_table           *sgtable;
    unsigned long             *pagearray;
    int                       handle;
    int                       fd;
}
gcsDMABUF;

static gceSTATUS
_DmabufAttach(
    IN gckALLOCATOR Allocator,
    IN gcsATTACH_DESC_PTR Desc,
    IN PLINUX_MDL Mdl
    )
{
    gceSTATUS status;

    gckOS os = Allocator->os;

    int fd = (int) Desc->handle;

    struct dma_buf *dmabuf = NULL;
    struct sg_table *sgt = NULL;
    struct dma_buf_attachment *attachment = NULL;
    int npages = 0;
    unsigned long *pagearray = NULL;
    int i, j, k = 0;
    struct scatterlist *s;
    gcsDMABUF *gcdmabuf = NULL;

    gcmkHEADER();

    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    /* Import dma buf handle. */
    dmabuf = dma_buf_get(fd);

    if (!dmabuf)
    {
        gcmkONERROR(gcvSTATUS_NOT_SUPPORTED);
    }

    attachment = dma_buf_attach(dmabuf, &os->device->platform->device->dev);

    if (!attachment)
    {
        gcmkONERROR(gcvSTATUS_NOT_SUPPORTED);
    }

    sgt = dma_buf_map_attachment(attachment, DMA_BIDIRECTIONAL);

    if (!sgt)
    {
        gcmkONERROR(gcvSTATUS_NOT_SUPPORTED);
    }

    /* Prepare page array. */
    /* Get number of pages. */
    for_each_sg(sgt->sgl, s, sgt->orig_nents, i)
    {
        npages += (sg_dma_len(s) + PAGE_SIZE - 1) / PAGE_SIZE;
    }

    /* Allocate page arrary. */
    gcmkONERROR(gckOS_Allocate(os, npages * gcmSIZEOF(*pagearray), (gctPOINTER *)&pagearray));

    /* Fill page arrary. */
    for_each_sg(sgt->sgl, s, sgt->orig_nents, i)
    {
        for (j = 0; j < (sg_dma_len(s) + PAGE_SIZE - 1) / PAGE_SIZE; j++)
        {
            pagearray[k++] = sg_dma_address(s) + j * PAGE_SIZE;
        }
    }

    /* Prepare descriptor. */
    gcmkONERROR(gckOS_Allocate(os, sizeof(gcsDMABUF), (gctPOINTER *)&gcdmabuf));

    gcdmabuf->fd = fd;
    gcdmabuf->dmabuf = dmabuf;
    gcdmabuf->pagearray = pagearray;
    gcdmabuf->attachment = attachment;
    gcdmabuf->sgtable = sgt;

    /* Record page number. */
    Mdl->numPages = npages;

    Mdl->priv = gcdmabuf;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}


static void
_DmabufFree(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl
    )
{
    gcsDMABUF *gcdmabuf = Mdl->priv;
    gckOS os = Allocator->os;

    dma_buf_unmap_attachment(gcdmabuf->attachment, gcdmabuf->sgtable, DMA_BIDIRECTIONAL);

    dma_buf_detach(gcdmabuf->dmabuf, gcdmabuf->attachment);

    dma_buf_put(gcdmabuf->dmabuf);

    gckOS_Free(os, gcdmabuf->pagearray);

    gckOS_Free(os, gcdmabuf);
}

static gctINT
_DmabufMapUser(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl,
    IN PLINUX_MDL_MAP MdlMap,
    IN gctBOOL Cacheable
    )
{
    gcsDMABUF *gcdmabuf = Mdl->priv;
    gceSTATUS       status;
    PLINUX_MDL      mdl = Mdl;
    PLINUX_MDL_MAP  mdlMap = MdlMap;
    struct file *   file = gcdmabuf->dmabuf->file;

    mdlMap->vmaAddr = (gctSTRING)vm_mmap(file,
                    0L,
                    mdl->numPages * PAGE_SIZE,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED,
                    0);

    if (IS_ERR(mdlMap->vmaAddr))
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
    }

    down_write(&current->mm->mmap_sem);

    mdlMap->vma = find_vma(current->mm, (unsigned long)mdlMap->vmaAddr);

    up_write(&current->mm->mmap_sem);

    if (mdlMap->vma == gcvNULL)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
    }

    return 0;

OnError:
    return status;
}

static void
_DmabufUnmapUser(
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

    vm_munmap((unsigned long)Logical, Size);
}

static gceSTATUS
_DmabufMapKernel(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl,
    OUT gctPOINTER *Logical
    )
{
    /* Kernel doesn't acess video memory. */
    return gcvSTATUS_NOT_SUPPORTED;

}

static gceSTATUS
_DmabufUnmapKernel(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl,
    IN gctPOINTER Logical
    )
{
    /* Kernel doesn't acess video memory. */
    return gcvSTATUS_NOT_SUPPORTED;
}

static gceSTATUS
_DmabufLogicalToPhysical(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl,
    IN gctPOINTER Logical,
    IN gctUINT32 ProcessID,
    OUT gctPHYS_ADDR_T * Physical
    )
{
    return gcvSTATUS_NOT_SUPPORTED;
}


static gceSTATUS
_DmabufCache(
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


static gceSTATUS
_DmabufPhysical(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl,
    IN gctUINT32 Offset,
    OUT gctPHYS_ADDR_T * Physical
    )
{
    gcsDMABUF *gcdmabuf = Mdl->priv;

    *Physical = gcdmabuf->pagearray[Offset];


    return gcvSTATUS_OK;
}

/* Default allocator operations. */
static gcsALLOCATOR_OPERATIONS DmabufAllocatorOperations =
{
    .Attach             = _DmabufAttach,
    .Free               = _DmabufFree,
    .MapUser            = _DmabufMapUser,
    .UnmapUser          = _DmabufUnmapUser,
    .MapKernel          = _DmabufMapKernel,
    .UnmapKernel        = _DmabufUnmapKernel,
    .LogicalToPhysical  = _DmabufLogicalToPhysical,
    .Cache              = _DmabufCache,
    .Physical           = _DmabufPhysical,
};

/* Default allocator entry. */
gceSTATUS
_DmabufAlloctorInit(
    IN gckOS Os,
    OUT gckALLOCATOR * Allocator
    )
{
    gceSTATUS status;
    gckALLOCATOR allocator;

    gcmkONERROR(
        gckALLOCATOR_Construct(Os, &DmabufAllocatorOperations, &allocator));

    allocator->capability = gcvALLOC_FLAG_DMABUF;

    *Allocator = allocator;

    return gcvSTATUS_OK;

OnError:
    return status;
}

