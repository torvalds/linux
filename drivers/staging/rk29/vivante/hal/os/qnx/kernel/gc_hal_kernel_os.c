/****************************************************************************
*
*    Copyright (c) 2005 - 2010 by Vivante Corp.  All rights reserved.
*
*    The material in this file is confidential and contains trade secrets
*    of Vivante Corporation. This is proprietary information owned by
*    Vivante Corporation. No part of this work may be disclosed,
*    reproduced, copied, transmitted, or used in any way for any purpose,
*    without the express written permission of Vivante Corporation.
*
*****************************************************************************
*
*    Auto-generated file on 12/8/2010. Do not edit!!!
*
*****************************************************************************/




#include "gc_hal_kernel_qnx.h"



# include <winmgr/gpu.h>

#define USE_VMALLOC 0

#define _GC_OBJ_ZONE gcvZONE_OS

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
    gctPOINTER                  mempoolBaseAddress;
    gctPOINTER                  mempoolBasePAddress;
    gctUINT32                   mempoolPageSize;

    gctUINT32                   baseAddress;

    /* Atomic operation lock. */
    gctPOINTER                  atomicOperationLock;
};


typedef struct _gcskSIGNAL
{
    pthread_mutex_t *mutex;

    /* Manual reset flag. */
    gctBOOL manualReset;
}
gcskSIGNAL;

typedef struct _gcskSIGNAL *    gcskSIGNAL_PTR;


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
gceSTATUS gckOS_Construct(
    IN gctPOINTER Context,
    OUT gckOS * Os
    )
{
    gckOS os;
    gceSTATUS status;

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Os != gcvNULL);

    /* Allocate the gckOS object. */
    os = (gckOS) malloc(gcmSIZEOF(struct _gckOS));

    if (os == gcvNULL)
    {
        /* Out of memory. */
        return gcvSTATUS_OUT_OF_MEMORY;
    }

    /* Zero the memory. */
    memset(os, 0, gcmSIZEOF(struct _gckOS));

    /* Initialize the gckOS object. */
    os->object.type = gcvOBJ_OS;

    /* Set device device. */
    os->device = Context;

    /* IMPORTANT! No heap yet. */
    os->heap = gcvNULL;

    /* Initialize the memory lock. */
    gcmkONERROR(
        gckOS_CreateMutex(os, &os->memoryLock));

    gcmkONERROR(
        gckOS_CreateMutex(os, &os->memoryMapLock));

    /* Create the gckHEAP object. */
    gcmkONERROR(
        gckHEAP_Construct(os, gcdHEAP_SIZE, &os->heap));

    /* Find the base address of the physical memory. */
    os->baseAddress = os->device->baseAddress;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_OS,
                  "Physical base address set to 0x%08X.\n",
                  os->baseAddress);

    os->mempoolBaseAddress = (gctPOINTER) drv_mempool_get_baseAddress();
    os->mempoolBasePAddress = (gctPOINTER) drv_mempool_get_basePAddress();
    os->mempoolPageSize = drv_mempool_get_page_size();


    /* Initialize the atomic operations lock. */
    gcmkONERROR(
        gckOS_CreateMutex(os, &os->atomicOperationLock));

    /* Return pointer to the gckOS object. */
    *Os = os;

    /* Success. */
    return gcvSTATUS_OK;

OnError:
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

    if (os->atomicOperationLock != gcvNULL)
    {
        gcmkVERIFY_OK(
            gckOS_DeleteMutex(os, os->atomicOperationLock));
    }

    free(os);

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
    gckHEAP heap;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    if (Os->heap != NULL)
    {
        /* Mark gckHEAP as gone. */
        heap     = Os->heap;
        Os->heap = NULL;

        /* Destroy the gckHEAP object. */
        gcmkVERIFY_OK(
            gckHEAP_Destroy(heap));
    }

    /* Destroy the memory lock. */
    gcmkVERIFY_OK(
        gckOS_DeleteMutex(Os, Os->memoryMapLock));

    gcmkVERIFY_OK(
        gckOS_DeleteMutex(Os, Os->memoryLock));

    /* Destroy the atomic operation lock. */
    gcmkVERIFY_OK(
        gckOS_DeleteMutex(Os, Os->atomicOperationLock));

    /* Mark the gckOS object as unknown. */
    Os->object.type = gcvOBJ_UNKNOWN;

    /* Free the gckOS object. */
    free(Os);

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

    /*gcmkHEADER_ARG("Os=0x%x Bytes=%lu", Os, Bytes);*/

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
    /*gcmkFOOTER_ARG("*memory=0x%x", *Memory);*/
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

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Memory != NULL);

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

    gcmkHEADER_ARG("Os=0x%x Bytes=%lu", Os, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Memory != NULL);

    memory = (gctPOINTER) calloc(1, Bytes);

    if (memory == NULL)
    {
        /* Out of memory. */
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    /* Return pointer to the memory allocation. */
    *Memory = memory;

    /* Success. */
    gcmkFOOTER_ARG("*Memory=0x%x", *Memory);
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
    gcmkHEADER_ARG("Memory=0x%x", Memory);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Memory != NULL);

    /* Free the memory from the OS pool. */
    free(Memory);

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
gceSTATUS gckOS_MapMemory(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Logical
    )
{
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != 0);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Logical != NULL);

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "Enter gckOS_MapMemory\n");

    MEMORY_LOCK(Os);

    /* Map physical address. */
    *Logical = mmap64(0,
        Bytes,
        PROT_READ | PROT_WRITE,
        MAP_PHYS | MAP_SHARED,
        NOFD,
        (off_t)Physical);

    MEMORY_UNLOCK(Os);

    if (*Logical == MAP_FAILED)
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR,
            gcvZONE_OS,
            "gckOS_MapMemory: mmap error: %s\n",
            strerror(errno));

        return gcvSTATUS_OUT_OF_MEMORY;
    }

    gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_OS,
                "gckOS_MapMemory: User Mapped address for 0x%x is 0x%x\n",
                (gctUINT32)Physical,
                (gctUINT32)*Logical);

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
gceSTATUS gckOS_UnmapMemory(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    IN gctPOINTER Logical
    )
{
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != 0);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Logical != NULL);

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "in gckOS_UnmapMemory\n");

    if (Logical)
    {
        gctUINT32 res;
        gcmkTRACE_ZONE(gcvLEVEL_VERBOSE,
            gcvZONE_OS,
            "[gckOS_UnmapMemory] Logical: 0x%x\n",
            Logical
            );

        MEMORY_LOCK(Os);

        res = munmap(Logical, Bytes);

        MEMORY_UNLOCK(Os);

        if (res == -1)
        {
            gcmkTRACE_ZONE(gcvLEVEL_ERROR,
                gcvZONE_OS,
                "gckOS_UnmapMemory: munmap error: %s\n",
                strerror(errno));

            return gcvSTATUS_INVALID_ARGUMENT;
        }
    }

    /* Success. */
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
gceSTATUS gckOS_AllocateNonPagedMemory(
    IN gckOS Os,
    IN gctBOOL InUserSpace,
    IN OUT gctSIZE_T * Bytes,
    OUT gctPHYS_ADDR * Physical,
    OUT gctPOINTER * Logical
    )
{

    if (InUserSpace)
    {
        /* TODO: Make a separate OS call for allocating from shared memory pool. */
        *Logical = drv_shmpool_alloc_contiguous((gctUINT32)Physical, (gctHANDLE)Logical, *Bytes);

        if (*Logical == gcvNULL)
        {
            gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "gckOS_AllocateNonPagedMemory: Out of memory.");

            *Bytes = 0;
            return gcvSTATUS_OUT_OF_RESOURCES;
        }

        /* Used to distinguish from memory allocated in kernel space. */
        *((gctUINT32*)Physical) = 0;
    }
    else
    {
        drv_mempool_alloc_contiguous(*Bytes, Physical, Logical);

        if (*Physical == gcvNULL || *Logical == gcvNULL)
        {
            gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "gckOS_AllocateNonPagedMemory: Out of memory.");

            *Bytes = 0;
            return gcvSTATUS_OUT_OF_RESOURCES;
        }
    }

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "gckOS_AllocateNonPagedMemory: "
                "Bytes->0x%x, Logical->0x%x Physical->0x%x\n",
                (gctUINT32)*Bytes,
                *Logical,
                *Physical);

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
    int rc;

    if (Physical)
    {
        rc = drv_mempool_free(Logical);
    }
    else
    {
        rc = drv_shmpool_free(Logical);
    }

    if (rc == -1)
    {
        gcmkTRACE_ZONE(gcvLEVEL_INFO,
                    gcvZONE_OS,
                    "gckOS_FreeNonPagedMemory: "
                    "Unmap Failed Logical->0x%x, Bytes->%d, Physical->0x%x\n",
                    (gctUINT32)Logical,
                    Bytes,
                    (gctUINT32)Physical);
        return gcvSTATUS_INVALID_ARGUMENT;
    }

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "gckOS_FreeNonPagedMemory: "
                "Logical->0x%x Physical->0x%x\n",
                (gctUINT32)Logical,
                (gctUINT32)Physical);

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

    *Data = (gctUINT32)in32((uintptr_t) ((gctUINT8 *)Os->device->registerBase + Address));

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
    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "gckOS_WriteRegister: "
                "Writing to physical address [%x] = %x\n",
                (gctUINT8 *)Os->device->registerBase,
                Data);

    out32((uintptr_t) ((gctUINT8 *)Os->device->registerBase + Address), (uint32_t)Data);

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
    *PageSize = (gctSIZE_T) __PAGESIZE;

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
**          Pointer to a variable that receives the 32-bit physical adress.
*/
gceSTATUS gckOS_GetPhysicalAddress(
    IN gckOS Os,
    IN gctPOINTER Logical,
    OUT gctUINT32 * Address
    )
{

    gctUINT32 res;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    if ( drv_mempool_mem_offset(Logical, Address) != gcvSTATUS_OK)
    {
        if ( drv_shmpool_mem_offset(Logical, Address) != gcvSTATUS_OK)
        {
            printf("Warning, using mem_offset for Logical:%x!\n", (gctUINT32) Logical);

            MEMORY_LOCK(Os);

            /* TODO: mem_offset in QNX works only for memory that is allocated
               contiguosly using gckOS_AllocateContiguous(). */
            res = mem_offset( Logical, NOFD, 1, (off_t *)Address, NULL);

            if (res == -1)
            {
                MEMORY_UNLOCK(Os);

                gcmkTRACE_ZONE(gcvLEVEL_INFO,
                            gcvZONE_OS,
                            "gckOS_GetPhysicalAddress: "
                            "Unable to get physical address for 0x%x\n",
                            (gctUINT32)Logical);

                return gcvSTATUS_INVALID_ARGUMENT;
            }

            MEMORY_UNLOCK(Os);
        }
    }

    /* Subtract base address to get a GPU physical address. */
    gcmASSERT(*Address >= Os->baseAddress);
    *Address -= Os->baseAddress;

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                    gcvZONE_OS,
                    "gckOS_GetPhysicalAddress: Logical->0x%x Physical->0x%x\n",
                    (gctUINT32)Logical,
                    (gctUINT32)*Address);

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
    gctUINT32 physical;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

    MEMORY_LOCK(Os);

    /* Compute true physical address (before subtraction of the baseAddress). */
    physical = Physical + Os->baseAddress;

    /* Map physical address. */
    *Logical = mmap64(0,
        Bytes,
        PROT_READ | PROT_WRITE,
        MAP_PHYS | MAP_SHARED | MAP_NOINIT,
        NOFD,
        (off_t)physical);

    MEMORY_UNLOCK(Os);

    if (*Logical == MAP_FAILED)
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR,
            gcvZONE_OS,
            "gckOS_MapMemory: mmap error: %s\n",
            strerror(errno));

        return gcvSTATUS_OUT_OF_MEMORY;
    }

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_OS,
                  "gckOS_MapPhysical: "
                  "Physical->0x%X Bytes->0x%X Logical->0x%X\n",
                  (gctUINT32) Physical,
                  (gctUINT32) Bytes,
                  (gctUINT32) *Logical);

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
    gctUINT32 res;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Logical != NULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

    MEMORY_LOCK(Os);

    res = munmap(Logical, Bytes);

    MEMORY_UNLOCK(Os);

    if (res == -1)
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR,
            gcvZONE_OS,
            "gckOS_UnmapMemory: munmap error: %s\n",
            strerror(errno));

        return gcvSTATUS_INVALID_ARGUMENT;
    }

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                    gcvZONE_OS,
                    "gckOS_UnmapPhysical: "
                    "Logical->0x%x Bytes->0x%x\n",
                    (gctUINT32)Logical,
                    (gctUINT32)Bytes);

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
    gctUINT32 res;

    /* Validate the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Mutex != NULL);

    /* Allocate a FAST_MUTEX structure. */
    *Mutex = (gctPOINTER) malloc(sizeof(pthread_mutex_t));

    if (*Mutex == gcvNULL)
    {
        return gcvSTATUS_OUT_OF_MEMORY;
    }

    /* Initialize the semaphore.. Come up in unlocked state. */
    res = pthread_mutex_init((pthread_mutex_t *)(*Mutex), NULL);
    if (res != EOK)
    {
        return gcvSTATUS_OUT_OF_RESOURCES;
    }

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
    gctUINT32 res;

    /* Validate the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Mutex != NULL);

    res = pthread_mutex_destroy((pthread_mutex_t *)(Mutex));

    if (res != EOK)
    {
        return gcvSTATUS_INVALID_ARGUMENT;
    }

    /* Delete the fast mutex. */
    free(Mutex);

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
    gcmkVERIFY_ARGUMENT(Mutex != gcvNULL);

    if (Timeout == gcvINFINITE)
    {
        pthread_mutex_lock((pthread_mutex_t *) Mutex);

        /* Success. */
        return gcvSTATUS_OK;
    }

    while (Timeout-- > 0)
    {
        /* Try to acquire the fast mutex. */
        if (!pthread_mutex_trylock((pthread_mutex_t *) Mutex))
        {
            /* Success. */
            return gcvSTATUS_OK;
        }

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
    pthread_mutex_unlock((pthread_mutex_t *) Mutex);

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

    /* Acquire atomic operation lock. */
    gcmkVERIFY_OK(gckOS_AcquireMutex(Os,
                                    Os->atomicOperationLock,
                                    gcvINFINITE));

    /* Exchange the pair of 32-bit values. */
    *OldValue = *Target;
    *Target = NewValue;

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Os, Os->atomicOperationLock));

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

    /* Acquire atomic operation lock. */
    gcmkVERIFY_OK(gckOS_AcquireMutex(Os,
                                    Os->atomicOperationLock,
                                    gcvINFINITE));

    /* Exchange the pair of pointers. */
    *OldValue = *Target;
    *Target = NewValue;

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Os, Os->atomicOperationLock));

    /* Success. */
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
gceSTATUS gckOS_Delay(
    IN gckOS Os,
    IN gctUINT32 Delay
    )
{
    /* Schedule delay. */
    delay(Delay);

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

    __cpu_membarrier();

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
    int rc, fd, shm_ctl_flags = SHMCTL_ANON | SHMCTL_LAZYWRITE;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Physical != NULL);

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "in gckOS_AllocatePagedMemory\n");

    if (Contiguous)
    {
        shm_ctl_flags |= SHMCTL_PHYS;
    }

    /* Lock down, to avoid opening same shm file twice. */
    MEMORY_LOCK(Os);

    fd = shm_open("shm_gal", O_RDWR | O_CREAT, 0777);
    if (fd == -1) {
        printf("shm_open failed. error %s\n", strerror( errno ) );
        return gcvSTATUS_GENERIC_IO;
    }

    /* Free to use the same name for next create shm object after shm_unlink. */
    shm_unlink("shm_gal");

    MEMORY_UNLOCK(Os);

    /* Special flags for this shm, to make it write buffered. */
    /* Virtual memory doesn't need to be physically contiguous. */
    /* Allocations would be page aligned. */
    rc = shm_ctl_special(fd,
                         SHMCTL_ANON /*| SHMCTL_PHYS*/ | SHMCTL_LAZYWRITE,
                         0,
                         Bytes,
                         0x9);
    if (rc == -1) {
        printf("shm_ctl_special failed. error %s\n", strerror( errno ) );
        close(fd);
        return gcvSTATUS_OUT_OF_MEMORY;
    }

    /* Use the fd as the handle for the physical memory just allocated. */
    *Physical = (gctPHYS_ADDR) fd;

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "gckOS_AllocatePagedMemory: "
                "Bytes->0x%x, Physical->0x%x\n",
                (gctUINT32)Bytes,
                (gctUINT32)*Physical);

    /* Success. */
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
    int rc;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != NULL);

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "in gckOS_FreePagedMemory\n");

    rc = close((gctINT)Physical);
    if ( rc == -1 )
    {
        printf("gckOS_FreePagedMemory failed. error: %s\n", strerror( errno ) );
        return gcvSTATUS_GENERIC_IO;
    }

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
**          the page table.
*/
gceSTATUS gckOS_LockPages(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    IN gctUINT32 Pid,
    OUT gctPOINTER * Logical,
    OUT gctSIZE_T * PageCount
    )
{
    void* addr;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != NULL);
    gcmkVERIFY_ARGUMENT(Logical != NULL);
    gcmkVERIFY_ARGUMENT(PageCount != NULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "in gckOS_LockPages\n");

    /* Map this memory inside user and galcore. */
    addr = mmap64_join(Pid,
                       0,
                       Bytes,
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED,
                       (int)Physical,
                       0);

    if (addr == MAP_FAILED)
    {
        printf("gckOS_LockPages: couldn't map memory of size %d, Pid: %x [errno %s]",
                (gctUINT32)Bytes, Pid, strerror( errno ) );
        return gcvSTATUS_GENERIC_IO;
    }

    /* TODO: MLOCK may or may not be needed!. */
    mlock((void*)addr, Bytes);

    *Logical = (gctPOINTER)addr;
    *PageCount = (gcmALIGN(Bytes, __PAGESIZE)) / __PAGESIZE;

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "gckOS_LockPages: "
                "gctPHYS_ADDR->0x%x Bytes->0x%x Logical->0x%x pid->%d\n",
                (gctUINT32)Physical,
                (gctUINT32)Bytes,
                (gctUINT32)*Logical,
                Pid);

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
    IN gctPOINTER Logical,
    IN gctSIZE_T PageCount,
    IN gctPOINTER PageTable
    )
{
    gctUINT32* table;
    gctPOINTER addr;
    size_t contigLen = 0;
    off_t offset;
    gctUINT32 bytes;
    int rc;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != NULL);
    gcmkVERIFY_ARGUMENT(PageCount > 0);
    gcmkVERIFY_ARGUMENT(PageTable != NULL);

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "in gckOS_MapPages\n");

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "gckOS_MapPages: "
                "Physical->0x%x PageCount->0x%x Logical->0x%x\n",
                (gctUINT32)Physical,
                (gctUINT32)PageCount,
                (gctUINT32)Logical);

    addr = Logical;
    table = (gctUINT32 *)PageTable;
    bytes = PageCount * __PAGESIZE;

    /* Try to get the user pages so DMA can happen. */
    while (PageCount > 0)
    {
        /* fd should be NOFD here, to get physical address. */
        rc = mem_offset( addr, NOFD, bytes, &offset, &contigLen);
        if (rc == -1) {
            printf("gckOS_MapPages: mem_offset failed: %s\n", strerror( errno ) );
            return gcvSTATUS_GENERIC_IO;
        }

        gcmASSERT(contigLen > 0);

        while(contigLen > 0)
        {
            *table++ = (gctUINT32) offset;
            offset += 4096;
            addr += 4096;
            contigLen -= 4096;
            bytes -= 4096;
            PageCount--;
        }
    }

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
    IN gctUINT32 Pid,
    IN gctSIZE_T Bytes,
    IN gctPOINTER Logical
    )
{
    int rc = 0;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != NULL);
    gcmkVERIFY_ARGUMENT(Logical != NULL);

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "in gckOS_UnlockPages\n");

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "gckOS_MapPages: "
                "Physical->0x%x Bytes->0x%x Logical->0x%x Pid->0x%x\n",
                (gctUINT32)Physical,
                (gctUINT32)Bytes,
                (gctUINT32)Logical,
                (gctUINT32)Pid);

    rc = munmap((void*)Logical, Bytes);
    if (rc == -1) {
        printf("munmap failed: %s\n", strerror( errno ) );
        return gcvSTATUS_GENERIC_IO;
    }

    rc = munmap_peer(Pid, (void*)Logical, Bytes);
    if (rc == -1) {
        printf("munmap_peer failed: %s\n", strerror( errno ) );
        return gcvSTATUS_GENERIC_IO;
    }

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
**  Return the kernel logical pointer that corresponds to the specified
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
    /* A pointer is assumed to be allocated from its shared memory object.
       Which is mapped by both user and kernel at the same vitual address. */
    /* TODO: Check if Pointer is a valid pointer? */
    *KernelPointer = Pointer;

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
    /* Nothing to unmap. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_MapUserPhysical
**
**  Map a physical address from the user process into the kernel address space.
**  The physical address should be obtained by user from gckOS_AllocateNonPagedMemory.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      IN gctPHYS_ADDR Phys,
**          Physical address of memory that needs to be mapped.
**
**  OUTPUT:
**
**      gctPHYS_ADDR * KernelPointer
**          Pointer to a variable receiving the mapped pointer in kernel address
**          space.
*/
gceSTATUS
gckOS_MapUserPhysical(
    IN gckOS Os,
    IN gctPHYS_ADDR Phys,
    OUT gctPHYS_ADDR * KernelPointer
    )
{
    /* A gctPHYS_ADDR is assumed to be allocated from physical memory pool. */
    /* Dont call this function for pointers already in kernel space. */
    printf("ERROR: %s Not supported.\n", __FUNCTION__);
    *KernelPointer = (gctPHYS_ADDR)0xDEADBEEF;

    return gcvSTATUS_NOT_SUPPORTED;
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
    *(gctUINT32 *)Address = Data;

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
**          Nonsignaled state in QNX is mutex acquired (not free).
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
    gcskSIGNAL_PTR signal;
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%x ManualReset=0x%x", Os, ManualReset);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Signal != NULL);

    /* Create an event structure. */
    signal = (gcskSIGNAL_PTR) malloc(sizeof(gcskSIGNAL));

    if (signal == gcvNULL)
    {
        gcmFOOTER_NO();
        return gcvSTATUS_OUT_OF_MEMORY;
    }

    signal->manualReset = ManualReset;

    status = gckOS_CreateMutex(Os, (gctPOINTER *)(&signal->mutex));

    if (gcmIS_ERROR(status))
    {
        /* Error. */
        free(signal);
        gcmFOOTER();
        return status;
    }

    *Signal = (gctSIGNAL) signal;

    /* Success. */
    gcmkFOOTER_ARG("Os=0x%x Signal=0x%x", Os, Signal);
    return gcvSTATUS_OK;

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

    gceSTATUS status = gcvSTATUS_OK;
    gcskSIGNAL_PTR signal;

    gcmkHEADER_ARG("Os=0x%x Signal=0x%x", Os, Signal);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Signal != NULL);

    signal = (gcskSIGNAL_PTR) Signal;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Signal != NULL);

    if (signal != gcvNULL )
    {
        status = gckOS_DeleteMutex(Os, (gctPOINTER)(signal->mutex));

        free(signal);
    }

    /* Success. */
    gcmkFOOTER_NO();
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
    gcskSIGNAL_PTR signal;
    gceSTATUS status = gcvSTATUS_OK;

    gcmkHEADER_ARG("Os=0x%x Signal=0x%x State=%d", Os, Signal, State);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Signal != gcvNULL);

    signal = (gcskSIGNAL_PTR) Signal;

    /* Set the new state of the event. */
    if (signal->manualReset && State)
    {
        /* Set the event to a signaled state. */
        gckOS_ReleaseMutex(Os,(gctPOINTER *)(&signal->mutex));
    }
    else
    {
        gckOS_AcquireMutex(Os, (gctPOINTER *)(&signal->mutex), gcvINFINITE);
    }

    /* Success. */
    gcmkFOOTER_NO();
    return status;
}

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
    IN gctINT Rcvid,
    IN gctINT Coid
    )
{
    gctINT rc;
    struct sigevent event;

    SIGEV_PULSE_INIT( &event, Coid, SIGEV_PULSE_PRIO_INHERIT, _PULSE_CODE_MINAVAIL, Signal);

    rc = MsgDeliverEvent_r(Rcvid, &event);
    if (rc != EOK)
    {
        gcmkTRACE(gcvLEVEL_INFO,
                 "%s(%d): Sent signal to (receive ID = %d, connect ID = %d).",
                 __FUNCTION__, __LINE__, Rcvid, Coid);

        gcmkTRACE(gcvLEVEL_ERROR,
                 "%s(%d): MsgDeliverEvent failed (%d).",
                 __FUNCTION__, __LINE__, rc);

;

        return gcvSTATUS_GENERIC_IO;
    }

    /* Return status. */
    return gcvSTATUS_OK;
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
    gceSTATUS status = gcvSTATUS_OK;

    gcskSIGNAL_PTR signal;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Signal != gcvNULL);

    signal = (gcskSIGNAL_PTR) Signal;

    status = gckOS_AcquireMutex(Os, (gctPOINTER *)(&signal->mutex), Wait);

    /* If manualReset is true, use gckOS_Signal to acquire mutex again. */
    if (signal->manualReset)
    {
        status = gckOS_ReleaseMutex(Os, (gctPOINTER *)(&signal->mutex));
    }

    /* Return status. */
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
    printf("ERROR: %s Not supported.\n", __FUNCTION__);
    return gcvSTATUS_NOT_SUPPORTED;
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
    IN gctPOINTER Memory,
    IN gctSIZE_T Size,
    OUT gctPOINTER * Info,
    OUT gctUINT32_PTR Address
    )
{
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);
    gcmkVERIFY_ARGUMENT(Size > 0);
    gcmkVERIFY_ARGUMENT(Info != gcvNULL);
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE,
        gcvZONE_OS,
        "[gckOS_MapUserMemory] enter.\n"
        );

    printf("ERROR: %s Not supported.\n", __FUNCTION__);

    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE,
        gcvZONE_OS,
        "[gckOS_MapUserMemory] leave.\n"
        );

    /* Return the status. */
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
    IN gctPOINTER Memory,
    IN gctSIZE_T Size,
    IN gctPOINTER Info,
    IN gctUINT32 Address
    )
{
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);
    gcmkVERIFY_ARGUMENT(Size > 0);
    gcmkVERIFY_ARGUMENT(Info != gcvNULL);

    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE,
        gcvZONE_OS,
        "[gckOS_UnmapUserMemory] enter.\n"
        );

    printf("ERROR: %s Not supported.\n", __FUNCTION__);

    /* Return the status. */
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

    InterruptLock(&Os->device->isrLock);

    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_ResumeInterrupt(
    IN gckOS Os
    )
{
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    InterruptUnlock(&Os->device->isrLock);

    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_NotifyIdle(
    IN gckOS Os,
    IN gctBOOL Idle
    )
{
    /* TODO */
    return gcvSTATUS_OK;
}

/* Perform a memory copy. */
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
gckOS_AllocateNonPagedMemoryShmPool(
    IN gckOS Os,
    IN gctBOOL InUserSpace,
    IN gctUINT32 Pid,
    IN gctHANDLE Handle,
    IN OUT gctSIZE_T * Bytes,
    OUT gctPHYS_ADDR * Physical,
    OUT gctPOINTER * Logical
    )
{

    if (InUserSpace)
    {
        *Logical = drv_shmpool_alloc_contiguous(Pid, Handle, *Bytes);

        if (*Logical == gcvNULL)
        {
            gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "gckOS_AllocateNonPagedMemory: Out of memory.");

            *Bytes = 0;
            return gcvSTATUS_OUT_OF_RESOURCES;
        }

        /* Used to distinguish from memory allocated in kernel space. */
        *((gctUINT32*)Physical) = 0;
    }
    else
    {
        return gcvSTATUS_INVALID_ARGUMENT;
    }

    gcmkTRACE_ZONE(gcvLEVEL_INFO,
                gcvZONE_OS,
                "gckOS_AllocateNonPagedMemoryShmPool: "
                "Bytes->0x%x, Logical->0x%x Physical->0x%x\n",
                (gctUINT32)*Bytes,
                *Logical,
                *Physical);

    /* Success. */
    return gcvSTATUS_OK;
}

int
memmgr_peer_sendnc(pid_t pid, int coid, void *smsg, size_t sbytes, void *rmsg, size_t rbytes )
{
    mem_peer_t  peer;
    iov_t       siov[2];
    int         rc;

    peer.i.type = _MEM_PEER;
    peer.i.peer_msg_len = sizeof(peer);
    peer.i.pid = pid;

    SETIOV(siov + 0, &peer, sizeof peer);
    SETIOV(siov + 1, smsg, sbytes);

    do {
        rc = MsgSendvsnc(coid, siov, 2, rmsg, rbytes);
    } while (rc == -1 && errno == EINTR);

    return rc;
}

void *
_mmap2_peer(pid_t pid, void *addr, size_t len, int prot, int flags, int fd, off64_t off,
        unsigned align, unsigned pre_load, void **base, size_t *size) {
    mem_map_t msg;

    msg.i.type = _MEM_MAP;
    msg.i.zero = 0;
    msg.i.addr = (uintptr_t)addr;
    msg.i.len = len;
    msg.i.prot = prot;
    msg.i.flags = flags;
    msg.i.fd = fd;
    msg.i.offset = off;
    msg.i.align = align;
    msg.i.preload = pre_load;
    msg.i.reserved1 = 0;
    if (memmgr_peer_sendnc(pid, MEMMGR_COID, &msg.i, sizeof msg.i, &msg.o, sizeof msg.o) == -1) {
        return MAP_FAILED;
    }
    if (base) {
        *base = (void *)(uintptr_t)msg.o.real_addr;
    }
    if (size) {
        *size = msg.o.real_size;
    }
    return (void *)(uintptr_t)msg.o.addr;
}

void *
mmap64_peer(pid_t pid, void *addr, size_t len, int prot, int flags, int fd, off64_t off) {
    return _mmap2_peer(pid, addr, len, prot, flags, fd, off, 0, 0, 0, 0);
}

int
munmap_flags_peer(pid_t pid, void *addr, size_t len, unsigned flags) {
    mem_ctrl_t msg;

    msg.i.type = _MEM_CTRL;
    msg.i.subtype = _MEM_CTRL_UNMAP;
    msg.i.addr = (uintptr_t)addr;
    msg.i.len = len;
    msg.i.flags = flags;
    return memmgr_peer_sendnc(pid, MEMMGR_COID, &msg.i, sizeof msg.i, 0, 0);
}

int
munmap_peer(pid_t pid, void *addr, size_t len) {
    return munmap_flags_peer(pid, addr, len, 0);
}

int
mem_offset64_peer(pid_t pid, const uintptr_t addr, size_t len,
                off64_t *offset, size_t *contig_len) {
    int rc;

    struct _peer_mem_off {
        struct _mem_peer peer;
        struct _mem_offset msg;
    };
    typedef union {
        struct _peer_mem_off i;
        struct _mem_offset_reply o;
    } memoffset_peer_t;
    memoffset_peer_t msg;

    msg.i.peer.type = _MEM_PEER;
    msg.i.peer.peer_msg_len = sizeof(msg.i.peer);
    msg.i.peer.pid = pid;
    msg.i.peer.reserved1 = 0;

    msg.i.msg.type = _MEM_OFFSET;
    msg.i.msg.subtype = _MEM_OFFSET_PHYS;
    msg.i.msg.addr = addr;
    msg.i.msg.reserved = -1;
    msg.i.msg.len = len;

    do {
        rc = MsgSendnc(MEMMGR_COID, &msg.i, sizeof msg.i, &msg.o, sizeof msg.o);
    } while (rc == -1 && errno == EINTR);

    if (rc == -1) {
        return -1;
    }

    *offset = msg.o.offset;
    *contig_len = msg.o.size;

    return 0;
}

#if defined(__X86__)
#define CPU_VADDR_SERVER_HINT 0x30000000u
#elif defined(__ARM__)
#define CPU_VADDR_SERVER_HINT 0x20000000u
#else
#error NO CPU SOUP FOR YOU!
#endif

/*
 * map the object into both client and server at the same virtual address
 */
void *
mmap64_join(pid_t pid, void *addr, size_t len, int prot, int flags, int fd, off64_t off) {
    void *svaddr, *cvaddr = MAP_FAILED;
    uintptr_t hint = (uintptr_t)addr;
    uintptr_t start_hint = hint;

    if ( hint == (uintptr_t)0 )
    {
        hint = (uintptr_t)CPU_VADDR_SERVER_HINT;
    }

    do {
        svaddr = mmap64( (void *)hint, len, prot, flags, fd, off );
        if ( svaddr == MAP_FAILED ) {
            break;
        }
        if ( svaddr == cvaddr ) {
            return svaddr;
        }

        cvaddr = mmap64_peer( pid, svaddr, len, prot, MAP_FIXED | flags, fd, off );
        if ( cvaddr == MAP_FAILED ) {
            break;
        }
        if ( svaddr == cvaddr ) {
            return svaddr;
        }

        if ( munmap( svaddr, len ) == -1 ) {
            svaddr = MAP_FAILED;
            break;
        }

        svaddr = mmap64( cvaddr, len, prot, flags, fd, off );
        if ( svaddr == MAP_FAILED ) {
            break;
        }
        if ( svaddr == cvaddr ) {
            return svaddr;
        }

        if ( munmap( svaddr, len ) == -1 ) {
            svaddr = MAP_FAILED;
            break;
        }
        if ( munmap_peer( pid, cvaddr, len ) == -1 ) {
            cvaddr = MAP_FAILED;
            break;
        }
        hint += __PAGESIZE;

    } while(hint != start_hint); /* do we really want to wrap all the way */

    if ( svaddr != MAP_FAILED ) {
        munmap( svaddr, len );
    }
    if ( cvaddr != MAP_FAILED ) {
        munmap_peer( pid, cvaddr, len );
    }

    return MAP_FAILED;
}

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
    return gcvSTATUS_OK;
}

/*******************************************************************************
** Broadcast interface.
*/

gceSTATUS
gckOS_Broadcast(
    IN gckOS Os,
    IN gckHARDWARE Hardware,
    IN gceBROADCAST Reason
    )
{
    return gcvSTATUS_OK;
}

