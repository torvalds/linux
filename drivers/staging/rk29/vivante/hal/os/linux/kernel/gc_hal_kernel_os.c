/****************************************************************************
*  
*    Copyright (C) 2005 - 2011 by Vivante Corp.
*  
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the license, or
*    (at your option) any later version.
*  
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*    GNU General Public License for more details.
*  
*    You should have received a copy of the GNU General Public License
*    along with this program; if not write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*  
*****************************************************************************/




#include "gc_hal_kernel_linux.h"

#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <asm/atomic.h>
#if USE_DMA_COHERENT
#include <linux/dma-mapping.h>
#endif /* NO_DMA_COHERENT */

// dkm: add
#include <linux/delay.h>
#include <mach/pmu.h>
#include <mach/cru.h>

#if !USE_NEW_LINUX_SIGNAL
#define USER_SIGNAL_TABLE_LEN_INIT  64
#endif

#define _GC_OBJ_ZONE    gcvZONE_OS

// dkm: gcdPAGE_ALLOC_LIMIT
#if gcdPAGE_ALLOC_LIMIT
int g_pages_alloced = 0;
#endif

#define MEMORY_LOCK(os) \
    gcmkVERIFY_OK(gckOS_AcquireMutex( \
                                (os), \
                                (os)->memoryLock, \
                                gcvINFINITE))

#define MEMORY_UNLOCK(os) \
    gcmkVERIFY_OK(gckOS_ReleaseMutex((os), (os)->memoryLock))

#define MEMORY_MAP_LOCK(os) \
    gcmkVERIFY_OK(gckOS_AcquireMutex( \
                                (os), \
                                (os)->memoryMapLock, \
                                gcvINFINITE))

#define MEMORY_MAP_UNLOCK(os) \
    gcmkVERIFY_OK(gckOS_ReleaseMutex((os), (os)->memoryMapLock))


// dkm : gcdkUSE_MAPED_NONPAGE_CACHE
#if gcdkUSE_MAPED_NONPAGE_CACHE
typedef struct _gcsMapedNonPagedCache
{
    PLINUX_MDL                      mdl;
    struct _gcsMapedNonPagedCache   *prev;
    struct _gcsMapedNonPagedCache   *next;
}
gcsMapedNonPagedCache;

static void _FreeAllMapedNonPagedCache(gckOS Os, gctINT pid);
#endif

gceSTATUS gckOS_FreeNonPagedMemoryRealy(
    IN gckOS Os,
    IN gctSIZE_T Bytes,
    IN gctPHYS_ADDR Physical,
    IN gctPOINTER Logical,
    IN gctBOOL MemLock
    );


/******************************************************************************\
********************************** Structures **********************************
\******************************************************************************/

struct _gckOS
{
    /* Object. */
    gcsOBJECT                   object;

    /* Heap. */
    gckHEAP                     heap;

    /* Pointer to device */
    gckGALDEVICE                device;

    /* Memory management */
    gctPOINTER                  memoryLock;
    gctPOINTER                  memoryMapLock;

    struct _LINUX_MDL           *mdlHead;
    struct _LINUX_MDL           *mdlTail;

    gctUINT32                   baseAddress;

    /* Kernel process ID. */
    gctUINT32                   kernelProcessID;

#if !USE_NEW_LINUX_SIGNAL
    /* Signal management. */
    struct _signal {
        /* Unused signal ID number. */
        gctINT                  unused;

        /* The pointer to the table. */
        gctPOINTER *            table;

        /* Signal table length. */
        gctINT                  tableLen;

        /* The current unused signal ID. */
        gctINT                  currentID;

        /* Lock. */
        gctPOINTER              lock;
    } signal;
#endif

// dkm : gcdkUSE_MAPED_NONPAGE_CACHE
#if gcdkUSE_MAPED_NONPAGE_CACHE
    gctINT                      cacheNum;
    gcsMapedNonPagedCache *     cacheHead;
    gcsMapedNonPagedCache *     cacheTail;

    gctINT                      pageNum;
    #if USE_DMA_COHERENT
        gctSTRING               addr[100];
        dma_addr_t              dmaHandle[100];
    #else
        struct page *           pageCache[100];
    #endif
#endif
};

#if !USE_NEW_LINUX_SIGNAL
typedef struct _gcsSIGNAL
{
    /* Kernel sync primitive. */
    struct completion event;

    /* Manual reset flag. */
    gctBOOL manualReset;

    /* The reference counter. */
    atomic_t ref;

    /* The owner of the signal. */
    gctHANDLE process;

    /* Destroyed flag. */
    gctBOOL destroyed;
}
gcsSIGNAL;

typedef struct _gcsSIGNAL * gcsSIGNAL_PTR;
#endif

typedef struct _gcsPageInfo * gcsPageInfo_PTR;

typedef struct _gcsPageInfo
{
    struct page **pages;
    gctUINT32_PTR pageTable;
}
gcsPageInfo;


static PLINUX_MDL
_CreateMdl(
    IN gctINT PID
    )
{
    PLINUX_MDL  mdl;

    mdl = (PLINUX_MDL)kmalloc(sizeof(struct _LINUX_MDL), GFP_ATOMIC);
    if (mdl == gcvNULL) return gcvNULL;

    mdl->pid    = PID;
    mdl->maps   = gcvNULL;
    mdl->prev   = gcvNULL;
    mdl->next   = gcvNULL;

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

    return gcvSTATUS_OK;
}

static PLINUX_MDL_MAP
_CreateMdlMap(
    IN PLINUX_MDL Mdl,
    IN gctINT PID
    )
{
    PLINUX_MDL_MAP  mdlMap;

    mdlMap = (PLINUX_MDL_MAP)kmalloc(sizeof(struct _LINUX_MDL_MAP), GFP_ATOMIC);
    if (mdlMap == gcvNULL) return gcvNULL;

    mdlMap->pid     = PID;
    mdlMap->vmaAddr = gcvNULL;
    mdlMap->vma     = gcvNULL;

    mdlMap->next    = Mdl->maps;
    Mdl->maps       = mdlMap;

    return mdlMap;
}

static gceSTATUS
_DestroyMdlMap(
    IN PLINUX_MDL Mdl,
    IN PLINUX_MDL_MAP MdlMap
    )
{
    PLINUX_MDL_MAP  prevMdlMap;

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

    return gcvSTATUS_OK;
}

extern PLINUX_MDL_MAP
FindMdlMap(
    IN PLINUX_MDL Mdl,
    IN gctINT PID
    )
{
    PLINUX_MDL_MAP  mdlMap;

    if (Mdl == gcvNULL)
    {
        return gcvNULL;
    }

    mdlMap = Mdl->maps;

    while (mdlMap != gcvNULL)
    {
        if (mdlMap->pid == PID) return mdlMap;

        mdlMap = mdlMap->next;
    }

    return gcvNULL;
}

void
FreeProcessMemoryOnExit(
    IN gckOS Os,
    IN gckKERNEL Kernel
    )
{
    PLINUX_MDL      mdl, nextMdl;
    PLINUX_MDL_MAP  mdlMap;

    MEMORY_LOCK(Os);

    mdl = Os->mdlHead;

    while (mdl != gcvNULL)
    {
        if (mdl != Os->mdlTail)
        {
            nextMdl = mdl->next;
        }
        else
        {
            nextMdl = gcvNULL;
        }

        if (mdl->pagedMem)
        {
            mdlMap = mdl->maps;

            if (mdlMap != gcvNULL
                && mdlMap->pid == current->tgid
                && mdlMap->next == gcvNULL)
            {
                MEMORY_UNLOCK(Os);

                gcmkVERIFY_OK(gckOS_FreePagedMemory(Os, mdl, mdl->numPages * PAGE_SIZE));

                MEMORY_LOCK(Os);

                nextMdl = Os->mdlHead;
            }
        }

        mdl = nextMdl;
    }

    MEMORY_UNLOCK(Os);
}

void
PrintInfoOnExit(
    IN gckOS Os,
    IN gckKERNEL Kernel
    )
{
    PLINUX_MDL      mdl, nextMdl;
    PLINUX_MDL_MAP  mdlMap;

    MEMORY_LOCK(Os);

    mdl = Os->mdlHead;

    while (mdl != gcvNULL)
    {
        if (mdl != Os->mdlTail)
        {
            nextMdl = mdl->next;
        }
        else
        {
            nextMdl = gcvNULL;
        }

        printk("Unfreed mdl: %p, pid: %d -> pagedMem: %s, addr: %p, dmaHandle: 0x%x, pages: %d",
            mdl,
            mdl->pid,
            mdl->pagedMem? "true" : "false",
            mdl->addr,
            mdl->dmaHandle,
            mdl->numPages);

        mdlMap = mdl->maps;

        while (mdlMap != gcvNULL)
        {
            printk("\tmap: %p, pid: %d -> vmaAddr: %p, vma: %p",
                    mdlMap,
                    mdlMap->pid,
                    mdlMap->vmaAddr,
                    mdlMap->vma);

            mdlMap = mdlMap->next;
        }

        mdl = nextMdl;
    }

    MEMORY_UNLOCK(Os);
}

void
OnProcessExit(
    IN gckOS Os,
    IN gckKERNEL Kernel
    )
{
    /* PrintInfoOnExit(Os, Kernel); */

#ifdef ANDROID
    FreeProcessMemoryOnExit(Os, Kernel);
#endif
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

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Os != gcvNULL);

    /* Allocate the gckOS object. */
    os = (gckOS) kmalloc(gcmSIZEOF(struct _gckOS), GFP_ATOMIC);

    if (os == gcvNULL)
    {
        /* Out of memory. */
        return gcvSTATUS_OUT_OF_MEMORY;
    }

    /* Zero the memory. */
    gckOS_ZeroMemory(os, gcmSIZEOF(struct _gckOS));

    /* Initialize the gckOS object. */
    os->object.type = gcvOBJ_OS;

    /* Set device device. */
    os->device = Context;

    /* IMPORTANT! No heap yet. */
    os->heap = gcvNULL;

    /* Initialize the memory lock. */
    gcmkONERROR(gckOS_CreateMutex(os, &os->memoryLock));

    gcmkONERROR(gckOS_CreateMutex(os, &os->memoryMapLock));

    /* Create the gckHEAP object. */
    gcmkONERROR(gckHEAP_Construct(os, gcdHEAP_SIZE, &os->heap));

    os->mdlHead = os->mdlTail = gcvNULL;

    /* Find the base address of the physical memory. */
    os->baseAddress = os->device->baseAddress;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_OS,
                  "Physical base address set to 0x%08X.",
                  os->baseAddress);

    /* Get the kernel process ID. */
    gcmkONERROR(gckOS_GetProcessID(&os->kernelProcessID));

#if !USE_NEW_LINUX_SIGNAL
    /*
     * Initialize the signal manager.
     * It creates the signals to be used in
     * the user space.
     */

    /* Initialize mutex. */
    gcmkONERROR(
        gckOS_CreateMutex(os, &os->signal.lock));

    /* Initialize the signal table. */
    os->signal.table =
        kmalloc(gcmSIZEOF(gctPOINTER) * USER_SIGNAL_TABLE_LEN_INIT, GFP_KERNEL);

    if (os->signal.table == gcvNULL)
    {
        /* Out of memory. */
        status = gcvSTATUS_OUT_OF_MEMORY;
        goto OnError;
    }

    gckOS_ZeroMemory(os->signal.table,
                     gcmSIZEOF(gctPOINTER) * USER_SIGNAL_TABLE_LEN_INIT);

    /* Set the signal table length. */
    os->signal.tableLen = USER_SIGNAL_TABLE_LEN_INIT;

    /* The table is empty. */
    os->signal.unused = os->signal.tableLen;

    /* Initial signal ID. */
    os->signal.currentID = 0;
#endif

#if gcdkUSE_MAPED_NONPAGE_CACHE
    for(os->pageNum=0; os->pageNum<50; os->pageNum++) {
    #if USE_DMA_COHERENT
        os->addr[os->pageNum] = 
        #if (2==gcdENABLE_MEM_CACHE)
            dma_alloc_writecombine(NULL, 5 * PAGE_SIZE, &os->dmaHandle[os->pageNum], GFP_ATOMIC);
        #else
            dma_alloc_coherent(NULL, 5 * PAGE_SIZE, &os->dmaHandle[os->pageNum], GFP_ATOMIC);
        #endif
    #else
        os->pageCache[os->pageNum] = alloc_pages(GFP_KERNEL | GFP_DMA, get_order(5 * PAGE_SIZE));
    #endif
    }
#endif

    /* Return pointer to the gckOS object. */
    *Os = os;

    /* Success. */
    return gcvSTATUS_OK;

OnError:
#if !USE_NEW_LINUX_SIGNAL
    /* Roll back any allocation. */
    if (os->signal.table != gcvNULL)
    {
        kfree(os->signal.table);
    }

    if (os->signal.lock != gcvNULL)
    {
        gcmkVERIFY_OK(
            gckOS_DeleteMutex(os, os->signal.lock));
    }
#endif

    if (os->heap != gcvNULL)
    {
        gcmkVERIFY_OK(
            gckHEAP_Destroy(os->heap));
    }

    if (os->memoryMapLock != gcvNULL)
    {
        gcmkVERIFY_OK(
            gckOS_DeleteMutex(os, os->memoryMapLock));
    }

    if (os->memoryLock != gcvNULL)
    {
        gcmkVERIFY_OK(
            gckOS_DeleteMutex(os, os->memoryLock));
    }

    kfree(os);

    /* Return the error. */
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
    gckHEAP     heap    = gcvNULL;
    gctINT      i       = 0;
    gctPOINTER *table   = Os->signal.table; 

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

// dkm : gcdkUSE_MAPED_NONPAGE_CACHE
#if gcdkUSE_MAPED_NONPAGE_CACHE
    _FreeAllMapedNonPagedCache(Os, 0);

    for(i=0; i<Os->pageNum; i++) {
    #if USE_DMA_COHERENT
        if(Os->addr[i])    dma_free_coherent(gcvNULL, 5 * PAGE_SIZE, Os->addr[i], Os->dmaHandle[i]);
    #else
        if(Os->pageCache[i])    free_pages((unsigned long)page_address(Os->pageCache[i]), get_order(5 * PAGE_SIZE));
    #endif
    }
#endif

#if !USE_NEW_LINUX_SIGNAL
    /*
     * Destroy the signal manager.
     */

    /* Destroy the mutex. */
    gcmkVERIFY_OK(
        gckOS_DeleteMutex(Os, Os->signal.lock));

    /* Free remain signal left in table */
    for (i = 0; i < Os->signal.tableLen; i++)
    {
        if(table[i] != gcvNULL)
        {
            kfree(table[i]);
            table[i] = gcvNULL;
        }
    }

    /* Free the signal table. */
    kfree(Os->signal.table);
    Os->signal.table    = gcvNULL;
    Os->signal.tableLen = 0;
#endif

    if (Os->heap != NULL)
    {
        /* Mark gckHEAP as gone. */
        heap     = Os->heap;
        Os->heap = NULL;

        /* Destroy the gckHEAP object. */
        gcmkVERIFY_OK(
            gckHEAP_Destroy(heap));
    }
    else
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR,
            gcvZONE_OS,
            "Failed to destroy signal, it was not unmampped \n"
            );
        return gcvSTATUS_INVALID_ARGUMENT;
    }

    /* Destroy the memory lock. */
    gcmkVERIFY_OK(
        gckOS_DeleteMutex(Os, Os->memoryMapLock));

    gcmkVERIFY_OK(
        gckOS_DeleteMutex(Os, Os->memoryLock));

    gcmkPRINT("$$FLUSH$$");

    /* Mark the gckOS object as unknown. */
    Os->object.type = gcvOBJ_UNKNOWN;

    /* Free the gckOS object. */
    kfree(Os);

    /* Success. */
    return gcvSTATUS_OK;
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

    /* gcmkHEADER_ARG("Os=0x%x Bytes=%lu", Os, Bytes); */

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Memory != NULL);

    /* Do we have a heap? */
    if (Os->heap != NULL)
    {
        /* Allocate from the heap. */
        gcmkONERROR(gckHEAP_Allocate(Os->heap, Bytes, Memory));
    }
    else
    {
        gcmkONERROR(gckOS_AllocateMemory(Os, Bytes, Memory));
    }

    /* Success. */
    /* gcmkFOOTER_ARG("*memory=0x%x", *Memory); */
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    /*gcmkFOOTER();*/
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

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Memory != NULL);

    /* gcmkHEADER_ARG("Os=0x%x Memory=0x%x", Os, memory); */

    /* Do we have a heap? */
    if (Os->heap != NULL)
    {
        /* Free from the heap. */
        gcmkONERROR(gckHEAP_Free(Os->heap, Memory));
    }
    else
    {
        gcmkONERROR(gckOS_FreeMemory(Os, Memory));
    }

    /* Success. */
    /* gcmkFOOTER_NO(); */
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
   /* gcmkFOOTER();*/
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

    gcmkHEADER_ARG("Os=0x%x Bytes=%lu", Os, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Memory != NULL);

    memory = (gctPOINTER) kmalloc(Bytes, GFP_ATOMIC);

    if (memory == NULL)
    {
        /* Out of memory. */
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    /* Return pointer to the memory allocation. */
    *Memory = memory;

    /* Success. */
    gcmkFOOTER_ARG("*Memory=%p", *Memory);
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
    gcmkHEADER_ARG("Memory=%p", Memory);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Memory != NULL);

    /* Free the memory from the OS pool. */
    kfree(Memory);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_AllocateVirtualMemory
**
**  Allocate virtual memory wrapper.
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
gckOS_AllocateVirtualMemory(
    IN gckOS Os,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Memory
    )
{
    gctPOINTER memory;
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%x Bytes=%lu", Os, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Memory != NULL);

    memory = (gctPOINTER) vmalloc(Bytes);

    if (memory == NULL)
    {
        /* Out of memory. */
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    /* Return pointer to the memory allocation. */
    *Memory = memory;

    /* Success. */
    gcmkFOOTER_ARG("*Memory=%p", *Memory);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_FreeVirtualMemory
**
**  Free allocated virtual memory wrapper.
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
gckOS_FreeVirtualMemory(
    IN gckOS Os,
    IN gctPOINTER Memory
    )
{
    gcmkHEADER_ARG("Memory=%p", Memory);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Memory != NULL);

    /* Free the memory from the OS pool. */
    vfree(Memory);

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
    PLINUX_MDL_MAP  mdlMap;
    PLINUX_MDL      mdl = (PLINUX_MDL)Physical;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != 0);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Logical != NULL);

    MEMORY_LOCK(Os);

    mdlMap = FindMdlMap(mdl, current->tgid);

    if (mdlMap == gcvNULL)
    {
        mdlMap = _CreateMdlMap(mdl, current->tgid);

        if (mdlMap == gcvNULL)
        {
            MEMORY_UNLOCK(Os);

            return gcvSTATUS_OUT_OF_MEMORY;
        }
    }

    if (mdlMap->vmaAddr == gcvNULL)
    {
        down_write(&current->mm->mmap_sem);

        mdlMap->vmaAddr = (char *)do_mmap_pgoff(NULL,
                    0L,
                    mdl->numPages * PAGE_SIZE,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED,
                    0);

        if (mdlMap->vmaAddr == gcvNULL)
        {
            gcmkTRACE_ZONE(gcvLEVEL_ERROR,
                gcvZONE_OS,
                "gckOS_MapMemory: do_mmap_pgoff error");

            gcmkTRACE_ZONE(gcvLEVEL_ERROR,
                gcvZONE_OS,
                "[gckOS_MapMemory] mdl->numPages: %d",
                "[gckOS_MapMemory] mdl->vmaAddr: 0x%x",
                mdl->numPages,
                mdlMap->vmaAddr
                );

            up_write(&current->mm->mmap_sem);

            MEMORY_UNLOCK(Os);

            return gcvSTATUS_OUT_OF_MEMORY;
        }

        mdlMap->vma = find_vma(current->mm, (unsigned long)mdlMap->vmaAddr);

        if (!mdlMap->vma)
        {
            gcmkTRACE_ZONE(gcvLEVEL_ERROR,
                    gcvZONE_OS,
                    "gckOS_MapMemory: find_vma error.");

            mdlMap->vmaAddr = gcvNULL;

            up_write(&current->mm->mmap_sem);

            MEMORY_UNLOCK(Os);

            return gcvSTATUS_OUT_OF_RESOURCES;
        }

#if USE_DMA_COHERENT
    #if (2==gcdENABLE_MEM_CACHE)
        if (dma_mmap_writecombine(NULL,
    #else
        if (dma_mmap_coherent(NULL,
    #endif
                    mdlMap->vma,
                    mdl->addr,
                    mdl->dmaHandle,
                    mdl->numPages * PAGE_SIZE) < 0)
        {
            up_write(&current->mm->mmap_sem);

            gcmkTRACE_ZONE(gcvLEVEL_ERROR,
                    gcvZONE_OS,
                    "gckOS_MapMemory: dma_mmap_coherent error.");

            mdlMap->vmaAddr = gcvNULL;

            MEMORY_UNLOCK(Os);

            return gcvSTATUS_OUT_OF_RESOURCES;
        }
#else

// dkm: gcdENABLE_MEM_CACHE
#if (2==gcdENABLE_MEM_CACHE)
        mdlMap->vma->vm_page_prot = pgprot_writecombine(mdlMap->vma->vm_page_prot);
#elif (1==gcdENABLE_MEM_CACHE)
    // NULL
#else
        mdlMap->vma->vm_page_prot = pgprot_noncached(mdlMap->vma->vm_page_prot);
#endif
        mdlMap->vma->vm_flags |= VM_IO | VM_DONTCOPY | VM_DONTEXPAND | VM_RESERVED;
        mdlMap->vma->vm_pgoff = 0;

        if (remap_pfn_range(mdlMap->vma,
                            mdlMap->vma->vm_start,
                            mdl->dmaHandle >> PAGE_SHIFT,
                            mdl->numPages*PAGE_SIZE,
                            mdlMap->vma->vm_page_prot) < 0)
        {
            up_write(&current->mm->mmap_sem);

            gcmkTRACE_ZONE(gcvLEVEL_ERROR,
                    gcvZONE_OS,
                    "gckOS_MapMemory: remap_pfn_range error.");

            mdlMap->vmaAddr = gcvNULL;

            MEMORY_UNLOCK(Os);

            return gcvSTATUS_OUT_OF_RESOURCES;
        }
#endif

        up_write(&current->mm->mmap_sem);
    }

    MEMORY_UNLOCK(Os);

    *Logical = mdlMap->vmaAddr;

    gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_OS,
                "gckOS_MapMemory: User Mapped address for 0x%x is 0x%x pid->%d",
                (gctUINT32)mdl->addr,
                (gctUINT32)*Logical,
                mdlMap->pid);

    return gcvSTATUS_OK;
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
    PLINUX_MDL_MAP          mdlMap;
    PLINUX_MDL              mdl = (PLINUX_MDL)Physical;
    struct task_struct *    task;
    // dkm: add
    struct mm_struct *      mm;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != 0);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Logical != NULL);

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "in gckOS_UnmapMemory");

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "gckOS_UnmapMemory Will be unmapping 0x%x mdl->0x%x",
                (gctUINT32)Logical,
                (gctUINT32)mdl);

    MEMORY_LOCK(Os);

    if (Logical)
    {
        gcmkTRACE_ZONE(gcvLEVEL_VERBOSE,
            gcvZONE_OS,
            "[gckOS_UnmapMemory] Logical: 0x%x",
            Logical
            );

        mdlMap = FindMdlMap(mdl, current->tgid);

        if (mdlMap == gcvNULL || mdlMap->vmaAddr == gcvNULL)
        {
            MEMORY_UNLOCK(Os);

            return gcvSTATUS_INVALID_ARGUMENT;
        }

        /* Get the current pointer for the task with stored pid. */
        task = FIND_TASK_BY_PID(mdlMap->pid);
        // dkm: add
        if(task) {
            mm = get_task_mm(task);
            put_task_struct(task);
        } else {
            mm = gcvNULL;
        }

        // dkm: modify
        if (mm)
        {
            down_write(&mm->mmap_sem);
            do_munmap(mm, (unsigned long)Logical, mdl->numPages*PAGE_SIZE);
            up_write(&mm->mmap_sem);
            mmput(mm);
        }
        else
        {
            gcmkTRACE_ZONE(gcvLEVEL_INFO,
                        gcvZONE_OS,
                        "Can't find the task with pid->%d. No unmapping",
                        mdlMap->pid);
        }

        gcmkVERIFY_OK(_DestroyMdlMap(mdl, mdlMap));
    }

    MEMORY_UNLOCK(Os);

    /* Success. */
    return gcvSTATUS_OK;
}  

// dkm : gcdkUSE_MAPED_NONPAGE_CACHE
#if gcdkUSE_MAPED_NONPAGE_CACHE
static gctBOOL _AddMapedNonPagedCache(gckOS Os, PLINUX_MDL mdl)
{
    gcsMapedNonPagedCache   *cache;
    gctINT Num = 0;

    if(5!=mdl->numPages)     return gcvFALSE;

    /* Create Caches */
    if(gcvNULL==Os->cacheHead)
    {
        while(Num < gcdkUSE_MAPED_NONPAGE_CACHE)
        {
            cache = (gcsMapedNonPagedCache *)kmalloc(sizeof(gcsMapedNonPagedCache), GFP_ATOMIC);
            if(gcvNULL==cache)      break;
            cache->mdl = gcvNULL;
            
            /* Add to list */
            if (Os->cacheHead == gcvNULL) {
                cache->prev   = gcvNULL;
                cache->next   = gcvNULL;
                Os->cacheHead = Os->cacheTail = cache;
            } else {
                /* Add to the Head. */
                cache->prev = gcvNULL;
                cache->next = Os->cacheHead;
                Os->cacheHead->prev = cache;
                Os->cacheHead = cache;
            }
            Num++;
        }
        if(Num < 1)  return gcvFALSE;
    }

    /* Get the last and Add it to the Head */
    cache = Os->cacheTail;
    if(cache->prev) {
        Os->cacheTail = cache->prev;
        Os->cacheTail->next = gcvNULL;
        cache->prev = gcvNULL;
        cache->next = Os->cacheHead;
        Os->cacheHead->prev = cache;
        Os->cacheHead = cache;
    }
    if(cache->mdl) {
        //printk("Add(%d) pid:%d, But exist pid:%d, free and get it!\n", Os->cacheNum, mdl->pid, cache->mdl->pid);
        gckOS_FreeNonPagedMemoryRealy(Os, cache->mdl->numPages, (gctPHYS_ADDR)cache->mdl, (gctPOINTER)cache->mdl->addr, gcvFALSE);
        cache->mdl = gcvNULL;
    } else {
        Os->cacheNum++;
        //printk("Add(%d) pid:%d succ!\n", Os->cacheNum, mdl->pid);
    }
    cache->mdl = mdl;
    return gcvTRUE;
}

static PLINUX_MDL _GetMapedNonPagedCache(gckOS Os, gctINT pid, gctINT numPages)
{
    gcsMapedNonPagedCache *cache = gcvNULL;
    PLINUX_MDL mdl = gcvNULL;

    if(5!=numPages)     return gcvNULL;
    
    cache = Os->cacheHead;
    
    while (cache != gcvNULL) 
    {
        if (cache->mdl && cache->mdl->pid==pid && cache->mdl->numPages==numPages) 
        {
            /* Move the cache to the Tail */
            if(cache->next) {
                if(cache->prev) {
                    cache->prev->next = cache->next;
                    cache->next->prev = cache->prev;
                } else {
                    Os->cacheHead = cache->next;
                    Os->cacheHead->prev = gcvNULL;
                }
                    
                cache->prev = Os->cacheTail;
                cache->next = gcvNULL;
                Os->cacheTail->next = cache;
                Os->cacheTail = cache;
            } 
          
            mdl = cache->mdl;
            cache->mdl = gcvNULL;
            Os->cacheNum--;
            break;
        }
        cache = cache->next; 
    }
    
    return mdl;
}

static void _FreeAllMapedNonPagedCache(gckOS Os, gctINT pid)
{
    gcsMapedNonPagedCache *cache, *next, *tail;

    MEMORY_LOCK(Os);

    cache = Os->cacheHead;
    tail = Os->cacheTail;

    /* free mdl that pid hit */
    while (cache != gcvNULL)
    {
        next = cache->next;
        
        if( (cache->mdl) && (cache->mdl->pid==pid || 0==pid))
        {
            /* Move the cache to the Tail */
            if(cache->next) {
                if(cache->prev) {
                    cache->prev->next = cache->next;
                    cache->next->prev = cache->prev;
                } else {
                    Os->cacheHead = cache->next;
                    Os->cacheHead->prev = gcvNULL;
                }
                    
                cache->prev = Os->cacheTail;
                cache->next = gcvNULL;
                Os->cacheTail->next = cache;
                Os->cacheTail = cache;
            } 

            /* Real Free memory */
            gckOS_FreeNonPagedMemoryRealy(Os, cache->mdl->numPages, (gctPHYS_ADDR)cache->mdl, (gctPOINTER)cache->mdl->addr, gcvFALSE);
            cache->mdl = gcvNULL;
            Os->cacheNum--;
        }

        if(tail == cache)   break;
        cache = next;
    }

    //printk("FreeAll(%d) pid:%d succ!\n", Os->cacheNum, pid);

    /* destroy all caches */
    if(0 == pid)
    {
        cache = Os->cacheHead;
        while (cache != gcvNULL)
        {
            next = cache->next;
            kfree(cache);
            cache = next;
        }
    }

    MEMORY_UNLOCK(Os);
}
#endif

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
    gctSIZE_T       bytes;
    gctINT          numPages;
    PLINUX_MDL      mdl;
    PLINUX_MDL_MAP  mdlMap = 0;
    gctSTRING       addr;

#if !USE_DMA_COHERENT
    struct page *   page;
    long            size, order;
    gctPOINTER      vaddr, reserved_vaddr;
    gctUINT32       reserved_size;
#endif

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT((Bytes != NULL) && (*Bytes > 0));
    gcmkVERIFY_ARGUMENT(Physical != NULL);
    gcmkVERIFY_ARGUMENT(Logical != NULL);

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "in gckOS_AllocateNonPagedMemory");

    /* Align number of bytes to page size. */
    bytes = gcmALIGN(*Bytes, PAGE_SIZE);

    /* Get total number of pages.. */
    numPages = GetPageCount(bytes, 0);

// dkm : gcdkUSE_MAPED_NONPAGE_CACHE
#if gcdkUSE_MAPED_NONPAGE_CACHE
    MEMORY_LOCK(Os);
    mdl = _GetMapedNonPagedCache(Os, current->tgid, numPages);
    if(mdl)
    {
        *Bytes = bytes;
        *Physical = (gctPHYS_ADDR) mdl;
        if (InUserSpace) {
            *Logical = mdl->maps->vmaAddr;
        } else {
            *Logical = (gctPOINTER)mdl->addr;
        }
        MEMORY_UNLOCK(Os);
        return gcvSTATUS_OK;
    }
    MEMORY_UNLOCK(Os);
#endif
    

    /* Allocate mdl+vector structure */
    mdl = _CreateMdl(current->tgid);

    if (mdl == gcvNULL)
    {
        return gcvSTATUS_OUT_OF_MEMORY;
    }

    mdl->pagedMem = 0;
    mdl->numPages = numPages;

    MEMORY_LOCK(Os);

#if USE_DMA_COHERENT
    #if gcdkUSE_MAPED_NONPAGE_CACHE
        if(5==mdl->numPages && Os->pageNum>0 && Os->addr[Os->pageNum-1]) {
            Os->pageNum--;
            addr = Os->addr[Os->pageNum];
            mdl->dmaHandle = Os->dmaHandle[Os->pageNum];
            Os->addr[Os->pageNum] = gcvNULL;
            Os->dmaHandle[Os->pageNum] = 0;
        } else {
        #if (2==gcdENABLE_MEM_CACHE)
            addr = dma_alloc_writecombine(NULL,
        #else
            addr = dma_alloc_coherent(NULL,
        #endif
                        mdl->numPages * PAGE_SIZE,
                        &mdl->dmaHandle,
                        GFP_ATOMIC);
        }
    #else
        #if (2==gcdENABLE_MEM_CACHE)
            addr = dma_alloc_writecombine(NULL,
        #else
            addr = dma_alloc_coherent(NULL,
        #endif
                        mdl->numPages * PAGE_SIZE,
                        &mdl->dmaHandle,
                        GFP_ATOMIC);
    #endif
#else
    size    = mdl->numPages * PAGE_SIZE;
    order   = get_order(size);
    
#if gcdkUSE_MAPED_NONPAGE_CACHE
    if(5==mdl->numPages && Os->pageNum>0 && Os->pageCache[Os->pageNum-1]) {
        Os->pageNum--;
        page    = Os->pageCache[Os->pageNum];
        Os->pageCache[Os->pageNum] = gcvNULL;
        //printk("pop pages, os->pageNum = %d\n", Os->pageNum);
    } else {
        page    = alloc_pages(GFP_KERNEL | GFP_DMA, order);
    }
#else
    page    = alloc_pages(GFP_KERNEL | GFP_DMA, order);
#endif

    if (page == gcvNULL)
    {
        MEMORY_UNLOCK(Os);

        return gcvSTATUS_OUT_OF_MEMORY;
    }

    /* 
     * On some system ioremap_nocache fails when 
     * given a size of more than 1 page if all
     * of the pages haven't been marked as reserved 
     * first. Allocating a single page works without
     * any changes though.
     */
    vaddr = (gctPOINTER)page_address(page);

    reserved_vaddr = vaddr;
    reserved_size  = size;

    while (reserved_size > 0)
    {
        SetPageReserved(virt_to_page(reserved_vaddr));

        reserved_vaddr += PAGE_SIZE;
        reserved_size  -= PAGE_SIZE;
    }

// dkm: gcdENABLE_MEM_CACHE
#if (1==gcdENABLE_MEM_CACHE)
    addr            = ioremap_cached(virt_to_phys(vaddr), size);
#else
    addr            = ioremap_nocache(virt_to_phys(vaddr), size);
#endif


    mdl->dmaHandle  = virt_to_phys(vaddr);
    mdl->kaddr      = vaddr;

#if ENABLE_ARM_L2_CACHE
    dma_cache_maint(vaddr, size, DMA_FROM_DEVICE);
#endif

#endif

    if (addr == gcvNULL)
    {
        gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "galcore: Can't allocate memorry for size->0x%x",
                (gctUINT32)bytes);

        gcmkVERIFY_OK(_DestroyMdl(mdl));

        MEMORY_UNLOCK(Os);

        return gcvSTATUS_OUT_OF_MEMORY;
    }

    if ((Os->baseAddress & 0x80000000) != (mdl->dmaHandle & 0x80000000))
    {
        mdl->dmaHandle = (mdl->dmaHandle & ~0x80000000)
                       | (Os->baseAddress & 0x80000000);
    }

    mdl->addr = addr;

    /*
     * We will not do any mapping from here.
     * Mapping will happen from mmap method.
     * mdl structure will be used.
     */

    /* Return allocated memory. */
    *Bytes = bytes;
    *Physical = (gctPHYS_ADDR) mdl;

    if (InUserSpace)
    {
        mdlMap = _CreateMdlMap(mdl, current->tgid);

        if (mdlMap == gcvNULL)
        {
            gcmkVERIFY_OK(_DestroyMdl(mdl));

            MEMORY_UNLOCK(Os);

            return gcvSTATUS_OUT_OF_MEMORY;
        }

        /* Only after mmap this will be valid. */

        /* We need to map this to user space. */
        down_write(&current->mm->mmap_sem);

        mdlMap->vmaAddr = (gctSTRING)do_mmap_pgoff(gcvNULL,
                0L,
                mdl->numPages * PAGE_SIZE,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,
                0);

        if (mdlMap->vmaAddr == gcvNULL)
        {
            gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "galcore: do_mmap_pgoff error");

            up_write(&current->mm->mmap_sem);

            gcmkVERIFY_OK(_DestroyMdlMap(mdl, mdlMap));
            gcmkVERIFY_OK(_DestroyMdl(mdl));

            MEMORY_UNLOCK(Os);

            return gcvSTATUS_OUT_OF_MEMORY;
        }

        mdlMap->vma = find_vma(current->mm, (unsigned long)mdlMap->vmaAddr);

        if (mdlMap->vma == gcvNULL)
        {
            gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "find_vma error");

            up_write(&current->mm->mmap_sem);

            gcmkVERIFY_OK(_DestroyMdlMap(mdl, mdlMap));
            gcmkVERIFY_OK(_DestroyMdl(mdl));

            MEMORY_UNLOCK(Os);

            return gcvSTATUS_OUT_OF_RESOURCES;
        }

#if USE_DMA_COHERENT
    #if (2==gcdENABLE_MEM_CACHE)
        if (dma_mmap_writecombine(NULL,
    #else
        if (dma_mmap_coherent(NULL,
    #endif
                mdlMap->vma,
                mdl->addr,
                mdl->dmaHandle,
                mdl->numPages * PAGE_SIZE) < 0)
        {
            up_write(&current->mm->mmap_sem);

            gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "dma_mmap_coherent error");

            gcmkVERIFY_OK(_DestroyMdlMap(mdl, mdlMap));
            gcmkVERIFY_OK(_DestroyMdl(mdl));

            MEMORY_UNLOCK(Os);

            return gcvSTATUS_OUT_OF_RESOURCES;
        }
#else

// dkm: gcdENABLE_MEM_CACHE
#if (2==gcdENABLE_MEM_CACHE)
        mdlMap->vma->vm_page_prot = pgprot_writecombine(mdlMap->vma->vm_page_prot);
#elif (1==gcdENABLE_MEM_CACHE)
        // NULL
#else
        mdlMap->vma->vm_page_prot = pgprot_noncached(mdlMap->vma->vm_page_prot);
#endif
        mdlMap->vma->vm_flags |= VM_IO | VM_DONTCOPY | VM_DONTEXPAND | VM_RESERVED;
        mdlMap->vma->vm_pgoff = 0;

        if (remap_pfn_range(mdlMap->vma,
                            mdlMap->vma->vm_start,
                            mdl->dmaHandle >> PAGE_SHIFT,
                            mdl->numPages * PAGE_SIZE,
                            mdlMap->vma->vm_page_prot))
        {
            up_write(&current->mm->mmap_sem);

            gcmkTRACE_ZONE(gcvLEVEL_INFO,
                    gcvZONE_OS,
                    "remap_pfn_range error");

            gcmkVERIFY_OK(_DestroyMdlMap(mdl, mdlMap));
            gcmkVERIFY_OK(_DestroyMdl(mdl));

            MEMORY_UNLOCK(Os);

            return gcvSTATUS_OUT_OF_RESOURCES;
        }
#endif /* NO_DMA_COHERENT */

        up_write(&current->mm->mmap_sem);

        *Logical = mdlMap->vmaAddr;
    }
    else
    {
        *Logical = (gctPOINTER)mdl->addr;
    }

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
        /* Add to the tail. */
        mdl->prev = Os->mdlTail;
        Os->mdlTail->next = mdl;
        Os->mdlTail = mdl;
    }

    MEMORY_UNLOCK(Os);

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "gckOS_AllocateNonPagedMemory: "
                "Bytes->0x%x, Mdl->%p, Logical->0x%x dmaHandle->0x%x",
                (gctUINT32)bytes,
                mdl,
                (gctUINT32)mdl->addr,
                mdl->dmaHandle);

    if (InUserSpace)
    {
        gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "vmaAddr->0x%x pid->%d",
                (gctUINT32)mdlMap->vmaAddr,
                mdlMap->pid);
    }

    /* Success. */
    return gcvSTATUS_OK;
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
// dkm : gcdkUSE_MAPED_NONPAGE_CACHE
#if gcdkUSE_MAPED_NONPAGE_CACHE
    MEMORY_LOCK(Os);
    if(_AddMapedNonPagedCache(Os, (PLINUX_MDL)Physical)) {
        MEMORY_UNLOCK(Os);
        return gcvSTATUS_OK;
    }
    MEMORY_UNLOCK(Os);
#endif
    
    return gckOS_FreeNonPagedMemoryRealy(Os, Bytes, Physical, Logical, gcvTRUE);
}


gceSTATUS gckOS_FreeNonPagedMemoryRealy(
    IN gckOS Os,
    IN gctSIZE_T Bytes,
    IN gctPHYS_ADDR Physical,
    IN gctPOINTER Logical,
    IN gctBOOL MemLock
    )
{
    PLINUX_MDL              mdl;
    PLINUX_MDL_MAP          mdlMap;
    struct task_struct *    task;
    // dkm: add
    struct mm_struct *      mm;

#if !USE_DMA_COHERENT
    unsigned                size;
    gctPOINTER              vaddr;
#endif /* NO_DMA_COHERENT */

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Physical != 0);
    gcmkVERIFY_ARGUMENT(Logical != NULL);

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "in gckOS_FreeNonPagedMemory");

    /* Convert physical address into a pointer to a MDL. */
    mdl = (PLINUX_MDL) Physical;

    if(MemLock)     MEMORY_LOCK(Os);

#if USE_DMA_COHERENT
    #if gcdkUSE_MAPED_NONPAGE_CACHE
        if(5==mdl->numPages && Os->pageNum<100 && !Os->addr[Os->pageNum]) {
            Os->addr[Os->pageNum] = mdl->addr;
            Os->dmaHandle[Os->pageNum] = mdl->dmaHandle;
            Os->pageNum ++;
        } else {
            dma_free_coherent(gcvNULL,
                            mdl->numPages * PAGE_SIZE,
                            mdl->addr,
                            mdl->dmaHandle);
        }
    #else
        dma_free_coherent(gcvNULL,
                        mdl->numPages * PAGE_SIZE,
                        mdl->addr,
                        mdl->dmaHandle);
    #endif
#else
    size    = mdl->numPages * PAGE_SIZE;
    vaddr   = mdl->kaddr;

    while (size > 0)
    {
        ClearPageReserved(virt_to_page(vaddr));

        vaddr   += PAGE_SIZE;
        size    -= PAGE_SIZE;
    }

#if gcdkUSE_MAPED_NONPAGE_CACHE
    if(5==mdl->numPages && Os->pageNum<100 && !Os->pageCache[Os->pageNum]) {
        Os->pageCache[Os->pageNum] = virt_to_page(mdl->kaddr);
        Os->pageNum ++;
        //printk("push pages, os->pageNum = %d\n", Os->pageNum);
    } else {
        free_pages((unsigned long)mdl->kaddr, get_order(mdl->numPages * PAGE_SIZE));
    }
#else
    free_pages((unsigned long)mdl->kaddr, get_order(mdl->numPages * PAGE_SIZE));
#endif

    iounmap(mdl->addr);
#endif /* NO_DMA_COHERENT */

    mdlMap = mdl->maps;

    while (mdlMap != gcvNULL)
    {
        if (mdlMap->vmaAddr != gcvNULL)
        {
            /* Get the current pointer for the task with stored pid. */
            task = FIND_TASK_BY_PID(mdlMap->pid);
            // dkm: add
            if(task) {
                mm = get_task_mm(task);
                put_task_struct(task);
            } else {
                mm = gcvNULL;
            }

            // dkm: modify
            if (mm)
            {
                down_write(&mm->mmap_sem);

                if (do_munmap(mm,
                            (unsigned long)mdlMap->vmaAddr,
                            mdl->numPages * PAGE_SIZE) < 0)
                {
                    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                                gcvZONE_OS,
                                "gckOS_FreeNonPagedMemory: "
                                "Unmap Failed ->Mdl->0x%x Logical->0x%x vmaAddr->0x%x",
                                (gctUINT32)mdl,
                                (gctUINT32)mdl->addr,
                                (gctUINT32)mdlMap->vmaAddr);
                }

                up_write(&mm->mmap_sem);
                mmput(mm);
            }

            mdlMap->vmaAddr = gcvNULL;
        }

        mdlMap = mdlMap->next;
    }

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

    if(MemLock)     MEMORY_UNLOCK(Os);

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "gckOS_FreeNonPagedMemory: "
                "Mdl->0x%x Logical->0x%x",
                (gctUINT32)mdl,
                (gctUINT32)mdl->addr);

    gcmkVERIFY_OK(_DestroyMdl(mdl));

    /* Success. */
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
gceSTATUS gckOS_ReadRegister(
    IN gckOS Os,
    IN gctUINT32 Address,
    OUT gctUINT32 * Data
    )
{
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Data != NULL);

    *Data = readl((gctUINT8 *)Os->device->registerBase + Address);

    /* Success. */
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
gceSTATUS gckOS_WriteRegister(
    IN gckOS Os,
    IN gctUINT32 Address,
    IN gctUINT32 Data
    )
{
    writel(Data, (gctUINT8 *)Os->device->registerBase + Address);

    /* Success. */
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
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(PageSize != NULL);

    /* Return the page size. */
    *PageSize = (gctSIZE_T) PAGE_SIZE;

    /* Success. */
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
**          Pointer to an gckOS object.
**
**      gctPOINTER Logical
**          Logical address.
**
**      gctUINT ProcessID
**          Procedd ID.
**
**  OUTPUT:
**
**      gctUINT32 * Address
**          Poinetr to a variable that receives the 32-bit physical adress.
*/
gceSTATUS
gckOS_GetPhysicalAddressProcess(
    IN gckOS Os,
    IN gctPOINTER Logical,
    IN gctUINT ProcessID,
    OUT gctUINT32 * Address
    )
{
    return gckOS_GetPhysicalAddress(Os, Logical, Address);
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
gceSTATUS gckOS_GetPhysicalAddress(
    IN gckOS Os,
    IN gctPOINTER Logical,
    OUT gctUINT32 * Address
    )
{
    PLINUX_MDL      mdl;
    PLINUX_MDL_MAP  mdlMap;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    /*
     * Try to search the address in our list.
     * This could be an mmaped memory.
      * Search in our list.
      */

    MEMORY_LOCK(Os);

    mdl = Os->mdlHead;

    while (mdl != gcvNULL)
    {
        /* Check for the logical address match. */
        if (mdl->addr
            && (gctUINT32)Logical >= (gctUINT32)mdl->addr
            && (gctUINT32)Logical < ((gctUINT32)mdl->addr + mdl->numPages*PAGE_SIZE))
        {
            if (mdl->dmaHandle)
            {
                /* The memory was from coherent area. */
                *Address = (gctUINT32)mdl->dmaHandle
                            + (gctUINT32)((gctUINT32)Logical - (gctUINT32)mdl->addr);
            }
            else if (mdl->pagedMem)
            {
                if (mdl->contiguous)
                {
                    *Address = (gctUINT32)virt_to_phys(mdl->addr)
                                + ((gctUINT32)Logical - (gctUINT32)mdl->addr);
                }
                else
                {
                    *Address = page_to_phys(vmalloc_to_page((gctSTRING)mdl->addr
                                + ((gctUINT32)Logical - (gctUINT32)mdl->addr)));
                }
            }
            else
            {
                *Address = (gctUINT32)virt_to_phys(mdl->addr)
                            + ((gctUINT32)Logical - (gctUINT32)mdl->addr);
            }
            break;
        }

        mdlMap = FindMdlMap(mdl, current->tgid);

        /* Is the given address within that range. */
        if (mdlMap != gcvNULL
            && mdlMap->vmaAddr != gcvNULL
            && Logical >= mdlMap->vmaAddr
            && Logical < (mdlMap->vmaAddr + mdl->numPages * PAGE_SIZE))
        {
            if (mdl->dmaHandle)
            {
                /* The memory was from coherent area. */
                *Address = (gctUINT32)mdl->dmaHandle
                            + (gctUINT32)((gctUINT32)Logical
                            - (gctUINT32)mdlMap->vmaAddr);
            }
            else if (mdl->pagedMem)
            {
                if (mdl->contiguous)
                {
                    *Address = (gctUINT32)virt_to_phys(mdl->addr)
                                + (gctUINT32)(Logical - mdlMap->vmaAddr);
                }
                else
                {
                    *Address = page_to_phys(vmalloc_to_page((gctSTRING)mdl->addr
                                + ((gctUINT32)Logical - (gctUINT32)mdlMap->vmaAddr)));
                }
            }
            else
            {
                /* Return the kernel virtual pointer based on this. */
                *Address = (gctUINT32)virt_to_phys(mdl->addr)
                            + (gctUINT32)(Logical - mdlMap->vmaAddr);
            }
            break;
        }

        mdl = mdl->next;
    }

    /* Subtract base address to get a GPU physical address. */
    gcmkASSERT(*Address >= Os->baseAddress);
    *Address -= Os->baseAddress;

    MEMORY_UNLOCK(Os);

    if (mdl == gcvNULL)
    {
        return gcvSTATUS_INVALID_ARGUMENT;
    }

    /* Success. */
    return gcvSTATUS_OK;
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
gceSTATUS gckOS_MapPhysical(
    IN gckOS Os,
    IN gctUINT32 Physical,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Logical
    )
{
    gctPOINTER logical;
    PLINUX_MDL mdl;
    gctUINT32 physical;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

    MEMORY_LOCK(Os);

    /* Compute true physical address (before subtraction of the baseAddress). */
    physical = Physical + Os->baseAddress;

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

    if (mdl == gcvNULL)
    {
        /* Map memory as cached memory. */
        request_mem_region(physical, Bytes, "MapRegion");
// dkm: gcdENABLE_MEM_CACHE
#if (1==gcdENABLE_MEM_CACHE)
        logical = (gctPOINTER) ioremap_cached(physical, Bytes);
#else
        logical = (gctPOINTER) ioremap_nocache(physical, Bytes);
#endif
        if (logical == NULL)
        {
            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_OS,
                          "gckOS_MapMemory: Failed to ioremap");

            MEMORY_UNLOCK(Os);

            /* Out of resources. */
            return gcvSTATUS_OUT_OF_RESOURCES;
        }

        /* Return pointer to mapped memory. */
        *Logical = logical;
    }

    MEMORY_UNLOCK(Os);

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_OS,
                  "gckOS_MapPhysical: "
                  "Physical->0x%X Bytes->0x%X Logical->0x%X MappingFound->%d",
                  (gctUINT32) Physical,
                  (gctUINT32) Bytes,
                  (gctUINT32) *Logical,
                   mdl ? 1 : 0);

    /* Success. */
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
gceSTATUS gckOS_UnmapPhysical(
    IN gckOS Os,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    )
{
    PLINUX_MDL  mdl;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Logical != NULL);
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
        iounmap(Logical);
    }

    MEMORY_UNLOCK(Os);

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                    gcvZONE_OS,
                    "gckOS_UnmapPhysical: "
                    "Logical->0x%x Bytes->0x%x MappingFound(?)->%d",
                    (gctUINT32)Logical,
                    (gctUINT32)Bytes,
                    mdl ? 1 : 0);

    /* Success. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_CreateMutex
**
**  Create a new mutex.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**  OUTPUT:
**
**      gctPOINTER * Mutex
**          Pointer to a variable that will hold a pointer to the mutex.
*/
gceSTATUS gckOS_CreateMutex(
    IN gckOS Os,
    OUT gctPOINTER * Mutex
    )
{
    /* Validate the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Mutex != NULL);

    /* Allocate a FAST_MUTEX structure. */
    *Mutex = (gctPOINTER)kmalloc(sizeof(struct semaphore), GFP_KERNEL);

    if (*Mutex == gcvNULL)
    {
        return gcvSTATUS_OUT_OF_MEMORY;
    }

    /* Initialize the semaphore.. Come up in unlocked state. */
    sema_init(*Mutex, 1);

    /* Return status. */
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
gceSTATUS gckOS_DeleteMutex(
    IN gckOS Os,
    IN gctPOINTER Mutex
    )
{
    /* Validate the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Mutex != NULL);

    /* Delete the fast mutex. */
    kfree(Mutex);

    return gcvSTATUS_OK;
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
    /* Validate the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Mutex != NULL);

    if (Timeout == gcvINFINITE)
    {
        down((struct semaphore *) Mutex);

        /* Success. */
        return gcvSTATUS_OK;
    }

    while (gcvTRUE)
    {
        /* Try to acquire the fast mutex. */
        if (!down_trylock((struct semaphore *) Mutex))
        {
            /* Success. */
            return gcvSTATUS_OK;
        }

        if (Timeout-- == 0) break;

        /* Wait for 1 millisecond. */
        gcmkVERIFY_OK(gckOS_Delay(Os, 1));
    }

    /* Timeout. */
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
gceSTATUS gckOS_ReleaseMutex(
    IN gckOS Os,
    IN gctPOINTER Mutex
    )
{
    /* Validate the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Mutex != NULL);

    /* Release the fast mutex. */
    up((struct semaphore *) Mutex);

    /* Success. */
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
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    /* Exchange the pair of 32-bit values. */
    *OldValue = (gctUINT32) atomic_xchg((atomic_t *) Target, (int) NewValue);

    /* Success. */
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
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    /* Exchange the pair of pointers. */
    *OldValue = (gctPOINTER) atomic_xchg((atomic_t *) Target, (int) NewValue);

    /* Success. */
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

    gcmkHEADER_ARG("Os=0x%x", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Atom != gcvNULL);

    /* Allocate the atom. */
    gcmkONERROR(gckOS_Allocate(Os, gcmSIZEOF(atomic_t), Atom));

    /* Initialize the atom. */
    atomic_set((atomic_t *) *Atom, 0);

    /* Success. */
    gcmkFOOTER_ARG("*Atom=0x%x", *Atom);
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

    gcmkHEADER_ARG("Os=0x%x Atom=0x%0x", Os, Atom);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Atom != gcvNULL);

    /* Free the atom. */
    gcmkONERROR(gckOS_Free(Os, Atom));

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
    gcmkHEADER_ARG("Os=0x%x Atom=0x%0x", Os, Atom);

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
**          Pointer to a variable the receives the original value of the atom.
*/
gceSTATUS
gckOS_AtomIncrement(
    IN gckOS Os,
    IN gctPOINTER Atom,
    OUT gctINT32_PTR Value
    )
{
    gcmkHEADER_ARG("Os=0x%x Atom=0x%0x", Os, Atom);

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
**          Pointer to a variable the receives the original value of the atom.
*/
gceSTATUS
gckOS_AtomDecrement(
    IN gckOS Os,
    IN gctPOINTER Atom,
    OUT gctINT32_PTR Value
    )
{
    gcmkHEADER_ARG("Os=0x%x Atom=0x%0x", Os, Atom);

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
    struct timeval now;
    unsigned long jiffies;

    if (Delay > 0)
    {
        /* Convert milliseconds into seconds and microseconds. */
        now.tv_sec  = Delay / 1000;
        now.tv_usec = (Delay % 1000) * 1000;

        /* Convert timeval to jiffies. */
        jiffies = timeval_to_jiffies(&now);

        /* Schedule timeout. */
        schedule_timeout_interruptible(jiffies);
    }

    /* Success. */
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
gceSTATUS gckOS_MemoryBarrier(
    IN gckOS Os,
    IN gctPOINTER Address
    )
{
    /* Verify thearguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    // dkm: change to dsb from mb
    dsb();

    /* Success. */
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
    return gckOS_AllocatePagedMemoryEx(Os, gcvFALSE, Bytes, Physical);
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
**      gctBOOL Contiguous
**          Need contiguous memory or not.
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
gceSTATUS gckOS_AllocatePagedMemoryEx(
    IN gckOS Os,
    IN gctBOOL Contiguous,
    IN gctSIZE_T Bytes,
    OUT gctPHYS_ADDR * Physical
    )
{
    gctINT numPages;
    gctINT i;
    PLINUX_MDL mdl;
    gctSTRING addr;
    gctSIZE_T bytes;

    gcmkHEADER_ARG("Os=0x%0x Contiguous=%d Bytes=%lu", Os, Contiguous, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Physical != NULL);

    bytes = gcmALIGN(Bytes, PAGE_SIZE);

    numPages = GetPageCount(bytes, 0);

    MEMORY_LOCK(Os);

    if (Contiguous)
    {
// dkm: gcdPAGE_ALLOC_LIMIT
#if gcdPAGE_ALLOC_LIMIT
#define PAGE_ALLOC_LIMIT_SIZE    0
        if( (g_pages_alloced + numPages) > (256*PAGE_ALLOC_LIMIT_SIZE) ) {
            //printk("full %d! \n", g_pages_alloced);
            addr = NULL;
        } else {
            addr = (char *)__get_free_pages(GFP_ATOMIC | GFP_DMA | __GFP_NOWARN | __GFP_NO_KSWAPD, GetOrder(numPages));
            if(addr) {
                g_pages_alloced += numPages;
                //printk("alloc %d / %d \n", numPages, g_pages_alloced);
            } else {
                printk("gpu : alloc %d fail! (%d/%d)\n", numPages,  g_pages_alloced, (256*PAGE_ALLOC_LIMIT_SIZE) );
            }
        }
#else
        addr = (char *)__get_free_pages(GFP_ATOMIC | GFP_DMA | __GFP_NOWARN | __GFP_NO_KSWAPD, GetOrder(numPages));
#endif
    }
    else
    {
        addr = vmalloc(bytes);
    }

    if (!addr)
    {
        gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "gckOS_AllocatePagedMemoryEx: "
                "Can't allocate memorry for size->0x%x",
                (gctUINT32)bytes);

        MEMORY_UNLOCK(Os);

        gcmkHEADER_ARG("status=%d", gcvSTATUS_OUT_OF_MEMORY);
        return gcvSTATUS_OUT_OF_MEMORY;
    }

    mdl = _CreateMdl(current->tgid);

    if (mdl == gcvNULL)
    {
        if (Contiguous)
        {
            free_pages((unsigned int) addr, GetOrder(mdl->numPages));
        }
        else
        {
            vfree(addr);
        }

        MEMORY_UNLOCK(Os);

        gcmkHEADER_ARG("status=%d", gcvSTATUS_OUT_OF_MEMORY);
        return gcvSTATUS_OUT_OF_MEMORY;
    }

    mdl->dmaHandle  = 0;
    mdl->addr       = addr;
    mdl->numPages   = numPages;
    mdl->pagedMem   = 1;
    mdl->contiguous = Contiguous;

    for (i = 0; i < mdl->numPages; i++)
    {
        struct page *page;

        if (mdl->contiguous)
        {
            page = virt_to_page((void *)(((unsigned long)addr) + i * PAGE_SIZE));
        }
        else
        {
            page = vmalloc_to_page((void *)(((unsigned long)addr) + i * PAGE_SIZE));
        }

        SetPageReserved(page);
        flush_dcache_page(page);
    }

    /* Return physical address. */
    *Physical = (gctPHYS_ADDR) mdl;

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

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_OS,
                   "%s: Bytes=%lu Mdl=0x%08x Logical=0x%08x",
                   __FUNCTION__, bytes, mdl, mdl->addr);

    /* Success. */
    gcmkHEADER_ARG("*Physical=0x%08x", *Physical);
    return gcvSTATUS_OK;
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
gceSTATUS gckOS_FreePagedMemory(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes
    )
{
    PLINUX_MDL  mdl = (PLINUX_MDL)Physical;
    gctSTRING   addr;
    gctINT      i;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != NULL);

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "in gckOS_FreePagedMemory");

    addr = mdl->addr;

    MEMORY_LOCK(Os);

    for (i = 0; i < mdl->numPages; i++)
    {
        if (mdl->contiguous)
        {
            ClearPageReserved(virt_to_page((gctPOINTER)(((unsigned long)addr) + i * PAGE_SIZE)));
        }
        else
        {
            ClearPageReserved(vmalloc_to_page((gctPOINTER)(((unsigned long)addr) + i * PAGE_SIZE)));
        }
    }

    if (mdl->contiguous)
    {
        free_pages((unsigned long)mdl->addr, GetOrder(mdl->numPages));
// dkm: gcdPAGE_ALLOC_LIMIT
#if gcdPAGE_ALLOC_LIMIT
        g_pages_alloced -= mdl->numPages;
        //printk("free %d / %d \n", mdl->numPages, g_pages_alloced);
        if(g_pages_alloced<0)    g_pages_alloced = 0;
#endif
    }
    else
    {
        vfree(mdl->addr);
    }

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

    /* Free the structure... */
    gcmkVERIFY_OK(_DestroyMdl(mdl));

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "gckOS_FreePagedMemory: Bytes->0x%x, Mdl->0x%x",
                (gctUINT32)Bytes,
                (gctUINT32)mdl);

    /* Success. */
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
gceSTATUS gckOS_LockPages(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Logical,
    OUT gctSIZE_T * PageCount
    )
{
    PLINUX_MDL      mdl;
    PLINUX_MDL_MAP  mdlMap;
    gctSTRING       addr;
    unsigned long   start;
    unsigned long   pfn;
    gctINT          i;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != NULL);
    gcmkVERIFY_ARGUMENT(Logical != NULL);
    gcmkVERIFY_ARGUMENT(PageCount != NULL);

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "in gckOS_LockPages");

    mdl = (PLINUX_MDL) Physical;

    MEMORY_LOCK(Os);

    mdlMap = FindMdlMap(mdl, current->tgid);

    if (mdlMap == gcvNULL)
    {
        mdlMap = _CreateMdlMap(mdl, current->tgid);

        if (mdlMap == gcvNULL)
        {
            MEMORY_UNLOCK(Os);

            return gcvSTATUS_OUT_OF_MEMORY;
        }
    }

    if (mdlMap->vmaAddr == gcvNULL)
    {
        down_write(&current->mm->mmap_sem);

        mdlMap->vmaAddr = (gctSTRING)do_mmap_pgoff(NULL,
                        0L,
                        mdl->numPages * PAGE_SIZE,
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED,
                        0);

        up_write(&current->mm->mmap_sem);

        gcmkTRACE_ZONE(gcvLEVEL_INFO,
                        gcvZONE_OS,
                        "gckOS_LockPages: "
                        "vmaAddr->0x%x for phys_addr->0x%x",
                        (gctUINT32)mdlMap->vmaAddr,
                        (gctUINT32)mdl);

        if (mdlMap->vmaAddr == gcvNULL)
        {
            gcmkTRACE_ZONE(gcvLEVEL_INFO,
                        gcvZONE_OS,
                        "gckOS_LockPages: do_mmap_pgoff error");

            MEMORY_UNLOCK(Os);

            return gcvSTATUS_OUT_OF_MEMORY;
        }

        mdlMap->vma = find_vma(current->mm, (unsigned long)mdlMap->vmaAddr);

        if (mdlMap->vma == gcvNULL)
        {
            gcmkTRACE_ZONE(gcvLEVEL_INFO,
                        gcvZONE_OS,
                        "find_vma error");

            mdlMap->vmaAddr = gcvNULL;

            MEMORY_UNLOCK(Os);

            return gcvSTATUS_OUT_OF_RESOURCES;
        }

        mdlMap->vma->vm_flags |= VM_RESERVED;
        /* Make this mapping non-cached. */

// dkm: gcdENABLE_MEM_CACHE
#if (2==gcdENABLE_MEM_CACHE)
        mdlMap->vma->vm_page_prot = pgprot_writecombine(mdlMap->vma->vm_page_prot);
#elif (1==gcdENABLE_MEM_CACHE)
        // NULL
#else
        mdlMap->vma->vm_page_prot = pgprot_noncached(mdlMap->vma->vm_page_prot);
#endif

        addr = mdl->addr;

        /* Now map all the vmalloc pages to this user address. */
        down_write(&current->mm->mmap_sem);

        if (mdl->contiguous)
        {
            /* map kernel memory to user space.. */
            if (remap_pfn_range(mdlMap->vma,
                                mdlMap->vma->vm_start,
                                virt_to_phys((gctPOINTER)mdl->addr) >> PAGE_SHIFT,
                                mdlMap->vma->vm_end - mdlMap->vma->vm_start,
                                mdlMap->vma->vm_page_prot) < 0)
            {
                up_write(&current->mm->mmap_sem);

                gcmkTRACE_ZONE(gcvLEVEL_INFO,
                            gcvZONE_OS,
                            "gckOS_LockPages: unable to mmap ret");

                mdlMap->vmaAddr = gcvNULL;

                MEMORY_UNLOCK(Os);

                return gcvSTATUS_OUT_OF_MEMORY;
            }
        }
        else
        {
            start = mdlMap->vma->vm_start;

            for (i = 0; i < mdl->numPages; i++)
            {
                pfn = vmalloc_to_pfn(addr);

                if (remap_pfn_range(mdlMap->vma,
                                    start,
                                    pfn,
                                    PAGE_SIZE,
                                    mdlMap->vma->vm_page_prot) < 0)
                {
                    up_write(&current->mm->mmap_sem);

                    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                                gcvZONE_OS,
                                "gckOS_LockPages: "
                                "gctPHYS_ADDR->0x%x Logical->0x%x Unable to map addr->0x%x to start->0x%x",
                                (gctUINT32)Physical,
                                (gctUINT32)*Logical,
                                (gctUINT32)addr,
                                (gctUINT32)start);

                    mdlMap->vmaAddr = gcvNULL;

                    MEMORY_UNLOCK(Os);

                    return gcvSTATUS_OUT_OF_MEMORY;
                }

                start += PAGE_SIZE;
                addr += PAGE_SIZE;
            }
        }

        up_write(&current->mm->mmap_sem);
    }

    /* Convert pointer to MDL. */
    *Logical = mdlMap->vmaAddr;

    /* Return the page number according to the GPU page size. */
    gcmkASSERT((PAGE_SIZE % 4096) == 0);
    gcmkASSERT((PAGE_SIZE / 4096) >= 1);

    *PageCount = mdl->numPages * (PAGE_SIZE / 4096);

    MEMORY_UNLOCK(Os);

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "gckOS_LockPages: "
                "gctPHYS_ADDR->0x%x Bytes->0x%x Logical->0x%x pid->%d",
                (gctUINT32)Physical,
                (gctUINT32)Bytes,
                (gctUINT32)*Logical,
                mdlMap->pid);

    /* Success. */
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
    PLINUX_MDL  mdl;
    gctUINT32*  table;
    gctSTRING   addr;
    gctINT      i = 0;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != NULL);
    gcmkVERIFY_ARGUMENT(PageCount > 0);
    gcmkVERIFY_ARGUMENT(PageTable != NULL);

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "in gckOS_MapPages");

    /* Convert pointer to MDL. */
    mdl = (PLINUX_MDL)Physical;

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "gckOS_MapPages: "
                "Physical->0x%x PageCount->0x%x PagedMemory->?%d",
                (gctUINT32)Physical,
                (gctUINT32)PageCount,
                mdl->pagedMem);

    MEMORY_LOCK(Os);

    table = (gctUINT32 *)PageTable;

     /* Get all the physical addresses and store them in the page table. */

    addr = mdl->addr;

    if (mdl->pagedMem)
    {
        /* Try to get the user pages so DMA can happen. */
        while (PageCount-- > 0)
        {
            if (mdl->contiguous)
            {
                *table++ = virt_to_phys(addr);
            }
            else
            {
                *table++ = page_to_phys(vmalloc_to_page(addr));
            }

            addr += 4096;
            i++;
        }
    }
    else
    {
        gcmkTRACE_ZONE(gcvLEVEL_INFO,
                    gcvZONE_OS,
                    "We should not get this call for Non Paged Memory!");

        while (PageCount-- > 0)
        {
            *table++ = (gctUINT32)virt_to_phys(addr);
            addr += 4096;
        }
    }

    MEMORY_UNLOCK(Os);

    /* Success. */
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
gceSTATUS gckOS_UnlockPages(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    IN gctPOINTER Logical
    )
{
    PLINUX_MDL_MAP          mdlMap;
    PLINUX_MDL              mdl = (PLINUX_MDL)Physical;
    struct task_struct *    task;
    // dkm: add
    struct mm_struct *      mm;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != NULL);
    gcmkVERIFY_ARGUMENT(Logical != NULL);

    /* Make sure there is already a mapping...*/
    gcmkVERIFY_ARGUMENT(mdl->addr != NULL);

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "in gckOS_UnlockPages");

    MEMORY_LOCK(Os);

    mdlMap = mdl->maps;

    while (mdlMap != gcvNULL)
    {
        if (mdlMap->vmaAddr != gcvNULL)
        {
            /* Get the current pointer for the task with stored pid. */
            task = FIND_TASK_BY_PID(mdlMap->pid);
            // dkm: add
            if(task) {
                mm = get_task_mm(task);
                put_task_struct(task);
            } else {
                mm = gcvNULL;
            }

            // dkm: modify
            if (mm)
            {
                down_write(&mm->mmap_sem);
                do_munmap(mm, (unsigned long)Logical, mdl->numPages * PAGE_SIZE);
                up_write(&mm->mmap_sem);
                mmput(mm);
            }

            mdlMap->vmaAddr = gcvNULL;
        }

        mdlMap = mdlMap->next;
    }

    MEMORY_UNLOCK(Os);

    /* Success. */
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
gceSTATUS gckOS_AllocateContiguous(
    IN gckOS Os,
    IN gctBOOL InUserSpace,
    IN OUT gctSIZE_T * Bytes,
    OUT gctPHYS_ADDR * Physical,
    OUT gctPOINTER * Logical
    )
{
    /* Same as non-paged memory for now. */
    return gckOS_AllocateNonPagedMemory(Os,
                InUserSpace,
                Bytes,
                Physical,
                Logical
                );
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
gceSTATUS gckOS_FreeContiguous(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    )
{
    /* Same of non-paged memory for now. */
    return gckOS_FreeNonPagedMemory(Os, Bytes, Physical, Logical);
}

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
    gceSTATUS status;

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
        kernel = device->kernel;

        /* Split the memory address into a pool type and offset. */
        gcmkERR_BREAK(gckHARDWARE_SplitMemory(
            kernel->hardware, Address, &pool, &offset
            ));

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
            return gcvSTATUS_INVALID_ARGUMENT;
        }

        /* Build logical address of specified address. */
        * KernelPointer = ((gctUINT8_PTR) logical) + offset;

        /* Success. */
        return gcvSTATUS_OK;
    }
    while (gcvFALSE);

    /* Return status. */
    return status;
}

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
#if NO_USER_DIRECT_ACCESS_FROM_KERNEL
    gctPOINTER buf = gcvNULL;
    gctUINT32 len;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Pointer != gcvNULL);
    gcmkVERIFY_ARGUMENT(Size > 0);
    gcmkVERIFY_ARGUMENT(KernelPointer != gcvNULL);

    buf = kmalloc(Size, GFP_KERNEL);
    if (buf == gcvNULL)
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR,
            gcvZONE_OS,
            "Failed to allocate memory at line %d in %s.",
            __LINE__, __FILE__
            );

        return gcvSTATUS_OUT_OF_MEMORY;
    }

    len = copy_from_user(buf, Pointer, Size);
    if (len != 0)
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR,
            gcvZONE_OS,
            "Failed to copy data from user at line %d in %s.",
            __LINE__, __FILE__
            );

        if (buf != gcvNULL)
        {
            kfree(buf);
        }

        return gcvSTATUS_GENERIC_IO;
    }

    *KernelPointer = buf;
#else
    *KernelPointer = Pointer;
#endif /* NO_USER_DIRECT_ACCESS_FROM_KERNEL */

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
#if NO_USER_DIRECT_ACCESS_FROM_KERNEL
    gctUINT32 len;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Pointer != gcvNULL);
    gcmkVERIFY_ARGUMENT(Size > 0);
    gcmkVERIFY_ARGUMENT(KernelPointer != gcvNULL);

    len = copy_to_user(Pointer, KernelPointer, Size);

    kfree(KernelPointer);

    if (len != 0)
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR,
            gcvZONE_OS,
            "Failed to copy data to user at line %d in %s.",
            __LINE__, __FILE__
            );
        return gcvSTATUS_GENERIC_IO;
    }
#endif /* NO_USER_DIRECT_ACCESS_FROM_KERNEL */

    return gcvSTATUS_OK;
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
    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Address != NULL);

    /* Write memory. */
    writel(Data, (gctUINT8 *)Address);

    /* Success. */
    return gcvSTATUS_OK;
}

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
#if USE_NEW_LINUX_SIGNAL
    return gcvSTATUS_NOT_SUPPORTED;
#else
    gcsSIGNAL_PTR signal;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Signal != NULL);

    /* Create an event structure. */
    signal = (gcsSIGNAL_PTR)kmalloc(sizeof(gcsSIGNAL), GFP_ATOMIC);

    if (signal == gcvNULL)
    {
        return gcvSTATUS_OUT_OF_MEMORY;
    }

    signal->manualReset = ManualReset;

    init_completion(&signal->event);

    atomic_set(&signal->ref, 1);

    signal->destroyed = gcvFALSE;

    *Signal = (gctSIGNAL) signal;

    return gcvSTATUS_OK;
#endif
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
#if USE_NEW_LINUX_SIGNAL
    return gcvSTATUS_NOT_SUPPORTED;
#else
    gcsSIGNAL_PTR signal = (gcsSIGNAL_PTR) Signal;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Signal != NULL);

    if (atomic_dec_and_test(&signal->ref))
    {
         /* Free the signal. */
        kfree(signal);
    }
    else
    {
        /* Set destroyed flag */
    	signal->destroyed = gcvTRUE;
    }

    /* Success. */
    return gcvSTATUS_OK;
#endif
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
#if USE_NEW_LINUX_SIGNAL
    return gcvSTATUS_NOT_SUPPORTED;
#else
    gcsSIGNAL_PTR signal;

    gcmkHEADER_ARG("Os=0x%x Signal=0x%x State=%d", Os, Signal, State);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Signal != gcvNULL);

    signal = (gcsSIGNAL_PTR) Signal;

    /* Set the new state of the event. */
    if (signal->manualReset)
    {
        if (State)
        {
            /* Set the event to a signaled state. */
            complete_all(&signal->event);
        }
        else
        {
            /* Set the event to an unsignaled state. */
            INIT_COMPLETION(signal->event);
        }
    }
    else
    {
        if (State)
        {
            /* Set the event to a signaled state. */
            complete(&signal->event);

        }
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
#endif
}

#if USE_NEW_LINUX_SIGNAL
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
    gctINT result;
    struct task_struct * task;
    struct siginfo info;

    task = FIND_TASK_BY_PID((pid_t) Process);

    if (task != gcvNULL)
    {
        /* Fill in the siginfo structure. */
        info.si_signo = Os->device->signal;
        info.si_errno = 0;
        info.si_code  = __SI_CODE(__SI_RT, SI_KERNEL);
        info.si_ptr   = Signal;

        /* Send the signal. */
        if ((result = send_sig_info(Os->device->signal, &info, task)) < 0)
        {
            status = gcvSTATUS_GENERIC_IO;

            gcmkTRACE(gcvLEVEL_ERROR,
                     "%s(%d): send_sig_info failed.",
                     __FUNCTION__, __LINE__);
        }
        else
        {
            /* Success. */
            status = gcvSTATUS_OK;
        }
        // dkm: add
        put_task_struct(task);
    }
    else
    {
        status = gcvSTATUS_GENERIC_IO;

        gcmkTRACE(gcvLEVEL_ERROR,
                 "%s(%d): find_task_by_pid failed.",
                 __FUNCTION__, __LINE__);
    }

    /* Return status. */
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
    IN gctUINT32 Wait
    )
{
    return gcvSTATUS_NOT_SUPPORTED;
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
    return gcvSTATUS_NOT_SUPPORTED;
}

/*******************************************************************************
**
**	gckOS_UnmapSignal
**
**	Unmap a signal .
**
**	INPUT:
**
**		gckOS Os
**			Pointer to an gckOS object.
**
**		gctSIGNAL Signal
**			Pointer to that gctSIGNAL mapped.
*/
gceSTATUS
gckOS_UnmapSignal(
    IN gckOS Os,
    IN gctSIGNAL Signal
    )
{
    return gcvSTATUS_NOT_SUPPORTED;
}

#else

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
    gceSTATUS status = gcvSTATUS_OK;
    gctSIGNAL signal = gcvNULL;
    gctBOOL   mapped = gcvFALSE;

    gcmkHEADER_ARG("Os=0x%x Signal=%d Process=0x%x",
                   Os, (gctINT) Signal, Process);

    /* Map the signal into kernel space. */
    gcmkONERROR(gckOS_MapSignal(Os, Signal, Process, &signal));
    mapped = gcvTRUE;

    /* Signal. */
    status = gckOS_Signal(Os, signal, gcvTRUE);

    /* Unmap the signal */
    mapped = gcvFALSE;
    gcmkONERROR(gckOS_UnmapSignal(Os, signal));

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (mapped)
    {
        gcmkVERIFY_OK(gckOS_UnmapSignal(Os, signal));
    }

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
    IN gctUINT32 Wait
    )
{
    gceSTATUS status;
    gcsSIGNAL_PTR signal;
    gctUINT timeout;
    gctUINT rc;

    gcmkHEADER_ARG("Os=0x%x Signal=0x%x Wait=%u", Os, Signal, Wait);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Signal != gcvNULL);

    signal = (gcsSIGNAL_PTR) Signal;

    /* Convert wait to milliseconds. */
    timeout = (Wait == gcvINFINITE) ? MAX_SCHEDULE_TIMEOUT : Wait*HZ/1000;

    /* Linux bug ? */
    if (!signal->manualReset && timeout == 0) timeout = 1;

    rc = wait_for_completion_interruptible_timeout(&signal->event, timeout);
    status = ((rc == 0) && !signal->event.done) ? gcvSTATUS_TIMEOUT
                                                : gcvSTATUS_OK;

    /* Return status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_WaitSignalUninterruptible
**
**  Wait for a signal to become signaled uninterruptibly.
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
gckOS_WaitSignalUninterruptible(
    IN gckOS Os,
    IN gctSIGNAL Signal,
    IN gctUINT32 Wait
    )
{
    gceSTATUS status;
    gcsSIGNAL_PTR signal;
    gctUINT timeout;
    gctUINT rc;

    gcmkHEADER_ARG("Os=0x%x Signal=0x%x Wait=%u", Os, Signal, Wait);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Signal != gcvNULL);

    signal = (gcsSIGNAL_PTR) Signal;

    /* Convert wait to milliseconds. */
    timeout = (Wait == gcvINFINITE) ? MAX_SCHEDULE_TIMEOUT : Wait*HZ/1000;

    /* Linux bug ? */
    if (!signal->manualReset && timeout == 0) timeout = 1;

    rc = wait_for_completion_timeout(&signal->event, timeout);
    status = ((rc == 0) && !signal->event.done) ? gcvSTATUS_TIMEOUT
                                                : gcvSTATUS_OK;

    /* Return status. */
    gcmkFOOTER();
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
    gctINT signalID;
    gcsSIGNAL_PTR signal;
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Os=0x%x Signal=0x%x Process=0x%x", Os, Signal, Process);

    gcmkVERIFY_ARGUMENT(Signal != gcvNULL);
    gcmkVERIFY_ARGUMENT(MappedSignal != gcvNULL);

    signalID = (gctINT) Signal - 1;

    gcmkONERROR(gckOS_AcquireMutex(Os, Os->signal.lock, gcvINFINITE));
    acquired = gcvTRUE;

    if (signalID >= 0 && signalID < Os->signal.tableLen)
    {
        /* It is a user space signal. */
        signal = Os->signal.table[signalID];

        if (signal == gcvNULL)
        {
            gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }
    }
    else
    {
        /* It is a kernel space signal structure. */
        signal = (gcsSIGNAL_PTR) Signal;
    }

    if (atomic_inc_return(&signal->ref) <= 1)
    {
        /* The previous value is 0, it has been deleted. */
        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    /* Release the mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Os, Os->signal.lock));

    *MappedSignal = (gctSIGNAL) signal;

    /* Success. */
    gcmkFOOTER_ARG("*MappedSignal=0x%x", *MappedSignal);
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Os, Os->signal.lock));
    }

    /* Return the staus. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**	gckOS_UnmapSignal
**
**	Unmap a signal .
**
**	INPUT:
**
**		gckOS Os
**			Pointer to an gckOS object.
**
**		gctSIGNAL Signal
**			Pointer to that gctSIGNAL mapped.
*/
gceSTATUS
gckOS_UnmapSignal(
    IN gckOS Os,
    IN gctSIGNAL Signal
    )
{
    gceSTATUS       status      = gcvSTATUS_OK;
    gctBOOL         acquired    = gcvFALSE;
    gcsSIGNAL_PTR   signal      = (gcsSIGNAL_PTR)Signal;

    gcmkVERIFY_ARGUMENT(Signal != gcvNULL);

    gcmkONERROR(gckOS_AcquireMutex(Os, Os->signal.lock, gcvINFINITE));
    acquired = gcvTRUE;

    if (atomic_dec_return(&signal->ref) < 1)
    {
        if (signal->destroyed)
        {
            /* Free the signal. */
            kfree(signal);
        }
        else
        {
            /* The previous value is less than 1, it hasn't been mapped. */
            gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }
    }

    /* Release the mutex. */
    acquired = gcvFALSE;
    gcmkONERROR(gckOS_ReleaseMutex(Os, Os->signal.lock));

    /* Success. */
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Os, Os->signal.lock));
    }

    /* Return the staus. */
    return status;
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
    gcsSIGNAL_PTR signal;
    gctINT unused, currentID, tableLen;
    gctPOINTER * table;
    gctINT i;
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Os=0x%0x ManualReset=%d", Os, ManualReset);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(SignalID != gcvNULL);

    /* Lock the table. */
    gcmkONERROR(
        gckOS_AcquireMutex(Os, Os->signal.lock, gcvINFINITE));

    acquired = gcvTRUE;

    if (Os->signal.unused < 1)
    {
        /* Enlarge the table. */
        table = (gctPOINTER *) kmalloc(
                    sizeof(gctPOINTER) * (Os->signal.tableLen + USER_SIGNAL_TABLE_LEN_INIT),
                    GFP_KERNEL);

        if (table == gcvNULL)
        {
            /* Out of memory. */
            gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
        }

        memset(table + Os->signal.tableLen, 0, sizeof(gctPOINTER) * USER_SIGNAL_TABLE_LEN_INIT);
        memcpy(table, Os->signal.table, sizeof(gctPOINTER) * Os->signal.tableLen);

        /* Release the old table. */
        kfree(Os->signal.table);

        /* Update the table. */
        Os->signal.table = table;
        Os->signal.currentID = Os->signal.tableLen;
        Os->signal.tableLen += USER_SIGNAL_TABLE_LEN_INIT;
        Os->signal.unused += USER_SIGNAL_TABLE_LEN_INIT;
    }

    table = Os->signal.table;
    currentID = Os->signal.currentID;
    tableLen = Os->signal.tableLen;
    unused = Os->signal.unused;

    /* Create a new signal. */
    gcmkONERROR(
        gckOS_CreateSignal(Os, ManualReset, (gctSIGNAL *) &signal));

    /* Save the process ID. */
    signal->process = (gctHANDLE) current->tgid;

    table[currentID] = signal;

    /* Plus 1 to avoid NULL claims. */
    *SignalID = currentID + 1;

    /* Update the currentID. */
    if (--unused > 0)
    {
        for (i = 0; i < tableLen; i++)
        {
            if (++currentID >= tableLen)
            {
                /* Wrap to the begin. */
                currentID = 0;
            }

            if (table[currentID] == gcvNULL)
            {
                break;
            }
        }
    }

    Os->signal.table = table;
    Os->signal.currentID = currentID;
    Os->signal.tableLen = tableLen;
    Os->signal.unused = unused;

    acquired = gcvFALSE;
    gcmkONERROR(
        gckOS_ReleaseMutex(Os, Os->signal.lock));

    gcmkFOOTER_ARG("*SignalID=%d", gcmOPT_VALUE(SignalID));
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(
            gckOS_ReleaseMutex(Os, Os->signal.lock));
    }

    /* Return the staus. */
    gcmkFOOTER();
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
    gceSTATUS status;
    gcsSIGNAL_PTR signal;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Os=0x%x SignalID=%d", Os, SignalID);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    gcmkONERROR(
        gckOS_AcquireMutex(Os, Os->signal.lock, gcvINFINITE));

    acquired = gcvTRUE;

    if (SignalID < 1 || SignalID > Os->signal.tableLen)
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR,
            gcvZONE_OS,
            "gckOS_DestroyUserSignal: invalid signal->%d.",
            (gctINT) SignalID
            );

        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    SignalID -= 1;

    signal = Os->signal.table[SignalID];

    if (signal == gcvNULL)
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR,
            gcvZONE_OS,
            "gckOS_DestroyUserSignal: signal is NULL."
            );

        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    /* Check to see if the process is the owner of the signal. */
    if (signal->process != (gctHANDLE) current->tgid)
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR,
            gcvZONE_OS,
            "gckOS_DestroyUserSignal: process id doesn't match. ",
            "signal->process: %d, current->tgid: %d",
            signal->process,
            current->tgid);

        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    gcmkONERROR(
        gckOS_DestroySignal(Os, signal));

    /* Update the table. */
    Os->signal.table[SignalID] = gcvNULL;
    if (Os->signal.unused++ == 0)
    {
        Os->signal.currentID = SignalID;
    }

    acquired = gcvFALSE;
    gcmkONERROR(
        gckOS_ReleaseMutex(Os, Os->signal.lock));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(
            gckOS_ReleaseMutex(Os, Os->signal.lock));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
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
    gceSTATUS status;
    gcsSIGNAL_PTR signal;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Os=0x%x SignalID=%d Wait=%u", Os, SignalID, Wait);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    gcmkONERROR(gckOS_AcquireMutex(Os, Os->signal.lock, gcvINFINITE));
    acquired = gcvTRUE;

    if (SignalID < 1 || SignalID > Os->signal.tableLen)
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR,
            gcvZONE_OS,
            "gckOS_WaitSignal: invalid signal.",
            SignalID
            );

        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    SignalID -= 1;

    signal = Os->signal.table[SignalID];

    acquired = gcvFALSE;
    gcmkONERROR(gckOS_ReleaseMutex(Os, Os->signal.lock));

    if (signal == gcvNULL)
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR,
            gcvZONE_OS,
            "gckOS_WaitSignal: signal is NULL."
            );

        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    if (signal->process != (gctHANDLE) current->tgid)
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR,
            gcvZONE_OS,
            "gckOS_WaitUserSignal: process id doesn't match. "
            "signal->process: %d, current->tgid: %d",
            signal->process,
            current->tgid);

        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }


    do
    {
        status = gckOS_WaitSignal(Os, signal, Wait == gcvINFINITE? 5000 : Wait);

        if (Wait == gcvINFINITE && status == gcvSTATUS_TIMEOUT)
        {
            gcmkPRINT("$$FLUSH$$");
        }
    }
    while (status == gcvSTATUS_TIMEOUT && Wait == gcvINFINITE);

    /* Return the status. */
    gcmkFOOTER();
    return status;

OnError:
    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(
            gckOS_ReleaseMutex(Os, Os->signal.lock));
    }

    /* Return the staus. */
    gcmkFOOTER();
    return status;
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
    gceSTATUS status;
    gcsSIGNAL_PTR signal;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Os=0x%x SignalID=%d State=%d", Os, SignalID, State);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    gcmkONERROR(gckOS_AcquireMutex(Os, Os->signal.lock, gcvINFINITE));
    acquired = gcvTRUE;

    if ((SignalID < 1)
    ||  (SignalID > Os->signal.tableLen)
    )
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR,  gcvZONE_OS,
                       "gckOS_WaitSignal: invalid signal->%d.", SignalID);

        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    SignalID -= 1;

    signal = Os->signal.table[SignalID];

    acquired = gcvFALSE;
    gcmkONERROR(gckOS_ReleaseMutex(Os, Os->signal.lock));

    if (signal == gcvNULL)
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR,
            gcvZONE_OS,
            "gckOS_WaitSignal: signal is NULL."
            );

        gcmkONERROR(gcvSTATUS_INVALID_REQUEST);
    }

    if (signal->process != (gctHANDLE) current->tgid)
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR,
            gcvZONE_OS,
            "gckOS_DestroyUserSignal: process id doesn't match. ",
            "signal->process: %d, current->tgid: %d",
            signal->process,
            current->tgid);

        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    status = gckOS_Signal(Os, signal, State);

    /* Success. */
    gcmkFOOTER();
    return status;

OnError:
    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(
            gckOS_ReleaseMutex(Os, Os->signal.lock));
    }

    /* Return the staus. */
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckOS_DestroyAllUserSignals(
    IN gckOS Os
    )
{
    gctINT signal;

    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    gcmkVERIFY_OK(gckOS_AcquireMutex(Os,
        Os->signal.lock,
        gcvINFINITE
        ));

    if (Os->signal.unused == Os->signal.tableLen)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Os,
            Os->signal.lock
            ));

        return gcvSTATUS_OK;
    }

    for (signal = 0; signal < Os->signal.tableLen; signal++)
    {
        if (Os->signal.table[signal] != gcvNULL &&
            ((gcsSIGNAL_PTR)Os->signal.table[signal])->process == (gctHANDLE) current->tgid)
        {
            gckOS_Signal(Os, Os->signal.table[signal], gcvTRUE);

            gckOS_DestroySignal(Os, Os->signal.table[signal]);

            /* Update the signal table. */
            Os->signal.table[signal] = gcvNULL;
            if (Os->signal.unused++ == 0)
            {
                Os->signal.currentID = signal;
            }
        }
    }

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Os,
        Os->signal.lock
        ));

    return gcvSTATUS_OK;
}

#endif /* USE_NEW_LINUX_SIGNAL */

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
    IN gctPOINTER Memory,
    IN gctSIZE_T Size,
    OUT gctPOINTER * Info,
    OUT gctUINT32_PTR Address
    )
{
    gceSTATUS status;
    gctSIZE_T pageCount, i, j;
    gctUINT32_PTR pageTable;
    gctUINT32 address;
    gctUINT32 start, end, memory;
    gctINT result = 0;

    gcsPageInfo_PTR info = gcvNULL;
    struct page **pages = gcvNULL;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);
    gcmkVERIFY_ARGUMENT(Size > 0);
    gcmkVERIFY_ARGUMENT(Info != gcvNULL);
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE,
        gcvZONE_OS,
        "[gckOS_MapUserMemory] enter."
        );

    do
    {
        memory = (gctUINT32) Memory;

        /* Get the number of required pages. */
        end = (memory + Size + PAGE_SIZE - 1) >> PAGE_SHIFT;
        start = memory >> PAGE_SHIFT;
        pageCount = end - start;

        gcmkTRACE_ZONE(gcvLEVEL_INFO,
            gcvZONE_OS,
            "[gckOS_MapUserMemory] pageCount: %d.",
            pageCount
            );

        /* Invalid argument. */
        if (pageCount == 0)
        {
            return gcvSTATUS_INVALID_ARGUMENT;
        }

        /* Overflow. */
        if ((memory + Size) < memory)
        {
            return gcvSTATUS_INVALID_ARGUMENT;
        }

        MEMORY_MAP_LOCK(Os);

        /* Allocate the Info struct. */
        info = (gcsPageInfo_PTR)kmalloc(sizeof(gcsPageInfo), GFP_KERNEL);

        if (info == gcvNULL)
        {
            status = gcvSTATUS_OUT_OF_MEMORY;
            break;
        }

        /* Allocate the array of page addresses. */
        pages = (struct page **)kmalloc(pageCount * sizeof(struct page *), GFP_KERNEL);

        if (pages == gcvNULL)
        {
            status = gcvSTATUS_OUT_OF_MEMORY;
            break;
        }

        /* Get the user pages. */
        down_read(&current->mm->mmap_sem);
        result = get_user_pages(current,
                    current->mm,
                    memory & PAGE_MASK,
                    pageCount,
                    1,
                    0,
                    pages,
                    NULL
                    );
        up_read(&current->mm->mmap_sem);

        if (result <=0 || result < pageCount)
        {
            struct vm_area_struct *vma;

            vma = find_vma(current->mm, memory);

            if (vma && (vma->vm_flags & VM_PFNMAP) )
            {
                do
                {
                    pte_t       * pte;
                    spinlock_t  * ptl;
                    unsigned long pfn;

                    pgd_t * pgd = pgd_offset(current->mm, memory);
                    pud_t * pud = pud_offset(pgd, memory);
                    if (pud)
                    {
                        pmd_t * pmd = pmd_offset(pud, memory);
                        if (pmd)
                        {
                            pte = pte_offset_map_lock(current->mm, pmd, memory, &ptl);
                            if (!pte)
                            {
                                break;
                            }
                        }
                        else
                        {
                            break;
                        }
                    }
                    else
                    {
                        break;
                    }

                    pfn      = pte_pfn(*pte);
                    *Address = ((pfn << PAGE_SHIFT) | (((unsigned long)Memory) & ~PAGE_MASK))
                                - Os->baseAddress;
                    *Info    = gcvNULL;

                    pte_unmap_unlock(pte, ptl);

                    /* Release page info struct. */
                    if (info != gcvNULL)
                    {
                        /* Free the page info struct. */
                        kfree(info);
                    }

                    /* Free the page table. */
                    if (pages != gcvNULL)
                    {
                        /* Release the pages if any. */
                        if (result > 0)
                        {
                            for (i = 0; i < result; i++)
                            {
                                if (pages[i] == gcvNULL)
                                {
                                    break;
                                }

                                page_cache_release(pages[i]);
                            }
                        }

                        kfree(pages);
                    }

                    MEMORY_MAP_UNLOCK(Os);

                    gcmkFOOTER_ARG("*Info=0x%x *Address=0x%08x",
                                   *Info, *Address);
                    return gcvSTATUS_OK;
                }
                while (gcvFALSE);

                *Address = ~0;
                *Info = gcvNULL;

                status = gcvSTATUS_OUT_OF_RESOURCES;
                break;
            }
            else
            {
                status = gcvSTATUS_OUT_OF_RESOURCES;
                break;
            }
        }

        for (i = 0; i < pageCount; i++)
        {
            /* Flush the data cache. */
#ifdef ANDROID
            dma_sync_single_for_device(
                        gcvNULL,
                        page_to_phys(pages[i]),
                        PAGE_SIZE,
                        DMA_TO_DEVICE);
#else
            flush_dcache_page(pages[i]);
#endif
        }

        /* Allocate pages inside the page table. */
        gcmkERR_BREAK(gckMMU_AllocatePages(Os->device->kernel->mmu,
                                          pageCount * (PAGE_SIZE/4096),
                                          (gctPOINTER *) &pageTable,
                                          &address));

        /* Fill the page table. */
        for (i = 0; i < pageCount; i++)
        {
            /* Get the physical address from page struct. */
            pageTable[i * (PAGE_SIZE/4096)] = page_to_phys(pages[i]);

            for (j = 1; j < (PAGE_SIZE/4096); j++)
            {
                pageTable[i * (PAGE_SIZE/4096) + j] = pageTable[i * (PAGE_SIZE/4096)] + 4096 * j;
            }

            gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "[gckOS_MapUserMemory] pages[%d]: 0x%x, pageTable[%d]: 0x%x.",
                i, pages[i],
                i, pageTable[i]);
        }

        /* Save pointer to page table. */
        info->pageTable = pageTable;
        info->pages = pages;

        *Info = (gctPOINTER) info;

        gcmkTRACE_ZONE(gcvLEVEL_INFO,
            gcvZONE_OS,
            "[gckOS_MapUserMemory] info->pages: 0x%x, info->pageTable: 0x%x, info: 0x%x.",
            info->pages,
            info->pageTable,
            info
            );

        /* Return address. */
        *Address = address + (memory & ~PAGE_MASK);

        gcmkTRACE_ZONE(gcvLEVEL_INFO,
            gcvZONE_OS,
            "[gckOS_MapUserMemory] Address: 0x%x.",
            *Address
            );

        /* Success. */
        status = gcvSTATUS_OK;
    }
    while (gcvFALSE);

    if (gcmIS_ERROR(status))
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR,
            gcvZONE_OS,
            "[gckOS_MapUserMemory] error occured: %d.",
            status
            );

        /* Release page array. */
        if (result > 0 && pages != gcvNULL)
        {
            gcmkTRACE_ZONE(gcvLEVEL_ERROR,
                gcvZONE_OS,
                "[gckOS_MapUserMemory] error: page table is freed."
                );

            for (i = 0; i < result; i++)
            {
                if (pages[i] == gcvNULL)
                {
                    break;
                }
#ifdef ANDROID
                dma_sync_single_for_device(
                            gcvNULL,
                            page_to_phys(pages[i]),
                            PAGE_SIZE,
                            DMA_FROM_DEVICE);
#endif
                page_cache_release(pages[i]);
            }
        }

        if (pages != gcvNULL)
        {
            gcmkTRACE_ZONE(gcvLEVEL_ERROR,
                gcvZONE_OS,
                "[gckOS_MapUserMemory] error: pages is freed."
                );

            /* Free the page table. */
            kfree(pages);
            info->pages = gcvNULL;
        }

        /* Release page info struct. */
        if (info != gcvNULL)
        {
            gcmkTRACE_ZONE(gcvLEVEL_ERROR,
                gcvZONE_OS,
                "[gckOS_MapUserMemory] error: info is freed."
                );

            /* Free the page info struct. */
            kfree(info);
            *Info = gcvNULL;
        }
    }

    MEMORY_MAP_UNLOCK(Os);

    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE,
        gcvZONE_OS,
        "[gckOS_MapUserMemory] leave."
        );

    /* Return the status. */
    return status;
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
    IN gctPOINTER Memory,
    IN gctSIZE_T Size,
    IN gctPOINTER Info,
    IN gctUINT32 Address
    )
{
    gceSTATUS status;
    gctUINT32 memory, start, end;
    gcsPageInfo_PTR info;
    gctSIZE_T pageCount, i;
    struct page **pages;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);
    gcmkVERIFY_ARGUMENT(Size > 0);
    gcmkVERIFY_ARGUMENT(Info != gcvNULL);

    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE,
        gcvZONE_OS,
        "[gckOS_UnmapUserMemory] enter."
        );

    do
    {
        info = (gcsPageInfo_PTR) Info;

        if (info == gcvNULL)
        {
            return gcvSTATUS_OK;
        }

        pages = info->pages;

        gcmkTRACE_ZONE(gcvLEVEL_INFO,
            gcvZONE_OS,
            "[gckOS_UnmapUserMemory] info: 0x%x, pages: 0x%x.",
            info,
            pages
            );

        /* Invalid page array. */
        if (pages == gcvNULL)
        {
            return gcvSTATUS_INVALID_ARGUMENT;
        }

        memory = (gctUINT32) Memory;
        end = (memory + Size + PAGE_SIZE - 1) >> PAGE_SHIFT;
        start = memory >> PAGE_SHIFT;
        pageCount = end - start;

        /* Overflow. */
        if ((memory + Size) < memory)
        {
            return gcvSTATUS_INVALID_ARGUMENT;
        }

        /* Invalid argument. */
        if (pageCount == 0)
        {
            return gcvSTATUS_INVALID_ARGUMENT;
        }

        gcmkTRACE_ZONE(gcvLEVEL_INFO,
            gcvZONE_OS,
            "[gckOS_UnmapUserMemory] memory: 0x%x, pageCount: %d, pageTable: 0x%x.",
            memory,
            pageCount,
            info->pageTable
            );

        MEMORY_MAP_LOCK(Os);

        /* Free the pages from the MMU. */
        gcmkERR_BREAK(gckMMU_FreePages(Os->device->kernel->mmu,
                                      info->pageTable,
                                      pageCount * (PAGE_SIZE/4096)
                                      ));

        /* Release the page cache. */
        for (i = 0; i < pageCount; i++)
        {
            gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "[gckOS_UnmapUserMemory] pages[%d]: 0x%x.",
                i,
                pages[i]
                );

            if (!PageReserved(pages[i]))
            {
                SetPageDirty(pages[i]);
            }

#ifdef ANDROID
            dma_sync_single_for_device(
                        gcvNULL,
                        page_to_phys(pages[i]),
                        PAGE_SIZE,
                        DMA_FROM_DEVICE);
#endif
            page_cache_release(pages[i]);
        }

        /* Success. */
        status = gcvSTATUS_OK;
    }
    while (gcvFALSE);

    if (info != gcvNULL)
    {
        /* Free the page array. */
        if (info->pages != gcvNULL)
        {
            kfree(info->pages);
        }

        kfree(info);
    }

    MEMORY_MAP_UNLOCK(Os);

    /* Return the status. */
    return status;
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
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(BaseAddress != gcvNULL);

    /* Return base address. */
    *BaseAddress = Os->baseAddress;

    /* Success. */
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_SuspendInterrupt(
    IN gckOS Os
    )
{
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    disable_irq(Os->device->irqLine);

    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_ResumeInterrupt(
    IN gckOS Os
    )
{
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    enable_irq(Os->device->irqLine);

    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_MemCopy(
        IN gctPOINTER Destination,
        IN gctCONST_POINTER Source,
        IN gctSIZE_T Bytes
        )
{
        gcmkVERIFY_ARGUMENT(Destination != NULL);
        gcmkVERIFY_ARGUMENT(Source != NULL);
        gcmkVERIFY_ARGUMENT(Bytes > 0);

        memcpy(Destination, Source, Bytes);

        return gcvSTATUS_OK;
}

gceSTATUS
gckOS_ZeroMemory(
    IN gctPOINTER Memory,
    IN gctSIZE_T Bytes
    )
{
    gcmkHEADER_ARG("Memory=0x%x Bytes=%lu", Memory, Bytes);

    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

    memset(Memory, 0, Bytes);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

#if gcdkUSE_MEMORY_RECORD
MEMORY_RECORD_PTR
CreateMemoryRecord(
    gckOS Os,
    gcsHAL_PRIVATE_DATA_PTR private,
    MEMORY_RECORD_PTR List,
    gceMEMORY_TYPE Type,
	gctSIZE_T Bytes,
	gctPHYS_ADDR Physical,
	gctPOINTER Logical
    )
{
    MEMORY_RECORD_PTR   mr;

    gcmkASSERT(Type == gcvCONTIGUOUS_MEMORY || Type == gcvNON_PAGED_MEMORY);

    mr = (MEMORY_RECORD_PTR)kmalloc(sizeof(struct MEMORY_RECORD), GFP_ATOMIC);
    if (mr == gcvNULL) return gcvNULL;

    MEMORY_LOCK(Os);

    mr->type                = Type;
    mr->u.Memory.bytes      = Bytes;
    mr->u.Memory.physical   = Physical;
    mr->u.Memory.logical    = Logical;

    mr->prev            = List;
    mr->next            = List->next;
    List->next->prev    = mr;
    List->next          = mr;

    MEMORY_UNLOCK(Os);

    return mr;
}

void
DestroyMemoryRecord(
    gckOS Os,
    gcsHAL_PRIVATE_DATA_PTR private,
    MEMORY_RECORD_PTR Mr
    )
{
    gcmkASSERT(Mr->type == gcvCONTIGUOUS_MEMORY || Mr->type == gcvNON_PAGED_MEMORY);

    MEMORY_LOCK(Os);

    Mr->prev->next      = Mr->next;
    Mr->next->prev      = Mr->prev;

    MEMORY_UNLOCK(Os);

    kfree(Mr);
}

MEMORY_RECORD_PTR
FindMemoryRecord(
    gckOS Os,
    gcsHAL_PRIVATE_DATA_PTR private,
    MEMORY_RECORD_PTR List,
    gceMEMORY_TYPE Type,
	gctSIZE_T Bytes,
	gctPHYS_ADDR Physical,
	gctPOINTER Logical
    )
{
    MEMORY_RECORD_PTR mr;

    gcmkASSERT(Type == gcvCONTIGUOUS_MEMORY || Type == gcvNON_PAGED_MEMORY);

    MEMORY_LOCK(Os);

    mr = List->next;

    while (mr != List)
    {
        if (mr->type                    == Type 
            && mr->u.Memory.bytes       == Bytes
            && mr->u.Memory.physical    == Physical
            && mr->u.Memory.logical     == Logical)
        {
            MEMORY_UNLOCK(Os);

            return mr;
        }

        mr = mr->next;
    }

    MEMORY_UNLOCK(Os);

    return gcvNULL;
}

MEMORY_RECORD_PTR
CreateVideoMemoryRecord(
    gckOS Os,
    gcsHAL_PRIVATE_DATA_PTR private,
    MEMORY_RECORD_PTR List,
    gcuVIDMEM_NODE_PTR Node,
    gceSURF_TYPE Type,
    gctSIZE_T Bytes
    )
{
    MEMORY_RECORD_PTR   mr;

    mr = (MEMORY_RECORD_PTR)kmalloc(sizeof(struct MEMORY_RECORD), GFP_ATOMIC);
    if (mr == gcvNULL) return gcvNULL;

    MEMORY_LOCK(Os);

    mr->type                = gcvVIDEO_MEMORY;
    mr->u.VideoMemory.node  = Node;
    mr->u.VideoMemory.type  = Type;
    mr->u.VideoMemory.bytes = Bytes;

#if gcdkREPORT_VIDMEM_USAGE
    private->allocatedMem[Type]   += Bytes;
    private->maxAllocatedMem[Type] = 
        (private->maxAllocatedMem[Type] > private->allocatedMem[Type])
            ? private->maxAllocatedMem[Type] : private->allocatedMem[Type];

    private->totalAllocatedMem   += Bytes;
    private->maxTotalAllocatedMem = 
        (private->maxTotalAllocatedMem > private->totalAllocatedMem)
            ? private->maxTotalAllocatedMem : private->totalAllocatedMem;
#endif

    mr->prev            = List;
    mr->next            = List->next;
    List->next->prev    = mr;
    List->next          = mr;

    MEMORY_UNLOCK(Os);

    return mr;
}

void
DestroyVideoMemoryRecord(
    gckOS Os,
    gcsHAL_PRIVATE_DATA_PTR private,
    MEMORY_RECORD_PTR Mr
    )
{
    gcmkASSERT(Mr->type == gcvVIDEO_MEMORY);

    MEMORY_LOCK(Os);

#if gcdkREPORT_VIDMEM_USAGE
    private->allocatedMem[Mr->u.VideoMemory.type] -= Mr->u.VideoMemory.bytes;
    private->totalAllocatedMem                    -= Mr->u.VideoMemory.bytes;
#endif

    Mr->prev->next      = Mr->next;
    Mr->next->prev      = Mr->prev;

    MEMORY_UNLOCK(Os);

    kfree(Mr);
}

MEMORY_RECORD_PTR
FindVideoMemoryRecord(
    gckOS Os,
    gcsHAL_PRIVATE_DATA_PTR private,
    MEMORY_RECORD_PTR List,
    gcuVIDMEM_NODE_PTR Node
    )
{
    MEMORY_RECORD_PTR mr;

    MEMORY_LOCK(Os);

    mr = List->next;

    while (mr != List)
    {
        if (mr->type == gcvVIDEO_MEMORY && mr->u.VideoMemory.node == Node)
        {
            MEMORY_UNLOCK(Os);

            return mr;
        }

        mr = mr->next;
    }

    MEMORY_UNLOCK(Os);

    return gcvNULL;
}

void
FreeAllMemoryRecord(
    gckOS Os,
    gcsHAL_PRIVATE_DATA_PTR private,
    MEMORY_RECORD_PTR List
    )
{
    MEMORY_RECORD_PTR mr;
    gctUINT i = 0;

#if gcdkREPORT_VIDMEM_USAGE
    gctUINT type;

    printk("------------------------------------\n");
    printk("   Type         Current          Max\n");

    for (type = 0; type < gcvSURF_NUM_TYPES; type++)
    {
        printk("[%8s]  %8llu KB  %8llu KB\n", 
               _MemTypes[type],
               private->allocatedMem[type] / 1024,
               private->maxAllocatedMem[type] / 1024);
    }

    printk("[   TOTAL]  %8llu KB  %8llu KB\n",
           private->totalAllocatedMem / 1024,
           private->maxTotalAllocatedMem / 1024);
#endif

    MEMORY_LOCK(Os);

    while (List->next != List)
    {
        mr = List->next;

        mr->prev->next      = mr->next;
        mr->next->prev      = mr->prev;

        i++;

        MEMORY_UNLOCK(Os);

        switch (mr->type)
        {
        case gcvNON_PAGED_MEMORY:
            gcmkTRACE_ZONE(gcvLEVEL_ERROR,
                    gcvZONE_OS,
                    "Unfreed non-paged memory: physical: %p, logical: %p, bytes: %d",
                    mr->u.Memory.physical, mr->u.Memory.logical, mr->u.Memory.bytes);

            gckOS_FreeNonPagedMemory(Os,
                                 mr->u.Memory.bytes,
                                 mr->u.Memory.physical,
                                 mr->u.Memory.logical);
            break;

        case gcvCONTIGUOUS_MEMORY:
            gcmkTRACE_ZONE(gcvLEVEL_ERROR,
                    gcvZONE_OS,
                    "Unfreed contiguous memory: physical: %p, logical: %p, bytes: %d",
                    mr->u.Memory.physical, mr->u.Memory.logical, mr->u.Memory.bytes);

            gckOS_FreeContiguous(Os,
                                 mr->u.Memory.physical,
                                 mr->u.Memory.logical,
                                 mr->u.Memory.bytes);
            break;

        case gcvVIDEO_MEMORY:
            gcmkTRACE_ZONE(gcvLEVEL_ERROR,
                    gcvZONE_OS,
                    "Unfreed %s memory: node: %p",
                    (mr->u.VideoMemory.node->VidMem.memory->object.type == gcvOBJ_VIDMEM)?
                        "video" : (mr->u.VideoMemory.node->Virtual.contiguous)?
                            "virtual-contiguous" : "virtual",
                    mr->u.VideoMemory.node);

            while (gcvTRUE)
            {
                if (mr->u.VideoMemory.node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
                {
                    if (mr->u.VideoMemory.node->VidMem.locked == 0) break;
                }
                else
                {
                    if (mr->u.VideoMemory.node->Virtual.locked == 0) break;
                }

                gckVIDMEM_Unlock(mr->u.VideoMemory.node, gcvSURF_TYPE_UNKNOWN, gcvNULL);
            }

            gckVIDMEM_Free(mr->u.VideoMemory.node);
            break;

        default:
            gcmkASSERT(0);
            break;
        }

        kfree(mr);

        MEMORY_LOCK(Os);
    }

    MEMORY_UNLOCK(Os);

// dkm : gcdkUSE_MAPED_NONPAGE_CACHE
#if gcdkUSE_MAPED_NONPAGE_CACHE
    _FreeAllMapedNonPagedCache(Os, current->tgid);
#endif

    if (i > 0)
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR,
                gcvZONE_OS,
                "======== Total %d unfreed memory block(s) ========", i);
    }
}
#endif

/*******************************************************************************
**  gckOS_CacheFlush
**
**  Flush the cache for the specified addresses.  The GPU is going to need the
**  data.  If the system is allocating memory as non-cachable, this function can
**  be ignored.
**
**  ARGUMENTS:
**
**      gckOS Os
**          Pointer to gckOS object.
**
**      gctHANDLE Process
**          Process handle Logical belongs to or gcvNULL if Logical belongs to
**          the kernel.
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
    IN gctHANDLE Process,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    )
{
// dkm: gcdENABLE_MEM_CACHE
#if (1==gcdENABLE_MEM_CACHE)
    dmac_clean_range(Logical, Logical+Bytes);
#elif (2==gcdENABLE_MEM_CACHE)
    dsb();
#endif
    return gcvSTATUS_OK;
}

/*******************************************************************************
**  gckOS_CacheInvalidate
**
**  Flush the cache for the specified addresses and invalidate the lines as
**  well.  The GPU is going to need and modify the data.  If the system is
**  allocating memory as non-cachable, this function can be ignored.
**
**  ARGUMENTS:
**
**      gckOS Os
**          Pointer to gckOS object.
**
**      gctHANDLE Process
**          Process handle Logical belongs to or gcvNULL if Logical belongs to
**          the kernel.
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
    IN gctHANDLE Process,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    )
{
// dkm: gcdENABLE_MEM_CACHE
#if (1==gcdENABLE_MEM_CACHE)
    dmac_flush_range(Logical, Logical+Bytes);
#elif (2==gcdENABLE_MEM_CACHE)
    dsb();
#endif
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
    gctUINT32 idle = 0, dma = 0, axi = 0, read0 = 0, read1 = 0, write = 0;
    gctUINT32 debugState = 0, memoryDebug = 0;
    gctUINT32 debugCmdLow = 0, debugCmdHi = 0;
    gctUINT32 i, debugSignalsPe, debugSignalsMc;

    gcmkHEADER_ARG("Os=0x%x Hardware=0x%x Reason=%d", Os, Hardware, Reason);

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
                                                gcvPOWER_IDLE_BROADCAST));
        break;

    case gcvBROADCAST_GPU_COMMIT:
        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_OS, "COMMIT has arrived.");

        /* Put GPU ON. */
        gcmkONERROR(
            gckHARDWARE_SetPowerManagementState(Hardware,
                                                gcvPOWER_ON_BROADCAST));
        break;

    case gcvBROADCAST_GPU_STUCK:
        gcmkONERROR(gckHARDWARE_GetIdle(Hardware, gcvFALSE, &idle));
        gcmkONERROR(gckOS_ReadRegister(Os, 0x00C, &axi));
        gcmkONERROR(gckOS_ReadRegister(Os, 0x664, &dma));
        gcmkPRINT("!!FATAL!! GPU Stuck");
        gcmkPRINT("  idle=0x%08X axi=0x%08X cmd=0x%08X", idle, axi, dma);

        if (Hardware->chipFeatures & (1 << 4))
        {
            gcmkONERROR(gckOS_ReadRegister(Os, 0x43C, &read0));
            gcmkONERROR(gckOS_ReadRegister(Os, 0x440, &read1));
            gcmkONERROR(gckOS_ReadRegister(Os, 0x444, &write));
            gcmkPRINT("  read0=0x%08X read1=0x%08X write=0x%08X",
                      read0, read1, write);
        }

        gcmkONERROR(gckOS_ReadRegister(Os, 0x660, &debugState));
        gcmkONERROR(gckOS_ReadRegister(Os, 0x414, &memoryDebug));
        gcmkPRINT("  debugState(0x660)=0x%08X memoryDebug(0x414)=0x%08X",
                  debugState, memoryDebug);

        gcmkONERROR(gckOS_ReadRegister(Os, 0x668, &debugCmdLow));
        gcmkONERROR(gckOS_ReadRegister(Os, 0x66C, &debugCmdHi));
        gcmkPRINT("  debugCmdLow(0x668)=0x%08X debugCmdHi(0x66C)=0x%08X",
                  debugCmdLow, debugCmdHi);

        for (i = 0; i < 16; i++)
        {
            gcmkONERROR(gckOS_WriteRegister(Os, 0x470, i << 16));
            gcmkPRINT("%d: Write 0x%08X to DebugControl0(0x470)", i, i << 16);
            
            gcmkONERROR(gckOS_ReadRegister(Os, 0x454, &debugSignalsPe));
            gcmkPRINT("%d: debugSignalsPe(0x454)=0x%08X", i, debugSignalsPe);

            gcmkPRINT("");
        }

        for (i = 0; i < 16; i++)
        {
            gcmkONERROR(gckOS_WriteRegister(Os, 0x478, i));
            gcmkPRINT("%d: Write 0x%08X to DebugControl2(0x478)", i, i);
            
            gcmkONERROR(gckOS_ReadRegister(Os, 0x468, &debugSignalsMc));
            gcmkPRINT("%d: debugSignalsMc(0x468)=0x%08X", i, debugSignalsMc);

            gcmkPRINT("");
        }


        gcmkONERROR(gckKERNEL_Recovery(Hardware->kernel));
        break;

    case gcvBROADCAST_AXI_BUS_ERROR:
        gcmkONERROR(gckHARDWARE_GetIdle(Hardware, gcvFALSE, &idle));
        gcmkONERROR(gckOS_ReadRegister(Os, 0x00C, &axi));
        gcmkONERROR(gckOS_ReadRegister(Os, 0x664, &dma));
        gcmkPRINT("!!FATAL!! AXI Bus Error");
        gcmkPRINT("  idle=0x%08X axi=0x%08X cmd=0x%08X", idle, axi, dma);

        if (Hardware->chipFeatures & (1 << 4))
        {
            gcmkONERROR(gckOS_ReadRegister(Os, 0x43C, &read0));
            gcmkONERROR(gckOS_ReadRegister(Os, 0x440, &read1));
            gcmkONERROR(gckOS_ReadRegister(Os, 0x444, &write));
            gcmkPRINT("  read0=0x%08X read1=0x%08X write=0x%08X",
                      read0, read1, write);
        }

        gcmkONERROR(gckKERNEL_Recovery(Hardware->kernel));
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
    struct semaphore *sem;

    gcmkHEADER_ARG("Os=0x%x", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Semaphore != gcvNULL);

    /* Allocate the semaphore structure. */
    gcmkONERROR(
        gckOS_Allocate(Os, gcmSIZEOF(struct semaphore), (gctPOINTER *) &sem));

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
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%x", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Semaphore != gcvNULL);

    /* Acquire the semaphore. */
    if (down_interruptible((struct semaphore *) Semaphore))
    {
        gcmkONERROR(gcvSTATUS_TIMEOUT);
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
    gcmkHEADER_ARG("Os=0x%x Semaphore=0x%x", Os, Semaphore);

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
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%x Semaphore=0x%x", Os, Semaphore);

     /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Semaphore != gcvNULL);

    /* Free the sempahore structure. */
    gcmkONERROR(gckOS_Free(Os, Semaphore));

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
    gcmkHEADER();

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(ProcessID != gcvNULL);

    /* Get process ID. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
    *ProcessID = task_tgid_vnr(current);
#else
    *ProcessID = current->tgid;
#endif

    /* Success. */
    gcmkFOOTER_ARG("*ProcessID=%u", *ProcessID);
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
    gcmkHEADER();

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(ThreadID != gcvNULL);

    /* Get thread ID. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
    *ThreadID = task_pid_vnr(current);
#else
    *ThreadID = current->pid;
#endif

    /* Success. */
    gcmkFOOTER_ARG("*ThreadID=%u", *ThreadID);
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
// dkm: modify
gceSTATUS
gckOS_SetGPUPower(
    IN gckOS Os,
    IN gctBOOL Clock,
    IN gctBOOL Power
    )
{
    //gcmkHEADER_ARG("Os=0x%x Clock=%d Power=%d", Os, Clock, Power);

    /* TODO: Put your code here. */
#if ENABLE_GPU_CLOCK_BY_DRIVER && LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)

    struct clk * clk_gpu = clk_get(NULL, "gpu");
    struct clk * clk_aclk_gpu = clk_get(NULL, "aclk_gpu");
    struct clk * clk_aclk_ddr_gpu = clk_get(NULL, "aclk_ddr_gpu");
    struct clk * clk_hclk_gpu = clk_get(NULL, "hclk_gpu");
    static int lastpower = 0;
    static int lastclock = 0;

    //printk("---------- gckOS_SetGPUPower Clock=%d Power=%d \n", Clock, Power);

    mdelay(1);
    if(lastclock!=Clock) 
    {
        if(Clock) {
            //printk("gpu: clk_enable... ");
            clk_enable(clk_gpu);
            clk_enable(clk_aclk_gpu);
            clk_enable(clk_aclk_ddr_gpu);
            clk_enable(clk_hclk_gpu);
            //printk("done!\n");
        } else {
            //printk("gpu: clk_disable... ");
            clk_disable(clk_hclk_gpu);
            clk_disable(clk_aclk_ddr_gpu);
            clk_disable(clk_aclk_gpu);
            clk_disable(clk_gpu);
            //printk("done!\n");
        }
    }
    lastclock = Clock;


    mdelay(1);
    if(lastpower!=Power)
    {
        if(Power) {
            //printk("gpu: power on... ");
            if(lastclock)    clk_disable(clk_aclk_ddr_gpu);
            mdelay(1);
            pmu_set_power_domain(PD_GPU, true);
            mdelay(1);
            if(lastclock)    clk_enable(clk_aclk_ddr_gpu);
            //printk("done!\n");
        } else {
            //printk("gpu: power off... ");
            pmu_set_power_domain(PD_GPU, false);
            //printk("done!\n");
        }
    }
    lastpower = Power;

    mdelay(1);
#endif

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

