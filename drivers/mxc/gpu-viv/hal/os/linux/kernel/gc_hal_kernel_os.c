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

#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/mman.h>
#include <asm/atomic.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/irqflags.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,23)
#include <linux/math64.h>
#endif
#include <linux/delay.h>
#include <linux/platform_device.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
#include <linux/anon_inodes.h>
#endif

#if gcdANDROID_NATIVE_FENCE_SYNC
#include <linux/file.h>
#include "gc_hal_kernel_sync.h"
#endif

#if defined(CONFIG_ARM) && LINUX_VERSION_CODE >= KERNEL_VERSION(4, 3, 0)
#include <dma.h>
#endif

#define _GC_OBJ_ZONE    gcvZONE_OS

#include "gc_hal_kernel_allocator.h"

#define MEMORY_LOCK(os) \
    gcmkVERIFY_OK(gckOS_AcquireMutex( \
                                (os), \
                                (os)->memoryLock, \
                                gcvINFINITE))

#define MEMORY_UNLOCK(os) \
    gcmkVERIFY_OK(gckOS_ReleaseMutex((os), (os)->memoryLock))

#define gcmkBUG_ON(x, func, line) \
    do { \
        if (unlikely(x)) \
        { \
            int i = 0; \
            while (1) \
            { \
                static int delay = 10 * 1000; \
                gcmkPRINT("[galcore]: BUG ON @ %s(%d) (%d)", func, line, i++); \
                dump_stack(); \
                gckOS_Delay(gcvNULL, delay); \
                delay *= 2; \
            } \
        } \
    } while (0)

/******************************************************************************\
******************************* Private Functions ******************************
\******************************************************************************/
static gctINT
_GetThreadID(
    void
    )
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
    return task_pid_vnr(current);
#else
    return current->pid;
#endif
}

static PLINUX_MDL
_CreateMdl(
    void
    )
{
    PLINUX_MDL  mdl;

    gcmkHEADER();

    mdl = (PLINUX_MDL)kzalloc(sizeof(struct _LINUX_MDL), GFP_KERNEL | gcdNOWARN);

    if (mdl)
    {
        mutex_init(&mdl->mapsMutex);
    }

    gcmkFOOTER_ARG("0x%X", mdl);
    return mdl;
}

static gceSTATUS
_DestroyMdlMap(
    IN PLINUX_MDL Mdl,
    IN PLINUX_MDL_MAP MdlMap
    );

static gceSTATUS
_DestroyMdl(
    IN PLINUX_MDL Mdl
    )
{
    PLINUX_MDL_MAP mdlMap, next;

    gcmkHEADER_ARG("Mdl=0x%X", Mdl);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Mdl != gcvNULL);

    mdlMap = Mdl->maps;

    while (mdlMap != gcvNULL)
    {
        next = mdlMap->next;

        gcmkVERIFY_OK(_DestroyMdlMap(Mdl, mdlMap));

        mdlMap = next;
    }

    kfree(Mdl);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

static PLINUX_MDL_MAP
_CreateMdlMap(
    IN PLINUX_MDL Mdl,
    IN gctINT ProcessID
    )
{
    PLINUX_MDL_MAP  mdlMap;

    gcmkHEADER_ARG("Mdl=0x%X ProcessID=%d", Mdl, ProcessID);

    mdlMap = (PLINUX_MDL_MAP)kmalloc(sizeof(struct _LINUX_MDL_MAP), GFP_KERNEL | gcdNOWARN);
    if (mdlMap == gcvNULL)
    {
        gcmkFOOTER_NO();
        return gcvNULL;
    }

    mdlMap->pid     = ProcessID;
    mdlMap->vmaAddr = gcvNULL;
    mdlMap->count   = 0;

    mdlMap->next    = Mdl->maps;
    Mdl->maps       = mdlMap;

    gcmkFOOTER_ARG("0x%X", mdlMap);
    return mdlMap;
}

static gceSTATUS
_DestroyMdlMap(
    IN PLINUX_MDL Mdl,
    IN PLINUX_MDL_MAP MdlMap
    )
{
    PLINUX_MDL_MAP  prevMdlMap;

    gcmkHEADER_ARG("Mdl=0x%X MdlMap=0x%X", Mdl, MdlMap);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(MdlMap != gcvNULL);
    gcmkASSERT(Mdl->maps != gcvNULL);

    if (Mdl->maps == MdlMap)
    {
        Mdl->maps = MdlMap->next;
    }
    else
    {
        prevMdlMap = Mdl->maps;

        while (prevMdlMap->next != MdlMap)
        {
            prevMdlMap = prevMdlMap->next;

            gcmkASSERT(prevMdlMap != gcvNULL);
        }

        prevMdlMap->next = MdlMap->next;
    }

    kfree(MdlMap);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

extern PLINUX_MDL_MAP
FindMdlMap(
    IN PLINUX_MDL Mdl,
    IN gctINT ProcessID
    )
{
    PLINUX_MDL_MAP  mdlMap;

    gcmkHEADER_ARG("Mdl=0x%X ProcessID=%d", Mdl, ProcessID);
    if(Mdl == gcvNULL)
    {
        gcmkFOOTER_NO();
        return gcvNULL;
    }
    mdlMap = Mdl->maps;

    while (mdlMap != gcvNULL)
    {
        if (mdlMap->pid == ProcessID)
        {
            gcmkFOOTER_ARG("0x%X", mdlMap);
            return mdlMap;
        }

        mdlMap = mdlMap->next;
    }

    gcmkFOOTER_NO();
    return gcvNULL;
}

/*******************************************************************************
** Integer Id Management.
*/
gceSTATUS
_AllocateIntegerId(
    IN gcsINTEGER_DB_PTR Database,
    IN gctPOINTER KernelPointer,
    OUT gctUINT32 *Id
    )
{
    int result;
    gctINT next;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
    idr_preload(GFP_KERNEL | gcdNOWARN);

    spin_lock(&Database->lock);

    next = (Database->curr + 1 <= 0) ? 1 : Database->curr + 1;

    result = idr_alloc(&Database->idr, KernelPointer, next, 0, GFP_ATOMIC);

    /* ID allocated should not be 0. */
    gcmkASSERT(result != 0);

    if (result > 0)
    {
        Database->curr = *Id = result;
    }

    spin_unlock(&Database->lock);

    idr_preload_end();

    if (result < 0)
    {
        return gcvSTATUS_OUT_OF_RESOURCES;
    }
#else
again:
    if (idr_pre_get(&Database->idr, GFP_KERNEL | gcdNOWARN) == 0)
    {
        return gcvSTATUS_OUT_OF_MEMORY;
    }

    spin_lock(&Database->lock);

    next = (Database->curr + 1 <= 0) ? 1 : Database->curr + 1;

    /* Try to get a id greater than 0. */
    result = idr_get_new_above(&Database->idr, KernelPointer, next, Id);

    if (!result)
    {
        Database->curr = *Id;
    }

    spin_unlock(&Database->lock);

    if (result == -EAGAIN)
    {
        goto again;
    }

    if (result != 0)
    {
        return gcvSTATUS_OUT_OF_RESOURCES;
    }
#endif

    return gcvSTATUS_OK;
}

gceSTATUS
_QueryIntegerId(
    IN gcsINTEGER_DB_PTR Database,
    IN gctUINT32  Id,
    OUT gctPOINTER * KernelPointer
    )
{
    gctPOINTER pointer;

    spin_lock(&Database->lock);

    pointer = idr_find(&Database->idr, Id);

    spin_unlock(&Database->lock);

    if(pointer)
    {
        *KernelPointer = pointer;
        return gcvSTATUS_OK;
    }
    else
    {
        gcmkTRACE_ZONE(
                gcvLEVEL_ERROR, gcvZONE_OS,
                "%s(%d) Id = %d is not found",
                __FUNCTION__, __LINE__, Id);

        return gcvSTATUS_NOT_FOUND;
    }
}

gceSTATUS
_DestroyIntegerId(
    IN gcsINTEGER_DB_PTR Database,
    IN gctUINT32 Id
    )
{
    spin_lock(&Database->lock);

    idr_remove(&Database->idr, Id);

    spin_unlock(&Database->lock);

    return gcvSTATUS_OK;
}

gceSTATUS
_QueryProcessPageTable(
    IN gctPOINTER Logical,
    OUT gctPHYS_ADDR_T * Address
    )
{
#ifndef CONFIG_DEBUG_SPINLOCK
    spinlock_t *lock;
#endif
    gctUINTPTR_T logical = (gctUINTPTR_T)Logical;
    pgd_t *pgd;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    gctUINT32 offset = logical & ~PAGE_MASK;

    if (is_vmalloc_addr(Logical))
    {
        *Address = page_to_phys(vmalloc_to_page(Logical)) + offset;

        return gcvSTATUS_OK;
    }

    if (!current->mm)
    {
        return gcvSTATUS_NOT_FOUND;
    }

    pgd = pgd_offset(current->mm, logical);
    if (pgd_none(*pgd) || pgd_bad(*pgd))
    {
        return gcvSTATUS_NOT_FOUND;
    }

    pud = pud_offset(pgd, logical);
    if (pud_none(*pud) || pud_bad(*pud))
    {
        return gcvSTATUS_NOT_FOUND;
    }

    pmd = pmd_offset(pud, logical);
    if (pmd_none(*pmd) || pmd_bad(*pmd))
    {
        return gcvSTATUS_NOT_FOUND;
    }

#ifndef CONFIG_DEBUG_SPINLOCK
    pte = pte_offset_map_lock(current->mm, pmd, logical, &lock);
#else
    spin_lock(&current->mm->page_table_lock);

    pte = pte_offset_map(pmd, logical);
#endif

    if (!pte)
    {
        return gcvSTATUS_NOT_FOUND;
    }

    if (!pte_present(*pte))
    {
#ifndef CONFIG_DEBUG_SPINLOCK
        pte_unmap_unlock(pte, lock);
#else
        spin_unlock(&current->mm->page_table_lock);
#endif
        return gcvSTATUS_NOT_FOUND;
    }

    *Address = (pte_pfn(*pte) << PAGE_SHIFT) | offset;
#ifndef CONFIG_DEBUG_SPINLOCK
    pte_unmap_unlock(pte, lock);
#else
    spin_unlock(&current->mm->page_table_lock);
#endif

    return gcvSTATUS_OK;
}

#if !gcdCACHE_FUNCTION_UNIMPLEMENTED && defined(CONFIG_OUTER_CACHE)
static inline gceSTATUS
outer_func(
    gceCACHEOPERATION Type,
    unsigned long Start,
    unsigned long End
    )
{
    switch (Type)
    {
        case gcvCACHE_CLEAN:
            outer_clean_range(Start, End);
            break;
        case gcvCACHE_INVALIDATE:
            outer_inv_range(Start, End);
            break;
        case gcvCACHE_FLUSH:
            outer_flush_range(Start, End);
            break;
        default:
            return gcvSTATUS_INVALID_ARGUMENT;
            break;
    }
    return gcvSTATUS_OK;
}

/*******************************************************************************
**  _HandleOuterCache
**
**  Handle the outer cache for the specified addresses.
**
**  ARGUMENTS:
**
**      gckOS Os
**          Pointer to gckOS object.
**
**      gctPOINTER Physical
**          Physical address to flush.
**
**      gctPOINTER Logical
**          Logical address to flush.
**
**      gctSIZE_T Bytes
**          Size of the address range in bytes to flush.
**
**      gceOUTERCACHE_OPERATION Type
**          Operation need to be execute.
*/
gceSTATUS
_HandleOuterCache(
    IN gckOS Os,
    IN gctUINT32 Physical,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes,
    IN gceCACHEOPERATION Type
    )
{
    gceSTATUS status;
    gctPHYS_ADDR_T paddr;
    gctPOINTER vaddr;
    gctUINT32 offset, bytes, left;

    gcmkHEADER_ARG("Os=0x%X Logical=0x%X Bytes=%lu",
                   Os, Logical, Bytes);

    if (Physical != gcvINVALID_ADDRESS)
    {
        /* Non paged memory or gcvPOOL_USER surface */
        paddr = (unsigned long) Physical;
        gcmkONERROR(outer_func(Type, paddr, paddr + Bytes));
    }
    else
    {
        /* Non contiguous virtual memory */
        vaddr = Logical;
        left = Bytes;

        while (left)
        {
            /* Handle (part of) current page. */
            offset = (gctUINTPTR_T)vaddr & ~PAGE_MASK;

            bytes = gcmMIN(left, PAGE_SIZE - offset);

            gcmkONERROR(_QueryProcessPageTable(vaddr, &paddr));
            gcmkONERROR(outer_func(Type, paddr, paddr + bytes));

            vaddr = (gctUINT8_PTR)vaddr + bytes;
            left -= bytes;
        }
    }

    mb();

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}
#endif

gctBOOL
_AllowAccess(
    IN gckOS Os,
    IN gceCORE Core,
    IN gctUINT32 Address
    )
{
    gctUINT32 data;

    /* Check external clock state. */
    if (Os->clockStates[Core] == gcvFALSE)
    {
        gcmkPRINT("[galcore]: %s(%d) GPU[%d] External clock off", __FUNCTION__, __LINE__, Core);
        return gcvFALSE;
    }

    /* Check internal clock state. */
    if (Address == 0)
    {
        return gcvTRUE;
    }

    data = readl((gctUINT8 *)Os->device->registerBases[Core] + 0x0);

    if ((data & 0x3) == 0x3)
    {
        gcmkPRINT("[galcore]: %s(%d) GPU[%d] Internal clock off", __FUNCTION__, __LINE__, Core);
        return gcvFALSE;
    }

    return gcvTRUE;
}

static gceSTATUS
_ShrinkMemory(
    IN gckOS Os
    )
{
    gcsPLATFORM * platform;
    gceSTATUS status = gcvSTATUS_OK;

    gcmkHEADER_ARG("Os=0x%X", Os);
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    platform = Os->device->platform;

    if (platform && platform->ops->shrinkMemory)
    {
        status = platform->ops->shrinkMemory(platform);
    }
    else
    {
        gcmkFOOTER_NO();
        return gcvSTATUS_NOT_SUPPORTED;
    }

    gcmkFOOTER_NO();
    return status;
}

/*******************************************************************************
**
**  gckOS_Construct
**
**  Construct a new gckOS object.
**
**  INPUT:
**
**      gctPOINTER Context
**          Pointer to the gckGALDEVICE class.
**
**  OUTPUT:
**
**      gckOS * Os
**          Pointer to a variable that will hold the pointer to the gckOS object.
*/
gceSTATUS
gckOS_Construct(
    IN gctPOINTER Context,
    OUT gckOS * Os
    )
{
    gckOS os;
    gceSTATUS status;
    gctINT i;

    gcmkHEADER_ARG("Context=0x%X", Context);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Os != gcvNULL);

    /* Allocate the gckOS object. */
    os = (gckOS) kmalloc(gcmSIZEOF(struct _gckOS), GFP_KERNEL | gcdNOWARN);

    if (os == gcvNULL)
    {
        /* Out of memory. */
        gcmkFOOTER_ARG("status=%d", gcvSTATUS_OUT_OF_MEMORY);
        return gcvSTATUS_OUT_OF_MEMORY;
    }

    /* Zero the memory. */
    gckOS_ZeroMemory(os, gcmSIZEOF(struct _gckOS));

    /* Initialize the gckOS object. */
    os->object.type = gcvOBJ_OS;

    /* Set device device. */
    os->device = Context;

    /* Set allocateCount to 0, gckOS_Allocate has not been used yet. */
    atomic_set(&os->allocateCount, 0);

    /* Initialize the memory lock. */
    gcmkONERROR(gckOS_CreateMutex(os, &os->memoryLock));

    /* Create debug lock mutex. */
    gcmkONERROR(gckOS_CreateMutex(os, &os->debugLock));

    os->mdlHead = os->mdlTail = gcvNULL;

    /* Get the kernel process ID. */
    gcmkONERROR(gckOS_GetProcessID(&os->kernelProcessID));

    /*
     * Initialize the signal manager.
     */

    /* Initialize mutex. */
    gcmkONERROR(gckOS_CreateMutex(os, &os->signalMutex));

    /* Initialize signal id database lock. */
    spin_lock_init(&os->signalDB.lock);

    /* Initialize signal id database. */
    idr_init(&os->signalDB.idr);

#if gcdANDROID_NATIVE_FENCE_SYNC
    /*
     * Initialize the sync point manager.
     */

    /* Initialize mutex. */
    gcmkONERROR(gckOS_CreateMutex(os, &os->syncPointMutex));

    /* Initialize sync point id database lock. */
    spin_lock_init(&os->syncPointDB.lock);

    /* Initialize sync point id database. */
    idr_init(&os->syncPointDB.idr);
#endif

    /* Create a workqueue for os timer. */
    os->workqueue = create_singlethread_workqueue("galcore workqueue");

    if (os->workqueue == gcvNULL)
    {
        /* Out of memory. */
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    os->paddingPage = alloc_page(GFP_KERNEL | __GFP_HIGHMEM | gcdNOWARN);
    if (os->paddingPage == gcvNULL)
    {
        /* Out of memory. */
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }
    else
    {
        SetPageReserved(os->paddingPage);
    }

    for (i = 0; i < gcdMAX_GPU_COUNT; i++)
    {
        mutex_init(&os->registerAccessLocks[i]);
    }

    gckOS_ImportAllocators(os);

#ifdef CONFIG_IOMMU_SUPPORT
    if (((gckGALDEVICE)(os->device))->args.mmu == gcvFALSE)
    {
        /* Only use IOMMU when internal MMU is not enabled. */
        status = gckIOMMU_Construct(os, &os->iommu);

        if (gcmIS_ERROR(status))
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_INFO, gcvZONE_OS,
                "%s(%d): Fail to setup IOMMU",
                __FUNCTION__, __LINE__
                );
        }
    }
#endif

    /* Return pointer to the gckOS object. */
    *Os = os;

    /* Success. */
    gcmkFOOTER_ARG("*Os=0x%X", *Os);
    return gcvSTATUS_OK;

OnError:

#if gcdANDROID_NATIVE_FENCE_SYNC
    if (os->syncPointMutex != gcvNULL)
    {
        gcmkVERIFY_OK(
            gckOS_DeleteMutex(os, os->syncPointMutex));
    }
#endif

    if (os->signalMutex != gcvNULL)
    {
        gcmkVERIFY_OK(
            gckOS_DeleteMutex(os, os->signalMutex));
    }

    if (os->memoryLock != gcvNULL)
    {
        gcmkVERIFY_OK(
            gckOS_DeleteMutex(os, os->memoryLock));
    }

    if (os->debugLock != gcvNULL)
    {
        gcmkVERIFY_OK(
            gckOS_DeleteMutex(os, os->debugLock));
    }

    if (os->workqueue != gcvNULL)
    {
        destroy_workqueue(os->workqueue);
    }

    kfree(os);

    /* Return the error. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_Destroy
**
**  Destroy an gckOS object.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object that needs to be destroyed.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_Destroy(
    IN gckOS Os
    )
{
    gcmkHEADER_ARG("Os=0x%X", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    if (Os->paddingPage != gcvNULL)
    {
        ClearPageReserved(Os->paddingPage);
        __free_page(Os->paddingPage);
        Os->paddingPage = gcvNULL;
    }

#if gcdANDROID_NATIVE_FENCE_SYNC
    /*
     * Destroy the sync point manager.
     */

    /* Destroy the mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Os, Os->syncPointMutex));
#endif

    /*
     * Destroy the signal manager.
     */

    /* Destroy the mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Os, Os->signalMutex));

    /* Destroy the memory lock. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Os, Os->memoryLock));

    /* Destroy debug lock mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Os, Os->debugLock));

    /* Wait for all works done. */
    flush_workqueue(Os->workqueue);

    /* Destory work queue. */
    destroy_workqueue(Os->workqueue);

    gckOS_FreeAllocators(Os);

#ifdef CONFIG_IOMMU_SUPPORT
    if (Os->iommu)
    {
        gckIOMMU_Destory(Os, Os->iommu);
    }
#endif

    /* Flush the debug cache. */
    gcmkDEBUGFLUSH(~0U);

    /* Mark the gckOS object as unknown. */
    Os->object.type = gcvOBJ_UNKNOWN;


    /* Free the gckOS object. */
    kfree(Os);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_CreateKernelVirtualMapping(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Logical,
    OUT gctSIZE_T * PageCount
    )
{
    gceSTATUS status;
    PLINUX_MDL mdl = (PLINUX_MDL)Physical;
    gckALLOCATOR allocator = mdl->allocator;

    gcmkHEADER();

    *PageCount = mdl->numPages;

    gcmkONERROR(allocator->ops->MapKernel(allocator, mdl, Logical));

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckOS_DestroyKernelVirtualMapping(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    IN gctPOINTER Logical
    )
{
    PLINUX_MDL mdl = (PLINUX_MDL)Physical;
    gckALLOCATOR allocator = mdl->allocator;

    gcmkHEADER();

    allocator->ops->UnmapKernel(allocator, mdl, Logical);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_CreateUserVirtualMapping(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Logical,
    OUT gctSIZE_T * PageCount
    )
{
    return gckOS_LockPages(Os, Physical, Bytes, gcvFALSE, Logical, PageCount);
}

gceSTATUS
gckOS_DestroyUserVirtualMapping(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    IN gctPOINTER Logical
    )
{
    return gckOS_UnlockPages(Os, Physical, Bytes, Logical);
}

/*******************************************************************************
**
**  gckOS_Allocate
**
**  Allocate memory.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctSIZE_T Bytes
**          Number of bytes to allocate.
**
**  OUTPUT:
**
**      gctPOINTER * Memory
**          Pointer to a variable that will hold the allocated memory location.
*/
gceSTATUS
gckOS_Allocate(
    IN gckOS Os,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Memory
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X Bytes=%lu", Os, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);

    gcmkONERROR(gckOS_AllocateMemory(Os, Bytes, Memory));

    /* Success. */
    gcmkFOOTER_ARG("*Memory=0x%X", *Memory);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_Free
**
**  Free allocated memory.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Memory
**          Pointer to memory allocation to free.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_Free(
    IN gckOS Os,
    IN gctPOINTER Memory
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X Memory=0x%X", Os, Memory);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);

    gcmkONERROR(gckOS_FreeMemory(Os, Memory));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_AllocateMemory
**
**  Allocate memory wrapper.
**
**  INPUT:
**
**      gctSIZE_T Bytes
**          Number of bytes to allocate.
**
**  OUTPUT:
**
**      gctPOINTER * Memory
**          Pointer to a variable that will hold the allocated memory location.
*/
gceSTATUS
gckOS_AllocateMemory(
    IN gckOS Os,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Memory
    )
{
    gctPOINTER memory;
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X Bytes=%lu", Os, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);

    if (Bytes > PAGE_SIZE)
    {
        memory = (gctPOINTER) vmalloc(Bytes);
    }
    else
    {
        memory = (gctPOINTER) kmalloc(Bytes, GFP_KERNEL | gcdNOWARN);
    }

    if (memory == gcvNULL)
    {
        /* Out of memory. */
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    /* Increase count. */
    atomic_inc(&Os->allocateCount);

    /* Return pointer to the memory allocation. */
    *Memory = memory;

    /* Success. */
    gcmkFOOTER_ARG("*Memory=0x%X", *Memory);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_FreeMemory
**
**  Free allocated memory wrapper.
**
**  INPUT:
**
**      gctPOINTER Memory
**          Pointer to memory allocation to free.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_FreeMemory(
    IN gckOS Os,
    IN gctPOINTER Memory
    )
{
    gcmkHEADER_ARG("Memory=0x%X", Memory);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);

    /* Free the memory from the OS pool. */
    if (is_vmalloc_addr(Memory))
    {
        vfree(Memory);
    }
    else
    {
        kfree(Memory);
    }

    /* Decrease count. */
    atomic_dec(&Os->allocateCount);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_MapMemory
**
**  Map physical memory into the current process.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPHYS_ADDR Physical
**          Start of physical address memory.
**
**      gctSIZE_T Bytes
**          Number of bytes to map.
**
**  OUTPUT:
**
**      gctPOINTER * Memory
**          Pointer to a variable that will hold the logical address of the
**          mapped memory.
*/
gceSTATUS
gckOS_MapMemory(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Logical
    )
{
    gceSTATUS status;
    PLINUX_MDL_MAP  mdlMap;
    PLINUX_MDL      mdl = (PLINUX_MDL)Physical;
    gckALLOCATOR allocator;

    gcmkHEADER_ARG("Os=0x%X Physical=0x%X Bytes=%lu", Os, Physical, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != 0);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

    mutex_lock(&mdl->mapsMutex);

    mdlMap = FindMdlMap(mdl, _GetProcessID());

    if (mdlMap == gcvNULL)
    {
        mdlMap = _CreateMdlMap(mdl, _GetProcessID());

        if (mdlMap == gcvNULL)
        {
            mutex_unlock(&mdl->mapsMutex);

            gcmkFOOTER_ARG("status=%d", gcvSTATUS_OUT_OF_MEMORY);
            return gcvSTATUS_OUT_OF_MEMORY;
        }
    }

    if (mdlMap->vmaAddr == gcvNULL)
    {
        allocator = mdl->allocator;
        gcmkONERROR(allocator->ops->MapUser(allocator, mdl, gcvFALSE, &mdlMap->vmaAddr));
        }

    mutex_unlock(&mdl->mapsMutex);

    *Logical = mdlMap->vmaAddr;

    gcmkFOOTER_ARG("*Logical=0x%X", *Logical);
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_UnmapMemory
**
**  Unmap physical memory out of the current process.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPHYS_ADDR Physical
**          Start of physical address memory.
**
**      gctSIZE_T Bytes
**          Number of bytes to unmap.
**
**      gctPOINTER Memory
**          Pointer to a previously mapped memory region.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_UnmapMemory(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    IN gctPOINTER Logical
    )
{
    gcmkHEADER_ARG("Os=0x%X Physical=0x%X Bytes=%lu Logical=0x%X",
                   Os, Physical, Bytes, Logical);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != 0);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

    gckOS_UnmapMemoryEx(Os, Physical, Bytes, Logical, _GetProcessID());

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}


/*******************************************************************************
**
**  gckOS_UnmapMemoryEx
**
**  Unmap physical memory in the specified process.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPHYS_ADDR Physical
**          Start of physical address memory.
**
**      gctSIZE_T Bytes
**          Number of bytes to unmap.
**
**      gctPOINTER Memory
**          Pointer to a previously mapped memory region.
**
**      gctUINT32 PID
**          Pid of the process that opened the device and mapped this memory.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_UnmapMemoryEx(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    IN gctPOINTER Logical,
    IN gctUINT32 PID
    )
{
    PLINUX_MDL_MAP          mdlMap;
    PLINUX_MDL              mdl = (PLINUX_MDL)Physical;

    gcmkHEADER_ARG("Os=0x%X Physical=0x%X Bytes=%lu Logical=0x%X PID=%d",
                   Os, Physical, Bytes, Logical, PID);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != 0);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
    gcmkVERIFY_ARGUMENT(PID != 0);

    if (Logical)
    {
        mutex_lock(&mdl->mapsMutex);

        mdlMap = FindMdlMap(mdl, PID);

        if (mdlMap == gcvNULL || mdlMap->vmaAddr == gcvNULL)
        {
            mutex_unlock(&mdl->mapsMutex);

            gcmkFOOTER_ARG("status=%d", gcvSTATUS_INVALID_ARGUMENT);
            return gcvSTATUS_INVALID_ARGUMENT;
        }

        _UnmapUserLogical(mdlMap->vmaAddr, mdl->numPages * PAGE_SIZE);

        gcmkVERIFY_OK(_DestroyMdlMap(mdl, mdlMap));

        mutex_unlock(&mdl->mapsMutex);
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_UnmapUserLogical
**
**  Unmap user logical memory out of physical memory.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPHYS_ADDR Physical
**          Start of physical address memory.
**
**      gctSIZE_T Bytes
**          Number of bytes to unmap.
**
**      gctPOINTER Memory
**          Pointer to a previously mapped memory region.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_UnmapUserLogical(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    IN gctPOINTER Logical
    )
{
    gcmkHEADER_ARG("Os=0x%X Physical=0x%X Bytes=%lu Logical=0x%X",
                   Os, Physical, Bytes, Logical);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != 0);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

    gckOS_UnmapMemory(Os, Physical, Bytes, Logical);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

}

/*******************************************************************************
**
**  gckOS_AllocateNonPagedMemory
**
**  Allocate a number of pages from non-paged memory.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctBOOL InUserSpace
**          gcvTRUE if the pages need to be mapped into user space.
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that holds the number of bytes to allocate.
**
**  OUTPUT:
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that hold the number of bytes allocated.
**
**      gctPHYS_ADDR * Physical
**          Pointer to a variable that will hold the physical address of the
**          allocation.
**
**      gctPOINTER * Logical
**          Pointer to a variable that will hold the logical address of the
**          allocation.
*/
gceSTATUS
gckOS_AllocateNonPagedMemory(
    IN gckOS Os,
    IN gctBOOL InUserSpace,
    IN OUT gctSIZE_T * Bytes,
    OUT gctPHYS_ADDR * Physical,
    OUT gctPOINTER * Logical
    )
{
    gctSIZE_T bytes;
    gctINT numPages;
    PLINUX_MDL mdl = gcvNULL;
    PLINUX_MDL_MAP mdlMap = gcvNULL;
    gctPOINTER addr;
    gceSTATUS status = gcvSTATUS_NOT_SUPPORTED;
    gckALLOCATOR allocator;
    gctUINT32 flag = gcvALLOC_FLAG_CONTIGUOUS;

    gcmkHEADER_ARG("Os=0x%X InUserSpace=%d *Bytes=%lu",
                   Os, InUserSpace, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes != gcvNULL);
    gcmkVERIFY_ARGUMENT(*Bytes > 0);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

    /* Align number of bytes to page size. */
    bytes = gcmALIGN(*Bytes, PAGE_SIZE);

    /* Get total number of pages.. */
    numPages = GetPageCount(bytes, 0);

    /* Allocate mdl structure */
    mdl = _CreateMdl();
    if (mdl == gcvNULL)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    /* Walk all allocators. */
    list_for_each_entry(allocator, &Os->allocatorList, head)
    {
        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_OS,
                       "%s(%d) flag = %x allocator->capability = %x",
                        __FUNCTION__, __LINE__, flag, allocator->capability);

#ifndef NO_DMA_COHERENT
        /* Point to dma coherent allocator. */
        if (strcmp(allocator->name, "dma"))
    {
            status = gcvSTATUS_NOT_SUPPORTED;
            continue;
    }
#else
        if ((flag & allocator->capability) != flag)
        {
            status = gcvSTATUS_NOT_SUPPORTED;
            continue;
        }
#endif


        status = allocator->ops->Alloc(allocator, mdl, numPages, flag);

        if (gcmIS_SUCCESS(status))
    {
            mdl->allocator = allocator;
            break;
        }
    }

    /* Check status. */
    gcmkONERROR(status);

    mdl->numPages = numPages;

    mdl->contiguous = gcvTRUE;

    gcmkONERROR(allocator->ops->MapKernel(allocator, mdl, &addr));

    /* Trigger a page fault. */
    memset(addr, 0, numPages * PAGE_SIZE);

    mdl->addr = addr;

    if (InUserSpace)
    {
        mdlMap = _CreateMdlMap(mdl, _GetProcessID());

        if (mdlMap == gcvNULL)
        {
            gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
        }

        gcmkONERROR(allocator->ops->MapUser(allocator, mdl, gcvFALSE, &mdlMap->vmaAddr));

        *Logical = mdlMap->vmaAddr;
    }
    else
    {
        *Logical = addr;
    }

    /*
     * Add this to a global list.
     * Will be used by get physical address
     * and mapuser pointer functions.
     */

    MEMORY_LOCK(Os);

    if (!Os->mdlHead)
    {
        /* Initialize the queue. */
        Os->mdlHead = Os->mdlTail = mdl;
    }
    else
    {
        /* Add to the tail. */
        mdl->prev = Os->mdlTail;
        Os->mdlTail->next = mdl;
        Os->mdlTail = mdl;
    }

    MEMORY_UNLOCK(Os);

    /* Return allocated memory. */
    *Bytes = bytes;
    *Physical = (gctPHYS_ADDR) mdl;

    /* Success. */
    gcmkFOOTER_ARG("*Bytes=%lu *Physical=0x%X *Logical=0x%X",
                   *Bytes, *Physical, *Logical);
    return gcvSTATUS_OK;

OnError:
    if (mdlMap != gcvNULL)
    {
        /* Free LINUX_MDL_MAP. */
        gcmkVERIFY_OK(_DestroyMdlMap(mdl, mdlMap));
    }

    if (mdl != gcvNULL)
    {
        /* Free LINUX_MDL. */
        gcmkVERIFY_OK(_DestroyMdl(mdl));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}


/*******************************************************************************
**
**  gckOS_FreeNonPagedMemory
**
**  Free previously allocated and mapped pages from non-paged memory.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctSIZE_T Bytes
**          Number of bytes allocated.
**
**      gctPHYS_ADDR Physical
**          Physical address of the allocated memory.
**
**      gctPOINTER Logical
**          Logical address of the allocated memory.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS gckOS_FreeNonPagedMemory(
    IN gckOS Os,
    IN gctSIZE_T Bytes,
    IN gctPHYS_ADDR Physical,
    IN gctPOINTER Logical
    )
{
    PLINUX_MDL mdl;

    gckALLOCATOR allocator;

    gcmkHEADER_ARG("Os=0x%X Bytes=%lu Physical=0x%X Logical=0x%X",
                   Os, Bytes, Physical, Logical);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Physical != 0);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

    /* Convert physical address into a pointer to a MDL. */
    mdl = (PLINUX_MDL) Physical;

    allocator = mdl->allocator;

    allocator->ops->UnmapKernel(allocator, mdl, mdl->addr);
    allocator->ops->Free(allocator, mdl);

    MEMORY_LOCK(Os);

    /* Remove the node from global list.. */
    if (mdl == Os->mdlHead)
    {
        if ((Os->mdlHead = mdl->next) == gcvNULL)
        {
            Os->mdlTail = gcvNULL;
        }
    }
    else
    {
        mdl->prev->next = mdl->next;
        if (mdl == Os->mdlTail)
        {
            Os->mdlTail = mdl->prev;
        }
        else
        {
            mdl->next->prev = mdl->prev;
        }
    }

    MEMORY_UNLOCK(Os);

    gcmkVERIFY_OK(_DestroyMdl(mdl));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_ReadRegister
**
**  Read data from a register.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctUINT32 Address
**          Address of register.
**
**  OUTPUT:
**
**      gctUINT32 * Data
**          Pointer to a variable that receives the data read from the register.
*/
gceSTATUS
gckOS_ReadRegister(
    IN gckOS Os,
    IN gctUINT32 Address,
    OUT gctUINT32 * Data
    )
{
    return gckOS_ReadRegisterEx(Os, gcvCORE_MAJOR, Address, Data);
}

gceSTATUS
gckOS_ReadRegisterEx(
    IN gckOS Os,
    IN gceCORE Core,
    IN gctUINT32 Address,
    OUT gctUINT32 * Data
    )
{
    gcmkHEADER_ARG("Os=0x%X Core=%d Address=0x%X", Os, Core, Address);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    gcmkVERIFY_ARGUMENT(Address < Os->device->requestedRegisterMemSizes[Core]);

    gcmkVERIFY_ARGUMENT(Data != gcvNULL);

    if (!in_irq())
    {
        mutex_lock(&Os->registerAccessLocks[Core]);
    }

    gcmkBUG_ON(!_AllowAccess(Os, Core, Address), __FUNCTION__, __LINE__);

    *Data = readl((gctUINT8 *)Os->device->registerBases[Core] + Address);

    if (!in_irq())
    {
        mutex_unlock(&Os->registerAccessLocks[Core]);
    }

#if gcdDUMP_AHB_ACCESS
    gcmkPRINT("@[RD %d] %08x %08x", Core, Address, *Data);
#endif

    /* Success. */
    gcmkFOOTER_ARG("*Data=0x%08x", *Data);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_WriteRegister
**
**  Write data to a register.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctUINT32 Address
**          Address of register.
**
**      gctUINT32 Data
**          Data for register.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_WriteRegister(
    IN gckOS Os,
    IN gctUINT32 Address,
    IN gctUINT32 Data
    )
{
    return gckOS_WriteRegisterEx(Os, gcvCORE_MAJOR, Address, Data);
}

gceSTATUS
gckOS_WriteRegisterEx(
    IN gckOS Os,
    IN gceCORE Core,
    IN gctUINT32 Address,
    IN gctUINT32 Data
    )
{
    gcmkHEADER_ARG("Os=0x%X Core=%d Address=0x%X Data=0x%08x", Os, Core, Address, Data);

    gcmkVERIFY_ARGUMENT(Address < Os->device->requestedRegisterMemSizes[Core]);

    if (!in_interrupt())
    {
        mutex_lock(&Os->registerAccessLocks[Core]);
    }

    gcmkBUG_ON(!_AllowAccess(Os, Core, Address), __FUNCTION__, __LINE__);

    writel(Data, (gctUINT8 *)Os->device->registerBases[Core] + Address);

    if (!in_interrupt())
    {
        mutex_unlock(&Os->registerAccessLocks[Core]);
    }

#if gcdDUMP_AHB_ACCESS
    gcmkPRINT("@[WR %d] %08x %08x", Core, Address, Data);
#endif

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_GetPageSize
**
**  Get the system's page size.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**  OUTPUT:
**
**      gctSIZE_T * PageSize
**          Pointer to a variable that will receive the system's page size.
*/
gceSTATUS gckOS_GetPageSize(
    IN gckOS Os,
    OUT gctSIZE_T * PageSize
    )
{
    gcmkHEADER_ARG("Os=0x%X", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(PageSize != gcvNULL);

    /* Return the page size. */
    *PageSize = (gctSIZE_T) PAGE_SIZE;

    /* Success. */
    gcmkFOOTER_ARG("*PageSize=%d", *PageSize);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_GetPhysicalAddressProcess
**
**  Get the physical system address of a corresponding virtual address for a
**  given process.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to gckOS object.
**
**      gctPOINTER Logical
**          Logical address.
**
**      gctUINT32 ProcessID
**          Process ID.
**
**  OUTPUT:
**
**      gctUINT32 * Address
**          Poinetr to a variable that receives the 32-bit physical adress.
*/
gceSTATUS
_GetPhysicalAddressProcess(
    IN gckOS Os,
    IN gctPOINTER Logical,
    IN gctUINT32 ProcessID,
    OUT gctPHYS_ADDR_T * Address
    )
{
    PLINUX_MDL mdl;
    gctINT8_PTR base;
    gceSTATUS status = gcvSTATUS_INVALID_ADDRESS;

    gcmkHEADER_ARG("Os=0x%X Logical=0x%X ProcessID=%d", Os, Logical, ProcessID);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    MEMORY_LOCK(Os);

    /* First try the contiguous memory pool. */
    if (Os->device->contiguousMapped)
    {
        base = (gctINT8_PTR) Os->device->contiguousBase;

        if (((gctINT8_PTR) Logical >= base)
        &&  ((gctINT8_PTR) Logical <  base + Os->device->contiguousSize)
        )
        {
            /* Convert logical address into physical. */
            *Address = Os->device->contiguousVidMem->baseAddress
                     + (gctINT8_PTR) Logical - base;
            status   = gcvSTATUS_OK;
        }
    }
    else
    {
        /* Try the contiguous memory pool. */
        mdl = (PLINUX_MDL) Os->device->contiguousPhysical;

        mutex_lock(&mdl->mapsMutex);

        status = _ConvertLogical2Physical(
                    Os,
                                          Logical,
                                          ProcessID,
                                          mdl,
                    Address
                    );

        mutex_unlock(&mdl->mapsMutex);
    }

    if (gcmIS_ERROR(status))
    {
        /* Walk all MDLs. */
        for (mdl = Os->mdlHead; mdl != gcvNULL; mdl = mdl->next)
        {
            mutex_lock(&mdl->mapsMutex);

            status = _ConvertLogical2Physical(
                    Os,
                            Logical,
                            ProcessID,
                    mdl,
                            Address
                            );

            mutex_unlock(&mdl->mapsMutex);

            if (gcmIS_SUCCESS(status))
            {
                break;
            }
        }
    }

    MEMORY_UNLOCK(Os);

    gcmkONERROR(status);

    /* Success. */
    gcmkFOOTER_ARG("*Address=0x%08x", *Address);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}



/*******************************************************************************
**
**  gckOS_GetPhysicalAddress
**
**  Get the physical system address of a corresponding virtual address.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Logical
**          Logical address.
**
**  OUTPUT:
**
**      gctUINT32 * Address
**          Poinetr to a variable that receives the 32-bit physical adress.
*/
gceSTATUS
gckOS_GetPhysicalAddress(
    IN gckOS Os,
    IN gctPOINTER Logical,
    OUT gctPHYS_ADDR_T * Address
    )
{
    gceSTATUS status;
    gctUINT32 processID;

    gcmkHEADER_ARG("Os=0x%X Logical=0x%X", Os, Logical);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    /* Query page table of current process first. */
    status = _QueryProcessPageTable(Logical, Address);

    if (gcmIS_ERROR(status))
    {
        /* Get current process ID. */
        processID = _GetProcessID();

        /* Route through other function. */
        gcmkONERROR(
            _GetPhysicalAddressProcess(Os, Logical, processID, Address));
    }

    gcmkVERIFY_OK(gckOS_CPUPhysicalToGPUPhysical(Os, *Address, Address));

    /* Success. */
    gcmkFOOTER_ARG("*Address=0x%08x", *Address);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_UserLogicalToPhysical
**
**  Get the physical system address of a corresponding user virtual address.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Logical
**          Logical address.
**
**  OUTPUT:
**
**      gctUINT32 * Address
**          Pointer to a variable that receives the 32-bit physical address.
*/
gceSTATUS gckOS_UserLogicalToPhysical(
    IN gckOS Os,
    IN gctPOINTER Logical,
    OUT gctPHYS_ADDR_T * Address
    )
{
    return gckOS_GetPhysicalAddress(Os, Logical, Address);
}

#if gcdSECURE_USER
static gceSTATUS
gckOS_AddMapping(
    IN gckOS Os,
    IN gctUINT32 Physical,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    )
{
    gceSTATUS status;
    gcsUSER_MAPPING_PTR map;

    gcmkHEADER_ARG("Os=0x%X Physical=0x%X Logical=0x%X Bytes=%lu",
                   Os, Physical, Logical, Bytes);

    gcmkONERROR(gckOS_Allocate(Os,
                               gcmSIZEOF(gcsUSER_MAPPING),
                               (gctPOINTER *) &map));

    map->next     = Os->userMap;
    map->physical = Physical - Os->device->baseAddress;
    map->logical  = Logical;
    map->bytes    = Bytes;
    map->start    = (gctINT8_PTR) Logical;
    map->end      = map->start + Bytes;

    Os->userMap = map;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

static gceSTATUS
gckOS_RemoveMapping(
    IN gckOS Os,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    )
{
    gceSTATUS status;
    gcsUSER_MAPPING_PTR map, prev;

    gcmkHEADER_ARG("Os=0x%X Logical=0x%X Bytes=%lu", Os, Logical, Bytes);

    for (map = Os->userMap, prev = gcvNULL; map != gcvNULL; map = map->next)
    {
        if ((map->logical == Logical)
        &&  (map->bytes   == Bytes)
        )
        {
            break;
        }

        prev = map;
    }

    if (map == gcvNULL)
    {
        gcmkONERROR(gcvSTATUS_INVALID_ADDRESS);
    }

    if (prev == gcvNULL)
    {
        Os->userMap = map->next;
    }
    else
    {
        prev->next = map->next;
    }

    gcmkONERROR(gcmkOS_SAFE_FREE(Os, map));

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}
#endif

gceSTATUS
_ConvertLogical2Physical(
    IN gckOS Os,
    IN gctPOINTER Logical,
    IN gctUINT32 ProcessID,
    IN PLINUX_MDL Mdl,
    OUT gctPHYS_ADDR_T * Physical
    )
{
    gckALLOCATOR allocator = Mdl->allocator;
    gctUINT32 offset;
    gceSTATUS status = gcvSTATUS_NOT_FOUND;
    gctINT8_PTR vBase;

    if ((gctUINTPTR_T)Logical >= TASK_SIZE)
    {
        /* Kernel virtual address. */
        vBase = Mdl->addr;
        }
        else
        {
        /* User virtual address. */
        PLINUX_MDL_MAP map;

        map   = FindMdlMap(Mdl, (gctINT) ProcessID);
        vBase = (map == gcvNULL) ? gcvNULL : (gctINT8_PTR) map->vmaAddr;
    }

        /* Is the given address within that range. */
        if ((vBase != gcvNULL)
        &&  ((gctINT8_PTR) Logical >= vBase)
        &&  ((gctINT8_PTR) Logical <  vBase + Mdl->numPages * PAGE_SIZE)
        )
        {
            offset = (gctINT8_PTR) Logical - vBase;

        allocator->ops->Physical(allocator, Mdl, offset, Physical);

        status = gcvSTATUS_OK;
    }

    return status;
}

/*******************************************************************************
**
**  gckOS_MapPhysical
**
**  Map a physical address into kernel space.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctUINT32 Physical
**          Physical address of the memory to map.
**
**      gctSIZE_T Bytes
**          Number of bytes to map.
**
**  OUTPUT:
**
**      gctPOINTER * Logical
**          Pointer to a variable that receives the base address of the mapped
**          memory.
*/
gceSTATUS
gckOS_MapPhysical(
    IN gckOS Os,
    IN gctUINT32 Physical,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Logical
    )
{
    gctPOINTER logical;
    PLINUX_MDL mdl;
    gctUINT32 physical = Physical;

    gcmkHEADER_ARG("Os=0x%X Physical=0x%X Bytes=%lu", Os, Physical, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

    MEMORY_LOCK(Os);

    /* Go through our mapping to see if we know this physical address already. */
    mdl = Os->mdlHead;

    while (mdl != gcvNULL)
    {
        if (mdl->dmaHandle != 0)
        {
            if ((physical >= mdl->dmaHandle)
            &&  (physical < mdl->dmaHandle + mdl->numPages * PAGE_SIZE)
            )
            {
                *Logical = mdl->addr + (physical - mdl->dmaHandle);
                break;
            }
        }

        mdl = mdl->next;
    }

    MEMORY_UNLOCK(Os);

    if (mdl == gcvNULL)
    {
        unsigned long pfn = physical >> PAGE_SHIFT;

        if (pfn_valid(pfn))
        {
            gctUINT32 offset = physical & ~PAGE_MASK;
            struct page ** pages;
            struct page * page;
            gctUINT numPages;
            gctINT i;

            numPages = GetPageCount(PAGE_ALIGN(offset + Bytes), 0);

            pages = kmalloc(sizeof(struct page *) * numPages, GFP_KERNEL | gcdNOWARN);

            if (!pages)
            {
                gcmkFOOTER_ARG("status=%d", gcvSTATUS_OUT_OF_MEMORY);
                return gcvSTATUS_OUT_OF_MEMORY;
            }

            page = pfn_to_page(pfn);

            for (i = 0; i < numPages; i++)
            {
                pages[i] = nth_page(page, i);
            }

            logical = vmap(pages, numPages, 0, gcmkNONPAGED_MEMROY_PROT(PAGE_KERNEL));

            kfree(pages);

            if (logical == gcvNULL)
            {
                gcmkTRACE_ZONE(
                    gcvLEVEL_INFO, gcvZONE_OS,
                    "%s(%d): Failed to vmap",
                    __FUNCTION__, __LINE__
                    );

                /* Out of resources. */
                gcmkFOOTER_ARG("status=%d", gcvSTATUS_OUT_OF_RESOURCES);
                return gcvSTATUS_OUT_OF_RESOURCES;
            }

            logical += offset;
        }
        else
        {
            /* Map memory as cached memory. */
            request_mem_region(physical, Bytes, "MapRegion");
            logical = (gctPOINTER) ioremap_nocache(physical, Bytes);

            if (logical == gcvNULL)
            {
                gcmkTRACE_ZONE(
                    gcvLEVEL_INFO, gcvZONE_OS,
                    "%s(%d): Failed to ioremap",
                    __FUNCTION__, __LINE__
                    );

                /* Out of resources. */
                gcmkFOOTER_ARG("status=%d", gcvSTATUS_OUT_OF_RESOURCES);
                return gcvSTATUS_OUT_OF_RESOURCES;
            }
        }

        /* Return pointer to mapped memory. */
        *Logical = logical;
    }


    /* Success. */
    gcmkFOOTER_ARG("*Logical=0x%X", *Logical);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_UnmapPhysical
**
**  Unmap a previously mapped memory region from kernel memory.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Logical
**          Pointer to the base address of the memory to unmap.
**
**      gctSIZE_T Bytes
**          Number of bytes to unmap.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_UnmapPhysical(
    IN gckOS Os,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    )
{
    PLINUX_MDL  mdl;

    gcmkHEADER_ARG("Os=0x%X Logical=0x%X Bytes=%lu", Os, Logical, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

    MEMORY_LOCK(Os);

    mdl = Os->mdlHead;

    while (mdl != gcvNULL)
    {
        if (mdl->addr != gcvNULL)
        {
            if (Logical >= (gctPOINTER)mdl->addr
                    && Logical < (gctPOINTER)((gctSTRING)mdl->addr + mdl->numPages * PAGE_SIZE))
            {
                break;
            }
        }

        mdl = mdl->next;
    }

    if (mdl == gcvNULL)
    {
        /* Unmap the memory. */
        vunmap((void *)((unsigned long)Logical & PAGE_MASK));
    }

    MEMORY_UNLOCK(Os);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_DeleteMutex
**
**  Delete a mutex.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Mutex
**          Pointer to the mute to be deleted.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_DeleteMutex(
    IN gckOS Os,
    IN gctPOINTER Mutex
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X Mutex=0x%X", Os, Mutex);

    /* Validate the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Mutex != gcvNULL);

    /* Destroy the mutex. */
    mutex_destroy((struct mutex *)Mutex);

    /* Free the mutex structure. */
    gcmkONERROR(gckOS_Free(Os, Mutex));

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_AcquireMutex
**
**  Acquire a mutex.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Mutex
**          Pointer to the mutex to be acquired.
**
**      gctUINT32 Timeout
**          Timeout value specified in milliseconds.
**          Specify the value of gcvINFINITE to keep the thread suspended
**          until the mutex has been acquired.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_AcquireMutex(
    IN gckOS Os,
    IN gctPOINTER Mutex,
    IN gctUINT32 Timeout
    )
{
    gcmkHEADER_ARG("Os=0x%X Mutex=0x%0x Timeout=%u", Os, Mutex, Timeout);

    /* Validate the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Mutex != gcvNULL);

    if (Timeout == gcvINFINITE)
    {
        /* Lock the mutex. */
        mutex_lock(Mutex);

        /* Success. */
        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

    for (;;)
    {
        /* Try to acquire the mutex. */
        if (mutex_trylock(Mutex))
        {
            /* Success. */
            gcmkFOOTER_NO();
            return gcvSTATUS_OK;
        }

        if (Timeout-- == 0)
        {
            break;
        }

        /* Wait for 1 millisecond. */
        gcmkVERIFY_OK(gckOS_Delay(Os, 1));
    }

    /* Timeout. */
    gcmkFOOTER_ARG("status=%d", gcvSTATUS_TIMEOUT);
    return gcvSTATUS_TIMEOUT;
}

/*******************************************************************************
**
**  gckOS_ReleaseMutex
**
**  Release an acquired mutex.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Mutex
**          Pointer to the mutex to be released.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_ReleaseMutex(
    IN gckOS Os,
    IN gctPOINTER Mutex
    )
{
    gcmkHEADER_ARG("Os=0x%X Mutex=0x%0x", Os, Mutex);

    /* Validate the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Mutex != gcvNULL);

    /* Release the mutex. */
    mutex_unlock(Mutex);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_AtomicExchange
**
**  Atomically exchange a pair of 32-bit values.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      IN OUT gctINT32_PTR Target
**          Pointer to the 32-bit value to exchange.
**
**      IN gctINT32 NewValue
**          Specifies a new value for the 32-bit value pointed to by Target.
**
**      OUT gctINT32_PTR OldValue
**          The old value of the 32-bit value pointed to by Target.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_AtomicExchange(
    IN gckOS Os,
    IN OUT gctUINT32_PTR Target,
    IN gctUINT32 NewValue,
    OUT gctUINT32_PTR OldValue
    )
{
    gcmkHEADER_ARG("Os=0x%X Target=0x%X NewValue=%u", Os, Target, NewValue);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(OldValue != gcvNULL);

    /* Exchange the pair of 32-bit values. */
    *OldValue = (gctUINT32) atomic_xchg((atomic_t *) Target, (int) NewValue);

    /* Success. */
    gcmkFOOTER_ARG("*OldValue=%u", *OldValue);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_AtomicExchangePtr
**
**  Atomically exchange a pair of pointers.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      IN OUT gctPOINTER * Target
**          Pointer to the 32-bit value to exchange.
**
**      IN gctPOINTER NewValue
**          Specifies a new value for the pointer pointed to by Target.
**
**      OUT gctPOINTER * OldValue
**          The old value of the pointer pointed to by Target.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_AtomicExchangePtr(
    IN gckOS Os,
    IN OUT gctPOINTER * Target,
    IN gctPOINTER NewValue,
    OUT gctPOINTER * OldValue
    )
{
    gcmkHEADER_ARG("Os=0x%X Target=0x%X NewValue=0x%X", Os, Target, NewValue);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(OldValue != gcvNULL);

    /* Exchange the pair of pointers. */
    *OldValue = (gctPOINTER)(gctUINTPTR_T) atomic_xchg((atomic_t *) Target, (int)(gctUINTPTR_T) NewValue);

    /* Success. */
    gcmkFOOTER_ARG("*OldValue=0x%X", *OldValue);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_AtomicSetMask
**
**  Atomically set mask to Atom
**
**  INPUT:
**      IN OUT gctPOINTER Atom
**          Pointer to the atom to set.
**
**      IN gctUINT32 Mask
**          Mask to set.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_AtomSetMask(
    IN gctPOINTER Atom,
    IN gctUINT32 Mask
    )
{
    gctUINT32 oval, nval;

    gcmkHEADER_ARG("Atom=0x%0x", Atom);
    gcmkVERIFY_ARGUMENT(Atom != gcvNULL);

    do
    {
        oval = atomic_read((atomic_t *) Atom);
        nval = oval | Mask;
    } while (atomic_cmpxchg((atomic_t *) Atom, oval, nval) != oval);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_AtomClearMask
**
**  Atomically clear mask from Atom
**
**  INPUT:
**      IN OUT gctPOINTER Atom
**          Pointer to the atom to clear.
**
**      IN gctUINT32 Mask
**          Mask to clear.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_AtomClearMask(
    IN gctPOINTER Atom,
    IN gctUINT32 Mask
    )
{
    gctUINT32 oval, nval;

    gcmkHEADER_ARG("Atom=0x%0x", Atom);
    gcmkVERIFY_ARGUMENT(Atom != gcvNULL);

    do
    {
        oval = atomic_read((atomic_t *) Atom);
        nval = oval & ~Mask;
    } while (atomic_cmpxchg((atomic_t *) Atom, oval, nval) != oval);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_AtomConstruct
**
**  Create an atom.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**  OUTPUT:
**
**      gctPOINTER * Atom
**          Pointer to a variable receiving the constructed atom.
*/
gceSTATUS
gckOS_AtomConstruct(
    IN gckOS Os,
    OUT gctPOINTER * Atom
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Atom != gcvNULL);

    /* Allocate the atom. */
    gcmkONERROR(gckOS_Allocate(Os, gcmSIZEOF(atomic_t), Atom));

    /* Initialize the atom. */
    atomic_set((atomic_t *) *Atom, 0);

    /* Success. */
    gcmkFOOTER_ARG("*Atom=0x%X", *Atom);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_AtomDestroy
**
**  Destroy an atom.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      gctPOINTER Atom
**          Pointer to the atom to destroy.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_AtomDestroy(
    IN gckOS Os,
    OUT gctPOINTER Atom
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X Atom=0x%0x", Os, Atom);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Atom != gcvNULL);

    /* Free the atom. */
    gcmkONERROR(gcmkOS_SAFE_FREE(Os, Atom));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_AtomGet
**
**  Get the 32-bit value protected by an atom.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      gctPOINTER Atom
**          Pointer to the atom.
**
**  OUTPUT:
**
**      gctINT32_PTR Value
**          Pointer to a variable the receives the value of the atom.
*/
gceSTATUS
gckOS_AtomGet(
    IN gckOS Os,
    IN gctPOINTER Atom,
    OUT gctINT32_PTR Value
    )
{
    gcmkHEADER_ARG("Os=0x%X Atom=0x%0x", Os, Atom);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Atom != gcvNULL);

    /* Return the current value of atom. */
    *Value = atomic_read((atomic_t *) Atom);

    /* Success. */
    gcmkFOOTER_ARG("*Value=%d", *Value);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_AtomSet
**
**  Set the 32-bit value protected by an atom.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      gctPOINTER Atom
**          Pointer to the atom.
**
**      gctINT32 Value
**          The value of the atom.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_AtomSet(
    IN gckOS Os,
    IN gctPOINTER Atom,
    IN gctINT32 Value
    )
{
    gcmkHEADER_ARG("Os=0x%X Atom=0x%0x Value=%d", Os, Atom);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Atom != gcvNULL);

    /* Set the current value of atom. */
    atomic_set((atomic_t *) Atom, Value);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_AtomIncrement
**
**  Atomically increment the 32-bit integer value inside an atom.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      gctPOINTER Atom
**          Pointer to the atom.
**
**  OUTPUT:
**
**      gctINT32_PTR Value
**          Pointer to a variable that receives the original value of the atom.
*/
gceSTATUS
gckOS_AtomIncrement(
    IN gckOS Os,
    IN gctPOINTER Atom,
    OUT gctINT32_PTR Value
    )
{
    gcmkHEADER_ARG("Os=0x%X Atom=0x%0x", Os, Atom);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Atom != gcvNULL);

    /* Increment the atom. */
    *Value = atomic_inc_return((atomic_t *) Atom) - 1;

    /* Success. */
    gcmkFOOTER_ARG("*Value=%d", *Value);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_AtomDecrement
**
**  Atomically decrement the 32-bit integer value inside an atom.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      gctPOINTER Atom
**          Pointer to the atom.
**
**  OUTPUT:
**
**      gctINT32_PTR Value
**          Pointer to a variable that receives the original value of the atom.
*/
gceSTATUS
gckOS_AtomDecrement(
    IN gckOS Os,
    IN gctPOINTER Atom,
    OUT gctINT32_PTR Value
    )
{
    gcmkHEADER_ARG("Os=0x%X Atom=0x%0x", Os, Atom);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Atom != gcvNULL);

    /* Decrement the atom. */
    *Value = atomic_dec_return((atomic_t *) Atom) + 1;

    /* Success. */
    gcmkFOOTER_ARG("*Value=%d", *Value);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_Delay
**
**  Delay execution of the current thread for a number of milliseconds.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctUINT32 Delay
**          Delay to sleep, specified in milliseconds.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_Delay(
    IN gckOS Os,
    IN gctUINT32 Delay
    )
{
    gcmkHEADER_ARG("Os=0x%X Delay=%u", Os, Delay);

    if (Delay > 0)
    {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)
        ktime_t delay = ktime_set((Delay / MSEC_PER_SEC), (Delay % MSEC_PER_SEC) * NSEC_PER_MSEC);
        __set_current_state(TASK_UNINTERRUPTIBLE);
        schedule_hrtimeout(&delay, HRTIMER_MODE_REL);
#else
        msleep(Delay);
#endif
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_GetTicks
**
**  Get the number of milliseconds since the system started.
**
**  INPUT:
**
**  OUTPUT:
**
**      gctUINT32_PTR Time
**          Pointer to a variable to get time.
**
*/
gceSTATUS
gckOS_GetTicks(
    OUT gctUINT32_PTR Time
    )
{
     gcmkHEADER();

    *Time = jiffies_to_msecs(jiffies);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_TicksAfter
**
**  Compare time values got from gckOS_GetTicks.
**
**  INPUT:
**      gctUINT32 Time1
**          First time value to be compared.
**
**      gctUINT32 Time2
**          Second time value to be compared.
**
**  OUTPUT:
**
**      gctBOOL_PTR IsAfter
**          Pointer to a variable to result.
**
*/
gceSTATUS
gckOS_TicksAfter(
    IN gctUINT32 Time1,
    IN gctUINT32 Time2,
    OUT gctBOOL_PTR IsAfter
    )
{
    gcmkHEADER();

    *IsAfter = time_after((unsigned long)Time1, (unsigned long)Time2);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_GetTime
**
**  Get the number of microseconds since the system started.
**
**  INPUT:
**
**  OUTPUT:
**
**      gctUINT64_PTR Time
**          Pointer to a variable to get time.
**
*/
gceSTATUS
gckOS_GetTime(
    OUT gctUINT64_PTR Time
    )
{
    struct timeval tv;
    gcmkHEADER();

    /* Return the time of day in microseconds. */
    do_gettimeofday(&tv);
    *Time = (tv.tv_sec * 1000000ULL) + tv.tv_usec;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_MemoryBarrier
**
**  Make sure the CPU has executed everything up to this point and the data got
**  written to the specified pointer.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Address
**          Address of memory that needs to be barriered.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_MemoryBarrier(
    IN gckOS Os,
    IN gctPOINTER Address
    )
{
    gcmkHEADER_ARG("Os=0x%X Address=0x%X", Os, Address);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

#if gcdNONPAGED_MEMORY_BUFFERABLE \
    && defined (CONFIG_ARM) \
    && (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34))
    /* drain write buffer */
    dsb();

    /* drain outer cache's write buffer? */
#else
    mb();
#endif

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_AllocatePagedMemory
**
**  Allocate memory from the paged pool.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctSIZE_T Bytes
**          Number of bytes to allocate.
**
**  OUTPUT:
**
**      gctPHYS_ADDR * Physical
**          Pointer to a variable that receives the physical address of the
**          memory allocation.
*/
gceSTATUS
gckOS_AllocatePagedMemory(
    IN gckOS Os,
    IN gctSIZE_T Bytes,
    OUT gctPHYS_ADDR * Physical
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X Bytes=%lu", Os, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);

    /* Allocate the memory. */
    gcmkONERROR(gckOS_AllocatePagedMemoryEx(Os, gcvALLOC_FLAG_NONE, Bytes, gcvNULL, Physical));

    /* Success. */
    gcmkFOOTER_ARG("*Physical=0x%X", *Physical);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_AllocatePagedMemoryEx
**
**  Allocate memory from the paged pool.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctUINT32 Flag
**          Allocation attribute.
**
**      gctSIZE_T Bytes
**          Number of bytes to allocate.
**
**  OUTPUT:
**
**      gctUINT32 * Gid
**          Save the global ID for the piece of allocated memory.
**
**      gctPHYS_ADDR * Physical
**          Pointer to a variable that receives the physical address of the
**          memory allocation.
*/
gceSTATUS
gckOS_AllocatePagedMemoryEx(
    IN gckOS Os,
    IN gctUINT32 Flag,
    IN gctSIZE_T Bytes,
    OUT gctUINT32 * Gid,
    OUT gctPHYS_ADDR * Physical
    )
{
    gctINT numPages;
    PLINUX_MDL mdl = gcvNULL;
    gctSIZE_T bytes;
    gceSTATUS status = gcvSTATUS_OUT_OF_MEMORY;
    gckALLOCATOR allocator;

    gcmkHEADER_ARG("Os=0x%X Flag=%x Bytes=%lu", Os, Flag, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);

    bytes = gcmALIGN(Bytes, PAGE_SIZE);

    numPages = GetPageCount(bytes, 0);

    mdl = _CreateMdl();
    if (mdl == gcvNULL)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    /* Walk all allocators. */
    list_for_each_entry(allocator, &Os->allocatorList, head)
    {
        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_OS,
                       "%s(%d) flag = %x allocator->capability = %x",
                        __FUNCTION__, __LINE__, Flag, allocator->capability);

        if ((Flag & allocator->capability) != Flag)
        {
            status = gcvSTATUS_NOT_SUPPORTED;
            continue;
        }

        status = allocator->ops->Alloc(allocator, mdl, numPages, Flag);

        if (gcmIS_SUCCESS(status))
        {
            mdl->allocator = allocator;
            break;
        }
    }

    /* Check status. */
    gcmkONERROR(status);

    mdl->dmaHandle  = 0;
    mdl->addr       = 0;
    mdl->numPages   = numPages;
    mdl->contiguous = Flag & gcvALLOC_FLAG_CONTIGUOUS;

    if (Gid != gcvNULL)
    {
        *Gid = mdl->gid;
    }

    MEMORY_LOCK(Os);

    /*
     * Add this to a global list.
     * Will be used by get physical address
     * and mapuser pointer functions.
     */
    if (!Os->mdlHead)
    {
        /* Initialize the queue. */
        Os->mdlHead = Os->mdlTail = mdl;
    }
    else
    {
        /* Add to tail. */
        mdl->prev           = Os->mdlTail;
        Os->mdlTail->next   = mdl;
        Os->mdlTail         = mdl;
    }

    MEMORY_UNLOCK(Os);

    /* Return physical address. */
    *Physical = (gctPHYS_ADDR) mdl;

    /* Success. */
    gcmkFOOTER_ARG("*Physical=0x%X", *Physical);
    return gcvSTATUS_OK;

OnError:
    if (mdl != gcvNULL)
    {
        /* Free the memory. */
        _DestroyMdl(mdl);
    }

    /* Return the status. */
    gcmkFOOTER_ARG("Os=0x%X Flag=%x Bytes=%lu", Os, Flag, Bytes);
    return status;
}

/*******************************************************************************
**
**  gckOS_FreePagedMemory
**
**  Free memory allocated from the paged pool.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPHYS_ADDR Physical
**          Physical address of the allocation.
**
**      gctSIZE_T Bytes
**          Number of bytes of the allocation.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_FreePagedMemory(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes
    )
{
    PLINUX_MDL mdl = (PLINUX_MDL) Physical;
    gckALLOCATOR allocator = (gckALLOCATOR)mdl->allocator;

    gcmkHEADER_ARG("Os=0x%X Physical=0x%X Bytes=%lu", Os, Physical, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

    MEMORY_LOCK(Os);

    /* Remove the node from global list. */
    if (mdl == Os->mdlHead)
    {
        if ((Os->mdlHead = mdl->next) == gcvNULL)
        {
            Os->mdlTail = gcvNULL;
        }
    }
    else
    {
        mdl->prev->next = mdl->next;

        if (mdl == Os->mdlTail)
        {
            Os->mdlTail = mdl->prev;
        }
        else
        {
            mdl->next->prev = mdl->prev;
        }
    }

    MEMORY_UNLOCK(Os);

    allocator->ops->Free(allocator, mdl);

    /* Free the structure... */
    gcmkVERIFY_OK(_DestroyMdl(mdl));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_LockPages
**
**  Lock memory allocated from the paged pool.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPHYS_ADDR Physical
**          Physical address of the allocation.
**
**      gctSIZE_T Bytes
**          Number of bytes of the allocation.
**
**      gctBOOL Cacheable
**          Cache mode of mapping.
**
**  OUTPUT:
**
**      gctPOINTER * Logical
**          Pointer to a variable that receives the address of the mapped
**          memory.
**
**      gctSIZE_T * PageCount
**          Pointer to a variable that receives the number of pages required for
**          the page table according to the GPU page size.
*/
gceSTATUS
gckOS_LockPages(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    IN gctBOOL Cacheable,
    OUT gctPOINTER * Logical,
    OUT gctSIZE_T * PageCount
    )
{
    gceSTATUS       status;
    PLINUX_MDL      mdl;
    PLINUX_MDL_MAP  mdlMap;
    gckALLOCATOR    allocator;

    gcmkHEADER_ARG("Os=0x%X Physical=0x%X Bytes=%lu", Os, Physical, Logical);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
    gcmkVERIFY_ARGUMENT(PageCount != gcvNULL);

    mdl = (PLINUX_MDL) Physical;
    allocator = mdl->allocator;

    mutex_lock(&mdl->mapsMutex);

    mdlMap = FindMdlMap(mdl, _GetProcessID());

    if (mdlMap == gcvNULL)
    {
        mdlMap = _CreateMdlMap(mdl, _GetProcessID());

        if (mdlMap == gcvNULL)
        {
            mutex_unlock(&mdl->mapsMutex);

            gcmkFOOTER_ARG("*status=%d", gcvSTATUS_OUT_OF_MEMORY);
            return gcvSTATUS_OUT_OF_MEMORY;
        }
    }

    if (mdlMap->vmaAddr == gcvNULL)
    {
        status = allocator->ops->MapUser(allocator, mdl, Cacheable, &mdlMap->vmaAddr);

        if (gcmIS_ERROR(status))
        {
            mutex_unlock(&mdl->mapsMutex);

            gcmkFOOTER_ARG("*status=%d", status);
            return status;
        }
    }

    mdlMap->count++;

    /* Convert pointer to MDL. */
    *Logical = mdlMap->vmaAddr;

    /* Return the page number according to the GPU page size. */
    gcmkASSERT((PAGE_SIZE % 4096) == 0);
    gcmkASSERT((PAGE_SIZE / 4096) >= 1);

    *PageCount = mdl->numPages * (PAGE_SIZE / 4096);

    mutex_unlock(&mdl->mapsMutex);

    /* Success. */
    gcmkFOOTER_ARG("*Logical=0x%X *PageCount=%lu", *Logical, *PageCount);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_MapPages
**
**  Map paged memory into a page table.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPHYS_ADDR Physical
**          Physical address of the allocation.
**
**      gctSIZE_T PageCount
**          Number of pages required for the physical address.
**
**      gctPOINTER PageTable
**          Pointer to the page table to fill in.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_MapPages(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T PageCount,
    IN gctPOINTER PageTable
    )
{
    return gcvSTATUS_NOT_SUPPORTED;
}

gceSTATUS
gckOS_MapPagesEx(
    IN gckOS Os,
    IN gceCORE Core,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T PageCount,
    IN gctUINT32 Address,
    IN gctPOINTER PageTable,
    IN gctBOOL Writable,
    IN gceSURF_TYPE Type
    )
{
    gceSTATUS status = gcvSTATUS_OK;
    PLINUX_MDL  mdl;
    gctUINT32*  table;
    gctUINT32   offset;
#if gcdNONPAGED_MEMORY_CACHEABLE
    gckMMU      mmu;
    PLINUX_MDL  mmuMdl;
    gctUINT32   bytes;
    gctPHYS_ADDR pageTablePhysical;
#endif

#if gcdPROCESS_ADDRESS_SPACE
    gckKERNEL kernel = Os->device->kernels[Core];
    gckMMU      mmu;
#endif
    gckALLOCATOR allocator;

    gctUINT32 policyID = 0;
    gctUINT32 axiConfig = 0;

    gcsPLATFORM * platform = Os->device->platform;

    gcmkHEADER_ARG("Os=0x%X Core=%d Physical=0x%X PageCount=%u PageTable=0x%X",
                   Os, Core, Physical, PageCount, PageTable);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);
    gcmkVERIFY_ARGUMENT(PageCount > 0);
    gcmkVERIFY_ARGUMENT(PageTable != gcvNULL);

    /* Convert pointer to MDL. */
    mdl = (PLINUX_MDL)Physical;

    allocator = mdl->allocator;

    gcmkASSERT(allocator != gcvNULL);

    gcmkTRACE_ZONE(
        gcvLEVEL_INFO, gcvZONE_OS,
        "%s(%d): Physical->0x%X PageCount->0x%X",
        __FUNCTION__, __LINE__,
        (gctUINT32)(gctUINTPTR_T)Physical,
        (gctUINT32)(gctUINTPTR_T)PageCount
        );

#if gcdPROCESS_ADDRESS_SPACE
    gcmkONERROR(gckKERNEL_GetProcessMMU(kernel, &mmu));
#endif

    table = (gctUINT32 *)PageTable;
#if gcdNONPAGED_MEMORY_CACHEABLE
    mmu = Os->device->kernels[Core]->mmu;
    bytes = PageCount * sizeof(*table);
    mmuMdl = (PLINUX_MDL)mmu->pageTablePhysical;
#endif

    if (platform && platform->ops->getPolicyID)
    {
        platform->ops->getPolicyID(platform, Type, &policyID, &axiConfig);

        gcmkBUG_ON(policyID > 0x1F, __FUNCTION__, __LINE__);

        /* ID[3:0] is used in STLB. */
        policyID &= 0xF;
    }

     /* Get all the physical addresses and store them in the page table. */

    offset = 0;
    PageCount = PageCount / (PAGE_SIZE / 4096);

    /* Try to get the user pages so DMA can happen. */
    while (PageCount-- > 0)
    {
        gctUINT i;
        gctPHYS_ADDR_T phys = ~0U;

        allocator->ops->Physical(allocator, mdl, offset * PAGE_SIZE, &phys);

        gcmkVERIFY_OK(gckOS_CPUPhysicalToGPUPhysical(Os, phys, &phys));

        if (policyID)
        {
            /* AxUSER must not used for address currently. */
            gcmkBUG_ON((phys >> 32) & 0xF, __FUNCTION__, __LINE__);

            /* Merge policyID to AxUSER[7:4].*/
            phys |= ((gctPHYS_ADDR_T)policyID << 36);
        }

#ifdef CONFIG_IOMMU_SUPPORT
        if (Os->iommu)
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_INFO, gcvZONE_OS,
                "%s(%d): Setup mapping in IOMMU %x => %x",
                __FUNCTION__, __LINE__,
                Address + (offset * PAGE_SIZE), phys
                );

            /* When use IOMMU, GPU use system PAGE_SIZE. */
            gcmkONERROR(gckIOMMU_Map(
                Os->iommu, Address + (offset * PAGE_SIZE), phys, PAGE_SIZE));
        }
        else
#endif
        {

#if gcdENABLE_VG
            if (Core == gcvCORE_VG)
            {
                for (i = 0; i < (PAGE_SIZE / 4096); i++)
                {
                    gcmkONERROR(
                        gckVGMMU_SetPage(Os->device->kernels[Core]->vg->mmu,
                            phys + (i * 4096),
                            table++));
                }
            }
            else
#endif
            {
                for (i = 0; i < (PAGE_SIZE / 4096); i++)
                {
#if gcdPROCESS_ADDRESS_SPACE
                    gctUINT32_PTR pageTableEntry;
                    gckMMU_GetPageEntry(mmu, Address + (offset * 4096), &pageTableEntry);
                    gcmkONERROR(
                        gckMMU_SetPage(mmu,
                            phys + (i * 4096),
                            Writable,
                            pageTableEntry));
#else
                    gcmkONERROR(
                        gckMMU_SetPage(Os->device->kernels[Core]->mmu,
                            phys + (i * 4096),
                            Writable,
                            table++));
#endif
                }
            }
        }

        offset += 1;
    }

#if gcdNONPAGED_MEMORY_CACHEABLE
    /* Get physical address of pageTable */
    pageTablePhysical = (gctPHYS_ADDR)(mmuMdl->dmaHandle +
                        ((gctUINT32 *)PageTable - mmu->pageTableLogical));

    /* Flush the mmu page table cache. */
    gcmkONERROR(gckOS_CacheClean(
        Os,
        _GetProcessID(),
        gcvNULL,
        pageTablePhysical,
        PageTable,
        bytes
        ));
#endif

OnError:

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckOS_UnmapPages(
    IN gckOS Os,
    IN gctSIZE_T PageCount,
    IN gctUINT32 Address
    )
{
#ifdef CONFIG_IOMMU_SUPPORT
    if (Os->iommu)
    {
        gcmkVERIFY_OK(gckIOMMU_Unmap(
            Os->iommu, Address, PageCount * PAGE_SIZE));
    }
#endif

    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_UnlockPages
**
**  Unlock memory allocated from the paged pool.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPHYS_ADDR Physical
**          Physical address of the allocation.
**
**      gctSIZE_T Bytes
**          Number of bytes of the allocation.
**
**      gctPOINTER Logical
**          Address of the mapped memory.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_UnlockPages(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    IN gctPOINTER Logical
    )
{
    PLINUX_MDL_MAP          mdlMap;
    PLINUX_MDL              mdl = (PLINUX_MDL)Physical;
    gckALLOCATOR            allocator = mdl->allocator;

    gcmkHEADER_ARG("Os=0x%X Physical=0x%X Bytes=%u Logical=0x%X",
                   Os, Physical, Bytes, Logical);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

    mutex_lock(&mdl->mapsMutex);

    mdlMap = mdl->maps;

    while (mdlMap != gcvNULL)
    {
        if ((mdlMap->vmaAddr != gcvNULL) && (_GetProcessID() == mdlMap->pid))
        {
            if (--mdlMap->count == 0)
            {
                allocator->ops->UnmapUser(
                    allocator,
                    mdlMap->vmaAddr,
                    mdl->numPages * PAGE_SIZE);

                mdlMap->vmaAddr = gcvNULL;
            }
        }

        mdlMap = mdlMap->next;
    }

    mutex_unlock(&mdl->mapsMutex);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}


/*******************************************************************************
**
**  gckOS_AllocateContiguous
**
**  Allocate memory from the contiguous pool.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctBOOL InUserSpace
**          gcvTRUE if the pages need to be mapped into user space.
**
**      gctSIZE_T * Bytes
**          Pointer to the number of bytes to allocate.
**
**  OUTPUT:
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that receives the number of bytes allocated.
**
**      gctPHYS_ADDR * Physical
**          Pointer to a variable that receives the physical address of the
**          memory allocation.
**
**      gctPOINTER * Logical
**          Pointer to a variable that receives the logical address of the
**          memory allocation.
*/
gceSTATUS
gckOS_AllocateContiguous(
    IN gckOS Os,
    IN gctBOOL InUserSpace,
    IN OUT gctSIZE_T * Bytes,
    OUT gctPHYS_ADDR * Physical,
    OUT gctPOINTER * Logical
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X InUserSpace=%d *Bytes=%lu",
                   Os, InUserSpace, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes != gcvNULL);
    gcmkVERIFY_ARGUMENT(*Bytes > 0);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

    /* Same as non-paged memory for now. */
    gcmkONERROR(gckOS_AllocateNonPagedMemory(Os,
                                             InUserSpace,
                                             Bytes,
                                             Physical,
                                             Logical));

    /* Success. */
    gcmkFOOTER_ARG("*Bytes=%lu *Physical=0x%X *Logical=0x%X",
                   *Bytes, *Physical, *Logical);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_FreeContiguous
**
**  Free memory allocated from the contiguous pool.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPHYS_ADDR Physical
**          Physical address of the allocation.
**
**      gctPOINTER Logical
**          Logicval address of the allocation.
**
**      gctSIZE_T Bytes
**          Number of bytes of the allocation.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_FreeContiguous(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X Physical=0x%X Logical=0x%X Bytes=%lu",
                   Os, Physical, Logical, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

    /* Same of non-paged memory for now. */
    gcmkONERROR(gckOS_FreeNonPagedMemory(Os, Bytes, Physical, Logical));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

#if gcdENABLE_VG
/******************************************************************************
**
**  gckOS_GetKernelLogical
**
**  Return the kernel logical pointer that corresponods to the specified
**  hardware address.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctUINT32 Address
**          Hardware physical address.
**
**  OUTPUT:
**
**      gctPOINTER * KernelPointer
**          Pointer to a variable receiving the pointer in kernel address space.
*/
gceSTATUS
gckOS_GetKernelLogical(
    IN gckOS Os,
    IN gctUINT32 Address,
    OUT gctPOINTER * KernelPointer
    )
{
    return gckOS_GetKernelLogicalEx(Os, gcvCORE_MAJOR, Address, KernelPointer);
}

gceSTATUS
gckOS_GetKernelLogicalEx(
    IN gckOS Os,
    IN gceCORE Core,
    IN gctUINT32 Address,
    OUT gctPOINTER * KernelPointer
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X Core=%d Address=0x%08x", Os, Core, Address);

    do
    {
        gckGALDEVICE device;
        gckKERNEL kernel;
        gcePOOL pool;
        gctUINT32 offset;
        gctPOINTER logical;

        /* Extract the pointer to the gckGALDEVICE class. */
        device = (gckGALDEVICE) Os->device;

        /* Kernel shortcut. */
        kernel = device->kernels[Core];
#if gcdENABLE_VG
       if (Core == gcvCORE_VG)
       {
           gcmkERR_BREAK(gckVGHARDWARE_SplitMemory(
                kernel->vg->hardware, Address, &pool, &offset
                ));
       }
       else
#endif
       {
        /* Split the memory address into a pool type and offset. */
            gcmkERR_BREAK(gckHARDWARE_SplitMemory(
                kernel->hardware, Address, &pool, &offset
                ));
       }

        /* Dispatch on pool. */
        switch (pool)
        {
        case gcvPOOL_LOCAL_INTERNAL:
            /* Internal memory. */
            logical = device->internalLogical;
            break;

        case gcvPOOL_LOCAL_EXTERNAL:
            /* External memory. */
            logical = device->externalLogical;
            break;

        case gcvPOOL_SYSTEM:
            /* System memory. */
            logical = device->contiguousBase;
            break;

        default:
            /* Invalid memory pool. */
            gcmkFOOTER();
            return gcvSTATUS_INVALID_ARGUMENT;
        }

        /* Build logical address of specified address. */
        * KernelPointer = ((gctUINT8_PTR) logical) + offset;

        /* Success. */
        gcmkFOOTER_ARG("*KernelPointer=0x%X", *KernelPointer);
        return gcvSTATUS_OK;
    }
    while (gcvFALSE);

    /* Return status. */
    gcmkFOOTER();
    return status;
}
#endif

/*******************************************************************************
**
**  gckOS_MapUserPointer
**
**  Map a pointer from the user process into the kernel address space.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Pointer
**          Pointer in user process space that needs to be mapped.
**
**      gctSIZE_T Size
**          Number of bytes that need to be mapped.
**
**  OUTPUT:
**
**      gctPOINTER * KernelPointer
**          Pointer to a variable receiving the mapped pointer in kernel address
**          space.
*/
gceSTATUS
gckOS_MapUserPointer(
    IN gckOS Os,
    IN gctPOINTER Pointer,
    IN gctSIZE_T Size,
    OUT gctPOINTER * KernelPointer
    )
{
    gcmkHEADER_ARG("Os=0x%X Pointer=0x%X Size=%lu", Os, Pointer, Size);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Pointer != gcvNULL);
    gcmkVERIFY_ARGUMENT(Size > 0);
    gcmkVERIFY_ARGUMENT(KernelPointer != gcvNULL);

    *KernelPointer = Pointer;

    gcmkFOOTER_ARG("*KernelPointer=0x%X", *KernelPointer);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_UnmapUserPointer
**
**  Unmap a user process pointer from the kernel address space.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Pointer
**          Pointer in user process space that needs to be unmapped.
**
**      gctSIZE_T Size
**          Number of bytes that need to be unmapped.
**
**      gctPOINTER KernelPointer
**          Pointer in kernel address space that needs to be unmapped.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_UnmapUserPointer(
    IN gckOS Os,
    IN gctPOINTER Pointer,
    IN gctSIZE_T Size,
    IN gctPOINTER KernelPointer
    )
{
    gcmkHEADER_ARG("Os=0x%X Pointer=0x%X Size=%lu KernelPointer=0x%X",
                   Os, Pointer, Size, KernelPointer);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_QueryNeedCopy
**
**  Query whether the memory can be accessed or mapped directly or it has to be
**  copied.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctUINT32 ProcessID
**          Process ID of the current process.
**
**  OUTPUT:
**
**      gctBOOL_PTR NeedCopy
**          Pointer to a boolean receiving gcvTRUE if the memory needs a copy or
**          gcvFALSE if the memory can be accessed or mapped dircetly.
*/
gceSTATUS
gckOS_QueryNeedCopy(
    IN gckOS Os,
    IN gctUINT32 ProcessID,
    OUT gctBOOL_PTR NeedCopy
    )
{
    gcmkHEADER_ARG("Os=0x%X ProcessID=%d", Os, ProcessID);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(NeedCopy != gcvNULL);

    /* We need to copy data. */
    *NeedCopy = gcvTRUE;

    /* Success. */
    gcmkFOOTER_ARG("*NeedCopy=%d", *NeedCopy);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_CopyFromUserData
**
**  Copy data from user to kernel memory.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER KernelPointer
**          Pointer to kernel memory.
**
**      gctPOINTER Pointer
**          Pointer to user memory.
**
**      gctSIZE_T Size
**          Number of bytes to copy.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_CopyFromUserData(
    IN gckOS Os,
    IN gctPOINTER KernelPointer,
    IN gctPOINTER Pointer,
    IN gctSIZE_T Size
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X KernelPointer=0x%X Pointer=0x%X Size=%lu",
                   Os, KernelPointer, Pointer, Size);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(KernelPointer != gcvNULL);
    gcmkVERIFY_ARGUMENT(Pointer != gcvNULL);
    gcmkVERIFY_ARGUMENT(Size > 0);

    /* Copy data from user. */
    if (copy_from_user(KernelPointer, Pointer, Size) != 0)
    {
        /* Could not copy all the bytes. */
        gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_CopyToUserData
**
**  Copy data from kernel to user memory.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER KernelPointer
**          Pointer to kernel memory.
**
**      gctPOINTER Pointer
**          Pointer to user memory.
**
**      gctSIZE_T Size
**          Number of bytes to copy.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_CopyToUserData(
    IN gckOS Os,
    IN gctPOINTER KernelPointer,
    IN gctPOINTER Pointer,
    IN gctSIZE_T Size
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X KernelPointer=0x%X Pointer=0x%X Size=%lu",
                   Os, KernelPointer, Pointer, Size);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(KernelPointer != gcvNULL);
    gcmkVERIFY_ARGUMENT(Pointer != gcvNULL);
    gcmkVERIFY_ARGUMENT(Size > 0);

    /* Copy data to user. */
    if (copy_to_user(Pointer, KernelPointer, Size) != 0)
    {
        /* Could not copy all the bytes. */
        gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_WriteMemory
**
**  Write data to a memory.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Address
**          Address of the memory to write to.
**
**      gctUINT32 Data
**          Data for register.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_WriteMemory(
    IN gckOS Os,
    IN gctPOINTER Address,
    IN gctUINT32 Data
    )
{
    gceSTATUS status;
    gcmkHEADER_ARG("Os=0x%X Address=0x%X Data=%u", Os, Address, Data);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    /* Write memory. */
    if (access_ok(VERIFY_WRITE, Address, 4))
    {
        /* User address. */
        if(put_user(Data, (gctUINT32*)Address))
        {
            gcmkONERROR(gcvSTATUS_INVALID_ADDRESS);
        }
    }
    else
    {
        /* Kernel address. */
        *(gctUINT32 *)Address = Data;
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckOS_ReadMappedPointer(
    IN gckOS Os,
    IN gctPOINTER Address,
    IN gctUINT32_PTR Data
    )
{
    gceSTATUS status;
    gcmkHEADER_ARG("Os=0x%X Address=0x%X Data=%u", Os, Address, Data);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    /* Write memory. */
    if (access_ok(VERIFY_READ, Address, 4))
    {
        /* User address. */
        if (get_user(*Data, (gctUINT32*)Address))
        {
            gcmkONERROR(gcvSTATUS_INVALID_ADDRESS);
        }
    }
    else
    {
        /* Kernel address. */
        *Data = *(gctUINT32_PTR)Address;
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_MapUserMemory
**
**  Lock down a user buffer and return an DMA'able address to be used by the
**  hardware to access it.
**
**  INPUT:
**
**      gctPOINTER Memory
**          Pointer to memory to lock down.
**
**      gctSIZE_T Size
**          Size in bytes of the memory to lock down.
**
**  OUTPUT:
**
**      gctPOINTER * Info
**          Pointer to variable receiving the information record required by
**          gckOS_UnmapUserMemory.
**
**      gctUINT32_PTR Address
**          Pointer to a variable that will receive the address DMA'able by the
**          hardware.
*/
gceSTATUS
gckOS_MapUserMemory(
    IN gckOS Os,
    IN gceCORE Core,
    IN gctPOINTER Memory,
    IN gctUINT32 Physical,
    IN gctSIZE_T Size,
    OUT gctPOINTER * Info,
    OUT gctUINT32_PTR Address
    )
{
    return gcvSTATUS_NOT_SUPPORTED;
}

/*******************************************************************************
**
**  gckOS_UnmapUserMemory
**
**  Unlock a user buffer and that was previously locked down by
**  gckOS_MapUserMemory.
**
**  INPUT:
**
**      gctPOINTER Memory
**          Pointer to memory to unlock.
**
**      gctSIZE_T Size
**          Size in bytes of the memory to unlock.
**
**      gctPOINTER Info
**          Information record returned by gckOS_MapUserMemory.
**
**      gctUINT32_PTR Address
**          The address returned by gckOS_MapUserMemory.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_UnmapUserMemory(
    IN gckOS Os,
    IN gceCORE Core,
    IN gctPOINTER Memory,
    IN gctSIZE_T Size,
    IN gctPOINTER Info,
    IN gctUINT32 Address
    )
{
    return gcvSTATUS_NOT_SUPPORTED;
}

/*******************************************************************************
**
**  gckOS_GetBaseAddress
**
**  Get the base address for the physical memory.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**  OUTPUT:
**
**      gctUINT32_PTR BaseAddress
**          Pointer to a variable that will receive the base address.
*/
gceSTATUS
gckOS_GetBaseAddress(
    IN gckOS Os,
    OUT gctUINT32_PTR BaseAddress
    )
{
    gcmkHEADER_ARG("Os=0x%X", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(BaseAddress != gcvNULL);

    /* Return base address. */
    *BaseAddress = Os->device->baseAddress;

    /* Success. */
    gcmkFOOTER_ARG("*BaseAddress=0x%08x", *BaseAddress);
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_SuspendInterrupt(
    IN gckOS Os
    )
{
    return gckOS_SuspendInterruptEx(Os, gcvCORE_MAJOR);
}

gceSTATUS
gckOS_SuspendInterruptEx(
    IN gckOS Os,
    IN gceCORE Core
    )
{
    gcmkHEADER_ARG("Os=0x%X Core=%d", Os, Core);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    disable_irq(Os->device->irqLines[Core]);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_ResumeInterrupt(
    IN gckOS Os
    )
{
    return gckOS_ResumeInterruptEx(Os, gcvCORE_MAJOR);
}

gceSTATUS
gckOS_ResumeInterruptEx(
    IN gckOS Os,
    IN gceCORE Core
    )
{
    gcmkHEADER_ARG("Os=0x%X Core=%d", Os, Core);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    enable_irq(Os->device->irqLines[Core]);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_MemCopy(
    IN gctPOINTER Destination,
    IN gctCONST_POINTER Source,
    IN gctSIZE_T Bytes
    )
{
    gcmkHEADER_ARG("Destination=0x%X Source=0x%X Bytes=%lu",
                   Destination, Source, Bytes);

    gcmkVERIFY_ARGUMENT(Destination != gcvNULL);
    gcmkVERIFY_ARGUMENT(Source != gcvNULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

    memcpy(Destination, Source, Bytes);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_ZeroMemory(
    IN gctPOINTER Memory,
    IN gctSIZE_T Bytes
    )
{
    gcmkHEADER_ARG("Memory=0x%X Bytes=%lu", Memory, Bytes);

    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

    memset(Memory, 0, Bytes);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
********************************* Cache Control ********************************
*******************************************************************************/

/*******************************************************************************
**  gckOS_CacheClean
**
**  Clean the cache for the specified addresses.  The GPU is going to need the
**  data.  If the system is allocating memory as non-cachable, this function can
**  be ignored.
**
**  ARGUMENTS:
**
**      gckOS Os
**          Pointer to gckOS object.
**
**      gctUINT32 ProcessID
**          Process ID Logical belongs.
**
**      gctPHYS_ADDR Handle
**          Physical address handle.  If gcvNULL it is video memory.
**
**      gctPOINTER Physical
**          Physical address to flush.
**
**      gctPOINTER Logical
**          Logical address to flush.
**
**      gctSIZE_T Bytes
**          Size of the address range in bytes to flush.
*/

/*

Following patch can be applied to kernel in case cache API is not exported.

diff --git a/arch/arm/mm/proc-syms.c b/arch/arm/mm/proc-syms.c
index 054b491..e9e74ec 100644
--- a/arch/arm/mm/proc-syms.c
+++ b/arch/arm/mm/proc-syms.c
@@ -30,6 +30,9 @@ EXPORT_SYMBOL(__cpuc_flush_user_all);
 EXPORT_SYMBOL(__cpuc_flush_user_range);
 EXPORT_SYMBOL(__cpuc_coherent_kern_range);
 EXPORT_SYMBOL(__cpuc_flush_dcache_area);
+EXPORT_SYMBOL(__glue(_CACHE,_dma_map_area));
+EXPORT_SYMBOL(__glue(_CACHE,_dma_unmap_area));
+EXPORT_SYMBOL(__glue(_CACHE,_dma_flush_range));
 #else
 EXPORT_SYMBOL(cpu_cache);
 #endif

*/
gceSTATUS
gckOS_CacheClean(
    IN gckOS Os,
    IN gctUINT32 ProcessID,
    IN gctPHYS_ADDR Handle,
    IN gctPHYS_ADDR_T Physical,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    )
{
    gcsPLATFORM * platform;

    gcmkHEADER_ARG("Os=0x%X ProcessID=%d Handle=0x%X Logical=0x%X Bytes=%lu",
                   Os, ProcessID, Handle, Logical, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

    platform = Os->device->platform;

    if (platform && platform->ops->cache)
    {
        platform->ops->cache(
            platform,
            ProcessID,
            Handle,
            Physical,
            Logical,
            Bytes,
            gcvCACHE_CLEAN
            );

        /* Success. */
        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

#if !gcdCACHE_FUNCTION_UNIMPLEMENTED
#if defined(CONFIG_ARM) || defined(CONFIG_ARM64)

#if defined (CONFIG_ARM)
    /* Inner cache. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)
    dmac_map_area(Logical, Bytes, DMA_TO_DEVICE);
#      else
    dmac_clean_range(Logical, Logical + Bytes);
#      endif

#elif defined(CONFIG_ARM64)
    __dma_map_area(Logical, Bytes, DMA_TO_DEVICE);
#endif

#if defined(CONFIG_OUTER_CACHE)
    /* Outer cache. */
    _HandleOuterCache(Os, Physical, Logical, Bytes, gcvCACHE_CLEAN);
#endif

#elif defined(CONFIG_MIPS)

    dma_cache_wback((unsigned long) Logical, Bytes);

#elif defined(CONFIG_PPC)

    /* TODO */

#else
    dma_sync_single_for_device(
              gcvNULL,
              (dma_addr_t)Physical,
              Bytes,
              DMA_TO_DEVICE);
#endif
#endif

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**  gckOS_CacheInvalidate
**
**  Invalidate the cache for the specified addresses. The GPU is going to need
**  data.  If the system is allocating memory as non-cachable, this function can
**  be ignored.
**
**  ARGUMENTS:
**
**      gckOS Os
**          Pointer to gckOS object.
**
**      gctUINT32 ProcessID
**          Process ID Logical belongs.
**
**      gctPHYS_ADDR Handle
**          Physical address handle.  If gcvNULL it is video memory.
**
**      gctPOINTER Logical
**          Logical address to flush.
**
**      gctSIZE_T Bytes
**          Size of the address range in bytes to flush.
*/
gceSTATUS
gckOS_CacheInvalidate(
    IN gckOS Os,
    IN gctUINT32 ProcessID,
    IN gctPHYS_ADDR Handle,
    IN gctPHYS_ADDR_T Physical,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    )
{
    gcsPLATFORM * platform;

    gcmkHEADER_ARG("Os=0x%X ProcessID=%d Handle=0x%X Logical=0x%X Bytes=%lu",
                   Os, ProcessID, Handle, Logical, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

    platform = Os->device->platform;

    if (platform && platform->ops->cache)
    {
        platform->ops->cache(
            platform,
            ProcessID,
            Handle,
            Physical,
            Logical,
            Bytes,
            gcvCACHE_INVALIDATE
            );

        /* Success. */
        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

#if !gcdCACHE_FUNCTION_UNIMPLEMENTED
#if defined(CONFIG_ARM) || defined(CONFIG_ARM64)

#if defined (CONFIG_ARM)
    /* Inner cache. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)
    dmac_map_area(Logical, Bytes, DMA_FROM_DEVICE);
#      else
    dmac_inv_range(Logical, Logical + Bytes);
#      endif
#elif defined(CONFIG_ARM64)
    __dma_map_area(Logical, Bytes, DMA_FROM_DEVICE);
#endif

#if defined(CONFIG_OUTER_CACHE)
    /* Outer cache. */
    _HandleOuterCache(Os, Physical, Logical, Bytes, gcvCACHE_INVALIDATE);
#endif

#elif defined(CONFIG_MIPS)
    dma_cache_inv((unsigned long) Logical, Bytes);
#elif defined(CONFIG_PPC)
    /* TODO */
#else
    dma_sync_single_for_device(
              gcvNULL,
              (dma_addr_t)Physical,
              Bytes,
              DMA_FROM_DEVICE);
#endif
#endif

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**  gckOS_CacheFlush
**
**  Clean the cache for the specified addresses and invalidate the lines as
**  well.  The GPU is going to need and modify the data.  If the system is
**  allocating memory as non-cachable, this function can be ignored.
**
**  ARGUMENTS:
**
**      gckOS Os
**          Pointer to gckOS object.
**
**      gctUINT32 ProcessID
**          Process ID Logical belongs.
**
**      gctPHYS_ADDR Handle
**          Physical address handle.  If gcvNULL it is video memory.
**
**      gctPOINTER Logical
**          Logical address to flush.
**
**      gctSIZE_T Bytes
**          Size of the address range in bytes to flush.
*/
gceSTATUS
gckOS_CacheFlush(
    IN gckOS Os,
    IN gctUINT32 ProcessID,
    IN gctPHYS_ADDR Handle,
    IN gctPHYS_ADDR_T Physical,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    )
{
    gcsPLATFORM * platform;

    gcmkHEADER_ARG("Os=0x%X ProcessID=%d Handle=0x%X Logical=0x%X Bytes=%lu",
                   Os, ProcessID, Handle, Logical, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

    platform = Os->device->platform;

    if (platform && platform->ops->cache)
    {
        platform->ops->cache(
            platform,
            ProcessID,
            Handle,
            Physical,
            Logical,
            Bytes,
            gcvCACHE_FLUSH
            );

        /* Success. */
        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

#if !gcdCACHE_FUNCTION_UNIMPLEMENTED
#if defined(CONFIG_ARM) || defined(CONFIG_ARM64)
#if defined (CONFIG_ARM)
    /* Inner cache. */
    dmac_flush_range(Logical, Logical + Bytes);
#elif defined (CONFIG_ARM64)
    __dma_flush_range(Logical, Logical + Bytes);
#endif

#if defined(CONFIG_OUTER_CACHE)
    /* Outer cache. */
    _HandleOuterCache(Os, Physical, Logical, Bytes, gcvCACHE_FLUSH);
#endif

#elif defined(CONFIG_MIPS)
    dma_cache_wback_inv((unsigned long) Logical, Bytes);
#elif defined(CONFIG_PPC)
    /* TODO */
#else
    dma_sync_single_for_device(
              gcvNULL,
              (dma_addr_t)Physical,
              Bytes,
              DMA_BIDIRECTIONAL);
#endif
#endif

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
********************************* Broadcasting *********************************
*******************************************************************************/

/*******************************************************************************
**
**  gckOS_Broadcast
**
**  System hook for broadcast events from the kernel driver.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object.
**
**      gceBROADCAST Reason
**          Reason for the broadcast.  Can be one of the following values:
**
**              gcvBROADCAST_GPU_IDLE
**                  Broadcasted when the kernel driver thinks the GPU might be
**                  idle.  This can be used to handle power management.
**
**              gcvBROADCAST_GPU_COMMIT
**                  Broadcasted when any client process commits a command
**                  buffer.  This can be used to handle power management.
**
**              gcvBROADCAST_GPU_STUCK
**                  Broadcasted when the kernel driver hits the timeout waiting
**                  for the GPU.
**
**              gcvBROADCAST_FIRST_PROCESS
**                  First process is trying to connect to the kernel.
**
**              gcvBROADCAST_LAST_PROCESS
**                  Last process has detached from the kernel.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_Broadcast(
    IN gckOS Os,
    IN gckHARDWARE Hardware,
    IN gceBROADCAST Reason
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X Hardware=0x%X Reason=%d", Os, Hardware, Reason);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    switch (Reason)
    {
    case gcvBROADCAST_FIRST_PROCESS:
        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_OS, "First process has attached");
        break;

    case gcvBROADCAST_LAST_PROCESS:
        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_OS, "Last process has detached");

        /* Put GPU OFF. */
        gcmkONERROR(
            gckHARDWARE_SetPowerManagementState(Hardware,
                                                gcvPOWER_OFF_BROADCAST));
        break;

    case gcvBROADCAST_GPU_IDLE:
        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_OS, "GPU idle.");

        /* Put GPU IDLE. */
        gcmkONERROR(
            gckHARDWARE_SetPowerManagementState(Hardware,
#if gcdPOWER_SUSPEND_WHEN_IDLE
                                                gcvPOWER_SUSPEND_BROADCAST));
#else
                                                gcvPOWER_IDLE_BROADCAST));
#endif

        /* Add idle process DB. */
        gcmkONERROR(gckKERNEL_AddProcessDB(Hardware->kernel,
                                           1,
                                           gcvDB_IDLE,
                                           gcvNULL, gcvNULL, 0));
        break;

    case gcvBROADCAST_GPU_COMMIT:
        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_OS, "COMMIT has arrived.");

        /* Add busy process DB. */
        gcmkONERROR(gckKERNEL_AddProcessDB(Hardware->kernel,
                                           0,
                                           gcvDB_IDLE,
                                           gcvNULL, gcvNULL, 0));

        /* Put GPU ON. */
        gcmkONERROR(
            gckHARDWARE_SetPowerManagementState(Hardware, gcvPOWER_ON_AUTO));
        break;

    case gcvBROADCAST_GPU_STUCK:
        gcmkTRACE_N(gcvLEVEL_ERROR, 0, "gcvBROADCAST_GPU_STUCK\n");
        gcmkONERROR(gckKERNEL_Recovery(Hardware->kernel));
        break;

    case gcvBROADCAST_AXI_BUS_ERROR:
        gcmkTRACE_N(gcvLEVEL_ERROR, 0, "gcvBROADCAST_AXI_BUS_ERROR\n");
        gcmkONERROR(gckHARDWARE_DumpGPUState(Hardware));
        gcmkONERROR(gckKERNEL_Recovery(Hardware->kernel));
        break;

    case gcvBROADCAST_OUT_OF_MEMORY:
        gcmkTRACE_N(gcvLEVEL_INFO, 0, "gcvBROADCAST_OUT_OF_MEMORY\n");

        status = _ShrinkMemory(Os);

        if (status == gcvSTATUS_NOT_SUPPORTED)
        {
            goto OnError;
        }

        gcmkONERROR(status);

        break;

    default:
        /* Skip unimplemented broadcast. */
        break;
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_BroadcastHurry
**
**  The GPU is running too slow.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object.
**
**      gctUINT Urgency
**          The higher the number, the higher the urgency to speed up the GPU.
**          The maximum value is defined by the gcdDYNAMIC_EVENT_THRESHOLD.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_BroadcastHurry(
    IN gckOS Os,
    IN gckHARDWARE Hardware,
    IN gctUINT Urgency
    )
{
    gcmkHEADER_ARG("Os=0x%x Hardware=0x%x Urgency=%u", Os, Hardware, Urgency);

    /* Do whatever you need to do to speed up the GPU now. */

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_BroadcastCalibrateSpeed
**
**  Calibrate the speed of the GPU.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object.
**
**      gctUINT Idle, Time
**          Idle/Time will give the percentage the GPU is idle, so you can use
**          this to calibrate the working point of the GPU.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_BroadcastCalibrateSpeed(
    IN gckOS Os,
    IN gckHARDWARE Hardware,
    IN gctUINT Idle,
    IN gctUINT Time
    )
{
    gcmkHEADER_ARG("Os=0x%x Hardware=0x%x Idle=%u Time=%u",
                   Os, Hardware, Idle, Time);

    /* Do whatever you need to do to callibrate the GPU speed. */

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
********************************** Semaphores **********************************
*******************************************************************************/

/*******************************************************************************
**
**  gckOS_CreateSemaphore
**
**  Create a semaphore.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**  OUTPUT:
**
**      gctPOINTER * Semaphore
**          Pointer to the variable that will receive the created semaphore.
*/
gceSTATUS
gckOS_CreateSemaphore(
    IN gckOS Os,
    OUT gctPOINTER * Semaphore
    )
{
    gceSTATUS status;
    struct semaphore *sem = gcvNULL;

    gcmkHEADER_ARG("Os=0x%X", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Semaphore != gcvNULL);

    /* Allocate the semaphore structure. */
    sem = (struct semaphore *)kmalloc(gcmSIZEOF(struct semaphore), GFP_KERNEL | gcdNOWARN);
    if (sem == gcvNULL)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    /* Initialize the semaphore. */
    sema_init(sem, 1);

    /* Return to caller. */
    *Semaphore = (gctPOINTER) sem;

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_AcquireSemaphore
**
**  Acquire a semaphore.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gctPOINTER Semaphore
**          Pointer to the semaphore thet needs to be acquired.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_AcquireSemaphore(
    IN gckOS Os,
    IN gctPOINTER Semaphore
    )
{
    gcmkHEADER_ARG("Os=0x%08X Semaphore=0x%08X", Os, Semaphore);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Semaphore != gcvNULL);

    /* Acquire the semaphore. */
    down((struct semaphore *) Semaphore);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_TryAcquireSemaphore
**
**  Try to acquire a semaphore.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gctPOINTER Semaphore
**          Pointer to the semaphore thet needs to be acquired.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_TryAcquireSemaphore(
    IN gckOS Os,
    IN gctPOINTER Semaphore
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%x", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Semaphore != gcvNULL);

    /* Acquire the semaphore. */
    if (down_trylock((struct semaphore *) Semaphore))
    {
        /* Timeout. */
        status = gcvSTATUS_TIMEOUT;
        gcmkFOOTER();
        return status;
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_ReleaseSemaphore
**
**  Release a previously acquired semaphore.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gctPOINTER Semaphore
**          Pointer to the semaphore thet needs to be released.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_ReleaseSemaphore(
    IN gckOS Os,
    IN gctPOINTER Semaphore
    )
{
    gcmkHEADER_ARG("Os=0x%X Semaphore=0x%X", Os, Semaphore);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Semaphore != gcvNULL);

    /* Release the semaphore. */
    up((struct semaphore *) Semaphore);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_DestroySemaphore
**
**  Destroy a semaphore.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gctPOINTER Semaphore
**          Pointer to the semaphore thet needs to be destroyed.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_DestroySemaphore(
    IN gckOS Os,
    IN gctPOINTER Semaphore
    )
{
    gcmkHEADER_ARG("Os=0x%X Semaphore=0x%X", Os, Semaphore);

     /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Semaphore != gcvNULL);

    /* Free the sempahore structure. */
    kfree(Semaphore);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_GetProcessID
**
**  Get current process ID.
**
**  INPUT:
**
**      Nothing.
**
**  OUTPUT:
**
**      gctUINT32_PTR ProcessID
**          Pointer to the variable that receives the process ID.
*/
gceSTATUS
gckOS_GetProcessID(
    OUT gctUINT32_PTR ProcessID
    )
{
    /* Get process ID. */
    if (ProcessID != gcvNULL)
    {
        *ProcessID = _GetProcessID();
    }

    /* Success. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_GetThreadID
**
**  Get current thread ID.
**
**  INPUT:
**
**      Nothing.
**
**  OUTPUT:
**
**      gctUINT32_PTR ThreadID
**          Pointer to the variable that receives the thread ID.
*/
gceSTATUS
gckOS_GetThreadID(
    OUT gctUINT32_PTR ThreadID
    )
{
    /* Get thread ID. */
    if (ThreadID != gcvNULL)
    {
        *ThreadID = _GetThreadID();
    }

    /* Success. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_SetGPUPower
**
**  Set the power of the GPU on or off.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      gceCORE Core
**          GPU whose power is set.
**
**      gctBOOL Clock
**          gcvTRUE to turn on the clock, or gcvFALSE to turn off the clock.
**
**      gctBOOL Power
**          gcvTRUE to turn on the power, or gcvFALSE to turn off the power.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_SetGPUPower(
    IN gckOS Os,
    IN gceCORE Core,
    IN gctBOOL Clock,
    IN gctBOOL Power
    )
{
    gcsPLATFORM * platform;

    gctBOOL powerChange = gcvFALSE;
    gctBOOL clockChange = gcvFALSE;

    gcmkHEADER_ARG("Os=0x%X Core=%d Clock=%d Power=%d", Os, Core, Clock, Power);
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    platform = Os->device->platform;

    powerChange = (Power != Os->powerStates[Core]);

    clockChange = (Clock != Os->clockStates[Core]);

    if (powerChange && (Power == gcvTRUE))
    {
        if (platform && platform->ops->setPower)
        {
            gcmkVERIFY_OK(platform->ops->setPower(platform, Core, Power));
        }

        Os->powerStates[Core] = Power;
    }

    if (clockChange)
    {
        mutex_lock(&Os->registerAccessLocks[Core]);

        if (platform && platform->ops->setClock)
        {
            gcmkVERIFY_OK(platform->ops->setClock(platform, Core, Clock));
        }

        Os->clockStates[Core] = Clock;

        mutex_unlock(&Os->registerAccessLocks[Core]);
    }

    if (powerChange && (Power == gcvFALSE))
    {
        if (platform && platform->ops->setPower)
        {
            gcmkVERIFY_OK(platform->ops->setPower(platform, Core, Power));
        }

        Os->powerStates[Core] = Power;
    }

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_ResetGPU
**
**  Reset the GPU.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      gckCORE Core
**          GPU whose power is set.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_ResetGPU(
    IN gckOS Os,
    IN gceCORE Core
    )
{
    gceSTATUS status = gcvSTATUS_NOT_SUPPORTED;
    gcsPLATFORM * platform;

    gcmkHEADER_ARG("Os=0x%X Core=%d", Os, Core);
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    platform = Os->device->platform;

    if (platform && platform->ops->reset)
    {
        status = platform->ops->reset(platform, Core);
    }

    gcmkFOOTER_NO();
    return status;
}

/*******************************************************************************
**
**  gckOS_PrepareGPUFrequency
**
**  Prepare to set GPU frequency and voltage.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      gckCORE Core
**          GPU whose frequency and voltage will be set.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_PrepareGPUFrequency(
    IN gckOS Os,
    IN gceCORE Core
    )
{
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_FinishGPUFrequency
**
**  Finish GPU frequency setting.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      gckCORE Core
**          GPU whose frequency and voltage is set.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_FinishGPUFrequency(
    IN gckOS Os,
    IN gceCORE Core
    )
{
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_QueryGPUFrequency
**
**  Query the current frequency of the GPU.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      gckCORE Core
**          GPU whose power is set.
**
**      gctUINT32 * Frequency
**          Pointer to a gctUINT32 to obtain current frequency, in MHz.
**
**      gctUINT8 * Scale
**          Pointer to a gctUINT8 to obtain current scale(1 - 64).
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_QueryGPUFrequency(
    IN gckOS Os,
    IN gceCORE Core,
    OUT gctUINT32 * Frequency,
    OUT gctUINT8 * Scale
    )
{
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_SetGPUFrequency
**
**  Set frequency and voltage of the GPU.
**
**      1. DVFS manager gives the target scale of full frequency, BSP must find
**         a real frequency according to this scale and board's configure.
**
**      2. BSP should find a suitable voltage for this frequency.
**
**      3. BSP must make sure setting take effect before this function returns.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      gckCORE Core
**          GPU whose power is set.
**
**      gctUINT8 Scale
**          Target scale of full frequency, range is [1, 64]. 1 means 1/64 of
**          full frequency and 64 means 64/64 of full frequency.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_SetGPUFrequency(
    IN gckOS Os,
    IN gceCORE Core,
    IN gctUINT8 Scale
    )
{
    return gcvSTATUS_OK;
}

/*----------------------------------------------------------------------------*/
/*----- Profile --------------------------------------------------------------*/

gceSTATUS
gckOS_GetProfileTick(
    OUT gctUINT64_PTR Tick
    )
{
    struct timespec time;

    ktime_get_ts(&time);

    *Tick = time.tv_nsec + time.tv_sec * 1000000000ULL;

    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_QueryProfileTickRate(
    OUT gctUINT64_PTR TickRate
    )
{
    struct timespec res;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
    res.tv_sec = 0;
    res.tv_nsec = hrtimer_resolution;
#else
    hrtimer_get_res(CLOCK_MONOTONIC, &res);
#endif

    *TickRate = res.tv_nsec + res.tv_sec * 1000000000ULL;

    return gcvSTATUS_OK;
}

gctUINT32
gckOS_ProfileToMS(
    IN gctUINT64 Ticks
    )
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,23)
    return div_u64(Ticks, 1000000);
#else
    gctUINT64 rem = Ticks;
    gctUINT64 b = 1000000;
    gctUINT64 res, d = 1;
    gctUINT32 high = rem >> 32;

    /* Reduce the thing a bit first */
    res = 0;
    if (high >= 1000000)
    {
        high /= 1000000;
        res   = (gctUINT64) high << 32;
        rem  -= (gctUINT64) (high * 1000000) << 32;
    }

    while (((gctINT64) b > 0) && (b < rem))
    {
        b <<= 1;
        d <<= 1;
    }

    do
    {
        if (rem >= b)
        {
            rem -= b;
            res += d;
        }

        b >>= 1;
        d >>= 1;
    }
    while (d);

    return (gctUINT32) res;
#endif
}

/******************************************************************************\
******************************* Signal Management ******************************
\******************************************************************************/

#undef _GC_OBJ_ZONE
#define _GC_OBJ_ZONE    gcvZONE_SIGNAL

/*******************************************************************************
**
**  gckOS_CreateSignal
**
**  Create a new signal.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctBOOL ManualReset
**          If set to gcvTRUE, gckOS_Signal with gcvFALSE must be called in
**          order to set the signal to nonsignaled state.
**          If set to gcvFALSE, the signal will automatically be set to
**          nonsignaled state by gckOS_WaitSignal function.
**
**  OUTPUT:
**
**      gctSIGNAL * Signal
**          Pointer to a variable receiving the created gctSIGNAL.
*/
gceSTATUS
gckOS_CreateSignal(
    IN gckOS Os,
    IN gctBOOL ManualReset,
    OUT gctSIGNAL * Signal
    )
{
    gceSTATUS status;
    gcsSIGNAL_PTR signal;

    gcmkHEADER_ARG("Os=0x%X ManualReset=%d", Os, ManualReset);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Signal != gcvNULL);

    /* Create an event structure. */
    signal = (gcsSIGNAL_PTR) kmalloc(sizeof(gcsSIGNAL), GFP_KERNEL | gcdNOWARN);

    if (signal == gcvNULL)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    /* Save the process ID. */
    signal->process = (gctHANDLE)(gctUINTPTR_T) _GetProcessID();
    signal->manualReset = ManualReset;
    init_completion(&signal->obj);
    atomic_set(&signal->ref, 1);

    gcmkONERROR(_AllocateIntegerId(&Os->signalDB, signal, &signal->id));

    *Signal = (gctSIGNAL)(gctUINTPTR_T)signal->id;

    gcmkFOOTER_ARG("*Signal=0x%X", *Signal);
    return gcvSTATUS_OK;

OnError:
    if (signal != gcvNULL)
    {
        kfree(signal);
    }

    gcmkFOOTER_NO();
    return status;
}

/*******************************************************************************
**
**  gckOS_DestroySignal
**
**  Destroy a signal.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctSIGNAL Signal
**          Pointer to the gctSIGNAL.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_DestroySignal(
    IN gckOS Os,
    IN gctSIGNAL Signal
    )
{
    gceSTATUS status;
    gcsSIGNAL_PTR signal;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Os=0x%X Signal=0x%X", Os, Signal);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Signal != gcvNULL);

    gcmkONERROR(gckOS_AcquireMutex(Os, Os->signalMutex, gcvINFINITE));
    acquired = gcvTRUE;

    gcmkONERROR(_QueryIntegerId(&Os->signalDB, (gctUINT32)(gctUINTPTR_T)Signal, (gctPOINTER)&signal));

    gcmkASSERT(signal->id == (gctUINT32)(gctUINTPTR_T)Signal);

    if (atomic_dec_and_test(&signal->ref))
    {
        gcmkVERIFY_OK(_DestroyIntegerId(&Os->signalDB, signal->id));

        /* Free the sgianl. */
        kfree(signal);
    }

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Os, Os->signalMutex));
    acquired = gcvFALSE;

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Os, Os->signalMutex));
    }

    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_Signal
**
**  Set a state of the specified signal.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctSIGNAL Signal
**          Pointer to the gctSIGNAL.
**
**      gctBOOL State
**          If gcvTRUE, the signal will be set to signaled state.
**          If gcvFALSE, the signal will be set to nonsignaled state.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_Signal(
    IN gckOS Os,
    IN gctSIGNAL Signal,
    IN gctBOOL State
    )
{
    gceSTATUS status;
    gcsSIGNAL_PTR signal;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Os=0x%X Signal=0x%X State=%d", Os, Signal, State);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Signal != gcvNULL);

    gcmkONERROR(gckOS_AcquireMutex(Os, Os->signalMutex, gcvINFINITE));
    acquired = gcvTRUE;

    gcmkONERROR(_QueryIntegerId(&Os->signalDB, (gctUINT32)(gctUINTPTR_T)Signal, (gctPOINTER)&signal));

    gcmkASSERT(signal->id == (gctUINT32)(gctUINTPTR_T)Signal);

    if (State)
    {
        /* Set the event to a signaled state. */
        complete(&signal->obj);
    }
    else
    {
        /* Set the event to an unsignaled state. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,13,0)
        reinit_completion(&signal->obj);
#else
        INIT_COMPLETION(signal->obj);
#endif
    }

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Os, Os->signalMutex));
    acquired = gcvFALSE;

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Os, Os->signalMutex));
    }

    gcmkFOOTER();
    return status;
}

#if gcdENABLE_VG
gceSTATUS
gckOS_SetSignalVG(
    IN gckOS Os,
    IN gctHANDLE Process,
    IN gctSIGNAL Signal
    )
{
    gceSTATUS status;
    gctINT result;
    struct task_struct * userTask;
    struct siginfo info;

    userTask = FIND_TASK_BY_PID((pid_t)(gctUINTPTR_T) Process);

    if (userTask != gcvNULL)
    {
        info.si_signo = 48;
        info.si_code  = __SI_CODE(__SI_RT, SI_KERNEL);
        info.si_pid   = 0;
        info.si_uid   = 0;
        info.si_ptr   = (gctPOINTER) Signal;

        /* Signals with numbers between 32 and 63 are real-time,
           send a real-time signal to the user process. */
        result = send_sig_info(48, &info, userTask);

        printk("gckOS_SetSignalVG:0x%x\n", result);
        /* Error? */
        if (result < 0)
        {
            status = gcvSTATUS_GENERIC_IO;

            gcmkTRACE(
                gcvLEVEL_ERROR,
                "%s(%d): an error has occurred.\n",
                __FUNCTION__, __LINE__
                );
        }
        else
        {
            status = gcvSTATUS_OK;
        }
    }
    else
    {
        status = gcvSTATUS_GENERIC_IO;

        gcmkTRACE(
            gcvLEVEL_ERROR,
            "%s(%d): an error has occurred.\n",
            __FUNCTION__, __LINE__
            );
    }

    /* Return status. */
    return status;
}
#endif

/*******************************************************************************
**
**  gckOS_UserSignal
**
**  Set the specified signal which is owned by a process to signaled state.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctSIGNAL Signal
**          Pointer to the gctSIGNAL.
**
**      gctHANDLE Process
**          Handle of process owning the signal.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_UserSignal(
    IN gckOS Os,
    IN gctSIGNAL Signal,
    IN gctHANDLE Process
    )
{
    gceSTATUS status;
    gctSIGNAL signal;

    gcmkHEADER_ARG("Os=0x%X Signal=0x%X Process=%d",
                   Os, Signal, (gctINT32)(gctUINTPTR_T)Process);

    /* Map the signal into kernel space. */
    gcmkONERROR(gckOS_MapSignal(Os, Signal, Process, &signal));

    /* Signal. */
    status = gckOS_Signal(Os, signal, gcvTRUE);

    /* Unmap the signal */
    gcmkVERIFY_OK(gckOS_UnmapSignal(Os, Signal));

    gcmkFOOTER();
    return status;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_WaitSignal
**
**  Wait for a signal to become signaled.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctSIGNAL Signal
**          Pointer to the gctSIGNAL.
**
**      gctUINT32 Wait
**          Number of milliseconds to wait.
**          Pass the value of gcvINFINITE for an infinite wait.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_WaitSignal(
    IN gckOS Os,
    IN gctSIGNAL Signal,
    IN gctBOOL Interruptable,
    IN gctUINT32 Wait
    )
{
    gceSTATUS status = gcvSTATUS_OK;
    gcsSIGNAL_PTR signal;

    gcmkHEADER_ARG("Os=0x%X Signal=0x%X Wait=0x%08X", Os, Signal, Wait);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Signal != gcvNULL);

    gcmkONERROR(_QueryIntegerId(&Os->signalDB, (gctUINT32)(gctUINTPTR_T)Signal, (gctPOINTER)&signal));

    gcmkASSERT(signal->id == (gctUINT32)(gctUINTPTR_T)Signal);

    might_sleep();

    spin_lock_irq(&signal->obj.wait.lock);

    if (signal->obj.done)
    {
        if (!signal->manualReset)
        {
            signal->obj.done = 0;
        }

        status = gcvSTATUS_OK;
    }
    else if (Wait == 0)
    {
        status = gcvSTATUS_TIMEOUT;
    }
    else
    {
        /* Convert wait to milliseconds. */
        long timeout = (Wait == gcvINFINITE)
            ? MAX_SCHEDULE_TIMEOUT
            : msecs_to_jiffies(Wait);

        DECLARE_WAITQUEUE(wait, current);
        wait.flags |= WQ_FLAG_EXCLUSIVE;
        __add_wait_queue_tail(&signal->obj.wait, &wait);

        while (gcvTRUE)
        {
            if (Interruptable && signal_pending(current))
            {
                /* Interrupt received. */
                status = gcvSTATUS_INTERRUPTED;
                break;
            }

            __set_current_state(TASK_INTERRUPTIBLE);
            spin_unlock_irq(&signal->obj.wait.lock);
            timeout = schedule_timeout(timeout);
            spin_lock_irq(&signal->obj.wait.lock);

            if (signal->obj.done)
            {
                if (!signal->manualReset)
                {
                    signal->obj.done = 0;
                }

                status = gcvSTATUS_OK;
                break;
            }

            if (timeout == 0)
            {

                status = gcvSTATUS_TIMEOUT;
                break;
            }
        }

        __remove_wait_queue(&signal->obj.wait, &wait);
    }

    spin_unlock_irq(&signal->obj.wait.lock);

OnError:
    /* Return status. */
    gcmkFOOTER_ARG("Signal=0x%X status=%d", Signal, status);
    return status;
}

/*******************************************************************************
**
**  gckOS_MapSignal
**
**  Map a signal in to the current process space.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctSIGNAL Signal
**          Pointer to tha gctSIGNAL to map.
**
**      gctHANDLE Process
**          Handle of process owning the signal.
**
**  OUTPUT:
**
**      gctSIGNAL * MappedSignal
**          Pointer to a variable receiving the mapped gctSIGNAL.
*/
gceSTATUS
gckOS_MapSignal(
    IN gckOS Os,
    IN gctSIGNAL Signal,
    IN gctHANDLE Process,
    OUT gctSIGNAL * MappedSignal
    )
{
    gceSTATUS status;
    gcsSIGNAL_PTR signal;
    gcmkHEADER_ARG("Os=0x%X Signal=0x%X Process=0x%X", Os, Signal, Process);

    gcmkVERIFY_ARGUMENT(Signal != gcvNULL);
    gcmkVERIFY_ARGUMENT(MappedSignal != gcvNULL);

    gcmkONERROR(_QueryIntegerId(&Os->signalDB, (gctUINT32)(gctUINTPTR_T)Signal, (gctPOINTER)&signal));

    if(atomic_inc_return(&signal->ref) <= 1)
    {
        /* The previous value is 0, it has been deleted. */
        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    *MappedSignal = (gctSIGNAL) Signal;

    /* Success. */
    gcmkFOOTER_ARG("*MappedSignal=0x%X", *MappedSignal);
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER_NO();
    return status;
}

/*******************************************************************************
**
**  gckOS_UnmapSignal
**
**  Unmap a signal .
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctSIGNAL Signal
**          Pointer to that gctSIGNAL mapped.
*/
gceSTATUS
gckOS_UnmapSignal(
    IN gckOS Os,
    IN gctSIGNAL Signal
    )
{
    return gckOS_DestroySignal(Os, Signal);
}

/*******************************************************************************
**
**  gckOS_CreateUserSignal
**
**  Create a new signal to be used in the user space.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctBOOL ManualReset
**          If set to gcvTRUE, gckOS_Signal with gcvFALSE must be called in
**          order to set the signal to nonsignaled state.
**          If set to gcvFALSE, the signal will automatically be set to
**          nonsignaled state by gckOS_WaitSignal function.
**
**  OUTPUT:
**
**      gctINT * SignalID
**          Pointer to a variable receiving the created signal's ID.
*/
gceSTATUS
gckOS_CreateUserSignal(
    IN gckOS Os,
    IN gctBOOL ManualReset,
    OUT gctINT * SignalID
    )
{
    gceSTATUS status;
    gctSIZE_T signal;

    /* Create a new signal. */
    gcmkONERROR(gckOS_CreateSignal(Os, ManualReset, (gctSIGNAL *) &signal));
    *SignalID = (gctINT) signal;

OnError:
    return status;
}

/*******************************************************************************
**
**  gckOS_DestroyUserSignal
**
**  Destroy a signal to be used in the user space.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctINT SignalID
**          The signal's ID.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_DestroyUserSignal(
    IN gckOS Os,
    IN gctINT SignalID
    )
{
    return gckOS_DestroySignal(Os, (gctSIGNAL)(gctUINTPTR_T)SignalID);
}

/*******************************************************************************
**
**  gckOS_WaitUserSignal
**
**  Wait for a signal used in the user mode to become signaled.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctINT SignalID
**          Signal ID.
**
**      gctUINT32 Wait
**          Number of milliseconds to wait.
**          Pass the value of gcvINFINITE for an infinite wait.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_WaitUserSignal(
    IN gckOS Os,
    IN gctINT SignalID,
    IN gctUINT32 Wait
    )
{
    return gckOS_WaitSignal(Os, (gctSIGNAL)(gctUINTPTR_T)SignalID, gcvTRUE, Wait);
}

/*******************************************************************************
**
**  gckOS_SignalUserSignal
**
**  Set a state of the specified signal to be used in the user space.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctINT SignalID
**          SignalID.
**
**      gctBOOL State
**          If gcvTRUE, the signal will be set to signaled state.
**          If gcvFALSE, the signal will be set to nonsignaled state.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_SignalUserSignal(
    IN gckOS Os,
    IN gctINT SignalID,
    IN gctBOOL State
    )
{
    return gckOS_Signal(Os, (gctSIGNAL)(gctUINTPTR_T)SignalID, State);
}

#if gcdENABLE_VG
gceSTATUS
gckOS_CreateSemaphoreVG(
    IN gckOS Os,
    OUT gctSEMAPHORE * Semaphore
    )
{
    gceSTATUS status;
    struct semaphore * newSemaphore;

    gcmkHEADER_ARG("Os=0x%X Semaphore=0x%x", Os, Semaphore);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Semaphore != gcvNULL);

    do
    {
        /* Allocate the semaphore structure. */
        newSemaphore = (struct semaphore *)kmalloc(gcmSIZEOF(struct semaphore), GFP_KERNEL | gcdNOWARN);
        if (newSemaphore == gcvNULL)
        {
            gcmkERR_BREAK(gcvSTATUS_OUT_OF_MEMORY);
        }

        /* Initialize the semaphore. */
        sema_init(newSemaphore, 0);

        /* Set the handle. */
        * Semaphore = (gctSEMAPHORE) newSemaphore;

        /* Success. */
        status = gcvSTATUS_OK;
    }
    while (gcvFALSE);

    gcmkFOOTER();
    /* Return the status. */
    return status;
}


gceSTATUS
gckOS_IncrementSemaphore(
    IN gckOS Os,
    IN gctSEMAPHORE Semaphore
    )
{
    gcmkHEADER_ARG("Os=0x%X Semaphore=0x%x", Os, Semaphore);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Semaphore != gcvNULL);

    /* Increment the semaphore's count. */
    up((struct semaphore *) Semaphore);

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_DecrementSemaphore(
    IN gckOS Os,
    IN gctSEMAPHORE Semaphore
    )
{
    gceSTATUS status;
    gctINT result;

    gcmkHEADER_ARG("Os=0x%X Semaphore=0x%x", Os, Semaphore);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Semaphore != gcvNULL);

    do
    {
        /* Decrement the semaphore's count. If the count is zero, wait
           until it gets incremented. */
        result = down_interruptible((struct semaphore *) Semaphore);

        /* Signal received? */
        if (result != 0)
        {
            status = gcvSTATUS_TERMINATE;
            break;
        }

        /* Success. */
        status = gcvSTATUS_OK;
    }
    while (gcvFALSE);

    gcmkFOOTER();
    /* Return the status. */
    return status;
}

/*******************************************************************************
**
**  gckOS_SetSignal
**
**  Set the specified signal to signaled state.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gctHANDLE Process
**          Handle of process owning the signal.
**
**      gctSIGNAL Signal
**          Pointer to the gctSIGNAL.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_SetSignal(
    IN gckOS Os,
    IN gctHANDLE Process,
    IN gctSIGNAL Signal
    )
{
    gceSTATUS status;
    gctINT result;
    struct task_struct * userTask;
    struct siginfo info;

    userTask = FIND_TASK_BY_PID((pid_t)(gctUINTPTR_T) Process);

    if (userTask != gcvNULL)
    {
        info.si_signo = 48;
        info.si_code  = __SI_CODE(__SI_RT, SI_KERNEL);
        info.si_pid   = 0;
        info.si_uid   = 0;
        info.si_ptr   = (gctPOINTER) Signal;

        /* Signals with numbers between 32 and 63 are real-time,
           send a real-time signal to the user process. */
        result = send_sig_info(48, &info, userTask);

        /* Error? */
        if (result < 0)
        {
            status = gcvSTATUS_GENERIC_IO;

            gcmkTRACE(
                gcvLEVEL_ERROR,
                "%s(%d): an error has occurred.\n",
                __FUNCTION__, __LINE__
                );
        }
        else
        {
            status = gcvSTATUS_OK;
        }
    }
    else
    {
        status = gcvSTATUS_GENERIC_IO;

        gcmkTRACE(
            gcvLEVEL_ERROR,
            "%s(%d): an error has occurred.\n",
            __FUNCTION__, __LINE__
            );
    }

    /* Return status. */
    return status;
}

/******************************************************************************\
******************************** Thread Object *********************************
\******************************************************************************/

gceSTATUS
gckOS_StartThread(
    IN gckOS Os,
    IN gctTHREADFUNC ThreadFunction,
    IN gctPOINTER ThreadParameter,
    OUT gctTHREAD * Thread
    )
{
    gceSTATUS status;
    struct task_struct * thread;

    gcmkHEADER_ARG("Os=0x%X ", Os);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(ThreadFunction != gcvNULL);
    gcmkVERIFY_ARGUMENT(Thread != gcvNULL);

    do
    {
        /* Create the thread. */
        thread = kthread_create(
            ThreadFunction,
            ThreadParameter,
            "Vivante Kernel Thread"
            );

        /* Failed? */
        if (IS_ERR(thread))
        {
            status = gcvSTATUS_GENERIC_IO;
            break;
        }

        /* Start the thread. */
        wake_up_process(thread);

        /* Set the thread handle. */
        * Thread = (gctTHREAD) thread;

        /* Success. */
        status = gcvSTATUS_OK;
    }
    while (gcvFALSE);

    gcmkFOOTER();
    /* Return the status. */
    return status;
}

gceSTATUS
gckOS_StopThread(
    IN gckOS Os,
    IN gctTHREAD Thread
    )
{
    gcmkHEADER_ARG("Os=0x%X Thread=0x%x", Os, Thread);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Thread != gcvNULL);

    /* Thread should have already been enabled to terminate. */
    kthread_stop((struct task_struct *) Thread);

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_VerifyThread(
    IN gckOS Os,
    IN gctTHREAD Thread
    )
{
    gcmkHEADER_ARG("Os=0x%X Thread=0x%x", Os, Thread);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Thread != gcvNULL);

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}
#endif

/******************************************************************************\
******************************** Software Timer ********************************
\******************************************************************************/

void
_TimerFunction(
    struct work_struct * work
    )
{
    gcsOSTIMER_PTR timer = (gcsOSTIMER_PTR)work;

    gctTIMERFUNCTION function = timer->function;

    function(timer->data);
}

/*******************************************************************************
**
**  gckOS_CreateTimer
**
**  Create a software timer.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gctTIMERFUNCTION Function.
**          Pointer to a call back function which will be called when timer is
**          expired.
**
**      gctPOINTER Data.
**          Private data which will be passed to call back function.
**
**  OUTPUT:
**
**      gctPOINTER * Timer
**          Pointer to a variable receiving the created timer.
*/
gceSTATUS
gckOS_CreateTimer(
    IN gckOS Os,
    IN gctTIMERFUNCTION Function,
    IN gctPOINTER Data,
    OUT gctPOINTER * Timer
    )
{
    gceSTATUS status;
    gcsOSTIMER_PTR pointer;
    gcmkHEADER_ARG("Os=0x%X Function=0x%X Data=0x%X", Os, Function, Data);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Timer != gcvNULL);

    gcmkONERROR(gckOS_Allocate(Os, sizeof(gcsOSTIMER), (gctPOINTER)&pointer));

    pointer->function = Function;
    pointer->data = Data;

    INIT_DELAYED_WORK(&pointer->work, _TimerFunction);

    *Timer = pointer;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_DestroyTimer
**
**  Destory a software timer.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gctPOINTER Timer
**          Pointer to the timer to be destoryed.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_DestroyTimer(
    IN gckOS Os,
    IN gctPOINTER Timer
    )
{
    gcsOSTIMER_PTR timer;
    gcmkHEADER_ARG("Os=0x%X Timer=0x%X", Os, Timer);

    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Timer != gcvNULL);

    timer = (gcsOSTIMER_PTR)Timer;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23)
    cancel_delayed_work_sync(&timer->work);
#else
    cancel_delayed_work(&timer->work);
    flush_workqueue(Os->workqueue);
#endif

    gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Os, Timer));

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_StartTimer
**
**  Schedule a software timer.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gctPOINTER Timer
**          Pointer to the timer to be scheduled.
**
**      gctUINT32 Delay
**          Delay in milliseconds.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_StartTimer(
    IN gckOS Os,
    IN gctPOINTER Timer,
    IN gctUINT32 Delay
    )
{
    gcsOSTIMER_PTR timer;

    gcmkHEADER_ARG("Os=0x%X Timer=0x%X Delay=%u", Os, Timer, Delay);

    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Timer != gcvNULL);
    gcmkVERIFY_ARGUMENT(Delay != 0);

    timer = (gcsOSTIMER_PTR)Timer;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)
    mod_delayed_work(Os->workqueue, &timer->work, msecs_to_jiffies(Delay));
#else
    if (unlikely(delayed_work_pending(&timer->work)))
    {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23)
        cancel_delayed_work_sync(&timer->work);
#else
        cancel_delayed_work(&timer->work);
        flush_workqueue(Os->workqueue);
#endif
    }

    queue_delayed_work(Os->workqueue, &timer->work, msecs_to_jiffies(Delay));
#endif

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_StopTimer
**
**  Cancel a unscheduled timer.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gctPOINTER Timer
**          Pointer to the timer to be cancel.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_StopTimer(
    IN gckOS Os,
    IN gctPOINTER Timer
    )
{
    gcsOSTIMER_PTR timer;
    gcmkHEADER_ARG("Os=0x%X Timer=0x%X", Os, Timer);

    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Timer != gcvNULL);

    timer = (gcsOSTIMER_PTR)Timer;

    cancel_delayed_work(&timer->work);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_GetProcessNameByPid(
    IN gctINT Pid,
    IN gctSIZE_T Length,
    OUT gctUINT8_PTR String
    )
{
    struct task_struct *task;

    /* Get the task_struct of the task with pid. */
    rcu_read_lock();

    task = FIND_TASK_BY_PID(Pid);

    if (task == gcvNULL)
    {
        rcu_read_unlock();
        return gcvSTATUS_NOT_FOUND;
    }

    /* Get name of process. */
    strncpy(String, task->comm, Length);

    rcu_read_unlock();

    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_DumpCallStack(
    IN gckOS Os
    )
{
    gcmkHEADER_ARG("Os=0x%X", Os);

    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    dump_stack();

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_DetectProcessByName
**
**      task->comm maybe part of process name, so this function
**      can only be used for debugging.
**
**  INPUT:
**
**      gctCONST_POINTER Name
**          Pointer to a string to hold name to be check. If the length
**          of name is longer than TASK_COMM_LEN (16), use part of name
**          to detect.
**
**  OUTPUT:
**
**      gcvSTATUS_TRUE if name of current process matches Name.
**
*/
gceSTATUS
gckOS_DetectProcessByName(
    IN gctCONST_POINTER Name
    )
{
    char comm[sizeof(current->comm)];

    memset(comm, 0, sizeof(comm));

    gcmkVERIFY_OK(
        gckOS_GetProcessNameByPid(_GetProcessID(), sizeof(current->comm), comm));

    return strstr(comm, Name) ? gcvSTATUS_TRUE
                              : gcvSTATUS_FALSE;
}

#if gcdANDROID_NATIVE_FENCE_SYNC

gceSTATUS
gckOS_CreateSyncPoint(
    IN gckOS Os,
    OUT gctSYNC_POINT * SyncPoint
    )
{
    gceSTATUS status;
    gcsSYNC_POINT_PTR syncPoint;

    gcmkHEADER_ARG("Os=0x%X", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    /* Create an sync point structure. */
    syncPoint = (gcsSYNC_POINT_PTR) kmalloc(
            sizeof(gcsSYNC_POINT), GFP_KERNEL | gcdNOWARN);

    if (syncPoint == gcvNULL)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    /* Initialize the sync point. */
    atomic_set(&syncPoint->ref, 1);
    atomic_set(&syncPoint->state, 0);

    gcmkONERROR(_AllocateIntegerId(&Os->syncPointDB, syncPoint, &syncPoint->id));

    *SyncPoint = (gctSYNC_POINT)(gctUINTPTR_T)syncPoint->id;

    gcmkFOOTER_ARG("*SyncPonint=%d", syncPoint->id);
    return gcvSTATUS_OK;

OnError:
    if (syncPoint != gcvNULL)
    {
        kfree(syncPoint);
    }

    gcmkFOOTER();
    return status;
}

gceSTATUS
gckOS_ReferenceSyncPoint(
    IN gckOS Os,
    IN gctSYNC_POINT SyncPoint
    )
{
    gceSTATUS status;
    gcsSYNC_POINT_PTR syncPoint;

    gcmkHEADER_ARG("Os=0x%X", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(SyncPoint != gcvNULL);

    gcmkONERROR(
        _QueryIntegerId(&Os->syncPointDB,
                        (gctUINT32)(gctUINTPTR_T)SyncPoint,
                        (gctPOINTER)&syncPoint));

    /* Initialize the sync point. */
    atomic_inc(&syncPoint->ref);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckOS_DestroySyncPoint(
    IN gckOS Os,
    IN gctSYNC_POINT SyncPoint
    )
{
    gceSTATUS status;
    gcsSYNC_POINT_PTR syncPoint;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Os=0x%X SyncPoint=%d", Os, (gctUINT32)(gctUINTPTR_T)SyncPoint);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(SyncPoint != gcvNULL);

    gcmkONERROR(gckOS_AcquireMutex(Os, Os->syncPointMutex, gcvINFINITE));
    acquired = gcvTRUE;

    gcmkONERROR(
        _QueryIntegerId(&Os->syncPointDB,
                        (gctUINT32)(gctUINTPTR_T)SyncPoint,
                        (gctPOINTER)&syncPoint));

    gcmkASSERT(syncPoint->id == (gctUINT32)(gctUINTPTR_T)SyncPoint);

    if (atomic_dec_and_test(&syncPoint->ref))
    {
        gcmkVERIFY_OK(_DestroyIntegerId(&Os->syncPointDB, syncPoint->id));

        /* Free the sgianl. */
        syncPoint->timeline = gcvNULL;
        kfree(syncPoint);
    }

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Os, Os->syncPointMutex));
    acquired = gcvFALSE;

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Os, Os->syncPointMutex));
    }

    gcmkFOOTER();
    return status;
}

gceSTATUS
gckOS_SignalSyncPoint(
    IN gckOS Os,
    IN gctSYNC_POINT SyncPoint
    )
{
    gceSTATUS status;
    gcsSYNC_POINT_PTR syncPoint;
    struct sync_timeline * timeline;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Os=0x%X SyncPoint=%d", Os, (gctUINT32)(gctUINTPTR_T)SyncPoint);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(SyncPoint != gcvNULL);

    gcmkONERROR(gckOS_AcquireMutex(Os, Os->syncPointMutex, gcvINFINITE));
    acquired = gcvTRUE;

    gcmkONERROR(
        _QueryIntegerId(&Os->syncPointDB,
                        (gctUINT32)(gctUINTPTR_T)SyncPoint,
                        (gctPOINTER)&syncPoint));

    gcmkASSERT(syncPoint->id == (gctUINT32)(gctUINTPTR_T)SyncPoint);

    /* Set signaled state. */
    atomic_set(&syncPoint->state, 1);

    /* Get parent timeline. */
    timeline = syncPoint->timeline;

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Os, Os->syncPointMutex));
    acquired = gcvFALSE;

    /* Signal timeline. */
    if (timeline)
    {
        sync_timeline_signal(timeline);
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Os, Os->syncPointMutex));
    }

    gcmkFOOTER();
    return status;
}

gceSTATUS
gckOS_QuerySyncPoint(
    IN gckOS Os,
    IN gctSYNC_POINT SyncPoint,
    OUT gctBOOL_PTR State
    )
{
    gceSTATUS status;
    gcsSYNC_POINT_PTR syncPoint;

    gcmkHEADER_ARG("Os=0x%X SyncPoint=%d", Os, (gctUINT32)(gctUINTPTR_T)SyncPoint);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(SyncPoint != gcvNULL);

    gcmkONERROR(
        _QueryIntegerId(&Os->syncPointDB,
                        (gctUINT32)(gctUINTPTR_T)SyncPoint,
                        (gctPOINTER)&syncPoint));

    gcmkASSERT(syncPoint->id == (gctUINT32)(gctUINTPTR_T)SyncPoint);

    /* Get state. */
    *State = atomic_read(&syncPoint->state);

    /* Success. */
    gcmkFOOTER_ARG("*State=%d", *State);
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckOS_CreateSyncTimeline(
    IN gckOS Os,
    IN gceCORE Core,
    OUT gctHANDLE * Timeline
    )
{
    struct viv_sync_timeline * timeline;
    char name[32];

    snprintf(name, 32, "gccore:%u", (unsigned int) Core);

    /* Create viv sync timeline. */
    timeline = viv_sync_timeline_create(name, Os);

    if (timeline == gcvNULL)
    {
        /* Out of memory. */
        return gcvSTATUS_OUT_OF_MEMORY;
    }

    *Timeline = (gctHANDLE) timeline;
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_DestroySyncTimeline(
    IN gckOS Os,
    IN gctHANDLE Timeline
    )
{
    struct viv_sync_timeline * timeline;
    gcmkASSERT(Timeline != gcvNULL);

    /* Destroy timeline. */
    timeline = (struct viv_sync_timeline *) Timeline;
    sync_timeline_destroy(&timeline->obj);

    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_CreateNativeFence(
    IN gckOS Os,
    IN gctHANDLE Timeline,
    IN gctSYNC_POINT SyncPoint,
    OUT gctINT * FenceFD
    )
{
    int fd = -1;
    struct viv_sync_timeline *timeline;
    struct sync_pt * pt = gcvNULL;
    struct sync_fence * fence;
    char name[32];
    gcsSYNC_POINT_PTR syncPoint;
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X Timeline=0x%X SyncPoint=%d",
                   Os, Timeline, (gctUINT)(gctUINTPTR_T)SyncPoint);

    gcmkONERROR(
        _QueryIntegerId(&Os->syncPointDB,
                        (gctUINT32)(gctUINTPTR_T)SyncPoint,
                        (gctPOINTER)&syncPoint));

    /* Cast timeline. */
    timeline = (struct viv_sync_timeline *) Timeline;

    fd = get_unused_fd_flags(O_CLOEXEC);

    if (fd < 0)
    {
        /* Out of resources. */
        gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
    }

    /* Create viv_sync_pt. */
    pt = viv_sync_pt_create(timeline, SyncPoint);

    if (pt == gcvNULL)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    /* Reference sync_timeline. */
    syncPoint->timeline = &timeline->obj;

    /* Build fence name. */
    snprintf(name, 32, "%.20s:%u",
             current->comm,
             (gctUINT32)(gctUINTPTR_T)SyncPoint);

    /* Create sync_fence. */
    fence = sync_fence_create(name, pt);

    if (fence == NULL)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    /* Install fence to fd. */
    sync_fence_install(fence, fd);

    *FenceFD = fd;
    gcmkFOOTER_ARG("*FenceFD=%d", fd);
    return gcvSTATUS_OK;

OnError:
    /* Error roll back. */
    if (pt)
    {
        sync_pt_free(pt);
    }

    if (fd > 0)
    {
        put_unused_fd(fd);
    }

    gcmkFOOTER();
    return status;
}

static void
_NativeFenceSignaled(
    struct sync_fence *fence,
    struct sync_fence_waiter *waiter
    )
{
    kfree(waiter);
    sync_fence_put(fence);
}

gceSTATUS
gckOS_WaitNativeFence(
    IN gckOS Os,
    IN gctHANDLE Timeline,
    IN gctINT FenceFD,
    IN gctUINT32 Timeout
    )
{
    struct sync_timeline * timeline;
    struct sync_fence * fence;
    gctBOOL wait;
    gceSTATUS status = gcvSTATUS_OK;

    gcmkHEADER_ARG("Os=0x%X Timeline=0x%X FenceFD=%d Timeout=%u",
                   Os, Timeline, FenceFD, Timeout);

    /* Get shortcut. */
    timeline = (struct sync_timeline *) Timeline;

    /* Get sync fence. */
    fence = sync_fence_fdget(FenceFD);

    if (!fence)
    {
        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    if (sync_fence_wait(fence, 0) == 0)
    {
        /* Already signaled. */
        sync_fence_put(fence);

        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

    wait = gcvFALSE;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,17,0)
    {
        int i;

        for (i = 0; i < fence->num_fences; i++)
        {
            struct fence *f = fence->cbs[i].sync_pt;
            struct sync_pt *pt = container_of(f, struct sync_pt, base);

            /* Do not need to wait on same timeline. */
            if ((sync_pt_parent(pt) != timeline) && !fence_is_signaled(f))
            {
                wait = gcvTRUE;
                break;
            }
        }
    }
#else
    {
        struct list_head *pos;
        list_for_each(pos, &fence->pt_list_head)
        {
            struct sync_pt * pt =
            container_of(pos, struct sync_pt, pt_list);

            /* Do not need to wait on same timeline. */
            if (pt->parent != timeline)
            {
                wait = gcvTRUE;
                break;
            }
        }
    }
#endif

    if (wait)
    {
        int err;
        long timeout = (Timeout == gcvINFINITE) ? - 1 : (long) Timeout;
        err = sync_fence_wait(fence, timeout);

        /* Put the fence. */
        sync_fence_put(fence);

        switch (err)
        {
        case 0:
            break;
        case -ETIME:
            status = gcvSTATUS_TIMEOUT;
            break;
        default:
            gcmkONERROR(gcvSTATUS_GENERIC_IO);
            break;
        }
    }
    else
    {
        int err;
        struct sync_fence_waiter *waiter;
        waiter = (struct sync_fence_waiter *)kmalloc(
                sizeof (struct sync_fence_waiter), gcdNOWARN | GFP_KERNEL);

        if (!waiter)
        {
            sync_fence_put(fence);
            gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
        }

        /* Schedule a waiter callback. */
        sync_fence_waiter_init(waiter, _NativeFenceSignaled);
        err = sync_fence_wait_async(fence, waiter);

        switch (err)
        {
        case 0:
            /* Put fence in callback function. */
            break;
        case 1:
            /* already signaled. */
            sync_fence_put(fence);
            break;
        default:
            sync_fence_put(fence);
            gcmkONERROR(gcvSTATUS_GENERIC_IO);
            break;
        }
    }

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}
#endif

#if gcdSECURITY
gceSTATUS
gckOS_AllocatePageArray(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T PageCount,
    OUT gctPOINTER * PageArrayLogical,
    OUT gctPHYS_ADDR * PageArrayPhysical
    )
{
    gceSTATUS status = gcvSTATUS_OK;
    PLINUX_MDL  mdl;
    gctUINT32*  table;
    gctUINT32   offset;
    gctSIZE_T   bytes;
    gckALLOCATOR allocator;

    gcmkHEADER_ARG("Os=0x%X Physical=0x%X PageCount=%u",
                   Os, Physical, PageCount);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);
    gcmkVERIFY_ARGUMENT(PageCount > 0);

    bytes = PageCount * gcmSIZEOF(gctUINT32);
    gcmkONERROR(gckOS_AllocateNonPagedMemory(
        Os,
        gcvFALSE,
        &bytes,
        PageArrayPhysical,
        PageArrayLogical
        ));

    table = *PageArrayLogical;

    /* Convert pointer to MDL. */
    mdl = (PLINUX_MDL)Physical;

    allocator = mdl->allocator;

     /* Get all the physical addresses and store them in the page table. */

    offset = 0;
    PageCount = PageCount / (PAGE_SIZE / 4096);

    /* Try to get the user pages so DMA can happen. */
    while (PageCount-- > 0)
    {
        unsigned long phys = ~0;

        gctPHYS_ADDR_T phys_addr;

        allocator->ops->Physical(allocator, mdl, offset * PAGE_SIZE, &phys_addr);

        phys = (unsigned long)phys_addr;

        table[offset] = phys;

        offset += 1;
    }

OnError:

    /* Return the status. */
    gcmkFOOTER();
    return status;
}
#endif

gceSTATUS
gckOS_CPUPhysicalToGPUPhysical(
    IN gckOS Os,
    IN gctPHYS_ADDR_T CPUPhysical,
    IN gctPHYS_ADDR_T * GPUPhysical
    )
{
    gcsPLATFORM * platform;
    gcmkHEADER_ARG("CPUPhysical=0x%X", CPUPhysical);

    platform = Os->device->platform;

    if (platform && platform->ops->getGPUPhysical)
    {
        gcmkVERIFY_OK(
            platform->ops->getGPUPhysical(platform, CPUPhysical, GPUPhysical));
    }
    else
    {
        *GPUPhysical = CPUPhysical;
    }

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_GPUPhysicalToCPUPhysical(
    IN gckOS Os,
    IN gctUINT32 GPUPhysical,
    IN gctPHYS_ADDR_T * CPUPhysical
    )
{
    gcsPLATFORM * platform;
    gcmkHEADER_ARG("GPUPhysical=0x%X", GPUPhysical);

    platform = Os->device->platform;

    if (platform && platform->ops->getCPUPhysical)
    {
        gcmkVERIFY_OK(
            platform->ops->getCPUPhysical(platform, GPUPhysical, CPUPhysical));
    }
    else
    {
        *CPUPhysical = GPUPhysical;
    }

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_PhysicalToPhysicalAddress(
    IN gckOS Os,
    IN gctPOINTER Physical,
    IN gctUINT32 Offset,
    OUT gctPHYS_ADDR_T * PhysicalAddress
    )
{
    PLINUX_MDL mdl = (PLINUX_MDL)Physical;
    gckALLOCATOR allocator = mdl->allocator;

    if (allocator)
    {
        return allocator->ops->Physical(allocator, mdl, Offset, PhysicalAddress);
    }

    return gcvSTATUS_NOT_SUPPORTED;
}

static int fd_release(struct inode *inode, struct file *file)
{
    gcsFDPRIVATE_PTR private = (gcsFDPRIVATE_PTR)file->private_data;

    if (private && private->release)
    {
        return private->release(private);
    }

    return 0;
}

static const struct file_operations fd_fops = {
    .release = fd_release,
};

gceSTATUS
gckOS_GetFd(
    IN gctSTRING Name,
    IN gcsFDPRIVATE_PTR Private,
    OUT gctINT * Fd
    )
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
    *Fd = anon_inode_getfd(Name, &fd_fops, Private, O_RDWR);

    if (*Fd < 0)
    {
        return gcvSTATUS_OUT_OF_RESOURCES;
    }

    return gcvSTATUS_OK;
#else
    return gcvSTATUS_NOT_SUPPORTED;
#endif
}

gceSTATUS
gckOS_QueryOption(
    IN gckOS Os,
    IN gctCONST_STRING Option,
    OUT gctUINT32 * Value
    )
{
    gckGALDEVICE device = Os->device;

    if (!strcmp(Option, "physBase"))
    {
        *Value = device->physBase;
        return gcvSTATUS_OK;
    }
    else if (!strcmp(Option, "physSize"))
    {
        *Value = device->physSize;
        return gcvSTATUS_OK;
    }
    else if (!strcmp(Option, "mmu"))
    {
#if gcdSECURITY
        *Value = 0;
#else
        *Value = device->args.mmu;
#endif
        return gcvSTATUS_OK;
    }
    else if (!strcmp(Option, "contiguousSize"))
    {
        *Value = device->contiguousSize;
        return gcvSTATUS_OK;
    }
    else if (!strcmp(Option, "contiguousBase"))
    {
        *Value = device->requestedContiguousBase;
        return gcvSTATUS_OK;
    }
    else if (!strcmp(Option, "recovery"))
    {
        *Value = device->args.recovery;
        return gcvSTATUS_OK;
    }
    else if (!strcmp(Option, "stuckDump"))
    {
        *Value = device->args.stuckDump;
        return gcvSTATUS_OK;
    }
    else if (!strcmp(Option, "powerManagement"))
    {
        *Value = device->args.powerManagement;
        return gcvSTATUS_OK;
    }
    else if (!strcmp(Option, "TA"))
    {
        *Value = 1;
        return gcvSTATUS_OK;
    }
    else if (!strcmp(Option, "gpuProfiler"))
    {
        *Value = device->args.gpuProfiler;
        return gcvSTATUS_OK;
    }

    return gcvSTATUS_NOT_SUPPORTED;
}

/*******************************************************************************
**
**  gckOS_WrapMemory
**
**  Import a number of pages allocated by other allocator.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctUINT32 Flag
**          Memory type.
**
**  OUTPUT:
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that hold the number of bytes allocated.
**
**      gctPHYS_ADDR * Physical
**          Pointer to a variable that will hold the physical address of the
**          allocation.
*/
gceSTATUS
gckOS_WrapMemory(
    IN gckOS Os,
    IN gcsUSER_MEMORY_DESC_PTR Desc,
    OUT gctSIZE_T *Bytes,
    OUT gctPHYS_ADDR * Physical,
    OUT gctBOOL *Contiguous
    )
{
    PLINUX_MDL mdl = gcvNULL;
    gceSTATUS status = gcvSTATUS_OUT_OF_MEMORY;
    gckALLOCATOR allocator;
    gcsATTACH_DESC desc;
    gctUINT32 flag;

    gcmkHEADER_ARG("Os=0x%X ", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);

    mdl = _CreateMdl();
    if (mdl == gcvNULL)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    desc.handle = Desc->handle;
    desc.memory = gcmUINT64_TO_PTR(Desc->logical);
    desc.physical = Desc->physical;
    desc.size = Desc->size;
    flag = Desc->flag;

    desc.info = Desc->externalMemoryInfo;

    /* Walk all allocators. */
    list_for_each_entry(allocator, &Os->allocatorList, head)
    {
        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_OS,
                       "%s(%d) Flag = %x allocator->capability = %x",
                        __FUNCTION__, __LINE__, flag, allocator->capability);

        if ((flag & allocator->capability) != flag)
        {
            status = gcvSTATUS_NOT_SUPPORTED;
            continue;
        }

        if (flag == gcvALLOC_FLAG_EXTERNAL_MEMORY)
        {
            /* Use name to match suitable allocator for external memory. */
            if (!strncmp(Desc->externalMemoryInfo.allocatorName, allocator->name, gcdEXTERNAL_MEMORY_NAME_MAX))
            {
                status = gcvSTATUS_NOT_SUPPORTED;
                continue;
            }
        }

        status = allocator->ops->Attach(allocator, &desc, mdl);

        if (gcmIS_SUCCESS(status))
        {
            mdl->allocator = allocator;
            break;
        }
    }

    /* Check status. */
    gcmkONERROR(status);

    mdl->dmaHandle  = 0;
    mdl->addr       = 0;

    *Bytes = mdl->numPages * PAGE_SIZE;

    /* Return physical address. */
    *Physical = (gctPHYS_ADDR) mdl;

    *Contiguous = mdl->contiguous;

    MEMORY_LOCK(Os);

    /*
     * Add this to a global list.
     * Will be used by get physical address
     * and mapuser pointer functions.
     */
    if (!Os->mdlHead)
    {
        /* Initialize the queue. */
        Os->mdlHead = Os->mdlTail = mdl;
    }
    else
    {
        /* Add to tail. */
        mdl->prev           = Os->mdlTail;
        Os->mdlTail->next   = mdl;
        Os->mdlTail         = mdl;
    }

    MEMORY_UNLOCK(Os);

    /* Success. */
    gcmkFOOTER_ARG("*Physical=0x%X", *Physical);
    return gcvSTATUS_OK;

OnError:
    if (mdl != gcvNULL)
    {
        /* Free the memory. */
        _DestroyMdl(mdl);
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckOS_GetPolicyID(
    IN gckOS Os,
    IN gceSURF_TYPE Type,
    OUT gctUINT32_PTR PolicyID,
    OUT gctUINT32_PTR AXIConfig
    )
{
    gcsPLATFORM * platform = Os->device->platform;

    if (platform && platform->ops->getPolicyID)
    {
        return platform->ops->getPolicyID(platform, Type, PolicyID, AXIConfig);
    }

    return gcvSTATUS_NOT_SUPPORTED;
}

