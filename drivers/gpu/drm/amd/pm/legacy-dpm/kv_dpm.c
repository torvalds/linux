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

#include "amdgpu.h"
#include "amdgpu_pm.h"
#include "cikd.h"
#include "atom.h"
#include "amdgpu_atombios.h"
#include "amdgpu_dpm.h"
#include "kv_dpm.h"
#include "gfx_v7_0.h"
#include <linux/seq_file.h>

#include "smu/smu_7_0_0_d.h"
#include "smu/smu_7_0_0_sh_mask.h"

#include "gca/gfx_7_2_d.h"
#include "gca/gfx_7_2_sh_mask.h"
#include "legacy_dpm.h"

#define KV_MAX_DEEPSLEEP_DIVIDER_ID     5
#define KV_MINIMUM_ENGINE_CLOCK         800
#define SMC_RAM_END                     0x40000

static const struct amd_pm_funcs kv_dpm_funcs;

static void kv_dpm_set_irq_funcs(struct amdgpu_device *adev);
static int kv_enable_nb_dpm(struct amdgpu_device *adev,
			    bool enable);
static void kv_init_graphics_levels(struct amdgpu_device *adev);
static int kv_calculate_ds_divider(struct amdgpu_device *adev);
static int kv_calculate_nbps_level_settings(struct amdgpu_device *adev);
static int kv_calculate_dpm_settings(struct amdgpu_device *adev);
static void kv_enable_new_levels(struct amdgpu_device *adev);
static void kv_program_nbps_index_settings(struct amdgpu_device *adev,
					   struct amdgpu_ps *new_rps);
static int kv_set_enabled_level(struct amdgpu_device *adev, u32 level);
static int kv_set_enabled_levels(struct amdgpu_device *adev);
static int kv_force_dpm_highest(struct amdgpu_device *adev);
static int kv_force_dpm_lowest(struct amdgpu_device *adev);
static void kv_apply_state_adjust_rules(struct amdgpu_device *adev,
					struct amdgpu_ps *new_rps,
					struct amdgpu_ps *old_rps);
static int kv_set_thermal_temperature_range(struct amdgpu_device *adev,
					    int min_temp, int max_temp);
static int kv_init_fps_limits(struct amdgpu_device *adev);

static void kv_dpm_powergate_samu(struct amdgpu_device *adev, bool gate);
static void kv_dpm_powergate_acp(struct amdgpu_device *adev, bool gate);


static u32 kv_convert_vid2_to_vid7(struct amdgpu_device *adev,
				   struct sumo_vid_mapping_table *vid_mapping_table,
				   u32 vid_2bit)
{
	struct amdgpu_clock_voltage_dependency_table *vddc_sclk_table =
		&adev->pm.dpm.dyn_state.vddc_dependency_on_sclk;
	u32 i;

	if (vddc_sclk_table && vddc_sclk_table->count) {
		if (vid_2bit < vddc_sclk_table->count)
			return vddc_sclk_table->entries[vid_2bit].v;
		else
			return vddc_sclk_table->entries[vddc_sclk_table->count - 1].v;
	} else {
		for (i = 0; i < vid_mapping_table->num_entries; i++) {
			if (vid_mapping_table->entries[i].vid_2bit == vid_2bit)
				return vid_mapping_table->entries[i].vid_7bit;
		}
		return vid_mapping_table->entries[vid_mapping_table->num_entries - 1].vid_7bit;
	}
}

static u32 kv_convert_vid7_to_vid2(struct amdgpu_device *adev,
				   struct sumo_vid_mapping_table *vid_mapping_table,
				   u32 vid_7bit)
{
	struct amdgpu_clock_voltage_dependency_table *vddc_sclk_table =
		&adev->pm.dpm.dyn_state.vddc_dependency_on_sclk;
	u32 i;

	if (vddc_sclk_table && vddc_sclk_table->count) {
		for (i = 0; i < vddc_sclk_table->count; i++) {
			if (vddc_sclk_table->entries[i].v == vid_7bit)
				return i;
		}
		return vddc_sclk_table->count - 1;
	} else {
		for (i = 0; i < vid_mapping_table->num_entries; i++) {
			if (vid_mapping_table->entries[i].vid_7bit == vid_7bit)
				return vid_mapping_table->entries[i].vid_2bit;
		}

		return vid_mapping_table->entries[vid_mapping_table->num_entries - 1].vid_2bit;
	}
}

static void sumo_take_smu_control(struct amdgpu_device *adev, bool enable)
{
/* This bit selects who handles display phy powergating.
 * Clear the bit to let atom handle it.
 * Set it to let the driver handle it.
 * For now we just let atom handle it.
 */
#if 0
	u32 v = RREG32(mmDOUT_SCRATCH3);

	if (enable)
		v |= 0x4;
	else
		v &= 0xFFFFFFFB;

	WREG32(mmDOUT_SCRATCH3, v);
#endif
}

static void sumo_construct_sclk_voltage_mapping_table(struct amdgpu_device *adev,
						      struct sumo_sclk_voltage_mapping_table *sclk_voltage_mapping_table,
						      ATOM_AVAILABLE_SCLK_LIST *table)
{
	u32 i;
	u32 n = 0;
	u32 prev_sclk = 0;

	for (i = 0; i < SUMO_MAX_HARDWARE_POWERLEVELS; i++) {
		if (table[i].ulSupportedSCLK > prev_sclk) {
			sclk_voltage_mapping_table->entries[n].sclk_frequency =
				table[i].ulSupportedSCLK;
			sclk_voltage_mapping_table->entries[n].vid_2bit =
				table[i].usVoltageIndex;
			prev_sclk = table[i].ulSupportedSCLK;
			n++;
		}
	}

	sclk_voltage_mapping_table->num_max_dpm_entries = n;
}

static void sumo_construct_vid_mapping_table(struct amdgpu_device *adev,
					     struct sumo_vid_mapping_table *vid_mapping_table,
					     ATOM_AVAILABLE_SCLK_LIST *table)
{
	u32 i, j;

	for (i = 0; i < SUMO_MAX_HARDWARE_POWERLEVELS; i++) {
		if (table[i].ulSupportedSCLK != 0) {
			vid_mapping_table->entries[table[i].usVoltageIndex].vid_7bit =
				table[i].usVoltageID;
			vid_mapping_table->entries[table[i].usVoltageIndex].vid_2bit =
				table[i].usVoltageIndex;
		}
	}

	for (i = 0; i < SUMO_MAX_NUMBER_VOLTAGES; i++) {
		if (vid_mapping_table->entries[i].vid_7bit == 0) {
			for (j = i + 1; j < SUMO_MAX_NUMBER_VOLTAGES; j++) {
				if (vid_mapping_table->entries[j].vid_7bit != 0) {
					vid_mapping_table->entries[i] =
						vid_mapping_table->entries[j];
					vid_mapping_table->entries[j].vid_7bit = 0;
					break;
				}
			}

			if (j == SUMO_MAX_NUMBER_VOLTAGES)
				break;
		}
	}

	vid_mapping_table->num_entries = i;
}

#if 0
static const struct kv_lcac_config_values sx_local_cac_cfg_kv[] = {
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

static const struct kv_lcac_config_values mc0_local_cac_cfg_kv[] = {
	{  0,       4,        1    },
	{ 0xffffffff }
};

static const struct kv_lcac_config_values mc1_local_cac_cfg_kv[] = {
	{  0,       4,        1    },
	{ 0xffffffff }
};

static const struct kv_lcac_config_values mc2_local_cac_cfg_kv[] = {
	{  0,       4,        1    },
	{ 0xffffffff }
};

static const struct kv_lcac_config_values mc3_local_cac_cfg_kv[] = {
	{  0,       4,        1    },
	{ 0xffffffff }
};

static const struct kv_lcac_config_values cpl_local_cac_cfg_kv[] = {
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

static const struct kv_lcac_config_reg sx0_cac_config_reg[] = {
	{ 0xc0400d00, 0x003e0000, 17, 0x3fc00000, 22, 0x0001fffe, 1, 0x00000001, 0 }
};

static const struct kv_lcac_config_reg mc0_cac_config_reg[] = {
	{ 0xc0400d30, 0x003e0000, 17, 0x3fc00000, 22, 0x0001fffe, 1, 0x00000001, 0 }
};

static const struct kv_lcac_config_reg mc1_cac_config_reg[] = {
	{ 0xc0400d3c, 0x003e0000, 17, 0x3fc00000, 22, 0x0001fffe, 1, 0x00000001, 0 }
};

static const struct kv_lcac_config_reg mc2_cac_config_reg[] = {
	{ 0xc0400d48, 0x003e0000, 17, 0x3fc00000, 22, 0x0001fffe, 1, 0x00000001, 0 }
};

static const struct kv_lcac_config_reg mc3_cac_config_reg[] = {
	{ 0xc0400d54, 0x003e0000, 17, 0x3fc00000, 22, 0x0001fffe, 1, 0x00000001, 0 }
};

static const struct kv_lcac_config_reg cpl_cac_config_reg[] = {
	{ 0xc0400d80, 0x003e0000, 17, 0x3fc00000, 22, 0x0001fffe, 1, 0x00000001, 0 }
};
#endif

static const struct kv_pt_config_reg didt_config_kv[] = {
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

static struct kv_ps *kv_get_ps(struct amdgpu_ps *rps)
{
	struct kv_ps *ps = rps->ps_priv;

	return ps;
}

static struct kv_power_info *kv_get_pi(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = adev->pm.dpm.priv;

	return pi;
}

#if 0
static void kv_program_local_cac_table(struct amdgpu_device *adev,
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

static int kv_program_pt_config_registers(struct amdgpu_device *adev,
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
				data = RREG32(config_regs->offset);
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
				WREG32(config_regs->offset, data);
				break;
			}
		}
		config_regs++;
	}

	return 0;
}

static void kv_do_enable_didt(struct amdgpu_device *adev, bool enable)
{
	struct kv_power_info *pi = kv_get_pi(adev);
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

static int kv_enable_didt(struct amdgpu_device *adev, bool enable)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	int ret;

	if (pi->caps_sq_ramping ||
	    pi->caps_db_ramping ||
	    pi->caps_td_ramping ||
	    pi->caps_tcp_ramping) {
		amdgpu_gfx_rlc_enter_safe_mode(adev, 0);

		if (enable) {
			ret = kv_program_pt_config_registers(adev, didt_config_kv);
			if (ret) {
				amdgpu_gfx_rlc_exit_safe_mode(adev, 0);
				return ret;
			}
		}

		kv_do_enable_didt(adev, enable);

		amdgpu_gfx_rlc_exit_safe_mode(adev, 0);
	}

	return 0;
}

#if 0
static void kv_initialize_hardware_cac_manager(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);

	if (pi->caps_cac) {
		WREG32_SMC(ixLCAC_SX0_OVR_SEL, 0);
		WREG32_SMC(ixLCAC_SX0_OVR_VAL, 0);
		kv_program_local_cac_table(adev, sx_local_cac_cfg_kv, sx0_cac_config_reg);

		WREG32_SMC(ixLCAC_MC0_OVR_SEL, 0);
		WREG32_SMC(ixLCAC_MC0_OVR_VAL, 0);
		kv_program_local_cac_table(adev, mc0_local_cac_cfg_kv, mc0_cac_config_reg);

		WREG32_SMC(ixLCAC_MC1_OVR_SEL, 0);
		WREG32_SMC(ixLCAC_MC1_OVR_VAL, 0);
		kv_program_local_cac_table(adev, mc1_local_cac_cfg_kv, mc1_cac_config_reg);

		WREG32_SMC(ixLCAC_MC2_OVR_SEL, 0);
		WREG32_SMC(ixLCAC_MC2_OVR_VAL, 0);
		kv_program_local_cac_table(adev, mc2_local_cac_cfg_kv, mc2_cac_config_reg);

		WREG32_SMC(ixLCAC_MC3_OVR_SEL, 0);
		WREG32_SMC(ixLCAC_MC3_OVR_VAL, 0);
		kv_program_local_cac_table(adev, mc3_local_cac_cfg_kv, mc3_cac_config_reg);

		WREG32_SMC(ixLCAC_CPL_OVR_SEL, 0);
		WREG32_SMC(ixLCAC_CPL_OVR_VAL, 0);
		kv_program_local_cac_table(adev, cpl_local_cac_cfg_kv, cpl_cac_config_reg);
	}
}
#endif

static int kv_enable_smc_cac(struct amdgpu_device *adev, bool enable)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	int ret = 0;

	if (pi->caps_cac) {
		if (enable) {
			ret = amdgpu_kv_notify_message_to_smu(adev, PPSMC_MSG_EnableCac);
			if (ret)
				pi->cac_enabled = false;
			else
				pi->cac_enabled = true;
		} else if (pi->cac_enabled) {
			amdgpu_kv_notify_message_to_smu(adev, PPSMC_MSG_DisableCac);
			pi->cac_enabled = false;
		}
	}

	return ret;
}

static int kv_process_firmware_header(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	u32 tmp;
	int ret;

	ret = amdgpu_kv_read_smc_sram_dword(adev, SMU7_FIRMWARE_HEADER_LOCATION +
				     offsetof(SMU7_Firmware_Header, DpmTable),
				     &tmp, pi->sram_end);

	if (ret == 0)
		pi->dpm_table_start = tmp;

	ret = amdgpu_kv_read_smc_sram_dword(adev, SMU7_FIRMWARE_HEADER_LOCATION +
				     offsetof(SMU7_Firmware_Header, SoftRegisters),
				     &tmp, pi->sram_end);

	if (ret == 0)
		pi->soft_regs_start = tmp;

	return ret;
}

static int kv_enable_dpm_voltage_scaling(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	int ret;

	pi->graphics_voltage_change_enable = 1;

	ret = amdgpu_kv_copy_bytes_to_smc(adev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, GraphicsVoltageChangeEnable),
				   &pi->graphics_voltage_change_enable,
				   sizeof(u8), pi->sram_end);

	return ret;
}

static int kv_set_dpm_interval(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	int ret;

	pi->graphics_interval = 1;

	ret = amdgpu_kv_copy_bytes_to_smc(adev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, GraphicsInterval),
				   &pi->graphics_interval,
				   sizeof(u8), pi->sram_end);

	return ret;
}

static int kv_set_dpm_boot_state(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	int ret;

	ret = amdgpu_kv_copy_bytes_to_smc(adev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, GraphicsBootLevel),
				   &pi->graphics_boot_level,
				   sizeof(u8), pi->sram_end);

	return ret;
}

static void kv_program_vc(struct amdgpu_device *adev)
{
	WREG32_SMC(ixCG_FREQ_TRAN_VOTING_0, 0x3FFFC100);
}

static void kv_clear_vc(struct amdgpu_device *adev)
{
	WREG32_SMC(ixCG_FREQ_TRAN_VOTING_0, 0);
}

static int kv_set_divider_value(struct amdgpu_device *adev,
				u32 index, u32 sclk)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	struct atom_clock_dividers dividers;
	int ret;

	ret = amdgpu_atombios_get_clock_dividers(adev, COMPUTE_ENGINE_PLL_PARAM,
						 sclk, false, &dividers);
	if (ret)
		return ret;

	pi->graphics_level[index].SclkDid = (u8)dividers.post_div;
	pi->graphics_level[index].SclkFrequency = cpu_to_be32(sclk);

	return 0;
}

static u16 kv_convert_8bit_index_to_voltage(struct amdgpu_device *adev,
					    u16 voltage)
{
	return 6200 - (voltage * 25);
}

static u16 kv_convert_2bit_index_to_voltage(struct amdgpu_device *adev,
					    u32 vid_2bit)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	u32 vid_8bit = kv_convert_vid2_to_vid7(adev,
					       &pi->sys_info.vid_mapping_table,
					       vid_2bit);

	return kv_convert_8bit_index_to_voltage(adev, (u16)vid_8bit);
}


static int kv_set_vid(struct amdgpu_device *adev, u32 index, u32 vid)
{
	struct kv_power_info *pi = kv_get_pi(adev);

	pi->graphics_level[index].VoltageDownH = (u8)pi->voltage_drop_t;
	pi->graphics_level[index].MinVddNb =
		cpu_to_be32(kv_convert_2bit_index_to_voltage(adev, vid));

	return 0;
}

static int kv_set_at(struct amdgpu_device *adev, u32 index, u32 at)
{
	struct kv_power_info *pi = kv_get_pi(adev);

	pi->graphics_level[index].AT = cpu_to_be16((u16)at);

	return 0;
}

static void kv_dpm_power_level_enable(struct amdgpu_device *adev,
				      u32 index, bool enable)
{
	struct kv_power_info *pi = kv_get_pi(adev);

	pi->graphics_level[index].EnabledForActivity = enable ? 1 : 0;
}

static void kv_start_dpm(struct amdgpu_device *adev)
{
	u32 tmp = RREG32_SMC(ixGENERAL_PWRMGT);

	tmp |= GENERAL_PWRMGT__GLOBAL_PWRMGT_EN_MASK;
	WREG32_SMC(ixGENERAL_PWRMGT, tmp);

	amdgpu_kv_smc_dpm_enable(adev, true);
}

static void kv_stop_dpm(struct amdgpu_device *adev)
{
	amdgpu_kv_smc_dpm_enable(adev, false);
}

static void kv_start_am(struct amdgpu_device *adev)
{
	u32 sclk_pwrmgt_cntl = RREG32_SMC(ixSCLK_PWRMGT_CNTL);

	sclk_pwrmgt_cntl &= ~(SCLK_PWRMGT_CNTL__RESET_SCLK_CNT_MASK |
			SCLK_PWRMGT_CNTL__RESET_BUSY_CNT_MASK);
	sclk_pwrmgt_cntl |= SCLK_PWRMGT_CNTL__DYNAMIC_PM_EN_MASK;

	WREG32_SMC(ixSCLK_PWRMGT_CNTL, sclk_pwrmgt_cntl);
}

static void kv_reset_am(struct amdgpu_device *adev)
{
	u32 sclk_pwrmgt_cntl = RREG32_SMC(ixSCLK_PWRMGT_CNTL);

	sclk_pwrmgt_cntl |= (SCLK_PWRMGT_CNTL__RESET_SCLK_CNT_MASK |
			SCLK_PWRMGT_CNTL__RESET_BUSY_CNT_MASK);

	WREG32_SMC(ixSCLK_PWRMGT_CNTL, sclk_pwrmgt_cntl);
}

static int kv_freeze_sclk_dpm(struct amdgpu_device *adev, bool freeze)
{
	return amdgpu_kv_notify_message_to_smu(adev, freeze ?
					PPSMC_MSG_SCLKDPM_FreezeLevel : PPSMC_MSG_SCLKDPM_UnfreezeLevel);
}

static int kv_force_lowest_valid(struct amdgpu_device *adev)
{
	return kv_force_dpm_lowest(adev);
}

static int kv_unforce_levels(struct amdgpu_device *adev)
{
	if (adev->asic_type == CHIP_KABINI || adev->asic_type == CHIP_MULLINS)
		return amdgpu_kv_notify_message_to_smu(adev, PPSMC_MSG_NoForcedLevel);
	else
		return kv_set_enabled_levels(adev);
}

static int kv_update_sclk_t(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	u32 low_sclk_interrupt_t = 0;
	int ret = 0;

	if (pi->caps_sclk_throttle_low_notification) {
		low_sclk_interrupt_t = cpu_to_be32(pi->low_sclk_interrupt_t);

		ret = amdgpu_kv_copy_bytes_to_smc(adev,
					   pi->dpm_table_start +
					   offsetof(SMU7_Fusion_DpmTable, LowSclkInterruptT),
					   (u8 *)&low_sclk_interrupt_t,
					   sizeof(u32), pi->sram_end);
	}
	return ret;
}

static int kv_program_bootup_state(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	u32 i;
	struct amdgpu_clock_voltage_dependency_table *table =
		&adev->pm.dpm.dyn_state.vddc_dependency_on_sclk;

	if (table && table->count) {
		for (i = pi->graphics_dpm_level_count - 1; i > 0; i--) {
			if (table->entries[i].clk == pi->boot_pl.sclk)
				break;
		}

		pi->graphics_boot_level = (u8)i;
		kv_dpm_power_level_enable(adev, i, true);
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
		kv_dpm_power_level_enable(adev, i, true);
	}
	return 0;
}

static int kv_enable_auto_thermal_throttling(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	int ret;

	pi->graphics_therm_throttle_enable = 1;

	ret = amdgpu_kv_copy_bytes_to_smc(adev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, GraphicsThermThrottleEnable),
				   &pi->graphics_therm_throttle_enable,
				   sizeof(u8), pi->sram_end);

	return ret;
}

static int kv_upload_dpm_settings(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	int ret;

	ret = amdgpu_kv_copy_bytes_to_smc(adev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, GraphicsLevel),
				   (u8 *)&pi->graphics_level,
				   sizeof(SMU7_Fusion_GraphicsLevel) * SMU7_MAX_LEVELS_GRAPHICS,
				   pi->sram_end);

	if (ret)
		return ret;

	ret = amdgpu_kv_copy_bytes_to_smc(adev,
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

static u32 kv_get_clk_bypass(struct amdgpu_device *adev, u32 clk)
{
	struct kv_power_info *pi = kv_get_pi(adev);
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

static int kv_populate_uvd_table(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	struct amdgpu_uvd_clock_voltage_dependency_table *table =
		&adev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table;
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
			(u8)kv_get_clk_bypass(adev, table->entries[i].vclk);
		pi->uvd_level[i].DClkBypassCntl =
			(u8)kv_get_clk_bypass(adev, table->entries[i].dclk);

		ret = amdgpu_atombios_get_clock_dividers(adev, COMPUTE_ENGINE_PLL_PARAM,
							 table->entries[i].vclk, false, &dividers);
		if (ret)
			return ret;
		pi->uvd_level[i].VclkDivider = (u8)dividers.post_div;

		ret = amdgpu_atombios_get_clock_dividers(adev, COMPUTE_ENGINE_PLL_PARAM,
							 table->entries[i].dclk, false, &dividers);
		if (ret)
			return ret;
		pi->uvd_level[i].DclkDivider = (u8)dividers.post_div;

		pi->uvd_level_count++;
	}

	ret = amdgpu_kv_copy_bytes_to_smc(adev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, UvdLevelCount),
				   (u8 *)&pi->uvd_level_count,
				   sizeof(u8), pi->sram_end);
	if (ret)
		return ret;

	pi->uvd_interval = 1;

	ret = amdgpu_kv_copy_bytes_to_smc(adev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, UVDInterval),
				   &pi->uvd_interval,
				   sizeof(u8), pi->sram_end);
	if (ret)
		return ret;

	ret = amdgpu_kv_copy_bytes_to_smc(adev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, UvdLevel),
				   (u8 *)&pi->uvd_level,
				   sizeof(SMU7_Fusion_UvdLevel) * SMU7_MAX_LEVELS_UVD,
				   pi->sram_end);

	return ret;

}

static int kv_populate_vce_table(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	int ret;
	u32 i;
	struct amdgpu_vce_clock_voltage_dependency_table *table =
		&adev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table;
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
			(u8)kv_get_clk_bypass(adev, table->entries[i].evclk);

		ret = amdgpu_atombios_get_clock_dividers(adev, COMPUTE_ENGINE_PLL_PARAM,
							 table->entries[i].evclk, false, &dividers);
		if (ret)
			return ret;
		pi->vce_level[i].Divider = (u8)dividers.post_div;

		pi->vce_level_count++;
	}

	ret = amdgpu_kv_copy_bytes_to_smc(adev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, VceLevelCount),
				   (u8 *)&pi->vce_level_count,
				   sizeof(u8),
				   pi->sram_end);
	if (ret)
		return ret;

	pi->vce_interval = 1;

	ret = amdgpu_kv_copy_bytes_to_smc(adev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, VCEInterval),
				   (u8 *)&pi->vce_interval,
				   sizeof(u8),
				   pi->sram_end);
	if (ret)
		return ret;

	ret = amdgpu_kv_copy_bytes_to_smc(adev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, VceLevel),
				   (u8 *)&pi->vce_level,
				   sizeof(SMU7_Fusion_ExtClkLevel) * SMU7_MAX_LEVELS_VCE,
				   pi->sram_end);

	return ret;
}

static int kv_populate_samu_table(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	struct amdgpu_clock_voltage_dependency_table *table =
		&adev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table;
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
			(u8)kv_get_clk_bypass(adev, table->entries[i].clk);

		ret = amdgpu_atombios_get_clock_dividers(adev, COMPUTE_ENGINE_PLL_PARAM,
							 table->entries[i].clk, false, &dividers);
		if (ret)
			return ret;
		pi->samu_level[i].Divider = (u8)dividers.post_div;

		pi->samu_level_count++;
	}

	ret = amdgpu_kv_copy_bytes_to_smc(adev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, SamuLevelCount),
				   (u8 *)&pi->samu_level_count,
				   sizeof(u8),
				   pi->sram_end);
	if (ret)
		return ret;

	pi->samu_interval = 1;

	ret = amdgpu_kv_copy_bytes_to_smc(adev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, SAMUInterval),
				   (u8 *)&pi->samu_interval,
				   sizeof(u8),
				   pi->sram_end);
	if (ret)
		return ret;

	ret = amdgpu_kv_copy_bytes_to_smc(adev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, SamuLevel),
				   (u8 *)&pi->samu_level,
				   sizeof(SMU7_Fusion_ExtClkLevel) * SMU7_MAX_LEVELS_SAMU,
				   pi->sram_end);
	if (ret)
		return ret;

	return ret;
}


static int kv_populate_acp_table(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	struct amdgpu_clock_voltage_dependency_table *table =
		&adev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table;
	struct atom_clock_dividers dividers;
	int ret;
	u32 i;

	if (table == NULL || table->count == 0)
		return 0;

	pi->acp_level_count = 0;
	for (i = 0; i < table->count; i++) {
		pi->acp_level[i].Frequency = cpu_to_be32(table->entries[i].clk);
		pi->acp_level[i].MinVoltage = cpu_to_be16(table->entries[i].v);

		ret = amdgpu_atombios_get_clock_dividers(adev, COMPUTE_ENGINE_PLL_PARAM,
							 table->entries[i].clk, false, &dividers);
		if (ret)
			return ret;
		pi->acp_level[i].Divider = (u8)dividers.post_div;

		pi->acp_level_count++;
	}

	ret = amdgpu_kv_copy_bytes_to_smc(adev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, AcpLevelCount),
				   (u8 *)&pi->acp_level_count,
				   sizeof(u8),
				   pi->sram_end);
	if (ret)
		return ret;

	pi->acp_interval = 1;

	ret = amdgpu_kv_copy_bytes_to_smc(adev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, ACPInterval),
				   (u8 *)&pi->acp_interval,
				   sizeof(u8),
				   pi->sram_end);
	if (ret)
		return ret;

	ret = amdgpu_kv_copy_bytes_to_smc(adev,
				   pi->dpm_table_start +
				   offsetof(SMU7_Fusion_DpmTable, AcpLevel),
				   (u8 *)&pi->acp_level,
				   sizeof(SMU7_Fusion_ExtClkLevel) * SMU7_MAX_LEVELS_ACP,
				   pi->sram_end);
	if (ret)
		return ret;

	return ret;
}

static void kv_calculate_dfs_bypass_settings(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	u32 i;
	struct amdgpu_clock_voltage_dependency_table *table =
		&adev->pm.dpm.dyn_state.vddc_dependency_on_sclk;

	if (table && table->count) {
		for (i = 0; i < pi->graphics_dpm_level_count; i++) {
			if (pi->caps_enable_dfs_bypass) {
				if (kv_get_clock_difference(table->entries[i].clk, 40000) < 200)
					pi->graphics_level[i].ClkBypassCntl = 3;
				else if (kv_get_clock_difference(table->entries[i].clk, 30000) < 200)
					pi->graphics_level[i].ClkBypassCntl = 2;
				else if (kv_get_clock_difference(table->entries[i].clk, 26600) < 200)
					pi->graphics_level[i].ClkBypassCntl = 7;
				else if (kv_get_clock_difference(table->entries[i].clk, 20000) < 200)
					pi->graphics_level[i].ClkBypassCntl = 6;
				else if (kv_get_clock_difference(table->entries[i].clk, 10000) < 200)
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

static int kv_enable_ulv(struct amdgpu_device *adev, bool enable)
{
	return amdgpu_kv_notify_message_to_smu(adev, enable ?
					PPSMC_MSG_EnableULV : PPSMC_MSG_DisableULV);
}

static void kv_reset_acp_boot_level(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);

	pi->acp_boot_level = 0xff;
}

static void kv_update_current_ps(struct amdgpu_device *adev,
				 struct amdgpu_ps *rps)
{
	struct kv_ps *new_ps = kv_get_ps(rps);
	struct kv_power_info *pi = kv_get_pi(adev);

	pi->current_rps = *rps;
	pi->current_ps = *new_ps;
	pi->current_rps.ps_priv = &pi->current_ps;
	adev->pm.dpm.current_ps = &pi->current_rps;
}

static void kv_update_requested_ps(struct amdgpu_device *adev,
				   struct amdgpu_ps *rps)
{
	struct kv_ps *new_ps = kv_get_ps(rps);
	struct kv_power_info *pi = kv_get_pi(adev);

	pi->requested_rps = *rps;
	pi->requested_ps = *new_ps;
	pi->requested_rps.ps_priv = &pi->requested_ps;
	adev->pm.dpm.requested_ps = &pi->requested_rps;
}

static void kv_dpm_enable_bapm(void *handle, bool enable)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct kv_power_info *pi = kv_get_pi(adev);
	int ret;

	if (pi->bapm_enable) {
		ret = amdgpu_kv_smc_bapm_enable(adev, enable);
		if (ret)
			DRM_ERROR("amdgpu_kv_smc_bapm_enable failed\n");
	}
}

static bool kv_is_internal_thermal_sensor(enum amdgpu_int_thermal_type sensor)
{
	switch (sensor) {
	case THERMAL_TYPE_KV:
		return true;
	case THERMAL_TYPE_NONE:
	case THERMAL_TYPE_EXTERNAL:
	case THERMAL_TYPE_EXTERNAL_GPIO:
	default:
		return false;
	}
}

static int kv_dpm_enable(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	int ret;

	ret = kv_process_firmware_header(adev);
	if (ret) {
		DRM_ERROR("kv_process_firmware_header failed\n");
		return ret;
	}
	kv_init_fps_limits(adev);
	kv_init_graphics_levels(adev);
	ret = kv_program_bootup_state(adev);
	if (ret) {
		DRM_ERROR("kv_program_bootup_state failed\n");
		return ret;
	}
	kv_calculate_dfs_bypass_settings(adev);
	ret = kv_upload_dpm_settings(adev);
	if (ret) {
		DRM_ERROR("kv_upload_dpm_settings failed\n");
		return ret;
	}
	ret = kv_populate_uvd_table(adev);
	if (ret) {
		DRM_ERROR("kv_populate_uvd_table failed\n");
		return ret;
	}
	ret = kv_populate_vce_table(adev);
	if (ret) {
		DRM_ERROR("kv_populate_vce_table failed\n");
		return ret;
	}
	ret = kv_populate_samu_table(adev);
	if (ret) {
		DRM_ERROR("kv_populate_samu_table failed\n");
		return ret;
	}
	ret = kv_populate_acp_table(adev);
	if (ret) {
		DRM_ERROR("kv_populate_acp_table failed\n");
		return ret;
	}
	kv_program_vc(adev);
#if 0
	kv_initialize_hardware_cac_manager(adev);
#endif
	kv_start_am(adev);
	if (pi->enable_auto_thermal_throttling) {
		ret = kv_enable_auto_thermal_throttling(adev);
		if (ret) {
			DRM_ERROR("kv_enable_auto_thermal_throttling failed\n");
			return ret;
		}
	}
	ret = kv_enable_dpm_voltage_scaling(adev);
	if (ret) {
		DRM_ERROR("kv_enable_dpm_voltage_scaling failed\n");
		return ret;
	}
	ret = kv_set_dpm_interval(adev);
	if (ret) {
		DRM_ERROR("kv_set_dpm_interval failed\n");
		return ret;
	}
	ret = kv_set_dpm_boot_state(adev);
	if (ret) {
		DRM_ERROR("kv_set_dpm_boot_state failed\n");
		return ret;
	}
	ret = kv_enable_ulv(adev, true);
	if (ret) {
		DRM_ERROR("kv_enable_ulv failed\n");
		return ret;
	}
	kv_start_dpm(adev);
	ret = kv_enable_didt(adev, true);
	if (ret) {
		DRM_ERROR("kv_enable_didt failed\n");
		return ret;
	}
	ret = kv_enable_smc_cac(adev, true);
	if (ret) {
		DRM_ERROR("kv_enable_smc_cac failed\n");
		return ret;
	}

	kv_reset_acp_boot_level(adev);

	ret = amdgpu_kv_smc_bapm_enable(adev, false);
	if (ret) {
		DRM_ERROR("amdgpu_kv_smc_bapm_enable failed\n");
		return ret;
	}

	if (adev->irq.installed &&
	    kv_is_internal_thermal_sensor(adev->pm.int_thermal_type)) {
		ret = kv_set_thermal_temperature_range(adev, KV_TEMP_RANGE_MIN, KV_TEMP_RANGE_MAX);
		if (ret) {
			DRM_ERROR("kv_set_thermal_temperature_range failed\n");
			return ret;
		}
		amdgpu_irq_get(adev, &adev->pm.dpm.thermal.irq,
			       AMDGPU_THERMAL_IRQ_LOW_TO_HIGH);
		amdgpu_irq_get(adev, &adev->pm.dpm.thermal.irq,
			       AMDGPU_THERMAL_IRQ_HIGH_TO_LOW);
	}

	return ret;
}

static void kv_dpm_disable(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	int err;

	amdgpu_irq_put(adev, &adev->pm.dpm.thermal.irq,
		       AMDGPU_THERMAL_IRQ_LOW_TO_HIGH);
	amdgpu_irq_put(adev, &adev->pm.dpm.thermal.irq,
		       AMDGPU_THERMAL_IRQ_HIGH_TO_LOW);

	err = amdgpu_kv_smc_bapm_enable(adev, false);
	if (err)
		DRM_ERROR("amdgpu_kv_smc_bapm_enable failed\n");

	if (adev->asic_type == CHIP_MULLINS)
		kv_enable_nb_dpm(adev, false);

	/* powerup blocks */
	kv_dpm_powergate_acp(adev, false);
	kv_dpm_powergate_samu(adev, false);
	if (pi->caps_vce_pg) /* power on the VCE block */
		amdgpu_kv_notify_message_to_smu(adev, PPSMC_MSG_VCEPowerON);
	if (pi->caps_uvd_pg) /* power on the UVD block */
		amdgpu_kv_notify_message_to_smu(adev, PPSMC_MSG_UVDPowerON);

	kv_enable_smc_cac(adev, false);
	kv_enable_didt(adev, false);
	kv_clear_vc(adev);
	kv_stop_dpm(adev);
	kv_enable_ulv(adev, false);
	kv_reset_am(adev);

	kv_update_current_ps(adev, adev->pm.dpm.boot_ps);
}

#if 0
static int kv_write_smc_soft_register(struct amdgpu_device *adev,
				      u16 reg_offset, u32 value)
{
	struct kv_power_info *pi = kv_get_pi(adev);

	return amdgpu_kv_copy_bytes_to_smc(adev, pi->soft_regs_start + reg_offset,
				    (u8 *)&value, sizeof(u16), pi->sram_end);
}

static int kv_read_smc_soft_register(struct amdgpu_device *adev,
				     u16 reg_offset, u32 *value)
{
	struct kv_power_info *pi = kv_get_pi(adev);

	return amdgpu_kv_read_smc_sram_dword(adev, pi->soft_regs_start + reg_offset,
				      value, pi->sram_end);
}
#endif

static void kv_init_sclk_t(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);

	pi->low_sclk_interrupt_t = 0;
}

static int kv_init_fps_limits(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	int ret = 0;

	if (pi->caps_fps) {
		u16 tmp;

		tmp = 45;
		pi->fps_high_t = cpu_to_be16(tmp);
		ret = amdgpu_kv_copy_bytes_to_smc(adev,
					   pi->dpm_table_start +
					   offsetof(SMU7_Fusion_DpmTable, FpsHighT),
					   (u8 *)&pi->fps_high_t,
					   sizeof(u16), pi->sram_end);

		tmp = 30;
		pi->fps_low_t = cpu_to_be16(tmp);

		ret = amdgpu_kv_copy_bytes_to_smc(adev,
					   pi->dpm_table_start +
					   offsetof(SMU7_Fusion_DpmTable, FpsLowT),
					   (u8 *)&pi->fps_low_t,
					   sizeof(u16), pi->sram_end);

	}
	return ret;
}

static void kv_init_powergate_state(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);

	pi->uvd_power_gated = false;
	pi->vce_power_gated = false;
	pi->samu_power_gated = false;
	pi->acp_power_gated = false;

}

static int kv_enable_uvd_dpm(struct amdgpu_device *adev, bool enable)
{
	return amdgpu_kv_notify_message_to_smu(adev, enable ?
					PPSMC_MSG_UVDDPM_Enable : PPSMC_MSG_UVDDPM_Disable);
}

static int kv_enable_vce_dpm(struct amdgpu_device *adev, bool enable)
{
	return amdgpu_kv_notify_message_to_smu(adev, enable ?
					PPSMC_MSG_VCEDPM_Enable : PPSMC_MSG_VCEDPM_Disable);
}

static int kv_enable_samu_dpm(struct amdgpu_device *adev, bool enable)
{
	return amdgpu_kv_notify_message_to_smu(adev, enable ?
					PPSMC_MSG_SAMUDPM_Enable : PPSMC_MSG_SAMUDPM_Disable);
}

static int kv_enable_acp_dpm(struct amdgpu_device *adev, bool enable)
{
	return amdgpu_kv_notify_message_to_smu(adev, enable ?
					PPSMC_MSG_ACPDPM_Enable : PPSMC_MSG_ACPDPM_Disable);
}

static int kv_update_uvd_dpm(struct amdgpu_device *adev, bool gate)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	struct amdgpu_uvd_clock_voltage_dependency_table *table =
		&adev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table;
	int ret;
	u32 mask;

	if (!gate) {
		if (table->count)
			pi->uvd_boot_level = table->count - 1;
		else
			pi->uvd_boot_level = 0;

		if (!pi->caps_uvd_dpm || pi->caps_stable_p_state) {
			mask = 1 << pi->uvd_boot_level;
		} else {
			mask = 0x1f;
		}

		ret = amdgpu_kv_copy_bytes_to_smc(adev,
					   pi->dpm_table_start +
					   offsetof(SMU7_Fusion_DpmTable, UvdBootLevel),
					   (uint8_t *)&pi->uvd_boot_level,
					   sizeof(u8), pi->sram_end);
		if (ret)
			return ret;

		amdgpu_kv_send_msg_to_smc_with_parameter(adev,
						  PPSMC_MSG_UVDDPM_SetEnabledMask,
						  mask);
	}

	return kv_enable_uvd_dpm(adev, !gate);
}

static u8 kv_get_vce_boot_level(struct amdgpu_device *adev, u32 evclk)
{
	u8 i;
	struct amdgpu_vce_clock_voltage_dependency_table *table =
		&adev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table;

	for (i = 0; i < table->count; i++) {
		if (table->entries[i].evclk >= evclk)
			break;
	}

	return i;
}

static int kv_update_vce_dpm(struct amdgpu_device *adev,
			     struct amdgpu_ps *amdgpu_new_state,
			     struct amdgpu_ps *amdgpu_current_state)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	struct amdgpu_vce_clock_voltage_dependency_table *table =
		&adev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table;
	int ret;

	if (amdgpu_new_state->evclk > 0 && amdgpu_current_state->evclk == 0) {
		if (pi->caps_stable_p_state)
			pi->vce_boot_level = table->count - 1;
		else
			pi->vce_boot_level = kv_get_vce_boot_level(adev, amdgpu_new_state->evclk);

		ret = amdgpu_kv_copy_bytes_to_smc(adev,
					   pi->dpm_table_start +
					   offsetof(SMU7_Fusion_DpmTable, VceBootLevel),
					   (u8 *)&pi->vce_boot_level,
					   sizeof(u8),
					   pi->sram_end);
		if (ret)
			return ret;

		if (pi->caps_stable_p_state)
			amdgpu_kv_send_msg_to_smc_with_parameter(adev,
							  PPSMC_MSG_VCEDPM_SetEnabledMask,
							  (1 << pi->vce_boot_level));
		kv_enable_vce_dpm(adev, true);
	} else if (amdgpu_new_state->evclk == 0 && amdgpu_current_state->evclk > 0) {
		kv_enable_vce_dpm(adev, false);
	}

	return 0;
}

static int kv_update_samu_dpm(struct amdgpu_device *adev, bool gate)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	struct amdgpu_clock_voltage_dependency_table *table =
		&adev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table;
	int ret;

	if (!gate) {
		if (pi->caps_stable_p_state)
			pi->samu_boot_level = table->count - 1;
		else
			pi->samu_boot_level = 0;

		ret = amdgpu_kv_copy_bytes_to_smc(adev,
					   pi->dpm_table_start +
					   offsetof(SMU7_Fusion_DpmTable, SamuBootLevel),
					   (u8 *)&pi->samu_boot_level,
					   sizeof(u8),
					   pi->sram_end);
		if (ret)
			return ret;

		if (pi->caps_stable_p_state)
			amdgpu_kv_send_msg_to_smc_with_parameter(adev,
							  PPSMC_MSG_SAMUDPM_SetEnabledMask,
							  (1 << pi->samu_boot_level));
	}

	return kv_enable_samu_dpm(adev, !gate);
}

static u8 kv_get_acp_boot_level(struct amdgpu_device *adev)
{
	return 0;
}

static void kv_update_acp_boot_level(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	u8 acp_boot_level;

	if (!pi->caps_stable_p_state) {
		acp_boot_level = kv_get_acp_boot_level(adev);
		if (acp_boot_level != pi->acp_boot_level) {
			pi->acp_boot_level = acp_boot_level;
			amdgpu_kv_send_msg_to_smc_with_parameter(adev,
							  PPSMC_MSG_ACPDPM_SetEnabledMask,
							  (1 << pi->acp_boot_level));
		}
	}
}

static int kv_update_acp_dpm(struct amdgpu_device *adev, bool gate)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	struct amdgpu_clock_voltage_dependency_table *table =
		&adev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table;
	int ret;

	if (!gate) {
		if (pi->caps_stable_p_state)
			pi->acp_boot_level = table->count - 1;
		else
			pi->acp_boot_level = kv_get_acp_boot_level(adev);

		ret = amdgpu_kv_copy_bytes_to_smc(adev,
					   pi->dpm_table_start +
					   offsetof(SMU7_Fusion_DpmTable, AcpBootLevel),
					   (u8 *)&pi->acp_boot_level,
					   sizeof(u8),
					   pi->sram_end);
		if (ret)
			return ret;

		if (pi->caps_stable_p_state)
			amdgpu_kv_send_msg_to_smc_with_parameter(adev,
							  PPSMC_MSG_ACPDPM_SetEnabledMask,
							  (1 << pi->acp_boot_level));
	}

	return kv_enable_acp_dpm(adev, !gate);
}

static void kv_dpm_powergate_uvd(void *handle, bool gate)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct kv_power_info *pi = kv_get_pi(adev);

	pi->uvd_power_gated = gate;

	if (gate) {
		/* stop the UVD block */
		amdgpu_device_ip_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_UVD,
						       AMD_PG_STATE_GATE);
		kv_update_uvd_dpm(adev, gate);
		if (pi->caps_uvd_pg)
			/* power off the UVD block */
			amdgpu_kv_notify_message_to_smu(adev, PPSMC_MSG_UVDPowerOFF);
	} else {
		if (pi->caps_uvd_pg)
			/* power on the UVD block */
			amdgpu_kv_notify_message_to_smu(adev, PPSMC_MSG_UVDPowerON);
			/* re-init the UVD block */
		kv_update_uvd_dpm(adev, gate);

		amdgpu_device_ip_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_UVD,
						       AMD_PG_STATE_UNGATE);
	}
}

static void kv_dpm_powergate_vce(void *handle, bool gate)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct kv_power_info *pi = kv_get_pi(adev);

	pi->vce_power_gated = gate;

	if (gate) {
		/* stop the VCE block */
		amdgpu_device_ip_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_VCE,
						       AMD_PG_STATE_GATE);
		kv_enable_vce_dpm(adev, false);
		if (pi->caps_vce_pg) /* power off the VCE block */
			amdgpu_kv_notify_message_to_smu(adev, PPSMC_MSG_VCEPowerOFF);
	} else {
		if (pi->caps_vce_pg) /* power on the VCE block */
			amdgpu_kv_notify_message_to_smu(adev, PPSMC_MSG_VCEPowerON);
		kv_enable_vce_dpm(adev, true);
		/* re-init the VCE block */
		amdgpu_device_ip_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_VCE,
						       AMD_PG_STATE_UNGATE);
	}
}


static void kv_dpm_powergate_samu(struct amdgpu_device *adev, bool gate)
{
	struct kv_power_info *pi = kv_get_pi(adev);

	if (pi->samu_power_gated == gate)
		return;

	pi->samu_power_gated = gate;

	if (gate) {
		kv_update_samu_dpm(adev, true);
		if (pi->caps_samu_pg)
			amdgpu_kv_notify_message_to_smu(adev, PPSMC_MSG_SAMPowerOFF);
	} else {
		if (pi->caps_samu_pg)
			amdgpu_kv_notify_message_to_smu(adev, PPSMC_MSG_SAMPowerON);
		kv_update_samu_dpm(adev, false);
	}
}

static void kv_dpm_powergate_acp(struct amdgpu_device *adev, bool gate)
{
	struct kv_power_info *pi = kv_get_pi(adev);

	if (pi->acp_power_gated == gate)
		return;

	if (adev->asic_type == CHIP_KABINI || adev->asic_type == CHIP_MULLINS)
		return;

	pi->acp_power_gated = gate;

	if (gate) {
		kv_update_acp_dpm(adev, true);
		if (pi->caps_acp_pg)
			amdgpu_kv_notify_message_to_smu(adev, PPSMC_MSG_ACPPowerOFF);
	} else {
		if (pi->caps_acp_pg)
			amdgpu_kv_notify_message_to_smu(adev, PPSMC_MSG_ACPPowerON);
		kv_update_acp_dpm(adev, false);
	}
}

static void kv_set_valid_clock_range(struct amdgpu_device *adev,
				     struct amdgpu_ps *new_rps)
{
	struct kv_ps *new_ps = kv_get_ps(new_rps);
	struct kv_power_info *pi = kv_get_pi(adev);
	u32 i;
	struct amdgpu_clock_voltage_dependency_table *table =
		&adev->pm.dpm.dyn_state.vddc_dependency_on_sclk;

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
			     new_ps->levels[new_ps->num_levels - 1].sclk))
				pi->highest_valid = pi->lowest_valid;
			else
				pi->lowest_valid =  pi->highest_valid;
		}
	}
}

static int kv_update_dfs_bypass_settings(struct amdgpu_device *adev,
					 struct amdgpu_ps *new_rps)
{
	struct kv_ps *new_ps = kv_get_ps(new_rps);
	struct kv_power_info *pi = kv_get_pi(adev);
	int ret = 0;
	u8 clk_bypass_cntl;

	if (pi->caps_enable_dfs_bypass) {
		clk_bypass_cntl = new_ps->need_dfs_bypass ?
			pi->graphics_level[pi->graphics_boot_level].ClkBypassCntl : 0;
		ret = amdgpu_kv_copy_bytes_to_smc(adev,
					   (pi->dpm_table_start +
					    offsetof(SMU7_Fusion_DpmTable, GraphicsLevel) +
					    (pi->graphics_boot_level * sizeof(SMU7_Fusion_GraphicsLevel)) +
					    offsetof(SMU7_Fusion_GraphicsLevel, ClkBypassCntl)),
					   &clk_bypass_cntl,
					   sizeof(u8), pi->sram_end);
	}

	return ret;
}

static int kv_enable_nb_dpm(struct amdgpu_device *adev,
			    bool enable)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	int ret = 0;

	if (enable) {
		if (pi->enable_nb_dpm && !pi->nb_dpm_enabled) {
			ret = amdgpu_kv_notify_message_to_smu(adev, PPSMC_MSG_NBDPM_Enable);
			if (ret == 0)
				pi->nb_dpm_enabled = true;
		}
	} else {
		if (pi->enable_nb_dpm && pi->nb_dpm_enabled) {
			ret = amdgpu_kv_notify_message_to_smu(adev, PPSMC_MSG_NBDPM_Disable);
			if (ret == 0)
				pi->nb_dpm_enabled = false;
		}
	}

	return ret;
}

static int kv_dpm_force_performance_level(void *handle,
					  enum amd_dpm_forced_level level)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (level == AMD_DPM_FORCED_LEVEL_HIGH) {
		ret = kv_force_dpm_highest(adev);
		if (ret)
			return ret;
	} else if (level == AMD_DPM_FORCED_LEVEL_LOW) {
		ret = kv_force_dpm_lowest(adev);
		if (ret)
			return ret;
	} else if (level == AMD_DPM_FORCED_LEVEL_AUTO) {
		ret = kv_unforce_levels(adev);
		if (ret)
			return ret;
	}

	adev->pm.dpm.forced_level = level;

	return 0;
}

static int kv_dpm_pre_set_power_state(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct kv_power_info *pi = kv_get_pi(adev);
	struct amdgpu_ps requested_ps = *adev->pm.dpm.requested_ps;
	struct amdgpu_ps *new_ps = &requested_ps;

	kv_update_requested_ps(adev, new_ps);

	kv_apply_state_adjust_rules(adev,
				    &pi->requested_rps,
				    &pi->current_rps);

	return 0;
}

static int kv_dpm_set_power_state(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct kv_power_info *pi = kv_get_pi(adev);
	struct amdgpu_ps *new_ps = &pi->requested_rps;
	struct amdgpu_ps *old_ps = &pi->current_rps;
	int ret;

	if (pi->bapm_enable) {
		ret = amdgpu_kv_smc_bapm_enable(adev, adev->pm.ac_power);
		if (ret) {
			DRM_ERROR("amdgpu_kv_smc_bapm_enable failed\n");
			return ret;
		}
	}

	if (adev->asic_type == CHIP_KABINI || adev->asic_type == CHIP_MULLINS) {
		if (pi->enable_dpm) {
			kv_set_valid_clock_range(adev, new_ps);
			kv_update_dfs_bypass_settings(adev, new_ps);
			ret = kv_calculate_ds_divider(adev);
			if (ret) {
				DRM_ERROR("kv_calculate_ds_divider failed\n");
				return ret;
			}
			kv_calculate_nbps_level_settings(adev);
			kv_calculate_dpm_settings(adev);
			kv_force_lowest_valid(adev);
			kv_enable_new_levels(adev);
			kv_upload_dpm_settings(adev);
			kv_program_nbps_index_settings(adev, new_ps);
			kv_unforce_levels(adev);
			kv_set_enabled_levels(adev);
			kv_force_lowest_valid(adev);
			kv_unforce_levels(adev);

			ret = kv_update_vce_dpm(adev, new_ps, old_ps);
			if (ret) {
				DRM_ERROR("kv_update_vce_dpm failed\n");
				return ret;
			}
			kv_update_sclk_t(adev);
			if (adev->asic_type == CHIP_MULLINS)
				kv_enable_nb_dpm(adev, true);
		}
	} else {
		if (pi->enable_dpm) {
			kv_set_valid_clock_range(adev, new_ps);
			kv_update_dfs_bypass_settings(adev, new_ps);
			ret = kv_calculate_ds_divider(adev);
			if (ret) {
				DRM_ERROR("kv_calculate_ds_divider failed\n");
				return ret;
			}
			kv_calculate_nbps_level_settings(adev);
			kv_calculate_dpm_settings(adev);
			kv_freeze_sclk_dpm(adev, true);
			kv_upload_dpm_settings(adev);
			kv_program_nbps_index_settings(adev, new_ps);
			kv_freeze_sclk_dpm(adev, false);
			kv_set_enabled_levels(adev);
			ret = kv_update_vce_dpm(adev, new_ps, old_ps);
			if (ret) {
				DRM_ERROR("kv_update_vce_dpm failed\n");
				return ret;
			}
			kv_update_acp_boot_level(adev);
			kv_update_sclk_t(adev);
			kv_enable_nb_dpm(adev, true);
		}
	}

	return 0;
}

static void kv_dpm_post_set_power_state(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct kv_power_info *pi = kv_get_pi(adev);
	struct amdgpu_ps *new_ps = &pi->requested_rps;

	kv_update_current_ps(adev, new_ps);
}

static void kv_dpm_setup_asic(struct amdgpu_device *adev)
{
	sumo_take_smu_control(adev, true);
	kv_init_powergate_state(adev);
	kv_init_sclk_t(adev);
}

#if 0
static void kv_dpm_reset_asic(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);

	if (adev->asic_type == CHIP_KABINI || adev->asic_type == CHIP_MULLINS) {
		kv_force_lowest_valid(adev);
		kv_init_graphics_levels(adev);
		kv_program_bootup_state(adev);
		kv_upload_dpm_settings(adev);
		kv_force_lowest_valid(adev);
		kv_unforce_levels(adev);
	} else {
		kv_init_graphics_levels(adev);
		kv_program_bootup_state(adev);
		kv_freeze_sclk_dpm(adev, true);
		kv_upload_dpm_settings(adev);
		kv_freeze_sclk_dpm(adev, false);
		kv_set_enabled_level(adev, pi->graphics_boot_level);
	}
}
#endif

static void kv_construct_max_power_limits_table(struct amdgpu_device *adev,
						struct amdgpu_clock_and_voltage_limits *table)
{
	struct kv_power_info *pi = kv_get_pi(adev);

	if (pi->sys_info.sclk_voltage_mapping_table.num_max_dpm_entries > 0) {
		int idx = pi->sys_info.sclk_voltage_mapping_table.num_max_dpm_entries - 1;
		table->sclk =
			pi->sys_info.sclk_voltage_mapping_table.entries[idx].sclk_frequency;
		table->vddc =
			kv_convert_2bit_index_to_voltage(adev,
							 pi->sys_info.sclk_voltage_mapping_table.entries[idx].vid_2bit);
	}

	table->mclk = pi->sys_info.nbp_memory_clock[0];
}

static void kv_patch_voltage_values(struct amdgpu_device *adev)
{
	int i;
	struct amdgpu_uvd_clock_voltage_dependency_table *uvd_table =
		&adev->pm.dpm.dyn_state.uvd_clock_voltage_dependency_table;
	struct amdgpu_vce_clock_voltage_dependency_table *vce_table =
		&adev->pm.dpm.dyn_state.vce_clock_voltage_dependency_table;
	struct amdgpu_clock_voltage_dependency_table *samu_table =
		&adev->pm.dpm.dyn_state.samu_clock_voltage_dependency_table;
	struct amdgpu_clock_voltage_dependency_table *acp_table =
		&adev->pm.dpm.dyn_state.acp_clock_voltage_dependency_table;

	if (uvd_table->count) {
		for (i = 0; i < uvd_table->count; i++)
			uvd_table->entries[i].v =
				kv_convert_8bit_index_to_voltage(adev,
								 uvd_table->entries[i].v);
	}

	if (vce_table->count) {
		for (i = 0; i < vce_table->count; i++)
			vce_table->entries[i].v =
				kv_convert_8bit_index_to_voltage(adev,
								 vce_table->entries[i].v);
	}

	if (samu_table->count) {
		for (i = 0; i < samu_table->count; i++)
			samu_table->entries[i].v =
				kv_convert_8bit_index_to_voltage(adev,
								 samu_table->entries[i].v);
	}

	if (acp_table->count) {
		for (i = 0; i < acp_table->count; i++)
			acp_table->entries[i].v =
				kv_convert_8bit_index_to_voltage(adev,
								 acp_table->entries[i].v);
	}

}

static void kv_construct_boot_state(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);

	pi->boot_pl.sclk = pi->sys_info.bootup_sclk;
	pi->boot_pl.vddc_index = pi->sys_info.bootup_nb_voltage_index;
	pi->boot_pl.ds_divider_index = 0;
	pi->boot_pl.ss_divider_index = 0;
	pi->boot_pl.allow_gnb_slow = 1;
	pi->boot_pl.force_nbp_state = 0;
	pi->boot_pl.display_wm = 0;
	pi->boot_pl.vce_wm = 0;
}

static int kv_force_dpm_highest(struct amdgpu_device *adev)
{
	int ret;
	u32 enable_mask, i;

	ret = amdgpu_kv_dpm_get_enable_mask(adev, &enable_mask);
	if (ret)
		return ret;

	for (i = SMU7_MAX_LEVELS_GRAPHICS - 1; i > 0; i--) {
		if (enable_mask & (1 << i))
			break;
	}

	if (adev->asic_type == CHIP_KABINI || adev->asic_type == CHIP_MULLINS)
		return amdgpu_kv_send_msg_to_smc_with_parameter(adev, PPSMC_MSG_DPM_ForceState, i);
	else
		return kv_set_enabled_level(adev, i);
}

static int kv_force_dpm_lowest(struct amdgpu_device *adev)
{
	int ret;
	u32 enable_mask, i;

	ret = amdgpu_kv_dpm_get_enable_mask(adev, &enable_mask);
	if (ret)
		return ret;

	for (i = 0; i < SMU7_MAX_LEVELS_GRAPHICS; i++) {
		if (enable_mask & (1 << i))
			break;
	}

	if (adev->asic_type == CHIP_KABINI || adev->asic_type == CHIP_MULLINS)
		return amdgpu_kv_send_msg_to_smc_with_parameter(adev, PPSMC_MSG_DPM_ForceState, i);
	else
		return kv_set_enabled_level(adev, i);
}

static u8 kv_get_sleep_divider_id_from_clock(struct amdgpu_device *adev,
					     u32 sclk, u32 min_sclk_in_sr)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	u32 i;
	u32 temp;
	u32 min = max(min_sclk_in_sr, (u32)KV_MINIMUM_ENGINE_CLOCK);

	if (sclk < min)
		return 0;

	if (!pi->caps_sclk_ds)
		return 0;

	for (i = KV_MAX_DEEPSLEEP_DIVIDER_ID; i > 0; i--) {
		temp = sclk >> i;
		if (temp >= min)
			break;
	}

	return (u8)i;
}

static int kv_get_high_voltage_limit(struct amdgpu_device *adev, int *limit)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	struct amdgpu_clock_voltage_dependency_table *table =
		&adev->pm.dpm.dyn_state.vddc_dependency_on_sclk;
	int i;

	if (table && table->count) {
		for (i = table->count - 1; i >= 0; i--) {
			if (pi->high_voltage_t &&
			    (kv_convert_8bit_index_to_voltage(adev, table->entries[i].v) <=
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
			    (kv_convert_2bit_index_to_voltage(adev, table->entries[i].vid_2bit) <=
			     pi->high_voltage_t)) {
				*limit = i;
				return 0;
			}
		}
	}

	*limit = 0;
	return 0;
}

static void kv_apply_state_adjust_rules(struct amdgpu_device *adev,
					struct amdgpu_ps *new_rps,
					struct amdgpu_ps *old_rps)
{
	struct kv_ps *ps = kv_get_ps(new_rps);
	struct kv_power_info *pi = kv_get_pi(adev);
	u32 min_sclk = 10000; /* ??? */
	u32 sclk, mclk = 0;
	int i, limit;
	bool force_high;
	struct amdgpu_clock_voltage_dependency_table *table =
		&adev->pm.dpm.dyn_state.vddc_dependency_on_sclk;
	u32 stable_p_state_sclk = 0;
	struct amdgpu_clock_and_voltage_limits *max_limits =
		&adev->pm.dpm.dyn_state.max_clock_voltage_on_ac;

	if (new_rps->vce_active) {
		new_rps->evclk = adev->pm.dpm.vce_states[adev->pm.dpm.vce_level].evclk;
		new_rps->ecclk = adev->pm.dpm.vce_states[adev->pm.dpm.vce_level].ecclk;
	} else {
		new_rps->evclk = 0;
		new_rps->ecclk = 0;
	}

	mclk = max_limits->mclk;
	sclk = min_sclk;

	if (pi->caps_stable_p_state) {
		stable_p_state_sclk = (max_limits->sclk * 75) / 100;

		for (i = table->count - 1; i >= 0; i--) {
			if (stable_p_state_sclk >= table->entries[i].clk) {
				stable_p_state_sclk = table->entries[i].clk;
				break;
			}
		}

		if (i > 0)
			stable_p_state_sclk = table->entries[0].clk;

		sclk = stable_p_state_sclk;
	}

	if (new_rps->vce_active) {
		if (sclk < adev->pm.dpm.vce_states[adev->pm.dpm.vce_level].sclk)
			sclk = adev->pm.dpm.vce_states[adev->pm.dpm.vce_level].sclk;
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
			     kv_convert_8bit_index_to_voltage(adev, ps->levels[i].vddc_index))) {
				kv_get_high_voltage_limit(adev, &limit);
				ps->levels[i].sclk = table->entries[limit].clk;
			}
		}
	} else {
		struct sumo_sclk_voltage_mapping_table *table =
			&pi->sys_info.sclk_voltage_mapping_table;

		for (i = 0; i < ps->num_levels; i++) {
			if (pi->high_voltage_t &&
			    (pi->high_voltage_t <
			     kv_convert_8bit_index_to_voltage(adev, ps->levels[i].vddc_index))) {
				kv_get_high_voltage_limit(adev, &limit);
				ps->levels[i].sclk = table->entries[limit].sclk_frequency;
			}
		}
	}

	if (pi->caps_stable_p_state) {
		for (i = 0; i < ps->num_levels; i++) {
			ps->levels[i].sclk = stable_p_state_sclk;
		}
	}

	pi->video_start = new_rps->dclk || new_rps->vclk ||
		new_rps->evclk || new_rps->ecclk;

	if ((new_rps->class & ATOM_PPLIB_CLASSIFICATION_UI_MASK) ==
	    ATOM_PPLIB_CLASSIFICATION_UI_BATTERY)
		pi->battery_state = true;
	else
		pi->battery_state = false;

	if (adev->asic_type == CHIP_KABINI || adev->asic_type == CHIP_MULLINS) {
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
				pi->video_start || (adev->pm.dpm.new_active_crtc_count >= 3) ||
				pi->disable_nb_ps3_in_battery;
			ps->dpm0_pg_nb_ps_lo = force_high ? 0x2 : 0x3;
			ps->dpm0_pg_nb_ps_hi = 0x2;
			ps->dpmx_nb_ps_lo = force_high ? 0x2 : 0x3;
			ps->dpmx_nb_ps_hi = 0x2;
		}
	}
}

static void kv_dpm_power_level_enabled_for_throttle(struct amdgpu_device *adev,
						    u32 index, bool enable)
{
	struct kv_power_info *pi = kv_get_pi(adev);

	pi->graphics_level[index].EnabledForThrottle = enable ? 1 : 0;
}

static int kv_calculate_ds_divider(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	u32 sclk_in_sr = 10000; /* ??? */
	u32 i;

	if (pi->lowest_valid > pi->highest_valid)
		return -EINVAL;

	for (i = pi->lowest_valid; i <= pi->highest_valid; i++) {
		pi->graphics_level[i].DeepSleepDivId =
			kv_get_sleep_divider_id_from_clock(adev,
							   be32_to_cpu(pi->graphics_level[i].SclkFrequency),
							   sclk_in_sr);
	}
	return 0;
}

static int kv_calculate_nbps_level_settings(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	u32 i;
	bool force_high;
	struct amdgpu_clock_and_voltage_limits *max_limits =
		&adev->pm.dpm.dyn_state.max_clock_voltage_on_ac;
	u32 mclk = max_limits->mclk;

	if (pi->lowest_valid > pi->highest_valid)
		return -EINVAL;

	if (adev->asic_type == CHIP_KABINI || adev->asic_type == CHIP_MULLINS) {
		for (i = pi->lowest_valid; i <= pi->highest_valid; i++) {
			pi->graphics_level[i].GnbSlow = 1;
			pi->graphics_level[i].ForceNbPs1 = 0;
			pi->graphics_level[i].UpH = 0;
		}

		if (!pi->sys_info.nb_dpm_enable)
			return 0;

		force_high = ((mclk >= pi->sys_info.nbp_memory_clock[3]) ||
			      (adev->pm.dpm.new_active_crtc_count >= 3) || pi->video_start);

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

static int kv_calculate_dpm_settings(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	u32 i;

	if (pi->lowest_valid > pi->highest_valid)
		return -EINVAL;

	for (i = pi->lowest_valid; i <= pi->highest_valid; i++)
		pi->graphics_level[i].DisplayWatermark = (i == pi->highest_valid) ? 1 : 0;

	return 0;
}

static void kv_init_graphics_levels(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	u32 i;
	struct amdgpu_clock_voltage_dependency_table *table =
		&adev->pm.dpm.dyn_state.vddc_dependency_on_sclk;

	if (table && table->count) {
		u32 vid_2bit;

		pi->graphics_dpm_level_count = 0;
		for (i = 0; i < table->count; i++) {
			if (pi->high_voltage_t &&
			    (pi->high_voltage_t <
			     kv_convert_8bit_index_to_voltage(adev, table->entries[i].v)))
				break;

			kv_set_divider_value(adev, i, table->entries[i].clk);
			vid_2bit = kv_convert_vid7_to_vid2(adev,
							   &pi->sys_info.vid_mapping_table,
							   table->entries[i].v);
			kv_set_vid(adev, i, vid_2bit);
			kv_set_at(adev, i, pi->at[i]);
			kv_dpm_power_level_enabled_for_throttle(adev, i, true);
			pi->graphics_dpm_level_count++;
		}
	} else {
		struct sumo_sclk_voltage_mapping_table *table =
			&pi->sys_info.sclk_voltage_mapping_table;

		pi->graphics_dpm_level_count = 0;
		for (i = 0; i < table->num_max_dpm_entries; i++) {
			if (pi->high_voltage_t &&
			    pi->high_voltage_t <
			    kv_convert_2bit_index_to_voltage(adev, table->entries[i].vid_2bit))
				break;

			kv_set_divider_value(adev, i, table->entries[i].sclk_frequency);
			kv_set_vid(adev, i, table->entries[i].vid_2bit);
			kv_set_at(adev, i, pi->at[i]);
			kv_dpm_power_level_enabled_for_throttle(adev, i, true);
			pi->graphics_dpm_level_count++;
		}
	}

	for (i = 0; i < SMU7_MAX_LEVELS_GRAPHICS; i++)
		kv_dpm_power_level_enable(adev, i, false);
}

static void kv_enable_new_levels(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	u32 i;

	for (i = 0; i < SMU7_MAX_LEVELS_GRAPHICS; i++) {
		if (i >= pi->lowest_valid && i <= pi->highest_valid)
			kv_dpm_power_level_enable(adev, i, true);
	}
}

static int kv_set_enabled_level(struct amdgpu_device *adev, u32 level)
{
	u32 new_mask = (1 << level);

	return amdgpu_kv_send_msg_to_smc_with_parameter(adev,
						 PPSMC_MSG_SCLKDPM_SetEnabledMask,
						 new_mask);
}

static int kv_set_enabled_levels(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	u32 i, new_mask = 0;

	for (i = pi->lowest_valid; i <= pi->highest_valid; i++)
		new_mask |= (1 << i);

	return amdgpu_kv_send_msg_to_smc_with_parameter(adev,
						 PPSMC_MSG_SCLKDPM_SetEnabledMask,
						 new_mask);
}

static void kv_program_nbps_index_settings(struct amdgpu_device *adev,
					   struct amdgpu_ps *new_rps)
{
	struct kv_ps *new_ps = kv_get_ps(new_rps);
	struct kv_power_info *pi = kv_get_pi(adev);
	u32 nbdpmconfig1;

	if (adev->asic_type == CHIP_KABINI || adev->asic_type == CHIP_MULLINS)
		return;

	if (pi->sys_info.nb_dpm_enable) {
		nbdpmconfig1 = RREG32_SMC(ixNB_DPM_CONFIG_1);
		nbdpmconfig1 &= ~(NB_DPM_CONFIG_1__Dpm0PgNbPsLo_MASK |
				NB_DPM_CONFIG_1__Dpm0PgNbPsHi_MASK |
				NB_DPM_CONFIG_1__DpmXNbPsLo_MASK |
				NB_DPM_CONFIG_1__DpmXNbPsHi_MASK);
		nbdpmconfig1 |= (new_ps->dpm0_pg_nb_ps_lo << NB_DPM_CONFIG_1__Dpm0PgNbPsLo__SHIFT) |
				(new_ps->dpm0_pg_nb_ps_hi << NB_DPM_CONFIG_1__Dpm0PgNbPsHi__SHIFT) |
				(new_ps->dpmx_nb_ps_lo << NB_DPM_CONFIG_1__DpmXNbPsLo__SHIFT) |
				(new_ps->dpmx_nb_ps_hi << NB_DPM_CONFIG_1__DpmXNbPsHi__SHIFT);
		WREG32_SMC(ixNB_DPM_CONFIG_1, nbdpmconfig1);
	}
}

static int kv_set_thermal_temperature_range(struct amdgpu_device *adev,
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

	tmp = RREG32_SMC(ixCG_THERMAL_INT_CTRL);
	tmp &= ~(CG_THERMAL_INT_CTRL__DIG_THERM_INTH_MASK |
		CG_THERMAL_INT_CTRL__DIG_THERM_INTL_MASK);
	tmp |= ((49 + (high_temp / 1000)) << CG_THERMAL_INT_CTRL__DIG_THERM_INTH__SHIFT) |
		((49 + (low_temp / 1000)) << CG_THERMAL_INT_CTRL__DIG_THERM_INTL__SHIFT);
	WREG32_SMC(ixCG_THERMAL_INT_CTRL, tmp);

	adev->pm.dpm.thermal.min_temp = low_temp;
	adev->pm.dpm.thermal.max_temp = high_temp;

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

static int kv_parse_sys_info_table(struct amdgpu_device *adev)
{
	struct kv_power_info *pi = kv_get_pi(adev);
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, IntegratedSystemInfo);
	union igp_info *igp_info;
	u8 frev, crev;
	u16 data_offset;
	int i;

	if (amdgpu_atom_parse_data_header(mode_info->atom_context, index, NULL,
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

		sumo_construct_sclk_voltage_mapping_table(adev,
							  &pi->sys_info.sclk_voltage_mapping_table,
							  igp_info->info_8.sAvail_SCLK);

		sumo_construct_vid_mapping_table(adev,
						 &pi->sys_info.vid_mapping_table,
						 igp_info->info_8.sAvail_SCLK);

		kv_construct_max_power_limits_table(adev,
						    &adev->pm.dpm.dyn_state.max_clock_voltage_on_ac);
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

static void kv_patch_boot_state(struct amdgpu_device *adev,
				struct kv_ps *ps)
{
	struct kv_power_info *pi = kv_get_pi(adev);

	ps->num_levels = 1;
	ps->levels[0] = pi->boot_pl;
}

static void kv_parse_pplib_non_clock_info(struct amdgpu_device *adev,
					  struct amdgpu_ps *rps,
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
		adev->pm.dpm.boot_ps = rps;
		kv_patch_boot_state(adev, ps);
	}
	if (rps->class & ATOM_PPLIB_CLASSIFICATION_UVDSTATE)
		adev->pm.dpm.uvd_ps = rps;
}

static void kv_parse_pplib_clock_info(struct amdgpu_device *adev,
				      struct amdgpu_ps *rps, int index,
					union pplib_clock_info *clock_info)
{
	struct kv_power_info *pi = kv_get_pi(adev);
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

static int kv_parse_power_table(struct amdgpu_device *adev)
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
	struct kv_ps *ps;

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

	adev->pm.dpm.ps = kcalloc(state_array->ucNumEntries,
				  sizeof(struct amdgpu_ps),
				  GFP_KERNEL);
	if (!adev->pm.dpm.ps)
		return -ENOMEM;
	power_state_offset = (u8 *)state_array->states;
	for (i = 0; i < state_array->ucNumEntries; i++) {
		u8 *idx;
		power_state = (union pplib_power_state *)power_state_offset;
		non_clock_array_index = power_state->v2.nonClockInfoIndex;
		non_clock_info = (struct _ATOM_PPLIB_NONCLOCK_INFO *)
			&non_clock_info_array->nonClockInfo[non_clock_array_index];
		ps = kzalloc(sizeof(struct kv_ps), GFP_KERNEL);
		if (ps == NULL)
			return -ENOMEM;
		adev->pm.dpm.ps[i].ps_priv = ps;
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
			kv_parse_pplib_clock_info(adev,
						  &adev->pm.dpm.ps[i], k,
						  clock_info);
			k++;
		}
		kv_parse_pplib_non_clock_info(adev, &adev->pm.dpm.ps[i],
					      non_clock_info,
					      non_clock_info_array->ucEntrySize);
		power_state_offset += 2 + power_state->v2.ucNumDPMLevels;
	}
	adev->pm.dpm.num_ps = state_array->ucNumEntries;

	/* fill in the vce power states */
	for (i = 0; i < adev->pm.dpm.num_of_vce_states; i++) {
		u32 sclk;
		clock_array_index = adev->pm.dpm.vce_states[i].clk_idx;
		clock_info = (union pplib_clock_info *)
			&clock_info_array->clockInfo[clock_array_index * clock_info_array->ucEntrySize];
		sclk = le16_to_cpu(clock_info->sumo.usEngineClockLow);
		sclk |= clock_info->sumo.ucEngineClockHigh << 16;
		adev->pm.dpm.vce_states[i].sclk = sclk;
		adev->pm.dpm.vce_states[i].mclk = 0;
	}

	return 0;
}

static int kv_dpm_init(struct amdgpu_device *adev)
{
	struct kv_power_info *pi;
	int ret, i;

	pi = kzalloc(sizeof(struct kv_power_info), GFP_KERNEL);
	if (pi == NULL)
		return -ENOMEM;
	adev->pm.dpm.priv = pi;

	ret = amdgpu_get_platform_caps(adev);
	if (ret)
		return ret;

	ret = amdgpu_parse_extended_power_table(adev);
	if (ret)
		return ret;

	for (i = 0; i < SUMO_MAX_HARDWARE_POWERLEVELS; i++)
		pi->at[i] = TRINITY_AT_DFLT;

	pi->sram_end = SMC_RAM_END;

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

	if (adev->pm.pp_feature & PP_SCLK_DEEP_SLEEP_MASK)
		pi->caps_sclk_ds = true;
	else
		pi->caps_sclk_ds = false;

	pi->enable_auto_thermal_throttling = true;
	pi->disable_nb_ps3_in_battery = false;
	if (amdgpu_bapm == 0)
		pi->bapm_enable = false;
	else
		pi->bapm_enable = true;
	pi->voltage_drop_t = 0;
	pi->caps_sclk_throttle_low_notification = false;
	pi->caps_fps = false; /* true? */
	pi->caps_uvd_pg = (adev->pg_flags & AMD_PG_SUPPORT_UVD) ? true : false;
	pi->caps_uvd_dpm = true;
	pi->caps_vce_pg = (adev->pg_flags & AMD_PG_SUPPORT_VCE) ? true : false;
	pi->caps_samu_pg = (adev->pg_flags & AMD_PG_SUPPORT_SAMU) ? true : false;
	pi->caps_acp_pg = (adev->pg_flags & AMD_PG_SUPPORT_ACP) ? true : false;
	pi->caps_stable_p_state = false;

	ret = kv_parse_sys_info_table(adev);
	if (ret)
		return ret;

	kv_patch_voltage_values(adev);
	kv_construct_boot_state(adev);

	ret = kv_parse_power_table(adev);
	if (ret)
		return ret;

	pi->enable_dpm = true;

	return 0;
}

static void
kv_dpm_debugfs_print_current_performance_level(void *handle,
					       struct seq_file *m)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct kv_power_info *pi = kv_get_pi(adev);
	u32 current_index =
		(RREG32_SMC(ixTARGET_AND_CURRENT_PROFILE_INDEX) &
		TARGET_AND_CURRENT_PROFILE_INDEX__CURR_SCLK_INDEX_MASK) >>
		TARGET_AND_CURRENT_PROFILE_INDEX__CURR_SCLK_INDEX__SHIFT;
	u32 sclk, tmp;
	u16 vddc;

	if (current_index >= SMU__NUM_SCLK_DPM_STATE) {
		seq_printf(m, "invalid dpm profile %d\n", current_index);
	} else {
		sclk = be32_to_cpu(pi->graphics_level[current_index].SclkFrequency);
		tmp = (RREG32_SMC(ixSMU_VOLTAGE_STATUS) &
			SMU_VOLTAGE_STATUS__SMU_VOLTAGE_CURRENT_LEVEL_MASK) >>
			SMU_VOLTAGE_STATUS__SMU_VOLTAGE_CURRENT_LEVEL__SHIFT;
		vddc = kv_convert_8bit_index_to_voltage(adev, (u16)tmp);
		seq_printf(m, "uvd    %sabled\n", pi->uvd_power_gated ? "dis" : "en");
		seq_printf(m, "vce    %sabled\n", pi->vce_power_gated ? "dis" : "en");
		seq_printf(m, "power level %d    sclk: %u vddc: %u\n",
			   current_index, sclk, vddc);
	}
}

static void
kv_dpm_print_power_state(void *handle, void *request_ps)
{
	int i;
	struct amdgpu_ps *rps = (struct amdgpu_ps *)request_ps;
	struct kv_ps *ps = kv_get_ps(rps);
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_dpm_print_class_info(rps->class, rps->class2);
	amdgpu_dpm_print_cap_info(rps->caps);
	printk("\tuvd    vclk: %d dclk: %d\n", rps->vclk, rps->dclk);
	for (i = 0; i < ps->num_levels; i++) {
		struct kv_pl *pl = &ps->levels[i];
		printk("\t\tpower level %d    sclk: %u vddc: %u\n",
		       i, pl->sclk,
		       kv_convert_8bit_index_to_voltage(adev, pl->vddc_index));
	}
	amdgpu_dpm_print_ps_status(adev, rps);
}

static void kv_dpm_fini(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->pm.dpm.num_ps; i++) {
		kfree(adev->pm.dpm.ps[i].ps_priv);
	}
	kfree(adev->pm.dpm.ps);
	kfree(adev->pm.dpm.priv);
	amdgpu_free_extended_power_table(adev);
}

static void kv_dpm_display_configuration_changed(void *handle)
{

}

static u32 kv_dpm_get_sclk(void *handle, bool low)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct kv_power_info *pi = kv_get_pi(adev);
	struct kv_ps *requested_state = kv_get_ps(&pi->requested_rps);

	if (low)
		return requested_state->levels[0].sclk;
	else
		return requested_state->levels[requested_state->num_levels - 1].sclk;
}

static u32 kv_dpm_get_mclk(void *handle, bool low)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct kv_power_info *pi = kv_get_pi(adev);

	return pi->sys_info.bootup_uma_clk;
}

/* get temperature in millidegrees */
static int kv_dpm_get_temp(void *handle)
{
	u32 temp;
	int actual_temp = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	temp = RREG32_SMC(0xC0300E0C);

	if (temp)
		actual_temp = (temp / 8) - 49;
	else
		actual_temp = 0;

	actual_temp = actual_temp * 1000;

	return actual_temp;
}

static int kv_dpm_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->powerplay.pp_funcs = &kv_dpm_funcs;
	adev->powerplay.pp_handle = adev;
	kv_dpm_set_irq_funcs(adev);

	return 0;
}

static int kv_dpm_late_init(void *handle)
{
	/* powerdown unused blocks for now */
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (!adev->pm.dpm_enabled)
		return 0;

	kv_dpm_powergate_acp(adev, true);
	kv_dpm_powergate_samu(adev, true);

	return 0;
}

static int kv_dpm_sw_init(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	ret = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, 230,
				&adev->pm.dpm.thermal.irq);
	if (ret)
		return ret;

	ret = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, 231,
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

	if (amdgpu_dpm == 0)
		return 0;

	INIT_WORK(&adev->pm.dpm.thermal.work, amdgpu_dpm_thermal_work_handler);
	ret = kv_dpm_init(adev);
	if (ret)
		goto dpm_failed;
	adev->pm.dpm.current_ps = adev->pm.dpm.requested_ps = adev->pm.dpm.boot_ps;
	if (amdgpu_dpm == 1)
		amdgpu_pm_print_power_states(adev);
	DRM_INFO("amdgpu: dpm initialized\n");

	return 0;

dpm_failed:
	kv_dpm_fini(adev);
	DRM_ERROR("amdgpu: dpm initialization failed\n");
	return ret;
}

static int kv_dpm_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	flush_work(&adev->pm.dpm.thermal.work);

	kv_dpm_fini(adev);

	return 0;
}

static int kv_dpm_hw_init(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (!amdgpu_dpm)
		return 0;

	kv_dpm_setup_asic(adev);
	ret = kv_dpm_enable(adev);
	if (ret)
		adev->pm.dpm_enabled = false;
	else
		adev->pm.dpm_enabled = true;
	amdgpu_legacy_dpm_compute_clocks(adev);
	return ret;
}

static int kv_dpm_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->pm.dpm_enabled)
		kv_dpm_disable(adev);

	return 0;
}

static int kv_dpm_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->pm.dpm_enabled) {
		/* disable dpm */
		kv_dpm_disable(adev);
		/* reset the power state */
		adev->pm.dpm.current_ps = adev->pm.dpm.requested_ps = adev->pm.dpm.boot_ps;
	}
	return 0;
}

static int kv_dpm_resume(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->pm.dpm_enabled) {
		/* asic init will reset to the boot state */
		kv_dpm_setup_asic(adev);
		ret = kv_dpm_enable(adev);
		if (ret)
			adev->pm.dpm_enabled = false;
		else
			adev->pm.dpm_enabled = true;
		if (adev->pm.dpm_enabled)
			amdgpu_legacy_dpm_compute_clocks(adev);
	}
	return 0;
}

static bool kv_dpm_is_idle(void *handle)
{
	return true;
}

static int kv_dpm_wait_for_idle(void *handle)
{
	return 0;
}


static int kv_dpm_soft_reset(void *handle)
{
	return 0;
}

static int kv_dpm_set_interrupt_state(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *src,
				      unsigned type,
				      enum amdgpu_interrupt_state state)
{
	u32 cg_thermal_int;

	switch (type) {
	case AMDGPU_THERMAL_IRQ_LOW_TO_HIGH:
		switch (state) {
		case AMDGPU_IRQ_STATE_DISABLE:
			cg_thermal_int = RREG32_SMC(ixCG_THERMAL_INT_CTRL);
			cg_thermal_int &= ~CG_THERMAL_INT_CTRL__THERM_INTH_MASK_MASK;
			WREG32_SMC(ixCG_THERMAL_INT_CTRL, cg_thermal_int);
			break;
		case AMDGPU_IRQ_STATE_ENABLE:
			cg_thermal_int = RREG32_SMC(ixCG_THERMAL_INT_CTRL);
			cg_thermal_int |= CG_THERMAL_INT_CTRL__THERM_INTH_MASK_MASK;
			WREG32_SMC(ixCG_THERMAL_INT_CTRL, cg_thermal_int);
			break;
		default:
			break;
		}
		break;

	case AMDGPU_THERMAL_IRQ_HIGH_TO_LOW:
		switch (state) {
		case AMDGPU_IRQ_STATE_DISABLE:
			cg_thermal_int = RREG32_SMC(ixCG_THERMAL_INT_CTRL);
			cg_thermal_int &= ~CG_THERMAL_INT_CTRL__THERM_INTL_MASK_MASK;
			WREG32_SMC(ixCG_THERMAL_INT_CTRL, cg_thermal_int);
			break;
		case AMDGPU_IRQ_STATE_ENABLE:
			cg_thermal_int = RREG32_SMC(ixCG_THERMAL_INT_CTRL);
			cg_thermal_int |= CG_THERMAL_INT_CTRL__THERM_INTL_MASK_MASK;
			WREG32_SMC(ixCG_THERMAL_INT_CTRL, cg_thermal_int);
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

static int kv_dpm_process_interrupt(struct amdgpu_device *adev,
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

static int kv_dpm_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	return 0;
}

static int kv_dpm_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	return 0;
}

static inline bool kv_are_power_levels_equal(const struct kv_pl *kv_cpl1,
						const struct kv_pl *kv_cpl2)
{
	return ((kv_cpl1->sclk == kv_cpl2->sclk) &&
		  (kv_cpl1->vddc_index == kv_cpl2->vddc_index) &&
		  (kv_cpl1->ds_divider_index == kv_cpl2->ds_divider_index) &&
		  (kv_cpl1->force_nbp_state == kv_cpl2->force_nbp_state));
}

static int kv_check_state_equal(void *handle,
				void *current_ps,
				void *request_ps,
				bool *equal)
{
	struct kv_ps *kv_cps;
	struct kv_ps *kv_rps;
	int i;
	struct amdgpu_ps *cps = (struct amdgpu_ps *)current_ps;
	struct amdgpu_ps *rps = (struct amdgpu_ps *)request_ps;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev == NULL || cps == NULL || rps == NULL || equal == NULL)
		return -EINVAL;

	kv_cps = kv_get_ps(cps);
	kv_rps = kv_get_ps(rps);

	if (kv_cps == NULL) {
		*equal = false;
		return 0;
	}

	if (kv_cps->num_levels != kv_rps->num_levels) {
		*equal = false;
		return 0;
	}

	for (i = 0; i < kv_cps->num_levels; i++) {
		if (!kv_are_power_levels_equal(&(kv_cps->levels[i]),
					&(kv_rps->levels[i]))) {
			*equal = false;
			return 0;
		}
	}

	/* If all performance levels are the same try to use the UVD clocks to break the tie.*/
	*equal = ((cps->vclk == rps->vclk) && (cps->dclk == rps->dclk));
	*equal &= ((cps->evclk == rps->evclk) && (cps->ecclk == rps->ecclk));

	return 0;
}

static int kv_dpm_read_sensor(void *handle, int idx,
			      void *value, int *size)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct kv_power_info *pi = kv_get_pi(adev);
	uint32_t sclk;
	u32 pl_index =
		(RREG32_SMC(ixTARGET_AND_CURRENT_PROFILE_INDEX) &
		TARGET_AND_CURRENT_PROFILE_INDEX__CURR_SCLK_INDEX_MASK) >>
		TARGET_AND_CURRENT_PROFILE_INDEX__CURR_SCLK_INDEX__SHIFT;

	/* size must be at least 4 bytes for all sensors */
	if (*size < 4)
		return -EINVAL;

	switch (idx) {
	case AMDGPU_PP_SENSOR_GFX_SCLK:
		if (pl_index < SMU__NUM_SCLK_DPM_STATE) {
			sclk = be32_to_cpu(
				pi->graphics_level[pl_index].SclkFrequency);
			*((uint32_t *)value) = sclk;
			*size = 4;
			return 0;
		}
		return -EINVAL;
	case AMDGPU_PP_SENSOR_GPU_TEMP:
		*((uint32_t *)value) = kv_dpm_get_temp(adev);
		*size = 4;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int kv_set_powergating_by_smu(void *handle,
				uint32_t block_type, bool gate)
{
	switch (block_type) {
	case AMD_IP_BLOCK_TYPE_UVD:
		kv_dpm_powergate_uvd(handle, gate);
		break;
	case AMD_IP_BLOCK_TYPE_VCE:
		kv_dpm_powergate_vce(handle, gate);
		break;
	default:
		break;
	}
	return 0;
}

static const struct amd_ip_funcs kv_dpm_ip_funcs = {
	.name = "kv_dpm",
	.early_init = kv_dpm_early_init,
	.late_init = kv_dpm_late_init,
	.sw_init = kv_dpm_sw_init,
	.sw_fini = kv_dpm_sw_fini,
	.hw_init = kv_dpm_hw_init,
	.hw_fini = kv_dpm_hw_fini,
	.suspend = kv_dpm_suspend,
	.resume = kv_dpm_resume,
	.is_idle = kv_dpm_is_idle,
	.wait_for_idle = kv_dpm_wait_for_idle,
	.soft_reset = kv_dpm_soft_reset,
	.set_clockgating_state = kv_dpm_set_clockgating_state,
	.set_powergating_state = kv_dpm_set_powergating_state,
	.dump_ip_state = NULL,
	.print_ip_state = NULL,
};

const struct amdgpu_ip_block_version kv_smu_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_SMC,
	.major = 1,
	.minor = 0,
	.rev = 0,
	.funcs = &kv_dpm_ip_funcs,
};

static const struct amd_pm_funcs kv_dpm_funcs = {
	.pre_set_power_state = &kv_dpm_pre_set_power_state,
	.set_power_state = &kv_dpm_set_power_state,
	.post_set_power_state = &kv_dpm_post_set_power_state,
	.display_configuration_changed = &kv_dpm_display_configuration_changed,
	.get_sclk = &kv_dpm_get_sclk,
	.get_mclk = &kv_dpm_get_mclk,
	.print_power_state = &kv_dpm_print_power_state,
	.debugfs_print_current_performance_level = &kv_dpm_debugfs_print_current_performance_level,
	.force_performance_level = &kv_dpm_force_performance_level,
	.set_powergating_by_smu = kv_set_powergating_by_smu,
	.enable_bapm = &kv_dpm_enable_bapm,
	.get_vce_clock_state = amdgpu_get_vce_clock_state,
	.check_state_equal = kv_check_state_equal,
	.read_sensor = &kv_dpm_read_sensor,
	.pm_compute_clocks = amdgpu_legacy_dpm_compute_clocks,
};

static const struct amdgpu_irq_src_funcs kv_dpm_irq_funcs = {
	.set = kv_dpm_set_interrupt_state,
	.process = kv_dpm_process_interrupt,
};

static void kv_dpm_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->pm.dpm.thermal.irq.num_types = AMDGPU_THERMAL_IRQ_LAST;
	adev->pm.dpm.thermal.irq.funcs = &kv_dpm_irq_funcs;
}
