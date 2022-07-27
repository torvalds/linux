/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_PROCFS_H
#define _RKISP_PROCFS_H

enum {
	RKISP_PROCFS_DUMP_REG = BIT(0),
	RKISP_PROCFS_DUMP_MEM = BIT(1),

	RKISP_PROCFS_FIL_AIQ = BIT(8),
	RKISP_PROCFS_FIL_SW = BIT(9),
};

struct rkisp_procfs {
	struct proc_dir_entry *procfs;
	wait_queue_head_t fs_wait;
	wait_queue_head_t fe_wait;
	u32 mode;
	bool is_fs_wait;
	bool is_fe_wait;
};

#ifdef CONFIG_PROC_FS
int rkisp_proc_init(struct rkisp_device *dev);
void rkisp_proc_cleanup(struct rkisp_device *dev);
#else
static inline int rkisp_proc_init(struct rkisp_device *dev)
{
	return 0;
}
static inline void rkisp_proc_cleanup(struct rkisp_device *dev)
{

}
#endif

#endif
