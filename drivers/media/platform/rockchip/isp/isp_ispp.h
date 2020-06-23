/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_ISPP_H
#define _RKISP_ISPP_H

#include <linux/platform_device.h>
#include <media/v4l2-subdev.h>

#define RKISP_ISPP_BUF_MAX 4
#define RKISP_ISPP_CMD_SET_MODE \
	_IOW('V', BASE_VIDIOC_PRIVATE + 0, struct rkisp_ispp_mode)

enum rkisp_ispp_buf_group {
	GROUP_BUF_PIC = 0,
	GROUP_BUF_GAIN,
	GROUP_BUF_MAX,
};

enum rkisp_ispp_work_mode {
	ISP_ISPP_FBC = BIT(0),
	ISP_ISPP_422 = BIT(1),
	ISP_ISPP_QUICK = BIT(2),
	ISP_ISPP_INIT_FAIL = BIT(7),
};

struct rkisp_ispp_mode {
	u8 work_mode;
	u8 buf_num;
};

struct rkisp_ispp_buf {
	struct list_head list;
	struct dma_buf *dbuf[GROUP_BUF_MAX];
	/* timestamp in ns */
	u64 frame_timestamp;
	u32 frame_id;
};

#if IS_BUILTIN(CONFIG_VIDEO_ROCKCHIP_ISP) && IS_BUILTIN(CONFIG_VIDEO_ROCKCHIP_ISPP)
int __init rkispp_plat_drv_init(void);
#endif

#if IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISP)
void rkisp_get_bridge_sd(struct platform_device *dev,
			 struct v4l2_subdev **sd);
#else
static inline void rkisp_get_bridge_sd(struct platform_device *dev,
				       struct v4l2_subdev **sd)
{
	*sd = NULL;
}
#endif

#endif
