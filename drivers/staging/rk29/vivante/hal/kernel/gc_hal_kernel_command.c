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




#include "gc_hal_kernel_precomp.h"
#include "gc_hal_user_context.h"

#if defined(__QNXNTO__)
#include <sys/slog.h>
#endif

#define _GC_OBJ_ZONE    gcvZONE_COMMAND

/******************************************************************************\
********************************* Support Code *********************************
\******************************************************************************/

#if gcdDUMP_COMMAND
static void
_DumpCommand(
    IN gckCOMMAND Command,
    IN gctPOINTER Pointer,
    IN gctSIZE_T Bytes
    )
{
    gctUINT32_PTR data = (gctUINT32_PTR) Pointer;
    gctUINT32 address;

    gckOS_GetPhysicalAddress(Command->os, Pointer, &address);

    gcmkPRINT("@[kernel.command %08X %08X", address, Bytes);
    while (Bytes >= 8*4)
    {
        gcmkPRINT("  %08X %08X %08X %08X %08X %08X %08X %08X",
                  data[0], data[1], data[2], data[3], data[4], data[5], data[6],
                  data[7]);
        data  += 8;
        Bytes -= 32;
    }

    switch (Bytes)
    {
    case 7*4:
        gcmkPRINT("  %08X %08X %08X %08X %08X %08X %08X",
                  data[0], data[1], data[2], data[3], data[4], data[5],
                  data[6]);
        break;

    case 6*4:
        gcmkPRINT("  %08X %08X %08X %08X %08X %08X",
                  data[0], data[1], data[2], data[3], data[4], data[5]);
        break;

    case 5*4:
        gcmkPRINT("  %08X %08X %08X %08X %08X",
                  data[0], data[1], data[2], data[3], data[4]);
        break;

    case 4*4:
        gcmkPRINT("  %08X %08X %08X %08X", data[0], data[1], data[2], data[3]);
        break;

    case 3*4:
        gcmkPRINT("  %08X %08X %08X", data[0], data[1], data[2]);
        break;

    case 2*4:
        gcmkPRINT("  %08X %08X", data[0], data[1]);
        break;

    case 1*4:
        gcmkPRINT("  %08X", data[0]);
        break;
    }

    gcmkPRINT("] -- command");
}
#endif

gceSTATUS
_WaitSignalFromGPU(
    IN gckOS Os,
    IN gckHARDWARE Hardware,
    IN gctSIGNAL Signal
    )
{
    gceSTATUS status;

#if gcdGPU_TIMEOUT
    gctUINT timer = 0;

    do
    {
        /* Wait for the signal. */
        status = gckOS_WaitSignal(Os, Signal, 250);

        if (status == gcvSTATUS_TIMEOUT)
        {
#if gcdDEBUG
            gctUINT32 idle;

            /* Read idle register. */
            gcmkVERIFY_OK(
                gckHARDWARE_GetIdle(Hardware,
                                    gcvFALSE,
                                    &idle));

            gcmkTRACE(gcvLEVEL_ERROR,
                      "%s(%d): idle=%08x",
                      __FUNCTION__, __LINE__, idle);
#endif

            /* Advance timer. */
            timer += 250;
        }
        else
        {
            gcmkONERROR(status);
        }
    }
    while (gcmIS_ERROR(status) && (timer < gcdGPU_TIMEOUT));

    /* Bail out on timeout. */
    if (gcmIS_ERROR(status))
    {
        /* Broadcast the stuck GPU. */
        gcmkONERROR(gckOS_Broadcast(Os,
                                    Hardware,
                                    gcvBROADCAST_GPU_STUCK));

        gcmkONERROR(gcvSTATUS_GPU_NOT_RESPONDING);
    }

#else
    gcmkONERROR(gckOS_WaitSignal(Os, Signal, gcvINFINITE));
#endif

    /* Success. */
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    return status;
}

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

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Switch to the next command buffer. */
    currentIndex = Command->index;
    newIndex     = (currentIndex + 1) % gcdCOMMAND_QUEUES;

    /* Wait for availability. */
#if gcdDUMP_COMMAND
    gcmkPRINT("@[kernel.waitsignal]");
#endif

    gcmkONERROR(
        _WaitSignalFromGPU(Command->os,
                           Command->kernel->hardware,
                           Command->queues[newIndex].signal));

    if (currentIndex >= 0)
    {
        /* Mark the command queue as available. */
        gcmkONERROR(gckEVENT_Signal(Command->kernel->event,
                                    Command->queues[currentIndex].signal,
                                    gcvKERNEL_COMMAND));
    }

    /* Update gckCOMMAND object with new command queue. */
    Command->index    = newIndex;
    Command->newQueue = gcvTRUE;
    Command->physical = Command->queues[newIndex].physical;
    Command->logical  = Command->queues[newIndex].logical;
    Command->offset   = 0;

    if (currentIndex >= 0)
    {
        /* Submit the event queue. */
        Command->submit = gcvTRUE;
    }

    /* Success. */
    gcmkFOOTER_ARG("Command->index=%d", Command->index);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

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

    gcmkHEADER_ARG("Kernel=0x%x", Kernel);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(Command != gcvNULL);

    /* Extract the gckOS object. */
    os = Kernel->os;

    /* Allocate the gckCOMMAND structure. */
    gcmkONERROR(
        gckOS_Allocate(os,
                       gcmSIZEOF(struct _gckCOMMAND),
                       (gctPOINTER *) &command));

    /* Initialize the gckCOMMAND object.*/
    command->object.type    = gcvOBJ_COMMAND;
    command->kernel         = Kernel;
    command->os             = os;
    command->mutexQueue     = gcvNULL;
    command->mutexContext   = gcvNULL;
    command->powerSemaphore = gcvNULL;
    command->atomCommit     = gcvNULL;

    /* No command queues created yet. */
    command->index = 0;
    for (i = 0; i < gcdCOMMAND_QUEUES; ++i)
    {
        command->queues[i].signal  = gcvNULL;
        command->queues[i].logical = gcvNULL;
    }

    /* Get the command buffer requirements. */
    gcmkONERROR(
        gckHARDWARE_QueryCommandBuffer(Kernel->hardware,
                                       &command->alignment,
                                       &command->reservedHead,
                                       &command->reservedTail));

    /* No contexts available yet. */
    command->contextCounter = command->currentContext = 0;

    /* Create the command queue mutex. */
    gcmkONERROR(
        gckOS_CreateMutex(os, &command->mutexQueue));

    /* Create the context switching mutex. */
    gcmkONERROR(
        gckOS_CreateMutex(os, &command->mutexContext));

    /* Create the power management semaphore. */
    gcmkONERROR(
        gckOS_CreateSemaphore(os, &command->powerSemaphore));

    /* Create the commit atom. */
    gcmkONERROR(gckOS_AtomConstruct(os, &command->atomCommit));

    /* Get the page size from teh OS. */
    gcmkONERROR(
        gckOS_GetPageSize(os, &command->pageSize));

    /* Set hardware to pipe 0. */
    command->pipeSelect = 0;

    /* Pre-allocate the command queues. */
    for (i = 0; i < gcdCOMMAND_QUEUES; ++i)
    {
        gcmkONERROR(
            gckOS_AllocateNonPagedMemory(os,
                                         gcvFALSE,
                                         &command->pageSize,
                                         &command->queues[i].physical,
                                         &command->queues[i].logical));
        gcmkONERROR(
            gckOS_CreateSignal(os, gcvFALSE, &command->queues[i].signal));

        gcmkONERROR(
            gckOS_Signal(os, command->queues[i].signal, gcvTRUE));
    }

    /* No command queue in use yet. */
    command->index    = -1;
    command->logical  = gcvNULL;
    command->newQueue = gcvFALSE;
    command->submit   = gcvFALSE;

    /* Command is not yet running. */
    command->running = gcvFALSE;

    /* Command queue is idle. */
    command->idle = gcvTRUE;

    /* Commit stamp is zero. */
    command->commitStamp = 0;

    /* Return pointer to the gckCOMMAND object. */
    *Command = command;

    /* Success. */
    gcmkFOOTER_ARG("*Command=0x%x", *Command);
    return gcvSTATUS_OK;

OnError:
    /* Roll back. */
    if (command != gcvNULL)
    {
        if (command->atomCommit != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_AtomDestroy(os, command->atomCommit));
        }

        if (command->powerSemaphore != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_DestroySemaphore(os, command->powerSemaphore));
        }

        if (command->mutexContext != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_DeleteMutex(os, command->mutexContext));
        }

        if (command->mutexQueue != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_DeleteMutex(os, command->mutexQueue));
        }

        for (i = 0; i < gcdCOMMAND_QUEUES; ++i)
        {
            if (command->queues[i].signal != gcvNULL)
            {
                gcmkVERIFY_OK(
                    gckOS_DestroySignal(os, command->queues[i].signal));
            }

            if (command->queues[i].logical != gcvNULL)
            {
                gcmkVERIFY_OK(
                    gckOS_FreeNonPagedMemory(os,
                                             command->pageSize,
                                             command->queues[i].physical,
                                             command->queues[i].logical));
            }
        }

        gcmkVERIFY_OK(gckOS_Free(os, command));
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
        gcmkASSERT(Command->queues[i].signal != gcvNULL);
        gcmkVERIFY_OK(
            gckOS_DestroySignal(Command->os, Command->queues[i].signal));

        gcmkASSERT(Command->queues[i].logical != gcvNULL);
        gcmkVERIFY_OK(
            gckOS_FreeNonPagedMemory(Command->os,
                                     Command->pageSize,
                                     Command->queues[i].physical,
                                     Command->queues[i].logical));
    }

    /* Delete the context switching mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Command->os, Command->mutexContext));

    /* Delete the command queue mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Command->os, Command->mutexQueue));

    /* Destroy the power management semaphore. */
    gcmkVERIFY_OK(gckOS_DestroySemaphore(Command->os, Command->powerSemaphore));

    /* Destroy the commit atom. */
    gcmkVERIFY_OK(gckOS_AtomDestroy(Command->os, Command->atomCommit));

    /* Mark object as unknown. */
    Command->object.type = gcvOBJ_UNKNOWN;

    /* Free the gckCOMMAND object. */
    gcmkVERIFY_OK(gckOS_Free(Command->os, Command));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
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
    gckHARDWARE hardware;
    gceSTATUS status;
    gctSIZE_T bytes;

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

    if (Command->logical == gcvNULL)
    {
        /* Start at beginning of a new queue. */
        gcmkONERROR(_NewQueue(Command));
    }

    /* Start at beginning of page. */
    Command->offset = 0;

    /* Append WAIT/LINK. */
    bytes = Command->pageSize;
    gcmkONERROR(
        gckHARDWARE_WaitLink(hardware,
                             Command->logical,
                             0,
                             &bytes,
                             &Command->wait,
                             &Command->waitSize));

    /* Flush the cache for the wait/link. */
    gcmkONERROR(gckOS_CacheFlush(Command->os,
                                 gcvNULL,
                                 Command->logical,
                                 bytes));

    /* Adjust offset. */
    Command->offset   = bytes;
    Command->newQueue = gcvFALSE;

    /* Enable command processor. */
#ifdef __QNXNTO__
    gcmkONERROR(
        gckHARDWARE_Execute(hardware,
                            Command->logical,
                            Command->physical,
                            gcvTRUE,
                            bytes));
#else
    gcmkONERROR(
        gckHARDWARE_Execute(hardware,
                            Command->logical,
                            bytes));
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

    /* Replace last WAIT with END. */
    gcmkONERROR(
        gckHARDWARE_End(hardware,
                        Command->wait,
                        &Command->waitSize));

    /* Wait for idle. */
    gcmkONERROR(
        gckHARDWARE_GetIdle(hardware, gcvTRUE, &idle));

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

typedef struct _gcsMAPPED * gcsMAPPED_PTR;
struct _gcsMAPPED
{
    gcsMAPPED_PTR next;
    gctPOINTER pointer;
    gctPOINTER kernelPointer;
    gctSIZE_T bytes;
};

static gceSTATUS
_AddMap(
    IN gckOS Os,
    IN gctPOINTER Source,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Destination,
    IN OUT gcsMAPPED_PTR * Stack
    )
{
    gcsMAPPED_PTR map = gcvNULL;
    gceSTATUS status;

    /* Don't try to map NULL pointers. */
    if (Source == gcvNULL)
    {
        *Destination = gcvNULL;
        return gcvSTATUS_OK;
    }

    /* Allocate the gcsMAPPED structure. */
    gcmkONERROR(
        gckOS_Allocate(Os, gcmSIZEOF(*map), (gctPOINTER *) &map));

    /* Map the user pointer into kernel addressing space. */
    gcmkONERROR(
        gckOS_MapUserPointer(Os, Source, Bytes, Destination));

    /* Save mapping. */
    map->pointer       = Source;
    map->kernelPointer = *Destination;
    map->bytes         = Bytes;

    /* Push structure on top of the stack. */
    map->next = *Stack;
    *Stack    = map;

    /* Success. */
    return gcvSTATUS_OK;

OnError:
    if (gcmIS_ERROR(status) && (map != gcvNULL))
    {
        /* Roll back on error. */
        gcmkVERIFY_OK(gckOS_Free(Os, map));
    }

    /* Return the status. */
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
**          Pointer to an gckCOMMAND object.
**
**      gcoCMDBUF CommandBuffer
**          Pointer to an gcoCMDBUF object.
**
**      gcoCONTEXT Context
**          Pointer to an gcoCONTEXT object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_Commit(
    IN gckCOMMAND Command,
    IN gcoCMDBUF CommandBuffer,
    IN gcoCONTEXT Context,
    IN gctHANDLE Process
    )
{
    gcoCMDBUF commandBuffer;
    gcoCONTEXT context;
    gckHARDWARE hardware = gcvNULL;
    gceSTATUS status;
    gctPOINTER initialLink, link;
    gctSIZE_T bytes, initialSize, lastRun;
    gcoCMDBUF buffer;
    gctPOINTER wait;
    gctSIZE_T waitSize;
    gctUINT32 offset;
    gctPOINTER fetchAddress;
    gctSIZE_T fetchSize;
    gctUINT8_PTR logical;
    gcsMAPPED_PTR stack = gcvNULL;
    gctINT acquired = 0;
#if gcdSECURE_USER
    gctUINT32_PTR hint;
#endif
#if gcdDUMP_COMMAND
    gctPOINTER dataPointer;
    gctSIZE_T dataBytes;
#endif
    gctPOINTER flushPointer;
    gctSIZE_T flushSize;
    gctBOOL semaAcquired = gcvFALSE;
    gctINT32 atomValue;
    gctBOOL atomIncremented = gcvFALSE;
    gctUINT32 process, thread;
    gctBOOL powerAcquired = gcvFALSE;

    gcmkHEADER_ARG("Command=0x%x CommandBuffer=0x%x Context=0x%x",
                   Command, CommandBuffer, Context);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

#if gcdNULL_DRIVER == 2
    /* Do nothing with infinite hardware. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
#endif

    gcmkONERROR(
        _AddMap(Command->os,
                CommandBuffer,
                gcmSIZEOF(struct _gcoCMDBUF),
                (gctPOINTER *) &commandBuffer,
                &stack));
    gcmkVERIFY_OBJECT(commandBuffer, gcvOBJ_COMMANDBUFFER);
    gcmkONERROR(
        _AddMap(Command->os,
                Context,
                gcmSIZEOF(struct _gcoCONTEXT),
                (gctPOINTER *) &context,
                &stack));
    gcmkVERIFY_OBJECT(context, gcvOBJ_CONTEXT);

    /* Extract the gckHARDWARE and gckEVENT objects. */
    hardware = Command->kernel->hardware;
    gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

    /* Get current process and thread IDs. */
    gcmkONERROR(gckOS_GetProcessID(&process));
    gcmkONERROR(gckOS_GetThreadID(&thread));

    /* Try to acquire the power mutex. */
    status = gckOS_AcquireMutex(Command->os, hardware->powerMutex, 0);

    if (status == gcvSTATUS_OK)
    {
        hardware->powerProcess = process;
        hardware->powerThread  = thread;
        powerAcquired = gcvTRUE;
    }
    else if (status == gcvSTATUS_TIMEOUT)
    {
        /* Check if we already own this mutex. */
        if ((hardware->powerProcess != process)
        ||  (hardware->powerThread  != thread)
        )
        {
            /* Acquire the power mutex. */
            gcmkONERROR(
                gckOS_AcquireMutex(Command->os,
                                   hardware->powerMutex, gcvINFINITE));

            hardware->powerProcess = process;
            hardware->powerThread  = thread;
            powerAcquired = gcvTRUE;
        }
    }
    else
    {
        gcmkONERROR(status);
    }

    /* Increment the commit atom. */
    gcmkONERROR(gckOS_AtomIncrement(Command->os,
                                    Command->atomCommit,
                                    &atomValue));
    atomIncremented = gcvTRUE;

    if (powerAcquired)
    {
        /* Release the power mutex. */
        gcmkONERROR(gckOS_ReleaseMutex(Command->os, hardware->powerMutex));
        powerAcquired = gcvFALSE;
    }

    /* Notify the system the GPU has a commit. */
    gcmkONERROR(gckOS_Broadcast(Command->os,
                                hardware,
                                gcvBROADCAST_GPU_COMMIT));

    /* Acquire the power management semaphore. */
    gcmkONERROR(gckOS_AcquireSemaphore(Command->os, Command->powerSemaphore));
    semaAcquired = gcvTRUE;

    /* Acquire the context switching mutex. */
    gcmkONERROR(
        gckOS_AcquireMutex(Command->os,
                           Command->mutexContext,
                           gcvINFINITE));

    ++acquired;

    /* Reserved slot in the context or command buffer. */
    gcmkONERROR(
        gckHARDWARE_PipeSelect(hardware, gcvNULL, 0, &bytes));

    /* Test if we need to switch to this context. */
    if ((context->id != 0)
    &&  (context->id != Command->currentContext)
    )
    {
        /* Map the context buffer. */
        gcmkONERROR(
            _AddMap(Command->os,
                    context->logical,
                    context->bufferSize,
                    (gctPOINTER *) &logical,
                    &stack));

#if gcdSECURE_USER
        /* Map the hint array. */
        gcmkONERROR(
            _AddMap(Command->os,
                    context->hintArray,
                    context->hintCount * gcmSIZEOF(gctUINT32),
                    (gctPOINTER *) &hint,
                    &stack));

        /* Loop while we have valid hints. */
        while (*hint != 0)
        {
            /* Map handle into physical address. */
            gcmkONERROR(
                gckKERNEL_MapLogicalToPhysical(
                    Command->kernel,
                    Process,
                    (gctPOINTER *) (logical + *hint)));

            /* Next hint. */
            ++hint;
        }
#endif

        /* See if we have to check pipes. */
        if (context->pipe2DIndex != 0)
        {
            /* See if we are in the correct pipe. */
            if (context->initialPipe == Command->pipeSelect)
            {
                gctUINT32 reserved = bytes;
                gctUINT8_PTR nop   = logical;

                /* Already in the correct pipe, fill context buffer with NOP. */
                while (reserved > 0)
                {
                    bytes = reserved;
                    gcmkONERROR(
                        gckHARDWARE_Nop(hardware, nop, &bytes));

                    gcmkASSERT(reserved >= bytes);
                    reserved -= bytes;
                    nop      += bytes;
                }
            }
            else
            {
                /* Switch to the correct pipe. */
                gcmkONERROR(
                    gckHARDWARE_PipeSelect(hardware,
                                           logical,
                                           context->initialPipe,
                                           &bytes));
            }
        }

        /* Save initial link pointer. */
        initialLink = logical;
        initialSize = context->bufferSize;

        /* Save initial buffer to flush. */
        flushPointer = initialLink;
        flushSize    = initialSize;

        /* Save pointer to next link. */
        gcmkONERROR(
            _AddMap(Command->os,
                    context->link,
                    8,
                    &link,
                    &stack));

        /* Start parsing CommandBuffer. */
        buffer = commandBuffer;

        /* Mark context buffer as used. */
        if (context->inUse != gcvNULL)
        {
            gctBOOL_PTR inUse;

            gcmkONERROR(
                _AddMap(Command->os,
                        (gctPOINTER) context->inUse,
                        gcmSIZEOF(gctBOOL),
                        (gctPOINTER *) &inUse,
                        &stack));

            *inUse = gcvTRUE;
        }
    }

    else
    {
        /* Test if this is a new context. */
        if (context->id == 0)
        {
            /* Generate unique ID for the context buffer. */
            context->id = ++ Command->contextCounter;

            if (context->id == 0)
            {
                /* Context counter overflow (wow!) */
                gcmkONERROR(gcvSTATUS_TOO_COMPLEX);
            }
        }

        /* Map the command buffer. */
        gcmkONERROR(
            _AddMap(Command->os,
                    commandBuffer->logical,
                    commandBuffer->offset,
                    (gctPOINTER *) &logical,
                    &stack));

#if gcdSECURE_USER
        /* Map the hint table. */
        gcmkONERROR(
            _AddMap(Command->os,
                    commandBuffer->hintCommit,
                    commandBuffer->offset - commandBuffer->startOffset,
                    (gctPOINTER *) &hint,
                    &stack));

        /* Walk while we have valid hints. */
        while (*hint != 0)
        {
            /* Map the handle to a physical address. */
            gcmkONERROR(
                gckKERNEL_MapLogicalToPhysical(
                    Command->kernel,
                    Process,
                    (gctPOINTER *) (logical + *hint)));

            /* Next hint. */
            ++hint;
        }
#endif

        if (context->entryPipe == Command->pipeSelect)
        {
            gctUINT32 reserved = Command->reservedHead;
            gctUINT8_PTR nop   = logical + commandBuffer->startOffset;

            /* Already in the correct pipe, fill context buffer with NOP. */
            while (reserved > 0)
            {
                bytes = reserved;
                gcmkONERROR(
                    gckHARDWARE_Nop(hardware, nop, &bytes));

                gcmkASSERT(reserved >= bytes);
                reserved -= bytes;
                nop      += bytes;
            }
        }
        else
        {
            /* Switch to the correct pipe. */
            gcmkONERROR(
                gckHARDWARE_PipeSelect(hardware,
                                       logical + commandBuffer->startOffset,
                                       context->entryPipe,
                                       &bytes));
        }

        /* Save initial link pointer. */
        initialLink = logical + commandBuffer->startOffset;
        initialSize = commandBuffer->offset
                    - commandBuffer->startOffset
                    + Command->reservedTail;

        /* Save initial buffer to flush. */
        flushPointer = initialLink;
        flushSize    = initialSize;

        /* Save pointer to next link. */
        link = logical + commandBuffer->offset;

        /* No more data. */
        buffer = gcvNULL;
    }

#if gcdDUMP_COMMAND
    dataPointer = initialLink;
    dataBytes   = initialSize;
#endif

    /* Loop through all remaining command buffers. */
    if (buffer != gcvNULL)
    {
        /* Map the command buffer. */
        gcmkONERROR(
            _AddMap(Command->os,
                    buffer->logical,
                    buffer->offset + Command->reservedTail,
                    (gctPOINTER *) &logical,
                    &stack));

#if gcdSECURE_USER
        /* Map the hint table. */
        gcmkONERROR(
            _AddMap(Command->os,
                    buffer->hintCommit,
                    buffer->offset - buffer->startOffset,
                    (gctPOINTER *) &hint,
                    &stack));

        /* Walk while we have valid hints. */
        while (*hint != 0)
        {
            /* Map the handle to a physical address. */
            gcmkONERROR(
                gckKERNEL_MapLogicalToPhysical(
                    Command->kernel,
                    Process,
                    (gctPOINTER *) (logical + *hint)));

            /* Next hint. */
            ++hint;
        }
#endif

        /* First slot becomes a NOP. */
        {
            gctUINT32 reserved = Command->reservedHead;
            gctUINT8_PTR nop   = logical + buffer->startOffset;

            /* Already in the correct pipe, fill context buffer with NOP. */
            while (reserved > 0)
            {
                bytes = reserved;
                gcmkONERROR(
                    gckHARDWARE_Nop(hardware, nop, &bytes));

                gcmkASSERT(reserved >= bytes);
                reserved -= bytes;
                nop      += bytes;
            }
        }

        /* Generate the LINK to this command buffer. */
        gcmkONERROR(
            gckHARDWARE_Link(hardware,
                             link,
                             logical + buffer->startOffset,
                             buffer->offset
                             - buffer->startOffset
                             + Command->reservedTail,
                             &bytes));

        /* Flush the initial buffer. */
        gcmkONERROR(gckOS_CacheFlush(Command->os,
                                     Process,
                                     flushPointer,
                                     flushSize));

        /* Save new flush pointer. */
        flushPointer = logical + buffer->startOffset;
        flushSize    = buffer->offset
                     - buffer->startOffset
                     + Command->reservedTail;

#if gcdDUMP_COMMAND
        _DumpCommand(Command, dataPointer, dataBytes);
        dataPointer = logical + buffer->startOffset;
        dataBytes   = buffer->offset - buffer->startOffset
                    + Command->reservedTail;
#endif

        /* Save pointer to next link. */
        link = logical + buffer->offset;
    }

    /* Compute number of bytes required for WAIT/LINK. */
    gcmkONERROR(
        gckHARDWARE_WaitLink(hardware,
                             gcvNULL,
                             Command->offset,
                             &bytes,
                             gcvNULL,
                             gcvNULL));

    lastRun = bytes;

    /* Grab the command queue mutex. */
    gcmkONERROR(
        gckOS_AcquireMutex(Command->os,
                           Command->mutexQueue,
                           gcvINFINITE));

    ++acquired;

    /* Compute number of bytes left in current command queue. */
    bytes = Command->pageSize - Command->offset;

    if (bytes < lastRun)
    {
        /* Create a new command queue. */
        gcmkONERROR(_NewQueue(Command));

        /* Adjust run size with any extra commands inserted. */
        lastRun += Command->offset;
    }

    /* Get current offset. */
    offset = Command->offset;

    /* Append WAIT/LINK in command queue. */
    bytes = Command->pageSize - offset;

    gcmkONERROR(
        gckHARDWARE_WaitLink(hardware,
                             (gctUINT8 *) Command->logical + offset,
                             offset,
                             &bytes,
                             &wait,
                             &waitSize));

    /* Flush the cache for the wait/link. */
    gcmkONERROR(gckOS_CacheFlush(Command->os,
                                 gcvNULL,
                                 (gctUINT8 *) Command->logical + offset,
                                 bytes));

#if gcdDUMP_COMMAND
    _DumpCommand(Command, (gctUINT8 *) Command->logical + offset, bytes);
#endif

    /* Adjust offset. */
    offset += bytes;

    if (Command->newQueue)
    {
        /* Compute fetch location and size for a new command queue. */
        fetchAddress = Command->logical;
        fetchSize    = offset;
    }
    else
    {
        /* Compute fetch location and size for an existing command queue. */
        fetchAddress = (gctUINT8 *) Command->logical + Command->offset;
        fetchSize    = offset - Command->offset;
    }

    bytes = 8;

    /* Link in WAIT/LINK. */
    gcmkONERROR(
        gckHARDWARE_Link(hardware,
                         link,
                         fetchAddress,
                         fetchSize,
                         &bytes));

    /* Flush the cache for the command buffer. */
    gcmkONERROR(gckOS_CacheFlush(Command->os,
                                 Process,
                                 flushPointer,
                                 flushSize));

#if gcdDUMP_COMMAND
    _DumpCommand(Command, dataPointer, dataBytes);
#endif

    /* Execute the entire sequence. */
    gcmkONERROR(
        gckHARDWARE_Link(hardware,
                         Command->wait,
                         initialLink,
                         initialSize,
                         &Command->waitSize));

    /* Flush the cache for the link. */
    gcmkONERROR(gckOS_CacheFlush(Command->os,
                                 gcvNULL,
                                 Command->wait,
                                 Command->waitSize));

#if gcdDUMP_COMMAND
    _DumpCommand(Command, Command->wait, Command->waitSize);
#endif

    /* Update command queue offset. */
    Command->offset   = offset;
    Command->newQueue = gcvFALSE;

    /* Update address of last WAIT. */
    Command->wait     = wait;
    Command->waitSize = waitSize;

    /* Update context and pipe select. */
    Command->currentContext = context->id;
    Command->pipeSelect     = context->currentPipe;

    /* Update queue tail pointer. */
    gcmkONERROR(
        gckHARDWARE_UpdateQueueTail(hardware,
                                    Command->logical,
                                    Command->offset));

#if gcdDUMP_COMMAND
    gcmkPRINT("@[kernel.commit]");
#endif

    /* Release the command queue mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Command->os, Command->mutexQueue));
    --acquired;

    /* Release the context switching mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Command->os, Command->mutexContext));
    --acquired;

    /* Release the power management semaphore. */
    gcmkONERROR(gckOS_ReleaseSemaphore(Command->os, Command->powerSemaphore));
    semaAcquired = gcvFALSE;

    /* Submit events if asked for. */
    if (Command->submit)
    {
        /* Submit events. */
        status = gckEVENT_Submit(Command->kernel->event, gcvFALSE);

        if (gcmIS_SUCCESS(status))
        {
            /* Success. */
            Command->submit = gcvFALSE;
        }
        else
        {
            gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_COMMAND,
                           "gckEVENT_Submit returned %d",
                           status);
        }
    }

    /* Success. */
    status = gcvSTATUS_OK;

OnError:
    if (acquired > 1)
    {
        /* Release the command queue mutex. */
        gcmkVERIFY_OK(
            gckOS_ReleaseMutex(Command->os, Command->mutexQueue));
    }

    if (acquired > 0)
    {
        /* Release the context switching mutex. */
        gcmkVERIFY_OK(
            gckOS_ReleaseMutex(Command->os, Command->mutexContext));
    }

    if (semaAcquired)
    {
        /* Release the power management semaphore. */
        gcmkVERIFY_OK(
            gckOS_ReleaseSemaphore(Command->os, Command->powerSemaphore));
    }

    if (atomIncremented)
    {
        /* Decrement the commit atom. */
        gcmkVERIFY_OK(gckOS_AtomDecrement(Command->os,
                                          Command->atomCommit,
                                          &atomValue));
    }

    /* Unmap all mapped pointers. */
    while (stack != gcvNULL)
    {
        gcsMAPPED_PTR map = stack;
        stack             = map->next;

        gcmkVERIFY_OK(
            gckOS_UnmapUserPointer(Command->os,
                                   map->pointer,
                                   map->bytes,
                                   map->kernelPointer));

        gcmkVERIFY_OK(
            gckOS_Free(Command->os, map));
    }

    if (powerAcquired)
    {
        /* Release the power mutex. */
        gcmkONERROR(gckOS_ReleaseMutex(Command->os, hardware->powerMutex));
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
    IN gctSIZE_T RequestedBytes,
    OUT gctPOINTER * Buffer,
    OUT gctSIZE_T * BufferSize
    )
{
    gceSTATUS status;
    gctSIZE_T requiredBytes, bytes;
    gctBOOL acquired = gcvFALSE;
    gctBOOL semaAcquired = gcvTRUE;
    gckHARDWARE hardware = gcvNULL;
    gctINT32 atomValue;
    gctBOOL atomIncremented = gcvFALSE;
    gctUINT32 process, thread;
    gctBOOL powerAcquired = gcvFALSE;

    gcmkHEADER_ARG("Command=0x%x RequestedBytes=%lu", Command, RequestedBytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    /* Extract the gckHARDWARE objects. */
    hardware = Command->kernel->hardware;
    gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

    /* Get current process and thread IDs. */
    gcmkONERROR(gckOS_GetProcessID(&process));
    gcmkONERROR(gckOS_GetThreadID(&thread));

    /* Try to acquire the power mutex. */
    status = gckOS_AcquireMutex(Command->os, hardware->powerMutex, 0);

    if (status == gcvSTATUS_OK)
    {
        hardware->powerProcess = process;
        hardware->powerThread  = thread;
        powerAcquired = gcvTRUE;
    }
    else if (status == gcvSTATUS_TIMEOUT)
    {
        /* Check if we already own this mutex. */
        if ((hardware->powerProcess != process)
        ||  (hardware->powerThread  != thread)
        )
        {
            /* Acquire the power mutex. */
            gcmkONERROR(
                gckOS_AcquireMutex(Command->os,
                                   hardware->powerMutex, gcvINFINITE));

            hardware->powerProcess = process;
            hardware->powerThread  = thread;
            powerAcquired = gcvTRUE;
        }
    }
    else
    {
        gcmkONERROR(status);
    }

    /* Increment the commit atom. */
    gcmkONERROR(
        gckOS_AtomIncrement(Command->os, Command->atomCommit, &atomValue));
    atomIncremented = gcvTRUE;

    if (powerAcquired)
    {
        /* Release the power mutex. */
        gcmkONERROR(gckOS_ReleaseMutex(Command->os,
                                       Command->kernel->hardware->powerMutex));
        powerAcquired = gcvFALSE;
    }

    /* Notify the system the GPU has a commit. */
    gcmkONERROR(gckOS_Broadcast(Command->os,
                                Command->kernel->hardware,
                                gcvBROADCAST_GPU_COMMIT));

    /* Grab the power management semaphore. */
    gcmkONERROR(gckOS_AcquireSemaphore(Command->os, Command->powerSemaphore));
    semaAcquired = gcvTRUE;

    /* Grab the conmmand queue mutex. */
    gcmkONERROR(
        gckOS_AcquireMutex(Command->os,
                           Command->mutexQueue,
                           gcvINFINITE));
    acquired = gcvTRUE;

    /* Compute number of bytes required for WAIT/LINK. */
    gcmkONERROR(
        gckHARDWARE_WaitLink(Command->kernel->hardware,
                             gcvNULL,
                             Command->offset + gcmALIGN(RequestedBytes,
                                                        Command->alignment),
                             &requiredBytes,
                             gcvNULL,
                             gcvNULL));

    /* Compute total number of bytes required. */
    requiredBytes += gcmALIGN(RequestedBytes, Command->alignment);

    /* Compute number of bytes available in command queue. */
    bytes = Command->pageSize - Command->offset;

    if (bytes < requiredBytes)
    {
        /* Create a new command queue. */
        gcmkONERROR(_NewQueue(Command));

        /* Recompute number of bytes available in command queue. */
        bytes = Command->pageSize - Command->offset;

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
    if (acquired)
    {
        /* Release command queue mutex on error. */
        gcmkVERIFY_OK(
            gckOS_ReleaseMutex(Command->os, Command->mutexQueue));
    }

    if (semaAcquired)
    {
        /* Release the power management semaphore. */
        gcmkVERIFY_OK(
            gckOS_ReleaseSemaphore(Command->os, Command->powerSemaphore));
    }

    if (atomIncremented)
    {
        /* Decrement the commit atom. */
        gcmkVERIFY_OK(
            gckOS_AtomDecrement(Command->os, Command->atomCommit, &atomValue));
    }

    if (powerAcquired)
    {
        /* Release the power mutex. */
        gcmkONERROR(gckOS_ReleaseMutex(Command->os,
                                       Command->kernel->hardware->powerMutex));
    }

    /* Return status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckCOMMAND_Release
**
**  Release a previously reserved command queue.  The command FIFO mutex will be
**  released.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to an gckCOMMAND object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_Release(
    IN gckCOMMAND Command
    )
{
    gceSTATUS status;
    gctINT32 atomValue;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    /* Release the command queue mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Command->os, Command->mutexQueue));

    /* Release the power management semaphore. */
    gcmkONERROR(gckOS_ReleaseSemaphore(Command->os, Command->powerSemaphore));

    /* Decrement the commit atom. */
    gcmkONERROR(
        gckOS_AtomDecrement(Command->os, Command->atomCommit, &atomValue));

    /* Success. */
    gcmkFOOTER_NO();

OnError:
    /* Return the status. */
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
    IN gctSIZE_T RequestedBytes
    )
{
    gctUINT32 offset;
    gctPOINTER address;
    gctSIZE_T bytes;
    gceSTATUS status;
    gctPOINTER wait;
    gctSIZE_T waitBytes;
    gctINT32 atomValue;

    gcmkHEADER_ARG("Command=0x%x RequestedBytes=%lu", Command, RequestedBytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    /* Compute offset for WAIT/LINK. */
    offset = Command->offset + RequestedBytes;

    /* Compute number of byts left in command queue. */
    bytes = Command->pageSize - offset;

    /* Append WAIT/LINK in command queue. */
    gcmkONERROR(
        gckHARDWARE_WaitLink(Command->kernel->hardware,
                             (gctUINT8 *) Command->logical + offset,
                             offset,
                             &bytes,
                             &wait,
                             &waitBytes));

    if (Command->newQueue)
    {
        /* For a new command queue, point to the start of the command
        ** queue and include both the commands inserted at the head of it
        ** and the WAIT/LINK. */
        address = Command->logical;
        bytes  += offset;
    }
    else
    {
        /* For an existing command queue, point to the current offset and
        ** include the WAIT/LINK. */
        address = (gctUINT8 *) Command->logical + Command->offset;
        bytes  += RequestedBytes;
    }

    /* Flush the cache. */
    gcmkONERROR(gckOS_CacheFlush(Command->os, gcvNULL, address, bytes));

#if gcdDUMP_COMMAND
    _DumpCommand(Command, address, bytes);
#endif

    /* Convert the last WAIT into a LINK. */
    gcmkONERROR(gckHARDWARE_Link(Command->kernel->hardware,
                                 Command->wait,
                                 address,
                                 bytes,
                                 &Command->waitSize));

    /* Flush the cache. */
    gcmkONERROR(gckOS_CacheFlush(Command->os,
                                 gcvNULL,
                                 Command->wait,
                                 Command->waitSize));

#if gcdDUMP_COMMAND
    _DumpCommand(Command, Command->wait, 8);
#endif

    /* Update the pointer to the last WAIT. */
    Command->wait     = wait;
    Command->waitSize = waitBytes;

    /* Update the command queue. */
    Command->offset  += bytes;
    Command->newQueue = gcvFALSE;

    /* Update queue tail pointer. */
    gcmkONERROR(
        gckHARDWARE_UpdateQueueTail(Command->kernel->hardware,
                                    Command->logical,
                                    Command->offset));

#if gcdDUMP_COMMAND
    gcmkPRINT("@[kernel.execute]");
#endif

    /* Release the command queue mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Command->os, Command->mutexQueue));

    /* Release the power management semaphore. */
    gcmkONERROR(gckOS_ReleaseSemaphore(Command->os, Command->powerSemaphore));

    /* Submit events if asked for. */
    if (Command->submit)
    {
        /* Submit events. */
        status = gckEVENT_Submit(Command->kernel->event, gcvFALSE);

        if (gcmIS_SUCCESS(status))
        {
            /* Success. */
            Command->submit = gcvFALSE;
        }
        else
        {
            gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_COMMAND,
                           "gckEVENT_Submit returned %d",
                           status);
        }
    }

    /* Decrement the commit atom. */
    gcmkONERROR(
        gckOS_AtomDecrement(Command->os, Command->atomCommit, &atomValue));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Release the command queue mutex. */
    gcmkVERIFY_OK(
        gckOS_ReleaseMutex(Command->os, Command->mutexQueue));

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
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_Stall(
    IN gckCOMMAND Command
    )
{
    gckOS os;
    gckHARDWARE hardware;
    gckEVENT event;
    gceSTATUS status;
    gctSIGNAL signal = gcvNULL;
    gctUINT timer = 0;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

#if gcdNULL_DRIVER == 2
    /* Do nothing with infinite hardware. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
#endif

    /* Extract the gckOS object pointer. */
    os = Command->os;
    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    /* Extract the gckHARDWARE object pointer. */
    hardware = Command->kernel->hardware;
    gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

    /* Extract the gckEVENT object pointer. */
    event = Command->kernel->event;
    gcmkVERIFY_OBJECT(event, gcvOBJ_EVENT);

    /* Allocate the signal. */
    gcmkONERROR(
        gckOS_CreateSignal(os, gcvTRUE, &signal));

    /* Append the EVENT command to trigger the signal. */
    gcmkONERROR(gckEVENT_Signal(event,
                                signal,
                                gcvKERNEL_PIXEL));

    /* Submit the event queue. */
    gcmkONERROR(gckEVENT_Submit(event, gcvTRUE));

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
        status = gckOS_WaitSignal(os, signal, 250);

        if (status == gcvSTATUS_TIMEOUT)
        {
#if gcdDEBUG
            gctUINT32 idle;

            /* Read idle register. */
            gcmkVERIFY_OK(
                gckHARDWARE_GetIdle(Command->kernel->hardware,
                                    gcvFALSE,
                                    &idle));

            gcmkTRACE(gcvLEVEL_ERROR,
                      "%s(%d): idle=%08x",
                      __FUNCTION__, __LINE__, idle);

            gcmkVERIFY_OK(
                gckOS_MemoryBarrier(os, gcvNULL));

#ifdef __QNXNTO__
            gctUINT32 reg_cmdbuf_fetch;
            gctUINT32 reg_intr;

            gcmkVERIFY_OK(
                    gckOS_ReadRegister(Command->kernel->hardware->os, 0x0664, &reg_cmdbuf_fetch));

            if (idle == 0x7FFFFFFE)
            {
                /*
                 * GPU is idle so there should not be pending interrupts.
                 * Just double check.
                 *
                 * Note that reading interrupt register clears it.
                 * That's why we don't read it in all cases.
                 */
                gcmkVERIFY_OK(
                        gckOS_ReadRegister(Command->kernel->hardware->os, 0x10, &reg_intr));

                slogf(
                    _SLOG_SETCODE(1, 0),
                    _SLOG_CRITICAL,
                    "GALcore: Stall timeout (idle = 0x%X, command buffer fetch = 0x%X, interrupt = 0x%X)",
                    idle, reg_cmdbuf_fetch, reg_intr);
            }
            else
            {
                slogf(
                    _SLOG_SETCODE(1, 0),
                    _SLOG_CRITICAL,
                    "GALcore: Stall timeout (idle = 0x%X, command buffer fetch = 0x%X)",
                    idle, reg_cmdbuf_fetch);
            }
#endif
#endif
            /* Advance timer. */
            timer += 250;
        }
    }
    while (gcmIS_ERROR(status)
#if gcdGPU_TIMEOUT
           && (timer < gcdGPU_TIMEOUT)
#endif
           );

    /* Bail out on timeout. */
    if (gcmIS_ERROR(status))
    {
        /* Broadcast the stuck GPU. */
        gcmkONERROR(gckOS_Broadcast(os,
                                    Command->kernel->hardware,
                                    gcvBROADCAST_GPU_STUCK));

        gcmkONERROR(gcvSTATUS_GPU_NOT_RESPONDING);
    }

    /* Delete the signal. */
    gcmkVERIFY_OK(gckOS_DestroySignal(os, signal));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Free the signal. */
    if (signal != gcvNULL)
    {
        gcmkVERIFY_OK(gckOS_DestroySignal(os, signal));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

