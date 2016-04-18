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

#ifndef _FIJI_HWMGR_H_
#define _FIJI_HWMGR_H_

#include "hwmgr.h"
#include "smu73.h"
#include "smu73_discrete.h"
#include "ppatomctrl.h"
#include "fiji_ppsmc.h"
#include "pp_endian.h"

#define FIJI_MAX_HARDWARE_POWERLEVELS	2
#define FIJI_AT_DFLT	30

#define FIJI_VOLTAGE_CONTROL_NONE                   0x0
#define FIJI_VOLTAGE_CONTROL_BY_GPIO                0x1
#define FIJI_VOLTAGE_CONTROL_BY_SVID2               0x2
#define FIJI_VOLTAGE_CONTROL_MERGED                 0x3

#define DPMTABLE_OD_UPDATE_SCLK     0x00000001
#define DPMTABLE_OD_UPDATE_MCLK     0x00000002
#define DPMTABLE_UPDATE_SCLK        0x00000004
#define DPMTABLE_UPDATE_MCLK        0x00000008

struct fiji_performance_level {
	uint32_t  memory_clock;
	uint32_t  engine_clock;
	uint16_t  pcie_gen;
	uint16_t  pcie_lane;
};

struct fiji_uvd_clocks {
	uint32_t  vclk;
	uint32_t  dclk;
};

struct fiji_vce_clocks {
	uint32_t  evclk;
	uint32_t  ecclk;
};

struct fiji_power_state {
    uint32_t                  magic;
    struct fiji_uvd_clocks    uvd_clks;
    struct fiji_vce_clocks    vce_clks;
    uint32_t                  sam_clk;
    uint32_t                  acp_clk;
    uint16_t                  performance_level_count;
    bool                      dc_compatible;
    uint32_t                  sclk_threshold;
    struct fiji_performance_level  performance_levels[FIJI_MAX_HARDWARE_POWERLEVELS];
};

struct fiji_dpm_level {
	bool	enabled;
    uint32_t	value;
    uint32_t	param1;
};

#define FIJI_MAX_DEEPSLEEP_DIVIDER_ID 5
#define MAX_REGULAR_DPM_NUMBER 8
#define FIJI_MINIMUM_ENGINE_CLOCK 2500

struct fiji_single_dpm_table {
	uint32_t		count;
	struct fiji_dpm_level	dpm_levels[MAX_REGULAR_DPM_NUMBER];
};

struct fiji_dpm_table {
	struct fiji_single_dpm_table  sclk_table;
	struct fiji_single_dpm_table  mclk_table;
	struct fiji_single_dpm_table  pcie_speed_table;
	struct fiji_single_dpm_table  vddc_table;
	struct fiji_single_dpm_table  vddci_table;
	struct fiji_single_dpm_table  mvdd_table;
};

struct fiji_clock_registers {
	uint32_t  vCG_SPLL_FUNC_CNTL;
	uint32_t  vCG_SPLL_FUNC_CNTL_2;
	uint32_t  vCG_SPLL_FUNC_CNTL_3;
	uint32_t  vCG_SPLL_FUNC_CNTL_4;
	uint32_t  vCG_SPLL_SPREAD_SPECTRUM;
	uint32_t  vCG_SPLL_SPREAD_SPECTRUM_2;
	uint32_t  vDLL_CNTL;
	uint32_t  vMCLK_PWRMGT_CNTL;
	uint32_t  vMPLL_AD_FUNC_CNTL;
	uint32_t  vMPLL_DQ_FUNC_CNTL;
	uint32_t  vMPLL_FUNC_CNTL;
	uint32_t  vMPLL_FUNC_CNTL_1;
	uint32_t  vMPLL_FUNC_CNTL_2;
	uint32_t  vMPLL_SS1;
	uint32_t  vMPLL_SS2;
};

struct fiji_voltage_smio_registers {
	uint32_t vS0_VID_LOWER_SMIO_CNTL;
};

#define FIJI_MAX_LEAKAGE_COUNT  8
struct fiji_leakage_voltage {
	uint16_t  count;
	uint16_t  leakage_id[FIJI_MAX_LEAKAGE_COUNT];
	uint16_t  actual_voltage[FIJI_MAX_LEAKAGE_COUNT];
};

struct fiji_vbios_boot_state {
	uint16_t    mvdd_bootup_value;
	uint16_t    vddc_bootup_value;
	uint16_t    vddci_bootup_value;
	uint32_t    sclk_bootup_value;
	uint32_t    mclk_bootup_value;
	uint16_t    pcie_gen_bootup_value;
	uint16_t    pcie_lane_bootup_value;
};

struct fiji_bacos {
	uint32_t                       best_match;
	uint32_t                       baco_flags;
	struct fiji_performance_level  performance_level;
};

/* Ultra Low Voltage parameter structure */
struct fiji_ulv_parm {
	bool                           ulv_supported;
	uint32_t                       cg_ulv_parameter;
	uint32_t                       ulv_volt_change_delay;
	struct fiji_performance_level  ulv_power_level;
};

struct fiji_display_timing {
	uint32_t  min_clock_in_sr;
	uint32_t  num_existing_displays;
};

struct fiji_dpmlevel_enable_mask {
	uint32_t  uvd_dpm_enable_mask;
	uint32_t  vce_dpm_enable_mask;
	uint32_t  acp_dpm_enable_mask;
	uint32_t  samu_dpm_enable_mask;
	uint32_t  sclk_dpm_enable_mask;
	uint32_t  mclk_dpm_enable_mask;
	uint32_t  pcie_dpm_enable_mask;
};

struct fiji_pcie_perf_range {
	uint16_t  max;
	uint16_t  min;
};

struct fiji_hwmgr {
	struct fiji_dpm_table			dpm_table;
	struct fiji_dpm_table			golden_dpm_table;

	uint32_t						voting_rights_clients0;
	uint32_t						voting_rights_clients1;
	uint32_t						voting_rights_clients2;
	uint32_t						voting_rights_clients3;
	uint32_t						voting_rights_clients4;
	uint32_t						voting_rights_clients5;
	uint32_t						voting_rights_clients6;
	uint32_t						voting_rights_clients7;
	uint32_t						static_screen_threshold_unit;
	uint32_t						static_screen_threshold;
	uint32_t						voltage_control;
	uint32_t						vddc_vddci_delta;

	uint32_t						active_auto_throttle_sources;

	struct fiji_clock_registers            clock_registers;
	struct fiji_voltage_smio_registers      voltage_smio_registers;

	bool                           is_memory_gddr5;
	uint16_t                       acpi_vddc;
	bool                           pspp_notify_required;
	uint16_t                       force_pcie_gen;
	uint16_t                       acpi_pcie_gen;
	uint32_t                       pcie_gen_cap;
	uint32_t                       pcie_lane_cap;
	uint32_t                       pcie_spc_cap;
	struct fiji_leakage_voltage          vddc_leakage;
	struct fiji_leakage_voltage          Vddci_leakage;

	uint32_t                             mvdd_control;
	uint32_t                             vddc_mask_low;
	uint32_t                             mvdd_mask_low;
	uint16_t                            max_vddc_in_pptable;
	uint16_t                            min_vddc_in_pptable;
	uint16_t                            max_vddci_in_pptable;
	uint16_t                            min_vddci_in_pptable;
	uint32_t                             mclk_strobe_mode_threshold;
	uint32_t                             mclk_stutter_mode_threshold;
	uint32_t                             mclk_edc_enable_threshold;
	uint32_t                             mclk_edcwr_enable_threshold;
	bool                                is_uvd_enabled;
	struct fiji_vbios_boot_state        vbios_boot_state;

	bool                           battery_state;
	bool                           is_tlu_enabled;

	/* ---- SMC SRAM Address of firmware header tables ---- */
	uint32_t                             sram_end;
	uint32_t                             dpm_table_start;
	uint32_t                             soft_regs_start;
	uint32_t                             mc_reg_table_start;
	uint32_t                             fan_table_start;
	uint32_t                             arb_table_start;
	struct SMU73_Discrete_DpmTable       smc_state_table;
	struct SMU73_Discrete_Ulv            ulv_setting;

	/* ---- Stuff originally coming from Evergreen ---- */
	uint32_t                             vddci_control;
	struct pp_atomctrl_voltage_table     vddc_voltage_table;
	struct pp_atomctrl_voltage_table     vddci_voltage_table;
	struct pp_atomctrl_voltage_table     mvdd_voltage_table;

	uint32_t                             mgcg_cgtt_local2;
	uint32_t                             mgcg_cgtt_local3;
	uint32_t                             gpio_debug;
	uint32_t                             mc_micro_code_feature;
	uint32_t                             highest_mclk;
	uint16_t                             acpi_vddci;
	uint8_t                              mvdd_high_index;
	uint8_t                              mvdd_low_index;
	bool                                 dll_default_on;
	bool                                 performance_request_registered;

	/* ---- Low Power Features ---- */
	struct fiji_bacos                    bacos;
	struct fiji_ulv_parm                 ulv;

	/* ---- CAC Stuff ---- */
	uint32_t                       cac_table_start;
	bool                           cac_configuration_required;
	bool                           driver_calculate_cac_leakage;
	bool                           cac_enabled;

	/* ---- DPM2 Parameters ---- */
	uint32_t                       power_containment_features;
	bool                           enable_dte_feature;
	bool                           enable_tdc_limit_feature;
	bool                           enable_pkg_pwr_tracking_feature;
	bool                           disable_uvd_power_tune_feature;
	struct fiji_pt_defaults       *power_tune_defaults;
	struct SMU73_Discrete_PmFuses  power_tune_table;
	uint32_t                       dte_tj_offset;
	uint32_t                       fast_watermark_threshold;

	/* ---- Phase Shedding ---- */
	bool                           vddc_phase_shed_control;

	/* ---- DI/DT ---- */
	struct fiji_display_timing        display_timing;

	/* ---- Thermal Temperature Setting ---- */
	struct fiji_dpmlevel_enable_mask     dpm_level_enable_mask;
	uint32_t                             need_update_smu7_dpm_table;
	uint32_t                             sclk_dpm_key_disabled;
	uint32_t                             mclk_dpm_key_disabled;
	uint32_t                             pcie_dpm_key_disabled;
	uint32_t                             min_engine_clocks;
	struct fiji_pcie_perf_range          pcie_gen_performance;
	struct fiji_pcie_perf_range          pcie_lane_performance;
	struct fiji_pcie_perf_range          pcie_gen_power_saving;
	struct fiji_pcie_perf_range          pcie_lane_power_saving;
	bool                                 use_pcie_performance_levels;
	bool                                 use_pcie_power_saving_levels;
	uint32_t                             activity_target[SMU73_MAX_LEVELS_GRAPHICS];
	uint32_t                             mclk_activity_target;
	uint32_t                             mclk_dpm0_activity_target;
	uint32_t                             low_sclk_interrupt_threshold;
	uint32_t                             last_mclk_dpm_enable_mask;
	bool                                 uvd_enabled;

	/* ---- Power Gating States ---- */
	bool                           uvd_power_gated;
	bool                           vce_power_gated;
	bool                           samu_power_gated;
	bool                           acp_power_gated;
	bool                           pg_acp_init;
	bool                           frtc_enabled;
	bool                           frtc_status_changed;
};

/* To convert to Q8.8 format for firmware */
#define FIJI_Q88_FORMAT_CONVERSION_UNIT             256

enum Fiji_I2CLineID {
    Fiji_I2CLineID_DDC1 = 0x90,
    Fiji_I2CLineID_DDC2 = 0x91,
    Fiji_I2CLineID_DDC3 = 0x92,
    Fiji_I2CLineID_DDC4 = 0x93,
    Fiji_I2CLineID_DDC5 = 0x94,
    Fiji_I2CLineID_DDC6 = 0x95,
    Fiji_I2CLineID_SCLSDA = 0x96,
    Fiji_I2CLineID_DDCVGA = 0x97
};

#define Fiji_I2C_DDC1DATA          0
#define Fiji_I2C_DDC1CLK           1
#define Fiji_I2C_DDC2DATA          2
#define Fiji_I2C_DDC2CLK           3
#define Fiji_I2C_DDC3DATA          4
#define Fiji_I2C_DDC3CLK           5
#define Fiji_I2C_SDA               40
#define Fiji_I2C_SCL               41
#define Fiji_I2C_DDC4DATA          65
#define Fiji_I2C_DDC4CLK           66
#define Fiji_I2C_DDC5DATA          0x48
#define Fiji_I2C_DDC5CLK           0x49
#define Fiji_I2C_DDC6DATA          0x4a
#define Fiji_I2C_DDC6CLK           0x4b
#define Fiji_I2C_DDCVGADATA        0x4c
#define Fiji_I2C_DDCVGACLK         0x4d

#define FIJI_UNUSED_GPIO_PIN       0x7F

extern int tonga_initializa_dynamic_state_adjustment_rule_settings(struct pp_hwmgr *hwmgr);
extern int tonga_hwmgr_backend_fini(struct pp_hwmgr *hwmgr);
extern int tonga_get_mc_microcode_version (struct pp_hwmgr *hwmgr);
extern int tonga_notify_smc_display_config_after_ps_adjustment(struct pp_hwmgr *hwmgr);
extern int tonga_notify_smc_display_change(struct pp_hwmgr *hwmgr, bool has_display);
int fiji_update_vce_dpm(struct pp_hwmgr *hwmgr, const void *input);
int fiji_update_uvd_dpm(struct pp_hwmgr *hwmgr, bool bgate);
int fiji_update_samu_dpm(struct pp_hwmgr *hwmgr, bool bgate);
int fiji_update_acp_dpm(struct pp_hwmgr *hwmgr, bool bgate);
int fiji_enable_disable_vce_dpm(struct pp_hwmgr *hwmgr, bool enable);

#endif /* _FIJI_HWMGR_H_ */
