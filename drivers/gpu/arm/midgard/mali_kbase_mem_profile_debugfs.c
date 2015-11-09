/*
 *
 * (C) COPYRIGHT 2012-2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#include <mali_kbase_gpu_memory_debugfs.h>

#ifdef CONFIG_DEBUG_FS

/** Show callback for the @c mem_profile debugfs file.
 *
 * This function is called to get the contents of the @c mem_profile debugfs
 * file. This is a report of current memory usage and distribution in userspace.
 *
 * @param sfile The debugfs entry
 * @param data Data associated with the entry
 *
 * @return 0 if it successfully prints data in debugfs entry file, non-zero otherwise
 */
static int kbasep_mem_profile_seq_show(struct seq_file *sfile, void *data)
{
	struct kbase_context *kctx = sfile->private;
	int err = 0;

	mutex_lock(&kctx->mem_profile_lock);

	err = seq_write(sfile, kctx->mem_profile_data, kctx->mem_profile_size);

	if (!err)
		err = seq_putc(sfile, '\n');

	mutex_unlock(&kctx->mem_profile_lock);

	return err;
}

/*
 *  File operations related to debugfs entry for mem_profile
 */
static int kbasep_mem_profile_debugfs_open(struct inode *in, struct file *file)
{
	return single_open(file, kbasep_mem_profile_seq_show, in->i_private);
}

static const struct file_operations kbasep_mem_profile_debugfs_fops = {
	.open = kbasep_mem_profile_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

int kbasep_mem_profile_debugfs_insert(struct kbase_context *kctx, char *data,
					size_t size)
{
	int err = 0;

	mutex_lock(&kctx->mem_profile_lock);

	dev_dbg(kctx->kbdev->dev, "initialised: %d",
				kctx->mem_profile_initialized);

	if (!kctx->mem_profile_initialized) {
		if (!debugfs_create_file("mem_profile", S_IRUGO,
					kctx->kctx_dentry, kctx,
					&kbasep_mem_profile_debugfs_fops)) {
			err = -EAGAIN;
		} else {
			kctx->mem_profile_initialized = true;
		}
	}

	if (kctx->mem_profile_initialized) {
		kfree(kctx->mem_profile_data);
		kctx->mem_profile_data = data;
		kctx->mem_profile_size = size;
	}

	dev_dbg(kctx->kbdev->dev, "returning: %d, initialised: %d",
				err, kctx->mem_profile_initialized);

	mutex_unlock(&kctx->mem_profile_lock);

	return err;
}

void kbasep_mem_profile_debugfs_remove(struct kbase_context *kctx)
{
	mutex_lock(&kctx->mem_profile_lock);

	dev_dbg(kctx->kbdev->dev, "initialised: %d",
				kctx->mem_profile_initialized);

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
