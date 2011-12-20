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

    /* Make sure the byte boundary makes sense. */
    if ((Bytes <= 0) || (Bytes > Node->VidMem.bytes))
    {
        return gcvFALSE;
    }

    /* Allocate a new gcuVIDMEM_NODE object. */
    if (gcmIS_ERROR(gckOS_Allocate(Os,
                                   gcmSIZEOF(gcuVIDMEM_NODE),
                                   (gctPOINTER *) &node)))
    {
        /* Error. */
        return gcvFALSE;
    }

    /* Initialize gcuVIDMEM_NODE structure. */
    node->VidMem.offset    = Node->VidMem.offset + Bytes;
    node->VidMem.bytes     = Node->VidMem.bytes  - Bytes;
    node->VidMem.alignment = 0;
    node->VidMem.locked    = 0;
    node->VidMem.memory    = Node->VidMem.memory;
    node->VidMem.pool      = Node->VidMem.pool;
    node->VidMem.physical  = Node->VidMem.physical;
#ifdef __QNXNTO__
    node->VidMem.logical   = gcvNULL;
    node->VidMem.handle    = 0;
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
    return gckOS_Free(Os, node);
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
    IN gctBOOL Contiguous,
    IN gctSIZE_T Bytes,
#ifdef __QNXNTO__
    IN gctHANDLE Handle,
#endif
    OUT gcuVIDMEM_NODE_PTR * Node
    )
{
    gckOS os;
    gceSTATUS status;
    gcuVIDMEM_NODE_PTR node = gcvNULL;

    gcmkHEADER_ARG("Kernel=0x%x Bytes=%lu", Kernel, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Node != gcvNULL);
#ifdef __QNXNTO__
    gcmkVERIFY_ARGUMENT(Handle != gcvNULL);
#endif

    /* Extract the gckOS object pointer. */
    os = Kernel->os;
    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    /* Allocate an gcuVIDMEM_NODE union. */
    gcmkONERROR(
        gckOS_Allocate(os, gcmSIZEOF(gcuVIDMEM_NODE), (gctPOINTER *) &node));

    /* Initialize gcuVIDMEM_NODE union for virtual memory. */
    node->Virtual.kernel        = Kernel;
    node->Virtual.contiguous    = Contiguous;
    node->Virtual.locked        = 0;
    node->Virtual.logical       = gcvNULL;
    node->Virtual.pageTable     = gcvNULL;
    node->Virtual.mutex         = gcvNULL;
#ifdef __QNXNTO__
    node->Virtual.next          = gcvNULL;
    node->Virtual.unlockPending = gcvFALSE;
    node->Virtual.freePending   = gcvFALSE;
    node->Virtual.handle        = Handle;
#else
    node->Virtual.pending       = gcvFALSE;
#endif

    /* Create the mutex. */
    gcmkONERROR(
        gckOS_CreateMutex(os, &node->Virtual.mutex));

    /* Allocate the virtual memory. */
    gcmkONERROR(
        gckOS_AllocatePagedMemoryEx(os,
                                    node->Virtual.contiguous,
                                    node->Virtual.bytes = Bytes,
                                    &node->Virtual.physical));

#ifdef __QNXNTO__
    /* Register. */
    gckMMU_InsertNode(Kernel->mmu, node);
#endif

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
        if (node->Virtual.mutex != gcvNULL)
        {
            /* Destroy the mutex. */
            gcmkVERIFY_OK(gckOS_DeleteMutex(os, node->Virtual.mutex));
        }

        /* Free the structure. */
        gcmkVERIFY_OK(gckOS_Free(os, node));
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

#ifdef __QNXNTO__
    /* Unregister. */
    gcmkVERIFY_OK(
            gckMMU_RemoveNode(Node->Virtual.kernel->mmu, Node));

    /* Free virtual memory. */
    gcmkVERIFY_OK(
            gckOS_FreePagedMemory(os,
                                  Node->Virtual.physical,
                                  Node->Virtual.bytes));
#endif

    /* Delete the mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(os, Node->Virtual.mutex));

    if (Node->Virtual.pageTable != gcvNULL)
    {
        /* Free the pages. */
        gcmkVERIFY_OK(gckMMU_FreePages(Node->Virtual.kernel->mmu,
                                       Node->Virtual.pageTable,
                                       Node->Virtual.pageCount));
    }

    /* Delete the gcuVIDMEM_NODE union. */
    gcmkVERIFY_OK(gckOS_Free(os, Node));

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

    gcmkHEADER_ARG("Os=0x%x BaseAddress=%08x Bytes=%lu Threshold=%lu "
                   "BankSize=%lu",
                   Os, BaseAddress, Bytes, Threshold, BankSize);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);

    /* Allocate the gckVIDMEM object. */
    gcmkONERROR(
        gckOS_Allocate(Os,
                       gcmSIZEOF(struct _gckVIDMEM),
                       (gctPOINTER *) &memory));

    /* Initialize the gckVIDMEM object. */
    memory->object.type = gcvOBJ_VIDMEM;
    memory->os          = Os;

    /* Set video memory heap information. */
    memory->baseAddress = BaseAddress;
    memory->bytes       = Bytes;
    memory->freeBytes   = Bytes;
    memory->threshold   = Threshold;
    memory->mutex       = gcvNULL;

    BaseAddress = 0;

    /* Walk all possible banks. */
    for (i = 0; i < gcmCOUNTOF(memory->sentinel); ++i)
    {
        gctSIZE_T bytes;

        if (BankSize == 0)
        {
            /* Use all bytes for the first bank. */
            bytes = Bytes;
        }
        else
        {
            /* Compute number of bytes for this bank. */
            bytes = gcmALIGN(BaseAddress + 1, BankSize) - BaseAddress;

            if (bytes > Bytes)
            {
                /* Make sure we don't exceed the total number of bytes. */
                bytes = Bytes;
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
        gcmkONERROR(
            gckOS_Allocate(Os,
                           gcmSIZEOF(gcuVIDMEM_NODE),
                           (gctPOINTER *) &node));

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
        node->VidMem.logical   = gcvNULL;
        node->VidMem.handle    = 0;
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
        Bytes       -= bytes;
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
            gcmkVERIFY_OK(gckOS_Free(Os, memory->sentinel[i].VidMem.next));
        }

        /* Free the object. */
        gcmkVERIFY_OK(gckOS_Free(Os, memory));
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
            gcmkVERIFY_OK(gckOS_Free(Memory->os, node));
        }
    }

    /* Free the mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Memory->os, Memory->mutex));

    /* Mark the object as unknown. */
    Memory->object.type = gcvOBJ_UNKNOWN;

    /* Free the gckVIDMEM object. */
    gcmkVERIFY_OK(gckOS_Free(Memory->os, Memory));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckVIDMEM_Allocate
**
**  Allocate rectangular memory from the gckVIDMEM object.
**
**  INPUT:
**
**      gckVIDMEM Memory
**          Pointer to an gckVIDMEM object.
**
**      gctUINT Width
**          Width of rectangle to allocate.  Make sure the width is properly
**          aligned.
**
**      gctUINT Height
**          Height of rectangle to allocate.  Make sure the height is properly
**          aligned.
**
**      gctUINT Depth
**          Depth of rectangle to allocate.  This equals to the number of
**          rectangles to allocate contiguously (i.e., for cubic maps and volume
**          textures).
**
**      gctUINT BytesPerPixel
**          Number of bytes per pixel.
**
**      gctUINT32 Alignment
**          Byte alignment for allocation.
**
**      gceSURF_TYPE Type
**          Type of surface to allocate (use by bank optimization).
**
**  OUTPUT:
**
**      gcuVIDMEM_NODE_PTR * Node
**          Pointer to a variable that will hold the allocated memory node.
*/
gceSTATUS
gckVIDMEM_Allocate(
    IN gckVIDMEM Memory,
    IN gctUINT Width,
    IN gctUINT Height,
    IN gctUINT Depth,
    IN gctUINT BytesPerPixel,
    IN gctUINT32 Alignment,
    IN gceSURF_TYPE Type,
#ifdef __QNXNTO__
    IN gctHANDLE Handle,
#endif
    OUT gcuVIDMEM_NODE_PTR * Node
    )
{
    gctSIZE_T bytes;
    gceSTATUS status;

    gcmkHEADER_ARG("Memory=0x%x Width=%u Height=%u Depth=%u BytesPerPixel=%u "
                   "Alignment=%u Type=%d",
                   Memory, Width, Height, Depth, BytesPerPixel, Alignment,
                   Type);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Memory, gcvOBJ_VIDMEM);
    gcmkVERIFY_ARGUMENT(Width > 0);
    gcmkVERIFY_ARGUMENT(Height > 0);
    gcmkVERIFY_ARGUMENT(Depth > 0);
    gcmkVERIFY_ARGUMENT(BytesPerPixel > 0);
    gcmkVERIFY_ARGUMENT(Node != gcvNULL);
#ifdef __QNXNTO__
    gcmkVERIFY_ARGUMENT(Handle != gcvNULL);
#endif

    /* Compute linear size. */
    bytes = Width * Height * Depth * BytesPerPixel;

    /* Allocate through linear function. */
#ifdef __QNXNTO__
    gcmkONERROR(
        gckVIDMEM_AllocateLinear(Memory, bytes, Alignment, Type, Handle, Node));
#else
    gcmkONERROR(
        gckVIDMEM_AllocateLinear(Memory, bytes, Alignment, Type, Node));
#endif

    /* Success. */
    gcmkFOOTER_ARG("*Node=0x%x", *Node);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

static gcuVIDMEM_NODE_PTR
_FindNode(
    IN gckVIDMEM Memory,
    IN gctINT Bank,
    IN gctSIZE_T Bytes,
    IN OUT gctUINT32_PTR Alignment
    )
{
    gcuVIDMEM_NODE_PTR node;
    gctUINT32 alignment;

    /* Walk all free nodes until we have one that is big enough or we have
       reached the sentinel. */
    for (node = Memory->sentinel[Bank].VidMem.nextFree;
         node->VidMem.bytes != 0;
         node = node->VidMem.nextFree)
    {
        /* Compute number of bytes to skip for alignment. */
        alignment = (*Alignment == 0)
                  ? 0
                  : (*Alignment - (node->VidMem.offset % *Alignment));

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
**  OUTPUT:
**
**      gcuVIDMEM_NODE_PTR * Node
**          Pointer to a variable that will hold the allocated memory node.
*/
gceSTATUS
gckVIDMEM_AllocateLinear(
    IN gckVIDMEM Memory,
    IN gctSIZE_T Bytes,
    IN gctUINT32 Alignment,
    IN gceSURF_TYPE Type,
#ifdef __QNXNTO__
    IN gctHANDLE Handle,
#endif
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
#ifdef __QNXNTO__
    gcmkVERIFY_ARGUMENT(Handle != gcvNULL);
#endif

    /* Acquire the mutex. */
    gcmkONERROR(
        gckOS_AcquireMutex(Memory->os, Memory->mutex, gcvINFINITE));

    acquired = gcvTRUE;

#if 0
    // dkm: 对于花屏死机的问题，感觉VV这么做只是规避，还是没有找到问题的原因
	if (Type == gcvSURF_TILE_STATUS
    && (Bytes + (1 << 20) > Memory->freeBytes))
    {
        //printk("alloc = %d, freeBytes = %d!, return OUT_OF_MEMORY!\n", (int)Bytes, (int)Memory->freeBytes);
        /* Not enough memory. */
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
	}
    //printk("alloc = %d, freeBytes = %d!\n", (int)Bytes, (int)Memory->freeBytes);
#else
    // dkm : 为gcvSURF_TILE_STATUS保留2M的空间
	if (Type != gcvSURF_TILE_STATUS
    && (Bytes + (2 << 20) > Memory->freeBytes)
	)
    {
        /* Not enough memory. */
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
	}
#endif

    // dkm: 多预留64K的空间，否则GPU会有访问非法地址的风险
    if (Bytes + (64 << 10) > Memory->freeBytes)
    {
        /* Not enough memory. */
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    /* Find the default bank for this surface type. */
    gcmkASSERT((gctINT) Type < gcmCOUNTOF(Memory->mapping));
    bank      = Memory->mapping[Type];
    alignment = Alignment;

    /* Find a free node in the default bank. */
    node = _FindNode(Memory, bank, Bytes, &alignment);

    /* Out of memory? */
    if (node == gcvNULL)
    {
        /* Walk all lower banks. */
        for (i = bank - 1; i >= 0; --i)
        {
            /* Find a free node inside the current bank. */
            node = _FindNode(Memory, i, Bytes, &alignment);
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
            node = _FindNode(Memory, i, Bytes, &alignment);
            if (node != gcvNULL)
            {
                break;
            }
        }
    }

    if (node == gcvNULL)
    {
        /* Out of memory. */
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
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
        } else {
            // dkm : Out of memory
            gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
        }
    }

    /* Do we have enough memory after the allocation to split it? */
    if (node->VidMem.bytes - Bytes > Memory->threshold)
    {
        /* Adjust the node size. */
        if(!_Split(Memory->os, node, Bytes)) {
            // dkm : Out of memory
            gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
        }
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
    node->VidMem.handle    = Handle;
#endif

    /* Adjust the number of free bytes. */
    Memory->freeBytes -= node->VidMem.bytes;

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
**      gcuVIDMEM_NODE_PTR Node
**          Pointer to a gcuVIDMEM_NODE object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckVIDMEM_Free(
    IN gcuVIDMEM_NODE_PTR Node
    )
{
    gckVIDMEM memory = gcvNULL;
    gcuVIDMEM_NODE_PTR node;
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;

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
        if (Node->VidMem.locked > 0)
        {
            gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_VIDMEM,
                           "Node 0x%x is locked (%d)",
                           Node, Node->VidMem.locked);

            /* Force unlock. */
            Node->VidMem.locked = 0;
        }

        /* Extract pointer to gckVIDMEM object owning the node. */
        memory = Node->VidMem.memory;

        /* Acquire the mutex. */
        gcmkONERROR(
            gckOS_AcquireMutex(memory->os, memory->mutex, gcvINFINITE));

        acquired = gcvTRUE;

#ifdef __QNXNTO__
        /* Reset handle to 0. */
        Node->VidMem.logical = gcvNULL;
        Node->VidMem.handle = 0;

        /* Don't try to a re-free an already freed node. */
        if ((Node->VidMem.nextFree == gcvNULL)
        &&  (Node->VidMem.prevFree == gcvNULL)
        )
#endif
        {
            /* Update the number of free bytes. */
            memory->freeBytes += Node->VidMem.bytes;

            /* Find the next free node. */
            for (node = Node->VidMem.next;
                 node->VidMem.nextFree == gcvNULL;
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

        /* Success. */
        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

    /*************************** Virtual Memory *******************************/

    /* Verify the gckKERNEL object pointer. */
    gcmkVERIFY_OBJECT(Node->Virtual.kernel, gcvOBJ_KERNEL);

#ifdef __QNXNTO__
    if (!Node->Virtual.unlockPending && (Node->Virtual.locked > 0))
#else
    if (!Node->Virtual.pending && (Node->Virtual.locked > 0))
#endif
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_VIDMEM,
                       "gckVIDMEM_Free: Virtual node 0x%x is locked (%d)",
                       Node, Node->Virtual.locked);

        /* Force unlock. */
        Node->Virtual.locked = 0;
    }

#ifdef __QNXNTO__
    if (!Node->Virtual.freePending) { if (Node->Virtual.unlockPending)
#else
    if (Node->Virtual.pending)
#endif
    {
        gcmkASSERT(Node->Virtual.locked == 1);

        /* Schedule the node to be freed. */
        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                       "gckVIDMEM_Free: Scheduling node 0x%x to be freed later",
                       Node);

        /* Schedule the video memory to be freed again. */
        gcmkONERROR(gckEVENT_FreeVideoMemory(Node->Virtual.kernel->event,
                                             Node,
                                             gcvKERNEL_PIXEL));

#ifdef __QNXNTO__
        Node->Virtual.freePending = gcvTRUE; }
#endif

        /* Success. */
        gcmkFOOTER_NO();
        return gcvSTATUS_SKIP;
    }

    else
    {
        /* Free the virtual memory. */
        gcmkVERIFY_OK(gckOS_FreePagedMemory(Node->Virtual.kernel->os,
                                            Node->Virtual.physical,
                                            Node->Virtual.bytes));

        /* Destroy the gcuVIDMEM_NODE union. */
        gcmkVERIFY_OK(gckVIDMEM_DestroyVirtual(Node));
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(memory->os, memory->mutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}


#ifdef __QNXNTO__
/*******************************************************************************
**
**  gcoVIDMEM_FreeHandleMemory
**
**  Free all allocated video memory nodes for a handle.
**
**  INPUT:
**
**      gcoVIDMEM Memory
**          Pointer to an gcoVIDMEM object..
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckVIDMEM_FreeHandleMemory(
    IN gckVIDMEM Memory,
    IN gctHANDLE Handle
    )
{
    gceSTATUS status;
    gctBOOL mutex = gcvFALSE;
    gcuVIDMEM_NODE_PTR node;
    gctINT i;
    gctUINT32 nodeCount = 0, byteCount = 0;
    gctBOOL again;

    gcmkHEADER_ARG("Memory=0x%x Handle=0x%x", Memory, Handle);

    gcmkVERIFY_OBJECT(Memory, gcvOBJ_VIDMEM);

    gcmkONERROR(gckOS_AcquireMutex(Memory->os, Memory->mutex, gcvINFINITE));
    mutex = gcvTRUE;

    /* Walk all sentinels. */
    for (i = 0; i < gcmCOUNTOF(Memory->sentinel); ++i)
    {
        /* Bail out of the heap if it is not used. */
        if (Memory->sentinel[i].VidMem.next == gcvNULL)
        {
            break;
        }

        do
        {
            again = gcvFALSE;

            /* Walk all the nodes until we reach the sentinel. */
            for (node = Memory->sentinel[i].VidMem.next;
                 node->VidMem.bytes != 0;
                 node = node->VidMem.next)
            {
                /* Free the node if it was allocated by Handle. */
                if (node->VidMem.handle == Handle)
                {
                    /* Unlock video memory. */
                    while (gckVIDMEM_Unlock(node, gcvSURF_TYPE_UNKNOWN, gcvNULL, gcvNULL)
                        != gcvSTATUS_MEMORY_UNLOCKED)
                        ;

                    nodeCount++;
                    byteCount += node->VidMem.bytes;

                    /* Free video memory. */
                    gcmkVERIFY_OK(gckVIDMEM_Free(node, gcvNULL));

                    /*
                     * Freeing may cause a merge which will invalidate our iteration.
                     * Don't be clever, just restart.
                     */
                    again = gcvTRUE;

                    break;
                }
            }
        }
        while (again);
    }

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Memory->os, Memory->mutex));
    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    if (mutex)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Memory->os, Memory->mutex));
    }

    gcmkFOOTER();
    return status;
}
#endif

/*******************************************************************************
**
**  gckVIDMEM_Lock
**
**  Lock a video memory node and return it's hardware specific address.
**
**  INPUT:
**
**      gcuVIDMEM_NODE_PTR Node
**          Pointer to a gcuVIDMEM_NODE union.
**
**  OUTPUT:
**
**      gctUINT32 * Address
**          Pointer to a variable that will hold the hardware specific address.
*/
gceSTATUS
gckVIDMEM_Lock(
    IN gcuVIDMEM_NODE_PTR Node,
    OUT gctUINT32 * Address
    )
{
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;
    gctBOOL locked = gcvFALSE;
    gckOS os = gcvNULL;

    gcmkHEADER_ARG("Node=0x%x", Node);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

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
        /* Increment the lock count. */
        Node->VidMem.locked ++;

        /* Return the address of the node. */
        *Address = Node->VidMem.memory->baseAddress
                 + Node->VidMem.offset
                 + Node->VidMem.alignment;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                      "Locked node 0x%x (%d) @ 0x%08X",
                      Node,
                      Node->VidMem.locked,
                      *Address);
    }

    /*************************** Virtual Memory *******************************/

    else
    {
        /* Verify the gckKERNEL object pointer. */
        gcmkVERIFY_OBJECT(Node->Virtual.kernel, gcvOBJ_KERNEL);

        /* Extract the gckOS object pointer. */
        os = Node->Virtual.kernel->os;
        gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

        /* Grab the mutex. */
        gcmkONERROR(gckOS_AcquireMutex(os, Node->Virtual.mutex, gcvINFINITE));
        acquired = gcvTRUE;

        /* Increment the lock count. */
        if (Node->Virtual.locked ++ == 0)
        {
            /* Is this node pending for a final unlock? */
#ifdef __QNXNTO__
            if (!Node->Virtual.contiguous && Node->Virtual.unlockPending)
#else
            if (!Node->Virtual.contiguous && Node->Virtual.pending)
#endif
            {
                /* Make sure we have a page table. */
                gcmkASSERT(Node->Virtual.pageTable != gcvNULL);

                /* Remove pending unlock. */
#ifdef __QNXNTO__
                Node->Virtual.unlockPending = gcvFALSE;
#else
                Node->Virtual.pending = gcvFALSE;
#endif
            }

            /* First lock - create a page table. */
            gcmkASSERT(Node->Virtual.pageTable == gcvNULL);

            /* Make sure we mark our node as not flushed. */
#ifdef __QNXNTO__
            Node->Virtual.unlockPending = gcvFALSE;
#else
            Node->Virtual.pending = gcvFALSE;
#endif

            /* Lock the allocated pages. */
#ifdef __QNXNTO__
            gcmkONERROR(
                gckOS_LockPages(os,
                                Node->Virtual.physical,
                                Node->Virtual.bytes,
                                Node->Virtual.userPID,
                                &Node->Virtual.logical,
                                &Node->Virtual.pageCount));
#else
            gcmkONERROR(
                gckOS_LockPages(os,
                                Node->Virtual.physical,
                                Node->Virtual.bytes,
                                &Node->Virtual.logical,
                                &Node->Virtual.pageCount));
#endif

            locked = gcvTRUE;

            if (Node->Virtual.contiguous)
            {
                /* Get physical address directly */
                gcmkONERROR(gckOS_GetPhysicalAddress(os,
                                    Node->Virtual.logical,
                                    &Node->Virtual.address));
            }
            else
            {
                /* Allocate pages inside the MMU. */
                gcmkONERROR(
                    gckMMU_AllocatePages(Node->Virtual.kernel->mmu,
                                         Node->Virtual.pageCount,
                                         &Node->Virtual.pageTable,
                                         &Node->Virtual.address));

                /* Map the pages. */
#ifdef __QNXNTO__
                gcmkONERROR(
                    gckOS_MapPages(os,
                                   Node->Virtual.physical,
                                   Node->Virtual.logical,
                                   Node->Virtual.pageCount,
                                   Node->Virtual.pageTable));
#else
                gcmkONERROR(
                    gckOS_MapPages(os,
                                   Node->Virtual.physical,
                                   Node->Virtual.pageCount,
                                   Node->Virtual.pageTable));
#endif

                gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                               "Mapped virtual node 0x%x to 0x%08X",
                               Node,
                               Node->Virtual.address);
            }
        }

        /* Return hardware address. */
        *Address = Node->Virtual.address;

        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(os, Node->Virtual.mutex));
    }

    /* Success. */
    gcmkFOOTER_ARG("*Address=%08x", *Address);
    return gcvSTATUS_OK;

OnError:
    if (locked)
    {
        if (Node->Virtual.pageTable != gcvNULL)
        {
            /* Free the pages from the MMU. */
            gcmkVERIFY_OK(
                gckMMU_FreePages(Node->Virtual.kernel->mmu,
                                 Node->Virtual.pageTable,
                                 Node->Virtual.pageCount));

            Node->Virtual.pageTable = gcvNULL;
        }

        /* Unlock the pages. */
#ifdef __QNXNTO__
        gcmkVERIFY_OK(
            gckOS_UnlockPages(os,
                              Node->Virtual.physical,
                              Node->Virtual.userPID,
                              Node->Virtual.bytes,
                              Node->Virtual.logical));
#else
        gcmkVERIFY_OK(
            gckOS_UnlockPages(os,
                              Node->Virtual.physical,
                              Node->Virtual.bytes,
                              Node->Virtual.logical));
#endif
    }

    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(os, Node->Virtual.mutex));
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
**      gcuVIDMEM_NODE_PTR Node
**          Pointer to a locked gcuVIDMEM_NODE union.
**
**      gceSURF_TYPE Type
**          Type of surface to unlock.
**
**      gctSIZE_T * CommandSize
**          Pointer to a variable specifying the number of bytes in the command
**          buffer specified by 'Commands'.  If gcvNULL, there is no command
**          buffer and the video memory shoud be unlocked synchronously.
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
    IN gcuVIDMEM_NODE_PTR Node,
    IN gceSURF_TYPE Type,
    IN OUT gctBOOL * Asynchroneous
    )
{
    gceSTATUS status;
    gckKERNEL kernel;
    gckHARDWARE hardware;
    gctPOINTER buffer;
    gctSIZE_T requested, bufferSize;
    gckCOMMAND command = gcvNULL;
    gceKERNEL_FLUSH flush;
    gckOS os = gcvNULL;
    gctBOOL acquired = gcvFALSE;
    gctBOOL needRelease = gcvFALSE;

    gcmkHEADER_ARG("Node=0x%x Type=%d *Asynchroneous=%d",
                   Node, Type, gcmOPT_VALUE(Asynchroneous));

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
        if (Node->VidMem.locked <= 0)
        {
            /* The surface was not locked. */
            gcmkONERROR(gcvSTATUS_MEMORY_UNLOCKED);
        }

        /* Decrement the lock count. */
        Node->VidMem.locked --;

        if (Asynchroneous != gcvNULL)
        {
            /* No need for any events. */
            *Asynchroneous = gcvFALSE;
        }
    }

    /*************************** Virtual Memory *******************************/

    else
    {
        /* Verify the gckKERNEL object pointer. */
        kernel = Node->Virtual.kernel;
        gcmkVERIFY_OBJECT(kernel, gcvOBJ_KERNEL);

        /* Verify the gckHARDWARE object pointer. */
        hardware = Node->Virtual.kernel->hardware;
        gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

        /* Verify the gckCOMMAND object pointer. */
        command = Node->Virtual.kernel->command;
        gcmkVERIFY_OBJECT(command, gcvOBJ_COMMAND);


        /* Get the gckOS object pointer. */
        os = kernel->os;
        gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

        /* Grab the mutex. */
        gcmkONERROR(
                gckOS_AcquireMutex(os, Node->Virtual.mutex, gcvINFINITE));

        acquired = gcvTRUE;


        if (Asynchroneous == gcvNULL)
        {
            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                           "gckVIDMEM_Unlock: Unlocking virtual node 0x%x (%d)",
                           Node,
                           Node->Virtual.locked);

            if (Node->Virtual.locked == 0)
            {
                status = gcvSTATUS_MEMORY_UNLOCKED;
                goto OnError;
            }

            /* Decrement lock count. */
            -- Node->Virtual.locked;

            /* See if we can unlock the resources. */
            if (Node->Virtual.locked == 0)
            {
                /* Unlock the pages. */
#ifdef __QNXNTO__
                gcmkONERROR(
                        gckOS_UnlockPages(os,
                            Node->Virtual.physical,
                            Node->Virtual.userPID,
                            Node->Virtual.bytes,
                            Node->Virtual.logical));
#else
                gcmkONERROR(
                        gckOS_UnlockPages(os,
                            Node->Virtual.physical,
                            Node->Virtual.bytes,
                            Node->Virtual.logical));
#endif
 
                /* Free the page table. */
                if (Node->Virtual.pageTable != gcvNULL)
                {
                    gcmkONERROR(
                            gckMMU_FreePages(Node->Virtual.kernel->mmu,
                                Node->Virtual.pageTable,
                                Node->Virtual.pageCount));

                    /* Mark page table as freed. */
                    Node->Virtual.pageTable = gcvNULL;
                }

                /* Mark node as unlocked. */
#ifdef __QNXTO
                Node->Virtual.unlockPending = gcvFALSE;
#else
                Node->Virtual.pending = gcvFALSE;
#endif
            }

            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                    "Unmapped virtual node 0x%x from 0x%08X",
                    Node, Node->Virtual.address);
        }
        else
        {
            /* If we need to unlock a node from virtual memory we have to be
            ** very carefull.  If the node is still inside the caches we
            ** might get a bus error later if the cache line needs to be
            ** replaced.  So - we have to flush the caches before we do
            ** anything.  We also need to stall to make sure the flush has
            ** happened.  However - when we get to this point we are inside
            ** the interrupt handler and we cannot just gckCOMMAND_Wait
            ** because it will wait forever.  So - what we do here is we
            ** verify the type of the surface, flush the appropriate cache,
            ** mark the node as flushed, and issue another unlock to unmap
            ** the MMU. */
            if (!Node->Virtual.contiguous
            &&  (Node->Virtual.locked == 1)
#ifdef __QNXTO__
            &&  !Node->Virtual.unlockPending
#else
            &&  !Node->Virtual.pending
#endif
            )
            {
                if (Type == gcvSURF_BITMAP)
                {
                    /* Flush 2D cache. */
                    flush = gcvFLUSH_2D;
                }
                else if (Type == gcvSURF_RENDER_TARGET)
                {
                    /* Flush color cache. */
                    flush = gcvFLUSH_COLOR;
                }
                else if (Type == gcvSURF_DEPTH)
                {
                    /* Flush depth cache. */
                    flush = gcvFLUSH_DEPTH;
                }
                else if (Type == gcvSURF_TEXTURE)   // dkm : add
                {
                    flush = gcvFLUSH_TEXTURE;
                }
                else
                {
                    /* No flush required. */
                    flush = (gceKERNEL_FLUSH) 0;
                }

                flush = flush | gcvFLUSH_2D;    // dkm : add to avoid the gpu hang

                gcmkONERROR(
                    gckHARDWARE_Flush(hardware, flush, gcvNULL, &requested));

                if (requested != 0)
                {
                    gcmkONERROR(
                        gckCOMMAND_Reserve(command,
                                           requested,
                                           &buffer,
                                           &bufferSize));

                    needRelease = gcvTRUE;

                    gcmkONERROR(gckHARDWARE_Flush(hardware,
                                                  flush,
                                                  buffer,
                                                  &bufferSize));

                    /* Mark node as pending. */
#ifdef __QNXNTO__
                    Node->Virtual.unlockPending = gcvTRUE;
#else
                    Node->Virtual.pending = gcvTRUE;
#endif

                    gcmkONERROR(gckCOMMAND_Execute(command, requested));

                    needRelease = gcvFALSE;
                }
            }

            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                           "Scheduled unlock for virtual node 0x%x",
                           Node);

            /* Schedule the surface to be unlocked. */
            *Asynchroneous = gcvTRUE;
        }

        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(os, Node->Virtual.mutex));
        acquired = gcvFALSE;
    }

    /* Success. */
    gcmkFOOTER_ARG("*Asynchroneous=%d", gcmOPT_VALUE(Asynchroneous));
    return gcvSTATUS_OK;

OnError:
    if (needRelease)
    {
        gcmkVERIFY_OK(gckCOMMAND_Release(command));
    }

    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(os, Node->Virtual.mutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

#ifdef __QNXNTO__
/* Set the allocating process' PID for this node. */
gceSTATUS
gckVIDMEM_SetPID(
    IN gcuVIDMEM_NODE_PTR Node,
    IN gctUINT32 Pid
    )
{
    if (Node != gcvNULL)
    {
        if (Node->VidMem.memory->object.type != gcvOBJ_VIDMEM)
        {
            Node->Virtual.userPID = Pid;
        }

    }
    else
    {
        return gcvSTATUS_INVALID_OBJECT;
    }

    return gcvSTATUS_OK;
}
#endif
