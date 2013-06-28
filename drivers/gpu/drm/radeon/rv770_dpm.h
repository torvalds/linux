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
#ifndef __RV770_DPM_H__
#define __RV770_DPM_H__

#include "rv770_smc.h"

struct rv770_clock_registers {
	u32 cg_spll_func_cntl;
	u32 cg_spll_func_cntl_2;
	u32 cg_spll_func_cntl_3;
	u32 cg_spll_spread_spectrum;
	u32 cg_spll_spread_spectrum_2;
	u32 mpll_ad_func_cntl;
	u32 mpll_ad_func_cntl_2;
	u32 mpll_dq_func_cntl;
	u32 mpll_dq_func_cntl_2;
	u32 mclk_pwrmgt_cntl;
	u32 dll_cntl;
	u32 mpll_ss1;
	u32 mpll_ss2;
};

struct rv730_clock_registers {
	u32 cg_spll_func_cntl;
	u32 cg_spll_func_cntl_2;
	u32 cg_spll_func_cntl_3;
	u32 cg_spll_spread_spectrum;
	u32 cg_spll_spread_spectrum_2;
	u32 mclk_pwrmgt_cntl;
	u32 dll_cntl;
	u32 mpll_func_cntl;
	u32 mpll_func_cntl2;
	u32 mpll_func_cntl3;
	u32 mpll_ss;
	u32 mpll_ss2;
};

union r7xx_clock_registers {
	struct rv770_clock_registers rv770;
	struct rv730_clock_registers rv730;
};

struct vddc_table_entry {
	u16 vddc;
	u8 vddc_index;
	u8 high_smio;
	u32 low_smio;
};

#define MAX_NO_OF_MVDD_VALUES 2
#define MAX_NO_VREG_STEPS 32

struct rv7xx_power_info {
	/* flags */
	bool mem_gddr5;
	bool pcie_gen2;
	bool dynamic_pcie_gen2;
	bool acpi_pcie_gen2;
	bool boot_in_gen2;
	bool voltage_control; /* vddc */
	bool mvdd_control;
	bool sclk_ss;
	bool mclk_ss;
	bool dynamic_ss;
	bool gfx_clock_gating;
	bool mg_clock_gating;
	bool mgcgtssm;
	bool power_gating;
	bool thermal_protection;
	bool display_gap;
	bool dcodt;
	bool ulps;
	/* registers */
	union r7xx_clock_registers clk_regs;
	u32 s0_vid_lower_smio_cntl;
	/* voltage */
	u32 vddc_mask_low;
	u32 mvdd_mask_low;
	u32 mvdd_split_frequency;
	u32 mvdd_low_smio[MAX_NO_OF_MVDD_VALUES];
	u16 max_vddc;
	u16 max_vddc_in_table;
	u16 min_vddc_in_table;
	struct vddc_table_entry vddc_table[MAX_NO_VREG_STEPS];
	u8 valid_vddc_entries;
	/* dc odt */
	u32 mclk_odt_threshold;
	u8 odt_value_0[2];
	u8 odt_value_1[2];
	/* stored values */
	u32 boot_sclk;
	u16 acpi_vddc;
	u32 ref_div;
	u32 active_auto_throttle_sources;
	u32 mclk_stutter_mode_threshold;
	u32 mclk_strobe_mode_threshold;
	u32 mclk_edc_enable_threshold;
	u32 bsp;
	u32 bsu;
	u32 pbsp;
	u32 pbsu;
	u32 dsp;
	u32 psp;
	u32 asi;
	u32 pasi;
	u32 vrc;
	u32 restricted_levels;
	u32 rlp;
	u32 rmp;
	u32 lhp;
	u32 lmp;
	/* smc offsets */
	u16 state_table_start;
	u16 soft_regs_start;
	u16 sram_end;
	/* scratch structs */
	RV770_SMC_STATETABLE smc_statetable;
};

struct rv7xx_pl {
	u32 sclk;
	u32 mclk;
	u16 vddc;
	u16 vddci; /* eg+ only */
	u32 flags;
	enum radeon_pcie_gen pcie_gen; /* si+ only */
};

struct rv7xx_ps {
	struct rv7xx_pl high;
	struct rv7xx_pl medium;
	struct rv7xx_pl low;
	bool dc_compatible;
};

#define RV770_RLP_DFLT                                10
#define RV770_RMP_DFLT                                25
#define RV770_LHP_DFLT                                25
#define RV770_LMP_DFLT                                10
#define RV770_VRC_DFLT                                0x003f
#define RV770_ASI_DFLT                                1000
#define RV770_HASI_DFLT                               200000
#define RV770_MGCGTTLOCAL0_DFLT                       0x00100000
#define RV7XX_MGCGTTLOCAL0_DFLT                       0
#define RV770_MGCGTTLOCAL1_DFLT                       0xFFFF0000
#define RV770_MGCGCGTSSMCTRL_DFLT                     0x55940000

#define MVDD_LOW_INDEX  0
#define MVDD_HIGH_INDEX 1

#define MVDD_LOW_VALUE  0
#define MVDD_HIGH_VALUE 0xffff

#define RV770_DEFAULT_VCLK_FREQ  53300 /* 10 khz */
#define RV770_DEFAULT_DCLK_FREQ  40000 /* 10 khz */

/* rv730/rv710 */
int rv730_populate_sclk_value(struct radeon_device *rdev,
			      u32 engine_clock,
			      RV770_SMC_SCLK_VALUE *sclk);
int rv730_populate_mclk_value(struct radeon_device *rdev,
			      u32 engine_clock, u32 memory_clock,
			      LPRV7XX_SMC_MCLK_VALUE mclk);
void rv730_read_clock_registers(struct radeon_device *rdev);
int rv730_populate_smc_acpi_state(struct radeon_device *rdev,
				  RV770_SMC_STATETABLE *table);
int rv730_populate_smc_initial_state(struct radeon_device *rdev,
				     struct radeon_ps *radeon_initial_state,
				     RV770_SMC_STATETABLE *table);
void rv730_program_memory_timing_parameters(struct radeon_device *rdev,
					    struct radeon_ps *radeon_state);
void rv730_power_gating_enable(struct radeon_device *rdev,
			       bool enable);
void rv730_start_dpm(struct radeon_device *rdev);
void rv730_stop_dpm(struct radeon_device *rdev);
void rv730_program_dcodt(struct radeon_device *rdev, bool use_dcodt);
void rv730_get_odt_values(struct radeon_device *rdev);

/* rv740 */
int rv740_populate_sclk_value(struct radeon_device *rdev, u32 engine_clock,
			      RV770_SMC_SCLK_VALUE *sclk);
int rv740_populate_mclk_value(struct radeon_device *rdev,
			      u32 engine_clock, u32 memory_clock,
			      RV7XX_SMC_MCLK_VALUE *mclk);
void rv740_read_clock_registers(struct radeon_device *rdev);
int rv740_populate_smc_acpi_state(struct radeon_device *rdev,
				  RV770_SMC_STATETABLE *table);
void rv740_enable_mclk_spread_spectrum(struct radeon_device *rdev,
				       bool enable);
u8 rv740_get_mclk_frequency_ratio(u32 memory_clock);
u32 rv740_get_dll_speed(bool is_gddr5, u32 memory_clock);
u32 rv740_get_decoded_reference_divider(u32 encoded_ref);

/* rv770 */
u32 rv770_map_clkf_to_ibias(struct radeon_device *rdev, u32 clkf);
int rv770_populate_vddc_value(struct radeon_device *rdev, u16 vddc,
			      RV770_SMC_VOLTAGE_VALUE *voltage);
int rv770_populate_mvdd_value(struct radeon_device *rdev, u32 mclk,
			      RV770_SMC_VOLTAGE_VALUE *voltage);
u8 rv770_get_seq_value(struct radeon_device *rdev,
		       struct rv7xx_pl *pl);
int rv770_populate_initial_mvdd_value(struct radeon_device *rdev,
				      RV770_SMC_VOLTAGE_VALUE *voltage);
u32 rv770_calculate_memory_refresh_rate(struct radeon_device *rdev,
					u32 engine_clock);
void rv770_program_response_times(struct radeon_device *rdev);
int rv770_populate_smc_sp(struct radeon_device *rdev,
			  struct radeon_ps *radeon_state,
			  RV770_SMC_SWSTATE *smc_state);
int rv770_populate_smc_t(struct radeon_device *rdev,
			 struct radeon_ps *radeon_state,
			 RV770_SMC_SWSTATE *smc_state);
void rv770_read_voltage_smio_registers(struct radeon_device *rdev);
void rv770_get_memory_type(struct radeon_device *rdev);
void r7xx_start_smc(struct radeon_device *rdev);
u8 rv770_get_memory_module_index(struct radeon_device *rdev);
void rv770_get_max_vddc(struct radeon_device *rdev);
void rv770_get_pcie_gen2_status(struct radeon_device *rdev);
void rv770_enable_acpi_pm(struct radeon_device *rdev);
void rv770_restore_cgcg(struct radeon_device *rdev);
bool rv770_dpm_enabled(struct radeon_device *rdev);
void rv770_enable_voltage_control(struct radeon_device *rdev,
				  bool enable);
void rv770_enable_backbias(struct radeon_device *rdev,
			   bool enable);
void rv770_enable_thermal_protection(struct radeon_device *rdev,
				     bool enable);
void rv770_enable_auto_throttle_source(struct radeon_device *rdev,
				       enum radeon_dpm_auto_throttle_src source,
				       bool enable);
void rv770_setup_bsp(struct radeon_device *rdev);
void rv770_program_git(struct radeon_device *rdev);
void rv770_program_tp(struct radeon_device *rdev);
void rv770_program_tpp(struct radeon_device *rdev);
void rv770_program_sstp(struct radeon_device *rdev);
void rv770_program_engine_speed_parameters(struct radeon_device *rdev);
void rv770_program_vc(struct radeon_device *rdev);
void rv770_clear_vc(struct radeon_device *rdev);
int rv770_upload_firmware(struct radeon_device *rdev);
void rv770_stop_dpm(struct radeon_device *rdev);
void r7xx_stop_smc(struct radeon_device *rdev);
void rv770_reset_smio_status(struct radeon_device *rdev);
int rv770_restrict_performance_levels_before_switch(struct radeon_device *rdev);
int rv770_unrestrict_performance_levels_after_switch(struct radeon_device *rdev);
int rv770_halt_smc(struct radeon_device *rdev);
int rv770_resume_smc(struct radeon_device *rdev);
int rv770_set_sw_state(struct radeon_device *rdev);
int rv770_set_boot_state(struct radeon_device *rdev);
int rv7xx_parse_power_table(struct radeon_device *rdev);
void rv770_set_uvd_clock_before_set_eng_clock(struct radeon_device *rdev,
					      struct radeon_ps *new_ps,
					      struct radeon_ps *old_ps);
void rv770_set_uvd_clock_after_set_eng_clock(struct radeon_device *rdev,
					     struct radeon_ps *new_ps,
					     struct radeon_ps *old_ps);

/* smc */
int rv770_read_smc_soft_register(struct radeon_device *rdev,
				 u16 reg_offset, u32 *value);
int rv770_write_smc_soft_register(struct radeon_device *rdev,
				  u16 reg_offset, u32 value);

/* thermal */
int rv770_set_thermal_temperature_range(struct radeon_device *rdev,
					int min_temp, int max_temp);

#endif
