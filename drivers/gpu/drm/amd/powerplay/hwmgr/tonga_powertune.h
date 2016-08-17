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

#ifndef TONGA_POWERTUNE_H
#define TONGA_POWERTUNE_H

enum _phw_tonga_ptc_config_reg_type {
	TONGA_CONFIGREG_MMR = 0,
	TONGA_CONFIGREG_SMC_IND,
	TONGA_CONFIGREG_DIDT_IND,
	TONGA_CONFIGREG_CACHE,

	TONGA_CONFIGREG_MAX
};
typedef enum _phw_tonga_ptc_config_reg_type phw_tonga_ptc_config_reg_type;

/* PowerContainment Features */
#define POWERCONTAINMENT_FEATURE_DTE             0x00000001


/* PowerContainment Features */
#define POWERCONTAINMENT_FEATURE_BAPM            0x00000001
#define POWERCONTAINMENT_FEATURE_TDCLimit        0x00000002
#define POWERCONTAINMENT_FEATURE_PkgPwrLimit     0x00000004

struct tonga_pt_config_reg {
	uint32_t                           Offset;
	uint32_t                           Mask;
	uint32_t                           Shift;
	uint32_t                           Value;
	phw_tonga_ptc_config_reg_type     Type;
};

struct tonga_pt_defaults {
	uint8_t   svi_load_line_en;
	uint8_t   svi_load_line_vddC;
	uint8_t   tdc_vddc_throttle_release_limit_perc;
	uint8_t   tdc_mawt;
	uint8_t   tdc_waterfall_ctl;
	uint8_t   dte_ambient_temp_base;
	uint32_t  display_cac;
	uint32_t  bamp_temp_gradient;
	uint16_t  bapmti_r[SMU72_DTE_ITERATIONS * SMU72_DTE_SOURCES * SMU72_DTE_SINKS];
	uint16_t  bapmti_rc[SMU72_DTE_ITERATIONS * SMU72_DTE_SOURCES * SMU72_DTE_SINKS];
};



void tonga_initialize_power_tune_defaults(struct pp_hwmgr *hwmgr);
int tonga_populate_bapm_parameters_in_dpm_table(struct pp_hwmgr *hwmgr);
int tonga_populate_pm_fuses(struct pp_hwmgr *hwmgr);
int tonga_enable_smc_cac(struct pp_hwmgr *hwmgr);
int tonga_disable_smc_cac(struct pp_hwmgr *hwmgr);
int tonga_enable_power_containment(struct pp_hwmgr *hwmgr);
int tonga_disable_power_containment(struct pp_hwmgr *hwmgr);
int tonga_set_power_limit(struct pp_hwmgr *hwmgr, uint32_t n);
int tonga_power_control_set_level(struct pp_hwmgr *hwmgr);

#endif

