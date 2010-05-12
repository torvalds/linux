/*
 * Copyright (c) 2007-2009 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/** 
 * @file
 * @brief <b>nVIDIA Driver Development Kit: 
 *           Power Resource manager </b>
 *
 * @b Description: NvRM DFS parameters. 
 * 
 */

#ifndef INCLUDED_AP15RM_POWER_DFS_H
#define INCLUDED_AP15RM_POWER_DFS_H

#include "nvrm_power.h"

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */

// Min KHz for CPU and AVP with regards to JTAG support - 1MHz * 8  = 8MHz
// TODO: any other limitations on min KHz?
// TODO: adjust boost parameters based on testing

/**
 * Default DFS algorithm parameters for CPU domain
 */
#define NVRM_DFS_PARAM_CPU_AP15 \
    NvRmFreqMaximum, /* Maximum domain frequency set to h/w limit */ \
    10000,  /* Minimum domain frequency 10 MHz */ \
    1000,   /* Frequency change upper band 1 MHz */ \
    1000,   /* Frequency change lower band 1 MHz */ \
    {          /* RT starvation control parameters */ \
        32000, /* Fixed frequency boost increase 32 MHz */ \
        255,   /* Proportional frequency boost increase 255/256 ~ 100% */ \
        128,   /* Proportional frequency boost decrease 128/256 ~ 50% */  \
    },\
    {          /* NRT starvation control parameters */ \
        4000,  /* Fixed frequency boost increase 4 MHz */ \
        255,   /* Proportional frequency boost increase 255/256 ~ 100% */ \
        128,   /* Proportional frequency boost decrease 128/256 ~ 50% */  \
    },\
    3,      /* Relative adjustement of average freqiency 1/2^3 ~ 12% */ \
    1,      /* Number of smaple intervals with NRT to trigger boost = 2 */ \
    1       /* NRT idle cycles threshold = 1 */ 

/**
 *  Default DFS algorithm parameters for AVP domain
 */
#define NVRM_DFS_PARAM_AVP_AP15 \
    NvRmFreqMaximum, /* Maximum domain frequency set to h/w limit */ \
    24000,  /* Minimum domain frequency 24 MHz */ \
    1000,   /* Frequency change upper band 1 MHz */ \
    1000,   /* Frequency change lower band 1 MHz */ \
    {          /* RT starvation control parameters */ \
        8000,  /* Fixed frequency boost increase 8 MHz */ \
        255,   /* Proportional frequency boost increase 255/256 ~ 100% */ \
        128,   /* Proportional frequency boost decrease 128/256 ~ 50% */  \
    },\
    {          /* NRT starvation control parameters */ \
        1000,  /* Fixed frequency NRT boost increase 1 MHz */ \
        255,   /* Proportional frequency boost increase 255/256 ~ 100% */ \
        128,   /* Proportional frequency boost decrease 128/256 ~ 50% */  \
    },\
    3,      /* Relative adjustement of average freqiency 1/2^3 ~ 12% */ \
    2,      /* Number of smaple intervals with NRT to trigger boost = 3 */ \
    1       /* NRT idle cycles threshold = 1 */ 

/**
 * Default DFS algorithm parameters for System clock domain
 */
#define NVRM_DFS_PARAM_SYSTEM_AP15 \
    NvRmFreqMaximum, /* Maximum domain frequency set to h/w limit */ \
    24000,  /* Minimum domain frequency 24 MHz */ \
    1000,   /* Frequency change upper band 1 MHz */ \
    1000,   /* Frequency change lower band 1 MHz */ \
    {          /* RT starvation control parameters */ \
        8000,  /* Fixed frequency boost increase 8 MHz */ \
        255,   /* Proportional frequency boost increase 255/256 ~ 100% */ \
        128,   /* Proportional frequency boost decrease 128/256 ~ 50% */  \
    },\
    {          /* NRT starvation control parameters */ \
        1000,  /* Fixed frequency NRT boost increase 1 MHz */ \
        255,   /* Proportional frequency boost increase 255/256 ~ 100% */ \
        32,    /* Proportional frequency boost decrease 32/256 ~ 12% */  \
    },\
    5,      /* Relative adjustement of average freqiency 1/2^5 ~ 3% */ \
    2,      /* Number of smaple intervals with NRT to trigger boost = 3 */ \
    1       /* NRT idle cycles threshold = 1 */ 

/**
 * Default DFS algorithm parameters for AHB clock domain
 */
#define NVRM_DFS_PARAM_AHB_AP15 \
    NvRmFreqMaximum, /* Maximum domain frequency set to h/w limit */ \
    24000,  /* Minimum domain frequency 24 MHz */ \
    1000,   /* Frequency change upper band 1 MHz */ \
    1000,   /* Frequency change lower band 1 MHz */ \
    {          /* RT starvation control parameters */ \
        8000,  /* Fixed frequency boost increase 8 MHz */ \
        255,   /* Proportional frequency boost increase 255/256 ~ 100% */ \
        128,   /* Proportional frequency boost decrease 128/256 ~ 50% */  \
    },\
    {          /* NRT starvation control parameters */ \
        1000,  /* Fixed frequency NRT boost increase 1 MHz */ \
        255,   /* Proportional frequency boost increase 255/256 ~ 100% */ \
        32,    /* Proportional frequency boost decrease 32/256 ~ 12% */  \
    },\
    0,      /* Relative adjustement of average freqiency 1/2^0 ~ 100% */ \
    0,      /* Number of smaple intervals with NRT to trigger boost = 1 */ \
    1       /* NRT idle cycles threshold = 1 */ 

/**
 * Default DFS algorithm parameters for APB clock domain
 */
#define NVRM_DFS_PARAM_APB_AP15 \
    NvRmFreqMaximum, /* Maximum domain frequency set to h/w limit */ \
    15000,  /* Minimum domain frequency 15 MHz */ \
    1000,   /* Frequency change upper band 1 MHz */ \
    1000,   /* Frequency change lower band 1 MHz */ \
    {          /* RT starvation control parameters */ \
        8000,  /* Fixed frequency boost increase 8 MHz */ \
        255,   /* Proportional frequency boost increase 255/256 ~ 100% */ \
        128,   /* Proportional frequency boost decrease 128/256 ~ 50% */  \
    },\
    {          /* NRT starvation control parameters */ \
        1000,  /* Fixed frequency NRT boost increase 1 MHz */ \
        255,   /* Proportional frequency boost increase 255/256 ~ 100% */ \
        32,    /* Proportional frequency boost decrease 32/256 ~ 12% */  \
    },\
    0,      /* Relative adjustement of average freqiency 1/2^0 ~ 100% */ \
    0,      /* Number of smaple intervals with NRT to trigger boost = 1 */ \
    1       /* NRT idle cycles threshold = 1 */ 

/**
 * Default DFS algorithm parameters for Video-pipe clock domain
 */
#define NVRM_DFS_PARAM_VPIPE_AP15 \
    NvRmFreqMaximum, /* Maximum domain frequency set to h/w limit */ \
    24000,  /* Minimum domain frequency 24 MHz */ \
    1000,   /* Frequency change upper band 1 MHz */ \
    1000,   /* Frequency change lower band 1 MHz */ \
    {          /* RT starvation control parameters */ \
        16000, /* Fixed frequency RT boost increase 16 MHz */ \
        255,   /* Proportional frequency boost increase 255/256 ~ 100% */ \
        128,   /* Proportional frequency boost decrease 128/256 ~ 50% */  \
    },\
    {          /* NRT starvation control parameters */ \
        1000,  /* Fixed frequency NRT boost increase 1 MHz */ \
        255,   /* Proportional frequency boost increase 255/256 ~ 100% */ \
        128,   /* Proportional frequency boost decrease 128/256 ~ 50% */  \
    },\
    5,      /* Relative adjustement of average freqiency 1/2^5 ~ 3% */ \
    3,      /* Number of smaple intervals with NRT to trigger boost = 4 */ \
    1       /* NRT idle cycles threshold = 1 */ 

/**
 * Default DFS algorithm parameters for EMC clock domain
 */
#define NVRM_DFS_PARAM_EMC_AP15 \
    NvRmFreqMaximum, /* Maximum domain frequency set to h/w limit */ \
    16000,  /* Minimum domain frequency 16 MHz */ \
    1000,   /* Frequency change upper band 1 MHz */ \
    1000,   /* Frequency change lower band 1 MHz */ \
    {          /* RT starvation control parameters */ \
        16000, /* Fixed frequency RT boost increase 16 MHz */ \
        255,   /* Proportional frequency boost increase 255/256 ~ 100% */ \
        128,   /* Proportional frequency boost decrease 128/256 ~ 50% */  \
    },\
    {          /* NRT starvation control parameters */ \
        1000,  /* Fixed frequency NRT boost increase 1 MHz */ \
        255,   /* Proportional frequency boost increase 255/256 ~ 100% */ \
        128,   /* Proportional frequency boost decrease 128/256 ~ 50% */  \
    },\
    0,      /* Relative adjustement of average freqiency 1/2^0 ~ 100% */ \
    0,      /* Number of smaple intervals with NRT to trigger boost = 1 */ \
    1       /* NRT idle cycles threshold = 1 */ 

/// Default low corner for core voltage
#define NVRM_AP15_LOW_CORE_MV (950)

/// Core voltage in suspend
#define NVRM_AP15_SUSPEND_CORE_MV (1000)

/*****************************************************************************/

/**
 * Initializes activity monitors within the DFS module. Only activity
 * monitors are affected. The rest of module's h/w is preserved.
 * 
 * @param pDfs - A pointer to DFS structure.
 * 
 * @return NvSuccess if initialization completed successfully
 * or one of common error codes on failure.
 */
NvError NvRmPrivAp15SystatMonitorsInit(NvRmDfs* pDfs);
NvError NvRmPrivAp15VdeMonitorsInit(NvRmDfs* pDfs);
NvError NvRmPrivAp15EmcMonitorsInit(NvRmDfs* pDfs);

/**
 * Deinitializes activity monitors within the DFS module. Only activity
 * monitors are affected. The rest of module's h/w is preserved.
 * 
 * @param pDfs - A pointer to DFS structure.
 */
void NvRmPrivAp15SystatMonitorsDeinit(NvRmDfs* pDfs);
void NvRmPrivAp15VdeMonitorsDeinit(NvRmDfs* pDfs);
void NvRmPrivAp15EmcMonitorsDeinit(NvRmDfs* pDfs);

/**
 * Starts activity monitors in the DFS module for the next sample interval.
 * 
 * @param pDfs - A pointer to DFS structure.
 * @param pDfsKHz - A pointer to current DFS clock frequencies structure.
 * @param IntervalMs Next sampling interval in ms.
 */
void
NvRmPrivAp15SystatMonitorsStart(
    const NvRmDfs* pDfs,
    const NvRmDfsFrequencies* pDfsKHz,
    const NvU32 IntervalMs);
void
NvRmPrivAp15VdeMonitorsStart(
    const NvRmDfs* pDfs,
    const NvRmDfsFrequencies* pDfsKHz,
    const NvU32 IntervalMs);
void
NvRmPrivAp15EmcMonitorsStart(
    const NvRmDfs* pDfs,
    const NvRmDfsFrequencies* pDfsKHz,
    const NvU32 IntervalMs);

/**
 * Reads idle count from activity monitors in the DFS module. The monitors are
 * stopped.
 * 
 * @param pDfs - A pointer to DFS structure.
 * @param pDfsKHz - A pointer to current DFS clock frequencies structure.
 * @param pIdleData - A pointer to idle cycles structure to be filled in with
 *  data read from the monitor.
 * 
 */
void
NvRmPrivAp15SystatMonitorsRead(
    const NvRmDfs* pDfs,
    const NvRmDfsFrequencies* pDfsKHz,
    NvRmDfsIdleData* pIdleData);
void
NvRmPrivAp15VdeMonitorsRead(
    const NvRmDfs* pDfs,
    const NvRmDfsFrequencies* pDfsKHz,
    NvRmDfsIdleData* pIdleData);
void
NvRmPrivAp15EmcMonitorsRead(
    const NvRmDfs* pDfs,
    const NvRmDfsFrequencies* pDfsKHz,
    NvRmDfsIdleData* pIdleData);

/**
 * Changes RAM timing SVOP settings.
 * 
 * @param hRm The RM device handle.
 * @param SvopSetting New SVOP setting.
 */
void
NvRmPrivAp15SetSvopControls(
    NvRmDeviceHandle hRm,
    NvU32 SvopSetting);

/**
 * Gets uS Timer RM virtual address,
 * 
 * @param hRm The RM device handle.
 * 
 * @return uS Timer RM virtual address mapped by RM
 */
void* NvRmPrivAp15GetTimerUsVirtAddr(NvRmDeviceHandle hRm);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif // INCLUDED_AP15RM_POWER_DFS_H
