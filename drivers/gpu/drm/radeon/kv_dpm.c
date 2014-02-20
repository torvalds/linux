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

#include "drmP.h"
#include "radeon.h"
#include "cikd.h"
#include "r600_dpm.h"
#include "kv_dpm.h"
#include "radeon_asic.h"
#include <linux/seq_file.h>

#define KV_MAX_DEEPSLEEP_DIVIDER_ID     5
#define KV_MINIMUM_ENGINE_CLOCK         800
#define SMC_RAM_END                     0x40000

static void kv_init_graphics_levels(struct radeon_device *rdev);
static int kv_calculate_ds_divider(struct radeon_device *rdev);
static int kv_calculate_nbps_level_settings(struct radeon_device *rdev);
static int kv_calculate_dpm_settings(struct radeon_device *rdev);
static void kv_enable_new_levels(struct radeon_device *rdev);
static void kv_program_nbps_index_settings(struct radeon_device *rdev,
					   struct radeon_ps *new_rps);
static int kv_set_enabled_level(struct radeon_device *rdev, u32 level);
static int kv_set_enabled_levels(struct radeon_device *rdev);
static int kv_force_dpm_highest(struct radeon_device *rdev);
static int kv_force_dpm_lowest(struct radeon_device *rdev);
static void kv_apply_state_adjust_rules(struct radeon_device *rdev,
					struct radeon_ps *new_rps,
					struct radeon_ps *old_rps);
static int kv_set_thermal_temperature_range(struct radeon_device *rdev,
					    int min_temp, int max_temp);
static int kv_init_fps_limits(struct radeon_device *rdev);

void kv_dpm_powergate_uvd(struct radeon_device *rdev, bool gate);
static void kv_dpm_powergate_vce(struct radeon_device *rdev, bool gate);
static void kv_dpm_powergate_samu(struct radeon_device *rdev, bool gate);
static void kv_dpm_powergate_acp(struct radeon_device *rdev, bool gate);

extern void cik_enter_rlc_safe_mode(struct radeon_device *rdev);
extern void cik_exit_rlc_safe_mode(struct radeon_device *rdev);
extern void cik_update_cg(struct radeon_device *rdev,
			  u32 block, bool enable);

static const struct kv_lcac_config_values sx_local_cac_cfg_kv[] =
{
	{  0,       4,        1    },
	{  1,       4,        1    },
	{  2,       5,        1    },
	{  3,       4,        2    },
	{  4,       1,        1    },
	{  5,       5,        2    },
	{  6,       6,        1    },
	{  7,       9,        2    },
	{ 0xffffffff }
};

static const struct kv_lcac_config_values mc0_local_cac_cfg_kv[] =
{
	{  0,       4,        1    },
	{ 0xffffffff }
};

static const struct kv_lcac_config_values mc1_local_cac_cfg_kv[] =
{
	{  0,       4,        1    },
	{ 0xffffffff }
};

static const struct kv_lcac_config_values mc2_local_cac_cfg_kv[] =
{
	{  0,       4,        1    },
	{ 0xffffffff }
};

static const struct kv_lcac_config_values mc3_local_cac_cfg_kv[] =
{
	{  0,       4,        1    },
	{ 0xffffffff }
};

static const struct kv_lcac_config_values cpl_local_cac_cfg_kv[] =
{
	{  0,       4,        1    },
	{  1,       4,        1    },
	{  2,       5,        1    },
	{  3,       4,        1    },
	{  4,       1,        1    },
	{  5,       5,        1    },
	{  6,       6,        1    },
	{  7,       9,        1    },
	{  8,       4,        1    },
	{  9,       2,        1    },
	{  10,      3,        1    },
	{  11,      6,        1    },
	{  12,      8,        2    },
	{  13,      1,        1    },
	{  14,      2,        1    },
	{  15,      3,        1    },
	{  16,      1,        1    },
	{  17,      4,        1    },
	{  18,      3,        1    },
	{  19,      1,        1    },
	{  20,      8,        1    },
	{  21,      5,        1    },
	{  22,      1,        1    },
	{  23,      1,        1    },
	{  24,      4,        1    },
	{  27,      6,        1    },
	{  28,      1,        1    },
	{ 0xffffffff }
};

static const struct kv_lcac_config_reg sx0_cac_config_reg[] =
{
	{ 0xc0400d00, 0x003e0000, 17, 0x3fc00000, 22, 0x0001fffe, 1, 0x00000001, 0 }
};

static const struct kv_lcac_config_reg mc0_cac_config_reg[] =
{
	{ 0xc0400d30, 0x003e0000, 17, 0x3fc00000, 22, 0x0001fffe, 1, 0x00000001, 0 }
};

static const struct kv_lcac_config_reg mc1_cac_config_reg[] =
{
	{ 0xc0400d3c, 0x003e0000, 17, 0x3fc00000, 22, 0x0001fffe, 1, 0x00000001, 0 }
};

static const struct kv_lcac_config_reg mc2_cac_config_reg[] =
{
	{ 0xc0400d48, 0x003e0000, 17, 0x3fc00000, 22, 0x0001fffe, 1, 0x00000001, 0 }
};

static const struct kv_lcac_config_reg mc3_cac_config_reg[] =
{
	{ 0xc0400d54, 0x003e0000, 17, 0x3fc00000, 22, 0x0001fffe, 1, 0x00000001, 0 }
};

static const struct kv_lcac_config_reg cpl_cac_config_reg[] =
{
	{ 0xc0400d80, 0x003e0000, 17, 0x3fc00000, 22, 0x0001fffe, 1, 0x00000001, 0 }
};

static const struct kv_pt_config_reg didt_config_kv[] =
{
	{ 0x10, 0x000000ff, 0, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x10, 0x0000ff00, 8, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x10, 0x00ff0000, 16, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x10, 0xff000000, 24, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x11, 0x000000ff, 0, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x11, 0x0000ff00, 8, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x11, 0x00ff0000, 16, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x11, 0xff000000, 24, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x12, 0x000000ff, 0, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x12, 0x0000ff00, 8, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x12, 0x00ff0000, 16, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x12, 0xff000000, 24, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x2, 0x00003fff, 0, 0x4, KV_CONFIGREG_DIDT_IND },
	{ 0x2, 0x03ff0000, 16, 0x80, KV_CONFIGREG_DIDT_IND },
	{ 0x2, 0x78000000, 27, 0x3, KV_CONFIGREG_DIDT_IND },
	{ 0x1, 0x0000ffff, 0, 0x3FFF, KV_CONFIGREG_DIDT_IND },
	{ 0x1, 0xffff0000, 16, 0x3FFF, KV_CONFIGREG_DIDT_IND },
	{ 0x0, 0x00000001, 0, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x30, 0x000000ff, 0, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x30, 0x0000ff00, 8, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x30, 0x00ff0000, 16, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x30, 0xff000000, 24, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x31, 0x000000ff, 0, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x31, 0x0000ff00, 8, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x31, 0x00ff0000, 16, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x31, 0xff000000, 24, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x32, 0x000000ff, 0, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x32, 0x0000ff00, 8, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x32, 0x00ff0000, 16, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x32, 0xff000000, 24, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x22, 0x00003fff, 0, 0x4, KV_CONFIGREG_DIDT_IND },
	{ 0x22, 0x03ff0000, 16, 0x80, KV_CONFIGREG_DIDT_IND },
	{ 0x22, 0x78000000, 27, 0x3, KV_CONFIGREG_DIDT_IND },
	{ 0x21, 0x0000ffff, 0, 0x3FFF, KV_CONFIGREG_DIDT_IND },
	{ 0x21, 0xffff0000, 16, 0x3FFF, KV_CONFIGREG_DIDT_IND },
	{ 0x20, 0x00000001, 0, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x50, 0x000000ff, 0, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x50, 0x0000ff00, 8, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x50, 0x00ff0000, 16, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x50, 0xff000000, 24, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x51, 0x000000ff, 0, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x51, 0x0000ff00, 8, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x51, 0x00ff0000, 16, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x51, 0xff000000, 24, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x52, 0x000000ff, 0, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x52, 0x0000ff00, 8, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x52, 0x00ff0000, 16, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x52, 0xff000000, 24, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x42, 0x00003fff, 0, 0x4, KV_CONFIGREG_DIDT_IND },
	{ 0x42, 0x03ff0000, 16, 0x80, KV_CONFIGREG_DIDT_IND },
	{ 0x42, 0x78000000, 27, 0x3, KV_CONFIGREG_DIDT_IND },
	{ 0x41, 0x0000ffff, 0, 0x3FFF, KV_CONFIGREG_DIDT_IND },
	{ 0x41, 0xffff0000, 16, 0x3FFF, KV_CONFIGREG_DIDT_IND },
	{ 0x40, 0x00000001, 0, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x70, 0x000000ff, 0, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x70, 0x0000ff00, 8, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x70, 0x00ff0000, 16, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x70, 0xff000000, 24, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x71, 0x000000ff, 0, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x71, 0x0000ff00, 8, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x71, 0x00ff0000, 16, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x71, 0xff000000, 24, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x72, 0x000000ff, 0, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x72, 0x0000ff00, 8, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x72, 0x00ff0000, 16, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x72, 0xff000000, 24, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0x62, 0x00003fff, 0, 0x4, KV_CONFIGREG_DIDT_IND },
	{ 0x62, 0x03ff0000, 16, 0x80, KV_CONFIGREG_DIDT_IND },
	{ 0x62, 0x78000000, 27, 0x3, KV_CONFIGREG_DIDT_IND },
	{ 0x61, 0x0000ffff, 0, 0x3FFF, KV_CONFIGREG_DIDT_IND },
	{ 0x61, 0xffff0000, 16, 0x3FFF, KV_CONFIGREG_DIDT_IND },
	{ 0x60, 0x00000001, 0, 0x0, KV_CONFIGREG_DIDT_IND },
	{ 0xFFFFFFFF }
};

static struct kv_ps *kv_get_ps(struct radeon_ps *rps)
{
	struct kv_ps *ps = rps->ps_priv;

	return ps;
}

static struct kv_power_info *kv_get_pi(struct radeon_device *rdev)
{
	struct kv_power_info *pi = rdev->pm.dpm.priv;

	return pi;
}

#if 0
static void kv_program_local_cac_table(struct radeon_device *rdev,
				       const struct kv_lcac_config_values *local_cac_table,
				       const struct kv_lcac_config_reg *local_cac_reg)
{
	u32 i, count, data;
	const struct kv_lcac_config_values *values = local_cac_table;

	while (values->block_id != 0xffffffff) {
		count = values->signal_id;
		for (i = 0; i < count; i++) {
			data = ((values->block_id << local_cac_reg->block_shift) &
				local_cac_reg->block_mask);
			data |= ((i << local_cac_reg->signal_shift) &
				 local_cac_reg->signal_mask);
			data |= ((values->t << local_cac_reg->t_shift) &
				 local_cac_reg->t_mask);
			data |= ((1 << local_cac_reg->enable_shift) &
				 local_cac_reg->enable_mask);
			WREG32_SMC(local_cac_reg->cntl, data);
		}
		values++;
	}
}
#endif

static int kv_program_pt_config_registers(struct radeon_device *rdev,
					  const struct kv_pt_config_reg *cac_config_regs)
{
	const struct kv_pt_config_reg *config_regs = cac_config_regs;
	u32 data;
	u32 cache = 0;

	if (config_regs == NULL)
		return -EINVAL;

	while (config_regs->offset != 0xFFFFFFFF) {
		if (config_regs->type == KV_CONFIGREG_CACHE) {
			cache |= ((config_regs->value << config_regs->shift) & config_regs->mask);
		} else {
			switch (config_regs->type) {
			case KV_CONFIGREG_SMC_IND:
				data = RREG32_SMC(config_regs->offset);
				break;
			case KV_CONFIGREG_DIDT_IND:
				data = RREG32_DIDT(config_regs->offset);
				break;
			default:
				data = RREG32(config_regs->offset << 2);
				break;
			}

			data &= ~config_regs->mask;
			data |= ((config_regs->value << config_regs->shift) & config_regs->mask);
			data |= cache;
			cache = 0;

			switch (config_regs->type) {
			case KV_CONFIGREG_SMC_IND:
				WREG32_SMC(config_regs->offset, data);
				break;
			case KV_CONFIGREG_DIDT_IND:
				WREG32_DIDT(config_regs->offset, data);
				break;
			default:
				WREG32(config_regs->offset << 2, data);
				break;
			}
		}
		config_regs++;
	}

	return 0;
}

static void kv_do_enable_didt(struct radeon_device *rdev, bool enable)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
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

static int kv_enable_didt(struct radeon_device *rdev, bool enable)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	int ret;

	if (pi->caps_sq_ramping ||
	    pi->caps_db_ramping ||
	    pi->caps_td_ramping ||
	    pi->caps_tcp_ramping) {
		cik_enter_rlc_safe_mode(rdev);

		if (enable) {
			ret = kv_program_pt_config_registers(rdev, didt_config_kv);
			if (ret) {
				cik_exit_rlc_safe_mode(rdev);
				return ret;
			}
		}

		kv_do_enable_didt(rdev, enable);

		cik_exit_rlc_safe_mode(rdev);
	}

	return 0;
}

#if 0
static void kv_initialize_hardware_cac_manager(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);

	if (pi->caps_cac) {
		WREG32_SMC(LCAC_SX0_OVR_SEL, 0);
		WREG32_SMC(LCAC_SX0_OVR_VAL, 0);
		kv_program_local_cac_table(rdev, sx_local_cac_cfg_kv, sx0_cac_config_reg);

		WREG32_SMC(LCAC_MC0_OVR_SEL, 0);
		WREG32_SMC(LCAC_MC0_OVR_VAL, 0);
		kv_program_local_cac_table(rdev, mc0_local_cac_cfg_kv, mc0_cac_config_reg);

		WREG32_SMC(LCAC_MC1_OVR_SEL, 0);
		WREG32_SMC(LCAC_MC1_OVR_VAL, 0);
		kv_program_local_cac_table(rdev, mc1_local_cac_cfg_kv, mc1_cac_config_reg);

		WREG32_SMC(LCAC_MC2_OVR_SEL, 0);
		WREG32_SMC(LCAC_MC2_OVR_VAL, 0);
		kv_program_local_cac_table(rdev, mc2_local_cac_cfg_kv, mc2_cac_config_reg);

		WREG32_SMC(LCAC_MC3_OVR_SEL, 0);
		WREG32_SMC(LCAC_MC3_OVR_VAL, 0);
		kv_program_local_cac_table(rdev, mc3_local_cac_cfg_kv, mc3_cac_config_reg);

		WREG32_SMC(LCAC_CPL_OVR_SEL, 0);
		WREG32_SMC(LCAC_CPL_OVR_VAL, 0);
		kv_program_local_cac_table(rdev, cpl_local_cac_cfg_kv, cpl_cac_config_reg);
	}
}
#endif

static int kv_enable_smc_cac(struct radeon_device *rdev, bool enable)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	int ret = 0;

	if (pi->caps_cac) {
		if (enable) {
			ret = kv_notify_message_to_smu(rdev, PPSMC_MSG_EnableCac);
			if (ret)
				pi->cac_enabled = false;
			else
				pi->cac_enabled = true;
		} else if (pi->cac_enabled) {
			kv_notify_message_to_smu(rdev, PPSMC_MSG_DisableCac);
			pi->cac_enabled = false;
		}
	}

	return ret;
}

static int kv_process_firmware_header(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	u32 tmp;
	int ret;

	ret = kv_read_smc_sram_dword(rdev, SMU7_FIRMWARE_HEADER_LOCATION +
				     offsetof(SMU7_Firmware_Header, DpmTable),
				     &tmp, pi->sram_end);

	if (ret == 0)
		pi->dpm_table_start = tmp;

	ret = kv_read_smc_sram_dword(rdev, SMU7_FIRMWARE_HEADER_LOCATION +
				     offsetof(SMU7_Firmware_Header, SoftRegisters),
				     &tmp, pi->sram_end);

	if (ret == 0)
		pi->soft_regs_start = tmp;

	return ret;
}

static int kv_enable_dpm_voltage_scaling(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	int ret;

	pi->graphics_voltage_change_enable = 1;

	ret = kv_copy_bytes_to_smc(rdev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, GraphicsVoltageChangeEnable),
				   &pi->graphics_voltage_change_enable,
				   sizeof(u8), pi->sram_end);

	return ret;
}

static int kv_set_dpm_interval(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	int ret;

	pi->graphics_interval = 1;

	ret = kv_copy_bytes_to_smc(rdev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, GraphicsInterval),
				   &pi->graphics_interval,
				   sizeof(u8), pi->sram_end);

	return ret;
}

static int kv_set_dpm_boot_state(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	int ret;

	ret = kv_copy_bytes_to_smc(rdev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, GraphicsBootLevel),
				   &pi->graphics_boot_level,
				   sizeof(u8), pi->sram_end);

	return ret;
}

static void kv_program_vc(struct radeon_device *rdev)
{
	WREG32_SMC(CG_FTV_0, 0x3FFFC100);
}

static void kv_clear_vc(struct radeon_device *rdev)
{
	WREG32_SMC(CG_FTV_0, 0);
}

static int kv_set_divider_value(struct radeon_device *rdev,
				u32 index, u32 sclk)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	struct atom_clock_dividers dividers;
	int ret;

	ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
					     sclk, false, &dividers);
	if (ret)
		return ret;

	pi->graphics_level[index].SclkDid = (u8)dividers.post_div;
	pi->graphics_level[index].SclkFrequency = cpu_to_be32(sclk);

	return 0;
}

static u16 kv_convert_8bit_index_to_voltage(struct radeon_device *rdev,
					    u16 voltage)
{
	return 6200 - (voltage * 25);
}

static u16 kv_convert_2bit_index_to_voltage(struct radeon_device *rdev,
					    u32 vid_2bit)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	u32 vid_8bit = sumo_convert_vid2_to_vid7(rdev,
						 &pi->sys_info.vid_mapping_table,
						 vid_2bit);

	return kv_convert_8bit_index_to_voltage(rdev, (u16)vid_8bit);
}


static int kv_set_vid(struct radeon_device *rdev, u32 index, u32 vid)
{
	struct kv_power_info *pi = kv_get_pi(rdev);

	pi->graphics_level[index].VoltageDownH = (u8)pi->voltage_drop_t;
	pi->graphics_level[index].MinVddNb =
		cpu_to_be32(kv_convert_2bit_index_to_voltage(rdev, vid));

	return 0;
}

static int kv_set_at(struct radeon_device *rdev, u32 index, u32 at)
{
	struct kv_power_info *pi = kv_get_pi(rdev);

	pi->graphics_level[index].AT = cpu_to_be16((u16)at);

	return 0;
}

static void kv_dpm_power_level_enable(struct radeon_device *rdev,
				      u32 index, bool enable)
{
	struct kv_power_info *pi = kv_get_pi(rdev);

	pi->graphics_level[index].EnabledForActivity = enable ? 1 : 0;
}

static void kv_start_dpm(struct radeon_device *rdev)
{
	u32 tmp = RREG32_SMC(GENERAL_PWRMGT);

	tmp |= GLOBAL_PWRMGT_EN;
	WREG32_SMC(GENERAL_PWRMGT, tmp);

	kv_smc_dpm_enable(rdev, true);
}

static void kv_stop_dpm(struct radeon_device *rdev)
{
	kv_smc_dpm_enable(rdev, false);
}

static void kv_start_am(struct radeon_device *rdev)
{
	u32 sclk_pwrmgt_cntl = RREG32_SMC(SCLK_PWRMGT_CNTL);

	sclk_pwrmgt_cntl &= ~(RESET_SCLK_CNT | RESET_BUSY_CNT);
	sclk_pwrmgt_cntl |= DYNAMIC_PM_EN;

	WREG32_SMC(SCLK_PWRMGT_CNTL, sclk_pwrmgt_cntl);
}

static void kv_reset_am(struct radeon_device *rdev)
{
	u32 sclk_pwrmgt_cntl = RREG32_SMC(SCLK_PWRMGT_CNTL);

	sclk_pwrmgt_cntl |= (RESET_SCLK_CNT | RESET_BUSY_CNT);

	WREG32_SMC(SCLK_PWRMGT_CNTL, sclk_pwrmgt_cntl);
}

static int kv_freeze_sclk_dpm(struct radeon_device *rdev, bool freeze)
{
	return kv_notify_message_to_smu(rdev, freeze ?
					PPSMC_MSG_SCLKDPM_FreezeLevel : PPSMC_MSG_SCLKDPM_UnfreezeLevel);
}

static int kv_force_lowest_valid(struct radeon_device *rdev)
{
	return kv_force_dpm_lowest(rdev);
}

static int kv_unforce_levels(struct radeon_device *rdev)
{
	if (rdev->family == CHIP_KABINI)
		return kv_notify_message_to_smu(rdev, PPSMC_MSG_NoForcedLevel);
	else
		return kv_set_enabled_levels(rdev);
}

static int kv_update_sclk_t(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	u32 low_sclk_interrupt_t = 0;
	int ret = 0;

	if (pi->caps_sclk_throttle_low_notification) {
		low_sclk_interrupt_t = cpu_to_be32(pi->low_sclk_interrupt_t);

		ret = kv_copy_bytes_to_smc(rdev,
					   pi->dpm_table_start +
					   offsetof(SMU7_Fusion_DpmTable, LowSclkInterruptT),
					   (u8 *)&low_sclk_interrupt_t,
					   sizeof(u32), pi->sram_end);
	}
	return ret;
}

static int kv_program_bootup_state(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	u32 i;
	struct radeon_clock_voltage_dependency_table *table =
		&rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk;

	if (table && table->count) {
		for (i = pi->graphics_dpm_level_count - 1; i > 0; i--) {
			if (table->entries[i].clk == pi->boot_pl.sclk)
				break;
		}

		pi->graphics_boot_level = (u8)i;
		kv_dpm_power_level_enable(rdev, i, true);
	} else {
		struct sumo_sclk_voltage_mapping_table *table =
			&pi->sys_info.sclk_voltage_mapping_table;

		if (table->num_max_dpm_entries == 0)
			return -EINVAL;

		for (i = pi->graphics_dpm_level_count - 1; i > 0; i--) {
			if (table->entries[i].sclk_frequency == pi->boot_pl.sclk)
				break;
		}

		pi->graphics_boot_level = (u8)i;
		kv_dpm_power_level_enable(rdev, i, true);
	}
	return 0;
}

static int kv_enable_auto_thermal_throttling(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	int ret;

	pi->graphics_therm_throttle_enable = 1;

	ret = kv_copy_bytes_to_smc(rdev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, GraphicsThermThrottleEnable),
				   &pi->graphics_therm_throttle_enable,
				   sizeof(u8), pi->sram_end);

	return ret;
}

static int kv_upload_dpm_settings(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	int ret;

	ret = kv_copy_bytes_to_smc(rdev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, GraphicsLevel),
				   (u8 *)&pi->graphics_level,
				   sizeof(SMU7_Fusion_GraphicsLevel) * SMU7_MAX_LEVELS_GRAPHICS,
				   pi->sram_end);

	if (ret)
		return ret;

	ret = kv_copy_bytes_to_smc(rdev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, GraphicsDpmLevelCount),
				   &pi->graphics_dpm_level_count,
				   sizeof(u8), pi->sram_end);

	return ret;
}

static u32 kv_get_clock_difference(u32 a, u32 b)
{
	return (a >= b) ? a - b : b - a;
}

static u32 kv_get_clk_bypass(struct radeon_device *rdev, u32 clk)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	u32 value;

	if (pi->caps_enable_dfs_bypass) {
		if (kv_get_clock_difference(clk, 40000) < 200)
			value = 3;
		else if (kv_get_clock_difference(clk, 30000) < 200)
			value = 2;
		else if (kv_get_clock_difference(clk, 20000) < 200)
			value = 7;
		else if (kv_get_clock_difference(clk, 15000) < 200)
			value = 6;
		else if (kv_get_clock_difference(clk, 10000) < 200)
			value = 8;
		else
			value = 0;
	} else {
		value = 0;
	}

	return value;
}

static int kv_populate_uvd_table(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	struct radeon_uvd_clock_voltage_dependency_table *table =
		&rdev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table;
	struct atom_clock_dividers dividers;
	int ret;
	u32 i;

	if (table == NULL || table->count == 0)
		return 0;

	pi->uvd_level_count = 0;
	for (i = 0; i < table->count; i++) {
		if (pi->high_voltage_t &&
		    (pi->high_voltage_t < table->entries[i].v))
			break;

		pi->uvd_level[i].VclkFrequency = cpu_to_be32(table->entries[i].vclk);
		pi->uvd_level[i].DclkFrequency = cpu_to_be32(table->entries[i].dclk);
		pi->uvd_level[i].MinVddNb = cpu_to_be16(table->entries[i].v);

		pi->uvd_level[i].VClkBypassCntl =
			(u8)kv_get_clk_bypass(rdev, table->entries[i].vclk);
		pi->uvd_level[i].DClkBypassCntl =
			(u8)kv_get_clk_bypass(rdev, table->entries[i].dclk);

		ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
						     table->entries[i].vclk, false, &dividers);
		if (ret)
			return ret;
		pi->uvd_level[i].VclkDivider = (u8)dividers.post_div;

		ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
						     table->entries[i].dclk, false, &dividers);
		if (ret)
			return ret;
		pi->uvd_level[i].DclkDivider = (u8)dividers.post_div;

		pi->uvd_level_count++;
	}

	ret = kv_copy_bytes_to_smc(rdev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, UvdLevelCount),
				   (u8 *)&pi->uvd_level_count,
				   sizeof(u8), pi->sram_end);
	if (ret)
		return ret;

	pi->uvd_interval = 1;

	ret = kv_copy_bytes_to_smc(rdev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, UVDInterval),
				   &pi->uvd_interval,
				   sizeof(u8), pi->sram_end);
	if (ret)
		return ret;

	ret = kv_copy_bytes_to_smc(rdev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, UvdLevel),
				   (u8 *)&pi->uvd_level,
				   sizeof(SMU7_Fusion_UvdLevel) * SMU7_MAX_LEVELS_UVD,
				   pi->sram_end);

	return ret;

}

static int kv_populate_vce_table(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	int ret;
	u32 i;
	struct radeon_vce_clock_voltage_dependency_table *table =
		&rdev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table;
	struct atom_clock_dividers dividers;

	if (table == NULL || table->count == 0)
		return 0;

	pi->vce_level_count = 0;
	for (i = 0; i < table->count; i++) {
		if (pi->high_voltage_t &&
		    pi->high_voltage_t < table->entries[i].v)
			break;

		pi->vce_level[i].Frequency = cpu_to_be32(table->entries[i].evclk);
		pi->vce_level[i].MinVoltage = cpu_to_be16(table->entries[i].v);

		pi->vce_level[i].ClkBypassCntl =
			(u8)kv_get_clk_bypass(rdev, table->entries[i].evclk);

		ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
						     table->entries[i].evclk, false, &dividers);
		if (ret)
			return ret;
		pi->vce_level[i].Divider = (u8)dividers.post_div;

		pi->vce_level_count++;
	}

	ret = kv_copy_bytes_to_smc(rdev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, VceLevelCount),
				   (u8 *)&pi->vce_level_count,
				   sizeof(u8),
				   pi->sram_end);
	if (ret)
		return ret;

	pi->vce_interval = 1;

	ret = kv_copy_bytes_to_smc(rdev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, VCEInterval),
				   (u8 *)&pi->vce_interval,
				   sizeof(u8),
				   pi->sram_end);
	if (ret)
		return ret;

	ret = kv_copy_bytes_to_smc(rdev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, VceLevel),
				   (u8 *)&pi->vce_level,
				   sizeof(SMU7_Fusion_ExtClkLevel) * SMU7_MAX_LEVELS_VCE,
				   pi->sram_end);

	return ret;
}

static int kv_populate_samu_table(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	struct radeon_clock_voltage_dependency_table *table =
		&rdev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table;
	struct atom_clock_dividers dividers;
	int ret;
	u32 i;

	if (table == NULL || table->count == 0)
		return 0;

	pi->samu_level_count = 0;
	for (i = 0; i < table->count; i++) {
		if (pi->high_voltage_t &&
		    pi->high_voltage_t < table->entries[i].v)
			break;

		pi->samu_level[i].Frequency = cpu_to_be32(table->entries[i].clk);
		pi->samu_level[i].MinVoltage = cpu_to_be16(table->entries[i].v);

		pi->samu_level[i].ClkBypassCntl =
			(u8)kv_get_clk_bypass(rdev, table->entries[i].clk);

		ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
						     table->entries[i].clk, false, &dividers);
		if (ret)
			return ret;
		pi->samu_level[i].Divider = (u8)dividers.post_div;

		pi->samu_level_count++;
	}

	ret = kv_copy_bytes_to_smc(rdev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, SamuLevelCount),
				   (u8 *)&pi->samu_level_count,
				   sizeof(u8),
				   pi->sram_end);
	if (ret)
		return ret;

	pi->samu_interval = 1;

	ret = kv_copy_bytes_to_smc(rdev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, SAMUInterval),
				   (u8 *)&pi->samu_interval,
				   sizeof(u8),
				   pi->sram_end);
	if (ret)
		return ret;

	ret = kv_copy_bytes_to_smc(rdev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, SamuLevel),
				   (u8 *)&pi->samu_level,
				   sizeof(SMU7_Fusion_ExtClkLevel) * SMU7_MAX_LEVELS_SAMU,
				   pi->sram_end);
	if (ret)
		return ret;

	return ret;
}


static int kv_populate_acp_table(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	struct radeon_clock_voltage_dependency_table *table =
		&rdev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table;
	struct atom_clock_dividers dividers;
	int ret;
	u32 i;

	if (table == NULL || table->count == 0)
		return 0;

	pi->acp_level_count = 0;
	for (i = 0; i < table->count; i++) {
		pi->acp_level[i].Frequency = cpu_to_be32(table->entries[i].clk);
		pi->acp_level[i].MinVoltage = cpu_to_be16(table->entries[i].v);

		ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
						     table->entries[i].clk, false, &dividers);
		if (ret)
			return ret;
		pi->acp_level[i].Divider = (u8)dividers.post_div;

		pi->acp_level_count++;
	}

	ret = kv_copy_bytes_to_smc(rdev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, AcpLevelCount),
				   (u8 *)&pi->acp_level_count,
				   sizeof(u8),
				   pi->sram_end);
	if (ret)
		return ret;

	pi->acp_interval = 1;

	ret = kv_copy_bytes_to_smc(rdev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, ACPInterval),
				   (u8 *)&pi->acp_interval,
				   sizeof(u8),
				   pi->sram_end);
	if (ret)
		return ret;

	ret = kv_copy_bytes_to_smc(rdev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, AcpLevel),
				   (u8 *)&pi->acp_level,
				   sizeof(SMU7_Fusion_ExtClkLevel) * SMU7_MAX_LEVELS_ACP,
				   pi->sram_end);
	if (ret)
		return ret;

	return ret;
}

static void kv_calculate_dfs_bypass_settings(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	u32 i;
	struct radeon_clock_voltage_dependency_table *table =
		&rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk;

	if (table && table->count) {
		for (i = 0; i < pi->graphics_dpm_level_count; i++) {
			if (pi->caps_enable_dfs_bypass) {
				if (kv_get_clock_difference(table->entries[i].clk, 40000) < 200)
					pi->graphics_level[i].ClkBypassCntl = 3;
				else if (kv_get_clock_difference(table->entries[i].clk, 30000) < 200)
					pi->graphics_level[i].ClkBypassCntl = 2;
				else if (kv_get_clock_difference(table->entries[i].clk, 26600) < 200)
					pi->graphics_level[i].ClkBypassCntl = 7;
				else if (kv_get_clock_difference(table->entries[i].clk , 20000) < 200)
					pi->graphics_level[i].ClkBypassCntl = 6;
				else if (kv_get_clock_difference(table->entries[i].clk , 10000) < 200)
					pi->graphics_level[i].ClkBypassCntl = 8;
				else
					pi->graphics_level[i].ClkBypassCntl = 0;
			} else {
				pi->graphics_level[i].ClkBypassCntl = 0;
			}
		}
	} else {
		struct sumo_sclk_voltage_mapping_table *table =
			&pi->sys_info.sclk_voltage_mapping_table;
		for (i = 0; i < pi->graphics_dpm_level_count; i++) {
			if (pi->caps_enable_dfs_bypass) {
				if (kv_get_clock_difference(table->entries[i].sclk_frequency, 40000) < 200)
					pi->graphics_level[i].ClkBypassCntl = 3;
				else if (kv_get_clock_difference(table->entries[i].sclk_frequency, 30000) < 200)
					pi->graphics_level[i].ClkBypassCntl = 2;
				else if (kv_get_clock_difference(table->entries[i].sclk_frequency, 26600) < 200)
					pi->graphics_level[i].ClkBypassCntl = 7;
				else if (kv_get_clock_difference(table->entries[i].sclk_frequency, 20000) < 200)
					pi->graphics_level[i].ClkBypassCntl = 6;
				else if (kv_get_clock_difference(table->entries[i].sclk_frequency, 10000) < 200)
					pi->graphics_level[i].ClkBypassCntl = 8;
				else
					pi->graphics_level[i].ClkBypassCntl = 0;
			} else {
				pi->graphics_level[i].ClkBypassCntl = 0;
			}
		}
	}
}

static int kv_enable_ulv(struct radeon_device *rdev, bool enable)
{
	return kv_notify_message_to_smu(rdev, enable ?
					PPSMC_MSG_EnableULV : PPSMC_MSG_DisableULV);
}

static void kv_reset_acp_boot_level(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);

	pi->acp_boot_level = 0xff;
}

static void kv_update_current_ps(struct radeon_device *rdev,
				 struct radeon_ps *rps)
{
	struct kv_ps *new_ps = kv_get_ps(rps);
	struct kv_power_info *pi = kv_get_pi(rdev);

	pi->current_rps = *rps;
	pi->current_ps = *new_ps;
	pi->current_rps.ps_priv = &pi->current_ps;
}

static void kv_update_requested_ps(struct radeon_device *rdev,
				   struct radeon_ps *rps)
{
	struct kv_ps *new_ps = kv_get_ps(rps);
	struct kv_power_info *pi = kv_get_pi(rdev);

	pi->requested_rps = *rps;
	pi->requested_ps = *new_ps;
	pi->requested_rps.ps_priv = &pi->requested_ps;
}

void kv_dpm_enable_bapm(struct radeon_device *rdev, bool enable)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	int ret;

	if (pi->bapm_enable) {
		ret = kv_smc_bapm_enable(rdev, enable);
		if (ret)
			DRM_ERROR("kv_smc_bapm_enable failed\n");
	}
}

int kv_dpm_enable(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	int ret;

	ret = kv_process_firmware_header(rdev);
	if (ret) {
		DRM_ERROR("kv_process_firmware_header failed\n");
		return ret;
	}
	kv_init_fps_limits(rdev);
	kv_init_graphics_levels(rdev);
	ret = kv_program_bootup_state(rdev);
	if (ret) {
		DRM_ERROR("kv_program_bootup_state failed\n");
		return ret;
	}
	kv_calculate_dfs_bypass_settings(rdev);
	ret = kv_upload_dpm_settings(rdev);
	if (ret) {
		DRM_ERROR("kv_upload_dpm_settings failed\n");
		return ret;
	}
	ret = kv_populate_uvd_table(rdev);
	if (ret) {
		DRM_ERROR("kv_populate_uvd_table failed\n");
		return ret;
	}
	ret = kv_populate_vce_table(rdev);
	if (ret) {
		DRM_ERROR("kv_populate_vce_table failed\n");
		return ret;
	}
	ret = kv_populate_samu_table(rdev);
	if (ret) {
		DRM_ERROR("kv_populate_samu_table failed\n");
		return ret;
	}
	ret = kv_populate_acp_table(rdev);
	if (ret) {
		DRM_ERROR("kv_populate_acp_table failed\n");
		return ret;
	}
	kv_program_vc(rdev);
#if 0
	kv_initialize_hardware_cac_manager(rdev);
#endif
	kv_start_am(rdev);
	if (pi->enable_auto_thermal_throttling) {
		ret = kv_enable_auto_thermal_throttling(rdev);
		if (ret) {
			DRM_ERROR("kv_enable_auto_thermal_throttling failed\n");
			return ret;
		}
	}
	ret = kv_enable_dpm_voltage_scaling(rdev);
	if (ret) {
		DRM_ERROR("kv_enable_dpm_voltage_scaling failed\n");
		return ret;
	}
	ret = kv_set_dpm_interval(rdev);
	if (ret) {
		DRM_ERROR("kv_set_dpm_interval failed\n");
		return ret;
	}
	ret = kv_set_dpm_boot_state(rdev);
	if (ret) {
		DRM_ERROR("kv_set_dpm_boot_state failed\n");
		return ret;
	}
	ret = kv_enable_ulv(rdev, true);
	if (ret) {
		DRM_ERROR("kv_enable_ulv failed\n");
		return ret;
	}
	kv_start_dpm(rdev);
	ret = kv_enable_didt(rdev, true);
	if (ret) {
		DRM_ERROR("kv_enable_didt failed\n");
		return ret;
	}
	ret = kv_enable_smc_cac(rdev, true);
	if (ret) {
		DRM_ERROR("kv_enable_smc_cac failed\n");
		return ret;
	}

	kv_reset_acp_boot_level(rdev);

	ret = kv_smc_bapm_enable(rdev, false);
	if (ret) {
		DRM_ERROR("kv_smc_bapm_enable failed\n");
		return ret;
	}

	kv_update_current_ps(rdev, rdev->pm.dpm.boot_ps);

	return ret;
}

int kv_dpm_late_enable(struct radeon_device *rdev)
{
	int ret;

	if (rdev->irq.installed &&
	    r600_is_internal_thermal_sensor(rdev->pm.int_thermal_type)) {
		ret = kv_set_thermal_temperature_range(rdev, R600_TEMP_RANGE_MIN, R600_TEMP_RANGE_MAX);
		if (ret) {
			DRM_ERROR("kv_set_thermal_temperature_range failed\n");
			return ret;
		}
		rdev->irq.dpm_thermal = true;
		radeon_irq_set(rdev);
	}

	/* powerdown unused blocks for now */
	kv_dpm_powergate_acp(rdev, true);
	kv_dpm_powergate_samu(rdev, true);
	kv_dpm_powergate_vce(rdev, true);
	kv_dpm_powergate_uvd(rdev, true);

	return ret;
}

void kv_dpm_disable(struct radeon_device *rdev)
{
	kv_smc_bapm_enable(rdev, false);

	/* powerup blocks */
	kv_dpm_powergate_acp(rdev, false);
	kv_dpm_powergate_samu(rdev, false);
	kv_dpm_powergate_vce(rdev, false);
	kv_dpm_powergate_uvd(rdev, false);

	kv_enable_smc_cac(rdev, false);
	kv_enable_didt(rdev, false);
	kv_clear_vc(rdev);
	kv_stop_dpm(rdev);
	kv_enable_ulv(rdev, false);
	kv_reset_am(rdev);

	kv_update_current_ps(rdev, rdev->pm.dpm.boot_ps);
}

#if 0
static int kv_write_smc_soft_register(struct radeon_device *rdev,
				      u16 reg_offset, u32 value)
{
	struct kv_power_info *pi = kv_get_pi(rdev);

	return kv_copy_bytes_to_smc(rdev, pi->soft_regs_start + reg_offset,
				    (u8 *)&value, sizeof(u16), pi->sram_end);
}

static int kv_read_smc_soft_register(struct radeon_device *rdev,
				     u16 reg_offset, u32 *value)
{
	struct kv_power_info *pi = kv_get_pi(rdev);

	return kv_read_smc_sram_dword(rdev, pi->soft_regs_start + reg_offset,
				      value, pi->sram_end);
}
#endif

static void kv_init_sclk_t(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);

	pi->low_sclk_interrupt_t = 0;
}

static int kv_init_fps_limits(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	int ret = 0;

	if (pi->caps_fps) {
		u16 tmp;

		tmp = 45;
		pi->fps_high_t = cpu_to_be16(tmp);
		ret = kv_copy_bytes_to_smc(rdev,
					   pi->dpm_table_start +
					   offsetof(SMU7_Fusion_DpmTable, FpsHighT),
					   (u8 *)&pi->fps_high_t,
					   sizeof(u16), pi->sram_end);

		tmp = 30;
		pi->fps_low_t = cpu_to_be16(tmp);

		ret = kv_copy_bytes_to_smc(rdev,
					   pi->dpm_table_start +
					   offsetof(SMU7_Fusion_DpmTable, FpsLowT),
					   (u8 *)&pi->fps_low_t,
					   sizeof(u16), pi->sram_end);

	}
	return ret;
}

static void kv_init_powergate_state(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);

	pi->uvd_power_gated = false;
	pi->vce_power_gated = false;
	pi->samu_power_gated = false;
	pi->acp_power_gated = false;

}

static int kv_enable_uvd_dpm(struct radeon_device *rdev, bool enable)
{
	return kv_notify_message_to_smu(rdev, enable ?
					PPSMC_MSG_UVDDPM_Enable : PPSMC_MSG_UVDDPM_Disable);
}

#if 0
static int kv_enable_vce_dpm(struct radeon_device *rdev, bool enable)
{
	return kv_notify_message_to_smu(rdev, enable ?
					PPSMC_MSG_VCEDPM_Enable : PPSMC_MSG_VCEDPM_Disable);
}
#endif

static int kv_enable_samu_dpm(struct radeon_device *rdev, bool enable)
{
	return kv_notify_message_to_smu(rdev, enable ?
					PPSMC_MSG_SAMUDPM_Enable : PPSMC_MSG_SAMUDPM_Disable);
}

static int kv_enable_acp_dpm(struct radeon_device *rdev, bool enable)
{
	return kv_notify_message_to_smu(rdev, enable ?
					PPSMC_MSG_ACPDPM_Enable : PPSMC_MSG_ACPDPM_Disable);
}

static int kv_update_uvd_dpm(struct radeon_device *rdev, bool gate)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	struct radeon_uvd_clock_voltage_dependency_table *table =
		&rdev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table;
	int ret;

	if (!gate) {
		if (!pi->caps_uvd_dpm || table->count || pi->caps_stable_p_state)
			pi->uvd_boot_level = table->count - 1;
		else
			pi->uvd_boot_level = 0;

		ret = kv_copy_bytes_to_smc(rdev,
					   pi->dpm_table_start +
					   offsetof(SMU7_Fusion_DpmTable, UvdBootLevel),
					   (uint8_t *)&pi->uvd_boot_level,
					   sizeof(u8), pi->sram_end);
		if (ret)
			return ret;

		if (!pi->caps_uvd_dpm ||
		    pi->caps_stable_p_state)
			kv_send_msg_to_smc_with_parameter(rdev,
							  PPSMC_MSG_UVDDPM_SetEnabledMask,
							  (1 << pi->uvd_boot_level));
	}

	return kv_enable_uvd_dpm(rdev, !gate);
}

#if 0
static u8 kv_get_vce_boot_level(struct radeon_device *rdev)
{
	u8 i;
	struct radeon_vce_clock_voltage_dependency_table *table =
		&rdev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table;

	for (i = 0; i < table->count; i++) {
		if (table->entries[i].evclk >= 0) /* XXX */
			break;
	}

	return i;
}

static int kv_update_vce_dpm(struct radeon_device *rdev,
			     struct radeon_ps *radeon_new_state,
			     struct radeon_ps *radeon_current_state)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	struct radeon_vce_clock_voltage_dependency_table *table =
		&rdev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table;
	int ret;

	if (radeon_new_state->evclk > 0 && radeon_current_state->evclk == 0) {
		if (pi->caps_stable_p_state)
			pi->vce_boot_level = table->count - 1;
		else
			pi->vce_boot_level = kv_get_vce_boot_level(rdev);

		ret = kv_copy_bytes_to_smc(rdev,
					   pi->dpm_table_start +
					   offsetof(SMU7_Fusion_DpmTable, VceBootLevel),
					   (u8 *)&pi->vce_boot_level,
					   sizeof(u8),
					   pi->sram_end);
		if (ret)
			return ret;

		if (pi->caps_stable_p_state)
			kv_send_msg_to_smc_with_parameter(rdev,
							  PPSMC_MSG_VCEDPM_SetEnabledMask,
							  (1 << pi->vce_boot_level));

		kv_enable_vce_dpm(rdev, true);
	} else if (radeon_new_state->evclk == 0 && radeon_current_state->evclk > 0) {
		kv_enable_vce_dpm(rdev, false);
	}

	return 0;
}
#endif

static int kv_update_samu_dpm(struct radeon_device *rdev, bool gate)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	struct radeon_clock_voltage_dependency_table *table =
		&rdev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table;
	int ret;

	if (!gate) {
		if (pi->caps_stable_p_state)
			pi->samu_boot_level = table->count - 1;
		else
			pi->samu_boot_level = 0;

		ret = kv_copy_bytes_to_smc(rdev,
					   pi->dpm_table_start +
					   offsetof(SMU7_Fusion_DpmTable, SamuBootLevel),
					   (u8 *)&pi->samu_boot_level,
					   sizeof(u8),
					   pi->sram_end);
		if (ret)
			return ret;

		if (pi->caps_stable_p_state)
			kv_send_msg_to_smc_with_parameter(rdev,
							  PPSMC_MSG_SAMUDPM_SetEnabledMask,
							  (1 << pi->samu_boot_level));
	}

	return kv_enable_samu_dpm(rdev, !gate);
}

static u8 kv_get_acp_boot_level(struct radeon_device *rdev)
{
	u8 i;
	struct radeon_clock_voltage_dependency_table *table =
		&rdev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table;

	for (i = 0; i < table->count; i++) {
		if (table->entries[i].clk >= 0) /* XXX */
			break;
	}

	if (i >= table->count)
		i = table->count - 1;

	return i;
}

static void kv_update_acp_boot_level(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	u8 acp_boot_level;

	if (!pi->caps_stable_p_state) {
		acp_boot_level = kv_get_acp_boot_level(rdev);
		if (acp_boot_level != pi->acp_boot_level) {
			pi->acp_boot_level = acp_boot_level;
			kv_send_msg_to_smc_with_parameter(rdev,
							  PPSMC_MSG_ACPDPM_SetEnabledMask,
							  (1 << pi->acp_boot_level));
		}
	}
}

static int kv_update_acp_dpm(struct radeon_device *rdev, bool gate)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	struct radeon_clock_voltage_dependency_table *table =
		&rdev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table;
	int ret;

	if (!gate) {
		if (pi->caps_stable_p_state)
			pi->acp_boot_level = table->count - 1;
		else
			pi->acp_boot_level = kv_get_acp_boot_level(rdev);

		ret = kv_copy_bytes_to_smc(rdev,
					   pi->dpm_table_start +
					   offsetof(SMU7_Fusion_DpmTable, AcpBootLevel),
					   (u8 *)&pi->acp_boot_level,
					   sizeof(u8),
					   pi->sram_end);
		if (ret)
			return ret;

		if (pi->caps_stable_p_state)
			kv_send_msg_to_smc_with_parameter(rdev,
							  PPSMC_MSG_ACPDPM_SetEnabledMask,
							  (1 << pi->acp_boot_level));
	}

	return kv_enable_acp_dpm(rdev, !gate);
}

void kv_dpm_powergate_uvd(struct radeon_device *rdev, bool gate)
{
	struct kv_power_info *pi = kv_get_pi(rdev);

	if (pi->uvd_power_gated == gate)
		return;

	pi->uvd_power_gated = gate;

	if (gate) {
		if (pi->caps_uvd_pg) {
			uvd_v1_0_stop(rdev);
			cik_update_cg(rdev, RADEON_CG_BLOCK_UVD, false);
		}
		kv_update_uvd_dpm(rdev, gate);
		if (pi->caps_uvd_pg)
			kv_notify_message_to_smu(rdev, PPSMC_MSG_UVDPowerOFF);
	} else {
		if (pi->caps_uvd_pg) {
			kv_notify_message_to_smu(rdev, PPSMC_MSG_UVDPowerON);
			uvd_v4_2_resume(rdev);
			uvd_v1_0_start(rdev);
			cik_update_cg(rdev, RADEON_CG_BLOCK_UVD, true);
		}
		kv_update_uvd_dpm(rdev, gate);
	}
}

static void kv_dpm_powergate_vce(struct radeon_device *rdev, bool gate)
{
	struct kv_power_info *pi = kv_get_pi(rdev);

	if (pi->vce_power_gated == gate)
		return;

	pi->vce_power_gated = gate;

	if (gate) {
		if (pi->caps_vce_pg)
			kv_notify_message_to_smu(rdev, PPSMC_MSG_VCEPowerOFF);
	} else {
		if (pi->caps_vce_pg)
			kv_notify_message_to_smu(rdev, PPSMC_MSG_VCEPowerON);
	}
}

static void kv_dpm_powergate_samu(struct radeon_device *rdev, bool gate)
{
	struct kv_power_info *pi = kv_get_pi(rdev);

	if (pi->samu_power_gated == gate)
		return;

	pi->samu_power_gated = gate;

	if (gate) {
		kv_update_samu_dpm(rdev, true);
		if (pi->caps_samu_pg)
			kv_notify_message_to_smu(rdev, PPSMC_MSG_SAMPowerOFF);
	} else {
		if (pi->caps_samu_pg)
			kv_notify_message_to_smu(rdev, PPSMC_MSG_SAMPowerON);
		kv_update_samu_dpm(rdev, false);
	}
}

static void kv_dpm_powergate_acp(struct radeon_device *rdev, bool gate)
{
	struct kv_power_info *pi = kv_get_pi(rdev);

	if (pi->acp_power_gated == gate)
		return;

	if (rdev->family == CHIP_KABINI)
		return;

	pi->acp_power_gated = gate;

	if (gate) {
		kv_update_acp_dpm(rdev, true);
		if (pi->caps_acp_pg)
			kv_notify_message_to_smu(rdev, PPSMC_MSG_ACPPowerOFF);
	} else {
		if (pi->caps_acp_pg)
			kv_notify_message_to_smu(rdev, PPSMC_MSG_ACPPowerON);
		kv_update_acp_dpm(rdev, false);
	}
}

static void kv_set_valid_clock_range(struct radeon_device *rdev,
				     struct radeon_ps *new_rps)
{
	struct kv_ps *new_ps = kv_get_ps(new_rps);
	struct kv_power_info *pi = kv_get_pi(rdev);
	u32 i;
	struct radeon_clock_voltage_dependency_table *table =
		&rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk;

	if (table && table->count) {
		for (i = 0; i < pi->graphics_dpm_level_count; i++) {
			if ((table->entries[i].clk >= new_ps->levels[0].sclk) ||
			    (i == (pi->graphics_dpm_level_count - 1))) {
				pi->lowest_valid = i;
				break;
			}
		}

		for (i = pi->graphics_dpm_level_count - 1; i > 0; i--) {
			if (table->entries[i].clk <= new_ps->levels[new_ps->num_levels - 1].sclk)
				break;
		}
		pi->highest_valid = i;

		if (pi->lowest_valid > pi->highest_valid) {
			if ((new_ps->levels[0].sclk - table->entries[pi->highest_valid].clk) >
			    (table->entries[pi->lowest_valid].clk - new_ps->levels[new_ps->num_levels - 1].sclk))
				pi->highest_valid = pi->lowest_valid;
			else
				pi->lowest_valid =  pi->highest_valid;
		}
	} else {
		struct sumo_sclk_voltage_mapping_table *table =
			&pi->sys_info.sclk_voltage_mapping_table;

		for (i = 0; i < (int)pi->graphics_dpm_level_count; i++) {
			if (table->entries[i].sclk_frequency >= new_ps->levels[0].sclk ||
			    i == (int)(pi->graphics_dpm_level_count - 1)) {
				pi->lowest_valid = i;
				break;
			}
		}

		for (i = pi->graphics_dpm_level_count - 1; i > 0; i--) {
			if (table->entries[i].sclk_frequency <=
			    new_ps->levels[new_ps->num_levels - 1].sclk)
				break;
		}
		pi->highest_valid = i;

		if (pi->lowest_valid > pi->highest_valid) {
			if ((new_ps->levels[0].sclk -
			     table->entries[pi->highest_valid].sclk_frequency) >
			    (table->entries[pi->lowest_valid].sclk_frequency -
			     new_ps->levels[new_ps->num_levels -1].sclk))
				pi->highest_valid = pi->lowest_valid;
			else
				pi->lowest_valid =  pi->highest_valid;
		}
	}
}

static int kv_update_dfs_bypass_settings(struct radeon_device *rdev,
					 struct radeon_ps *new_rps)
{
	struct kv_ps *new_ps = kv_get_ps(new_rps);
	struct kv_power_info *pi = kv_get_pi(rdev);
	int ret = 0;
	u8 clk_bypass_cntl;

	if (pi->caps_enable_dfs_bypass) {
		clk_bypass_cntl = new_ps->need_dfs_bypass ?
			pi->graphics_level[pi->graphics_boot_level].ClkBypassCntl : 0;
		ret = kv_copy_bytes_to_smc(rdev,
					   (pi->dpm_table_start +
					    offsetof(SMU7_Fusion_DpmTable, GraphicsLevel) +
					    (pi->graphics_boot_level * sizeof(SMU7_Fusion_GraphicsLevel)) +
					    offsetof(SMU7_Fusion_GraphicsLevel, ClkBypassCntl)),
					   &clk_bypass_cntl,
					   sizeof(u8), pi->sram_end);
	}

	return ret;
}

static int kv_enable_nb_dpm(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	int ret = 0;

	if (pi->enable_nb_dpm && !pi->nb_dpm_enabled) {
		ret = kv_notify_message_to_smu(rdev, PPSMC_MSG_NBDPM_Enable);
		if (ret == 0)
			pi->nb_dpm_enabled = true;
	}

	return ret;
}

int kv_dpm_force_performance_level(struct radeon_device *rdev,
				   enum radeon_dpm_forced_level level)
{
	int ret;

	if (level == RADEON_DPM_FORCED_LEVEL_HIGH) {
		ret = kv_force_dpm_highest(rdev);
		if (ret)
			return ret;
	} else if (level == RADEON_DPM_FORCED_LEVEL_LOW) {
		ret = kv_force_dpm_lowest(rdev);
		if (ret)
			return ret;
	} else if (level == RADEON_DPM_FORCED_LEVEL_AUTO) {
		ret = kv_unforce_levels(rdev);
		if (ret)
			return ret;
	}

	rdev->pm.dpm.forced_level = level;

	return 0;
}

int kv_dpm_pre_set_power_state(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	struct radeon_ps requested_ps = *rdev->pm.dpm.requested_ps;
	struct radeon_ps *new_ps = &requested_ps;

	kv_update_requested_ps(rdev, new_ps);

	kv_apply_state_adjust_rules(rdev,
				    &pi->requested_rps,
				    &pi->current_rps);

	return 0;
}

int kv_dpm_set_power_state(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	struct radeon_ps *new_ps = &pi->requested_rps;
	/*struct radeon_ps *old_ps = &pi->current_rps;*/
	int ret;

	if (pi->bapm_enable) {
		ret = kv_smc_bapm_enable(rdev, rdev->pm.dpm.ac_power);
		if (ret) {
			DRM_ERROR("kv_smc_bapm_enable failed\n");
			return ret;
		}
	}

	if (rdev->family == CHIP_KABINI) {
		if (pi->enable_dpm) {
			kv_set_valid_clock_range(rdev, new_ps);
			kv_update_dfs_bypass_settings(rdev, new_ps);
			ret = kv_calculate_ds_divider(rdev);
			if (ret) {
				DRM_ERROR("kv_calculate_ds_divider failed\n");
				return ret;
			}
			kv_calculate_nbps_level_settings(rdev);
			kv_calculate_dpm_settings(rdev);
			kv_force_lowest_valid(rdev);
			kv_enable_new_levels(rdev);
			kv_upload_dpm_settings(rdev);
			kv_program_nbps_index_settings(rdev, new_ps);
			kv_unforce_levels(rdev);
			kv_set_enabled_levels(rdev);
			kv_force_lowest_valid(rdev);
			kv_unforce_levels(rdev);
#if 0
			ret = kv_update_vce_dpm(rdev, new_ps, old_ps);
			if (ret) {
				DRM_ERROR("kv_update_vce_dpm failed\n");
				return ret;
			}
#endif
			kv_update_sclk_t(rdev);
		}
	} else {
		if (pi->enable_dpm) {
			kv_set_valid_clock_range(rdev, new_ps);
			kv_update_dfs_bypass_settings(rdev, new_ps);
			ret = kv_calculate_ds_divider(rdev);
			if (ret) {
				DRM_ERROR("kv_calculate_ds_divider failed\n");
				return ret;
			}
			kv_calculate_nbps_level_settings(rdev);
			kv_calculate_dpm_settings(rdev);
			kv_freeze_sclk_dpm(rdev, true);
			kv_upload_dpm_settings(rdev);
			kv_program_nbps_index_settings(rdev, new_ps);
			kv_freeze_sclk_dpm(rdev, false);
			kv_set_enabled_levels(rdev);
#if 0
			ret = kv_update_vce_dpm(rdev, new_ps, old_ps);
			if (ret) {
				DRM_ERROR("kv_update_vce_dpm failed\n");
				return ret;
			}
#endif
			kv_update_acp_boot_level(rdev);
			kv_update_sclk_t(rdev);
			kv_enable_nb_dpm(rdev);
		}
	}

	return 0;
}

void kv_dpm_post_set_power_state(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	struct radeon_ps *new_ps = &pi->requested_rps;

	kv_update_current_ps(rdev, new_ps);
}

void kv_dpm_setup_asic(struct radeon_device *rdev)
{
	sumo_take_smu_control(rdev, true);
	kv_init_powergate_state(rdev);
	kv_init_sclk_t(rdev);
}

void kv_dpm_reset_asic(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);

	if (rdev->family == CHIP_KABINI) {
		kv_force_lowest_valid(rdev);
		kv_init_graphics_levels(rdev);
		kv_program_bootup_state(rdev);
		kv_upload_dpm_settings(rdev);
		kv_force_lowest_valid(rdev);
		kv_unforce_levels(rdev);
	} else {
		kv_init_graphics_levels(rdev);
		kv_program_bootup_state(rdev);
		kv_freeze_sclk_dpm(rdev, true);
		kv_upload_dpm_settings(rdev);
		kv_freeze_sclk_dpm(rdev, false);
		kv_set_enabled_level(rdev, pi->graphics_boot_level);
	}
}

//XXX use sumo_dpm_display_configuration_changed

static void kv_construct_max_power_limits_table(struct radeon_device *rdev,
						struct radeon_clock_and_voltage_limits *table)
{
	struct kv_power_info *pi = kv_get_pi(rdev);

	if (pi->sys_info.sclk_voltage_mapping_table.num_max_dpm_entries > 0) {
		int idx = pi->sys_info.sclk_voltage_mapping_table.num_max_dpm_entries - 1;
		table->sclk =
			pi->sys_info.sclk_voltage_mapping_table.entries[idx].sclk_frequency;
		table->vddc =
			kv_convert_2bit_index_to_voltage(rdev,
							 pi->sys_info.sclk_voltage_mapping_table.entries[idx].vid_2bit);
	}

	table->mclk = pi->sys_info.nbp_memory_clock[0];
}

static void kv_patch_voltage_values(struct radeon_device *rdev)
{
	int i;
	struct radeon_uvd_clock_voltage_dependency_table *table =
		&rdev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table;

	if (table->count) {
		for (i = 0; i < table->count; i++)
			table->entries[i].v =
				kv_convert_8bit_index_to_voltage(rdev,
								 table->entries[i].v);
	}

}

static void kv_construct_boot_state(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);

	pi->boot_pl.sclk = pi->sys_info.bootup_sclk;
	pi->boot_pl.vddc_index = pi->sys_info.bootup_nb_voltage_index;
	pi->boot_pl.ds_divider_index = 0;
	pi->boot_pl.ss_divider_index = 0;
	pi->boot_pl.allow_gnb_slow = 1;
	pi->boot_pl.force_nbp_state = 0;
	pi->boot_pl.display_wm = 0;
	pi->boot_pl.vce_wm = 0;
}

static int kv_force_dpm_highest(struct radeon_device *rdev)
{
	int ret;
	u32 enable_mask, i;

	ret = kv_dpm_get_enable_mask(rdev, &enable_mask);
	if (ret)
		return ret;

	for (i = SMU7_MAX_LEVELS_GRAPHICS - 1; i > 0; i--) {
		if (enable_mask & (1 << i))
			break;
	}

	if (rdev->family == CHIP_KABINI)
		return kv_send_msg_to_smc_with_parameter(rdev, PPSMC_MSG_DPM_ForceState, i);
	else
		return kv_set_enabled_level(rdev, i);
}

static int kv_force_dpm_lowest(struct radeon_device *rdev)
{
	int ret;
	u32 enable_mask, i;

	ret = kv_dpm_get_enable_mask(rdev, &enable_mask);
	if (ret)
		return ret;

	for (i = 0; i < SMU7_MAX_LEVELS_GRAPHICS; i++) {
		if (enable_mask & (1 << i))
			break;
	}

	if (rdev->family == CHIP_KABINI)
		return kv_send_msg_to_smc_with_parameter(rdev, PPSMC_MSG_DPM_ForceState, i);
	else
		return kv_set_enabled_level(rdev, i);
}

static u8 kv_get_sleep_divider_id_from_clock(struct radeon_device *rdev,
					     u32 sclk, u32 min_sclk_in_sr)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	u32 i;
	u32 temp;
	u32 min = (min_sclk_in_sr > KV_MINIMUM_ENGINE_CLOCK) ?
		min_sclk_in_sr : KV_MINIMUM_ENGINE_CLOCK;

	if (sclk < min)
		return 0;

	if (!pi->caps_sclk_ds)
		return 0;

	for (i = KV_MAX_DEEPSLEEP_DIVIDER_ID; i > 0; i--) {
		temp = sclk / sumo_get_sleep_divider_from_id(i);
		if (temp >= min)
			break;
	}

	return (u8)i;
}

static int kv_get_high_voltage_limit(struct radeon_device *rdev, int *limit)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	struct radeon_clock_voltage_dependency_table *table =
		&rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk;
	int i;

	if (table && table->count) {
		for (i = table->count - 1; i >= 0; i--) {
			if (pi->high_voltage_t &&
			    (kv_convert_8bit_index_to_voltage(rdev, table->entries[i].v) <=
			     pi->high_voltage_t)) {
				*limit = i;
				return 0;
			}
		}
	} else {
		struct sumo_sclk_voltage_mapping_table *table =
			&pi->sys_info.sclk_voltage_mapping_table;

		for (i = table->num_max_dpm_entries - 1; i >= 0; i--) {
			if (pi->high_voltage_t &&
			    (kv_convert_2bit_index_to_voltage(rdev, table->entries[i].vid_2bit) <=
			     pi->high_voltage_t)) {
				*limit = i;
				return 0;
			}
		}
	}

	*limit = 0;
	return 0;
}

static void kv_apply_state_adjust_rules(struct radeon_device *rdev,
					struct radeon_ps *new_rps,
					struct radeon_ps *old_rps)
{
	struct kv_ps *ps = kv_get_ps(new_rps);
	struct kv_power_info *pi = kv_get_pi(rdev);
	u32 min_sclk = 10000; /* ??? */
	u32 sclk, mclk = 0;
	int i, limit;
	bool force_high;
	struct radeon_clock_voltage_dependency_table *table =
		&rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk;
	u32 stable_p_state_sclk = 0;
	struct radeon_clock_and_voltage_limits *max_limits =
		&rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac;

	mclk = max_limits->mclk;
	sclk = min_sclk;

	if (pi->caps_stable_p_state) {
		stable_p_state_sclk = (max_limits->sclk * 75) / 100;

		for (i = table->count - 1; i >= 0; i++) {
			if (stable_p_state_sclk >= table->entries[i].clk) {
				stable_p_state_sclk = table->entries[i].clk;
				break;
			}
		}

		if (i > 0)
			stable_p_state_sclk = table->entries[0].clk;

		sclk = stable_p_state_sclk;
	}

	ps->need_dfs_bypass = true;

	for (i = 0; i < ps->num_levels; i++) {
		if (ps->levels[i].sclk < sclk)
			ps->levels[i].sclk = sclk;
	}

	if (table && table->count) {
		for (i = 0; i < ps->num_levels; i++) {
			if (pi->high_voltage_t &&
			    (pi->high_voltage_t <
			     kv_convert_8bit_index_to_voltage(rdev, ps->levels[i].vddc_index))) {
				kv_get_high_voltage_limit(rdev, &limit);
				ps->levels[i].sclk = table->entries[limit].clk;
			}
		}
	} else {
		struct sumo_sclk_voltage_mapping_table *table =
			&pi->sys_info.sclk_voltage_mapping_table;

		for (i = 0; i < ps->num_levels; i++) {
			if (pi->high_voltage_t &&
			    (pi->high_voltage_t <
			     kv_convert_8bit_index_to_voltage(rdev, ps->levels[i].vddc_index))) {
				kv_get_high_voltage_limit(rdev, &limit);
				ps->levels[i].sclk = table->entries[limit].sclk_frequency;
			}
		}
	}

	if (pi->caps_stable_p_state) {
		for (i = 0; i < ps->num_levels; i++) {
			ps->levels[i].sclk = stable_p_state_sclk;
		}
	}

	pi->video_start = new_rps->dclk || new_rps->vclk;

	if ((new_rps->class & ATOM_PPLIB_CLASSIFICATION_UI_MASK) ==
	    ATOM_PPLIB_CLASSIFICATION_UI_BATTERY)
		pi->battery_state = true;
	else
		pi->battery_state = false;

	if (rdev->family == CHIP_KABINI) {
		ps->dpm0_pg_nb_ps_lo = 0x1;
		ps->dpm0_pg_nb_ps_hi = 0x0;
		ps->dpmx_nb_ps_lo = 0x1;
		ps->dpmx_nb_ps_hi = 0x0;
	} else {
		ps->dpm0_pg_nb_ps_lo = 0x3;
		ps->dpm0_pg_nb_ps_hi = 0x0;
		ps->dpmx_nb_ps_lo = 0x3;
		ps->dpmx_nb_ps_hi = 0x0;

		if (pi->sys_info.nb_dpm_enable) {
			force_high = (mclk >= pi->sys_info.nbp_memory_clock[3]) ||
				pi->video_start || (rdev->pm.dpm.new_active_crtc_count >= 3) ||
				pi->disable_nb_ps3_in_battery;
			ps->dpm0_pg_nb_ps_lo = force_high ? 0x2 : 0x3;
			ps->dpm0_pg_nb_ps_hi = 0x2;
			ps->dpmx_nb_ps_lo = force_high ? 0x2 : 0x3;
			ps->dpmx_nb_ps_hi = 0x2;
		}
	}
}

static void kv_dpm_power_level_enabled_for_throttle(struct radeon_device *rdev,
						    u32 index, bool enable)
{
	struct kv_power_info *pi = kv_get_pi(rdev);

	pi->graphics_level[index].EnabledForThrottle = enable ? 1 : 0;
}

static int kv_calculate_ds_divider(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	u32 sclk_in_sr = 10000; /* ??? */
	u32 i;

	if (pi->lowest_valid > pi->highest_valid)
		return -EINVAL;

	for (i = pi->lowest_valid; i <= pi->highest_valid; i++) {
		pi->graphics_level[i].DeepSleepDivId =
			kv_get_sleep_divider_id_from_clock(rdev,
							   be32_to_cpu(pi->graphics_level[i].SclkFrequency),
							   sclk_in_sr);
	}
	return 0;
}

static int kv_calculate_nbps_level_settings(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	u32 i;
	bool force_high;
	struct radeon_clock_and_voltage_limits *max_limits =
		&rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac;
	u32 mclk = max_limits->mclk;

	if (pi->lowest_valid > pi->highest_valid)
		return -EINVAL;

	if (rdev->family == CHIP_KABINI) {
		for (i = pi->lowest_valid; i <= pi->highest_valid; i++) {
			pi->graphics_level[i].GnbSlow = 1;
			pi->graphics_level[i].ForceNbPs1 = 0;
			pi->graphics_level[i].UpH = 0;
		}

		if (!pi->sys_info.nb_dpm_enable)
			return 0;

		force_high = ((mclk >= pi->sys_info.nbp_memory_clock[3]) ||
			      (rdev->pm.dpm.new_active_crtc_count >= 3) || pi->video_start);

		if (force_high) {
			for (i = pi->lowest_valid; i <= pi->highest_valid; i++)
				pi->graphics_level[i].GnbSlow = 0;
		} else {
			if (pi->battery_state)
				pi->graphics_level[0].ForceNbPs1 = 1;

			pi->graphics_level[1].GnbSlow = 0;
			pi->graphics_level[2].GnbSlow = 0;
			pi->graphics_level[3].GnbSlow = 0;
			pi->graphics_level[4].GnbSlow = 0;
		}
	} else {
		for (i = pi->lowest_valid; i <= pi->highest_valid; i++) {
			pi->graphics_level[i].GnbSlow = 1;
			pi->graphics_level[i].ForceNbPs1 = 0;
			pi->graphics_level[i].UpH = 0;
		}

		if (pi->sys_info.nb_dpm_enable && pi->battery_state) {
			pi->graphics_level[pi->lowest_valid].UpH = 0x28;
			pi->graphics_level[pi->lowest_valid].GnbSlow = 0;
			if (pi->lowest_valid != pi->highest_valid)
				pi->graphics_level[pi->lowest_valid].ForceNbPs1 = 1;
		}
	}
	return 0;
}

static int kv_calculate_dpm_settings(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	u32 i;

	if (pi->lowest_valid > pi->highest_valid)
		return -EINVAL;

	for (i = pi->lowest_valid; i <= pi->highest_valid; i++)
		pi->graphics_level[i].DisplayWatermark = (i == pi->highest_valid) ? 1 : 0;

	return 0;
}

static void kv_init_graphics_levels(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	u32 i;
	struct radeon_clock_voltage_dependency_table *table =
		&rdev->pm.dpm.dyn_state.vddc_dependency_on_sclk;

	if (table && table->count) {
		u32 vid_2bit;

		pi->graphics_dpm_level_count = 0;
		for (i = 0; i < table->count; i++) {
			if (pi->high_voltage_t &&
			    (pi->high_voltage_t <
			     kv_convert_8bit_index_to_voltage(rdev, table->entries[i].v)))
				break;

			kv_set_divider_value(rdev, i, table->entries[i].clk);
			vid_2bit = sumo_convert_vid7_to_vid2(rdev,
							     &pi->sys_info.vid_mapping_table,
							     table->entries[i].v);
			kv_set_vid(rdev, i, vid_2bit);
			kv_set_at(rdev, i, pi->at[i]);
			kv_dpm_power_level_enabled_for_throttle(rdev, i, true);
			pi->graphics_dpm_level_count++;
		}
	} else {
		struct sumo_sclk_voltage_mapping_table *table =
			&pi->sys_info.sclk_voltage_mapping_table;

		pi->graphics_dpm_level_count = 0;
		for (i = 0; i < table->num_max_dpm_entries; i++) {
			if (pi->high_voltage_t &&
			    pi->high_voltage_t <
			    kv_convert_2bit_index_to_voltage(rdev, table->entries[i].vid_2bit))
				break;

			kv_set_divider_value(rdev, i, table->entries[i].sclk_frequency);
			kv_set_vid(rdev, i, table->entries[i].vid_2bit);
			kv_set_at(rdev, i, pi->at[i]);
			kv_dpm_power_level_enabled_for_throttle(rdev, i, true);
			pi->graphics_dpm_level_count++;
		}
	}

	for (i = 0; i < SMU7_MAX_LEVELS_GRAPHICS; i++)
		kv_dpm_power_level_enable(rdev, i, false);
}

static void kv_enable_new_levels(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	u32 i;

	for (i = 0; i < SMU7_MAX_LEVELS_GRAPHICS; i++) {
		if (i >= pi->lowest_valid && i <= pi->highest_valid)
			kv_dpm_power_level_enable(rdev, i, true);
	}
}

static int kv_set_enabled_level(struct radeon_device *rdev, u32 level)
{
	u32 new_mask = (1 << level);

	return kv_send_msg_to_smc_with_parameter(rdev,
						 PPSMC_MSG_SCLKDPM_SetEnabledMask,
						 new_mask);
}

static int kv_set_enabled_levels(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	u32 i, new_mask = 0;

	for (i = pi->lowest_valid; i <= pi->highest_valid; i++)
		new_mask |= (1 << i);

	return kv_send_msg_to_smc_with_parameter(rdev,
						 PPSMC_MSG_SCLKDPM_SetEnabledMask,
						 new_mask);
}

static void kv_program_nbps_index_settings(struct radeon_device *rdev,
					   struct radeon_ps *new_rps)
{
	struct kv_ps *new_ps = kv_get_ps(new_rps);
	struct kv_power_info *pi = kv_get_pi(rdev);
	u32 nbdpmconfig1;

	if (rdev->family == CHIP_KABINI)
		return;

	if (pi->sys_info.nb_dpm_enable) {
		nbdpmconfig1 = RREG32_SMC(NB_DPM_CONFIG_1);
		nbdpmconfig1 &= ~(Dpm0PgNbPsLo_MASK | Dpm0PgNbPsHi_MASK |
				  DpmXNbPsLo_MASK | DpmXNbPsHi_MASK);
		nbdpmconfig1 |= (Dpm0PgNbPsLo(new_ps->dpm0_pg_nb_ps_lo) |
				 Dpm0PgNbPsHi(new_ps->dpm0_pg_nb_ps_hi) |
				 DpmXNbPsLo(new_ps->dpmx_nb_ps_lo) |
				 DpmXNbPsHi(new_ps->dpmx_nb_ps_hi));
		WREG32_SMC(NB_DPM_CONFIG_1, nbdpmconfig1);
	}
}

static int kv_set_thermal_temperature_range(struct radeon_device *rdev,
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

	tmp = RREG32_SMC(CG_THERMAL_INT_CTRL);
	tmp &= ~(DIG_THERM_INTH_MASK | DIG_THERM_INTL_MASK);
	tmp |= (DIG_THERM_INTH(49 + (high_temp / 1000)) |
		DIG_THERM_INTL(49 + (low_temp / 1000)));
	WREG32_SMC(CG_THERMAL_INT_CTRL, tmp);

	rdev->pm.dpm.thermal.min_temp = low_temp;
	rdev->pm.dpm.thermal.max_temp = high_temp;

	return 0;
}

union igp_info {
	struct _ATOM_INTEGRATED_SYSTEM_INFO info;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V2 info_2;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V5 info_5;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V6 info_6;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V1_7 info_7;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V1_8 info_8;
};

static int kv_parse_sys_info_table(struct radeon_device *rdev)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, IntegratedSystemInfo);
	union igp_info *igp_info;
	u8 frev, crev;
	u16 data_offset;
	int i;

	if (atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		igp_info = (union igp_info *)(mode_info->atom_context->bios +
					      data_offset);

		if (crev != 8) {
			DRM_ERROR("Unsupported IGP table: %d %d\n", frev, crev);
			return -EINVAL;
		}
		pi->sys_info.bootup_sclk = le32_to_cpu(igp_info->info_8.ulBootUpEngineClock);
		pi->sys_info.bootup_uma_clk = le32_to_cpu(igp_info->info_8.ulBootUpUMAClock);
		pi->sys_info.bootup_nb_voltage_index =
			le16_to_cpu(igp_info->info_8.usBootUpNBVoltage);
		if (igp_info->info_8.ucHtcTmpLmt == 0)
			pi->sys_info.htc_tmp_lmt = 203;
		else
			pi->sys_info.htc_tmp_lmt = igp_info->info_8.ucHtcTmpLmt;
		if (igp_info->info_8.ucHtcHystLmt == 0)
			pi->sys_info.htc_hyst_lmt = 5;
		else
			pi->sys_info.htc_hyst_lmt = igp_info->info_8.ucHtcHystLmt;
		if (pi->sys_info.htc_tmp_lmt <= pi->sys_info.htc_hyst_lmt) {
			DRM_ERROR("The htcTmpLmt should be larger than htcHystLmt.\n");
		}

		if (le32_to_cpu(igp_info->info_8.ulSystemConfig) & (1 << 3))
			pi->sys_info.nb_dpm_enable = true;
		else
			pi->sys_info.nb_dpm_enable = false;

		for (i = 0; i < KV_NUM_NBPSTATES; i++) {
			pi->sys_info.nbp_memory_clock[i] =
				le32_to_cpu(igp_info->info_8.ulNbpStateMemclkFreq[i]);
			pi->sys_info.nbp_n_clock[i] =
				le32_to_cpu(igp_info->info_8.ulNbpStateNClkFreq[i]);
		}
		if (le32_to_cpu(igp_info->info_8.ulGPUCapInfo) &
		    SYS_INFO_GPUCAPS__ENABEL_DFS_BYPASS)
			pi->caps_enable_dfs_bypass = true;

		sumo_construct_sclk_voltage_mapping_table(rdev,
							  &pi->sys_info.sclk_voltage_mapping_table,
							  igp_info->info_8.sAvail_SCLK);

		sumo_construct_vid_mapping_table(rdev,
						 &pi->sys_info.vid_mapping_table,
						 igp_info->info_8.sAvail_SCLK);

		kv_construct_max_power_limits_table(rdev,
						    &rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac);
	}
	return 0;
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

static void kv_patch_boot_state(struct radeon_device *rdev,
				struct kv_ps *ps)
{
	struct kv_power_info *pi = kv_get_pi(rdev);

	ps->num_levels = 1;
	ps->levels[0] = pi->boot_pl;
}

static void kv_parse_pplib_non_clock_info(struct radeon_device *rdev,
					  struct radeon_ps *rps,
					  struct _ATOM_PPLIB_NONCLOCK_INFO *non_clock_info,
					  u8 table_rev)
{
	struct kv_ps *ps = kv_get_ps(rps);

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

	if (rps->class & ATOM_PPLIB_CLASSIFICATION_BOOT) {
		rdev->pm.dpm.boot_ps = rps;
		kv_patch_boot_state(rdev, ps);
	}
	if (rps->class & ATOM_PPLIB_CLASSIFICATION_UVDSTATE)
		rdev->pm.dpm.uvd_ps = rps;
}

static void kv_parse_pplib_clock_info(struct radeon_device *rdev,
				      struct radeon_ps *rps, int index,
					union pplib_clock_info *clock_info)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	struct kv_ps *ps = kv_get_ps(rps);
	struct kv_pl *pl = &ps->levels[index];
	u32 sclk;

	sclk = le16_to_cpu(clock_info->sumo.usEngineClockLow);
	sclk |= clock_info->sumo.ucEngineClockHigh << 16;
	pl->sclk = sclk;
	pl->vddc_index = clock_info->sumo.vddcIndex;

	ps->num_levels = index + 1;

	if (pi->caps_sclk_ds) {
		pl->ds_divider_index = 5;
		pl->ss_divider_index = 5;
	}
}

static int kv_parse_power_table(struct radeon_device *rdev)
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
	struct kv_ps *ps;

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

	rdev->pm.dpm.ps = kzalloc(sizeof(struct radeon_ps) *
				  state_array->ucNumEntries, GFP_KERNEL);
	if (!rdev->pm.dpm.ps)
		return -ENOMEM;
	power_state_offset = (u8 *)state_array->states;
	rdev->pm.dpm.platform_caps = le32_to_cpu(power_info->pplib.ulPlatformCaps);
	rdev->pm.dpm.backbias_response_time = le16_to_cpu(power_info->pplib.usBackbiasTime);
	rdev->pm.dpm.voltage_response_time = le16_to_cpu(power_info->pplib.usVoltageTime);
	for (i = 0; i < state_array->ucNumEntries; i++) {
		u8 *idx;
		power_state = (union pplib_power_state *)power_state_offset;
		non_clock_array_index = power_state->v2.nonClockInfoIndex;
		non_clock_info = (struct _ATOM_PPLIB_NONCLOCK_INFO *)
			&non_clock_info_array->nonClockInfo[non_clock_array_index];
		if (!rdev->pm.power_state[i].clock_info)
			return -EINVAL;
		ps = kzalloc(sizeof(struct kv_ps), GFP_KERNEL);
		if (ps == NULL) {
			kfree(rdev->pm.dpm.ps);
			return -ENOMEM;
		}
		rdev->pm.dpm.ps[i].ps_priv = ps;
		k = 0;
		idx = (u8 *)&power_state->v2.clockInfoIndex[0];
		for (j = 0; j < power_state->v2.ucNumDPMLevels; j++) {
			clock_array_index = idx[j];
			if (clock_array_index >= clock_info_array->ucNumEntries)
				continue;
			if (k >= SUMO_MAX_HARDWARE_POWERLEVELS)
				break;
			clock_info = (union pplib_clock_info *)
				((u8 *)&clock_info_array->clockInfo[0] +
				 (clock_array_index * clock_info_array->ucEntrySize));
			kv_parse_pplib_clock_info(rdev,
						  &rdev->pm.dpm.ps[i], k,
						  clock_info);
			k++;
		}
		kv_parse_pplib_non_clock_info(rdev, &rdev->pm.dpm.ps[i],
					      non_clock_info,
					      non_clock_info_array->ucEntrySize);
		power_state_offset += 2 + power_state->v2.ucNumDPMLevels;
	}
	rdev->pm.dpm.num_ps = state_array->ucNumEntries;
	return 0;
}

int kv_dpm_init(struct radeon_device *rdev)
{
	struct kv_power_info *pi;
	int ret, i;

	pi = kzalloc(sizeof(struct kv_power_info), GFP_KERNEL);
	if (pi == NULL)
		return -ENOMEM;
	rdev->pm.dpm.priv = pi;

	ret = r600_parse_extended_power_table(rdev);
	if (ret)
		return ret;

	for (i = 0; i < SUMO_MAX_HARDWARE_POWERLEVELS; i++)
		pi->at[i] = TRINITY_AT_DFLT;

        pi->sram_end = SMC_RAM_END;

	if (rdev->family == CHIP_KABINI)
		pi->high_voltage_t = 4001;

	pi->enable_nb_dpm = true;

	pi->caps_power_containment = true;
	pi->caps_cac = true;
	pi->enable_didt = false;
	if (pi->enable_didt) {
		pi->caps_sq_ramping = true;
		pi->caps_db_ramping = true;
		pi->caps_td_ramping = true;
		pi->caps_tcp_ramping = true;
	}

	pi->caps_sclk_ds = true;
	pi->enable_auto_thermal_throttling = true;
	pi->disable_nb_ps3_in_battery = false;
	pi->bapm_enable = false;
	pi->voltage_drop_t = 0;
	pi->caps_sclk_throttle_low_notification = false;
	pi->caps_fps = false; /* true? */
	pi->caps_uvd_pg = true;
	pi->caps_uvd_dpm = true;
	pi->caps_vce_pg = false;
	pi->caps_samu_pg = false;
	pi->caps_acp_pg = false;
	pi->caps_stable_p_state = false;

	ret = kv_parse_sys_info_table(rdev);
	if (ret)
		return ret;

	kv_patch_voltage_values(rdev);
	kv_construct_boot_state(rdev);

	ret = kv_parse_power_table(rdev);
	if (ret)
		return ret;

	pi->enable_dpm = true;

	return 0;
}

void kv_dpm_debugfs_print_current_performance_level(struct radeon_device *rdev,
						    struct seq_file *m)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	u32 current_index =
		(RREG32_SMC(TARGET_AND_CURRENT_PROFILE_INDEX) & CURR_SCLK_INDEX_MASK) >>
		CURR_SCLK_INDEX_SHIFT;
	u32 sclk, tmp;
	u16 vddc;

	if (current_index >= SMU__NUM_SCLK_DPM_STATE) {
		seq_printf(m, "invalid dpm profile %d\n", current_index);
	} else {
		sclk = be32_to_cpu(pi->graphics_level[current_index].SclkFrequency);
		tmp = (RREG32_SMC(SMU_VOLTAGE_STATUS) & SMU_VOLTAGE_CURRENT_LEVEL_MASK) >>
			SMU_VOLTAGE_CURRENT_LEVEL_SHIFT;
		vddc = kv_convert_8bit_index_to_voltage(rdev, (u16)tmp);
		seq_printf(m, "power level %d    sclk: %u vddc: %u\n",
			   current_index, sclk, vddc);
	}
}

void kv_dpm_print_power_state(struct radeon_device *rdev,
			      struct radeon_ps *rps)
{
	int i;
	struct kv_ps *ps = kv_get_ps(rps);

	r600_dpm_print_class_info(rps->class, rps->class2);
	r600_dpm_print_cap_info(rps->caps);
	printk("\tuvd    vclk: %d dclk: %d\n", rps->vclk, rps->dclk);
	for (i = 0; i < ps->num_levels; i++) {
		struct kv_pl *pl = &ps->levels[i];
		printk("\t\tpower level %d    sclk: %u vddc: %u\n",
		       i, pl->sclk,
		       kv_convert_8bit_index_to_voltage(rdev, pl->vddc_index));
	}
	r600_dpm_print_ps_status(rdev, rps);
}

void kv_dpm_fini(struct radeon_device *rdev)
{
	int i;

	for (i = 0; i < rdev->pm.dpm.num_ps; i++) {
		kfree(rdev->pm.dpm.ps[i].ps_priv);
	}
	kfree(rdev->pm.dpm.ps);
	kfree(rdev->pm.dpm.priv);
	r600_free_extended_power_table(rdev);
}

void kv_dpm_display_configuration_changed(struct radeon_device *rdev)
{

}

u32 kv_dpm_get_sclk(struct radeon_device *rdev, bool low)
{
	struct kv_power_info *pi = kv_get_pi(rdev);
	struct kv_ps *requested_state = kv_get_ps(&pi->requested_rps);

	if (low)
		return requested_state->levels[0].sclk;
	else
		return requested_state->levels[requested_state->num_levels - 1].sclk;
}

u32 kv_dpm_get_mclk(struct radeon_device *rdev, bool low)
{
	struct kv_power_info *pi = kv_get_pi(rdev);

	return pi->sys_info.bootup_uma_clk;
}

