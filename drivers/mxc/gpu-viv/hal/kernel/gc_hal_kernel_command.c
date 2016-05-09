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
#include "gc_hal_kernel_context.h"

#define _GC_OBJ_ZONE            gcvZONE_COMMAND

/******************************************************************************\
********************************* Support Code *********************************
\******************************************************************************/

/*******************************************************************************
**
**  _NewQueue
**
**  Allocate a new command queue.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to an gckCOMMAND object.
**
**  OUTPUT:
**
**      gckCOMMAND Command
**          gckCOMMAND object has been updated with a new command queue.
*/
static gceSTATUS
_NewQueue(
    IN OUT gckCOMMAND Command
    )
{
    gceSTATUS status;
    gctINT currentIndex, newIndex;
    gctPHYS_ADDR_T physical;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Switch to the next command buffer. */
    currentIndex = Command->index;
    newIndex     = (currentIndex + 1) % gcdCOMMAND_QUEUES;

    /* Wait for availability. */
#if gcdDUMP_COMMAND
    gcmkPRINT("@[kernel.waitsignal]");
#endif

    gcmkONERROR(gckOS_WaitSignal(
        Command->os,
        Command->queues[newIndex].signal,
        gcvINFINITE
        ));

#if gcmIS_DEBUG(gcdDEBUG_TRACE)
    if (newIndex < currentIndex)
    {
        Command->wrapCount += 1;

        gcmkTRACE_ZONE_N(
            gcvLEVEL_INFO, gcvZONE_COMMAND,
            2 * 4,
            "%s(%d): queue array wrapped around.\n",
            __FUNCTION__, __LINE__
            );
    }

    gcmkTRACE_ZONE_N(
        gcvLEVEL_INFO, gcvZONE_COMMAND,
        3 * 4,
        "%s(%d): total queue wrap arounds %d.\n",
        __FUNCTION__, __LINE__, Command->wrapCount
        );

    gcmkTRACE_ZONE_N(
        gcvLEVEL_INFO, gcvZONE_COMMAND,
        3 * 4,
        "%s(%d): switched to queue %d.\n",
        __FUNCTION__, __LINE__, newIndex
        );
#endif

    /* Update gckCOMMAND object with new command queue. */
    Command->index         = newIndex;
    Command->newQueue      = gcvTRUE;
    Command->virtualMemory = Command->queues[newIndex].physical;
    Command->logical       = Command->queues[newIndex].logical;
    Command->address       = Command->queues[newIndex].address;
    Command->offset        = 0;

    gcmkONERROR(gckOS_GetPhysicalAddress(
        Command->os,
        Command->logical,
       &physical
        ));

    gcmkSAFECASTPHYSADDRT(Command->physical, physical);

    if (currentIndex != -1)
    {
        /* Mark the command queue as available. */
        gcmkONERROR(gckEVENT_Signal(
            Command->kernel->eventObj,
            Command->queues[currentIndex].signal,
            gcvKERNEL_COMMAND
            ));
    }

    /* Success. */
    gcmkFOOTER_ARG("Command->index=%d", Command->index);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

static gceSTATUS
_IncrementCommitAtom(
    IN gckCOMMAND Command,
    IN gctBOOL Increment
    )
{
    gceSTATUS status;
    gckHARDWARE hardware;
    gctINT32 atomValue;
    gctBOOL powerAcquired = gcvFALSE;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Extract the gckHARDWARE and gckEVENT objects. */
    hardware = Command->kernel->hardware;
    gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

    /* Grab the power mutex. */
    gcmkONERROR(gckOS_AcquireMutex(
        Command->os, hardware->powerMutex, gcvINFINITE
        ));
    powerAcquired = gcvTRUE;

    /* Increment the commit atom. */
    if (Increment)
    {
        gcmkONERROR(gckOS_AtomIncrement(
            Command->os, Command->atomCommit, &atomValue
            ));
    }
    else
    {
        gcmkONERROR(gckOS_AtomDecrement(
            Command->os, Command->atomCommit, &atomValue
            ));
    }

    /* Release the power mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(
        Command->os, hardware->powerMutex
        ));
    powerAcquired = gcvFALSE;

    /* Success. */
    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    if (powerAcquired)
    {
        /* Release the power mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(
            Command->os, hardware->powerMutex
            ));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

#if gcdSECURE_USER
static gceSTATUS
_ProcessHints(
    IN gckCOMMAND Command,
    IN gctUINT32 ProcessID,
    IN gcoCMDBUF CommandBuffer
    )
{
    gceSTATUS status = gcvSTATUS_OK;
    gckKERNEL kernel;
    gctBOOL needCopy = gcvFALSE;
    gcskSECURE_CACHE_PTR cache;
    gctUINT8_PTR commandBufferLogical;
    gctUINT8_PTR hintedData;
    gctUINT32_PTR hintArray;
    gctUINT i, hintCount;

    gcmkHEADER_ARG(
        "Command=0x%08X ProcessID=%d CommandBuffer=0x%08X",
        Command, ProcessID, CommandBuffer
        );

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    /* Reset state array pointer. */
    hintArray = gcvNULL;

    /* Get the kernel object. */
    kernel = Command->kernel;

    /* Get the cache form the database. */
    gcmkONERROR(gckKERNEL_GetProcessDBCache(kernel, ProcessID, &cache));

    /* Determine the start of the command buffer. */
    commandBufferLogical
        = (gctUINT8_PTR) CommandBuffer->logical
        +                CommandBuffer->startOffset;

    /* Determine the number of records in the state array. */
    hintCount = CommandBuffer->hintArrayTail - CommandBuffer->hintArray;

    /* Check wehther we need to copy the structures or not. */
    gcmkONERROR(gckOS_QueryNeedCopy(Command->os, ProcessID, &needCopy));

    /* Get access to the state array. */
    if (needCopy)
    {
        gctUINT copySize;

        if (Command->hintArrayAllocated &&
            (Command->hintArraySize < CommandBuffer->hintArraySize))
        {
            gcmkONERROR(gcmkOS_SAFE_FREE(Command->os, gcmUINT64_TO_PTR(Command->hintArray)));
            Command->hintArraySize = gcvFALSE;
        }

        if (!Command->hintArrayAllocated)
        {
            gctPOINTER pointer = gcvNULL;

            gcmkONERROR(gckOS_Allocate(
                Command->os,
                CommandBuffer->hintArraySize,
                &pointer
                ));

            Command->hintArray          = gcmPTR_TO_UINT64(pointer);
            Command->hintArrayAllocated = gcvTRUE;
            Command->hintArraySize      = CommandBuffer->hintArraySize;
        }

        hintArray = gcmUINT64_TO_PTR(Command->hintArray);
        copySize   = hintCount * gcmSIZEOF(gctUINT32);

        gcmkONERROR(gckOS_CopyFromUserData(
            Command->os,
            hintArray,
            gcmUINT64_TO_PTR(CommandBuffer->hintArray),
            copySize
            ));
    }
    else
    {
        gctPOINTER pointer = gcvNULL;

        gcmkONERROR(gckOS_MapUserPointer(
            Command->os,
            gcmUINT64_TO_PTR(CommandBuffer->hintArray),
            CommandBuffer->hintArraySize,
            &pointer
            ));

        hintArray = pointer;
    }

    /* Scan through the buffer. */
    for (i = 0; i < hintCount; i += 1)
    {
        /* Determine the location of the hinted data. */
        hintedData = commandBufferLogical + hintArray[i];

        /* Map handle into physical address. */
        gcmkONERROR(gckKERNEL_MapLogicalToPhysical(
            kernel, cache, (gctPOINTER) hintedData
            ));
    }

OnError:
    /* Get access to the state array. */
    if (!needCopy && (hintArray != gcvNULL))
    {
        gcmkVERIFY_OK(gckOS_UnmapUserPointer(
            Command->os,
            gcmUINT64_TO_PTR(CommandBuffer->hintArray),
            CommandBuffer->hintArraySize,
            hintArray
            ));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}
#endif

#if !gcdNULL_DRIVER
static gceSTATUS
_FlushMMU(
    IN gckCOMMAND Command
    )
{
#if gcdSECURITY
    return gcvSTATUS_OK;
#else
    gceSTATUS status;
    gctUINT32 oldValue;
    gckHARDWARE hardware = Command->kernel->hardware;
    gctBOOL pause = gcvFALSE;

    gctUINT8_PTR pointer;
    gctUINT32 eventBytes;
    gctUINT32 endBytes;
    gctUINT32 bufferSize;
    gctUINT32 executeBytes;
    gctUINT32 waitLinkBytes;

    gcmkONERROR(gckOS_AtomicExchange(Command->os,
                                     hardware->pageTableDirty,
                                     0,
                                     &oldValue));

    if (oldValue)
    {
        /* Page Table is upated, flush mmu before commit. */
        gcmkONERROR(gckHARDWARE_FlushMMU(hardware));

        if ((oldValue & gcvPAGE_TABLE_DIRTY_BIT_FE)
          && (!hardware->stallFEPrefetch)
        )
        {
            pause = gcvTRUE;
        }
    }

    if (pause)
    {
        /* Query size. */
        gcmkONERROR(gckHARDWARE_Event(hardware, gcvNULL, 0, gcvKERNEL_PIXEL, &eventBytes));
        gcmkONERROR(gckHARDWARE_End(hardware, gcvNULL, &endBytes));

        executeBytes = eventBytes + endBytes;

        gcmkONERROR(gckHARDWARE_WaitLink(
            hardware,
            gcvNULL,
            ~0U,
            Command->offset + executeBytes,
            &waitLinkBytes,
            gcvNULL,
            gcvNULL
            ));

        /* Reserve space. */
        gcmkONERROR(gckCOMMAND_Reserve(
            Command,
            executeBytes,
            (gctPOINTER *)&pointer,
            &bufferSize
            ));

        /* Append EVENT(29). */
        gcmkONERROR(gckHARDWARE_Event(
            hardware,
            pointer,
            29,
            gcvKERNEL_PIXEL,
            &eventBytes
            ));

        /* Append END. */
        pointer += eventBytes;
        gcmkONERROR(gckHARDWARE_End(hardware, pointer, &endBytes));

#if USE_KERNEL_VIRTUAL_BUFFERS
        if (hardware->kernel->virtualCommandBuffer)
        {
            gcmkONERROR(gckKERNEL_GetGPUAddress(
                hardware->kernel,
                pointer,
                gcvFALSE,
                Command->virtualMemory,
                &hardware->lastEnd
                ));
        }
#endif

        gcmkONERROR(gckCOMMAND_Execute(Command, executeBytes));
    }

    return gcvSTATUS_OK;
OnError:
    return status;
#endif
}
#endif

static gceSTATUS
_DummyDraw(
    IN gckCOMMAND Command
    )
{
#if gcdSECURITY
    return gcvSTATUS_OK;
#else
    gceSTATUS status;
    gckHARDWARE hardware = Command->kernel->hardware;

    gctUINT8_PTR pointer;
    gctUINT32 bufferSize;

    gctUINT32 dummyDrawBytes;

    gckHARDWARE_DummyDraw(hardware, gcvNULL, Command->queues[0].address, &dummyDrawBytes);

    /* Reserve space. */
    gcmkONERROR(gckCOMMAND_Reserve(
        Command,
        dummyDrawBytes,
        (gctPOINTER *)&pointer,
        &bufferSize
        ));

    gckHARDWARE_DummyDraw(hardware, pointer, Command->queues[0].address, &dummyDrawBytes);

    gcmkONERROR(gckCOMMAND_Execute(Command, dummyDrawBytes));

    return gcvSTATUS_OK;
OnError:
    return status;
#endif
}

static void
_DumpBuffer(
    IN gctPOINTER Buffer,
    IN gctUINT32 GpuAddress,
    IN gctSIZE_T Size
    )
{
    gctSIZE_T i, line, left;
    gctUINT32_PTR data = Buffer;

    line = Size / 32;
    left = Size % 32;

    for (i = 0; i < line; i++)
    {
        gcmkPRINT("%08X : %08X %08X %08X %08X %08X %08X %08X %08X",
                  GpuAddress, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
        data += 8;
        GpuAddress += 8 * 4;
    }

    switch(left)
    {
        case 28:
            gcmkPRINT("%08X : %08X %08X %08X %08X %08X %08X %08X",
                      GpuAddress, data[0], data[1], data[2], data[3], data[4], data[5], data[6]);
            break;
        case 24:
            gcmkPRINT("%08X : %08X %08X %08X %08X %08X %08X",
                      GpuAddress, data[0], data[1], data[2], data[3], data[4], data[5]);
            break;
        case 20:
            gcmkPRINT("%08X : %08X %08X %08X %08X %08X",
                      GpuAddress, data[0], data[1], data[2], data[3], data[4]);
            break;
        case 16:
            gcmkPRINT("%08X : %08X %08X %08X %08X",
                      GpuAddress, data[0], data[1], data[2], data[3]);
            break;
        case 12:
            gcmkPRINT("%08X : %08X %08X %08X",
                      GpuAddress, data[0], data[1], data[2]);
            break;
        case 8:
            gcmkPRINT("%08X : %08X %08X",
                      GpuAddress, data[0], data[1]);
            break;
        case 4:
            gcmkPRINT("%08X : %08X",
                      GpuAddress, data[0]);
            break;
        default:
            break;
    }
}

static void
_DumpKernelCommandBuffer(
    IN gckCOMMAND Command
    )
{
    gctINT i;
    gctUINT64 physical = 0;
    gctUINT32 address;
    gctPOINTER entry   = gcvNULL;

    for (i = 0; i < gcdCOMMAND_QUEUES; i++)
    {
        entry = Command->queues[i].logical;

        gckOS_GetPhysicalAddress(Command->os, entry, &physical);

        gcmkPRINT("Kernel command buffer %d\n", i);

        gcmkSAFECASTPHYSADDRT(address, physical);

        _DumpBuffer(entry, address, Command->pageSize);
    }
}

#if !gcdNULL_DRIVER
static gceSTATUS
_HandlePatchList(
    IN gckCOMMAND Command,
    IN gcoCMDBUF CommandBuffer,
    IN gctBOOL NeedCopy,
    OUT gctUINT64 *AsyncCommandStamp
    )
{
    gceSTATUS status;
    gcsPATCH_LIST * uList;
    gcsPATCH_LIST * previous;
    gcsPATCH_LIST * kList;
    gctUINT64 asyncStamp = 0;

    gcmkHEADER_ARG(
        "Command=0x%x CommandBuffer=0x%x NeedCopy=%d",
        Command, CommandBuffer, NeedCopy
        );

    uList = gcmUINT64_TO_PTR(CommandBuffer->patchHead);

    while (uList)
    {
        gctUINT i;

        kList = gcvNULL;
        previous = uList;

        gcmkONERROR(gckKERNEL_OpenUserData(
            Command->kernel,
            NeedCopy,
            Command->kList,
            uList,
            gcmSIZEOF(gcsPATCH_LIST),
            (gctPOINTER *)&kList
            ));

        for (i = 0; i < kList->count; i++)
        {
            gctUINT64 stamp = 0;
            gcsPATCH * patch = &kList->patch[i];

            /* Touch video memory node. */
            gcmkVERIFY_OK(gckVIDMEM_SetCommitStamp(Command->kernel, gcvENGINE_RENDER, patch->handle, Command->commitStamp));

            /* Get stamp touched async command buffer. */
            gcmkVERIFY_OK(gckVIDMEM_GetCommitStamp(Command->kernel, gcvENGINE_BLT, patch->handle, &stamp));

            /* Find latest one. */
            asyncStamp = gcmMAX(asyncStamp, stamp);
        }

        uList = kList->next;

        gcmkVERIFY_OK(gckKERNEL_CloseUserData(
            Command->kernel,
            NeedCopy,
            gcvFALSE,
            previous,
            gcmSIZEOF(gcsPATCH_LIST),
            (gctPOINTER *)&kList
            ));
    }

    if ((Command->asyncCommand != gcvNULL)
     && (*(gctUINT64 *)Command->asyncCommand->fence->logical > asyncStamp)
    )
    {
        /* No need to wait for async command buffer. */
        *AsyncCommandStamp = 0;
    }
    else
    {
        /* Need to add a fence wait. */
        *AsyncCommandStamp = asyncStamp;
    }

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (kList)
    {
        gcmkVERIFY_OK(gckKERNEL_CloseUserData(
            Command->kernel,
            NeedCopy,
            gcvFALSE,
            previous,
            gcmSIZEOF(gcsPATCH_LIST),
            (gctPOINTER *)&kList
            ));
    }

    gcmkFOOTER();
    return status;
}

static gceSTATUS
_WaitForAsyncCommandStamp(
    IN gckCOMMAND Command,
    IN gctUINT64 Stamp
    )
{
    gctUINT32 bytes;
    gceSTATUS status;
    gctUINT32 fenceAddress;
    gctUINT32 bufferSize;
    gctPOINTER pointer;
    gcmkHEADER_ARG("Stamp = 0x%llx", Stamp);

    fenceAddress = Command->asyncCommand->fence->address;

    gcmkONERROR(gckHARDWARE_WaitFence(Command->kernel->hardware,
        gcvNULL,
        Stamp,
        fenceAddress,
        &bytes
        ));

    gcmkONERROR(gckCOMMAND_Reserve(
        Command,
        bytes,
        &pointer,
        &bufferSize
        ));

    gcmkONERROR(gckHARDWARE_WaitFence(
        Command->kernel->hardware,
        pointer,
        Stamp,
        fenceAddress,
        &bytes
        ));

    gcmkONERROR(gckCOMMAND_Execute(Command, bytes));

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}
#endif

/******************************************************************************\
****************************** gckCOMMAND API Code ******************************
\******************************************************************************/

/*******************************************************************************
**
**  gckCOMMAND_Construct
**
**  Construct a new gckCOMMAND object.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**  OUTPUT:
**
**      gckCOMMAND * Command
**          Pointer to a variable that will hold the pointer to the gckCOMMAND
**          object.
*/
gceSTATUS
gckCOMMAND_Construct(
    IN gckKERNEL Kernel,
    OUT gckCOMMAND * Command
    )
{
    gckOS os;
    gckCOMMAND command = gcvNULL;
    gceSTATUS status;
    gctINT i;
    gctPOINTER pointer = gcvNULL;
    gctSIZE_T pageSize;

    gcmkHEADER_ARG("Kernel=0x%x", Kernel);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(Command != gcvNULL);

    /* Extract the gckOS object. */
    os = Kernel->os;

    /* Allocate the gckCOMMAND structure. */
    gcmkONERROR(gckOS_Allocate(os, gcmSIZEOF(struct _gckCOMMAND), &pointer));
    command = pointer;

    /* Reset the entire object. */
    gcmkONERROR(gckOS_ZeroMemory(command, gcmSIZEOF(struct _gckCOMMAND)));

    /* Initialize the gckCOMMAND object.*/
    command->object.type    = gcvOBJ_COMMAND;
    command->kernel         = Kernel;
    command->os             = os;

    /* Get the command buffer requirements. */
    gcmkONERROR(gckHARDWARE_QueryCommandBuffer(
        Kernel->hardware,
        gcvENGINE_RENDER,
        &command->alignment,
        &command->reservedHead,
        gcvNULL
        ));

    /* Create the command queue mutex. */
    gcmkONERROR(gckOS_CreateMutex(os, &command->mutexQueue));

    /* Create the context switching mutex. */
    gcmkONERROR(gckOS_CreateMutex(os, &command->mutexContext));

#if VIVANTE_PROFILER_CONTEXT
    /* Create the context switching mutex. */
    gcmkONERROR(gckOS_CreateMutex(os, &command->mutexContextSeq));
#endif

    /* Create the power management semaphore. */
    gcmkONERROR(gckOS_CreateSemaphore(os, &command->powerSemaphore));

    /* Create the commit atom. */
    gcmkONERROR(gckOS_AtomConstruct(os, &command->atomCommit));

    /* Get the page size from teh OS. */
    gcmkONERROR(gckOS_GetPageSize(os, &pageSize));

    gcmkSAFECASTSIZET(command->pageSize, pageSize);

    /* Get process ID. */
    gcmkONERROR(gckOS_GetProcessID(&command->kernelProcessID));

    /* Set hardware to pipe 0. */
    command->pipeSelect = gcvPIPE_INVALID;

    /* Pre-allocate the command queues. */
    for (i = 0; i < gcdCOMMAND_QUEUES; ++i)
    {
#if USE_KERNEL_VIRTUAL_BUFFERS
        if (Kernel->virtualCommandBuffer)
        {
            gcmkONERROR(gckKERNEL_AllocateVirtualCommandBuffer(
                Kernel,
                gcvFALSE,
                &pageSize,
                &command->queues[i].physical,
                &command->queues[i].logical
                ));

            gcmkONERROR(gckKERNEL_GetGPUAddress(
                Kernel,
                command->queues[i].logical,
                gcvFALSE,
                command->queues[i].physical,
                &command->queues[i].address
                ));
        }
        else
#endif
        {
            gcmkONERROR(gckOS_AllocateNonPagedMemory(
                os,
                gcvFALSE,
                &pageSize,
                &command->queues[i].physical,
                &command->queues[i].logical
                ));

            gcmkONERROR(gckHARDWARE_ConvertLogical(
                Kernel->hardware,
                command->queues[i].logical,
                gcvFALSE,
                &command->queues[i].address
                ));

            gcmkONERROR(gckMMU_FillFlatMapping(
                Kernel->mmu, command->queues[i].address, pageSize
                ));
        }

        gcmkONERROR(gckOS_CreateSignal(
            os, gcvFALSE, &command->queues[i].signal
            ));

        gcmkONERROR(gckOS_Signal(
            os, command->queues[i].signal, gcvTRUE
            ));
    }

#if gcdRECORD_COMMAND
    gcmkONERROR(gckRECORDER_Construct(os, Kernel->hardware, &command->recorder));
#endif

    gcmkONERROR(gckOS_Allocate(os, gcmSIZEOF(gcsPATCH_LIST), &command->kList));

    gcmkONERROR(gckFENCE_Create(
        os, Kernel, &command->fence
        ));

    /* No command queue in use yet. */
    command->index    = -1;
    command->logical  = gcvNULL;
    command->newQueue = gcvFALSE;

    /* Command is not yet running. */
    command->running = gcvFALSE;

    /* Command queue is idle. */
    command->idle = gcvTRUE;

    /* Commit stamp start from 1. */
    command->commitStamp = 1;

    /* END event signal not created. */
    command->endEventSignal = gcvNULL;

    /* Return pointer to the gckCOMMAND object. */
    *Command = command;

    /* Success. */
    gcmkFOOTER_ARG("*Command=0x%x", *Command);
    return gcvSTATUS_OK;

OnError:
    /* Roll back. */
    if (command != gcvNULL)
    {
        gcmkVERIFY_OK(gckCOMMAND_Destroy(command));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckCOMMAND_Destroy
**
**  Destroy an gckCOMMAND object.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to an gckCOMMAND object to destroy.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_Destroy(
    IN gckCOMMAND Command
    )
{
    gctINT i;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    /* Stop the command queue. */
    gcmkVERIFY_OK(gckCOMMAND_Stop(Command));

    for (i = 0; i < gcdCOMMAND_QUEUES; ++i)
    {
        if (Command->queues[i].signal)
        {
            gcmkVERIFY_OK(gckOS_DestroySignal(
                Command->os, Command->queues[i].signal
                ));
        }

        if (Command->queues[i].logical)
        {
#if USE_KERNEL_VIRTUAL_BUFFERS
            if (Command->kernel->virtualCommandBuffer)
            {
                gcmkVERIFY_OK(gckKERNEL_DestroyVirtualCommandBuffer(
                    Command->kernel,
                    Command->pageSize,
                    Command->queues[i].physical,
                    Command->queues[i].logical
                    ));
            }
            else
#endif
            {
                gcmkVERIFY_OK(gckOS_FreeNonPagedMemory(
                    Command->os,
                    Command->pageSize,
                    Command->queues[i].physical,
                    Command->queues[i].logical
                    ));
            }
        }
    }

    /* END event signal. */
    if (Command->endEventSignal != gcvNULL)
    {
        gcmkVERIFY_OK(gckOS_DestroySignal(
            Command->os, Command->endEventSignal
            ));
    }

    if (Command->mutexContext)
    {
        /* Delete the context switching mutex. */
        gcmkVERIFY_OK(gckOS_DeleteMutex(Command->os, Command->mutexContext));
    }

#if VIVANTE_PROFILER_CONTEXT
    if (Command->mutexContextSeq != gcvNULL)
        gcmkVERIFY_OK(gckOS_DeleteMutex(Command->os, Command->mutexContextSeq));
#endif

    if (Command->mutexQueue)
    {
        /* Delete the command queue mutex. */
        gcmkVERIFY_OK(gckOS_DeleteMutex(Command->os, Command->mutexQueue));
    }

    if (Command->powerSemaphore)
    {
        /* Destroy the power management semaphore. */
        gcmkVERIFY_OK(gckOS_DestroySemaphore(Command->os, Command->powerSemaphore));
    }

    if (Command->atomCommit)
    {
        /* Destroy the commit atom. */
        gcmkVERIFY_OK(gckOS_AtomDestroy(Command->os, Command->atomCommit));
    }

#if gcdSECURE_USER
    /* Free state array. */
    if (Command->hintArrayAllocated)
    {
        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Command->os, gcmUINT64_TO_PTR(Command->hintArray)));
        Command->hintArrayAllocated = gcvFALSE;
    }
#endif

#if gcdRECORD_COMMAND
    gckRECORDER_Destory(Command->os, Command->recorder);
#endif

    if (Command->stateMap)
    {
        gcmkOS_SAFE_FREE(Command->os, Command->stateMap);
    }

    if (Command->kList)
    {
        gcmkOS_SAFE_FREE(Command->os, Command->kList);
    }

    if (Command->fence)
    {
        gcmkVERIFY_OK(gckFENCE_Destory(Command->os, Command->fence));
    }

    /* Mark object as unknown. */
    Command->object.type = gcvOBJ_UNKNOWN;

    /* Free the gckCOMMAND object. */
    gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Command->os, Command));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckCOMMAND_EnterCommit
**
**  Acquire command queue synchronization objects.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to an gckCOMMAND object to destroy.
**
**      gctBOOL FromPower
**          Determines whether the call originates from inside the power
**          management or not.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_EnterCommit(
    IN gckCOMMAND Command,
    IN gctBOOL FromPower
    )
{
    gceSTATUS status;
    gckHARDWARE hardware;
    gctBOOL atomIncremented = gcvFALSE;
    gctBOOL semaAcquired = gcvFALSE;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Extract the gckHARDWARE and gckEVENT objects. */
    hardware = Command->kernel->hardware;
    gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

    if (!FromPower)
    {
        /* Increment COMMIT atom to let power management know that a commit is
        ** in progress. */
        gcmkONERROR(_IncrementCommitAtom(Command, gcvTRUE));
        atomIncremented = gcvTRUE;

        /* Notify the system the GPU has a commit. */
        gcmkONERROR(gckOS_Broadcast(Command->os,
                                    hardware,
                                    gcvBROADCAST_GPU_COMMIT));

        /* Acquire the power management semaphore. */
        gcmkONERROR(gckOS_AcquireSemaphore(Command->os,
                                           Command->powerSemaphore));
        semaAcquired = gcvTRUE;
    }

    /* Grab the conmmand queue mutex. */
    gcmkONERROR(gckOS_AcquireMutex(Command->os,
                                   Command->mutexQueue,
                                   gcvINFINITE));

    /* Success. */
    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    if (semaAcquired)
    {
        /* Release the power management semaphore. */
        gcmkVERIFY_OK(gckOS_ReleaseSemaphore(
            Command->os, Command->powerSemaphore
            ));
    }

    if (atomIncremented)
    {
        /* Decrement the commit atom. */
        gcmkVERIFY_OK(_IncrementCommitAtom(
            Command, gcvFALSE
            ));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckCOMMAND_ExitCommit
**
**  Release command queue synchronization objects.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to an gckCOMMAND object to destroy.
**
**      gctBOOL FromPower
**          Determines whether the call originates from inside the power
**          management or not.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_ExitCommit(
    IN gckCOMMAND Command,
    IN gctBOOL FromPower
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Release the power mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Command->os, Command->mutexQueue));

    if (!FromPower)
    {
        /* Release the power management semaphore. */
        gcmkONERROR(gckOS_ReleaseSemaphore(Command->os,
                                           Command->powerSemaphore));

        /* Decrement the commit atom. */
        gcmkONERROR(_IncrementCommitAtom(Command, gcvFALSE));
    }

    /* Success. */
    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckCOMMAND_Start
**
**  Start up the command queue.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to an gckCOMMAND object to start.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_Start(
    IN gckCOMMAND Command
    )
{
    gceSTATUS status;
    gckHARDWARE hardware;
    gctUINT32 waitOffset = 0;
    gctUINT32 waitLinkBytes;
    gctPOINTER logical;
    gctUINT32 physical;
    gctUINT32 address;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    if (Command->running)
    {
        /* Command queue already running. */
        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

    /* Extract the gckHARDWARE object. */
    hardware = Command->kernel->hardware;
    gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

    /* Query the size of WAIT/LINK command sequence. */
    gcmkONERROR(gckHARDWARE_WaitLink(
        hardware,
        gcvNULL,
        ~0U,
        Command->offset,
        &waitLinkBytes,
        gcvNULL,
        gcvNULL
        ));

    if ((Command->pageSize - Command->offset < waitLinkBytes)
     || (Command->logical == gcvNULL)
     )
    {
        /* Start at beginning of a new queue. */
        gcmkONERROR(_NewQueue(Command));
    }

    logical  = (gctUINT8_PTR) Command->logical + Command->offset;
    physical =                Command->physical + Command->offset;
    address  =                Command->address + Command->offset;

    /* Append WAIT/LINK. */
    gcmkONERROR(gckHARDWARE_WaitLink(
        hardware,
        logical,
        address,
        0,
        &waitLinkBytes,
        &waitOffset,
        &Command->waitSize
        ));

    Command->waitLogical  = (gctUINT8_PTR) logical  + waitOffset;
    Command->waitPhysical =                physical + waitOffset;

#if gcdNONPAGED_MEMORY_CACHEABLE
    /* Flush the cache for the wait/link. */
    gcmkONERROR(gckOS_CacheClean(
        Command->os,
        Command->kernelProcessID,
        gcvNULL,
        physical,
        logical,
        waitLinkBytes
        ));
#endif

    /* Adjust offset. */
    Command->offset   += waitLinkBytes;
    Command->newQueue = gcvFALSE;

#if gcdSECURITY
    /* Start FE by calling security service. */
    gckKERNEL_SecurityStartCommand(
        Command->kernel
        );
#else
    /* Enable command processor. */
    gcmkONERROR(gckHARDWARE_Execute(
        hardware,
        address,
        waitLinkBytes
        ));
#endif

    /* Command queue is running. */
    Command->running = gcvTRUE;

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
**  gckCOMMAND_Stop
**
**  Stop the command queue.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to an gckCOMMAND object to stop.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_Stop(
    IN gckCOMMAND Command
    )
{
    gckHARDWARE hardware;
    gceSTATUS status;
    gctUINT32 idle;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    if (!Command->running)
    {
        /* Command queue is not running. */
        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

    /* Extract the gckHARDWARE object. */
    hardware = Command->kernel->hardware;
    gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

    if (gckHARDWARE_IsFeatureAvailable(hardware,
                                       gcvFEATURE_END_EVENT) == gcvSTATUS_TRUE)
    {
        /* Allocate the signal. */
        if (Command->endEventSignal == gcvNULL)
        {
            gcmkONERROR(gckOS_CreateSignal(Command->os,
                                           gcvTRUE,
                                           &Command->endEventSignal));
        }

        /* Append the END EVENT command to trigger the signal. */
        gcmkONERROR(gckEVENT_Stop(Command->kernel->eventObj,
                                  Command->kernelProcessID,
                                  Command->waitPhysical,
                                  Command->waitLogical,
                                  Command->endEventSignal,
                                  &Command->waitSize));
    }
    else
    {
        /* Replace last WAIT with END. */
        gcmkONERROR(gckHARDWARE_End(
            hardware, Command->waitLogical, &Command->waitSize
            ));

#if USE_KERNEL_VIRTUAL_BUFFERS
        if (hardware->kernel->virtualCommandBuffer)
        {
            gcmkONERROR(gckKERNEL_GetGPUAddress(
                hardware->kernel,
                Command->waitLogical,
                gcvFALSE,
                Command->virtualMemory,
                &hardware->lastEnd
                ));
        }
#endif

#if gcdSECURITY
        gcmkONERROR(gckKERNEL_SecurityExecute(
            Command->kernel, Command->waitLogical, 8
            ));
#endif

        /* Update queue tail pointer. */
        gcmkONERROR(gckHARDWARE_UpdateQueueTail(Command->kernel->hardware,
                                                Command->logical,
                                                Command->offset));

#if gcdNONPAGED_MEMORY_CACHEABLE
        /* Flush the cache for the END. */
        gcmkONERROR(gckOS_CacheClean(
            Command->os,
            Command->kernelProcessID,
            gcvNULL,
            (gctUINT32)Command->waitPhysical,
            Command->waitLogical,
            Command->waitSize
            ));
#endif

        /* Wait for idle. */
        gcmkONERROR(gckHARDWARE_GetIdle(hardware, gcvTRUE, &idle));
    }

    /* Command queue is no longer running. */
    Command->running = gcvFALSE;

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
**  gckCOMMAND_Commit
**
**  Commit a command buffer to the command queue.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to a gckCOMMAND object.
**
**      gckCONTEXT Context
**          Pointer to a gckCONTEXT object.
**
**      gcoCMDBUF CommandBuffer
**          Pointer to a gcoCMDBUF object.
**
**      gcsSTATE_DELTA_PTR StateDelta
**          Pointer to the state delta.
**
**      gctUINT32 ProcessID
**          Current process ID.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_Commit(
    IN gckCOMMAND Command,
    IN gckCONTEXT Context,
    IN gcoCMDBUF CommandBuffer,
    IN gcsSTATE_DELTA_PTR StateDelta,
    IN gcsQUEUE_PTR EventQueue,
    IN gctUINT32 ProcessID,
    IN gctBOOL Shared,
    IN gctUINT32 Index
    )
{
    gceSTATUS status;
    gctBOOL commitEntered = gcvFALSE;
    gctBOOL contextAcquired = gcvFALSE;
    gckHARDWARE hardware;
    gctBOOL needCopy = gcvFALSE;
    gcsQUEUE_PTR eventRecord = gcvNULL;
    gcsQUEUE _eventRecord;
    gcsQUEUE_PTR nextEventRecord;
    gctBOOL commandBufferMapped = gcvFALSE;
    gcoCMDBUF commandBufferObject = gcvNULL;
    gctBOOL stall = gcvFALSE;

#if !gcdNULL_DRIVER
    gcsCONTEXT_PTR contextBuffer;
    struct _gcoCMDBUF _commandBufferObject;
    gctPHYS_ADDR_T commandBufferPhysical;
    gctUINT8_PTR commandBufferLogical = gcvNULL;
    gctUINT32 commandBufferAddress = 0;
    gctUINT8_PTR commandBufferLink = gcvNULL;
    gctUINT8_PTR commandBufferTail = gcvNULL;
    gctUINT8_PTR commandBufferFence = gcvNULL;
    gctUINT commandBufferSize;
    gctSIZE_T nopBytes;
    gctUINT32 pipeBytes;
    gctUINT32 linkBytes;
    gctSIZE_T bytes;
    gctUINT32 offset;
#if gcdNONPAGED_MEMORY_CACHEABLE
    gctPHYS_ADDR entryPhysical;
#endif
    gctPOINTER entryLogical;
    gctUINT32 entryAddress;
    gctUINT32 entryBytes;
#if gcdNONPAGED_MEMORY_CACHEABLE
    gctPHYS_ADDR exitPhysical;
    gctPOINTER exitLogical;
#endif
    gctUINT32 exitAddress;
    gctUINT32 exitBytes;
    gctUINT32 waitLinkPhysical;
    gctPOINTER waitLinkLogical;
    gctUINT32 waitLinkAddress;
    gctUINT32 waitLinkBytes;
    gctUINT32 waitPhysical;
    gctPOINTER waitLogical;
    gctUINT32 waitOffset;
    gctUINT32 waitSize;

#ifdef __QNXNTO__
    gctPOINTER userCommandBufferLogical       = gcvNULL;
    gctBOOL    userCommandBufferLogicalMapped = gcvFALSE;
#endif

#if gcdPROCESS_ADDRESS_SPACE
    gckMMU mmu;
    gctUINT32 oldValue;
#endif

#if gcdDUMP_COMMAND
    gctPOINTER contextDumpLogical = gcvNULL;
    gctSIZE_T contextDumpBytes = 0;
    gctPOINTER bufferDumpLogical = gcvNULL;
    gctSIZE_T bufferDumpBytes = 0;
# endif
    gctUINT32 exitLinkLow = 0, exitLinkHigh = 0;
    gctUINT32 entryLinkLow = 0, entryLinkHigh = 0;
    gctUINT32 commandLinkLow = 0, commandLinkHigh = 0;

    gckVIRTUAL_COMMAND_BUFFER_PTR virtualCommandBuffer = gcvNULL;
    gctUINT64 asyncCommandStamp = 0;
#endif

    gctPOINTER pointer = gcvNULL;

    gcmkHEADER_ARG(
        "Command=0x%x CommandBuffer=0x%x ProcessID=%d",
        Command, CommandBuffer, ProcessID
        );

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

#if gcdPROCESS_ADDRESS_SPACE
    gcmkONERROR(gckKERNEL_GetProcessMMU(Command->kernel, &mmu));

    gcmkONERROR(gckOS_AtomicExchange(Command->os,
                                     mmu->pageTableDirty[Command->kernel->core],
                                     0,
                                     &oldValue));
#else
#endif

    /* Acquire the command queue. */
    gcmkONERROR(gckCOMMAND_EnterCommit(Command, gcvFALSE));
    commitEntered = gcvTRUE;

    /* Acquire the context switching mutex. */
    gcmkONERROR(gckOS_AcquireMutex(
        Command->os, Command->mutexContext, gcvINFINITE
        ));
    contextAcquired = gcvTRUE;

    /* Extract the gckHARDWARE and gckEVENT objects. */
    hardware = Command->kernel->hardware;

    /* Check wehther we need to copy the structures or not. */
    gcmkONERROR(gckOS_QueryNeedCopy(Command->os, ProcessID, &needCopy));

#if gcdNULL_DRIVER
    /* Context switch required? */
    if ((Context != gcvNULL) && (Command->currContext != Context))
    {
        /* Yes, merge in the deltas. */
        gckCONTEXT_Update(Context, ProcessID, StateDelta);

        /* Update the current context. */
        Command->currContext = Context;
    }
#else
    if (needCopy)
    {
        commandBufferObject = &_commandBufferObject;

        gcmkONERROR(gckOS_CopyFromUserData(
            Command->os,
            commandBufferObject,
            CommandBuffer,
            gcmSIZEOF(struct _gcoCMDBUF)
            ));

        gcmkVERIFY_OBJECT(commandBufferObject, gcvOBJ_COMMANDBUFFER);
    }
    else
    {
        gcmkONERROR(gckOS_MapUserPointer(
            Command->os,
            CommandBuffer,
            gcmSIZEOF(struct _gcoCMDBUF),
            &pointer
            ));

        commandBufferObject = pointer;

        gcmkVERIFY_OBJECT(commandBufferObject, gcvOBJ_COMMANDBUFFER);
        commandBufferMapped = gcvTRUE;
    }

    gcmkONERROR(_HandlePatchList(Command, commandBufferObject, needCopy, &asyncCommandStamp));

    /* Query the size of NOP command. */
    gcmkONERROR(gckHARDWARE_Nop(
        hardware, gcvNULL, &nopBytes
        ));

    /* Query the size of pipe select command sequence. */
    gcmkONERROR(gckHARDWARE_PipeSelect(
        hardware, gcvNULL, gcvPIPE_3D, &pipeBytes
        ));

    /* Query the size of LINK command. */
    gcmkONERROR(gckHARDWARE_Link(
        hardware, gcvNULL, 0, 0, &linkBytes, gcvNULL, gcvNULL
        ));

    /* Compute the command buffer entry and the size. */
    commandBufferLogical
        = (gctUINT8_PTR) gcmUINT64_TO_PTR(commandBufferObject->logical)
        +                commandBufferObject->startOffset;

    /* Get the hardware address. */
    if (Command->kernel->virtualCommandBuffer)
    {
        gckKERNEL kernel = Command->kernel;

        virtualCommandBuffer = gcmNAME_TO_PTR(commandBufferObject->physical);

        if (virtualCommandBuffer == gcvNULL)
        {
            gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }

        gcmkONERROR(gckKERNEL_GetGPUAddress(
            Command->kernel,
            commandBufferLogical,
            gcvTRUE,
            virtualCommandBuffer,
            &commandBufferAddress
            ));
    }
    else
    {
        gcmkONERROR(gckHARDWARE_ConvertLogical(
            hardware,
            commandBufferLogical,
            gcvTRUE,
            &commandBufferAddress
            ));
    }

    /* Get the physical address. */
    gcmkONERROR(gckOS_UserLogicalToPhysical(
        Command->os,
        commandBufferLogical,
        &commandBufferPhysical
        ));

#ifdef __QNXNTO__
    userCommandBufferLogical = (gctPOINTER) commandBufferLogical;

    gcmkONERROR(gckOS_MapUserPointer(
        Command->os,
        userCommandBufferLogical,
        0,
        &pointer));

    commandBufferLogical = pointer;

    userCommandBufferLogicalMapped = gcvTRUE;
#endif

    commandBufferSize
        = commandBufferObject->offset
        + commandBufferObject->reservedTail
        - commandBufferObject->startOffset;

    gcmkONERROR(_FlushMMU(Command));

    if (gckHARDWARE_IsFeatureAvailable(hardware, gcvFEATURE_FENCE) && asyncCommandStamp != 0)
    {
        gcmkONERROR(_WaitForAsyncCommandStamp(Command, asyncCommandStamp));
    }

    if (gckHARDWARE_IsFeatureAvailable(hardware, gcvFEATURE_FE_NEED_DUMMYDRAW) && Command->currContext == gcvNULL)
    {
        gcmkONERROR(_DummyDraw(Command));
    }

    /* Get the current offset. */
    offset = Command->offset;

    /* Compute number of bytes left in current kernel command queue. */
    bytes = Command->pageSize - offset;

    /* Query the size of WAIT/LINK command sequence. */
    gcmkONERROR(gckHARDWARE_WaitLink(
        hardware,
        gcvNULL,
        ~0U,
        offset,
        &waitLinkBytes,
        gcvNULL,
        gcvNULL
        ));

    /* Is there enough space in the current command queue? */
    if (bytes < waitLinkBytes)
    {
        /* No, create a new one. */
        gcmkONERROR(_NewQueue(Command));

        /* Get the new current offset. */
        offset = Command->offset;

        /* Recompute the number of bytes in the new kernel command queue. */
        bytes = Command->pageSize - offset;
        gcmkASSERT(bytes >= waitLinkBytes);
    }

    /* Compute the location if WAIT/LINK command sequence. */
    waitLinkPhysical =                Command->physical + offset;
    waitLinkLogical  = (gctUINT8_PTR) Command->logical  + offset;
    waitLinkAddress  =                Command->address  + offset;

    /* Context switch required? */
    if (Context == gcvNULL)
    {
        /* See if we have to switch pipes for the command buffer. */
        if (commandBufferObject->entryPipe == Command->pipeSelect)
        {
            /* Skip pipe switching sequence. */
            offset = pipeBytes;
        }
        else
        {
            /* The current hardware and the entry command buffer pipes
            ** are different, switch to the correct pipe. */
            gcmkONERROR(gckHARDWARE_PipeSelect(
                Command->kernel->hardware,
                commandBufferLogical,
                commandBufferObject->entryPipe,
                &pipeBytes
                ));

            /* Do not skip pipe switching sequence. */
            offset = 0;
        }

        /* Compute the entry. */
#if gcdNONPAGED_MEMORY_CACHEABLE
        entryPhysical = (gctUINT8_PTR) commandBufferPhysical + offset;
#endif
        entryLogical  =                commandBufferLogical  + offset;
        entryAddress  =                commandBufferAddress  + offset;
        entryBytes    =                commandBufferSize     - offset;

        Command->currContext = gcvNULL;
    }
#if gcdDEBUG_OPTION && gcdDEBUG_FORCE_CONTEXT_UPDATE
    else if (1)
#else
    else if (Command->currContext != Context)
#endif
    {
        /* Get the current context buffer. */
        contextBuffer = Context->buffer;

        /* Yes, merge in the deltas. */
        gcmkONERROR(gckCONTEXT_Update(Context, ProcessID, StateDelta));

        /***************************************************************
        ** SWITCHING CONTEXT.
        */

        /* Determine context buffer entry offset. */
        offset = (Command->pipeSelect == gcvPIPE_3D)

            /* Skip pipe switching sequence. */
            ? Context->entryOffset3D + Context->pipeSelectBytes

            /* Do not skip pipe switching sequence. */
            : Context->entryOffset3D;

        /* Compute the entry. */
#if gcdNONPAGED_MEMORY_CACHEABLE
        entryPhysical = (gctUINT8_PTR) contextBuffer->physical + offset;
#endif
        entryLogical  = (gctUINT8_PTR) contextBuffer->logical  + offset;
        entryAddress  =                contextBuffer->address  + offset;
        entryBytes    =                Context->bufferSize     - offset;

        /* See if we have to switch pipes between the context
            and command buffers. */
        if (commandBufferObject->entryPipe == gcvPIPE_3D)
        {
            /* Skip pipe switching sequence. */
            offset = pipeBytes;
        }
        else
        {
            /* The current hardware and the initial context pipes are
                different, switch to the correct pipe. */
            gcmkONERROR(gckHARDWARE_PipeSelect(
                Command->kernel->hardware,
                commandBufferLogical,
                commandBufferObject->entryPipe,
                &pipeBytes
                ));

            /* Do not skip pipe switching sequence. */
            offset = 0;
        }

        /* Generate a LINK from the context buffer to
            the command buffer. */
        gcmkONERROR(gckHARDWARE_Link(
            hardware,
            contextBuffer->link3D,
            commandBufferAddress + offset,
            commandBufferSize    - offset,
            &linkBytes,
            &commandLinkLow,
            &commandLinkHigh
            ));

#if gcdNONPAGED_MEMORY_CACHEABLE
        /* Flush the context buffer cache. */
        gcmkONERROR(gckOS_CacheClean(
            Command->os,
            Command->kernelProcessID,
            gcvNULL,
            (gctUINT32)entryPhysical,
            entryLogical,
            entryBytes
            ));
#endif

        /* Update the current context. */
        Command->currContext = Context;

#if gcdDUMP_COMMAND
        contextDumpLogical = entryLogical;
        contextDumpBytes   = entryBytes;
#endif

#if gcdSECURITY
        /* Commit context buffer to trust zone. */
        gckKERNEL_SecurityExecute(
            Command->kernel,
            entryLogical,
            entryBytes - 8
            );
#endif

#if gcdRECORD_COMMAND
        gckRECORDER_Record(
            Command->recorder,
            gcvNULL,
            0xFFFFFFFF,
            entryLogical,
            entryBytes
            );
#endif
    }

    /* Same context. */
    else
    {
        /* See if we have to switch pipes for the command buffer. */
        if (commandBufferObject->entryPipe == Command->pipeSelect)
        {
            /* Skip pipe switching sequence. */
            offset = pipeBytes;
        }
        else
        {
            /* The current hardware and the entry command buffer pipes
            ** are different, switch to the correct pipe. */
            gcmkONERROR(gckHARDWARE_PipeSelect(
                Command->kernel->hardware,
                commandBufferLogical,
                commandBufferObject->entryPipe,
                &pipeBytes
                ));

            /* Do not skip pipe switching sequence. */
            offset = 0;
        }

        /* Compute the entry. */
#if gcdNONPAGED_MEMORY_CACHEABLE
        entryPhysical = (gctUINT8_PTR) commandBufferPhysical + offset;
#endif
        entryLogical  =                commandBufferLogical  + offset;
        entryAddress  =                commandBufferAddress  + offset;
        entryBytes    =                commandBufferSize     - offset;
    }

#if gcdDUMP_COMMAND
    bufferDumpLogical = commandBufferLogical + offset;
    bufferDumpBytes   = commandBufferSize    - offset;
#endif

#if gcdSECURE_USER
    /* Process user hints. */
    gcmkONERROR(_ProcessHints(Command, ProcessID, commandBufferObject));
#endif

    /* Determine the location to jump to for the command buffer being
    ** scheduled. */
    if (Command->newQueue)
    {
        /* New command queue, jump to the beginning of it. */
#if gcdNONPAGED_MEMORY_CACHEABLE
        exitPhysical = Command->physical;
        exitLogical  = Command->logical;
#endif
        exitAddress  = Command->address;
        exitBytes    = Command->offset + waitLinkBytes;
    }
    else
    {
        /* Still within the preexisting command queue, jump to the new
           WAIT/LINK command sequence. */
#if gcdNONPAGED_MEMORY_CACHEABLE
        exitPhysical = waitLinkPhysical;
        exitLogical  = waitLinkLogical;
#endif
        exitAddress  = waitLinkAddress;
        exitBytes    = waitLinkBytes;
    }

    /* Add a new WAIT/LINK command sequence. When the command buffer which is
       currently being scheduled is fully executed by the GPU, the FE will
       jump to this WAIT/LINK sequence. */
    gcmkONERROR(gckHARDWARE_WaitLink(
        hardware,
        waitLinkLogical,
        waitLinkAddress,
        offset,
        &waitLinkBytes,
        &waitOffset,
        &waitSize
        ));

    /* Compute the location if WAIT command. */
    waitPhysical =                waitLinkPhysical + waitOffset;
    waitLogical  = (gctUINT8_PTR) waitLinkLogical  + waitOffset;

#if gcdNONPAGED_MEMORY_CACHEABLE
    /* Flush the command queue cache. */
    gcmkONERROR(gckOS_CacheClean(
        Command->os,
        Command->kernelProcessID,
        gcvNULL,
        (gctUINT32)exitPhysical,
        exitLogical,
        exitBytes
        ));
#endif

    /* Determine the location of the TAIL in the command buffer. */
    commandBufferTail
        = commandBufferLogical
        + commandBufferSize
        - commandBufferObject->reservedTail;

    if (gckHARDWARE_IsFeatureAvailable(hardware, gcvFEATURE_FENCE))
    {
        /* Determine the location of the FENCE command in the command buffer. */
        commandBufferFence = commandBufferTail;

        /* Determine the location of the LINK command in the command buffer. */
        commandBufferLink = commandBufferTail + gcdRENDER_FENCE_LENGTH;
    }
    else
    {
        /* Determine the location of the LINK command in the command buffer. */
        commandBufferLink = commandBufferTail;
    }

    /* Generate command which writes out commit stamp. */
    if (commandBufferFence)
    {
        gctUINT32 bytes;

        gcmkONERROR(gckHARDWARE_Fence(
            hardware,
            gcvENGINE_RENDER,
            commandBufferFence,
            Command->fence->address,
            Command->commitStamp,
            &bytes
            ));
    }

    /* Generate a LINK from the end of the command buffer being scheduled
       back to the kernel command queue. */
#if !gcdSECURITY
    if (Shared == gcvFALSE)
    {
        gcmkONERROR(gckHARDWARE_Link(
            hardware,
            commandBufferLink,
            exitAddress,
            exitBytes,
            &linkBytes,
            &exitLinkLow,
            &exitLinkHigh
            ));
    }
    else
    {
        gctUINT8_PTR link = commandBufferLink + Index * 16;
        gctSIZE_T bytes = 8;

        gcmkONERROR(gckHARDWARE_ChipEnable(
            hardware,
            link,
            (gceCORE_3D_MASK)(1 << hardware->kernel->chipID),
            &bytes
            ));

        link += bytes;

        gcmkONERROR(gckHARDWARE_Link(
            hardware,
            link,
            exitAddress,
            exitBytes,
            &linkBytes,
            &exitLinkLow,
            &exitLinkHigh
            ));

        link += linkBytes;
    }
#endif

#if gcdNONPAGED_MEMORY_CACHEABLE
    /* Flush the command buffer cache. */
    gcmkONERROR(gckOS_CacheClean(
        Command->os,
        ProcessID,
        gcvNULL,
        (gctUINT32)commandBufferPhysical,
        commandBufferLogical,
        commandBufferSize
        ));
#endif

#if gcdRECORD_COMMAND
    gckRECORDER_Record(
        Command->recorder,
        commandBufferLogical + offset,
        commandBufferSize - offset,
        gcvNULL,
        0xFFFFFFFF
        );

    gckRECORDER_AdvanceIndex(Command->recorder, Command->commitStamp);
#endif

#if gcdSECURITY
    /* Submit command buffer to trust zone. */
    gckKERNEL_SecurityExecute(
        Command->kernel,
        commandBufferLogical + offset,
        commandBufferSize    - offset - 8
        );
#else
    /* Generate a LINK from the previous WAIT/LINK command sequence to the
       entry determined above (either the context or the command buffer).
       This LINK replaces the WAIT instruction from the previous WAIT/LINK
       pair, therefore we use WAIT metrics for generation of this LINK.
       This action will execute the entire sequence. */
    gcmkONERROR(gckHARDWARE_Link(
        hardware,
        Command->waitLogical,
        entryAddress,
        entryBytes,
        &Command->waitSize,
        &entryLinkLow,
        &entryLinkHigh
        ));
#endif

#if gcdLINK_QUEUE_SIZE
    if (Command->kernel->stuckDump >= gcvSTUCK_DUMP_USER_COMMAND)
    {
        gcuQUEUEDATA data;

        gcmkVERIFY_OK(gckOS_GetProcessID(&data.linkData.pid));

        data.linkData.start    = entryAddress;
        data.linkData.end      = entryAddress + entryBytes;
        data.linkData.linkLow  = entryLinkLow;
        data.linkData.linkHigh = entryLinkHigh;

        gckQUEUE_Enqueue(&hardware->linkQueue, &data);

        if (commandBufferAddress + offset != entryAddress)
        {
             data.linkData.start    =  commandBufferAddress + offset;
             data.linkData.end      =  commandBufferAddress + commandBufferSize;
             data.linkData.linkLow  = commandLinkLow;
             data.linkData.linkHigh = commandLinkHigh;

            gckQUEUE_Enqueue(&hardware->linkQueue, &data);
        }

        if (Command->kernel->stuckDump >= gcvSTUCK_DUMP_ALL_COMMAND)
        {
            data.linkData.start    = exitAddress;
            data.linkData.end      = exitAddress + exitBytes;
            data.linkData.linkLow  = exitLinkLow;
            data.linkData.linkHigh = exitLinkHigh;

            /* Dump kernel command.*/
            gckQUEUE_Enqueue(&hardware->linkQueue, &data);
        }
    }
#endif

#if gcdNONPAGED_MEMORY_CACHEABLE
    /* Flush the cache for the link. */
    gcmkONERROR(gckOS_CacheClean(
        Command->os,
        Command->kernelProcessID,
        gcvNULL,
        (gctUINT32)Command->waitPhysical,
        Command->waitLogical,
        Command->waitSize
        ));
#endif

    gcmkDUMPCOMMAND(
        Command->os,
        Command->waitLogical,
        Command->waitSize,
        gceDUMP_BUFFER_LINK,
        gcvFALSE
        );

    gcmkDUMPCOMMAND(
        Command->os,
        contextDumpLogical,
        contextDumpBytes,
        gceDUMP_BUFFER_CONTEXT,
        gcvFALSE
        );

    gcmkDUMPCOMMAND(
        Command->os,
        bufferDumpLogical,
        bufferDumpBytes,
        gceDUMP_BUFFER_USER,
        gcvFALSE
        );

    gcmkDUMPCOMMAND(
        Command->os,
        waitLinkLogical,
        waitLinkBytes,
        gceDUMP_BUFFER_WAITLINK,
        gcvFALSE
        );

    /* Update the current pipe. */
    Command->pipeSelect = commandBufferObject->exitPipe;

    /* Update command queue offset. */
    Command->offset  += waitLinkBytes;
    Command->newQueue = gcvFALSE;

    /* Update address of last WAIT. */
    Command->waitPhysical = waitPhysical;
    Command->waitLogical  = waitLogical;
    Command->waitSize     = waitSize;

    /* Update queue tail pointer. */
    gcmkONERROR(gckHARDWARE_UpdateQueueTail(
        hardware, Command->logical, Command->offset
        ));

#if gcdDUMP_COMMAND
    gcmkPRINT("@[kernel.commit]");
#endif
#endif /* gcdNULL_DRIVER */

    /* Release the context switching mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Command->os, Command->mutexContext));
    contextAcquired = gcvFALSE;

    Command->commitStamp++;

    stall = gcvFALSE;

#if gcdLINK_QUEUE_SIZE
    if (Command->kernel->stuckDump == gcvSTUCK_DUMP_STALL_COMMAND)
    {
        if ((Command->commitStamp % (gcdLINK_QUEUE_SIZE/2)) == 0)
        {
            /* If only context buffer and command buffer is recorded,
            ** each commit costs 2 slot in queue, to make sure command
            ** causing stuck is recorded, number of pending command buffer
            ** is limited to (gckLINK_QUEUE_SIZE/2)
            */
            stall = gcvTRUE;
        }
    }
#endif

    /* Release the command queue. */
    gcmkONERROR(gckCOMMAND_ExitCommit(Command, gcvFALSE));
    commitEntered = gcvFALSE;


    /* Loop while there are records in the queue. */
    while (EventQueue != gcvNULL)
    {
        if (needCopy)
        {
            /* Point to stack record. */
            eventRecord = &_eventRecord;

            /* Copy the data from the client. */
            gcmkONERROR(gckOS_CopyFromUserData(
                Command->os, eventRecord, EventQueue, gcmSIZEOF(gcsQUEUE)
                ));
        }
        else
        {
            /* Map record into kernel memory. */
            gcmkONERROR(gckOS_MapUserPointer(Command->os,
                                             EventQueue,
                                             gcmSIZEOF(gcsQUEUE),
                                             &pointer));

            eventRecord = pointer;
        }

        /* Append event record to event queue. */
        gcmkONERROR(gckEVENT_AddList(
            Command->kernel->eventObj, &eventRecord->iface, gcvKERNEL_PIXEL, gcvTRUE, gcvFALSE
            ));

        /* Next record in the queue. */
        nextEventRecord = gcmUINT64_TO_PTR(eventRecord->next);

        if (!needCopy)
        {
            /* Unmap record from kernel memory. */
            gcmkONERROR(gckOS_UnmapUserPointer(
                Command->os, EventQueue, gcmSIZEOF(gcsQUEUE), (gctPOINTER *) eventRecord
                ));

            eventRecord = gcvNULL;
        }

        EventQueue = nextEventRecord;
    }

    if ((Command->kernel->eventObj->queueHead == gcvNULL
      && Command->kernel->hardware->powerManagement == gcvTRUE)
    || (Command->kernel->hardware->gpuProfiler == gcvTRUE
      && Command->kernel->profileEnable == gcvTRUE)
    )
    {
        /* Commit done event by which work thread knows all jobs done. */
        gcmkVERIFY_OK(
            gckEVENT_CommitDone(Command->kernel->eventObj, gcvKERNEL_PIXEL, Command->currContext));
    }

    /* Submit events. */
    status = gckEVENT_Submit(Command->kernel->eventObj, gcvTRUE, gcvFALSE);
    if (status == gcvSTATUS_INTERRUPTED)
    {
        gcmkTRACE(
            gcvLEVEL_INFO,
            "%s(%d): Intterupted in gckEVENT_Submit",
            __FUNCTION__, __LINE__
            );
        status = gcvSTATUS_OK;
    }
    else
    {
        gcmkONERROR(status);
    }

#ifdef __QNXNTO__
    if (userCommandBufferLogicalMapped)
    {
        gcmkONERROR(gckOS_UnmapUserPointer(
            Command->os,
            userCommandBufferLogical,
            0,
            commandBufferLogical));

        userCommandBufferLogicalMapped = gcvFALSE;
    }
#endif

    /* Unmap the command buffer pointer. */
    if (commandBufferMapped)
    {
        gcmkONERROR(gckOS_UnmapUserPointer(
            Command->os,
            CommandBuffer,
            gcmSIZEOF(struct _gcoCMDBUF),
            commandBufferObject
            ));

        commandBufferMapped = gcvFALSE;
    }

    /* Return status. */
    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    if ((eventRecord != gcvNULL) && !needCopy)
    {
        /* Roll back. */
        gcmkVERIFY_OK(gckOS_UnmapUserPointer(
            Command->os,
            EventQueue,
            gcmSIZEOF(gcsQUEUE),
            (gctPOINTER *) eventRecord
            ));
    }

    if (contextAcquired)
    {
        /* Release the context switching mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Command->os, Command->mutexContext));
    }

    if (commitEntered)
    {
        /* Release the command queue mutex. */
        gcmkVERIFY_OK(gckCOMMAND_ExitCommit(Command, gcvFALSE));
    }

#ifdef __QNXNTO__
    if (userCommandBufferLogicalMapped)
    {
        gcmkVERIFY_OK(gckOS_UnmapUserPointer(
            Command->os,
            userCommandBufferLogical,
            0,
            commandBufferLogical));
    }
#endif

    /* Unmap the command buffer pointer. */
    if (commandBufferMapped)
    {
        gcmkVERIFY_OK(gckOS_UnmapUserPointer(
            Command->os,
            CommandBuffer,
            gcmSIZEOF(struct _gcoCMDBUF),
            commandBufferObject
            ));
    }

    /* Return status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckCOMMAND_Reserve
**
**  Reserve space in the command queue.  Also acquire the command queue mutex.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to an gckCOMMAND object.
**
**      gctSIZE_T RequestedBytes
**          Number of bytes previously reserved.
**
**  OUTPUT:
**
**      gctPOINTER * Buffer
**          Pointer to a variable that will receive the address of the reserved
**          space.
**
**      gctSIZE_T * BufferSize
**          Pointer to a variable that will receive the number of bytes
**          available in the command queue.
*/
gceSTATUS
gckCOMMAND_Reserve(
    IN gckCOMMAND Command,
    IN gctUINT32 RequestedBytes,
    OUT gctPOINTER * Buffer,
    OUT gctUINT32 * BufferSize
    )
{
    gceSTATUS status;
    gctUINT32 bytes;
    gctUINT32 requiredBytes;
    gctUINT32 requestedAligned;

    gcmkHEADER_ARG("Command=0x%x RequestedBytes=%lu", Command, RequestedBytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    /* Compute aligned number of reuested bytes. */
    requestedAligned = gcmALIGN(RequestedBytes, Command->alignment);

    /* Another WAIT/LINK command sequence will have to be appended after
       the requested area being reserved. Compute the number of bytes
       required for WAIT/LINK at the location after the reserved area. */
    gcmkONERROR(gckHARDWARE_WaitLink(
        Command->kernel->hardware,
        gcvNULL,
        ~0U,
        Command->offset + requestedAligned,
        &requiredBytes,
        gcvNULL,
        gcvNULL
        ));

    /* Compute total number of bytes required. */
    requiredBytes += requestedAligned;

    /* Compute number of bytes available in command queue. */
    bytes = Command->pageSize - Command->offset;

    /* Is there enough space in the current command queue? */
    if (bytes < requiredBytes)
    {
        /* Create a new command queue. */
        gcmkONERROR(_NewQueue(Command));

        /* Recompute the number of bytes in the new kernel command queue. */
        bytes = Command->pageSize - Command->offset;

        /* Still not enough space? */
        if (bytes < requiredBytes)
        {
            /* Rare case, not enough room in command queue. */
            gcmkONERROR(gcvSTATUS_BUFFER_TOO_SMALL);
        }
    }

    /* Return pointer to empty slot command queue. */
    *Buffer = (gctUINT8 *) Command->logical + Command->offset;

    /* Return number of bytes left in command queue. */
    *BufferSize = bytes;

    /* Success. */
    gcmkFOOTER_ARG("*Buffer=0x%x *BufferSize=%lu", *Buffer, *BufferSize);
    return gcvSTATUS_OK;

OnError:
    /* Return status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckCOMMAND_Execute
**
**  Execute a previously reserved command queue by appending a WAIT/LINK command
**  sequence after it and modifying the last WAIT into a LINK command.  The
**  command FIFO mutex will be released whether this function succeeds or not.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to an gckCOMMAND object.
**
**      gctSIZE_T RequestedBytes
**          Number of bytes previously reserved.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_Execute(
    IN gckCOMMAND Command,
    IN gctUINT32 RequestedBytes
    )
{
    gceSTATUS status;

    gctUINT32 waitLinkPhysical;
    gctUINT8_PTR waitLinkLogical;
    gctUINT32 waitLinkAddress;
    gctUINT32 waitLinkOffset;
    gctUINT32 waitLinkBytes;

    gctUINT32 waitPhysical;
    gctPOINTER waitLogical;
    gctUINT32 waitOffset;
    gctUINT32 waitBytes;

    gctUINT32 linkLow, linkHigh;

#if gcdNONPAGED_MEMORY_CACHEABLE
    gctPHYS_ADDR execPhysical;
#endif
    gctPOINTER execLogical;
    gctUINT32 execAddress;
    gctUINT32 execBytes;

    gcmkHEADER_ARG("Command=0x%x RequestedBytes=%lu", Command, RequestedBytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    /* Compute offset for WAIT/LINK. */
    waitLinkOffset = Command->offset + RequestedBytes;

    /* Compute number of bytes left in command queue. */
    waitLinkBytes = Command->pageSize - waitLinkOffset;

    /* Compute the location if WAIT/LINK command sequence. */
    waitLinkPhysical =                Command->physical + waitLinkOffset;
    waitLinkLogical  = (gctUINT8_PTR) Command->logical  + waitLinkOffset;
    waitLinkAddress  =                Command->address  + waitLinkOffset;

    /* Append WAIT/LINK in command queue. */
    gcmkONERROR(gckHARDWARE_WaitLink(
        Command->kernel->hardware,
        waitLinkLogical,
        waitLinkAddress,
        waitLinkOffset,
        &waitLinkBytes,
        &waitOffset,
        &waitBytes
        ));

    /* Compute the location if WAIT command. */
    waitPhysical = waitLinkPhysical + waitOffset;
    waitLogical  = waitLinkLogical  + waitOffset;

    /* Determine the location to jump to for the command buffer being
    ** scheduled. */
    if (Command->newQueue)
    {
        /* New command queue, jump to the beginning of it. */
#if gcdNONPAGED_MEMORY_CACHEABLE
        execPhysical = Command->physical;
#endif
        execLogical  = Command->logical;
        execAddress  = Command->address;
        execBytes    = waitLinkOffset + waitLinkBytes;
    }
    else
    {
        /* Still within the preexisting command queue, jump directly to the
           reserved area. */
#if gcdNONPAGED_MEMORY_CACHEABLE
        execPhysical = (gctUINT8 *) Command->physical + Command->offset;
#endif
        execLogical  = (gctUINT8 *) Command->logical  + Command->offset;
        execAddress  =              Command->address  + Command->offset;
        execBytes    = RequestedBytes + waitLinkBytes;
    }

#if gcdNONPAGED_MEMORY_CACHEABLE
    /* Flush the cache. */
    gcmkONERROR(gckOS_CacheClean(
        Command->os,
        Command->kernelProcessID,
        gcvNULL,
        (gctUINT32)execPhysical,
        execLogical,
        execBytes
        ));
#endif

    /* Convert the last WAIT into a LINK. */
    gcmkONERROR(gckHARDWARE_Link(
        Command->kernel->hardware,
        Command->waitLogical,
        execAddress,
        execBytes,
        &Command->waitSize,
        &linkLow,
        &linkHigh
        ));

#if gcdNONPAGED_MEMORY_CACHEABLE
    /* Flush the cache. */
    gcmkONERROR(gckOS_CacheClean(
        Command->os,
        Command->kernelProcessID,
        gcvNULL,
        (gctUINT32)Command->waitPhysical,
        Command->waitLogical,
        Command->waitSize
        ));
#endif

#if gcdLINK_QUEUE_SIZE
    if (Command->kernel->stuckDump >= gcvSTUCK_DUMP_ALL_COMMAND)
    {
        gcuQUEUEDATA data;

        gcmkVERIFY_OK(gckOS_GetProcessID(&data.linkData.pid));

        data.linkData.start    = execAddress;
        data.linkData.end      = execAddress + execBytes;
        data.linkData.linkLow  = linkLow;
        data.linkData.linkHigh = linkHigh;

        gckQUEUE_Enqueue(&Command->kernel->hardware->linkQueue, &data);
    }
#endif

    gcmkDUMPCOMMAND(
        Command->os,
        Command->waitLogical,
        Command->waitSize,
        gceDUMP_BUFFER_LINK,
        gcvFALSE
        );

    gcmkDUMPCOMMAND(
        Command->os,
        execLogical,
        execBytes,
        gceDUMP_BUFFER_KERNEL,
        gcvFALSE
        );

    /* Update the pointer to the last WAIT. */
    Command->waitPhysical = waitPhysical;
    Command->waitLogical  = waitLogical;
    Command->waitSize     = waitBytes;

    /* Update the command queue. */
    Command->offset  += RequestedBytes + waitLinkBytes;
    Command->newQueue = gcvFALSE;

    /* Update queue tail pointer. */
    gcmkONERROR(gckHARDWARE_UpdateQueueTail(
        Command->kernel->hardware, Command->logical, Command->offset
        ));

#if gcdDUMP_COMMAND
    gcmkPRINT("@[kernel.execute]");
#endif

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
**  gckCOMMAND_Stall
**
**  The calling thread will be suspended until the command queue has been
**  completed.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to an gckCOMMAND object.
**
**      gctBOOL FromPower
**          Determines whether the call originates from inside the power
**          management or not.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_Stall(
    IN gckCOMMAND Command,
    IN gctBOOL FromPower
    )
{
#if gcdNULL_DRIVER
    /* Do nothing with infinite hardware. */
    return gcvSTATUS_OK;
#else
    gckOS os;
    gckHARDWARE hardware;
    gckEVENT eventObject;
    gceSTATUS status;
    gctSIGNAL signal = gcvNULL;
    gctUINT timer = 0;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    /* Extract the gckOS object pointer. */
    os = Command->os;
    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    /* Extract the gckHARDWARE object pointer. */
    hardware = Command->kernel->hardware;
    gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

    /* Extract the gckEVENT object pointer. */
    eventObject = Command->kernel->eventObj;
    gcmkVERIFY_OBJECT(eventObject, gcvOBJ_EVENT);

    /* Allocate the signal. */
    gcmkONERROR(gckOS_CreateSignal(os, gcvTRUE, &signal));

    /* Append the EVENT command to trigger the signal. */
    gcmkONERROR(gckEVENT_Signal(eventObject, signal, gcvKERNEL_PIXEL));

    /* Submit the event queue. */
    gcmkONERROR(gckEVENT_Submit(eventObject, gcvTRUE, FromPower));

#if gcdDUMP_COMMAND
    gcmkPRINT("@[kernel.stall]");
#endif

    if (status == gcvSTATUS_CHIP_NOT_READY)
    {
        /* Error. */
        goto OnError;
    }

    do
    {
        /* Wait for the signal. */
        status = gckOS_WaitSignal(os, signal, gcdGPU_ADVANCETIMER);

        if (status == gcvSTATUS_TIMEOUT)
        {
#if gcmIS_DEBUG(gcdDEBUG_CODE)
            gctUINT32 idle;

            /* Read idle register. */
            gcmkVERIFY_OK(gckHARDWARE_GetIdle(
                hardware, gcvFALSE, &idle
                ));

            gcmkTRACE(
                gcvLEVEL_ERROR,
                "%s(%d): idle=%08x",
                __FUNCTION__, __LINE__, idle
                );

            gcmkVERIFY_OK(gckOS_MemoryBarrier(os, gcvNULL));
#endif

            /* Advance timer. */
            timer += gcdGPU_ADVANCETIMER;
        }
        else if (status == gcvSTATUS_INTERRUPTED)
        {
            gcmkONERROR(gcvSTATUS_INTERRUPTED);
        }

    }
    while (gcmIS_ERROR(status));

    /* Bail out on timeout. */
    if (gcmIS_ERROR(status))
    {
        /* Broadcast the stuck GPU. */
        gcmkONERROR(gckOS_Broadcast(
            os, hardware, gcvBROADCAST_GPU_STUCK
            ));
    }

    /* Delete the signal. */
    gcmkVERIFY_OK(gckOS_DestroySignal(os, signal));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (signal != gcvNULL)
    {
        /* Free the signal. */
        gcmkVERIFY_OK(gckOS_DestroySignal(os, signal));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
#endif
}

/*******************************************************************************
**
**  gckCOMMAND_Attach
**
**  Attach user process.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to a gckCOMMAND object.
**
**      gctUINT32 ProcessID
**          Current process ID.
**
**  OUTPUT:
**
**      gckCONTEXT * Context
**          Pointer to a variable that will receive a pointer to a new
**          gckCONTEXT object.
**
**      gctSIZE_T * StateCount
**          Pointer to a variable that will receive the number of states
**          in the context buffer.
*/
#if (gcdENABLE_3D || gcdENABLE_2D)
gceSTATUS
gckCOMMAND_Attach(
    IN gckCOMMAND Command,
    OUT gckCONTEXT * Context,
    OUT gctSIZE_T * MaxState,
    OUT gctUINT32 * NumStates,
    IN gctUINT32 ProcessID
    )
{
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    /* Acquire the context switching mutex. */
    gcmkONERROR(gckOS_AcquireMutex(
        Command->os, Command->mutexContext, gcvINFINITE
        ));
    acquired = gcvTRUE;

    /* Construct a gckCONTEXT object. */
    gcmkONERROR(gckCONTEXT_Construct(
        Command->os,
        Command->kernel->hardware,
        ProcessID,
        Context
        ));

    /* Return the number of states in the context. */
    * MaxState  = (* Context)->maxState;
    * NumStates = (* Context)->numStates;

    /* Release the context switching mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Command->os, Command->mutexContext));
    acquired = gcvFALSE;

    /* Success. */
    gcmkFOOTER_ARG("*Context=0x%x", *Context);
    return gcvSTATUS_OK;

OnError:
    /* Release mutex. */
    if (acquired)
    {
        /* Release the context switching mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Command->os, Command->mutexContext));
        acquired = gcvFALSE;
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}
#endif

/*******************************************************************************
**
**  gckCOMMAND_Detach
**
**  Detach user process.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to a gckCOMMAND object.
**
**      gckCONTEXT Context
**          Pointer to a gckCONTEXT object to be destroyed.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_Detach(
    IN gckCOMMAND Command,
    IN gckCONTEXT Context
    )
{
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Command=0x%x Context=0x%x", Command, Context);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    /* Acquire the context switching mutex. */
    gcmkONERROR(gckOS_AcquireMutex(
        Command->os, Command->mutexContext, gcvINFINITE
        ));
    acquired = gcvTRUE;

    /* Construct a gckCONTEXT object. */
    gcmkONERROR(gckCONTEXT_Destroy(Context));

    if (Command->currContext == Context)
    {
        /* Detach from gckCOMMAND object if the destoryed context is current context. */
        Command->currContext = gcvNULL;
    }

    /* Release the context switching mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Command->os, Command->mutexContext));
    acquired = gcvFALSE;

    /* Return the status. */
    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    /* Release mutex. */
    if (acquired)
    {
        /* Release the context switching mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Command->os, Command->mutexContext));
        acquired = gcvFALSE;
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckCOMMAND_DumpExecutingBuffer
**
**  Dump the command buffer which GPU is executing.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to a gckCOMMAND object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_DumpExecutingBuffer(
    IN gckCOMMAND Command
    )
{
    gceSTATUS status;
    gckVIRTUAL_COMMAND_BUFFER_PTR buffer = gcvNULL;
    gctUINT32 gpuAddress;
    gctSIZE_T pageCount;
    gctPOINTER entry;
    gckOS os = Command->os;
    gckKERNEL kernel = Command->kernel;
    gctUINT32 i;
    gctUINT32 dumpRear;
    gckQUEUE queue = &kernel->hardware->linkQueue;
    gctSIZE_T bytes;
    gckLINKDATA linkData;
    gcuQUEUEDATA * queueData;
    gctUINT32 offset;
    gctPOINTER entryDump;
    gctUINT32 pid;
    gctUINT8 processName[24] = {0};
    gctPHYS_ADDR_T cpuPhysical;

    gcmkPRINT("**************************\n");
    gcmkPRINT("**** COMMAND BUF DUMP ****\n");
    gcmkPRINT("**************************\n");

    gcmkPRINT("  Submitted commit stamp = %lld", Command->commitStamp - 1);
    gcmkPRINT("  Executed commit stamp  = %lld", *(gctUINT64_PTR)Command->fence->logical);

    gcmkVERIFY_OK(gckOS_ReadRegisterEx(os, kernel->core, 0x664, &gpuAddress));
    gcmkVERIFY_OK(gckOS_ReadRegisterEx(os, kernel->core, 0x664, &gpuAddress));

    gcmkPRINT("DMA Address 0x%08X, memory around:", gpuAddress);

    /* Search and dump memory around DMA address. */
    if (kernel->virtualCommandBuffer)
    {
        status = gckDEVICE_QueryGPUAddress(kernel->device, kernel, gpuAddress, &buffer);
    }
    else
    {
        status = gcvSTATUS_OK;
    }

    if (gcmIS_SUCCESS(status))
    {
        if (kernel->virtualCommandBuffer)
        {
            gcmkVERIFY_OK(gckOS_CreateKernelVirtualMapping(
                os, buffer->virtualBuffer.physical, buffer->virtualBuffer.bytes, &entry, &pageCount));

            offset = gpuAddress - buffer->virtualBuffer.gpuAddress;

            entryDump  = entry;

            /* Dump one pages. */
            bytes = 4096;

            /* Align to page. */
            offset &= 0xfffff000;

            /* Kernel address of page where stall point stay. */
            entryDump = (gctUINT8_PTR)entryDump + offset;

            /* Align to page. */
            gpuAddress &= 0xfffff000;
        }
        else
        {
            gcmkVERIFY_OK(gckOS_GPUPhysicalToCPUPhysical(os, gpuAddress, &cpuPhysical));

            gcmkVERIFY_OK(gckOS_MapPhysical(os, (gctUINT32) cpuPhysical, 4096, &entry));

            /* Align to page start. */
            entryDump  = (gctPOINTER)((gctUINTPTR_T)entry & ~0xFFF);
            gpuAddress = gpuAddress & ~0xFFF;
            bytes      = 4096;
        }

        gcmkPRINT("User Command Buffer:\n");
        _DumpBuffer(entryDump, gpuAddress, bytes);

        if (kernel->virtualCommandBuffer)
        {
            gcmkVERIFY_OK(gckOS_DestroyKernelVirtualMapping(
                os, buffer->virtualBuffer.physical, buffer->virtualBuffer.bytes, entry));
        }
        else
        {
             gcmkVERIFY_OK(gckOS_UnmapPhysical(os, entry, 4096));
        }
    }
    else
    {
        _DumpKernelCommandBuffer(Command);
    }

    /* Dump link queue. */
    if (queue->count)
    {
        gcmkPRINT("Dump Level is %d, dump %d valid record in link queue:",
                  Command->kernel->stuckDump, queue->count);

        dumpRear  = queue->count;

        for (i = 0; i < dumpRear; i++)
        {
            gckQUEUE_GetData(queue, i, &queueData);

            linkData = &queueData->linkData;

            /* Get gpu address of this command buffer. */
            gpuAddress = linkData->start;
            bytes = linkData->end - gpuAddress;

            pid = linkData->pid;

            gckOS_GetProcessNameByPid(pid, 16, processName);

            if (kernel->virtualCommandBuffer)
            {
                buffer = gcvNULL;

                /* Get the whole buffer. */
                status = gckDEVICE_QueryGPUAddress(kernel->device, kernel, gpuAddress, &buffer);

                if (gcmIS_ERROR(status))
                {
                    /* Get kernel address of kernel command buffer. */
                    status = gckCOMMAND_AddressInKernelCommandBuffer(
                             kernel->command, gpuAddress, &entry);

                    if (gcmIS_ERROR(status))
                    {
                        status = gckHARDWARE_AddressInHardwareFuncions(
                                 kernel->hardware, gpuAddress, &entry);

                        if (gcmIS_ERROR(status))
                        {
                            gcmkPRINT("Buffer [%08X - %08X] not found, may be freed",
                                      linkData->start,
                                      linkData->end);
                            continue;
                        }
                    }

                    offset = 0;
                    gcmkPRINT("Kernel Command Buffer: %08X, %08X", linkData->linkLow, linkData->linkHigh);
                }
                else
                {
                    /* Get kernel logical for dump. */
                    if (buffer->virtualBuffer.kernelLogical)
                    {
                        /* Get kernel logical directly if it is a context buffer. */
                        entry = buffer->virtualBuffer.kernelLogical;
                        gcmkPRINT("Context Buffer: %08X, %08X PID:%d %s",
                                  linkData->linkLow, linkData->linkHigh, linkData->pid, processName);
                    }
                    else
                    {
                        /* Make it accessiable by kernel if it is a user command buffer. */
                        gcmkVERIFY_OK(
                            gckOS_CreateKernelVirtualMapping(os,
                                                             buffer->virtualBuffer.physical,
                                                             buffer->virtualBuffer.bytes,
                                                             &entry,
                                                             &pageCount));
                         gcmkPRINT("User Command Buffer: %08X, %08X PID:%d %s",
                                   linkData->linkLow, linkData->linkHigh, linkData->pid, processName);
                    }

                    offset = gpuAddress - buffer->virtualBuffer.gpuAddress;
                }

                /* Dump from the entry. */
                _DumpBuffer((gctUINT8_PTR)entry + offset, gpuAddress, bytes);

                /* Release kernel logical address if neccessary. */
                if (buffer && !buffer->virtualBuffer.kernelLogical)
                {
                    gcmkVERIFY_OK(
                        gckOS_DestroyKernelVirtualMapping(os,
                                                          buffer->virtualBuffer.physical,
                                                          buffer->virtualBuffer.bytes,
                                                          entry));
                }
            }
            else
            {
                gcmkVERIFY_OK(gckOS_GPUPhysicalToCPUPhysical(os, gpuAddress, &cpuPhysical));

                gcmkVERIFY_OK(gckOS_MapPhysical(os, (gctUINT32) cpuPhysical, bytes, &entry));

                gcmkPRINT("Command Buffer: %08X, %08X PID:%d %s",
                          linkData->linkLow, linkData->linkHigh, linkData->pid, processName);

                _DumpBuffer((gctUINT8_PTR)entry, gpuAddress, bytes);

                gcmkVERIFY_OK(gckOS_UnmapPhysical(os, entry, bytes));
            }
        }
    }

    return gcvSTATUS_OK;
}

gceSTATUS
gckCOMMAND_AddressInKernelCommandBuffer(
    IN gckCOMMAND Command,
    IN gctUINT32 Address,
    OUT gctPOINTER * Pointer
    )
{
    gctINT i;

    for (i = 0; i < gcdCOMMAND_QUEUES; i++)
    {
        if ((Address >= Command->queues[i].address)
         && (Address < (Command->queues[i].address + Command->pageSize))
        )
        {
            *Pointer = (gctUINT8_PTR)Command->queues[i].logical
                     + (Address - Command->queues[i].address)
                     ;

            return gcvSTATUS_OK;
        }
    }

    return gcvSTATUS_NOT_FOUND;
}

