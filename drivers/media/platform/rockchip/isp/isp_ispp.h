/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_ISPP_H
#define _RKISP_ISPP_H

#include <linux/platform_device.h>
#include <media/v4l2-subdev.h>

#define RKISPP_BUF_MAX 5
#define RKISP_ISPP_BUF_MAX (RKISPP_BUF_MAX + (2 * (DEV_MAX - 1)))

#define RKISP_ISPP_CMD_SET_MODE \
	_IOW('V', BASE_VIDIOC_PRIVATE + 0, struct rkisp_ispp_mode)
#define RKISP_ISPP_CMD_SET_FMT \
	_IOW('V', BASE_VIDIOC_PRIVATE + 1, struct max_input)

enum rkisp_ispp_dev {
	DEV_ID0 = 0,
	DEV_ID1,
	DEV_ID2,
	DEV_ID3,
	DEV_MAX,
};

enum rkisp_ispp_sw_reg {
	SW_REG_CACHE = 0xffffffff,
	SW_REG_CACHE_SYNC = 0xeeeeeeee,
};

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

struct frame_debug_info {
	u64 timestamp;
	u32 interval;
	u32 delay;
	u32 id;
};

struct max_input {
	u32 w;
	u32 h;
	u32 fps;
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
	u32 index;
	bool is_isp;
};

#if IS_BUILTIN(CONFIG_VIDEO_ROCKCHIP_ISP) && IS_BUILTIN(CONFIG_VIDEO_ROCKCHIP_ISPP)
int __init rkispp_hw_drv_init(void);
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
