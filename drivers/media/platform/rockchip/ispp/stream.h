/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISPP_STREAM_H
#define _RKISPP_STREAM_H

#include "common.h"

#define RKISPP_BUF_POOL_MAX (RKISP_ISPP_BUF_MAX + 1)
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
	int (*limit_check)(struct rkispp_stream *stream,
			   struct v4l2_pix_format_mplane *try_fmt);
};

/* stream input/out flag */
enum stream_type {
	STREAM_INPUT,
	STREAM_OUTPUT,
};

/* internal using buf */

struct rkispp_isp_buf_pool {
	struct rkisp_ispp_buf *dbufs;
	void *mem_priv[GROUP_BUF_MAX];
	dma_addr_t dma[GROUP_BUF_MAX];
};

struct in_tnr_buf {
	struct rkispp_dummy_buffer iir;
	struct rkispp_dummy_buffer gain_kg;
	struct rkispp_dummy_buffer wr[RKISP_ISPP_BUF_MAX][GROUP_BUF_MAX];
};

struct in_nr_buf {
	struct rkispp_dummy_buffer tmp_yuv;
	struct rkispp_dummy_buffer wr[RKISP_ISPP_BUF_MAX];
};

struct tnr_module {
	struct in_tnr_buf buf;
	struct list_head list_rd;
	struct list_head list_wr;
	spinlock_t buf_lock;
	struct rkisp_ispp_buf *cur_rd;
	struct rkisp_ispp_buf *nxt_rd;
	struct rkisp_ispp_buf *cur_wr;
	u32 uv_offset;
	bool is_end;
	bool is_3to1;
};

struct nr_module {
	struct in_nr_buf buf;
	struct list_head list_rd;
	struct list_head list_wr;
	spinlock_t buf_lock;
	struct rkisp_ispp_buf *cur_rd;
	struct rkispp_dummy_buffer *cur_wr;
	u32 uv_offset;
	bool is_end;
};

struct fec_module {
	struct list_head list_rd;
	struct rkispp_dummy_buffer *cur_rd;
	spinlock_t buf_lock;
	u32 uv_offset;
	bool is_end;
};

/* fec internal using buf */
struct in_fec_buf {
	struct rkispp_dummy_buffer mesh_xint;
	struct rkispp_dummy_buffer mesh_yint;
	struct rkispp_dummy_buffer mesh_xfra;
	struct rkispp_dummy_buffer mesh_yfra;
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
	bool streaming;
	bool stopping;
	bool linked;
	bool is_upd;
};

/* rkispp stream device */
struct rkispp_stream_vdev {
	struct rkispp_stream stream[STREAM_MAX];
	struct rkispp_isp_buf_pool pool[RKISPP_BUF_POOL_MAX];
	struct tnr_module tnr;
	struct nr_module nr;
	struct fec_module fec;
	struct in_fec_buf fec_buf;
	atomic_t refcnt;
	u32 module_ens;
	u32 irq_ends;
};

void rkispp_free_pool(struct rkispp_stream_vdev *vdev);
void rkispp_module_work_event(struct rkispp_device *dev,
			      void *buf_rd, void *buf_wr,
			      u32 module, bool is_isr);
void rkispp_isr(u32 mis_val, struct rkispp_device *dev);
void rkispp_unregister_stream_vdevs(struct rkispp_device *dev);
int rkispp_register_stream_vdevs(struct rkispp_device *dev);
#endif
