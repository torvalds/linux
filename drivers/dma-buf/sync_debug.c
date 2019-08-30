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
static LIST_HEAD(sync_file_list_head);
static DEFINE_SPINLOCK(sync_file_list_lock);

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

void sync_file_debug_add(struct sync_file *sync_file)
{
	unsigned long flags;

	spin_lock_irqsave(&sync_file_list_lock, flags);
	list_add_tail(&sync_file->sync_file_list, &sync_file_list_head);
	spin_unlock_irqrestore(&sync_file_list_lock, flags);
}

void sync_file_debug_remove(struct sync_file *sync_file)
{
	unsigned long flags;

	spin_lock_irqsave(&sync_file_list_lock, flags);
	list_del(&sync_file->sync_file_list);
	spin_unlock_irqrestore(&sync_file_list_lock, flags);
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

	if (fence->ops->timeline_value_str &&
		fence->ops->fence_value_str) {
		char value[64];
		bool success;

		fence->ops->fence_value_str(fence, value, sizeof(value));
		success = strlen(value);

		if (success) {
			seq_printf(s, ": %s", value);

			fence->ops->timeline_value_str(fence, value,
						       sizeof(value));

			if (strlen(value))
				seq_printf(s, " / %s", value);
		}
	}

	seq_putc(s, '\n');
}

static void sync_print_obj(struct seq_file *s, struct sync_timeline *obj)
{
	struct list_head *pos;

	seq_printf(s, "%s: %d\n", obj->name, obj->value);

	spin_lock_irq(&obj->lock);
	list_for_each(pos, &obj->pt_list) {
		struct sync_pt *pt = container_of(pos, struct sync_pt, link);
		sync_print_fence(s, &pt->base, false);
	}
	spin_unlock_irq(&obj->lock);
}

static void sync_print_sync_file(struct seq_file *s,
				  struct sync_file *sync_file)
{
	char buf[128];
	int i;

	seq_printf(s, "[%p] %s: %s\n", sync_file,
		   sync_file_get_name(sync_file, buf, sizeof(buf)),
		   sync_status_str(dma_fence_get_status(sync_file->fence)));

	if (dma_fence_is_array(sync_file->fence)) {
		struct dma_fence_array *array = to_dma_fence_array(sync_file->fence);

		for (i = 0; i < array->num_fences; ++i)
			sync_print_fence(s, array->fences[i], true);
	} else {
		sync_print_fence(s, sync_file->fence, true);
	}
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

	spin_lock_irq(&sync_file_list_lock);
	list_for_each(pos, &sync_file_list_head) {
		struct sync_file *sync_file =
			container_of(pos, struct sync_file, sync_file_list);

		sync_print_sync_file(s, sync_file);
		seq_putc(s, '\n');
	}
	spin_unlock_irq(&sync_file_list_lock);
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

#define DUMP_CHUNK 256
static char sync_dump_buf[64 * 1024];
void sync_dump(void)
{
	struct seq_file s = {
		.buf = sync_dump_buf,
		.size = sizeof(sync_dump_buf) - 1,
	};
	int i;

	sync_info_debugfs_show(&s, NULL);

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
