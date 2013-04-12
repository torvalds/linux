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
		WREG32_P(GENERAL_PWRMGT, 0, ~SCLK_PWRMGT_OFF);
	else
		WREG32_P(GENERAL_PWRMGT, SCLK_PWRMGT_OFF, ~SCLK_PWRMGT_OFF);
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
