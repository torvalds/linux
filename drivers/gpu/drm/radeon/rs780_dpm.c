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
#include "rs780d.h"
#include "r600_dpm.h"
#include "rs780_dpm.h"
#include "atom.h"

static struct igp_ps *rs780_get_ps(struct radeon_ps *rps)
{
	struct igp_ps *ps = rps->ps_priv;

	return ps;
}

static struct igp_power_info *rs780_get_pi(struct radeon_device *rdev)
{
	struct igp_power_info *pi = rdev->pm.dpm.priv;

	return pi;
}

static void rs780_get_pm_mode_parameters(struct radeon_device *rdev)
{
	struct igp_power_info *pi = rs780_get_pi(rdev);
	struct radeon_mode_info *minfo = &rdev->mode_info;
	struct drm_crtc *crtc;
	struct radeon_crtc *radeon_crtc;
	int i;

	/* defaults */
	pi->crtc_id = 0;
	pi->refresh_rate = 60;

	for (i = 0; i < rdev->num_crtc; i++) {
		crtc = (struct drm_crtc *)minfo->crtcs[i];
		if (crtc && crtc->enabled) {
			radeon_crtc = to_radeon_crtc(crtc);
			pi->crtc_id = radeon_crtc->crtc_id;
			if (crtc->mode.htotal && crtc->mode.vtotal)
				pi->refresh_rate =
					(crtc->mode.clock * 1000) /
					(crtc->mode.htotal * crtc->mode.vtotal);
			break;
		}
	}
}

static void rs780_voltage_scaling_enable(struct radeon_device *rdev, bool enable);

static int rs780_initialize_dpm_power_state(struct radeon_device *rdev,
					    struct radeon_ps *boot_ps)
{
	struct atom_clock_dividers dividers;
	struct igp_ps *default_state = rs780_get_ps(boot_ps);
	int i, ret;

	ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
					     default_state->sclk_low, false, &dividers);
	if (ret)
		return ret;

	r600_engine_clock_entry_set_reference_divider(rdev, 0, dividers.ref_div);
	r600_engine_clock_entry_set_feedback_divider(rdev, 0, dividers.fb_div);
	r600_engine_clock_entry_set_post_divider(rdev, 0, dividers.post_div);

	if (dividers.enable_post_div)
		r600_engine_clock_entry_enable_post_divider(rdev, 0, true);
	else
		r600_engine_clock_entry_enable_post_divider(rdev, 0, false);

	r600_engine_clock_entry_set_step_time(rdev, 0, R600_SST_DFLT);
	r600_engine_clock_entry_enable_pulse_skipping(rdev, 0, false);

	r600_engine_clock_entry_enable(rdev, 0, true);
	for (i = 1; i < R600_PM_NUMBER_OF_SCLKS; i++)
		r600_engine_clock_entry_enable(rdev, i, false);

	r600_enable_mclk_control(rdev, false);
	r600_voltage_control_enable_pins(rdev, 0);

	return 0;
}

static int rs780_initialize_dpm_parameters(struct radeon_device *rdev,
					   struct radeon_ps *boot_ps)
{
	int ret = 0;
	int i;

	r600_set_bsp(rdev, R600_BSU_DFLT, R600_BSP_DFLT);

	r600_set_at(rdev, 0, 0, 0, 0);

	r600_set_git(rdev, R600_GICST_DFLT);

	for (i = 0; i < R600_PM_NUMBER_OF_TC; i++)
		r600_set_tc(rdev, i, 0, 0);

	r600_select_td(rdev, R600_TD_DFLT);
	r600_set_vrc(rdev, 0);

	r600_set_tpu(rdev, R600_TPU_DFLT);
	r600_set_tpc(rdev, R600_TPC_DFLT);

	r600_set_sstu(rdev, R600_SSTU_DFLT);
	r600_set_sst(rdev, R600_SST_DFLT);

	r600_set_fctu(rdev, R600_FCTU_DFLT);
	r600_set_fct(rdev, R600_FCT_DFLT);

	r600_set_vddc3d_oorsu(rdev, R600_VDDC3DOORSU_DFLT);
	r600_set_vddc3d_oorphc(rdev, R600_VDDC3DOORPHC_DFLT);
	r600_set_vddc3d_oorsdc(rdev, R600_VDDC3DOORSDC_DFLT);
	r600_set_ctxcgtt3d_rphc(rdev, R600_CTXCGTT3DRPHC_DFLT);
	r600_set_ctxcgtt3d_rsdc(rdev, R600_CTXCGTT3DRSDC_DFLT);

	r600_vid_rt_set_vru(rdev, R600_VRU_DFLT);
	r600_vid_rt_set_vrt(rdev, R600_VOLTAGERESPONSETIME_DFLT);
	r600_vid_rt_set_ssu(rdev, R600_SPLLSTEPUNIT_DFLT);

	ret = rs780_initialize_dpm_power_state(rdev, boot_ps);

	r600_power_level_set_voltage_index(rdev, R600_POWER_LEVEL_LOW,     0);
	r600_power_level_set_voltage_index(rdev, R600_POWER_LEVEL_MEDIUM,  0);
	r600_power_level_set_voltage_index(rdev, R600_POWER_LEVEL_HIGH,    0);

	r600_power_level_set_mem_clock_index(rdev, R600_POWER_LEVEL_LOW,    0);
	r600_power_level_set_mem_clock_index(rdev, R600_POWER_LEVEL_MEDIUM, 0);
	r600_power_level_set_mem_clock_index(rdev, R600_POWER_LEVEL_HIGH,   0);

	r600_power_level_set_eng_clock_index(rdev, R600_POWER_LEVEL_LOW,    0);
	r600_power_level_set_eng_clock_index(rdev, R600_POWER_LEVEL_MEDIUM, 0);
	r600_power_level_set_eng_clock_index(rdev, R600_POWER_LEVEL_HIGH,   0);

	r600_power_level_set_watermark_id(rdev, R600_POWER_LEVEL_LOW,    R600_DISPLAY_WATERMARK_HIGH);
	r600_power_level_set_watermark_id(rdev, R600_POWER_LEVEL_MEDIUM, R600_DISPLAY_WATERMARK_HIGH);
	r600_power_level_set_watermark_id(rdev, R600_POWER_LEVEL_HIGH,   R600_DISPLAY_WATERMARK_HIGH);

	r600_power_level_enable(rdev, R600_POWER_LEVEL_CTXSW, false);
	r600_power_level_enable(rdev, R600_POWER_LEVEL_HIGH, false);
	r600_power_level_enable(rdev, R600_POWER_LEVEL_MEDIUM, false);
	r600_power_level_enable(rdev, R600_POWER_LEVEL_LOW, true);

	r600_power_level_set_enter_index(rdev, R600_POWER_LEVEL_LOW);

	r600_set_vrc(rdev, RS780_CGFTV_DFLT);

	return ret;
}

static void rs780_start_dpm(struct radeon_device *rdev)
{
	r600_enable_sclk_control(rdev, false);
	r600_enable_mclk_control(rdev, false);

	r600_dynamicpm_enable(rdev, true);

	radeon_wait_for_vblank(rdev, 0);
	radeon_wait_for_vblank(rdev, 1);

	r600_enable_spll_bypass(rdev, true);
	r600_wait_for_spll_change(rdev);
	r600_enable_spll_bypass(rdev, false);
	r600_wait_for_spll_change(rdev);

	r600_enable_spll_bypass(rdev, true);
	r600_wait_for_spll_change(rdev);
	r600_enable_spll_bypass(rdev, false);
	r600_wait_for_spll_change(rdev);

	r600_enable_sclk_control(rdev, true);
}


static void rs780_preset_ranges_slow_clk_fbdiv_en(struct radeon_device *rdev)
{
	WREG32_P(FVTHROT_SLOW_CLK_FEEDBACK_DIV_REG1, RANGE_SLOW_CLK_FEEDBACK_DIV_EN,
		 ~RANGE_SLOW_CLK_FEEDBACK_DIV_EN);

	WREG32_P(FVTHROT_SLOW_CLK_FEEDBACK_DIV_REG1,
		 RANGE0_SLOW_CLK_FEEDBACK_DIV(RS780_SLOWCLKFEEDBACKDIV_DFLT),
		 ~RANGE0_SLOW_CLK_FEEDBACK_DIV_MASK);
}

static void rs780_preset_starting_fbdiv(struct radeon_device *rdev)
{
	u32 fbdiv = (RREG32(CG_SPLL_FUNC_CNTL) & SPLL_FB_DIV_MASK) >> SPLL_FB_DIV_SHIFT;

	WREG32_P(FVTHROT_FBDIV_REG1, STARTING_FEEDBACK_DIV(fbdiv),
		 ~STARTING_FEEDBACK_DIV_MASK);

	WREG32_P(FVTHROT_FBDIV_REG2, FORCED_FEEDBACK_DIV(fbdiv),
		 ~FORCED_FEEDBACK_DIV_MASK);

	WREG32_P(FVTHROT_FBDIV_REG1, FORCE_FEEDBACK_DIV, ~FORCE_FEEDBACK_DIV);
}

static void rs780_voltage_scaling_init(struct radeon_device *rdev)
{
	struct igp_power_info *pi = rs780_get_pi(rdev);
	struct drm_device *dev = rdev->ddev;
	u32 fv_throt_pwm_fb_div_range[3];
	u32 fv_throt_pwm_range[4];

	if (dev->pdev->device == 0x9614) {
		fv_throt_pwm_fb_div_range[0] = RS780D_FVTHROTPWMFBDIVRANGEREG0_DFLT;
		fv_throt_pwm_fb_div_range[1] = RS780D_FVTHROTPWMFBDIVRANGEREG1_DFLT;
		fv_throt_pwm_fb_div_range[2] = RS780D_FVTHROTPWMFBDIVRANGEREG2_DFLT;
	} else if ((dev->pdev->device == 0x9714) ||
		   (dev->pdev->device == 0x9715)) {
		fv_throt_pwm_fb_div_range[0] = RS880D_FVTHROTPWMFBDIVRANGEREG0_DFLT;
		fv_throt_pwm_fb_div_range[1] = RS880D_FVTHROTPWMFBDIVRANGEREG1_DFLT;
		fv_throt_pwm_fb_div_range[2] = RS880D_FVTHROTPWMFBDIVRANGEREG2_DFLT;
	} else {
		fv_throt_pwm_fb_div_range[0] = RS780_FVTHROTPWMFBDIVRANGEREG0_DFLT;
		fv_throt_pwm_fb_div_range[1] = RS780_FVTHROTPWMFBDIVRANGEREG1_DFLT;
		fv_throt_pwm_fb_div_range[2] = RS780_FVTHROTPWMFBDIVRANGEREG2_DFLT;
	}

	if (pi->pwm_voltage_control) {
		fv_throt_pwm_range[0] = pi->min_voltage;
		fv_throt_pwm_range[1] = pi->min_voltage;
		fv_throt_pwm_range[2] = pi->max_voltage;
		fv_throt_pwm_range[3] = pi->max_voltage;
	} else {
		fv_throt_pwm_range[0] = pi->invert_pwm_required ?
			RS780_FVTHROTPWMRANGE3_GPIO_DFLT : RS780_FVTHROTPWMRANGE0_GPIO_DFLT;
		fv_throt_pwm_range[1] = pi->invert_pwm_required ?
			RS780_FVTHROTPWMRANGE2_GPIO_DFLT : RS780_FVTHROTPWMRANGE1_GPIO_DFLT;
		fv_throt_pwm_range[2] = pi->invert_pwm_required ?
			RS780_FVTHROTPWMRANGE1_GPIO_DFLT : RS780_FVTHROTPWMRANGE2_GPIO_DFLT;
		fv_throt_pwm_range[3] = pi->invert_pwm_required ?
			RS780_FVTHROTPWMRANGE0_GPIO_DFLT : RS780_FVTHROTPWMRANGE3_GPIO_DFLT;
	}

	WREG32_P(FVTHROT_PWM_CTRL_REG0,
		 STARTING_PWM_HIGHTIME(pi->max_voltage),
		 ~STARTING_PWM_HIGHTIME_MASK);

	WREG32_P(FVTHROT_PWM_CTRL_REG0,
		 NUMBER_OF_CYCLES_IN_PERIOD(pi->num_of_cycles_in_period),
		 ~NUMBER_OF_CYCLES_IN_PERIOD_MASK);

	WREG32_P(FVTHROT_PWM_CTRL_REG0, FORCE_STARTING_PWM_HIGHTIME,
		 ~FORCE_STARTING_PWM_HIGHTIME);

	if (pi->invert_pwm_required)
		WREG32_P(FVTHROT_PWM_CTRL_REG0, INVERT_PWM_WAVEFORM, ~INVERT_PWM_WAVEFORM);
	else
		WREG32_P(FVTHROT_PWM_CTRL_REG0, 0, ~INVERT_PWM_WAVEFORM);

	rs780_voltage_scaling_enable(rdev, true);

	WREG32(FVTHROT_PWM_CTRL_REG1,
	       (MIN_PWM_HIGHTIME(pi->min_voltage) |
		MAX_PWM_HIGHTIME(pi->max_voltage)));

	WREG32(FVTHROT_PWM_US_REG0, RS780_FVTHROTPWMUSREG0_DFLT);
	WREG32(FVTHROT_PWM_US_REG1, RS780_FVTHROTPWMUSREG1_DFLT);
	WREG32(FVTHROT_PWM_DS_REG0, RS780_FVTHROTPWMDSREG0_DFLT);
	WREG32(FVTHROT_PWM_DS_REG1, RS780_FVTHROTPWMDSREG1_DFLT);

	WREG32_P(FVTHROT_PWM_FEEDBACK_DIV_REG1,
		 RANGE0_PWM_FEEDBACK_DIV(fv_throt_pwm_fb_div_range[0]),
		 ~RANGE0_PWM_FEEDBACK_DIV_MASK);

	WREG32(FVTHROT_PWM_FEEDBACK_DIV_REG2,
	       (RANGE1_PWM_FEEDBACK_DIV(fv_throt_pwm_fb_div_range[1]) |
		RANGE2_PWM_FEEDBACK_DIV(fv_throt_pwm_fb_div_range[2])));

	WREG32(FVTHROT_PWM_FEEDBACK_DIV_REG3,
	       (RANGE0_PWM(fv_throt_pwm_range[1]) |
		RANGE1_PWM(fv_throt_pwm_range[2])));
	WREG32(FVTHROT_PWM_FEEDBACK_DIV_REG4,
	       (RANGE2_PWM(fv_throt_pwm_range[1]) |
		RANGE3_PWM(fv_throt_pwm_range[2])));
}

static void rs780_clk_scaling_enable(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32_P(FVTHROT_CNTRL_REG, ENABLE_FV_THROT | ENABLE_FV_UPDATE,
			 ~(ENABLE_FV_THROT | ENABLE_FV_UPDATE));
	else
		WREG32_P(FVTHROT_CNTRL_REG, 0,
			 ~(ENABLE_FV_THROT | ENABLE_FV_UPDATE));
}

static void rs780_voltage_scaling_enable(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32_P(FVTHROT_CNTRL_REG, ENABLE_FV_THROT_IO, ~ENABLE_FV_THROT_IO);
	else
		WREG32_P(FVTHROT_CNTRL_REG, 0, ~ENABLE_FV_THROT_IO);
}

static void rs780_set_engine_clock_wfc(struct radeon_device *rdev)
{
	WREG32(FVTHROT_UTC0, RS780_FVTHROTUTC0_DFLT);
	WREG32(FVTHROT_UTC1, RS780_FVTHROTUTC1_DFLT);
	WREG32(FVTHROT_UTC2, RS780_FVTHROTUTC2_DFLT);
	WREG32(FVTHROT_UTC3, RS780_FVTHROTUTC3_DFLT);
	WREG32(FVTHROT_UTC4, RS780_FVTHROTUTC4_DFLT);

	WREG32(FVTHROT_DTC0, RS780_FVTHROTDTC0_DFLT);
	WREG32(FVTHROT_DTC1, RS780_FVTHROTDTC1_DFLT);
	WREG32(FVTHROT_DTC2, RS780_FVTHROTDTC2_DFLT);
	WREG32(FVTHROT_DTC3, RS780_FVTHROTDTC3_DFLT);
	WREG32(FVTHROT_DTC4, RS780_FVTHROTDTC4_DFLT);
}

static void rs780_set_engine_clock_sc(struct radeon_device *rdev)
{
	WREG32_P(FVTHROT_FBDIV_REG2,
		 FB_DIV_TIMER_VAL(RS780_FBDIVTIMERVAL_DFLT),
		 ~FB_DIV_TIMER_VAL_MASK);

	WREG32_P(FVTHROT_CNTRL_REG,
		 REFRESH_RATE_DIVISOR(0) | MINIMUM_CIP(0xf),
		 ~(REFRESH_RATE_DIVISOR_MASK | MINIMUM_CIP_MASK));
}

static void rs780_set_engine_clock_tdc(struct radeon_device *rdev)
{
	WREG32_P(FVTHROT_CNTRL_REG, 0, ~(FORCE_TREND_SEL | TREND_SEL_MODE));
}

static void rs780_set_engine_clock_ssc(struct radeon_device *rdev)
{
	WREG32(FVTHROT_FB_US_REG0, RS780_FVTHROTFBUSREG0_DFLT);
	WREG32(FVTHROT_FB_US_REG1, RS780_FVTHROTFBUSREG1_DFLT);
	WREG32(FVTHROT_FB_DS_REG0, RS780_FVTHROTFBDSREG0_DFLT);
	WREG32(FVTHROT_FB_DS_REG1, RS780_FVTHROTFBDSREG1_DFLT);

	WREG32_P(FVTHROT_FBDIV_REG1, MAX_FEEDBACK_STEP(1), ~MAX_FEEDBACK_STEP_MASK);
}

static void rs780_program_at(struct radeon_device *rdev)
{
	struct igp_power_info *pi = rs780_get_pi(rdev);

	WREG32(FVTHROT_TARGET_REG, 30000000 / pi->refresh_rate);
	WREG32(FVTHROT_CB1, 1000000 * 5 / pi->refresh_rate);
	WREG32(FVTHROT_CB2, 1000000 * 10 / pi->refresh_rate);
	WREG32(FVTHROT_CB3, 1000000 * 30 / pi->refresh_rate);
	WREG32(FVTHROT_CB4, 1000000 * 50 / pi->refresh_rate);
}

static void rs780_disable_vbios_powersaving(struct radeon_device *rdev)
{
	WREG32_P(CG_INTGFX_MISC, 0, ~0xFFF00000);
}

static void rs780_force_voltage_to_high(struct radeon_device *rdev)
{
	struct igp_power_info *pi = rs780_get_pi(rdev);
	struct igp_ps *current_state = rs780_get_ps(rdev->pm.dpm.current_ps);

	if ((current_state->max_voltage == RS780_VDDC_LEVEL_HIGH) &&
	    (current_state->min_voltage == RS780_VDDC_LEVEL_HIGH))
		return;

	WREG32_P(GFX_MACRO_BYPASS_CNTL, SPLL_BYPASS_CNTL, ~SPLL_BYPASS_CNTL);

	udelay(1);

	WREG32_P(FVTHROT_PWM_CTRL_REG0,
		 STARTING_PWM_HIGHTIME(pi->max_voltage),
		 ~STARTING_PWM_HIGHTIME_MASK);

	WREG32_P(FVTHROT_PWM_CTRL_REG0,
		 FORCE_STARTING_PWM_HIGHTIME, ~FORCE_STARTING_PWM_HIGHTIME);

	WREG32_P(FVTHROT_PWM_FEEDBACK_DIV_REG1, 0,
		~RANGE_PWM_FEEDBACK_DIV_EN);

	udelay(1);

	WREG32_P(GFX_MACRO_BYPASS_CNTL, 0, ~SPLL_BYPASS_CNTL);
}

static int rs780_set_engine_clock_scaling(struct radeon_device *rdev,
					  struct radeon_ps *new_ps,
					  struct radeon_ps *old_ps)
{
	struct atom_clock_dividers min_dividers, max_dividers, current_max_dividers;
	struct igp_ps *new_state = rs780_get_ps(new_ps);
	struct igp_ps *old_state = rs780_get_ps(old_ps);
	int ret;

	if ((new_state->sclk_high == old_state->sclk_high) &&
	    (new_state->sclk_low == old_state->sclk_low))
		return 0;

	ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
					     new_state->sclk_low, false, &min_dividers);
	if (ret)
		return ret;

	ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
					     new_state->sclk_high, false, &max_dividers);
	if (ret)
		return ret;

	ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
					     old_state->sclk_high, false, &current_max_dividers);
	if (ret)
		return ret;

	WREG32_P(GFX_MACRO_BYPASS_CNTL, SPLL_BYPASS_CNTL, ~SPLL_BYPASS_CNTL);

	WREG32_P(FVTHROT_FBDIV_REG2, FORCED_FEEDBACK_DIV(max_dividers.fb_div),
		 ~FORCED_FEEDBACK_DIV_MASK);
	WREG32_P(FVTHROT_FBDIV_REG1, STARTING_FEEDBACK_DIV(max_dividers.fb_div),
		 ~STARTING_FEEDBACK_DIV_MASK);
	WREG32_P(FVTHROT_FBDIV_REG1, FORCE_FEEDBACK_DIV, ~FORCE_FEEDBACK_DIV);

	udelay(100);

	WREG32_P(GFX_MACRO_BYPASS_CNTL, 0, ~SPLL_BYPASS_CNTL);

	if (max_dividers.fb_div > min_dividers.fb_div) {
		WREG32_P(FVTHROT_FBDIV_REG0,
			 MIN_FEEDBACK_DIV(min_dividers.fb_div) |
			 MAX_FEEDBACK_DIV(max_dividers.fb_div),
			 ~(MIN_FEEDBACK_DIV_MASK | MAX_FEEDBACK_DIV_MASK));

		WREG32_P(FVTHROT_FBDIV_REG1, 0, ~FORCE_FEEDBACK_DIV);
	}

	return 0;
}

static void rs780_set_engine_clock_spc(struct radeon_device *rdev,
				       struct radeon_ps *new_ps,
				       struct radeon_ps *old_ps)
{
	struct igp_ps *new_state = rs780_get_ps(new_ps);
	struct igp_ps *old_state = rs780_get_ps(old_ps);
	struct igp_power_info *pi = rs780_get_pi(rdev);

	if ((new_state->sclk_high == old_state->sclk_high) &&
	    (new_state->sclk_low == old_state->sclk_low))
		return;

	if (pi->crtc_id == 0)
		WREG32_P(CG_INTGFX_MISC, 0, ~FVTHROT_VBLANK_SEL);
	else
		WREG32_P(CG_INTGFX_MISC, FVTHROT_VBLANK_SEL, ~FVTHROT_VBLANK_SEL);

}

static void rs780_activate_engine_clk_scaling(struct radeon_device *rdev,
					      struct radeon_ps *new_ps,
					      struct radeon_ps *old_ps)
{
	struct igp_ps *new_state = rs780_get_ps(new_ps);
	struct igp_ps *old_state = rs780_get_ps(old_ps);

	if ((new_state->sclk_high == old_state->sclk_high) &&
	    (new_state->sclk_low == old_state->sclk_low))
		return;

	rs780_clk_scaling_enable(rdev, true);
}

static u32 rs780_get_voltage_for_vddc_level(struct radeon_device *rdev,
					    enum rs780_vddc_level vddc)
{
	struct igp_power_info *pi = rs780_get_pi(rdev);

	if (vddc == RS780_VDDC_LEVEL_HIGH)
		return pi->max_voltage;
	else if (vddc == RS780_VDDC_LEVEL_LOW)
		return pi->min_voltage;
	else
		return pi->max_voltage;
}

static void rs780_enable_voltage_scaling(struct radeon_device *rdev,
					 struct radeon_ps *new_ps)
{
	struct igp_ps *new_state = rs780_get_ps(new_ps);
	struct igp_power_info *pi = rs780_get_pi(rdev);
	enum rs780_vddc_level vddc_high, vddc_low;

	udelay(100);

	if ((new_state->max_voltage == RS780_VDDC_LEVEL_HIGH) &&
	    (new_state->min_voltage == RS780_VDDC_LEVEL_HIGH))
		return;

	vddc_high = rs780_get_voltage_for_vddc_level(rdev,
						     new_state->max_voltage);
	vddc_low = rs780_get_voltage_for_vddc_level(rdev,
						    new_state->min_voltage);

	WREG32_P(GFX_MACRO_BYPASS_CNTL, SPLL_BYPASS_CNTL, ~SPLL_BYPASS_CNTL);

	udelay(1);
	if (vddc_high > vddc_low) {
		WREG32_P(FVTHROT_PWM_FEEDBACK_DIV_REG1,
			 RANGE_PWM_FEEDBACK_DIV_EN, ~RANGE_PWM_FEEDBACK_DIV_EN);

		WREG32_P(FVTHROT_PWM_CTRL_REG0, 0, ~FORCE_STARTING_PWM_HIGHTIME);
	} else if (vddc_high == vddc_low) {
		if (pi->max_voltage != vddc_high) {
			WREG32_P(FVTHROT_PWM_CTRL_REG0,
				 STARTING_PWM_HIGHTIME(vddc_high),
				 ~STARTING_PWM_HIGHTIME_MASK);

			WREG32_P(FVTHROT_PWM_CTRL_REG0,
				 FORCE_STARTING_PWM_HIGHTIME,
				 ~FORCE_STARTING_PWM_HIGHTIME);
		}
	}

	WREG32_P(GFX_MACRO_BYPASS_CNTL, 0, ~SPLL_BYPASS_CNTL);
}

static void rs780_set_uvd_clock_before_set_eng_clock(struct radeon_device *rdev,
						     struct radeon_ps *new_ps,
						     struct radeon_ps *old_ps)
{
	struct igp_ps *new_state = rs780_get_ps(new_ps);
	struct igp_ps *current_state = rs780_get_ps(old_ps);

	if ((new_ps->vclk == old_ps->vclk) &&
	    (new_ps->dclk == old_ps->dclk))
		return;

	if (new_state->sclk_high >= current_state->sclk_high)
		return;

	radeon_set_uvd_clocks(rdev, new_ps->vclk, new_ps->dclk);
}

static void rs780_set_uvd_clock_after_set_eng_clock(struct radeon_device *rdev,
						    struct radeon_ps *new_ps,
						    struct radeon_ps *old_ps)
{
	struct igp_ps *new_state = rs780_get_ps(new_ps);
	struct igp_ps *current_state = rs780_get_ps(old_ps);

	if ((new_ps->vclk == old_ps->vclk) &&
	    (new_ps->dclk == old_ps->dclk))
		return;

	if (new_state->sclk_high < current_state->sclk_high)
		return;

	radeon_set_uvd_clocks(rdev, new_ps->vclk, new_ps->dclk);
}

int rs780_dpm_enable(struct radeon_device *rdev)
{
	struct igp_power_info *pi = rs780_get_pi(rdev);
	struct radeon_ps *boot_ps = rdev->pm.dpm.boot_ps;
	int ret;

	rs780_get_pm_mode_parameters(rdev);
	rs780_disable_vbios_powersaving(rdev);

	if (r600_dynamicpm_enabled(rdev))
		return -EINVAL;
	ret = rs780_initialize_dpm_parameters(rdev, boot_ps);
	if (ret)
		return ret;
	rs780_start_dpm(rdev);

	rs780_preset_ranges_slow_clk_fbdiv_en(rdev);
	rs780_preset_starting_fbdiv(rdev);
	if (pi->voltage_control)
		rs780_voltage_scaling_init(rdev);
	rs780_clk_scaling_enable(rdev, true);
	rs780_set_engine_clock_sc(rdev);
	rs780_set_engine_clock_wfc(rdev);
	rs780_program_at(rdev);
	rs780_set_engine_clock_tdc(rdev);
	rs780_set_engine_clock_ssc(rdev);

	if (pi->gfx_clock_gating)
		r600_gfx_clockgating_enable(rdev, true);

	if (rdev->irq.installed && (rdev->pm.int_thermal_type == THERMAL_TYPE_RV6XX)) {
		ret = r600_set_thermal_temperature_range(rdev, R600_TEMP_RANGE_MIN, R600_TEMP_RANGE_MAX);
		if (ret)
			return ret;
		rdev->irq.dpm_thermal = true;
		radeon_irq_set(rdev);
	}

	return 0;
}

void rs780_dpm_disable(struct radeon_device *rdev)
{
	struct igp_power_info *pi = rs780_get_pi(rdev);

	r600_dynamicpm_enable(rdev, false);

	rs780_clk_scaling_enable(rdev, false);
	rs780_voltage_scaling_enable(rdev, false);

	if (pi->gfx_clock_gating)
		r600_gfx_clockgating_enable(rdev, false);

	if (rdev->irq.installed &&
	    (rdev->pm.int_thermal_type == THERMAL_TYPE_RV6XX)) {
		rdev->irq.dpm_thermal = false;
		radeon_irq_set(rdev);
	}
}

int rs780_dpm_set_power_state(struct radeon_device *rdev)
{
	struct igp_power_info *pi = rs780_get_pi(rdev);
	struct radeon_ps *new_ps = rdev->pm.dpm.requested_ps;
	struct radeon_ps *old_ps = rdev->pm.dpm.current_ps;
	int ret;

	rs780_get_pm_mode_parameters(rdev);

	rs780_set_uvd_clock_before_set_eng_clock(rdev, new_ps, old_ps);

	if (pi->voltage_control) {
		rs780_force_voltage_to_high(rdev);
		mdelay(5);
	}

	ret = rs780_set_engine_clock_scaling(rdev, new_ps, old_ps);
	if (ret)
		return ret;
	rs780_set_engine_clock_spc(rdev, new_ps, old_ps);

	rs780_activate_engine_clk_scaling(rdev, new_ps, old_ps);

	if (pi->voltage_control)
		rs780_enable_voltage_scaling(rdev, new_ps);

	rs780_set_uvd_clock_after_set_eng_clock(rdev, new_ps, old_ps);

	return 0;
}

void rs780_dpm_setup_asic(struct radeon_device *rdev)
{

}

void rs780_dpm_display_configuration_changed(struct radeon_device *rdev)
{
	rs780_get_pm_mode_parameters(rdev);
	rs780_program_at(rdev);
}

union igp_info {
	struct _ATOM_INTEGRATED_SYSTEM_INFO info;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V2 info_2;
};

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

static void rs780_parse_pplib_non_clock_info(struct radeon_device *rdev,
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
		rps->vclk = RS780_DEFAULT_VCLK_FREQ;
		rps->dclk = RS780_DEFAULT_DCLK_FREQ;
	} else {
		rps->vclk = 0;
		rps->dclk = 0;
	}

	if (rps->class & ATOM_PPLIB_CLASSIFICATION_BOOT)
		rdev->pm.dpm.boot_ps = rps;
	if (rps->class & ATOM_PPLIB_CLASSIFICATION_UVDSTATE)
		rdev->pm.dpm.uvd_ps = rps;
}

static void rs780_parse_pplib_clock_info(struct radeon_device *rdev,
					 struct radeon_ps *rps,
					 union pplib_clock_info *clock_info)
{
	struct igp_ps *ps = rs780_get_ps(rps);
	u32 sclk;

	sclk = le16_to_cpu(clock_info->rs780.usLowEngineClockLow);
	sclk |= clock_info->rs780.ucLowEngineClockHigh << 16;
	ps->sclk_low = sclk;
	sclk = le16_to_cpu(clock_info->rs780.usHighEngineClockLow);
	sclk |= clock_info->rs780.ucHighEngineClockHigh << 16;
	ps->sclk_high = sclk;
	switch (le16_to_cpu(clock_info->rs780.usVDDC)) {
	case ATOM_PPLIB_RS780_VOLTAGE_NONE:
	default:
		ps->min_voltage = RS780_VDDC_LEVEL_UNKNOWN;
		ps->max_voltage = RS780_VDDC_LEVEL_UNKNOWN;
		break;
	case ATOM_PPLIB_RS780_VOLTAGE_LOW:
		ps->min_voltage = RS780_VDDC_LEVEL_LOW;
		ps->max_voltage = RS780_VDDC_LEVEL_LOW;
		break;
	case ATOM_PPLIB_RS780_VOLTAGE_HIGH:
		ps->min_voltage = RS780_VDDC_LEVEL_HIGH;
		ps->max_voltage = RS780_VDDC_LEVEL_HIGH;
		break;
	case ATOM_PPLIB_RS780_VOLTAGE_VARIABLE:
		ps->min_voltage = RS780_VDDC_LEVEL_LOW;
		ps->max_voltage = RS780_VDDC_LEVEL_HIGH;
		break;
	}
	ps->flags = le32_to_cpu(clock_info->rs780.ulFlags);

	if (rps->class & ATOM_PPLIB_CLASSIFICATION_BOOT) {
		ps->sclk_low = rdev->clock.default_sclk;
		ps->sclk_high = rdev->clock.default_sclk;
		ps->min_voltage = RS780_VDDC_LEVEL_HIGH;
		ps->max_voltage = RS780_VDDC_LEVEL_HIGH;
	}
}

static int rs780_parse_power_table(struct radeon_device *rdev)
{
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	struct _ATOM_PPLIB_NONCLOCK_INFO *non_clock_info;
	union pplib_power_state *power_state;
	int i;
	union pplib_clock_info *clock_info;
	union power_info *power_info;
	int index = GetIndexIntoMasterTable(DATA, PowerPlayInfo);
        u16 data_offset;
	u8 frev, crev;
	struct igp_ps *ps;

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
			clock_info = (union pplib_clock_info *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(power_info->pplib.usClockInfoArrayOffset) +
				 (power_state->v1.ucClockStateIndices[0] *
				  power_info->pplib.ucClockInfoSize));
			ps = kzalloc(sizeof(struct igp_ps), GFP_KERNEL);
			if (ps == NULL) {
				kfree(rdev->pm.dpm.ps);
				return -ENOMEM;
			}
			rdev->pm.dpm.ps[i].ps_priv = ps;
			rs780_parse_pplib_non_clock_info(rdev, &rdev->pm.dpm.ps[i],
							 non_clock_info,
							 power_info->pplib.ucNonClockSize);
			rs780_parse_pplib_clock_info(rdev,
						     &rdev->pm.dpm.ps[i],
						     clock_info);
		}
	}
	rdev->pm.dpm.num_ps = power_info->pplib.ucNumStates;
	return 0;
}

int rs780_dpm_init(struct radeon_device *rdev)
{
	struct igp_power_info *pi;
	int index = GetIndexIntoMasterTable(DATA, IntegratedSystemInfo);
	union igp_info *info;
	u16 data_offset;
	u8 frev, crev;
	int ret;

	pi = kzalloc(sizeof(struct igp_power_info), GFP_KERNEL);
	if (pi == NULL)
		return -ENOMEM;
	rdev->pm.dpm.priv = pi;

	ret = rs780_parse_power_table(rdev);
	if (ret)
		return ret;

	pi->voltage_control = false;
	pi->gfx_clock_gating = true;

	if (atom_parse_data_header(rdev->mode_info.atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		info = (union igp_info *)(rdev->mode_info.atom_context->bios + data_offset);

		/* Get various system informations from bios */
		switch (crev) {
		case 1:
			pi->num_of_cycles_in_period =
				info->info.ucNumberOfCyclesInPeriod;
			pi->num_of_cycles_in_period |=
				info->info.ucNumberOfCyclesInPeriodHi << 8;
			pi->invert_pwm_required =
				(pi->num_of_cycles_in_period & 0x8000) ? true : false;
			pi->boot_voltage = info->info.ucStartingPWM_HighTime;
			pi->max_voltage = info->info.ucMaxNBVoltage;
			pi->max_voltage |= info->info.ucMaxNBVoltageHigh << 8;
			pi->min_voltage = info->info.ucMinNBVoltage;
			pi->min_voltage |= info->info.ucMinNBVoltageHigh << 8;
			pi->inter_voltage_low =
				le16_to_cpu(info->info.usInterNBVoltageLow);
			pi->inter_voltage_high =
				le16_to_cpu(info->info.usInterNBVoltageHigh);
			pi->voltage_control = true;
			pi->bootup_uma_clk = info->info.usK8MemoryClock * 100;
			break;
		case 2:
			pi->num_of_cycles_in_period =
				le16_to_cpu(info->info_2.usNumberOfCyclesInPeriod);
			pi->invert_pwm_required =
				(pi->num_of_cycles_in_period & 0x8000) ? true : false;
			pi->boot_voltage =
				le16_to_cpu(info->info_2.usBootUpNBVoltage);
			pi->max_voltage =
				le16_to_cpu(info->info_2.usMaxNBVoltage);
			pi->min_voltage =
				le16_to_cpu(info->info_2.usMinNBVoltage);
			pi->system_config =
				le32_to_cpu(info->info_2.ulSystemConfig);
			pi->pwm_voltage_control =
				(pi->system_config & 0x4) ? true : false;
			pi->voltage_control = true;
			pi->bootup_uma_clk = le32_to_cpu(info->info_2.ulBootUpUMAClock);
			break;
		default:
			DRM_ERROR("No integrated system info for your GPU\n");
			return -EINVAL;
		}
		if (pi->min_voltage > pi->max_voltage)
			pi->voltage_control = false;
		if (pi->pwm_voltage_control) {
			if ((pi->num_of_cycles_in_period == 0) ||
			    (pi->max_voltage == 0) ||
			    (pi->min_voltage == 0))
				pi->voltage_control = false;
		} else {
			if ((pi->num_of_cycles_in_period == 0) ||
			    (pi->max_voltage == 0))
				pi->voltage_control = false;
		}

		return 0;
	}
	radeon_dpm_fini(rdev);
	return -EINVAL;
}

void rs780_dpm_print_power_state(struct radeon_device *rdev,
				 struct radeon_ps *rps)
{
	struct igp_ps *ps = rs780_get_ps(rps);

	r600_dpm_print_class_info(rps->class, rps->class2);
	r600_dpm_print_cap_info(rps->caps);
	printk("\tuvd    vclk: %d dclk: %d\n", rps->vclk, rps->dclk);
	printk("\t\tpower level 0    sclk: %u vddc_index: %d\n",
	       ps->sclk_low, ps->min_voltage);
	printk("\t\tpower level 1    sclk: %u vddc_index: %d\n",
	       ps->sclk_high, ps->max_voltage);
	r600_dpm_print_ps_status(rdev, rps);
}

void rs780_dpm_fini(struct radeon_device *rdev)
{
	int i;

	for (i = 0; i < rdev->pm.dpm.num_ps; i++) {
		kfree(rdev->pm.dpm.ps[i].ps_priv);
	}
	kfree(rdev->pm.dpm.ps);
	kfree(rdev->pm.dpm.priv);
}

u32 rs780_dpm_get_sclk(struct radeon_device *rdev, bool low)
{
	struct igp_ps *requested_state = rs780_get_ps(rdev->pm.dpm.requested_ps);

	if (low)
		return requested_state->sclk_low;
	else
		return requested_state->sclk_high;
}

u32 rs780_dpm_get_mclk(struct radeon_device *rdev, bool low)
{
	struct igp_power_info *pi = rs780_get_pi(rdev);

	return pi->bootup_uma_clk;
}
