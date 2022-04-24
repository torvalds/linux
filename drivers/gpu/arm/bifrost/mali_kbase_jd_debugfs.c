// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2014-2022 ARM Limited. All rights reserved.
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

#if IS_ENABLED(CONFIG_DEBUG_FS)

#include <linux/seq_file.h>
#include <mali_kbase.h>
#include <mali_kbase_jd_debugfs.h>
#include <mali_kbase_dma_fence.h>
#if defined(CONFIG_SYNC) || defined(CONFIG_SYNC_FILE)
#include <mali_kbase_sync.h>
#endif
#include <uapi/gpu/arm/bifrost/mali_kbase_ioctl.h>

struct kbase_jd_debugfs_depinfo {
	u8 id;
	char type;
};

static void kbase_jd_debugfs_fence_info(struct kbase_jd_atom *atom,
					struct seq_file *sfile)
{
#if defined(CONFIG_SYNC) || defined(CONFIG_SYNC_FILE)
	struct kbase_sync_fence_info info;
	int res;

	switch (atom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) {
	case BASE_JD_REQ_SOFT_FENCE_TRIGGER:
		res = kbase_sync_fence_out_info_get(atom, &info);
		if (res == 0)
			seq_printf(sfile, "Sa([%pK]%d) ",
				   info.fence, info.status);
		break;
	case BASE_JD_REQ_SOFT_FENCE_WAIT:
		res = kbase_sync_fence_in_info_get(atom, &info);
		if (res == 0)
			seq_printf(sfile, "Wa([%pK]%d) ",
				   info.fence, info.status);
		break;
	default:
		break;
	}
#endif /* CONFIG_SYNC || CONFIG_SYNC_FILE */

#ifdef CONFIG_MALI_BIFROST_DMA_FENCE
	if (atom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES) {
		struct kbase_fence_cb *cb;

		if (atom->dma_fence.fence) {
#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
			struct fence *fence = atom->dma_fence.fence;
#else
			struct dma_fence *fence = atom->dma_fence.fence;
#endif

			seq_printf(sfile,
#if (KERNEL_VERSION(4, 8, 0) > LINUX_VERSION_CODE)
				   "Sd(%u#%u: %s) ",
#elif (KERNEL_VERSION(5, 1, 0) > LINUX_VERSION_CODE)
				   "Sd(%llu#%u: %s) ",
#else
				   "Sd(%llu#%llu: %s) ",
#endif
				   fence->context, fence->seqno,
				   dma_fence_is_signaled(fence) ? "signaled" :
								  "active");
		}

		list_for_each_entry(cb, &atom->dma_fence.callbacks,
				    node) {
#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
			struct fence *fence = cb->fence;
#else
			struct dma_fence *fence = cb->fence;
#endif

			seq_printf(sfile,
#if (KERNEL_VERSION(4, 8, 0) > LINUX_VERSION_CODE)
				   "Wd(%u#%u: %s) ",
#elif (KERNEL_VERSION(5, 1, 0) > LINUX_VERSION_CODE)
				   "Wd(%llu#%u: %s) ",
#else
				   "Wd(%llu#%llu: %s) ",
#endif
				   fence->context, fence->seqno,
				   dma_fence_is_signaled(fence) ? "signaled" :
								  "active");
		}
	}
#endif /* CONFIG_MALI_BIFROST_DMA_FENCE */

}

static void kbasep_jd_debugfs_atom_deps(
		struct kbase_jd_debugfs_depinfo *deps,
		struct kbase_jd_atom *atom)
{
	struct kbase_context *kctx = atom->kctx;
	int i;

	for (i = 0; i < 2; i++)	{
		deps[i].id = (unsigned int)(atom->dep[i].atom ?
				kbase_jd_atom_id(kctx, atom->dep[i].atom) : 0);

		switch (atom->dep[i].dep_type) {
		case BASE_JD_DEP_TYPE_INVALID:
			deps[i].type = ' ';
			break;
		case BASE_JD_DEP_TYPE_DATA:
			deps[i].type = 'D';
			break;
		case BASE_JD_DEP_TYPE_ORDER:
			deps[i].type = '>';
			break;
		default:
			deps[i].type = '?';
			break;
		}
	}
}
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

	/* Print version */
	seq_printf(sfile, "v%u\n", MALI_JD_DEBUGFS_VERSION);

	/* Print U/K API version */
	seq_printf(sfile, "ukv%u.%u\n", BASE_UK_VERSION_MAJOR,
			BASE_UK_VERSION_MINOR);

	/* Print table heading */
	seq_puts(sfile, " ID, Core req, St,   Predeps,           Start time, Additional info...\n");

	atoms = kctx->jctx.atoms;
	/* General atom states */
	mutex_lock(&kctx->jctx.lock);
	/* JS-related states */
	spin_lock_irqsave(&kctx->kbdev->hwaccess_lock, irq_flags);
	for (i = 0; i != BASE_JD_ATOM_COUNT; ++i) {
		struct kbase_jd_atom *atom = &atoms[i];
		s64 start_timestamp = 0;
		struct kbase_jd_debugfs_depinfo deps[2];

		if (atom->status == KBASE_JD_ATOM_STATE_UNUSED)
			continue;

		/* start_timestamp is cleared as soon as the atom leaves UNUSED state
		 * and set before a job is submitted to the h/w, a non-zero value means
		 * it is valid
		 */
		if (ktime_to_ns(atom->start_timestamp))
			start_timestamp =
				ktime_to_ns(ktime_sub(ktime_get_raw(), atom->start_timestamp));

		kbasep_jd_debugfs_atom_deps(deps, atom);

		seq_printf(sfile,
				"%3u, %8x, %2u, %c%3u %c%3u, %20lld, ",
				i, atom->core_req, atom->status,
				deps[0].type, deps[0].id,
				deps[1].type, deps[1].id,
				start_timestamp);


		kbase_jd_debugfs_fence_info(atom, sfile);

		seq_puts(sfile, "\n");
	}
	spin_unlock_irqrestore(&kctx->kbdev->hwaccess_lock, irq_flags);
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
	.owner = THIS_MODULE,
	.open = kbasep_jd_debugfs_atoms_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void kbasep_jd_debugfs_ctx_init(struct kbase_context *kctx)
{
#if (KERNEL_VERSION(4, 7, 0) <= LINUX_VERSION_CODE)
	const mode_t mode = 0444;
#else
	const mode_t mode = 0400;
#endif

	/* Caller already ensures this, but we keep the pattern for
	 * maintenance safety.
	 */
	if (WARN_ON(!kctx) ||
		WARN_ON(IS_ERR_OR_NULL(kctx->kctx_dentry)))
		return;

	/* Expose all atoms */
	debugfs_create_file("atoms", mode, kctx->kctx_dentry, kctx,
			&kbasep_jd_debugfs_atoms_fops);

}

#endif /* CONFIG_DEBUG_FS */
