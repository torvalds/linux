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
#include "rv770d.h"
#include "r600_dpm.h"
#include "rv770_dpm.h"
#include "cypress_dpm.h"
#include "atom.h"

#define MC_CG_ARB_FREQ_F0           0x0a
#define MC_CG_ARB_FREQ_F1           0x0b
#define MC_CG_ARB_FREQ_F2           0x0c
#define MC_CG_ARB_FREQ_F3           0x0d

#define MC_CG_SEQ_DRAMCONF_S0       0x05
#define MC_CG_SEQ_DRAMCONF_S1       0x06

#define PCIE_BUS_CLK                10000
#define TCLK                        (PCIE_BUS_CLK / 10)

#define SMC_RAM_END 0xC000

struct rv7xx_ps *rv770_get_ps(struct radeon_ps *rps)
{
	struct rv7xx_ps *ps = rps->ps_priv;

	return ps;
}

struct rv7xx_power_info *rv770_get_pi(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rdev->pm.dpm.priv;

	return pi;
}

struct evergreen_power_info *evergreen_get_pi(struct radeon_device *rdev)
{
	struct evergreen_power_info *pi = rdev->pm.dpm.priv;

	return pi;
}

static void rv770_enable_bif_dynamic_pcie_gen2(struct radeon_device *rdev,
					       bool enable)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u32 tmp;

	tmp = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
	if (enable) {
		tmp &= ~LC_HW_VOLTAGE_IF_CONTROL_MASK;
		tmp |= LC_HW_VOLTAGE_IF_CONTROL(1);
		tmp |= LC_GEN2_EN_STRAP;
	} else {
		if (!pi->boot_in_gen2) {
			tmp &= ~LC_HW_VOLTAGE_IF_CONTROL_MASK;
			tmp &= ~LC_GEN2_EN_STRAP;
		}
	}
	if ((tmp & LC_OTHER_SIDE_EVER_SENT_GEN2) ||
	    (tmp & LC_OTHER_SIDE_SUPPORTS_GEN2))
		WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, tmp);

}

static void rv770_enable_l0s(struct radeon_device *rdev)
{
	u32 tmp;

	tmp = RREG32_PCIE_PORT(PCIE_LC_CNTL) & ~LC_L0S_INACTIVITY_MASK;
	tmp |= LC_L0S_INACTIVITY(3);
	WREG32_PCIE_PORT(PCIE_LC_CNTL, tmp);
}

static void rv770_enable_l1(struct radeon_device *rdev)
{
	u32 tmp;

	tmp = RREG32_PCIE_PORT(PCIE_LC_CNTL);
	tmp &= ~LC_L1_INACTIVITY_MASK;
	tmp |= LC_L1_INACTIVITY(4);
	tmp &= ~LC_PMI_TO_L1_DIS;
	tmp &= ~LC_ASPM_TO_L1_DIS;
	WREG32_PCIE_PORT(PCIE_LC_CNTL, tmp);
}

static void rv770_enable_pll_sleep_in_l1(struct radeon_device *rdev)
{
	u32 tmp;

	tmp = RREG32_PCIE_PORT(PCIE_LC_CNTL) & ~LC_L1_INACTIVITY_MASK;
	tmp |= LC_L1_INACTIVITY(8);
	WREG32_PCIE_PORT(PCIE_LC_CNTL, tmp);

	/* NOTE, this is a PCIE indirect reg, not PCIE PORT */
	tmp = RREG32_PCIE(PCIE_P_CNTL);
	tmp |= P_PLL_PWRDN_IN_L1L23;
	tmp &= ~P_PLL_BUF_PDNB;
	tmp &= ~P_PLL_PDNB;
	tmp |= P_ALLOW_PRX_FRONTEND_SHUTOFF;
	WREG32_PCIE(PCIE_P_CNTL, tmp);
}

static void rv770_gfx_clock_gating_enable(struct radeon_device *rdev,
					  bool enable)
{
	if (enable)
		WREG32_P(SCLK_PWRMGT_CNTL, DYN_GFX_CLK_OFF_EN, ~DYN_GFX_CLK_OFF_EN);
	else {
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~DYN_GFX_CLK_OFF_EN);
		WREG32_P(SCLK_PWRMGT_CNTL, GFX_CLK_FORCE_ON, ~GFX_CLK_FORCE_ON);
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~GFX_CLK_FORCE_ON);
		RREG32(GB_TILING_CONFIG);
	}
}

static void rv770_mg_clock_gating_enable(struct radeon_device *rdev,
					 bool enable)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	if (enable) {
		u32 mgcg_cgtt_local0;

		if (rdev->family == CHIP_RV770)
			mgcg_cgtt_local0 = RV770_MGCGTTLOCAL0_DFLT;
		else
			mgcg_cgtt_local0 = RV7XX_MGCGTTLOCAL0_DFLT;

		WREG32(CG_CGTT_LOCAL_0, mgcg_cgtt_local0);
		WREG32(CG_CGTT_LOCAL_1, (RV770_MGCGTTLOCAL1_DFLT & 0xFFFFCFFF));

		if (pi->mgcgtssm)
			WREG32(CGTS_SM_CTRL_REG, RV770_MGCGCGTSSMCTRL_DFLT);
	} else {
		WREG32(CG_CGTT_LOCAL_0, 0xFFFFFFFF);
		WREG32(CG_CGTT_LOCAL_1, 0xFFFFCFFF);
	}
}

void rv770_restore_cgcg(struct radeon_device *rdev)
{
	bool dpm_en = false, cg_en = false;

	if (RREG32(GENERAL_PWRMGT) & GLOBAL_PWRMGT_EN)
		dpm_en = true;
	if (RREG32(SCLK_PWRMGT_CNTL) & DYN_GFX_CLK_OFF_EN)
		cg_en = true;

	if (dpm_en && !cg_en)
		WREG32_P(SCLK_PWRMGT_CNTL, DYN_GFX_CLK_OFF_EN, ~DYN_GFX_CLK_OFF_EN);
}

static void rv770_start_dpm(struct radeon_device *rdev)
{
	WREG32_P(SCLK_PWRMGT_CNTL, 0, ~SCLK_PWRMGT_OFF);

	WREG32_P(MCLK_PWRMGT_CNTL, 0, ~MPLL_PWRMGT_OFF);

	WREG32_P(GENERAL_PWRMGT, GLOBAL_PWRMGT_EN, ~GLOBAL_PWRMGT_EN);
}

void rv770_stop_dpm(struct radeon_device *rdev)
{
	PPSMC_Result result;

	result = rv770_send_msg_to_smc(rdev, PPSMC_MSG_TwoLevelsDisabled);

	if (result != PPSMC_Result_OK)
		DRM_ERROR("Could not force DPM to low.\n");

	WREG32_P(GENERAL_PWRMGT, 0, ~GLOBAL_PWRMGT_EN);

	WREG32_P(SCLK_PWRMGT_CNTL, SCLK_PWRMGT_OFF, ~SCLK_PWRMGT_OFF);

	WREG32_P(MCLK_PWRMGT_CNTL, MPLL_PWRMGT_OFF, ~MPLL_PWRMGT_OFF);
}

bool rv770_dpm_enabled(struct radeon_device *rdev)
{
	if (RREG32(GENERAL_PWRMGT) & GLOBAL_PWRMGT_EN)
		return true;
	else
		return false;
}

void rv770_enable_thermal_protection(struct radeon_device *rdev,
				     bool enable)
{
	if (enable)
		WREG32_P(GENERAL_PWRMGT, 0, ~THERMAL_PROTECTION_DIS);
	else
		WREG32_P(GENERAL_PWRMGT, THERMAL_PROTECTION_DIS, ~THERMAL_PROTECTION_DIS);
}

void rv770_enable_acpi_pm(struct radeon_device *rdev)
{
	WREG32_P(GENERAL_PWRMGT, STATIC_PM_EN, ~STATIC_PM_EN);
}

u8 rv770_get_seq_value(struct radeon_device *rdev,
		       struct rv7xx_pl *pl)
{
	return (pl->flags & ATOM_PPLIB_R600_FLAGS_LOWPOWER) ?
		MC_CG_SEQ_DRAMCONF_S0 : MC_CG_SEQ_DRAMCONF_S1;
}

int rv770_read_smc_soft_register(struct radeon_device *rdev,
				 u16 reg_offset, u32 *value)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	return rv770_read_smc_sram_dword(rdev,
					 pi->soft_regs_start + reg_offset,
					 value, pi->sram_end);
}

int rv770_write_smc_soft_register(struct radeon_device *rdev,
				  u16 reg_offset, u32 value)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	return rv770_write_smc_sram_dword(rdev,
					  pi->soft_regs_start + reg_offset,
					  value, pi->sram_end);
}

int rv770_populate_smc_t(struct radeon_device *rdev,
			 struct radeon_ps *radeon_state,
			 RV770_SMC_SWSTATE *smc_state)
{
	struct rv7xx_ps *state = rv770_get_ps(radeon_state);
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	int i;
	int a_n;
	int a_d;
	u8 l[RV770_SMC_PERFORMANCE_LEVELS_PER_SWSTATE];
	u8 r[RV770_SMC_PERFORMANCE_LEVELS_PER_SWSTATE];
	u32 a_t;

	l[0] = 0;
	r[2] = 100;

	a_n = (int)state->medium.sclk * pi->lmp +
		(int)state->low.sclk * (R600_AH_DFLT - pi->rlp);
	a_d = (int)state->low.sclk * (100 - (int)pi->rlp) +
		(int)state->medium.sclk * pi->lmp;

	l[1] = (u8)(pi->lmp - (int)pi->lmp * a_n / a_d);
	r[0] = (u8)(pi->rlp + (100 - (int)pi->rlp) * a_n / a_d);

	a_n = (int)state->high.sclk * pi->lhp + (int)state->medium.sclk *
		(R600_AH_DFLT - pi->rmp);
	a_d = (int)state->medium.sclk * (100 - (int)pi->rmp) +
		(int)state->high.sclk * pi->lhp;

	l[2] = (u8)(pi->lhp - (int)pi->lhp * a_n / a_d);
	r[1] = (u8)(pi->rmp + (100 - (int)pi->rmp) * a_n / a_d);

	for (i = 0; i < (RV770_SMC_PERFORMANCE_LEVELS_PER_SWSTATE - 1); i++) {
		a_t = CG_R(r[i] * pi->bsp / 200) | CG_L(l[i] * pi->bsp / 200);
		smc_state->levels[i].aT = cpu_to_be32(a_t);
	}

	a_t = CG_R(r[RV770_SMC_PERFORMANCE_LEVELS_PER_SWSTATE - 1] * pi->pbsp / 200) |
		CG_L(l[RV770_SMC_PERFORMANCE_LEVELS_PER_SWSTATE - 1] * pi->pbsp / 200);

	smc_state->levels[RV770_SMC_PERFORMANCE_LEVELS_PER_SWSTATE - 1].aT =
		cpu_to_be32(a_t);

	return 0;
}

int rv770_populate_smc_sp(struct radeon_device *rdev,
			  struct radeon_ps *radeon_state,
			  RV770_SMC_SWSTATE *smc_state)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	int i;

	for (i = 0; i < (RV770_SMC_PERFORMANCE_LEVELS_PER_SWSTATE - 1); i++)
		smc_state->levels[i].bSP = cpu_to_be32(pi->dsp);

	smc_state->levels[RV770_SMC_PERFORMANCE_LEVELS_PER_SWSTATE - 1].bSP =
		cpu_to_be32(pi->psp);

	return 0;
}

static void rv770_calculate_fractional_mpll_feedback_divider(u32 memory_clock,
							     u32 reference_clock,
							     bool gddr5,
							     struct atom_clock_dividers *dividers,
							     u32 *clkf,
							     u32 *clkfrac)
{
	u32 post_divider, reference_divider, feedback_divider8;
	u32 fyclk;

	if (gddr5)
		fyclk = (memory_clock * 8) / 2;
	else
		fyclk = (memory_clock * 4) / 2;

	post_divider = dividers->post_div;
	reference_divider = dividers->ref_div;

	feedback_divider8 =
		(8 * fyclk * reference_divider * post_divider) / reference_clock;

	*clkf = feedback_divider8 / 8;
	*clkfrac = feedback_divider8 % 8;
}

static int rv770_encode_yclk_post_div(u32 postdiv, u32 *encoded_postdiv)
{
	int ret = 0;

	switch (postdiv) {
        case 1:
		*encoded_postdiv = 0;
		break;
        case 2:
		*encoded_postdiv = 1;
		break;
        case 4:
		*encoded_postdiv = 2;
		break;
        case 8:
		*encoded_postdiv = 3;
		break;
        case 16:
		*encoded_postdiv = 4;
		break;
        default:
		ret = -EINVAL;
		break;
	}

    return ret;
}

u32 rv770_map_clkf_to_ibias(struct radeon_device *rdev, u32 clkf)
{
	if (clkf <= 0x10)
		return 0x4B;
	if (clkf <= 0x19)
		return 0x5B;
	if (clkf <= 0x21)
		return 0x2B;
	if (clkf <= 0x27)
		return 0x6C;
	if (clkf <= 0x31)
		return 0x9D;
	return 0xC6;
}

static int rv770_populate_mclk_value(struct radeon_device *rdev,
				     u32 engine_clock, u32 memory_clock,
				     RV7XX_SMC_MCLK_VALUE *mclk)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u8 encoded_reference_dividers[] = { 0, 16, 17, 20, 21 };
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
	u32 dll_cntl = pi->clk_regs.rv770.dll_cntl;
	struct atom_clock_dividers dividers;
	u32 reference_clock = rdev->clock.mpll.reference_freq;
	u32 clkf, clkfrac;
	u32 postdiv_yclk;
	u32 ibias;
	int ret;

	ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_MEMORY_PLL_PARAM,
					     memory_clock, false, &dividers);
	if (ret)
		return ret;

	if ((dividers.ref_div < 1) || (dividers.ref_div > 5))
		return -EINVAL;

	rv770_calculate_fractional_mpll_feedback_divider(memory_clock, reference_clock,
							 pi->mem_gddr5,
							 &dividers, &clkf, &clkfrac);

	ret = rv770_encode_yclk_post_div(dividers.post_div, &postdiv_yclk);
	if (ret)
		return ret;

	ibias = rv770_map_clkf_to_ibias(rdev, clkf);

	mpll_ad_func_cntl &= ~(CLKR_MASK |
			       YCLK_POST_DIV_MASK |
			       CLKF_MASK |
			       CLKFRAC_MASK |
			       IBIAS_MASK);
	mpll_ad_func_cntl |= CLKR(encoded_reference_dividers[dividers.ref_div - 1]);
	mpll_ad_func_cntl |= YCLK_POST_DIV(postdiv_yclk);
	mpll_ad_func_cntl |= CLKF(clkf);
	mpll_ad_func_cntl |= CLKFRAC(clkfrac);
	mpll_ad_func_cntl |= IBIAS(ibias);

	if (dividers.vco_mode)
		mpll_ad_func_cntl_2 |= VCO_MODE;
	else
		mpll_ad_func_cntl_2 &= ~VCO_MODE;

	if (pi->mem_gddr5) {
		rv770_calculate_fractional_mpll_feedback_divider(memory_clock,
								 reference_clock,
								 pi->mem_gddr5,
								 &dividers, &clkf, &clkfrac);

		ibias = rv770_map_clkf_to_ibias(rdev, clkf);

		ret = rv770_encode_yclk_post_div(dividers.post_div, &postdiv_yclk);
		if (ret)
			return ret;

		mpll_dq_func_cntl &= ~(CLKR_MASK |
				       YCLK_POST_DIV_MASK |
				       CLKF_MASK |
				       CLKFRAC_MASK |
				       IBIAS_MASK);
		mpll_dq_func_cntl |= CLKR(encoded_reference_dividers[dividers.ref_div - 1]);
		mpll_dq_func_cntl |= YCLK_POST_DIV(postdiv_yclk);
		mpll_dq_func_cntl |= CLKF(clkf);
		mpll_dq_func_cntl |= CLKFRAC(clkfrac);
		mpll_dq_func_cntl |= IBIAS(ibias);

		if (dividers.vco_mode)
			mpll_dq_func_cntl_2 |= VCO_MODE;
		else
			mpll_dq_func_cntl_2 &= ~VCO_MODE;
	}

	mclk->mclk770.mclk_value = cpu_to_be32(memory_clock);
	mclk->mclk770.vMPLL_AD_FUNC_CNTL = cpu_to_be32(mpll_ad_func_cntl);
	mclk->mclk770.vMPLL_AD_FUNC_CNTL_2 = cpu_to_be32(mpll_ad_func_cntl_2);
	mclk->mclk770.vMPLL_DQ_FUNC_CNTL = cpu_to_be32(mpll_dq_func_cntl);
	mclk->mclk770.vMPLL_DQ_FUNC_CNTL_2 = cpu_to_be32(mpll_dq_func_cntl_2);
	mclk->mclk770.vMCLK_PWRMGT_CNTL = cpu_to_be32(mclk_pwrmgt_cntl);
	mclk->mclk770.vDLL_CNTL = cpu_to_be32(dll_cntl);

	return 0;
}

static int rv770_populate_sclk_value(struct radeon_device *rdev,
				     u32 engine_clock,
				     RV770_SMC_SCLK_VALUE *sclk)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct atom_clock_dividers dividers;
	u32 spll_func_cntl =
		pi->clk_regs.rv770.cg_spll_func_cntl;
	u32 spll_func_cntl_2 =
		pi->clk_regs.rv770.cg_spll_func_cntl_2;
	u32 spll_func_cntl_3 =
		pi->clk_regs.rv770.cg_spll_func_cntl_3;
	u32 cg_spll_spread_spectrum =
		pi->clk_regs.rv770.cg_spll_spread_spectrum;
	u32 cg_spll_spread_spectrum_2 =
		pi->clk_regs.rv770.cg_spll_spread_spectrum_2;
	u64 tmp;
	u32 reference_clock = rdev->clock.spll.reference_freq;
	u32 reference_divider, post_divider;
	u32 fbdiv;
	int ret;

	ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
					     engine_clock, false, &dividers);
	if (ret)
		return ret;

	reference_divider = 1 + dividers.ref_div;

	if (dividers.enable_post_div)
		post_divider = (0x0f & (dividers.post_div >> 4)) + (0x0f & dividers.post_div) + 2;
	else
		post_divider = 1;

	tmp = (u64) engine_clock * reference_divider * post_divider * 16384;
	do_div(tmp, reference_clock);
	fbdiv = (u32) tmp;

	if (dividers.enable_post_div)
		spll_func_cntl |= SPLL_DIVEN;
	else
		spll_func_cntl &= ~SPLL_DIVEN;
	spll_func_cntl &= ~(SPLL_HILEN_MASK | SPLL_LOLEN_MASK | SPLL_REF_DIV_MASK);
	spll_func_cntl |= SPLL_REF_DIV(dividers.ref_div);
	spll_func_cntl |= SPLL_HILEN((dividers.post_div >> 4) & 0xf);
	spll_func_cntl |= SPLL_LOLEN(dividers.post_div & 0xf);

	spll_func_cntl_2 &= ~SCLK_MUX_SEL_MASK;
	spll_func_cntl_2 |= SCLK_MUX_SEL(2);

	spll_func_cntl_3 &= ~SPLL_FB_DIV_MASK;
	spll_func_cntl_3 |= SPLL_FB_DIV(fbdiv);
	spll_func_cntl_3 |= SPLL_DITHEN;

	if (pi->sclk_ss) {
		struct radeon_atom_ss ss;
		u32 vco_freq = engine_clock * post_divider;

		if (radeon_atombios_get_asic_ss_info(rdev, &ss,
						     ASIC_INTERNAL_ENGINE_SS, vco_freq)) {
			u32 clk_s = reference_clock * 5 / (reference_divider * ss.rate);
			u32 clk_v = ss.percentage * fbdiv / (clk_s * 10000);

			cg_spll_spread_spectrum &= ~CLKS_MASK;
			cg_spll_spread_spectrum |= CLKS(clk_s);
			cg_spll_spread_spectrum |= SSEN;

			cg_spll_spread_spectrum_2 &= ~CLKV_MASK;
			cg_spll_spread_spectrum_2 |= CLKV(clk_v);
		}
	}

	sclk->sclk_value = cpu_to_be32(engine_clock);
	sclk->vCG_SPLL_FUNC_CNTL = cpu_to_be32(spll_func_cntl);
	sclk->vCG_SPLL_FUNC_CNTL_2 = cpu_to_be32(spll_func_cntl_2);
	sclk->vCG_SPLL_FUNC_CNTL_3 = cpu_to_be32(spll_func_cntl_3);
	sclk->vCG_SPLL_SPREAD_SPECTRUM = cpu_to_be32(cg_spll_spread_spectrum);
	sclk->vCG_SPLL_SPREAD_SPECTRUM_2 = cpu_to_be32(cg_spll_spread_spectrum_2);

	return 0;
}

int rv770_populate_vddc_value(struct radeon_device *rdev, u16 vddc,
			      RV770_SMC_VOLTAGE_VALUE *voltage)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	int i;

	if (!pi->voltage_control) {
		voltage->index = 0;
		voltage->value = 0;
		return 0;
	}

	for (i = 0; i < pi->valid_vddc_entries; i++) {
		if (vddc <= pi->vddc_table[i].vddc) {
			voltage->index = pi->vddc_table[i].vddc_index;
			voltage->value = cpu_to_be16(vddc);
			break;
		}
	}

	if (i == pi->valid_vddc_entries)
		return -EINVAL;

	return 0;
}

int rv770_populate_mvdd_value(struct radeon_device *rdev, u32 mclk,
			      RV770_SMC_VOLTAGE_VALUE *voltage)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	if (!pi->mvdd_control) {
		voltage->index = MVDD_HIGH_INDEX;
		voltage->value = cpu_to_be16(MVDD_HIGH_VALUE);
		return 0;
	}

	if (mclk <= pi->mvdd_split_frequency) {
		voltage->index = MVDD_LOW_INDEX;
		voltage->value = cpu_to_be16(MVDD_LOW_VALUE);
	} else {
		voltage->index = MVDD_HIGH_INDEX;
		voltage->value = cpu_to_be16(MVDD_HIGH_VALUE);
	}

	return 0;
}

static int rv770_convert_power_level_to_smc(struct radeon_device *rdev,
					    struct rv7xx_pl *pl,
					    RV770_SMC_HW_PERFORMANCE_LEVEL *level,
					    u8 watermark_level)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	int ret;

	level->gen2PCIE = pi->pcie_gen2 ?
		((pl->flags & ATOM_PPLIB_R600_FLAGS_PCIEGEN2) ? 1 : 0) : 0;
	level->gen2XSP  = (pl->flags & ATOM_PPLIB_R600_FLAGS_PCIEGEN2) ? 1 : 0;
	level->backbias = (pl->flags & ATOM_PPLIB_R600_FLAGS_BACKBIASENABLE) ? 1 : 0;
	level->displayWatermark = watermark_level;

	if (rdev->family == CHIP_RV740)
		ret = rv740_populate_sclk_value(rdev, pl->sclk,
						&level->sclk);
	else if ((rdev->family == CHIP_RV730) || (rdev->family == CHIP_RV710))
		ret = rv730_populate_sclk_value(rdev, pl->sclk,
						&level->sclk);
	else
		ret = rv770_populate_sclk_value(rdev, pl->sclk,
						&level->sclk);
	if (ret)
		return ret;

	if (rdev->family == CHIP_RV740) {
		if (pi->mem_gddr5) {
			if (pl->mclk <= pi->mclk_strobe_mode_threshold)
				level->strobeMode =
					rv740_get_mclk_frequency_ratio(pl->mclk) | 0x10;
			else
				level->strobeMode = 0;

			if (pl->mclk > pi->mclk_edc_enable_threshold)
				level->mcFlags = SMC_MC_EDC_RD_FLAG | SMC_MC_EDC_WR_FLAG;
			else
				level->mcFlags =  0;
		}
		ret = rv740_populate_mclk_value(rdev, pl->sclk,
						pl->mclk, &level->mclk);
	} else if ((rdev->family == CHIP_RV730) || (rdev->family == CHIP_RV710))
		ret = rv730_populate_mclk_value(rdev, pl->sclk,
						pl->mclk, &level->mclk);
	else
		ret = rv770_populate_mclk_value(rdev, pl->sclk,
						pl->mclk, &level->mclk);
	if (ret)
		return ret;

	ret = rv770_populate_vddc_value(rdev, pl->vddc,
					&level->vddc);
	if (ret)
		return ret;

	ret = rv770_populate_mvdd_value(rdev, pl->mclk, &level->mvdd);

	return ret;
}

static int rv770_convert_power_state_to_smc(struct radeon_device *rdev,
					    struct radeon_ps *radeon_state,
					    RV770_SMC_SWSTATE *smc_state)
{
	struct rv7xx_ps *state = rv770_get_ps(radeon_state);
	int ret;

	if (!(radeon_state->caps & ATOM_PPLIB_DISALLOW_ON_DC))
		smc_state->flags |= PPSMC_SWSTATE_FLAG_DC;

	ret = rv770_convert_power_level_to_smc(rdev,
					       &state->low,
					       &smc_state->levels[0],
					       PPSMC_DISPLAY_WATERMARK_LOW);
	if (ret)
		return ret;

	ret = rv770_convert_power_level_to_smc(rdev,
					       &state->medium,
					       &smc_state->levels[1],
					       PPSMC_DISPLAY_WATERMARK_LOW);
	if (ret)
		return ret;

	ret = rv770_convert_power_level_to_smc(rdev,
					       &state->high,
					       &smc_state->levels[2],
					       PPSMC_DISPLAY_WATERMARK_HIGH);
	if (ret)
		return ret;

	smc_state->levels[0].arbValue = MC_CG_ARB_FREQ_F1;
	smc_state->levels[1].arbValue = MC_CG_ARB_FREQ_F2;
	smc_state->levels[2].arbValue = MC_CG_ARB_FREQ_F3;

	smc_state->levels[0].seqValue = rv770_get_seq_value(rdev,
							    &state->low);
	smc_state->levels[1].seqValue = rv770_get_seq_value(rdev,
							    &state->medium);
	smc_state->levels[2].seqValue = rv770_get_seq_value(rdev,
							    &state->high);

	rv770_populate_smc_sp(rdev, radeon_state, smc_state);

	return rv770_populate_smc_t(rdev, radeon_state, smc_state);

}

u32 rv770_calculate_memory_refresh_rate(struct radeon_device *rdev,
					u32 engine_clock)
{
	u32 dram_rows;
	u32 dram_refresh_rate;
	u32 mc_arb_rfsh_rate;
	u32 tmp;

	tmp = (RREG32(MC_ARB_RAMCFG) & NOOFROWS_MASK) >> NOOFROWS_SHIFT;
	dram_rows = 1 << (tmp + 10);
	tmp = RREG32(MC_SEQ_MISC0) & 3;
	dram_refresh_rate = 1 << (tmp + 3);
	mc_arb_rfsh_rate = ((engine_clock * 10) * dram_refresh_rate / dram_rows - 32) / 64;

	return mc_arb_rfsh_rate;
}

static void rv770_program_memory_timing_parameters(struct radeon_device *rdev,
						   struct radeon_ps *radeon_state)
{
	struct rv7xx_ps *state = rv770_get_ps(radeon_state);
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u32 sqm_ratio;
	u32 arb_refresh_rate;
	u32 high_clock;

	if (state->high.sclk < (state->low.sclk * 0xFF / 0x40))
		high_clock = state->high.sclk;
	else
		high_clock = (state->low.sclk * 0xFF / 0x40);

	radeon_atom_set_engine_dram_timings(rdev, high_clock,
					    state->high.mclk);

	sqm_ratio =
		STATE0(64 * high_clock / pi->boot_sclk) |
		STATE1(64 * high_clock / state->low.sclk) |
		STATE2(64 * high_clock / state->medium.sclk) |
		STATE3(64 * high_clock / state->high.sclk);
	WREG32(MC_ARB_SQM_RATIO, sqm_ratio);

	arb_refresh_rate =
		POWERMODE0(rv770_calculate_memory_refresh_rate(rdev, pi->boot_sclk)) |
		POWERMODE1(rv770_calculate_memory_refresh_rate(rdev, state->low.sclk)) |
		POWERMODE2(rv770_calculate_memory_refresh_rate(rdev, state->medium.sclk)) |
		POWERMODE3(rv770_calculate_memory_refresh_rate(rdev, state->high.sclk));
	WREG32(MC_ARB_RFSH_RATE, arb_refresh_rate);
}

void rv770_enable_backbias(struct radeon_device *rdev,
			   bool enable)
{
	if (enable)
		WREG32_P(GENERAL_PWRMGT, BACKBIAS_PAD_EN, ~BACKBIAS_PAD_EN);
	else
		WREG32_P(GENERAL_PWRMGT, 0, ~(BACKBIAS_VALUE | BACKBIAS_PAD_EN));
}

static void rv770_enable_spread_spectrum(struct radeon_device *rdev,
					 bool enable)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	if (enable) {
		if (pi->sclk_ss)
			WREG32_P(GENERAL_PWRMGT, DYN_SPREAD_SPECTRUM_EN, ~DYN_SPREAD_SPECTRUM_EN);

		if (pi->mclk_ss) {
			if (rdev->family == CHIP_RV740)
				rv740_enable_mclk_spread_spectrum(rdev, true);
		}
	} else {
		WREG32_P(CG_SPLL_SPREAD_SPECTRUM, 0, ~SSEN);

		WREG32_P(GENERAL_PWRMGT, 0, ~DYN_SPREAD_SPECTRUM_EN);

		WREG32_P(CG_MPLL_SPREAD_SPECTRUM, 0, ~SSEN);

		if (rdev->family == CHIP_RV740)
			rv740_enable_mclk_spread_spectrum(rdev, false);
	}
}

static void rv770_program_mpll_timing_parameters(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	if ((rdev->family == CHIP_RV770) && !pi->mem_gddr5) {
		WREG32(MPLL_TIME,
		       (MPLL_LOCK_TIME(R600_MPLLLOCKTIME_DFLT * pi->ref_div) |
			MPLL_RESET_TIME(R600_MPLLRESETTIME_DFLT)));
	}
}

void rv770_setup_bsp(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u32 xclk = radeon_get_xclk(rdev);

	r600_calculate_u_and_p(pi->asi,
			       xclk,
			       16,
			       &pi->bsp,
			       &pi->bsu);

	r600_calculate_u_and_p(pi->pasi,
			       xclk,
			       16,
			       &pi->pbsp,
			       &pi->pbsu);

	pi->dsp = BSP(pi->bsp) | BSU(pi->bsu);
	pi->psp = BSP(pi->pbsp) | BSU(pi->pbsu);

	WREG32(CG_BSP, pi->dsp);

}

void rv770_program_git(struct radeon_device *rdev)
{
	WREG32_P(CG_GIT, CG_GICST(R600_GICST_DFLT), ~CG_GICST_MASK);
}

void rv770_program_tp(struct radeon_device *rdev)
{
	int i;
	enum r600_td td = R600_TD_DFLT;

	for (i = 0; i < R600_PM_NUMBER_OF_TC; i++)
		WREG32(CG_FFCT_0 + (i * 4), (UTC_0(r600_utc[i]) | DTC_0(r600_dtc[i])));

	if (td == R600_TD_AUTO)
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~FIR_FORCE_TREND_SEL);
	else
		WREG32_P(SCLK_PWRMGT_CNTL, FIR_FORCE_TREND_SEL, ~FIR_FORCE_TREND_SEL);
	if (td == R600_TD_UP)
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~FIR_TREND_MODE);
	if (td == R600_TD_DOWN)
		WREG32_P(SCLK_PWRMGT_CNTL, FIR_TREND_MODE, ~FIR_TREND_MODE);
}

void rv770_program_tpp(struct radeon_device *rdev)
{
	WREG32(CG_TPC, R600_TPC_DFLT);
}

void rv770_program_sstp(struct radeon_device *rdev)
{
	WREG32(CG_SSP, (SSTU(R600_SSTU_DFLT) | SST(R600_SST_DFLT)));
}

void rv770_program_engine_speed_parameters(struct radeon_device *rdev)
{
	WREG32_P(SPLL_CNTL_MODE, SPLL_DIV_SYNC, ~SPLL_DIV_SYNC);
}

static void rv770_enable_display_gap(struct radeon_device *rdev)
{
	u32 tmp = RREG32(CG_DISPLAY_GAP_CNTL);

	tmp &= ~(DISP1_GAP_MCHG_MASK | DISP2_GAP_MCHG_MASK);
	tmp |= (DISP1_GAP_MCHG(R600_PM_DISPLAY_GAP_IGNORE) |
		DISP2_GAP_MCHG(R600_PM_DISPLAY_GAP_IGNORE));
	WREG32(CG_DISPLAY_GAP_CNTL, tmp);
}

void rv770_program_vc(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	WREG32(CG_FTV, pi->vrc);
}

void rv770_clear_vc(struct radeon_device *rdev)
{
	WREG32(CG_FTV, 0);
}

int rv770_upload_firmware(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	int ret;

	rv770_reset_smc(rdev);
	rv770_stop_smc_clock(rdev);

	ret = rv770_load_smc_ucode(rdev, pi->sram_end);
	if (ret)
		return ret;

	return 0;
}

static int rv770_populate_smc_acpi_state(struct radeon_device *rdev,
					 RV770_SMC_STATETABLE *table)
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
	u32 spll_func_cntl =
		pi->clk_regs.rv770.cg_spll_func_cntl;
	u32 spll_func_cntl_2 =
		pi->clk_regs.rv770.cg_spll_func_cntl_2;
	u32 spll_func_cntl_3 =
		pi->clk_regs.rv770.cg_spll_func_cntl_3;
	u32 mclk_pwrmgt_cntl;
	u32 dll_cntl;

	table->ACPIState = table->initialState;

	table->ACPIState.flags &= ~PPSMC_SWSTATE_FLAG_DC;

	if (pi->acpi_vddc) {
		rv770_populate_vddc_value(rdev, pi->acpi_vddc,
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
		rv770_populate_vddc_value(rdev, pi->min_vddc_in_table,
					  &table->ACPIState.levels[0].vddc);
		table->ACPIState.levels[0].gen2PCIE = 0;
	}


	mpll_ad_func_cntl_2 |= BIAS_GEN_PDNB | RESET_EN;

	mpll_dq_func_cntl_2 |= BIAS_GEN_PDNB | RESET_EN;

	mclk_pwrmgt_cntl = (MRDCKA0_RESET |
			    MRDCKA1_RESET |
			    MRDCKB0_RESET |
			    MRDCKB1_RESET |
			    MRDCKC0_RESET |
			    MRDCKC1_RESET |
			    MRDCKD0_RESET |
			    MRDCKD1_RESET);

	dll_cntl = 0xff000000;

	spll_func_cntl |= SPLL_RESET | SPLL_SLEEP | SPLL_BYPASS_EN;

	spll_func_cntl_2 &= ~SCLK_MUX_SEL_MASK;
	spll_func_cntl_2 |= SCLK_MUX_SEL(4);

	table->ACPIState.levels[0].mclk.mclk770.vMPLL_AD_FUNC_CNTL = cpu_to_be32(mpll_ad_func_cntl);
	table->ACPIState.levels[0].mclk.mclk770.vMPLL_AD_FUNC_CNTL_2 = cpu_to_be32(mpll_ad_func_cntl_2);
	table->ACPIState.levels[0].mclk.mclk770.vMPLL_DQ_FUNC_CNTL = cpu_to_be32(mpll_dq_func_cntl);
	table->ACPIState.levels[0].mclk.mclk770.vMPLL_DQ_FUNC_CNTL_2 = cpu_to_be32(mpll_dq_func_cntl_2);

	table->ACPIState.levels[0].mclk.mclk770.vMCLK_PWRMGT_CNTL = cpu_to_be32(mclk_pwrmgt_cntl);
	table->ACPIState.levels[0].mclk.mclk770.vDLL_CNTL = cpu_to_be32(dll_cntl);

	table->ACPIState.levels[0].mclk.mclk770.mclk_value = 0;

	table->ACPIState.levels[0].sclk.vCG_SPLL_FUNC_CNTL = cpu_to_be32(spll_func_cntl);
	table->ACPIState.levels[0].sclk.vCG_SPLL_FUNC_CNTL_2 = cpu_to_be32(spll_func_cntl_2);
	table->ACPIState.levels[0].sclk.vCG_SPLL_FUNC_CNTL_3 = cpu_to_be32(spll_func_cntl_3);

	table->ACPIState.levels[0].sclk.sclk_value = 0;

	rv770_populate_mvdd_value(rdev, 0, &table->ACPIState.levels[0].mvdd);

	table->ACPIState.levels[1] = table->ACPIState.levels[0];
	table->ACPIState.levels[2] = table->ACPIState.levels[0];

	return 0;
}

int rv770_populate_initial_mvdd_value(struct radeon_device *rdev,
				      RV770_SMC_VOLTAGE_VALUE *voltage)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	if ((pi->s0_vid_lower_smio_cntl & pi->mvdd_mask_low) ==
	     (pi->mvdd_low_smio[MVDD_LOW_INDEX] & pi->mvdd_mask_low) ) {
		voltage->index = MVDD_LOW_INDEX;
		voltage->value = cpu_to_be16(MVDD_LOW_VALUE);
	} else {
		voltage->index = MVDD_HIGH_INDEX;
		voltage->value = cpu_to_be16(MVDD_HIGH_VALUE);
	}

	return 0;
}

static int rv770_populate_smc_initial_state(struct radeon_device *rdev,
					    struct radeon_ps *radeon_state,
					    RV770_SMC_STATETABLE *table)
{
	struct rv7xx_ps *initial_state = rv770_get_ps(radeon_state);
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
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

	table->initialState.levels[0].seqValue =
		rv770_get_seq_value(rdev, &initial_state->low);

	rv770_populate_vddc_value(rdev,
				  initial_state->low.vddc,
				  &table->initialState.levels[0].vddc);
	rv770_populate_initial_mvdd_value(rdev,
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

	if (rdev->family == CHIP_RV740) {
		if (pi->mem_gddr5) {
			if (initial_state->low.mclk <= pi->mclk_strobe_mode_threshold)
				table->initialState.levels[0].strobeMode =
					rv740_get_mclk_frequency_ratio(initial_state->low.mclk) | 0x10;
			else
				table->initialState.levels[0].strobeMode = 0;

			if (initial_state->low.mclk >= pi->mclk_edc_enable_threshold)
				table->initialState.levels[0].mcFlags = SMC_MC_EDC_RD_FLAG | SMC_MC_EDC_WR_FLAG;
			else
				table->initialState.levels[0].mcFlags =  0;
		}
	}

	table->initialState.levels[1] = table->initialState.levels[0];
	table->initialState.levels[2] = table->initialState.levels[0];

	table->initialState.flags |= PPSMC_SWSTATE_FLAG_DC;

	return 0;
}

static int rv770_populate_smc_vddc_table(struct radeon_device *rdev,
					 RV770_SMC_STATETABLE *table)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	int i;

	for (i = 0; i < pi->valid_vddc_entries; i++) {
		table->highSMIO[pi->vddc_table[i].vddc_index] =
			pi->vddc_table[i].high_smio;
		table->lowSMIO[pi->vddc_table[i].vddc_index] =
			cpu_to_be32(pi->vddc_table[i].low_smio);
	}

	table->voltageMaskTable.highMask[RV770_SMC_VOLTAGEMASK_VDDC] = 0;
	table->voltageMaskTable.lowMask[RV770_SMC_VOLTAGEMASK_VDDC] =
		cpu_to_be32(pi->vddc_mask_low);

	for (i = 0;
	     ((i < pi->valid_vddc_entries) &&
	      (pi->max_vddc_in_table >
	       pi->vddc_table[i].vddc));
	     i++);

	table->maxVDDCIndexInPPTable =
		pi->vddc_table[i].vddc_index;

	return 0;
}

static int rv770_populate_smc_mvdd_table(struct radeon_device *rdev,
					 RV770_SMC_STATETABLE *table)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	if (pi->mvdd_control) {
		table->lowSMIO[MVDD_HIGH_INDEX] |=
			cpu_to_be32(pi->mvdd_low_smio[MVDD_HIGH_INDEX]);
		table->lowSMIO[MVDD_LOW_INDEX] |=
			cpu_to_be32(pi->mvdd_low_smio[MVDD_LOW_INDEX]);

		table->voltageMaskTable.highMask[RV770_SMC_VOLTAGEMASK_MVDD] = 0;
		table->voltageMaskTable.lowMask[RV770_SMC_VOLTAGEMASK_MVDD] =
			cpu_to_be32(pi->mvdd_mask_low);
	}

	return 0;
}

static int rv770_init_smc_table(struct radeon_device *rdev,
				struct radeon_ps *radeon_boot_state)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct rv7xx_ps *boot_state = rv770_get_ps(radeon_boot_state);
	RV770_SMC_STATETABLE *table = &pi->smc_statetable;
	int ret;

	memset(table, 0, sizeof(RV770_SMC_STATETABLE));

	pi->boot_sclk = boot_state->low.sclk;

	rv770_populate_smc_vddc_table(rdev, table);
	rv770_populate_smc_mvdd_table(rdev, table);

	switch (rdev->pm.int_thermal_type) {
        case THERMAL_TYPE_RV770:
        case THERMAL_TYPE_ADT7473_WITH_INTERNAL:
		table->thermalProtectType = PPSMC_THERMAL_PROTECT_TYPE_INTERNAL;
		break;
        case THERMAL_TYPE_NONE:
		table->thermalProtectType = PPSMC_THERMAL_PROTECT_TYPE_NONE;
		break;
        case THERMAL_TYPE_EXTERNAL_GPIO:
        default:
		table->thermalProtectType = PPSMC_THERMAL_PROTECT_TYPE_EXTERNAL;
		break;
	}

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_HARDWAREDC) {
		table->systemFlags |= PPSMC_SYSTEMFLAG_GPIO_DC;

		if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_DONT_WAIT_FOR_VBLANK_ON_ALERT)
			table->extraFlags |= PPSMC_EXTRAFLAGS_AC2DC_DONT_WAIT_FOR_VBLANK;

		if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_GOTO_BOOT_ON_ALERT)
			table->extraFlags |= PPSMC_EXTRAFLAGS_AC2DC_ACTION_GOTOINITIALSTATE;
	}

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_STEPVDDC)
		table->systemFlags |= PPSMC_SYSTEMFLAG_STEPVDDC;

	if (pi->mem_gddr5)
		table->systemFlags |= PPSMC_SYSTEMFLAG_GDDR5;

	if ((rdev->family == CHIP_RV730) || (rdev->family == CHIP_RV710))
		ret = rv730_populate_smc_initial_state(rdev, radeon_boot_state, table);
	else
		ret = rv770_populate_smc_initial_state(rdev, radeon_boot_state, table);
	if (ret)
		return ret;

	if (rdev->family == CHIP_RV740)
		ret = rv740_populate_smc_acpi_state(rdev, table);
	else if ((rdev->family == CHIP_RV730) || (rdev->family == CHIP_RV710))
		ret = rv730_populate_smc_acpi_state(rdev, table);
	else
		ret = rv770_populate_smc_acpi_state(rdev, table);
	if (ret)
		return ret;

	table->driverState = table->initialState;

	return rv770_copy_bytes_to_smc(rdev,
				       pi->state_table_start,
				       (const u8 *)table,
				       sizeof(RV770_SMC_STATETABLE),
				       pi->sram_end);
}

static int rv770_construct_vddc_table(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u16 min, max, step;
	u32 steps = 0;
	u8 vddc_index = 0;
	u32 i;

	radeon_atom_get_min_voltage(rdev, SET_VOLTAGE_TYPE_ASIC_VDDC, &min);
	radeon_atom_get_max_voltage(rdev, SET_VOLTAGE_TYPE_ASIC_VDDC, &max);
	radeon_atom_get_voltage_step(rdev, SET_VOLTAGE_TYPE_ASIC_VDDC, &step);

	steps = (max - min) / step + 1;

	if (steps > MAX_NO_VREG_STEPS)
		return -EINVAL;

	for (i = 0; i < steps; i++) {
		u32 gpio_pins, gpio_mask;

		pi->vddc_table[i].vddc = (u16)(min + i * step);
		radeon_atom_get_voltage_gpio_settings(rdev,
						      pi->vddc_table[i].vddc,
						      SET_VOLTAGE_TYPE_ASIC_VDDC,
						      &gpio_pins, &gpio_mask);
		pi->vddc_table[i].low_smio = gpio_pins & gpio_mask;
		pi->vddc_table[i].high_smio = 0;
		pi->vddc_mask_low = gpio_mask;
		if (i > 0) {
			if ((pi->vddc_table[i].low_smio !=
			     pi->vddc_table[i - 1].low_smio ) ||
			     (pi->vddc_table[i].high_smio !=
			      pi->vddc_table[i - 1].high_smio))
				vddc_index++;
		}
		pi->vddc_table[i].vddc_index = vddc_index;
	}

	pi->valid_vddc_entries = (u8)steps;

	return 0;
}

static u32 rv770_get_mclk_split_point(struct atom_memory_info *memory_info)
{
	if (memory_info->mem_type == MEM_TYPE_GDDR3)
		return 30000;

	return 0;
}

static int rv770_get_mvdd_pin_configuration(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u32 gpio_pins, gpio_mask;

	radeon_atom_get_voltage_gpio_settings(rdev,
					      MVDD_HIGH_VALUE, SET_VOLTAGE_TYPE_ASIC_MVDDC,
					      &gpio_pins, &gpio_mask);
	pi->mvdd_mask_low = gpio_mask;
	pi->mvdd_low_smio[MVDD_HIGH_INDEX] =
		gpio_pins & gpio_mask;

	radeon_atom_get_voltage_gpio_settings(rdev,
					      MVDD_LOW_VALUE, SET_VOLTAGE_TYPE_ASIC_MVDDC,
					      &gpio_pins, &gpio_mask);
	pi->mvdd_low_smio[MVDD_LOW_INDEX] =
		gpio_pins & gpio_mask;

	return 0;
}

u8 rv770_get_memory_module_index(struct radeon_device *rdev)
{
	return (u8) ((RREG32(BIOS_SCRATCH_4) >> 16) & 0xff);
}

static int rv770_get_mvdd_configuration(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u8 memory_module_index;
	struct atom_memory_info memory_info;

	memory_module_index = rv770_get_memory_module_index(rdev);

	if (radeon_atom_get_memory_info(rdev, memory_module_index, &memory_info)) {
		pi->mvdd_control = false;
		return 0;
	}

	pi->mvdd_split_frequency =
		rv770_get_mclk_split_point(&memory_info);

	if (pi->mvdd_split_frequency == 0) {
		pi->mvdd_control = false;
		return 0;
	}

	return rv770_get_mvdd_pin_configuration(rdev);
}

void rv770_enable_voltage_control(struct radeon_device *rdev,
				  bool enable)
{
	if (enable)
		WREG32_P(GENERAL_PWRMGT, VOLT_PWRMGT_EN, ~VOLT_PWRMGT_EN);
	else
		WREG32_P(GENERAL_PWRMGT, 0, ~VOLT_PWRMGT_EN);
}

static void rv770_program_display_gap(struct radeon_device *rdev)
{
	u32 tmp = RREG32(CG_DISPLAY_GAP_CNTL);

	tmp &= ~(DISP1_GAP_MCHG_MASK | DISP2_GAP_MCHG_MASK);
	if (RREG32(AVIVO_D1CRTC_CONTROL) & AVIVO_CRTC_EN) {
		tmp |= DISP1_GAP_MCHG(R600_PM_DISPLAY_GAP_VBLANK);
		tmp |= DISP2_GAP_MCHG(R600_PM_DISPLAY_GAP_IGNORE);
	} else if (RREG32(AVIVO_D2CRTC_CONTROL) & AVIVO_CRTC_EN) {
		tmp |= DISP1_GAP_MCHG(R600_PM_DISPLAY_GAP_IGNORE);
		tmp |= DISP2_GAP_MCHG(R600_PM_DISPLAY_GAP_VBLANK);
	} else {
		tmp |= DISP1_GAP_MCHG(R600_PM_DISPLAY_GAP_IGNORE);
		tmp |= DISP2_GAP_MCHG(R600_PM_DISPLAY_GAP_IGNORE);
	}
	WREG32(CG_DISPLAY_GAP_CNTL, tmp);
}

static void rv770_enable_dynamic_pcie_gen2(struct radeon_device *rdev,
					   bool enable)
{
	rv770_enable_bif_dynamic_pcie_gen2(rdev, enable);

	if (enable)
		WREG32_P(GENERAL_PWRMGT, ENABLE_GEN2PCIE, ~ENABLE_GEN2PCIE);
	else
		WREG32_P(GENERAL_PWRMGT, 0, ~ENABLE_GEN2PCIE);
}

static void r7xx_program_memory_timing_parameters(struct radeon_device *rdev,
						  struct radeon_ps *radeon_new_state)
{
	if ((rdev->family == CHIP_RV730) ||
	    (rdev->family == CHIP_RV710) ||
	    (rdev->family == CHIP_RV740))
		rv730_program_memory_timing_parameters(rdev, radeon_new_state);
	else
		rv770_program_memory_timing_parameters(rdev, radeon_new_state);
}

static int rv770_upload_sw_state(struct radeon_device *rdev,
				 struct radeon_ps *radeon_new_state)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u16 address = pi->state_table_start +
		offsetof(RV770_SMC_STATETABLE, driverState);
	RV770_SMC_SWSTATE state = { 0 };
	int ret;

	ret = rv770_convert_power_state_to_smc(rdev, radeon_new_state, &state);
	if (ret)
		return ret;

	return rv770_copy_bytes_to_smc(rdev, address, (const u8 *)&state,
				       sizeof(RV770_SMC_SWSTATE),
				       pi->sram_end);
}

int rv770_halt_smc(struct radeon_device *rdev)
{
	if (rv770_send_msg_to_smc(rdev, PPSMC_MSG_Halt) != PPSMC_Result_OK)
		return -EINVAL;

	if (rv770_wait_for_smc_inactive(rdev) != PPSMC_Result_OK)
		return -EINVAL;

	return 0;
}

int rv770_resume_smc(struct radeon_device *rdev)
{
	if (rv770_send_msg_to_smc(rdev, PPSMC_MSG_Resume) != PPSMC_Result_OK)
		return -EINVAL;
	return 0;
}

int rv770_set_sw_state(struct radeon_device *rdev)
{
	if (rv770_send_msg_to_smc(rdev, PPSMC_MSG_SwitchToSwState) != PPSMC_Result_OK)
		return -EINVAL;
	return 0;
}

int rv770_set_boot_state(struct radeon_device *rdev)
{
	if (rv770_send_msg_to_smc(rdev, PPSMC_MSG_SwitchToInitialState) != PPSMC_Result_OK)
		return -EINVAL;
	return 0;
}

void rv770_set_uvd_clock_before_set_eng_clock(struct radeon_device *rdev,
					      struct radeon_ps *new_ps,
					      struct radeon_ps *old_ps)
{
	struct rv7xx_ps *new_state = rv770_get_ps(new_ps);
	struct rv7xx_ps *current_state = rv770_get_ps(old_ps);

	if ((new_ps->vclk == old_ps->vclk) &&
	    (new_ps->dclk == old_ps->dclk))
		return;

	if (new_state->high.sclk >= current_state->high.sclk)
		return;

	radeon_set_uvd_clocks(rdev, new_ps->vclk, old_ps->dclk);
}

void rv770_set_uvd_clock_after_set_eng_clock(struct radeon_device *rdev,
					     struct radeon_ps *new_ps,
					     struct radeon_ps *old_ps)
{
	struct rv7xx_ps *new_state = rv770_get_ps(new_ps);
	struct rv7xx_ps *current_state = rv770_get_ps(old_ps);

	if ((new_ps->vclk == old_ps->vclk) &&
	    (new_ps->dclk == old_ps->dclk))
		return;

	if (new_state->high.sclk < current_state->high.sclk)
		return;

	radeon_set_uvd_clocks(rdev, new_ps->vclk, new_ps->dclk);
}

int rv770_restrict_performance_levels_before_switch(struct radeon_device *rdev)
{
	if (rv770_send_msg_to_smc(rdev, (PPSMC_Msg)(PPSMC_MSG_NoForcedLevel)) != PPSMC_Result_OK)
		return -EINVAL;

	if (rv770_send_msg_to_smc(rdev, (PPSMC_Msg)(PPSMC_MSG_TwoLevelsDisabled)) != PPSMC_Result_OK)
		return -EINVAL;

	return 0;
}

int rv770_unrestrict_performance_levels_after_switch(struct radeon_device *rdev)
{
	if (rv770_send_msg_to_smc(rdev, (PPSMC_Msg)(PPSMC_MSG_NoForcedLevel)) != PPSMC_Result_OK)
		return -EINVAL;

	if (rv770_send_msg_to_smc(rdev, (PPSMC_Msg)(PPSMC_MSG_ZeroLevelsDisabled)) != PPSMC_Result_OK)
		return -EINVAL;

	return 0;
}

void r7xx_start_smc(struct radeon_device *rdev)
{
	rv770_start_smc(rdev);
	rv770_start_smc_clock(rdev);
}


void r7xx_stop_smc(struct radeon_device *rdev)
{
	rv770_reset_smc(rdev);
	rv770_stop_smc_clock(rdev);
}

static void rv770_read_clock_registers(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	pi->clk_regs.rv770.cg_spll_func_cntl =
		RREG32(CG_SPLL_FUNC_CNTL);
	pi->clk_regs.rv770.cg_spll_func_cntl_2 =
		RREG32(CG_SPLL_FUNC_CNTL_2);
	pi->clk_regs.rv770.cg_spll_func_cntl_3 =
		RREG32(CG_SPLL_FUNC_CNTL_3);
	pi->clk_regs.rv770.cg_spll_spread_spectrum =
		RREG32(CG_SPLL_SPREAD_SPECTRUM);
	pi->clk_regs.rv770.cg_spll_spread_spectrum_2 =
		RREG32(CG_SPLL_SPREAD_SPECTRUM_2);
	pi->clk_regs.rv770.mpll_ad_func_cntl =
		RREG32(MPLL_AD_FUNC_CNTL);
	pi->clk_regs.rv770.mpll_ad_func_cntl_2 =
		RREG32(MPLL_AD_FUNC_CNTL_2);
	pi->clk_regs.rv770.mpll_dq_func_cntl =
		RREG32(MPLL_DQ_FUNC_CNTL);
	pi->clk_regs.rv770.mpll_dq_func_cntl_2 =
		RREG32(MPLL_DQ_FUNC_CNTL_2);
	pi->clk_regs.rv770.mclk_pwrmgt_cntl =
		RREG32(MCLK_PWRMGT_CNTL);
	pi->clk_regs.rv770.dll_cntl = RREG32(DLL_CNTL);
}

static void r7xx_read_clock_registers(struct radeon_device *rdev)
{
	if (rdev->family == CHIP_RV740)
		rv740_read_clock_registers(rdev);
	else if ((rdev->family == CHIP_RV730) || (rdev->family == CHIP_RV710))
		rv730_read_clock_registers(rdev);
	else
		rv770_read_clock_registers(rdev);
}

void rv770_read_voltage_smio_registers(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	pi->s0_vid_lower_smio_cntl =
		RREG32(S0_VID_LOWER_SMIO_CNTL);
}

void rv770_reset_smio_status(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u32 sw_smio_index, vid_smio_cntl;

	sw_smio_index =
		(RREG32(GENERAL_PWRMGT) & SW_SMIO_INDEX_MASK) >> SW_SMIO_INDEX_SHIFT;
	switch (sw_smio_index) {
        case 3:
		vid_smio_cntl = RREG32(S3_VID_LOWER_SMIO_CNTL);
		break;
        case 2:
		vid_smio_cntl = RREG32(S2_VID_LOWER_SMIO_CNTL);
		break;
        case 1:
		vid_smio_cntl = RREG32(S1_VID_LOWER_SMIO_CNTL);
		break;
        case 0:
		return;
        default:
		vid_smio_cntl = pi->s0_vid_lower_smio_cntl;
		break;
	}

	WREG32(S0_VID_LOWER_SMIO_CNTL, vid_smio_cntl);
	WREG32_P(GENERAL_PWRMGT, SW_SMIO_INDEX(0), ~SW_SMIO_INDEX_MASK);
}

void rv770_get_memory_type(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u32 tmp;

	tmp = RREG32(MC_SEQ_MISC0);

	if (((tmp & MC_SEQ_MISC0_GDDR5_MASK) >> MC_SEQ_MISC0_GDDR5_SHIFT) ==
	    MC_SEQ_MISC0_GDDR5_VALUE)
		pi->mem_gddr5 = true;
	else
		pi->mem_gddr5 = false;

}

void rv770_get_pcie_gen2_status(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u32 tmp;

	tmp = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);

	if ((tmp & LC_OTHER_SIDE_EVER_SENT_GEN2) &&
	    (tmp & LC_OTHER_SIDE_SUPPORTS_GEN2))
		pi->pcie_gen2 = true;
	else
		pi->pcie_gen2 = false;

	if (pi->pcie_gen2) {
		if (tmp & LC_CURRENT_DATA_RATE)
			pi->boot_in_gen2 = true;
		else
			pi->boot_in_gen2 = false;
	} else
		pi->boot_in_gen2 = false;
}

#if 0
static int rv770_enter_ulp_state(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	if (pi->gfx_clock_gating) {
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~DYN_GFX_CLK_OFF_EN);
		WREG32_P(SCLK_PWRMGT_CNTL, GFX_CLK_FORCE_ON, ~GFX_CLK_FORCE_ON);
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~GFX_CLK_FORCE_ON);
		RREG32(GB_TILING_CONFIG);
	}

	WREG32_P(SMC_MSG, HOST_SMC_MSG(PPSMC_MSG_SwitchToMinimumPower),
		 ~HOST_SMC_MSG_MASK);

	udelay(7000);

	return 0;
}

static int rv770_exit_ulp_state(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	int i;

	WREG32_P(SMC_MSG, HOST_SMC_MSG(PPSMC_MSG_ResumeFromMinimumPower),
		 ~HOST_SMC_MSG_MASK);

	udelay(7000);

	for (i = 0; i < rdev->usec_timeout; i++) {
		if (((RREG32(SMC_MSG) & HOST_SMC_RESP_MASK) >> HOST_SMC_RESP_SHIFT) == 1)
			break;
		udelay(1000);
	}

	if (pi->gfx_clock_gating)
		WREG32_P(SCLK_PWRMGT_CNTL, DYN_GFX_CLK_OFF_EN, ~DYN_GFX_CLK_OFF_EN);

	return 0;
}
#endif

static void rv770_get_mclk_odt_threshold(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u8 memory_module_index;
	struct atom_memory_info memory_info;

	pi->mclk_odt_threshold = 0;

	if ((rdev->family == CHIP_RV730) || (rdev->family == CHIP_RV710)) {
		memory_module_index = rv770_get_memory_module_index(rdev);

		if (radeon_atom_get_memory_info(rdev, memory_module_index, &memory_info))
			return;

		if (memory_info.mem_type == MEM_TYPE_DDR2 ||
		    memory_info.mem_type == MEM_TYPE_DDR3)
			pi->mclk_odt_threshold = 30000;
	}
}

void rv770_get_max_vddc(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u16 vddc;

	if (radeon_atom_get_max_vddc(rdev, 0, 0, &vddc))
		pi->max_vddc = 0;
	else
		pi->max_vddc = vddc;
}

void rv770_program_response_times(struct radeon_device *rdev)
{
	u32 voltage_response_time, backbias_response_time;
	u32 acpi_delay_time, vbi_time_out;
	u32 vddc_dly, bb_dly, acpi_dly, vbi_dly;
	u32 reference_clock;

	voltage_response_time = (u32)rdev->pm.dpm.voltage_response_time;
	backbias_response_time = (u32)rdev->pm.dpm.backbias_response_time;

	if (voltage_response_time == 0)
		voltage_response_time = 1000;

	if (backbias_response_time == 0)
		backbias_response_time = 1000;

	acpi_delay_time = 15000;
	vbi_time_out = 100000;

	reference_clock = radeon_get_xclk(rdev);

	vddc_dly = (voltage_response_time  * reference_clock) / 1600;
	bb_dly = (backbias_response_time * reference_clock) / 1600;
	acpi_dly = (acpi_delay_time * reference_clock) / 1600;
	vbi_dly = (vbi_time_out * reference_clock) / 1600;

	rv770_write_smc_soft_register(rdev,
				      RV770_SMC_SOFT_REGISTER_delay_vreg, vddc_dly);
	rv770_write_smc_soft_register(rdev,
				      RV770_SMC_SOFT_REGISTER_delay_bbias, bb_dly);
	rv770_write_smc_soft_register(rdev,
				      RV770_SMC_SOFT_REGISTER_delay_acpi, acpi_dly);
	rv770_write_smc_soft_register(rdev,
				      RV770_SMC_SOFT_REGISTER_mclk_chg_timeout, vbi_dly);
#if 0
	/* XXX look up hw revision */
	if (WEKIVA_A21)
		rv770_write_smc_soft_register(rdev,
					      RV770_SMC_SOFT_REGISTER_baby_step_timer,
					      0x10);
#endif
}

static void rv770_program_dcodt_before_state_switch(struct radeon_device *rdev,
						    struct radeon_ps *radeon_new_state,
						    struct radeon_ps *radeon_current_state)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct rv7xx_ps *new_state = rv770_get_ps(radeon_new_state);
	struct rv7xx_ps *current_state = rv770_get_ps(radeon_current_state);
	bool current_use_dc = false;
	bool new_use_dc = false;

	if (pi->mclk_odt_threshold == 0)
		return;

	if (current_state->high.mclk <= pi->mclk_odt_threshold)
		current_use_dc = true;

	if (new_state->high.mclk <= pi->mclk_odt_threshold)
		new_use_dc = true;

	if (current_use_dc == new_use_dc)
		return;

	if (!current_use_dc && new_use_dc)
		return;

	if ((rdev->family == CHIP_RV730) || (rdev->family == CHIP_RV710))
		rv730_program_dcodt(rdev, new_use_dc);
}

static void rv770_program_dcodt_after_state_switch(struct radeon_device *rdev,
						   struct radeon_ps *radeon_new_state,
						   struct radeon_ps *radeon_current_state)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct rv7xx_ps *new_state = rv770_get_ps(radeon_new_state);
	struct rv7xx_ps *current_state = rv770_get_ps(radeon_current_state);
	bool current_use_dc = false;
	bool new_use_dc = false;

	if (pi->mclk_odt_threshold == 0)
		return;

	if (current_state->high.mclk <= pi->mclk_odt_threshold)
		current_use_dc = true;

	if (new_state->high.mclk <= pi->mclk_odt_threshold)
		new_use_dc = true;

	if (current_use_dc == new_use_dc)
		return;

	if (current_use_dc && !new_use_dc)
		return;

	if ((rdev->family == CHIP_RV730) || (rdev->family == CHIP_RV710))
		rv730_program_dcodt(rdev, new_use_dc);
}

static void rv770_retrieve_odt_values(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	if (pi->mclk_odt_threshold == 0)
		return;

	if ((rdev->family == CHIP_RV730) || (rdev->family == CHIP_RV710))
		rv730_get_odt_values(rdev);
}

static void rv770_set_dpm_event_sources(struct radeon_device *rdev, u32 sources)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	bool want_thermal_protection;
	enum radeon_dpm_event_src dpm_event_src;

	switch (sources) {
        case 0:
        default:
		want_thermal_protection = false;
		break;
        case (1 << RADEON_DPM_AUTO_THROTTLE_SRC_THERMAL):
		want_thermal_protection = true;
		dpm_event_src = RADEON_DPM_EVENT_SRC_DIGITAL;
		break;

        case (1 << RADEON_DPM_AUTO_THROTTLE_SRC_EXTERNAL):
		want_thermal_protection = true;
		dpm_event_src = RADEON_DPM_EVENT_SRC_EXTERNAL;
		break;

        case ((1 << RADEON_DPM_AUTO_THROTTLE_SRC_EXTERNAL) |
	      (1 << RADEON_DPM_AUTO_THROTTLE_SRC_THERMAL)):
		want_thermal_protection = true;
		dpm_event_src = RADEON_DPM_EVENT_SRC_DIGIAL_OR_EXTERNAL;
		break;
	}

	if (want_thermal_protection) {
		WREG32_P(CG_THERMAL_CTRL, DPM_EVENT_SRC(dpm_event_src), ~DPM_EVENT_SRC_MASK);
		if (pi->thermal_protection)
			WREG32_P(GENERAL_PWRMGT, 0, ~THERMAL_PROTECTION_DIS);
	} else {
		WREG32_P(GENERAL_PWRMGT, THERMAL_PROTECTION_DIS, ~THERMAL_PROTECTION_DIS);
	}
}

void rv770_enable_auto_throttle_source(struct radeon_device *rdev,
				       enum radeon_dpm_auto_throttle_src source,
				       bool enable)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	if (enable) {
		if (!(pi->active_auto_throttle_sources & (1 << source))) {
			pi->active_auto_throttle_sources |= 1 << source;
			rv770_set_dpm_event_sources(rdev, pi->active_auto_throttle_sources);
		}
	} else {
		if (pi->active_auto_throttle_sources & (1 << source)) {
			pi->active_auto_throttle_sources &= ~(1 << source);
			rv770_set_dpm_event_sources(rdev, pi->active_auto_throttle_sources);
		}
	}
}

int rv770_set_thermal_temperature_range(struct radeon_device *rdev,
					int min_temp, int max_temp)
{
	int low_temp = 0 * 1000;
	int high_temp = 255 * 1000;

	if (low_temp < min_temp)
		low_temp = min_temp;
	if (high_temp > max_temp)
		high_temp = max_temp;
	if (high_temp < low_temp) {
		DRM_ERROR("invalid thermal range: %d - %d\n", low_temp, high_temp);
		return -EINVAL;
	}

	WREG32_P(CG_THERMAL_INT, DIG_THERM_INTH(high_temp / 1000), ~DIG_THERM_INTH_MASK);
	WREG32_P(CG_THERMAL_INT, DIG_THERM_INTL(low_temp / 1000), ~DIG_THERM_INTL_MASK);
	WREG32_P(CG_THERMAL_CTRL, DIG_THERM_DPM(high_temp / 1000), ~DIG_THERM_DPM_MASK);

	rdev->pm.dpm.thermal.min_temp = low_temp;
	rdev->pm.dpm.thermal.max_temp = high_temp;

	return 0;
}

int rv770_dpm_enable(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct radeon_ps *boot_ps = rdev->pm.dpm.boot_ps;
	int ret;

	if (pi->gfx_clock_gating)
		rv770_restore_cgcg(rdev);

	if (rv770_dpm_enabled(rdev))
		return -EINVAL;

	if (pi->voltage_control) {
		rv770_enable_voltage_control(rdev, true);
		ret = rv770_construct_vddc_table(rdev);
		if (ret) {
			DRM_ERROR("rv770_construct_vddc_table failed\n");
			return ret;
		}
	}

	if (pi->dcodt)
		rv770_retrieve_odt_values(rdev);

	if (pi->mvdd_control) {
		ret = rv770_get_mvdd_configuration(rdev);
		if (ret) {
			DRM_ERROR("rv770_get_mvdd_configuration failed\n");
			return ret;
		}
	}

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_BACKBIAS)
		rv770_enable_backbias(rdev, true);

	rv770_enable_spread_spectrum(rdev, true);

	if (pi->thermal_protection)
		rv770_enable_thermal_protection(rdev, true);

	rv770_program_mpll_timing_parameters(rdev);
	rv770_setup_bsp(rdev);
	rv770_program_git(rdev);
	rv770_program_tp(rdev);
	rv770_program_tpp(rdev);
	rv770_program_sstp(rdev);
	rv770_program_engine_speed_parameters(rdev);
	rv770_enable_display_gap(rdev);
	rv770_program_vc(rdev);

	if (pi->dynamic_pcie_gen2)
		rv770_enable_dynamic_pcie_gen2(rdev, true);

	ret = rv770_upload_firmware(rdev);
	if (ret) {
		DRM_ERROR("rv770_upload_firmware failed\n");
		return ret;
	}
	ret = rv770_init_smc_table(rdev, boot_ps);
	if (ret) {
		DRM_ERROR("rv770_init_smc_table failed\n");
		return ret;
	}

	rv770_program_response_times(rdev);
	r7xx_start_smc(rdev);

	if ((rdev->family == CHIP_RV730) || (rdev->family == CHIP_RV710))
		rv730_start_dpm(rdev);
	else
		rv770_start_dpm(rdev);

	if (pi->gfx_clock_gating)
		rv770_gfx_clock_gating_enable(rdev, true);

	if (pi->mg_clock_gating)
		rv770_mg_clock_gating_enable(rdev, true);

	if (rdev->irq.installed &&
	    r600_is_internal_thermal_sensor(rdev->pm.int_thermal_type)) {
		PPSMC_Result result;

		ret = rv770_set_thermal_temperature_range(rdev, R600_TEMP_RANGE_MIN, R600_TEMP_RANGE_MAX);
		if (ret)
			return ret;
		rdev->irq.dpm_thermal = true;
		radeon_irq_set(rdev);
		result = rv770_send_msg_to_smc(rdev, PPSMC_MSG_EnableThermalInterrupt);

		if (result != PPSMC_Result_OK)
			DRM_DEBUG_KMS("Could not enable thermal interrupts.\n");
	}

	rv770_enable_auto_throttle_source(rdev, RADEON_DPM_AUTO_THROTTLE_SRC_THERMAL, true);

	return 0;
}

void rv770_dpm_disable(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	if (!rv770_dpm_enabled(rdev))
		return;

	rv770_clear_vc(rdev);

	if (pi->thermal_protection)
		rv770_enable_thermal_protection(rdev, false);

	rv770_enable_spread_spectrum(rdev, false);

	if (pi->dynamic_pcie_gen2)
		rv770_enable_dynamic_pcie_gen2(rdev, false);

	if (rdev->irq.installed &&
	    r600_is_internal_thermal_sensor(rdev->pm.int_thermal_type)) {
		rdev->irq.dpm_thermal = false;
		radeon_irq_set(rdev);
	}

	if (pi->gfx_clock_gating)
		rv770_gfx_clock_gating_enable(rdev, false);

	if (pi->mg_clock_gating)
		rv770_mg_clock_gating_enable(rdev, false);

	if ((rdev->family == CHIP_RV730) || (rdev->family == CHIP_RV710))
		rv730_stop_dpm(rdev);
	else
		rv770_stop_dpm(rdev);

	r7xx_stop_smc(rdev);
	rv770_reset_smio_status(rdev);
}

int rv770_dpm_set_power_state(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct radeon_ps *new_ps = rdev->pm.dpm.requested_ps;
	struct radeon_ps *old_ps = rdev->pm.dpm.current_ps;
	int ret;

	ret = rv770_restrict_performance_levels_before_switch(rdev);
	if (ret) {
		DRM_ERROR("rv770_restrict_performance_levels_before_switch failed\n");
		return ret;
	}
	rv770_set_uvd_clock_before_set_eng_clock(rdev, new_ps, old_ps);
	ret = rv770_halt_smc(rdev);
	if (ret) {
		DRM_ERROR("rv770_halt_smc failed\n");
		return ret;
	}
	ret = rv770_upload_sw_state(rdev, new_ps);
	if (ret) {
		DRM_ERROR("rv770_upload_sw_state failed\n");
		return ret;
	}
	r7xx_program_memory_timing_parameters(rdev, new_ps);
	if (pi->dcodt)
		rv770_program_dcodt_before_state_switch(rdev, new_ps, old_ps);
	ret = rv770_resume_smc(rdev);
	if (ret) {
		DRM_ERROR("rv770_resume_smc failed\n");
		return ret;
	}
	ret = rv770_set_sw_state(rdev);
	if (ret) {
		DRM_ERROR("rv770_set_sw_state failed\n");
		return ret;
	}
	if (pi->dcodt)
		rv770_program_dcodt_after_state_switch(rdev, new_ps, old_ps);
	rv770_set_uvd_clock_after_set_eng_clock(rdev, new_ps, old_ps);
	ret = rv770_unrestrict_performance_levels_after_switch(rdev);
	if (ret)
		return ret;

	return 0;
}

void rv770_dpm_reset_asic(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct radeon_ps *boot_ps = rdev->pm.dpm.boot_ps;

	rv770_restrict_performance_levels_before_switch(rdev);
	if (pi->dcodt)
		rv770_program_dcodt_before_state_switch(rdev, boot_ps, boot_ps);
	rv770_set_boot_state(rdev);
	if (pi->dcodt)
		rv770_program_dcodt_after_state_switch(rdev, boot_ps, boot_ps);
}

void rv770_dpm_setup_asic(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	r7xx_read_clock_registers(rdev);
	rv770_read_voltage_smio_registers(rdev);
	rv770_get_memory_type(rdev);
	if (pi->dcodt)
		rv770_get_mclk_odt_threshold(rdev);
	rv770_get_pcie_gen2_status(rdev);

	rv770_enable_acpi_pm(rdev);

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_ASPM_L0s)
		rv770_enable_l0s(rdev);
	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_ASPM_L1)
		rv770_enable_l1(rdev);
	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_TURNOFFPLL_ASPML1)
		rv770_enable_pll_sleep_in_l1(rdev);
}

void rv770_dpm_display_configuration_changed(struct radeon_device *rdev)
{
	rv770_program_display_gap(rdev);
}

union power_info {
	struct _ATOM_POWERPLAY_INFO info;
	struct _ATOM_POWERPLAY_INFO_V2 info_2;
	struct _ATOM_POWERPLAY_INFO_V3 info_3;
	struct _ATOM_PPLIB_POWERPLAYTABLE pplib;
	struct _ATOM_PPLIB_POWERPLAYTABLE2 pplib2;
	struct _ATOM_PPLIB_POWERPLAYTABLE3 pplib3;
};

union pplib_clock_info {
	struct _ATOM_PPLIB_R600_CLOCK_INFO r600;
	struct _ATOM_PPLIB_RS780_CLOCK_INFO rs780;
	struct _ATOM_PPLIB_EVERGREEN_CLOCK_INFO evergreen;
	struct _ATOM_PPLIB_SUMO_CLOCK_INFO sumo;
};

union pplib_power_state {
	struct _ATOM_PPLIB_STATE v1;
	struct _ATOM_PPLIB_STATE_V2 v2;
};

static void rv7xx_parse_pplib_non_clock_info(struct radeon_device *rdev,
					     struct radeon_ps *rps,
					     struct _ATOM_PPLIB_NONCLOCK_INFO *non_clock_info,
					     u8 table_rev)
{
	rps->caps = le32_to_cpu(non_clock_info->ulCapsAndSettings);
	rps->class = le16_to_cpu(non_clock_info->usClassification);
	rps->class2 = le16_to_cpu(non_clock_info->usClassification2);

	if (ATOM_PPLIB_NONCLOCKINFO_VER1 < table_rev) {
		rps->vclk = le32_to_cpu(non_clock_info->ulVCLK);
		rps->dclk = le32_to_cpu(non_clock_info->ulDCLK);
	} else if (r600_is_uvd_state(rps->class, rps->class2)) {
		rps->vclk = RV770_DEFAULT_VCLK_FREQ;
		rps->dclk = RV770_DEFAULT_DCLK_FREQ;
	} else {
		rps->vclk = 0;
		rps->dclk = 0;
	}

	if (rps->class & ATOM_PPLIB_CLASSIFICATION_BOOT)
		rdev->pm.dpm.boot_ps = rps;
	if (rps->class & ATOM_PPLIB_CLASSIFICATION_UVDSTATE)
		rdev->pm.dpm.uvd_ps = rps;
}

static void rv7xx_parse_pplib_clock_info(struct radeon_device *rdev,
					 struct radeon_ps *rps, int index,
					 union pplib_clock_info *clock_info)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct evergreen_power_info *eg_pi = evergreen_get_pi(rdev);
	struct rv7xx_ps *ps = rv770_get_ps(rps);
	u32 sclk, mclk;
	u16 vddc;
	struct rv7xx_pl *pl;

	switch (index) {
	case 0:
		pl = &ps->low;
		break;
	case 1:
		pl = &ps->medium;
		break;
	case 2:
	default:
		pl = &ps->high;
		break;
	}

	if (rdev->family >= CHIP_CEDAR) {
		sclk = le16_to_cpu(clock_info->evergreen.usEngineClockLow);
		sclk |= clock_info->evergreen.ucEngineClockHigh << 16;
		mclk = le16_to_cpu(clock_info->evergreen.usMemoryClockLow);
		mclk |= clock_info->evergreen.ucMemoryClockHigh << 16;

		pl->vddc = le16_to_cpu(clock_info->evergreen.usVDDC);
		pl->vddci = le16_to_cpu(clock_info->evergreen.usVDDCI);
		pl->flags = le32_to_cpu(clock_info->evergreen.ulFlags);
	} else {
		sclk = le16_to_cpu(clock_info->r600.usEngineClockLow);
		sclk |= clock_info->r600.ucEngineClockHigh << 16;
		mclk = le16_to_cpu(clock_info->r600.usMemoryClockLow);
		mclk |= clock_info->r600.ucMemoryClockHigh << 16;

		pl->vddc = le16_to_cpu(clock_info->r600.usVDDC);
		pl->flags = le32_to_cpu(clock_info->r600.ulFlags);
	}

	pl->mclk = mclk;
	pl->sclk = sclk;

	/* patch up vddc if necessary */
	if (pl->vddc == 0xff01) {
		if (radeon_atom_get_max_vddc(rdev, 0, 0, &vddc) == 0)
			pl->vddc = vddc;
	}

	if (rps->class & ATOM_PPLIB_CLASSIFICATION_ACPI) {
		pi->acpi_vddc = pl->vddc;
		if (rdev->family >= CHIP_CEDAR)
			eg_pi->acpi_vddci = pl->vddci;
		if (ps->low.flags & ATOM_PPLIB_R600_FLAGS_PCIEGEN2)
			pi->acpi_pcie_gen2 = true;
		else
			pi->acpi_pcie_gen2 = false;
	}

	if (rps->class2 & ATOM_PPLIB_CLASSIFICATION2_ULV) {
		if (rdev->family >= CHIP_BARTS) {
			eg_pi->ulv.supported = true;
			eg_pi->ulv.pl = pl;
		}
	}

	if (pi->min_vddc_in_table > pl->vddc)
		pi->min_vddc_in_table = pl->vddc;

	if (pi->max_vddc_in_table < pl->vddc)
		pi->max_vddc_in_table = pl->vddc;

	/* patch up boot state */
	if (rps->class & ATOM_PPLIB_CLASSIFICATION_BOOT) {
		u16 vddc, vddci, mvdd;
		radeon_atombios_get_default_voltages(rdev, &vddc, &vddci, &mvdd);
		pl->mclk = rdev->clock.default_mclk;
		pl->sclk = rdev->clock.default_sclk;
		pl->vddc = vddc;
		pl->vddci = vddci;
	}

	if (rdev->family >= CHIP_BARTS) {
		if ((rps->class & ATOM_PPLIB_CLASSIFICATION_UI_MASK) ==
		    ATOM_PPLIB_CLASSIFICATION_UI_PERFORMANCE) {
			rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac.sclk = pl->sclk;
			rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac.mclk = pl->mclk;
			rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac.vddc = pl->vddc;
			rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac.vddci = pl->vddci;
		}
	}
}

int rv7xx_parse_power_table(struct radeon_device *rdev)
{
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	struct _ATOM_PPLIB_NONCLOCK_INFO *non_clock_info;
	union pplib_power_state *power_state;
	int i, j;
	union pplib_clock_info *clock_info;
	union power_info *power_info;
	int index = GetIndexIntoMasterTable(DATA, PowerPlayInfo);
        u16 data_offset;
	u8 frev, crev;
	struct rv7xx_ps *ps;

	if (!atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset))
		return -EINVAL;
	power_info = (union power_info *)(mode_info->atom_context->bios + data_offset);

	rdev->pm.dpm.ps = kzalloc(sizeof(struct radeon_ps) *
				  power_info->pplib.ucNumStates, GFP_KERNEL);
	if (!rdev->pm.dpm.ps)
		return -ENOMEM;
	rdev->pm.dpm.platform_caps = le32_to_cpu(power_info->pplib.ulPlatformCaps);
	rdev->pm.dpm.backbias_response_time = le16_to_cpu(power_info->pplib.usBackbiasTime);
	rdev->pm.dpm.voltage_response_time = le16_to_cpu(power_info->pplib.usVoltageTime);

	for (i = 0; i < power_info->pplib.ucNumStates; i++) {
		power_state = (union pplib_power_state *)
			(mode_info->atom_context->bios + data_offset +
			 le16_to_cpu(power_info->pplib.usStateArrayOffset) +
			 i * power_info->pplib.ucStateEntrySize);
		non_clock_info = (struct _ATOM_PPLIB_NONCLOCK_INFO *)
			(mode_info->atom_context->bios + data_offset +
			 le16_to_cpu(power_info->pplib.usNonClockInfoArrayOffset) +
			 (power_state->v1.ucNonClockStateIndex *
			  power_info->pplib.ucNonClockSize));
		if (power_info->pplib.ucStateEntrySize - 1) {
			ps = kzalloc(sizeof(struct rv7xx_ps), GFP_KERNEL);
			if (ps == NULL) {
				kfree(rdev->pm.dpm.ps);
				return -ENOMEM;
			}
			rdev->pm.dpm.ps[i].ps_priv = ps;
			rv7xx_parse_pplib_non_clock_info(rdev, &rdev->pm.dpm.ps[i],
							 non_clock_info,
							 power_info->pplib.ucNonClockSize);
			for (j = 0; j < (power_info->pplib.ucStateEntrySize - 1); j++) {
				clock_info = (union pplib_clock_info *)
					(mode_info->atom_context->bios + data_offset +
					 le16_to_cpu(power_info->pplib.usClockInfoArrayOffset) +
					 (power_state->v1.ucClockStateIndices[j] *
					  power_info->pplib.ucClockInfoSize));
				rv7xx_parse_pplib_clock_info(rdev,
							     &rdev->pm.dpm.ps[i], j,
							     clock_info);
			}
		}
	}
	rdev->pm.dpm.num_ps = power_info->pplib.ucNumStates;
	return 0;
}

int rv770_dpm_init(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi;
	int index = GetIndexIntoMasterTable(DATA, ASIC_InternalSS_Info);
	uint16_t data_offset, size;
	uint8_t frev, crev;
	struct atom_clock_dividers dividers;
	int ret;

	pi = kzalloc(sizeof(struct rv7xx_power_info), GFP_KERNEL);
	if (pi == NULL)
		return -ENOMEM;
	rdev->pm.dpm.priv = pi;

	rv770_get_max_vddc(rdev);

	pi->acpi_vddc = 0;
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

	pi->mclk_strobe_mode_threshold = 30000;
	pi->mclk_edc_enable_threshold = 30000;

	pi->rlp = RV770_RLP_DFLT;
	pi->rmp = RV770_RMP_DFLT;
	pi->lhp = RV770_LHP_DFLT;
	pi->lmp = RV770_LMP_DFLT;

	pi->voltage_control =
		radeon_atom_is_voltage_gpio(rdev, SET_VOLTAGE_TYPE_ASIC_VDDC, 0);

	pi->mvdd_control =
		radeon_atom_is_voltage_gpio(rdev, SET_VOLTAGE_TYPE_ASIC_MVDDC, 0);

	if (atom_parse_data_header(rdev->mode_info.atom_context, index, &size,
                                   &frev, &crev, &data_offset)) {
		pi->sclk_ss = true;
		pi->mclk_ss = true;
		pi->dynamic_ss = true;
	} else {
		pi->sclk_ss = false;
		pi->mclk_ss = false;
		pi->dynamic_ss = false;
	}

	pi->asi = RV770_ASI_DFLT;
	pi->pasi = RV770_HASI_DFLT;
	pi->vrc = RV770_VRC_DFLT;

	pi->power_gating = false;

	pi->gfx_clock_gating = true;

	pi->mg_clock_gating = true;
	pi->mgcgtssm = true;

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

	pi->mclk_stutter_mode_threshold = 0;

	pi->sram_end = SMC_RAM_END;
	pi->state_table_start = RV770_SMC_TABLE_ADDRESS;
	pi->soft_regs_start = RV770_SMC_SOFT_REGISTERS_START;

	return 0;
}

void rv770_dpm_print_power_state(struct radeon_device *rdev,
				 struct radeon_ps *rps)
{
	struct rv7xx_ps *ps = rv770_get_ps(rps);
	struct rv7xx_pl *pl;

	r600_dpm_print_class_info(rps->class, rps->class2);
	r600_dpm_print_cap_info(rps->caps);
	printk("\tuvd    vclk: %d dclk: %d\n", rps->vclk, rps->dclk);
	if (rdev->family >= CHIP_CEDAR) {
		pl = &ps->low;
		printk("\t\tpower level 0    sclk: %u mclk: %u vddc: %u vddci: %u\n",
		       pl->sclk, pl->mclk, pl->vddc, pl->vddci);
		pl = &ps->medium;
		printk("\t\tpower level 1    sclk: %u mclk: %u vddc: %u vddci: %u\n",
		       pl->sclk, pl->mclk, pl->vddc, pl->vddci);
		pl = &ps->high;
		printk("\t\tpower level 2    sclk: %u mclk: %u vddc: %u vddci: %u\n",
		       pl->sclk, pl->mclk, pl->vddc, pl->vddci);
	} else {
		pl = &ps->low;
		printk("\t\tpower level 0    sclk: %u mclk: %u vddc: %u\n",
		       pl->sclk, pl->mclk, pl->vddc);
		pl = &ps->medium;
		printk("\t\tpower level 1    sclk: %u mclk: %u vddc: %u\n",
		       pl->sclk, pl->mclk, pl->vddc);
		pl = &ps->high;
		printk("\t\tpower level 2    sclk: %u mclk: %u vddc: %u\n",
		       pl->sclk, pl->mclk, pl->vddc);
	}
	r600_dpm_print_ps_status(rdev, rps);
}

void rv770_dpm_fini(struct radeon_device *rdev)
{
	int i;

	for (i = 0; i < rdev->pm.dpm.num_ps; i++) {
		kfree(rdev->pm.dpm.ps[i].ps_priv);
	}
	kfree(rdev->pm.dpm.ps);
	kfree(rdev->pm.dpm.priv);
}

u32 rv770_dpm_get_sclk(struct radeon_device *rdev, bool low)
{
	struct rv7xx_ps *requested_state = rv770_get_ps(rdev->pm.dpm.requested_ps);

	if (low)
		return requested_state->low.sclk;
	else
		return requested_state->high.sclk;
}

u32 rv770_dpm_get_mclk(struct radeon_device *rdev, bool low)
{
	struct rv7xx_ps *requested_state = rv770_get_ps(rdev->pm.dpm.requested_ps);

	if (low)
		return requested_state->low.mclk;
	else
		return requested_state->high.mclk;
}
