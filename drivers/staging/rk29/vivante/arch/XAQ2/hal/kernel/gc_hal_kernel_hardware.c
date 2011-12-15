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




#include "gc_hal.h"
#include "gc_hal_kernel.h"

#define _GC_OBJ_ZONE    gcvZONE_HARDWARE

extern uint gpuState;

// dkm: gcdENABLE_AUTO_FREQ
#if (1==gcdENABLE_AUTO_FREQ)
#include <linux/time.h>
#include <linux/clk.h>
u32 usec_run = 0;
u32 usec_idle = 0;
gceCHIPPOWERSTATE lastState = gcvPOWER_IDLE;
struct timeval tv_on, tv_idle;

int nextfreq = 0;
struct clk *clk_gpu = NULL;

void get_run_idle(u32 *run, u32 *idle)
{
    if(gcvPOWER_ON!=lastState)
    {
        do_gettimeofday(&tv_on);
        usec_idle += (1000000*(tv_on.tv_sec-tv_idle.tv_sec)+(tv_on.tv_usec-tv_idle.tv_usec));
        tv_idle = tv_on;
        *idle = usec_idle;
        *run = usec_run;
    } else {
        do_gettimeofday(&tv_idle);
        usec_run += (1000000*(tv_idle.tv_sec-tv_on.tv_sec)+(tv_idle.tv_usec-tv_on.tv_usec));
        tv_on = tv_idle;
        *idle = usec_idle;
        *run = usec_run; 
    }
    usec_idle = 0;
    usec_run = 0;
}

void set_nextfreq(int freq)
{
    nextfreq = freq;
}

inline void cal_run_idle(gceCHIPPOWERSTATE State)
{
    int freq = 0;
    
    if(gcvPOWER_ON!=lastState && gcvPOWER_ON==State)  //NotON -> ON
    {
        do_gettimeofday(&tv_on);
        usec_idle += (1000000*(tv_on.tv_sec-tv_idle.tv_sec)+(tv_on.tv_usec-tv_idle.tv_usec));
    } 
    else if(gcvPOWER_ON==lastState && gcvPOWER_ON!=State)  //ON -> NotON
    {
        do_gettimeofday(&tv_idle);
        usec_run += (1000000*(tv_idle.tv_sec-tv_on.tv_sec)+(tv_idle.tv_usec-tv_on.tv_usec));

        freq = nextfreq;
        nextfreq = 0;
        if(freq) {
            if(freq<24)      freq = 24;
            if(freq>600)     freq = 600;
            clk_gpu = clk_get(NULL, "gpu");
            clk_set_rate(clk_gpu, freq*1000000);
            printk("           == > gpu change freq to %d \n", freq); 
        }
    }
    
    lastState = State;
}
#elif (2==gcdENABLE_AUTO_FREQ)
#include <linux/clk.h>
gceCHIPPOWERSTATE lastState = gcvPOWER_IDLE;
int lasthighfreq = 0;
extern int needhighfreq;
extern int lowfreq;
extern int highfreq;
struct clk *clk_gpu = NULL;
inline void get_idle_change(gceCHIPPOWERSTATE State)
{
    if(gcvPOWER_ON!=lastState && gcvPOWER_ON==State)  //gcvPOWER_IDLE->gcvPOWER_ON
    {
        if(lasthighfreq != needhighfreq) {
            int gpufreq = needhighfreq ? highfreq : lowfreq;
            if(gpufreq<24)      gpufreq = 24;
            if(gpufreq>600)     gpufreq = 600;
            
            clk_gpu = clk_get(NULL, "gpu");
            clk_set_rate(clk_gpu, gpufreq*1000000);
            
            lasthighfreq = needhighfreq;
            
            printk("gpu: change freq to %d, got %ld\n", gpufreq, clk_get_rate(clk_gpu)/1000000);
        }
    }
    lastState = State;
}
#endif

// dkm: gcdENABLE_LONG_IDLE_POWEROFF
#if gcdENABLE_LONG_IDLE_POWEROFF
#include <linux/workqueue.h>
struct delayed_work poweroff_work;
static gckHARDWARE gHardware = gcvNULL;
void time_to_poweroff(struct work_struct *work)
{
    gceSTATUS status;
    if(NULL==gHardware)     return;

    status = gckHARDWARE_SetPowerManagementState(gHardware, gcvPOWER_OFF_BROADCAST);
    if (gcmIS_ERROR(status))
    {
        printk("%s fail!\n", __func__);
        return;
    }
}
#endif

/******************************************************************************\
********************************* Support Code *********************************
\******************************************************************************/

static gceSTATUS
_IdentifyHardware(
    IN gckOS Os,
    OUT gceCHIPMODEL * ChipModel,
    OUT gctUINT32_PTR ChipRevision,
    OUT gctUINT32_PTR ChipFeatures,
    OUT gctUINT32_PTR ChipMinorFeatures0,
    OUT gctUINT32_PTR ChipMinorFeatures1,
    OUT gctUINT32_PTR ChipMinorFeatures2
    )
{
    gceSTATUS status;
    gctUINT32 chipIdentity;

    gcmkHEADER_ARG("Os=0x%x", Os);

    /* Read chip identity register. */
    gcmkONERROR(
        gckOS_ReadRegister(Os, 0x00018, &chipIdentity));

    /* Special case for older graphic cores. */
    if (((((gctUINT32) (chipIdentity)) >> (0 ? 31:24) & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1)))))) == (0x01 & ((gctUINT32) ((((1 ? 31:24) - (0 ? 31:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:24) - (0 ? 31:24) + 1))))))))
    {
        *ChipModel    = gcv500;
        *ChipRevision = ( ((((gctUINT32) (chipIdentity)) >> (0 ? 15:12)) & ((gctUINT32) ((((1 ? 15:12) - (0 ? 15:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:12) - (0 ? 15:12) + 1)))))) );
    }

    else
    {
        /* Read chip identity register. */
        gcmkONERROR(
            gckOS_ReadRegister(Os,
                               0x00020,
                               (gctUINT32_PTR) ChipModel));

        /* !!!! HACK ALERT !!!! */
        /* Because people change device IDs without letting software know
        ** about it - here is the hack to make it all look the same.  Only
        ** for GC400 family.  Next time - TELL ME!!! */
        if ((*ChipModel & 0xFF00) == 0x0400)
        {
            *ChipModel &= 0x0400;
        }

        /* Read CHIP_REV register. */
        gcmkONERROR(
            gckOS_ReadRegister(Os, 0x00024, ChipRevision));

        if ((*ChipModel    == gcv300)
        &&  (*ChipRevision == 0x2201)
        )
        {
            gctUINT32 date, time;

            /* Read date and time registers. */
            gcmkONERROR(
                gckOS_ReadRegister(Os, 0x00028, &date));

            gcmkONERROR(
                gckOS_ReadRegister(Os, 0x0002C, &time));

            if ((date == 0x20080814) && (time == 0x12051100))
            {
                /* This IP has an ECO; put the correct revision in it. */
                *ChipRevision = 0x1051;
            }
        }
    }

    /* Read chip feature register. */
    gcmkONERROR(
        gckOS_ReadRegister(Os, 0x0001C, ChipFeatures));

    /* Disable fast clear on GC700. */
    if (*ChipModel == gcv700)
    {
        *ChipFeatures = ((((gctUINT32) (*ChipFeatures)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));
    }

    if (((*ChipModel == gcv500) && (*ChipRevision < 2))
    ||  ((*ChipModel == gcv300) && (*ChipRevision < 0x2000))
    )
    {
        /* GC500 rev 1.x and GC300 rev < 2.0 doesn't have these registers. */
        *ChipMinorFeatures0 = 0;
        *ChipMinorFeatures1 = 0;
        *ChipMinorFeatures2 = 0;
    }
    else
    {
        /* Read chip minor feature register #0. */
        gcmkONERROR(
            gckOS_ReadRegister(Os,
                               0x00034,
                               ChipMinorFeatures0));

        *ChipMinorFeatures0 = ((((gctUINT32) (*ChipMinorFeatures0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:27) - (0 ? 27:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:27) - (0 ? 27:27) + 1))))))) << (0 ? 27:27))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 27:27) - (0 ? 27:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:27) - (0 ? 27:27) + 1))))))) << (0 ? 27:27)));

        if (((((gctUINT32) (*ChipMinorFeatures0)) >> (0 ? 21:21) & ((gctUINT32) ((((1 ? 21:21) - (0 ? 21:21) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:21) - (0 ? 21:21) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 21:21) - (0 ? 21:21) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:21) - (0 ? 21:21) + 1)))))))
        )
        {
            /* Read chip minor featuress register #1. */
            gcmkONERROR(
                gckOS_ReadRegister(Os,
                                   0x00074,
                                   ChipMinorFeatures1));

            /* Read chip minor featuress register #1. */
#if defined GC_MINOR_FEATURES2_Address
            gcmkONERROR(
                gckOS_ReadRegister(Os,
                                   GC_MINOR_FEATURES2_Address,
                                   ChipMinorFeatures2));
#else
            /* Chip doesn't has minor features register 2. */
            *ChipMinorFeatures2 = 0;
#endif
        }
        else
        {
            /* Chip doesn't has minor features register #1 or 2. */
            *ChipMinorFeatures1 = 0;
            *ChipMinorFeatures2 = 0;
        }
    }

    *ChipMinorFeatures0 = ((((gctUINT32) (*ChipMinorFeatures0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:27) - (0 ? 27:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:27) - (0 ? 27:27) + 1))))))) << (0 ? 27:27))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 27:27) - (0 ? 27:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:27) - (0 ? 27:27) + 1))))))) << (0 ? 27:27)));

    /* Success. */
    gcmkFOOTER_ARG("*ChipModel=%x *ChipRevision=%x *ChipFeatures=0x%08x "
                   "*ChipMinorFeatures0=0x%08X *ChipMinorFeatures1=0x%08x "
                   "*ChipMinorFeatures2=0x%08x",
                   *ChipModel, *ChipRevision, *ChipFeatures,
                   *ChipMinorFeatures0, *ChipMinorFeatures1,
                   *ChipMinorFeatures2);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

static gceSTATUS
_GetChipSpecs(
    IN gckHARDWARE Hardware
    )
{
    gctUINT32 streamCount = 0;
    gctUINT32 registerMax = 0;
    gctUINT32 threadCount = 0;
    gctUINT32 shaderCoreCount = 0;
    gctUINT32 vertexCacheSize = 0;
    gctUINT32 vertexOutputBufferSize = 0;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    if (((((gctUINT32) (Hardware->chipMinorFeatures0)) >> (0 ? 21:21) & ((gctUINT32) ((((1 ? 21:21) - (0 ? 21:21) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:21) - (0 ? 21:21) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 21:21) - (0 ? 21:21) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:21) - (0 ? 21:21) + 1))))))))
    {
        gctUINT32 specs;

        /* Read gcChipSpecs register. */
        gcmkONERROR(
            gckOS_ReadRegister(Hardware->os, 0x00048, &specs));

        /* Handy macro to improve reading. */
#define gcmSPEC_FIELD(field) \
        ( ((((gctUINT32) (specs)) >> (0 ? GC_CHIP_SPECS_field)) & ((gctUINT32) ((((1 ? GC_CHIP_SPECS_field) - (0 ? GC_CHIP_SPECS_field) + 1) == 32) ? ~0 : (~(~0 << ((1 ? GC_CHIP_SPECS_field) - (0 ? GC_CHIP_SPECS_field) + 1)))))) )

        /* Extract the fields. */
        streamCount            = ( ((((gctUINT32) (specs)) >> (0 ? 3:0)) & ((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1)))))) );
        registerMax            = ( ((((gctUINT32) (specs)) >> (0 ? 7:4)) & ((gctUINT32) ((((1 ? 7:4) - (0 ? 7:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:4) - (0 ? 7:4) + 1)))))) );
        threadCount            = ( ((((gctUINT32) (specs)) >> (0 ? 11:8)) & ((gctUINT32) ((((1 ? 11:8) - (0 ? 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1)))))) );
        shaderCoreCount        = ( ((((gctUINT32) (specs)) >> (0 ? 24:20)) & ((gctUINT32) ((((1 ? 24:20) - (0 ? 24:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 24:20) - (0 ? 24:20) + 1)))))) );
        vertexCacheSize        = ( ((((gctUINT32) (specs)) >> (0 ? 16:12)) & ((gctUINT32) ((((1 ? 16:12) - (0 ? 16:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:12) - (0 ? 16:12) + 1)))))) );
        vertexOutputBufferSize = ( ((((gctUINT32) (specs)) >> (0 ? 31:28)) & ((gctUINT32) ((((1 ? 31:28) - (0 ? 31:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:28) - (0 ? 31:28) + 1)))))) );
    }

    /* Get the stream count. */
    Hardware->streamCount = (streamCount != 0)
                          ? streamCount
                          : (Hardware->chipModel >= gcv1000) ? 4 : 1;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: streamCount=%u%s",
                   Hardware->streamCount,
                   (streamCount == 0) ? " (default)" : "");

    /* Get the vertex output buffer size. */
    Hardware->vertexOutputBufferSize = (vertexOutputBufferSize != 0)
                                     ? 1 << vertexOutputBufferSize
                                     : (Hardware->chipModel == gcv400)
                                       ? (Hardware->chipRevision < 0x4000) ? 512
                                       : (Hardware->chipRevision < 0x4200) ? 256
                                       : 128
                                     : (Hardware->chipModel == gcv530)
                                       ? (Hardware->chipRevision < 0x4200) ? 512
                                       : 128
                                     : 512;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: vertexOutputBufferSize=%u%s",
                   Hardware->vertexOutputBufferSize,
                   (vertexOutputBufferSize == 0) ? " (default)" : "");

    /* Get the maximum number of threads. */
    Hardware->threadCount = (threadCount != 0)
                          ? 1 << threadCount
                          : (Hardware->chipModel == gcv400) ? 64
                          : (Hardware->chipModel == gcv500) ? 128
                          : (Hardware->chipModel == gcv530) ? 128
                          : 256;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: threadCount=%u%s",
                   Hardware->threadCount,
                   (threadCount == 0) ? " (default)" : "");

    /* Get the number of shader cores. */
    Hardware->shaderCoreCount = (shaderCoreCount != 0)
                              ? shaderCoreCount
                              : (Hardware->chipModel >= gcv1000) ? 2
                              : 1;
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: shaderCoreCount=%u%s",
                   Hardware->shaderCoreCount,
                   (shaderCoreCount == 0) ? " (default)" : "");

    /* Get the vertex cache size. */
    Hardware->vertexCacheSize = (vertexCacheSize != 0)
                              ? vertexCacheSize
                              : 8;
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: vertexCacheSize=%u%s",
                   Hardware->vertexCacheSize,
                   (vertexCacheSize == 0) ? " (default)" : "");

    /* Get the maximum number of temporary registers. */
    Hardware->registerMax = (registerMax != 0)
                          ? 1 << registerMax
                          : (Hardware->chipModel == gcv400) ? 32
                          : 64;
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: registerMax=%u%s",
                   Hardware->registerMax,
                   (registerMax == 0) ? " (default)" : "");

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
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
**  OUTPUT:
**
**      gckHARDWARE * Hardware
**          Pointer to a variable that will hold the pointer to the gckHARDWARE
**          object.
*/
gceSTATUS
gckHARDWARE_Construct(
    IN gckOS Os,
    OUT gckHARDWARE * Hardware
    )
{
    gckHARDWARE hardware = gcvNULL;
    gceSTATUS status;
    gceCHIPMODEL chipModel;
    gctUINT32 chipRevision;
    gctUINT32 chipFeatures;
    gctUINT32 chipMinorFeatures0;
    gctUINT32 chipMinorFeatures1;
    gctUINT32 chipMinorFeatures2;
    gctUINT16 data = 0xff00;

    gcmkHEADER_ARG("Os=0x%x", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Hardware != gcvNULL);

    /* Enable the GPU. */
    gcmkONERROR(gckOS_SetGPUPower(Os, gcvTRUE, gcvTRUE));
    gcmkONERROR(gckOS_WriteRegister(Os, 0x00000, 0));

    /* Identify the hardware. */
    gcmkONERROR(_IdentifyHardware(Os,
                                  &chipModel,
                                  &chipRevision,
                                  &chipFeatures,
                                  &chipMinorFeatures0,
                                  &chipMinorFeatures1,
                                  &chipMinorFeatures2));

    /* Allocate the gckHARDWARE object. */
    gcmkONERROR(gckOS_Allocate(Os,
                               gcmSIZEOF(struct _gckHARDWARE),
                               (gctPOINTER *) &hardware));

    /* Initialize the gckHARDWARE object. */
    hardware->object.type = gcvOBJ_HARDWARE;
    hardware->os          = Os;

    /* Set chip identity. */
    hardware->chipModel          = chipModel;
    hardware->chipRevision       = chipRevision;
    hardware->chipFeatures       = chipFeatures;
    hardware->chipMinorFeatures0 = chipMinorFeatures0;
    hardware->chipMinorFeatures1 = chipMinorFeatures1;
    hardware->chipMinorFeatures2 = chipMinorFeatures2;
    hardware->powerBaseAddress   = (  (chipModel == gcv300)
                                   && (chipRevision < 0x2000)
                                   ) ? 0x100 : 0x00;
    hardware->powerMutex         = gcvNULL;

    /* Get chip specs. */
    gcmkONERROR(_GetChipSpecs(hardware));

    /* Determine whether bug fixes #1 are present. */
    hardware->extraEventStates = ((((gctUINT32) (chipMinorFeatures1)) >> (0 ? 3:3) & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1)))))) == (0x0 & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1)))))));

    /* Check if big endian */
    hardware->bigEndian = (*(gctUINT8 *)&data == 0xff);

    /* Initialize the fast clear. */
    gcmkONERROR(gckHARDWARE_SetFastClear(hardware, -1, -1));

    /* Set power state to ON. */
    hardware->chipPowerState    = gcvPOWER_ON;
    hardware->broadcast         = gcvFALSE;
    hardware->settingPowerState = gcvFALSE;

    hardware->lastWaitLink   = ~0U;

    gcmkONERROR(gckOS_CreateMutex(Os, &hardware->powerMutex));

    /* Return pointer to the gckHARDWARE object. */
    *Hardware = hardware;

// dkm: gcdENABLE_LONG_IDLE_POWEROFF
#if gcdENABLE_LONG_IDLE_POWEROFF
    INIT_DELAYED_WORK(&poweroff_work, time_to_poweroff);
    gHardware = hardware;
#endif

    /* Success. */
    gcmkFOOTER_ARG("*Hardware=0x%x", *Hardware);
    return gcvSTATUS_OK;

OnError:
    /* Roll back. */
    if (hardware->powerMutex != gcvNULL)
    {
        gcmkVERIFY_OK(gckOS_DeleteMutex(Os, hardware->powerMutex));
    }

    if (hardware != gcvNULL)
    {
        gcmkVERIFY_OK(gckOS_Free(Os, hardware));
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

    /* Destroy the power mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Hardware->os, Hardware->powerMutex));

    /* Mark the object as unknown. */
    Hardware->object.type = gcvOBJ_UNKNOWN;

    /* Free the object. */
    gcmkONERROR(gckOS_Free(Hardware->os, Hardware));

    /* Success. */
    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
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

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Read the chip revision register. */
    gcmkONERROR(gckOS_ReadRegister(Hardware->os,
                                   0x00024,
                                   &chipRev));

    if (chipRev != Hardware->chipRevision)
    {
        /* Chip is not there! */
        gcmkONERROR(gcvSTATUS_CONTEXT_LOSSED);
    }

    /* Disable isolate GPU bit. */
    gcmkONERROR(gckOS_WriteRegister(Hardware->os,
                                    0x00000,
                                    ((((gctUINT32) (0x00000100)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ? 19:19))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ? 19:19)))));

    /* Reset memory counters. */
    gcmkONERROR(gckOS_WriteRegister(Hardware->os,
                                    0x0003C,
                                    ~0U));

    gcmkONERROR(gckOS_WriteRegister(Hardware->os,
                                    0x0003C,
                                    0));

    /* Get the system's physical base address. */
    gcmkONERROR(gckOS_GetBaseAddress(Hardware->os, &baseAddress));

    /* Program the base addesses. */
    gcmkONERROR(gckOS_WriteRegister(Hardware->os,
                                    0x0041C,
                                    baseAddress));

    gcmkONERROR(gckOS_WriteRegister(Hardware->os,
                                    0x00418,
                                    baseAddress));

    gcmkONERROR(gckOS_WriteRegister(Hardware->os,
                                    0x00420,
                                    baseAddress));

    gcmkONERROR(gckOS_WriteRegister(Hardware->os,
                                    0x00428,
                                    baseAddress));

    gcmkONERROR(gckOS_WriteRegister(Hardware->os,
                                    0x00424,
                                    baseAddress));

#if !VIVANTE_PROFILER && 1
    {
        gctUINT32 data;

        gcmkONERROR(gckOS_ReadRegister(Hardware->os,
                                       Hardware->powerBaseAddress +
                                       0x00100,
                                       &data));

        /* Enable clock gating. */
        data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));

        if ((Hardware->chipRevision == 0x4301)
        ||  (Hardware->chipRevision == 0x4302)
        )
        {
            /* Disable stall module level clock gating for 4.3.0.1 and 4.3.0.2
            ** revisions. */
            data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1)));
        }

        gcmkONERROR(gckOS_WriteRegister(Hardware->os,
                                        Hardware->powerBaseAddress
                                        + 0x00100,
                                        data));

        /* Disable PE clock gating on revs < 5.0 when HZ is present without a
        ** bug fix. */
        if ((Hardware->chipRevision < 0x5000)
        &&  ((((gctUINT32) (Hardware->chipMinorFeatures1)) >> (0 ? 9:9) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1)))))) == (0x0 & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1)))))))
        &&  ((((gctUINT32) (Hardware->chipMinorFeatures0)) >> (0 ? 27:27) & ((gctUINT32) ((((1 ? 27:27) - (0 ? 27:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:27) - (0 ? 27:27) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 27:27) - (0 ? 27:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:27) - (0 ? 27:27) + 1)))))))
        )
        {
            gcmkONERROR(
                gckOS_ReadRegister(Hardware->os,
                                   Hardware->powerBaseAddress
                                   + 0x00104,
                                   &data));

            /* Disable PE clock gating. */
            data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2)));

            gcmkONERROR(
                gckOS_WriteRegister(Hardware->os,
                                    Hardware->powerBaseAddress
                                    + 0x00104,
                                    data));
        }
    }
#endif

    /* Test if MMU is initialized. */
    if ((Hardware->kernel      != gcvNULL)
    &&  (Hardware->kernel->mmu != gcvNULL)
    )
    {
        /* Reset MMU. */
        gcmkONERROR(
            gckHARDWARE_SetMMU(Hardware,
                               Hardware->kernel->mmu->pageTableLogical));
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
**      gctUINT32 * ChipMinorFeatures1
**          If 'ChipMinorFeatures1' is not gcvNULL, the variable it points to
**          will receive the minor feature set 1 of the chip.
**
**      gctUINT32 * ChipMinorFeatures2
**          If 'ChipMinorFeatures2' is not gcvNULL, the variable it points to
**          will receive the minor feature set 2 of the chip.
**
*/
gceSTATUS
gckHARDWARE_QueryChipIdentity(
    IN gckHARDWARE Hardware,
    OUT gceCHIPMODEL * ChipModel,
    OUT gctUINT32 * ChipRevision,
    OUT gctUINT32* ChipFeatures,
    OUT gctUINT32* ChipMinorFeatures,
    OUT gctUINT32* ChipMinorFeatures1,
    OUT gctUINT32* ChipMinorFeatures2
    )
{
    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

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

        if (( ((((gctUINT32) (features)) >> (0 ? 0:0)) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1)))))) ))
        {
            /* Override fast clear by command line. */
            features = ((((gctUINT32) (features)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (Hardware->allowFastClear) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)));
        }

        if (( ((((gctUINT32) (features)) >> (0 ? 5:5)) & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1)))))) ))
        {
            /* Override compression by command line. */
            features = ((((gctUINT32) (features)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5))) | (((gctUINT32) ((gctUINT32) (Hardware->allowCompression) & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5)));
        }

        /* Mark 2D pipe as available for GC500.0 through GC500.2 and GC300,
        ** since they did not have this bit. */
        if ((  (Hardware->chipModel == gcv500)
            && (Hardware->chipRevision <= 2)
            )
        ||  (Hardware->chipModel == gcv300)
        )
        {
            features = ((((gctUINT32) (features)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9)));
        }

        *ChipFeatures = features;
    }

    /* Return minor feature set. */
    if (ChipMinorFeatures != gcvNULL)
    {
        *ChipMinorFeatures = Hardware->chipMinorFeatures0;
    }

    /* Return minor feature set 1. */
    if (ChipMinorFeatures1 != gcvNULL)
    {
        *ChipMinorFeatures1 = Hardware->chipMinorFeatures1;
    }

    /* Return minor feature set 2. */
    if (ChipMinorFeatures2 != gcvNULL)
    {
        *ChipMinorFeatures2 = Hardware->chipMinorFeatures2;
    }

    /* Success. */
    gcmkFOOTER_ARG("*ChipModel=0x%x *ChipRevision=0x%x *ChipFeatures=0x%08x "
                   "*ChipMinorFeatures=0x%08x *ChipMinorFeatures1=0x%08x "
                   "*ChipMinorFeatures2=0x%08x",
                   gcmOPT_VALUE(ChipModel), gcmOPT_VALUE(ChipRevision),
                   gcmOPT_VALUE(ChipFeatures), gcmOPT_VALUE(ChipMinorFeatures),
                   gcmOPT_VALUE(ChipMinorFeatures1),
                   gcmOPT_VALUE(ChipMinorFeatures2));

    return gcvSTATUS_OK;
}

gceSTATUS
gckHARDWARE_QueryChipSpecs(
    IN gckHARDWARE Hardware,
    OUT gctUINT32_PTR StreamCount,
    OUT gctUINT32_PTR RegisterMax,
    OUT gctUINT32_PTR ThreadCount,
    OUT gctUINT32_PTR ShaderCoreCount,
    OUT gctUINT32_PTR VertexCacheSize,
    OUT gctUINT32_PTR VertexOutputBufferSize
    )
{
    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Return the number of streams. */
    if (StreamCount != gcvNULL)
    {
        *StreamCount = Hardware->streamCount;
    }

    /* Return the number of temporary registers. */
    if (RegisterMax != gcvNULL)
    {
        *RegisterMax = Hardware->registerMax;
    }

    /* Return the maximum number of thrteads. */
    if (ThreadCount != gcvNULL)
    {
        *ThreadCount = Hardware->threadCount;
    }

    /* Return the number of shader cores. */
    if (ShaderCoreCount != gcvNULL)
    {
        *ShaderCoreCount = Hardware->shaderCoreCount;
    }

    /* Return the number of entries in the vertex cache. */
    if (VertexCacheSize != gcvNULL)
    {
        *VertexCacheSize = Hardware->vertexCacheSize;
    }

    /* Return the number of entries in the vertex output buffer. */
    if (VertexOutputBufferSize != gcvNULL)
    {
        *VertexOutputBufferSize = Hardware->vertexOutputBufferSize;
    }

    /* Success. */
    gcmkFOOTER_ARG("*StreamCount=%u *RegisterMax=%u *ThreadCount=%u "
                   "*ShaderCoreCount=%u *VertexCacheSize=%u "
                   "*VertexOutputBufferSize=%u",
                   gcmOPT_VALUE(StreamCount), gcmOPT_VALUE(RegisterMax),
                   gcmOPT_VALUE(ThreadCount), gcmOPT_VALUE(ShaderCoreCount),
                   gcmOPT_VALUE(VertexCacheSize),
                   gcmOPT_VALUE(VertexOutputBufferSize));
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckHARDWARE_ConvertFormat
**
**  Convert an API format to hardware parameters.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object.
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
gckHARDWARE_ConvertFormat(
    IN gckHARDWARE Hardware,
    IN gceSURF_FORMAT Format,
    OUT gctUINT32 * BitsPerPixel,
    OUT gctUINT32 * BytesPerTile
    )
{
    gctUINT32 bitsPerPixel;
    gctUINT32 bytesPerTile;

    gcmkHEADER_ARG("Hardware=0x%x Format=%d", Hardware, Format);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Dispatch on format. */
    switch (Format)
    {
    case gcvSURF_INDEX8:
    case gcvSURF_A8:
    case gcvSURF_L8:
        /* 8-bpp format. */
        bitsPerPixel  = 8;
        bytesPerTile  = (8 * 4 * 4) / 8;
        break;

    case gcvSURF_YV12:
    case gcvSURF_I420:
    case gcvSURF_NV12:
    case gcvSURF_NV21:
        /* 12-bpp planar YUV formats. */
        bitsPerPixel  = 12;
        bytesPerTile  = (12 * 4 * 4) / 8;
        break;

    case gcvSURF_A8L8:
    case gcvSURF_X4R4G4B4:
    case gcvSURF_A4R4G4B4:
    case gcvSURF_X1R5G5B5:
    case gcvSURF_A1R5G5B5:
    case gcvSURF_R5G5B5X1:
    case gcvSURF_R4G4B4X4:
    case gcvSURF_X4B4G4R4:
    case gcvSURF_X1B5G5R5:
    case gcvSURF_B4G4R4X4:
    case gcvSURF_R5G6B5:
    case gcvSURF_B5G5R5X1:
    case gcvSURF_YUY2:
    case gcvSURF_UYVY:
    case gcvSURF_YVYU:
    case gcvSURF_VYUY:
    case gcvSURF_NV16:
    case gcvSURF_NV61:
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
    case gcvSURF_D32:
        /* 32-bpp format. */
        bitsPerPixel  = 32;
        bytesPerTile  = (32 * 4 * 4) / 8;
        break;

    case gcvSURF_D24S8:
    case gcvSURF_D24X8:
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
        gcmkFOOTER_ARG("status=%d", gcvSTATUS_INVALID_ARGUMENT);
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

    /* Success. */
    gcmkFOOTER_ARG("*BitsPerPixel=%u *BytesPerTile=%u",
                   gcmOPT_VALUE(BitsPerPixel), gcmOPT_VALUE(BytesPerTile));
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
    gcmkHEADER_ARG("Hardware=0x%x Addres=%08x", Hardware, Address);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Pool != gcvNULL);
    gcmkVERIFY_ARGUMENT(Offset != gcvNULL);

    /* Dispatch on memory type. */
    switch (( ((((gctUINT32) (Address)) >> (0 ? 31:31)) & ((gctUINT32) ((((1 ? 31:31) - (0 ? 31:31) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1)))))) ))
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
    *Offset = ( ((((gctUINT32) (Address)) >> (0 ? 30:0)) & ((gctUINT32) ((((1 ? 30:0) - (0 ? 30:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 30:0) - (0 ? 30:0) + 1)))))) );

    /* Success. */
    gcmkFOOTER_ARG("*Pool=%d *Offset=%08x", *Pool, *Offset);
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
**      gctPOINTER Logical
**          Logical address of command buffer.
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
    IN gctPOINTER Logical,
#ifdef __QNXNTO__
    IN gctPOINTER Physical,
    IN gctBOOL PhysicalAddresses,
#endif
    IN gctSIZE_T Bytes
    )
{
    gceSTATUS status;
    gctUINT32 address = 0, control;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x Bytes=%lu",
                   Hardware, Logical, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

#ifdef __QNXNTO__
    if (PhysicalAddresses)
    {
        /* Convert physical into hardware specific address. */
        gcmkONERROR(
            gckHARDWARE_ConvertPhysical(Hardware, Physical, &address));
    }
    else
    {
#endif
    /* Convert logical into hardware specific address. */
    gcmkONERROR(
        gckHARDWARE_ConvertLogical(Hardware, Logical, &address));
#ifdef __QNXNTO__
    }
#endif

    /* Enable all events. */
    gcmkONERROR(
        gckOS_WriteRegister(Hardware->os, 0x00014, ~0U));

    /* Write address register. */
    gcmkONERROR(
        gckOS_WriteRegister(Hardware->os, 0x00654, address));

    /* Build control register. */
    control = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ? 16:16))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1))))))) << (0 ? 16:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) ((Bytes+7)>>3) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

    /* Set big endian */
    if (Hardware->bigEndian)
    {
        control |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 21:20) - (0 ? 21:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:20) - (0 ? 21:20) + 1))))))) << (0 ? 21:20))) | (((gctUINT32) (0x2 & ((gctUINT32) ((((1 ? 21:20) - (0 ? 21:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:20) - (0 ? 21:20) + 1))))))) << (0 ? 21:20)));
    }

    /* Write control register. */
    gcmkONERROR(
        gckOS_WriteRegister(Hardware->os, 0x00658, control));

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                  "Started command buffer @ %08x",
                  address);

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
**      gctPOINTER * Wait
**          Pointer to a variable that will receive the pointer to the WAIT
**          command.  If 'Wait' is gcvNULL nothing will be returned.
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
    IN OUT gctSIZE_T * Bytes,
    OUT gctPOINTER * Wait,
    OUT gctSIZE_T * WaitSize
    )
{
    gceSTATUS status;
    gctUINT32 address;
    gctUINT32_PTR logical = (gctUINT32_PTR) Logical;
    gctSIZE_T bytes;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x Offset=%08x *Bytes=%lu",
                   Hardware, Logical, Offset, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT((Logical == gcvNULL) || (Bytes != gcvNULL));

    /* Compute number of bytes required. */
    bytes = gcmALIGN(Offset + 16, 8) - Offset;

    if (Logical != gcvNULL)
    {
        /* Convert logical into hardware specific address. */
        gcmkONERROR(
            gckHARDWARE_ConvertLogical(Hardware,
                                       Logical,
                                       &address));

        if (*Bytes < bytes)
        {
            /* Command queue too small. */
            gcmkONERROR(gcvSTATUS_BUFFER_TOO_SMALL);
        }

        /* Append WAIT(200). */
        logical[0] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                   | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (200) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "0x%08x: WAIT", address);

        /* Append LINK(2, address). */
        logical[2] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x08 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                   | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (bytes>>3) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        logical[3] = address;

        Hardware->lastWaitLink = address;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "0x%08x: LINK %08x, #%lu",
                       address + 8, address, bytes);

        if (Wait != gcvNULL)
        {
            /* Return pointer to WAIT command. */
            *Wait = Logical;
        }

        if (WaitSize != gcvNULL)
        {
            /* Return number of bytes used by the WAIT command. */
            *WaitSize = 8;
        }
    }

    if (Bytes != gcvNULL)
    {
        /* Return number of bytes required by the WAIT/LINK command
        ** sequence. */
        *Bytes = bytes;
    }

    /* Success. */
    gcmkFOOTER_ARG("*Bytes=%lu *Wait=0x%x *WaitSize=%lu",
                   gcmOPT_VALUE(Bytes), gcmOPT_POINTER(Wait),
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

        /* Append END. */
       logical[0] =
            ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x02 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE, "0x%x: END", Logical);

        /* Make sure the CPU writes out the data to memory. */
        gcmkVERIFY_OK(
            gckOS_MemoryBarrier(Hardware->os, Logical));
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
**  gckHARDWARE_Wait
**
**  Append a WAIT command at the specified location in the command queue.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      gctPOINTER Logical
**          Pointer to the current location inside the command queue to append
**          WAIT command at or gcvNULL just to query the size of the WAIT command.
**
**      gctUINT32 Count
**          Number of cycles to wait.
**
**      gctSIZE_T * Bytes
**          Pointer to the number of bytes available for the WAIT command.  If
**          'Logical' is gcvNULL, this argument will be ignored.
**
**  OUTPUT:
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          for the NOP command.  If 'Bytes' is gcvNULL, nothing will be returned.
*/
gceSTATUS
gckHARDWARE_Wait(
    IN gckHARDWARE Hardware,
    IN gctPOINTER Logical,
    IN gctUINT32 Count,
    IN OUT gctSIZE_T * Bytes
    )
{
    gctUINT32_PTR logical = (gctUINT32_PTR) Logical;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x Count=%u *Bytes=%lu",
                   Hardware, Logical, Count, gcmOPT_VALUE(Bytes));

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

        /* Append WAIT. */
        logical[0] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                   | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (Count) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "0x%x: WAIT %u", Logical, Count);
    }

    if (Bytes != gcvNULL)
    {
        /* Return number of bytes required by the WAIT command. */
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
    IN OUT gctSIZE_T * Bytes
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
            destination = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5))) | (((gctUINT32) (0x1&((gctUINT32)((((1?5:5)-(0?5:5)+1)==32)?~0:(~(~0<<((1?5:5)-(0?5:5)+1)))))))<<(0?5:5)));
            break;

        case gcvKERNEL_PIXEL:
            /* From pixel engine. */
            destination = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6))) | (((gctUINT32) (0x1&((gctUINT32)((((1?6:6)-(0?6:6)+1)==32)?~0:(~(~0<<((1?6:6)-(0?6:6)+1)))))))<<(0?6:6)));
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

#if gcdDEBUG
        {
            gctUINT32 phys;
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
**      gctUINT32 Pipe
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
    IN gctUINT32 Pipe,
    IN OUT gctSIZE_T * Bytes
    )
{
    gctUINT32_PTR logical = (gctUINT32_PTR) Logical;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x Pipe=%u *Bytes=%lu",
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

        flush = (Pipe == 0x1)
              ? ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) (0x1&((gctUINT32)((((1?1:1)-(0?1:1)+1)==32)?~0:(~(~0<<((1?1:1)-(0?1:1)+1)))))))<<(0?1:1)))
              | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) (0x1&((gctUINT32)((((1?0:0)-(0?0:0)+1)==32)?~0:(~(~0<<((1?0:0)-(0?0:0)+1)))))))<<(0?0:0)))
              : ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3))) | (((gctUINT32) (0x1&((gctUINT32)((((1?3:3)-(0?3:3)+1)==32)?~0:(~(~0<<((1?3:3)-(0?3:3)+1)))))))<<(0?3:3)));

        stall = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
              | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* LoadState(AQFlush, 1), flush. */
        logical[0] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                   | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E03) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
                   | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

        logical[1] = flush;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "0x%x: FLUSH %x", logical, flush);

        /* LoadState(AQSempahore, 1), stall. */
        logical[2] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                   | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
                   | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

        logical[3] = stall;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "0x%x: SEMAPHORE %x", logical + 2, stall);

        /* Stall, stall. */
        logical[4] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

        logical[5] = stall;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "0x%x: STALL %x", logical + 4, stall);

        /* LoadState(AQPipeSelect, 1), pipe. */
        logical[6] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                   | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E00) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
                   | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

        logical[7] = Pipe;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "0x%x: PIPE %u", logical + 6, Pipe);
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
**      gctPOINTER FetchAddress
**          Logical address of destination of LINK.
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
    IN gctPOINTER FetchAddress,
    IN gctSIZE_T FetchSize,
    IN OUT gctSIZE_T * Bytes
    )
{
    gceSTATUS status;
    gctSIZE_T bytes;
    gctUINT32 address;
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

        /* Convert logical address to hardware address. */
        gcmkONERROR(
            gckHARDWARE_ConvertLogical(Hardware, FetchAddress, &address));

        logical[1] = address;

        /* Make sure the address got written before the LINK command. */
        gcmkONERROR(
            gckOS_MemoryBarrier(Hardware->os, logical + 1));

        /* Compute number of 64-byte aligned bytes to fetch. */
        bytes = gcmALIGN(address + FetchSize, 64) - address;

        /* Append LINK(bytes / 8), FetchAddress. */
        logical[0] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x08 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                   | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (bytes>>3) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

#if gcdDEBUG
        {
            gctUINT32 phys;
            gckHARDWARE_ConvertLogical(Hardware, Logical, &phys);
            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                           "0x%08x: LINK %08x, #%lu", phys, address, bytes);
        }
#endif

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
**  gckHARDWARE_AlignToTile
**
**  Align the specified width and height to tile boundaries.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
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
**
**      gctBOOL_PTR SuperTiled
**          Pointer to a variable that receives the super-tiling flag for the
**          surface.
*/
gceSTATUS
gckHARDWARE_AlignToTile(
    IN gckHARDWARE Hardware,
    IN gceSURF_TYPE Type,
    IN OUT gctUINT32_PTR Width,
    IN OUT gctUINT32_PTR Height,
    OUT gctBOOL_PTR SuperTiled
    )
{
    gctBOOL superTiled = gcvFALSE;
    gctUINT32 xAlignment, yAlignment;

    gcmkHEADER_ARG("Hardware=0x%x Type=%d *Width=%u *Height=%u",
                   Hardware, Type, gcmOPT_VALUE(Width), gcmOPT_VALUE(Height));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Super tiling can be enabled for render targets and depth buffers. */
    superTiled =
        (  (Type == gcvSURF_RENDER_TARGET)
        || (Type == gcvSURF_DEPTH)
        )
        &&
        /* Of course, hardware needs to support super tiles. */
        ((((gctUINT32) (Hardware->chipMinorFeatures0)) >> (0 ? 12:12) & ((gctUINT32) ((((1 ? 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1)))))));

    /* Compute alignment factors. */
    xAlignment = superTiled ? 64
               : (Type == gcvSURF_TEXTURE) ? 4
               : 16;
    yAlignment = superTiled ? 64 : 4;

    if (Width != gcvNULL)
    {
        /* Align the width. */
        *Width = gcmALIGN(*Width, xAlignment);
    }

    if (Height != gcvNULL)
    {
        /* Align the height. */
        *Height = gcmALIGN(*Height, yAlignment);
    }

    if (SuperTiled != gcvNULL)
    {
        /* Copy the super tiling. */
        *SuperTiled = superTiled;
    }

    /* Success. */
    gcmkFOOTER_ARG("*Width=%u *Height=%u *SuperTiled=%d",
                   gcmOPT_VALUE(Width), gcmOPT_VALUE(Height),
                   gcmOPT_VALUE(SuperTiled));
    return gcvSTATUS_OK;
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

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x Offset=%08x",
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
    OUT gctUINT32 * Address
    )
{
    gctUINT32 address;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x", Hardware, Logical);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    /* Convert logical address into a physical address. */
    gcmkONERROR(
        gckOS_GetPhysicalAddress(Hardware->os, Logical, &address));

    /* Return hardware specific address. */
    *Address = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:31) - (0 ? 31:31) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1))))))) << (0 ? 31:31))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 31:31) - (0 ? 31:31) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1))))))) << (0 ? 31:31)))
             | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 30:0) - (0 ? 30:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 30:0) - (0 ? 30:0) + 1))))))) << (0 ? 30:0))) | (((gctUINT32) ((gctUINT32) (address) & ((gctUINT32) ((((1 ? 30:0) - (0 ? 30:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 30:0) - (0 ? 30:0) + 1))))))) << (0 ? 30:0)));

    /* Success. */
    gcmkFOOTER_ARG("*Address=%08x", *Address);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_ConvertPhysical
**
**  Convert a physical address into a hardware specific address.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      gctPHYS_ADDR Physical
**          Physical address to convert.
**
**      gctUINT32* Address
**          Return hardware specific address.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_ConvertPhysical(
    IN gckHARDWARE Hardware,
    IN gctPHYS_ADDR Physical,
    OUT gctUINT32 * Address
    )
{
    gctUINT32 address;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    address = (gctUINT32)Physical;

    /* Return hardware specific address. */
    *Address = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:31) - (0 ? 31:31) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1))))))) << (0 ? 31:31))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 31:31) - (0 ? 31:31) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:31) - (0 ? 31:31) + 1))))))) << (0 ? 31:31)))
             | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 30:0) - (0 ? 30:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 30:0) - (0 ? 30:0) + 1))))))) << (0 ? 30:0))) | (((gctUINT32) ((gctUINT32) (address) & ((gctUINT32) ((((1 ? 30:0) - (0 ? 30:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 30:0) - (0 ? 30:0) + 1))))))) << (0 ? 30:0)));

    /* Return the status. */
    gcmkFOOTER_ARG("*Address=%08x", *Address);
    return gcvSTATUS_OK;
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
    gckEVENT event;
    gctUINT32 data;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x InterruptValid=%d", Hardware, InterruptValid);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Extract gckEVENT object. */
    event = Hardware->kernel->event;
    gcmkVERIFY_OBJECT(event, gcvOBJ_EVENT);

    if (InterruptValid)
    {
        /* Read AQIntrAcknowledge register. */
        gcmkONERROR(
            gckOS_ReadRegister(Hardware->os,
                               0x00010,
                               &data));

#if gcdDEBUG
        if (data & 0x80000000)
        {
            gcmkONERROR(gckOS_Broadcast(Hardware->os,
                                        Hardware,
                                        gcvBROADCAST_AXI_BUS_ERROR));
        }
#endif

        if (data == 0)
        {
            /* Not our interrupt. */
            status = gcvSTATUS_NOT_OUR_INTERRUPT;
        }
        else
        {
            /* Inform gckEVENT of the interrupt. */
            status = gckEVENT_Interrupt(event, data & 0x7FFFFFFF);
        }
    }
    else
    {
        /* Handle events. */
        status = gckEVENT_Notify(event, 0);
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
    OUT gctSIZE_T * Alignment,
    OUT gctSIZE_T * ReservedHead,
    OUT gctSIZE_T * ReservedTail
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
    gctUINT32 baseAddress;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x", Hardware, Logical);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

    /* Convert the logical address into an hardware address. */
    gcmkONERROR(
        gckHARDWARE_ConvertLogical(Hardware, Logical, &address));

    /* Also get the base address - we need a real physical address. */
    gcmkONERROR(
        gckOS_GetBaseAddress(Hardware->os, &baseAddress));

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Setting page table to 0x%08X",
                   address + baseAddress);

    /* Write the AQMemoryFePageTable register. */
    gcmkONERROR(
        gckOS_WriteRegister(Hardware->os,
                            0x00400,
                            address + baseAddress));

    /* Write the AQMemoryRaPageTable register. */
    gcmkONERROR(
        gckOS_WriteRegister(Hardware->os,
                            0x00410,
                            address + baseAddress));

    /* Write the AQMemoryTxPageTable register. */
    gcmkONERROR(
        gckOS_WriteRegister(Hardware->os,
                            0x00404,
                            address + baseAddress));

    /* Write the AQMemoryPePageTable register. */
    gcmkONERROR(
        gckOS_WriteRegister(Hardware->os,
                            0x00408,
                            address + baseAddress));

    /* Write the AQMemoryPezPageTable register. */
    gcmkONERROR(
        gckOS_WriteRegister(Hardware->os,
                            0x0040C,
                            address + baseAddress));

    /* Return the status. */
    gcmkFOOTER_NO();
    return status;

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
    gctUINT32 flush;
    gctUINT32_PTR buffer;
    gctSIZE_T bufferSize;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Flush the memory controller. */
    flush = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) (0x1&((gctUINT32)((((1?0:0)-(0?0:0)+1)==32)?~0:(~(~0<<((1?0:0)-(0?0:0)+1)))))))<<(0?0:0)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) (0x1&((gctUINT32)((((1?1:1)-(0?1:1)+1)==32)?~0:(~(~0<<((1?1:1)-(0?1:1)+1)))))))<<(0?1:1)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2))) | (((gctUINT32) (0x1&((gctUINT32)((((1?2:2)-(0?2:2)+1)==32)?~0:(~(~0<<((1?2:2)-(0?2:2)+1)))))))<<(0?2:2)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3))) | (((gctUINT32) (0x1&((gctUINT32)((((1?3:3)-(0?3:3)+1)==32)?~0:(~(~0<<((1?3:3)-(0?3:3)+1)))))))<<(0?3:3)))
          | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4))) | (((gctUINT32) (0x1&((gctUINT32)((((1?4:4)-(0?4:4)+1)==32)?~0:(~(~0<<((1?4:4)-(0?4:4)+1)))))))<<(0?4:4)));

    gcmkONERROR(
        gckCOMMAND_Reserve(Hardware->kernel->command,
                           8,
                           (gctPOINTER *) &buffer,
                           &bufferSize));

    buffer[0] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
              | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E04) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
              | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

    buffer[1] = flush;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "0x%x: FLUSH MMU", buffer);

    gcmkONERROR(
        gckCOMMAND_Execute(Hardware->kernel->command, 8));

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
             | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 30:0) - (0 ? 30:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 30:0) - (0 ? 30:0) + 1))))))) << (0 ? 30:0))) | (((gctUINT32) ((gctUINT32) (Offset|(Index<<12)) & ((gctUINT32) ((((1 ? 30:0) - (0 ? 30:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 30:0) - (0 ? 30:0) + 1))))))) << (0 ? 30:0)));

    /* Success. */
    gcmkFOOTER_ARG("*Address=%08x", *Address);
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

    gcmkHEADER_ARG("Hardware=0x%x Wait=%d", Hardware, Wait);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Data != gcvNULL);


    /* If we have to wait, try 100 polls per millisecond. */
    pollCount = Wait ? 100 : 1;

    /* At most, try for 1 second. */
    // dkm: 1000超时太长了，改为200
    for (retry = 0; retry < 200; ++retry)
    {
        /* If we have to wait, try 100 polls per millisecond. */
        for (poll = pollCount; poll > 0; --poll)
        {
            /* Read register. */
            gcmkONERROR(
                gckOS_ReadRegister(Hardware->os, 0x00004, &idle));

            /* See if we have to wait for FE idle. */
                if (( ((((gctUINT32) (idle)) >> (0 ? 0:0)) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1)))))) ))
            {
                    /* FE is idle. */
                break;
            }
        }

        /* Check if we need to wait for FE and FE is busy. */
        if (Wait && !( ((((gctUINT32) (idle)) >> (0 ? 0:0)) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1)))))) ))
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

    /* Success. */
    gcmkFOOTER_ARG("*Data=%08x", *Data);
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
    IN OUT gctSIZE_T * Bytes
    )
{
    gctUINT32 pipe;
    gctUINT32 flush = 0;
    gctUINT32_PTR logical = (gctUINT32_PTR) Logical;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Flush=%x Logical=0x%x *Bytes=%lu",
                   Hardware, Flush, Logical, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Get current pipe. */
    pipe = Hardware->kernel->command->pipeSelect;

    /* Flush 3D color cache. */
    if ((Flush & gcvFLUSH_COLOR) && (pipe == 0x0))
    {
        flush |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) (0x1&((gctUINT32)((((1?1:1)-(0?1:1)+1)==32)?~0:(~(~0<<((1?1:1)-(0?1:1)+1)))))))<<(0?1:1)));
    }

    /* Flush 3D depth cache. */
    if ((Flush & gcvFLUSH_DEPTH) && (pipe == 0x0))
    {
        flush |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) (0x1&((gctUINT32)((((1?0:0)-(0?0:0)+1)==32)?~0:(~(~0<<((1?0:0)-(0?0:0)+1)))))))<<(0?0:0)));
    }

    /* Flush 3D texture cache. */
    if ((Flush & gcvFLUSH_TEXTURE) && (pipe == 0x0))
    {
        flush |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2))) | (((gctUINT32) (0x1&((gctUINT32)((((1?2:2)-(0?2:2)+1)==32)?~0:(~(~0<<((1?2:2)-(0?2:2)+1)))))))<<(0?2:2)));
    }

    /* Flush 2D cache. */
    if ((Flush & gcvFLUSH_2D) && (pipe == 0x1))
    {
        flush |= ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3))) | (((gctUINT32) (0x1&((gctUINT32)((((1?3:3)-(0?3:3)+1)==32)?~0:(~(~0<<((1?3:3)-(0?3:3)+1)))))))<<(0?3:3)));
    }

    /* See if there is a valid flush. */
    if (flush == 0)
    {
        if (Bytes != gcvNULL)
        {
            /* No bytes required. */
            *Bytes = 0;
        }
    }

    else
    {
        /* Copy to command queue. */
        if (Logical != gcvNULL)
        {
            if (*Bytes < 8)
            {
                /* Command queue too small. */
                gcmkONERROR(gcvSTATUS_BUFFER_TOO_SMALL);
            }

            /* Append LOAD_STATE to AQFlush. */
            logical[0] = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                       | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E03) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
                       | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

            logical[1] = flush;

            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                           "0x%x: FLUSH %x", logical, flush);
        }

        if (Bytes != gcvNULL)
        {
            /* 8 bytes required. */
            *Bytes = 8;
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
    gctUINT32 debug;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Enable=%d Compression=%d",
                   Hardware, Enable, Compression);

    /* Only process if fast clear is available. */
    if (( ((((gctUINT32) (Hardware->chipFeatures)) >> (0 ? 0:0)) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1)))))) ))
    {
        if (Enable == -1)
        {
            /* Determine automatic value for fast clear. */
            Enable = (Hardware->chipModel != gcv500)
                   | (Hardware->chipRevision >= 3);
        }

        if (Compression == -1)
        {
            /* Determine automatic value for compression. */
            Compression = Enable
                        & ( ((((gctUINT32) (Hardware->chipFeatures)) >> (0 ? 5:5)) & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1)))))) );
        }

        /* Read AQMemoryDebug register. */
        gcmkONERROR(
            gckOS_ReadRegister(Hardware->os, 0x00414, &debug));

        /* Set fast clear bypass. */
        debug = ((((gctUINT32) (debug)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 20:20) - (0 ? 20:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:20) - (0 ? 20:20) + 1))))))) << (0 ? 20:20))) | (((gctUINT32) ((gctUINT32) (Enable==0) & ((gctUINT32) ((((1 ? 20:20) - (0 ? 20:20) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:20) - (0 ? 20:20) + 1))))))) << (0 ? 20:20)));

        /* Set copression bypass. */
        debug = ((((gctUINT32) (debug)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 21:21) - (0 ? 21:21) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:21) - (0 ? 21:21) + 1))))))) << (0 ? 21:21))) | (((gctUINT32) ((gctUINT32) (Compression==0) & ((gctUINT32) ((((1 ? 21:21) - (0 ? 21:21) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 21:21) - (0 ? 21:21) + 1))))))) << (0 ? 21:21)));

        /* Write back AQMemoryDebug register. */
        gcmkONERROR(
            gckOS_WriteRegister(Hardware->os,
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
    gcvPOWER_FLAG_OFF           = 1 << 8,
    gcvPOWER_FLAG_CLOCK_OFF     = 1 << 9,
}
gcePOWER_FLAGS;

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
#if !gcdNO_POWER_MANAGEMENT
    gceSTATUS status;
    gckCOMMAND command = gcvNULL;
    gckOS os;
    gctUINT flag, clock;
    gctPOINTER buffer;
    gctSIZE_T bytes, requested;
    gctBOOL acquired = gcvFALSE;
    gctBOOL reserved = gcvFALSE;
    gctBOOL mutexAcquired = gcvFALSE;
    gctBOOL stall = gcvTRUE;
    gctBOOL broadcast = gcvFALSE;
    gctUINT32 process, thread;
// dkm: gcdENABLE_LONG_IDLE_POWEROFF
#if gcdENABLE_LONG_IDLE_POWEROFF
    gceCHIPPOWERSTATE curState = State;
#endif

    /* State transition flags. */
    static const gctUINT flags[4][4] =
    {
        /* gcvPOWER_ON           */
        {   /* ON                */ 0,
            /* OFF               */ gcvPOWER_FLAG_ACQUIRE |
                                    gcvPOWER_FLAG_STALL   |
                                    gcvPOWER_FLAG_STOP    |
                                    gcvPOWER_FLAG_OFF     |
                                    gcvPOWER_FLAG_CLOCK_OFF,
            /* IDLE              */ gcvPOWER_FLAG_ACQUIRE |
                                    gcvPOWER_FLAG_STALL,
            /* SUSPEND           */ gcvPOWER_FLAG_ACQUIRE |
                                    gcvPOWER_FLAG_STALL   |
                                    gcvPOWER_FLAG_STOP    |
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
            /* OFF               */ gcvPOWER_FLAG_STOP |
                                    gcvPOWER_FLAG_OFF  |
                                    gcvPOWER_FLAG_CLOCK_OFF,
            /* IDLE              */ 0,
            /* SUSPEND           */ gcvPOWER_FLAG_STOP |
                                    gcvPOWER_FLAG_CLOCK_OFF,
        },

        /* gcvPOWER_SUSPEND      */
        // dkm: 这边漏了gcvPOWER_FLAG_INITIALIZE
        {   /* ON                */ gcvPOWER_FLAG_INITIALIZE |
                                    gcvPOWER_FLAG_START   |
                                    gcvPOWER_FLAG_RELEASE |
                                    gcvPOWER_FLAG_DELAY,
            /* OFF               */ gcvPOWER_FLAG_SAVE |
                                    gcvPOWER_FLAG_OFF  |
                                    gcvPOWER_FLAG_CLOCK_OFF,
        // dkm: 这边漏了gcvPOWER_FLAG_INITIALIZE
            /* IDLE              */ gcvPOWER_FLAG_INITIALIZE |
                                    gcvPOWER_FLAG_START |
                                    gcvPOWER_FLAG_DELAY,
            /* SUSPEND           */ 0,
        },
    };

    /* Clocks. */
    static const gctUINT clocks[4] =
    {
        /* gcvPOWER_ON */
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))|
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1)))|
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2))) | (((gctUINT32) ((gctUINT32) (64) & ((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2)))|
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))), 

        /* gcvPOWER_OFF */
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))|
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1)))|
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2)))|
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))), 

        /* gcvPOWER_IDLE */
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))|
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1)))|
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2)))|
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))), 

        /* gcvPOWER_SUSPEND */
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))|
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1)))|
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 8:2) - (0 ? 8:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 8:2) - (0 ? 8:2) + 1))))))) << (0 ? 8:2)))|
        ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))), 
    };

    gcmkHEADER_ARG("Hardware=0x%x State=%d", Hardware, State);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Get the gckOS object pointer. */
    os = Hardware->os;
    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    /* Convert the broadcast power state. */
    switch (State)
    {
    case gcvPOWER_ON_BROADCAST:
        /* Convert to ON and and not we are inside broadcast. */
        State     = gcvPOWER_ON;
        stall     = gcvFALSE;
        broadcast = gcvTRUE;
        break;

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
        /* Convert to OFF and note we are inside broadcast. */
        State     = gcvPOWER_OFF;
        stall     = gcvFALSE;
        broadcast = gcvTRUE;
        break;

    default:
        break;
    }
    
// dkm: gcdENABLE_LONG_IDLE_POWEROFF
#if gcdENABLE_LONG_IDLE_POWEROFF
    if(gcvPOWER_IDLE_BROADCAST==curState) {
        cancel_delayed_work_sync(&poweroff_work);
        schedule_delayed_work(&poweroff_work, 5*HZ);
    } else if(gcvPOWER_OFF_BROADCAST==curState) {
        // NULL
    } else {
        cancel_delayed_work_sync(&poweroff_work);
    }
#endif

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
            // dkm: add 110330
            else if(gcvPOWER_IDLE==State)
            {
                /* Bail out on idle broadcast with other process is setting power. */
                gcmkFOOTER_NO();
                return gcvSTATUS_OK;
            }
            else
            {
                /* Acquire the power mutex. */
                gcmkONERROR(
                    gckOS_AcquireMutex(os, Hardware->powerMutex, gcvINFINITE));
            }
        }
    }
    else
    {
        /* Acquire the power mutex. */
        gcmkONERROR(gckOS_AcquireMutex(os, Hardware->powerMutex, gcvINFINITE));
    }
    
    Hardware->powerProcess = process;
    Hardware->powerThread  = thread;
    mutexAcquired          = gcvTRUE;
    
// dkm: gcdENABLE_AUTO_FREQ
#if (1==gcdENABLE_AUTO_FREQ)
    cal_run_idle(State);
#elif (2==gcdENABLE_AUTO_FREQ)
    get_idle_change(State);
#endif

    /* Grab control flags and clock. */
    flag  = flags[Hardware->chipPowerState][State];
    clock = clocks[State];

    if ((flag == 0) || (Hardware->settingPowerState))
    {
// dkm: gcdENABLE_LONG_IDLE_POWEROFF
#if gcdENABLE_LONG_IDLE_POWEROFF
        if( (gcvPOWER_OFF==Hardware->chipPowerState) && (gcvPOWER_OFF==State) && (gcvFALSE==broadcast) )
        {
            Hardware->broadcast = gcvFALSE;
        }
#endif
        Hardware->powerProcess = Hardware->powerThread = 0x0;

        /* Release the power mutex. */
        gcmkONERROR(gckOS_ReleaseMutex(os, Hardware->powerMutex));

        /* No need to do anything. */
        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

    if (broadcast && !Hardware->broadcast
    &&  (Hardware->chipPowerState == gcvPOWER_OFF)
    )
    {
        Hardware->powerProcess = Hardware->powerThread = 0x0;

        /* Release the power mutex. */
        gcmkONERROR(gckOS_ReleaseMutex(os, Hardware->powerMutex));

        /* No broadcast while GPU is forced power off. */
        gcmkFOOTER_NO();
        return gcvSTATUS_CHIP_NOT_READY;
    }

    Hardware->settingPowerState = gcvTRUE;

    gcmkASSERT(Hardware->kernel          != gcvNULL);
    gcmkASSERT(Hardware->kernel->command != gcvNULL);
    command = Hardware->kernel->command;

    if (flag & gcvPOWER_FLAG_INITIALIZE)
    {
        /* Turn on the power. */
        gcmkONERROR(gckOS_SetGPUPower(Hardware->os, gcvTRUE, gcvTRUE));
	}

    if ((flag & gcvPOWER_FLAG_STALL) && stall)
    {
        gctBOOL idle;
        gctINT32 atomValue;

        /* Check commit atom. */
        gcmkONERROR(
            gckOS_AtomGet(Hardware->os, command->atomCommit, &atomValue));

        if (atomValue > 0)
        {
            /* Commits are pending - abort power management. */
            status = broadcast ? gcvSTATUS_CHIP_NOT_READY
                               : gcvSTATUS_MORE_DATA;
            goto OnError;
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
            /* Get the size of the flush command. */
            gcmkONERROR(gckHARDWARE_Flush(Hardware,
                                          gcvFLUSH_ALL,
                                          gcvNULL,
                                          &requested));

            /* Reserve space in the command queue. */
            gcmkONERROR(
                gckCOMMAND_Reserve(command, requested, &buffer, &bytes));

            reserved = gcvTRUE;

            /* Append a flush. */
            gcmkONERROR(gckHARDWARE_Flush(Hardware,
                                          gcvFLUSH_ALL,
                                          buffer,
                                          &bytes));

            /* Execute the command queue. */
            acquired = gcvFALSE;
            gcmkONERROR(gckCOMMAND_Execute(command, requested));

            /* Wait to finish all commands. */
            gcmkONERROR(gckCOMMAND_Stall(command));
        }
    }

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
        gcmkONERROR(Hardware->stopIsr(Hardware->isrContext));
    }

    /* Write the clock control register. */
    gcmkONERROR(gckOS_WriteRegister(os,
                                    0x00000,
                                    clock));

    /* Done loading the frequency scaler. */
    gcmkONERROR(gckOS_WriteRegister(os,
                                    0x00000,
                                    ((((gctUINT32) (clock)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1))))))) << (0 ? 9:9)))));

    if (flag & gcvPOWER_FLAG_DELAY)
    {
        /* Wait for the specified amount of time to settle coming back from
        ** power-off or suspend state. */
        gcmkONERROR(gckOS_Delay(os, gcdPOWER_CONTROL_DELAY));
    }

    if (flag & gcvPOWER_FLAG_INITIALIZE)
    {
        /* Initialize hardware. */
        gcmkONERROR(
            gckHARDWARE_InitializeHardware(Hardware));

        gcmkONERROR(
            gckHARDWARE_SetFastClear(Hardware,
                                     Hardware->allowFastClear,
                                     Hardware->allowCompression));

        /* Force the command queue to reload the next context. */
        command->currentContext = 0;
    }

    if (flag & (gcvPOWER_FLAG_OFF | gcvPOWER_FLAG_CLOCK_OFF))
    {
        /* Turn off the GPU power. */
        gcmkONERROR(
            gckOS_SetGPUPower(os,
                              (flag & gcvPOWER_FLAG_CLOCK_OFF) ? gcvFALSE
                                                               : gcvTRUE,
                              (flag & gcvPOWER_FLAG_OFF) ? gcvFALSE : gcvTRUE));
    }

    if (flag & gcvPOWER_FLAG_START)
    {
        /* Start the command processor. */
        gcmkONERROR(gckCOMMAND_Start(command));

        /* Start the Isr. */
        gcmkONERROR(Hardware->startIsr(Hardware->isrContext));
    }

    if (flag & gcvPOWER_FLAG_RELEASE)
    {
        /* Release the power management semaphore. */
        gcmkONERROR(gckOS_ReleaseSemaphore(os, command->powerSemaphore));
    }

    /* Save the new power state. */
    Hardware->chipPowerState    = State;
    Hardware->broadcast         = broadcast;
    Hardware->settingPowerState = gcvFALSE;
    Hardware->powerProcess      = Hardware->powerThread = 0x0;

    /* Release the power mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(os, Hardware->powerMutex));

    gpuState = State;

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (reserved)
    {
        /* Release command queue. */
        gcmkVERIFY_OK(gckCOMMAND_Release(command));
    }

    if (acquired)
    {
        /* Release semaphore. */
        gcmkVERIFY_OK(
            gckOS_ReleaseSemaphore(Hardware->os, command->powerSemaphore));
    }

    if (mutexAcquired)
    {
        Hardware->settingPowerState = gcvFALSE;
        Hardware->powerProcess = Hardware->powerThread = 0x0;

        gcmkVERIFY_OK(gckOS_ReleaseMutex(Hardware->os, Hardware->powerMutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
#else
    /* Do nothing */
    return gcvSTATUS_OK;
#endif
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

    gckOS_AcquireMutex(Hardware->os, Hardware->powerMutex, gcvINFINITE);

    /* Return the statue. */
    *State = Hardware->chipPowerState;

    gckOS_ReleaseMutex(Hardware->os, Hardware->powerMutex);

    /* Success. */
    gcmkFOOTER_ARG("*State=%d", *State);
    return gcvSTATUS_OK;
}

gceSTATUS
gckHARDWARE_QueryIdle(
    IN gckHARDWARE Hardware,
    OUT gctBOOL_PTR IsIdle
    )
{
    gceSTATUS status;
    gctUINT32 idle, address;

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
        gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00004, &idle));

        /* Pipe must be idle. */
        if ((( ((((gctUINT32) (idle)) >> (0 ? 1:1)) & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1)))))) )!=1)
        ||  (( ((((gctUINT32) (idle)) >> (0 ? 3:3)) & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1)))))) )!=1)
        ||  (( ((((gctUINT32) (idle)) >> (0 ? 4:4)) & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1)))))) )!=1)
        ||  (( ((((gctUINT32) (idle)) >> (0 ? 5:5)) & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1)))))) )!=1)
        ||  (( ((((gctUINT32) (idle)) >> (0 ? 6:6)) & ((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1)))))) )!=1)
        ||  (( ((((gctUINT32) (idle)) >> (0 ? 7:7)) & ((gctUINT32) ((((1 ? 7:7) - (0 ? 7:7) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 7:7) - (0 ? 7:7) + 1)))))) )!=1)
        ||  (( ((((gctUINT32) (idle)) >> (0 ? 2:2)) & ((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1)))))) )!=1)
        )
        {
            /* Something is busy. */
            *IsIdle = gcvFALSE;
        }

        else
        {
            /* Read the current FE address. */
            gcmkONERROR(gckOS_ReadRegister(Hardware->os,
                                           0x00664,
                                           &address));

            /* Test if address is inside the last WAIT/LINK sequence. */
            if ((address >= Hardware->lastWaitLink)
            &&  (address <= Hardware->lastWaitLink + 16)
            )
            {
                /* FE is in last WAIT/LINK and the pipe is idle. */
                *IsIdle = gcvTRUE;
            }
            else
            {
                /* FE is not in WAIT/LINK yet. */
                *IsIdle = gcvFALSE;
            }
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

/*******************************************************************************
** Handy macros that will help in reading those debug registers.
*/

#define gcmkREAD_DEBUG_REGISTER(control, block, index, data) \
    gcmkONERROR( \
        gckOS_WriteRegister(Hardware->os, \
                            GC_DEBUG_CONTROL##control##_Address, \
                            gcmSETFIELD(0, \
                                        GC_DEBUG_CONTROL##control, \
                                        block, \
                                        index))); \
    gcmkONERROR( \
        gckOS_ReadRegister(Hardware->os, \
                           GC_DEBUG_SIGNALS_##block##_Address, \
                           &profiler->data))

#define gcmkRESET_DEBUG_REGISTER(control, block) \
    gcmkONERROR( \
        gckOS_WriteRegister(Hardware->os, \
                            GC_DEBUG_CONTROL##control##_Address, \
                            gcmSETFIELD(0, \
                                        GC_DEBUG_CONTROL##control, \
                                        block, \
                                        15))); \
    gcmkONERROR( \
        gckOS_WriteRegister(Hardware->os, \
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
            gckOS_ReadRegister(Hardware->os,
                               0x00438,
                               &Profile->cycleCount));

        /* Read pixels rendered by 2D engine. */
        gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00470, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) | (((gctUINT32) ((gctUINT32) (11) & ((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) )));
        gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00454, &profiler->pixelsRendered));

        /* Reset counter. */
        gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00470, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) | (((gctUINT32) ((gctUINT32) (15) & ((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) )));
        gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00470, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) 
)));
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
    OUT gcsPROFILER_COUNTERS * Counters
    )
{
    gceSTATUS status;
    gcsPROFILER_COUNTERS * profiler = Counters;

    gcmkHEADER_ARG("Hardware=0x%x Counters=0x%x", Hardware, Counters);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Read the counters. */
    gcmkONERROR(
        gckOS_ReadRegister(Hardware->os,
                           0x00040,
                           &profiler->gpuTotalRead64BytesPerFrame));
    gcmkONERROR(
        gckOS_ReadRegister(Hardware->os,
                           0x00044,
                           &profiler->gpuTotalWrite64BytesPerFrame));
    gcmkONERROR(
        gckOS_ReadRegister(Hardware->os,
                           0x00438,
                           &profiler->gpuCyclesCounter));

    /* Reset counters. */
    gcmkONERROR(
        gckOS_WriteRegister(Hardware->os, 0x0003C, 1));
    gcmkONERROR(
        gckOS_WriteRegister(Hardware->os, 0x0003C, 0));
    gcmkONERROR(
        gckOS_WriteRegister(Hardware->os, 0x00438, 0));

    /* PE */
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00470, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00454, &profiler->pe_pixel_count_killed_by_color_pipe));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00470, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00454, &profiler->pe_pixel_count_killed_by_depth_pipe));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00470, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00454, &profiler->pe_pixel_count_drawn_by_color_pipe));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00470, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00454, &profiler->pe_pixel_count_drawn_by_depth_pipe));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00470, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) | (((gctUINT32) ((gctUINT32) (15) & ((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) )));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00470, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) 
)));

    /* SH */
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00470, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (7) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x0045C, &profiler->ps_inst_counter));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00470, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (8) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x0045C, &profiler->rendered_pixel_counter));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00470, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (9) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x0045C, &profiler->vs_inst_counter));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00470, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (10) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x0045C, &profiler->rendered_vertice_counter));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00470, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (11) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x0045C, &profiler->vtx_branch_inst_counter));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00470, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (12) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x0045C, &profiler->vtx_texld_inst_counter));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00470, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (13) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x0045C, &profiler->pxl_branch_inst_counter));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00470, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (14) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x0045C, &profiler->pxl_texld_inst_counter));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00470, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (15) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) )));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00470, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) 
)));

    /* PA */
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00460, &profiler->pa_input_vtx_counter));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) | (((gctUINT32) ((gctUINT32) (4) & ((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00460, &profiler->pa_input_prim_counter));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) | (((gctUINT32) ((gctUINT32) (5) & ((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00460, &profiler->pa_output_prim_counter));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) | (((gctUINT32) ((gctUINT32) (6) & ((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00460, &profiler->pa_depth_clipped_counter));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) | (((gctUINT32) ((gctUINT32) (7) & ((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00460, &profiler->pa_trivial_rejected_counter));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) | (((gctUINT32) ((gctUINT32) (8) & ((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00460, &profiler->pa_culled_counter));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) | (((gctUINT32) ((gctUINT32) (15) & ((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) )));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) 
)));

    /* SE */
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 11:8) - (0 ? 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1))))))) << (0 ? 11:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 11:8) - (0 ? 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1))))))) << (0 ? 11:8))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00464, &profiler->se_culled_triangle_count));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 11:8) - (0 ? 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1))))))) << (0 ? 11:8))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 11:8) - (0 ? 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1))))))) << (0 ? 11:8))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00464, &profiler->se_culled_lines_count));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 11:8) - (0 ? 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1))))))) << (0 ? 11:8))) | (((gctUINT32) ((gctUINT32) (15) & ((gctUINT32) ((((1 ? 11:8) - (0 ? 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1))))))) << (0 ? 11:8))) )));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 11:8) - (0 ? 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1))))))) << (0 ? 11:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 11:8) - (0 ? 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1))))))) << (0 ? 11:8))) 
)));

    /* RA */
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00448, &profiler->ra_valid_pixel_count));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00448, &profiler->ra_total_quad_count));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00448, &profiler->ra_valid_quad_count_after_early_z));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00448, &profiler->ra_total_primitive_count));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) | (((gctUINT32) ((gctUINT32) (9) & ((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00448, &profiler->ra_pipe_cache_miss_counter));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) | (((gctUINT32) ((gctUINT32) (10) & ((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00448, &profiler->ra_prefetch_cache_miss_counter));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) | (((gctUINT32) ((gctUINT32) (15) & ((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) )));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 19:16) - (0 ? 19:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:16) - (0 ? 19:16) + 1))))))) << (0 ? 19:16))) 
)));

    /* TX */
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x0044C, &profiler->tx_total_bilinear_requests));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x0044C, &profiler->tx_total_trilinear_requests));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x0044C, &profiler->tx_total_discarded_texture_requests));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x0044C, &profiler->tx_total_texture_requests));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (5) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x0044C, &profiler->tx_mem_read_count));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (6) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x0044C, &profiler->tx_mem_read_in_8B_count));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (7) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x0044C, &profiler->tx_cache_miss_count));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (8) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x0044C, &profiler->tx_cache_hit_texel_count));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (9) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x0044C, &profiler->tx_cache_miss_texel_count));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (15) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) )));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00474, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 27:24) - (0 ? 27:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:24) - (0 ? 27:24) + 1))))))) << (0 ? 27:24))) 
)));

    /* MC */
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00478, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00468, &profiler->mc_total_read_req_8B_from_pipeline));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00478, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00468, &profiler->mc_total_read_req_8B_from_IP));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00478, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) | (((gctUINT32) ((gctUINT32) (3) & ((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x00468, &profiler->mc_total_write_req_8B_from_pipeline));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00478, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) | (((gctUINT32) ((gctUINT32) (15) & ((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) )));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00478, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 3:0) - (0 ? 3:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:0) - (0 ? 3:0) + 1))))))) << (0 ? 3:0))) 
)));

    /* HI */
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00478, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 11:8) - (0 ? 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1))))))) << (0 ? 11:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 11:8) - (0 ? 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1))))))) << (0 ? 11:8))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x0046C, &profiler->hi_axi_cycles_read_request_stalled));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00478, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 11:8) - (0 ? 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1))))))) << (0 ? 11:8))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 11:8) - (0 ? 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1))))))) << (0 ? 11:8))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x0046C, &profiler->hi_axi_cycles_write_request_stalled));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00478, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 11:8) - (0 ? 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1))))))) << (0 ? 11:8))) | (((gctUINT32) ((gctUINT32) (2) & ((gctUINT32) ((((1 ? 11:8) - (0 ? 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1))))))) << (0 ? 11:8))) )));
    gcmkONERROR(gckOS_ReadRegister(Hardware->os, 0x0046C, &profiler->hi_axi_cycles_write_data_stalled));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00478, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 11:8) - (0 ? 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1))))))) << (0 ? 11:8))) | (((gctUINT32) ((gctUINT32) (15) & ((gctUINT32) ((((1 ? 11:8) - (0 ? 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1))))))) << (0 ? 11:8))) )));
    gcmkONERROR(gckOS_WriteRegister(Hardware->os, 0x00478, (  ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 11:8) - (0 ? 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1))))))) << (0 ? 11:8))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 11:8) - (0 ? 11:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:8) - (0 ? 11:8) + 1))))))) << (0 ? 11:8))) 
)));

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
gckHARDWARE_Reset(
    IN gckHARDWARE Hardware
    )
{
    gceSTATUS status;
    gctUINT32 control, idle;
    gckCOMMAND command;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkASSERT(Hardware->kernel != gcvNULL);
    command = Hardware->kernel->command;
    gcmkASSERT(command != gcvNULL);

    if (Hardware->chipRevision < 0x4600)
    {
        /* Not supported - we need the isolation bit. */
        gcmkONERROR(gcvSTATUS_NOT_SUPPORTED);
    }

    if (Hardware->chipPowerState == gcvPOWER_ON)
    {
        /* Acquire the power management semaphore. */
        gcmkONERROR(
            gckOS_AcquireSemaphore(Hardware->os, command->powerSemaphore));
        acquired = gcvTRUE;
    }

    if ((Hardware->chipPowerState == gcvPOWER_ON)
    ||  (Hardware->chipPowerState == gcvPOWER_IDLE)
    )
    {
        /* Stop the command processor. */
        gcmkONERROR(
            gckCOMMAND_Stop(command));

        /* Grab the queue mutex. */
        gcmkONERROR(
            gckOS_AcquireMutex(Hardware->os,
                               command->mutexQueue,
                               gcvINFINITE));
    }

    /* Read register. */
    gcmkONERROR(
        gckOS_ReadRegister(Hardware->os,
                           0x00000,
                           &control));

    for (;;)
    {
        /* Isolate the GPU. */
        control = ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ? 19:19))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ? 19:19)));

        gcmkONERROR(
            gckOS_WriteRegister(Hardware->os,
                                0x00000,
                                control));

        /* Set soft reset. */
        gcmkONERROR(
            gckOS_WriteRegister(Hardware->os,
                                0x00000,
                                ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ? 12:12))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ? 12:12)))));

        /* Wait for reset. */
        gcmkONERROR(
            gckOS_Delay(Hardware->os, 1));

        /* Reset soft reset bit. */
        gcmkONERROR(
            gckOS_WriteRegister(Hardware->os,
                                0x00000,
                                ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ? 12:12))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 12:12) - (0 ? 12:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:12) - (0 ? 12:12) + 1))))))) << (0 ? 12:12)))));

        /* Reset GPU isolation. */
        control = ((((gctUINT32) (control)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ? 19:19))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 19:19) - (0 ? 19:19) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 19:19) - (0 ? 19:19) + 1))))))) << (0 ? 19:19)));

        gcmkONERROR(
            gckOS_WriteRegister(Hardware->os,
                                0x00000,
                                control));

        /* Read idle register. */
        gcmkONERROR(
            gckOS_ReadRegister(Hardware->os,
                               0x00004,
                               &idle));

        if (( ((((gctUINT32) (idle)) >> (0 ? 0:0)) & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1)))))) )==0)
        {
            continue;
        }

        /* Read reset register. */
        gcmkONERROR(
            gckOS_ReadRegister(Hardware->os,
                               0x00000,
                               &control));

        if ((( ((((gctUINT32) (control)) >> (0 ? 16:16)) & ((gctUINT32) ((((1 ? 16:16) - (0 ? 16:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 16:16) - (0 ? 16:16) + 1)))))) )==0)
        ||  (( ((((gctUINT32) (control)) >> (0 ? 17:17)) & ((gctUINT32) ((((1 ? 17:17) - (0 ? 17:17) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 17:17) - (0 ? 17:17) + 1)))))) )==0)
        )
        {
            continue;
        }

        /* GPU is idle. */
        break;
    }

    /* Force an OFF to ON power switch. */
    Hardware->chipPowerState = gcvPOWER_OFF;
    gcmkONERROR(
        gckHARDWARE_SetPowerManagementState(Hardware, gcvPOWER_ON));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the power management semaphore. */
        gcmkVERIFY_OK(
            gckOS_ReleaseSemaphore(Hardware->os, command->powerSemaphore));
    }

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
    if (((((gctUINT32) (Hardware->chipMinorFeatures0)) >> (0 ? 22:22) & ((gctUINT32) ((((1 ? 22:22) - (0 ? 22:22) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 22:22) - (0 ? 22:22) + 1)))))) == (0x1 & ((gctUINT32) ((((1 ? 22:22) - (0 ? 22:22) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 22:22) - (0 ? 22:22) + 1))))))))
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
    if (((((gctUINT32) (State)) >> (0 ? 31:27) & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1)))))) == (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27)+1))))))))
    {
        /* Get the state address. */
        switch (( ((((gctUINT32) (State)) >> (0 ? 15:0)) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0)+1))))))))
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

