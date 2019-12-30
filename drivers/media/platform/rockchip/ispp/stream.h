/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISPP_STREAM_H
#define _RKISPP_STREAM_H

#include "common.h"

struct rkispp_stream;

/*
 * STREAM_II: input image data
 * STREAM_MB: module bypass output, no scale
 * STREAM_S0: scale0 output
 * STREAM_S1: scale1 output
 * STREAM_S2: scale2 output
 */
enum rkispp_stream_id {
	STREAM_II = 0,
	STREAM_MB,
	STREAM_S0,
	STREAM_S1,
	STREAM_S2,
	STREAM_MAX
};

/*
 * fourcc: pixel format
 * cplanes: number of colour planes
 * mplanes: number of stored memory planes
 * wr_fmt: defines format for reg
 * bpp: bits per pixel
 */
struct capture_fmt {
	u32 fourcc;
	u8 cplanes;
	u8 mplanes;
	u8 wr_fmt;
	u8 bpp[VIDEO_MAX_PLANES];
};

/* Different config for stream */
struct stream_config {
	const struct capture_fmt *fmts;
	unsigned int fmt_size;
	u32 frame_end_id;
	/* registers */
	struct {
		u32 ctrl;
		u32 factor;
		u32 cur_y_base;
		u32 cur_uv_base;
		u32 cur_vir_stride;
		u32 cur_y_base_shd;
		u32 cur_uv_base_shd;
	} reg;
};

/* Different reg ops for stream */
struct streams_ops {
	int (*config)(struct rkispp_stream *stream);
	void (*update)(struct rkispp_stream *stream);
	void (*stop)(struct rkispp_stream *stream);
	int (*start)(struct rkispp_stream *stream);
	int (*is_stopped)(struct rkispp_stream *stream);
};

/* stream input/out flag */
enum stream_type {
	STREAM_INPUT,
	STREAM_OUTPUT,
};

/* tnr internal using buf */
struct in_tnr_buf {
	struct rkispp_dummy_buffer pic_cur;
	struct rkispp_dummy_buffer pic_next;
	struct rkispp_dummy_buffer gain_cur;
	struct rkispp_dummy_buffer gain_next;
	struct rkispp_dummy_buffer gain_kg;
	struct rkispp_dummy_buffer iir;
	struct rkispp_dummy_buffer pic_wr;
	struct rkispp_dummy_buffer gain_wr;
};

/* nr internal using buf */
struct in_nr_buf {
	struct rkispp_dummy_buffer pic_cur;
	struct rkispp_dummy_buffer gain_cur;
	struct rkispp_dummy_buffer pic_wr;
	struct rkispp_dummy_buffer tmp_yuv;
};

/* struct rkispp_stream - ISPP stream video device
 * id: stream video identify
 * buf_queue: queued buffer list
 * curr_buf: the buffer used for current frame
 * next_buf: the buffer used for next frame
 * dummy_buf: dummy space to store dropped data
 * done: wait frame end event queue
 * vbq_lock: lock to protect buf_queue
 * out_cap_fmt: the output of ispp
 * out_fmt: the output of v4l2 pix format
 * last_module: last function module
 * streaming: stream start flag
 * stopping: stream stop flag
 * linked: link enable flag
 */
struct rkispp_stream {
	enum rkispp_stream_id id;
	struct rkispp_device *isppdev;
	struct rkispp_vdev_node vnode;

	struct list_head buf_queue;
	struct rkispp_buffer *curr_buf;
	struct rkispp_buffer *next_buf;
	struct rkispp_dummy_buffer dummy_buf;
	wait_queue_head_t done;
	spinlock_t vbq_lock;

	enum stream_type type;
	struct streams_ops *ops;
	struct stream_config *config;
	struct capture_fmt out_cap_fmt;
	struct v4l2_pix_format_mplane out_fmt;
	u8 last_module;
	u8 streaming;
	u8 stopping;
	u8 linked;
};

/* rkispp stream device */
struct rkispp_stream_vdev {
	struct rkispp_stream stream[STREAM_MAX];
	struct in_tnr_buf tnr_buf;
	struct in_nr_buf nr_buf;
	atomic_t refcnt;
};

void rkispp_isr(u32 mis_val, struct rkispp_device *dev);
void rkispp_unregister_stream_vdevs(struct rkispp_device *dev);
int rkispp_register_stream_vdevs(struct rkispp_device *dev);
#endif
