// SPDX-License-Identifier: GPL-2.0
/*
 * Hantro VP9 codec driver
 *
 * Copyright (C) 2021 Collabora Ltd.
 */
#include "media/videobuf2-core.h"
#include "media/videobuf2-dma-contig.h"
#include "media/videobuf2-v4l2.h"
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-vp9.h>

#include "hantro.h"
#include "hantro_vp9.h"
#include "hantro_g2_regs.h"

#define G2_ALIGN 16

enum hantro_ref_frames {
	INTRA_FRAME = 0,
	LAST_FRAME = 1,
	GOLDEN_FRAME = 2,
	ALTREF_FRAME = 3,
	MAX_REF_FRAMES = 4
};

static int start_prepare_run(struct hantro_ctx *ctx, const struct v4l2_ctrl_vp9_frame **dec_params)
{
	const struct v4l2_ctrl_vp9_compressed_hdr *prob_updates;
	struct hantro_vp9_dec_hw_ctx *vp9_ctx = &ctx->vp9_dec;
	struct v4l2_ctrl *ctrl;
	unsigned int fctx_idx;

	/* v4l2-specific stuff */
	hantro_start_prepare_run(ctx);

	ctrl = v4l2_ctrl_find(&ctx->ctrl_handler, V4L2_CID_STATELESS_VP9_FRAME);
	if (WARN_ON(!ctrl))
		return -EINVAL;
	*dec_params = ctrl->p_cur.p;

	ctrl = v4l2_ctrl_find(&ctx->ctrl_handler, V4L2_CID_STATELESS_VP9_COMPRESSED_HDR);
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
	fctx_idx = v4l2_vp9_reset_frame_ctx(*dec_params, vp9_ctx->frame_context);
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
	v4l2_vp9_fw_update_probs(&vp9_ctx->probability_tables, prob_updates, *dec_params);

	return 0;
}

static size_t chroma_offset(const struct hantro_ctx *ctx,
			    const struct v4l2_ctrl_vp9_frame *dec_params)
{
	int bytes_per_pixel = dec_params->bit_depth == 8 ? 1 : 2;

	return ctx->src_fmt.width * ctx->src_fmt.height * bytes_per_pixel;
}

static size_t mv_offset(const struct hantro_ctx *ctx,
			const struct v4l2_ctrl_vp9_frame *dec_params)
{
	size_t cr_offset = chroma_offset(ctx, dec_params);

	return ALIGN((cr_offset * 3) / 2, G2_ALIGN);
}

static struct hantro_decoded_buffer *
get_ref_buf(struct hantro_ctx *ctx, struct vb2_v4l2_buffer *dst, u64 timestamp)
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

	return vb2_to_hantro_decoded_buf(buf);
}

static void update_dec_buf_info(struct hantro_decoded_buffer *buf,
				const struct v4l2_ctrl_vp9_frame *dec_params)
{
	buf->vp9.width = dec_params->frame_width_minus_1 + 1;
	buf->vp9.height = dec_params->frame_height_minus_1 + 1;
	buf->vp9.bit_depth = dec_params->bit_depth;
}

static void update_ctx_cur_info(struct hantro_vp9_dec_hw_ctx *vp9_ctx,
				struct hantro_decoded_buffer *buf,
				const struct v4l2_ctrl_vp9_frame *dec_params)
{
	vp9_ctx->cur.valid = true;
	vp9_ctx->cur.reference_mode = dec_params->reference_mode;
	vp9_ctx->cur.interpolation_filter = dec_params->interpolation_filter;
	vp9_ctx->cur.flags = dec_params->flags;
	vp9_ctx->cur.timestamp = buf->base.vb.vb2_buf.timestamp;
}

static void config_output(struct hantro_ctx *ctx,
			  struct hantro_decoded_buffer *dst,
			  const struct v4l2_ctrl_vp9_frame *dec_params)
{
	dma_addr_t luma_addr, chroma_addr, mv_addr;

	hantro_reg_write(ctx->dev, &g2_out_dis, 0);
	if (!ctx->dev->variant->legacy_regs)
		hantro_reg_write(ctx->dev, &g2_output_format, 0);

	luma_addr = hantro_get_dec_buf_addr(ctx, &dst->base.vb.vb2_buf);
	hantro_write_addr(ctx->dev, G2_OUT_LUMA_ADDR, luma_addr);

	chroma_addr = luma_addr + chroma_offset(ctx, dec_params);
	hantro_write_addr(ctx->dev, G2_OUT_CHROMA_ADDR, chroma_addr);

	mv_addr = luma_addr + mv_offset(ctx, dec_params);
	hantro_write_addr(ctx->dev, G2_OUT_MV_ADDR, mv_addr);
}

struct hantro_vp9_ref_reg {
	const struct hantro_reg width;
	const struct hantro_reg height;
	const struct hantro_reg hor_scale;
	const struct hantro_reg ver_scale;
	u32 y_base;
	u32 c_base;
};

static void config_ref(struct hantro_ctx *ctx,
		       struct hantro_decoded_buffer *dst,
		       const struct hantro_vp9_ref_reg *ref_reg,
		       const struct v4l2_ctrl_vp9_frame *dec_params,
		       u64 ref_ts)
{
	struct hantro_decoded_buffer *buf;
	dma_addr_t luma_addr, chroma_addr;
	u32 refw, refh;

	buf = get_ref_buf(ctx, &dst->base.vb, ref_ts);
	refw = buf->vp9.width;
	refh = buf->vp9.height;

	hantro_reg_write(ctx->dev, &ref_reg->width, refw);
	hantro_reg_write(ctx->dev, &ref_reg->height, refh);

	hantro_reg_write(ctx->dev, &ref_reg->hor_scale, (refw << 14) / dst->vp9.width);
	hantro_reg_write(ctx->dev, &ref_reg->ver_scale, (refh << 14) / dst->vp9.height);

	luma_addr = hantro_get_dec_buf_addr(ctx, &buf->base.vb.vb2_buf);
	hantro_write_addr(ctx->dev, ref_reg->y_base, luma_addr);

	chroma_addr = luma_addr + chroma_offset(ctx, dec_params);
	hantro_write_addr(ctx->dev, ref_reg->c_base, chroma_addr);
}

static void config_ref_registers(struct hantro_ctx *ctx,
				 const struct v4l2_ctrl_vp9_frame *dec_params,
				 struct hantro_decoded_buffer *dst,
				 struct hantro_decoded_buffer *mv_ref)
{
	static const struct hantro_vp9_ref_reg ref_regs[] = {
		{
			/* Last */
			.width = vp9_lref_width,
			.height = vp9_lref_height,
			.hor_scale = vp9_lref_hor_scale,
			.ver_scale = vp9_lref_ver_scale,
			.y_base = G2_REF_LUMA_ADDR(0),
			.c_base = G2_REF_CHROMA_ADDR(0),
		}, {
			/* Golden */
			.width = vp9_gref_width,
			.height = vp9_gref_height,
			.hor_scale = vp9_gref_hor_scale,
			.ver_scale = vp9_gref_ver_scale,
			.y_base = G2_REF_LUMA_ADDR(4),
			.c_base = G2_REF_CHROMA_ADDR(4),
		}, {
			/* Altref */
			.width = vp9_aref_width,
			.height = vp9_aref_height,
			.hor_scale = vp9_aref_hor_scale,
			.ver_scale = vp9_aref_ver_scale,
			.y_base = G2_REF_LUMA_ADDR(5),
			.c_base = G2_REF_CHROMA_ADDR(5),
		},
	};
	dma_addr_t mv_addr;

	config_ref(ctx, dst, &ref_regs[0], dec_params, dec_params->last_frame_ts);
	config_ref(ctx, dst, &ref_regs[1], dec_params, dec_params->golden_frame_ts);
	config_ref(ctx, dst, &ref_regs[2], dec_params, dec_params->alt_frame_ts);

	mv_addr = hantro_get_dec_buf_addr(ctx, &mv_ref->base.vb.vb2_buf) +
		  mv_offset(ctx, dec_params);
	hantro_write_addr(ctx->dev, G2_REF_MV_ADDR(0), mv_addr);

	hantro_reg_write(ctx->dev, &vp9_last_sign_bias,
			 dec_params->ref_frame_sign_bias & V4L2_VP9_SIGN_BIAS_LAST ? 1 : 0);

	hantro_reg_write(ctx->dev, &vp9_gref_sign_bias,
			 dec_params->ref_frame_sign_bias & V4L2_VP9_SIGN_BIAS_GOLDEN ? 1 : 0);

	hantro_reg_write(ctx->dev, &vp9_aref_sign_bias,
			 dec_params->ref_frame_sign_bias & V4L2_VP9_SIGN_BIAS_ALT ? 1 : 0);
}

static void recompute_tile_info(unsigned short *tile_info, unsigned int tiles, unsigned int sbs)
{
	int i;
	unsigned int accumulated = 0;
	unsigned int next_accumulated;

	for (i = 1; i <= tiles; ++i) {
		next_accumulated = i * sbs / tiles;
		*tile_info++ = next_accumulated - accumulated;
		accumulated = next_accumulated;
	}
}

static void
recompute_tile_rc_info(struct hantro_ctx *ctx,
		       unsigned int tile_r, unsigned int tile_c,
		       unsigned int sbs_r, unsigned int sbs_c)
{
	struct hantro_vp9_dec_hw_ctx *vp9_ctx = &ctx->vp9_dec;

	recompute_tile_info(vp9_ctx->tile_r_info, tile_r, sbs_r);
	recompute_tile_info(vp9_ctx->tile_c_info, tile_c, sbs_c);

	vp9_ctx->last_tile_r = tile_r;
	vp9_ctx->last_tile_c = tile_c;
	vp9_ctx->last_sbs_r = sbs_r;
	vp9_ctx->last_sbs_c = sbs_c;
}

static inline unsigned int first_tile_row(unsigned int tile_r, unsigned int sbs_r)
{
	if (tile_r == sbs_r + 1)
		return 1;

	if (tile_r == sbs_r + 2)
		return 2;

	return 0;
}

static void
fill_tile_info(struct hantro_ctx *ctx,
	       unsigned int tile_r, unsigned int tile_c,
	       unsigned int sbs_r, unsigned int sbs_c,
	       unsigned short *tile_mem)
{
	struct hantro_vp9_dec_hw_ctx *vp9_ctx = &ctx->vp9_dec;
	unsigned int i, j;
	bool first = true;

	for (i = first_tile_row(tile_r, sbs_r); i < tile_r; ++i) {
		unsigned short r_info = vp9_ctx->tile_r_info[i];

		if (first) {
			if (i > 0)
				r_info += vp9_ctx->tile_r_info[0];
			if (i == 2)
				r_info += vp9_ctx->tile_r_info[1];
			first = false;
		}
		for (j = 0; j < tile_c; ++j) {
			*tile_mem++ = vp9_ctx->tile_c_info[j];
			*tile_mem++ = r_info;
		}
	}
}

static void
config_tiles(struct hantro_ctx *ctx,
	     const struct v4l2_ctrl_vp9_frame *dec_params,
	     struct hantro_decoded_buffer *dst)
{
	struct hantro_vp9_dec_hw_ctx *vp9_ctx = &ctx->vp9_dec;
	struct hantro_aux_buf *misc = &vp9_ctx->misc;
	struct hantro_aux_buf *tile_edge = &vp9_ctx->tile_edge;
	dma_addr_t addr;
	unsigned short *tile_mem;
	unsigned int rows, cols;

	addr = misc->dma + vp9_ctx->tile_info_offset;
	hantro_write_addr(ctx->dev, G2_TILE_SIZES_ADDR, addr);

	tile_mem = misc->cpu + vp9_ctx->tile_info_offset;
	if (dec_params->tile_cols_log2 || dec_params->tile_rows_log2) {
		unsigned int tile_r = (1 << dec_params->tile_rows_log2);
		unsigned int tile_c = (1 << dec_params->tile_cols_log2);
		unsigned int sbs_r = hantro_vp9_num_sbs(dst->vp9.height);
		unsigned int sbs_c = hantro_vp9_num_sbs(dst->vp9.width);

		if (tile_r != vp9_ctx->last_tile_r || tile_c != vp9_ctx->last_tile_c ||
		    sbs_r != vp9_ctx->last_sbs_r || sbs_c != vp9_ctx->last_sbs_c)
			recompute_tile_rc_info(ctx, tile_r, tile_c, sbs_r, sbs_c);

		fill_tile_info(ctx, tile_r, tile_c, sbs_r, sbs_c, tile_mem);

		cols = tile_c;
		rows = tile_r;
		hantro_reg_write(ctx->dev, &g2_tile_e, 1);
	} else {
		tile_mem[0] = hantro_vp9_num_sbs(dst->vp9.width);
		tile_mem[1] = hantro_vp9_num_sbs(dst->vp9.height);

		cols = 1;
		rows = 1;
		hantro_reg_write(ctx->dev, &g2_tile_e, 0);
	}

	if (ctx->dev->variant->legacy_regs) {
		hantro_reg_write(ctx->dev, &g2_num_tile_cols_old, cols);
		hantro_reg_write(ctx->dev, &g2_num_tile_rows_old, rows);
	} else {
		hantro_reg_write(ctx->dev, &g2_num_tile_cols, cols);
		hantro_reg_write(ctx->dev, &g2_num_tile_rows, rows);
	}

	/* provide aux buffers even if no tiles are used */
	addr = tile_edge->dma;
	hantro_write_addr(ctx->dev, G2_TILE_FILTER_ADDR, addr);

	addr = tile_edge->dma + vp9_ctx->bsd_ctrl_offset;
	hantro_write_addr(ctx->dev, G2_TILE_BSD_ADDR, addr);
}

static void
update_feat_and_flag(struct hantro_vp9_dec_hw_ctx *vp9_ctx,
		     const struct v4l2_vp9_segmentation *seg,
		     unsigned int feature,
		     unsigned int segid)
{
	u8 mask = V4L2_VP9_SEGMENT_FEATURE_ENABLED(feature);

	vp9_ctx->feature_data[segid][feature] = seg->feature_data[segid][feature];
	vp9_ctx->feature_enabled[segid] &= ~mask;
	vp9_ctx->feature_enabled[segid] |= (seg->feature_enabled[segid] & mask);
}

static inline s16 clip3(s16 x, s16 y, s16 z)
{
	return (z < x) ? x : (z > y) ? y : z;
}

static s16 feat_val_clip3(s16 feat_val, s16 feature_data, bool absolute, u8 clip)
{
	if (absolute)
		return feature_data;

	return clip3(0, 255, feat_val + feature_data);
}

static void config_segment(struct hantro_ctx *ctx, const struct v4l2_ctrl_vp9_frame *dec_params)
{
	struct hantro_vp9_dec_hw_ctx *vp9_ctx = &ctx->vp9_dec;
	const struct v4l2_vp9_segmentation *seg;
	s16 feat_val;
	unsigned char feat_id;
	unsigned int segid;
	bool segment_enabled, absolute, update_data;

	static const struct hantro_reg seg_regs[8][V4L2_VP9_SEG_LVL_MAX] = {
		{ vp9_quant_seg0, vp9_filt_level_seg0, vp9_refpic_seg0, vp9_skip_seg0 },
		{ vp9_quant_seg1, vp9_filt_level_seg1, vp9_refpic_seg1, vp9_skip_seg1 },
		{ vp9_quant_seg2, vp9_filt_level_seg2, vp9_refpic_seg2, vp9_skip_seg2 },
		{ vp9_quant_seg3, vp9_filt_level_seg3, vp9_refpic_seg3, vp9_skip_seg3 },
		{ vp9_quant_seg4, vp9_filt_level_seg4, vp9_refpic_seg4, vp9_skip_seg4 },
		{ vp9_quant_seg5, vp9_filt_level_seg5, vp9_refpic_seg5, vp9_skip_seg5 },
		{ vp9_quant_seg6, vp9_filt_level_seg6, vp9_refpic_seg6, vp9_skip_seg6 },
		{ vp9_quant_seg7, vp9_filt_level_seg7, vp9_refpic_seg7, vp9_skip_seg7 },
	};

	segment_enabled = !!(dec_params->seg.flags & V4L2_VP9_SEGMENTATION_FLAG_ENABLED);
	hantro_reg_write(ctx->dev, &vp9_segment_e, segment_enabled);
	hantro_reg_write(ctx->dev, &vp9_segment_upd_e,
			 !!(dec_params->seg.flags & V4L2_VP9_SEGMENTATION_FLAG_UPDATE_MAP));
	hantro_reg_write(ctx->dev, &vp9_segment_temp_upd_e,
			 !!(dec_params->seg.flags & V4L2_VP9_SEGMENTATION_FLAG_TEMPORAL_UPDATE));

	seg = &dec_params->seg;
	absolute = !!(seg->flags & V4L2_VP9_SEGMENTATION_FLAG_ABS_OR_DELTA_UPDATE);
	update_data = !!(seg->flags & V4L2_VP9_SEGMENTATION_FLAG_UPDATE_DATA);

	for (segid = 0; segid < 8; ++segid) {
		/* Quantizer segment feature */
		feat_id = V4L2_VP9_SEG_LVL_ALT_Q;
		feat_val = dec_params->quant.base_q_idx;
		if (segment_enabled) {
			if (update_data)
				update_feat_and_flag(vp9_ctx, seg, feat_id, segid);
			if (v4l2_vp9_seg_feat_enabled(vp9_ctx->feature_enabled, feat_id, segid))
				feat_val = feat_val_clip3(feat_val,
							  vp9_ctx->feature_data[segid][feat_id],
							  absolute, 255);
		}
		hantro_reg_write(ctx->dev, &seg_regs[segid][feat_id], feat_val);

		/* Loop filter segment feature */
		feat_id = V4L2_VP9_SEG_LVL_ALT_L;
		feat_val = dec_params->lf.level;
		if (segment_enabled) {
			if (update_data)
				update_feat_and_flag(vp9_ctx, seg, feat_id, segid);
			if (v4l2_vp9_seg_feat_enabled(vp9_ctx->feature_enabled, feat_id, segid))
				feat_val = feat_val_clip3(feat_val,
							  vp9_ctx->feature_data[segid][feat_id],
							  absolute, 63);
		}
		hantro_reg_write(ctx->dev, &seg_regs[segid][feat_id], feat_val);

		/* Reference frame segment feature */
		feat_id = V4L2_VP9_SEG_LVL_REF_FRAME;
		feat_val = 0;
		if (segment_enabled) {
			if (update_data)
				update_feat_and_flag(vp9_ctx, seg, feat_id, segid);
			if (!(dec_params->flags & V4L2_VP9_FRAME_FLAG_KEY_FRAME) &&
			    v4l2_vp9_seg_feat_enabled(vp9_ctx->feature_enabled, feat_id, segid))
				feat_val = vp9_ctx->feature_data[segid][feat_id] + 1;
		}
		hantro_reg_write(ctx->dev, &seg_regs[segid][feat_id], feat_val);

		/* Skip segment feature */
		feat_id = V4L2_VP9_SEG_LVL_SKIP;
		feat_val = 0;
		if (segment_enabled) {
			if (update_data)
				update_feat_and_flag(vp9_ctx, seg, feat_id, segid);
			feat_val = v4l2_vp9_seg_feat_enabled(vp9_ctx->feature_enabled,
							     feat_id, segid) ? 1 : 0;
		}
		hantro_reg_write(ctx->dev, &seg_regs[segid][feat_id], feat_val);
	}
}

static void config_loop_filter(struct hantro_ctx *ctx, const struct v4l2_ctrl_vp9_frame *dec_params)
{
	bool d = dec_params->lf.flags & V4L2_VP9_LOOP_FILTER_FLAG_DELTA_ENABLED;

	hantro_reg_write(ctx->dev, &vp9_filt_level, dec_params->lf.level);
	hantro_reg_write(ctx->dev, &g2_out_filtering_dis, dec_params->lf.level == 0);
	hantro_reg_write(ctx->dev, &vp9_filt_sharpness, dec_params->lf.sharpness);

	hantro_reg_write(ctx->dev, &vp9_filt_ref_adj_0, d ? dec_params->lf.ref_deltas[0] : 0);
	hantro_reg_write(ctx->dev, &vp9_filt_ref_adj_1, d ? dec_params->lf.ref_deltas[1] : 0);
	hantro_reg_write(ctx->dev, &vp9_filt_ref_adj_2, d ? dec_params->lf.ref_deltas[2] : 0);
	hantro_reg_write(ctx->dev, &vp9_filt_ref_adj_3, d ? dec_params->lf.ref_deltas[3] : 0);
	hantro_reg_write(ctx->dev, &vp9_filt_mb_adj_0, d ? dec_params->lf.mode_deltas[0] : 0);
	hantro_reg_write(ctx->dev, &vp9_filt_mb_adj_1, d ? dec_params->lf.mode_deltas[1] : 0);
}

static void config_picture_dimensions(struct hantro_ctx *ctx, struct hantro_decoded_buffer *dst)
{
	u32 pic_w_4x4, pic_h_4x4;

	hantro_reg_write(ctx->dev, &g2_pic_width_in_cbs, (dst->vp9.width + 7) / 8);
	hantro_reg_write(ctx->dev, &g2_pic_height_in_cbs, (dst->vp9.height + 7) / 8);
	pic_w_4x4 = roundup(dst->vp9.width, 8) >> 2;
	pic_h_4x4 = roundup(dst->vp9.height, 8) >> 2;
	hantro_reg_write(ctx->dev, &g2_pic_width_4x4, pic_w_4x4);
	hantro_reg_write(ctx->dev, &g2_pic_height_4x4, pic_h_4x4);
}

static void
config_bit_depth(struct hantro_ctx *ctx, const struct v4l2_ctrl_vp9_frame *dec_params)
{
	if (ctx->dev->variant->legacy_regs) {
		hantro_reg_write(ctx->dev, &g2_bit_depth_y, dec_params->bit_depth);
		hantro_reg_write(ctx->dev, &g2_bit_depth_c, dec_params->bit_depth);
		hantro_reg_write(ctx->dev, &g2_pix_shift, 0);
	} else {
		hantro_reg_write(ctx->dev, &g2_bit_depth_y_minus8, dec_params->bit_depth - 8);
		hantro_reg_write(ctx->dev, &g2_bit_depth_c_minus8, dec_params->bit_depth - 8);
	}
}

static inline bool is_lossless(const struct v4l2_vp9_quantization *quant)
{
	return quant->base_q_idx == 0 && quant->delta_q_uv_ac == 0 &&
	       quant->delta_q_uv_dc == 0 && quant->delta_q_y_dc == 0;
}

static void
config_quant(struct hantro_ctx *ctx, const struct v4l2_ctrl_vp9_frame *dec_params)
{
	hantro_reg_write(ctx->dev, &vp9_qp_delta_y_dc, dec_params->quant.delta_q_y_dc);
	hantro_reg_write(ctx->dev, &vp9_qp_delta_ch_dc, dec_params->quant.delta_q_uv_dc);
	hantro_reg_write(ctx->dev, &vp9_qp_delta_ch_ac, dec_params->quant.delta_q_uv_ac);
	hantro_reg_write(ctx->dev, &vp9_lossless_e, is_lossless(&dec_params->quant));
}

static u32
hantro_interp_filter_from_v4l2(unsigned int interpolation_filter)
{
	switch (interpolation_filter) {
	case V4L2_VP9_INTERP_FILTER_EIGHTTAP:
		return 0x1;
	case V4L2_VP9_INTERP_FILTER_EIGHTTAP_SMOOTH:
		return 0;
	case V4L2_VP9_INTERP_FILTER_EIGHTTAP_SHARP:
		return 0x2;
	case V4L2_VP9_INTERP_FILTER_BILINEAR:
		return 0x3;
	case V4L2_VP9_INTERP_FILTER_SWITCHABLE:
		return 0x4;
	}

	return 0;
}

static void
config_others(struct hantro_ctx *ctx, const struct v4l2_ctrl_vp9_frame *dec_params,
	      bool intra_only, bool resolution_change)
{
	struct hantro_vp9_dec_hw_ctx *vp9_ctx = &ctx->vp9_dec;

	hantro_reg_write(ctx->dev, &g2_idr_pic_e, intra_only);

	hantro_reg_write(ctx->dev, &vp9_transform_mode, vp9_ctx->cur.tx_mode);

	hantro_reg_write(ctx->dev, &vp9_mcomp_filt_type, intra_only ?
		0 : hantro_interp_filter_from_v4l2(dec_params->interpolation_filter));

	hantro_reg_write(ctx->dev, &vp9_high_prec_mv_e,
			 !!(dec_params->flags & V4L2_VP9_FRAME_FLAG_ALLOW_HIGH_PREC_MV));

	hantro_reg_write(ctx->dev, &vp9_comp_pred_mode, dec_params->reference_mode);

	hantro_reg_write(ctx->dev, &g2_tempor_mvp_e,
			 !(dec_params->flags & V4L2_VP9_FRAME_FLAG_ERROR_RESILIENT) &&
			 !(dec_params->flags & V4L2_VP9_FRAME_FLAG_KEY_FRAME) &&
			 !(vp9_ctx->last.flags & V4L2_VP9_FRAME_FLAG_KEY_FRAME) &&
			 !(dec_params->flags & V4L2_VP9_FRAME_FLAG_INTRA_ONLY) &&
			 !resolution_change &&
			 vp9_ctx->last.flags & V4L2_VP9_FRAME_FLAG_SHOW_FRAME
	);

	hantro_reg_write(ctx->dev, &g2_write_mvs_e,
			 !(dec_params->flags & V4L2_VP9_FRAME_FLAG_KEY_FRAME));
}

static void
config_compound_reference(struct hantro_ctx *ctx,
			  const struct v4l2_ctrl_vp9_frame *dec_params)
{
	u32 comp_fixed_ref, comp_var_ref[2];
	bool last_ref_frame_sign_bias;
	bool golden_ref_frame_sign_bias;
	bool alt_ref_frame_sign_bias;
	bool comp_ref_allowed = 0;

	comp_fixed_ref = 0;
	comp_var_ref[0] = 0;
	comp_var_ref[1] = 0;

	last_ref_frame_sign_bias = dec_params->ref_frame_sign_bias & V4L2_VP9_SIGN_BIAS_LAST;
	golden_ref_frame_sign_bias = dec_params->ref_frame_sign_bias & V4L2_VP9_SIGN_BIAS_GOLDEN;
	alt_ref_frame_sign_bias = dec_params->ref_frame_sign_bias & V4L2_VP9_SIGN_BIAS_ALT;

	/* 6.3.12 Frame reference mode syntax */
	comp_ref_allowed |= golden_ref_frame_sign_bias != last_ref_frame_sign_bias;
	comp_ref_allowed |= alt_ref_frame_sign_bias != last_ref_frame_sign_bias;

	if (comp_ref_allowed) {
		if (last_ref_frame_sign_bias ==
		    golden_ref_frame_sign_bias) {
			comp_fixed_ref = ALTREF_FRAME;
			comp_var_ref[0] = LAST_FRAME;
			comp_var_ref[1] = GOLDEN_FRAME;
		} else if (last_ref_frame_sign_bias ==
			   alt_ref_frame_sign_bias) {
			comp_fixed_ref = GOLDEN_FRAME;
			comp_var_ref[0] = LAST_FRAME;
			comp_var_ref[1] = ALTREF_FRAME;
		} else {
			comp_fixed_ref = LAST_FRAME;
			comp_var_ref[0] = GOLDEN_FRAME;
			comp_var_ref[1] = ALTREF_FRAME;
		}
	}

	hantro_reg_write(ctx->dev, &vp9_comp_pred_fixed_ref, comp_fixed_ref);
	hantro_reg_write(ctx->dev, &vp9_comp_pred_var_ref0, comp_var_ref[0]);
	hantro_reg_write(ctx->dev, &vp9_comp_pred_var_ref1, comp_var_ref[1]);
}

#define INNER_LOOP \
do {									\
	for (m = 0; m < ARRAY_SIZE(adaptive->coef[0][0][0][0]); ++m) {	\
		memcpy(adaptive->coef[i][j][k][l][m],			\
		       probs->coef[i][j][k][l][m],			\
		       sizeof(probs->coef[i][j][k][l][m]));		\
									\
		adaptive->coef[i][j][k][l][m][3] = 0;			\
	}								\
} while (0)

static void config_probs(struct hantro_ctx *ctx, const struct v4l2_ctrl_vp9_frame *dec_params)
{
	struct hantro_vp9_dec_hw_ctx *vp9_ctx = &ctx->vp9_dec;
	struct hantro_aux_buf *misc = &vp9_ctx->misc;
	struct hantro_g2_all_probs *all_probs = misc->cpu;
	struct hantro_g2_probs *adaptive;
	struct hantro_g2_mv_probs *mv;
	const struct v4l2_vp9_segmentation *seg = &dec_params->seg;
	const struct v4l2_vp9_frame_context *probs = &vp9_ctx->probability_tables;
	int i, j, k, l, m;

	for (i = 0; i < ARRAY_SIZE(all_probs->kf_y_mode_prob); ++i)
		for (j = 0; j < ARRAY_SIZE(all_probs->kf_y_mode_prob[0]); ++j) {
			memcpy(all_probs->kf_y_mode_prob[i][j],
			       v4l2_vp9_kf_y_mode_prob[i][j],
			       ARRAY_SIZE(all_probs->kf_y_mode_prob[i][j]));

			all_probs->kf_y_mode_prob_tail[i][j][0] =
				v4l2_vp9_kf_y_mode_prob[i][j][8];
		}

	memcpy(all_probs->mb_segment_tree_probs, seg->tree_probs,
	       sizeof(all_probs->mb_segment_tree_probs));

	memcpy(all_probs->segment_pred_probs, seg->pred_probs,
	       sizeof(all_probs->segment_pred_probs));

	for (i = 0; i < ARRAY_SIZE(all_probs->kf_uv_mode_prob); ++i) {
		memcpy(all_probs->kf_uv_mode_prob[i], v4l2_vp9_kf_uv_mode_prob[i],
		       ARRAY_SIZE(all_probs->kf_uv_mode_prob[i]));

		all_probs->kf_uv_mode_prob_tail[i][0] = v4l2_vp9_kf_uv_mode_prob[i][8];
	}

	adaptive = &all_probs->probs;

	for (i = 0; i < ARRAY_SIZE(adaptive->inter_mode); ++i) {
		memcpy(adaptive->inter_mode[i], probs->inter_mode[i],
		       ARRAY_SIZE(probs->inter_mode[i]));

		adaptive->inter_mode[i][3] = 0;
	}

	memcpy(adaptive->is_inter, probs->is_inter, sizeof(adaptive->is_inter));

	for (i = 0; i < ARRAY_SIZE(adaptive->uv_mode); ++i) {
		memcpy(adaptive->uv_mode[i], probs->uv_mode[i],
		       sizeof(adaptive->uv_mode[i]));
		adaptive->uv_mode_tail[i][0] = probs->uv_mode[i][8];
	}

	memcpy(adaptive->tx8, probs->tx8, sizeof(adaptive->tx8));
	memcpy(adaptive->tx16, probs->tx16, sizeof(adaptive->tx16));
	memcpy(adaptive->tx32, probs->tx32, sizeof(adaptive->tx32));

	for (i = 0; i < ARRAY_SIZE(adaptive->y_mode); ++i) {
		memcpy(adaptive->y_mode[i], probs->y_mode[i],
		       ARRAY_SIZE(adaptive->y_mode[i]));

		adaptive->y_mode_tail[i][0] = probs->y_mode[i][8];
	}

	for (i = 0; i < ARRAY_SIZE(adaptive->partition[0]); ++i) {
		memcpy(adaptive->partition[0][i], v4l2_vp9_kf_partition_probs[i],
		       sizeof(v4l2_vp9_kf_partition_probs[i]));

		adaptive->partition[0][i][3] = 0;
	}

	for (i = 0; i < ARRAY_SIZE(adaptive->partition[1]); ++i) {
		memcpy(adaptive->partition[1][i], probs->partition[i],
		       sizeof(probs->partition[i]));

		adaptive->partition[1][i][3] = 0;
	}

	memcpy(adaptive->interp_filter, probs->interp_filter,
	       sizeof(adaptive->interp_filter));

	memcpy(adaptive->comp_mode, probs->comp_mode, sizeof(adaptive->comp_mode));

	memcpy(adaptive->skip, probs->skip, sizeof(adaptive->skip));

	mv = &adaptive->mv;

	memcpy(mv->joint, probs->mv.joint, sizeof(mv->joint));
	memcpy(mv->sign, probs->mv.sign, sizeof(mv->sign));
	memcpy(mv->class0_bit, probs->mv.class0_bit, sizeof(mv->class0_bit));
	memcpy(mv->fr, probs->mv.fr, sizeof(mv->fr));
	memcpy(mv->class0_hp, probs->mv.class0_hp, sizeof(mv->class0_hp));
	memcpy(mv->hp, probs->mv.hp, sizeof(mv->hp));
	memcpy(mv->classes, probs->mv.classes, sizeof(mv->classes));
	memcpy(mv->class0_fr, probs->mv.class0_fr, sizeof(mv->class0_fr));
	memcpy(mv->bits, probs->mv.bits, sizeof(mv->bits));

	memcpy(adaptive->single_ref, probs->single_ref, sizeof(adaptive->single_ref));

	memcpy(adaptive->comp_ref, probs->comp_ref, sizeof(adaptive->comp_ref));

	for (i = 0; i < ARRAY_SIZE(adaptive->coef); ++i)
		for (j = 0; j < ARRAY_SIZE(adaptive->coef[0]); ++j)
			for (k = 0; k < ARRAY_SIZE(adaptive->coef[0][0]); ++k)
				for (l = 0; l < ARRAY_SIZE(adaptive->coef[0][0][0]); ++l)
					INNER_LOOP;

	hantro_write_addr(ctx->dev, G2_VP9_PROBS_ADDR, misc->dma);
}

static void config_counts(struct hantro_ctx *ctx)
{
	struct hantro_vp9_dec_hw_ctx *vp9_dec = &ctx->vp9_dec;
	struct hantro_aux_buf *misc = &vp9_dec->misc;
	dma_addr_t addr = misc->dma + vp9_dec->ctx_counters_offset;

	hantro_write_addr(ctx->dev, G2_VP9_CTX_COUNT_ADDR, addr);
}

static void config_seg_map(struct hantro_ctx *ctx,
			   const struct v4l2_ctrl_vp9_frame *dec_params,
			   bool intra_only, bool update_map)
{
	struct hantro_vp9_dec_hw_ctx *vp9_ctx = &ctx->vp9_dec;
	struct hantro_aux_buf *segment_map = &vp9_ctx->segment_map;
	dma_addr_t addr;

	if (intra_only ||
	    (dec_params->flags & V4L2_VP9_FRAME_FLAG_ERROR_RESILIENT)) {
		memset(segment_map->cpu, 0, segment_map->size);
		memset(vp9_ctx->feature_data, 0, sizeof(vp9_ctx->feature_data));
		memset(vp9_ctx->feature_enabled, 0, sizeof(vp9_ctx->feature_enabled));
	}

	addr = segment_map->dma + vp9_ctx->active_segment * vp9_ctx->segment_map_size;
	hantro_write_addr(ctx->dev, G2_VP9_SEGMENT_READ_ADDR, addr);

	addr = segment_map->dma + (1 - vp9_ctx->active_segment) * vp9_ctx->segment_map_size;
	hantro_write_addr(ctx->dev, G2_VP9_SEGMENT_WRITE_ADDR, addr);

	if (update_map)
		vp9_ctx->active_segment = 1 - vp9_ctx->active_segment;
}

static void
config_source(struct hantro_ctx *ctx, const struct v4l2_ctrl_vp9_frame *dec_params,
	      struct vb2_v4l2_buffer *vb2_src)
{
	dma_addr_t stream_base, tmp_addr;
	unsigned int headres_size;
	u32 src_len, start_bit, src_buf_len;

	headres_size = dec_params->uncompressed_header_size
		     + dec_params->compressed_header_size;

	stream_base = vb2_dma_contig_plane_dma_addr(&vb2_src->vb2_buf, 0);

	tmp_addr = stream_base + headres_size;
	if (ctx->dev->variant->legacy_regs)
		hantro_write_addr(ctx->dev, G2_STREAM_ADDR, (tmp_addr & ~0xf));
	else
		hantro_write_addr(ctx->dev, G2_STREAM_ADDR, stream_base);

	start_bit = (tmp_addr & 0xf) * 8;
	hantro_reg_write(ctx->dev, &g2_start_bit, start_bit);

	src_len = vb2_get_plane_payload(&vb2_src->vb2_buf, 0);
	src_len += start_bit / 8 - headres_size;
	hantro_reg_write(ctx->dev, &g2_stream_len, src_len);

	if (!ctx->dev->variant->legacy_regs) {
		tmp_addr &= ~0xf;
		hantro_reg_write(ctx->dev, &g2_strm_start_offset, tmp_addr - stream_base);
		src_buf_len = vb2_plane_size(&vb2_src->vb2_buf, 0);
		hantro_reg_write(ctx->dev, &g2_strm_buffer_len, src_buf_len);
	}
}

static void
config_registers(struct hantro_ctx *ctx, const struct v4l2_ctrl_vp9_frame *dec_params,
		 struct vb2_v4l2_buffer *vb2_src, struct vb2_v4l2_buffer *vb2_dst)
{
	struct hantro_decoded_buffer *dst, *last, *mv_ref;
	struct hantro_vp9_dec_hw_ctx *vp9_ctx = &ctx->vp9_dec;
	const struct v4l2_vp9_segmentation *seg;
	bool intra_only, resolution_change;

	/* vp9 stuff */
	dst = vb2_to_hantro_decoded_buf(&vb2_dst->vb2_buf);

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

	if (!intra_only &&
	    !(dec_params->flags & V4L2_VP9_FRAME_FLAG_ERROR_RESILIENT) &&
	    vp9_ctx->last.valid)
		mv_ref = last;
	else
		mv_ref = dst;

	resolution_change = dst->vp9.width != last->vp9.width ||
			    dst->vp9.height != last->vp9.height;

	/* configure basic registers */
	hantro_reg_write(ctx->dev, &g2_mode, VP9_DEC_MODE);
	if (!ctx->dev->variant->legacy_regs) {
		hantro_reg_write(ctx->dev, &g2_strm_swap, 0xf);
		hantro_reg_write(ctx->dev, &g2_dirmv_swap, 0xf);
		hantro_reg_write(ctx->dev, &g2_compress_swap, 0xf);
		hantro_reg_write(ctx->dev, &g2_ref_compress_bypass, 1);
	} else {
		hantro_reg_write(ctx->dev, &g2_strm_swap_old, 0x1f);
		hantro_reg_write(ctx->dev, &g2_pic_swap, 0x10);
		hantro_reg_write(ctx->dev, &g2_dirmv_swap_old, 0x10);
		hantro_reg_write(ctx->dev, &g2_tab0_swap_old, 0x10);
		hantro_reg_write(ctx->dev, &g2_tab1_swap_old, 0x10);
		hantro_reg_write(ctx->dev, &g2_tab2_swap_old, 0x10);
		hantro_reg_write(ctx->dev, &g2_tab3_swap_old, 0x10);
		hantro_reg_write(ctx->dev, &g2_rscan_swap, 0x10);
	}
	hantro_reg_write(ctx->dev, &g2_buswidth, BUS_WIDTH_128);
	hantro_reg_write(ctx->dev, &g2_max_burst, 16);
	hantro_reg_write(ctx->dev, &g2_apf_threshold, 8);
	hantro_reg_write(ctx->dev, &g2_clk_gate_e, 1);
	hantro_reg_write(ctx->dev, &g2_max_cb_size, 6);
	hantro_reg_write(ctx->dev, &g2_min_cb_size, 3);
	if (ctx->dev->variant->double_buffer)
		hantro_reg_write(ctx->dev, &g2_double_buffer_e, 1);

	config_output(ctx, dst, dec_params);

	if (!intra_only)
		config_ref_registers(ctx, dec_params, dst, mv_ref);

	config_tiles(ctx, dec_params, dst);
	config_segment(ctx, dec_params);
	config_loop_filter(ctx, dec_params);
	config_picture_dimensions(ctx, dst);
	config_bit_depth(ctx, dec_params);
	config_quant(ctx, dec_params);
	config_others(ctx, dec_params, intra_only, resolution_change);
	config_compound_reference(ctx, dec_params);
	config_probs(ctx, dec_params);
	config_counts(ctx);
	config_seg_map(ctx, dec_params, intra_only,
		       seg->flags & V4L2_VP9_SEGMENTATION_FLAG_UPDATE_MAP);
	config_source(ctx, dec_params, vb2_src);
}

int hantro_g2_vp9_dec_run(struct hantro_ctx *ctx)
{
	const struct v4l2_ctrl_vp9_frame *decode_params;
	struct vb2_v4l2_buffer *src;
	struct vb2_v4l2_buffer *dst;
	int ret;

	hantro_g2_check_idle(ctx->dev);

	ret = start_prepare_run(ctx, &decode_params);
	if (ret) {
		hantro_end_prepare_run(ctx);
		return ret;
	}

	src = hantro_get_src_buf(ctx);
	dst = hantro_get_dst_buf(ctx);

	config_registers(ctx, decode_params, src, dst);

	hantro_end_prepare_run(ctx);

	vdpu_write(ctx->dev, G2_REG_INTERRUPT_DEC_E, G2_REG_INTERRUPT);

	return 0;
}

#define copy_tx_and_skip(p1, p2)				\
do {								\
	memcpy((p1)->tx8, (p2)->tx8, sizeof((p1)->tx8));	\
	memcpy((p1)->tx16, (p2)->tx16, sizeof((p1)->tx16));	\
	memcpy((p1)->tx32, (p2)->tx32, sizeof((p1)->tx32));	\
	memcpy((p1)->skip, (p2)->skip, sizeof((p1)->skip));	\
} while (0)

void hantro_g2_vp9_dec_done(struct hantro_ctx *ctx)
{
	struct hantro_vp9_dec_hw_ctx *vp9_ctx = &ctx->vp9_dec;
	unsigned int fctx_idx;

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
		struct symbol_counts *hantro_cnts;
		u32 tx16p[2][4];
		int i;

		/* buffer the forward-updated TX and skip probs */
		if (frame_is_intra)
			copy_tx_and_skip(tx_skip, probs);

		/* 6.1.2 refresh_probs(): load_probs() and load_probs2() */
		*probs = vp9_ctx->frame_context[fctx_idx];

		/* if FrameIsIntra then undo the effect of load_probs2() */
		if (frame_is_intra)
			copy_tx_and_skip(probs, tx_skip);

		counts = &vp9_ctx->cnts;
		hantro_cnts = vp9_ctx->misc.cpu + vp9_ctx->ctx_counters_offset;
		for (i = 0; i < ARRAY_SIZE(tx16p); ++i) {
			memcpy(tx16p[i],
			       hantro_cnts->tx16x16_count[i],
			       sizeof(hantro_cnts->tx16x16_count[0]));
			tx16p[i][3] = 0;
		}
		counts->tx16p = &tx16p;

		v4l2_vp9_adapt_coef_probs(probs, counts,
					  !vp9_ctx->last.valid ||
					  vp9_ctx->last.flags & V4L2_VP9_FRAME_FLAG_KEY_FRAME,
					  frame_is_intra);

		if (!frame_is_intra) {
			/* load_probs2() already done */
			u32 mv_mode[7][4];

			for (i = 0; i < ARRAY_SIZE(mv_mode); ++i) {
				mv_mode[i][0] = hantro_cnts->inter_mode_counts[i][1][0];
				mv_mode[i][1] = hantro_cnts->inter_mode_counts[i][2][0];
				mv_mode[i][2] = hantro_cnts->inter_mode_counts[i][0][0];
				mv_mode[i][3] = hantro_cnts->inter_mode_counts[i][2][1];
			}
			counts->mv_mode = &mv_mode;
			v4l2_vp9_adapt_noncoef_probs(&vp9_ctx->probability_tables, counts,
						     vp9_ctx->cur.reference_mode,
						     vp9_ctx->cur.interpolation_filter,
						     vp9_ctx->cur.tx_mode, vp9_ctx->cur.flags);
		}
	}

	vp9_ctx->frame_context[fctx_idx] = vp9_ctx->probability_tables;

out_update_last:
	vp9_ctx->last = vp9_ctx->cur;
}
