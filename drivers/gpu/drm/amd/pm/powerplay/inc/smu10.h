/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#ifndef SMU10_H
#define SMU10_H

#pragma pack(push, 1)

#define ENABLE_DEBUG_FEATURES

/* Feature Control Defines */
#define FEATURE_CCLK_CONTROLLER_BIT   0
#define FEATURE_FAN_CONTROLLER_BIT    1
#define FEATURE_DATA_CALCULATION_BIT  2
#define FEATURE_PPT_BIT               3
#define FEATURE_TDC_BIT               4
#define FEATURE_THERMAL_BIT           5
#define FEATURE_FIT_BIT               6
#define FEATURE_EDC_BIT               7
#define FEATURE_PLL_POWER_DOWN_BIT    8
#define FEATURE_ULV_BIT               9
#define FEATURE_VDDOFF_BIT            10
#define FEATURE_VCN_DPM_BIT           11
#define FEATURE_ACP_DPM_BIT           12
#define FEATURE_ISP_DPM_BIT           13
#define FEATURE_FCLK_DPM_BIT          14
#define FEATURE_SOCCLK_DPM_BIT        15
#define FEATURE_MP0CLK_DPM_BIT        16
#define FEATURE_LCLK_DPM_BIT          17
#define FEATURE_SHUBCLK_DPM_BIT       18
#define FEATURE_DCEFCLK_DPM_BIT       19
#define FEATURE_GFX_DPM_BIT           20
#define FEATURE_DS_GFXCLK_BIT         21
#define FEATURE_DS_SOCCLK_BIT         22
#define FEATURE_DS_LCLK_BIT           23
#define FEATURE_DS_DCEFCLK_BIT        24
#define FEATURE_DS_SHUBCLK_BIT        25
#define FEATURE_RM_BIT                26
#define FEATURE_S0i2_BIT              27
#define FEATURE_WHISPER_MODE_BIT      28
#define FEATURE_DS_FCLK_BIT           29
#define FEATURE_DS_SMNCLK_BIT         30
#define FEATURE_DS_MP1CLK_BIT         31
#define FEATURE_DS_MP0CLK_BIT         32
#define FEATURE_MGCG_BIT              33
#define FEATURE_DS_FUSE_SRAM_BIT      34
#define FEATURE_GFX_CKS               35
#define FEATURE_PSI0_BIT              36
#define FEATURE_PROCHOT_BIT           37
#define FEATURE_CPUOFF_BIT            38
#define FEATURE_STAPM_BIT             39
#define FEATURE_CORE_CSTATES_BIT      40
#define FEATURE_SPARE_41_BIT          41
#define FEATURE_SPARE_42_BIT          42
#define FEATURE_SPARE_43_BIT          43
#define FEATURE_SPARE_44_BIT          44
#define FEATURE_SPARE_45_BIT          45
#define FEATURE_SPARE_46_BIT          46
#define FEATURE_SPARE_47_BIT          47
#define FEATURE_SPARE_48_BIT          48
#define FEATURE_SPARE_49_BIT          49
#define FEATURE_SPARE_50_BIT          50
#define FEATURE_SPARE_51_BIT          51
#define FEATURE_SPARE_52_BIT          52
#define FEATURE_SPARE_53_BIT          53
#define FEATURE_SPARE_54_BIT          54
#define FEATURE_SPARE_55_BIT          55
#define FEATURE_SPARE_56_BIT          56
#define FEATURE_SPARE_57_BIT          57
#define FEATURE_SPARE_58_BIT          58
#define FEATURE_SPARE_59_BIT          59
#define FEATURE_SPARE_60_BIT          60
#define FEATURE_SPARE_61_BIT          61
#define FEATURE_SPARE_62_BIT          62
#define FEATURE_SPARE_63_BIT          63

#define NUM_FEATURES                  64

#define FEATURE_CCLK_CONTROLLER_MASK  (1 << FEATURE_CCLK_CONTROLLER_BIT)
#define FEATURE_FAN_CONTROLLER_MASK   (1 << FEATURE_FAN_CONTROLLER_BIT)
#define FEATURE_DATA_CALCULATION_MASK (1 << FEATURE_DATA_CALCULATION_BIT)
#define FEATURE_PPT_MASK              (1 << FEATURE_PPT_BIT)
#define FEATURE_TDC_MASK              (1 << FEATURE_TDC_BIT)
#define FEATURE_THERMAL_MASK          (1 << FEATURE_THERMAL_BIT)
#define FEATURE_FIT_MASK              (1 << FEATURE_FIT_BIT)
#define FEATURE_EDC_MASK              (1 << FEATURE_EDC_BIT)
#define FEATURE_PLL_POWER_DOWN_MASK   (1 << FEATURE_PLL_POWER_DOWN_BIT)
#define FEATURE_ULV_MASK              (1 << FEATURE_ULV_BIT)
#define FEATURE_VDDOFF_MASK           (1 << FEATURE_VDDOFF_BIT)
#define FEATURE_VCN_DPM_MASK          (1 << FEATURE_VCN_DPM_BIT)
#define FEATURE_ACP_DPM_MASK          (1 << FEATURE_ACP_DPM_BIT)
#define FEATURE_ISP_DPM_MASK          (1 << FEATURE_ISP_DPM_BIT)
#define FEATURE_FCLK_DPM_MASK         (1 << FEATURE_FCLK_DPM_BIT)
#define FEATURE_SOCCLK_DPM_MASK       (1 << FEATURE_SOCCLK_DPM_BIT)
#define FEATURE_MP0CLK_DPM_MASK       (1 << FEATURE_MP0CLK_DPM_BIT)
#define FEATURE_LCLK_DPM_MASK         (1 << FEATURE_LCLK_DPM_BIT)
#define FEATURE_SHUBCLK_DPM_MASK      (1 << FEATURE_SHUBCLK_DPM_BIT)
#define FEATURE_DCEFCLK_DPM_MASK      (1 << FEATURE_DCEFCLK_DPM_BIT)
#define FEATURE_GFX_DPM_MASK          (1 << FEATURE_GFX_DPM_BIT)
#define FEATURE_DS_GFXCLK_MASK        (1 << FEATURE_DS_GFXCLK_BIT)
#define FEATURE_DS_SOCCLK_MASK        (1 << FEATURE_DS_SOCCLK_BIT)
#define FEATURE_DS_LCLK_MASK          (1 << FEATURE_DS_LCLK_BIT)
#define FEATURE_DS_DCEFCLK_MASK       (1 << FEATURE_DS_DCEFCLK_BIT)
#define FEATURE_DS_SHUBCLK_MASK       (1 << FEATURE_DS_SHUBCLK_BIT)
#define FEATURE_RM_MASK               (1 << FEATURE_RM_BIT)
#define FEATURE_DS_FCLK_MASK          (1 << FEATURE_DS_FCLK_BIT)
#define FEATURE_DS_SMNCLK_MASK        (1 << FEATURE_DS_SMNCLK_BIT)
#define FEATURE_DS_MP1CLK_MASK        (1 << FEATURE_DS_MP1CLK_BIT)
#define FEATURE_DS_MP0CLK_MASK        (1 << FEATURE_DS_MP0CLK_BIT)
#define FEATURE_MGCG_MASK             (1 << FEATURE_MGCG_BIT)
#define FEATURE_DS_FUSE_SRAM_MASK     (1 << FEATURE_DS_FUSE_SRAM_BIT)
#define FEATURE_PSI0_MASK             (1 << FEATURE_PSI0_BIT)
#define FEATURE_STAPM_MASK            (1 << FEATURE_STAPM_BIT)
#define FEATURE_PROCHOT_MASK          (1 << FEATURE_PROCHOT_BIT)
#define FEATURE_CPUOFF_MASK           (1 << FEATURE_CPUOFF_BIT)
#define FEATURE_CORE_CSTATES_MASK     (1 << FEATURE_CORE_CSTATES_BIT)

/* Workload bits */
#define WORKLOAD_PPLIB_FULL_SCREEN_3D_BIT 0
#define WORKLOAD_PPLIB_VIDEO_BIT          2
#define WORKLOAD_PPLIB_VR_BIT             3
#define WORKLOAD_PPLIB_COMPUTE_BIT        4
#define WORKLOAD_PPLIB_CUSTOM_BIT         5
#define WORKLOAD_PPLIB_COUNT              6

typedef struct {
	/* MP1_EXT_SCRATCH0 */
	uint32_t CurrLevel_ACP     : 4;
	uint32_t CurrLevel_ISP     : 4;
	uint32_t CurrLevel_VCN     : 4;
	uint32_t CurrLevel_LCLK    : 4;
	uint32_t CurrLevel_MP0CLK  : 4;
	uint32_t CurrLevel_FCLK    : 4;
	uint32_t CurrLevel_SOCCLK  : 4;
	uint32_t CurrLevel_DCEFCLK : 4;
	/* MP1_EXT_SCRATCH1 */
	uint32_t TargLevel_ACP     : 4;
	uint32_t TargLevel_ISP     : 4;
	uint32_t TargLevel_VCN     : 4;
	uint32_t TargLevel_LCLK    : 4;
	uint32_t TargLevel_MP0CLK  : 4;
	uint32_t TargLevel_FCLK    : 4;
	uint32_t TargLevel_SOCCLK  : 4;
	uint32_t TargLevel_DCEFCLK : 4;
	/* MP1_EXT_SCRATCH2 */
	uint32_t CurrLevel_SHUBCLK  : 4;
	uint32_t TargLevel_SHUBCLK  : 4;
	uint32_t InUlv              : 1;
	uint32_t InS0i2             : 1;
	uint32_t InWhisperMode      : 1;
	uint32_t Reserved           : 21;
	/* MP1_EXT_SCRATCH3-4 */
	uint32_t Reserved2[2];
	/* MP1_EXT_SCRATCH5 */
	uint32_t FeatureStatus[NUM_FEATURES / 32];
} FwStatus_t;

#define TABLE_BIOS_IF            0 /* Called by BIOS */
#define TABLE_WATERMARKS         1 /* Called by Driver */
#define TABLE_CUSTOM_DPM         2 /* Called by Driver */
#define TABLE_PMSTATUSLOG        3 /* Called by Tools for Agm logging */
#define TABLE_DPMCLOCKS          4 /* Called by Driver */
#define TABLE_MOMENTARY_PM       5 /* Called by Tools */
#define TABLE_COUNT              6

#pragma pack(pop)

#endif
