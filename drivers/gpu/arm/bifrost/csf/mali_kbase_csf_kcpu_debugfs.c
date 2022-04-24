// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2019-2022 ARM Limited. All rights reserved.
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

#include "mali_kbase_csf_kcpu_debugfs.h"
#include <mali_kbase.h>
#include <linux/seq_file.h>

#if IS_ENABLED(CONFIG_SYNC_FILE)
#include "mali_kbase_sync.h"
#endif

#if IS_ENABLED(CONFIG_DEBUG_FS)

/**
 * kbasep_csf_kcpu_debugfs_print_cqs_waits() - Print additional info for KCPU
 *					queues blocked on CQS wait commands.
 *
 * @file:  The seq_file to print to
 * @kctx:  The context of the KCPU queue
 * @waits: Pointer to the KCPU CQS wait command info
 */
static void kbasep_csf_kcpu_debugfs_print_cqs_waits(struct seq_file *file,
		struct kbase_context *kctx,
		struct kbase_kcpu_command_cqs_wait_info *waits)
{
	unsigned int i;

	for (i = 0; i < waits->nr_objs; i++) {
		struct kbase_vmap_struct *mapping;
		u32 val;
		char const *msg;
		u32 *const cpu_ptr = (u32 *)kbase_phy_alloc_mapping_get(kctx,
					waits->objs[i].addr, &mapping);

		if (!cpu_ptr)
			return;

		val = *cpu_ptr;

		kbase_phy_alloc_mapping_put(kctx, mapping);

		msg = (waits->inherit_err_flags && (1U << i)) ? "true" :
								"false";
		seq_printf(file, "   %llx(%u > %u, inherit_err: %s), ",
			   waits->objs[i].addr, val, waits->objs[i].val, msg);
	}
}

/**
 * kbasep_csf_kcpu_debugfs_print_queue() - Print debug data for a KCPU queue
 *
 * @file:  The seq_file to print to
 * @kctx:  The context of the KCPU queue
 * @queue: Pointer to the KCPU queue
 */
static void kbasep_csf_kcpu_debugfs_print_queue(struct seq_file *file,
		struct kbase_context *kctx,
		struct kbase_kcpu_command_queue *queue)
{
	if (WARN_ON(!queue))
		return;

	lockdep_assert_held(&kctx->csf.kcpu_queues.lock);

	seq_printf(file, "%16u, %11u, %7u, %13llu  %8u",
			queue->num_pending_cmds, queue->enqueue_failed,
			queue->command_started ? 1 : 0,
			queue->fence_context, queue->fence_seqno);

	if (queue->command_started) {
		struct kbase_kcpu_command *cmd =
				&queue->commands[queue->start_offset];
		switch (cmd->type) {
#if IS_ENABLED(CONFIG_SYNC_FILE)
		case BASE_KCPU_COMMAND_TYPE_FENCE_WAIT:
		{
			struct kbase_sync_fence_info info;

			kbase_sync_fence_info_get(cmd->info.fence.fence, &info);
			seq_printf(file, ",  Fence      %pK %s %s",
				   info.fence, info.name,
				   kbase_sync_status_string(info.status));
			break;
		}
#endif
		case BASE_KCPU_COMMAND_TYPE_CQS_WAIT:
			seq_puts(file, ",  CQS     ");
			kbasep_csf_kcpu_debugfs_print_cqs_waits(file, kctx,
					&cmd->info.cqs_wait);
			break;
		default:
			seq_puts(file, ", U, Unknown blocking command");
			break;
		}
	}

	seq_puts(file, "\n");
}

/**
 * kbasep_csf_kcpu_debugfs_show() - Print the KCPU queues debug information
 *
 * @file: The seq_file for printing to
 * @data: The debugfs dentry private data, a pointer to kbase_context
 *
 * Return: Negative error code or 0 on success.
 */
static int kbasep_csf_kcpu_debugfs_show(struct seq_file *file, void *data)
{
	struct kbase_context *kctx = file->private;
	unsigned long idx;

	seq_printf(file, "MALI_CSF_KCPU_DEBUGFS_VERSION: v%u\n", MALI_CSF_KCPU_DEBUGFS_VERSION);
	seq_puts(file, "Queue Idx(err-mode), Pending Commands, Enqueue err, Blocked, Fence context  &  seqno, (Wait Type, Additional info)\n");
	mutex_lock(&kctx->csf.kcpu_queues.lock);

	idx = find_first_bit(kctx->csf.kcpu_queues.in_use,
			KBASEP_MAX_KCPU_QUEUES);

	while (idx < KBASEP_MAX_KCPU_QUEUES) {
		struct kbase_kcpu_command_queue *queue =
					kctx->csf.kcpu_queues.array[idx];

		seq_printf(file, "%9lu( %s ), ", idx,
				 queue->has_error ? "InErr" : "NoErr");
		kbasep_csf_kcpu_debugfs_print_queue(file, kctx,
				kctx->csf.kcpu_queues.array[idx]);

		idx = find_next_bit(kctx->csf.kcpu_queues.in_use,
				KBASEP_MAX_KCPU_QUEUES, idx + 1);
	}

	mutex_unlock(&kctx->csf.kcpu_queues.lock);
	return 0;
}

static int kbasep_csf_kcpu_debugfs_open(struct inode *in, struct file *file)
{
	return single_open(file, kbasep_csf_kcpu_debugfs_show, in->i_private);
}

static const struct file_operations kbasep_csf_kcpu_debugfs_fops = {
	.open = kbasep_csf_kcpu_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void kbase_csf_kcpu_debugfs_init(struct kbase_context *kctx)
{
	struct dentry *file;
#if (KERNEL_VERSION(4, 7, 0) <= LINUX_VERSION_CODE)
	const mode_t mode = 0444;
#else
	const mode_t mode = 0400;
#endif

	if (WARN_ON(!kctx || IS_ERR_OR_NULL(kctx->kctx_dentry)))
		return;

	file = debugfs_create_file("kcpu_queues", mode, kctx->kctx_dentry,
			kctx, &kbasep_csf_kcpu_debugfs_fops);

	if (IS_ERR_OR_NULL(file)) {
		dev_warn(kctx->kbdev->dev,
				"Unable to create KCPU debugfs entry");
	}
}


#else
/*
 * Stub functions for when debugfs is disabled
 */
void kbase_csf_kcpu_debugfs_init(struct kbase_context *kctx)
{
}

#endif /* CONFIG_DEBUG_FS */
