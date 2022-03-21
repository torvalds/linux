/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_GT_DEBUGFS_H
#define INTEL_GT_DEBUGFS_H

#include <linux/file.h>

struct intel_gt;

#define __GT_DEBUGFS_ATTRIBUTE_FOPS(__name)				\
static const struct file_operations __name ## _fops = {			\
	.owner = THIS_MODULE,						\
	.open = __name ## _open,					\
	.read = seq_read,						\
	.llseek = seq_lseek,						\
	.release = single_release,					\
}

#define DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(__name)			\
static int __name ## _open(struct inode *inode, struct file *file)	\
{									\
	return single_open(file, __name ## _show, inode->i_private);	\
}									\
__GT_DEBUGFS_ATTRIBUTE_FOPS(__name)

#define DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE_WITH_SIZE(__name, __size_vf)		\
static int __name ## _open(struct inode *inode, struct file *file)		\
{										\
	return single_open_size(file, __name ## _show, inode->i_private,	\
			    __size_vf(inode->i_private));			\
}										\
__GT_DEBUGFS_ATTRIBUTE_FOPS(__name)

void intel_gt_debugfs_register(struct intel_gt *gt);

struct intel_gt_debugfs_file {
	const char *name;
	const struct file_operations *fops;
	bool (*eval)(void *data);
};

void intel_gt_debugfs_register_files(struct dentry *root,
				     const struct intel_gt_debugfs_file *files,
				     unsigned long count, void *data);

/* functions that need to be accessed by the upper level non-gt interfaces */
int intel_gt_debugfs_reset_show(struct intel_gt *gt, u64 *val);
void intel_gt_debugfs_reset_store(struct intel_gt *gt, u64 val);

#endif /* INTEL_GT_DEBUGFS_H */
