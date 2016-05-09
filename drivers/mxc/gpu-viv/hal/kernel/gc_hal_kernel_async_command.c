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

#define _GC_OBJ_ZONE            gcvZONE_ASYNC_COMMAND

static gceSTATUS
_HandlePatchList(
    IN gckASYNC_COMMAND Command,
    IN gcoCMDBUF CommandBuffer,
    IN gctBOOL NeedCopy
    )
{
    gceSTATUS status;
    gcsPATCH_LIST * uList;
    gcsPATCH_LIST * previous;
    gcsPATCH_LIST * kList;

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
            gcsPATCH * patch = &kList->patch[i];

            /* Touch video memory node. */
            gcmkVERIFY_OK(gckVIDMEM_SetCommitStamp(Command->kernel, gcvENGINE_BLT, patch->handle, Command->commitStamp));
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


gceSTATUS
gckASYNC_COMMAND_Construct(
    IN gckKERNEL Kernel,
    OUT gckASYNC_COMMAND * Command
    )
{
    gceSTATUS status;
    gckASYNC_COMMAND command;
    gckOS os = Kernel->os;

    gcmkHEADER();

    /* Allocate gckASYNC_COMMAND object. */
    gcmkONERROR(gckOS_Allocate(os, gcmSIZEOF(gcsASYNC_COMMAND), (gctPOINTER *)&command));

    gckOS_ZeroMemory(command, gcmSIZEOF(gcsASYNC_COMMAND));

    /* Mutex to protect gckFE. */
    gcmkONERROR(gckOS_CreateMutex(os, &command->mutex));

    /* Initialize gckFE. */
    gckFE_Initialize(Kernel->hardware, &command->fe);

    /* Initialize gckASYNC_COMMAND object. */
    command->os = os;
    command->kernel = Kernel;
    command->hardware = Kernel->hardware;

    gcmkVERIFY_OK(gckHARDWARE_QueryCommandBuffer(
        Kernel->hardware,
        gcvENGINE_BLT,
        gcvNULL,
        gcvNULL,
        &command->reservedTail
        ));

    gcmkONERROR(gckFENCE_Create(
        os, Kernel, &command->fence
        ));

    gcmkONERROR(gckOS_Allocate(os, gcmSIZEOF(gcsPATCH_LIST), &command->kList));

    /* Commit stamp start from 1. */
    command->commitStamp = 1;

    *Command = command;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Rollback. */
    gckASYNC_COMMAND_Destroy(command);

    gcmkFOOTER();
    return status;
}

gceSTATUS
gckASYNC_COMMAND_Destroy(
    IN gckASYNC_COMMAND Command
    )
{
    gcmkHEADER();

    if (Command)
    {
        if (Command->mutex)
        {
            gcmkVERIFY_OK(gckOS_DeleteMutex(Command->os, Command->mutex));
        }

        if (Command->fence)
        {
            gcmkVERIFY_OK(gckFENCE_Destory(Command->os, Command->fence));
        }

        if (Command->kList)
        {
            gcmkOS_SAFE_FREE(Command->os, Command->kList);
        }

        gcmkOS_SAFE_FREE(Command->os, Command);
    }

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

gceSTATUS
gckASYNC_COMMAND_Commit(
    IN gckASYNC_COMMAND Command,
    IN gcoCMDBUF CommandBuffer,
    IN gcsQUEUE_PTR EventQueue
    )
{
    gceSTATUS       status;
    gctBOOL         available = gcvFALSE;
    gctBOOL         acquired = gcvFALSE;
    gcoCMDBUF       commandBufferObject = gcvNULL;
    struct _gcoCMDBUF _commandBufferObject;
    gctUINT8_PTR    commandBufferLogical;
    gctUINT8_PTR    commandBufferTail;
    gctUINT         commandBufferSize;
    gctUINT32       commandBufferAddress;
    gcsFEDescriptor descriptor;
    gctUINT32       pipeBytes;
    gctUINT32       fenceBytes;
    gctBOOL         needCopy;
    gcmkHEADER();

    gckHARDWARE_PipeSelect(Command->hardware, gcvNULL, gcvPIPE_3D, &pipeBytes);

    gckOS_QueryNeedCopy(Command->os, 0, &needCopy);

    gcmkVERIFY_OK(_HandlePatchList(Command, CommandBuffer, needCopy));

    /* Open user passed gcoCMDBUF object. */
    gcmkONERROR(gckKERNEL_OpenUserData(
        Command->kernel,
        needCopy,
        &_commandBufferObject,
        CommandBuffer,
        gcmSIZEOF(struct _gcoCMDBUF),
        (gctPOINTER *)&commandBufferObject
        ));

    gcmkVERIFY_OBJECT(commandBufferObject, gcvOBJ_COMMANDBUFFER);

    /* Compute the command buffer entry and the size. */
    commandBufferLogical
        = (gctUINT8_PTR) gcmUINT64_TO_PTR(commandBufferObject->logical)
        +                commandBufferObject->startOffset
        +                pipeBytes;

    commandBufferSize
        = commandBufferObject->offset
        + Command->reservedTail
        - commandBufferObject->startOffset
        - pipeBytes;

    commandBufferTail
        = commandBufferLogical
        + commandBufferSize
        - Command->reservedTail;

    /* Get the hardware address. */
    if (Command->kernel && Command->kernel->virtualCommandBuffer)
    {
        gckKERNEL kernel = Command->kernel;
        gckVIRTUAL_COMMAND_BUFFER_PTR virtualCommandBuffer
            = gcmNAME_TO_PTR(commandBufferObject->physical);

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
            Command->hardware,
            commandBufferLogical,
            gcvTRUE,
            &commandBufferAddress
            ));
    }

    gcmkONERROR(gckHARDWARE_Fence(
        Command->hardware,
        gcvENGINE_BLT,
        commandBufferTail,
        Command->fence->address,
        Command->commitStamp,
        &fenceBytes
        ));

    descriptor.start = commandBufferAddress;
    descriptor.end   = commandBufferAddress + commandBufferSize;

    gcmkDUMPCOMMAND(
        Command->os,
        commandBufferLogical,
        commandBufferSize,
        gceDUMP_BUFFER_USER,
        gcvFALSE
        );

    gckOS_AcquireMutex(Command->os, Command->mutex, gcvINFINITE);
    acquired = gcvTRUE;

    /* Acquire a slot. */
    for(;;)
    {
        gcmkONERROR(gckFE_ReserveSlot(Command->hardware, &Command->fe, &available));

        if (available)
        {
            break;
        }
        else
        {
            gcmkTRACE_ZONE(gcvLEVEL_INFO, _GC_OBJ_ZONE, "No available slot, have to wait");

            gckOS_Delay(Command->os, 1);
        }
    }

    /* Send descriptor. */
    gckFE_Execute(Command->hardware, &Command->fe, &descriptor);

    Command->commitStamp++;

    gckOS_ReleaseMutex(Command->os, Command->mutex);
    acquired = gcvFALSE;

    gcmkVERIFY_OK(gckKERNEL_CloseUserData(
        Command->kernel,
        needCopy,
        gcvFALSE,
        CommandBuffer,
        gcmSIZEOF(struct _gcoCMDBUF),
        (gctPOINTER *)&commandBufferObject
        ));

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        gckOS_ReleaseMutex(Command->os, Command->mutex);
    }

    if (commandBufferObject)
    {
        gcmkVERIFY_OK(gckKERNEL_CloseUserData(
            Command->kernel,
            needCopy,
            gcvFALSE,
            CommandBuffer,
            gcmSIZEOF(struct _gcoCMDBUF),
            (gctPOINTER *)&commandBufferObject
            ));
    }

    gcmkFOOTER();
    return status;
}

gceSTATUS
gckASYNC_COMMAND_EnterCommit(
    IN gckASYNC_COMMAND Command
    )
{
    return gckOS_AcquireMutex(Command->os, Command->mutex, gcvINFINITE);
}


gceSTATUS
gckASYNC_COMMAND_ExitCommit(
    IN gckASYNC_COMMAND Command
    )
{
    return gckOS_ReleaseMutex(Command->os, Command->mutex);
}

gceSTATUS
gckASYNC_COMMAND_Execute(
    IN gckASYNC_COMMAND Command,
    IN gctUINT32 Start,
    IN gctUINT32 End
    )
{
    gceSTATUS status;
    gcsFEDescriptor descriptor;
    gctBOOL available;

    descriptor.start = Start;
    descriptor.end   = End;

    /* Acquire a slot. */
    for(;;)
    {
        gcmkONERROR(gckFE_ReserveSlot(Command->hardware, &Command->fe, &available));

        if (available)
        {
            break;
        }
        else
        {
            gckOS_Delay(Command->os, 1);
        }
    }

    /* Send descriptor. */
    gckFE_Execute(Command->hardware, &Command->fe, &descriptor);

    return gcvSTATUS_OK;

OnError:
    return status;
}

