/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_BRIDGE_H
#define _RKISP_BRIDGE_H

#include "linux/platform_device.h"
#include "isp_ispp.h"

#define BRIDGE_DEV_NAME DRIVER_NAME "-bridge-ispp"
#define BRIDGE_BUF_MAX	(RKISP_ISPP_BUF_MAX + 1)

struct rkisp_bridge_device;

struct rkisp_bridge_ops {
	int (*config)(struct rkisp_bridge_device *dev);
	void (*disable)(void __iomem *base);
	bool (*is_stopped)(void __iomem *base);
};

struct rkisp_bridge_config {
	const int frame_end_id;
	u32 offset;
	struct {
		u32 y0_base;
		u32 uv0_base;
		u32 y1_base;
		u32 uv1_base;
		u32 g0_base;
		u32 g1_base;

		u32 y0_base_shd;
		u32 uv0_base_shd;
		u32 g0_base_shd;
	} reg;
};

struct rkisp_bridge_buf {
	struct rkisp_ispp_buf dbufs;
	struct rkisp_dummy_buffer dummy[GROUP_BUF_MAX];
};

struct rkisp_bridge_device {
	struct rkisp_device *ispdev;
	struct v4l2_subdev sd;
	struct v4l2_rect crop;
	struct media_pad pad;
	wait_queue_head_t done;
	spinlock_t buf_lock;
	struct list_head list;
	struct rkisp_bridge_buf bufs[BRIDGE_BUF_MAX];
	struct rkisp_ispp_buf *cur_buf;
	struct rkisp_ispp_buf *nxt_buf;
	struct rkisp_bridge_ops *ops;
	struct rkisp_bridge_config *cfg;
	u8 work_mode;
	u8 buf_num;
	bool pingpong;
	bool stopping;
	bool linked;
	bool en;
};

int rkisp_register_bridge_subdev(struct rkisp_device *dev,
				 struct v4l2_device *v4l2_dev);
void rkisp_unregister_bridge_subdev(struct rkisp_device *dev);
void rkisp_bridge_isr(u32 *mis_val, struct rkisp_device *dev);
void rkisp_get_bridge_sd(struct platform_device *dev,
			 struct v4l2_subdev **sd);
#endif
