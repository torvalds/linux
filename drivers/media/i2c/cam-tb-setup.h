/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Rockchip Electronics Co., Ltd. */

#ifndef CAM_TB_SETUP_H
#define CAM_TB_SETUP_H

#include <linux/types.h>

#ifdef CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP
u32 get_rk_cam_w(void);
u32 get_rk_cam_h(void);
u32 get_rk_cam_hdr(void);
u32 get_rk_cam_fps(void);
#else
static inline u32 get_rk_cam_w(void)
{
	return 0;
}
static inline u32 get_rk_cam_h(void)
{
	return 0;
}
static inline u32 get_rk_cam_hdr(void)
{
	return 0;
}
static inline u32 get_rk_cam_fps(void)
{
	return 0;
}
#endif

#endif
