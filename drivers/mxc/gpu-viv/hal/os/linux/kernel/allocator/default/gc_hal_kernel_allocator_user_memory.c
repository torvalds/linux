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


#include "gc_hal_kernel_linux.h"
#include "gc_hal_kernel_allocator.h"

#include <linux/slab.h>
#include <linux/pagemap.h>

#define _GC_OBJ_ZONE gcvZONE_ALLOCATOR

/* Descriptor of a user memory imported. */
typedef struct _gcsUserMemory
{
    gctPHYS_ADDR_T           physical;
    struct page **           pages;
    gctUINT32                extraPage;
    gctPOINTER               logical;
    gctSIZE_T                pageCount;
    gctUINT32                offset;
    gctBOOL                  contiguous;
    gctBOOL                  userPages;
    gctBOOL                  *ref;
}
gcsUserMemory;

static gceSTATUS
_Import(
    IN gckOS Os,
    IN gctPOINTER Memory,
    IN gctUINT32 Physical,
    IN gctSIZE_T Size,
    IN gcsUserMemory * UserMemory
    )
{
    gceSTATUS status = gcvSTATUS_OK;

    gctUINTPTR_T start, end, memory;
    gctINT result = 0;

    struct page **pages = gcvNULL;
    gctSIZE_T extraPage;
    gctSIZE_T pageCount, i;
    gctBOOL *ref = gcvNULL;

    gcmkHEADER_ARG("Os=0x%x Memory=0x%x Physical=0x%x Size=%lu", Os, Memory, Physical, Size);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Memory != gcvNULL || Physical != ~0U);
    gcmkVERIFY_ARGUMENT(Size > 0);

    memory = (gctUINTPTR_T) Memory;

    /* Get the number of required pages. */
    end = (memory + Size + PAGE_SIZE - 1) >> PAGE_SHIFT;
    start = memory >> PAGE_SHIFT;
    pageCount = end - start;

    /* Allocate extra page to avoid cache overflow */
#if gcdENABLE_2D
    extraPage = 2;
#else
    extraPage = (((memory + gcmALIGN(Size + 64, 64) + PAGE_SIZE - 1) >> PAGE_SHIFT) > end) ? 1 : 0;
#endif

    gcmkTRACE_ZONE(
        gcvLEVEL_INFO, _GC_OBJ_ZONE,
        "%s(%d): pageCount: %d. extraPage: %d",
        __FUNCTION__, __LINE__,
        pageCount, extraPage
        );

    /* Overflow. */
    if ((memory + Size) < memory)
    {
        gcmkFOOTER_ARG("status=%d", gcvSTATUS_INVALID_ARGUMENT);
        return gcvSTATUS_INVALID_ARGUMENT;
    }

    /* Allocate the array of page addresses. */
    pages = (struct page **)kmalloc((pageCount + extraPage) * sizeof(struct page *), GFP_KERNEL | gcdNOWARN);

    if (pages == gcvNULL)
    {
        status = gcvSTATUS_OUT_OF_MEMORY;
        goto OnError;
    }

    ref = (gctBOOL *)kzalloc((pageCount + extraPage) * sizeof(gctBOOL), GFP_KERNEL | gcdNOWARN);

    if (ref == gcvNULL)
    {
        status = gcvSTATUS_OUT_OF_MEMORY;
        goto OnError;
    }

    if (Physical != ~0U)
    {
        unsigned long pfn = Physical >> PAGE_SHIFT;

        UserMemory->contiguous = gcvTRUE;

        if (pfn_valid(pfn))
        {
            for (i = 0; i < pageCount; i++)
            {
                pages[i] = pfn_to_page((Physical >> PAGE_SHIFT) + i);
            }
        }
        else
        {
            /* Free pages array since there is no struct page for this memory. */
            kfree(pages);
            pages = gcvNULL;
        }
    }
    else
    {
        UserMemory->contiguous = gcvFALSE;
        UserMemory->userPages = gcvTRUE;

        /* Get the user pages. */
        down_read(&current->mm->mmap_sem);

        result = get_user_pages(current,
                current->mm,
                memory & PAGE_MASK,
                pageCount,
                1,
                0,
                pages,
                gcvNULL
                );

        up_read(&current->mm->mmap_sem);

        if (result <=0 || result < pageCount)
        {
            struct vm_area_struct *vma;

            /* Release the pages if any. */
            if (result > 0)
            {
                for (i = 0; i < result; i++)
                {
                    if (pages[i] == gcvNULL)
                    {
                        break;
                    }

                    page_cache_release(pages[i]);
                    pages[i] = gcvNULL;
                }

                result = 0;
            }

            vma = find_vma(current->mm, memory);

            if (vma && (vma->vm_flags & VM_PFNMAP))
            {
                pte_t       * pte;
                spinlock_t  * ptl;
                gctUINTPTR_T logical = memory;

                for (i = 0; i < pageCount; i++)
                {
                    pgd_t * pgd = pgd_offset(current->mm, logical);
                    pud_t * pud = pud_offset(pgd, logical);

                    if (pud)
                    {
                        pmd_t * pmd = pmd_offset(pud, logical);
                        pte = pte_offset_map_lock(current->mm, pmd, logical, &ptl);
                        if (!pte)
                        {
                            gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
                        }
                    }
                    else
                    {
                        gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
                    }

                    pages[i] = pte_page(*pte);
                    pte_unmap_unlock(pte, ptl);

                    /* Advance to next. */
                    logical += PAGE_SIZE;
                }
            }
            else
            {
                gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
            }

            /* Check whether pages are contigous. */
            for (i = 1; i < pageCount; i++)
            {
                if (pages[i] != nth_page(pages[0], i))
                {
                    /* Non-contiguous. */
                    break;
                }
            }

            if (i == pageCount)
            {
                UserMemory->contiguous = gcvTRUE;
            }

            /* Reference pages. */
            for (i = 0; i < pageCount; i++)
            {
                if (pfn_valid(page_to_pfn(pages[i])))
                {
                    ref[i] = get_page_unless_zero(pages[i]);
                }
            }
        }
        else
        {
            /* Mark feference when pages from get_user_pages. */
            for (i = 0; i < pageCount; i++)
            {
                ref[i] = gcvTRUE;
            }
        }
    }

    if (UserMemory->userPages)
    {
        for (i = 0; i < pageCount; i++)
        {
            gctUINT32 phys;

    #ifdef CONFIG_ARM
            gctUINT32 data;

            if (memory)
            {
                get_user(data, (gctUINT32*)((memory & PAGE_MASK) + i * PAGE_SIZE));
            }
    #endif

            if (pages)
            {
                phys = page_to_phys(pages[i]);
            }
            else
            {
                phys = Physical + i * PAGE_SIZE;
            }

            if (memory)
            {
                /* Flush(clean) the data cache. */
                gcmkONERROR(gckOS_CacheFlush(Os, _GetProcessID(), gcvNULL,
                            phys,
                            (gctPOINTER)(memory & PAGE_MASK) + i*PAGE_SIZE,
                            PAGE_SIZE));
            }
        }
    }

    if (extraPage)
    {
        if (pages)
        {
            for (i = 0; i < extraPage; i++)
            {
                pages[pageCount++] = Os->paddingPage;
            }
        }

        /* Adjust pageCount to include padding page. */
        UserMemory->extraPage = extraPage;
    }

    UserMemory->physical = Physical;
    UserMemory->pages = pages;
    UserMemory->pageCount = pageCount;
    UserMemory->logical = Memory;
    UserMemory->ref = ref;

    UserMemory->offset = (Physical != ~0U)
                       ? (Physical & ~PAGE_MASK)
                       : (memory & ~PAGE_MASK);

    /* Success. */
    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:

    /* Release page array. */
    if (result > 0 && pages != gcvNULL)
    {
        gcmkTRACE(
            gcvLEVEL_ERROR,
            "%s(%d): error: page table is freed.",
            __FUNCTION__, __LINE__
            );

        for (i = 0; i < result; i++)
        {
            if (pages[i] == gcvNULL)
            {
                break;
            }

            page_cache_release(pages[i]);
        }
    }

    if (pages != gcvNULL)
    {
        gcmkTRACE(
            gcvLEVEL_ERROR,
            "%s(%d): error: pages is freed.",
            __FUNCTION__, __LINE__
            );

        /* Free the page table. */
        kfree(pages);
    }

    if (ref != gcvNULL)
    {
        /* Free the ref table. */
        kfree(ref);
    }

    gcmkFOOTER();
    return status;
}

static gceSTATUS
_Free(
    IN gckOS Os,
    IN gcsUserMemory * UserMemory
    )
{
    gctSIZE_T pageCount, i;
    struct page **pages;
    gctBOOL *ref;

    gcmkHEADER_ARG("Os=0x%X", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    pages = UserMemory->pages;

    /* Invalid page array. */
    if (pages == gcvNULL)
    {
        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

    pageCount = UserMemory->pageCount;

    if (UserMemory->extraPage)
    {
        pageCount -= UserMemory->extraPage;
    }

    ref = UserMemory->ref;

    if (UserMemory->userPages)
    {
        /* Release the page cache. */
        if (pages)
        {
            for (i = 0; i < pageCount; i++)
            {
                if (!PageReserved(pages[i]))
                {
                     SetPageDirty(pages[i]);
                }

                if (pfn_valid(page_to_pfn(pages[i])) && ref[i])
                {
                    page_cache_release(pages[i]);
                }
            }
        }
    }

    kfree(ref);
    kfree(pages);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

static gceSTATUS
_UserMemoryAttach(
    IN gckALLOCATOR Allocator,
    IN gcsATTACH_DESC_PTR Desc,
    IN PLINUX_MDL Mdl
    )
{
    gceSTATUS status;
    gcsUserMemory * userMemory;

    gckOS os = Allocator->os;

    gcmkHEADER();

    /* Handle is meangless for this importer. */
    gcmkVERIFY_ARGUMENT(Desc != gcvNULL);

    gcmkONERROR(gckOS_Allocate(os, gcmSIZEOF(gcsUserMemory), (gctPOINTER *)&userMemory));

    gckOS_ZeroMemory(userMemory, gcmSIZEOF(gcsUserMemory));

    gcmkONERROR(_Import(os, Desc->memory, Desc->physical, Desc->size, userMemory));

    Mdl->priv = userMemory;
    Mdl->numPages = userMemory->pageCount;
    Mdl->contiguous = userMemory->contiguous;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:

    gcmkFOOTER();
    return status;
}

static void
_UserMemoryFree(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl
    )
{
    gckOS os = Allocator->os;
    gcsUserMemory *userMemory = Mdl->priv;

    gcmkHEADER();

    if (userMemory)
    {
        gcmkVERIFY_OK(_Free(os, userMemory));

        gcmkOS_SAFE_FREE(os, userMemory);
    }

    gcmkFOOTER_NO();
}

static gctINT
_UserMemoryMapUser(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl,
    IN gctBOOL Cacheable,
    OUT gctPOINTER * UserLogical
    )
{
    gcsUserMemory *userMemory = Mdl->priv;

    *UserLogical = userMemory->logical;

    return 0;
}

static void
_UserMemoryUnmapUser(
    IN gckALLOCATOR Allocator,
    IN gctPOINTER Logical,
    IN gctUINT32 Size
    )
{
    return;
}

static gceSTATUS
_UserMemoryMapKernel(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl,
    OUT gctPOINTER *Logical
    )
{
    /* Kernel doesn't acess video memory. */
    return gcvSTATUS_NOT_SUPPORTED;

}

static gceSTATUS
_UserMemoryUnmapKernel(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl,
    IN gctPOINTER Logical
    )
{
    /* Kernel doesn't acess video memory. */
    return gcvSTATUS_NOT_SUPPORTED;
}

static gceSTATUS
_UserMemoryCache(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl,
    IN gctPOINTER Logical,
    IN gctUINT32 Physical,
    IN gctUINT32 Bytes,
    IN gceCACHEOPERATION Operation
    )
{
    return gcvSTATUS_OK;
}

static gceSTATUS
_UserMemoryPhysical(
    IN gckALLOCATOR Allocator,
    IN PLINUX_MDL Mdl,
    IN gctUINT32 Offset,
    OUT gctPHYS_ADDR_T * Physical
    )
{
    gckOS os = Allocator->os;
    gcsUserMemory *userMemory = Mdl->priv;
    gctUINT32 offsetInPage = Offset & ~PAGE_MASK;
    gctUINT32 index = Offset / PAGE_SIZE;

    if (userMemory->pages)
    {
        if (index == userMemory->pageCount - 1 && userMemory->extraPage)
        {
            *Physical = page_to_phys(os->paddingPage);
        }
        else
        {
            *Physical = page_to_phys(userMemory->pages[index]);
        }
    }
    else
    {
        *Physical = userMemory->physical + index * PAGE_SIZE;
    }

    *Physical += offsetInPage;

    return gcvSTATUS_OK;
}

/* User memory allocator (importer) operations. */
static gcsALLOCATOR_OPERATIONS UserMemoryAllocatorOperations =
{
    .Attach             = _UserMemoryAttach,
    .Free               = _UserMemoryFree,
    .MapUser            = _UserMemoryMapUser,
    .UnmapUser          = _UserMemoryUnmapUser,
    .MapKernel          = _UserMemoryMapKernel,
    .UnmapKernel        = _UserMemoryUnmapKernel,
    .Cache              = _UserMemoryCache,
    .Physical           = _UserMemoryPhysical,
};

/* Default allocator entry. */
gceSTATUS
_UserMemoryAlloctorInit(
    IN gckOS Os,
    OUT gckALLOCATOR * Allocator
    )
{
    gceSTATUS status;
    gckALLOCATOR allocator;

    gcmkONERROR(
        gckALLOCATOR_Construct(Os, &UserMemoryAllocatorOperations, &allocator));

    allocator->capability = gcvALLOC_FLAG_USERMEMORY;

    *Allocator = allocator;

    return gcvSTATUS_OK;

OnError:
    return status;
}

