/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2015 Vivante Corporation
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
*    Copyright (C) 2014 - 2015 Vivante Corporation
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

#define gcdDISABLE_FE_L2    1

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
    gctUINT32       signature;
}
gcsiDEBUG_REGISTERS;

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

static gceSTATUS
_IdentifyHardware(
    IN gckOS Os,
    IN gceCORE Core,
    OUT gcsHAL_QUERY_CHIP_IDENTITY_PTR Identity
    )
{
    gceSTATUS status;

    gctUINT32 chipIdentity;

    gctUINT32 streamCount = 0;
    gctUINT32 registerMax = 0;
    gctUINT32 threadCount = 0;
    gctUINT32 shaderCoreCount = 0;
    gctUINT32 vertexCacheSize = 0;
    gctUINT32 vertexOutputBufferSize = 0;
    gctUINT32 pixelPipes = 0;
    gctUINT32 instructionCount = 0;
    gctUINT32 numConstants = 0;
    gctUINT32 bufferSize = 0;
    gctUINT32 varyingsCount = 0;
#if gcdMULTI_GPU
    gctUINT32 gpuCoreCount = 0;
#endif
    gctUINT32 debugControl0 = 0;

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
    if (((((gctUINT32) (chipIdentity)) >> (0 ? 31:24) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1)))))) == (0x01 & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))))
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

        if ((Identity->chipModel == gcv2000) && (Identity->chipRevision & 0xffff0000) == 0xffff0000)
        {
            Identity->chipModel = gcv3000;
            Identity->chipRevision &= 0xffff;
            Identity->chipFlags |= gcvCHIP_FLAG_GC2000_R2;
        }

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


    /***************************************************************************
    ** Get chip features.
    */

    /* Read chip feature register. */
    gcmkONERROR(
        gckOS_ReadRegisterEx(Os, Core,
                             0x0001C,
                             &Identity->chipFeatures));

#if gcdENABLE_3D
    /* Disable fast clear on GC700. */
    if (Identity->chipModel == gcv700)
    {
        Identity->chipFeatures
            = ((((gctUINT32) (Identity->chipFeatures)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));
    }
#endif

    if (((Identity->chipModel == gcv500) && (Identity->chipRevision < 2))
    ||  ((Identity->chipModel == gcv300) && (Identity->chipRevision < 0x2000))
    )
    {
        /* GC500 rev 1.x and GC300 rev < 2.0 doesn't have these registers. */
        Identity->chipMinorFeatures  = 0;
        Identity->chipMinorFeatures1 = 0;
        Identity->chipMinorFeatures2 = 0;
        Identity->chipMinorFeatures3 = 0;
        Identity->chipMinorFeatures4 = 0;
        Identity->chipMinorFeatures5 = 0;
        Identity->chipMinorFeatures6 = 0;
    }
    else
    {
        /* Read chip minor feature register #0. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Os, Core,
                                 0x00034,
                                 &Identity->chipMinorFeatures));

        if (((((gctUINT32) (Identity->chipMinorFeatures)) >> (0 ? 21:21) & ((gctUINT32) ((((1 ? 21:21) - (0 ? 21:21) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:21) - (0 ? 21:21) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 21:21) - (0 ? 21:21) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:21) - (0 ? 21:21) + 1)))))))
        )
        {
            /* Read chip minor featuress register #1. */
            gcmkONERROR(
                gckOS_ReadRegisterEx(Os, Core,
                                     0x00074,
                                     &Identity->chipMinorFeatures1));

            /* Read chip minor featuress register #2. */
            gcmkONERROR(
                gckOS_ReadRegisterEx(Os, Core,
                                     0x00084,
                                     &Identity->chipMinorFeatures2));

            /*Identity->chipMinorFeatures2 &= ~(0x1 << 3);*/

            /* Read chip minor featuress register #1. */
            gcmkONERROR(
                gckOS_ReadRegisterEx(Os, Core,
                                     0x00088,
                                     &Identity->chipMinorFeatures3));


            /* Read chip minor featuress register #4. */
            gcmkONERROR(
                gckOS_ReadRegisterEx(Os, Core,
                                     0x00094,
                                     &Identity->chipMinorFeatures4));

            /* Read chip minor featuress register #5. */
            gcmkONERROR(
                gckOS_ReadRegisterEx(Os, Core,
                                     0x000A0,
                                     &Identity->chipMinorFeatures5));

            /* Read chip minor featuress register #6. */
            gcmkONERROR(
                gckOS_ReadRegisterEx(Os, Core,
                                     0x000DC,
                                     &Identity->chipMinorFeatures6));
        }
        else
        {
            /* Chip doesn't has minor features register #1 or 2 or 3 or 4. */
            Identity->chipMinorFeatures1 = 0;
            Identity->chipMinorFeatures2 = 0;
            Identity->chipMinorFeatures3 = 0;
            Identity->chipMinorFeatures4 = 0;
            Identity->chipMinorFeatures5 = 0;
            Identity->chipMinorFeatures6 = 0;
        }
    }

    /* Get the Supertile layout in the hardware. */
    if (((((gctUINT32) (Identity->chipMinorFeatures3)) >> (0 ? 26:26) & ((gctUINT32) ((((1 ? 26:26) - (0 ? 26:26) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 26:26) - (0 ? 26:26) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 26:26) - (0 ? 26:26) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 26:26) - (0 ? 26:26) + 1)))))))
     || ((((gctUINT32) (Identity->chipMinorFeatures3)) >> (0 ? 8:8) & ((gctUINT32) ((((1 ? 8:8) - (0 ? 8:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:8) - (0 ? 8:8) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 8:8) - (0 ? 8:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:8) - (0 ? 8:8) + 1))))))))
    {
        Identity->superTileMode = 2;
    }
    else if (((((gctUINT32) (Identity->chipMinorFeatures)) >> (0 ? 27:27) & ((gctUINT32) ((((1 ? 27:27) - (0 ? 27:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:27) - (0 ? 27:27) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 27:27) - (0 ? 27:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:27) - (0 ? 27:27) + 1))))))))
    {
        Identity->superTileMode = 1;
    }
    else
    {
        Identity->superTileMode = 0;
    }

    /* Exception for GC1000, revision 5035 &  GC800, revision 4612 */
    if (((Identity->chipModel == gcv1000) && ((Identity->chipRevision == 0x5035)
                                           || (Identity->chipRevision == 0x5036)
                                           || (Identity->chipRevision == 0x5037)
                                           || (Identity->chipRevision == 0x5039)
                                           || (Identity->chipRevision >= 0x5040)))
    || ((Identity->chipModel == gcv800) && (Identity->chipRevision == 0x4612))
	|| ((Identity->chipModel == gcv880) && (Identity->chipRevision == 0x5124))
    || ((Identity->chipModel == gcv600) && (Identity->chipRevision >= 0x4650))
    || ((Identity->chipModel == gcv860) && (Identity->chipRevision == 0x4647))
    || ((Identity->chipModel == gcv400) && (Identity->chipRevision >= 0x4633)))
    {
        Identity->superTileMode = 1;
    }

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Identity: chipFeatures=0x%08X",
                   Identity->chipFeatures);

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Identity: chipMinorFeatures=0x%08X",
                   Identity->chipMinorFeatures);

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Identity: chipMinorFeatures1=0x%08X",
                   Identity->chipMinorFeatures1);

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Identity: chipMinorFeatures2=0x%08X",
                   Identity->chipMinorFeatures2);

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Identity: chipMinorFeatures3=0x%08X",
                   Identity->chipMinorFeatures3);

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Identity: chipMinorFeatures4=0x%08X",
                   Identity->chipMinorFeatures4);

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Identity: chipMinorFeatures5=0x%08X",
                   Identity->chipMinorFeatures5);

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Identity: chipMinorFeatures6=0x%08X",
                   Identity->chipMinorFeatures6);

    /***************************************************************************
    ** Get chip specs.
    */

    if (((((gctUINT32) (Identity->chipMinorFeatures)) >> (0 ? 21:21) & ((gctUINT32) ((((1 ? 21:21) - (0 ? 21:21) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:21) - (0 ? 21:21) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 21:21) - (0 ? 21:21) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:21) - (0 ? 21:21) + 1))))))))
    {
        gctUINT32 specs, specs2, specs3, specs4;

        /* Read gcChipSpecs register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Os, Core,
                                 0x00048,
                                 &specs));

        /* Extract the fields. */
        registerMax            = (((((gctUINT32) (specs)) >> (0 ? 7:4)) & ((gctUINT32) ((((1 ? 7:4) - (0 ? 7:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:4) - (0 ? 7:4) + 1)))))) );
        threadCount            = (((((gctUINT32) (specs)) >> (0 ? 11:8)) & ((gctUINT32) ((((1 ? 11:8) - (0 ? 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1)))))) );
        shaderCoreCount        = (((((gctUINT32) (specs)) >> (0 ? 24:20)) & ((gctUINT32) ((((1 ? 24:20) - (0 ? 24:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 24:20) - (0 ? 24:20) + 1)))))) );
        vertexCacheSize        = (((((gctUINT32) (specs)) >> (0 ? 16:12)) & ((gctUINT32) ((((1 ? 16:12) - (0 ? 16:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:12) - (0 ? 16:12) + 1)))))) );
        vertexOutputBufferSize = (((((gctUINT32) (specs)) >> (0 ? 31:28)) & ((gctUINT32) ((((1 ? 31:28) - (0 ? 31:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:28) - (0 ? 31:28) + 1)))))) );
        pixelPipes             = (((((gctUINT32) (specs)) >> (0 ? 27:25)) & ((gctUINT32) ((((1 ? 27:25) - (0 ? 27:25) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:25) - (0 ? 27:25) + 1)))))) );

        /* Read gcChipSpecs2 register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Os, Core,
                                 0x00080,
                                 &specs2));

        instructionCount       = (((((gctUINT32) (specs2)) >> (0 ? 15:8)) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1)))))) );
        numConstants           = (((((gctUINT32) (specs2)) >> (0 ? 31:16)) & ((gctUINT32) ((((1 ? 31:16) - (0 ? 31:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:16) - (0 ? 31:16) + 1)))))) );
        bufferSize             = (((((gctUINT32) (specs2)) >> (0 ? 7:0)) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1)))))) );

        /* Read gcChipSpecs3 register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Os, Core,
                                 0x0008C,
                                 &specs3));

        varyingsCount          = (((((gctUINT32) (specs3)) >> (0 ? 8:4)) & ((gctUINT32) ((((1 ? 8:4) - (0 ? 8:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:4) - (0 ? 8:4) + 1)))))) );
#if gcdMULTI_GPU
        gpuCoreCount           = (((((gctUINT32) (specs3)) >> (0 ? 2:0)) & ((gctUINT32) ((((1 ? 2:0) - (0 ? 2:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:0) - (0 ? 2:0) + 1)))))) );
#endif

        /* Read gcChipSpecs4 register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Os, Core,
                                 0x0009C,
                                 &specs4));


        streamCount            = (((((gctUINT32) (specs4)) >> (0 ? 16:12)) & ((gctUINT32) ((((1 ? 16:12) - (0 ? 16:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:12) - (0 ? 16:12) + 1)))))) );
        if (streamCount == 0)
        {
            /* Extract stream count from older register. */
            streamCount        = (((((gctUINT32) (specs)) >> (0 ? 3:0)) & ((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1)))))) );
        }

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "Identity: chipSpecs1=0x%08X",
                       specs);

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "Identity: chipSpecs2=0x%08X",
                       specs2);

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "Identity: chipSpecs3=0x%08X",
                       specs3);

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "Identity: chipSpecs4=0x%08X",
                       specs4);
    }

    /* Get the number of pixel pipes. */
    Identity->pixelPipes = gcmMAX(pixelPipes, 1);

    /* Get the stream count. */
    Identity->streamCount = (streamCount != 0)
                          ? streamCount
                          : (Identity->chipModel >= gcv1000) ? 4 : 1;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: streamCount=%u%s",
                   Identity->streamCount,
                   (streamCount == 0) ? " (default)" : "");

    /* Get the vertex output buffer size. */
    Identity->vertexOutputBufferSize = (vertexOutputBufferSize != 0)
                                     ? 1 << vertexOutputBufferSize
                                     : (Identity->chipModel == gcv400)
                                       ? (Identity->chipRevision < 0x4000) ? 512
                                       : (Identity->chipRevision < 0x4200) ? 256
                                       : 128
                                     : (Identity->chipModel == gcv530)
                                       ? (Identity->chipRevision < 0x4200) ? 512
                                       : 128
                                     : 512;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: vertexOutputBufferSize=%u%s",
                   Identity->vertexOutputBufferSize,
                   (vertexOutputBufferSize == 0) ? " (default)" : "");

    /* Get the maximum number of threads. */
    Identity->threadCount = (threadCount != 0)
                          ? 1 << threadCount
                          : (Identity->chipModel == gcv400) ? 64
                          : (Identity->chipModel == gcv500) ? 128
                          : (Identity->chipModel == gcv530) ? 128
                          : 256;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: threadCount=%u%s",
                   Identity->threadCount,
                   (threadCount == 0) ? " (default)" : "");

    /* Get the number of shader cores. */
    Identity->shaderCoreCount = (shaderCoreCount != 0)
                              ? shaderCoreCount
                              : (Identity->chipModel >= gcv1000) ? 2
                              : 1;
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: shaderCoreCount=%u%s",
                   Identity->shaderCoreCount,
                   (shaderCoreCount == 0) ? " (default)" : "");

    /* Get the vertex cache size. */
    Identity->vertexCacheSize = (vertexCacheSize != 0)
                              ? vertexCacheSize
                              : 8;
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: vertexCacheSize=%u%s",
                   Identity->vertexCacheSize,
                   (vertexCacheSize == 0) ? " (default)" : "");

    /* Get the maximum number of temporary registers. */
    Identity->registerMax = (registerMax != 0)
        /* Maximum of registerMax/4 registers are accessible to 1 shader */
                          ? 1 << registerMax
                          : (Identity->chipModel == gcv400) ? 32
                          : 64;
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: registerMax=%u%s",
                   Identity->registerMax,
                   (registerMax == 0) ? " (default)" : "");

    /* Get the instruction count. */
    Identity->instructionCount = (instructionCount == 0) ? 256
                               : (instructionCount == 1) ? 1024
                               : (instructionCount == 2) ? 2048
                               : (instructionCount == 0xFF) ? 512
                               : 256;

    if (Identity->instructionCount == 256)
    {
        if ((Identity->chipModel == gcv2000 && Identity->chipRevision == 0x5108)
        ||  Identity->chipModel == gcv880)
        {
            Identity->instructionCount = 512;
        }
        else if (((((gctUINT32) (Identity->chipMinorFeatures3)) >> (0 ? 3:3) & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))))
        {
            Identity->instructionCount = 512;
        }
    }

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: instructionCount=%u%s",
                   Identity->instructionCount,
                   (instructionCount == 0) ? " (default)" : "");

    /* Get the number of constants. */
    Identity->numConstants = (numConstants == 0) ? 168 : numConstants;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: numConstants=%u%s",
                   Identity->numConstants,
                   (numConstants == 0) ? " (default)" : "");

    /* Get the buffer size. */
    Identity->bufferSize = bufferSize;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: bufferSize=%u%s",
                   Identity->bufferSize,
                   (bufferSize == 0) ? " (default)" : "");


     if (varyingsCount != 0)
     {
         Identity->varyingsCount = varyingsCount;
     }
     else if (((((gctUINT32) (Identity->chipMinorFeatures1)) >> (0 ? 23:23) & ((gctUINT32) ((((1 ? 23:23) - (0 ? 23:23) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:23) - (0 ? 23:23) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 23:23) - (0 ? 23:23) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:23) - (0 ? 23:23) + 1))))))))
     {
         Identity->varyingsCount = 12;
     }
     else
     {
         Identity->varyingsCount = 8;
     }

     /* For some cores, it consumes two varying for position, so the max varying vectors should minus one. */
     if ((Identity->chipModel == gcv5000 && Identity->chipRevision == 0x5434) ||
         (Identity->chipModel == gcv4000 && Identity->chipRevision == 0x5222) ||
         (Identity->chipModel == gcv4000 && Identity->chipRevision == 0x5208) ||
         (Identity->chipModel == gcv4000 && Identity->chipRevision == 0x5245) ||
         (Identity->chipModel == gcv3000 && Identity->chipRevision == 0x5435) ||
         (Identity->chipModel == gcv2200 && Identity->chipRevision == 0x5244) ||
         (Identity->chipModel == gcv1500 && Identity->chipRevision == 0x5246) ||
         ((Identity->chipModel == gcv2100 || Identity->chipModel == gcv2000) && Identity->chipRevision == 0x5108) ||
         (Identity->chipModel == gcv880 && (Identity->chipRevision == 0x5107 || Identity->chipRevision == 0x5106)))
     {
         Identity->varyingsCount -= 1;
     }

    Identity->chip2DControl = 0;
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
            Identity->chip2DControl |= 0xFF &
                (Identity->chipRevision == 0x5220 ? 8 :
                (Identity->chipRevision == 0x5007 ? 12 : 0));
        }

        if  (Identity->chipRevision == 0x5007)
        {
            /* Disable splitting rectangle. */
            Identity->chip2DControl |= 0x100;

            /* Enable 2D Flush. */
            Identity->chip2DControl |= 0x200;
        }
    }

#if gcdMULTI_GPU
#if gcdMULTI_GPU > 1
     Identity->gpuCoreCount = gpuCoreCount + 1;
#else
     Identity->gpuCoreCount = 1;
#endif
#endif

    gcmkONERROR(
            gckOS_ReadRegisterEx(Os, Core,
                                 0x00470,
                                 &debugControl0));

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

#define gcdDEBUG_MODULE_CLOCK_GATING   0
#define gcdDISABLE_MODULE_CLOCK_GATING 0
#define gcdDISABLE_FE_CLOCK_GATING     0
#define gcdDISABLE_PE_CLOCK_GATING     0
#define gcdDISABLE_SH_CLOCK_GATING     0
#define gcdDISABLE_PA_CLOCK_GATING     0
#define gcdDISABLE_SE_CLOCK_GATING     0
#define gcdDISABLE_RA_CLOCK_GATING     0
#define gcdDISABLE_RA_EZ_CLOCK_GATING  0
#define gcdDISABLE_RA_HZ_CLOCK_GATING  0
#define gcdDISABLE_TX_CLOCK_GATING     0

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
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));
#endif

#if gcdDISABLE_PE_CLOCK_GATING
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2)));
#endif

#if gcdDISABLE_SH_CLOCK_GATING
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3)));
#endif

#if gcdDISABLE_PA_CLOCK_GATING
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4)));
#endif

#if gcdDISABLE_SE_CLOCK_GATING
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5)));
#endif

#if gcdDISABLE_RA_CLOCK_GATING
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6)));
#endif

#if gcdDISABLE_TX_CLOCK_GATING
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ? 7:7))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ? 7:7)));
#endif

#if gcdDISABLE_RA_EZ_CLOCK_GATING
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ? 16:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ? 16:16)));
#endif

#if gcdDISABLE_RA_HZ_CLOCK_GATING
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 17:17) - (0 ? 17:17) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:17) - (0 ? 17:17) + 1))))))) << (0 ? 17:17))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 17:17) - (0 ? 17:17) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:17) - (0 ? 17:17) + 1))))))) << (0 ? 17:17)));
#endif

    gcmkVERIFY_OK(
        gckOS_WriteRegisterEx(Hardware->os,
                              Hardware->core,
                              Hardware->powerBaseAddress
                              + 0x00104,
                              data));

#if gcdDISABLE_MODULE_CLOCK_GATING
    gcmkVERIFY_OK(
        gckOS_ReadRegisterEx(Hardware->os,
                             Hardware->core,
                             Hardware->powerBaseAddress +
                             0x00100,
                             &data));

    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));


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

    gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x660, State1));
    gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x664, Address1));

    for (i = 0; i < 500; i += 1)
    {
        gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x660, State2));
        gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x664, Address2));

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
    gceSTATUS status = gcvSTATUS_OK;
    gctUINT32 select;
    gctUINT32 data = 0;
    gctUINT i;

    gcmkHEADER_ARG("Os=0x%X Descriptor=0x%X", Os, Descriptor);

    gcmkPRINT_N(4, "    %s debug registers:\n", Descriptor->module);

    for (i = 0; i < Descriptor->count; i += 1)
    {
        select = i << Descriptor->shift;

        gcmkONERROR(gckOS_WriteRegisterEx(Os, Core, Descriptor->index, select));
#if gcdFPGA_BUILD
        gcmkONERROR(gckOS_Delay(Os, 1000));
#endif
        gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, Descriptor->data, &data));

        gcmkPRINT_N(12, "      [0x%02X] 0x%08X\n", i, data);
    }

    select = 0xF << Descriptor->shift;

    for (i = 0; i < 500; i += 1)
    {
        gcmkONERROR(gckOS_WriteRegisterEx(Os, Core, Descriptor->index, select));
#if gcdFPGA_BUILD
        gcmkONERROR(gckOS_Delay(Os, 1000));
#endif
        gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, Descriptor->data, &data));

        if (data == Descriptor->signature)
        {
            break;
        }
    }

    if (i == 500)
    {
        gcmkPRINT_N(4, "      failed to obtain the signature (read 0x%08X).\n", data);
    }
    else
    {
        gcmkPRINT_N(8, "      signature = 0x%08X (%d read attempt(s))\n", data, i + 1);
    }

OnError:
    /* Return the error. */
    gcmkFOOTER();
    return status;
}

static gceSTATUS
_IsGPUPresent(
    IN gckHARDWARE Hardware
    )
{
    gceSTATUS status;
    gcsHAL_QUERY_CHIP_IDENTITY identity;
    gctUINT32 control;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x00000,
                                     &control));

    control = ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1)));
    control = ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00000,
                                      control));

    /* Identify the hardware. */
    gcmkONERROR(_IdentifyHardware(Hardware->os,
                                  Hardware->core,
                                  &identity));

    /* Check if these are the same values as saved before. */
    if ((Hardware->identity.chipModel          != identity.chipModel)
    ||  (Hardware->identity.chipRevision       != identity.chipRevision)
    ||  (Hardware->identity.chipFeatures       != identity.chipFeatures)
    ||  (Hardware->identity.chipMinorFeatures  != identity.chipMinorFeatures)
    ||  (Hardware->identity.chipMinorFeatures1 != identity.chipMinorFeatures1)
    ||  (Hardware->identity.chipMinorFeatures2 != identity.chipMinorFeatures2)
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

gctBOOL
_IsGPUIdle(
    IN gctUINT32 Idle
    )
{
   return  (((((gctUINT32) (Idle)) >> (0 ? 0:0)) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1)))))) )
        && (((((gctUINT32) (Idle)) >> (0 ? 1:1)) & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1)))))) )
        && (((((gctUINT32) (Idle)) >> (0 ? 3:3)) & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1)))))) )
        && (((((gctUINT32) (Idle)) >> (0 ? 4:4)) & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1)))))) )
        && (((((gctUINT32) (Idle)) >> (0 ? 5:5)) & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1)))))) )
        && (((((gctUINT32) (Idle)) >> (0 ? 6:6)) & ((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1)))))) )
        && (((((gctUINT32) (Idle)) >> (0 ? 7:7)) & ((gctUINT32) ((((1 ? 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1)))))) )
        && (((((gctUINT32) (Idle)) >> (0 ? 2:2)) & ((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1)))))) )
        ;
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
#if gcdMULTI_GPU_AFFINITY
    gctUINT32 control;
#endif

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

    /* Identify the hardware. */
    gcmkONERROR(_IdentifyHardware(Os, Core, &hardware->identity));

    /* Determine the hardware type */
    switch (hardware->identity.chipModel)
    {
    case gcv350:
    case gcv355:
        hardware->type = gcvHARDWARE_VG;
        break;

    case gcv200:
    case gcv300:
    case gcv320:
    case gcv328:
    case gcv420:
    case gcv428:
    case gcv520:
        hardware->type = gcvHARDWARE_2D;
        break;

    default:
#if gcdMULTI_GPU_AFFINITY
        hardware->type = (Core == gcvCORE_MAJOR) ? gcvHARDWARE_3D : gcvHARDWARE_OCL;
#else
        hardware->type = gcvHARDWARE_3D;
#endif

        if(hardware->identity.chipModel == gcv880 && hardware->identity.chipRevision == 0x5107)
        {
            /*set outstanding limit*/
            gctUINT32 axi_ot;
            gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x00414, &axi_ot));
            axi_ot = (axi_ot & (~0xFF)) | 0x00010;
            gcmkONERROR(gckOS_WriteRegisterEx(Os, Core, 0x00414, axi_ot));
        }


        if ((((((gctUINT32) (hardware->identity.chipFeatures)) >> (0 ? 9:9)) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1)))))) ))
        {
            hardware->type = (gceHARDWARE_TYPE) (hardware->type | gcvHARDWARE_2D);
        }
    }

    hardware->powerBaseAddress
        = ((hardware->identity.chipModel   == gcv300)
        && (hardware->identity.chipRevision < 0x2000))
            ? 0x0100
            : 0x0000;

    if (!(_IsHardwareMatch(hardware, gcv3000, 0x5450) && (hardware->identity.chipFlags & gcvCHIP_FLAG_GC2000_R2)))
    {
        /* _ResetGPU need powerBaseAddress. */
        status = _ResetGPU(hardware, Os, Core);

        if (status != gcvSTATUS_OK)
        {
            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                "_ResetGPU failed: status=%d\n", status);
        }
    }

#if gcdENABLE_DEC_COMPRESSION && !gcdDEC_ENABLE_AHB
    gcmkONERROR(gckOS_WriteRegisterEx(Os, gcvCORE_DEC, 0x18180, ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 22:22) - (0 ? 22:22) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 22:22) - (0 ? 22:22) + 1))))))) << (0 ? 22:22))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 22:22) - (0 ? 22:22) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 22:22) - (0 ? 22:22) + 1))))))) << (0 ? 22:22)))));
#endif

#if gcdMULTI_GPU
    gcmkONERROR(gckOS_WriteRegisterEx(Os,
                                      Core,
                                      0x0055C,
#if gcdDISABLE_FE_L2
                                      0x00FFFFFF));
#else
                                      0x00FFFF05));
#endif

#elif gcdMULTI_GPU_AFFINITY
    control = ((((gctUINT32) (0x00FF0A05)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:27) - (0 ? 27:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:27) - (0 ? 27:27) + 1))))))) << (0 ? 27:27))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 27:27) - (0 ? 27:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:27) - (0 ? 27:27) + 1))))))) << (0 ? 27:27)));

    gcmkONERROR(gckOS_WriteRegisterEx(Os,
                                      Core,
                                      0x0055C,
                                      control));
#endif

    hardware->powerMutex = gcvNULL;

    hardware->mmuVersion
        = (((((gctUINT32) (hardware->identity.chipMinorFeatures1)) >> (0 ? 28:28)) & ((gctUINT32) ((((1 ? 28:28) - (0 ? 28:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 28:28) - (0 ? 28:28) + 1)))))) );

    /* Determine whether bug fixes #1 are present. */
    hardware->extraEventStates = ((((gctUINT32) (hardware->identity.chipMinorFeatures1)) >> (0 ? 3:3) & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1)))))) == (0x0 & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1)))))));

    /* Check if big endian */
    hardware->bigEndian = (*(gctUINT8 *)&data == 0xff);

    /* Initialize the fast clear. */
    gcmkONERROR(gckHARDWARE_SetFastClear(hardware, -1, -1));

#if !gcdENABLE_128B_MERGE

    if (((((gctUINT32) (hardware->identity.chipMinorFeatures2)) >> (0 ? 21:21) & ((gctUINT32) ((((1 ? 21:21) - (0 ? 21:21) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:21) - (0 ? 21:21) + 1)))))) == (0x1  & ((gctUINT32) ((((1 ? 21:21) - (0 ? 21:21) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:21) - (0 ? 21:21) + 1))))))))
    {
        /* 128B merge is turned on by default. Disable it. */
        gcmkONERROR(gckOS_WriteRegisterEx(Os, Core, 0x00558, 0));
    }

#endif

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

#if gcdLINK_QUEUE_SIZE
    hardware->linkQueue.front = 0;
    hardware->linkQueue.rear = 0;
    hardware->linkQueue.count = 0;
#endif

    /* Enable power management by default. */
    hardware->powerManagement = gcvTRUE;

    /* Disable profiler by default */
    hardware->gpuProfiler = gcvFALSE;

#if defined(LINUX) || defined(__QNXNTO__) || defined(UNDERCE)
    if (hardware->mmuVersion)
    {
        hardware->endAfterFlushMmuCache = gcvTRUE;
    }
    else
#endif
    {
        hardware->endAfterFlushMmuCache = gcvFALSE;
    }

    gcmkONERROR(gckOS_QueryOption(Os, "mmu", (gctUINT32_PTR)&hardware->enableMMU));

    hardware->minFscaleValue = 1;

    gckSTATETIMER_Reset(&hardware->powerStateTimer, 0);

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

    gcmkVERIFY_OK(gckOS_FreeNonPagedMemory(
        Hardware->os,
        Hardware->functionBytes,
        Hardware->functionPhysical,
        Hardware->functionLogical
        ));

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
    gctUINT32 baseAddress;
    gctUINT32 chipRev;
    gctUINT32 control;
    gctUINT32 data;
    gctUINT32 regPMC = 0;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Read the chip revision register. */
    gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x00024,
                                     &chipRev));

    chipRev &= 0xffff;

    if (chipRev != Hardware->identity.chipRevision)
    {
        /* Chip is not there! */
        gcmkONERROR(gcvSTATUS_CONTEXT_LOSSED);
    }

    /* Disable isolate GPU bit. */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00000,
                                      ((((gctUINT32) (0x00000900)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ? 19:19))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ? 19:19)))));

    gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x00000,
                                     &control));

    /* Enable debug register. */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00000,
                                      ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 11:11) - (0 ? 11:11) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:11) - (0 ? 11:11) + 1))))))) << (0 ? 11:11))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 11:11) - (0 ? 11:11) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:11) - (0 ? 11:11) + 1))))))) << (0 ? 11:11)))));

    /* Reset memory counters. */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x0003C,
                                      ~0U));

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x0003C,
                                      0));

    /* Get the system's physical base address. */
    gcmkONERROR(gckOS_GetBaseAddress(Hardware->os, &baseAddress));

    /* Program the base addesses. */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x0041C,
                                      baseAddress));

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00418,
                                      baseAddress));

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00428,
                                      baseAddress));

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00420,
                                      baseAddress));

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00424,
                                      baseAddress));

    {
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         Hardware->powerBaseAddress +
                                         0x00100,
                                         &data));

        /* Enable clock gating. */
        data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));

        if ((Hardware->identity.chipRevision == 0x4301)
        ||  (Hardware->identity.chipRevision == 0x4302)
        )
        {
            /* Disable stall module level clock gating for 4.3.0.1 and 4.3.0.2
            ** revisions. */
            data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1)));
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
        &&  ((((gctUINT32) (Hardware->identity.chipMinorFeatures1)) >> (0 ? 9:9) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1)))))) == (0x0 & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1)))))))
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
            regPMC = ((((gctUINT32) (regPMC)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2)));
        }

#endif
    }

    if (Hardware->identity.chipModel == gcv4000 &&
        ((Hardware->identity.chipRevision == 0x5208) || (Hardware->identity.chipRevision == 0x5222)))
    {
        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  0x0010C,
                                  ((((gctUINT32) (0x01590880)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:23) - (0 ? 23:23) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:23) - (0 ? 23:23) + 1))))))) << (0 ? 23:23))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 23:23) - (0 ? 23:23) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:23) - (0 ? 23:23) + 1))))))) << (0 ? 23:23)))));
    }

    if (Hardware->identity.chipModel == gcv1000 &&
        (Hardware->identity.chipRevision == 0x5039 ||
        Hardware->identity.chipRevision == 0x5040))
    {
        gctUINT32 pulseEater;

        pulseEater = ((((gctUINT32) (0x01590880)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ? 16:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ? 16:16)));

        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  0x0010C,
                                  ((((gctUINT32) (pulseEater)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 17:17) - (0 ? 17:17) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:17) - (0 ? 17:17) + 1))))))) << (0 ? 17:17))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 17:17) - (0 ? 17:17) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:17) - (0 ? 17:17) + 1))))))) << (0 ? 17:17)))));
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

        regPMC = ((((gctUINT32) (regPMC)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:15) - (0 ? 15:15) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:15) - (0 ? 15:15) + 1))))))) << (0 ? 15:15))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 15:15) - (0 ? 15:15) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:15) - (0 ? 15:15) + 1))))))) << (0 ? 15:15)));
    }

    if (_IsHardwareMatch(Hardware, gcv2000, 0x5108))
    {
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 0x00480,
                                 &data));

        /* Set FE bus to one, TX bus to zero */
        data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3)));
        data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ? 7:7))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ? 7:7)));

        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  0x00480,
                                  data));
    }

    gcmkONERROR(
        gckHARDWARE_SetMMU(Hardware,
                           Hardware->kernel->mmu->pageTableLogical));

    if (Hardware->identity.chipModel >= gcv400
    &&  Hardware->identity.chipModel != gcv420
    &&  (((((gctUINT32) (Hardware->identity.chipMinorFeatures3)) >> (0 ? 15:15) & ((gctUINT32) ((((1 ? 15:15) - (0 ? 15:15) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:15) - (0 ? 15:15) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 15:15) - (0 ? 15:15) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:15) - (0 ? 15:15) + 1))))))) != gcvTRUE)
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

        /* Disable PA clock gating. */
        regPMC = ((((gctUINT32) (regPMC)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4)));
    }

    /* Limit 2D outstanding request. */
    if (_IsHardwareMatch(Hardware, gcv880, 0x5107))
    {
        gctUINT32 axi_ot;
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00414, &axi_ot));
        axi_ot = (axi_ot & (~0xFF)) | 0x00010;
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00414, axi_ot));
    }

    if (Hardware->identity.chip2DControl & 0xFF)
    {
        gctUINT32 data;

        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 0x00414,
                                 &data));

        data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (Hardware->identity.chip2DControl & 0xFF) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0)));

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
        data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ? 12:12))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ? 12:12)));

        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  0x00414,
                                  data));
    }

    if (_IsHardwareMatch(Hardware, gcv4000, 0x5222)
     || _IsHardwareMatch(Hardware, gcv2000, 0x5108)
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
        regPMC = ((((gctUINT32) (regPMC)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ? 7:7))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ? 7:7)));
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
    regPMC = ((((gctUINT32) (regPMC)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 17:17) - (0 ? 17:17) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:17) - (0 ? 17:17) + 1))))))) << (0 ? 17:17))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 17:17) - (0 ? 17:17) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:17) - (0 ? 17:17) + 1))))))) << (0 ? 17:17)));

    /* Disable RA EZ clock gating. */
    regPMC = ((((gctUINT32) (regPMC)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ? 16:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ? 16:16)));

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
     || (_IsHardwareMatch(Hardware, gcv3000, 0x5450) && (Hardware->identity.chipFlags & gcvCHIP_FLAG_GC2000_R2))
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
        data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 18:18) - (0 ? 18:18) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 18:18) - (0 ? 18:18) + 1))))))) << (0 ? 18:18))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 18:18) - (0 ? 18:18) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 18:18) - (0 ? 18:18) + 1))))))) << (0 ? 18:18)));

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x0010C,
                                      data));
    }

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
    gctUINT32 features;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Identity != gcvNULL);

    /* Return chip model and revision. */
    Identity->chipModel = Hardware->identity.chipModel;
    Identity->chipRevision = Hardware->identity.chipRevision;

    /* Return feature set. */
    features = Hardware->identity.chipFeatures;

    if ((((((gctUINT32) (features)) >> (0 ? 0:0)) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1)))))) ))
    {
        /* Override fast clear by command line. */
        features = ((((gctUINT32) (features)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (Hardware->allowFastClear) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));
    }

    if ((((((gctUINT32) (features)) >> (0 ? 5:5)) & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1)))))) ))
    {
        /* Override compression by command line. */
        features = ((((gctUINT32) (features)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5))) | (((gctUINT32) ((gctUINT32) (Hardware->allowCompression) & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5)));
    }

    /* Mark 2D pipe as available for GC500.0 through GC500.2 and GC300,
    ** since they did not have this bit. */
    if (((Hardware->identity.chipModel == gcv500) && (Hardware->identity.chipRevision <= 2))
    ||   (Hardware->identity.chipModel == gcv300)
    )
    {
        features = ((((gctUINT32) (features)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9)));
    }

    Identity->chipFeatures = features;

    /* Return minor features. */
    Identity->chipMinorFeatures  = Hardware->identity.chipMinorFeatures;
    Identity->chipMinorFeatures1 = Hardware->identity.chipMinorFeatures1;
    Identity->chipMinorFeatures2 = Hardware->identity.chipMinorFeatures2;
    Identity->chipMinorFeatures3 = Hardware->identity.chipMinorFeatures3;
    Identity->chipMinorFeatures4 = Hardware->identity.chipMinorFeatures4;
    Identity->chipMinorFeatures5 = Hardware->identity.chipMinorFeatures5;
    Identity->chipMinorFeatures6 = Hardware->identity.chipMinorFeatures6;

    /* Return chip specs. */
    Identity->streamCount            = Hardware->identity.streamCount;
    Identity->registerMax            = Hardware->identity.registerMax;
    Identity->threadCount            = Hardware->identity.threadCount;
    Identity->shaderCoreCount        = Hardware->identity.shaderCoreCount;
    Identity->vertexCacheSize        = Hardware->identity.vertexCacheSize;
    Identity->vertexOutputBufferSize = Hardware->identity.vertexOutputBufferSize;
    Identity->pixelPipes             = Hardware->identity.pixelPipes;
    Identity->instructionCount       = Hardware->identity.instructionCount;
    Identity->numConstants           = Hardware->identity.numConstants;
    Identity->bufferSize             = Hardware->identity.bufferSize;
    Identity->varyingsCount          = Hardware->identity.varyingsCount;
    Identity->superTileMode          = Hardware->identity.superTileMode;
#if gcdMULTI_GPU
    Identity->gpuCoreCount           = Hardware->identity.gpuCoreCount;
#endif
    Identity->chip2DControl          = Hardware->identity.chip2DControl;

    Identity->productID              = Hardware->identity.productID;
    Identity->chipFlags               = Hardware->identity.chipFlags;

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
    control = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ? 16:16))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ? 16:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) ((Bytes + 7) >> 3) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

    /* Set big endian */
    if (Hardware->bigEndian)
    {
        control |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 21:20) - (0 ? 21:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:20) - (0 ? 21:20) + 1))))))) << (0 ? 21:20))) | (((gctUINT32) (0x2 & ((gctUINT32) ((((1 ? 21:20) - (0 ? 21:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:20) - (0 ? 21:20) + 1))))))) << (0 ? 21:20)));
    }

    /* Write control register. */
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00658, control));

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
    IN gctUINT32 Offset,
    IN OUT gctUINT32 * Bytes,
    OUT gctUINT32 * WaitOffset,
    OUT gctUINT32 * WaitSize
    )
{
    static const gctUINT waitCount = 200;

    gceSTATUS status;
    gctUINT32 address;
    gctUINT32_PTR logical;
    gctUINT32 bytes;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x Offset=0x%08x *Bytes=%lu",
                   Hardware, Logical, Offset, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT((Logical != gcvNULL) || (Bytes != gcvNULL));

#if gcdMULTI_GPU && !gcdDISABLE_FE_L2
    bytes = gcmALIGN(Offset + 40, 8) - Offset;
#else
    /* Compute number of bytes required. */
    bytes = gcmALIGN(Offset + 16, 8) - Offset;
#endif
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

        /* Convert logical into hardware specific address. */
        gcmkONERROR(gckHARDWARE_ConvertLogical(Hardware, logical, gcvFALSE, &address));

        /* Store the WAIT/LINK address. */
        Hardware->lastWaitLink = address;

        /* Append WAIT(count). */
        logical[0]
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (waitCount) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

#if gcdMULTI_GPU && !gcdDISABLE_FE_L2
        logical[2] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x0D & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                   | gcvCORE_3D_0_MASK;

        logical[3] = 0;

        /* LoadState(AQFlush, 1), flush. */
        logical[4] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                   | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E03) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
                   | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

        logical[5] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6)));

        logical[6] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x0D & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                   | gcvCORE_3D_ALL_MASK;

        logical[7] = 0;

        /* Append LINK(2, address). */
        logical[8] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x08 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                   | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (bytes >> 3) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        logical[9] = address;

        gcmkTRACE_ZONE(
            gcvLEVEL_INFO, gcvZONE_HARDWARE,
            "0x%08x: WAIT %u", address, waitCount
            );

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                        "0x%x: FLUSH 0x%x", address + 8, logical[3]);

        gcmkTRACE_ZONE(
            gcvLEVEL_INFO, gcvZONE_HARDWARE,
            "0x%08x: LINK 0x%08x, #%lu",
            address + 16, address, bytes
            );
#else

        /* Append LINK(2, address). */
        logical[2]
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x08 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (bytes >> 3) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        logical[3] = address;

        gcmkTRACE_ZONE(
            gcvLEVEL_INFO, gcvZONE_HARDWARE,
            "0x%08x: WAIT %u", address, waitCount
            );

        gcmkTRACE_ZONE(
            gcvLEVEL_INFO, gcvZONE_HARDWARE,
            "0x%08x: LINK 0x%08x, #%lu",
            address + 8, address, bytes
            );
#endif
        if (WaitOffset != gcvNULL)
        {
            /* Return the offset pointer to WAIT command. */
            *WaitOffset = 0;
        }

        if (WaitSize != gcvNULL)
        {
            /* Return number of bytes used by the WAIT command. */
#if gcdMULTI_GPU && !gcdDISABLE_FE_L2
            *WaitSize = 32;
#else
            *WaitSize = 8;
#endif
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
            ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x02 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

        /* Record the count of execution which is finised by this END. */
        logical[1] =
            Hardware->executeCount;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE, "0x%x: END", Logical);

        /* Make sure the CPU writes out the data to memory. */
        gcmkONERROR(
            gckOS_MemoryBarrier(Hardware->os, Logical));

        gcmkONERROR(gckHARDWARE_ConvertLogical(Hardware, logical, gcvFALSE, &address));

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

#if gcdMULTI_GPU
gceSTATUS
gckHARDWARE_ChipEnable(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gceCORE_3D_MASK ChipEnable,
    IN OUT gctSIZE_T * Bytes
    )
{
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
        logical[0] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x0D & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                   | ChipEnable;

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
#endif

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
        logical[0] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x03 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

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

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x Event=%u FromWhere=%d *Bytes=%lu",
                   Hardware, Logical, Event, FromWhere, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT((Logical == gcvNULL) || (Bytes != gcvNULL));
    gcmkVERIFY_ARGUMENT(Event < 32);

#if gcdMULTI_GPU
    if (FromWhere == gcvKERNEL_COMMAND) FromWhere = gcvKERNEL_PIXEL;
#endif

    /* Determine the size of the command. */

    size = (Hardware->extraEventStates && (FromWhere == gcvKERNEL_PIXEL))
         ? gcmALIGN(8 + (1 + 5) * 4, 8) /* EVENT + 5 STATES */
         : 8;

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
            destination = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5)));
            break;

        case gcvKERNEL_PIXEL:
            /* From pixel engine. */
            destination = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6)));
            break;

        default:
            gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }

        /* Append EVENT(Event, destiantion). */
        logical[0] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                   | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E01) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
                   | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

        logical[1] = ((((gctUINT32) (destination)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) ((gctUINT32) (Event) & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)));

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
        if (size > 8)
        {
            logical[2] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                       | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0100) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
                       | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (5) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));
            logical[3] = 0;
            logical[4] = 0;
            logical[5] = 0;
            logical[6] = 0;
            logical[7] = 0;
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
              ? ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1)))
              | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))
              : ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3)));

        stall = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
              | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* LoadState(AQFlush, 1), flush. */
        logical[0]
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E03) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

        logical[1]
            = flush;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "0x%x: FLUSH 0x%x", logical, flush);

        /* LoadState(AQSempahore, 1), stall. */
        logical[2]
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        logical[3]
            = stall;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "0x%x: SEMAPHORE 0x%x", logical + 2, stall);

        /* Stall, stall. */
        logical[4] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));
        logical[5] = stall;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "0x%x: STALL 0x%x", logical + 4, stall);

        /* LoadState(AQPipeSelect, 1), pipe. */
        logical[6]
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E00) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

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
        link = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x08 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
             | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (bytes >> 3) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

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
#if gcdMULTI_GPU
    gcmkONERROR(
        gckKERNEL_Notify(Hardware->kernel,
                         0,
                         gcvNOTIFY_COMMAND_QUEUE,
                         gcvFALSE));
#else
    gcmkONERROR(
        gckKERNEL_Notify(Hardware->kernel,
                         gcvNOTIFY_COMMAND_QUEUE,
                         gcvFALSE));
#endif

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
    gctUINT32 baseAddress;
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
        gcmkONERROR(gckOS_GetBaseAddress(Hardware->os, &baseAddress));

        /* Subtract base address to get a GPU address. */
        gcmkASSERT(address >= baseAddress);
        address -= baseAddress;
    }

    /* Return hardware specific address. */
    *Address = (Hardware->mmuVersion == 0)
             ? ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:31) - (0 ? 31:31) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1))))))) << (0 ? 31:31))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 31:31) - (0 ? 31:31) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1))))))) << (0 ? 31:31)))
               | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 30:0) - (0 ? 30:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 30:0) - (0 ? 30:0) + 1))))))) << (0 ? 30:0))) | (((gctUINT32) ((gctUINT32) (address) & ((gctUINT32) ((((1 ? 30:0) - (0 ? 30:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 30:0) - (0 ? 30:0) + 1))))))) << (0 ? 30:0)))
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
#if gcdMULTI_GPU
    IN gctUINT CoreId,
#endif
    IN gctBOOL InterruptValid
    )
{
    gckEVENT eventObj;
    gctUINT32 data = 0;
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
#if gcdMULTI_GPU
        if (Hardware->core == gcvCORE_MAJOR)
        {
            gcmkONERROR(
                gckOS_ReadRegisterByCoreId(Hardware->os,
                                           Hardware->core,
                                           CoreId,
                                           0x00010,
                                           &data));
        }
        else
        {
            gcmkONERROR(
                gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x00010,
                                     &data));
        }
#else
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 0x00010,
                                 &data));
#endif

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
#if gcdMULTI_GPU
                                        CoreId,
#endif
                                        data);
        }
    }
    else
    {
            /* Handle events. */
            status = gckEVENT_Notify(eventObj, 0);
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
        /* Reserve space for Link(). */
        *ReservedTail = 8;
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
        *SystemBaseAddress = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:31) - (0 ? 31:31) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1))))))) << (0 ? 31:31))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 31:31) - (0 ? 31:31) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1))))))) << (0 ? 31:31)));
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

    gcmkHEADER_ARG("Hardware=0x%x VertexUniforms=0x%x "
                   "FragmentUniforms=0x%x UnifiedUnforms=0x%x",
                   Hardware, VertexUniforms,
                   FragmentUniforms, UnifiedUnforms);

    {if (Hardware->identity.numConstants > 256){    unifiedConst = gcvTRUE;    vsConstBase  = 0xC000;    psConstBase  = 0xC000;    vsConstMax   = gcmMIN(512, Hardware->identity.numConstants - 64);    psConstMax   = gcmMIN(512, Hardware->identity.numConstants - 64);    ConstMax     = Hardware->identity.numConstants;}else if (Hardware->identity.numConstants == 256){    if (Hardware->identity.chipModel == gcv2000 && Hardware->identity.chipRevision == 0x5118)    {        unifiedConst = gcvFALSE;        vsConstBase  = 0x1400;        psConstBase  = 0x1C00;        vsConstMax   = 256;        psConstMax   = 64;        ConstMax     = 320;    }    else    {        unifiedConst = gcvFALSE;        vsConstBase  = 0x1400;        psConstBase  = 0x1C00;        vsConstMax   = 256;        psConstMax   = 256;        ConstMax     = 512;    }}else{    unifiedConst = gcvFALSE;    vsConstBase  = 0x1400;    psConstBase  = 0x1C00;    vsConstMax   = 168;    psConstMax   = 64;    ConstMax     = 232;}};

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
    else if (Hardware->enableMMU == gcvTRUE)
    {
        /* Prepared command sequence contains an END,
        ** so update lastEnd and store executeCount to END command.
        */
        gcsHARDWARE_FUNCTION *function = &Hardware->functions[gcvHARDWARE_FUNCTION_MMU];
        gctUINT32_PTR endLogical = (gctUINT32_PTR)function->endLogical;

        Hardware->lastEnd = function->endAddress;

        *(endLogical + 1) = Hardware->executeCount + 1;

        /* Execute prepared command sequence. */
        gcmkONERROR(gckHARDWARE_Execute(
            Hardware,
            function->address,
            function->bytes
            ));

#if gcdLINK_QUEUE_SIZE
        gckLINKQUEUE_Enqueue(
            &Hardware->linkQueue,
            function->address,
            function->address + function->bytes,
            0,
            0
            );
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
        gcmkONERROR(gckOS_WriteRegisterEx(
            Hardware->os,
            Hardware->core,
            0x0018C,
            ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (gcvTRUE) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))
            ));
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
    gctUINT32 count;
    gctPHYS_ADDR_T physical;
    gctUINT32 address;

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
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E04) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

        buffer[1]
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4)));

        gcmkONERROR(gckCOMMAND_Execute(command, 8));
    }
    else
    {
        /* semaphore stall cmd size */
        gctUINT32 stCmds = gckHARDWARE_IsFeatureAvailable(Hardware, gcvFEATURE_TEX_CACHE_FLUSH_FIX) ? 0 : 4;

        flushSize =  (stCmds + 18) * 4;

        gcmkONERROR(gckCOMMAND_Reserve(
            command, flushSize, &pointer, &bufferSize
            ));

        buffer = (gctUINT32_PTR) pointer;

        count = ((gctUINT)bufferSize - flushSize + 7) >> 3;

        gcmkONERROR(gckOS_GetPhysicalAddress(command->os, buffer, &physical));

        gcmkSAFECASTPHYSADDRT(address, physical);

        if (stCmds)
        {
            /* Arm the PE-FE Semaphore. */
            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

            /* STALL FE until PE is done flushing. */
            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));
        }

        /* Flush cache. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E03) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6)));

        /* Flush VTS in separate command */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E03) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4)));

        /* Arm the PE-FE Semaphore. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* STALL FE until PE is done flushing. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* LINK to next slot to flush FE FIFO. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x08 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (4) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        *buffer++
            = address + (stCmds + 10) * gcmSIZEOF(gctUINT32);

        /* Flush MMU cache. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0061) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

        *buffer++
            = (((((gctUINT32) (~0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4))) &  ((((gctUINT32) (~0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ? 7:7))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ? 7:7))));

        /* Arm the PE-FE Semaphore. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* STALL FE until PE is done flushing. */
       *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

       *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* LINK to next slot to flush FE FIFO. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x08 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (count) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

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

    gctBOOL config2D;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Hardware->mmuVersion != 0);

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

    switch (Mode)
    {
    case gcvMMU_MODE_1K:
        if (config & 0x3FF)
        {
            gcmkONERROR(gcvSTATUS_NOT_ALIGNED);
        }

        config |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));

        break;

    case gcvMMU_MODE_4K:
        if (config & 0xFFF)
        {
            gcmkONERROR(gcvSTATUS_NOT_ALIGNED);
        }

        config |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));

        break;

    default:
        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    if (ace)
    {
        configEx = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (extSafeAddrss) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0)))
                 | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (extMtlb) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16)));
    }

    if (Logical != gcvNULL)
    {
        buffer = Logical;

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0061) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

        *buffer++ = config;

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0060) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

        *buffer++ = address;

        if (ace)
        {
            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0068) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

            *buffer++
                = configEx;
        }

        do{*buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))); *buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))); *buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))); *buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));} while(0);;

        if (config2D)
        {
            /* LoadState(AQPipeSelect, 1), pipe. */
            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E00) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

            *buffer++ = 0x1;

            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0061) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

            *buffer++ = config;

            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0060) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

            *buffer++ = address;

            if (ace)
            {
                *buffer++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0068) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

                *buffer++
                    = configEx;
            }

            do{*buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))); *buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))); *buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))); *buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));} while(0);;

            /* LoadState(AQPipeSelect, 1), pipe. */
            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E00) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

            *buffer++ = 0x0;

            do{*buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))); *buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))); *buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))); *buffer++ = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));} while(0);;
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
    gctUINT32 physical;
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

        /* Get physical address of Master TLB. */
        gcmkONERROR(gckOS_GetPhysicalAddress(Hardware->os, MtlbLogical, &config));

        config |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4)));

        if (stCmds)
        {
            /* Arm the PE-FE Semaphore. */
            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

            /* STALL FE until PE is done flushing. */
            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

            *buffer++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));
        }

        /* Flush cache. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E03) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6)));

        /* Flush VTS in separate command */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E03) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4)));

        /* Flush tile status cache. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0594) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));

        /* Arm the PE-FE Semaphore. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* STALL FE until PE is done flushing. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* LINK to next slot to flush FE FIFO. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x08 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (4) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        *buffer++
            = physical + (stCmds + 12) * gcmSIZEOF(gctUINT32);

        /* Configure MMU. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0061) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

        *buffer++
            = (((((gctUINT32) (~0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4))) &  ((((gctUINT32) (~0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ? 7:7))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ? 7:7))));

        /* Arm the PE-FE Semaphore. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* STALL FE until PE is done flushing. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* LINK to next slot to flush FE FIFO. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x08 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (5) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        *buffer++
            = physical + (stCmds + 20) * 4;

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0061) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

        *buffer++
            = config;

        /* Arm the PE-FE Semaphore. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* STALL FE until PE is done flushing. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* Event 29. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E01) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

        event = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6)));
        event = ((((gctUINT32) (event)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) ((gctUINT32) (29) & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)));

        *buffer++
            = event;

        /* Append END. */
        *buffer++
           = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x02 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));
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
#if gcdMULTI_GPU
        *WaitLinkBytes = 40;
#else
        *WaitLinkBytes = 4 * 4;
#endif
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
    *Address = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:31) - (0 ? 31:31) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1))))))) << (0 ? 31:31))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 31:31) - (0 ? 31:31) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1))))))) << (0 ? 31:31)))
             | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 30:0) - (0 ? 30:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 30:0) - (0 ? 30:0) + 1))))))) << (0 ? 30:0))) | (((gctUINT32) ((gctUINT32) (Offset | (Index << 12)) & ((gctUINT32) ((((1 ? 30:0) - (0 ? 30:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 30:0) - (0 ? 30:0) + 1))))))) << (0 ? 30:0)));

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

    gcmkHEADER_ARG("Hardware=0x%x Flush=0x%x Logical=0x%x *Bytes=%lu",
                   Hardware, Flush, Logical, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Get current pipe. */
    pipe = Hardware->kernel->command->pipeSelect;

    /* Flush tile status cache. */
    flushTileStatus = Flush & gcvFLUSH_TILE_STATUS;

    /* Flush 3D color cache. */
    if ((Flush & gcvFLUSH_COLOR) && (pipe == 0x0))
    {
        flush |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1)));
    }

    /* Flush 3D depth cache. */
    if ((Flush & gcvFLUSH_DEPTH) && (pipe == 0x0))
    {
        flush |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));
    }

    /* Flush 3D texture cache. */
    if ((Flush & gcvFLUSH_TEXTURE) && (pipe == 0x0))
    {
        flush |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2)));
        flushVST = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4)));
    }

    /* Flush 2D cache. */
    if ((Flush & gcvFLUSH_2D) && (pipe == 0x1))
    {
        flush |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3)));
    }

#if gcdMULTI_GPU
    /* Flush L2 cache. */
    if ((Flush & gcvFLUSH_L2) && (pipe == 0x0))
    {
        flush |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6)));
    }
#endif

    /* See if there is a valid flush. */
    if ((flush == 0) && (flushTileStatus == gcvFALSE))
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
        if (!txCacheFix)
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

            if (!txCacheFix)
            {
                /* Semaphore. */
                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

                /* Stall. */
                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));
            }

            if (flush)
            {
                /* Append LOAD_STATE to AQFlush. */
                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E03) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

                *logical++ = flush;

                gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE, "0x%x: FLUSH 0x%x", logical - 1, flush);
            }

            if (flushVST)
            {
                /* Append LOAD_STATE to AQFlush. */
                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E03) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

                *logical++ = flushVST;

                gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE, "0x%x: FLUSH 0x%x", logical - 1, flush);
            }

            if (flushTileStatus)
            {
                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0594) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

                *logical++
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));

                gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                               "0x%x: FLUSH TILE STATUS 0x%x", logical - 1, logical[-1]);
            }

            /* Semaphore. */
            *logical++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

            *logical++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

            /* Stall. */
            *logical++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

            *logical++
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));
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
    if ((((((gctUINT32) (Hardware->identity.chipFeatures)) >> (0 ? 0:0)) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1)))))) ))
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
            Compression = Enable
                        & (((((gctUINT32) (Hardware->identity.chipFeatures)) >> (0 ? 5:5)) & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1)))))) );
        }

        /* Read AQMemoryDebug register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00414, &debug));

        /* Set fast clear bypass. */
        debug = ((((gctUINT32) (debug)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 20:20) - (0 ? 20:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:20) - (0 ? 20:20) + 1))))))) << (0 ? 20:20))) | (((gctUINT32) ((gctUINT32) (Enable == 0) & ((gctUINT32) ((((1 ? 20:20) - (0 ? 20:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:20) - (0 ? 20:20) + 1))))))) << (0 ? 20:20)));

        if (
            ((((gctUINT32) (Hardware->identity.chipMinorFeatures2)) >> (0 ? 27:27) & ((gctUINT32) ((((1 ? 27:27) - (0 ? 27:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:27) - (0 ? 27:27) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 27:27) - (0 ? 27:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:27) - (0 ? 27:27) + 1))))))) ||
            (Hardware->identity.chipModel >= gcv4000))
        {
            /* Set compression bypass. */
            debug = ((((gctUINT32) (debug)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 21:21) - (0 ? 21:21) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:21) - (0 ? 21:21) + 1))))))) << (0 ? 21:21))) | (((gctUINT32) ((gctUINT32) (Compression == 0) & ((gctUINT32) ((((1 ? 21:21) - (0 ? 21:21) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:21) - (0 ? 21:21) + 1))))))) << (0 ? 21:21)));
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
    gctPOINTER buffer;
    gctUINT32 bytes, requested;
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
    gctBOOL commitEntered = gcvFALSE;
    gctBOOL commandStarted = gcvFALSE;
    gctBOOL isrStarted = gcvFALSE;

#if gcdENABLE_PROFILING
    gctUINT64 time, freq, mutexTime, onTime, stallTime, stopTime, delayTime,
              initTime, offTime, startTime, totalTime;
#endif
    gctBOOL global = gcvFALSE;
    gctBOOL globalAcquired = gcvFALSE;
    gctBOOL configMmu = gcvFALSE;

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
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2))) | (((gctUINT32) ((gctUINT32) (64) & ((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))),

        /* gcvPOWER_OFF */
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))),

        /* gcvPOWER_IDLE */
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))),

        /* gcvPOWER_SUSPEND */
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2))) |
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))),
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
                status = gcvSTATUS_INVALID_REQUEST;
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
        clock = ((((gctUINT32) (clock)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2))) | (((gctUINT32) ((gctUINT32) (Hardware->powerOnFscaleVal) & ((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2)));
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
        clock = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) |
                ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) |
                ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2))) |
                ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) ;
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
                gcmkONERROR(gcvSTATUS_INVALID_REQUEST);
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
            /* Acquire the command queue. */
            gcmkONERROR(gckCOMMAND_EnterCommit(command, gcvTRUE));
            commitEntered = gcvTRUE;

            /* Get the size of the flush command. */
            gcmkONERROR(gckHARDWARE_Flush(Hardware,
                                          gcvFLUSH_ALL,
                                          gcvNULL,
                                          &requested));

            /* Reserve space in the command queue. */
            gcmkONERROR(gckCOMMAND_Reserve(command,
                                           requested,
                                           &buffer,
                                           &bytes));

            /* Append a flush. */
            gcmkONERROR(gckHARDWARE_Flush(
                Hardware, gcvFLUSH_ALL, buffer, &bytes
                ));

            /* Execute the command queue. */
            gcmkONERROR(gckCOMMAND_Execute(command, requested));

            /* Release the command queue. */
            gcmkONERROR(gckCOMMAND_ExitCommit(command, gcvTRUE));
            commitEntered = gcvFALSE;

            /* Wait to finish all commands. */
#if gcdMULTI_GPU
            gcmkONERROR(gckCOMMAND_Stall(command, gcvTRUE, gcvCORE_3D_ALL_MASK));
#else
            gcmkONERROR(gckCOMMAND_Stall(command, gcvTRUE));
#endif
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
        gcmkONERROR(gckCOMMAND_Stop(command, gcvFALSE));

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
                                                  ((((gctUINT32) (clocks[0])) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9)))));
            }
        }

        gcmkONERROR(gckCOMMAND_Start(command));

        gcmkONERROR(_FlushCache(Hardware, command));

        gckOS_Delay(gcvNULL, 1);

        /* Stop the command parser. */
        gcmkONERROR(gckCOMMAND_Stop(command, gcvFALSE));

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
                                          ((((gctUINT32) (clock)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9)))));
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

        /* Need to config mmu after command start. */
        configMmu = gcvTRUE;
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
        gcmkVERIFY_OK(gckCOMMAND_Stop(command, gcvFALSE));
    }

    if (isrStarted)
    {
        gcmkVERIFY_OK(Hardware->stopIsr(Hardware->isrContext));
    }

    if (commitEntered)
    {
        /* Release the command queue mutex. */
        gcmkVERIFY_OK(gckCOMMAND_ExitCommit(command, gcvTRUE));
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
    if(!Hardware->powerManagementLock)
    {
        gcmkVERIFY_OK(
            gckOS_AcquireMutex(Hardware->os, Hardware->powerMutex, gcvINFINITE));

        Hardware->powerManagement = PowerManagement;

        gcmkVERIFY_OK(gckOS_ReleaseMutex(Hardware->os, Hardware->powerMutex));
    }
    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckHARDWARE_SetPowerManagementLock
**
**  Disable dynamic GPU power management switch.
**  Only used in driver initialization stage.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**      gctBOOL Lock
**          Power Mangement Lock State.
**
*/
gceSTATUS
gckHARDWARE_SetPowerManagementLock(
    IN gckHARDWARE Hardware,
    IN gctBOOL Lock
    )
{
    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    Hardware->powerManagementLock = Lock;

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

        data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));


        gcmkVERIFY_OK(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  Hardware->powerBaseAddress
                                  + 0x00100,
                                  data));
    }

    Hardware->gpuProfiler = GpuProfiler;

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
                                  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))
                                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1)))
                                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2)))
                                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3)))
                                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4)))
                                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5)))
                                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6)))
                                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ? 7:7))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1))))))) << (0 ? 7:7)))
                                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 8:8) - (0 ? 8:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:8) - (0 ? 8:8) + 1))))))) << (0 ? 8:8))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 8:8) - (0 ? 8:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:8) - (0 ? 8:8) + 1))))))) << (0 ? 8:8)))
                                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9)))
                                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 11:11) - (0 ? 11:11) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:11) - (0 ? 11:11) + 1))))))) << (0 ? 11:11))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 11:11) - (0 ? 11:11) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:11) - (0 ? 11:11) + 1))))))) << (0 ? 11:11)))));

        clock = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))
              | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1)))
              | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2))) | (((gctUINT32) ((gctUINT32) (FscaleValue) & ((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2)))
              | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9)));

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                          Hardware->core,
                                          0x00000,
                                          clock));

        /* Done loading the frequency scaler. */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                          Hardware->core,
                                          0x00000,
                                          ((((gctUINT32) (clock)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9)))));

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
#if gcdMULTI_GPU > 1
    gctUINT32 idle3D1 = 0;
    gctUINT32 address3D1;
    gctBOOL   isIdle3D1 = gcvFALSE;
#endif

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
#if gcdMULTI_GPU > 1
        isIdle3D1 = gcvTRUE;
#endif
    }

    else
    {
        /* Read idle register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00004, &idle));

#if gcdMULTI_GPU > 1
        if (Hardware->core == gcvCORE_MAJOR)
        {
            gcmkONERROR(
                gckOS_ReadRegisterByCoreId(Hardware->os,
                                           Hardware->core,
                                           gcvCORE_3D_1_ID,
                                           0x00004,
                                           &idle3D1));
        }
#endif

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

            /* Test if address is inside the last WAIT/LINK sequence. */
            if ((address >= Hardware->lastWaitLink)
#if gcdMULTI_GPU
            &&  (address <= Hardware->lastWaitLink + 40)
#else
            &&  (address <= Hardware->lastWaitLink + 16)
#endif
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

#if gcdMULTI_GPU > 1
        if (Hardware->core == gcvCORE_MAJOR)
        {
            /* Pipe must be idle. */
            if (((((((gctUINT32) (idle3D1)) >> (0 ? 1:1)) & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1)))))) ) != 1)
                ||  ((((((gctUINT32) (idle3D1)) >> (0 ? 3:3)) & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1)))))) ) != 1)
                ||  ((((((gctUINT32) (idle3D1)) >> (0 ? 4:4)) & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1)))))) ) != 1)
                ||  ((((((gctUINT32) (idle3D1)) >> (0 ? 5:5)) & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1)))))) ) != 1)
                ||  ((((((gctUINT32) (idle3D1)) >> (0 ? 6:6)) & ((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1)))))) ) != 1)
                ||  ((((((gctUINT32) (idle3D1)) >> (0 ? 7:7)) & ((gctUINT32) ((((1 ? 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1)))))) ) != 1)
                ||  ((((((gctUINT32) (idle3D1)) >> (0 ? 2:2)) & ((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1)))))) ) != 1)
            )
            {
                /* Something is busy. */
                isIdle3D1 = gcvFALSE;
            }

            else
            {
                /* Read the current FE address. */
                gcmkONERROR(gckOS_ReadRegisterByCoreId(Hardware->os,
                                                       Hardware->core,
                                                       gcvCORE_3D_1_ID,
                                                       0x00664,
                                                       &address3D1));

                /* Test if address is inside the last WAIT/LINK sequence. */
                if ((address3D1 >= Hardware->lastWaitLink)
                    &&  (address3D1 <= Hardware->lastWaitLink + 40)
                )
                {
                    /* FE is in last WAIT/LINK and the pipe is idle. */
                    isIdle3D1 = gcvTRUE;
                }
                else
                {
                    /* FE is not in WAIT/LINK yet. */
                    isIdle3D1 = gcvFALSE;
                }
            }
        }
#endif

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

#if gcdMULTI_GPU > 1
    if (Hardware->core == gcvCORE_MAJOR)
    {
        *IsIdle = (isIdle & isIdle3D1);
    }
    else
#endif
    {
        *IsIdle = isIdle;
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
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (11) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454, &profiler->pixelsRendered));

        /* Reset counter. */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (15) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16)))
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

    gcmkHEADER_ARG("Hardware=0x%x Counters=0x%x", Hardware, Counters);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    chipModel = Hardware->identity.chipModel;
    chipRevision = Hardware->identity.chipRevision;
    if ((chipModel == gcv5000 && chipRevision == 0x5434) || (chipModel == gcv3000 && chipRevision == 0x5435))
    {
        resetValue = 0xFF;
    }

    /* Read the counters. */
    gcmkONERROR(
        gckOS_ReadRegisterEx(Hardware->os,
                             Hardware->core,
                             0x00438,
                             &profiler->gpuCyclesCounter));

    gcmkONERROR(
        gckOS_ReadRegisterEx(Hardware->os,
                             Hardware->core,
                             0x00078,
                             &profiler->gpuTotalCyclesCounter));

    gcmkONERROR(
        gckOS_ReadRegisterEx(Hardware->os,
                             Hardware->core,
                             0x0007C,
                             &profiler->gpuIdleCyclesCounter));


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
                                          ((((gctUINT32) (clock)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:20) - (0 ? 23:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:20) - (0 ? 23:20) + 1))))))) << (0 ? 23:20))) | (((gctUINT32) ((gctUINT32) (i) & ((gctUINT32) ((((1 ? 23:20) - (0 ? 23:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:20) - (0 ? 23:20) + 1))))))) << (0 ? 23:20)))));

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
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470, ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16)))));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454, &colorKilled));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470, ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16)))));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454, &depthKilled));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470, ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16)))));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454, &colorDrawn));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470, ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16)))));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454, &depthDrawn));

        profiler->pe_pixel_count_killed_by_color_pipe += colorKilled;
        profiler->pe_pixel_count_killed_by_depth_pipe += depthKilled;
        profiler->pe_pixel_count_drawn_by_color_pipe += colorDrawn;
        profiler->pe_pixel_count_drawn_by_depth_pipe += depthDrawn;
    }

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
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16)))
));

    /* SH */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (7) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->ps_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (8) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->rendered_pixel_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (9) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->vs_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (10) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->rendered_vertice_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (11) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->vtx_branch_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (12) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->vtx_texld_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (13) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->pxl_branch_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (14) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->pxl_texld_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24)))
));

    /* PA */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_input_vtx_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (4) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_input_prim_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (5) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_output_prim_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (6) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_depth_clipped_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (7) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_trivial_rejected_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (8) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_culled_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0)))
));

    /* SE */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_culled_triangle_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_culled_lines_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8)))
));

    /* RA */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_valid_pixel_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_total_quad_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_valid_quad_count_after_early_z));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_total_primitive_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (9) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_pipe_cache_miss_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (10) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_prefetch_cache_miss_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16)))
));

    /* TX */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_total_bilinear_requests));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_total_trilinear_requests));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_total_discarded_texture_requests));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_total_texture_requests));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (5) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_mem_read_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (6) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_mem_read_in_8B_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (7) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_cache_miss_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (8) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_cache_hit_texel_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (9) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_cache_miss_texel_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24)))
));

    /* MC */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00468, &profiler->mc_total_read_req_8B_from_pipeline));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00468, &profiler->mc_total_read_req_8B_from_IP));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00468, &profiler->mc_total_write_req_8B_from_pipeline));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0)))
));

    /* HI */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0046C, &profiler->hi_axi_cycles_read_request_stalled));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0046C, &profiler->hi_axi_cycles_write_request_stalled));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0046C, &profiler->hi_axi_cycles_write_data_stalled));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8)))
));

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

    /* Reset counters. */
    gcmkVERIFY_OK(gckOS_ZeroMemory(
        &Context->histroyProfiler, gcmSIZEOF(gcsPROFILER_COUNTERS)
        ));

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

#if USE_SW_RESET
#define gcmkRESET_PROFILE_DATA(counterName, prevCounterName) \
    temp = profiler->counterName; \
    profiler->counterName = CalcDelta(temp, Context->prevCounterName); \
    Context->prevCounterName = temp

#endif

gceSTATUS
gckHARDWARE_UpdateContextProfile(
    IN gckHARDWARE Hardware,
    IN gckCONTEXT Context
    )
{
    gceSTATUS status;
    gcsPROFILER_COUNTERS * profiler = &Context->latestProfiler;
    gcsPROFILER_COUNTERS * profilerHistroy = &Context->histroyProfiler;
    gctUINT i, clock;
    gctUINT32 colorKilled = 0, colorDrawn = 0, depthKilled = 0, depthDrawn = 0;
    gctUINT32 totalRead, totalWrite;
    gceCHIPMODEL chipModel;
    gctUINT32 chipRevision;
    gctUINT32 temp;
    gctUINT32 resetValue = 0xF;

    gcmkHEADER_ARG("Hardware=0x%x Context=0x%x", Hardware, Context);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_OBJECT(Context, gcvOBJ_CONTEXT);

    chipModel = Hardware->identity.chipModel;
    chipRevision = Hardware->identity.chipRevision;
    if ((chipModel == gcv5000 && chipRevision == 0x5434) || (chipModel == gcv3000 && chipRevision == 0x5435))
    {
        resetValue = 0xFF;
    }

    /* Read the counters. */
    gcmkONERROR(
        gckOS_ReadRegisterEx(Hardware->os,
                             Hardware->core,
                             0x00438,
                             &profiler->gpuCyclesCounter));
    gcmkUPDATE_PROFILE_DATA(gpuCyclesCounter);

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
                                           ((((gctUINT32) (clock)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:20) - (0 ? 23:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:20) - (0 ? 23:20) + 1))))))) << (0 ? 23:20))) | (((gctUINT32) ((gctUINT32) (i) & ((gctUINT32) ((((1 ? 23:20) - (0 ? 23:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:20) - (0 ? 23:20) + 1))))))) << (0 ? 23:20)))));

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
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470, ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16)))));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454, &colorKilled));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470, ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16)))));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454, &depthKilled));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470, ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16)))));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454, &colorDrawn));
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470, ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16)))));gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454, &depthDrawn));

        profiler->pe_pixel_count_killed_by_color_pipe += colorKilled;
        profiler->pe_pixel_count_killed_by_depth_pipe += depthKilled;
        profiler->pe_pixel_count_drawn_by_color_pipe += colorDrawn;
        profiler->pe_pixel_count_drawn_by_depth_pipe += depthDrawn;
    }

    gcmkUPDATE_PROFILE_DATA(gpuTotalRead64BytesPerFrame);
    gcmkUPDATE_PROFILE_DATA(gpuTotalWrite64BytesPerFrame);
#if USE_SW_RESET
    gcmkRESET_PROFILE_DATA(pe_pixel_count_killed_by_color_pipe, prevPePixelCountKilledByColorPipe);
    gcmkRESET_PROFILE_DATA(pe_pixel_count_killed_by_depth_pipe, prevPePixelCountKilledByDepthPipe);
    gcmkRESET_PROFILE_DATA(pe_pixel_count_drawn_by_color_pipe, prevPePixelCountDrawnByColorPipe);
    gcmkRESET_PROFILE_DATA(pe_pixel_count_drawn_by_depth_pipe, prevPePixelCountDrawnByDepthPipe);
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
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16)))
));
#endif
    /* SH */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (7) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->ps_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (8) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->rendered_pixel_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (9) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->vs_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (10) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->rendered_vertice_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (11) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->vtx_branch_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (12) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->vtx_texld_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (13) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->pxl_branch_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (14) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->pxl_texld_inst_counter));

#if USE_SW_RESET
    gcmkRESET_PROFILE_DATA(ps_inst_counter, prevPSInstCount);
    gcmkRESET_PROFILE_DATA(rendered_pixel_counter, prevPSPixelCount);
    gcmkRESET_PROFILE_DATA(vs_inst_counter, prevVSInstCount);
    gcmkRESET_PROFILE_DATA(rendered_vertice_counter, prevVSVertexCount);
    gcmkRESET_PROFILE_DATA(vtx_branch_inst_counter, prevVSBranchInstCount);
    gcmkRESET_PROFILE_DATA(vtx_texld_inst_counter, prevVSTexInstCount);
    gcmkRESET_PROFILE_DATA(pxl_branch_inst_counter, prevPSBranchInstCount);
    gcmkRESET_PROFILE_DATA(pxl_texld_inst_counter, prevPSTexInstCount);
#endif

    gcmkUPDATE_PROFILE_DATA(ps_inst_counter);
    gcmkUPDATE_PROFILE_DATA(rendered_pixel_counter);
    gcmkUPDATE_PROFILE_DATA(vs_inst_counter);
    gcmkUPDATE_PROFILE_DATA(rendered_vertice_counter);
    gcmkUPDATE_PROFILE_DATA(vtx_branch_inst_counter);
    gcmkUPDATE_PROFILE_DATA(vtx_texld_inst_counter);
    gcmkUPDATE_PROFILE_DATA(pxl_branch_inst_counter);
    gcmkUPDATE_PROFILE_DATA(pxl_texld_inst_counter);

#if !USE_SW_RESET
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24)))
));
#endif

    /* PA */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_input_vtx_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (4) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_input_prim_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (5) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_output_prim_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (6) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_depth_clipped_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (7) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_trivial_rejected_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (8) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_culled_counter));
#if USE_SW_RESET
    gcmkRESET_PROFILE_DATA(pa_input_vtx_counter, prevPaInputVtxCounter);
    gcmkRESET_PROFILE_DATA(pa_input_prim_counter, prevPaInputPrimCounter);
    gcmkRESET_PROFILE_DATA(pa_output_prim_counter, prevPaOutputPrimCounter);
    gcmkRESET_PROFILE_DATA(pa_depth_clipped_counter, prevPaDepthClippedCounter);
    gcmkRESET_PROFILE_DATA(pa_trivial_rejected_counter, prevPaTrivialRejectedCounter);
    gcmkRESET_PROFILE_DATA(pa_culled_counter, prevPaCulledCounter);
#endif
    gcmkUPDATE_PROFILE_DATA(pa_input_vtx_counter);
    gcmkUPDATE_PROFILE_DATA(pa_input_prim_counter);
    gcmkUPDATE_PROFILE_DATA(pa_output_prim_counter);
    gcmkUPDATE_PROFILE_DATA(pa_depth_clipped_counter);
    gcmkUPDATE_PROFILE_DATA(pa_trivial_rejected_counter);
    gcmkUPDATE_PROFILE_DATA(pa_culled_counter);
#if !USE_SW_RESET
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0)))
));
#endif

    /* SE */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_culled_triangle_count));
    gcmkUPDATE_PROFILE_DATA(se_culled_triangle_count);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_culled_lines_count));
    gcmkUPDATE_PROFILE_DATA(se_culled_lines_count);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8)))
));

    /* RA */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_valid_pixel_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_total_quad_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_valid_quad_count_after_early_z));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_total_primitive_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (9) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_pipe_cache_miss_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (10) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_prefetch_cache_miss_counter));
#if USE_SW_RESET
    gcmkRESET_PROFILE_DATA(ra_valid_pixel_count, prevRaValidPixelCount);
    gcmkRESET_PROFILE_DATA(ra_total_quad_count, prevRaTotalQuadCount);
    gcmkRESET_PROFILE_DATA(ra_valid_quad_count_after_early_z, prevRaValidQuadCountAfterEarlyZ);
    gcmkRESET_PROFILE_DATA(ra_total_primitive_count, prevRaTotalPrimitiveCount);
    gcmkRESET_PROFILE_DATA(ra_pipe_cache_miss_counter, prevRaPipeCacheMissCounter);
    gcmkRESET_PROFILE_DATA(ra_prefetch_cache_miss_counter, prevRaPrefetchCacheMissCounter);
#endif
    gcmkUPDATE_PROFILE_DATA(ra_valid_pixel_count);
    gcmkUPDATE_PROFILE_DATA(ra_total_quad_count);
    gcmkUPDATE_PROFILE_DATA(ra_valid_quad_count_after_early_z);
    gcmkUPDATE_PROFILE_DATA(ra_total_primitive_count);
    gcmkUPDATE_PROFILE_DATA(ra_pipe_cache_miss_counter);
    gcmkUPDATE_PROFILE_DATA(ra_prefetch_cache_miss_counter);
#if !USE_SW_RESET
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 23:16) - (0 ? 23:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:16) - (0 ? 23:16) + 1))))))) << (0 ? 23:16)))
));
#endif

    /* TX */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_total_bilinear_requests));
    gcmkUPDATE_PROFILE_DATA(tx_total_bilinear_requests);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_total_trilinear_requests));
    gcmkUPDATE_PROFILE_DATA(tx_total_trilinear_requests);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_total_discarded_texture_requests));
    gcmkUPDATE_PROFILE_DATA(tx_total_discarded_texture_requests);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_total_texture_requests));
    gcmkUPDATE_PROFILE_DATA(tx_total_texture_requests);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (5) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_mem_read_count));
    gcmkUPDATE_PROFILE_DATA(tx_mem_read_count);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (6) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_mem_read_in_8B_count));
    gcmkUPDATE_PROFILE_DATA(tx_mem_read_in_8B_count);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (7) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_cache_miss_count));
    gcmkUPDATE_PROFILE_DATA(tx_cache_miss_count);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (8) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_cache_hit_texel_count));
    gcmkUPDATE_PROFILE_DATA(tx_cache_hit_texel_count);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (9) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_cache_miss_texel_count));
    gcmkUPDATE_PROFILE_DATA(tx_cache_miss_texel_count);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))) << (0 ? 31:24)))
));

    /* MC */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00468, &profiler->mc_total_read_req_8B_from_pipeline));
    gcmkUPDATE_PROFILE_DATA(mc_total_read_req_8B_from_pipeline);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00468, &profiler->mc_total_read_req_8B_from_IP));
    gcmkUPDATE_PROFILE_DATA(mc_total_read_req_8B_from_IP);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00468, &profiler->mc_total_write_req_8B_from_pipeline));
    gcmkUPDATE_PROFILE_DATA(mc_total_write_req_8B_from_pipeline);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 7:0) - (0 ? 7:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:0) - (0 ? 7:0) + 1))))))) << (0 ? 7:0)))
));

    /* HI */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0046C, &profiler->hi_axi_cycles_read_request_stalled));
    gcmkUPDATE_PROFILE_DATA(hi_axi_cycles_read_request_stalled);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0046C, &profiler->hi_axi_cycles_write_request_stalled));
    gcmkUPDATE_PROFILE_DATA(hi_axi_cycles_write_request_stalled);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0046C, &profiler->hi_axi_cycles_write_data_stalled));
    gcmkUPDATE_PROFILE_DATA(hi_axi_cycles_write_data_stalled);
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) | (((gctUINT32) ((gctUINT32) (resetValue) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8)))
));

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
                                      ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 11:11) - (0 ? 11:11) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:11) - (0 ? 11:11) + 1))))))) << (0 ? 11:11))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 11:11) - (0 ? 11:11) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:11) - (0 ? 11:11) + 1))))))) << (0 ? 11:11)))));

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

        control = ((((gctUINT32) (0x01590880)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 17:17) - (0 ? 17:17) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:17) - (0 ? 17:17) + 1))))))) << (0 ? 17:17))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 17:17) - (0 ? 17:17) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:17) - (0 ? 17:17) + 1))))))) << (0 ? 17:17)));

        /* Disable pulse-eater. */
        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                    Core,
                    0x0010C,
                    control));

        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                    Core,
                    0x0010C,
                    ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))));

        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                    Core,
                    0x0010C,
                    control));

        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                    Core,
                    0x00000,
                    ((((gctUINT32) (0x00000900)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9)))));

        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                    Core,
                    0x00000,
                    0x00000900));

        /* Wait for clock being stable. */
        gcmkONERROR(gckOS_Delay(Os, 1));

        /* Isolate the GPU. */
        control = ((((gctUINT32) (0x00000900)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ? 19:19))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ? 19:19)));

        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                                          Core,
                                          0x00000,
                                          control));

        /* Set soft reset. */
        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                                          Core,
                                          0x00000,
                                          ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ? 12:12))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ? 12:12)))));

        /* Wait for reset. */
        gcmkONERROR(gckOS_Delay(Os, 1));

        /* Reset soft reset bit. */
        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                                          Core,
                                          0x00000,
                                          ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ? 12:12))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ? 12:12)))));

        /* Reset GPU isolation. */
        control = ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ? 19:19))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ? 19:19)));

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

#if gcdMULTI_GPU > 1
        if (Core == gcvCORE_MAJOR)
        {
            /* Read idle register. */
            gcmkONERROR(gckOS_ReadRegisterByCoreId(Os,
                                                   Core,
                                                   gcvCORE_3D_1_ID,
                                                   0x00004,
                                                   &idle));

            if ((((((gctUINT32) (idle)) >> (0 ? 0:0)) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1)))))) ) == 0)
            {
                continue;
            }
        }
#endif
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

#if gcdMULTI_GPU > 1
        if (Core == gcvCORE_MAJOR)
        {
            /* Read reset register. */
            gcmkONERROR(gckOS_ReadRegisterByCoreId(Os,
                                                   Core,
                                                   gcvCORE_3D_1_ID,
                                                   0x00000,
                                                   &control));

            if (((((((gctUINT32) (control)) >> (0 ? 16:16)) & ((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1)))))) ) == 0)
                ||  ((((((gctUINT32) (control)) >> (0 ? 17:17)) & ((gctUINT32) ((((1 ? 17:17) - (0 ? 17:17) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:17) - (0 ? 17:17) + 1)))))) ) == 0)
            )
            {
                continue;
            }
        }
#endif
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
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(BaseAddress != gcvNULL);

    /* Test if we have a new Memory Controller. */
    if (((((gctUINT32) (Hardware->identity.chipMinorFeatures)) >> (0 ? 22:22) & ((gctUINT32) ((((1 ? 22:22) - (0 ? 22:22) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 22:22) - (0 ? 22:22) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 22:22) - (0 ? 22:22) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 22:22) - (0 ? 22:22) + 1))))))))
    {
        /* No base address required. */
        *BaseAddress = 0;
    }
    else
    {
        /* Get the base address from the OS. */
        gcmkONERROR(gckOS_GetBaseAddress(Hardware->os, BaseAddress));
    }

    /* Success. */
    gcmkFOOTER_ARG("*BaseAddress=0x%08x", *BaseAddress);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
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
    if (((((gctUINT32) (State)) >> (0 ? 31:27) & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1)))))) == (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))))
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
        StopIsr == gcvNULL ||
        Context == gcvNULL)
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
        = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ? 1:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ? 1:0)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 5:4) - (0 ? 5:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:4) - (0 ? 5:4) + 1))))))) << (0 ? 5:4))) | (((gctUINT32) (0x3 & ((gctUINT32) ((((1 ? 5:4) - (0 ? 5:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:4) - (0 ? 5:4) + 1))))))) << (0 ? 5:4)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 8:8) - (0 ? 8:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:8) - (0 ? 8:8) + 1))))))) << (0 ? 8:8))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 8:8) - (0 ? 8:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:8) - (0 ? 8:8) + 1))))))) << (0 ? 8:8)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 24:24) - (0 ? 24:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 24:24) - (0 ? 24:24) + 1))))))) << (0 ? 24:24))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 24:24) - (0 ? 24:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 24:24) - (0 ? 24:24) + 1))))))) << (0 ? 24:24)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ? 12:12))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ? 12:12)))
        | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 20:16) - (0 ? 20:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:16) - (0 ? 20:16) + 1))))))) << (0 ? 20:16))) | (((gctUINT32) ((gctUINT32) (EventID) & ((gctUINT32) ((((1 ? 20:16) - (0 ? 20:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:16) - (0 ? 20:16) + 1))))))) << (0 ? 20:16)))
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
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ? 1:0))) | (((gctUINT32) (0x3 & ((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ? 1:0)))
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

    /* Only features needed by common kernel logic added here. */
    switch (Feature)
    {
    case gcvFEATURE_END_EVENT:
        /*available = gcmVERIFYFIELDVALUE(Hardware->identity.chipMinorFeatures2,
            GC_MINOR_FEATURES2, END_EVENT, AVAILABLE
            );*/
        available = gcvFALSE;
        break;

    case gcvFEATURE_MC20:
        available = ((((gctUINT32) (Hardware->identity.chipMinorFeatures)) >> (0 ? 22:22) & ((gctUINT32) ((((1 ? 22:22) - (0 ? 22:22) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 22:22) - (0 ? 22:22) + 1)))))) == (0x1  & ((gctUINT32) ((((1 ? 22:22) - (0 ? 22:22) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 22:22) - (0 ? 22:22) + 1)))))));
        break;

    case gcvFEATURE_EARLY_Z:
        available = ((((gctUINT32) (Hardware->identity.chipFeatures)) >> (0 ? 16:16) & ((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1)))))) == (0x0 & ((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1)))))));
        break;

    case gcvFEATURE_HZ:
        available = ((((gctUINT32) (Hardware->identity.chipMinorFeatures)) >> (0 ? 27:27) & ((gctUINT32) ((((1 ? 27:27) - (0 ? 27:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:27) - (0 ? 27:27) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 27:27) - (0 ? 27:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:27) - (0 ? 27:27) + 1)))))));
        break;

    case gcvFEATURE_NEW_HZ:
        available = ((((gctUINT32) (Hardware->identity.chipMinorFeatures3)) >> (0 ? 26:26) & ((gctUINT32) ((((1 ? 26:26) - (0 ? 26:26) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 26:26) - (0 ? 26:26) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 26:26) - (0 ? 26:26) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 26:26) - (0 ? 26:26) + 1)))))));
        break;

    case gcvFEATURE_FAST_MSAA:
        available = ((((gctUINT32) (Hardware->identity.chipMinorFeatures3)) >> (0 ? 8:8) & ((gctUINT32) ((((1 ? 8:8) - (0 ? 8:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:8) - (0 ? 8:8) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 8:8) - (0 ? 8:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:8) - (0 ? 8:8) + 1)))))));
        break;

    case gcvFEATURE_SMALL_MSAA:
        available = ((((gctUINT32) (Hardware->identity.chipMinorFeatures4)) >> (0 ? 18:18) & ((gctUINT32) ((((1 ? 18:18) - (0 ? 18:18) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 18:18) - (0 ? 18:18) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 18:18) - (0 ? 18:18) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 18:18) - (0 ? 18:18) + 1)))))));
        break;

    case gcvFEATURE_DYNAMIC_FREQUENCY_SCALING:
        /* This feature doesn't apply for 2D cores. */
        available = ((((gctUINT32) (Hardware->identity.chipMinorFeatures2)) >> (0 ? 14:14) & ((gctUINT32) ((((1 ? 14:14) - (0 ? 14:14) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 14:14) - (0 ? 14:14) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 14:14) - (0 ? 14:14) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 14:14) - (0 ? 14:14) + 1)))))))
            &&      ((((gctUINT32) (Hardware->identity.chipFeatures)) >> (0 ? 2:2) & ((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1)))))));

        if (Hardware->identity.chipModel == gcv1000 &&
            (Hardware->identity.chipRevision == 0x5039 ||
            Hardware->identity.chipRevision == 0x5040))
        {
            available = gcvFALSE;
        }
        break;

    case gcvFEATURE_ACE:
        available = ((((gctUINT32) (Hardware->identity.chipMinorFeatures3)) >> (0 ? 18:18) & ((gctUINT32) ((((1 ? 18:18) - (0 ? 18:18) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 18:18) - (0 ? 18:18) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 18:18) - (0 ? 18:18) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 18:18) - (0 ? 18:18) + 1)))))));
        break;

    case gcvFEATURE_HALTI2:
        available = ((((gctUINT32) (Hardware->identity.chipMinorFeatures4)) >> (0 ? 16:16) & ((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1)))))));
        break;

    case gcvFEATURE_PIPE_2D:
        available = ((((gctUINT32) (Hardware->identity.chipFeatures)) >> (0 ? 9:9) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1)))))) == (0x1  & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1)))))));
        break;

    case gcvFEATURE_PIPE_3D:
#if gcdENABLE_3D
        available = ((((gctUINT32) (Hardware->identity.chipFeatures)) >> (0 ? 2:2) & ((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1)))))) == (0x1  & ((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1)))))));
#else
        available = gcvFALSE;
#endif
        break;

    case gcvFEATURE_FC_FLUSH_STALL:
        available = ((((gctUINT32) (Hardware->identity.chipMinorFeatures1)) >> (0 ? 31:31) & ((gctUINT32) ((((1 ? 31:31) - (0 ? 31:31) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 31:31) - (0 ? 31:31) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1)))))));
        break;

    case gcvFEATURE_TEX_CACHE_FLUSH_FIX:
        available = ((((gctUINT32) (Hardware->identity.chipMinorFeatures5)) >> (0 ? 14:14) & ((gctUINT32) ((((1 ? 14:14) - (0 ? 14:14) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 14:14) - (0 ? 14:14) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 14:14) - (0 ? 14:14) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 14:14) - (0 ? 14:14) + 1)))))));
        break;

    case gcvFEATURE_MMU:
        available = ((((gctUINT32) (Hardware->identity.chipMinorFeatures1)) >> (0 ? 28:28) & ((gctUINT32) ((((1 ? 28:28) - (0 ? 28:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 28:28) - (0 ? 28:28) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 28:28) - (0 ? 28:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 28:28) - (0 ? 28:28) + 1)))))));
        break;

    default:
        gcmkFATAL("Invalid feature has been requested.");
        available = gcvFALSE;
    }

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

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

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
                             0x00188,
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

        default:
              gcmkPRINT("  MMU%d: unknown state\n", i);
        }

        gcmkVERIFY_OK(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 0x00190 + i * 4,
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
        { "RA", 0x474, 16, 0x448, 32, 0x12344321 },
        { "TX", 0x474, 24, 0x44C, 32, 0x12211221 },
        { "FE", 0x470, 0, 0x450, 32, 0xBABEF00D },
        { "PE", 0x470, 16, 0x454, 48, 0xBABEF00D },
        { "DE", 0x470, 8, 0x458, 32, 0xBABEF00D },
        { "SH", 0x470, 24, 0x45C, 32, 0xDEADBEEF },
        { "PA", 0x474, 0, 0x460, 32, 0x0000AAAA },
        { "SE", 0x474, 8, 0x464, 32, 0x5E5E5E5E },
        { "MC", 0x478, 0, 0x468, 32, 0x12345678 },
        { "HI", 0x478, 8, 0x46C, 32, 0xAAAAAAAA }
    };

    static gctUINT32 _otherRegs[] =
    {
        0x040, 0x044, 0x04C, 0x050, 0x054, 0x058, 0x05C, 0x060,
        0x43c, 0x440, 0x444, 0x414,
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

    gcmkONERROR(gckOS_ReadRegisterEx(os, core, 0x004, &idle));
    gcmkONERROR(gckOS_ReadRegisterEx(os, core, 0x00C, &axi));
    gcmkONERROR(gckOS_ReadRegisterEx(os, core, 0x668, &dmaLow));
    gcmkONERROR(gckOS_ReadRegisterEx(os, core, 0x66C, &dmaHigh));

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

    /* Record control. */
    gckOS_ReadRegisterEx(os, core, 0x0, &oldControl);

    for (pipe = 0; pipe < pixelPipes; pipe++)
    {
        gcmkPRINT_N(4, "  Debug registers of pipe[%d]:\n", pipe);

        /* Switch pipe. */
        gcmkONERROR(gckOS_ReadRegisterEx(os, core, 0x0, &control));
        control &= ~(0xF << 20);
        control |= (pipe << 20);
        gcmkONERROR(gckOS_WriteRegisterEx(os, core, 0x0, control));

        for (i = 0; i < gcmCOUNTOF(_dbgRegs); i += 1)
        {
            gcmkONERROR(_DumpDebugRegisters(os, core, &_dbgRegs[i]));
        }

        gcmkPRINT_N(0, "    Other Registers:\n");
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

    if (kernel->hardware->identity.chipFeatures & (1 << 4))
    {
        gctUINT32 read0, read1, write;

        read0 = read1 = write = 0;

        gcmkONERROR(gckOS_ReadRegisterEx(os, core, 0x43C, &read0));
        gcmkONERROR(gckOS_ReadRegisterEx(os, core, 0x440, &read1));
        gcmkONERROR(gckOS_ReadRegisterEx(os, core, 0x444, &write));

        gcmkPRINT_N(4, "  read0    = 0x%08X\n", read0);
        gcmkPRINT_N(4, "  read1    = 0x%08X\n", read1);
        gcmkPRINT_N(4, "  write    = 0x%08X\n", write);
    }

    /* Restore control. */
    gcmkONERROR(gckOS_WriteRegisterEx(os, core, 0x0, oldControl));

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
                                          ((((gctUINT32) (clock)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:20) - (0 ? 23:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:20) - (0 ? 23:20) + 1))))))) << (0 ? 23:20))) | (((gctUINT32) ((gctUINT32) (i) & ((gctUINT32) ((((1 ? 23:20) - (0 ? 23:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:20) - (0 ? 23:20) + 1))))))) << (0 ? 23:20)))));

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
                                          ((((gctUINT32) (eater)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8))) | (((gctUINT32) ((gctUINT32) (period) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1))))))) << (0 ? 15:8)))));

#if READ_FROM_EATER1
        /* Config eater1. */
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x00130,
                                         &eater1));

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                          Hardware->core,
                                          0x00130,
                                          ((((gctUINT32) (eater1)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:16) - (0 ? 31:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:16) - (0 ? 31:16) + 1))))))) << (0 ? 31:16))) | (((gctUINT32) ((gctUINT32) (period1) & ((gctUINT32) ((((1 ? 31:16) - (0 ? 31:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:16) - (0 ? 31:16) + 1))))))) << (0 ? 31:16)))));
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

    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ? 16:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ? 16:16)));
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 18:18) - (0 ? 18:18) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 18:18) - (0 ? 18:18) + 1))))))) << (0 ? 18:18))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 18:18) - (0 ? 18:18) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 18:18) - (0 ? 18:18) + 1))))))) << (0 ? 18:18)));
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ? 19:19))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ? 19:19)));
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 20:20) - (0 ? 20:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:20) - (0 ? 20:20) + 1))))))) << (0 ? 20:20))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 20:20) - (0 ? 20:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:20) - (0 ? 20:20) + 1))))))) << (0 ? 20:20)));
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 23:23) - (0 ? 23:23) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:23) - (0 ? 23:23) + 1))))))) << (0 ? 23:23))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 23:23) - (0 ? 23:23) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:23) - (0 ? 23:23) + 1))))))) << (0 ? 23:23)));
    data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 22:22) - (0 ? 22:22) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 22:22) - (0 ? 22:22) + 1))))))) << (0 ? 22:22))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 22:22) - (0 ? 22:22) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 22:22) - (0 ? 22:22) + 1))))))) << (0 ? 22:22)));

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
    gctUINT8_PTR logical;
    gctPHYS_ADDR_T physical;

    gcmkHEADER_ARG("%x", Hardware);

    os = Hardware->os;

    gcmkVERIFY_OK(gckOS_GetPageSize(os, &Hardware->functionBytes));

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

    if (Hardware->mmuVersion > 0)
    {
        /* MMU configure command sequence. */
        logical = (gctUINT8_PTR)Hardware->functionLogical + offset;

        Hardware->functions[gcvHARDWARE_FUNCTION_MMU].address
            = Hardware->functionAddress + offset;

        gcmkONERROR(gckHARDWARE_SetMMUStates(
            Hardware,
            Hardware->kernel->mmu->mtlbLogical,
            gcvMMU_MODE_4K,
            (gctUINT8_PTR)Hardware->kernel->mmu->mtlbLogical + gcdMMU_MTLB_SIZE,
            logical,
            &mmuBytes
            ));

        offset += mmuBytes;

        logical = (gctUINT8_PTR)Hardware->functionLogical + offset;

        gcmkONERROR(gckHARDWARE_End(
            Hardware,
            gcvNULL,
            &endBytes
            ));

        gcmkONERROR(gckHARDWARE_End(
            Hardware,
            logical,
            &endBytes
            ));

        offset += endBytes;

        Hardware->functions[gcvHARDWARE_FUNCTION_MMU].bytes = mmuBytes + endBytes;

        Hardware->functions[gcvHARDWARE_FUNCTION_MMU].endAddress =
            Hardware->functions[gcvHARDWARE_FUNCTION_MMU].address + mmuBytes;

        Hardware->functions[gcvHARDWARE_FUNCTION_MMU].endLogical =
            logical + mmuBytes;
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

