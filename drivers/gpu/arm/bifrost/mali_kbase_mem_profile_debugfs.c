// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2012-2017, 2019-2021 ARM Limited. All rights reserved.
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

#include <mali_kbase.h>

#if IS_ENABLED(CONFIG_DEBUG_FS)

/**
 * Show callback for the @c mem_profile debugfs file.
 *
 * This function is called to get the contents of the @c mem_profile debugfs
 * file. This is a report of current memory usage and distribution in userspace.
 *
 * @sfile: The debugfs entry
 * @data:  Data associated with the entry
 *
 * Return: 0 if it successfully prints data in debugfs entry file, non-zero
 * otherwise
 */
static int kbasep_mem_profile_seq_show(struct seq_file *sfile, void *data)
{
	struct kbase_context *kctx = sfile->private;

	mutex_lock(&kctx->mem_profile_lock);

	seq_write(sfile, kctx->mem_profile_data, kctx->mem_profile_size);

	seq_putc(sfile, '\n');

	mutex_unlock(&kctx->mem_profile_lock);

	return 0;
}

/*
 *  File operations related to debugfs entry for mem_profile
 */
static int kbasep_mem_profile_debugfs_open(struct inode *in, struct file *file)
{
	return single_open(file, kbasep_mem_profile_seq_show, in->i_private);
}

static const struct file_operations kbasep_mem_profile_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = kbasep_mem_profile_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

int kbasep_mem_profile_debugfs_insert(struct kbase_context *kctx, char *data,
					size_t size)
{
#if (KERNEL_VERSION(4, 7, 0) <= LINUX_VERSION_CODE)
	const mode_t mode = 0444;
#else
	const mode_t mode = 0400;
#endif
	int err = 0;

	mutex_lock(&kctx->mem_profile_lock);

	dev_dbg(kctx->kbdev->dev, "initialised: %d",
		kbase_ctx_flag(kctx, KCTX_MEM_PROFILE_INITIALIZED));

	if (!kbase_ctx_flag(kctx, KCTX_MEM_PROFILE_INITIALIZED)) {
		if (IS_ERR_OR_NULL(kctx->kctx_dentry)) {
			err  = -ENOMEM;
		} else if (!debugfs_create_file("mem_profile", mode,
					kctx->kctx_dentry, kctx,
					&kbasep_mem_profile_debugfs_fops)) {
			err = -EAGAIN;
		} else {
			kbase_ctx_flag_set(kctx,
					   KCTX_MEM_PROFILE_INITIALIZED);
		}
	}

	if (kbase_ctx_flag(kctx, KCTX_MEM_PROFILE_INITIALIZED)) {
		kfree(kctx->mem_profile_data);
		kctx->mem_profile_data = data;
		kctx->mem_profile_size = size;
	} else {
		kfree(data);
	}

	dev_dbg(kctx->kbdev->dev, "returning: %d, initialised: %d",
		err, kbase_ctx_flag(kctx, KCTX_MEM_PROFILE_INITIALIZED));

	mutex_unlock(&kctx->mem_profile_lock);

	return err;
}

void kbasep_mem_profile_debugfs_remove(struct kbase_context *kctx)
{
	mutex_lock(&kctx->mem_profile_lock);

	dev_dbg(kctx->kbdev->dev, "initialised: %d",
				kbase_ctx_flag(kctx, KCTX_MEM_PROFILE_INITIALIZED));

	kfree(kctx->mem_profile_data);
	kctx->mem_profile_data = NULL;
	kctx->mem_profile_size = 0;

	mutex_unlock(&kctx->mem_profile_lock);
}

#else /* CONFIG_DEBUG_FS */

int kbasep_mem_profile_debugfs_insert(struct kbase_context *kctx, char *data,
					size_t size)
{
	kfree(data);
	return 0;
}
#endif /* CONFIG_DEBUG_FS */
