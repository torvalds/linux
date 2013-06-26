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
 * Authors: Alex Deucher
 */

#include "drmP.h"
#include "radeon.h"
#include "evergreend.h"
#include "r600_dpm.h"
#include "cypress_dpm.h"
#include "atom.h"

#define SMC_RAM_END 0x8000

#define MC_CG_ARB_FREQ_F0           0x0a
#define MC_CG_ARB_FREQ_F1           0x0b
#define MC_CG_ARB_FREQ_F2           0x0c
#define MC_CG_ARB_FREQ_F3           0x0d

#define MC_CG_SEQ_DRAMCONF_S0       0x05
#define MC_CG_SEQ_DRAMCONF_S1       0x06
#define MC_CG_SEQ_YCLK_SUSPEND      0x04
#define MC_CG_SEQ_YCLK_RESUME       0x0a

struct rv7xx_ps *rv770_get_ps(struct radeon_ps *rps);
struct rv7xx_power_info *rv770_get_pi(struct radeon_device *rdev);
struct evergreen_power_info *evergreen_get_pi(struct radeon_device *rdev);

static u8 cypress_get_mclk_frequency_ratio(struct radeon_device *rdev,
					   u32 memory_clock, bool strobe_mode);

static void cypress_enable_bif_dynamic_pcie_gen2(struct radeon_device *rdev,
						 bool enable)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u32 tmp, bif;

	tmp = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
	if (enable) {
		if ((tmp & LC_OTHER_SIDE_EVER_SENT_GEN2) &&
		    (tmp & LC_OTHER_SIDE_SUPPORTS_GEN2)) {
			if (!pi->boot_in_gen2) {
				bif = RREG32(CG_BIF_REQ_AND_RSP) & ~CG_CLIENT_REQ_MASK;
				bif |= CG_CLIENT_REQ(0xd);
				WREG32(CG_BIF_REQ_AND_RSP, bif);

				tmp &= ~LC_HW_VOLTAGE_IF_CONTROL_MASK;
				tmp |= LC_HW_VOLTAGE_IF_CONTROL(1);
				tmp |= LC_GEN2_EN_STRAP;

				tmp |= LC_CLR_FAILED_SPD_CHANGE_CNT;
				WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, tmp);
				udelay(10);
				tmp &= ~LC_CLR_FAILED_SPD_CHANGE_CNT;
				WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, tmp);
			}
		}
	} else {
		if (!pi->boot_in_gen2) {
			tmp &= ~LC_HW_VOLTAGE_IF_CONTROL_MASK;
			tmp &= ~LC_GEN2_EN_STRAP;
		}
		if ((tmp & LC_OTHER_SIDE_EVER_SENT_GEN2) ||
		    (tmp & LC_OTHER_SIDE_SUPPORTS_GEN2))
			WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, tmp);
	}
}

static void cypress_enable_dynamic_pcie_gen2(struct radeon_device *rdev,
					     bool enable)
{
	cypress_enable_bif_dynamic_pcie_gen2(rdev, enable);

	if (enable)
		WREG32_P(GENERAL_PWRMGT, ENABLE_GEN2PCIE, ~ENABLE_GEN2PCIE);
	else
		WREG32_P(GENERAL_PWRMGT, 0, ~ENABLE_GEN2PCIE);
}

#if 0
static int cypress_enter_ulp_state(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	if (pi->gfx_clock_gating) {
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~DYN_GFX_CLK_OFF_EN);
		WREG32_P(SCLK_PWRMGT_CNTL, GFX_CLK_FORCE_ON, ~GFX_CLK_FORCE_ON);
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~GFX_CLK_FORCE_ON);

		RREG32(GB_ADDR_CONFIG);
	}

	WREG32_P(SMC_MSG, HOST_SMC_MSG(PPSMC_MSG_SwitchToMinimumPower),
		 ~HOST_SMC_MSG_MASK);

	udelay(7000);

	return 0;
}
#endif

static void cypress_gfx_clock_gating_enable(struct radeon_device *rdev,
					    bool enable)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	if (enable) {
		if (eg_pi->light_sleep) {
			WREG32(GRBM_GFX_INDEX, 0xC0000000);

			WREG32_CG(CG_CGLS_TILE_0, 0xFFFFFFFF);
			WREG32_CG(CG_CGLS_TILE_1, 0xFFFFFFFF);
			WREG32_CG(CG_CGLS_TILE_2, 0xFFFFFFFF);
			WREG32_CG(CG_CGLS_TILE_3, 0xFFFFFFFF);
			WREG32_CG(CG_CGLS_TILE_4, 0xFFFFFFFF);
			WREG32_CG(CG_CGLS_TILE_5, 0xFFFFFFFF);
			WREG32_CG(CG_CGLS_TILE_6, 0xFFFFFFFF);
			WREG32_CG(CG_CGLS_TILE_7, 0xFFFFFFFF);
			WREG32_CG(CG_CGLS_TILE_8, 0xFFFFFFFF);
			WREG32_CG(CG_CGLS_TILE_9, 0xFFFFFFFF);
			WREG32_CG(CG_CGLS_TILE_10, 0xFFFFFFFF);
			WREG32_CG(CG_CGLS_TILE_11, 0xFFFFFFFF);

			WREG32_P(SCLK_PWRMGT_CNTL, DYN_LIGHT_SLEEP_EN, ~DYN_LIGHT_SLEEP_EN);
		}
		WREG32_P(SCLK_PWRMGT_CNTL, DYN_GFX_CLK_OFF_EN, ~DYN_GFX_CLK_OFF_EN);
	} else {
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~DYN_GFX_CLK_OFF_EN);
		WREG32_P(SCLK_PWRMGT_CNTL, GFX_CLK_FORCE_ON, ~GFX_CLK_FORCE_ON);
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~GFX_CLK_FORCE_ON);
		RREG32(GB_ADDR_CONFIG);

		if (eg_pi->light_sleep) {
			WREG32_P(SCLK_PWRMGT_CNTL, 0, ~DYN_LIGHT_SLEEP_EN);

			WREG32(GRBM_GFX_INDEX, 0xC0000000);

			WREG32_CG(CG_CGLS_TILE_0, 0);
			WREG32_CG(CG_CGLS_TILE_1, 0);
			WREG32_CG(CG_CGLS_TILE_2, 0);
			WREG32_CG(CG_CGLS_TILE_3, 0);
			WREG32_CG(CG_CGLS_TILE_4, 0);
			WREG32_CG(CG_CGLS_TILE_5, 0);
			WREG32_CG(CG_CGLS_TILE_6, 0);
			WREG32_CG(CG_CGLS_TILE_7, 0);
			WREG32_CG(CG_CGLS_TILE_8, 0);
			WREG32_CG(CG_CGLS_TILE_9, 0);
			WREG32_CG(CG_CGLS_TILE_10, 0);
			WREG32_CG(CG_CGLS_TILE_11, 0);
		}
	}
}

static void cypress_mg_clock_gating_enable(struct radeon_device *rdev,
					   bool enable)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	if (enable) {
		u32 cgts_sm_ctrl_reg;

		if (rdev->family == CHIP_CEDAR)
			cgts_sm_ctrl_reg = CEDAR_MGCGCGTSSMCTRL_DFLT;
		else if (rdev->family == CHIP_REDWOOD)
			cgts_sm_ctrl_reg = REDWOOD_MGCGCGTSSMCTRL_DFLT;
		else
			cgts_sm_ctrl_reg = CYPRESS_MGCGCGTSSMCTRL_DFLT;

		WREG32(GRBM_GFX_INDEX, 0xC0000000);

		WREG32_CG(CG_CGTT_LOCAL_0, CYPRESS_MGCGTTLOCAL0_DFLT);
		WREG32_CG(CG_CGTT_LOCAL_1, CYPRESS_MGCGTTLOCAL1_DFLT & 0xFFFFCFFF);
		WREG32_CG(CG_CGTT_LOCAL_2, CYPRESS_MGCGTTLOCAL2_DFLT);
		WREG32_CG(CG_CGTT_LOCAL_3, CYPRESS_MGCGTTLOCAL3_DFLT);

		if (pi->mgcgtssm)
			WREG32(CGTS_SM_CTRL_REG, cgts_sm_ctrl_reg);

		if (eg_pi->mcls) {
			WREG32_P(MC_CITF_MISC_RD_CG, MEM_LS_ENABLE, ~MEM_LS_ENABLE);
			WREG32_P(MC_CITF_MISC_WR_CG, MEM_LS_ENABLE, ~MEM_LS_ENABLE);
			WREG32_P(MC_CITF_MISC_VM_CG, MEM_LS_ENABLE, ~MEM_LS_ENABLE);
			WREG32_P(MC_HUB_MISC_HUB_CG, MEM_LS_ENABLE, ~MEM_LS_ENABLE);
			WREG32_P(MC_HUB_MISC_VM_CG, MEM_LS_ENABLE, ~MEM_LS_ENABLE);
			WREG32_P(MC_HUB_MISC_SIP_CG, MEM_LS_ENABLE, ~MEM_LS_ENABLE);
			WREG32_P(MC_XPB_CLK_GAT, MEM_LS_ENABLE, ~MEM_LS_ENABLE);
			WREG32_P(VM_L2_CG, MEM_LS_ENABLE, ~MEM_LS_ENABLE);
		}
	} else {
		WREG32(GRBM_GFX_INDEX, 0xC0000000);

		WREG32_CG(CG_CGTT_LOCAL_0, 0xFFFFFFFF);
		WREG32_CG(CG_CGTT_LOCAL_1, 0xFFFFFFFF);
		WREG32_CG(CG_CGTT_LOCAL_2, 0xFFFFFFFF);
		WREG32_CG(CG_CGTT_LOCAL_3, 0xFFFFFFFF);

		if (pi->mgcgtssm)
			WREG32(CGTS_SM_CTRL_REG, 0x81f44bc0);
	}
}

void cypress_enable_spread_spectrum(struct radeon_device *rdev,
				    bool enable)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	if (enable) {
		if (pi->sclk_ss)
			WREG32_P(GENERAL_PWRMGT, DYN_SPREAD_SPECTRUM_EN, ~DYN_SPREAD_SPECTRUM_EN);

		if (pi->mclk_ss)
			WREG32_P(MPLL_CNTL_MODE, SS_SSEN, ~SS_SSEN);
	} else {
		WREG32_P(CG_SPLL_SPREAD_SPECTRUM, 0, ~SSEN);
		WREG32_P(GENERAL_PWRMGT, 0, ~DYN_SPREAD_SPECTRUM_EN);
		WREG32_P(MPLL_CNTL_MODE, 0, ~SS_SSEN);
		WREG32_P(MPLL_CNTL_MODE, 0, ~SS_DSMODE_EN);
	}
}

void cypress_start_dpm(struct radeon_device *rdev)
{
	WREG32_P(GENERAL_PWRMGT, GLOBAL_PWRMGT_EN, ~GLOBAL_PWRMGT_EN);
}

void cypress_enable_sclk_control(struct radeon_device *rdev,
				 bool enable)
{
	if (enable)
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~SCLK_PWRMGT_OFF);
	else
		WREG32_P(SCLK_PWRMGT_CNTL, SCLK_PWRMGT_OFF, ~SCLK_PWRMGT_OFF);
}

void cypress_enable_mclk_control(struct radeon_device *rdev,
				 bool enable)
{
	if (enable)
		WREG32_P(MCLK_PWRMGT_CNTL, 0, ~MPLL_PWRMGT_OFF);
	else
		WREG32_P(MCLK_PWRMGT_CNTL, MPLL_PWRMGT_OFF, ~MPLL_PWRMGT_OFF);
}

int cypress_notify_smc_display_change(struct radeon_device *rdev,
				      bool has_display)
{
	PPSMC_Msg msg = has_display ?
		(PPSMC_Msg)PPSMC_MSG_HasDisplay : (PPSMC_Msg)PPSMC_MSG_NoDisplay;

	if (rv770_send_msg_to_smc(rdev, msg) != PPSMC_Result_OK)
		return -EINVAL;

	return 0;
}

void cypress_program_response_times(struct radeon_device *rdev)
{
	u32 reference_clock;
	u32 mclk_switch_limit;

	reference_clock = radeon_get_xclk(rdev);
	mclk_switch_limit = (460 * reference_clock) / 100;

	rv770_write_smc_soft_register(rdev,
				      RV770_SMC_SOFT_REGISTER_mclk_switch_lim,
				      mclk_switch_limit);

	rv770_write_smc_soft_register(rdev,
				      RV770_SMC_SOFT_REGISTER_mvdd_chg_time, 1);

	rv770_write_smc_soft_register(rdev,
				      RV770_SMC_SOFT_REGISTER_mc_block_delay, 0xAA);

	rv770_program_response_times(rdev);

	if (ASIC_IS_LOMBOK(rdev))
		rv770_write_smc_soft_register(rdev,
					      RV770_SMC_SOFT_REGISTER_is_asic_lombok, 1);

}

static int cypress_pcie_performance_request(struct radeon_device *rdev,
					    u8 perf_req, bool advertise)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	u32 tmp;

	udelay(10);
	tmp = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
	if ((perf_req == PCIE_PERF_REQ_PECI_GEN1) && (tmp & LC_CURRENT_DATA_RATE))
		return 0;

#if defined(CONFIG_ACPI)
	if ((perf_req == PCIE_PERF_REQ_PECI_GEN1) ||
	    (perf_req == PCIE_PERF_REQ_PECI_GEN2)) {
		eg_pi->pcie_performance_request_registered = true;
		return radeon_acpi_pcie_performance_request(rdev, perf_req, advertise);
	} else if ((perf_req == PCIE_PERF_REQ_REMOVE_REGISTRY) &&
		   eg_pi->pcie_performance_request_registered) {
		eg_pi->pcie_performance_request_registered = false;
		return radeon_acpi_pcie_performance_request(rdev, perf_req, advertise);
	}
#endif

	return 0;
}

void cypress_advertise_gen2_capability(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u32 tmp;

#if defined(CONFIG_ACPI)
	radeon_acpi_pcie_notify_device_ready(rdev);
#endif

	tmp = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);

	if ((tmp & LC_OTHER_SIDE_EVER_SENT_GEN2) &&
	    (tmp & LC_OTHER_SIDE_SUPPORTS_GEN2))
		pi->pcie_gen2 = true;
	else
		pi->pcie_gen2 = false;

	if (!pi->pcie_gen2)
		cypress_pcie_performance_request(rdev, PCIE_PERF_REQ_PECI_GEN2, true);

}

static u32 cypress_get_maximum_link_speed(struct radeon_ps *radeon_state)
{
	struct rv7xx_ps *state = rv770_get_ps(radeon_state);

	if (state->high.flags & ATOM_PPLIB_R600_FLAGS_PCIEGEN2)
		return 1;
	return 0;
}

void cypress_notify_link_speed_change_after_state_change(struct radeon_device *rdev)
{
	struct radeon_ps *radeon_new_state = rdev->pm.dpm.requested_ps;
	struct radeon_ps *radeon_current_state = rdev->pm.dpm.current_ps;
	u32 pcie_link_speed_target =  cypress_get_maximum_link_speed(radeon_new_state);
	u32 pcie_link_speed_current = cypress_get_maximum_link_speed(radeon_current_state);
	u8 request;

	if (pcie_link_speed_target < pcie_link_speed_current) {
		if (pcie_link_speed_target == 0)
			request = PCIE_PERF_REQ_PECI_GEN1;
		else if (pcie_link_speed_target == 1)
			request = PCIE_PERF_REQ_PECI_GEN2;
		else
			request = PCIE_PERF_REQ_PECI_GEN3;

		cypress_pcie_performance_request(rdev, request, false);
	}
}

void cypress_notify_link_speed_change_before_state_change(struct radeon_device *rdev)
{
	struct radeon_ps *radeon_new_state = rdev->pm.dpm.requested_ps;
	struct radeon_ps *radeon_current_state = rdev->pm.dpm.current_ps;
	u32 pcie_link_speed_target =  cypress_get_maximum_link_speed(radeon_new_state);
	u32 pcie_link_speed_current = cypress_get_maximum_link_speed(radeon_current_state);
	u8 request;

	if (pcie_link_speed_target > pcie_link_speed_current) {
		if (pcie_link_speed_target == 0)
			request = PCIE_PERF_REQ_PECI_GEN1;
		else if (pcie_link_speed_target == 1)
			request = PCIE_PERF_REQ_PECI_GEN2;
		else
			request = PCIE_PERF_REQ_PECI_GEN3;

		cypress_pcie_performance_request(rdev, request, false);
	}
}

static int cypress_populate_voltage_value(struct radeon_device *rdev,
					  struct atom_voltage_table *table,
					  u16 value, RV770_SMC_VOLTAGE_VALUE *voltage)
{
	unsigned int i;

	for (i = 0; i < table->count; i++) {
		if (value <= table->entries[i].value) {
			voltage->index = (u8)i;
			voltage->value = cpu_to_be16(table->entries[i].value);
			break;
		}
	}

	if (i == table->count)
		return -EINVAL;

	return 0;
}

static u8 cypress_get_strobe_mode_settings(struct radeon_device *rdev, u32 mclk)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u8 result = 0;
	bool strobe_mode = false;

	if (pi->mem_gddr5) {
		if (mclk <= pi->mclk_strobe_mode_threshold)
			strobe_mode = true;
		result = cypress_get_mclk_frequency_ratio(rdev, mclk, strobe_mode);

		if (strobe_mode)
			result |= SMC_STROBE_ENABLE;
	}

	return result;
}

static u32 cypress_map_clkf_to_ibias(struct radeon_device *rdev, u32 clkf)
{
	u32 ref_clk = rdev->clock.mpll.reference_freq;
	u32 vco = clkf * ref_clk;

	/* 100 Mhz ref clk */
	if (ref_clk == 10000) {
		if (vco > 500000)
			return 0xC6;
		if (vco > 400000)
			return 0x9D;
		if (vco > 330000)
			return 0x6C;
		if (vco > 250000)
			return 0x2B;
		if (vco >  160000)
			return 0x5B;
		if (vco > 120000)
			return 0x0A;
		return 0x4B;
	}

	/* 27 Mhz ref clk */
	if (vco > 250000)
		return 0x8B;
	if (vco > 200000)
		return 0xCC;
	if (vco > 150000)
		return 0x9B;
	return 0x6B;
}

static int cypress_populate_mclk_value(struct radeon_device *rdev,
				       u32 engine_clock, u32 memory_clock,
				       RV7XX_SMC_MCLK_VALUE *mclk,
				       bool strobe_mode, bool dll_state_on)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	u32 mpll_ad_func_cntl =
		pi->clk_regs.rv770.mpll_ad_func_cntl;
	u32 mpll_ad_func_cntl_2 =
		pi->clk_regs.rv770.mpll_ad_func_cntl_2;
	u32 mpll_dq_func_cntl =
		pi->clk_regs.rv770.mpll_dq_func_cntl;
	u32 mpll_dq_func_cntl_2 =
		pi->clk_regs.rv770.mpll_dq_func_cntl_2;
	u32 mclk_pwrmgt_cntl =
		pi->clk_regs.rv770.mclk_pwrmgt_cntl;
	u32 dll_cntl =
		pi->clk_regs.rv770.dll_cntl;
	u32 mpll_ss1 = pi->clk_regs.rv770.mpll_ss1;
	u32 mpll_ss2 = pi->clk_regs.rv770.mpll_ss2;
	struct atom_clock_dividers dividers;
	u32 ibias;
	u32 dll_speed;
	int ret;
	u32 mc_seq_misc7;

	ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_MEMORY_PLL_PARAM,
					     memory_clock, strobe_mode, &dividers);
	if (ret)
		return ret;

	if (!strobe_mode) {
		mc_seq_misc7 = RREG32(MC_SEQ_MISC7);

		if(mc_seq_misc7 & 0x8000000)
			dividers.post_div = 1;
	}

	ibias = cypress_map_clkf_to_ibias(rdev, dividers.whole_fb_div);

	mpll_ad_func_cntl &= ~(CLKR_MASK |
			       YCLK_POST_DIV_MASK |
			       CLKF_MASK |
			       CLKFRAC_MASK |
			       IBIAS_MASK);
	mpll_ad_func_cntl |= CLKR(dividers.ref_div);
	mpll_ad_func_cntl |= YCLK_POST_DIV(dividers.post_div);
	mpll_ad_func_cntl |= CLKF(dividers.whole_fb_div);
	mpll_ad_func_cntl |= CLKFRAC(dividers.frac_fb_div);
	mpll_ad_func_cntl |= IBIAS(ibias);

	if (dividers.vco_mode)
		mpll_ad_func_cntl_2 |= VCO_MODE;
	else
		mpll_ad_func_cntl_2 &= ~VCO_MODE;

	if (pi->mem_gddr5) {
		mpll_dq_func_cntl &= ~(CLKR_MASK |
				       YCLK_POST_DIV_MASK |
				       CLKF_MASK |
				       CLKFRAC_MASK |
				       IBIAS_MASK);
		mpll_dq_func_cntl |= CLKR(dividers.ref_div);
		mpll_dq_func_cntl |= YCLK_POST_DIV(dividers.post_div);
		mpll_dq_func_cntl |= CLKF(dividers.whole_fb_div);
		mpll_dq_func_cntl |= CLKFRAC(dividers.frac_fb_div);
		mpll_dq_func_cntl |= IBIAS(ibias);

		if (strobe_mode)
			mpll_dq_func_cntl &= ~PDNB;
		else
			mpll_dq_func_cntl |= PDNB;

		if (dividers.vco_mode)
			mpll_dq_func_cntl_2 |= VCO_MODE;
		else
			mpll_dq_func_cntl_2 &= ~VCO_MODE;
	}

	if (pi->mclk_ss) {
		struct radeon_atom_ss ss;
		u32 vco_freq = memory_clock * dividers.post_div;

		if (radeon_atombios_get_asic_ss_info(rdev, &ss,
						     ASIC_INTERNAL_MEMORY_SS, vco_freq)) {
			u32 reference_clock = rdev->clock.mpll.reference_freq;
			u32 decoded_ref = rv740_get_decoded_reference_divider(dividers.ref_div);
			u32 clk_s = reference_clock * 5 / (decoded_ref * ss.rate);
			u32 clk_v = ss.percentage *
				(0x4000 * dividers.whole_fb_div + 0x800 * dividers.frac_fb_div) / (clk_s * 625);

			mpll_ss1 &= ~CLKV_MASK;
			mpll_ss1 |= CLKV(clk_v);

			mpll_ss2 &= ~CLKS_MASK;
			mpll_ss2 |= CLKS(clk_s);
		}
	}

	dll_speed = rv740_get_dll_speed(pi->mem_gddr5,
					memory_clock);

	mclk_pwrmgt_cntl &= ~DLL_SPEED_MASK;
	mclk_pwrmgt_cntl |= DLL_SPEED(dll_speed);
	if (dll_state_on)
		mclk_pwrmgt_cntl |= (MRDCKA0_PDNB |
				     MRDCKA1_PDNB |
				     MRDCKB0_PDNB |
				     MRDCKB1_PDNB |
				     MRDCKC0_PDNB |
				     MRDCKC1_PDNB |
				     MRDCKD0_PDNB |
				     MRDCKD1_PDNB);
	else
		mclk_pwrmgt_cntl &= ~(MRDCKA0_PDNB |
				      MRDCKA1_PDNB |
				      MRDCKB0_PDNB |
				      MRDCKB1_PDNB |
				      MRDCKC0_PDNB |
				      MRDCKC1_PDNB |
				      MRDCKD0_PDNB |
				      MRDCKD1_PDNB);

	mclk->mclk770.mclk_value = cpu_to_be32(memory_clock);
	mclk->mclk770.vMPLL_AD_FUNC_CNTL = cpu_to_be32(mpll_ad_func_cntl);
	mclk->mclk770.vMPLL_AD_FUNC_CNTL_2 = cpu_to_be32(mpll_ad_func_cntl_2);
	mclk->mclk770.vMPLL_DQ_FUNC_CNTL = cpu_to_be32(mpll_dq_func_cntl);
	mclk->mclk770.vMPLL_DQ_FUNC_CNTL_2 = cpu_to_be32(mpll_dq_func_cntl_2);
	mclk->mclk770.vMCLK_PWRMGT_CNTL = cpu_to_be32(mclk_pwrmgt_cntl);
	mclk->mclk770.vDLL_CNTL = cpu_to_be32(dll_cntl);
	mclk->mclk770.vMPLL_SS = cpu_to_be32(mpll_ss1);
	mclk->mclk770.vMPLL_SS2 = cpu_to_be32(mpll_ss2);

	return 0;
}

static u8 cypress_get_mclk_frequency_ratio(struct radeon_device *rdev,
					   u32 memory_clock, bool strobe_mode)
{
	u8 mc_para_index;

	if (rdev->family >= CHIP_BARTS) {
		if (strobe_mode) {
			if (memory_clock < 10000)
				mc_para_index = 0x00;
			else if (memory_clock > 47500)
				mc_para_index = 0x0f;
			else
				mc_para_index = (u8)((memory_clock - 10000) / 2500);
		} else {
			if (memory_clock < 65000)
				mc_para_index = 0x00;
			else if (memory_clock > 135000)
				mc_para_index = 0x0f;
			else
				mc_para_index = (u8)((memory_clock - 60000) / 5000);
		}
	} else {
		if (strobe_mode) {
			if (memory_clock < 10000)
				mc_para_index = 0x00;
			else if (memory_clock > 47500)
				mc_para_index = 0x0f;
			else
				mc_para_index = (u8)((memory_clock - 10000) / 2500);
		} else {
			if (memory_clock < 40000)
				mc_para_index = 0x00;
			else if (memory_clock > 115000)
				mc_para_index = 0x0f;
			else
				mc_para_index = (u8)((memory_clock - 40000) / 5000);
		}
	}
	return mc_para_index;
}

static int cypress_populate_mvdd_value(struct radeon_device *rdev,
				       u32 mclk,
				       RV770_SMC_VOLTAGE_VALUE *voltage)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	if (!pi->mvdd_control) {
		voltage->index = eg_pi->mvdd_high_index;
		voltage->value = cpu_to_be16(MVDD_HIGH_VALUE);
		return 0;
	}

	if (mclk <= pi->mvdd_split_frequency) {
		voltage->index = eg_pi->mvdd_low_index;
		voltage->value = cpu_to_be16(MVDD_LOW_VALUE);
	} else {
		voltage->index = eg_pi->mvdd_high_index;
		voltage->value = cpu_to_be16(MVDD_HIGH_VALUE);
	}

	return 0;
}

int cypress_convert_power_level_to_smc(struct radeon_device *rdev,
				       struct rv7xx_pl *pl,
				       RV770_SMC_HW_PERFORMANCE_LEVEL *level,
				       u8 watermark_level)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	int ret;
	bool dll_state_on;

	level->gen2PCIE = pi->pcie_gen2 ?
		((pl->flags & ATOM_PPLIB_R600_FLAGS_PCIEGEN2) ? 1 : 0) : 0;
	level->gen2XSP  = (pl->flags & ATOM_PPLIB_R600_FLAGS_PCIEGEN2) ? 1 : 0;
	level->backbias = (pl->flags & ATOM_PPLIB_R600_FLAGS_BACKBIASENABLE) ? 1 : 0;
	level->displayWatermark = watermark_level;

	ret = rv740_populate_sclk_value(rdev, pl->sclk, &level->sclk);
	if (ret)
		return ret;

	level->mcFlags =  0;
	if (pi->mclk_stutter_mode_threshold &&
	    (pl->mclk <= pi->mclk_stutter_mode_threshold) &&
	    !eg_pi->uvd_enabled) {
		level->mcFlags |= SMC_MC_STUTTER_EN;
		if (eg_pi->sclk_deep_sleep)
			level->stateFlags |= PPSMC_STATEFLAG_AUTO_PULSE_SKIP;
		else
			level->stateFlags &= ~PPSMC_STATEFLAG_AUTO_PULSE_SKIP;
	}

	if (pi->mem_gddr5) {
		if (pl->mclk > pi->mclk_edc_enable_threshold)
			level->mcFlags |= SMC_MC_EDC_RD_FLAG;

		if (pl->mclk > eg_pi->mclk_edc_wr_enable_threshold)
			level->mcFlags |= SMC_MC_EDC_WR_FLAG;

		level->strobeMode = cypress_get_strobe_mode_settings(rdev, pl->mclk);

		if (level->strobeMode & SMC_STROBE_ENABLE) {
			if (cypress_get_mclk_frequency_ratio(rdev, pl->mclk, true) >=
			    ((RREG32(MC_SEQ_MISC7) >> 16) & 0xf))
				dll_state_on = ((RREG32(MC_SEQ_MISC5) >> 1) & 0x1) ? true : false;
			else
				dll_state_on = ((RREG32(MC_SEQ_MISC6) >> 1) & 0x1) ? true : false;
		} else
			dll_state_on = eg_pi->dll_default_on;

		ret = cypress_populate_mclk_value(rdev,
						  pl->sclk,
						  pl->mclk,
						  &level->mclk,
						  (level->strobeMode & SMC_STROBE_ENABLE) != 0,
						  dll_state_on);
	} else {
		ret = cypress_populate_mclk_value(rdev,
						  pl->sclk,
						  pl->mclk,
						  &level->mclk,
						  true,
						  true);
	}
	if (ret)
		return ret;

	ret = cypress_populate_voltage_value(rdev,
					     &eg_pi->vddc_voltage_table,
					     pl->vddc,
					     &level->vddc);
	if (ret)
		return ret;

	if (eg_pi->vddci_control) {
		ret = cypress_populate_voltage_value(rdev,
						     &eg_pi->vddci_voltage_table,
						     pl->vddci,
						     &level->vddci);
		if (ret)
			return ret;
	}

	ret = cypress_populate_mvdd_value(rdev, pl->mclk, &level->mvdd);

	return ret;
}

static int cypress_convert_power_state_to_smc(struct radeon_device *rdev,
					      struct radeon_ps *radeon_state,
					      RV770_SMC_SWSTATE *smc_state)
{
	struct rv7xx_ps *state = rv770_get_ps(radeon_state);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	int ret;

	if (!(radeon_state->caps & ATOM_PPLIB_DISALLOW_ON_DC))
		smc_state->flags |= PPSMC_SWSTATE_FLAG_DC;

	ret = cypress_convert_power_level_to_smc(rdev,
						 &state->low,
						 &smc_state->levels[0],
						 PPSMC_DISPLAY_WATERMARK_LOW);
	if (ret)
		return ret;

	ret = cypress_convert_power_level_to_smc(rdev,
						 &state->medium,
						 &smc_state->levels[1],
						 PPSMC_DISPLAY_WATERMARK_LOW);
	if (ret)
		return ret;

	ret = cypress_convert_power_level_to_smc(rdev,
						 &state->high,
						 &smc_state->levels[2],
						 PPSMC_DISPLAY_WATERMARK_HIGH);
	if (ret)
		return ret;

	smc_state->levels[0].arbValue = MC_CG_ARB_FREQ_F1;
	smc_state->levels[1].arbValue = MC_CG_ARB_FREQ_F2;
	smc_state->levels[2].arbValue = MC_CG_ARB_FREQ_F3;

	if (eg_pi->dynamic_ac_timing) {
		smc_state->levels[0].ACIndex = 2;
		smc_state->levels[1].ACIndex = 3;
		smc_state->levels[2].ACIndex = 4;
	} else {
		smc_state->levels[0].ACIndex = 0;
		smc_state->levels[1].ACIndex = 0;
		smc_state->levels[2].ACIndex = 0;
	}

	rv770_populate_smc_sp(rdev, radeon_state, smc_state);

	return rv770_populate_smc_t(rdev, radeon_state, smc_state);
}

static void cypress_convert_mc_registers(struct evergreen_mc_reg_entry *entry,
					 SMC_Evergreen_MCRegisterSet *data,
					 u32 num_entries, u32 valid_flag)
{
	u32 i, j;

	for (i = 0, j = 0; j < num_entries; j++) {
		if (valid_flag & (1 << j)) {
			data->value[i] = cpu_to_be32(entry->mc_data[j]);
			i++;
		}
	}
}

static void cypress_convert_mc_reg_table_entry_to_smc(struct radeon_device *rdev,
						      struct rv7xx_pl *pl,
						      SMC_Evergreen_MCRegisterSet *mc_reg_table_data)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	u32 i = 0;

	for (i = 0; i < eg_pi->mc_reg_table.num_entries; i++) {
		if (pl->mclk <=
		    eg_pi->mc_reg_table.mc_reg_table_entry[i].mclk_max)
			break;
	}

	if ((i == eg_pi->mc_reg_table.num_entries) && (i > 0))
		--i;

	cypress_convert_mc_registers(&eg_pi->mc_reg_table.mc_reg_table_entry[i],
				     mc_reg_table_data,
				     eg_pi->mc_reg_table.last,
				     eg_pi->mc_reg_table.valid_flag);
}

static void cypress_convert_mc_reg_table_to_smc(struct radeon_device *rdev,
						struct radeon_ps *radeon_state,
						SMC_Evergreen_MCRegisters *mc_reg_table)
{
	struct rv7xx_ps *state = rv770_get_ps(radeon_state);

	cypress_convert_mc_reg_table_entry_to_smc(rdev,
						  &state->low,
						  &mc_reg_table->data[2]);
	cypress_convert_mc_reg_table_entry_to_smc(rdev,
						  &state->medium,
						  &mc_reg_table->data[3]);
	cypress_convert_mc_reg_table_entry_to_smc(rdev,
						  &state->high,
						  &mc_reg_table->data[4]);
}

int cypress_upload_sw_state(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct radeon_ps *radeon_new_state = rdev->pm.dpm.requested_ps;
	u16 address = pi->state_table_start +
		offsetof(RV770_SMC_STATETABLE, driverState);
	RV770_SMC_SWSTATE state = { 0 };
	int ret;

	ret = cypress_convert_power_state_to_smc(rdev, radeon_new_state, &state);
	if (ret)
		return ret;

	return rv770_copy_bytes_to_smc(rdev, address, (u8 *)&state,
				    sizeof(RV770_SMC_SWSTATE),
				    pi->sram_end);
}

int cypress_upload_mc_reg_table(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct radeon_ps *radeon_new_state = rdev->pm.dpm.requested_ps;
	SMC_Evergreen_MCRegisters mc_reg_table = { 0 };
	u16 address;

	cypress_convert_mc_reg_table_to_smc(rdev, radeon_new_state, &mc_reg_table);

	address = eg_pi->mc_reg_table_start +
		(u16)offsetof(SMC_Evergreen_MCRegisters, data[2]);

	return rv770_copy_bytes_to_smc(rdev, address,
				       (u8 *)&mc_reg_table.data[2],
				       sizeof(SMC_Evergreen_MCRegisterSet) * 3,
				       pi->sram_end);
}

u32 cypress_calculate_burst_time(struct radeon_device *rdev,
				 u32 engine_clock, u32 memory_clock)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u32 multiplier = pi->mem_gddr5 ? 1 : 2;
	u32 result = (4 * multiplier * engine_clock) / (memory_clock / 2);
	u32 burst_time;

	if (result <= 4)
		burst_time = 0;
	else if (result < 8)
		burst_time = result - 4;
	else {
		burst_time = result / 2 ;
		if (burst_time > 18)
			burst_time = 18;
	}

	return burst_time;
}

void cypress_program_memory_timing_parameters(struct radeon_device *rdev)
{
	struct radeon_ps *radeon_new_state = rdev->pm.dpm.requested_ps;
	struct rv7xx_ps *new_state = rv770_get_ps(radeon_new_state);
	u32 mc_arb_burst_time = RREG32(MC_ARB_BURST_TIME);

	mc_arb_burst_time &= ~(STATE1_MASK | STATE2_MASK | STATE3_MASK);

	mc_arb_burst_time |= STATE1(cypress_calculate_burst_time(rdev,
								 new_state->low.sclk,
								 new_state->low.mclk));
	mc_arb_burst_time |= STATE2(cypress_calculate_burst_time(rdev,
								 new_state->medium.sclk,
								 new_state->medium.mclk));
	mc_arb_burst_time |= STATE3(cypress_calculate_burst_time(rdev,
								 new_state->high.sclk,
								 new_state->high.mclk));

	rv730_program_memory_timing_parameters(rdev, radeon_new_state);

	WREG32(MC_ARB_BURST_TIME, mc_arb_burst_time);
}

static void cypress_populate_mc_reg_addresses(struct radeon_device *rdev,
					      SMC_Evergreen_MCRegisters *mc_reg_table)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	u32 i, j;

	for (i = 0, j = 0; j < eg_pi->mc_reg_table.last; j++) {
		if (eg_pi->mc_reg_table.valid_flag & (1 << j)) {
			mc_reg_table->address[i].s0 =
				cpu_to_be16(eg_pi->mc_reg_table.mc_reg_address[j].s0);
			mc_reg_table->address[i].s1 =
				cpu_to_be16(eg_pi->mc_reg_table.mc_reg_address[j].s1);
			i++;
		}
	}

	mc_reg_table->last = (u8)i;
}

static void cypress_set_mc_reg_address_table(struct radeon_device *rdev)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	u32 i = 0;

	eg_pi->mc_reg_table.mc_reg_address[i].s0 = MC_SEQ_RAS_TIMING_LP >> 2;
	eg_pi->mc_reg_table.mc_reg_address[i].s1 = MC_SEQ_RAS_TIMING >> 2;
	i++;

	eg_pi->mc_reg_table.mc_reg_address[i].s0 = MC_SEQ_CAS_TIMING_LP >> 2;
	eg_pi->mc_reg_table.mc_reg_address[i].s1 = MC_SEQ_CAS_TIMING >> 2;
	i++;

	eg_pi->mc_reg_table.mc_reg_address[i].s0 = MC_SEQ_MISC_TIMING_LP >> 2;
	eg_pi->mc_reg_table.mc_reg_address[i].s1 = MC_SEQ_MISC_TIMING >> 2;
	i++;

	eg_pi->mc_reg_table.mc_reg_address[i].s0 = MC_SEQ_MISC_TIMING2_LP >> 2;
	eg_pi->mc_reg_table.mc_reg_address[i].s1 = MC_SEQ_MISC_TIMING2 >> 2;
	i++;

	eg_pi->mc_reg_table.mc_reg_address[i].s0 = MC_SEQ_RD_CTL_D0_LP >> 2;
	eg_pi->mc_reg_table.mc_reg_address[i].s1 = MC_SEQ_RD_CTL_D0 >> 2;
	i++;

	eg_pi->mc_reg_table.mc_reg_address[i].s0 = MC_SEQ_RD_CTL_D1_LP >> 2;
	eg_pi->mc_reg_table.mc_reg_address[i].s1 = MC_SEQ_RD_CTL_D1 >> 2;
	i++;

	eg_pi->mc_reg_table.mc_reg_address[i].s0 = MC_SEQ_WR_CTL_D0_LP >> 2;
	eg_pi->mc_reg_table.mc_reg_address[i].s1 = MC_SEQ_WR_CTL_D0 >> 2;
	i++;

	eg_pi->mc_reg_table.mc_reg_address[i].s0 = MC_SEQ_WR_CTL_D1_LP >> 2;
	eg_pi->mc_reg_table.mc_reg_address[i].s1 = MC_SEQ_WR_CTL_D1 >> 2;
	i++;

	eg_pi->mc_reg_table.mc_reg_address[i].s0 = MC_SEQ_PMG_CMD_EMRS_LP >> 2;
	eg_pi->mc_reg_table.mc_reg_address[i].s1 = MC_PMG_CMD_EMRS >> 2;
	i++;

	eg_pi->mc_reg_table.mc_reg_address[i].s0 = MC_SEQ_PMG_CMD_MRS_LP >> 2;
	eg_pi->mc_reg_table.mc_reg_address[i].s1 = MC_PMG_CMD_MRS >> 2;
	i++;

	eg_pi->mc_reg_table.mc_reg_address[i].s0 = MC_SEQ_PMG_CMD_MRS1_LP >> 2;
	eg_pi->mc_reg_table.mc_reg_address[i].s1 = MC_PMG_CMD_MRS1 >> 2;
	i++;

	eg_pi->mc_reg_table.mc_reg_address[i].s0 = MC_SEQ_MISC1 >> 2;
	eg_pi->mc_reg_table.mc_reg_address[i].s1 = MC_SEQ_MISC1 >> 2;
	i++;

	eg_pi->mc_reg_table.mc_reg_address[i].s0 = MC_SEQ_RESERVE_M >> 2;
	eg_pi->mc_reg_table.mc_reg_address[i].s1 = MC_SEQ_RESERVE_M >> 2;
	i++;

	eg_pi->mc_reg_table.mc_reg_address[i].s0 = MC_SEQ_MISC3 >> 2;
	eg_pi->mc_reg_table.mc_reg_address[i].s1 = MC_SEQ_MISC3 >> 2;
	i++;

	eg_pi->mc_reg_table.last = (u8)i;
}

static void cypress_retrieve_ac_timing_for_one_entry(struct radeon_device *rdev,
						     struct evergreen_mc_reg_entry *entry)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	u32 i;

	for (i = 0; i < eg_pi->mc_reg_table.last; i++)
		entry->mc_data[i] =
			RREG32(eg_pi->mc_reg_table.mc_reg_address[i].s1 << 2);

}

static void cypress_retrieve_ac_timing_for_all_ranges(struct radeon_device *rdev,
						      struct atom_memory_clock_range_table *range_table)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	u32 i, j;

	for (i = 0; i < range_table->num_entries; i++) {
		eg_pi->mc_reg_table.mc_reg_table_entry[i].mclk_max =
			range_table->mclk[i];
		radeon_atom_set_ac_timing(rdev, range_table->mclk[i]);
		cypress_retrieve_ac_timing_for_one_entry(rdev,
							 &eg_pi->mc_reg_table.mc_reg_table_entry[i]);
	}

	eg_pi->mc_reg_table.num_entries = range_table->num_entries;
	eg_pi->mc_reg_table.valid_flag = 0;

	for (i = 0; i < eg_pi->mc_reg_table.last; i++) {
		for (j = 1; j < range_table->num_entries; j++) {
			if (eg_pi->mc_reg_table.mc_reg_table_entry[j-1].mc_data[i] !=
			    eg_pi->mc_reg_table.mc_reg_table_entry[j].mc_data[i]) {
				eg_pi->mc_reg_table.valid_flag |= (1 << i);
				break;
			}
		}
	}
}

static int cypress_initialize_mc_reg_table(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u8 module_index = rv770_get_memory_module_index(rdev);
	struct atom_memory_clock_range_table range_table = { 0 };
	int ret;

	ret = radeon_atom_get_mclk_range_table(rdev,
					       pi->mem_gddr5,
					       module_index, &range_table);
	if (ret)
		return ret;

	cypress_retrieve_ac_timing_for_all_ranges(rdev, &range_table);

	return 0;
}

static void cypress_wait_for_mc_sequencer(struct radeon_device *rdev, u8 value)
{
	u32 i, j;
	u32 channels = 2;

	if ((rdev->family == CHIP_CYPRESS) ||
	    (rdev->family == CHIP_HEMLOCK))
		channels = 4;
	else if (rdev->family == CHIP_CEDAR)
		channels = 1;

	for (i = 0; i < channels; i++) {
		if ((rdev->family == CHIP_CYPRESS) ||
		    (rdev->family == CHIP_HEMLOCK)) {
			WREG32_P(MC_CONFIG_MCD, MC_RD_ENABLE_MCD(i), ~MC_RD_ENABLE_MCD_MASK);
			WREG32_P(MC_CG_CONFIG_MCD, MC_RD_ENABLE_MCD(i), ~MC_RD_ENABLE_MCD_MASK);
		} else {
			WREG32_P(MC_CONFIG, MC_RD_ENABLE(i), ~MC_RD_ENABLE_MASK);
			WREG32_P(MC_CG_CONFIG, MC_RD_ENABLE(i), ~MC_RD_ENABLE_MASK);
		}
		for (j = 0; j < rdev->usec_timeout; j++) {
			if (((RREG32(MC_SEQ_CG) & CG_SEQ_RESP_MASK) >> CG_SEQ_RESP_SHIFT) == value)
				break;
			udelay(1);
		}
	}
}

static void cypress_force_mc_use_s1(struct radeon_device *rdev)
{
	struct radeon_ps *radeon_boot_state = rdev->pm.dpm.boot_ps;
	struct rv7xx_ps *boot_state = rv770_get_ps(radeon_boot_state);
	u32 strobe_mode;
	u32 mc_seq_cg;
	int i;

	if (RREG32(MC_SEQ_STATUS_M) & PMG_PWRSTATE)
		return;

	radeon_atom_set_ac_timing(rdev, boot_state->low.mclk);
	radeon_mc_wait_for_idle(rdev);

	if ((rdev->family == CHIP_CYPRESS) ||
	    (rdev->family == CHIP_HEMLOCK)) {
		WREG32(MC_CONFIG_MCD, 0xf);
		WREG32(MC_CG_CONFIG_MCD, 0xf);
	} else {
		WREG32(MC_CONFIG, 0xf);
		WREG32(MC_CG_CONFIG, 0xf);
	}

	for (i = 0; i < rdev->num_crtc; i++)
		radeon_wait_for_vblank(rdev, i);

	WREG32(MC_SEQ_CG, MC_CG_SEQ_YCLK_SUSPEND);
	cypress_wait_for_mc_sequencer(rdev, MC_CG_SEQ_YCLK_SUSPEND);

	strobe_mode = cypress_get_strobe_mode_settings(rdev,
						       boot_state->low.mclk);

	mc_seq_cg = CG_SEQ_REQ(MC_CG_SEQ_DRAMCONF_S1);
	mc_seq_cg |= SEQ_CG_RESP(strobe_mode);
	WREG32(MC_SEQ_CG, mc_seq_cg);

	for (i = 0; i < rdev->usec_timeout; i++) {
		if (RREG32(MC_SEQ_STATUS_M) & PMG_PWRSTATE)
			break;
		udelay(1);
	}

	mc_seq_cg &= ~CG_SEQ_REQ_MASK;
	mc_seq_cg |= CG_SEQ_REQ(MC_CG_SEQ_YCLK_RESUME);
	WREG32(MC_SEQ_CG, mc_seq_cg);

	cypress_wait_for_mc_sequencer(rdev, MC_CG_SEQ_YCLK_RESUME);
}

static void cypress_copy_ac_timing_from_s1_to_s0(struct radeon_device *rdev)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	u32 value;
	u32 i;

	for (i = 0; i < eg_pi->mc_reg_table.last; i++) {
		value = RREG32(eg_pi->mc_reg_table.mc_reg_address[i].s1 << 2);
		WREG32(eg_pi->mc_reg_table.mc_reg_address[i].s0 << 2, value);
	}
}

static void cypress_force_mc_use_s0(struct radeon_device *rdev)
{
	struct radeon_ps *radeon_boot_state = rdev->pm.dpm.boot_ps;
	struct rv7xx_ps *boot_state = rv770_get_ps(radeon_boot_state);
	u32 strobe_mode;
	u32 mc_seq_cg;
	int i;

	cypress_copy_ac_timing_from_s1_to_s0(rdev);
	radeon_mc_wait_for_idle(rdev);

	if ((rdev->family == CHIP_CYPRESS) ||
	    (rdev->family == CHIP_HEMLOCK)) {
		WREG32(MC_CONFIG_MCD, 0xf);
		WREG32(MC_CG_CONFIG_MCD, 0xf);
	} else {
		WREG32(MC_CONFIG, 0xf);
		WREG32(MC_CG_CONFIG, 0xf);
	}

	for (i = 0; i < rdev->num_crtc; i++)
		radeon_wait_for_vblank(rdev, i);

	WREG32(MC_SEQ_CG, MC_CG_SEQ_YCLK_SUSPEND);
	cypress_wait_for_mc_sequencer(rdev, MC_CG_SEQ_YCLK_SUSPEND);

	strobe_mode = cypress_get_strobe_mode_settings(rdev,
						       boot_state->low.mclk);

	mc_seq_cg = CG_SEQ_REQ(MC_CG_SEQ_DRAMCONF_S0);
	mc_seq_cg |= SEQ_CG_RESP(strobe_mode);
	WREG32(MC_SEQ_CG, mc_seq_cg);

	for (i = 0; i < rdev->usec_timeout; i++) {
		if (!(RREG32(MC_SEQ_STATUS_M) & PMG_PWRSTATE))
			break;
		udelay(1);
	}

	mc_seq_cg &= ~CG_SEQ_REQ_MASK;
	mc_seq_cg |= CG_SEQ_REQ(MC_CG_SEQ_YCLK_RESUME);
	WREG32(MC_SEQ_CG, mc_seq_cg);

	cypress_wait_for_mc_sequencer(rdev, MC_CG_SEQ_YCLK_RESUME);
}

static int cypress_populate_initial_mvdd_value(struct radeon_device *rdev,
					       RV770_SMC_VOLTAGE_VALUE *voltage)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	voltage->index = eg_pi->mvdd_high_index;
	voltage->value = cpu_to_be16(MVDD_HIGH_VALUE);

	return 0;
}

int cypress_populate_smc_initial_state(struct radeon_device *rdev,
				       struct radeon_ps *radeon_initial_state,
				       RV770_SMC_STATETABLE *table)
{
	struct rv7xx_ps *initial_state = rv770_get_ps(radeon_initial_state);
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	u32 a_t;

	table->initialState.levels[0].mclk.mclk770.vMPLL_AD_FUNC_CNTL =
		cpu_to_be32(pi->clk_regs.rv770.mpll_ad_func_cntl);
	table->initialState.levels[0].mclk.mclk770.vMPLL_AD_FUNC_CNTL_2 =
		cpu_to_be32(pi->clk_regs.rv770.mpll_ad_func_cntl_2);
	table->initialState.levels[0].mclk.mclk770.vMPLL_DQ_FUNC_CNTL =
		cpu_to_be32(pi->clk_regs.rv770.mpll_dq_func_cntl);
	table->initialState.levels[0].mclk.mclk770.vMPLL_DQ_FUNC_CNTL_2 =
		cpu_to_be32(pi->clk_regs.rv770.mpll_dq_func_cntl_2);
	table->initialState.levels[0].mclk.mclk770.vMCLK_PWRMGT_CNTL =
		cpu_to_be32(pi->clk_regs.rv770.mclk_pwrmgt_cntl);
	table->initialState.levels[0].mclk.mclk770.vDLL_CNTL =
		cpu_to_be32(pi->clk_regs.rv770.dll_cntl);

	table->initialState.levels[0].mclk.mclk770.vMPLL_SS =
		cpu_to_be32(pi->clk_regs.rv770.mpll_ss1);
	table->initialState.levels[0].mclk.mclk770.vMPLL_SS2 =
		cpu_to_be32(pi->clk_regs.rv770.mpll_ss2);

	table->initialState.levels[0].mclk.mclk770.mclk_value =
		cpu_to_be32(initial_state->low.mclk);

	table->initialState.levels[0].sclk.vCG_SPLL_FUNC_CNTL =
		cpu_to_be32(pi->clk_regs.rv770.cg_spll_func_cntl);
	table->initialState.levels[0].sclk.vCG_SPLL_FUNC_CNTL_2 =
		cpu_to_be32(pi->clk_regs.rv770.cg_spll_func_cntl_2);
	table->initialState.levels[0].sclk.vCG_SPLL_FUNC_CNTL_3 =
		cpu_to_be32(pi->clk_regs.rv770.cg_spll_func_cntl_3);
	table->initialState.levels[0].sclk.vCG_SPLL_SPREAD_SPECTRUM =
		cpu_to_be32(pi->clk_regs.rv770.cg_spll_spread_spectrum);
	table->initialState.levels[0].sclk.vCG_SPLL_SPREAD_SPECTRUM_2 =
		cpu_to_be32(pi->clk_regs.rv770.cg_spll_spread_spectrum_2);

	table->initialState.levels[0].sclk.sclk_value =
		cpu_to_be32(initial_state->low.sclk);

	table->initialState.levels[0].arbValue = MC_CG_ARB_FREQ_F0;

	table->initialState.levels[0].ACIndex = 0;

	cypress_populate_voltage_value(rdev,
				       &eg_pi->vddc_voltage_table,
				       initial_state->low.vddc,
				       &table->initialState.levels[0].vddc);

	if (eg_pi->vddci_control)
		cypress_populate_voltage_value(rdev,
					       &eg_pi->vddci_voltage_table,
					       initial_state->low.vddci,
					       &table->initialState.levels[0].vddci);

	cypress_populate_initial_mvdd_value(rdev,
					    &table->initialState.levels[0].mvdd);

	a_t = CG_R(0xffff) | CG_L(0);
	table->initialState.levels[0].aT = cpu_to_be32(a_t);

	table->initialState.levels[0].bSP = cpu_to_be32(pi->dsp);


	if (pi->boot_in_gen2)
		table->initialState.levels[0].gen2PCIE = 1;
	else
		table->initialState.levels[0].gen2PCIE = 0;
	if (initial_state->low.flags & ATOM_PPLIB_R600_FLAGS_PCIEGEN2)
		table->initialState.levels[0].gen2XSP = 1;
	else
		table->initialState.levels[0].gen2XSP = 0;

	if (pi->mem_gddr5) {
		table->initialState.levels[0].strobeMode =
			cypress_get_strobe_mode_settings(rdev,
							 initial_state->low.mclk);

		if (initial_state->low.mclk > pi->mclk_edc_enable_threshold)
			table->initialState.levels[0].mcFlags = SMC_MC_EDC_RD_FLAG | SMC_MC_EDC_WR_FLAG;
		else
			table->initialState.levels[0].mcFlags =  0;
	}

	table->initialState.levels[1] = table->initialState.levels[0];
	table->initialState.levels[2] = table->initialState.levels[0];

	table->initialState.flags |= PPSMC_SWSTATE_FLAG_DC;

	return 0;
}

int cypress_populate_smc_acpi_state(struct radeon_device *rdev,
				    RV770_SMC_STATETABLE *table)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	u32 mpll_ad_func_cntl =
		pi->clk_regs.rv770.mpll_ad_func_cntl;
	u32 mpll_ad_func_cntl_2 =
		pi->clk_regs.rv770.mpll_ad_func_cntl_2;
	u32 mpll_dq_func_cntl =
		pi->clk_regs.rv770.mpll_dq_func_cntl;
	u32 mpll_dq_func_cntl_2 =
		pi->clk_regs.rv770.mpll_dq_func_cntl_2;
	u32 spll_func_cntl =
		pi->clk_regs.rv770.cg_spll_func_cntl;
	u32 spll_func_cntl_2 =
		pi->clk_regs.rv770.cg_spll_func_cntl_2;
	u32 spll_func_cntl_3 =
		pi->clk_regs.rv770.cg_spll_func_cntl_3;
	u32 mclk_pwrmgt_cntl =
		pi->clk_regs.rv770.mclk_pwrmgt_cntl;
	u32 dll_cntl =
		pi->clk_regs.rv770.dll_cntl;

	table->ACPIState = table->initialState;

	table->ACPIState.flags &= ~PPSMC_SWSTATE_FLAG_DC;

	if (pi->acpi_vddc) {
		cypress_populate_voltage_value(rdev,
					       &eg_pi->vddc_voltage_table,
					       pi->acpi_vddc,
					       &table->ACPIState.levels[0].vddc);
		if (pi->pcie_gen2) {
			if (pi->acpi_pcie_gen2)
				table->ACPIState.levels[0].gen2PCIE = 1;
			else
				table->ACPIState.levels[0].gen2PCIE = 0;
		} else
			table->ACPIState.levels[0].gen2PCIE = 0;
		if (pi->acpi_pcie_gen2)
			table->ACPIState.levels[0].gen2XSP = 1;
		else
			table->ACPIState.levels[0].gen2XSP = 0;
	} else {
		cypress_populate_voltage_value(rdev,
					       &eg_pi->vddc_voltage_table,
					       pi->min_vddc_in_table,
					       &table->ACPIState.levels[0].vddc);
		table->ACPIState.levels[0].gen2PCIE = 0;
	}

	if (eg_pi->acpi_vddci) {
		if (eg_pi->vddci_control) {
			cypress_populate_voltage_value(rdev,
						       &eg_pi->vddci_voltage_table,
						       eg_pi->acpi_vddci,
						       &table->ACPIState.levels[0].vddci);
		}
	}

	mpll_ad_func_cntl &= ~PDNB;

	mpll_ad_func_cntl_2 |= BIAS_GEN_PDNB | RESET_EN;

	if (pi->mem_gddr5)
		mpll_dq_func_cntl &= ~PDNB;
	mpll_dq_func_cntl_2 |= BIAS_GEN_PDNB | RESET_EN | BYPASS;

	mclk_pwrmgt_cntl |= (MRDCKA0_RESET |
			     MRDCKA1_RESET |
			     MRDCKB0_RESET |
			     MRDCKB1_RESET |
			     MRDCKC0_RESET |
			     MRDCKC1_RESET |
			     MRDCKD0_RESET |
			     MRDCKD1_RESET);

	mclk_pwrmgt_cntl &= ~(MRDCKA0_PDNB |
			      MRDCKA1_PDNB |
			      MRDCKB0_PDNB |
			      MRDCKB1_PDNB |
			      MRDCKC0_PDNB |
			      MRDCKC1_PDNB |
			      MRDCKD0_PDNB |
			      MRDCKD1_PDNB);

	dll_cntl |= (MRDCKA0_BYPASS |
		     MRDCKA1_BYPASS |
		     MRDCKB0_BYPASS |
		     MRDCKB1_BYPASS |
		     MRDCKC0_BYPASS |
		     MRDCKC1_BYPASS |
		     MRDCKD0_BYPASS |
		     MRDCKD1_BYPASS);

	/* evergreen only */
	if (rdev->family <= CHIP_HEMLOCK)
		spll_func_cntl |= SPLL_RESET | SPLL_SLEEP | SPLL_BYPASS_EN;

	spll_func_cntl_2 &= ~SCLK_MUX_SEL_MASK;
	spll_func_cntl_2 |= SCLK_MUX_SEL(4);

	table->ACPIState.levels[0].mclk.mclk770.vMPLL_AD_FUNC_CNTL =
		cpu_to_be32(mpll_ad_func_cntl);
	table->ACPIState.levels[0].mclk.mclk770.vMPLL_AD_FUNC_CNTL_2 =
		cpu_to_be32(mpll_ad_func_cntl_2);
	table->ACPIState.levels[0].mclk.mclk770.vMPLL_DQ_FUNC_CNTL =
		cpu_to_be32(mpll_dq_func_cntl);
	table->ACPIState.levels[0].mclk.mclk770.vMPLL_DQ_FUNC_CNTL_2 =
		cpu_to_be32(mpll_dq_func_cntl_2);
	table->ACPIState.levels[0].mclk.mclk770.vMCLK_PWRMGT_CNTL =
		cpu_to_be32(mclk_pwrmgt_cntl);
	table->ACPIState.levels[0].mclk.mclk770.vDLL_CNTL = cpu_to_be32(dll_cntl);

	table->ACPIState.levels[0].mclk.mclk770.mclk_value = 0;

	table->ACPIState.levels[0].sclk.vCG_SPLL_FUNC_CNTL =
		cpu_to_be32(spll_func_cntl);
	table->ACPIState.levels[0].sclk.vCG_SPLL_FUNC_CNTL_2 =
		cpu_to_be32(spll_func_cntl_2);
	table->ACPIState.levels[0].sclk.vCG_SPLL_FUNC_CNTL_3 =
		cpu_to_be32(spll_func_cntl_3);

	table->ACPIState.levels[0].sclk.sclk_value = 0;

	cypress_populate_mvdd_value(rdev, 0, &table->ACPIState.levels[0].mvdd);

	if (eg_pi->dynamic_ac_timing)
		table->ACPIState.levels[0].ACIndex = 1;

	table->ACPIState.levels[1] = table->ACPIState.levels[0];
	table->ACPIState.levels[2] = table->ACPIState.levels[0];

	return 0;
}

static void cypress_trim_voltage_table_to_fit_state_table(struct radeon_device *rdev,
							  struct atom_voltage_table *voltage_table)
{
	unsigned int i, diff;

	if (voltage_table->count <= MAX_NO_VREG_STEPS)
		return;

	diff = voltage_table->count - MAX_NO_VREG_STEPS;

	for (i= 0; i < MAX_NO_VREG_STEPS; i++)
		voltage_table->entries[i] = voltage_table->entries[i + diff];

	voltage_table->count = MAX_NO_VREG_STEPS;
}

int cypress_construct_voltage_tables(struct radeon_device *rdev)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	int ret;

	ret = radeon_atom_get_voltage_table(rdev, SET_VOLTAGE_TYPE_ASIC_VDDC,
					    &eg_pi->vddc_voltage_table);
	if (ret)
		return ret;

	if (eg_pi->vddc_voltage_table.count > MAX_NO_VREG_STEPS)
		cypress_trim_voltage_table_to_fit_state_table(rdev,
							      &eg_pi->vddc_voltage_table);

	if (eg_pi->vddci_control) {
		ret = radeon_atom_get_voltage_table(rdev, SET_VOLTAGE_TYPE_ASIC_VDDCI,
						    &eg_pi->vddci_voltage_table);
		if (ret)
			return ret;

		if (eg_pi->vddci_voltage_table.count > MAX_NO_VREG_STEPS)
			cypress_trim_voltage_table_to_fit_state_table(rdev,
								      &eg_pi->vddci_voltage_table);
	}

	return 0;
}

static void cypress_populate_smc_voltage_table(struct radeon_device *rdev,
					       struct atom_voltage_table *voltage_table,
					       RV770_SMC_STATETABLE *table)
{
	unsigned int i;

	for (i = 0; i < voltage_table->count; i++) {
		table->highSMIO[i] = 0;
		table->lowSMIO[i] |= cpu_to_be32(voltage_table->entries[i].smio_low);
	}
}

int cypress_populate_smc_voltage_tables(struct radeon_device *rdev,
					RV770_SMC_STATETABLE *table)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	unsigned char i;

	if (eg_pi->vddc_voltage_table.count) {
		cypress_populate_smc_voltage_table(rdev,
						   &eg_pi->vddc_voltage_table,
						   table);

		table->voltageMaskTable.highMask[RV770_SMC_VOLTAGEMASK_VDDC] = 0;
		table->voltageMaskTable.lowMask[RV770_SMC_VOLTAGEMASK_VDDC] =
			cpu_to_be32(eg_pi->vddc_voltage_table.mask_low);

		for (i = 0; i < eg_pi->vddc_voltage_table.count; i++) {
			if (pi->max_vddc_in_table <=
			    eg_pi->vddc_voltage_table.entries[i].value) {
				table->maxVDDCIndexInPPTable = i;
				break;
			}
		}
	}

	if (eg_pi->vddci_voltage_table.count) {
		cypress_populate_smc_voltage_table(rdev,
						   &eg_pi->vddci_voltage_table,
						   table);

		table->voltageMaskTable.highMask[RV770_SMC_VOLTAGEMASK_VDDCI] = 0;
		table->voltageMaskTable.lowMask[RV770_SMC_VOLTAGEMASK_VDDCI] =
			cpu_to_be32(eg_pi->vddc_voltage_table.mask_low);
	}

	return 0;
}

static u32 cypress_get_mclk_split_point(struct atom_memory_info *memory_info)
{
	if ((memory_info->mem_type == MEM_TYPE_GDDR3) ||
	    (memory_info->mem_type == MEM_TYPE_DDR3))
		return 30000;

	return 0;
}

int cypress_get_mvdd_configuration(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	u8 module_index;
	struct atom_memory_info memory_info;
	u32 tmp = RREG32(GENERAL_PWRMGT);

	if (!(tmp & BACKBIAS_PAD_EN)) {
		eg_pi->mvdd_high_index = 0;
		eg_pi->mvdd_low_index = 1;
		pi->mvdd_control = false;
		return 0;
	}

	if (tmp & BACKBIAS_VALUE)
		eg_pi->mvdd_high_index = 1;
	else
		eg_pi->mvdd_high_index = 0;

	eg_pi->mvdd_low_index =
		(eg_pi->mvdd_high_index == 0) ? 1 : 0;

	module_index = rv770_get_memory_module_index(rdev);

	if (radeon_atom_get_memory_info(rdev, module_index, &memory_info)) {
		pi->mvdd_control = false;
		return 0;
	}

	pi->mvdd_split_frequency =
		cypress_get_mclk_split_point(&memory_info);

	if (pi->mvdd_split_frequency == 0) {
		pi->mvdd_control = false;
		return 0;
	}

	return 0;
}

static int cypress_init_smc_table(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct radeon_ps *radeon_boot_state = rdev->pm.dpm.boot_ps;
	RV770_SMC_STATETABLE *table = &pi->smc_statetable;
	int ret;

	memset(table, 0, sizeof(RV770_SMC_STATETABLE));

	cypress_populate_smc_voltage_tables(rdev, table);

	switch (rdev->pm.int_thermal_type) {
        case THERMAL_TYPE_EVERGREEN:
        case THERMAL_TYPE_EMC2103_WITH_INTERNAL:
		table->thermalProtectType = PPSMC_THERMAL_PROTECT_TYPE_INTERNAL;
		break;
        case THERMAL_TYPE_NONE:
		table->thermalProtectType = PPSMC_THERMAL_PROTECT_TYPE_NONE;
		break;
        default:
		table->thermalProtectType = PPSMC_THERMAL_PROTECT_TYPE_EXTERNAL;
		break;
	}

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_HARDWAREDC)
		table->systemFlags |= PPSMC_SYSTEMFLAG_GPIO_DC;

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_REGULATOR_HOT)
		table->systemFlags |= PPSMC_SYSTEMFLAG_REGULATOR_HOT;

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_STEPVDDC)
		table->systemFlags |= PPSMC_SYSTEMFLAG_STEPVDDC;

	if (pi->mem_gddr5)
		table->systemFlags |= PPSMC_SYSTEMFLAG_GDDR5;

	ret = cypress_populate_smc_initial_state(rdev, radeon_boot_state, table);
	if (ret)
		return ret;

	ret = cypress_populate_smc_acpi_state(rdev, table);
	if (ret)
		return ret;

	table->driverState = table->initialState;

	return rv770_copy_bytes_to_smc(rdev,
				       pi->state_table_start,
				       (u8 *)table, sizeof(RV770_SMC_STATETABLE),
				       pi->sram_end);
}

int cypress_populate_mc_reg_table(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct radeon_ps *radeon_boot_state = rdev->pm.dpm.boot_ps;
	struct rv7xx_ps *boot_state = rv770_get_ps(radeon_boot_state);
	SMC_Evergreen_MCRegisters mc_reg_table = { 0 };

	rv770_write_smc_soft_register(rdev,
				      RV770_SMC_SOFT_REGISTER_seq_index, 1);

	cypress_populate_mc_reg_addresses(rdev, &mc_reg_table);

	cypress_convert_mc_reg_table_entry_to_smc(rdev,
						  &boot_state->low,
						  &mc_reg_table.data[0]);

	cypress_convert_mc_registers(&eg_pi->mc_reg_table.mc_reg_table_entry[0],
				     &mc_reg_table.data[1], eg_pi->mc_reg_table.last,
				     eg_pi->mc_reg_table.valid_flag);

	cypress_convert_mc_reg_table_to_smc(rdev, radeon_boot_state, &mc_reg_table);

	return rv770_copy_bytes_to_smc(rdev, eg_pi->mc_reg_table_start,
				       (u8 *)&mc_reg_table, sizeof(SMC_Evergreen_MCRegisters),
				       pi->sram_end);
}

int cypress_get_table_locations(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	u32 tmp;
	int ret;

	ret = rv770_read_smc_sram_dword(rdev,
					EVERGREEN_SMC_FIRMWARE_HEADER_LOCATION +
					EVERGREEN_SMC_FIRMWARE_HEADER_stateTable,
					&tmp, pi->sram_end);
	if (ret)
		return ret;

	pi->state_table_start = (u16)tmp;

	ret = rv770_read_smc_sram_dword(rdev,
					EVERGREEN_SMC_FIRMWARE_HEADER_LOCATION +
					EVERGREEN_SMC_FIRMWARE_HEADER_softRegisters,
					&tmp, pi->sram_end);
	if (ret)
		return ret;

	pi->soft_regs_start = (u16)tmp;

	ret = rv770_read_smc_sram_dword(rdev,
					EVERGREEN_SMC_FIRMWARE_HEADER_LOCATION +
					EVERGREEN_SMC_FIRMWARE_HEADER_mcRegisterTable,
					&tmp, pi->sram_end);
	if (ret)
		return ret;

	eg_pi->mc_reg_table_start = (u16)tmp;

	return 0;
}

void cypress_enable_display_gap(struct radeon_device *rdev)
{
	u32 tmp = RREG32(CG_DISPLAY_GAP_CNTL);

	tmp &= ~(DISP1_GAP_MASK | DISP2_GAP_MASK);
	tmp |= (DISP1_GAP(R600_PM_DISPLAY_GAP_IGNORE) |
		DISP2_GAP(R600_PM_DISPLAY_GAP_IGNORE));

	tmp &= ~(DISP1_GAP_MCHG_MASK | DISP2_GAP_MCHG_MASK);
	tmp |= (DISP1_GAP_MCHG(R600_PM_DISPLAY_GAP_VBLANK) |
		DISP2_GAP_MCHG(R600_PM_DISPLAY_GAP_IGNORE));
	WREG32(CG_DISPLAY_GAP_CNTL, tmp);
}

static void cypress_program_display_gap(struct radeon_device *rdev)
{
	u32 tmp, pipe;
	int i;

	tmp = RREG32(CG_DISPLAY_GAP_CNTL) & ~(DISP1_GAP_MASK | DISP2_GAP_MASK);
	if (rdev->pm.dpm.new_active_crtc_count > 0)
		tmp |= DISP1_GAP(R600_PM_DISPLAY_GAP_VBLANK_OR_WM);
	else
		tmp |= DISP1_GAP(R600_PM_DISPLAY_GAP_IGNORE);

	if (rdev->pm.dpm.new_active_crtc_count > 1)
		tmp |= DISP2_GAP(R600_PM_DISPLAY_GAP_VBLANK_OR_WM);
	else
		tmp |= DISP2_GAP(R600_PM_DISPLAY_GAP_IGNORE);

	WREG32(CG_DISPLAY_GAP_CNTL, tmp);

	tmp = RREG32(DCCG_DISP_SLOW_SELECT_REG);
	pipe = (tmp & DCCG_DISP1_SLOW_SELECT_MASK) >> DCCG_DISP1_SLOW_SELECT_SHIFT;

	if ((rdev->pm.dpm.new_active_crtc_count > 0) &&
	    (!(rdev->pm.dpm.new_active_crtcs & (1 << pipe)))) {
		/* find the first active crtc */
		for (i = 0; i < rdev->num_crtc; i++) {
			if (rdev->pm.dpm.new_active_crtcs & (1 << i))
				break;
		}
		if (i == rdev->num_crtc)
			pipe = 0;
		else
			pipe = i;

		tmp &= ~DCCG_DISP1_SLOW_SELECT_MASK;
		tmp |= DCCG_DISP1_SLOW_SELECT(pipe);
		WREG32(DCCG_DISP_SLOW_SELECT_REG, tmp);
	}

	cypress_notify_smc_display_change(rdev, rdev->pm.dpm.new_active_crtc_count > 0);
}

void cypress_dpm_setup_asic(struct radeon_device *rdev)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	rv740_read_clock_registers(rdev);
	rv770_read_voltage_smio_registers(rdev);
	rv770_get_max_vddc(rdev);
	rv770_get_memory_type(rdev);

	if (eg_pi->pcie_performance_request)
		eg_pi->pcie_performance_request_registered = false;

	if (eg_pi->pcie_performance_request)
		cypress_advertise_gen2_capability(rdev);

	rv770_get_pcie_gen2_status(rdev);

	rv770_enable_acpi_pm(rdev);
}

int cypress_dpm_enable(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	if (pi->gfx_clock_gating)
		rv770_restore_cgcg(rdev);

	if (rv770_dpm_enabled(rdev))
		return -EINVAL;

	if (pi->voltage_control) {
		rv770_enable_voltage_control(rdev, true);
		cypress_construct_voltage_tables(rdev);
	}

	if (pi->mvdd_control)
		cypress_get_mvdd_configuration(rdev);

	if (eg_pi->dynamic_ac_timing) {
		cypress_set_mc_reg_address_table(rdev);
		cypress_force_mc_use_s0(rdev);
		cypress_initialize_mc_reg_table(rdev);
		cypress_force_mc_use_s1(rdev);
	}

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_BACKBIAS)
		rv770_enable_backbias(rdev, true);

	if (pi->dynamic_ss)
		cypress_enable_spread_spectrum(rdev, true);

	if (pi->thermal_protection)
		rv770_enable_thermal_protection(rdev, true);

	rv770_setup_bsp(rdev);
	rv770_program_git(rdev);
	rv770_program_tp(rdev);
	rv770_program_tpp(rdev);
	rv770_program_sstp(rdev);
	rv770_program_engine_speed_parameters(rdev);
	cypress_enable_display_gap(rdev);
	rv770_program_vc(rdev);

	if (pi->dynamic_pcie_gen2)
		cypress_enable_dynamic_pcie_gen2(rdev, true);

	if (rv770_upload_firmware(rdev))
		return -EINVAL;

	cypress_get_table_locations(rdev);

	if (cypress_init_smc_table(rdev))
		return -EINVAL;

	if (eg_pi->dynamic_ac_timing)
		cypress_populate_mc_reg_table(rdev);

	cypress_program_response_times(rdev);

	r7xx_start_smc(rdev);

	cypress_notify_smc_display_change(rdev, false);

	cypress_enable_sclk_control(rdev, true);

	if (eg_pi->memory_transition)
		cypress_enable_mclk_control(rdev, true);

	cypress_start_dpm(rdev);

	if (pi->gfx_clock_gating)
		cypress_gfx_clock_gating_enable(rdev, true);

	if (pi->mg_clock_gating)
		cypress_mg_clock_gating_enable(rdev, true);

	if (rdev->irq.installed &&
	    r600_is_internal_thermal_sensor(rdev->pm.int_thermal_type)) {
		PPSMC_Result result;

		rv770_set_thermal_temperature_range(rdev, R600_TEMP_RANGE_MIN, R600_TEMP_RANGE_MAX);
		rdev->irq.dpm_thermal = true;
		radeon_irq_set(rdev);
		result = rv770_send_msg_to_smc(rdev, PPSMC_MSG_EnableThermalInterrupt);

		if (result != PPSMC_Result_OK)
			DRM_DEBUG_KMS("Could not enable thermal interrupts.\n");
	}

	rv770_enable_auto_throttle_source(rdev, RADEON_DPM_AUTO_THROTTLE_SRC_THERMAL, true);

	return 0;
}

void cypress_dpm_disable(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	if (!rv770_dpm_enabled(rdev))
		return;

	rv770_clear_vc(rdev);

	if (pi->thermal_protection)
		rv770_enable_thermal_protection(rdev, false);

	if (pi->dynamic_pcie_gen2)
		cypress_enable_dynamic_pcie_gen2(rdev, false);

	if (rdev->irq.installed &&
	    r600_is_internal_thermal_sensor(rdev->pm.int_thermal_type)) {
		rdev->irq.dpm_thermal = false;
		radeon_irq_set(rdev);
	}

	if (pi->gfx_clock_gating)
		cypress_gfx_clock_gating_enable(rdev, false);

	if (pi->mg_clock_gating)
		cypress_mg_clock_gating_enable(rdev, false);

	rv770_stop_dpm(rdev);
	r7xx_stop_smc(rdev);

	cypress_enable_spread_spectrum(rdev, false);

	if (eg_pi->dynamic_ac_timing)
		cypress_force_mc_use_s1(rdev);

	rv770_reset_smio_status(rdev);
}

int cypress_dpm_set_power_state(struct radeon_device *rdev)
{
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);

	rv770_restrict_performance_levels_before_switch(rdev);

	if (eg_pi->pcie_performance_request)
		cypress_notify_link_speed_change_before_state_change(rdev);

	rv770_set_uvd_clock_before_set_eng_clock(rdev);
	rv770_halt_smc(rdev);
	cypress_upload_sw_state(rdev);

	if (eg_pi->dynamic_ac_timing)
		cypress_upload_mc_reg_table(rdev);

	cypress_program_memory_timing_parameters(rdev);

	rv770_resume_smc(rdev);
	rv770_set_sw_state(rdev);
	rv770_set_uvd_clock_after_set_eng_clock(rdev);

	if (eg_pi->pcie_performance_request)
		cypress_notify_link_speed_change_after_state_change(rdev);

	rv770_unrestrict_performance_levels_after_switch(rdev);

	return 0;
}

void cypress_dpm_reset_asic(struct radeon_device *rdev)
{
	rv770_restrict_performance_levels_before_switch(rdev);
	rv770_set_boot_state(rdev);
}

void cypress_dpm_display_configuration_changed(struct radeon_device *rdev)
{
	cypress_program_display_gap(rdev);
}

int cypress_dpm_init(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi;
	struct evergreen_power_info *eg_pi;
	int index = GetIndexIntoMasterTable(DATA, ASIC_InternalSS_Info);
	uint16_t data_offset, size;
	uint8_t frev, crev;
	struct atom_clock_dividers dividers;
	int ret;

	eg_pi = kzalloc(sizeof(struct evergreen_power_info), GFP_KERNEL);
	if (eg_pi == NULL)
		return -ENOMEM;
	rdev->pm.dpm.priv = eg_pi;
	pi = &eg_pi->rv7xx;

	rv770_get_max_vddc(rdev);

	eg_pi->ulv.supported = false;
	pi->acpi_vddc = 0;
	eg_pi->acpi_vddci = 0;
	pi->min_vddc_in_table = 0;
	pi->max_vddc_in_table = 0;

	ret = rv7xx_parse_power_table(rdev);
	if (ret)
		return ret;

	if (rdev->pm.dpm.voltage_response_time == 0)
		rdev->pm.dpm.voltage_response_time = R600_VOLTAGERESPONSETIME_DFLT;
	if (rdev->pm.dpm.backbias_response_time == 0)
		rdev->pm.dpm.backbias_response_time = R600_BACKBIASRESPONSETIME_DFLT;

	ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
					     0, false, &dividers);
	if (ret)
		pi->ref_div = dividers.ref_div + 1;
	else
		pi->ref_div = R600_REFERENCEDIVIDER_DFLT;

	pi->mclk_strobe_mode_threshold = 40000;
	pi->mclk_edc_enable_threshold = 40000;
	eg_pi->mclk_edc_wr_enable_threshold = 40000;

	pi->rlp = RV770_RLP_DFLT;
	pi->rmp = RV770_RMP_DFLT;
	pi->lhp = RV770_LHP_DFLT;
	pi->lmp = RV770_LMP_DFLT;

	pi->voltage_control =
		radeon_atom_is_voltage_gpio(rdev, SET_VOLTAGE_TYPE_ASIC_VDDC);

	pi->mvdd_control =
		radeon_atom_is_voltage_gpio(rdev, SET_VOLTAGE_TYPE_ASIC_MVDDC);

	eg_pi->vddci_control =
		radeon_atom_is_voltage_gpio(rdev, SET_VOLTAGE_TYPE_ASIC_VDDCI);

	if (atom_parse_data_header(rdev->mode_info.atom_context, index, &size,
                                   &frev, &crev, &data_offset)) {
		pi->sclk_ss = true;
		pi->mclk_ss = true;
		pi->dynamic_ss = true;
	} else {
		pi->sclk_ss = false;
		pi->mclk_ss = false;
		pi->dynamic_ss = true;
	}

	pi->asi = RV770_ASI_DFLT;
	pi->pasi = CYPRESS_HASI_DFLT;
	pi->vrc = CYPRESS_VRC_DFLT;

	pi->power_gating = false;

	if ((rdev->family == CHIP_CYPRESS) ||
	    (rdev->family == CHIP_HEMLOCK))
		pi->gfx_clock_gating = false;
	else
		pi->gfx_clock_gating = true;

	pi->mg_clock_gating = true;
	pi->mgcgtssm = true;
	eg_pi->ls_clock_gating = false;
	eg_pi->sclk_deep_sleep = false;

	pi->dynamic_pcie_gen2 = true;

	if (pi->gfx_clock_gating &&
	    (rdev->pm.int_thermal_type != THERMAL_TYPE_NONE))
		pi->thermal_protection = true;
	else
		pi->thermal_protection = false;

	pi->display_gap = true;

	if (rdev->flags & RADEON_IS_MOBILITY)
		pi->dcodt = true;
	else
		pi->dcodt = false;

	pi->ulps = true;

	eg_pi->dynamic_ac_timing = true;
	eg_pi->abm = true;
	eg_pi->mcls = true;
	eg_pi->light_sleep = true;
	eg_pi->memory_transition = true;
#if defined(CONFIG_ACPI)
	eg_pi->pcie_performance_request =
		radeon_acpi_is_pcie_performance_request_supported(rdev);
#else
	eg_pi->pcie_performance_request = false;
#endif

	if ((rdev->family == CHIP_CYPRESS) ||
	    (rdev->family == CHIP_HEMLOCK) ||
	    (rdev->family == CHIP_JUNIPER))
		eg_pi->dll_default_on = true;
	else
		eg_pi->dll_default_on = false;

	eg_pi->sclk_deep_sleep = false;
	pi->mclk_stutter_mode_threshold = 0;

	pi->sram_end = SMC_RAM_END;

	return 0;
}

void cypress_dpm_fini(struct radeon_device *rdev)
{
	int i;

	for (i = 0; i < rdev->pm.dpm.num_ps; i++) {
		kfree(rdev->pm.dpm.ps[i].ps_priv);
	}
	kfree(rdev->pm.dpm.ps);
	kfree(rdev->pm.dpm.priv);
}
