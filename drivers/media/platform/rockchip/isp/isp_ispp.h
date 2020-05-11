/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_ISPP_H
#define _RKISP_ISPP_H

#include <linux/platform_device.h>
#include <media/v4l2-subdev.h>

#if IS_BUILTIN(CONFIG_VIDEO_ROCKCHIP_ISP) && IS_BUILTIN(CONFIG_VIDEO_ROCKCHIP_ISPP)
int __init rkispp_plat_drv_init(void);
#endif

#if defined(CONFIG_VIDEO_ROCKCHIP_ISP)
void rkisp_get_mpfbc_sd(struct platform_device *dev,
			struct v4l2_subdev **sd);
#else
static inline void rkisp_get_mpfbc_sd(struct platform_device *dev,
				      struct v4l2_subdev **sd)
{
	*sd = NULL;
}
#endif

#endif
