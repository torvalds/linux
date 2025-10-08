// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip Video Decoder VP9 backend
 *
 * Copyright (C) 2019 Collabora, Ltd.
 *	Boris Brezillon <boris.brezillon@collabora.com>
 * Copyright (C) 2021 Collabora, Ltd.
 *	Andrzej Pietrasiewicz <andrzej.p@collabora.com>
 *
 * Copyright (C) 2016 Rockchip Electronics Co., Ltd.
 *	Alpha Lin <Alpha.Lin@rock-chips.com>
 */

/*
 * For following the vp9 spec please start reading this driver
 * code from rkvdec_vp9_run() followed by rkvdec_vp9_done().
 */

#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-vp9.h>

#include "rkvdec.h"
#include "rkvdec-regs.h"

#define RKVDEC_VP9_PROBE_SIZE		4864
#define RKVDEC_VP9_COUNT_SIZE		13232
#define RKVDEC_VP9_MAX_SEGMAP_SIZE	73728

struct rkvdec_vp9_intra_mode_probs {
	u8 y_mode[105];
	u8 uv_mode[23];
};

struct rkvdec_vp9_intra_only_frame_probs {
	u8 coef_intra[4][2][128];
	struct rkvdec_vp9_intra_mode_probs intra_mode[10];
};

struct rkvdec_vp9_inter_frame_probs {
	u8 y_mode[4][9];
	u8 comp_mode[5];
	u8 comp_ref[5];
	u8 single_ref[5][2];
	u8 inter_mode[7][3];
	u8 interp_filter[4][2];
	u8 padding0[11];
	u8 coef[2][4][2][128];
	u8 uv_mode_0_2[3][9];
	u8 padding1[5];
	u8 uv_mode_3_5[3][9];
	u8 padding2[5];
	u8 uv_mode_6_8[3][9];
	u8 padding3[5];
	u8 uv_mode_9[9];
	u8 padding4[7];
	u8 padding5[16];
	struct {
		u8 joint[3];
		u8 sign[2];
		u8 classes[2][10];
		u8 class0_bit[2];
		u8 bits[2][10];
		u8 class0_fr[2][2][3];
		u8 fr[2][3];
		u8 class0_hp[2];
		u8 hp[2];
	} mv;
};

struct rkvdec_vp9_probs {
	u8 partition[16][3];
	u8 pred[3];
	u8 tree[7];
	u8 skip[3];
	u8 tx32[2][3];
	u8 tx16[2][2];
	u8 tx8[2][1];
	u8 is_inter[4];
	/* 128 bit alignment */
	u8 padding0[3];
	union {
		struct rkvdec_vp9_inter_frame_probs inter;
		struct rkvdec_vp9_intra_only_frame_probs intra_only;
	};
	/* 128 bit alignment */
	u8 padding1[11];
};

/* Data structure describing auxiliary buffer format. */
struct rkvdec_vp9_priv_tbl {
	struct rkvdec_vp9_probs probs;
	u8 segmap[2][RKVDEC_VP9_MAX_SEGMAP_SIZE];
};

struct rkvdec_vp9_refs_counts {
	u32 eob[2];
	u32 coeff[3];
};

struct rkvdec_vp9_inter_frame_symbol_counts {
	u32 partition[16][4];
	u32 skip[3][2];
	u32 inter[4][2];
	u32 tx32p[2][4];
	u32 tx16p[2][4];
	u32 tx8p[2][2];
	u32 y_mode[4][10];
	u32 uv_mode[10][10];
	u32 comp[5][2];
	u32 comp_ref[5][2];
	u32 single_ref[5][2][2];
	u32 mv_mode[7][4];
	u32 filter[4][3];
	u32 mv_joint[4];
	u32 sign[2][2];
	/* add 1 element for align */
	u32 classes[2][11 + 1];
	u32 class0[2][2];
	u32 bits[2][10][2];
	u32 class0_fp[2][2][4];
	u32 fp[2][4];
	u32 class0_hp[2][2];
	u32 hp[2][2];
	struct rkvdec_vp9_refs_counts ref_cnt[2][4][2][6][6];
};

struct rkvdec_vp9_intra_frame_symbol_counts {
	u32 partition[4][4][4];
	u32 skip[3][2];
	u32 intra[4][2];
	u32 tx32p[2][4];
	u32 tx16p[2][4];
	u32 tx8p[2][2];
	struct rkvdec_vp9_refs_counts ref_cnt[2][4][2][6][6];
};

struct rkvdec_vp9_run {
	struct rkvdec_run base;
	const struct v4l2_ctrl_vp9_frame *decode_params;
};

struct rkvdec_vp9_frame_info {
	u32 valid : 1;
	u32 segmapid : 1;
	u32 frame_context_idx : 2;
	u32 reference_mode : 2;
	u32 tx_mode : 3;
	u32 interpolation_filter : 3;
	u32 flags;
	u64 timestamp;
	struct v4l2_vp9_segmentation seg;
	struct v4l2_vp9_loop_filter lf;
};

struct rkvdec_vp9_ctx {
	struct rkvdec_aux_buf priv_tbl;
	struct rkvdec_aux_buf count_tbl;
	struct v4l2_vp9_frame_symbol_counts inter_cnts;
	struct v4l2_vp9_frame_symbol_counts intra_cnts;
	struct v4l2_vp9_frame_context probability_tables;
	struct v4l2_vp9_frame_context frame_context[4];
	struct rkvdec_vp9_frame_info cur;
	struct rkvdec_vp9_frame_info last;
};

static void write_coeff_plane(const u8 coef[6][6][3], u8 *coeff_plane)
{
	unsigned int idx = 0, byte_count = 0;
	int k, m, n;
	u8 p;

	for (k = 0; k < 6; k++) {
		for (m = 0; m < 6; m++) {
			for (n = 0; n < 3; n++) {
				p = coef[k][m][n];
				coeff_plane[idx++] = p;
				byte_count++;
				if (byte_count == 27) {
					idx += 5;
					byte_count = 0;
				}
			}
		}
	}
}

static void init_intra_only_probs(struct rkvdec_ctx *ctx,
				  const struct rkvdec_vp9_run *run)
{
	struct rkvdec_vp9_ctx *vp9_ctx = ctx->priv;
	struct rkvdec_vp9_priv_tbl *tbl = vp9_ctx->priv_tbl.cpu;
	struct rkvdec_vp9_intra_only_frame_probs *rkprobs;
	const struct v4l2_vp9_frame_context *probs;
	unsigned int i, j, k;

	rkprobs = &tbl->probs.intra_only;
	probs = &vp9_ctx->probability_tables;

	/*
	 * intra only 149 x 128 bits ,aligned to 152 x 128 bits coeff related
	 * prob 64 x 128 bits
	 */
	for (i = 0; i < ARRAY_SIZE(probs->coef); i++) {
		for (j = 0; j < ARRAY_SIZE(probs->coef[0]); j++)
			write_coeff_plane(probs->coef[i][j][0],
					  rkprobs->coef_intra[i][j]);
	}

	/* intra mode prob  80 x 128 bits */
	for (i = 0; i < ARRAY_SIZE(v4l2_vp9_kf_y_mode_prob); i++) {
		unsigned int byte_count = 0;
		int idx = 0;

		/* vp9_kf_y_mode_prob */
		for (j = 0; j < ARRAY_SIZE(v4l2_vp9_kf_y_mode_prob[0]); j++) {
			for (k = 0; k < ARRAY_SIZE(v4l2_vp9_kf_y_mode_prob[0][0]);
			     k++) {
				u8 val = v4l2_vp9_kf_y_mode_prob[i][j][k];

				rkprobs->intra_mode[i].y_mode[idx++] = val;
				byte_count++;
				if (byte_count == 27) {
					byte_count = 0;
					idx += 5;
				}
			}
		}
	}

	for (i = 0; i < sizeof(v4l2_vp9_kf_uv_mode_prob); ++i) {
		const u8 *ptr = (const u8 *)v4l2_vp9_kf_uv_mode_prob;

		rkprobs->intra_mode[i / 23].uv_mode[i % 23] = ptr[i];
	}
}

static void init_inter_probs(struct rkvdec_ctx *ctx,
			     const struct rkvdec_vp9_run *run)
{
	struct rkvdec_vp9_ctx *vp9_ctx = ctx->priv;
	struct rkvdec_vp9_priv_tbl *tbl = vp9_ctx->priv_tbl.cpu;
	struct rkvdec_vp9_inter_frame_probs *rkprobs;
	const struct v4l2_vp9_frame_context *probs;
	unsigned int i, j, k;

	rkprobs = &tbl->probs.inter;
	probs = &vp9_ctx->probability_tables;

	/*
	 * inter probs
	 * 151 x 128 bits, aligned to 152 x 128 bits
	 * inter only
	 * intra_y_mode & inter_block info 6 x 128 bits
	 */

	memcpy(rkprobs->y_mode, probs->y_mode, sizeof(rkprobs->y_mode));
	memcpy(rkprobs->comp_mode, probs->comp_mode,
	       sizeof(rkprobs->comp_mode));
	memcpy(rkprobs->comp_ref, probs->comp_ref,
	       sizeof(rkprobs->comp_ref));
	memcpy(rkprobs->single_ref, probs->single_ref,
	       sizeof(rkprobs->single_ref));
	memcpy(rkprobs->inter_mode, probs->inter_mode,
	       sizeof(rkprobs->inter_mode));
	memcpy(rkprobs->interp_filter, probs->interp_filter,
	       sizeof(rkprobs->interp_filter));

	/* 128 x 128 bits coeff related */
	for (i = 0; i < ARRAY_SIZE(probs->coef); i++) {
		for (j = 0; j < ARRAY_SIZE(probs->coef[0]); j++) {
			for (k = 0; k < ARRAY_SIZE(probs->coef[0][0]); k++)
				write_coeff_plane(probs->coef[i][j][k],
						  rkprobs->coef[k][i][j]);
		}
	}

	/* intra uv mode 6 x 128 */
	memcpy(rkprobs->uv_mode_0_2, &probs->uv_mode[0],
	       sizeof(rkprobs->uv_mode_0_2));
	memcpy(rkprobs->uv_mode_3_5, &probs->uv_mode[3],
	       sizeof(rkprobs->uv_mode_3_5));
	memcpy(rkprobs->uv_mode_6_8, &probs->uv_mode[6],
	       sizeof(rkprobs->uv_mode_6_8));
	memcpy(rkprobs->uv_mode_9, &probs->uv_mode[9],
	       sizeof(rkprobs->uv_mode_9));

	/* mv related 6 x 128 */
	memcpy(rkprobs->mv.joint, probs->mv.joint,
	       sizeof(rkprobs->mv.joint));
	memcpy(rkprobs->mv.sign, probs->mv.sign,
	       sizeof(rkprobs->mv.sign));
	memcpy(rkprobs->mv.classes, probs->mv.classes,
	       sizeof(rkprobs->mv.classes));
	memcpy(rkprobs->mv.class0_bit, probs->mv.class0_bit,
	       sizeof(rkprobs->mv.class0_bit));
	memcpy(rkprobs->mv.bits, probs->mv.bits,
	       sizeof(rkprobs->mv.bits));
	memcpy(rkprobs->mv.class0_fr, probs->mv.class0_fr,
	       sizeof(rkprobs->mv.class0_fr));
	memcpy(rkprobs->mv.fr, probs->mv.fr,
	       sizeof(rkprobs->mv.fr));
	memcpy(rkprobs->mv.class0_hp, probs->mv.class0_hp,
	       sizeof(rkprobs->mv.class0_hp));
	memcpy(rkprobs->mv.hp, probs->mv.hp,
	       sizeof(rkprobs->mv.hp));
}

static void init_probs(struct rkvdec_ctx *ctx,
		       const struct rkvdec_vp9_run *run)
{
	const struct v4l2_ctrl_vp9_frame *dec_params;
	struct rkvdec_vp9_ctx *vp9_ctx = ctx->priv;
	struct rkvdec_vp9_priv_tbl *tbl = vp9_ctx->priv_tbl.cpu;
	struct rkvdec_vp9_probs *rkprobs = &tbl->probs;
	const struct v4l2_vp9_segmentation *seg;
	const struct v4l2_vp9_frame_context *probs;
	bool intra_only;

	dec_params = run->decode_params;
	probs = &vp9_ctx->probability_tables;
	seg = &dec_params->seg;

	memset(rkprobs, 0, sizeof(*rkprobs));

	intra_only = !!(dec_params->flags &
			(V4L2_VP9_FRAME_FLAG_KEY_FRAME |
			 V4L2_VP9_FRAME_FLAG_INTRA_ONLY));

	/* sb info  5 x 128 bit */
	memcpy(rkprobs->partition,
	       intra_only ? v4l2_vp9_kf_partition_probs : probs->partition,
	       sizeof(rkprobs->partition));

	memcpy(rkprobs->pred, seg->pred_probs, sizeof(rkprobs->pred));
	memcpy(rkprobs->tree, seg->tree_probs, sizeof(rkprobs->tree));
	memcpy(rkprobs->skip, probs->skip, sizeof(rkprobs->skip));
	memcpy(rkprobs->tx32, probs->tx32, sizeof(rkprobs->tx32));
	memcpy(rkprobs->tx16, probs->tx16, sizeof(rkprobs->tx16));
	memcpy(rkprobs->tx8, probs->tx8, sizeof(rkprobs->tx8));
	memcpy(rkprobs->is_inter, probs->is_inter, sizeof(rkprobs->is_inter));

	if (intra_only)
		init_intra_only_probs(ctx, run);
	else
		init_inter_probs(ctx, run);
}

struct rkvdec_vp9_ref_reg {
	u32 reg_frm_size;
	u32 reg_hor_stride;
	u32 reg_y_stride;
	u32 reg_yuv_stride;
	u32 reg_ref_base;
};

static struct rkvdec_vp9_ref_reg ref_regs[] = {
	{
		.reg_frm_size = RKVDEC_REG_VP9_FRAME_SIZE(0),
		.reg_hor_stride = RKVDEC_VP9_HOR_VIRSTRIDE(0),
		.reg_y_stride = RKVDEC_VP9_LAST_FRAME_YSTRIDE,
		.reg_yuv_stride = RKVDEC_VP9_LAST_FRAME_YUVSTRIDE,
		.reg_ref_base = RKVDEC_REG_VP9_LAST_FRAME_BASE,
	},
	{
		.reg_frm_size = RKVDEC_REG_VP9_FRAME_SIZE(1),
		.reg_hor_stride = RKVDEC_VP9_HOR_VIRSTRIDE(1),
		.reg_y_stride = RKVDEC_VP9_GOLDEN_FRAME_YSTRIDE,
		.reg_yuv_stride = 0,
		.reg_ref_base = RKVDEC_REG_VP9_GOLDEN_FRAME_BASE,
	},
	{
		.reg_frm_size = RKVDEC_REG_VP9_FRAME_SIZE(2),
		.reg_hor_stride = RKVDEC_VP9_HOR_VIRSTRIDE(2),
		.reg_y_stride = RKVDEC_VP9_ALTREF_FRAME_YSTRIDE,
		.reg_yuv_stride = 0,
		.reg_ref_base = RKVDEC_REG_VP9_ALTREF_FRAME_BASE,
	}
};

static struct rkvdec_decoded_buffer *
get_ref_buf(struct rkvdec_ctx *ctx, struct vb2_v4l2_buffer *dst, u64 timestamp)
{
	struct v4l2_m2m_ctx *m2m_ctx = ctx->fh.m2m_ctx;
	struct vb2_queue *cap_q = &m2m_ctx->cap_q_ctx.q;
	struct vb2_buffer *buf;

	/*
	 * If a ref is unused or invalid, address of current destination
	 * buffer is returned.
	 */
	buf = vb2_find_buffer(cap_q, timestamp);
	if (!buf)
		buf = &dst->vb2_buf;

	return vb2_to_rkvdec_decoded_buf(buf);
}

static dma_addr_t get_mv_base_addr(struct rkvdec_decoded_buffer *buf)
{
	unsigned int aligned_pitch, aligned_height, yuv_len;

	aligned_height = round_up(buf->vp9.height, 64);
	aligned_pitch = round_up(buf->vp9.width * buf->vp9.bit_depth, 512) / 8;
	yuv_len = (aligned_height * aligned_pitch * 3) / 2;

	return vb2_dma_contig_plane_dma_addr(&buf->base.vb.vb2_buf, 0) +
	       yuv_len;
}

static void config_ref_registers(struct rkvdec_ctx *ctx,
				 const struct rkvdec_vp9_run *run,
				 struct rkvdec_decoded_buffer *ref_buf,
				 struct rkvdec_vp9_ref_reg *ref_reg)
{
	unsigned int aligned_pitch, aligned_height, y_len, yuv_len;
	struct rkvdec_dev *rkvdec = ctx->dev;

	aligned_height = round_up(ref_buf->vp9.height, 64);
	writel_relaxed(RKVDEC_VP9_FRAMEWIDTH(ref_buf->vp9.width) |
		       RKVDEC_VP9_FRAMEHEIGHT(ref_buf->vp9.height),
		       rkvdec->regs + ref_reg->reg_frm_size);

	writel_relaxed(vb2_dma_contig_plane_dma_addr(&ref_buf->base.vb.vb2_buf, 0),
		       rkvdec->regs + ref_reg->reg_ref_base);

	if (&ref_buf->base.vb == run->base.bufs.dst)
		return;

	aligned_pitch = round_up(ref_buf->vp9.width * ref_buf->vp9.bit_depth, 512) / 8;
	y_len = aligned_height * aligned_pitch;
	yuv_len = (y_len * 3) / 2;

	writel_relaxed(RKVDEC_HOR_Y_VIRSTRIDE(aligned_pitch / 16) |
		       RKVDEC_HOR_UV_VIRSTRIDE(aligned_pitch / 16),
		       rkvdec->regs + ref_reg->reg_hor_stride);
	writel_relaxed(RKVDEC_VP9_REF_YSTRIDE(y_len / 16),
		       rkvdec->regs + ref_reg->reg_y_stride);

	if (!ref_reg->reg_yuv_stride)
		return;

	writel_relaxed(RKVDEC_VP9_REF_YUVSTRIDE(yuv_len / 16),
		       rkvdec->regs + ref_reg->reg_yuv_stride);
}

static void config_seg_registers(struct rkvdec_ctx *ctx, unsigned int segid)
{
	struct rkvdec_vp9_ctx *vp9_ctx = ctx->priv;
	const struct v4l2_vp9_segmentation *seg;
	struct rkvdec_dev *rkvdec = ctx->dev;
	s16 feature_val;
	int feature_id;
	u32 val = 0;

	seg = vp9_ctx->last.valid ? &vp9_ctx->last.seg : &vp9_ctx->cur.seg;
	feature_id = V4L2_VP9_SEG_LVL_ALT_Q;
	if (v4l2_vp9_seg_feat_enabled(seg->feature_enabled, feature_id, segid)) {
		feature_val = seg->feature_data[segid][feature_id];
		val |= RKVDEC_SEGID_FRAME_QP_DELTA_EN(1) |
		       RKVDEC_SEGID_FRAME_QP_DELTA(feature_val);
	}

	feature_id = V4L2_VP9_SEG_LVL_ALT_L;
	if (v4l2_vp9_seg_feat_enabled(seg->feature_enabled, feature_id, segid)) {
		feature_val = seg->feature_data[segid][feature_id];
		val |= RKVDEC_SEGID_FRAME_LOOPFILTER_VALUE_EN(1) |
		       RKVDEC_SEGID_FRAME_LOOPFILTER_VALUE(feature_val);
	}

	feature_id = V4L2_VP9_SEG_LVL_REF_FRAME;
	if (v4l2_vp9_seg_feat_enabled(seg->feature_enabled, feature_id, segid)) {
		feature_val = seg->feature_data[segid][feature_id];
		val |= RKVDEC_SEGID_REFERINFO_EN(1) |
		       RKVDEC_SEGID_REFERINFO(feature_val);
	}

	feature_id = V4L2_VP9_SEG_LVL_SKIP;
	if (v4l2_vp9_seg_feat_enabled(seg->feature_enabled, feature_id, segid))
		val |= RKVDEC_SEGID_FRAME_SKIP_EN(1);

	if (!segid &&
	    (seg->flags & V4L2_VP9_SEGMENTATION_FLAG_ABS_OR_DELTA_UPDATE))
		val |= RKVDEC_SEGID_ABS_DELTA(1);

	writel_relaxed(val, rkvdec->regs + RKVDEC_VP9_SEGID_GRP(segid));
}

static void update_dec_buf_info(struct rkvdec_decoded_buffer *buf,
				const struct v4l2_ctrl_vp9_frame *dec_params)
{
	buf->vp9.width = dec_params->frame_width_minus_1 + 1;
	buf->vp9.height = dec_params->frame_height_minus_1 + 1;
	buf->vp9.bit_depth = dec_params->bit_depth;
}

static void update_ctx_cur_info(struct rkvdec_vp9_ctx *vp9_ctx,
				struct rkvdec_decoded_buffer *buf,
				const struct v4l2_ctrl_vp9_frame *dec_params)
{
	vp9_ctx->cur.valid = true;
	vp9_ctx->cur.reference_mode = dec_params->reference_mode;
	vp9_ctx->cur.interpolation_filter = dec_params->interpolation_filter;
	vp9_ctx->cur.flags = dec_params->flags;
	vp9_ctx->cur.timestamp = buf->base.vb.vb2_buf.timestamp;
	vp9_ctx->cur.seg = dec_params->seg;
	vp9_ctx->cur.lf = dec_params->lf;
}

static void update_ctx_last_info(struct rkvdec_vp9_ctx *vp9_ctx)
{
	vp9_ctx->last = vp9_ctx->cur;
}

static void config_registers(struct rkvdec_ctx *ctx,
			     const struct rkvdec_vp9_run *run)
{
	unsigned int y_len, uv_len, yuv_len, bit_depth, aligned_height, aligned_pitch, stream_len;
	const struct v4l2_ctrl_vp9_frame *dec_params;
	struct rkvdec_decoded_buffer *ref_bufs[3];
	struct rkvdec_decoded_buffer *dst, *last, *mv_ref;
	struct rkvdec_vp9_ctx *vp9_ctx = ctx->priv;
	u32 val, last_frame_info = 0;
	const struct v4l2_vp9_segmentation *seg;
	struct rkvdec_dev *rkvdec = ctx->dev;
	dma_addr_t addr;
	bool intra_only;
	unsigned int i;

	dec_params = run->decode_params;
	dst = vb2_to_rkvdec_decoded_buf(&run->base.bufs.dst->vb2_buf);
	ref_bufs[0] = get_ref_buf(ctx, &dst->base.vb, dec_params->last_frame_ts);
	ref_bufs[1] = get_ref_buf(ctx, &dst->base.vb, dec_params->golden_frame_ts);
	ref_bufs[2] = get_ref_buf(ctx, &dst->base.vb, dec_params->alt_frame_ts);

	if (vp9_ctx->last.valid)
		last = get_ref_buf(ctx, &dst->base.vb, vp9_ctx->last.timestamp);
	else
		last = dst;

	update_dec_buf_info(dst, dec_params);
	update_ctx_cur_info(vp9_ctx, dst, dec_params);
	seg = &dec_params->seg;

	intra_only = !!(dec_params->flags &
			(V4L2_VP9_FRAME_FLAG_KEY_FRAME |
			 V4L2_VP9_FRAME_FLAG_INTRA_ONLY));

	writel_relaxed(RKVDEC_MODE(RKVDEC_MODE_VP9),
		       rkvdec->regs + RKVDEC_REG_SYSCTRL);

	bit_depth = dec_params->bit_depth;
	aligned_height = round_up(ctx->decoded_fmt.fmt.pix_mp.height, 64);

	aligned_pitch = round_up(ctx->decoded_fmt.fmt.pix_mp.width *
				 bit_depth,
				 512) / 8;
	y_len = aligned_height * aligned_pitch;
	uv_len = y_len / 2;
	yuv_len = y_len + uv_len;

	writel_relaxed(RKVDEC_Y_HOR_VIRSTRIDE(aligned_pitch / 16) |
		       RKVDEC_UV_HOR_VIRSTRIDE(aligned_pitch / 16),
		       rkvdec->regs + RKVDEC_REG_PICPAR);
	writel_relaxed(RKVDEC_Y_VIRSTRIDE(y_len / 16),
		       rkvdec->regs + RKVDEC_REG_Y_VIRSTRIDE);
	writel_relaxed(RKVDEC_YUV_VIRSTRIDE(yuv_len / 16),
		       rkvdec->regs + RKVDEC_REG_YUV_VIRSTRIDE);

	stream_len = vb2_get_plane_payload(&run->base.bufs.src->vb2_buf, 0);
	writel_relaxed(RKVDEC_STRM_LEN(stream_len),
		       rkvdec->regs + RKVDEC_REG_STRM_LEN);

	/*
	 * Reset count buffer, because decoder only output intra related syntax
	 * counts when decoding intra frame, but update entropy need to update
	 * all the probabilities.
	 */
	if (intra_only)
		memset(vp9_ctx->count_tbl.cpu, 0, vp9_ctx->count_tbl.size);

	vp9_ctx->cur.segmapid = vp9_ctx->last.segmapid;
	if (!intra_only &&
	    !(dec_params->flags & V4L2_VP9_FRAME_FLAG_ERROR_RESILIENT) &&
	    (!(seg->flags & V4L2_VP9_SEGMENTATION_FLAG_ENABLED) ||
	     (seg->flags & V4L2_VP9_SEGMENTATION_FLAG_UPDATE_MAP)))
		vp9_ctx->cur.segmapid++;

	for (i = 0; i < ARRAY_SIZE(ref_bufs); i++)
		config_ref_registers(ctx, run, ref_bufs[i], &ref_regs[i]);

	for (i = 0; i < 8; i++)
		config_seg_registers(ctx, i);

	writel_relaxed(RKVDEC_VP9_TX_MODE(vp9_ctx->cur.tx_mode) |
		       RKVDEC_VP9_FRAME_REF_MODE(dec_params->reference_mode),
		       rkvdec->regs + RKVDEC_VP9_CPRHEADER_CONFIG);

	if (!intra_only) {
		const struct v4l2_vp9_loop_filter *lf;
		s8 delta;

		if (vp9_ctx->last.valid)
			lf = &vp9_ctx->last.lf;
		else
			lf = &vp9_ctx->cur.lf;

		val = 0;
		for (i = 0; i < ARRAY_SIZE(lf->ref_deltas); i++) {
			delta = lf->ref_deltas[i];
			val |= RKVDEC_REF_DELTAS_LASTFRAME(i, delta);
		}

		writel_relaxed(val,
			       rkvdec->regs + RKVDEC_VP9_REF_DELTAS_LASTFRAME);

		for (i = 0; i < ARRAY_SIZE(lf->mode_deltas); i++) {
			delta = lf->mode_deltas[i];
			last_frame_info |= RKVDEC_MODE_DELTAS_LASTFRAME(i,
									delta);
		}
	}

	if (vp9_ctx->last.valid && !intra_only &&
	    vp9_ctx->last.seg.flags & V4L2_VP9_SEGMENTATION_FLAG_ENABLED)
		last_frame_info |= RKVDEC_SEG_EN_LASTFRAME;

	if (vp9_ctx->last.valid &&
	    vp9_ctx->last.flags & V4L2_VP9_FRAME_FLAG_SHOW_FRAME)
		last_frame_info |= RKVDEC_LAST_SHOW_FRAME;

	if (vp9_ctx->last.valid &&
	    vp9_ctx->last.flags &
	    (V4L2_VP9_FRAME_FLAG_KEY_FRAME | V4L2_VP9_FRAME_FLAG_INTRA_ONLY))
		last_frame_info |= RKVDEC_LAST_INTRA_ONLY;

	if (vp9_ctx->last.valid &&
	    last->vp9.width == dst->vp9.width &&
	    last->vp9.height == dst->vp9.height)
		last_frame_info |= RKVDEC_LAST_WIDHHEIGHT_EQCUR;

	writel_relaxed(last_frame_info,
		       rkvdec->regs + RKVDEC_VP9_INFO_LASTFRAME);

	writel_relaxed(stream_len - dec_params->compressed_header_size -
		       dec_params->uncompressed_header_size,
		       rkvdec->regs + RKVDEC_VP9_LASTTILE_SIZE);

	for (i = 0; !intra_only && i < ARRAY_SIZE(ref_bufs); i++) {
		unsigned int refw = ref_bufs[i]->vp9.width;
		unsigned int refh = ref_bufs[i]->vp9.height;
		u32 hscale, vscale;

		hscale = (refw << 14) /	dst->vp9.width;
		vscale = (refh << 14) / dst->vp9.height;
		writel_relaxed(RKVDEC_VP9_REF_HOR_SCALE(hscale) |
			       RKVDEC_VP9_REF_VER_SCALE(vscale),
			       rkvdec->regs + RKVDEC_VP9_REF_SCALE(i));
	}

	addr = vb2_dma_contig_plane_dma_addr(&dst->base.vb.vb2_buf, 0);
	writel_relaxed(addr, rkvdec->regs + RKVDEC_REG_DECOUT_BASE);
	addr = vb2_dma_contig_plane_dma_addr(&run->base.bufs.src->vb2_buf, 0);
	writel_relaxed(addr, rkvdec->regs + RKVDEC_REG_STRM_RLC_BASE);
	writel_relaxed(vp9_ctx->priv_tbl.dma +
		       offsetof(struct rkvdec_vp9_priv_tbl, probs),
		       rkvdec->regs + RKVDEC_REG_CABACTBL_PROB_BASE);
	writel_relaxed(vp9_ctx->count_tbl.dma,
		       rkvdec->regs + RKVDEC_REG_VP9COUNT_BASE);

	writel_relaxed(vp9_ctx->priv_tbl.dma +
		       offsetof(struct rkvdec_vp9_priv_tbl, segmap) +
		       (RKVDEC_VP9_MAX_SEGMAP_SIZE * vp9_ctx->cur.segmapid),
		       rkvdec->regs + RKVDEC_REG_VP9_SEGIDCUR_BASE);
	writel_relaxed(vp9_ctx->priv_tbl.dma +
		       offsetof(struct rkvdec_vp9_priv_tbl, segmap) +
		       (RKVDEC_VP9_MAX_SEGMAP_SIZE * (!vp9_ctx->cur.segmapid)),
		       rkvdec->regs + RKVDEC_REG_VP9_SEGIDLAST_BASE);

	if (!intra_only &&
	    !(dec_params->flags & V4L2_VP9_FRAME_FLAG_ERROR_RESILIENT) &&
	    vp9_ctx->last.valid)
		mv_ref = last;
	else
		mv_ref = dst;

	writel_relaxed(get_mv_base_addr(mv_ref),
		       rkvdec->regs + RKVDEC_VP9_REF_COLMV_BASE);

	writel_relaxed(ctx->decoded_fmt.fmt.pix_mp.width |
		       (ctx->decoded_fmt.fmt.pix_mp.height << 16),
		       rkvdec->regs + RKVDEC_REG_PERFORMANCE_CYCLE);
}

static int validate_dec_params(struct rkvdec_ctx *ctx,
			       const struct v4l2_ctrl_vp9_frame *dec_params)
{
	unsigned int aligned_width, aligned_height;

	/* We only support profile 0. */
	if (dec_params->profile != 0) {
		dev_err(ctx->dev->dev, "unsupported profile %d\n",
			dec_params->profile);
		return -EINVAL;
	}

	aligned_width = round_up(dec_params->frame_width_minus_1 + 1, 64);
	aligned_height = round_up(dec_params->frame_height_minus_1 + 1, 64);

	/*
	 * Userspace should update the capture/decoded format when the
	 * resolution changes.
	 */
	if (aligned_width != ctx->decoded_fmt.fmt.pix_mp.width ||
	    aligned_height != ctx->decoded_fmt.fmt.pix_mp.height) {
		dev_err(ctx->dev->dev,
			"unexpected bitstream resolution %dx%d\n",
			dec_params->frame_width_minus_1 + 1,
			dec_params->frame_height_minus_1 + 1);
		return -EINVAL;
	}

	return 0;
}

static int rkvdec_vp9_run_preamble(struct rkvdec_ctx *ctx,
				   struct rkvdec_vp9_run *run)
{
	const struct v4l2_ctrl_vp9_frame *dec_params;
	const struct v4l2_ctrl_vp9_compressed_hdr *prob_updates;
	struct rkvdec_vp9_ctx *vp9_ctx = ctx->priv;
	struct v4l2_ctrl *ctrl;
	unsigned int fctx_idx;
	int ret;

	/* v4l2-specific stuff */
	rkvdec_run_preamble(ctx, &run->base);

	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_VP9_FRAME);
	if (WARN_ON(!ctrl))
		return -EINVAL;
	dec_params = ctrl->p_cur.p;

	ret = validate_dec_params(ctx, dec_params);
	if (ret)
		return ret;

	run->decode_params = dec_params;

	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl, V4L2_CID_STATELESS_VP9_COMPRESSED_HDR);
	if (WARN_ON(!ctrl))
		return -EINVAL;
	prob_updates = ctrl->p_cur.p;
	vp9_ctx->cur.tx_mode = prob_updates->tx_mode;

	/*
	 * vp9 stuff
	 *
	 * by this point the userspace has done all parts of 6.2 uncompressed_header()
	 * except this fragment:
	 * if ( FrameIsIntra || error_resilient_mode ) {
	 *	setup_past_independence ( )
	 *	if ( frame_type == KEY_FRAME || error_resilient_mode == 1 ||
	 *	     reset_frame_context == 3 ) {
	 *		for ( i = 0; i < 4; i ++ ) {
	 *			save_probs( i )
	 *		}
	 *	} else if ( reset_frame_context == 2 ) {
	 *		save_probs( frame_context_idx )
	 *	}
	 *	frame_context_idx = 0
	 * }
	 */
	fctx_idx = v4l2_vp9_reset_frame_ctx(dec_params, vp9_ctx->frame_context);
	vp9_ctx->cur.frame_context_idx = fctx_idx;

	/* 6.1 frame(sz): load_probs() and load_probs2() */
	vp9_ctx->probability_tables = vp9_ctx->frame_context[fctx_idx];

	/*
	 * The userspace has also performed 6.3 compressed_header(), but handling the
	 * probs in a special way. All probs which need updating, except MV-related,
	 * have been read from the bitstream and translated through inv_map_table[],
	 * but no 6.3.6 inv_recenter_nonneg(v, m) has been performed. The values passed
	 * by userspace are either translated values (there are no 0 values in
	 * inv_map_table[]), or zero to indicate no update. All MV-related probs which need
	 * updating have been read from the bitstream and (mv_prob << 1) | 1 has been
	 * performed. The values passed by userspace are either new values
	 * to replace old ones (the above mentioned shift and bitwise or never result in
	 * a zero) or zero to indicate no update.
	 * fw_update_probs() performs actual probs updates or leaves probs as-is
	 * for values for which a zero was passed from userspace.
	 */
	v4l2_vp9_fw_update_probs(&vp9_ctx->probability_tables, prob_updates, dec_params);

	return 0;
}

static int rkvdec_vp9_run(struct rkvdec_ctx *ctx)
{
	struct rkvdec_dev *rkvdec = ctx->dev;
	struct rkvdec_vp9_run run = { };
	int ret;

	ret = rkvdec_vp9_run_preamble(ctx, &run);
	if (ret) {
		rkvdec_run_postamble(ctx, &run.base);
		return ret;
	}

	/* Prepare probs. */
	init_probs(ctx, &run);

	/* Configure hardware registers. */
	config_registers(ctx, &run);

	rkvdec_run_postamble(ctx, &run.base);

	schedule_delayed_work(&rkvdec->watchdog_work, msecs_to_jiffies(2000));

	writel(1, rkvdec->regs + RKVDEC_REG_PREF_LUMA_CACHE_COMMAND);
	writel(1, rkvdec->regs + RKVDEC_REG_PREF_CHR_CACHE_COMMAND);

	writel(0xe, rkvdec->regs + RKVDEC_REG_STRMD_ERR_EN);
	/* Start decoding! */
	writel(RKVDEC_INTERRUPT_DEC_E | RKVDEC_CONFIG_DEC_CLK_GATE_E |
	       RKVDEC_TIMEOUT_E | RKVDEC_BUF_EMPTY_E,
	       rkvdec->regs + RKVDEC_REG_INTERRUPT);

	return 0;
}

#define copy_tx_and_skip(p1, p2)				\
do {								\
	memcpy((p1)->tx8, (p2)->tx8, sizeof((p1)->tx8));	\
	memcpy((p1)->tx16, (p2)->tx16, sizeof((p1)->tx16));	\
	memcpy((p1)->tx32, (p2)->tx32, sizeof((p1)->tx32));	\
	memcpy((p1)->skip, (p2)->skip, sizeof((p1)->skip));	\
} while (0)

static void rkvdec_vp9_done(struct rkvdec_ctx *ctx,
			    struct vb2_v4l2_buffer *src_buf,
			    struct vb2_v4l2_buffer *dst_buf,
			    enum vb2_buffer_state result)
{
	struct rkvdec_vp9_ctx *vp9_ctx = ctx->priv;
	unsigned int fctx_idx;

	/* v4l2-specific stuff */
	if (result == VB2_BUF_STATE_ERROR)
		goto out_update_last;

	/*
	 * vp9 stuff
	 *
	 * 6.1.2 refresh_probs()
	 *
	 * In the spec a complementary condition goes last in 6.1.2 refresh_probs(),
	 * but it makes no sense to perform all the activities from the first "if"
	 * there if we actually are not refreshing the frame context. On top of that,
	 * because of 6.2 uncompressed_header() whenever error_resilient_mode == 1,
	 * refresh_frame_context == 0. Consequently, if we don't jump to out_update_last
	 * it means error_resilient_mode must be 0.
	 */
	if (!(vp9_ctx->cur.flags & V4L2_VP9_FRAME_FLAG_REFRESH_FRAME_CTX))
		goto out_update_last;

	fctx_idx = vp9_ctx->cur.frame_context_idx;

	if (!(vp9_ctx->cur.flags & V4L2_VP9_FRAME_FLAG_PARALLEL_DEC_MODE)) {
		/* error_resilient_mode == 0 && frame_parallel_decoding_mode == 0 */
		struct v4l2_vp9_frame_context *probs = &vp9_ctx->probability_tables;
		bool frame_is_intra = vp9_ctx->cur.flags &
		    (V4L2_VP9_FRAME_FLAG_KEY_FRAME | V4L2_VP9_FRAME_FLAG_INTRA_ONLY);
		struct tx_and_skip {
			u8 tx8[2][1];
			u8 tx16[2][2];
			u8 tx32[2][3];
			u8 skip[3];
		} _tx_skip, *tx_skip = &_tx_skip;
		struct v4l2_vp9_frame_symbol_counts *counts;

		/* buffer the forward-updated TX and skip probs */
		if (frame_is_intra)
			copy_tx_and_skip(tx_skip, probs);

		/* 6.1.2 refresh_probs(): load_probs() and load_probs2() */
		*probs = vp9_ctx->frame_context[fctx_idx];

		/* if FrameIsIntra then undo the effect of load_probs2() */
		if (frame_is_intra)
			copy_tx_and_skip(probs, tx_skip);

		counts = frame_is_intra ? &vp9_ctx->intra_cnts : &vp9_ctx->inter_cnts;
		v4l2_vp9_adapt_coef_probs(probs, counts,
					  !vp9_ctx->last.valid ||
					  vp9_ctx->last.flags & V4L2_VP9_FRAME_FLAG_KEY_FRAME,
					  frame_is_intra);
		if (!frame_is_intra) {
			const struct rkvdec_vp9_inter_frame_symbol_counts *inter_cnts;
			u32 classes[2][11];
			int i;

			inter_cnts = vp9_ctx->count_tbl.cpu;
			for (i = 0; i < ARRAY_SIZE(classes); ++i)
				memcpy(classes[i], inter_cnts->classes[i], sizeof(classes[0]));
			counts->classes = &classes;

			/* load_probs2() already done */
			v4l2_vp9_adapt_noncoef_probs(&vp9_ctx->probability_tables, counts,
						     vp9_ctx->cur.reference_mode,
						     vp9_ctx->cur.interpolation_filter,
						     vp9_ctx->cur.tx_mode, vp9_ctx->cur.flags);
		}
	}

	/* 6.1.2 refresh_probs(): save_probs(fctx_idx) */
	vp9_ctx->frame_context[fctx_idx] = vp9_ctx->probability_tables;

out_update_last:
	update_ctx_last_info(vp9_ctx);
}

static void rkvdec_init_v4l2_vp9_count_tbl(struct rkvdec_ctx *ctx)
{
	struct rkvdec_vp9_ctx *vp9_ctx = ctx->priv;
	struct rkvdec_vp9_intra_frame_symbol_counts *intra_cnts = vp9_ctx->count_tbl.cpu;
	struct rkvdec_vp9_inter_frame_symbol_counts *inter_cnts = vp9_ctx->count_tbl.cpu;
	int i, j, k, l, m;

	vp9_ctx->inter_cnts.partition = &inter_cnts->partition;
	vp9_ctx->inter_cnts.skip = &inter_cnts->skip;
	vp9_ctx->inter_cnts.intra_inter = &inter_cnts->inter;
	vp9_ctx->inter_cnts.tx32p = &inter_cnts->tx32p;
	vp9_ctx->inter_cnts.tx16p = &inter_cnts->tx16p;
	vp9_ctx->inter_cnts.tx8p = &inter_cnts->tx8p;

	vp9_ctx->intra_cnts.partition = (u32 (*)[16][4])(&intra_cnts->partition);
	vp9_ctx->intra_cnts.skip = &intra_cnts->skip;
	vp9_ctx->intra_cnts.intra_inter = &intra_cnts->intra;
	vp9_ctx->intra_cnts.tx32p = &intra_cnts->tx32p;
	vp9_ctx->intra_cnts.tx16p = &intra_cnts->tx16p;
	vp9_ctx->intra_cnts.tx8p = &intra_cnts->tx8p;

	vp9_ctx->inter_cnts.y_mode = &inter_cnts->y_mode;
	vp9_ctx->inter_cnts.uv_mode = &inter_cnts->uv_mode;
	vp9_ctx->inter_cnts.comp = &inter_cnts->comp;
	vp9_ctx->inter_cnts.comp_ref = &inter_cnts->comp_ref;
	vp9_ctx->inter_cnts.single_ref = &inter_cnts->single_ref;
	vp9_ctx->inter_cnts.mv_mode = &inter_cnts->mv_mode;
	vp9_ctx->inter_cnts.filter = &inter_cnts->filter;
	vp9_ctx->inter_cnts.mv_joint = &inter_cnts->mv_joint;
	vp9_ctx->inter_cnts.sign = &inter_cnts->sign;
	/*
	 * rk hardware actually uses "u32 classes[2][11 + 1];"
	 * instead of "u32 classes[2][11];", so this must be explicitly
	 * copied into vp9_ctx->classes when passing the data to the
	 * vp9 library function
	 */
	vp9_ctx->inter_cnts.class0 = &inter_cnts->class0;
	vp9_ctx->inter_cnts.bits = &inter_cnts->bits;
	vp9_ctx->inter_cnts.class0_fp = &inter_cnts->class0_fp;
	vp9_ctx->inter_cnts.fp = &inter_cnts->fp;
	vp9_ctx->inter_cnts.class0_hp = &inter_cnts->class0_hp;
	vp9_ctx->inter_cnts.hp = &inter_cnts->hp;

#define INNERMOST_LOOP \
	do {										\
		for (m = 0; m < ARRAY_SIZE(vp9_ctx->inter_cnts.coeff[0][0][0][0]); ++m) {\
			vp9_ctx->inter_cnts.coeff[i][j][k][l][m] =			\
				&inter_cnts->ref_cnt[k][i][j][l][m].coeff;		\
			vp9_ctx->inter_cnts.eob[i][j][k][l][m][0] =			\
				&inter_cnts->ref_cnt[k][i][j][l][m].eob[0];		\
			vp9_ctx->inter_cnts.eob[i][j][k][l][m][1] =			\
				&inter_cnts->ref_cnt[k][i][j][l][m].eob[1];		\
											\
			vp9_ctx->intra_cnts.coeff[i][j][k][l][m] =			\
				&intra_cnts->ref_cnt[k][i][j][l][m].coeff;		\
			vp9_ctx->intra_cnts.eob[i][j][k][l][m][0] =			\
				&intra_cnts->ref_cnt[k][i][j][l][m].eob[0];		\
			vp9_ctx->intra_cnts.eob[i][j][k][l][m][1] =			\
				&intra_cnts->ref_cnt[k][i][j][l][m].eob[1];		\
		}									\
	} while (0)

	for (i = 0; i < ARRAY_SIZE(vp9_ctx->inter_cnts.coeff); ++i)
		for (j = 0; j < ARRAY_SIZE(vp9_ctx->inter_cnts.coeff[0]); ++j)
			for (k = 0; k < ARRAY_SIZE(vp9_ctx->inter_cnts.coeff[0][0]); ++k)
				for (l = 0; l < ARRAY_SIZE(vp9_ctx->inter_cnts.coeff[0][0][0]); ++l)
					INNERMOST_LOOP;
#undef INNERMOST_LOOP
}

static int rkvdec_vp9_start(struct rkvdec_ctx *ctx)
{
	struct rkvdec_dev *rkvdec = ctx->dev;
	struct rkvdec_vp9_priv_tbl *priv_tbl;
	struct rkvdec_vp9_ctx *vp9_ctx;
	unsigned char *count_tbl;
	int ret;

	vp9_ctx = kzalloc(sizeof(*vp9_ctx), GFP_KERNEL);
	if (!vp9_ctx)
		return -ENOMEM;

	ctx->priv = vp9_ctx;

	BUILD_BUG_ON(sizeof(priv_tbl->probs) % 16); /* ensure probs size is 128-bit aligned */
	priv_tbl = dma_alloc_coherent(rkvdec->dev, sizeof(*priv_tbl),
				      &vp9_ctx->priv_tbl.dma, GFP_KERNEL);
	if (!priv_tbl) {
		ret = -ENOMEM;
		goto err_free_ctx;
	}

	vp9_ctx->priv_tbl.size = sizeof(*priv_tbl);
	vp9_ctx->priv_tbl.cpu = priv_tbl;

	count_tbl = dma_alloc_coherent(rkvdec->dev, RKVDEC_VP9_COUNT_SIZE,
				       &vp9_ctx->count_tbl.dma, GFP_KERNEL);
	if (!count_tbl) {
		ret = -ENOMEM;
		goto err_free_priv_tbl;
	}

	vp9_ctx->count_tbl.size = RKVDEC_VP9_COUNT_SIZE;
	vp9_ctx->count_tbl.cpu = count_tbl;
	rkvdec_init_v4l2_vp9_count_tbl(ctx);

	return 0;

err_free_priv_tbl:
	dma_free_coherent(rkvdec->dev, vp9_ctx->priv_tbl.size,
			  vp9_ctx->priv_tbl.cpu, vp9_ctx->priv_tbl.dma);

err_free_ctx:
	kfree(vp9_ctx);
	return ret;
}

static void rkvdec_vp9_stop(struct rkvdec_ctx *ctx)
{
	struct rkvdec_vp9_ctx *vp9_ctx = ctx->priv;
	struct rkvdec_dev *rkvdec = ctx->dev;

	dma_free_coherent(rkvdec->dev, vp9_ctx->count_tbl.size,
			  vp9_ctx->count_tbl.cpu, vp9_ctx->count_tbl.dma);
	dma_free_coherent(rkvdec->dev, vp9_ctx->priv_tbl.size,
			  vp9_ctx->priv_tbl.cpu, vp9_ctx->priv_tbl.dma);
	kfree(vp9_ctx);
}

static int rkvdec_vp9_adjust_fmt(struct rkvdec_ctx *ctx,
				 struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *fmt = &f->fmt.pix_mp;

	fmt->num_planes = 1;
	if (!fmt->plane_fmt[0].sizeimage)
		fmt->plane_fmt[0].sizeimage = fmt->width * fmt->height * 2;
	return 0;
}

const struct rkvdec_coded_fmt_ops rkvdec_vp9_fmt_ops = {
	.adjust_fmt = rkvdec_vp9_adjust_fmt,
	.start = rkvdec_vp9_start,
	.stop = rkvdec_vp9_stop,
	.run = rkvdec_vp9_run,
	.done = rkvdec_vp9_done,
};
