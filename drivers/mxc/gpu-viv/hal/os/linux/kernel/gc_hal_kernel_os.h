/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 Vivante Corporation
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
*    Copyright (C) 2014  Vivante Corporation
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


#ifndef __gc_hal_kernel_os_h_
#define __gc_hal_kernel_os_h_

typedef struct _LINUX_MDL_MAP
{
    gctINT                  pid;
    gctPOINTER              vmaAddr;
    gctUINT32               count;
    struct vm_area_struct * vma;
    struct _LINUX_MDL_MAP * next;
}
LINUX_MDL_MAP;

typedef struct _LINUX_MDL_MAP * PLINUX_MDL_MAP;

typedef struct _LINUX_MDL
{
    char *                  addr;

    union _pages
    {
        /* Pointer to a array of pages. */
        struct page *       contiguousPages;
        /* Pointer to a array of pointers to page. */
        struct page **      nonContiguousPages;
    }
    u;

#ifdef NO_DMA_COHERENT
    gctPOINTER              kaddr;
#endif /* NO_DMA_COHERENT */

    gctINT                  numPages;
    gctINT                  pagedMem;
    gctBOOL                 contiguous;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
    gctBOOL                 exact;
#endif
    dma_addr_t              dmaHandle;
    PLINUX_MDL_MAP          maps;
    struct _LINUX_MDL *     prev;
    struct _LINUX_MDL *     next;

    /* Pointer to allocator which allocates memory for this mdl. */
    void *                  allocator;

    /* Private data used by allocator. */
    void *                  priv;

    uint                    gid;
}
LINUX_MDL, *PLINUX_MDL;

extern PLINUX_MDL_MAP
FindMdlMap(
    IN PLINUX_MDL Mdl,
    IN gctINT PID
    );

typedef struct _DRIVER_ARGS
{
    gctUINT64               InputBuffer;
    gctUINT64               InputBufferSize;
    gctUINT64               OutputBuffer;
    gctUINT64               OutputBufferSize;
}
DRIVER_ARGS;

#endif /* __gc_hal_kernel_os_h_ */
