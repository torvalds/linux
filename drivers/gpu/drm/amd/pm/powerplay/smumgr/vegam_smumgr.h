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

#ifndef _VEGAM_SMUMANAGER_H
#define _VEGAM_SMUMANAGER_H


#include <pp_endian.h>
#include "smu75_discrete.h"
#include "smu7_smumgr.h"

#define SMC_RAM_END 0x40000

#define DPMTuning_Uphyst_Shift    0
#define DPMTuning_Downhyst_Shift  8
#define DPMTuning_Activity_Shift  16

#define GraphicsDPMTuning_VEGAM    0x001e6400
#define MemoryDPMTuning_VEGAM      0x000f3c0a
#define SclkDPMTuning_VEGAM        0x002d000a
#define MclkDPMTuning_VEGAM        0x001f100a


struct vegam_pt_defaults {
	uint8_t   SviLoadLineEn;
	uint8_t   SviLoadLineVddC;
	uint8_t   TDC_VDDC_ThrottleReleaseLimitPerc;
	uint8_t   TDC_MAWt;
	uint8_t   TdcWaterfallCtl;
	uint8_t   DTEAmbientTempBase;

	uint32_t  DisplayCac;
	uint32_t  BAPM_TEMP_GRADIENT;
	uint16_t  BAPMTI_R[SMU75_DTE_ITERATIONS * SMU75_DTE_SOURCES * SMU75_DTE_SINKS];
	uint16_t  BAPMTI_RC[SMU75_DTE_ITERATIONS * SMU75_DTE_SOURCES * SMU75_DTE_SINKS];
};

struct vegam_range_table {
	uint32_t trans_lower_frequency; /* in 10khz */
	uint32_t trans_upper_frequency;
};

struct vegam_smumgr {
	struct smu7_smumgr smu7_data;
	uint8_t protected_mode;
	SMU75_Discrete_DpmTable              smc_state_table;
	struct SMU75_Discrete_Ulv            ulv_setting;
	struct SMU75_Discrete_PmFuses  power_tune_table;
	struct vegam_range_table                range_table[NUM_SCLK_RANGE];
	const struct vegam_pt_defaults       *power_tune_defaults;
	uint32_t               bif_sclk_table[SMU75_MAX_LEVELS_LINK];
};


#endif
