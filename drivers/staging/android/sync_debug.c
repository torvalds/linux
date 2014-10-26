/*
 * drivers/base/sync.c
 *
 * Copyright (C) 2012 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/debugfs.h>
#include <linux/export.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/anon_inodes.h>
#include <linux/time64.h>
#include "sync.h"

#ifdef CONFIG_DEBUG_FS

static LIST_HEAD(sync_timeline_list_head);
static DEFINE_SPINLOCK(sync_timeline_list_lock);
static LIST_HEAD(sync_fence_list_head);
static DEFINE_SPINLOCK(sync_fence_list_lock);

void sync_timeline_debug_add(struct sync_timeline *obj)
{
	unsigned long flags;

	spin_lock_irqsave(&sync_timeline_list_lock, flags);
	list_add_tail(&obj->sync_timeline_list, &sync_timeline_list_head);
	spin_unlock_irqrestore(&sync_timeline_list_lock, flags);
}

void sync_timeline_debug_remove(struct sync_timeline *obj)
{
	unsigned long flags;

	spin_lock_irqsave(&sync_timeline_list_lock, flags);
	list_del(&obj->sync_timeline_list);
	spin_unlock_irqrestore(&sync_timeline_list_lock, flags);
}

void sync_fence_debug_add(struct sync_fence *fence)
{
	unsigned long flags;

	spin_lock_irqsave(&sync_fence_list_lock, flags);
	list_add_tail(&fence->sync_fence_list, &sync_fence_list_head);
	spin_unlock_irqrestore(&sync_fence_list_lock, flags);
}

void sync_fence_debug_remove(struct sync_fence *fence)
{
	unsigned long flags;

	spin_lock_irqsave(&sync_fence_list_lock, flags);
	list_del(&fence->sync_fence_list);
	spin_unlock_irqrestore(&sync_fence_list_lock, flags);
}

static const char *sync_status_str(int status)
{
	if (status == 0)
		return "signaled";

	if (status > 0)
		return "active";

	return "error";
}

static void sync_print_pt(struct seq_file *s, struct sync_pt *pt, bool fence)
{
	int status = 1;
	struct sync_timeline *parent = sync_pt_parent(pt);

	if (fence_is_signaled_locked(&pt->base))
		status = pt->base.status;

	seq_printf(s, "  %s%spt %s",
		   fence ? parent->name : "",
		   fence ? "_" : "",
		   sync_status_str(status));

	if (status <= 0) {
		struct timespec64 ts64 = ktime_to_timespec64(pt->base.timestamp);

		seq_printf(s, "@%lld.%09ld", (s64)ts64.tv_sec, ts64.tv_nsec);
	}

	if (parent->ops->timeline_value_str &&
	    parent->ops->pt_value_str) {
		char value[64];

		parent->ops->pt_value_str(pt, value, sizeof(value));
		seq_printf(s, ": %s", value);
		if (fence) {
			parent->ops->timeline_value_str(parent, value,
						    sizeof(value));
			seq_printf(s, " / %s", value);
		}
	}

	seq_puts(s, "\n");
}

static void sync_print_obj(struct seq_file *s, struct sync_timeline *obj)
{
	struct list_head *pos;
	unsigned long flags;

	seq_printf(s, "%s %s", obj->name, obj->ops->driver_name);

	if (obj->ops->timeline_value_str) {
		char value[64];

		obj->ops->timeline_value_str(obj, value, sizeof(value));
		seq_printf(s, ": %s", value);
	}

	seq_puts(s, "\n");

	spin_lock_irqsave(&obj->child_list_lock, flags);
	list_for_each(pos, &obj->child_list_head) {
		struct sync_pt *pt =
			container_of(pos, struct sync_pt, child_list);
		sync_print_pt(s, pt, false);
	}
	spin_unlock_irqrestore(&obj->child_list_lock, flags);
}

static void sync_print_fence(struct seq_file *s, struct sync_fence *fence)
{
	wait_queue_t *pos;
	unsigned long flags;
	int i;

	seq_printf(s, "[%p] %s: %s\n", fence, fence->name,
		   sync_status_str(atomic_read(&fence->status)));

	for (i = 0; i < fence->num_fences; ++i) {
		struct sync_pt *pt =
			container_of(fence->cbs[i].sync_pt,
				     struct sync_pt, base);

		sync_print_pt(s, pt, true);
	}

	spin_lock_irqsave(&fence->wq.lock, flags);
	list_for_each_entry(pos, &fence->wq.task_list, task_list) {
		struct sync_fence_waiter *waiter;

		if (pos->func != &sync_fence_wake_up_wq)
			continue;

		waiter = container_of(pos, struct sync_fence_waiter, work);

		seq_printf(s, "waiter %pF\n", waiter->callback);
	}
	spin_unlock_irqrestore(&fence->wq.lock, flags);
}

static int sync_debugfs_show(struct seq_file *s, void *unused)
{
	unsigned long flags;
	struct list_head *pos;

	seq_puts(s, "objs:\n--------------\n");

	spin_lock_irqsave(&sync_timeline_list_lock, flags);
	list_for_each(pos, &sync_timeline_list_head) {
		struct sync_timeline *obj =
			container_of(pos, struct sync_timeline,
				     sync_timeline_list);

		sync_print_obj(s, obj);
		seq_puts(s, "\n");
	}
	spin_unlock_irqrestore(&sync_timeline_list_lock, flags);

	seq_puts(s, "fences:\n--------------\n");

	spin_lock_irqsave(&sync_fence_list_lock, flags);
	list_for_each(pos, &sync_fence_list_head) {
		struct sync_fence *fence =
			container_of(pos, struct sync_fence, sync_fence_list);

		sync_print_fence(s, fence);
		seq_puts(s, "\n");
	}
	spin_unlock_irqrestore(&sync_fence_list_lock, flags);
	return 0;
}

static int sync_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, sync_debugfs_show, inode->i_private);
}

static const struct file_operations sync_debugfs_fops = {
	.open           = sync_debugfs_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static __init int sync_debugfs_init(void)
{
	debugfs_create_file("sync", S_IRUGO, NULL, NULL, &sync_debugfs_fops);
	return 0;
}
late_initcall(sync_debugfs_init);

#define DUMP_CHUNK 256
static char sync_dump_buf[64 * 1024];
void sync_dump(void)
{
	struct seq_file s = {
		.buf = sync_dump_buf,
		.size = sizeof(sync_dump_buf) - 1,
	};
	int i;

	sync_debugfs_show(&s, NULL);

	for (i = 0; i < s.count; i += DUMP_CHUNK) {
		if ((s.count - i) > DUMP_CHUNK) {
			char c = s.buf[i + DUMP_CHUNK];

			s.buf[i + DUMP_CHUNK] = 0;
			pr_cont("%s", s.buf + i);
			s.buf[i + DUMP_CHUNK] = c;
		} else {
			s.buf[s.count] = 0;
			pr_cont("%s", s.buf + i);
		}
	}
}

#endif
