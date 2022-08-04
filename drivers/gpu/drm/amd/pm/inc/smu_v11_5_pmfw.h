/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __SMU_V11_5_0_PMFW_H__
#define __SMU_V11_5_0_PMFW_H__

#include "smu11_driver_if_vangogh.h"

#pragma pack(push, 1)

#define ENABLE_DEBUG_FEATURES

// Feature Control Defines
#define FEATURE_CCLK_DPM_BIT           0
#define FEATURE_FAN_CONTROLLER_BIT     1
#define FEATURE_DATA_CALCULATION_BIT   2
#define FEATURE_PPT_BIT                3
#define FEATURE_TDC_BIT                4
#define FEATURE_THERMAL_BIT            5
#define FEATURE_FIT_BIT                6
#define FEATURE_EDC_BIT                7
#define FEATURE_PLL_POWER_DOWN_BIT     8
#define FEATURE_ULV_BIT                9
#define FEATURE_VDDOFF_BIT            10
#define FEATURE_VCN_DPM_BIT           11
#define FEATURE_CSTATE_BOOST_BIT      12
#define FEATURE_FCLK_DPM_BIT          13
#define FEATURE_SOCCLK_DPM_BIT        14
#define FEATURE_MP0CLK_DPM_BIT        15
#define FEATURE_LCLK_DPM_BIT          16
#define FEATURE_SHUBCLK_DPM_BIT       17
#define FEATURE_DCFCLK_DPM_BIT        18
#define FEATURE_GFX_DPM_BIT           19
#define FEATURE_DS_GFXCLK_BIT         20
#define FEATURE_DS_SOCCLK_BIT         21
#define FEATURE_DS_LCLK_BIT           22
#define FEATURE_DS_DCFCLK_BIT         23
#define FEATURE_DS_SHUBCLK_BIT        24
#define FEATURE_GFX_TEMP_VMIN_BIT     25
#define FEATURE_S0I2_BIT              26
#define FEATURE_WHISPER_MODE_BIT      27
#define FEATURE_DS_FCLK_BIT           28
#define FEATURE_DS_SMNCLK_BIT         29
#define FEATURE_DS_MP1CLK_BIT         30
#define FEATURE_DS_MP0CLK_BIT         31
#define FEATURE_SMU_LOW_POWER_BIT     32
#define FEATURE_FUSE_PG_BIT           33
#define FEATURE_GFX_DEM_BIT           34
#define FEATURE_PSI_BIT               35
#define FEATURE_PROCHOT_BIT           36
#define FEATURE_CPUOFF_BIT            37
#define FEATURE_STAPM_BIT             38
#define FEATURE_S0I3_BIT              39
#define FEATURE_DF_CSTATES_BIT        40
#define FEATURE_PERF_LIMIT_BIT        41
#define FEATURE_CORE_DLDO_BIT         42
#define FEATURE_RSMU_LOW_POWER_BIT    43
#define FEATURE_SMN_LOW_POWER_BIT     44
#define FEATURE_THM_LOW_POWER_BIT     45
#define FEATURE_SMUIO_LOW_POWER_BIT   46
#define FEATURE_MP1_LOW_POWER_BIT     47
#define FEATURE_DS_VCN_BIT            48
#define FEATURE_CPPC_BIT              49
#define FEATURE_OS_CSTATES_BIT        50
#define FEATURE_ISP_DPM_BIT           51
#define FEATURE_A55_DPM_BIT           52
#define FEATURE_CVIP_DSP_DPM_BIT      53
#define FEATURE_MSMU_LOW_POWER_BIT    54
#define FEATURE_SOC_VOLTAGE_MON_BIT   55
#define FEATURE_ATHUB_PG_BIT          56
#define FEATURE_ECO_DEEPCSTATE_BIT    57
#define FEATURE_CC6_BIT               58
#define FEATURE_GFX_EDC_BIT           59
#define NUM_FEATURES                  60

typedef struct {
  // MP1_EXT_SCRATCH0
  uint32_t DpmHandlerID         : 8;
  uint32_t ActivityMonitorID    : 8;
  uint32_t DpmTimerID           : 8;
  uint32_t DpmHubID             : 4;
  uint32_t DpmHubTask           : 4;
  // MP1_EXT_SCRATCH1
  uint32_t GfxStatus            : 2;
  uint32_t GfxoffStatus         : 8;
  uint32_t CpuOff               : 1;
  uint32_t VddOff               : 1;
  uint32_t InUlv                : 1;
  uint32_t InS0i2               : 2;
  uint32_t InWhisperMode        : 1;
  uint32_t spare1               : 16;
  // MP1_EXT_SCRATCH2
  uint32_t P2JobHandler			: 32;
  // MP1_EXT_SCRATCH3: used for postcodes

  // MP1_EXT_SCRATCH4:6 are used by Kernel
  // MP1_EXT_SCRATCH7: used by HW
} FwStatus_t;


#pragma pack(pop)

#endif
