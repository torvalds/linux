// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2021-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include "mali_kbase_pbha.h"

#include <device/mali_kbase_device.h>
#include <mali_kbase.h>
#define DTB_SET_SIZE 2

static bool read_setting_valid(unsigned int id, unsigned int read_setting)
{
	switch (id) {
	/* Valid ID - fall through all */
	case SYSC_ALLOC_ID_R_OTHER:
	case SYSC_ALLOC_ID_R_CSF:
	case SYSC_ALLOC_ID_R_MMU:
	case SYSC_ALLOC_ID_R_TILER_VERT:
	case SYSC_ALLOC_ID_R_TILER_PTR:
	case SYSC_ALLOC_ID_R_TILER_INDEX:
	case SYSC_ALLOC_ID_R_TILER_OTHER:
	case SYSC_ALLOC_ID_R_IC:
	case SYSC_ALLOC_ID_R_ATTR:
	case SYSC_ALLOC_ID_R_SCM:
	case SYSC_ALLOC_ID_R_FSDC:
	case SYSC_ALLOC_ID_R_VL:
	case SYSC_ALLOC_ID_R_PLR:
	case SYSC_ALLOC_ID_R_TEX:
	case SYSC_ALLOC_ID_R_LSC:
		switch (read_setting) {
		/* Valid setting value - fall through all */
		case SYSC_ALLOC_L2_ALLOC:
		case SYSC_ALLOC_NEVER_ALLOC:
		case SYSC_ALLOC_ALWAYS_ALLOC:
		case SYSC_ALLOC_PTL_ALLOC:
		case SYSC_ALLOC_L2_PTL_ALLOC:
			return true;
		default:
			return false;
		}
	default:
		return false;
	}

	/* Unreachable */
	return false;
}

static bool write_setting_valid(unsigned int id, unsigned int write_setting)
{
	switch (id) {
	/* Valid ID - fall through all */
	case SYSC_ALLOC_ID_W_OTHER:
	case SYSC_ALLOC_ID_W_CSF:
	case SYSC_ALLOC_ID_W_PCB:
	case SYSC_ALLOC_ID_W_TILER_PTR:
	case SYSC_ALLOC_ID_W_TILER_VERT_PLIST:
	case SYSC_ALLOC_ID_W_TILER_OTHER:
	case SYSC_ALLOC_ID_W_L2_EVICT:
	case SYSC_ALLOC_ID_W_L2_FLUSH:
	case SYSC_ALLOC_ID_W_TIB_COLOR:
	case SYSC_ALLOC_ID_W_TIB_COLOR_AFBCH:
	case SYSC_ALLOC_ID_W_TIB_COLOR_AFBCB:
	case SYSC_ALLOC_ID_W_TIB_CRC:
	case SYSC_ALLOC_ID_W_TIB_DS:
	case SYSC_ALLOC_ID_W_TIB_DS_AFBCH:
	case SYSC_ALLOC_ID_W_TIB_DS_AFBCB:
	case SYSC_ALLOC_ID_W_LSC:
		switch (write_setting) {
		/* Valid setting value - fall through all */
		case SYSC_ALLOC_L2_ALLOC:
		case SYSC_ALLOC_NEVER_ALLOC:
		case SYSC_ALLOC_ALWAYS_ALLOC:
		case SYSC_ALLOC_PTL_ALLOC:
		case SYSC_ALLOC_L2_PTL_ALLOC:
			return true;
		default:
			return false;
		}
	default:
		return false;
	}

	/* Unreachable */
	return false;
}

/* Private structure to be returned as setting validity status */
struct settings_status {
	/* specifies whether id and either one of settings is valid */
	bool overall;
	/* specifies whether read setting is valid */
	bool read;
	/* specifies whether write setting is valid*/
	bool write;
};

static struct settings_status settings_valid(unsigned int id, unsigned int read_setting,
					     unsigned int write_setting)
{
	struct settings_status valid = { .overall = (id < SYSC_ALLOC_COUNT * sizeof(u32)) };

	if (valid.overall) {
		valid.read = read_setting_valid(id, read_setting);
		valid.write = write_setting_valid(id, write_setting);
		valid.overall = valid.read || valid.write;
	}

	return valid;
}

bool kbasep_pbha_supported(struct kbase_device *kbdev)
{
	const u32 arch_maj_rev =
		ARCH_MAJOR_REV_REG(kbdev->gpu_props.props.raw_props.gpu_id);

	return (arch_maj_rev >= GPU_ID2_ARCH_MAJOR_REV_MAKE(11, 3));
}

int kbase_pbha_record_settings(struct kbase_device *kbdev, bool runtime,
			       unsigned int id, unsigned int read_setting,
			       unsigned int write_setting)
{
	struct settings_status const valid = settings_valid(id, read_setting, write_setting);

	if (valid.overall) {
		unsigned int const sysc_alloc_num = id / sizeof(u32);
		u32 modified_reg;

		if (runtime) {
			int i;

			kbase_pm_context_active(kbdev);
			/* Ensure host copy of SYSC_ALLOC is up to date */
			for (i = 0; i < SYSC_ALLOC_COUNT; i++)
				kbdev->sysc_alloc[i] = kbase_reg_read(
					kbdev, GPU_CONTROL_REG(SYSC_ALLOC(i)));
			kbase_pm_context_idle(kbdev);
		}

		modified_reg = kbdev->sysc_alloc[sysc_alloc_num];

		switch (id % sizeof(u32)) {
		case 0:
			modified_reg = valid.read ? SYSC_ALLOC_R_SYSC_ALLOC0_SET(modified_reg,
										 read_setting) :
						    modified_reg;
			modified_reg = valid.write ? SYSC_ALLOC_W_SYSC_ALLOC0_SET(modified_reg,
										  write_setting) :
						     modified_reg;
			break;
		case 1:
			modified_reg = valid.read ? SYSC_ALLOC_R_SYSC_ALLOC1_SET(modified_reg,
										 read_setting) :
						    modified_reg;
			modified_reg = valid.write ? SYSC_ALLOC_W_SYSC_ALLOC1_SET(modified_reg,
										  write_setting) :
						     modified_reg;
			break;
		case 2:
			modified_reg = valid.read ? SYSC_ALLOC_R_SYSC_ALLOC2_SET(modified_reg,
										 read_setting) :
						    modified_reg;
			modified_reg = valid.write ? SYSC_ALLOC_W_SYSC_ALLOC2_SET(modified_reg,
										  write_setting) :
						     modified_reg;
			break;
		case 3:
			modified_reg = valid.read ? SYSC_ALLOC_R_SYSC_ALLOC3_SET(modified_reg,
										 read_setting) :
						    modified_reg;
			modified_reg = valid.write ? SYSC_ALLOC_W_SYSC_ALLOC3_SET(modified_reg,
										  write_setting) :
						     modified_reg;
			break;
		}

		kbdev->sysc_alloc[sysc_alloc_num] = modified_reg;
	}

	return valid.overall ? 0 : -EINVAL;
}

void kbase_pbha_write_settings(struct kbase_device *kbdev)
{
	if (kbasep_pbha_supported(kbdev)) {
		int i;

		for (i = 0; i < SYSC_ALLOC_COUNT; ++i)
			kbase_reg_write(kbdev, GPU_CONTROL_REG(SYSC_ALLOC(i)),
					kbdev->sysc_alloc[i]);
	}
}

static int kbase_pbha_read_int_id_override_property(struct kbase_device *kbdev,
						    const struct device_node *pbha_node)
{
	u32 dtb_data[SYSC_ALLOC_COUNT * sizeof(u32) * DTB_SET_SIZE];
	int sz, i;
	bool valid = true;

	sz = of_property_count_elems_of_size(pbha_node, "int_id_override",
					     sizeof(u32));
	if (sz <= 0 || (sz % DTB_SET_SIZE != 0)) {
		dev_err(kbdev->dev, "Bad DTB format: pbha.int_id_override\n");
		return -EINVAL;
	}
	if (of_property_read_u32_array(pbha_node, "int_id_override", dtb_data,
				       sz) != 0) {
		dev_err(kbdev->dev,
			"Failed to read DTB pbha.int_id_override\n");
		return -EINVAL;
	}

	for (i = 0; valid && i < sz; i = i + DTB_SET_SIZE) {
		unsigned int rdset =
			SYSC_ALLOC_R_SYSC_ALLOC0_GET(dtb_data[i + 1]);
		unsigned int wrset =
			SYSC_ALLOC_W_SYSC_ALLOC0_GET(dtb_data[i + 1]);
		valid = valid &&
			(kbase_pbha_record_settings(kbdev, false, dtb_data[i],
						    rdset, wrset) == 0);
		if (valid)
			dev_info(kbdev->dev,
				 "pbha.int_id_override 0x%x r0x%x w0x%x\n",
				 dtb_data[i], rdset, wrset);
	}
	if (i != sz || (!valid)) {
		dev_err(kbdev->dev,
			"Failed recording DTB data (pbha.int_id_override)\n");
		return -EINVAL;
	}
	return 0;
}

#if MALI_USE_CSF
static int kbase_pbha_read_propagate_bits_property(struct kbase_device *kbdev,
						   const struct device_node *pbha_node)
{
	u32 bits;
	int err;

	if (!kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_PBHA_HWU))
		return 0;

	err = of_property_read_u32(pbha_node, "propagate_bits", &bits);

	if (err < 0) {
		if (err != -EINVAL) {
			dev_err(kbdev->dev,
				"DTB value for propagate_bits is improperly formed (err=%d)\n",
				err);
			return err;
		}
	}

	if (bits > (L2_CONFIG_PBHA_HWU_MASK >> L2_CONFIG_PBHA_HWU_SHIFT)) {
		dev_err(kbdev->dev, "Bad DTB value for propagate_bits: 0x%x\n", bits);
		return -EINVAL;
	}

	kbdev->pbha_propagate_bits = bits;
	return 0;
}
#endif

int kbase_pbha_read_dtb(struct kbase_device *kbdev)
{
	const struct device_node *pbha_node;
	int err;

	if (!kbasep_pbha_supported(kbdev))
		return 0;

	pbha_node = of_get_child_by_name(kbdev->dev->of_node, "pbha");
	if (!pbha_node)
		return 0;

	err = kbase_pbha_read_int_id_override_property(kbdev, pbha_node);

#if MALI_USE_CSF
	if (err < 0)
		return err;

	err = kbase_pbha_read_propagate_bits_property(kbdev, pbha_node);
#endif

	return err;
}
