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


#include "gc_hal_types.h"
#include "gc_hal_base.h"
#include "gc_hal_security_interface.h"
#include "gc_hal_ta.h"
#include "gc_hal.h"

#define _GC_OBJ_ZONE 2
/*******************************************************************************
************************************ Define ************************************
********************************************************************************/

#define gcdMMU_MTLB_SHIFT           22
#define gcdMMU_STLB_4K_SHIFT        12
#define gcdMMU_STLB_64K_SHIFT       16

#define gcdMMU_MTLB_BITS            (32 - gcdMMU_MTLB_SHIFT)
#define gcdMMU_PAGE_4K_BITS         gcdMMU_STLB_4K_SHIFT
#define gcdMMU_STLB_4K_BITS         (32 - gcdMMU_MTLB_BITS - gcdMMU_PAGE_4K_BITS)
#define gcdMMU_PAGE_64K_BITS        gcdMMU_STLB_64K_SHIFT
#define gcdMMU_STLB_64K_BITS        (32 - gcdMMU_MTLB_BITS - gcdMMU_PAGE_64K_BITS)

#define gcdMMU_MTLB_ENTRY_NUM       (1 << gcdMMU_MTLB_BITS)
#define gcdMMU_MTLB_SIZE            (gcdMMU_MTLB_ENTRY_NUM << 2)
#define gcdMMU_STLB_4K_ENTRY_NUM    (1 << gcdMMU_STLB_4K_BITS)
#define gcdMMU_STLB_4K_SIZE         (gcdMMU_STLB_4K_ENTRY_NUM << 2)
#define gcdMMU_PAGE_4K_SIZE         (1 << gcdMMU_STLB_4K_SHIFT)
#define gcdMMU_STLB_64K_ENTRY_NUM   (1 << gcdMMU_STLB_64K_BITS)
#define gcdMMU_STLB_64K_SIZE        (gcdMMU_STLB_64K_ENTRY_NUM << 2)
#define gcdMMU_PAGE_64K_SIZE        (1 << gcdMMU_STLB_64K_SHIFT)

#define gcdMMU_MTLB_MASK            (~((1U << gcdMMU_MTLB_SHIFT)-1))
#define gcdMMU_STLB_4K_MASK         ((~0U << gcdMMU_STLB_4K_SHIFT) ^ gcdMMU_MTLB_MASK)
#define gcdMMU_PAGE_4K_MASK         (gcdMMU_PAGE_4K_SIZE - 1)
#define gcdMMU_STLB_64K_MASK        ((~((1U << gcdMMU_STLB_64K_SHIFT)-1)) ^ gcdMMU_MTLB_MASK)
#define gcdMMU_PAGE_64K_MASK        (gcdMMU_PAGE_64K_SIZE - 1)

/* Page offset definitions. */
#define gcdMMU_OFFSET_4K_BITS       (32 - gcdMMU_MTLB_BITS - gcdMMU_STLB_4K_BITS)
#define gcdMMU_OFFSET_4K_MASK       ((1U << gcdMMU_OFFSET_4K_BITS) - 1)
#define gcdMMU_OFFSET_16K_BITS      (32 - gcdMMU_MTLB_BITS - gcdMMU_STLB_16K_BITS)
#define gcdMMU_OFFSET_16K_MASK      ((1U << gcdMMU_OFFSET_16K_BITS) - 1)

#define gcdMMU_MTLB_PRESENT         0x00000001
#define gcdMMU_MTLB_EXCEPTION       0x00000002
#define gcdMMU_MTLB_4K_PAGE         0x00000000

#define gcdMMU_STLB_PRESENT         0x00000001
#define gcdMMU_STLB_EXCEPTION       0x00000002
#define gcdMMU_STLB_SECURITY        (1 << 4)
#define gcdMMU_STLB_4K_PAGE         0x00000000

#define gcdUSE_MMU_EXCEPTION        1

#define gcdMMU_SECURE_AREA_START    ((gcdMMU_MTLB_ENTRY_NUM - gcdMMU_SECURE_AREA_SIZE) << gcdMMU_MTLB_SHIFT)

typedef enum _gceMMU_TYPE
{
    gcvMMU_USED     = (0 << 4),
    gcvMMU_SINGLE   = (1 << 4),
    gcvMMU_FREE     = (2 << 4),
}
gceMMU_TYPE;

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


#define gcmENTRY_TYPE(x) (x & 0xF0)
/*
* We need flat mapping ta command buffer.

*/

/*
* Helper
*/
gctUINT32
_MtlbOffset(
    gctUINT32 Address
    )
{
    return (Address & gcdMMU_MTLB_MASK) >> gcdMMU_MTLB_SHIFT;
}

gctUINT32
_StlbOffset(
    gctUINT32 Address
    )
{
    return (Address & gcdMMU_STLB_4K_MASK) >> gcdMMU_STLB_4K_SHIFT;
}

static gctUINT32
_SetPage(gctUINT32 PageAddress)
{
    return PageAddress
           /* writable */
           | (1 << 2)
           /* Ignore exception */
           | (0 << 1)
           /* Present */
           | (1 << 0);
}

static void
_WritePageEntry(
    IN gctUINT32_PTR PageEntry,
    IN gctUINT32     EntryValue
    )
{
    *PageEntry = EntryValue;

    gctaOS_CacheClean((gctUINT8_PTR)PageEntry, gcmSIZEOF(gctUINT32));
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
_AllocateStlb(
    IN gctaOS Os,
    OUT gcsMMU_STLB_PTR *Stlb
    )
{
    gceSTATUS status;
    gcsMMU_STLB_PTR stlb;
    gctPOINTER pointer;

    /* Allocate slave TLB record. */
    gcmkONERROR(gctaOS_Allocate(gcmSIZEOF(gcsMMU_STLB), &pointer));
    stlb = pointer;

    stlb->size = gcdMMU_STLB_4K_SIZE;

    /* Allocate slave TLB entries. */
    gcmkONERROR(gctaOS_AllocateSecurityMemory(
        Os,
        &stlb->size,
        (gctPOINTER *)&stlb->logical,
        &stlb->physical
        ));

    gcmkONERROR(gctaOS_GetPhysicalAddress(Os, stlb->logical, &stlb->physBase));

#if gcdUSE_MMU_EXCEPTION
    _FillPageTable(stlb->logical, stlb->size / 4, gcdMMU_STLB_EXCEPTION);
#else
    gctaOS_ZeroMemory(stlb->logical, stlb->size);
#endif

    *Stlb = stlb;

    return gcvSTATUS_OK;

OnError:
    return status;
}

gceSTATUS
gctaMMU_Construct(
    IN gcTA TA,
    OUT gcTA_MMU *Mmu
    )
{
    gceSTATUS status;
    gctSIZE_T bytes = 4096;

    gcTA_MMU mmu;

    gcmkONERROR(gctaOS_Allocate(
        gcmSIZEOF(gcsTA_MMU),
        (gctPOINTER *)&mmu
        ));

    mmu->os = TA->os;

    /* MTLB bytes. */
    mmu->mtlbBytes = gcdMMU_MTLB_SIZE;

    /* Allocate MTLB. */
    gcmkONERROR(gctaOS_AllocateSecurityMemory(
        TA->os,
        &mmu->mtlbBytes,
        &mmu->mtlbLogical,
        &mmu->mtlbPhysical
        ));

    /* Allocate a array to store stlbs. */
    gcmkONERROR(gctaOS_Allocate(mmu->mtlbBytes, &mmu->stlbs));

    gctaOS_ZeroMemory((gctUINT8_PTR)mmu->stlbs, mmu->mtlbBytes);

    /* Allocate security safe page. */
    gcmkONERROR(gctaOS_AllocateSecurityMemory(
        TA->os,
        &bytes,
        &mmu->safePageLogical,
        &mmu->safePagePhysical
        ));

    gctaOS_ZeroMemory((gctUINT8_PTR)mmu->safePageLogical, bytes);

    /* Allocate non security safe page. */
    gcmkONERROR(gctaOS_AllocateSecurityMemory(
        TA->os,
        &bytes,
        &mmu->nonSecureSafePageLogical,
        &mmu->nonSecureSafePagePhysical
        ));

    gctaOS_ZeroMemory((gctUINT8_PTR)mmu->nonSecureSafePageLogical, bytes);

    /* gcmkONERROR(gctaOS_CreateMutex(TA->os, &mmu->mutex)); */

    *Mmu = mmu;

    return gcvSTATUS_OK;

OnError:
    return status;
}

gceSTATUS
gctaMMU_Destory(
    IN gcTA_MMU Mmu
    )
{
    gctaOS os = Mmu->os;

    if (Mmu->safePageLogical)
    {
        gcmkVERIFY_OK(gctaOS_FreeSecurityMemory(
            os,
            4096,
            Mmu->safePageLogical,
            Mmu->safePagePhysical
            ));
    }

    if (Mmu->nonSecureSafePageLogical)
    {
        gcmkVERIFY_OK(gctaOS_FreeSecurityMemory(
            os,
            4096,
            Mmu->nonSecureSafePageLogical,
            Mmu->nonSecureSafePagePhysical
            ));
    }

    if (Mmu->mtlbLogical)
    {
        gcmkVERIFY_OK(gctaOS_FreeSecurityMemory(
            os,
            4096,
            Mmu->mtlbLogical,
            Mmu->mtlbPhysical
            ));
    }

    gcmkVERIFY_OK(gctaOS_Free(Mmu));

    return gcvSTATUS_OK;
}

gceSTATUS
gctaMMU_GetPageEntry(
    IN gcTA_MMU Mmu,
    IN gctUINT32 Address,
    OUT gctUINT32_PTR *PageTable,
    OUT gctBOOL * Secure
    )
{
    gceSTATUS status;
    struct _gcsMMU_STLB *stlb;
    struct _gcsMMU_STLB **stlbs = (struct _gcsMMU_STLB **)Mmu->stlbs;
    gctUINT32 offset = _MtlbOffset(Address);
    gctUINT32 mtlbEntry;
    gctBOOL secure = Address > gcdMMU_SECURE_AREA_START;

    gcmkHEADER_ARG("Mmu=0x%x", Mmu);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT((Address & 0xFFF) == 0);

    stlb = stlbs[offset];

    if (stlb == gcvNULL)
    {
        gcmkONERROR(_AllocateStlb(Mmu->os, &stlb));

        mtlbEntry = (gctUINT32)(stlb->physBase & 0xFFFFFFFF)
                  | gcdMMU_MTLB_4K_PAGE
                  | gcdMMU_MTLB_PRESENT
                  ;

        if (secure)
        {
            /* Secure MTLB. */
            mtlbEntry |= (1 << 4);
        }

        /* Insert Slave TLB address to Master TLB entry.*/
        _WritePageEntry((gctUINT32_PTR)Mmu->mtlbLogical + offset, mtlbEntry);

        /* Record stlb. */
        stlbs[offset] = stlb;
    }

    *PageTable = &stlb->logical[_StlbOffset(Address)];

    if (Secure)
    {
        *Secure = secure;
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

gceSTATUS
gctaMMU_SetPage(
    IN gcTA_MMU Mmu,
    IN gctUINT32 PageAddress,
    IN gctUINT32 *PageEntry
    )
{
    /* gctBOOL secure; */

    gcmkHEADER_ARG("Mmu=0x%x", Mmu);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(PageEntry != gcvNULL);
    gcmkVERIFY_ARGUMENT(!(PageAddress & 0xFFF));

    _WritePageEntry(PageEntry, _SetPage(PageAddress));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

gceSTATUS
gctaMMU_FreePages(
    IN gcTA_MMU Mmu,
    IN gctUINT32 Address,
    IN gctUINT32 PageCount
    )
{
    gceSTATUS status;
    gctUINT32 i;
    gctUINT32_PTR entry;
    gcmkHEADER_ARG("Mmu=0x%x", Mmu);

    /* Fill in page table. */
    for (i = 0; i < PageCount; i++)
    {
        gcmkONERROR(gctaMMU_GetPageEntry(Mmu, Address, &entry, gcvNULL));

#if gcdUSE_MMU_EXCEPTION
        *entry = gcdMMU_STLB_EXCEPTION;
#else
        *entry = 0;
#endif

        Address += 4096;
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

gceSTATUS
gctaMMU_Enable(
    IN gcTA_MMU Mmu,
    IN gcTA TA
    )
{
    gceSTATUS status;
    gctPHYS_ADDR_T address;
    gctPHYS_ADDR_T safeAddress;

    gcmkONERROR(gctaOS_GetPhysicalAddress(Mmu->os, Mmu->mtlbLogical, &address));

    gctaOS_GetPhysicalAddress(Mmu->os, Mmu->safePageLogical, &safeAddress);

    return gcvSTATUS_OK;

OnError:
    return status;
}

void
gctaMMU_DumpPagetableEntry(
    IN gcTA_MMU Mmu,
    IN gctUINT32 Address
    )
{
    gctUINT32 entry;
    gctUINT32 mtlb = _MtlbOffset(Address);
    gctUINT32_PTR mtlbLogical = Mmu->mtlbLogical;
    gctUINT32_PTR stlbLogical;
    gcsMMU_STLB_PTR stlb;
    struct _gcsMMU_STLB **stlbs = (struct _gcsMMU_STLB **)Mmu->stlbs;

    gctUINT32 stlbOffset   = (Address & gcdMMU_STLB_4K_MASK) >> gcdMMU_STLB_4K_SHIFT;
    gctUINT32 offsetInPage = Address & gcdMMU_OFFSET_4K_MASK;

    stlb = stlbs[mtlb];

    gcmkPRINT("    MTLB entry = %d\n", mtlb);

    gcmkPRINT("    STLB entry = %d\n", stlbOffset);

    gcmkPRINT("    Offset = 0x%08X (%d)\n", offsetInPage, offsetInPage);


    if (stlb == gcvNULL)
    {
        /* Dmp mtlb entry. */
        entry = mtlbLogical[mtlb];

        gcmkPRINT("   mtlb entry [%d] = %x", mtlb, entry);
    }
    else
    {
        stlbLogical = stlb->logical;

        gcmkPRINT("    stlb entry = 0x%08X", stlbLogical[stlbOffset]);
    }
}


