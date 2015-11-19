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
#ifndef ELLESMERE_POWERTUNE_H
#define ELLESMERE_POWERTUNE_H

enum ellesmere_pt_config_reg_type {
	ELLESMERE_CONFIGREG_MMR = 0,
	ELLESMERE_CONFIGREG_SMC_IND,
	ELLESMERE_CONFIGREG_DIDT_IND,
	ELLESMERE_CONFIGREG_CACHE,
	ELLESMERE_CONFIGREG_MAX
};

/* PowerContainment Features */
#define POWERCONTAINMENT_FEATURE_DTE             0x00000001
#define POWERCONTAINMENT_FEATURE_TDCLimit        0x00000002
#define POWERCONTAINMENT_FEATURE_PkgPwrLimit     0x00000004

struct ellesmere_pt_config_reg {
	uint32_t                           offset;
	uint32_t                           mask;
	uint32_t                           shift;
	uint32_t                           value;
	enum ellesmere_pt_config_reg_type       type;
};

struct ellesmere_pt_defaults {
	uint8_t   SviLoadLineEn;
	uint8_t   SviLoadLineVddC;
	uint8_t   TDC_VDDC_ThrottleReleaseLimitPerc;
	uint8_t   TDC_MAWt;
	uint8_t   TdcWaterfallCtl;
	uint8_t   DTEAmbientTempBase;

	uint32_t  DisplayCac;
	uint32_t  BAPM_TEMP_GRADIENT;
	uint16_t  BAPMTI_R[SMU74_DTE_ITERATIONS * SMU74_DTE_SOURCES * SMU74_DTE_SINKS];
	uint16_t  BAPMTI_RC[SMU74_DTE_ITERATIONS * SMU74_DTE_SOURCES * SMU74_DTE_SINKS];
};

void ellesmere_initialize_power_tune_defaults(struct pp_hwmgr *hwmgr);
int ellesmere_populate_bapm_parameters_in_dpm_table(struct pp_hwmgr *hwmgr);
int ellesmere_populate_pm_fuses(struct pp_hwmgr *hwmgr);
int ellesmere_enable_smc_cac(struct pp_hwmgr *hwmgr);
int ellesmere_enable_power_containment(struct pp_hwmgr *hwmgr);
int ellesmere_set_power_limit(struct pp_hwmgr *hwmgr, uint32_t n);
int ellesmere_power_control_set_level(struct pp_hwmgr *hwmgr);

#endif  /* ELLESMERE_POWERTUNE_H */

