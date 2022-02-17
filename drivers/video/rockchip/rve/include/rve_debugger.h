/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author:
 *	Cerf Yu <cerf.yu@rock-chips.com>
 *	Huang Lee <Putin.li@rock-chips.com>
 */

#ifndef _RVE_DEBUGGER_H_
#define _RVE_DEBUGGER_H_

#ifdef CONFIG_ROCKCHIP_RVE_DEBUGGER

extern int RVE_DEBUG_MONITOR;
extern int RVE_DEBUG_REG;
extern int RVE_DEBUG_MSG;
extern int RVE_DEBUG_TIME;
extern int RVE_DEBUG_CHECK_MODE;
extern int RVE_DEBUG_NONUSE;
extern int RVE_DEBUG_INT_FLAG;

#define DEBUGGER_EN(name) (unlikely(RVE_DEBUG_##name ? true : false))

/*
 * struct rve_debugger - RVE debugger information
 *
 * This structure represents a debugger to be created by the rve driver
 * or core.
 */
struct rve_debugger {
#ifdef CONFIG_ROCKCHIP_RVE_DEBUG_FS
	/* Directory of debugfs file */
	struct dentry *debugfs_dir;
	struct list_head debugfs_entry_list;
	struct mutex debugfs_lock;
#endif

#ifdef CONFIG_ROCKCHIP_RVE_PROC_FS
	/* Directory of procfs file */
	struct proc_dir_entry *procfs_dir;
	struct list_head procfs_entry_list;
	struct mutex procfs_lock;
#endif
};

/*
 * struct rve_debugger_list - debugfs/procfs info list entry
 *
 * This structure represents a debugfs/procfs file to be created by the rve
 * driver or core.
 */
struct rve_debugger_list {
	/* File name */
	const char *name;
	/*
	 * Show callback. &seq_file->private will be set to the &struct
	 * rve_debugger_node corresponding to the instance of this info
	 * on a given &struct rve_debugger.
	 */
	int (*show)(struct seq_file *seq, void *data);
	/*
	 * Write callback. &seq_file->private will be set to the &struct
	 * rve_debugger_node corresponding to the instance of this info
	 * on a given &struct rve_debugger.
	 */
	ssize_t (*write)(struct file *file, const char __user *ubuf,
		size_t len, loff_t *offp);
	/* Procfs/Debugfs private data. */
	void *data;
};

/*
 * struct rve_debugger_node - Nodes for debugfs/procfs
 *
 * This structure represents each instance of procfs/debugfs created from the
 * template.
 */
struct rve_debugger_node {
	struct rve_debugger *debugger;

	/* template for this node. */
	const struct rve_debugger_list *info_ent;

	/* Each Procfs/Debugfs file. */
#ifdef CONFIG_ROCKCHIP_RVE_DEBUG_FS
	struct dentry *dent;
#endif

#ifdef CONFIG_ROCKCHIP_RVE_PROC_FS
	struct proc_dir_entry *pent;
#endif

	struct list_head list;
};

#ifdef CONFIG_ROCKCHIP_RVE_DEBUG_FS
int rve_debugfs_init(void);
int rve_debugfs_remove(void);
#else
static inline int rve_debugfs_remove(void)
{
	return 0;
}
static inline int rve_debugfs_init(void)
{
	return 0;
}
#endif /* #ifdef CONFIG_ROCKCHIP_RVE_DEBUG_FS */

#ifdef CONFIG_ROCKCHIP_RVE_PROC_FS
int rve_procfs_remove(void);
int rve_procfs_init(void);
#else
static inline int rve_procfs_remove(void)
{
	return 0;
}
static inline int rve_procfs_init(void)
{
	return 0;
}
#endif /* #ifdef CONFIG_ROCKCHIP_RVE_PROC_FS */

#else

#define DEBUGGER_EN(name) (unlikely(false))

#endif /* #ifdef CONFIG_ROCKCHIP_RVE_DEBUGGER */

#endif /* #ifndef _RVE_DEBUGGER_H_ */

