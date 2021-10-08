/*
 * Rockchip isp1 driver
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _RKISP_PATH_VIDEO_H
#define _RKISP_PATH_VIDEO_H

#include "common.h"
#include "capture_v1x.h"
#include "capture_v2x.h"
#include "isp_ispp.h"

#define RDBK_MAX		3
#define RDBK_L			0
#define RDBK_M			1
#define RDBK_S			2

struct rkisp_stream;

/*
 * @fourcc: pixel format
 * @mbus_code: pixel format over bus
 * @fmt_type: helper filed for pixel format
 * @bpp: bits per pixel
 * @bayer_pat: bayer patten type
 * @cplanes: number of colour planes
 * @mplanes: number of stored memory planes
 * @uv_swap: if cb cr swaped, for yuv
 * @write_format: defines how YCbCr self picture data is written to memory
 * @input_format: defines sp input format
 * @output_format: defines sp output format
 */
struct capture_fmt {
	u32 fourcc;
	u32 mbus_code;
	u8 fmt_type;
	u8 cplanes;
	u8 mplanes;
	u8 uv_swap;
	u32 write_format;
	u32 output_format;
	u8 bpp[VIDEO_MAX_PLANES];
};

enum rkisp_sp_inp {
	RKISP_SP_INP_ISP,
	RKISP_SP_INP_DMA_SP,
	RKISP_SP_INP_MAX
};

enum rkisp_field {
	RKISP_FIELD_ODD,
	RKISP_FIELD_EVEN,
	RKISP_FIELD_INVAL,
};

struct rkisp_stream_sp {
	int y_stride;
	int vir_offs;
	enum rkisp_sp_inp input_sel;
	enum rkisp_field field;
	enum rkisp_field field_rec;
};

struct rkisp_stream_mp {
	bool raw_enable;
};

struct rkisp_stream_dmatx {
	u8 pre_stop;
	u8 is_config;
};

struct rkisp_stream_dmarx {
	int y_stride;
};

/* Different config between selfpath and mainpath */
struct stream_config {
	const struct capture_fmt *fmts;
	int fmt_size;
	/* constrains */
	const int max_rsz_width;
	const int max_rsz_height;
	const int min_rsz_width;
	const int min_rsz_height;
	const int frame_end_id;
	/* registers */
	struct {
		u32 ctrl;
		u32 ctrl_shd;
		u32 scale_hy;
		u32 scale_hcr;
		u32 scale_hcb;
		u32 scale_vy;
		u32 scale_vc;
		u32 scale_lut;
		u32 scale_lut_addr;
		u32 scale_hy_shd;
		u32 scale_hcr_shd;
		u32 scale_hcb_shd;
		u32 scale_vy_shd;
		u32 scale_vc_shd;
		u32 phase_hy;
		u32 phase_hc;
		u32 phase_vy;
		u32 phase_vc;
		u32 phase_hy_shd;
		u32 phase_hc_shd;
		u32 phase_vy_shd;
		u32 phase_vc_shd;
	} rsz;
	struct {
		u32 ctrl;
		u32 yuvmode_mask;
		u32 rawmode_mask;
		u32 h_offset;
		u32 v_offset;
		u32 h_size;
		u32 v_size;
	} dual_crop;
	struct {
		u32 y_size_init;
		u32 cb_size_init;
		u32 cr_size_init;
		u32 y_base_ad_init;
		u32 cb_base_ad_init;
		u32 cr_base_ad_init;
		u32 y_offs_cnt_init;
		u32 cb_offs_cnt_init;
		u32 cr_offs_cnt_init;
		u32 y_base_ad_shd;
		u32 length;
	} mi;
	struct {
		u32 ctrl;
		u32 pic_size;
		u32 pic_offs;
	} dma;
};

/* Different reg ops between selfpath and mainpath */
struct streams_ops {
	int (*config_mi)(struct rkisp_stream *stream);
	void (*stop_mi)(struct rkisp_stream *stream);
	void (*enable_mi)(struct rkisp_stream *stream);
	void (*disable_mi)(struct rkisp_stream *stream);
	void (*set_data_path)(void __iomem *base);
	bool (*is_stream_stopped)(void __iomem *base);
	void (*update_mi)(struct rkisp_stream *stream);
	int (*frame_end)(struct rkisp_stream *stream);
};

/*
 * struct rkisp_stream - ISP capture video device
 *
 * @id: stream video identify
 * @interlaced: selfpath interlaced flag
 * @out_isp_fmt: output isp format
 * @out_fmt: output buffer size
 * @dcrop: coordinates of dual-crop
 *
 * @vbq_lock: lock to protect buf_queue
 * @buf_queue: queued buffer list
 *
 * rkisp use shadowsock registers, so it need two buffer at a time
 * @curr_buf: the buffer used for current frame
 * @next_buf: the buffer used for next frame
 * @linked: stream link to isp
 * @done: wait frame end event queue
 * @burst: burst length for Y and CB/CR
 * @sequence: damtx video frame sequence
 */
struct rkisp_stream {
	unsigned int id;
	unsigned interlaced:1;
	struct rkisp_device *ispdev;
	struct rkisp_vdev_node vnode;
	struct capture_fmt out_isp_fmt;
	struct v4l2_pix_format_mplane out_fmt;
	struct v4l2_rect dcrop;
	struct streams_ops *ops;
	struct stream_config *config;
	spinlock_t vbq_lock;
	struct list_head buf_queue;
	struct rkisp_buffer *curr_buf;
	struct rkisp_buffer *next_buf;
	struct mutex apilock;
	bool streaming;
	bool stopping;
	bool frame_end;
	bool linked;
	bool start_stream;
	wait_queue_head_t done;
	unsigned int burst;
	atomic_t sequence;
	struct frame_debug_info dbg;
	u32 memory;
	union {
		struct rkisp_stream_sp sp;
		struct rkisp_stream_mp mp;
		struct rkisp_stream_dmarx dmarx;
		struct rkisp_stream_dmatx dmatx;
	} u;
};

struct rkisp_capture_device {
	struct rkisp_device *ispdev;
	struct rkisp_stream stream[RKISP_MAX_STREAM];
	struct rkisp_buffer *rdbk_buf[RDBK_MAX];
	atomic_t refcnt;
	u32 wait_line;
	bool is_done_early;
};

extern struct stream_config rkisp_mp_stream_config;
extern struct stream_config rkisp_sp_stream_config;

void rkisp_unregister_stream_vdev(struct rkisp_stream *stream);
int rkisp_register_stream_vdev(struct rkisp_stream *stream);
void rkisp_unregister_stream_vdevs(struct rkisp_device *dev);
int rkisp_register_stream_vdevs(struct rkisp_device *dev);
void rkisp_mi_isr(u32 mis_val, struct rkisp_device *dev);
void rkisp_set_stream_def_fmt(struct rkisp_device *dev, u32 id,
			      u32 width, u32 height, u32 pixelformat);
int rkisp_fcc_xysubs(u32 fcc, u32 *xsubs, u32 *ysubs);
int rkisp_mbus_code_xysubs(u32 code, u32 *xsubs, u32 *ysubs);
int rkisp_fh_open(struct file *filp);
int rkisp_fop_release(struct file *file);
#endif /* _RKISP_PATH_VIDEO_H */
