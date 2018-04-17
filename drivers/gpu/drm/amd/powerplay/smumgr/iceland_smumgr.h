/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 * Author: Huang Rui <ray.huang@amd.com>
 *
 */

#ifndef _ICELAND_SMUMGR_H_
#define _ICELAND_SMUMGR_H_


#include "smu7_smumgr.h"
#include "pp_endian.h"
#include "smu71_discrete.h"

struct iceland_pt_defaults {
	uint8_t   svi_load_line_en;
	uint8_t   svi_load_line_vddc;
	uint8_t   tdc_vddc_throttle_release_limit_perc;
	uint8_t   tdc_mawt;
	uint8_t   tdc_waterfall_ctl;
	uint8_t   dte_ambient_temp_base;
	uint32_t  display_cac;
	uint32_t  bapm_temp_gradient;
	uint16_t  bapmti_r[SMU71_DTE_ITERATIONS * SMU71_DTE_SOURCES * SMU71_DTE_SINKS];
	uint16_t  bapmti_rc[SMU71_DTE_ITERATIONS * SMU71_DTE_SOURCES * SMU71_DTE_SINKS];
};

struct iceland_mc_reg_entry {
	uint32_t mclk_max;
	uint32_t mc_data[SMU71_DISCRETE_MC_REGISTER_ARRAY_SIZE];
};

struct iceland_mc_reg_table {
	uint8_t   last;               /* number of registers*/
	uint8_t   num_entries;        /* number of entries in mc_reg_table_entry used*/
	uint16_t  validflag;          /* indicate the corresponding register is valid or not. 1: valid, 0: invalid. bit0->address[0], bit1->address[1], etc.*/
	struct iceland_mc_reg_entry    mc_reg_table_entry[MAX_AC_TIMING_ENTRIES];
	SMU71_Discrete_MCRegisterAddress mc_reg_address[SMU71_DISCRETE_MC_REGISTER_ARRAY_SIZE];
};

struct iceland_smumgr {
	struct smu7_smumgr smu7_data;
	struct SMU71_Discrete_DpmTable       smc_state_table;
	struct SMU71_Discrete_PmFuses  power_tune_table;
	struct SMU71_Discrete_Ulv            ulv_setting;
	const struct iceland_pt_defaults  *power_tune_defaults;
	SMU71_Discrete_MCRegisters      mc_regs;
	struct iceland_mc_reg_table mc_reg_table;
	uint32_t        activity_target[SMU71_MAX_LEVELS_GRAPHICS];
};

#endif
