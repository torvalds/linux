// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sync File validation framework and debug information
 *
 * Copyright (C) 2012 Google, Inc.
 */

#include <linux/debugfs.h>
#include "sync_debug.h"

static struct dentry *dbgfs;

static LIST_HEAD(sync_timeline_list_head);
static DEFINE_SPINLOCK(sync_timeline_list_lock);

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

static const char *sync_status_str(int status)
{
	if (status < 0)
		return "error";

	if (status > 0)
		return "signaled";

	return "active";
}

static void sync_print_fence(struct seq_file *s,
			     struct dma_fence *fence, bool show)
{
	struct sync_timeline *parent = dma_fence_parent(fence);
	int status;

	status = dma_fence_get_status_locked(fence);

	seq_printf(s, "  %s%sfence %s",
		   show ? parent->name : "",
		   show ? "_" : "",
		   sync_status_str(status));

	if (test_bit(DMA_FENCE_FLAG_TIMESTAMP_BIT, &fence->flags)) {
		struct timespec64 ts64 =
			ktime_to_timespec64(fence->timestamp);

		seq_printf(s, "@%lld.%09ld", (s64)ts64.tv_sec, ts64.tv_nsec);
	}

	seq_printf(s, ": %lld", fence->seqno);
	seq_printf(s, " / %d", parent->value);
	seq_putc(s, '\n');
}

static void sync_print_obj(struct seq_file *s, struct sync_timeline *obj)
{
	struct list_head *pos;

	seq_printf(s, "%s: %d\n", obj->name, obj->value);

	spin_lock(&obj->lock); /* Caller already disabled IRQ. */
	list_for_each(pos, &obj->pt_list) {
		struct sync_pt *pt = container_of(pos, struct sync_pt, link);
		sync_print_fence(s, &pt->base, false);
	}
	spin_unlock(&obj->lock);
}

static int sync_info_debugfs_show(struct seq_file *s, void *unused)
{
	struct list_head *pos;

	seq_puts(s, "objs:\n--------------\n");

	spin_lock_irq(&sync_timeline_list_lock);
	list_for_each(pos, &sync_timeline_list_head) {
		struct sync_timeline *obj =
			container_of(pos, struct sync_timeline,
				     sync_timeline_list);

		sync_print_obj(s, obj);
		seq_putc(s, '\n');
	}
	spin_unlock_irq(&sync_timeline_list_lock);

	seq_puts(s, "fences:\n--------------\n");

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(sync_info_debugfs);

static __init int sync_debugfs_init(void)
{
	dbgfs = debugfs_create_dir("sync", NULL);

	/*
	 * The debugfs files won't ever get removed and thus, there is
	 * no need to protect it against removal races. The use of
	 * debugfs_create_file_unsafe() is actually safe here.
	 */
	debugfs_create_file_unsafe("info", 0444, dbgfs, NULL,
				   &sync_info_debugfs_fops);
	debugfs_create_file_unsafe("sw_sync", 0644, dbgfs, NULL,
				   &sw_sync_debugfs_fops);

	return 0;
}
late_initcall(sync_debugfs_init);
