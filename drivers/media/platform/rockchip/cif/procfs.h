/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd. */

#ifndef _RKCIF_PROCFS_H
#define _RKCIF_PROCFS_H

#ifdef CONFIG_PROC_FS

int rkcif_proc_init(struct rkcif_device *dev);
void rkcif_proc_cleanup(struct rkcif_device *dev);

#else

static inline int rkcif_proc_init(struct rkisp_device *dev)
{
	return 0;
}
static inline void rkcif_proc_cleanup(struct rkisp_device *dev)
{

}

#endif

#endif
