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
#ifndef _CI_SMUMANAGER_H_
#define _CI_SMUMANAGER_H_

#define SMU__NUM_SCLK_DPM_STATE  8
#define SMU__NUM_MCLK_DPM_LEVELS 6
#define SMU__NUM_LCLK_DPM_LEVELS 8
#define SMU__NUM_PCIE_DPM_LEVELS 8

#include "smu7_discrete.h"
#include <pp_endian.h>
#include "ppatomctrl.h"

struct ci_pt_defaults {
	u8 svi_load_line_en;
	u8 svi_load_line_vddc;
	u8 tdc_vddc_throttle_release_limit_perc;
	u8 tdc_mawt;
	u8 tdc_waterfall_ctl;
	u8 dte_ambient_temp_base;
	u32 display_cac;
	u32 bapm_temp_gradient;
	u16 bapmti_r[SMU7_DTE_ITERATIONS * SMU7_DTE_SOURCES * SMU7_DTE_SINKS];
	u16 bapmti_rc[SMU7_DTE_ITERATIONS * SMU7_DTE_SOURCES * SMU7_DTE_SINKS];
};

struct ci_mc_reg_entry {
	uint32_t mclk_max;
	uint32_t mc_data[SMU7_DISCRETE_MC_REGISTER_ARRAY_SIZE];
};

struct ci_mc_reg_table {
	uint8_t   last;
	uint8_t   num_entries;
	uint16_t  validflag;
	struct ci_mc_reg_entry    mc_reg_table_entry[MAX_AC_TIMING_ENTRIES];
	SMU7_Discrete_MCRegisterAddress mc_reg_address[SMU7_DISCRETE_MC_REGISTER_ARRAY_SIZE];
};

struct ci_smumgr {
	uint32_t                             soft_regs_start;
	uint32_t                             dpm_table_start;
	uint32_t                             mc_reg_table_start;
	uint32_t                             fan_table_start;
	uint32_t                             arb_table_start;
	uint32_t                             ulv_setting_starts;
	struct SMU7_Discrete_DpmTable       smc_state_table;
	struct SMU7_Discrete_PmFuses  power_tune_table;
	const struct ci_pt_defaults  *power_tune_defaults;
	SMU7_Discrete_MCRegisters      mc_regs;
	struct ci_mc_reg_table mc_reg_table;
	uint32_t        activity_target[SMU7_MAX_LEVELS_GRAPHICS];

};

#endif

