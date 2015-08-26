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


#ifndef __gc_hal_kernel_linux_h_
#define __gc_hal_kernel_linux_h_

#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/signal.h>
#ifdef FLAREON
#   include <asm/arch-realview/dove_gpio_irq.h>
#endif
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>

#include <linux/idr.h>

#ifdef MODVERSIONS
#  include <linux/modversions.h>
#endif
#include <asm/io.h>
#include <asm/uaccess.h>

#if ENABLE_GPU_CLOCK_BY_DRIVER && LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
#include <linux/clk.h>
#endif

#define NTSTRSAFE_NO_CCH_FUNCTIONS
#include "gc_hal.h"
#include "gc_hal_driver.h"
#include "gc_hal_kernel.h"
#include "gc_hal_kernel_platform.h"
#include "gc_hal_kernel_device.h"
#include "gc_hal_kernel_os.h"
#include "gc_hal_kernel_debugfs.h"


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
#define FIND_TASK_BY_PID(x) pid_task(find_vpid(x), PIDTYPE_PID)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
#define FIND_TASK_BY_PID(x) find_task_by_vpid(x)
#else
#define FIND_TASK_BY_PID(x) find_task_by_pid(x)
#endif

#define _WIDE(string)                L##string
#define WIDE(string)                _WIDE(string)

#define countof(a)                    (sizeof(a) / sizeof(a[0]))

#ifndef DEVICE_NAME
#ifdef CONFIG_DOVE_GPU
#   define DEVICE_NAME              "dove_gpu"
#else
#   define DEVICE_NAME              "galcore"
#endif
#endif

#ifndef CLASS_NAME
#   define CLASS_NAME               "graphics_class"
#endif

#define GetPageCount(size, offset)     ((((size) + ((offset) & ~PAGE_CACHE_MASK)) + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT)

#if LINUX_VERSION_CODE >= KERNEL_VERSION (3,7,0)
#define gcdVM_FLAGS (VM_IO | VM_DONTCOPY | VM_DONTEXPAND | VM_DONTDUMP)
#else
#define gcdVM_FLAGS (VM_IO | VM_DONTCOPY | VM_DONTEXPAND | VM_RESERVED)
#endif

/* Protection bit when mapping memroy to user sapce */
#define gcmkPAGED_MEMROY_PROT(x)    pgprot_writecombine(x)

#if gcdNONPAGED_MEMORY_BUFFERABLE
#define gcmkIOREMAP                 ioremap_wc
#define gcmkNONPAGED_MEMROY_PROT(x) pgprot_writecombine(x)
#elif !gcdNONPAGED_MEMORY_CACHEABLE
#define gcmkIOREMAP                 ioremap_nocache
#define gcmkNONPAGED_MEMROY_PROT(x) pgprot_noncached(x)
#endif

#define gcdSUPPRESS_OOM_MESSAGE 1

#if gcdSUPPRESS_OOM_MESSAGE
#define gcdNOWARN __GFP_NOWARN
#else
#define gcdNOWARN 0
#endif

/******************************************************************************\
********************************** Structures **********************************
\******************************************************************************/
typedef struct _gcsIOMMU * gckIOMMU;

typedef struct _gcsUSER_MAPPING * gcsUSER_MAPPING_PTR;
typedef struct _gcsUSER_MAPPING
{
    /* Pointer to next mapping structure. */
    gcsUSER_MAPPING_PTR         next;

    /* Physical address of this mapping. */
    gctUINT32                   physical;

    /* Logical address of this mapping. */
    gctPOINTER                  logical;

    /* Number of bytes of this mapping. */
    gctSIZE_T                   bytes;

    /* Starting address of this mapping. */
    gctINT8_PTR                 start;

    /* Ending address of this mapping. */
    gctINT8_PTR                 end;
}
gcsUSER_MAPPING;

typedef struct _gcsINTEGER_DB * gcsINTEGER_DB_PTR;
typedef struct _gcsINTEGER_DB
{
    struct idr                  idr;
    spinlock_t                  lock;
    gctINT                      curr;
}
gcsINTEGER_DB;

struct _gckOS
{
    /* Object. */
    gcsOBJECT                   object;

    /* Pointer to device */
    gckGALDEVICE                device;

    /* Memory management */
    gctPOINTER                  memoryLock;
    gctPOINTER                  memoryMapLock;

    struct _LINUX_MDL           *mdlHead;
    struct _LINUX_MDL           *mdlTail;

    /* Kernel process ID. */
    gctUINT32                   kernelProcessID;

    /* Signal management. */

    /* Lock. */
    gctPOINTER                  signalMutex;

    /* signal id database. */
    gcsINTEGER_DB               signalDB;

#if gcdANDROID_NATIVE_FENCE_SYNC
    /* Lock. */
    gctPOINTER                  syncPointMutex;

    /* sync point id database. */
    gcsINTEGER_DB               syncPointDB;
#endif

    gcsUSER_MAPPING_PTR         userMap;
    gctPOINTER                  debugLock;

    /* workqueue for os timer. */
    struct workqueue_struct *   workqueue;

    /* Allocate extra page to avoid cache overflow */
    struct page* paddingPage;

    /* Detect unfreed allocation. */
    atomic_t                    allocateCount;

    struct list_head            allocatorList;

    gcsDEBUGFS_DIR              allocatorDebugfsDir;

    /* Lock for register access check. */
    struct mutex                registerAccessLocks[gcdMAX_GPU_COUNT];

    /* External power states. */
    gctBOOL                     powerStates[gcdMAX_GPU_COUNT];

    /* External clock states. */
    gctBOOL                     clockStates[gcdMAX_GPU_COUNT];

    /* IOMMU. */
    gckIOMMU                    iommu;
};

typedef struct _gcsSIGNAL * gcsSIGNAL_PTR;
typedef struct _gcsSIGNAL
{
    /* Kernel sync primitive. */
    struct completion obj;

    /* Manual reset flag. */
    gctBOOL manualReset;

    /* The reference counter. */
    atomic_t ref;

    /* The owner of the signal. */
    gctHANDLE process;

    gckHARDWARE hardware;

    /* ID. */
    gctUINT32 id;
}
gcsSIGNAL;

#if gcdANDROID_NATIVE_FENCE_SYNC
typedef struct _gcsSYNC_POINT * gcsSYNC_POINT_PTR;
typedef struct _gcsSYNC_POINT
{
    /* The reference counter. */
    atomic_t ref;

    /* State. */
    atomic_t state;

    /* timeline. */
    struct sync_timeline * timeline;

    /* ID. */
    gctUINT32 id;
}
gcsSYNC_POINT;
#endif

typedef struct _gcsPageInfo * gcsPageInfo_PTR;
typedef struct _gcsPageInfo
{
    struct page **pages;
    gctUINT32_PTR pageTable;
    gctUINT32   extraPage;
    gctUINT32 address;
#if gcdPROCESS_ADDRESS_SPACE
    gckMMU mmu;
#endif
}
gcsPageInfo;

typedef struct _gcsOSTIMER * gcsOSTIMER_PTR;
typedef struct _gcsOSTIMER
{
    struct delayed_work     work;
    gctTIMERFUNCTION        function;
    gctPOINTER              data;
} gcsOSTIMER;

gceSTATUS
gckOS_ImportAllocators(
    gckOS Os
    );

gceSTATUS
gckOS_FreeAllocators(
    gckOS Os
    );

gceSTATUS
_HandleOuterCache(
    IN gckOS Os,
    IN gctUINT32 Physical,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes,
    IN gceCACHEOPERATION Type
    );

gceSTATUS
_ConvertLogical2Physical(
    IN gckOS Os,
    IN gctPOINTER Logical,
    IN gctUINT32 ProcessID,
    IN PLINUX_MDL Mdl,
    OUT gctPHYS_ADDR_T * Physical
    );

gctSTRING
_CreateKernelVirtualMapping(
    IN PLINUX_MDL Mdl
    );

void
_DestoryKernelVirtualMapping(
    IN gctSTRING Addr
    );

void
_UnmapUserLogical(
    IN gctPOINTER Logical,
    IN gctUINT32  Size
    );

static inline gctINT
_GetProcessID(
    void
    )
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
    return task_tgid_vnr(current);
#else
    return current->tgid;
#endif
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)
static inline int
is_vmalloc_addr(
    void *Addr
    )
{
    unsigned long addr = (unsigned long)Addr;

    return addr >= VMALLOC_START && addr < VMALLOC_END;
}
#endif

#ifdef CONFIG_IOMMU_SUPPORT
void
gckIOMMU_Destory(
    IN gckOS Os,
    IN gckIOMMU Iommu
    );

gceSTATUS
gckIOMMU_Construct(
    IN gckOS Os,
    OUT gckIOMMU * Iommu
    );

gceSTATUS
gckIOMMU_Map(
    IN gckIOMMU Iommu,
    IN gctUINT32 DomainAddress,
    IN gctUINT32 Physical,
    IN gctUINT32 Bytes
    );

gceSTATUS
gckIOMMU_Unmap(
    IN gckIOMMU Iommu,
    IN gctUINT32 DomainAddress,
    IN gctUINT32 Bytes
    );
#endif

#endif /* __gc_hal_kernel_linux_h_ */
