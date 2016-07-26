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


#include "gc_hal.h"
#include "gc_hal_kernel.h"
#if VIVANTE_PROFILER_CONTEXT
#include "gc_hal_kernel_context.h"
#endif

#include "gc_feature_database.h"

#define _GC_OBJ_ZONE    gcvZONE_HARDWARE

#define gcmSEMAPHORESTALL(buffer) \
        do \
        { \
            /* Arm the PE-FE Semaphore. */ \
            *buffer++ \
                = gcmSETFIELDVALUE(0, AQ_COMMAND_LOAD_STATE_COMMAND, OPCODE, LOAD_STATE) \
                | gcmSETFIELD     (0, AQ_COMMAND_LOAD_STATE_COMMAND, COUNT, 1) \
                | gcmSETFIELD     (0, AQ_COMMAND_LOAD_STATE_COMMAND, ADDRESS, 0x0E02); \
            \
            *buffer++ \
                = gcmSETFIELDVALUE(0, AQ_SEMAPHORE, SOURCE, FRONT_END) \
                | gcmSETFIELDVALUE(0, AQ_SEMAPHORE, DESTINATION, PIXEL_ENGINE);\
            \
            /* STALL FE until PE is done flushing. */ \
            *buffer++ \
                = gcmSETFIELDVALUE(0, STALL_COMMAND, OPCODE, STALL); \
            \
            *buffer++ \
                = gcmSETFIELDVALUE(0, STALL_STALL, SOURCE, FRONT_END) \
                | gcmSETFIELDVALUE(0, STALL_STALL, DESTINATION, PIXEL_ENGINE); \
        } while(0)

typedef struct _gcsiDEBUG_REGISTERS * gcsiDEBUG_REGISTERS_PTR;
typedef struct _gcsiDEBUG_REGISTERS
{
    gctSTRING       module;
    gctUINT         index;
    gctUINT         shift;
    gctUINT         data;
    gctUINT         count;
    gctUINT32       pipeMask;
    gctUINT32       selectStart;
}
gcsiDEBUG_REGISTERS;

typedef struct _gcsFE_STACK
{
    gctSTRING       name;
    gctINT          count;
    gctUINT32       highSelect;
    gctUINT32       lowSelect;
    gctUINT32       linkSelect;
    gctUINT32       clear;
    gctUINT32       next;
}
gcsFE_STACK;

/******************************************************************************\
********************************* Support Code *********************************
\******************************************************************************/
static gctBOOL
_IsHardwareMatch(
    IN gckHARDWARE Hardware,
    IN gctINT32 ChipModel,
    IN gctUINT32 ChipRevision
    )
{
    return ((Hardware->identity.chipModel == ChipModel) &&
            (Hardware->identity.chipRevision == ChipRevision));
}

static gceSTATUS
_ResetGPU(
    IN gckHARDWARE Hardware,
    IN gckOS Os,
    IN gceCORE Core
    );

static void
_GetEcoID(
    IN gckHARDWARE Hardware,
    IN OUT gcsHAL_QUERY_CHIP_IDENTITY_PTR Identity
    )
{
    gcmkVERIFY_OK(gckOS_ReadRegisterEx(
        Hardware->os,
        Hardware->core,
        0x000E8,
        &Identity->ecoID
        ));

    /* VIV: PXA1088. */
    if (_IsHardwareMatch(Hardware, 0x1000, 0x5037) && (Identity->chipDate == 0x20120617))
    {
        Identity->ecoID = 1;
    }

    /* VIV: BG2CT*/
    if (_IsHardwareMatch(Hardware, 0x320, 0x5303) && (Identity->chipDate == 0x20140511))
    {
        Identity->ecoID = 1;
    }

    /* VIV: Hope this is the end of list. */
}

static gceSTATUS
_IdentifyHardwareByDatabase(
    IN gckHARDWARE Hardware,
    IN gckOS Os,
    IN gceCORE Core,
    OUT gcsHAL_QUERY_CHIP_IDENTITY_PTR Identity
    )
{
    gceSTATUS status;
    gctUINT32 chipIdentity;
    gctUINT32 debugControl0;
    gcsFEATURE_DATABASE *database;

    gcmkHEADER_ARG("Os=0x%x", Os);

    /* Get chip date. */
    gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x00028, &Identity->chipDate));

    /***************************************************************************
    ** Get chip ID and revision.
    */

    /* Read chip identity register. */
    gcmkONERROR(
        gckOS_ReadRegisterEx(Os, Core,
                             0x00018,
                             &chipIdentity));

    /* Special case for older graphic cores. */
    if (((((gctUINT32) (chipIdentity)) >> (0 ? 31:24) & ((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1)))))) == (0x01 & ((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:
24) + 1))))))))
    {
        Identity->chipModel    = gcv500;
        Identity->chipRevision = (((((gctUINT32) (chipIdentity)) >> (0 ? 15:12)) & ((gctUINT32) ((((1 ? 15:12) - (0 ? 15:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:12) - (0 ? 15:12) + 1)))))) );
    }

    else
    {
        /* Read chip identity register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Os, Core,
                                 0x00020,
                                 (gctUINT32_PTR) &Identity->chipModel));

        if (((Identity->chipModel & 0xFF00) == 0x0400)
          && (Identity->chipModel != 0x0420)
          && (Identity->chipModel != 0x0428))
        {
            Identity->chipModel = (gceCHIPMODEL) (Identity->chipModel & 0x0400);
        }

        /* Read CHIP_REV register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Os, Core,
                                 0x00024,
                                 &Identity->chipRevision));

        if ((Identity->chipModel    == gcv300)
        &&  (Identity->chipRevision == 0x2201)
        )
        {
            gctUINT32 chipDate;
            gctUINT32 chipTime;

            /* Read date and time registers. */
            gcmkONERROR(
                gckOS_ReadRegisterEx(Os, Core,
                                     0x00028,
                                     &chipDate));

            gcmkONERROR(
                gckOS_ReadRegisterEx(Os, Core,
                                     0x0002C,
                                     &chipTime));

            if ((chipDate == 0x20080814) && (chipTime == 0x12051100))
            {
                /* This IP has an ECO; put the correct revision in it. */
                Identity->chipRevision = 0x1051;
            }
        }

        gcmkONERROR(
            gckOS_ReadRegisterEx(Os, Core,
                                 0x000A8,
                                 &Identity->productID));
    }

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Identity: chipModel=%X",
                   Identity->chipModel);

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Identity: chipRevision=%X",
                   Identity->chipRevision);

    _GetEcoID(Hardware, Identity);

    gcmkONERROR(
        gckOS_ReadRegisterEx(Os, Core,
                                0x00030,
                                &Identity->customerID));

    /***************************************************************************
    ** Get chip features.
    */

    database =
    Hardware->featureDatabase =
    gcQueryFeatureDB(
        Hardware->identity.chipModel,
        Hardware->identity.chipRevision,
        Hardware->identity.productID,
        Hardware->identity.ecoID,
        Hardware->identity.customerID);

    if (database == gcvNULL)
    {
        gcmkPRINT("[galcore]: Feature database is not found,"
                  "chipModel=0x%0x, chipRevision=0x%x, productID=0x%x, ecoID=0x%x",
                  Hardware->identity.chipModel,
                  Hardware->identity.chipRevision,
                  Hardware->identity.productID,
                  Hardware->identity.ecoID);
        gcmkONERROR(gcvSTATUS_NOT_FOUND);
    }

    Identity->pixelPipes             = database->NumPixelPipes;
    Identity->resolvePipes           = database->NumResolvePipes;
    Identity->instructionCount       = database->InstructionCount;
    Identity->numConstants           = database->NumberOfConstants;
    Identity->varyingsCount          = database->VaryingCount;
    Identity->gpuCoreCount           = database->CoreCount;
    Identity->streamCount            = database->Streams;

    if (Identity->chipModel == gcv320)
    {
        gctUINT32 data;

        gcmkONERROR(
            gckOS_ReadRegisterEx(Os,
                                 Core,
                                 0x0002C,
                                 &data));

        if ((data != 33956864) &&
            ((Identity->chipRevision == 0x5007) ||
            (Identity->chipRevision == 0x5220)))
        {
            Hardware->maxOutstandingReads = 0xFF &
                (Identity->chipRevision == 0x5220 ? 8 :
                (Identity->chipRevision == 0x5007 ? 12 : 0));
        }
    }

    if (_IsHardwareMatch(Hardware, gcv880, 0x5107))
    {
        Hardware->maxOutstandingReads = 0x00010;
    }

    gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x00470, &debugControl0));

    if (debugControl0 & (1 << 16))
    {
        Identity->chipFlags |= gcvCHIP_FLAG_MSAA_COHERENCEY_ECO_FIX;
    }

    /* Success. */
    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

static gceSTATUS
_GetHardwareSignature(
    IN gckHARDWARE Hardware,
    IN gckOS Os,
    IN gceCORE Core,
    OUT gcsHARDWARE_SIGNATURE * Signature
    )
{
    gceSTATUS status;

    gctUINT32 chipIdentity;

    gcmkHEADER_ARG("Os=0x%x", Os);

    /***************************************************************************
    ** Get chip ID and revision.
    */

    /* Read chip identity register. */
    gcmkONERROR(
        gckOS_ReadRegisterEx(Os, Core,
                             0x00018,
                             &chipIdentity));

    /* Special case for older graphic cores. */
    if (((((gctUINT32) (chipIdentity)) >> (0 ? 31:24) & ((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1)))))) == (0x01 & ((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:
24) + 1))))))))
    {
        Signature->chipModel    = gcv500;
        Signature->chipRevision = (((((gctUINT32) (chipIdentity)) >> (0 ? 15:12)) & ((gctUINT32) ((((1 ? 15:12) - (0 ? 15:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:12) - (0 ? 15:12) + 1)))))) );
    }

    else
    {
        /* Read chip identity register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Os, Core,
                                 0x00020,
                                 (gctUINT32_PTR) &Signature->chipModel));

        /* Read CHIP_REV register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Os, Core,
                                 0x00024,
                                 &Signature->chipRevision));
    }

    /***************************************************************************
    ** Get chip features.
    */

    /* Read chip feature register. */
    gcmkONERROR(
        gckOS_ReadRegisterEx(Os, Core,
                             0x0001C,
                             &Signature->chipFeatures));

    if (((Signature->chipModel == gcv500) && (Signature->chipRevision < 2))
    ||  ((Signature->chipModel == gcv300) && (Signature->chipRevision < 0x2000))
    )
    {
        /* GC500 rev 1.x and GC300 rev < 2.0 doesn't have these registers. */
        Signature->chipMinorFeatures  = 0;
        Signature->chipMinorFeatures1 = 0;
        Signature->chipMinorFeatures2 = 0;
    }
    else
    {
        /* Read chip minor feature register #0. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Os, Core,
                                 0x00034,
                                 &Signature->chipMinorFeatures));

        if (((((gctUINT32) (Signature->chipMinorFeatures)) >> (0 ? 21:21) & ((gctUINT32) ((((1 ?
 21:21) - (0 ? 21:21) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:21) - (0 ? 21:21) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ?
 21:21) - (0 ? 21:21) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:21) - (0 ? 21:
21) + 1)))))))
        )
        {
            /* Read chip minor features register #1. */
            gcmkONERROR(
                gckOS_ReadRegisterEx(Os, Core,
                                     0x00074,
                                     &Signature->chipMinorFeatures1));

            /* Read chip minor features register #2. */
            gcmkONERROR(
                gckOS_ReadRegisterEx(Os, Core,
                                     0x00084,
                                     &Signature->chipMinorFeatures2));
        }
        else
        {
            /* Chip doesn't has minor features register #1 or 2 or 3 or 4 or 5. */
            Signature->chipMinorFeatures1 = 0;
            Signature->chipMinorFeatures2 = 0;
        }
    }

    /* Success. */
    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/* Set to 1 to enable module clock gating debug function.
*  Following options take effect when it is set to 1.
*/
#define gcdDEBUG_MODULE_CLOCK_GATING          0
/* Set to 1 to disable module clock gating of all modules. */
#define gcdDISABLE_MODULE_CLOCK_GATING        0
/* Set to 1 to disable module clock gating of each module. */
#define gcdDISABLE_STARVE_MODULE_CLOCK_GATING 0
#define gcdDISABLE_FE_CLOCK_GATING            0
#define gcdDISABLE_PE_CLOCK_GATING            0
#define gcdDISABLE_SH_CLOCK_GATING            0
#define gcdDISABLE_PA_CLOCK_GATING            0
#define gcdDISABLE_SE_CLOCK_GATING            0
#define gcdDISABLE_RA_CLOCK_GATING            0
#define gcdDISABLE_RA_EZ_CLOCK_GATING         0
#define gcdDISABLE_RA_HZ_CLOCK_GATING         0
#define gcdDISABLE_TX_CLOCK_GATING            0

#if gcdDEBUG_MODULE_CLOCK_GATING
gceSTATUS
_ConfigureModuleLevelClockGating(
    gckHARDWARE Hardware
    )
{
    gctUINT32 data;

    gcmkVERIFY_OK(
        gckOS_ReadRegisterEx(Hardware->os,
                             Hardware->core,
                             Hardware->powerBaseAddress
                             + 0x00104,
                             &data));

#if gcdDISABLE_FE_CLOCK_GATING
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0)));
#endif

#if gcdDISABLE_PE_CLOCK_GATING
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ?
 2:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 2:2) - (0 ?
 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ?
 2:2)));
#endif

#if gcdDISABLE_SH_CLOCK_GATING
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ?
 3:3))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 3:3) - (0 ?
 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ?
 3:3)));
#endif

#if gcdDISABLE_PA_CLOCK_GATING
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ?
 4:4))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 4:4) - (0 ?
 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ?
 4:4)));
#endif

#if gcdDISABLE_SE_CLOCK_GATING
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ?
 5:5))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 5:5) - (0 ?
 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ?
 5:5)));
#endif

#if gcdDISABLE_RA_CLOCK_GATING
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ?
 6:6))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 6:6) - (0 ?
 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ?
 6:6)));
#endif

#if gcdDISABLE_TX_CLOCK_GATING
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ?
 7:7))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 7:7) - (0 ?
 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ?
 7:7)));
#endif

#if gcdDISABLE_RA_EZ_CLOCK_GATING
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ?
 16:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 16:16) - (0 ?
 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ?
 16:16)));
#endif

#if gcdDISABLE_RA_HZ_CLOCK_GATING
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 17:17) - (0 ? 17:17) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:17) - (0 ? 17:17) + 1))))))) << (0 ?
 17:17))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 17:17) - (0 ?
 17:17) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:17) - (0 ? 17:17) + 1))))))) << (0 ?
 17:17)));
#endif

    gcmkVERIFY_OK(
        gckOS_WriteRegisterEx(Hardware->os,
                              Hardware->core,
                              Hardware->powerBaseAddress
                              + 0x00104,
                              data));

#if gcdDISABLE_STARVE_MODULE_CLOCK_GATING
    gcmkVERIFY_OK(
        gckOS_ReadRegisterEx(Hardware->os,
                             Hardware->core,
                             Hardware->powerBaseAddress +
                             0x00100,
                             &data));

    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ?
 2:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 2:2) - (0 ?
 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ?
 2:2)));

    gcmkVERIFY_OK(
        gckOS_WriteRegisterEx(Hardware->os,
                              Hardware->core,
                              Hardware->powerBaseAddress
                              + 0x00100,
                              data));

#endif

#if gcdDISABLE_MODULE_CLOCK_GATING
    gcmkVERIFY_OK(
        gckOS_ReadRegisterEx(Hardware->os,
                             Hardware->core,
                             Hardware->powerBaseAddress +
                             0x00100,
                             &data));

    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0)));


    gcmkVERIFY_OK(
        gckOS_WriteRegisterEx(Hardware->os,
                              Hardware->core,
                              Hardware->powerBaseAddress
                              + 0x00100,
                              data));
#endif

    return gcvSTATUS_OK;
}
#endif

#if gcdPOWEROFF_TIMEOUT
void
_PowerTimerFunction(
    gctPOINTER Data
    )
{
    gckHARDWARE hardware = (gckHARDWARE)Data;
    gcmkVERIFY_OK(
        gckHARDWARE_SetPowerManagementState(hardware, gcvPOWER_OFF_TIMEOUT));
}
#endif

static gceSTATUS
_VerifyDMA(
    IN gckOS Os,
    IN gceCORE Core,
    gctUINT32_PTR Address1,
    gctUINT32_PTR Address2,
    gctUINT32_PTR State1,
    gctUINT32_PTR State2
    )
{
    gceSTATUS status;
    gctUINT32 i;

    gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x00660, State1));
    gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x00660, State1));
    gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x00664, Address1));
    gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x00664, Address1));

    for (i = 0; i < 500; i += 1)
    {
        gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x00660, State2));
        gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x00660, State2));
        gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x00664, Address2));
        gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x00664, Address2));

        if (*Address1 != *Address2)
        {
            break;
        }

        if (*State1 != *State2)
        {
            break;
        }
    }

OnError:
    return status;
}

static gceSTATUS
_DumpDebugRegisters(
    IN gckOS Os,
    IN gceCORE Core,
    IN gcsiDEBUG_REGISTERS_PTR Descriptor
    )
{
/* If this value is changed, print formats need to be changed too. */
#define REG_PER_LINE 8
    gceSTATUS status = gcvSTATUS_OK;
    gctUINT32 select;
    gctUINT i, j, pipe;
    gctUINT32 datas[REG_PER_LINE];
    gctUINT32 oldControl, control;

    gcmkHEADER_ARG("Os=0x%X Descriptor=0x%X", Os, Descriptor);

    /* Record control. */
    gckOS_ReadRegisterEx(Os, Core, 0x0, &oldControl);

    for (pipe = 0; pipe < 2; pipe++)
    {
        if (!(Descriptor->pipeMask & (1 << pipe)))
        {
            continue;
        }

        gcmkPRINT_N(8, "    %s[%d] debug registers:\n", Descriptor->module, pipe);

        /* Switch pipe. */
        gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x0, &control));
        control &= ~(0xF << 20);
        control |= (pipe << 20);
        gcmkONERROR(gckOS_WriteRegisterEx(Os, Core, 0x0, control));

        gcmkASSERT(Descriptor->count % REG_PER_LINE);

        for (i = 0; i < Descriptor->count; i += REG_PER_LINE)
        {
            /* Select of first one in the group. */
            select = i + Descriptor->selectStart;

            /* Read a group of registers. */
            for (j = 0; j < REG_PER_LINE; j++)
            {
                /* Shift select to right position. */
                gcmkONERROR(gckOS_WriteRegisterEx(Os, Core, Descriptor->index, (select + j) << Descriptor->shift));
                gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, Descriptor->data, &datas[j]));
            }

            gcmkPRINT_N(32, "    [%02X] %08X %08X %08X %08X %08X %08X %08X %08X\n",
                        select, datas[0], datas[1], datas[2], datas[3], datas[4], datas[5], datas[6], datas[7]);
        }
    }

    /* Restore control. */
    gcmkONERROR(gckOS_WriteRegisterEx(Os, Core, 0x0, oldControl));

OnError:
    /* Return the error. */
    gcmkFOOTER();
    return status;
}

static gceSTATUS
_DumpLinkStack(
    IN gckOS Os,
    IN gceCORE Core,
    IN gcsiDEBUG_REGISTERS_PTR Descriptor
    )
{
    /* Get wrptr */
    gctUINT32 shift = Descriptor->shift;
    gctUINT32 pointerSelect = 0xE << shift;
    gctUINT32 pointer, wrPtr, rdPtr, links[16];
    gctUINT32 stackSize = 16;
    gctUINT32 oldestPtr = 0;
    gctUINT32 i;

    gcmkVERIFY_OK(gckOS_WriteRegisterEx(Os, Core, Descriptor->index, pointerSelect));
    gcmkVERIFY_OK(gckOS_ReadRegisterEx(Os, Core, Descriptor->data, &pointer));

    wrPtr = (pointer & 0xF0) >> 4;
    rdPtr = pointer & 0xF;

    /* Move rdptr to the oldest one (next one to the latest one. ) */
    oldestPtr = (wrPtr + 1) % stackSize;

    while (rdPtr != oldestPtr)
    {
        gcmkVERIFY_OK(gckOS_WriteRegisterEx(Os, Core, Descriptor->index, 0x0));
        gcmkVERIFY_OK(gckOS_WriteRegisterEx(Os, Core, Descriptor->index, 0xF << shift));


        gcmkVERIFY_OK(gckOS_WriteRegisterEx(Os, Core, Descriptor->index, pointerSelect));
        gcmkVERIFY_OK(gckOS_ReadRegisterEx(Os, Core, Descriptor->data, &pointer));

        rdPtr = pointer & 0xF;
    }

    gcmkPRINT("    Link stack:");

    /* Read from stack bottom*/
    for (i = 0; i < stackSize; i++)
    {
        gcmkVERIFY_OK(gckOS_WriteRegisterEx(Os, Core, Descriptor->index, 0xD << shift));
        gcmkVERIFY_OK(gckOS_ReadRegisterEx(Os, Core, Descriptor->data, &links[i]));

        /* Advance rdPtr. */
        gcmkVERIFY_OK(gckOS_WriteRegisterEx(Os, Core, Descriptor->index, 0x0));
        gcmkVERIFY_OK(gckOS_WriteRegisterEx(Os, Core, Descriptor->index, 0xF << shift));
    }

    /* Print. */
    for (i = 0; i < stackSize; i += 4)
    {
        gcmkPRINT_N(32, "      [0x%02X] 0x%08X [0x%02X] 0x%08X [0x%02X] 0x%08X [0x%02X] 0x%08X\n",
                    i, links[i], i + 1, links[i + 1], i + 2, links[i + 2], i + 3, links[i + 3]);
    }

    return gcvSTATUS_OK;
}

static gceSTATUS
_DumpFEStack(
    IN gckOS Os,
    IN gceCORE Core,
    IN gcsiDEBUG_REGISTERS_PTR Descriptor
    )
{
    gctUINT i;
    gctINT j;
    gctUINT32 stack[32][2];
    gctUINT32 link[32];

    static gcsFE_STACK _feStacks[] =
    {
        { "PRE_STACK", 32, 0x1A, 0x9A, 0x00, 0x1B, 0x1E },
        { "CMD_STACK", 32, 0x1C, 0x9C, 0x1E, 0x1D, 0x1E },
    };

    for (i = 0; i < gcmCOUNTOF(_feStacks); i++)
    {
        gckOS_WriteRegisterEx(Os, Core, Descriptor->index, _feStacks[i].clear);

        for (j = 0; j < _feStacks[i].count; j++)
        {
            gckOS_WriteRegisterEx(Os, Core, Descriptor->index, _feStacks[i].highSelect);

            gckOS_ReadRegisterEx(Os, Core, Descriptor->data, &stack[j][0]);

            gckOS_WriteRegisterEx(Os, Core, Descriptor->index, _feStacks[i].lowSelect);

            gckOS_ReadRegisterEx(Os, Core, Descriptor->data, &stack[j][1]);

            gckOS_WriteRegisterEx(Os, Core, Descriptor->index, _feStacks[i].next);

            if (_feStacks[i].linkSelect)
            {
                gckOS_WriteRegisterEx(Os, Core, Descriptor->index, _feStacks[i].linkSelect);

                gckOS_ReadRegisterEx(Os, Core, Descriptor->data, &link[j]);
            }
        }

        gcmkPRINT("  %s:", _feStacks[i].name);

        for (j = 31; j >= 3; j -= 4)
        {
            gcmkPRINT("    %08X %08X %08X %08X %08X %08X %08X %08X",
                      stack[j][0], stack[j][1], stack[j - 1][0], stack[j - 1][1],
                      stack[j - 2][0], stack[j - 2][1], stack[j - 3][0], stack[j - 3][1]);
        }

        if (_feStacks[i].linkSelect)
        {
            gcmkPRINT("  LINK_STACK:");

            for (j = 31; j >= 3; j -= 4)
            {
                gcmkPRINT("    %08X %08X %08X %08X %08X %08X %08X %08X",
                          link[j], link[j], link[j - 1], link[j - 1],
                          link[j - 2], link[j - 2], link[j - 3], link[j - 3]);
            }
        }

    }

    return gcvSTATUS_OK;
}

static gceSTATUS
_IsGPUPresent(
    IN gckHARDWARE Hardware
    )
{
    gceSTATUS status;
    gcsHARDWARE_SIGNATURE signature;
    gctUINT32 control;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x00000,
                                     &control));

    control = ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 1:1) - (0 ?
 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1)));
    control = ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0)));

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00000,
                                      control));

    gckOS_ZeroMemory((gctPOINTER)&signature, gcmSIZEOF(gcsHARDWARE_SIGNATURE));

    /* Identify the hardware. */
    gcmkONERROR(_GetHardwareSignature(Hardware,
                                      Hardware->os,
                                      Hardware->core,
                                      &signature));

    /* Check if these are the same values as saved before. */
    if ((Hardware->signature.chipModel          != signature.chipModel)
    ||  (Hardware->signature.chipRevision       != signature.chipRevision)
    ||  (Hardware->signature.chipFeatures       != signature.chipFeatures)
    ||  (Hardware->signature.chipMinorFeatures  != signature.chipMinorFeatures)
    ||  (Hardware->signature.chipMinorFeatures1 != signature.chipMinorFeatures1)
    ||  (Hardware->signature.chipMinorFeatures2 != signature.chipMinorFeatures2)
    )
    {
        gcmkPRINT("[galcore]: GPU is not present.");
        gcmkONERROR(gcvSTATUS_GPU_NOT_RESPONDING);
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the error. */
    gcmkFOOTER();
    return status;
}

gceSTATUS
_FlushCache(
    gckHARDWARE Hardware,
    gckCOMMAND Command
    )
{
    gceSTATUS status;
    gctUINT32 bytes, requested;
    gctPOINTER buffer;

    /* Get the size of the flush command. */
    gcmkONERROR(gckHARDWARE_Flush(Hardware,
                                  gcvFLUSH_ALL,
                                  gcvNULL,
                                  &requested));

    /* Reserve space in the command queue. */
    gcmkONERROR(gckCOMMAND_Reserve(Command,
                                   requested,
                                   &buffer,
                                   &bytes));

    /* Append a flush. */
    gcmkONERROR(gckHARDWARE_Flush(
        Hardware, gcvFLUSH_ALL, buffer, &bytes
        ));

    /* Execute the command queue. */
    gcmkONERROR(gckCOMMAND_Execute(Command, requested));

    return gcvSTATUS_OK;

OnError:
    return status;
}

static gctBOOL
_IsGPUIdle(
    IN gctUINT32 Idle
    )
{
    return Idle == 0x7FFFFFFF;
}

gctBOOL
_QueryFeatureDatabase(
    IN gckHARDWARE Hardware,
    IN gceFEATURE Feature
    )
{
    gctBOOL available;

    gcsFEATURE_DATABASE *database = Hardware->featureDatabase;

    gcmkHEADER_ARG("Hardware=0x%x Feature=%d", Hardware, Feature);

     /* Only features needed by common kernel logic added here. */
    switch (Feature)
    {
    case gcvFEATURE_END_EVENT:
        available = gcvFALSE;
        break;

    case gcvFEATURE_MC20:
        available = database->REG_MC20;
        break;

    case gcvFEATURE_EARLY_Z:
        available = database->REG_NoEZ == 0;
        break;

    case gcvFEATURE_HZ:
        available = database->REG_HierarchicalZ;
        break;

    case gcvFEATURE_NEW_HZ:
        available = database->REG_NewHZ;
        break;

    case gcvFEATURE_FAST_MSAA:
        available = database->REG_FastMSAA;
        break;

    case gcvFEATURE_SMALL_MSAA:
        available = database->REG_SmallMSAA;
        break;

    case gcvFEATURE_DYNAMIC_FREQUENCY_SCALING:
        /* This feature doesn't apply for 2D cores. */
        available = database->REG_DynamicFrequencyScaling && database->REG_Pipe3D;

        if (Hardware->identity.chipModel == gcv1000 &&
            (Hardware->identity.chipRevision == 0x5039 ||
            Hardware->identity.chipRevision == 0x5040))
        {
            available = gcvFALSE;
        }
        break;

    case gcvFEATURE_ACE:
        available = database->REG_ACE;
        break;

    case gcvFEATURE_HALTI2:
        available = database->REG_Halti2;
        break;

    case gcvFEATURE_PIPE_2D:
        available = database->REG_Pipe2D;
        break;

    case gcvFEATURE_PIPE_3D:
#if gcdENABLE_3D
        available = database->REG_Pipe3D;
#else
        available = gcvFALSE;
#endif
        break;

    case gcvFEATURE_FC_FLUSH_STALL:
        available = database->REG_FcFlushStall;
        break;

    case gcvFEATURE_BLT_ENGINE:
        available = database->REG_BltEngine;
       break;

    case gcvFEATURE_HALTI0:
        available = database->REG_Halti0;
        break;

    case gcvFEATURE_FE_ALLOW_STALL_PREFETCH_ENG:
        available = database->REG_FEAllowStallPrefetchEng;
        break;

    case gcvFEATURE_MMU:
        available = database->REG_MMU;
        break;

    case gcvFEATURE_FENCE:
        available = database->REG_Halti5;
        break;

    case gcvFEATURE_TEX_BASELOD:
        available = database->REG_Halti2;

        if (_IsHardwareMatch(Hardware, gcv900, 0x5250))
        {
            available = gcvTRUE;
        }
        break;

    case gcvFEATURE_TEX_CACHE_FLUSH_FIX:
        available = database->REG_Halti5;
        break;

    case gcvFEATURE_BUG_FIXES1:
        available = database->REG_BugFixes1;
        break;

    case gcvFEATURE_MULTI_SOURCE_BLT:
        available = database->REG_MultiSourceBlt;
        break;

    case gcvFEATURE_HALTI5:
        available = database->REG_Halti5;
        break;

    case gcvFEATURE_FAST_CLEAR:
        available = database->REG_FastClear;

        if (Hardware->identity.chipModel == gcv700)
        {
            available = gcvFALSE;
        }
        break;

    case gcvFEATURE_BUG_FIXES7:
        available = database->REG_BugFixes7;
        break;

    case gcvFEATURE_ZCOMPRESSION:
        available = database->REG_ZCompression;
        break;

    case gcvFEATURE_SHADER_HAS_INSTRUCTION_CACHE:
        available = database->REG_InstructionCache;
        break;

    case gcvFEATURE_YUV420_TILER:
        available = database->REG_YUV420Tiler;
        break;

    case gcvFEATURE_2DPE20:
        available = database->REG_2DPE20;
        break;

    case gcvFEATURE_DITHER_AND_FILTER_PLUS_ALPHA_2D:
        available = database->REG_DitherAndFilterPlusAlpha2D;
        break;

    case gcvFEATURE_ONE_PASS_2D_FILTER:
        available = database->REG_OnePass2DFilter;
        break;

    case gcvFEATURE_HALTI1:
        available = database->REG_Halti1;
        break;

    case gcvFEATURE_HALTI3:
        available = database->REG_Halti3;
        break;

    case gcvFEATURE_HALTI4:
        available = database->REG_Halti4;
        break;

    case gcvFEATURE_GEOMETRY_SHADER:
        available = database->REG_GeometryShader;
        break;

    case gcvFEATURE_TESSELLATION:
        available = database->REG_TessellationShaders;
        break;

    case gcvFEATURE_GENERIC_ATTRIB:
        available = database->REG_Generics;
        break;

    case gcvFEATURE_TEXTURE_LINEAR:
        available = database->REG_LinearTextureSupport;
        break;

    case gcvFEATURE_TX_FILTER:
        available = database->REG_TXFilter;
        break;

    case gcvFEATURE_TX_SUPPORT_DEC:
        available = database->REG_TXSupportDEC;
        break;

    case gcvFEATURE_TX_FRAC_PRECISION_6BIT:
        available = database->REG_TX6bitFrac;
        break;

    case gcvFEATURE_TEXTURE_ASTC:
        available = database->REG_TXEnhancements4 && !database->NO_ASTC;
        break;

    case gcvFEATURE_SHADER_ENHANCEMENTS2:
        available = database->REG_SHEnhancements2;
        break;

    case gcvFEATURE_BUG_FIXES18:
        available = database->REG_BugFixes18;
        break;

    case gcvFEATURE_64K_L2_CACHE:
        available = gcvFALSE;
        break;

    case gcvFEATURE_BUG_FIXES4:
        available = database->REG_BugFixes4;
        break;

    case gcvFEATURE_BUG_FIXES12:
        available = database->REG_BugFixes12;
        break;

    case gcvFEATURE_HW_TFB:
        available = database->HWTFB;
        break;

    case gcvFEATURE_SNAPPAGE_CMD_FIX:
        available = database->SH_SNAP2PAGE_FIX;
        break;

    case gcvFEATURE_SECURITY:
        available = database->SECURITY;
        break;

    case gcvFEATURE_TX_DESCRIPTOR:
        available = database->REG_Halti5;
        break;

    case gcvFEATURE_TX_DESC_CACHE_CLOCKGATE_FIX:
        available = database->TX_DESC_CACHE_CLOCKGATE_FIX;
        break;

    case gcvFEATURE_ROBUSTNESS:
        available = database->ROBUSTNESS;
        break;

    case gcvFEATURE_SNAPPAGE_CMD:
        available = database->SNAPPAGE_CMD;
        break;

    case gcvFEATURE_HALF_FLOAT_PIPE:
        available = database->REG_HalfFloatPipe;
        break;

    case gcvFEATURE_SH_INSTRUCTION_PREFETCH:
        available = database->SH_ICACHE_PREFETCH;
        break;

    case gcvFEATURE_FE_NEED_DUMMYDRAW:
        available = database->FE_NEED_DUMMYDRAW;
        break;

    case gcvFEATURE_DEC_COMPRESSION:
        available = database->REG_DEC;
        break;

    case gcvFEATURE_USC_DEFER_FILL_FIX:
        available = database->USC_DEFER_FILL_FIX;
        break;

    case gcvFEATURE_USC:
        available = database->REG_Halti5;
        break;

    default:
        gcmkFATAL("Invalid feature has been requested.");
        available = gcvFALSE;
    }

    gcmkFOOTER_ARG("%d", available ? gcvSTATUS_TRUE : gcvSTATUS_FALSE);
    return available;
}

static void
_ConfigurePolicyID(
    IN gckHARDWARE Hardware
    )
{
    gceSTATUS status;
    gctUINT32 policyID;
    gctUINT32 auxBit = ~0U;
    gctUINT32 axiConfig;
    gckOS os = Hardware->os;
    gceCORE core = Hardware->core;
    gctUINT32 i;
    gctUINT32 offset;
    gctUINT32 shift;
    gctUINT32 currentAxiConfig;

    status = gckOS_GetPolicyID(os, gcvSURF_TYPE_UNKNOWN, &policyID, &axiConfig);

    if (status == gcvSTATUS_NOT_SUPPORTED)
    {
        /* No customized policyID setting. */
        return;
    }

    for (i = 0; i < 16; i++)
    {
        /* Mapping 16 surface type.*/
        status = gckOS_GetPolicyID(os, (gceSURF_TYPE) i, &policyID, &axiConfig);

        if (gcmIS_SUCCESS(status))
        {
            if (auxBit == ~0U)
            {
                /* There is a customized policyID setting for this type. */
                auxBit = (policyID >> 4) & 0x1;
            }
            else
            {
                /* Check whether this bit changes. */
                if (auxBit != ((policyID >> 4) & 0x1))
                {
                    gcmkPRINT("[galcore]: AUX_BIT changes");
                    return;
                }
            }

            offset = policyID >> 1;

            shift  = (policyID & 0x1) * 16;

            axiConfig &= 0xFFFF;

            gcmkVERIFY_OK(gckOS_ReadRegisterEx(
                os,
                core,
                (0x0070 + offset) << 2,
                &currentAxiConfig
                ));

            currentAxiConfig |= (axiConfig << shift);

            gcmkVERIFY_OK(gckOS_WriteRegisterEx(
                os,
                core,
                (0x0070 + offset) << 2,
                currentAxiConfig
                ));
        }
    }

    if (auxBit != ~0U)
    {
        gcmkVERIFY_OK(gckOS_WriteRegisterEx(
            os,
            core,
            0x000EC,
            ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 8:8) - (0 ?
 8:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:8) - (0 ? 8:8) + 1))))))) << (0 ?
 8:8))) | (((gctUINT32) ((gctUINT32) (auxBit) & ((gctUINT32) ((((1 ? 8:8) - (0 ?
 8:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:8) - (0 ? 8:8) + 1))))))) << (0 ?
 8:8)))
            ));
    }
}

/*
* State timer helper must be called with powerMutex held.
*/
void
gckSTATETIMER_Reset(
    IN gcsSTATETIMER * StateTimer,
    IN gctUINT64 Start
    )
{
    gctUINT64 now;

    if (Start)
    {
        now = Start;
    }
    else
    {
        gckOS_GetProfileTick(&now);
    }

    StateTimer->recent = StateTimer->start = now;

    gckOS_ZeroMemory(StateTimer->elapse, gcmSIZEOF(StateTimer->elapse));
}

void
gckSTATETIMER_Accumulate(
    IN gcsSTATETIMER * StateTimer,
    IN gceCHIPPOWERSTATE OldState
    )
{
    gctUINT64 now;
    gctUINT64 elapse;

    gckOS_GetProfileTick(&now);

    elapse = now - StateTimer->recent;

    StateTimer->recent = now;

    StateTimer->elapse[OldState] += elapse;
}

void
gckSTATETIMER_Query(
    IN gcsSTATETIMER * StateTimer,
    IN gceCHIPPOWERSTATE State,
    OUT gctUINT64_PTR Start,
    OUT gctUINT64_PTR End,
    OUT gctUINT64_PTR On,
    OUT gctUINT64_PTR Off,
    OUT gctUINT64_PTR Idle,
    OUT gctUINT64_PTR Suspend
    )
{
    *Start = StateTimer->start;

    gckSTATETIMER_Accumulate(StateTimer, State);

    *End = StateTimer->recent;

    *On      = StateTimer->elapse[gcvPOWER_ON];
    *Off     = StateTimer->elapse[gcvPOWER_OFF];
    *Idle    = StateTimer->elapse[gcvPOWER_IDLE];
    *Suspend = StateTimer->elapse[gcvPOWER_SUSPEND];

    gckSTATETIMER_Reset(StateTimer, StateTimer->recent);
}

/******************************************************************************\
****************************** gckHARDWARE API code *****************************
\******************************************************************************/

/*******************************************************************************
**
**  gckHARDWARE_Construct
**
**  Construct a new gckHARDWARE object.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an initialized gckOS object.
**
**      gceCORE Core
**          Specified core.
**
**  OUTPUT:
**
**      gckHARDWARE * Hardware
**          Pointer to a variable that will hold the pointer to the gckHARDWARE
**          object.
*/
gceSTATUS
gckHARDWARE_Construct(
    IN gckOS Os,
    IN gceCORE Core,
    OUT gckHARDWARE * Hardware
    )
{
    gceSTATUS status;
    gckHARDWARE hardware = gcvNULL;
    gctUINT16 data = 0xff00;
    gctPOINTER pointer = gcvNULL;

    gcmkHEADER_ARG("Os=0x%x", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Hardware != gcvNULL);

    /* Enable the GPU. */
    gcmkONERROR(gckOS_SetGPUPower(Os, Core, gcvTRUE, gcvTRUE));
    gcmkONERROR(gckOS_WriteRegisterEx(Os,
                                      Core,
                                      0x00000,
                                      0x00000900));

    /* Allocate the gckHARDWARE object. */
    gcmkONERROR(gckOS_Allocate(Os,
                               gcmSIZEOF(struct _gckHARDWARE),
                               &pointer));

    gckOS_ZeroMemory(pointer, gcmSIZEOF(struct _gckHARDWARE));

    hardware = (gckHARDWARE) pointer;

    /* Initialize the gckHARDWARE object. */
    hardware->object.type = gcvOBJ_HARDWARE;
    hardware->os          = Os;
    hardware->core        = Core;

    gcmkONERROR(_GetHardwareSignature(hardware, Os, Core, &hardware->signature));

    /* Identify the hardware. */
    gcmkONERROR(_IdentifyHardwareByDatabase(hardware, Os, Core, &hardware->identity));

    /* Get the system's physical base address. */
    gcmkONERROR(gckOS_GetBaseAddress(Os, &hardware->baseAddress));

    /* Determine the hardware type */
    if (gckHARDWARE_IsFeatureAvailable(hardware, gcvFEATURE_PIPE_3D)
     && gckHARDWARE_IsFeatureAvailable(hardware, gcvFEATURE_PIPE_2D)
    )
    {
        hardware->type = gcvHARDWARE_3D2D;
    }
    else
    if (gckHARDWARE_IsFeatureAvailable(hardware, gcvFEATURE_PIPE_2D))
    {
        hardware->type = gcvHARDWARE_2D;
    }
    else
    {
        hardware->type = gcvHARDWARE_3D;
    }

    hardware->powerBaseAddress
        = ((hardware->identity.chipModel   == gcv300)
        && (hardware->identity.chipRevision < 0x2000))
            ? 0x0100
            : 0x0000;

    /* _ResetGPU need powerBaseAddress. */
    status = _ResetGPU(hardware, Os, Core);

    if (status != gcvSTATUS_OK)
    {
        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
            "_ResetGPU failed: status=%d\n", status);
    }

#if gcdDEC_ENABLE_AHB
    gcmkONERROR(gckOS_WriteRegisterEx(Os, gcvCORE_DEC, 0x18180, ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 22:22) - (0 ? 22:22) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 22:22) - (0 ? 22:22) + 1))))))) << (0 ?
 22:22))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 22:22) - (0 ? 22:22) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 22:22) - (0 ? 22:22) + 1))))))) << (0 ? 22:22)))));
#endif

    if (gckHARDWARE_IsFeatureAvailable(hardware, gcvFEATURE_64K_L2_CACHE) == gcvFALSE)
    {
        /* VIV: On all cores without 64k L2, we disable L2, no matter whether L2 exists or not. */
        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                                          Core,
                                          0x0055C,
                                          0x00FFFFFF));
    }

    hardware->powerMutex = gcvNULL;

    hardware->mmuVersion = gckHARDWARE_IsFeatureAvailable(hardware, gcvFEATURE_MMU);


    /* Determine whether bug fixes #1 are present. */
    hardware->extraEventStates = (gckHARDWARE_IsFeatureAvailable(hardware, gcvFEATURE_BUG_FIXES1) == gcvFALSE);

    /* Check if big endian */
    hardware->bigEndian = (*(gctUINT8 *)&data == 0xff);

    /* Initialize the fast clear. */
    gcmkONERROR(gckHARDWARE_SetFastClear(hardware, -1, -1));

#if !gcdENABLE_128B_MERGE

    if (gckHARDWARE_IsFeatureAvailable(hardware, gcvFEATURE_MULTI_SOURCE_BLT))
    {
        /* 128B merge is turned on by default. Disable it. */
        gcmkONERROR(gckOS_WriteRegisterEx(Os, Core, 0x00558, 0));
    }

#endif

    {
        gctUINT32 value;
        gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x00090, &value));
#if gcdDEC_ENABLE_AHB
        if (gckHARDWARE_IsFeatureAvailable(hardware, gcvFEATURE_DEC_COMPRESSION))
        {
            value |= ~0xFFFFFFBF;
        }
        else
#endif
        {
            value &= 0xFFFFFFBF;
        }
        gcmkONERROR(gckOS_WriteRegisterEx(Os, Core, 0x00090, value));
    }

    /* Set power state to ON. */
    hardware->chipPowerState  = gcvPOWER_ON;
    hardware->clockState      = gcvTRUE;
    hardware->powerState      = gcvTRUE;
    hardware->lastWaitLink    = ~0U;
    hardware->lastEnd         = ~0U;
    hardware->globalSemaphore = gcvNULL;
#if gcdENABLE_FSCALE_VAL_ADJUST
    hardware->powerOnFscaleVal = 64;
#endif

    gcmkONERROR(gckOS_CreateMutex(Os, &hardware->powerMutex));
    gcmkONERROR(gckOS_CreateSemaphore(Os, &hardware->globalSemaphore));
    hardware->startIsr = gcvNULL;
    hardware->stopIsr = gcvNULL;

#if gcdPOWEROFF_TIMEOUT
    hardware->powerOffTimeout = gcdPOWEROFF_TIMEOUT;

    gcmkVERIFY_OK(gckOS_CreateTimer(Os,
                                    _PowerTimerFunction,
                                    (gctPOINTER)hardware,
                                    &hardware->powerOffTimer));
#endif

    gcmkONERROR(gckOS_AtomConstruct(Os, &hardware->pageTableDirty));
    gcmkONERROR(gckOS_AtomConstruct(Os, &hardware->pendingEvent));

    status = gckOS_QueryOption(Os, "powerManagement", (gctUINT32*)&hardware->powerManagement);

    if (status == gcvSTATUS_NOT_SUPPORTED)
    {
        /* Enable power management by default. */
        hardware->powerManagement = gcvTRUE;
    }

    /* Disable profiler by default */
    hardware->gpuProfiler = gcvFALSE;
    status = gckOS_QueryOption(Os, "gpuProfiler", (gctUINT32*)&hardware->gpuProfiler);
    if (status == gcvSTATUS_NOT_SUPPORTED)
    {
        /* Enable power management by default. */
        hardware->gpuProfiler= gcvFALSE;
    }

#if defined(LINUX) || defined(__QNXNTO__) || defined(UNDER_CE)
    if (hardware->mmuVersion)
    {
        hardware->stallFEPrefetch
            = gckHARDWARE_IsFeatureAvailable(hardware, gcvFEATURE_FE_ALLOW_STALL_PREFETCH_ENG);
    }
    else
#endif
    {
        hardware->stallFEPrefetch = gcvTRUE;
    }

    gcmkONERROR(gckOS_QueryOption(Os, "mmu", (gctUINT32_PTR)&hardware->enableMMU));

    hardware->minFscaleValue = 1;
    hardware->waitCount = 200;

    gckSTATETIMER_Reset(&hardware->powerStateTimer, 0);

#if gcdLINK_QUEUE_SIZE
    gcmkONERROR(gckQUEUE_Allocate(hardware->os, &hardware->linkQueue, gcdLINK_QUEUE_SIZE));
#endif

    if (gckHARDWARE_IsFeatureAvailable(hardware, gcvFEATURE_SECURITY))
    {
        gctUINT32 ta = 0;

        status = gckOS_QueryOption(Os, "TA", &ta);

        if (gcmIS_SUCCESS(status))
        {
            hardware->secureMode = ta ? gcvSECURE_IN_TA : gcvSECURE_IN_NORMAL;
        }
    }

    if (hardware->secureMode == gcvSECURE_IN_NORMAL)
    {
        hardware->pagetableArray.size = 4096;

        gcmkONERROR(gckOS_AllocateNonPagedMemory(
            hardware->os,
            gcvFALSE,
            &hardware->pagetableArray.size,
            &hardware->pagetableArray.physical,
            &hardware->pagetableArray.logical
            ));

        gcmkONERROR(gckOS_GetPhysicalAddress(
            hardware->os,
            hardware->pagetableArray.logical,
            &hardware->pagetableArray.address
            ));
    }

    /* Return pointer to the gckHARDWARE object. */
    *Hardware = hardware;

    /* Success. */
    gcmkFOOTER_ARG("*Hardware=0x%x", *Hardware);
    return gcvSTATUS_OK;

OnError:
    /* Roll back. */
    if (hardware != gcvNULL)
    {
        /* Turn off the power. */
        gcmkVERIFY_OK(gckOS_SetGPUPower(Os, Core, gcvFALSE, gcvFALSE));

        if (hardware->globalSemaphore != gcvNULL)
        {
            /* Destroy the global semaphore. */
            gcmkVERIFY_OK(gckOS_DestroySemaphore(Os,
                                                 hardware->globalSemaphore));
        }

        if (hardware->powerMutex != gcvNULL)
        {
            /* Destroy the power mutex. */
            gcmkVERIFY_OK(gckOS_DeleteMutex(Os, hardware->powerMutex));
        }

#if gcdPOWEROFF_TIMEOUT
        if (hardware->powerOffTimer != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_StopTimer(Os, hardware->powerOffTimer));
            gcmkVERIFY_OK(gckOS_DestroyTimer(Os, hardware->powerOffTimer));
        }
#endif

        if (hardware->pageTableDirty != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_AtomDestroy(Os, hardware->pageTableDirty));
        }

        if (hardware->pendingEvent != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_AtomDestroy(Os, hardware->pendingEvent));
        }

        if (hardware->pagetableArray.logical != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_FreeNonPagedMemory(
                Os,
                hardware->pagetableArray.size,
                hardware->pagetableArray.physical,
                hardware->pagetableArray.logical
                ));
        }

        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Os, hardware));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_Destroy
**
**  Destroy an gckHARDWARE object.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object that needs to be destroyed.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_Destroy(
    IN gckHARDWARE Hardware
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Destroy the power semaphore. */
    gcmkVERIFY_OK(gckOS_DestroySemaphore(Hardware->os,
                                         Hardware->globalSemaphore));

    /* Destroy the power mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Hardware->os, Hardware->powerMutex));

#if gcdPOWEROFF_TIMEOUT
    gcmkVERIFY_OK(gckOS_StopTimer(Hardware->os, Hardware->powerOffTimer));
    gcmkVERIFY_OK(gckOS_DestroyTimer(Hardware->os, Hardware->powerOffTimer));
#endif

    gcmkVERIFY_OK(gckOS_AtomDestroy(Hardware->os, Hardware->pageTableDirty));

    gcmkVERIFY_OK(gckOS_AtomDestroy(Hardware->os, Hardware->pendingEvent));

    if (Hardware->functionBytes)
    {
#if USE_KERNEL_VIRTUAL_BUFFERS
        if (Hardware->kernel->virtualCommandBuffer)
        {
            gckVIRTUAL_COMMAND_BUFFER_PTR    buffer = (gckVIRTUAL_COMMAND_BUFFER_PTR)Hardware->functionPhysical;

            gcmkVERIFY_OK(gckKERNEL_FreeVirtualMemory(
                Hardware->functionPhysical,
                Hardware->functionLogical,
                gcvTRUE
                ));
        }
        else
#endif
        {
            gcmkVERIFY_OK(gckOS_FreeNonPagedMemory(
                Hardware->os,
                Hardware->functionBytes,
                Hardware->functionPhysical,
                Hardware->functionLogical
                ));
        }
    }

#if gcdLINK_QUEUE_SIZE
    gckQUEUE_Free(Hardware->os, &Hardware->linkQueue);
#endif

    if (Hardware->pagetableArray.logical != gcvNULL)
    {
        gcmkVERIFY_OK(gckOS_FreeNonPagedMemory(
            Hardware->os,
            Hardware->pagetableArray.size,
            Hardware->pagetableArray.physical,
            Hardware->pagetableArray.logical
            ));
    }

    /* Mark the object as unknown. */
    Hardware->object.type = gcvOBJ_UNKNOWN;

    /* Free the object. */
    gcmkONERROR(gcmkOS_SAFE_FREE(Hardware->os, Hardware));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_GetType
**
**  Get the hardware type.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**  OUTPUT:
**
**      gceHARDWARE_TYPE * Type
**          Pointer to a variable that receives the type of hardware object.
*/
gceSTATUS
gckHARDWARE_GetType(
    IN gckHARDWARE Hardware,
    OUT gceHARDWARE_TYPE * Type
    )
{
    gcmkHEADER_ARG("Hardware=0x%x", Hardware);
    gcmkVERIFY_ARGUMENT(Type != gcvNULL);

    *Type = Hardware->type;

    gcmkFOOTER_ARG("*Type=%d", *Type);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckHARDWARE_InitializeHardware
**
**  Initialize the hardware.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_InitializeHardware(
    IN gckHARDWARE Hardware
    )
{
    gceSTATUS status;
    gctUINT32 control;
    gctUINT32 data;
    gctUINT32 regPMC = 0;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Disable isolate GPU bit. */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00000,
                                      ((((gctUINT32) (0x00000900)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ?
 19:19))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 19:19) - (0 ?
 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ?
 19:19)))));

    gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x00000,
                                     &control));

    /* Enable debug register. */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00000,
                                      ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 11:11) - (0 ? 11:11) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:11) - (0 ? 11:11) + 1))))))) << (0 ?
 11:11))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 11:11) - (0 ?
 11:11) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:11) - (0 ? 11:11) + 1))))))) << (0 ?
 11:11)))));

    /* Reset memory counters. */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x0003C,
                                      ~0U));

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x0003C,
                                      0));

    if (Hardware->mmuVersion == 0)
    {
        /* Program the base addesses. */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                          Hardware->core,
                                          0x0041C,
                                          Hardware->baseAddress));

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                          Hardware->core,
                                          0x00418,
                                          Hardware->baseAddress));

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                          Hardware->core,
                                          0x00428,
                                          Hardware->baseAddress));

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                          Hardware->core,
                                          0x00420,
                                          Hardware->baseAddress));

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                          Hardware->core,
                                          0x00424,
                                          Hardware->baseAddress));
    }

    gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     Hardware->powerBaseAddress +
                                     0x00100,
                                     &data));

    /* Enable clock gating. */
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0)));

    if ((Hardware->identity.chipRevision == 0x4301)
    ||  (Hardware->identity.chipRevision == 0x4302)
    )
    {
        /* Disable stall module level clock gating for 4.3.0.1 and 4.3.0.2
        ** revisions. */
        data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 1:1) - (0 ?
 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1)));
    }

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      Hardware->powerBaseAddress
                                      + 0x00100,
                                      data));

#if gcdENABLE_3D
    /* Disable PE clock gating on revs < 5.0 when HZ is present without a
    ** bug fix. */
    if ((Hardware->identity.chipRevision < 0x5000)
    &&  gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_HZ)
    &&  !gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_BUG_FIXES4)
    )
    {
        if (regPMC == 0)
        {
            gcmkONERROR(
                gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     Hardware->powerBaseAddress
                                     + 0x00104,
                                     &regPMC));
        }

        /* Disable PE clock gating. */
        regPMC = ((((gctUINT32) (regPMC)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ?
 2:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 2:2) - (0 ?
 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ?
 2:2)));
    }
#endif

    if (Hardware->identity.chipModel == gcv4000 &&
        ((Hardware->identity.chipRevision == 0x5208) || (Hardware->identity.chipRevision == 0x5222)))
    {
        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  0x0010C,
                                  ((((gctUINT32) (0x01590880)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:23) - (0 ? 23:23) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:23) - (0 ? 23:23) + 1))))))) << (0 ?
 23:23))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 23:23) - (0 ?
 23:23) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:23) - (0 ? 23:23) + 1))))))) << (0 ?
 23:23)))));
    }

    if ((Hardware->identity.chipModel == gcv1000 &&
         (Hardware->identity.chipRevision == 0x5039 ||
          Hardware->identity.chipRevision == 0x5040))
        ||
        (Hardware->identity.chipModel == gcv2000 &&
         Hardware->identity.chipRevision == 0x5140)
       )
    {
        gctUINT32 pulseEater;

        pulseEater = ((((gctUINT32) (0x01590880)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ?
 16:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 16:16) - (0 ?
 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ?
 16:16)));

        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  0x0010C,
                                  ((((gctUINT32) (pulseEater)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 17:17) - (0 ? 17:17) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:17) - (0 ? 17:17) + 1))))))) << (0 ?
 17:17))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 17:17) - (0 ?
 17:17) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:17) - (0 ? 17:17) + 1))))))) << (0 ?
 17:17)))));
    }

    if ((gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_HALTI2) == gcvSTATUS_FALSE)
     || (Hardware->identity.chipRevision < 0x5422)
    )
    {
        if (regPMC == 0)
        {
            gcmkONERROR(
                gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     Hardware->powerBaseAddress
                                     + 0x00104,
                                     &regPMC));
        }

        regPMC = ((((gctUINT32) (regPMC)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:15) - (0 ? 15:15) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:15) - (0 ? 15:15) + 1))))))) << (0 ?
 15:15))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 15:15) - (0 ?
 15:15) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:15) - (0 ? 15:15) + 1))))))) << (0 ?
 15:15)));
    }

    if (_IsHardwareMatch(Hardware, gcv2000, 0x5108))
    {
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 0x00480,
                                 &data));

        /* Set FE bus to one, TX bus to zero */
        data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ?
 3:3))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 3:3) - (0 ?
 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ?
 3:3)));
        data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ?
 7:7))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 7:7) - (0 ?
 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ?
 7:7)));

        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  0x00480,
                                  data));
    }

    gcmkONERROR(
        gckHARDWARE_SetMMU(Hardware,
                           Hardware->kernel->mmu->area[0].pageTableLogical));

    if (Hardware->identity.chipModel >= gcv400
    &&  Hardware->identity.chipModel != gcv420
    &&  !gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_BUG_FIXES12))
    {
        if (regPMC == 0)
        {
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 Hardware->powerBaseAddress
                                 + 0x00104,
                                 &regPMC));
        }

        /* Disable PA clock gating. */
        regPMC = ((((gctUINT32) (regPMC)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ?
 4:4))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 4:4) - (0 ?
 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ?
 4:4)));
    }

    /* Limit 2D outstanding request. */
    if (Hardware->maxOutstandingReads)
    {
        gctUINT32 data;

        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 0x00414,
                                 &data));

        data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (Hardware->maxOutstandingReads & 0xFF) & ((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0)));

        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  0x00414,
                                  data));
    }

    if (_IsHardwareMatch(Hardware, gcv1000, 0x5035))
    {
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 0x00414,
                                 &data));

        /* Disable HZ-L2. */
        data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ?
 12:12))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 12:12) - (0 ?
 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ?
 12:12)));

        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  0x00414,
                                  data));
    }

    if (_IsHardwareMatch(Hardware, gcv4000, 0x5222)
     || _IsHardwareMatch(Hardware, gcv2000, 0x5108)
     || (gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_TX_DESCRIPTOR)
       && !gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_TX_DESC_CACHE_CLOCKGATE_FIX)
        )
    )
    {
        if (regPMC == 0)
        {
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 Hardware->powerBaseAddress
                                 + 0x00104,
                                 &regPMC));
        }

        /* Disable TX clock gating. */
        regPMC = ((((gctUINT32) (regPMC)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ?
 7:7))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 7:7) - (0 ?
 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ?
 7:7)));
    }

    if (_IsHardwareMatch(Hardware, gcv880, 0x5106))
    {
        Hardware->kernel->timeOut = 140 * 1000;
    }

    if (regPMC == 0)
    {
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 Hardware->powerBaseAddress
                                 + 0x00104,
                                 &regPMC));
    }

    /* Disable RA HZ clock gating. */
    regPMC = ((((gctUINT32) (regPMC)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 17:17) - (0 ? 17:17) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:17) - (0 ? 17:17) + 1))))))) << (0 ?
 17:17))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 17:17) - (0 ?
 17:17) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:17) - (0 ? 17:17) + 1))))))) << (0 ?
 17:17)));

    /* Disable RA EZ clock gating. */
    regPMC = ((((gctUINT32) (regPMC)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ?
 16:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 16:16) - (0 ?
 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ?
 16:16)));

    if (regPMC != 0)
    {
        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  Hardware->powerBaseAddress
                                  + 0x00104,
                                  regPMC));
    }

    if (_IsHardwareMatch(Hardware, gcv2000, 0x5108)
     || (_IsHardwareMatch(Hardware, gcv2000, 0xffff5450))
     || _IsHardwareMatch(Hardware, gcv320, 0x5007)
     || _IsHardwareMatch(Hardware, gcv320, 0x5303)
     || _IsHardwareMatch(Hardware, gcv880, 0x5106)
     || _IsHardwareMatch(Hardware, gcv400, 0x4645)
    )
    {
        /* Update GPU AXI cache atttribute. */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                          Hardware->core,
                                          0x00008,
                                          0x00002200));
    }


    if ((Hardware->identity.chipRevision > 0x5420)
     && gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_PIPE_3D))
    {
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x0010C,
                                     &data));

        /* Disable internal DFS. */
        data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 18:18) - (0 ? 18:18) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 18:18) - (0 ? 18:18) + 1))))))) << (0 ?
 18:18))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 18:18) - (0 ?
 18:18) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 18:18) - (0 ? 18:18) + 1))))))) << (0 ?
 18:18)));

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x0010C,
                                      data));
    }


    /* VIV: #15495. */
    if (_IsHardwareMatch(Hardware, gcv2500, 0x5422))
    {
        gcmkONERROR(gckOS_ReadRegisterEx(
            Hardware->os, Hardware->core, 0x00090, &data));

        /* AXI switch setup to SPLIT_TO64 mode */
        data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 1:0) - (0 ? 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ?
 1:0))) | (((gctUINT32) (0x2 & ((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ? 1:0)));

        gcmkONERROR(gckOS_WriteRegisterEx(
            Hardware->os, Hardware->core, 0x00090, data));
    }

    _ConfigurePolicyID(Hardware);

#if gcdDEBUG_MODULE_CLOCK_GATING
    _ConfigureModuleLevelClockGating(Hardware);
#endif

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the error. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_QueryMemory
**
**  Query the amount of memory available on the hardware.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object.
**
**  OUTPUT:
**
**      gctSIZE_T * InternalSize
**          Pointer to a variable that will hold the size of the internal video
**          memory in bytes.  If 'InternalSize' is gcvNULL, no information of the
**          internal memory will be returned.
**
**      gctUINT32 * InternalBaseAddress
**          Pointer to a variable that will hold the hardware's base address for
**          the internal video memory.  This pointer cannot be gcvNULL if
**          'InternalSize' is also non-gcvNULL.
**
**      gctUINT32 * InternalAlignment
**          Pointer to a variable that will hold the hardware's base address for
**          the internal video memory.  This pointer cannot be gcvNULL if
**          'InternalSize' is also non-gcvNULL.
**
**      gctSIZE_T * ExternalSize
**          Pointer to a variable that will hold the size of the external video
**          memory in bytes.  If 'ExternalSize' is gcvNULL, no information of the
**          external memory will be returned.
**
**      gctUINT32 * ExternalBaseAddress
**          Pointer to a variable that will hold the hardware's base address for
**          the external video memory.  This pointer cannot be gcvNULL if
**          'ExternalSize' is also non-gcvNULL.
**
**      gctUINT32 * ExternalAlignment
**          Pointer to a variable that will hold the hardware's base address for
**          the external video memory.  This pointer cannot be gcvNULL if
**          'ExternalSize' is also non-gcvNULL.
**
**      gctUINT32 * HorizontalTileSize
**          Number of horizontal pixels per tile.  If 'HorizontalTileSize' is
**          gcvNULL, no horizontal pixel per tile will be returned.
**
**      gctUINT32 * VerticalTileSize
**          Number of vertical pixels per tile.  If 'VerticalTileSize' is
**          gcvNULL, no vertical pixel per tile will be returned.
*/
gceSTATUS
gckHARDWARE_QueryMemory(
    IN gckHARDWARE Hardware,
    OUT gctSIZE_T * InternalSize,
    OUT gctUINT32 * InternalBaseAddress,
    OUT gctUINT32 * InternalAlignment,
    OUT gctSIZE_T * ExternalSize,
    OUT gctUINT32 * ExternalBaseAddress,
    OUT gctUINT32 * ExternalAlignment,
    OUT gctUINT32 * HorizontalTileSize,
    OUT gctUINT32 * VerticalTileSize
    )
{
    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    if (InternalSize != gcvNULL)
    {
        /* No internal memory. */
        *InternalSize = 0;
    }

    if (ExternalSize != gcvNULL)
    {
        /* No external memory. */
        *ExternalSize = 0;
    }

    if (HorizontalTileSize != gcvNULL)
    {
        /* 4x4 tiles. */
        *HorizontalTileSize = 4;
    }

    if (VerticalTileSize != gcvNULL)
    {
        /* 4x4 tiles. */
        *VerticalTileSize = 4;
    }

    /* Success. */
    gcmkFOOTER_ARG("*InternalSize=%lu *InternalBaseAddress=0x%08x "
                   "*InternalAlignment=0x%08x *ExternalSize=%lu "
                   "*ExternalBaseAddress=0x%08x *ExtenalAlignment=0x%08x "
                   "*HorizontalTileSize=%u *VerticalTileSize=%u",
                   gcmOPT_VALUE(InternalSize),
                   gcmOPT_VALUE(InternalBaseAddress),
                   gcmOPT_VALUE(InternalAlignment),
                   gcmOPT_VALUE(ExternalSize),
                   gcmOPT_VALUE(ExternalBaseAddress),
                   gcmOPT_VALUE(ExternalAlignment),
                   gcmOPT_VALUE(HorizontalTileSize),
                   gcmOPT_VALUE(VerticalTileSize));
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckHARDWARE_QueryChipIdentity
**
**  Query the identity of the hardware.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object.
**
**  OUTPUT:
**
**      gcsHAL_QUERY_CHIP_IDENTITY_PTR Identity
**          Pointer to the identity structure.
**
*/
gceSTATUS
gckHARDWARE_QueryChipIdentity(
    IN gckHARDWARE Hardware,
    OUT gcsHAL_QUERY_CHIP_IDENTITY_PTR Identity
    )
{
    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Identity != gcvNULL);

    *Identity = Hardware->identity;

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckHARDWARE_SplitMemory
**
**  Split a hardware specific memory address into a pool and offset.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object.
**
**      gctUINT32 Address
**          Address in hardware specific format.
**
**  OUTPUT:
**
**      gcePOOL * Pool
**          Pointer to a variable that will hold the pool type for the address.
**
**      gctUINT32 * Offset
**          Pointer to a variable that will hold the offset for the address.
*/
gceSTATUS
gckHARDWARE_SplitMemory(
    IN gckHARDWARE Hardware,
    IN gctUINT32 Address,
    OUT gcePOOL * Pool,
    OUT gctUINT32 * Offset
    )
{
    gcmkHEADER_ARG("Hardware=0x%x Addres=0x%08x", Hardware, Address);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Pool != gcvNULL);
    gcmkVERIFY_ARGUMENT(Offset != gcvNULL);

    if (Hardware->mmuVersion == 0)
    {
        /* Dispatch on memory type. */
        switch ((((((gctUINT32) (Address)) >> (0 ? 31:31)) & ((gctUINT32) ((((1 ? 31:31) - (0 ? 31:31) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1)))))) ))
        {
        case 0x0:
            /* System memory. */
            *Pool = gcvPOOL_SYSTEM;
            break;

        case 0x1:
            /* Virtual memory. */
            *Pool = gcvPOOL_VIRTUAL;
            break;

        default:
            /* Invalid memory type. */
            gcmkFOOTER_ARG("status=%d", gcvSTATUS_INVALID_ARGUMENT);
            return gcvSTATUS_INVALID_ARGUMENT;
        }

        /* Return offset of address. */
        *Offset = (((((gctUINT32) (Address)) >> (0 ? 30:0)) & ((gctUINT32) ((((1 ? 30:0) - (0 ? 30:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 30:0) - (0 ? 30:0) + 1)))))) );
    }
    else
    {
        *Pool = gcvPOOL_SYSTEM;
        *Offset = Address;
    }

    /* Success. */
    gcmkFOOTER_ARG("*Pool=%d *Offset=0x%08x", *Pool, *Offset);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckHARDWARE_Execute
**
**  Kickstart the hardware's command processor with an initialized command
**  buffer.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object.
**
**      gctUINT32 Address
**          Hardware address of command buffer.
**
**      gctSIZE_T Bytes
**          Number of bytes for the prefetch unit (until after the first LINK).
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_Execute(
    IN gckHARDWARE Hardware,
    IN gctUINT32 Address,
    IN gctSIZE_T Bytes
    )
{
    gceSTATUS status;
    gctUINT32 control;

    gcmkHEADER_ARG("Hardware=0x%x Address=0x%x Bytes=%lu",
                   Hardware, Address, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Enable all events. */
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00014, ~0U));

    /* Write address register. */
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00654, Address));

    /* Build control register. */
    control = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ?
 16:16))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ? 16:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) ((Bytes + 7) >> 3) & ((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

    /* Set big endian */
    if (Hardware->bigEndian)
    {
        control |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 21:20) - (0 ? 21:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:20) - (0 ? 21:20) + 1))))))) << (0 ?
 21:20))) | (((gctUINT32) (0x2 & ((gctUINT32) ((((1 ? 21:20) - (0 ? 21:20) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 21:20) - (0 ? 21:20) + 1))))))) << (0 ? 21:20)));
    }

    /* Make sure writing to command buffer and previous AHB register is done. */
    gcmkONERROR(gckOS_MemoryBarrier(Hardware->os, gcvNULL));

    /* Write control register. */
    switch (Hardware->secureMode)
    {
    case gcvSECURE_NONE:
        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00658, control));
        break;
    case gcvSECURE_IN_NORMAL:

#if defined(__KERNEL__)
        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00658, control));
#endif
        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x003A4, control));

        break;
#if gcdENABLE_TRUST_APPLICATION
    case gcvSECURE_IN_TA:
        /* Send message to TA. */
        gcmkONERROR(gckKERNEL_SecurityStartCommand(Hardware->kernel, Address, (gctUINT32)Bytes));
        break;
#endif
    default:
        break;
    }

    /* Increase execute count. */
    Hardware->executeCount++;

    /* Record last execute address. */
    Hardware->lastExecuteAddress = Address;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                  "Started command buffer @ 0x%08x",
                  Address);

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
**  gckHARDWARE_WaitLink
**
**  Append a WAIT/LINK command sequence at the specified location in the command
**  queue.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      gctPOINTER Logical
**          Pointer to the current location inside the command queue to append
**          WAIT/LINK command sequence at or gcvNULL just to query the size of the
**          WAIT/LINK command sequence.
**
**      gctUINT32 Address
**          GPU address of current location inside the command queue.
**
**      gctUINT32 Offset
**          Offset into command buffer required for alignment.
**
**      gctSIZE_T * Bytes
**          Pointer to the number of bytes available for the WAIT/LINK command
**          sequence.  If 'Logical' is gcvNULL, this argument will be ignored.
**
**  OUTPUT:
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          by the WAIT/LINK command sequence.  If 'Bytes' is gcvNULL, nothing will
**          be returned.
**
**      gctUINT32 * WaitOffset
**          Pointer to a variable that will receive the offset of the WAIT command
**          from the specified logcial pointer.
**          If 'WaitOffset' is gcvNULL nothing will be returned.
**
**      gctSIZE_T * WaitSize
**          Pointer to a variable that will receive the number of bytes used by
**          the WAIT command.  If 'LinkSize' is gcvNULL nothing will be returned.
*/
gceSTATUS
gckHARDWARE_WaitLink(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gctUINT32 Address,
    IN gctUINT32 Offset,
    IN OUT gctUINT32 * Bytes,
    OUT gctUINT32 * WaitOffset,
    OUT gctUINT32 * WaitSize
    )
{
    gceSTATUS status;
    gctUINT32_PTR logical;
    gctUINT32 bytes;
    gctBOOL useL2;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x Offset=0x%08x *Bytes=%lu",
                   Hardware, Logical, Offset, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT((Logical != gcvNULL) || (Bytes != gcvNULL));

    useL2 = gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_64K_L2_CACHE);

    /* Compute number of bytes required. */
    if (useL2)
    {
        bytes = gcmALIGN(Offset + 24, 8) - Offset;
    }
    else
    {
        bytes = gcmALIGN(Offset + 16, 8) - Offset;
    }

    /* Cast the input pointer. */
    logical = (gctUINT32_PTR) Logical;

    if (logical != gcvNULL)
    {
        /* Not enough space? */
        if (*Bytes < bytes)
        {
            /* Command queue too small. */
            gcmkONERROR(gcvSTATUS_BUFFER_TOO_SMALL);
        }

        gcmkASSERT(Address != ~0U);

        /* Store the WAIT/LINK address. */
        Hardware->lastWaitLink = Address;

        /* Append WAIT(count). */
        *logical++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (Hardware->waitCount) & ((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

        logical++;

        if (useL2)
        {
            /* LoadState(AQFlush, 1), flush. */
            *logical++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                       | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E03) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
                       | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

            *logical++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ?
 6:6))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6)));
        }

        /* Append LINK(2, address). */
        *logical++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x08 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (bytes >> 3) & ((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

        *logical++ = Address;

        gcmkTRACE_ZONE(
            gcvLEVEL_INFO, gcvZONE_HARDWARE,
            "0x%08x: WAIT %u", Address, Hardware->waitCount
            );

        gcmkTRACE_ZONE(
            gcvLEVEL_INFO, gcvZONE_HARDWARE,
            "0x%08x: LINK 0x%08x, #%lu",
            Address + 8, Address, bytes
            );

        if (WaitOffset != gcvNULL)
        {
            /* Return the offset pointer to WAIT command. */
            *WaitOffset = 0;
        }

        if (WaitSize != gcvNULL)
        {
            /* Return number of bytes used by the WAIT command. */
            if (useL2)
            {
                *WaitSize = 16;
            }
            else
            {
                *WaitSize = 8;
            }
        }
    }

    if (Bytes != gcvNULL)
    {
        /* Return number of bytes required by the WAIT/LINK command
        ** sequence. */
        *Bytes = bytes;
    }

    /* Success. */
    gcmkFOOTER_ARG("*Bytes=%lu *WaitOffset=0x%x *WaitSize=%lu",
                   gcmOPT_VALUE(Bytes), gcmOPT_VALUE(WaitOffset),
                   gcmOPT_VALUE(WaitSize));
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_End
**
**  Append an END command at the specified location in the command queue.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      gctPOINTER Logical
**          Pointer to the current location inside the command queue to append
**          END command at or gcvNULL just to query the size of the END command.
**
**      gctSIZE_T * Bytes
**          Pointer to the number of bytes available for the END command.  If
**          'Logical' is gcvNULL, this argument will be ignored.
**
**  OUTPUT:
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          for the END command.  If 'Bytes' is gcvNULL, nothing will be returned.
*/
gceSTATUS
gckHARDWARE_End(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN OUT gctUINT32 * Bytes
    )
{
    gctUINT32_PTR logical = (gctUINT32_PTR) Logical;
    gctUINT32 address;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x *Bytes=%lu",
                   Hardware, Logical, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
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
            Hardware->executeCount;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE, "0x%x: END", Logical);

        /* Make sure the CPU writes out the data to memory. */
        gcmkONERROR(
            gckOS_MemoryBarrier(Hardware->os, Logical));

#if USE_KERNEL_VIRTUAL_BUFFERS
        if (!Hardware->kernel->virtualCommandBuffer)
#endif
        {
            gcmkONERROR(gckHARDWARE_ConvertLogical(Hardware, logical, gcvFALSE, &address));
        }

        Hardware->lastEnd = address;
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
gckHARDWARE_ChipEnable(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gceCORE_3D_MASK ChipEnable,
    IN OUT gctSIZE_T * Bytes
    )
{
    gckOS os = Hardware->os;
    gctUINT32_PTR logical = (gctUINT32_PTR) Logical;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x ChipEnable=0x%x *Bytes=%lu",
                   Hardware, Logical, ChipEnable, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT((Logical == gcvNULL) || (Bytes != gcvNULL));

    if (Logical != gcvNULL)
    {
        if (*Bytes < 8)
        {
            /* Command queue too small. */
            gcmkONERROR(gcvSTATUS_BUFFER_TOO_SMALL);
        }

        /* Append CHIPENABLE. */
        gcmkWRITE_MEMORY(
            logical,
            ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x0D & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | ChipEnable
            );

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE, "0x%x: CHIPENABLE 0x%x", Logical, ChipEnable);
    }

    if (Bytes != gcvNULL)
    {
        /* Return number of bytes required by the CHIPENABLE command. */
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

/*******************************************************************************
**
**  gckHARDWARE_Nop
**
**  Append a NOP command at the specified location in the command queue.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      gctPOINTER Logical
**          Pointer to the current location inside the command queue to append
**          NOP command at or gcvNULL just to query the size of the NOP command.
**
**      gctSIZE_T * Bytes
**          Pointer to the number of bytes available for the NOP command.  If
**          'Logical' is gcvNULL, this argument will be ignored.
**
**  OUTPUT:
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          for the NOP command.  If 'Bytes' is gcvNULL, nothing will be returned.
*/
gceSTATUS
gckHARDWARE_Nop(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN OUT gctSIZE_T * Bytes
    )
{
    gctUINT32_PTR logical = (gctUINT32_PTR) Logical;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x *Bytes=%lu",
                   Hardware, Logical, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT((Logical == gcvNULL) || (Bytes != gcvNULL));

    if (Logical != gcvNULL)
    {
        if (*Bytes < 8)
        {
            /* Command queue too small. */
            gcmkONERROR(gcvSTATUS_BUFFER_TOO_SMALL);
        }

        /* Append NOP. */
        logical[0] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x03 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE, "0x%x: NOP", Logical);
    }

    if (Bytes != gcvNULL)
    {
        /* Return number of bytes required by the NOP command. */
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

/*******************************************************************************
**
**  gckHARDWARE_Event
**
**  Append an EVENT command at the specified location in the command queue.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      gctPOINTER Logical
**          Pointer to the current location inside the command queue to append
**          the EVENT command at or gcvNULL just to query the size of the EVENT
**          command.
**
**      gctUINT8 Event
**          Event ID to program.
**
**      gceKERNEL_WHERE FromWhere
**          Location of the pipe to send the event.
**
**      gctSIZE_T * Bytes
**          Pointer to the number of bytes available for the EVENT command.  If
**          'Logical' is gcvNULL, this argument will be ignored.
**
**  OUTPUT:
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          for the EVENT command.  If 'Bytes' is gcvNULL, nothing will be
**          returned.
*/
gceSTATUS
gckHARDWARE_Event(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gctUINT8 Event,
    IN gceKERNEL_WHERE FromWhere,
    IN OUT gctUINT32 * Bytes
    )
{
    gctUINT size;
    gctUINT32 destination = 0;
    gctUINT32_PTR logical = (gctUINT32_PTR) Logical;
    gceSTATUS status;
    gctBOOL blt;
    gctBOOL extraEventStates;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x Event=%u FromWhere=%d *Bytes=%lu",
                   Hardware, Logical, Event, FromWhere, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT((Logical == gcvNULL) || (Bytes != gcvNULL));
    gcmkVERIFY_ARGUMENT(Event < 32);

    if (gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_BLT_ENGINE))
    {
        /* Send all event from blt. */
        if (FromWhere == gcvKERNEL_PIXEL)
        {
            FromWhere = gcvKERNEL_BLT;
        }
    }

    blt = FromWhere == gcvKERNEL_BLT ? gcvTRUE : gcvFALSE;

    /* Determine the size of the command. */

    extraEventStates = Hardware->extraEventStates && (FromWhere == gcvKERNEL_PIXEL);

    size = extraEventStates
         ? gcmALIGN(8 + (1 + 5) * 4, 8) /* EVENT + 5 STATES */
         : 8;

    if (blt)
    {
        size += 16;
    }

    if (Logical != gcvNULL)
    {
        if (*Bytes < size)
        {
            /* Command queue too small. */
            gcmkONERROR(gcvSTATUS_BUFFER_TOO_SMALL);
        }

        switch (FromWhere)
        {
        case gcvKERNEL_COMMAND:
            /* From command processor. */
            destination = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ?
 5:5))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5)));
            break;

        case gcvKERNEL_PIXEL:
            /* From pixel engine. */
            destination = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ?
 6:6))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6)));
            break;

        case gcvKERNEL_BLT:
            destination = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ?
 7:7))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 7:7) - (0 ? 7:7) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ? 7:7)));
            break;

        default:
            gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }

        if (blt)
        {
            *logical++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x502E) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

            *logical++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));
        }

        /* Append EVENT(Event, destination). */
        *logical++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E01) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

        *logical++
            = ((((gctUINT32) (destination)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) ((gctUINT32) (Event) & ((gctUINT32) ((((1 ? 4:0) - (0 ?
 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0)));

        if (blt)
        {
            *logical++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x502E) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

            *logical++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));
        }


        /* Make sure the event ID gets written out before GPU can access it. */
        gcmkONERROR(
            gckOS_MemoryBarrier(Hardware->os, logical + 1));

#if gcmIS_DEBUG(gcdDEBUG_TRACE)
        {
            gctPHYS_ADDR_T phys;
            gckOS_GetPhysicalAddress(Hardware->os, Logical, &phys);
            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                           "0x%08x: EVENT %d", phys, Event);
        }
#endif

        /* Append the extra states. These are needed for the chips that do not
        ** support back-to-back events due to the async interface. The extra
        ** states add the necessary delay to ensure that event IDs do not
        ** collide. */
        if (extraEventStates)
        {
            *logical++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                       | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0100) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
                       | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (5) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));
            *logical++ = 0;
            *logical++ = 0;
            *logical++ = 0;
            *logical++ = 0;
            *logical++ = 0;
        }

#if gcdINTERRUPT_STATISTIC
        if (Event < gcmCOUNTOF(Hardware->kernel->eventObj->queues))
        {
            gckOS_AtomSetMask(Hardware->pendingEvent, 1 << Event);
        }
#endif
    }

    if (Bytes != gcvNULL)
    {
        /* Return number of bytes required by the EVENT command. */
        *Bytes = size;
    }

    /* Success. */
    gcmkFOOTER_ARG("*Bytes=%lu", gcmOPT_VALUE(Bytes));
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_PipeSelect
**
**  Append a PIPESELECT command at the specified location in the command queue.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      gctPOINTER Logical
**          Pointer to the current location inside the command queue to append
**          the PIPESELECT command at or gcvNULL just to query the size of the
**          PIPESELECT command.
**
**      gcePIPE_SELECT Pipe
**          Pipe value to select.
**
**      gctSIZE_T * Bytes
**          Pointer to the number of bytes available for the PIPESELECT command.
**          If 'Logical' is gcvNULL, this argument will be ignored.
**
**  OUTPUT:
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          for the PIPESELECT command.  If 'Bytes' is gcvNULL, nothing will be
**          returned.
*/
gceSTATUS
gckHARDWARE_PipeSelect(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gcePIPE_SELECT Pipe,
    IN OUT gctUINT32 * Bytes
    )
{
    gctUINT32_PTR logical = (gctUINT32_PTR) Logical;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x Pipe=%d *Bytes=%lu",
                   Hardware, Logical, Pipe, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT((Logical == gcvNULL) || (Bytes != gcvNULL));

    /* Append a PipeSelect. */
    if (Logical != gcvNULL)
    {
        gctUINT32 flush, stall;

        if (*Bytes < 32)
        {
            /* Command queue too small. */
            gcmkONERROR(gcvSTATUS_BUFFER_TOO_SMALL);
        }

        flush = (Pipe == gcvPIPE_2D)
              ? ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1)))
              | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))
              : ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ?
 3:3))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3)));

        stall = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
              | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* LoadState(AQFlush, 1), flush. */
        logical[0]
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E03) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

        logical[1]
            = flush;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "0x%x: FLUSH 0x%x", logical, flush);

        /* LoadState(AQSempahore, 1), stall. */
        logical[2]
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

        logical[3]
            = stall;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "0x%x: SEMAPHORE 0x%x", logical + 2, stall);

        /* Stall, stall. */
        logical[4] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));
        logical[5] = stall;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "0x%x: STALL 0x%x", logical + 4, stall);

        /* LoadState(AQPipeSelect, 1), pipe. */
        logical[6]
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E00) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

        logical[7] = (Pipe == gcvPIPE_2D)
            ? 0x1
            : 0x0;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "0x%x: PIPE %d", logical + 6, Pipe);
    }

    if (Bytes != gcvNULL)
    {
        /* Return number of bytes required by the PIPESELECT command. */
        *Bytes = 32;
    }

    /* Success. */
    gcmkFOOTER_ARG("*Bytes=%lu", gcmOPT_VALUE(Bytes));
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_Link
**
**  Append a LINK command at the specified location in the command queue.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      gctPOINTER Logical
**          Pointer to the current location inside the command queue to append
**          the LINK command at or gcvNULL just to query the size of the LINK
**          command.
**
**      gctUINT32 FetchAddress
**          Hardware address of destination of LINK.
**
**      gctSIZE_T FetchSize
**          Number of bytes in destination of LINK.
**
**      gctSIZE_T * Bytes
**          Pointer to the number of bytes available for the LINK command.  If
**          'Logical' is gcvNULL, this argument will be ignored.
**
**  OUTPUT:
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          for the LINK command.  If 'Bytes' is gcvNULL, nothing will be returned.
*/
gceSTATUS
gckHARDWARE_Link(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gctUINT32 FetchAddress,
    IN gctUINT32 FetchSize,
    IN OUT gctUINT32 * Bytes,
    OUT gctUINT32 * Low,
    OUT gctUINT32 * High
    )
{
    gceSTATUS status;
    gctSIZE_T bytes;
    gctUINT32 link;
    gctUINT32_PTR logical = (gctUINT32_PTR) Logical;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x FetchAddress=0x%x FetchSize=%lu "
                   "*Bytes=%lu",
                   Hardware, Logical, FetchAddress, FetchSize,
                   gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT((Logical == gcvNULL) || (Bytes != gcvNULL));

    if (Logical != gcvNULL)
    {
        if (*Bytes < 8)
        {
            /* Command queue too small. */
            gcmkONERROR(gcvSTATUS_BUFFER_TOO_SMALL);
        }

        gcmkONERROR(
            gckOS_WriteMemory(Hardware->os, logical + 1, FetchAddress));

        if (High)
        {
            *High = FetchAddress;
        }

        /* Make sure the address got written before the LINK command. */
        gcmkONERROR(
            gckOS_MemoryBarrier(Hardware->os, logical + 1));

        /* Compute number of 64-byte aligned bytes to fetch. */
        bytes = gcmALIGN(FetchAddress + FetchSize, 64) - FetchAddress;

        /* Append LINK(bytes / 8), FetchAddress. */
        link = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x08 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
             | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (bytes >> 3) & ((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

        gcmkONERROR(
            gckOS_WriteMemory(Hardware->os, logical, link));

        if (Low)
        {
            *Low = link;
        }

        /* Memory barrier. */
        gcmkONERROR(
            gckOS_MemoryBarrier(Hardware->os, logical));
    }

    if (Bytes != gcvNULL)
    {
        /* Return number of bytes required by the LINK command. */
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
gckHARDWARE_FenceRender(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gctUINT32 FenceAddress,
    IN gctUINT64 FenceData,
    IN OUT gctUINT32 * Bytes
    )
{
    gckOS os = Hardware->os;
    gctUINT32_PTR logical = (gctUINT32_PTR)Logical;

    gctUINT32 dataLow = (gctUINT32)FenceData;
    gctUINT32 dataHigh = (gctUINT32)(FenceData >> 32);

    if (logical)
    {
        gcmkWRITE_MEMORY(
            logical,
            ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E1A) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
            );

        gcmkWRITE_MEMORY(
            logical,
            FenceAddress
            );

        gcmkWRITE_MEMORY(
            logical,
            ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E26) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
            );

        gcmkWRITE_MEMORY(
            logical,
            dataHigh
            );

        gcmkWRITE_MEMORY(
            logical,
            ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E1B) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
            );

        *logical++
            = dataLow;
    }
    else
    {
        *Bytes = gcdRENDER_FENCE_LENGTH;
    }

    return gcvSTATUS_OK;
}

gceSTATUS
gckHARDWARE_FenceBlt(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gctUINT32 FenceAddress,
    IN gctUINT64 FenceData,
    IN OUT gctUINT32 * Bytes
    )
{
    gckOS os = Hardware->os;
    gctUINT32_PTR logical = (gctUINT32_PTR)Logical;

    gctUINT32 dataLow = (gctUINT32)FenceData;
    gctUINT32 dataHigh = (gctUINT32)(FenceData >> 32);

    if (logical)
    {
        gcmkWRITE_MEMORY(
            logical,
            ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x502E) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
            );

        gcmkWRITE_MEMORY(
            logical,
            ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))
            );

        gcmkWRITE_MEMORY(
            logical,
            ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x5029) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
            );

        gcmkWRITE_MEMORY(
            logical,
            FenceAddress
            );

        gcmkWRITE_MEMORY(
            logical,
            ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x502D) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
            );

        gcmkWRITE_MEMORY(
            logical,
            dataHigh
            );

        gcmkWRITE_MEMORY(
            logical,
            ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x502A) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
            );

        gcmkWRITE_MEMORY(
            logical,
            dataLow
            );

        gcmkWRITE_MEMORY(
            logical,
            ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x502E) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
            );

        gcmkWRITE_MEMORY(
            logical,
            ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))
            );
    }
    else
    {
        *Bytes = gcdBLT_FENCE_LENGTH;
    }

    return gcvSTATUS_OK;
}

gceSTATUS
gckHARDWARE_Fence(
    IN gckHARDWARE Hardware,
    IN gceENGINE  Engine,
    IN gctPOINTER Logical,
    IN gctUINT32 FenceAddress,
    IN gctUINT64 FenceData,
    IN OUT gctUINT32 * Bytes
    )
{
    if (Engine == gcvENGINE_RENDER)
    {
        return gckHARDWARE_FenceRender(Hardware, Logical, FenceAddress, FenceData, Bytes);
    }
    else
    {
         return gckHARDWARE_FenceBlt(Hardware, Logical, FenceAddress, FenceData, Bytes);
    }
}

/*******************************************************************************
**
**  gckHARDWARE_UpdateQueueTail
**
**  Update the tail of the command queue.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      gctPOINTER Logical
**          Logical address of the start of the command queue.
**
**      gctUINT32 Offset
**          Offset into the command queue of the tail (last command).
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_UpdateQueueTail(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gctUINT32 Offset
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x Offset=0x%08x",
                   Hardware, Logical, Offset);

    /* Verify the hardware. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Force a barrier. */
    gcmkONERROR(
        gckOS_MemoryBarrier(Hardware->os, Logical));

    /* Notify gckKERNEL object of change. */
    gcmkONERROR(
        gckKERNEL_Notify(Hardware->kernel,
                         gcvNOTIFY_COMMAND_QUEUE,
                         gcvFALSE));

    if (status == gcvSTATUS_CHIP_NOT_READY)
    {
        gcmkONERROR(gcvSTATUS_DEVICE);
    }

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
**  gckHARDWARE_ConvertLogical
**
**  Convert a logical system address into a hardware specific address.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      gctPOINTER Logical
**          Logical address to convert.
**
**      gctBOOL InUserSpace
**          gcvTRUE if the memory in user space.
**
**      gctUINT32* Address
**          Return hardware specific address.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_ConvertLogical(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gctBOOL InUserSpace,
    OUT gctUINT32 * Address
    )
{
    gctUINT32 address;
    gceSTATUS status;
    gctPHYS_ADDR_T physical;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x InUserSpace=%d",
                   Hardware, Logical, InUserSpace);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    /* Convert logical address into a physical address. */
    if (InUserSpace)
    {
        gcmkONERROR(gckOS_UserLogicalToPhysical(Hardware->os, Logical, &physical));
    }
    else
    {
        gcmkONERROR(gckOS_GetPhysicalAddress(Hardware->os, Logical, &physical));
    }

    gcmkSAFECASTPHYSADDRT(address, physical);

    /* For old MMU, get GPU address according to baseAddress. */
    if (Hardware->mmuVersion == 0)
    {
        /* Subtract base address to get a GPU address. */
        gcmkASSERT(address >= Hardware->baseAddress);
        address -= Hardware->baseAddress;
    }

    /* Return hardware specific address. */
    *Address = (Hardware->mmuVersion == 0)
             ? ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:31) - (0 ? 31:31) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1))))))) << (0 ?
 31:31))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 31:31) - (0 ? 31:31) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1))))))) << (0 ? 31:31)))
               | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 30:0) - (0 ? 30:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 30:0) - (0 ? 30:0) + 1))))))) << (0 ?
 30:0))) | (((gctUINT32) ((gctUINT32) (address) & ((gctUINT32) ((((1 ? 30:0) - (0 ?
 30:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 30:0) - (0 ? 30:0) + 1))))))) << (0 ?
 30:0)))
             : address;

    /* Success. */
    gcmkFOOTER_ARG("*Address=0x%08x", *Address);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_Interrupt
**
**  Process an interrupt.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      gctBOOL InterruptValid
**          If gcvTRUE, this function will read the interrupt acknowledge
**          register, stores the data, and return whether or not the interrupt
**          is ours or not.  If gcvFALSE, this functions will read the interrupt
**          acknowledge register and combine it with any stored value to handle
**          the event notifications.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_Interrupt(
    IN gckHARDWARE Hardware,
    IN gctBOOL InterruptValid
    )
{
    gckEVENT eventObj;
    gctUINT32 data = 0;
    gctUINT32 dataEx;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x InterruptValid=%d", Hardware, InterruptValid);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Extract gckEVENT object. */
    eventObj = Hardware->kernel->eventObj;
    gcmkVERIFY_OBJECT(eventObj, gcvOBJ_EVENT);

    if (InterruptValid)
    {
        /* Read AQIntrAcknowledge register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 0x00010,
                                 &data));

        if (data == 0)
        {
            /* Not our interrupt. */
            status = gcvSTATUS_NOT_OUR_INTERRUPT;
        }
        else
        {

#if gcdINTERRUPT_STATISTIC
            gckOS_AtomClearMask(Hardware->pendingEvent, data);
#endif

            /* Inform gckEVENT of the interrupt. */
            status = gckEVENT_Interrupt(eventObj,
                                        data);
        }

        if (gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_BLT_ENGINE))
        {
            /* Read BLT interrupt. */
            gcmkONERROR(gckOS_ReadRegisterEx(
                Hardware->os,
                Hardware->core,
                0x000D4,
                &dataEx
                ));

            if (dataEx & 0x80000000)
            {
                /* Descriptor fetched, update counter. */
                gckFE_UpdateAvaiable(Hardware, &Hardware->kernel->asyncCommand->fe);

                dataEx &= ~0x80000000;
            }

            if (dataEx)
            {
                status = gckEVENT_Interrupt(Hardware->kernel->asyncEvent,
                                            dataEx
                                            );
            }
        }
    }
    else
    {
            /* Handle events. */
            status = gckEVENT_Notify(eventObj, 0);

            if (gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_BLT_ENGINE))
            {
                /* TODO : Move it to a indpendent worker thread. */
                status = gckEVENT_Notify(Hardware->kernel->asyncEvent, 0);
            }
    }

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_QueryCommandBuffer
**
**  Query the command buffer alignment and number of reserved bytes.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**  OUTPUT:
**
**      gctSIZE_T * Alignment
**          Pointer to a variable receiving the alignment for each command.
**
**      gctSIZE_T * ReservedHead
**          Pointer to a variable receiving the number of reserved bytes at the
**          head of each command buffer.
**
**      gctSIZE_T * ReservedTail
**          Pointer to a variable receiving the number of bytes reserved at the
**          tail of each command buffer.
*/
gceSTATUS
gckHARDWARE_QueryCommandBuffer(
    IN gckHARDWARE Hardware,
    IN gceENGINE Engine,
    OUT gctUINT32 * Alignment,
    OUT gctUINT32 * ReservedHead,
    OUT gctUINT32 * ReservedTail
    )
{
    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    if (Alignment != gcvNULL)
    {
        /* Align every 8 bytes. */
        *Alignment = 8;
    }

    if (ReservedHead != gcvNULL)
    {
        /* Reserve space for SelectPipe(). */
        *ReservedHead = 32;
    }

    if (ReservedTail != gcvNULL)
    {
        if (Engine == gcvENGINE_RENDER)
        {
            gcmkFOOTER_NO();
            return gcvSTATUS_NOT_SUPPORTED;
        }
        else
        {
            *ReservedTail = gcdBLT_FENCE_LENGTH;
        }
    }

    /* Success. */
    gcmkFOOTER_ARG("*Alignment=%lu *ReservedHead=%lu *ReservedTail=%lu",
                   gcmOPT_VALUE(Alignment), gcmOPT_VALUE(ReservedHead),
                   gcmOPT_VALUE(ReservedTail));
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckHARDWARE_QuerySystemMemory
**
**  Query the command buffer alignment and number of reserved bytes.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**  OUTPUT:
**
**      gctSIZE_T * SystemSize
**          Pointer to a variable that receives the maximum size of the system
**          memory.
**
**      gctUINT32 * SystemBaseAddress
**          Poinetr to a variable that receives the base address for system
**          memory.
*/
gceSTATUS
gckHARDWARE_QuerySystemMemory(
    IN gckHARDWARE Hardware,
    OUT gctSIZE_T * SystemSize,
    OUT gctUINT32 * SystemBaseAddress
    )
{
    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    if (SystemSize != gcvNULL)
    {
        /* Maximum system memory can be 2GB. */
        *SystemSize = 1U << 31;
    }

    if (SystemBaseAddress != gcvNULL)
    {
        /* Set system memory base address. */
        *SystemBaseAddress = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:31) - (0 ? 31:31) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1))))))) << (0 ?
 31:31))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 31:31) - (0 ? 31:31) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1))))))) << (0 ? 31:31)));
    }

    /* Success. */
    gcmkFOOTER_ARG("*SystemSize=%lu *SystemBaseAddress=%lu",
                   gcmOPT_VALUE(SystemSize), gcmOPT_VALUE(SystemBaseAddress));
    return gcvSTATUS_OK;
}

#if gcdENABLE_3D
/*******************************************************************************
**
**  gckHARDWARE_QueryShaderCaps
**
**  Query the shader capabilities.
**
**  INPUT:
**
**      Nothing.
**
**  OUTPUT:
**
**      gctUINT * VertexUniforms
**          Pointer to a variable receiving the number of uniforms in the vertex
**          shader.
**
**      gctUINT * FragmentUniforms
**          Pointer to a variable receiving the number of uniforms in the
**          fragment shader.
**
**      gctBOOL * UnifiedUnforms
**          Pointer to a variable receiving whether the uniformas are unified.
*/
gceSTATUS
gckHARDWARE_QueryShaderCaps(
    IN gckHARDWARE Hardware,
    OUT gctUINT * VertexUniforms,
    OUT gctUINT * FragmentUniforms,
    OUT gctBOOL * UnifiedUnforms
    )
{
    gctBOOL unifiedConst;
    gctUINT32 vsConstMax;
    gctUINT32 psConstMax;
    gctUINT32 vsConstBase;
    gctUINT32 psConstBase;
    gctUINT32 ConstMax;
    gctBOOL   halti5;

    gcmkHEADER_ARG("Hardware=0x%x VertexUniforms=0x%x "
                   "FragmentUniforms=0x%x UnifiedUnforms=0x%x",
                   Hardware, VertexUniforms,
                   FragmentUniforms, UnifiedUnforms);

    halti5 = gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_HALTI5);

    {if (Hardware->identity.numConstants > 256){    unifiedConst = gcvTRUE;
if (halti5){    vsConstBase  = 0xD000;
    psConstBase  = 0xD800;
}else{    vsConstBase  = 0xC000;
    psConstBase  = 0xC000;
}if ((Hardware->identity.chipModel == gcv880) && ((Hardware->identity.chipRevision & 0xfff0) == 0x5120)){    vsConstMax   = 512;
    psConstMax   = 64;
    ConstMax     = 576;
}else{    vsConstMax   = gcmMIN(512, Hardware->identity.numConstants - 64);
    psConstMax   = gcmMIN(512, Hardware->identity.numConstants - 64);
    ConstMax     = Hardware->identity.numConstants;
}}else if (Hardware->identity.numConstants == 256){    if (Hardware->identity.chipModel == gcv2000 && (Hardware->identity.chipRevision == 0x5118 || Hardware->identity.chipRevision == 0x5140))    {        unifiedConst = gcvFALSE;
        vsConstBase  = 0x1400;
        psConstBase  = 0x1C00;
        vsConstMax   = 256;
        psConstMax   = 64;
        ConstMax     = 320;
    }    else    {        unifiedConst = gcvFALSE;
        vsConstBase  = 0x1400;
        psConstBase  = 0x1C00;
        vsConstMax   = 256;
        psConstMax   = 256;
        ConstMax     = 512;
    }}else{    unifiedConst = gcvFALSE;
    vsConstBase  = 0x1400;
    psConstBase  = 0x1C00;
    vsConstMax   = 168;
    psConstMax   = 64;
    ConstMax     = 232;
}};


    if (VertexUniforms != gcvNULL)
    {
        /* Return the vs shader const count. */
        *VertexUniforms = vsConstMax;
    }

    if (FragmentUniforms != gcvNULL)
    {
        /* Return the ps shader const count. */
        *FragmentUniforms = psConstMax;
    }

    if (UnifiedUnforms != gcvNULL)
    {
        /* Return whether the uniformas are unified. */
        *UnifiedUnforms = unifiedConst;
    }

    psConstBase = psConstBase;
    vsConstBase = vsConstBase;
    ConstMax = ConstMax;

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}
#endif

/*******************************************************************************
**
**  gckHARDWARE_SetMMU
**
**  Set the page table base address.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**      gctPOINTER Logical
**          Logical address of the page table.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_SetMMU(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical
    )
{
    gceSTATUS status;
    gctUINT32 address = 0;
    gctUINT32 idle;
    gctUINT32 timer = 0, delay = 1;
    gctPHYS_ADDR_T physical;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x", Hardware, Logical);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    if (Hardware->mmuVersion == 0)
    {
        gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

        /* Convert the logical address into physical address. */
        gcmkONERROR(gckOS_GetPhysicalAddress(Hardware->os, Logical, &physical));

        gcmkSAFECASTPHYSADDRT(address, physical);

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "Setting page table to 0x%08X",
                       address);

        /* Write the AQMemoryFePageTable register. */
        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  0x00400,
                                  address));

        /* Write the AQMemoryRaPageTable register. */
        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  0x00410,
                                  address));

        /* Write the AQMemoryTxPageTable register. */
        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  0x00404,
                                  address));


        /* Write the AQMemoryPePageTable register. */
        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  0x00408,
                                  address));

        /* Write the AQMemoryPezPageTable register. */
        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  0x0040C,
                                  address));
    }
    else if (Hardware->enableMMU == gcvTRUE && Hardware->secureMode != gcvSECURE_IN_TA)
    {
        /* Prepared command sequence contains an END,
        ** so update lastEnd and store executeCount to END command.
        */
        gcsHARDWARE_FUNCTION *function = &Hardware->functions[gcvHARDWARE_FUNCTION_MMU];
        gctUINT32_PTR endLogical = (gctUINT32_PTR)function->endLogical;

        Hardware->lastEnd = function->endAddress;

        *(endLogical + 1) = Hardware->executeCount + 1;

        if (Hardware->secureMode == gcvSECURE_IN_NORMAL)
        {
            /* Set up base address of page table array. */
            gcmkONERROR(gckOS_WriteRegisterEx(
                Hardware->os,
                Hardware->core,
                0x0038C,
                (gctUINT32)(Hardware->pagetableArray.address & 0xFFFFFFFF)
                ));

            gcmkONERROR(gckOS_WriteRegisterEx(
                Hardware->os,
                Hardware->core,
                0x00390,
                (gctUINT32)((Hardware->pagetableArray.address >> 32) & 0xFFFFFFFF)
                ));

            gcmkONERROR(gckOS_WriteRegisterEx(
                Hardware->os,
                Hardware->core,
                0x00394,
                1
                ));
        }

        /* Execute prepared command sequence. */
        gcmkONERROR(gckHARDWARE_Execute(
            Hardware,
            function->address,
            function->bytes
            ));

#if gcdLINK_QUEUE_SIZE
        {
            gcuQUEUEDATA data;

            gcmkVERIFY_OK(gckOS_GetProcessID(&data.linkData.pid));

            data.linkData.start    = function->address;
            data.linkData.end      = function->address + function->bytes;
            data.linkData.linkLow  = 0;
            data.linkData.linkHigh = 0;

            gckQUEUE_Enqueue(&Hardware->linkQueue, &data);
        }
#endif

        /* Wait until MMU configure finishes. */
        do
        {
            gckOS_Delay(Hardware->os, delay);

            gcmkONERROR(gckOS_ReadRegisterEx(
                Hardware->os,
                Hardware->core,
                0x00004,
                &idle));

            timer += delay;
            delay *= 2;

#if gcdGPU_TIMEOUT
            if (timer >= Hardware->kernel->timeOut)
            {
                gckHARDWARE_DumpGPUState(Hardware);
                gckCOMMAND_DumpExecutingBuffer(Hardware->kernel->command);

                /* Even if hardware is not reset correctly, let software
                ** continue to avoid software stuck. Software will timeout again
                ** and try to recover GPU in next timeout.
                */
                gcmkONERROR(gcvSTATUS_DEVICE);
            }
#endif
        }
        while (!(((((gctUINT32) (idle)) >> (0 ? 0:0)) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1)))))) ));

        /* Enable MMU. */
        if (Hardware->secureMode == gcvSECURE_IN_NORMAL)
        {
            gcmkONERROR(gckOS_WriteRegisterEx(
                Hardware->os,
                Hardware->core,
                0x00388,
                ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))
                ));
        }
        else
        {
            gcmkONERROR(gckOS_WriteRegisterEx(
                Hardware->os,
                Hardware->core,
                0x0018C,
                ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) ((gctUINT32) (gcvTRUE) & ((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0)))
                ));
        }
    }

    /* Return the status. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_FlushMMU
**
**  Flush the page table.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_FlushMMU(
    IN gckHARDWARE Hardware
    )
{
    gceSTATUS status;
    gckCOMMAND command;
    gctUINT32_PTR buffer;
    gctUINT32 bufferSize;
    gctPOINTER pointer = gcvNULL;
    gctUINT32 flushSize;
    gctUINT32 count, offset;
    gctPHYS_ADDR_T physical;
    gctUINT32 address;
    gctBOOL bltEngine;
    gctUINT32 semaphore, stall;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Verify the gckCOMMAND object pointer. */
    command = Hardware->kernel->command;

    /* Flush the memory controller. */
    if (Hardware->mmuVersion == 0)
    {
        gcmkONERROR(gckCOMMAND_Reserve(
            command, 8, &pointer, &bufferSize
            ));

        buffer = (gctUINT32_PTR) pointer;

        buffer[0]
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E04) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

        buffer[1]
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ?
 2:2))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ?
 3:3))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ?
 4:4))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4)));

        gcmkONERROR(gckCOMMAND_Execute(command, 8));
    }
    else
    {
        flushSize = 10 * 4;
        offset = 2;

        bltEngine = gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_BLT_ENGINE);

        if (bltEngine)
        {
            flushSize +=  4 * 4;
        }

        gcmkONERROR(gckCOMMAND_Reserve(
            command, flushSize, &pointer, &bufferSize
            ));

        buffer = (gctUINT32_PTR) pointer;

        count = ((gctUINT)bufferSize - flushSize + 7) >> 3;

        gcmkONERROR(gckOS_GetPhysicalAddress(command->os, buffer, &physical));

        gcmkSAFECASTPHYSADDRT(address, physical);

        /* LINK to next slot to flush FE FIFO. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x08 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) ((bltEngine ? 6 : 4)) & ((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

        *buffer++
            = address + offset * gcmSIZEOF(gctUINT32);

        /* Flush MMU cache. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0061) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

        *buffer++
            = (((((gctUINT32) (~0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ?
 4:4))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4))) &  ((((gctUINT32) (~0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ?
 7:7))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 7:7) - (0 ? 7:7) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ? 7:7))));

        if (bltEngine)
        {
            /* Blt lock. */
            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x502E) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));
        }

        /* Arm the PE-FE Semaphore. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

        semaphore = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)));

        if (Hardware->stallFEPrefetch)
        {
            semaphore |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 29:28) - (0 ? 29:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 29:28) - (0 ? 29:28) + 1))))))) << (0 ?
 29:28))) | (((gctUINT32) (0x3 & ((gctUINT32) ((((1 ? 29:28) - (0 ? 29:28) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 29:28) - (0 ? 29:28) + 1))))))) << (0 ? 29:28)));
        }

        if (bltEngine)
        {
            semaphore |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x10 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));
        }
        else
        {
            semaphore |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));
        }

        *buffer++
            = semaphore;

        /* STALL FE until PE is done flushing. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

        stall = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)));

        if (Hardware->stallFEPrefetch)
        {
           stall |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 29:28) - (0 ? 29:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 29:28) - (0 ? 29:28) + 1))))))) << (0 ?
 29:28))) | (((gctUINT32) (0x3 & ((gctUINT32) ((((1 ? 29:28) - (0 ? 29:28) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 29:28) - (0 ? 29:28) + 1))))))) << (0 ? 29:28)));
        }

        if (bltEngine)
        {
            stall |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x10 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));
        }
        else
        {
            stall |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));
        }

        *buffer++
            = stall;

        if (bltEngine)
        {
            /* Blt unlock. */
            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x502E) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));
        }

        /* LINK to next slot to flush FE FIFO. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x08 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (count) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

        *buffer++
            = address + flushSize;

        gcmkONERROR(gckCOMMAND_Execute(command, flushSize));
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckHARDWARE_SetMMUStates(
    IN gckHARDWARE Hardware,
    IN gctPOINTER MtlbAddress,
    IN gceMMU_MODE Mode,
    IN gctPOINTER SafeAddress,
    IN gctPOINTER Logical,
    IN OUT gctUINT32 * Bytes
    )
{
    gceSTATUS status;
    gctUINT32 config, address;
    gctUINT32 extMtlb, extSafeAddrss, configEx = 0;
    gctPHYS_ADDR_T physical;
    gctUINT32_PTR buffer;
    gctBOOL ace;
    gctUINT32 reserveBytes = 16 + 4 * 4;
    gcsMMU_TABLE_ARRAY_ENTRY * entry;

    gctBOOL config2D;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Hardware->mmuVersion != 0);

    entry = (gcsMMU_TABLE_ARRAY_ENTRY *) Hardware->pagetableArray.logical;

    ace = gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_ACE);

    if (ace)
    {
        reserveBytes += 8;
    }

    config2D =  gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_PIPE_3D)
             && gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_PIPE_2D);

    if (config2D)
    {
        reserveBytes +=
            /* Pipe Select. */
            4 * 4
            /* Configure MMU States. */
          + 4 * 4
            /* Semaphore stall */
          + 4 * 8;

        if (ace)
        {
            reserveBytes += 8;
        }
    }

    /* Convert logical address into physical address. */
    gcmkONERROR(
        gckOS_GetPhysicalAddress(Hardware->os, MtlbAddress, &physical));

    config  = (gctUINT32)(physical & 0xFFFFFFFF);
    extMtlb = (gctUINT32)(physical >> 32);

    gcmkONERROR(
        gckOS_GetPhysicalAddress(Hardware->os, SafeAddress, &physical));

    address = (gctUINT32)(physical & 0xFFFFFFFF);
    extSafeAddrss = (gctUINT32)(physical >> 32);

    if (address & 0x3F)
    {
        gcmkONERROR(gcvSTATUS_NOT_ALIGNED);
    }

    if (ace)
    {
        configEx = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (extSafeAddrss) & ((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0)))
                 | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (extMtlb) & ((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16)));
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

        if (Hardware->secureMode == gcvSECURE_IN_NORMAL)
        {
            /* Setup page table array entry. */
            entry->low = config;
            entry->high = (gctUINT32)(physical >> 32);

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
        else
        {
            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0061) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

            *buffer++ = config;
        }

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0060) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

        *buffer++ = address;

        if (ace)
        {
            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0068) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

            *buffer++
                = configEx;
        }

        do{*buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))); *buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))); *buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))); *buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));} while(0);
;


        if (config2D)
        {
            /* LoadState(AQPipeSelect, 1), pipe. */
            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E00) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

            *buffer++ = 0x1;

            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0061) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

            *buffer++ = config;

            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0060) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

            *buffer++ = address;

            if (ace)
            {
                *buffer++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0068) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

                *buffer++
                    = configEx;
            }

            do{*buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))); *buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))); *buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))); *buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));} while(0);
;


            /* LoadState(AQPipeSelect, 1), pipe. */
            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E00) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

            *buffer++ = 0x0;

            do{*buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))); *buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))); *buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))); *buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));} while(0);
;

        }

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

#if gcdPROCESS_ADDRESS_SPACE
/*******************************************************************************
**
**  gckHARDWARE_ConfigMMU
**
**  Append a MMU Configuration command sequence at the specified location in the command
**  queue. That command sequence consists of mmu configuration, LINK and WAIT/LINK.
**  LINK is fetched and paresed with new mmu configuration.
**
**  If MMU Configuration is not changed between commit, change last WAIT/LINK to
**  link to ENTRY.
**
**  -+-----------+-----------+-----------------------------------------
**   | WAIT/LINK | WAIT/LINK |
**  -+-----------+-----------+-----------------------------------------
**         |          /|\
**        \|/          |
**    +--------------------+
**    | ENTRY | ... | LINK |
**    +--------------------+
**
**  If MMU Configuration is changed between commit, change last WAIT/LINK to
**  link to MMU CONFIGURATION command sequence, and there are an EVNET and
**  an END at the end of this command sequence, when interrupt handler
**  receives this event, it will start FE at ENTRY to continue the command
**  buffer execution.
**
**  -+-----------+-------------------+---------+---------+-----------+--
**   | WAIT/LINK | MMU CONFIGURATION |  EVENT  |  END    | WAIT/LINK |
**  -+-----------+-------------------+---------+---------+-----------+--
**        |            /|\                                   /|\
**        +-------------+                                     |
**                                          +--------------------+
**                                          | ENTRY | ... | LINK |
**                                          +--------------------+
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      gctPOINTER Logical
**          Pointer to the current location inside the command queue to append
**          command sequence at or gcvNULL just to query the size of the
**          command sequence.
**
**      gctPOINTER MtlbLogical
**          Pointer to the current Master TLB.
**
**      gctUINT32 Offset
**          Offset into command buffer required for alignment.
**
**      gctSIZE_T * Bytes
**          Pointer to the number of bytes available for the command
**          sequence.  If 'Logical' is gcvNULL, this argument will be ignored.
**
**  OUTPUT:
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          by the command sequence.  If 'Bytes' is gcvNULL, nothing will
**          be returned.
**
**      gctUINT32 * WaitLinkOffset
**          Pointer to a variable that will receive the offset of the WAIT/LINK command
**          from the specified logcial pointer.
**          If 'WaitLinkOffset' is gcvNULL nothing will be returned.
**
**      gctSIZE_T * WaitLinkBytes
**          Pointer to a variable that will receive the number of bytes used by
**          the WAIT command.
**          If 'WaitLinkBytes' is gcvNULL nothing will be returned.
*/
gceSTATUS
gckHARDWARE_ConfigMMU(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gctPOINTER MtlbLogical,
    IN gctUINT32 Offset,
    IN OUT gctSIZE_T * Bytes,
    OUT gctSIZE_T * WaitLinkOffset,
    OUT gctSIZE_T * WaitLinkBytes
    )
{
    gceSTATUS status;
    gctSIZE_T bytes, bytesAligned;
    gctUINT32 config;
    gctUINT32_PTR buffer = (gctUINT32_PTR) Logical;
    gctPHYS_ADDR_T physical;
    gctUINT32 address;
    gctUINT32 event;
    gctSIZE_T stCmds; /* semaphore stall cmd size */;

    gcmkHEADER_ARG("Hardware=0x%08X Logical=0x%08x MtlbLogical=0x%08X",
                   Hardware, Logical, MtlbLogical);

    stCmds = gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_TEX_CACHE_FLUSH_FIX) ? 0 : 4;

    bytes
        /* Semaphore stall states. */
        = stCmds * 4
        /* Flush cache states. */
        + 20 * 4
        /* MMU configuration states. */
        + 6 * 4
        /* EVENT. */
        + 2 * 4
        /* END. */
        + 2 * 4
        /* WAIT/LINK. */
        + 4 * 4;

    /* Compute number of bytes required. */
    bytesAligned = gcmALIGN(Offset + bytes, 8) - Offset;

    if (buffer != gcvNULL)
    {
        if (MtlbLogical == gcvNULL)
        {
            gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }

        /* Get physical address of this command buffer segment. */
        gcmkONERROR(gckOS_GetPhysicalAddress(Hardware->os, buffer, &physical));

        gcmkSAFECASTPHYSADDRT(address, physical);

        /* Get physical address of Master TLB. */
        gcmkONERROR(gckOS_GetPhysicalAddress(Hardware->os, MtlbLogical, &physical));

        gcmkSAFECASTPHYSADDRT(config, physical);

        config |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ?
 4:4))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4)));

        if (stCmds)
        {
            /* Arm the PE-FE Semaphore. */
            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

            /* STALL FE until PE is done flushing. */
            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));
        }

        /* Flush cache. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E03) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ?
 3:3))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ?
 2:2))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ?
 5:5))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 AQ_FLUSH_VSHL1_CACHE) - (0 ? AQ_FLUSH_VSHL1_CACHE) + 1) == 32) ? ~0 : (~(~0 << ((1 ?
 AQ_FLUSH_VSHL1_CACHE) - (0 ? AQ_FLUSH_VSHL1_CACHE) + 1))))))) << (0 ? AQ_FLUSH_VSHL1_CACHE))) | (((gctUINT32) (AQ_FLUSH_VSHL1_CACHE_ENABLE & ((gctUINT32) ((((1 ?
 AQ_FLUSH_VSHL1_CACHE) - (0 ? AQ_FLUSH_VSHL1_CACHE) + 1) == 32) ? ~0 : (~(~0 << ((1 ?
 AQ_FLUSH_VSHL1_CACHE) - (0 ? AQ_FLUSH_VSHL1_CACHE) + 1))))))) << (0 ? AQ_FLUSH_VSHL1_CACHE)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 AQ_FLUSH_PSHL1_CACHE) - (0 ? AQ_FLUSH_PSHL1_CACHE) + 1) == 32) ? ~0 : (~(~0 << ((1 ?
 AQ_FLUSH_PSHL1_CACHE) - (0 ? AQ_FLUSH_PSHL1_CACHE) + 1))))))) << (0 ? AQ_FLUSH_PSHL1_CACHE))) | (((gctUINT32) (AQ_FLUSH_PSHL1_CACHE_ENABLE & ((gctUINT32) ((((1 ?
 AQ_FLUSH_PSHL1_CACHE) - (0 ? AQ_FLUSH_PSHL1_CACHE) + 1) == 32) ? ~0 : (~(~0 << ((1 ?
 AQ_FLUSH_PSHL1_CACHE) - (0 ? AQ_FLUSH_PSHL1_CACHE) + 1))))))) << (0 ? AQ_FLUSH_PSHL1_CACHE)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ?
 6:6))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6)));

        /* Flush VTS in separate command */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E03) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ?
 4:4))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4)));

        /* Flush tile status cache. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0594) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));

        /* Arm the PE-FE Semaphore. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* STALL FE until PE is done flushing. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* LINK to next slot to flush FE FIFO. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x08 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (4) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

        *buffer++
            = address + (stCmds + 12) * gcmSIZEOF(gctUINT32);

        /* Configure MMU. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0061) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

        *buffer++
            = (((((gctUINT32) (~0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ?
 4:4))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4))) &  ((((gctUINT32) (~0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ?
 7:7))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 7:7) - (0 ? 7:7) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ? 7:7))));

        /* Arm the PE-FE Semaphore. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* STALL FE until PE is done flushing. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* LINK to next slot to flush FE FIFO. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x08 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (5) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

        *buffer++
            = physical + (stCmds + 20) * 4;

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0061) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

        *buffer++
            = config;

        /* Arm the PE-FE Semaphore. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* STALL FE until PE is done flushing. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* Event 29. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E01) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

        event = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ?
 6:6))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6)));
        event = ((((gctUINT32) (event)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) ((gctUINT32) (29) & ((gctUINT32) ((((1 ? 4:0) - (0 ?
 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0)));

        *buffer++
            = event;

        /* Append END. */
        *buffer++
           = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x02 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));
    }

    if (Bytes != gcvNULL)
    {
        *Bytes = bytesAligned;
    }

    if (WaitLinkOffset != gcvNULL)
    {
        *WaitLinkOffset = bytes - 4 * 4;
    }

    if (WaitLinkBytes != gcvNULL)
    {
        *WaitLinkBytes = 4 * 4;
    }

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}
#endif

/*******************************************************************************
**
**  gckHARDWARE_BuildVirtualAddress
**
**  Build a virtual address.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**      gctUINT32 Index
**          Index into page table.
**
**      gctUINT32 Offset
**          Offset into page.
**
**  OUTPUT:
**
**      gctUINT32 * Address
**          Pointer to a variable receiving te hardware address.
*/
gceSTATUS
gckHARDWARE_BuildVirtualAddress(
    IN gckHARDWARE Hardware,
    IN gctUINT32 Index,
    IN gctUINT32 Offset,
    OUT gctUINT32 * Address
    )
{
    gcmkHEADER_ARG("Hardware=0x%x Index=%u Offset=%u", Hardware, Index, Offset);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    /* Build virtual address. */
    *Address = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:31) - (0 ? 31:31) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1))))))) << (0 ?
 31:31))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 31:31) - (0 ? 31:31) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1))))))) << (0 ? 31:31)))
             | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 30:0) - (0 ? 30:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 30:0) - (0 ? 30:0) + 1))))))) << (0 ?
 30:0))) | (((gctUINT32) ((gctUINT32) (Offset | (Index << 12)) & ((gctUINT32) ((((1 ?
 30:0) - (0 ? 30:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 30:0) - (0 ? 30:0) + 1))))))) << (0 ?
 30:0)));

    /* Success. */
    gcmkFOOTER_ARG("*Address=0x%08x", *Address);
    return gcvSTATUS_OK;
}

gceSTATUS
gckHARDWARE_GetIdle(
    IN gckHARDWARE Hardware,
    IN gctBOOL Wait,
    OUT gctUINT32 * Data
    )
{
    gceSTATUS status;
    gctUINT32 idle = 0;
    gctINT retry, poll, pollCount;
    gctUINT32 address;

    gcmkHEADER_ARG("Hardware=0x%x Wait=%d", Hardware, Wait);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Data != gcvNULL);


    /* If we have to wait, try 100 polls per millisecond. */
    pollCount = Wait ? 100 : 1;

    /* At most, try for 1 second. */
    for (retry = 0; retry < 1000; ++retry)
    {
        /* If we have to wait, try 100 polls per millisecond. */
        for (poll = pollCount; poll > 0; --poll)
        {
            /* Read register. */
            gcmkONERROR(
                gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00004, &idle));

            /* Read the current FE address. */
            gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                             Hardware->core,
                                             0x00664,
                                             &address));

            gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                             Hardware->core,
                                             0x00664,
                                             &address));

            /* See if we have to wait for FE idle. */
            if (_IsGPUIdle(idle)
             && (address == Hardware->lastEnd + 8)
             )
            {
                /* FE is idle. */
                break;
            }
        }

        /* Check if we need to wait for FE and FE is busy. */
        if (Wait && !_IsGPUIdle(idle))
        {
            /* Wait a little. */
            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                           "%s: Waiting for idle: 0x%08X",
                           __FUNCTION__, idle);

            gcmkVERIFY_OK(gckOS_Delay(Hardware->os, 1));
        }
        else
        {
            break;
        }
    }

    /* Return idle to caller. */
    *Data = idle;

#if defined(EMULATOR)
    /* Wait a little while until CModel FE gets END.
     * END is supposed to be appended by caller.
     */
    gckOS_Delay(gcvNULL, 100);
#endif

    /* Success. */
    gcmkFOOTER_ARG("*Data=0x%08x", *Data);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/* Flush the caches. */
gceSTATUS
gckHARDWARE_Flush(
    IN gckHARDWARE Hardware,
    IN gceKERNEL_FLUSH Flush,
    IN gctPOINTER Logical,
    IN OUT gctUINT32 * Bytes
    )
{
    gctUINT32 pipe;
    gctUINT32 flush = 0;
    gctUINT32 flushVST = 0;
    gctBOOL flushTileStatus;
    gctUINT32_PTR logical = (gctUINT32_PTR) Logical;
    gceSTATUS status;
    gctBOOL halti5;
    gctBOOL flushICache;
    gctBOOL flushTXDescCache;
    gctBOOL flushTFB;
    gctBOOL hwTFB;

    gcmkHEADER_ARG("Hardware=0x%x Flush=0x%x Logical=0x%x *Bytes=%lu",
                   Hardware, Flush, Logical, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Get current pipe. */
    pipe = Hardware->kernel->command->pipeSelect;

    halti5 = gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_HALTI5);

    hwTFB = gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_HW_TFB);

    /* Flush tile status cache. */
    flushTileStatus = Flush & gcvFLUSH_TILE_STATUS;

    /* Flush Icache for halti5 hardware as we dont do it when program or context switches*/
    flushICache = (Flush & gcvFLUSH_ICACHE) && halti5;

    /* Flush texture descriptor cache */
    flushTXDescCache = Flush & gcvFLUSH_TXDESC;

    /* Flush USC cache for TFB client */
    flushTFB = (Flush & gcvFLUSH_TFBHEADER) && hwTFB;

    /* Flush TFB for vertex buffer */
    if (hwTFB && (Flush & gcvFLUSH_VERTEX))
    {
        flushTFB = gcvTRUE;
    }

    /* Flush 3D color cache. */
    if ((Flush & gcvFLUSH_COLOR) && (pipe == 0x0))
    {
        flush |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1)));
    }

    /* Flush 3D depth cache. */
    if ((Flush & gcvFLUSH_DEPTH) && (pipe == 0x0))
    {
        flush |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));
    }

    /* Flush 3D texture cache. */
    if ((Flush & gcvFLUSH_TEXTURE) && (pipe == 0x0))
    {
        flush |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ?
 2:2))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2)));
        flushVST = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ?
 4:4))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4)));
    }

    /* Flush 2D cache. */
    if ((Flush & gcvFLUSH_2D) && (pipe == 0x1))
    {
        flush |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ?
 3:3))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3)));
    }

    /* Flush L2 cache. */
    if ((Flush & gcvFLUSH_L2) && (pipe == 0x0))
    {
        flush |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ?
 6:6))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6)));
    }

    /* Vertex buffer and texture could be touched by SHL1 for SSBO and image load/store */
    if ((Flush & (gcvFLUSH_VERTEX | gcvFLUSH_TEXTURE)) && (pipe == 0x0))
    {
        flush |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ?
 5:5))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5)))
               | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 10:10) - (0 ? 10:10) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 10:10) - (0 ? 10:10) + 1))))))) << (0 ?
 10:10))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 10:10) - (0 ? 10:10) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 10:10) - (0 ? 10:10) + 1))))))) << (0 ? 10:10)))
               | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 11:11) - (0 ? 11:11) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:11) - (0 ? 11:11) + 1))))))) << (0 ?
 11:11))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 11:11) - (0 ? 11:11) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 11:11) - (0 ? 11:11) + 1))))))) << (0 ? 11:11)));
    }

    /* See if there is a valid flush. */
    if ((flush == 0) &&
        (flushTileStatus == gcvFALSE) &&
        (flushICache == gcvFALSE) &&
        (flushTXDescCache == gcvFALSE) &&
        (flushTFB == gcvFALSE))
    {
        if (Bytes != gcvNULL)
        {
            /* No bytes required. */
            *Bytes = 0;
        }
    }
    else
    {
        gctUINT32 reserveBytes = 0;
        gctBOOL txCacheFix = gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_TEX_CACHE_FLUSH_FIX)
                           ? gcvTRUE : gcvFALSE;

        /* Determine reserve bytes. */
        if (!txCacheFix || flushICache || flushTXDescCache)
        {
            /* Semaphore/Stall */
            reserveBytes += 4 * gcmSIZEOF(gctUINT32);
        }

        if (flush)
        {
            reserveBytes += 2 * gcmSIZEOF(gctUINT32);
        }

        if (flushVST)
        {
            reserveBytes += 2 * gcmSIZEOF(gctUINT32);
        }

        if (flushTileStatus)
        {
            reserveBytes += 2 * gcmSIZEOF(gctUINT32);
        }

        if (flushICache)
        {
            reserveBytes += 2 * gcmSIZEOF(gctUINT32);
        }

        if (flushTXDescCache)
        {
            reserveBytes += 2 * gcmSIZEOF(gctUINT32);
        }

        if (flushTFB)
        {
            reserveBytes += 2 * gcmSIZEOF(gctUINT32);
        }

        /* Semaphore/Stall */
        reserveBytes += 4 * gcmSIZEOF(gctUINT32);

        /* Copy to command queue. */
        if (Logical != gcvNULL)
        {
            if (*Bytes < reserveBytes)
            {
                /* Command queue too small. */
                gcmkONERROR(gcvSTATUS_BUFFER_TOO_SMALL);
            }

            if (!txCacheFix || flushICache || flushTXDescCache)
            {
                /* Semaphore. */
                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

                /* Stall. */
                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));
            }

            if (flush)
            {
                /* Append LOAD_STATE to AQFlush. */
                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E03) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

                *logical++ = flush;

                gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE, "0x%x: FLUSH 0x%x", logical - 1, flush);
            }

            if (flushVST)
            {
                /* Append LOAD_STATE to AQFlush. */
                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E03) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

                *logical++ = flushVST;

                gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE, "0x%x: FLUSH 0x%x", logical - 1, flush);
            }

            if (flushTileStatus)
            {
                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0594) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));

                gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                               "0x%x: FLUSH TILE STATUS 0x%x", logical - 1, logical[-1]);
            }

            if (flushICache)
            {
                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x022C) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 1:1) - (0 ?
 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ?
 2:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 2:2) - (0 ?
 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ?
 2:2)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ?
 3:3))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 3:3) - (0 ?
 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ?
 3:3)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ?
 4:4))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 4:4) - (0 ?
 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ?
 4:4)));

                gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                               "0x%x: FLUSH Icache 0x%x", logical - 1, logical[-1]);

            }

            if (flushTXDescCache)
            {
                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x5311) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:28) - (0 ? 31:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:28) - (0 ? 31:28) + 1))))))) << (0 ?
 31:28))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 31:28) - (0 ? 31:28) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:28) - (0 ? 31:28) + 1))))))) << (0 ? 31:28)));

                gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                               "0x%x: FLUSH Icache 0x%x", logical - 1, logical[-1]);

            }

            if (flushTFB)
            {
                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x7003) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

                *logical++
                    = 0x12345678;

                gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                               "0x%x: FLUSH TFB cache 0x%x", logical - 1, logical[-1]);

            }

            /* Semaphore. */
            *logical++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)));

            *logical++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

            /* Stall. */
            *logical++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

            *logical++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));
        }

        if (Bytes != gcvNULL)
        {
            /* bytes required. */
            *Bytes = reserveBytes;
        }
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
gckHARDWARE_SetFastClear(
    IN gckHARDWARE Hardware,
    IN gctINT Enable,
    IN gctINT Compression
    )
{
#if gcdENABLE_3D
    gctUINT32 debug;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Enable=%d Compression=%d",
                   Hardware, Enable, Compression);

    /* Only process if fast clear is available. */
    if (gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_FAST_CLEAR))
    {
        if (Enable == -1)
        {
            /* Determine automatic value for fast clear. */
            Enable = ((Hardware->identity.chipModel    != gcv500)
                     || (Hardware->identity.chipRevision >= 3)
                     ) ? 1 : 0;
        }

        if (Compression == -1)
        {
            /* Determine automatic value for compression. */
            Compression = Enable & gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_ZCOMPRESSION);
        }

        /* Read AQMemoryDebug register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00414, &debug));

        /* Set fast clear bypass. */
        debug = ((((gctUINT32) (debug)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 20:20) - (0 ? 20:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:20) - (0 ? 20:20) + 1))))))) << (0 ?
 20:20))) | (((gctUINT32) ((gctUINT32) (Enable == 0) & ((gctUINT32) ((((1 ?
 20:20) - (0 ? 20:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:20) - (0 ? 20:20) + 1))))))) << (0 ?
 20:20)));

        if (gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_BUG_FIXES7) ||
            (Hardware->identity.chipModel >= gcv4000))
        {
            /* Set compression bypass. */
            debug = ((((gctUINT32) (debug)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 21:21) - (0 ? 21:21) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:21) - (0 ? 21:21) + 1))))))) << (0 ?
 21:21))) | (((gctUINT32) ((gctUINT32) (Compression == 0) & ((gctUINT32) ((((1 ?
 21:21) - (0 ? 21:21) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:21) - (0 ? 21:21) + 1))))))) << (0 ?
 21:21)));
        }

        /* Write back AQMemoryDebug register. */
        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  0x00414,
                                  debug));

        /* Store fast clear and comprersison flags. */
        Hardware->allowFastClear   = Enable;
        Hardware->allowCompression = Compression;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "FastClear=%d Compression=%d", Enable, Compression);
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
#else
    return gcvSTATUS_OK;
#endif
}

typedef enum
{
    gcvPOWER_FLAG_INITIALIZE    = 1 << 0,
    gcvPOWER_FLAG_STALL         = 1 << 1,
    gcvPOWER_FLAG_STOP          = 1 << 2,
    gcvPOWER_FLAG_START         = 1 << 3,
    gcvPOWER_FLAG_RELEASE       = 1 << 4,
    gcvPOWER_FLAG_DELAY         = 1 << 5,
    gcvPOWER_FLAG_SAVE          = 1 << 6,
    gcvPOWER_FLAG_ACQUIRE       = 1 << 7,
    gcvPOWER_FLAG_POWER_OFF     = 1 << 8,
    gcvPOWER_FLAG_CLOCK_OFF     = 1 << 9,
    gcvPOWER_FLAG_CLOCK_ON      = 1 << 10,
}
gcePOWER_FLAGS;

#if gcmIS_DEBUG(gcdDEBUG_TRACE)
static gctCONST_STRING
_PowerEnum(gceCHIPPOWERSTATE State)
{
    const gctCONST_STRING states[] =
    {
        gcmSTRING(gcvPOWER_ON),
        gcmSTRING(gcvPOWER_OFF),
        gcmSTRING(gcvPOWER_IDLE),
        gcmSTRING(gcvPOWER_SUSPEND),
        gcmSTRING(gcvPOWER_SUSPEND_ATPOWERON),
        gcmSTRING(gcvPOWER_OFF_ATPOWERON),
        gcmSTRING(gcvPOWER_IDLE_BROADCAST),
        gcmSTRING(gcvPOWER_SUSPEND_BROADCAST),
        gcmSTRING(gcvPOWER_OFF_BROADCAST),
        gcmSTRING(gcvPOWER_OFF_RECOVERY),
        gcmSTRING(gcvPOWER_OFF_TIMEOUT),
        gcmSTRING(gcvPOWER_ON_AUTO)
    };

    if ((State >= gcvPOWER_ON) && (State <= gcvPOWER_ON_AUTO))
    {
        return states[State - gcvPOWER_ON];
    }

    return "unknown";
}
#endif

/*******************************************************************************
**
**  gckHARDWARE_SetPowerManagementState
**
**  Set GPU to a specified power state.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**      gceCHIPPOWERSTATE State
**          Power State.
**
*/
gceSTATUS
gckHARDWARE_SetPowerManagementState(
    IN gckHARDWARE Hardware,
    IN gceCHIPPOWERSTATE State
    )
{
    gceSTATUS status;
    gckCOMMAND command = gcvNULL;
    gckOS os;
    gctUINT flag, clock;
    gctBOOL acquired = gcvFALSE;
    gctBOOL mutexAcquired = gcvFALSE;
    gctBOOL stall = gcvTRUE;
    gctBOOL broadcast = gcvFALSE;
#if gcdPOWEROFF_TIMEOUT
    gctBOOL timeout = gcvFALSE;
    gctBOOL isAfter = gcvFALSE;
    gctUINT32 currentTime;
#endif
    gctUINT32 process, thread;
    gctBOOL commandStarted = gcvFALSE;
    gctBOOL isrStarted = gcvFALSE;

#if gcdENABLE_PROFILING
    gctUINT64 time, freq, mutexTime, onTime, stallTime, stopTime, delayTime,
              initTime, offTime, startTime, totalTime;
#endif
    gctBOOL global = gcvFALSE;
    gctBOOL globalAcquired = gcvFALSE;

    /* State transition flags. */
    static const gctUINT flags[4][4] =
    {
        /* gcvPOWER_ON           */
        {   /* ON                */ 0,
            /* OFF               */ gcvPOWER_FLAG_ACQUIRE   |
                                    gcvPOWER_FLAG_STALL     |
                                    gcvPOWER_FLAG_STOP      |
                                    gcvPOWER_FLAG_POWER_OFF |
                                    gcvPOWER_FLAG_CLOCK_OFF,
            /* IDLE              */ gcvPOWER_FLAG_ACQUIRE   |
                                    gcvPOWER_FLAG_STALL,
            /* SUSPEND           */ gcvPOWER_FLAG_ACQUIRE   |
                                    gcvPOWER_FLAG_STALL     |
                                    gcvPOWER_FLAG_STOP      |
                                    gcvPOWER_FLAG_CLOCK_OFF,
        },

        /* gcvPOWER_OFF          */
        {   /* ON                */ gcvPOWER_FLAG_INITIALIZE |
                                    gcvPOWER_FLAG_START      |
                                    gcvPOWER_FLAG_RELEASE    |
                                    gcvPOWER_FLAG_DELAY,
            /* OFF               */ 0,
            /* IDLE              */ gcvPOWER_FLAG_INITIALIZE |
                                    gcvPOWER_FLAG_START      |
                                    gcvPOWER_FLAG_DELAY,
            /* SUSPEND           */ gcvPOWER_FLAG_INITIALIZE |
                                    gcvPOWER_FLAG_CLOCK_OFF,
        },

        /* gcvPOWER_IDLE         */
        {   /* ON                */ gcvPOWER_FLAG_RELEASE,
            /* OFF               */ gcvPOWER_FLAG_STOP      |
                                    gcvPOWER_FLAG_POWER_OFF |
                                    gcvPOWER_FLAG_CLOCK_OFF,
            /* IDLE              */ 0,
            /* SUSPEND           */ gcvPOWER_FLAG_STOP      |
                                    gcvPOWER_FLAG_CLOCK_OFF,
        },

        /* gcvPOWER_SUSPEND      */
        {   /* ON                */ gcvPOWER_FLAG_START     |
                                    gcvPOWER_FLAG_RELEASE   |
                                    gcvPOWER_FLAG_DELAY     |
                                    gcvPOWER_FLAG_CLOCK_ON,
            /* OFF               */ gcvPOWER_FLAG_SAVE      |
                                    gcvPOWER_FLAG_POWER_OFF |
                                    gcvPOWER_FLAG_CLOCK_OFF,
            /* IDLE              */ gcvPOWER_FLAG_START     |
                                    gcvPOWER_FLAG_DELAY     |
                                    gcvPOWER_FLAG_CLOCK_ON,
            /* SUSPEND           */ 0,
        },
    };

    /* Clocks. */
    static const gctUINT clocks[4] =
    {
        /* gcvPOWER_ON */
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ?
 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 1:1) - (0 ?
 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 8:2) - (0 ?
 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ?
 8:2))) | (((gctUINT32) ((gctUINT32) (64) & ((gctUINT32) ((((1 ? 8:2) - (0 ?
 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ?
 8:2))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ?
 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 9:9) - (0 ?
 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9))),

        /* gcvPOWER_OFF */
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ?
 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 1:1) - (0 ?
 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 8:2) - (0 ?
 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ?
 8:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 8:2) - (0 ?
 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ?
 8:2))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ?
 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 9:9) - (0 ?
 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9))),

        /* gcvPOWER_IDLE */
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ?
 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 1:1) - (0 ?
 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 8:2) - (0 ?
 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ?
 8:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 8:2) - (0 ?
 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ?
 8:2))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ?
 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 9:9) - (0 ?
 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9))),

        /* gcvPOWER_SUSPEND */
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ?
 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 1:1) - (0 ?
 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 8:2) - (0 ?
 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ?
 8:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 8:2) - (0 ?
 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ?
 8:2))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ?
 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 9:9) - (0 ?
 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9))),
    };

    gcmkHEADER_ARG("Hardware=0x%x State=%d", Hardware, State);
#if gcmIS_DEBUG(gcdDEBUG_TRACE)
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Switching to power state %d(%s)",
                   State, _PowerEnum(State));
#endif

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Get the gckOS object pointer. */
    os = Hardware->os;
    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    /* Get the gckCOMMAND object pointer. */
    gcmkVERIFY_OBJECT(Hardware->kernel, gcvOBJ_KERNEL);
    command = Hardware->kernel->command;
    gcmkVERIFY_OBJECT(command, gcvOBJ_COMMAND);

    /* Start profiler. */
    gcmkPROFILE_INIT(freq, time);

    /* Convert the broadcast power state. */
    switch (State)
    {
    case gcvPOWER_SUSPEND_ATPOWERON:
        /* Convert to SUSPEND and don't wait for STALL. */
        State = gcvPOWER_SUSPEND;
        stall = gcvFALSE;
        break;

    case gcvPOWER_OFF_ATPOWERON:
        /* Convert to OFF and don't wait for STALL. */
        State = gcvPOWER_OFF;
        stall = gcvFALSE;
        break;

    case gcvPOWER_IDLE_BROADCAST:
        /* Convert to IDLE and note we are inside broadcast. */
        State     = gcvPOWER_IDLE;
        broadcast = gcvTRUE;
        break;

    case gcvPOWER_SUSPEND_BROADCAST:
        /* Convert to SUSPEND and note we are inside broadcast. */
        State     = gcvPOWER_SUSPEND;
        broadcast = gcvTRUE;
        break;

    case gcvPOWER_OFF_BROADCAST:
        /* Convert to OFF and note we are inside broadcast. */
        State     = gcvPOWER_OFF;
        broadcast = gcvTRUE;
        break;

    case gcvPOWER_OFF_RECOVERY:
        /* Convert to OFF and note we are inside recovery. */
        State     = gcvPOWER_OFF;
        stall     = gcvFALSE;
        broadcast = gcvTRUE;
        break;

    case gcvPOWER_ON_AUTO:
        /* Convert to ON and note we are inside recovery. */
        State = gcvPOWER_ON;
        break;

    case gcvPOWER_ON:
    case gcvPOWER_IDLE:
    case gcvPOWER_SUSPEND:
    case gcvPOWER_OFF:
        /* Mark as global power management. */
        global = gcvTRUE;
        break;

#if gcdPOWEROFF_TIMEOUT
    case gcvPOWER_OFF_TIMEOUT:
        /* Convert to OFF and note we are inside broadcast. */
        State     = gcvPOWER_OFF;
        broadcast = gcvTRUE;
        /* Check time out */
        timeout = gcvTRUE;
        break;
#endif

    default:
        break;
    }

    if (Hardware->powerManagement == gcvFALSE
     && State != gcvPOWER_ON
    )
    {
        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

    /* Get current process and thread IDs. */
    gcmkONERROR(gckOS_GetProcessID(&process));
    gcmkONERROR(gckOS_GetThreadID(&thread));

    if (broadcast)
    {
        /* Try to acquire the power mutex. */
        status = gckOS_AcquireMutex(os, Hardware->powerMutex, 0);

        if (status == gcvSTATUS_TIMEOUT)
        {
            /* Check if we already own this mutex. */
            if ((Hardware->powerProcess == process)
            &&  (Hardware->powerThread  == thread)
            )
            {
                /* Bail out on recursive power management. */
                gcmkFOOTER_NO();
                return gcvSTATUS_OK;
            }
            else if (State != gcvPOWER_ON)
            {
                /* Called from IST,
                ** so waiting here will cause deadlock,
                ** if lock holder call gckCOMMAND_Stall() */
                status = gcvSTATUS_OK;
                goto OnError;
            }
            else
            {
                /* Acquire the power mutex. */
                gcmkONERROR(gckOS_AcquireMutex(os,
                                               Hardware->powerMutex,
                                               gcvINFINITE));
            }
        }
    }
    else
    {
        /* Acquire the power mutex. */
        gcmkONERROR(gckOS_AcquireMutex(os, Hardware->powerMutex, gcvINFINITE));
    }

    /* Get time until mtuex acquired. */
    gcmkPROFILE_QUERY(time, mutexTime);

    Hardware->powerProcess = process;
    Hardware->powerThread  = thread;
    mutexAcquired          = gcvTRUE;

    /* Grab control flags and clock. */
    flag  = flags[Hardware->chipPowerState][State];
    clock = clocks[State];

#if gcdENABLE_FSCALE_VAL_ADJUST
    if (State == gcvPOWER_ON)
    {
        clock = ((((gctUINT32) (clock)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ?
 8:2))) | (((gctUINT32) ((gctUINT32) (Hardware->powerOnFscaleVal) & ((gctUINT32) ((((1 ?
 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ?
 8:2)));
    }
#endif

    if (State == gcvPOWER_SUSPEND && Hardware->chipPowerState == gcvPOWER_OFF && broadcast)
    {
#if gcdPOWER_SUSPEND_WHEN_IDLE
    /* Do nothing */

        /* Release the power mutex. */
        gcmkONERROR(gckOS_ReleaseMutex(os, Hardware->powerMutex));

           gcmkFOOTER_NO();
        return gcvSTATUS_OK;
#else
    /* Clock should be on when switch power from off to suspend */
        clock = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) |
                ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 1:1) - (0 ?
 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1))) |
                ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ?
 8:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 8:2) - (0 ?
 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ?
 8:2))) |
                ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 9:9) - (0 ?
 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9))) ;
#endif
    }

#if gcdPOWEROFF_TIMEOUT
    if (timeout)
    {
        gcmkONERROR(gckOS_GetTicks(&currentTime));

        gcmkONERROR(
            gckOS_TicksAfter(Hardware->powerOffTime, currentTime, &isAfter));

        /* powerOffTime is pushed forward, give up.*/
        if (isAfter
        /* Expect a transition start from IDLE or SUSPEND. */
        ||  (Hardware->chipPowerState == gcvPOWER_ON)
        ||  (Hardware->chipPowerState == gcvPOWER_OFF)
        )
        {
            /* Release the power mutex. */
            gcmkONERROR(gckOS_ReleaseMutex(os, Hardware->powerMutex));

            /* No need to do anything. */
            gcmkFOOTER_NO();
            return gcvSTATUS_OK;
        }

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "Power Off GPU[%d] at %u [supposed to be at %u]",
                       Hardware->core, currentTime, Hardware->powerOffTime);
    }
#endif

    if (flag == 0)
    {
        /* Release the power mutex. */
        gcmkONERROR(gckOS_ReleaseMutex(os, Hardware->powerMutex));

        /* No need to do anything. */
        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

    /* If this is an internal power management, we have to check if we can grab
    ** the global power semaphore. If we cannot, we have to wait until the
    ** external world changes power management. */
    if (!global)
    {
        /* Try to acquire the global semaphore. */
        status = gckOS_TryAcquireSemaphore(os, Hardware->globalSemaphore);
        if (status == gcvSTATUS_TIMEOUT)
        {
            if (State == gcvPOWER_IDLE || State == gcvPOWER_SUSPEND)
            {
                /* Called from thread routine which should NEVER sleep.*/
                status = gcvSTATUS_OK;
                goto OnError;
            }

            /* Release the power mutex. */
            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                           "Releasing the power mutex.");
            gcmkONERROR(gckOS_ReleaseMutex(os, Hardware->powerMutex));
            mutexAcquired = gcvFALSE;

            /* Wait for the semaphore. */
            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                           "Waiting for global semaphore.");
            gcmkONERROR(gckOS_AcquireSemaphore(os, Hardware->globalSemaphore));
            globalAcquired = gcvTRUE;

            /* Acquire the power mutex. */
            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                           "Reacquiring the power mutex.");
            gcmkONERROR(gckOS_AcquireMutex(os,
                                           Hardware->powerMutex,
                                           gcvINFINITE));
            mutexAcquired = gcvTRUE;

            /* chipPowerState may be changed by external world during the time
            ** we give up powerMutex, so updating flag now is necessary. */
            flag = flags[Hardware->chipPowerState][State];

            if (flag == 0)
            {
                gcmkONERROR(gckOS_ReleaseSemaphore(os, Hardware->globalSemaphore));
                globalAcquired = gcvFALSE;

                gcmkONERROR(gckOS_ReleaseMutex(os, Hardware->powerMutex));
                mutexAcquired = gcvFALSE;

                gcmkFOOTER_NO();
                return gcvSTATUS_OK;
            }
        }
        else
        {
            /* Error. */
            gcmkONERROR(status);
        }

        /* Release the global semaphore again. */
        gcmkONERROR(gckOS_ReleaseSemaphore(os, Hardware->globalSemaphore));
        globalAcquired = gcvFALSE;
    }
    else
    {
        if (State == gcvPOWER_OFF || State == gcvPOWER_SUSPEND || State == gcvPOWER_IDLE)
        {
            /* Acquire the global semaphore if it has not been acquired. */
            status = gckOS_TryAcquireSemaphore(os, Hardware->globalSemaphore);
            if (status == gcvSTATUS_OK)
            {
                globalAcquired = gcvTRUE;
            }
            else if (status != gcvSTATUS_TIMEOUT)
            {
                /* Other errors. */
                gcmkONERROR(status);
            }
            /* Ignore gcvSTATUS_TIMEOUT and leave globalAcquired as gcvFALSE.
            ** gcvSTATUS_TIMEOUT means global semaphore has already
            ** been acquired before this operation, so even if we fail,
            ** we should not release it in our error handling. It should be
            ** released by the next successful global gcvPOWER_ON. */
        }

        /* Global power management can't be aborted, so sync with
        ** proceeding last commit. */
        if (flag & gcvPOWER_FLAG_ACQUIRE)
        {
            /* Acquire the power management semaphore. */
            gcmkONERROR(gckOS_AcquireSemaphore(os, command->powerSemaphore));
            acquired = gcvTRUE;

            /* avoid acquiring again. */
            flag &= ~gcvPOWER_FLAG_ACQUIRE;
        }
    }

    if (flag & (gcvPOWER_FLAG_INITIALIZE | gcvPOWER_FLAG_CLOCK_ON))
    {
        /* Turn on the power. */
        gcmkONERROR(gckOS_SetGPUPower(os, Hardware->core, gcvTRUE, gcvTRUE));

        /* Mark clock and power as enabled. */
        Hardware->clockState = gcvTRUE;
        Hardware->powerState = gcvTRUE;

        for (;;)
        {
            /* Check if GPU is present and awake. */
            status = _IsGPUPresent(Hardware);

            /* Check if the GPU is not responding. */
            if (status == gcvSTATUS_GPU_NOT_RESPONDING)
            {
                /* Turn off the power and clock. */
                gcmkONERROR(gckOS_SetGPUPower(os, Hardware->core, gcvFALSE, gcvFALSE));

                Hardware->clockState = gcvFALSE;
                Hardware->powerState = gcvFALSE;

                /* Wait a little. */
                gckOS_Delay(os, 1);

                /* Turn on the power and clock. */
                gcmkONERROR(gckOS_SetGPUPower(os, Hardware->core, gcvTRUE, gcvTRUE));

                Hardware->clockState = gcvTRUE;
                Hardware->powerState = gcvTRUE;

                /* We need to initialize the hardware and start the command
                 * processor. */
                flag |= gcvPOWER_FLAG_INITIALIZE | gcvPOWER_FLAG_START;
            }
            else
            {
                /* Test for error. */
                gcmkONERROR(status);

                /* Break out of loop. */
                break;
            }
        }
    }

    /* Get time until powered on. */
    gcmkPROFILE_QUERY(time, onTime);

    if ((flag & gcvPOWER_FLAG_STALL) && stall)
    {
        gctBOOL idle;
        gctINT32 atomValue;

        /* For global operation, all pending commits have already been
        ** blocked by globalSemaphore or powerSemaphore.*/
        if (!global)
        {
            /* Check commit atom. */
            gcmkONERROR(gckOS_AtomGet(os, command->atomCommit, &atomValue));

            if (atomValue > 0)
            {
                /* Commits are pending - abort power management. */
                status = broadcast ? gcvSTATUS_CHIP_NOT_READY
                                   : gcvSTATUS_MORE_DATA;
                goto OnError;
            }
        }

        if (broadcast)
        {
            /* Check for idle. */
            gcmkONERROR(gckHARDWARE_QueryIdle(Hardware, &idle));

            if (!idle)
            {
                status = gcvSTATUS_CHIP_NOT_READY;
                goto OnError;
            }
        }

        else
        {
            /* Wait to finish all commands. */
            gcmkONERROR(gckCOMMAND_Stall(command, gcvTRUE));
        }
    }

    /* Get time until stalled. */
    gcmkPROFILE_QUERY(time, stallTime);

    if (flag & gcvPOWER_FLAG_ACQUIRE)
    {
        /* Acquire the power management semaphore. */
        gcmkONERROR(gckOS_AcquireSemaphore(os, command->powerSemaphore));
        acquired = gcvTRUE;
    }

    if (flag & gcvPOWER_FLAG_STOP)
    {
        /* Stop the command parser. */
        gcmkONERROR(gckCOMMAND_Stop(command));

        /* Stop the Isr. */
        if (Hardware->stopIsr)
        {
            gcmkONERROR(Hardware->stopIsr(Hardware->isrContext));
        }
    }

    /* Flush Cache before Power Off. */
    if (flag & gcvPOWER_FLAG_POWER_OFF)
    {
        if (Hardware->clockState == gcvFALSE)
        {
            /* Turn off the GPU power. */
            gcmkONERROR(
                    gckOS_SetGPUPower(os,
                        Hardware->core,
                        gcvTRUE,
                        gcvTRUE));

            Hardware->clockState = gcvTRUE;

            if (gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_DYNAMIC_FREQUENCY_SCALING) != gcvTRUE)
            {
                /* Write the clock control register. */
                gcmkONERROR(gckOS_WriteRegisterEx(os,
                                                  Hardware->core,
                                                  0x00000,
                                                  clocks[0]));

                /* Done loading the frequency scaler. */
                gcmkONERROR(gckOS_WriteRegisterEx(os,
                                                  Hardware->core,
                                                  0x00000,
                                                  ((((gctUINT32) (clocks[0])) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 9:9) - (0 ?
 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9)))));
            }
        }

        gcmkONERROR(gckCOMMAND_Start(command));

        gcmkONERROR(_FlushCache(Hardware, command));

        gckOS_Delay(gcvNULL, 1);

        /* Stop the command parser. */
        gcmkONERROR(gckCOMMAND_Stop(command));

        flag |= gcvPOWER_FLAG_CLOCK_OFF;
    }

    /* Get time until stopped. */
    gcmkPROFILE_QUERY(time, stopTime);

    /* Only process this when hardware is enabled. */
    if (Hardware->clockState && Hardware->powerState
    /* Don't touch clock control if dynamic frequency scaling is available. */
    && gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_DYNAMIC_FREQUENCY_SCALING) != gcvTRUE
    )
    {
        if (flag & (gcvPOWER_FLAG_POWER_OFF | gcvPOWER_FLAG_CLOCK_OFF))
        {
            if (Hardware->identity.chipModel == gcv4000
            && ((Hardware->identity.chipRevision == 0x5208) || (Hardware->identity.chipRevision == 0x5222)))
            {
                clock &= ~2U;
            }
        }

        /* Write the clock control register. */
        gcmkONERROR(gckOS_WriteRegisterEx(os,
                                          Hardware->core,
                                          0x00000,
                                          clock));

        /* Done loading the frequency scaler. */
        gcmkONERROR(gckOS_WriteRegisterEx(os,
                                          Hardware->core,
                                          0x00000,
                                          ((((gctUINT32) (clock)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 9:9) - (0 ?
 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9)))));
    }

    if (flag & gcvPOWER_FLAG_DELAY)
    {
        /* Wait for the specified amount of time to settle coming back from
        ** power-off or suspend state. */
        gcmkONERROR(gckOS_Delay(os, gcdPOWER_CONTROL_DELAY));
    }

    /* Get time until delayed. */
    gcmkPROFILE_QUERY(time, delayTime);

    if (flag & gcvPOWER_FLAG_INITIALIZE)
    {
        /* Initialize hardware. */
        gcmkONERROR(gckHARDWARE_InitializeHardware(Hardware));

        gcmkONERROR(gckHARDWARE_SetFastClear(Hardware,
                                             Hardware->allowFastClear,
                                             Hardware->allowCompression));

        /* Force the command queue to reload the next context. */
        command->currContext = gcvNULL;

        /* Trigger a possible dummy draw. */
        command->dummyDraw = gcvTRUE;
    }

    /* Get time until initialized. */
    gcmkPROFILE_QUERY(time, initTime);

    if (flag & (gcvPOWER_FLAG_POWER_OFF | gcvPOWER_FLAG_CLOCK_OFF))
    {
        /* Turn off the GPU power. */
        gcmkONERROR(
            gckOS_SetGPUPower(os,
                              Hardware->core,
                              (flag & gcvPOWER_FLAG_CLOCK_OFF) ? gcvFALSE
                                                               : gcvTRUE,
                              (flag & gcvPOWER_FLAG_POWER_OFF) ? gcvFALSE
                                                               : gcvTRUE));

        /* Save current hardware power and clock states. */
        Hardware->clockState = (flag & gcvPOWER_FLAG_CLOCK_OFF) ? gcvFALSE
                                                                : gcvTRUE;
        Hardware->powerState = (flag & gcvPOWER_FLAG_POWER_OFF) ? gcvFALSE
                                                                : gcvTRUE;
    }

    /* Get time until off. */
    gcmkPROFILE_QUERY(time, offTime);

    if (flag & gcvPOWER_FLAG_START)
    {
        /* Start the command processor. */
        gcmkONERROR(gckCOMMAND_Start(command));
        commandStarted = gcvTRUE;

        if (Hardware->startIsr)
        {
            /* Start the Isr. */
            gcmkONERROR(Hardware->startIsr(Hardware->isrContext));
            isrStarted = gcvTRUE;
        }
    }

    /* Get time until started. */
    gcmkPROFILE_QUERY(time, startTime);

    if (flag & gcvPOWER_FLAG_RELEASE)
    {
        /* Release the power management semaphore. */
        gcmkONERROR(gckOS_ReleaseSemaphore(os, command->powerSemaphore));
        acquired = gcvFALSE;

        if (global)
        {
            /* Verify global semaphore has been acquired already before
            ** we release it.
            ** If it was acquired, gckOS_TryAcquireSemaphore will return
            ** gcvSTATUS_TIMEOUT and we release it. Otherwise, global
            ** semaphore will be acquried now, but it still is released
            ** immediately. */
            status = gckOS_TryAcquireSemaphore(os, Hardware->globalSemaphore);
            if (status != gcvSTATUS_TIMEOUT)
            {
                gcmkONERROR(status);
            }

            /* Release the global semaphore. */
            gcmkONERROR(gckOS_ReleaseSemaphore(os, Hardware->globalSemaphore));
            globalAcquired = gcvFALSE;
        }
    }

    gckSTATETIMER_Accumulate(&Hardware->powerStateTimer, Hardware->chipPowerState);

    /* Save the new power state. */
    Hardware->chipPowerState = State;

#if gcdDVFS
    if (State == gcvPOWER_ON && Hardware->kernel->dvfs)
    {
        gckDVFS_Start(Hardware->kernel->dvfs);
    }
#endif

#if gcdPOWEROFF_TIMEOUT
    /* Reset power off time */
    gcmkONERROR(gckOS_GetTicks(&currentTime));

    Hardware->powerOffTime = currentTime + Hardware->powerOffTimeout;

    if (State == gcvPOWER_IDLE || State == gcvPOWER_SUSPEND)
    {
        /* Start a timer to power off GPU when GPU enters IDLE or SUSPEND. */
        gcmkVERIFY_OK(gckOS_StartTimer(os,
                                       Hardware->powerOffTimer,
                                       Hardware->powerOffTimeout));
    }
    else
    {
        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE, "Cancel powerOfftimer");

        /* Cancel running timer when GPU enters ON or OFF. */
        gcmkVERIFY_OK(gckOS_StopTimer(os, Hardware->powerOffTimer));
    }
#endif

    /* Release the power mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(os, Hardware->powerMutex));

    /* Get total time. */
    gcmkPROFILE_QUERY(time, totalTime);
#if gcdENABLE_PROFILING
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "PROF(%llu): mutex:%llu on:%llu stall:%llu stop:%llu",
                   freq, mutexTime, onTime, stallTime, stopTime);
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "  delay:%llu init:%llu off:%llu start:%llu total:%llu",
                   delayTime, initTime, offTime, startTime, totalTime);
#endif

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (commandStarted)
    {
        gcmkVERIFY_OK(gckCOMMAND_Stop(command));
    }

    if (isrStarted)
    {
        gcmkVERIFY_OK(Hardware->stopIsr(Hardware->isrContext));
    }

    if (acquired)
    {
        /* Release semaphore. */
        gcmkVERIFY_OK(gckOS_ReleaseSemaphore(Hardware->os,
                                             command->powerSemaphore));
    }

    if (globalAcquired)
    {
        gcmkVERIFY_OK(gckOS_ReleaseSemaphore(Hardware->os,
                                             Hardware->globalSemaphore));
    }

    if (mutexAcquired)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Hardware->os, Hardware->powerMutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_QueryPowerManagementState
**
**  Get GPU power state.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**      gceCHIPPOWERSTATE* State
**          Power State.
**
*/
gceSTATUS
gckHARDWARE_QueryPowerManagementState(
    IN gckHARDWARE Hardware,
    OUT gceCHIPPOWERSTATE* State
    )
{
    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(State != gcvNULL);

    /* Return the statue. */
    *State = Hardware->chipPowerState;

    /* Success. */
    gcmkFOOTER_ARG("*State=%d", *State);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckHARDWARE_SetPowerManagement
**
**  Configure GPU power management function.
**  Only used in driver initialization stage.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**      gctBOOL PowerManagement
**          Power Mangement State.
**
*/
gceSTATUS
gckHARDWARE_SetPowerManagement(
    IN gckHARDWARE Hardware,
    IN gctBOOL PowerManagement
    )
{
    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    gcmkVERIFY_OK(
        gckOS_AcquireMutex(Hardware->os, Hardware->powerMutex, gcvINFINITE));

    Hardware->powerManagement = PowerManagement;

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Hardware->os, Hardware->powerMutex));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckHARDWARE_SetGpuProfiler
**
**  Configure GPU profiler function.
**  Only used in driver initialization stage.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**      gctBOOL GpuProfiler
**          GOU Profiler State.
**
*/
gceSTATUS
gckHARDWARE_SetGpuProfiler(
    IN gckHARDWARE Hardware,
    IN gctBOOL GpuProfiler
    )
{
    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    if (GpuProfiler == gcvTRUE)
    {
        gctUINT32 data = 0;

        /* Need to disable clock gating when doing profiling. */
        gcmkVERIFY_OK(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 Hardware->powerBaseAddress +
                                 0x00100,
                                 &data));

        data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0)));


        gcmkVERIFY_OK(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  Hardware->powerBaseAddress
                                  + 0x00100,
                                  data));
    }

    Hardware->gpuProfiler = GpuProfiler;

    if (GpuProfiler == gcvTRUE)
    {
        Hardware->waitCount = 200 * 100;
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

#if gcdENABLE_FSCALE_VAL_ADJUST
gceSTATUS
gckHARDWARE_SetFscaleValue(
    IN gckHARDWARE Hardware,
    IN gctUINT32   FscaleValue
    )
{
    gceSTATUS status;
    gctUINT32 clock;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Hardware=0x%x FscaleValue=%d", Hardware, FscaleValue);

    gcmkVERIFY_ARGUMENT(FscaleValue > 0 && FscaleValue <= 64);

    gcmkONERROR(
        gckOS_AcquireMutex(Hardware->os, Hardware->powerMutex, gcvINFINITE));
    acquired =  gcvTRUE;

    Hardware->powerOnFscaleVal = FscaleValue;

    if (Hardware->chipPowerState == gcvPOWER_ON)
    {
        gctUINT32 data;

        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 Hardware->powerBaseAddress
                                 + 0x00104,
                                 &data));

        /* Disable all clock gating. */
        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  Hardware->powerBaseAddress
                                  + 0x00104,
                                  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0)))
                                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 1:1) - (0 ?
 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1)))
                                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ?
 2:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 2:2) - (0 ?
 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ?
 2:2)))
                                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ?
 3:3))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 3:3) - (0 ?
 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ?
 3:3)))
                                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ?
 4:4))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 4:4) - (0 ?
 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ?
 4:4)))
                                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ?
 5:5))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 5:5) - (0 ?
 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ?
 5:5)))
                                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ?
 6:6))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 6:6) - (0 ?
 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ?
 6:6)))
                                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ?
 7:7))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 7:7) - (0 ?
 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ?
 7:7)))
                                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 8:8) - (0 ? 8:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:8) - (0 ? 8:8) + 1))))))) << (0 ?
 8:8))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 8:8) - (0 ?
 8:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:8) - (0 ? 8:8) + 1))))))) << (0 ?
 8:8)))
                                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 9:9) - (0 ?
 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9)))
                                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 11:11) - (0 ? 11:11) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:11) - (0 ? 11:11) + 1))))))) << (0 ?
 11:11))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 11:11) - (0 ?
 11:11) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:11) - (0 ? 11:11) + 1))))))) << (0 ?
 11:11)))));

        clock = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0)))
              | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 1:1) - (0 ?
 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1)))
              | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ?
 8:2))) | (((gctUINT32) ((gctUINT32) (FscaleValue) & ((gctUINT32) ((((1 ?
 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ?
 8:2)))
              | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 9:9) - (0 ?
 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9)));

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                          Hardware->core,
                                          0x00000,
                                          clock));

        /* Done loading the frequency scaler. */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                          Hardware->core,
                                          0x00000,
                                          ((((gctUINT32) (clock)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 9:9) - (0 ?
 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9)))));

        /* Restore all clock gating. */
        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  Hardware->powerBaseAddress
                                  + 0x00104,
                                  data));
    }

    gcmkVERIFY(gckOS_ReleaseMutex(Hardware->os, Hardware->powerMutex));

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        gcmkVERIFY(gckOS_ReleaseMutex(Hardware->os, Hardware->powerMutex));
    }

    gcmkFOOTER();
    return status;
}

gceSTATUS
gckHARDWARE_GetFscaleValue(
    IN gckHARDWARE Hardware,
    IN gctUINT * FscaleValue,
    IN gctUINT * MinFscaleValue,
    IN gctUINT * MaxFscaleValue
    )
{
    *FscaleValue = Hardware->powerOnFscaleVal;
    *MinFscaleValue = Hardware->minFscaleValue;
    *MaxFscaleValue = 64;

    return gcvSTATUS_OK;
}

gceSTATUS
gckHARDWARE_SetMinFscaleValue(
    IN gckHARDWARE Hardware,
    IN gctUINT MinFscaleValue
    )
{
    if (MinFscaleValue >= 1 && MinFscaleValue <= 64)
    {
        Hardware->minFscaleValue = MinFscaleValue;
    }

    return gcvSTATUS_OK;
}
#endif

#if gcdPOWEROFF_TIMEOUT
gceSTATUS
gckHARDWARE_SetPowerOffTimeout(
    IN gckHARDWARE  Hardware,
    IN gctUINT32    Timeout
)
{
    gcmkHEADER_ARG("Hardware=0x%x Timeout=%d", Hardware, Timeout);

    Hardware->powerOffTimeout = Timeout;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}


gceSTATUS
gckHARDWARE_QueryPowerOffTimeout(
    IN gckHARDWARE  Hardware,
    OUT gctUINT32*  Timeout
)
{
    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    *Timeout = Hardware->powerOffTimeout;

    gcmkFOOTER_ARG("*Timeout=%d", *Timeout);
    return gcvSTATUS_OK;
}
#endif

gceSTATUS
gckHARDWARE_QueryIdle(
    IN gckHARDWARE Hardware,
    OUT gctBOOL_PTR IsIdle
    )
{
    gceSTATUS status;
    gctUINT32 idle, address;
    gctBOOL   isIdle;

#if gcdINTERRUPT_STATISTIC
    gctINT32 pendingInterrupt;
#endif

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(IsIdle != gcvNULL);

    /* We are idle when the power is not ON. */
    if (Hardware->chipPowerState != gcvPOWER_ON)
    {
        isIdle = gcvTRUE;
    }

    else
    {
        /* Read idle register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00004, &idle));

        /* Pipe must be idle. */
        if (((((((gctUINT32) (idle)) >> (0 ? 1:1)) & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1)))))) ) != 1)
        ||  ((((((gctUINT32) (idle)) >> (0 ? 3:3)) & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1)))))) ) != 1)
        ||  ((((((gctUINT32) (idle)) >> (0 ? 4:4)) & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1)))))) ) != 1)
        ||  ((((((gctUINT32) (idle)) >> (0 ? 5:5)) & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1)))))) ) != 1)
        ||  ((((((gctUINT32) (idle)) >> (0 ? 6:6)) & ((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1)))))) ) != 1)
        ||  ((((((gctUINT32) (idle)) >> (0 ? 7:7)) & ((gctUINT32) ((((1 ? 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1)))))) ) != 1)
        ||  ((((((gctUINT32) (idle)) >> (0 ? 2:2)) & ((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1)))))) ) != 1)
        )
        {
            /* Something is busy. */
            isIdle = gcvFALSE;
        }

        else
        {
#if gcdSECURITY
            isIdle = gcvTRUE;
            address = 0;
#else
            /* Read the current FE address. */
            gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                             Hardware->core,
                                             0x00664,
                                             &address));

            gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                             Hardware->core,
                                             0x00664,
                                             &address));

            /* Test if address is inside the last WAIT/LINK sequence. */
            if ((address >= Hardware->lastWaitLink)
            &&  (address <= Hardware->lastWaitLink + 16)
            )
            {
                /* FE is in last WAIT/LINK and the pipe is idle. */
                isIdle = gcvTRUE;
            }
            else
            {
                /* FE is not in WAIT/LINK yet. */
                isIdle = gcvFALSE;
            }
#endif
        }
    }

#if gcdINTERRUPT_STATISTIC
    gcmkONERROR(gckOS_AtomGet(
        Hardware->os,
        Hardware->kernel->eventObj->interruptCount,
        &pendingInterrupt
        ));

    if (pendingInterrupt)
    {
        isIdle = gcvFALSE;
    }
#endif

    *IsIdle = isIdle;

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
** Handy macros that will help in reading those debug registers.
*/

#define gcmkREAD_DEBUG_REGISTER(control, block, index, data) \
    gcmkONERROR(\
        gckOS_WriteRegisterEx(Hardware->os, \
                              Hardware->core, \
                              GC_DEBUG_CONTROL##control##_Address, \
                              gcmSETFIELD(0, \
                                          GC_DEBUG_CONTROL##control, \
                                          block, \
                                          index))); \
    gcmkONERROR(\
        gckOS_ReadRegisterEx(Hardware->os, \
                             Hardware->core, \
                             GC_DEBUG_SIGNALS_##block##_Address, \
                             &profiler->data))

#if USE_SW_RESET
#define gcmkREAD_DEBUG_REGISTER_Q(control, block, index, data) \
    gcmkREAD_DEBUG_REGISTER(control, block, index, data); \
    tempCounterValue = profiler->data; \
    profiler->data = CalcDelta(profiler->data, profilerHistory.data); \
    profilerHistory.data = tempCounterValue

#else
#define gcmkREAD_DEBUG_REGISTER_Q(control, block, index, data) \
    gcmkREAD_DEBUG_REGISTER(control, block, index, data)

#endif

#define gcmkREAD_DEBUG_REGISTER_N(control, block, index, data) \
    gcmkONERROR(\
        gckOS_WriteRegisterEx(Hardware->os, \
                              Hardware->core, \
                              GC_DEBUG_CONTROL##control##_Address, \
                              gcmSETFIELD(0, \
                                          GC_DEBUG_CONTROL##control, \
                                          block, \
                                          index))); \
    gcmkONERROR(\
        gckOS_ReadRegisterEx(Hardware->os, \
                             Hardware->core, \
                             GC_DEBUG_SIGNALS_##block##_Address, \
                             &data))

#define gcmkRESET_DEBUG_REGISTER(control, block, value) \
    gcmkONERROR(\
        gckOS_WriteRegisterEx(Hardware->os, \
                              Hardware->core, \
                              GC_DEBUG_CONTROL##control##_Address, \
                              gcmSETFIELD(0, \
                                          GC_DEBUG_CONTROL##control, \
                                          block, \
                                          value))); \
    gcmkONERROR(\
        gckOS_WriteRegisterEx(Hardware->os, \
                              Hardware->core, \
                              GC_DEBUG_CONTROL##control##_Address, \
                              gcmSETFIELD(0, \
                                          GC_DEBUG_CONTROL##control, \
                                          block, \
                                          0)))


/*******************************************************************************
**
**  gckHARDWARE_ProfileEngine2D
**
**  Read the profile registers available in the 2D engine and sets them in the
**  profile.  The function will also reset the pixelsRendered counter every time.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      OPTIONAL gcs2D_PROFILE_PTR Profile
**          Pointer to a gcs2D_Profile structure.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_ProfileEngine2D(
    IN gckHARDWARE Hardware,
    OPTIONAL gcs2D_PROFILE_PTR Profile
    )
{
    gceSTATUS status;
    gcs2D_PROFILE_PTR profiler = Profile;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    if (Profile != gcvNULL)
    {
        /* Read the cycle count. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 0x00438,
                                 &Profile->cycleCount));

        /* Read pixels rendered by 2D engine. */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (11) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454, &profiler->pixelsRendered));

        /* Reset counter. */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (15) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16)))
));
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

#if VIVANTE_PROFILER

#if !VIVANTE_PROFILER_ALL_COUNTER
static gctUINT32
CalcDelta(
    IN gctUINT32 new,
    IN gctUINT32 old
    )
{
    if (new >= old)
    {
        return new - old;
    }
    else
    {
        return (gctUINT32)((gctUINT64)new + 0x100000000ll - old);
    }
}
#endif

#if USE_SW_RESET
#define gcmkRESET_PROFILE_DATA(counterName, preCounters) \
    temp = profiler->counterName; \
    profiler->counterName = CalcDelta(temp, Context->preCounters.counterName); \
    Context->preCounters.counterName = temp

#endif

gceSTATUS
gckHARDWARE_QueryProfileRegisters(
    IN gckHARDWARE Hardware,
    IN gctBOOL Reset,
    OUT gcsPROFILER_COUNTERS * Counters
    )
{
    gceSTATUS status;
    gcsPROFILER_COUNTERS * profiler = Counters;
    gctUINT i, clock;
    gctUINT32 colorKilled, colorDrawn, depthKilled, depthDrawn;
    gctUINT32 totalRead, totalWrite;
    gceCHIPMODEL chipModel;
    gctUINT32 chipRevision;
    gctUINT32 resetValue = 0xF;
    gctBOOL hasNewCounters = gcvFALSE;
    gctUINT32 mc_axi_max_min_latency;
    static gcsPROFILER_COUNTERS profilerHistory;
    static gctBOOL isFirstFrame = gcvTRUE;
    gctUINT32 tempCounterValue;
    gctUINT32 totalColorKilled = 0;
    gctUINT32 totalDepthKilled = 0;
    gctUINT32 totalColorDrawn = 0;
    gctUINT32 totalDepthDrawn = 0;

    gcmkHEADER_ARG("Hardware=0x%x Counters=0x%x", Hardware, Counters);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    if (isFirstFrame)
    {
        gckOS_ZeroMemory(&profilerHistory, sizeof(gcsPROFILER_COUNTERS));
        isFirstFrame = gcvFALSE;
    }

    chipModel = Hardware->identity.chipModel;
    chipRevision = Hardware->identity.chipRevision;
    if ((chipModel == gcv5000 && chipRevision == 0x5434) || (chipModel == gcv3000 && chipRevision == 0x5435))
    {
        resetValue = 0xFF;
        hasNewCounters = gcvTRUE;
    }

    /* Read the counters. */
    gcmkONERROR(
        gckOS_ReadRegisterEx(Hardware->os,
                             Hardware->core,
                             0x00438,
                             &profiler->gpuCyclesCounter));

    /* This counter should be equal to 0x00438. Currently it's not displayed in vAnalyzer */
    gcmkONERROR(
        gckOS_ReadRegisterEx(Hardware->os,
                             Hardware->core,
                             0x00078,
                             &profiler->gpuTotalCyclesCounter));

    if (chipModel == gcv2100 || chipModel == gcv2000 || chipModel == gcv880)
    {
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 0x00078,
                                 &profiler->gpuIdleCyclesCounter));
    }
    else
    {
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 0x0007C,
                                 &profiler->gpuIdleCyclesCounter));
    }

    /* Read clock control register. */
    gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x00000,
                                     &clock));

    profiler->gpuTotalRead64BytesPerFrame = 0;
    profiler->gpuTotalWrite64BytesPerFrame = 0;
    profiler->pe_pixel_count_killed_by_color_pipe = 0;
    profiler->pe_pixel_count_killed_by_depth_pipe = 0;
    profiler->pe_pixel_count_drawn_by_color_pipe = 0;
    profiler->pe_pixel_count_drawn_by_depth_pipe = 0;

    /* Walk through all avaiable pixel pipes. */
    for (i = 0; i < Hardware->identity.pixelPipes; ++i)
    {
        /* Select proper pipe. */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                          Hardware->core,
                                          0x00000,
                                          ((((gctUINT32) (clock)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:20) - (0 ? 23:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:20) - (0 ? 23:20) + 1))))))) << (0 ?
 23:20))) | (((gctUINT32) ((gctUINT32) (i) & ((gctUINT32) ((((1 ? 23:20) - (0 ?
 23:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:20) - (0 ? 23:20) + 1))))))) << (0 ?
 23:20)))));

        /* BW */
        gcmkONERROR(
        gckOS_ReadRegisterEx(Hardware->os,
                             Hardware->core,
                             0x00040,
                             &totalRead));
        gcmkONERROR(
        gckOS_ReadRegisterEx(Hardware->os,
                             Hardware->core,
                             0x00044,
                             &totalWrite));

        profiler->gpuTotalRead64BytesPerFrame += totalRead;
        profiler->gpuTotalWrite64BytesPerFrame += totalWrite;

        /* PE */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470, ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16)))));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454,
 &colorKilled));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470, ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16)))));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454,
 &depthKilled));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470, ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16)))));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454,
 &colorDrawn));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470, ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16)))));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454,
 &depthDrawn));

#if USE_SW_RESET
        totalColorKilled += colorKilled;
        totalDepthKilled += depthKilled;
        totalColorDrawn += colorDrawn;
        totalDepthDrawn += depthDrawn;
#else
        profiler->pe_pixel_count_killed_by_color_pipe += colorKilled;
        profiler->pe_pixel_count_killed_by_depth_pipe += depthKilled;
        profiler->pe_pixel_count_drawn_by_color_pipe += colorDrawn;
        profiler->pe_pixel_count_drawn_by_depth_pipe += depthDrawn;
#endif
    }

#if USE_SW_RESET
    profiler->pe_pixel_count_killed_by_color_pipe = CalcDelta(totalColorKilled, profilerHistory.pe_pixel_count_killed_by_color_pipe);
    profiler->pe_pixel_count_killed_by_depth_pipe = CalcDelta(totalDepthKilled, profilerHistory.pe_pixel_count_killed_by_depth_pipe);
    profiler->pe_pixel_count_drawn_by_color_pipe = CalcDelta(totalColorDrawn, profilerHistory.pe_pixel_count_drawn_by_color_pipe);
    profiler->pe_pixel_count_drawn_by_depth_pipe = CalcDelta(totalDepthDrawn, profilerHistory.pe_pixel_count_drawn_by_depth_pipe);
    profilerHistory.pe_pixel_count_killed_by_color_pipe = totalColorKilled;
    profilerHistory.pe_pixel_count_killed_by_depth_pipe = totalDepthKilled;
    profilerHistory.pe_pixel_count_drawn_by_color_pipe = totalColorDrawn;
    profilerHistory.pe_pixel_count_drawn_by_depth_pipe = totalDepthDrawn;
#endif

    /* Reset clock control register. */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00000,
                                      clock));

    /* Reset counters. */
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x0003C, 1));
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x0003C, 0));
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00438, 0));
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00078, 0));
#if !USE_SW_RESET
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16)))
));
#endif

    /* SH */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (7) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->ps_inst_counter));  tempCounterValue = profiler->ps_inst_counter;
  profiler->ps_inst_counter = CalcDelta(profiler->ps_inst_counter, profilerHistory.ps_inst_counter);
  profilerHistory.ps_inst_counter = tempCounterValue;

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (8) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->rendered_pixel_counter));  tempCounterValue = profiler->rendered_pixel_counter;
  profiler->rendered_pixel_counter = CalcDelta(profiler->rendered_pixel_counter,
 profilerHistory.rendered_pixel_counter);  profilerHistory.rendered_pixel_counter = tempCounterValue;

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (9) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->vs_inst_counter));  tempCounterValue = profiler->vs_inst_counter;
  profiler->vs_inst_counter = CalcDelta(profiler->vs_inst_counter, profilerHistory.vs_inst_counter);
  profilerHistory.vs_inst_counter = tempCounterValue;

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (10) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->rendered_vertice_counter));  tempCounterValue = profiler->rendered_vertice_counter;
  profiler->rendered_vertice_counter = CalcDelta(profiler->rendered_vertice_counter,
 profilerHistory.rendered_vertice_counter);  profilerHistory.rendered_vertice_counter = tempCounterValue;

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (11) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->vtx_branch_inst_counter));  tempCounterValue = profiler->vtx_branch_inst_counter;
  profiler->vtx_branch_inst_counter = CalcDelta(profiler->vtx_branch_inst_counter,
 profilerHistory.vtx_branch_inst_counter);  profilerHistory.vtx_branch_inst_counter = tempCounterValue;

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (12) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->vtx_texld_inst_counter));  tempCounterValue = profiler->vtx_texld_inst_counter;
  profiler->vtx_texld_inst_counter = CalcDelta(profiler->vtx_texld_inst_counter,
 profilerHistory.vtx_texld_inst_counter);  profilerHistory.vtx_texld_inst_counter = tempCounterValue;

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (13) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->pxl_branch_inst_counter));  tempCounterValue = profiler->pxl_branch_inst_counter;
  profiler->pxl_branch_inst_counter = CalcDelta(profiler->pxl_branch_inst_counter,
 profilerHistory.pxl_branch_inst_counter);  profilerHistory.pxl_branch_inst_counter = tempCounterValue;

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (14) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->pxl_texld_inst_counter));  tempCounterValue = profiler->pxl_texld_inst_counter;
  profiler->pxl_texld_inst_counter = CalcDelta(profiler->pxl_texld_inst_counter,
 profilerHistory.pxl_texld_inst_counter);  profilerHistory.pxl_texld_inst_counter = tempCounterValue;

    if (hasNewCounters)
    {
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (19) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->vs_non_idle_starve_count));  tempCounterValue = profiler->vs_non_idle_starve_count;
  profiler->vs_non_idle_starve_count = CalcDelta(profiler->vs_non_idle_starve_count,
 profilerHistory.vs_non_idle_starve_count);  profilerHistory.vs_non_idle_starve_count = tempCounterValue;

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (15) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->vs_starve_count));  tempCounterValue = profiler->vs_starve_count;
  profiler->vs_starve_count = CalcDelta(profiler->vs_starve_count, profilerHistory.vs_starve_count);
  profilerHistory.vs_starve_count = tempCounterValue;

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (16) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->vs_stall_count));  tempCounterValue = profiler->vs_stall_count;
  profiler->vs_stall_count = CalcDelta(profiler->vs_stall_count, profilerHistory.vs_stall_count);
  profilerHistory.vs_stall_count = tempCounterValue;

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (21) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->vs_process_count));  tempCounterValue = profiler->vs_process_count;
  profiler->vs_process_count = CalcDelta(profiler->vs_process_count, profilerHistory.vs_process_count);
  profilerHistory.vs_process_count = tempCounterValue;

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (20) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->ps_non_idle_starve_count));  tempCounterValue = profiler->ps_non_idle_starve_count;
  profiler->ps_non_idle_starve_count = CalcDelta(profiler->ps_non_idle_starve_count,
 profilerHistory.ps_non_idle_starve_count);  profilerHistory.ps_non_idle_starve_count = tempCounterValue;

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (17) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->ps_starve_count));  tempCounterValue = profiler->ps_starve_count;
  profiler->ps_starve_count = CalcDelta(profiler->ps_starve_count, profilerHistory.ps_starve_count);
  profilerHistory.ps_starve_count = tempCounterValue;

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (18) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->ps_stall_count));  tempCounterValue = profiler->ps_stall_count;
  profiler->ps_stall_count = CalcDelta(profiler->ps_stall_count, profilerHistory.ps_stall_count);
  profilerHistory.ps_stall_count = tempCounterValue;

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (22) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->ps_process_count));  tempCounterValue = profiler->ps_process_count;
  profiler->ps_process_count = CalcDelta(profiler->ps_process_count, profilerHistory.ps_process_count);
  profilerHistory.ps_process_count = tempCounterValue;

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (4) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->shader_cycle_count));  tempCounterValue = profiler->shader_cycle_count;
  profiler->shader_cycle_count = CalcDelta(profiler->shader_cycle_count,
 profilerHistory.shader_cycle_count);  profilerHistory.shader_cycle_count = tempCounterValue;

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (23) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->tx_non_idle_starve_count));  tempCounterValue = profiler->tx_non_idle_starve_count;
  profiler->tx_non_idle_starve_count = CalcDelta(profiler->tx_non_idle_starve_count,
 profilerHistory.tx_non_idle_starve_count);  profilerHistory.tx_non_idle_starve_count = tempCounterValue;

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (24) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->tx_starve_count));  tempCounterValue = profiler->tx_starve_count;
  profiler->tx_starve_count = CalcDelta(profiler->tx_starve_count, profilerHistory.tx_starve_count);
  profilerHistory.tx_starve_count = tempCounterValue;

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (25) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->tx_stall_count));  tempCounterValue = profiler->tx_stall_count;
  profiler->tx_stall_count = CalcDelta(profiler->tx_stall_count, profilerHistory.tx_stall_count);
  profilerHistory.tx_stall_count = tempCounterValue;

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (26) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C,
 &profiler->tx_process_count));  tempCounterValue = profiler->tx_process_count;
  profiler->tx_process_count = CalcDelta(profiler->tx_process_count, profilerHistory.tx_process_count);
  profilerHistory.tx_process_count = tempCounterValue;

    }
#if !USE_SW_RESET
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24)))
));
#endif

    /* PA */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460,
 &profiler->pa_input_vtx_counter));  tempCounterValue = profiler->pa_input_vtx_counter;
  profiler->pa_input_vtx_counter = CalcDelta(profiler->pa_input_vtx_counter,
 profilerHistory.pa_input_vtx_counter);  profilerHistory.pa_input_vtx_counter = tempCounterValue;

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (4) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460,
 &profiler->pa_input_prim_counter));  tempCounterValue = profiler->pa_input_prim_counter;
  profiler->pa_input_prim_counter = CalcDelta(profiler->pa_input_prim_counter,
 profilerHistory.pa_input_prim_counter);  profilerHistory.pa_input_prim_counter = tempCounterValue;

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (5) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460,
 &profiler->pa_output_prim_counter));  tempCounterValue = profiler->pa_output_prim_counter;
  profiler->pa_output_prim_counter = CalcDelta(profiler->pa_output_prim_counter,
 profilerHistory.pa_output_prim_counter);  profilerHistory.pa_output_prim_counter = tempCounterValue;

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (6) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460,
 &profiler->pa_depth_clipped_counter));  tempCounterValue = profiler->pa_depth_clipped_counter;
  profiler->pa_depth_clipped_counter = CalcDelta(profiler->pa_depth_clipped_counter,
 profilerHistory.pa_depth_clipped_counter);  profilerHistory.pa_depth_clipped_counter = tempCounterValue;

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (7) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460,
 &profiler->pa_trivial_rejected_counter));  tempCounterValue = profiler->pa_trivial_rejected_counter;
  profiler->pa_trivial_rejected_counter = CalcDelta(profiler->pa_trivial_rejected_counter,
 profilerHistory.pa_trivial_rejected_counter);  profilerHistory.pa_trivial_rejected_counter = tempCounterValue;

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (8) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460,
 &profiler->pa_culled_counter));  tempCounterValue = profiler->pa_culled_counter;
  profiler->pa_culled_counter = CalcDelta(profiler->pa_culled_counter, profilerHistory.pa_culled_counter);
  profilerHistory.pa_culled_counter = tempCounterValue;

    if (hasNewCounters)
    {
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (13) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460,
 &profiler->pa_starve_count));  tempCounterValue = profiler->pa_starve_count;
  profiler->pa_starve_count = CalcDelta(profiler->pa_starve_count, profilerHistory.pa_starve_count);
  profilerHistory.pa_starve_count = tempCounterValue;

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (14) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460,
 &profiler->pa_stall_count));  tempCounterValue = profiler->pa_stall_count;
  profiler->pa_stall_count = CalcDelta(profiler->pa_stall_count, profilerHistory.pa_stall_count);
  profilerHistory.pa_stall_count = tempCounterValue;

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (15) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460,
 &profiler->pa_process_count));  tempCounterValue = profiler->pa_process_count;
  profiler->pa_process_count = CalcDelta(profiler->pa_process_count, profilerHistory.pa_process_count);
  profilerHistory.pa_process_count = tempCounterValue;

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (12) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460,
 &profiler->pa_non_idle_starve_count));  tempCounterValue = profiler->pa_non_idle_starve_count;
  profiler->pa_non_idle_starve_count = CalcDelta(profiler->pa_non_idle_starve_count,
 profilerHistory.pa_non_idle_starve_count);  profilerHistory.pa_non_idle_starve_count = tempCounterValue;

    }
#if !USE_SW_RESET
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0)))
));
#endif

    /* SE */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_culled_triangle_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_culled_lines_count));
    if (hasNewCounters)
    {
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (8) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_starve_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (9) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_stall_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (10) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_receive_triangle_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (11) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_send_triangle_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (12) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_receive_lines_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (13) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_send_lines_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (19) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_process_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (20) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_non_idle_starve_count));
    }
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8)))
));

    /* RA */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448,
 &profiler->ra_valid_pixel_count));  tempCounterValue = profiler->ra_valid_pixel_count;
  profiler->ra_valid_pixel_count = CalcDelta(profiler->ra_valid_pixel_count,
 profilerHistory.ra_valid_pixel_count);  profilerHistory.ra_valid_pixel_count = tempCounterValue;

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448,
 &profiler->ra_total_quad_count));  tempCounterValue = profiler->ra_total_quad_count;
  profiler->ra_total_quad_count = CalcDelta(profiler->ra_total_quad_count,
 profilerHistory.ra_total_quad_count);  profilerHistory.ra_total_quad_count = tempCounterValue;

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448,
 &profiler->ra_valid_quad_count_after_early_z));  tempCounterValue = profiler->ra_valid_quad_count_after_early_z;
  profiler->ra_valid_quad_count_after_early_z = CalcDelta(profiler->ra_valid_quad_count_after_early_z,
 profilerHistory.ra_valid_quad_count_after_early_z);  profilerHistory.ra_valid_quad_count_after_early_z = tempCounterValue;

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448,
 &profiler->ra_total_primitive_count));  tempCounterValue = profiler->ra_total_primitive_count;
  profiler->ra_total_primitive_count = CalcDelta(profiler->ra_total_primitive_count,
 profilerHistory.ra_total_primitive_count);  profilerHistory.ra_total_primitive_count = tempCounterValue;

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (9) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448,
 &profiler->ra_pipe_cache_miss_counter));  tempCounterValue = profiler->ra_pipe_cache_miss_counter;
  profiler->ra_pipe_cache_miss_counter = CalcDelta(profiler->ra_pipe_cache_miss_counter,
 profilerHistory.ra_pipe_cache_miss_counter);  profilerHistory.ra_pipe_cache_miss_counter = tempCounterValue;

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (10) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448,
 &profiler->ra_prefetch_cache_miss_counter));  tempCounterValue = profiler->ra_prefetch_cache_miss_counter;
  profiler->ra_prefetch_cache_miss_counter = CalcDelta(profiler->ra_prefetch_cache_miss_counter,
 profilerHistory.ra_prefetch_cache_miss_counter);  profilerHistory.ra_prefetch_cache_miss_counter = tempCounterValue;

    if (hasNewCounters)
    {
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (13) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448,
 &profiler->ra_non_idle_starve_count));  tempCounterValue = profiler->ra_non_idle_starve_count;
  profiler->ra_non_idle_starve_count = CalcDelta(profiler->ra_non_idle_starve_count,
 profilerHistory.ra_non_idle_starve_count);  profilerHistory.ra_non_idle_starve_count = tempCounterValue;

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (14) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448,
 &profiler->ra_starve_count));  tempCounterValue = profiler->ra_starve_count;
  profiler->ra_starve_count = CalcDelta(profiler->ra_starve_count, profilerHistory.ra_starve_count);
  profilerHistory.ra_starve_count = tempCounterValue;

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (15) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448,
 &profiler->ra_stall_count));  tempCounterValue = profiler->ra_stall_count;
  profiler->ra_stall_count = CalcDelta(profiler->ra_stall_count, profilerHistory.ra_stall_count);
  profilerHistory.ra_stall_count = tempCounterValue;

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (16) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448,
 &profiler->ra_process_count));  tempCounterValue = profiler->ra_process_count;
  profiler->ra_process_count = CalcDelta(profiler->ra_process_count, profilerHistory.ra_process_count);
  profilerHistory.ra_process_count = tempCounterValue;

    }
#if !USE_SW_RESET
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16)))
));
#endif

    /* TX */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_total_bilinear_requests));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_total_trilinear_requests));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_total_discarded_texture_requests));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_total_texture_requests));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (5) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_mem_read_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (6) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_mem_read_in_8B_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (7) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_cache_miss_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (8) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_cache_hit_texel_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (9) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_cache_miss_texel_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24)))
));

    /* MC */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00468, &profiler->mc_total_read_req_8B_from_pipeline));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00468, &profiler->mc_total_read_req_8B_from_IP));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00468, &profiler->mc_total_write_req_8B_from_pipeline));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0)))
));

    /* HI */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0046C, &profiler->hi_axi_cycles_read_request_stalled));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0046C, &profiler->hi_axi_cycles_write_request_stalled));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0046C, &profiler->hi_axi_cycles_write_data_stalled));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8)))
));

    if (hasNewCounters)
    {
        /* latency */
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x0056C,
                                     &mc_axi_max_min_latency));

        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x00570,
                                     &profiler->mc_axi_total_latency));

        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x00574,
                                     &profiler->mc_axi_sample_count));

        /* Reset Latency counters */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00568,
                                      0x10a));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00568,
                                      0xa));

        profiler->mc_axi_min_latency = (mc_axi_max_min_latency & 0x0fff0000) >> 16;
        profiler->mc_axi_max_latency = (mc_axi_max_min_latency & 0x00000fff);
        if (profiler->mc_axi_min_latency == 4095)
            profiler->mc_axi_min_latency = 0;

        /* FE */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (10) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00450, &profiler->fe_draw_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (11) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00450, &profiler->fe_out_vertex_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (14) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00450, &profiler->fe_stall_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (15) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00450, &profiler->fe_starve_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0)))
));
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}
#endif


#if VIVANTE_PROFILER_CONTEXT
#define gcmkUPDATE_PROFILE_DATA(data) \
    profilerHistroy->data += profiler->data

gceSTATUS
gckHARDWARE_QueryContextProfile(
    IN gckHARDWARE Hardware,
    IN gctBOOL Reset,
    IN gckCONTEXT Context,
    OUT gcsPROFILER_COUNTERS * Counters
    )
{
    gceSTATUS status;
    gckCOMMAND command = Hardware->kernel->command;
    gcsPROFILER_COUNTERS * profiler = Counters;

    gcmkHEADER_ARG("Hardware=0x%x Counters=0x%x", Hardware, Counters);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Acquire the context sequnence mutex. */
    gcmkONERROR(gckOS_AcquireMutex(
        command->os, command->mutexContextSeq, gcvINFINITE
        ));

    /* Read the counters. */
    gcmkVERIFY_OK(gckOS_MemCopy(
        profiler, &Context->histroyProfiler, gcmSIZEOF(gcsPROFILER_COUNTERS)
        ));

#if VIVANTE_PROFILER_ALL_COUNTER
    {
        gctUINT32 tid, i;
        static gctUINT32 frameNum = 0;
        gcmkONERROR(gckOS_GetThreadID(&tid));
        gcmkPRINT("TID: %d: Frame #%d\n", tid, frameNum);

        gcmkPRINT("TID: %d; GPU cycles: cycle: %u, total: %u, idle: %u\n", tid,
                  profiler->gpuCyclesCounter,
                  profiler->gpuTotalCyclesCounter,
                  profiler->gpuIdleCyclesCounter);

        gcmkPRINT("TID: %d; BW: read: %u, write: %u\n", tid,
                  profiler->gpuTotalRead64BytesPerFrame,
                  profiler->gpuTotalWrite64BytesPerFrame);

        gcmkPRINT("TID: %d; Latency: min: %u, max: %u, total: %u, sample count: %u\n", tid,
                  profiler->mc_axi_min_latency,
                  profiler->mc_axi_max_latency,
                  profiler->mc_axi_total_latency,
                  profiler->mc_axi_sample_count);

        for (i = 0; i < MODULE_FRONT_END_COUNTER_NUM; i++)
        {
            gcmkPRINT("TID: %d; FE counter #%d: %u\n", tid,i,profiler->feCounters[i]);
        }
        for (i = 0; i < MODULE_PRIMITIVE_ASSEMBLY_COUNTER_NUM; i++)
        {
            gcmkPRINT("TID: %d; PA counter #%d: %u\n", tid,i,profiler->paCounters[i]);
        }
        for (i = 0; i < MODULE_SHADER_COUNTER_NUM; i++)
        {
            gcmkPRINT("TID: %d; SH counter #%d: %u\n", tid,i,profiler->shCounters[i]);
        }
        for (i = 0; i < MODULE_SETUP_COUNTER_NUM; i++)
        {
            gcmkPRINT("TID: %d; SE counter #%d: %u\n", tid,i,profiler->seCounters[i]);
        }
                for (i = 0; i < MODULE_FRONT_END_COUNTER_NUM; i++)
        {
            gcmkPRINT("TID: %d; RA counter #%d: %u\n", tid,i,profiler->raCounters[i]);
        }
        for (i = 0; i < MODULE_PRIMITIVE_ASSEMBLY_COUNTER_NUM; i++)
        {
            gcmkPRINT("TID: %d; PE counter #%d: %u\n", tid,i,profiler->peCounters[i]);
        }
        for (i = 0; i < MODULE_TEXTURE_COUNTER_NUM; i++)
        {
            gcmkPRINT("TID: %d; TX counter #%d: %u\n", tid,i,profiler->txCounters[i]);
        }
        for (i = 0; i < MODULE_SHADER_COUNTER_NUM; i++)
        {
            gcmkPRINT("TID: %d; MC counter #%d: %u\n", tid,i,profiler->mcCounters[i]);
        }
        for (i = 0; i < MODULE_SETUP_COUNTER_NUM; i++)
        {
            gcmkPRINT("TID: %d; HI counter #%d: %u\n", tid,i,profiler->hiCounters[i]);
        }
        frameNum++;
    }
#endif

    /* Reset counters. */
    if (Reset)
    {
        gcmkVERIFY_OK(gckOS_ZeroMemory(
            &Context->histroyProfiler, gcmSIZEOF(gcsPROFILER_COUNTERS)
            ));
    }

    gcmkVERIFY_OK(gckOS_ReleaseMutex(
        command->os, command->mutexContextSeq
        ));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckHARDWARE_UpdateContextProfile(
    IN gckHARDWARE Hardware,
    IN gckCONTEXT Context
    )
{
    gceSTATUS status;
    gcsPROFILER_COUNTERS * profiler = &Context->latestProfiler;
    gcsPROFILER_COUNTERS * profilerHistroy = &Context->histroyProfiler;
    gceCHIPMODEL chipModel;
    gctUINT32 chipRevision;
    gctUINT32 i;
    gctUINT32 resetValue = 0xF;
    gctBOOL hasNewCounters = gcvFALSE;

    gcmkHEADER_ARG("Hardware=0x%x Context=0x%x", Hardware, Context);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_OBJECT(Context, gcvOBJ_CONTEXT);

    chipModel = Hardware->identity.chipModel;
    chipRevision = Hardware->identity.chipRevision;
    if ((chipModel == gcv5000 && chipRevision == 0x5434) || (chipModel == gcv3000 && chipRevision == 0x5435))
    {
        resetValue = 0xFF;
        hasNewCounters = gcvTRUE;
    }

    /* Read the counters. */
    gcmkONERROR(
        gckOS_ReadRegisterEx(Hardware->os,
                             Hardware->core,
                             0x00438,
                             &profiler->gpuCyclesCounter));
    gcmkUPDATE_PROFILE_DATA(gpuCyclesCounter);

    /* This counter should be equal to 0x00438. Currently it's not displayed in vAnalyzer */
    gcmkONERROR(
        gckOS_ReadRegisterEx(Hardware->os,
                             Hardware->core,
                             0x00078,
                             &profiler->gpuTotalCyclesCounter));
    gcmkUPDATE_PROFILE_DATA(gpuTotalCyclesCounter);

    if (chipModel == gcv2100 || chipModel == gcv2000 || chipModel == gcv880)
    {
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 0x00078,
                                 &profiler->gpuIdleCyclesCounter));
    }
    else
    {
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 0x0007C,
                                 &profiler->gpuIdleCyclesCounter));
    }
    gcmkUPDATE_PROFILE_DATA(gpuIdleCyclesCounter);

#if !VIVANTE_PROFILER_ALL_COUNTER
    {
    gctUINT clock;
    gctUINT32 colorKilled = 0, colorDrawn = 0, depthKilled = 0, depthDrawn = 0;
    gctUINT32 totalRead, totalWrite;
    gctUINT32 mc_axi_max_min_latency;

    gctUINT32 temp;

    /* Read clock control register. */
    gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x00000,
                                     &clock));

    profiler->gpuTotalRead64BytesPerFrame = 0;
    profiler->gpuTotalWrite64BytesPerFrame = 0;
    profiler->pe_pixel_count_killed_by_color_pipe = 0;
    profiler->pe_pixel_count_killed_by_depth_pipe = 0;
    profiler->pe_pixel_count_drawn_by_color_pipe = 0;
    profiler->pe_pixel_count_drawn_by_depth_pipe = 0;

    /* Walk through all avaiable pixel pipes. */
    for (i = 0; i < Hardware->identity.pixelPipes; ++i)
    {
        /* Select proper pipe. */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                           Hardware->core,
                                           0x00000,
                                           ((((gctUINT32) (clock)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:20) - (0 ? 23:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:20) - (0 ? 23:20) + 1))))))) << (0 ?
 23:20))) | (((gctUINT32) ((gctUINT32) (i) & ((gctUINT32) ((((1 ? 23:20) - (0 ?
 23:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:20) - (0 ? 23:20) + 1))))))) << (0 ?
 23:20)))));

        /* BW */
        gcmkONERROR(
        gckOS_ReadRegisterEx(Hardware->os,
                             Hardware->core,
                             0x00040,
                             &totalRead));
        gcmkONERROR(
        gckOS_ReadRegisterEx(Hardware->os,
                             Hardware->core,
                             0x00044,
                             &totalWrite));

        profiler->gpuTotalRead64BytesPerFrame += totalRead;
        profiler->gpuTotalWrite64BytesPerFrame += totalWrite;

        /* PE */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470, ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16)))));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454,
 &colorKilled));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470, ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16)))));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454,
 &depthKilled));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470, ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16)))));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454,
 &colorDrawn));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470, ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16)))));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454,
 &depthDrawn));

        profiler->pe_pixel_count_killed_by_color_pipe += colorKilled;
        profiler->pe_pixel_count_killed_by_depth_pipe += depthKilled;
        profiler->pe_pixel_count_drawn_by_color_pipe += colorDrawn;
        profiler->pe_pixel_count_drawn_by_depth_pipe += depthDrawn;
    }

    gcmkUPDATE_PROFILE_DATA(gpuTotalRead64BytesPerFrame);
    gcmkUPDATE_PROFILE_DATA(gpuTotalWrite64BytesPerFrame);
#if USE_SW_RESET
    gcmkRESET_PROFILE_DATA(pe_pixel_count_killed_by_color_pipe, preProfiler);
    gcmkRESET_PROFILE_DATA(pe_pixel_count_killed_by_depth_pipe, preProfiler);
    gcmkRESET_PROFILE_DATA(pe_pixel_count_drawn_by_color_pipe, preProfiler);
    gcmkRESET_PROFILE_DATA(pe_pixel_count_drawn_by_depth_pipe, preProfiler);
#endif
    gcmkUPDATE_PROFILE_DATA(pe_pixel_count_killed_by_color_pipe);
    gcmkUPDATE_PROFILE_DATA(pe_pixel_count_killed_by_depth_pipe);
    gcmkUPDATE_PROFILE_DATA(pe_pixel_count_drawn_by_color_pipe);
    gcmkUPDATE_PROFILE_DATA(pe_pixel_count_drawn_by_depth_pipe);

    /* Reset clock control register. */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00000,
                                      clock));

    /* Reset counters. */
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x0003C, 1));
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x0003C, 0));
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00438, 0));
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00078, 0));
#if !USE_SW_RESET
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16)))
));
#endif

    /* SH */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (7) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->ps_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (8) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->rendered_pixel_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (9) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->vs_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (10) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->rendered_vertice_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (11) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->vtx_branch_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (12) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->vtx_texld_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (13) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->pxl_branch_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (14) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->pxl_texld_inst_counter));
    if (hasNewCounters)
    {
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (19) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->vs_non_idle_starve_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (15) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->vs_starve_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (16) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->vs_stall_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (21) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->vs_process_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (20) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->ps_non_idle_starve_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (17) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->ps_starve_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (18) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->ps_stall_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (22) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->ps_process_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (4) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->shader_cycle_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (23) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->tx_non_idle_starve_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (24) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->tx_starve_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (25) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->tx_stall_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (26) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->tx_process_count));
    }
#if USE_SW_RESET
    gcmkRESET_PROFILE_DATA(ps_inst_counter, preProfiler);
    gcmkRESET_PROFILE_DATA(rendered_pixel_counter, preProfiler);
    gcmkRESET_PROFILE_DATA(vs_inst_counter, preProfiler);
    gcmkRESET_PROFILE_DATA(rendered_vertice_counter, preProfiler);
    gcmkRESET_PROFILE_DATA(vtx_branch_inst_counter, preProfiler);
    gcmkRESET_PROFILE_DATA(vtx_texld_inst_counter, preProfiler);
    gcmkRESET_PROFILE_DATA(pxl_branch_inst_counter, preProfiler);
    gcmkRESET_PROFILE_DATA(pxl_texld_inst_counter, preProfiler);
    if (hasNewCounters)
    {
        gcmkRESET_PROFILE_DATA(vs_non_idle_starve_count, preProfiler);
        gcmkRESET_PROFILE_DATA(vs_starve_count, preProfiler);
        gcmkRESET_PROFILE_DATA(vs_stall_count, preProfiler);
        gcmkRESET_PROFILE_DATA(vs_process_count, preProfiler);
        gcmkRESET_PROFILE_DATA(ps_non_idle_starve_count, preProfiler);
        gcmkRESET_PROFILE_DATA(ps_starve_count, preProfiler);
        gcmkRESET_PROFILE_DATA(ps_stall_count, preProfiler);
        gcmkRESET_PROFILE_DATA(ps_process_count, preProfiler);
        gcmkRESET_PROFILE_DATA(shader_cycle_count, preProfiler);
        gcmkRESET_PROFILE_DATA(tx_non_idle_starve_count, preProfiler);
        gcmkRESET_PROFILE_DATA(tx_starve_count, preProfiler);
        gcmkRESET_PROFILE_DATA(tx_stall_count, preProfiler);
        gcmkRESET_PROFILE_DATA(tx_process_count, preProfiler);
    }
#endif
    gcmkUPDATE_PROFILE_DATA(ps_inst_counter);
    gcmkUPDATE_PROFILE_DATA(rendered_pixel_counter);
    gcmkUPDATE_PROFILE_DATA(vs_inst_counter);
    gcmkUPDATE_PROFILE_DATA(rendered_vertice_counter);
    gcmkUPDATE_PROFILE_DATA(vtx_branch_inst_counter);
    gcmkUPDATE_PROFILE_DATA(vtx_texld_inst_counter);
    gcmkUPDATE_PROFILE_DATA(pxl_branch_inst_counter);
    gcmkUPDATE_PROFILE_DATA(pxl_texld_inst_counter);
    if (hasNewCounters)
    {
        gcmkUPDATE_PROFILE_DATA(vs_non_idle_starve_count);
        gcmkUPDATE_PROFILE_DATA(vs_starve_count);
        gcmkUPDATE_PROFILE_DATA(vs_stall_count);
        gcmkUPDATE_PROFILE_DATA(vs_process_count);
        gcmkUPDATE_PROFILE_DATA(ps_non_idle_starve_count);
        gcmkUPDATE_PROFILE_DATA(ps_starve_count);
        gcmkUPDATE_PROFILE_DATA(ps_stall_count);
        gcmkUPDATE_PROFILE_DATA(ps_process_count);
        gcmkUPDATE_PROFILE_DATA(shader_cycle_count);
        gcmkUPDATE_PROFILE_DATA(tx_non_idle_starve_count);
        gcmkUPDATE_PROFILE_DATA(tx_starve_count);
        gcmkUPDATE_PROFILE_DATA(tx_stall_count);
        gcmkUPDATE_PROFILE_DATA(tx_process_count);
    }
#if !USE_SW_RESET
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24)))
));
#endif

    /* PA */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_input_vtx_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (4) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_input_prim_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (5) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_output_prim_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (6) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_depth_clipped_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (7) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_trivial_rejected_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (8) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_culled_counter));
    if (hasNewCounters)
    {
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (12) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_non_idle_starve_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (13) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_starve_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (14) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_stall_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (15) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_process_count));
    }
#if USE_SW_RESET
    gcmkRESET_PROFILE_DATA(pa_input_vtx_counter, preProfiler);
    gcmkRESET_PROFILE_DATA(pa_input_prim_counter, preProfiler);
    gcmkRESET_PROFILE_DATA(pa_output_prim_counter, preProfiler);
    gcmkRESET_PROFILE_DATA(pa_depth_clipped_counter, preProfiler);
    gcmkRESET_PROFILE_DATA(pa_trivial_rejected_counter, preProfiler);
    gcmkRESET_PROFILE_DATA(pa_culled_counter, preProfiler);
    if (hasNewCounters)
    {
        gcmkRESET_PROFILE_DATA(pa_non_idle_starve_count, preProfiler);
        gcmkRESET_PROFILE_DATA(pa_starve_count, preProfiler);
        gcmkRESET_PROFILE_DATA(pa_stall_count, preProfiler);
        gcmkRESET_PROFILE_DATA(pa_process_count, preProfiler);
    }
#endif
    gcmkUPDATE_PROFILE_DATA(pa_input_vtx_counter);
    gcmkUPDATE_PROFILE_DATA(pa_input_prim_counter);
    gcmkUPDATE_PROFILE_DATA(pa_output_prim_counter);
    gcmkUPDATE_PROFILE_DATA(pa_depth_clipped_counter);
    gcmkUPDATE_PROFILE_DATA(pa_trivial_rejected_counter);
    gcmkUPDATE_PROFILE_DATA(pa_culled_counter);
    if (hasNewCounters)
    {
        gcmkUPDATE_PROFILE_DATA(pa_non_idle_starve_count);
        gcmkUPDATE_PROFILE_DATA(pa_starve_count);
        gcmkUPDATE_PROFILE_DATA(pa_stall_count);
        gcmkUPDATE_PROFILE_DATA(pa_process_count);
    }
#if !USE_SW_RESET
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0)))
));
#endif

    /* SE */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_culled_triangle_count));
    gcmkUPDATE_PROFILE_DATA(se_culled_triangle_count);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_culled_lines_count));
    gcmkUPDATE_PROFILE_DATA(se_culled_lines_count);
    if (hasNewCounters)
    {
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (8) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_starve_count));
        gcmkUPDATE_PROFILE_DATA(se_starve_count);
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (9) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_stall_count));
        gcmkUPDATE_PROFILE_DATA(se_stall_count);
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (10) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_receive_triangle_count));
        gcmkUPDATE_PROFILE_DATA(se_receive_triangle_count);
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (11) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_send_triangle_count));
        gcmkUPDATE_PROFILE_DATA(se_send_triangle_count);
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (12) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_receive_lines_count));
        gcmkUPDATE_PROFILE_DATA(se_receive_lines_count);
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (13) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_send_lines_count));
        gcmkUPDATE_PROFILE_DATA(se_send_lines_count);
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (19) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_process_count));
        gcmkUPDATE_PROFILE_DATA(se_process_count);
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (20) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_non_idle_starve_count));
        gcmkUPDATE_PROFILE_DATA(se_non_idle_starve_count);
    }
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8)))
));

    /* RA */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_valid_pixel_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_total_quad_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_valid_quad_count_after_early_z));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_total_primitive_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (9) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_pipe_cache_miss_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (10) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_prefetch_cache_miss_counter));
    if (hasNewCounters)
    {
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (13) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_non_idle_starve_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (14) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_starve_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (15) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_stall_count));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (16) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_process_count));
    }
#if USE_SW_RESET
    gcmkRESET_PROFILE_DATA(ra_valid_pixel_count, preProfiler);
    gcmkRESET_PROFILE_DATA(ra_total_quad_count, preProfiler);
    gcmkRESET_PROFILE_DATA(ra_valid_quad_count_after_early_z, preProfiler);
    gcmkRESET_PROFILE_DATA(ra_total_primitive_count, preProfiler);
    gcmkRESET_PROFILE_DATA(ra_pipe_cache_miss_counter, preProfiler);
    gcmkRESET_PROFILE_DATA(ra_prefetch_cache_miss_counter, preProfiler);
    if (hasNewCounters)
    {
        gcmkRESET_PROFILE_DATA(ra_non_idle_starve_count, preProfiler);
        gcmkRESET_PROFILE_DATA(ra_starve_count, preProfiler);
        gcmkRESET_PROFILE_DATA(ra_stall_count, preProfiler);
        gcmkRESET_PROFILE_DATA(ra_process_count, preProfiler);
    }
#endif
    gcmkUPDATE_PROFILE_DATA(ra_valid_pixel_count);
    gcmkUPDATE_PROFILE_DATA(ra_total_quad_count);
    gcmkUPDATE_PROFILE_DATA(ra_valid_quad_count_after_early_z);
    gcmkUPDATE_PROFILE_DATA(ra_total_primitive_count);
    gcmkUPDATE_PROFILE_DATA(ra_pipe_cache_miss_counter);
    gcmkUPDATE_PROFILE_DATA(ra_prefetch_cache_miss_counter);
    if (hasNewCounters)
    {
        gcmkUPDATE_PROFILE_DATA(ra_non_idle_starve_count);
        gcmkUPDATE_PROFILE_DATA(ra_starve_count);
        gcmkUPDATE_PROFILE_DATA(ra_stall_count);
        gcmkUPDATE_PROFILE_DATA(ra_process_count);
    }
#if !USE_SW_RESET
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16)))
));
#endif

    /* TX */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_total_bilinear_requests));
    gcmkUPDATE_PROFILE_DATA(tx_total_bilinear_requests);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_total_trilinear_requests));
    gcmkUPDATE_PROFILE_DATA(tx_total_trilinear_requests);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_total_discarded_texture_requests));
    gcmkUPDATE_PROFILE_DATA(tx_total_discarded_texture_requests);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_total_texture_requests));
    gcmkUPDATE_PROFILE_DATA(tx_total_texture_requests);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (5) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_mem_read_count));
    gcmkUPDATE_PROFILE_DATA(tx_mem_read_count);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (6) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_mem_read_in_8B_count));
    gcmkUPDATE_PROFILE_DATA(tx_mem_read_in_8B_count);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (7) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_cache_miss_count));
    gcmkUPDATE_PROFILE_DATA(tx_cache_miss_count);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (8) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_cache_hit_texel_count));
    gcmkUPDATE_PROFILE_DATA(tx_cache_hit_texel_count);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (9) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_cache_miss_texel_count));
    gcmkUPDATE_PROFILE_DATA(tx_cache_miss_texel_count);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24)))
));

    /* MC */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00468, &profiler->mc_total_read_req_8B_from_pipeline));
    gcmkUPDATE_PROFILE_DATA(mc_total_read_req_8B_from_pipeline);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00468, &profiler->mc_total_read_req_8B_from_IP));
    gcmkUPDATE_PROFILE_DATA(mc_total_read_req_8B_from_IP);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00468, &profiler->mc_total_write_req_8B_from_pipeline));
    gcmkUPDATE_PROFILE_DATA(mc_total_write_req_8B_from_pipeline);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0)))
));

    /* read latency counters */
    if (hasNewCounters)
    {
        /* latency */
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x0056C,
                                     &mc_axi_max_min_latency));

        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x00570,
                                     &profiler->mc_axi_total_latency));

        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x00574,
                                     &profiler->mc_axi_sample_count));

        /* Reset Latency counters */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00568,
                                      0x10a));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00568,
                                      0xa));

        profiler->mc_axi_min_latency = (mc_axi_max_min_latency & 0x0fff0000) >> 16;
        profiler->mc_axi_max_latency = (mc_axi_max_min_latency & 0x00000fff);
        if (profiler->mc_axi_min_latency == 4095)
            profiler->mc_axi_min_latency = 0;

        gcmkUPDATE_PROFILE_DATA(mc_axi_min_latency);
        gcmkUPDATE_PROFILE_DATA(mc_axi_max_latency);
        gcmkUPDATE_PROFILE_DATA(mc_axi_total_latency);
        gcmkUPDATE_PROFILE_DATA(mc_axi_sample_count);

        /* FE */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (10) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00450, &profiler->fe_draw_count));
        gcmkUPDATE_PROFILE_DATA(fe_draw_count);
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (11) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00450, &profiler->fe_out_vertex_count));
        gcmkUPDATE_PROFILE_DATA(fe_draw_count);
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (14) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00450, &profiler->fe_stall_count));
        gcmkUPDATE_PROFILE_DATA(fe_draw_count);
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (15) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00450, &profiler->fe_starve_count));
        gcmkUPDATE_PROFILE_DATA(fe_draw_count);
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0)))
));
    }

    /* HI */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0046C, &profiler->hi_axi_cycles_read_request_stalled));
    gcmkUPDATE_PROFILE_DATA(hi_axi_cycles_read_request_stalled);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0046C, &profiler->hi_axi_cycles_write_request_stalled));
    gcmkUPDATE_PROFILE_DATA(hi_axi_cycles_write_request_stalled);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0046C, &profiler->hi_axi_cycles_write_data_stalled));
    gcmkUPDATE_PROFILE_DATA(hi_axi_cycles_write_data_stalled);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8)))
));
    }

#else
    {
        gctUINT32 mc_axi_max_min_latency;
        gctUINT clock;
        gctUINT32 totalRead, totalWrite;

        /* Read clock control register. */
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x00000,
                                         &clock));

        profiler->gpuTotalRead64BytesPerFrame = 0;
        profiler->gpuTotalWrite64BytesPerFrame = 0;

        /* Walk through all avaiable pixel pipes. */
        for (i = 0; i < Hardware->identity.pixelPipes; ++i)
        {
            /* Select proper pipe. */
            gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                               Hardware->core,
                                               0x00000,
                                               ((((gctUINT32) (clock)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:20) - (0 ? 23:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:20) - (0 ? 23:20) + 1))))))) << (0 ?
 23:20))) | (((gctUINT32) ((gctUINT32) (i) & ((gctUINT32) ((((1 ? 23:20) - (0 ?
 23:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:20) - (0 ? 23:20) + 1))))))) << (0 ?
 23:20)))));

            /* BW */
            gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 0x00040,
                                 &totalRead));
            gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 0x00044,
                                 &totalWrite));

            profiler->gpuTotalRead64BytesPerFrame += totalRead;
            profiler->gpuTotalWrite64BytesPerFrame += totalWrite;
        }

        gcmkUPDATE_PROFILE_DATA(gpuTotalRead64BytesPerFrame);
        gcmkUPDATE_PROFILE_DATA(gpuTotalWrite64BytesPerFrame);

        /* Reset clock control register. */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                          Hardware->core,
                                          0x00000,
                                          clock));

        /* Reset counters. */
        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x0003C, 1));
        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x0003C, 0));
        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00438, 0));
        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00078, 0));

        /* read latency counters */
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x0056C,
                                     &mc_axi_max_min_latency));

        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x00570,
                                     &profiler->mc_axi_total_latency));

        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x00574,
                                     &profiler->mc_axi_sample_count));

        /* Reset Latency counters */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00568,
                                      0x10a));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00568,
                                      0xa));

        profiler->mc_axi_min_latency = (mc_axi_max_min_latency & 0x0fff0000) >> 16;
        profiler->mc_axi_max_latency = (mc_axi_max_min_latency & 0x00000fff);
        if (profiler->mc_axi_min_latency == 4095)
            profiler->mc_axi_min_latency = 0;

        gcmkUPDATE_PROFILE_DATA(mc_axi_min_latency);
        gcmkUPDATE_PROFILE_DATA(mc_axi_max_latency);
        gcmkUPDATE_PROFILE_DATA(mc_axi_total_latency);
        gcmkUPDATE_PROFILE_DATA(mc_axi_sample_count);

        /* FE */
        for (i = 0; i < MODULE_FRONT_END_COUNTER_NUM; i++)
        {
            gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (i) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00450, &profiler->feCounters[i]));
            gcmkUPDATE_PROFILE_DATA(feCounters[i]);
        }
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0)))
));

        /* PA */
        for (i = 0; i < MODULE_FRONT_END_COUNTER_NUM; i++)
        {
            gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (i) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->paCounters[i]));
            gcmkUPDATE_PROFILE_DATA(paCounters[i]);
        }
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0)))
));

        /* SH */
        for (i = 0; i < MODULE_SHADER_COUNTER_NUM; i++)
        {
            gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (i) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->shCounters[i]));
            gcmkUPDATE_PROFILE_DATA(shCounters[i]);
        }
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24)))
));

        /* SE */
        for (i = 0; i < MODULE_SETUP_COUNTER_NUM; i++)
        {
            gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (i) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->seCounters[i]));
            gcmkUPDATE_PROFILE_DATA(seCounters[i]);
        }
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8)))
));

        /* RA */
        for (i = 0; i < MODULE_RASTERIZER_COUNTER_NUM; i++)
        {
            gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (i) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->raCounters[i]));
            gcmkUPDATE_PROFILE_DATA(raCounters[i]);
        }
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16)))
));

        /* TX */
        for (i = 0; i < MODULE_TEXTURE_COUNTER_NUM; i++)
        {
            gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (i) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->txCounters[i]));
            gcmkUPDATE_PROFILE_DATA(txCounters[i]);
        }
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24)))
));

         /* PE */
        for (i = 0; i < MODULE_PIXEL_ENGINE_COUNTER_NUM; i++)
        {
            gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (i) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454, &profiler->peCounters[i]));
            gcmkUPDATE_PROFILE_DATA(peCounters[i]);
        }
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16)))
));

        /* MC */
        for (i = 0; i < MODULE_MEMORY_CONTROLLER_COUNTER_NUM; i++)
        {
            gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (i) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00468, &profiler->mcCounters[i]));
            gcmkUPDATE_PROFILE_DATA(mcCounters[i]);
        }
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 7:0) - (0 ?
 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ?
 7:0)))
));

         /* HI */
        for (i = 0; i < MODULE_HOST_INTERFACE_COUNTER_NUM; i++)
        {
            gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (i) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0046C, &profiler->hiCounters[i]));
            gcmkUPDATE_PROFILE_DATA(hiCounters[i]);
        }
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8)))
));

    }
#endif

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}
#endif


gceSTATUS
gckHARDWARE_InitProfiler(
    IN gckHARDWARE Hardware
    )
{
    gceSTATUS status;
    gctUINT32 control;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);
    gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x00000,
                                     &control));
    /* Enable debug register. */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00000,
                                      ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 11:11) - (0 ? 11:11) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:11) - (0 ? 11:11) + 1))))))) << (0 ?
 11:11))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 11:11) - (0 ?
 11:11) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:11) - (0 ? 11:11) + 1))))))) << (0 ?
 11:11)))));

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

static gceSTATUS
_ResetGPU(
    IN gckHARDWARE Hardware,
    IN gckOS Os,
    IN gceCORE Core
    )
{
    gctUINT32 control, idle;
    gceSTATUS status;

    for (;;)
    {
        /* Disable clock gating. */
        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                    Core,
                    Hardware->powerBaseAddress +
                    0x00104,
                    0x00000000));

        control = ((((gctUINT32) (0x01590880)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 17:17) - (0 ? 17:17) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:17) - (0 ? 17:17) + 1))))))) << (0 ?
 17:17))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 17:17) - (0 ?
 17:17) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:17) - (0 ? 17:17) + 1))))))) << (0 ?
 17:17)));

        /* Disable pulse-eater. */
        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                    Core,
                    0x0010C,
                    control));

        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                    Core,
                    0x0010C,
                    ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0)))));

        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                    Core,
                    0x0010C,
                    control));

        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                    Core,
                    0x00000,
                    ((((gctUINT32) (0x00000900)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 9:9) - (0 ?
 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ?
 9:9)))));

        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                    Core,
                    0x00000,
                    0x00000900));

        /* Wait for clock being stable. */
        gcmkONERROR(gckOS_Delay(Os, 1));

        /* Isolate the GPU. */
        control = ((((gctUINT32) (0x00000900)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ?
 19:19))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 19:19) - (0 ?
 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ?
 19:19)));

        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                                          Core,
                                          0x00000,
                                          control));

        /* Set soft reset. */
        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                                          Core,
                                          0x00000,
                                          ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ?
 12:12))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 12:12) - (0 ?
 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ?
 12:12)))));

        /* Wait for reset. */
        gcmkONERROR(gckOS_Delay(Os, 1));

        /* Reset soft reset bit. */
        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                                          Core,
                                          0x00000,
                                          ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ?
 12:12))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 12:12) - (0 ?
 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ?
 12:12)))));

        /* Reset GPU isolation. */
        control = ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ?
 19:19))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 19:19) - (0 ?
 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ?
 19:19)));

        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                                          Core,
                                          0x00000,
                                          control));

        /* Read idle register. */
        gcmkONERROR(gckOS_ReadRegisterEx(Os,
                                         Core,
                                         0x00004,
                                         &idle));

        if ((((((gctUINT32) (idle)) >> (0 ? 0:0)) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1)))))) ) == 0)
        {
            continue;
        }

        /* Read reset register. */
        gcmkONERROR(gckOS_ReadRegisterEx(Os,
                                         Core,
                                         0x00000,
                                         &control));

        if (((((((gctUINT32) (control)) >> (0 ? 16:16)) & ((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1)))))) ) == 0)
        ||  ((((((gctUINT32) (control)) >> (0 ? 17:17)) & ((gctUINT32) ((((1 ? 17:17) - (0 ? 17:17) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:17) - (0 ? 17:17) + 1)))))) ) == 0)
        )
        {
            continue;
        }

        /* GPU is idle. */
        break;
    }

    /* Success. */
    return gcvSTATUS_OK;

OnError:

    /* Return the error. */
    return status;
}

gceSTATUS
gckHARDWARE_Reset(
    IN gckHARDWARE Hardware
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_OBJECT(Hardware->kernel, gcvOBJ_KERNEL);

    /* Record context ID in debug register before reset. */
    gcmkONERROR(gckHARDWARE_UpdateContextID(Hardware));

    /* Hardware reset. */
    status = gckOS_ResetGPU(Hardware->os, Hardware->core);

    if (gcmIS_ERROR(status))
    {
        if (Hardware->identity.chipRevision < 0x4600)
        {
            /* Not supported - we need the isolation bit. */
            gcmkONERROR(gcvSTATUS_NOT_SUPPORTED);
        }

        /* Soft reset. */
        gcmkONERROR(_ResetGPU(Hardware, Hardware->os, Hardware->core));
    }

    /* Force the command queue to reload the next context. */
    Hardware->kernel->command->currContext = gcvNULL;

    /* Initialize hardware. */
    gcmkONERROR(gckHARDWARE_InitializeHardware(Hardware));

    /* Jump to address into which GPU should run if it doesn't stuck. */
    gcmkONERROR(gckHARDWARE_Execute(Hardware, Hardware->kernel->restoreAddress, 16));

    gcmkPRINT("[galcore]: recovery done");

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkPRINT("[galcore]: Hardware not reset successfully, give up");

    /* Return the error. */
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckHARDWARE_GetBaseAddress(
    IN gckHARDWARE Hardware,
    OUT gctUINT32_PTR BaseAddress
    )
{
    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(BaseAddress != gcvNULL);

    /* Test if we have a new Memory Controller. */
    if (gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_MC20))
    {
        /* No base address required. */
        *BaseAddress = 0;
    }
    else
    {
        /* Get the base address from the OS. */
        *BaseAddress = Hardware->baseAddress;
    }

    /* Success. */
    gcmkFOOTER_ARG("*BaseAddress=0x%08x", *BaseAddress);
    return gcvSTATUS_OK;
}

gceSTATUS
gckHARDWARE_NeedBaseAddress(
    IN gckHARDWARE Hardware,
    IN gctUINT32 State,
    OUT gctBOOL_PTR NeedBase
    )
{
    gctBOOL need = gcvFALSE;

    gcmkHEADER_ARG("Hardware=0x%x State=0x%08x", Hardware, State);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(NeedBase != gcvNULL);

    /* Make sure this is a load state. */
    if (((((gctUINT32) (State)) >> (0 ? 31:27) & ((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1)))))) == (0x01 & ((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:
27) + 1))))))))
    {
#if gcdENABLE_3D
        /* Get the state address. */
        switch ((((((gctUINT32) (State)) >> (0 ? 15:0)) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1)))))) ))
        {
        case 0x0596:
        case 0x0597:
        case 0x0599:
        case 0x059A:
        case 0x05A9:
            /* These states need a TRUE physical address. */
            need = gcvTRUE;
            break;
        }
#else
        /* 2D addresses don't need a base address. */
#endif
    }

    /* Return the flag. */
    *NeedBase = need;

    /* Success. */
    gcmkFOOTER_ARG("*NeedBase=%d", *NeedBase);
    return gcvSTATUS_OK;
}

gceSTATUS
gckHARDWARE_SetIsrManager(
   IN gckHARDWARE Hardware,
   IN gctISRMANAGERFUNC StartIsr,
   IN gctISRMANAGERFUNC StopIsr,
   IN gctPOINTER Context
   )
{
    gceSTATUS status = gcvSTATUS_OK;

    gcmkHEADER_ARG("Hardware=0x%x, StartIsr=0x%x, StopIsr=0x%x, Context=0x%x",
                   Hardware, StartIsr, StopIsr, Context);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    if (StartIsr == gcvNULL ||
        StopIsr == gcvNULL)
    {
        status = gcvSTATUS_INVALID_ARGUMENT;

        gcmkFOOTER();
        return status;
    }

    Hardware->startIsr = StartIsr;
    Hardware->stopIsr = StopIsr;
    Hardware->isrContext = Context;

    /* Success. */
    gcmkFOOTER();

    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_Compose
**
**  Start a composition.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_Compose(
    IN gckHARDWARE Hardware,
    IN gctUINT32 ProcessID,
    IN gctPHYS_ADDR Physical,
    IN gctPOINTER Logical,
    IN gctSIZE_T Offset,
    IN gctSIZE_T Size,
    IN gctUINT8 EventID
    )
{
#if gcdENABLE_3D
    gceSTATUS status;
    gctUINT32_PTR triggerState;

    gcmkHEADER_ARG("Hardware=0x%x Physical=0x%x Logical=0x%x"
                   " Offset=%d Size=%d EventID=%d",
                   Hardware, Physical, Logical, Offset, Size, EventID);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(((Size + 8) & 63) == 0);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

    /* Program the trigger state. */
    triggerState = (gctUINT32_PTR) ((gctUINT8_PTR) Logical + Offset + Size);
    triggerState[0] = 0x0C03;
    triggerState[1]
        = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:0) - (0 ?
 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ?
 1:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ? 1:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 5:4) - (0 ?
 5:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:4) - (0 ? 5:4) + 1))))))) << (0 ?
 5:4))) | (((gctUINT32) (0x3 & ((gctUINT32) ((((1 ? 5:4) - (0 ? 5:4) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 5:4) - (0 ? 5:4) + 1))))))) << (0 ? 5:4)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 8:8) - (0 ?
 8:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:8) - (0 ? 8:8) + 1))))))) << (0 ?
 8:8))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 8:8) - (0 ?
 8:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:8) - (0 ? 8:8) + 1))))))) << (0 ?
 8:8)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 24:24) - (0 ?
 24:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 24:24) - (0 ? 24:24) + 1))))))) << (0 ?
 24:24))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 24:24) - (0 ?
 24:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 24:24) - (0 ? 24:24) + 1))))))) << (0 ?
 24:24)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:12) - (0 ?
 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ?
 12:12))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 12:12) - (0 ?
 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ?
 12:12)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 20:16) - (0 ?
 20:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:16) - (0 ? 20:16) + 1))))))) << (0 ?
 20:16))) | (((gctUINT32) ((gctUINT32) (EventID) & ((gctUINT32) ((((1 ?
 20:16) - (0 ? 20:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:16) - (0 ? 20:16) + 1))))))) << (0 ?
 20:16)))
        ;

#if gcdNONPAGED_MEMORY_CACHEABLE
    /* Flush the cache for the wait/link. */
    gcmkONERROR(gckOS_CacheClean(
        Hardware->os, ProcessID, gcvNULL,
        (gctUINT32)Physical, Logical, Offset + Size
        ));
#endif

    /* Start composition. */
    gcmkONERROR(gckOS_WriteRegisterEx(
        Hardware->os, Hardware->core, 0x00554,
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:0) - (0 ?
 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ?
 1:0))) | (((gctUINT32) (0x3 & ((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ? 1:0)))
        ));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
#else
    /* Return the status. */
    return gcvSTATUS_NOT_SUPPORTED;
#endif
}

/*******************************************************************************
**
**  gckHARDWARE_IsFeatureAvailable
**
**  Verifies whether the specified feature is available in hardware.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      gceFEATURE Feature
**          Feature to be verified.
*/
gceSTATUS
gckHARDWARE_IsFeatureAvailable(
    IN gckHARDWARE Hardware,
    IN gceFEATURE Feature
    )
{
    gctBOOL available;

    gcmkHEADER_ARG("Hardware=0x%x Feature=%d", Hardware, Feature);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    available = _QueryFeatureDatabase(Hardware, Feature);

    /* Return result. */
    gcmkFOOTER_ARG("%d", available ? gcvSTATUS_TRUE : gcvSTATUS_FALSE);
    return available ? gcvSTATUS_TRUE : gcvSTATUS_FALSE;
}

/*******************************************************************************
**
**  gckHARDWARE_DumpMMUException
**
**  Dump the MMU debug info on an MMU exception.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_DumpMMUException(
    IN gckHARDWARE Hardware
    )
{
    gctUINT32 mmu       = 0;
    gctUINT32 mmuStatus = 0;
    gctUINT32 address   = 0;
    gctUINT32 i         = 0;
    gctUINT32 mtlb      = 0;
    gctUINT32 stlb      = 0;
    gctUINT32 offset    = 0;
#if gcdPROCESS_ADDRESS_SPACE
    gcsDATABASE_PTR database;
#endif
    gctUINT32 mmuStatusRegAddress;
    gctUINT32 mmuExceptionAddress;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

#if gcdENABLE_TRUST_APPLICATION
    if (Hardware->secureMode == gcvSECURE_IN_TA)
    {
        gcmkVERIFY_OK(gckKERNEL_SecurityDumpMMUException(Hardware->kernel));

        gckMMU_DumpRecentFreedAddress(Hardware->kernel->mmu);

        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }
    else
#endif
    if (Hardware->secureMode == gcvSECURE_NONE)
    {
        mmuStatusRegAddress = 0x00188;
        mmuExceptionAddress = 0x00190;
    }
    else
    {
        mmuStatusRegAddress = 0x00384;
        mmuExceptionAddress = 0x00380;
    }

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    gcmkPRINT("GPU[%d](ChipModel=0x%x ChipRevision=0x%x):\n",
              Hardware->core,
              Hardware->identity.chipModel,
              Hardware->identity.chipRevision);

    gcmkPRINT("**************************\n");
    gcmkPRINT("***   MMU ERROR DUMP   ***\n");
    gcmkPRINT("**************************\n");


    gcmkVERIFY_OK(
        gckOS_ReadRegisterEx(Hardware->os,
                             Hardware->core,
                             mmuStatusRegAddress,
                             &mmuStatus));

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

        gcmkVERIFY_OK(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 mmuExceptionAddress + i * 4,
                                 &address));

        mtlb   = (address & gcdMMU_MTLB_MASK) >> gcdMMU_MTLB_SHIFT;
        stlb   = (address & gcdMMU_STLB_4K_MASK) >> gcdMMU_STLB_4K_SHIFT;
        offset =  address & gcdMMU_OFFSET_4K_MASK;

        gcmkPRINT("  MMU%d: exception address = 0x%08X\n", i, address);

        gcmkPRINT("    MTLB entry = %d\n", mtlb);

        gcmkPRINT("    STLB entry = %d\n", stlb);

        gcmkPRINT("    Offset = 0x%08X (%d)\n", offset, offset);

        gckMMU_DumpPageTableEntry(Hardware->kernel->mmu, address);

#if gcdPROCESS_ADDRESS_SPACE
        for (i = 0; i < gcmCOUNTOF(Hardware->kernel->db->db); ++i)
        {
            for (database = Hardware->kernel->db->db[i];
                    database != gcvNULL;
                    database = database->next)
            {
                gcmkPRINT("    database [%d] :", database->processID);
                gckMMU_DumpPageTableEntry(database->mmu, address);
            }
        }
#endif

        gckMMU_DumpRecentFreedAddress(Hardware->kernel->mmu);
    }

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckHARDWARE_DumpGPUState
**
**  Dump the GPU debug registers.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_DumpGPUState(
    IN gckHARDWARE Hardware
    )
{
    static gctCONST_STRING _cmdState[] =
    {
        "PAR_IDLE_ST", "PAR_DEC_ST", "PAR_ADR0_ST", "PAR_LOAD0_ST",
        "PAR_ADR1_ST", "PAR_LOAD1_ST", "PAR_3DADR_ST", "PAR_3DCMD_ST",
        "PAR_3DCNTL_ST", "PAR_3DIDXCNTL_ST", "PAR_INITREQDMA_ST",
        "PAR_DRAWIDX_ST", "PAR_DRAW_ST", "PAR_2DRECT0_ST", "PAR_2DRECT1_ST",
        "PAR_2DDATA0_ST", "PAR_2DDATA1_ST", "PAR_WAITFIFO_ST", "PAR_WAIT_ST",
        "PAR_LINK_ST", "PAR_END_ST", "PAR_STALL_ST"
    };

    static gctCONST_STRING _cmdDmaState[] =
    {
        "CMD_IDLE_ST", "CMD_START_ST", "CMD_REQ_ST", "CMD_END_ST"
    };

    static gctCONST_STRING _cmdFetState[] =
    {
        "FET_IDLE_ST", "FET_RAMVALID_ST", "FET_VALID_ST"
    };

    static gctCONST_STRING _reqDmaState[] =
    {
        "REQ_IDLE_ST", "REQ_WAITIDX_ST", "REQ_CAL_ST"
    };

    static gctCONST_STRING _calState[] =
    {
        "CAL_IDLE_ST", "CAL_LDADR_ST", "CAL_IDXCALC_ST"
    };

    static gctCONST_STRING _veReqState[] =
    {
        "VER_IDLE_ST", "VER_CKCACHE_ST", "VER_MISS_ST"
    };

    static gcsiDEBUG_REGISTERS _dbgRegs[] =
    {
        { "RA", 0x474, 16, 0x448, 256, 0x1, 0x00 },
        { "TX", 0x474, 24, 0x44C, 128, 0x1, 0x00 },
        { "FE", 0x470, 0, 0x450, 256, 0x1, 0x00 },
        { "PE", 0x470, 16, 0x454, 256, 0x3, 0x00 },
        { "DE", 0x470, 8, 0x458, 256, 0x1, 0x00 },
        { "SH", 0x470, 24, 0x45C, 256, 0x1, 0x00 },
        { "PA", 0x474, 0, 0x460, 256, 0x1, 0x00 },
        { "SE", 0x474, 8, 0x464, 256, 0x1, 0x00 },
        { "MC", 0x478, 0, 0x468, 256, 0x3, 0x00 },
        { "HI", 0x478, 8, 0x46C, 256, 0x1, 0x00 },
        { "TPG", 0x474, 24, 0x44C, 32, 0x2, 0x80 },
        { "TFB", 0x474, 24, 0x44C, 32, 0x2, 0xA0 },
        { "USC", 0x474, 24, 0x44C, 64, 0x2, 0xC0 },
        { "L2", 0x478, 0, 0x564, 256, 0x1, 0x00 },
        { "BLT", 0x478, 24, 0x1A4, 256, 0x1, 0x00 }
    };

    static gctUINT32 _otherRegs[] =
    {
        0x040, 0x044, 0x04C, 0x050, 0x054, 0x058, 0x05C, 0x060,
        0x43c, 0x440, 0x444, 0x414, 0x100
    };

    gceSTATUS status;
    gckKERNEL kernel = gcvNULL;
    gctUINT32 idle = 0, axi = 0;
    gctUINT32 dmaAddress1 = 0, dmaAddress2 = 0;
    gctUINT32 dmaState1 = 0, dmaState2 = 0;
    gctUINT32 dmaLow = 0, dmaHigh = 0;
    gctUINT32 cmdState = 0, cmdDmaState = 0, cmdFetState = 0;
    gctUINT32 dmaReqState = 0, calState = 0, veReqState = 0;
    gctUINT i;
    gctUINT pipe = 0, pixelPipes = 0;
    gctUINT32 control = 0, oldControl = 0;
    gckOS os = Hardware->os;
    gceCORE core = Hardware->core;

    gcmkHEADER_ARG("Hardware=0x%X", Hardware);

    kernel = Hardware->kernel;

    gcmkPRINT_N(12, "GPU[%d](ChipModel=0x%x ChipRevision=0x%x):\n",
                core,
                Hardware->identity.chipModel,
                Hardware->identity.chipRevision);

    pixelPipes = Hardware->identity.pixelPipes
               ? Hardware->identity.pixelPipes
               : 1;

    /* Reset register values. */
    idle        = axi         =
    dmaState1   = dmaState2   =
    dmaAddress1 = dmaAddress2 =
    dmaLow      = dmaHigh     = 0;

    /* Verify whether DMA is running. */
    gcmkONERROR(_VerifyDMA(
        os, core, &dmaAddress1, &dmaAddress2, &dmaState1, &dmaState2
        ));

    cmdState    =  dmaState2        & 0x1F;
    cmdDmaState = (dmaState2 >>  8) & 0x03;
    cmdFetState = (dmaState2 >> 10) & 0x03;
    dmaReqState = (dmaState2 >> 12) & 0x03;
    calState    = (dmaState2 >> 14) & 0x03;
    veReqState  = (dmaState2 >> 16) & 0x03;

    gcmkONERROR(gckOS_ReadRegisterEx(os, core, 0x00004, &idle));
    gcmkONERROR(gckOS_ReadRegisterEx(os, core, 0x0000C, &axi));
    gcmkONERROR(gckOS_ReadRegisterEx(os, core, 0x00668, &dmaLow));
    gcmkONERROR(gckOS_ReadRegisterEx(os, core, 0x00668, &dmaLow));
    gcmkONERROR(gckOS_ReadRegisterEx(os, core, 0x0066C, &dmaHigh));
    gcmkONERROR(gckOS_ReadRegisterEx(os, core, 0x0066C, &dmaHigh));

    gcmkPRINT_N(0, "**************************\n");
    gcmkPRINT_N(0, "***   GPU STATE DUMP   ***\n");
    gcmkPRINT_N(0, "**************************\n");

    gcmkPRINT_N(4, "  axi      = 0x%08X\n", axi);

    gcmkPRINT_N(4, "  idle     = 0x%08X\n", idle);
    if ((idle & 0x00000001) == 0) gcmkPRINT_N(0, "    FE not idle\n");
    if ((idle & 0x00000002) == 0) gcmkPRINT_N(0, "    DE not idle\n");
    if ((idle & 0x00000004) == 0) gcmkPRINT_N(0, "    PE not idle\n");
    if ((idle & 0x00000008) == 0) gcmkPRINT_N(0, "    SH not idle\n");
    if ((idle & 0x00000010) == 0) gcmkPRINT_N(0, "    PA not idle\n");
    if ((idle & 0x00000020) == 0) gcmkPRINT_N(0, "    SE not idle\n");
    if ((idle & 0x00000040) == 0) gcmkPRINT_N(0, "    RA not idle\n");
    if ((idle & 0x00000080) == 0) gcmkPRINT_N(0, "    TX not idle\n");
    if ((idle & 0x00000100) == 0) gcmkPRINT_N(0, "    VG not idle\n");
    if ((idle & 0x00000200) == 0) gcmkPRINT_N(0, "    IM not idle\n");
    if ((idle & 0x00000400) == 0) gcmkPRINT_N(0, "    FP not idle\n");
    if ((idle & 0x00000800) == 0) gcmkPRINT_N(0, "    TS not idle\n");
    if ((idle & 0x00001000) == 0) gcmkPRINT_N(0, "    BL not idle\n");
    if ((idle & 0x00002000) == 0) gcmkPRINT_N(0, "    BP not idle\n");
    if ((idle & 0x00004000) == 0) gcmkPRINT_N(0, "    MC not idle\n");
    if ((idle & 0x80000000) != 0) gcmkPRINT_N(0, "    AXI low power mode\n");

    if (
        (dmaAddress1 == dmaAddress2)
     && (dmaState1 == dmaState2)
    )
    {
        gcmkPRINT_N(0, "  DMA appears to be stuck at this address:\n");
        gcmkPRINT_N(4, "    0x%08X\n", dmaAddress1);
    }
    else
    {
        if (dmaAddress1 == dmaAddress2)
        {
            gcmkPRINT_N(0, "  DMA address is constant, but state is changing:\n");
            gcmkPRINT_N(4, "    0x%08X\n", dmaState1);
            gcmkPRINT_N(4, "    0x%08X\n", dmaState2);
        }
        else
        {
            gcmkPRINT_N(0, "  DMA is running; known addresses are:\n");
            gcmkPRINT_N(4, "    0x%08X\n", dmaAddress1);
            gcmkPRINT_N(4, "    0x%08X\n", dmaAddress2);
        }
    }

    gcmkPRINT_N(4, "  dmaLow   = 0x%08X\n", dmaLow);
    gcmkPRINT_N(4, "  dmaHigh  = 0x%08X\n", dmaHigh);
    gcmkPRINT_N(4, "  dmaState = 0x%08X\n", dmaState2);
    gcmkPRINT_N(8, "    command state       = %d (%s)\n", cmdState, _cmdState   [cmdState]);
    gcmkPRINT_N(8, "    command DMA state   = %d (%s)\n", cmdDmaState, _cmdDmaState[cmdDmaState]);
    gcmkPRINT_N(8, "    command fetch state = %d (%s)\n", cmdFetState, _cmdFetState[cmdFetState]);
    gcmkPRINT_N(8, "    DMA request state   = %d (%s)\n", dmaReqState, _reqDmaState[dmaReqState]);
    gcmkPRINT_N(8, "    cal state           = %d (%s)\n", calState, _calState   [calState]);
    gcmkPRINT_N(8, "    VE request state    = %d (%s)\n", veReqState, _veReqState [veReqState]);

    gcmkPRINT_N(0, "  Debug registers:\n");

    for (i = 0; i < gcmCOUNTOF(_dbgRegs); i += 1)
    {
        gcmkONERROR(_DumpDebugRegisters(os, core, &_dbgRegs[i]));
    }

    /* Record control. */
    gckOS_ReadRegisterEx(os, core, 0x0, &oldControl);

    for (pipe = 0; pipe < pixelPipes; pipe++)
    {
        gcmkPRINT_N(4, "    Other Registers[%d]:\n", pipe);

        /* Switch pipe. */
        gcmkONERROR(gckOS_ReadRegisterEx(os, core, 0x0, &control));
        control &= ~(0xF << 20);
        control |= (pipe << 20);
        gcmkONERROR(gckOS_WriteRegisterEx(os, core, 0x0, control));

        for (i = 0; i < gcmCOUNTOF(_otherRegs); i += 1)
        {
            gctUINT32 read;
            gcmkONERROR(gckOS_ReadRegisterEx(os, core, _otherRegs[i], &read));
            gcmkPRINT_N(12, "      [0x%04X] 0x%08X\n", _otherRegs[i], read);
        }

        if (Hardware->mmuVersion)
        {
            gcmkPRINT("    MMU status from MC[%d]:", pipe);

            gckHARDWARE_DumpMMUException(Hardware);
        }
    }

    /* Restore control. */
    gcmkONERROR(gckOS_WriteRegisterEx(os, core, 0x0, oldControl));

    if (gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_HALTI0))
    {
        /* FE debug register. */
        gcmkVERIFY_OK(_DumpLinkStack(os, core, &_dbgRegs[2]));
    }

    _DumpFEStack(os, core, &_dbgRegs[2]);

    gcmkPRINT_N(0, "**************************\n");
    gcmkPRINT_N(0, "*****   SW COUNTERS  *****\n");
    gcmkPRINT_N(0, "**************************\n");
    gcmkPRINT_N(4, "    Execute Count = 0x%08X\n", Hardware->executeCount);
    gcmkPRINT_N(4, "    Execute Addr  = 0x%08X\n", Hardware->lastExecuteAddress);
    gcmkPRINT_N(4, "    End     Addr  = 0x%08X\n", Hardware->lastEnd);

    /* dump stack. */
    gckOS_DumpCallStack(os);

OnError:

    /* Return the error. */
    gcmkFOOTER();
    return status;
}

static gceSTATUS
gckHARDWARE_ReadPerformanceRegister(
    IN gckHARDWARE Hardware,
    IN gctUINT PerformanceAddress,
    IN gctUINT IndexAddress,
    IN gctUINT IndexShift,
    IN gctUINT Index,
    OUT gctUINT32_PTR Value
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x PerformanceAddress=0x%x IndexAddress=0x%x "
                   "IndexShift=%u Index=%u",
                   Hardware, PerformanceAddress, IndexAddress, IndexShift,
                   Index);

    /* Write the index. */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      IndexAddress,
                                      Index << IndexShift));

    /* Read the register. */
    gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     PerformanceAddress,
                                     Value));

    /* Test for reset. */
    if (Index == 15)
    {
        /* Index another register to get out of reset. */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, IndexAddress, 0));
    }

    /* Success. */
    gcmkFOOTER_ARG("*Value=0x%x", *Value);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckHARDWARE_GetFrameInfo(
    IN gckHARDWARE Hardware,
    OUT gcsHAL_FRAME_INFO * FrameInfo
    )
{
    gceSTATUS status;
    gctUINT i, clock;
    gcsHAL_FRAME_INFO info;
#if gcdFRAME_DB_RESET
    gctUINT reset;
#endif

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Get profile tick. */
    gcmkONERROR(gckOS_GetProfileTick(&info.ticks));

    /* Read SH counters and reset them. */
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0045C,
        0x00470,
        24,
        4,
        &info.shaderCycles));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0045C,
        0x00470,
        24,
        9,
        &info.vsInstructionCount));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0045C,
        0x00470,
        24,
        12,
        &info.vsTextureCount));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0045C,
        0x00470,
        24,
        7,
        &info.psInstructionCount));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0045C,
        0x00470,
        24,
        14,
        &info.psTextureCount));
#if gcdFRAME_DB_RESET
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0045C,
        0x00470,
        24,
        15,
        &reset));
#endif

    /* Read PA counters and reset them. */
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00460,
        0x00474,
        0,
        3,
        &info.vertexCount));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00460,
        0x00474,
        0,
        4,
        &info.primitiveCount));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00460,
        0x00474,
        0,
        7,
        &info.rejectedPrimitives));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00460,
        0x00474,
        0,
        8,
        &info.culledPrimitives));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00460,
        0x00474,
        0,
        6,
        &info.clippedPrimitives));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00460,
        0x00474,
        0,
        5,
        &info.outPrimitives));
#if gcdFRAME_DB_RESET
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00460,
        0x00474,
        0,
        15,
        &reset));
#endif

    /* Read RA counters and reset them. */
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00448,
        0x00474,
        16,
        3,
        &info.inPrimitives));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00448,
        0x00474,
        16,
        11,
        &info.culledQuadCount));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00448,
        0x00474,
        16,
        1,
        &info.totalQuadCount));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00448,
        0x00474,
        16,
        2,
        &info.quadCount));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00448,
        0x00474,
        16,
        0,
        &info.totalPixelCount));
#if gcdFRAME_DB_RESET
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00448,
        0x00474,
        16,
        15,
        &reset));
#endif

    /* Read TX counters and reset them. */
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0044C,
        0x00474,
        24,
        0,
        &info.bilinearRequests));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0044C,
        0x00474,
        24,
        1,
        &info.trilinearRequests));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0044C,
        0x00474,
        24,
        8,
        &info.txHitCount));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0044C,
        0x00474,
        24,
        9,
        &info.txMissCount));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0044C,
        0x00474,
        24,
        6,
        &info.txBytes8));
#if gcdFRAME_DB_RESET
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0044C,
        0x00474,
        24,
        15,
        &reset));
#endif

    /* Read clock control register. */
    gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x00000,
                                     &clock));

    /* Walk through all avaiable pixel pipes. */
    for (i = 0; i < Hardware->identity.pixelPipes; ++i)
    {
        /* Select proper pipe. */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                          Hardware->core,
                                          0x00000,
                                          ((((gctUINT32) (clock)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:20) - (0 ? 23:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:20) - (0 ? 23:20) + 1))))))) << (0 ?
 23:20))) | (((gctUINT32) ((gctUINT32) (i) & ((gctUINT32) ((((1 ? 23:20) - (0 ?
 23:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:20) - (0 ? 23:20) + 1))))))) << (0 ?
 23:20)))));

        /* Read cycle registers. */
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x00078,
                                         &info.cycles[i]));
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x0007C,
                                         &info.idleCycles[i]));
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x00438,
                                         &info.mcCycles[i]));

        /* Read bandwidth registers. */
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x0005C,
                                         &info.readRequests[i]));
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x00040,
                                         &info.readBytes8[i]));
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x00050,
                                         &info.writeRequests[i]));
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x00044,
                                         &info.writeBytes8[i]));

        /* Read PE counters. */
        gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
            Hardware,
            0x00454,
            0x00470,
            16,
            0,
            &info.colorKilled[i]));
        gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
            Hardware,
            0x00454,
            0x00470,
            16,
            2,
            &info.colorDrawn[i]));
        gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
            Hardware,
            0x00454,
            0x00470,
            16,
            1,
            &info.depthKilled[i]));
        gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
            Hardware,
            0x00454,
            0x00470,
            16,
            3,
            &info.depthDrawn[i]));
    }

    /* Zero out remaning reserved counters. */
    for (; i < 8; ++i)
    {
        info.readBytes8[i]    = 0;
        info.writeBytes8[i]   = 0;
        info.cycles[i]        = 0;
        info.idleCycles[i]    = 0;
        info.mcCycles[i]      = 0;
        info.readRequests[i]  = 0;
        info.writeRequests[i] = 0;
        info.colorKilled[i]   = 0;
        info.colorDrawn[i]    = 0;
        info.depthKilled[i]   = 0;
        info.depthDrawn[i]    = 0;
    }

    /* Reset clock control register. */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00000,
                                      clock));

    /* Reset cycle and bandwidth counters. */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x0003C,
                                      1));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x0003C,
                                      0));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00078,
                                      0));

#if gcdFRAME_DB_RESET
    /* Reset PE counters. */
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00454,
        0x00470,
        16,
        15,
        &reset));
#endif

    /* Copy to user. */
    gcmkONERROR(gckOS_CopyToUserData(Hardware->os,
                                     &info,
                                     FrameInfo,
                                     gcmSIZEOF(info)));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

#if gcdDVFS
#define READ_FROM_EATER1 0

gceSTATUS
gckHARDWARE_QueryLoad(
    IN gckHARDWARE Hardware,
    OUT gctUINT32 * Load
    )
{
    gctUINT32 debug1;
    gceSTATUS status;
    gcmkHEADER_ARG("Hardware=0x%X", Hardware);

    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Load != gcvNULL);

    gckOS_AcquireMutex(Hardware->os, Hardware->powerMutex, gcvINFINITE);

    if (Hardware->chipPowerState == gcvPOWER_ON)
    {
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x00110,
                                         Load));
#if READ_FROM_EATER1
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x00134,
                                         Load));
#endif

        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x00114,
                                         &debug1));

        /* Patch result of 0x110 with result of 0x114. */
        if ((debug1 & 0xFF) == 1)
        {
            *Load &= ~0xFF;
            *Load |= 1;
        }

        if (((debug1 & 0xFF00) >> 8) == 1)
        {
            *Load &= ~(0xFF << 8);
            *Load |= 1 << 8;
        }

        if (((debug1 & 0xFF0000) >> 16) == 1)
        {
            *Load &= ~(0xFF << 16);
            *Load |= 1 << 16;
        }

        if (((debug1 & 0xFF000000) >> 24) == 1)
        {
            *Load &= ~(0xFF << 24);
            *Load |= 1 << 24;
        }
    }
    else
    {
        status = gcvSTATUS_INVALID_REQUEST;
    }

OnError:

    gckOS_ReleaseMutex(Hardware->os, Hardware->powerMutex);

    gcmkFOOTER();
    return status;
}

gceSTATUS
gckHARDWARE_SetDVFSPeroid(
    IN gckHARDWARE Hardware,
    OUT gctUINT32 Frequency
    )
{
    gceSTATUS status;
    gctUINT32 period;
    gctUINT32 eater;

#if READ_FROM_EATER1
    gctUINT32 period1;
    gctUINT32 eater1;
#endif

    gcmkHEADER_ARG("Hardware=0x%X Frequency=%d", Hardware, Frequency);

    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    period = 0;

    while((64 << period) < (gcdDVFS_ANAYLSE_WINDOW * Frequency * 1000) )
    {
        period++;
    }

#if READ_FROM_EATER1
    /*
    *  Peroid = F * 1000 * 1000 / (60 * 16 * 1024);
    */
    period1 = Frequency * 6250 / 6114;
#endif

    gckOS_AcquireMutex(Hardware->os, Hardware->powerMutex, gcvINFINITE);

    if (Hardware->chipPowerState == gcvPOWER_ON)
    {
        /* Get current configure. */
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x0010C,
                                         &eater));

        /* Change peroid. */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                          Hardware->core,
                                          0x0010C,
                                          ((((gctUINT32) (eater)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8))) | (((gctUINT32) ((gctUINT32) (period) & ((gctUINT32) ((((1 ? 15:8) - (0 ?
 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ?
 15:8)))));

#if READ_FROM_EATER1
        /* Config eater1. */
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x00130,
                                         &eater1));

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                          Hardware->core,
                                          0x00130,
                                          ((((gctUINT32) (eater1)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:16) - (0 ? 31:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:16) - (0 ? 31:16) + 1))))))) << (0 ?
 31:16))) | (((gctUINT32) ((gctUINT32) (period1) & ((gctUINT32) ((((1 ?
 31:16) - (0 ? 31:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:16) - (0 ? 31:16) + 1))))))) << (0 ?
 31:16)))));
#endif
    }
    else
    {
        status = gcvSTATUS_INVALID_REQUEST;
    }

OnError:
    gckOS_ReleaseMutex(Hardware->os, Hardware->powerMutex);

    gcmkFOOTER();
    return status;
}

gceSTATUS
gckHARDWARE_InitDVFS(
    IN gckHARDWARE Hardware
    )
{
    gceSTATUS status;
    gctUINT32 data;

    gcmkHEADER_ARG("Hardware=0x%X", Hardware);

    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x0010C,
                                     &data));

    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ?
 16:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 16:16) - (0 ?
 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ?
 16:16)));
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 18:18) - (0 ? 18:18) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 18:18) - (0 ? 18:18) + 1))))))) << (0 ?
 18:18))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 18:18) - (0 ?
 18:18) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 18:18) - (0 ? 18:18) + 1))))))) << (0 ?
 18:18)));
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ?
 19:19))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 19:19) - (0 ?
 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ?
 19:19)));
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 20:20) - (0 ? 20:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:20) - (0 ? 20:20) + 1))))))) << (0 ?
 20:20))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 20:20) - (0 ?
 20:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:20) - (0 ? 20:20) + 1))))))) << (0 ?
 20:20)));
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 23:23) - (0 ? 23:23) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:23) - (0 ? 23:23) + 1))))))) << (0 ?
 23:23))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 23:23) - (0 ?
 23:23) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:23) - (0 ? 23:23) + 1))))))) << (0 ?
 23:23)));
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 22:22) - (0 ? 22:22) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 22:22) - (0 ? 22:22) + 1))))))) << (0 ?
 22:22))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 22:22) - (0 ?
 22:22) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 22:22) - (0 ? 22:22) + 1))))))) << (0 ?
 22:22)));

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "DVFS Configure=0x%X",
                   data);

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x0010C,
                                      data));

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}
#endif

/*******************************************************************************
**
**  gckHARDWARE_PrepareFunctions
**
**  Generate command buffer snippets which will be used by gckHARDWARE, by which
**  gckHARDWARE can manipulate GPU by FE command without using gckCOMMAND to avoid
**  race condition and deadlock.
**
**  Notice:
**  1. Each snippet can only be executed when GPU is idle.
**  2. Execution is triggered by AHB (0x658)
**  3. Each snippet followed by END so software can sync with GPU by checking GPU
**     idle
**  4. It is transparent to gckCOMMAND command buffer.
**
**  Existing Snippets:
**  1. MMU Configure
**     For new MMU, after GPU is reset, FE execute this command sequence to enble MMU.
*/
gceSTATUS
gckHARDWARE_PrepareFunctions(
    gckHARDWARE Hardware
    )
{
    gceSTATUS status;
    gckOS os;
    gctUINT32 offset = 0;
    gctUINT32 mmuBytes;
    gctUINT32 endBytes;
    gctUINT32 flushBytes;
    gctUINT32 eventBytes;
    gctUINT8_PTR logical;
    gctPHYS_ADDR_T physical;
    gcsHARDWARE_FUNCTION *function;
    gctUINT8 i;

    gcmkHEADER_ARG("%x", Hardware);

    os = Hardware->os;

    gcmkVERIFY_OK(gckOS_GetPageSize(os, &Hardware->functionBytes));

#if USE_KERNEL_VIRTUAL_BUFFERS
    if (Hardware->kernel->virtualCommandBuffer)
    {
        gcmkONERROR(gckKERNEL_AllocateVirtualCommandBuffer(
            Hardware->kernel,
            gcvFALSE,
            &Hardware->functionBytes,
            &Hardware->functionPhysical,
            &Hardware->functionLogical
            ));

        gcmkONERROR(gckKERNEL_GetGPUAddress(
            Hardware->kernel,
            Hardware->functionLogical,
            gcvFALSE,
            Hardware->functionPhysical,
            &Hardware->functionAddress
            ));
    }
    else
#endif
    {
        /* Allocate a command buffer. */
        gcmkONERROR(gckOS_AllocateNonPagedMemory(
            os,
            gcvFALSE,
            &Hardware->functionBytes,
            &Hardware->functionPhysical,
            &Hardware->functionLogical
            ));

        gcmkONERROR(gckOS_GetPhysicalAddress(
            os,
            Hardware->functionLogical,
            &physical
            ));

        gcmkSAFECASTPHYSADDRT(Hardware->functionAddress, physical);

        gcmkONERROR(gckMMU_FillFlatMapping(
            Hardware->kernel->mmu,
            Hardware->functionAddress,
            Hardware->functionBytes
            ));
    }

    gcmkONERROR(gckHARDWARE_End(
        Hardware,
        gcvNULL,
        &endBytes
        ));

    if (Hardware->mmuVersion > 0)
    {
        function = &Hardware->functions[gcvHARDWARE_FUNCTION_MMU];

        /* MMU configure command sequence. */
        function->logical = logical = (gctUINT8_PTR)Hardware->functionLogical + offset;

        function->address = Hardware->functionAddress + offset;

        gcmkONERROR(gckHARDWARE_SetMMUStates(
            Hardware,
            Hardware->kernel->mmu->mtlbLogical,
            gcvMMU_MODE_4K,
            Hardware->kernel->mmu->safePageLogical,
            logical,
            &mmuBytes
            ));

        offset += mmuBytes;

        logical = (gctUINT8_PTR)Hardware->functionLogical + offset;

        gcmkONERROR(gckHARDWARE_End(
            Hardware,
            logical,
            &endBytes
            ));

#if USE_KERNEL_VIRTUAL_BUFFERS
        if (Hardware->kernel->virtualCommandBuffer)
        {
            gcmkONERROR(gckKERNEL_GetGPUAddress(
                Hardware->kernel,
                logical,
                gcvFALSE,
                Hardware->functionPhysical,
                &Hardware->lastEnd
                ));
        }
#endif

        offset += endBytes;

        function->bytes = mmuBytes + endBytes;

        function->endAddress = function->address + mmuBytes;
        function->endLogical = function->logical + mmuBytes;
    }

    /*
    ** All cache flush command sequence.
    */
    function = &Hardware->functions[gcvHARDWARE_FUNCTION_FLUSH];

    function->logical = logical = (gctUINT8_PTR)Hardware->functionLogical + offset;

    function->address = Hardware->functionAddress + offset;

    /* Get the size of the flush command. */
    gcmkONERROR(gckHARDWARE_Flush(Hardware, gcvFLUSH_ALL, gcvNULL, &flushBytes));

    /* Append a flush. */
    gcmkONERROR(gckHARDWARE_Flush(Hardware, gcvFLUSH_ALL, logical, &flushBytes));

    offset += flushBytes;

    logical = (gctUINT8_PTR)Hardware->functionLogical + offset;

    gcmkONERROR(gckHARDWARE_End(Hardware, logical, &endBytes));

#if USE_KERNEL_VIRTUAL_BUFFERS
    if (Hardware->kernel->virtualCommandBuffer)
    {
        gcmkONERROR(gckKERNEL_GetGPUAddress(
            Hardware->kernel,
            logical,
            gcvFALSE,
            Hardware->functionPhysical,
            &Hardware->lastEnd
            ));
    }
#endif

    offset += endBytes;

    function->bytes = flushBytes + endBytes;

    function->endAddress = function->address + flushBytes;
    function->endLogical = function->logical + flushBytes;

    /*
    ** BLT Engine event command
    */
    if (gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_BLT_ENGINE))
    {
        function = &Hardware->functions[gcvHARDWARE_FUNCTION_BLT_EVENT];

        function->logical = logical = (gctUINT8_PTR)Hardware->functionLogical + offset;
        function->address = Hardware->functionAddress + offset;

        gcmkONERROR(gckHARDWARE_Event(Hardware, gcvNULL, 0, gcvKERNEL_BLT, &eventBytes));

        for (i = 0; i < 29; i++)
        {
            gcmkONERROR(gckHARDWARE_Event(
                Hardware,
                logical + i * eventBytes,
                i,
                gcvKERNEL_BLT,
                &eventBytes
                ));

            offset += eventBytes;
        }

        function->bytes = eventBytes * 29;
    }

    gcmkASSERT(offset < Hardware->functionBytes);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckHARDWARE_AddressInHardwareFuncions(
    IN gckHARDWARE Hardware,
    IN gctUINT32 Address,
    OUT gctPOINTER *Pointer
    )
{
    if (Address >= Hardware->functionAddress && Address <= Hardware->functionAddress - 1 + Hardware->functionBytes)
    {
        *Pointer = (gctUINT8_PTR)Hardware->functionLogical
                 + (Address - Hardware->functionAddress)
                 ;

        return gcvSTATUS_OK;
    }

    return gcvSTATUS_NOT_FOUND;
}

gceSTATUS
gckHARDWARE_QueryStateTimer(
    IN gckHARDWARE Hardware,
    OUT gctUINT64_PTR Start,
    OUT gctUINT64_PTR End,
    OUT gctUINT64_PTR On,
    OUT gctUINT64_PTR Off,
    OUT gctUINT64_PTR Idle,
    OUT gctUINT64_PTR Suspend
    )
{
    gckOS_AcquireMutex(Hardware->os, Hardware->powerMutex, gcvINFINITE);

    gckSTATETIMER_Query(
        &Hardware->powerStateTimer, Hardware->chipPowerState, Start, End, On, Off, Idle, Suspend);

    gckOS_ReleaseMutex(Hardware->os, Hardware->powerMutex);

    return gcvSTATUS_OK;
}

gceSTATUS
gckHARDWARE_WaitFence(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gctUINT64 FenceData,
    IN gctUINT32 FenceAddress,
    OUT gctUINT32 *Bytes
    )
{
    gctUINT32_PTR logical = (gctUINT32_PTR)Logical;

    gctUINT32 dataLow = (gctUINT32)FenceData;
    gctUINT32 dataHigh = (gctUINT32)(FenceData >> 32);

    if (logical)
    {
        *logical++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E26) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

        *logical++
            = dataHigh;

        *logical++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x01FA) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)));

        *logical++
            = dataLow;

        *logical++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x0F & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (Hardware->waitCount) & ((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 17:16) - (0 ? 17:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:16) - (0 ? 17:16) + 1))))))) << (0 ?
 17:16))) | (((gctUINT32) (0x2 & ((gctUINT32) ((((1 ? 17:16) - (0 ? 17:16) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 17:16) - (0 ? 17:16) + 1))))))) << (0 ? 17:16)));

        *logical++
            = FenceAddress;
    }
    else
    {
        *Bytes = 6 * gcmSIZEOF(gctUINT32);
    }

    return gcvSTATUS_OK;
}

gceSTATUS
gckHARDWARE_UpdateContextID(
    IN gckHARDWARE Hardware
    )
{
    static gcsiDEBUG_REGISTERS fe = { "FE", 0x470, 0, 0x450, 256, 0x1, 0x00 };
    gckOS os = Hardware->os;
    gceCORE core = Hardware->core;
    gctUINT32 contextIDLow, contextIDHigh;
    gceSTATUS status;

    gcmkONERROR(gckOS_WriteRegisterEx(os, core, fe.index, 0x53 << fe.shift));
    gcmkONERROR(gckOS_ReadRegisterEx(os, core, fe.data, &contextIDLow));

    gcmkONERROR(gckOS_WriteRegisterEx(os, core, fe.index, 0x54 << fe.shift));
    gcmkONERROR(gckOS_ReadRegisterEx(os, core, fe.data, &contextIDHigh));

    Hardware->contextID = ((gctUINT64)contextIDHigh << 32) + contextIDLow;

    return gcvSTATUS_OK;

OnError:
    return status;
}

gceSTATUS
gckFE_Initialize(
    IN gckHARDWARE Hardware,
    OUT gckFE FE
    )
{
    gceSTATUS status;
    gctUINT32 data;

    gcmkHEADER();

    gckOS_ZeroMemory(FE, gcmSIZEOF(gcsFE));

    gcmkVERIFY_OK(gckOS_ReadRegisterEx(
        Hardware->os,
        Hardware->core,
        0x007E4,
       &data
        ));

    gcmkONERROR(gckOS_AtomConstruct(Hardware->os, &FE->freeDscriptors));

    data = (((((gctUINT32) (data)) >> (0 ? 6:0)) & ((gctUINT32) ((((1 ? 6:0) - (0 ? 6:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:0) - (0 ? 6:0) + 1)))))) );

    gcmkTRACE_ZONE(gcvLEVEL_INFO, _GC_OBJ_ZONE, "free descriptor=%d", data);

    gcmkONERROR(gckOS_AtomSet(Hardware->os, FE->freeDscriptors, data));

    /* Enable interrupts. */
    gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x000D8, ~0U);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:

    if (FE->freeDscriptors)
    {
        gckOS_AtomDestroy(Hardware->os, FE->freeDscriptors);
    }

    gcmkFOOTER();
    return status;
}

void
gckFE_UpdateAvaiable(
    IN gckHARDWARE Hardware,
    OUT gckFE FE
    )
{
    gctUINT32 data;
    gctINT32 oldValue;

    gcmkVERIFY_OK(gckOS_ReadRegisterEx(
        Hardware->os,
        Hardware->core,
        0x007E4,
        &data
        ));

    data = (((((gctUINT32) (data)) >> (0 ? 6:0)) & ((gctUINT32) ((((1 ? 6:0) - (0 ? 6:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:0) - (0 ? 6:0) + 1)))))) );

    while (data--)
    {
        gckOS_AtomIncrement(Hardware->os, FE->freeDscriptors, &oldValue);
    }
}

gceSTATUS
gckFE_ReserveSlot(
    IN gckHARDWARE Hardware,
    IN gckFE FE,
    OUT gctBOOL * Available
    )
{
    gctINT32 oldValue;

    gckOS_AtomDecrement(Hardware->os, FE->freeDscriptors, &oldValue);

    if (oldValue > 0)
    {
        /* Get one slot. */
        *Available = gcvTRUE;
    }
    else
    {
        /* No available slot, restore decreased one.*/
        gckOS_AtomIncrement(Hardware->os, FE->freeDscriptors, &oldValue);
        *Available = gcvFALSE;
    }

    return gcvSTATUS_OK;
}

void
gckFE_Execute(
    IN gckHARDWARE Hardware,
    IN gckFE FE,
    IN gcsFEDescriptor * Desc
    )
{
    gckOS_WriteRegisterEx(
        Hardware->os,
        Hardware->core,
        0x007DC,
        Desc->start
        );

    gckOS_WriteRegisterEx(
        Hardware->os,
        Hardware->core,
        0x007E0,
        Desc->end
        );
}

gceSTATUS
gckHARDWARE_DummyDraw(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gctUINT32 Address,
    IN gceDUMMY_DRAW_TYPE DummyDrawType,
    IN OUT gctUINT32 * Bytes
    )
{
    gctUINT32 dummyDraw_gc400[] = {

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0193) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        0x000000,

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0194) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        0,

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0180) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:0) - (0 ?
 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ?
 3:0))) | (((gctUINT32) (0x8 & ((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 13:12) - (0 ?
 13:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 13:12) - (0 ? 13:12) + 1))))))) << (0 ?
 13:12))) | (((gctUINT32) ((gctUINT32) (4) & ((gctUINT32) ((((1 ? 13:12) - (0 ?
 13:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 13:12) - (0 ? 13:12) + 1))))))) << (0 ?
 13:12)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ?
 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ?
 23:16)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ?
 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24))) | (((gctUINT32) ((gctUINT32) (4 * gcmSIZEOF(float)) & ((gctUINT32) ((((1 ?
 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ?
 31:24)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:7) - (0 ?
 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ?
 7:7))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 7:7) - (0 ? 7:7) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ? 7:7))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E05) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:0) - (0 ?
 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ?
 1:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ? 1:0))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0202) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 5:0) - (0 ?
 5:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:0) - (0 ? 5:0) + 1))))))) << (0 ?
 5:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 5:0) - (0 ?
 5:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:0) - (0 ? 5:0) + 1))))))) << (0 ?
 5:0))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0208) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 5:0) - (0 ?
 5:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:0) - (0 ? 5:0) + 1))))))) << (0 ?
 5:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 5:0) - (0 ?
 5:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:0) - (0 ? 5:0) + 1))))))) << (0 ?
 5:0))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0201) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 5:0) - (0 ?
 5:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:0) - (0 ? 5:0) + 1))))))) << (0 ?
 5:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 5:0) - (0 ?
 5:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:0) - (0 ? 5:0) + 1))))))) << (0 ?
 5:0))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0204) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 5:0) - (0 ?
 5:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:0) - (0 ? 5:0) + 1))))))) << (0 ?
 5:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 5:0) - (0 ?
 5:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:0) - (0 ? 5:0) + 1))))))) << (0 ?
 5:0))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x1000) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (4) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        0x0, 0x0, 0x0, 0x0,
        0xDEADDEAD,

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0203) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 6:0) - (0 ?
 6:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:0) - (0 ? 6:0) + 1))))))) << (0 ?
 6:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 6:0) - (0 ?
 6:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:0) - (0 ? 6:0) + 1))))))) << (0 ?
 6:0))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x020E) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        0,

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0200) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        1,

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x020C) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        0x000F003F,

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x028C) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 11:8) - (0 ?
 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1))))))) << (0 ?
 11:8))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 11:8) - (0 ?
 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1))))))) << (0 ?
 11:8))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0500) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:0) - (0 ?
 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ?
 1:0))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ? 1:0))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x028D) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 13:12) - (0 ?
 13:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 13:12) - (0 ? 13:12) + 1))))))) << (0 ?
 13:12))) | (((gctUINT32) (0x2 & ((gctUINT32) ((((1 ? 13:12) - (0 ? 13:12) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 13:12) - (0 ? 13:12) + 1))))))) << (0 ? 13:12)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:8) - (0 ?
 9:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:8) - (0 ? 9:8) + 1))))))) << (0 ?
 9:8))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 9:8) - (0 ? 9:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 9:8) - (0 ? 9:8) + 1))))))) << (0 ? 9:8)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 17:16) - (0 ?
 17:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:16) - (0 ? 17:16) + 1))))))) << (0 ?
 17:16))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 17:16) - (0 ? 17:16) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 17:16) - (0 ? 17:16) + 1))))))) << (0 ? 17:16))),


        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0300) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        0,

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0301) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        0,

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0302) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        0,

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0303) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        0,

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0289) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:0) - (0 ?
 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ?
 3:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x05 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:0) - (0 ?
 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ?
 3:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:0) - (0 ?
 23:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:0) - (0 ? 23:0) + 1))))))) << (0 ?
 23:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:0) - (0 ?
 23:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:0) - (0 ? 23:0) + 1))))))) << (0 ?
 23:0))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:0) - (0 ?
 23:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:0) - (0 ? 23:0) + 1))))))) << (0 ?
 23:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 23:0) - (0 ?
 23:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:0) - (0 ? 23:0) + 1))))))) << (0 ?
 23:0))),
        };

    gctUINT32 dummyDraw_v60[] = {

        /* Semaphore from FE to PE. */
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ?
 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))),

        /* Stall from FE to PE. */
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ?
 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x021A) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) ((gctUINT32) (0x0) & ((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E06) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:0) - (0 ?
 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ?
 1:0))) | (((gctUINT32) ((gctUINT32) (0x0) & ((gctUINT32) ((((1 ? 1:0) - (0 ?
 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ?
 1:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 14:12) - (0 ? 14:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 14:12) - (0 ? 14:12) + 1))))))) << (0 ?
 14:12))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 14:12) - (0 ? 14:12) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 14:12) - (0 ? 14:12) + 1))))))) << (0 ? 14:12)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 17:16) - (0 ? 17:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:16) - (0 ? 17:16) + 1))))))) << (0 ?
 17:16))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 17:16) - (0 ? 17:16) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 17:16) - (0 ? 17:16) + 1))))))) << (0 ? 17:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 7:4) - (0 ? 7:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:4) - (0 ? 7:4) + 1))))))) << (0 ?
 7:4))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 7:4) - (0 ?
 7:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:4) - (0 ? 7:4) + 1))))))) << (0 ?
 7:4))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0401) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (6) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        0x0,
        0x2,
        0x0,
        0x0,
        0x0,
        0x0,
        (gctUINT32)~0x0,

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x020C) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        0xffffffff,

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E07) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        2,

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E08) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        2,

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0420) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 2:0) - (0 ?
 2:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:0) - (0 ? 2:0) + 1))))))) << (0 ?
 2:0))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 2:0) - (0 ?
 2:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:0) - (0 ? 2:0) + 1))))))) << (0 ?
 2:0))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0424) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        1,

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0403) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        3,

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E21) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 2:0) - (0 ?
 2:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:0) - (0 ? 2:0) + 1))))))) << (0 ?
 2:0))) | (((gctUINT32) (0x2 & ((gctUINT32) ((((1 ? 2:0) - (0 ? 2:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 2:0) - (0 ? 2:0) + 1))))))) << (0 ? 2:0))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x040A) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        0,

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x2000) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1 << 2) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        0x07801033,0x3fc00900,0x00000040,0x00390008,
        (gctUINT32)~0,

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x021F) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        0x0,

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0240) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:0) - (0 ?
 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ?
 1:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ? 1:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 6:4) - (0 ? 6:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:4) - (0 ? 6:4) + 1))))))) << (0 ?
 6:4))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 6:4) - (0 ? 6:4) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 6:4) - (0 ? 6:4) + 1))))))) << (0 ? 6:4)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 8:8) - (0 ? 8:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:8) - (0 ? 8:8) + 1))))))) << (0 ?
 8:8))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 8:8) - (0 ? 8:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 8:8) - (0 ? 8:8) + 1))))))) << (0 ? 8:8)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 26:24) - (0 ? 26:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 26:24) - (0 ? 26:24) + 1))))))) << (0 ?
 26:24))) | (((gctUINT32) (0x3 & ((gctUINT32) ((((1 ? 26:24) - (0 ? 26:24) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 26:24) - (0 ? 26:24) + 1))))))) << (0 ? 26:24))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0241) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (31) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:16) - (0 ? 31:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:16) - (0 ? 31:16) + 1))))))) << (0 ?
 31:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 31:16) - (0 ?
 31:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:16) - (0 ? 31:16) + 1))))))) << (0 ?
 31:16))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0244) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:0) - (0 ?
 9:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:0) - (0 ? 9:0) + 1))))))) << (0 ?
 9:0))) | (((gctUINT32) ((gctUINT32) (31) & ((gctUINT32) ((((1 ? 9:0) - (0 ?
 9:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:0) - (0 ? 9:0) + 1))))))) << (0 ?
 9:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 31:16) - (0 ? 31:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:16) - (0 ? 31:16) + 1))))))) << (0 ?
 31:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 31:16) - (0 ?
 31:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:16) - (0 ? 31:16) + 1))))))) << (0 ?
 31:16))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0247) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),

        (32+(4*(((gcsFEATURE_DATABASE *)Hardware->featureDatabase)->NumShaderCores)-1))/(4*(((gcsFEATURE_DATABASE *)Hardware->featureDatabase)->NumShaderCores)),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0248) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        1,

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E03) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 5:5) - (0 ?
 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ?
 5:5))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5))),

        /* Semaphore from FE to PE. */
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ?
 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))),

        /* Stall from FE to PE. */
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))),

        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ?
 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ?
 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ?
 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))),

        /* Invalidate I cache.*/
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ?
 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ?
 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ?
 ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0))) | (((gctUINT32) ((gctUINT32) (0x022C) & ((gctUINT32) ((((1 ? 15:0) - (0 ?
 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ?
 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ?
 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ?
 25:16))),
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 0:0) - (0 ?
 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ?
 0:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 1:1) - (0 ?
 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ?
 1:1)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ?
 2:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 2:2) - (0 ?
 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ?
 2:2)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ?
 3:3))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 3:3) - (0 ?
 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ?
 3:3)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ?
 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ?
 4:4))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 4:4) - (0 ?
 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ?
 4:4))),
    };

    gctUINT32 bytes = 0;
    gctUINT32_PTR dummyDraw = gcvNULL;


    switch(DummyDrawType)
    {
    case gcvDUMMY_DRAW_GC400:
        dummyDraw = dummyDraw_gc400;
        bytes = gcmSIZEOF(dummyDraw_gc400);
        *(dummyDraw + 1) = Address;
        break;
    case gcvDUMMY_DRAW_V60:
        dummyDraw = dummyDraw_v60;
        bytes = gcmSIZEOF(dummyDraw_v60);
        break;
    default:
        /* other chip no need dummy draw.*/
        gcmkASSERT(0);
        break;
    };

    if (Logical != gcvNULL)
    {
        gckOS_MemCopy(Logical, dummyDraw, bytes);
    }

    *Bytes = bytes;

    return gcvSTATUS_OK;
}


