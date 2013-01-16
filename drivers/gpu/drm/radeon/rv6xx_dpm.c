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
#include "rv6xxd.h"
#include "r600_dpm.h"
#include "rv6xx_dpm.h"
#include "atom.h"

static u32 rv6xx_scale_count_given_unit(struct radeon_device *rdev,
					u32 unscaled_count, u32 unit);

static struct rv6xx_ps *rv6xx_get_ps(struct radeon_ps *rps)
{
	struct rv6xx_ps *ps = rps->ps_priv;

	return ps;
}

static struct rv6xx_power_info *rv6xx_get_pi(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rdev->pm.dpm.priv;

	return pi;
}

static void rv6xx_force_pcie_gen1(struct radeon_device *rdev)
{
	u32 tmp;
	int i;

	tmp = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
	tmp &= LC_GEN2_EN;
	WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, tmp);

	tmp = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
	tmp |= LC_INITIATE_LINK_SPEED_CHANGE;
	WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, tmp);

	for (i = 0; i < rdev->usec_timeout; i++) {
		if (!(RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL) & LC_CURRENT_DATA_RATE))
			break;
		udelay(1);
	}

	tmp = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
	tmp &= ~LC_INITIATE_LINK_SPEED_CHANGE;
	WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, tmp);
}

static void rv6xx_enable_pcie_gen2_support(struct radeon_device *rdev)
{
	u32 tmp;

	tmp = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);

	if ((tmp & LC_OTHER_SIDE_EVER_SENT_GEN2) &&
	    (tmp & LC_OTHER_SIDE_SUPPORTS_GEN2)) {
		tmp |= LC_GEN2_EN;
		WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, tmp);
	}
}

static void rv6xx_enable_bif_dynamic_pcie_gen2(struct radeon_device *rdev,
					       bool enable)
{
	u32 tmp;

	tmp = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL) & ~LC_HW_VOLTAGE_IF_CONTROL_MASK;
	if (enable)
		tmp |= LC_HW_VOLTAGE_IF_CONTROL(1);
	else
		tmp |= LC_HW_VOLTAGE_IF_CONTROL(0);
	WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, tmp);
}

static void rv6xx_enable_l0s(struct radeon_device *rdev)
{
	u32 tmp;

	tmp = RREG32_PCIE_PORT(PCIE_LC_CNTL) & ~LC_L0S_INACTIVITY_MASK;
	tmp |= LC_L0S_INACTIVITY(3);
	WREG32_PCIE_PORT(PCIE_LC_CNTL, tmp);
}

static void rv6xx_enable_l1(struct radeon_device *rdev)
{
	u32 tmp;

	tmp = RREG32_PCIE_PORT(PCIE_LC_CNTL);
	tmp &= ~LC_L1_INACTIVITY_MASK;
	tmp |= LC_L1_INACTIVITY(4);
	tmp &= ~LC_PMI_TO_L1_DIS;
	tmp &= ~LC_ASPM_TO_L1_DIS;
	WREG32_PCIE_PORT(PCIE_LC_CNTL, tmp);
}

static void rv6xx_enable_pll_sleep_in_l1(struct radeon_device *rdev)
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

static int rv6xx_convert_clock_to_stepping(struct radeon_device *rdev,
					   u32 clock, struct rv6xx_sclk_stepping *step)
{
	int ret;
	struct atom_clock_dividers dividers;

	ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
					     clock, false, &dividers);
	if (ret)
		return ret;

	if (dividers.enable_post_div)
		step->post_divider = 2 + (dividers.post_div & 0xF) + (dividers.post_div >> 4);
	else
		step->post_divider = 1;

	step->vco_frequency = clock * step->post_divider;

	return 0;
}

static void rv6xx_output_stepping(struct radeon_device *rdev,
				  u32 step_index, struct rv6xx_sclk_stepping *step)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);
	u32 ref_clk = rdev->clock.spll.reference_freq;
	u32 fb_divider;
	u32 spll_step_count = rv6xx_scale_count_given_unit(rdev,
							   R600_SPLLSTEPTIME_DFLT *
							   pi->spll_ref_div,
							   R600_SPLLSTEPUNIT_DFLT);

	r600_engine_clock_entry_enable(rdev, step_index, true);
	r600_engine_clock_entry_enable_pulse_skipping(rdev, step_index, false);

	if (step->post_divider == 1)
		r600_engine_clock_entry_enable_post_divider(rdev, step_index, false);
	else {
		u32 lo_len = (step->post_divider - 2) / 2;
		u32 hi_len = step->post_divider - 2 - lo_len;

		r600_engine_clock_entry_enable_post_divider(rdev, step_index, true);
		r600_engine_clock_entry_set_post_divider(rdev, step_index, (hi_len << 4) | lo_len);
	}

	fb_divider = ((step->vco_frequency * pi->spll_ref_div) / ref_clk) >>
		pi->fb_div_scale;

	r600_engine_clock_entry_set_reference_divider(rdev, step_index,
						      pi->spll_ref_div - 1);
	r600_engine_clock_entry_set_feedback_divider(rdev, step_index, fb_divider);
	r600_engine_clock_entry_set_step_time(rdev, step_index, spll_step_count);

}

static struct rv6xx_sclk_stepping rv6xx_next_vco_step(struct radeon_device *rdev,
						      struct rv6xx_sclk_stepping *cur,
						      bool increasing_vco, u32 step_size)
{
	struct rv6xx_sclk_stepping next;

	next.post_divider = cur->post_divider;

	if (increasing_vco)
		next.vco_frequency = (cur->vco_frequency * (100 + step_size)) / 100;
	else
		next.vco_frequency = (cur->vco_frequency * 100 + 99 + step_size) / (100 + step_size);

	return next;
}

static bool rv6xx_can_step_post_div(struct radeon_device *rdev,
				    struct rv6xx_sclk_stepping *cur,
                                    struct rv6xx_sclk_stepping *target)
{
	return (cur->post_divider > target->post_divider) &&
		((cur->vco_frequency * target->post_divider) <=
		 (target->vco_frequency * (cur->post_divider - 1)));
}

static struct rv6xx_sclk_stepping rv6xx_next_post_div_step(struct radeon_device *rdev,
							   struct rv6xx_sclk_stepping *cur,
							   struct rv6xx_sclk_stepping *target)
{
	struct rv6xx_sclk_stepping next = *cur;

	while (rv6xx_can_step_post_div(rdev, &next, target))
		next.post_divider--;

	return next;
}

static bool rv6xx_reached_stepping_target(struct radeon_device *rdev,
					  struct rv6xx_sclk_stepping *cur,
					  struct rv6xx_sclk_stepping *target,
					  bool increasing_vco)
{
	return (increasing_vco && (cur->vco_frequency >= target->vco_frequency)) ||
		(!increasing_vco && (cur->vco_frequency <= target->vco_frequency));
}

static void rv6xx_generate_steps(struct radeon_device *rdev,
				 u32 low, u32 high,
                                 u32 start_index, u8 *end_index)
{
	struct rv6xx_sclk_stepping cur;
	struct rv6xx_sclk_stepping target;
	bool increasing_vco;
	u32 step_index = start_index;

	rv6xx_convert_clock_to_stepping(rdev, low, &cur);
	rv6xx_convert_clock_to_stepping(rdev, high, &target);

	rv6xx_output_stepping(rdev, step_index++, &cur);

	increasing_vco = (target.vco_frequency >= cur.vco_frequency);

	if (target.post_divider > cur.post_divider)
		cur.post_divider = target.post_divider;

	while (1) {
		struct rv6xx_sclk_stepping next;

		if (rv6xx_can_step_post_div(rdev, &cur, &target))
			next = rv6xx_next_post_div_step(rdev, &cur, &target);
		else
			next = rv6xx_next_vco_step(rdev, &cur, increasing_vco, R600_VCOSTEPPCT_DFLT);

		if (rv6xx_reached_stepping_target(rdev, &next, &target, increasing_vco)) {
			struct rv6xx_sclk_stepping tiny =
				rv6xx_next_vco_step(rdev, &target, !increasing_vco, R600_ENDINGVCOSTEPPCT_DFLT);
			tiny.post_divider = next.post_divider;

			if (!rv6xx_reached_stepping_target(rdev, &tiny, &cur, !increasing_vco))
				rv6xx_output_stepping(rdev, step_index++, &tiny);

			if ((next.post_divider != target.post_divider) &&
			    (next.vco_frequency != target.vco_frequency)) {
				struct rv6xx_sclk_stepping final_vco;

				final_vco.vco_frequency = target.vco_frequency;
				final_vco.post_divider = next.post_divider;

				rv6xx_output_stepping(rdev, step_index++, &final_vco);
			}

			rv6xx_output_stepping(rdev, step_index++, &target);
			break;
		} else
			rv6xx_output_stepping(rdev, step_index++, &next);

		cur = next;
	}

	*end_index = (u8)step_index - 1;

}

static void rv6xx_generate_single_step(struct radeon_device *rdev,
				       u32 clock, u32 index)
{
	struct rv6xx_sclk_stepping step;

	rv6xx_convert_clock_to_stepping(rdev, clock, &step);
	rv6xx_output_stepping(rdev, index, &step);
}

static void rv6xx_invalidate_intermediate_steps_range(struct radeon_device *rdev,
						      u32 start_index, u32 end_index)
{
	u32 step_index;

	for (step_index = start_index + 1; step_index < end_index; step_index++)
		r600_engine_clock_entry_enable(rdev, step_index, false);
}

static void rv6xx_set_engine_spread_spectrum_clk_s(struct radeon_device *rdev,
						   u32 index, u32 clk_s)
{
	WREG32_P(CG_SPLL_SPREAD_SPECTRUM_LOW + (index * 4),
		 CLKS(clk_s), ~CLKS_MASK);
}

static void rv6xx_set_engine_spread_spectrum_clk_v(struct radeon_device *rdev,
						   u32 index, u32 clk_v)
{
	WREG32_P(CG_SPLL_SPREAD_SPECTRUM_LOW + (index * 4),
		 CLKV(clk_v), ~CLKV_MASK);
}

static void rv6xx_enable_engine_spread_spectrum(struct radeon_device *rdev,
						u32 index, bool enable)
{
	if (enable)
		WREG32_P(CG_SPLL_SPREAD_SPECTRUM_LOW + (index * 4),
			 SSEN, ~SSEN);
	else
		WREG32_P(CG_SPLL_SPREAD_SPECTRUM_LOW + (index * 4),
			 0, ~SSEN);
}

static void rv6xx_set_memory_spread_spectrum_clk_s(struct radeon_device *rdev,
						   u32 clk_s)
{
	WREG32_P(CG_MPLL_SPREAD_SPECTRUM, CLKS(clk_s), ~CLKS_MASK);
}

static void rv6xx_set_memory_spread_spectrum_clk_v(struct radeon_device *rdev,
						   u32 clk_v)
{
	WREG32_P(CG_MPLL_SPREAD_SPECTRUM, CLKV(clk_v), ~CLKV_MASK);
}

static void rv6xx_enable_memory_spread_spectrum(struct radeon_device *rdev,
						bool enable)
{
	if (enable)
		WREG32_P(CG_MPLL_SPREAD_SPECTRUM, SSEN, ~SSEN);
	else
		WREG32_P(CG_MPLL_SPREAD_SPECTRUM, 0, ~SSEN);
}

static void rv6xx_enable_dynamic_spread_spectrum(struct radeon_device *rdev,
						 bool enable)
{
	if (enable)
		WREG32_P(GENERAL_PWRMGT, DYN_SPREAD_SPECTRUM_EN, ~DYN_SPREAD_SPECTRUM_EN);
	else
		WREG32_P(GENERAL_PWRMGT, 0, ~DYN_SPREAD_SPECTRUM_EN);
}

static void rv6xx_memory_clock_entry_enable_post_divider(struct radeon_device *rdev,
							 u32 index, bool enable)
{
	if (enable)
		WREG32_P(MPLL_FREQ_LEVEL_0 + (index * 4),
			 LEVEL0_MPLL_DIV_EN, ~LEVEL0_MPLL_DIV_EN);
	else
		WREG32_P(MPLL_FREQ_LEVEL_0 + (index * 4), 0, ~LEVEL0_MPLL_DIV_EN);
}

static void rv6xx_memory_clock_entry_set_post_divider(struct radeon_device *rdev,
						      u32 index, u32 divider)
{
	WREG32_P(MPLL_FREQ_LEVEL_0 + (index * 4),
		 LEVEL0_MPLL_POST_DIV(divider), ~LEVEL0_MPLL_POST_DIV_MASK);
}

static void rv6xx_memory_clock_entry_set_feedback_divider(struct radeon_device *rdev,
							  u32 index, u32 divider)
{
	WREG32_P(MPLL_FREQ_LEVEL_0 + (index * 4), LEVEL0_MPLL_FB_DIV(divider),
		 ~LEVEL0_MPLL_FB_DIV_MASK);
}

static void rv6xx_memory_clock_entry_set_reference_divider(struct radeon_device *rdev,
							   u32 index, u32 divider)
{
	WREG32_P(MPLL_FREQ_LEVEL_0 + (index * 4),
		 LEVEL0_MPLL_REF_DIV(divider), ~LEVEL0_MPLL_REF_DIV_MASK);
}

static void rv6xx_vid_response_set_brt(struct radeon_device *rdev, u32 rt)
{
	WREG32_P(VID_RT, BRT(rt), ~BRT_MASK);
}

static void rv6xx_enable_engine_feedback_and_reference_sync(struct radeon_device *rdev)
{
	WREG32_P(SPLL_CNTL_MODE, SPLL_DIV_SYNC, ~SPLL_DIV_SYNC);
}

static u64 rv6xx_clocks_per_unit(u32 unit)
{
	u64 tmp = 1 << (2 * unit);

	return tmp;
}

static u32 rv6xx_scale_count_given_unit(struct radeon_device *rdev,
					u32 unscaled_count, u32 unit)
{
	u32 count_per_unit = (u32)rv6xx_clocks_per_unit(unit);

	return (unscaled_count + count_per_unit - 1) / count_per_unit;
}

static u32 rv6xx_compute_count_for_delay(struct radeon_device *rdev,
					 u32 delay_us, u32 unit)
{
	u32 ref_clk = rdev->clock.spll.reference_freq;

	return rv6xx_scale_count_given_unit(rdev, delay_us * (ref_clk / 100), unit);
}

static void rv6xx_calculate_engine_speed_stepping_parameters(struct radeon_device *rdev,
							     struct rv6xx_ps *state)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	pi->hw.sclks[R600_POWER_LEVEL_LOW] =
		state->low.sclk;
	pi->hw.sclks[R600_POWER_LEVEL_MEDIUM] =
		state->medium.sclk;
	pi->hw.sclks[R600_POWER_LEVEL_HIGH] =
		state->high.sclk;

	pi->hw.low_sclk_index = R600_POWER_LEVEL_LOW;
	pi->hw.medium_sclk_index = R600_POWER_LEVEL_MEDIUM;
	pi->hw.high_sclk_index = R600_POWER_LEVEL_HIGH;
}

static void rv6xx_calculate_memory_clock_stepping_parameters(struct radeon_device *rdev,
							     struct rv6xx_ps *state)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	pi->hw.mclks[R600_POWER_LEVEL_CTXSW] =
		state->high.mclk;
	pi->hw.mclks[R600_POWER_LEVEL_HIGH] =
		state->high.mclk;
	pi->hw.mclks[R600_POWER_LEVEL_MEDIUM] =
		state->medium.mclk;
	pi->hw.mclks[R600_POWER_LEVEL_LOW] =
		state->low.mclk;

	pi->hw.high_mclk_index = R600_POWER_LEVEL_HIGH;

	if (state->high.mclk == state->medium.mclk)
		pi->hw.medium_mclk_index =
			pi->hw.high_mclk_index;
	else
		pi->hw.medium_mclk_index = R600_POWER_LEVEL_MEDIUM;


	if (state->medium.mclk == state->low.mclk)
		pi->hw.low_mclk_index =
			pi->hw.medium_mclk_index;
	else
		pi->hw.low_mclk_index = R600_POWER_LEVEL_LOW;
}

static void rv6xx_calculate_voltage_stepping_parameters(struct radeon_device *rdev,
							struct rv6xx_ps *state)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	pi->hw.vddc[R600_POWER_LEVEL_CTXSW] = state->high.vddc;
	pi->hw.vddc[R600_POWER_LEVEL_HIGH] = state->high.vddc;
	pi->hw.vddc[R600_POWER_LEVEL_MEDIUM] = state->medium.vddc;
	pi->hw.vddc[R600_POWER_LEVEL_LOW] = state->low.vddc;

	pi->hw.backbias[R600_POWER_LEVEL_CTXSW] =
		(state->high.flags & ATOM_PPLIB_R600_FLAGS_BACKBIASENABLE) ? true : false;
	pi->hw.backbias[R600_POWER_LEVEL_HIGH] =
		(state->high.flags & ATOM_PPLIB_R600_FLAGS_BACKBIASENABLE) ? true : false;
	pi->hw.backbias[R600_POWER_LEVEL_MEDIUM] =
		(state->medium.flags & ATOM_PPLIB_R600_FLAGS_BACKBIASENABLE) ? true : false;
	pi->hw.backbias[R600_POWER_LEVEL_LOW] =
		(state->low.flags & ATOM_PPLIB_R600_FLAGS_BACKBIASENABLE) ? true : false;

	pi->hw.pcie_gen2[R600_POWER_LEVEL_HIGH] =
		(state->high.flags & ATOM_PPLIB_R600_FLAGS_PCIEGEN2) ? true : false;
	pi->hw.pcie_gen2[R600_POWER_LEVEL_MEDIUM] =
		(state->medium.flags & ATOM_PPLIB_R600_FLAGS_PCIEGEN2) ? true : false;
	pi->hw.pcie_gen2[R600_POWER_LEVEL_LOW] =
		(state->low.flags & ATOM_PPLIB_R600_FLAGS_PCIEGEN2) ? true : false;

	pi->hw.high_vddc_index = R600_POWER_LEVEL_HIGH;

	if ((state->high.vddc == state->medium.vddc) &&
	    ((state->high.flags & ATOM_PPLIB_R600_FLAGS_BACKBIASENABLE) ==
	     (state->medium.flags & ATOM_PPLIB_R600_FLAGS_BACKBIASENABLE)))
		pi->hw.medium_vddc_index =
			pi->hw.high_vddc_index;
	else
		pi->hw.medium_vddc_index = R600_POWER_LEVEL_MEDIUM;

	if ((state->medium.vddc == state->low.vddc) &&
	    ((state->medium.flags & ATOM_PPLIB_R600_FLAGS_BACKBIASENABLE) ==
	     (state->low.flags & ATOM_PPLIB_R600_FLAGS_BACKBIASENABLE)))
		pi->hw.low_vddc_index =
			pi->hw.medium_vddc_index;
	else
		pi->hw.medium_vddc_index = R600_POWER_LEVEL_LOW;
}

static inline u32 rv6xx_calculate_vco_frequency(u32 ref_clock,
						struct atom_clock_dividers *dividers,
						u32 fb_divider_scale)
{
	return ref_clock * ((dividers->fb_div & ~1) << fb_divider_scale) /
		(dividers->ref_div + 1);
}

static inline u32 rv6xx_calculate_spread_spectrum_clk_v(u32 vco_freq, u32 ref_freq,
							u32 ss_rate, u32 ss_percent,
							u32 fb_divider_scale)
{
	u32 fb_divider = vco_freq / ref_freq;

	return (ss_percent * ss_rate * 4 * (fb_divider * fb_divider) /
		(5375 * ((vco_freq * 10) / (4096 >> fb_divider_scale))));
}

static inline u32 rv6xx_calculate_spread_spectrum_clk_s(u32 ss_rate, u32 ref_freq)
{
	return (((ref_freq * 10) / (ss_rate * 2)) - 1) / 4;
}

static void rv6xx_program_engine_spread_spectrum(struct radeon_device *rdev,
						 u32 clock, enum r600_power_level level)
{
	u32 ref_clk = rdev->clock.spll.reference_freq;
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);
	struct atom_clock_dividers dividers;
	struct radeon_atom_ss ss;
	u32 vco_freq, clk_v, clk_s;

	rv6xx_enable_engine_spread_spectrum(rdev, level, false);

	if (clock && pi->sclk_ss) {
		if (radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM, clock, false, &dividers) == 0) {
			vco_freq = rv6xx_calculate_vco_frequency(ref_clk, &dividers,
								 pi->fb_div_scale);

			if (radeon_atombios_get_asic_ss_info(rdev, &ss,
							     ASIC_INTERNAL_ENGINE_SS, vco_freq)) {
				clk_v = rv6xx_calculate_spread_spectrum_clk_v(vco_freq,
									      (ref_clk / (dividers.ref_div + 1)),
									      ss.rate,
									      ss.percentage,
									      pi->fb_div_scale);

				clk_s = rv6xx_calculate_spread_spectrum_clk_s(ss.rate,
									      (ref_clk / (dividers.ref_div + 1)));

				rv6xx_set_engine_spread_spectrum_clk_v(rdev, level, clk_v);
				rv6xx_set_engine_spread_spectrum_clk_s(rdev, level, clk_s);
				rv6xx_enable_engine_spread_spectrum(rdev, level, true);
			}
		}
	}
}

static void rv6xx_program_sclk_spread_spectrum_parameters_except_lowest_entry(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	rv6xx_program_engine_spread_spectrum(rdev,
					     pi->hw.sclks[R600_POWER_LEVEL_HIGH],
					     R600_POWER_LEVEL_HIGH);

	rv6xx_program_engine_spread_spectrum(rdev,
					     pi->hw.sclks[R600_POWER_LEVEL_MEDIUM],
					     R600_POWER_LEVEL_MEDIUM);

}

static int rv6xx_program_mclk_stepping_entry(struct radeon_device *rdev,
					     u32 entry, u32 clock)
{
	struct atom_clock_dividers dividers;

	if (radeon_atom_get_clock_dividers(rdev, COMPUTE_MEMORY_PLL_PARAM, clock, false, &dividers))
	    return -EINVAL;


	rv6xx_memory_clock_entry_set_reference_divider(rdev, entry, dividers.ref_div);
	rv6xx_memory_clock_entry_set_feedback_divider(rdev, entry, dividers.fb_div);
	rv6xx_memory_clock_entry_set_post_divider(rdev, entry, dividers.post_div);

	if (dividers.enable_post_div)
		rv6xx_memory_clock_entry_enable_post_divider(rdev, entry, true);
	else
		rv6xx_memory_clock_entry_enable_post_divider(rdev, entry, false);

	return 0;
}

static void rv6xx_program_mclk_stepping_parameters_except_lowest_entry(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);
	int i;

	for (i = 1; i < R600_PM_NUMBER_OF_MCLKS; i++) {
		if (pi->hw.mclks[i])
			rv6xx_program_mclk_stepping_entry(rdev, i,
							  pi->hw.mclks[i]);
	}
}

static void rv6xx_find_memory_clock_with_highest_vco(struct radeon_device *rdev,
						     u32 requested_memory_clock,
						     u32 ref_clk,
						     struct atom_clock_dividers *dividers,
						     u32 *vco_freq)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);
	struct atom_clock_dividers req_dividers;
	u32 vco_freq_temp;

	if (radeon_atom_get_clock_dividers(rdev, COMPUTE_MEMORY_PLL_PARAM,
					   requested_memory_clock, false, &req_dividers) == 0) {
		vco_freq_temp = rv6xx_calculate_vco_frequency(ref_clk, &req_dividers,
							      pi->fb_div_scale);

		if (vco_freq_temp > *vco_freq) {
			*dividers = req_dividers;
			*vco_freq = vco_freq_temp;
		}
	}
}

static void rv6xx_program_mclk_spread_spectrum_parameters(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);
	u32 ref_clk = rdev->clock.mpll.reference_freq;
	struct atom_clock_dividers dividers;
	struct radeon_atom_ss ss;
	u32 vco_freq = 0, clk_v, clk_s;

	rv6xx_enable_memory_spread_spectrum(rdev, false);

	if (pi->mclk_ss) {
		rv6xx_find_memory_clock_with_highest_vco(rdev,
							 pi->hw.mclks[pi->hw.high_mclk_index],
							 ref_clk,
							 &dividers,
							 &vco_freq);

		rv6xx_find_memory_clock_with_highest_vco(rdev,
							 pi->hw.mclks[pi->hw.medium_mclk_index],
							 ref_clk,
							 &dividers,
							 &vco_freq);

		rv6xx_find_memory_clock_with_highest_vco(rdev,
							 pi->hw.mclks[pi->hw.low_mclk_index],
							 ref_clk,
							 &dividers,
							 &vco_freq);

		if (vco_freq) {
			if (radeon_atombios_get_asic_ss_info(rdev, &ss,
							     ASIC_INTERNAL_MEMORY_SS, vco_freq)) {
				clk_v = rv6xx_calculate_spread_spectrum_clk_v(vco_freq,
									     (ref_clk / (dividers.ref_div + 1)),
									     ss.rate,
									     ss.percentage,
									     pi->fb_div_scale);

				clk_s = rv6xx_calculate_spread_spectrum_clk_s(ss.rate,
									     (ref_clk / (dividers.ref_div + 1)));

				rv6xx_set_memory_spread_spectrum_clk_v(rdev, clk_v);
				rv6xx_set_memory_spread_spectrum_clk_s(rdev, clk_s);
				rv6xx_enable_memory_spread_spectrum(rdev, true);
			}
		}
	}
}

static int rv6xx_program_voltage_stepping_entry(struct radeon_device *rdev,
						u32 entry, u16 voltage)
{
	u32 mask, set_pins;
	int ret;

	ret = radeon_atom_get_voltage_gpio_settings(rdev, voltage,
						    SET_VOLTAGE_TYPE_ASIC_VDDC,
						    &set_pins, &mask);
	if (ret)
		return ret;

	r600_voltage_control_program_voltages(rdev, entry, set_pins);

	return 0;
}

static void rv6xx_program_voltage_stepping_parameters_except_lowest_entry(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);
	int i;

	for (i = 1; i < R600_PM_NUMBER_OF_VOLTAGE_LEVELS; i++)
		rv6xx_program_voltage_stepping_entry(rdev, i,
						     pi->hw.vddc[i]);

}

static void rv6xx_program_backbias_stepping_parameters_except_lowest_entry(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	if (pi->hw.backbias[1])
		WREG32_P(VID_UPPER_GPIO_CNTL, MEDIUM_BACKBIAS_VALUE, ~MEDIUM_BACKBIAS_VALUE);
	else
		WREG32_P(VID_UPPER_GPIO_CNTL, 0, ~MEDIUM_BACKBIAS_VALUE);

	if (pi->hw.backbias[2])
		WREG32_P(VID_UPPER_GPIO_CNTL, HIGH_BACKBIAS_VALUE, ~HIGH_BACKBIAS_VALUE);
	else
		WREG32_P(VID_UPPER_GPIO_CNTL, 0, ~HIGH_BACKBIAS_VALUE);
}

static void rv6xx_program_sclk_spread_spectrum_parameters_lowest_entry(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	rv6xx_program_engine_spread_spectrum(rdev,
					     pi->hw.sclks[R600_POWER_LEVEL_LOW],
					     R600_POWER_LEVEL_LOW);
}

static void rv6xx_program_mclk_stepping_parameters_lowest_entry(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	if (pi->hw.mclks[0])
		rv6xx_program_mclk_stepping_entry(rdev, 0,
						  pi->hw.mclks[0]);
}

static void rv6xx_program_voltage_stepping_parameters_lowest_entry(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	rv6xx_program_voltage_stepping_entry(rdev, 0,
					     pi->hw.vddc[0]);

}

static void rv6xx_program_backbias_stepping_parameters_lowest_entry(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	if (pi->hw.backbias[0])
		WREG32_P(VID_UPPER_GPIO_CNTL, LOW_BACKBIAS_VALUE, ~LOW_BACKBIAS_VALUE);
	else
		WREG32_P(VID_UPPER_GPIO_CNTL, 0, ~LOW_BACKBIAS_VALUE);
}

static u32 calculate_memory_refresh_rate(struct radeon_device *rdev,
					 u32 engine_clock)
{
	u32 dram_rows, dram_refresh_rate;
	u32 tmp;

	tmp = (RREG32(RAMCFG) & NOOFROWS_MASK) >> NOOFROWS_SHIFT;
	dram_rows = 1 << (tmp + 10);
	dram_refresh_rate = 1 << ((RREG32(MC_SEQ_RESERVE_M) & 0x3) + 3);

	return ((engine_clock * 10) * dram_refresh_rate / dram_rows - 32) / 64;
}

static void rv6xx_program_memory_timing_parameters(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);
	u32 sqm_ratio;
	u32 arb_refresh_rate;
	u32 high_clock;

	if (pi->hw.sclks[R600_POWER_LEVEL_HIGH] <
	    (pi->hw.sclks[R600_POWER_LEVEL_LOW] * 0xFF / 0x40))
		high_clock = pi->hw.sclks[R600_POWER_LEVEL_HIGH];
	else
		high_clock =
			pi->hw.sclks[R600_POWER_LEVEL_LOW] * 0xFF / 0x40;

	radeon_atom_set_engine_dram_timings(rdev, high_clock, 0);

	sqm_ratio = (STATE0(64 * high_clock / pi->hw.sclks[R600_POWER_LEVEL_LOW]) |
		     STATE1(64 * high_clock / pi->hw.sclks[R600_POWER_LEVEL_MEDIUM]) |
		     STATE2(64 * high_clock / pi->hw.sclks[R600_POWER_LEVEL_HIGH]) |
		     STATE3(64 * high_clock / pi->hw.sclks[R600_POWER_LEVEL_HIGH]));
	WREG32(SQM_RATIO, sqm_ratio);

	arb_refresh_rate =
		(POWERMODE0(calculate_memory_refresh_rate(rdev,
							  pi->hw.sclks[R600_POWER_LEVEL_LOW])) |
		 POWERMODE1(calculate_memory_refresh_rate(rdev,
							  pi->hw.sclks[R600_POWER_LEVEL_MEDIUM])) |
		 POWERMODE2(calculate_memory_refresh_rate(rdev,
							  pi->hw.sclks[R600_POWER_LEVEL_MEDIUM])) |
		 POWERMODE3(calculate_memory_refresh_rate(rdev,
							  pi->hw.sclks[R600_POWER_LEVEL_HIGH])));
	WREG32(ARB_RFSH_RATE, arb_refresh_rate);
}

static void rv6xx_program_mpll_timing_parameters(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	r600_set_mpll_lock_time(rdev, R600_MPLLLOCKTIME_DFLT *
				pi->mpll_ref_div);
	r600_set_mpll_reset_time(rdev, R600_MPLLRESETTIME_DFLT);
}

static void rv6xx_program_bsp(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);
	u32 ref_clk = rdev->clock.spll.reference_freq;

	r600_calculate_u_and_p(R600_ASI_DFLT,
			       ref_clk, 16,
			       &pi->bsp,
			       &pi->bsu);

	r600_set_bsp(rdev, pi->bsu, pi->bsp);
}

static void rv6xx_program_at(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	r600_set_at(rdev,
		    (pi->hw.rp[0] * pi->bsp) / 200,
		    (pi->hw.rp[1] * pi->bsp) / 200,
		    (pi->hw.lp[2] * pi->bsp) / 200,
		    (pi->hw.lp[1] * pi->bsp) / 200);
}

static void rv6xx_program_git(struct radeon_device *rdev)
{
	r600_set_git(rdev, R600_GICST_DFLT);
}

static void rv6xx_program_tp(struct radeon_device *rdev)
{
	int i;

	for (i = 0; i < R600_PM_NUMBER_OF_TC; i++)
		r600_set_tc(rdev, i, r600_utc[i], r600_dtc[i]);

	r600_select_td(rdev, R600_TD_DFLT);
}

static void rv6xx_program_vc(struct radeon_device *rdev)
{
	r600_set_vrc(rdev, R600_VRC_DFLT);
}

static void rv6xx_clear_vc(struct radeon_device *rdev)
{
	r600_set_vrc(rdev, 0);
}

static void rv6xx_program_tpp(struct radeon_device *rdev)
{
	r600_set_tpu(rdev, R600_TPU_DFLT);
	r600_set_tpc(rdev, R600_TPC_DFLT);
}

static void rv6xx_program_sstp(struct radeon_device *rdev)
{
	r600_set_sstu(rdev, R600_SSTU_DFLT);
	r600_set_sst(rdev, R600_SST_DFLT);
}

static void rv6xx_program_fcp(struct radeon_device *rdev)
{
	r600_set_fctu(rdev, R600_FCTU_DFLT);
	r600_set_fct(rdev, R600_FCT_DFLT);
}

static void rv6xx_program_vddc3d_parameters(struct radeon_device *rdev)
{
	r600_set_vddc3d_oorsu(rdev, R600_VDDC3DOORSU_DFLT);
	r600_set_vddc3d_oorphc(rdev, R600_VDDC3DOORPHC_DFLT);
	r600_set_vddc3d_oorsdc(rdev, R600_VDDC3DOORSDC_DFLT);
	r600_set_ctxcgtt3d_rphc(rdev, R600_CTXCGTT3DRPHC_DFLT);
	r600_set_ctxcgtt3d_rsdc(rdev, R600_CTXCGTT3DRSDC_DFLT);
}

static void rv6xx_program_voltage_timing_parameters(struct radeon_device *rdev)
{
	u32 rt;

	r600_vid_rt_set_vru(rdev, R600_VRU_DFLT);

	r600_vid_rt_set_vrt(rdev,
			    rv6xx_compute_count_for_delay(rdev,
							  rdev->pm.dpm.voltage_response_time,
							  R600_VRU_DFLT));

	rt = rv6xx_compute_count_for_delay(rdev,
					   rdev->pm.dpm.backbias_response_time,
					   R600_VRU_DFLT);

	rv6xx_vid_response_set_brt(rdev, (rt + 0x1F) >> 5);
}

static void rv6xx_program_engine_speed_parameters(struct radeon_device *rdev)
{
	r600_vid_rt_set_ssu(rdev, R600_SPLLSTEPUNIT_DFLT);
	rv6xx_enable_engine_feedback_and_reference_sync(rdev);
}

static u64 rv6xx_get_master_voltage_mask(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);
	u64 master_mask = 0;
	int i;

	for (i = 0; i < R600_PM_NUMBER_OF_VOLTAGE_LEVELS; i++) {
		u32 tmp_mask, tmp_set_pins;
		int ret;

		ret = radeon_atom_get_voltage_gpio_settings(rdev,
							    pi->hw.vddc[i],
							    SET_VOLTAGE_TYPE_ASIC_VDDC,
							    &tmp_set_pins, &tmp_mask);

		if (ret == 0)
			master_mask |= tmp_mask;
	}

	return master_mask;
}

static void rv6xx_program_voltage_gpio_pins(struct radeon_device *rdev)
{
	r600_voltage_control_enable_pins(rdev,
					 rv6xx_get_master_voltage_mask(rdev));
}

static void rv6xx_enable_static_voltage_control(struct radeon_device *rdev,
						struct radeon_ps *new_ps,
						bool enable)
{
	struct rv6xx_ps *new_state = rv6xx_get_ps(new_ps);

	if (enable)
		radeon_atom_set_voltage(rdev,
					new_state->low.vddc,
					SET_VOLTAGE_TYPE_ASIC_VDDC);
	else
		r600_voltage_control_deactivate_static_control(rdev,
							       rv6xx_get_master_voltage_mask(rdev));
}

static void rv6xx_enable_display_gap(struct radeon_device *rdev, bool enable)
{
	if (enable) {
		u32 tmp = (DISP1_GAP(R600_PM_DISPLAY_GAP_VBLANK_OR_WM) |
			   DISP2_GAP(R600_PM_DISPLAY_GAP_VBLANK_OR_WM) |
			   DISP1_GAP_MCHG(R600_PM_DISPLAY_GAP_IGNORE) |
			   DISP2_GAP_MCHG(R600_PM_DISPLAY_GAP_IGNORE) |
			   VBI_TIMER_COUNT(0x3FFF) |
			   VBI_TIMER_UNIT(7));
		WREG32(CG_DISPLAY_GAP_CNTL, tmp);

		WREG32_P(MCLK_PWRMGT_CNTL, USE_DISPLAY_GAP, ~USE_DISPLAY_GAP);
	} else
		WREG32_P(MCLK_PWRMGT_CNTL, 0, ~USE_DISPLAY_GAP);
}

static void rv6xx_program_power_level_enter_state(struct radeon_device *rdev)
{
	r600_power_level_set_enter_index(rdev, R600_POWER_LEVEL_MEDIUM);
}

static void rv6xx_calculate_t(u32 l_f, u32 h_f, int h,
			      int d_l, int d_r, u8 *l, u8 *r)
{
	int a_n, a_d, h_r, l_r;

	h_r = d_l;
	l_r = 100 - d_r;

	a_n = (int)h_f * d_l + (int)l_f * (h - d_r);
	a_d = (int)l_f * l_r + (int)h_f * h_r;

	if (a_d != 0) {
		*l = d_l - h_r * a_n / a_d;
		*r = d_r + l_r * a_n / a_d;
	}
}

static void rv6xx_calculate_ap(struct radeon_device *rdev,
			       struct rv6xx_ps *state)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	pi->hw.lp[0] = 0;
	pi->hw.rp[R600_PM_NUMBER_OF_ACTIVITY_LEVELS - 1]
		= 100;

	rv6xx_calculate_t(state->low.sclk,
			  state->medium.sclk,
			  R600_AH_DFLT,
			  R600_LMP_DFLT,
			  R600_RLP_DFLT,
			  &pi->hw.lp[1],
			  &pi->hw.rp[0]);

	rv6xx_calculate_t(state->medium.sclk,
			  state->high.sclk,
			  R600_AH_DFLT,
			  R600_LHP_DFLT,
			  R600_RMP_DFLT,
			  &pi->hw.lp[2],
			  &pi->hw.rp[1]);

}

static void rv6xx_calculate_stepping_parameters(struct radeon_device *rdev,
						struct radeon_ps *new_ps)
{
	struct rv6xx_ps *new_state = rv6xx_get_ps(new_ps);

	rv6xx_calculate_engine_speed_stepping_parameters(rdev, new_state);
	rv6xx_calculate_memory_clock_stepping_parameters(rdev, new_state);
	rv6xx_calculate_voltage_stepping_parameters(rdev, new_state);
	rv6xx_calculate_ap(rdev, new_state);
}

static void rv6xx_program_stepping_parameters_except_lowest_entry(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	rv6xx_program_mclk_stepping_parameters_except_lowest_entry(rdev);
	if (pi->voltage_control)
		rv6xx_program_voltage_stepping_parameters_except_lowest_entry(rdev);
	rv6xx_program_backbias_stepping_parameters_except_lowest_entry(rdev);
	rv6xx_program_sclk_spread_spectrum_parameters_except_lowest_entry(rdev);
	rv6xx_program_mclk_spread_spectrum_parameters(rdev);
	rv6xx_program_memory_timing_parameters(rdev);
}

static void rv6xx_program_stepping_parameters_lowest_entry(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	rv6xx_program_mclk_stepping_parameters_lowest_entry(rdev);
	if (pi->voltage_control)
		rv6xx_program_voltage_stepping_parameters_lowest_entry(rdev);
	rv6xx_program_backbias_stepping_parameters_lowest_entry(rdev);
	rv6xx_program_sclk_spread_spectrum_parameters_lowest_entry(rdev);
}

static void rv6xx_program_power_level_low(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	r600_power_level_set_voltage_index(rdev, R600_POWER_LEVEL_LOW,
					   pi->hw.low_vddc_index);
	r600_power_level_set_mem_clock_index(rdev, R600_POWER_LEVEL_LOW,
					     pi->hw.low_mclk_index);
	r600_power_level_set_eng_clock_index(rdev, R600_POWER_LEVEL_LOW,
					     pi->hw.low_sclk_index);
	r600_power_level_set_watermark_id(rdev, R600_POWER_LEVEL_LOW,
					  R600_DISPLAY_WATERMARK_LOW);
	r600_power_level_set_pcie_gen2(rdev, R600_POWER_LEVEL_LOW,
				       pi->hw.pcie_gen2[R600_POWER_LEVEL_LOW]);
}

static void rv6xx_program_power_level_low_to_lowest_state(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	r600_power_level_set_voltage_index(rdev, R600_POWER_LEVEL_LOW, 0);
	r600_power_level_set_mem_clock_index(rdev, R600_POWER_LEVEL_LOW, 0);
	r600_power_level_set_eng_clock_index(rdev, R600_POWER_LEVEL_LOW, 0);

	r600_power_level_set_watermark_id(rdev, R600_POWER_LEVEL_LOW,
					  R600_DISPLAY_WATERMARK_LOW);

	r600_power_level_set_pcie_gen2(rdev, R600_POWER_LEVEL_LOW,
				       pi->hw.pcie_gen2[R600_POWER_LEVEL_LOW]);

}

static void rv6xx_program_power_level_medium(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	r600_power_level_set_voltage_index(rdev, R600_POWER_LEVEL_MEDIUM,
					  pi->hw.medium_vddc_index);
	r600_power_level_set_mem_clock_index(rdev, R600_POWER_LEVEL_MEDIUM,
					    pi->hw.medium_mclk_index);
	r600_power_level_set_eng_clock_index(rdev, R600_POWER_LEVEL_MEDIUM,
					    pi->hw.medium_sclk_index);
	r600_power_level_set_watermark_id(rdev, R600_POWER_LEVEL_MEDIUM,
					 R600_DISPLAY_WATERMARK_LOW);
	r600_power_level_set_pcie_gen2(rdev, R600_POWER_LEVEL_MEDIUM,
				      pi->hw.pcie_gen2[R600_POWER_LEVEL_MEDIUM]);
}

static void rv6xx_program_power_level_medium_for_transition(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	rv6xx_program_mclk_stepping_entry(rdev,
					  R600_POWER_LEVEL_CTXSW,
					  pi->hw.mclks[pi->hw.low_mclk_index]);

	r600_power_level_set_voltage_index(rdev, R600_POWER_LEVEL_MEDIUM, 1);

	r600_power_level_set_mem_clock_index(rdev, R600_POWER_LEVEL_MEDIUM,
					     R600_POWER_LEVEL_CTXSW);
	r600_power_level_set_eng_clock_index(rdev, R600_POWER_LEVEL_MEDIUM,
					     pi->hw.medium_sclk_index);

	r600_power_level_set_watermark_id(rdev, R600_POWER_LEVEL_MEDIUM,
					  R600_DISPLAY_WATERMARK_LOW);

	rv6xx_enable_engine_spread_spectrum(rdev, R600_POWER_LEVEL_MEDIUM, false);

	r600_power_level_set_pcie_gen2(rdev, R600_POWER_LEVEL_MEDIUM,
				       pi->hw.pcie_gen2[R600_POWER_LEVEL_LOW]);
}

static void rv6xx_program_power_level_high(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	r600_power_level_set_voltage_index(rdev, R600_POWER_LEVEL_HIGH,
					   pi->hw.high_vddc_index);
	r600_power_level_set_mem_clock_index(rdev, R600_POWER_LEVEL_HIGH,
					     pi->hw.high_mclk_index);
	r600_power_level_set_eng_clock_index(rdev, R600_POWER_LEVEL_HIGH,
					     pi->hw.high_sclk_index);

	r600_power_level_set_watermark_id(rdev, R600_POWER_LEVEL_HIGH,
					  R600_DISPLAY_WATERMARK_HIGH);

	r600_power_level_set_pcie_gen2(rdev, R600_POWER_LEVEL_HIGH,
				       pi->hw.pcie_gen2[R600_POWER_LEVEL_HIGH]);
}

static void rv6xx_enable_backbias(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32_P(GENERAL_PWRMGT, BACKBIAS_PAD_EN | BACKBIAS_DPM_CNTL,
			 ~(BACKBIAS_PAD_EN | BACKBIAS_DPM_CNTL));
	else
		WREG32_P(GENERAL_PWRMGT, 0,
			 ~(BACKBIAS_VALUE | BACKBIAS_PAD_EN | BACKBIAS_DPM_CNTL));
}

static void rv6xx_program_display_gap(struct radeon_device *rdev)
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

static void rv6xx_set_sw_voltage_to_safe(struct radeon_device *rdev,
					 struct radeon_ps *new_ps,
					 struct radeon_ps *old_ps)
{
	struct rv6xx_ps *new_state = rv6xx_get_ps(new_ps);
	struct rv6xx_ps *old_state = rv6xx_get_ps(old_ps);
	u16 safe_voltage;

	safe_voltage = (new_state->low.vddc >= old_state->low.vddc) ?
		new_state->low.vddc : old_state->low.vddc;

	rv6xx_program_voltage_stepping_entry(rdev, R600_POWER_LEVEL_CTXSW,
					     safe_voltage);

	WREG32_P(GENERAL_PWRMGT, SW_GPIO_INDEX(R600_POWER_LEVEL_CTXSW),
		 ~SW_GPIO_INDEX_MASK);
}

static void rv6xx_set_sw_voltage_to_low(struct radeon_device *rdev,
					struct radeon_ps *old_ps)
{
	struct rv6xx_ps *old_state = rv6xx_get_ps(old_ps);

	rv6xx_program_voltage_stepping_entry(rdev, R600_POWER_LEVEL_CTXSW,
					     old_state->low.vddc);

	WREG32_P(GENERAL_PWRMGT, SW_GPIO_INDEX(R600_POWER_LEVEL_CTXSW),
		~SW_GPIO_INDEX_MASK);
}

static void rv6xx_set_safe_backbias(struct radeon_device *rdev,
				    struct radeon_ps *new_ps,
				    struct radeon_ps *old_ps)
{
	struct rv6xx_ps *new_state = rv6xx_get_ps(new_ps);
	struct rv6xx_ps *old_state = rv6xx_get_ps(old_ps);

	if ((new_state->low.flags & ATOM_PPLIB_R600_FLAGS_BACKBIASENABLE) &&
	    (old_state->low.flags & ATOM_PPLIB_R600_FLAGS_BACKBIASENABLE))
		WREG32_P(GENERAL_PWRMGT, BACKBIAS_VALUE, ~BACKBIAS_VALUE);
	else
		WREG32_P(GENERAL_PWRMGT, 0, ~BACKBIAS_VALUE);
}

static void rv6xx_set_safe_pcie_gen2(struct radeon_device *rdev,
				     struct radeon_ps *new_ps,
				     struct radeon_ps *old_ps)
{
	struct rv6xx_ps *new_state = rv6xx_get_ps(new_ps);
	struct rv6xx_ps *old_state = rv6xx_get_ps(old_ps);

	if ((new_state->low.flags & ATOM_PPLIB_R600_FLAGS_PCIEGEN2) !=
	    (old_state->low.flags & ATOM_PPLIB_R600_FLAGS_PCIEGEN2))
		rv6xx_force_pcie_gen1(rdev);
}

static void rv6xx_enable_dynamic_voltage_control(struct radeon_device *rdev,
						 bool enable)
{
	if (enable)
		WREG32_P(GENERAL_PWRMGT, VOLT_PWRMGT_EN, ~VOLT_PWRMGT_EN);
	else
		WREG32_P(GENERAL_PWRMGT, 0, ~VOLT_PWRMGT_EN);
}

static void rv6xx_enable_dynamic_backbias_control(struct radeon_device *rdev,
						  bool enable)
{
	if (enable)
		WREG32_P(GENERAL_PWRMGT, BACKBIAS_DPM_CNTL, ~BACKBIAS_DPM_CNTL);
	else
		WREG32_P(GENERAL_PWRMGT, 0, ~BACKBIAS_DPM_CNTL);
}

static int rv6xx_step_sw_voltage(struct radeon_device *rdev,
				 u16 initial_voltage,
				 u16 target_voltage)
{
	u16 current_voltage;
	u16 true_target_voltage;
	u16 voltage_step;
	int signed_voltage_step;

	if ((radeon_atom_get_voltage_step(rdev, SET_VOLTAGE_TYPE_ASIC_VDDC,
					  &voltage_step)) ||
	    (radeon_atom_round_to_true_voltage(rdev, SET_VOLTAGE_TYPE_ASIC_VDDC,
					       initial_voltage, &current_voltage)) ||
	    (radeon_atom_round_to_true_voltage(rdev, SET_VOLTAGE_TYPE_ASIC_VDDC,
					       target_voltage, &true_target_voltage)))
		return -EINVAL;

	if (true_target_voltage < current_voltage)
		signed_voltage_step = -(int)voltage_step;
	else
		signed_voltage_step = voltage_step;

	while (current_voltage != true_target_voltage) {
		current_voltage += signed_voltage_step;
		rv6xx_program_voltage_stepping_entry(rdev, R600_POWER_LEVEL_CTXSW,
						     current_voltage);
		msleep((rdev->pm.dpm.voltage_response_time + 999) / 1000);
	}

	return 0;
}

static int rv6xx_step_voltage_if_increasing(struct radeon_device *rdev,
					    struct radeon_ps *new_ps,
					    struct radeon_ps *old_ps)
{
	struct rv6xx_ps *new_state = rv6xx_get_ps(new_ps);
	struct rv6xx_ps *old_state = rv6xx_get_ps(old_ps);

	if (new_state->low.vddc > old_state->low.vddc)
		return rv6xx_step_sw_voltage(rdev,
					     old_state->low.vddc,
					     new_state->low.vddc);

	return 0;
}

static int rv6xx_step_voltage_if_decreasing(struct radeon_device *rdev,
					    struct radeon_ps *new_ps,
					    struct radeon_ps *old_ps)
{
	struct rv6xx_ps *new_state = rv6xx_get_ps(new_ps);
	struct rv6xx_ps *old_state = rv6xx_get_ps(old_ps);

	if (new_state->low.vddc < old_state->low.vddc)
		return rv6xx_step_sw_voltage(rdev,
					     old_state->low.vddc,
					     new_state->low.vddc);
	else
		return 0;
}

static void rv6xx_enable_high(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	if ((pi->restricted_levels < 1) ||
	    (pi->restricted_levels == 3))
		r600_power_level_enable(rdev, R600_POWER_LEVEL_HIGH, true);
}

static void rv6xx_enable_medium(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	if (pi->restricted_levels < 2)
		r600_power_level_enable(rdev, R600_POWER_LEVEL_MEDIUM, true);
}

static void rv6xx_set_dpm_event_sources(struct radeon_device *rdev, u32 sources)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);
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

static void rv6xx_enable_auto_throttle_source(struct radeon_device *rdev,
					      enum radeon_dpm_auto_throttle_src source,
					      bool enable)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	if (enable) {
		if (!(pi->active_auto_throttle_sources & (1 << source))) {
			pi->active_auto_throttle_sources |= 1 << source;
			rv6xx_set_dpm_event_sources(rdev, pi->active_auto_throttle_sources);
		}
	} else {
		if (pi->active_auto_throttle_sources & (1 << source)) {
			pi->active_auto_throttle_sources &= ~(1 << source);
			rv6xx_set_dpm_event_sources(rdev, pi->active_auto_throttle_sources);
		}
	}
}


static void rv6xx_enable_thermal_protection(struct radeon_device *rdev,
					    bool enable)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	if (pi->active_auto_throttle_sources)
		r600_enable_thermal_protection(rdev, enable);
}

static void rv6xx_generate_transition_stepping(struct radeon_device *rdev,
					       struct radeon_ps *new_ps,
					       struct radeon_ps *old_ps)
{
	struct rv6xx_ps *new_state = rv6xx_get_ps(new_ps);
	struct rv6xx_ps *old_state = rv6xx_get_ps(old_ps);
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	rv6xx_generate_steps(rdev,
			     old_state->low.sclk,
			     new_state->low.sclk,
			     0, &pi->hw.medium_sclk_index);
}

static void rv6xx_generate_low_step(struct radeon_device *rdev,
				    struct radeon_ps *new_ps)
{
	struct rv6xx_ps *new_state = rv6xx_get_ps(new_ps);
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	pi->hw.low_sclk_index = 0;
	rv6xx_generate_single_step(rdev,
				   new_state->low.sclk,
				   0);
}

static void rv6xx_invalidate_intermediate_steps(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	rv6xx_invalidate_intermediate_steps_range(rdev, 0,
						  pi->hw.medium_sclk_index);
}

static void rv6xx_generate_stepping_table(struct radeon_device *rdev,
					  struct radeon_ps *new_ps)
{
	struct rv6xx_ps *new_state = rv6xx_get_ps(new_ps);
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);

	pi->hw.low_sclk_index = 0;

	rv6xx_generate_steps(rdev,
			     new_state->low.sclk,
			     new_state->medium.sclk,
			     0,
			     &pi->hw.medium_sclk_index);
	rv6xx_generate_steps(rdev,
			     new_state->medium.sclk,
			     new_state->high.sclk,
			     pi->hw.medium_sclk_index,
			     &pi->hw.high_sclk_index);
}

static void rv6xx_enable_spread_spectrum(struct radeon_device *rdev,
					 bool enable)
{
	if (enable)
		rv6xx_enable_dynamic_spread_spectrum(rdev, true);
	else {
		rv6xx_enable_engine_spread_spectrum(rdev, R600_POWER_LEVEL_LOW, false);
		rv6xx_enable_engine_spread_spectrum(rdev, R600_POWER_LEVEL_MEDIUM, false);
		rv6xx_enable_engine_spread_spectrum(rdev, R600_POWER_LEVEL_HIGH, false);
		rv6xx_enable_dynamic_spread_spectrum(rdev, false);
		rv6xx_enable_memory_spread_spectrum(rdev, false);
	}
}

static void rv6xx_reset_lvtm_data_sync(struct radeon_device *rdev)
{
	if (ASIC_IS_DCE3(rdev))
		WREG32_P(DCE3_LVTMA_DATA_SYNCHRONIZATION, LVTMA_PFREQCHG, ~LVTMA_PFREQCHG);
	else
		WREG32_P(LVTMA_DATA_SYNCHRONIZATION, LVTMA_PFREQCHG, ~LVTMA_PFREQCHG);
}

static void rv6xx_enable_dynamic_pcie_gen2(struct radeon_device *rdev,
					   struct radeon_ps *new_ps,
					   bool enable)
{
	struct rv6xx_ps *new_state = rv6xx_get_ps(new_ps);

	if (enable) {
		rv6xx_enable_bif_dynamic_pcie_gen2(rdev, true);
		rv6xx_enable_pcie_gen2_support(rdev);
		r600_enable_dynamic_pcie_gen2(rdev, true);
	} else {
		if (!(new_state->low.flags & ATOM_PPLIB_R600_FLAGS_PCIEGEN2))
			rv6xx_force_pcie_gen1(rdev);
		rv6xx_enable_bif_dynamic_pcie_gen2(rdev, false);
		r600_enable_dynamic_pcie_gen2(rdev, false);
	}
}

int rv6xx_dpm_enable(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);
	struct radeon_ps *boot_ps = rdev->pm.dpm.boot_ps;

	if (r600_dynamicpm_enabled(rdev))
		return -EINVAL;

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_BACKBIAS)
		rv6xx_enable_backbias(rdev, true);

	if (pi->dynamic_ss)
		rv6xx_enable_spread_spectrum(rdev, true);

	rv6xx_program_mpll_timing_parameters(rdev);
	rv6xx_program_bsp(rdev);
	rv6xx_program_git(rdev);
	rv6xx_program_tp(rdev);
	rv6xx_program_tpp(rdev);
	rv6xx_program_sstp(rdev);
	rv6xx_program_fcp(rdev);
	rv6xx_program_vddc3d_parameters(rdev);
	rv6xx_program_voltage_timing_parameters(rdev);
	rv6xx_program_engine_speed_parameters(rdev);

	rv6xx_enable_display_gap(rdev, true);
	if (pi->display_gap == false)
		rv6xx_enable_display_gap(rdev, false);

	rv6xx_program_power_level_enter_state(rdev);

	rv6xx_calculate_stepping_parameters(rdev, boot_ps);

	if (pi->voltage_control)
		rv6xx_program_voltage_gpio_pins(rdev);

	rv6xx_generate_stepping_table(rdev, boot_ps);

	rv6xx_program_stepping_parameters_except_lowest_entry(rdev);
	rv6xx_program_stepping_parameters_lowest_entry(rdev);

	rv6xx_program_power_level_low(rdev);
	rv6xx_program_power_level_medium(rdev);
	rv6xx_program_power_level_high(rdev);
	rv6xx_program_vc(rdev);
	rv6xx_program_at(rdev);

	r600_power_level_enable(rdev, R600_POWER_LEVEL_LOW, true);
	r600_power_level_enable(rdev, R600_POWER_LEVEL_MEDIUM, true);
	r600_power_level_enable(rdev, R600_POWER_LEVEL_HIGH, true);

	if (rdev->irq.installed &&
	    r600_is_internal_thermal_sensor(rdev->pm.int_thermal_type)) {
		r600_set_thermal_temperature_range(rdev, R600_TEMP_RANGE_MIN, R600_TEMP_RANGE_MAX);
		rdev->irq.dpm_thermal = true;
		radeon_irq_set(rdev);
	}

	rv6xx_enable_auto_throttle_source(rdev, RADEON_DPM_AUTO_THROTTLE_SRC_THERMAL, true);

	r600_start_dpm(rdev);

	if (pi->voltage_control)
		rv6xx_enable_static_voltage_control(rdev, boot_ps, false);

	if (pi->dynamic_pcie_gen2)
		rv6xx_enable_dynamic_pcie_gen2(rdev, boot_ps, true);

	if (pi->gfx_clock_gating)
		r600_gfx_clockgating_enable(rdev, true);

	return 0;
}

void rv6xx_dpm_disable(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);
	struct radeon_ps *boot_ps = rdev->pm.dpm.boot_ps;

	if (!r600_dynamicpm_enabled(rdev))
		return;

	r600_power_level_enable(rdev, R600_POWER_LEVEL_LOW, true);
	r600_power_level_enable(rdev, R600_POWER_LEVEL_MEDIUM, true);
	rv6xx_enable_display_gap(rdev, false);
	rv6xx_clear_vc(rdev);
	r600_set_at(rdev, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF);

	if (pi->thermal_protection)
		r600_enable_thermal_protection(rdev, false);

	r600_wait_for_power_level(rdev, R600_POWER_LEVEL_LOW);
	r600_power_level_enable(rdev, R600_POWER_LEVEL_HIGH, false);
	r600_power_level_enable(rdev, R600_POWER_LEVEL_MEDIUM, false);

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_BACKBIAS)
		rv6xx_enable_backbias(rdev, false);

	rv6xx_enable_spread_spectrum(rdev, false);

	if (pi->voltage_control)
		rv6xx_enable_static_voltage_control(rdev, boot_ps, true);

	if (pi->dynamic_pcie_gen2)
		rv6xx_enable_dynamic_pcie_gen2(rdev, boot_ps, false);

	if (rdev->irq.installed &&
	    r600_is_internal_thermal_sensor(rdev->pm.int_thermal_type)) {
		rdev->irq.dpm_thermal = false;
		radeon_irq_set(rdev);
	}

	if (pi->gfx_clock_gating)
		r600_gfx_clockgating_enable(rdev, false);

	r600_stop_dpm(rdev);
}

int rv6xx_dpm_set_power_state(struct radeon_device *rdev)
{
	struct rv6xx_power_info *pi = rv6xx_get_pi(rdev);
	struct radeon_ps *new_ps = rdev->pm.dpm.requested_ps;
	struct radeon_ps *old_ps = rdev->pm.dpm.current_ps;

	rv6xx_clear_vc(rdev);
	r600_power_level_enable(rdev, R600_POWER_LEVEL_LOW, true);
	r600_set_at(rdev, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF);

	if (pi->thermal_protection)
		r600_enable_thermal_protection(rdev, false);

	r600_wait_for_power_level(rdev, R600_POWER_LEVEL_LOW);
	r600_power_level_enable(rdev, R600_POWER_LEVEL_HIGH, false);
	r600_power_level_enable(rdev, R600_POWER_LEVEL_MEDIUM, false);

	rv6xx_generate_transition_stepping(rdev, new_ps, old_ps);
	rv6xx_program_power_level_medium_for_transition(rdev);

	if (pi->voltage_control) {
		rv6xx_set_sw_voltage_to_safe(rdev, new_ps, old_ps);
		if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_STEPVDDC)
			rv6xx_set_sw_voltage_to_low(rdev, old_ps);
	}

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_BACKBIAS)
		rv6xx_set_safe_backbias(rdev, new_ps, old_ps);

	if (pi->dynamic_pcie_gen2)
		rv6xx_set_safe_pcie_gen2(rdev, new_ps, old_ps);

	if (pi->voltage_control)
		rv6xx_enable_dynamic_voltage_control(rdev, false);

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_BACKBIAS)
		rv6xx_enable_dynamic_backbias_control(rdev, false);

	if (pi->voltage_control) {
		if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_STEPVDDC)
			rv6xx_step_voltage_if_increasing(rdev, new_ps, old_ps);
		msleep((rdev->pm.dpm.voltage_response_time + 999) / 1000);
	}

	r600_power_level_enable(rdev, R600_POWER_LEVEL_MEDIUM, true);
	r600_power_level_enable(rdev, R600_POWER_LEVEL_LOW, false);
	r600_wait_for_power_level_unequal(rdev, R600_POWER_LEVEL_LOW);

	rv6xx_generate_low_step(rdev, new_ps);
	rv6xx_invalidate_intermediate_steps(rdev);
	rv6xx_calculate_stepping_parameters(rdev, new_ps);
	rv6xx_program_stepping_parameters_lowest_entry(rdev);
	rv6xx_program_power_level_low_to_lowest_state(rdev);

	r600_power_level_enable(rdev, R600_POWER_LEVEL_LOW, true);
	r600_wait_for_power_level(rdev, R600_POWER_LEVEL_LOW);
	r600_power_level_enable(rdev, R600_POWER_LEVEL_MEDIUM, false);

	if (pi->voltage_control) {
		if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_STEPVDDC)
			rv6xx_step_voltage_if_decreasing(rdev, new_ps, old_ps);
		rv6xx_enable_dynamic_voltage_control(rdev, true);
	}

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_BACKBIAS)
		rv6xx_enable_dynamic_backbias_control(rdev, true);

	if (pi->dynamic_pcie_gen2)
		rv6xx_enable_dynamic_pcie_gen2(rdev, new_ps, true);

	rv6xx_reset_lvtm_data_sync(rdev);

	rv6xx_generate_stepping_table(rdev, new_ps);
	rv6xx_program_stepping_parameters_except_lowest_entry(rdev);
	rv6xx_program_power_level_low(rdev);
	rv6xx_program_power_level_medium(rdev);
	rv6xx_program_power_level_high(rdev);
	rv6xx_enable_medium(rdev);
	rv6xx_enable_high(rdev);

	if (pi->thermal_protection)
		rv6xx_enable_thermal_protection(rdev, true);
	rv6xx_program_vc(rdev);
	rv6xx_program_at(rdev);

	return 0;
}

void rv6xx_setup_asic(struct radeon_device *rdev)
{
	r600_enable_acpi_pm(rdev);

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_ASPM_L0s)
		rv6xx_enable_l0s(rdev);
	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_ASPM_L1)
		rv6xx_enable_l1(rdev);
	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_TURNOFFPLL_ASPML1)
		rv6xx_enable_pll_sleep_in_l1(rdev);
}

void rv6xx_dpm_display_configuration_changed(struct radeon_device *rdev)
{
	rv6xx_program_display_gap(rdev);
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

static void rv6xx_parse_pplib_non_clock_info(struct radeon_device *rdev,
					     struct radeon_ps *rps,
					     struct _ATOM_PPLIB_NONCLOCK_INFO *non_clock_info)
{
	rps->caps = le32_to_cpu(non_clock_info->ulCapsAndSettings);
	rps->class = le16_to_cpu(non_clock_info->usClassification);
	rps->class2 = le16_to_cpu(non_clock_info->usClassification2);

	if (r600_is_uvd_state(rps->class, rps->class2)) {
		rps->vclk = RV6XX_DEFAULT_VCLK_FREQ;
		rps->dclk = RV6XX_DEFAULT_DCLK_FREQ;
	} else {
		rps->vclk = 0;
		rps->dclk = 0;
	}

	if (rps->class & ATOM_PPLIB_CLASSIFICATION_BOOT)
		rdev->pm.dpm.boot_ps = rps;
	if (rps->class & ATOM_PPLIB_CLASSIFICATION_UVDSTATE)
		rdev->pm.dpm.uvd_ps = rps;
}

static void rv6xx_parse_pplib_clock_info(struct radeon_device *rdev,
					 struct radeon_ps *rps, int index,
					 union pplib_clock_info *clock_info)
{
	struct rv6xx_ps *ps = rv6xx_get_ps(rps);
	u32 sclk, mclk;
	u16 vddc;
	struct rv6xx_pl *pl;

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

	sclk = le16_to_cpu(clock_info->r600.usEngineClockLow);
	sclk |= clock_info->r600.ucEngineClockHigh << 16;
	mclk = le16_to_cpu(clock_info->r600.usMemoryClockLow);
	mclk |= clock_info->r600.ucMemoryClockHigh << 16;

	pl->mclk = mclk;
	pl->sclk = sclk;
	pl->vddc = le16_to_cpu(clock_info->r600.usVDDC);
	pl->flags = le32_to_cpu(clock_info->r600.ulFlags);

	/* patch up vddc if necessary */
	if (pl->vddc == 0xff01) {
		if (radeon_atom_get_max_vddc(rdev, 0, 0, &vddc) == 0)
			pl->vddc = vddc;
	}

	/* fix up pcie gen2 */
	if (pl->flags & ATOM_PPLIB_R600_FLAGS_PCIEGEN2) {
		if ((rdev->family == CHIP_RV610) || (rdev->family == CHIP_RV630)) {
			if (pl->vddc < 1100)
				pl->flags &= ~ATOM_PPLIB_R600_FLAGS_PCIEGEN2;
		}
	}

	/* patch up boot state */
	if (rps->class & ATOM_PPLIB_CLASSIFICATION_BOOT) {
		u16 vddc, vddci;
		radeon_atombios_get_default_voltages(rdev, &vddc, &vddci);
		pl->mclk = rdev->clock.default_mclk;
		pl->sclk = rdev->clock.default_sclk;
		pl->vddc = vddc;
	}
}

static int rv6xx_parse_power_table(struct radeon_device *rdev)
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
	struct rv6xx_ps *ps;

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
			ps = kzalloc(sizeof(struct rv6xx_ps), GFP_KERNEL);
			if (ps == NULL) {
				kfree(rdev->pm.dpm.ps);
				return -ENOMEM;
			}
			rdev->pm.dpm.ps[i].ps_priv = ps;
			rv6xx_parse_pplib_non_clock_info(rdev, &rdev->pm.dpm.ps[i],
							 non_clock_info);
			for (j = 0; j < (power_info->pplib.ucStateEntrySize - 1); j++) {
				clock_info = (union pplib_clock_info *)
					(mode_info->atom_context->bios + data_offset +
					 le16_to_cpu(power_info->pplib.usClockInfoArrayOffset) +
					 (power_state->v1.ucClockStateIndices[j] *
					  power_info->pplib.ucClockInfoSize));
				rv6xx_parse_pplib_clock_info(rdev,
							     &rdev->pm.dpm.ps[i], j,
							     clock_info);
			}
		}
	}
	rdev->pm.dpm.num_ps = power_info->pplib.ucNumStates;
	return 0;
}

int rv6xx_dpm_init(struct radeon_device *rdev)
{
	int index = GetIndexIntoMasterTable(DATA, ASIC_InternalSS_Info);
	uint16_t data_offset, size;
	uint8_t frev, crev;
	struct atom_clock_dividers dividers;
	struct rv6xx_power_info *pi;
	int ret;

	pi = kzalloc(sizeof(struct rv6xx_power_info), GFP_KERNEL);
	if (pi == NULL)
		return -ENOMEM;
	rdev->pm.dpm.priv = pi;

	ret = rv6xx_parse_power_table(rdev);
	if (ret)
		return ret;

	if (rdev->pm.dpm.voltage_response_time == 0)
		rdev->pm.dpm.voltage_response_time = R600_VOLTAGERESPONSETIME_DFLT;
	if (rdev->pm.dpm.backbias_response_time == 0)
		rdev->pm.dpm.backbias_response_time = R600_BACKBIASRESPONSETIME_DFLT;

	ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
					     0, false, &dividers);
	if (ret)
		pi->spll_ref_div = dividers.ref_div + 1;
	else
		pi->spll_ref_div = R600_REFERENCEDIVIDER_DFLT;

	ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_MEMORY_PLL_PARAM,
					     0, false, &dividers);
	if (ret)
		pi->mpll_ref_div = dividers.ref_div + 1;
	else
		pi->mpll_ref_div = R600_REFERENCEDIVIDER_DFLT;

	if (rdev->family >= CHIP_RV670)
		pi->fb_div_scale = 1;
	else
		pi->fb_div_scale = 0;

	pi->voltage_control =
		radeon_atom_is_voltage_gpio(rdev, SET_VOLTAGE_TYPE_ASIC_VDDC);

	pi->gfx_clock_gating = true;

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

	pi->dynamic_pcie_gen2 = true;

	if (pi->gfx_clock_gating &&
	    (rdev->pm.int_thermal_type != THERMAL_TYPE_NONE))
		pi->thermal_protection = true;
	else
		pi->thermal_protection = false;

	pi->display_gap = true;

	return 0;
}

void rv6xx_dpm_print_power_state(struct radeon_device *rdev,
				 struct radeon_ps *rps)
{
	struct rv6xx_ps *ps = rv6xx_get_ps(rps);
	struct rv6xx_pl *pl;

	r600_dpm_print_class_info(rps->class, rps->class2);
	r600_dpm_print_cap_info(rps->caps);
	printk("\tuvd    vclk: %d dclk: %d\n", rps->vclk, rps->dclk);
	pl = &ps->low;
	printk("\t\tpower level 0    sclk: %u mclk: %u vddc: %u\n",
	       pl->sclk, pl->mclk, pl->vddc);
	pl = &ps->medium;
	printk("\t\tpower level 1    sclk: %u mclk: %u vddc: %u\n",
	       pl->sclk, pl->mclk, pl->vddc);
	pl = &ps->high;
	printk("\t\tpower level 2    sclk: %u mclk: %u vddc: %u\n",
	       pl->sclk, pl->mclk, pl->vddc);
	r600_dpm_print_ps_status(rdev, rps);
}

void rv6xx_dpm_fini(struct radeon_device *rdev)
{
	int i;

	for (i = 0; i < rdev->pm.dpm.num_ps; i++) {
		kfree(rdev->pm.dpm.ps[i].ps_priv);
	}
	kfree(rdev->pm.dpm.ps);
	kfree(rdev->pm.dpm.priv);
}

u32 rv6xx_dpm_get_sclk(struct radeon_device *rdev, bool low)
{
	struct rv6xx_ps *requested_state = rv6xx_get_ps(rdev->pm.dpm.requested_ps);

	if (low)
		return requested_state->low.sclk;
	else
		return requested_state->high.sclk;
}

u32 rv6xx_dpm_get_mclk(struct radeon_device *rdev, bool low)
{
	struct rv6xx_ps *requested_state = rv6xx_get_ps(rdev->pm.dpm.requested_ps);

	if (low)
		return requested_state->low.mclk;
	else
		return requested_state->high.mclk;
}
