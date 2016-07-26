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

#define _GC_OBJ_ZONE    gcvZONE_MMU

typedef enum _gceMMU_TYPE
{
    gcvMMU_USED     = (0 << 4),
    gcvMMU_SINGLE   = (1 << 4),
    gcvMMU_FREE     = (2 << 4),
}
gceMMU_TYPE;

#define gcmENTRY_TYPE(x) (x & 0xF0)

#define gcmENTRY_COUNT(x) ((x & 0xFFFFFF00) >> 8)

#define gcdMMU_TABLE_DUMP       0

#define gcdVERTEX_START      (128 << 10)

typedef struct _gcsMMU_STLB *gcsMMU_STLB_PTR;

typedef struct _gcsMMU_STLB
{
    gctPHYS_ADDR    physical;
    gctUINT32_PTR   logical;
    gctSIZE_T       size;
    gctPHYS_ADDR_T  physBase;
    gctSIZE_T       pageCount;
    gctUINT32       mtlbIndex;
    gctUINT32       mtlbEntryNum;
    gcsMMU_STLB_PTR next;
} gcsMMU_STLB;

#if gcdSHARED_PAGETABLE
typedef struct _gcsSharedPageTable * gcsSharedPageTable_PTR;
typedef struct _gcsSharedPageTable
{
    /* Shared gckMMU object. */
    gckMMU          mmu;

    /* Hardwares which use this shared pagetable. */
    gckHARDWARE     hardwares[gcdMAX_GPU_COUNT];

    /* Number of cores use this shared pagetable. */
    gctUINT32       reference;
}
gcsSharedPageTable;

static gcsSharedPageTable_PTR sharedPageTable = gcvNULL;
#endif

typedef struct _gcsDynamicSpaceNode * gcsDynamicSpaceNode_PTR;
typedef struct _gcsDynamicSpaceNode
{
    gctUINT32       start;
    gctINT32        entries;
}
gcsDynamicSpaceNode;

static void
_WritePageEntry(
    IN gctUINT32_PTR PageEntry,
    IN gctUINT32     EntryValue
    )
{
    static gctUINT16 data = 0xff00;

    if (*(gctUINT8 *)&data == 0xff)
    {
        *PageEntry = gcmSWAB32(EntryValue);
    }
    else
    {
        *PageEntry = EntryValue;
    }
}

static gctUINT32
_ReadPageEntry(
    IN gctUINT32_PTR PageEntry
    )
{
    static gctUINT16 data = 0xff00;
    gctUINT32 entryValue;

    if (*(gctUINT8 *)&data == 0xff)
    {
        entryValue = *PageEntry;
        return gcmSWAB32(entryValue);
    }
    else
    {
        return *PageEntry;
    }
}

static gceSTATUS
_FillPageTable(
    IN gctUINT32_PTR PageTable,
    IN gctUINT32     PageCount,
    IN gctUINT32     EntryValue
)
{
    gctUINT i;

    for (i = 0; i < PageCount; i++)
    {
        _WritePageEntry(PageTable + i, EntryValue);
    }

    return gcvSTATUS_OK;
}

static gceSTATUS
_FillMap(
    IN gctUINT32_PTR Map,
    IN gctUINT32     PageCount,
    IN gctUINT32     EntryValue
)
{
    gctUINT i;

    for (i = 0; i < PageCount; i++)
    {
        Map[i] = EntryValue;
    }

    return gcvSTATUS_OK;
}

static gceSTATUS
_Link(
    IN gcsADDRESS_AREA_PTR Area,
    IN gctUINT32 Index,
    IN gctUINT32 Next
    )
{
    if (Index >= Area->pageTableEntries)
    {
        /* Just move heap pointer. */
        Area->heapList = Next;
    }
    else
    {
        /* Address page table. */
        gctUINT32_PTR map = Area->mapLogical;

        /* Dispatch on node type. */
        switch (gcmENTRY_TYPE(map[Index]))
        {
        case gcvMMU_SINGLE:
            /* Set single index. */
            map[Index] = (Next << 8) | gcvMMU_SINGLE;
            break;

        case gcvMMU_FREE:
            /* Set index. */
            map[Index + 1] = Next;
            break;

        default:
            gcmkFATAL("MMU table correcupted at index %u!", Index);
            return gcvSTATUS_HEAP_CORRUPTED;
        }
    }

    /* Success. */
    return gcvSTATUS_OK;
}

static gceSTATUS
_AddFree(
    IN gcsADDRESS_AREA_PTR Area,
    IN gctUINT32 Index,
    IN gctUINT32 Node,
    IN gctUINT32 Count
    )
{
    gctUINT32_PTR map = Area->mapLogical;

    if (Count == 1)
    {
        /* Initialize a single page node. */
        map[Node] = (~((1U<<8)-1)) | gcvMMU_SINGLE;
    }
    else
    {
        /* Initialize the node. */
        map[Node + 0] = (Count << 8) | gcvMMU_FREE;
        map[Node + 1] = ~0U;
    }

    /* Append the node. */
    return _Link(Area, Index, Node);
}

static gceSTATUS
_Collect(
    IN gcsADDRESS_AREA_PTR Area
    )
{
    gctUINT32_PTR map = Area->mapLogical;
    gceSTATUS status;
    gctUINT32 i, previous, start = 0, count = 0;

    previous = Area->heapList = ~0U;
    Area->freeNodes = gcvFALSE;

    /* Walk the entire page table. */
    for (i = 0; i < Area->pageTableEntries; ++i)
    {
        /* Dispatch based on type of page. */
        switch (gcmENTRY_TYPE(map[i]))
        {
        case gcvMMU_USED:
            /* Used page, so close any open node. */
            if (count > 0)
            {
                /* Add the node. */
                gcmkONERROR(_AddFree(Area, previous, start, count));

                /* Reset the node. */
                previous = start;
                count    = 0;
            }
            break;

        case gcvMMU_SINGLE:
            /* Single free node. */
            if (count++ == 0)
            {
                /* Start a new node. */
                start = i;
            }
            break;

        case gcvMMU_FREE:
            /* A free node. */
            if (count == 0)
            {
                /* Start a new node. */
                start = i;
            }

            /* Advance the count. */
            count += map[i] >> 8;

            /* Advance the index into the page table. */
            i     += (map[i] >> 8) - 1;
            break;

        default:
            gcmkFATAL("MMU page table correcupted at index %u!", i);
            return gcvSTATUS_HEAP_CORRUPTED;
        }
    }

    /* See if we have an open node left. */
    if (count > 0)
    {
        /* Add the node to the list. */
        gcmkONERROR(_AddFree(Area, previous, start, count));
    }

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_MMU,
                   "Performed a garbage collection of the MMU heap.");

    /* Success. */
    return gcvSTATUS_OK;

OnError:
    /* Return the staus. */
    return status;
}

static gctUINT32
_SetPage(gctUINT32 PageAddress, gctUINT32 PageAddressExt, gctBOOL Writable)
{
    gctUINT32 entry = PageAddress
                    /* AddressExt */
                    | (PageAddressExt << 4)
                    /* Ignore exception */
                    | (0 << 1)
                    /* Present */
                    | (1 << 0);

    if (Writable)
    {
        /* writable */
        entry |= (1 << 2);
    }
#if gcdUSE_MMU_EXCEPTION
    else
    {
        /* If this page is read only, set exception bit to make exception happens
        ** when writing to it. */
        entry |= gcdMMU_STLB_EXCEPTION;
    }
#endif

    return entry;
}

static gctUINT32
_MtlbOffset(
    gctUINT32 Address
    )
{
    return (Address & gcdMMU_MTLB_MASK) >> gcdMMU_MTLB_SHIFT;
}

gctUINT32
_AddressToIndex(
    IN gcsADDRESS_AREA_PTR Area,
    IN gctUINT32 Address
    )
{
    gctUINT32 mtlbOffset = (Address & gcdMMU_MTLB_MASK) >> gcdMMU_MTLB_SHIFT;
    gctUINT32 stlbOffset = (Address & gcdMMU_STLB_4K_MASK) >> gcdMMU_STLB_4K_SHIFT;

    return (mtlbOffset - Area->dynamicMappingStart) * gcdMMU_STLB_4K_ENTRY_NUM + stlbOffset;
}

static gctUINT32_PTR
_StlbEntry(
    gcsADDRESS_AREA_PTR Area,
    gctUINT32 Address
    )
{
    gctUINT32 index = _AddressToIndex(Area, Address);

    return &Area->pageTableLogical[index];
}

static gceSTATUS
_FillFlatMappingInMap(
    gcsADDRESS_AREA_PTR Area,
    gctUINT32 Index,
    gctUINT32 NumPages
    )
{
    gceSTATUS status;
    gctUINT32 i;
    gctBOOL gotIt = gcvFALSE;
    gctUINT32 index = Index;
    gctUINT32_PTR map = Area->mapLogical;
    gctUINT32 previous = ~0U;

    /* Find node which contains index. */
    for (i = 0; !gotIt && (i < Area->pageTableEntries);)
    {
        gctUINT32 numPages;

        switch (gcmENTRY_TYPE(map[i]))
        {
        case gcvMMU_SINGLE:
            if (i == index)
            {
                gotIt = gcvTRUE;
            }
            else
            {
                previous = i;
                i = map[i] >> 8;
            }
            break;

        case gcvMMU_FREE:
            numPages = map[i] >> 8;
            if (index >= i && index + NumPages - 1 < i + numPages)
            {
                gotIt = gcvTRUE;
            }
            else
            {
                previous = i;
                i = map[i + 1];
            }
            break;

        case gcvMMU_USED:
            i++;
            break;

        default:
            gcmkFATAL("MMU table correcupted at index %u!", index);
            gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
        }
    }

    switch (gcmENTRY_TYPE(map[i]))
    {
    case gcvMMU_SINGLE:
        /* Unlink single node from free list. */
        gcmkONERROR(
            _Link(Area, previous, map[i] >> 8));
        break;

    case gcvMMU_FREE:
        /* Split the node. */
        {
            gctUINT32 start;
            gctUINT32 next = map[i+1];
            gctUINT32 total = map[i] >> 8;
            gctUINT32 countLeft = index - i;
            gctUINT32 countRight = total - countLeft - NumPages;

            if (countLeft)
            {
                start = i;
                _AddFree(Area, previous, start, countLeft);
                previous = start;
            }

            if (countRight)
            {
                start = index + NumPages;
                _AddFree(Area, previous, start, countRight);
                previous = start;
            }

            _Link(Area, previous, next);
        }
        break;
    }

    _FillMap(&map[index], NumPages, gcvMMU_USED);

    return gcvSTATUS_OK;
OnError:
    return status;
}

#if gcdPROCESS_ADDRESS_SPACE
gctUINT32
_StlbOffset(
    gctUINT32 Address
    )
{
    return (Address & gcdMMU_STLB_4K_MASK) >> gcdMMU_STLB_4K_SHIFT;
}

static gceSTATUS
_AllocateStlb(
    IN gckOS Os,
    OUT gcsMMU_STLB_PTR *Stlb
    )
{
    gceSTATUS status;
    gcsMMU_STLB_PTR stlb;
    gctPOINTER pointer;

    /* Allocate slave TLB record. */
    gcmkONERROR(gckOS_Allocate(Os, gcmSIZEOF(gcsMMU_STLB), &pointer));
    stlb = pointer;

    stlb->size = gcdMMU_STLB_4K_SIZE;

    /* Allocate slave TLB entries. */
    gcmkONERROR(gckOS_AllocateContiguous(
        Os,
        gcvFALSE,
        &stlb->size,
        &stlb->physical,
        (gctPOINTER)&stlb->logical
        ));

    gcmkONERROR(gckOS_GetPhysicalAddress(Os, stlb->logical, &stlb->physBase));

#if gcdUSE_MMU_EXCEPTION
    _FillPageTable(stlb->logical, stlb->size / 4, gcdMMU_STLB_EXCEPTION);
#else
    gckOS_ZeroMemory(stlb->logical, stlb->size);
#endif

    *Stlb = stlb;

    return gcvSTATUS_OK;

OnError:
    return status;
}

gceSTATUS
_SetupProcessAddressSpace(
    IN gckMMU Mmu
    )
{
    gceSTATUS status;
    gctINT numEntries = 0;
    gctUINT32_PTR map;

    numEntries = gcdPROCESS_ADDRESS_SPACE_SIZE
               /* Address space mapped by one MTLB entry. */
               / (1 << gcdMMU_MTLB_SHIFT);

    area->dynamicMappingStart = 0;

    area->pageTableSize = numEntries * 4096;

    area->pageTableEntries = area->pageTableSize / gcmSIZEOF(gctUINT32);

    gcmkONERROR(gckOS_Allocate(Mmu->os,
                               area->pageTableSize,
                               (void **)&area->mapLogical));

    /* Initialization. */
    map      = area->mapLogical;
    map[0]   = (area->pageTableEntries << 8) | gcvMMU_FREE;
    map[1]   = ~0U;
    area->heapList  = 0;
    area->freeNodes = gcvFALSE;

    return gcvSTATUS_OK;

OnError:
    return status;
}
#else
static gceSTATUS
_FillFlatMapping(
    IN gckMMU Mmu,
    IN gctUINT32 PhysBase,
    OUT gctSIZE_T Size
    )
{
    gceSTATUS status;
    gctBOOL mutex = gcvFALSE;
    gcsMMU_STLB_PTR head = gcvNULL, pre = gcvNULL;
    gctUINT32 start = PhysBase & ~gcdMMU_PAGE_64K_MASK;
    gctUINT32 end = (gctUINT32) (PhysBase + Size - 1) & ~gcdMMU_PAGE_64K_MASK;
    gctUINT32 mStart = start >> gcdMMU_MTLB_SHIFT;
    gctUINT32 mEnd = end >> gcdMMU_MTLB_SHIFT;
    gctUINT32 sStart = (start & gcdMMU_STLB_64K_MASK) >> gcdMMU_STLB_64K_SHIFT;
    gctUINT32 sEnd = (end & gcdMMU_STLB_64K_MASK) >> gcdMMU_STLB_64K_SHIFT;
    gctPHYS_ADDR_T physical;
    gctUINT32 size;

    gctUINT32 mtlb = _MtlbOffset(PhysBase);
    gcsADDRESS_AREA_PTR area = &Mmu->area[0];

    /************************ Setup flat mapping in dynamic range. ****************/

    if (area->dynamicMappingStart != gcvINVALID_ADDRESS && mtlb >= area->dynamicMappingStart &&
	_MtlbOffset(PhysBase + Size - 1) < area->dynamicMappingEnd)
    {
        gctUINT32_PTR stlbEntry;
        gctUINT i;

        stlbEntry = _StlbEntry(area, PhysBase);

        /* Must be aligned to page. */
        gcmkASSERT((Size & 0xFFF) == 0);

        for (i = 0; i < (Size / 4096); i++)
        {
            /* Flat mapping in page table. */
            _WritePageEntry(stlbEntry, _SetPage(PhysBase + i * 4096, 0, gcvTRUE));
        }

        gcmkSAFECASTSIZET(size, Size);

        /* Flat mapping in map. */
        _FillFlatMappingInMap(area, _AddressToIndex(area, PhysBase), size / 4096);

        return gcvSTATUS_OK;
    }

    /************************ Setup flat mapping in non dynamic range. **************/

    /* Grab the mutex. */
    gcmkONERROR(gckOS_AcquireMutex(Mmu->os, Mmu->pageTableMutex, gcvINFINITE));
    mutex = gcvTRUE;

    while (mStart <= mEnd)
    {
        gcsMMU_STLB_PTR stlb = gcvNULL;
        gctUINT32 last = (mStart == mEnd) ? sEnd : (gcdMMU_STLB_64K_ENTRY_NUM - 1);

        gcmkASSERT(mStart < gcdMMU_MTLB_ENTRY_NUM);
        if (*(Mmu->mtlbLogical + mStart) == 0)
        {
            gctPOINTER pointer = gcvNULL;

            gctUINT32 mtlbEntry;

            gcmkONERROR(gckOS_Allocate(Mmu->os, sizeof(struct _gcsMMU_STLB), &pointer));
            stlb = pointer;

            stlb->mtlbEntryNum = 0;
            stlb->next = gcvNULL;
            stlb->physical = gcvNULL;
            stlb->logical = gcvNULL;
            stlb->size = gcdMMU_STLB_64K_SIZE;
            stlb->pageCount = 0;

            if (pre == gcvNULL)
            {
                pre = head = stlb;
            }
            else
            {
                gcmkASSERT(pre->next == gcvNULL);
                pre->next = stlb;
                pre = stlb;
            }

            gcmkONERROR(
                    gckOS_AllocateContiguous(Mmu->os,
                                             gcvFALSE,
                                             &stlb->size,
                                             &stlb->physical,
                                             (gctPOINTER)&stlb->logical));

            gcmkONERROR(gckOS_ZeroMemory(stlb->logical, stlb->size));

            gcmkONERROR(gckOS_GetPhysicalAddress(
                Mmu->os,
                stlb->logical,
                &physical));

            gcmkSAFECASTPHYSADDRT(stlb->physBase, physical);

            if (stlb->physBase & (gcdMMU_STLB_64K_SIZE - 1))
            {
                gcmkONERROR(gcvSTATUS_NOT_ALIGNED);
            }

            physical  = stlb->physBase
                      /* 64KB page size */
                      | (1 << 2)
                      /* Ignore exception */
                      | (0 << 1)
                      /* Present */
                      | (1 << 0);

            gcmkSAFECASTPHYSADDRT(mtlbEntry, physical);

            _WritePageEntry(Mmu->mtlbLogical + mStart, mtlbEntry);

#if gcdMMU_TABLE_DUMP
            gckOS_Print("%s(%d): insert MTLB[%d]: %08x\n",
                __FUNCTION__, __LINE__,
                mStart,
                _ReadPageEntry(Mmu->mtlbLogical + mStart));
#endif

            stlb->mtlbIndex = mStart;
            stlb->mtlbEntryNum = 1;
#if gcdMMU_TABLE_DUMP
            gckOS_Print("%s(%d): STLB: logical:%08x -> physical:%08x\n",
                    __FUNCTION__, __LINE__,
                    stlb->logical,
                    stlb->physBase);
#endif


        }
        else
        {
            stlb = Mmu->staticSTLB;

            while (stlb)
            {
                gctUINT32 mtlbEntry = _ReadPageEntry(Mmu->mtlbLogical + mStart);

                if (stlb->physBase == (mtlbEntry & gcdMMU_MTLB_ENTRY_STLB_MASK))
                {
                    break;
                }

                stlb = stlb->next;
            }
        }

        /* Fill STLB. */
        sStart = (start & gcdMMU_STLB_64K_MASK) >> gcdMMU_STLB_64K_SHIFT;

        while (sStart <= last)
        {
            gcmkASSERT(!(start & gcdMMU_PAGE_64K_MASK));
            _WritePageEntry(stlb->logical + sStart, _SetPage(start, 0, gcvTRUE));
#if gcdMMU_TABLE_DUMP
            gckOS_Print("%s(%d): insert STLB[%d]: %08x\n",
                __FUNCTION__, __LINE__,
                sStart,
                _ReadPageEntry(stlb->logical + sStart));
#endif
            /* next page. */
            start += gcdMMU_PAGE_64K_SIZE;
            sStart++;
            stlb->pageCount++;
        }

        ++mStart;
    }

    if (pre)
    {
        /* Insert the stlb into staticSTLB. */
        if (Mmu->staticSTLB == gcvNULL)
        {
            Mmu->staticSTLB = head;
        }
        else
        {
            gcmkASSERT(pre != gcvNULL);
            gcmkASSERT(pre->next == gcvNULL);
            pre->next = Mmu->staticSTLB;
            Mmu->staticSTLB = head;
        }
    }

    /* Release the mutex. */
    gcmkVERIFY_OK(gckOS_ReleaseMutex(Mmu->os, Mmu->pageTableMutex));

#if gcdENABLE_TRUST_APPLICATION
    if (Mmu->hardware->secureMode == gcvSECURE_IN_TA)
    {
        gckKERNEL_SecurityMapMemory(Mmu->hardware->kernel, gcvNULL, PhysBase, (gctUINT32)Size/4096, &PhysBase);
    }
#endif

    return gcvSTATUS_OK;

OnError:

    /* Roll back. */
    while (head != gcvNULL)
    {
        pre = head;
        head = head->next;

        if (pre->physical != gcvNULL)
        {
            gcmkVERIFY_OK(
                gckOS_FreeContiguous(Mmu->os,
                    pre->physical,
                    pre->logical,
                    pre->size));
        }

        if (pre->mtlbEntryNum != 0)
        {
            gcmkASSERT(pre->mtlbEntryNum == 1);
            _WritePageEntry(Mmu->mtlbLogical + pre->mtlbIndex, 0);
        }

        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Mmu->os, pre));
    }

    if (mutex)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Mmu->os, Mmu->pageTableMutex));
    }

    return status;
}

static gceSTATUS
_FindDynamicSpace(
    IN gckMMU Mmu,
    OUT gcsDynamicSpaceNode_PTR *Array,
    OUT gctINT * Size
    )
{
    gceSTATUS status = gcvSTATUS_OK;
    gctPOINTER pointer = gcvNULL;
    gcsDynamicSpaceNode_PTR array = gcvNULL;
    gctINT size = 0;
    gctINT i = 0, nodeStart = -1, nodeEntries = 0;

    /* Allocate memory for the array. */
    gcmkONERROR(gckOS_Allocate(Mmu->os,
                               gcmSIZEOF(*array) * (gcdMMU_MTLB_ENTRY_NUM / 2),
                               &pointer));

    array = (gcsDynamicSpaceNode_PTR)pointer;

    /* Loop all the entries. */
    while (i < gcdMMU_MTLB_ENTRY_NUM)
    {
        if (!Mmu->mtlbLogical[i])
        {
            if (nodeStart < 0)
            {
                /* This is the first entry of the dynamic space. */
                nodeStart   = i;
                nodeEntries = 1;
            }
            else
            {
                /* Other entries of the dynamic space. */
                nodeEntries++;
            }
        }
        else if (nodeStart >= 0)
        {
            /* Save the previous node. */
            array[size].start   = nodeStart;
            array[size].entries = nodeEntries;
            size++;

            /* Reset the start. */
            nodeStart   = -1;
            nodeEntries = 0;
        }

        i++;
    }

    /* Save the previous node. */
    if (nodeStart >= 0)
    {
        array[size].start   = nodeStart;
        array[size].entries = nodeEntries;
        size++;
    }

#if gcdMMU_TABLE_DUMP
    for (i = 0; i < size; i++)
    {
        gckOS_Print("%s(%d): [%d]: start=%d, entries=%d.\n",
                __FUNCTION__, __LINE__,
                i,
                array[i].start,
                array[i].entries);
    }
#endif

    *Array = array;
    *Size  = size;

    return gcvSTATUS_OK;

OnError:
    if (pointer != gcvNULL)
    {
        gckOS_Free(Mmu->os, pointer);
    }

    return status;
}

static gceSTATUS
_SetupAddressArea(
    IN gckOS Os,
    IN gcsADDRESS_AREA_PTR Area,
    IN gctUINT32 NumMTLBEntries
    )
{
    gceSTATUS status;
    gctUINT32_PTR map;

    gcmkHEADER();
    Area->pageTableSize = NumMTLBEntries * 4096;

    gcmkSAFECASTSIZET(Area->pageTableEntries, Area->pageTableSize / gcmSIZEOF(gctUINT32));

    gcmkONERROR(gckOS_Allocate(Os, Area->pageTableSize, (void **)&Area->mapLogical));

    /* Initialization. */
    map      = Area->mapLogical;
    map[0]   = (Area->pageTableEntries << 8) | gcvMMU_FREE;
    map[1]   = ~0U;
    Area->heapList  = 0;
    Area->freeNodes = gcvFALSE;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

static gceSTATUS
_SetupDynamicSpace(
    IN gckMMU Mmu
    )
{
    gceSTATUS status;
    gcsDynamicSpaceNode_PTR nodeArray = gcvNULL;
    gctINT i, nodeArraySize = 0;
    gctPHYS_ADDR_T physical;
    gctUINT32 address;
    gctINT numEntries = 0;
    gctBOOL acquired = gcvFALSE;
    gctUINT32 mtlbEntry;
    gcsADDRESS_AREA_PTR area = &Mmu->area[0];
    gcsADDRESS_AREA_PTR areaSecure = &Mmu->area[gcvADDRESS_AREA_SECURE];
    gctUINT32 secureAreaSize = 0;

    /* Find all the dynamic address space. */
    gcmkONERROR(_FindDynamicSpace(Mmu, &nodeArray, &nodeArraySize));

    /* TODO: We only use the largest one for now. */
    for (i = 0; i < nodeArraySize; i++)
    {
        if (nodeArray[i].entries > numEntries)
        {
            area->dynamicMappingStart = nodeArray[i].start;
            numEntries                = nodeArray[i].entries;
            area->dynamicMappingEnd   = area->dynamicMappingStart + numEntries;
        }
    }

    gckOS_Free(Mmu->os, (gctPOINTER)nodeArray);

#if gcdENABLE_TRUST_APPLICATION
    if (gckHARDWARE_IsFeatureAvailable(Mmu->hardware, gcvFEATURE_SECURITY) == gcvSTATUS_TRUE)
    {
        secureAreaSize = gcdMMU_SECURE_AREA_SIZE;
    }
#endif

    /* Setup secure address area if need. */
    if (secureAreaSize > 0)
    {
        gcmkASSERT(numEntries > (gctINT)secureAreaSize);

        areaSecure->dynamicMappingStart = area->dynamicMappingStart
                                        + (numEntries - secureAreaSize);

        gcmkONERROR(_SetupAddressArea(Mmu->os, areaSecure, secureAreaSize));

        numEntries -= secureAreaSize;
    }

    /* Setup normal address area. */
    gcmkONERROR(_SetupAddressArea(Mmu->os, area, numEntries));

    /* Construct Slave TLB. */
    gcmkONERROR(gckOS_AllocateContiguous(Mmu->os,
                gcvFALSE,
                &area->pageTableSize,
                &area->pageTablePhysical,
                (gctPOINTER)&area->pageTableLogical));

#if gcdUSE_MMU_EXCEPTION
    gcmkONERROR(_FillPageTable(area->pageTableLogical,
                               area->pageTableEntries,
                               /* Enable exception */
                               1 << 1));
#else
    /* Invalidate all entries. */
    gcmkONERROR(gckOS_ZeroMemory(area->pageTableLogical,
                area->pageTableSize));
#endif

    gcmkONERROR(gckOS_GetPhysicalAddress(Mmu->os,
                area->pageTableLogical,
                &physical));

    gcmkSAFECASTPHYSADDRT(address, physical);

    /* Grab the mutex. */
    gcmkONERROR(gckOS_AcquireMutex(Mmu->os, Mmu->pageTableMutex, gcvINFINITE));
    acquired = gcvTRUE;

    /* Map to Master TLB. */
    for (i = (gctINT)area->dynamicMappingStart;
         i < (gctINT)area->dynamicMappingStart + numEntries;
         i++)
    {
        mtlbEntry = address
                  /* 4KB page size */
                  | (0 << 2)
                  /* Ignore exception */
                  | (0 << 1)
                  /* Present */
                  | (1 << 0);

        _WritePageEntry(Mmu->mtlbLogical + i, mtlbEntry);

#if gcdMMU_TABLE_DUMP
        gckOS_Print("%s(%d): insert MTLB[%d]: %08x\n",
                __FUNCTION__, __LINE__,
                i,
                _ReadPageEntry(Mmu->mtlbLogical + i));
#endif
        address += gcdMMU_STLB_4K_SIZE;
    }

    /* Release the mutex. */
    gcmkVERIFY_OK(gckOS_ReleaseMutex(Mmu->os, Mmu->pageTableMutex));

    return gcvSTATUS_OK;

OnError:
    if (area->mapLogical)
    {
        gcmkVERIFY_OK(
            gckOS_Free(Mmu->os, (gctPOINTER) area->mapLogical));


        gcmkVERIFY_OK(
            gckOS_FreeContiguous(Mmu->os,
                                 area->pageTablePhysical,
                                 (gctPOINTER) area->pageTableLogical,
                                 area->pageTableSize));
    }

    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Mmu->os, Mmu->pageTableMutex));
    }

    return status;
}
#endif

gctUINT32
_GetPageCountOfUsedNode(
    gctUINT32_PTR Node
    )
{
    gctUINT32 count;

    count = gcmENTRY_COUNT(*Node);

    if ((count << 8) == (~((1U<<8)-1)))
    {
        count = 1;
    }

    return count;
}

static gcsADDRESS_AREA_PTR
_GetProcessArea(
    IN gckMMU Mmu,
    IN gctBOOL Secure
    )
{
    gceADDRESS_AREA area = gcvADDRESS_AREA_NORMAL;

#if gcdENABLE_TRUST_APPLICATION
    if (Secure == gcvTRUE)
    {
        area = gcvADDRESS_AREA_SECURE;
    }
#endif

    return &Mmu->area[area];
}

/*******************************************************************************
**
**  _Construct
**
**  Construct a new gckMMU object.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      gctSIZE_T MmuSize
**          Number of bytes for the page table.
**
**  OUTPUT:
**
**      gckMMU * Mmu
**          Pointer to a variable that receives the gckMMU object pointer.
*/
gceSTATUS
_Construct(
    IN gckKERNEL Kernel,
    IN gctSIZE_T MmuSize,
    OUT gckMMU * Mmu
    )
{
    gckOS os;
    gckHARDWARE hardware;
    gceSTATUS status;
    gckMMU mmu = gcvNULL;
    gctUINT32_PTR map;
    gctPOINTER pointer = gcvNULL;
    gctUINT32 physBase;
    gctUINT32 physSize;
    gctUINT32 contiguousBase;
    gctUINT32 contiguousSize;
    gctUINT32 gpuAddress;
    gctPHYS_ADDR_T gpuPhysical;
    gcsADDRESS_AREA_PTR area = gcvNULL;

    gcmkHEADER_ARG("Kernel=0x%x MmuSize=%lu", Kernel, MmuSize);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(MmuSize > 0);
    gcmkVERIFY_ARGUMENT(Mmu != gcvNULL);

    /* Extract the gckOS object pointer. */
    os = Kernel->os;
    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    /* Extract the gckHARDWARE object pointer. */
    hardware = Kernel->hardware;
    gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

    /* Allocate memory for the gckMMU object. */
    gcmkONERROR(gckOS_Allocate(os, sizeof(struct _gckMMU), &pointer));

    gckOS_ZeroMemory(pointer, sizeof(struct _gckMMU));

    mmu = pointer;

    /* Initialize the gckMMU object. */
    mmu->object.type      = gcvOBJ_MMU;
    mmu->os               = os;
    mmu->hardware         = hardware;
    mmu->pageTableMutex   = gcvNULL;
    mmu->mtlbLogical      = gcvNULL;
    mmu->staticSTLB       = gcvNULL;
    mmu->enabled          = gcvFALSE;
    gcsLIST_Init(&mmu->hardwareList);


    area = &mmu->area[0];
    area->mapLogical       = gcvNULL;
    area->pageTableLogical = gcvNULL;

    /* Create the page table mutex. */
    gcmkONERROR(gckOS_CreateMutex(os, &mmu->pageTableMutex));


    if (hardware->mmuVersion == 0)
    {
        area->pageTableSize = MmuSize;

        /* Construct address space management table. */
        gcmkONERROR(gckOS_Allocate(mmu->os,
                                   area->pageTableSize,
                                   &pointer));

        area->mapLogical = pointer;

        /* Construct page table read by GPU. */
        gcmkONERROR(gckOS_AllocateContiguous(mmu->os,
                    gcvFALSE,
                    &area->pageTableSize,
                    &area->pageTablePhysical,
                    (gctPOINTER)&area->pageTableLogical));


        /* Compute number of entries in page table. */
        gcmkSAFECASTSIZET(area->pageTableEntries, area->pageTableSize / sizeof(gctUINT32));

        /* Mark all pages as free. */
        map      = area->mapLogical;

        _FillPageTable(area->pageTableLogical, area->pageTableEntries, mmu->safeAddress);

        map[0] = (area->pageTableEntries << 8) | gcvMMU_FREE;
        map[1] = ~0U;
        area->heapList  = 0;
        area->freeNodes = gcvFALSE;
    }
    else
    {
        /* Allocate the 4K mode MTLB table. */
        mmu->mtlbSize = gcdMMU_MTLB_SIZE;

        gcmkONERROR(
            gckOS_AllocateContiguous(os,
                                     gcvFALSE,
                                     &mmu->mtlbSize,
                                     &mmu->mtlbPhysical,
                                     &pointer));

        mmu->mtlbLogical = pointer;

        area->dynamicMappingStart = gcvINVALID_ADDRESS;

#if gcdPROCESS_ADDRESS_SPACE
        _FillPageTable(pointer, mmu->mtlbSize / 4, gcdMMU_MTLB_EXCEPTION);

        /* Allocate a array to store stlbs. */
        gcmkONERROR(gckOS_Allocate(os, mmu->mtlbSize, &mmu->stlbs));

        gckOS_ZeroMemory(mmu->stlbs, mmu->mtlbSize);

        for (i = 0; i < gcdMAX_GPU_COUNT; i++)
        {
            gcmkONERROR(gckOS_AtomConstruct(os, &mmu->pageTableDirty[i]));
        }

        _SetupProcessAddressSpace(mmu);

        /* Map kernel command buffer in MMU. */
        for (i = 0; i < gcdCOMMAND_QUEUES; i++)
        {
            gcmkONERROR(gckOS_GetPhysicalAddress(
                mmu->os,
                Kernel->command->queues[i].logical,
                &gpuPhysical
                ));

            gcmkSAFECASTPHYSADDRT(gpuAddress, gpuPhysical);

            gcmkONERROR(gckMMU_FlatMapping(mmu, gpuAddress, 1));
        }
#else
        /* Invalid all the entries. */
        gcmkONERROR(
            gckOS_ZeroMemory(pointer, mmu->mtlbSize));

        gcmkONERROR(
            gckOS_QueryOption(mmu->os, "physBase", &physBase));

        gcmkONERROR(
            gckOS_QueryOption(mmu->os, "physSize", &physSize));

        gcmkONERROR(
            gckOS_CPUPhysicalToGPUPhysical(mmu->os, physBase, &gpuPhysical));

        gcmkSAFECASTPHYSADDRT(gpuAddress, gpuPhysical);

        mmu->flatMappingStart = gpuAddress;
        mmu->flatMappingEnd   = gpuAddress + physSize;

        if (physSize)
        {
            /* Setup user specified flat mapping. */
            gcmkONERROR(_FillFlatMapping(mmu, gpuAddress, physSize));
        }

        status = gckOS_QueryOption(mmu->os, "contiguousBase", &contiguousBase);

        if (gcmIS_SUCCESS(status))
        {
            status = gckOS_QueryOption(mmu->os, "contiguousSize", &contiguousSize);

            if (gcmIS_SUCCESS(status))
            {
                if (contiguousSize)
                {
                    /* Setup flat mapping for reserved memory (VIDMEM). */
                    gcmkONERROR(_FillFlatMapping(mmu, contiguousBase, contiguousSize));
                }
            }
        }

        gcmkONERROR(_SetupDynamicSpace(mmu));
#endif
    }

    mmu->safePageSize = 4096;

    gcmkONERROR(gckOS_AllocateContiguous(
        os,
        gcvFALSE,
        &mmu->safePageSize,
        &mmu->safePagePhysical,
        &mmu->safePageLogical
        ));

    gcmkONERROR(gckOS_GetPhysicalAddress(os,
        mmu->safePageLogical,
        &gpuPhysical
        ));

    gcmkSAFECASTPHYSADDRT(mmu->safeAddress, gpuPhysical);

    gckOS_ZeroMemory(mmu->safePageLogical, mmu->safePageSize);

    gcmkONERROR(gckQUEUE_Allocate(os, &mmu->recentFreedAddresses, 16));

    /* Return the gckMMU object pointer. */
    *Mmu = mmu;

    /* Success. */
    gcmkFOOTER_ARG("*Mmu=0x%x", *Mmu);
    return gcvSTATUS_OK;

OnError:
    /* Roll back. */
    if (mmu != gcvNULL)
    {
        if (area != gcvNULL && area->mapLogical != gcvNULL)
        {
            gcmkVERIFY_OK(
                gckOS_Free(os, (gctPOINTER) area->mapLogical));


            gcmkVERIFY_OK(
                gckOS_FreeContiguous(os,
                                     area->pageTablePhysical,
                                     (gctPOINTER) area->pageTableLogical,
                                     area->pageTableSize));
        }

        if (mmu->mtlbLogical != gcvNULL)
        {
            gcmkVERIFY_OK(
                gckOS_FreeContiguous(os,
                                     mmu->mtlbPhysical,
                                     (gctPOINTER) mmu->mtlbLogical,
                                     mmu->mtlbSize));
        }

        if (mmu->pageTableMutex != gcvNULL)
        {
            /* Delete the mutex. */
            gcmkVERIFY_OK(
                gckOS_DeleteMutex(os, mmu->pageTableMutex));
        }

        gcmkVERIFY_OK(gckQUEUE_Free(os, &mmu->recentFreedAddresses));

        /* Mark the gckMMU object as unknown. */
        mmu->object.type = gcvOBJ_UNKNOWN;

        /* Free the allocates memory. */
        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(os, mmu));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  _Destroy
**
**  Destroy a gckMMU object.
**
**  INPUT:
**
**      gckMMU Mmu
**          Pointer to an gckMMU object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
_Destroy(
    IN gckMMU Mmu
    )
{
    gctUINT32 i;
    gcmkHEADER_ARG("Mmu=0x%x", Mmu);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Mmu, gcvOBJ_MMU);

    while (Mmu->staticSTLB != gcvNULL)
    {
        gcsMMU_STLB_PTR pre = Mmu->staticSTLB;
        Mmu->staticSTLB = pre->next;

        if (pre->physical != gcvNULL)
        {
            gcmkVERIFY_OK(
                gckOS_FreeContiguous(Mmu->os,
                    pre->physical,
                    pre->logical,
                    pre->size));
        }

        if (pre->mtlbEntryNum != 0)
        {
            gcmkASSERT(pre->mtlbEntryNum == 1);
            _WritePageEntry(Mmu->mtlbLogical + pre->mtlbIndex, 0);
#if gcdMMU_TABLE_DUMP
            gckOS_Print("%s(%d): clean MTLB[%d]\n",
                __FUNCTION__, __LINE__,
                pre->mtlbIndex);
#endif
        }

        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Mmu->os, pre));
    }

    if (Mmu->hardware->mmuVersion != 0)
    {
        gcmkVERIFY_OK(
                gckOS_FreeContiguous(Mmu->os,
                    Mmu->mtlbPhysical,
                    (gctPOINTER) Mmu->mtlbLogical,
                    Mmu->mtlbSize));
    }

    for (i = 0; i < gcvADDRESS_AREA_COUNT; i++)
    {
        gcsADDRESS_AREA_PTR area = &Mmu->area[i];

        /* Free address space management table. */
        if (area->mapLogical != gcvNULL)
        {
            gcmkVERIFY_OK(
                gckOS_Free(Mmu->os, (gctPOINTER) area->mapLogical));
        }

        if (area->pageTableLogical != gcvNULL)
        {
            /* Free page table. */
            gcmkVERIFY_OK(
                gckOS_FreeContiguous(Mmu->os,
                                     area->pageTablePhysical,
                                     (gctPOINTER) area->pageTableLogical,
                                     area->pageTableSize));
        }
    }

    /* Delete the page table mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Mmu->os, Mmu->pageTableMutex));

#if gcdPROCESS_ADDRESS_SPACE
    for (i = 0; i < Mmu->mtlbSize / 4; i++)
    {
        struct _gcsMMU_STLB *stlb = ((struct _gcsMMU_STLB **)Mmu->stlbs)[i];

        if (stlb)
        {
            gcmkVERIFY_OK(gckOS_FreeContiguous(
                Mmu->os,
                stlb->physical,
                stlb->logical,
                stlb->size));

            gcmkOS_SAFE_FREE(Mmu->os, stlb);
        }
    }

    gcmkOS_SAFE_FREE(Mmu->os, Mmu->stlbs);
#endif

    if (Mmu->safePageLogical != gcvNULL)
    {
        gcmkVERIFY_OK(gckOS_FreeContiguous(
            Mmu->os,
            Mmu->safePagePhysical,
            Mmu->safePageLogical,
            Mmu->safePageSize
            ));
    }

    gcmkVERIFY_OK(gckQUEUE_Free(Mmu->os, &Mmu->recentFreedAddresses));

    /* Mark the gckMMU object as unknown. */
    Mmu->object.type = gcvOBJ_UNKNOWN;

    /* Free the gckMMU object. */
    gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Mmu->os, Mmu));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
** _AdjstIndex
**
**  Adjust the index from which we search for a usable node to make sure
**  index allocated is greater than Start.
*/
gceSTATUS
_AdjustIndex(
    IN gckMMU Mmu,
    IN gctUINT32 Index,
    IN gctUINT32 PageCount,
    IN gctUINT32 Start,
    OUT gctUINT32 * IndexAdjusted
    )
{
    gceSTATUS status;
    gctUINT32 index = Index;
    gcsADDRESS_AREA_PTR area = &Mmu->area[0];
    gctUINT32_PTR map = area->mapLogical;

    gcmkHEADER();

    for (; index < area->pageTableEntries;)
    {
        gctUINT32 result = 0;
        gctUINT32 nodeSize = 0;

        if (index >= Start)
        {
            break;
        }

        switch (gcmENTRY_TYPE(map[index]))
        {
        case gcvMMU_SINGLE:
            nodeSize = 1;
            break;

        case gcvMMU_FREE:
            nodeSize = map[index] >> 8;
            break;

        default:
            gcmkFATAL("MMU table correcupted at index %u!", index);
            gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
        }

        if (nodeSize > PageCount)
        {
            result = index + (nodeSize - PageCount);

            if (result >= Start)
            {
                break;
            }
        }

        switch (gcmENTRY_TYPE(map[index]))
        {
        case gcvMMU_SINGLE:
            index = map[index] >> 8;
            break;

        case gcvMMU_FREE:
            index = map[index + 1];
            break;

        default:
            gcmkFATAL("MMU table correcupted at index %u!", index);
            gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
        }
    }

    *IndexAdjusted = index;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckMMU_Construct(
    IN gckKERNEL Kernel,
    IN gctSIZE_T MmuSize,
    OUT gckMMU * Mmu
    )
{
#if gcdSHARED_PAGETABLE
    gceSTATUS status;
    gctPOINTER pointer;

    gcmkHEADER_ARG("Kernel=0x%08x", Kernel);

    if (sharedPageTable == gcvNULL)
    {
        gcmkONERROR(
                gckOS_Allocate(Kernel->os,
                               sizeof(struct _gcsSharedPageTable),
                               &pointer));
        sharedPageTable = pointer;

        gcmkONERROR(
                gckOS_ZeroMemory(sharedPageTable,
                    sizeof(struct _gcsSharedPageTable)));

        gcmkONERROR(_Construct(Kernel, MmuSize, &sharedPageTable->mmu));
    }

    *Mmu = sharedPageTable->mmu;

    sharedPageTable->hardwares[sharedPageTable->reference] = Kernel->hardware;

    sharedPageTable->reference++;

    gcmkFOOTER_ARG("sharedPageTable->reference=%lu", sharedPageTable->reference);
    return gcvSTATUS_OK;

OnError:
    if (sharedPageTable)
    {
        if (sharedPageTable->mmu)
        {
            gcmkVERIFY_OK(gckMMU_Destroy(sharedPageTable->mmu));
        }

        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Kernel->os, sharedPageTable));
    }

    gcmkFOOTER();
    return status;
#else
    return _Construct(Kernel, MmuSize, Mmu);
#endif
}

gceSTATUS
gckMMU_Destroy(
    IN gckMMU Mmu
    )
{
#if gcdSHARED_PAGETABLE
    gckOS os = Mmu->os;

    sharedPageTable->reference--;

    if (sharedPageTable->reference == 0)
    {
        if (sharedPageTable->mmu)
        {
            gcmkVERIFY_OK(_Destroy(Mmu));
        }

        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(os, sharedPageTable));
    }

    return gcvSTATUS_OK;
#else
    return _Destroy(Mmu);
#endif
}

/*******************************************************************************
**
**  gckMMU_AllocatePages
**
**  Allocate pages inside the page table.
**
**  INPUT:
**
**      gckMMU Mmu
**          Pointer to an gckMMU object.
**
**      gctSIZE_T PageCount
**          Number of pages to allocate.
**
**  OUTPUT:
**
**      gctPOINTER * PageTable
**          Pointer to a variable that receives the base address of the page
**          table.
**
**      gctUINT32 * Address
**          Pointer to a variable that receives the hardware specific address.
*/
gceSTATUS
_AllocatePages(
    IN gckMMU Mmu,
    IN gctSIZE_T PageCount,
    IN gceSURF_TYPE Type,
    IN gctBOOL Secure,
    OUT gctPOINTER * PageTable,
    OUT gctUINT32 * Address
    )
{
    gceSTATUS status;
    gctBOOL mutex = gcvFALSE;
    gctUINT32 index = 0, previous = ~0U, left;
    gctUINT32_PTR map;
    gctBOOL gotIt;
    gctUINT32 address;
    gctUINT32 pageCount;
    gcsADDRESS_AREA_PTR area = _GetProcessArea(Mmu, Secure);

    gcmkHEADER_ARG("Mmu=0x%x PageCount=%lu", Mmu, PageCount);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Mmu, gcvOBJ_MMU);
    gcmkVERIFY_ARGUMENT(PageCount > 0);
    gcmkVERIFY_ARGUMENT(PageTable != gcvNULL);

    if (PageCount > area->pageTableEntries)
    {
        /* Not enough pages avaiable. */
        gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
    }

    gcmkSAFECASTSIZET(pageCount, PageCount);

#if gcdBOUNDARY_CHECK
    /* Extra pages as bounary. */
    pageCount += gcdBOUNDARY_CHECK * 2;
#endif

    /* Grab the mutex. */
    gcmkONERROR(gckOS_AcquireMutex(Mmu->os, Mmu->pageTableMutex, gcvINFINITE));
    mutex = gcvTRUE;

    /* Cast pointer to page table. */
    for (map = area->mapLogical, gotIt = gcvFALSE; !gotIt;)
    {
        index = area->heapList;

        if ((Mmu->hardware->mmuVersion == 0) && (Type == gcvSURF_VERTEX))
        {
            gcmkONERROR(_AdjustIndex(
                Mmu,
                index,
                pageCount,
                gcdVERTEX_START / gcmSIZEOF(gctUINT32),
                &index
                ));
        }

        /* Walk the heap list. */
        for (; !gotIt && (index < area->pageTableEntries);)
        {
            /* Check the node type. */
            switch (gcmENTRY_TYPE(map[index]))
            {
            case gcvMMU_SINGLE:
                /* Single odes are valid if we only need 1 page. */
                if (pageCount == 1)
                {
                    gotIt = gcvTRUE;
                }
                else
                {
                    /* Move to next node. */
                    previous = index;
                    index    = map[index] >> 8;
                }
                break;

            case gcvMMU_FREE:
                /* Test if the node has enough space. */
                if (pageCount <= (map[index] >> 8))
                {
                    gotIt = gcvTRUE;
                }
                else
                {
                    /* Move to next node. */
                    previous = index;
                    index    = map[index + 1];
                }
                break;

            default:
                gcmkFATAL("MMU table correcupted at index %u!", index);
                gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
            }
        }

        /* Test if we are out of memory. */
        if (index >= area->pageTableEntries)
        {
            if (area->freeNodes)
            {
                /* Time to move out the trash! */
                gcmkONERROR(_Collect(area));

                /* We are going to search from start, so reset previous to start. */
                previous = ~0U;
            }
            else
            {
                /* Out of resources. */
                gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
            }
        }
    }

    switch (gcmENTRY_TYPE(map[index]))
    {
    case gcvMMU_SINGLE:
        /* Unlink single node from free list. */
        gcmkONERROR(
            _Link(area, previous, map[index] >> 8));
        break;

    case gcvMMU_FREE:
        /* Check how many pages will be left. */
        left = (map[index] >> 8) - pageCount;
        switch (left)
        {
        case 0:
            /* The entire node is consumed, just unlink it. */
            gcmkONERROR(
                _Link(area, previous, map[index + 1]));
            break;

        case 1:
            /* One page will remain.  Convert the node to a single node and
            ** advance the index. */
            map[index] = (map[index + 1] << 8) | gcvMMU_SINGLE;
            index ++;
            break;

        default:
            /* Enough pages remain for a new node.  However, we will just adjust
            ** the size of the current node and advance the index. */
            map[index] = (left << 8) | gcvMMU_FREE;
            index += left;
            break;
        }
        break;
    }

    /* Mark node as used. */
    gcmkONERROR(_FillMap(&map[index], pageCount, gcvMMU_USED));

#if gcdBOUNDARY_CHECK
    index += gcdBOUNDARY_CHECK;
#endif

    /* Record pageCount of allocated node at the beginning of node. */
    if (pageCount == 1)
    {
        map[index] = (~((1U<<8)-1)) | gcvMMU_USED;
    }
    else
    {
        map[index] = (pageCount << 8) | gcvMMU_USED;
    }

    if (area->pageTableLogical != gcvNULL)
    {
    /* Return pointer to page table. */
    *PageTable = &area->pageTableLogical[index];
    }
    else
    {
        /* Page table for secure area is handled in trust application. */
        *PageTable = gcvNULL;
    }

    /* Build virtual address. */
    if (Mmu->hardware->mmuVersion == 0)
    {
        gcmkONERROR(
                gckHARDWARE_BuildVirtualAddress(Mmu->hardware, index, 0, &address));
    }
    else
    {
        gctUINT32 masterOffset = index / gcdMMU_STLB_4K_ENTRY_NUM
                               + area->dynamicMappingStart;
        gctUINT32 slaveOffset = index % gcdMMU_STLB_4K_ENTRY_NUM;

        address = (masterOffset << gcdMMU_MTLB_SHIFT)
                | (slaveOffset << gcdMMU_STLB_4K_SHIFT);
    }

    if (Address != gcvNULL)
    {
        *Address = address;
    }

    /* Release the mutex. */
    gcmkVERIFY_OK(gckOS_ReleaseMutex(Mmu->os, Mmu->pageTableMutex));

    /* Success. */
    gcmkFOOTER_ARG("*PageTable=0x%x *Address=%08x",
                   *PageTable, gcmOPT_VALUE(Address));
    return gcvSTATUS_OK;

OnError:

    if (mutex)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Mmu->os, Mmu->pageTableMutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckMMU_FreePages
**
**  Free pages inside the page table.
**
**  INPUT:
**
**      gckMMU Mmu
**          Pointer to an gckMMU object.
**
**      gctPOINTER PageTable
**          Base address of the page table to free.
**
**      gctSIZE_T PageCount
**          Number of pages to free.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
_FreePages(
    IN gckMMU Mmu,
    IN gctBOOL Secure,
    IN gctUINT32 Address,
    IN gctPOINTER PageTable,
    IN gctSIZE_T PageCount
    )
{
    gctUINT32_PTR node;
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;
    gctUINT32 pageCount;
    gcuQUEUEDATA data;
    gcsADDRESS_AREA_PTR area = _GetProcessArea(Mmu, Secure);

    gcmkHEADER_ARG("Mmu=0x%x PageTable=0x%x PageCount=%lu",
                   Mmu, PageTable, PageCount);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Mmu, gcvOBJ_MMU);
    gcmkVERIFY_ARGUMENT(PageCount > 0);

    gcmkSAFECASTSIZET(pageCount, PageCount);

#if gcdBOUNDARY_CHECK
    pageCount += gcdBOUNDARY_CHECK * 2;
#endif

    /* Get the node by index. */
    node = area->mapLogical + ((gctUINT32_PTR)PageTable - area->pageTableLogical);

#if gcdBOUNDARY_CHECK
    node -= gcdBOUNDARY_CHECK;
#endif

    gcmkONERROR(gckOS_AcquireMutex(Mmu->os, Mmu->pageTableMutex, gcvINFINITE));
    acquired = gcvTRUE;

    if (Mmu->hardware->mmuVersion == 0)
    {
        _FillPageTable(PageTable, pageCount, Mmu->safeAddress);
    }

    if (PageCount != _GetPageCountOfUsedNode(node))
    {
        gcmkONERROR(gcvSTATUS_INVALID_REQUEST);
    }

    if (PageCount == 1)
    {
       /* Single page node. */
        node[0] = (~((1U<<8)-1)) | gcvMMU_SINGLE;

        if (PageTable != gcvNULL)
        {
#if gcdUSE_MMU_EXCEPTION
        /* Enable exception */
        _WritePageEntry(PageTable, (1 << 1));
#else
        _WritePageEntry(PageTable, 0);
#endif
    }
    }
    else
    {
        /* Mark the node as free. */
        node[0] = (pageCount << 8) | gcvMMU_FREE;
        node[1] = ~0U;

        if (PageTable != gcvNULL)
        {
#if gcdUSE_MMU_EXCEPTION
        /* Enable exception */
        gcmkVERIFY_OK(_FillPageTable(PageTable, pageCount, 1 << 1));
#else
        gcmkVERIFY_OK(_FillPageTable(PageTable, pageCount, 0));
#endif
    }
    }

    /* We have free nodes. */
    area->freeNodes = gcvTRUE;

    /* Record freed address range. */
    data.addressData.start = Address;
    data.addressData.end = Address + (gctUINT32)PageCount * 4096;
    gckQUEUE_Enqueue(&Mmu->recentFreedAddresses, &data);

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Mmu->os, Mmu->pageTableMutex));
    acquired = gcvFALSE;

#if gcdENABLE_TRUST_APPLICATION
    if (Mmu->hardware->secureMode == gcvSECURE_IN_TA)
    {
        gckKERNEL_SecurityUnmapMemory(Mmu->hardware->kernel, Address, (gctUINT32)PageCount);
    }
#endif

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Mmu->os, Mmu->pageTableMutex));
    }

    gcmkFOOTER();
    return status;
}

gceSTATUS
gckMMU_AllocatePages(
    IN gckMMU Mmu,
    IN gctSIZE_T PageCount,
    OUT gctPOINTER * PageTable,
    OUT gctUINT32 * Address
    )
{
    return gckMMU_AllocatePagesEx(
                Mmu, PageCount, gcvSURF_TYPE_UNKNOWN, gcvFALSE, PageTable, Address);
}

gceSTATUS
gckMMU_AllocatePagesEx(
    IN gckMMU Mmu,
    IN gctSIZE_T PageCount,
    IN gceSURF_TYPE Type,
    IN gctBOOL Secure,
    OUT gctPOINTER * PageTable,
    OUT gctUINT32 * Address
    )
{
#if gcdDISABLE_GPU_VIRTUAL_ADDRESS
    gcmkPRINT("GPU virtual address is disabled.");
    return gcvSTATUS_NOT_SUPPORTED;
#else
    return _AllocatePages(Mmu, PageCount, Type, Secure, PageTable, Address);
#endif
}

gceSTATUS
gckMMU_FreePages(
    IN gckMMU Mmu,
    IN gctBOOL Secure,
    IN gctUINT32 Address,
    IN gctPOINTER PageTable,
    IN gctSIZE_T PageCount
    )
{
    return _FreePages(Mmu, Secure, Address, PageTable, PageCount);
}

gceSTATUS
gckMMU_SetPage(
    IN gckMMU Mmu,
    IN gctPHYS_ADDR_T PageAddress,
    IN gctBOOL Writable,
    IN gctUINT32 *PageEntry
    )
{
    gctUINT32 addressExt;
    gctUINT32 address;

    gcmkHEADER_ARG("Mmu=0x%x", Mmu);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Mmu, gcvOBJ_MMU);
    gcmkVERIFY_ARGUMENT(PageEntry != gcvNULL);
    gcmkVERIFY_ARGUMENT(!(PageAddress & 0xFFF));

    /* [31:0]. */
    address    = (gctUINT32)(PageAddress & 0xFFFFFFFF);
    /* [39:32]. */
    addressExt = (gctUINT32)((PageAddress >> 32) & 0xFF);

    if (Mmu->hardware->mmuVersion == 0)
    {
        _WritePageEntry(PageEntry, address);
    }
    else
    {
        _WritePageEntry(PageEntry, _SetPage(address, addressExt, gcvTRUE));
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

#if gcdPROCESS_ADDRESS_SPACE
gceSTATUS
gckMMU_GetPageEntry(
    IN gckMMU Mmu,
    IN gctUINT32 Address,
    IN gctUINT32_PTR *PageTable
    )
{
    gceSTATUS status;
    struct _gcsMMU_STLB *stlb;
    struct _gcsMMU_STLB **stlbs = Mmu->stlbs;
    gctUINT32 offset = _MtlbOffset(Address);
    gctUINT32 mtlbEntry;
    gctBOOL ace = gckHARDWARE_IsFeatureAvailable(Mmu->hardware, gcvFEATURE_ACE);

    gcmkHEADER_ARG("Mmu=0x%x", Mmu);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Mmu, gcvOBJ_MMU);
    gcmkVERIFY_ARGUMENT((Address & 0xFFF) == 0);

    stlb = stlbs[offset];

    if (stlb == gcvNULL)
    {
        gcmkONERROR(_AllocateStlb(Mmu->os, &stlb));

        mtlbEntry = stlb->physBase
                  | gcdMMU_MTLB_4K_PAGE
                  | gcdMMU_MTLB_PRESENT
                  ;

        /* Insert Slave TLB address to Master TLB entry.*/
        _WritePageEntry(Mmu->mtlbLogical + offset, mtlbEntry);

        /* Record stlb. */
        stlbs[offset] = stlb;
    }

    *PageTable = &stlb->logical[_StlbOffset(Address)];

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

gceSTATUS
_CheckMap(
    IN gckMMU Mmu
    )
{
    gceSTATUS status;
    gctUINT32_PTR map = area->mapLogical;
    gctUINT32 index;

    for (index = area->heapList; index < area->pageTableEntries;)
    {
        /* Check the node type. */
        switch (gcmENTRY_TYPE(map[index]))
        {
        case gcvMMU_SINGLE:
            /* Move to next node. */
            index    = map[index] >> 8;
            break;

        case gcvMMU_FREE:
            /* Move to next node. */
            index    = map[index + 1];
            break;

        default:
            gcmkFATAL("MMU table correcupted at index [%u] = %x!", index, map[index]);
            gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
        }
    }

    return gcvSTATUS_OK;

OnError:
    return status;
}

gceSTATUS
gckMMU_FlatMapping(
    IN gckMMU Mmu,
    IN gctUINT32 Physical,
    IN gctUINT32 NumPages
    )
{
    gceSTATUS status;
    gctUINT32 index = _AddressToIndex(Mmu, Physical);
    gctUINT32 i;
    gctUINT32_PTR pageTable;

    for (i = 0; i < NumPages; i++)
    {
        gckMMU_GetPageEntry(Mmu, Physical + i * 4096, &pageTable);

        _WritePageEntry(pageTable, _SetPage(Physical + i * 4096, 0));
    }

    gcmkONERROR(_FillFlatMappingInMap(Mmu, index, NumPages));

    return gcvSTATUS_OK;

OnError:

    /* Roll back. */
    return status;
}

gceSTATUS
gckMMU_FreePagesEx(
    IN gckMMU Mmu,
    IN gctUINT32 Address,
    IN gctSIZE_T PageCount
    )
{
    gctUINT32_PTR node;
    gceSTATUS status;

#if gcdUSE_MMU_EXCEPTION
    gctUINT32 i;
    struct _gcsMMU_STLB *stlb;
    struct _gcsMMU_STLB **stlbs = Mmu->stlbs;
#endif

    gcmkHEADER_ARG("Mmu=0x%x Address=0x%x PageCount=%lu",
                   Mmu, Address, PageCount);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Mmu, gcvOBJ_MMU);
    gcmkVERIFY_ARGUMENT(PageCount > 0);

    /* Get the node by index. */
    node = area->mapLogical + _AddressToIndex(Mmu, Address);

    gcmkONERROR(gckOS_AcquireMutex(Mmu->os, Mmu->pageTableMutex, gcvINFINITE));

    if (PageCount == 1)
    {
       /* Single page node. */
        node[0] = (~((1U<<8)-1)) | gcvMMU_SINGLE;
    }
    else
    {
        /* Mark the node as free. */
        node[0] = (PageCount << 8) | gcvMMU_FREE;
        node[1] = ~0U;
    }

    /* We have free nodes. */
    area->freeNodes = gcvTRUE;

#if gcdUSE_MMU_EXCEPTION
    for (i = 0; i < PageCount; i++)
    {
        /* Get */
        stlb = stlbs[_MtlbOffset(Address)];

        /* Enable exception */
        stlb->logical[_StlbOffset(Address)] = gcdMMU_STLB_EXCEPTION;
    }
#endif

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Mmu->os, Mmu->pageTableMutex));


    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}
#endif

gceSTATUS
gckMMU_Flush(
    IN gckMMU Mmu,
    IN gceSURF_TYPE Type
    )
{
#if !gcdPROCESS_ADDRESS_SPACE
    gckHARDWARE hardware;
#endif
    gctUINT32 mask;
    gctINT i;

    if (Type == gcvSURF_VERTEX || Type == gcvSURF_INDEX)
    {
        mask = gcvPAGE_TABLE_DIRTY_BIT_FE;
    }
    else
    {
        mask = gcvPAGE_TABLE_DIRTY_BIT_OTHER;
    }

    i = 0;

#if gcdPROCESS_ADDRESS_SPACE
    for (i = 0; i < gcdMAX_GPU_COUNT; i++)
    {
        gcmkVERIFY_OK(
            gckOS_AtomSetMask(Mmu->pageTableDirty[i], mask));
    }
#else
#if gcdSHARED_PAGETABLE
    for (i = 0; i < gcdMAX_GPU_COUNT; i++)
    {
        hardware = sharedPageTable->hardwares[i];
        if (hardware)
        {
            gcmkVERIFY_OK(gckOS_AtomSetMask(hardware->pageTableDirty, mask));
        }
    }
#else
    hardware = Mmu->hardware;
    gcmkVERIFY_OK(
        gckOS_AtomSetMask(hardware->pageTableDirty, mask));

    {
        gcsLISTHEAD_PTR hardwareHead;
        gcmkLIST_FOR_EACH(hardwareHead, &Mmu->hardwareList)
        {
            hardware = gcmCONTAINEROF(hardwareHead, _gckHARDWARE, mmuHead);

            if (hardware != Mmu->hardware)
            {
                gcmkVERIFY_OK(
                    gckOS_AtomSetMask(hardware->pageTableDirty, mask));
            }
        }
    }
#endif
#endif

    return gcvSTATUS_OK;
}

gceSTATUS
gckMMU_DumpPageTableEntry(
    IN gckMMU Mmu,
    IN gctUINT32 Address
    )
{
#if gcdPROCESS_ADDRESS_SPACE
    gcsMMU_STLB_PTR *stlbs = Mmu->stlbs;
    gcsMMU_STLB_PTR stlbDesc = stlbs[_MtlbOffset(Address)];
#else
    gctUINT32_PTR pageTable;
    gctUINT32 index;
    gctUINT32 mtlb, stlb;
#endif
    gcsADDRESS_AREA_PTR area = &Mmu->area[0];

    gcmkHEADER_ARG("Mmu=0x%08X Address=0x%08X", Mmu, Address);
    gcmkVERIFY_OBJECT(Mmu, gcvOBJ_MMU);

    gcmkASSERT(Mmu->hardware->mmuVersion > 0);

#if gcdPROCESS_ADDRESS_SPACE
    if (stlbDesc)
    {
        gcmkPRINT("    STLB entry = 0x%08X",
                  _ReadPageEntry(&stlbDesc->logical[_StlbOffset(Address)]));
    }
    else
    {
        gcmkPRINT("    MTLB entry is empty.");
    }
#else
    mtlb   = (Address & gcdMMU_MTLB_MASK) >> gcdMMU_MTLB_SHIFT;

    if (mtlb >= area->dynamicMappingStart)
    {
        stlb   = (Address & gcdMMU_STLB_4K_MASK) >> gcdMMU_STLB_4K_SHIFT;

        pageTable = area->pageTableLogical;

        index = (mtlb - area->dynamicMappingStart)
              * gcdMMU_STLB_4K_ENTRY_NUM
              + stlb;

        gcmkPRINT("    Page table entry = 0x%08X", _ReadPageEntry(pageTable + index));
    }
    else
    {
        gcsMMU_STLB_PTR stlbObj = Mmu->staticSTLB;
        gctUINT32 entry = Mmu->mtlbLogical[mtlb];

        stlb = (Address & gcdMMU_STLB_64K_MASK) >> gcdMMU_STLB_64K_SHIFT;

        entry &= 0xFFFFFFF0;

        while (stlbObj)
        {

            if (entry == stlbObj->physBase)
            {
                gcmkPRINT("    Page table entry = 0x%08X", stlbObj->logical[stlb]);
                break;
            }

            stlbObj = stlbObj->next;
        }
    }
#endif

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

void
gckMMU_CheckSaftPage(
    IN gckMMU Mmu
    )
{
    gctUINT8_PTR safeLogical = Mmu->safePageLogical;
    gctUINT32 offsets[] = {
        0,
        64,
        128,
        256,
        2560,
        4000
    };

    gctUINT32 i = 0;

    while (i < gcmCOUNTOF(offsets))
    {
        if (safeLogical[offsets[i]] != 0)
        {
            gcmkPRINT("%s(%d) safe page is over written [%d] = %x",
                      __FUNCTION__, __LINE__, i, safeLogical[offsets[i]]);
        }
    }
}

void
gckMMU_DumpAddressSpace(
    IN gckMMU Mmu
    )
{
    gctUINT i;
    gctUINT next;
    gcsADDRESS_AREA_PTR area = &Mmu->area[0];
    gctUINT32_PTR map = area->mapLogical;
    gctBOOL used = gcvFALSE;
    gctUINT32 numPages;

    /* Grab the mutex. */
    gcmkVERIFY_OK(gckOS_AcquireMutex(Mmu->os, Mmu->pageTableMutex, gcvINFINITE));

    /* Find node which contains index. */
    for (i = 0; i < area->pageTableEntries; i = next)
    {
        switch (gcmENTRY_TYPE(map[i]))
        {
        case gcvMMU_SINGLE:
            numPages = 1;
            next = i + numPages;
            used = gcvFALSE;
            break;

        case gcvMMU_FREE:
            numPages = map[i] >> 8;
            next = i + numPages;
            used = gcvFALSE;
            break;

        case gcvMMU_USED:
            numPages = 1;
            next = i + numPages;
            used = gcvTRUE;
            break;

        default:
            gcmkFATAL("MMU table correcupted at index %u!", i);
            return;
        }

        if (!used)
        {
            gcmkPRINT("Available Range [%d - %d)", i, i + numPages);
        }
    }

    /* Release the mutex. */
    gcmkVERIFY_OK(gckOS_ReleaseMutex(Mmu->os, Mmu->pageTableMutex));

}

void
gckMMU_DumpRecentFreedAddress(
    IN gckMMU Mmu
    )
{
    gckQUEUE queue = &Mmu->recentFreedAddresses;
    gctUINT32 i;
    gcuQUEUEDATA *data;

    if (queue->count)
    {
        gcmkPRINT("    Recent %d freed GPU address ranges:", queue->count);

        for (i = 0; i < queue->count; i++)
        {
            gckQUEUE_GetData(queue, i, &data);

            gcmkPRINT("      [%08X - %08X]", data->addressData.start, data->addressData.end);
        }
    }
}

gceSTATUS
gckMMU_FillFlatMapping(
    IN gckMMU Mmu,
    IN gctUINT32 PhysBase,
    IN gctSIZE_T Size
    )
{
    gceSTATUS status;
    gckHARDWARE hardware = Mmu->hardware;

    if (hardware->mmuVersion)
    {
        gcmkONERROR(_FillFlatMapping(Mmu, PhysBase, Size));
    }

    return gcvSTATUS_OK;

OnError:
    return status;
}

gceSTATUS
gckMMU_IsFlatMapped(
    IN gckMMU Mmu,
    OUT gctUINT32 Physical,
    OUT gctBOOL *In
    )
{
    gceSTATUS status;

    gcmkHEADER();

    gcmkVERIFY_ARGUMENT(In != gcvNULL);

    if (gckHARDWARE_IsFeatureAvailable(Mmu->hardware, gcvFEATURE_MMU) == gcvFALSE)
    {
        gcmkONERROR(gcvSTATUS_NOT_SUPPORTED);
    }

    *In = (Physical >= Mmu->flatMappingStart) && (Physical < Mmu->flatMappingEnd)
        ? gcvTRUE
        : gcvFALSE;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckMMU_AttachHardware(
    IN gckMMU Mmu,
    IN gckHARDWARE Hardware
    )
{
    gcmkHEADER();

    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    gcmkTRACE_ZONE(gcvLEVEL_INFO, _GC_OBJ_ZONE, "Attach core %d", Hardware->core);

    gcsLIST_Add(&Hardware->mmuHead, &Mmu->hardwareList);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/******************************************************************************
****************************** T E S T   C O D E ******************************
******************************************************************************/

