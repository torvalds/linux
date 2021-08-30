/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 * Author: Cerf Yu <cerf.yu@rock-chips.com>
 */

#ifndef _RGA_DEBUGGER_H_
#define _RGA_DEBUGGER_H_

#ifdef CONFIG_ROCKCHIP_RGA2_DEBUGGER
extern int RGA2_TEST_REG;
extern int RGA2_TEST_MSG;
extern int RGA2_TEST_TIME;
extern int RGA2_CHECK_MODE;
extern int RGA2_NONUSE;
extern int RGA2_INT_FLAG;

/*
 * struct rga_debugger - RGA debugger information
 *
 * This structure represents a debugger  to be created by the rga driver
 * or core.
 */
struct rga_debugger {
#ifdef CONFIG_ROCKCHIP_RGA2_DEBUG_FS
	/* Directory of debugfs file */
	struct dentry *debugfs_dir;
	struct list_head debugfs_entry_list;
	struct mutex debugfs_lock;
#endif

#ifdef CONFIG_ROCKCHIP_RGA2_PROC_FS
	/* Directory of procfs file */
	struct proc_dir_entry *procfs_dir;
	struct list_head procfs_entry_list;
	struct mutex procfs_lock;
#endif
};

/*
 * struct rga_debugger_list - debugfs/procfs info list entry
 *
 * This structure represents a debugfs/procfs file to be created by the rga
 * driver or core.
 */
struct rga_debugger_list {
	/* File name */
	const char *name;
	/*
	 * Show callback. &seq_file->private will be set to the &struct
	 * rga_debugger_node corresponding to the instance of this info on a given
	 * &struct rga_debugger.
	 */
	int (*show)(struct seq_file *seq, void *data);
	/*
	 * Write callback. &seq_file->private will be set to the &struct
	 * rga_debugger_node corresponding to the instance of this info on a given
	 * &struct rga_debugger.
	 */
	ssize_t (*write)(struct file *file, const char __user *ubuf, size_t len, loff_t *offp);
	/* Procfs/Debugfs private data. */
	void *data;
};

/*
 * struct rga_debugger_node - Nodes for debugfs/procfs
 *
 * This structure represents each instance of procfs/debugfs created from the
 * template.
 */
struct rga_debugger_node {
	struct rga_debugger *debugger;

	/* template for this node. */
	const struct rga_debugger_list *info_ent;

	/* Each Procfs/Debugfs file. */
#ifdef CONFIG_ROCKCHIP_RGA2_DEBUG_FS
	struct dentry *dent;
#endif

#ifdef CONFIG_ROCKCHIP_RGA2_PROC_FS
	struct proc_dir_entry *pent;
#endif

	struct list_head list;
};

#ifdef CONFIG_ROCKCHIP_RGA2_DEBUG_FS
int rga2_debugfs_init(void);
int rga2_debugfs_remove(void);
#else
static inline int rga2_debugfs_remove(void)
{
	return 0;
}
static inline int rga2_debugfs_init(void)
{
	return 0;
}
#endif /* #ifdef CONFIG_ROCKCHIP_RGA2_DEBUG_FS */

#ifdef CONFIG_ROCKCHIP_RGA2_PROC_FS
int rga2_procfs_remove(void);
int rga2_procfs_init(void);
#else
static inline int rga2_procfs_remove(void)
{
	return 0;
}
static inline int rga2_procfs_init(void)
{
	return 0;
}
#endif /* #ifdef CONFIG_ROCKCHIP_RGA2_PROC_FS */

#endif /* #ifdef CONFIG_ROCKCHIP_RGA2_DEBUGGER */

#endif /* #ifndef _RGA_DEBUGGER_H_ */

