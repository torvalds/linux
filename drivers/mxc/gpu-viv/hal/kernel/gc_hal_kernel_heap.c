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


/**
**  @file
**  gckHEAP object for kernel HAL layer.  The heap implemented here is an arena-
**  based memory allocation.  An arena-based memory heap allocates data quickly
**  from specified arenas and reduces memory fragmentation.
**
*/
#include "gc_hal_kernel_precomp.h"

#define _GC_OBJ_ZONE            gcvZONE_HEAP

/*******************************************************************************
***** Structures ***************************************************************
*******************************************************************************/
#define gcdIN_USE               ((gcskNODE_PTR)gcvMAXUINTPTR_T)

typedef struct _gcskNODE *      gcskNODE_PTR;
typedef struct _gcskNODE
{
    /* Number of byets in node. */
    gctSIZE_T                   bytes;

    /* Pointer to next free node, or gcvNULL to mark the node as freed, or
    ** gcdIN_USE to mark the node as used. */
    gcskNODE_PTR                next;

#if gcmIS_DEBUG(gcdDEBUG_CODE)
    /* Time stamp of allocation. */
    gctUINT64                   timeStamp;
#endif
}
gcskNODE;

typedef struct _gcskHEAP    *   gcskHEAP_PTR;
typedef struct _gcskHEAP
{
    /* Linked list. */
    gcskHEAP_PTR                next;
    gcskHEAP_PTR                prev;

    /* Heap size. */
    gctSIZE_T                   size;

    /* Free list. */
    gcskNODE_PTR                freeList;
}
gcskHEAP;

struct _gckHEAP
{
    /* Object. */
    gcsOBJECT                   object;

    /* Pointer to a gckOS object. */
    gckOS                       os;

    /* Locking mutex. */
    gctPOINTER                  mutex;

    /* Allocation parameters. */
    gctSIZE_T                   allocationSize;

    /* Heap list. */
    gcskHEAP_PTR                heap;
#if gcmIS_DEBUG(gcdDEBUG_CODE)
    gctUINT64                   timeStamp;
#endif

#if VIVANTE_PROFILER || gcmIS_DEBUG(gcdDEBUG_CODE)
    /* Profile information. */
    gctUINT32                   allocCount;
    gctUINT64                   allocBytes;
    gctUINT64                   allocBytesMax;
    gctUINT64                   allocBytesTotal;
    gctUINT32                   heapCount;
    gctUINT32                   heapCountMax;
    gctUINT64                   heapMemory;
    gctUINT64                   heapMemoryMax;
#endif
};

/*******************************************************************************
***** Static Support Functions *************************************************
*******************************************************************************/

#if gcmIS_DEBUG(gcdDEBUG_CODE)
static gctSIZE_T
_DumpHeap(
    IN gcskHEAP_PTR Heap
    )
{
    gctPOINTER p;
    gctSIZE_T leaked = 0;

    /* Start at first node. */
    for (p = Heap + 1;;)
    {
        /* Convert the pointer. */
        gcskNODE_PTR node = (gcskNODE_PTR) p;

        /* Check if this is a used node. */
        if (node->next == gcdIN_USE)
        {
            /* Print the leaking node. */
            gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_HEAP,
                           "Detected leaking: node=0x%x bytes=%lu timeStamp=%llu "
                           "(%08X %c%c%c%c)",
                           node, node->bytes, node->timeStamp,
                           ((gctUINT32_PTR) (node + 1))[0],
                           gcmPRINTABLE(((gctUINT8_PTR) (node + 1))[0]),
                           gcmPRINTABLE(((gctUINT8_PTR) (node + 1))[1]),
                           gcmPRINTABLE(((gctUINT8_PTR) (node + 1))[2]),
                           gcmPRINTABLE(((gctUINT8_PTR) (node + 1))[3]));

            /* Add leaking byte count. */
            leaked += node->bytes;
        }

        /* Test for end of heap. */
        if (node->bytes == 0)
        {
            break;
        }

        else
        {
            /* Move to next node. */
            p = (gctUINT8_PTR) node + node->bytes;
        }
    }

    /* Return the number of leaked bytes. */
    return leaked;
}
#endif

static gceSTATUS
_CompactKernelHeap(
    IN gckHEAP Heap
    )
{
    gcskHEAP_PTR heap, next;
    gctPOINTER p;
    gcskHEAP_PTR freeList = gcvNULL;

    gcmkHEADER_ARG("Heap=0x%x", Heap);

    /* Walk all the heaps. */
    for (heap = Heap->heap; heap != gcvNULL; heap = next)
    {
        gcskNODE_PTR lastFree = gcvNULL;

        /* Zero out the free list. */
        heap->freeList = gcvNULL;

        /* Start at the first node. */
        for (p = (gctUINT8_PTR) (heap + 1);;)
        {
            /* Convert the pointer. */
            gcskNODE_PTR node = (gcskNODE_PTR) p;

            gcmkASSERT(p <= (gctPOINTER) ((gctUINT8_PTR) (heap + 1) + heap->size));

            /* Test if this node not used. */
            if (node->next != gcdIN_USE)
            {
                /* Test if this is the end of the heap. */
                if (node->bytes == 0)
                {
                    break;
                }

                /* Test of this is the first free node. */
                else if (lastFree == gcvNULL)
                {
                    /* Initialzie the free list. */
                    heap->freeList = node;
                    lastFree       = node;
                }

                else
                {
                    /* Test if this free node is contiguous with the previous
                    ** free node. */
                    if ((gctUINT8_PTR) lastFree + lastFree->bytes == p)
                    {
                        /* Just increase the size of the previous free node. */
                        lastFree->bytes += node->bytes;
                    }
                    else
                    {
                        /* Add to linked list. */
                        lastFree->next = node;
                        lastFree       = node;
                    }
                }
            }

            /* Move to next node. */
            p = (gctUINT8_PTR) node + node->bytes;
        }

        /* Mark the end of the chain. */
        if (lastFree != gcvNULL)
        {
            lastFree->next = gcvNULL;
        }

        /* Get next heap. */
        next = heap->next;

        /* Check if the entire heap is free. */
        if ((heap->freeList != gcvNULL)
        &&  (heap->freeList->bytes == heap->size - gcmSIZEOF(gcskNODE))
        )
        {
            /* Remove the heap from the linked list. */
            if (heap->prev == gcvNULL)
            {
                Heap->heap = next;
            }
            else
            {
                heap->prev->next = next;
            }

            if (heap->next != gcvNULL)
            {
                heap->next->prev = heap->prev;
            }

#if VIVANTE_PROFILER || gcmIS_DEBUG(gcdDEBUG_CODE)
            /* Update profiling. */
            Heap->heapCount  -= 1;
            Heap->heapMemory -= heap->size + gcmSIZEOF(gcskHEAP);
#endif

            /* Add this heap to the list of heaps that need to be freed. */
            heap->next = freeList;
            freeList   = heap;
        }
    }

    if (freeList != gcvNULL)
    {
        /* Release the mutex, remove any chance for a dead lock. */
        gcmkVERIFY_OK(
            gckOS_ReleaseMutex(Heap->os, Heap->mutex));

        /* Free all heaps in the free list. */
        for (heap = freeList; heap != gcvNULL; heap = next)
        {
            /* Get pointer to the next heap. */
            next = heap->next;

            /* Free the heap. */
            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HEAP,
                           "Freeing heap 0x%x (%lu bytes)",
                           heap, heap->size + gcmSIZEOF(gcskHEAP));
            gcmkVERIFY_OK(gckOS_FreeMemory(Heap->os, heap));
        }

        /* Acquire the mutex again. */
        gcmkVERIFY_OK(
            gckOS_AcquireMutex(Heap->os, Heap->mutex, gcvINFINITE));
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
***** gckHEAP API Code *********************************************************
*******************************************************************************/

/*******************************************************************************
**
**  gckHEAP_Construct
**
**  Construct a new gckHEAP object.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      gctSIZE_T AllocationSize
**          Minimum size per arena.
**
**  OUTPUT:
**
**      gckHEAP * Heap
**          Pointer to a variable that will hold the pointer to the gckHEAP
**          object.
*/
gceSTATUS
gckHEAP_Construct(
    IN gckOS Os,
    IN gctSIZE_T AllocationSize,
    OUT gckHEAP * Heap
    )
{
    gceSTATUS status;
    gckHEAP heap = gcvNULL;
    gctPOINTER pointer = gcvNULL;

    gcmkHEADER_ARG("Os=0x%x AllocationSize=%lu", Os, AllocationSize);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Heap != gcvNULL);

    /* Allocate the gckHEAP object. */
    gcmkONERROR(gckOS_AllocateMemory(Os,
                                     gcmSIZEOF(struct _gckHEAP),
                                     &pointer));

    heap = pointer;

    /* Initialize the gckHEAP object. */
    heap->object.type    = gcvOBJ_HEAP;
    heap->os             = Os;
    heap->allocationSize = AllocationSize;
    heap->heap           = gcvNULL;
#if gcmIS_DEBUG(gcdDEBUG_CODE)
    heap->timeStamp      = 0;
#endif

#if VIVANTE_PROFILER || gcmIS_DEBUG(gcdDEBUG_CODE)
    /* Zero the counters. */
    heap->allocCount      = 0;
    heap->allocBytes      = 0;
    heap->allocBytesMax   = 0;
    heap->allocBytesTotal = 0;
    heap->heapCount       = 0;
    heap->heapCountMax    = 0;
    heap->heapMemory      = 0;
    heap->heapMemoryMax   = 0;
#endif

    /* Create the mutex. */
    gcmkONERROR(gckOS_CreateMutex(Os, &heap->mutex));

    /* Return the pointer to the gckHEAP object. */
    *Heap = heap;

    /* Success. */
    gcmkFOOTER_ARG("*Heap=0x%x", *Heap);
    return gcvSTATUS_OK;

OnError:
    /* Roll back. */
    if (heap != gcvNULL)
    {
        /* Free the heap structure. */
        gcmkVERIFY_OK(gckOS_FreeMemory(Os, heap));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHEAP_Destroy
**
**  Destroy a gckHEAP object.
**
**  INPUT:
**
**      gckHEAP Heap
**          Pointer to a gckHEAP object to destroy.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHEAP_Destroy(
    IN gckHEAP Heap
    )
{
    gcskHEAP_PTR heap;
#if gcmIS_DEBUG(gcdDEBUG_CODE)
    gctSIZE_T leaked = 0;
#endif

    gcmkHEADER_ARG("Heap=0x%x", Heap);

    for (heap = Heap->heap; heap != gcvNULL; heap = Heap->heap)
    {
        /* Unlink heap from linked list. */
        Heap->heap = heap->next;

#if gcmIS_DEBUG(gcdDEBUG_CODE)
        /* Check for leaked memory. */
        leaked += _DumpHeap(heap);
#endif

        /* Free the heap. */
        gcmkVERIFY_OK(gckOS_FreeMemory(Heap->os, heap));
    }

    /* Free the mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Heap->os, Heap->mutex));

    /* Free the heap structure. */
    gcmkVERIFY_OK(gckOS_FreeMemory(Heap->os, Heap));

    /* Success. */
#if gcmIS_DEBUG(gcdDEBUG_CODE)
    gcmkFOOTER_ARG("leaked=%lu", leaked);
#else
    gcmkFOOTER_NO();
#endif
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckHEAP_Allocate
**
**  Allocate data from the heap.
**
**  INPUT:
**
**      gckHEAP Heap
**          Pointer to a gckHEAP object.
**
**      IN gctSIZE_T Bytes
**          Number of byte to allocate.
**
**  OUTPUT:
**
**      gctPOINTER * Memory
**          Pointer to a variable that will hold the address of the allocated
**          memory.
*/
gceSTATUS
gckHEAP_Allocate(
    IN gckHEAP Heap,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Memory
    )
{
    gctBOOL acquired = gcvFALSE;
    gcskHEAP_PTR heap;
    gceSTATUS status;
    gctSIZE_T bytes;
    gcskNODE_PTR node, used, prevFree = gcvNULL;
    gctPOINTER memory = gcvNULL;

    gcmkHEADER_ARG("Heap=0x%x Bytes=%lu", Heap, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Heap, gcvOBJ_HEAP);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);

    /* Determine number of bytes required for a node. */
    bytes = gcmALIGN(Bytes + gcmSIZEOF(gcskNODE), 8);

    /* Acquire the mutex. */
    gcmkONERROR(
        gckOS_AcquireMutex(Heap->os, Heap->mutex, gcvINFINITE));

    acquired = gcvTRUE;

    /* Check if this allocation is bigger than the default allocation size. */
    if (bytes > Heap->allocationSize - gcmSIZEOF(gcskHEAP) - gcmSIZEOF(gcskNODE))
    {
        /* Adjust allocation size. */
        Heap->allocationSize = bytes * 2;
    }

    else if (Heap->heap != gcvNULL)
    {
        gctINT i;

        /* 2 retries, since we might need to compact. */
        for (i = 0; i < 2; ++i)
        {
            /* Walk all the heaps. */
            for (heap = Heap->heap; heap != gcvNULL; heap = heap->next)
            {
                /* Check if this heap has enough bytes to hold the request. */
                if (bytes <= heap->size - gcmSIZEOF(gcskNODE))
                {
                    prevFree = gcvNULL;

                    /* Walk the chain of free nodes. */
                    for (node = heap->freeList;
                         node != gcvNULL;
                         node = node->next
                    )
                    {
                        gcmkASSERT(node->next != gcdIN_USE);

                        /* Check if this free node has enough bytes. */
                        if (node->bytes >= bytes)
                        {
                            /* Use the node. */
                            goto UseNode;
                        }

                        /* Save current free node for linked list management. */
                        prevFree = node;
                    }
                }
            }

            if (i == 0)
            {
                /* Compact the heap. */
                gcmkVERIFY_OK(_CompactKernelHeap(Heap));

#if gcmIS_DEBUG(gcdDEBUG_CODE)
                gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_HEAP,
                               "===== KERNEL HEAP =====");
                gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_HEAP,
                               "Number of allocations           : %12u",
                               Heap->allocCount);
                gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_HEAP,
                               "Number of bytes allocated       : %12llu",
                               Heap->allocBytes);
                gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_HEAP,
                               "Maximum allocation size         : %12llu",
                               Heap->allocBytesMax);
                gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_HEAP,
                               "Total number of bytes allocated : %12llu",
                               Heap->allocBytesTotal);
                gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_HEAP,
                               "Number of heaps                 : %12u",
                               Heap->heapCount);
                gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_HEAP,
                               "Heap memory in bytes            : %12llu",
                               Heap->heapMemory);
                gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_HEAP,
                               "Maximum number of heaps         : %12u",
                               Heap->heapCountMax);
                gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_HEAP,
                               "Maximum heap memory in bytes    : %12llu",
                               Heap->heapMemoryMax);
#endif
            }
        }
    }

    /* Release the mutex. */
    gcmkONERROR(
        gckOS_ReleaseMutex(Heap->os, Heap->mutex));

    acquired = gcvFALSE;

    /* Allocate a new heap. */
    gcmkONERROR(
        gckOS_AllocateMemory(Heap->os,
                             Heap->allocationSize,
                             &memory));

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HEAP,
                   "Allocated heap 0x%x (%lu bytes)",
                   memory, Heap->allocationSize);

    /* Acquire the mutex. */
    gcmkONERROR(
        gckOS_AcquireMutex(Heap->os, Heap->mutex, gcvINFINITE));

    acquired = gcvTRUE;

    /* Use the allocated memory as the heap. */
    heap = (gcskHEAP_PTR) memory;

    /* Insert this heap to the head of the chain. */
    heap->next = Heap->heap;
    heap->prev = gcvNULL;
    heap->size = Heap->allocationSize - gcmSIZEOF(gcskHEAP);

    if (heap->next != gcvNULL)
    {
        heap->next->prev = heap;
    }
    Heap->heap = heap;

    /* Mark the end of the heap. */
    node = (gcskNODE_PTR) ( (gctUINT8_PTR) heap
                          + Heap->allocationSize
                          - gcmSIZEOF(gcskNODE)
                          );
    node->bytes = 0;
    node->next  = gcvNULL;

    /* Create a free list. */
    node           = (gcskNODE_PTR) (heap + 1);
    heap->freeList = node;

    /* Initialize the free list. */
    node->bytes = heap->size - gcmSIZEOF(gcskNODE);
    node->next  = gcvNULL;

    /* No previous free. */
    prevFree = gcvNULL;

#if VIVANTE_PROFILER || gcmIS_DEBUG(gcdDEBUG_CODE)
    /* Update profiling. */
    Heap->heapCount  += 1;
    Heap->heapMemory += Heap->allocationSize;

    if (Heap->heapCount > Heap->heapCountMax)
    {
        Heap->heapCountMax = Heap->heapCount;
    }
    if (Heap->heapMemory > Heap->heapMemoryMax)
    {
        Heap->heapMemoryMax = Heap->heapMemory;
    }
#endif

UseNode:
    /* Verify some stuff. */
    gcmkASSERT(heap != gcvNULL);
    gcmkASSERT(node != gcvNULL);
    gcmkASSERT(node->bytes >= bytes);

    if (heap->prev != gcvNULL)
    {
        /* Unlink the heap from the linked list. */
        heap->prev->next = heap->next;
        if (heap->next != gcvNULL)
        {
            heap->next->prev = heap->prev;
        }

        /* Move the heap to the front of the list. */
        heap->next       = Heap->heap;
        heap->prev       = gcvNULL;
        Heap->heap       = heap;
        heap->next->prev = heap;
    }

    /* Check if there is enough free space left after usage for another free
    ** node. */
    if (node->bytes - bytes >= gcmSIZEOF(gcskNODE))
    {
        /* Allocated used space from the back of the free list. */
        used = (gcskNODE_PTR) ((gctUINT8_PTR) node + node->bytes - bytes);

        /* Adjust the number of free bytes. */
        node->bytes -= bytes;
        gcmkASSERT(node->bytes >= gcmSIZEOF(gcskNODE));
    }
    else
    {
        /* Remove this free list from the chain. */
        if (prevFree == gcvNULL)
        {
            heap->freeList = node->next;
        }
        else
        {
            prevFree->next = node->next;
        }

        /* Consume the entire free node. */
        used  = (gcskNODE_PTR) node;
        bytes = node->bytes;
    }

    /* Mark node as used. */
    used->bytes     = bytes;
    used->next      = gcdIN_USE;
#if gcmIS_DEBUG(gcdDEBUG_CODE)
    used->timeStamp = ++Heap->timeStamp;
#endif

#if VIVANTE_PROFILER || gcmIS_DEBUG(gcdDEBUG_CODE)
    /* Update profile counters. */
    Heap->allocCount      += 1;
    Heap->allocBytes      += bytes;
    Heap->allocBytesMax    = gcmMAX(Heap->allocBytes, Heap->allocBytesMax);
    Heap->allocBytesTotal += bytes;
#endif

    /* Release the mutex. */
    gcmkVERIFY_OK(
        gckOS_ReleaseMutex(Heap->os, Heap->mutex));

    /* Return pointer to memory. */
    *Memory = used + 1;

    /* Success. */
    gcmkFOOTER_ARG("*Memory=0x%x", *Memory);
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(
            gckOS_ReleaseMutex(Heap->os, Heap->mutex));
    }

    if (memory != gcvNULL)
    {
        /* Free the heap memory. */
        gckOS_FreeMemory(Heap->os, memory);
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHEAP_Free
**
**  Free allocated memory from the heap.
**
**  INPUT:
**
**      gckHEAP Heap
**          Pointer to a gckHEAP object.
**
**      IN gctPOINTER Memory
**          Pointer to memory to free.
**
**  OUTPUT:
**
**      NOTHING.
*/
gceSTATUS
gckHEAP_Free(
    IN gckHEAP Heap,
    IN gctPOINTER Memory
    )
{
    gcskNODE_PTR node;
    gceSTATUS status;

    gcmkHEADER_ARG("Heap=0x%x Memory=0x%x", Heap, Memory);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Heap, gcvOBJ_HEAP);
    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);

    /* Acquire the mutex. */
    gcmkONERROR(
        gckOS_AcquireMutex(Heap->os, Heap->mutex, gcvINFINITE));

    /* Pointer to structure. */
    node = (gcskNODE_PTR) Memory - 1;

    /* Mark the node as freed. */
    node->next = gcvNULL;

#if VIVANTE_PROFILER || gcmIS_DEBUG(gcdDEBUG_CODE)
    /* Update profile counters. */
    Heap->allocBytes -= node->bytes;
#endif

    /* Release the mutex. */
    gcmkVERIFY_OK(
        gckOS_ReleaseMutex(Heap->os, Heap->mutex));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

#if VIVANTE_PROFILER
gceSTATUS
gckHEAP_ProfileStart(
    IN gckHEAP Heap
    )
{
    gcmkHEADER_ARG("Heap=0x%x", Heap);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Heap, gcvOBJ_HEAP);

    /* Zero the counters. */
    Heap->allocCount      = 0;
    Heap->allocBytes      = 0;
    Heap->allocBytesMax   = 0;
    Heap->allocBytesTotal = 0;
    Heap->heapCount       = 0;
    Heap->heapCountMax    = 0;
    Heap->heapMemory      = 0;
    Heap->heapMemoryMax   = 0;

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

gceSTATUS
gckHEAP_ProfileEnd(
    IN gckHEAP Heap,
    IN gctCONST_STRING Title
    )
{
    gcmkHEADER_ARG("Heap=0x%x Title=0x%x", Heap, Title);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Heap, gcvOBJ_HEAP);
    gcmkVERIFY_ARGUMENT(Title != gcvNULL);

    gcmkPRINT("");
    gcmkPRINT("=====[ HEAP - %s ]=====", Title);
    gcmkPRINT("Number of allocations           : %12u",   Heap->allocCount);
    gcmkPRINT("Number of bytes allocated       : %12llu", Heap->allocBytes);
    gcmkPRINT("Maximum allocation size         : %12llu", Heap->allocBytesMax);
    gcmkPRINT("Total number of bytes allocated : %12llu", Heap->allocBytesTotal);
    gcmkPRINT("Number of heaps                 : %12u",   Heap->heapCount);
    gcmkPRINT("Heap memory in bytes            : %12llu", Heap->heapMemory);
    gcmkPRINT("Maximum number of heaps         : %12u",   Heap->heapCountMax);
    gcmkPRINT("Maximum heap memory in bytes    : %12llu", Heap->heapMemoryMax);
    gcmkPRINT("==============================================");

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}
#endif /* VIVANTE_PROFILER */

/*******************************************************************************
***** Test Code ****************************************************************
*******************************************************************************/

