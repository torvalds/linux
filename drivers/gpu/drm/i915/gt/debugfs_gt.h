/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef DEBUGFS_GT_H
#define DEBUGFS_GT_H

#include <linux/file.h>

struct intel_gt;

#define DEFINE_GT_DEBUGFS_ATTRIBUTE(__name)				\
	static int __name ## _open(struct inode *inode, struct file *file) \
{									\
	return single_open(file, __name ## _show, inode->i_private);	\
}									\
static const struct file_operations __name ## _fops = {			\
	.owner = THIS_MODULE,						\
	.open = __name ## _open,					\
	.read = seq_read,						\
	.llseek = seq_lseek,						\
	.release = single_release,					\
}

void debugfs_gt_register(struct intel_gt *gt);

struct debugfs_gt_file {
	const char *name;
	const struct file_operations *fops;
	bool (*eval)(const struct intel_gt *gt);
};

void debugfs_gt_register_files(struct intel_gt *gt,
			       struct dentry *root,
			       const struct debugfs_gt_file *files,
			       unsigned long count);

#endif /* DEBUGFS_GT_H */
