/*
 * Copyright 2016-2017 Advanced Micro Devices, Inc.
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
#include "kfd_priv.h"

static struct dentry *debugfs_root;

static int kfd_debugfs_open(struct inode *inode, struct file *file)
{
	int (*show)(struct seq_file *, void *) = inode->i_private;

	return single_open(file, show, NULL);
}

static const struct file_operations kfd_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = kfd_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void kfd_debugfs_init(void)
{
	struct dentry *ent;

	debugfs_root = debugfs_create_dir("kfd", NULL);
	if (!debugfs_root || debugfs_root == ERR_PTR(-ENODEV)) {
		pr_warn("Failed to create kfd debugfs dir\n");
		return;
	}

	ent = debugfs_create_file("mqds", S_IFREG | 0444, debugfs_root,
				  kfd_debugfs_mqds_by_process,
				  &kfd_debugfs_fops);
	if (!ent)
		pr_warn("Failed to create mqds in kfd debugfs\n");

	ent = debugfs_create_file("hqds", S_IFREG | 0444, debugfs_root,
				  kfd_debugfs_hqds_by_device,
				  &kfd_debugfs_fops);
	if (!ent)
		pr_warn("Failed to create hqds in kfd debugfs\n");

	ent = debugfs_create_file("rls", S_IFREG | 0444, debugfs_root,
				  kfd_debugfs_rls_by_device,
				  &kfd_debugfs_fops);
	if (!ent)
		pr_warn("Failed to create rls in kfd debugfs\n");
}

void kfd_debugfs_fini(void)
{
	debugfs_remove_recursive(debugfs_root);
}
