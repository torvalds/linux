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
#ifndef TONGA_HWMGR_H
#define TONGA_HWMGR_H

#include "hwmgr.h"
#include "smu72_discrete.h"
#include "ppatomctrl.h"
#include "ppinterrupt.h"
#include "tonga_powertune.h"
#include "pp_endian.h"

#define TONGA_MAX_HARDWARE_POWERLEVELS 2
#define TONGA_DYNCLK_NUMBER_OF_TREND_COEFFICIENTS 15

struct tonga_performance_level {
	uint32_t	memory_clock;
	uint32_t	engine_clock;
	uint16_t    pcie_gen;
	uint16_t    pcie_lane;
};

struct _phw_tonga_bacos {
	uint32_t                          best_match;
	uint32_t                          baco_flags;
	struct tonga_performance_level		  performance_level;
};
typedef struct _phw_tonga_bacos phw_tonga_bacos;

struct _phw_tonga_uvd_clocks {
	uint32_t   VCLK;
	uint32_t   DCLK;
};

typedef struct _phw_tonga_uvd_clocks phw_tonga_uvd_clocks;

struct _phw_tonga_vce_clocks {
	uint32_t   EVCLK;
	uint32_t   ECCLK;
};

typedef struct _phw_tonga_vce_clocks phw_tonga_vce_clocks;

struct tonga_power_state {
	uint32_t                    magic;
	phw_tonga_uvd_clocks        uvd_clocks;
	phw_tonga_vce_clocks        vce_clocks;
	uint32_t                    sam_clk;
	uint32_t                    acp_clk;
	uint16_t                    performance_level_count;
	bool                        dc_compatible;
	uint32_t                    sclk_threshold;
	struct tonga_performance_level performance_levels[TONGA_MAX_HARDWARE_POWERLEVELS];
};

struct _phw_tonga_dpm_level {
	bool		enabled;
	uint32_t    value;
	uint32_t    param1;
};
typedef struct _phw_tonga_dpm_level phw_tonga_dpm_level;

#define TONGA_MAX_DEEPSLEEP_DIVIDER_ID 5
#define MAX_REGULAR_DPM_NUMBER 8
#define TONGA_MINIMUM_ENGINE_CLOCK 2500

struct tonga_single_dpm_table {
	uint32_t count;
	phw_tonga_dpm_level dpm_levels[MAX_REGULAR_DPM_NUMBER];
};

struct tonga_dpm_table {
	struct tonga_single_dpm_table  sclk_table;
	struct tonga_single_dpm_table  mclk_table;
	struct tonga_single_dpm_table  pcie_speed_table;
	struct tonga_single_dpm_table  vddc_table;
	struct tonga_single_dpm_table  vdd_gfx_table;
	struct tonga_single_dpm_table  vdd_ci_table;
	struct tonga_single_dpm_table  mvdd_table;
};
typedef struct _phw_tonga_dpm_table phw_tonga_dpm_table;


struct _phw_tonga_clock_regisiters {
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
typedef struct _phw_tonga_clock_regisiters phw_tonga_clock_registers;

struct _phw_tonga_voltage_smio_registers {
	uint32_t vs0_vid_lower_smio_cntl;
};
typedef struct _phw_tonga_voltage_smio_registers phw_tonga_voltage_smio_registers;


struct _phw_tonga_mc_reg_entry {
	uint32_t mclk_max;
	uint32_t mc_data[SMU72_DISCRETE_MC_REGISTER_ARRAY_SIZE];
};
typedef struct _phw_tonga_mc_reg_entry phw_tonga_mc_reg_entry;

struct _phw_tonga_mc_reg_table {
	uint8_t   last;               /* number of registers*/
	uint8_t   num_entries;        /* number of entries in mc_reg_table_entry used*/
	uint16_t  validflag;          /* indicate the corresponding register is valid or not. 1: valid, 0: invalid. bit0->address[0], bit1->address[1], etc.*/
	phw_tonga_mc_reg_entry    mc_reg_table_entry[MAX_AC_TIMING_ENTRIES];
	SMU72_Discrete_MCRegisterAddress mc_reg_address[SMU72_DISCRETE_MC_REGISTER_ARRAY_SIZE];
};
typedef struct _phw_tonga_mc_reg_table phw_tonga_mc_reg_table;

#define DISABLE_MC_LOADMICROCODE   1
#define DISABLE_MC_CFGPROGRAMMING  2

/*Ultra Low Voltage parameter structure */
struct _phw_tonga_ulv_parm{
	bool	ulv_supported;
	uint32_t   ch_ulv_parameter;
	uint32_t   ulv_volt_change_delay;
	struct tonga_performance_level   ulv_power_level;
};
typedef struct _phw_tonga_ulv_parm phw_tonga_ulv_parm;

#define TONGA_MAX_LEAKAGE_COUNT  8

struct _phw_tonga_leakage_voltage {
	uint16_t  count;
	uint16_t  leakage_id[TONGA_MAX_LEAKAGE_COUNT];
	uint16_t  actual_voltage[TONGA_MAX_LEAKAGE_COUNT];
};
typedef struct _phw_tonga_leakage_voltage phw_tonga_leakage_voltage;

struct _phw_tonga_display_timing {
	uint32_t min_clock_insr;
	uint32_t num_existing_displays;
};
typedef struct _phw_tonga_display_timing phw_tonga_display_timing;

struct _phw_tonga_dpmlevel_enable_mask {
	uint32_t uvd_dpm_enable_mask;
	uint32_t vce_dpm_enable_mask;
	uint32_t acp_dpm_enable_mask;
	uint32_t samu_dpm_enable_mask;
	uint32_t sclk_dpm_enable_mask;
	uint32_t mclk_dpm_enable_mask;
	uint32_t pcie_dpm_enable_mask;
};
typedef struct _phw_tonga_dpmlevel_enable_mask phw_tonga_dpmlevel_enable_mask;

struct _phw_tonga_pcie_perf_range {
	uint16_t max;
	uint16_t min;
};
typedef struct _phw_tonga_pcie_perf_range phw_tonga_pcie_perf_range;

struct _phw_tonga_vbios_boot_state {
	uint16_t					mvdd_bootup_value;
	uint16_t					vddc_bootup_value;
	uint16_t					vddci_bootup_value;
	uint16_t					vddgfx_bootup_value;
	uint32_t					sclk_bootup_value;
	uint32_t					mclk_bootup_value;
	uint16_t					pcie_gen_bootup_value;
	uint16_t					pcie_lane_bootup_value;
};
typedef struct _phw_tonga_vbios_boot_state phw_tonga_vbios_boot_state;

#define DPMTABLE_OD_UPDATE_SCLK     0x00000001
#define DPMTABLE_OD_UPDATE_MCLK     0x00000002
#define DPMTABLE_UPDATE_SCLK        0x00000004
#define DPMTABLE_UPDATE_MCLK        0x00000008

/* We need to review which fields are needed. */
/* This is mostly a copy of the RV7xx/Evergreen structure which is close, but not identical to the N.Islands one. */
struct tonga_hwmgr {
	struct tonga_dpm_table               dpm_table;
	struct tonga_dpm_table               golden_dpm_table;

	uint32_t                           voting_rights_clients0;
	uint32_t                           voting_rights_clients1;
	uint32_t                           voting_rights_clients2;
	uint32_t                           voting_rights_clients3;
	uint32_t                           voting_rights_clients4;
	uint32_t                           voting_rights_clients5;
	uint32_t                           voting_rights_clients6;
	uint32_t                           voting_rights_clients7;
	uint32_t                           static_screen_threshold_unit;
	uint32_t                           static_screen_threshold;
	uint32_t                           voltage_control;
	uint32_t                           vdd_gfx_control;

	uint32_t                           vddc_vddci_delta;
	uint32_t                           vddc_vddgfx_delta;

	struct pp_interrupt_registration_info    internal_high_thermal_interrupt_info;
	struct pp_interrupt_registration_info    internal_low_thermal_interrupt_info;
	struct pp_interrupt_registration_info    smc_to_host_interrupt_info;
	uint32_t                          active_auto_throttle_sources;

	struct pp_interrupt_registration_info    external_throttle_interrupt;
	irq_handler_func_t             external_throttle_callback;
	void                             *external_throttle_context;

	struct pp_interrupt_registration_info    ctf_interrupt_info;
	irq_handler_func_t             ctf_callback;
	void                             *ctf_context;

	phw_tonga_clock_registers	  clock_registers;
	phw_tonga_voltage_smio_registers  voltage_smio_registers;

	bool	is_memory_GDDR5;
	uint16_t                          acpi_vddc;
	bool	pspp_notify_required;        /* Flag to indicate if PSPP notification to SBIOS is required */
	uint16_t                          force_pcie_gen;            /* The forced PCI-E speed if not 0xffff */
	uint16_t                          acpi_pcie_gen;             /* The PCI-E speed at ACPI time */
	uint32_t                           pcie_gen_cap;             /* The PCI-E speed capabilities bitmap from CAIL */
	uint32_t                           pcie_lane_cap;            /* The PCI-E lane capabilities bitmap from CAIL */
	uint32_t                           pcie_spc_cap;             /* Symbol Per Clock Capabilities from registry */
	phw_tonga_leakage_voltage	vddc_leakage;            /* The Leakage VDDC supported (based on leakage ID).*/
	phw_tonga_leakage_voltage	vddcgfx_leakage;         /* The Leakage VDDC supported (based on leakage ID). */
	phw_tonga_leakage_voltage	vddci_leakage;           /* The Leakage VDDCI supported (based on leakage ID). */

	uint32_t                           mvdd_control;
	uint32_t                           vddc_mask_low;
	uint32_t                           mvdd_mask_low;
	uint16_t                          max_vddc_in_pp_table;        /* the maximum VDDC value in the powerplay table*/
	uint16_t                          min_vddc_in_pp_table;
	uint16_t                          max_vddci_in_pp_table;       /* the maximum VDDCI value in the powerplay table */
	uint16_t                          min_vddci_in_pp_table;
	uint32_t                           mclk_strobe_mode_threshold;
	uint32_t                           mclk_stutter_mode_threshold;
	uint32_t                           mclk_edc_enable_threshold;
	uint32_t                           mclk_edc_wr_enable_threshold;
	bool	is_uvd_enabled;
	bool	is_xdma_enabled;
	phw_tonga_vbios_boot_state      vbios_boot_state;

	bool                         battery_state;
	bool                         is_tlu_enabled;
	bool                         pcie_performance_request;

	/* -------------- SMC SRAM Address of firmware header tables ----------------*/
	uint32_t                           sram_end;                 /* The first address after the SMC SRAM. */
	uint32_t                           dpm_table_start;          /* The start of the dpm table in the SMC SRAM. */
	uint32_t                           soft_regs_start;          /* The start of the soft registers in the SMC SRAM. */
	uint32_t                           mc_reg_table_start;       /* The start of the mc register table in the SMC SRAM. */
	uint32_t                           fan_table_start;          /* The start of the fan table in the SMC SRAM. */
	uint32_t                           arb_table_start;          /* The start of the ARB setting table in the SMC SRAM. */
	SMU72_Discrete_DpmTable         smc_state_table;             /* The carbon copy of the SMC state table. */
	SMU72_Discrete_MCRegisters      mc_reg_table;
	SMU72_Discrete_Ulv              ulv_setting;                 /* The carbon copy of ULV setting. */
	/* -------------- Stuff originally coming from Evergreen --------------------*/
	phw_tonga_mc_reg_table			tonga_mc_reg_table;
	uint32_t                         vdd_ci_control;
	pp_atomctrl_voltage_table        vddc_voltage_table;
	pp_atomctrl_voltage_table        vddci_voltage_table;
	pp_atomctrl_voltage_table        vddgfx_voltage_table;
	pp_atomctrl_voltage_table        mvdd_voltage_table;

	uint32_t                           mgcg_cgtt_local2;
	uint32_t                           mgcg_cgtt_local3;
	uint32_t                           gpio_debug;
	uint32_t							mc_micro_code_feature;
	uint32_t							highest_mclk;
	uint16_t                          acpi_vdd_ci;
	uint8_t                           mvdd_high_index;
	uint8_t                           mvdd_low_index;
	bool                         dll_defaule_on;
	bool                         performance_request_registered;

	/* ----------------- Low Power Features ---------------------*/
	phw_tonga_bacos					bacos;
	phw_tonga_ulv_parm              ulv;
	/* ----------------- CAC Stuff ---------------------*/
	uint32_t					cac_table_start;
	bool                         cac_configuration_required;    /* TRUE if PP_CACConfigurationRequired == 1 */
	bool                         driver_calculate_cac_leakage;  /* TRUE if PP_DriverCalculateCACLeakage == 1 */
	bool                         cac_enabled;
	/* ----------------- DPM2 Parameters ---------------------*/
	uint32_t					power_containment_features;
	bool                         enable_bapm_feature;
	bool                         enable_tdc_limit_feature;
	bool                         enable_pkg_pwr_tracking_feature;
	bool                         disable_uvd_power_tune_feature;
	phw_tonga_pt_defaults           *power_tune_defaults;
	SMU72_Discrete_PmFuses           power_tune_table;
	uint32_t                           ul_dte_tj_offset;             /* Fudge factor in DPM table to correct HW DTE errors */
	uint32_t                           fast_watemark_threshold;      /* use fast watermark if clock is equal or above this. In percentage of the target high sclk. */

	/* ----------------- Phase Shedding ---------------------*/
	bool                         vddc_phase_shed_control;
	/* --------------------- DI/DT --------------------------*/
	phw_tonga_display_timing       display_timing;
	/* --------- ReadRegistry data for memory and engine clock margins ---- */
	uint32_t                           engine_clock_data;
	uint32_t                           memory_clock_data;
	/* -------- Thermal Temperature Setting --------------*/
	phw_tonga_dpmlevel_enable_mask     dpm_level_enable_mask;
	uint32_t                           need_update_smu7_dpm_table;
	uint32_t                           sclk_dpm_key_disabled;
	uint32_t                           mclk_dpm_key_disabled;
	uint32_t                           pcie_dpm_key_disabled;
	uint32_t                           min_engine_clocks; /* used to store the previous dal min sclock */
	phw_tonga_pcie_perf_range       pcie_gen_performance;
	phw_tonga_pcie_perf_range       pcie_lane_performance;
	phw_tonga_pcie_perf_range       pcie_gen_power_saving;
	phw_tonga_pcie_perf_range       pcie_lane_power_saving;
	bool                            use_pcie_performance_levels;
	bool                            use_pcie_power_saving_levels;
	uint32_t                           activity_target[SMU72_MAX_LEVELS_GRAPHICS]; /* percentage value from 0-100, default 50 */
	uint32_t                           mclk_activity_target;
	uint32_t                           low_sclk_interrupt_threshold;
	uint32_t                           last_mclk_dpm_enable_mask;
	bool								uvd_enabled;
	uint32_t                           pcc_monitor_enabled;

	/* --------- Power Gating States ------------*/
	bool                           uvd_power_gated;  /* 1: gated, 0:not gated */
	bool                           vce_power_gated;  /* 1: gated, 0:not gated */
	bool                           samu_power_gated; /* 1: gated, 0:not gated */
	bool                           acp_power_gated;  /* 1: gated, 0:not gated */
	bool                           pg_acp_init;

	/* soft pptable for re-uploading into smu */
	void *soft_pp_table;
};

typedef struct tonga_hwmgr tonga_hwmgr;

#define TONGA_DPM2_NEAR_TDP_DEC          10
#define TONGA_DPM2_ABOVE_SAFE_INC        5
#define TONGA_DPM2_BELOW_SAFE_INC        20

#define TONGA_DPM2_LTA_WINDOW_SIZE       7  /* Log2 of the LTA window size (l2numWin_TDP). Eg. If LTA windows size is 128, then this value should be Log2(128) = 7. */

#define TONGA_DPM2_LTS_TRUNCATE          0

#define TONGA_DPM2_TDP_SAFE_LIMIT_PERCENT            80  /* Maximum 100 */

#define TONGA_DPM2_MAXPS_PERCENT_H                   90  /* Maximum 0xFF */
#define TONGA_DPM2_MAXPS_PERCENT_M                   90  /* Maximum 0xFF */

#define TONGA_DPM2_PWREFFICIENCYRATIO_MARGIN         50

#define TONGA_DPM2_SQ_RAMP_MAX_POWER                 0x3FFF
#define TONGA_DPM2_SQ_RAMP_MIN_POWER                 0x12
#define TONGA_DPM2_SQ_RAMP_MAX_POWER_DELTA           0x15
#define TONGA_DPM2_SQ_RAMP_SHORT_TERM_INTERVAL_SIZE  0x1E
#define TONGA_DPM2_SQ_RAMP_LONG_TERM_INTERVAL_RATIO  0xF

#define TONGA_VOLTAGE_CONTROL_NONE                   0x0
#define TONGA_VOLTAGE_CONTROL_BY_GPIO                0x1
#define TONGA_VOLTAGE_CONTROL_BY_SVID2               0x2
#define TONGA_VOLTAGE_CONTROL_MERGED                 0x3

#define TONGA_Q88_FORMAT_CONVERSION_UNIT             256 /*To convert to Q8.8 format for firmware */

#define TONGA_UNUSED_GPIO_PIN                        0x7F

int tonga_hwmgr_init(struct pp_hwmgr *hwmgr);
int tonga_update_vce_dpm(struct pp_hwmgr *hwmgr, const void *input);
int tonga_update_uvd_dpm(struct pp_hwmgr *hwmgr, bool bgate);
int tonga_enable_disable_uvd_dpm(struct pp_hwmgr *hwmgr, bool enable);
int tonga_enable_disable_vce_dpm(struct pp_hwmgr *hwmgr, bool enable);
uint32_t tonga_get_xclk(struct pp_hwmgr *hwmgr);

#endif

