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

#include "rockchip_vpu_common.h"

#include <linux/types.h>
#include <linux/sort.h>

#include "rk3288_vpu_regs.h"
#include "rockchip_vpu_hw.h"

/* Various parameters specific to VP8 encoder. */
#define VP8_CABAC_CTX_OFFSET			192
#define VP8_CABAC_CTX_SIZE			((55 + 96) << 3)

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

static inline unsigned int ref_luma_size(unsigned int w, unsigned int h)
{
	return round_up(w, MB_DIM) * round_up(h, MB_DIM);
}

int rk3288_vpu_vp8e_init(struct rockchip_vpu_ctx *ctx)
{
	struct rockchip_vpu_dev *vpu = ctx->dev;
	size_t height = ctx->src_fmt.height;
	size_t width = ctx->src_fmt.width;
	size_t ref_buf_size;
	size_t mv_size;
	int ret;

	ret = rockchip_vpu_aux_buf_alloc(vpu, &ctx->hw.vp8e.ctrl_buf,
				sizeof(struct rk3288_vpu_vp8e_ctrl_buf));
	if (ret) {
		vpu_err("failed to allocate ctrl buffer\n");
		return ret;
	}

	mv_size = DIV_ROUND_UP(width, 16) * DIV_ROUND_UP(height, 16) / 4;
	ret = rockchip_vpu_aux_buf_alloc(vpu, &ctx->hw.vp8e.mv_buf, mv_size);
	if (ret) {
		vpu_err("failed to allocate MV buffer\n");
		goto err_ctrl_buf;
	}

	ref_buf_size = ref_luma_size(width, height) * 3 / 2;
	ret = rockchip_vpu_aux_buf_alloc(vpu, &ctx->hw.vp8e.ext_buf,
					2 * ref_buf_size);
	if (ret) {
		vpu_err("failed to allocate ext buffer\n");
		goto err_mv_buf;
	}

	return 0;

err_mv_buf:
	rockchip_vpu_aux_buf_free(vpu, &ctx->hw.vp8e.mv_buf);
err_ctrl_buf:
	rockchip_vpu_aux_buf_free(vpu, &ctx->hw.vp8e.ctrl_buf);

	return ret;
}

void rk3288_vpu_vp8e_exit(struct rockchip_vpu_ctx *ctx)
{
	struct rockchip_vpu_dev *vpu = ctx->dev;

	rockchip_vpu_aux_buf_free(vpu, &ctx->hw.vp8e.ext_buf);
	rockchip_vpu_aux_buf_free(vpu, &ctx->hw.vp8e.mv_buf);
	rockchip_vpu_aux_buf_free(vpu, &ctx->hw.vp8e.ctrl_buf);
}

static inline u32 enc_in_img_ctrl(struct rockchip_vpu_ctx *ctx)
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

static void rk3288_vpu_vp8e_set_buffers(struct rockchip_vpu_dev *vpu,
					struct rockchip_vpu_ctx *ctx)
{
	struct vb2_v4l2_buffer *vb2_dst = to_vb2_v4l2_buffer(&ctx->run.dst->vb.vb2_buf);
	const struct rk3288_vp8e_reg_params *params =
		(struct rk3288_vp8e_reg_params *)ctx->run.vp8e.reg_params;
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

	if (rockchip_vpu_ctx_is_dummy_encode(ctx)) {
		dst_dma = vpu->dummy_encode_dst.dma;
		dst_size = vpu->dummy_encode_dst.size;
	} else {
		dst_dma = vb2_dma_contig_plane_dma_addr(&ctx->run.dst->vb.vb2_buf, 0);
		dst_size = vb2_plane_size(&ctx->run.dst->vb.vb2_buf, 0);
	}

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
		vb2_dst->flags |= V4L2_BUF_FLAG_KEYFRAME;
	else
		vb2_dst->flags &= ~V4L2_BUF_FLAG_KEYFRAME;

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
	if (rockchip_vpu_ctx_is_dummy_encode(ctx)) {
		vepu_write_relaxed(vpu, vpu->dummy_encode_src[PLANE_Y].dma,
					VEPU_REG_ADDR_IN_LUMA);
		vepu_write_relaxed(vpu, vpu->dummy_encode_src[PLANE_CB].dma,
					VEPU_REG_ADDR_IN_CB);
		vepu_write_relaxed(vpu, vpu->dummy_encode_src[PLANE_CR].dma,
					VEPU_REG_ADDR_IN_CR);
	} else {
		vepu_write_relaxed(vpu, vb2_dma_contig_plane_dma_addr(
					&ctx->run.src->vb.vb2_buf, PLANE_Y),
					VEPU_REG_ADDR_IN_LUMA);
		vepu_write_relaxed(vpu, vb2_dma_contig_plane_dma_addr(
					&ctx->run.src->vb.vb2_buf, PLANE_CB),
					VEPU_REG_ADDR_IN_CB);
		vepu_write_relaxed(vpu, vb2_dma_contig_plane_dma_addr(
					&ctx->run.src->vb.vb2_buf, PLANE_CR),
					VEPU_REG_ADDR_IN_CR);
	}

	/* Source parameters. */
	vepu_write_relaxed(vpu, enc_in_img_ctrl(ctx), VEPU_REG_IN_IMG_CTRL);
}

static void rk3288_vpu_vp8e_set_params(struct rockchip_vpu_dev *vpu,
				       struct rockchip_vpu_ctx *ctx)
{
	const struct rk3288_vp8e_reg_params *params =
		(struct rk3288_vp8e_reg_params *)ctx->run.vp8e.reg_params;
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

void rk3288_vpu_vp8e_run(struct rockchip_vpu_ctx *ctx)
{
	struct vb2_v4l2_buffer *vb2_dst = to_vb2_v4l2_buffer(&ctx->run.dst->vb.vb2_buf);
	struct rockchip_vpu_dev *vpu = ctx->dev;
	u32 reg;

	/* The hardware expects the control buffer to be zeroed. */
	memset(ctx->hw.vp8e.ctrl_buf.cpu, 0,
		sizeof(struct rk3288_vpu_vp8e_ctrl_buf));

	/*
	 * Program the hardware.
	 */
	rockchip_vpu_power_on(vpu);

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

	if (vb2_dst->flags & V4L2_BUF_FLAG_KEYFRAME)
		reg |= VEPU_REG_ENC_CTRL_KEYFRAME_BIT;

	vepu_write(vpu, reg, VEPU_REG_ENC_CTRL);
}

void rk3288_vpu_vp8e_done(struct rockchip_vpu_ctx *ctx,
			  enum vb2_buffer_state result)
{
	struct rk3288_vpu_vp8e_ctrl_buf *ctrl_buf = ctx->hw.vp8e.ctrl_buf.cpu;

	/* Read length information of this run from utility buffer. */
	ctx->run.dst->vp8e.ext_hdr_size = ctrl_buf->ext_hdr_size;
	ctx->run.dst->vp8e.dct_size = ctrl_buf->dct_size;

	rockchip_vpu_run_done(ctx, result);
}

/*
 * WAR for encoder state corruption after decoding
 */

static const struct rockchip_reg_params dummy_encode_reg_params = {
	.rk3288_vp8e = {
		/* 00000014 */ .hdr_len = 0x00000000,
		/* 00000038 */ .enc_ctrl = VEPU_REG_ENC_CTRL_KEYFRAME_BIT,
		/* 00000040 */ .enc_ctrl0 = 0x00000000,
		/* 00000044 */ .enc_ctrl1 = 0x00000000,
		/* 00000048 */ .enc_ctrl2 = 0x00040014,
		/* 0000004c */ .enc_ctrl3 = 0x404083c0,
		/* 00000050 */ .enc_ctrl5 = 0x01006bff,
		/* 00000054 */ .enc_ctrl4 = 0x00000039,
		/* 00000058 */ .str_hdr_rem_msb = 0x85848805,
		/* 0000005c */ .str_hdr_rem_lsb = 0x02000000,
		/* 00000064 */ .mad_ctrl = 0x00000000,
		/* 0000006c */ .qp_val = {
			/* 0000006c */ 0x020213b1,
			/* 00000070 */ 0x02825249,
			/* 00000074 */ 0x048409d8,
			/* 00000078 */ 0x03834c30,
			/* 0000007c */ 0x020213b1,
			/* 00000080 */ 0x02825249,
			/* 00000084 */ 0x00340e0d,
			/* 00000088 */ 0x401c1a15,
		},
		/* 0000008c */ .bool_enc = 0x00018140,
		/* 00000090 */ .vp8_ctrl0 = 0x000695c0,
		/* 00000094 */ .rlc_ctrl = 0x14000000,
		/* 00000098 */ .mb_ctrl = 0x00000000,
		/* 000000d4 */ .rgb_yuv_coeff = {
			/* 000000d4 */ 0x962b4c85,
			/* 000000d8 */ 0x90901d50,
		},
		/* 000000dc */ .rgb_mask_msb = 0x0000b694,
		/* 000000e0 */ .intra_area_ctrl = 0xffffffff,
		/* 000000e4 */ .cir_intra_ctrl = 0x00000000,
		/* 000000f0 */ .first_roi_area = 0xffffffff,
		/* 000000f4 */ .second_roi_area = 0xffffffff,
		/* 000000f8 */ .mvc_ctrl = 0x01780000,
		/* 00000100 */ .intra_penalty = {
			/* 00000100 */ 0x00010005,
			/* 00000104 */ 0x00015011,
			/* 00000108 */ 0x0000c005,
			/* 0000010c */ 0x00016010,
			/* 00000110 */ 0x0001a018,
			/* 00000114 */ 0x00018015,
			/* 00000118 */ 0x0001d01a,
		},
		/* 00000120 */ .seg_qp = {
			/* 00000120 */ 0x020213b1,
			/* 00000124 */ 0x02825249,
			/* 00000128 */ 0x048409d8,
			/* 0000012c */ 0x03834c30,
			/* 00000130 */ 0x020213b1,
			/* 00000134 */ 0x02825249,
			/* 00000138 */ 0x00340e0d,
			/* 0000013c */ 0x341c1a15,
			/* 00000140 */ 0x020213b1,
			/* 00000144 */ 0x02825249,
			/* 00000148 */ 0x048409d8,
			/* 0000014c */ 0x03834c30,
			/* 00000150 */ 0x020213b1,
			/* 00000154 */ 0x02825249,
			/* 00000158 */ 0x00340e0d,
			/* 0000015c */ 0x341c1a15,
			/* 00000160 */ 0x020213b1,
			/* 00000164 */ 0x02825249,
			/* 00000168 */ 0x048409d8,
			/* 0000016c */ 0x03834c30,
			/* 00000170 */ 0x020213b1,
			/* 00000174 */ 0x02825249,
			/* 00000178 */ 0x00340e0d,
			/* 0000017c */ 0x341c1a15,
		},
		/* 00000180 */ .dmv_4p_1p_penalty = {
			/* 00000180 */ 0x00020406,
			/* 00000184 */ 0x080a0c0e,
			/* 00000188 */ 0x10121416,
			/* 0000018c */ 0x181a1c1e,
			/* 00000190 */ 0x20222426,
			/* 00000194 */ 0x282a2c2e,
			/* 00000198 */ 0x30323436,
			/* 0000019c */ 0x383a3c3e,
			/* 000001a0 */ 0x40424446,
			/* 000001a4 */ 0x484a4c4e,
			/* 000001a8 */ 0x50525456,
			/* 000001ac */ 0x585a5c5e,
			/* 000001b0 */ 0x60626466,
			/* 000001b4 */ 0x686a6c6e,
			/* 000001b8 */ 0x70727476,
			/* NOTE: Further 17 registers set to 0. */
		},
		/*
		 * NOTE: Following registers all set to 0:
		 * - dmv_qpel_penalty,
		 * - vp8_ctrl1,
		 * - bit_cost_golden,
		 * - loop_flt_delta.
		 */
	},
};

const struct rockchip_reg_params *rk3288_vpu_vp8e_get_dummy_params(void)
{
	return &dummy_encode_reg_params;
}
