/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author:
 *	Cerf Yu <cerf.yu@rock-chips.com>
 *	Huang Lee <Putin.li@rock-chips.com>
 */

#ifndef _RGA_DEBUGGER_H_
#define _RGA_DEBUGGER_H_

#include "rga_drv.h"

#ifdef CONFIG_ROCKCHIP_RGA_DEBUGGER

extern int RGA_DEBUG_REG;
extern int RGA_DEBUG_MSG;
extern int RGA_DEBUG_TIME;
extern int RGA_DEBUG_INT_FLAG;
extern int RGA_DEBUG_MM;
extern int RGA_DEBUG_CHECK_MODE;
extern int RGA_DEBUG_NONUSE;
extern int RGA_DEBUG_DUMP_IMAGE;

#define DEBUGGER_EN(name) (unlikely(RGA_DEBUG_##name ? true : false))

/*
 * struct rga_debugger - RGA debugger information
 *
 * This structure represents a debugger to be created by the rga driver
 * or core.
 */
struct rga_debugger {
#ifdef CONFIG_ROCKCHIP_RGA_DEBUG_FS
	/* Directory of debugfs file */
	struct dentry *debugfs_dir;
	struct list_head debugfs_entry_list;
	struct mutex debugfs_lock;
#endif

#ifdef CONFIG_ROCKCHIP_RGA_PROC_FS
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
	 * rga_debugger_node corresponding to the instance of this info
	 * on a given &struct rga_debugger.
	 */
	int (*show)(struct seq_file *seq, void *data);
	/*
	 * Write callback. &seq_file->private will be set to the &struct
	 * rga_debugger_node corresponding to the instance of this info
	 * on a given &struct rga_debugger.
	 */
	ssize_t (*write)(struct file *file, const char __user *ubuf,
		size_t len, loff_t *offp);
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
#ifdef CONFIG_ROCKCHIP_RGA_DEBUG_FS
	struct dentry *dent;
#endif

#ifdef CONFIG_ROCKCHIP_RGA_PROC_FS
	struct proc_dir_entry *pent;
#endif

	struct list_head list;
};

#ifdef CONFIG_ROCKCHIP_RGA_DEBUG_FS
int rga_debugfs_init(void);
int rga_debugfs_remove(void);
#else
static inline int rga_debugfs_remove(void)
{
	return 0;
}
static inline int rga_debugfs_init(void)
{
	return 0;
}
#endif /* #ifdef CONFIG_ROCKCHIP_RGA_DEBUG_FS */

#ifdef CONFIG_ROCKCHIP_RGA_PROC_FS
int rga_procfs_remove(void);
int rga_procfs_init(void);
#else
static inline int rga_procfs_remove(void)
{
	return 0;
}
static inline int rga_procfs_init(void)
{
	return 0;
}
#endif /* #ifdef CONFIG_ROCKCHIP_RGA_PROC_FS */

#else

#define DEBUGGER_EN(name) (unlikely(false))

#endif /* #ifdef CONFIG_ROCKCHIP_RGA_DEBUGGER */

void rga_cmd_print_debug_info(struct rga_req *req);
void rga_request_task_debug_info(struct seq_file *m, struct rga_req *req);
void rga_dump_external_buffer(struct rga_external_buffer *buffer);
#ifdef CONFIG_NO_GKI
void rga_dump_job_image(struct rga_job *dump_job);
#else
static inline void rga_dump_job_image(struct rga_job *dump_job)
{
}
#endif /* #ifdef CONFIG_NO_GKI */

#endif /* #ifndef _RGA_DEBUGGER_H_ */

