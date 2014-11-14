/*
 * Rockchip RK3288 VPU codec driver
 *
 * Copyright (C) 2014 Rockchip Electronics Co., Ltd.
 *	Alpha Lin <Alpha.Lin@rock-chips.com>
 *	Jeffy Chen <jeffy.chen@rock-chips.com>
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

#include "rk3288_vpu_common.h"

#include <linux/types.h>
#include <linux/sort.h>

#include "rk3288_vpu_regs.h"
#include "rk3288_vpu_hw.h"

/* Various parameters specific to VP8 encoder. */
#define VP8_CABAC_CTX_OFFSET			192
#define VP8_CABAC_CTX_SIZE			((55 + 96) << 3)

#define VP8_KEY_FRAME_HDR_SIZE			10
#define VP8_INTER_FRAME_HDR_SIZE		3

#define VP8_FRAME_TAG_KEY_FRAME_BIT		BIT(0)
#define VP8_FRAME_TAG_LENGTH_SHIFT		5
#define VP8_FRAME_TAG_LENGTH_MASK		(0x7ffff << 5)

/**
 * struct rk3288_vpu_vp8e_ctrl_buf - hardware control buffer layout
 * @ext_hdr_size:	Ext header size in bytes (written by hardware).
 * @dct_size:		DCT partition size (written by hardware).
 * @rsvd:		Reserved for hardware.
 */
struct rk3288_vpu_vp8e_ctrl_buf {
	u32 ext_hdr_size;
	u32 dct_size;
	u8 rsvd[1016];
};

/*
 * The hardware takes care only of ext hdr and dct partition. The software
 * must take care of frame header.
 *
 * Buffer layout as received from hardware:
 *   |<--gap-->|<--ext hdr-->|<-gap->|<---dct part---
 *   |<-------dct part offset------->|
 *
 * Required buffer layout:
 *   |<--hdr-->|<--ext hdr-->|<---dct part---
 */
void rk3288_vpu_vp8e_assemble_bitstream(struct rk3288_vpu_ctx *ctx,
					struct rk3288_vpu_buf *dst_buf)
{
	size_t ext_hdr_size = dst_buf->vp8e.ext_hdr_size;
	size_t dct_size = dst_buf->vp8e.dct_size;
	size_t hdr_size = dst_buf->vp8e.hdr_size;
	size_t dst_size;
	size_t tag_size;
	void *dst;
	u32 *tag;

	dst_size = vb2_plane_size(&dst_buf->b, 0);
	dst = vb2_plane_vaddr(&dst_buf->b, 0);
	tag = dst; /* To access frame tag words. */

	if (WARN_ON(hdr_size + ext_hdr_size + dct_size > dst_size))
		return;
	if (WARN_ON(dst_buf->vp8e.dct_offset + dct_size > dst_size))
		return;

	vpu_debug(1, "%s: hdr_size = %u, ext_hdr_size = %u, dct_size = %u\n",
			__func__, hdr_size, ext_hdr_size, dct_size);

	memmove(dst + hdr_size + ext_hdr_size,
		dst + dst_buf->vp8e.dct_offset, dct_size);
	memcpy(dst, dst_buf->vp8e.header, hdr_size);

	/* Patch frame tag at first 32-bit word of the frame. */
	if (dst_buf->b.v4l2_buf.flags & V4L2_BUF_FLAG_KEYFRAME) {
		tag_size = VP8_KEY_FRAME_HDR_SIZE;
		tag[0] &= ~VP8_FRAME_TAG_KEY_FRAME_BIT;
	} else {
		tag_size = VP8_INTER_FRAME_HDR_SIZE;
		tag[0] |= VP8_FRAME_TAG_KEY_FRAME_BIT;
	}

	tag[0] &= ~VP8_FRAME_TAG_LENGTH_MASK;
	tag[0] |= (hdr_size + ext_hdr_size - tag_size)
						<< VP8_FRAME_TAG_LENGTH_SHIFT;

	vb2_set_plane_payload(&dst_buf->b, 0,
				hdr_size + ext_hdr_size + dct_size);
}

static inline unsigned int ref_luma_size(unsigned int w, unsigned int h)
{
	return round_up(w, MB_DIM) * round_up(h, MB_DIM);
}

int rk3288_vpu_vp8e_init(struct rk3288_vpu_ctx *ctx)
{
	struct rk3288_vpu_dev *vpu = ctx->dev;
	size_t height = ctx->src_fmt.height;
	size_t width = ctx->src_fmt.width;
	size_t ref_buf_size;
	size_t mv_size;
	int ret;

	ret = rk3288_vpu_aux_buf_alloc(vpu, &ctx->hw.vp8e.ctrl_buf,
				sizeof(struct rk3288_vpu_vp8e_ctrl_buf));
	if (ret) {
		vpu_err("failed to allocate ctrl buffer\n");
		return ret;
	}

	mv_size = DIV_ROUND_UP(width, 16) * DIV_ROUND_UP(height, 16) / 4;
	ret = rk3288_vpu_aux_buf_alloc(vpu, &ctx->hw.vp8e.mv_buf, mv_size);
	if (ret) {
		vpu_err("failed to allocate MV buffer\n");
		goto err_ctrl_buf;
	}

	ref_buf_size = ref_luma_size(width, height) * 3 / 2;
	ret = rk3288_vpu_aux_buf_alloc(vpu, &ctx->hw.vp8e.ext_buf,
					2 * ref_buf_size);
	if (ret) {
		vpu_err("failed to allocate ext buffer\n");
		goto err_mv_buf;
	}

	return 0;

err_mv_buf:
	rk3288_vpu_aux_buf_free(vpu, &ctx->hw.vp8e.mv_buf);
err_ctrl_buf:
	rk3288_vpu_aux_buf_free(vpu, &ctx->hw.vp8e.ctrl_buf);

	return ret;
}

void rk3288_vpu_vp8e_exit(struct rk3288_vpu_ctx *ctx)
{
	struct rk3288_vpu_dev *vpu = ctx->dev;

	rk3288_vpu_aux_buf_free(vpu, &ctx->hw.vp8e.ext_buf);
	rk3288_vpu_aux_buf_free(vpu, &ctx->hw.vp8e.mv_buf);
	rk3288_vpu_aux_buf_free(vpu, &ctx->hw.vp8e.ctrl_buf);
}

static inline u32 enc_in_img_ctrl(struct rk3288_vpu_ctx *ctx)
{
	struct v4l2_pix_format_mplane *pix_fmt = &ctx->src_fmt;
	struct v4l2_rect *crop = &ctx->src_crop;
	unsigned bytes_per_line, overfill_r, overfill_b;

	/*
	 * The hardware needs only the value for luma plane, because
	 * values of other planes are calculated internally based on
	 * format setting.
	 */
	bytes_per_line = pix_fmt->plane_fmt[0].bytesperline;
	overfill_r = (pix_fmt->width - crop->width) / 4;
	overfill_b = pix_fmt->height - crop->height;

	return VEPU_REG_IN_IMG_CTRL_ROW_LEN(bytes_per_line)
			| VEPU_REG_IN_IMG_CTRL_OVRFLR_D4(overfill_r)
			| VEPU_REG_IN_IMG_CTRL_OVRFLB_D4(overfill_b)
			| VEPU_REG_IN_IMG_CTRL_FMT(ctx->vpu_src_fmt->enc_fmt);
}

static void rk3288_vpu_vp8e_set_buffers(struct rk3288_vpu_dev *vpu,
					struct rk3288_vpu_ctx *ctx)
{
	const struct rk3288_vp8e_reg_params *params = ctx->run.vp8e.reg_params;
	dma_addr_t ref_buf_dma, rec_buf_dma;
	dma_addr_t stream_dma;
	size_t rounded_size;
	dma_addr_t dst_dma;
	u32 start_offset;
	size_t dst_size;

	rounded_size = ref_luma_size(ctx->src_fmt.width,
						ctx->src_fmt.height);

	ref_buf_dma = rec_buf_dma = ctx->hw.vp8e.ext_buf.dma;
	if (ctx->hw.vp8e.ref_rec_ptr)
		ref_buf_dma += rounded_size * 3 / 2;
	else
		rec_buf_dma += rounded_size * 3 / 2;
	ctx->hw.vp8e.ref_rec_ptr ^= 1;

	dst_dma = vb2_dma_contig_plane_dma_addr(&ctx->run.dst->b, 0);
	dst_size = vb2_plane_size(&ctx->run.dst->b, 0);

	/*
	 * stream addr-->|
	 * align 64bits->|<-start offset->|
	 * |<---------header size-------->|<---dst buf---
	 */
	start_offset = (params->rlc_ctrl & VEPU_REG_RLC_CTRL_STR_OFFS_MASK)
					>> VEPU_REG_RLC_CTRL_STR_OFFS_SHIFT;
	stream_dma = dst_dma + params->hdr_len;

	/**
	 * Userspace will pass 8 bytes aligned size(round_down) to us,
	 * so we need to plus start offset to get real header size.
	 *
	 * |<-aligned size->|<-start offset->|
	 * |<----------header size---------->|
	 */
	ctx->run.dst->vp8e.hdr_size = params->hdr_len + (start_offset >> 3);

	if (params->enc_ctrl & VEPU_REG_ENC_CTRL_KEYFRAME_BIT)
		ctx->run.dst->b.v4l2_buf.flags |= V4L2_BUF_FLAG_KEYFRAME;
	else
		ctx->run.dst->b.v4l2_buf.flags &= ~V4L2_BUF_FLAG_KEYFRAME;

	/*
	 * We assume here that 1/10 of the buffer is enough for headers.
	 * DCT partition will be placed in remaining 9/10 of the buffer.
	 */
	ctx->run.dst->vp8e.dct_offset = round_up(dst_size / 10, 8);

	/* Destination buffer. */
	vepu_write_relaxed(vpu, stream_dma, VEPU_REG_ADDR_OUTPUT_STREAM);
	vepu_write_relaxed(vpu, dst_dma + ctx->run.dst->vp8e.dct_offset,
				VEPU_REG_ADDR_VP8_DCT_PART(0));
	vepu_write_relaxed(vpu, dst_size - ctx->run.dst->vp8e.dct_offset,
				VEPU_REG_STR_BUF_LIMIT);

	/* Auxilliary buffers. */
	vepu_write_relaxed(vpu, ctx->hw.vp8e.ctrl_buf.dma,
				VEPU_REG_ADDR_OUTPUT_CTRL);
	vepu_write_relaxed(vpu, ctx->hw.vp8e.mv_buf.dma,
				VEPU_REG_ADDR_MV_OUT);
	vepu_write_relaxed(vpu, ctx->run.priv_dst.dma,
				VEPU_REG_ADDR_VP8_PROB_CNT);
	vepu_write_relaxed(vpu, ctx->run.priv_src.dma + VP8_CABAC_CTX_OFFSET,
				VEPU_REG_ADDR_CABAC_TBL);
	vepu_write_relaxed(vpu, ctx->run.priv_src.dma
				+ VP8_CABAC_CTX_OFFSET + VP8_CABAC_CTX_SIZE,
				VEPU_REG_ADDR_VP8_SEG_MAP);

	/* Reference buffers. */
	vepu_write_relaxed(vpu, ref_buf_dma,
				VEPU_REG_ADDR_REF_LUMA);
	vepu_write_relaxed(vpu, ref_buf_dma + rounded_size,
				VEPU_REG_ADDR_REF_CHROMA);

	/* Reconstruction buffers. */
	vepu_write_relaxed(vpu, rec_buf_dma,
				VEPU_REG_ADDR_REC_LUMA);
	vepu_write_relaxed(vpu, rec_buf_dma + rounded_size,
				VEPU_REG_ADDR_REC_CHROMA);

	/* Source buffer. */
	vepu_write_relaxed(vpu, vb2_dma_contig_plane_dma_addr(&ctx->run.src->b,
				PLANE_Y), VEPU_REG_ADDR_IN_LUMA);
	vepu_write_relaxed(vpu, vb2_dma_contig_plane_dma_addr(&ctx->run.src->b,
				PLANE_CB), VEPU_REG_ADDR_IN_CB);
	vepu_write_relaxed(vpu, vb2_dma_contig_plane_dma_addr(&ctx->run.src->b,
				PLANE_CR), VEPU_REG_ADDR_IN_CR);

	/* Source parameters. */
	vepu_write_relaxed(vpu, enc_in_img_ctrl(ctx), VEPU_REG_IN_IMG_CTRL);
}

static void rk3288_vpu_vp8e_set_params(struct rk3288_vpu_dev *vpu,
				       struct rk3288_vpu_ctx *ctx)
{
	const struct rk3288_vp8e_reg_params *params = ctx->run.vp8e.reg_params;
	int i;

	vepu_write_relaxed(vpu, params->enc_ctrl0, VEPU_REG_ENC_CTRL0);
	vepu_write_relaxed(vpu, params->enc_ctrl1, VEPU_REG_ENC_CTRL1);
	vepu_write_relaxed(vpu, params->enc_ctrl2, VEPU_REG_ENC_CTRL2);
	vepu_write_relaxed(vpu, params->enc_ctrl3, VEPU_REG_ENC_CTRL3);
	vepu_write_relaxed(vpu, params->enc_ctrl5, VEPU_REG_ENC_CTRL5);
	vepu_write_relaxed(vpu, params->enc_ctrl4, VEPU_REG_ENC_CTRL4);
	vepu_write_relaxed(vpu, params->str_hdr_rem_msb,
				VEPU_REG_STR_HDR_REM_MSB);
	vepu_write_relaxed(vpu, params->str_hdr_rem_lsb,
				VEPU_REG_STR_HDR_REM_LSB);
	vepu_write_relaxed(vpu, params->mad_ctrl, VEPU_REG_MAD_CTRL);

	for (i = 0; i < ARRAY_SIZE(params->qp_val); ++i)
		vepu_write_relaxed(vpu, params->qp_val[i],
					VEPU_REG_VP8_QP_VAL(i));

	vepu_write_relaxed(vpu, params->bool_enc, VEPU_REG_VP8_BOOL_ENC);
	vepu_write_relaxed(vpu, params->vp8_ctrl0, VEPU_REG_VP8_CTRL0);
	vepu_write_relaxed(vpu, params->rlc_ctrl, VEPU_REG_RLC_CTRL);
	vepu_write_relaxed(vpu, params->mb_ctrl, VEPU_REG_MB_CTRL);

	for (i = 0; i < ARRAY_SIZE(params->rgb_yuv_coeff); ++i)
		vepu_write_relaxed(vpu, params->rgb_yuv_coeff[i],
					VEPU_REG_RGB_YUV_COEFF(i));

	vepu_write_relaxed(vpu, params->rgb_mask_msb,
				VEPU_REG_RGB_MASK_MSB);
	vepu_write_relaxed(vpu, params->intra_area_ctrl,
				VEPU_REG_INTRA_AREA_CTRL);
	vepu_write_relaxed(vpu, params->cir_intra_ctrl,
				VEPU_REG_CIR_INTRA_CTRL);
	vepu_write_relaxed(vpu, params->first_roi_area,
				VEPU_REG_FIRST_ROI_AREA);
	vepu_write_relaxed(vpu, params->second_roi_area,
				VEPU_REG_SECOND_ROI_AREA);
	vepu_write_relaxed(vpu, params->mvc_ctrl,
				VEPU_REG_MVC_CTRL);

	for (i = 0; i < ARRAY_SIZE(params->intra_penalty); ++i)
		vepu_write_relaxed(vpu, params->intra_penalty[i],
					VEPU_REG_VP8_INTRA_PENALTY(i));

	for (i = 0; i < ARRAY_SIZE(params->seg_qp); ++i)
		vepu_write_relaxed(vpu, params->seg_qp[i],
					VEPU_REG_VP8_SEG_QP(i));

	for (i = 0; i < ARRAY_SIZE(params->dmv_4p_1p_penalty); ++i)
		vepu_write_relaxed(vpu, params->dmv_4p_1p_penalty[i],
					VEPU_REG_DMV_4P_1P_PENALTY(i));

	for (i = 0; i < ARRAY_SIZE(params->dmv_qpel_penalty); ++i)
		vepu_write_relaxed(vpu, params->dmv_qpel_penalty[i],
					VEPU_REG_DMV_QPEL_PENALTY(i));

	vepu_write_relaxed(vpu, params->vp8_ctrl1, VEPU_REG_VP8_CTRL1);
	vepu_write_relaxed(vpu, params->bit_cost_golden,
				VEPU_REG_VP8_BIT_COST_GOLDEN);

	for (i = 0; i < ARRAY_SIZE(params->loop_flt_delta); ++i)
		vepu_write_relaxed(vpu, params->loop_flt_delta[i],
					VEPU_REG_VP8_LOOP_FLT_DELTA(i));
}

void rk3288_vpu_vp8e_run(struct rk3288_vpu_ctx *ctx)
{
	struct rk3288_vpu_dev *vpu = ctx->dev;
	u32 reg;

	/* The hardware expects the control buffer to be zeroed. */
	memset(ctx->hw.vp8e.ctrl_buf.cpu, 0,
		sizeof(struct rk3288_vpu_vp8e_ctrl_buf));

	/*
	 * Program the hardware.
	 */
	rk3288_vpu_power_on(vpu);

	vepu_write_relaxed(vpu, VEPU_REG_ENC_CTRL_ENC_MODE_VP8,
				VEPU_REG_ENC_CTRL);

	rk3288_vpu_vp8e_set_params(vpu, ctx);
	rk3288_vpu_vp8e_set_buffers(vpu, ctx);

	/* Make sure that all registers are written at this point. */
	wmb();

	/* Set the watchdog. */
	schedule_delayed_work(&vpu->watchdog_work, msecs_to_jiffies(2000));

	/* Start the hardware. */
	reg = VEPU_REG_AXI_CTRL_OUTPUT_SWAP16
		| VEPU_REG_AXI_CTRL_INPUT_SWAP16
		| VEPU_REG_AXI_CTRL_BURST_LEN(16)
		| VEPU_REG_AXI_CTRL_GATE_BIT
		| VEPU_REG_AXI_CTRL_OUTPUT_SWAP32
		| VEPU_REG_AXI_CTRL_INPUT_SWAP32
		| VEPU_REG_AXI_CTRL_OUTPUT_SWAP8
		| VEPU_REG_AXI_CTRL_INPUT_SWAP8;
	vepu_write(vpu, reg, VEPU_REG_AXI_CTRL);

	vepu_write(vpu, 0, VEPU_REG_INTERRUPT);

	reg = VEPU_REG_ENC_CTRL_NAL_MODE_BIT
		| VEPU_REG_ENC_CTRL_WIDTH(MB_WIDTH(ctx->src_fmt.width))
		| VEPU_REG_ENC_CTRL_HEIGHT(MB_HEIGHT(ctx->src_fmt.height))
		| VEPU_REG_ENC_CTRL_ENC_MODE_VP8
		| VEPU_REG_ENC_CTRL_EN_BIT;

	if (ctx->run.dst->b.v4l2_buf.flags & V4L2_BUF_FLAG_KEYFRAME)
		reg |= VEPU_REG_ENC_CTRL_KEYFRAME_BIT;

	vepu_write(vpu, reg, VEPU_REG_ENC_CTRL);
}

void rk3288_vpu_vp8e_done(struct rk3288_vpu_ctx *ctx,
			  enum vb2_buffer_state result)
{
	struct rk3288_vpu_vp8e_ctrl_buf *ctrl_buf = ctx->hw.vp8e.ctrl_buf.cpu;

	/* Read length information of this run from utility buffer. */
	ctx->run.dst->vp8e.ext_hdr_size = ctrl_buf->ext_hdr_size;
	ctx->run.dst->vp8e.dct_size = ctrl_buf->dct_size;

	rk3288_vpu_run_done(ctx, result);
}
