/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd. */

#ifndef _RKISPP_PROCFS_H
#define _RKISPP_PROCFS_H

#ifdef CONFIG_PROC_FS
int rkispp_proc_init(struct rkispp_device *dev);
void rkispp_proc_cleanup(struct rkispp_device *dev);
#else
static inline int rkispp_proc_init(struct rkispp_device *dev)
{
	return 0;
}
static inline void rkispp_proc_cleanup(struct rkispp_device *dev)
{

}
#endif

#endif
