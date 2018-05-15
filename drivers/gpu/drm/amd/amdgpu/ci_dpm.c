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
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_pm.h"
#include "amdgpu_ucode.h"
#include "cikd.h"
#include "amdgpu_dpm.h"
#include "ci_dpm.h"
#include "gfx_v7_0.h"
#include "atom.h"
#include "amd_pcie.h"
#include <linux/seq_file.h>

#include "smu/smu_7_0_1_d.h"
#include "smu/smu_7_0_1_sh_mask.h"

#include "dce/dce_8_0_d.h"
#include "dce/dce_8_0_sh_mask.h"

#include "bif/bif_4_1_d.h"
#include "bif/bif_4_1_sh_mask.h"

#include "gca/gfx_7_2_d.h"
#include "gca/gfx_7_2_sh_mask.h"

#include "gmc/gmc_7_1_d.h"
#include "gmc/gmc_7_1_sh_mask.h"

MODULE_FIRMWARE("radeon/bonaire_smc.bin");
MODULE_FIRMWARE("radeon/bonaire_k_smc.bin");
MODULE_FIRMWARE("radeon/hawaii_smc.bin");
MODULE_FIRMWARE("radeon/hawaii_k_smc.bin");

#define MC_CG_ARB_FREQ_F0           0x0a
#define MC_CG_ARB_FREQ_F1           0x0b
#define MC_CG_ARB_FREQ_F2           0x0c
#define MC_CG_ARB_FREQ_F3           0x0d

#define SMC_RAM_END 0x40000

#define VOLTAGE_SCALE               4
#define VOLTAGE_VID_OFFSET_SCALE1    625
#define VOLTAGE_VID_OFFSET_SCALE2    100

static const struct amd_pm_funcs ci_dpm_funcs;

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

#if 0
static const struct ci_pt_defaults defaults_bonaire_pro =
{
	1, 0xF, 0xFD, 0x19, 5, 45, 0, 0x65062,
	{ 0x8C,  0x23F, 0x244, 0xA6,  0x83,  0x85,  0x86,  0x86,  0x83,  0xDB,  0xDB,  0xDA,  0x67,  0x60,  0x5F  },
	{ 0x187, 0x193, 0x193, 0x1C7, 0x1D1, 0x1D1, 0x210, 0x219, 0x219, 0x266, 0x26C, 0x26C, 0x2C9, 0x2CB, 0x2CB }
};
#endif

static const struct ci_pt_defaults defaults_saturn_xt =
{
	1, 0xF, 0xFD, 0x19, 5, 55, 0, 0x70000,
	{ 0x8C,  0x247, 0x249, 0xA6,  0x80,  0x81,  0x8B,  0x89,  0x86,  0xC9,  0xCA,  0xC9,  0x4D,  0x4D,  0x4D  },
	{ 0x187, 0x187, 0x187, 0x1C7, 0x1C7, 0x1C7, 0x210, 0x210, 0x210, 0x266, 0x266, 0x266, 0x2C9, 0x2C9, 0x2C9 }
};

#if 0
static const struct ci_pt_defaults defaults_saturn_pro =
{
	1, 0xF, 0xFD, 0x19, 5, 55, 0, 0x30000,
	{ 0x96,  0x21D, 0x23B, 0xA1,  0x85,  0x87,  0x83,  0x84,  0x81,  0xE6,  0xE6,  0xE6,  0x71,  0x6A,  0x6A  },
	{ 0x193, 0x19E, 0x19E, 0x1D2, 0x1DC, 0x1DC, 0x21A, 0x223, 0x223, 0x26E, 0x27E, 0x274, 0x2CF, 0x2D2, 0x2D2 }
};
#endif

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

static u8 ci_get_memory_module_index(struct amdgpu_device *adev)
{
	return (u8) ((RREG32(mmBIOS_SCRATCH_4) >> 16) & 0xff);
}

#define MC_CG_ARB_FREQ_F0           0x0a
#define MC_CG_ARB_FREQ_F1           0x0b
#define MC_CG_ARB_FREQ_F2           0x0c
#define MC_CG_ARB_FREQ_F3           0x0d

static int ci_copy_and_switch_arb_sets(struct amdgpu_device *adev,
				       u32 arb_freq_src, u32 arb_freq_dest)
{
	u32 mc_arb_dram_timing;
	u32 mc_arb_dram_timing2;
	u32 burst_time;
	u32 mc_cg_config;

	switch (arb_freq_src) {
	case MC_CG_ARB_FREQ_F0:
		mc_arb_dram_timing  = RREG32(mmMC_ARB_DRAM_TIMING);
		mc_arb_dram_timing2 = RREG32(mmMC_ARB_DRAM_TIMING2);
		burst_time = (RREG32(mmMC_ARB_BURST_TIME) & MC_ARB_BURST_TIME__STATE0_MASK) >>
			 MC_ARB_BURST_TIME__STATE0__SHIFT;
		break;
	case MC_CG_ARB_FREQ_F1:
		mc_arb_dram_timing  = RREG32(mmMC_ARB_DRAM_TIMING_1);
		mc_arb_dram_timing2 = RREG32(mmMC_ARB_DRAM_TIMING2_1);
		burst_time = (RREG32(mmMC_ARB_BURST_TIME) & MC_ARB_BURST_TIME__STATE1_MASK) >>
			 MC_ARB_BURST_TIME__STATE1__SHIFT;
		break;
	default:
		return -EINVAL;
	}

	switch (arb_freq_dest) {
	case MC_CG_ARB_FREQ_F0:
		WREG32(mmMC_ARB_DRAM_TIMING, mc_arb_dram_timing);
		WREG32(mmMC_ARB_DRAM_TIMING2, mc_arb_dram_timing2);
		WREG32_P(mmMC_ARB_BURST_TIME, (burst_time << MC_ARB_BURST_TIME__STATE0__SHIFT),
			~MC_ARB_BURST_TIME__STATE0_MASK);
		break;
	case MC_CG_ARB_FREQ_F1:
		WREG32(mmMC_ARB_DRAM_TIMING_1, mc_arb_dram_timing);
		WREG32(mmMC_ARB_DRAM_TIMING2_1, mc_arb_dram_timing2);
		WREG32_P(mmMC_ARB_BURST_TIME, (burst_time << MC_ARB_BURST_TIME__STATE1__SHIFT),
			~MC_ARB_BURST_TIME__STATE1_MASK);
		break;
	default:
		return -EINVAL;
	}

	mc_cg_config = RREG32(mmMC_CG_CONFIG) | 0x0000000F;
	WREG32(mmMC_CG_CONFIG, mc_cg_config);
	WREG32_P(mmMC_ARB_CG, (arb_freq_dest) << MC_ARB_CG__CG_ARB_REQ__SHIFT,
		~MC_ARB_CG__CG_ARB_REQ_MASK);

	return 0;
}

static u8 ci_get_ddr3_mclk_frequency_ratio(u32 memory_clock)
{
	u8 mc_para_index;

	if (memory_clock < 10000)
		mc_para_index = 0;
	else if (memory_clock >= 80000)
		mc_para_index = 0x0f;
	else
		mc_para_index = (u8)((memory_clock - 10000) / 5000 + 1);
	return mc_para_index;
}

static u8 ci_get_mclk_frequency_ratio(u32 memory_clock, bool strobe_mode)
{
	u8 mc_para_index;

	if (strobe_mode) {
		if (memory_clock < 12500)
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
	return mc_para_index;
}

static void ci_trim_voltage_table_to_fit_state_table(struct amdgpu_device *adev,
						     u32 max_voltage_steps,
						     struct atom_voltage_table *voltage_table)
{
	unsigned int i, diff;

	if (voltage_table->count <= max_voltage_steps)
		return;

	diff = voltage_table->count - max_voltage_steps;

	for (i = 0; i < max_voltage_steps; i++)
		voltage_table->entries[i] = voltage_table->entries[i + diff];

	voltage_table->count = max_voltage_steps;
}

static int ci_get_std_voltage_value_sidd(struct amdgpu_device *adev,
					 struct atom_voltage_table_entry *voltage_table,
					 u16 *std_voltage_hi_sidd, u16 *std_voltage_lo_sidd);
static int ci_set_power_limit(struct amdgpu_device *adev, u32 n);
static int ci_set_overdrive_target_tdp(struct amdgpu_device *adev,
				       u32 target_tdp);
static int ci_update_uvd_dpm(struct amdgpu_device *adev, bool gate);
static void ci_dpm_set_irq_funcs(struct amdgpu_device *adev);

static PPSMC_Result amdgpu_ci_send_msg_to_smc_with_parameter(struct amdgpu_device *adev,
							     PPSMC_Msg msg, u32 parameter);
static void ci_thermal_start_smc_fan_control(struct amdgpu_device *adev);
static void ci_fan_ctrl_set_default_mode(struct amdgpu_device *adev);

static struct ci_power_info *ci_get_pi(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = adev->pm.dpm.priv;

	return pi;
}

static struct ci_ps *ci_get_ps(struct amdgpu_ps *rps)
{
	struct ci_ps *ps = rps->ps_priv;

	return ps;
}

static void ci_initialize_powertune_defaults(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);

	switch (adev->pdev->device) {
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
		if (adev->asic_type == CHIP_HAWAII)
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

static int ci_populate_bapm_vddc_vid_sidd(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	u8 *hi_vid = pi->smc_powertune_table.BapmVddCVidHiSidd;
	u8 *lo_vid = pi->smc_powertune_table.BapmVddCVidLoSidd;
	u8 *hi2_vid = pi->smc_powertune_table.BapmVddCVidHiSidd2;
	u32 i;

	if (adev->pm.dpm.dyn_state.cac_leakage_table.entries == NULL)
		return -EINVAL;
	if (adev->pm.dpm.dyn_state.cac_leakage_table.count > 8)
		return -EINVAL;
	if (adev->pm.dpm.dyn_state.cac_leakage_table.count !=
	    adev->pm.dpm.dyn_state.vddc_dependency_on_sclk.count)
		return -EINVAL;

	for (i = 0; i < adev->pm.dpm.dyn_state.cac_leakage_table.count; i++) {
		if (adev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_EVV) {
			lo_vid[i] = ci_convert_to_vid(adev->pm.dpm.dyn_state.cac_leakage_table.entries[i].vddc1);
			hi_vid[i] = ci_convert_to_vid(adev->pm.dpm.dyn_state.cac_leakage_table.entries[i].vddc2);
			hi2_vid[i] = ci_convert_to_vid(adev->pm.dpm.dyn_state.cac_leakage_table.entries[i].vddc3);
		} else {
			lo_vid[i] = ci_convert_to_vid(adev->pm.dpm.dyn_state.cac_leakage_table.entries[i].vddc);
			hi_vid[i] = ci_convert_to_vid((u16)adev->pm.dpm.dyn_state.cac_leakage_table.entries[i].leakage);
		}
	}
	return 0;
}

static int ci_populate_vddc_vid(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	u8 *vid = pi->smc_powertune_table.VddCVid;
	u32 i;

	if (pi->vddc_voltage_table.count > 8)
		return -EINVAL;

	for (i = 0; i < pi->vddc_voltage_table.count; i++)
		vid[i] = ci_convert_to_vid(pi->vddc_voltage_table.entries[i].value);

	return 0;
}

static int ci_populate_svi_load_line(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	const struct ci_pt_defaults *pt_defaults = pi->powertune_defaults;

	pi->smc_powertune_table.SviLoadLineEn = pt_defaults->svi_load_line_en;
	pi->smc_powertune_table.SviLoadLineVddC = pt_defaults->svi_load_line_vddc;
	pi->smc_powertune_table.SviLoadLineTrimVddC = 3;
	pi->smc_powertune_table.SviLoadLineOffsetVddC = 0;

	return 0;
}

static int ci_populate_tdc_limit(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	const struct ci_pt_defaults *pt_defaults = pi->powertune_defaults;
	u16 tdc_limit;

	tdc_limit = adev->pm.dpm.dyn_state.cac_tdp_table->tdc * 256;
	pi->smc_powertune_table.TDC_VDDC_PkgLimit = cpu_to_be16(tdc_limit);
	pi->smc_powertune_table.TDC_VDDC_ThrottleReleaseLimitPerc =
		pt_defaults->tdc_vddc_throttle_release_limit_perc;
	pi->smc_powertune_table.TDC_MAWt = pt_defaults->tdc_mawt;

	return 0;
}

static int ci_populate_dw8(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	const struct ci_pt_defaults *pt_defaults = pi->powertune_defaults;
	int ret;

	ret = amdgpu_ci_read_smc_sram_dword(adev,
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

static int ci_populate_fuzzy_fan(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);

	if ((adev->pm.dpm.fan.fan_output_sensitivity & (1 << 15)) ||
	    (adev->pm.dpm.fan.fan_output_sensitivity == 0))
		adev->pm.dpm.fan.fan_output_sensitivity =
			adev->pm.dpm.fan.default_fan_output_sensitivity;

	pi->smc_powertune_table.FuzzyFan_PwmSetDelta =
		cpu_to_be16(adev->pm.dpm.fan.fan_output_sensitivity);

	return 0;
}

static int ci_min_max_v_gnbl_pm_lid_from_bapm_vddc(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
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

static int ci_populate_bapm_vddc_base_leakage_sidd(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	u16 hi_sidd = pi->smc_powertune_table.BapmVddCBaseLeakageHiSidd;
	u16 lo_sidd = pi->smc_powertune_table.BapmVddCBaseLeakageLoSidd;
	struct amdgpu_cac_tdp_table *cac_tdp_table =
		adev->pm.dpm.dyn_state.cac_tdp_table;

	hi_sidd = cac_tdp_table->high_cac_leakage / 100 * 256;
	lo_sidd = cac_tdp_table->low_cac_leakage / 100 * 256;

	pi->smc_powertune_table.BapmVddCBaseLeakageHiSidd = cpu_to_be16(hi_sidd);
	pi->smc_powertune_table.BapmVddCBaseLeakageLoSidd = cpu_to_be16(lo_sidd);

	return 0;
}

static int ci_populate_bapm_parameters_in_dpm_table(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	const struct ci_pt_defaults *pt_defaults = pi->powertune_defaults;
	SMU7_Discrete_DpmTable  *dpm_table = &pi->smc_state_table;
	struct amdgpu_cac_tdp_table *cac_tdp_table =
		adev->pm.dpm.dyn_state.cac_tdp_table;
	struct amdgpu_ppm_table *ppm = adev->pm.dpm.dyn_state.ppm_table;
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

static int ci_populate_pm_base(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	u32 pm_fuse_table_offset;
	int ret;

	if (pi->caps_power_containment) {
		ret = amdgpu_ci_read_smc_sram_dword(adev,
					     SMU7_FIRMWARE_HEADER_LOCATION +
					     offsetof(SMU7_Firmware_Header, PmFuseTable),
					     &pm_fuse_table_offset, pi->sram_end);
		if (ret)
			return ret;
		ret = ci_populate_bapm_vddc_vid_sidd(adev);
		if (ret)
			return ret;
		ret = ci_populate_vddc_vid(adev);
		if (ret)
			return ret;
		ret = ci_populate_svi_load_line(adev);
		if (ret)
			return ret;
		ret = ci_populate_tdc_limit(adev);
		if (ret)
			return ret;
		ret = ci_populate_dw8(adev);
		if (ret)
			return ret;
		ret = ci_populate_fuzzy_fan(adev);
		if (ret)
			return ret;
		ret = ci_min_max_v_gnbl_pm_lid_from_bapm_vddc(adev);
		if (ret)
			return ret;
		ret = ci_populate_bapm_vddc_base_leakage_sidd(adev);
		if (ret)
			return ret;
		ret = amdgpu_ci_copy_bytes_to_smc(adev, pm_fuse_table_offset,
					   (u8 *)&pi->smc_powertune_table,
					   sizeof(SMU7_Discrete_PmFuses), pi->sram_end);
		if (ret)
			return ret;
	}

	return 0;
}

static void ci_do_enable_didt(struct amdgpu_device *adev, const bool enable)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	u32 data;

	if (pi->caps_sq_ramping) {
		data = RREG32_DIDT(ixDIDT_SQ_CTRL0);
		if (enable)
			data |= DIDT_SQ_CTRL0__DIDT_CTRL_EN_MASK;
		else
			data &= ~DIDT_SQ_CTRL0__DIDT_CTRL_EN_MASK;
		WREG32_DIDT(ixDIDT_SQ_CTRL0, data);
	}

	if (pi->caps_db_ramping) {
		data = RREG32_DIDT(ixDIDT_DB_CTRL0);
		if (enable)
			data |= DIDT_DB_CTRL0__DIDT_CTRL_EN_MASK;
		else
			data &= ~DIDT_DB_CTRL0__DIDT_CTRL_EN_MASK;
		WREG32_DIDT(ixDIDT_DB_CTRL0, data);
	}

	if (pi->caps_td_ramping) {
		data = RREG32_DIDT(ixDIDT_TD_CTRL0);
		if (enable)
			data |= DIDT_TD_CTRL0__DIDT_CTRL_EN_MASK;
		else
			data &= ~DIDT_TD_CTRL0__DIDT_CTRL_EN_MASK;
		WREG32_DIDT(ixDIDT_TD_CTRL0, data);
	}

	if (pi->caps_tcp_ramping) {
		data = RREG32_DIDT(ixDIDT_TCP_CTRL0);
		if (enable)
			data |= DIDT_TCP_CTRL0__DIDT_CTRL_EN_MASK;
		else
			data &= ~DIDT_TCP_CTRL0__DIDT_CTRL_EN_MASK;
		WREG32_DIDT(ixDIDT_TCP_CTRL0, data);
	}
}

static int ci_program_pt_config_registers(struct amdgpu_device *adev,
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
				data = RREG32(config_regs->offset);
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
				WREG32(config_regs->offset, data);
				break;
			}
			cache = 0;
		}
		config_regs++;
	}
	return 0;
}

static int ci_enable_didt(struct amdgpu_device *adev, bool enable)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	int ret;

	if (pi->caps_sq_ramping || pi->caps_db_ramping ||
	    pi->caps_td_ramping || pi->caps_tcp_ramping) {
		adev->gfx.rlc.funcs->enter_safe_mode(adev);

		if (enable) {
			ret = ci_program_pt_config_registers(adev, didt_config_ci);
			if (ret) {
				adev->gfx.rlc.funcs->exit_safe_mode(adev);
				return ret;
			}
		}

		ci_do_enable_didt(adev, enable);

		adev->gfx.rlc.funcs->exit_safe_mode(adev);
	}

	return 0;
}

static int ci_enable_power_containment(struct amdgpu_device *adev, bool enable)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	PPSMC_Result smc_result;
	int ret = 0;

	if (enable) {
		pi->power_containment_features = 0;
		if (pi->caps_power_containment) {
			if (pi->enable_bapm_feature) {
				smc_result = amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_EnableDTE);
				if (smc_result != PPSMC_Result_OK)
					ret = -EINVAL;
				else
					pi->power_containment_features |= POWERCONTAINMENT_FEATURE_BAPM;
			}

			if (pi->enable_tdc_limit_feature) {
				smc_result = amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_TDCLimitEnable);
				if (smc_result != PPSMC_Result_OK)
					ret = -EINVAL;
				else
					pi->power_containment_features |= POWERCONTAINMENT_FEATURE_TDCLimit;
			}

			if (pi->enable_pkg_pwr_tracking_feature) {
				smc_result = amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_PkgPwrLimitEnable);
				if (smc_result != PPSMC_Result_OK) {
					ret = -EINVAL;
				} else {
					struct amdgpu_cac_tdp_table *cac_tdp_table =
						adev->pm.dpm.dyn_state.cac_tdp_table;
					u32 default_pwr_limit =
						(u32)(cac_tdp_table->maximum_power_delivery_limit * 256);

					pi->power_containment_features |= POWERCONTAINMENT_FEATURE_PkgPwrLimit;

					ci_set_power_limit(adev, default_pwr_limit);
				}
			}
		}
	} else {
		if (pi->caps_power_containment && pi->power_containment_features) {
			if (pi->power_containment_features & POWERCONTAINMENT_FEATURE_TDCLimit)
				amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_TDCLimitDisable);

			if (pi->power_containment_features & POWERCONTAINMENT_FEATURE_BAPM)
				amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_DisableDTE);

			if (pi->power_containment_features & POWERCONTAINMENT_FEATURE_PkgPwrLimit)
				amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_PkgPwrLimitDisable);
			pi->power_containment_features = 0;
		}
	}

	return ret;
}

static int ci_enable_smc_cac(struct amdgpu_device *adev, bool enable)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	PPSMC_Result smc_result;
	int ret = 0;

	if (pi->caps_cac) {
		if (enable) {
			smc_result = amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_EnableCac);
			if (smc_result != PPSMC_Result_OK) {
				ret = -EINVAL;
				pi->cac_enabled = false;
			} else {
				pi->cac_enabled = true;
			}
		} else if (pi->cac_enabled) {
			amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_DisableCac);
			pi->cac_enabled = false;
		}
	}

	return ret;
}

static int ci_enable_thermal_based_sclk_dpm(struct amdgpu_device *adev,
					    bool enable)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	PPSMC_Result smc_result = PPSMC_Result_OK;

	if (pi->thermal_sclk_dpm_enabled) {
		if (enable)
			smc_result = amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_ENABLE_THERMAL_DPM);
		else
			smc_result = amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_DISABLE_THERMAL_DPM);
	}

	if (smc_result == PPSMC_Result_OK)
		return 0;
	else
		return -EINVAL;
}

static int ci_power_control_set_level(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	struct amdgpu_cac_tdp_table *cac_tdp_table =
		adev->pm.dpm.dyn_state.cac_tdp_table;
	s32 adjust_percent;
	s32 target_tdp;
	int ret = 0;
	bool adjust_polarity = false; /* ??? */

	if (pi->caps_power_containment) {
		adjust_percent = adjust_polarity ?
			adev->pm.dpm.tdp_adjustment : (-1 * adev->pm.dpm.tdp_adjustment);
		target_tdp = ((100 + adjust_percent) *
			      (s32)cac_tdp_table->configurable_tdp) / 100;

		ret = ci_set_overdrive_target_tdp(adev, (u32)target_tdp);
	}

	return ret;
}

static void ci_dpm_powergate_uvd(void *handle, bool gate)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct ci_power_info *pi = ci_get_pi(adev);

	pi->uvd_power_gated = gate;

	if (gate) {
		/* stop the UVD block */
		amdgpu_device_ip_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_UVD,
						       AMD_PG_STATE_GATE);
		ci_update_uvd_dpm(adev, gate);
	} else {
		amdgpu_device_ip_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_UVD,
						       AMD_PG_STATE_UNGATE);
		ci_update_uvd_dpm(adev, gate);
	}
}

static bool ci_dpm_vblank_too_short(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 vblank_time = amdgpu_dpm_get_vblank_time(adev);
	u32 switch_limit = adev->gmc.vram_type == AMDGPU_VRAM_TYPE_GDDR5 ? 450 : 300;

	/* disable mclk switching if the refresh is >120Hz, even if the
	 * blanking period would allow it
	 */
	if (amdgpu_dpm_get_vrefresh(adev) > 120)
		return true;

	if (vblank_time < switch_limit)
		return true;
	else
		return false;

}

static void ci_apply_state_adjust_rules(struct amdgpu_device *adev,
					struct amdgpu_ps *rps)
{
	struct ci_ps *ps = ci_get_ps(rps);
	struct ci_power_info *pi = ci_get_pi(adev);
	struct amdgpu_clock_and_voltage_limits *max_limits;
	bool disable_mclk_switching;
	u32 sclk, mclk;
	int i;

	if (rps->vce_active) {
		rps->evclk = adev->pm.dpm.vce_states[adev->pm.dpm.vce_level].evclk;
		rps->ecclk = adev->pm.dpm.vce_states[adev->pm.dpm.vce_level].ecclk;
	} else {
		rps->evclk = 0;
		rps->ecclk = 0;
	}

	if ((adev->pm.dpm.new_active_crtc_count > 1) ||
	    ci_dpm_vblank_too_short(adev))
		disable_mclk_switching = true;
	else
		disable_mclk_switching = false;

	if ((rps->class & ATOM_PPLIB_CLASSIFICATION_UI_MASK) == ATOM_PPLIB_CLASSIFICATION_UI_BATTERY)
		pi->battery_state = true;
	else
		pi->battery_state = false;

	if (adev->pm.dpm.ac_power)
		max_limits = &adev->pm.dpm.dyn_state.max_clock_voltage_on_ac;
	else
		max_limits = &adev->pm.dpm.dyn_state.max_clock_voltage_on_dc;

	if (adev->pm.dpm.ac_power == false) {
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

	if (adev->pm.pm_display_cfg.min_core_set_clock > sclk)
		sclk = adev->pm.pm_display_cfg.min_core_set_clock;

	if (adev->pm.pm_display_cfg.min_mem_set_clock > mclk)
		mclk = adev->pm.pm_display_cfg.min_mem_set_clock;

	if (rps->vce_active) {
		if (sclk < adev->pm.dpm.vce_states[adev->pm.dpm.vce_level].sclk)
			sclk = adev->pm.dpm.vce_states[adev->pm.dpm.vce_level].sclk;
		if (mclk < adev->pm.dpm.vce_states[adev->pm.dpm.vce_level].mclk)
			mclk = adev->pm.dpm.vce_states[adev->pm.dpm.vce_level].mclk;
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

static int ci_thermal_set_temperature_range(struct amdgpu_device *adev,
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

	tmp = RREG32_SMC(ixCG_THERMAL_INT);
	tmp &= ~(CG_THERMAL_INT__DIG_THERM_INTH_MASK | CG_THERMAL_INT__DIG_THERM_INTL_MASK);
	tmp |= ((high_temp / 1000) << CG_THERMAL_INT__DIG_THERM_INTH__SHIFT) |
		((low_temp / 1000)) << CG_THERMAL_INT__DIG_THERM_INTL__SHIFT;
	WREG32_SMC(ixCG_THERMAL_INT, tmp);

#if 0
	/* XXX: need to figure out how to handle this properly */
	tmp = RREG32_SMC(ixCG_THERMAL_CTRL);
	tmp &= DIG_THERM_DPM_MASK;
	tmp |= DIG_THERM_DPM(high_temp / 1000);
	WREG32_SMC(ixCG_THERMAL_CTRL, tmp);
#endif

	adev->pm.dpm.thermal.min_temp = low_temp;
	adev->pm.dpm.thermal.max_temp = high_temp;
	return 0;
}

static int ci_thermal_enable_alert(struct amdgpu_device *adev,
				   bool enable)
{
	u32 thermal_int = RREG32_SMC(ixCG_THERMAL_INT);
	PPSMC_Result result;

	if (enable) {
		thermal_int &= ~(CG_THERMAL_INT_CTRL__THERM_INTH_MASK_MASK |
				 CG_THERMAL_INT_CTRL__THERM_INTL_MASK_MASK);
		WREG32_SMC(ixCG_THERMAL_INT, thermal_int);
		result = amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_Thermal_Cntl_Enable);
		if (result != PPSMC_Result_OK) {
			DRM_DEBUG_KMS("Could not enable thermal interrupts.\n");
			return -EINVAL;
		}
	} else {
		thermal_int |= CG_THERMAL_INT_CTRL__THERM_INTH_MASK_MASK |
			CG_THERMAL_INT_CTRL__THERM_INTL_MASK_MASK;
		WREG32_SMC(ixCG_THERMAL_INT, thermal_int);
		result = amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_Thermal_Cntl_Disable);
		if (result != PPSMC_Result_OK) {
			DRM_DEBUG_KMS("Could not disable thermal interrupts.\n");
			return -EINVAL;
		}
	}

	return 0;
}

static void ci_fan_ctrl_set_static_mode(struct amdgpu_device *adev, u32 mode)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	u32 tmp;

	if (pi->fan_ctrl_is_in_default_mode) {
		tmp = (RREG32_SMC(ixCG_FDO_CTRL2) & CG_FDO_CTRL2__FDO_PWM_MODE_MASK)
			>> CG_FDO_CTRL2__FDO_PWM_MODE__SHIFT;
		pi->fan_ctrl_default_mode = tmp;
		tmp = (RREG32_SMC(ixCG_FDO_CTRL2) & CG_FDO_CTRL2__TMIN_MASK)
			>> CG_FDO_CTRL2__TMIN__SHIFT;
		pi->t_min = tmp;
		pi->fan_ctrl_is_in_default_mode = false;
	}

	tmp = RREG32_SMC(ixCG_FDO_CTRL2) & ~CG_FDO_CTRL2__TMIN_MASK;
	tmp |= 0 << CG_FDO_CTRL2__TMIN__SHIFT;
	WREG32_SMC(ixCG_FDO_CTRL2, tmp);

	tmp = RREG32_SMC(ixCG_FDO_CTRL2) & ~CG_FDO_CTRL2__FDO_PWM_MODE_MASK;
	tmp |= mode << CG_FDO_CTRL2__FDO_PWM_MODE__SHIFT;
	WREG32_SMC(ixCG_FDO_CTRL2, tmp);
}

static int ci_thermal_setup_fan_table(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	SMU7_Discrete_FanTable fan_table = { FDO_MODE_HARDWARE };
	u32 duty100;
	u32 t_diff1, t_diff2, pwm_diff1, pwm_diff2;
	u16 fdo_min, slope1, slope2;
	u32 reference_clock, tmp;
	int ret;
	u64 tmp64;

	if (!pi->fan_table_start) {
		adev->pm.dpm.fan.ucode_fan_control = false;
		return 0;
	}

	duty100 = (RREG32_SMC(ixCG_FDO_CTRL1) & CG_FDO_CTRL1__FMAX_DUTY100_MASK)
		>> CG_FDO_CTRL1__FMAX_DUTY100__SHIFT;

	if (duty100 == 0) {
		adev->pm.dpm.fan.ucode_fan_control = false;
		return 0;
	}

	tmp64 = (u64)adev->pm.dpm.fan.pwm_min * duty100;
	do_div(tmp64, 10000);
	fdo_min = (u16)tmp64;

	t_diff1 = adev->pm.dpm.fan.t_med - adev->pm.dpm.fan.t_min;
	t_diff2 = adev->pm.dpm.fan.t_high - adev->pm.dpm.fan.t_med;

	pwm_diff1 = adev->pm.dpm.fan.pwm_med - adev->pm.dpm.fan.pwm_min;
	pwm_diff2 = adev->pm.dpm.fan.pwm_high - adev->pm.dpm.fan.pwm_med;

	slope1 = (u16)((50 + ((16 * duty100 * pwm_diff1) / t_diff1)) / 100);
	slope2 = (u16)((50 + ((16 * duty100 * pwm_diff2) / t_diff2)) / 100);

	fan_table.TempMin = cpu_to_be16((50 + adev->pm.dpm.fan.t_min) / 100);
	fan_table.TempMed = cpu_to_be16((50 + adev->pm.dpm.fan.t_med) / 100);
	fan_table.TempMax = cpu_to_be16((50 + adev->pm.dpm.fan.t_max) / 100);

	fan_table.Slope1 = cpu_to_be16(slope1);
	fan_table.Slope2 = cpu_to_be16(slope2);

	fan_table.FdoMin = cpu_to_be16(fdo_min);

	fan_table.HystDown = cpu_to_be16(adev->pm.dpm.fan.t_hyst);

	fan_table.HystUp = cpu_to_be16(1);

	fan_table.HystSlope = cpu_to_be16(1);

	fan_table.TempRespLim = cpu_to_be16(5);

	reference_clock = amdgpu_asic_get_xclk(adev);

	fan_table.RefreshPeriod = cpu_to_be32((adev->pm.dpm.fan.cycle_delay *
					       reference_clock) / 1600);

	fan_table.FdoMax = cpu_to_be16((u16)duty100);

	tmp = (RREG32_SMC(ixCG_MULT_THERMAL_CTRL) & CG_MULT_THERMAL_CTRL__TEMP_SEL_MASK)
		>> CG_MULT_THERMAL_CTRL__TEMP_SEL__SHIFT;
	fan_table.TempSrc = (uint8_t)tmp;

	ret = amdgpu_ci_copy_bytes_to_smc(adev,
					  pi->fan_table_start,
					  (u8 *)(&fan_table),
					  sizeof(fan_table),
					  pi->sram_end);

	if (ret) {
		DRM_ERROR("Failed to load fan table to the SMC.");
		adev->pm.dpm.fan.ucode_fan_control = false;
	}

	return 0;
}

static int ci_fan_ctrl_start_smc_fan_control(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	PPSMC_Result ret;

	if (pi->caps_od_fuzzy_fan_control_support) {
		ret = amdgpu_ci_send_msg_to_smc_with_parameter(adev,
							       PPSMC_StartFanControl,
							       FAN_CONTROL_FUZZY);
		if (ret != PPSMC_Result_OK)
			return -EINVAL;
		ret = amdgpu_ci_send_msg_to_smc_with_parameter(adev,
							       PPSMC_MSG_SetFanPwmMax,
							       adev->pm.dpm.fan.default_max_fan_pwm);
		if (ret != PPSMC_Result_OK)
			return -EINVAL;
	} else {
		ret = amdgpu_ci_send_msg_to_smc_with_parameter(adev,
							       PPSMC_StartFanControl,
							       FAN_CONTROL_TABLE);
		if (ret != PPSMC_Result_OK)
			return -EINVAL;
	}

	pi->fan_is_controlled_by_smc = true;
	return 0;
}


static int ci_fan_ctrl_stop_smc_fan_control(struct amdgpu_device *adev)
{
	PPSMC_Result ret;
	struct ci_power_info *pi = ci_get_pi(adev);

	ret = amdgpu_ci_send_msg_to_smc(adev, PPSMC_StopFanControl);
	if (ret == PPSMC_Result_OK) {
		pi->fan_is_controlled_by_smc = false;
		return 0;
	} else {
		return -EINVAL;
	}
}

static int ci_dpm_get_fan_speed_percent(void *handle,
					u32 *speed)
{
	u32 duty, duty100;
	u64 tmp64;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->pm.no_fan)
		return -ENOENT;

	duty100 = (RREG32_SMC(ixCG_FDO_CTRL1) & CG_FDO_CTRL1__FMAX_DUTY100_MASK)
		>> CG_FDO_CTRL1__FMAX_DUTY100__SHIFT;
	duty = (RREG32_SMC(ixCG_THERMAL_STATUS) & CG_THERMAL_STATUS__FDO_PWM_DUTY_MASK)
		>> CG_THERMAL_STATUS__FDO_PWM_DUTY__SHIFT;

	if (duty100 == 0)
		return -EINVAL;

	tmp64 = (u64)duty * 100;
	do_div(tmp64, duty100);
	*speed = (u32)tmp64;

	if (*speed > 100)
		*speed = 100;

	return 0;
}

static int ci_dpm_set_fan_speed_percent(void *handle,
					u32 speed)
{
	u32 tmp;
	u32 duty, duty100;
	u64 tmp64;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct ci_power_info *pi = ci_get_pi(adev);

	if (adev->pm.no_fan)
		return -ENOENT;

	if (pi->fan_is_controlled_by_smc)
		return -EINVAL;

	if (speed > 100)
		return -EINVAL;

	duty100 = (RREG32_SMC(ixCG_FDO_CTRL1) & CG_FDO_CTRL1__FMAX_DUTY100_MASK)
		>> CG_FDO_CTRL1__FMAX_DUTY100__SHIFT;

	if (duty100 == 0)
		return -EINVAL;

	tmp64 = (u64)speed * duty100;
	do_div(tmp64, 100);
	duty = (u32)tmp64;

	tmp = RREG32_SMC(ixCG_FDO_CTRL0) & ~CG_FDO_CTRL0__FDO_STATIC_DUTY_MASK;
	tmp |= duty << CG_FDO_CTRL0__FDO_STATIC_DUTY__SHIFT;
	WREG32_SMC(ixCG_FDO_CTRL0, tmp);

	return 0;
}

static void ci_dpm_set_fan_control_mode(void *handle, u32 mode)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	switch (mode) {
	case AMD_FAN_CTRL_NONE:
		if (adev->pm.dpm.fan.ucode_fan_control)
			ci_fan_ctrl_stop_smc_fan_control(adev);
		ci_dpm_set_fan_speed_percent(adev, 100);
		break;
	case AMD_FAN_CTRL_MANUAL:
		if (adev->pm.dpm.fan.ucode_fan_control)
			ci_fan_ctrl_stop_smc_fan_control(adev);
		break;
	case AMD_FAN_CTRL_AUTO:
		if (adev->pm.dpm.fan.ucode_fan_control)
			ci_thermal_start_smc_fan_control(adev);
		break;
	default:
		break;
	}
}

static u32 ci_dpm_get_fan_control_mode(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct ci_power_info *pi = ci_get_pi(adev);

	if (pi->fan_is_controlled_by_smc)
		return AMD_FAN_CTRL_AUTO;
	else
		return AMD_FAN_CTRL_MANUAL;
}

#if 0
static int ci_fan_ctrl_get_fan_speed_rpm(struct amdgpu_device *adev,
					 u32 *speed)
{
	u32 tach_period;
	u32 xclk = amdgpu_asic_get_xclk(adev);

	if (adev->pm.no_fan)
		return -ENOENT;

	if (adev->pm.fan_pulses_per_revolution == 0)
		return -ENOENT;

	tach_period = (RREG32_SMC(ixCG_TACH_STATUS) & CG_TACH_STATUS__TACH_PERIOD_MASK)
		>> CG_TACH_STATUS__TACH_PERIOD__SHIFT;
	if (tach_period == 0)
		return -ENOENT;

	*speed = 60 * xclk * 10000 / tach_period;

	return 0;
}

static int ci_fan_ctrl_set_fan_speed_rpm(struct amdgpu_device *adev,
					 u32 speed)
{
	u32 tach_period, tmp;
	u32 xclk = amdgpu_asic_get_xclk(adev);

	if (adev->pm.no_fan)
		return -ENOENT;

	if (adev->pm.fan_pulses_per_revolution == 0)
		return -ENOENT;

	if ((speed < adev->pm.fan_min_rpm) ||
	    (speed > adev->pm.fan_max_rpm))
		return -EINVAL;

	if (adev->pm.dpm.fan.ucode_fan_control)
		ci_fan_ctrl_stop_smc_fan_control(adev);

	tach_period = 60 * xclk * 10000 / (8 * speed);
	tmp = RREG32_SMC(ixCG_TACH_CTRL) & ~CG_TACH_CTRL__TARGET_PERIOD_MASK;
	tmp |= tach_period << CG_TACH_CTRL__TARGET_PERIOD__SHIFT;
	WREG32_SMC(CG_TACH_CTRL, tmp);

	ci_fan_ctrl_set_static_mode(adev, FDO_PWM_MODE_STATIC_RPM);

	return 0;
}
#endif

static void ci_fan_ctrl_set_default_mode(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	u32 tmp;

	if (!pi->fan_ctrl_is_in_default_mode) {
		tmp = RREG32_SMC(ixCG_FDO_CTRL2) & ~CG_FDO_CTRL2__FDO_PWM_MODE_MASK;
		tmp |= pi->fan_ctrl_default_mode << CG_FDO_CTRL2__FDO_PWM_MODE__SHIFT;
		WREG32_SMC(ixCG_FDO_CTRL2, tmp);

		tmp = RREG32_SMC(ixCG_FDO_CTRL2) & ~CG_FDO_CTRL2__TMIN_MASK;
		tmp |= pi->t_min << CG_FDO_CTRL2__TMIN__SHIFT;
		WREG32_SMC(ixCG_FDO_CTRL2, tmp);
		pi->fan_ctrl_is_in_default_mode = true;
	}
}

static void ci_thermal_start_smc_fan_control(struct amdgpu_device *adev)
{
	if (adev->pm.dpm.fan.ucode_fan_control) {
		ci_fan_ctrl_start_smc_fan_control(adev);
		ci_fan_ctrl_set_static_mode(adev, FDO_PWM_MODE_STATIC);
	}
}

static void ci_thermal_initialize(struct amdgpu_device *adev)
{
	u32 tmp;

	if (adev->pm.fan_pulses_per_revolution) {
		tmp = RREG32_SMC(ixCG_TACH_CTRL) & ~CG_TACH_CTRL__EDGE_PER_REV_MASK;
		tmp |= (adev->pm.fan_pulses_per_revolution - 1)
			<< CG_TACH_CTRL__EDGE_PER_REV__SHIFT;
		WREG32_SMC(ixCG_TACH_CTRL, tmp);
	}

	tmp = RREG32_SMC(ixCG_FDO_CTRL2) & ~CG_FDO_CTRL2__TACH_PWM_RESP_RATE_MASK;
	tmp |= 0x28 << CG_FDO_CTRL2__TACH_PWM_RESP_RATE__SHIFT;
	WREG32_SMC(ixCG_FDO_CTRL2, tmp);
}

static int ci_thermal_start_thermal_controller(struct amdgpu_device *adev)
{
	int ret;

	ci_thermal_initialize(adev);
	ret = ci_thermal_set_temperature_range(adev, CISLANDS_TEMP_RANGE_MIN, CISLANDS_TEMP_RANGE_MAX);
	if (ret)
		return ret;
	ret = ci_thermal_enable_alert(adev, true);
	if (ret)
		return ret;
	if (adev->pm.dpm.fan.ucode_fan_control) {
		ret = ci_thermal_setup_fan_table(adev);
		if (ret)
			return ret;
		ci_thermal_start_smc_fan_control(adev);
	}

	return 0;
}

static void ci_thermal_stop_thermal_controller(struct amdgpu_device *adev)
{
	if (!adev->pm.no_fan)
		ci_fan_ctrl_set_default_mode(adev);
}

static int ci_read_smc_soft_register(struct amdgpu_device *adev,
				     u16 reg_offset, u32 *value)
{
	struct ci_power_info *pi = ci_get_pi(adev);

	return amdgpu_ci_read_smc_sram_dword(adev,
				      pi->soft_regs_start + reg_offset,
				      value, pi->sram_end);
}

static int ci_write_smc_soft_register(struct amdgpu_device *adev,
				      u16 reg_offset, u32 value)
{
	struct ci_power_info *pi = ci_get_pi(adev);

	return amdgpu_ci_write_smc_sram_dword(adev,
				       pi->soft_regs_start + reg_offset,
				       value, pi->sram_end);
}

static void ci_init_fps_limits(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	SMU7_Discrete_DpmTable *table = &pi->smc_state_table;

	if (pi->caps_fps) {
		u16 tmp;

		tmp = 45;
		table->FpsHighT = cpu_to_be16(tmp);

		tmp = 30;
		table->FpsLowT = cpu_to_be16(tmp);
	}
}

static int ci_update_sclk_t(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	int ret = 0;
	u32 low_sclk_interrupt_t = 0;

	if (pi->caps_sclk_throttle_low_notification) {
		low_sclk_interrupt_t = cpu_to_be32(pi->low_sclk_interrupt_t);

		ret = amdgpu_ci_copy_bytes_to_smc(adev,
					   pi->dpm_table_start +
					   offsetof(SMU7_Discrete_DpmTable, LowSclkInterruptT),
					   (u8 *)&low_sclk_interrupt_t,
					   sizeof(u32), pi->sram_end);

	}

	return ret;
}

static void ci_get_leakage_voltages(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	u16 leakage_id, virtual_voltage_id;
	u16 vddc, vddci;
	int i;

	pi->vddc_leakage.count = 0;
	pi->vddci_leakage.count = 0;

	if (adev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_EVV) {
		for (i = 0; i < CISLANDS_MAX_LEAKAGE_COUNT; i++) {
			virtual_voltage_id = ATOM_VIRTUAL_VOLTAGE_ID0 + i;
			if (amdgpu_atombios_get_voltage_evv(adev, virtual_voltage_id, &vddc) != 0)
				continue;
			if (vddc != 0 && vddc != virtual_voltage_id) {
				pi->vddc_leakage.actual_voltage[pi->vddc_leakage.count] = vddc;
				pi->vddc_leakage.leakage_id[pi->vddc_leakage.count] = virtual_voltage_id;
				pi->vddc_leakage.count++;
			}
		}
	} else if (amdgpu_atombios_get_leakage_id_from_vbios(adev, &leakage_id) == 0) {
		for (i = 0; i < CISLANDS_MAX_LEAKAGE_COUNT; i++) {
			virtual_voltage_id = ATOM_VIRTUAL_VOLTAGE_ID0 + i;
			if (amdgpu_atombios_get_leakage_vddc_based_on_leakage_params(adev, &vddc, &vddci,
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

static void ci_set_dpm_event_sources(struct amdgpu_device *adev, u32 sources)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	bool want_thermal_protection;
	enum amdgpu_dpm_event_src dpm_event_src;
	u32 tmp;

	switch (sources) {
	case 0:
	default:
		want_thermal_protection = false;
		break;
	case (1 << AMDGPU_DPM_AUTO_THROTTLE_SRC_THERMAL):
		want_thermal_protection = true;
		dpm_event_src = AMDGPU_DPM_EVENT_SRC_DIGITAL;
		break;
	case (1 << AMDGPU_DPM_AUTO_THROTTLE_SRC_EXTERNAL):
		want_thermal_protection = true;
		dpm_event_src = AMDGPU_DPM_EVENT_SRC_EXTERNAL;
		break;
	case ((1 << AMDGPU_DPM_AUTO_THROTTLE_SRC_EXTERNAL) |
	      (1 << AMDGPU_DPM_AUTO_THROTTLE_SRC_THERMAL)):
		want_thermal_protection = true;
		dpm_event_src = AMDGPU_DPM_EVENT_SRC_DIGIAL_OR_EXTERNAL;
		break;
	}

	if (want_thermal_protection) {
#if 0
		/* XXX: need to figure out how to handle this properly */
		tmp = RREG32_SMC(ixCG_THERMAL_CTRL);
		tmp &= DPM_EVENT_SRC_MASK;
		tmp |= DPM_EVENT_SRC(dpm_event_src);
		WREG32_SMC(ixCG_THERMAL_CTRL, tmp);
#endif

		tmp = RREG32_SMC(ixGENERAL_PWRMGT);
		if (pi->thermal_protection)
			tmp &= ~GENERAL_PWRMGT__THERMAL_PROTECTION_DIS_MASK;
		else
			tmp |= GENERAL_PWRMGT__THERMAL_PROTECTION_DIS_MASK;
		WREG32_SMC(ixGENERAL_PWRMGT, tmp);
	} else {
		tmp = RREG32_SMC(ixGENERAL_PWRMGT);
		tmp |= GENERAL_PWRMGT__THERMAL_PROTECTION_DIS_MASK;
		WREG32_SMC(ixGENERAL_PWRMGT, tmp);
	}
}

static void ci_enable_auto_throttle_source(struct amdgpu_device *adev,
					   enum amdgpu_dpm_auto_throttle_src source,
					   bool enable)
{
	struct ci_power_info *pi = ci_get_pi(adev);

	if (enable) {
		if (!(pi->active_auto_throttle_sources & (1 << source))) {
			pi->active_auto_throttle_sources |= 1 << source;
			ci_set_dpm_event_sources(adev, pi->active_auto_throttle_sources);
		}
	} else {
		if (pi->active_auto_throttle_sources & (1 << source)) {
			pi->active_auto_throttle_sources &= ~(1 << source);
			ci_set_dpm_event_sources(adev, pi->active_auto_throttle_sources);
		}
	}
}

static void ci_enable_vr_hot_gpio_interrupt(struct amdgpu_device *adev)
{
	if (adev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_REGULATOR_HOT)
		amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_EnableVRHotGPIOInterrupt);
}

static int ci_unfreeze_sclk_mclk_dpm(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	PPSMC_Result smc_result;

	if (!pi->need_update_smu7_dpm_table)
		return 0;

	if ((!pi->sclk_dpm_key_disabled) &&
	    (pi->need_update_smu7_dpm_table & (DPMTABLE_OD_UPDATE_SCLK | DPMTABLE_UPDATE_SCLK))) {
		smc_result = amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_SCLKDPM_UnfreezeLevel);
		if (smc_result != PPSMC_Result_OK)
			return -EINVAL;
	}

	if ((!pi->mclk_dpm_key_disabled) &&
	    (pi->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_MCLK)) {
		smc_result = amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_MCLKDPM_UnfreezeLevel);
		if (smc_result != PPSMC_Result_OK)
			return -EINVAL;
	}

	pi->need_update_smu7_dpm_table = 0;
	return 0;
}

static int ci_enable_sclk_mclk_dpm(struct amdgpu_device *adev, bool enable)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	PPSMC_Result smc_result;

	if (enable) {
		if (!pi->sclk_dpm_key_disabled) {
			smc_result = amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_DPM_Enable);
			if (smc_result != PPSMC_Result_OK)
				return -EINVAL;
		}

		if (!pi->mclk_dpm_key_disabled) {
			smc_result = amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_MCLKDPM_Enable);
			if (smc_result != PPSMC_Result_OK)
				return -EINVAL;

			WREG32_P(mmMC_SEQ_CNTL_3, MC_SEQ_CNTL_3__CAC_EN_MASK,
					~MC_SEQ_CNTL_3__CAC_EN_MASK);

			WREG32_SMC(ixLCAC_MC0_CNTL, 0x05);
			WREG32_SMC(ixLCAC_MC1_CNTL, 0x05);
			WREG32_SMC(ixLCAC_CPL_CNTL, 0x100005);

			udelay(10);

			WREG32_SMC(ixLCAC_MC0_CNTL, 0x400005);
			WREG32_SMC(ixLCAC_MC1_CNTL, 0x400005);
			WREG32_SMC(ixLCAC_CPL_CNTL, 0x500005);
		}
	} else {
		if (!pi->sclk_dpm_key_disabled) {
			smc_result = amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_DPM_Disable);
			if (smc_result != PPSMC_Result_OK)
				return -EINVAL;
		}

		if (!pi->mclk_dpm_key_disabled) {
			smc_result = amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_MCLKDPM_Disable);
			if (smc_result != PPSMC_Result_OK)
				return -EINVAL;
		}
	}

	return 0;
}

static int ci_start_dpm(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	PPSMC_Result smc_result;
	int ret;
	u32 tmp;

	tmp = RREG32_SMC(ixGENERAL_PWRMGT);
	tmp |= GENERAL_PWRMGT__GLOBAL_PWRMGT_EN_MASK;
	WREG32_SMC(ixGENERAL_PWRMGT, tmp);

	tmp = RREG32_SMC(ixSCLK_PWRMGT_CNTL);
	tmp |= SCLK_PWRMGT_CNTL__DYNAMIC_PM_EN_MASK;
	WREG32_SMC(ixSCLK_PWRMGT_CNTL, tmp);

	ci_write_smc_soft_register(adev, offsetof(SMU7_SoftRegisters, VoltageChangeTimeout), 0x1000);

	WREG32_P(mmBIF_LNCNT_RESET, 0, ~BIF_LNCNT_RESET__RESET_LNCNT_EN_MASK);

	smc_result = amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_Voltage_Cntl_Enable);
	if (smc_result != PPSMC_Result_OK)
		return -EINVAL;

	ret = ci_enable_sclk_mclk_dpm(adev, true);
	if (ret)
		return ret;

	if (!pi->pcie_dpm_key_disabled) {
		smc_result = amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_PCIeDPM_Enable);
		if (smc_result != PPSMC_Result_OK)
			return -EINVAL;
	}

	return 0;
}

static int ci_freeze_sclk_mclk_dpm(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	PPSMC_Result smc_result;

	if (!pi->need_update_smu7_dpm_table)
		return 0;

	if ((!pi->sclk_dpm_key_disabled) &&
	    (pi->need_update_smu7_dpm_table & (DPMTABLE_OD_UPDATE_SCLK | DPMTABLE_UPDATE_SCLK))) {
		smc_result = amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_SCLKDPM_FreezeLevel);
		if (smc_result != PPSMC_Result_OK)
			return -EINVAL;
	}

	if ((!pi->mclk_dpm_key_disabled) &&
	    (pi->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_MCLK)) {
		smc_result = amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_MCLKDPM_FreezeLevel);
		if (smc_result != PPSMC_Result_OK)
			return -EINVAL;
	}

	return 0;
}

static int ci_stop_dpm(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	PPSMC_Result smc_result;
	int ret;
	u32 tmp;

	tmp = RREG32_SMC(ixGENERAL_PWRMGT);
	tmp &= ~GENERAL_PWRMGT__GLOBAL_PWRMGT_EN_MASK;
	WREG32_SMC(ixGENERAL_PWRMGT, tmp);

	tmp = RREG32_SMC(ixSCLK_PWRMGT_CNTL);
	tmp &= ~SCLK_PWRMGT_CNTL__DYNAMIC_PM_EN_MASK;
	WREG32_SMC(ixSCLK_PWRMGT_CNTL, tmp);

	if (!pi->pcie_dpm_key_disabled) {
		smc_result = amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_PCIeDPM_Disable);
		if (smc_result != PPSMC_Result_OK)
			return -EINVAL;
	}

	ret = ci_enable_sclk_mclk_dpm(adev, false);
	if (ret)
		return ret;

	smc_result = amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_Voltage_Cntl_Disable);
	if (smc_result != PPSMC_Result_OK)
		return -EINVAL;

	return 0;
}

static void ci_enable_sclk_control(struct amdgpu_device *adev, bool enable)
{
	u32 tmp = RREG32_SMC(ixSCLK_PWRMGT_CNTL);

	if (enable)
		tmp &= ~SCLK_PWRMGT_CNTL__SCLK_PWRMGT_OFF_MASK;
	else
		tmp |= SCLK_PWRMGT_CNTL__SCLK_PWRMGT_OFF_MASK;
	WREG32_SMC(ixSCLK_PWRMGT_CNTL, tmp);
}

#if 0
static int ci_notify_hw_of_power_source(struct amdgpu_device *adev,
					bool ac_power)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	struct amdgpu_cac_tdp_table *cac_tdp_table =
		adev->pm.dpm.dyn_state.cac_tdp_table;
	u32 power_limit;

	if (ac_power)
		power_limit = (u32)(cac_tdp_table->maximum_power_delivery_limit * 256);
	else
		power_limit = (u32)(cac_tdp_table->battery_power_limit * 256);

	ci_set_power_limit(adev, power_limit);

	if (pi->caps_automatic_dc_transition) {
		if (ac_power)
			amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_RunningOnAC);
		else
			amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_Remove_DC_Clamp);
	}

	return 0;
}
#endif

static PPSMC_Result amdgpu_ci_send_msg_to_smc_with_parameter(struct amdgpu_device *adev,
						      PPSMC_Msg msg, u32 parameter)
{
	WREG32(mmSMC_MSG_ARG_0, parameter);
	return amdgpu_ci_send_msg_to_smc(adev, msg);
}

static PPSMC_Result amdgpu_ci_send_msg_to_smc_return_parameter(struct amdgpu_device *adev,
							PPSMC_Msg msg, u32 *parameter)
{
	PPSMC_Result smc_result;

	smc_result = amdgpu_ci_send_msg_to_smc(adev, msg);

	if ((smc_result == PPSMC_Result_OK) && parameter)
		*parameter = RREG32(mmSMC_MSG_ARG_0);

	return smc_result;
}

static int ci_dpm_force_state_sclk(struct amdgpu_device *adev, u32 n)
{
	struct ci_power_info *pi = ci_get_pi(adev);

	if (!pi->sclk_dpm_key_disabled) {
		PPSMC_Result smc_result =
			amdgpu_ci_send_msg_to_smc_with_parameter(adev, PPSMC_MSG_SCLKDPM_SetEnabledMask, 1 << n);
		if (smc_result != PPSMC_Result_OK)
			return -EINVAL;
	}

	return 0;
}

static int ci_dpm_force_state_mclk(struct amdgpu_device *adev, u32 n)
{
	struct ci_power_info *pi = ci_get_pi(adev);

	if (!pi->mclk_dpm_key_disabled) {
		PPSMC_Result smc_result =
			amdgpu_ci_send_msg_to_smc_with_parameter(adev, PPSMC_MSG_MCLKDPM_SetEnabledMask, 1 << n);
		if (smc_result != PPSMC_Result_OK)
			return -EINVAL;
	}

	return 0;
}

static int ci_dpm_force_state_pcie(struct amdgpu_device *adev, u32 n)
{
	struct ci_power_info *pi = ci_get_pi(adev);

	if (!pi->pcie_dpm_key_disabled) {
		PPSMC_Result smc_result =
			amdgpu_ci_send_msg_to_smc_with_parameter(adev, PPSMC_MSG_PCIeDPM_ForceLevel, n);
		if (smc_result != PPSMC_Result_OK)
			return -EINVAL;
	}

	return 0;
}

static int ci_set_power_limit(struct amdgpu_device *adev, u32 n)
{
	struct ci_power_info *pi = ci_get_pi(adev);

	if (pi->power_containment_features & POWERCONTAINMENT_FEATURE_PkgPwrLimit) {
		PPSMC_Result smc_result =
			amdgpu_ci_send_msg_to_smc_with_parameter(adev, PPSMC_MSG_PkgPwrSetLimit, n);
		if (smc_result != PPSMC_Result_OK)
			return -EINVAL;
	}

	return 0;
}

static int ci_set_overdrive_target_tdp(struct amdgpu_device *adev,
				       u32 target_tdp)
{
	PPSMC_Result smc_result =
		amdgpu_ci_send_msg_to_smc_with_parameter(adev, PPSMC_MSG_OverDriveSetTargetTdp, target_tdp);
	if (smc_result != PPSMC_Result_OK)
		return -EINVAL;
	return 0;
}

#if 0
static int ci_set_boot_state(struct amdgpu_device *adev)
{
	return ci_enable_sclk_mclk_dpm(adev, false);
}
#endif

static u32 ci_get_average_sclk_freq(struct amdgpu_device *adev)
{
	u32 sclk_freq;
	PPSMC_Result smc_result =
		amdgpu_ci_send_msg_to_smc_return_parameter(adev,
						    PPSMC_MSG_API_GetSclkFrequency,
						    &sclk_freq);
	if (smc_result != PPSMC_Result_OK)
		sclk_freq = 0;

	return sclk_freq;
}

static u32 ci_get_average_mclk_freq(struct amdgpu_device *adev)
{
	u32 mclk_freq;
	PPSMC_Result smc_result =
		amdgpu_ci_send_msg_to_smc_return_parameter(adev,
						    PPSMC_MSG_API_GetMclkFrequency,
						    &mclk_freq);
	if (smc_result != PPSMC_Result_OK)
		mclk_freq = 0;

	return mclk_freq;
}

static void ci_dpm_start_smc(struct amdgpu_device *adev)
{
	int i;

	amdgpu_ci_program_jump_on_start(adev);
	amdgpu_ci_start_smc_clock(adev);
	amdgpu_ci_start_smc(adev);
	for (i = 0; i < adev->usec_timeout; i++) {
		if (RREG32_SMC(ixFIRMWARE_FLAGS) & FIRMWARE_FLAGS__INTERRUPTS_ENABLED_MASK)
			break;
	}
}

static void ci_dpm_stop_smc(struct amdgpu_device *adev)
{
	amdgpu_ci_reset_smc(adev);
	amdgpu_ci_stop_smc_clock(adev);
}

static int ci_process_firmware_header(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	u32 tmp;
	int ret;

	ret = amdgpu_ci_read_smc_sram_dword(adev,
				     SMU7_FIRMWARE_HEADER_LOCATION +
				     offsetof(SMU7_Firmware_Header, DpmTable),
				     &tmp, pi->sram_end);
	if (ret)
		return ret;

	pi->dpm_table_start = tmp;

	ret = amdgpu_ci_read_smc_sram_dword(adev,
				     SMU7_FIRMWARE_HEADER_LOCATION +
				     offsetof(SMU7_Firmware_Header, SoftRegisters),
				     &tmp, pi->sram_end);
	if (ret)
		return ret;

	pi->soft_regs_start = tmp;

	ret = amdgpu_ci_read_smc_sram_dword(adev,
				     SMU7_FIRMWARE_HEADER_LOCATION +
				     offsetof(SMU7_Firmware_Header, mcRegisterTable),
				     &tmp, pi->sram_end);
	if (ret)
		return ret;

	pi->mc_reg_table_start = tmp;

	ret = amdgpu_ci_read_smc_sram_dword(adev,
				     SMU7_FIRMWARE_HEADER_LOCATION +
				     offsetof(SMU7_Firmware_Header, FanTable),
				     &tmp, pi->sram_end);
	if (ret)
		return ret;

	pi->fan_table_start = tmp;

	ret = amdgpu_ci_read_smc_sram_dword(adev,
				     SMU7_FIRMWARE_HEADER_LOCATION +
				     offsetof(SMU7_Firmware_Header, mcArbDramTimingTable),
				     &tmp, pi->sram_end);
	if (ret)
		return ret;

	pi->arb_table_start = tmp;

	return 0;
}

static void ci_read_clock_registers(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);

	pi->clock_registers.cg_spll_func_cntl =
		RREG32_SMC(ixCG_SPLL_FUNC_CNTL);
	pi->clock_registers.cg_spll_func_cntl_2 =
		RREG32_SMC(ixCG_SPLL_FUNC_CNTL_2);
	pi->clock_registers.cg_spll_func_cntl_3 =
		RREG32_SMC(ixCG_SPLL_FUNC_CNTL_3);
	pi->clock_registers.cg_spll_func_cntl_4 =
		RREG32_SMC(ixCG_SPLL_FUNC_CNTL_4);
	pi->clock_registers.cg_spll_spread_spectrum =
		RREG32_SMC(ixCG_SPLL_SPREAD_SPECTRUM);
	pi->clock_registers.cg_spll_spread_spectrum_2 =
		RREG32_SMC(ixCG_SPLL_SPREAD_SPECTRUM_2);
	pi->clock_registers.dll_cntl = RREG32(mmDLL_CNTL);
	pi->clock_registers.mclk_pwrmgt_cntl = RREG32(mmMCLK_PWRMGT_CNTL);
	pi->clock_registers.mpll_ad_func_cntl = RREG32(mmMPLL_AD_FUNC_CNTL);
	pi->clock_registers.mpll_dq_func_cntl = RREG32(mmMPLL_DQ_FUNC_CNTL);
	pi->clock_registers.mpll_func_cntl = RREG32(mmMPLL_FUNC_CNTL);
	pi->clock_registers.mpll_func_cntl_1 = RREG32(mmMPLL_FUNC_CNTL_1);
	pi->clock_registers.mpll_func_cntl_2 = RREG32(mmMPLL_FUNC_CNTL_2);
	pi->clock_registers.mpll_ss1 = RREG32(mmMPLL_SS1);
	pi->clock_registers.mpll_ss2 = RREG32(mmMPLL_SS2);
}

static void ci_init_sclk_t(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);

	pi->low_sclk_interrupt_t = 0;
}

static void ci_enable_thermal_protection(struct amdgpu_device *adev,
					 bool enable)
{
	u32 tmp = RREG32_SMC(ixGENERAL_PWRMGT);

	if (enable)
		tmp &= ~GENERAL_PWRMGT__THERMAL_PROTECTION_DIS_MASK;
	else
		tmp |= GENERAL_PWRMGT__THERMAL_PROTECTION_DIS_MASK;
	WREG32_SMC(ixGENERAL_PWRMGT, tmp);
}

static void ci_enable_acpi_power_management(struct amdgpu_device *adev)
{
	u32 tmp = RREG32_SMC(ixGENERAL_PWRMGT);

	tmp |= GENERAL_PWRMGT__STATIC_PM_EN_MASK;

	WREG32_SMC(ixGENERAL_PWRMGT, tmp);
}

#if 0
static int ci_enter_ulp_state(struct amdgpu_device *adev)
{

	WREG32(mmSMC_MESSAGE_0, PPSMC_MSG_SwitchToMinimumPower);

	udelay(25000);

	return 0;
}

static int ci_exit_ulp_state(struct amdgpu_device *adev)
{
	int i;

	WREG32(mmSMC_MESSAGE_0, PPSMC_MSG_ResumeFromMinimumPower);

	udelay(7000);

	for (i = 0; i < adev->usec_timeout; i++) {
		if (RREG32(mmSMC_RESP_0) == 1)
			break;
		udelay(1000);
	}

	return 0;
}
#endif

static int ci_notify_smc_display_change(struct amdgpu_device *adev,
					bool has_display)
{
	PPSMC_Msg msg = has_display ? PPSMC_MSG_HasDisplay : PPSMC_MSG_NoDisplay;

	return (amdgpu_ci_send_msg_to_smc(adev, msg) == PPSMC_Result_OK) ?  0 : -EINVAL;
}

static int ci_enable_ds_master_switch(struct amdgpu_device *adev,
				      bool enable)
{
	struct ci_power_info *pi = ci_get_pi(adev);

	if (enable) {
		if (pi->caps_sclk_ds) {
			if (amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_MASTER_DeepSleep_ON) != PPSMC_Result_OK)
				return -EINVAL;
		} else {
			if (amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_MASTER_DeepSleep_OFF) != PPSMC_Result_OK)
				return -EINVAL;
		}
	} else {
		if (pi->caps_sclk_ds) {
			if (amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_MASTER_DeepSleep_OFF) != PPSMC_Result_OK)
				return -EINVAL;
		}
	}

	return 0;
}

static void ci_program_display_gap(struct amdgpu_device *adev)
{
	u32 tmp = RREG32_SMC(ixCG_DISPLAY_GAP_CNTL);
	u32 pre_vbi_time_in_us;
	u32 frame_time_in_us;
	u32 ref_clock = adev->clock.spll.reference_freq;
	u32 refresh_rate = amdgpu_dpm_get_vrefresh(adev);
	u32 vblank_time = amdgpu_dpm_get_vblank_time(adev);

	tmp &= ~CG_DISPLAY_GAP_CNTL__DISP_GAP_MASK;
	if (adev->pm.dpm.new_active_crtc_count > 0)
		tmp |= (AMDGPU_PM_DISPLAY_GAP_VBLANK_OR_WM << CG_DISPLAY_GAP_CNTL__DISP_GAP__SHIFT);
	else
		tmp |= (AMDGPU_PM_DISPLAY_GAP_IGNORE << CG_DISPLAY_GAP_CNTL__DISP_GAP__SHIFT);
	WREG32_SMC(ixCG_DISPLAY_GAP_CNTL, tmp);

	if (refresh_rate == 0)
		refresh_rate = 60;
	if (vblank_time == 0xffffffff)
		vblank_time = 500;
	frame_time_in_us = 1000000 / refresh_rate;
	pre_vbi_time_in_us =
		frame_time_in_us - 200 - vblank_time;
	tmp = pre_vbi_time_in_us * (ref_clock / 100);

	WREG32_SMC(ixCG_DISPLAY_GAP_CNTL2, tmp);
	ci_write_smc_soft_register(adev, offsetof(SMU7_SoftRegisters, PreVBlankGap), 0x64);
	ci_write_smc_soft_register(adev, offsetof(SMU7_SoftRegisters, VBlankTimeout), (frame_time_in_us - pre_vbi_time_in_us));


	ci_notify_smc_display_change(adev, (adev->pm.dpm.new_active_crtc_count == 1));

}

static void ci_enable_spread_spectrum(struct amdgpu_device *adev, bool enable)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	u32 tmp;

	if (enable) {
		if (pi->caps_sclk_ss_support) {
			tmp = RREG32_SMC(ixGENERAL_PWRMGT);
			tmp |= GENERAL_PWRMGT__DYN_SPREAD_SPECTRUM_EN_MASK;
			WREG32_SMC(ixGENERAL_PWRMGT, tmp);
		}
	} else {
		tmp = RREG32_SMC(ixCG_SPLL_SPREAD_SPECTRUM);
		tmp &= ~CG_SPLL_SPREAD_SPECTRUM__SSEN_MASK;
		WREG32_SMC(ixCG_SPLL_SPREAD_SPECTRUM, tmp);

		tmp = RREG32_SMC(ixGENERAL_PWRMGT);
		tmp &= ~GENERAL_PWRMGT__DYN_SPREAD_SPECTRUM_EN_MASK;
		WREG32_SMC(ixGENERAL_PWRMGT, tmp);
	}
}

static void ci_program_sstp(struct amdgpu_device *adev)
{
	WREG32_SMC(ixCG_STATIC_SCREEN_PARAMETER,
	((CISLANDS_SSTU_DFLT << CG_STATIC_SCREEN_PARAMETER__STATIC_SCREEN_THRESHOLD_UNIT__SHIFT) |
	 (CISLANDS_SST_DFLT << CG_STATIC_SCREEN_PARAMETER__STATIC_SCREEN_THRESHOLD__SHIFT)));
}

static void ci_enable_display_gap(struct amdgpu_device *adev)
{
	u32 tmp = RREG32_SMC(ixCG_DISPLAY_GAP_CNTL);

	tmp &= ~(CG_DISPLAY_GAP_CNTL__DISP_GAP_MASK |
			CG_DISPLAY_GAP_CNTL__DISP_GAP_MCHG_MASK);
	tmp |= ((AMDGPU_PM_DISPLAY_GAP_IGNORE << CG_DISPLAY_GAP_CNTL__DISP_GAP__SHIFT) |
		(AMDGPU_PM_DISPLAY_GAP_VBLANK << CG_DISPLAY_GAP_CNTL__DISP_GAP_MCHG__SHIFT));

	WREG32_SMC(ixCG_DISPLAY_GAP_CNTL, tmp);
}

static void ci_program_vc(struct amdgpu_device *adev)
{
	u32 tmp;

	tmp = RREG32_SMC(ixSCLK_PWRMGT_CNTL);
	tmp &= ~(SCLK_PWRMGT_CNTL__RESET_SCLK_CNT_MASK | SCLK_PWRMGT_CNTL__RESET_BUSY_CNT_MASK);
	WREG32_SMC(ixSCLK_PWRMGT_CNTL, tmp);

	WREG32_SMC(ixCG_FREQ_TRAN_VOTING_0, CISLANDS_VRC_DFLT0);
	WREG32_SMC(ixCG_FREQ_TRAN_VOTING_1, CISLANDS_VRC_DFLT1);
	WREG32_SMC(ixCG_FREQ_TRAN_VOTING_2, CISLANDS_VRC_DFLT2);
	WREG32_SMC(ixCG_FREQ_TRAN_VOTING_3, CISLANDS_VRC_DFLT3);
	WREG32_SMC(ixCG_FREQ_TRAN_VOTING_4, CISLANDS_VRC_DFLT4);
	WREG32_SMC(ixCG_FREQ_TRAN_VOTING_5, CISLANDS_VRC_DFLT5);
	WREG32_SMC(ixCG_FREQ_TRAN_VOTING_6, CISLANDS_VRC_DFLT6);
	WREG32_SMC(ixCG_FREQ_TRAN_VOTING_7, CISLANDS_VRC_DFLT7);
}

static void ci_clear_vc(struct amdgpu_device *adev)
{
	u32 tmp;

	tmp = RREG32_SMC(ixSCLK_PWRMGT_CNTL);
	tmp |= (SCLK_PWRMGT_CNTL__RESET_SCLK_CNT_MASK | SCLK_PWRMGT_CNTL__RESET_BUSY_CNT_MASK);
	WREG32_SMC(ixSCLK_PWRMGT_CNTL, tmp);

	WREG32_SMC(ixCG_FREQ_TRAN_VOTING_0, 0);
	WREG32_SMC(ixCG_FREQ_TRAN_VOTING_1, 0);
	WREG32_SMC(ixCG_FREQ_TRAN_VOTING_2, 0);
	WREG32_SMC(ixCG_FREQ_TRAN_VOTING_3, 0);
	WREG32_SMC(ixCG_FREQ_TRAN_VOTING_4, 0);
	WREG32_SMC(ixCG_FREQ_TRAN_VOTING_5, 0);
	WREG32_SMC(ixCG_FREQ_TRAN_VOTING_6, 0);
	WREG32_SMC(ixCG_FREQ_TRAN_VOTING_7, 0);
}

static int ci_upload_firmware(struct amdgpu_device *adev)
{
	int i, ret;

	if (amdgpu_ci_is_smc_running(adev)) {
		DRM_INFO("smc is running, no need to load smc firmware\n");
		return 0;
	}

	for (i = 0; i < adev->usec_timeout; i++) {
		if (RREG32_SMC(ixRCU_UC_EVENTS) & RCU_UC_EVENTS__boot_seq_done_MASK)
			break;
	}
	WREG32_SMC(ixSMC_SYSCON_MISC_CNTL, 1);

	amdgpu_ci_stop_smc_clock(adev);
	amdgpu_ci_reset_smc(adev);

	ret = amdgpu_ci_load_smc_ucode(adev, SMC_RAM_END);

	return ret;

}

static int ci_get_svi2_voltage_table(struct amdgpu_device *adev,
				     struct amdgpu_clock_voltage_dependency_table *voltage_dependency_table,
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

static int ci_construct_voltage_tables(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	int ret;

	if (pi->voltage_control == CISLANDS_VOLTAGE_CONTROL_BY_GPIO) {
		ret = amdgpu_atombios_get_voltage_table(adev, VOLTAGE_TYPE_VDDC,
							VOLTAGE_OBJ_GPIO_LUT,
							&pi->vddc_voltage_table);
		if (ret)
			return ret;
	} else if (pi->voltage_control == CISLANDS_VOLTAGE_CONTROL_BY_SVID2) {
		ret = ci_get_svi2_voltage_table(adev,
						&adev->pm.dpm.dyn_state.vddc_dependency_on_mclk,
						&pi->vddc_voltage_table);
		if (ret)
			return ret;
	}

	if (pi->vddc_voltage_table.count > SMU7_MAX_LEVELS_VDDC)
		ci_trim_voltage_table_to_fit_state_table(adev, SMU7_MAX_LEVELS_VDDC,
							 &pi->vddc_voltage_table);

	if (pi->vddci_control == CISLANDS_VOLTAGE_CONTROL_BY_GPIO) {
		ret = amdgpu_atombios_get_voltage_table(adev, VOLTAGE_TYPE_VDDCI,
							VOLTAGE_OBJ_GPIO_LUT,
							&pi->vddci_voltage_table);
		if (ret)
			return ret;
	} else if (pi->vddci_control == CISLANDS_VOLTAGE_CONTROL_BY_SVID2) {
		ret = ci_get_svi2_voltage_table(adev,
						&adev->pm.dpm.dyn_state.vddci_dependency_on_mclk,
						&pi->vddci_voltage_table);
		if (ret)
			return ret;
	}

	if (pi->vddci_voltage_table.count > SMU7_MAX_LEVELS_VDDCI)
		ci_trim_voltage_table_to_fit_state_table(adev, SMU7_MAX_LEVELS_VDDCI,
							 &pi->vddci_voltage_table);

	if (pi->mvdd_control == CISLANDS_VOLTAGE_CONTROL_BY_GPIO) {
		ret = amdgpu_atombios_get_voltage_table(adev, VOLTAGE_TYPE_MVDDC,
							VOLTAGE_OBJ_GPIO_LUT,
							&pi->mvdd_voltage_table);
		if (ret)
			return ret;
	} else if (pi->mvdd_control == CISLANDS_VOLTAGE_CONTROL_BY_SVID2) {
		ret = ci_get_svi2_voltage_table(adev,
						&adev->pm.dpm.dyn_state.mvdd_dependency_on_mclk,
						&pi->mvdd_voltage_table);
		if (ret)
			return ret;
	}

	if (pi->mvdd_voltage_table.count > SMU7_MAX_LEVELS_MVDD)
		ci_trim_voltage_table_to_fit_state_table(adev, SMU7_MAX_LEVELS_MVDD,
							 &pi->mvdd_voltage_table);

	return 0;
}

static void ci_populate_smc_voltage_table(struct amdgpu_device *adev,
					  struct atom_voltage_table_entry *voltage_table,
					  SMU7_Discrete_VoltageLevel *smc_voltage_table)
{
	int ret;

	ret = ci_get_std_voltage_value_sidd(adev, voltage_table,
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

static int ci_populate_smc_vddc_table(struct amdgpu_device *adev,
				      SMU7_Discrete_DpmTable *table)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	unsigned int count;

	table->VddcLevelCount = pi->vddc_voltage_table.count;
	for (count = 0; count < table->VddcLevelCount; count++) {
		ci_populate_smc_voltage_table(adev,
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

static int ci_populate_smc_vddci_table(struct amdgpu_device *adev,
				       SMU7_Discrete_DpmTable *table)
{
	unsigned int count;
	struct ci_power_info *pi = ci_get_pi(adev);

	table->VddciLevelCount = pi->vddci_voltage_table.count;
	for (count = 0; count < table->VddciLevelCount; count++) {
		ci_populate_smc_voltage_table(adev,
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

static int ci_populate_smc_mvdd_table(struct amdgpu_device *adev,
				      SMU7_Discrete_DpmTable *table)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	unsigned int count;

	table->MvddLevelCount = pi->mvdd_voltage_table.count;
	for (count = 0; count < table->MvddLevelCount; count++) {
		ci_populate_smc_voltage_table(adev,
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

static int ci_populate_smc_voltage_tables(struct amdgpu_device *adev,
					  SMU7_Discrete_DpmTable *table)
{
	int ret;

	ret = ci_populate_smc_vddc_table(adev, table);
	if (ret)
		return ret;

	ret = ci_populate_smc_vddci_table(adev, table);
	if (ret)
		return ret;

	ret = ci_populate_smc_mvdd_table(adev, table);
	if (ret)
		return ret;

	return 0;
}

static int ci_populate_mvdd_value(struct amdgpu_device *adev, u32 mclk,
				  SMU7_Discrete_VoltageLevel *voltage)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	u32 i = 0;

	if (pi->mvdd_control != CISLANDS_VOLTAGE_CONTROL_NONE) {
		for (i = 0; i < adev->pm.dpm.dyn_state.mvdd_dependency_on_mclk.count; i++) {
			if (mclk <= adev->pm.dpm.dyn_state.mvdd_dependency_on_mclk.entries[i].clk) {
				voltage->Voltage = pi->mvdd_voltage_table.entries[i].value;
				break;
			}
		}

		if (i >= adev->pm.dpm.dyn_state.mvdd_dependency_on_mclk.count)
			return -EINVAL;
	}

	return -EINVAL;
}

static int ci_get_std_voltage_value_sidd(struct amdgpu_device *adev,
					 struct atom_voltage_table_entry *voltage_table,
					 u16 *std_voltage_hi_sidd, u16 *std_voltage_lo_sidd)
{
	u16 v_index, idx;
	bool voltage_found = false;
	*std_voltage_hi_sidd = voltage_table->value * VOLTAGE_SCALE;
	*std_voltage_lo_sidd = voltage_table->value * VOLTAGE_SCALE;

	if (adev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries == NULL)
		return -EINVAL;

	if (adev->pm.dpm.dyn_state.cac_leakage_table.entries) {
		for (v_index = 0; (u32)v_index < adev->pm.dpm.dyn_state.vddc_dependency_on_sclk.count; v_index++) {
			if (voltage_table->value ==
			    adev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries[v_index].v) {
				voltage_found = true;
				if ((u32)v_index < adev->pm.dpm.dyn_state.cac_leakage_table.count)
					idx = v_index;
				else
					idx = adev->pm.dpm.dyn_state.cac_leakage_table.count - 1;
				*std_voltage_lo_sidd =
					adev->pm.dpm.dyn_state.cac_leakage_table.entries[idx].vddc * VOLTAGE_SCALE;
				*std_voltage_hi_sidd =
					adev->pm.dpm.dyn_state.cac_leakage_table.entries[idx].leakage * VOLTAGE_SCALE;
				break;
			}
		}

		if (!voltage_found) {
			for (v_index = 0; (u32)v_index < adev->pm.dpm.dyn_state.vddc_dependency_on_sclk.count; v_index++) {
				if (voltage_table->value <=
				    adev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries[v_index].v) {
					voltage_found = true;
					if ((u32)v_index < adev->pm.dpm.dyn_state.cac_leakage_table.count)
						idx = v_index;
					else
						idx = adev->pm.dpm.dyn_state.cac_leakage_table.count - 1;
					*std_voltage_lo_sidd =
						adev->pm.dpm.dyn_state.cac_leakage_table.entries[idx].vddc * VOLTAGE_SCALE;
					*std_voltage_hi_sidd =
						adev->pm.dpm.dyn_state.cac_leakage_table.entries[idx].leakage * VOLTAGE_SCALE;
					break;
				}
			}
		}
	}

	return 0;
}

static void ci_populate_phase_value_based_on_sclk(struct amdgpu_device *adev,
						  const struct amdgpu_phase_shedding_limits_table *limits,
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

static void ci_populate_phase_value_based_on_mclk(struct amdgpu_device *adev,
						  const struct amdgpu_phase_shedding_limits_table *limits,
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

static int ci_init_arb_table_index(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	u32 tmp;
	int ret;

	ret = amdgpu_ci_read_smc_sram_dword(adev, pi->arb_table_start,
				     &tmp, pi->sram_end);
	if (ret)
		return ret;

	tmp &= 0x00FFFFFF;
	tmp |= MC_CG_ARB_FREQ_F1 << 24;

	return amdgpu_ci_write_smc_sram_dword(adev, pi->arb_table_start,
				       tmp, pi->sram_end);
}

static int ci_get_dependency_volt_by_clk(struct amdgpu_device *adev,
					 struct amdgpu_clock_voltage_dependency_table *allowed_clock_voltage_table,
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

static u8 ci_get_sleep_divider_id_from_clock(u32 sclk, u32 min_sclk_in_sr)
{
	u32 i;
	u32 tmp;
	u32 min = max(min_sclk_in_sr, (u32)CISLAND_MINIMUM_ENGINE_CLOCK);

	if (sclk < min)
		return 0;

	for (i = CISLAND_MAX_DEEPSLEEP_DIVIDER_ID;  ; i--) {
		tmp = sclk >> i;
		if (tmp >= min || i == 0)
			break;
	}

	return (u8)i;
}

static int ci_initial_switch_from_arb_f0_to_f1(struct amdgpu_device *adev)
{
	return ci_copy_and_switch_arb_sets(adev, MC_CG_ARB_FREQ_F0, MC_CG_ARB_FREQ_F1);
}

static int ci_reset_to_default(struct amdgpu_device *adev)
{
	return (amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_ResetToDefaults) == PPSMC_Result_OK) ?
		0 : -EINVAL;
}

static int ci_force_switch_to_arb_f0(struct amdgpu_device *adev)
{
	u32 tmp;

	tmp = (RREG32_SMC(ixSMC_SCRATCH9) & 0x0000ff00) >> 8;

	if (tmp == MC_CG_ARB_FREQ_F0)
		return 0;

	return ci_copy_and_switch_arb_sets(adev, tmp, MC_CG_ARB_FREQ_F0);
}

static void ci_register_patching_mc_arb(struct amdgpu_device *adev,
					const u32 engine_clock,
					const u32 memory_clock,
					u32 *dram_timimg2)
{
	bool patch;
	u32 tmp, tmp2;

	tmp = RREG32(mmMC_SEQ_MISC0);
	patch = ((tmp & 0x0000f00) == 0x300) ? true : false;

	if (patch &&
	    ((adev->pdev->device == 0x67B0) ||
	     (adev->pdev->device == 0x67B1))) {
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

static int ci_populate_memory_timing_parameters(struct amdgpu_device *adev,
						u32 sclk,
						u32 mclk,
						SMU7_Discrete_MCArbDramTimingTableEntry *arb_regs)
{
	u32 dram_timing;
	u32 dram_timing2;
	u32 burst_time;

	amdgpu_atombios_set_engine_dram_timings(adev, sclk, mclk);

	dram_timing  = RREG32(mmMC_ARB_DRAM_TIMING);
	dram_timing2 = RREG32(mmMC_ARB_DRAM_TIMING2);
	burst_time = RREG32(mmMC_ARB_BURST_TIME) & MC_ARB_BURST_TIME__STATE0_MASK;

	ci_register_patching_mc_arb(adev, sclk, mclk, &dram_timing2);

	arb_regs->McArbDramTiming  = cpu_to_be32(dram_timing);
	arb_regs->McArbDramTiming2 = cpu_to_be32(dram_timing2);
	arb_regs->McArbBurstTime = (u8)burst_time;

	return 0;
}

static int ci_do_program_memory_timing_parameters(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	SMU7_Discrete_MCArbDramTimingTable arb_regs;
	u32 i, j;
	int ret =  0;

	memset(&arb_regs, 0, sizeof(SMU7_Discrete_MCArbDramTimingTable));

	for (i = 0; i < pi->dpm_table.sclk_table.count; i++) {
		for (j = 0; j < pi->dpm_table.mclk_table.count; j++) {
			ret = ci_populate_memory_timing_parameters(adev,
								   pi->dpm_table.sclk_table.dpm_levels[i].value,
								   pi->dpm_table.mclk_table.dpm_levels[j].value,
								   &arb_regs.entries[i][j]);
			if (ret)
				break;
		}
	}

	if (ret == 0)
		ret = amdgpu_ci_copy_bytes_to_smc(adev,
					   pi->arb_table_start,
					   (u8 *)&arb_regs,
					   sizeof(SMU7_Discrete_MCArbDramTimingTable),
					   pi->sram_end);

	return ret;
}

static int ci_program_memory_timing_parameters(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);

	if (pi->need_update_smu7_dpm_table == 0)
		return 0;

	return ci_do_program_memory_timing_parameters(adev);
}

static void ci_populate_smc_initial_state(struct amdgpu_device *adev,
					  struct amdgpu_ps *amdgpu_boot_state)
{
	struct ci_ps *boot_state = ci_get_ps(amdgpu_boot_state);
	struct ci_power_info *pi = ci_get_pi(adev);
	u32 level = 0;

	for (level = 0; level < adev->pm.dpm.dyn_state.vddc_dependency_on_sclk.count; level++) {
		if (adev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries[level].clk >=
		    boot_state->performance_levels[0].sclk) {
			pi->smc_state_table.GraphicsBootLevel = level;
			break;
		}
	}

	for (level = 0; level < adev->pm.dpm.dyn_state.vddc_dependency_on_mclk.count; level++) {
		if (adev->pm.dpm.dyn_state.vddc_dependency_on_mclk.entries[level].clk >=
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

static void ci_populate_smc_link_level(struct amdgpu_device *adev,
				       SMU7_Discrete_DpmTable *table)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	struct ci_dpm_table *dpm_table = &pi->dpm_table;
	u32 i;

	for (i = 0; i < dpm_table->pcie_speed_table.count; i++) {
		table->LinkLevel[i].PcieGenSpeed =
			(u8)dpm_table->pcie_speed_table.dpm_levels[i].value;
		table->LinkLevel[i].PcieLaneCount =
			amdgpu_encode_pci_lane_width(dpm_table->pcie_speed_table.dpm_levels[i].param1);
		table->LinkLevel[i].EnabledForActivity = 1;
		table->LinkLevel[i].DownT = cpu_to_be32(5);
		table->LinkLevel[i].UpT = cpu_to_be32(30);
	}

	pi->smc_state_table.LinkLevelCount = (u8)dpm_table->pcie_speed_table.count;
	pi->dpm_level_enable_mask.pcie_dpm_enable_mask =
		ci_get_dpm_level_enable_mask_value(&dpm_table->pcie_speed_table);
}

static int ci_populate_smc_uvd_level(struct amdgpu_device *adev,
				     SMU7_Discrete_DpmTable *table)
{
	u32 count;
	struct atom_clock_dividers dividers;
	int ret = -EINVAL;

	table->UvdLevelCount =
		adev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.count;

	for (count = 0; count < table->UvdLevelCount; count++) {
		table->UvdLevel[count].VclkFrequency =
			adev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.entries[count].vclk;
		table->UvdLevel[count].DclkFrequency =
			adev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.entries[count].dclk;
		table->UvdLevel[count].MinVddc =
			adev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.entries[count].v * VOLTAGE_SCALE;
		table->UvdLevel[count].MinVddcPhases = 1;

		ret = amdgpu_atombios_get_clock_dividers(adev,
							 COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK,
							 table->UvdLevel[count].VclkFrequency, false, &dividers);
		if (ret)
			return ret;

		table->UvdLevel[count].VclkDivider = (u8)dividers.post_divider;

		ret = amdgpu_atombios_get_clock_dividers(adev,
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

static int ci_populate_smc_vce_level(struct amdgpu_device *adev,
				     SMU7_Discrete_DpmTable *table)
{
	u32 count;
	struct atom_clock_dividers dividers;
	int ret = -EINVAL;

	table->VceLevelCount =
		adev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.count;

	for (count = 0; count < table->VceLevelCount; count++) {
		table->VceLevel[count].Frequency =
			adev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.entries[count].evclk;
		table->VceLevel[count].MinVoltage =
			(u16)adev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.entries[count].v * VOLTAGE_SCALE;
		table->VceLevel[count].MinPhases = 1;

		ret = amdgpu_atombios_get_clock_dividers(adev,
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

static int ci_populate_smc_acp_level(struct amdgpu_device *adev,
				     SMU7_Discrete_DpmTable *table)
{
	u32 count;
	struct atom_clock_dividers dividers;
	int ret = -EINVAL;

	table->AcpLevelCount = (u8)
		(adev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table.count);

	for (count = 0; count < table->AcpLevelCount; count++) {
		table->AcpLevel[count].Frequency =
			adev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table.entries[count].clk;
		table->AcpLevel[count].MinVoltage =
			adev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table.entries[count].v;
		table->AcpLevel[count].MinPhases = 1;

		ret = amdgpu_atombios_get_clock_dividers(adev,
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

static int ci_populate_smc_samu_level(struct amdgpu_device *adev,
				      SMU7_Discrete_DpmTable *table)
{
	u32 count;
	struct atom_clock_dividers dividers;
	int ret = -EINVAL;

	table->SamuLevelCount =
		adev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table.count;

	for (count = 0; count < table->SamuLevelCount; count++) {
		table->SamuLevel[count].Frequency =
			adev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table.entries[count].clk;
		table->SamuLevel[count].MinVoltage =
			adev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table.entries[count].v * VOLTAGE_SCALE;
		table->SamuLevel[count].MinPhases = 1;

		ret = amdgpu_atombios_get_clock_dividers(adev,
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

static int ci_calculate_mclk_params(struct amdgpu_device *adev,
				    u32 memory_clock,
				    SMU7_Discrete_MemoryLevel *mclk,
				    bool strobe_mode,
				    bool dll_state_on)
{
	struct ci_power_info *pi = ci_get_pi(adev);
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

	ret = amdgpu_atombios_get_memory_pll_dividers(adev, memory_clock, strobe_mode, &mpll_param);
	if (ret)
		return ret;

	mpll_func_cntl &= ~MPLL_FUNC_CNTL__BWCTRL_MASK;
	mpll_func_cntl |= (mpll_param.bwcntl << MPLL_FUNC_CNTL__BWCTRL__SHIFT);

	mpll_func_cntl_1 &= ~(MPLL_FUNC_CNTL_1__CLKF_MASK | MPLL_FUNC_CNTL_1__CLKFRAC_MASK |
			MPLL_FUNC_CNTL_1__VCO_MODE_MASK);
	mpll_func_cntl_1 |= (mpll_param.clkf) << MPLL_FUNC_CNTL_1__CLKF__SHIFT |
		(mpll_param.clkfrac << MPLL_FUNC_CNTL_1__CLKFRAC__SHIFT) |
		(mpll_param.vco_mode << MPLL_FUNC_CNTL_1__VCO_MODE__SHIFT);

	mpll_ad_func_cntl &= ~MPLL_AD_FUNC_CNTL__YCLK_POST_DIV_MASK;
	mpll_ad_func_cntl |= (mpll_param.post_div << MPLL_AD_FUNC_CNTL__YCLK_POST_DIV__SHIFT);

	if (adev->gmc.vram_type == AMDGPU_VRAM_TYPE_GDDR5) {
		mpll_dq_func_cntl &= ~(MPLL_DQ_FUNC_CNTL__YCLK_SEL_MASK |
				MPLL_AD_FUNC_CNTL__YCLK_POST_DIV_MASK);
		mpll_dq_func_cntl |= (mpll_param.yclk_sel << MPLL_DQ_FUNC_CNTL__YCLK_SEL__SHIFT) |
				(mpll_param.post_div << MPLL_AD_FUNC_CNTL__YCLK_POST_DIV__SHIFT);
	}

	if (pi->caps_mclk_ss_support) {
		struct amdgpu_atom_ss ss;
		u32 freq_nom;
		u32 tmp;
		u32 reference_clock = adev->clock.mpll.reference_freq;

		if (mpll_param.qdr == 1)
			freq_nom = memory_clock * 4 * (1 << mpll_param.post_div);
		else
			freq_nom = memory_clock * 2 * (1 << mpll_param.post_div);

		tmp = (freq_nom / reference_clock);
		tmp = tmp * tmp;
		if (amdgpu_atombios_get_asic_ss_info(adev, &ss,
						     ASIC_INTERNAL_MEMORY_SS, freq_nom)) {
			u32 clks = reference_clock * 5 / ss.rate;
			u32 clkv = (u32)((((131 * ss.percentage * ss.rate) / 100) * tmp) / freq_nom);

			mpll_ss1 &= ~MPLL_SS1__CLKV_MASK;
			mpll_ss1 |= (clkv << MPLL_SS1__CLKV__SHIFT);

			mpll_ss2 &= ~MPLL_SS2__CLKS_MASK;
			mpll_ss2 |= (clks << MPLL_SS2__CLKS__SHIFT);
		}
	}

	mclk_pwrmgt_cntl &= ~MCLK_PWRMGT_CNTL__DLL_SPEED_MASK;
	mclk_pwrmgt_cntl |= (mpll_param.dll_speed << MCLK_PWRMGT_CNTL__DLL_SPEED__SHIFT);

	if (dll_state_on)
		mclk_pwrmgt_cntl |= MCLK_PWRMGT_CNTL__MRDCK0_PDNB_MASK |
			MCLK_PWRMGT_CNTL__MRDCK1_PDNB_MASK;
	else
		mclk_pwrmgt_cntl &= ~(MCLK_PWRMGT_CNTL__MRDCK0_PDNB_MASK |
			MCLK_PWRMGT_CNTL__MRDCK1_PDNB_MASK);

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

static int ci_populate_single_memory_level(struct amdgpu_device *adev,
					   u32 memory_clock,
					   SMU7_Discrete_MemoryLevel *memory_level)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	int ret;
	bool dll_state_on;

	if (adev->pm.dpm.dyn_state.vddc_dependency_on_mclk.entries) {
		ret = ci_get_dependency_volt_by_clk(adev,
						    &adev->pm.dpm.dyn_state.vddc_dependency_on_mclk,
						    memory_clock, &memory_level->MinVddc);
		if (ret)
			return ret;
	}

	if (adev->pm.dpm.dyn_state.vddci_dependency_on_mclk.entries) {
		ret = ci_get_dependency_volt_by_clk(adev,
						    &adev->pm.dpm.dyn_state.vddci_dependency_on_mclk,
						    memory_clock, &memory_level->MinVddci);
		if (ret)
			return ret;
	}

	if (adev->pm.dpm.dyn_state.mvdd_dependency_on_mclk.entries) {
		ret = ci_get_dependency_volt_by_clk(adev,
						    &adev->pm.dpm.dyn_state.mvdd_dependency_on_mclk,
						    memory_clock, &memory_level->MinMvdd);
		if (ret)
			return ret;
	}

	memory_level->MinVddcPhases = 1;

	if (pi->vddc_phase_shed_control)
		ci_populate_phase_value_based_on_mclk(adev,
						      &adev->pm.dpm.dyn_state.phase_shedding_limits_table,
						      memory_clock,
						      &memory_level->MinVddcPhases);

	memory_level->EnabledForActivity = 1;
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
	    (!pi->uvd_enabled) &&
	    (RREG32(mmDPG_PIPE_STUTTER_CONTROL) & DPG_PIPE_STUTTER_CONTROL__STUTTER_ENABLE_MASK) &&
	    (adev->pm.dpm.new_active_crtc_count <= 2))
		memory_level->StutterEnable = true;

	if (pi->mclk_strobe_mode_threshold &&
	    (memory_clock <= pi->mclk_strobe_mode_threshold))
		memory_level->StrobeEnable = 1;

	if (adev->gmc.vram_type == AMDGPU_VRAM_TYPE_GDDR5) {
		memory_level->StrobeRatio =
			ci_get_mclk_frequency_ratio(memory_clock, memory_level->StrobeEnable);
		if (pi->mclk_edc_enable_threshold &&
		    (memory_clock > pi->mclk_edc_enable_threshold))
			memory_level->EdcReadEnable = true;

		if (pi->mclk_edc_wr_enable_threshold &&
		    (memory_clock > pi->mclk_edc_wr_enable_threshold))
			memory_level->EdcWriteEnable = true;

		if (memory_level->StrobeEnable) {
			if (ci_get_mclk_frequency_ratio(memory_clock, true) >=
			    ((RREG32(mmMC_SEQ_MISC7) >> 16) & 0xf))
				dll_state_on = ((RREG32(mmMC_SEQ_MISC5) >> 1) & 0x1) ? true : false;
			else
				dll_state_on = ((RREG32(mmMC_SEQ_MISC6) >> 1) & 0x1) ? true : false;
		} else {
			dll_state_on = pi->dll_default_on;
		}
	} else {
		memory_level->StrobeRatio = ci_get_ddr3_mclk_frequency_ratio(memory_clock);
		dll_state_on = ((RREG32(mmMC_SEQ_MISC5) >> 1) & 0x1) ? true : false;
	}

	ret = ci_calculate_mclk_params(adev, memory_clock, memory_level, memory_level->StrobeEnable, dll_state_on);
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

static int ci_populate_smc_acpi_level(struct amdgpu_device *adev,
				      SMU7_Discrete_DpmTable *table)
{
	struct ci_power_info *pi = ci_get_pi(adev);
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

	table->ACPILevel.SclkFrequency = adev->clock.spll.reference_freq;

	ret = amdgpu_atombios_get_clock_dividers(adev,
						 COMPUTE_GPUCLK_INPUT_FLAG_SCLK,
						 table->ACPILevel.SclkFrequency, false, &dividers);
	if (ret)
		return ret;

	table->ACPILevel.SclkDid = (u8)dividers.post_divider;
	table->ACPILevel.DisplayWatermark = PPSMC_DISPLAY_WATERMARK_LOW;
	table->ACPILevel.DeepSleepDivId = 0;

	spll_func_cntl &= ~CG_SPLL_FUNC_CNTL__SPLL_PWRON_MASK;
	spll_func_cntl |= CG_SPLL_FUNC_CNTL__SPLL_RESET_MASK;

	spll_func_cntl_2 &= ~CG_SPLL_FUNC_CNTL_2__SCLK_MUX_SEL_MASK;
	spll_func_cntl_2 |= (4 << CG_SPLL_FUNC_CNTL_2__SCLK_MUX_SEL__SHIFT);

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

	if (ci_populate_mvdd_value(adev, 0, &voltage_level))
		table->MemoryACPILevel.MinMvdd = 0;
	else
		table->MemoryACPILevel.MinMvdd =
			cpu_to_be32(voltage_level.Voltage * VOLTAGE_SCALE);

	mclk_pwrmgt_cntl |= MCLK_PWRMGT_CNTL__MRDCK0_RESET_MASK |
		MCLK_PWRMGT_CNTL__MRDCK1_RESET_MASK;
	mclk_pwrmgt_cntl &= ~(MCLK_PWRMGT_CNTL__MRDCK0_PDNB_MASK |
			MCLK_PWRMGT_CNTL__MRDCK1_PDNB_MASK);

	dll_cntl &= ~(DLL_CNTL__MRDCK0_BYPASS_MASK | DLL_CNTL__MRDCK1_BYPASS_MASK);

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


static int ci_enable_ulv(struct amdgpu_device *adev, bool enable)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	struct ci_ulv_parm *ulv = &pi->ulv;

	if (ulv->supported) {
		if (enable)
			return (amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_EnableULV) == PPSMC_Result_OK) ?
				0 : -EINVAL;
		else
			return (amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_DisableULV) == PPSMC_Result_OK) ?
				0 : -EINVAL;
	}

	return 0;
}

static int ci_populate_ulv_level(struct amdgpu_device *adev,
				 SMU7_Discrete_Ulv *state)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	u16 ulv_voltage = adev->pm.dpm.backbias_response_time;

	state->CcPwrDynRm = 0;
	state->CcPwrDynRm1 = 0;

	if (ulv_voltage == 0) {
		pi->ulv.supported = false;
		return 0;
	}

	if (pi->voltage_control != CISLANDS_VOLTAGE_CONTROL_BY_SVID2) {
		if (ulv_voltage > adev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries[0].v)
			state->VddcOffset = 0;
		else
			state->VddcOffset =
				adev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries[0].v - ulv_voltage;
	} else {
		if (ulv_voltage > adev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries[0].v)
			state->VddcOffsetVid = 0;
		else
			state->VddcOffsetVid = (u8)
				((adev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries[0].v - ulv_voltage) *
				 VOLTAGE_VID_OFFSET_SCALE2 / VOLTAGE_VID_OFFSET_SCALE1);
	}
	state->VddcPhase = pi->vddc_phase_shed_control ? 0 : 1;

	state->CcPwrDynRm = cpu_to_be32(state->CcPwrDynRm);
	state->CcPwrDynRm1 = cpu_to_be32(state->CcPwrDynRm1);
	state->VddcOffset = cpu_to_be16(state->VddcOffset);

	return 0;
}

static int ci_calculate_sclk_params(struct amdgpu_device *adev,
				    u32 engine_clock,
				    SMU7_Discrete_GraphicsLevel *sclk)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	struct atom_clock_dividers dividers;
	u32 spll_func_cntl_3 = pi->clock_registers.cg_spll_func_cntl_3;
	u32 spll_func_cntl_4 = pi->clock_registers.cg_spll_func_cntl_4;
	u32 cg_spll_spread_spectrum = pi->clock_registers.cg_spll_spread_spectrum;
	u32 cg_spll_spread_spectrum_2 = pi->clock_registers.cg_spll_spread_spectrum_2;
	u32 reference_clock = adev->clock.spll.reference_freq;
	u32 reference_divider;
	u32 fbdiv;
	int ret;

	ret = amdgpu_atombios_get_clock_dividers(adev,
						 COMPUTE_GPUCLK_INPUT_FLAG_SCLK,
						 engine_clock, false, &dividers);
	if (ret)
		return ret;

	reference_divider = 1 + dividers.ref_div;
	fbdiv = dividers.fb_div & 0x3FFFFFF;

	spll_func_cntl_3 &= ~CG_SPLL_FUNC_CNTL_3__SPLL_FB_DIV_MASK;
	spll_func_cntl_3 |= (fbdiv << CG_SPLL_FUNC_CNTL_3__SPLL_FB_DIV__SHIFT);
	spll_func_cntl_3 |= CG_SPLL_FUNC_CNTL_3__SPLL_DITHEN_MASK;

	if (pi->caps_sclk_ss_support) {
		struct amdgpu_atom_ss ss;
		u32 vco_freq = engine_clock * dividers.post_div;

		if (amdgpu_atombios_get_asic_ss_info(adev, &ss,
						     ASIC_INTERNAL_ENGINE_SS, vco_freq)) {
			u32 clk_s = reference_clock * 5 / (reference_divider * ss.rate);
			u32 clk_v = 4 * ss.percentage * fbdiv / (clk_s * 10000);

			cg_spll_spread_spectrum &= ~(CG_SPLL_SPREAD_SPECTRUM__CLKS_MASK | CG_SPLL_SPREAD_SPECTRUM__SSEN_MASK);
			cg_spll_spread_spectrum |= (clk_s << CG_SPLL_SPREAD_SPECTRUM__CLKS__SHIFT);
			cg_spll_spread_spectrum |= (1 << CG_SPLL_SPREAD_SPECTRUM__SSEN__SHIFT);

			cg_spll_spread_spectrum_2 &= ~CG_SPLL_SPREAD_SPECTRUM_2__CLKV_MASK;
			cg_spll_spread_spectrum_2 |= (clk_v << CG_SPLL_SPREAD_SPECTRUM_2__CLKV__SHIFT);
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

static int ci_populate_single_graphic_level(struct amdgpu_device *adev,
					    u32 engine_clock,
					    u16 sclk_activity_level_t,
					    SMU7_Discrete_GraphicsLevel *graphic_level)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	int ret;

	ret = ci_calculate_sclk_params(adev, engine_clock, graphic_level);
	if (ret)
		return ret;

	ret = ci_get_dependency_volt_by_clk(adev,
					    &adev->pm.dpm.dyn_state.vddc_dependency_on_sclk,
					    engine_clock, &graphic_level->MinVddc);
	if (ret)
		return ret;

	graphic_level->SclkFrequency = engine_clock;

	graphic_level->Flags =  0;
	graphic_level->MinVddcPhases = 1;

	if (pi->vddc_phase_shed_control)
		ci_populate_phase_value_based_on_sclk(adev,
						      &adev->pm.dpm.dyn_state.phase_shedding_limits_table,
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
		graphic_level->DeepSleepDivId = ci_get_sleep_divider_id_from_clock(engine_clock,
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

static int ci_populate_all_graphic_levels(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	struct ci_dpm_table *dpm_table = &pi->dpm_table;
	u32 level_array_address = pi->dpm_table_start +
		offsetof(SMU7_Discrete_DpmTable, GraphicsLevel);
	u32 level_array_size = sizeof(SMU7_Discrete_GraphicsLevel) *
		SMU7_MAX_LEVELS_GRAPHICS;
	SMU7_Discrete_GraphicsLevel *levels = pi->smc_state_table.GraphicsLevel;
	u32 i, ret;

	memset(levels, 0, level_array_size);

	for (i = 0; i < dpm_table->sclk_table.count; i++) {
		ret = ci_populate_single_graphic_level(adev,
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

	ret = amdgpu_ci_copy_bytes_to_smc(adev, level_array_address,
				   (u8 *)levels, level_array_size,
				   pi->sram_end);
	if (ret)
		return ret;

	return 0;
}

static int ci_populate_ulv_state(struct amdgpu_device *adev,
				 SMU7_Discrete_Ulv *ulv_level)
{
	return ci_populate_ulv_level(adev, ulv_level);
}

static int ci_populate_all_memory_levels(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
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
		ret = ci_populate_single_memory_level(adev,
						      dpm_table->mclk_table.dpm_levels[i].value,
						      &pi->smc_state_table.MemoryLevel[i]);
		if (ret)
			return ret;
	}

	if ((dpm_table->mclk_table.count >= 2) &&
	    ((adev->pdev->device == 0x67B0) || (adev->pdev->device == 0x67B1))) {
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

	ret = amdgpu_ci_copy_bytes_to_smc(adev, level_array_address,
				   (u8 *)levels, level_array_size,
				   pi->sram_end);
	if (ret)
		return ret;

	return 0;
}

static void ci_reset_single_dpm_table(struct amdgpu_device *adev,
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

static int ci_setup_default_pcie_tables(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);

	if (!pi->use_pcie_performance_levels && !pi->use_pcie_powersaving_levels)
		return -EINVAL;

	if (pi->use_pcie_performance_levels && !pi->use_pcie_powersaving_levels) {
		pi->pcie_gen_powersaving = pi->pcie_gen_performance;
		pi->pcie_lane_powersaving = pi->pcie_lane_performance;
	} else if (!pi->use_pcie_performance_levels && pi->use_pcie_powersaving_levels) {
		pi->pcie_gen_performance = pi->pcie_gen_powersaving;
		pi->pcie_lane_performance = pi->pcie_lane_powersaving;
	}

	ci_reset_single_dpm_table(adev,
				  &pi->dpm_table.pcie_speed_table,
				  SMU7_MAX_LEVELS_LINK);

	if (adev->asic_type == CHIP_BONAIRE)
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

static int ci_setup_default_dpm_tables(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	struct amdgpu_clock_voltage_dependency_table *allowed_sclk_vddc_table =
		&adev->pm.dpm.dyn_state.vddc_dependency_on_sclk;
	struct amdgpu_clock_voltage_dependency_table *allowed_mclk_table =
		&adev->pm.dpm.dyn_state.vddc_dependency_on_mclk;
	struct amdgpu_cac_leakage_table *std_voltage_table =
		&adev->pm.dpm.dyn_state.cac_leakage_table;
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

	ci_reset_single_dpm_table(adev,
				  &pi->dpm_table.sclk_table,
				  SMU7_MAX_LEVELS_GRAPHICS);
	ci_reset_single_dpm_table(adev,
				  &pi->dpm_table.mclk_table,
				  SMU7_MAX_LEVELS_MEMORY);
	ci_reset_single_dpm_table(adev,
				  &pi->dpm_table.vddc_table,
				  SMU7_MAX_LEVELS_VDDC);
	ci_reset_single_dpm_table(adev,
				  &pi->dpm_table.vddci_table,
				  SMU7_MAX_LEVELS_VDDCI);
	ci_reset_single_dpm_table(adev,
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

	allowed_mclk_table = &adev->pm.dpm.dyn_state.vddci_dependency_on_mclk;
	if (allowed_mclk_table) {
		for (i = 0; i < allowed_mclk_table->count; i++) {
			pi->dpm_table.vddci_table.dpm_levels[i].value =
				allowed_mclk_table->entries[i].v;
			pi->dpm_table.vddci_table.dpm_levels[i].enabled = true;
		}
		pi->dpm_table.vddci_table.count = allowed_mclk_table->count;
	}

	allowed_mclk_table = &adev->pm.dpm.dyn_state.mvdd_dependency_on_mclk;
	if (allowed_mclk_table) {
		for (i = 0; i < allowed_mclk_table->count; i++) {
			pi->dpm_table.mvdd_table.dpm_levels[i].value =
				allowed_mclk_table->entries[i].v;
			pi->dpm_table.mvdd_table.dpm_levels[i].enabled = true;
		}
		pi->dpm_table.mvdd_table.count = allowed_mclk_table->count;
	}

	ci_setup_default_pcie_tables(adev);

	/* save a copy of the default DPM table */
	memcpy(&(pi->golden_dpm_table), &(pi->dpm_table),
			sizeof(struct ci_dpm_table));

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

static int ci_init_smc_table(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	struct ci_ulv_parm *ulv = &pi->ulv;
	struct amdgpu_ps *amdgpu_boot_state = adev->pm.dpm.boot_ps;
	SMU7_Discrete_DpmTable *table = &pi->smc_state_table;
	int ret;

	ret = ci_setup_default_dpm_tables(adev);
	if (ret)
		return ret;

	if (pi->voltage_control != CISLANDS_VOLTAGE_CONTROL_NONE)
		ci_populate_smc_voltage_tables(adev, table);

	ci_init_fps_limits(adev);

	if (adev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_HARDWAREDC)
		table->SystemFlags |= PPSMC_SYSTEMFLAG_GPIO_DC;

	if (adev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_STEPVDDC)
		table->SystemFlags |= PPSMC_SYSTEMFLAG_STEPVDDC;

	if (adev->gmc.vram_type == AMDGPU_VRAM_TYPE_GDDR5)
		table->SystemFlags |= PPSMC_SYSTEMFLAG_GDDR5;

	if (ulv->supported) {
		ret = ci_populate_ulv_state(adev, &pi->smc_state_table.Ulv);
		if (ret)
			return ret;
		WREG32_SMC(ixCG_ULV_PARAMETER, ulv->cg_ulv_parameter);
	}

	ret = ci_populate_all_graphic_levels(adev);
	if (ret)
		return ret;

	ret = ci_populate_all_memory_levels(adev);
	if (ret)
		return ret;

	ci_populate_smc_link_level(adev, table);

	ret = ci_populate_smc_acpi_level(adev, table);
	if (ret)
		return ret;

	ret = ci_populate_smc_vce_level(adev, table);
	if (ret)
		return ret;

	ret = ci_populate_smc_acp_level(adev, table);
	if (ret)
		return ret;

	ret = ci_populate_smc_samu_level(adev, table);
	if (ret)
		return ret;

	ret = ci_do_program_memory_timing_parameters(adev);
	if (ret)
		return ret;

	ret = ci_populate_smc_uvd_level(adev, table);
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

	ci_populate_smc_initial_state(adev, amdgpu_boot_state);

	ret = ci_populate_bapm_parameters_in_dpm_table(adev);
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

	ret = amdgpu_ci_copy_bytes_to_smc(adev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Discrete_DpmTable, SystemFlags),
				   (u8 *)&table->SystemFlags,
				   sizeof(SMU7_Discrete_DpmTable) - 3 * sizeof(SMU7_PIDController),
				   pi->sram_end);
	if (ret)
		return ret;

	return 0;
}

static void ci_trim_single_dpm_states(struct amdgpu_device *adev,
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

static void ci_trim_pcie_dpm_states(struct amdgpu_device *adev,
				    u32 speed_low, u32 lanes_low,
				    u32 speed_high, u32 lanes_high)
{
	struct ci_power_info *pi = ci_get_pi(adev);
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

static int ci_trim_dpm_states(struct amdgpu_device *adev,
			      struct amdgpu_ps *amdgpu_state)
{
	struct ci_ps *state = ci_get_ps(amdgpu_state);
	struct ci_power_info *pi = ci_get_pi(adev);
	u32 high_limit_count;

	if (state->performance_level_count < 1)
		return -EINVAL;

	if (state->performance_level_count == 1)
		high_limit_count = 0;
	else
		high_limit_count = 1;

	ci_trim_single_dpm_states(adev,
				  &pi->dpm_table.sclk_table,
				  state->performance_levels[0].sclk,
				  state->performance_levels[high_limit_count].sclk);

	ci_trim_single_dpm_states(adev,
				  &pi->dpm_table.mclk_table,
				  state->performance_levels[0].mclk,
				  state->performance_levels[high_limit_count].mclk);

	ci_trim_pcie_dpm_states(adev,
				state->performance_levels[0].pcie_gen,
				state->performance_levels[0].pcie_lane,
				state->performance_levels[high_limit_count].pcie_gen,
				state->performance_levels[high_limit_count].pcie_lane);

	return 0;
}

static int ci_apply_disp_minimum_voltage_request(struct amdgpu_device *adev)
{
	struct amdgpu_clock_voltage_dependency_table *disp_voltage_table =
		&adev->pm.dpm.dyn_state.vddc_dependency_on_dispclk;
	struct amdgpu_clock_voltage_dependency_table *vddc_table =
		&adev->pm.dpm.dyn_state.vddc_dependency_on_sclk;
	u32 requested_voltage = 0;
	u32 i;

	if (disp_voltage_table == NULL)
		return -EINVAL;
	if (!disp_voltage_table->count)
		return -EINVAL;

	for (i = 0; i < disp_voltage_table->count; i++) {
		if (adev->clock.current_dispclk == disp_voltage_table->entries[i].clk)
			requested_voltage = disp_voltage_table->entries[i].v;
	}

	for (i = 0; i < vddc_table->count; i++) {
		if (requested_voltage <= vddc_table->entries[i].v) {
			requested_voltage = vddc_table->entries[i].v;
			return (amdgpu_ci_send_msg_to_smc_with_parameter(adev,
								  PPSMC_MSG_VddC_Request,
								  requested_voltage * VOLTAGE_SCALE) == PPSMC_Result_OK) ?
				0 : -EINVAL;
		}
	}

	return -EINVAL;
}

static int ci_upload_dpm_level_enable_mask(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	PPSMC_Result result;

	ci_apply_disp_minimum_voltage_request(adev);

	if (!pi->sclk_dpm_key_disabled) {
		if (pi->dpm_level_enable_mask.sclk_dpm_enable_mask) {
			result = amdgpu_ci_send_msg_to_smc_with_parameter(adev,
								   PPSMC_MSG_SCLKDPM_SetEnabledMask,
								   pi->dpm_level_enable_mask.sclk_dpm_enable_mask);
			if (result != PPSMC_Result_OK)
				return -EINVAL;
		}
	}

	if (!pi->mclk_dpm_key_disabled) {
		if (pi->dpm_level_enable_mask.mclk_dpm_enable_mask) {
			result = amdgpu_ci_send_msg_to_smc_with_parameter(adev,
								   PPSMC_MSG_MCLKDPM_SetEnabledMask,
								   pi->dpm_level_enable_mask.mclk_dpm_enable_mask);
			if (result != PPSMC_Result_OK)
				return -EINVAL;
		}
	}

#if 0
	if (!pi->pcie_dpm_key_disabled) {
		if (pi->dpm_level_enable_mask.pcie_dpm_enable_mask) {
			result = amdgpu_ci_send_msg_to_smc_with_parameter(adev,
								   PPSMC_MSG_PCIeDPM_SetEnabledMask,
								   pi->dpm_level_enable_mask.pcie_dpm_enable_mask);
			if (result != PPSMC_Result_OK)
				return -EINVAL;
		}
	}
#endif

	return 0;
}

static void ci_find_dpm_states_clocks_in_dpm_table(struct amdgpu_device *adev,
						   struct amdgpu_ps *amdgpu_state)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	struct ci_ps *state = ci_get_ps(amdgpu_state);
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
		/* XXX check display min clock requirements */
		if (CISLAND_MINIMUM_ENGINE_CLOCK != CISLAND_MINIMUM_ENGINE_CLOCK)
			pi->need_update_smu7_dpm_table |= DPMTABLE_UPDATE_SCLK;
	}

	for (i = 0; i < mclk_table->count; i++) {
		if (mclk == mclk_table->dpm_levels[i].value)
			break;
	}

	if (i >= mclk_table->count)
		pi->need_update_smu7_dpm_table |= DPMTABLE_OD_UPDATE_MCLK;

	if (adev->pm.dpm.current_active_crtc_count !=
	    adev->pm.dpm.new_active_crtc_count)
		pi->need_update_smu7_dpm_table |= DPMTABLE_UPDATE_MCLK;
}

static int ci_populate_and_upload_sclk_mclk_dpm_levels(struct amdgpu_device *adev,
						       struct amdgpu_ps *amdgpu_state)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	struct ci_ps *state = ci_get_ps(amdgpu_state);
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
		ret = ci_populate_all_graphic_levels(adev);
		if (ret)
			return ret;
	}

	if (pi->need_update_smu7_dpm_table & (DPMTABLE_OD_UPDATE_MCLK | DPMTABLE_UPDATE_MCLK)) {
		ret = ci_populate_all_memory_levels(adev);
		if (ret)
			return ret;
	}

	return 0;
}

static int ci_enable_uvd_dpm(struct amdgpu_device *adev, bool enable)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	const struct amdgpu_clock_and_voltage_limits *max_limits;
	int i;

	if (adev->pm.dpm.ac_power)
		max_limits = &adev->pm.dpm.dyn_state.max_clock_voltage_on_ac;
	else
		max_limits = &adev->pm.dpm.dyn_state.max_clock_voltage_on_dc;

	if (enable) {
		pi->dpm_level_enable_mask.uvd_dpm_enable_mask = 0;

		for (i = adev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.count - 1; i >= 0; i--) {
			if (adev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.entries[i].v <= max_limits->vddc) {
				pi->dpm_level_enable_mask.uvd_dpm_enable_mask |= 1 << i;

				if (!pi->caps_uvd_dpm)
					break;
			}
		}

		amdgpu_ci_send_msg_to_smc_with_parameter(adev,
						  PPSMC_MSG_UVDDPM_SetEnabledMask,
						  pi->dpm_level_enable_mask.uvd_dpm_enable_mask);

		if (pi->last_mclk_dpm_enable_mask & 0x1) {
			pi->uvd_enabled = true;
			pi->dpm_level_enable_mask.mclk_dpm_enable_mask &= 0xFFFFFFFE;
			amdgpu_ci_send_msg_to_smc_with_parameter(adev,
							  PPSMC_MSG_MCLKDPM_SetEnabledMask,
							  pi->dpm_level_enable_mask.mclk_dpm_enable_mask);
		}
	} else {
		if (pi->uvd_enabled) {
			pi->uvd_enabled = false;
			pi->dpm_level_enable_mask.mclk_dpm_enable_mask |= 1;
			amdgpu_ci_send_msg_to_smc_with_parameter(adev,
							  PPSMC_MSG_MCLKDPM_SetEnabledMask,
							  pi->dpm_level_enable_mask.mclk_dpm_enable_mask);
		}
	}

	return (amdgpu_ci_send_msg_to_smc(adev, enable ?
				   PPSMC_MSG_UVDDPM_Enable : PPSMC_MSG_UVDDPM_Disable) == PPSMC_Result_OK) ?
		0 : -EINVAL;
}

static int ci_enable_vce_dpm(struct amdgpu_device *adev, bool enable)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	const struct amdgpu_clock_and_voltage_limits *max_limits;
	int i;

	if (adev->pm.dpm.ac_power)
		max_limits = &adev->pm.dpm.dyn_state.max_clock_voltage_on_ac;
	else
		max_limits = &adev->pm.dpm.dyn_state.max_clock_voltage_on_dc;

	if (enable) {
		pi->dpm_level_enable_mask.vce_dpm_enable_mask = 0;
		for (i = adev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.count - 1; i >= 0; i--) {
			if (adev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table.entries[i].v <= max_limits->vddc) {
				pi->dpm_level_enable_mask.vce_dpm_enable_mask |= 1 << i;

				if (!pi->caps_vce_dpm)
					break;
			}
		}

		amdgpu_ci_send_msg_to_smc_with_parameter(adev,
						  PPSMC_MSG_VCEDPM_SetEnabledMask,
						  pi->dpm_level_enable_mask.vce_dpm_enable_mask);
	}

	return (amdgpu_ci_send_msg_to_smc(adev, enable ?
				   PPSMC_MSG_VCEDPM_Enable : PPSMC_MSG_VCEDPM_Disable) == PPSMC_Result_OK) ?
		0 : -EINVAL;
}

#if 0
static int ci_enable_samu_dpm(struct amdgpu_device *adev, bool enable)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	const struct amdgpu_clock_and_voltage_limits *max_limits;
	int i;

	if (adev->pm.dpm.ac_power)
		max_limits = &adev->pm.dpm.dyn_state.max_clock_voltage_on_ac;
	else
		max_limits = &adev->pm.dpm.dyn_state.max_clock_voltage_on_dc;

	if (enable) {
		pi->dpm_level_enable_mask.samu_dpm_enable_mask = 0;
		for (i = adev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table.count - 1; i >= 0; i--) {
			if (adev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table.entries[i].v <= max_limits->vddc) {
				pi->dpm_level_enable_mask.samu_dpm_enable_mask |= 1 << i;

				if (!pi->caps_samu_dpm)
					break;
			}
		}

		amdgpu_ci_send_msg_to_smc_with_parameter(adev,
						  PPSMC_MSG_SAMUDPM_SetEnabledMask,
						  pi->dpm_level_enable_mask.samu_dpm_enable_mask);
	}
	return (amdgpu_ci_send_msg_to_smc(adev, enable ?
				   PPSMC_MSG_SAMUDPM_Enable : PPSMC_MSG_SAMUDPM_Disable) == PPSMC_Result_OK) ?
		0 : -EINVAL;
}

static int ci_enable_acp_dpm(struct amdgpu_device *adev, bool enable)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	const struct amdgpu_clock_and_voltage_limits *max_limits;
	int i;

	if (adev->pm.dpm.ac_power)
		max_limits = &adev->pm.dpm.dyn_state.max_clock_voltage_on_ac;
	else
		max_limits = &adev->pm.dpm.dyn_state.max_clock_voltage_on_dc;

	if (enable) {
		pi->dpm_level_enable_mask.acp_dpm_enable_mask = 0;
		for (i = adev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table.count - 1; i >= 0; i--) {
			if (adev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table.entries[i].v <= max_limits->vddc) {
				pi->dpm_level_enable_mask.acp_dpm_enable_mask |= 1 << i;

				if (!pi->caps_acp_dpm)
					break;
			}
		}

		amdgpu_ci_send_msg_to_smc_with_parameter(adev,
						  PPSMC_MSG_ACPDPM_SetEnabledMask,
						  pi->dpm_level_enable_mask.acp_dpm_enable_mask);
	}

	return (amdgpu_ci_send_msg_to_smc(adev, enable ?
				   PPSMC_MSG_ACPDPM_Enable : PPSMC_MSG_ACPDPM_Disable) == PPSMC_Result_OK) ?
		0 : -EINVAL;
}
#endif

static int ci_update_uvd_dpm(struct amdgpu_device *adev, bool gate)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	u32 tmp;
	int ret = 0;

	if (!gate) {
		/* turn the clocks on when decoding */
		if (pi->caps_uvd_dpm ||
		    (adev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.count <= 0))
			pi->smc_state_table.UvdBootLevel = 0;
		else
			pi->smc_state_table.UvdBootLevel =
				adev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table.count - 1;

		tmp = RREG32_SMC(ixDPM_TABLE_475);
		tmp &= ~DPM_TABLE_475__UvdBootLevel_MASK;
		tmp |= (pi->smc_state_table.UvdBootLevel << DPM_TABLE_475__UvdBootLevel__SHIFT);
		WREG32_SMC(ixDPM_TABLE_475, tmp);
		ret = ci_enable_uvd_dpm(adev, true);
	} else {
		ret = ci_enable_uvd_dpm(adev, false);
		if (ret)
			return ret;
	}

	return ret;
}

static u8 ci_get_vce_boot_level(struct amdgpu_device *adev)
{
	u8 i;
	u32 min_evclk = 30000; /* ??? */
	struct amdgpu_vce_clock_voltage_dependency_table *table =
		&adev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table;

	for (i = 0; i < table->count; i++) {
		if (table->entries[i].evclk >= min_evclk)
			return i;
	}

	return table->count - 1;
}

static int ci_update_vce_dpm(struct amdgpu_device *adev,
			     struct amdgpu_ps *amdgpu_new_state,
			     struct amdgpu_ps *amdgpu_current_state)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	int ret = 0;
	u32 tmp;

	if (amdgpu_current_state->evclk != amdgpu_new_state->evclk) {
		if (amdgpu_new_state->evclk) {
			pi->smc_state_table.VceBootLevel = ci_get_vce_boot_level(adev);
			tmp = RREG32_SMC(ixDPM_TABLE_475);
			tmp &= ~DPM_TABLE_475__VceBootLevel_MASK;
			tmp |= (pi->smc_state_table.VceBootLevel << DPM_TABLE_475__VceBootLevel__SHIFT);
			WREG32_SMC(ixDPM_TABLE_475, tmp);

			ret = ci_enable_vce_dpm(adev, true);
		} else {
			ret = ci_enable_vce_dpm(adev, false);
			if (ret)
				return ret;
		}
	}
	return ret;
}

#if 0
static int ci_update_samu_dpm(struct amdgpu_device *adev, bool gate)
{
	return ci_enable_samu_dpm(adev, gate);
}

static int ci_update_acp_dpm(struct amdgpu_device *adev, bool gate)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	u32 tmp;

	if (!gate) {
		pi->smc_state_table.AcpBootLevel = 0;

		tmp = RREG32_SMC(ixDPM_TABLE_475);
		tmp &= ~AcpBootLevel_MASK;
		tmp |= AcpBootLevel(pi->smc_state_table.AcpBootLevel);
		WREG32_SMC(ixDPM_TABLE_475, tmp);
	}

	return ci_enable_acp_dpm(adev, !gate);
}
#endif

static int ci_generate_dpm_level_enable_mask(struct amdgpu_device *adev,
					     struct amdgpu_ps *amdgpu_state)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	int ret;

	ret = ci_trim_dpm_states(adev, amdgpu_state);
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

static u32 ci_get_lowest_enabled_level(struct amdgpu_device *adev,
				       u32 level_mask)
{
	u32 level = 0;

	while ((level_mask & (1 << level)) == 0)
		level++;

	return level;
}


static int ci_dpm_force_performance_level(void *handle,
					  enum amd_dpm_forced_level level)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct ci_power_info *pi = ci_get_pi(adev);
	u32 tmp, levels, i;
	int ret;

	if (level == AMD_DPM_FORCED_LEVEL_HIGH) {
		if ((!pi->pcie_dpm_key_disabled) &&
		    pi->dpm_level_enable_mask.pcie_dpm_enable_mask) {
			levels = 0;
			tmp = pi->dpm_level_enable_mask.pcie_dpm_enable_mask;
			while (tmp >>= 1)
				levels++;
			if (levels) {
				ret = ci_dpm_force_state_pcie(adev, level);
				if (ret)
					return ret;
				for (i = 0; i < adev->usec_timeout; i++) {
					tmp = (RREG32_SMC(ixTARGET_AND_CURRENT_PROFILE_INDEX_1) &
					TARGET_AND_CURRENT_PROFILE_INDEX_1__CURR_PCIE_INDEX_MASK) >>
					TARGET_AND_CURRENT_PROFILE_INDEX_1__CURR_PCIE_INDEX__SHIFT;
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
				ret = ci_dpm_force_state_sclk(adev, levels);
				if (ret)
					return ret;
				for (i = 0; i < adev->usec_timeout; i++) {
					tmp = (RREG32_SMC(ixTARGET_AND_CURRENT_PROFILE_INDEX) &
					TARGET_AND_CURRENT_PROFILE_INDEX__CURR_SCLK_INDEX_MASK) >>
					TARGET_AND_CURRENT_PROFILE_INDEX__CURR_SCLK_INDEX__SHIFT;
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
				ret = ci_dpm_force_state_mclk(adev, levels);
				if (ret)
					return ret;
				for (i = 0; i < adev->usec_timeout; i++) {
					tmp = (RREG32_SMC(ixTARGET_AND_CURRENT_PROFILE_INDEX) &
					TARGET_AND_CURRENT_PROFILE_INDEX__CURR_MCLK_INDEX_MASK) >>
					TARGET_AND_CURRENT_PROFILE_INDEX__CURR_MCLK_INDEX__SHIFT;
					if (tmp == levels)
						break;
					udelay(1);
				}
			}
		}
	} else if (level == AMD_DPM_FORCED_LEVEL_LOW) {
		if ((!pi->sclk_dpm_key_disabled) &&
		    pi->dpm_level_enable_mask.sclk_dpm_enable_mask) {
			levels = ci_get_lowest_enabled_level(adev,
							     pi->dpm_level_enable_mask.sclk_dpm_enable_mask);
			ret = ci_dpm_force_state_sclk(adev, levels);
			if (ret)
				return ret;
			for (i = 0; i < adev->usec_timeout; i++) {
				tmp = (RREG32_SMC(ixTARGET_AND_CURRENT_PROFILE_INDEX) &
				TARGET_AND_CURRENT_PROFILE_INDEX__CURR_SCLK_INDEX_MASK) >>
				TARGET_AND_CURRENT_PROFILE_INDEX__CURR_SCLK_INDEX__SHIFT;
				if (tmp == levels)
					break;
				udelay(1);
			}
		}
		if ((!pi->mclk_dpm_key_disabled) &&
		    pi->dpm_level_enable_mask.mclk_dpm_enable_mask) {
			levels = ci_get_lowest_enabled_level(adev,
							     pi->dpm_level_enable_mask.mclk_dpm_enable_mask);
			ret = ci_dpm_force_state_mclk(adev, levels);
			if (ret)
				return ret;
			for (i = 0; i < adev->usec_timeout; i++) {
				tmp = (RREG32_SMC(ixTARGET_AND_CURRENT_PROFILE_INDEX) &
				TARGET_AND_CURRENT_PROFILE_INDEX__CURR_MCLK_INDEX_MASK) >>
				TARGET_AND_CURRENT_PROFILE_INDEX__CURR_MCLK_INDEX__SHIFT;
				if (tmp == levels)
					break;
				udelay(1);
			}
		}
		if ((!pi->pcie_dpm_key_disabled) &&
		    pi->dpm_level_enable_mask.pcie_dpm_enable_mask) {
			levels = ci_get_lowest_enabled_level(adev,
							     pi->dpm_level_enable_mask.pcie_dpm_enable_mask);
			ret = ci_dpm_force_state_pcie(adev, levels);
			if (ret)
				return ret;
			for (i = 0; i < adev->usec_timeout; i++) {
				tmp = (RREG32_SMC(ixTARGET_AND_CURRENT_PROFILE_INDEX_1) &
				TARGET_AND_CURRENT_PROFILE_INDEX_1__CURR_PCIE_INDEX_MASK) >>
				TARGET_AND_CURRENT_PROFILE_INDEX_1__CURR_PCIE_INDEX__SHIFT;
				if (tmp == levels)
					break;
				udelay(1);
			}
		}
	} else if (level == AMD_DPM_FORCED_LEVEL_AUTO) {
		if (!pi->pcie_dpm_key_disabled) {
			PPSMC_Result smc_result;

			smc_result = amdgpu_ci_send_msg_to_smc(adev,
							       PPSMC_MSG_PCIeDPM_UnForceLevel);
			if (smc_result != PPSMC_Result_OK)
				return -EINVAL;
		}
		ret = ci_upload_dpm_level_enable_mask(adev);
		if (ret)
			return ret;
	}

	adev->pm.dpm.forced_level = level;

	return 0;
}

static int ci_set_mc_special_registers(struct amdgpu_device *adev,
				       struct ci_mc_reg_table *table)
{
	u8 i, j, k;
	u32 temp_reg;

	for (i = 0, j = table->last; i < table->last; i++) {
		if (j >= SMU7_DISCRETE_MC_REGISTER_ARRAY_SIZE)
			return -EINVAL;
		switch(table->mc_reg_address[i].s1) {
		case mmMC_SEQ_MISC1:
			temp_reg = RREG32(mmMC_PMG_CMD_EMRS);
			table->mc_reg_address[j].s1 = mmMC_PMG_CMD_EMRS;
			table->mc_reg_address[j].s0 = mmMC_SEQ_PMG_CMD_EMRS_LP;
			for (k = 0; k < table->num_entries; k++) {
				table->mc_reg_table_entry[k].mc_data[j] =
					((temp_reg & 0xffff0000)) | ((table->mc_reg_table_entry[k].mc_data[i] & 0xffff0000) >> 16);
			}
			j++;

			if (j >= SMU7_DISCRETE_MC_REGISTER_ARRAY_SIZE)
				return -EINVAL;
			temp_reg = RREG32(mmMC_PMG_CMD_MRS);
			table->mc_reg_address[j].s1 = mmMC_PMG_CMD_MRS;
			table->mc_reg_address[j].s0 = mmMC_SEQ_PMG_CMD_MRS_LP;
			for (k = 0; k < table->num_entries; k++) {
				table->mc_reg_table_entry[k].mc_data[j] =
					(temp_reg & 0xffff0000) | (table->mc_reg_table_entry[k].mc_data[i] & 0x0000ffff);
				if (adev->gmc.vram_type != AMDGPU_VRAM_TYPE_GDDR5)
					table->mc_reg_table_entry[k].mc_data[j] |= 0x100;
			}
			j++;

			if (adev->gmc.vram_type != AMDGPU_VRAM_TYPE_GDDR5) {
				if (j >= SMU7_DISCRETE_MC_REGISTER_ARRAY_SIZE)
					return -EINVAL;
				table->mc_reg_address[j].s1 = mmMC_PMG_AUTO_CMD;
				table->mc_reg_address[j].s0 = mmMC_PMG_AUTO_CMD;
				for (k = 0; k < table->num_entries; k++) {
					table->mc_reg_table_entry[k].mc_data[j] =
						(table->mc_reg_table_entry[k].mc_data[i] & 0xffff0000) >> 16;
				}
				j++;
			}
			break;
		case mmMC_SEQ_RESERVE_M:
			temp_reg = RREG32(mmMC_PMG_CMD_MRS1);
			table->mc_reg_address[j].s1 = mmMC_PMG_CMD_MRS1;
			table->mc_reg_address[j].s0 = mmMC_SEQ_PMG_CMD_MRS1_LP;
			for (k = 0; k < table->num_entries; k++) {
				table->mc_reg_table_entry[k].mc_data[j] =
					(temp_reg & 0xffff0000) | (table->mc_reg_table_entry[k].mc_data[i] & 0x0000ffff);
			}
			j++;
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
	case mmMC_SEQ_RAS_TIMING:
		*out_reg = mmMC_SEQ_RAS_TIMING_LP;
		break;
	case mmMC_SEQ_DLL_STBY:
		*out_reg = mmMC_SEQ_DLL_STBY_LP;
		break;
	case mmMC_SEQ_G5PDX_CMD0:
		*out_reg = mmMC_SEQ_G5PDX_CMD0_LP;
		break;
	case mmMC_SEQ_G5PDX_CMD1:
		*out_reg = mmMC_SEQ_G5PDX_CMD1_LP;
		break;
	case mmMC_SEQ_G5PDX_CTRL:
		*out_reg = mmMC_SEQ_G5PDX_CTRL_LP;
		break;
	case mmMC_SEQ_CAS_TIMING:
		*out_reg = mmMC_SEQ_CAS_TIMING_LP;
	    break;
	case mmMC_SEQ_MISC_TIMING:
		*out_reg = mmMC_SEQ_MISC_TIMING_LP;
		break;
	case mmMC_SEQ_MISC_TIMING2:
		*out_reg = mmMC_SEQ_MISC_TIMING2_LP;
		break;
	case mmMC_SEQ_PMG_DVS_CMD:
		*out_reg = mmMC_SEQ_PMG_DVS_CMD_LP;
		break;
	case mmMC_SEQ_PMG_DVS_CTL:
		*out_reg = mmMC_SEQ_PMG_DVS_CTL_LP;
		break;
	case mmMC_SEQ_RD_CTL_D0:
		*out_reg = mmMC_SEQ_RD_CTL_D0_LP;
		break;
	case mmMC_SEQ_RD_CTL_D1:
		*out_reg = mmMC_SEQ_RD_CTL_D1_LP;
		break;
	case mmMC_SEQ_WR_CTL_D0:
		*out_reg = mmMC_SEQ_WR_CTL_D0_LP;
		break;
	case mmMC_SEQ_WR_CTL_D1:
		*out_reg = mmMC_SEQ_WR_CTL_D1_LP;
		break;
	case mmMC_PMG_CMD_EMRS:
		*out_reg = mmMC_SEQ_PMG_CMD_EMRS_LP;
		break;
	case mmMC_PMG_CMD_MRS:
		*out_reg = mmMC_SEQ_PMG_CMD_MRS_LP;
		break;
	case mmMC_PMG_CMD_MRS1:
		*out_reg = mmMC_SEQ_PMG_CMD_MRS1_LP;
		break;
	case mmMC_SEQ_PMG_TIMING:
		*out_reg = mmMC_SEQ_PMG_TIMING_LP;
		break;
	case mmMC_PMG_CMD_MRS2:
		*out_reg = mmMC_SEQ_PMG_CMD_MRS2_LP;
		break;
	case mmMC_SEQ_WR_CTL_2:
		*out_reg = mmMC_SEQ_WR_CTL_2_LP;
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

static int ci_register_patching_mc_seq(struct amdgpu_device *adev,
				       struct ci_mc_reg_table *table)
{
	u8 i, k;
	u32 tmp;
	bool patch;

	tmp = RREG32(mmMC_SEQ_MISC0);
	patch = ((tmp & 0x0000f00) == 0x300) ? true : false;

	if (patch &&
	    ((adev->pdev->device == 0x67B0) ||
	     (adev->pdev->device == 0x67B1))) {
		for (i = 0; i < table->last; i++) {
			if (table->last >= SMU7_DISCRETE_MC_REGISTER_ARRAY_SIZE)
				return -EINVAL;
			switch (table->mc_reg_address[i].s1) {
			case mmMC_SEQ_MISC1:
				for (k = 0; k < table->num_entries; k++) {
					if ((table->mc_reg_table_entry[k].mclk_max == 125000) ||
					    (table->mc_reg_table_entry[k].mclk_max == 137500))
						table->mc_reg_table_entry[k].mc_data[i] =
							(table->mc_reg_table_entry[k].mc_data[i] & 0xFFFFFFF8) |
							0x00000007;
				}
				break;
			case mmMC_SEQ_WR_CTL_D0:
				for (k = 0; k < table->num_entries; k++) {
					if ((table->mc_reg_table_entry[k].mclk_max == 125000) ||
					    (table->mc_reg_table_entry[k].mclk_max == 137500))
						table->mc_reg_table_entry[k].mc_data[i] =
							(table->mc_reg_table_entry[k].mc_data[i] & 0xFFFF0F00) |
							0x0000D0DD;
				}
				break;
			case mmMC_SEQ_WR_CTL_D1:
				for (k = 0; k < table->num_entries; k++) {
					if ((table->mc_reg_table_entry[k].mclk_max == 125000) ||
					    (table->mc_reg_table_entry[k].mclk_max == 137500))
						table->mc_reg_table_entry[k].mc_data[i] =
							(table->mc_reg_table_entry[k].mc_data[i] & 0xFFFF0F00) |
							0x0000D0DD;
				}
				break;
			case mmMC_SEQ_WR_CTL_2:
				for (k = 0; k < table->num_entries; k++) {
					if ((table->mc_reg_table_entry[k].mclk_max == 125000) ||
					    (table->mc_reg_table_entry[k].mclk_max == 137500))
						table->mc_reg_table_entry[k].mc_data[i] = 0;
				}
				break;
			case mmMC_SEQ_CAS_TIMING:
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
			case mmMC_SEQ_MISC_TIMING:
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

		WREG32(mmMC_SEQ_IO_DEBUG_INDEX, 3);
		tmp = RREG32(mmMC_SEQ_IO_DEBUG_DATA);
		tmp = (tmp & 0xFFF8FFFF) | (1 << 16);
		WREG32(mmMC_SEQ_IO_DEBUG_INDEX, 3);
		WREG32(mmMC_SEQ_IO_DEBUG_DATA, tmp);
	}

	return 0;
}

static int ci_initialize_mc_reg_table(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	struct atom_mc_reg_table *table;
	struct ci_mc_reg_table *ci_table = &pi->mc_reg_table;
	u8 module_index = ci_get_memory_module_index(adev);
	int ret;

	table = kzalloc(sizeof(struct atom_mc_reg_table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	WREG32(mmMC_SEQ_RAS_TIMING_LP, RREG32(mmMC_SEQ_RAS_TIMING));
	WREG32(mmMC_SEQ_CAS_TIMING_LP, RREG32(mmMC_SEQ_CAS_TIMING));
	WREG32(mmMC_SEQ_DLL_STBY_LP, RREG32(mmMC_SEQ_DLL_STBY));
	WREG32(mmMC_SEQ_G5PDX_CMD0_LP, RREG32(mmMC_SEQ_G5PDX_CMD0));
	WREG32(mmMC_SEQ_G5PDX_CMD1_LP, RREG32(mmMC_SEQ_G5PDX_CMD1));
	WREG32(mmMC_SEQ_G5PDX_CTRL_LP, RREG32(mmMC_SEQ_G5PDX_CTRL));
	WREG32(mmMC_SEQ_PMG_DVS_CMD_LP, RREG32(mmMC_SEQ_PMG_DVS_CMD));
	WREG32(mmMC_SEQ_PMG_DVS_CTL_LP, RREG32(mmMC_SEQ_PMG_DVS_CTL));
	WREG32(mmMC_SEQ_MISC_TIMING_LP, RREG32(mmMC_SEQ_MISC_TIMING));
	WREG32(mmMC_SEQ_MISC_TIMING2_LP, RREG32(mmMC_SEQ_MISC_TIMING2));
	WREG32(mmMC_SEQ_PMG_CMD_EMRS_LP, RREG32(mmMC_PMG_CMD_EMRS));
	WREG32(mmMC_SEQ_PMG_CMD_MRS_LP, RREG32(mmMC_PMG_CMD_MRS));
	WREG32(mmMC_SEQ_PMG_CMD_MRS1_LP, RREG32(mmMC_PMG_CMD_MRS1));
	WREG32(mmMC_SEQ_WR_CTL_D0_LP, RREG32(mmMC_SEQ_WR_CTL_D0));
	WREG32(mmMC_SEQ_WR_CTL_D1_LP, RREG32(mmMC_SEQ_WR_CTL_D1));
	WREG32(mmMC_SEQ_RD_CTL_D0_LP, RREG32(mmMC_SEQ_RD_CTL_D0));
	WREG32(mmMC_SEQ_RD_CTL_D1_LP, RREG32(mmMC_SEQ_RD_CTL_D1));
	WREG32(mmMC_SEQ_PMG_TIMING_LP, RREG32(mmMC_SEQ_PMG_TIMING));
	WREG32(mmMC_SEQ_PMG_CMD_MRS2_LP, RREG32(mmMC_PMG_CMD_MRS2));
	WREG32(mmMC_SEQ_WR_CTL_2_LP, RREG32(mmMC_SEQ_WR_CTL_2));

	ret = amdgpu_atombios_init_mc_reg_table(adev, module_index, table);
	if (ret)
		goto init_mc_done;

	ret = ci_copy_vbios_mc_reg_table(table, ci_table);
	if (ret)
		goto init_mc_done;

	ci_set_s0_mc_reg_index(ci_table);

	ret = ci_register_patching_mc_seq(adev, ci_table);
	if (ret)
		goto init_mc_done;

	ret = ci_set_mc_special_registers(adev, ci_table);
	if (ret)
		goto init_mc_done;

	ci_set_valid_flag(ci_table);

init_mc_done:
	kfree(table);

	return ret;
}

static int ci_populate_mc_reg_addresses(struct amdgpu_device *adev,
					SMU7_Discrete_MCRegisters *mc_reg_table)
{
	struct ci_power_info *pi = ci_get_pi(adev);
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

static void ci_convert_mc_reg_table_entry_to_smc(struct amdgpu_device *adev,
						 const u32 memory_clock,
						 SMU7_Discrete_MCRegisterSet *mc_reg_table_data)
{
	struct ci_power_info *pi = ci_get_pi(adev);
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

static void ci_convert_mc_reg_table_to_smc(struct amdgpu_device *adev,
					   SMU7_Discrete_MCRegisters *mc_reg_table)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	u32 i;

	for (i = 0; i < pi->dpm_table.mclk_table.count; i++)
		ci_convert_mc_reg_table_entry_to_smc(adev,
						     pi->dpm_table.mclk_table.dpm_levels[i].value,
						     &mc_reg_table->data[i]);
}

static int ci_populate_initial_mc_reg_table(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	int ret;

	memset(&pi->smc_mc_reg_table, 0, sizeof(SMU7_Discrete_MCRegisters));

	ret = ci_populate_mc_reg_addresses(adev, &pi->smc_mc_reg_table);
	if (ret)
		return ret;
	ci_convert_mc_reg_table_to_smc(adev, &pi->smc_mc_reg_table);

	return amdgpu_ci_copy_bytes_to_smc(adev,
				    pi->mc_reg_table_start,
				    (u8 *)&pi->smc_mc_reg_table,
				    sizeof(SMU7_Discrete_MCRegisters),
				    pi->sram_end);
}

static int ci_update_and_upload_mc_reg_table(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);

	if (!(pi->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_MCLK))
		return 0;

	memset(&pi->smc_mc_reg_table, 0, sizeof(SMU7_Discrete_MCRegisters));

	ci_convert_mc_reg_table_to_smc(adev, &pi->smc_mc_reg_table);

	return amdgpu_ci_copy_bytes_to_smc(adev,
				    pi->mc_reg_table_start +
				    offsetof(SMU7_Discrete_MCRegisters, data[0]),
				    (u8 *)&pi->smc_mc_reg_table.data[0],
				    sizeof(SMU7_Discrete_MCRegisterSet) *
				    pi->dpm_table.mclk_table.count,
				    pi->sram_end);
}

static void ci_enable_voltage_control(struct amdgpu_device *adev)
{
	u32 tmp = RREG32_SMC(ixGENERAL_PWRMGT);

	tmp |= GENERAL_PWRMGT__VOLT_PWRMGT_EN_MASK;
	WREG32_SMC(ixGENERAL_PWRMGT, tmp);
}

static enum amdgpu_pcie_gen ci_get_maximum_link_speed(struct amdgpu_device *adev,
						      struct amdgpu_ps *amdgpu_state)
{
	struct ci_ps *state = ci_get_ps(amdgpu_state);
	int i;
	u16 pcie_speed, max_speed = 0;

	for (i = 0; i < state->performance_level_count; i++) {
		pcie_speed = state->performance_levels[i].pcie_gen;
		if (max_speed < pcie_speed)
			max_speed = pcie_speed;
	}

	return max_speed;
}

static u16 ci_get_current_pcie_speed(struct amdgpu_device *adev)
{
	u32 speed_cntl = 0;

	speed_cntl = RREG32_PCIE(ixPCIE_LC_SPEED_CNTL) &
		PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE_MASK;
	speed_cntl >>= PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE__SHIFT;

	return (u16)speed_cntl;
}

static int ci_get_current_pcie_lane_number(struct amdgpu_device *adev)
{
	u32 link_width = 0;

	link_width = RREG32_PCIE(ixPCIE_LC_LINK_WIDTH_CNTL) &
		PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD_MASK;
	link_width >>= PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD__SHIFT;

	switch (link_width) {
	case 1:
		return 1;
	case 2:
		return 2;
	case 3:
		return 4;
	case 4:
		return 8;
	case 0:
	case 6:
	default:
		return 16;
	}
}

static void ci_request_link_speed_change_before_state_change(struct amdgpu_device *adev,
							     struct amdgpu_ps *amdgpu_new_state,
							     struct amdgpu_ps *amdgpu_current_state)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	enum amdgpu_pcie_gen target_link_speed =
		ci_get_maximum_link_speed(adev, amdgpu_new_state);
	enum amdgpu_pcie_gen current_link_speed;

	if (pi->force_pcie_gen == AMDGPU_PCIE_GEN_INVALID)
		current_link_speed = ci_get_maximum_link_speed(adev, amdgpu_current_state);
	else
		current_link_speed = pi->force_pcie_gen;

	pi->force_pcie_gen = AMDGPU_PCIE_GEN_INVALID;
	pi->pspp_notify_required = false;
	if (target_link_speed > current_link_speed) {
		switch (target_link_speed) {
#ifdef CONFIG_ACPI
		case AMDGPU_PCIE_GEN3:
			if (amdgpu_acpi_pcie_performance_request(adev, PCIE_PERF_REQ_PECI_GEN3, false) == 0)
				break;
			pi->force_pcie_gen = AMDGPU_PCIE_GEN2;
			if (current_link_speed == AMDGPU_PCIE_GEN2)
				break;
		case AMDGPU_PCIE_GEN2:
			if (amdgpu_acpi_pcie_performance_request(adev, PCIE_PERF_REQ_PECI_GEN2, false) == 0)
				break;
#endif
		default:
			pi->force_pcie_gen = ci_get_current_pcie_speed(adev);
			break;
		}
	} else {
		if (target_link_speed < current_link_speed)
			pi->pspp_notify_required = true;
	}
}

static void ci_notify_link_speed_change_after_state_change(struct amdgpu_device *adev,
							   struct amdgpu_ps *amdgpu_new_state,
							   struct amdgpu_ps *amdgpu_current_state)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	enum amdgpu_pcie_gen target_link_speed =
		ci_get_maximum_link_speed(adev, amdgpu_new_state);
	u8 request;

	if (pi->pspp_notify_required) {
		if (target_link_speed == AMDGPU_PCIE_GEN3)
			request = PCIE_PERF_REQ_PECI_GEN3;
		else if (target_link_speed == AMDGPU_PCIE_GEN2)
			request = PCIE_PERF_REQ_PECI_GEN2;
		else
			request = PCIE_PERF_REQ_PECI_GEN1;

		if ((request == PCIE_PERF_REQ_PECI_GEN1) &&
		    (ci_get_current_pcie_speed(adev) > 0))
			return;

#ifdef CONFIG_ACPI
		amdgpu_acpi_pcie_performance_request(adev, request, false);
#endif
	}
}

static int ci_set_private_data_variables_based_on_pptable(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	struct amdgpu_clock_voltage_dependency_table *allowed_sclk_vddc_table =
		&adev->pm.dpm.dyn_state.vddc_dependency_on_sclk;
	struct amdgpu_clock_voltage_dependency_table *allowed_mclk_vddc_table =
		&adev->pm.dpm.dyn_state.vddc_dependency_on_mclk;
	struct amdgpu_clock_voltage_dependency_table *allowed_mclk_vddci_table =
		&adev->pm.dpm.dyn_state.vddci_dependency_on_mclk;

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

	adev->pm.dpm.dyn_state.max_clock_voltage_on_ac.sclk =
		allowed_sclk_vddc_table->entries[allowed_sclk_vddc_table->count - 1].clk;
	adev->pm.dpm.dyn_state.max_clock_voltage_on_ac.mclk =
		allowed_mclk_vddc_table->entries[allowed_sclk_vddc_table->count - 1].clk;
	adev->pm.dpm.dyn_state.max_clock_voltage_on_ac.vddc =
		allowed_sclk_vddc_table->entries[allowed_sclk_vddc_table->count - 1].v;
	adev->pm.dpm.dyn_state.max_clock_voltage_on_ac.vddci =
		allowed_mclk_vddci_table->entries[allowed_mclk_vddci_table->count - 1].v;

	return 0;
}

static void ci_patch_with_vddc_leakage(struct amdgpu_device *adev, u16 *vddc)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	struct ci_leakage_voltage *leakage_table = &pi->vddc_leakage;
	u32 leakage_index;

	for (leakage_index = 0; leakage_index < leakage_table->count; leakage_index++) {
		if (leakage_table->leakage_id[leakage_index] == *vddc) {
			*vddc = leakage_table->actual_voltage[leakage_index];
			break;
		}
	}
}

static void ci_patch_with_vddci_leakage(struct amdgpu_device *adev, u16 *vddci)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	struct ci_leakage_voltage *leakage_table = &pi->vddci_leakage;
	u32 leakage_index;

	for (leakage_index = 0; leakage_index < leakage_table->count; leakage_index++) {
		if (leakage_table->leakage_id[leakage_index] == *vddci) {
			*vddci = leakage_table->actual_voltage[leakage_index];
			break;
		}
	}
}

static void ci_patch_clock_voltage_dependency_table_with_vddc_leakage(struct amdgpu_device *adev,
								      struct amdgpu_clock_voltage_dependency_table *table)
{
	u32 i;

	if (table) {
		for (i = 0; i < table->count; i++)
			ci_patch_with_vddc_leakage(adev, &table->entries[i].v);
	}
}

static void ci_patch_clock_voltage_dependency_table_with_vddci_leakage(struct amdgpu_device *adev,
								       struct amdgpu_clock_voltage_dependency_table *table)
{
	u32 i;

	if (table) {
		for (i = 0; i < table->count; i++)
			ci_patch_with_vddci_leakage(adev, &table->entries[i].v);
	}
}

static void ci_patch_vce_clock_voltage_dependency_table_with_vddc_leakage(struct amdgpu_device *adev,
									  struct amdgpu_vce_clock_voltage_dependency_table *table)
{
	u32 i;

	if (table) {
		for (i = 0; i < table->count; i++)
			ci_patch_with_vddc_leakage(adev, &table->entries[i].v);
	}
}

static void ci_patch_uvd_clock_voltage_dependency_table_with_vddc_leakage(struct amdgpu_device *adev,
									  struct amdgpu_uvd_clock_voltage_dependency_table *table)
{
	u32 i;

	if (table) {
		for (i = 0; i < table->count; i++)
			ci_patch_with_vddc_leakage(adev, &table->entries[i].v);
	}
}

static void ci_patch_vddc_phase_shed_limit_table_with_vddc_leakage(struct amdgpu_device *adev,
								   struct amdgpu_phase_shedding_limits_table *table)
{
	u32 i;

	if (table) {
		for (i = 0; i < table->count; i++)
			ci_patch_with_vddc_leakage(adev, &table->entries[i].voltage);
	}
}

static void ci_patch_clock_voltage_limits_with_vddc_leakage(struct amdgpu_device *adev,
							    struct amdgpu_clock_and_voltage_limits *table)
{
	if (table) {
		ci_patch_with_vddc_leakage(adev, (u16 *)&table->vddc);
		ci_patch_with_vddci_leakage(adev, (u16 *)&table->vddci);
	}
}

static void ci_patch_cac_leakage_table_with_vddc_leakage(struct amdgpu_device *adev,
							 struct amdgpu_cac_leakage_table *table)
{
	u32 i;

	if (table) {
		for (i = 0; i < table->count; i++)
			ci_patch_with_vddc_leakage(adev, &table->entries[i].vddc);
	}
}

static void ci_patch_dependency_tables_with_leakage(struct amdgpu_device *adev)
{

	ci_patch_clock_voltage_dependency_table_with_vddc_leakage(adev,
								  &adev->pm.dpm.dyn_state.vddc_dependency_on_sclk);
	ci_patch_clock_voltage_dependency_table_with_vddc_leakage(adev,
								  &adev->pm.dpm.dyn_state.vddc_dependency_on_mclk);
	ci_patch_clock_voltage_dependency_table_with_vddc_leakage(adev,
								  &adev->pm.dpm.dyn_state.vddc_dependency_on_dispclk);
	ci_patch_clock_voltage_dependency_table_with_vddci_leakage(adev,
								   &adev->pm.dpm.dyn_state.vddci_dependency_on_mclk);
	ci_patch_vce_clock_voltage_dependency_table_with_vddc_leakage(adev,
								      &adev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table);
	ci_patch_uvd_clock_voltage_dependency_table_with_vddc_leakage(adev,
								      &adev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table);
	ci_patch_clock_voltage_dependency_table_with_vddc_leakage(adev,
								  &adev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table);
	ci_patch_clock_voltage_dependency_table_with_vddc_leakage(adev,
								  &adev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table);
	ci_patch_vddc_phase_shed_limit_table_with_vddc_leakage(adev,
							       &adev->pm.dpm.dyn_state.phase_shedding_limits_table);
	ci_patch_clock_voltage_limits_with_vddc_leakage(adev,
							&adev->pm.dpm.dyn_state.max_clock_voltage_on_ac);
	ci_patch_clock_voltage_limits_with_vddc_leakage(adev,
							&adev->pm.dpm.dyn_state.max_clock_voltage_on_dc);
	ci_patch_cac_leakage_table_with_vddc_leakage(adev,
						     &adev->pm.dpm.dyn_state.cac_leakage_table);

}

static void ci_update_current_ps(struct amdgpu_device *adev,
				 struct amdgpu_ps *rps)
{
	struct ci_ps *new_ps = ci_get_ps(rps);
	struct ci_power_info *pi = ci_get_pi(adev);

	pi->current_rps = *rps;
	pi->current_ps = *new_ps;
	pi->current_rps.ps_priv = &pi->current_ps;
	adev->pm.dpm.current_ps = &pi->current_rps;
}

static void ci_update_requested_ps(struct amdgpu_device *adev,
				   struct amdgpu_ps *rps)
{
	struct ci_ps *new_ps = ci_get_ps(rps);
	struct ci_power_info *pi = ci_get_pi(adev);

	pi->requested_rps = *rps;
	pi->requested_ps = *new_ps;
	pi->requested_rps.ps_priv = &pi->requested_ps;
	adev->pm.dpm.requested_ps = &pi->requested_rps;
}

static int ci_dpm_pre_set_power_state(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct ci_power_info *pi = ci_get_pi(adev);
	struct amdgpu_ps requested_ps = *adev->pm.dpm.requested_ps;
	struct amdgpu_ps *new_ps = &requested_ps;

	ci_update_requested_ps(adev, new_ps);

	ci_apply_state_adjust_rules(adev, &pi->requested_rps);

	return 0;
}

static void ci_dpm_post_set_power_state(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct ci_power_info *pi = ci_get_pi(adev);
	struct amdgpu_ps *new_ps = &pi->requested_rps;

	ci_update_current_ps(adev, new_ps);
}


static void ci_dpm_setup_asic(struct amdgpu_device *adev)
{
	ci_read_clock_registers(adev);
	ci_enable_acpi_power_management(adev);
	ci_init_sclk_t(adev);
}

static int ci_dpm_enable(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	struct amdgpu_ps *boot_ps = adev->pm.dpm.boot_ps;
	int ret;

	if (pi->voltage_control != CISLANDS_VOLTAGE_CONTROL_NONE) {
		ci_enable_voltage_control(adev);
		ret = ci_construct_voltage_tables(adev);
		if (ret) {
			DRM_ERROR("ci_construct_voltage_tables failed\n");
			return ret;
		}
	}
	if (pi->caps_dynamic_ac_timing) {
		ret = ci_initialize_mc_reg_table(adev);
		if (ret)
			pi->caps_dynamic_ac_timing = false;
	}
	if (pi->dynamic_ss)
		ci_enable_spread_spectrum(adev, true);
	if (pi->thermal_protection)
		ci_enable_thermal_protection(adev, true);
	ci_program_sstp(adev);
	ci_enable_display_gap(adev);
	ci_program_vc(adev);
	ret = ci_upload_firmware(adev);
	if (ret) {
		DRM_ERROR("ci_upload_firmware failed\n");
		return ret;
	}
	ret = ci_process_firmware_header(adev);
	if (ret) {
		DRM_ERROR("ci_process_firmware_header failed\n");
		return ret;
	}
	ret = ci_initial_switch_from_arb_f0_to_f1(adev);
	if (ret) {
		DRM_ERROR("ci_initial_switch_from_arb_f0_to_f1 failed\n");
		return ret;
	}
	ret = ci_init_smc_table(adev);
	if (ret) {
		DRM_ERROR("ci_init_smc_table failed\n");
		return ret;
	}
	ret = ci_init_arb_table_index(adev);
	if (ret) {
		DRM_ERROR("ci_init_arb_table_index failed\n");
		return ret;
	}
	if (pi->caps_dynamic_ac_timing) {
		ret = ci_populate_initial_mc_reg_table(adev);
		if (ret) {
			DRM_ERROR("ci_populate_initial_mc_reg_table failed\n");
			return ret;
		}
	}
	ret = ci_populate_pm_base(adev);
	if (ret) {
		DRM_ERROR("ci_populate_pm_base failed\n");
		return ret;
	}
	ci_dpm_start_smc(adev);
	ci_enable_vr_hot_gpio_interrupt(adev);
	ret = ci_notify_smc_display_change(adev, false);
	if (ret) {
		DRM_ERROR("ci_notify_smc_display_change failed\n");
		return ret;
	}
	ci_enable_sclk_control(adev, true);
	ret = ci_enable_ulv(adev, true);
	if (ret) {
		DRM_ERROR("ci_enable_ulv failed\n");
		return ret;
	}
	ret = ci_enable_ds_master_switch(adev, true);
	if (ret) {
		DRM_ERROR("ci_enable_ds_master_switch failed\n");
		return ret;
	}
	ret = ci_start_dpm(adev);
	if (ret) {
		DRM_ERROR("ci_start_dpm failed\n");
		return ret;
	}
	ret = ci_enable_didt(adev, true);
	if (ret) {
		DRM_ERROR("ci_enable_didt failed\n");
		return ret;
	}
	ret = ci_enable_smc_cac(adev, true);
	if (ret) {
		DRM_ERROR("ci_enable_smc_cac failed\n");
		return ret;
	}
	ret = ci_enable_power_containment(adev, true);
	if (ret) {
		DRM_ERROR("ci_enable_power_containment failed\n");
		return ret;
	}

	ret = ci_power_control_set_level(adev);
	if (ret) {
		DRM_ERROR("ci_power_control_set_level failed\n");
		return ret;
	}

	ci_enable_auto_throttle_source(adev, AMDGPU_DPM_AUTO_THROTTLE_SRC_THERMAL, true);

	ret = ci_enable_thermal_based_sclk_dpm(adev, true);
	if (ret) {
		DRM_ERROR("ci_enable_thermal_based_sclk_dpm failed\n");
		return ret;
	}

	ci_thermal_start_thermal_controller(adev);

	ci_update_current_ps(adev, boot_ps);

	return 0;
}

static void ci_dpm_disable(struct amdgpu_device *adev)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	struct amdgpu_ps *boot_ps = adev->pm.dpm.boot_ps;

	amdgpu_irq_put(adev, &adev->pm.dpm.thermal.irq,
		       AMDGPU_THERMAL_IRQ_LOW_TO_HIGH);
	amdgpu_irq_put(adev, &adev->pm.dpm.thermal.irq,
		       AMDGPU_THERMAL_IRQ_HIGH_TO_LOW);

	ci_dpm_powergate_uvd(adev, true);

	if (!amdgpu_ci_is_smc_running(adev))
		return;

	ci_thermal_stop_thermal_controller(adev);

	if (pi->thermal_protection)
		ci_enable_thermal_protection(adev, false);
	ci_enable_power_containment(adev, false);
	ci_enable_smc_cac(adev, false);
	ci_enable_didt(adev, false);
	ci_enable_spread_spectrum(adev, false);
	ci_enable_auto_throttle_source(adev, AMDGPU_DPM_AUTO_THROTTLE_SRC_THERMAL, false);
	ci_stop_dpm(adev);
	ci_enable_ds_master_switch(adev, false);
	ci_enable_ulv(adev, false);
	ci_clear_vc(adev);
	ci_reset_to_default(adev);
	ci_dpm_stop_smc(adev);
	ci_force_switch_to_arb_f0(adev);
	ci_enable_thermal_based_sclk_dpm(adev, false);

	ci_update_current_ps(adev, boot_ps);
}

static int ci_dpm_set_power_state(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct ci_power_info *pi = ci_get_pi(adev);
	struct amdgpu_ps *new_ps = &pi->requested_rps;
	struct amdgpu_ps *old_ps = &pi->current_rps;
	int ret;

	ci_find_dpm_states_clocks_in_dpm_table(adev, new_ps);
	if (pi->pcie_performance_request)
		ci_request_link_speed_change_before_state_change(adev, new_ps, old_ps);
	ret = ci_freeze_sclk_mclk_dpm(adev);
	if (ret) {
		DRM_ERROR("ci_freeze_sclk_mclk_dpm failed\n");
		return ret;
	}
	ret = ci_populate_and_upload_sclk_mclk_dpm_levels(adev, new_ps);
	if (ret) {
		DRM_ERROR("ci_populate_and_upload_sclk_mclk_dpm_levels failed\n");
		return ret;
	}
	ret = ci_generate_dpm_level_enable_mask(adev, new_ps);
	if (ret) {
		DRM_ERROR("ci_generate_dpm_level_enable_mask failed\n");
		return ret;
	}

	ret = ci_update_vce_dpm(adev, new_ps, old_ps);
	if (ret) {
		DRM_ERROR("ci_update_vce_dpm failed\n");
		return ret;
	}

	ret = ci_update_sclk_t(adev);
	if (ret) {
		DRM_ERROR("ci_update_sclk_t failed\n");
		return ret;
	}
	if (pi->caps_dynamic_ac_timing) {
		ret = ci_update_and_upload_mc_reg_table(adev);
		if (ret) {
			DRM_ERROR("ci_update_and_upload_mc_reg_table failed\n");
			return ret;
		}
	}
	ret = ci_program_memory_timing_parameters(adev);
	if (ret) {
		DRM_ERROR("ci_program_memory_timing_parameters failed\n");
		return ret;
	}
	ret = ci_unfreeze_sclk_mclk_dpm(adev);
	if (ret) {
		DRM_ERROR("ci_unfreeze_sclk_mclk_dpm failed\n");
		return ret;
	}
	ret = ci_upload_dpm_level_enable_mask(adev);
	if (ret) {
		DRM_ERROR("ci_upload_dpm_level_enable_mask failed\n");
		return ret;
	}
	if (pi->pcie_performance_request)
		ci_notify_link_speed_change_after_state_change(adev, new_ps, old_ps);

	return 0;
}

#if 0
static void ci_dpm_reset_asic(struct amdgpu_device *adev)
{
	ci_set_boot_state(adev);
}
#endif

static void ci_dpm_display_configuration_changed(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	ci_program_display_gap(adev);
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

static void ci_parse_pplib_non_clock_info(struct amdgpu_device *adev,
					  struct amdgpu_ps *rps,
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
		adev->pm.dpm.boot_ps = rps;
	if (rps->class & ATOM_PPLIB_CLASSIFICATION_UVDSTATE)
		adev->pm.dpm.uvd_ps = rps;
}

static void ci_parse_pplib_clock_info(struct amdgpu_device *adev,
				      struct amdgpu_ps *rps, int index,
				      union pplib_clock_info *clock_info)
{
	struct ci_power_info *pi = ci_get_pi(adev);
	struct ci_ps *ps = ci_get_ps(rps);
	struct ci_pl *pl = &ps->performance_levels[index];

	ps->performance_level_count = index + 1;

	pl->sclk = le16_to_cpu(clock_info->ci.usEngineClockLow);
	pl->sclk |= clock_info->ci.ucEngineClockHigh << 16;
	pl->mclk = le16_to_cpu(clock_info->ci.usMemoryClockLow);
	pl->mclk |= clock_info->ci.ucMemoryClockHigh << 16;

	pl->pcie_gen = amdgpu_get_pcie_gen_support(adev,
						   pi->sys_pcie_mask,
						   pi->vbios_boot_state.pcie_gen_bootup_value,
						   clock_info->ci.ucPCIEGen);
	pl->pcie_lane = amdgpu_get_pcie_lane_support(adev,
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

static int ci_parse_power_table(struct amdgpu_device *adev)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
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

	if (!amdgpu_atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset))
		return -EINVAL;
	power_info = (union power_info *)(mode_info->atom_context->bios + data_offset);

	amdgpu_add_thermal_controller(adev);

	state_array = (struct _StateArray *)
		(mode_info->atom_context->bios + data_offset +
		 le16_to_cpu(power_info->pplib.usStateArrayOffset));
	clock_info_array = (struct _ClockInfoArray *)
		(mode_info->atom_context->bios + data_offset +
		 le16_to_cpu(power_info->pplib.usClockInfoArrayOffset));
	non_clock_info_array = (struct _NonClockInfoArray *)
		(mode_info->atom_context->bios + data_offset +
		 le16_to_cpu(power_info->pplib.usNonClockInfoArrayOffset));

	adev->pm.dpm.ps = kzalloc(sizeof(struct amdgpu_ps) *
				  state_array->ucNumEntries, GFP_KERNEL);
	if (!adev->pm.dpm.ps)
		return -ENOMEM;
	power_state_offset = (u8 *)state_array->states;
	for (i = 0; i < state_array->ucNumEntries; i++) {
		u8 *idx;
		power_state = (union pplib_power_state *)power_state_offset;
		non_clock_array_index = power_state->v2.nonClockInfoIndex;
		non_clock_info = (struct _ATOM_PPLIB_NONCLOCK_INFO *)
			&non_clock_info_array->nonClockInfo[non_clock_array_index];
		ps = kzalloc(sizeof(struct ci_ps), GFP_KERNEL);
		if (ps == NULL) {
			kfree(adev->pm.dpm.ps);
			return -ENOMEM;
		}
		adev->pm.dpm.ps[i].ps_priv = ps;
		ci_parse_pplib_non_clock_info(adev, &adev->pm.dpm.ps[i],
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
			ci_parse_pplib_clock_info(adev,
						  &adev->pm.dpm.ps[i], k,
						  clock_info);
			k++;
		}
		power_state_offset += 2 + power_state->v2.ucNumDPMLevels;
	}
	adev->pm.dpm.num_ps = state_array->ucNumEntries;

	/* fill in the vce power states */
	for (i = 0; i < adev->pm.dpm.num_of_vce_states; i++) {
		u32 sclk, mclk;
		clock_array_index = adev->pm.dpm.vce_states[i].clk_idx;
		clock_info = (union pplib_clock_info *)
			&clock_info_array->clockInfo[clock_array_index * clock_info_array->ucEntrySize];
		sclk = le16_to_cpu(clock_info->ci.usEngineClockLow);
		sclk |= clock_info->ci.ucEngineClockHigh << 16;
		mclk = le16_to_cpu(clock_info->ci.usMemoryClockLow);
		mclk |= clock_info->ci.ucMemoryClockHigh << 16;
		adev->pm.dpm.vce_states[i].sclk = sclk;
		adev->pm.dpm.vce_states[i].mclk = mclk;
	}

	return 0;
}

static int ci_get_vbios_boot_values(struct amdgpu_device *adev,
				    struct ci_vbios_boot_state *boot_state)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, FirmwareInfo);
	ATOM_FIRMWARE_INFO_V2_2 *firmware_info;
	u8 frev, crev;
	u16 data_offset;

	if (amdgpu_atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		firmware_info =
			(ATOM_FIRMWARE_INFO_V2_2 *)(mode_info->atom_context->bios +
						    data_offset);
		boot_state->mvdd_bootup_value = le16_to_cpu(firmware_info->usBootUpMVDDCVoltage);
		boot_state->vddc_bootup_value = le16_to_cpu(firmware_info->usBootUpVDDCVoltage);
		boot_state->vddci_bootup_value = le16_to_cpu(firmware_info->usBootUpVDDCIVoltage);
		boot_state->pcie_gen_bootup_value = ci_get_current_pcie_speed(adev);
		boot_state->pcie_lane_bootup_value = ci_get_current_pcie_lane_number(adev);
		boot_state->sclk_bootup_value = le32_to_cpu(firmware_info->ulDefaultEngineClock);
		boot_state->mclk_bootup_value = le32_to_cpu(firmware_info->ulDefaultMemoryClock);

		return 0;
	}
	return -EINVAL;
}

static void ci_dpm_fini(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->pm.dpm.num_ps; i++) {
		kfree(adev->pm.dpm.ps[i].ps_priv);
	}
	kfree(adev->pm.dpm.ps);
	kfree(adev->pm.dpm.priv);
	kfree(adev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries);
	amdgpu_free_extended_power_table(adev);
}

/**
 * ci_dpm_init_microcode - load ucode images from disk
 *
 * @adev: amdgpu_device pointer
 *
 * Use the firmware interface to load the ucode images into
 * the driver (not loaded into hw).
 * Returns 0 on success, error on failure.
 */
static int ci_dpm_init_microcode(struct amdgpu_device *adev)
{
	const char *chip_name;
	char fw_name[30];
	int err;

	DRM_DEBUG("\n");

	switch (adev->asic_type) {
	case CHIP_BONAIRE:
		if ((adev->pdev->revision == 0x80) ||
		    (adev->pdev->revision == 0x81) ||
		    (adev->pdev->device == 0x665f))
			chip_name = "bonaire_k";
		else
			chip_name = "bonaire";
		break;
	case CHIP_HAWAII:
		if (adev->pdev->revision == 0x80)
			chip_name = "hawaii_k";
		else
			chip_name = "hawaii";
		break;
	case CHIP_KAVERI:
	case CHIP_KABINI:
	case CHIP_MULLINS:
	default: BUG();
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_smc.bin", chip_name);
	err = request_firmware(&adev->pm.fw, fw_name, adev->dev);
	if (err)
		goto out;
	err = amdgpu_ucode_validate(adev->pm.fw);

out:
	if (err) {
		pr_err("cik_smc: Failed to load firmware \"%s\"\n", fw_name);
		release_firmware(adev->pm.fw);
		adev->pm.fw = NULL;
	}
	return err;
}

static int ci_dpm_init(struct amdgpu_device *adev)
{
	int index = GetIndexIntoMasterTable(DATA, ASIC_InternalSS_Info);
	SMU7_Discrete_DpmTable *dpm_table;
	struct amdgpu_gpio_rec gpio;
	u16 data_offset, size;
	u8 frev, crev;
	struct ci_power_info *pi;
	int ret;

	pi = kzalloc(sizeof(struct ci_power_info), GFP_KERNEL);
	if (pi == NULL)
		return -ENOMEM;
	adev->pm.dpm.priv = pi;

	pi->sys_pcie_mask =
		(adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_MASK) >>
		CAIL_PCIE_LINK_SPEED_SUPPORT_SHIFT;

	pi->force_pcie_gen = AMDGPU_PCIE_GEN_INVALID;

	pi->pcie_gen_performance.max = AMDGPU_PCIE_GEN1;
	pi->pcie_gen_performance.min = AMDGPU_PCIE_GEN3;
	pi->pcie_gen_powersaving.max = AMDGPU_PCIE_GEN1;
	pi->pcie_gen_powersaving.min = AMDGPU_PCIE_GEN3;

	pi->pcie_lane_performance.max = 0;
	pi->pcie_lane_performance.min = 16;
	pi->pcie_lane_powersaving.max = 0;
	pi->pcie_lane_powersaving.min = 16;

	ret = ci_get_vbios_boot_values(adev, &pi->vbios_boot_state);
	if (ret) {
		ci_dpm_fini(adev);
		return ret;
	}

	ret = amdgpu_get_platform_caps(adev);
	if (ret) {
		ci_dpm_fini(adev);
		return ret;
	}

	ret = amdgpu_parse_extended_power_table(adev);
	if (ret) {
		ci_dpm_fini(adev);
		return ret;
	}

	ret = ci_parse_power_table(adev);
	if (ret) {
		ci_dpm_fini(adev);
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

	if (adev->powerplay.pp_feature & PP_SCLK_DEEP_SLEEP_MASK)
		pi->caps_sclk_ds = true;
	else
		pi->caps_sclk_ds = false;

	pi->mclk_strobe_mode_threshold = 40000;
	pi->mclk_stutter_mode_threshold = 40000;
	pi->mclk_edc_enable_threshold = 40000;
	pi->mclk_edc_wr_enable_threshold = 40000;

	ci_initialize_powertune_defaults(adev);

	pi->caps_fps = false;

	pi->caps_sclk_throttle_low_notification = false;

	pi->caps_uvd_dpm = true;
	pi->caps_vce_dpm = true;

	ci_get_leakage_voltages(adev);
	ci_patch_dependency_tables_with_leakage(adev);
	ci_set_private_data_variables_based_on_pptable(adev);

	adev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries =
		kzalloc(4 * sizeof(struct amdgpu_clock_voltage_dependency_entry), GFP_KERNEL);
	if (!adev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries) {
		ci_dpm_fini(adev);
		return -ENOMEM;
	}
	adev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.count = 4;
	adev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries[0].clk = 0;
	adev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries[0].v = 0;
	adev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries[1].clk = 36000;
	adev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries[1].v = 720;
	adev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries[2].clk = 54000;
	adev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries[2].v = 810;
	adev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries[3].clk = 72000;
	adev->pm.dpm.dyn_state.vddc_dependency_on_dispclk.entries[3].v = 900;

	adev->pm.dpm.dyn_state.mclk_sclk_ratio = 4;
	adev->pm.dpm.dyn_state.sclk_mclk_delta = 15000;
	adev->pm.dpm.dyn_state.vddc_vddci_delta = 200;

	adev->pm.dpm.dyn_state.valid_sclk_values.count = 0;
	adev->pm.dpm.dyn_state.valid_sclk_values.values = NULL;
	adev->pm.dpm.dyn_state.valid_mclk_values.count = 0;
	adev->pm.dpm.dyn_state.valid_mclk_values.values = NULL;

	if (adev->asic_type == CHIP_HAWAII) {
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

	gpio = amdgpu_atombios_lookup_gpio(adev, VDDC_VRHOT_GPIO_PINID);
	if (gpio.valid) {
		dpm_table->VRHotGpio = gpio.shift;
		adev->pm.dpm.platform_caps |= ATOM_PP_PLATFORM_CAP_REGULATOR_HOT;
	} else {
		dpm_table->VRHotGpio = CISLANDS_UNUSED_GPIO_PIN;
		adev->pm.dpm.platform_caps &= ~ATOM_PP_PLATFORM_CAP_REGULATOR_HOT;
	}

	gpio = amdgpu_atombios_lookup_gpio(adev, PP_AC_DC_SWITCH_GPIO_PINID);
	if (gpio.valid) {
		dpm_table->AcDcGpio = gpio.shift;
		adev->pm.dpm.platform_caps |= ATOM_PP_PLATFORM_CAP_HARDWAREDC;
	} else {
		dpm_table->AcDcGpio = CISLANDS_UNUSED_GPIO_PIN;
		adev->pm.dpm.platform_caps &= ~ATOM_PP_PLATFORM_CAP_HARDWAREDC;
	}

	gpio = amdgpu_atombios_lookup_gpio(adev, VDDC_PCC_GPIO_PINID);
	if (gpio.valid) {
		u32 tmp = RREG32_SMC(ixCNB_PWRMGT_CNTL);

		switch (gpio.shift) {
		case 0:
			tmp &= ~CNB_PWRMGT_CNTL__GNB_SLOW_MODE_MASK;
			tmp |= 1 << CNB_PWRMGT_CNTL__GNB_SLOW_MODE__SHIFT;
			break;
		case 1:
			tmp &= ~CNB_PWRMGT_CNTL__GNB_SLOW_MODE_MASK;
			tmp |= 2 << CNB_PWRMGT_CNTL__GNB_SLOW_MODE__SHIFT;
			break;
		case 2:
			tmp |= CNB_PWRMGT_CNTL__GNB_SLOW_MASK;
			break;
		case 3:
			tmp |= CNB_PWRMGT_CNTL__FORCE_NB_PS1_MASK;
			break;
		case 4:
			tmp |= CNB_PWRMGT_CNTL__DPM_ENABLED_MASK;
			break;
		default:
			DRM_INFO("Invalid PCC GPIO: %u!\n", gpio.shift);
			break;
		}
		WREG32_SMC(ixCNB_PWRMGT_CNTL, tmp);
	}

	pi->voltage_control = CISLANDS_VOLTAGE_CONTROL_NONE;
	pi->vddci_control = CISLANDS_VOLTAGE_CONTROL_NONE;
	pi->mvdd_control = CISLANDS_VOLTAGE_CONTROL_NONE;
	if (amdgpu_atombios_is_voltage_gpio(adev, VOLTAGE_TYPE_VDDC, VOLTAGE_OBJ_GPIO_LUT))
		pi->voltage_control = CISLANDS_VOLTAGE_CONTROL_BY_GPIO;
	else if (amdgpu_atombios_is_voltage_gpio(adev, VOLTAGE_TYPE_VDDC, VOLTAGE_OBJ_SVID2))
		pi->voltage_control = CISLANDS_VOLTAGE_CONTROL_BY_SVID2;

	if (adev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_VDDCI_CONTROL) {
		if (amdgpu_atombios_is_voltage_gpio(adev, VOLTAGE_TYPE_VDDCI, VOLTAGE_OBJ_GPIO_LUT))
			pi->vddci_control = CISLANDS_VOLTAGE_CONTROL_BY_GPIO;
		else if (amdgpu_atombios_is_voltage_gpio(adev, VOLTAGE_TYPE_VDDCI, VOLTAGE_OBJ_SVID2))
			pi->vddci_control = CISLANDS_VOLTAGE_CONTROL_BY_SVID2;
		else
			adev->pm.dpm.platform_caps &= ~ATOM_PP_PLATFORM_CAP_VDDCI_CONTROL;
	}

	if (adev->pm.dpm.platform_caps & ATOM_PP_PLATFORM_CAP_MVDDCONTROL) {
		if (amdgpu_atombios_is_voltage_gpio(adev, VOLTAGE_TYPE_MVDDC, VOLTAGE_OBJ_GPIO_LUT))
			pi->mvdd_control = CISLANDS_VOLTAGE_CONTROL_BY_GPIO;
		else if (amdgpu_atombios_is_voltage_gpio(adev, VOLTAGE_TYPE_MVDDC, VOLTAGE_OBJ_SVID2))
			pi->mvdd_control = CISLANDS_VOLTAGE_CONTROL_BY_SVID2;
		else
			adev->pm.dpm.platform_caps &= ~ATOM_PP_PLATFORM_CAP_MVDDCONTROL;
	}

	pi->vddc_phase_shed_control = true;

#if defined(CONFIG_ACPI)
	pi->pcie_performance_request =
		amdgpu_acpi_is_pcie_performance_request_supported(adev);
#else
	pi->pcie_performance_request = false;
#endif

	if (amdgpu_atom_parse_data_header(adev->mode_info.atom_context, index, &size,
				   &frev, &crev, &data_offset)) {
		pi->caps_sclk_ss_support = true;
		pi->caps_mclk_ss_support = true;
		pi->dynamic_ss = true;
	} else {
		pi->caps_sclk_ss_support = false;
		pi->caps_mclk_ss_support = false;
		pi->dynamic_ss = true;
	}

	if (adev->pm.int_thermal_type != THERMAL_TYPE_NONE)
		pi->thermal_protection = true;
	else
		pi->thermal_protection = false;

	pi->caps_dynamic_ac_timing = true;

	pi->uvd_power_gated = true;

	/* make sure dc limits are valid */
	if ((adev->pm.dpm.dyn_state.max_clock_voltage_on_dc.sclk == 0) ||
	    (adev->pm.dpm.dyn_state.max_clock_voltage_on_dc.mclk == 0))
		adev->pm.dpm.dyn_state.max_clock_voltage_on_dc =
			adev->pm.dpm.dyn_state.max_clock_voltage_on_ac;

	pi->fan_ctrl_is_in_default_mode = true;

	return 0;
}

static void
ci_dpm_debugfs_print_current_performance_level(void *handle,
					       struct seq_file *m)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct ci_power_info *pi = ci_get_pi(adev);
	struct amdgpu_ps *rps = &pi->current_rps;
	u32 sclk = ci_get_average_sclk_freq(adev);
	u32 mclk = ci_get_average_mclk_freq(adev);
	u32 activity_percent = 50;
	int ret;

	ret = ci_read_smc_soft_register(adev, offsetof(SMU7_SoftRegisters, AverageGraphicsA),
					&activity_percent);

	if (ret == 0) {
		activity_percent += 0x80;
		activity_percent >>= 8;
		activity_percent = activity_percent > 100 ? 100 : activity_percent;
	}

	seq_printf(m, "uvd %sabled\n", pi->uvd_power_gated ? "dis" : "en");
	seq_printf(m, "vce %sabled\n", rps->vce_active ? "en" : "dis");
	seq_printf(m, "power level avg    sclk: %u mclk: %u\n",
		   sclk, mclk);
	seq_printf(m, "GPU load: %u %%\n", activity_percent);
}

static void ci_dpm_print_power_state(void *handle, void *current_ps)
{
	struct amdgpu_ps *rps = (struct amdgpu_ps *)current_ps;
	struct ci_ps *ps = ci_get_ps(rps);
	struct ci_pl *pl;
	int i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_dpm_print_class_info(rps->class, rps->class2);
	amdgpu_dpm_print_cap_info(rps->caps);
	printk("\tuvd    vclk: %d dclk: %d\n", rps->vclk, rps->dclk);
	for (i = 0; i < ps->performance_level_count; i++) {
		pl = &ps->performance_levels[i];
		printk("\t\tpower level %d    sclk: %u mclk: %u pcie gen: %u pcie lanes: %u\n",
		       i, pl->sclk, pl->mclk, pl->pcie_gen + 1, pl->pcie_lane);
	}
	amdgpu_dpm_print_ps_status(adev, rps);
}

static inline bool ci_are_power_levels_equal(const struct ci_pl *ci_cpl1,
						const struct ci_pl *ci_cpl2)
{
	return ((ci_cpl1->mclk == ci_cpl2->mclk) &&
		  (ci_cpl1->sclk == ci_cpl2->sclk) &&
		  (ci_cpl1->pcie_gen == ci_cpl2->pcie_gen) &&
		  (ci_cpl1->pcie_lane == ci_cpl2->pcie_lane));
}

static int ci_check_state_equal(void *handle,
				void *current_ps,
				void *request_ps,
				bool *equal)
{
	struct ci_ps *ci_cps;
	struct ci_ps *ci_rps;
	int i;
	struct amdgpu_ps *cps = (struct amdgpu_ps *)current_ps;
	struct amdgpu_ps *rps = (struct amdgpu_ps *)request_ps;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev == NULL || cps == NULL || rps == NULL || equal == NULL)
		return -EINVAL;

	ci_cps = ci_get_ps((struct amdgpu_ps *)cps);
	ci_rps = ci_get_ps((struct amdgpu_ps *)rps);

	if (ci_cps == NULL) {
		*equal = false;
		return 0;
	}

	if (ci_cps->performance_level_count != ci_rps->performance_level_count) {

		*equal = false;
		return 0;
	}

	for (i = 0; i < ci_cps->performance_level_count; i++) {
		if (!ci_are_power_levels_equal(&(ci_cps->performance_levels[i]),
					&(ci_rps->performance_levels[i]))) {
			*equal = false;
			return 0;
		}
	}

	/* If all performance levels are the same try to use the UVD clocks to break the tie.*/
	*equal = ((cps->vclk == rps->vclk) && (cps->dclk == rps->dclk));
	*equal &= ((cps->evclk == rps->evclk) && (cps->ecclk == rps->ecclk));

	return 0;
}

static u32 ci_dpm_get_sclk(void *handle, bool low)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct ci_power_info *pi = ci_get_pi(adev);
	struct ci_ps *requested_state = ci_get_ps(&pi->requested_rps);

	if (low)
		return requested_state->performance_levels[0].sclk;
	else
		return requested_state->performance_levels[requested_state->performance_level_count - 1].sclk;
}

static u32 ci_dpm_get_mclk(void *handle, bool low)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct ci_power_info *pi = ci_get_pi(adev);
	struct ci_ps *requested_state = ci_get_ps(&pi->requested_rps);

	if (low)
		return requested_state->performance_levels[0].mclk;
	else
		return requested_state->performance_levels[requested_state->performance_level_count - 1].mclk;
}

/* get temperature in millidegrees */
static int ci_dpm_get_temp(void *handle)
{
	u32 temp;
	int actual_temp = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	temp = (RREG32_SMC(ixCG_MULT_THERMAL_STATUS) & CG_MULT_THERMAL_STATUS__CTF_TEMP_MASK) >>
		CG_MULT_THERMAL_STATUS__CTF_TEMP__SHIFT;

	if (temp & 0x200)
		actual_temp = 255;
	else
		actual_temp = temp & 0x1ff;

	actual_temp = actual_temp * 1000;

	return actual_temp;
}

static int ci_set_temperature_range(struct amdgpu_device *adev)
{
	int ret;

	ret = ci_thermal_enable_alert(adev, false);
	if (ret)
		return ret;
	ret = ci_thermal_set_temperature_range(adev, CISLANDS_TEMP_RANGE_MIN,
					       CISLANDS_TEMP_RANGE_MAX);
	if (ret)
		return ret;
	ret = ci_thermal_enable_alert(adev, true);
	if (ret)
		return ret;
	return ret;
}

static int ci_dpm_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->powerplay.pp_funcs = &ci_dpm_funcs;
	adev->powerplay.pp_handle = adev;
	ci_dpm_set_irq_funcs(adev);

	return 0;
}

static int ci_dpm_late_init(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (!adev->pm.dpm_enabled)
		return 0;

	/* init the sysfs and debugfs files late */
	ret = amdgpu_pm_sysfs_init(adev);
	if (ret)
		return ret;

	ret = ci_set_temperature_range(adev);
	if (ret)
		return ret;

	return 0;
}

static int ci_dpm_sw_init(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	ret = amdgpu_irq_add_id(adev, AMDGPU_IH_CLIENTID_LEGACY, 230,
				&adev->pm.dpm.thermal.irq);
	if (ret)
		return ret;

	ret = amdgpu_irq_add_id(adev, AMDGPU_IH_CLIENTID_LEGACY, 231,
				&adev->pm.dpm.thermal.irq);
	if (ret)
		return ret;

	/* default to balanced state */
	adev->pm.dpm.state = POWER_STATE_TYPE_BALANCED;
	adev->pm.dpm.user_state = POWER_STATE_TYPE_BALANCED;
	adev->pm.dpm.forced_level = AMD_DPM_FORCED_LEVEL_AUTO;
	adev->pm.default_sclk = adev->clock.default_sclk;
	adev->pm.default_mclk = adev->clock.default_mclk;
	adev->pm.current_sclk = adev->clock.default_sclk;
	adev->pm.current_mclk = adev->clock.default_mclk;
	adev->pm.int_thermal_type = THERMAL_TYPE_NONE;

	ret = ci_dpm_init_microcode(adev);
	if (ret)
		return ret;

	if (amdgpu_dpm == 0)
		return 0;

	INIT_WORK(&adev->pm.dpm.thermal.work, amdgpu_dpm_thermal_work_handler);
	mutex_lock(&adev->pm.mutex);
	ret = ci_dpm_init(adev);
	if (ret)
		goto dpm_failed;
	adev->pm.dpm.current_ps = adev->pm.dpm.requested_ps = adev->pm.dpm.boot_ps;
	if (amdgpu_dpm == 1)
		amdgpu_pm_print_power_states(adev);
	mutex_unlock(&adev->pm.mutex);
	DRM_INFO("amdgpu: dpm initialized\n");

	return 0;

dpm_failed:
	ci_dpm_fini(adev);
	mutex_unlock(&adev->pm.mutex);
	DRM_ERROR("amdgpu: dpm initialization failed\n");
	return ret;
}

static int ci_dpm_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	flush_work(&adev->pm.dpm.thermal.work);

	mutex_lock(&adev->pm.mutex);
	ci_dpm_fini(adev);
	mutex_unlock(&adev->pm.mutex);

	release_firmware(adev->pm.fw);
	adev->pm.fw = NULL;

	return 0;
}

static int ci_dpm_hw_init(void *handle)
{
	int ret;

	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (!amdgpu_dpm) {
		ret = ci_upload_firmware(adev);
		if (ret) {
			DRM_ERROR("ci_upload_firmware failed\n");
			return ret;
		}
		ci_dpm_start_smc(adev);
		return 0;
	}

	mutex_lock(&adev->pm.mutex);
	ci_dpm_setup_asic(adev);
	ret = ci_dpm_enable(adev);
	if (ret)
		adev->pm.dpm_enabled = false;
	else
		adev->pm.dpm_enabled = true;
	mutex_unlock(&adev->pm.mutex);

	return ret;
}

static int ci_dpm_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->pm.dpm_enabled) {
		mutex_lock(&adev->pm.mutex);
		ci_dpm_disable(adev);
		mutex_unlock(&adev->pm.mutex);
	} else {
		ci_dpm_stop_smc(adev);
	}

	return 0;
}

static int ci_dpm_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->pm.dpm_enabled) {
		mutex_lock(&adev->pm.mutex);
		amdgpu_irq_put(adev, &adev->pm.dpm.thermal.irq,
			       AMDGPU_THERMAL_IRQ_LOW_TO_HIGH);
		amdgpu_irq_put(adev, &adev->pm.dpm.thermal.irq,
			       AMDGPU_THERMAL_IRQ_HIGH_TO_LOW);
		adev->pm.dpm.last_user_state = adev->pm.dpm.user_state;
		adev->pm.dpm.last_state = adev->pm.dpm.state;
		adev->pm.dpm.user_state = POWER_STATE_TYPE_INTERNAL_BOOT;
		adev->pm.dpm.state = POWER_STATE_TYPE_INTERNAL_BOOT;
		mutex_unlock(&adev->pm.mutex);
		amdgpu_pm_compute_clocks(adev);

	}

	return 0;
}

static int ci_dpm_resume(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->pm.dpm_enabled) {
		/* asic init will reset to the boot state */
		mutex_lock(&adev->pm.mutex);
		ci_dpm_setup_asic(adev);
		ret = ci_dpm_enable(adev);
		if (ret)
			adev->pm.dpm_enabled = false;
		else
			adev->pm.dpm_enabled = true;
		adev->pm.dpm.user_state = adev->pm.dpm.last_user_state;
		adev->pm.dpm.state = adev->pm.dpm.last_state;
		mutex_unlock(&adev->pm.mutex);
		if (adev->pm.dpm_enabled)
			amdgpu_pm_compute_clocks(adev);
	}
	return 0;
}

static bool ci_dpm_is_idle(void *handle)
{
	/* XXX */
	return true;
}

static int ci_dpm_wait_for_idle(void *handle)
{
	/* XXX */
	return 0;
}

static int ci_dpm_soft_reset(void *handle)
{
	return 0;
}

static int ci_dpm_set_interrupt_state(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      unsigned type,
				      enum amdgpu_interrupt_state state)
{
	u32 cg_thermal_int;

	switch (type) {
	case AMDGPU_THERMAL_IRQ_LOW_TO_HIGH:
		switch (state) {
		case AMDGPU_IRQ_STATE_DISABLE:
			cg_thermal_int = RREG32_SMC(ixCG_THERMAL_INT);
			cg_thermal_int |= CG_THERMAL_INT_CTRL__THERM_INTH_MASK_MASK;
			WREG32_SMC(ixCG_THERMAL_INT, cg_thermal_int);
			break;
		case AMDGPU_IRQ_STATE_ENABLE:
			cg_thermal_int = RREG32_SMC(ixCG_THERMAL_INT);
			cg_thermal_int &= ~CG_THERMAL_INT_CTRL__THERM_INTH_MASK_MASK;
			WREG32_SMC(ixCG_THERMAL_INT, cg_thermal_int);
			break;
		default:
			break;
		}
		break;

	case AMDGPU_THERMAL_IRQ_HIGH_TO_LOW:
		switch (state) {
		case AMDGPU_IRQ_STATE_DISABLE:
			cg_thermal_int = RREG32_SMC(ixCG_THERMAL_INT);
			cg_thermal_int |= CG_THERMAL_INT_CTRL__THERM_INTL_MASK_MASK;
			WREG32_SMC(ixCG_THERMAL_INT, cg_thermal_int);
			break;
		case AMDGPU_IRQ_STATE_ENABLE:
			cg_thermal_int = RREG32_SMC(ixCG_THERMAL_INT);
			cg_thermal_int &= ~CG_THERMAL_INT_CTRL__THERM_INTL_MASK_MASK;
			WREG32_SMC(ixCG_THERMAL_INT, cg_thermal_int);
			break;
		default:
			break;
		}
		break;

	default:
		break;
	}
	return 0;
}

static int ci_dpm_process_interrupt(struct amdgpu_device *adev,
				    struct amdgpu_irq_src *source,
				    struct amdgpu_iv_entry *entry)
{
	bool queue_thermal = false;

	if (entry == NULL)
		return -EINVAL;

	switch (entry->src_id) {
	case 230: /* thermal low to high */
		DRM_DEBUG("IH: thermal low to high\n");
		adev->pm.dpm.thermal.high_to_low = false;
		queue_thermal = true;
		break;
	case 231: /* thermal high to low */
		DRM_DEBUG("IH: thermal high to low\n");
		adev->pm.dpm.thermal.high_to_low = true;
		queue_thermal = true;
		break;
	default:
		break;
	}

	if (queue_thermal)
		schedule_work(&adev->pm.dpm.thermal.work);

	return 0;
}

static int ci_dpm_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	return 0;
}

static int ci_dpm_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	return 0;
}

static int ci_dpm_print_clock_levels(void *handle,
		enum pp_clock_type type, char *buf)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct ci_power_info *pi = ci_get_pi(adev);
	struct ci_single_dpm_table *sclk_table = &pi->dpm_table.sclk_table;
	struct ci_single_dpm_table *mclk_table = &pi->dpm_table.mclk_table;
	struct ci_single_dpm_table *pcie_table = &pi->dpm_table.pcie_speed_table;

	int i, now, size = 0;
	uint32_t clock, pcie_speed;

	switch (type) {
	case PP_SCLK:
		amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_API_GetSclkFrequency);
		clock = RREG32(mmSMC_MSG_ARG_0);

		for (i = 0; i < sclk_table->count; i++) {
			if (clock > sclk_table->dpm_levels[i].value)
				continue;
			break;
		}
		now = i;

		for (i = 0; i < sclk_table->count; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
					i, sclk_table->dpm_levels[i].value / 100,
					(i == now) ? "*" : "");
		break;
	case PP_MCLK:
		amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_API_GetMclkFrequency);
		clock = RREG32(mmSMC_MSG_ARG_0);

		for (i = 0; i < mclk_table->count; i++) {
			if (clock > mclk_table->dpm_levels[i].value)
				continue;
			break;
		}
		now = i;

		for (i = 0; i < mclk_table->count; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
					i, mclk_table->dpm_levels[i].value / 100,
					(i == now) ? "*" : "");
		break;
	case PP_PCIE:
		pcie_speed = ci_get_current_pcie_speed(adev);
		for (i = 0; i < pcie_table->count; i++) {
			if (pcie_speed != pcie_table->dpm_levels[i].value)
				continue;
			break;
		}
		now = i;

		for (i = 0; i < pcie_table->count; i++)
			size += sprintf(buf + size, "%d: %s %s\n", i,
					(pcie_table->dpm_levels[i].value == 0) ? "2.5GT/s, x1" :
					(pcie_table->dpm_levels[i].value == 1) ? "5.0GT/s, x16" :
					(pcie_table->dpm_levels[i].value == 2) ? "8.0GT/s, x16" : "",
					(i == now) ? "*" : "");
		break;
	default:
		break;
	}

	return size;
}

static int ci_dpm_force_clock_level(void *handle,
		enum pp_clock_type type, uint32_t mask)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct ci_power_info *pi = ci_get_pi(adev);

	if (adev->pm.dpm.forced_level != AMD_DPM_FORCED_LEVEL_MANUAL)
		return -EINVAL;

	if (mask == 0)
		return -EINVAL;

	switch (type) {
	case PP_SCLK:
		if (!pi->sclk_dpm_key_disabled)
			amdgpu_ci_send_msg_to_smc_with_parameter(adev,
					PPSMC_MSG_SCLKDPM_SetEnabledMask,
					pi->dpm_level_enable_mask.sclk_dpm_enable_mask & mask);
		break;

	case PP_MCLK:
		if (!pi->mclk_dpm_key_disabled)
			amdgpu_ci_send_msg_to_smc_with_parameter(adev,
					PPSMC_MSG_MCLKDPM_SetEnabledMask,
					pi->dpm_level_enable_mask.mclk_dpm_enable_mask & mask);
		break;

	case PP_PCIE:
	{
		uint32_t tmp = mask & pi->dpm_level_enable_mask.pcie_dpm_enable_mask;

		if (!pi->pcie_dpm_key_disabled) {
			if (fls(tmp) != ffs(tmp))
				amdgpu_ci_send_msg_to_smc(adev, PPSMC_MSG_PCIeDPM_UnForceLevel);
			else
				amdgpu_ci_send_msg_to_smc_with_parameter(adev,
					PPSMC_MSG_PCIeDPM_ForceLevel,
					fls(tmp) - 1);
		}
		break;
	}
	default:
		break;
	}

	return 0;
}

static int ci_dpm_get_sclk_od(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct ci_power_info *pi = ci_get_pi(adev);
	struct ci_single_dpm_table *sclk_table = &(pi->dpm_table.sclk_table);
	struct ci_single_dpm_table *golden_sclk_table =
			&(pi->golden_dpm_table.sclk_table);
	int value;

	value = (sclk_table->dpm_levels[sclk_table->count - 1].value -
			golden_sclk_table->dpm_levels[golden_sclk_table->count - 1].value) *
			100 /
			golden_sclk_table->dpm_levels[golden_sclk_table->count - 1].value;

	return value;
}

static int ci_dpm_set_sclk_od(void *handle, uint32_t value)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct ci_power_info *pi = ci_get_pi(adev);
	struct ci_ps *ps = ci_get_ps(adev->pm.dpm.requested_ps);
	struct ci_single_dpm_table *golden_sclk_table =
			&(pi->golden_dpm_table.sclk_table);

	if (value > 20)
		value = 20;

	ps->performance_levels[ps->performance_level_count - 1].sclk =
			golden_sclk_table->dpm_levels[golden_sclk_table->count - 1].value *
			value / 100 +
			golden_sclk_table->dpm_levels[golden_sclk_table->count - 1].value;

	return 0;
}

static int ci_dpm_get_mclk_od(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct ci_power_info *pi = ci_get_pi(adev);
	struct ci_single_dpm_table *mclk_table = &(pi->dpm_table.mclk_table);
	struct ci_single_dpm_table *golden_mclk_table =
			&(pi->golden_dpm_table.mclk_table);
	int value;

	value = (mclk_table->dpm_levels[mclk_table->count - 1].value -
			golden_mclk_table->dpm_levels[golden_mclk_table->count - 1].value) *
			100 /
			golden_mclk_table->dpm_levels[golden_mclk_table->count - 1].value;

	return value;
}

static int ci_dpm_set_mclk_od(void *handle, uint32_t value)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct ci_power_info *pi = ci_get_pi(adev);
	struct ci_ps *ps = ci_get_ps(adev->pm.dpm.requested_ps);
	struct ci_single_dpm_table *golden_mclk_table =
			&(pi->golden_dpm_table.mclk_table);

	if (value > 20)
		value = 20;

	ps->performance_levels[ps->performance_level_count - 1].mclk =
			golden_mclk_table->dpm_levels[golden_mclk_table->count - 1].value *
			value / 100 +
			golden_mclk_table->dpm_levels[golden_mclk_table->count - 1].value;

	return 0;
}

static int ci_dpm_read_sensor(void *handle, int idx,
			      void *value, int *size)
{
	u32 activity_percent = 50;
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* size must be at least 4 bytes for all sensors */
	if (*size < 4)
		return -EINVAL;

	switch (idx) {
	case AMDGPU_PP_SENSOR_GFX_SCLK:
		*((uint32_t *)value) = ci_get_average_sclk_freq(adev);
		*size = 4;
		return 0;
	case AMDGPU_PP_SENSOR_GFX_MCLK:
		*((uint32_t *)value) = ci_get_average_mclk_freq(adev);
		*size = 4;
		return 0;
	case AMDGPU_PP_SENSOR_GPU_TEMP:
		*((uint32_t *)value) = ci_dpm_get_temp(adev);
		*size = 4;
		return 0;
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		ret = ci_read_smc_soft_register(adev,
						offsetof(SMU7_SoftRegisters,
							 AverageGraphicsA),
						&activity_percent);
		if (ret == 0) {
			activity_percent += 0x80;
			activity_percent >>= 8;
			activity_percent =
				activity_percent > 100 ? 100 : activity_percent;
		}
		*((uint32_t *)value) = activity_percent;
		*size = 4;
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct amd_ip_funcs ci_dpm_ip_funcs = {
	.name = "ci_dpm",
	.early_init = ci_dpm_early_init,
	.late_init = ci_dpm_late_init,
	.sw_init = ci_dpm_sw_init,
	.sw_fini = ci_dpm_sw_fini,
	.hw_init = ci_dpm_hw_init,
	.hw_fini = ci_dpm_hw_fini,
	.suspend = ci_dpm_suspend,
	.resume = ci_dpm_resume,
	.is_idle = ci_dpm_is_idle,
	.wait_for_idle = ci_dpm_wait_for_idle,
	.soft_reset = ci_dpm_soft_reset,
	.set_clockgating_state = ci_dpm_set_clockgating_state,
	.set_powergating_state = ci_dpm_set_powergating_state,
};

const struct amdgpu_ip_block_version ci_smu_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_SMC,
	.major = 7,
	.minor = 0,
	.rev = 0,
	.funcs = &ci_dpm_ip_funcs,
};

static const struct amd_pm_funcs ci_dpm_funcs = {
	.pre_set_power_state = &ci_dpm_pre_set_power_state,
	.set_power_state = &ci_dpm_set_power_state,
	.post_set_power_state = &ci_dpm_post_set_power_state,
	.display_configuration_changed = &ci_dpm_display_configuration_changed,
	.get_sclk = &ci_dpm_get_sclk,
	.get_mclk = &ci_dpm_get_mclk,
	.print_power_state = &ci_dpm_print_power_state,
	.debugfs_print_current_performance_level = &ci_dpm_debugfs_print_current_performance_level,
	.force_performance_level = &ci_dpm_force_performance_level,
	.vblank_too_short = &ci_dpm_vblank_too_short,
	.powergate_uvd = &ci_dpm_powergate_uvd,
	.set_fan_control_mode = &ci_dpm_set_fan_control_mode,
	.get_fan_control_mode = &ci_dpm_get_fan_control_mode,
	.set_fan_speed_percent = &ci_dpm_set_fan_speed_percent,
	.get_fan_speed_percent = &ci_dpm_get_fan_speed_percent,
	.print_clock_levels = ci_dpm_print_clock_levels,
	.force_clock_level = ci_dpm_force_clock_level,
	.get_sclk_od = ci_dpm_get_sclk_od,
	.set_sclk_od = ci_dpm_set_sclk_od,
	.get_mclk_od = ci_dpm_get_mclk_od,
	.set_mclk_od = ci_dpm_set_mclk_od,
	.check_state_equal = ci_check_state_equal,
	.get_vce_clock_state = amdgpu_get_vce_clock_state,
	.read_sensor = ci_dpm_read_sensor,
};

static const struct amdgpu_irq_src_funcs ci_dpm_irq_funcs = {
	.set = ci_dpm_set_interrupt_state,
	.process = ci_dpm_process_interrupt,
};

static void ci_dpm_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->pm.dpm.thermal.irq.num_types = AMDGPU_THERMAL_IRQ_LAST;
	adev->pm.dpm.thermal.irq.funcs = &ci_dpm_irq_funcs;
}
