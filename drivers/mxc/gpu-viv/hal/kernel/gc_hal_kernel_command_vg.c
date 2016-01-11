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

#include "gc_hal_kernel_hardware_command_vg.h"

#define _GC_OBJ_ZONE            gcvZONE_COMMAND

#ifdef __QNXNTO__
extern gceSTATUS
drv_signal_mgr_add(
    gctUINT32 Pid,
    gctINT32 Coid,
    gctINT32 Rcvid,
    gctUINT64 Signal,
    gctPOINTER *Handle);
#endif

/******************************************************************************\
*********************************** Debugging **********************************
\******************************************************************************/

#define gcvDISABLE_TIMEOUT      1
#define gcvDUMP_COMMAND_BUFFER  0
#define gcvDUMP_COMMAND_LINES   0


#if gcvDEBUG || defined(EMULATOR) || gcvDISABLE_TIMEOUT
#   define gcvQUEUE_TIMEOUT ~0
#else
#   define gcvQUEUE_TIMEOUT 10
#endif


/******************************************************************************\
********************************** Definitions *********************************
\******************************************************************************/

/* Minimum buffer size. */
#define gcvMINUMUM_BUFFER \
    gcmSIZEOF(gcsKERNEL_QUEUE_HEADER) + \
    gcmSIZEOF(gcsKERNEL_CMDQUEUE) * 2

#define gcmDECLARE_INTERRUPT_HANDLER(Block, Number) \
    static gceSTATUS \
    _EventHandler_##Block##_##Number( \
        IN gckVGKERNEL Kernel \
        )

#define gcmDEFINE_INTERRUPT_HANDLER(Block, Number) \
    gcmDECLARE_INTERRUPT_HANDLER(Block, Number) \
    { \
        return _EventHandler_Block( \
            Kernel, \
            &Kernel->command->taskTable[gcvBLOCK_##Block], \
            gcvFALSE \
            ); \
    }

#define gcmDEFINE_INTERRUPT_HANDLER_ENTRY(Block, Number) \
    { gcvBLOCK_##Block, _EventHandler_##Block##_##Number }

/* Block interrupt handling table entry. */
typedef struct _gcsBLOCK_INTERRUPT_HANDLER * gcsBLOCK_INTERRUPT_HANDLER_PTR;
typedef struct _gcsBLOCK_INTERRUPT_HANDLER
{
    gceBLOCK                block;
    gctINTERRUPT_HANDLER    handler;
}
gcsBLOCK_INTERRUPT_HANDLER;

/* Queue control functions. */
typedef struct _gcsQUEUE_UPDATE_CONTROL * gcsQUEUE_UPDATE_CONTROL_PTR;
typedef struct _gcsQUEUE_UPDATE_CONTROL
{
    gctOBJECT_HANDLER       execute;
    gctOBJECT_HANDLER       update;
    gctOBJECT_HANDLER       lastExecute;
    gctOBJECT_HANDLER       lastUpdate;
}
gcsQUEUE_UPDATE_CONTROL;


/******************************************************************************\
********************************* Support Code *********************************
\******************************************************************************/
static gceSTATUS
_FlushMMU(
    IN gckVGCOMMAND Command
    )
{
    gceSTATUS status;
    gctUINT32 oldValue;
    gckVGHARDWARE hardware = Command->hardware;

    gcmkONERROR(gckOS_AtomicExchange(Command->os,
                                     hardware->pageTableDirty,
                                     0,
                                     &oldValue));

    if (oldValue)
    {
        /* Page Table is upated, flush mmu before commit. */
        gcmkONERROR(gckVGHARDWARE_FlushMMU(hardware));
    }

    return gcvSTATUS_OK;
OnError:
    return status;
}

static gceSTATUS
_WaitForIdle(
    IN gckVGCOMMAND Command,
    IN gcsKERNEL_QUEUE_HEADER_PTR Queue
    )
{
    gceSTATUS status = gcvSTATUS_OK;
    gctUINT32 idle;
    gctUINT timeout = 0;

    /* Loop while not idle. */
    while (Queue->pending)
    {
        /* Did we reach the timeout limit? */
        if (timeout == gcvQUEUE_TIMEOUT)
        {
            /* Hardware is probably dead... */
            return gcvSTATUS_TIMEOUT;
        }

        /* Sleep for 100ms. */
        gcmkERR_BREAK(gckOS_Delay(Command->os, 100));

        /* Not the first loop? */
        if (timeout > 0)
        {
            /* Read IDLE register. */
            gcmkVERIFY_OK(gckVGHARDWARE_GetIdle(Command->hardware, &idle));

            gcmkTRACE_ZONE(
                gcvLEVEL_ERROR, gcvZONE_COMMAND,
                "%s: timeout, IDLE=%08X\n",
                __FUNCTION__, idle
                );
        }

        /* Increment the timeout counter. */
        timeout += 1;
    }

    /* Return status. */
    return status;
}

static gctINT32
_GetNextInterrupt(
    IN gckVGCOMMAND Command,
    IN gceBLOCK Block
    )
{
    gctUINT index;
    gcsBLOCK_TASK_ENTRY_PTR entry;
    gctINT32 interrupt;

    /* Get the block entry. */
    entry = &Command->taskTable[Block];

    /* Make sure we have initialized interrupts. */
    gcmkASSERT(entry->interruptCount > 0);

    /* Decrement the interrupt usage semaphore. */
    gcmkVERIFY_OK(gckOS_DecrementSemaphore(
        Command->os, entry->interruptSemaphore
        ));

    /* Get the value index. */
    index = entry->interruptIndex;

    /* Get the interrupt value. */
    interrupt = entry->interruptArray[index];

    /* Must be a valid value. */
    gcmkASSERT((interrupt >= 0) && (interrupt <= 31));

    /* Advance the index to the next value. */
    index += 1;

    /* Set the new index. */
    entry->interruptIndex = (index == entry->interruptCount)
        ? 0
        : index;

    /* Return interrupt value. */
    return interrupt;
}


/******************************************************************************\
***************************** Task Storage Management **************************
\******************************************************************************/

/* Minimum task buffer size. */
#define gcvMIN_TASK_BUFFER \
( \
    gcmSIZEOF(gcsTASK_CONTAINER) + 128 \
)

/* Free list terminator. */
#define gcvFREE_TASK_TERMINATOR \
( \
    (gcsTASK_CONTAINER_PTR) gcmINT2PTR(~0) \
)


/*----------------------------------------------------------------------------*/
/*------------------- Allocated Task Buffer List Management ------------------*/

static void
_InsertTaskBuffer(
    IN gcsTASK_CONTAINER_PTR AddAfter,
    IN gcsTASK_CONTAINER_PTR Buffer
    )
{
    gcsTASK_CONTAINER_PTR addBefore;

    /* Cannot add before the first buffer. */
    gcmkASSERT(AddAfter != gcvNULL);

    /* Create a shortcut to the next buffer. */
    addBefore = AddAfter->allocNext;

    /* Initialize the links. */
    Buffer->allocPrev = AddAfter;
    Buffer->allocNext = addBefore;

    /* Link to the previous buffer. */
    AddAfter->allocNext = Buffer;

    /* Link to the next buffer. */
    if (addBefore != gcvNULL)
    {
        addBefore->allocPrev = Buffer;
    }
}

static void
_RemoveTaskBuffer(
    IN gcsTASK_CONTAINER_PTR Buffer
    )
{
    gcsTASK_CONTAINER_PTR prev;
    gcsTASK_CONTAINER_PTR next;

    /* Cannot remove the first buffer. */
    gcmkASSERT(Buffer->allocPrev != gcvNULL);

    /* Create shortcuts to the previous and next buffers. */
    prev = Buffer->allocPrev;
    next = Buffer->allocNext;

    /* Tail buffer? */
    if (next == gcvNULL)
    {
        /* Remove from the list. */
        prev->allocNext = gcvNULL;
    }

    /* Buffer from the middle. */
    else
    {
        prev->allocNext = next;
        next->allocPrev = prev;
    }
}


/*----------------------------------------------------------------------------*/
/*--------------------- Free Task Buffer List Management ---------------------*/

static void
_AppendToFreeList(
    IN gckVGCOMMAND Command,
    IN gcsTASK_CONTAINER_PTR Buffer
    )
{
    /* Cannot be a part of the free list already. */
    gcmkASSERT(Buffer->freePrev == gcvNULL);
    gcmkASSERT(Buffer->freeNext == gcvNULL);

    /* First buffer to add? */
    if (Command->taskFreeHead == gcvNULL)
    {
        /* Terminate the links. */
        Buffer->freePrev = gcvFREE_TASK_TERMINATOR;
        Buffer->freeNext = gcvFREE_TASK_TERMINATOR;

        /* Initialize the list pointer. */
        Command->taskFreeHead = Command->taskFreeTail = Buffer;
    }

    /* Not the first, add after the tail. */
    else
    {
        /* Initialize the new tail buffer. */
        Buffer->freePrev = Command->taskFreeTail;
        Buffer->freeNext = gcvFREE_TASK_TERMINATOR;

        /* Add after the tail. */
        Command->taskFreeTail->freeNext = Buffer;
        Command->taskFreeTail = Buffer;
    }
}

static void
_RemoveFromFreeList(
    IN gckVGCOMMAND Command,
    IN gcsTASK_CONTAINER_PTR Buffer
    )
{
    /* Has to be a part of the free list. */
    gcmkASSERT(Buffer->freePrev != gcvNULL);
    gcmkASSERT(Buffer->freeNext != gcvNULL);

    /* Head buffer? */
    if (Buffer->freePrev == gcvFREE_TASK_TERMINATOR)
    {
        /* Tail buffer as well? */
        if (Buffer->freeNext == gcvFREE_TASK_TERMINATOR)
        {
            /* Reset the list pointer. */
            Command->taskFreeHead = Command->taskFreeTail = gcvNULL;
        }

        /* No, just the head. */
        else
        {
            /* Update the head. */
            Command->taskFreeHead = Buffer->freeNext;

            /* Terminate the next buffer. */
            Command->taskFreeHead->freePrev = gcvFREE_TASK_TERMINATOR;
        }
    }

    /* Not the head. */
    else
    {
        /* Tail buffer? */
        if (Buffer->freeNext == gcvFREE_TASK_TERMINATOR)
        {
            /* Update the tail. */
            Command->taskFreeTail = Buffer->freePrev;

            /* Terminate the previous buffer. */
            Command->taskFreeTail->freeNext = gcvFREE_TASK_TERMINATOR;
        }

        /* A buffer in the middle. */
        else
        {
            /* Remove the buffer from the list. */
            Buffer->freePrev->freeNext = Buffer->freeNext;
            Buffer->freeNext->freePrev = Buffer->freePrev;
        }
    }

    /* Reset free list pointers. */
    Buffer->freePrev = gcvNULL;
    Buffer->freeNext = gcvNULL;
}


/*----------------------------------------------------------------------------*/
/*-------------------------- Task Buffer Allocation --------------------------*/

static void
_SplitTaskBuffer(
    IN gckVGCOMMAND Command,
    IN gcsTASK_CONTAINER_PTR Buffer,
    IN gctUINT Size
    )
{
    /* Determine the size of the new buffer. */
    gctINT splitBufferSize = Buffer->size - Size;
    gcmkASSERT(splitBufferSize >= 0);

    /* Is the split buffer big enough to become a separate buffer? */
    if (splitBufferSize >= gcvMIN_TASK_BUFFER)
    {
        /* Place the new path data. */
        gcsTASK_CONTAINER_PTR splitBuffer = (gcsTASK_CONTAINER_PTR)
        (
            (gctUINT8_PTR) Buffer + Size
        );

        /* Set the trimmed buffer size. */
        Buffer->size = Size;

        /* Initialize the split buffer. */
        splitBuffer->referenceCount = 0;
        splitBuffer->size           = splitBufferSize;
        splitBuffer->freePrev       = gcvNULL;
        splitBuffer->freeNext       = gcvNULL;

        /* Link in. */
        _InsertTaskBuffer(Buffer, splitBuffer);
        _AppendToFreeList(Command, splitBuffer);
    }
}

static gceSTATUS
_AllocateTaskContainer(
    IN gckVGCOMMAND Command,
    IN gctUINT Size,
    OUT gcsTASK_CONTAINER_PTR * Buffer
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Command=0x%x Size=0x%x, Buffer ==0x%x", Command, Size, Buffer);

    /* Verify arguments. */
    gcmkVERIFY_ARGUMENT(Buffer != gcvNULL);

    do
    {
        gcsTASK_STORAGE_PTR storage;
        gcsTASK_CONTAINER_PTR buffer;

        /* Adjust the size. */
        Size += gcmSIZEOF(gcsTASK_CONTAINER);

        /* Adjust the allocation size if not big enough. */
        if (Size > Command->taskStorageUsable)
        {
            Command->taskStorageGranularity
                = gcmALIGN(Size + gcmSIZEOF(gcsTASK_STORAGE), 1024);

            Command->taskStorageUsable
                = Command->taskStorageGranularity - gcmSIZEOF(gcsTASK_STORAGE);
        }

        /* Is there a free buffer available? */
        else if (Command->taskFreeHead != gcvNULL)
        {
            /* Set the initial free buffer. */
            gcsTASK_CONTAINER_PTR buffer = Command->taskFreeHead;

            do
            {
                /* Is the buffer big enough? */
                if (buffer->size >= Size)
                {
                    /* Remove the buffer from the free list. */
                    _RemoveFromFreeList(Command, buffer);

                    /* Split the buffer. */
                    _SplitTaskBuffer(Command, buffer, Size);

                    /* Set the result. */
                    * Buffer = buffer;

                    gcmkFOOTER_ARG("*Buffer=0x%x",*Buffer);
                    /* Success. */
                    return gcvSTATUS_OK;
                }

                /* Get the next free buffer. */
                buffer = buffer->freeNext;
            }
            while (buffer != gcvFREE_TASK_TERMINATOR);
        }

        /* Allocate a container. */
        gcmkERR_BREAK(gckOS_Allocate(
            Command->os,
            Command->taskStorageGranularity,
            (gctPOINTER *) &storage
            ));

        /* Link in the storage buffer. */
        storage->next = Command->taskStorage;
        Command->taskStorage = storage;

        /* Place the task buffer. */
        buffer = (gcsTASK_CONTAINER_PTR) (storage + 1);

        /* Determine the size of the buffer. */
        buffer->size
            = Command->taskStorageGranularity
            - gcmSIZEOF(gcsTASK_STORAGE);

        /* Initialize the task buffer. */
        buffer->referenceCount = 0;
        buffer->allocPrev      = gcvNULL;
        buffer->allocNext      = gcvNULL;
        buffer->freePrev       = gcvNULL;
        buffer->freeNext       = gcvNULL;

        /* Split the buffer. */
        _SplitTaskBuffer(Command, buffer, Size);

        /* Set the result. */
        * Buffer = buffer;

        gcmkFOOTER_ARG("*Buffer=0x%x",*Buffer);
        /* Success. */
        return gcvSTATUS_OK;
    }
    while (gcvFALSE);

    gcmkFOOTER();
    /* Return status. */
    return status;
}

static void
_FreeTaskContainer(
    IN gckVGCOMMAND Command,
    IN gcsTASK_CONTAINER_PTR Buffer
    )
{
    gcsTASK_CONTAINER_PTR prev;
    gcsTASK_CONTAINER_PTR next;
    gcsTASK_CONTAINER_PTR merged;

    gctUINT32 mergedSize;

    /* Verify arguments. */
    gcmkASSERT(Buffer != gcvNULL);
    gcmkASSERT(Buffer->freePrev == gcvNULL);
    gcmkASSERT(Buffer->freeNext == gcvNULL);

    /* Get shortcuts to the previous and next path data buffers. */
    prev = Buffer->allocPrev;
    next = Buffer->allocNext;

    /* Is the previous path data buffer already free? */
    if (prev && prev->freeNext)
    {
        /* The previous path data buffer is the one that remains. */
        merged = prev;

        /* Is the next path data buffer already free? */
        if (next && next->freeNext)
        {
            /* Merge all three path data buffers into the previous. */
            mergedSize = prev->size + Buffer->size + next->size;

            /* Remove the next path data buffer. */
            _RemoveFromFreeList(Command, next);
            _RemoveTaskBuffer(next);
        }
        else
        {
            /* Merge the current path data buffer into the previous. */
            mergedSize = prev->size + Buffer->size;
        }

        /* Delete the current path data buffer. */
        _RemoveTaskBuffer(Buffer);

        /* Set new size. */
        merged->size = mergedSize;
    }
    else
    {
        /* The current path data buffer is the one that remains. */
        merged = Buffer;

        /* Is the next buffer already free? */
        if (next && next->freeNext)
        {
            /* Merge the next into the current. */
            mergedSize = Buffer->size + next->size;

            /* Remove the next buffer. */
            _RemoveFromFreeList(Command, next);
            _RemoveTaskBuffer(next);

            /* Set new size. */
            merged->size = mergedSize;
        }

        /* Add the current buffer into the free list. */
        _AppendToFreeList(Command, merged);
    }
}

gceSTATUS
_RemoveRecordFromProcesDB(
    IN gckVGCOMMAND Command,
    IN gcsTASK_HEADER_PTR Task
    )
{
    gceSTATUS status;
    gcsTASK_PTR task = (gcsTASK_PTR)((gctUINT8_PTR)Task - sizeof(gcsTASK));
    gcsTASK_FREE_VIDEO_MEMORY_PTR freeVideoMemory;
    gcsTASK_UNLOCK_VIDEO_MEMORY_PTR unlockVideoMemory;
    gctINT pid;
    gctUINT32 size;
    gctUINT32 handle;
    gckKERNEL kernel = Command->kernel->kernel;
    gckVIDMEM_NODE unlockNode = gcvNULL;
    gckVIDMEM_NODE nodeObject = gcvNULL;
    gceDATABASE_TYPE type;

    /* Get the total size of all tasks. */
    size = task->size;

    gcmkVERIFY_OK(gckOS_GetProcessID((gctUINT32_PTR)&pid));

    do
    {
        switch (Task->id)
        {
        case gcvTASK_FREE_VIDEO_MEMORY:
            freeVideoMemory = (gcsTASK_FREE_VIDEO_MEMORY_PTR)Task;

            handle = (gctUINT32)freeVideoMemory->node;

            status = gckVIDMEM_HANDLE_Lookup(
                Command->kernel->kernel,
                pid,
                handle,
                &nodeObject);

            if (gcmIS_ERROR(status))
            {
                return status;
            }

            gckVIDMEM_HANDLE_Dereference(kernel, pid, handle);
            freeVideoMemory->node = gcmALL_TO_UINT32(nodeObject);

            type = gcvDB_VIDEO_MEMORY
                | (nodeObject->type << gcdDB_VIDEO_MEMORY_TYPE_SHIFT)
                | (nodeObject->pool << gcdDB_VIDEO_MEMORY_POOL_SHIFT);

            /* Remove record from process db. */
            gcmkVERIFY_OK(gckKERNEL_RemoveProcessDB(
                Command->kernel->kernel,
                pid,
                type,
                gcmINT2PTR(handle)));

            /* Advance to next task. */
            size -= sizeof(gcsTASK_FREE_VIDEO_MEMORY);
            Task = (gcsTASK_HEADER_PTR)(freeVideoMemory + 1);

            break;
        case gcvTASK_UNLOCK_VIDEO_MEMORY:
            unlockVideoMemory = (gcsTASK_UNLOCK_VIDEO_MEMORY_PTR)Task;

            /* Remove record from process db. */
            gcmkVERIFY_OK(gckKERNEL_RemoveProcessDB(
                Command->kernel->kernel,
                pid,
                gcvDB_VIDEO_MEMORY_LOCKED,
                gcmUINT64_TO_PTR(unlockVideoMemory->node)));

            handle = (gctUINT32)unlockVideoMemory->node;

            status = gckVIDMEM_HANDLE_Lookup(
                Command->kernel->kernel,
                pid,
                handle,
                &unlockNode);

            if (gcmIS_ERROR(status))
            {
                return status;
            }

            gckVIDMEM_HANDLE_Dereference(kernel, pid, handle);
            unlockVideoMemory->node = gcmPTR_TO_UINT64(unlockNode);

            /* Advance to next task. */
            size -= sizeof(gcsTASK_UNLOCK_VIDEO_MEMORY);
            Task = (gcsTASK_HEADER_PTR)(unlockVideoMemory + 1);

            break;
        default:
            /* Skip the whole task. */
            size = 0;
            break;
        }
    }
    while(size);

    return gcvSTATUS_OK;
}

/******************************************************************************\
********************************* Task Scheduling ******************************
\******************************************************************************/

static gceSTATUS
_ScheduleTasks(
    IN gckVGCOMMAND Command,
    IN gcsTASK_MASTER_TABLE_PTR TaskTable,
    IN gctUINT8_PTR PreviousEnd
    )
{
    gceSTATUS status;

    do
    {
        gctINT block;
        gcsTASK_CONTAINER_PTR container;
        gcsTASK_MASTER_ENTRY_PTR userTaskEntry;
        gcsBLOCK_TASK_ENTRY_PTR kernelTaskEntry;
        gcsTASK_PTR userTask;
        gctUINT8_PTR kernelTask;
        gctINT32 interrupt;
        gctUINT8_PTR eventCommand;

#ifdef __QNXNTO__
        gcsTASK_PTR oldUserTask = gcvNULL;
        gctPOINTER pointer;
#endif

        /* Nothing to schedule? */
        if (TaskTable->size == 0)
        {
            status = gcvSTATUS_OK;
            break;
        }

        /* Acquire the mutex. */
        gcmkERR_BREAK(gckOS_AcquireMutex(
            Command->os,
            Command->taskMutex,
            gcvINFINITE
            ));

        gcmkTRACE_ZONE(
            gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
            "%s(%d)\n",
            __FUNCTION__, __LINE__
            );

        do
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
                "  number of tasks scheduled   = %d\n"
                "  size of event data in bytes = %d\n",
                TaskTable->count,
                TaskTable->size
                );

            /* Allocate task buffer. */
            gcmkERR_BREAK(_AllocateTaskContainer(
                Command,
                TaskTable->size,
                &container
                ));

            /* Determine the task data pointer. */
            kernelTask = (gctUINT8_PTR) (container + 1);

            /* Initialize the reference count. */
            container->referenceCount = TaskTable->count;

            /* Process tasks. */
            for (block = gcvBLOCK_COUNT - 1; block >= 0; block -= 1)
            {
                /* Get the current user table entry. */
                userTaskEntry = &TaskTable->table[block];

                /* Are there tasks scheduled? */
                if (userTaskEntry->head == gcvNULL)
                {
                    /* No, skip to the next block. */
                    continue;
                }

                gcmkTRACE_ZONE(
                    gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
                    "  processing tasks for block %d\n",
                    block
                    );

                /* Get the current kernel table entry. */
                kernelTaskEntry = &Command->taskTable[block];

                /* Are there tasks for the current block scheduled? */
                if (kernelTaskEntry->container == gcvNULL)
                {
                    gcmkTRACE_ZONE(
                        gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
                        "  first task container for the block added\n",
                        block
                        );

                    /* Nothing yet, set the container buffer pointer. */
                    kernelTaskEntry->container = container;
                    kernelTaskEntry->task      = (gcsTASK_HEADER_PTR) kernelTask;
                }

                /* Yes, append to the end. */
                else
                {
                    kernelTaskEntry->link->cotainer = container;
                    kernelTaskEntry->link->task     = (gcsTASK_HEADER_PTR) kernelTask;
                }

                /* Set initial task. */
                userTask = userTaskEntry->head;

                gcmkTRACE_ZONE(
                    gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
                    "  copying user tasks over to the kernel\n"
                    );

                /* Copy tasks. */
                do
                {
                    gcsTASK_HEADER_PTR taskHeader;

#ifdef __QNXNTO__
                    oldUserTask = userTask;

                    gcmkERR_BREAK(gckOS_MapUserPointer(
                        Command->os,
                        oldUserTask,
                        0,
                        &pointer));

                    userTask = pointer;
#endif

                    taskHeader = (gcsTASK_HEADER_PTR) (userTask + 1);

                    gcmkVERIFY_OK(_RemoveRecordFromProcesDB(Command, taskHeader));

                    gcmkTRACE_ZONE(
                        gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
                        "    task ID = %d, size = %d\n",
                        ((gcsTASK_HEADER_PTR) (userTask + 1))->id,
                        userTask->size
                        );

                    /* Copy the task data. */
                    gcmkVERIFY_OK(gckOS_MemCopy(
                        kernelTask, taskHeader, userTask->size
                        ));

#ifdef __QNXNTO__
                    if (taskHeader->id == gcvTASK_SIGNAL)
                    {
                        gcsTASK_SIGNAL_PTR taskSignal = (gcsTASK_SIGNAL_PTR)kernelTask;
                        gctPOINTER signal;
                        gctUINT32 pid;

                        gcmkVERIFY_OK(gckOS_GetProcessID(&pid));

                        taskSignal->coid  = TaskTable->coid;
                        taskSignal->rcvid = TaskTable->rcvid;

                        gcmkERR_BREAK(drv_signal_mgr_add(
                            pid,
                            taskSignal->coid,
                            taskSignal->rcvid,
                            gcmPTR_TO_UINT64(taskSignal->signal),
                            &signal));

                        taskSignal->signal = signal;
                    }
#endif

                    /* Advance to the next task. */
                    kernelTask += userTask->size;
                    userTask    = userTask->next;

#ifdef __QNXNTO__
                    gcmkERR_BREAK(gckOS_UnmapUserPointer(
                        Command->os,
                        oldUserTask,
                        0,
                        pointer));
#endif
                }
                while (userTask != gcvNULL);

                /* Update link pointer in the header. */
                kernelTaskEntry->link = (gcsTASK_LINK_PTR) kernelTask;

                /* Initialize link task. */
                kernelTaskEntry->link->id       = gcvTASK_LINK;
                kernelTaskEntry->link->cotainer = gcvNULL;
                kernelTaskEntry->link->task     = gcvNULL;

                /* Advance the task data pointer. */
                kernelTask += gcmSIZEOF(gcsTASK_LINK);
            }
        }
        while (gcvFALSE);

        /* Release the mutex. */
        gcmkERR_BREAK(gckOS_ReleaseMutex(
            Command->os,
            Command->taskMutex
            ));

        /* Assign interrupts to the blocks. */
        eventCommand = PreviousEnd;

        for (block = gcvBLOCK_COUNT - 1; block >= 0; block -= 1)
        {
            /* Get the current user table entry. */
            userTaskEntry = &TaskTable->table[block];

            /* Are there tasks scheduled? */
            if (userTaskEntry->head == gcvNULL)
            {
                /* No, skip to the next block. */
                continue;
            }

            /* Get the interrupt number. */
            interrupt = _GetNextInterrupt(Command, block);

            gcmkTRACE_ZONE(
                gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
                "%s(%d): block = %d interrupt = %d\n",
                __FUNCTION__, __LINE__,
                block, interrupt
                );

            /* Determine the command position. */
            eventCommand -= Command->info.eventCommandSize;

            /* Append an EVENT command. */
            gcmkERR_BREAK(gckVGCOMMAND_EventCommand(
                Command, eventCommand, block, interrupt, gcvNULL
                ));
        }
    }
    while (gcvFALSE);

    /* Return status. */
    return status;
}


/******************************************************************************\
******************************** Memory Management *****************************
\******************************************************************************/

static gceSTATUS
_HardwareToKernel(
    IN gckOS Os,
    IN gcuVIDMEM_NODE_PTR Node,
    IN gctUINT32 Address,
    OUT gctPOINTER * KernelPointer
    )
{
    gceSTATUS status;
    gckVIDMEM memory;
    gctUINT32 offset;
    gctUINT32 nodePhysical;
    gctPOINTER *logical;
    gctSIZE_T bytes;
    status = gcvSTATUS_OK;

    memory = Node->VidMem.memory;

    if (memory->object.type == gcvOBJ_VIDMEM)
    {
        nodePhysical = memory->baseAddress
                     + (gctUINT32)Node->VidMem.offset
                     + Node->VidMem.alignment;
        bytes = Node->VidMem.bytes;
        logical = &Node->VidMem.kernelVirtual;
    }
    else
    {
        gcmkSAFECASTPHYSADDRT(nodePhysical, Node->Virtual.physicalAddress);
        bytes = Node->Virtual.bytes;
        logical = &Node->Virtual.kernelVirtual;
    }

    if (*logical == gcvNULL)
    {
        status = gckOS_MapPhysical(Os, nodePhysical, bytes, logical);

        if (gcmkIS_ERROR(status))
        {
            return status;
        }
    }

    offset = Address - nodePhysical;
    *KernelPointer = (gctPOINTER)((gctUINT8_PTR)(*logical) + offset);

    /* Return status. */
    return status;
}

static gceSTATUS
_ConvertUserCommandBufferPointer(
    IN gckVGCOMMAND Command,
    IN gcsCMDBUFFER_PTR UserCommandBuffer,
    OUT gcsCMDBUFFER_PTR * KernelCommandBuffer
    )
{
    gceSTATUS status, last;
    gcsCMDBUFFER_PTR mappedUserCommandBuffer = gcvNULL;
    gckKERNEL kernel = Command->kernel->kernel;
    gctUINT32 pid;
    gckVIDMEM_NODE node;

    gckOS_GetProcessID(&pid);

    do
    {
        gctUINT32 headerAddress;

        /* Map the command buffer structure into the kernel space. */
        gcmkERR_BREAK(gckOS_MapUserPointer(
            Command->os,
            UserCommandBuffer,
            gcmSIZEOF(gcsCMDBUFFER),
            (gctPOINTER *) &mappedUserCommandBuffer
            ));

        /* Determine the address of the header. */
        headerAddress
            = mappedUserCommandBuffer->address
            - mappedUserCommandBuffer->bufferOffset;

        gcmkERR_BREAK(gckVIDMEM_HANDLE_Lookup(
            kernel,
            pid,
            gcmPTR2INT32(mappedUserCommandBuffer->node),
            &node));

        /* Translate the logical address to the kernel space. */
        gcmkERR_BREAK(_HardwareToKernel(
            Command->os,
            node->node,
            headerAddress,
            (gctPOINTER *) KernelCommandBuffer
            ));
    }
    while (gcvFALSE);

    /* Unmap the user command buffer. */
    if (mappedUserCommandBuffer != gcvNULL)
    {
        gcmkCHECK_STATUS(gckOS_UnmapUserPointer(
            Command->os,
            UserCommandBuffer,
            gcmSIZEOF(gcsCMDBUFFER),
            mappedUserCommandBuffer
            ));
    }

    /* Return status. */
    return status;
}

static gceSTATUS
_AllocateLinear(
    IN gckVGCOMMAND Command,
    IN gctUINT Size,
    IN gctUINT Alignment,
    OUT gcuVIDMEM_NODE_PTR * Node,
    OUT gctUINT32 * Address,
    OUT gctPOINTER * Logical
    )
{
    gceSTATUS status, last;
    gctPOINTER logical;
    gctPHYS_ADDR physical;
    gctUINT32 address;
    gctSIZE_T size = Size;
    gctPHYS_ADDR_T paddr;

    do
    {
        gcmkERR_BREAK(gckOS_AllocateContiguous(
            Command->os,
            gcvFALSE,
            &size,
            &physical,
            &logical
            ));

        gcmkERR_BREAK(gckOS_GetPhysicalAddress(Command->os, logical, &paddr));

        gcmkSAFECASTPHYSADDRT(address, paddr);

        /* Set return values. */
        * Node    = physical;
        * Address = address;
        * Logical = logical;

        /* Success. */
        return gcvSTATUS_OK;
    }
    while (gcvFALSE);

    /* Roll back. */
    if (physical != gcvNULL)
    {
        /* Free the command buffer. */
        gcmkCHECK_STATUS(gckOS_FreeContiguous(Command->os, physical, logical, size));
    }

    /* Return status. */
    return status;
}

static gceSTATUS
_FreeLinear(
    IN gckVGKERNEL Kernel,
    IN gcuVIDMEM_NODE_PTR Node,
    IN gctPOINTER Logical
    )
{
    gceSTATUS status = gcvSTATUS_OK;

    do
    {
        gcmkERR_BREAK(gckOS_FreeContiguous(Kernel->os, Node, Logical, 1));
    }
    while (gcvFALSE);

    /* Return status. */
    return status;
}

gceSTATUS
_AllocateCommandBuffer(
    IN gckVGCOMMAND Command,
    IN gctSIZE_T Size,
    OUT gcsCMDBUFFER_PTR * CommandBuffer
    )
{
    gceSTATUS status, last;
    gcuVIDMEM_NODE_PTR node = gcvNULL;
    gcsCMDBUFFER_PTR commandBuffer = gcvNULL;

    do
    {
        gctUINT alignedHeaderSize;
        gctUINT requestedSize;
        gctUINT allocationSize;
        gctUINT32 address = 0;
        gctUINT8_PTR endCommand;

        /* Determine the aligned header size. */
        alignedHeaderSize
            = (gctUINT32)gcmALIGN(gcmSIZEOF(gcsCMDBUFFER), Command->info.addressAlignment);

        /* Align the requested size. */
        requestedSize
            = (gctUINT32)gcmALIGN(Size, Command->info.commandAlignment);

        /* Determine the size of the buffer to allocate. */
        allocationSize
            = alignedHeaderSize
            + requestedSize
            + (gctUINT32)Command->info.staticTailSize;

        /* Allocate the command buffer. */
        gcmkERR_BREAK(_AllocateLinear(
            Command,
            allocationSize,
            Command->info.addressAlignment,
            &node,
            &address,
            (gctPOINTER *) &commandBuffer
            ));

        /* Initialize the structure. */
        commandBuffer->completion    = gcvVACANT_BUFFER;
        commandBuffer->node          = node;
        commandBuffer->address       = address + alignedHeaderSize;
        commandBuffer->bufferOffset  = alignedHeaderSize;
        commandBuffer->size          = requestedSize;
        commandBuffer->offset        = requestedSize;
        commandBuffer->nextAllocated = gcvNULL;
        commandBuffer->nextSubBuffer = gcvNULL;

        /* Determine the data count. */
        commandBuffer->dataCount
            = (requestedSize + Command->info.staticTailSize)
            / Command->info.commandAlignment;

        /* Determine the location of the END command. */
        endCommand
            = (gctUINT8_PTR) commandBuffer
            + alignedHeaderSize
            + requestedSize;

        /* Append an END command. */
        gcmkERR_BREAK(gckVGCOMMAND_EndCommand(
            Command,
            endCommand,
            Command->info.feBufferInt,
            gcvNULL
            ));

        /* Set the return pointer. */
        * CommandBuffer = commandBuffer;

        /* Success. */
        return gcvSTATUS_OK;
    }
    while (gcvFALSE);

    /* Roll back. */
    if (node != gcvNULL)
    {
        /* Free the command buffer. */
        gcmkCHECK_STATUS(_FreeLinear(Command->kernel, node, commandBuffer));
    }

    /* Return status. */
    return status;
}

static gceSTATUS
_FreeCommandBuffer(
    IN gckVGKERNEL Kernel,
    IN gcsCMDBUFFER_PTR CommandBuffer
    )
{
    gceSTATUS status;

    /* Free the buffer. */
    status = _FreeLinear(Kernel, CommandBuffer->node, CommandBuffer);

    /* Return status. */
    return status;
}


/******************************************************************************\
****************************** TS Overflow Handler *****************************
\******************************************************************************/

static gceSTATUS
_EventHandler_TSOverflow(
    IN gckVGKERNEL Kernel
    )
{
    gcmkTRACE(
        gcvLEVEL_ERROR,
        "%s(%d): **** TS OVERFLOW ENCOUNTERED ****\n",
        __FUNCTION__, __LINE__
        );

    return gcvSTATUS_OK;
}


/******************************************************************************\
****************************** Bus Error Handler *******************************
\******************************************************************************/

static gceSTATUS
_EventHandler_BusError(
    IN gckVGKERNEL Kernel
    )
{
    gcmkTRACE(
        gcvLEVEL_ERROR,
        "%s(%d): **** BUS ERROR ENCOUNTERED ****\n",
        __FUNCTION__, __LINE__
        );

    return gcvSTATUS_OK;
}

/******************************************************************************\
****************************** Power Stall Handler *******************************
\******************************************************************************/

static gceSTATUS
_EventHandler_PowerStall(
    IN gckVGKERNEL Kernel
    )
{
    /* Signal. */
    return gckOS_Signal(
        Kernel->os,
        Kernel->command->powerStallSignal,
        gcvTRUE);
}

/******************************************************************************\
******************************** Task Routines *********************************
\******************************************************************************/

typedef gceSTATUS (* gctTASKROUTINE) (
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    );

static gceSTATUS
_TaskLink(
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    );

static gceSTATUS
_TaskCluster(
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    );

static gceSTATUS
_TaskIncrement(
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    );

static gceSTATUS
_TaskDecrement(
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    );

static gceSTATUS
_TaskSignal(
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    );

static gceSTATUS
_TaskLockdown(
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    );

static gceSTATUS
_TaskUnlockVideoMemory(
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    );

static gceSTATUS
_TaskFreeVideoMemory(
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    );

static gceSTATUS
_TaskFreeContiguousMemory(
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    );

static gceSTATUS
_TaskUnmapUserMemory(
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    );

static gctTASKROUTINE _taskRoutine[] =
{
    _TaskLink,                  /* gcvTASK_LINK                   */
    _TaskCluster,               /* gcvTASK_CLUSTER                */
    _TaskIncrement,             /* gcvTASK_INCREMENT              */
    _TaskDecrement,             /* gcvTASK_DECREMENT              */
    _TaskSignal,                /* gcvTASK_SIGNAL                 */
    _TaskLockdown,              /* gcvTASK_LOCKDOWN               */
    _TaskUnlockVideoMemory,     /* gcvTASK_UNLOCK_VIDEO_MEMORY    */
    _TaskFreeVideoMemory,       /* gcvTASK_FREE_VIDEO_MEMORY      */
    _TaskFreeContiguousMemory,  /* gcvTASK_FREE_CONTIGUOUS_MEMORY */
    _TaskUnmapUserMemory,       /* gcvTASK_UNMAP_USER_MEMORY      */
};

static gceSTATUS
_TaskLink(
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    )
{
    /* Cast the task pointer. */
    gcsTASK_LINK_PTR task = (gcsTASK_LINK_PTR) TaskHeader->task;

    /* Save the pointer to the container. */
    gcsTASK_CONTAINER_PTR container = TaskHeader->container;

    /* No more tasks in the list? */
    if (task->task == gcvNULL)
    {
        /* Reset the entry. */
        TaskHeader->container = gcvNULL;
        TaskHeader->task      = gcvNULL;
        TaskHeader->link      = gcvNULL;
    }
    else
    {
        /* Update the entry. */
        TaskHeader->container = task->cotainer;
        TaskHeader->task      = task->task;
    }

    /* Decrement the task buffer reference. */
    gcmkASSERT(container->referenceCount >= 0);
    if (container->referenceCount == 0)
    {
        /* Free the container. */
        _FreeTaskContainer(Command, container);
    }

    /* Success. */
    return gcvSTATUS_OK;
}

static gceSTATUS
_TaskCluster(
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    )
{
    gceSTATUS status = gcvSTATUS_OK;

    /* Cast the task pointer. */
    gcsTASK_CLUSTER_PTR cluster = (gcsTASK_CLUSTER_PTR) TaskHeader->task;

    /* Get the number of tasks. */
    gctUINT taskCount = cluster->taskCount;

    /* Advance to the next task. */
    TaskHeader->task = (gcsTASK_HEADER_PTR) (cluster + 1);

    /* Perform all tasks in the cluster. */
    while (taskCount)
    {
        /* Perform the current task. */
        gcmkERR_BREAK(_taskRoutine[TaskHeader->task->id](
            Command,
            TaskHeader
            ));

        /* Update the task count. */
        taskCount -= 1;
    }

    /* Return status. */
    return status;
}

static gceSTATUS
_TaskIncrement(
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    )
{
    gceSTATUS status;

    do
    {
        /* Cast the task pointer. */
        gcsTASK_INCREMENT_PTR task = (gcsTASK_INCREMENT_PTR) TaskHeader->task;

        /* Convert physical into logical address. */
        gctUINT32_PTR logical;
        gcmkERR_BREAK(gckOS_MapPhysical(
            Command->os,
            task->address,
            gcmSIZEOF(gctUINT32),
            (gctPOINTER *) &logical
            ));

        /* Increment data. */
        (* logical) += 1;

        /* Unmap the physical memory. */
        gcmkERR_BREAK(gckOS_UnmapPhysical(
            Command->os,
            logical,
            gcmSIZEOF(gctUINT32)
            ));

        /* Update the reference counter. */
        TaskHeader->container->referenceCount -= 1;

        /* Update the task pointer. */
        TaskHeader->task = (gcsTASK_HEADER_PTR) (task + 1);
    }
    while (gcvFALSE);

    /* Return status. */
    return status;
}

static gceSTATUS
_TaskDecrement(
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    )
{
    gceSTATUS status;

    do
    {
        /* Cast the task pointer. */
        gcsTASK_DECREMENT_PTR task = (gcsTASK_DECREMENT_PTR) TaskHeader->task;

        /* Convert physical into logical address. */
        gctUINT32_PTR logical;
        gcmkERR_BREAK(gckOS_MapPhysical(
            Command->os,
            task->address,
            gcmSIZEOF(gctUINT32),
            (gctPOINTER *) &logical
            ));

        /* Decrement data. */
        (* logical) -= 1;

        /* Unmap the physical memory. */
        gcmkERR_BREAK(gckOS_UnmapPhysical(
            Command->os,
            logical,
            gcmSIZEOF(gctUINT32)
            ));

        /* Update the reference counter. */
        TaskHeader->container->referenceCount -= 1;

        /* Update the task pointer. */
        TaskHeader->task = (gcsTASK_HEADER_PTR) (task + 1);
    }
    while (gcvFALSE);

    /* Return status. */
    return status;
}

static gceSTATUS
_TaskSignal(
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    )
{
    gceSTATUS status;

    do
    {
        /* Cast the task pointer. */
        gcsTASK_SIGNAL_PTR task = (gcsTASK_SIGNAL_PTR) TaskHeader->task;


        /* Map the signal into kernel space. */
#ifdef __QNXNTO__
        gcmkERR_BREAK(gckOS_UserSignal(
            Command->os, task->signal, task->rcvid, task->coid
            ));
#else
        gcmkERR_BREAK(gckOS_UserSignal(
            Command->os, task->signal, task->process
            ));
#endif /* __QNXNTO__ */

        /* Update the reference counter. */
        TaskHeader->container->referenceCount -= 1;

        /* Update the task pointer. */
        TaskHeader->task = (gcsTASK_HEADER_PTR) (task + 1);
    }
    while (gcvFALSE);

    /* Return status. */
    return status;
}

static gceSTATUS
_TaskLockdown(
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    )
{
    gceSTATUS status;
    gctUINT32_PTR userCounter   = gcvNULL;
    gctUINT32_PTR kernelCounter = gcvNULL;
    gctSIGNAL signal            = gcvNULL;

    do
    {
        /* Cast the task pointer. */
        gcsTASK_LOCKDOWN_PTR task = (gcsTASK_LOCKDOWN_PTR) TaskHeader->task;

        /* Convert physical addresses into logical. */
        gcmkERR_BREAK(gckOS_MapPhysical(
            Command->os,
            task->userCounter,
            gcmSIZEOF(gctUINT32),
            (gctPOINTER *) &userCounter
            ));

        gcmkERR_BREAK(gckOS_MapPhysical(
            Command->os,
            task->kernelCounter,
            gcmSIZEOF(gctUINT32),
            (gctPOINTER *) &kernelCounter
            ));

        /* Update the kernel counter. */
        (* kernelCounter) += 1;

        /* Are the counters equal? */
        if ((* userCounter) == (* kernelCounter))
        {
            /* Map the signal into kernel space. */
            gcmkERR_BREAK(gckOS_MapSignal(
                Command->os, task->signal, task->process, &signal
                ));

            if (signal == gcvNULL)
            {
                /* Signal. */
                gcmkERR_BREAK(gckOS_Signal(
                    Command->os, task->signal, gcvTRUE
                    ));
            }
            else
            {
                /* Signal. */
                gcmkERR_BREAK(gckOS_Signal(
                    Command->os, signal, gcvTRUE
                    ));
            }
        }

        /* Update the reference counter. */
        TaskHeader->container->referenceCount -= 1;

        /* Update the task pointer. */
        TaskHeader->task = (gcsTASK_HEADER_PTR) (task + 1);
    }
    while (gcvFALSE);

    /* Destroy the mapped signal. */
    if (signal != gcvNULL)
    {
        gcmkVERIFY_OK(gckOS_DestroySignal(
            Command->os, signal
            ));
    }

    /* Unmap the physical memory. */
    if (kernelCounter != gcvNULL)
    {
        gcmkVERIFY_OK(gckOS_UnmapPhysical(
            Command->os,
            kernelCounter,
            gcmSIZEOF(gctUINT32)
            ));
    }

    if (userCounter != gcvNULL)
    {
        gcmkVERIFY_OK(gckOS_UnmapPhysical(
            Command->os,
            userCounter,
            gcmSIZEOF(gctUINT32)
            ));
    }

    /* Return status. */
    return status;
}

static gceSTATUS
_TaskUnlockVideoMemory(
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    )
{
    gceSTATUS status;

    do
    {
        /* Cast the task pointer. */
        gcsTASK_UNLOCK_VIDEO_MEMORY_PTR task
            = (gcsTASK_UNLOCK_VIDEO_MEMORY_PTR) TaskHeader->task;

        /* Unlock video memory. */
        gcmkERR_BREAK(gckVIDMEM_Unlock(
            Command->kernel->kernel,
            (gckVIDMEM_NODE)gcmUINT64_TO_PTR(task->node),
            gcvSURF_TYPE_UNKNOWN,
            gcvNULL));

        gcmkERR_BREAK(gckVIDMEM_NODE_Dereference(
            Command->kernel->kernel,
            gcmUINT64_TO_PTR(task->node)));

        /* Update the reference counter. */
        TaskHeader->container->referenceCount -= 1;

        /* Update the task pointer. */
        TaskHeader->task = (gcsTASK_HEADER_PTR) (task + 1);
    }
    while (gcvFALSE);

    /* Return status. */
    return status;
}

static gceSTATUS
_TaskFreeVideoMemory(
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    )
{
    gceSTATUS status;

    do
    {
        /* Cast the task pointer. */
        gcsTASK_FREE_VIDEO_MEMORY_PTR task
            = (gcsTASK_FREE_VIDEO_MEMORY_PTR) TaskHeader->task;

        /* Free video memory. */
        gcmkERR_BREAK(gckVIDMEM_NODE_Dereference(
            Command->kernel->kernel,
            gcmINT2PTR(task->node)));

        /* Update the reference counter. */
        TaskHeader->container->referenceCount -= 1;

        /* Update the task pointer. */
        TaskHeader->task = (gcsTASK_HEADER_PTR) (task + 1);
    }
    while (gcvFALSE);

    /* Return status. */
    return status;
}

static gceSTATUS
_TaskFreeContiguousMemory(
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    )
{
    gceSTATUS status;

    do
    {
        /* Cast the task pointer. */
        gcsTASK_FREE_CONTIGUOUS_MEMORY_PTR task
            = (gcsTASK_FREE_CONTIGUOUS_MEMORY_PTR) TaskHeader->task;

        /* Free contiguous memory. */
        gcmkERR_BREAK(gckOS_FreeContiguous(
            Command->os, task->physical, task->logical, task->bytes
            ));

        /* Update the reference counter. */
        TaskHeader->container->referenceCount -= 1;

        /* Update the task pointer. */
        TaskHeader->task = (gcsTASK_HEADER_PTR) (task + 1);
    }
    while (gcvFALSE);

    /* Return status. */
    return status;
}

static gceSTATUS
_TaskUnmapUserMemory(
    gckVGCOMMAND Command,
    gcsBLOCK_TASK_ENTRY_PTR TaskHeader
    )
{
    gceSTATUS status;
    gctPOINTER info;

    do
    {
        /* Cast the task pointer. */
        gcsTASK_UNMAP_USER_MEMORY_PTR task
            = (gcsTASK_UNMAP_USER_MEMORY_PTR) TaskHeader->task;

        info = gckKERNEL_QueryPointerFromName(
                Command->kernel->kernel, gcmALL_TO_UINT32(task->info));

        /* Unmap the user memory. */
        gcmkERR_BREAK(gckOS_UnmapUserMemory(
            Command->os, gcvCORE_VG, task->memory, task->size, info, task->address
            ));

        /* Update the reference counter. */
        TaskHeader->container->referenceCount -= 1;

        /* Update the task pointer. */
        TaskHeader->task = (gcsTASK_HEADER_PTR) (task + 1);
    }
    while (gcvFALSE);

    /* Return status. */
    return status;
}

/******************************************************************************\
************ Hardware Block Interrupt Handlers For Scheduled Events ************
\******************************************************************************/

static gceSTATUS
_EventHandler_Block(
    IN gckVGKERNEL Kernel,
    IN gcsBLOCK_TASK_ENTRY_PTR TaskHeader,
    IN gctBOOL ProcessAll
    )
{
    gceSTATUS status = gcvSTATUS_OK, last;

    gcmkHEADER_ARG("Kernel=0x%x TaskHeader=0x%x ProcessAll=0x%x", Kernel, TaskHeader, ProcessAll);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);

    if (TaskHeader->task == gcvNULL)
    {
        gcmkFOOTER();
        return gcvSTATUS_OK;
    }

    do
    {
        gckVGCOMMAND command;

        /* Get the command buffer object. */
        command = Kernel->command;

        /* Increment the interrupt usage semaphore. */
        gcmkERR_BREAK(gckOS_IncrementSemaphore(
            command->os, TaskHeader->interruptSemaphore
            ));

        /* Acquire the mutex. */
        gcmkERR_BREAK(gckOS_AcquireMutex(
            command->os,
            command->taskMutex,
            gcvINFINITE
            ));

        /* Verify inputs. */
        gcmkASSERT(TaskHeader            != gcvNULL);
        gcmkASSERT(TaskHeader->container != gcvNULL);
        gcmkASSERT(TaskHeader->task      != gcvNULL);
        gcmkASSERT(TaskHeader->link      != gcvNULL);

        /* Process tasks. */
        do
        {
            /* Process the current task. */
            gcmkERR_BREAK(_taskRoutine[TaskHeader->task->id](
                command,
                TaskHeader
                ));

            /* Is the next task is LINK? */
            if (TaskHeader->task->id == gcvTASK_LINK)
            {
                gcmkERR_BREAK(_taskRoutine[TaskHeader->task->id](
                    command,
                    TaskHeader
                    ));

                /* Done. */
                break;
            }
        }
        while (ProcessAll);

        /* Release the mutex. */
        gcmkCHECK_STATUS(gckOS_ReleaseMutex(
            command->os,
            command->taskMutex
            ));
    }
    while (gcvFALSE);

    gcmkFOOTER();
    /* Return status. */
    return status;
}

gcmDECLARE_INTERRUPT_HANDLER(COMMAND, 0)
{
    gceSTATUS status, last;

    gcmkHEADER_ARG("Kernel=0x%x ", Kernel);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);


    do
    {
        gckVGCOMMAND command;
        gcsKERNEL_QUEUE_HEADER_PTR mergeQueue;
        gcsKERNEL_QUEUE_HEADER_PTR queueTail;
        gcsKERNEL_CMDQUEUE_PTR entry;
        gctUINT entryCount;

        /* Get the command buffer object. */
        command = Kernel->command;

        /* Acquire the mutex. */
        gcmkERR_BREAK(gckOS_AcquireMutex(
            command->os,
            command->queueMutex,
            gcvINFINITE
            ));

        /* Get the current queue. */
        queueTail = command->queueTail;

        /* Get the current queue entry. */
        entry = queueTail->currentEntry;

        /* Get the number of entries in the queue. */
        entryCount = queueTail->pending;

        /* Process all entries. */
        while (gcvTRUE)
        {
            /* Call post-execution function. */
            status = entry->handler(Kernel, entry);

            /* Failed? */
            if (gcmkIS_ERROR(status))
            {
                gcmkTRACE_ZONE(
                    gcvLEVEL_ERROR,
                    gcvZONE_COMMAND,
                    "[%s] line %d: post action failed.\n",
                    __FUNCTION__, __LINE__
                    );
            }

            /* Executed the next buffer? */
            if (status == gcvSTATUS_EXECUTED)
            {
                /* Update the queue. */
                queueTail->pending      = entryCount;
                queueTail->currentEntry = entry;

                /* Success. */
                status = gcvSTATUS_OK;

                /* Break out of the loop. */
                break;
            }

            /* Advance to the next entry. */
            entry      += 1;
            entryCount -= 1;

            /* Last entry? */
            if (entryCount == 0)
            {
                /* Reset the queue to idle. */
                queueTail->pending = 0;

                /* Get a shortcut to the queue to merge with. */
                mergeQueue = command->mergeQueue;

                /* Merge the queues if necessary. */
                if (mergeQueue != queueTail)
                {
                    gcmkASSERT(mergeQueue < queueTail);
                    gcmkASSERT(mergeQueue->next == queueTail);

                    mergeQueue->size
                        += gcmSIZEOF(gcsKERNEL_QUEUE_HEADER)
                        + queueTail->size;

                    mergeQueue->next = queueTail->next;
                }

                /* Advance to the next queue. */
                queueTail = queueTail->next;

                /* Did it wrap around? */
                if (command->queue == queueTail)
                {
                    /* Reset merge queue. */
                    command->mergeQueue = queueTail;
                }

                /* Set new queue. */
                command->queueTail = queueTail;

                /* Is the next queue scheduled? */
                if (queueTail->pending > 0)
                {
                    gcsCMDBUFFER_PTR commandBuffer;

                    /* The first entry must be a command buffer. */
                    commandBuffer = queueTail->currentEntry->commandBuffer;

                    /* Start the command processor. */
                    status = gckVGHARDWARE_Execute(
                        command->hardware,
                        commandBuffer->address,
                        commandBuffer->dataCount
                        );

                    /* Failed? */
                    if (gcmkIS_ERROR(status))
                    {
                        gcmkTRACE_ZONE(
                            gcvLEVEL_ERROR,
                            gcvZONE_COMMAND,
                            "[%s] line %d: failed to start the next queue.\n",
                            __FUNCTION__, __LINE__
                            );
                    }
                }
                else
                {
                    status = gckVGHARDWARE_SetPowerManagementState(
                                Kernel->command->hardware, gcvPOWER_IDLE_BROADCAST
                                );
                }

                /* Break out of the loop. */
                break;
            }
        }

        /* Release the mutex. */
        gcmkCHECK_STATUS(gckOS_ReleaseMutex(
            command->os,
            command->queueMutex
            ));
    }
    while (gcvFALSE);


    gcmkFOOTER();
    /* Return status. */
    return status;
}

/* Define standard block interrupt handlers. */
gcmDEFINE_INTERRUPT_HANDLER(TESSELLATOR, 0)
gcmDEFINE_INTERRUPT_HANDLER(VG,          0)
gcmDEFINE_INTERRUPT_HANDLER(PIXEL,       0)
gcmDEFINE_INTERRUPT_HANDLER(PIXEL,       1)
gcmDEFINE_INTERRUPT_HANDLER(PIXEL,       2)
gcmDEFINE_INTERRUPT_HANDLER(PIXEL,       3)
gcmDEFINE_INTERRUPT_HANDLER(PIXEL,       4)
gcmDEFINE_INTERRUPT_HANDLER(PIXEL,       5)
gcmDEFINE_INTERRUPT_HANDLER(PIXEL,       6)
gcmDEFINE_INTERRUPT_HANDLER(PIXEL,       7)
gcmDEFINE_INTERRUPT_HANDLER(PIXEL,       8)
gcmDEFINE_INTERRUPT_HANDLER(PIXEL,       9)

/* The entries in the array are arranged by event priority. */
static gcsBLOCK_INTERRUPT_HANDLER _blockHandlers[] =
{
    gcmDEFINE_INTERRUPT_HANDLER_ENTRY(TESSELLATOR, 0),
    gcmDEFINE_INTERRUPT_HANDLER_ENTRY(VG,          0),
    gcmDEFINE_INTERRUPT_HANDLER_ENTRY(PIXEL,       0),
    gcmDEFINE_INTERRUPT_HANDLER_ENTRY(PIXEL,       1),
    gcmDEFINE_INTERRUPT_HANDLER_ENTRY(PIXEL,       2),
    gcmDEFINE_INTERRUPT_HANDLER_ENTRY(PIXEL,       3),
    gcmDEFINE_INTERRUPT_HANDLER_ENTRY(PIXEL,       4),
    gcmDEFINE_INTERRUPT_HANDLER_ENTRY(PIXEL,       5),
    gcmDEFINE_INTERRUPT_HANDLER_ENTRY(PIXEL,       6),
    gcmDEFINE_INTERRUPT_HANDLER_ENTRY(PIXEL,       7),
    gcmDEFINE_INTERRUPT_HANDLER_ENTRY(PIXEL,       8),
    gcmDEFINE_INTERRUPT_HANDLER_ENTRY(PIXEL,       9),
    gcmDEFINE_INTERRUPT_HANDLER_ENTRY(COMMAND,     0),
};


/******************************************************************************\
************************* Static Command Buffer Handlers ***********************
\******************************************************************************/

static gceSTATUS
_UpdateStaticCommandBuffer(
    IN gckVGKERNEL Kernel,
    IN gcsKERNEL_CMDQUEUE_PTR Entry
    )
{
    gcmkTRACE_ZONE(
        gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
        "%s(%d)\n",
        __FUNCTION__, __LINE__
        );

    /* Success. */
    return gcvSTATUS_OK;
}

static gceSTATUS
_ExecuteStaticCommandBuffer(
    IN gckVGKERNEL Kernel,
    IN gcsKERNEL_CMDQUEUE_PTR Entry
    )
{
    gceSTATUS status;

    do
    {
        gcsCMDBUFFER_PTR commandBuffer;

        /* Cast the command buffer header. */
        commandBuffer = Entry->commandBuffer;

        /* Set to update the command buffer next time. */
        Entry->handler = _UpdateStaticCommandBuffer;

        gcmkTRACE_ZONE(
            gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
            "%s(%d): executing next buffer @ 0x%08X, data count = %d\n",
            __FUNCTION__, __LINE__,
            commandBuffer->address,
            commandBuffer->dataCount
            );

        /* Start the command processor. */
        gcmkERR_BREAK(gckVGHARDWARE_Execute(
            Kernel->hardware,
            commandBuffer->address,
            commandBuffer->dataCount
            ));

        /* Success. */
        return gcvSTATUS_EXECUTED;
    }
    while (gcvFALSE);

    /* Return status. */
    return status;
}

static gceSTATUS
_UpdateLastStaticCommandBuffer(
    IN gckVGKERNEL Kernel,
    IN gcsKERNEL_CMDQUEUE_PTR Entry
    )
{
#if gcvDEBUG || gcdFORCE_MESSAGES
    /* Get the command buffer header. */
    gcsCMDBUFFER_PTR commandBuffer = Entry->commandBuffer;

    /* Validate the command buffer. */
    gcmkASSERT(commandBuffer->completion != gcvNULL);
    gcmkASSERT(commandBuffer->completion != gcvVACANT_BUFFER);

#endif

    gcmkTRACE_ZONE(
        gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
        "%s(%d): processing all tasks scheduled for FE.\n",
        __FUNCTION__, __LINE__
        );

    /* Perform scheduled tasks. */
    return _EventHandler_Block(
        Kernel,
        &Kernel->command->taskTable[gcvBLOCK_COMMAND],
        gcvTRUE
        );
}

static gceSTATUS
_ExecuteLastStaticCommandBuffer(
    IN gckVGKERNEL Kernel,
    IN gcsKERNEL_CMDQUEUE_PTR Entry
    )
{
    gceSTATUS status;

    do
    {
        /* Cast the command buffer header. */
        gcsCMDBUFFER_PTR commandBuffer = Entry->commandBuffer;

        /* Set to update the command buffer next time. */
        Entry->handler = _UpdateLastStaticCommandBuffer;

        gcmkTRACE_ZONE(
            gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
            "%s(%d): executing next buffer @ 0x%08X, data count = %d\n",
            __FUNCTION__, __LINE__,
            commandBuffer->address,
            commandBuffer->dataCount
            );

        /* Start the command processor. */
        gcmkERR_BREAK(gckVGHARDWARE_Execute(
            Kernel->hardware,
            commandBuffer->address,
            commandBuffer->dataCount
            ));

        /* Success. */
        return gcvSTATUS_EXECUTED;
    }
    while (gcvFALSE);

    /* Return status. */
    return status;
}


/******************************************************************************\
************************* Dynamic Command Buffer Handlers **********************
\******************************************************************************/

static gceSTATUS
_UpdateDynamicCommandBuffer(
    IN gckVGKERNEL Kernel,
    IN gcsKERNEL_CMDQUEUE_PTR Entry
    )
{
    gcmkTRACE_ZONE(
        gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
        "%s(%d)\n",
        __FUNCTION__, __LINE__
        );

    /* Success. */
    return gcvSTATUS_OK;
}

static gceSTATUS
_ExecuteDynamicCommandBuffer(
    IN gckVGKERNEL Kernel,
    IN gcsKERNEL_CMDQUEUE_PTR Entry
    )
{
    gceSTATUS status;

    do
    {
        /* Cast the command buffer header. */
        gcsCMDBUFFER_PTR commandBuffer = Entry->commandBuffer;

        /* Set to update the command buffer next time. */
        Entry->handler = _UpdateDynamicCommandBuffer;

        gcmkTRACE_ZONE(
            gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
            "%s(%d): executing next buffer @ 0x%08X, data count = %d\n",
            __FUNCTION__, __LINE__,
            commandBuffer->address,
            commandBuffer->dataCount
            );

        /* Start the command processor. */
        gcmkERR_BREAK(gckVGHARDWARE_Execute(
            Kernel->hardware,
            commandBuffer->address,
            commandBuffer->dataCount
            ));

        /* Success. */
        return gcvSTATUS_EXECUTED;
    }
    while (gcvFALSE);

    /* Return status. */
    return status;
}

static gceSTATUS
_UpdateLastDynamicCommandBuffer(
    IN gckVGKERNEL Kernel,
    IN gcsKERNEL_CMDQUEUE_PTR Entry
    )
{
#if gcvDEBUG || gcdFORCE_MESSAGES
    /* Get the command buffer header. */
    gcsCMDBUFFER_PTR commandBuffer = Entry->commandBuffer;

    /* Validate the command buffer. */
    gcmkASSERT(commandBuffer->completion != gcvNULL);
    gcmkASSERT(commandBuffer->completion != gcvVACANT_BUFFER);

#endif

    gcmkTRACE_ZONE(
        gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
        "%s(%d): processing all tasks scheduled for FE.\n",
        __FUNCTION__, __LINE__
        );

    /* Perform scheduled tasks. */
    return _EventHandler_Block(
        Kernel,
        &Kernel->command->taskTable[gcvBLOCK_COMMAND],
        gcvTRUE
        );
}

static gceSTATUS
_ExecuteLastDynamicCommandBuffer(
    IN gckVGKERNEL Kernel,
    IN gcsKERNEL_CMDQUEUE_PTR Entry
    )
{
    gceSTATUS status;

    do
    {
        /* Cast the command buffer header. */
        gcsCMDBUFFER_PTR commandBuffer = Entry->commandBuffer;

        /* Set to update the command buffer next time. */
        Entry->handler = _UpdateLastDynamicCommandBuffer;

        gcmkTRACE_ZONE(
            gcvLEVEL_VERBOSE, gcvZONE_COMMAND,
            "%s(%d): executing next buffer @ 0x%08X, data count = %d\n",
            __FUNCTION__, __LINE__,
            commandBuffer->address,
            commandBuffer->dataCount
            );

        /* Start the command processor. */
        gcmkERR_BREAK(gckVGHARDWARE_Execute(
            Kernel->hardware,
            commandBuffer->address,
            commandBuffer->dataCount
            ));

        /* Success. */
        return gcvSTATUS_EXECUTED;
    }
    while (gcvFALSE);

    /* Return status. */
    return status;
}


/******************************************************************************\
********************************* Other Handlers *******************************
\******************************************************************************/

static gceSTATUS
_FreeKernelCommandBuffer(
    IN gckVGKERNEL Kernel,
    IN gcsKERNEL_CMDQUEUE_PTR Entry
    )
{
    gceSTATUS status;

    /* Free the command buffer. */
    status = _FreeCommandBuffer(Kernel, Entry->commandBuffer);

    /* Return status. */
    return status;
}


/******************************************************************************\
******************************* Queue Management *******************************
\******************************************************************************/

#if gcvDUMP_COMMAND_BUFFER
static void
_DumpCommandQueue(
    IN gckVGCOMMAND Command,
    IN gcsKERNEL_QUEUE_HEADER_PTR QueueHeader,
    IN gctUINT EntryCount
    )
{
    gcsKERNEL_CMDQUEUE_PTR entry;
    gctUINT queueIndex;

#if defined(gcvCOMMAND_BUFFER_NAME)
    static gctUINT arrayCount = 0;
#endif

    /* Is dumpinng enabled? */
    if (!Commad->enableDumping)
    {
        return;
    }

#if !defined(gcvCOMMAND_BUFFER_NAME)
    gcmkTRACE_ZONE(
        gcvLEVEL_INFO, gcvZONE_COMMAND,
        "COMMAND QUEUE DUMP: %d entries\n", EntryCount
        );
#endif

    /* Get the pointer to the first entry. */
    entry = QueueHeader->currentEntry;

    /* Iterate through the queue. */
    for (queueIndex = 0; queueIndex < EntryCount; queueIndex += 1)
    {
        gcsCMDBUFFER_PTR buffer;
        gctUINT bufferCount;
        gctUINT bufferIndex;
        gctUINT i, count;
        gctUINT size;
        gctUINT32_PTR data;

#if gcvDUMP_COMMAND_LINES
        gctUINT lineNumber;
#endif

#if !defined(gcvCOMMAND_BUFFER_NAME)
        gcmkTRACE_ZONE(
            gcvLEVEL_INFO, gcvZONE_COMMAND,
            "ENTRY %d\n", queueIndex
            );
#endif

        /* Reset the count. */
        bufferCount = 0;

        /* Set the initial buffer. */
        buffer = entry->commandBuffer;

        /* Loop through all subbuffers. */
        while (buffer)
        {
            /* Update the count. */
            bufferCount += 1;

            /* Advance to the next subbuffer. */
            buffer = buffer->nextSubBuffer;
        }

#if !defined(gcvCOMMAND_BUFFER_NAME)
        if (bufferCount > 1)
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_INFO,
                gcvZONE_COMMAND,
                "  COMMAND BUFFER SET: %d buffers.\n",
                bufferCount
                );
        }
#endif

        /* Reset the buffer index. */
        bufferIndex = 0;

        /* Set the initial buffer. */
        buffer = entry->commandBuffer;

        /* Loop through all subbuffers. */
        while (buffer)
        {
            /* Determine the size of the buffer. */
            size = buffer->dataCount * Command->info.commandAlignment;

#if !defined(gcvCOMMAND_BUFFER_NAME)
            /* A single buffer? */
            if (bufferCount == 1)
            {
                gcmkTRACE_ZONE(
                    gcvLEVEL_INFO,
                    gcvZONE_COMMAND,
                    "  COMMAND BUFFER: count=%d (0x%X), size=%d bytes @ %08X.\n",
                    buffer->dataCount,
                    buffer->dataCount,
                    size,
                    buffer->address
                    );
            }
            else
            {
                gcmkTRACE_ZONE(
                    gcvLEVEL_INFO,
                    gcvZONE_COMMAND,
                    "  COMMAND BUFFER %d: count=%d (0x%X), size=%d bytes @ %08X\n",
                    bufferIndex,
                    buffer->dataCount,
                    buffer->dataCount,
                    size,
                    buffer->address
                    );
            }
#endif

            /* Determine the number of double words to print. */
            count = size / 4;

            /* Determine the buffer location. */
            data = (gctUINT32_PTR)
            (
                (gctUINT8_PTR) buffer + buffer->bufferOffset
            );

#if defined(gcvCOMMAND_BUFFER_NAME)
            gcmkTRACE_ZONE(
                gcvLEVEL_INFO,
                gcvZONE_COMMAND,
                "unsigned int _" gcvCOMMAND_BUFFER_NAME "_%d[] =\n",
                arrayCount
                );

            gcmkTRACE_ZONE(
                gcvLEVEL_INFO,
                gcvZONE_COMMAND,
                "{\n"
                );

            arrayCount += 1;
#endif

#if gcvDUMP_COMMAND_LINES
            /* Reset the line number. */
            lineNumber = 0;
#endif

#if defined(gcvCOMMAND_BUFFER_NAME)
            count -= 2;
#endif

            for (i = 0; i < count; i += 1)
            {
                if ((i % 8) == 0)
                {
#if defined(gcvCOMMAND_BUFFER_NAME)
                    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_COMMAND, "\t");
#else
                    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_COMMAND, "    ");
#endif
                }

#if gcvDUMP_COMMAND_LINES
                if (lineNumber == gcvDUMP_COMMAND_LINES)
                {
                    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_COMMAND, " . . . . . . . . .\n");
                    break;
                }
#endif
                gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_COMMAND, "0x%08X", data[i]);

                if (i + 1 == count)
                {
                    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_COMMAND, "\n");

#if gcvDUMP_COMMAND_LINES
                    lineNumber += 1;
#endif
                }
                else
                {
                    if (((i + 1) % 8) == 0)
                    {
                        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_COMMAND, ",\n");

#if gcvDUMP_COMMAND_LINES
                        lineNumber += 1;
#endif
                    }
                    else
                    {
                        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_COMMAND, ", ");
                    }
                }
            }

#if defined(gcvCOMMAND_BUFFER_NAME)
            gcmkTRACE_ZONE(
                gcvLEVEL_INFO,
                gcvZONE_COMMAND,
                "};\n\n"
                );
#endif

            /* Advance to the next subbuffer. */
            buffer = buffer->nextSubBuffer;
            bufferIndex += 1;
        }

        /* Advance to the next entry. */
        entry += 1;
    }
}
#endif

static gceSTATUS
_LockCurrentQueue(
    IN gckVGCOMMAND Command,
    OUT gcsKERNEL_CMDQUEUE_PTR * Entries,
    OUT gctUINT_PTR EntryCount
    )
{
    gceSTATUS status;

    do
    {
        gcsKERNEL_QUEUE_HEADER_PTR queueHead;

        /* Get a shortcut to the head of the queue. */
        queueHead = Command->queueHead;

        /* Is the head buffer still being worked on? */
        if (queueHead->pending)
        {
            /* Increment overflow count. */
            Command->queueOverflow += 1;

            /* Wait until the head becomes idle. */
            gcmkERR_BREAK(_WaitForIdle(Command, queueHead));
        }

        /* Acquire the mutex. */
        gcmkERR_BREAK(gckOS_AcquireMutex(
            Command->os,
            Command->queueMutex,
            gcvINFINITE
            ));

        /* Determine the first queue entry. */
        queueHead->currentEntry = (gcsKERNEL_CMDQUEUE_PTR)
        (
            (gctUINT8_PTR) queueHead + gcmSIZEOF(gcsKERNEL_QUEUE_HEADER)
        );

        /* Set the pointer to the first entry. */
        * Entries = queueHead->currentEntry;

        /* Determine the number of available entries. */
        * EntryCount = queueHead->size / gcmSIZEOF(gcsKERNEL_CMDQUEUE);

        /* Success. */
        return gcvSTATUS_OK;
    }
    while (gcvFALSE);

    /* Return status. */
    return status;
}

static gceSTATUS
_UnlockCurrentQueue(
    IN gckVGCOMMAND Command,
    IN gctUINT EntryCount
    )
{
    gceSTATUS status;

    do
    {
#if !gcdENABLE_INFINITE_SPEED_HW
        gcsKERNEL_QUEUE_HEADER_PTR queueTail;
        gcsKERNEL_QUEUE_HEADER_PTR queueHead;
        gcsKERNEL_QUEUE_HEADER_PTR queueNext;
        gctUINT queueSize;
        gctUINT newSize;
        gctUINT unusedSize;

        /* Get shortcut to the head and to the tail of the queue. */
        queueTail = Command->queueTail;
        queueHead = Command->queueHead;

        /* Dump the command buffer. */
#if gcvDUMP_COMMAND_BUFFER
        _DumpCommandQueue(Command, queueHead, EntryCount);
#endif

        /* Get a shortcut to the current queue size. */
        queueSize = queueHead->size;

        /* Determine the new queue size. */
        newSize = EntryCount * gcmSIZEOF(gcsKERNEL_CMDQUEUE);
        gcmkASSERT(newSize <= queueSize);

        /* Determine the size of the unused area. */
        unusedSize = queueSize - newSize;

        /* Is the unused area big enough to become a buffer? */
        if (unusedSize >= gcvMINUMUM_BUFFER)
        {
            gcsKERNEL_QUEUE_HEADER_PTR nextHead;

            /* Place the new header. */
            nextHead = (gcsKERNEL_QUEUE_HEADER_PTR)
            (
                (gctUINT8_PTR) queueHead
                    + gcmSIZEOF(gcsKERNEL_QUEUE_HEADER)
                    + newSize
            );

            /* Initialize the buffer. */
            nextHead->size    = unusedSize - gcmSIZEOF(gcsKERNEL_QUEUE_HEADER);
            nextHead->pending = 0;

            /* Link the buffer in. */
            nextHead->next  = queueHead->next;
            queueHead->next = nextHead;
            queueNext       = nextHead;

            /* Update the size of the current buffer. */
            queueHead->size = newSize;
        }

        /* Not big enough. */
        else
        {
            /* Determine the next queue. */
            queueNext = queueHead->next;
        }

        /* Mark the buffer as busy. */
        queueHead->pending = EntryCount;

        /* Advance to the next buffer. */
        Command->queueHead = queueNext;

        /* Start the command processor if the queue was empty. */
        if (queueTail == queueHead)
        {
            gcsCMDBUFFER_PTR commandBuffer;

            /* The first entry must be a command buffer. */
            commandBuffer = queueTail->currentEntry->commandBuffer;

            /* Start the command processor. */
            gcmkERR_BREAK(gckVGHARDWARE_Execute(
                Command->hardware,
                commandBuffer->address,
                commandBuffer->dataCount
                ));
        }

        /* The queue was not empty. */
        else
        {
            /* Advance the merge buffer if needed. */
            if (queueHead == Command->mergeQueue)
            {
                Command->mergeQueue = queueNext;
            }
        }
#endif

        /* Release the mutex. */
        gcmkERR_BREAK(gckOS_ReleaseMutex(
            Command->os,
            Command->queueMutex
            ));

        /* Success. */
        return gcvSTATUS_OK;
    }
    while (gcvFALSE);

    /* Return status. */
    return status;
}



/******************************************************************************\
****************************** gckVGCOMMAND API Code *****************************
\******************************************************************************/
gceSTATUS
gckVGCOMMAND_Construct(
    IN gckVGKERNEL Kernel,
    IN gctUINT TaskGranularity,
    IN gctUINT QueueSize,
    OUT gckVGCOMMAND * Command
    )
{
    gceSTATUS status, last;
    gckVGCOMMAND command = gcvNULL;
    gcsKERNEL_QUEUE_HEADER_PTR queue;
    gctUINT i, j;

    gcmkHEADER_ARG("Kernel=0x%x TaskGranularity=0x%x QueueSize=0x%x Command=0x%x",
        Kernel, TaskGranularity, QueueSize, Command);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(QueueSize >= gcvMINUMUM_BUFFER);
    gcmkVERIFY_ARGUMENT(Command != gcvNULL);

    do
    {
        /***********************************************************************
        ** Generic object initialization.
        */

        /* Allocate the gckVGCOMMAND structure. */
        gcmkERR_BREAK(gckOS_Allocate(
            Kernel->os,
            gcmSIZEOF(struct _gckVGCOMMAND),
            (gctPOINTER *) &command
            ));

        /* Initialize the object. */
        command->object.type = gcvOBJ_COMMAND;

        /* Set the object pointers. */
        command->kernel      = Kernel;
        command->os          = Kernel->os;
        command->hardware    = Kernel->hardware;

        /* Reset pointers. */
        command->queue       = gcvNULL;
        command->queueMutex  = gcvNULL;
        command->taskMutex   = gcvNULL;
        command->commitMutex = gcvNULL;

        command->powerStallBuffer   = gcvNULL;
        command->powerStallSignal   = gcvNULL;
        command->powerSemaphore     = gcvNULL;

        /* Reset context states. */
        command->contextCounter = 0;
        command->currentContext = 0;

        /* Enable command buffer dumping. */
        command->enableDumping = gcvTRUE;

        /* Set features. */
        command->fe20 = Kernel->hardware->fe20;
        command->vg20 = Kernel->hardware->vg20;
        command->vg21 = Kernel->hardware->vg21;

        /* Reset task table .*/
        gcmkVERIFY_OK(gckOS_ZeroMemory(
            command->taskTable, gcmSIZEOF(command->taskTable)
            ));

        /* Query command buffer attributes. */
        gcmkERR_BREAK(gckVGCOMMAND_InitializeInfo(command));

        /* Create the control mutexes. */
        gcmkERR_BREAK(gckOS_CreateMutex(Kernel->os, &command->queueMutex));
        gcmkERR_BREAK(gckOS_CreateMutex(Kernel->os, &command->taskMutex));
        gcmkERR_BREAK(gckOS_CreateMutex(Kernel->os, &command->commitMutex));

        /* Create the power management semaphore. */
        gcmkERR_BREAK(gckOS_CreateSemaphore(Kernel->os,
            &command->powerSemaphore));

        gcmkERR_BREAK(gckOS_CreateSignal(Kernel->os,
            gcvFALSE, &command->powerStallSignal));

        /***********************************************************************
        ** Command queue initialization.
        */

        /* Allocate the command queue. */
        gcmkERR_BREAK(gckOS_Allocate(
            Kernel->os,
            QueueSize,
            (gctPOINTER *) &command->queue
            ));

        /* Initialize the command queue. */
        queue = command->queue;

        queue->size    = QueueSize - gcmSIZEOF(gcsKERNEL_QUEUE_HEADER);
        queue->pending = 0;
        queue->next    = queue;

        command->queueHead  =
        command->queueTail  =
        command->mergeQueue = command->queue;

        command->queueOverflow = 0;


        /***********************************************************************
        ** Enable TS overflow interrupt.
        */

        command->info.tsOverflowInt = 0;
        gcmkERR_BREAK(gckVGINTERRUPT_Enable(
            Kernel->interrupt,
            &command->info.tsOverflowInt,
            _EventHandler_TSOverflow
            ));

        /* Mask out the interrupt. */
        Kernel->hardware->eventMask &= ~(1 << command->info.tsOverflowInt);


        /***********************************************************************
        ** Enable Bus Error interrupt.
        */

        /* Hardwired to bit 31. */
        command->busErrorInt = 31;

        /* Enable the interrupt. */
        gcmkERR_BREAK(gckVGINTERRUPT_Enable(
            Kernel->interrupt,
            &command->busErrorInt,
            _EventHandler_BusError
            ));


        command->powerStallInt = 30;
        /* Enable the interrupt. */
        gcmkERR_BREAK(gckVGINTERRUPT_Enable(
            Kernel->interrupt,
            &command->powerStallInt,
            _EventHandler_PowerStall
            ));

        /***********************************************************************
        ** Task management initialization.
        */

        command->taskStorage            = gcvNULL;
        command->taskStorageGranularity = TaskGranularity;
        command->taskStorageUsable      = TaskGranularity - gcmSIZEOF(gcsTASK_STORAGE);

        command->taskFreeHead = gcvNULL;
        command->taskFreeTail = gcvNULL;

        /* Enable block handlers. */
        for (i = 0; i < gcmCOUNTOF(_blockHandlers); i += 1)
        {
            /* Get the target hardware block. */
            gceBLOCK block = _blockHandlers[i].block;

            /* Get the interrupt array entry. */
            gcsBLOCK_TASK_ENTRY_PTR entry = &command->taskTable[block];

            /* Determine the interrupt value index. */
            gctUINT index = entry->interruptCount;

            /* Create the block semaphore. */
            if (entry->interruptSemaphore == gcvNULL)
            {
                gcmkERR_BREAK(gckOS_CreateSemaphoreVG(
                    command->os, &entry->interruptSemaphore
                    ));
            }

            /* Enable auto-detection. */
            entry->interruptArray[index] = -1;

            /* Enable interrupt for the block. */
            gcmkERR_BREAK(gckVGINTERRUPT_Enable(
                Kernel->interrupt,
                &entry->interruptArray[index],
                _blockHandlers[i].handler
                ));

            /* Update the number of registered interrupts. */
            entry->interruptCount += 1;

            /* Inrement the semaphore to allow the usage of the registered
               interrupt. */
            gcmkERR_BREAK(gckOS_IncrementSemaphore(
                command->os, entry->interruptSemaphore
                ));

        }

        /* Error? */
        if (gcmkIS_ERROR(status))
        {
            break;
        }

        /* Get the FE interrupt. */
        command->info.feBufferInt
            = command->taskTable[gcvBLOCK_COMMAND].interruptArray[0];

        /* Return gckVGCOMMAND object pointer. */
        *Command = command;

        gcmkFOOTER_ARG("*Command=0x%x",*Command);
        /* Success. */
        return gcvSTATUS_OK;
    }
    while (gcvFALSE);

    /* Roll back. */
    if (command != gcvNULL)
    {
        /* Disable block handlers. */
        for (i = 0; i < gcvBLOCK_COUNT; i += 1)
        {
            /* Get the task table entry. */
            gcsBLOCK_TASK_ENTRY_PTR entry = &command->taskTable[i];

            /* Destroy the semaphore. */
            if (entry->interruptSemaphore != gcvNULL)
            {
                gcmkCHECK_STATUS(gckOS_DestroySemaphore(
                    command->os, entry->interruptSemaphore
                    ));
            }

            /* Disable all enabled interrupts. */
            for (j = 0; j < entry->interruptCount; j += 1)
            {
                /* Must be a valid value. */
                gcmkASSERT(entry->interruptArray[j] >= 0);
                gcmkASSERT(entry->interruptArray[j] <= 31);

                /* Disable the interrupt. */
                gcmkCHECK_STATUS(gckVGINTERRUPT_Disable(
                    Kernel->interrupt,
                    entry->interruptArray[j]
                    ));
            }
        }

        /* Disable the bus error interrupt. */
        gcmkCHECK_STATUS(gckVGINTERRUPT_Disable(
            Kernel->interrupt,
            command->busErrorInt
            ));

        /* Disable TS overflow interrupt. */
        if (command->info.tsOverflowInt != -1)
        {
            gcmkCHECK_STATUS(gckVGINTERRUPT_Disable(
                Kernel->interrupt,
                command->info.tsOverflowInt
                ));
        }

        /* Delete the commit mutex. */
        if (command->commitMutex != gcvNULL)
        {
            gcmkCHECK_STATUS(gckOS_DeleteMutex(
                Kernel->os, command->commitMutex
                ));
        }

        /* Delete the command queue mutex. */
        if (command->taskMutex != gcvNULL)
        {
            gcmkCHECK_STATUS(gckOS_DeleteMutex(
                Kernel->os, command->taskMutex
                ));
        }

        /* Delete the command queue mutex. */
        if (command->queueMutex != gcvNULL)
        {
            gcmkCHECK_STATUS(gckOS_DeleteMutex(
                Kernel->os, command->queueMutex
                ));
        }

        /* Delete the command queue. */
        if (command->queue != gcvNULL)
        {
            gcmkCHECK_STATUS(gckOS_Free(
                Kernel->os, command->queue
                ));
        }

        if (command->powerSemaphore != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_DestroySemaphore(
                Kernel->os, command->powerSemaphore));
        }

        if (command->powerStallSignal != gcvNULL)
        {
            /* Create the power management semaphore. */
            gcmkVERIFY_OK(gckOS_DestroySignal(
                Kernel->os,
                command->powerStallSignal));
        }

        /* Free the gckVGCOMMAND structure. */
        gcmkCHECK_STATUS(gckOS_Free(
            Kernel->os, command
            ));
    }

    gcmkFOOTER();
    /* Return the error. */
    return status;
}

gceSTATUS
gckVGCOMMAND_Destroy(
    OUT gckVGCOMMAND Command
    )
{
    gceSTATUS status = gcvSTATUS_OK;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    do
    {
        gctUINT i;
        gcsTASK_STORAGE_PTR nextStorage;

        if (Command->queueHead != gcvNULL)
        {
            /* Wait until the head becomes idle. */
            gcmkERR_BREAK(_WaitForIdle(Command, Command->queueHead));
        }

        /* Disable block handlers. */
        for (i = 0; i < gcvBLOCK_COUNT; i += 1)
        {
            /* Get the interrupt array entry. */
            gcsBLOCK_TASK_ENTRY_PTR entry = &Command->taskTable[i];

            /* Determine the index of the last interrupt in the array. */
            gctINT index = entry->interruptCount - 1;

            /* Destroy the semaphore. */
            if (entry->interruptSemaphore != gcvNULL)
            {
                gcmkERR_BREAK(gckOS_DestroySemaphore(
                    Command->os, entry->interruptSemaphore
                    ));
            }

            /* Disable all enabled interrupts. */
            while (index >= 0)
            {
                /* Must be a valid value. */
                gcmkASSERT(entry->interruptArray[index] >= 0);
                gcmkASSERT(entry->interruptArray[index] <= 31);

                /* Disable the interrupt. */
                gcmkERR_BREAK(gckVGINTERRUPT_Disable(
                    Command->kernel->interrupt,
                    entry->interruptArray[index]
                    ));

                /* Update to the next interrupt. */
                index                 -= 1;
                entry->interruptCount -= 1;
            }

            /* Error? */
            if (gcmkIS_ERROR(status))
            {
                break;
            }
        }

        /* Error? */
        if (gcmkIS_ERROR(status))
        {
            break;
        }

        /* Disable the bus error interrupt. */
        gcmkERR_BREAK(gckVGINTERRUPT_Disable(
            Command->kernel->interrupt,
            Command->busErrorInt
            ));

        /* Disable TS overflow interrupt. */
        if (Command->info.tsOverflowInt != -1)
        {
            gcmkERR_BREAK(gckVGINTERRUPT_Disable(
                Command->kernel->interrupt,
                Command->info.tsOverflowInt
                ));

            Command->info.tsOverflowInt = -1;
        }

        /* Delete the commit mutex. */
        if (Command->commitMutex != gcvNULL)
        {
            gcmkERR_BREAK(gckOS_DeleteMutex(
                Command->os, Command->commitMutex
                ));

            Command->commitMutex = gcvNULL;
        }

        /* Delete the command queue mutex. */
        if (Command->taskMutex != gcvNULL)
        {
            gcmkERR_BREAK(gckOS_DeleteMutex(
                Command->os, Command->taskMutex
                ));

            Command->taskMutex = gcvNULL;
        }

        /* Delete the command queue mutex. */
        if (Command->queueMutex != gcvNULL)
        {
            gcmkERR_BREAK(gckOS_DeleteMutex(
                Command->os, Command->queueMutex
                ));

            Command->queueMutex = gcvNULL;
        }

        if (Command->powerSemaphore != gcvNULL)
        {
            /* Destroy the power management semaphore. */
            gcmkERR_BREAK(gckOS_DestroySemaphore(
                Command->os, Command->powerSemaphore));
        }

        if (Command->powerStallSignal != gcvNULL)
        {
            /* Create the power management semaphore. */
            gcmkERR_BREAK(gckOS_DestroySignal(
                Command->os,
                Command->powerStallSignal));
        }

        if (Command->queue != gcvNULL)
        {
            /* Delete the command queue. */
            gcmkERR_BREAK(gckOS_Free(
                Command->os, Command->queue
                ));
        }

        /* Destroy all allocated buffers. */
        while (Command->taskStorage)
        {
            /* Copy the buffer pointer. */
            nextStorage = Command->taskStorage->next;

            /* Free the current container. */
            gcmkERR_BREAK(gckOS_Free(
                Command->os, Command->taskStorage
                ));

            /* Advance to the next one. */
            Command->taskStorage = nextStorage;
        }

        /* Error? */
        if (gcmkIS_ERROR(status))
        {
            break;
        }

        /* Mark the object as unknown. */
        Command->object.type = gcvOBJ_UNKNOWN;

        /* Free the gckVGCOMMAND structure. */
        gcmkERR_BREAK(gckOS_Free(Command->os, Command));

        gcmkFOOTER_NO();
        /* Success. */
        return gcvSTATUS_OK;
    }
    while (gcvFALSE);

    /* Restore the object type if failed. */
    Command->object.type = gcvOBJ_COMMAND;

    gcmkFOOTER();
    /* Return the error. */
    return status;
}

gceSTATUS
gckVGCOMMAND_QueryCommandBuffer(
    IN gckVGCOMMAND Command,
    OUT gcsCOMMAND_BUFFER_INFO_PTR Information
    )
{
    gcmkHEADER_ARG("Command=0x%x Information=0x%x", Command, Information);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);
    gcmkVERIFY_ARGUMENT(Information != gcvNULL);

    /* Copy the information. */
    gcmkVERIFY_OK(gckOS_MemCopy(
        Information, &Command->info, sizeof(gcsCOMMAND_BUFFER_INFO)
        ));

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

gceSTATUS
gckVGCOMMAND_Allocate(
    IN gckVGCOMMAND Command,
    IN gctSIZE_T Size,
    OUT gcsCMDBUFFER_PTR * CommandBuffer,
    OUT gctPOINTER * Data
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Command=0x%x Size=0x%x CommandBuffer=0x%x Data=0x%x",
        Command, Size, CommandBuffer, Data);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);
    gcmkVERIFY_ARGUMENT(Data != gcvNULL);

    do
    {
        /* Allocate the buffer. */
        gcmkERR_BREAK(_AllocateCommandBuffer(Command, Size, CommandBuffer));

        /* Determine the data pointer. */
        * Data = (gctUINT8_PTR) (*CommandBuffer) + (* CommandBuffer)->bufferOffset;
    }
    while (gcvFALSE);

    gcmkFOOTER();
    /* Return status. */
    return status;
}

gceSTATUS
gckVGCOMMAND_Free(
    IN gckVGCOMMAND Command,
    IN gcsCMDBUFFER_PTR CommandBuffer
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Command=0x%x CommandBuffer=0x%x",
        Command, CommandBuffer);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);
    gcmkVERIFY_ARGUMENT(CommandBuffer != gcvNULL);

    /* Free command buffer. */
    status = _FreeCommandBuffer(Command->kernel, CommandBuffer);

    gcmkFOOTER();
    /* Return status. */
    return status;
}

gceSTATUS
gckVGCOMMAND_Execute(
    IN gckVGCOMMAND Command,
    IN gcsCMDBUFFER_PTR CommandBuffer
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Command=0x%x CommandBuffer=0x%x",
        Command, CommandBuffer);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);
    gcmkVERIFY_ARGUMENT(CommandBuffer != gcvNULL);

    do
    {
        gctUINT queueLength;
        gcsKERNEL_CMDQUEUE_PTR kernelEntry;

        /* Lock the current queue. */
        gcmkERR_BREAK(_LockCurrentQueue(
            Command, &kernelEntry, &queueLength
            ));

        /* Set the buffer. */
        kernelEntry->commandBuffer = CommandBuffer;
        kernelEntry->handler = _FreeKernelCommandBuffer;

        /* Lock the current queue. */
        gcmkERR_BREAK(_UnlockCurrentQueue(
            Command, 1
            ));
    }
    while (gcvFALSE);

    gcmkFOOTER();
    /* Return status. */
    return status;
}

gceSTATUS
gckVGCOMMAND_Commit(
    IN gckVGCOMMAND Command,
    IN gcsVGCONTEXT_PTR Context,
    IN gcsVGCMDQUEUE_PTR Queue,
    IN gctUINT EntryCount,
    IN gcsTASK_MASTER_TABLE_PTR TaskTable
    )
{
    /*
        The first buffer is executed through a direct gckVGHARDWARE_Execute call,
        therefore only an update is needed after the execution is over. All
        consequent buffers need to be executed upon the first update call from
        the FE interrupt handler.
    */

    static gcsQUEUE_UPDATE_CONTROL _dynamicBuffer[] =
    {
        {
            _UpdateDynamicCommandBuffer,
            _UpdateDynamicCommandBuffer,
            _UpdateLastDynamicCommandBuffer,
            _UpdateLastDynamicCommandBuffer
        },
        {
            _ExecuteDynamicCommandBuffer,
            _UpdateDynamicCommandBuffer,
            _ExecuteLastDynamicCommandBuffer,
            _UpdateLastDynamicCommandBuffer
        }
    };

    static gcsQUEUE_UPDATE_CONTROL _staticBuffer[] =
    {
        {
            _UpdateStaticCommandBuffer,
            _UpdateStaticCommandBuffer,
            _UpdateLastStaticCommandBuffer,
            _UpdateLastStaticCommandBuffer
        },
        {
            _ExecuteStaticCommandBuffer,
            _UpdateStaticCommandBuffer,
            _ExecuteLastStaticCommandBuffer,
            _UpdateLastStaticCommandBuffer
        }
    };

    gceSTATUS status, last;

#ifdef __QNXNTO__
    gcsVGCONTEXT_PTR userContext = gcvNULL;
    gctBOOL userContextMapped = gcvFALSE;
    gcsTASK_MASTER_TABLE_PTR userTaskTable = gcvNULL;
    gctBOOL userTaskTableMapped = gcvFALSE;
    gctPOINTER pointer = gcvNULL;
#endif

    gcmkHEADER_ARG("Command=0x%x Context=0x%x Queue=0x%x EntryCount=0x%x TaskTable=0x%x",
        Command, Context, Queue, EntryCount, TaskTable);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);
    gcmkVERIFY_ARGUMENT(Context != gcvNULL);
    gcmkVERIFY_ARGUMENT(Queue != gcvNULL);
    gcmkVERIFY_ARGUMENT(EntryCount > 1);

    do
    {
        gctBOOL haveFETasks;
        gctUINT queueSize;
        gcsVGCMDQUEUE_PTR mappedQueue;
        gcsVGCMDQUEUE_PTR userEntry;
        gcsKERNEL_CMDQUEUE_PTR kernelEntry;
        gcsQUEUE_UPDATE_CONTROL_PTR queueControl;
        gctUINT currentLength;
        gctUINT queueLength;
        gctUINT entriesQueued;
        gctUINT8_PTR previousEnd;
        gctBOOL previousDynamic;
        gctBOOL previousExecuted;
        gctUINT controlIndex;

#ifdef __QNXNTO__
        /* Map the context into the kernel space. */
        userContext = Context;

        gcmkERR_BREAK(gckOS_MapUserPointer(
            Command->os,
            userContext,
            gcmSIZEOF(*userContext),
            &pointer));

        Context = pointer;

        userContextMapped = gcvTRUE;

        /* Map the taskTable into the kernel space. */
        userTaskTable = TaskTable;

        gcmkERR_BREAK(gckOS_MapUserPointer(
            Command->os,
            userTaskTable,
            gcmSIZEOF(*userTaskTable),
            &pointer));

        TaskTable = pointer;

        userTaskTableMapped = gcvTRUE;

        /* Update the signal info. */
        TaskTable->coid  = Context->coid;
        TaskTable->rcvid = Context->rcvid;
#endif

        gcmkERR_BREAK(gckVGHARDWARE_SetPowerManagementState(
            Command->hardware, gcvPOWER_ON_AUTO
            ));

        /* Acquire the power semaphore. */
        gcmkERR_BREAK(gckOS_AcquireSemaphore(
            Command->os, Command->powerSemaphore
            ));

        /* Acquire the mutex. */
        status = gckOS_AcquireMutex(
            Command->os,
            Command->commitMutex,
            gcvINFINITE
            );

        if (gcmIS_ERROR(status))
        {
            gcmkVERIFY_OK(gckOS_ReleaseSemaphore(
                Command->os, Command->powerSemaphore));
            break;
        }

        do
        {
            gcmkERR_BREAK(_FlushMMU(Command));

            /* Assign a context ID if not yet assigned. */
            if (Context->id == 0)
            {
                /* Assign the next context number. */
                Context->id = ++ Command->contextCounter;

                /* See if we overflowed. */
                if (Command->contextCounter == 0)
                {
                    /* We actually did overflow, wow... */
                    status = gcvSTATUS_OUT_OF_RESOURCES;
                    break;
                }
            }

            /* The first entry in the queue is always the context buffer.
               Verify whether the user context is the same as the current
               context and if that's the case, skip the first entry. */
            if (Context->id == Command->currentContext)
            {
                /* Same context as before, skip the first entry. */
                EntryCount -= 1;
                Queue      += 1;

                /* Set the signal to avoid user waiting. */
#ifdef __QNXNTO__
                gcmkERR_BREAK(gckOS_UserSignal(
                    Command->os,
                    Context->userSignal,
                    Context->rcvid,
                    Context->coid
                    ));
#else
                gcmkERR_BREAK(gckOS_UserSignal(
                    Command->os, Context->signal, Context->process
                    ));
#endif
            }
            else
            {
                /* Different user context - keep the first entry.
                   Set the user context as the current one. */
                Command->currentContext = Context->id;
            }

            /* Reset pointers. */
            queueControl = gcvNULL;
            previousEnd  = gcvNULL;

            /* Determine whether there are FE tasks to be performed. */
            haveFETasks = (TaskTable->table[gcvBLOCK_COMMAND].head != gcvNULL);

            /* Determine the size of the queue. */
            queueSize = EntryCount * gcmSIZEOF(gcsVGCMDQUEUE);

            /* Map the command queue into the kernel space. */
            gcmkERR_BREAK(gckOS_MapUserPointer(
                Command->os,
                Queue,
                queueSize,
                (gctPOINTER *) &mappedQueue
                ));

            /* Set the first entry. */
            userEntry = mappedQueue;

            /* Process the command queue. */
            while (EntryCount)
            {
                /* Lock the current queue. */
                gcmkERR_BREAK(_LockCurrentQueue(
                    Command, &kernelEntry, &queueLength
                    ));

                /* Determine the number of entries to process. */
                currentLength = (queueLength < EntryCount)
                    ? queueLength
                    : EntryCount;

                /* Update the number of the entries left to process. */
                EntryCount -= currentLength;

                /* Reset previous flags. */
                previousDynamic  = gcvFALSE;
                previousExecuted = gcvFALSE;

                /* Set the initial control index. */
                controlIndex = 0;

                /* Process entries. */
                for (entriesQueued = 0; entriesQueued < currentLength; entriesQueued += 1)
                {
                    /* Get the kernel pointer to the command buffer header. */
                    gcsCMDBUFFER_PTR commandBuffer = gcvNULL;
                    gcmkERR_BREAK(_ConvertUserCommandBufferPointer(
                        Command,
                        userEntry->commandBuffer,
                        &commandBuffer
                        ));

                    /* Is it a dynamic command buffer? */
                    if (userEntry->dynamic)
                    {
                        /* Select dynamic buffer control functions. */
                        queueControl = &_dynamicBuffer[controlIndex];
                    }

                    /* No, a static command buffer. */
                    else
                    {
                        /* Select static buffer control functions. */
                        queueControl = &_staticBuffer[controlIndex];
                    }

                    /* Set the command buffer pointer to the entry. */
                    kernelEntry->commandBuffer = commandBuffer;

                    /* If the previous entry was a dynamic command buffer,
                       link it to the current. */
                    if (previousDynamic)
                    {
                        gcmkERR_BREAK(gckVGCOMMAND_FetchCommand(
                            Command,
                            previousEnd,
                            commandBuffer->address,
                            commandBuffer->dataCount,
                            gcvNULL
                            ));

                        /* The buffer will be auto-executed, only need to
                           update it after it has been executed. */
                        kernelEntry->handler = queueControl->update;

                        /* The buffer is only being updated. */
                        previousExecuted = gcvFALSE;
                    }
                    else
                    {
                        /* Set the buffer up for execution. */
                        kernelEntry->handler = queueControl->execute;

                        /* The buffer is being updated. */
                        previousExecuted = gcvTRUE;
                    }

                    /* The current buffer's END command becomes the last END. */
                    previousEnd
                        = ((gctUINT8_PTR) commandBuffer)
                        + commandBuffer->bufferOffset
                        + commandBuffer->dataCount * Command->info.commandAlignment
                        - Command->info.staticTailSize;

                    /* Update the last entry info. */
                    previousDynamic = userEntry->dynamic;

                    /* Advance entries. */
                    userEntry   += 1;
                    kernelEntry += 1;

                    /* Update the control index. */
                    controlIndex = 1;
                }

                /* If the previous entry was a dynamic command buffer,
                   terminate it with an END. */
                if (previousDynamic)
                {
                    gcmkERR_BREAK(gckVGCOMMAND_EndCommand(
                        Command,
                        previousEnd,
                        Command->info.feBufferInt,
                        gcvNULL
                        ));
                }

                /* Last buffer? */
                if (EntryCount == 0)
                {
                    /* Modify the last command buffer's routines to handle
                       tasks if any.*/
                    if (haveFETasks)
                    {
                        if (previousExecuted)
                        {
                            kernelEntry[-1].handler = queueControl->lastExecute;
                        }
                        else
                        {
                            kernelEntry[-1].handler = queueControl->lastUpdate;
                        }
                    }

                    /* Release the mutex. */
                    gcmkERR_BREAK(gckOS_ReleaseMutex(
                        Command->os,
                        Command->queueMutex
                        ));
                    /* Schedule tasks. */
                    gcmkERR_BREAK(_ScheduleTasks(Command, TaskTable, previousEnd));

                    /* Acquire the mutex. */
                    gcmkERR_BREAK(gckOS_AcquireMutex(
                        Command->os,
                        Command->queueMutex,
                        gcvINFINITE
                        ));
                }

                /* Unkock and schedule the current queue for execution. */
                gcmkERR_BREAK(_UnlockCurrentQueue(
                    Command, currentLength
                    ));
            }


            /* Unmap the user command buffer. */
            gcmkERR_BREAK(gckOS_UnmapUserPointer(
                Command->os,
                Queue,
                queueSize,
                mappedQueue
                ));
        }
        while (gcvFALSE);

        /* Release the mutex. */
        gcmkCHECK_STATUS(gckOS_ReleaseMutex(
            Command->os,
            Command->commitMutex
            ));

        gcmkVERIFY_OK(gckOS_ReleaseSemaphore(
            Command->os, Command->powerSemaphore));
    }
    while (gcvFALSE);

#ifdef __QNXNTO__
    if (userContextMapped)
    {
        /* Unmap the user context. */
        gcmkVERIFY_OK(gckOS_UnmapUserPointer(
            Command->os,
            userContext,
            gcmSIZEOF(*userContext),
            Context));
    }

    if (userTaskTableMapped)
    {
        /* Unmap the user taskTable. */
        gcmkVERIFY_OK(gckOS_UnmapUserPointer(
            Command->os,
            userTaskTable,
            gcmSIZEOF(*userTaskTable),
            TaskTable));
    }
#endif

    gcmkFOOTER();
    /* Return status. */
    return status;
}

#endif /* gcdENABLE_VG */
