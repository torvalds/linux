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

#ifndef _RKISP1_PATH_VIDEO_H
#define _RKISP1_PATH_VIDEO_H

#include "common.h"

struct rkisp1_stream;

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

enum rkisp1_sp_inp {
	RKISP1_SP_INP_ISP,
	RKISP1_SP_INP_DMA_SP,
	RKISP1_SP_INP_MAX
};

struct rkisp1_stream_sp {
	int y_stride;
	enum rkisp1_sp_inp input_sel;
};

struct rkisp1_stream_mp {
	bool raw_enable;
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
	} mi;
};

/* Different reg ops between selfpath and mainpath */
struct streams_ops {
	int (*config_mi)(struct rkisp1_stream *stream);
	void (*stop_mi)(struct rkisp1_stream *stream);
	void (*enable_mi)(struct rkisp1_stream *stream);
	void (*disable_mi)(struct rkisp1_stream *stream);
	void (*set_data_path)(void __iomem *base);
	bool (*is_stream_stopped)(void __iomem *base);
};

/*
 * struct rkisp1_stream - ISP capture video device
 *
 * @out_isp_fmt: output isp format
 * @out_fmt: output buffer size
 * @dcrop: coordinates of dual-crop
 *
 * @vbq_lock: lock to protect buf_queue
 * @buf_queue: queued buffer list
 * @dummy_buf: dummy space to store dropped data
 *
 * rkisp1 use shadowsock registers, so it need two buffer at a time
 * @curr_buf: the buffer used for current frame
 * @next_buf: the buffer used for next frame
 */
struct rkisp1_stream {
	unsigned id:1;
	struct rkisp1_device *ispdev;
	struct rkisp1_vdev_node vnode;
	struct capture_fmt out_isp_fmt;
	struct v4l2_pix_format_mplane out_fmt;
	struct v4l2_rect dcrop;
	struct streams_ops *ops;
	struct stream_config *config;
	spinlock_t vbq_lock;
	struct list_head buf_queue;
	struct rkisp1_dummy_buffer dummy_buf;
	struct rkisp1_buffer *curr_buf;
	struct rkisp1_buffer *next_buf;
	bool streaming;
	bool stopping;
	wait_queue_head_t done;
	union {
		struct rkisp1_stream_sp sp;
		struct rkisp1_stream_mp mp;
	} u;
};

void rkisp1_unregister_stream_vdevs(struct rkisp1_device *dev);
int rkisp1_register_stream_vdevs(struct rkisp1_device *dev);
void rkisp1_mi_isr(u32 mis_val, struct rkisp1_device *dev);
void rkisp1_stream_init(struct rkisp1_device *dev, u32 id);

#endif /* _RKISP1_PATH_VIDEO_H */
