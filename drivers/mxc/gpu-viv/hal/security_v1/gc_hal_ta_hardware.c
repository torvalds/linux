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
#include "gc_hal_ta_hardware.h"
#include "gc_hal.h"
#include "gc_feature_database.h"


#define _GC_OBJ_ZONE     1
#define SRC_MAX          8
#define RECT_ADDR_OFFSET 3

#define INVALID_ADDRESS  ~0U

/******************************************************************************\
********************************* Support Code *********************************
\******************************************************************************/
static gceSTATUS
_IdentifyHardwareByDatabase(
    IN gcTA_HARDWARE Hardware
    )
{
    gceSTATUS status;
    gctUINT32 chipIdentity;
    gcsFEATURE_DATABASE *database;
    gctaOS os = Hardware->os;

    gcmkHEADER();

    /***************************************************************************
    ** Get chip ID and revision.
    */

    /* Read chip identity register. */
    gcmkONERROR(gctaOS_ReadRegister(os, 0x00018, &chipIdentity));

    /* Special case for older graphic cores. */
    if (((((gctUINT32) (chipIdentity)) >> (0 ? 31:24) & ((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1)))))) == (0x01 & ((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:
24) + 1))))))))
    {
        Hardware->chipModel    = gcv500;
        Hardware->chipRevision = (((((gctUINT32) (chipIdentity)) >> (0 ? 15:12)) & ((gctUINT32) ((((1 ? 15:12) - (0 ? 15:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:12) - (0 ? 15:12) + 1)))))) );
    }

    else
    {
        /* Read chip identity register. */
        gcmkONERROR(
            gctaOS_ReadRegister(os,
                                 0x00020,
                                 (gctUINT32_PTR) &Hardware->chipModel));

        if (((Hardware->chipModel & 0xFF00) == 0x0400)
          && (Hardware->chipModel != 0x0420)
          && (Hardware->chipModel != 0x0428))
        {
            Hardware->chipModel = (gceCHIPMODEL) (Hardware->chipModel & 0x0400);
        }

        /* Read CHIP_REV register. */
        gcmkONERROR(
            gctaOS_ReadRegister(os,
                                 0x00024,
                                 &Hardware->chipRevision));

        if ((Hardware->chipModel    == gcv300)
        &&  (Hardware->chipRevision == 0x2201)
        )
        {
            gctUINT32 chipDate;
            gctUINT32 chipTime;

            /* Read date and time registers. */
            gcmkONERROR(
                gctaOS_ReadRegister(os,
                                     0x00028,
                                     &chipDate));

            gcmkONERROR(
                gctaOS_ReadRegister(os,
                                     0x0002C,
                                     &chipTime));

            if ((chipDate == 0x20080814) && (chipTime == 0x12051100))
            {
                /* This IP has an ECO; put the correct revision in it. */
                Hardware->chipRevision = 0x1051;
            }
        }

        gcmkONERROR(
            gctaOS_ReadRegister(os,
                                 0x000A8,
                                 &Hardware->productID));
    }

    gcmkVERIFY_OK(gctaOS_ReadRegister(
        os,
        0x000E8,
        &Hardware->ecoID
        ));

    gcmkVERIFY_OK(gctaOS_ReadRegister(
        os,
        0x00030,
        &Hardware->customerID
        ));

    /***************************************************************************
    ** Get chip features.
    */

    database =
    Hardware->featureDatabase =
    gcQueryFeatureDB(
        Hardware->chipModel,
        Hardware->chipRevision,
        Hardware->productID,
        Hardware->ecoID,
        Hardware->customerID
        );

    if (database == gcvNULL)
    {
        gcmkPRINT("[galcore]: Feature database is not found,"
                  "chipModel=0x%0x, chipRevision=0x%x, productID=0x%x, ecoID=0x%x",
                  Hardware->chipModel,
                  Hardware->chipRevision,
                  Hardware->productID,
                  Hardware->ecoID);
        gcmkONERROR(gcvSTATUS_NOT_FOUND);
    }

    /* Success. */
    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}


gceSTATUS
gctaHARDWARE_SetMMUStates(
    IN gcTA_HARDWARE Hardware,
    IN gctPOINTER MtlbAddress,
    IN gceMMU_MODE Mode,
    IN gctPOINTER SafeAddress,
    IN gctPOINTER Logical,
    IN OUT gctUINT32 * Bytes
    )
{
    gceSTATUS status;
    gctUINT32 config, address;
    gctUINT32 extMtlb, extSafeAddrss;
    gctPHYS_ADDR_T physical;
    gctUINT32_PTR buffer;
    gctUINT32 reserveBytes = 2 * 4;
    gcsMMU_TABLE_ARRAY_ENTRY * entry;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    entry = (gcsMMU_TABLE_ARRAY_ENTRY *) Hardware->pagetableArray.logical;

    /* Convert logical address into physical address. */
    gcmkONERROR(
        gctaOS_GetPhysicalAddress(Hardware->os, MtlbAddress, &physical));

    config  = (gctUINT32)(physical & 0xFFFFFFFF);
    extMtlb = (gctUINT32)(physical >> 32);

    gcmkONERROR(
        gctaOS_GetPhysicalAddress(Hardware->os, SafeAddress, &physical));

    address = (gctUINT32)(physical & 0xFFFFFFFF);
    extSafeAddrss = (gctUINT32)(physical >> 32);

    if (address & 0x3F)
    {
        gcmkONERROR(gcvSTATUS_NOT_ALIGNED);
    }

    switch (Mode)
    {
    case gcvMMU_MODE_1K:
        if (config & 0x3FF)
        {
            gcmkONERROR(gcvSTATUS_NOT_ALIGNED);
        }

        config |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));

        break;

    case gcvMMU_MODE_4K:
        if (config & 0xFFF)
        {
            gcmkONERROR(gcvSTATUS_NOT_ALIGNED);
        }

        config |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));

        break;

    default:
        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    if (Logical != gcvNULL)
    {
        buffer = Logical;

        /* Setup page table array entry. */
        entry->low = config;
        entry->high = physical >> 32;

        /* Setup command buffer to load index 0 of page table array. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x006B) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

        *buffer++
            = (((((gctUINT32) (~0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) &((((gctUINT32) (~0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 16:16) - (0 ?
 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ?
 16:16))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ? 16:16))));
    }

    if (Bytes != gcvNULL)
    {
        *Bytes = reserveBytes;
    }

    /* Return the status. */
    gcmkFOOTER_NO();
    return status;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

gceSTATUS
gctaHARDWARE_End(
    IN gcTA_HARDWARE Hardware,
    IN gctPOINTER Logical,
    IN OUT gctUINT32 * Bytes
    )
{
    gctUINT32_PTR logical = (gctUINT32_PTR) Logical;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x *Bytes=%lu",
        Hardware, Logical, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT((Logical == gcvNULL) || (Bytes != gcvNULL));

    if (Logical != gcvNULL)
    {
        if (*Bytes < 8)
        {
            /* Command queue too small. */
            gcmkONERROR(gcvSTATUS_BUFFER_TOO_SMALL);
        }

        /* Append END. */
        logical[0] =
            ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x02 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

        /* Record the count of execution which is finised by this END. */
        logical[1] =
            0;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE, "0x%x: END", Logical);
    }

    if (Bytes != gcvNULL)
    {
        /* Return number of bytes required by the END command. */
        *Bytes = 8;
    }

    /* Success. */
    gcmkFOOTER_ARG("*Bytes=%lu", gcmOPT_VALUE(Bytes));
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}


gceSTATUS
gctaHARDWARE_Construct(
    IN gcTA TA,
    OUT gcTA_HARDWARE * Hardware
    )
{
    gceSTATUS status;
    gcTA_HARDWARE hardware;

    gctaOS os = TA->os;

    gcmkONERROR(gctaOS_Allocate(
        gcmSIZEOF(gcsTA_HARDWARE),
        (gctPOINTER *)&hardware
        ));

    gctaOS_ZeroMemory((gctUINT8_PTR)hardware, gcmSIZEOF(gcsTA_HARDWARE));

    hardware->ta = TA;
    hardware->os = os;

    hardware->pagetableArray.size = 4096;

    hardware->functionBytes = 4096;

    /*************************************/
    /********  Get chip information ******/
    /*************************************/

    _IdentifyHardwareByDatabase(hardware);

    *Hardware = hardware;

    return gcvSTATUS_OK;
OnError:
    return status;
}

gceSTATUS
gctaHARDWARE_Destroy(
    IN gcTA_HARDWARE Hardware
    )
{
    if (Hardware->pagetableArray.logical)
    {
        gctaOS_FreeSecurityMemory(
            Hardware->ta->os,
            Hardware->pagetableArray.size,
            Hardware->pagetableArray.logical,
            (gctUINT32_PTR)Hardware->pagetableArray.physical
            );
    }

    if (Hardware->functionLogical)
    {
        gctaOS_FreeSecurityMemory(
            Hardware->ta->os,
            Hardware->functionBytes,
            Hardware->functionLogical,
            (gctUINT32_PTR)Hardware->functionPhysical
            );
    }

    gctaOS_Free(Hardware);

    return gcvSTATUS_OK;
}

gceSTATUS
gctaHARDWARE_Execute(
    IN gcTA TA,
    IN gctUINT32 Address,
    IN gctUINT32 Bytes
    )
{
    gceSTATUS status;
    gctUINT32 address = Address, control;

    gcmkHEADER_ARG("Address=0x%x Bytes=%lu",
        Address, Bytes);

    /* Enable all events. */
    gcmkONERROR(
        gctaOS_WriteRegister(TA->os, 0x00014, ~0U));

    /* Write address register. */
    gcmkONERROR(
        gctaOS_WriteRegister(TA->os, 0x00654, address));

    /* Build control register. */
    control = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ?
 16:16))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ? 16:16)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) ((Bytes + 7) >> 3) & ((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

    /* Write control register. */
    gcmkONERROR(
        gctaOS_WriteRegister(TA->os, 0x003A4, control));

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
        "Started command buffer @ 0x%08x",
        address);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

gceSTATUS
gctaHARDWARE_MmuEnable(
    IN gcTA_HARDWARE Hardware
    )
{
    gctaOS_WriteRegister(
        Hardware->ta->os,
        0x0018C,
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) ((gctUINT32) (1 ) & ((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))));

    return gcvSTATUS_OK;
}

/*
* In trust zone, we prepare page table array table and configure base address of
* it to hardware.
*/
gceSTATUS
gctaHARDWARE_SetMMU(
    IN gcTA_HARDWARE Hardware,
    IN gctPOINTER Logical
    )
{
    gcsMMU_TABLE_ARRAY_ENTRY *entry;
    gcsHARDWARE_FUNCTION *function = &Hardware->functions[0];
    gctUINT32 delay = 1;
    gctUINT32 timer = 0;
    gctUINT32 idle;
    gctPHYS_ADDR_T mtlbPhysical;
    gctPHYS_ADDR_T secureSafeAddress;
    gctPHYS_ADDR_T nonSecureSafeAddress;

    gctaOS_GetPhysicalAddress(Hardware->ta->os, Logical, &mtlbPhysical);

    gctaOS_GetPhysicalAddress(Hardware->ta->os, Hardware->ta->mmu->safePageLogical, &secureSafeAddress);

    gctaOS_GetPhysicalAddress(Hardware->ta->os, Hardware->ta->mmu->nonSecureSafePageLogical, &nonSecureSafeAddress);

    /* Fill entry 0 of page table array. */
    entry = (gcsMMU_TABLE_ARRAY_ENTRY *)Hardware->pagetableArray.logical;

    entry->low  = (gctUINT32)(mtlbPhysical & 0xFFFFFFFF);

    entry->high = (gctUINT32)(mtlbPhysical >> 32)
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 8:8) - (0 ? 8:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:8) - (0 ? 8:8) + 1))))))) << (0 ?
 8:8))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 8:8) - (0 ?
 8:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:8) - (0 ? 8:8) + 1))))))) << (0 ?
 8:8)))
                ;

    /* Set page table base. */
    gctaOS_WriteRegister(
        Hardware->ta->os,
        0x0038C,
        (gctUINT32)(Hardware->pagetableArray.address & 0xFFFFFFFF)
        );

    gctaOS_WriteRegister(
        Hardware->ta->os,
        0x00390,
        (gctUINT32)((Hardware->pagetableArray.address >> 32) & 0xFFFFFFFF)
        );

    gctaOS_WriteRegister(
        Hardware->ta->os,
        0x00394,
        1
        );

    gctaOS_WriteRegister(
        Hardware->ta->os,
        0x0039C,
        (gctUINT32)(secureSafeAddress & 0xFFFFFFFF)
        );

    gctaOS_WriteRegister(
        Hardware->ta->os,
        0x00398,
        (gctUINT32)(nonSecureSafeAddress & 0xFFFFFFFF)
        );

    gctaOS_WriteRegister(
        Hardware->ta->os,
        0x003A0,
        (((((gctUINT32) (~0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) ((gctUINT32)((secureSafeAddress >> 32) & 0xFFFFFFFF)) & ((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) &((((gctUINT32) (~0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:31) - (0 ?
 31:31) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1))))))) << (0 ?
 31:31))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 31:31) - (0 ? 31:31) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1))))))) << (0 ? 31:31))))
      | (((((gctUINT32) (~0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) ((gctUINT32)((nonSecureSafeAddress >> 32) & 0xFFFFFFFF)) & ((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) &((((gctUINT32) (~0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:15) - (0 ?
 15:15) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:15) - (0 ? 15:15) + 1))))))) << (0 ?
 15:15))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 15:15) - (0 ? 15:15) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 15:15) - (0 ? 15:15) + 1))))))) << (0 ? 15:15))))
        );

    /* Execute prepared command sequence. */
    gctaHARDWARE_Execute(
        Hardware->ta,
        function->address,
        function->bytes
        );

    /* Wait until MMU configure finishes. */
    do
    {
        gctaOS_Delay(Hardware->os, delay);

        gctaOS_ReadRegister(
            Hardware->ta->os,
            0x00004,
            &idle);

        timer += delay;
        delay *= 2;
    }
    while (!(((((gctUINT32) (idle)) >> (0 ? 0:0)) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1)))))) ));

    /* Enable MMU. */
    gctaOS_WriteRegister(
        Hardware->os,
        0x00388,
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))
        );

    return gcvSTATUS_OK;
}

gceSTATUS
gctaHARDWARE_PrepareFunctions(
    IN gcTA_HARDWARE Hardware
    )
{
    gceSTATUS status;
    gcsHARDWARE_FUNCTION * function;
    gctUINT32 mmuBytes;
    gctUINT32 endBytes = 8;
    gctUINT8_PTR logical;

    gcmkHEADER();

    /* Allocate page table array. */
    gcmkONERROR(gctaOS_AllocateSecurityMemory(
        Hardware->ta->os,
        &Hardware->pagetableArray.size,
        &Hardware->pagetableArray.logical,
        &Hardware->pagetableArray.physical
        ));

    gcmkONERROR(gctaOS_GetPhysicalAddress(
        Hardware->ta->os,
        Hardware->pagetableArray.logical,
        &Hardware->pagetableArray.address
        ));

    /* Allocate GPU functions. */
    gcmkONERROR(gctaOS_AllocateSecurityMemory(
        Hardware->ta->os,
        &Hardware->functionBytes,
        &Hardware->functionLogical,
        &Hardware->functionPhysical
        ));

    gcmkONERROR(gctaOS_GetPhysicalAddress(
        Hardware->ta->os,
        Hardware->functionLogical,
        (gctPHYS_ADDR_T *)&Hardware->functionAddress
        ));

    function = &Hardware->functions[0];

    function->logical = Hardware->functionLogical;

    function->address = Hardware->functionAddress;

    logical = function->logical;

    gcmkONERROR(gctaHARDWARE_SetMMUStates(
        Hardware,
        Hardware->ta->mmu->mtlbLogical,
        gcvMMU_MODE_4K,
        Hardware->ta->mmu->safePageLogical,
        logical,
        &mmuBytes
        ));

    logical += 8;

    gcmkONERROR(gctaHARDWARE_End(
        Hardware,
        logical,
        &endBytes
        ));

    function->bytes = mmuBytes + endBytes;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

gceSTATUS
gctaHARDWARE_IsFeatureAvailable(
    IN gcTA_HARDWARE Hardware,
    IN gceFEATURE Feature
    )
{
    gctBOOL available;
    gcsFEATURE_DATABASE *database = Hardware->featureDatabase;

    switch (Feature)
    {
    case gcvFEATURE_SECURITY:
        available = database->SECURITY;
        break;
    default:
        gcmkFATAL("Invalid feature has been requested.");
        available = gcvFALSE;
    }

    return available;
}

gceSTATUS
gctaHARDWARE_DumpMMUException(
    IN gcTA_HARDWARE Hardware
    )
{
    gctUINT32 mmu       = 0;
    gctUINT32 mmuStatus = 0;
    gctUINT32 address   = 0;
    gctUINT32 i         = 0;

    gctUINT32 mmuStatusRegAddress;
    gctUINT32 mmuExceptionAddress;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    mmuStatusRegAddress = 0x00384;
    mmuExceptionAddress = 0x00380;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    gcmkPRINT("ChipModel=0x%x ChipRevision=0x%x:\n",
              Hardware->chipModel,
              Hardware->chipRevision);

    gcmkPRINT("**************************\n");
    gcmkPRINT("***   MMU ERROR DUMP   ***\n");
    gcmkPRINT("**************************\n");

    gcmkVERIFY_OK(gctaOS_ReadRegister(
        Hardware->os,
        mmuStatusRegAddress,
        &mmuStatus
        ));

    gcmkPRINT("  MMU status = 0x%08X\n", mmuStatus);

    for (i = 0; i < 4; i += 1)
    {
        mmu = mmuStatus & 0xF;
        mmuStatus >>= 4;

        if (mmu == 0)
        {
            continue;
        }

        switch (mmu)
        {
        case 1:
              gcmkPRINT("  MMU%d: slave not present\n", i);
              break;

        case 2:
              gcmkPRINT("  MMU%d: page not present\n", i);
              break;

        case 3:
              gcmkPRINT("  MMU%d: write violation\n", i);
              break;

        case 4:
              gcmkPRINT("  MMU%d: out of bound", i);
              break;

        case 5:
              gcmkPRINT("  MMU%d: read security violation", i);
              break;

        case 6:
              gcmkPRINT("  MMU%d: write security violation", i);
              break;

        default:
              gcmkPRINT("  MMU%d: unknown state\n", i);
        }

        gcmkVERIFY_OK(gctaOS_ReadRegister(
            Hardware->os,
            mmuExceptionAddress + i * 4,
            &address
            ));

        gcmkPRINT("  MMU%d: exception address = 0x%08X\n", i, address);

        gctaMMU_DumpPagetableEntry(Hardware->ta->mmu, address);
    }

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

