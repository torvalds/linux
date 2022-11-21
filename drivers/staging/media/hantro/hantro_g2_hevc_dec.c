// SPDX-License-Identifier: GPL-2.0
/*
 * Hantro VPU HEVC codec driver
 *
 * Copyright (C) 2020 Safran Passenger Innovations LLC
 */

#include "hantro_hw.h"
#include "hantro_g2_regs.h"

static void prepare_tile_info_buffer(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	const struct hantro_hevc_dec_ctrls *ctrls = &ctx->hevc_dec.ctrls;
	const struct v4l2_ctrl_hevc_pps *pps = ctrls->pps;
	const struct v4l2_ctrl_hevc_sps *sps = ctrls->sps;
	u16 *p = (u16 *)((u8 *)ctx->hevc_dec.tile_sizes.cpu);
	unsigned int num_tile_rows = pps->num_tile_rows_minus1 + 1;
	unsigned int num_tile_cols = pps->num_tile_columns_minus1 + 1;
	unsigned int pic_width_in_ctbs, pic_height_in_ctbs;
	unsigned int max_log2_ctb_size, ctb_size;
	bool tiles_enabled, uniform_spacing;
	u32 no_chroma = 0;

	tiles_enabled = !!(pps->flags & V4L2_HEVC_PPS_FLAG_TILES_ENABLED);
	uniform_spacing = !!(pps->flags & V4L2_HEVC_PPS_FLAG_UNIFORM_SPACING);

	hantro_reg_write(vpu, &g2_tile_e, tiles_enabled);

	max_log2_ctb_size = sps->log2_min_luma_coding_block_size_minus3 + 3 +
			    sps->log2_diff_max_min_luma_coding_block_size;
	pic_width_in_ctbs = (sps->pic_width_in_luma_samples +
			    (1 << max_log2_ctb_size) - 1) >> max_log2_ctb_size;
	pic_height_in_ctbs = (sps->pic_height_in_luma_samples + (1 << max_log2_ctb_size) - 1)
			     >> max_log2_ctb_size;
	ctb_size = 1 << max_log2_ctb_size;

	vpu_debug(1, "Preparing tile sizes buffer for %dx%d CTBs (CTB size %d)\n",
		  pic_width_in_ctbs, pic_height_in_ctbs, ctb_size);

	if (tiles_enabled) {
		unsigned int i, j, h;

		vpu_debug(1, "Tiles enabled! %dx%d\n", num_tile_cols, num_tile_rows);

		hantro_reg_write(vpu, &g2_num_tile_rows, num_tile_rows);
		hantro_reg_write(vpu, &g2_num_tile_cols, num_tile_cols);

		/* write width + height for each tile in pic */
		if (!uniform_spacing) {
			u32 tmp_w = 0, tmp_h = 0;

			for (i = 0; i < num_tile_rows; i++) {
				if (i == num_tile_rows - 1)
					h = pic_height_in_ctbs - tmp_h;
				else
					h = pps->row_height_minus1[i] + 1;
				tmp_h += h;
				if (i == 0 && h == 1 && ctb_size == 16)
					no_chroma = 1;
				for (j = 0, tmp_w = 0; j < num_tile_cols - 1; j++) {
					tmp_w += pps->column_width_minus1[j] + 1;
					*p++ = pps->column_width_minus1[j + 1];
					*p++ = h;
					if (i == 0 && h == 1 && ctb_size == 16)
						no_chroma = 1;
				}
				/* last column */
				*p++ = pic_width_in_ctbs - tmp_w;
				*p++ = h;
			}
		} else { /* uniform spacing */
			u32 tmp, prev_h, prev_w;

			for (i = 0, prev_h = 0; i < num_tile_rows; i++) {
				tmp = (i + 1) * pic_height_in_ctbs / num_tile_rows;
				h = tmp - prev_h;
				prev_h = tmp;
				if (i == 0 && h == 1 && ctb_size == 16)
					no_chroma = 1;
				for (j = 0, prev_w = 0; j < num_tile_cols; j++) {
					tmp = (j + 1) * pic_width_in_ctbs / num_tile_cols;
					*p++ = tmp - prev_w;
					*p++ = h;
					if (j == 0 &&
					    (pps->column_width_minus1[0] + 1) == 1 &&
					    ctb_size == 16)
						no_chroma = 1;
					prev_w = tmp;
				}
			}
		}
	} else {
		hantro_reg_write(vpu, &g2_num_tile_rows, 1);
		hantro_reg_write(vpu, &g2_num_tile_cols, 1);

		/* There's one tile, with dimensions equal to pic size. */
		p[0] = pic_width_in_ctbs;
		p[1] = pic_height_in_ctbs;
	}

	if (no_chroma)
		vpu_debug(1, "%s: no chroma!\n", __func__);
}

static void set_params(struct hantro_ctx *ctx)
{
	const struct hantro_hevc_dec_ctrls *ctrls = &ctx->hevc_dec.ctrls;
	const struct v4l2_ctrl_hevc_sps *sps = ctrls->sps;
	const struct v4l2_ctrl_hevc_pps *pps = ctrls->pps;
	const struct v4l2_ctrl_hevc_decode_params *decode_params = ctrls->decode_params;
	struct hantro_dev *vpu = ctx->dev;
	u32 min_log2_cb_size, max_log2_ctb_size, min_cb_size, max_ctb_size;
	u32 pic_width_in_min_cbs, pic_height_in_min_cbs;
	u32 pic_width_aligned, pic_height_aligned;
	u32 partial_ctb_x, partial_ctb_y;

	hantro_reg_write(vpu, &g2_bit_depth_y_minus8, sps->bit_depth_luma_minus8);
	hantro_reg_write(vpu, &g2_bit_depth_c_minus8, sps->bit_depth_chroma_minus8);

	hantro_reg_write(vpu, &g2_output_8_bits, 0);

	hantro_reg_write(vpu, &g2_hdr_skip_length, ctrls->hevc_hdr_skip_length);

	min_log2_cb_size = sps->log2_min_luma_coding_block_size_minus3 + 3;
	max_log2_ctb_size = min_log2_cb_size + sps->log2_diff_max_min_luma_coding_block_size;

	hantro_reg_write(vpu, &g2_min_cb_size, min_log2_cb_size);
	hantro_reg_write(vpu, &g2_max_cb_size, max_log2_ctb_size);

	min_cb_size = 1 << min_log2_cb_size;
	max_ctb_size = 1 << max_log2_ctb_size;

	pic_width_in_min_cbs = sps->pic_width_in_luma_samples / min_cb_size;
	pic_height_in_min_cbs = sps->pic_height_in_luma_samples / min_cb_size;
	pic_width_aligned = ALIGN(sps->pic_width_in_luma_samples, max_ctb_size);
	pic_height_aligned = ALIGN(sps->pic_height_in_luma_samples, max_ctb_size);

	partial_ctb_x = !!(sps->pic_width_in_luma_samples != pic_width_aligned);
	partial_ctb_y = !!(sps->pic_height_in_luma_samples != pic_height_aligned);

	hantro_reg_write(vpu, &g2_partial_ctb_x, partial_ctb_x);
	hantro_reg_write(vpu, &g2_partial_ctb_y, partial_ctb_y);

	hantro_reg_write(vpu, &g2_pic_width_in_cbs, pic_width_in_min_cbs);
	hantro_reg_write(vpu, &g2_pic_height_in_cbs, pic_height_in_min_cbs);

	hantro_reg_write(vpu, &g2_pic_width_4x4,
			 (pic_width_in_min_cbs * min_cb_size) / 4);
	hantro_reg_write(vpu, &g2_pic_height_4x4,
			 (pic_height_in_min_cbs * min_cb_size) / 4);

	hantro_reg_write(vpu, &hevc_max_inter_hierdepth,
			 sps->max_transform_hierarchy_depth_inter);
	hantro_reg_write(vpu, &hevc_max_intra_hierdepth,
			 sps->max_transform_hierarchy_depth_intra);
	hantro_reg_write(vpu, &hevc_min_trb_size,
			 sps->log2_min_luma_transform_block_size_minus2 + 2);
	hantro_reg_write(vpu, &hevc_max_trb_size,
			 sps->log2_min_luma_transform_block_size_minus2 + 2 +
			 sps->log2_diff_max_min_luma_transform_block_size);

	hantro_reg_write(vpu, &g2_tempor_mvp_e,
			 !!(sps->flags & V4L2_HEVC_SPS_FLAG_SPS_TEMPORAL_MVP_ENABLED) &&
			 !(decode_params->flags & V4L2_HEVC_DECODE_PARAM_FLAG_IDR_PIC));
	hantro_reg_write(vpu, &g2_strong_smooth_e,
			 !!(sps->flags & V4L2_HEVC_SPS_FLAG_STRONG_INTRA_SMOOTHING_ENABLED));
	hantro_reg_write(vpu, &g2_asym_pred_e,
			 !!(sps->flags & V4L2_HEVC_SPS_FLAG_AMP_ENABLED));
	hantro_reg_write(vpu, &g2_sao_e,
			 !!(sps->flags & V4L2_HEVC_SPS_FLAG_SAMPLE_ADAPTIVE_OFFSET));
	hantro_reg_write(vpu, &g2_sign_data_hide,
			 !!(pps->flags & V4L2_HEVC_PPS_FLAG_SIGN_DATA_HIDING_ENABLED));

	if (pps->flags & V4L2_HEVC_PPS_FLAG_CU_QP_DELTA_ENABLED) {
		hantro_reg_write(vpu, &g2_cu_qpd_e, 1);
		hantro_reg_write(vpu, &g2_max_cu_qpd_depth, pps->diff_cu_qp_delta_depth);
	} else {
		hantro_reg_write(vpu, &g2_cu_qpd_e, 0);
		hantro_reg_write(vpu, &g2_max_cu_qpd_depth, 0);
	}

	if (pps->flags & V4L2_HEVC_PPS_FLAG_PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT) {
		hantro_reg_write(vpu, &g2_cb_qp_offset, pps->pps_cb_qp_offset);
		hantro_reg_write(vpu, &g2_cr_qp_offset, pps->pps_cr_qp_offset);
	} else {
		hantro_reg_write(vpu, &g2_cb_qp_offset, 0);
		hantro_reg_write(vpu, &g2_cr_qp_offset, 0);
	}

	hantro_reg_write(vpu, &g2_filt_offset_beta, pps->pps_beta_offset_div2);
	hantro_reg_write(vpu, &g2_filt_offset_tc, pps->pps_tc_offset_div2);
	hantro_reg_write(vpu, &g2_slice_hdr_ext_e,
			 !!(pps->flags & V4L2_HEVC_PPS_FLAG_SLICE_SEGMENT_HEADER_EXTENSION_PRESENT));
	hantro_reg_write(vpu, &g2_slice_hdr_ext_bits, pps->num_extra_slice_header_bits);
	hantro_reg_write(vpu, &g2_slice_chqp_present,
			 !!(pps->flags & V4L2_HEVC_PPS_FLAG_PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT));
	hantro_reg_write(vpu, &g2_weight_bipr_idc,
			 !!(pps->flags & V4L2_HEVC_PPS_FLAG_WEIGHTED_BIPRED));
	hantro_reg_write(vpu, &g2_transq_bypass,
			 !!(pps->flags & V4L2_HEVC_PPS_FLAG_TRANSQUANT_BYPASS_ENABLED));
	hantro_reg_write(vpu, &g2_list_mod_e,
			 !!(pps->flags & V4L2_HEVC_PPS_FLAG_LISTS_MODIFICATION_PRESENT));
	hantro_reg_write(vpu, &g2_entropy_sync_e,
			 !!(pps->flags & V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED));
	hantro_reg_write(vpu, &g2_cabac_init_present,
			 !!(pps->flags & V4L2_HEVC_PPS_FLAG_CABAC_INIT_PRESENT));
	hantro_reg_write(vpu, &g2_idr_pic_e,
			 !!(decode_params->flags & V4L2_HEVC_DECODE_PARAM_FLAG_IRAP_PIC));
	hantro_reg_write(vpu, &hevc_parallel_merge,
			 pps->log2_parallel_merge_level_minus2 + 2);
	hantro_reg_write(vpu, &g2_pcm_filt_d,
			 !!(sps->flags & V4L2_HEVC_SPS_FLAG_PCM_LOOP_FILTER_DISABLED));
	hantro_reg_write(vpu, &g2_pcm_e,
			 !!(sps->flags & V4L2_HEVC_SPS_FLAG_PCM_ENABLED));
	if (sps->flags & V4L2_HEVC_SPS_FLAG_PCM_ENABLED) {
		hantro_reg_write(vpu, &g2_max_pcm_size,
				 sps->log2_diff_max_min_pcm_luma_coding_block_size +
				 sps->log2_min_pcm_luma_coding_block_size_minus3 + 3);
		hantro_reg_write(vpu, &g2_min_pcm_size,
				 sps->log2_min_pcm_luma_coding_block_size_minus3 + 3);
		hantro_reg_write(vpu, &g2_bit_depth_pcm_y,
				 sps->pcm_sample_bit_depth_luma_minus1 + 1);
		hantro_reg_write(vpu, &g2_bit_depth_pcm_c,
				 sps->pcm_sample_bit_depth_chroma_minus1 + 1);
	} else {
		hantro_reg_write(vpu, &g2_max_pcm_size, 0);
		hantro_reg_write(vpu, &g2_min_pcm_size, 0);
		hantro_reg_write(vpu, &g2_bit_depth_pcm_y, 0);
		hantro_reg_write(vpu, &g2_bit_depth_pcm_c, 0);
	}

	hantro_reg_write(vpu, &g2_start_code_e, 1);
	hantro_reg_write(vpu, &g2_init_qp, pps->init_qp_minus26 + 26);
	hantro_reg_write(vpu, &g2_weight_pred_e,
			 !!(pps->flags & V4L2_HEVC_PPS_FLAG_WEIGHTED_PRED));
	hantro_reg_write(vpu, &g2_cabac_init_present,
			 !!(pps->flags & V4L2_HEVC_PPS_FLAG_CABAC_INIT_PRESENT));
	hantro_reg_write(vpu, &g2_const_intra_e,
			 !!(pps->flags & V4L2_HEVC_PPS_FLAG_CONSTRAINED_INTRA_PRED));
	hantro_reg_write(vpu, &g2_transform_skip,
			 !!(pps->flags & V4L2_HEVC_PPS_FLAG_TRANSFORM_SKIP_ENABLED));
	hantro_reg_write(vpu, &g2_out_filtering_dis,
			 !!(pps->flags & V4L2_HEVC_PPS_FLAG_PPS_DISABLE_DEBLOCKING_FILTER));
	hantro_reg_write(vpu, &g2_filt_ctrl_pres,
			 !!(pps->flags & V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT));
	hantro_reg_write(vpu, &g2_dependent_slice,
			 !!(pps->flags & V4L2_HEVC_PPS_FLAG_DEPENDENT_SLICE_SEGMENT_ENABLED));
	hantro_reg_write(vpu, &g2_filter_override,
			 !!(pps->flags & V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_OVERRIDE_ENABLED));
	hantro_reg_write(vpu, &g2_refidx0_active,
			 pps->num_ref_idx_l0_default_active_minus1 + 1);
	hantro_reg_write(vpu, &g2_refidx1_active,
			 pps->num_ref_idx_l1_default_active_minus1 + 1);
	hantro_reg_write(vpu, &g2_apf_threshold, 8);
}

static void set_ref_pic_list(struct hantro_ctx *ctx)
{
	const struct hantro_hevc_dec_ctrls *ctrls = &ctx->hevc_dec.ctrls;
	struct hantro_dev *vpu = ctx->dev;
	const struct v4l2_ctrl_hevc_decode_params *decode_params = ctrls->decode_params;
	u32 list0[V4L2_HEVC_DPB_ENTRIES_NUM_MAX] = {};
	u32 list1[V4L2_HEVC_DPB_ENTRIES_NUM_MAX] = {};
	static const struct hantro_reg ref_pic_regs0[] = {
		hevc_rlist_f0,
		hevc_rlist_f1,
		hevc_rlist_f2,
		hevc_rlist_f3,
		hevc_rlist_f4,
		hevc_rlist_f5,
		hevc_rlist_f6,
		hevc_rlist_f7,
		hevc_rlist_f8,
		hevc_rlist_f9,
		hevc_rlist_f10,
		hevc_rlist_f11,
		hevc_rlist_f12,
		hevc_rlist_f13,
		hevc_rlist_f14,
		hevc_rlist_f15,
	};
	static const struct hantro_reg ref_pic_regs1[] = {
		hevc_rlist_b0,
		hevc_rlist_b1,
		hevc_rlist_b2,
		hevc_rlist_b3,
		hevc_rlist_b4,
		hevc_rlist_b5,
		hevc_rlist_b6,
		hevc_rlist_b7,
		hevc_rlist_b8,
		hevc_rlist_b9,
		hevc_rlist_b10,
		hevc_rlist_b11,
		hevc_rlist_b12,
		hevc_rlist_b13,
		hevc_rlist_b14,
		hevc_rlist_b15,
	};
	unsigned int i, j;

	/* List 0 contains: short term before, short term after and long term */
	j = 0;
	for (i = 0; i < decode_params->num_poc_st_curr_before && j < ARRAY_SIZE(list0); i++)
		list0[j++] = decode_params->poc_st_curr_before[i];
	for (i = 0; i < decode_params->num_poc_st_curr_after && j < ARRAY_SIZE(list0); i++)
		list0[j++] = decode_params->poc_st_curr_after[i];
	for (i = 0; i < decode_params->num_poc_lt_curr && j < ARRAY_SIZE(list0); i++)
		list0[j++] = decode_params->poc_lt_curr[i];

	/* Fill the list, copying over and over */
	i = 0;
	while (j < ARRAY_SIZE(list0))
		list0[j++] = list0[i++];

	j = 0;
	for (i = 0; i < decode_params->num_poc_st_curr_after && j < ARRAY_SIZE(list1); i++)
		list1[j++] = decode_params->poc_st_curr_after[i];
	for (i = 0; i < decode_params->num_poc_st_curr_before && j < ARRAY_SIZE(list1); i++)
		list1[j++] = decode_params->poc_st_curr_before[i];
	for (i = 0; i < decode_params->num_poc_lt_curr && j < ARRAY_SIZE(list1); i++)
		list1[j++] = decode_params->poc_lt_curr[i];

	i = 0;
	while (j < ARRAY_SIZE(list1))
		list1[j++] = list1[i++];

	for (i = 0; i < V4L2_HEVC_DPB_ENTRIES_NUM_MAX; i++) {
		hantro_reg_write(vpu, &ref_pic_regs0[i], list0[i]);
		hantro_reg_write(vpu, &ref_pic_regs1[i], list1[i]);
	}
}

static int set_ref(struct hantro_ctx *ctx)
{
	const struct hantro_hevc_dec_ctrls *ctrls = &ctx->hevc_dec.ctrls;
	const struct v4l2_ctrl_hevc_sps *sps = ctrls->sps;
	const struct v4l2_ctrl_hevc_pps *pps = ctrls->pps;
	const struct v4l2_ctrl_hevc_decode_params *decode_params = ctrls->decode_params;
	const struct v4l2_hevc_dpb_entry *dpb = decode_params->dpb;
	dma_addr_t luma_addr, chroma_addr, mv_addr = 0;
	struct hantro_dev *vpu = ctx->dev;
	struct vb2_v4l2_buffer *vb2_dst;
	struct hantro_decoded_buffer *dst;
	size_t cr_offset = hantro_hevc_chroma_offset(sps);
	size_t mv_offset = hantro_hevc_motion_vectors_offset(sps);
	u32 max_ref_frames;
	u16 dpb_longterm_e;
	static const struct hantro_reg cur_poc[] = {
		hevc_cur_poc_00,
		hevc_cur_poc_01,
		hevc_cur_poc_02,
		hevc_cur_poc_03,
		hevc_cur_poc_04,
		hevc_cur_poc_05,
		hevc_cur_poc_06,
		hevc_cur_poc_07,
		hevc_cur_poc_08,
		hevc_cur_poc_09,
		hevc_cur_poc_10,
		hevc_cur_poc_11,
		hevc_cur_poc_12,
		hevc_cur_poc_13,
		hevc_cur_poc_14,
		hevc_cur_poc_15,
	};
	unsigned int i;

	max_ref_frames = decode_params->num_poc_lt_curr +
		decode_params->num_poc_st_curr_before +
		decode_params->num_poc_st_curr_after;
	/*
	 * Set max_ref_frames to non-zero to avoid HW hang when decoding
	 * badly marked I-frames.
	 */
	max_ref_frames = max_ref_frames ? max_ref_frames : 1;
	hantro_reg_write(vpu, &g2_num_ref_frames, max_ref_frames);
	hantro_reg_write(vpu, &g2_filter_over_slices,
			 !!(pps->flags & V4L2_HEVC_PPS_FLAG_PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED));
	hantro_reg_write(vpu, &g2_filter_over_tiles,
			 !!(pps->flags & V4L2_HEVC_PPS_FLAG_LOOP_FILTER_ACROSS_TILES_ENABLED));

	/*
	 * Write POC count diff from current pic. For frame decoding only compute
	 * pic_order_cnt[0] and ignore pic_order_cnt[1] used in field-coding.
	 */
	for (i = 0; i < decode_params->num_active_dpb_entries && i < ARRAY_SIZE(cur_poc); i++) {
		char poc_diff = decode_params->pic_order_cnt_val - dpb[i].pic_order_cnt[0];

		hantro_reg_write(vpu, &cur_poc[i], poc_diff);
	}

	if (i < ARRAY_SIZE(cur_poc)) {
		/*
		 * After the references, fill one entry pointing to itself,
		 * i.e. difference is zero.
		 */
		hantro_reg_write(vpu, &cur_poc[i], 0);
		i++;
	}

	/* Fill the rest with the current picture */
	for (; i < ARRAY_SIZE(cur_poc); i++)
		hantro_reg_write(vpu, &cur_poc[i], decode_params->pic_order_cnt_val);

	set_ref_pic_list(ctx);

	/* We will only keep the references picture that are still used */
	ctx->hevc_dec.ref_bufs_used = 0;

	/* Set up addresses of DPB buffers */
	dpb_longterm_e = 0;
	for (i = 0; i < decode_params->num_active_dpb_entries &&
	     i < (V4L2_HEVC_DPB_ENTRIES_NUM_MAX - 1); i++) {
		luma_addr = hantro_hevc_get_ref_buf(ctx, dpb[i].pic_order_cnt[0]);
		if (!luma_addr)
			return -ENOMEM;

		chroma_addr = luma_addr + cr_offset;
		mv_addr = luma_addr + mv_offset;

		if (dpb[i].flags & V4L2_HEVC_DPB_ENTRY_LONG_TERM_REFERENCE)
			dpb_longterm_e |= BIT(V4L2_HEVC_DPB_ENTRIES_NUM_MAX - 1 - i);

		hantro_write_addr(vpu, G2_REF_LUMA_ADDR(i), luma_addr);
		hantro_write_addr(vpu, G2_REF_CHROMA_ADDR(i), chroma_addr);
		hantro_write_addr(vpu, G2_REF_MV_ADDR(i), mv_addr);
	}

	vb2_dst = hantro_get_dst_buf(ctx);
	dst = vb2_to_hantro_decoded_buf(&vb2_dst->vb2_buf);
	luma_addr = hantro_get_dec_buf_addr(ctx, &dst->base.vb.vb2_buf);
	if (!luma_addr)
		return -ENOMEM;

	if (hantro_hevc_add_ref_buf(ctx, decode_params->pic_order_cnt_val, luma_addr))
		return -EINVAL;

	chroma_addr = luma_addr + cr_offset;
	mv_addr = luma_addr + mv_offset;

	hantro_write_addr(vpu, G2_REF_LUMA_ADDR(i), luma_addr);
	hantro_write_addr(vpu, G2_REF_CHROMA_ADDR(i), chroma_addr);
	hantro_write_addr(vpu, G2_REF_MV_ADDR(i++), mv_addr);

	hantro_write_addr(vpu, G2_OUT_LUMA_ADDR, luma_addr);
	hantro_write_addr(vpu, G2_OUT_CHROMA_ADDR, chroma_addr);
	hantro_write_addr(vpu, G2_OUT_MV_ADDR, mv_addr);

	hantro_hevc_ref_remove_unused(ctx);

	for (; i < V4L2_HEVC_DPB_ENTRIES_NUM_MAX; i++) {
		hantro_write_addr(vpu, G2_REF_LUMA_ADDR(i), 0);
		hantro_write_addr(vpu, G2_REF_CHROMA_ADDR(i), 0);
		hantro_write_addr(vpu, G2_REF_MV_ADDR(i), 0);
	}

	hantro_reg_write(vpu, &g2_refer_lterm_e, dpb_longterm_e);

	return 0;
}

static void set_buffers(struct hantro_ctx *ctx)
{
	struct vb2_v4l2_buffer *src_buf;
	struct hantro_dev *vpu = ctx->dev;
	dma_addr_t src_dma;
	u32 src_len, src_buf_len;

	src_buf = hantro_get_src_buf(ctx);

	/* Source (stream) buffer. */
	src_dma = vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);
	src_len = vb2_get_plane_payload(&src_buf->vb2_buf, 0);
	src_buf_len = vb2_plane_size(&src_buf->vb2_buf, 0);

	hantro_write_addr(vpu, G2_STREAM_ADDR, src_dma);
	hantro_reg_write(vpu, &g2_stream_len, src_len);
	hantro_reg_write(vpu, &g2_strm_buffer_len, src_buf_len);
	hantro_reg_write(vpu, &g2_strm_start_offset, 0);
	hantro_reg_write(vpu, &g2_write_mvs_e, 1);

	hantro_write_addr(vpu, G2_TILE_SIZES_ADDR, ctx->hevc_dec.tile_sizes.dma);
	hantro_write_addr(vpu, G2_TILE_FILTER_ADDR, ctx->hevc_dec.tile_filter.dma);
	hantro_write_addr(vpu, G2_TILE_SAO_ADDR, ctx->hevc_dec.tile_sao.dma);
	hantro_write_addr(vpu, G2_TILE_BSD_ADDR, ctx->hevc_dec.tile_bsd.dma);
}

static void prepare_scaling_list_buffer(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	const struct hantro_hevc_dec_ctrls *ctrls = &ctx->hevc_dec.ctrls;
	const struct v4l2_ctrl_hevc_scaling_matrix *sc = ctrls->scaling;
	const struct v4l2_ctrl_hevc_sps *sps = ctrls->sps;
	u8 *p = ((u8 *)ctx->hevc_dec.scaling_lists.cpu);
	unsigned int scaling_list_enabled;
	unsigned int i, j, k;

	scaling_list_enabled = !!(sps->flags & V4L2_HEVC_SPS_FLAG_SCALING_LIST_ENABLED);
	hantro_reg_write(vpu, &g2_scaling_list_e, scaling_list_enabled);

	if (!scaling_list_enabled)
		return;

	for (i = 0; i < ARRAY_SIZE(sc->scaling_list_dc_coef_16x16); i++)
		*p++ = sc->scaling_list_dc_coef_16x16[i];

	for (i = 0; i < ARRAY_SIZE(sc->scaling_list_dc_coef_32x32); i++)
		*p++ = sc->scaling_list_dc_coef_32x32[i];

	/* 128-bit boundary */
	p += 8;

	/* write scaling lists column by column */

	for (i = 0; i < 6; i++)
		for (j = 0; j < 4; j++)
			for (k = 0; k < 4; k++)
				*p++ = sc->scaling_list_4x4[i][4 * k + j];

	for (i = 0; i < 6; i++)
		for (j = 0; j < 8; j++)
			for (k = 0; k < 8; k++)
				*p++ = sc->scaling_list_8x8[i][8 * k + j];

	for (i = 0; i < 6; i++)
		for (j = 0; j < 8; j++)
			for (k = 0; k < 8; k++)
				*p++ = sc->scaling_list_16x16[i][8 * k + j];

	for (i = 0; i < 2; i++)
		for (j = 0; j < 8; j++)
			for (k = 0; k < 8; k++)
				*p++ = sc->scaling_list_32x32[i][8 * k + j];

	hantro_write_addr(vpu, G2_HEVC_SCALING_LIST_ADDR, ctx->hevc_dec.scaling_lists.dma);
}

int hantro_g2_hevc_dec_run(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	int ret;

	hantro_g2_check_idle(vpu);

	/* Prepare HEVC decoder context. */
	ret = hantro_hevc_dec_prepare_run(ctx);
	if (ret)
		return ret;

	/* Configure hardware registers. */
	set_params(ctx);

	/* set reference pictures */
	ret = set_ref(ctx);
	if (ret)
		return ret;

	set_buffers(ctx);
	prepare_tile_info_buffer(ctx);

	prepare_scaling_list_buffer(ctx);

	hantro_end_prepare_run(ctx);

	hantro_reg_write(vpu, &g2_mode, HEVC_DEC_MODE);
	hantro_reg_write(vpu, &g2_clk_gate_e, 1);

	/* Don't disable output */
	hantro_reg_write(vpu, &g2_out_dis, 0);

	/* Don't compress buffers */
	hantro_reg_write(vpu, &g2_ref_compress_bypass, 1);

	/* Bus width and max burst */
	hantro_reg_write(vpu, &g2_buswidth, BUS_WIDTH_128);
	hantro_reg_write(vpu, &g2_max_burst, 16);

	/* Swap */
	hantro_reg_write(vpu, &g2_strm_swap, 0xf);
	hantro_reg_write(vpu, &g2_dirmv_swap, 0xf);
	hantro_reg_write(vpu, &g2_compress_swap, 0xf);

	/* Start decoding! */
	vdpu_write(vpu, G2_REG_INTERRUPT_DEC_E, G2_REG_INTERRUPT);

	return 0;
}
