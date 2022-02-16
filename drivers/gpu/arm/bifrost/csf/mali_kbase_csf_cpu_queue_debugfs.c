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

#include "mali_kbase_csf_cpu_queue_debugfs.h"
#include <mali_kbase.h>
#include <linux/seq_file.h>

#if IS_ENABLED(CONFIG_DEBUG_FS)

bool kbase_csf_cpu_queue_read_dump_req(struct kbase_context *kctx,
					struct base_csf_notification *req)
{
	if (atomic_cmpxchg(&kctx->csf.cpu_queue.dump_req_status,
			   BASE_CSF_CPU_QUEUE_DUMP_ISSUED,
			   BASE_CSF_CPU_QUEUE_DUMP_PENDING) !=
		BASE_CSF_CPU_QUEUE_DUMP_ISSUED) {
		return false;
	}

	req->type = BASE_CSF_NOTIFICATION_CPU_QUEUE_DUMP;
	return true;
}

/**
 * kbasep_csf_cpu_queue_debugfs_show() - Print cpu queue information for per context
 *
 * @file: The seq_file for printing to
 * @data: The debugfs dentry private data, a pointer to kbase_context
 *
 * Return: Negative error code or 0 on success.
 */
static int kbasep_csf_cpu_queue_debugfs_show(struct seq_file *file, void *data)
{
	struct kbase_context *kctx = file->private;

	mutex_lock(&kctx->csf.lock);
	if (atomic_read(&kctx->csf.cpu_queue.dump_req_status) !=
				BASE_CSF_CPU_QUEUE_DUMP_COMPLETE) {
		seq_puts(file, "Dump request already started! (try again)\n");
		mutex_unlock(&kctx->csf.lock);
		return -EBUSY;
	}

	atomic_set(&kctx->csf.cpu_queue.dump_req_status, BASE_CSF_CPU_QUEUE_DUMP_ISSUED);
	init_completion(&kctx->csf.cpu_queue.dump_cmp);
	kbase_event_wakeup(kctx);
	mutex_unlock(&kctx->csf.lock);

	seq_puts(file,
		"CPU Queues table (version:v" __stringify(MALI_CSF_CPU_QUEUE_DEBUGFS_VERSION) "):\n");

	wait_for_completion_timeout(&kctx->csf.cpu_queue.dump_cmp,
			msecs_to_jiffies(3000));

	mutex_lock(&kctx->csf.lock);
	if (kctx->csf.cpu_queue.buffer) {
		WARN_ON(atomic_read(&kctx->csf.cpu_queue.dump_req_status) !=
				    BASE_CSF_CPU_QUEUE_DUMP_PENDING);

		seq_printf(file, "%s\n", kctx->csf.cpu_queue.buffer);

		kfree(kctx->csf.cpu_queue.buffer);
		kctx->csf.cpu_queue.buffer = NULL;
		kctx->csf.cpu_queue.buffer_size = 0;
	} else
		seq_puts(file, "Dump error! (time out)\n");

	atomic_set(&kctx->csf.cpu_queue.dump_req_status,
			BASE_CSF_CPU_QUEUE_DUMP_COMPLETE);

	mutex_unlock(&kctx->csf.lock);
	return 0;
}

static int kbasep_csf_cpu_queue_debugfs_open(struct inode *in, struct file *file)
{
	return single_open(file, kbasep_csf_cpu_queue_debugfs_show, in->i_private);
}

static const struct file_operations kbasep_csf_cpu_queue_debugfs_fops = {
	.open = kbasep_csf_cpu_queue_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void kbase_csf_cpu_queue_debugfs_init(struct kbase_context *kctx)
{
	struct dentry *file;

	if (WARN_ON(!kctx || IS_ERR_OR_NULL(kctx->kctx_dentry)))
		return;

	file = debugfs_create_file("cpu_queue", 0444, kctx->kctx_dentry,
			kctx, &kbasep_csf_cpu_queue_debugfs_fops);

	if (IS_ERR_OR_NULL(file)) {
		dev_warn(kctx->kbdev->dev,
				"Unable to create cpu queue debugfs entry");
	}

	kctx->csf.cpu_queue.buffer = NULL;
	kctx->csf.cpu_queue.buffer_size = 0;
	atomic_set(&kctx->csf.cpu_queue.dump_req_status,
		   BASE_CSF_CPU_QUEUE_DUMP_COMPLETE);
}

int kbase_csf_cpu_queue_dump(struct kbase_context *kctx,
		u64 buffer, size_t buf_size)
{
	int err = 0;

	size_t alloc_size = buf_size;
	char *dump_buffer;

	if (!buffer || !alloc_size)
		goto done;

	alloc_size = (alloc_size + PAGE_SIZE) & ~(PAGE_SIZE - 1);
	dump_buffer = kzalloc(alloc_size, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(dump_buffer)) {
		err = -ENOMEM;
		goto done;
	}

	WARN_ON(kctx->csf.cpu_queue.buffer != NULL);

	err = copy_from_user(dump_buffer,
			u64_to_user_ptr(buffer),
			buf_size);
	if (err) {
		kfree(dump_buffer);
		err = -EFAULT;
		goto done;
	}

	mutex_lock(&kctx->csf.lock);

	kfree(kctx->csf.cpu_queue.buffer);

	if (atomic_read(&kctx->csf.cpu_queue.dump_req_status) ==
			BASE_CSF_CPU_QUEUE_DUMP_PENDING) {
		kctx->csf.cpu_queue.buffer = dump_buffer;
		kctx->csf.cpu_queue.buffer_size = buf_size;
		complete_all(&kctx->csf.cpu_queue.dump_cmp);
	} else {
		kfree(dump_buffer);
	}

	mutex_unlock(&kctx->csf.lock);
done:
	return err;
}
#else
/*
 * Stub functions for when debugfs is disabled
 */
void kbase_csf_cpu_queue_debugfs_init(struct kbase_context *kctx)
{
}

bool kbase_csf_cpu_queue_read_dump_req(struct kbase_context *kctx,
					struct base_csf_notification *req)
{
	return false;
}

int kbase_csf_cpu_queue_dump(struct kbase_context *kctx,
			u64 buffer, size_t buf_size)
{
	return 0;
}
#endif /* CONFIG_DEBUG_FS */
