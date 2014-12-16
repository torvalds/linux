/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
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

/* mam_profile file name max length 22 based on format <int>_<int>\0 */
#define KBASEP_DEBUGFS_FNAME_SIZE_MAX (10+1+10+1)

void kbasep_mem_profile_debugfs_insert(struct kbase_context *kctx, char *data,
		size_t size)
{
	spin_lock(&kctx->mem_profile_lock);
	kfree(kctx->mem_profile_data);
	kctx->mem_profile_data = data;
	kctx->mem_profile_size = size;
	spin_unlock(&kctx->mem_profile_lock);
}

/** Show callback for the @c mem_profile debugfs file.
 *
 * This function is called to get the contents of the @c mem_profile debugfs
 * file. This is a report of current memory usage and distribution in userspace.
 *
 * @param sfile The debugfs entry
 * @param data Data associated with the entry
 *
 * @return 0 if successfully prints data in debugfs entry file
 *         -1 if it encountered an error
 */
static int kbasep_mem_profile_seq_show(struct seq_file *sfile, void *data)
{
	struct kbase_context *kctx = sfile->private;

	KBASE_DEBUG_ASSERT(kctx != NULL);

	spin_lock(&kctx->mem_profile_lock);
	seq_write(sfile, kctx->mem_profile_data, kctx->mem_profile_size);
	seq_putc(sfile, '\n');
	spin_unlock(&kctx->mem_profile_lock);

	return 0;
}

/*
 *  File operations related to debugfs entry for mem_profile
 */
STATIC int kbasep_mem_profile_debugfs_open(struct inode *in, struct file *file)
{
	return single_open(file, kbasep_mem_profile_seq_show, in->i_private);
}

static const struct file_operations kbasep_mem_profile_debugfs_fops = {
	.open = kbasep_mem_profile_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

mali_error kbasep_mem_profile_debugfs_add(struct kbase_context *kctx)
{
	char name[KBASEP_DEBUGFS_FNAME_SIZE_MAX];

	KBASE_DEBUG_ASSERT(kctx != NULL);

	spin_lock_init(&kctx->mem_profile_lock);

	scnprintf(name, KBASEP_DEBUGFS_FNAME_SIZE_MAX, "%d_%d", kctx->pid,
			kctx->id);

	kctx->mem_dentry = debugfs_create_file(name, S_IRUGO,
			kctx->kbdev->memory_profile_directory,
			kctx, &kbasep_mem_profile_debugfs_fops);
	if (IS_ERR(kctx->mem_dentry))
		goto error_out;

	return MALI_ERROR_NONE;

error_out:
	return MALI_ERROR_FUNCTION_FAILED;
}

void kbasep_mem_profile_debugfs_remove(struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kctx != NULL);

	spin_lock(&kctx->mem_profile_lock);
	kfree(kctx->mem_profile_data);
	kctx->mem_profile_data = NULL;
	spin_unlock(&kctx->mem_profile_lock);

	if (IS_ERR(kctx->mem_dentry))
		return;
	debugfs_remove(kctx->mem_dentry);
}

#else /* CONFIG_DEBUG_FS */

/**
 * @brief Stub function for when debugfs is disabled
 */
mali_error kbasep_mem_profile_debugfs_add(struct kbase_context *ctx)
{
	return MALI_ERROR_NONE;
}

/**
 * @brief Stub function for when debugfs is disabled
 */
void kbasep_mem_profile_debugfs_remove(struct kbase_context *ctx)
{
}

/**
 * @brief Stub function for when debugfs is disabled
 */
void kbasep_mem_profile_debugfs_insert(struct kbase_context *kctx, char *data,
		size_t size)
{
	kfree(data);
}
#endif /* CONFIG_DEBUG_FS */
