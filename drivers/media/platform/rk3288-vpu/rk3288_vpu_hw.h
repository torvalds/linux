/*
 * Rockchip RK3288 VPU codec driver
 *
 * Copyright (C) 2014 Google, Inc.
 *	Tomasz Figa <tfiga@chromium.org>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef RK3288_VPU_HW_H_
#define RK3288_VPU_HW_H_

#include <media/videobuf2-core.h>

#define RK3288_HEADER_SIZE		1280
#define RK3288_HW_PARAMS_SIZE		5487
#define RK3288_RET_PARAMS_SIZE		488

struct rk3288_vpu_dev;
struct rk3288_vpu_ctx;
struct rk3288_vpu_buf;

struct rk3288_vpu_h264d_priv_tbl;

/**
 * enum rk3288_vpu_enc_fmt - source format ID for hardware registers.
 */
enum rk3288_vpu_enc_fmt {
	RK3288_VPU_ENC_FMT_YUV420P = 0,
	RK3288_VPU_ENC_FMT_YUV420SP = 1,
	RK3288_VPU_ENC_FMT_YUYV422 = 2,
	RK3288_VPU_ENC_FMT_UYVY422 = 3,
};

/**
 * struct rk3288_vp8e_reg_params - low level encoding parameters
 * TODO: Create abstract structures for more generic controls or just
 *       remove unused fields.
 */
struct rk3288_vp8e_reg_params {
	u32 unused_00[5];
	u32 hdr_len;
	u32 unused_18[8];
	u32 enc_ctrl;
	u32 unused_3c;
	u32 enc_ctrl0;
	u32 enc_ctrl1;
	u32 enc_ctrl2;
	u32 enc_ctrl3;
	u32 enc_ctrl5;
	u32 enc_ctrl4;
	u32 str_hdr_rem_msb;
	u32 str_hdr_rem_lsb;
	u32 unused_60;
	u32 mad_ctrl;
	u32 unused_68;
	u32 qp_val[8];
	u32 bool_enc;
	u32 vp8_ctrl0;
	u32 rlc_ctrl;
	u32 mb_ctrl;
	u32 unused_9c[14];
	u32 rgb_yuv_coeff[2];
	u32 rgb_mask_msb;
	u32 intra_area_ctrl;
	u32 cir_intra_ctrl;
	u32 unused_e8[2];
	u32 first_roi_area;
	u32 second_roi_area;
	u32 mvc_ctrl;
	u32 unused_fc;
	u32 intra_penalty[7];
	u32 unused_11c;
	u32 seg_qp[24];
	u32 dmv_4p_1p_penalty[32];
	u32 dmv_qpel_penalty[32];
	u32 vp8_ctrl1;
	u32 bit_cost_golden;
	u32 loop_flt_delta[2];
};

/**
 * struct rk3288_vpu_aux_buf - auxiliary DMA buffer for hardware data
 * @cpu:	CPU pointer to the buffer.
 * @dma:	DMA address of the buffer.
 * @size:	Size of the buffer.
 */
struct rk3288_vpu_aux_buf {
	void *cpu;
	dma_addr_t dma;
	size_t size;
};

/**
 * struct rk3288_vpu_vp8e_hw_ctx - Context private data specific to codec mode.
 * @ctrl_buf:		VP8 control buffer.
 * @ext_buf:		VP8 ext data buffer.
 * @mv_buf:		VP8 motion vector buffer.
 * @ref_rec_ptr:	Bit flag for swapping ref and rec buffers every frame.
 */
struct rk3288_vpu_vp8e_hw_ctx {
	struct rk3288_vpu_aux_buf ctrl_buf;
	struct rk3288_vpu_aux_buf ext_buf;
	struct rk3288_vpu_aux_buf mv_buf;
	u8 ref_rec_ptr:1;
};

/**
 * struct rk3288_vpu_vp8d_hw_ctx - Context private data of VP8 decoder.
 * @segment_map:	Segment map buffer.
 * @prob_tbl:		Probability table buffer.
 */
struct rk3288_vpu_vp8d_hw_ctx {
	struct rk3288_vpu_aux_buf segment_map;
	struct rk3288_vpu_aux_buf prob_tbl;
};

/**
 * struct rk3288_vpu_h264d_hw_ctx - Per context data specific to H264 decoding.
 * @priv_tbl:		Private auxiliary buffer for hardware.
 */
struct rk3288_vpu_h264d_hw_ctx {
	struct rk3288_vpu_aux_buf priv_tbl;
};

/**
 * struct rk3288_vpu_hw_ctx - Context private data of hardware code.
 * @codec_ops:		Set of operations associated with current codec mode.
 */
struct rk3288_vpu_hw_ctx {
	const struct rk3288_vpu_codec_ops *codec_ops;

	/* Specific for particular codec modes. */
	union {
		struct rk3288_vpu_vp8e_hw_ctx vp8e;
		struct rk3288_vpu_vp8d_hw_ctx vp8d;
		struct rk3288_vpu_h264d_hw_ctx h264d;
		/* Other modes will need different data. */
	};
};

int rk3288_vpu_hw_probe(struct rk3288_vpu_dev *vpu);
void rk3288_vpu_hw_remove(struct rk3288_vpu_dev *vpu);

int rk3288_vpu_init(struct rk3288_vpu_ctx *ctx);
void rk3288_vpu_deinit(struct rk3288_vpu_ctx *ctx);

void rk3288_vpu_run(struct rk3288_vpu_ctx *ctx);

/* Run ops for H264 decoder */
int rk3288_vpu_h264d_init(struct rk3288_vpu_ctx *ctx);
void rk3288_vpu_h264d_exit(struct rk3288_vpu_ctx *ctx);
void rk3288_vpu_h264d_run(struct rk3288_vpu_ctx *ctx);
void rk3288_vpu_power_on(struct rk3288_vpu_dev *vpu);

/* Run ops for VP8 decoder */
int rk3288_vpu_vp8d_init(struct rk3288_vpu_ctx *ctx);
void rk3288_vpu_vp8d_exit(struct rk3288_vpu_ctx *ctx);
void rk3288_vpu_vp8d_run(struct rk3288_vpu_ctx *ctx);

/* Run ops for VP8 encoder */
int rk3288_vpu_vp8e_init(struct rk3288_vpu_ctx *ctx);
void rk3288_vpu_vp8e_exit(struct rk3288_vpu_ctx *ctx);
void rk3288_vpu_vp8e_run(struct rk3288_vpu_ctx *ctx);
void rk3288_vpu_vp8e_done(struct rk3288_vpu_ctx *ctx,
			  enum vb2_buffer_state result);

void rk3288_vpu_vp8e_assemble_bitstream(struct rk3288_vpu_ctx *ctx,
					struct rk3288_vpu_buf *dst_buf);

#endif /* RK3288_VPU_HW_H_ */
