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

#include "radeon.h"
#include "rv730d.h"
#include "r600_dpm.h"
#include "rv770.h"
#include "rv770_dpm.h"
#include "atom.h"

#define MC_CG_ARB_FREQ_F0           0x0a
#define MC_CG_ARB_FREQ_F1           0x0b
#define MC_CG_ARB_FREQ_F2           0x0c
#define MC_CG_ARB_FREQ_F3           0x0d

int rv730_populate_sclk_value(struct radeon_device *rdev,
			      u32 engine_clock,
			      RV770_SMC_SCLK_VALUE *sclk)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	struct atom_clock_dividers dividers;
	u32 spll_func_cntl = pi->clk_regs.rv730.cg_spll_func_cntl;
	u32 spll_func_cntl_2 = pi->clk_regs.rv730.cg_spll_func_cntl_2;
	u32 spll_func_cntl_3 = pi->clk_regs.rv730.cg_spll_func_cntl_3;
	u32 cg_spll_spread_spectrum = pi->clk_regs.rv730.cg_spll_spread_spectrum;
	u32 cg_spll_spread_spectrum_2 = pi->clk_regs.rv730.cg_spll_spread_spectrum_2;
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
		post_divider = ((dividers.post_div >> 4) & 0xf) +
			(dividers.post_div & 0xf) + 2;
	else
		post_divider = 1;

	tmp = (u64) engine_clock * reference_divider * post_divider * 16384;
	do_div(tmp, reference_clock);
	fbdiv = (u32) tmp;

	/* set up registers */
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

			cg_spll_spread_spectrum &= ~CLK_S_MASK;
			cg_spll_spread_spectrum |= CLK_S(clk_s);
			cg_spll_spread_spectrum |= SSEN;

			cg_spll_spread_spectrum_2 &= ~CLK_V_MASK;
			cg_spll_spread_spectrum_2 |= CLK_V(clk_v);
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

int rv730_populate_mclk_value(struct radeon_device *rdev,
			      u32 engine_clock, u32 memory_clock,
			      LPRV7XX_SMC_MCLK_VALUE mclk)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u32 mclk_pwrmgt_cntl = pi->clk_regs.rv730.mclk_pwrmgt_cntl;
	u32 dll_cntl = pi->clk_regs.rv730.dll_cntl;
	u32 mpll_func_cntl = pi->clk_regs.rv730.mpll_func_cntl;
	u32 mpll_func_cntl_2 = pi->clk_regs.rv730.mpll_func_cntl2;
	u32 mpll_func_cntl_3 = pi->clk_regs.rv730.mpll_func_cntl3;
	u32 mpll_ss = pi->clk_regs.rv730.mpll_ss;
	u32 mpll_ss2 = pi->clk_regs.rv730.mpll_ss2;
	struct atom_clock_dividers dividers;
	u32 post_divider, reference_divider;
	int ret;

	ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_MEMORY_PLL_PARAM,
					     memory_clock, false, &dividers);
	if (ret)
		return ret;

	reference_divider = dividers.ref_div + 1;

	if (dividers.enable_post_div)
		post_divider = ((dividers.post_div >> 4) & 0xf) +
			(dividers.post_div & 0xf) + 2;
	else
		post_divider = 1;

	/* setup the registers */
	if (dividers.enable_post_div)
		mpll_func_cntl |= MPLL_DIVEN;
	else
		mpll_func_cntl &= ~MPLL_DIVEN;

	mpll_func_cntl &= ~(MPLL_REF_DIV_MASK | MPLL_HILEN_MASK | MPLL_LOLEN_MASK);
	mpll_func_cntl |= MPLL_REF_DIV(dividers.ref_div);
	mpll_func_cntl |= MPLL_HILEN((dividers.post_div >> 4) & 0xf);
	mpll_func_cntl |= MPLL_LOLEN(dividers.post_div & 0xf);

	mpll_func_cntl_3 &= ~MPLL_FB_DIV_MASK;
	mpll_func_cntl_3 |= MPLL_FB_DIV(dividers.fb_div);
	if (dividers.enable_dithen)
		mpll_func_cntl_3 |= MPLL_DITHEN;
	else
		mpll_func_cntl_3 &= ~MPLL_DITHEN;

	if (pi->mclk_ss) {
		struct radeon_atom_ss ss;
		u32 vco_freq = memory_clock * post_divider;

		if (radeon_atombios_get_asic_ss_info(rdev, &ss,
						     ASIC_INTERNAL_MEMORY_SS, vco_freq)) {
			u32 reference_clock = rdev->clock.mpll.reference_freq;
			u32 clk_s = reference_clock * 5 / (reference_divider * ss.rate);
			u32 clk_v = ss.percentage * dividers.fb_div / (clk_s * 10000);

			mpll_ss &= ~CLK_S_MASK;
			mpll_ss |= CLK_S(clk_s);
			mpll_ss |= SSEN;

			mpll_ss2 &= ~CLK_V_MASK;
			mpll_ss |= CLK_V(clk_v);
		}
	}


	mclk->mclk730.vMCLK_PWRMGT_CNTL = cpu_to_be32(mclk_pwrmgt_cntl);
	mclk->mclk730.vDLL_CNTL = cpu_to_be32(dll_cntl);
	mclk->mclk730.mclk_value = cpu_to_be32(memory_clock);
	mclk->mclk730.vMPLL_FUNC_CNTL = cpu_to_be32(mpll_func_cntl);
	mclk->mclk730.vMPLL_FUNC_CNTL2 = cpu_to_be32(mpll_func_cntl_2);
	mclk->mclk730.vMPLL_FUNC_CNTL3 = cpu_to_be32(mpll_func_cntl_3);
	mclk->mclk730.vMPLL_SS = cpu_to_be32(mpll_ss);
	mclk->mclk730.vMPLL_SS2 = cpu_to_be32(mpll_ss2);

	return 0;
}

void rv730_read_clock_registers(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);

	pi->clk_regs.rv730.cg_spll_func_cntl =
		RREG32(CG_SPLL_FUNC_CNTL);
	pi->clk_regs.rv730.cg_spll_func_cntl_2 =
		RREG32(CG_SPLL_FUNC_CNTL_2);
	pi->clk_regs.rv730.cg_spll_func_cntl_3 =
		RREG32(CG_SPLL_FUNC_CNTL_3);
	pi->clk_regs.rv730.cg_spll_spread_spectrum =
		RREG32(CG_SPLL_SPREAD_SPECTRUM);
	pi->clk_regs.rv730.cg_spll_spread_spectrum_2 =
		RREG32(CG_SPLL_SPREAD_SPECTRUM_2);

	pi->clk_regs.rv730.mclk_pwrmgt_cntl =
		RREG32(TCI_MCLK_PWRMGT_CNTL);
	pi->clk_regs.rv730.dll_cntl =
		RREG32(TCI_DLL_CNTL);
	pi->clk_regs.rv730.mpll_func_cntl =
		RREG32(CG_MPLL_FUNC_CNTL);
	pi->clk_regs.rv730.mpll_func_cntl2 =
		RREG32(CG_MPLL_FUNC_CNTL_2);
	pi->clk_regs.rv730.mpll_func_cntl3 =
		RREG32(CG_MPLL_FUNC_CNTL_3);
	pi->clk_regs.rv730.mpll_ss =
		RREG32(CG_TCI_MPLL_SPREAD_SPECTRUM);
	pi->clk_regs.rv730.mpll_ss2 =
		RREG32(CG_TCI_MPLL_SPREAD_SPECTRUM_2);
}

int rv730_populate_smc_acpi_state(struct radeon_device *rdev,
				  RV770_SMC_STATETABLE *table)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u32 mpll_func_cntl = 0;
	u32 mpll_func_cntl_2 = 0 ;
	u32 mpll_func_cntl_3 = 0;
	u32 mclk_pwrmgt_cntl;
	u32 dll_cntl;
	u32 spll_func_cntl;
	u32 spll_func_cntl_2;
	u32 spll_func_cntl_3;

	table->ACPIState = table->initialState;
	table->ACPIState.flags &= ~PPSMC_SWSTATE_FLAG_DC;

	if (pi->acpi_vddc) {
		rv770_populate_vddc_value(rdev, pi->acpi_vddc,
					  &table->ACPIState.levels[0].vddc);
		table->ACPIState.levels[0].gen2PCIE = pi->pcie_gen2 ?
			pi->acpi_pcie_gen2 : 0;
		table->ACPIState.levels[0].gen2XSP =
			pi->acpi_pcie_gen2;
	} else {
		rv770_populate_vddc_value(rdev, pi->min_vddc_in_table,
					  &table->ACPIState.levels[0].vddc);
		table->ACPIState.levels[0].gen2PCIE = 0;
	}

	mpll_func_cntl = pi->clk_regs.rv730.mpll_func_cntl;
	mpll_func_cntl_2 = pi->clk_regs.rv730.mpll_func_cntl2;
	mpll_func_cntl_3 = pi->clk_regs.rv730.mpll_func_cntl3;

	mpll_func_cntl |= MPLL_RESET | MPLL_BYPASS_EN;
	mpll_func_cntl &= ~MPLL_SLEEP;

	mpll_func_cntl_2 &= ~MCLK_MUX_SEL_MASK;
	mpll_func_cntl_2 |= MCLK_MUX_SEL(1);

	mclk_pwrmgt_cntl = (MRDCKA_RESET |
			    MRDCKB_RESET |
			    MRDCKC_RESET |
			    MRDCKD_RESET |
			    MRDCKE_RESET |
			    MRDCKF_RESET |
			    MRDCKG_RESET |
			    MRDCKH_RESET |
			    MRDCKA_SLEEP |
			    MRDCKB_SLEEP |
			    MRDCKC_SLEEP |
			    MRDCKD_SLEEP |
			    MRDCKE_SLEEP |
			    MRDCKF_SLEEP |
			    MRDCKG_SLEEP |
			    MRDCKH_SLEEP);

	dll_cntl = 0xff000000;

	spll_func_cntl = pi->clk_regs.rv730.cg_spll_func_cntl;
	spll_func_cntl_2 = pi->clk_regs.rv730.cg_spll_func_cntl_2;
	spll_func_cntl_3 = pi->clk_regs.rv730.cg_spll_func_cntl_3;

	spll_func_cntl |= SPLL_RESET | SPLL_BYPASS_EN;
	spll_func_cntl &= ~SPLL_SLEEP;

	spll_func_cntl_2 &= ~SCLK_MUX_SEL_MASK;
	spll_func_cntl_2 |= SCLK_MUX_SEL(4);

	table->ACPIState.levels[0].mclk.mclk730.vMPLL_FUNC_CNTL = cpu_to_be32(mpll_func_cntl);
	table->ACPIState.levels[0].mclk.mclk730.vMPLL_FUNC_CNTL2 = cpu_to_be32(mpll_func_cntl_2);
	table->ACPIState.levels[0].mclk.mclk730.vMPLL_FUNC_CNTL3 = cpu_to_be32(mpll_func_cntl_3);
	table->ACPIState.levels[0].mclk.mclk730.vMCLK_PWRMGT_CNTL = cpu_to_be32(mclk_pwrmgt_cntl);
	table->ACPIState.levels[0].mclk.mclk730.vDLL_CNTL = cpu_to_be32(dll_cntl);

	table->ACPIState.levels[0].mclk.mclk730.mclk_value = 0;

	table->ACPIState.levels[0].sclk.vCG_SPLL_FUNC_CNTL = cpu_to_be32(spll_func_cntl);
	table->ACPIState.levels[0].sclk.vCG_SPLL_FUNC_CNTL_2 = cpu_to_be32(spll_func_cntl_2);
	table->ACPIState.levels[0].sclk.vCG_SPLL_FUNC_CNTL_3 = cpu_to_be32(spll_func_cntl_3);

	table->ACPIState.levels[0].sclk.sclk_value = 0;

	rv770_populate_mvdd_value(rdev, 0, &table->ACPIState.levels[0].mvdd);

	table->ACPIState.levels[1] = table->ACPIState.levels[0];
	table->ACPIState.levels[2] = table->ACPIState.levels[0];

	return 0;
}

int rv730_populate_smc_initial_state(struct radeon_device *rdev,
				     struct radeon_ps *radeon_state,
				     RV770_SMC_STATETABLE *table)
{
	struct rv7xx_ps *initial_state = rv770_get_ps(radeon_state);
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u32 a_t;

	table->initialState.levels[0].mclk.mclk730.vMPLL_FUNC_CNTL =
		cpu_to_be32(pi->clk_regs.rv730.mpll_func_cntl);
	table->initialState.levels[0].mclk.mclk730.vMPLL_FUNC_CNTL2 =
		cpu_to_be32(pi->clk_regs.rv730.mpll_func_cntl2);
	table->initialState.levels[0].mclk.mclk730.vMPLL_FUNC_CNTL3 =
		cpu_to_be32(pi->clk_regs.rv730.mpll_func_cntl3);
	table->initialState.levels[0].mclk.mclk730.vMCLK_PWRMGT_CNTL =
		cpu_to_be32(pi->clk_regs.rv730.mclk_pwrmgt_cntl);
	table->initialState.levels[0].mclk.mclk730.vDLL_CNTL =
		cpu_to_be32(pi->clk_regs.rv730.dll_cntl);
	table->initialState.levels[0].mclk.mclk730.vMPLL_SS =
		cpu_to_be32(pi->clk_regs.rv730.mpll_ss);
	table->initialState.levels[0].mclk.mclk730.vMPLL_SS2 =
		cpu_to_be32(pi->clk_regs.rv730.mpll_ss2);

	table->initialState.levels[0].mclk.mclk730.mclk_value =
		cpu_to_be32(initial_state->low.mclk);

	table->initialState.levels[0].sclk.vCG_SPLL_FUNC_CNTL =
		cpu_to_be32(pi->clk_regs.rv730.cg_spll_func_cntl);
	table->initialState.levels[0].sclk.vCG_SPLL_FUNC_CNTL_2 =
		cpu_to_be32(pi->clk_regs.rv730.cg_spll_func_cntl_2);
	table->initialState.levels[0].sclk.vCG_SPLL_FUNC_CNTL_3 =
		cpu_to_be32(pi->clk_regs.rv730.cg_spll_func_cntl_3);
	table->initialState.levels[0].sclk.vCG_SPLL_SPREAD_SPECTRUM =
		cpu_to_be32(pi->clk_regs.rv730.cg_spll_spread_spectrum);
	table->initialState.levels[0].sclk.vCG_SPLL_SPREAD_SPECTRUM_2 =
		cpu_to_be32(pi->clk_regs.rv730.cg_spll_spread_spectrum_2);

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

	table->initialState.levels[1] = table->initialState.levels[0];
	table->initialState.levels[2] = table->initialState.levels[0];

	table->initialState.flags |= PPSMC_SWSTATE_FLAG_DC;

	return 0;
}

void rv730_program_memory_timing_parameters(struct radeon_device *rdev,
					    struct radeon_ps *radeon_state)
{
	struct rv7xx_ps *state = rv770_get_ps(radeon_state);
	u32 arb_refresh_rate = 0;
	u32 dram_timing = 0;
	u32 dram_timing2 = 0;
	u32 old_dram_timing = 0;
	u32 old_dram_timing2 = 0;

	arb_refresh_rate = RREG32(MC_ARB_RFSH_RATE) &
		~(POWERMODE1_MASK | POWERMODE2_MASK | POWERMODE3_MASK);
	arb_refresh_rate |=
		(POWERMODE1(rv770_calculate_memory_refresh_rate(rdev, state->low.sclk)) |
		 POWERMODE2(rv770_calculate_memory_refresh_rate(rdev, state->medium.sclk)) |
		 POWERMODE3(rv770_calculate_memory_refresh_rate(rdev, state->high.sclk)));
	WREG32(MC_ARB_RFSH_RATE, arb_refresh_rate);

	/* save the boot dram timings */
	old_dram_timing = RREG32(MC_ARB_DRAM_TIMING);
	old_dram_timing2 = RREG32(MC_ARB_DRAM_TIMING2);

	radeon_atom_set_engine_dram_timings(rdev,
					    state->high.sclk,
					    state->high.mclk);

	dram_timing = RREG32(MC_ARB_DRAM_TIMING);
	dram_timing2 = RREG32(MC_ARB_DRAM_TIMING2);

	WREG32(MC_ARB_DRAM_TIMING_3, dram_timing);
	WREG32(MC_ARB_DRAM_TIMING2_3, dram_timing2);

	radeon_atom_set_engine_dram_timings(rdev,
					    state->medium.sclk,
					    state->medium.mclk);

	dram_timing = RREG32(MC_ARB_DRAM_TIMING);
	dram_timing2 = RREG32(MC_ARB_DRAM_TIMING2);

	WREG32(MC_ARB_DRAM_TIMING_2, dram_timing);
	WREG32(MC_ARB_DRAM_TIMING2_2, dram_timing2);

	radeon_atom_set_engine_dram_timings(rdev,
					    state->low.sclk,
					    state->low.mclk);

	dram_timing = RREG32(MC_ARB_DRAM_TIMING);
	dram_timing2 = RREG32(MC_ARB_DRAM_TIMING2);

	WREG32(MC_ARB_DRAM_TIMING_1, dram_timing);
	WREG32(MC_ARB_DRAM_TIMING2_1, dram_timing2);

	/* restore the boot dram timings */
	WREG32(MC_ARB_DRAM_TIMING, old_dram_timing);
	WREG32(MC_ARB_DRAM_TIMING2, old_dram_timing2);

}

void rv730_start_dpm(struct radeon_device *rdev)
{
	WREG32_P(SCLK_PWRMGT_CNTL, 0, ~SCLK_PWRMGT_OFF);

	WREG32_P(TCI_MCLK_PWRMGT_CNTL, 0, ~MPLL_PWRMGT_OFF);

	WREG32_P(GENERAL_PWRMGT, GLOBAL_PWRMGT_EN, ~GLOBAL_PWRMGT_EN);
}

void rv730_stop_dpm(struct radeon_device *rdev)
{
	PPSMC_Result result;

	result = rv770_send_msg_to_smc(rdev, PPSMC_MSG_TwoLevelsDisabled);

	if (result != PPSMC_Result_OK)
		DRM_DEBUG("Could not force DPM to low\n");

	WREG32_P(GENERAL_PWRMGT, 0, ~GLOBAL_PWRMGT_EN);

	WREG32_P(SCLK_PWRMGT_CNTL, SCLK_PWRMGT_OFF, ~SCLK_PWRMGT_OFF);

	WREG32_P(TCI_MCLK_PWRMGT_CNTL, MPLL_PWRMGT_OFF, ~MPLL_PWRMGT_OFF);
}

void rv730_program_dcodt(struct radeon_device *rdev, bool use_dcodt)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u32 i = use_dcodt ? 0 : 1;
	u32 mc4_io_pad_cntl;

	mc4_io_pad_cntl = RREG32(MC4_IO_DQ_PAD_CNTL_D0_I0);
	mc4_io_pad_cntl &= 0xFFFFFF00;
	mc4_io_pad_cntl |= pi->odt_value_0[i];
	WREG32(MC4_IO_DQ_PAD_CNTL_D0_I0, mc4_io_pad_cntl);
	WREG32(MC4_IO_DQ_PAD_CNTL_D0_I1, mc4_io_pad_cntl);

	mc4_io_pad_cntl = RREG32(MC4_IO_QS_PAD_CNTL_D0_I0);
	mc4_io_pad_cntl &= 0xFFFFFF00;
	mc4_io_pad_cntl |= pi->odt_value_1[i];
	WREG32(MC4_IO_QS_PAD_CNTL_D0_I0, mc4_io_pad_cntl);
	WREG32(MC4_IO_QS_PAD_CNTL_D0_I1, mc4_io_pad_cntl);
}

void rv730_get_odt_values(struct radeon_device *rdev)
{
	struct rv7xx_power_info *pi = rv770_get_pi(rdev);
	u32 mc4_io_pad_cntl;

	pi->odt_value_0[0] = (u8)0;
	pi->odt_value_1[0] = (u8)0x80;

	mc4_io_pad_cntl = RREG32(MC4_IO_DQ_PAD_CNTL_D0_I0);
	pi->odt_value_0[1] = (u8)(mc4_io_pad_cntl & 0xff);

	mc4_io_pad_cntl = RREG32(MC4_IO_QS_PAD_CNTL_D0_I0);
	pi->odt_value_1[1] = (u8)(mc4_io_pad_cntl & 0xff);
}
