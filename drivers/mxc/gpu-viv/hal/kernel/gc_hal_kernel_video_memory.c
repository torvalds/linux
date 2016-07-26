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

#if defined(__QNXNTO__)
#include <stdlib.h>
#include <sys/slogcodes.h>
#include <time.h>

extern unsigned int slogUsageInterval;
#endif

#define _GC_OBJ_ZONE    gcvZONE_VIDMEM

/******************************************************************************\
******************************* Private Functions ******************************
\******************************************************************************/

/*******************************************************************************
**
**  _Split
**
**  Split a node on the required byte boundary.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gcuVIDMEM_NODE_PTR Node
**          Pointer to the node to split.
**
**      gctSIZE_T Bytes
**          Number of bytes to keep in the node.
**
**  OUTPUT:
**
**      Nothing.
**
**  RETURNS:
**
**      gctBOOL
**          gcvTRUE if the node was split successfully, or gcvFALSE if there is an
**          error.
**
*/
static gctBOOL
_Split(
    IN gckOS Os,
    IN gcuVIDMEM_NODE_PTR Node,
    IN gctSIZE_T Bytes
    )
{
    gcuVIDMEM_NODE_PTR node;
    gctPOINTER pointer = gcvNULL;

    /* Make sure the byte boundary makes sense. */
    if ((Bytes <= 0) || (Bytes > Node->VidMem.bytes))
    {
        return gcvFALSE;
    }

    /* Allocate a new gcuVIDMEM_NODE object. */
    if (gcmIS_ERROR(gckOS_Allocate(Os,
                                   gcmSIZEOF(gcuVIDMEM_NODE),
                                   &pointer)))
    {
        /* Error. */
        return gcvFALSE;
    }

    node = pointer;

    /* Initialize gcuVIDMEM_NODE structure. */
    node->VidMem.offset    = Node->VidMem.offset + Bytes;
    node->VidMem.bytes     = Node->VidMem.bytes  - Bytes;
    node->VidMem.alignment = 0;
    node->VidMem.locked    = 0;
    node->VidMem.memory    = Node->VidMem.memory;
    node->VidMem.pool      = Node->VidMem.pool;
    node->VidMem.physical  = Node->VidMem.physical;
#ifdef __QNXNTO__
    node->VidMem.processID = 0;
    node->VidMem.logical   = gcvNULL;
#endif

    /* Insert node behind specified node. */
    node->VidMem.next = Node->VidMem.next;
    node->VidMem.prev = Node;
    Node->VidMem.next = node->VidMem.next->VidMem.prev = node;

    /* Insert free node behind specified node. */
    node->VidMem.nextFree = Node->VidMem.nextFree;
    node->VidMem.prevFree = Node;
    Node->VidMem.nextFree = node->VidMem.nextFree->VidMem.prevFree = node;

    /* Adjust size of specified node. */
    Node->VidMem.bytes = Bytes;

    /* Success. */
    return gcvTRUE;
}

/*******************************************************************************
**
**  _Merge
**
**  Merge two adjacent nodes together.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gcuVIDMEM_NODE_PTR Node
**          Pointer to the first of the two nodes to merge.
**
**  OUTPUT:
**
**      Nothing.
**
*/
static gceSTATUS
_Merge(
    IN gckOS Os,
    IN gcuVIDMEM_NODE_PTR Node
    )
{
    gcuVIDMEM_NODE_PTR node;
    gceSTATUS status;

    /* Save pointer to next node. */
    node = Node->VidMem.next;

    /* This is a good time to make sure the heap is not corrupted. */
    if (Node->VidMem.offset + Node->VidMem.bytes != node->VidMem.offset)
    {
        /* Corrupted heap. */
        gcmkASSERT(
            Node->VidMem.offset + Node->VidMem.bytes == node->VidMem.offset);
        return gcvSTATUS_HEAP_CORRUPTED;
    }

    /* Adjust byte count. */
    Node->VidMem.bytes += node->VidMem.bytes;

    /* Unlink next node from linked list. */
    Node->VidMem.next     = node->VidMem.next;
    Node->VidMem.nextFree = node->VidMem.nextFree;

    Node->VidMem.next->VidMem.prev         =
    Node->VidMem.nextFree->VidMem.prevFree = Node;

    /* Free next node. */
    status = gcmkOS_SAFE_FREE(Os, node);
    return status;
}

/******************************************************************************\
******************************* gckVIDMEM API Code ******************************
\******************************************************************************/

/*******************************************************************************
**
**  gckVIDMEM_ConstructVirtual
**
**  Construct a new gcuVIDMEM_NODE union for virtual memory.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      gctSIZE_T Bytes
**          Number of byte to allocate.
**
**  OUTPUT:
**
**      gcuVIDMEM_NODE_PTR * Node
**          Pointer to a variable that receives the gcuVIDMEM_NODE union pointer.
*/
gceSTATUS
gckVIDMEM_ConstructVirtual(
    IN gckKERNEL Kernel,
    IN gctUINT32 Flag,
    IN gctSIZE_T Bytes,
    OUT gcuVIDMEM_NODE_PTR * Node
    )
{
    gckOS os;
    gceSTATUS status;
    gcuVIDMEM_NODE_PTR node = gcvNULL;
    gctPOINTER pointer = gcvNULL;
    gctINT i;

    gcmkHEADER_ARG("Kernel=0x%x Flag=%x Bytes=%lu", Kernel, Flag, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Node != gcvNULL);

    /* Extract the gckOS object pointer. */
    os = Kernel->os;
    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    /* Allocate an gcuVIDMEM_NODE union. */
    gcmkONERROR(gckOS_Allocate(os, gcmSIZEOF(gcuVIDMEM_NODE), &pointer));

    node = pointer;

    /* Initialize gcuVIDMEM_NODE union for virtual memory. */
    node->Virtual.kernel        = Kernel;
    node->Virtual.contiguous    = Flag & gcvALLOC_FLAG_CONTIGUOUS;
    node->Virtual.logical       = gcvNULL;
#if gcdENABLE_VG
    node->Virtual.kernelVirtual = gcvNULL;
#endif
    node->Virtual.secure        = (Flag & gcvALLOC_FLAG_SECURITY) != 0;

    for (i = 0; i < gcdMAX_GPU_COUNT; i++)
    {
        node->Virtual.lockeds[i]        = 0;
        node->Virtual.pageTables[i]     = gcvNULL;
    }

    /* Allocate the virtual memory. */
    gcmkONERROR(
        gckOS_AllocatePagedMemoryEx(os,
                                    Flag,
                                    node->Virtual.bytes = Bytes,
                                    &node->Virtual.gid,
                                    &node->Virtual.physical));

    /* Return pointer to the gcuVIDMEM_NODE union. */
    *Node = node;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                   "Created virtual node 0x%x for %u bytes @ 0x%x",
                   node, Bytes, node->Virtual.physical);

    /* Success. */
    gcmkFOOTER_ARG("*Node=0x%x", *Node);
    return gcvSTATUS_OK;

OnError:
    /* Roll back. */
    if (node != gcvNULL)
    {
        /* Free the structure. */
        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(os, node));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckVIDMEM_DestroyVirtual
**
**  Destroy an gcuVIDMEM_NODE union for virtual memory.
**
**  INPUT:
**
**      gcuVIDMEM_NODE_PTR Node
**          Pointer to a gcuVIDMEM_NODE union.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckVIDMEM_DestroyVirtual(
    IN gcuVIDMEM_NODE_PTR Node
    )
{
    gckOS os;

    gcmkHEADER_ARG("Node=0x%x", Node);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Node->Virtual.kernel, gcvOBJ_KERNEL);

    /* Extact the gckOS object pointer. */
    os = Node->Virtual.kernel->os;
    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    /* Delete the gcuVIDMEM_NODE union. */
    gcmkVERIFY_OK(gcmkOS_SAFE_FREE(os, Node));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckVIDMEM_Construct
**
**  Construct a new gckVIDMEM object.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctUINT32 BaseAddress
**          Base address for the video memory heap.
**
**      gctSIZE_T Bytes
**          Number of bytes in the video memory heap.
**
**      gctSIZE_T Threshold
**          Minimum number of bytes beyond am allocation before the node is
**          split.  Can be used as a minimum alignment requirement.
**
**      gctSIZE_T BankSize
**          Number of bytes per physical memory bank.  Used by bank
**          optimization.
**
**  OUTPUT:
**
**      gckVIDMEM * Memory
**          Pointer to a variable that will hold the pointer to the gckVIDMEM
**          object.
*/
gceSTATUS
gckVIDMEM_Construct(
    IN gckOS Os,
    IN gctUINT32 BaseAddress,
    IN gctSIZE_T Bytes,
    IN gctSIZE_T Threshold,
    IN gctSIZE_T BankSize,
    OUT gckVIDMEM * Memory
    )
{
    gckVIDMEM memory = gcvNULL;
    gceSTATUS status;
    gcuVIDMEM_NODE_PTR node;
    gctINT i, banks = 0;
    gctPOINTER pointer = gcvNULL;
    gctUINT32 heapBytes;
    gctUINT32 bankSize;

    gcmkHEADER_ARG("Os=0x%x BaseAddress=%08x Bytes=%lu Threshold=%lu "
                   "BankSize=%lu",
                   Os, BaseAddress, Bytes, Threshold, BankSize);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);

    gcmkSAFECASTSIZET(heapBytes, Bytes);
    gcmkSAFECASTSIZET(bankSize, BankSize);

    /* Allocate the gckVIDMEM object. */
    gcmkONERROR(gckOS_Allocate(Os, gcmSIZEOF(struct _gckVIDMEM), &pointer));

    memory = pointer;

    /* Initialize the gckVIDMEM object. */
    memory->object.type = gcvOBJ_VIDMEM;
    memory->os          = Os;

    /* Set video memory heap information. */
    memory->baseAddress = BaseAddress;
    memory->bytes       = heapBytes;
    memory->freeBytes   = heapBytes;
    memory->threshold   = Threshold;
    memory->mutex       = gcvNULL;

    BaseAddress = 0;

    /* Walk all possible banks. */
    for (i = 0; i < gcmCOUNTOF(memory->sentinel); ++i)
    {
        gctUINT32 bytes;

        if (BankSize == 0)
        {
            /* Use all bytes for the first bank. */
            bytes = heapBytes;
        }
        else
        {
            /* Compute number of bytes for this bank. */
            bytes = gcmALIGN(BaseAddress + 1, bankSize) - BaseAddress;

            if (bytes > heapBytes)
            {
                /* Make sure we don't exceed the total number of bytes. */
                bytes = heapBytes;
            }
        }

        if (bytes == 0)
        {
            /* Mark heap is not used. */
            memory->sentinel[i].VidMem.next     =
            memory->sentinel[i].VidMem.prev     =
            memory->sentinel[i].VidMem.nextFree =
            memory->sentinel[i].VidMem.prevFree = gcvNULL;
            continue;
        }

        /* Allocate one gcuVIDMEM_NODE union. */
        gcmkONERROR(gckOS_Allocate(Os, gcmSIZEOF(gcuVIDMEM_NODE), &pointer));

        node = pointer;

        /* Initialize gcuVIDMEM_NODE union. */
        node->VidMem.memory    = memory;

        node->VidMem.next      =
        node->VidMem.prev      =
        node->VidMem.nextFree  =
        node->VidMem.prevFree  = &memory->sentinel[i];

        node->VidMem.offset    = BaseAddress;
        node->VidMem.bytes     = bytes;
        node->VidMem.alignment = 0;
        node->VidMem.physical  = 0;
        node->VidMem.pool      = gcvPOOL_UNKNOWN;

        node->VidMem.locked    = 0;

#ifdef __QNXNTO__
        node->VidMem.processID = 0;
        node->VidMem.logical   = gcvNULL;
#endif

#if gcdENABLE_VG
        node->VidMem.kernelVirtual = gcvNULL;
#endif

        /* Initialize the linked list of nodes. */
        memory->sentinel[i].VidMem.next     =
        memory->sentinel[i].VidMem.prev     =
        memory->sentinel[i].VidMem.nextFree =
        memory->sentinel[i].VidMem.prevFree = node;

        /* Mark sentinel. */
        memory->sentinel[i].VidMem.bytes = 0;

        /* Adjust address for next bank. */
        BaseAddress += bytes;
        heapBytes       -= bytes;
        banks       ++;
    }

    /* Assign all the bank mappings. */
    memory->mapping[gcvSURF_RENDER_TARGET]      = banks - 1;
    memory->mapping[gcvSURF_BITMAP]             = banks - 1;
    if (banks > 1) --banks;
    memory->mapping[gcvSURF_DEPTH]              = banks - 1;
    memory->mapping[gcvSURF_HIERARCHICAL_DEPTH] = banks - 1;
    if (banks > 1) --banks;
    memory->mapping[gcvSURF_TEXTURE]            = banks - 1;
    if (banks > 1) --banks;
    memory->mapping[gcvSURF_VERTEX]             = banks - 1;
    if (banks > 1) --banks;
    memory->mapping[gcvSURF_INDEX]              = banks - 1;
    if (banks > 1) --banks;
    memory->mapping[gcvSURF_TILE_STATUS]        = banks - 1;
    if (banks > 1) --banks;
    memory->mapping[gcvSURF_TYPE_UNKNOWN]       = 0;

#if gcdENABLE_VG
    memory->mapping[gcvSURF_IMAGE]   = 0;
    memory->mapping[gcvSURF_MASK]    = 0;
    memory->mapping[gcvSURF_SCISSOR] = 0;
#endif
    memory->mapping[gcvSURF_ICACHE]             = 0;
    memory->mapping[gcvSURF_TXDESC]             = 0;
    memory->mapping[gcvSURF_FENCE]              = 0;
    memory->mapping[gcvSURF_TFBHEADER]          = 0;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                  "[GALCORE] INDEX:         bank %d",
                  memory->mapping[gcvSURF_INDEX]);
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                  "[GALCORE] VERTEX:        bank %d",
                  memory->mapping[gcvSURF_VERTEX]);
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                  "[GALCORE] TEXTURE:       bank %d",
                  memory->mapping[gcvSURF_TEXTURE]);
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                  "[GALCORE] RENDER_TARGET: bank %d",
                  memory->mapping[gcvSURF_RENDER_TARGET]);
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                  "[GALCORE] DEPTH:         bank %d",
                  memory->mapping[gcvSURF_DEPTH]);
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                  "[GALCORE] TILE_STATUS:   bank %d",
                  memory->mapping[gcvSURF_TILE_STATUS]);

    /* Allocate the mutex. */
    gcmkONERROR(gckOS_CreateMutex(Os, &memory->mutex));

    /* Return pointer to the gckVIDMEM object. */
    *Memory = memory;

    /* Success. */
    gcmkFOOTER_ARG("*Memory=0x%x", *Memory);
    return gcvSTATUS_OK;

OnError:
    /* Roll back. */
    if (memory != gcvNULL)
    {
        if (memory->mutex != gcvNULL)
        {
            /* Delete the mutex. */
            gcmkVERIFY_OK(gckOS_DeleteMutex(Os, memory->mutex));
        }

        for (i = 0; i < banks; ++i)
        {
            /* Free the heap. */
            gcmkASSERT(memory->sentinel[i].VidMem.next != gcvNULL);
            gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Os, memory->sentinel[i].VidMem.next));
        }

        /* Free the object. */
        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Os, memory));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckVIDMEM_Destroy
**
**  Destroy an gckVIDMEM object.
**
**  INPUT:
**
**      gckVIDMEM Memory
**          Pointer to an gckVIDMEM object to destroy.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckVIDMEM_Destroy(
    IN gckVIDMEM Memory
    )
{
    gcuVIDMEM_NODE_PTR node, next;
    gctINT i;

    gcmkHEADER_ARG("Memory=0x%x", Memory);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Memory, gcvOBJ_VIDMEM);

    /* Walk all sentinels. */
    for (i = 0; i < gcmCOUNTOF(Memory->sentinel); ++i)
    {
        /* Bail out of the heap is not used. */
        if (Memory->sentinel[i].VidMem.next == gcvNULL)
        {
            break;
        }

        /* Walk all the nodes until we reach the sentinel. */
        for (node = Memory->sentinel[i].VidMem.next;
             node->VidMem.bytes != 0;
             node = next)
        {
            /* Save pointer to the next node. */
            next = node->VidMem.next;

            /* Free the node. */
            gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Memory->os, node));
        }
    }

    /* Free the mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Memory->os, Memory->mutex));

    /* Mark the object as unknown. */
    Memory->object.type = gcvOBJ_UNKNOWN;

    /* Free the gckVIDMEM object. */
    gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Memory->os, Memory));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

#if gcdENABLE_BANK_ALIGNMENT

#if !gcdBANK_BIT_START
#error gcdBANK_BIT_START not defined.
#endif

#if !gcdBANK_BIT_END
#error gcdBANK_BIT_END not defined.
#endif
/*******************************************************************************
**  _GetSurfaceBankAlignment
**
**  Return the required offset alignment required to the make BaseAddress
**  aligned properly.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to gcoOS object.
**
**      gceSURF_TYPE Type
**          Type of allocation.
**
**      gctUINT32 BaseAddress
**          Base address of current video memory node.
**
**  OUTPUT:
**
**      gctUINT32_PTR AlignmentOffset
**          Pointer to a variable that will hold the number of bytes to skip in
**          the current video memory node in order to make the alignment bank
**          aligned.
*/
static gceSTATUS
_GetSurfaceBankAlignment(
    IN gckKERNEL Kernel,
    IN gceSURF_TYPE Type,
    IN gctUINT32 BaseAddress,
    OUT gctUINT32_PTR AlignmentOffset
    )
{
    gctUINT32 bank;
    /* To retrieve the bank. */
    static const gctUINT32 bankMask = (0xFFFFFFFF << gcdBANK_BIT_START)
                                    ^ (0xFFFFFFFF << (gcdBANK_BIT_END + 1));

    /* To retrieve the bank and all the lower bytes. */
    static const gctUINT32 byteMask = ~(0xFFFFFFFF << (gcdBANK_BIT_END + 1));

    gcmkHEADER_ARG("Type=%d BaseAddress=0x%x ", Type, BaseAddress);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(AlignmentOffset != gcvNULL);

    switch (Type)
    {
    case gcvSURF_RENDER_TARGET:
        bank = (BaseAddress & bankMask) >> (gcdBANK_BIT_START);

        /* Align to the first bank. */
        *AlignmentOffset = (bank == 0) ?
            0 :
            ((1 << (gcdBANK_BIT_END + 1)) + 0) -  (BaseAddress & byteMask);
        break;

    case gcvSURF_DEPTH:
        bank = (BaseAddress & bankMask) >> (gcdBANK_BIT_START);

        /* Align to the third bank. */
        *AlignmentOffset = (bank == 2) ?
            0 :
            ((1 << (gcdBANK_BIT_END + 1)) + (2 << gcdBANK_BIT_START)) -  (BaseAddress & byteMask);

        /* Minimum 256 byte alignment needed for fast_msaa. */
        if ((gcdBANK_CHANNEL_BIT > 7) ||
            ((gckHARDWARE_IsFeatureAvailable(Kernel->hardware, gcvFEATURE_FAST_MSAA) != gcvSTATUS_TRUE) &&
             (gckHARDWARE_IsFeatureAvailable(Kernel->hardware, gcvFEATURE_SMALL_MSAA) != gcvSTATUS_TRUE)))
        {
            /* Add a channel offset at the channel bit. */
            *AlignmentOffset += (1 << gcdBANK_CHANNEL_BIT);
        }
        break;

    default:
        /* no alignment needed. */
        *AlignmentOffset = 0;
    }

    /* Return the status. */
    gcmkFOOTER_ARG("*AlignmentOffset=%u", *AlignmentOffset);
    return gcvSTATUS_OK;
}
#endif

static gcuVIDMEM_NODE_PTR
_FindNode(
    IN gckKERNEL Kernel,
    IN gckVIDMEM Memory,
    IN gctINT Bank,
    IN gctSIZE_T Bytes,
    IN gceSURF_TYPE Type,
    IN OUT gctUINT32_PTR Alignment
    )
{
    gcuVIDMEM_NODE_PTR node;
    gctUINT32 alignment;

#if gcdENABLE_BANK_ALIGNMENT
    gctUINT32 bankAlignment;
    gceSTATUS status;
#endif

    if (Memory->sentinel[Bank].VidMem.nextFree == gcvNULL)
    {
        /* No free nodes left. */
        return gcvNULL;
    }

#if gcdENABLE_BANK_ALIGNMENT
    /* Walk all free nodes until we have one that is big enough or we have
    ** reached the sentinel. */
    for (node = Memory->sentinel[Bank].VidMem.nextFree;
         node->VidMem.bytes != 0;
         node = node->VidMem.nextFree)
    {
        if (node->VidMem.bytes < Bytes)
        {
            continue;
        }

        gcmkONERROR(_GetSurfaceBankAlignment(
            Kernel,
            Type,
            node->VidMem.memory->baseAddress + node->VidMem.offset,
            &bankAlignment));

        bankAlignment = gcmALIGN(bankAlignment, *Alignment);

        /* Compute number of bytes to skip for alignment. */
        alignment = (*Alignment == 0)
                  ? 0
                  : (*Alignment - (node->VidMem.offset % *Alignment));

        if (alignment == *Alignment)
        {
            /* Node is already aligned. */
            alignment = 0;
        }

        if (node->VidMem.bytes >= Bytes + alignment + bankAlignment)
        {
            /* This node is big enough. */
            *Alignment = alignment + bankAlignment;
            return node;
        }
    }
#endif

    /* Walk all free nodes until we have one that is big enough or we have
       reached the sentinel. */
    for (node = Memory->sentinel[Bank].VidMem.nextFree;
         node->VidMem.bytes != 0;
         node = node->VidMem.nextFree)
    {
        gctUINT offset;

        gctINT modulo;

        gcmkSAFECASTSIZET(offset, node->VidMem.offset);

        modulo = gckMATH_ModuloInt(offset, *Alignment);

        /* Compute number of bytes to skip for alignment. */
        alignment = (*Alignment == 0) ? 0 : (*Alignment - modulo);

        if (alignment == *Alignment)
        {
            /* Node is already aligned. */
            alignment = 0;
        }

        if (node->VidMem.bytes >= Bytes + alignment)
        {
            /* This node is big enough. */
            *Alignment = alignment;
            return node;
        }
    }

#if gcdENABLE_BANK_ALIGNMENT
OnError:
#endif
    /* Not enough memory. */
    return gcvNULL;
}

/*******************************************************************************
**
**  gckVIDMEM_AllocateLinear
**
**  Allocate linear memory from the gckVIDMEM object.
**
**  INPUT:
**
**      gckVIDMEM Memory
**          Pointer to an gckVIDMEM object.
**
**      gctSIZE_T Bytes
**          Number of bytes to allocate.
**
**      gctUINT32 Alignment
**          Byte alignment for allocation.
**
**      gceSURF_TYPE Type
**          Type of surface to allocate (use by bank optimization).
**
**      gctBOOL Specified
**          If user must use this pool, it should set Specified to gcvTRUE,
**          otherwise allocator may reserve some memory for other usage, such
**          as small block size allocation request.
**
**  OUTPUT:
**
**      gcuVIDMEM_NODE_PTR * Node
**          Pointer to a variable that will hold the allocated memory node.
*/
gceSTATUS
gckVIDMEM_AllocateLinear(
    IN gckKERNEL Kernel,
    IN gckVIDMEM Memory,
    IN gctSIZE_T Bytes,
    IN gctUINT32 Alignment,
    IN gceSURF_TYPE Type,
    IN gctBOOL Specified,
    OUT gcuVIDMEM_NODE_PTR * Node
    )
{
    gceSTATUS status;
    gcuVIDMEM_NODE_PTR node;
    gctUINT32 alignment;
    gctINT bank, i;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Memory=0x%x Bytes=%lu Alignment=%u Type=%d",
                   Memory, Bytes, Alignment, Type);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Memory, gcvOBJ_VIDMEM);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Node != gcvNULL);
    gcmkVERIFY_ARGUMENT(Type < gcvSURF_NUM_TYPES);

    /* Acquire the mutex. */
    gcmkONERROR(gckOS_AcquireMutex(Memory->os, Memory->mutex, gcvINFINITE));

    acquired = gcvTRUE;
#if defined(__QNXNTO__)
    if (slogUsageInterval > 0) {
        static gctSIZE_T lowwaterFPC = ~0;
        static time_t last_slog_time;
        int do_slog_now = 0;
        time_t this_slog_time = time(NULL);

        if (Memory->freeBytes < lowwaterFPC) {
            do_slog_now = 1;
            lowwaterFPC = Memory->freeBytes;
        }

        if (abs(this_slog_time - last_slog_time) > slogUsageInterval) {
            do_slog_now = 1;
        }

        if (do_slog_now) {
            last_slog_time = this_slog_time;
            slogf(_SLOGC_GRAPHICS_GL, _SLOG_INFO, "%s: Memory->freeBytes = %u, lowest Memory->freeBytes = %u",
                    __FUNCTION__, (unsigned) Memory->freeBytes, (unsigned) lowwaterFPC);
        }
    }
#endif
    if (Bytes > Memory->freeBytes)
    {
        /* Not enough memory. */
        status = gcvSTATUS_OUT_OF_MEMORY;
        goto OnError;
    }

#if gcdSMALL_BLOCK_SIZE
    if ((Memory->freeBytes < (Memory->bytes/gcdRATIO_FOR_SMALL_MEMORY))
    &&  (Bytes >= gcdSMALL_BLOCK_SIZE)
    &&  (Specified == gcvFALSE)
    )
    {
        /* The left memory is for small memory.*/
        status = gcvSTATUS_OUT_OF_MEMORY;
        goto OnError;
    }
#endif

    /* Find the default bank for this surface type. */
    gcmkASSERT((gctINT) Type < gcmCOUNTOF(Memory->mapping));
    bank      = Memory->mapping[Type];
    alignment = Alignment;

    /* Find a free node in the default bank. */
    node = _FindNode(Kernel, Memory, bank, Bytes, Type, &alignment);

    /* Out of memory? */
    if (node == gcvNULL)
    {
        /* Walk all lower banks. */
        for (i = bank - 1; i >= 0; --i)
        {
            /* Find a free node inside the current bank. */
            node = _FindNode(Kernel, Memory, i, Bytes, Type, &alignment);
            if (node != gcvNULL)
            {
                break;
            }
        }
    }

    if (node == gcvNULL)
    {
        /* Walk all upper banks. */
        for (i = bank + 1; i < gcmCOUNTOF(Memory->sentinel); ++i)
        {
            if (Memory->sentinel[i].VidMem.nextFree == gcvNULL)
            {
                /* Abort when we reach unused banks. */
                break;
            }

            /* Find a free node inside the current bank. */
            node = _FindNode(Kernel, Memory, i, Bytes, Type, &alignment);
            if (node != gcvNULL)
            {
                break;
            }
        }
    }

    if (node == gcvNULL)
    {
        /* Out of memory. */
        status = gcvSTATUS_OUT_OF_MEMORY;
        goto OnError;
    }

    /* Do we have an alignment? */
    if (alignment > 0)
    {
        /* Split the node so it is aligned. */
        if (_Split(Memory->os, node, alignment))
        {
            /* Successful split, move to aligned node. */
            node = node->VidMem.next;

            /* Remove alignment. */
            alignment = 0;
        }
    }

    /* Do we have enough memory after the allocation to split it? */
    if (node->VidMem.bytes - Bytes > Memory->threshold)
    {
        /* Adjust the node size. */
        _Split(Memory->os, node, Bytes);
    }

    /* Remove the node from the free list. */
    node->VidMem.prevFree->VidMem.nextFree = node->VidMem.nextFree;
    node->VidMem.nextFree->VidMem.prevFree = node->VidMem.prevFree;
    node->VidMem.nextFree                  =
    node->VidMem.prevFree                  = gcvNULL;

    /* Fill in the information. */
    node->VidMem.alignment = alignment;
    node->VidMem.memory    = Memory;
#ifdef __QNXNTO__
    node->VidMem.logical   = gcvNULL;
    gcmkONERROR(gckOS_GetProcessID(&node->VidMem.processID));
#endif

    /* Adjust the number of free bytes. */
    Memory->freeBytes -= node->VidMem.bytes;

#if gcdENABLE_VG
    node->VidMem.kernelVirtual = gcvNULL;
#endif

    /* Release the mutex. */
    gcmkVERIFY_OK(gckOS_ReleaseMutex(Memory->os, Memory->mutex));

    /* Return the pointer to the node. */
    *Node = node;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                   "Allocated %u bytes @ 0x%x [0x%08X]",
                   node->VidMem.bytes, node, node->VidMem.offset);

    /* Success. */
    gcmkFOOTER_ARG("*Node=0x%x", *Node);
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
     /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Memory->os, Memory->mutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckVIDMEM_Free
**
**  Free an allocated video memory node.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      gcuVIDMEM_NODE_PTR Node
**          Pointer to a gcuVIDMEM_NODE object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckVIDMEM_Free(
    IN gckKERNEL Kernel,
    IN gcuVIDMEM_NODE_PTR Node
    )
{
    gceSTATUS status;
    gckKERNEL kernel = gcvNULL;
    gckVIDMEM memory = gcvNULL;
    gcuVIDMEM_NODE_PTR node;
    gctBOOL mutexAcquired = gcvFALSE;

    gcmkHEADER_ARG("Node=0x%x", Node);

    /* Verify the arguments. */
    if ((Node == gcvNULL)
    ||  (Node->VidMem.memory == gcvNULL)
    )
    {
        /* Invalid object. */
        gcmkONERROR(gcvSTATUS_INVALID_OBJECT);
    }

    /**************************** Video Memory ********************************/

    if (Node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
    {
        /* Extract pointer to gckVIDMEM object owning the node. */
        memory = Node->VidMem.memory;

        /* Acquire the mutex. */
        gcmkONERROR(
            gckOS_AcquireMutex(memory->os, memory->mutex, gcvINFINITE));

        mutexAcquired = gcvTRUE;

#ifdef __QNXNTO__
        /* Unmap the video memory. */
        if (Node->VidMem.logical != gcvNULL)
        {
            gckKERNEL_UnmapVideoMemory(
                    Kernel,
                    Node->VidMem.logical,
                    Node->VidMem.processID,
                    Node->VidMem.bytes);
            Node->VidMem.logical = gcvNULL;
        }

        /* Reset. */
        Node->VidMem.processID = 0;

        /* Don't try to re-free an already freed node. */
        if ((Node->VidMem.nextFree == gcvNULL)
        &&  (Node->VidMem.prevFree == gcvNULL)
        )
#endif
        {
#if gcdENABLE_VG
            if (Node->VidMem.kernelVirtual)
            {
                gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                        "%s(%d) Unmap %x from kernel space.",
                        __FUNCTION__, __LINE__,
                        Node->VidMem.kernelVirtual);

                gcmkVERIFY_OK(
                    gckOS_UnmapPhysical(memory->os,
                                        Node->VidMem.kernelVirtual,
                                        Node->VidMem.bytes));

                Node->VidMem.kernelVirtual = gcvNULL;
            }
#endif

            /* Check if Node is already freed. */
            if (Node->VidMem.nextFree)
            {
                /* Node is alread freed. */
                gcmkONERROR(gcvSTATUS_INVALID_DATA);
            }

            /* Update the number of free bytes. */
            memory->freeBytes += Node->VidMem.bytes;

            /* Find the next free node. */
            for (node = Node->VidMem.next;
                 node != gcvNULL && node->VidMem.nextFree == gcvNULL;
                 node = node->VidMem.next) ;

            /* Insert this node in the free list. */
            Node->VidMem.nextFree = node;
            Node->VidMem.prevFree = node->VidMem.prevFree;

            Node->VidMem.prevFree->VidMem.nextFree =
            node->VidMem.prevFree                  = Node;

            /* Is the next node a free node and not the sentinel? */
            if ((Node->VidMem.next == Node->VidMem.nextFree)
            &&  (Node->VidMem.next->VidMem.bytes != 0)
            )
            {
                /* Merge this node with the next node. */
                gcmkONERROR(_Merge(memory->os, node = Node));
                gcmkASSERT(node->VidMem.nextFree != node);
                gcmkASSERT(node->VidMem.prevFree != node);
            }

            /* Is the previous node a free node and not the sentinel? */
            if ((Node->VidMem.prev == Node->VidMem.prevFree)
            &&  (Node->VidMem.prev->VidMem.bytes != 0)
            )
            {
                /* Merge this node with the previous node. */
                gcmkONERROR(_Merge(memory->os, node = Node->VidMem.prev));
                gcmkASSERT(node->VidMem.nextFree != node);
                gcmkASSERT(node->VidMem.prevFree != node);
            }
        }

        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(memory->os, memory->mutex));

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                       "Node 0x%x is freed.",
                       Node);

        /* Success. */
        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

    /*************************** Virtual Memory *******************************/

    /* Get gckKERNEL object. */
    kernel = Node->Virtual.kernel;

    /* Verify the gckKERNEL object pointer. */
    gcmkVERIFY_OBJECT(kernel, gcvOBJ_KERNEL);

#if gcdENABLE_VG
    if (Node->Virtual.kernelVirtual)
    {
        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                "%s(%d) Unmap %x from kernel space.",
                __FUNCTION__, __LINE__,
                Node->Virtual.kernelVirtual);

        gcmkVERIFY_OK(
            gckOS_UnmapPhysical(kernel->os,
                                Node->Virtual.kernelVirtual,
                                Node->Virtual.bytes));

        Node->Virtual.kernelVirtual = gcvNULL;
    }
#endif

    /* Free the virtual memory. */
    gcmkVERIFY_OK(gckOS_FreePagedMemory(kernel->os,
                                        Node->Virtual.physical,
                                        Node->Virtual.bytes));

    /* Destroy the gcuVIDMEM_NODE union. */
    gcmkVERIFY_OK(gckVIDMEM_DestroyVirtual(Node));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (mutexAcquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(
            memory->os, memory->mutex
            ));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

#if !gcdPROCESS_ADDRESS_SPACE
/*******************************************************************************
**
** _NeedVirtualMapping
**
**  Whether setup GPU page table for video node.
**
**  INPUT:
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      gcuVIDMEM_NODE_PTR Node
**          Pointer to a gcuVIDMEM_NODE union.
**
**      gceCORE  Core
**          Id of current GPU.
**
**  OUTPUT:
**      gctBOOL * NeedMapping
**          A pointer hold the result whether Node should be mapping.
*/
static gceSTATUS
_NeedVirtualMapping(
    IN gckKERNEL Kernel,
    IN gceCORE  Core,
    IN gcuVIDMEM_NODE_PTR Node,
    OUT gctBOOL * NeedMapping
)
{
    gceSTATUS status;
    gctPHYS_ADDR_T phys;
    gctUINT32 address;
    gctUINT32 end;
    gcePOOL pool;
    gctUINT32 offset;
    gctUINT32 bytes;

    gcmkHEADER_ARG("Node=0x%X", Node);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Kernel != gcvNULL);
    gcmkVERIFY_ARGUMENT(Node != gcvNULL);
    gcmkVERIFY_ARGUMENT(NeedMapping != gcvNULL);
    gcmkVERIFY_ARGUMENT(Core < gcdMAX_GPU_COUNT);

    if (Node->Virtual.contiguous)
    {
#if gcdENABLE_VG
        if (Core == gcvCORE_VG)
        {
            *NeedMapping = gcvFALSE;
        }
        else
#endif
        if (Node->Virtual.secure)
        {
            *NeedMapping = gcvTRUE;
        }
        else
        {
            /* Convert logical address into a physical address. */
            gcmkONERROR(gckOS_UserLogicalToPhysical(
                Kernel->os, Node->Virtual.logical, &phys
                ));

            gcmkSAFECASTPHYSADDRT(address, phys);

            if (!gckHARDWARE_IsFeatureAvailable(Kernel->hardware, gcvFEATURE_MMU))
            {
                gcmkASSERT(address >= Kernel->hardware->baseAddress);

                /* Subtract baseAddress to get a GPU address used for programming. */
                address -= Kernel->hardware->baseAddress;

                /* If part of region is belong to gcvPOOL_VIRTUAL,
                ** whole region has to be mapped. */
                gcmkSAFECASTSIZET(bytes, Node->Virtual.bytes);
                end = address + bytes - 1;

                gcmkONERROR(gckHARDWARE_SplitMemory(
                            Kernel->hardware, end, &pool, &offset
                            ));

                *NeedMapping = (pool == gcvPOOL_VIRTUAL);
            }
            /* no flat mapping for 4G above address */
            else if (phys >> 32)
            {
                *NeedMapping = gcvTRUE;
            }
            else
            {
                gctBOOL flatMapped;

                gcmkONERROR(gckMMU_IsFlatMapped(Kernel->mmu, address, &flatMapped));

                *NeedMapping = !flatMapped;
            }
        }
    }
    else
    {
        *NeedMapping = gcvTRUE;
    }

    gcmkFOOTER_ARG("*NeedMapping=%d", *NeedMapping);
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}
#endif

#if gcdPROCESS_ADDRESS_SPACE
gcsGPU_MAP_PTR
_FindGPUMap(
    IN gcsGPU_MAP_PTR Head,
    IN gctINT ProcessID
    )
{
    gcsGPU_MAP_PTR map = Head;

    while (map)
    {
        if (map->pid == ProcessID)
        {
            return map;
        }

        map = map->next;
    }

    return gcvNULL;
}

gcsGPU_MAP_PTR
_CreateGPUMap(
    IN gckOS Os,
    IN gcsGPU_MAP_PTR *Head,
    IN gcsGPU_MAP_PTR *Tail,
    IN gctINT ProcessID
    )
{
    gcsGPU_MAP_PTR gpuMap;
    gctPOINTER pointer = gcvNULL;

    gckOS_Allocate(Os, sizeof(gcsGPU_MAP), &pointer);

    if (pointer == gcvNULL)
    {
        return gcvNULL;
    }

    gpuMap = pointer;

    gckOS_ZeroMemory(pointer, sizeof(gcsGPU_MAP));

    gpuMap->pid = ProcessID;

    if (!*Head)
    {
        *Head = *Tail = gpuMap;
    }
    else
    {
        gpuMap->prev = *Tail;
        (*Tail)->next = gpuMap;
        *Tail = gpuMap;
    }

    return gpuMap;
}

void
_DestroyGPUMap(
    IN gckOS Os,
    IN gcsGPU_MAP_PTR *Head,
    IN gcsGPU_MAP_PTR *Tail,
    IN gcsGPU_MAP_PTR gpuMap
    )
{

    if (gpuMap == *Head)
    {
        if ((*Head = gpuMap->next) == gcvNULL)
        {
            *Tail = gcvNULL;
        }
    }
    else
    {
        gpuMap->prev->next = gpuMap->next;
        if (gpuMap == *Tail)
        {
            *Tail = gpuMap->prev;
        }
        else
        {
            gpuMap->next->prev = gpuMap->prev;
        }
    }

    gcmkOS_SAFE_FREE(Os, gpuMap);
}
#endif

/*******************************************************************************
**
**  gckVIDMEM_Lock
**
**  Lock a video memory node and return its hardware specific address.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      gcuVIDMEM_NODE_PTR Node
**          Pointer to a gcuVIDMEM_NODE union.
**
**  OUTPUT:
**
**      gctUINT32 * Address
**          Pointer to a variable that will hold the hardware specific address.
**
**      gctUINT32 * PhysicalAddress
**          Pointer to a variable that will hold the bus address of a contiguous
**          video node.
*/
gceSTATUS
gckVIDMEM_Lock(
    IN gckKERNEL Kernel,
    IN gckVIDMEM_NODE Node,
    IN gctBOOL Cacheable,
    OUT gctUINT32 * Address,
    OUT gctUINT32 * Gid,
    OUT gctUINT64 * PhysicalAddress
    )
{
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;
    gctBOOL locked = gcvFALSE;
    gckOS os = gcvNULL;
#if !gcdPROCESS_ADDRESS_SPACE
    gctBOOL needMapping = gcvFALSE;
#endif
    gctUINT64 physicalAddress;
    gcuVIDMEM_NODE_PTR node = Node->node;
    gctPHYS_ADDR_T physical;
    gctSIZE_T pageSize;
    gctUINT32 pageMask;

    gcmkHEADER_ARG("Node=0x%x", Node);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);

    /* Extract the gckOS object pointer. */
    os = Kernel->os;
    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    if ((node == gcvNULL)
    ||  (node->VidMem.memory == gcvNULL)
    )
    {
        /* Invalid object. */
        gcmkONERROR(gcvSTATUS_INVALID_OBJECT);
    }

    /* Grab the mutex. */
    gcmkONERROR(gckOS_AcquireMutex(os, Node->mutex, gcvINFINITE));
    acquired = gcvTRUE;

    /**************************** Video Memory ********************************/

    if (node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
    {
        gctUINT32 offset;

        if (Cacheable == gcvTRUE)
        {
            gcmkONERROR(gcvSTATUS_INVALID_REQUEST);
        }

        /* Increment the lock count. */
        node->VidMem.locked ++;

        /* Return the physical address of the node. */
        gcmkSAFECASTSIZET(offset, node->VidMem.offset);

        *Address = node->VidMem.memory->baseAddress
                 + offset
                 + node->VidMem.alignment;

        physicalAddress = *Address;

        /* Get hardware specific address. */
#if gcdENABLE_VG
        if (Kernel->vg == gcvNULL)
#endif
        {
            if (Kernel->hardware->mmuVersion == 0)
            {
                /* Convert physical to GPU address for old mmu. */
                gcmkASSERT(*Address > Kernel->hardware->baseAddress);
                *Address -= Kernel->hardware->baseAddress;
            }
        }

        gcmkVERIFY_OK(gckOS_CPUPhysicalToGPUPhysical(
            Kernel->os,
            *Address,
            &physical
            ));

        gcmkSAFECASTSIZET(*Address, physical);

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                      "Locked node 0x%x (%d) @ 0x%08X",
                      node,
                      node->VidMem.locked,
                      *Address);
    }

    /*************************** Virtual Memory *******************************/

    else
    {

        *Gid = node->Virtual.gid;

#if gcdPAGED_MEMORY_CACHEABLE
        /* Force video memory cacheable. */
        Cacheable = gcvTRUE;
#endif

        gcmkONERROR(
            gckOS_LockPages(os,
                            node->Virtual.physical,
                            node->Virtual.bytes,
                            Cacheable,
                            &node->Virtual.logical,
                            &node->Virtual.pageCount));

        gcmkONERROR(gckOS_UserLogicalToPhysical(
            os,
            node->Virtual.logical,
            &physicalAddress
            ));

#if gcdENABLE_VG
        node->Virtual.physicalAddress = physicalAddress;
#endif

#if !gcdPROCESS_ADDRESS_SPACE
        /* Increment the lock count. */
        if (node->Virtual.lockeds[Kernel->core] ++ == 0)
        {
            locked = gcvTRUE;

            gcmkONERROR(_NeedVirtualMapping(Kernel, Kernel->core, node, &needMapping));

            if (needMapping == gcvFALSE)
            {
                /* Get hardware specific address. */
#if gcdENABLE_VG
                if (Kernel->vg != gcvNULL)
                {
                    gcmkONERROR(gckVGHARDWARE_ConvertLogical(
                                Kernel->vg->hardware,
                                node->Virtual.logical,
                                gcvTRUE,
                                &node->Virtual.addresses[Kernel->core]));
                }
                else
#endif
                {
                    gcmkONERROR(gckHARDWARE_ConvertLogical(
                                Kernel->hardware,
                                node->Virtual.logical,
                                gcvTRUE,
                                &node->Virtual.addresses[Kernel->core]));
                }
            }
            else
            {
#if gcdSECURITY
                gctPHYS_ADDR physicalArrayPhysical;
                gctPOINTER physicalArrayLogical;

                gcmkONERROR(gckOS_AllocatePageArray(
                    os,
                    node->Virtual.physical,
                    node->Virtual.pageCount,
                    &physicalArrayLogical,
                    &physicalArrayPhysical
                    ));

                gcmkONERROR(gckKERNEL_SecurityMapMemory(
                    Kernel,
                    physicalArrayLogical,
                    node->Virtual.pageCount,
                    &node->Virtual.addresses[Kernel->core]
                    ));

                gcmkONERROR(gckOS_FreeNonPagedMemory(
                    os,
                    1,
                    physicalArrayPhysical,
                    physicalArrayLogical
                    ));
#else
#if gcdENABLE_VG
                if (Kernel->vg != gcvNULL)
                {
                    /* Allocate pages inside the MMU. */
                    gcmkONERROR(
                        gckVGMMU_AllocatePages(Kernel->vg->mmu,
                                             node->Virtual.pageCount,
                                             &node->Virtual.pageTables[Kernel->core],
                                             &node->Virtual.addresses[Kernel->core]));
                }
                else
#endif
                {
                    /* Allocate pages inside the MMU. */
                    gcmkONERROR(
                        gckMMU_AllocatePagesEx(Kernel->mmu,
                                             node->Virtual.pageCount,
                                             node->Virtual.type,
                                             node->Virtual.secure,
                                             &node->Virtual.pageTables[Kernel->core],
                                             &node->Virtual.addresses[Kernel->core]));
                }

#if gcdENABLE_TRUST_APPLICATION
#if gcdENABLE_VG
                if (Kernel->core != gcvCORE_VG && Kernel->hardware->secureMode == gcvSECURE_IN_TA)
#else
                if (Kernel->hardware->secureMode == gcvSECURE_IN_TA)
#endif
                {
                    gcmkONERROR(gckKERNEL_MapInTrustApplicaiton(
                        Kernel,
                        node->Virtual.logical,
                        node->Virtual.physical,
                        node->Virtual.addresses[Kernel->core],
                        node->Virtual.pageCount
                        ));
                }
                else
#endif
                {
                /* Map the pages. */
                gcmkONERROR(
                    gckOS_MapPagesEx(os,
                                     Kernel->core,
                                     node->Virtual.physical,
                                     node->Virtual.pageCount,
                                     node->Virtual.addresses[Kernel->core],
                                     node->Virtual.pageTables[Kernel->core],
                                     gcvTRUE,
                                     node->Virtual.type));
                }

#if gcdENABLE_VG
                if (Kernel->core == gcvCORE_VG)
                {
                    gcmkONERROR(gckVGMMU_Flush(Kernel->vg->mmu));
                }
                else
#endif
                {
                    gcmkONERROR(gckMMU_Flush(Kernel->mmu, node->Virtual.type));
                }
#endif
            }
            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                           "Mapped virtual node 0x%x to 0x%08X",
                           node,
                           node->Virtual.addresses[Kernel->core]);
        }

        /* Return hardware address. */
        *Address = node->Virtual.addresses[Kernel->core];

        if (needMapping == gcvTRUE)
        {

#if gcdENABLE_VG
            if (Kernel->core == gcvCORE_VG)
            {
                gcmkVERIFY_OK(gckOS_GetPageSize(os, &pageSize));
            }
            else
#endif
            {
                pageSize = Kernel->command->pageSize;
            }

            pageMask = (gctUINT32)pageSize - 1;

            *Address += (gctUINT32)physicalAddress & pageMask;
        }
#endif
    }

    /* Release the mutex. */
    gcmkVERIFY_OK(gckOS_ReleaseMutex(os, Node->mutex));

    *PhysicalAddress = (gctUINT64)physicalAddress;

    /* Success. */
    gcmkFOOTER_ARG("*Address=%08x", *Address);
    return gcvSTATUS_OK;

OnError:
    if (locked)
    {
        if (node->Virtual.pageTables[Kernel->core] != gcvNULL)
        {
#if gcdENABLE_VG
            if (Kernel->vg != gcvNULL)
            {
                /* Free the pages from the MMU. */
                gcmkVERIFY_OK(
                    gckVGMMU_FreePages(Kernel->vg->mmu,
                                     node->Virtual.pageTables[Kernel->core],
                                     node->Virtual.pageCount));
            }
            else
#endif
            {
                /* Free the pages from the MMU. */
                gcmkVERIFY_OK(
                    gckMMU_FreePages(Kernel->mmu,
                                     node->Virtual.secure,
                                     node->Virtual.addresses[Kernel->core],
                                     node->Virtual.pageTables[Kernel->core],
                                     node->Virtual.pageCount));
            }
            node->Virtual.pageTables[Kernel->core]  = gcvNULL;
        }

        /* Unlock the pages. */
        gcmkVERIFY_OK(
            gckOS_UnlockPages(os,
                              node->Virtual.physical,
                              node->Virtual.bytes,
                              node->Virtual.logical
                              ));

        node->Virtual.lockeds[Kernel->core]--;
    }

    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(os, Node->mutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckVIDMEM_Unlock
**
**  Unlock a video memory node.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      gcuVIDMEM_NODE_PTR Node
**          Pointer to a locked gcuVIDMEM_NODE union.
**
**      gceSURF_TYPE Type
**          Type of surface to unlock.
**
**      gctBOOL * Asynchroneous
**          Pointer to a variable specifying whether the surface should be
**          unlocked asynchroneously or not.
**
**  OUTPUT:
**
**      gctBOOL * Asynchroneous
**          Pointer to a variable receiving the number of bytes used in the
**          command buffer specified by 'Commands'.  If gcvNULL, there is no
**          command buffer.
*/
gceSTATUS
gckVIDMEM_Unlock(
    IN gckKERNEL Kernel,
    IN gckVIDMEM_NODE Node,
    IN gceSURF_TYPE Type,
    IN OUT gctBOOL * Asynchroneous
    )
{
    gceSTATUS status;
    gckOS os = gcvNULL;
    gctBOOL acquired = gcvFALSE;
    gcuVIDMEM_NODE_PTR node = Node->node;

    gcmkHEADER_ARG("Node=0x%x Type=%d *Asynchroneous=%d",
                   Node, Type, gcmOPT_VALUE(Asynchroneous));

    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);

    /* Get the gckOS object pointer. */
    os = Kernel->os;
    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    /* Verify the arguments. */
    if ((node == gcvNULL)
    ||  (node->VidMem.memory == gcvNULL)
    )
    {
        /* Invalid object. */
        gcmkONERROR(gcvSTATUS_INVALID_OBJECT);
    }

    /* Grab the mutex. */
    gcmkONERROR(gckOS_AcquireMutex(os, Node->mutex, gcvINFINITE));
    acquired = gcvTRUE;

    /**************************** Video Memory ********************************/

    if (node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
    {
        if (node->VidMem.locked <= 0)
        {
            /* The surface was not locked. */
            status = gcvSTATUS_MEMORY_UNLOCKED;
            goto OnError;
        }

        if (Asynchroneous != gcvNULL)
        {
            /* Schedule an event to sync with GPU. */
            *Asynchroneous = gcvTRUE;
        }
        else
        {
            /* Decrement the lock count. */
            node->VidMem.locked --;
        }

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                      "Unlocked node 0x%x (%d)",
                      node,
                      node->VidMem.locked);
    }

    /*************************** Virtual Memory *******************************/

    else
    {


        if (Asynchroneous == gcvNULL)
        {
#if !gcdPROCESS_ADDRESS_SPACE
            if (node->Virtual.lockeds[Kernel->core] == 0)
            {
                status = gcvSTATUS_MEMORY_UNLOCKED;
                goto OnError;
            }

            /* Decrement lock count. */
            -- node->Virtual.lockeds[Kernel->core];

            /* See if we can unlock the resources. */
            if (node->Virtual.lockeds[Kernel->core] == 0)
            {
#if gcdSECURITY
                if (node->Virtual.addresses[Kernel->core] > 0x80000000)
                {
                    gcmkONERROR(gckKERNEL_SecurityUnmapMemory(
                        Kernel,
                        node->Virtual.addresses[Kernel->core],
                        node->Virtual.pageCount
                        ));
                }
#else
                /* Free the page table. */
                if (node->Virtual.pageTables[Kernel->core] != gcvNULL)
                {
#if gcdENABLE_VG
                    if (Kernel->vg != gcvNULL)
                    {
                        gcmkONERROR(
                            gckVGMMU_FreePages(Kernel->vg->mmu,
                                             node->Virtual.pageTables[Kernel->core],
                                             node->Virtual.pageCount));
                    }
                    else
#endif
                    {
                        gcmkONERROR(
                            gckMMU_FreePages(Kernel->mmu,
                                             node->Virtual.secure,
                                             node->Virtual.addresses[Kernel->core],
                                             node->Virtual.pageTables[Kernel->core],
                                             node->Virtual.pageCount));
                    }

                    gcmkONERROR(gckOS_UnmapPages(
                        Kernel->os,
                        node->Virtual.pageCount,
                        node->Virtual.addresses[Kernel->core]
                        ));

                    /* Mark page table as freed. */
                    node->Virtual.pageTables[Kernel->core] = gcvNULL;
                }
#endif
            }

            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                           "Unmapped virtual node 0x%x from 0x%08X",
                           node, node->Virtual.addresses[Kernel->core]);
#endif

        }

        else
        {
            gcmkONERROR(
                gckOS_UnlockPages(os,
                              node->Virtual.physical,
                              node->Virtual.bytes,
                              node->Virtual.logical));

            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                           "Scheduled unlock for virtual node 0x%x",
                           node);

            /* Schedule the surface to be unlocked. */
            *Asynchroneous = gcvTRUE;
        }
    }

    /* Release the mutex. */
    gcmkVERIFY_OK(gckOS_ReleaseMutex(os, Node->mutex));
    acquired = gcvFALSE;

    /* Success. */
    gcmkFOOTER_ARG("*Asynchroneous=%d", gcmOPT_VALUE(Asynchroneous));
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(os, Node->mutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

#if gcdPROCESS_ADDRESS_SPACE
gceSTATUS
gckVIDMEM_Node_Lock(
    IN gckKERNEL Kernel,
    IN gckVIDMEM_NODE Node,
    OUT gctUINT32 *Address
    )
{
    gceSTATUS           status;
    gckOS               os;
    gcuVIDMEM_NODE_PTR  node = Node->node;
    gcsGPU_MAP_PTR      gpuMap;
    gctPHYS_ADDR        physical = gcvNULL;
    gctUINT32           phys = gcvINVALID_ADDRESS;
    gctUINT32           processID;
    gcsLOCK_INFO_PTR    lockInfo;
    gctUINT32           pageCount;
    gckMMU              mmu;
    gctUINT32           i;
    gctUINT32_PTR       pageTableEntry;
    gctUINT32           offset = 0;
    gctBOOL             acquired = gcvFALSE;

    gcmkHEADER_ARG("Node = %x", Node);

    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(Node != gcvNULL);
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    os = Kernel->os;
    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    gcmkONERROR(gckOS_GetProcessID(&processID));

    gcmkONERROR(gckKERNEL_GetProcessMMU(Kernel, &mmu));

    gcmkONERROR(gckOS_AcquireMutex(os, Node->mapMutex, gcvINFINITE));
    acquired = gcvTRUE;

    /* Get map information for current process. */
    gpuMap = _FindGPUMap(Node->mapHead, processID);

    if (gpuMap == gcvNULL)
    {
        gpuMap = _CreateGPUMap(os, &Node->mapHead, &Node->mapTail, processID);

        if (gpuMap == gcvNULL)
        {
            gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
        }
    }

    lockInfo = &gpuMap->lockInfo;

    if (lockInfo->lockeds[Kernel->core] ++ == 0)
    {
        /* Get necessary information. */
        if (node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
        {
            phys = node->VidMem.memory->baseAddress
                 + node->VidMem.offset
                 + node->VidMem.alignment;

            /* GPU page table use 4K page. */
            pageCount = ((phys + node->VidMem.bytes + 4096 - 1) >> 12)
                      - (phys >> 12);

            offset = phys & 0xFFF;
        }
        else
        {
            pageCount = node->Virtual.pageCount;
            physical = node->Virtual.physical;
        }

        /* Allocate pages inside the MMU. */
        gcmkONERROR(gckMMU_AllocatePages(
            mmu,
            pageCount,
            &lockInfo->pageTables[Kernel->core],
            &lockInfo->GPUAddresses[Kernel->core]));

        /* Record MMU from which pages are allocated.  */
        lockInfo->lockMmus[Kernel->core] = mmu;

        pageTableEntry = lockInfo->pageTables[Kernel->core];

        /* Fill page table entries. */
        if (phys != gcvINVALID_ADDRESS)
        {
            gctUINT32 address = lockInfo->GPUAddresses[Kernel->core];
            for (i = 0; i < pageCount; i++)
            {
                gckMMU_GetPageEntry(mmu, address, &pageTableEntry);
                gckMMU_SetPage(mmu, phys & 0xFFFFF000, pageTableEntry);
                phys += 4096;
                address += 4096;
                pageTableEntry += 1;
            }
        }
        else
        {
            gctUINT32 address = lockInfo->GPUAddresses[Kernel->core];
            gcmkASSERT(physical != gcvNULL);
            gcmkONERROR(gckOS_MapPagesEx(os,
                Kernel->core,
                physical,
                pageCount,
                address,
                pageTableEntry));
        }

        gcmkONERROR(gckMMU_Flush(mmu, Node->type));
    }

    *Address = lockInfo->GPUAddresses[Kernel->core] + offset;

    gcmkVERIFY_OK(gckOS_ReleaseMutex(os, Node->mapMutex));
    acquired = gcvFALSE;


    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(os, Node->mapMutex));
    }

    gcmkFOOTER();
    return status;
}

gceSTATUS
gckVIDMEM_NODE_Unlock(
    IN gckKERNEL Kernel,
    IN gckVIDMEM_NODE Node,
    IN gctUINT32 ProcessID
    )
{
    gceSTATUS           status;
    gcsGPU_MAP_PTR      gpuMap;
    gcsLOCK_INFO_PTR    lockInfo;
    gckMMU              mmu;
    gcuVIDMEM_NODE_PTR  node;
    gctUINT32           pageCount;
    gctBOOL             acquired = gcvFALSE;

    gcmkHEADER_ARG("Kernel=0x%08X, Node = %x, ProcessID=%d",
                   Kernel, Node, ProcessID);

    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(Node != gcvNULL);

    gcmkONERROR(gckOS_AcquireMutex(Kernel->os, Node->mapMutex, gcvINFINITE));
    acquired = gcvTRUE;

    /* Get map information for current process. */
    gpuMap = _FindGPUMap(Node->mapHead, ProcessID);

    if (gpuMap == gcvNULL)
    {
        /* No mapping for this process. */
        gcmkONERROR(gcvSTATUS_INVALID_DATA);
    }

    lockInfo = &gpuMap->lockInfo;

    if (--lockInfo->lockeds[Kernel->core] == 0)
    {
        node = Node->node;

        /* Get necessary information. */
        if (node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
        {
            gctUINT32 phys = node->VidMem.memory->baseAddress
                           + node->VidMem.offset
                           + node->VidMem.alignment;

            /* GPU page table use 4K page. */
            pageCount = ((phys + node->VidMem.bytes + 4096 - 1) >> 12)
                      - (phys >> 12);
        }
        else
        {
            pageCount = node->Virtual.pageCount;
        }

        /* Get MMU which allocates pages. */
        mmu = lockInfo->lockMmus[Kernel->core];

        /* Free virtual spaces in page table. */
        gcmkVERIFY_OK(gckMMU_FreePagesEx(
            mmu,
            lockInfo->GPUAddresses[Kernel->core],
            pageCount
            ));

        _DestroyGPUMap(Kernel->os, &Node->mapHead, &Node->mapTail, gpuMap);
    }

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, Node->mapMutex));
    acquired = gcvFALSE;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, Node->mapMutex));
    }

    gcmkFOOTER();
    return status;
}
#endif

/*******************************************************************************
**
**  gckVIDMEM_HANDLE_Allocate
**
**  Allocate a handle for a gckVIDMEM_NODE object.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      gckVIDMEM_NODE Node
**          Pointer to a gckVIDMEM_NODE object.
**
**  OUTPUT:
**
**      gctUINT32 * Handle
**          Pointer to a variable receiving a handle represent this
**          gckVIDMEM_NODE in userspace.
*/
static gceSTATUS
gckVIDMEM_HANDLE_Allocate(
    IN gckKERNEL Kernel,
    IN gckVIDMEM_NODE Node,
    OUT gctUINT32 * Handle
    )
{
    gceSTATUS status;
    gctUINT32 processID           = 0;
    gctPOINTER pointer            = gcvNULL;
    gctPOINTER handleDatabase     = gcvNULL;
    gctPOINTER mutex              = gcvNULL;
    gctUINT32 handle              = 0;
    gckVIDMEM_HANDLE handleObject = gcvNULL;
    gckOS os                      = Kernel->os;

    gcmkHEADER_ARG("Kernel=0x%X, Node=0x%X", Kernel, Node);

    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    /* Allocate a gckVIDMEM_HANDLE object. */
    gcmkONERROR(gckOS_Allocate(os, gcmSIZEOF(gcsVIDMEM_HANDLE), &pointer));

    gcmkVERIFY_OK(gckOS_ZeroMemory(pointer, gcmSIZEOF(gcsVIDMEM_HANDLE)));

    handleObject = pointer;

    gcmkONERROR(gckOS_AtomConstruct(os, &handleObject->reference));

    /* Set default reference count to 1. */
    gckOS_AtomSet(os, handleObject->reference, 1);

    gcmkVERIFY_OK(gckOS_GetProcessID(&processID));

    gcmkONERROR(
        gckKERNEL_FindHandleDatbase(Kernel,
                                    processID,
                                    &handleDatabase,
                                    &mutex));

    /* Allocate a handle for this object. */
    gcmkONERROR(
        gckKERNEL_AllocateIntegerId(handleDatabase, handleObject, &handle));

    handleObject->node = Node;
    handleObject->handle = handle;

    *Handle = handle;

    gcmkFOOTER_ARG("*Handle=%d", *Handle);
    return gcvSTATUS_OK;

OnError:
    if (handleObject != gcvNULL)
    {
        if (handleObject->reference != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_AtomDestroy(os, handleObject->reference));
        }

        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(os, handleObject));
    }

    gcmkFOOTER();
    return status;
}

static gceSTATUS
gckVIDMEM_NODE_Reference(
    IN gckKERNEL Kernel,
    IN gckVIDMEM_NODE Node
    )
{
    gctINT32 oldValue;
    gcmkHEADER_ARG("Kernel=0x%X Node=0x%X", Kernel, Node);

    gckOS_AtomIncrement(Kernel->os, Node->reference, &oldValue);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

gceSTATUS
gckVIDMEM_HANDLE_Reference(
    IN gckKERNEL Kernel,
    IN gctUINT32 ProcessID,
    IN gctUINT32 Handle
    )
{
    gceSTATUS status;
    gckVIDMEM_HANDLE handleObject = gcvNULL;
    gctPOINTER database           = gcvNULL;
    gctPOINTER mutex              = gcvNULL;
    gctINT32 oldValue             = 0;
    gctBOOL acquired              = gcvFALSE;

    gcmkHEADER_ARG("Handle=%d PrcoessID=%d", Handle, ProcessID);

    gcmkONERROR(
        gckKERNEL_FindHandleDatbase(Kernel, ProcessID, &database, &mutex));

    gcmkVERIFY_OK(gckOS_AcquireMutex(Kernel->os, mutex, gcvINFINITE));
    acquired = gcvTRUE;

    /* Translate handle to gckVIDMEM_HANDLE object. */
    gcmkONERROR(
        gckKERNEL_QueryIntegerId(database, Handle, (gctPOINTER *)&handleObject));

    /* Increase the reference count. */
    gckOS_AtomIncrement(Kernel->os, handleObject->reference, &oldValue);

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, mutex));
    acquired = gcvFALSE;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, mutex));
    }

    gcmkFOOTER();
    return status;
}

gceSTATUS
gckVIDMEM_HANDLE_Dereference(
    IN gckKERNEL Kernel,
    IN gctUINT32 ProcessID,
    IN gctUINT32 Handle
    )
{
    gceSTATUS status;
    gctPOINTER handleDatabase     = gcvNULL;
    gctPOINTER mutex              = gcvNULL;
    gctINT32 oldValue             = 0;
    gckVIDMEM_HANDLE handleObject = gcvNULL;
    gctBOOL acquired              = gcvFALSE;

    gcmkHEADER_ARG("Handle=%d PrcoessID=%d", Handle, ProcessID);

    gcmkONERROR(
        gckKERNEL_FindHandleDatbase(Kernel,
                                    ProcessID,
                                    &handleDatabase,
                                    &mutex));

    gcmkVERIFY_OK(gckOS_AcquireMutex(Kernel->os, mutex, gcvINFINITE));
    acquired = gcvTRUE;

    /* Translate handle to gckVIDMEM_HANDLE. */
    gcmkONERROR(
        gckKERNEL_QueryIntegerId(handleDatabase, Handle, (gctPOINTER *)&handleObject));

    gckOS_AtomDecrement(Kernel->os, handleObject->reference, &oldValue);

    if (oldValue == 1)
    {
        /* Remove handle from database if this is the last reference. */
        gcmkVERIFY_OK(gckKERNEL_FreeIntegerId(handleDatabase, Handle));
    }

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, mutex));
    acquired = gcvFALSE;

    if (oldValue == 1)
    {
        gcmkVERIFY_OK(gckOS_AtomDestroy(Kernel->os, handleObject->reference));
        gcmkOS_SAFE_FREE(Kernel->os, handleObject);
    }

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, mutex));
    }

    gcmkFOOTER();
    return status;
}

gceSTATUS
gckVIDMEM_HANDLE_LookupAndReference(
    IN gckKERNEL Kernel,
    IN gctUINT32 Handle,
    OUT gckVIDMEM_NODE * Node
    )
{
    gceSTATUS status;
    gckVIDMEM_HANDLE handleObject = gcvNULL;
    gckVIDMEM_NODE node           = gcvNULL;
    gctPOINTER database           = gcvNULL;
    gctPOINTER mutex              = gcvNULL;
    gctUINT32 processID           = 0;
    gctBOOL acquired              = gcvFALSE;

    gcmkHEADER_ARG("Kernel=0x%X Handle=%d", Kernel, Handle);

    gckOS_GetProcessID(&processID);

    gcmkONERROR(
        gckKERNEL_FindHandleDatbase(Kernel, processID, &database, &mutex));

    gcmkVERIFY_OK(gckOS_AcquireMutex(Kernel->os, mutex, gcvINFINITE));
    acquired = gcvTRUE;

    /* Translate handle to gckVIDMEM_HANDLE object. */
    gcmkONERROR(
        gckKERNEL_QueryIntegerId(database, Handle, (gctPOINTER *)&handleObject));

    /* Get gckVIDMEM_NODE object. */
    node = handleObject->node;

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, mutex));
    acquired = gcvFALSE;

    /* Reference this gckVIDMEM_NODE object. */
    gcmkVERIFY_OK(gckVIDMEM_NODE_Reference(Kernel, node));

    /* Return result. */
    *Node = node;

    gcmkFOOTER_ARG("*Node=%d", *Node);
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, mutex));
    }

    gcmkFOOTER();
    return status;
}

gceSTATUS
gckVIDMEM_HANDLE_Lookup(
    IN gckKERNEL Kernel,
    IN gctUINT32 ProcessID,
    IN gctUINT32 Handle,
    OUT gckVIDMEM_NODE * Node
    )
{
    gceSTATUS status;
    gckVIDMEM_HANDLE handleObject = gcvNULL;
    gckVIDMEM_NODE node           = gcvNULL;
    gctPOINTER database           = gcvNULL;
    gctPOINTER mutex              = gcvNULL;
    gctBOOL acquired              = gcvFALSE;

    gcmkHEADER_ARG("Kernel=0x%X ProcessID=%d Handle=%d",
                   Kernel, ProcessID, Handle);

    gcmkONERROR(
        gckKERNEL_FindHandleDatbase(Kernel, ProcessID, &database, &mutex));

    gcmkVERIFY_OK(gckOS_AcquireMutex(Kernel->os, mutex, gcvINFINITE));
    acquired = gcvTRUE;

    gcmkONERROR(
        gckKERNEL_QueryIntegerId(database, Handle, (gctPOINTER *)&handleObject));

    node = handleObject->node;

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, mutex));
    acquired = gcvFALSE;

    *Node = node;

    gcmkFOOTER_ARG("*Node=%d", *Node);
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, mutex));
    }

    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckVIDMEM_NODE_Allocate
**
**  Allocate a gckVIDMEM_NODE object.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      gcuVIDMEM_NODE_PTR Node
**          Pointer to a gcuVIDMEM_NODE union.
**
**  OUTPUT:
**
**      gctUINT32 * Handle
**          Pointer to a variable receiving a handle represent this
**          gckVIDMEM_NODE in userspace.
*/
gceSTATUS
gckVIDMEM_NODE_Allocate(
    IN gckKERNEL Kernel,
    IN gcuVIDMEM_NODE_PTR VideoNode,
    IN gceSURF_TYPE Type,
    IN gcePOOL Pool,
    IN gctUINT32 * Handle
    )
{
    gceSTATUS status;
    gckVIDMEM_NODE node = gcvNULL;
    gctPOINTER pointer  = gcvNULL;
    gctUINT32 handle    = 0;
    gckOS os            = Kernel->os;
    gctUINT i;

    gcmkHEADER_ARG("Kernel=0x%X VideoNode=0x%X", Kernel, VideoNode);

    /* Construct a node. */
    gcmkONERROR(gckOS_Allocate(os, gcmSIZEOF(gcsVIDMEM_NODE), &pointer));

    gcmkVERIFY_OK(gckOS_ZeroMemory(pointer, gcmSIZEOF(gcsVIDMEM_NODE)));

    node = pointer;

    node->node = VideoNode;
    node->type = Type;
    node->pool = Pool;

#if gcdPROCESS_ADDRESS_SPACE
    gcmkONERROR(gckOS_CreateMutex(os, &node->mapMutex));
#endif

    gcmkONERROR(gckOS_AtomConstruct(os, &node->reference));

    gcmkONERROR(gckOS_CreateMutex(os, &node->mutex));

    for (i = 0; i < gcvENGINE_COUNT; i++)
    {
        gcmkONERROR(gckOS_CreateSignal(os, gcvFALSE, &node->sync[i].signal));
    }

    /* Reference is 1 by default . */
    gckVIDMEM_NODE_Reference(Kernel, node);

    /* Create a handle to represent this node. */
    gcmkONERROR(gckVIDMEM_HANDLE_Allocate(Kernel, node, &handle));

    *Handle = handle;

    gcmkFOOTER_ARG("*Handle=%d", *Handle);
    return gcvSTATUS_OK;

OnError:
    if (node != gcvNULL)
    {
#if gcdPROCESS_ADDRESS_SPACE
        if (node->mapMutex != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_DeleteMutex(os, node->mapMutex));
        }
#endif

        if (node->mutex)
        {
            gcmkVERIFY_OK(gckOS_DeleteMutex(os, node->mutex));
        }

        if (node->reference != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_AtomDestroy(os, node->reference));
        }

        for (i = 0; i < gcvENGINE_COUNT; i++)
        {
            if (node->sync[i].signal != gcvNULL)
            {
                gcmkVERIFY_OK(gckOS_DestroySignal(os, node->sync[i].signal));
            }
        }

        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(os, node));
    }

    gcmkFOOTER();
    return status;
}

gceSTATUS
gckVIDMEM_NODE_Dereference(
    IN gckKERNEL Kernel,
    IN gckVIDMEM_NODE Node
    )
{
    gctINT32 oldValue   = 0;
    gctPOINTER database = Kernel->db->nameDatabase;
    gctPOINTER mutex    = Kernel->db->nameDatabaseMutex;
    gctUINT i;

    gcmkHEADER_ARG("Kernel=0x%X Node=0x%X", Kernel, Node);

    gcmkVERIFY_OK(gckOS_AcquireMutex(Kernel->os, mutex, gcvINFINITE));

    gcmkVERIFY_OK(gckOS_AtomDecrement(Kernel->os, Node->reference, &oldValue));

    if (oldValue == 1 && Node->name)
    {
        /* Free name if exists. */
        gcmkVERIFY_OK(gckKERNEL_FreeIntegerId(database, Node->name));
    }

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, mutex));

    if (oldValue == 1)
    {
        /* Free gcuVIDMEM_NODE. */
        gcmkVERIFY_OK(gckVIDMEM_Free(Kernel, Node->node));
        gcmkVERIFY_OK(gckOS_AtomDestroy(Kernel->os, Node->reference));
#if gcdPROCESS_ADDRESS_SPACE
        gcmkVERIFY_OK(gckOS_DeleteMutex(Kernel->os, Node->mapMutex));
#endif
        gcmkVERIFY_OK(gckOS_DeleteMutex(Kernel->os, Node->mutex));

        for (i = 0; i < gcvENGINE_COUNT; i++)
        {
            if (Node->sync[i].signal != gcvNULL)
            {
                gcmkVERIFY_OK(gckOS_DestroySignal(Kernel->os, Node->sync[i].signal));
            }
        }

        gcmkOS_SAFE_FREE(Kernel->os, Node);
    }

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckVIDMEM_NODE_Name
**
**  Naming a gckVIDMEM_NODE object.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      gctUINT32 Handle
**          Handle to a gckVIDMEM_NODE object.
**
**  OUTPUT:
**
**      gctUINT32 * Name
**          Pointer to a variable receiving a name which can be pass to another
**          process.
*/
gceSTATUS
gckVIDMEM_NODE_Name(
    IN gckKERNEL Kernel,
    IN gctUINT32 Handle,
    IN gctUINT32 * Name
    )
{
    gceSTATUS status;
    gckVIDMEM_NODE node = gcvNULL;
    gctUINT32 name      = 0;
    gctUINT32 processID = 0;
    gctPOINTER database = Kernel->db->nameDatabase;
    gctPOINTER mutex    = Kernel->db->nameDatabaseMutex;
    gctBOOL acquired    = gcvFALSE;
    gctBOOL referenced  = gcvFALSE;
    gcmkHEADER_ARG("Kernel=0x%X Handle=%d", Kernel, Handle);

    gcmkVERIFY_ARGUMENT(Name != gcvNULL);

    gcmkONERROR(gckOS_GetProcessID(&processID));

    gcmkONERROR(gckOS_AcquireMutex(Kernel->os, mutex, gcvINFINITE));
    acquired = gcvTRUE;

    gcmkONERROR(gckVIDMEM_HANDLE_LookupAndReference(Kernel, Handle, &node));
    referenced = gcvTRUE;

    if (node->name == 0)
    {
        /* Name this node. */
        gcmkONERROR(gckKERNEL_AllocateIntegerId(database, node, &name));
        node->name = name;
    }

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, mutex));
    acquired = gcvFALSE;

    gcmkVERIFY_OK(gckVIDMEM_NODE_Dereference(Kernel, node));

    *Name = node->name;

    gcmkFOOTER_ARG("*Name=%d", *Name);
    return gcvSTATUS_OK;

OnError:
    if (referenced)
    {
        gcmkVERIFY_OK(gckVIDMEM_NODE_Dereference(Kernel, node));
    }

    if (acquired)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, mutex));
    }

    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckVIDMEM_NODE_Import
**
**  Import a gckVIDMEM_NODE object.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      gctUINT32 Name
**          Name of a gckVIDMEM_NODE object.
**
**  OUTPUT:
**
**      gctUINT32 * Handle
**          Pointer to a variable receiving a handle represent this
**          gckVIDMEM_NODE in userspace.
*/
gceSTATUS
gckVIDMEM_NODE_Import(
    IN gckKERNEL Kernel,
    IN gctUINT32 Name,
    IN gctUINT32 * Handle
    )
{
    gceSTATUS status;
    gckVIDMEM_NODE node = gcvNULL;
    gctPOINTER database = Kernel->db->nameDatabase;
    gctPOINTER mutex    = Kernel->db->nameDatabaseMutex;
    gctBOOL acquired    = gcvFALSE;
    gctBOOL referenced  = gcvFALSE;

    gcmkHEADER_ARG("Kernel=0x%X Name=%d", Kernel, Name);

    gcmkONERROR(gckOS_AcquireMutex(Kernel->os, mutex, gcvINFINITE));
    acquired = gcvTRUE;

    /* Lookup in database to get the node. */
    gcmkONERROR(gckKERNEL_QueryIntegerId(database, Name, (gctPOINTER *)&node));

    /* Reference the node. */
    gcmkONERROR(gckVIDMEM_NODE_Reference(Kernel, node));
    referenced = gcvTRUE;

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, mutex));
    acquired = gcvFALSE;

    /* Allocate a handle for current process. */
    gcmkONERROR(gckVIDMEM_HANDLE_Allocate(Kernel, node, Handle));

    gcmkFOOTER_ARG("*Handle=%d", *Handle);
    return gcvSTATUS_OK;

OnError:
    if (referenced)
    {
        gcmkVERIFY_OK(gckVIDMEM_NODE_Dereference(Kernel, node));
    }

    if (acquired)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, mutex));
    }

    gcmkFOOTER();
    return status;
}


typedef struct _gcsVIDMEM_NODE_FDPRIVATE
{
    gcsFDPRIVATE   base;
    gckKERNEL      kernel;
    gckVIDMEM_NODE node;
}
gcsVIDMEM_NODE_FDPRIVATE;


static gctINT
_ReleaseFdPrivate(
    gcsFDPRIVATE_PTR FdPrivate
    )
{
    /* Cast private info. */
    gcsVIDMEM_NODE_FDPRIVATE * private = (gcsVIDMEM_NODE_FDPRIVATE *) FdPrivate;

    gckVIDMEM_NODE_Dereference(private->kernel, private->node);
    gckOS_Free(private->kernel->os, private);

    return 0;
}


/*******************************************************************************
**
**  gckVIDMEM_NODE_GetFd
**
**  Attach a gckVIDMEM_NODE object to a native fd.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      gctUINT32 Handle
**          Handle to a gckVIDMEM_NODE object.
**
**  OUTPUT:
**
**      gctUINT32 * Fd
**          Pointer to a variable receiving a native fd from os.
*/
gceSTATUS
gckVIDMEM_NODE_GetFd(
    IN gckKERNEL Kernel,
    IN gctUINT32 Handle,
    OUT gctINT * Fd
    )
{
    gceSTATUS status;
    gckVIDMEM_NODE node = gcvNULL;
    gctBOOL referenced  = gcvFALSE;
    gcsVIDMEM_NODE_FDPRIVATE * fdPrivate = gcvNULL;
    gcmkHEADER_ARG("Kernel=0x%X Handle=%d", Kernel, Handle);

    /* Query and reference handle. */
    gcmkONERROR(gckVIDMEM_HANDLE_LookupAndReference(Kernel, Handle, &node));
    referenced = gcvTRUE;

    /* Allocated fd owns a reference. */
    gcmkONERROR(gckOS_Allocate(
        Kernel->os,
        gcmSIZEOF(gcsVIDMEM_NODE_FDPRIVATE),
        (gctPOINTER *)&fdPrivate
        ));

    fdPrivate->base.release = _ReleaseFdPrivate;
    fdPrivate->kernel = Kernel;
    fdPrivate->node = node;

    /* Allocated fd owns a reference. */
    gcmkONERROR(gckOS_GetFd("vidmem", &fdPrivate->base, Fd));

    gcmkFOOTER_ARG("*Fd=%d", *Fd);
    return gcvSTATUS_OK;

OnError:
    if (referenced)
    {
        gcmkVERIFY_OK(gckVIDMEM_NODE_Dereference(Kernel, node));
    }

    if (fdPrivate)
    {
        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Kernel->os, fdPrivate));
    }

    gcmkFOOTER();
    return status;
}

gceSTATUS
gckVIDMEM_ConstructVirtualFromUserMemory(
    IN gckKERNEL Kernel,
    IN gcsUSER_MEMORY_DESC_PTR Desc,
    OUT gcuVIDMEM_NODE_PTR * Node
    )
{
    gckOS os;
    gceSTATUS status;
    gcuVIDMEM_NODE_PTR node = gcvNULL;
    gctPOINTER pointer = gcvNULL;

    gcmkHEADER_ARG("Kernel=0x%x", Kernel);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(Node != gcvNULL);

    /* Extract the gckOS object pointer. */
    os = Kernel->os;
    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    /* Allocate an gcuVIDMEM_NODE union. */
    gcmkONERROR(gckOS_Allocate(os, gcmSIZEOF(gcuVIDMEM_NODE), &pointer));

    gckOS_ZeroMemory(pointer, gcmSIZEOF(gcuVIDMEM_NODE));

    node = pointer;

    /* Initialize gcuVIDMEM_NODE union for virtual memory. */
    node->Virtual.kernel     = Kernel;

    /* Wrap Memory. */
    gcmkONERROR(gckOS_WrapMemory(
        os, Desc, &node->Virtual.bytes, &node->Virtual.physical, &node->Virtual.contiguous));

    /* Return pointer to the gcuVIDMEM_NODE union. */
    *Node = node;

    /* Success. */
    gcmkFOOTER_ARG("*Node=0x%x", *Node);
    return gcvSTATUS_OK;

OnError:
    /* Roll back. */
    if (node != gcvNULL)
    {
        /* Free the structure. */
        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(os, node));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckVIDMEM_SetCommitStamp(
    IN gckKERNEL Kernel,
    IN gceENGINE Engine,
    IN gctUINT32 Handle,
    IN gctUINT64 CommitStamp
    )
{
    gceSTATUS status;
    gckVIDMEM_NODE node;
    gctUINT32 processID;

    gckOS_GetProcessID(&processID);

    gcmkONERROR(gckVIDMEM_HANDLE_LookupAndReference(Kernel, Handle, &node));

    node->sync[Engine].commitStamp = CommitStamp;

    gckVIDMEM_NODE_Dereference(Kernel, node);

    return gcvSTATUS_OK;

OnError:
    return status;
}

gceSTATUS
gckVIDMEM_GetCommitStamp(
    IN gckKERNEL Kernel,
    IN gceENGINE Engine,
    IN gctUINT32 Handle,
    OUT gctUINT64_PTR CommitStamp
    )
{
    gceSTATUS status;
    gckVIDMEM_NODE node;
    gctUINT32 processID;

    gckOS_GetProcessID(&processID);

    gcmkONERROR(gckVIDMEM_HANDLE_LookupAndReference(Kernel, Handle, &node));

    *CommitStamp = node->sync[Engine].commitStamp;

    gckVIDMEM_NODE_Dereference(Kernel, node);

    return gcvSTATUS_OK;

OnError:
    return status;
}

