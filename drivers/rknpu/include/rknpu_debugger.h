/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#ifndef __LINUX_RKNPU_DEBUGGER_H_
#define __LINUX_RKNPU_DEBUGGER_H_

/*
 * struct rknpu_debugger - rknpu debugger information
 *
 * This structure represents a debugger to be created by the rknpu driver
 * or core.
 */
struct rknpu_debugger {
#ifdef CONFIG_ROCKCHIP_RKNPU_DEBUG_FS
	/* Directory of debugfs file */
	struct dentry *debugfs_dir;
	struct list_head debugfs_entry_list;
	struct mutex debugfs_lock;
#endif
#ifdef CONFIG_ROCKCHIP_RKNPU_PROC_FS
	/* Directory of procfs file */
	struct proc_dir_entry *procfs_dir;
	struct list_head procfs_entry_list;
	struct mutex procfs_lock;
#endif
};

/*
 * struct rknpu_debugger_list - debugfs/procfs info list entry
 *
 * This structure represents a debugfs/procfs file to be created by the npu
 * driver or core.
 */
struct rknpu_debugger_list {
	/* File name */
	const char *name;
	/*
	 * Show callback. &seq_file->private will be set to the &struct
	 * rknpu_debugger_node corresponding to the instance of this info
	 * on a given &struct rknpu_debugger.
	 */
	int (*show)(struct seq_file *seq, void *data);
	/*
	 * Write callback. &seq_file->private will be set to the &struct
	 * rknpu_debugger_node corresponding to the instance of this info
	 * on a given &struct rknpu_debugger.
	 */
	ssize_t (*write)(struct file *file, const char __user *ubuf, size_t len,
			 loff_t *offp);
	/* Procfs/Debugfs private data. */
	void *data;
};

/*
 * struct rknpu_debugger_node - Nodes for debugfs/procfs
 *
 * This structure represents each instance of procfs/debugfs created from the
 * template.
 */
struct rknpu_debugger_node {
	struct rknpu_debugger *debugger;

	/* template for this node. */
	const struct rknpu_debugger_list *info_ent;

	/* Each Procfs/Debugfs file. */
#ifdef CONFIG_ROCKCHIP_RKNPU_DEBUG_FS
	struct dentry *dent;
#endif

#ifdef CONFIG_ROCKCHIP_RKNPU_PROC_FS
	struct proc_dir_entry *pent;
#endif

	struct list_head list;
};

int rknpu_debugger_init(struct rknpu_device *rknpu_dev);
int rknpu_debugger_remove(struct rknpu_device *rknpu_dev);

#endif /* __LINUX_RKNPU_FENCE_H_ */
