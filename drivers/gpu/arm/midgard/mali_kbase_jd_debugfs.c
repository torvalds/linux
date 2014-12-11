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



#include <linux/seq_file.h>

#include <mali_kbase_jd_debugfs.h>

#ifdef CONFIG_DEBUG_FS

/**
 * @brief Show callback for the @c JD atoms debugfs file.
 *
 * This function is called to get the contents of the @c JD atoms debugfs file.
 * This is a report of all atoms managed by kbase_jd_context::atoms .
 *
 * @param sfile The debugfs entry
 * @param data Data associated with the entry
 *
 * @return 0 if successfully prints data in debugfs entry file, failure
 * otherwise
 */
static int kbasep_jd_debugfs_atoms_show(struct seq_file *sfile, void *data)
{
	struct kbase_context *kctx = sfile->private;
	struct kbase_jd_atom *atoms;
	unsigned long irq_flags;
	int i;

	KBASE_DEBUG_ASSERT(kctx != NULL);

	/* Print table heading */
	seq_puts(sfile, "atom id,core reqs,status,coreref status,predeps,start time,time on gpu\n");

	atoms = kctx->jctx.atoms;
	/* General atom states */
	mutex_lock(&kctx->jctx.lock);
	/* JS-related states */
	spin_lock_irqsave(&kctx->kbdev->js_data.runpool_irq.lock, irq_flags);
	for (i = 0; i != BASE_JD_ATOM_COUNT; ++i) {
		struct kbase_jd_atom *atom = &atoms[i];
		s64 start_timestamp = 0;

		if (atom->status == KBASE_JD_ATOM_STATE_UNUSED)
			continue;

		/* start_timestamp is cleared as soon as the atom leaves UNUSED state
		 * and set before a job is submitted to the h/w, a non-zero value means
		 * it is valid */
		if (ktime_to_ns(atom->start_timestamp))
			start_timestamp = ktime_to_ns(
					ktime_sub(ktime_get(), atom->start_timestamp));

		seq_printf(sfile,
				"%i,%u,%u,%u,%u %u,%lli,%llu\n",
				i, atom->core_req, atom->status, atom->coreref_state,
				atom->dep[0].atom ? atom->dep[0].atom - atoms : 0,
				atom->dep[1].atom ? atom->dep[1].atom - atoms : 0,
				(signed long long)start_timestamp,
				(unsigned long long)(atom->time_spent_us ?
					atom->time_spent_us * 1000 : start_timestamp)
				);
	}
	spin_unlock_irqrestore(&kctx->kbdev->js_data.runpool_irq.lock, irq_flags);
	mutex_unlock(&kctx->jctx.lock);

	return 0;
}


/**
 * @brief File operations related to debugfs entry for atoms
 */
static int kbasep_jd_debugfs_atoms_open(struct inode *in, struct file *file)
{
	return single_open(file, kbasep_jd_debugfs_atoms_show, in->i_private);
}

static const struct file_operations kbasep_jd_debugfs_atoms_fops = {
	.open = kbasep_jd_debugfs_atoms_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};


int kbasep_jd_debugfs_init(struct kbase_device *kbdev)
{
	kbdev->jd_directory = debugfs_create_dir(
			"jd", kbdev->mali_debugfs_directory);
	if (IS_ERR(kbdev->jd_directory)) {
		dev_err(kbdev->dev, "Couldn't create mali jd debugfs directory\n");
		goto err;
	}

	return 0;

err:
	return -1;
}


void kbasep_jd_debugfs_term(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	if (!IS_ERR(kbdev->jd_directory))
		debugfs_remove_recursive(kbdev->jd_directory);
}


int kbasep_jd_debugfs_ctx_add(struct kbase_context *kctx)
{
	/* Refer below for format string, %u is 10 chars max */
	char dir_name[10 * 2 + 2];

	KBASE_DEBUG_ASSERT(kctx != NULL);

	/* Create per-context directory */
	scnprintf(dir_name, sizeof(dir_name), "%u_%u", kctx->pid, kctx->id);
	kctx->jd_ctx_dir = debugfs_create_dir(dir_name, kctx->kbdev->jd_directory);
	if (IS_ERR(kctx->jd_ctx_dir))
		goto err;

	/* Expose all atoms */
	if (IS_ERR(debugfs_create_file("atoms", S_IRUGO,
			kctx->jd_ctx_dir, kctx, &kbasep_jd_debugfs_atoms_fops)))
		goto err_jd_ctx_dir;

	return 0;

err_jd_ctx_dir:
	debugfs_remove_recursive(kctx->jd_ctx_dir);
err:
	return -1;
}


void kbasep_jd_debugfs_ctx_remove(struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kctx != NULL);

	if (!IS_ERR(kctx->jd_ctx_dir))
		debugfs_remove_recursive(kctx->jd_ctx_dir);
}

#else /* CONFIG_DEBUG_FS */

/**
 * @brief Stub functions for when debugfs is disabled
 */
int kbasep_jd_debugfs_init(struct kbase_device *kbdev)
{
	return 0;
}
void kbasep_jd_debugfs_term(struct kbase_device *kbdev)
{
}
int kbasep_jd_debugfs_ctx_add(struct kbase_context *ctx)
{
	return 0;
}
void kbasep_jd_debugfs_ctx_remove(struct kbase_context *ctx)
{
}

#endif /* CONFIG_DEBUG_FS */
