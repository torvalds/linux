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
#include <linux/sync_file.h>
#include "sw_sync.h"

#ifdef CONFIG_DEBUG_FS

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
	if (status == 0)
		return "signaled";

	if (status > 0)
		return "active";

	return "error";
}

static void sync_print_fence(struct seq_file *s, struct fence *fence, bool show)
{
	int status = 1;
	struct sync_timeline *parent = fence_parent(fence);

	if (fence_is_signaled_locked(fence))
		status = fence->status;

	seq_printf(s, "  %s%sfence %s",
		   show ? parent->name : "",
		   show ? "_" : "",
		   sync_status_str(status));

	if (status <= 0) {
		struct timespec64 ts64 =
			ktime_to_timespec64(fence->timestamp);

		seq_printf(s, "@%lld.%09ld", (s64)ts64.tv_sec, ts64.tv_nsec);
	}

	if ((!fence || fence->ops->timeline_value_str) &&
		fence->ops->fence_value_str) {
		char value[64];
		bool success;

		fence->ops->fence_value_str(fence, value, sizeof(value));
		success = strlen(value);

		if (success)
			seq_printf(s, ": %s", value);

		if (success && fence) {
			fence->ops->timeline_value_str(fence, value,
						       sizeof(value));

			if (strlen(value))
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
		struct fence *fence =
			container_of(pos, struct fence, child_list);
		sync_print_fence(s, fence, false);
	}
	spin_unlock_irqrestore(&obj->child_list_lock, flags);
}

static void sync_print_sync_file(struct seq_file *s,
				  struct sync_file *sync_file)
{
	int i;

	seq_printf(s, "[%p] %s: %s\n", sync_file, sync_file->name,
		   sync_status_str(atomic_read(&sync_file->status)));

	for (i = 0; i < sync_file->num_fences; ++i)
		sync_print_fence(s, sync_file->cbs[i].fence, true);
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

	spin_lock_irqsave(&sync_file_list_lock, flags);
	list_for_each(pos, &sync_file_list_head) {
		struct sync_file *sync_file =
			container_of(pos, struct sync_file, sync_file_list);

		sync_print_sync_file(s, sync_file);
		seq_puts(s, "\n");
	}
	spin_unlock_irqrestore(&sync_file_list_lock, flags);
	return 0;
}

static int sync_info_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, sync_debugfs_show, inode->i_private);
}

static const struct file_operations sync_info_debugfs_fops = {
	.open           = sync_info_debugfs_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

/*
 * *WARNING*
 *
 * improper use of this can result in deadlocking kernel drivers from userspace.
 */

/* opening sw_sync create a new sync obj */
static int sw_sync_debugfs_open(struct inode *inode, struct file *file)
{
	struct sw_sync_timeline *obj;
	char task_comm[TASK_COMM_LEN];

	get_task_comm(task_comm, current);

	obj = sw_sync_timeline_create(task_comm);
	if (!obj)
		return -ENOMEM;

	file->private_data = obj;

	return 0;
}

static int sw_sync_debugfs_release(struct inode *inode, struct file *file)
{
	struct sw_sync_timeline *obj = file->private_data;

	sync_timeline_destroy(&obj->obj);
	return 0;
}

static long sw_sync_ioctl_create_fence(struct sw_sync_timeline *obj,
				       unsigned long arg)
{
	int fd = get_unused_fd_flags(O_CLOEXEC);
	int err;
	struct fence *fence;
	struct sync_file *sync_file;
	struct sw_sync_create_fence_data data;

	if (fd < 0)
		return fd;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data))) {
		err = -EFAULT;
		goto err;
	}

	fence = sw_sync_pt_create(obj, data.value);
	if (!fence) {
		err = -ENOMEM;
		goto err;
	}

	sync_file = sync_file_create(fence);
	if (!sync_file) {
		fence_put(fence);
		err = -ENOMEM;
		goto err;
	}

	data.fence = fd;
	if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
		fput(sync_file->file);
		err = -EFAULT;
		goto err;
	}

	fd_install(fd, sync_file->file);

	return 0;

err:
	put_unused_fd(fd);
	return err;
}

static long sw_sync_ioctl_inc(struct sw_sync_timeline *obj, unsigned long arg)
{
	u32 value;

	if (copy_from_user(&value, (void __user *)arg, sizeof(value)))
		return -EFAULT;

	sw_sync_timeline_inc(obj, value);

	return 0;
}

static long sw_sync_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	struct sw_sync_timeline *obj = file->private_data;

	switch (cmd) {
	case SW_SYNC_IOC_CREATE_FENCE:
		return sw_sync_ioctl_create_fence(obj, arg);

	case SW_SYNC_IOC_INC:
		return sw_sync_ioctl_inc(obj, arg);

	default:
		return -ENOTTY;
	}
}

static const struct file_operations sw_sync_debugfs_fops = {
	.open           = sw_sync_debugfs_open,
	.release        = sw_sync_debugfs_release,
	.unlocked_ioctl = sw_sync_ioctl,
	.compat_ioctl = sw_sync_ioctl,
};

static __init int sync_debugfs_init(void)
{
	dbgfs = debugfs_create_dir("sync", NULL);

	debugfs_create_file("info", 0444, dbgfs, NULL, &sync_info_debugfs_fops);
	debugfs_create_file("sw_sync", 0644, dbgfs, NULL,
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
