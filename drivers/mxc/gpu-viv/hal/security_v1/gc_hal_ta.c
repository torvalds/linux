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

/*
* Responsibility of TA (trust application).
* 1) Start FE.
*   When non secure driver asks for start FE. TA enable MMU and start FE.
*   TA always execute MMU enable processes because it has no idea whether
*   GPU has been power off.
*
* 2) Setup page table
*   When non secure driver asks for set up GPU address to physical address
*   mapping, TA check the attribute of physical address and attribute of
*   GPU address to make sure they are match. Then it change page table.
*
*/

/*******************************************************************************
**
**  gcTA_Construct
**
**  Construct a new gcTA object.
*/
int
gcTA_Construct(
    IN gctaOS Os,
    OUT gcTA *TA
    )
{
    gceSTATUS status;
    gctPOINTER pointer;
    gcTA ta;

    /* Construct a gcTA object. */
    gcmkONERROR(gctaOS_Allocate(sizeof(struct _gcTA), &pointer));

    gctaOS_ZeroMemory(pointer, sizeof(struct _gcTA));

    ta = (gcTA)pointer;

    ta->os = Os;

    gcmkONERROR(gctaHARDWARE_Construct(ta, &ta->hardware));

    if (gctaHARDWARE_IsFeatureAvailable(ta->hardware, gcvFEATURE_SECURITY))
    {
        gcmkONERROR(gctaMMU_Construct(ta, &ta->mmu));

        gcmkONERROR(gctaHARDWARE_PrepareFunctions(ta->hardware));
    }

    *TA = ta;

    return 0;

OnError:
    return status;
}

/*******************************************************************************
**
**  gcTA_Construct
**
**  Destroy a gcTA object.
*/
int
gcTA_Destroy(
    IN gcTA TA
    )
{
    if (TA->mmu)
    {
        gcmkVERIFY_OK(gctaMMU_Destory(TA->mmu));
    }

    if (TA->hardware)
    {
        gcmkVERIFY_OK(gctaHARDWARE_Destroy(TA->hardware));
    }

    gcmkVERIFY_OK(gctaOS_Free(TA));

    /* Destroy. */
    return 0;
}


/*
*   Map a scatter gather list into gpu address space.
*
*/
gceSTATUS
gcTA_MapMemory(
    IN gcTA TA,
    IN gctUINT32 *PhysicalArray,
    IN gctPHYS_ADDR_T Physical,
    IN gctUINT32 PageCount,
    OUT gctUINT32 *GPUAddress
    )
{
    gceSTATUS status;
    gcTA_MMU mmu;
    gctUINT32 pageCount = PageCount;
    gctUINT32 i;
    gctUINT32 gpuAddress = *GPUAddress;
    gctBOOL mtlbSecure = gcvFALSE;
    gctBOOL physicalSecure = gcvFALSE;

    mmu = TA->mmu;

    /* Fill in page table. */
    for (i = 0; i < pageCount; i++)
    {
        gctUINT32 physical;
        gctUINT32_PTR entry;

        if (PhysicalArray)
        {
            physical = PhysicalArray[i];
        }
        else
        {
            physical = (gctUINT32)Physical + 4096 * i;
        }

        gcmkONERROR(gctaMMU_GetPageEntry(mmu, gpuAddress, &entry, &mtlbSecure));

        status = gctaOS_IsPhysicalSecure(TA->os, physical, &physicalSecure);

        if (gcmIS_SUCCESS(status) && physicalSecure != mtlbSecure)
        {
            gcmkONERROR(gcvSTATUS_NOT_SUPPORTED);
        }

        gctaMMU_SetPage(mmu, physical, entry);

        gpuAddress += 4096;
    }

    return gcvSTATUS_OK;

OnError:
    return status;
}

gceSTATUS
gcTA_UnmapMemory(
    IN gcTA TA,
    IN gctUINT32 GPUAddress,
    IN gctUINT32 PageCount
    )
{
    gceSTATUS status;

    gcmkONERROR(gctaMMU_FreePages(TA->mmu, GPUAddress, PageCount));

    return gcvSTATUS_OK;

OnError:
    return status;
}

gceSTATUS
gcTA_StartCommand(
    IN gcTA TA,
    IN gctUINT32 Address,
    IN gctUINT32 Bytes
    )
{
    gctaHARDWARE_Execute(TA, Address, Bytes);
    return gcvSTATUS_OK;
}

int
gcTA_Dispatch(
    IN gcTA TA,
    IN gcsTA_INTERFACE * Interface
    )
{
    int command = Interface->command;

    gceSTATUS status = gcvSTATUS_OK;

    switch (command)
    {
    case KERNEL_START_COMMAND:
        /* Enable MMU every time FE starts.
        ** Because if normal world stop GPU and power off GPU, MMU states is reset.
        */
        gcmkONERROR(gctaHARDWARE_SetMMU(TA->hardware, TA->mmu->mtlbLogical));

        gcmkONERROR(gcTA_StartCommand(
            TA,
            Interface->u.StartCommand.address,
            Interface->u.StartCommand.bytes
            ));
        break;

    case KERNEL_MAP_MEMORY:
        gcmkONERROR(gcTA_MapMemory(
            TA,
            Interface->u.MapMemory.physicals,
            Interface->u.MapMemory.physical,
            Interface->u.MapMemory.pageCount,
            &Interface->u.MapMemory.gpuAddress
            ));

        break;

    case KERNEL_UNMAP_MEMORY:
        status = gcTA_UnmapMemory(
            TA,
            Interface->u.UnmapMemory.gpuAddress,
            Interface->u.UnmapMemory.pageCount
            );
        break;

    case KERNEL_DUMP_MMU_EXCEPTION:
        status = gctaHARDWARE_DumpMMUException(TA->hardware);
        break;

    default:
        gcmkASSERT(0);

        status = gcvSTATUS_INVALID_ARGUMENT;
        break;
    }

OnError:
    Interface->result = status;

    return 0;
}



