// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2016-2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/debugfs.h>
#include <linux/uaccess.h>

#include "kfd_priv.h"

static struct dentry *debugfs_root;
static struct dentry *debugfs_proc;
static struct list_head procs;

struct debugfs_proc_entry {
	struct list_head list;
	struct dentry *proc_dentry;
	pid_t pid;
};

#define MAX_DEBUGFS_FILENAME_LEN 32

static int kfd_debugfs_open(struct inode *inode, struct file *file)
{
	int (*show)(struct seq_file *, void *) = inode->i_private;

	return single_open(file, show, NULL);
}
static int kfd_debugfs_hang_hws_read(struct seq_file *m, void *data)
{
	seq_puts(m, "echo gpu_id > hang_hws\n");
	return 0;
}

static ssize_t kfd_debugfs_hang_hws_write(struct file *file,
	const char __user *user_buf, size_t size, loff_t *ppos)
{
	struct kfd_node *dev;
	char tmp[16];
	uint32_t gpu_id;
	int ret = -EINVAL;

	memset(tmp, 0, 16);
	if (size >= 16) {
		pr_err("Invalid input for gpu id.\n");
		goto out;
	}
	if (copy_from_user(tmp, user_buf, size)) {
		ret = -EFAULT;
		goto out;
	}
	if (kstrtoint(tmp, 10, &gpu_id)) {
		pr_err("Invalid input for gpu id.\n");
		goto out;
	}
	dev = kfd_device_by_id(gpu_id);
	if (dev) {
		kfd_debugfs_hang_hws(dev);
		ret = size;
	} else
		pr_err("Cannot find device %d.\n", gpu_id);

out:
	return ret;
}

static const struct file_operations kfd_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = kfd_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations kfd_debugfs_hang_hws_fops = {
	.owner = THIS_MODULE,
	.open = kfd_debugfs_open,
	.read = seq_read,
	.write = kfd_debugfs_hang_hws_write,
	.llseek = seq_lseek,
	.release = single_release,
};

void kfd_debugfs_init(void)
{
	debugfs_root = debugfs_create_dir("kfd", NULL);
	debugfs_proc = debugfs_create_dir("proc", debugfs_root);
	INIT_LIST_HEAD(&procs);

	debugfs_create_file("mqds", S_IFREG | 0444, debugfs_root,
			    kfd_debugfs_mqds_by_process, &kfd_debugfs_fops);
	debugfs_create_file("hqds", S_IFREG | 0444, debugfs_root,
			    kfd_debugfs_hqds_by_device, &kfd_debugfs_fops);
	debugfs_create_file("rls", S_IFREG | 0444, debugfs_root,
			    kfd_debugfs_rls_by_device, &kfd_debugfs_fops);
	debugfs_create_file("hang_hws", S_IFREG | 0200, debugfs_root,
			    kfd_debugfs_hang_hws_read, &kfd_debugfs_hang_hws_fops);
	debugfs_create_file("mem_limit", S_IFREG | 0200, debugfs_root,
			    kfd_debugfs_kfd_mem_limits, &kfd_debugfs_fops);
}

void kfd_debugfs_fini(void)
{
	debugfs_remove_recursive(debugfs_proc);
	debugfs_remove_recursive(debugfs_root);
}

static ssize_t kfd_debugfs_pasid_read(struct file *file, char __user *buf,
				      size_t count, loff_t *ppos)
{
	struct kfd_process_device *pdd = file_inode(file)->i_private;
	char tmp[32];
	int len;

	len = snprintf(tmp, sizeof(tmp), "%u\n", pdd->pasid);

	return simple_read_from_buffer(buf, count, ppos, tmp, len);
}

static const struct file_operations kfd_debugfs_pasid_fops = {
	.owner = THIS_MODULE,
	.read = kfd_debugfs_pasid_read,
};

void kfd_debugfs_add_process(struct kfd_process *p)
{
	int i;
	char name[MAX_DEBUGFS_FILENAME_LEN];
	struct debugfs_proc_entry *entry;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return;

	list_add(&entry->list, &procs);
	entry->pid = p->lead_thread->pid;
	snprintf(name, MAX_DEBUGFS_FILENAME_LEN, "%d",
		 (int)entry->pid);
	entry->proc_dentry = debugfs_create_dir(name, debugfs_proc);

	/* Create debugfs files for each GPU:
	 * - proc/<pid>/pasid_<gpuid>
	 */
	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];

		snprintf(name, MAX_DEBUGFS_FILENAME_LEN, "pasid_%u",
			 pdd->dev->id);
		debugfs_create_file((const char *)name, S_IFREG | 0444,
				    entry->proc_dentry, pdd,
				    &kfd_debugfs_pasid_fops);
	}
}

void kfd_debugfs_remove_process(struct kfd_process *p)
{
	struct debugfs_proc_entry *entry, *next;

	mutex_lock(&kfd_processes_mutex);
	list_for_each_entry_safe(entry, next, &procs, list) {
		if (entry->pid != p->lead_thread->pid)
			continue;

		debugfs_remove_recursive(entry->proc_dentry);
		list_del(&entry->list);
		kfree(entry);
	}
	mutex_unlock(&kfd_processes_mutex);
}
