/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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
#ifndef __NI_DPM_H__
#define __NI_DPM_H__

#include "cypress_dpm.h"
#include "btc_dpm.h"
#include "nislands_smc.h"

struct ni_clock_registers {
	u32 cg_spll_func_cntl;
	u32 cg_spll_func_cntl_2;
	u32 cg_spll_func_cntl_3;
	u32 cg_spll_func_cntl_4;
	u32 cg_spll_spread_spectrum;
	u32 cg_spll_spread_spectrum_2;
	u32 mclk_pwrmgt_cntl;
	u32 dll_cntl;
	u32 mpll_ad_func_cntl;
	u32 mpll_ad_func_cntl_2;
	u32 mpll_dq_func_cntl;
	u32 mpll_dq_func_cntl_2;
	u32 mpll_ss1;
	u32 mpll_ss2;
};

struct ni_mc_reg_entry {
	u32 mclk_max;
	u32 mc_data[SMC_NISLANDS_MC_REGISTER_ARRAY_SIZE];
};

struct ni_mc_reg_table {
	u8 last;
	u8 num_entries;
	u16 valid_flag;
	struct ni_mc_reg_entry mc_reg_table_entry[MAX_AC_TIMING_ENTRIES];
	SMC_NIslands_MCRegisterAddress mc_reg_address[SMC_NISLANDS_MC_REGISTER_ARRAY_SIZE];
};

#define NISLANDS_MCREGISTERTABLE_FIRST_DRIVERSTATE_SLOT 2

enum ni_dc_cac_level
{
	NISLANDS_DCCAC_LEVEL_0 = 0,
	NISLANDS_DCCAC_LEVEL_1,
	NISLANDS_DCCAC_LEVEL_2,
	NISLANDS_DCCAC_LEVEL_3,
	NISLANDS_DCCAC_LEVEL_4,
	NISLANDS_DCCAC_LEVEL_5,
	NISLANDS_DCCAC_LEVEL_6,
	NISLANDS_DCCAC_LEVEL_7,
	NISLANDS_DCCAC_MAX_LEVELS
};

struct ni_leakage_coeffients
{
	u32 at;
	u32 bt;
	u32 av;
	u32 bv;
	s32 t_slope;
	s32 t_intercept;
	u32 t_ref;
};

struct ni_cac_data
{
	struct ni_leakage_coeffients leakage_coefficients;
	u32 i_leakage;
	s32 leakage_minimum_temperature;
	u32 pwr_const;
	u32 dc_cac_value;
	u32 bif_cac_value;
	u32 lkge_pwr;
	u8 mc_wr_weight;
	u8 mc_rd_weight;
	u8 allow_ovrflw;
	u8 num_win_tdp;
	u8 l2num_win_tdp;
	u8 lts_truncate_n;
};

struct ni_cac_weights
{
	u32 weight_tcp_sig0;
	u32 weight_tcp_sig1;
	u32 weight_ta_sig;
	u32 weight_tcc_en0;
	u32 weight_tcc_en1;
	u32 weight_tcc_en2;
	u32 weight_cb_en0;
	u32 weight_cb_en1;
	u32 weight_cb_en2;
	u32 weight_cb_en3;
	u32 weight_db_sig0;
	u32 weight_db_sig1;
	u32 weight_db_sig2;
	u32 weight_db_sig3;
	u32 weight_sxm_sig0;
	u32 weight_sxm_sig1;
	u32 weight_sxm_sig2;
	u32 weight_sxs_sig0;
	u32 weight_sxs_sig1;
	u32 weight_xbr_0;
	u32 weight_xbr_1;
	u32 weight_xbr_2;
	u32 weight_spi_sig0;
	u32 weight_spi_sig1;
	u32 weight_spi_sig2;
	u32 weight_spi_sig3;
	u32 weight_spi_sig4;
	u32 weight_spi_sig5;
	u32 weight_lds_sig0;
	u32 weight_lds_sig1;
	u32 weight_sc;
	u32 weight_bif;
	u32 weight_cp;
	u32 weight_pa_sig0;
	u32 weight_pa_sig1;
	u32 weight_vgt_sig0;
	u32 weight_vgt_sig1;
	u32 weight_vgt_sig2;
	u32 weight_dc_sig0;
	u32 weight_dc_sig1;
	u32 weight_dc_sig2;
	u32 weight_dc_sig3;
	u32 weight_uvd_sig0;
	u32 weight_uvd_sig1;
	u32 weight_spare0;
	u32 weight_spare1;
	u32 weight_sq_vsp;
	u32 weight_sq_vsp0;
	u32 weight_sq_gpr;
	u32 ovr_mode_spare_0;
	u32 ovr_val_spare_0;
	u32 ovr_mode_spare_1;
	u32 ovr_val_spare_1;
	u32 vsp;
	u32 vsp0;
	u32 gpr;
	u8 mc_read_weight;
	u8 mc_write_weight;
	u32 tid_cnt;
	u32 tid_unit;
	u32 l2_lta_window_size;
	u32 lts_truncate;
	u32 dc_cac[NISLANDS_DCCAC_MAX_LEVELS];
	u32 pcie_cac[SMC_NISLANDS_BIF_LUT_NUM_OF_ENTRIES];
	bool enable_power_containment_by_default;
};

struct ni_ps {
	u16 performance_level_count;
	bool dc_compatible;
	struct rv7xx_pl performance_levels[NISLANDS_MAX_SMC_PERFORMANCE_LEVELS_PER_SWSTATE];
};

struct ni_power_info {
	/* must be first! */
	struct evergreen_power_info eg;
	struct ni_clock_registers clock_registers;
	struct ni_mc_reg_table mc_reg_table;
	u32 mclk_rtt_mode_threshold;
	/* flags */
	bool use_power_boost_limit;
	bool support_cac_long_term_average;
	bool cac_enabled;
	bool cac_configuration_required;
	bool driver_calculate_cac_leakage;
	bool pc_enabled;
	bool enable_power_containment;
	bool enable_cac;
	bool enable_sq_ramping;
	/* smc offsets */
	u16 arb_table_start;
	u16 fan_table_start;
	u16 cac_table_start;
	u16 spll_table_start;
	/* CAC stuff */
	struct ni_cac_data cac_data;
	u32 dc_cac_table[NISLANDS_DCCAC_MAX_LEVELS];
	const struct ni_cac_weights *cac_weights;
	u8 lta_window_size;
	u8 lts_truncate;
	struct ni_ps current_ps;
	struct ni_ps requested_ps;
	/* scratch structs */
	SMC_NIslands_MCRegisters smc_mc_reg_table;
	NISLANDS_SMC_STATETABLE smc_statetable;
};

#define NISLANDS_INITIAL_STATE_ARB_INDEX    0
#define NISLANDS_ACPI_STATE_ARB_INDEX       1
#define NISLANDS_ULV_STATE_ARB_INDEX        2
#define NISLANDS_DRIVER_STATE_ARB_INDEX     3

#define NISLANDS_DPM2_MAX_PULSE_SKIP        256

#define NISLANDS_DPM2_NEAR_TDP_DEC          10
#define NISLANDS_DPM2_ABOVE_SAFE_INC        5
#define NISLANDS_DPM2_BELOW_SAFE_INC        20

#define NISLANDS_DPM2_TDP_SAFE_LIMIT_PERCENT            80

#define NISLANDS_DPM2_MAXPS_PERCENT_H                   90
#define NISLANDS_DPM2_MAXPS_PERCENT_M                   0

#define NISLANDS_DPM2_SQ_RAMP_MAX_POWER                 0x3FFF
#define NISLANDS_DPM2_SQ_RAMP_MIN_POWER                 0x12
#define NISLANDS_DPM2_SQ_RAMP_MAX_POWER_DELTA           0x15
#define NISLANDS_DPM2_SQ_RAMP_STI_SIZE                  0x1E
#define NISLANDS_DPM2_SQ_RAMP_LTI_RATIO                 0xF

int ni_copy_and_switch_arb_sets(struct radeon_device *rdev,
				u32 arb_freq_src, u32 arb_freq_dest);
void ni_update_current_ps(struct radeon_device *rdev,
			  struct radeon_ps *rps);
void ni_update_requested_ps(struct radeon_device *rdev,
			    struct radeon_ps *rps);

void ni_set_uvd_clock_before_set_eng_clock(struct radeon_device *rdev,
					   struct radeon_ps *new_ps,
					   struct radeon_ps *old_ps);
void ni_set_uvd_clock_after_set_eng_clock(struct radeon_device *rdev,
					  struct radeon_ps *new_ps,
					  struct radeon_ps *old_ps);

bool ni_dpm_vblank_too_short(struct radeon_device *rdev);

#endif
