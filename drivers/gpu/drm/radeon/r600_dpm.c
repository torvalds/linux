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
#include "r600d.h"
#include "r600_dpm.h"
#include "atom.h"

const u32 r600_utc[R600_PM_NUMBER_OF_TC] =
{
	R600_UTC_DFLT_00,
	R600_UTC_DFLT_01,
	R600_UTC_DFLT_02,
	R600_UTC_DFLT_03,
	R600_UTC_DFLT_04,
	R600_UTC_DFLT_05,
	R600_UTC_DFLT_06,
	R600_UTC_DFLT_07,
	R600_UTC_DFLT_08,
	R600_UTC_DFLT_09,
	R600_UTC_DFLT_10,
	R600_UTC_DFLT_11,
	R600_UTC_DFLT_12,
	R600_UTC_DFLT_13,
	R600_UTC_DFLT_14,
};

const u32 r600_dtc[R600_PM_NUMBER_OF_TC] =
{
	R600_DTC_DFLT_00,
	R600_DTC_DFLT_01,
	R600_DTC_DFLT_02,
	R600_DTC_DFLT_03,
	R600_DTC_DFLT_04,
	R600_DTC_DFLT_05,
	R600_DTC_DFLT_06,
	R600_DTC_DFLT_07,
	R600_DTC_DFLT_08,
	R600_DTC_DFLT_09,
	R600_DTC_DFLT_10,
	R600_DTC_DFLT_11,
	R600_DTC_DFLT_12,
	R600_DTC_DFLT_13,
	R600_DTC_DFLT_14,
};

void r600_dpm_print_class_info(u32 class, u32 class2)
{
	printk("\tui class: ");
	switch (class & ATOM_PPLIB_CLASSIFICATION_UI_MASK) {
	case ATOM_PPLIB_CLASSIFICATION_UI_NONE:
	default:
		printk("none\n");
		break;
	case ATOM_PPLIB_CLASSIFICATION_UI_BATTERY:
		printk("battery\n");
		break;
	case ATOM_PPLIB_CLASSIFICATION_UI_BALANCED:
		printk("balanced\n");
		break;
	case ATOM_PPLIB_CLASSIFICATION_UI_PERFORMANCE:
		printk("performance\n");
		break;
	}
	printk("\tinternal class: ");
	if (((class & ~ATOM_PPLIB_CLASSIFICATION_UI_MASK) == 0) &&
	    (class2 == 0))
		printk("none");
	else {
		if (class & ATOM_PPLIB_CLASSIFICATION_BOOT)
			printk("boot ");
		if (class & ATOM_PPLIB_CLASSIFICATION_THERMAL)
			printk("thermal ");
		if (class & ATOM_PPLIB_CLASSIFICATION_LIMITEDPOWERSOURCE)
			printk("limited_pwr ");
		if (class & ATOM_PPLIB_CLASSIFICATION_REST)
			printk("rest ");
		if (class & ATOM_PPLIB_CLASSIFICATION_FORCED)
			printk("forced ");
		if (class & ATOM_PPLIB_CLASSIFICATION_3DPERFORMANCE)
			printk("3d_perf ");
		if (class & ATOM_PPLIB_CLASSIFICATION_OVERDRIVETEMPLATE)
			printk("ovrdrv ");
		if (class & ATOM_PPLIB_CLASSIFICATION_UVDSTATE)
			printk("uvd ");
		if (class & ATOM_PPLIB_CLASSIFICATION_3DLOW)
			printk("3d_low ");
		if (class & ATOM_PPLIB_CLASSIFICATION_ACPI)
			printk("acpi ");
		if (class & ATOM_PPLIB_CLASSIFICATION_HD2STATE)
			printk("uvd_hd2 ");
		if (class & ATOM_PPLIB_CLASSIFICATION_HDSTATE)
			printk("uvd_hd ");
		if (class & ATOM_PPLIB_CLASSIFICATION_SDSTATE)
			printk("uvd_sd ");
		if (class2 & ATOM_PPLIB_CLASSIFICATION2_LIMITEDPOWERSOURCE_2)
			printk("limited_pwr2 ");
		if (class2 & ATOM_PPLIB_CLASSIFICATION2_ULV)
			printk("ulv ");
		if (class2 & ATOM_PPLIB_CLASSIFICATION2_MVC)
			printk("uvd_mvc ");
	}
	printk("\n");
}

void r600_dpm_print_cap_info(u32 caps)
{
	printk("\tcaps: ");
	if (caps & ATOM_PPLIB_SINGLE_DISPLAY_ONLY)
		printk("single_disp ");
	if (caps & ATOM_PPLIB_SUPPORTS_VIDEO_PLAYBACK)
		printk("video ");
	if (caps & ATOM_PPLIB_DISALLOW_ON_DC)
		printk("no_dc ");
	printk("\n");
}

void r600_dpm_print_ps_status(struct radeon_device *rdev,
			      struct radeon_ps *rps)
{
	printk("\tstatus: ");
	if (rps == rdev->pm.dpm.current_ps)
		printk("c ");
	if (rps == rdev->pm.dpm.requested_ps)
		printk("r ");
	if (rps == rdev->pm.dpm.boot_ps)
		printk("b ");
	printk("\n");
}

u32 r600_dpm_get_vblank_time(struct radeon_device *rdev)
{
	struct drm_device *dev = rdev->ddev;
	struct drm_crtc *crtc;
	struct radeon_crtc *radeon_crtc;
	u32 line_time_us, vblank_lines;
	u32 vblank_time_us = 0xffffffff; /* if the displays are off, vblank time is max */

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		radeon_crtc = to_radeon_crtc(crtc);
		if (crtc->enabled && radeon_crtc->enabled && radeon_crtc->hw_mode.clock) {
			line_time_us = (radeon_crtc->hw_mode.crtc_htotal * 1000) /
				radeon_crtc->hw_mode.clock;
			vblank_lines = radeon_crtc->hw_mode.crtc_vblank_end -
				radeon_crtc->hw_mode.crtc_vdisplay +
				(radeon_crtc->v_border * 2);
			vblank_time_us = vblank_lines * line_time_us;
			break;
		}
	}

	return vblank_time_us;
}

void r600_calculate_u_and_p(u32 i, u32 r_c, u32 p_b,
			    u32 *p, u32 *u)
{
	u32 b_c = 0;
	u32 i_c;
	u32 tmp;

	i_c = (i * r_c) / 100;
	tmp = i_c >> p_b;

	while (tmp) {
		b_c++;
		tmp >>= 1;
	}

	*u = (b_c + 1) / 2;
	*p = i_c / (1 << (2 * (*u)));
}

int r600_calculate_at(u32 t, u32 h, u32 fh, u32 fl, u32 *tl, u32 *th)
{
	u32 k, a, ah, al;
	u32 t1;

	if ((fl == 0) || (fh == 0) || (fl > fh))
		return -EINVAL;

	k = (100 * fh) / fl;
	t1 = (t * (k - 100));
	a = (1000 * (100 * h + t1)) / (10000 + (t1 / 100));
	a = (a + 5) / 10;
	ah = ((a * t) + 5000) / 10000;
	al = a - ah;

	*th = t - ah;
	*tl = t + al;

	return 0;
}

void r600_gfx_clockgating_enable(struct radeon_device *rdev, bool enable)
{
	int i;

	if (enable) {
		WREG32_P(SCLK_PWRMGT_CNTL, DYN_GFX_CLK_OFF_EN, ~DYN_GFX_CLK_OFF_EN);
	} else {
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~DYN_GFX_CLK_OFF_EN);

		WREG32(CG_RLC_REQ_AND_RSP, 0x2);

		for (i = 0; i < rdev->usec_timeout; i++) {
			if (((RREG32(CG_RLC_REQ_AND_RSP) & CG_RLC_RSP_TYPE_MASK) >> CG_RLC_RSP_TYPE_SHIFT) == 1)
				break;
			udelay(1);
		}

		WREG32(CG_RLC_REQ_AND_RSP, 0x0);

		WREG32(GRBM_PWR_CNTL, 0x1);
		RREG32(GRBM_PWR_CNTL);
	}
}

void r600_dynamicpm_enable(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32_P(GENERAL_PWRMGT, GLOBAL_PWRMGT_EN, ~GLOBAL_PWRMGT_EN);
	else
		WREG32_P(GENERAL_PWRMGT, 0, ~GLOBAL_PWRMGT_EN);
}

void r600_enable_thermal_protection(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32_P(GENERAL_PWRMGT, 0, ~THERMAL_PROTECTION_DIS);
	else
		WREG32_P(GENERAL_PWRMGT, THERMAL_PROTECTION_DIS, ~THERMAL_PROTECTION_DIS);
}

void r600_enable_acpi_pm(struct radeon_device *rdev)
{
	WREG32_P(GENERAL_PWRMGT, STATIC_PM_EN, ~STATIC_PM_EN);
}

void r600_enable_dynamic_pcie_gen2(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32_P(GENERAL_PWRMGT, ENABLE_GEN2PCIE, ~ENABLE_GEN2PCIE);
	else
		WREG32_P(GENERAL_PWRMGT, 0, ~ENABLE_GEN2PCIE);
}

bool r600_dynamicpm_enabled(struct radeon_device *rdev)
{
	if (RREG32(GENERAL_PWRMGT) & GLOBAL_PWRMGT_EN)
		return true;
	else
		return false;
}

void r600_enable_sclk_control(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~SCLK_PWRMGT_OFF);
	else
		WREG32_P(SCLK_PWRMGT_CNTL, SCLK_PWRMGT_OFF, ~SCLK_PWRMGT_OFF);
}

void r600_enable_mclk_control(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32_P(MCLK_PWRMGT_CNTL, 0, ~MPLL_PWRMGT_OFF);
	else
		WREG32_P(MCLK_PWRMGT_CNTL, MPLL_PWRMGT_OFF, ~MPLL_PWRMGT_OFF);
}

void r600_enable_spll_bypass(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32_P(CG_SPLL_FUNC_CNTL, SPLL_BYPASS_EN, ~SPLL_BYPASS_EN);
	else
		WREG32_P(CG_SPLL_FUNC_CNTL, 0, ~SPLL_BYPASS_EN);
}

void r600_wait_for_spll_change(struct radeon_device *rdev)
{
	int i;

	for (i = 0; i < rdev->usec_timeout; i++) {
		if (RREG32(CG_SPLL_FUNC_CNTL) & SPLL_CHG_STATUS)
			break;
		udelay(1);
	}
}

void r600_set_bsp(struct radeon_device *rdev, u32 u, u32 p)
{
	WREG32(CG_BSP, BSP(p) | BSU(u));
}

void r600_set_at(struct radeon_device *rdev,
		 u32 l_to_m, u32 m_to_h,
		 u32 h_to_m, u32 m_to_l)
{
	WREG32(CG_RT, FLS(l_to_m) | FMS(m_to_h));
	WREG32(CG_LT, FHS(h_to_m) | FMS(m_to_l));
}

void r600_set_tc(struct radeon_device *rdev,
		 u32 index, u32 u_t, u32 d_t)
{
	WREG32(CG_FFCT_0 + (index * 4), UTC_0(u_t) | DTC_0(d_t));
}

void r600_select_td(struct radeon_device *rdev,
		    enum r600_td td)
{
	if (td == R600_TD_AUTO)
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~FIR_FORCE_TREND_SEL);
	else
		WREG32_P(SCLK_PWRMGT_CNTL, FIR_FORCE_TREND_SEL, ~FIR_FORCE_TREND_SEL);
	if (td == R600_TD_UP)
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~FIR_TREND_MODE);
	if (td == R600_TD_DOWN)
		WREG32_P(SCLK_PWRMGT_CNTL, FIR_TREND_MODE, ~FIR_TREND_MODE);
}

void r600_set_vrc(struct radeon_device *rdev, u32 vrv)
{
	WREG32(CG_FTV, vrv);
}

void r600_set_tpu(struct radeon_device *rdev, u32 u)
{
	WREG32_P(CG_TPC, TPU(u), ~TPU_MASK);
}

void r600_set_tpc(struct radeon_device *rdev, u32 c)
{
	WREG32_P(CG_TPC, TPCC(c), ~TPCC_MASK);
}

void r600_set_sstu(struct radeon_device *rdev, u32 u)
{
	WREG32_P(CG_SSP, CG_SSTU(u), ~CG_SSTU_MASK);
}

void r600_set_sst(struct radeon_device *rdev, u32 t)
{
	WREG32_P(CG_SSP, CG_SST(t), ~CG_SST_MASK);
}

void r600_set_git(struct radeon_device *rdev, u32 t)
{
	WREG32_P(CG_GIT, CG_GICST(t), ~CG_GICST_MASK);
}

void r600_set_fctu(struct radeon_device *rdev, u32 u)
{
	WREG32_P(CG_FC_T, FC_TU(u), ~FC_TU_MASK);
}

void r600_set_fct(struct radeon_device *rdev, u32 t)
{
	WREG32_P(CG_FC_T, FC_T(t), ~FC_T_MASK);
}

void r600_set_ctxcgtt3d_rphc(struct radeon_device *rdev, u32 p)
{
	WREG32_P(CG_CTX_CGTT3D_R, PHC(p), ~PHC_MASK);
}

void r600_set_ctxcgtt3d_rsdc(struct radeon_device *rdev, u32 s)
{
	WREG32_P(CG_CTX_CGTT3D_R, SDC(s), ~SDC_MASK);
}

void r600_set_vddc3d_oorsu(struct radeon_device *rdev, u32 u)
{
	WREG32_P(CG_VDDC3D_OOR, SU(u), ~SU_MASK);
}

void r600_set_vddc3d_oorphc(struct radeon_device *rdev, u32 p)
{
	WREG32_P(CG_VDDC3D_OOR, PHC(p), ~PHC_MASK);
}

void r600_set_vddc3d_oorsdc(struct radeon_device *rdev, u32 s)
{
	WREG32_P(CG_VDDC3D_OOR, SDC(s), ~SDC_MASK);
}

void r600_set_mpll_lock_time(struct radeon_device *rdev, u32 lock_time)
{
	WREG32_P(MPLL_TIME, MPLL_LOCK_TIME(lock_time), ~MPLL_LOCK_TIME_MASK);
}

void r600_set_mpll_reset_time(struct radeon_device *rdev, u32 reset_time)
{
	WREG32_P(MPLL_TIME, MPLL_RESET_TIME(reset_time), ~MPLL_RESET_TIME_MASK);
}

void r600_engine_clock_entry_enable(struct radeon_device *rdev,
				    u32 index, bool enable)
{
	if (enable)
		WREG32_P(SCLK_FREQ_SETTING_STEP_0_PART2 + (index * 4 * 2),
			 STEP_0_SPLL_ENTRY_VALID, ~STEP_0_SPLL_ENTRY_VALID);
	else
		WREG32_P(SCLK_FREQ_SETTING_STEP_0_PART2 + (index * 4 * 2),
			 0, ~STEP_0_SPLL_ENTRY_VALID);
}

void r600_engine_clock_entry_enable_pulse_skipping(struct radeon_device *rdev,
						   u32 index, bool enable)
{
	if (enable)
		WREG32_P(SCLK_FREQ_SETTING_STEP_0_PART2 + (index * 4 * 2),
			 STEP_0_SPLL_STEP_ENABLE, ~STEP_0_SPLL_STEP_ENABLE);
	else
		WREG32_P(SCLK_FREQ_SETTING_STEP_0_PART2 + (index * 4 * 2),
			 0, ~STEP_0_SPLL_STEP_ENABLE);
}

void r600_engine_clock_entry_enable_post_divider(struct radeon_device *rdev,
						 u32 index, bool enable)
{
	if (enable)
		WREG32_P(SCLK_FREQ_SETTING_STEP_0_PART2 + (index * 4 * 2),
			 STEP_0_POST_DIV_EN, ~STEP_0_POST_DIV_EN);
	else
		WREG32_P(SCLK_FREQ_SETTING_STEP_0_PART2 + (index * 4 * 2),
			 0, ~STEP_0_POST_DIV_EN);
}

void r600_engine_clock_entry_set_post_divider(struct radeon_device *rdev,
					      u32 index, u32 divider)
{
	WREG32_P(SCLK_FREQ_SETTING_STEP_0_PART1 + (index * 4 * 2),
		 STEP_0_SPLL_POST_DIV(divider), ~STEP_0_SPLL_POST_DIV_MASK);
}

void r600_engine_clock_entry_set_reference_divider(struct radeon_device *rdev,
						   u32 index, u32 divider)
{
	WREG32_P(SCLK_FREQ_SETTING_STEP_0_PART1 + (index * 4 * 2),
		 STEP_0_SPLL_REF_DIV(divider), ~STEP_0_SPLL_REF_DIV_MASK);
}

void r600_engine_clock_entry_set_feedback_divider(struct radeon_device *rdev,
						  u32 index, u32 divider)
{
	WREG32_P(SCLK_FREQ_SETTING_STEP_0_PART1 + (index * 4 * 2),
		 STEP_0_SPLL_FB_DIV(divider), ~STEP_0_SPLL_FB_DIV_MASK);
}

void r600_engine_clock_entry_set_step_time(struct radeon_device *rdev,
					   u32 index, u32 step_time)
{
	WREG32_P(SCLK_FREQ_SETTING_STEP_0_PART1 + (index * 4 * 2),
		 STEP_0_SPLL_STEP_TIME(step_time), ~STEP_0_SPLL_STEP_TIME_MASK);
}

void r600_vid_rt_set_ssu(struct radeon_device *rdev, u32 u)
{
	WREG32_P(VID_RT, SSTU(u), ~SSTU_MASK);
}

void r600_vid_rt_set_vru(struct radeon_device *rdev, u32 u)
{
	WREG32_P(VID_RT, VID_CRTU(u), ~VID_CRTU_MASK);
}

void r600_vid_rt_set_vrt(struct radeon_device *rdev, u32 rt)
{
	WREG32_P(VID_RT, VID_CRT(rt), ~VID_CRT_MASK);
}

void r600_voltage_control_enable_pins(struct radeon_device *rdev,
				      u64 mask)
{
	WREG32(LOWER_GPIO_ENABLE, mask & 0xffffffff);
	WREG32(UPPER_GPIO_ENABLE, upper_32_bits(mask));
}


void r600_voltage_control_program_voltages(struct radeon_device *rdev,
					   enum r600_power_level index, u64 pins)
{
	u32 tmp, mask;
	u32 ix = 3 - (3 & index);

	WREG32(CTXSW_VID_LOWER_GPIO_CNTL + (ix * 4), pins & 0xffffffff);

	mask = 7 << (3 * ix);
	tmp = RREG32(VID_UPPER_GPIO_CNTL);
	tmp = (tmp & ~mask) | ((pins >> (32 - (3 * ix))) & mask);
	WREG32(VID_UPPER_GPIO_CNTL, tmp);
}

void r600_voltage_control_deactivate_static_control(struct radeon_device *rdev,
						    u64 mask)
{
	u32 gpio;

	gpio = RREG32(GPIOPAD_MASK);
	gpio &= ~mask;
	WREG32(GPIOPAD_MASK, gpio);

	gpio = RREG32(GPIOPAD_EN);
	gpio &= ~mask;
	WREG32(GPIOPAD_EN, gpio);

	gpio = RREG32(GPIOPAD_A);
	gpio &= ~mask;
	WREG32(GPIOPAD_A, gpio);
}

void r600_power_level_enable(struct radeon_device *rdev,
			     enum r600_power_level index, bool enable)
{
	u32 ix = 3 - (3 & index);

	if (enable)
		WREG32_P(CTXSW_PROFILE_INDEX + (ix * 4), CTXSW_FREQ_STATE_ENABLE,
			 ~CTXSW_FREQ_STATE_ENABLE);
	else
		WREG32_P(CTXSW_PROFILE_INDEX + (ix * 4), 0,
			 ~CTXSW_FREQ_STATE_ENABLE);
}

void r600_power_level_set_voltage_index(struct radeon_device *rdev,
					enum r600_power_level index, u32 voltage_index)
{
	u32 ix = 3 - (3 & index);

	WREG32_P(CTXSW_PROFILE_INDEX + (ix * 4),
		 CTXSW_FREQ_VIDS_CFG_INDEX(voltage_index), ~CTXSW_FREQ_VIDS_CFG_INDEX_MASK);
}

void r600_power_level_set_mem_clock_index(struct radeon_device *rdev,
					  enum r600_power_level index, u32 mem_clock_index)
{
	u32 ix = 3 - (3 & index);

	WREG32_P(CTXSW_PROFILE_INDEX + (ix * 4),
		 CTXSW_FREQ_MCLK_CFG_INDEX(mem_clock_index), ~CTXSW_FREQ_MCLK_CFG_INDEX_MASK);
}

void r600_power_level_set_eng_clock_index(struct radeon_device *rdev,
					  enum r600_power_level index, u32 eng_clock_index)
{
	u32 ix = 3 - (3 & index);

	WREG32_P(CTXSW_PROFILE_INDEX + (ix * 4),
		 CTXSW_FREQ_SCLK_CFG_INDEX(eng_clock_index), ~CTXSW_FREQ_SCLK_CFG_INDEX_MASK);
}

void r600_power_level_set_watermark_id(struct radeon_device *rdev,
				       enum r600_power_level index,
				       enum r600_display_watermark watermark_id)
{
	u32 ix = 3 - (3 & index);
	u32 tmp = 0;

	if (watermark_id == R600_DISPLAY_WATERMARK_HIGH)
		tmp = CTXSW_FREQ_DISPLAY_WATERMARK;
	WREG32_P(CTXSW_PROFILE_INDEX + (ix * 4), tmp, ~CTXSW_FREQ_DISPLAY_WATERMARK);
}

void r600_power_level_set_pcie_gen2(struct radeon_device *rdev,
				    enum r600_power_level index, bool compatible)
{
	u32 ix = 3 - (3 & index);
	u32 tmp = 0;

	if (compatible)
		tmp = CTXSW_FREQ_GEN2PCIE_VOLT;
	WREG32_P(CTXSW_PROFILE_INDEX + (ix * 4), tmp, ~CTXSW_FREQ_GEN2PCIE_VOLT);
}

enum r600_power_level r600_power_level_get_current_index(struct radeon_device *rdev)
{
	u32 tmp;

	tmp = RREG32(TARGET_AND_CURRENT_PROFILE_INDEX) & CURRENT_PROFILE_INDEX_MASK;
	tmp >>= CURRENT_PROFILE_INDEX_SHIFT;
	return tmp;
}

enum r600_power_level r600_power_level_get_target_index(struct radeon_device *rdev)
{
	u32 tmp;

	tmp = RREG32(TARGET_AND_CURRENT_PROFILE_INDEX) & TARGET_PROFILE_INDEX_MASK;
	tmp >>= TARGET_PROFILE_INDEX_SHIFT;
	return tmp;
}

void r600_power_level_set_enter_index(struct radeon_device *rdev,
				      enum r600_power_level index)
{
	WREG32_P(TARGET_AND_CURRENT_PROFILE_INDEX, DYN_PWR_ENTER_INDEX(index),
		 ~DYN_PWR_ENTER_INDEX_MASK);
}

void r600_wait_for_power_level_unequal(struct radeon_device *rdev,
				       enum r600_power_level index)
{
	int i;

	for (i = 0; i < rdev->usec_timeout; i++) {
		if (r600_power_level_get_target_index(rdev) != index)
			break;
		udelay(1);
	}

	for (i = 0; i < rdev->usec_timeout; i++) {
		if (r600_power_level_get_current_index(rdev) != index)
			break;
		udelay(1);
	}
}

void r600_wait_for_power_level(struct radeon_device *rdev,
			       enum r600_power_level index)
{
	int i;

	for (i = 0; i < rdev->usec_timeout; i++) {
		if (r600_power_level_get_target_index(rdev) == index)
			break;
		udelay(1);
	}

	for (i = 0; i < rdev->usec_timeout; i++) {
		if (r600_power_level_get_current_index(rdev) == index)
			break;
		udelay(1);
	}
}

void r600_start_dpm(struct radeon_device *rdev)
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
	r600_enable_mclk_control(rdev, true);
}

void r600_stop_dpm(struct radeon_device *rdev)
{
	r600_dynamicpm_enable(rdev, false);
}

int r600_dpm_pre_set_power_state(struct radeon_device *rdev)
{
	return 0;
}

void r600_dpm_post_set_power_state(struct radeon_device *rdev)
{

}

bool r600_is_uvd_state(u32 class, u32 class2)
{
	if (class & ATOM_PPLIB_CLASSIFICATION_UVDSTATE)
		return true;
	if (class & ATOM_PPLIB_CLASSIFICATION_HD2STATE)
		return true;
	if (class & ATOM_PPLIB_CLASSIFICATION_HDSTATE)
		return true;
	if (class & ATOM_PPLIB_CLASSIFICATION_SDSTATE)
		return true;
	if (class2 & ATOM_PPLIB_CLASSIFICATION2_MVC)
		return true;
	return false;
}

int r600_set_thermal_temperature_range(struct radeon_device *rdev,
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

bool r600_is_internal_thermal_sensor(enum radeon_int_thermal_type sensor)
{
	switch (sensor) {
	case THERMAL_TYPE_RV6XX:
	case THERMAL_TYPE_RV770:
	case THERMAL_TYPE_EVERGREEN:
	case THERMAL_TYPE_SUMO:
	case THERMAL_TYPE_NI:
	case THERMAL_TYPE_SI:
	case THERMAL_TYPE_CI:
	case THERMAL_TYPE_KV:
		return true;
	case THERMAL_TYPE_ADT7473_WITH_INTERNAL:
	case THERMAL_TYPE_EMC2103_WITH_INTERNAL:
		return false; /* need special handling */
	case THERMAL_TYPE_NONE:
	case THERMAL_TYPE_EXTERNAL:
	case THERMAL_TYPE_EXTERNAL_GPIO:
	default:
		return false;
	}
}

union power_info {
	struct _ATOM_POWERPLAY_INFO info;
	struct _ATOM_POWERPLAY_INFO_V2 info_2;
	struct _ATOM_POWERPLAY_INFO_V3 info_3;
	struct _ATOM_PPLIB_POWERPLAYTABLE pplib;
	struct _ATOM_PPLIB_POWERPLAYTABLE2 pplib2;
	struct _ATOM_PPLIB_POWERPLAYTABLE3 pplib3;
	struct _ATOM_PPLIB_POWERPLAYTABLE4 pplib4;
	struct _ATOM_PPLIB_POWERPLAYTABLE5 pplib5;
};

union fan_info {
	struct _ATOM_PPLIB_FANTABLE fan;
	struct _ATOM_PPLIB_FANTABLE2 fan2;
};

static int r600_parse_clk_voltage_dep_table(struct radeon_clock_voltage_dependency_table *radeon_table,
					    ATOM_PPLIB_Clock_Voltage_Dependency_Table *atom_table)
{
	u32 size = atom_table->ucNumEntries *
		sizeof(struct radeon_clock_voltage_dependency_entry);
	int i;

	radeon_table->entries = kzalloc(size, GFP_KERNEL);
	if (!radeon_table->entries)
		return -ENOMEM;

	for (i = 0; i < atom_table->ucNumEntries; i++) {
		radeon_table->entries[i].clk = le16_to_cpu(atom_table->entries[i].usClockLow) |
			(atom_table->entries[i].ucClockHigh << 16);
		radeon_table->entries[i].v = le16_to_cpu(atom_table->entries[i].usVoltage);
	}
	radeon_table->count = atom_table->ucNumEntries;

	return 0;
}

/* sizeof(ATOM_PPLIB_EXTENDEDHEADER) */
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V2 12
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V3 14
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V4 16
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V5 18
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V6 20
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V7 22

int r600_parse_extended_power_table(struct radeon_device *rdev)
{
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	union power_info *power_info;
	union fan_info *fan_info;
	ATOM_PPLIB_Clock_Voltage_Dependency_Table *dep_table;
	int index = GetIndexIntoMasterTable(DATA, PowerPlayInfo);
        u16 data_offset;
	u8 frev, crev;
	int ret, i;

	if (!atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset))
		return -EINVAL;
	power_info = (union power_info *)(mode_info->atom_context->bios + data_offset);

	/* fan table */
	if (le16_to_cpu(power_info->pplib.usTableSize) >=
	    sizeof(struct _ATOM_PPLIB_POWERPLAYTABLE3)) {
		if (power_info->pplib3.usFanTableOffset) {
			fan_info = (union fan_info *)(mode_info->atom_context->bios + data_offset +
						      le16_to_cpu(power_info->pplib3.usFanTableOffset));
			rdev->pm.dpm.fan.t_hyst = fan_info->fan.ucTHyst;
			rdev->pm.dpm.fan.t_min = le16_to_cpu(fan_info->fan.usTMin);
			rdev->pm.dpm.fan.t_med = le16_to_cpu(fan_info->fan.usTMed);
			rdev->pm.dpm.fan.t_high = le16_to_cpu(fan_info->fan.usTHigh);
			rdev->pm.dpm.fan.pwm_min = le16_to_cpu(fan_info->fan.usPWMMin);
			rdev->pm.dpm.fan.pwm_med = le16_to_cpu(fan_info->fan.usPWMMed);
			rdev->pm.dpm.fan.pwm_high = le16_to_cpu(fan_info->fan.usPWMHigh);
			if (fan_info->fan.ucFanTableFormat >= 2)
				rdev->pm.dpm.fan.t_max = le16_to_cpu(fan_info->fan2.usTMax);
			else
				rdev->pm.dpm.fan.t_max = 10900;
			rdev->pm.dpm.fan.cycle_delay = 100000;
			rdev->pm.dpm.fan.ucode_fan_control = true;
		}
	}

	/* clock dependancy tables, shedding tables */
	if (le16_to_cpu(power_info->pplib.usTableSize) >=
	    sizeof(struct _ATOM_PPLIB_POWERPLAYTABLE4)) {
		if (power_info->pplib4.usVddcDependencyOnSCLKOffset) {
			dep_table = (ATOM_PPLIB_Clock_Voltage_Dependency_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(power_info->pplib4.usVddcDependencyOnSCLKOffset));
			ret = r600_parse_clk_voltage_dep_table(&rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk,
							       dep_table);
			if (ret)
				return ret;
		}
		if (power_info->pplib4.usVddciDependencyOnMCLKOffset) {
			dep_table = (ATOM_PPLIB_Clock_Voltage_Dependency_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(power_info->pplib4.usVddciDependencyOnMCLKOffset));
			ret = r600_parse_clk_voltage_dep_table(&rdev->pm.dpm.dyn_state.vddci_dependency_on_mclk,
							       dep_table);
			if (ret) {
				kfree(rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries);
				return ret;
			}
		}
		if (power_info->pplib4.usVddcDependencyOnMCLKOffset) {
			dep_table = (ATOM_PPLIB_Clock_Voltage_Dependency_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(power_info->pplib4.usVddcDependencyOnMCLKOffset));
			ret = r600_parse_clk_voltage_dep_table(&rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk,
							       dep_table);
			if (ret) {
				kfree(rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries);
				kfree(rdev->pm.dpm.dyn_state.vddci_dependency_on_mclk.entries);
				return ret;
			}
		}
		if (power_info->pplib4.usMvddDependencyOnMCLKOffset) {
			dep_table = (ATOM_PPLIB_Clock_Voltage_Dependency_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(power_info->pplib4.usMvddDependencyOnMCLKOffset));
			ret = r600_parse_clk_voltage_dep_table(&rdev->pm.dpm.dyn_state.mvdd_dependency_on_mclk,
							       dep_table);
			if (ret) {
				kfree(rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries);
				kfree(rdev->pm.dpm.dyn_state.vddci_dependency_on_mclk.entries);
				kfree(rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk.entries);
				return ret;
			}
		}
		if (power_info->pplib4.usMaxClockVoltageOnDCOffset) {
			ATOM_PPLIB_Clock_Voltage_Limit_Table *clk_v =
				(ATOM_PPLIB_Clock_Voltage_Limit_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(power_info->pplib4.usMaxClockVoltageOnDCOffset));
			if (clk_v->ucNumEntries) {
				rdev->pm.dpm.dyn_state.max_clock_voltage_on_dc.sclk =
					le16_to_cpu(clk_v->entries[0].usSclkLow) |
					(clk_v->entries[0].ucSclkHigh << 16);
				rdev->pm.dpm.dyn_state.max_clock_voltage_on_dc.mclk =
					le16_to_cpu(clk_v->entries[0].usMclkLow) |
					(clk_v->entries[0].ucMclkHigh << 16);
				rdev->pm.dpm.dyn_state.max_clock_voltage_on_dc.vddc =
					le16_to_cpu(clk_v->entries[0].usVddc);
				rdev->pm.dpm.dyn_state.max_clock_voltage_on_dc.vddci =
					le16_to_cpu(clk_v->entries[0].usVddci);
			}
		}
		if (power_info->pplib4.usVddcPhaseShedLimitsTableOffset) {
			ATOM_PPLIB_PhaseSheddingLimits_Table *psl =
				(ATOM_PPLIB_PhaseSheddingLimits_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(power_info->pplib4.usVddcPhaseShedLimitsTableOffset));

			rdev->pm.dpm.dyn_state.phase_shedding_limits_table.entries =
				kzalloc(psl->ucNumEntries *
					sizeof(struct radeon_phase_shedding_limits_entry),
					GFP_KERNEL);
			if (!rdev->pm.dpm.dyn_state.phase_shedding_limits_table.entries) {
				kfree(rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries);
				kfree(rdev->pm.dpm.dyn_state.vddci_dependency_on_mclk.entries);
				kfree(rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk.entries);
				kfree(rdev->pm.dpm.dyn_state.mvdd_dependency_on_mclk.entries);
				return -ENOMEM;
			}

			for (i = 0; i < psl->ucNumEntries; i++) {
				rdev->pm.dpm.dyn_state.phase_shedding_limits_table.entries[i].sclk =
					le16_to_cpu(psl->entries[i].usSclkLow) |
					(psl->entries[i].ucSclkHigh << 16);
				rdev->pm.dpm.dyn_state.phase_shedding_limits_table.entries[i].mclk =
					le16_to_cpu(psl->entries[i].usMclkLow) |
					(psl->entries[i].ucMclkHigh << 16);
				rdev->pm.dpm.dyn_state.phase_shedding_limits_table.entries[i].voltage =
					le16_to_cpu(psl->entries[i].usVoltage);
			}
			rdev->pm.dpm.dyn_state.phase_shedding_limits_table.count =
				psl->ucNumEntries;
		}
	}

	/* cac data */
	if (le16_to_cpu(power_info->pplib.usTableSize) >=
	    sizeof(struct _ATOM_PPLIB_POWERPLAYTABLE5)) {
		rdev->pm.dpm.tdp_limit = le32_to_cpu(power_info->pplib5.ulTDPLimit);
		rdev->pm.dpm.near_tdp_limit = le32_to_cpu(power_info->pplib5.ulNearTDPLimit);
		rdev->pm.dpm.near_tdp_limit_adjusted = rdev->pm.dpm.near_tdp_limit;
		rdev->pm.dpm.tdp_od_limit = le16_to_cpu(power_info->pplib5.usTDPODLimit);
		if (rdev->pm.dpm.tdp_od_limit)
			rdev->pm.dpm.power_control = true;
		else
			rdev->pm.dpm.power_control = false;
		rdev->pm.dpm.tdp_adjustment = 0;
		rdev->pm.dpm.sq_ramping_threshold = le32_to_cpu(power_info->pplib5.ulSQRampingThreshold);
		rdev->pm.dpm.cac_leakage = le32_to_cpu(power_info->pplib5.ulCACLeakage);
		rdev->pm.dpm.load_line_slope = le16_to_cpu(power_info->pplib5.usLoadLineSlope);
		if (power_info->pplib5.usCACLeakageTableOffset) {
			ATOM_PPLIB_CAC_Leakage_Table *cac_table =
				(ATOM_PPLIB_CAC_Leakage_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(power_info->pplib5.usCACLeakageTableOffset));
			u32 size = cac_table->ucNumEntries * sizeof(struct radeon_cac_leakage_table);
			rdev->pm.dpm.dyn_state.cac_leakage_table.entries = kzalloc(size, GFP_KERNEL);
			if (!rdev->pm.dpm.dyn_state.cac_leakage_table.entries) {
				kfree(rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries);
				kfree(rdev->pm.dpm.dyn_state.vddci_dependency_on_mclk.entries);
				kfree(rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk.entries);
				kfree(rdev->pm.dpm.dyn_state.mvdd_dependency_on_mclk.entries);
				return -ENOMEM;
			}
			for (i = 0; i < cac_table->ucNumEntries; i++) {
				if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_EVV) {
					rdev->pm.dpm.dyn_state.cac_leakage_table.entries[i].vddc1 =
						le16_to_cpu(cac_table->entries[i].usVddc1);
					rdev->pm.dpm.dyn_state.cac_leakage_table.entries[i].vddc2 =
						le16_to_cpu(cac_table->entries[i].usVddc2);
					rdev->pm.dpm.dyn_state.cac_leakage_table.entries[i].vddc3 =
						le16_to_cpu(cac_table->entries[i].usVddc3);
				} else {
					rdev->pm.dpm.dyn_state.cac_leakage_table.entries[i].vddc =
						le16_to_cpu(cac_table->entries[i].usVddc);
					rdev->pm.dpm.dyn_state.cac_leakage_table.entries[i].leakage =
						le32_to_cpu(cac_table->entries[i].ulLeakageValue);
				}
			}
			rdev->pm.dpm.dyn_state.cac_leakage_table.count = cac_table->ucNumEntries;
		}
	}

	/* ext tables */
	if (le16_to_cpu(power_info->pplib.usTableSize) >=
	    sizeof(struct _ATOM_PPLIB_POWERPLAYTABLE3)) {
		ATOM_PPLIB_EXTENDEDHEADER *ext_hdr = (ATOM_PPLIB_EXTENDEDHEADER *)
			(mode_info->atom_context->bios + data_offset +
			 le16_to_cpu(power_info->pplib3.usExtendendedHeaderOffset));
		if ((le16_to_cpu(ext_hdr->usSize) >= SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V2) &&
			ext_hdr->usVCETableOffset) {
			VCEClockInfoArray *array = (VCEClockInfoArray *)
				(mode_info->atom_context->bios + data_offset +
                                 le16_to_cpu(ext_hdr->usVCETableOffset) + 1);
			ATOM_PPLIB_VCE_Clock_Voltage_Limit_Table *limits =
				(ATOM_PPLIB_VCE_Clock_Voltage_Limit_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(ext_hdr->usVCETableOffset) + 1 +
				 1 + array->ucNumEntries * sizeof(VCEClockInfo));
			u32 size = limits->numEntries *
				sizeof(struct radeon_vce_clock_voltage_dependency_entry);
			rdev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.entries =
				kzalloc(size, GFP_KERNEL);
			if (!rdev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.entries) {
				kfree(rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries);
				kfree(rdev->pm.dpm.dyn_state.vddci_dependency_on_mclk.entries);
				kfree(rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk.entries);
				kfree(rdev->pm.dpm.dyn_state.mvdd_dependency_on_mclk.entries);
				kfree(rdev->pm.dpm.dyn_state.cac_leakage_table.entries);
				return -ENOMEM;
			}
			rdev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.count =
				limits->numEntries;
			for (i = 0; i < limits->numEntries; i++) {
				VCEClockInfo *vce_clk =
					&array->entries[limits->entries[i].ucVCEClockInfoIndex];
				rdev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.entries[i].evclk =
					le16_to_cpu(vce_clk->usEVClkLow) | (vce_clk->ucEVClkHigh << 16);
				rdev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.entries[i].ecclk =
					le16_to_cpu(vce_clk->usECClkLow) | (vce_clk->ucECClkHigh << 16);
				rdev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.entries[i].v =
					le16_to_cpu(limits->entries[i].usVoltage);
			}
		}
		if ((le16_to_cpu(ext_hdr->usSize) >= SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V5) &&
		    ext_hdr->usPPMTableOffset) {
			ATOM_PPLIB_PPM_Table *ppm = (ATOM_PPLIB_PPM_Table *)
				(mode_info->atom_context->bios + data_offset +
				 le16_to_cpu(ext_hdr->usPPMTableOffset));
			rdev->pm.dpm.dyn_state.ppm_table =
				kzalloc(sizeof(struct radeon_ppm_table), GFP_KERNEL);
			if (!rdev->pm.dpm.dyn_state.ppm_table) {
				kfree(rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries);
				kfree(rdev->pm.dpm.dyn_state.vddci_dependency_on_mclk.entries);
				kfree(rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk.entries);
				kfree(rdev->pm.dpm.dyn_state.mvdd_dependency_on_mclk.entries);
				kfree(rdev->pm.dpm.dyn_state.cac_leakage_table.entries);
				kfree(rdev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.entries);
				return -ENOMEM;
			}
			rdev->pm.dpm.dyn_state.ppm_table->ppm_design = ppm->ucPpmDesign;
			rdev->pm.dpm.dyn_state.ppm_table->cpu_core_number =
				le16_to_cpu(ppm->usCpuCoreNumber);
			rdev->pm.dpm.dyn_state.ppm_table->platform_tdp =
				le32_to_cpu(ppm->ulPlatformTDP);
			rdev->pm.dpm.dyn_state.ppm_table->small_ac_platform_tdp =
				le32_to_cpu(ppm->ulSmallACPlatformTDP);
			rdev->pm.dpm.dyn_state.ppm_table->platform_tdc =
				le32_to_cpu(ppm->ulPlatformTDC);
			rdev->pm.dpm.dyn_state.ppm_table->small_ac_platform_tdc =
				le32_to_cpu(ppm->ulSmallACPlatformTDC);
			rdev->pm.dpm.dyn_state.ppm_table->apu_tdp =
				le32_to_cpu(ppm->ulApuTDP);
			rdev->pm.dpm.dyn_state.ppm_table->dgpu_tdp =
				le32_to_cpu(ppm->ulDGpuTDP);
			rdev->pm.dpm.dyn_state.ppm_table->dgpu_ulv_power =
				le32_to_cpu(ppm->ulDGpuUlvPower);
			rdev->pm.dpm.dyn_state.ppm_table->tj_max =
				le32_to_cpu(ppm->ulTjmax);
		}
		if ((le16_to_cpu(ext_hdr->usSize) >= SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V7) &&
			ext_hdr->usPowerTuneTableOffset) {
			u8 rev = *(u8 *)(mode_info->atom_context->bios + data_offset +
					 le16_to_cpu(ext_hdr->usPowerTuneTableOffset));
			ATOM_PowerTune_Table *pt;
			rdev->pm.dpm.dyn_state.cac_tdp_table =
				kzalloc(sizeof(struct radeon_cac_tdp_table), GFP_KERNEL);
			if (!rdev->pm.dpm.dyn_state.cac_tdp_table) {
				kfree(rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries);
				kfree(rdev->pm.dpm.dyn_state.vddci_dependency_on_mclk.entries);
				kfree(rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk.entries);
				kfree(rdev->pm.dpm.dyn_state.mvdd_dependency_on_mclk.entries);
				kfree(rdev->pm.dpm.dyn_state.cac_leakage_table.entries);
				kfree(rdev->pm.dpm.dyn_state.ppm_table);
				kfree(rdev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.entries);
				return -ENOMEM;
			}
			if (rev > 0) {
				ATOM_PPLIB_POWERTUNE_Table_V1 *ppt = (ATOM_PPLIB_POWERTUNE_Table_V1 *)
					(mode_info->atom_context->bios + data_offset +
					 le16_to_cpu(ext_hdr->usPowerTuneTableOffset));
				rdev->pm.dpm.dyn_state.cac_tdp_table->maximum_power_delivery_limit =
					ppt->usMaximumPowerDeliveryLimit;
				pt = &ppt->power_tune_table;
			} else {
				ATOM_PPLIB_POWERTUNE_Table *ppt = (ATOM_PPLIB_POWERTUNE_Table *)
					(mode_info->atom_context->bios + data_offset +
					 le16_to_cpu(ext_hdr->usPowerTuneTableOffset));
				rdev->pm.dpm.dyn_state.cac_tdp_table->maximum_power_delivery_limit = 255;
				pt = &ppt->power_tune_table;
			}
			rdev->pm.dpm.dyn_state.cac_tdp_table->tdp = le16_to_cpu(pt->usTDP);
			rdev->pm.dpm.dyn_state.cac_tdp_table->configurable_tdp =
				le16_to_cpu(pt->usConfigurableTDP);
			rdev->pm.dpm.dyn_state.cac_tdp_table->tdc = le16_to_cpu(pt->usTDC);
			rdev->pm.dpm.dyn_state.cac_tdp_table->battery_power_limit =
				le16_to_cpu(pt->usBatteryPowerLimit);
			rdev->pm.dpm.dyn_state.cac_tdp_table->small_power_limit =
				le16_to_cpu(pt->usSmallPowerLimit);
			rdev->pm.dpm.dyn_state.cac_tdp_table->low_cac_leakage =
				le16_to_cpu(pt->usLowCACLeakage);
			rdev->pm.dpm.dyn_state.cac_tdp_table->high_cac_leakage =
				le16_to_cpu(pt->usHighCACLeakage);
		}
	}

	return 0;
}

void r600_free_extended_power_table(struct radeon_device *rdev)
{
	if (rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries)
		kfree(rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries);
	if (rdev->pm.dpm.dyn_state.vddci_dependency_on_mclk.entries)
		kfree(rdev->pm.dpm.dyn_state.vddci_dependency_on_mclk.entries);
	if (rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk.entries)
		kfree(rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk.entries);
	if (rdev->pm.dpm.dyn_state.mvdd_dependency_on_mclk.entries)
		kfree(rdev->pm.dpm.dyn_state.mvdd_dependency_on_mclk.entries);
	if (rdev->pm.dpm.dyn_state.cac_leakage_table.entries)
		kfree(rdev->pm.dpm.dyn_state.cac_leakage_table.entries);
	if (rdev->pm.dpm.dyn_state.phase_shedding_limits_table.entries)
		kfree(rdev->pm.dpm.dyn_state.phase_shedding_limits_table.entries);
	if (rdev->pm.dpm.dyn_state.ppm_table)
		kfree(rdev->pm.dpm.dyn_state.ppm_table);
	if (rdev->pm.dpm.dyn_state.cac_tdp_table)
		kfree(rdev->pm.dpm.dyn_state.cac_tdp_table);
	if (rdev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.entries)
		kfree(rdev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.entries);
}

enum radeon_pcie_gen r600_get_pcie_gen_support(struct radeon_device *rdev,
					       u32 sys_mask,
					       enum radeon_pcie_gen asic_gen,
					       enum radeon_pcie_gen default_gen)
{
	switch (asic_gen) {
	case RADEON_PCIE_GEN1:
		return RADEON_PCIE_GEN1;
	case RADEON_PCIE_GEN2:
		return RADEON_PCIE_GEN2;
	case RADEON_PCIE_GEN3:
		return RADEON_PCIE_GEN3;
	default:
		if ((sys_mask & DRM_PCIE_SPEED_80) && (default_gen == RADEON_PCIE_GEN3))
			return RADEON_PCIE_GEN3;
		else if ((sys_mask & DRM_PCIE_SPEED_50) && (default_gen == RADEON_PCIE_GEN2))
			return RADEON_PCIE_GEN2;
		else
			return RADEON_PCIE_GEN1;
	}
	return RADEON_PCIE_GEN1;
}
