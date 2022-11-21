/*
 * Copyright 2013 Advanced Micro Devices, Inc.
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

#include <linux/firmware.h>
#include <linux/pci.h>
#include <linux/seq_file.h>

#include "atom.h"
#include "ci_dpm.h"
#include "cik.h"
#include "cikd.h"
#include "r600_dpm.h"
#include "radeon.h"
#include "radeon_asic.h"
#include "radeon_ucode.h"
#include "si_dpm.h"

#define MC_CG_ARB_FREQ_F0           0x0a
#define MC_CG_ARB_FREQ_F1           0x0b
#define MC_CG_ARB_FREQ_F2           0x0c
#define MC_CG_ARB_FREQ_F3           0x0d

#define SMC_RAM_END 0x40000

#define VOLTAGE_SCALE               4
#define VOLTAGE_VID_OFFSET_SCALE1    625
#define VOLTAGE_VID_OFFSET_SCALE2    100

static const struct ci_pt_defaults defaults_hawaii_xt =
{
	1, 0xF, 0xFD, 0x19, 5, 0x14, 0, 0xB0000,
	{ 0x2E,  0x00,  0x00,  0x88,  0x00,  0x00,  0x72,  0x60,  0x51,  0xA7,  0x79,  0x6B,  0x90,  0xBD,  0x79  },
	{ 0x217, 0x217, 0x217, 0x242, 0x242, 0x242, 0x269, 0x269, 0x269, 0x2A1, 0x2A1, 0x2A1, 0x2C9, 0x2C9, 0x2C9 }
};

static const struct ci_pt_defaults defaults_hawaii_pro =
{
	1, 0xF, 0xFD, 0x19, 5, 0x14, 0, 0x65062,
	{ 0x2E,  0x00,  0x00,  0x88,  0x00,  0x00,  0x72,  0x60,  0x51,  0xA7,  0x79,  0x6B,  0x90,  0xBD,  0x79  },
	{ 0x217, 0x217, 0x217, 0x242, 0x242, 0x242, 0x269, 0x269, 0x269, 0x2A1, 0x2A1, 0x2A1, 0x2C9, 0x2C9, 0x2C9 }
};

static const struct ci_pt_defaults defaults_bonaire_xt =
{
	1, 0xF, 0xFD, 0x19, 5, 45, 0, 0xB0000,
	{ 0x79,  0x253, 0x25D, 0xAE,  0x72,  0x80,  0x83,  0x86,  0x6F,  0xC8,  0xC9,  0xC9,  0x2F,  0x4D,  0x61  },
	{ 0x17C, 0x172, 0x180, 0x1BC, 0x1B3, 0x1BD, 0x206, 0x200, 0x203, 0x25D, 0x25A, 0x255, 0x2C3, 0x2C5, 0x2B4 }
};

static const struct ci_pt_defaults defaults_saturn_xt =
{
	1, 0xF, 0xFD, 0x19, 5, 55, 0, 0x70000,
	{ 0x8C,  0x247, 0x249, 0xA6,  0x80,  0x81,  0x8B,  0x89,  0x86,  0xC9,  0xCA,  0xC9,  0x4D,  0x4D,  0x4D  },
	{ 0x187, 0x187, 0x187, 0x1C7, 0x1C7, 0x1C7, 0x210, 0x210, 0x210, 0x266, 0x266, 0x266, 0x2C9, 0x2C9, 0x2C9 }
};

static const struct ci_pt_config_reg didt_config_ci[] =
{
	{ 0x10, 0x000000ff, 0, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x10, 0x0000ff00, 8, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x10, 0x00ff0000, 16, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x10, 0xff000000, 24, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x11, 0x000000ff, 0, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x11, 0x0000ff00, 8, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x11, 0x00ff0000, 16, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x11, 0xff000000, 24, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x12, 0x000000ff, 0, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x12, 0x0000ff00, 8, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x12, 0x00ff0000, 16, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x12, 0xff000000, 24, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x2, 0x00003fff, 0, 0x4, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x2, 0x03ff0000, 16, 0x80, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x2, 0x78000000, 27, 0x3, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x1, 0x0000ffff, 0, 0x3FFF, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x1, 0xffff0000, 16, 0x3FFF, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x0, 0x00000001, 0, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x30, 0x000000ff, 0, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x30, 0x0000ff00, 8, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x30, 0x00ff0000, 16, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x30, 0xff000000, 24, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x31, 0x000000ff, 0, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x31, 0x0000ff00, 8, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x31, 0x00ff0000, 16, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x31, 0xff000000, 24, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x32, 0x000000ff, 0, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x32, 0x0000ff00, 8, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x32, 0x00ff0000, 16, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x32, 0xff000000, 24, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x22, 0x00003fff, 0, 0x4, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x22, 0x03ff0000, 16, 0x80, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x22, 0x78000000, 27, 0x3, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x21, 0x0000ffff, 0, 0x3FFF, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x21, 0xffff0000, 16, 0x3FFF, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x20, 0x00000001, 0, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x50, 0x000000ff, 0, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x50, 0x0000ff00, 8, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x50, 0x00ff0000, 16, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x50, 0xff000000, 24, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x51, 0x000000ff, 0, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x51, 0x0000ff00, 8, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x51, 0x00ff0000, 16, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x51, 0xff000000, 24, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x52, 0x000000ff, 0, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x52, 0x0000ff00, 8, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x52, 0x00ff0000, 16, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x52, 0xff000000, 24, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x42, 0x00003fff, 0, 0x4, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x42, 0x03ff0000, 16, 0x80, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x42, 0x78000000, 27, 0x3, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x41, 0x0000ffff, 0, 0x3FFF, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x41, 0xffff0000, 16, 0x3FFF, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x40, 0x00000001, 0, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x70, 0x000000ff, 0, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x70, 0x0000ff00, 8, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x70, 0x00ff0000, 16, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x70, 0xff000000, 24, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x71, 0x000000ff, 0, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x71, 0x0000ff00, 8, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x71, 0x00ff0000, 16, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x71, 0xff000000, 24, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x72, 0x000000ff, 0, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x72, 0x0000ff00, 8, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x72, 0x00ff0000, 16, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x72, 0xff000000, 24, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x62, 0x00003fff, 0, 0x4, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x62, 0x03ff0000, 16, 0x80, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x62, 0x78000000, 27, 0x3, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x61, 0x0000ffff, 0, 0x3FFF, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x61, 0xffff0000, 16, 0x3FFF, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0x60, 0x00000001, 0, 0x0, CISLANDS_CONFIGREG_DIDT_IND },
	{ 0xFFFFFFFF }
};

extern u8 rv770_get_memory_module_index(struct radeon_device *rdev);
extern int ni_copy_and_switch_arb_sets(struct radeon_device *rdev,
				       u32 arb_freq_src, u32 arb_freq_dest);
static int ci_get_std_voltage_value_sidd(struct radeon_device *rdev,
					 struct atom_voltage_table_entry *voltage_table,
					 u16 *std_voltage_hi_sidd, u16 *std_voltage_lo_sidd);
static int ci_set_power_limit(struct radeon_device *rdev, u32 n);
static int ci_set_overdrive_target_tdp(struct radeon_device *rdev,
				       u32 target_tdp);
static int ci_update_uvd_dpm(struct radeon_device *rdev, bool gate);

static PPSMC_Result ci_send_msg_to_smc(struct radeon_device *rdev, PPSMC_Msg msg);
static PPSMC_Result ci_send_msg_to_smc_with_parameter(struct radeon_device *rdev,
						      PPSMC_Msg msg, u32 parameter);

static void ci_thermal_start_smc_fan_control(struct radeon_device *rdev);
static void ci_fan_ctrl_set_default_mode(struct radeon_device *rdev);

static struct ci_power_info *ci_get_pi(struct radeon_device *rdev)
{
	struct ci_power_info *pi = rdev->pm.dpm.priv;

	return pi;
}

static struct ci_ps *ci_get_ps(struct radeon_ps *rps)
{
	struct ci_ps *ps = rps->ps_priv;

	return ps;
}

static void ci_initialize_powertune_defaults(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);

	switch (rdev->pdev->device) {
	case 0x6649:
	case 0x6650:
	case 0x6651:
	case 0x6658:
	case 0x665C:
	case 0x665D:
	default:
		pi->powertune_defaults = &defaults_bonaire_xt;
		break;
	case 0x6640:
	case 0x6641:
	case 0x6646:
	case 0x6647:
		pi->powertune_defaults = &defaults_saturn_xt;
		break;
	case 0x67B8:
	case 0x67B0:
		pi->powertune_defaults = &defaults_hawaii_xt;
		break;
	case 0x67BA:
	case 0x67B1:
		pi->powertune_defaults = &defaults_hawaii_pro;
		break;
	case 0x67A0:
	case 0x67A1:
	case 0x67A2:
	case 0x67A8:
	case 0x67A9:
	case 0x67AA:
	case 0x67B9:
	case 0x67BE:
		pi->powertune_defaults = &defaults_bonaire_xt;
		break;
	}

	pi->dte_tj_offset = 0;

	pi->caps_power_containment = true;
	pi->caps_cac = false;
	pi->caps_sq_ramping = false;
	pi->caps_db_ramping = false;
	pi->caps_td_ramping = false;
	pi->caps_tcp_ramping = false;

	if (pi->caps_power_containment) {
		pi->caps_cac = true;
		if (rdev->family == CHIP_HAWAII)
			pi->enable_bapm_feature = false;
		else
			pi->enable_bapm_feature = true;
		pi->enable_tdc_limit_feature = true;
		pi->enable_pkg_pwr_tracking_feature = true;
	}
}

static u8 ci_convert_to_vid(u16 vddc)
{
	return (6200 - (vddc * VOLTAGE_SCALE)) / 25;
}

static int ci_populate_bapm_vddc_vid_sidd(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u8 *hi_vid = pi->smc_powertune_table.BapmVddCVidHiSidd;
	u8 *lo_vid = pi->smc_powertune_table.BapmVddCVidLoSidd;
	u8 *hi2_vid = pi->smc_powertune_table.BapmVddCVidHiSidd2;
	u32 i;

	if (rdev->pm.dpm.dyn_state.cac_leakage_table.entries == NULL)
		return -EINVAL;
	if (rdev->pm.dpm.dyn_state.cac_leakage_table.count > 8)
		return -EINVAL;
	if (rdev->pm.dpm.dyn_state.cac_leakage_table.count !=
	    rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.count)
		return -EINVAL;

	for (i = 0; i < rdev->pm.dpm.dyn_state.cac_leakage_table.count; i++) {
		if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_EVV) {
			lo_vid[i] = ci_convert_to_vid(rdev->pm.dpm.dyn_state.cac_leakage_table.entries[i].vddc1);
			hi_vid[i] = ci_convert_to_vid(rdev->pm.dpm.dyn_state.cac_leakage_table.entries[i].vddc2);
			hi2_vid[i] = ci_convert_to_vid(rdev->pm.dpm.dyn_state.cac_leakage_table.entries[i].vddc3);
		} else {
			lo_vid[i] = ci_convert_to_vid(rdev->pm.dpm.dyn_state.cac_leakage_table.entries[i].vddc);
			hi_vid[i] = ci_convert_to_vid((u16)rdev->pm.dpm.dyn_state.cac_leakage_table.entries[i].leakage);
		}
	}
	return 0;
}

static int ci_populate_vddc_vid(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u8 *vid = pi->smc_powertune_table.VddCVid;
	u32 i;

	if (pi->vddc_voltage_table.count > 8)
		return -EINVAL;

	for (i = 0; i < pi->vddc_voltage_table.count; i++)
		vid[i] = ci_convert_to_vid(pi->vddc_voltage_table.entries[i].value);

	return 0;
}

static int ci_populate_svi_load_line(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	const struct ci_pt_defaults *pt_defaults = pi->powertune_defaults;

	pi->smc_powertune_table.SviLoadLineEn = pt_defaults->svi_load_line_en;
	pi->smc_powertune_table.SviLoadLineVddC = pt_defaults->svi_load_line_vddc;
	pi->smc_powertune_table.SviLoadLineTrimVddC = 3;
	pi->smc_powertune_table.SviLoadLineOffsetVddC = 0;

	return 0;
}

static int ci_populate_tdc_limit(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	const struct ci_pt_defaults *pt_defaults = pi->powertune_defaults;
	u16 tdc_limit;

	tdc_limit = rdev->pm.dpm.dyn_state.cac_tdp_table->tdc * 256;
	pi->smc_powertune_table.TDC_VDDC_PkgLimit = cpu_to_be16(tdc_limit);
	pi->smc_powertune_table.TDC_VDDC_ThrottleReleaseLimitPerc =
		pt_defaults->tdc_vddc_throttle_release_limit_perc;
	pi->smc_powertune_table.TDC_MAWt = pt_defaults->tdc_mawt;

	return 0;
}

static int ci_populate_dw8(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	const struct ci_pt_defaults *pt_defaults = pi->powertune_defaults;
	int ret;

	ret = ci_read_smc_sram_dword(rdev,
				     SMU7_FIRMWARE_HEADER_LOCATION +
				     offsetof(SMU7_Firmware_Header, PmFuseTable) +
				     offsetof(SMU7_Discrete_PmFuses, TdcWaterfallCtl),
				     (u32 *)&pi->smc_powertune_table.TdcWaterfallCtl,
				     pi->sram_end);
	if (ret)
		return -EINVAL;
	else
		pi->smc_powertune_table.TdcWaterfallCtl = pt_defaults->tdc_waterfall_ctl;

	return 0;
}

static int ci_populate_fuzzy_fan(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);

	if ((rdev->pm.dpm.fan.fan_output_sensitivity & (1 << 15)) ||
	    (rdev->pm.dpm.fan.fan_output_sensitivity == 0))
		rdev->pm.dpm.fan.fan_output_sensitivity =
			rdev->pm.dpm.fan.default_fan_output_sensitivity;

	pi->smc_powertune_table.FuzzyFan_PwmSetDelta =
		cpu_to_be16(rdev->pm.dpm.fan.fan_output_sensitivity);

	return 0;
}

static int ci_min_max_v_gnbl_pm_lid_from_bapm_vddc(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u8 *hi_vid = pi->smc_powertune_table.BapmVddCVidHiSidd;
	u8 *lo_vid = pi->smc_powertune_table.BapmVddCVidLoSidd;
	int i, min, max;

	min = max = hi_vid[0];
	for (i = 0; i < 8; i++) {
		if (0 != hi_vid[i]) {
			if (min > hi_vid[i])
				min = hi_vid[i];
			if (max < hi_vid[i])
				max = hi_vid[i];
		}

		if (0 != lo_vid[i]) {
			if (min > lo_vid[i])
				min = lo_vid[i];
			if (max < lo_vid[i])
				max = lo_vid[i];
		}
	}

	if ((min == 0) || (max == 0))
		return -EINVAL;
	pi->smc_powertune_table.GnbLPMLMaxVid = (u8)max;
	pi->smc_powertune_table.GnbLPMLMinVid = (u8)min;

	return 0;
}

static int ci_populate_bapm_vddc_base_leakage_sidd(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u16 hi_sidd, lo_sidd;
	struct radeon_cac_tdp_table *cac_tdp_table =
		rdev->pm.dpm.dyn_state.cac_tdp_table;

	hi_sidd = cac_tdp_table->high_cac_leakage / 100 * 256;
	lo_sidd = cac_tdp_table->low_cac_leakage / 100 * 256;

	pi->smc_powertune_table.BapmVddCBaseLeakageHiSidd = cpu_to_be16(hi_sidd);
	pi->smc_powertune_table.BapmVddCBaseLeakageLoSidd = cpu_to_be16(lo_sidd);

	return 0;
}

static int ci_populate_bapm_parameters_in_dpm_table(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	const struct ci_pt_defaults *pt_defaults = pi->powertune_defaults;
	SMU7_Discrete_DpmTable  *dpm_table = &pi->smc_state_table;
	struct radeon_cac_tdp_table *cac_tdp_table =
		rdev->pm.dpm.dyn_state.cac_tdp_table;
	struct radeon_ppm_table *ppm = rdev->pm.dpm.dyn_state.ppm_table;
	int i, j, k;
	const u16 *def1;
	const u16 *def2;

	dpm_table->DefaultTdp = cac_tdp_table->tdp * 256;
	dpm_table->TargetTdp = cac_tdp_table->configurable_tdp * 256;

	dpm_table->DTETjOffset = (u8)pi->dte_tj_offset;
	dpm_table->GpuTjMax =
		(u8)(pi->thermal_temp_setting.temperature_high / 1000);
	dpm_table->GpuTjHyst = 8;

	dpm_table->DTEAmbientTempBase = pt_defaults->dte_ambient_temp_base;

	if (ppm) {
		dpm_table->PPM_PkgPwrLimit = cpu_to_be16((u16)ppm->dgpu_tdp * 256 / 1000);
		dpm_table->PPM_TemperatureLimit = cpu_to_be16((u16)ppm->tj_max * 256);
	} else {
		dpm_table->PPM_PkgPwrLimit = cpu_to_be16(0);
		dpm_table->PPM_TemperatureLimit = cpu_to_be16(0);
	}

	dpm_table->BAPM_TEMP_GRADIENT = cpu_to_be32(pt_defaults->bapm_temp_gradient);
	def1 = pt_defaults->bapmti_r;
	def2 = pt_defaults->bapmti_rc;

	for (i = 0; i < SMU7_DTE_ITERATIONS; i++) {
		for (j = 0; j < SMU7_DTE_SOURCES; j++) {
			for (k = 0; k < SMU7_DTE_SINKS; k++) {
				dpm_table->BAPMTI_R[i][j][k] = cpu_to_be16(*def1);
				dpm_table->BAPMTI_RC[i][j][k] = cpu_to_be16(*def2);
				def1++;
				def2++;
			}
		}
	}

	return 0;
}

static int ci_populate_pm_base(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u32 pm_fuse_table_offset;
	int ret;

	if (pi->caps_power_containment) {
		ret = ci_read_smc_sram_dword(rdev,
					     SMU7_FIRMWARE_HEADER_LOCATION +
					     offsetof(SMU7_Firmware_Header, PmFuseTable),
					     &pm_fuse_table_offset, pi->sram_end);
		if (ret)
			return ret;
		ret = ci_populate_bapm_vddc_vid_sidd(rdev);
		if (ret)
			return ret;
		ret = ci_populate_vddc_vid(rdev);
		if (ret)
			return ret;
		ret = ci_populate_svi_load_line(rdev);
		if (ret)
			return ret;
		ret = ci_populate_tdc_limit(rdev);
		if (ret)
			return ret;
		ret = ci_populate_dw8(rdev);
		if (ret)
			return ret;
		ret = ci_populate_fuzzy_fan(rdev);
		if (ret)
			return ret;
		ret = ci_min_max_v_gnbl_pm_lid_from_bapm_vddc(rdev);
		if (ret)
			return ret;
		ret = ci_populate_bapm_vddc_base_leakage_sidd(rdev);
		if (ret)
			return ret;
		ret = ci_copy_bytes_to_smc(rdev, pm_fuse_table_offset,
					   (u8 *)&pi->smc_powertune_table,
					   sizeof(SMU7_Discrete_PmFuses), pi->sram_end);
		if (ret)
			return ret;
	}

	return 0;
}

static void ci_do_enable_didt(struct radeon_device *rdev, const bool enable)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u32 data;

	if (pi->caps_sq_ramping) {
		data = RREG32_DIDT(DIDT_SQ_CTRL0);
		if (enable)
			data |= DIDT_CTRL_EN;
		else
			data &= ~DIDT_CTRL_EN;
		WREG32_DIDT(DIDT_SQ_CTRL0, data);
	}

	if (pi->caps_db_ramping) {
		data = RREG32_DIDT(DIDT_DB_CTRL0);
		if (enable)
			data |= DIDT_CTRL_EN;
		else
			data &= ~DIDT_CTRL_EN;
		WREG32_DIDT(DIDT_DB_CTRL0, data);
	}

	if (pi->caps_td_ramping) {
		data = RREG32_DIDT(DIDT_TD_CTRL0);
		if (enable)
			data |= DIDT_CTRL_EN;
		else
			data &= ~DIDT_CTRL_EN;
		WREG32_DIDT(DIDT_TD_CTRL0, data);
	}

	if (pi->caps_tcp_ramping) {
		data = RREG32_DIDT(DIDT_TCP_CTRL0);
		if (enable)
			data |= DIDT_CTRL_EN;
		else
			data &= ~DIDT_CTRL_EN;
		WREG32_DIDT(DIDT_TCP_CTRL0, data);
	}
}

static int ci_program_pt_config_registers(struct radeon_device *rdev,
					  const struct ci_pt_config_reg *cac_config_regs)
{
	const struct ci_pt_config_reg *config_regs = cac_config_regs;
	u32 data;
	u32 cache = 0;

	if (config_regs == NULL)
		return -EINVAL;

	while (config_regs->offset != 0xFFFFFFFF) {
		if (config_regs->type == CISLANDS_CONFIGREG_CACHE) {
			cache |= ((config_regs->value << config_regs->shift) & config_regs->mask);
		} else {
			switch (config_regs->type) {
			case CISLANDS_CONFIGREG_SMC_IND:
				data = RREG32_SMC(config_regs->offset);
				break;
			case CISLANDS_CONFIGREG_DIDT_IND:
				data = RREG32_DIDT(config_regs->offset);
				break;
			default:
				data = RREG32(config_regs->offset << 2);
				break;
			}

			data &= ~config_regs->mask;
			data |= ((config_regs->value << config_regs->shift) & config_regs->mask);
			data |= cache;

			switch (config_regs->type) {
			case CISLANDS_CONFIGREG_SMC_IND:
				WREG32_SMC(config_regs->offset, data);
				break;
			case CISLANDS_CONFIGREG_DIDT_IND:
				WREG32_DIDT(config_regs->offset, data);
				break;
			default:
				WREG32(config_regs->offset << 2, data);
				break;
			}
			cache = 0;
		}
		config_regs++;
	}
	return 0;
}

static int ci_enable_didt(struct radeon_device *rdev, bool enable)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	int ret;

	if (pi->caps_sq_ramping || pi->caps_db_ramping ||
	    pi->caps_td_ramping || pi->caps_tcp_ramping) {
		cik_enter_rlc_safe_mode(rdev);

		if (enable) {
			ret = ci_program_pt_config_registers(rdev, didt_config_ci);
			if (ret) {
				cik_exit_rlc_safe_mode(rdev);
				return ret;
			}
		}

		ci_do_enable_didt(rdev, enable);

		cik_exit_rlc_safe_mode(rdev);
	}

	return 0;
}

static int ci_enable_power_containment(struct radeon_device *rdev, bool enable)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	PPSMC_Result smc_result;
	int ret = 0;

	if (enable) {
		pi->power_containment_features = 0;
		if (pi->caps_power_containment) {
			if (pi->enable_bapm_feature) {
				smc_result = ci_send_msg_to_smc(rdev, PPSMC_MSG_EnableDTE);
				if (smc_result != PPSMC_Result_OK)
					ret = -EINVAL;
				else
					pi->power_containment_features |= POWERCONTAINMENT_FEATURE_BAPM;
			}

			if (pi->enable_tdc_limit_feature) {
				smc_result = ci_send_msg_to_smc(rdev, PPSMC_MSG_TDCLimitEnable);
				if (smc_result != PPSMC_Result_OK)
					ret = -EINVAL;
				else
					pi->power_containment_features |= POWERCONTAINMENT_FEATURE_TDCLimit;
			}

			if (pi->enable_pkg_pwr_tracking_feature) {
				smc_result = ci_send_msg_to_smc(rdev, PPSMC_MSG_PkgPwrLimitEnable);
				if (smc_result != PPSMC_Result_OK) {
					ret = -EINVAL;
				} else {
					struct radeon_cac_tdp_table *cac_tdp_table =
						rdev->pm.dpm.dyn_state.cac_tdp_table;
					u32 default_pwr_limit =
						(u32)(cac_tdp_table->maximum_power_delivery_limit * 256);

					pi->power_containment_features |= POWERCONTAINMENT_FEATURE_PkgPwrLimit;

					ci_set_power_limit(rdev, default_pwr_limit);
				}
			}
		}
	} else {
		if (pi->caps_power_containment && pi->power_containment_features) {
			if (pi->power_containment_features & POWERCONTAINMENT_FEATURE_TDCLimit)
				ci_send_msg_to_smc(rdev, PPSMC_MSG_TDCLimitDisable);

			if (pi->power_containment_features & POWERCONTAINMENT_FEATURE_BAPM)
				ci_send_msg_to_smc(rdev, PPSMC_MSG_DisableDTE);

			if (pi->power_containment_features & POWERCONTAINMENT_FEATURE_PkgPwrLimit)
				ci_send_msg_to_smc(rdev, PPSMC_MSG_PkgPwrLimitDisable);
			pi->power_containment_features = 0;
		}
	}

	return ret;
}

static int ci_enable_smc_cac(struct radeon_device *rdev, bool enable)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	PPSMC_Result smc_result;
	int ret = 0;

	if (pi->caps_cac) {
		if (enable) {
			smc_result = ci_send_msg_to_smc(rdev, PPSMC_MSG_EnableCac);
			if (smc_result != PPSMC_Result_OK) {
				ret = -EINVAL;
				pi->cac_enabled = false;
			} else {
				pi->cac_enabled = true;
			}
		} else if (pi->cac_enabled) {
			ci_send_msg_to_smc(rdev, PPSMC_MSG_DisableCac);
			pi->cac_enabled = false;
		}
	}

	return ret;
}

static int ci_enable_thermal_based_sclk_dpm(struct radeon_device *rdev,
					    bool enable)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	PPSMC_Result smc_result = PPSMC_Result_OK;

	if (pi->thermal_sclk_dpm_enabled) {
		if (enable)
			smc_result = ci_send_msg_to_smc(rdev, PPSMC_MSG_ENABLE_THERMAL_DPM);
		else
			smc_result = ci_send_msg_to_smc(rdev, PPSMC_MSG_DISABLE_THERMAL_DPM);
	}

	if (smc_result == PPSMC_Result_OK)
		return 0;
	else
		return -EINVAL;
}

static int ci_power_control_set_level(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct radeon_cac_tdp_table *cac_tdp_table =
		rdev->pm.dpm.dyn_state.cac_tdp_table;
	s32 adjust_percent;
	s32 target_tdp;
	int ret = 0;
	bool adjust_polarity = false; /* ??? */

	if (pi->caps_power_containment) {
		adjust_percent = adjust_polarity ?
			rdev->pm.dpm.tdp_adjustment : (-1 * rdev->pm.dpm.tdp_adjustment);
		target_tdp = ((100 + adjust_percent) *
			      (s32)cac_tdp_table->configurable_tdp) / 100;

		ret = ci_set_overdrive_target_tdp(rdev, (u32)target_tdp);
	}

	return ret;
}

void ci_dpm_powergate_uvd(struct radeon_device *rdev, bool gate)
{
	struct ci_power_info *pi = ci_get_pi(rdev);

	if (pi->uvd_power_gated == gate)
		return;

	pi->uvd_power_gated = gate;

	ci_update_uvd_dpm(rdev, gate);
}

bool ci_dpm_vblank_too_short(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u32 vblank_time = r600_dpm_get_vblank_time(rdev);
	u32 switch_limit = pi->mem_gddr5 ? 450 : 300;

	/* disable mclk switching if the refresh is >120Hz, even if the
        * blanking period would allow it
        */
	if (r600_dpm_get_vrefresh(rdev) > 120)
		return true;

	if (vblank_time < switch_limit)
		return true;
	else
		return false;

}

static void ci_apply_state_adjust_rules(struct radeon_device *rdev,
					struct radeon_ps *rps)
{
	struct ci_ps *ps = ci_get_ps(rps);
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct radeon_clock_and_voltage_limits *max_limits;
	bool disable_mclk_switching;
	u32 sclk, mclk;
	int i;

	if (rps->vce_active) {
		rps->evclk = rdev->pm.dpm.vce_states[rdev->pm.dpm.vce_level].evclk;
		rps->ecclk = rdev->pm.dpm.vce_states[rdev->pm.dpm.vce_level].ecclk;
	} else {
		rps->evclk = 0;
		rps->ecclk = 0;
	}

	if ((rdev->pm.dpm.new_active_crtc_count > 1) ||
	    ci_dpm_vblank_too_short(rdev))
		disable_mclk_switching = true;
	else
		disable_mclk_switching = false;

	if ((rps->class & ATOM_PPLIB_CLASSIFICATION_UI_MASK) == ATOM_PPLIB_CLASSIFICATION_UI_BATTERY)
		pi->battery_state = true;
	else
		pi->battery_state = false;

	if (rdev->pm.dpm.ac_power)
		max_limits = &rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac;
	else
		max_limits = &rdev->pm.dpm.dyn_state.max_clock_voltage_on_dc;

	if (rdev->pm.dpm.ac_power == false) {
		for (i = 0; i < ps->performance_level_count; i++) {
			if (ps->performance_levels[i].mclk > max_limits->mclk)
				ps->performance_levels[i].mclk = max_limits->mclk;
			if (ps->performance_levels[i].sclk > max_limits->sclk)
				ps->performance_levels[i].sclk = max_limits->sclk;
		}
	}

	/* XXX validate the min clocks required for display */

	if (disable_mclk_switching) {
		mclk  = ps->performance_levels[ps->performance_level_count - 1].mclk;
		sclk = ps->performance_levels[0].sclk;
	} else {
		mclk = ps->performance_levels[0].mclk;
		sclk = ps->performance_levels[0].sclk;
	}

	if (rps->vce_active) {
		if (sclk < rdev->pm.dpm.vce_states[rdev->pm.dpm.vce_level].sclk)
			sclk = rdev->pm.dpm.vce_states[rdev->pm.dpm.vce_level].sclk;
		if (mclk < rdev->pm.dpm.vce_states[rdev->pm.dpm.vce_level].mclk)
			mclk = rdev->pm.dpm.vce_states[rdev->pm.dpm.vce_level].mclk;
	}

	ps->performance_levels[0].sclk = sclk;
	ps->performance_levels[0].mclk = mclk;

	if (ps->performance_levels[1].sclk < ps->performance_levels[0].sclk)
		ps->performance_levels[1].sclk = ps->performance_levels[0].sclk;

	if (disable_mclk_switching) {
		if (ps->performance_levels[0].mclk < ps->performance_levels[1].mclk)
			ps->performance_levels[0].mclk = ps->performance_levels[1].mclk;
	} else {
		if (ps->performance_levels[1].mclk < ps->performance_levels[0].mclk)
			ps->performance_levels[1].mclk = ps->performance_levels[0].mclk;
	}
}

static int ci_thermal_set_temperature_range(struct radeon_device *rdev,
					    int min_temp, int max_temp)
{
	int low_temp = 0 * 1000;
	int high_temp = 255 * 1000;
	u32 tmp;

	if (low_temp < min_temp)
		low_temp = min_temp;
	if (high_temp > max_temp)
		high_temp = max_temp;
	if (high_temp < low_temp) {
		DRM_ERROR("invalid thermal range: %d - %d\n", low_temp, high_temp);
		return -EINVAL;
	}

	tmp = RREG32_SMC(CG_THERMAL_INT);
	tmp &= ~(CI_DIG_THERM_INTH_MASK | CI_DIG_THERM_INTL_MASK);
	tmp |= CI_DIG_THERM_INTH(high_temp / 1000) |
		CI_DIG_THERM_INTL(low_temp / 1000);
	WREG32_SMC(CG_THERMAL_INT, tmp);

#if 0
	/* XXX: need to figure out how to handle this properly */
	tmp = RREG32_SMC(CG_THERMAL_CTRL);
	tmp &= DIG_THERM_DPM_MASK;
	tmp |= DIG_THERM_DPM(high_temp / 1000);
	WREG32_SMC(CG_THERMAL_CTRL, tmp);
#endif

	rdev->pm.dpm.thermal.min_temp = low_temp;
	rdev->pm.dpm.thermal.max_temp = high_temp;

	return 0;
}

static int ci_thermal_enable_alert(struct radeon_device *rdev,
				   bool enable)
{
	u32 thermal_int = RREG32_SMC(CG_THERMAL_INT);
	PPSMC_Result result;

	if (enable) {
		thermal_int &= ~(THERM_INT_MASK_HIGH | THERM_INT_MASK_LOW);
		WREG32_SMC(CG_THERMAL_INT, thermal_int);
		rdev->irq.dpm_thermal = false;
		result = ci_send_msg_to_smc(rdev, PPSMC_MSG_Thermal_Cntl_Enable);
		if (result != PPSMC_Result_OK) {
			DRM_DEBUG_KMS("Could not enable thermal interrupts.\n");
			return -EINVAL;
		}
	} else {
		thermal_int |= THERM_INT_MASK_HIGH | THERM_INT_MASK_LOW;
		WREG32_SMC(CG_THERMAL_INT, thermal_int);
		rdev->irq.dpm_thermal = true;
		result = ci_send_msg_to_smc(rdev, PPSMC_MSG_Thermal_Cntl_Disable);
		if (result != PPSMC_Result_OK) {
			DRM_DEBUG_KMS("Could not disable thermal interrupts.\n");
			return -EINVAL;
		}
	}

	return 0;
}

static void ci_fan_ctrl_set_static_mode(struct radeon_device *rdev, u32 mode)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u32 tmp;

	if (pi->fan_ctrl_is_in_default_mode) {
		tmp = (RREG32_SMC(CG_FDO_CTRL2) & FDO_PWM_MODE_MASK) >> FDO_PWM_MODE_SHIFT;
		pi->fan_ctrl_default_mode = tmp;
		tmp = (RREG32_SMC(CG_FDO_CTRL2) & TMIN_MASK) >> TMIN_SHIFT;
		pi->t_min = tmp;
		pi->fan_ctrl_is_in_default_mode = false;
	}

	tmp = RREG32_SMC(CG_FDO_CTRL2) & ~TMIN_MASK;
	tmp |= TMIN(0);
	WREG32_SMC(CG_FDO_CTRL2, tmp);

	tmp = RREG32_SMC(CG_FDO_CTRL2) & ~FDO_PWM_MODE_MASK;
	tmp |= FDO_PWM_MODE(mode);
	WREG32_SMC(CG_FDO_CTRL2, tmp);
}

static int ci_thermal_setup_fan_table(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	SMU7_Discrete_FanTable fan_table = { FDO_MODE_HARDWARE };
	u32 duty100;
	u32 t_diff1, t_diff2, pwm_diff1, pwm_diff2;
	u16 fdo_min, slope1, slope2;
	u32 reference_clock, tmp;
	int ret;
	u64 tmp64;

	if (!pi->fan_table_start) {
		rdev->pm.dpm.fan.ucode_fan_control = false;
		return 0;
	}

	duty100 = (RREG32_SMC(CG_FDO_CTRL1) & FMAX_DUTY100_MASK) >> FMAX_DUTY100_SHIFT;

	if (duty100 == 0) {
		rdev->pm.dpm.fan.ucode_fan_control = false;
		return 0;
	}

	tmp64 = (u64)rdev->pm.dpm.fan.pwm_min * duty100;
	do_div(tmp64, 10000);
	fdo_min = (u16)tmp64;

	t_diff1 = rdev->pm.dpm.fan.t_med - rdev->pm.dpm.fan.t_min;
	t_diff2 = rdev->pm.dpm.fan.t_high - rdev->pm.dpm.fan.t_med;

	pwm_diff1 = rdev->pm.dpm.fan.pwm_med - rdev->pm.dpm.fan.pwm_min;
	pwm_diff2 = rdev->pm.dpm.fan.pwm_high - rdev->pm.dpm.fan.pwm_med;

	slope1 = (u16)((50 + ((16 * duty100 * pwm_diff1) / t_diff1)) / 100);
	slope2 = (u16)((50 + ((16 * duty100 * pwm_diff2) / t_diff2)) / 100);

	fan_table.TempMin = cpu_to_be16((50 + rdev->pm.dpm.fan.t_min) / 100);
	fan_table.TempMed = cpu_to_be16((50 + rdev->pm.dpm.fan.t_med) / 100);
	fan_table.TempMax = cpu_to_be16((50 + rdev->pm.dpm.fan.t_max) / 100);

	fan_table.Slope1 = cpu_to_be16(slope1);
	fan_table.Slope2 = cpu_to_be16(slope2);

	fan_table.FdoMin = cpu_to_be16(fdo_min);

	fan_table.HystDown = cpu_to_be16(rdev->pm.dpm.fan.t_hyst);

	fan_table.HystUp = cpu_to_be16(1);

	fan_table.HystSlope = cpu_to_be16(1);

	fan_table.TempRespLim = cpu_to_be16(5);

	reference_clock = radeon_get_xclk(rdev);

	fan_table.RefreshPeriod = cpu_to_be32((rdev->pm.dpm.fan.cycle_delay *
					       reference_clock) / 1600);

	fan_table.FdoMax = cpu_to_be16((u16)duty100);

	tmp = (RREG32_SMC(CG_MULT_THERMAL_CTRL) & TEMP_SEL_MASK) >> TEMP_SEL_SHIFT;
	fan_table.TempSrc = (uint8_t)tmp;

	ret = ci_copy_bytes_to_smc(rdev,
				   pi->fan_table_start,
				   (u8 *)(&fan_table),
				   sizeof(fan_table),
				   pi->sram_end);

	if (ret) {
		DRM_ERROR("Failed to load fan table to the SMC.");
		rdev->pm.dpm.fan.ucode_fan_control = false;
	}

	return 0;
}

static int ci_fan_ctrl_start_smc_fan_control(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	PPSMC_Result ret;

	if (pi->caps_od_fuzzy_fan_control_support) {
		ret = ci_send_msg_to_smc_with_parameter(rdev,
							PPSMC_StartFanControl,
							FAN_CONTROL_FUZZY);
		if (ret != PPSMC_Result_OK)
			return -EINVAL;
		ret = ci_send_msg_to_smc_with_parameter(rdev,
							PPSMC_MSG_SetFanPwmMax,
							rdev->pm.dpm.fan.default_max_fan_pwm);
		if (ret != PPSMC_Result_OK)
			return -EINVAL;
	} else {
		ret = ci_send_msg_to_smc_with_parameter(rdev,
							PPSMC_StartFanControl,
							FAN_CONTROL_TABLE);
		if (ret != PPSMC_Result_OK)
			return -EINVAL;
	}

	pi->fan_is_controlled_by_smc = true;
	return 0;
}

static int ci_fan_ctrl_stop_smc_fan_control(struct radeon_device *rdev)
{
	PPSMC_Result ret;
	struct ci_power_info *pi = ci_get_pi(rdev);

	ret = ci_send_msg_to_smc(rdev, PPSMC_StopFanControl);
	if (ret == PPSMC_Result_OK) {
		pi->fan_is_controlled_by_smc = false;
		return 0;
	} else
		return -EINVAL;
}

int ci_fan_ctrl_get_fan_speed_percent(struct radeon_device *rdev,
					     u32 *speed)
{
	u32 duty, duty100;
	u64 tmp64;

	if (rdev->pm.no_fan)
		return -ENOENT;

	duty100 = (RREG32_SMC(CG_FDO_CTRL1) & FMAX_DUTY100_MASK) >> FMAX_DUTY100_SHIFT;
	duty = (RREG32_SMC(CG_THERMAL_STATUS) & FDO_PWM_DUTY_MASK) >> FDO_PWM_DUTY_SHIFT;

	if (duty100 == 0)
		return -EINVAL;

	tmp64 = (u64)duty * 100;
	do_div(tmp64, duty100);
	*speed = (u32)tmp64;

	if (*speed > 100)
		*speed = 100;

	return 0;
}

int ci_fan_ctrl_set_fan_speed_percent(struct radeon_device *rdev,
					     u32 speed)
{
	u32 tmp;
	u32 duty, duty100;
	u64 tmp64;
	struct ci_power_info *pi = ci_get_pi(rdev);

	if (rdev->pm.no_fan)
		return -ENOENT;

	if (pi->fan_is_controlled_by_smc)
		return -EINVAL;

	if (speed > 100)
		return -EINVAL;

	duty100 = (RREG32_SMC(CG_FDO_CTRL1) & FMAX_DUTY100_MASK) >> FMAX_DUTY100_SHIFT;

	if (duty100 == 0)
		return -EINVAL;

	tmp64 = (u64)speed * duty100;
	do_div(tmp64, 100);
	duty = (u32)tmp64;

	tmp = RREG32_SMC(CG_FDO_CTRL0) & ~FDO_STATIC_DUTY_MASK;
	tmp |= FDO_STATIC_DUTY(duty);
	WREG32_SMC(CG_FDO_CTRL0, tmp);

	return 0;
}

void ci_fan_ctrl_set_mode(struct radeon_device *rdev, u32 mode)
{
	if (mode) {
		/* stop auto-manage */
		if (rdev->pm.dpm.fan.ucode_fan_control)
			ci_fan_ctrl_stop_smc_fan_control(rdev);
		ci_fan_ctrl_set_static_mode(rdev, mode);
	} else {
		/* restart auto-manage */
		if (rdev->pm.dpm.fan.ucode_fan_control)
			ci_thermal_start_smc_fan_control(rdev);
		else
			ci_fan_ctrl_set_default_mode(rdev);
	}
}

u32 ci_fan_ctrl_get_mode(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u32 tmp;

	if (pi->fan_is_controlled_by_smc)
		return 0;

	tmp = RREG32_SMC(CG_FDO_CTRL2) & FDO_PWM_MODE_MASK;
	return (tmp >> FDO_PWM_MODE_SHIFT);
}

#if 0
static int ci_fan_ctrl_get_fan_speed_rpm(struct radeon_device *rdev,
					 u32 *speed)
{
	u32 tach_period;
	u32 xclk = radeon_get_xclk(rdev);

	if (rdev->pm.no_fan)
		return -ENOENT;

	if (rdev->pm.fan_pulses_per_revolution == 0)
		return -ENOENT;

	tach_period = (RREG32_SMC(CG_TACH_STATUS) & TACH_PERIOD_MASK) >> TACH_PERIOD_SHIFT;
	if (tach_period == 0)
		return -ENOENT;

	*speed = 60 * xclk * 10000 / tach_period;

	return 0;
}

static int ci_fan_ctrl_set_fan_speed_rpm(struct radeon_device *rdev,
					 u32 speed)
{
	u32 tach_period, tmp;
	u32 xclk = radeon_get_xclk(rdev);

	if (rdev->pm.no_fan)
		return -ENOENT;

	if (rdev->pm.fan_pulses_per_revolution == 0)
		return -ENOENT;

	if ((speed < rdev->pm.fan_min_rpm) ||
	    (speed > rdev->pm.fan_max_rpm))
		return -EINVAL;

	if (rdev->pm.dpm.fan.ucode_fan_control)
		ci_fan_ctrl_stop_smc_fan_control(rdev);

	tach_period = 60 * xclk * 10000 / (8 * speed);
	tmp = RREG32_SMC(CG_TACH_CTRL) & ~TARGET_PERIOD_MASK;
	tmp |= TARGET_PERIOD(tach_period);
	WREG32_SMC(CG_TACH_CTRL, tmp);

	ci_fan_ctrl_set_static_mode(rdev, FDO_PWM_MODE_STATIC_RPM);

	return 0;
}
#endif

static void ci_fan_ctrl_set_default_mode(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u32 tmp;

	if (!pi->fan_ctrl_is_in_default_mode) {
		tmp = RREG32_SMC(CG_FDO_CTRL2) & ~FDO_PWM_MODE_MASK;
		tmp |= FDO_PWM_MODE(pi->fan_ctrl_default_mode);
		WREG32_SMC(CG_FDO_CTRL2, tmp);

		tmp = RREG32_SMC(CG_FDO_CTRL2) & ~TMIN_MASK;
		tmp |= TMIN(pi->t_min);
		WREG32_SMC(CG_FDO_CTRL2, tmp);
		pi->fan_ctrl_is_in_default_mode = true;
	}
}

static void ci_thermal_start_smc_fan_control(struct radeon_device *rdev)
{
	if (rdev->pm.dpm.fan.ucode_fan_control) {
		ci_fan_ctrl_start_smc_fan_control(rdev);
		ci_fan_ctrl_set_static_mode(rdev, FDO_PWM_MODE_STATIC);
	}
}

static void ci_thermal_initialize(struct radeon_device *rdev)
{
	u32 tmp;

	if (rdev->pm.fan_pulses_per_revolution) {
		tmp = RREG32_SMC(CG_TACH_CTRL) & ~EDGE_PER_REV_MASK;
		tmp |= EDGE_PER_REV(rdev->pm.fan_pulses_per_revolution -1);
		WREG32_SMC(CG_TACH_CTRL, tmp);
	}

	tmp = RREG32_SMC(CG_FDO_CTRL2) & ~TACH_PWM_RESP_RATE_MASK;
	tmp |= TACH_PWM_RESP_RATE(0x28);
	WREG32_SMC(CG_FDO_CTRL2, tmp);
}

static int ci_thermal_start_thermal_controller(struct radeon_device *rdev)
{
	int ret;

	ci_thermal_initialize(rdev);
	ret = ci_thermal_set_temperature_range(rdev, R600_TEMP_RANGE_MIN, R600_TEMP_RANGE_MAX);
	if (ret)
		return ret;
	ret = ci_thermal_enable_alert(rdev, true);
	if (ret)
		return ret;
	if (rdev->pm.dpm.fan.ucode_fan_control) {
		ret = ci_thermal_setup_fan_table(rdev);
		if (ret)
			return ret;
		ci_thermal_start_smc_fan_control(rdev);
	}

	return 0;
}

static void ci_thermal_stop_thermal_controller(struct radeon_device *rdev)
{
	if (!rdev->pm.no_fan)
		ci_fan_ctrl_set_default_mode(rdev);
}

#if 0
static int ci_read_smc_soft_register(struct radeon_device *rdev,
				     u16 reg_offset, u32 *value)
{
	struct ci_power_info *pi = ci_get_pi(rdev);

	return ci_read_smc_sram_dword(rdev,
				      pi->soft_regs_start + reg_offset,
				      value, pi->sram_end);
}
#endif

static int ci_write_smc_soft_register(struct radeon_device *rdev,
				      u16 reg_offset, u32 value)
{
	struct ci_power_info *pi = ci_get_pi(rdev);

	return ci_write_smc_sram_dword(rdev,
				       pi->soft_regs_start + reg_offset,
				       value, pi->sram_end);
}

static void ci_init_fps_limits(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	SMU7_Discrete_DpmTable *table = &pi->smc_state_table;

	if (pi->caps_fps) {
		u16 tmp;

		tmp = 45;
		table->FpsHighT = cpu_to_be16(tmp);

		tmp = 30;
		table->FpsLowT = cpu_to_be16(tmp);
	}
}

static int ci_update_sclk_t(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	int ret = 0;
	u32 low_sclk_interrupt_t = 0;

	if (pi->caps_sclk_throttle_low_notification) {
		low_sclk_interrupt_t = cpu_to_be32(pi->low_sclk_interrupt_t);

		ret = ci_copy_bytes_to_smc(rdev,
					   pi->dpm_table_start +
					   offsetof(SMU7_Discrete_DpmTable, LowSclkInterruptT),
					   (u8 *)&low_sclk_interrupt_t,
					   sizeof(u32), pi->sram_end);

	}

	return ret;
}

static void ci_get_leakage_voltages(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u16 leakage_id, virtual_voltage_id;
	u16 vddc, vddci;
	int i;

	pi->vddc_leakage.count = 0;
	pi->vddci_leakage.count = 0;

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_EVV) {
		for (i = 0; i < CISLANDS_MAX_LEAKAGE_COUNT; i++) {
			virtual_voltage_id = ATOM_VIRTUAL_VOLTAGE_ID0 + i;
			if (radeon_atom_get_voltage_evv(rdev, virtual_voltage_id, &vddc) != 0)
				continue;
			if (vddc != 0 && vddc != virtual_voltage_id) {
				pi->vddc_leakage.actual_voltage[pi->vddc_leakage.count] = vddc;
				pi->vddc_leakage.leakage_id[pi->vddc_leakage.count] = virtual_voltage_id;
				pi->vddc_leakage.count++;
			}
		}
	} else if (radeon_atom_get_leakage_id_from_vbios(rdev, &leakage_id) == 0) {
		for (i = 0; i < CISLANDS_MAX_LEAKAGE_COUNT; i++) {
			virtual_voltage_id = ATOM_VIRTUAL_VOLTAGE_ID0 + i;
			if (radeon_atom_get_leakage_vddc_based_on_leakage_params(rdev, &vddc, &vddci,
										 virtual_voltage_id,
										 leakage_id) == 0) {
				if (vddc != 0 && vddc != virtual_voltage_id) {
					pi->vddc_leakage.actual_voltage[pi->vddc_leakage.count] = vddc;
					pi->vddc_leakage.leakage_id[pi->vddc_leakage.count] = virtual_voltage_id;
					pi->vddc_leakage.count++;
				}
				if (vddci != 0 && vddci != virtual_voltage_id) {
					pi->vddci_leakage.actual_voltage[pi->vddci_leakage.count] = vddci;
					pi->vddci_leakage.leakage_id[pi->vddci_leakage.count] = virtual_voltage_id;
					pi->vddci_leakage.count++;
				}
			}
		}
	}
}

static void ci_set_dpm_event_sources(struct radeon_device *rdev, u32 sources)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	bool want_thermal_protection;
	u32 tmp;

	switch (sources) {
	case 0:
	default:
		want_thermal_protection = false;
		break;
	case (1 << RADEON_DPM_AUTO_THROTTLE_SRC_THERMAL):
		want_thermal_protection = true;
		break;
	case (1 << RADEON_DPM_AUTO_THROTTLE_SRC_EXTERNAL):
		want_thermal_protection = true;
		break;
	case ((1 << RADEON_DPM_AUTO_THROTTLE_SRC_EXTERNAL) |
	      (1 << RADEON_DPM_AUTO_THROTTLE_SRC_THERMAL)):
		want_thermal_protection = true;
		break;
	}

	if (want_thermal_protection) {
		tmp = RREG32_SMC(GENERAL_PWRMGT);
		if (pi->thermal_protection)
			tmp &= ~THERMAL_PROTECTION_DIS;
		else
			tmp |= THERMAL_PROTECTION_DIS;
		WREG32_SMC(GENERAL_PWRMGT, tmp);
	} else {
		tmp = RREG32_SMC(GENERAL_PWRMGT);
		tmp |= THERMAL_PROTECTION_DIS;
		WREG32_SMC(GENERAL_PWRMGT, tmp);
	}
}

static void ci_enable_auto_throttle_source(struct radeon_device *rdev,
					   enum radeon_dpm_auto_throttle_src source,
					   bool enable)
{
	struct ci_power_info *pi = ci_get_pi(rdev);

	if (enable) {
		if (!(pi->active_auto_throttle_sources & (1 << source))) {
			pi->active_auto_throttle_sources |= 1 << source;
			ci_set_dpm_event_sources(rdev, pi->active_auto_throttle_sources);
		}
	} else {
		if (pi->active_auto_throttle_sources & (1 << source)) {
			pi->active_auto_throttle_sources &= ~(1 << source);
			ci_set_dpm_event_sources(rdev, pi->active_auto_throttle_sources);
		}
	}
}

static void ci_enable_vr_hot_gpio_interrupt(struct radeon_device *rdev)
{
	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_REGULATOR_HOT)
		ci_send_msg_to_smc(rdev, PPSMC_MSG_EnableVRHotGPIOInterrupt);
}

static int ci_unfreeze_sclk_mclk_dpm(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	PPSMC_Result smc_result;

	if (!pi->need_update_smu7_dpm_table)
		return 0;

	if ((!pi->sclk_dpm_key_disabled) &&
	    (pi->need_update_smu7_dpm_table & (DPMTABLE_OD_UPDATE_SCLK | DPMTABLE_UPDATE_SCLK))) {
		smc_result = ci_send_msg_to_smc(rdev, PPSMC_MSG_SCLKDPM_UnfreezeLevel);
		if (smc_result != PPSMC_Result_OK)
			return -EINVAL;
	}

	if ((!pi->mclk_dpm_key_disabled) &&
	    (pi->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_MCLK)) {
		smc_result = ci_send_msg_to_smc(rdev, PPSMC_MSG_MCLKDPM_UnfreezeLevel);
		if (smc_result != PPSMC_Result_OK)
			return -EINVAL;
	}

	pi->need_update_smu7_dpm_table = 0;
	return 0;
}

static int ci_enable_sclk_mclk_dpm(struct radeon_device *rdev, bool enable)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	PPSMC_Result smc_result;

	if (enable) {
		if (!pi->sclk_dpm_key_disabled) {
			smc_result = ci_send_msg_to_smc(rdev, PPSMC_MSG_DPM_Enable);
			if (smc_result != PPSMC_Result_OK)
				return -EINVAL;
		}

		if (!pi->mclk_dpm_key_disabled) {
			smc_result = ci_send_msg_to_smc(rdev, PPSMC_MSG_MCLKDPM_Enable);
			if (smc_result != PPSMC_Result_OK)
				return -EINVAL;

			WREG32_P(MC_SEQ_CNTL_3, CAC_EN, ~CAC_EN);

			WREG32_SMC(LCAC_MC0_CNTL, 0x05);
			WREG32_SMC(LCAC_MC1_CNTL, 0x05);
			WREG32_SMC(LCAC_CPL_CNTL, 0x100005);

			udelay(10);

			WREG32_SMC(LCAC_MC0_CNTL, 0x400005);
			WREG32_SMC(LCAC_MC1_CNTL, 0x400005);
			WREG32_SMC(LCAC_CPL_CNTL, 0x500005);
		}
	} else {
		if (!pi->sclk_dpm_key_disabled) {
			smc_result = ci_send_msg_to_smc(rdev, PPSMC_MSG_DPM_Disable);
			if (smc_result != PPSMC_Result_OK)
				return -EINVAL;
		}

		if (!pi->mclk_dpm_key_disabled) {
			smc_result = ci_send_msg_to_smc(rdev, PPSMC_MSG_MCLKDPM_Disable);
			if (smc_result != PPSMC_Result_OK)
				return -EINVAL;
		}
	}

	return 0;
}

static int ci_start_dpm(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	PPSMC_Result smc_result;
	int ret;
	u32 tmp;

	tmp = RREG32_SMC(GENERAL_PWRMGT);
	tmp |= GLOBAL_PWRMGT_EN;
	WREG32_SMC(GENERAL_PWRMGT, tmp);

	tmp = RREG32_SMC(SCLK_PWRMGT_CNTL);
	tmp |= DYNAMIC_PM_EN;
	WREG32_SMC(SCLK_PWRMGT_CNTL, tmp);

	ci_write_smc_soft_register(rdev, offsetof(SMU7_SoftRegisters, VoltageChangeTimeout), 0x1000);

	WREG32_P(BIF_LNCNT_RESET, 0, ~RESET_LNCNT_EN);

	smc_result = ci_send_msg_to_smc(rdev, PPSMC_MSG_Voltage_Cntl_Enable);
	if (smc_result != PPSMC_Result_OK)
		return -EINVAL;

	ret = ci_enable_sclk_mclk_dpm(rdev, true);
	if (ret)
		return ret;

	if (!pi->pcie_dpm_key_disabled) {
		smc_result = ci_send_msg_to_smc(rdev, PPSMC_MSG_PCIeDPM_Enable);
		if (smc_result != PPSMC_Result_OK)
			return -EINVAL;
	}

	return 0;
}

static int ci_freeze_sclk_mclk_dpm(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	PPSMC_Result smc_result;

	if (!pi->need_update_smu7_dpm_table)
		return 0;

	if ((!pi->sclk_dpm_key_disabled) &&
	    (pi->need_update_smu7_dpm_table & (DPMTABLE_OD_UPDATE_SCLK | DPMTABLE_UPDATE_SCLK))) {
		smc_result = ci_send_msg_to_smc(rdev, PPSMC_MSG_SCLKDPM_FreezeLevel);
		if (smc_result != PPSMC_Result_OK)
			return -EINVAL;
	}

	if ((!pi->mclk_dpm_key_disabled) &&
	    (pi->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_MCLK)) {
		smc_result = ci_send_msg_to_smc(rdev, PPSMC_MSG_MCLKDPM_FreezeLevel);
		if (smc_result != PPSMC_Result_OK)
			return -EINVAL;
	}

	return 0;
}

static int ci_stop_dpm(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	PPSMC_Result smc_result;
	int ret;
	u32 tmp;

	tmp = RREG32_SMC(GENERAL_PWRMGT);
	tmp &= ~GLOBAL_PWRMGT_EN;
	WREG32_SMC(GENERAL_PWRMGT, tmp);

	tmp = RREG32_SMC(SCLK_PWRMGT_CNTL);
	tmp &= ~DYNAMIC_PM_EN;
	WREG32_SMC(SCLK_PWRMGT_CNTL, tmp);

	if (!pi->pcie_dpm_key_disabled) {
		smc_result = ci_send_msg_to_smc(rdev, PPSMC_MSG_PCIeDPM_Disable);
		if (smc_result != PPSMC_Result_OK)
			return -EINVAL;
	}

	ret = ci_enable_sclk_mclk_dpm(rdev, false);
	if (ret)
		return ret;

	smc_result = ci_send_msg_to_smc(rdev, PPSMC_MSG_Voltage_Cntl_Disable);
	if (smc_result != PPSMC_Result_OK)
		return -EINVAL;

	return 0;
}

static void ci_enable_sclk_control(struct radeon_device *rdev, bool enable)
{
	u32 tmp = RREG32_SMC(SCLK_PWRMGT_CNTL);

	if (enable)
		tmp &= ~SCLK_PWRMGT_OFF;
	else
		tmp |= SCLK_PWRMGT_OFF;
	WREG32_SMC(SCLK_PWRMGT_CNTL, tmp);
}

#if 0
static int ci_notify_hw_of_power_source(struct radeon_device *rdev,
					bool ac_power)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct radeon_cac_tdp_table *cac_tdp_table =
		rdev->pm.dpm.dyn_state.cac_tdp_table;
	u32 power_limit;

	if (ac_power)
		power_limit = (u32)(cac_tdp_table->maximum_power_delivery_limit * 256);
	else
		power_limit = (u32)(cac_tdp_table->battery_power_limit * 256);

	ci_set_power_limit(rdev, power_limit);

	if (pi->caps_automatic_dc_transition) {
		if (ac_power)
			ci_send_msg_to_smc(rdev, PPSMC_MSG_RunningOnAC);
		else
			ci_send_msg_to_smc(rdev, PPSMC_MSG_Remove_DC_Clamp);
	}

	return 0;
}
#endif

static PPSMC_Result ci_send_msg_to_smc(struct radeon_device *rdev, PPSMC_Msg msg)
{
	u32 tmp;
	int i;

	if (!ci_is_smc_running(rdev))
		return PPSMC_Result_Failed;

	WREG32(SMC_MESSAGE_0, msg);

	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(SMC_RESP_0);
		if (tmp != 0)
			break;
		udelay(1);
	}
	tmp = RREG32(SMC_RESP_0);

	return (PPSMC_Result)tmp;
}

static PPSMC_Result ci_send_msg_to_smc_with_parameter(struct radeon_device *rdev,
						      PPSMC_Msg msg, u32 parameter)
{
	WREG32(SMC_MSG_ARG_0, parameter);
	return ci_send_msg_to_smc(rdev, msg);
}

static PPSMC_Result ci_send_msg_to_smc_return_parameter(struct radeon_device *rdev,
							PPSMC_Msg msg, u32 *parameter)
{
	PPSMC_Result smc_result;

	smc_result = ci_send_msg_to_smc(rdev, msg);

	if ((smc_result == PPSMC_Result_OK) && parameter)
		*parameter = RREG32(SMC_MSG_ARG_0);

	return smc_result;
}

static int ci_dpm_force_state_sclk(struct radeon_device *rdev, u32 n)
{
	struct ci_power_info *pi = ci_get_pi(rdev);

	if (!pi->sclk_dpm_key_disabled) {
		PPSMC_Result smc_result =
			ci_send_msg_to_smc_with_parameter(rdev, PPSMC_MSG_SCLKDPM_SetEnabledMask, 1 << n);
		if (smc_result != PPSMC_Result_OK)
			return -EINVAL;
	}

	return 0;
}

static int ci_dpm_force_state_mclk(struct radeon_device *rdev, u32 n)
{
	struct ci_power_info *pi = ci_get_pi(rdev);

	if (!pi->mclk_dpm_key_disabled) {
		PPSMC_Result smc_result =
			ci_send_msg_to_smc_with_parameter(rdev, PPSMC_MSG_MCLKDPM_SetEnabledMask, 1 << n);
		if (smc_result != PPSMC_Result_OK)
			return -EINVAL;
	}

	return 0;
}

static int ci_dpm_force_state_pcie(struct radeon_device *rdev, u32 n)
{
	struct ci_power_info *pi = ci_get_pi(rdev);

	if (!pi->pcie_dpm_key_disabled) {
		PPSMC_Result smc_result =
			ci_send_msg_to_smc_with_parameter(rdev, PPSMC_MSG_PCIeDPM_ForceLevel, n);
		if (smc_result != PPSMC_Result_OK)
			return -EINVAL;
	}

	return 0;
}

static int ci_set_power_limit(struct radeon_device *rdev, u32 n)
{
	struct ci_power_info *pi = ci_get_pi(rdev);

	if (pi->power_containment_features & POWERCONTAINMENT_FEATURE_PkgPwrLimit) {
		PPSMC_Result smc_result =
			ci_send_msg_to_smc_with_parameter(rdev, PPSMC_MSG_PkgPwrSetLimit, n);
		if (smc_result != PPSMC_Result_OK)
			return -EINVAL;
	}

	return 0;
}

static int ci_set_overdrive_target_tdp(struct radeon_device *rdev,
				       u32 target_tdp)
{
	PPSMC_Result smc_result =
		ci_send_msg_to_smc_with_parameter(rdev, PPSMC_MSG_OverDriveSetTargetTdp, target_tdp);
	if (smc_result != PPSMC_Result_OK)
		return -EINVAL;
	return 0;
}

#if 0
static int ci_set_boot_state(struct radeon_device *rdev)
{
	return ci_enable_sclk_mclk_dpm(rdev, false);
}
#endif

static u32 ci_get_average_sclk_freq(struct radeon_device *rdev)
{
	u32 sclk_freq;
	PPSMC_Result smc_result =
		ci_send_msg_to_smc_return_parameter(rdev,
						    PPSMC_MSG_API_GetSclkFrequency,
						    &sclk_freq);
	if (smc_result != PPSMC_Result_OK)
		sclk_freq = 0;

	return sclk_freq;
}

static u32 ci_get_average_mclk_freq(struct radeon_device *rdev)
{
	u32 mclk_freq;
	PPSMC_Result smc_result =
		ci_send_msg_to_smc_return_parameter(rdev,
						    PPSMC_MSG_API_GetMclkFrequency,
						    &mclk_freq);
	if (smc_result != PPSMC_Result_OK)
		mclk_freq = 0;

	return mclk_freq;
}

static void ci_dpm_start_smc(struct radeon_device *rdev)
{
	int i;

	ci_program_jump_on_start(rdev);
	ci_start_smc_clock(rdev);
	ci_start_smc(rdev);
	for (i = 0; i < rdev->usec_timeout; i++) {
		if (RREG32_SMC(FIRMWARE_FLAGS) & INTERRUPTS_ENABLED)
			break;
	}
}

static void ci_dpm_stop_smc(struct radeon_device *rdev)
{
	ci_reset_smc(rdev);
	ci_stop_smc_clock(rdev);
}

static int ci_process_firmware_header(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u32 tmp;
	int ret;

	ret = ci_read_smc_sram_dword(rdev,
				     SMU7_FIRMWARE_HEADER_LOCATION +
				     offsetof(SMU7_Firmware_Header, DpmTable),
				     &tmp, pi->sram_end);
	if (ret)
		return ret;

	pi->dpm_table_start = tmp;

	ret = ci_read_smc_sram_dword(rdev,
				     SMU7_FIRMWARE_HEADER_LOCATION +
				     offsetof(SMU7_Firmware_Header, SoftRegisters),
				     &tmp, pi->sram_end);
	if (ret)
		return ret;

	pi->soft_regs_start = tmp;

	ret = ci_read_smc_sram_dword(rdev,
				     SMU7_FIRMWARE_HEADER_LOCATION +
				     offsetof(SMU7_Firmware_Header, mcRegisterTable),
				     &tmp, pi->sram_end);
	if (ret)
		return ret;

	pi->mc_reg_table_start = tmp;

	ret = ci_read_smc_sram_dword(rdev,
				     SMU7_FIRMWARE_HEADER_LOCATION +
				     offsetof(SMU7_Firmware_Header, FanTable),
				     &tmp, pi->sram_end);
	if (ret)
		return ret;

	pi->fan_table_start = tmp;

	ret = ci_read_smc_sram_dword(rdev,
				     SMU7_FIRMWARE_HEADER_LOCATION +
				     offsetof(SMU7_Firmware_Header, mcArbDramTimingTable),
				     &tmp, pi->sram_end);
	if (ret)
		return ret;

	pi->arb_table_start = tmp;

	return 0;
}

static void ci_read_clock_registers(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);

	pi->clock_registers.cg_spll_func_cntl =
		RREG32_SMC(CG_SPLL_FUNC_CNTL);
	pi->clock_registers.cg_spll_func_cntl_2 =
		RREG32_SMC(CG_SPLL_FUNC_CNTL_2);
	pi->clock_registers.cg_spll_func_cntl_3 =
		RREG32_SMC(CG_SPLL_FUNC_CNTL_3);
	pi->clock_registers.cg_spll_func_cntl_4 =
		RREG32_SMC(CG_SPLL_FUNC_CNTL_4);
	pi->clock_registers.cg_spll_spread_spectrum =
		RREG32_SMC(CG_SPLL_SPREAD_SPECTRUM);
	pi->clock_registers.cg_spll_spread_spectrum_2 =
		RREG32_SMC(CG_SPLL_SPREAD_SPECTRUM_2);
	pi->clock_registers.dll_cntl = RREG32(DLL_CNTL);
	pi->clock_registers.mclk_pwrmgt_cntl = RREG32(MCLK_PWRMGT_CNTL);
	pi->clock_registers.mpll_ad_func_cntl = RREG32(MPLL_AD_FUNC_CNTL);
	pi->clock_registers.mpll_dq_func_cntl = RREG32(MPLL_DQ_FUNC_CNTL);
	pi->clock_registers.mpll_func_cntl = RREG32(MPLL_FUNC_CNTL);
	pi->clock_registers.mpll_func_cntl_1 = RREG32(MPLL_FUNC_CNTL_1);
	pi->clock_registers.mpll_func_cntl_2 = RREG32(MPLL_FUNC_CNTL_2);
	pi->clock_registers.mpll_ss1 = RREG32(MPLL_SS1);
	pi->clock_registers.mpll_ss2 = RREG32(MPLL_SS2);
}

static void ci_init_sclk_t(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);

	pi->low_sclk_interrupt_t = 0;
}

static void ci_enable_thermal_protection(struct radeon_device *rdev,
					 bool enable)
{
	u32 tmp = RREG32_SMC(GENERAL_PWRMGT);

	if (enable)
		tmp &= ~THERMAL_PROTECTION_DIS;
	else
		tmp |= THERMAL_PROTECTION_DIS;
	WREG32_SMC(GENERAL_PWRMGT, tmp);
}

static void ci_enable_acpi_power_management(struct radeon_device *rdev)
{
	u32 tmp = RREG32_SMC(GENERAL_PWRMGT);

	tmp |= STATIC_PM_EN;

	WREG32_SMC(GENERAL_PWRMGT, tmp);
}

#if 0
static int ci_enter_ulp_state(struct radeon_device *rdev)
{

	WREG32(SMC_MESSAGE_0, PPSMC_MSG_SwitchToMinimumPower);

	udelay(25000);

	return 0;
}

static int ci_exit_ulp_state(struct radeon_device *rdev)
{
	int i;

	WREG32(SMC_MESSAGE_0, PPSMC_MSG_ResumeFromMinimumPower);

	udelay(7000);

	for (i = 0; i < rdev->usec_timeout; i++) {
		if (RREG32(SMC_RESP_0) == 1)
			break;
		udelay(1000);
	}

	return 0;
}
#endif

static int ci_notify_smc_display_change(struct radeon_device *rdev,
					bool has_display)
{
	PPSMC_Msg msg = has_display ? PPSMC_MSG_HasDisplay : PPSMC_MSG_NoDisplay;

	return (ci_send_msg_to_smc(rdev, msg) == PPSMC_Result_OK) ?  0 : -EINVAL;
}

static int ci_enable_ds_master_switch(struct radeon_device *rdev,
				      bool enable)
{
	struct ci_power_info *pi = ci_get_pi(rdev);

	if (enable) {
		if (pi->caps_sclk_ds) {
			if (ci_send_msg_to_smc(rdev, PPSMC_MSG_MASTER_DeepSleep_ON) != PPSMC_Result_OK)
				return -EINVAL;
		} else {
			if (ci_send_msg_to_smc(rdev, PPSMC_MSG_MASTER_DeepSleep_OFF) != PPSMC_Result_OK)
				return -EINVAL;
		}
	} else {
		if (pi->caps_sclk_ds) {
			if (ci_send_msg_to_smc(rdev, PPSMC_MSG_MASTER_DeepSleep_OFF) != PPSMC_Result_OK)
				return -EINVAL;
		}
	}

	return 0;
}

static void ci_program_display_gap(struct radeon_device *rdev)
{
	u32 tmp = RREG32_SMC(CG_DISPLAY_GAP_CNTL);
	u32 pre_vbi_time_in_us;
	u32 frame_time_in_us;
	u32 ref_clock = rdev->clock.spll.reference_freq;
	u32 refresh_rate = r600_dpm_get_vrefresh(rdev);
	u32 vblank_time = r600_dpm_get_vblank_time(rdev);

	tmp &= ~DISP_GAP_MASK;
	if (rdev->pm.dpm.new_active_crtc_count > 0)
		tmp |= DISP_GAP(R600_PM_DISPLAY_GAP_VBLANK_OR_WM);
	else
		tmp |= DISP_GAP(R600_PM_DISPLAY_GAP_IGNORE);
	WREG32_SMC(CG_DISPLAY_GAP_CNTL, tmp);

	if (refresh_rate == 0)
		refresh_rate = 60;
	if (vblank_time == 0xffffffff)
		vblank_time = 500;
	frame_time_in_us = 1000000 / refresh_rate;
	pre_vbi_time_in_us =
		frame_time_in_us - 200 - vblank_time;
	tmp = pre_vbi_time_in_us * (ref_clock / 100);

	WREG32_SMC(CG_DISPLAY_GAP_CNTL2, tmp);
	ci_write_smc_soft_register(rdev, offsetof(SMU7_SoftRegisters, PreVBlankGap), 0x64);
	ci_write_smc_soft_register(rdev, offsetof(SMU7_SoftRegisters, VBlankTimeout), (frame_time_in_us - pre_vbi_time_in_us));


	ci_notify_smc_display_change(rdev, (rdev->pm.dpm.new_active_crtc_count == 1));

}

static void ci_enable_spread_spectrum(struct radeon_device *rdev, bool enable)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u32 tmp;

	if (enable) {
		if (pi->caps_sclk_ss_support) {
			tmp = RREG32_SMC(GENERAL_PWRMGT);
			tmp |= DYN_SPREAD_SPECTRUM_EN;
			WREG32_SMC(GENERAL_PWRMGT, tmp);
		}
	} else {
		tmp = RREG32_SMC(CG_SPLL_SPREAD_SPECTRUM);
		tmp &= ~SSEN;
		WREG32_SMC(CG_SPLL_SPREAD_SPECTRUM, tmp);

		tmp = RREG32_SMC(GENERAL_PWRMGT);
		tmp &= ~DYN_SPREAD_SPECTRUM_EN;
		WREG32_SMC(GENERAL_PWRMGT, tmp);
	}
}

static void ci_program_sstp(struct radeon_device *rdev)
{
	WREG32_SMC(CG_SSP, (SSTU(R600_SSTU_DFLT) | SST(R600_SST_DFLT)));
}

static void ci_enable_display_gap(struct radeon_device *rdev)
{
	u32 tmp = RREG32_SMC(CG_DISPLAY_GAP_CNTL);

	tmp &= ~(DISP_GAP_MASK | DISP_GAP_MCHG_MASK);
	tmp |= (DISP_GAP(R600_PM_DISPLAY_GAP_IGNORE) |
		DISP_GAP_MCHG(R600_PM_DISPLAY_GAP_VBLANK));

	WREG32_SMC(CG_DISPLAY_GAP_CNTL, tmp);
}

static void ci_program_vc(struct radeon_device *rdev)
{
	u32 tmp;

	tmp = RREG32_SMC(SCLK_PWRMGT_CNTL);
	tmp &= ~(RESET_SCLK_CNT | RESET_BUSY_CNT);
	WREG32_SMC(SCLK_PWRMGT_CNTL, tmp);

	WREG32_SMC(CG_FTV_0, CISLANDS_VRC_DFLT0);
	WREG32_SMC(CG_FTV_1, CISLANDS_VRC_DFLT1);
	WREG32_SMC(CG_FTV_2, CISLANDS_VRC_DFLT2);
	WREG32_SMC(CG_FTV_3, CISLANDS_VRC_DFLT3);
	WREG32_SMC(CG_FTV_4, CISLANDS_VRC_DFLT4);
	WREG32_SMC(CG_FTV_5, CISLANDS_VRC_DFLT5);
	WREG32_SMC(CG_FTV_6, CISLANDS_VRC_DFLT6);
	WREG32_SMC(CG_FTV_7, CISLANDS_VRC_DFLT7);
}

static void ci_clear_vc(struct radeon_device *rdev)
{
	u32 tmp;

	tmp = RREG32_SMC(SCLK_PWRMGT_CNTL);
	tmp |= (RESET_SCLK_CNT | RESET_BUSY_CNT);
	WREG32_SMC(SCLK_PWRMGT_CNTL, tmp);

	WREG32_SMC(CG_FTV_0, 0);
	WREG32_SMC(CG_FTV_1, 0);
	WREG32_SMC(CG_FTV_2, 0);
	WREG32_SMC(CG_FTV_3, 0);
	WREG32_SMC(CG_FTV_4, 0);
	WREG32_SMC(CG_FTV_5, 0);
	WREG32_SMC(CG_FTV_6, 0);
	WREG32_SMC(CG_FTV_7, 0);
}

static int ci_upload_firmware(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	int i, ret;

	for (i = 0; i < rdev->usec_timeout; i++) {
		if (RREG32_SMC(RCU_UC_EVENTS) & BOOT_SEQ_DONE)
			break;
	}
	WREG32_SMC(SMC_SYSCON_MISC_CNTL, 1);

	ci_stop_smc_clock(rdev);
	ci_reset_smc(rdev);

	ret = ci_load_smc_ucode(rdev, pi->sram_end);

	return ret;

}

static int ci_get_svi2_voltage_table(struct radeon_device *rdev,
				     struct radeon_clock_voltage_dependency_table *voltage_dependency_table,
				     struct atom_voltage_table *voltage_table)
{
	u32 i;

	if (voltage_dependency_table == NULL)
		return -EINVAL;

	voltage_table->mask_low = 0;
	voltage_table->phase_delay = 0;

	voltage_table->count = voltage_dependency_table->count;
	for (i = 0; i < voltage_table->count; i++) {
		voltage_table->entries[i].value = voltage_dependency_table->entries[i].v;
		voltage_table->entries[i].smio_low = 0;
	}

	return 0;
}

static int ci_construct_voltage_tables(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	int ret;

	if (pi->voltage_control == CISLANDS_VOLTAGE_CONTROL_BY_GPIO) {
		ret = radeon_atom_get_voltage_table(rdev, VOLTAGE_TYPE_VDDC,
						    VOLTAGE_OBJ_GPIO_LUT,
						    &pi->vddc_voltage_table);
		if (ret)
			return ret;
	} else if (pi->voltage_control == CISLANDS_VOLTAGE_CONTROL_BY_SVID2) {
		ret = ci_get_svi2_voltage_table(rdev,
						&rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk,
						&pi->vddc_voltage_table);
		if (ret)
			return ret;
	}

	if (pi->vddc_voltage_table.count > SMU7_MAX_LEVELS_VDDC)
		si_trim_voltage_table_to_fit_state_table(rdev, SMU7_MAX_LEVELS_VDDC,
							 &pi->vddc_voltage_table);

	if (pi->vddci_control == CISLANDS_VOLTAGE_CONTROL_BY_GPIO) {
		ret = radeon_atom_get_voltage_table(rdev, VOLTAGE_TYPE_VDDCI,
						    VOLTAGE_OBJ_GPIO_LUT,
						    &pi->vddci_voltage_table);
		if (ret)
			return ret;
	} else if (pi->vddci_control == CISLANDS_VOLTAGE_CONTROL_BY_SVID2) {
		ret = ci_get_svi2_voltage_table(rdev,
						&rdev->pm.dpm.dyn_state.vddci_dependency_on_mclk,
						&pi->vddci_voltage_table);
		if (ret)
			return ret;
	}

	if (pi->vddci_voltage_table.count > SMU7_MAX_LEVELS_VDDCI)
		si_trim_voltage_table_to_fit_state_table(rdev, SMU7_MAX_LEVELS_VDDCI,
							 &pi->vddci_voltage_table);

	if (pi->mvdd_control == CISLANDS_VOLTAGE_CONTROL_BY_GPIO) {
		ret = radeon_atom_get_voltage_table(rdev, VOLTAGE_TYPE_MVDDC,
						    VOLTAGE_OBJ_GPIO_LUT,
						    &pi->mvdd_voltage_table);
		if (ret)
			return ret;
	} else if (pi->mvdd_control == CISLANDS_VOLTAGE_CONTROL_BY_SVID2) {
		ret = ci_get_svi2_voltage_table(rdev,
						&rdev->pm.dpm.dyn_state.mvdd_dependency_on_mclk,
						&pi->mvdd_voltage_table);
		if (ret)
			return ret;
	}

	if (pi->mvdd_voltage_table.count > SMU7_MAX_LEVELS_MVDD)
		si_trim_voltage_table_to_fit_state_table(rdev, SMU7_MAX_LEVELS_MVDD,
							 &pi->mvdd_voltage_table);

	return 0;
}

static void ci_populate_smc_voltage_table(struct radeon_device *rdev,
					  struct atom_voltage_table_entry *voltage_table,
					  SMU7_Discrete_VoltageLevel *smc_voltage_table)
{
	int ret;

	ret = ci_get_std_voltage_value_sidd(rdev, voltage_table,
					    &smc_voltage_table->StdVoltageHiSidd,
					    &smc_voltage_table->StdVoltageLoSidd);

	if (ret) {
		smc_voltage_table->StdVoltageHiSidd = voltage_table->value * VOLTAGE_SCALE;
		smc_voltage_table->StdVoltageLoSidd = voltage_table->value * VOLTAGE_SCALE;
	}

	smc_voltage_table->Voltage = cpu_to_be16(voltage_table->value * VOLTAGE_SCALE);
	smc_voltage_table->StdVoltageHiSidd =
		cpu_to_be16(smc_voltage_table->StdVoltageHiSidd);
	smc_voltage_table->StdVoltageLoSidd =
		cpu_to_be16(smc_voltage_table->StdVoltageLoSidd);
}

static int ci_populate_smc_vddc_table(struct radeon_device *rdev,
				      SMU7_Discrete_DpmTable *table)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	unsigned int count;

	table->VddcLevelCount = pi->vddc_voltage_table.count;
	for (count = 0; count < table->VddcLevelCount; count++) {
		ci_populate_smc_voltage_table(rdev,
					      &pi->vddc_voltage_table.entries[count],
					      &table->VddcLevel[count]);

		if (pi->voltage_control == CISLANDS_VOLTAGE_CONTROL_BY_GPIO)
			table->VddcLevel[count].Smio |=
				pi->vddc_voltage_table.entries[count].smio_low;
		else
			table->VddcLevel[count].Smio = 0;
	}
	table->VddcLevelCount = cpu_to_be32(table->VddcLevelCount);

	return 0;
}

static int ci_populate_smc_vddci_table(struct radeon_device *rdev,
				       SMU7_Discrete_DpmTable *table)
{
	unsigned int count;
	struct ci_power_info *pi = ci_get_pi(rdev);

	table->VddciLevelCount = pi->vddci_voltage_table.count;
	for (count = 0; count < table->VddciLevelCount; count++) {
		ci_populate_smc_voltage_table(rdev,
					      &pi->vddci_voltage_table.entries[count],
					      &table->VddciLevel[count]);

		if (pi->vddci_control == CISLANDS_VOLTAGE_CONTROL_BY_GPIO)
			table->VddciLevel[count].Smio |=
				pi->vddci_voltage_table.entries[count].smio_low;
		else
			table->VddciLevel[count].Smio = 0;
	}
	table->VddciLevelCount = cpu_to_be32(table->VddciLevelCount);

	return 0;
}

static int ci_populate_smc_mvdd_table(struct radeon_device *rdev,
				      SMU7_Discrete_DpmTable *table)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	unsigned int count;

	table->MvddLevelCount = pi->mvdd_voltage_table.count;
	for (count = 0; count < table->MvddLevelCount; count++) {
		ci_populate_smc_voltage_table(rdev,
					      &pi->mvdd_voltage_table.entries[count],
					      &table->MvddLevel[count]);

		if (pi->mvdd_control == CISLANDS_VOLTAGE_CONTROL_BY_GPIO)
			table->MvddLevel[count].Smio |=
				pi->mvdd_voltage_table.entries[count].smio_low;
		else
			table->MvddLevel[count].Smio = 0;
	}
	table->MvddLevelCount = cpu_to_be32(table->MvddLevelCount);

	return 0;
}

static int ci_populate_smc_voltage_tables(struct radeon_device *rdev,
					  SMU7_Discrete_DpmTable *table)
{
	int ret;

	ret = ci_populate_smc_vddc_table(rdev, table);
	if (ret)
		return ret;

	ret = ci_populate_smc_vddci_table(rdev, table);
	if (ret)
		return ret;

	ret = ci_populate_smc_mvdd_table(rdev, table);
	if (ret)
		return ret;

	return 0;
}

static int ci_populate_mvdd_value(struct radeon_device *rdev, u32 mclk,
				  SMU7_Discrete_VoltageLevel *voltage)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u32 i = 0;

	if (pi->mvdd_control != CISLANDS_VOLTAGE_CONTROL_NONE) {
		for (i = 0; i < rdev->pm.dpm.dyn_state.mvdd_dependency_on_mclk.count; i++) {
			if (mclk <= rdev->pm.dpm.dyn_state.mvdd_dependency_on_mclk.entries[i].clk) {
				voltage->Voltage = pi->mvdd_voltage_table.entries[i].value;
				break;
			}
		}

		if (i >= rdev->pm.dpm.dyn_state.mvdd_dependency_on_mclk.count)
			return -EINVAL;
	}

	return -EINVAL;
}

static int ci_get_std_voltage_value_sidd(struct radeon_device *rdev,
					 struct atom_voltage_table_entry *voltage_table,
					 u16 *std_voltage_hi_sidd, u16 *std_voltage_lo_sidd)
{
	u16 v_index, idx;
	bool voltage_found = false;
	*std_voltage_hi_sidd = voltage_table->value * VOLTAGE_SCALE;
	*std_voltage_lo_sidd = voltage_table->value * VOLTAGE_SCALE;

	if (rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries == NULL)
		return -EINVAL;

	if (rdev->pm.dpm.dyn_state.cac_leakage_table.entries) {
		for (v_index = 0; (u32)v_index < rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.count; v_index++) {
			if (voltage_table->value ==
			    rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries[v_index].v) {
				voltage_found = true;
				if ((u32)v_index < rdev->pm.dpm.dyn_state.cac_leakage_table.count)
					idx = v_index;
				else
					idx = rdev->pm.dpm.dyn_state.cac_leakage_table.count - 1;
				*std_voltage_lo_sidd =
					rdev->pm.dpm.dyn_state.cac_leakage_table.entries[idx].vddc * VOLTAGE_SCALE;
				*std_voltage_hi_sidd =
					rdev->pm.dpm.dyn_state.cac_leakage_table.entries[idx].leakage * VOLTAGE_SCALE;
				break;
			}
		}

		if (!voltage_found) {
			for (v_index = 0; (u32)v_index < rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.count; v_index++) {
				if (voltage_table->value <=
				    rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries[v_index].v) {
					voltage_found = true;
					if ((u32)v_index < rdev->pm.dpm.dyn_state.cac_leakage_table.count)
						idx = v_index;
					else
						idx = rdev->pm.dpm.dyn_state.cac_leakage_table.count - 1;
					*std_voltage_lo_sidd =
						rdev->pm.dpm.dyn_state.cac_leakage_table.entries[idx].vddc * VOLTAGE_SCALE;
					*std_voltage_hi_sidd =
						rdev->pm.dpm.dyn_state.cac_leakage_table.entries[idx].leakage * VOLTAGE_SCALE;
					break;
				}
			}
		}
	}

	return 0;
}

static void ci_populate_phase_value_based_on_sclk(struct radeon_device *rdev,
						  const struct radeon_phase_shedding_limits_table *limits,
						  u32 sclk,
						  u32 *phase_shedding)
{
	unsigned int i;

	*phase_shedding = 1;

	for (i = 0; i < limits->count; i++) {
		if (sclk < limits->entries[i].sclk) {
			*phase_shedding = i;
			break;
		}
	}
}

static void ci_populate_phase_value_based_on_mclk(struct radeon_device *rdev,
						  const struct radeon_phase_shedding_limits_table *limits,
						  u32 mclk,
						  u32 *phase_shedding)
{
	unsigned int i;

	*phase_shedding = 1;

	for (i = 0; i < limits->count; i++) {
		if (mclk < limits->entries[i].mclk) {
			*phase_shedding = i;
			break;
		}
	}
}

static int ci_init_arb_table_index(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u32 tmp;
	int ret;

	ret = ci_read_smc_sram_dword(rdev, pi->arb_table_start,
				     &tmp, pi->sram_end);
	if (ret)
		return ret;

	tmp &= 0x00FFFFFF;
	tmp |= MC_CG_ARB_FREQ_F1 << 24;

	return ci_write_smc_sram_dword(rdev, pi->arb_table_start,
				       tmp, pi->sram_end);
}

static int ci_get_dependency_volt_by_clk(struct radeon_device *rdev,
					 struct radeon_clock_voltage_dependency_table *allowed_clock_voltage_table,
					 u32 clock, u32 *voltage)
{
	u32 i = 0;

	if (allowed_clock_voltage_table->count == 0)
		return -EINVAL;

	for (i = 0; i < allowed_clock_voltage_table->count; i++) {
		if (allowed_clock_voltage_table->entries[i].clk >= clock) {
			*voltage = allowed_clock_voltage_table->entries[i].v;
			return 0;
		}
	}

	*voltage = allowed_clock_voltage_table->entries[i-1].v;

	return 0;
}

static u8 ci_get_sleep_divider_id_from_clock(struct radeon_device *rdev,
					     u32 sclk, u32 min_sclk_in_sr)
{
	u32 i;
	u32 tmp;
	u32 min = (min_sclk_in_sr > CISLAND_MINIMUM_ENGINE_CLOCK) ?
		min_sclk_in_sr : CISLAND_MINIMUM_ENGINE_CLOCK;

	if (sclk < min)
		return 0;

	for (i = CISLAND_MAX_DEEPSLEEP_DIVIDER_ID;  ; i--) {
		tmp = sclk / (1 << i);
		if (tmp >= min || i == 0)
			break;
	}

	return (u8)i;
}

static int ci_initial_switch_from_arb_f0_to_f1(struct radeon_device *rdev)
{
	return ni_copy_and_switch_arb_sets(rdev, MC_CG_ARB_FREQ_F0, MC_CG_ARB_FREQ_F1);
}

static int ci_reset_to_default(struct radeon_device *rdev)
{
	return (ci_send_msg_to_smc(rdev, PPSMC_MSG_ResetToDefaults) == PPSMC_Result_OK) ?
		0 : -EINVAL;
}

static int ci_force_switch_to_arb_f0(struct radeon_device *rdev)
{
	u32 tmp;

	tmp = (RREG32_SMC(SMC_SCRATCH9) & 0x0000ff00) >> 8;

	if (tmp == MC_CG_ARB_FREQ_F0)
		return 0;

	return ni_copy_and_switch_arb_sets(rdev, tmp, MC_CG_ARB_FREQ_F0);
}

static void ci_register_patching_mc_arb(struct radeon_device *rdev,
					const u32 engine_clock,
					const u32 memory_clock,
					u32 *dram_timimg2)
{
	bool patch;
	u32 tmp, tmp2;

	tmp = RREG32(MC_SEQ_MISC0);
	patch = ((tmp & 0x0000f00) == 0x300) ? true : false;

	if (patch &&
	    ((rdev->pdev->device == 0x67B0) ||
	     (rdev->pdev->device == 0x67B1))) {
		if ((memory_clock > 100000) && (memory_clock <= 125000)) {
			tmp2 = (((0x31 * engine_clock) / 125000) - 1) & 0xff;
			*dram_timimg2 &= ~0x00ff0000;
			*dram_timimg2 |= tmp2 << 16;
		} else if ((memory_clock > 125000) && (memory_clock <= 137500)) {
			tmp2 = (((0x36 * engine_clock) / 137500) - 1) & 0xff;
			*dram_timimg2 &= ~0x00ff0000;
			*dram_timimg2 |= tmp2 << 16;
		}
	}
}


static int ci_populate_memory_timing_parameters(struct radeon_device *rdev,
						u32 sclk,
						u32 mclk,
						SMU7_Discrete_MCArbDramTimingTableEntry *arb_regs)
{
	u32 dram_timing;
	u32 dram_timing2;
	u32 burst_time;

	radeon_atom_set_engine_dram_timings(rdev, sclk, mclk);

	dram_timing  = RREG32(MC_ARB_DRAM_TIMING);
	dram_timing2 = RREG32(MC_ARB_DRAM_TIMING2);
	burst_time = RREG32(MC_ARB_BURST_TIME) & STATE0_MASK;

	ci_register_patching_mc_arb(rdev, sclk, mclk, &dram_timing2);

	arb_regs->McArbDramTiming  = cpu_to_be32(dram_timing);
	arb_regs->McArbDramTiming2 = cpu_to_be32(dram_timing2);
	arb_regs->McArbBurstTime = (u8)burst_time;

	return 0;
}

static int ci_do_program_memory_timing_parameters(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	SMU7_Discrete_MCArbDramTimingTable arb_regs;
	u32 i, j;
	int ret =  0;

	memset(&arb_regs, 0, sizeof(SMU7_Discrete_MCArbDramTimingTable));

	for (i = 0; i < pi->dpm_table.sclk_table.count; i++) {
		for (j = 0; j < pi->dpm_table.mclk_table.count; j++) {
			ret = ci_populate_memory_timing_parameters(rdev,
								   pi->dpm_table.sclk_table.dpm_levels[i].value,
								   pi->dpm_table.mclk_table.dpm_levels[j].value,
								   &arb_regs.entries[i][j]);
			if (ret)
				break;
		}
	}

	if (ret == 0)
		ret = ci_copy_bytes_to_smc(rdev,
					   pi->arb_table_start,
					   (u8 *)&arb_regs,
					   sizeof(SMU7_Discrete_MCArbDramTimingTable),
					   pi->sram_end);

	return ret;
}

static int ci_program_memory_timing_parameters(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);

	if (pi->need_update_smu7_dpm_table == 0)
		return 0;

	return ci_do_program_memory_timing_parameters(rdev);
}

static void ci_populate_smc_initial_state(struct radeon_device *rdev,
					  struct radeon_ps *radeon_boot_state)
{
	struct ci_ps *boot_state = ci_get_ps(radeon_boot_state);
	struct ci_power_info *pi = ci_get_pi(rdev);
	u32 level = 0;

	for (level = 0; level < rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.count; level++) {
		if (rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries[level].clk >=
		    boot_state->performance_levels[0].sclk) {
			pi->smc_state_table.GraphicsBootLevel = level;
			break;
		}
	}

	for (level = 0; level < rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk.count; level++) {
		if (rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk.entries[level].clk >=
		    boot_state->performance_levels[0].mclk) {
			pi->smc_state_table.MemoryBootLevel = level;
			break;
		}
	}
}

static u32 ci_get_dpm_level_enable_mask_value(struct ci_single_dpm_table *dpm_table)
{
	u32 i;
	u32 mask_value = 0;

	for (i = dpm_table->count; i > 0; i--) {
		mask_value = mask_value << 1;
		if (dpm_table->dpm_levels[i-1].enabled)
			mask_value |= 0x1;
		else
			mask_value &= 0xFFFFFFFE;
	}

	return mask_value;
}

static void ci_populate_smc_link_level(struct radeon_device *rdev,
				       SMU7_Discrete_DpmTable *table)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct ci_dpm_table *dpm_table = &pi->dpm_table;
	u32 i;

	for (i = 0; i < dpm_table->pcie_speed_table.count; i++) {
		table->LinkLevel[i].PcieGenSpeed =
			(u8)dpm_table->pcie_speed_table.dpm_levels[i].value;
		table->LinkLevel[i].PcieLaneCount =
			r600_encode_pci_lane_width(dpm_table->pcie_speed_table.dpm_levels[i].param1);
		table->LinkLevel[i].EnabledForActivity = 1;
		table->LinkLevel[i].DownT = cpu_to_be32(5);
		table->LinkLevel[i].UpT = cpu_to_be32(30);
	}

	pi->smc_state_table.LinkLevelCount = (u8)dpm_table->pcie_speed_table.count;
	pi->dpm_level_enable_mask.pcie_dpm_enable_mask =
		ci_get_dpm_level_enable_mask_value(&dpm_table->pcie_speed_table);
}

static int ci_populate_smc_uvd_level(struct radeon_device *rdev,
				     SMU7_Discrete_DpmTable *table)
{
	u32 count;
	struct atom_clock_dividers dividers;
	int ret = -EINVAL;

	table->UvdLevelCount =
		rdev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.count;

	for (count = 0; count < table->UvdLevelCount; count++) {
		table->UvdLevel[count].VclkFrequency =
			rdev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.entries[count].vclk;
		table->UvdLevel[count].DclkFrequency =
			rdev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.entries[count].dclk;
		table->UvdLevel[count].MinVddc =
			rdev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.entries[count].v * VOLTAGE_SCALE;
		table->UvdLevel[count].MinVddcPhases = 1;

		ret = radeon_atom_get_clock_dividers(rdev,
						     COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK,
						     table->UvdLevel[count].VclkFrequency, false, &dividers);
		if (ret)
			return ret;

		table->UvdLevel[count].VclkDivider = (u8)dividers.post_divider;

		ret = radeon_atom_get_clock_dividers(rdev,
						     COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK,
						     table->UvdLevel[count].DclkFrequency, false, &dividers);
		if (ret)
			return ret;

		table->UvdLevel[count].DclkDivider = (u8)dividers.post_divider;

		table->UvdLevel[count].VclkFrequency = cpu_to_be32(table->UvdLevel[count].VclkFrequency);
		table->UvdLevel[count].DclkFrequency = cpu_to_be32(table->UvdLevel[count].DclkFrequency);
		table->UvdLevel[count].MinVddc = cpu_to_be16(table->UvdLevel[count].MinVddc);
	}

	return ret;
}

static int ci_populate_smc_vce_level(struct radeon_device *rdev,
				     SMU7_Discrete_DpmTable *table)
{
	u32 count;
	struct atom_clock_dividers dividers;
	int ret = -EINVAL;

	table->VceLevelCount =
		rdev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.count;

	for (count = 0; count < table->VceLevelCount; count++) {
		table->VceLevel[count].Frequency =
			rdev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.entries[count].evclk;
		table->VceLevel[count].MinVoltage =
			(u16)rdev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.entries[count].v * VOLTAGE_SCALE;
		table->VceLevel[count].MinPhases = 1;

		ret = radeon_atom_get_clock_dividers(rdev,
						     COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK,
						     table->VceLevel[count].Frequency, false, &dividers);
		if (ret)
			return ret;

		table->VceLevel[count].Divider = (u8)dividers.post_divider;

		table->VceLevel[count].Frequency = cpu_to_be32(table->VceLevel[count].Frequency);
		table->VceLevel[count].MinVoltage = cpu_to_be16(table->VceLevel[count].MinVoltage);
	}

	return ret;

}

static int ci_populate_smc_acp_level(struct radeon_device *rdev,
				     SMU7_Discrete_DpmTable *table)
{
	u32 count;
	struct atom_clock_dividers dividers;
	int ret = -EINVAL;

	table->AcpLevelCount = (u8)
		(rdev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table.count);

	for (count = 0; count < table->AcpLevelCount; count++) {
		table->AcpLevel[count].Frequency =
			rdev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table.entries[count].clk;
		table->AcpLevel[count].MinVoltage =
			rdev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table.entries[count].v;
		table->AcpLevel[count].MinPhases = 1;

		ret = radeon_atom_get_clock_dividers(rdev,
						     COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK,
						     table->AcpLevel[count].Frequency, false, &dividers);
		if (ret)
			return ret;

		table->AcpLevel[count].Divider = (u8)dividers.post_divider;

		table->AcpLevel[count].Frequency = cpu_to_be32(table->AcpLevel[count].Frequency);
		table->AcpLevel[count].MinVoltage = cpu_to_be16(table->AcpLevel[count].MinVoltage);
	}

	return ret;
}

static int ci_populate_smc_samu_level(struct radeon_device *rdev,
				      SMU7_Discrete_DpmTable *table)
{
	u32 count;
	struct atom_clock_dividers dividers;
	int ret = -EINVAL;

	table->SamuLevelCount =
		rdev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table.count;

	for (count = 0; count < table->SamuLevelCount; count++) {
		table->SamuLevel[count].Frequency =
			rdev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table.entries[count].clk;
		table->SamuLevel[count].MinVoltage =
			rdev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table.entries[count].v * VOLTAGE_SCALE;
		table->SamuLevel[count].MinPhases = 1;

		ret = radeon_atom_get_clock_dividers(rdev,
						     COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK,
						     table->SamuLevel[count].Frequency, false, &dividers);
		if (ret)
			return ret;

		table->SamuLevel[count].Divider = (u8)dividers.post_divider;

		table->SamuLevel[count].Frequency = cpu_to_be32(table->SamuLevel[count].Frequency);
		table->SamuLevel[count].MinVoltage = cpu_to_be16(table->SamuLevel[count].MinVoltage);
	}

	return ret;
}

static int ci_calculate_mclk_params(struct radeon_device *rdev,
				    u32 memory_clock,
				    SMU7_Discrete_MemoryLevel *mclk,
				    bool strobe_mode,
				    bool dll_state_on)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u32  dll_cntl = pi->clock_registers.dll_cntl;
	u32  mclk_pwrmgt_cntl = pi->clock_registers.mclk_pwrmgt_cntl;
	u32  mpll_ad_func_cntl = pi->clock_registers.mpll_ad_func_cntl;
	u32  mpll_dq_func_cntl = pi->clock_registers.mpll_dq_func_cntl;
	u32  mpll_func_cntl = pi->clock_registers.mpll_func_cntl;
	u32  mpll_func_cntl_1 = pi->clock_registers.mpll_func_cntl_1;
	u32  mpll_func_cntl_2 = pi->clock_registers.mpll_func_cntl_2;
	u32  mpll_ss1 = pi->clock_registers.mpll_ss1;
	u32  mpll_ss2 = pi->clock_registers.mpll_ss2;
	struct atom_mpll_param mpll_param;
	int ret;

	ret = radeon_atom_get_memory_pll_dividers(rdev, memory_clock, strobe_mode, &mpll_param);
	if (ret)
		return ret;

	mpll_func_cntl &= ~BWCTRL_MASK;
	mpll_func_cntl |= BWCTRL(mpll_param.bwcntl);

	mpll_func_cntl_1 &= ~(CLKF_MASK | CLKFRAC_MASK | VCO_MODE_MASK);
	mpll_func_cntl_1 |= CLKF(mpll_param.clkf) |
		CLKFRAC(mpll_param.clkfrac) | VCO_MODE(mpll_param.vco_mode);

	mpll_ad_func_cntl &= ~YCLK_POST_DIV_MASK;
	mpll_ad_func_cntl |= YCLK_POST_DIV(mpll_param.post_div);

	if (pi->mem_gddr5) {
		mpll_dq_func_cntl &= ~(YCLK_SEL_MASK | YCLK_POST_DIV_MASK);
		mpll_dq_func_cntl |= YCLK_SEL(mpll_param.yclk_sel) |
			YCLK_POST_DIV(mpll_param.post_div);
	}

	if (pi->caps_mclk_ss_support) {
		struct radeon_atom_ss ss;
		u32 freq_nom;
		u32 tmp;
		u32 reference_clock = rdev->clock.mpll.reference_freq;

		if (mpll_param.qdr == 1)
			freq_nom = memory_clock * 4 * (1 << mpll_param.post_div);
		else
			freq_nom = memory_clock * 2 * (1 << mpll_param.post_div);

		tmp = (freq_nom / reference_clock);
		tmp = tmp * tmp;
		if (radeon_atombios_get_asic_ss_info(rdev, &ss,
						     ASIC_INTERNAL_MEMORY_SS, freq_nom)) {
			u32 clks = reference_clock * 5 / ss.rate;
			u32 clkv = (u32)((((131 * ss.percentage * ss.rate) / 100) * tmp) / freq_nom);

			mpll_ss1 &= ~CLKV_MASK;
			mpll_ss1 |= CLKV(clkv);

			mpll_ss2 &= ~CLKS_MASK;
			mpll_ss2 |= CLKS(clks);
		}
	}

	mclk_pwrmgt_cntl &= ~DLL_SPEED_MASK;
	mclk_pwrmgt_cntl |= DLL_SPEED(mpll_param.dll_speed);

	if (dll_state_on)
		mclk_pwrmgt_cntl |= MRDCK0_PDNB | MRDCK1_PDNB;
	else
		mclk_pwrmgt_cntl &= ~(MRDCK0_PDNB | MRDCK1_PDNB);

	mclk->MclkFrequency = memory_clock;
	mclk->MpllFuncCntl = mpll_func_cntl;
	mclk->MpllFuncCntl_1 = mpll_func_cntl_1;
	mclk->MpllFuncCntl_2 = mpll_func_cntl_2;
	mclk->MpllAdFuncCntl = mpll_ad_func_cntl;
	mclk->MpllDqFuncCntl = mpll_dq_func_cntl;
	mclk->MclkPwrmgtCntl = mclk_pwrmgt_cntl;
	mclk->DllCntl = dll_cntl;
	mclk->MpllSs1 = mpll_ss1;
	mclk->MpllSs2 = mpll_ss2;

	return 0;
}

static int ci_populate_single_memory_level(struct radeon_device *rdev,
					   u32 memory_clock,
					   SMU7_Discrete_MemoryLevel *memory_level)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	int ret;
	bool dll_state_on;

	if (rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk.entries) {
		ret = ci_get_dependency_volt_by_clk(rdev,
						    &rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk,
						    memory_clock, &memory_level->MinVddc);
		if (ret)
			return ret;
	}

	if (rdev->pm.dpm.dyn_state.vddci_dependency_on_mclk.entries) {
		ret = ci_get_dependency_volt_by_clk(rdev,
						    &rdev->pm.dpm.dyn_state.vddci_dependency_on_mclk,
						    memory_clock, &memory_level->MinVddci);
		if (ret)
			return ret;
	}

	if (rdev->pm.dpm.dyn_state.mvdd_dependency_on_mclk.entries) {
		ret = ci_get_dependency_volt_by_clk(rdev,
						    &rdev->pm.dpm.dyn_state.mvdd_dependency_on_mclk,
						    memory_clock, &memory_level->MinMvdd);
		if (ret)
			return ret;
	}

	memory_level->MinVddcPhases = 1;

	if (pi->vddc_phase_shed_control)
		ci_populate_phase_value_based_on_mclk(rdev,
						      &rdev->pm.dpm.dyn_state.phase_shedding_limits_table,
						      memory_clock,
						      &memory_level->MinVddcPhases);

	memory_level->EnabledForThrottle = 1;
	memory_level->UpH = 0;
	memory_level->DownH = 100;
	memory_level->VoltageDownH = 0;
	memory_level->ActivityLevel = (u16)pi->mclk_activity_target;

	memory_level->StutterEnable = false;
	memory_level->StrobeEnable = false;
	memory_level->EdcReadEnable = false;
	memory_level->EdcWriteEnable = false;
	memory_level->RttEnable = false;

	memory_level->DisplayWatermark = PPSMC_DISPLAY_WATERMARK_LOW;

	if (pi->mclk_stutter_mode_threshold &&
	    (memory_clock <= pi->mclk_stutter_mode_threshold) &&
	    (pi->uvd_enabled == false) &&
	    (RREG32(DPG_PIPE_STUTTER_CONTROL) & STUTTER_ENABLE) &&
	    (rdev->pm.dpm.new_active_crtc_count <= 2))
		memory_level->StutterEnable = true;

	if (pi->mclk_strobe_mode_threshold &&
	    (memory_clock <= pi->mclk_strobe_mode_threshold))
		memory_level->StrobeEnable = 1;

	if (pi->mem_gddr5) {
		memory_level->StrobeRatio =
			si_get_mclk_frequency_ratio(memory_clock, memory_level->StrobeEnable);
		if (pi->mclk_edc_enable_threshold &&
		    (memory_clock > pi->mclk_edc_enable_threshold))
			memory_level->EdcReadEnable = true;

		if (pi->mclk_edc_wr_enable_threshold &&
		    (memory_clock > pi->mclk_edc_wr_enable_threshold))
			memory_level->EdcWriteEnable = true;

		if (memory_level->StrobeEnable) {
			if (si_get_mclk_frequency_ratio(memory_clock, true) >=
			    ((RREG32(MC_SEQ_MISC7) >> 16) & 0xf))
				dll_state_on = ((RREG32(MC_SEQ_MISC5) >> 1) & 0x1) ? true : false;
			else
				dll_state_on = ((RREG32(MC_SEQ_MISC6) >> 1) & 0x1) ? true : false;
		} else {
			dll_state_on = pi->dll_default_on;
		}
	} else {
		memory_level->StrobeRatio = si_get_ddr3_mclk_frequency_ratio(memory_clock);
		dll_state_on = ((RREG32(MC_SEQ_MISC5) >> 1) & 0x1) ? true : false;
	}

	ret = ci_calculate_mclk_params(rdev, memory_clock, memory_level, memory_level->StrobeEnable, dll_state_on);
	if (ret)
		return ret;

	memory_level->MinVddc = cpu_to_be32(memory_level->MinVddc * VOLTAGE_SCALE);
	memory_level->MinVddcPhases = cpu_to_be32(memory_level->MinVddcPhases);
	memory_level->MinVddci = cpu_to_be32(memory_level->MinVddci * VOLTAGE_SCALE);
	memory_level->MinMvdd = cpu_to_be32(memory_level->MinMvdd * VOLTAGE_SCALE);

	memory_level->MclkFrequency = cpu_to_be32(memory_level->MclkFrequency);
	memory_level->ActivityLevel = cpu_to_be16(memory_level->ActivityLevel);
	memory_level->MpllFuncCntl = cpu_to_be32(memory_level->MpllFuncCntl);
	memory_level->MpllFuncCntl_1 = cpu_to_be32(memory_level->MpllFuncCntl_1);
	memory_level->MpllFuncCntl_2 = cpu_to_be32(memory_level->MpllFuncCntl_2);
	memory_level->MpllAdFuncCntl = cpu_to_be32(memory_level->MpllAdFuncCntl);
	memory_level->MpllDqFuncCntl = cpu_to_be32(memory_level->MpllDqFuncCntl);
	memory_level->MclkPwrmgtCntl = cpu_to_be32(memory_level->MclkPwrmgtCntl);
	memory_level->DllCntl = cpu_to_be32(memory_level->DllCntl);
	memory_level->MpllSs1 = cpu_to_be32(memory_level->MpllSs1);
	memory_level->MpllSs2 = cpu_to_be32(memory_level->MpllSs2);

	return 0;
}

static int ci_populate_smc_acpi_level(struct radeon_device *rdev,
				      SMU7_Discrete_DpmTable *table)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct atom_clock_dividers dividers;
	SMU7_Discrete_VoltageLevel voltage_level;
	u32 spll_func_cntl = pi->clock_registers.cg_spll_func_cntl;
	u32 spll_func_cntl_2 = pi->clock_registers.cg_spll_func_cntl_2;
	u32 dll_cntl = pi->clock_registers.dll_cntl;
	u32 mclk_pwrmgt_cntl = pi->clock_registers.mclk_pwrmgt_cntl;
	int ret;

	table->ACPILevel.Flags &= ~PPSMC_SWSTATE_FLAG_DC;

	if (pi->acpi_vddc)
		table->ACPILevel.MinVddc = cpu_to_be32(pi->acpi_vddc * VOLTAGE_SCALE);
	else
		table->ACPILevel.MinVddc = cpu_to_be32(pi->min_vddc_in_pp_table * VOLTAGE_SCALE);

	table->ACPILevel.MinVddcPhases = pi->vddc_phase_shed_control ? 0 : 1;

	table->ACPILevel.SclkFrequency = rdev->clock.spll.reference_freq;

	ret = radeon_atom_get_clock_dividers(rdev,
					     COMPUTE_GPUCLK_INPUT_FLAG_SCLK,
					     table->ACPILevel.SclkFrequency, false, &dividers);
	if (ret)
		return ret;

	table->ACPILevel.SclkDid = (u8)dividers.post_divider;
	table->ACPILevel.DisplayWatermark = PPSMC_DISPLAY_WATERMARK_LOW;
	table->ACPILevel.DeepSleepDivId = 0;

	spll_func_cntl &= ~SPLL_PWRON;
	spll_func_cntl |= SPLL_RESET;

	spll_func_cntl_2 &= ~SCLK_MUX_SEL_MASK;
	spll_func_cntl_2 |= SCLK_MUX_SEL(4);

	table->ACPILevel.CgSpllFuncCntl = spll_func_cntl;
	table->ACPILevel.CgSpllFuncCntl2 = spll_func_cntl_2;
	table->ACPILevel.CgSpllFuncCntl3 = pi->clock_registers.cg_spll_func_cntl_3;
	table->ACPILevel.CgSpllFuncCntl4 = pi->clock_registers.cg_spll_func_cntl_4;
	table->ACPILevel.SpllSpreadSpectrum = pi->clock_registers.cg_spll_spread_spectrum;
	table->ACPILevel.SpllSpreadSpectrum2 = pi->clock_registers.cg_spll_spread_spectrum_2;
	table->ACPILevel.CcPwrDynRm = 0;
	table->ACPILevel.CcPwrDynRm1 = 0;

	table->ACPILevel.Flags = cpu_to_be32(table->ACPILevel.Flags);
	table->ACPILevel.MinVddcPhases = cpu_to_be32(table->ACPILevel.MinVddcPhases);
	table->ACPILevel.SclkFrequency = cpu_to_be32(table->ACPILevel.SclkFrequency);
	table->ACPILevel.CgSpllFuncCntl = cpu_to_be32(table->ACPILevel.CgSpllFuncCntl);
	table->ACPILevel.CgSpllFuncCntl2 = cpu_to_be32(table->ACPILevel.CgSpllFuncCntl2);
	table->ACPILevel.CgSpllFuncCntl3 = cpu_to_be32(table->ACPILevel.CgSpllFuncCntl3);
	table->ACPILevel.CgSpllFuncCntl4 = cpu_to_be32(table->ACPILevel.CgSpllFuncCntl4);
	table->ACPILevel.SpllSpreadSpectrum = cpu_to_be32(table->ACPILevel.SpllSpreadSpectrum);
	table->ACPILevel.SpllSpreadSpectrum2 = cpu_to_be32(table->ACPILevel.SpllSpreadSpectrum2);
	table->ACPILevel.CcPwrDynRm = cpu_to_be32(table->ACPILevel.CcPwrDynRm);
	table->ACPILevel.CcPwrDynRm1 = cpu_to_be32(table->ACPILevel.CcPwrDynRm1);

	table->MemoryACPILevel.MinVddc = table->ACPILevel.MinVddc;
	table->MemoryACPILevel.MinVddcPhases = table->ACPILevel.MinVddcPhases;

	if (pi->vddci_control != CISLANDS_VOLTAGE_CONTROL_NONE) {
		if (pi->acpi_vddci)
			table->MemoryACPILevel.MinVddci =
				cpu_to_be32(pi->acpi_vddci * VOLTAGE_SCALE);
		else
			table->MemoryACPILevel.MinVddci =
				cpu_to_be32(pi->min_vddci_in_pp_table * VOLTAGE_SCALE);
	}

	if (ci_populate_mvdd_value(rdev, 0, &voltage_level))
		table->MemoryACPILevel.MinMvdd = 0;
	else
		table->MemoryACPILevel.MinMvdd =
			cpu_to_be32(voltage_level.Voltage * VOLTAGE_SCALE);

	mclk_pwrmgt_cntl |= MRDCK0_RESET | MRDCK1_RESET;
	mclk_pwrmgt_cntl &= ~(MRDCK0_PDNB | MRDCK1_PDNB);

	dll_cntl &= ~(MRDCK0_BYPASS | MRDCK1_BYPASS);

	table->MemoryACPILevel.DllCntl = cpu_to_be32(dll_cntl);
	table->MemoryACPILevel.MclkPwrmgtCntl = cpu_to_be32(mclk_pwrmgt_cntl);
	table->MemoryACPILevel.MpllAdFuncCntl =
		cpu_to_be32(pi->clock_registers.mpll_ad_func_cntl);
	table->MemoryACPILevel.MpllDqFuncCntl =
		cpu_to_be32(pi->clock_registers.mpll_dq_func_cntl);
	table->MemoryACPILevel.MpllFuncCntl =
		cpu_to_be32(pi->clock_registers.mpll_func_cntl);
	table->MemoryACPILevel.MpllFuncCntl_1 =
		cpu_to_be32(pi->clock_registers.mpll_func_cntl_1);
	table->MemoryACPILevel.MpllFuncCntl_2 =
		cpu_to_be32(pi->clock_registers.mpll_func_cntl_2);
	table->MemoryACPILevel.MpllSs1 = cpu_to_be32(pi->clock_registers.mpll_ss1);
	table->MemoryACPILevel.MpllSs2 = cpu_to_be32(pi->clock_registers.mpll_ss2);

	table->MemoryACPILevel.EnabledForThrottle = 0;
	table->MemoryACPILevel.EnabledForActivity = 0;
	table->MemoryACPILevel.UpH = 0;
	table->MemoryACPILevel.DownH = 100;
	table->MemoryACPILevel.VoltageDownH = 0;
	table->MemoryACPILevel.ActivityLevel =
		cpu_to_be16((u16)pi->mclk_activity_target);

	table->MemoryACPILevel.StutterEnable = false;
	table->MemoryACPILevel.StrobeEnable = false;
	table->MemoryACPILevel.EdcReadEnable = false;
	table->MemoryACPILevel.EdcWriteEnable = false;
	table->MemoryACPILevel.RttEnable = false;

	return 0;
}


static int ci_enable_ulv(struct radeon_device *rdev, bool enable)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct ci_ulv_parm *ulv = &pi->ulv;

	if (ulv->supported) {
		if (enable)
			return (ci_send_msg_to_smc(rdev, PPSMC_MSG_EnableULV) == PPSMC_Result_OK) ?
				0 : -EINVAL;
		else
			return (ci_send_msg_to_smc(rdev, PPSMC_MSG_DisableULV) == PPSMC_Result_OK) ?
				0 : -EINVAL;
	}

	return 0;
}

static int ci_populate_ulv_level(struct radeon_device *rdev,
				 SMU7_Discrete_Ulv *state)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u16 ulv_voltage = rdev->pm.dpm.backbias_response_time;

	state->CcPwrDynRm = 0;
	state->CcPwrDynRm1 = 0;

	if (ulv_voltage == 0) {
		pi->ulv.supported = false;
		return 0;
	}

	if (pi->voltage_control != CISLANDS_VOLTAGE_CONTROL_BY_SVID2) {
		if (ulv_voltage > rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries[0].v)
			state->VddcOffset = 0;
		else
			state->VddcOffset =
				rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries[0].v - ulv_voltage;
	} else {
		if (ulv_voltage > rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries[0].v)
			state->VddcOffsetVid = 0;
		else
			state->VddcOffsetVid = (u8)
				((rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries[0].v - ulv_voltage) *
				 VOLTAGE_VID_OFFSET_SCALE2 / VOLTAGE_VID_OFFSET_SCALE1);
	}
	state->VddcPhase = pi->vddc_phase_shed_control ? 0 : 1;

	state->CcPwrDynRm = cpu_to_be32(state->CcPwrDynRm);
	state->CcPwrDynRm1 = cpu_to_be32(state->CcPwrDynRm1);
	state->VddcOffset = cpu_to_be16(state->VddcOffset);

	return 0;
}

static int ci_calculate_sclk_params(struct radeon_device *rdev,
				    u32 engine_clock,
				    SMU7_Discrete_GraphicsLevel *sclk)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct atom_clock_dividers dividers;
	u32 spll_func_cntl_3 = pi->clock_registers.cg_spll_func_cntl_3;
	u32 spll_func_cntl_4 = pi->clock_registers.cg_spll_func_cntl_4;
	u32 cg_spll_spread_spectrum = pi->clock_registers.cg_spll_spread_spectrum;
	u32 cg_spll_spread_spectrum_2 = pi->clock_registers.cg_spll_spread_spectrum_2;
	u32 reference_clock = rdev->clock.spll.reference_freq;
	u32 reference_divider;
	u32 fbdiv;
	int ret;

	ret = radeon_atom_get_clock_dividers(rdev,
					     COMPUTE_GPUCLK_INPUT_FLAG_SCLK,
					     engine_clock, false, &dividers);
	if (ret)
		return ret;

	reference_divider = 1 + dividers.ref_div;
	fbdiv = dividers.fb_div & 0x3FFFFFF;

	spll_func_cntl_3 &= ~SPLL_FB_DIV_MASK;
	spll_func_cntl_3 |= SPLL_FB_DIV(fbdiv);
	spll_func_cntl_3 |= SPLL_DITHEN;

	if (pi->caps_sclk_ss_support) {
		struct radeon_atom_ss ss;
		u32 vco_freq = engine_clock * dividers.post_div;

		if (radeon_atombios_get_asic_ss_info(rdev, &ss,
						     ASIC_INTERNAL_ENGINE_SS, vco_freq)) {
			u32 clk_s = reference_clock * 5 / (reference_divider * ss.rate);
			u32 clk_v = 4 * ss.percentage * fbdiv / (clk_s * 10000);

			cg_spll_spread_spectrum &= ~CLK_S_MASK;
			cg_spll_spread_spectrum |= CLK_S(clk_s);
			cg_spll_spread_spectrum |= SSEN;

			cg_spll_spread_spectrum_2 &= ~CLK_V_MASK;
			cg_spll_spread_spectrum_2 |= CLK_V(clk_v);
		}
	}

	sclk->SclkFrequency = engine_clock;
	sclk->CgSpllFuncCntl3 = spll_func_cntl_3;
	sclk->CgSpllFuncCntl4 = spll_func_cntl_4;
	sclk->SpllSpreadSpectrum = cg_spll_spread_spectrum;
	sclk->SpllSpreadSpectrum2  = cg_spll_spread_spectrum_2;
	sclk->SclkDid = (u8)dividers.post_divider;

	return 0;
}

static int ci_populate_single_graphic_level(struct radeon_device *rdev,
					    u32 engine_clock,
					    u16 sclk_activity_level_t,
					    SMU7_Discrete_GraphicsLevel *graphic_level)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	int ret;

	ret = ci_calculate_sclk_params(rdev, engine_clock, graphic_level);
	if (ret)
		return ret;

	ret = ci_get_dependency_volt_by_clk(rdev,
					    &rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk,
					    engine_clock, &graphic_level->MinVddc);
	if (ret)
		return ret;

	graphic_level->SclkFrequency = engine_clock;

	graphic_level->Flags =  0;
	graphic_level->MinVddcPhases = 1;

	if (pi->vddc_phase_shed_control)
		ci_populate_phase_value_based_on_sclk(rdev,
						      &rdev->pm.dpm.dyn_state.phase_shedding_limits_table,
						      engine_clock,
						      &graphic_level->MinVddcPhases);

	graphic_level->ActivityLevel = sclk_activity_level_t;

	graphic_level->CcPwrDynRm = 0;
	graphic_level->CcPwrDynRm1 = 0;
	graphic_level->EnabledForThrottle = 1;
	graphic_level->UpH = 0;
	graphic_level->DownH = 0;
	graphic_level->VoltageDownH = 0;
	graphic_level->PowerThrottle = 0;

	if (pi->caps_sclk_ds)
		graphic_level->DeepSleepDivId = ci_get_sleep_divider_id_from_clock(rdev,
										   engine_clock,
										   CISLAND_MINIMUM_ENGINE_CLOCK);

	graphic_level->DisplayWatermark = PPSMC_DISPLAY_WATERMARK_LOW;

	graphic_level->Flags = cpu_to_be32(graphic_level->Flags);
	graphic_level->MinVddc = cpu_to_be32(graphic_level->MinVddc * VOLTAGE_SCALE);
	graphic_level->MinVddcPhases = cpu_to_be32(graphic_level->MinVddcPhases);
	graphic_level->SclkFrequency = cpu_to_be32(graphic_level->SclkFrequency);
	graphic_level->ActivityLevel = cpu_to_be16(graphic_level->ActivityLevel);
	graphic_level->CgSpllFuncCntl3 = cpu_to_be32(graphic_level->CgSpllFuncCntl3);
	graphic_level->CgSpllFuncCntl4 = cpu_to_be32(graphic_level->CgSpllFuncCntl4);
	graphic_level->SpllSpreadSpectrum = cpu_to_be32(graphic_level->SpllSpreadSpectrum);
	graphic_level->SpllSpreadSpectrum2 = cpu_to_be32(graphic_level->SpllSpreadSpectrum2);
	graphic_level->CcPwrDynRm = cpu_to_be32(graphic_level->CcPwrDynRm);
	graphic_level->CcPwrDynRm1 = cpu_to_be32(graphic_level->CcPwrDynRm1);

	return 0;
}

static int ci_populate_all_graphic_levels(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct ci_dpm_table *dpm_table = &pi->dpm_table;
	u32 level_array_address = pi->dpm_table_start +
		offsetof(SMU7_Discrete_DpmTable, GraphicsLevel);
	u32 level_array_size = sizeof(SMU7_Discrete_GraphicsLevel) *
		SMU7_MAX_LEVELS_GRAPHICS;
	SMU7_Discrete_GraphicsLevel *levels = pi->smc_state_table.GraphicsLevel;
	u32 i, ret;

	memset(levels, 0, level_array_size);

	for (i = 0; i < dpm_table->sclk_table.count; i++) {
		ret = ci_populate_single_graphic_level(rdev,
						       dpm_table->sclk_table.dpm_levels[i].value,
						       (u16)pi->activity_target[i],
						       &pi->smc_state_table.GraphicsLevel[i]);
		if (ret)
			return ret;
		if (i > 1)
			pi->smc_state_table.GraphicsLevel[i].DeepSleepDivId = 0;
		if (i == (dpm_table->sclk_table.count - 1))
			pi->smc_state_table.GraphicsLevel[i].DisplayWatermark =
				PPSMC_DISPLAY_WATERMARK_HIGH;
	}
	pi->smc_state_table.GraphicsLevel[0].EnabledForActivity = 1;

	pi->smc_state_table.GraphicsDpmLevelCount = (u8)dpm_table->sclk_table.count;
	pi->dpm_level_enable_mask.sclk_dpm_enable_mask =
		ci_get_dpm_level_enable_mask_value(&dpm_table->sclk_table);

	ret = ci_copy_bytes_to_smc(rdev, level_array_address,
				   (u8 *)levels, level_array_size,
				   pi->sram_end);
	if (ret)
		return ret;

	return 0;
}

static int ci_populate_ulv_state(struct radeon_device *rdev,
				 SMU7_Discrete_Ulv *ulv_level)
{
	return ci_populate_ulv_level(rdev, ulv_level);
}

static int ci_populate_all_memory_levels(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct ci_dpm_table *dpm_table = &pi->dpm_table;
	u32 level_array_address = pi->dpm_table_start +
		offsetof(SMU7_Discrete_DpmTable, MemoryLevel);
	u32 level_array_size = sizeof(SMU7_Discrete_MemoryLevel) *
		SMU7_MAX_LEVELS_MEMORY;
	SMU7_Discrete_MemoryLevel *levels = pi->smc_state_table.MemoryLevel;
	u32 i, ret;

	memset(levels, 0, level_array_size);

	for (i = 0; i < dpm_table->mclk_table.count; i++) {
		if (dpm_table->mclk_table.dpm_levels[i].value == 0)
			return -EINVAL;
		ret = ci_populate_single_memory_level(rdev,
						      dpm_table->mclk_table.dpm_levels[i].value,
						      &pi->smc_state_table.MemoryLevel[i]);
		if (ret)
			return ret;
	}

	pi->smc_state_table.MemoryLevel[0].EnabledForActivity = 1;

	if ((dpm_table->mclk_table.count >= 2) &&
	    ((rdev->pdev->device == 0x67B0) || (rdev->pdev->device == 0x67B1))) {
		pi->smc_state_table.MemoryLevel[1].MinVddc =
			pi->smc_state_table.MemoryLevel[0].MinVddc;
		pi->smc_state_table.MemoryLevel[1].MinVddcPhases =
			pi->smc_state_table.MemoryLevel[0].MinVddcPhases;
	}

	pi->smc_state_table.MemoryLevel[0].ActivityLevel = cpu_to_be16(0x1F);

	pi->smc_state_table.MemoryDpmLevelCount = (u8)dpm_table->mclk_table.count;
	pi->dpm_level_enable_mask.mclk_dpm_enable_mask =
		ci_get_dpm_level_enable_mask_value(&dpm_table->mclk_table);

	pi->smc_state_table.MemoryLevel[dpm_table->mclk_table.count - 1].DisplayWatermark =
		PPSMC_DISPLAY_WATERMARK_HIGH;

	ret = ci_copy_bytes_to_smc(rdev, level_array_address,
				   (u8 *)levels, level_array_size,
				   pi->sram_end);
	if (ret)
		return ret;

	return 0;
}

static void ci_reset_single_dpm_table(struct radeon_device *rdev,
				      struct ci_single_dpm_table* dpm_table,
				      u32 count)
{
	u32 i;

	dpm_table->count = count;
	for (i = 0; i < MAX_REGULAR_DPM_NUMBER; i++)
		dpm_table->dpm_levels[i].enabled = false;
}

static void ci_setup_pcie_table_entry(struct ci_single_dpm_table* dpm_table,
				      u32 index, u32 pcie_gen, u32 pcie_lanes)
{
	dpm_table->dpm_levels[index].value = pcie_gen;
	dpm_table->dpm_levels[index].param1 = pcie_lanes;
	dpm_table->dpm_levels[index].enabled = true;
}

static int ci_setup_default_pcie_tables(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);

	if (!pi->use_pcie_performance_levels && !pi->use_pcie_powersaving_levels)
		return -EINVAL;

	if (pi->use_pcie_performance_levels && !pi->use_pcie_powersaving_levels) {
		pi->pcie_gen_powersaving = pi->pcie_gen_performance;
		pi->pcie_lane_powersaving = pi->pcie_lane_performance;
	} else if (!pi->use_pcie_performance_levels && pi->use_pcie_powersaving_levels) {
		pi->pcie_gen_performance = pi->pcie_gen_powersaving;
		pi->pcie_lane_performance = pi->pcie_lane_powersaving;
	}

	ci_reset_single_dpm_table(rdev,
				  &pi->dpm_table.pcie_speed_table,
				  SMU7_MAX_LEVELS_LINK);

	if (rdev->family == CHIP_BONAIRE)
		ci_setup_pcie_table_entry(&pi->dpm_table.pcie_speed_table, 0,
					  pi->pcie_gen_powersaving.min,
					  pi->pcie_lane_powersaving.max);
	else
		ci_setup_pcie_table_entry(&pi->dpm_table.pcie_speed_table, 0,
					  pi->pcie_gen_powersaving.min,
					  pi->pcie_lane_powersaving.min);
	ci_setup_pcie_table_entry(&pi->dpm_table.pcie_speed_table, 1,
				  pi->pcie_gen_performance.min,
				  pi->pcie_lane_performance.min);
	ci_setup_pcie_table_entry(&pi->dpm_table.pcie_speed_table, 2,
				  pi->pcie_gen_powersaving.min,
				  pi->pcie_lane_powersaving.max);
	ci_setup_pcie_table_entry(&pi->dpm_table.pcie_speed_table, 3,
				  pi->pcie_gen_performance.min,
				  pi->pcie_lane_performance.max);
	ci_setup_pcie_table_entry(&pi->dpm_table.pcie_speed_table, 4,
				  pi->pcie_gen_powersaving.max,
				  pi->pcie_lane_powersaving.max);
	ci_setup_pcie_table_entry(&pi->dpm_table.pcie_speed_table, 5,
				  pi->pcie_gen_performance.max,
				  pi->pcie_lane_performance.max);

	pi->dpm_table.pcie_speed_table.count = 6;

	return 0;
}

static int ci_setup_default_dpm_tables(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct radeon_clock_voltage_dependency_table *allowed_sclk_vddc_table =
		&rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk;
	struct radeon_clock_voltage_dependency_table *allowed_mclk_table =
		&rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk;
	struct radeon_cac_leakage_table *std_voltage_table =
		&rdev->pm.dpm.dyn_state.cac_leakage_table;
	u32 i;

	if (allowed_sclk_vddc_table == NULL)
		return -EINVAL;
	if (allowed_sclk_vddc_table->count < 1)
		return -EINVAL;
	if (allowed_mclk_table == NULL)
		return -EINVAL;
	if (allowed_mclk_table->count < 1)
		return -EINVAL;

	memset(&pi->dpm_table, 0, sizeof(struct ci_dpm_table));

	ci_reset_single_dpm_table(rdev,
				  &pi->dpm_table.sclk_table,
				  SMU7_MAX_LEVELS_GRAPHICS);
	ci_reset_single_dpm_table(rdev,
				  &pi->dpm_table.mclk_table,
				  SMU7_MAX_LEVELS_MEMORY);
	ci_reset_single_dpm_table(rdev,
				  &pi->dpm_table.vddc_table,
				  SMU7_MAX_LEVELS_VDDC);
	ci_reset_single_dpm_table(rdev,
				  &pi->dpm_table.vddci_table,
				  SMU7_MAX_LEVELS_VDDCI);
	ci_reset_single_dpm_table(rdev,
				  &pi->dpm_table.mvdd_table,
				  SMU7_MAX_LEVELS_MVDD);

	pi->dpm_table.sclk_table.count = 0;
	for (i = 0; i < allowed_sclk_vddc_table->count; i++) {
		if ((i == 0) ||
		    (pi->dpm_table.sclk_table.dpm_levels[pi->dpm_table.sclk_table.count-1].value !=
		     allowed_sclk_vddc_table->entries[i].clk)) {
			pi->dpm_table.sclk_table.dpm_levels[pi->dpm_table.sclk_table.count].value =
				allowed_sclk_vddc_table->entries[i].clk;
			pi->dpm_table.sclk_table.dpm_levels[pi->dpm_table.sclk_table.count].enabled =
				(i == 0) ? true : false;
			pi->dpm_table.sclk_table.count++;
		}
	}

	pi->dpm_table.mclk_table.count = 0;
	for (i = 0; i < allowed_mclk_table->count; i++) {
		if ((i == 0) ||
		    (pi->dpm_table.mclk_table.dpm_levels[pi->dpm_table.mclk_table.count-1].value !=
		     allowed_mclk_table->entries[i].clk)) {
			pi->dpm_table.mclk_table.dpm_levels[pi->dpm_table.mclk_table.count].value =
				allowed_mclk_table->entries[i].clk;
			pi->dpm_table.mclk_table.dpm_levels[pi->dpm_table.mclk_table.count].enabled =
				(i == 0) ? true : false;
			pi->dpm_table.mclk_table.count++;
		}
	}

	for (i = 0; i < allowed_sclk_vddc_table->count; i++) {
		pi->dpm_table.vddc_table.dpm_levels[i].value =
			allowed_sclk_vddc_table->entries[i].v;
		pi->dpm_table.vddc_table.dpm_levels[i].param1 =
			std_voltage_table->entries[i].leakage;
		pi->dpm_table.vddc_table.dpm_levels[i].enabled = true;
	}
	pi->dpm_table.vddc_table.count = allowed_sclk_vddc_table->count;

	allowed_mclk_table = &rdev->pm.dpm.dyn_state.vddci_dependency_on_mclk;
	if (allowed_mclk_table) {
		for (i = 0; i < allowed_mclk_table->count; i++) {
			pi->dpm_table.vddci_table.dpm_levels[i].value =
				allowed_mclk_table->entries[i].v;
			pi->dpm_table.vddci_table.dpm_levels[i].enabled = true;
		}
		pi->dpm_table.vddci_table.count = allowed_mclk_table->count;
	}

	allowed_mclk_table = &rdev->pm.dpm.dyn_state.mvdd_dependency_on_mclk;
	if (allowed_mclk_table) {
		for (i = 0; i < allowed_mclk_table->count; i++) {
			pi->dpm_table.mvdd_table.dpm_levels[i].value =
				allowed_mclk_table->entries[i].v;
			pi->dpm_table.mvdd_table.dpm_levels[i].enabled = true;
		}
		pi->dpm_table.mvdd_table.count = allowed_mclk_table->count;
	}

	ci_setup_default_pcie_tables(rdev);

	return 0;
}

static int ci_find_boot_level(struct ci_single_dpm_table *table,
			      u32 value, u32 *boot_level)
{
	u32 i;
	int ret = -EINVAL;

	for(i = 0; i < table->count; i++) {
		if (value == table->dpm_levels[i].value) {
			*boot_level = i;
			ret = 0;
		}
	}

	return ret;
}

static int ci_init_smc_table(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct ci_ulv_parm *ulv = &pi->ulv;
	struct radeon_ps *radeon_boot_state = rdev->pm.dpm.boot_ps;
	SMU7_Discrete_DpmTable *table = &pi->smc_state_table;
	int ret;

	ret = ci_setup_default_dpm_tables(rdev);
	if (ret)
		return ret;

	if (pi->voltage_control != CISLANDS_VOLTAGE_CONTROL_NONE)
		ci_populate_smc_voltage_tables(rdev, table);

	ci_init_fps_limits(rdev);

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_HARDWAREDC)
		table->SystemFlags |= PPSMC_SYSTEMFLAG_GPIO_DC;

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_STEPVDDC)
		table->SystemFlags |= PPSMC_SYSTEMFLAG_STEPVDDC;

	if (pi->mem_gddr5)
		table->SystemFlags |= PPSMC_SYSTEMFLAG_GDDR5;

	if (ulv->supported) {
		ret = ci_populate_ulv_state(rdev, &pi->smc_state_table.Ulv);
		if (ret)
			return ret;
		WREG32_SMC(CG_ULV_PARAMETER, ulv->cg_ulv_parameter);
	}

	ret = ci_populate_all_graphic_levels(rdev);
	if (ret)
		return ret;

	ret = ci_populate_all_memory_levels(rdev);
	if (ret)
		return ret;

	ci_populate_smc_link_level(rdev, table);

	ret = ci_populate_smc_acpi_level(rdev, table);
	if (ret)
		return ret;

	ret = ci_populate_smc_vce_level(rdev, table);
	if (ret)
		return ret;

	ret = ci_populate_smc_acp_level(rdev, table);
	if (ret)
		return ret;

	ret = ci_populate_smc_samu_level(rdev, table);
	if (ret)
		return ret;

	ret = ci_do_program_memory_timing_parameters(rdev);
	if (ret)
		return ret;

	ret = ci_populate_smc_uvd_level(rdev, table);
	if (ret)
		return ret;

	table->UvdBootLevel  = 0;
	table->VceBootLevel  = 0;
	table->AcpBootLevel  = 0;
	table->SamuBootLevel  = 0;
	table->GraphicsBootLevel  = 0;
	table->MemoryBootLevel  = 0;

	ret = ci_find_boot_level(&pi->dpm_table.sclk_table,
				 pi->vbios_boot_state.sclk_bootup_value,
				 (u32 *)&pi->smc_state_table.GraphicsBootLevel);

	ret = ci_find_boot_level(&pi->dpm_table.mclk_table,
				 pi->vbios_boot_state.mclk_bootup_value,
				 (u32 *)&pi->smc_state_table.MemoryBootLevel);

	table->BootVddc = pi->vbios_boot_state.vddc_bootup_value;
	table->BootVddci = pi->vbios_boot_state.vddci_bootup_value;
	table->BootMVdd = pi->vbios_boot_state.mvdd_bootup_value;

	ci_populate_smc_initial_state(rdev, radeon_boot_state);

	ret = ci_populate_bapm_parameters_in_dpm_table(rdev);
	if (ret)
		return ret;

	table->UVDInterval = 1;
	table->VCEInterval = 1;
	table->ACPInterval = 1;
	table->SAMUInterval = 1;
	table->GraphicsVoltageChangeEnable = 1;
	table->GraphicsThermThrottleEnable = 1;
	table->GraphicsInterval = 1;
	table->VoltageInterval = 1;
	table->ThermalInterval = 1;
	table->TemperatureLimitHigh = (u16)((pi->thermal_temp_setting.temperature_high *
					     CISLANDS_Q88_FORMAT_CONVERSION_UNIT) / 1000);
	table->TemperatureLimitLow = (u16)((pi->thermal_temp_setting.temperature_low *
					    CISLANDS_Q88_FORMAT_CONVERSION_UNIT) / 1000);
	table->MemoryVoltageChangeEnable = 1;
	table->MemoryInterval = 1;
	table->VoltageResponseTime = 0;
	table->VddcVddciDelta = 4000;
	table->PhaseResponseTime = 0;
	table->MemoryThermThrottleEnable = 1;
	table->PCIeBootLinkLevel = pi->dpm_table.pcie_speed_table.count - 1;
	table->PCIeGenInterval = 1;
	if (pi->voltage_control == CISLANDS_VOLTAGE_CONTROL_BY_SVID2)
		table->SVI2Enable  = 1;
	else
		table->SVI2Enable  = 0;

	table->ThermGpio = 17;
	table->SclkStepSize = 0x4000;

	table->SystemFlags = cpu_to_be32(table->SystemFlags);
	table->SmioMaskVddcVid = cpu_to_be32(table->SmioMaskVddcVid);
	table->SmioMaskVddcPhase = cpu_to_be32(table->SmioMaskVddcPhase);
	table->SmioMaskVddciVid = cpu_to_be32(table->SmioMaskVddciVid);
	table->SmioMaskMvddVid = cpu_to_be32(table->SmioMaskMvddVid);
	table->SclkStepSize = cpu_to_be32(table->SclkStepSize);
	table->TemperatureLimitHigh = cpu_to_be16(table->TemperatureLimitHigh);
	table->TemperatureLimitLow = cpu_to_be16(table->TemperatureLimitLow);
	table->VddcVddciDelta = cpu_to_be16(table->VddcVddciDelta);
	table->VoltageResponseTime = cpu_to_be16(table->VoltageResponseTime);
	table->PhaseResponseTime = cpu_to_be16(table->PhaseResponseTime);
	table->BootVddc = cpu_to_be16(table->BootVddc * VOLTAGE_SCALE);
	table->BootVddci = cpu_to_be16(table->BootVddci * VOLTAGE_SCALE);
	table->BootMVdd = cpu_to_be16(table->BootMVdd * VOLTAGE_SCALE);

	ret = ci_copy_bytes_to_smc(rdev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Discrete_DpmTable, SystemFlags),
				   (u8 *)&table->SystemFlags,
				   sizeof(SMU7_Discrete_DpmTable) - 3 * sizeof(SMU7_PIDController),
				   pi->sram_end);
	if (ret)
		return ret;

	return 0;
}

static void ci_trim_single_dpm_states(struct radeon_device *rdev,
				      struct ci_single_dpm_table *dpm_table,
				      u32 low_limit, u32 high_limit)
{
	u32 i;

	for (i = 0; i < dpm_table->count; i++) {
		if ((dpm_table->dpm_levels[i].value < low_limit) ||
		    (dpm_table->dpm_levels[i].value > high_limit))
			dpm_table->dpm_levels[i].enabled = false;
		else
			dpm_table->dpm_levels[i].enabled = true;
	}
}

static void ci_trim_pcie_dpm_states(struct radeon_device *rdev,
				    u32 speed_low, u32 lanes_low,
				    u32 speed_high, u32 lanes_high)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct ci_single_dpm_table *pcie_table = &pi->dpm_table.pcie_speed_table;
	u32 i, j;

	for (i = 0; i < pcie_table->count; i++) {
		if ((pcie_table->dpm_levels[i].value < speed_low) ||
		    (pcie_table->dpm_levels[i].param1 < lanes_low) ||
		    (pcie_table->dpm_levels[i].value > speed_high) ||
		    (pcie_table->dpm_levels[i].param1 > lanes_high))
			pcie_table->dpm_levels[i].enabled = false;
		else
			pcie_table->dpm_levels[i].enabled = true;
	}

	for (i = 0; i < pcie_table->count; i++) {
		if (pcie_table->dpm_levels[i].enabled) {
			for (j = i + 1; j < pcie_table->count; j++) {
				if (pcie_table->dpm_levels[j].enabled) {
					if ((pcie_table->dpm_levels[i].value == pcie_table->dpm_levels[j].value) &&
					    (pcie_table->dpm_levels[i].param1 == pcie_table->dpm_levels[j].param1))
						pcie_table->dpm_levels[j].enabled = false;
				}
			}
		}
	}
}

static int ci_trim_dpm_states(struct radeon_device *rdev,
			      struct radeon_ps *radeon_state)
{
	struct ci_ps *state = ci_get_ps(radeon_state);
	struct ci_power_info *pi = ci_get_pi(rdev);
	u32 high_limit_count;

	if (state->performance_level_count < 1)
		return -EINVAL;

	if (state->performance_level_count == 1)
		high_limit_count = 0;
	else
		high_limit_count = 1;

	ci_trim_single_dpm_states(rdev,
				  &pi->dpm_table.sclk_table,
				  state->performance_levels[0].sclk,
				  state->performance_levels[high_limit_count].sclk);

	ci_trim_single_dpm_states(rdev,
				  &pi->dpm_table.mclk_table,
				  state->performance_levels[0].mclk,
				  state->performance_levels[high_limit_count].mclk);

	ci_trim_pcie_dpm_states(rdev,
				state->performance_levels[0].pcie_gen,
				state->performance_levels[0].pcie_lane,
				state->performance_levels[high_limit_count].pcie_gen,
				state->performance_levels[high_limit_count].pcie_lane);

	return 0;
}

static int ci_apply_disp_minimum_voltage_request(struct radeon_device *rdev)
{
	struct radeon_clock_voltage_dependency_table *disp_voltage_table =
		&rdev->pm.dpm.dyn_state.vddc_dependency_on_dispclk;
	struct radeon_clock_voltage_dependency_table *vddc_table =
		&rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk;
	u32 requested_voltage = 0;
	u32 i;

	if (disp_voltage_table == NULL)
		return -EINVAL;
	if (!disp_voltage_table->count)
		return -EINVAL;

	for (i = 0; i < disp_voltage_table->count; i++) {
		if (rdev->clock.current_dispclk == disp_voltage_table->entries[i].clk)
			requested_voltage = disp_voltage_table->entries[i].v;
	}

	for (i = 0; i < vddc_table->count; i++) {
		if (requested_voltage <= vddc_table->entries[i].v) {
			requested_voltage = vddc_table->entries[i].v;
			return (ci_send_msg_to_smc_with_parameter(rdev,
								  PPSMC_MSG_VddC_Request,
								  requested_voltage * VOLTAGE_SCALE) == PPSMC_Result_OK) ?
				0 : -EINVAL;
		}
	}

	return -EINVAL;
}

static int ci_upload_dpm_level_enable_mask(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	PPSMC_Result result;

	ci_apply_disp_minimum_voltage_request(rdev);

	if (!pi->sclk_dpm_key_disabled) {
		if (pi->dpm_level_enable_mask.sclk_dpm_enable_mask) {
			result = ci_send_msg_to_smc_with_parameter(rdev,
								   PPSMC_MSG_SCLKDPM_SetEnabledMask,
								   pi->dpm_level_enable_mask.sclk_dpm_enable_mask);
			if (result != PPSMC_Result_OK)
				return -EINVAL;
		}
	}

	if (!pi->mclk_dpm_key_disabled) {
		if (pi->dpm_level_enable_mask.mclk_dpm_enable_mask) {
			result = ci_send_msg_to_smc_with_parameter(rdev,
								   PPSMC_MSG_MCLKDPM_SetEnabledMask,
								   pi->dpm_level_enable_mask.mclk_dpm_enable_mask);
			if (result != PPSMC_Result_OK)
				return -EINVAL;
		}
	}
#if 0
	if (!pi->pcie_dpm_key_disabled) {
		if (pi->dpm_level_enable_mask.pcie_dpm_enable_mask) {
			result = ci_send_msg_to_smc_with_parameter(rdev,
								   PPSMC_MSG_PCIeDPM_SetEnabledMask,
								   pi->dpm_level_enable_mask.pcie_dpm_enable_mask);
			if (result != PPSMC_Result_OK)
				return -EINVAL;
		}
	}
#endif
	return 0;
}

static void ci_find_dpm_states_clocks_in_dpm_table(struct radeon_device *rdev,
						   struct radeon_ps *radeon_state)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct ci_ps *state = ci_get_ps(radeon_state);
	struct ci_single_dpm_table *sclk_table = &pi->dpm_table.sclk_table;
	u32 sclk = state->performance_levels[state->performance_level_count-1].sclk;
	struct ci_single_dpm_table *mclk_table = &pi->dpm_table.mclk_table;
	u32 mclk = state->performance_levels[state->performance_level_count-1].mclk;
	u32 i;

	pi->need_update_smu7_dpm_table = 0;

	for (i = 0; i < sclk_table->count; i++) {
		if (sclk == sclk_table->dpm_levels[i].value)
			break;
	}

	if (i >= sclk_table->count) {
		pi->need_update_smu7_dpm_table |= DPMTABLE_OD_UPDATE_SCLK;
	} else {
		/* XXX The current code always reprogrammed the sclk levels,
		 * but we don't currently handle disp sclk requirements
		 * so just skip it.
		 */
		if (CISLAND_MINIMUM_ENGINE_CLOCK != CISLAND_MINIMUM_ENGINE_CLOCK)
			pi->need_update_smu7_dpm_table |= DPMTABLE_UPDATE_SCLK;
	}

	for (i = 0; i < mclk_table->count; i++) {
		if (mclk == mclk_table->dpm_levels[i].value)
			break;
	}

	if (i >= mclk_table->count)
		pi->need_update_smu7_dpm_table |= DPMTABLE_OD_UPDATE_MCLK;

	if (rdev->pm.dpm.current_active_crtc_count !=
	    rdev->pm.dpm.new_active_crtc_count)
		pi->need_update_smu7_dpm_table |= DPMTABLE_UPDATE_MCLK;
}

static int ci_populate_and_upload_sclk_mclk_dpm_levels(struct radeon_device *rdev,
						       struct radeon_ps *radeon_state)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct ci_ps *state = ci_get_ps(radeon_state);
	u32 sclk = state->performance_levels[state->performance_level_count-1].sclk;
	u32 mclk = state->performance_levels[state->performance_level_count-1].mclk;
	struct ci_dpm_table *dpm_table = &pi->dpm_table;
	int ret;

	if (!pi->need_update_smu7_dpm_table)
		return 0;

	if (pi->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_SCLK)
		dpm_table->sclk_table.dpm_levels[dpm_table->sclk_table.count-1].value = sclk;

	if (pi->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_MCLK)
		dpm_table->mclk_table.dpm_levels[dpm_table->mclk_table.count-1].value = mclk;

	if (pi->need_update_smu7_dpm_table & (DPMTABLE_OD_UPDATE_SCLK | DPMTABLE_UPDATE_SCLK)) {
		ret = ci_populate_all_graphic_levels(rdev);
		if (ret)
			return ret;
	}

	if (pi->need_update_smu7_dpm_table & (DPMTABLE_OD_UPDATE_MCLK | DPMTABLE_UPDATE_MCLK)) {
		ret = ci_populate_all_memory_levels(rdev);
		if (ret)
			return ret;
	}

	return 0;
}

static int ci_enable_uvd_dpm(struct radeon_device *rdev, bool enable)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	const struct radeon_clock_and_voltage_limits *max_limits;
	int i;

	if (rdev->pm.dpm.ac_power)
		max_limits = &rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac;
	else
		max_limits = &rdev->pm.dpm.dyn_state.max_clock_voltage_on_dc;

	if (enable) {
		pi->dpm_level_enable_mask.uvd_dpm_enable_mask = 0;

		for (i = rdev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.count - 1; i >= 0; i--) {
			if (rdev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.entries[i].v <= max_limits->vddc) {
				pi->dpm_level_enable_mask.uvd_dpm_enable_mask |= 1 << i;

				if (!pi->caps_uvd_dpm)
					break;
			}
		}

		ci_send_msg_to_smc_with_parameter(rdev,
						  PPSMC_MSG_UVDDPM_SetEnabledMask,
						  pi->dpm_level_enable_mask.uvd_dpm_enable_mask);

		if (pi->last_mclk_dpm_enable_mask & 0x1) {
			pi->uvd_enabled = true;
			pi->dpm_level_enable_mask.mclk_dpm_enable_mask &= 0xFFFFFFFE;
			ci_send_msg_to_smc_with_parameter(rdev,
							  PPSMC_MSG_MCLKDPM_SetEnabledMask,
							  pi->dpm_level_enable_mask.mclk_dpm_enable_mask);
		}
	} else {
		if (pi->last_mclk_dpm_enable_mask & 0x1) {
			pi->uvd_enabled = false;
			pi->dpm_level_enable_mask.mclk_dpm_enable_mask |= 1;
			ci_send_msg_to_smc_with_parameter(rdev,
							  PPSMC_MSG_MCLKDPM_SetEnabledMask,
							  pi->dpm_level_enable_mask.mclk_dpm_enable_mask);
		}
	}

	return (ci_send_msg_to_smc(rdev, enable ?
				   PPSMC_MSG_UVDDPM_Enable : PPSMC_MSG_UVDDPM_Disable) == PPSMC_Result_OK) ?
		0 : -EINVAL;
}

static int ci_enable_vce_dpm(struct radeon_device *rdev, bool enable)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	const struct radeon_clock_and_voltage_limits *max_limits;
	int i;

	if (rdev->pm.dpm.ac_power)
		max_limits = &rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac;
	else
		max_limits = &rdev->pm.dpm.dyn_state.max_clock_voltage_on_dc;

	if (enable) {
		pi->dpm_level_enable_mask.vce_dpm_enable_mask = 0;
		for (i = rdev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.count - 1; i >= 0; i--) {
			if (rdev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.entries[i].v <= max_limits->vddc) {
				pi->dpm_level_enable_mask.vce_dpm_enable_mask |= 1 << i;

				if (!pi->caps_vce_dpm)
					break;
			}
		}

		ci_send_msg_to_smc_with_parameter(rdev,
						  PPSMC_MSG_VCEDPM_SetEnabledMask,
						  pi->dpm_level_enable_mask.vce_dpm_enable_mask);
	}

	return (ci_send_msg_to_smc(rdev, enable ?
				   PPSMC_MSG_VCEDPM_Enable : PPSMC_MSG_VCEDPM_Disable) == PPSMC_Result_OK) ?
		0 : -EINVAL;
}

#if 0
static int ci_enable_samu_dpm(struct radeon_device *rdev, bool enable)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	const struct radeon_clock_and_voltage_limits *max_limits;
	int i;

	if (rdev->pm.dpm.ac_power)
		max_limits = &rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac;
	else
		max_limits = &rdev->pm.dpm.dyn_state.max_clock_voltage_on_dc;

	if (enable) {
		pi->dpm_level_enable_mask.samu_dpm_enable_mask = 0;
		for (i = rdev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table.count - 1; i >= 0; i--) {
			if (rdev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table.entries[i].v <= max_limits->vddc) {
				pi->dpm_level_enable_mask.samu_dpm_enable_mask |= 1 << i;

				if (!pi->caps_samu_dpm)
					break;
			}
		}

		ci_send_msg_to_smc_with_parameter(rdev,
						  PPSMC_MSG_SAMUDPM_SetEnabledMask,
						  pi->dpm_level_enable_mask.samu_dpm_enable_mask);
	}
	return (ci_send_msg_to_smc(rdev, enable ?
				   PPSMC_MSG_SAMUDPM_Enable : PPSMC_MSG_SAMUDPM_Disable) == PPSMC_Result_OK) ?
		0 : -EINVAL;
}

static int ci_enable_acp_dpm(struct radeon_device *rdev, bool enable)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	const struct radeon_clock_and_voltage_limits *max_limits;
	int i;

	if (rdev->pm.dpm.ac_power)
		max_limits = &rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac;
	else
		max_limits = &rdev->pm.dpm.dyn_state.max_clock_voltage_on_dc;

	if (enable) {
		pi->dpm_level_enable_mask.acp_dpm_enable_mask = 0;
		for (i = rdev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table.count - 1; i >= 0; i--) {
			if (rdev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table.entries[i].v <= max_limits->vddc) {
				pi->dpm_level_enable_mask.acp_dpm_enable_mask |= 1 << i;

				if (!pi->caps_acp_dpm)
					break;
			}
		}

		ci_send_msg_to_smc_with_parameter(rdev,
						  PPSMC_MSG_ACPDPM_SetEnabledMask,
						  pi->dpm_level_enable_mask.acp_dpm_enable_mask);
	}

	return (ci_send_msg_to_smc(rdev, enable ?
				   PPSMC_MSG_ACPDPM_Enable : PPSMC_MSG_ACPDPM_Disable) == PPSMC_Result_OK) ?
		0 : -EINVAL;
}
#endif

static int ci_update_uvd_dpm(struct radeon_device *rdev, bool gate)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u32 tmp;

	if (!gate) {
		if (pi->caps_uvd_dpm ||
		    (rdev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.count <= 0))
			pi->smc_state_table.UvdBootLevel = 0;
		else
			pi->smc_state_table.UvdBootLevel =
				rdev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.count - 1;

		tmp = RREG32_SMC(DPM_TABLE_475);
		tmp &= ~UvdBootLevel_MASK;
		tmp |= UvdBootLevel(pi->smc_state_table.UvdBootLevel);
		WREG32_SMC(DPM_TABLE_475, tmp);
	}

	return ci_enable_uvd_dpm(rdev, !gate);
}

static u8 ci_get_vce_boot_level(struct radeon_device *rdev)
{
	u8 i;
	u32 min_evclk = 30000; /* ??? */
	struct radeon_vce_clock_voltage_dependency_table *table =
		&rdev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table;

	for (i = 0; i < table->count; i++) {
		if (table->entries[i].evclk >= min_evclk)
			return i;
	}

	return table->count - 1;
}

static int ci_update_vce_dpm(struct radeon_device *rdev,
			     struct radeon_ps *radeon_new_state,
			     struct radeon_ps *radeon_current_state)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	int ret = 0;
	u32 tmp;

	if (radeon_current_state->evclk != radeon_new_state->evclk) {
		if (radeon_new_state->evclk) {
			/* turn the clocks on when encoding */
			cik_update_cg(rdev, RADEON_CG_BLOCK_VCE, false);

			pi->smc_state_table.VceBootLevel = ci_get_vce_boot_level(rdev);
			tmp = RREG32_SMC(DPM_TABLE_475);
			tmp &= ~VceBootLevel_MASK;
			tmp |= VceBootLevel(pi->smc_state_table.VceBootLevel);
			WREG32_SMC(DPM_TABLE_475, tmp);

			ret = ci_enable_vce_dpm(rdev, true);
		} else {
			/* turn the clocks off when not encoding */
			cik_update_cg(rdev, RADEON_CG_BLOCK_VCE, true);

			ret = ci_enable_vce_dpm(rdev, false);
		}
	}
	return ret;
}

#if 0
static int ci_update_samu_dpm(struct radeon_device *rdev, bool gate)
{
	return ci_enable_samu_dpm(rdev, gate);
}

static int ci_update_acp_dpm(struct radeon_device *rdev, bool gate)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u32 tmp;

	if (!gate) {
		pi->smc_state_table.AcpBootLevel = 0;

		tmp = RREG32_SMC(DPM_TABLE_475);
		tmp &= ~AcpBootLevel_MASK;
		tmp |= AcpBootLevel(pi->smc_state_table.AcpBootLevel);
		WREG32_SMC(DPM_TABLE_475, tmp);
	}

	return ci_enable_acp_dpm(rdev, !gate);
}
#endif

static int ci_generate_dpm_level_enable_mask(struct radeon_device *rdev,
					     struct radeon_ps *radeon_state)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	int ret;

	ret = ci_trim_dpm_states(rdev, radeon_state);
	if (ret)
		return ret;

	pi->dpm_level_enable_mask.sclk_dpm_enable_mask =
		ci_get_dpm_level_enable_mask_value(&pi->dpm_table.sclk_table);
	pi->dpm_level_enable_mask.mclk_dpm_enable_mask =
		ci_get_dpm_level_enable_mask_value(&pi->dpm_table.mclk_table);
	pi->last_mclk_dpm_enable_mask =
		pi->dpm_level_enable_mask.mclk_dpm_enable_mask;
	if (pi->uvd_enabled) {
		if (pi->dpm_level_enable_mask.mclk_dpm_enable_mask & 1)
			pi->dpm_level_enable_mask.mclk_dpm_enable_mask &= 0xFFFFFFFE;
	}
	pi->dpm_level_enable_mask.pcie_dpm_enable_mask =
		ci_get_dpm_level_enable_mask_value(&pi->dpm_table.pcie_speed_table);

	return 0;
}

static u32 ci_get_lowest_enabled_level(struct radeon_device *rdev,
				       u32 level_mask)
{
	u32 level = 0;

	while ((level_mask & (1 << level)) == 0)
		level++;

	return level;
}


int ci_dpm_force_performance_level(struct radeon_device *rdev,
				   enum radeon_dpm_forced_level level)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u32 tmp, levels, i;
	int ret;

	if (level == RADEON_DPM_FORCED_LEVEL_HIGH) {
		if ((!pi->pcie_dpm_key_disabled) &&
		    pi->dpm_level_enable_mask.pcie_dpm_enable_mask) {
			levels = 0;
			tmp = pi->dpm_level_enable_mask.pcie_dpm_enable_mask;
			while (tmp >>= 1)
				levels++;
			if (levels) {
				ret = ci_dpm_force_state_pcie(rdev, level);
				if (ret)
					return ret;
				for (i = 0; i < rdev->usec_timeout; i++) {
					tmp = (RREG32_SMC(TARGET_AND_CURRENT_PROFILE_INDEX_1) &
					       CURR_PCIE_INDEX_MASK) >> CURR_PCIE_INDEX_SHIFT;
					if (tmp == levels)
						break;
					udelay(1);
				}
			}
		}
		if ((!pi->sclk_dpm_key_disabled) &&
		    pi->dpm_level_enable_mask.sclk_dpm_enable_mask) {
			levels = 0;
			tmp = pi->dpm_level_enable_mask.sclk_dpm_enable_mask;
			while (tmp >>= 1)
				levels++;
			if (levels) {
				ret = ci_dpm_force_state_sclk(rdev, levels);
				if (ret)
					return ret;
				for (i = 0; i < rdev->usec_timeout; i++) {
					tmp = (RREG32_SMC(TARGET_AND_CURRENT_PROFILE_INDEX) &
					       CURR_SCLK_INDEX_MASK) >> CURR_SCLK_INDEX_SHIFT;
					if (tmp == levels)
						break;
					udelay(1);
				}
			}
		}
		if ((!pi->mclk_dpm_key_disabled) &&
		    pi->dpm_level_enable_mask.mclk_dpm_enable_mask) {
			levels = 0;
			tmp = pi->dpm_level_enable_mask.mclk_dpm_enable_mask;
			while (tmp >>= 1)
				levels++;
			if (levels) {
				ret = ci_dpm_force_state_mclk(rdev, levels);
				if (ret)
					return ret;
				for (i = 0; i < rdev->usec_timeout; i++) {
					tmp = (RREG32_SMC(TARGET_AND_CURRENT_PROFILE_INDEX) &
					       CURR_MCLK_INDEX_MASK) >> CURR_MCLK_INDEX_SHIFT;
					if (tmp == levels)
						break;
					udelay(1);
				}
			}
		}
	} else if (level == RADEON_DPM_FORCED_LEVEL_LOW) {
		if ((!pi->sclk_dpm_key_disabled) &&
		    pi->dpm_level_enable_mask.sclk_dpm_enable_mask) {
			levels = ci_get_lowest_enabled_level(rdev,
							     pi->dpm_level_enable_mask.sclk_dpm_enable_mask);
			ret = ci_dpm_force_state_sclk(rdev, levels);
			if (ret)
				return ret;
			for (i = 0; i < rdev->usec_timeout; i++) {
				tmp = (RREG32_SMC(TARGET_AND_CURRENT_PROFILE_INDEX) &
				       CURR_SCLK_INDEX_MASK) >> CURR_SCLK_INDEX_SHIFT;
				if (tmp == levels)
					break;
				udelay(1);
			}
		}
		if ((!pi->mclk_dpm_key_disabled) &&
		    pi->dpm_level_enable_mask.mclk_dpm_enable_mask) {
			levels = ci_get_lowest_enabled_level(rdev,
							     pi->dpm_level_enable_mask.mclk_dpm_enable_mask);
			ret = ci_dpm_force_state_mclk(rdev, levels);
			if (ret)
				return ret;
			for (i = 0; i < rdev->usec_timeout; i++) {
				tmp = (RREG32_SMC(TARGET_AND_CURRENT_PROFILE_INDEX) &
				       CURR_MCLK_INDEX_MASK) >> CURR_MCLK_INDEX_SHIFT;
				if (tmp == levels)
					break;
				udelay(1);
			}
		}
		if ((!pi->pcie_dpm_key_disabled) &&
		    pi->dpm_level_enable_mask.pcie_dpm_enable_mask) {
			levels = ci_get_lowest_enabled_level(rdev,
							     pi->dpm_level_enable_mask.pcie_dpm_enable_mask);
			ret = ci_dpm_force_state_pcie(rdev, levels);
			if (ret)
				return ret;
			for (i = 0; i < rdev->usec_timeout; i++) {
				tmp = (RREG32_SMC(TARGET_AND_CURRENT_PROFILE_INDEX_1) &
				       CURR_PCIE_INDEX_MASK) >> CURR_PCIE_INDEX_SHIFT;
				if (tmp == levels)
					break;
				udelay(1);
			}
		}
	} else if (level == RADEON_DPM_FORCED_LEVEL_AUTO) {
		if (!pi->pcie_dpm_key_disabled) {
			PPSMC_Result smc_result;

			smc_result = ci_send_msg_to_smc(rdev,
							PPSMC_MSG_PCIeDPM_UnForceLevel);
			if (smc_result != PPSMC_Result_OK)
				return -EINVAL;
		}
		ret = ci_upload_dpm_level_enable_mask(rdev);
		if (ret)
			return ret;
	}

	rdev->pm.dpm.forced_level = level;

	return 0;
}

static int ci_set_mc_special_registers(struct radeon_device *rdev,
				       struct ci_mc_reg_table *table)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u8 i, j, k;
	u32 temp_reg;

	for (i = 0, j = table->last; i < table->last; i++) {
		if (j >= SMU7_DISCRETE_MC_REGISTER_ARRAY_SIZE)
			return -EINVAL;
		switch(table->mc_reg_address[i].s1 << 2) {
		case MC_SEQ_MISC1:
			temp_reg = RREG32(MC_PMG_CMD_EMRS);
			table->mc_reg_address[j].s1 = MC_PMG_CMD_EMRS >> 2;
			table->mc_reg_address[j].s0 = MC_SEQ_PMG_CMD_EMRS_LP >> 2;
			for (k = 0; k < table->num_entries; k++) {
				table->mc_reg_table_entry[k].mc_data[j] =
					((temp_reg & 0xffff0000)) | ((table->mc_reg_table_entry[k].mc_data[i] & 0xffff0000) >> 16);
			}
			j++;
			if (j >= SMU7_DISCRETE_MC_REGISTER_ARRAY_SIZE)
				return -EINVAL;

			temp_reg = RREG32(MC_PMG_CMD_MRS);
			table->mc_reg_address[j].s1 = MC_PMG_CMD_MRS >> 2;
			table->mc_reg_address[j].s0 = MC_SEQ_PMG_CMD_MRS_LP >> 2;
			for (k = 0; k < table->num_entries; k++) {
				table->mc_reg_table_entry[k].mc_data[j] =
					(temp_reg & 0xffff0000) | (table->mc_reg_table_entry[k].mc_data[i] & 0x0000ffff);
				if (!pi->mem_gddr5)
					table->mc_reg_table_entry[k].mc_data[j] |= 0x100;
			}
			j++;
			if (j >= SMU7_DISCRETE_MC_REGISTER_ARRAY_SIZE)
				return -EINVAL;

			if (!pi->mem_gddr5) {
				table->mc_reg_address[j].s1 = MC_PMG_AUTO_CMD >> 2;
				table->mc_reg_address[j].s0 = MC_PMG_AUTO_CMD >> 2;
				for (k = 0; k < table->num_entries; k++) {
					table->mc_reg_table_entry[k].mc_data[j] =
						(table->mc_reg_table_entry[k].mc_data[i] & 0xffff0000) >> 16;
				}
				j++;
				if (j > SMU7_DISCRETE_MC_REGISTER_ARRAY_SIZE)
					return -EINVAL;
			}
			break;
		case MC_SEQ_RESERVE_M:
			temp_reg = RREG32(MC_PMG_CMD_MRS1);
			table->mc_reg_address[j].s1 = MC_PMG_CMD_MRS1 >> 2;
			table->mc_reg_address[j].s0 = MC_SEQ_PMG_CMD_MRS1_LP >> 2;
			for (k = 0; k < table->num_entries; k++) {
				table->mc_reg_table_entry[k].mc_data[j] =
					(temp_reg & 0xffff0000) | (table->mc_reg_table_entry[k].mc_data[i] & 0x0000ffff);
			}
			j++;
			if (j > SMU7_DISCRETE_MC_REGISTER_ARRAY_SIZE)
				return -EINVAL;
			break;
		default:
			break;
		}

	}

	table->last = j;

	return 0;
}

static bool ci_check_s0_mc_reg_index(u16 in_reg, u16 *out_reg)
{
	bool result = true;

	switch(in_reg) {
	case MC_SEQ_RAS_TIMING >> 2:
		*out_reg = MC_SEQ_RAS_TIMING_LP >> 2;
		break;
	case MC_SEQ_DLL_STBY >> 2:
		*out_reg = MC_SEQ_DLL_STBY_LP >> 2;
		break;
	case MC_SEQ_G5PDX_CMD0 >> 2:
		*out_reg = MC_SEQ_G5PDX_CMD0_LP >> 2;
		break;
	case MC_SEQ_G5PDX_CMD1 >> 2:
		*out_reg = MC_SEQ_G5PDX_CMD1_LP >> 2;
		break;
	case MC_SEQ_G5PDX_CTRL >> 2:
		*out_reg = MC_SEQ_G5PDX_CTRL_LP >> 2;
		break;
	case MC_SEQ_CAS_TIMING >> 2:
		*out_reg = MC_SEQ_CAS_TIMING_LP >> 2;
		break;
	case MC_SEQ_MISC_TIMING >> 2:
		*out_reg = MC_SEQ_MISC_TIMING_LP >> 2;
		break;
	case MC_SEQ_MISC_TIMING2 >> 2:
		*out_reg = MC_SEQ_MISC_TIMING2_LP >> 2;
		break;
	case MC_SEQ_PMG_DVS_CMD >> 2:
		*out_reg = MC_SEQ_PMG_DVS_CMD_LP >> 2;
		break;
	case MC_SEQ_PMG_DVS_CTL >> 2:
		*out_reg = MC_SEQ_PMG_DVS_CTL_LP >> 2;
		break;
	case MC_SEQ_RD_CTL_D0 >> 2:
		*out_reg = MC_SEQ_RD_CTL_D0_LP >> 2;
		break;
	case MC_SEQ_RD_CTL_D1 >> 2:
		*out_reg = MC_SEQ_RD_CTL_D1_LP >> 2;
		break;
	case MC_SEQ_WR_CTL_D0 >> 2:
		*out_reg = MC_SEQ_WR_CTL_D0_LP >> 2;
		break;
	case MC_SEQ_WR_CTL_D1 >> 2:
		*out_reg = MC_SEQ_WR_CTL_D1_LP >> 2;
		break;
	case MC_PMG_CMD_EMRS >> 2:
		*out_reg = MC_SEQ_PMG_CMD_EMRS_LP >> 2;
		break;
	case MC_PMG_CMD_MRS >> 2:
		*out_reg = MC_SEQ_PMG_CMD_MRS_LP >> 2;
		break;
	case MC_PMG_CMD_MRS1 >> 2:
		*out_reg = MC_SEQ_PMG_CMD_MRS1_LP >> 2;
		break;
	case MC_SEQ_PMG_TIMING >> 2:
		*out_reg = MC_SEQ_PMG_TIMING_LP >> 2;
		break;
	case MC_PMG_CMD_MRS2 >> 2:
		*out_reg = MC_SEQ_PMG_CMD_MRS2_LP >> 2;
		break;
	case MC_SEQ_WR_CTL_2 >> 2:
		*out_reg = MC_SEQ_WR_CTL_2_LP >> 2;
		break;
	default:
		result = false;
		break;
	}

	return result;
}

static void ci_set_valid_flag(struct ci_mc_reg_table *table)
{
	u8 i, j;

	for (i = 0; i < table->last; i++) {
		for (j = 1; j < table->num_entries; j++) {
			if (table->mc_reg_table_entry[j-1].mc_data[i] !=
			    table->mc_reg_table_entry[j].mc_data[i]) {
				table->valid_flag |= 1 << i;
				break;
			}
		}
	}
}

static void ci_set_s0_mc_reg_index(struct ci_mc_reg_table *table)
{
	u32 i;
	u16 address;

	for (i = 0; i < table->last; i++) {
		table->mc_reg_address[i].s0 =
			ci_check_s0_mc_reg_index(table->mc_reg_address[i].s1, &address) ?
			address : table->mc_reg_address[i].s1;
	}
}

static int ci_copy_vbios_mc_reg_table(const struct atom_mc_reg_table *table,
				      struct ci_mc_reg_table *ci_table)
{
	u8 i, j;

	if (table->last > SMU7_DISCRETE_MC_REGISTER_ARRAY_SIZE)
		return -EINVAL;
	if (table->num_entries > MAX_AC_TIMING_ENTRIES)
		return -EINVAL;

	for (i = 0; i < table->last; i++)
		ci_table->mc_reg_address[i].s1 = table->mc_reg_address[i].s1;

	ci_table->last = table->last;

	for (i = 0; i < table->num_entries; i++) {
		ci_table->mc_reg_table_entry[i].mclk_max =
			table->mc_reg_table_entry[i].mclk_max;
		for (j = 0; j < table->last; j++)
			ci_table->mc_reg_table_entry[i].mc_data[j] =
				table->mc_reg_table_entry[i].mc_data[j];
	}
	ci_table->num_entries = table->num_entries;

	return 0;
}

static int ci_register_patching_mc_seq(struct radeon_device *rdev,
				       struct ci_mc_reg_table *table)
{
	u8 i, k;
	u32 tmp;
	bool patch;

	tmp = RREG32(MC_SEQ_MISC0);
	patch = ((tmp & 0x0000f00) == 0x300) ? true : false;

	if (patch &&
	    ((rdev->pdev->device == 0x67B0) ||
	     (rdev->pdev->device == 0x67B1))) {
		for (i = 0; i < table->last; i++) {
			if (table->last >= SMU7_DISCRETE_MC_REGISTER_ARRAY_SIZE)
				return -EINVAL;
			switch(table->mc_reg_address[i].s1 >> 2) {
			case MC_SEQ_MISC1:
				for (k = 0; k < table->num_entries; k++) {
					if ((table->mc_reg_table_entry[k].mclk_max == 125000) ||
					    (table->mc_reg_table_entry[k].mclk_max == 137500))
						table->mc_reg_table_entry[k].mc_data[i] =
							(table->mc_reg_table_entry[k].mc_data[i] & 0xFFFFFFF8) |
							0x00000007;
				}
				break;
			case MC_SEQ_WR_CTL_D0:
				for (k = 0; k < table->num_entries; k++) {
					if ((table->mc_reg_table_entry[k].mclk_max == 125000) ||
					    (table->mc_reg_table_entry[k].mclk_max == 137500))
						table->mc_reg_table_entry[k].mc_data[i] =
							(table->mc_reg_table_entry[k].mc_data[i] & 0xFFFF0F00) |
							0x0000D0DD;
				}
				break;
			case MC_SEQ_WR_CTL_D1:
				for (k = 0; k < table->num_entries; k++) {
					if ((table->mc_reg_table_entry[k].mclk_max == 125000) ||
					    (table->mc_reg_table_entry[k].mclk_max == 137500))
						table->mc_reg_table_entry[k].mc_data[i] =
							(table->mc_reg_table_entry[k].mc_data[i] & 0xFFFF0F00) |
							0x0000D0DD;
				}
				break;
			case MC_SEQ_WR_CTL_2:
				for (k = 0; k < table->num_entries; k++) {
					if ((table->mc_reg_table_entry[k].mclk_max == 125000) ||
					    (table->mc_reg_table_entry[k].mclk_max == 137500))
						table->mc_reg_table_entry[k].mc_data[i] = 0;
				}
				break;
			case MC_SEQ_CAS_TIMING:
				for (k = 0; k < table->num_entries; k++) {
					if (table->mc_reg_table_entry[k].mclk_max == 125000)
						table->mc_reg_table_entry[k].mc_data[i] =
							(table->mc_reg_table_entry[k].mc_data[i] & 0xFFE0FE0F) |
							0x000C0140;
					else if (table->mc_reg_table_entry[k].mclk_max == 137500)
						table->mc_reg_table_entry[k].mc_data[i] =
							(table->mc_reg_table_entry[k].mc_data[i] & 0xFFE0FE0F) |
							0x000C0150;
				}
				break;
			case MC_SEQ_MISC_TIMING:
				for (k = 0; k < table->num_entries; k++) {
					if (table->mc_reg_table_entry[k].mclk_max == 125000)
						table->mc_reg_table_entry[k].mc_data[i] =
							(table->mc_reg_table_entry[k].mc_data[i] & 0xFFFFFFE0) |
							0x00000030;
					else if (table->mc_reg_table_entry[k].mclk_max == 137500)
						table->mc_reg_table_entry[k].mc_data[i] =
							(table->mc_reg_table_entry[k].mc_data[i] & 0xFFFFFFE0) |
							0x00000035;
				}
				break;
			default:
				break;
			}
		}

		WREG32(MC_SEQ_IO_DEBUG_INDEX, 3);
		tmp = RREG32(MC_SEQ_IO_DEBUG_DATA);
		tmp = (tmp & 0xFFF8FFFF) | (1 << 16);
		WREG32(MC_SEQ_IO_DEBUG_INDEX, 3);
		WREG32(MC_SEQ_IO_DEBUG_DATA, tmp);
	}

	return 0;
}

static int ci_initialize_mc_reg_table(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct atom_mc_reg_table *table;
	struct ci_mc_reg_table *ci_table = &pi->mc_reg_table;
	u8 module_index = rv770_get_memory_module_index(rdev);
	int ret;

	table = kzalloc(sizeof(struct atom_mc_reg_table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	WREG32(MC_SEQ_RAS_TIMING_LP, RREG32(MC_SEQ_RAS_TIMING));
	WREG32(MC_SEQ_CAS_TIMING_LP, RREG32(MC_SEQ_CAS_TIMING));
	WREG32(MC_SEQ_DLL_STBY_LP, RREG32(MC_SEQ_DLL_STBY));
	WREG32(MC_SEQ_G5PDX_CMD0_LP, RREG32(MC_SEQ_G5PDX_CMD0));
	WREG32(MC_SEQ_G5PDX_CMD1_LP, RREG32(MC_SEQ_G5PDX_CMD1));
	WREG32(MC_SEQ_G5PDX_CTRL_LP, RREG32(MC_SEQ_G5PDX_CTRL));
	WREG32(MC_SEQ_PMG_DVS_CMD_LP, RREG32(MC_SEQ_PMG_DVS_CMD));
	WREG32(MC_SEQ_PMG_DVS_CTL_LP, RREG32(MC_SEQ_PMG_DVS_CTL));
	WREG32(MC_SEQ_MISC_TIMING_LP, RREG32(MC_SEQ_MISC_TIMING));
	WREG32(MC_SEQ_MISC_TIMING2_LP, RREG32(MC_SEQ_MISC_TIMING2));
	WREG32(MC_SEQ_PMG_CMD_EMRS_LP, RREG32(MC_PMG_CMD_EMRS));
	WREG32(MC_SEQ_PMG_CMD_MRS_LP, RREG32(MC_PMG_CMD_MRS));
	WREG32(MC_SEQ_PMG_CMD_MRS1_LP, RREG32(MC_PMG_CMD_MRS1));
	WREG32(MC_SEQ_WR_CTL_D0_LP, RREG32(MC_SEQ_WR_CTL_D0));
	WREG32(MC_SEQ_WR_CTL_D1_LP, RREG32(MC_SEQ_WR_CTL_D1));
	WREG32(MC_SEQ_RD_CTL_D0_LP, RREG32(MC_SEQ_RD_CTL_D0));
	WREG32(MC_SEQ_RD_CTL_D1_LP, RREG32(MC_SEQ_RD_CTL_D1));
	WREG32(MC_SEQ_PMG_TIMING_LP, RREG32(MC_SEQ_PMG_TIMING));
	WREG32(MC_SEQ_PMG_CMD_MRS2_LP, RREG32(MC_PMG_CMD_MRS2));
	WREG32(MC_SEQ_WR_CTL_2_LP, RREG32(MC_SEQ_WR_CTL_2));

	ret = radeon_atom_init_mc_reg_table(rdev, module_index, table);
	if (ret)
		goto init_mc_done;

	ret = ci_copy_vbios_mc_reg_table(table, ci_table);
	if (ret)
		goto init_mc_done;

	ci_set_s0_mc_reg_index(ci_table);

	ret = ci_register_patching_mc_seq(rdev, ci_table);
	if (ret)
		goto init_mc_done;

	ret = ci_set_mc_special_registers(rdev, ci_table);
	if (ret)
		goto init_mc_done;

	ci_set_valid_flag(ci_table);

init_mc_done:
	kfree(table);

	return ret;
}

static int ci_populate_mc_reg_addresses(struct radeon_device *rdev,
					SMU7_Discrete_MCRegisters *mc_reg_table)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u32 i, j;

	for (i = 0, j = 0; j < pi->mc_reg_table.last; j++) {
		if (pi->mc_reg_table.valid_flag & (1 << j)) {
			if (i >= SMU7_DISCRETE_MC_REGISTER_ARRAY_SIZE)
				return -EINVAL;
			mc_reg_table->address[i].s0 = cpu_to_be16(pi->mc_reg_table.mc_reg_address[j].s0);
			mc_reg_table->address[i].s1 = cpu_to_be16(pi->mc_reg_table.mc_reg_address[j].s1);
			i++;
		}
	}

	mc_reg_table->last = (u8)i;

	return 0;
}

static void ci_convert_mc_registers(const struct ci_mc_reg_entry *entry,
				    SMU7_Discrete_MCRegisterSet *data,
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

static void ci_convert_mc_reg_table_entry_to_smc(struct radeon_device *rdev,
						 const u32 memory_clock,
						 SMU7_Discrete_MCRegisterSet *mc_reg_table_data)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u32 i = 0;

	for(i = 0; i < pi->mc_reg_table.num_entries; i++) {
		if (memory_clock <= pi->mc_reg_table.mc_reg_table_entry[i].mclk_max)
			break;
	}

	if ((i == pi->mc_reg_table.num_entries) && (i > 0))
		--i;

	ci_convert_mc_registers(&pi->mc_reg_table.mc_reg_table_entry[i],
				mc_reg_table_data, pi->mc_reg_table.last,
				pi->mc_reg_table.valid_flag);
}

static void ci_convert_mc_reg_table_to_smc(struct radeon_device *rdev,
					   SMU7_Discrete_MCRegisters *mc_reg_table)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u32 i;

	for (i = 0; i < pi->dpm_table.mclk_table.count; i++)
		ci_convert_mc_reg_table_entry_to_smc(rdev,
						     pi->dpm_table.mclk_table.dpm_levels[i].value,
						     &mc_reg_table->data[i]);
}

static int ci_populate_initial_mc_reg_table(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	int ret;

	memset(&pi->smc_mc_reg_table, 0, sizeof(SMU7_Discrete_MCRegisters));

	ret = ci_populate_mc_reg_addresses(rdev, &pi->smc_mc_reg_table);
	if (ret)
		return ret;
	ci_convert_mc_reg_table_to_smc(rdev, &pi->smc_mc_reg_table);

	return ci_copy_bytes_to_smc(rdev,
				    pi->mc_reg_table_start,
				    (u8 *)&pi->smc_mc_reg_table,
				    sizeof(SMU7_Discrete_MCRegisters),
				    pi->sram_end);
}

static int ci_update_and_upload_mc_reg_table(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);

	if (!(pi->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_MCLK))
		return 0;

	memset(&pi->smc_mc_reg_table, 0, sizeof(SMU7_Discrete_MCRegisters));

	ci_convert_mc_reg_table_to_smc(rdev, &pi->smc_mc_reg_table);

	return ci_copy_bytes_to_smc(rdev,
				    pi->mc_reg_table_start +
				    offsetof(SMU7_Discrete_MCRegisters, data[0]),
				    (u8 *)&pi->smc_mc_reg_table.data[0],
				    sizeof(SMU7_Discrete_MCRegisterSet) *
				    pi->dpm_table.mclk_table.count,
				    pi->sram_end);
}

static void ci_enable_voltage_control(struct radeon_device *rdev)
{
	u32 tmp = RREG32_SMC(GENERAL_PWRMGT);

	tmp |= VOLT_PWRMGT_EN;
	WREG32_SMC(GENERAL_PWRMGT, tmp);
}

static enum radeon_pcie_gen ci_get_maximum_link_speed(struct radeon_device *rdev,
						      struct radeon_ps *radeon_state)
{
	struct ci_ps *state = ci_get_ps(radeon_state);
	int i;
	u16 pcie_speed, max_speed = 0;

	for (i = 0; i < state->performance_level_count; i++) {
		pcie_speed = state->performance_levels[i].pcie_gen;
		if (max_speed < pcie_speed)
			max_speed = pcie_speed;
	}

	return max_speed;
}

static u16 ci_get_current_pcie_speed(struct radeon_device *rdev)
{
	u32 speed_cntl = 0;

	speed_cntl = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL) & LC_CURRENT_DATA_RATE_MASK;
	speed_cntl >>= LC_CURRENT_DATA_RATE_SHIFT;

	return (u16)speed_cntl;
}

static int ci_get_current_pcie_lane_number(struct radeon_device *rdev)
{
	u32 link_width = 0;

	link_width = RREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL) & LC_LINK_WIDTH_RD_MASK;
	link_width >>= LC_LINK_WIDTH_RD_SHIFT;

	switch (link_width) {
	case RADEON_PCIE_LC_LINK_WIDTH_X1:
		return 1;
	case RADEON_PCIE_LC_LINK_WIDTH_X2:
		return 2;
	case RADEON_PCIE_LC_LINK_WIDTH_X4:
		return 4;
	case RADEON_PCIE_LC_LINK_WIDTH_X8:
		return 8;
	case RADEON_PCIE_LC_LINK_WIDTH_X12:
		/* not actually supported */
		return 12;
	case RADEON_PCIE_LC_LINK_WIDTH_X0:
	case RADEON_PCIE_LC_LINK_WIDTH_X16:
	default:
		return 16;
	}
}

static void ci_request_link_speed_change_before_state_change(struct radeon_device *rdev,
							     struct radeon_ps *radeon_new_state,
							     struct radeon_ps *radeon_current_state)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	enum radeon_pcie_gen target_link_speed =
		ci_get_maximum_link_speed(rdev, radeon_new_state);
	enum radeon_pcie_gen current_link_speed;

	if (pi->force_pcie_gen == RADEON_PCIE_GEN_INVALID)
		current_link_speed = ci_get_maximum_link_speed(rdev, radeon_current_state);
	else
		current_link_speed = pi->force_pcie_gen;

	pi->force_pcie_gen = RADEON_PCIE_GEN_INVALID;
	pi->pspp_notify_required = false;
	if (target_link_speed > current_link_speed) {
		switch (target_link_speed) {
#ifdef CONFIG_ACPI
		case RADEON_PCIE_GEN3:
			if (radeon_acpi_pcie_performance_request(rdev, PCIE_PERF_REQ_PECI_GEN3, false) == 0)
				break;
			pi->force_pcie_gen = RADEON_PCIE_GEN2;
			if (current_link_speed == RADEON_PCIE_GEN2)
				break;
			fallthrough;
		case RADEON_PCIE_GEN2:
			if (radeon_acpi_pcie_performance_request(rdev, PCIE_PERF_REQ_PECI_GEN2, false) == 0)
				break;
			fallthrough;
#endif
		default:
			pi->force_pcie_gen = ci_get_current_pcie_speed(rdev);
			break;
		}
	} else {
		if (target_link_speed < current_link_speed)
			pi->pspp_notify_required = true;
	}
}

static void ci_notify_link_speed_change_after_state_change(struct radeon_device *rdev,
							   struct radeon_ps *radeon_new_state,
							   struct radeon_ps *radeon_current_state)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	enum radeon_pcie_gen target_link_speed =
		ci_get_maximum_link_speed(rdev, radeon_new_state);
	u8 request;

	if (pi->pspp_notify_required) {
		if (target_link_speed == RADEON_PCIE_GEN3)
			request = PCIE_PERF_REQ_PECI_GEN3;
		else if (target_link_speed == RADEON_PCIE_GEN2)
			request = PCIE_PERF_REQ_PECI_GEN2;
		else
			request = PCIE_PERF_REQ_PECI_GEN1;

		if ((request == PCIE_PERF_REQ_PECI_GEN1) &&
		    (ci_get_current_pcie_speed(rdev) > 0))
			return;

#ifdef CONFIG_ACPI
		radeon_acpi_pcie_performance_request(rdev, request, false);
#endif
	}
}

static int ci_set_private_data_variables_based_on_pptable(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct radeon_clock_voltage_dependency_table *allowed_sclk_vddc_table =
		&rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk;
	struct radeon_clock_voltage_dependency_table *allowed_mclk_vddc_table =
		&rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk;
	struct radeon_clock_voltage_dependency_table *allowed_mclk_vddci_table =
		&rdev->pm.dpm.dyn_state.vddci_dependency_on_mclk;

	if (allowed_sclk_vddc_table == NULL)
		return -EINVAL;
	if (allowed_sclk_vddc_table->count < 1)
		return -EINVAL;
	if (allowed_mclk_vddc_table == NULL)
		return -EINVAL;
	if (allowed_mclk_vddc_table->count < 1)
		return -EINVAL;
	if (allowed_mclk_vddci_table == NULL)
		return -EINVAL;
	if (allowed_mclk_vddci_table->count < 1)
		return -EINVAL;

	pi->min_vddc_in_pp_table = allowed_sclk_vddc_table->entries[0].v;
	pi->max_vddc_in_pp_table =
		allowed_sclk_vddc_table->entries[allowed_sclk_vddc_table->count - 1].v;

	pi->min_vddci_in_pp_table = allowed_mclk_vddci_table->entries[0].v;
	pi->max_vddci_in_pp_table =
		allowed_mclk_vddci_table->entries[allowed_mclk_vddci_table->count - 1].v;

	rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac.sclk =
		allowed_sclk_vddc_table->entries[allowed_sclk_vddc_table->count - 1].clk;
	rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac.mclk =
		allowed_mclk_vddc_table->entries[allowed_sclk_vddc_table->count - 1].clk;
	rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac.vddc =
		allowed_sclk_vddc_table->entries[allowed_sclk_vddc_table->count - 1].v;
	rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac.vddci =
		allowed_mclk_vddci_table->entries[allowed_mclk_vddci_table->count - 1].v;

	return 0;
}

static void ci_patch_with_vddc_leakage(struct radeon_device *rdev, u16 *vddc)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct ci_leakage_voltage *leakage_table = &pi->vddc_leakage;
	u32 leakage_index;

	for (leakage_index = 0; leakage_index < leakage_table->count; leakage_index++) {
		if (leakage_table->leakage_id[leakage_index] == *vddc) {
			*vddc = leakage_table->actual_voltage[leakage_index];
			break;
		}
	}
}

static void ci_patch_with_vddci_leakage(struct radeon_device *rdev, u16 *vddci)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct ci_leakage_voltage *leakage_table = &pi->vddci_leakage;
	u32 leakage_index;

	for (leakage_index = 0; leakage_index < leakage_table->count; leakage_index++) {
		if (leakage_table->leakage_id[leakage_index] == *vddci) {
			*vddci = leakage_table->actual_voltage[leakage_index];
			break;
		}
	}
}

static void ci_patch_clock_voltage_dependency_table_with_vddc_leakage(struct radeon_device *rdev,
								      struct radeon_clock_voltage_dependency_table *table)
{
	u32 i;

	if (table) {
		for (i = 0; i < table->count; i++)
			ci_patch_with_vddc_leakage(rdev, &table->entries[i].v);
	}
}

static void ci_patch_clock_voltage_dependency_table_with_vddci_leakage(struct radeon_device *rdev,
								       struct radeon_clock_voltage_dependency_table *table)
{
	u32 i;

	if (table) {
		for (i = 0; i < table->count; i++)
			ci_patch_with_vddci_leakage(rdev, &table->entries[i].v);
	}
}

static void ci_patch_vce_clock_voltage_dependency_table_with_vddc_leakage(struct radeon_device *rdev,
									  struct radeon_vce_clock_voltage_dependency_table *table)
{
	u32 i;

	if (table) {
		for (i = 0; i < table->count; i++)
			ci_patch_with_vddc_leakage(rdev, &table->entries[i].v);
	}
}

static void ci_patch_uvd_clock_voltage_dependency_table_with_vddc_leakage(struct radeon_device *rdev,
									  struct radeon_uvd_clock_voltage_dependency_table *table)
{
	u32 i;

	if (table) {
		for (i = 0; i < table->count; i++)
			ci_patch_with_vddc_leakage(rdev, &table->entries[i].v);
	}
}

static void ci_patch_vddc_phase_shed_limit_table_with_vddc_leakage(struct radeon_device *rdev,
								   struct radeon_phase_shedding_limits_table *table)
{
	u32 i;

	if (table) {
		for (i = 0; i < table->count; i++)
			ci_patch_with_vddc_leakage(rdev, &table->entries[i].voltage);
	}
}

static void ci_patch_clock_voltage_limits_with_vddc_leakage(struct radeon_device *rdev,
							    struct radeon_clock_and_voltage_limits *table)
{
	if (table) {
		ci_patch_with_vddc_leakage(rdev, (u16 *)&table->vddc);
		ci_patch_with_vddci_leakage(rdev, (u16 *)&table->vddci);
	}
}

static void ci_patch_cac_leakage_table_with_vddc_leakage(struct radeon_device *rdev,
							 struct radeon_cac_leakage_table *table)
{
	u32 i;

	if (table) {
		for (i = 0; i < table->count; i++)
			ci_patch_with_vddc_leakage(rdev, &table->entries[i].vddc);
	}
}

static void ci_patch_dependency_tables_with_leakage(struct radeon_device *rdev)
{

	ci_patch_clock_voltage_dependency_table_with_vddc_leakage(rdev,
								  &rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk);
	ci_patch_clock_voltage_dependency_table_with_vddc_leakage(rdev,
								  &rdev->pm.dpm.dyn_state.vddc_dependency_on_mclk);
	ci_patch_clock_voltage_dependency_table_with_vddc_leakage(rdev,
								  &rdev->pm.dpm.dyn_state.vddc_dependency_on_dispclk);
	ci_patch_clock_voltage_dependency_table_with_vddci_leakage(rdev,
								   &rdev->pm.dpm.dyn_state.vddci_dependency_on_mclk);
	ci_patch_vce_clock_voltage_dependency_table_with_vddc_leakage(rdev,
								      &rdev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table);
	ci_patch_uvd_clock_voltage_dependency_table_with_vddc_leakage(rdev,
								      &rdev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table);
	ci_patch_clock_voltage_dependency_table_with_vddc_leakage(rdev,
								  &rdev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table);
	ci_patch_clock_voltage_dependency_table_with_vddc_leakage(rdev,
								  &rdev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table);
	ci_patch_vddc_phase_shed_limit_table_with_vddc_leakage(rdev,
							       &rdev->pm.dpm.dyn_state.phase_shedding_limits_table);
	ci_patch_clock_voltage_limits_with_vddc_leakage(rdev,
							&rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac);
	ci_patch_clock_voltage_limits_with_vddc_leakage(rdev,
							&rdev->pm.dpm.dyn_state.max_clock_voltage_on_dc);
	ci_patch_cac_leakage_table_with_vddc_leakage(rdev,
						     &rdev->pm.dpm.dyn_state.cac_leakage_table);

}

static void ci_get_memory_type(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	u32 tmp;

	tmp = RREG32(MC_SEQ_MISC0);

	if (((tmp & MC_SEQ_MISC0_GDDR5_MASK) >> MC_SEQ_MISC0_GDDR5_SHIFT) ==
	    MC_SEQ_MISC0_GDDR5_VALUE)
		pi->mem_gddr5 = true;
	else
		pi->mem_gddr5 = false;

}

static void ci_update_current_ps(struct radeon_device *rdev,
				 struct radeon_ps *rps)
{
	struct ci_ps *new_ps = ci_get_ps(rps);
	struct ci_power_info *pi = ci_get_pi(rdev);

	pi->current_rps = *rps;
	pi->current_ps = *new_ps;
	pi->current_rps.ps_priv = &pi->current_ps;
}

static void ci_update_requested_ps(struct radeon_device *rdev,
				   struct radeon_ps *rps)
{
	struct ci_ps *new_ps = ci_get_ps(rps);
	struct ci_power_info *pi = ci_get_pi(rdev);

	pi->requested_rps = *rps;
	pi->requested_ps = *new_ps;
	pi->requested_rps.ps_priv = &pi->requested_ps;
}

int ci_dpm_pre_set_power_state(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct radeon_ps requested_ps = *rdev->pm.dpm.requested_ps;
	struct radeon_ps *new_ps = &requested_ps;

	ci_update_requested_ps(rdev, new_ps);

	ci_apply_state_adjust_rules(rdev, &pi->requested_rps);

	return 0;
}

void ci_dpm_post_set_power_state(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct radeon_ps *new_ps = &pi->requested_rps;

	ci_update_current_ps(rdev, new_ps);
}


void ci_dpm_setup_asic(struct radeon_device *rdev)
{
	int r;

	r = ci_mc_load_microcode(rdev);
	if (r)
		DRM_ERROR("Failed to load MC firmware!\n");
	ci_read_clock_registers(rdev);
	ci_get_memory_type(rdev);
	ci_enable_acpi_power_management(rdev);
	ci_init_sclk_t(rdev);
}

int ci_dpm_enable(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct radeon_ps *boot_ps = rdev->pm.dpm.boot_ps;
	int ret;

	if (ci_is_smc_running(rdev))
		return -EINVAL;
	if (pi->voltage_control != CISLANDS_VOLTAGE_CONTROL_NONE) {
		ci_enable_voltage_control(rdev);
		ret = ci_construct_voltage_tables(rdev);
		if (ret) {
			DRM_ERROR("ci_construct_voltage_tables failed\n");
			return ret;
		}
	}
	if (pi->caps_dynamic_ac_timing) {
		ret = ci_initialize_mc_reg_table(rdev);
		if (ret)
			pi->caps_dynamic_ac_timing = false;
	}
	if (pi->dynamic_ss)
		ci_enable_spread_spectrum(rdev, true);
	if (pi->thermal_protection)
		ci_enable_thermal_protection(rdev, true);
	ci_program_sstp(rdev);
	ci_enable_display_gap(rdev);
	ci_program_vc(rdev);
	ret = ci_upload_firmware(rdev);
	if (ret) {
		DRM_ERROR("ci_upload_firmware failed\n");
		return ret;
	}
	ret = ci_process_firmware_header(rdev);
	if (ret) {
		DRM_ERROR("ci_process_firmware_header failed\n");
		return ret;
	}
	ret = ci_initial_switch_from_arb_f0_to_f1(rdev);
	if (ret) {
		DRM_ERROR("ci_initial_switch_from_arb_f0_to_f1 failed\n");
		return ret;
	}
	ret = ci_init_smc_table(rdev);
	if (ret) {
		DRM_ERROR("ci_init_smc_table failed\n");
		return ret;
	}
	ret = ci_init_arb_table_index(rdev);
	if (ret) {
		DRM_ERROR("ci_init_arb_table_index failed\n");
		return ret;
	}
	if (pi->caps_dynamic_ac_timing) {
		ret = ci_populate_initial_mc_reg_table(rdev);
		if (ret) {
			DRM_ERROR("ci_populate_initial_mc_reg_table failed\n");
			return ret;
		}
	}
	ret = ci_populate_pm_base(rdev);
	if (ret) {
		DRM_ERROR("ci_populate_pm_base failed\n");
		return ret;
	}
	ci_dpm_start_smc(rdev);
	ci_enable_vr_hot_gpio_interrupt(rdev);
	ret = ci_notify_smc_display_change(rdev, false);
	if (ret) {
		DRM_ERROR("ci_notify_smc_display_change failed\n");
		return ret;
	}
	ci_enable_sclk_control(rdev, true);
	ret = ci_enable_ulv(rdev, true);
	if (ret) {
		DRM_ERROR("ci_enable_ulv failed\n");
		return ret;
	}
	ret = ci_enable_ds_master_switch(rdev, true);
	if (ret) {
		DRM_ERROR("ci_enable_ds_master_switch failed\n");
		return ret;
	}
	ret = ci_start_dpm(rdev);
	if (ret) {
		DRM_ERROR("ci_start_dpm failed\n");
		return ret;
	}
	ret = ci_enable_didt(rdev, true);
	if (ret) {
		DRM_ERROR("ci_enable_didt failed\n");
		return ret;
	}
	ret = ci_enable_smc_cac(rdev, true);
	if (ret) {
		DRM_ERROR("ci_enable_smc_cac failed\n");
		return ret;
	}
	ret = ci_enable_power_containment(rdev, true);
	if (ret) {
		DRM_ERROR("ci_enable_power_containment failed\n");
		return ret;
	}

	ret = ci_power_control_set_level(rdev);
	if (ret) {
		DRM_ERROR("ci_power_control_set_level failed\n");
		return ret;
	}

	ci_enable_auto_throttle_source(rdev, RADEON_DPM_AUTO_THROTTLE_SRC_THERMAL, true);

	ret = ci_enable_thermal_based_sclk_dpm(rdev, true);
	if (ret) {
		DRM_ERROR("ci_enable_thermal_based_sclk_dpm failed\n");
		return ret;
	}

	ci_thermal_start_thermal_controller(rdev);

	ci_update_current_ps(rdev, boot_ps);

	return 0;
}

static int ci_set_temperature_range(struct radeon_device *rdev)
{
	int ret;

	ret = ci_thermal_enable_alert(rdev, false);
	if (ret)
		return ret;
	ret = ci_thermal_set_temperature_range(rdev, R600_TEMP_RANGE_MIN, R600_TEMP_RANGE_MAX);
	if (ret)
		return ret;
	ret = ci_thermal_enable_alert(rdev, true);
	if (ret)
		return ret;

	return ret;
}

int ci_dpm_late_enable(struct radeon_device *rdev)
{
	int ret;

	ret = ci_set_temperature_range(rdev);
	if (ret)
		return ret;

	ci_dpm_powergate_uvd(rdev, true);

	return 0;
}

void ci_dpm_disable(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct radeon_ps *boot_ps = rdev->pm.dpm.boot_ps;

	ci_dpm_powergate_uvd(rdev, false);

	if (!ci_is_smc_running(rdev))
		return;

	ci_thermal_stop_thermal_controller(rdev);

	if (pi->thermal_protection)
		ci_enable_thermal_protection(rdev, false);
	ci_enable_power_containment(rdev, false);
	ci_enable_smc_cac(rdev, false);
	ci_enable_didt(rdev, false);
	ci_enable_spread_spectrum(rdev, false);
	ci_enable_auto_throttle_source(rdev, RADEON_DPM_AUTO_THROTTLE_SRC_THERMAL, false);
	ci_stop_dpm(rdev);
	ci_enable_ds_master_switch(rdev, false);
	ci_enable_ulv(rdev, false);
	ci_clear_vc(rdev);
	ci_reset_to_default(rdev);
	ci_dpm_stop_smc(rdev);
	ci_force_switch_to_arb_f0(rdev);
	ci_enable_thermal_based_sclk_dpm(rdev, false);

	ci_update_current_ps(rdev, boot_ps);
}

int ci_dpm_set_power_state(struct radeon_device *rdev)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct radeon_ps *new_ps = &pi->requested_rps;
	struct radeon_ps *old_ps = &pi->current_rps;
	int ret;

	ci_find_dpm_states_clocks_in_dpm_table(rdev, new_ps);
	if (pi->pcie_performance_request)
		ci_request_link_speed_change_before_state_change(rdev, new_ps, old_ps);
	ret = ci_freeze_sclk_mclk_dpm(rdev);
	if (ret) {
		DRM_ERROR("ci_freeze_sclk_mclk_dpm failed\n");
		return ret;
	}
	ret = ci_populate_and_upload_sclk_mclk_dpm_levels(rdev, new_ps);
	if (ret) {
		DRM_ERROR("ci_populate_and_upload_sclk_mclk_dpm_levels failed\n");
		return ret;
	}
	ret = ci_generate_dpm_level_enable_mask(rdev, new_ps);
	if (ret) {
		DRM_ERROR("ci_generate_dpm_level_enable_mask failed\n");
		return ret;
	}

	ret = ci_update_vce_dpm(rdev, new_ps, old_ps);
	if (ret) {
		DRM_ERROR("ci_update_vce_dpm failed\n");
		return ret;
	}

	ret = ci_update_sclk_t(rdev);
	if (ret) {
		DRM_ERROR("ci_update_sclk_t failed\n");
		return ret;
	}
	if (pi->caps_dynamic_ac_timing) {
		ret = ci_update_and_upload_mc_reg_table(rdev);
		if (ret) {
			DRM_ERROR("ci_update_and_upload_mc_reg_table failed\n");
			return ret;
		}
	}
	ret = ci_program_memory_timing_parameters(rdev);
	if (ret) {
		DRM_ERROR("ci_program_memory_timing_parameters failed\n");
		return ret;
	}
	ret = ci_unfreeze_sclk_mclk_dpm(rdev);
	if (ret) {
		DRM_ERROR("ci_unfreeze_sclk_mclk_dpm failed\n");
		return ret;
	}
	ret = ci_upload_dpm_level_enable_mask(rdev);
	if (ret) {
		DRM_ERROR("ci_upload_dpm_level_enable_mask failed\n");
		return ret;
	}
	if (pi->pcie_performance_request)
		ci_notify_link_speed_change_after_state_change(rdev, new_ps, old_ps);

	return 0;
}

#if 0
void ci_dpm_reset_asic(struct radeon_device *rdev)
{
	ci_set_boot_state(rdev);
}
#endif

void ci_dpm_display_configuration_changed(struct radeon_device *rdev)
{
	ci_program_display_gap(rdev);
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
	struct _ATOM_PPLIB_SI_CLOCK_INFO si;
	struct _ATOM_PPLIB_CI_CLOCK_INFO ci;
};

union pplib_power_state {
	struct _ATOM_PPLIB_STATE v1;
	struct _ATOM_PPLIB_STATE_V2 v2;
};

static void ci_parse_pplib_non_clock_info(struct radeon_device *rdev,
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
	} else {
		rps->vclk = 0;
		rps->dclk = 0;
	}

	if (rps->class & ATOM_PPLIB_CLASSIFICATION_BOOT)
		rdev->pm.dpm.boot_ps = rps;
	if (rps->class & ATOM_PPLIB_CLASSIFICATION_UVDSTATE)
		rdev->pm.dpm.uvd_ps = rps;
}

static void ci_parse_pplib_clock_info(struct radeon_device *rdev,
				      struct radeon_ps *rps, int index,
				      union pplib_clock_info *clock_info)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct ci_ps *ps = ci_get_ps(rps);
	struct ci_pl *pl = &ps->performance_levels[index];

	ps->performance_level_count = index + 1;

	pl->sclk = le16_to_cpu(clock_info->ci.usEngineClockLow);
	pl->sclk |= clock_info->ci.ucEngineClockHigh << 16;
	pl->mclk = le16_to_cpu(clock_info->ci.usMemoryClockLow);
	pl->mclk |= clock_info->ci.ucMemoryClockHigh << 16;

	pl->pcie_gen = r600_get_pcie_gen_support(rdev,
						 pi->sys_pcie_mask,
						 pi->vbios_boot_state.pcie_gen_bootup_value,
						 clock_info->ci.ucPCIEGen);
	pl->pcie_lane = r600_get_pcie_lane_support(rdev,
						   pi->vbios_boot_state.pcie_lane_bootup_value,
						   le16_to_cpu(clock_info->ci.usPCIELane));

	if (rps->class & ATOM_PPLIB_CLASSIFICATION_ACPI) {
		pi->acpi_pcie_gen = pl->pcie_gen;
	}

	if (rps->class2 & ATOM_PPLIB_CLASSIFICATION2_ULV) {
		pi->ulv.supported = true;
		pi->ulv.pl = *pl;
		pi->ulv.cg_ulv_parameter = CISLANDS_CGULVPARAMETER_DFLT;
	}

	/* patch up boot state */
	if (rps->class & ATOM_PPLIB_CLASSIFICATION_BOOT) {
		pl->mclk = pi->vbios_boot_state.mclk_bootup_value;
		pl->sclk = pi->vbios_boot_state.sclk_bootup_value;
		pl->pcie_gen = pi->vbios_boot_state.pcie_gen_bootup_value;
		pl->pcie_lane = pi->vbios_boot_state.pcie_lane_bootup_value;
	}

	switch (rps->class & ATOM_PPLIB_CLASSIFICATION_UI_MASK) {
	case ATOM_PPLIB_CLASSIFICATION_UI_BATTERY:
		pi->use_pcie_powersaving_levels = true;
		if (pi->pcie_gen_powersaving.max < pl->pcie_gen)
			pi->pcie_gen_powersaving.max = pl->pcie_gen;
		if (pi->pcie_gen_powersaving.min > pl->pcie_gen)
			pi->pcie_gen_powersaving.min = pl->pcie_gen;
		if (pi->pcie_lane_powersaving.max < pl->pcie_lane)
			pi->pcie_lane_powersaving.max = pl->pcie_lane;
		if (pi->pcie_lane_powersaving.min > pl->pcie_lane)
			pi->pcie_lane_powersaving.min = pl->pcie_lane;
		break;
	case ATOM_PPLIB_CLASSIFICATION_UI_PERFORMANCE:
		pi->use_pcie_performance_levels = true;
		if (pi->pcie_gen_performance.max < pl->pcie_gen)
			pi->pcie_gen_performance.max = pl->pcie_gen;
		if (pi->pcie_gen_performance.min > pl->pcie_gen)
			pi->pcie_gen_performance.min = pl->pcie_gen;
		if (pi->pcie_lane_performance.max < pl->pcie_lane)
			pi->pcie_lane_performance.max = pl->pcie_lane;
		if (pi->pcie_lane_performance.min > pl->pcie_lane)
			pi->pcie_lane_performance.min = pl->pcie_lane;
		break;
	default:
		break;
	}
}

static int ci_parse_power_table(struct radeon_device *rdev)
{
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	struct _ATOM_PPLIB_NONCLOCK_INFO *non_clock_info;
	union pplib_power_state *power_state;
	int i, j, k, non_clock_array_index, clock_array_index;
	union pplib_clock_info *clock_info;
	struct _StateArray *state_array;
	struct _ClockInfoArray *clock_info_array;
	struct _NonClockInfoArray *non_clock_info_array;
	union power_info *power_info;
	int index = GetIndexIntoMasterTable(DATA, PowerPlayInfo);
	u16 data_offset;
	u8 frev, crev;
	u8 *power_state_offset;
	struct ci_ps *ps;

	if (!atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset))
		return -EINVAL;
	power_info = (union power_info *)(mode_info->atom_context->bios + data_offset);

	state_array = (struct _StateArray *)
		(mode_info->atom_context->bios + data_offset +
		 le16_to_cpu(power_info->pplib.usStateArrayOffset));
	clock_info_array = (struct _ClockInfoArray *)
		(mode_info->atom_context->bios + data_offset +
		 le16_to_cpu(power_info->pplib.usClockInfoArrayOffset));
	non_clock_info_array = (struct _NonClockInfoArray *)
		(mode_info->atom_context->bios + data_offset +
		 le16_to_cpu(power_info->pplib.usNonClockInfoArrayOffset));

	rdev->pm.dpm.ps = kcalloc(state_array->ucNumEntries,
				  sizeof(struct radeon_ps),
				  GFP_KERNEL);
	if (!rdev->pm.dpm.ps)
		return -ENOMEM;
	power_state_offset = (u8 *)state_array->states;
	rdev->pm.dpm.num_ps = 0;
	for (i = 0; i < state_array->ucNumEntries; i++) {
		u8 *idx;
		power_state = (union pplib_power_state *)power_state_offset;
		non_clock_array_index = power_state->v2.nonClockInfoIndex;
		non_clock_info = (struct _ATOM_PPLIB_NONCLOCK_INFO *)
			&non_clock_info_array->nonClockInfo[non_clock_array_index];
		if (!rdev->pm.power_state[i].clock_info)
			return -EINVAL;
		ps = kzalloc(sizeof(struct ci_ps), GFP_KERNEL);
		if (ps == NULL)
			return -ENOMEM;
		rdev->pm.dpm.ps[i].ps_priv = ps;
		ci_parse_pplib_non_clock_info(rdev, &rdev->pm.dpm.ps[i],
					      non_clock_info,
					      non_clock_info_array->ucEntrySize);
		k = 0;
		idx = (u8 *)&power_state->v2.clockInfoIndex[0];
		for (j = 0; j < power_state->v2.ucNumDPMLevels; j++) {
			clock_array_index = idx[j];
			if (clock_array_index >= clock_info_array->ucNumEntries)
				continue;
			if (k >= CISLANDS_MAX_HARDWARE_POWERLEVELS)
				break;
			clock_info = (union pplib_clock_info *)
				((u8 *)&clock_info_array->clockInfo[0] +
				 (clock_array_index * clock_info_array->ucEntrySize));
			ci_parse_pplib_clock_info(rdev,
						  &rdev->pm.dpm.ps[i], k,
						  clock_info);
			k++;
		}
		power_state_offset += 2 + power_state->v2.ucNumDPMLevels;
		rdev->pm.dpm.num_ps = i + 1;
	}

	/* fill in the vce power states */
	for (i = 0; i < RADEON_MAX_VCE_LEVELS; i++) {
		u32 sclk, mclk;
		clock_array_index = rdev->pm.dpm.vce_states[i].clk_idx;
		clock_info = (union pplib_clock_info *)
			&clock_info_array->clockInfo[clock_array_index * clock_info_array->ucEntrySize];
		sclk = le16_to_cpu(clock_info->ci.usEngineClockLow);
		sclk |= clock_info->ci.ucEngineClockHigh << 16;
		mclk = le16_to_cpu(clock_info->ci.usMemoryClockLow);
		mclk |= clock_info->ci.ucMemoryClockHigh << 16;
		rdev->pm.dpm.vce_states[i].sclk = sclk;
		rdev->pm.dpm.vce_states[i].mclk = mclk;
	}

	return 0;
}

static int ci_get_vbios_boot_values(struct radeon_device *rdev,
				    struct ci_vbios_boot_state *boot_state)
{
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, FirmwareInfo);
	ATOM_FIRMWARE_INFO_V2_2 *firmware_info;
	u8 frev, crev;
	u16 data_offset;

	if (atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		firmware_info =
			(ATOM_FIRMWARE_INFO_V2_2 *)(mode_info->atom_context->bios +
						    data_offset);
		boot_state->mvdd_bootup_value = le16_to_cpu(firmware_info->usBootUpMVDDCVoltage);
		boot_state->vddc_bootup_value = le16_to_cpu(firmware_info->usBootUpVDDCVoltage);
		boot_state->vddci_bootup_value = le16_to_cpu(firmware_info->usBootUpVDDCIVoltage);
		boot_state->pcie_gen_bootup_value = ci_get_current_pcie_speed(rdev);
		boot_state->pcie_lane_bootup_value = ci_get_current_pcie_lane_number(rdev);
		boot_state->sclk_bootup_value = le32_to_cpu(firmware_info->ulDefaultEngineClock);
		boot_state->mclk_bootup_value = le32_to_cpu(firmware_info->ulDefaultMemoryClock);

		return 0;
	}
	return -EINVAL;
}

void ci_dpm_fini(struct radeon_device *rdev)
{
	int i;

	for (i = 0; i < rdev->pm.dpm.num_ps; i++) {
		kfree(rdev->pm.dpm.ps[i].ps_priv);
	}
	kfree(rdev->pm.dpm.ps);
	kfree(rdev->pm.dpm.priv);
	kfree(rdev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries);
	r600_free_extended_power_table(rdev);
}

int ci_dpm_init(struct radeon_device *rdev)
{
	int index = GetIndexIntoMasterTable(DATA, ASIC_InternalSS_Info);
	SMU7_Discrete_DpmTable  *dpm_table;
	struct radeon_gpio_rec gpio;
	u16 data_offset, size;
	u8 frev, crev;
	struct ci_power_info *pi;
	enum pci_bus_speed speed_cap = PCI_SPEED_UNKNOWN;
	struct pci_dev *root = rdev->pdev->bus->self;
	int ret;

	pi = kzalloc(sizeof(struct ci_power_info), GFP_KERNEL);
	if (pi == NULL)
		return -ENOMEM;
	rdev->pm.dpm.priv = pi;

	if (!pci_is_root_bus(rdev->pdev->bus))
		speed_cap = pcie_get_speed_cap(root);
	if (speed_cap == PCI_SPEED_UNKNOWN) {
		pi->sys_pcie_mask = 0;
	} else {
		if (speed_cap == PCIE_SPEED_8_0GT)
			pi->sys_pcie_mask = RADEON_PCIE_SPEED_25 |
				RADEON_PCIE_SPEED_50 |
				RADEON_PCIE_SPEED_80;
		else if (speed_cap == PCIE_SPEED_5_0GT)
			pi->sys_pcie_mask = RADEON_PCIE_SPEED_25 |
				RADEON_PCIE_SPEED_50;
		else
			pi->sys_pcie_mask = RADEON_PCIE_SPEED_25;
	}
	pi->force_pcie_gen = RADEON_PCIE_GEN_INVALID;

	pi->pcie_gen_performance.max = RADEON_PCIE_GEN1;
	pi->pcie_gen_performance.min = RADEON_PCIE_GEN3;
	pi->pcie_gen_powersaving.max = RADEON_PCIE_GEN1;
	pi->pcie_gen_powersaving.min = RADEON_PCIE_GEN3;

	pi->pcie_lane_performance.max = 0;
	pi->pcie_lane_performance.min = 16;
	pi->pcie_lane_powersaving.max = 0;
	pi->pcie_lane_powersaving.min = 16;

	ret = ci_get_vbios_boot_values(rdev, &pi->vbios_boot_state);
	if (ret) {
		ci_dpm_fini(rdev);
		return ret;
	}

	ret = r600_get_platform_caps(rdev);
	if (ret) {
		ci_dpm_fini(rdev);
		return ret;
	}

	ret = r600_parse_extended_power_table(rdev);
	if (ret) {
		ci_dpm_fini(rdev);
		return ret;
	}

	ret = ci_parse_power_table(rdev);
	if (ret) {
		ci_dpm_fini(rdev);
		return ret;
	}

	pi->dll_default_on = false;
	pi->sram_end = SMC_RAM_END;

	pi->activity_target[0] = CISLAND_TARGETACTIVITY_DFLT;
	pi->activity_target[1] = CISLAND_TARGETACTIVITY_DFLT;
	pi->activity_target[2] = CISLAND_TARGETACTIVITY_DFLT;
	pi->activity_target[3] = CISLAND_TARGETACTIVITY_DFLT;
	pi->activity_target[4] = CISLAND_TARGETACTIVITY_DFLT;
	pi->activity_target[5] = CISLAND_TARGETACTIVITY_DFLT;
	pi->activity_target[6] = CISLAND_TARGETACTIVITY_DFLT;
	pi->activity_target[7] = CISLAND_TARGETACTIVITY_DFLT;

	pi->mclk_activity_target = CISLAND_MCLK_TARGETACTIVITY_DFLT;

	pi->sclk_dpm_key_disabled = 0;
	pi->mclk_dpm_key_disabled = 0;
	pi->pcie_dpm_key_disabled = 0;
	pi->thermal_sclk_dpm_enabled = 0;

	/* mclk dpm is unstable on some R7 260X cards with the old mc ucode */
	if ((rdev->pdev->device == 0x6658) &&
	    (rdev->mc_fw->size == (BONAIRE_MC_UCODE_SIZE * 4))) {
		pi->mclk_dpm_key_disabled = 1;
	}

	pi->caps_sclk_ds = true;

	pi->mclk_strobe_mode_threshold = 40000;
	pi->mclk_stutter_mode_threshold = 40000;
	pi->mclk_edc_enable_threshold = 40000;
	pi->mclk_edc_wr_enable_threshold = 40000;

	ci_initialize_powertune_defaults(rdev);

	pi->caps_fps = false;

	pi->caps_sclk_throttle_low_notification = false;

	pi->caps_uvd_dpm = true;
	pi->caps_vce_dpm = true;

	ci_get_leakage_voltages(rdev);
	ci_patch_dependency_tables_with_leakage(rdev);
	ci_set_private_data_variables_based_on_pptable(rdev);

	rdev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries =
		kcalloc(4,
			sizeof(struct radeon_clock_voltage_dependency_entry),
			GFP_KERNEL);
	if (!rdev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries) {
		ci_dpm_fini(rdev);
		return -ENOMEM;
	}
	rdev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.count = 4;
	rdev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries[0].clk = 0;
	rdev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries[0].v = 0;
	rdev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries[1].clk = 36000;
	rdev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries[1].v = 720;
	rdev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries[2].clk = 54000;
	rdev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries[2].v = 810;
	rdev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries[3].clk = 72000;
	rdev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries[3].v = 900;

	rdev->pm.dpm.dyn_state.mclk_sclk_ratio = 4;
	rdev->pm.dpm.dyn_state.sclk_mclk_delta = 15000;
	rdev->pm.dpm.dyn_state.vddc_vddci_delta = 200;

	rdev->pm.dpm.dyn_state.valid_sclk_values.count = 0;
	rdev->pm.dpm.dyn_state.valid_sclk_values.values = NULL;
	rdev->pm.dpm.dyn_state.valid_mclk_values.count = 0;
	rdev->pm.dpm.dyn_state.valid_mclk_values.values = NULL;

	if (rdev->family == CHIP_HAWAII) {
		pi->thermal_temp_setting.temperature_low = 94500;
		pi->thermal_temp_setting.temperature_high = 95000;
		pi->thermal_temp_setting.temperature_shutdown = 104000;
	} else {
		pi->thermal_temp_setting.temperature_low = 99500;
		pi->thermal_temp_setting.temperature_high = 100000;
		pi->thermal_temp_setting.temperature_shutdown = 104000;
	}

	pi->uvd_enabled = false;

	dpm_table = &pi->smc_state_table;

	gpio = radeon_atombios_lookup_gpio(rdev, VDDC_VRHOT_GPIO_PINID);
	if (gpio.valid) {
		dpm_table->VRHotGpio = gpio.shift;
		rdev->pm.dpm.platform_caps |= ATOM_PP_PLATFORM_CAP_REGULATOR_HOT;
	} else {
		dpm_table->VRHotGpio = CISLANDS_UNUSED_GPIO_PIN;
		rdev->pm.dpm.platform_caps &= ~ATOM_PP_PLATFORM_CAP_REGULATOR_HOT;
	}

	gpio = radeon_atombios_lookup_gpio(rdev, PP_AC_DC_SWITCH_GPIO_PINID);
	if (gpio.valid) {
		dpm_table->AcDcGpio = gpio.shift;
		rdev->pm.dpm.platform_caps |= ATOM_PP_PLATFORM_CAP_HARDWAREDC;
	} else {
		dpm_table->AcDcGpio = CISLANDS_UNUSED_GPIO_PIN;
		rdev->pm.dpm.platform_caps &= ~ATOM_PP_PLATFORM_CAP_HARDWAREDC;
	}

	gpio = radeon_atombios_lookup_gpio(rdev, VDDC_PCC_GPIO_PINID);
	if (gpio.valid) {
		u32 tmp = RREG32_SMC(CNB_PWRMGT_CNTL);

		switch (gpio.shift) {
		case 0:
			tmp &= ~GNB_SLOW_MODE_MASK;
			tmp |= GNB_SLOW_MODE(1);
			break;
		case 1:
			tmp &= ~GNB_SLOW_MODE_MASK;
			tmp |= GNB_SLOW_MODE(2);
			break;
		case 2:
			tmp |= GNB_SLOW;
			break;
		case 3:
			tmp |= FORCE_NB_PS1;
			break;
		case 4:
			tmp |= DPM_ENABLED;
			break;
		default:
			DRM_DEBUG("Invalid PCC GPIO: %u!\n", gpio.shift);
			break;
		}
		WREG32_SMC(CNB_PWRMGT_CNTL, tmp);
	}

	pi->voltage_control = CISLANDS_VOLTAGE_CONTROL_NONE;
	pi->vddci_control = CISLANDS_VOLTAGE_CONTROL_NONE;
	pi->mvdd_control = CISLANDS_VOLTAGE_CONTROL_NONE;
	if (radeon_atom_is_voltage_gpio(rdev, VOLTAGE_TYPE_VDDC, VOLTAGE_OBJ_GPIO_LUT))
		pi->voltage_control = CISLANDS_VOLTAGE_CONTROL_BY_GPIO;
	else if (radeon_atom_is_voltage_gpio(rdev, VOLTAGE_TYPE_VDDC, VOLTAGE_OBJ_SVID2))
		pi->voltage_control = CISLANDS_VOLTAGE_CONTROL_BY_SVID2;

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_VDDCI_CONTROL) {
		if (radeon_atom_is_voltage_gpio(rdev, VOLTAGE_TYPE_VDDCI, VOLTAGE_OBJ_GPIO_LUT))
			pi->vddci_control = CISLANDS_VOLTAGE_CONTROL_BY_GPIO;
		else if (radeon_atom_is_voltage_gpio(rdev, VOLTAGE_TYPE_VDDCI, VOLTAGE_OBJ_SVID2))
			pi->vddci_control = CISLANDS_VOLTAGE_CONTROL_BY_SVID2;
		else
			rdev->pm.dpm.platform_caps &= ~ATOM_PP_PLATFORM_CAP_VDDCI_CONTROL;
	}

	if (rdev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_MVDDCONTROL) {
		if (radeon_atom_is_voltage_gpio(rdev, VOLTAGE_TYPE_MVDDC, VOLTAGE_OBJ_GPIO_LUT))
			pi->mvdd_control = CISLANDS_VOLTAGE_CONTROL_BY_GPIO;
		else if (radeon_atom_is_voltage_gpio(rdev, VOLTAGE_TYPE_MVDDC, VOLTAGE_OBJ_SVID2))
			pi->mvdd_control = CISLANDS_VOLTAGE_CONTROL_BY_SVID2;
		else
			rdev->pm.dpm.platform_caps &= ~ATOM_PP_PLATFORM_CAP_MVDDCONTROL;
	}

	pi->vddc_phase_shed_control = true;

#if defined(CONFIG_ACPI)
	pi->pcie_performance_request =
		radeon_acpi_is_pcie_performance_request_supported(rdev);
#else
	pi->pcie_performance_request = false;
#endif

	if (atom_parse_data_header(rdev->mode_info.atom_context, index, &size,
				   &frev, &crev, &data_offset)) {
		pi->caps_sclk_ss_support = true;
		pi->caps_mclk_ss_support = true;
		pi->dynamic_ss = true;
	} else {
		pi->caps_sclk_ss_support = false;
		pi->caps_mclk_ss_support = false;
		pi->dynamic_ss = true;
	}

	if (rdev->pm.int_thermal_type != THERMAL_TYPE_NONE)
		pi->thermal_protection = true;
	else
		pi->thermal_protection = false;

	pi->caps_dynamic_ac_timing = true;

	pi->uvd_power_gated = false;

	/* make sure dc limits are valid */
	if ((rdev->pm.dpm.dyn_state.max_clock_voltage_on_dc.sclk == 0) ||
	    (rdev->pm.dpm.dyn_state.max_clock_voltage_on_dc.mclk == 0))
		rdev->pm.dpm.dyn_state.max_clock_voltage_on_dc =
			rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac;

	pi->fan_ctrl_is_in_default_mode = true;

	return 0;
}

void ci_dpm_debugfs_print_current_performance_level(struct radeon_device *rdev,
						    struct seq_file *m)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct radeon_ps *rps = &pi->current_rps;
	u32 sclk = ci_get_average_sclk_freq(rdev);
	u32 mclk = ci_get_average_mclk_freq(rdev);

	seq_printf(m, "uvd    %sabled\n", pi->uvd_enabled ? "en" : "dis");
	seq_printf(m, "vce    %sabled\n", rps->vce_active ? "en" : "dis");
	seq_printf(m, "power level avg    sclk: %u mclk: %u\n",
		   sclk, mclk);
}

void ci_dpm_print_power_state(struct radeon_device *rdev,
			      struct radeon_ps *rps)
{
	struct ci_ps *ps = ci_get_ps(rps);
	struct ci_pl *pl;
	int i;

	r600_dpm_print_class_info(rps->class, rps->class2);
	r600_dpm_print_cap_info(rps->caps);
	printk("\tuvd    vclk: %d dclk: %d\n", rps->vclk, rps->dclk);
	for (i = 0; i < ps->performance_level_count; i++) {
		pl = &ps->performance_levels[i];
		printk("\t\tpower level %d    sclk: %u mclk: %u pcie gen: %u pcie lanes: %u\n",
		       i, pl->sclk, pl->mclk, pl->pcie_gen + 1, pl->pcie_lane);
	}
	r600_dpm_print_ps_status(rdev, rps);
}

u32 ci_dpm_get_current_sclk(struct radeon_device *rdev)
{
	u32 sclk = ci_get_average_sclk_freq(rdev);

	return sclk;
}

u32 ci_dpm_get_current_mclk(struct radeon_device *rdev)
{
	u32 mclk = ci_get_average_mclk_freq(rdev);

	return mclk;
}

u32 ci_dpm_get_sclk(struct radeon_device *rdev, bool low)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct ci_ps *requested_state = ci_get_ps(&pi->requested_rps);

	if (low)
		return requested_state->performance_levels[0].sclk;
	else
		return requested_state->performance_levels[requested_state->performance_level_count - 1].sclk;
}

u32 ci_dpm_get_mclk(struct radeon_device *rdev, bool low)
{
	struct ci_power_info *pi = ci_get_pi(rdev);
	struct ci_ps *requested_state = ci_get_ps(&pi->requested_rps);

	if (low)
		return requested_state->performance_levels[0].mclk;
	else
		return requested_state->performance_levels[requested_state->performance_level_count - 1].mclk;
}
