/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISPP_STREAM_H
#define _RKISPP_STREAM_H

#include "common.h"
#include "params.h"

struct rkispp_stream;

/*
 * STREAM_II: input image data
 * STREAM_MB: module bypass output, no scale
 * STREAM_S0: scale0 output
 * STREAM_S1: scale1 output
 * STREAM_S2: scale2 output
 * STREAM_VIR: virtual output for debug
 */
enum rkispp_stream_id {
	STREAM_II = 0,
	STREAM_MB,
	STREAM_VIR,
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

struct in_tnr_buf {
	struct rkispp_dummy_buffer iir;
	struct rkispp_dummy_buffer gain_kg;
	struct rkispp_dummy_buffer wr[RKISPP_BUF_MAX][GROUP_BUF_MAX];
};

struct in_nr_buf {
	struct rkispp_dummy_buffer tmp_yuv;
	struct rkispp_dummy_buffer wr[RKISPP_BUF_MAX];
};

struct tnr_module {
	struct in_tnr_buf buf;
	struct list_head list_rd;
	struct list_head list_wr;
	struct list_head list_rpt;
	spinlock_t buf_lock;
	struct rkisp_ispp_buf *cur_rd;
	struct rkisp_ispp_buf *nxt_rd;
	struct rkisp_ispp_buf *cur_wr;
	struct rkisp_ispp_reg *reg_buf;
	struct frame_debug_info dbg;
	u32 uv_offset;
	bool is_end;
	bool is_3to1;
	bool is_but_init;
	bool is_trigger;
};

struct nr_module {
	struct in_nr_buf buf;
	struct list_head list_rd;
	struct list_head list_wr;
	spinlock_t buf_lock;
	struct rkisp_ispp_buf *cur_rd;
	struct rkispp_dummy_buffer *cur_wr;
	struct rkisp_ispp_reg *reg_buf;
	struct frame_debug_info dbg;
	u32 uv_offset;
	bool is_end;
};

struct fec_module {
	struct list_head list_rd;
	struct list_head list_wr;
	struct rkisp_ispp_buf *cur_rd;
	struct rkispp_dummy_buffer *dummy_cur_rd;
	struct rkisp_ispp_reg *reg_buf;
	struct frame_debug_info dbg;
	spinlock_t buf_lock;
	u32 uv_offset;
	bool is_end;
};

/* struct rkispp_stream - ISPP stream video device
 * id: stream video identify
 * buf_queue: queued buffer list
 * curr_buf: the buffer used for current frame
 * next_buf: the buffer used for next frame
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
	wait_queue_head_t done;
	spinlock_t vbq_lock;

	enum stream_type type;
	struct streams_ops *ops;
	struct stream_config *config;
	struct capture_fmt out_cap_fmt;
	struct v4l2_pix_format_mplane out_fmt;
	struct frame_debug_info dbg;

	u8 last_module;
	u8 conn_id;
	bool streaming;
	bool stopping;
	bool linked;
	bool is_upd;
	bool is_cfg;
	bool is_end;
	bool is_reg_withstream;
};

enum {
	MONITOR_OFF = 0,
	MONITOR_TNR = BIT(0),
	MONITOR_NR = BIT(1),
	MONITOR_FEC = BIT(2),
};

struct module_monitor {
	struct rkispp_device *dev;
	struct work_struct work;
	struct completion cmpl;
	u16 time;
	u8 module;
	bool is_err;
};

struct rkispp_monitor {
	struct module_monitor tnr;
	struct module_monitor nr;
	struct module_monitor fec;
	struct completion cmpl;
	spinlock_t lock;
	u8 monitoring_module;
	u8 restart_module;
	u8 retry;
	bool is_restart;
	bool is_en;
};


struct rkispp_stream_ops {
	int (*config_modules)(struct rkispp_device *dev);
	void (*destroy_buf)(struct rkispp_stream *stream);
	void (*fec_work_event)(struct rkispp_device *dev, void *buf_rd,
			       bool is_isr, bool is_quick);
	int (*start_isp)(struct rkispp_device *dev);
	void (*check_to_force_update)(struct rkispp_device *dev, u32 mis_val);
	void (*update_mi)(struct rkispp_stream *stream);
	enum hrtimer_restart (*rkispp_frame_done_early)(struct hrtimer *timer);
	void (*rkispp_module_work_event)(struct rkispp_device *dev,
					 void *buf_rd, void *buf_wr,
					 u32 module, bool is_isr);
};

struct rkispp_vir_cpy {
	struct work_struct work;
	struct completion cmpl;
	struct list_head queue;
	struct rkispp_stream *stream;
};

/* rkispp stream device */
struct rkispp_stream_vdev {
	struct rkispp_stream stream[STREAM_MAX];
	struct rkispp_isp_buf_pool pool[RKISPP_BUF_POOL_MAX];
	struct tnr_module tnr;
	struct nr_module nr;
	struct fec_module fec;
	struct frame_debug_info dbg;
	struct rkispp_monitor monitor;
	struct rkispp_stream_ops *stream_ops;
	struct rkispp_vir_cpy vir_cpy;
	struct rkisp_ispp_buf input[VIDEO_MAX_FRAME];
	struct hrtimer fec_qst;
	struct hrtimer frame_qst;
	atomic_t refcnt;
	u32 module_ens;
	u32 irq_ends;
	u32 wait_line;
	bool is_done_early;
};

int rkispp_get_tnrbuf_fd(struct rkispp_device *dev, struct rkispp_buf_idxfd *idxfd);
void rkispp_sendbuf_to_nr(struct rkispp_device *dev,
			  struct rkispp_tnr_inf *tnr_inf);
void rkispp_set_trigger_mode(struct rkispp_device *dev,
			     struct rkispp_trigger_mode *mode);
void rkispp_isr(u32 mis_val, struct rkispp_device *dev);
void rkispp_unregister_stream_vdevs(struct rkispp_device *dev);
int rkispp_register_stream_vdevs(struct rkispp_device *dev);
void *get_pool_buf(struct rkispp_device *dev, struct rkisp_ispp_buf *dbufs);
void *dbuf_to_dummy(struct dma_buf *dbuf, struct rkispp_dummy_buffer *pool, int num);
void *get_list_buf(struct list_head *list, bool is_isp_ispp);
void get_stream_buf(struct rkispp_stream *stream);
void secure_config_mb(struct rkispp_stream *stream);

#if IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISPP_VERSION_V10)
void rkispp_stream_init_ops_v10(struct rkispp_stream_vdev *stream_vdev);
void rkispp_params_init_ops_v10(struct rkispp_params_vdev *params_vdev);
#else
static inline void rkispp_stream_init_ops_v10(struct rkispp_stream_vdev *stream_vdev) {}
static inline void rkispp_params_init_ops_v10(struct rkispp_params_vdev *params_vdev) {}
#endif

#if IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISPP_VERSION_V20)
void rkispp_stream_init_ops_v20(struct rkispp_stream_vdev *stream_vdev);
void rkispp_params_init_ops_v20(struct rkispp_params_vdev *params_vdev);
#else
static inline void rkispp_stream_init_ops_v20(struct rkispp_stream_vdev *stream_vdev) {}
static inline void rkispp_params_init_ops_v20(struct rkispp_params_vdev *params_vdev) {}
#endif
int rkispp_frame_end(struct rkispp_stream *stream, u32 state);
void rkispp_start_3a_run(struct rkispp_device *dev);
#endif
