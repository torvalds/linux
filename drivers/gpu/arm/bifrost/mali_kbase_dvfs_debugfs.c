// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2020-2021 ARM Limited. All rights reserved.
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

#include "mali_kbase_dvfs_debugfs.h"
#include <mali_kbase.h>
#include <linux/seq_file.h>

#if IS_ENABLED(CONFIG_DEBUG_FS)

/**
 * kbasep_dvfs_utilization_debugfs_show() - Print the DVFS utilization info
 *
 * @file: The seq_file for printing to
 * @data: The debugfs dentry private data, a pointer to kbase_context
 *
 * Return: Negative error code or 0 on success.
 */
static int kbasep_dvfs_utilization_debugfs_show(struct seq_file *file, void *data)
{
	struct kbase_device *kbdev = file->private;

#if MALI_USE_CSF
	seq_printf(file, "busy_time: %u idle_time: %u protm_time: %u\n",
		   kbdev->pm.backend.metrics.values.time_busy,
		   kbdev->pm.backend.metrics.values.time_idle,
		   kbdev->pm.backend.metrics.values.time_in_protm);
#else
	seq_printf(file, "busy_time: %u idle_time: %u\n",
		   kbdev->pm.backend.metrics.values.time_busy,
		   kbdev->pm.backend.metrics.values.time_idle);
#endif

	return 0;
}

static int kbasep_dvfs_utilization_debugfs_open(struct inode *in,
						struct file *file)
{
	return single_open(file, kbasep_dvfs_utilization_debugfs_show,
			   in->i_private);
}

static const struct file_operations kbasep_dvfs_utilization_debugfs_fops = {
	.open = kbasep_dvfs_utilization_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void kbase_dvfs_status_debugfs_init(struct kbase_device *kbdev)
{
	struct dentry *file;
#if (KERNEL_VERSION(4, 7, 0) <= LINUX_VERSION_CODE)
	const mode_t mode = 0444;
#else
	const mode_t mode = 0400;
#endif

	if (WARN_ON(!kbdev || IS_ERR_OR_NULL(kbdev->mali_debugfs_directory)))
		return;

	file = debugfs_create_file("dvfs_utilization", mode,
				   kbdev->mali_debugfs_directory, kbdev,
				   &kbasep_dvfs_utilization_debugfs_fops);

	if (IS_ERR_OR_NULL(file)) {
		dev_warn(kbdev->dev,
			 "Unable to create dvfs debugfs entry");
	}
}

#else
/*
 * Stub functions for when debugfs is disabled
 */
void kbase_dvfs_status_debugfs_init(struct kbase_device *kbdev)
{
}

#endif /* CONFIG_DEBUG_FS */
