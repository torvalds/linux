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

#ifndef __SMU_V13_0_5_PMFW_H__
#define __SMU_V13_0_5_PMFW_H__

#include "smu13_driver_if_v13_0_5.h"

#pragma pack(push, 1)

#define ENABLE_DEBUG_FEATURES

// Firmware features
// Feature Control Defines
#define FEATURE_DATA_CALCULATION_BIT        0
#define FEATURE_PPT_BIT                     1
#define FEATURE_TDC_BIT                     2
#define FEATURE_THERMAL_BIT                 3
#define FEATURE_FIT_BIT                     4
#define FEATURE_EDC_BIT                     5
#define FEATURE_CSTATE_BOOST_BIT            6
#define FEATURE_PROCHOT_BIT                 7
#define FEATURE_CCLK_DPM_BIT                8
#define FEATURE_FCLK_DPM_BIT                9
#define FEATURE_LCLK_DPM_BIT                10
#define FEATURE_PSI7_BIT                    11
#define FEATURE_DLDO_BIT                    12
#define FEATURE_SOCCLK_DEEP_SLEEP_BIT       13
#define FEATURE_LCLK_DEEP_SLEEP_BIT         14
#define FEATURE_SHUBCLK_DEEP_SLEEP_BIT      15
#define FEATURE_DVO_BIT                     16
#define FEATURE_CC6_BIT                     17
#define FEATURE_PC6_BIT                     18
#define FEATURE_DF_CSTATES_BIT              19
#define FEATURE_CLOCK_GATING_BIT            20
#define FEATURE_FAN_CONTROLLER_BIT          21
#define FEATURE_CPPC_BIT                    22
#define FEATURE_DLDO_DROPOUT_LIMITER_BIT    23
#define FEATURE_CPPC_PREFERRED_CORES_BIT    24
#define FEATURE_GMI_FOLDING_BIT             25
#define FEATURE_GMI_DLWM_BIT                26
#define FEATURE_XGMI_DLWM_BIT               27
#define FEATURE_DF_LIGHT_CSTATE_BIT         28
#define FEATURE_SMNCLK_DEEP_SLEEP_BIT       29
#define FEATURE_PCIE_SPEED_CONTROLLER_BIT   30
#define FEATURE_GFX_DPM_BIT             31
#define FEATURE_DS_GFXCLK_BIT           32
#define FEATURE_PCC_BIT                    33
#define FEATURE_spare0_BIT                  34
#define FEATURE_S0I3_BIT                35
#define FEATURE_VCN_DPM_BIT             36
#define FEATURE_DS_VCN_BIT              37
#define FEATURE_MPDMA_TF_CLK_DEEP_SLEEP_BIT 38
#define FEATURE_MPDMA_PM_CLK_DEEP_SLEEP_BIT 39
#define FEATURE_VDDOFF_BIT              40
#define FEATURE_DCFCLK_DPM_BIT          41
#define FEATURE_DCFCLK_DEEP_SLEEP_BIT       42
#define FEATURE_ATHUB_PG_BIT            43
#define FEATURE_SOCCLK_DPM_BIT          44
#define FEATURE_SHUBCLK_DPM_BIT         45
#define FEATURE_MP0CLK_DPM_BIT          46
#define FEATURE_MP0CLK_DEEP_SLEEP_BIT       47
#define FEATURE_PERCCXPC6_BIT               48
#define FEATURE_GFXOFF_BIT                  49
#define NUM_FEATURES                    50

typedef struct {
  // MP1_EXT_SCRATCH0
  uint32_t CurrLevel_ACP     : 4;
  uint32_t CurrLevel_ISP     : 4;
  uint32_t CurrLevel_VCN     : 4;
  uint32_t CurrLevel_LCLK    : 4;
  uint32_t CurrLevel_MP0CLK  : 4;
  uint32_t CurrLevel_FCLK    : 4;
  uint32_t CurrLevel_SOCCLK  : 4;
  uint32_t CurrLevel_DCFCLK : 4;
  // MP1_EXT_SCRATCH1
  uint32_t TargLevel_ACP     : 4;
  uint32_t TargLevel_ISP     : 4;
  uint32_t TargLevel_VCN     : 4;
  uint32_t TargLevel_LCLK    : 4;
  uint32_t TargLevel_MP0CLK  : 4;
  uint32_t TargLevel_FCLK    : 4;
  uint32_t TargLevel_SOCCLK  : 4;
  uint32_t TargLevel_DCFCLK : 4;
  // MP1_EXT_SCRATCH2
  uint32_t CurrLevel_SHUBCLK  : 4;
  uint32_t TargLevel_SHUBCLK  : 4;
  uint32_t InUlv              : 1;
  uint32_t InS0i2             : 1;
  uint32_t InWhisperMode      : 1;
  uint32_t GfxOn              : 1;
  uint32_t RsmuCalBusyDpmIndex: 8;
  uint32_t DpmHandlerId       : 8;
  uint32_t DpmTimerId         : 4;
  // MP1_EXT_SCRATCH3
  uint32_t ReadWriteSmnRegAddr: 32;
  // MP1_EXT_SCRATCH4
  uint32_t Reserved1;
  // MP1_EXT_SCRATCH5
  uint32_t FeatureStatus[NUM_FEATURES / 32];
} FwStatus_t;

#pragma pack(pop)

#endif
