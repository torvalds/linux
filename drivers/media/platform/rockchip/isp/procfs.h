/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_PROCFS_H
#define _RKISP_PROCFS_H

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
