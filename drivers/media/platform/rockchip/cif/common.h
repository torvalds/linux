/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021 Rockchip Electronics Co., Ltd. */

#ifndef _RKCIF_COMMON_H
#define _RKCIF_COMMON_H
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/media.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-mc.h>
#include "dev.h"

int rkcif_alloc_buffer(struct rkcif_device *dev,
		       struct rkcif_dummy_buffer *buf);
void rkcif_free_buffer(struct rkcif_device *dev,
			struct rkcif_dummy_buffer *buf);

int rkcif_alloc_common_dummy_buf(struct rkcif_device *dev, struct rkcif_dummy_buffer *buf);
void rkcif_free_common_dummy_buf(struct rkcif_device *dev, struct rkcif_dummy_buffer *buf);

#endif /* _RKCIF_COMMON_H */

