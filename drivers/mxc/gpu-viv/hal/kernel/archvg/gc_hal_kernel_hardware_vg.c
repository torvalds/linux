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


#include "gc_hal.h"
#include "gc_hal_kernel.h"
#include "gc_hal_kernel_hardware_command_vg.h"

#if gcdENABLE_VG

#define _GC_OBJ_ZONE    gcvZONE_HARDWARE

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
    gcvPOWER_FLAG_NOP           = 1 << 11,
}
gcePOWER_FLAGS;

/******************************************************************************\
********************************* Support Code *********************************
\******************************************************************************/
static gceSTATUS
_ResetGPU(
    IN gckOS Os
    )
{
    gctUINT32 control, idle;
    gceSTATUS status;

    /* Read register. */
    gcmkONERROR(gckOS_ReadRegisterEx(Os,
                                     gcvCORE_VG,
                                     0x00000,
                                     &control));

    for (;;)
    {
        /* Disable clock gating. */
        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                    gcvCORE_VG,
                    0x00104,
                    0x00000000));

        /* Wait for clock being stable. */
        gcmkONERROR(gckOS_Delay(Os, 1));

        /* Isolate the GPU. */
        control = ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ? 19:19))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ? 19:19)));

        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                                          gcvCORE_VG,
                                          0x00000,
                                          control));

        /* Set soft reset. */
        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                                          gcvCORE_VG,
                                          0x00000,
                                          ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ? 12:12))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ? 12:12)))));

        /* Wait for reset. */
        gcmkONERROR(gckOS_Delay(Os, 1));

        /* Reset soft reset bit. */
        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                                          gcvCORE_VG,
                                          0x00000,
                                          ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ? 12:12))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ? 12:12)))));

        /* Reset GPU isolation. */
        control = ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ? 19:19))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ? 19:19)));

        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                                          gcvCORE_VG,
                                          0x00000,
                                          control));

        /* Read idle register. */
        gcmkONERROR(gckOS_ReadRegisterEx(Os,
                                         gcvCORE_VG,
                                         0x00004,
                                         &idle));

        if ((((((gctUINT32) (idle)) >> (0 ? 0:0)) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1)))))) ) == 0)
        {
            continue;
        }

        /* Read reset register. */
        gcmkONERROR(gckOS_ReadRegisterEx(Os,
                                         gcvCORE_VG,
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


static gceSTATUS
_IdentifyHardware(
    IN gckOS Os,
    OUT gceCHIPMODEL * ChipModel,
    OUT gctUINT32 * ChipRevision,
    OUT gctUINT32 * ChipFeatures,
    OUT gctUINT32 * ChipMinorFeatures,
    OUT gctUINT32 * ChipMinorFeatures2
    )
{
    gceSTATUS status;
    gctUINT32 chipIdentity;

    do
    {
        /* Read chip identity register. */
        gcmkERR_BREAK(gckOS_ReadRegisterEx(Os, gcvCORE_VG, 0x00018, &chipIdentity));

        /* Special case for older graphic cores. */
        if (((((gctUINT32) (chipIdentity)) >> (0 ? 31:24) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1)))))) == (0x01 & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))))
        {
            *ChipModel    = gcv500;
            *ChipRevision = (((((gctUINT32) (chipIdentity)) >> (0 ? 15:12)) & ((gctUINT32) ((((1 ? 15:12) - (0 ? 15:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:12) - (0 ? 15:12) + 1)))))) );
        }

        else
        {
            /* Read chip identity register. */
            gcmkERR_BREAK(gckOS_ReadRegisterEx(Os, gcvCORE_VG,
                                            0x00020,
                                            (gctUINT32 *) ChipModel));

            /* Read CHIP_REV register. */
            gcmkERR_BREAK(gckOS_ReadRegisterEx(Os, gcvCORE_VG,
                                            0x00024,
                                            ChipRevision));
        }

        /* Read chip feature register. */
        gcmkERR_BREAK(gckOS_ReadRegisterEx(
            Os, gcvCORE_VG, 0x0001C, ChipFeatures
            ));

        /* Read chip minor feature register. */
        gcmkERR_BREAK(gckOS_ReadRegisterEx(
            Os, gcvCORE_VG, 0x00034, ChipMinorFeatures
            ));

        /* Read chip minor feature register #2. */
        gcmkERR_BREAK(gckOS_ReadRegisterEx(
            Os, gcvCORE_VG, 0x00074, ChipMinorFeatures2
            ));

        gcmkTRACE(
            gcvLEVEL_VERBOSE,
            "ChipModel=0x%08X\n"
            "ChipRevision=0x%08X\n"
            "ChipFeatures=0x%08X\n"
            "ChipMinorFeatures=0x%08X\n"
            "ChipMinorFeatures2=0x%08X\n",
            *ChipModel,
            *ChipRevision,
            *ChipFeatures,
            *ChipMinorFeatures,
            *ChipMinorFeatures2
            );

        /* Success. */
        return gcvSTATUS_OK;
    }
    while (gcvFALSE);

    /* Return the status. */
    return status;
}

#if gcdPOWEROFF_TIMEOUT
void
_VGPowerTimerFunction(
    gctPOINTER Data
    )
{
    gckVGHARDWARE hardware = (gckVGHARDWARE)Data;
    gcmkVERIFY_OK(
        gckVGHARDWARE_SetPowerManagementState(hardware, gcvPOWER_OFF_TIMEOUT));
}
#endif

/******************************************************************************\
****************************** gckVGHARDWARE API code *****************************
\******************************************************************************/

/*******************************************************************************
**
**  gckVGHARDWARE_Construct
**
**  Construct a new gckVGHARDWARE object.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an initialized gckOS object.
**
**  OUTPUT:
**
**      gckVGHARDWARE * Hardware
**          Pointer to a variable that will hold the pointer to the gckVGHARDWARE
**          object.
*/
gceSTATUS
gckVGHARDWARE_Construct(
    IN gckOS Os,
    OUT gckVGHARDWARE * Hardware
    )
{
    gckVGHARDWARE hardware = gcvNULL;
    gceSTATUS status;
    gceCHIPMODEL chipModel;
    gctUINT32 chipRevision;
    gctUINT32 chipFeatures;
    gctUINT32 chipMinorFeatures;
    gctUINT32 chipMinorFeatures2;

    gcmkHEADER_ARG("Os=0x%x Hardware=0x%x ", Os, Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Hardware != gcvNULL);

    do
    {
        gcmkERR_BREAK(gckOS_SetGPUPower(Os, gcvCORE_VG, gcvTRUE, gcvTRUE));

        status = _ResetGPU(Os);

        if (status != gcvSTATUS_OK)
        {
            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                "_ResetGPU failed: status=%d\n", status);
        }

        /* Identify the hardware. */
        gcmkERR_BREAK(_IdentifyHardware(Os,
            &chipModel, &chipRevision,
            &chipFeatures, &chipMinorFeatures, &chipMinorFeatures2
            ));

        /* Allocate the gckVGHARDWARE object. */
        gcmkERR_BREAK(gckOS_Allocate(Os,
            gcmSIZEOF(struct _gckVGHARDWARE), (gctPOINTER *) &hardware
            ));

        /* Initialize the gckVGHARDWARE object. */
        hardware->object.type = gcvOBJ_HARDWARE;
        hardware->os = Os;

        /* Set chip identity. */
        hardware->chipModel          = chipModel;
        hardware->chipRevision       = chipRevision;
        hardware->chipFeatures       = chipFeatures;
        hardware->chipMinorFeatures  = chipMinorFeatures;
        hardware->chipMinorFeatures2 = chipMinorFeatures2;

        hardware->powerMutex            = gcvNULL;
        hardware->chipPowerState        = gcvPOWER_ON;
        hardware->chipPowerStateGlobal  = gcvPOWER_ON;
        hardware->clockState            = gcvTRUE;
        hardware->powerState            = gcvTRUE;

#if gcdPOWEROFF_TIMEOUT
        hardware->powerOffTime          = 0;
        hardware->powerOffTimeout       = gcdPOWEROFF_TIMEOUT;

        gcmkVERIFY_OK(gckOS_CreateTimer(Os,
                                        _VGPowerTimerFunction,
                                        (gctPOINTER)hardware,
                                        &hardware->powerOffTimer));
#endif

        /* Determine whether FE 2.0 is present. */
        hardware->fe20 = ((((gctUINT32) (hardware->chipFeatures)) >> (0 ? 28:28) & ((gctUINT32) ((((1 ? 28:28) - (0 ? 28:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 28:28) - (0 ? 28:28) + 1)))))) == (0x1  & ((gctUINT32) ((((1 ? 28:28) - (0 ? 28:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 28:28) - (0 ? 28:28) + 1)))))));

        /* Determine whether VG 2.0 is present. */
        hardware->vg20 = ((((gctUINT32) (hardware->chipMinorFeatures)) >> (0 ? 13:13) & ((gctUINT32) ((((1 ? 13:13) - (0 ? 13:13) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 13:13) - (0 ? 13:13) + 1)))))) == (0x1  & ((gctUINT32) ((((1 ? 13:13) - (0 ? 13:13) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 13:13) - (0 ? 13:13) + 1)))))));

        /* Determine whether VG 2.1 is present. */
        hardware->vg21 = ((((gctUINT32) (hardware->chipMinorFeatures)) >> (0 ? 18:18) & ((gctUINT32) ((((1 ? 18:18) - (0 ? 18:18) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 18:18) - (0 ? 18:18) + 1)))))) == (0x1  & ((gctUINT32) ((((1 ? 18:18) - (0 ? 18:18) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 18:18) - (0 ? 18:18) + 1)))))));

        /* Set default event mask. */
        hardware->eventMask = 0xFFFFFFFF;

        gcmkERR_BREAK(gckOS_AtomConstruct(Os, &hardware->pageTableDirty));

        /* Set fast clear to auto. */
        gcmkVERIFY_OK(gckVGHARDWARE_SetFastClear(hardware, -1));

        gcmkERR_BREAK(gckOS_CreateMutex(Os, &hardware->powerMutex));

        /* Enable power management by default. */
        hardware->powerManagement = gcvTRUE;

        /* Return pointer to the gckVGHARDWARE object. */
        *Hardware = hardware;

        gcmkFOOTER_NO();
        /* Success. */
        return gcvSTATUS_OK;
    }
    while (gcvFALSE);

#if gcdPOWEROFF_TIMEOUT
    if (hardware->powerOffTimer != gcvNULL)
    {
        gcmkVERIFY_OK(gckOS_StopTimer(Os, hardware->powerOffTimer));
        gcmkVERIFY_OK(gckOS_DestroyTimer(Os, hardware->powerOffTimer));
    }
#endif

    gcmkVERIFY_OK(gckOS_SetGPUPower(Os, gcvCORE_VG, gcvFALSE, gcvFALSE));

    if (hardware != gcvNULL && hardware->pageTableDirty != gcvNULL)
    {
        gcmkVERIFY_OK(gckOS_AtomDestroy(Os, hardware->pageTableDirty));
    }

    if (hardware != gcvNULL)
    {
        gcmkVERIFY_OK(gckOS_Free(Os, hardware));
    }

    gcmkFOOTER();
    /* Return the status. */
    return status;
}

/*******************************************************************************
**
**  gckVGHARDWARE_Destroy
**
**  Destroy an gckVGHARDWARE object.
**
**  INPUT:
**
**      gckVGHARDWARE Hardware
**          Pointer to the gckVGHARDWARE object that needs to be destroyed.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckVGHARDWARE_Destroy(
    IN gckVGHARDWARE Hardware
    )
{
    gceSTATUS status;
    gcmkHEADER_ARG("Hardware=0x%x ", Hardware);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Mark the object as unknown. */
    Hardware->object.type = gcvOBJ_UNKNOWN;

    if (Hardware->powerMutex != gcvNULL)
    {
        gcmkVERIFY_OK(gckOS_DeleteMutex(
            Hardware->os, Hardware->powerMutex));
    }

#if gcdPOWEROFF_TIMEOUT
    gcmkVERIFY_OK(gckOS_StopTimer(Hardware->os, Hardware->powerOffTimer));
    gcmkVERIFY_OK(gckOS_DestroyTimer(Hardware->os, Hardware->powerOffTimer));
#endif

    if (Hardware->pageTableDirty != gcvNULL)
    {
        gcmkVERIFY_OK(gckOS_AtomDestroy(Hardware->os, Hardware->pageTableDirty));
    }

    /* Free the object. */
    status = gckOS_Free(Hardware->os, Hardware);
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckVGHARDWARE_QueryMemory
**
**  Query the amount of memory available on the hardware.
**
**  INPUT:
**
**      gckVGHARDWARE Hardware
**          Pointer to the gckVGHARDWARE object.
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
gckVGHARDWARE_QueryMemory(
    IN gckVGHARDWARE Hardware,
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
    gcmkHEADER_ARG("Hardware=0x%x InternalSize=0x%x InternalBaseAddress=0x%x InternalAlignment=0x%x"
        "ExternalSize=0x%x ExternalBaseAddress=0x%x ExternalAlignment=0x%x HorizontalTileSize=0x%x VerticalTileSize=0x%x",
        Hardware, InternalSize, InternalBaseAddress, InternalAlignment,
        ExternalSize, ExternalBaseAddress, ExternalAlignment, HorizontalTileSize, VerticalTileSize);

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

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckVGHARDWARE_QueryChipIdentity
**
**  Query the identity of the hardware.
**
**  INPUT:
**
**      gckVGHARDWARE Hardware
**          Pointer to the gckVGHARDWARE object.
**
**  OUTPUT:
**
**      gceCHIPMODEL * ChipModel
**          If 'ChipModel' is not gcvNULL, the variable it points to will
**          receive the model of the chip.
**
**      gctUINT32 * ChipRevision
**          If 'ChipRevision' is not gcvNULL, the variable it points to will
**          receive the revision of the chip.
**
**      gctUINT32 * ChipFeatures
**          If 'ChipFeatures' is not gcvNULL, the variable it points to will
**          receive the feature set of the chip.
**
**      gctUINT32 * ChipMinorFeatures
**          If 'ChipMinorFeatures' is not gcvNULL, the variable it points to
**          will receive the minor feature set of the chip.
**
**      gctUINT32 * ChipMinorFeatures2
**          If 'ChipMinorFeatures2' is not gcvNULL, the variable it points to
**          will receive the minor feature set of the chip.
**
*/
gceSTATUS
gckVGHARDWARE_QueryChipIdentity(
    IN gckVGHARDWARE Hardware,
    OUT gceCHIPMODEL * ChipModel,
    OUT gctUINT32 * ChipRevision,
    OUT gctUINT32* ChipFeatures,
    OUT gctUINT32* ChipMinorFeatures,
    OUT gctUINT32* ChipMinorFeatures2
    )
{
    gcmkHEADER_ARG("Hardware=0x%x ChipModel=0x%x ChipRevision=0x%x ChipFeatures = 0x%x ChipMinorFeatures = 0x%x ChipMinorFeatures2 = 0x%x",
                   Hardware, ChipModel, ChipRevision, ChipFeatures, ChipMinorFeatures, ChipMinorFeatures2);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Return chip model. */
    if (ChipModel != gcvNULL)
    {
        *ChipModel = Hardware->chipModel;
    }

    /* Return revision number. */
    if (ChipRevision != gcvNULL)
    {
        *ChipRevision = Hardware->chipRevision;
    }

    /* Return feature set. */
    if (ChipFeatures != gcvNULL)
    {
        gctUINT32 features = Hardware->chipFeatures;

        if ((((((gctUINT32) (features)) >> (0 ? 0:0)) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1)))))) ))
        {
            features = ((((gctUINT32) (features)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (Hardware->allowFastClear) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));
        }

        /* Mark 2D pipe as available for GC500.0 since it did not have this *\
        \* bit.                                                             */
        if ((Hardware->chipModel == gcv500)
        &&  (Hardware->chipRevision == 0)
        )
        {
            features = ((((gctUINT32) (features)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9)));
        }

        /* Mark 2D pipe as available for GC300 since it did not have this   *\
        \* bit.                                                             */
        if (Hardware->chipModel == gcv300)
        {
            features = ((((gctUINT32) (features)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9)));
        }

        *ChipFeatures = features;
    }

    /* Return minor feature set. */
    if (ChipMinorFeatures != gcvNULL)
    {
        *ChipMinorFeatures = Hardware->chipMinorFeatures;
    }

    /* Return minor feature set #2. */
    if (ChipMinorFeatures2 != gcvNULL)
    {
        *ChipMinorFeatures2 = Hardware->chipMinorFeatures2;
    }

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckVGHARDWARE_ConvertFormat
**
**  Convert an API format to hardware parameters.
**
**  INPUT:
**
**      gckVGHARDWARE Hardware
**          Pointer to the gckVGHARDWARE object.
**
**      gceSURF_FORMAT Format
**          API format to convert.
**
**  OUTPUT:
**
**      gctUINT32 * BitsPerPixel
**          Pointer to a variable that will hold the number of bits per pixel.
**
**      gctUINT32 * BytesPerTile
**          Pointer to a variable that will hold the number of bytes per tile.
*/
gceSTATUS
gckVGHARDWARE_ConvertFormat(
    IN gckVGHARDWARE Hardware,
    IN gceSURF_FORMAT Format,
    OUT gctUINT32 * BitsPerPixel,
    OUT gctUINT32 * BytesPerTile
    )
{
    gctUINT32 bitsPerPixel;
    gctUINT32 bytesPerTile;

    gcmkHEADER_ARG("Hardware=0x%x Format=0x%x BitsPerPixel=0x%x BytesPerTile = 0x%x",
                   Hardware, Format, BitsPerPixel, BytesPerTile);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Dispatch on format. */
    switch (Format)
    {
    case gcvSURF_A1:
    case gcvSURF_L1:
        /* 1-bpp format. */
        bitsPerPixel  = 1;
        bytesPerTile  = (1 * 4 * 4) / 8;
        break;

    case gcvSURF_A4:
        /* 4-bpp format. */
        bitsPerPixel  = 4;
        bytesPerTile  = (4 * 4 * 4) / 8;
        break;

    case gcvSURF_INDEX8:
    case gcvSURF_A8:
    case gcvSURF_L8:
        /* 8-bpp format. */
        bitsPerPixel  = 8;
        bytesPerTile  = (8 * 4 * 4) / 8;
        break;

    case gcvSURF_YV12:
        /* 12-bpp planar YUV formats. */
        bitsPerPixel  = 12;
        bytesPerTile  = (12 * 4 * 4) / 8;
        break;

    case gcvSURF_NV12:
        /* 12-bpp planar YUV formats. */
        bitsPerPixel  = 12;
        bytesPerTile  = (12 * 4 * 4) / 8;
        break;

    /* 4444 variations. */
    case gcvSURF_X4R4G4B4:
    case gcvSURF_A4R4G4B4:
    case gcvSURF_R4G4B4X4:
    case gcvSURF_R4G4B4A4:
    case gcvSURF_B4G4R4X4:
    case gcvSURF_B4G4R4A4:
    case gcvSURF_X4B4G4R4:
    case gcvSURF_A4B4G4R4:

    /* 1555 variations. */
    case gcvSURF_X1R5G5B5:
    case gcvSURF_A1R5G5B5:
    case gcvSURF_R5G5B5X1:
    case gcvSURF_R5G5B5A1:
    case gcvSURF_X1B5G5R5:
    case gcvSURF_A1B5G5R5:
    case gcvSURF_B5G5R5X1:
    case gcvSURF_B5G5R5A1:

    /* 565 variations. */
    case gcvSURF_R5G6B5:
    case gcvSURF_B5G6R5:

    case gcvSURF_A8L8:
    case gcvSURF_YUY2:
    case gcvSURF_UYVY:
    case gcvSURF_D16:
        /* 16-bpp format. */
        bitsPerPixel  = 16;
        bytesPerTile  = (16 * 4 * 4) / 8;
        break;

    case gcvSURF_X8R8G8B8:
    case gcvSURF_A8R8G8B8:
    case gcvSURF_X8B8G8R8:
    case gcvSURF_A8B8G8R8:
    case gcvSURF_R8G8B8X8:
    case gcvSURF_R8G8B8A8:
    case gcvSURF_B8G8R8X8:
    case gcvSURF_B8G8R8A8:
    case gcvSURF_D32:
        /* 32-bpp format. */
        bitsPerPixel  = 32;
        bytesPerTile  = (32 * 4 * 4) / 8;
        break;

    case gcvSURF_D24S8:
        /* 24-bpp format. */
        bitsPerPixel  = 32;
        bytesPerTile  = (32 * 4 * 4) / 8;
        break;

    case gcvSURF_DXT1:
    case gcvSURF_ETC1:
        bitsPerPixel  = 4;
        bytesPerTile  = (4 * 4 * 4) / 8;
        break;

    case gcvSURF_DXT2:
    case gcvSURF_DXT3:
    case gcvSURF_DXT4:
    case gcvSURF_DXT5:
        bitsPerPixel  = 8;
        bytesPerTile  = (8 * 4 * 4) / 8;
        break;

    default:
        /* Invalid format. */
        gcmkFOOTER_NO();
        return gcvSTATUS_INVALID_ARGUMENT;
    }

    /* Set the result. */
    if (BitsPerPixel != gcvNULL)
    {
        * BitsPerPixel = bitsPerPixel;
    }

    if (BytesPerTile != gcvNULL)
    {
        * BytesPerTile = bytesPerTile;
    }

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckVGHARDWARE_SplitMemory
**
**  Split a hardware specific memory address into a pool and offset.
**
**  INPUT:
**
**      gckVGHARDWARE Hardware
**          Pointer to the gckVGHARDWARE object.
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
gckVGHARDWARE_SplitMemory(
    IN gckVGHARDWARE Hardware,
    IN gctUINT32 Address,
    OUT gcePOOL * Pool,
    OUT gctUINT32 * Offset
    )
{
    gcmkHEADER_ARG("Hardware=0x%x Address=0x%x Pool=0x%x Offset = 0x%x",
                   Hardware, Address, Pool, Offset);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Pool != gcvNULL);
    gcmkVERIFY_ARGUMENT(Offset != gcvNULL);

    /* Dispatch on memory type. */
    switch ((((((gctUINT32) (Address)) >> (0 ? 1:0)) & ((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1)))))) ))
    {
    case 0x0:
        /* System memory. */
        *Pool = gcvPOOL_SYSTEM;
        break;

    case 0x2:
        /* Virtual memory. */
        *Pool = gcvPOOL_VIRTUAL;
        break;

    default:
        /* Invalid memory type. */
        gcmkFOOTER_NO();
        return gcvSTATUS_INVALID_ARGUMENT;
    }

    /* Return offset of address. */
    *Offset = ((((gctUINT32) (Address)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ? 1:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ? 1:0)));

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckVGHARDWARE_Execute
**
**  Kickstart the hardware's command processor with an initialized command
**  buffer.
**
**  INPUT:
**
**      gckVGHARDWARE Hardware
**          Pointer to the gckVGHARDWARE object.
**
**      gctUINT32 Address
**          Address of the command buffer.
**
**      gctSIZE_T Count
**          Number of command-sized data units to be executed.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckVGHARDWARE_Execute(
    IN gckVGHARDWARE Hardware,
    IN gctUINT32 Address,
    IN gctUINT32 Count
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Address=0x%x Count=0x%x",
                   Hardware, Address, Count);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    do
    {
        /* Enable all events. */
        gcmkERR_BREAK(gckOS_WriteRegisterEx(
            Hardware->os,
            gcvCORE_VG,
            0x00014,
            Hardware->eventMask
            ));

        if (Hardware->fe20)
        {
            /* Write address register. */
            gcmkERR_BREAK(gckOS_WriteRegisterEx(
                Hardware->os,
                gcvCORE_VG,
                0x00500,
                gcmkFIXADDRESS(Address)
                ));

            /* Write control register. */
            gcmkERR_BREAK(gckOS_WriteRegisterEx(
                Hardware->os,
                gcvCORE_VG,
                0x00504,
                Count
                ));
        }
        else
        {
            /* Write address register. */
            gcmkERR_BREAK(gckOS_WriteRegisterEx(
                Hardware->os,
                gcvCORE_VG,
                0x00654,
                gcmkFIXADDRESS(Address)
                ));

            /* Write control register. */
            gcmkERR_BREAK(gckOS_WriteRegisterEx(
                Hardware->os,
                gcvCORE_VG,
                0x00658,
                ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ? 16:16))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ? 16:16))) |
                ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (Count) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
                ));
        }

        /* Success. */
        gcmkFOOTER();
        return gcvSTATUS_OK;
    }
    while (gcvFALSE);


    gcmkFOOTER();
    /* Return the status. */
    return status;
}

/*******************************************************************************
**
**  gckVGHARDWARE_AlignToTile
**
**  Align the specified width and height to tile boundaries.
**
**  INPUT:
**
**      gckVGHARDWARE Hardware
**          Pointer to an gckVGHARDWARE object.
**
**      gceSURF_TYPE Type
**          Type of alignment.
**
**      gctUINT32 * Width
**          Pointer to the width to be aligned.  If 'Width' is gcvNULL, no width
**          will be aligned.
**
**      gctUINT32 * Height
**          Pointer to the height to be aligned.  If 'Height' is gcvNULL, no height
**          will be aligned.
**
**  OUTPUT:
**
**      gctUINT32 * Width
**          Pointer to a variable that will receive the aligned width.
**
**      gctUINT32 * Height
**          Pointer to a variable that will receive the aligned height.
*/
gceSTATUS
gckVGHARDWARE_AlignToTile(
    IN gckVGHARDWARE Hardware,
    IN gceSURF_TYPE Type,
    IN OUT gctUINT32 * Width,
    IN OUT gctUINT32 * Height
    )
{
    gcmkHEADER_ARG("Hardware=0x%x Type=0x%x Width=0x%x Height=0x%x",
                   Hardware, Type, Width, Height);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    if (Width != gcvNULL)
    {
        /* Align the width. */
        *Width = gcmALIGN(*Width, (Type == gcvSURF_TEXTURE) ? 4 : 16);
    }

    if (Height != gcvNULL)
    {
        /* Special case for VG images. */
        if ((*Height == 0) && (Type == gcvSURF_IMAGE))
        {
            *Height = 4;
        }
        else
        {
            /* Align the height. */
            *Height = gcmALIGN(*Height, 4);
        }
    }

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckVGHARDWARE_ConvertLogical
**
**  Convert a logical system address into a hardware specific address.
**
**  INPUT:
**
**      gckVGHARDWARE Hardware
**          Pointer to an gckVGHARDWARE object.
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
gckVGHARDWARE_ConvertLogical(
    IN gckVGHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gctBOOL InUserSpace,
    OUT gctUINT32 * Address
    )
{
    gctPHYS_ADDR_T physical;
    gctUINT32 address;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x InUserSpace=%d Address=0x%x",
                   Hardware, Logical, InUserSpace, Address);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    do
    {
        /* Convert logical address into a physical address. */
        if (InUserSpace)
        {
            gcmkERR_BREAK(gckOS_UserLogicalToPhysical(
                Hardware->os, Logical, &physical
                ));
        }
        else
        {
            gcmkERR_BREAK(gckOS_GetPhysicalAddress(
                Hardware->os, Logical, &physical
                ));
        }

        gcmkSAFECASTPHYSADDRT(address, physical);

        /* Return hardware specific address. */
        *Address = ((((gctUINT32) (address)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ? 1:0))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ? 1:0)));

        /* Success. */
        gcmkFOOTER();
        return gcvSTATUS_OK;
    }
    while (gcvFALSE);

    gcmkFOOTER();
    /* Return the status. */
    return status;
}

/*******************************************************************************
**
**  gckVGHARDWARE_QuerySystemMemory
**
**  Query the command buffer alignment and number of reserved bytes.
**
**  INPUT:
**
**      gckVGHARDWARE Harwdare
**          Pointer to an gckVGHARDWARE object.
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
gceSTATUS gckVGHARDWARE_QuerySystemMemory(
    IN gckVGHARDWARE Hardware,
    OUT gctSIZE_T * SystemSize,
    OUT gctUINT32 * SystemBaseAddress
    )
{
    gcmkHEADER_ARG("Hardware=0x%x SystemSize=0x%x SystemBaseAddress=0x%x",
                   Hardware, SystemSize, SystemBaseAddress);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    if (SystemSize != gcvNULL)
    {
        /* Maximum system memory can be 2GB. */
        *SystemSize = (gctSIZE_T)(1 << 31);
    }

    if (SystemBaseAddress != gcvNULL)
    {
        /* Set system memory base address. */
        *SystemBaseAddress = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ? 1:0))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ? 1:0)));
    }

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckVGHARDWARE_SetMMU
**
**  Set the page table base address.
**
**  INPUT:
**
**      gckVGHARDWARE Harwdare
**          Pointer to an gckVGHARDWARE object.
**
**      gctPOINTER Logical
**          Logical address of the page table.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS gckVGHARDWARE_SetMMU(
    IN gckVGHARDWARE Hardware,
    IN gctPOINTER Logical
    )
{
    gceSTATUS status;
    gctUINT32 address = 0;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x",
                   Hardware, Logical);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

    do
    {
        /* Convert the logical address into an hardware address. */
        gcmkERR_BREAK(gckVGHARDWARE_ConvertLogical(Hardware, Logical,
                                      gcvFALSE, &address));

        /* Write the AQMemoryFePageTable register. */
        gcmkERR_BREAK(gckOS_WriteRegisterEx(Hardware->os, gcvCORE_VG,
                                      0x00400,
                                      gcmkFIXADDRESS(address)));

        /* Write the AQMemoryTxPageTable register. */
        gcmkERR_BREAK(gckOS_WriteRegisterEx(Hardware->os, gcvCORE_VG,
                                      0x00404,
                                      gcmkFIXADDRESS(address)));

        /* Write the AQMemoryPePageTable register. */
        gcmkERR_BREAK(gckOS_WriteRegisterEx(Hardware->os, gcvCORE_VG,
                                      0x00408,
                                      gcmkFIXADDRESS(address)));

        /* Write the AQMemoryPezPageTable register. */
        gcmkERR_BREAK(gckOS_WriteRegisterEx(Hardware->os, gcvCORE_VG,
                                      0x0040C,
                                      gcmkFIXADDRESS(address)));

        /* Write the AQMemoryRaPageTable register. */
        gcmkERR_BREAK(gckOS_WriteRegisterEx(Hardware->os, gcvCORE_VG,
                                      0x00410,
                                      gcmkFIXADDRESS(address)));
    }
    while (gcvFALSE);

    gcmkFOOTER();
    /* Return the status. */
    return status;
}

/*******************************************************************************
**
**  gckVGHARDWARE_FlushMMU
**
**  Flush the page table.
**
**  INPUT:
**
**      gckVGHARDWARE Harwdare
**          Pointer to an gckVGHARDWARE object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS gckVGHARDWARE_FlushMMU(
    IN gckVGHARDWARE Hardware
    )
{
    gceSTATUS status;
    gckVGCOMMAND command;

    gcmkHEADER_ARG("Hardware=0x%x ", Hardware);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    do
    {
        gcsCMDBUFFER_PTR commandBuffer;
        gctUINT32_PTR buffer;

        /* Create a shortcut to the command buffer object. */
        command = Hardware->kernel->command;

        /* Allocate command buffer space. */
        gcmkERR_BREAK(gckVGCOMMAND_Allocate(
            command, 8, &commandBuffer, (gctPOINTER *) &buffer
            ));

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

        gcmkERR_BREAK(gckVGCOMMAND_Execute(
            command,
            commandBuffer
            ));
    }
    while(gcvFALSE);

    gcmkFOOTER();
    /* Return the status. */
    return status;
}

/*******************************************************************************
**
**  gckVGHARDWARE_BuildVirtualAddress
**
**  Build a virtual address.
**
**  INPUT:
**
**      gckVGHARDWARE Harwdare
**          Pointer to an gckVGHARDWARE object.
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
gceSTATUS gckVGHARDWARE_BuildVirtualAddress(
    IN gckVGHARDWARE Hardware,
    IN gctUINT32 Index,
    IN gctUINT32 Offset,
    OUT gctUINT32 * Address
    )
{
    gctUINT32 address;

    gcmkHEADER_ARG("Hardware=0x%x Index=0x%x Offset=0x%x Address=0x%x",
                   Hardware, Index, Offset, Address);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    /* Build virtual address. */
    address = (Index << 12) | Offset;

    /* Set virtual type. */
    address = ((((gctUINT32) (address)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ? 1:0))) | (((gctUINT32) (0x2 & ((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ? 1:0)));

    /* Set the result. */
    *Address = address;

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

gceSTATUS
gckVGHARDWARE_GetIdle(
    IN gckVGHARDWARE Hardware,
    OUT gctUINT32 * Data
    )
{
    gceSTATUS status;
    gcmkHEADER_ARG("Hardware=0x%x Data=0x%x", Hardware, Data);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Data != gcvNULL);

    /* Read register and return. */
    status = gckOS_ReadRegisterEx(Hardware->os, gcvCORE_VG, 0x00004, Data);
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckVGHARDWARE_SetFastClear(
    IN gckVGHARDWARE Hardware,
    IN gctINT Enable
    )
{
    gctUINT32 debug;
    gceSTATUS status;

    if (!(((((gctUINT32) (Hardware->chipFeatures)) >> (0 ? 0:0)) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1)))))) ))
    {
        return gcvSTATUS_OK;
    }

    do
    {
        if (Enable == -1)
        {
            Enable = (Hardware->chipModel > gcv500) ||
                ((Hardware->chipModel == gcv500) && (Hardware->chipRevision >= 3));
        }

        gcmkERR_BREAK(gckOS_ReadRegisterEx(Hardware->os, gcvCORE_VG,
                                        0x00414,
                    &debug));

        debug = ((((gctUINT32) (debug)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 20:20) - (0 ? 20:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:20) - (0 ? 20:20) + 1))))))) << (0 ? 20:20))) | (((gctUINT32) ((gctUINT32) (Enable == 0) & ((gctUINT32) ((((1 ? 20:20) - (0 ? 20:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:20) - (0 ? 20:20) + 1))))))) << (0 ? 20:20)));

#ifdef AQ_MEMORY_DEBUG_DISABLE_Z_COMPRESSION
        debug = ((((gctUINT32) (debug)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? AQ_MEMORY_DEBUG_DISABLE_Z_COMPRESSION) - (0 ? AQ_MEMORY_DEBUG_DISABLE_Z_COMPRESSION) + 1) == 32) ? ~0 : (~(~0 << ((1 ? AQ_MEMORY_DEBUG_DISABLE_Z_COMPRESSION) - (0 ? AQ_MEMORY_DEBUG_DISABLE_Z_COMPRESSION) + 1))))))) << (0 ? AQ_MEMORY_DEBUG_DISABLE_Z_COMPRESSION))) | (((gctUINT32) ((gctUINT32) (Enable == 0) & ((gctUINT32) ((((1 ? AQ_MEMORY_DEBUG_DISABLE_Z_COMPRESSION) - (0 ? AQ_MEMORY_DEBUG_DISABLE_Z_COMPRESSION) + 1) == 32) ? ~0 : (~(~0 << ((1 ? AQ_MEMORY_DEBUG_DISABLE_Z_COMPRESSION) - (0 ? AQ_MEMORY_DEBUG_DISABLE_Z_COMPRESSION) + 1))))))) << (0 ? AQ_MEMORY_DEBUG_DISABLE_Z_COMPRESSION)));
#endif

        gcmkERR_BREAK(gckOS_WriteRegisterEx(Hardware->os, gcvCORE_VG,
                                     0x00414,
                     debug));

        Hardware->allowFastClear = Enable;

        status = gcvFALSE;
    }
    while (gcvFALSE);

    return status;
}

gceSTATUS
gckVGHARDWARE_ReadInterrupt(
    IN gckVGHARDWARE Hardware,
    OUT gctUINT32_PTR IDs
    )
{
    gceSTATUS status;
    gcmkHEADER_ARG("Hardware=0x%x IDs=0x%x", Hardware, IDs);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(IDs != gcvNULL);

    /* Read AQIntrAcknowledge register. */
    status = gckOS_ReadRegisterEx(Hardware->os, gcvCORE_VG,
                              0x00010,
                              IDs);
    gcmkFOOTER();
    return status;
}

static gceSTATUS _CommandStall(
    gckVGHARDWARE Hardware)
{
    gceSTATUS status;
    gckVGCOMMAND command;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    do
    {
        gctUINT32_PTR buffer;
        command = Hardware->kernel->command;

        /* Allocate command buffer space. */
        gcmkERR_BREAK(gckVGCOMMAND_Allocate(
            command, 8, &command->powerStallBuffer,
            (gctPOINTER *) &buffer
            ));

        gcmkERR_BREAK(gckVGCOMMAND_EventCommand(
            command, buffer, gcvBLOCK_PIXEL,
            command->powerStallInt, gcvNULL));

        gcmkERR_BREAK(gckVGCOMMAND_Execute(
            command,
            command->powerStallBuffer
            ));

        /* Wait the signal. */
        gcmkERR_BREAK(gckOS_WaitSignal(
            command->os,
            command->powerStallSignal,
            command->kernel->kernel->timeOut));


    }
    while(gcvFALSE);

    gcmkFOOTER();
    /* Return the status. */
    return status;
}

static gceSTATUS
_IsGPUPresent(
    IN gckVGHARDWARE Hardware
    )
{
    gceSTATUS status;
    gceCHIPMODEL chipModel;
    gctUINT32 chipRev, chipFeatures, chipMinorFeatures, chipMinorFeatures2;
    /*gcsHAL_QUERY_CHIP_IDENTITY identity;*/
    gctUINT32 control;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     gcvCORE_VG,
                                     0x00000,
                                     &control));

    control = ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1)));

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      gcvCORE_VG,
                                      0x00000,
                                      control));

    /* Identify the hardware. */
    gcmkONERROR(_IdentifyHardware(Hardware->os,
                                  &chipModel, &chipRev,
                                  &chipFeatures,
                                  &chipMinorFeatures,
                                  &chipMinorFeatures2
                                  ));
    /* Check if these are the same values as saved before. */
    if ((Hardware->chipModel          != chipModel)
    ||  (Hardware->chipRevision       != chipRev)
    ||  (Hardware->chipFeatures       != chipFeatures)
    ||  (Hardware->chipMinorFeatures  != chipMinorFeatures)
    ||  (Hardware->chipMinorFeatures2 != chipMinorFeatures2)
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
gckVGHARDWARE_SetPowerManagementState(
    IN gckVGHARDWARE Hardware,
    IN gceCHIPPOWERSTATE State
    )
{
    gceSTATUS status;
    gckVGCOMMAND command = gcvNULL;
    gckOS os;
    gctUINT flag/*, clock*/;

    gctBOOL acquired        = gcvFALSE;
    gctBOOL stall           = gcvTRUE;
    gctBOOL commitMutex     = gcvFALSE;
    gctBOOL mutexAcquired   = gcvFALSE;

#if gcdPOWEROFF_TIMEOUT
    gctBOOL timeout = gcvFALSE;
    gctBOOL isAfter = gcvFALSE;
    gctUINT32 currentTime;
#endif

    gctBOOL broadcast = gcvFALSE;
    gctUINT32 process, thread;
    gctBOOL global = gcvFALSE;

#if gcdENABLE_PROFILING
    gctUINT64 time, freq, mutexTime, onTime, stallTime, stopTime, delayTime,
              initTime, offTime, startTime, totalTime;
#endif

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
            /* IDLE              */ gcvPOWER_FLAG_NOP,
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
                                    gcvPOWER_FLAG_RELEASE    |
                                    gcvPOWER_FLAG_DELAY,
            /* SUSPEND           */ gcvPOWER_FLAG_INITIALIZE |
                                    gcvPOWER_FLAG_CLOCK_OFF,
        },

        /* gcvPOWER_IDLE         */
        {   /* ON                */ gcvPOWER_FLAG_NOP,
            /* OFF               */ gcvPOWER_FLAG_ACQUIRE   |
                                    gcvPOWER_FLAG_STOP      |
                                    gcvPOWER_FLAG_POWER_OFF |
                                    gcvPOWER_FLAG_CLOCK_OFF,
            /* IDLE              */ 0,
            /* SUSPEND           */ gcvPOWER_FLAG_ACQUIRE   |
                                    gcvPOWER_FLAG_STOP      |
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
                                    gcvPOWER_FLAG_RELEASE   |
                                    gcvPOWER_FLAG_CLOCK_ON,
            /* SUSPEND           */ 0,
        },
    };

    gcmkHEADER_ARG("Hardware=0x%x State=%d", Hardware, State);
#if gcmIS_DEBUG(gcdDEBUG_TRACE)
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Switching to power state %d",
                   State);
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

    if (Hardware->powerManagement == gcvFALSE)
    {
        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

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

    /* Get current process and thread IDs. */
    gcmkONERROR(gckOS_GetProcessID(&process));
    gcmkONERROR(gckOS_GetThreadID(&thread));

    /* Acquire the power mutex. */
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
            else if (State == gcvPOWER_IDLE)
            {
                /* gcvPOWER_IDLE_BROADCAST is from IST,
                ** so waiting here will cause deadlock,
                ** if lock holder call gckCOMMAND_Stall() */
                gcmkONERROR(gcvSTATUS_INVALID_REQUEST);
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
    /*clock = clocks[State];*/

#if gcdPOWEROFF_TIMEOUT
    if (timeout)
    {
        gcmkONERROR(gckOS_GetTicks(&currentTime));

        gcmkONERROR(
            gckOS_TicksAfter(Hardware->powerOffTime, currentTime, &isAfter));

        /* powerOffTime is pushed forward, give up.*/
        if (isAfter
        /* Expect a transition start from IDLE. */
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

    /* internal power control */
    if (!global)
    {
        if (Hardware->chipPowerStateGlobal == gcvPOWER_OFF)
        {
            /* Release the power mutex. */
            gcmkONERROR(gckOS_ReleaseMutex(os, Hardware->powerMutex));

            /* No need to do anything. */
            gcmkFOOTER_NO();
            return gcvSTATUS_OK;
        }
    }
    else
    {
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
        gcmkONERROR(gckOS_SetGPUPower(os, gcvCORE_VG, gcvTRUE, gcvTRUE));

        /* Mark clock and power as enabled. */
        Hardware->clockState = gcvTRUE;
        Hardware->powerState = gcvTRUE;
    }

    for (;;)
    {
        /* Check if GPU is present and awake. */
        status = _IsGPUPresent(Hardware);

        /* Check if the GPU is not responding. */
        if (status == gcvSTATUS_GPU_NOT_RESPONDING)
        {
            /* Turn off the power and clock. */
            gcmkONERROR(gckOS_SetGPUPower(os, gcvCORE_VG, gcvFALSE, gcvFALSE));

            Hardware->clockState = gcvFALSE;
            Hardware->powerState = gcvFALSE;

            /* Wait a little. */
            gckOS_Delay(os, 1);

            /* Turn on the power and clock. */
            gcmkONERROR(gckOS_SetGPUPower(os, gcvCORE_VG, gcvTRUE, gcvTRUE));

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


    /* Get time until powered on. */
    gcmkPROFILE_QUERY(time, onTime);

    if ((flag & gcvPOWER_FLAG_STALL) && stall)
    {
        /* Acquire the mutex. */
        gcmkONERROR(gckOS_AcquireMutex(
            command->os,
            command->commitMutex,
            gcvINFINITE
            ));

        commitMutex = gcvTRUE;

        gcmkONERROR(_CommandStall(Hardware));
    }

    /* Get time until stalled. */
    gcmkPROFILE_QUERY(time, stallTime);

    if (flag & gcvPOWER_FLAG_ACQUIRE)
    {
        /* Acquire the power management semaphore. */
        gcmkONERROR(gckOS_AcquireSemaphore(os, command->powerSemaphore));

        acquired = gcvTRUE;
    }


    /* Get time until stopped. */
    gcmkPROFILE_QUERY(time, stopTime);


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

        /* Initialize GPU here, replaced by InitializeHardware later */
        gcmkONERROR(gckVGHARDWARE_SetMMU(Hardware, Hardware->kernel->mmu->pageTableLogical));
        gcmkVERIFY_OK(gckVGHARDWARE_SetFastClear(Hardware, -1));

        /* Force the command queue to reload the next context. */
        command->currentContext = 0;
    }

    /* Get time until initialized. */
    gcmkPROFILE_QUERY(time, initTime);

    if (flag & (gcvPOWER_FLAG_POWER_OFF | gcvPOWER_FLAG_CLOCK_OFF))
    {
        /* Turn off the GPU power. */
        gcmkONERROR(
            gckOS_SetGPUPower(os,
                              gcvCORE_VG,
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


    /* Get time until started. */
    gcmkPROFILE_QUERY(time, startTime);

    if (flag & gcvPOWER_FLAG_RELEASE)
    {
        /* Release the power management semaphore. */
        gcmkONERROR(gckOS_ReleaseSemaphore(os, command->powerSemaphore));
        acquired = gcvFALSE;
    }

    /* Save the new power state. */
    Hardware->chipPowerState = State;

    if (global)
    {
        /* Save the new power state. */
        Hardware->chipPowerStateGlobal = State;
    }

    if (commitMutex)
    {
        /* Acquire the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(
            command->os,
            command->commitMutex
            ));
    }

#if gcdPOWEROFF_TIMEOUT
    /* Reset power off time */
    gcmkONERROR(gckOS_GetTicks(&currentTime));

    Hardware->powerOffTime = currentTime + Hardware->powerOffTimeout;

    if (State == gcvPOWER_IDLE)
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

    if (acquired)
    {
        /* Release semaphore. */
        gcmkVERIFY_OK(gckOS_ReleaseSemaphore(Hardware->os,
                                             command->powerSemaphore));
    }

    if (mutexAcquired)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Hardware->os, Hardware->powerMutex));
    }

    if (commitMutex)
    {
        /* Acquire the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(
            command->os,
            command->commitMutex
            ));
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
gckVGHARDWARE_QueryPowerManagementState(
    IN gckVGHARDWARE Hardware,
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
**  gckVGHARDWARE_SetPowerManagement
**
**  Configure GPU power management function.
**  Only used in driver initialization stage.
**
**  INPUT:
**
**      gckVGHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**      gctBOOL PowerManagement
**          Power Mangement State.
**
*/
gceSTATUS
gckVGHARDWARE_SetPowerManagement(
    IN gckVGHARDWARE Hardware,
    IN gctBOOL PowerManagement
    )
{
    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    Hardware->powerManagement = PowerManagement;

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

#if gcdPOWEROFF_TIMEOUT
gceSTATUS
gckVGHARDWARE_SetPowerOffTimeout(
    IN gckVGHARDWARE  Hardware,
    IN gctUINT32    Timeout
    )
{
    gcmkHEADER_ARG("Hardware=0x%x Timeout=%d", Hardware, Timeout);

    Hardware->powerOffTimeout = Timeout;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}


gceSTATUS
gckVGHARDWARE_QueryPowerOffTimeout(
    IN gckVGHARDWARE  Hardware,
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
gckVGHARDWARE_QueryIdle(
    IN gckVGHARDWARE Hardware,
    OUT gctBOOL_PTR IsIdle
    )
{
    gceSTATUS status;
    gctUINT32 idle;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(IsIdle != gcvNULL);

    /* We are idle when the power is not ON. */
    if (Hardware->chipPowerState != gcvPOWER_ON)
    {
        *IsIdle = gcvTRUE;
    }

    else
    {
        /* Read idle register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os, gcvCORE_VG, 0x00004, &idle));

        /* Pipe must be idle. */
        if (((((((gctUINT32) (idle)) >> (0 ? 0:0)) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1)))))) ) != 1)
        ||  ((((((gctUINT32) (idle)) >> (0 ? 8:8)) & ((gctUINT32) ((((1 ? 8:8) - (0 ? 8:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:8) - (0 ? 8:8) + 1)))))) ) != 1)
        ||  ((((((gctUINT32) (idle)) >> (0 ? 9:9)) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1)))))) ) != 1)
        ||  ((((((gctUINT32) (idle)) >> (0 ? 10:10)) & ((gctUINT32) ((((1 ? 10:10) - (0 ? 10:10) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 10:10) - (0 ? 10:10) + 1)))))) ) != 1)
        ||  ((((((gctUINT32) (idle)) >> (0 ? 11:11)) & ((gctUINT32) ((((1 ? 11:11) - (0 ? 11:11) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:11) - (0 ? 11:11) + 1)))))) ) != 1)
        )
        {
            /* Something is busy. */
            *IsIdle = gcvFALSE;
        }

        else
        {
            *IsIdle = gcvTRUE;
        }
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}
#endif /* gcdENABLE_VG */

