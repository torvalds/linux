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

#include "mali_kbase_csf_tiler_heap_debugfs.h"
#include "mali_kbase_csf_tiler_heap_def.h"
#include <mali_kbase.h>
#include <linux/seq_file.h>

#if IS_ENABLED(CONFIG_DEBUG_FS)

/**
 * kbasep_csf_tiler_heap_debugfs_show() - Print tiler heap information for per context
 *
 * @file: The seq_file for printing to
 * @data: The debugfs dentry private data, a pointer to kbase_context
 *
 * Return: 0 in any case.
 */
static int kbasep_csf_tiler_heap_debugfs_show(struct seq_file *file, void *data)
{
	struct kbase_context *kctx = file->private;
	struct kbase_csf_tiler_heap_context *tiler_heaps_p = &kctx->csf.tiler_heaps;
	struct kbase_csf_tiler_heap *heap;
	struct kbase_csf_tiler_heap_chunk *chunk;

	seq_printf(file, "MALI_CSF_TILER_HEAP_DEBUGFS_VERSION: v%u\n", MALI_CSF_TILER_HEAP_DEBUGFS_VERSION);

	mutex_lock(&tiler_heaps_p->lock);

	list_for_each_entry(heap, &tiler_heaps_p->list, link) {
		if (heap->kctx != kctx)
			continue;

		seq_printf(file, "HEAP(gpu_va = 0x%llx):\n", heap->gpu_va);
		seq_printf(file, "\tchunk_size = %u\n", heap->chunk_size);
		seq_printf(file, "\tchunk_count = %u\n", heap->chunk_count);
		seq_printf(file, "\tmax_chunks = %u\n", heap->max_chunks);
		seq_printf(file, "\ttarget_in_flight = %u\n", heap->target_in_flight);

		list_for_each_entry(chunk, &heap->chunks_list, link)
			seq_printf(file, "\t\tchunk gpu_va = 0x%llx\n",
				   chunk->gpu_va);
	}

	mutex_unlock(&tiler_heaps_p->lock);

	return 0;
}

/**
 * kbasep_csf_tiler_heap_total_debugfs_show() - Print the total memory allocated
 *                                              for all tiler heaps in a context.
 *
 * @file: The seq_file for printing to
 * @data: The debugfs dentry private data, a pointer to kbase_context
 *
 * Return: 0 in any case.
 */
static int kbasep_csf_tiler_heap_total_debugfs_show(struct seq_file *file, void *data)
{
	struct kbase_context *kctx = file->private;

	seq_printf(file, "MALI_CSF_TILER_HEAP_DEBUGFS_VERSION: v%u\n",
		   MALI_CSF_TILER_HEAP_DEBUGFS_VERSION);
	seq_printf(file, "Total number of chunks of all heaps in the context: %lu\n",
		   (unsigned long)kctx->running_total_tiler_heap_nr_chunks);
	seq_printf(file, "Total allocated memory of all heaps in the context: %llu\n",
		   (unsigned long long)kctx->running_total_tiler_heap_memory);
	seq_printf(file, "Peak allocated tiler heap memory in the context: %llu\n",
		   (unsigned long long)kctx->peak_total_tiler_heap_memory);

	return 0;
}

static int kbasep_csf_tiler_heap_debugfs_open(struct inode *in, struct file *file)
{
	return single_open(file, kbasep_csf_tiler_heap_debugfs_show, in->i_private);
}

static int kbasep_csf_tiler_heap_total_debugfs_open(struct inode *in, struct file *file)
{
	return single_open(file, kbasep_csf_tiler_heap_total_debugfs_show, in->i_private);
}

static const struct file_operations kbasep_csf_tiler_heap_debugfs_fops = {
	.open = kbasep_csf_tiler_heap_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations kbasep_csf_tiler_heap_total_debugfs_fops = {
	.open = kbasep_csf_tiler_heap_total_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void kbase_csf_tiler_heap_debugfs_init(struct kbase_context *kctx)
{
	struct dentry *file;

	if (WARN_ON(!kctx || IS_ERR_OR_NULL(kctx->kctx_dentry)))
		return;

	file = debugfs_create_file("tiler_heaps", 0444, kctx->kctx_dentry,
			kctx, &kbasep_csf_tiler_heap_debugfs_fops);

	if (IS_ERR_OR_NULL(file)) {
		dev_warn(kctx->kbdev->dev,
				"Unable to create tiler heap debugfs entry");
	}
}

void kbase_csf_tiler_heap_total_debugfs_init(struct kbase_context *kctx)
{
	struct dentry *file;

	if (WARN_ON(!kctx || IS_ERR_OR_NULL(kctx->kctx_dentry)))
		return;

	file = debugfs_create_file("tiler_heaps_total", 0444, kctx->kctx_dentry,
				   kctx, &kbasep_csf_tiler_heap_total_debugfs_fops);

	if (IS_ERR_OR_NULL(file)) {
		dev_warn(kctx->kbdev->dev,
			"Unable to create total tiler heap allocated memory debugfs entry");
	}
}

#else
/*
 * Stub functions for when debugfs is disabled
 */
void kbase_csf_tiler_heap_debugfs_init(struct kbase_context *kctx)
{
}

void kbase_csf_tiler_heap_total_debugfs_init(struct kbase_context *kctx)
{
}

#endif /* CONFIG_DEBUG_FS */

