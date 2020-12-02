/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_TB_HELPER_H
#define _RKISP_TB_HELPER_H

#include <linux/rkisp1-config.h>

enum rkisp_tb_state {
	RKISP_TB_RUN = 0,
	RKISP_TB_OK,
	RKISP_TB_NG
};

#ifdef CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP
void rkisp_tb_unprotect_clk(void);
void rkisp_tb_set_state(enum rkisp_tb_state result);
enum rkisp_tb_state rkisp_tb_get_state(void);
long rkisp_tb_shm_ioctl(struct rkisp_thunderboot_shmem *shmem);
#else
static inline void rkisp_tb_unprotect_clk(void) {}
static inline void rkisp_tb_set_state(enum rkisp_tb_state result) {}
static inline enum rkisp_tb_state rkisp_tb_get_state(void)
{
	return RKISP_TB_OK;
}
static inline long rkisp_tb_shm_ioctl(struct rkisp_thunderboot_shmem *shmem)
{
	return 0;
}
#endif

#endif
