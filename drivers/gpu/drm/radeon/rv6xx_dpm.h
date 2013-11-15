/*
 * Copyright 2011 Advanced Micro Devices, Inc.
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
 * Authors: Alex Deucher
 */

#ifndef __RV6XX_DPM_H__
#define __RV6XX_DPM_H__

#include "r600_dpm.h"

/* Represents a single SCLK step. */
struct rv6xx_sclk_stepping
{
    u32 vco_frequency;
    u32 post_divider;
};

struct rv6xx_pm_hw_state {
	u32 sclks[R600_PM_NUMBER_OF_ACTIVITY_LEVELS];
	u32 mclks[R600_PM_NUMBER_OF_MCLKS];
	u16 vddc[R600_PM_NUMBER_OF_VOLTAGE_LEVELS];
	bool backbias[R600_PM_NUMBER_OF_VOLTAGE_LEVELS];
	bool pcie_gen2[R600_PM_NUMBER_OF_ACTIVITY_LEVELS];
	u8 high_sclk_index;
	u8 medium_sclk_index;
	u8 low_sclk_index;
	u8 high_mclk_index;
	u8 medium_mclk_index;
	u8 low_mclk_index;
	u8 high_vddc_index;
	u8 medium_vddc_index;
	u8 low_vddc_index;
	u8 rp[R600_PM_NUMBER_OF_ACTIVITY_LEVELS];
	u8 lp[R600_PM_NUMBER_OF_ACTIVITY_LEVELS];
};

struct rv6xx_power_info {
	/* flags */
	bool voltage_control;
	bool sclk_ss;
	bool mclk_ss;
	bool dynamic_ss;
	bool dynamic_pcie_gen2;
	bool thermal_protection;
	bool display_gap;
	bool gfx_clock_gating;
	/* clk values */
	u32 fb_div_scale;
	u32 spll_ref_div;
	u32 mpll_ref_div;
	u32 bsu;
	u32 bsp;
	/* */
	u32 active_auto_throttle_sources;
	/* current power state */
	u32 restricted_levels;
	struct rv6xx_pm_hw_state hw;
};

struct rv6xx_pl {
	u32 sclk;
	u32 mclk;
	u16 vddc;
	u32 flags;
};

struct rv6xx_ps {
	struct rv6xx_pl high;
	struct rv6xx_pl medium;
	struct rv6xx_pl low;
};

#define RV6XX_DEFAULT_VCLK_FREQ  40000 /* 10 khz */
#define RV6XX_DEFAULT_DCLK_FREQ  30000 /* 10 khz */

#endif
