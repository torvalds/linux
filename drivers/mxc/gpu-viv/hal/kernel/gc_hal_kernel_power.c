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

#define _GC_OBJ_ZONE    gcvZONE_POWER

/******************************************************************************\
************************ Dynamic Voltage Frequency Setting *********************
\******************************************************************************/
#if gcdDVFS
static gctUINT32
_GetLoadHistory(
    IN gckDVFS Dvfs,
    IN gctUINT32 Select,
    IN gctUINT32 Index
)
{
    return Dvfs->loads[Index];
}

static void
_IncreaseScale(
    IN gckDVFS Dvfs,
    IN gctUINT32 Load,
    OUT gctUINT8 *Scale
    )
{
    if (Dvfs->currentScale < 32)
    {
        *Scale = Dvfs->currentScale + 8;
    }
    else
    {
        *Scale = Dvfs->currentScale + 8;
        *Scale = gcmMIN(64, *Scale);
    }
}

static void
_RecordFrequencyHistory(
    gckDVFS Dvfs,
    gctUINT32 Frequency
    )
{
    gctUINT32 i = 0;

    struct _FrequencyHistory *history = Dvfs->frequencyHistory;

    for (i = 0; i < 16; i++)
    {
        if (history->frequency == Frequency)
        {
            break;
        }

        if (history->frequency == 0)
        {
            history->frequency = Frequency;
            break;
        }

        history++;
    }

    if (i < 16)
    {
        history->count++;
    }
}

static gctUINT32
_GetFrequencyHistory(
    gckDVFS Dvfs,
    gctUINT32 Frequency
    )
{
    gctUINT32 i = 0;

    struct _FrequencyHistory * history = Dvfs->frequencyHistory;

    for (i = 0; i < 16; i++)
    {
        if (history->frequency == Frequency)
        {
            break;
        }

        history++;
    }

    if (i < 16)
    {
        return history->count;
    }

    return 0;
}

static void
_Policy(
    IN gckDVFS Dvfs,
    IN gctUINT32 Load,
    OUT gctUINT8 *Scale
    )
{
    gctUINT8 load[4], nextLoad;
    gctUINT8 scale;

    /* Last 4 history. */
    load[0] = (Load & 0xFF);
    load[1] = (Load & 0xFF00) >> 8;
    load[2] = (Load & 0xFF0000) >> 16;
    load[3] = (Load & 0xFF000000) >> 24;

    /* Determine target scale. */
    if (load[0] > 54)
    {
        _IncreaseScale(Dvfs, Load, &scale);
    }
    else
    {
        nextLoad = (load[0] + load[1] + load[2] + load[3])/4;

        scale = Dvfs->currentScale * (nextLoad) / 54;

        scale = gcmMAX(1, scale);
        scale = gcmMIN(64, scale);
    }

    Dvfs->totalConfig++;

    Dvfs->loads[(load[0]-1)/8]++;

    *Scale = scale;


    if (Dvfs->totalConfig % 100 == 0)
    {
        gcmkPRINT("=======================================================");
        gcmkPRINT("GPU Load:       %-8d %-8d %-8d %-8d %-8d %-8d %-8d %-8d",
                                   8, 16, 24, 32, 40, 48, 56, 64);
        gcmkPRINT("                %-8d %-8d %-8d %-8d %-8d %-8d %-8d %-8d",
                  _GetLoadHistory(Dvfs,2, 0),
                  _GetLoadHistory(Dvfs,2, 1),
                  _GetLoadHistory(Dvfs,2, 2),
                  _GetLoadHistory(Dvfs,2, 3),
                  _GetLoadHistory(Dvfs,2, 4),
                  _GetLoadHistory(Dvfs,2, 5),
                  _GetLoadHistory(Dvfs,2, 6),
                  _GetLoadHistory(Dvfs,2, 7)
                  );

        gcmkPRINT("Frequency(MHz)  %-8d %-8d %-8d %-8d %-8d",
                  58, 120, 240, 360, 480);
        gcmkPRINT("                %-8d %-8d %-8d %-8d %-8d",
                  _GetFrequencyHistory(Dvfs, 58),
                  _GetFrequencyHistory(Dvfs,120),
                  _GetFrequencyHistory(Dvfs,240),
                  _GetFrequencyHistory(Dvfs,360),
                  _GetFrequencyHistory(Dvfs,480)
                  );
    }
}

static void
_TimerFunction(
    gctPOINTER Data
    )
{
    gceSTATUS status;
    gckDVFS dvfs = (gckDVFS) Data;
    gckHARDWARE hardware = dvfs->hardware;
    gctUINT32 value;
    gctUINT32 frequency;
    gctUINT8 scale;
    gctUINT32 t1, t2, consumed;

    gckOS_GetTicks(&t1);

    gcmkONERROR(gckHARDWARE_QueryLoad(hardware, &value));

    /* determine target sacle. */
    _Policy(dvfs, value, &scale);

    /* Set frequency and voltage. */
    gcmkONERROR(gckOS_SetGPUFrequency(hardware->os, hardware->core, scale));

    /* Query real frequency. */
    gcmkONERROR(
        gckOS_QueryGPUFrequency(hardware->os,
                                hardware->core,
                                &frequency,
                                &dvfs->currentScale));

    _RecordFrequencyHistory(dvfs, frequency);

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_POWER,
                   "Current frequency = %d",
                   frequency);

    /* Set period. */
    gcmkONERROR(gckHARDWARE_SetDVFSPeroid(hardware, frequency));

OnError:
    /* Determine next querying time. */
    gckOS_GetTicks(&t2);

    consumed = gcmMIN(((long)t2 - (long)t1), 5);

    if (dvfs->stop == gcvFALSE)
    {
        gcmkVERIFY_OK(gckOS_StartTimer(hardware->os,
                                       dvfs->timer,
                                       dvfs->pollingTime - consumed));
    }

    return;
}

gceSTATUS
gckDVFS_Construct(
    IN gckHARDWARE Hardware,
    OUT gckDVFS * Dvfs
    )
{
    gceSTATUS status;
    gctPOINTER pointer;
    gckDVFS dvfs = gcvNULL;
    gckOS os = Hardware->os;

    gcmkHEADER_ARG("Hardware=0x%X", Hardware);

    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Dvfs != gcvNULL);

    /* Allocate a gckDVFS manager. */
    gcmkONERROR(gckOS_Allocate(os, gcmSIZEOF(struct _gckDVFS), &pointer));

    gckOS_ZeroMemory(pointer, gcmSIZEOF(struct _gckDVFS));

    dvfs = pointer;

    /* Initialization. */
    dvfs->hardware = Hardware;
    dvfs->pollingTime = gcdDVFS_POLLING_TIME;
    dvfs->os = Hardware->os;
    dvfs->currentScale = 64;

    /* Create a polling timer. */
    gcmkONERROR(gckOS_CreateTimer(os, _TimerFunction, pointer, &dvfs->timer));

    /* Initialize frequency and voltage adjustment helper. */
    gcmkONERROR(gckOS_PrepareGPUFrequency(os, Hardware->core));

    /* Return result. */
    *Dvfs = dvfs;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Roll back. */
    if (dvfs)
    {
        if (dvfs->timer)
        {
            gcmkVERIFY_OK(gckOS_DestroyTimer(os, dvfs->timer));
        }

        gcmkOS_SAFE_FREE(os, dvfs);
    }

    gcmkFOOTER();
    return status;
}

gceSTATUS
gckDVFS_Destroy(
    IN gckDVFS Dvfs
    )
{
    gcmkHEADER_ARG("Dvfs=0x%X", Dvfs);
    gcmkVERIFY_ARGUMENT(Dvfs != gcvNULL);

    /* Deinitialize helper fuunction. */
    gcmkVERIFY_OK(gckOS_FinishGPUFrequency(Dvfs->os, Dvfs->hardware->core));

    /* DestroyTimer. */
    gcmkVERIFY_OK(gckOS_DestroyTimer(Dvfs->os, Dvfs->timer));

    gcmkOS_SAFE_FREE(Dvfs->os, Dvfs);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

gceSTATUS
gckDVFS_Start(
    IN gckDVFS Dvfs
    )
{
    gcmkHEADER_ARG("Dvfs=0x%X", Dvfs);
    gcmkVERIFY_ARGUMENT(Dvfs != gcvNULL);

    gckHARDWARE_InitDVFS(Dvfs->hardware);

    Dvfs->stop = gcvFALSE;

    gckOS_StartTimer(Dvfs->os, Dvfs->timer, Dvfs->pollingTime);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

gceSTATUS
gckDVFS_Stop(
    IN gckDVFS Dvfs
    )
{
    gcmkHEADER_ARG("Dvfs=0x%X", Dvfs);
    gcmkVERIFY_ARGUMENT(Dvfs != gcvNULL);

    Dvfs->stop = gcvTRUE;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}
#endif
