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
#ifndef ICELAND_POWERTUNE_H
#define ICELAND_POWERTUNE_H

#include "smu71.h"

enum iceland_pt_config_reg_type {
	ICELAND_CONFIGREG_MMR = 0,
	ICELAND_CONFIGREG_SMC_IND,
	ICELAND_CONFIGREG_DIDT_IND,
	ICELAND_CONFIGREG_CACHE,
	ICELAND_CONFIGREG_MAX
};

/* PowerContainment Features */
#define POWERCONTAINMENT_FEATURE_DTE             0x00000001
#define POWERCONTAINMENT_FEATURE_TDCLimit        0x00000002
#define POWERCONTAINMENT_FEATURE_PkgPwrLimit     0x00000004
#define POWERCONTAINMENT_FEATURE_BAPM		 0x00000001

struct iceland_pt_config_reg {
	uint32_t                           offset;
	uint32_t                           mask;
	uint32_t                           shift;
	uint32_t                           value;
	enum iceland_pt_config_reg_type       type;
};

struct iceland_pt_defaults
{
	uint8_t   svi_load_line_en;
	uint8_t   svi_load_line_vddc;
	uint8_t   tdc_vddc_throttle_release_limit_perc;
	uint8_t   tdc_mawt;
	uint8_t   tdc_waterfall_ctl;
	uint8_t   dte_ambient_temp_base;
	uint32_t  display_cac;
	uint32_t  bamp_temp_gradient;
	uint16_t  bapmti_r[SMU71_DTE_ITERATIONS * SMU71_DTE_SOURCES * SMU71_DTE_SINKS];
	uint16_t  bapmti_rc[SMU71_DTE_ITERATIONS * SMU71_DTE_SOURCES * SMU71_DTE_SINKS];
};

void iceland_initialize_power_tune_defaults(struct pp_hwmgr *hwmgr);
int iceland_populate_bapm_parameters_in_dpm_table(struct pp_hwmgr *hwmgr);
int iceland_populate_pm_fuses(struct pp_hwmgr *hwmgr);
int iceland_enable_smc_cac(struct pp_hwmgr *hwmgr);
int iceland_enable_power_containment(struct pp_hwmgr *hwmgr);
int iceland_power_control_set_level(struct pp_hwmgr *hwmgr);

#endif  /* ICELAND_POWERTUNE_H */

