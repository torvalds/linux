// SPDX-License-Identifier: GPL-2.0
/*
 * Hantro VP8 codec driver
 *
 * Copyright (C) 2019 Rockchip Electronics Co., Ltd.
 *	ZhiChao Yu <zhichao.yu@rock-chips.com>
 *
 * Copyright (C) 2019 Google, Inc.
 *	Tomasz Figa <tfiga@chromium.org>
 */

#include <media/v4l2-mem2mem.h>

#include "hantro_hw.h"
#include "hantro.h"
#include "hantro_g1_regs.h"

/* DCT partition base address regs */
static const struct hantro_reg vp8_dec_dct_base[8] = {
	{ G1_REG_ADDR_STR, 0, 0xffffffff },
	{ G1_REG_ADDR_REF(8), 0, 0xffffffff },
	{ G1_REG_ADDR_REF(9), 0, 0xffffffff },
	{ G1_REG_ADDR_REF(10), 0, 0xffffffff },
	{ G1_REG_ADDR_REF(11), 0, 0xffffffff },
	{ G1_REG_ADDR_REF(12), 0, 0xffffffff },
	{ G1_REG_ADDR_REF(14), 0, 0xffffffff },
	{ G1_REG_ADDR_REF(15), 0, 0xffffffff },
};

/* Loop filter level regs */
static const struct hantro_reg vp8_dec_lf_level[4] = {
	{ G1_REG_REF_PIC(2), 18, 0x3f },
	{ G1_REG_REF_PIC(2), 12, 0x3f },
	{ G1_REG_REF_PIC(2), 6, 0x3f },
	{ G1_REG_REF_PIC(2), 0, 0x3f },
};

/* Macroblock loop filter level adjustment regs */
static const struct hantro_reg vp8_dec_mb_adj[4] = {
	{ G1_REG_REF_PIC(0), 21, 0x7f },
	{ G1_REG_REF_PIC(0), 14, 0x7f },
	{ G1_REG_REF_PIC(0), 7, 0x7f },
	{ G1_REG_REF_PIC(0), 0, 0x7f },
};

/* Reference frame adjustment regs */
static const struct hantro_reg vp8_dec_ref_adj[4] = {
	{ G1_REG_REF_PIC(1), 21, 0x7f },
	{ G1_REG_REF_PIC(1), 14, 0x7f },
	{ G1_REG_REF_PIC(1), 7, 0x7f },
	{ G1_REG_REF_PIC(1), 0, 0x7f },
};

/* Quantizer */
static const struct hantro_reg vp8_dec_quant[4] = {
	{ G1_REG_REF_PIC(3), 11, 0x7ff },
	{ G1_REG_REF_PIC(3), 0, 0x7ff },
	{ G1_REG_BD_REF_PIC(4), 11, 0x7ff },
	{ G1_REG_BD_REF_PIC(4), 0, 0x7ff },
};

/* Quantizer delta regs */
static const struct hantro_reg vp8_dec_quant_delta[5] = {
	{ G1_REG_REF_PIC(3), 27, 0x1f },
	{ G1_REG_REF_PIC(3), 22, 0x1f },
	{ G1_REG_BD_REF_PIC(4), 27, 0x1f },
	{ G1_REG_BD_REF_PIC(4), 22, 0x1f },
	{ G1_REG_BD_P_REF_PIC, 27, 0x1f },
};

/* DCT partition start bits regs */
static const struct hantro_reg vp8_dec_dct_start_bits[8] = {
	{ G1_REG_DEC_CTRL2, 26, 0x3f }, { G1_REG_DEC_CTRL4, 26, 0x3f },
	{ G1_REG_DEC_CTRL4, 20, 0x3f }, { G1_REG_DEC_CTRL7, 24, 0x3f },
	{ G1_REG_DEC_CTRL7, 18, 0x3f }, { G1_REG_DEC_CTRL7, 12, 0x3f },
	{ G1_REG_DEC_CTRL7, 6, 0x3f },  { G1_REG_DEC_CTRL7, 0, 0x3f },
};

/* Precision filter tap regs */
static const struct hantro_reg vp8_dec_pred_bc_tap[8][4] = {
	{
		{ G1_REG_PRED_FLT, 22, 0x3ff },
		{ G1_REG_PRED_FLT, 12, 0x3ff },
		{ G1_REG_PRED_FLT, 2, 0x3ff },
		{ G1_REG_REF_PIC(4), 22, 0x3ff },
	},
	{
		{ G1_REG_REF_PIC(4), 12, 0x3ff },
		{ G1_REG_REF_PIC(4), 2, 0x3ff },
		{ G1_REG_REF_PIC(5), 22, 0x3ff },
		{ G1_REG_REF_PIC(5), 12, 0x3ff },
	},
	{
		{ G1_REG_REF_PIC(5), 2, 0x3ff },
		{ G1_REG_REF_PIC(6), 22, 0x3ff },
		{ G1_REG_REF_PIC(6), 12, 0x3ff },
		{ G1_REG_REF_PIC(6), 2, 0x3ff },
	},
	{
		{ G1_REG_REF_PIC(7), 22, 0x3ff },
		{ G1_REG_REF_PIC(7), 12, 0x3ff },
		{ G1_REG_REF_PIC(7), 2, 0x3ff },
		{ G1_REG_LT_REF, 22, 0x3ff },
	},
	{
		{ G1_REG_LT_REF, 12, 0x3ff },
		{ G1_REG_LT_REF, 2, 0x3ff },
		{ G1_REG_VALID_REF, 22, 0x3ff },
		{ G1_REG_VALID_REF, 12, 0x3ff },
	},
	{
		{ G1_REG_VALID_REF, 2, 0x3ff },
		{ G1_REG_BD_REF_PIC(0), 22, 0x3ff },
		{ G1_REG_BD_REF_PIC(0), 12, 0x3ff },
		{ G1_REG_BD_REF_PIC(0), 2, 0x3ff },
	},
	{
		{ G1_REG_BD_REF_PIC(1), 22, 0x3ff },
		{ G1_REG_BD_REF_PIC(1), 12, 0x3ff },
		{ G1_REG_BD_REF_PIC(1), 2, 0x3ff },
		{ G1_REG_BD_REF_PIC(2), 22, 0x3ff },
	},
	{
		{ G1_REG_BD_REF_PIC(2), 12, 0x3ff },
		{ G1_REG_BD_REF_PIC(2), 2, 0x3ff },
		{ G1_REG_BD_REF_PIC(3), 22, 0x3ff },
		{ G1_REG_BD_REF_PIC(3), 12, 0x3ff },
	},
};

/*
 * Set loop filters
 */
static void cfg_lf(struct hantro_ctx *ctx,
		   const struct v4l2_ctrl_vp8_frame *hdr)
{
	const struct v4l2_vp8_segment *seg = &hdr->segment;
	const struct v4l2_vp8_loop_filter *lf = &hdr->lf;
	struct hantro_dev *vpu = ctx->dev;
	unsigned int i;
	u32 reg;

	if (!(seg->flags & V4L2_VP8_SEGMENT_FLAG_ENABLED)) {
		hantro_reg_write(vpu, &vp8_dec_lf_level[0], lf->level);
	} else if (seg->flags & V4L2_VP8_SEGMENT_FLAG_DELTA_VALUE_MODE) {
		for (i = 0; i < 4; i++) {
			u32 lf_level = clamp(lf->level + seg->lf_update[i],
					     0, 63);

			hantro_reg_write(vpu, &vp8_dec_lf_level[i], lf_level);
		}
	} else {
		for (i = 0; i < 4; i++)
			hantro_reg_write(vpu, &vp8_dec_lf_level[i],
					 seg->lf_update[i]);
	}

	reg = G1_REG_REF_PIC_FILT_SHARPNESS(lf->sharpness_level);
	if (lf->flags & V4L2_VP8_LF_FILTER_TYPE_SIMPLE)
		reg |= G1_REG_REF_PIC_FILT_TYPE_E;
	vdpu_write_relaxed(vpu, reg, G1_REG_REF_PIC(0));

	if (lf->flags & V4L2_VP8_LF_ADJ_ENABLE) {
		for (i = 0; i < 4; i++) {
			hantro_reg_write(vpu, &vp8_dec_mb_adj[i],
					 lf->mb_mode_delta[i]);
			hantro_reg_write(vpu, &vp8_dec_ref_adj[i],
					 lf->ref_frm_delta[i]);
		}
	}
}

/*
 * Set quantization parameters
 */
static void cfg_qp(struct hantro_ctx *ctx,
		   const struct v4l2_ctrl_vp8_frame *hdr)
{
	const struct v4l2_vp8_quantization *q = &hdr->quant;
	const struct v4l2_vp8_segment *seg = &hdr->segment;
	struct hantro_dev *vpu = ctx->dev;
	unsigned int i;

	if (!(seg->flags & V4L2_VP8_SEGMENT_FLAG_ENABLED)) {
		hantro_reg_write(vpu, &vp8_dec_quant[0], q->y_ac_qi);
	} else if (seg->flags & V4L2_VP8_SEGMENT_FLAG_DELTA_VALUE_MODE) {
		for (i = 0; i < 4; i++) {
			u32 quant = clamp(q->y_ac_qi + seg->quant_update[i],
					  0, 127);

			hantro_reg_write(vpu, &vp8_dec_quant[i], quant);
		}
	} else {
		for (i = 0; i < 4; i++)
			hantro_reg_write(vpu, &vp8_dec_quant[i],
					 seg->quant_update[i]);
	}

	hantro_reg_write(vpu, &vp8_dec_quant_delta[0], q->y_dc_delta);
	hantro_reg_write(vpu, &vp8_dec_quant_delta[1], q->y2_dc_delta);
	hantro_reg_write(vpu, &vp8_dec_quant_delta[2], q->y2_ac_delta);
	hantro_reg_write(vpu, &vp8_dec_quant_delta[3], q->uv_dc_delta);
	hantro_reg_write(vpu, &vp8_dec_quant_delta[4], q->uv_ac_delta);
}

/*
 * set control partition and DCT partition regs
 *
 * VP8 frame stream data layout:
 *
 *	                     first_part_size          parttion_sizes[0]
 *                              ^                     ^
 * src_dma                      |                     |
 * ^                   +--------+------+        +-----+-----+
 * |                   | control part  |        |           |
 * +--------+----------------+------------------+-----------+-----+-----------+
 * | tag 3B | extra 7B | hdr | mb_data | DCT sz | DCT part0 | ... | DCT partn |
 * +--------+-----------------------------------+-----------+-----+-----------+
 *                           |         |        |                             |
 *                           v         +----+---+                             v
 *                           mb_start       |                       src_dma_end
 *                                          v
 *                                       DCT size part
 *                                      (num_dct-1)*3B
 * Note:
 *   1. only key-frames have extra 7-bytes
 *   2. all offsets are base on src_dma
 *   3. number of DCT parts is 1, 2, 4 or 8
 *   4. the addresses set to the VPU must be 64-bits aligned
 */
static void cfg_parts(struct hantro_ctx *ctx,
		      const struct v4l2_ctrl_vp8_frame *hdr)
{
	struct hantro_dev *vpu = ctx->dev;
	struct vb2_v4l2_buffer *vb2_src;
	u32 first_part_offset = V4L2_VP8_FRAME_IS_KEY_FRAME(hdr) ? 10 : 3;
	u32 mb_size, mb_offset_bytes, mb_offset_bits, mb_start_bits;
	u32 dct_size_part_size, dct_part_offset;
	struct hantro_reg reg;
	dma_addr_t src_dma;
	u32 dct_part_total_len = 0;
	u32 count = 0;
	unsigned int i;

	vb2_src = hantro_get_src_buf(ctx);
	src_dma = vb2_dma_contig_plane_dma_addr(&vb2_src->vb2_buf, 0);

	/*
	 * Calculate control partition mb data info
	 * @first_part_header_bits:	bits offset of mb data from first
	 *				part start pos
	 * @mb_offset_bits:		bits offset of mb data from src_dma
	 *				base addr
	 * @mb_offset_byte:		bytes offset of mb data from src_dma
	 *				base addr
	 * @mb_start_bits:		bits offset of mb data from mb data
	 *				64bits alignment addr
	 */
	mb_offset_bits = first_part_offset * 8 +
			 hdr->first_part_header_bits + 8;
	mb_offset_bytes = mb_offset_bits / 8;
	mb_start_bits = mb_offset_bits -
			(mb_offset_bytes & (~DEC_8190_ALIGN_MASK)) * 8;
	mb_size = hdr->first_part_size -
		  (mb_offset_bytes - first_part_offset) +
		  (mb_offset_bytes & DEC_8190_ALIGN_MASK);

	/* Macroblock data aligned base addr */
	vdpu_write_relaxed(vpu, (mb_offset_bytes & (~DEC_8190_ALIGN_MASK))
				+ src_dma, G1_REG_ADDR_REF(13));

	/* Macroblock data start bits */
	reg.base = G1_REG_DEC_CTRL2;
	reg.mask = 0x3f;
	reg.shift = 18;
	hantro_reg_write(vpu, &reg, mb_start_bits);

	/* Macroblock aligned data length */
	reg.base = G1_REG_DEC_CTRL6;
	reg.mask = 0x3fffff;
	reg.shift = 0;
	hantro_reg_write(vpu, &reg, mb_size + 1);

	/*
	 * Calculate DCT partition info
	 * @dct_size_part_size: Containing sizes of DCT part, every DCT part
	 *			has 3 bytes to store its size, except the last
	 *			DCT part
	 * @dct_part_offset:	bytes offset of DCT parts from src_dma base addr
	 * @dct_part_total_len: total size of all DCT parts
	 */
	dct_size_part_size = (hdr->num_dct_parts - 1) * 3;
	dct_part_offset = first_part_offset + hdr->first_part_size;
	for (i = 0; i < hdr->num_dct_parts; i++)
		dct_part_total_len += hdr->dct_part_sizes[i];
	dct_part_total_len += dct_size_part_size;
	dct_part_total_len += (dct_part_offset & DEC_8190_ALIGN_MASK);

	/* Number of DCT partitions */
	reg.base = G1_REG_DEC_CTRL6;
	reg.mask = 0xf;
	reg.shift = 24;
	hantro_reg_write(vpu, &reg, hdr->num_dct_parts - 1);

	/* DCT partition length */
	vdpu_write_relaxed(vpu,
			   G1_REG_DEC_CTRL3_STREAM_LEN(dct_part_total_len),
			   G1_REG_DEC_CTRL3);

	/* DCT partitions base address */
	for (i = 0; i < hdr->num_dct_parts; i++) {
		u32 byte_offset = dct_part_offset + dct_size_part_size + count;
		u32 base_addr = byte_offset + src_dma;

		hantro_reg_write(vpu, &vp8_dec_dct_base[i],
				 base_addr & (~DEC_8190_ALIGN_MASK));

		hantro_reg_write(vpu, &vp8_dec_dct_start_bits[i],
				 (byte_offset & DEC_8190_ALIGN_MASK) * 8);

		count += hdr->dct_part_sizes[i];
	}
}

/*
 * prediction filter taps
 * normal 6-tap filters
 */
static void cfg_tap(struct hantro_ctx *ctx,
		    const struct v4l2_ctrl_vp8_frame *hdr)
{
	struct hantro_dev *vpu = ctx->dev;
	struct hantro_reg reg;
	u32 val = 0;
	int i, j;

	reg.base = G1_REG_BD_REF_PIC(3);
	reg.mask = 0xf;

	if ((hdr->version & 0x03) != 0)
		return; /* Tap filter not used. */

	for (i = 0; i < 8; i++) {
		val = (hantro_vp8_dec_mc_filter[i][0] << 2) |
		       hantro_vp8_dec_mc_filter[i][5];

		for (j = 0; j < 4; j++)
			hantro_reg_write(vpu, &vp8_dec_pred_bc_tap[i][j],
					 hantro_vp8_dec_mc_filter[i][j + 1]);

		switch (i) {
		case 2:
			reg.shift = 8;
			break;
		case 4:
			reg.shift = 4;
			break;
		case 6:
			reg.shift = 0;
			break;
		default:
			continue;
		}

		hantro_reg_write(vpu, &reg, val);
	}
}

static void cfg_ref(struct hantro_ctx *ctx,
		    const struct v4l2_ctrl_vp8_frame *hdr,
		    struct vb2_v4l2_buffer *vb2_dst)
{
	struct hantro_dev *vpu = ctx->dev;
	dma_addr_t ref;


	ref = hantro_get_ref(ctx, hdr->last_frame_ts);
	if (!ref) {
		vpu_debug(0, "failed to find last frame ts=%llu\n",
			  hdr->last_frame_ts);
		ref = vb2_dma_contig_plane_dma_addr(&vb2_dst->vb2_buf, 0);
	}
	vdpu_write_relaxed(vpu, ref, G1_REG_ADDR_REF(0));

	ref = hantro_get_ref(ctx, hdr->golden_frame_ts);
	if (!ref && hdr->golden_frame_ts)
		vpu_debug(0, "failed to find golden frame ts=%llu\n",
			  hdr->golden_frame_ts);
	if (!ref)
		ref = vb2_dma_contig_plane_dma_addr(&vb2_dst->vb2_buf, 0);
	if (hdr->flags & V4L2_VP8_FRAME_FLAG_SIGN_BIAS_GOLDEN)
		ref |= G1_REG_ADDR_REF_TOPC_E;
	vdpu_write_relaxed(vpu, ref, G1_REG_ADDR_REF(4));

	ref = hantro_get_ref(ctx, hdr->alt_frame_ts);
	if (!ref && hdr->alt_frame_ts)
		vpu_debug(0, "failed to find alt frame ts=%llu\n",
			  hdr->alt_frame_ts);
	if (!ref)
		ref = vb2_dma_contig_plane_dma_addr(&vb2_dst->vb2_buf, 0);
	if (hdr->flags & V4L2_VP8_FRAME_FLAG_SIGN_BIAS_ALT)
		ref |= G1_REG_ADDR_REF_TOPC_E;
	vdpu_write_relaxed(vpu, ref, G1_REG_ADDR_REF(5));
}

static void cfg_buffers(struct hantro_ctx *ctx,
			const struct v4l2_ctrl_vp8_frame *hdr,
			struct vb2_v4l2_buffer *vb2_dst)
{
	const struct v4l2_vp8_segment *seg = &hdr->segment;
	struct hantro_dev *vpu = ctx->dev;
	dma_addr_t dst_dma;
	u32 reg;

	/* Set probability table buffer address */
	vdpu_write_relaxed(vpu, ctx->vp8_dec.prob_tbl.dma,
			   G1_REG_ADDR_QTABLE);

	/* Set segment map address */
	reg = G1_REG_FWD_PIC1_SEGMENT_BASE(ctx->vp8_dec.segment_map.dma);
	if (seg->flags & V4L2_VP8_SEGMENT_FLAG_ENABLED) {
		reg |= G1_REG_FWD_PIC1_SEGMENT_E;
		if (seg->flags & V4L2_VP8_SEGMENT_FLAG_UPDATE_MAP)
			reg |= G1_REG_FWD_PIC1_SEGMENT_UPD_E;
	}
	vdpu_write_relaxed(vpu, reg, G1_REG_FWD_PIC(0));

	dst_dma = hantro_get_dec_buf_addr(ctx, &vb2_dst->vb2_buf);
	vdpu_write_relaxed(vpu, dst_dma, G1_REG_ADDR_DST);
}

int hantro_g1_vp8_dec_run(struct hantro_ctx *ctx)
{
	const struct v4l2_ctrl_vp8_frame *hdr;
	struct hantro_dev *vpu = ctx->dev;
	struct vb2_v4l2_buffer *vb2_dst;
	size_t height = ctx->dst_fmt.height;
	size_t width = ctx->dst_fmt.width;
	u32 mb_width, mb_height;
	u32 reg;

	hantro_start_prepare_run(ctx);

	hdr = hantro_get_ctrl(ctx, V4L2_CID_STATELESS_VP8_FRAME);
	if (WARN_ON(!hdr))
		return -EINVAL;

	/* Reset segment_map buffer in keyframe */
	if (V4L2_VP8_FRAME_IS_KEY_FRAME(hdr) && ctx->vp8_dec.segment_map.cpu)
		memset(ctx->vp8_dec.segment_map.cpu, 0,
		       ctx->vp8_dec.segment_map.size);

	hantro_vp8_prob_update(ctx, hdr);

	reg = G1_REG_CONFIG_DEC_TIMEOUT_E |
	      G1_REG_CONFIG_DEC_STRENDIAN_E |
	      G1_REG_CONFIG_DEC_INSWAP32_E |
	      G1_REG_CONFIG_DEC_STRSWAP32_E |
	      G1_REG_CONFIG_DEC_OUTSWAP32_E |
	      G1_REG_CONFIG_DEC_CLK_GATE_E |
	      G1_REG_CONFIG_DEC_IN_ENDIAN |
	      G1_REG_CONFIG_DEC_OUT_ENDIAN |
	      G1_REG_CONFIG_DEC_MAX_BURST(16);
	vdpu_write_relaxed(vpu, reg, G1_REG_CONFIG);

	reg = G1_REG_DEC_CTRL0_DEC_MODE(10);
	if (!V4L2_VP8_FRAME_IS_KEY_FRAME(hdr))
		reg |= G1_REG_DEC_CTRL0_PIC_INTER_E;
	if (!(hdr->flags & V4L2_VP8_FRAME_FLAG_MB_NO_SKIP_COEFF))
		reg |= G1_REG_DEC_CTRL0_SKIP_MODE;
	if (hdr->lf.level == 0)
		reg |= G1_REG_DEC_CTRL0_FILTERING_DIS;
	vdpu_write_relaxed(vpu, reg, G1_REG_DEC_CTRL0);

	/* Frame dimensions */
	mb_width = MB_WIDTH(width);
	mb_height = MB_HEIGHT(height);
	reg = G1_REG_DEC_CTRL1_PIC_MB_WIDTH(mb_width) |
	      G1_REG_DEC_CTRL1_PIC_MB_HEIGHT_P(mb_height) |
	      G1_REG_DEC_CTRL1_PIC_MB_W_EXT(mb_width >> 9) |
	      G1_REG_DEC_CTRL1_PIC_MB_H_EXT(mb_height >> 8);
	vdpu_write_relaxed(vpu, reg, G1_REG_DEC_CTRL1);

	/* Boolean decoder */
	reg = G1_REG_DEC_CTRL2_BOOLEAN_RANGE(hdr->coder_state.range)
		| G1_REG_DEC_CTRL2_BOOLEAN_VALUE(hdr->coder_state.value);
	vdpu_write_relaxed(vpu, reg, G1_REG_DEC_CTRL2);

	reg = 0;
	if (hdr->version != 3)
		reg |= G1_REG_DEC_CTRL4_VC1_HEIGHT_EXT;
	if (hdr->version & 0x3)
		reg |= G1_REG_DEC_CTRL4_BILIN_MC_E;
	vdpu_write_relaxed(vpu, reg, G1_REG_DEC_CTRL4);

	cfg_lf(ctx, hdr);
	cfg_qp(ctx, hdr);
	cfg_parts(ctx, hdr);
	cfg_tap(ctx, hdr);

	vb2_dst = hantro_get_dst_buf(ctx);
	cfg_ref(ctx, hdr, vb2_dst);
	cfg_buffers(ctx, hdr, vb2_dst);

	hantro_end_prepare_run(ctx);

	vdpu_write(vpu, G1_REG_INTERRUPT_DEC_E, G1_REG_INTERRUPT);

	return 0;
}
