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
#include <linux/module.h>
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
#include "sw_sync.h"

#ifdef CONFIG_DEBUG_FS

static struct dentry *dbgfs;

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

static void sync_print_pt(struct seq_file *s, struct fence *pt, bool fence)
{
	int status = 1;

	if (fence_is_signaled_locked(pt))
		status = pt->status;

	seq_printf(s, "  %s%spt %s",
		   fence && pt->ops->get_timeline_name ?
		   pt->ops->get_timeline_name(pt) : "",
		   fence ? "_" : "",
		   sync_status_str(status));

	if (status <= 0) {
		struct timespec64 ts64 =
			ktime_to_timespec64(pt->timestamp);

		seq_printf(s, "@%lld.%09ld", (s64)ts64.tv_sec, ts64.tv_nsec);
	}

	if ((!fence || pt->ops->timeline_value_str) &&
	    pt->ops->fence_value_str) {
		char value[64];
		bool success;

		pt->ops->fence_value_str(pt, value, sizeof(value));
		success = strlen(value);

		if (success)
			seq_printf(s, ": %s", value);

		if (success && fence) {
			pt->ops->timeline_value_str(pt, value, sizeof(value));

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
		struct sync_pt *pt =
			container_of(pos, struct sync_pt, child_list);
		sync_print_pt(s, &pt->base, false);
	}
	spin_unlock_irqrestore(&obj->child_list_lock, flags);
}

static void sync_print_fence(struct seq_file *s, struct sync_fence *fence)
{
	int i;

	seq_printf(s, "[%p] %s: %s\n", fence, fence->name,
		   sync_status_str(atomic_read(&fence->status)));

	for (i = 0; i < fence->num_fences; ++i)
		sync_print_pt(s, fence->cbs[i].sync_pt, true);
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
	struct sync_pt *pt;
	struct sync_fence *fence;
	struct sw_sync_create_fence_data data;

	if (fd < 0)
		return fd;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data))) {
		err = -EFAULT;
		goto err;
	}

	pt = sw_sync_pt_create(obj, data.value);
	if (!pt) {
		err = -ENOMEM;
		goto err;
	}

	data.name[sizeof(data.name) - 1] = '\0';
	fence = sync_fence_create(data.name, pt);
	if (!fence) {
		sync_pt_free(pt);
		err = -ENOMEM;
		goto err;
	}

	data.fence = fd;
	if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
		sync_fence_put(fence);
		err = -EFAULT;
		goto err;
	}

	sync_fence_install(fence, fd);

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

static __exit void sync_debugfs_exit(void)
{
	if (dbgfs)
		debugfs_remove_recursive(dbgfs);
}
module_exit(sync_debugfs_exit);

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
