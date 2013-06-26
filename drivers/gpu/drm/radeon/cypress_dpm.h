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
 */
#ifndef __CYPRESS_DPM_H__
#define __CYPRESS_DPM_H__

#include "rv770_dpm.h"
#include "evergreen_smc.h"

struct evergreen_mc_reg_entry {
	u32 mclk_max;
	u32 mc_data[SMC_EVERGREEN_MC_REGISTER_ARRAY_SIZE];
};

struct evergreen_mc_reg_table {
	u8 last;
	u8 num_entries;
	u16 valid_flag;
	struct evergreen_mc_reg_entry mc_reg_table_entry[MAX_AC_TIMING_ENTRIES];
	SMC_Evergreen_MCRegisterAddress mc_reg_address[SMC_EVERGREEN_MC_REGISTER_ARRAY_SIZE];
};

struct evergreen_ulv_param {
	bool supported;
	struct rv7xx_pl *pl;
};

struct evergreen_arb_registers {
	u32 mc_arb_dram_timing;
	u32 mc_arb_dram_timing2;
	u32 mc_arb_rfsh_rate;
	u32 mc_arb_burst_time;
};

struct at {
	u32 rlp;
	u32 rmp;
	u32 lhp;
	u32 lmp;
};

struct evergreen_power_info {
	/* must be first! */
	struct rv7xx_power_info rv7xx;
	/* flags */
	bool vddci_control;
	bool dynamic_ac_timing;
	bool abm;
	bool mcls;
	bool light_sleep;
	bool memory_transition;
	bool pcie_performance_request;
	bool pcie_performance_request_registered;
	bool sclk_deep_sleep;
	bool dll_default_on;
	bool ls_clock_gating;
	bool smu_uvd_hs;
	bool uvd_enabled;
	/* stored values */
	u16 acpi_vddci;
	u8 mvdd_high_index;
	u8 mvdd_low_index;
	u32 mclk_edc_wr_enable_threshold;
	struct evergreen_mc_reg_table mc_reg_table;
	struct atom_voltage_table vddc_voltage_table;
	struct atom_voltage_table vddci_voltage_table;
	struct evergreen_arb_registers bootup_arb_registers;
	struct evergreen_ulv_param ulv;
	struct at ats[2];
	/* smc offsets */
	u16 mc_reg_table_start;
};

#define CYPRESS_HASI_DFLT                               400000
#define CYPRESS_MGCGTTLOCAL0_DFLT                       0x00000000
#define CYPRESS_MGCGTTLOCAL1_DFLT                       0x00000000
#define CYPRESS_MGCGTTLOCAL2_DFLT                       0x00000000
#define CYPRESS_MGCGTTLOCAL3_DFLT                       0x00000000
#define CYPRESS_MGCGCGTSSMCTRL_DFLT                     0x81944bc0
#define REDWOOD_MGCGCGTSSMCTRL_DFLT                     0x6e944040
#define CEDAR_MGCGCGTSSMCTRL_DFLT                       0x46944040
#define CYPRESS_VRC_DFLT                                0xC00033

#define PCIE_PERF_REQ_REMOVE_REGISTRY   0
#define PCIE_PERF_REQ_FORCE_LOWPOWER    1
#define PCIE_PERF_REQ_PECI_GEN1         2
#define PCIE_PERF_REQ_PECI_GEN2         3
#define PCIE_PERF_REQ_PECI_GEN3         4

int cypress_convert_power_level_to_smc(struct radeon_device *rdev,
				       struct rv7xx_pl *pl,
				       RV770_SMC_HW_PERFORMANCE_LEVEL *level,
				       u8 watermark_level);
int cypress_populate_smc_acpi_state(struct radeon_device *rdev,
				    RV770_SMC_STATETABLE *table);
int cypress_populate_smc_voltage_tables(struct radeon_device *rdev,
					RV770_SMC_STATETABLE *table);
int cypress_populate_smc_initial_state(struct radeon_device *rdev,
				       struct radeon_ps *radeon_initial_state,
				       RV770_SMC_STATETABLE *table);
u32 cypress_calculate_burst_time(struct radeon_device *rdev,
				 u32 engine_clock, u32 memory_clock);
void cypress_notify_link_speed_change_before_state_change(struct radeon_device *rdev);
int cypress_upload_sw_state(struct radeon_device *rdev);
int cypress_upload_mc_reg_table(struct radeon_device *rdev);
void cypress_program_memory_timing_parameters(struct radeon_device *rdev);
void cypress_notify_link_speed_change_after_state_change(struct radeon_device *rdev);
int cypress_construct_voltage_tables(struct radeon_device *rdev);
int cypress_get_mvdd_configuration(struct radeon_device *rdev);
void cypress_enable_spread_spectrum(struct radeon_device *rdev,
				    bool enable);
void cypress_enable_display_gap(struct radeon_device *rdev);
int cypress_get_table_locations(struct radeon_device *rdev);
int cypress_populate_mc_reg_table(struct radeon_device *rdev);
void cypress_program_response_times(struct radeon_device *rdev);
int cypress_notify_smc_display_change(struct radeon_device *rdev,
				      bool has_display);
void cypress_enable_sclk_control(struct radeon_device *rdev,
				 bool enable);
void cypress_enable_mclk_control(struct radeon_device *rdev,
				 bool enable);
void cypress_start_dpm(struct radeon_device *rdev);
void cypress_advertise_gen2_capability(struct radeon_device *rdev);

#endif
