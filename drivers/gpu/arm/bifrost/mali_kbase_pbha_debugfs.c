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

#include "mali_kbase_pbha_debugfs.h"
#include "mali_kbase_pbha.h"
#include <device/mali_kbase_device.h>
#include <mali_kbase_reset_gpu.h>
#include <mali_kbase.h>

#if MALI_USE_CSF
#include "backend/gpu/mali_kbase_pm_internal.h"
#endif

static int int_id_overrides_show(struct seq_file *sfile, void *data)
{
	struct kbase_device *kbdev = sfile->private;
	int i;

	kbase_pm_context_active(kbdev);

	/* Minimal header for readability */
	seq_puts(sfile, "// R   W\n");
	for (i = 0; i < SYSC_ALLOC_COUNT; ++i) {
		int j;
		u32 reg = kbase_reg_read(kbdev, GPU_CONTROL_REG(SYSC_ALLOC(i)));

		for (j = 0; j < sizeof(u32); ++j) {
			u8 r_val;
			u8 w_val;

			switch (j) {
			case 0:
				r_val = SYSC_ALLOC_R_SYSC_ALLOC0_GET(reg);
				w_val = SYSC_ALLOC_W_SYSC_ALLOC0_GET(reg);
				break;
			case 1:
				r_val = SYSC_ALLOC_R_SYSC_ALLOC1_GET(reg);
				w_val = SYSC_ALLOC_W_SYSC_ALLOC1_GET(reg);
				break;
			case 2:
				r_val = SYSC_ALLOC_R_SYSC_ALLOC2_GET(reg);
				w_val = SYSC_ALLOC_W_SYSC_ALLOC2_GET(reg);
				break;
			case 3:
				r_val = SYSC_ALLOC_R_SYSC_ALLOC3_GET(reg);
				w_val = SYSC_ALLOC_W_SYSC_ALLOC3_GET(reg);
				break;
			}
			seq_printf(sfile, "%2zu 0x%x 0x%x\n",
				   (i * sizeof(u32)) + j, r_val, w_val);
		}
	}
	kbase_pm_context_idle(kbdev);

	return 0;
}

static ssize_t int_id_overrides_write(struct file *file,
				      const char __user *ubuf, size_t count,
				      loff_t *ppos)
{
	struct seq_file *sfile = file->private_data;
	struct kbase_device *kbdev = sfile->private;
	char raw_str[128];
	unsigned int id;
	unsigned int r_val;
	unsigned int w_val;

	if (count >= sizeof(raw_str))
		return -E2BIG;
	if (copy_from_user(raw_str, ubuf, count))
		return -EINVAL;
	raw_str[count] = '\0';

	if (sscanf(raw_str, "%u %x %x", &id, &r_val, &w_val) != 3)
		return -EINVAL;

	if (kbase_pbha_record_settings(kbdev, true, id, r_val, w_val))
		return -EINVAL;

	/* This is a debugfs config write, so reset GPU such that changes take effect ASAP */
	kbase_pm_context_active(kbdev);
	if (kbase_prepare_to_reset_gpu(kbdev, RESET_FLAGS_NONE))
		kbase_reset_gpu(kbdev);
	kbase_pm_context_idle(kbdev);

	return count;
}

static int int_id_overrides_open(struct inode *in, struct file *file)
{
	return single_open(file, int_id_overrides_show, in->i_private);
}

#if MALI_USE_CSF
/**
 * propagate_bits_show - Read PBHA bits from L2_CONFIG out to debugfs.
 *
 * @sfile: The debugfs entry.
 * @data: Data associated with the entry.
 *
 * Return: 0 in all cases.
 */
static int propagate_bits_show(struct seq_file *sfile, void *data)
{
	struct kbase_device *kbdev = sfile->private;
	u32 l2_config_val;

	kbase_csf_scheduler_pm_active(kbdev);
	kbase_pm_wait_for_l2_powered(kbdev);
	l2_config_val = L2_CONFIG_PBHA_HWU_GET(kbase_reg_read(kbdev, GPU_CONTROL_REG(L2_CONFIG)));
	kbase_csf_scheduler_pm_idle(kbdev);

	seq_printf(sfile, "PBHA Propagate Bits: 0x%x\n", l2_config_val);
	return 0;
}

static int propagate_bits_open(struct inode *in, struct file *file)
{
	return single_open(file, propagate_bits_show, in->i_private);
}

/**
 * propagate_bits_write - Write input value from debugfs to PBHA bits of L2_CONFIG register.
 *
 * @file:     Pointer to file struct of debugfs node.
 * @ubuf:     Pointer to user buffer with value to be written.
 * @count:    Size of user buffer.
 * @ppos:     Not used.
 *
 * Return: Size of buffer passed in when successful, but error code E2BIG/EINVAL otherwise.
 */
static ssize_t propagate_bits_write(struct file *file, const char __user *ubuf, size_t count,
				    loff_t *ppos)
{
	struct seq_file *sfile = file->private_data;
	struct kbase_device *kbdev = sfile->private;
	/* 32 characters should be enough for the input string in any base */
	char raw_str[32];
	unsigned long propagate_bits;

	if (count >= sizeof(raw_str))
		return -E2BIG;
	if (copy_from_user(raw_str, ubuf, count))
		return -EINVAL;
	raw_str[count] = '\0';
	if (kstrtoul(raw_str, 0, &propagate_bits))
		return -EINVAL;

	/* Check propagate_bits input argument does not
	 * exceed the maximum size of the propagate_bits mask.
	 */
	if (propagate_bits > (L2_CONFIG_PBHA_HWU_MASK >> L2_CONFIG_PBHA_HWU_SHIFT))
		return -EINVAL;
	/* Cast to u8 is safe as check is done already to ensure size is within
	 * correct limits.
	 */
	kbdev->pbha_propagate_bits = (u8)propagate_bits;

	/* GPU Reset will set new values in L2 config */
	if (kbase_prepare_to_reset_gpu(kbdev, RESET_FLAGS_NONE)) {
		kbase_reset_gpu(kbdev);
		kbase_reset_gpu_wait(kbdev);
	}

	return count;
}

static const struct file_operations pbha_propagate_bits_fops = {
	.owner = THIS_MODULE,
	.open = propagate_bits_open,
	.read = seq_read,
	.write = propagate_bits_write,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif /* MALI_USE_CSF */

static const struct file_operations pbha_int_id_overrides_fops = {
	.owner = THIS_MODULE,
	.open = int_id_overrides_open,
	.read = seq_read,
	.write = int_id_overrides_write,
	.llseek = seq_lseek,
	.release = single_release,
};

void kbase_pbha_debugfs_init(struct kbase_device *kbdev)
{
	if (kbasep_pbha_supported(kbdev)) {
		const mode_t mode = 0644;
		struct dentry *debugfs_pbha_dir = debugfs_create_dir(
			"pbha", kbdev->mali_debugfs_directory);

		if (IS_ERR_OR_NULL(debugfs_pbha_dir)) {
			dev_err(kbdev->dev,
				"Couldn't create mali debugfs page-based hardware attributes directory\n");
			return;
		}

		debugfs_create_file("int_id_overrides", mode, debugfs_pbha_dir,
				    kbdev, &pbha_int_id_overrides_fops);
#if MALI_USE_CSF
		if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_PBHA_HWU))
			debugfs_create_file("propagate_bits", mode, debugfs_pbha_dir, kbdev,
					    &pbha_propagate_bits_fops);
#endif /* MALI_USE_CSF */
	}
}
