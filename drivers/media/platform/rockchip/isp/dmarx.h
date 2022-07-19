/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_DMARX_H
#define _RKISP_DMARX_H

#include "capture.h"
#include "common.h"
#include "isp_external.h"

#define DMA_VDEV_NAME DRIVER_NAME	"_dmapath"
#define DMARX0_VDEV_NAME DRIVER_NAME	"_rawrd0_m"
#define DMARX1_VDEV_NAME DRIVER_NAME	"_rawrd1_l"
#define DMARX2_VDEV_NAME DRIVER_NAME	"_rawrd2_s"

struct rkisp_dmarx_device;

enum {
	RKISP_STREAM_DMARX,
	RKISP_STREAM_RAWRD0,
	RKISP_STREAM_RAWRD1,
	RKISP_STREAM_RAWRD2,
	RKISP_MAX_DMARX_STREAM,
};

enum rkisp_dmarx_pad {
	RKISP_DMARX_PAD_SINK,
	RKISP_DMARX_PAD_SOURCE,
	RKISP_DMARX_PAD_MAX
};

enum rkisp_dmarx_trigger {
	T_AUTO = 0,
	T_MANUAL,
};

struct rkisp_dmarx_frame {
	u64 sof_timestamp;
	u64 timestamp;
	u32 id;
};

struct rkisp_rx_buf_pool {
	struct rkisp_buffer buf;
	struct rkisp_rx_buf *dbufs;
	void *mem_priv;
};

/*
 * struct rkisp_dmarx_device
 * trigger: read back mode
 * cur_frame: current frame id and timestamp in ns
 * pre_frame: previous frame id and timestamp in ns
 */
struct rkisp_dmarx_device {
	struct rkisp_device *ispdev;
	struct rkisp_stream stream[RKISP_MAX_DMARX_STREAM];
	enum rkisp_dmarx_trigger trigger;
	struct rkisp_dmarx_frame cur_frame;
	struct rkisp_dmarx_frame pre_frame;
};

void rkisp_dmarx_isr(u32 mis_val, struct rkisp_device *dev);
void rkisp2_rawrd_isr(u32 mis_val, struct rkisp_device *dev);
void rkisp_dmarx_set_fmt(struct rkisp_stream *stream,
			 struct v4l2_pix_format_mplane pixm);
void rkisp_rawrd_set_pic_size(struct rkisp_device *dev,
			      u32 width, u32 height);
void rkisp_dmarx_get_frame(struct rkisp_device *dev, u32 *id,
			   u64 *sof_timestamp, u64 *timestamp,
			   bool sync);
void rkisp_unregister_dmarx_vdev(struct rkisp_device *dev);
int rkisp_register_dmarx_vdev(struct rkisp_device *dev);
#endif /* _RKISP_DMARX_H */
