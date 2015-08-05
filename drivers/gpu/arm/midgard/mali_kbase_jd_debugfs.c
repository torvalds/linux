/*
 *
 * (C) COPYRIGHT 2014 ARM Limited. All rights reserved.
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
 * kbasep_jd_debugfs_atoms_show - Show callback for the JD atoms debugfs file.
 * @sfile: The debugfs entry
 * @data:  Data associated with the entry
 *
 * This function is called to get the contents of the JD atoms debugfs file.
 * This is a report of all atoms managed by kbase_jd_context.atoms
 *
 * Return: 0 if successfully prints data in debugfs entry file, failure
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
				(unsigned)(atom->dep[0].atom ?
						atom->dep[0].atom - atoms : 0),
				(unsigned)(atom->dep[1].atom ?
						atom->dep[1].atom - atoms : 0),
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
 * kbasep_jd_debugfs_atoms_open - open operation for atom debugfs file
 * @in: &struct inode pointer
 * @file: &struct file pointer
 *
 * Return: file descriptor
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

void kbasep_jd_debugfs_ctx_add(struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kctx != NULL);

	/* Expose all atoms */
	debugfs_create_file("atoms", S_IRUGO, kctx->kctx_dentry, kctx,
			&kbasep_jd_debugfs_atoms_fops);

}

#endif /* CONFIG_DEBUG_FS */
