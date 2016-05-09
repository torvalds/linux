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


#include "gc_hal_kernel_precomp.h"

#if gcdENABLE_VG

#define _GC_OBJ_ZONE            gcvZONE_VG

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
gceSTATUS gckVGKERNEL_Construct(
    IN gckOS Os,
    IN gctPOINTER Context,
    IN gckKERNEL  inKernel,
    OUT gckVGKERNEL * Kernel
    )
{
    gceSTATUS status;
    gckVGKERNEL kernel = gcvNULL;

    gcmkHEADER_ARG("Os=0x%x Context=0x%x", Os, Context);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Kernel != gcvNULL);

    do
    {
        /* Allocate the gckKERNEL object. */
        gcmkERR_BREAK(gckOS_Allocate(
            Os,
            sizeof(struct _gckVGKERNEL),
            (gctPOINTER *) &kernel
            ));

        /* Initialize the gckKERNEL object. */
        kernel->object.type = gcvOBJ_KERNEL;
        kernel->os          = Os;
        kernel->context     = Context;
        kernel->hardware    = gcvNULL;
        kernel->interrupt   = gcvNULL;
        kernel->command     = gcvNULL;
        kernel->mmu         = gcvNULL;
        kernel->kernel      = inKernel;

        /* Construct the gckVGHARDWARE object. */
        gcmkERR_BREAK(gckVGHARDWARE_Construct(
            Os, &kernel->hardware
            ));

        /* Set pointer to gckKERNEL object in gckVGHARDWARE object. */
        kernel->hardware->kernel = kernel;

        /* Construct the gckVGINTERRUPT object. */
        gcmkERR_BREAK(gckVGINTERRUPT_Construct(
            kernel, &kernel->interrupt
            ));

        /* Construct the gckVGCOMMAND object. */
        gcmkERR_BREAK(gckVGCOMMAND_Construct(
            kernel, gcmKB2BYTES(8), gcmKB2BYTES(2), &kernel->command
            ));

        /* Construct the gckVGMMU object. */
        gcmkERR_BREAK(gckVGMMU_Construct(
            kernel, gcmKB2BYTES(gcdGC355_VGMMU_MEMORY_SIZE_KB), &kernel->mmu
            ));

        /* Return pointer to the gckKERNEL object. */
        *Kernel = kernel;

        gcmkFOOTER_ARG("*Kernel=0x%x", *Kernel);
        /* Success. */
        return gcvSTATUS_OK;
    }
    while (gcvFALSE);

    /* Roll back. */
    if (kernel != gcvNULL)
    {
        if (kernel->mmu != gcvNULL)
        {
            gcmkVERIFY_OK(gckVGMMU_Destroy(kernel->mmu));
        }

        if (kernel->command != gcvNULL)
        {
            gcmkVERIFY_OK(gckVGCOMMAND_Destroy(kernel->command));
        }

        if (kernel->interrupt != gcvNULL)
        {
            gcmkVERIFY_OK(gckVGINTERRUPT_Destroy(kernel->interrupt));
        }

        if (kernel->hardware != gcvNULL)
        {
            gcmkVERIFY_OK(gckVGHARDWARE_Destroy(kernel->hardware));
        }

        gcmkVERIFY_OK(gckOS_Free(Os, kernel));
    }

    gcmkFOOTER();
    /* Return status. */
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
gceSTATUS gckVGKERNEL_Destroy(
    IN gckVGKERNEL Kernel
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Kernel=0x%x", Kernel);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);

    do
    {
        /* Destroy the gckVGMMU object. */
        if (Kernel->mmu != gcvNULL)
        {
            gcmkERR_BREAK(gckVGMMU_Destroy(Kernel->mmu));
            Kernel->mmu = gcvNULL;
        }

        /* Destroy the gckVGCOMMAND object. */
        if (Kernel->command != gcvNULL)
        {
            gcmkERR_BREAK(gckVGCOMMAND_Destroy(Kernel->command));
            Kernel->command = gcvNULL;
        }

        /* Destroy the gckVGINTERRUPT object. */
        if (Kernel->interrupt != gcvNULL)
        {
            gcmkERR_BREAK(gckVGINTERRUPT_Destroy(Kernel->interrupt));
            Kernel->interrupt = gcvNULL;
        }

        /* Destroy the gckVGHARDWARE object. */
        if (Kernel->hardware != gcvNULL)
        {
            gcmkERR_BREAK(gckVGHARDWARE_Destroy(Kernel->hardware));
            Kernel->hardware = gcvNULL;
        }

        /* Mark the gckKERNEL object as unknown. */
        Kernel->object.type = gcvOBJ_UNKNOWN;

        /* Free the gckKERNEL object. */
        gcmkERR_BREAK(gckOS_Free(Kernel->os, Kernel));
    }
    while (gcvFALSE);

    gcmkFOOTER();

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
gceSTATUS gckVGKERNEL_Dispatch(
    IN gckKERNEL Kernel,
    IN gctBOOL FromUser,
    IN OUT gcsHAL_INTERFACE * Interface
    )
{
    gceSTATUS status;
    gcsHAL_INTERFACE * kernelInterface = Interface;
    gctUINT32 processID;
    gckKERNEL kernel = Kernel;
    gctPHYS_ADDR physical = gcvNULL;
    gctPOINTER logical = gcvNULL;
    gctSIZE_T bytes = 0;

    gcmkHEADER_ARG("Kernel=0x%x Interface=0x%x ", Kernel, Interface);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(Interface != gcvNULL);

    gcmkONERROR(gckOS_GetProcessID(&processID));

    /* Dispatch on command. */
    switch (Interface->command)
    {
    case gcvHAL_QUERY_CHIP_IDENTITY:
        /* Query chip identity. */
        gcmkERR_BREAK(gckVGHARDWARE_QueryChipIdentity(
            Kernel->vg->hardware,
            &kernelInterface->u.QueryChipIdentity.chipModel,
            &kernelInterface->u.QueryChipIdentity.chipRevision,
            &kernelInterface->u.QueryChipIdentity.productID,
            &kernelInterface->u.QueryChipIdentity.ecoID,
            &kernelInterface->u.QueryChipIdentity.chipFeatures,
            &kernelInterface->u.QueryChipIdentity.chipMinorFeatures,
            &kernelInterface->u.QueryChipIdentity.chipMinorFeatures2
            ));
        break;

    case gcvHAL_QUERY_COMMAND_BUFFER:
        /* Query command buffer information. */
        gcmkERR_BREAK(gckKERNEL_QueryCommandBuffer(
            Kernel,
            &kernelInterface->u.QueryCommandBuffer.information
            ));
        break;

    case gcvHAL_ALLOCATE_NON_PAGED_MEMORY:
        bytes = (gctSIZE_T) kernelInterface->u.AllocateNonPagedMemory.bytes;
        /* Allocate non-paged memory. */
        gcmkERR_BREAK(gckOS_AllocateNonPagedMemory(
            Kernel->os,
            gcvTRUE,
            &bytes,
            &physical,
            &logical
            ));

        kernelInterface->u.AllocateNonPagedMemory.bytes    = bytes;
        kernelInterface->u.AllocateNonPagedMemory.logical  = gcmPTR_TO_UINT64(logical);
        kernelInterface->u.AllocateNonPagedMemory.physical = gcmPTR_TO_NAME(physical);
        break;

    case gcvHAL_FREE_NON_PAGED_MEMORY:
        physical = gcmNAME_TO_PTR(kernelInterface->u.AllocateNonPagedMemory.physical);

        /* Unmap user logical out of physical memory first. */
        gcmkERR_BREAK(gckOS_UnmapUserLogical(
            Kernel->os,
            physical,
            (gctSIZE_T) kernelInterface->u.AllocateNonPagedMemory.bytes,
            gcmUINT64_TO_PTR(kernelInterface->u.AllocateNonPagedMemory.logical)
            ));

        /* Free non-paged memory. */
        gcmkERR_BREAK(gckOS_FreeNonPagedMemory(
            Kernel->os,
            (gctSIZE_T) kernelInterface->u.AllocateNonPagedMemory.bytes,
            physical,
            gcmUINT64_TO_PTR(kernelInterface->u.AllocateNonPagedMemory.logical)
            ));

        gcmRELEASE_NAME(kernelInterface->u.AllocateNonPagedMemory.physical);
        break;

    case gcvHAL_ALLOCATE_CONTIGUOUS_MEMORY:
        bytes = (gctSIZE_T) kernelInterface->u.AllocateNonPagedMemory.bytes;
        /* Allocate contiguous memory. */
        gcmkERR_BREAK(gckOS_AllocateContiguous(
            Kernel->os,
            gcvTRUE,
            &bytes,
            &physical,
            &logical
            ));

        kernelInterface->u.AllocateNonPagedMemory.bytes    = bytes;
        kernelInterface->u.AllocateNonPagedMemory.logical  = gcmPTR_TO_UINT64(logical);
        kernelInterface->u.AllocateNonPagedMemory.physical = gcmPTR_TO_NAME(physical);
        break;

    case gcvHAL_FREE_CONTIGUOUS_MEMORY:
        physical = gcmNAME_TO_PTR(kernelInterface->u.AllocateNonPagedMemory.physical);
        /* Unmap user logical out of physical memory first. */
        gcmkERR_BREAK(gckOS_UnmapUserLogical(
            Kernel->os,
            physical,
            (gctSIZE_T) kernelInterface->u.AllocateNonPagedMemory.bytes,
            gcmUINT64_TO_PTR(kernelInterface->u.AllocateNonPagedMemory.logical)
            ));

        /* Free contiguous memory. */
        gcmkERR_BREAK(gckOS_FreeContiguous(
            Kernel->os,
            physical,
            gcmUINT64_TO_PTR(kernelInterface->u.AllocateNonPagedMemory.logical),
            (gctSIZE_T) kernelInterface->u.AllocateNonPagedMemory.bytes
            ));

        gcmRELEASE_NAME(kernelInterface->u.AllocateNonPagedMemory.physical);
        break;

    case gcvHAL_ALLOCATE_VIDEO_MEMORY:
        gcmkERR_BREAK(gcvSTATUS_NOT_SUPPORTED);
        break;

    case gcvHAL_MAP_MEMORY:
        /* Map memory. */
        gcmkERR_BREAK(gckKERNEL_MapMemory(
            Kernel,
            gcmINT2PTR(kernelInterface->u.MapMemory.physical),
            (gctSIZE_T) kernelInterface->u.MapMemory.bytes,
            &logical
            ));
        kernelInterface->u.MapMemory.logical = gcmPTR_TO_UINT64(logical);
        break;

    case gcvHAL_UNMAP_MEMORY:
        /* Unmap memory. */
        gcmkERR_BREAK(gckKERNEL_UnmapMemory(
            Kernel,
            gcmINT2PTR(kernelInterface->u.MapMemory.physical),
            (gctSIZE_T) kernelInterface->u.MapMemory.bytes,
            gcmUINT64_TO_PTR(kernelInterface->u.MapMemory.logical)
            ));
        break;

    case gcvHAL_MAP_USER_MEMORY:

        gcmkONERROR(gcvSTATUS_NOT_SUPPORTED);

        break;

    case gcvHAL_UNMAP_USER_MEMORY:

        gcmkONERROR(gcvSTATUS_NOT_SUPPORTED);

        break;

    case gcvHAL_LOCK_VIDEO_MEMORY:
        gcmkONERROR(gckKERNEL_LockVideoMemory(Kernel, gcvCORE_VG, processID, FromUser, Interface));
        break;

    case gcvHAL_USER_SIGNAL:
#if !USE_NEW_LINUX_SIGNAL
        /* Dispatch depends on the user signal subcommands. */
        switch(Interface->u.UserSignal.command)
        {
        case gcvUSER_SIGNAL_CREATE:
            /* Create a signal used in the user space. */
            gcmkERR_BREAK(
                gckOS_CreateUserSignal(Kernel->os,
                                       Interface->u.UserSignal.manualReset,
                                       &Interface->u.UserSignal.id));

            gcmkVERIFY_OK(
                gckKERNEL_AddProcessDB(Kernel,
                                       processID, gcvDB_SIGNAL,
                                       gcmINT2PTR(Interface->u.UserSignal.id),
                                       gcvNULL,
                                       0));
            break;

        case gcvUSER_SIGNAL_DESTROY:
            gcmkVERIFY_OK(gckKERNEL_RemoveProcessDB(
                Kernel,
                processID, gcvDB_SIGNAL,
                gcmINT2PTR(Interface->u.UserSignal.id)));

            /* Destroy the signal. */
            gcmkERR_BREAK(
                gckOS_DestroyUserSignal(Kernel->os,
                                        Interface->u.UserSignal.id));

            break;

        case gcvUSER_SIGNAL_SIGNAL:
            /* Signal the signal. */
            gcmkERR_BREAK(
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
            gcmkERR_BREAK(gcvSTATUS_INVALID_ARGUMENT);
        }
#endif
        break;

    case gcvHAL_COMMIT:
        /* Commit a command and context buffer. */
        gcmkERR_BREAK(gckVGCOMMAND_Commit(
            Kernel->vg->command,
            gcmUINT64_TO_PTR(kernelInterface->u.VGCommit.context),
            gcmUINT64_TO_PTR(kernelInterface->u.VGCommit.queue),
            kernelInterface->u.VGCommit.entryCount,
            gcmUINT64_TO_PTR(kernelInterface->u.VGCommit.taskTable)
            ));
        break;

    case gcvHAL_GET_BASE_ADDRESS:
       /* Get base address. */
        gcmkONERROR(
            gckOS_GetBaseAddress(Kernel->os,
                                 &Interface->u.GetBaseAddress.baseAddress));
        break;

    case gcvHAL_EVENT_COMMIT:
        gcmkERR_BREAK(gcvSTATUS_NOT_SUPPORTED);
        break;

    default:
        /* Invalid command, try gckKERNEL_Dispatch */
        status = gckKERNEL_Dispatch(Kernel, gcvTRUE, Interface);
    }

OnError:
    /* Save status. */
    kernelInterface->status = status;

    gcmkFOOTER();

    /* Return the status. */
    return status;
}

/*******************************************************************************
**
**  gckKERNEL_QueryCommandBuffer
**
**  Query command buffer attributes.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckVGHARDWARE object.
**
**  OUTPUT:
**
**      gcsCOMMAND_BUFFER_INFO_PTR Information
**          Pointer to the information structure to receive buffer attributes.
*/
gceSTATUS
gckKERNEL_QueryCommandBuffer(
    IN gckKERNEL Kernel,
    OUT gcsCOMMAND_BUFFER_INFO_PTR Information
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Kernel=0x%x *Pool=0x%x",
                   Kernel, Information);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);

    /* Get the information. */
    status = gckVGCOMMAND_QueryCommandBuffer(Kernel->vg->command, Information);

    gcmkFOOTER();
    /* Return status. */
    return status;
}

#endif /* gcdENABLE_VG */
