/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#ifndef _TONGA_SMUMGR_H_
#define _TONGA_SMUMGR_H_

#include "smu72_discrete.h"
#include "smu7_smumgr.h"
#include "smu72.h"


#define ASICID_IS_TONGA_P(wDID, bRID)	 \
	(((wDID == 0x6930) && ((bRID == 0xF0) || (bRID == 0xF1) || (bRID == 0xFF))) \
	|| ((wDID == 0x6920) && ((bRID == 0) || (bRID == 1))))

struct tonga_pt_defaults {
	uint8_t   svi_load_line_en;
	uint8_t   svi_load_line_vddC;
	uint8_t   tdc_vddc_throttle_release_limit_perc;
	uint8_t   tdc_mawt;
	uint8_t   tdc_waterfall_ctl;
	uint8_t   dte_ambient_temp_base;
	uint32_t  display_cac;
	uint32_t  bapm_temp_gradient;
	uint16_t  bapmti_r[SMU72_DTE_ITERATIONS * SMU72_DTE_SOURCES * SMU72_DTE_SINKS];
	uint16_t  bapmti_rc[SMU72_DTE_ITERATIONS * SMU72_DTE_SOURCES * SMU72_DTE_SINKS];
};

struct tonga_mc_reg_entry {
	uint32_t mclk_max;
	uint32_t mc_data[SMU72_DISCRETE_MC_REGISTER_ARRAY_SIZE];
};

struct tonga_mc_reg_table {
	uint8_t   last;               /* number of registers*/
	uint8_t   num_entries;        /* number of entries in mc_reg_table_entry used*/
	uint16_t  validflag;          /* indicate the corresponding register is valid or not. 1: valid, 0: invalid. bit0->address[0], bit1->address[1], etc.*/
	struct tonga_mc_reg_entry    mc_reg_table_entry[MAX_AC_TIMING_ENTRIES];
	SMU72_Discrete_MCRegisterAddress mc_reg_address[SMU72_DISCRETE_MC_REGISTER_ARRAY_SIZE];
};


struct tonga_smumgr {

	struct smu7_smumgr                   smu7_data;
	struct SMU72_Discrete_DpmTable       smc_state_table;
	struct SMU72_Discrete_Ulv            ulv_setting;
	struct SMU72_Discrete_PmFuses  power_tune_table;
	const struct tonga_pt_defaults  *power_tune_defaults;
	SMU72_Discrete_MCRegisters      mc_regs;
	struct tonga_mc_reg_table mc_reg_table;
};

#endif
