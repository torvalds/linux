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


#include <linux/string.h>
#include "gc_hal_kernel_precomp.h"

#define _GC_OBJ_ZONE    gcvZONE_KERNEL

/******************************************************************************\
******************************* gckKERNEL API Code ******************************
\******************************************************************************/

/*******************************************************************************
**
**  gckKERNEL_Construct
**
**  Construct a new gckKERNEL object.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      IN gctPOINTER Context
**          Pointer to a driver defined context.
**
**  OUTPUT:
**
**      gckKERNEL * Kernel
**          Pointer to a variable that will hold the pointer to the gckKERNEL
**          object.
*/

#ifdef ANDROID
#if gcdNEW_PROFILER_FILE
#define DEFAULT_PROFILE_FILE_NAME   "/sdcard/vprofiler.vpd"
#else
#define DEFAULT_PROFILE_FILE_NAME   "/sdcard/vprofiler.xml"
#endif
#else
#if gcdNEW_PROFILER_FILE
#define DEFAULT_PROFILE_FILE_NAME   "vprofiler.vpd"		/* "vpd" means "vprofile data" */
#else
#define DEFAULT_PROFILE_FILE_NAME   "vprofiler.xml"
#endif
#endif

gceSTATUS
gckKERNEL_Construct(
    IN gckOS Os,
    IN gctPOINTER Context,
    OUT gckKERNEL * Kernel
    )
{
    gckKERNEL kernel = gcvNULL;
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%x Context=0x%x", Os, Context);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Kernel != gcvNULL);

    /* Allocate the gckKERNEL object. */
    gcmkONERROR(
        gckOS_Allocate(Os,
                       gcmSIZEOF(struct _gckKERNEL),
                       (gctPOINTER *) &kernel));

    /* Zero the object pointers. */
    kernel->hardware = gcvNULL;
    kernel->command  = gcvNULL;
    kernel->event    = gcvNULL;
    kernel->mmu      = gcvNULL;

    /* Initialize the gckKERNEL object. */
    kernel->object.type = gcvOBJ_KERNEL;
    kernel->os          = Os;

    /* Save context. */
    kernel->context = Context;

    /* Construct atom holding number of clients. */
    kernel->atomClients = gcvNULL;
    gcmkONERROR(gckOS_AtomConstruct(Os, &kernel->atomClients));

#if gcdSECURE_USER
    kernel->cacheSlots     = 0;
    kernel->cacheTimeStamp = 0;
#endif

    /* Construct the gckHARDWARE object. */
    gcmkONERROR(
        gckHARDWARE_Construct(Os, &kernel->hardware));

    /* Set pointer to gckKERNEL object in gckHARDWARE object. */
    kernel->hardware->kernel = kernel;

    /* Initialize the hardware. */
    gcmkONERROR(
        gckHARDWARE_InitializeHardware(kernel->hardware));

    /* Construct the gckCOMMAND object. */
    gcmkONERROR(
        gckCOMMAND_Construct(kernel, &kernel->command));

    /* Construct the gckEVENT object. */
    gcmkONERROR(
        gckEVENT_Construct(kernel, &kernel->event));

    /* Construct the gckMMU object. */
    gcmkONERROR(
        gckMMU_Construct(kernel, gcdMMU_SIZE, &kernel->mmu));

#if VIVANTE_PROFILER
    /* Initialize profile setting */
#if defined ANDROID
    kernel->profileEnable = gcvFALSE;
#else
    kernel->profileEnable = gcvTRUE;
#endif

    gcmkVERIFY_OK(
        gckOS_MemCopy(kernel->profileFileName,
                      DEFAULT_PROFILE_FILE_NAME,
                      gcmSIZEOF(DEFAULT_PROFILE_FILE_NAME) + 1));
#endif

    /* Return pointer to the gckKERNEL object. */
    *Kernel = kernel;

    /* Success. */
    gcmkFOOTER_ARG("*Kernel=0x%x", *Kernel);
    return gcvSTATUS_OK;

OnError:
    if (kernel != gcvNULL)
    {
        if (kernel->event != gcvNULL)
        {
            gcmkVERIFY_OK(gckEVENT_Destroy(kernel->event));
        }

        if (kernel->command != gcvNULL)
        {
        gcmkVERIFY_OK(gckCOMMAND_Destroy(kernel->command));
        }

        if (kernel->hardware != gcvNULL)
        {
            gcmkVERIFY_OK(gckHARDWARE_Destroy(kernel->hardware));
        }

        if (kernel->atomClients != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_AtomDestroy(Os, kernel->atomClients));
        }

        gcmkVERIFY_OK(gckOS_Free(Os, kernel));
    }

    /* Return the error. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckKERNEL_Destroy
**
**  Destroy an gckKERNEL object.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object to destroy.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckKERNEL_Destroy(
    IN gckKERNEL Kernel
    )
{
    gcmkHEADER_ARG("Kernel=0x%x", Kernel);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);

    /* Destroy the gckMMU object. */
    gcmkVERIFY_OK(gckMMU_Destroy(Kernel->mmu));

    /* Destroy the gckEVENT object. */
    gcmkVERIFY_OK(gckEVENT_Destroy(Kernel->event));

    /* Destroy the gckCOMMNAND object. */
    gcmkVERIFY_OK(gckCOMMAND_Destroy(Kernel->command));

    /* Destroy the gckHARDWARE object. */
    gcmkVERIFY_OK(gckHARDWARE_Destroy(Kernel->hardware));

    /* Detsroy the client atom. */
    gcmkVERIFY_OK(gckOS_AtomDestroy(Kernel->os, Kernel->atomClients));

    /* Mark the gckKERNEL object as unknown. */
    Kernel->object.type = gcvOBJ_UNKNOWN;

    /* Free the gckKERNEL object. */
    gcmkVERIFY_OK(gckOS_Free(Kernel->os, Kernel));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  _AllocateMemory
**
**  Private function to walk all required memory pools to allocate the requested
**  amount of video memory.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      gcsHAL_INTERFACE * Interface
**          Pointer to a gcsHAL_INTERFACE structure that defines the command to
**          be dispatched.
**
**  OUTPUT:
**
**      gcsHAL_INTERFACE * Interface
**          Pointer to a gcsHAL_INTERFACE structure that receives any data to be
**          returned.
*/
static gceSTATUS
_AllocateMemory(
    IN gckKERNEL Kernel,
    IN OUT gcePOOL * Pool,
    IN gctSIZE_T Bytes,
    IN gctSIZE_T Alignment,
    IN gceSURF_TYPE Type,
#ifdef __QNXNTO__
    IN gctHANDLE Handle,
#endif
    OUT gcuVIDMEM_NODE_PTR * Node
    )
{
    gcePOOL pool;
    gceSTATUS status;
    gckVIDMEM videoMemory;

    gcmkVERIFY_ARGUMENT(Pool != gcvNULL);

    /* Get initial pool. */
    switch (pool = *Pool)
    {
    case gcvPOOL_DEFAULT:
    case gcvPOOL_LOCAL:
        pool = gcvPOOL_LOCAL_INTERNAL;
        break;

    case gcvPOOL_UNIFIED:
        pool = gcvPOOL_SYSTEM;
        break;

    default:
        break;
    }
    
#if (0==gcdPAGE_ALLOC_LIMIT)
    // dkm : force gcvSURF_TILE_STATUS use contiguous memory
    //if(gcvSURF_TILE_STATUS == Type)     pool = gcvPOOL_CONTIGUOUS;
#endif

    do
    {
        /* Verify the number of bytes to allocate. */
        if (Bytes == 0)
        {
            gcmkERR_BREAK(gcvSTATUS_INVALID_ARGUMENT);
        }

        if (pool == gcvPOOL_VIRTUAL)
        {
            /* Create a gcuVIDMEM_NODE for virtual memory. */
#ifdef __QNXNTO__
            gcmkERR_BREAK(
                gckVIDMEM_ConstructVirtual(Kernel, gcvFALSE, Bytes, Handle, Node));
#else
            gcmkERR_BREAK(
                gckVIDMEM_ConstructVirtual(Kernel, gcvFALSE, Bytes, Node));
#endif

            /* Success. */
            break;
        }
        else if (pool == gcvPOOL_CONTIGUOUS)
        {
            /* Create a gcuVIDMEM_NODE for contiguous memory. */
#ifdef __QNXNTO__
            status = gckVIDMEM_ConstructVirtual(Kernel, gcvTRUE, Bytes, Handle, Node);
#else
            status = gckVIDMEM_ConstructVirtual(Kernel, gcvTRUE, Bytes, Node);
#endif
            if (gcmIS_SUCCESS(status))
            {
                /* Memory allocated. */
                break;
            }
        }
        else
        {
            /* Get pointer to gckVIDMEM object for pool. */
            status = gckKERNEL_GetVideoMemoryPool(Kernel, pool, &videoMemory);

            if (gcmIS_SUCCESS(status))
            {
                /* Allocate memory. */
                status = gckVIDMEM_AllocateLinear(videoMemory,
                                                  Bytes,
                                                  Alignment,
                                                  Type,
#ifdef __QNXNTO__
                                                  Handle,
#endif
                                                  Node);

                if (gcmIS_SUCCESS(status))
                {
                    /* Memory allocated. */
                    (*Node)->VidMem.pool = pool;
                    break;
                }
            }
        }

        if (pool == gcvPOOL_LOCAL_INTERNAL)
        {
            /* Advance to external memory. */
            pool = gcvPOOL_LOCAL_EXTERNAL;
        }
        else
        if (pool == gcvPOOL_LOCAL_EXTERNAL)
        {
            /* Advance to contiguous system memory. */
            pool = gcvPOOL_SYSTEM;
        }
        else
        if ((pool == gcvPOOL_SYSTEM)
        &&  (Type != gcvSURF_TILE_STATUS)
        )
        {
            /* Advance to contiguous memory. */
            pool = gcvPOOL_CONTIGUOUS;
        }
        else
        if ((pool == gcvPOOL_CONTIGUOUS)
        &&  (Type != gcvSURF_TILE_STATUS)
        )
        {
            /* Advance to virtual memory. */
            pool = gcvPOOL_VIRTUAL;
        }
        else
        {
            /* Out of pools. */
            break;
        }
    }
    /* Loop only for multiple selection pools. */
    while ((*Pool == gcvPOOL_DEFAULT)
    ||     (*Pool == gcvPOOL_LOCAL)
    ||     (*Pool == gcvPOOL_UNIFIED)
#if (0==gcdPAGE_ALLOC_LIMIT)
    // dkm : let gcvPOOL_SYSTEM can use contiguous memory
    ||     ((*Pool == gcvPOOL_SYSTEM) && (pool==gcvPOOL_CONTIGUOUS))    
#endif
    );

    if (gcmIS_SUCCESS(status))
    {
        /* Return pool used for allocation. */
        *Pool = pool;
    } else {
        printk("_AllocateMemory fail! pool=%d->%d, Bytes=%d, Type=%d\n", *Pool, pool, (int)Bytes, Type);
    }

    /* Return status. */
    return status;
}

/*******************************************************************************
**
**  gckKERNEL_Dispatch
**
**  Dispatch a command received from the user HAL layer.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      gctBOOL FromUser
**          whether the call is from the user space.
**
**      gcsHAL_INTERFACE * Interface
**          Pointer to a gcsHAL_INTERFACE structure that defines the command to
**          be dispatched.
**
**  OUTPUT:
**
**      gcsHAL_INTERFACE * Interface
**          Pointer to a gcsHAL_INTERFACE structure that receives any data to be
**          returned.
*/
gceSTATUS
gckKERNEL_Dispatch(
    IN gckKERNEL Kernel,
    IN gctBOOL FromUser,
    IN OUT gcsHAL_INTERFACE * Interface
    )
{
    gceSTATUS status;
    gctUINT32 bitsPerPixel;
    gctSIZE_T bytes;
    gcuVIDMEM_NODE_PTR node;
    gctBOOL locked = gcvFALSE;
    gctPHYS_ADDR physical;
    gctUINT32 address;

    gcmkHEADER_ARG("Kernel=0x%x FromUser=%d Interface=0x%x",
                   Kernel, FromUser, Interface);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(Interface != gcvNULL);

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_KERNEL,
                   "Dispatching command %d", Interface->command);

    /* Dispatch on command. */
    switch (Interface->command)
    {
    case gcvHAL_GET_BASE_ADDRESS:
        /* Get base address. */
        gcmkONERROR(
            gckOS_GetBaseAddress(Kernel->os,
                                 &Interface->u.GetBaseAddress.baseAddress));
        strcpy(Interface->u.GetBaseAddress.fwVersion, GPU_FW_VERSION);
#if BUILD_FOR_1_28
        strcat(Interface->u.GetBaseAddress.fwVersion, "_for1.28");
#endif
        break;

    case gcvHAL_QUERY_VIDEO_MEMORY:
        /* Query video memory size. */
        gcmkONERROR(gckKERNEL_QueryVideoMemory(Kernel, Interface));
        break;

    case gcvHAL_QUERY_CHIP_IDENTITY:
        /* Query chip identity. */
        gcmkONERROR(
            gckHARDWARE_QueryChipIdentity(
                Kernel->hardware,
                &Interface->u.QueryChipIdentity.chipModel,
                &Interface->u.QueryChipIdentity.chipRevision,
                &Interface->u.QueryChipIdentity.chipFeatures,
                &Interface->u.QueryChipIdentity.chipMinorFeatures,
                &Interface->u.QueryChipIdentity.chipMinorFeatures1,
                &Interface->u.QueryChipIdentity.chipMinorFeatures2));

        /* Query chip specifications. */
        gcmkONERROR(
            gckHARDWARE_QueryChipSpecs(
                Kernel->hardware,
                &Interface->u.QueryChipIdentity.streamCount,
                &Interface->u.QueryChipIdentity.registerMax,
                &Interface->u.QueryChipIdentity.threadCount,
                &Interface->u.QueryChipIdentity.shaderCoreCount,
                &Interface->u.QueryChipIdentity.vertexCacheSize,
                &Interface->u.QueryChipIdentity.vertexOutputBufferSize));
        break;

    case gcvHAL_MAP_MEMORY:
        physical = Interface->u.MapMemory.physical;

        /* Map memory. */
        gcmkONERROR(
            gckKERNEL_MapMemory(Kernel,
                                physical,
                                Interface->u.MapMemory.bytes,
                                &Interface->u.MapMemory.logical));
        break;

    case gcvHAL_UNMAP_MEMORY:
        physical = Interface->u.UnmapMemory.physical;

        /* Unmap memory. */
        gcmkONERROR(
            gckKERNEL_UnmapMemory(Kernel,
                                  physical,
                                  Interface->u.UnmapMemory.bytes,
                                  Interface->u.UnmapMemory.logical));
        break;

    case gcvHAL_ALLOCATE_NON_PAGED_MEMORY:
        /* Allocate non-paged memory. */
#ifdef __QNXNTO__
        if (FromUser)
        {
            gcmkONERROR(
                gckOS_AllocateNonPagedMemoryShmPool(
                Kernel->os,
                FromUser,
                Interface->pid,
                Interface->handle,
                &Interface->u.AllocateNonPagedMemory.bytes,
                &Interface->u.AllocateNonPagedMemory.physical,
                &Interface->u.AllocateNonPagedMemory.logical));
            break;
        }
#endif
        gcmkONERROR(
            gckOS_AllocateNonPagedMemory(
                Kernel->os,
                FromUser,
                &Interface->u.AllocateNonPagedMemory.bytes,
                &Interface->u.AllocateNonPagedMemory.physical,
                &Interface->u.AllocateNonPagedMemory.logical));
        break;

    case gcvHAL_FREE_NON_PAGED_MEMORY:
        physical = Interface->u.FreeNonPagedMemory.physical;

        /* Free non-paged memory. */
        gcmkONERROR(
            gckOS_FreeNonPagedMemory(Kernel->os,
                                     Interface->u.FreeNonPagedMemory.bytes,
                                     physical,
                                     Interface->u.FreeNonPagedMemory.logical));
        break;

    case gcvHAL_ALLOCATE_CONTIGUOUS_MEMORY:
        /* Allocate contiguous memory. */
#ifdef __QNXNTO__
        if (FromUser)
        {
            gcmkONERROR(
                gckOS_AllocateNonPagedMemoryShmPool(
                Kernel->os,
                FromUser,
                Interface->pid,
                Interface->handle,
                &Interface->u.AllocateNonPagedMemory.bytes,
                &Interface->u.AllocateNonPagedMemory.physical,
                &Interface->u.AllocateNonPagedMemory.logical));
            break;
        }
#endif
        gcmkONERROR(
            gckOS_AllocateContiguous(
                Kernel->os,
                FromUser,
                &Interface->u.AllocateContiguousMemory.bytes,
                &Interface->u.AllocateContiguousMemory.physical,
                &Interface->u.AllocateContiguousMemory.logical));

        break;

    case gcvHAL_FREE_CONTIGUOUS_MEMORY:
        physical = Interface->u.FreeContiguousMemory.physical;

       /* Free contiguous memory. */
        gcmkONERROR(
            gckOS_FreeContiguous(Kernel->os,
                                 physical,
                                 Interface->u.FreeContiguousMemory.logical,
                                 Interface->u.FreeContiguousMemory.bytes));
        break;

    case gcvHAL_ALLOCATE_VIDEO_MEMORY:
        /* Align width and height to tiles. */
        gcmkONERROR(
            gckHARDWARE_AlignToTile(Kernel->hardware,
                                    Interface->u.AllocateVideoMemory.type,
                                    &Interface->u.AllocateVideoMemory.width,
                                    &Interface->u.AllocateVideoMemory.height,
                                    gcvNULL));

        /* Convert format into bytes per pixel and bytes per tile. */
        gcmkONERROR(
            gckHARDWARE_ConvertFormat(Kernel->hardware,
                                      Interface->u.AllocateVideoMemory.format,
                                      &bitsPerPixel,
                                      gcvNULL));

        /* Compute number of bytes for the allocation. */
        bytes = Interface->u.AllocateVideoMemory.width * bitsPerPixel
              * Interface->u.AllocateVideoMemory.height
              * Interface->u.AllocateVideoMemory.depth / 8;

        /* Allocate memory. */
#ifdef __QNXNTO__
        gcmkONERROR(
            _AllocateMemory(Kernel,
                            &Interface->u.AllocateVideoMemory.pool,
                            bytes,
                            64,
                            Interface->u.AllocateVideoMemory.type,
                            Interface->handle,
                            &Interface->u.AllocateVideoMemory.node));
#else
        gcmkONERROR(
            _AllocateMemory(Kernel,
                            &Interface->u.AllocateVideoMemory.pool,
                            bytes,
                            64,
                            Interface->u.AllocateVideoMemory.type,
                            &Interface->u.AllocateVideoMemory.node));
#endif
        break;

    case gcvHAL_ALLOCATE_LINEAR_VIDEO_MEMORY:
        /* Allocate memory. */
#ifdef __QNXNTO__
        gcmkONERROR(
            _AllocateMemory(Kernel,
                            &Interface->u.AllocateLinearVideoMemory.pool,
                            Interface->u.AllocateLinearVideoMemory.bytes,
                            Interface->u.AllocateLinearVideoMemory.alignment,
                            Interface->u.AllocateLinearVideoMemory.type,
                            Interface->handle,
                            &Interface->u.AllocateLinearVideoMemory.node));

        /* Set the current user pid in the node,
         * which is used while locking memory. */
        gcmkVERIFY_OK(gckVIDMEM_SetPID(
                Interface->u.AllocateLinearVideoMemory.node,
                Interface->pid));
#else
        gcmkONERROR(
            _AllocateMemory(Kernel,
                            &Interface->u.AllocateLinearVideoMemory.pool,
                            Interface->u.AllocateLinearVideoMemory.bytes,
                            Interface->u.AllocateLinearVideoMemory.alignment,
                            Interface->u.AllocateLinearVideoMemory.type,
                            &Interface->u.AllocateLinearVideoMemory.node));
#endif
        break;

    case gcvHAL_FREE_VIDEO_MEMORY:
#ifdef __QNXNTO__
        node = Interface->u.FreeVideoMemory.node;
        if (node->VidMem.memory->object.type == gcvOBJ_VIDMEM
         && node->VidMem.logical != gcvNULL)
        {
            gcmkONERROR(
                    gckKERNEL_UnmapVideoMemory(Kernel,
                                               node->VidMem.logical,
                                               Interface->pid,
                                               node->VidMem.bytes));
            node->VidMem.logical = gcvNULL;
        }
#endif
        /* Free video memory. */
        gcmkONERROR(
            gckVIDMEM_Free(Interface->u.FreeVideoMemory.node));
        break;

    case gcvHAL_LOCK_VIDEO_MEMORY:
        /* Lock video memory. */
        gcmkONERROR(
            gckVIDMEM_Lock(Interface->u.LockVideoMemory.node,
                           &Interface->u.LockVideoMemory.address));

        locked = gcvTRUE;

        node = Interface->u.LockVideoMemory.node;
        if (node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
        {
            /* Map video memory address into user space. */
#ifdef __QNXNTO__
        if (node->VidMem.logical == gcvNULL)
        {
            gcmkONERROR(
                gckKERNEL_MapVideoMemory(Kernel,
                                         FromUser,
                                         Interface->u.LockVideoMemory.address,
                                         Interface->pid,
                                         node->VidMem.bytes,
                                         &node->VidMem.logical));
        }
        Interface->u.LockVideoMemory.memory = node->VidMem.logical;
#else
            gcmkONERROR(
                gckKERNEL_MapVideoMemory(Kernel,
                                         FromUser,
                                         Interface->u.LockVideoMemory.address,
                                         &Interface->u.LockVideoMemory.memory));
#endif

#ifdef __QNXNTO__
            /* Add more information to node, which is used while unmapping. */
            gcmkVERIFY_OK(gckVIDMEM_SetPID(
                    Interface->u.LockVideoMemory.node,
                    Interface->pid));
#endif
        }

        else
        {
            /* Copy logical memory for virtual memory. */
            Interface->u.LockVideoMemory.memory = node->Virtual.logical;

            /* Success. */
            status = gcvSTATUS_OK;
        }

#if gcdSECURE_USER
        /* Return logical address as physical address. */
        Interface->u.LockVideoMemory.address =
            gcmPTR2INT(Interface->u.LockVideoMemory.memory);
#endif
        break;

    case gcvHAL_UNLOCK_VIDEO_MEMORY:
        /* Unlock video memory. */
        node = Interface->u.UnlockVideoMemory.node;

        /* Unlock video memory. */
        gcmkONERROR(
            gckVIDMEM_Unlock(node,
                             Interface->u.UnlockVideoMemory.type,
                             &Interface->u.UnlockVideoMemory.asynchroneous));
        break;

    case gcvHAL_EVENT_COMMIT:
        /* Commit an event queue. */
        gcmkONERROR(
            gckEVENT_Commit(Kernel->event,
                            Interface->u.Event.queue));
        break;

    case gcvHAL_COMMIT:
        /* Commit a command and context buffer. */
        gcmkONERROR(
            gckCOMMAND_Commit(Kernel->command,
                              Interface->u.Commit.commandBuffer,
                              Interface->u.Commit.contextBuffer,
                              Interface->u.Commit.process));
        break;

    case gcvHAL_STALL:
        /* Stall the command queue. */
        gcmkONERROR(gckCOMMAND_Stall(Kernel->command));
        break;

    case gcvHAL_MAP_USER_MEMORY:
        /* Map user memory to DMA. */
        gcmkONERROR(
            gckOS_MapUserMemory(Kernel->os,
                                Interface->u.MapUserMemory.memory,
                                Interface->u.MapUserMemory.size,
                                &Interface->u.MapUserMemory.info,
                                &Interface->u.MapUserMemory.address));
        break;

    case gcvHAL_UNMAP_USER_MEMORY:
        address = Interface->u.MapUserMemory.address;

        /* Unmap user memory. */
        gcmkONERROR(
            gckOS_UnmapUserMemory(Kernel->os,
                                  Interface->u.UnmapUserMemory.memory,
                                  Interface->u.UnmapUserMemory.size,
                                  Interface->u.UnmapUserMemory.info,
                                  address));
        break;

#if !USE_NEW_LINUX_SIGNAL
    case gcvHAL_USER_SIGNAL:
        /* Dispatch depends on the user signal subcommands. */
        switch(Interface->u.UserSignal.command)
        {
        case gcvUSER_SIGNAL_CREATE:
            /* Create a signal used in the user space. */
            gcmkONERROR(
                gckOS_CreateUserSignal(Kernel->os,
                                       Interface->u.UserSignal.manualReset,
                                       &Interface->u.UserSignal.id));
            break;

        case gcvUSER_SIGNAL_DESTROY:
            /* Destroy the signal. */
            gcmkONERROR(
                gckOS_DestroyUserSignal(Kernel->os,
                                        Interface->u.UserSignal.id));
            break;

        case gcvUSER_SIGNAL_SIGNAL:
            /* Signal the signal. */
            gcmkONERROR(
                gckOS_SignalUserSignal(Kernel->os,
                                       Interface->u.UserSignal.id,
                                       Interface->u.UserSignal.state));
            break;

        case gcvUSER_SIGNAL_WAIT:
            /* Wait on the signal. */
            status = gckOS_WaitUserSignal(Kernel->os,
                                          Interface->u.UserSignal.id,
                                          Interface->u.UserSignal.wait);
            break;

        default:
            /* Invalid user signal command. */
            gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }
        break;
#endif

    case gcvHAL_SET_POWER_MANAGEMENT_STATE:
        /* Set the power management state. */
        gcmkONERROR(
            gckHARDWARE_SetPowerManagementState(
                Kernel->hardware,
                Interface->u.SetPowerManagement.state));
        break;

    case gcvHAL_QUERY_POWER_MANAGEMENT_STATE:
        /* Chip is not idle. */
        Interface->u.QueryPowerManagement.isIdle = gcvFALSE;

        /* Query the power management state. */
        gcmkONERROR(gckHARDWARE_QueryPowerManagementState(
            Kernel->hardware,
            &Interface->u.QueryPowerManagement.state));

        /* Query the idle state. */
        gcmkONERROR(
            gckHARDWARE_QueryIdle(Kernel->hardware,
                                  &Interface->u.QueryPowerManagement.isIdle));
        break;

    case gcvHAL_READ_REGISTER:
#if gcdREGISTER_ACCESS_FROM_USER
        /* Read a register. */
        gcmkONERROR(
            gckOS_ReadRegister(Kernel->os,
                               Interface->u.ReadRegisterData.address,
                               &Interface->u.ReadRegisterData.data));
#else
        /* No access from user land to read registers. */
        Interface->u.ReadRegisterData.data = 0;
        status = gcvSTATUS_NOT_SUPPORTED;
#endif
        break;

    case gcvHAL_WRITE_REGISTER:
#if gcdREGISTER_ACCESS_FROM_USER
        /* Write a register. */
        gcmkONERROR(
            gckOS_WriteRegister(Kernel->os,
                                Interface->u.WriteRegisterData.address,
                                Interface->u.WriteRegisterData.data));
#else
        /* No access from user land to write registers. */
        status = gcvSTATUS_NOT_SUPPORTED;
#endif
        break;

    case gcvHAL_READ_ALL_PROFILE_REGISTERS:
#if VIVANTE_PROFILER
        /* Read all 3D profile registers. */
        gcmkONERROR(
            gckHARDWARE_QueryProfileRegisters(
                Kernel->hardware,
                &Interface->u.RegisterProfileData.counters));
#else
        status = gcvSTATUS_OK;
#endif
        break;

    case gcvHAL_PROFILE_REGISTERS_2D:
#if VIVANTE_PROFILER
        /* Read all 2D profile registers. */
        gcmkONERROR(
            gckHARDWARE_ProfileEngine2D(
                Kernel->hardware,
                Interface->u.RegisterProfileData2D.hwProfile2D));
#else
        status = gcvSTATUS_OK;
#endif
        break;

    case gcvHAL_GET_PROFILE_SETTING:
#if VIVANTE_PROFILER
        /* Get profile setting */
        Interface->u.GetProfileSetting.enable = Kernel->profileEnable;

        gcmkVERIFY_OK(
            gckOS_MemCopy(Interface->u.GetProfileSetting.fileName,
                          Kernel->profileFileName,
                          gcdMAX_PROFILE_FILE_NAME));
#endif

        status = gcvSTATUS_OK;
        break;

    case gcvHAL_SET_PROFILE_SETTING:
#if VIVANTE_PROFILER
        /* Set profile setting */
        Kernel->profileEnable = Interface->u.SetProfileSetting.enable;

        gcmkVERIFY_OK(
            gckOS_MemCopy(Kernel->profileFileName,
                          Interface->u.SetProfileSetting.fileName,
                          gcdMAX_PROFILE_FILE_NAME));
#endif

        status = gcvSTATUS_OK;
        break;

    case gcvHAL_QUERY_KERNEL_SETTINGS:
        /* Get kernel settings. */
        gcmkONERROR(
            gckKERNEL_QuerySettings(Kernel,
                                    &Interface->u.QueryKernelSettings.settings));
        break;

    case gcvHAL_RESET:
        /* Reset the hardware. */
        gcmkONERROR(
            gckHARDWARE_Reset(Kernel->hardware));
        break;

    case gcvHAL_DEBUG:
// dkm : add "#if gcdDUMP_IN_KERNEL" to invalidate the code.
#if gcdDUMP_IN_KERNEL
        /* Set debug level and zones. */
        if (Interface->u.Debug.set)
        {
            gckOS_SetDebugLevel(Interface->u.Debug.level);
            gckOS_SetDebugZones(Interface->u.Debug.zones,
                                Interface->u.Debug.enable);
        }

        if (Interface->u.Debug.message[0] != '\0')
        {
            /* Print a message to the debugger. */
            gcmkPRINT(Interface->u.Debug.message);
        }
#endif
        status = gcvSTATUS_OK;
        break;

    case gcvHAL_CACHE:
        if (Interface->u.Cache.invalidate)
        {
            /* Flush and invalidate the cache. */
            status = gckOS_CacheInvalidate(Kernel->os,
                                           Interface->u.Cache.process,
                                           Interface->u.Cache.logical,
                                           Interface->u.Cache.bytes);
        }
        else
        {
            /* Flush the cache. */
            status = gckOS_CacheFlush(Kernel->os,
                                      Interface->u.Cache.process,
                                      Interface->u.Cache.logical,
                                      Interface->u.Cache.bytes);
        }
        break;

#if gcdGPU_TIMEOUT
    case gcvHAL_BROADCAST_GPU_STUCK:
        /* Broadcast GPU stuck */
        status = gckOS_Broadcast(Kernel->os,
                                 Kernel->hardware,
                                 gcvBROADCAST_GPU_STUCK);
        break;
#endif

    default:
        /* Invalid command. */
        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

OnError:
    /* Save status. */
    Interface->status = status;

    if (gcmIS_ERROR(status))
    {
        if (locked)
        {
            /* Roll back the lock. */
            gcmkVERIFY_OK(
                gckVIDMEM_Unlock(Interface->u.LockVideoMemory.node,
                                 gcvSURF_TYPE_UNKNOWN,
                                 gcvNULL));
        }
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckKERNEL_AttachProcess(
    IN gckKERNEL Kernel,
    IN gctBOOL Attach
    )
{
    gceSTATUS status;
    gctINT32 old;

    gcmkHEADER_ARG("Kernel=0x%x Attach=%d", Kernel, Attach);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);

    if (Attach)
    {
        /* Increment the number of clients attached. */
        gcmkONERROR(
            gckOS_AtomIncrement(Kernel->os, Kernel->atomClients, &old));

        if (old == 0)
        {
            gcmkONERROR(gckOS_Broadcast(Kernel->os,
                                        Kernel->hardware,
                                        gcvBROADCAST_FIRST_PROCESS));
        }
    }

    else
    {
        /* Decrement the number of clients attached. */
        gcmkONERROR(
            gckOS_AtomDecrement(Kernel->os, Kernel->atomClients, &old));

        if (old == 1)
        {
            /* Last client detached, switch to SUSPEND power state. */
            gcmkONERROR(gckOS_Broadcast(Kernel->os,
                                        Kernel->hardware,
                                        gcvBROADCAST_LAST_PROCESS));

            /* Flush the debug cache. */
            gcmkPRINT("$$FLUSH$$");
        }
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

#if gcdSECURE_USER
gceSTATUS
gckKERNEL_MapLogicalToPhysical(
    IN gckKERNEL Kernel,
    IN gctHANDLE Process,
    IN OUT gctPOINTER * Data
    )
{
    gctUINT i, oldest;
    gceSTATUS status;
    gctUINT32 baseAddress;

    gcmkHEADER_ARG("Kernel=0x%x Process=0x%x *Data=0x%x",
                   Kernel, Process, gcmOPT_POINTER(Data));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(Data != gcvNULL);

    /* Get base address. */
    gcmkONERROR(gckHARDWARE_GetBaseAddress(Kernel->hardware, &baseAddress));

    /* Walk all used cache slots. */
    for (i = oldest = 0; i < Kernel->cacheSlots; ++i)
    {
        if ((Kernel->cache[i].logical == *Data)
        &&  (Kernel->cache[i].process == Process)
        )
        {
            /* Bail out. */
            break;
        }

        if (i == 0)
        {
            /* First slot. */
            oldest = i;
        }

        /* Test if this cache slot is older. */
        else if (Kernel->cache[i].stamp < Kernel->cache[oldest].stamp)
        {
            oldest = i;
        }
    }

    /* See if we had a match. */
    if (i == Kernel->cacheSlots)
    {
        /* See if there is room in the cache. */
        if (i < gcmCOUNTOF(Kernel->cache))
        {
            /* Just append to the cache. */
            i = Kernel->cacheSlots++;
        }

        else
        {
            /* Evict the oldest cache line. */
            i = oldest;
        }

        /* Initialize the cache line. */
        Kernel->cache[i].logical = *Data;
        Kernel->cache[i].process = Process;

        /* Map the logical address to a DMA address. */
        gcmkONERROR(gckOS_GetPhysicalAddress(Kernel->os,
                                             *Data,
                                             &Kernel->cache[i].dma));

        if (baseAddress != 0)
        {
            gctBOOL needBase;

            /* Does this state load need a base address? */
            gcmkONERROR(gckHARDWARE_NeedBaseAddress(Kernel->hardware,
                                                    ((gctUINT32_PTR) Data)[-1],
                                                    &needBase));

            if (needBase)
            {
                /* Add the base address. */
                Kernel->cache[i].dma += baseAddress;
            }
        }
    }

    /* Increment time stamp of the cache slot. */
    Kernel->cache[i].stamp = Kernel->cacheTimeStamp++;

    /* Return DMA address. */
    *Data = gcmINT2PTR(Kernel->cache[i].dma);

    /* Success. */
    gcmkFOOTER_ARG("*Data=0x%08x", *Data);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}
#endif

/*******************************************************************************
**
**  gckKERNEL_Recovery
**
**  Try to recover the GPU from a fatal error.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckKERNEL_Recovery(
    IN gckKERNEL Kernel
    )
{
    gceSTATUS status;
    gckEVENT event;
    gckHARDWARE hardware;
#if gcdSECURE_USER
    gctUINT32 processID;
#endif

    gcmkHEADER_ARG("Kernel=0x%x", Kernel);

    /* Validate the arguemnts. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);

    /* Grab gckEVENT object. */
    event = Kernel->event;
    gcmkVERIFY_OBJECT(event, gcvOBJ_EVENT);

    /* Grab gckHARDWARE object. */
    hardware = Kernel->hardware;
    gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

    /* Handle all outstanding events now. */
    event->pending = ~0U;
    gcmkONERROR(gckEVENT_Notify(event, 1));

    /* Again in case more events got submitted. */
    event->pending = ~0U;
    gcmkONERROR(gckEVENT_Notify(event, 2));

#if gcdSECURE_USER
    /* Flush the secure mapping cache. */
    gcmkONERROR(gckOS_GetProcessID(&processID));
    gcmkONERROR(gckKERNEL_MapLogicalToPhysical(Kernel, processID, gcvNULL));
#endif

    /* Try issuing a soft reset for the GPU. */
    status = gckHARDWARE_Reset(hardware);
    if (status == gcvSTATUS_NOT_SUPPORTED)
    {
        /* Switch to OFF power.  The next submit should return the GPU to ON
        ** state. */
        gcmkONERROR(
            gckHARDWARE_SetPowerManagementState(hardware,
                                                gcvPOWER_OFF_RECOVERY));
    }
    else
    {
        /* Bail out on reset error. */
        gcmkONERROR(status);
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}
