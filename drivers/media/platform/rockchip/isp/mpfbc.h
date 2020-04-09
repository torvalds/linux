/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_mpfbc_H
#define _RKISP_mpfbc_H

#include "linux/platform_device.h"

#define MPFBC_DEV_NAME DRIVER_NAME "-mpfbc-subdev"

struct rkisp_mpfbc_device;

struct rkisp_dma_buf {
	struct dma_buf *dbuf;
	void *mem_priv;
};

struct rkisp_mpfbc_device {
	struct rkisp_device *ispdev;
	struct v4l2_subdev sd;
	struct v4l2_rect crop;
	struct media_pad pad;
	wait_queue_head_t done;
	struct rkisp_dma_buf *pic_cur;
	struct rkisp_dma_buf *pic_nxt;
	struct rkisp_dma_buf *gain_cur;
	struct rkisp_dma_buf *gain_nxt;
	u8 pingpong;
	u8 stopping;
	u8 en;
	bool linked;
};

int rkisp_register_mpfbc_subdev(struct rkisp_device *dev,
				struct v4l2_device *v4l2_dev);
void rkisp_unregister_mpfbc_subdev(struct rkisp_device *dev);
void rkisp_mpfbc_isr(u32 mis_val, struct rkisp_device *dev);
void rkisp_get_mpfbc_sd(struct platform_device *dev,
			struct v4l2_subdev **sd);
#endif
