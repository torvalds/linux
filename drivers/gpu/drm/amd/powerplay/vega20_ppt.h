/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
#ifndef __VEGA20_PPT_H__
#define __VEGA20_PPT_H__

#define VEGA20_UMD_PSTATE_GFXCLK_LEVEL         0x3
#define VEGA20_UMD_PSTATE_SOCCLK_LEVEL         0x3
#define VEGA20_UMD_PSTATE_MCLK_LEVEL           0x2
#define VEGA20_UMD_PSTATE_UVDCLK_LEVEL         0x3
#define VEGA20_UMD_PSTATE_VCEMCLK_LEVEL        0x3

#define MAX_REGULAR_DPM_NUMBER 16
#define MAX_PCIE_CONF 2

#define VOLTAGE_SCALE 4
#define AVFS_CURVE 0
#define OD8_HOTCURVE_TEMPERATURE 85

#define SMU_FEATURES_LOW_MASK        0x00000000FFFFFFFF
#define SMU_FEATURES_LOW_SHIFT       0
#define SMU_FEATURES_HIGH_MASK       0xFFFFFFFF00000000
#define SMU_FEATURES_HIGH_SHIFT      32

enum {
	GNLD_DPM_PREFETCHER = 0,
	GNLD_DPM_GFXCLK,
	GNLD_DPM_UCLK,
	GNLD_DPM_SOCCLK,
	GNLD_DPM_UVD,
	GNLD_DPM_VCE,
	GNLD_ULV,
	GNLD_DPM_MP0CLK,
	GNLD_DPM_LINK,
	GNLD_DPM_DCEFCLK,
	GNLD_DS_GFXCLK,
	GNLD_DS_SOCCLK,
	GNLD_DS_LCLK,
	GNLD_PPT,
	GNLD_TDC,
	GNLD_THERMAL,
	GNLD_GFX_PER_CU_CG,
	GNLD_RM,
	GNLD_DS_DCEFCLK,
	GNLD_ACDC,
	GNLD_VR0HOT,
	GNLD_VR1HOT,
	GNLD_FW_CTF,
	GNLD_LED_DISPLAY,
	GNLD_FAN_CONTROL,
	GNLD_DIDT,
	GNLD_GFXOFF,
	GNLD_CG,
	GNLD_DPM_FCLK,
	GNLD_DS_FCLK,
	GNLD_DS_MP1CLK,
	GNLD_DS_MP0CLK,
	GNLD_XGMI,
	GNLD_ECC,

	GNLD_FEATURES_MAX
};

struct vega20_dpm_level {
        bool            enabled;
        uint32_t        value;
        uint32_t        param1;
};

struct vega20_dpm_state {
        uint32_t  soft_min_level;
        uint32_t  soft_max_level;
        uint32_t  hard_min_level;
        uint32_t  hard_max_level;
};

struct vega20_single_dpm_table {
        uint32_t                count;
        struct vega20_dpm_state dpm_state;
        struct vega20_dpm_level dpm_levels[MAX_REGULAR_DPM_NUMBER];
};

struct vega20_pcie_table {
        uint16_t count;
        uint8_t  pcie_gen[MAX_PCIE_CONF];
        uint8_t  pcie_lane[MAX_PCIE_CONF];
        uint32_t lclk[MAX_PCIE_CONF];
};

struct vega20_dpm_table {
	struct vega20_single_dpm_table  soc_table;
        struct vega20_single_dpm_table  gfx_table;
        struct vega20_single_dpm_table  mem_table;
        struct vega20_single_dpm_table  eclk_table;
        struct vega20_single_dpm_table  vclk_table;
        struct vega20_single_dpm_table  dclk_table;
        struct vega20_single_dpm_table  dcef_table;
        struct vega20_single_dpm_table  pixel_table;
        struct vega20_single_dpm_table  display_table;
        struct vega20_single_dpm_table  phy_table;
        struct vega20_single_dpm_table  fclk_table;
        struct vega20_pcie_table        pcie_table;
};

enum OD8_FEATURE_ID
{
	OD8_GFXCLK_LIMITS               = 1 << 0,
	OD8_GFXCLK_CURVE                = 1 << 1,
	OD8_UCLK_MAX                    = 1 << 2,
	OD8_POWER_LIMIT                 = 1 << 3,
	OD8_ACOUSTIC_LIMIT_SCLK         = 1 << 4,   //FanMaximumRpm
	OD8_FAN_SPEED_MIN               = 1 << 5,   //FanMinimumPwm
	OD8_TEMPERATURE_FAN             = 1 << 6,   //FanTargetTemperature
	OD8_TEMPERATURE_SYSTEM          = 1 << 7,   //MaxOpTemp
	OD8_MEMORY_TIMING_TUNE          = 1 << 8,
	OD8_FAN_ZERO_RPM_CONTROL        = 1 << 9
};

enum OD8_SETTING_ID
{
	OD8_SETTING_GFXCLK_FMIN = 0,
	OD8_SETTING_GFXCLK_FMAX,
	OD8_SETTING_GFXCLK_FREQ1,
	OD8_SETTING_GFXCLK_VOLTAGE1,
	OD8_SETTING_GFXCLK_FREQ2,
	OD8_SETTING_GFXCLK_VOLTAGE2,
	OD8_SETTING_GFXCLK_FREQ3,
	OD8_SETTING_GFXCLK_VOLTAGE3,
	OD8_SETTING_UCLK_FMAX,
	OD8_SETTING_POWER_PERCENTAGE,
	OD8_SETTING_FAN_ACOUSTIC_LIMIT,
	OD8_SETTING_FAN_MIN_SPEED,
	OD8_SETTING_FAN_TARGET_TEMP,
	OD8_SETTING_OPERATING_TEMP_MAX,
	OD8_SETTING_AC_TIMING,
	OD8_SETTING_FAN_ZERO_RPM_CONTROL,
	OD8_SETTING_COUNT
};

struct vega20_od8_single_setting {
	uint32_t	feature_id;
	int32_t		min_value;
	int32_t		max_value;
	int32_t		current_value;
	int32_t		default_value;
};

struct vega20_od8_settings {
	struct vega20_od8_single_setting	od8_settings_array[OD8_SETTING_COUNT];
};

extern void vega20_set_ppt_funcs(struct smu_context *smu);

#endif
