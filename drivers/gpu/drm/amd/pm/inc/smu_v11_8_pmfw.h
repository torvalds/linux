/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#ifndef __SMU_V11_8_0_PMFW_H__
#define __SMU_V11_8_0_PMFW_H__

#pragma pack(push, 1)

#define ENABLE_DEBUG_FEATURES

// Feature Control Defines
#define FEATURE_CCLK_CONTROLLER_BIT       0
#define FEATURE_GFXCLK_EFFT_FREQ_BIT      1
#define FEATURE_DATA_CALCULATION_BIT      2
#define FEATURE_THERMAL_BIT               3
#define FEATURE_PLL_POWER_DOWN_BIT        4
#define FEATURE_FCLK_DPM_BIT              5
#define FEATURE_GFX_DPM_BIT               6
#define FEATURE_DS_GFXCLK_BIT             7
#define FEATURE_DS_SOCCLK_BIT             8
#define FEATURE_DS_LCLK_BIT               9
#define FEATURE_CORE_CSTATES_BIT          10
#define FEATURE_G6_SSC_BIT                11 //G6 memory UCLK and UCLK_DIV SS
#define FEATURE_RM_BIT                    12
#define FEATURE_SOC_DPM_BIT               13
#define FEATURE_DS_SMNCLK_BIT             14
#define FEATURE_DS_MP1CLK_BIT             15
#define FEATURE_DS_MP0CLK_BIT             16
#define FEATURE_MGCG_BIT                  17
#define FEATURE_DS_FUSE_SRAM_BIT          18
#define FEATURE_GFX_CKS_BIT               19
#define FEATURE_FP_THROTTLING_BIT         20
#define FEATURE_PROCHOT_BIT               21
#define FEATURE_CPUOFF_BIT                22
#define FEATURE_UMC_THROTTLE_BIT          23
#define FEATURE_DF_THROTTLE_BIT           24
#define FEATURE_DS_MP3CLK_BIT             25
#define FEATURE_DS_SHUBCLK_BIT            26
#define FEATURE_TDC_BIT                   27 //Legacy APM_BIT
#define FEATURE_UMC_CAL_SHARING_BIT       28
#define FEATURE_DFLL_BTC_CALIBRATION_BIT  29
#define FEATURE_EDC_BIT                   30
#define FEATURE_DLDO_BIT                  31
#define FEATURE_MEAS_DRAM_BLACKOUT_BIT    32
#define FEATURE_CC1_BIT                   33
#define FEATURE_PPT_BIT                   34
#define FEATURE_STAPM_BIT                 35
#define FEATURE_CSTATE_BOOST_BIT          36
#define FEATURE_SPARE_37_BIT              37
#define FEATURE_SPARE_38_BIT              38
#define FEATURE_SPARE_39_BIT              39
#define FEATURE_SPARE_40_BIT              40
#define FEATURE_SPARE_41_BIT              41
#define FEATURE_SPARE_42_BIT              42
#define FEATURE_SPARE_43_BIT              43
#define FEATURE_SPARE_44_BIT              44
#define FEATURE_SPARE_45_BIT              45
#define FEATURE_SPARE_46_BIT              46
#define FEATURE_SPARE_47_BIT              47
#define FEATURE_SPARE_48_BIT              48
#define FEATURE_SPARE_49_BIT              49
#define FEATURE_SPARE_50_BIT              50
#define FEATURE_SPARE_51_BIT              51
#define FEATURE_SPARE_52_BIT              52
#define FEATURE_SPARE_53_BIT              53
#define FEATURE_SPARE_54_BIT              54
#define FEATURE_SPARE_55_BIT              55
#define FEATURE_SPARE_56_BIT              56
#define FEATURE_SPARE_57_BIT              57
#define FEATURE_SPARE_58_BIT              58
#define FEATURE_SPARE_59_BIT              59
#define FEATURE_SPARE_60_BIT              60
#define FEATURE_SPARE_61_BIT              61
#define FEATURE_SPARE_62_BIT              62
#define FEATURE_SPARE_63_BIT              63

#define NUM_FEATURES                      64

#define FEATURE_CCLK_CONTROLLER_MASK  (1 << FEATURE_CCLK_CONTROLLER_BIT)
#define FEATURE_DATA_CALCULATION_MASK (1 << FEATURE_DATA_CALCULATION_BIT)
#define FEATURE_THERMAL_MASK          (1 << FEATURE_THERMAL_BIT)
#define FEATURE_PLL_POWER_DOWN_MASK   (1 << FEATURE_PLL_POWER_DOWN_BIT)
#define FEATURE_FCLK_DPM_MASK         (1 << FEATURE_FCLK_DPM_BIT)
#define FEATURE_GFX_DPM_MASK          (1 << FEATURE_GFX_DPM_BIT)
#define FEATURE_DS_GFXCLK_MASK        (1 << FEATURE_DS_GFXCLK_BIT)
#define FEATURE_DS_SOCCLK_MASK        (1 << FEATURE_DS_SOCCLK_BIT)
#define FEATURE_DS_LCLK_MASK          (1 << FEATURE_DS_LCLK_BIT)
#define FEATURE_RM_MASK               (1 << FEATURE_RM_BIT)
#define FEATURE_DS_SMNCLK_MASK        (1 << FEATURE_DS_SMNCLK_BIT)
#define FEATURE_DS_MP1CLK_MASK        (1 << FEATURE_DS_MP1CLK_BIT)
#define FEATURE_DS_MP0CLK_MASK        (1 << FEATURE_DS_MP0CLK_BIT)
#define FEATURE_MGCG_MASK             (1 << FEATURE_MGCG_BIT)
#define FEATURE_DS_FUSE_SRAM_MASK     (1 << FEATURE_DS_FUSE_SRAM_BIT)
#define FEATURE_PROCHOT_MASK          (1 << FEATURE_PROCHOT_BIT)
#define FEATURE_CPUOFF_MASK           (1 << FEATURE_CPUOFF_BIT)
#define FEATURE_GFX_CKS_MASK          (1 << FEATURE_GFX_CKS_BIT)
#define FEATURE_UMC_THROTTLE_MASK     (1 << FEATURE_UMC_THROTTLE_BIT)
#define FEATURE_DF_THROTTLE_MASK      (1 << FEATURE_DF_THROTTLE_BIT)
#define FEATURE_SOC_DPM_MASK          (1 << FEATURE_SOC_DPM_BIT)

typedef struct {
	// MP1_EXT_SCRATCH0
	uint32_t SPARE1            : 4;
	uint32_t SPARE2            : 4;
	uint32_t SPARE3            : 4;
	uint32_t CurrLevel_LCLK    : 4;
	uint32_t CurrLevel_MP0CLK  : 4;
	uint32_t CurrLevel_FCLK    : 4;
	uint32_t CurrLevel_SOCCLK  : 4;
	uint32_t CurrLevel_DCEFCLK : 4;
	// MP1_EXT_SCRATCH1
	uint32_t SPARE4            : 4;
	uint32_t SPARE5            : 4;
	uint32_t SPARE6            : 4;
	uint32_t TargLevel_LCLK    : 4;
	uint32_t TargLevel_MP0CLK  : 4;
	uint32_t TargLevel_FCLK    : 4;
	uint32_t TargLevel_SOCCLK  : 4;
	uint32_t TargLevel_DCEFCLK : 4;
	// MP1_EXT_SCRATCH2
	uint32_t CurrLevel_SHUBCLK  : 4;
	uint32_t TargLevel_SHUBCLK  : 4;
	uint32_t Reserved          : 24;
	// MP1_EXT_SCRATCH3-4
	uint32_t Reserved2[2];
	// MP1_EXT_SCRATCH5
	uint32_t FeatureStatus[NUM_FEATURES / 32];
} FwStatus_t;

#pragma pack(pop)

#endif
