// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip VDPU383 HEVC backend
 *
 * Copyright (C) 2025 Collabora, Ltd.
 *  Detlev Casanova <detlev.casanova@collabora.com>
 */

#include <media/v4l2-mem2mem.h>

#include "rkvdec.h"
#include "rkvdec-cabac.h"
#include "rkvdec-rcb.h"
#include "rkvdec-hevc-common.h"
#include "rkvdec-vdpu383-regs.h"

struct rkvdec_hevc_sps_pps {
	// SPS
	u16 video_parameters_set_id			: 4;
	u16 seq_parameters_set_id_sps			: 4;
	u16 chroma_format_idc				: 2;
	u16 width					: 16;
	u16 height					: 16;
	u16 bit_depth_luma				: 3;
	u16 bit_depth_chroma				: 3;
	u16 max_pic_order_count_lsb			: 5;
	u16 diff_max_min_luma_coding_block_size		: 2;
	u16 min_luma_coding_block_size			: 3;
	u16 min_transform_block_size			: 3;
	u16 diff_max_min_transform_block_size		: 2;
	u16 max_transform_hierarchy_depth_inter		: 3;
	u16 max_transform_hierarchy_depth_intra		: 3;
	u16 scaling_list_enabled_flag			: 1;
	u16 amp_enabled_flag				: 1;
	u16 sample_adaptive_offset_enabled_flag		: 1;
	u16 pcm_enabled_flag				: 1;
	u16 pcm_sample_bit_depth_luma			: 4;
	u16 pcm_sample_bit_depth_chroma			: 4;
	u16 pcm_loop_filter_disabled_flag		: 1;
	u16 diff_max_min_pcm_luma_coding_block_size	: 3;
	u16 min_pcm_luma_coding_block_size		: 3;
	u16 num_short_term_ref_pic_sets			: 7;
	u16 long_term_ref_pics_present_flag		: 1;
	u16 num_long_term_ref_pics_sps			: 6;
	u16 sps_temporal_mvp_enabled_flag		: 1;
	u16 strong_intra_smoothing_enabled_flag		: 1;
	u16 reserved0					: 7;
	u16 sps_max_dec_pic_buffering_minus1		: 4;
	u16 separate_colour_plane_flag			: 1;
	u16 high_precision_offsets_enabled_flag		: 1;
	u16 persistent_rice_adaptation_enabled_flag	: 1;

	// PPS
	u16 picture_parameters_set_id			: 6;
	u16 seq_parameters_set_id_pps			: 4;
	u16 dependent_slice_segments_enabled_flag	: 1;
	u16 output_flag_present_flag			: 1;
	u16 num_extra_slice_header_bits			: 13;
	u16 sign_data_hiding_enabled_flag		: 1;
	u16 cabac_init_present_flag			: 1;
	u16 num_ref_idx_l0_default_active		: 4;
	u16 num_ref_idx_l1_default_active		: 4;
	u16 init_qp_minus26				: 7;
	u16 constrained_intra_pred_flag			: 1;
	u16 transform_skip_enabled_flag			: 1;
	u16 cu_qp_delta_enabled_flag			: 1;
	u16 log2_min_cb_size				: 3;
	u16 pps_cb_qp_offset				: 5;
	u16 pps_cr_qp_offset				: 5;
	u16 pps_slice_chroma_qp_offsets_present_flag	: 1;
	u16 weighted_pred_flag				: 1;
	u16 weighted_bipred_flag			: 1;
	u16 transquant_bypass_enabled_flag		: 1;
	u16 tiles_enabled_flag				: 1;
	u16 entropy_coding_sync_enabled_flag		: 1;
	u16 pps_loop_filter_across_slices_enabled_flag	: 1;
	u16 loop_filter_across_tiles_enabled_flag	: 1;
	u16 deblocking_filter_override_enabled_flag	: 1;
	u16 pps_deblocking_filter_disabled_flag		: 1;
	u16 pps_beta_offset_div2			: 4;
	u16 pps_tc_offset_div2				: 4;
	u16 lists_modification_present_flag		: 1;
	u16 log2_parallel_merge_level			: 3;
	u16 slice_segment_header_extension_present_flag	: 1;
	u16 reserved1					: 3;

	// pps extensions
	u16 log2_max_transform_skip_block_size		: 2;
	u16 cross_component_prediction_enabled_flag	: 1;
	u16 chroma_qp_offset_list_enabled_flag		: 1;
	u16 log2_min_cu_chroma_qp_delta_size		: 3;
	u16 cb_qp_offset_list0				: 5;
	u16 cb_qp_offset_list1				: 5;
	u16 cb_qp_offset_list2				: 5;
	u16 cb_qp_offset_list3				: 5;
	u16 cb_qp_offset_list4				: 5;
	u16 cb_qp_offset_list5				: 5;
	u16 cb_cr_offset_list0				: 5;
	u16 cb_cr_offset_list1				: 5;
	u16 cb_cr_offset_list2				: 5;
	u16 cb_cr_offset_list3				: 5;
	u16 cb_cr_offset_list4				: 5;
	u16 cb_cr_offset_list5				: 5;
	u16 chroma_qp_offset_list_len_minus1		: 3;

	/* mvc0 && mvc1 */
	u16 mvc_ff					: 16;
	u16 mvc_00					: 9;

	/* poc info */
	u16 reserved2					: 3;
	u32 current_poc					: 32;
	u32 ref_pic_poc0				: 32;
	u32 ref_pic_poc1				: 32;
	u32 ref_pic_poc2				: 32;
	u32 ref_pic_poc3				: 32;
	u32 ref_pic_poc4				: 32;
	u32 ref_pic_poc5				: 32;
	u32 ref_pic_poc6				: 32;
	u32 ref_pic_poc7				: 32;
	u32 ref_pic_poc8				: 32;
	u32 ref_pic_poc9				: 32;
	u32 ref_pic_poc10				: 32;
	u32 ref_pic_poc11				: 32;
	u32 ref_pic_poc12				: 32;
	u32 ref_pic_poc13				: 32;
	u32 ref_pic_poc14				: 32;
	u32 reserved3					: 32;
	u32 ref_is_valid				: 15;
	u32 reserved4					: 1;

	/* tile info*/
	u16 num_tile_columns				: 5;
	u16 num_tile_rows				: 5;
	u32 column_width0				: 24;
	u32 column_width1				: 24;
	u32 column_width2				: 24;
	u32 column_width3				: 24;
	u32 column_width4				: 24;
	u32 column_width5				: 24;
	u32 column_width6				: 24;
	u32 column_width7				: 24;
	u32 column_width8				: 24;
	u32 column_width9				: 24;
	u32 row_height0					: 24;
	u32 row_height1					: 24;
	u32 row_height2					: 24;
	u32 row_height3					: 24;
	u32 row_height4					: 24;
	u32 row_height5					: 24;
	u32 row_height6					: 24;
	u32 row_height7					: 24;
	u32 row_height8					: 24;
	u32 row_height9					: 24;
	u32 row_height10				: 24;
	u32 reserved5					: 2;
	u32 padding;
} __packed;

struct rkvdec_hevc_priv_tbl {
	struct rkvdec_hevc_sps_pps param_set;
	struct rkvdec_rps rps;
	struct scaling_factor scaling_list;
	u8 cabac_table[27456];
}  __packed;

struct rkvdec_hevc_ctx {
	struct rkvdec_aux_buf			priv_tbl;
	struct v4l2_ctrl_hevc_scaling_matrix	scaling_matrix_cache;
	struct v4l2_ctrl_hevc_ext_sps_st_rps	st_cache;
	struct vdpu383_regs_h26x		regs;
};

static void set_column_row(struct rkvdec_hevc_sps_pps *hw_ps, u16 *column, u16 *row)
{
	hw_ps->column_width0 = column[0] | (column[1] << 12);
	hw_ps->row_height0 = row[0] | (row[1] << 12);
	hw_ps->column_width1 = column[2] | (column[3] << 12);
	hw_ps->row_height1 = row[2] | (row[3] << 12);
	hw_ps->column_width2 = column[4] | (column[5] << 12);
	hw_ps->row_height2 = row[4] | (row[5] << 12);
	hw_ps->column_width3 = column[6] | (column[7] << 12);
	hw_ps->row_height3 = row[6] | (row[7] << 12);
	hw_ps->column_width4 = column[8] | (column[9] << 12);
	hw_ps->row_height4 = row[8] | (row[9] << 12);
	hw_ps->column_width5 = column[10] | (column[11] << 12);
	hw_ps->row_height5 = row[10] | (row[11] << 12);
	hw_ps->column_width6 = column[12] | (column[13] << 12);
	hw_ps->row_height6 = row[12] | (row[13] << 12);
	hw_ps->column_width7 = column[14] | (column[15] << 12);
	hw_ps->row_height7 = row[14] | (row[15] << 12);
	hw_ps->column_width8 = column[16] | (column[17] << 12);
	hw_ps->row_height8 = row[16] | (row[17] << 12);
	hw_ps->column_width9 = column[18] | (column[19] << 12);
	hw_ps->row_height9 = row[18] | (row[19] << 12);

	hw_ps->row_height10 = row[20] | (row[21] << 12);
}

static void set_pps_ref_pic_poc(struct rkvdec_hevc_sps_pps *hw_ps, const struct v4l2_hevc_dpb_entry *dpb)
{
	hw_ps->ref_pic_poc0 = dpb[0].pic_order_cnt_val;
	hw_ps->ref_pic_poc1 = dpb[1].pic_order_cnt_val;
	hw_ps->ref_pic_poc2 = dpb[2].pic_order_cnt_val;
	hw_ps->ref_pic_poc3 = dpb[3].pic_order_cnt_val;
	hw_ps->ref_pic_poc4 = dpb[4].pic_order_cnt_val;
	hw_ps->ref_pic_poc5 = dpb[5].pic_order_cnt_val;
	hw_ps->ref_pic_poc6 = dpb[6].pic_order_cnt_val;
	hw_ps->ref_pic_poc7 = dpb[7].pic_order_cnt_val;
	hw_ps->ref_pic_poc8 = dpb[8].pic_order_cnt_val;
	hw_ps->ref_pic_poc9 = dpb[9].pic_order_cnt_val;
	hw_ps->ref_pic_poc10 = dpb[10].pic_order_cnt_val;
	hw_ps->ref_pic_poc11 = dpb[11].pic_order_cnt_val;
	hw_ps->ref_pic_poc12 = dpb[12].pic_order_cnt_val;
	hw_ps->ref_pic_poc13 = dpb[13].pic_order_cnt_val;
	hw_ps->ref_pic_poc14 = dpb[14].pic_order_cnt_val;
}

static void assemble_hw_pps(struct rkvdec_ctx *ctx,
			    struct rkvdec_hevc_run *run)
{
	struct rkvdec_hevc_ctx *h264_ctx = ctx->priv;
	const struct v4l2_ctrl_hevc_sps *sps = run->sps;
	const struct v4l2_ctrl_hevc_pps *pps = run->pps;
	const struct v4l2_ctrl_hevc_decode_params *dec_params = run->decode_params;
	struct rkvdec_hevc_priv_tbl *priv_tbl = h264_ctx->priv_tbl.cpu;
	struct rkvdec_hevc_sps_pps *hw_ps;
	bool tiles_enabled;
	s32 max_cu_width;
	s32 pic_in_cts_width;
	s32 pic_in_cts_height;
	u16 log2_min_cb_size, width, height;
	u16 column_width[22];
	u16 row_height[22];
	u8 pcm_enabled;
	u32 i;

	/*
	 * HW read the SPS/PPS information from PPS packet index by PPS id.
	 * offset from the base can be calculated by PPS_id * 32 (size per PPS
	 * packet unit). so the driver copy SPS/PPS information to the exact PPS
	 * packet unit for HW accessing.
	 */
	hw_ps = &priv_tbl->param_set;
	memset(hw_ps, 0, sizeof(*hw_ps));

	/* write sps */
	hw_ps->video_parameters_set_id = sps->video_parameter_set_id;
	hw_ps->seq_parameters_set_id_sps = sps->seq_parameter_set_id;
	hw_ps->chroma_format_idc = sps->chroma_format_idc;

	log2_min_cb_size = sps->log2_min_luma_coding_block_size_minus3 + 3;
	width = sps->pic_width_in_luma_samples;
	height = sps->pic_height_in_luma_samples;
	hw_ps->width = width;
	hw_ps->height = height;
	hw_ps->bit_depth_luma = sps->bit_depth_luma_minus8 + 8;
	hw_ps->bit_depth_chroma = sps->bit_depth_chroma_minus8 + 8;
	hw_ps->max_pic_order_count_lsb = sps->log2_max_pic_order_cnt_lsb_minus4 + 4;
	hw_ps->diff_max_min_luma_coding_block_size = sps->log2_diff_max_min_luma_coding_block_size;
	hw_ps->min_luma_coding_block_size = sps->log2_min_luma_coding_block_size_minus3 + 3;
	hw_ps->min_transform_block_size = sps->log2_min_luma_transform_block_size_minus2 + 2;
	hw_ps->diff_max_min_transform_block_size =
		sps->log2_diff_max_min_luma_transform_block_size;
	hw_ps->max_transform_hierarchy_depth_inter = sps->max_transform_hierarchy_depth_inter;
	hw_ps->max_transform_hierarchy_depth_intra = sps->max_transform_hierarchy_depth_intra;
	hw_ps->scaling_list_enabled_flag =
		!!(sps->flags & V4L2_HEVC_SPS_FLAG_SCALING_LIST_ENABLED);
	hw_ps->amp_enabled_flag = !!(sps->flags & V4L2_HEVC_SPS_FLAG_AMP_ENABLED);
	hw_ps->sample_adaptive_offset_enabled_flag =
		!!(sps->flags & V4L2_HEVC_SPS_FLAG_SAMPLE_ADAPTIVE_OFFSET);

	pcm_enabled = !!(sps->flags & V4L2_HEVC_SPS_FLAG_PCM_ENABLED);
	hw_ps->pcm_enabled_flag = pcm_enabled;
	hw_ps->pcm_sample_bit_depth_luma =
		pcm_enabled ? sps->pcm_sample_bit_depth_luma_minus1 + 1 : 0;
	hw_ps->pcm_sample_bit_depth_chroma =
		pcm_enabled ? sps->pcm_sample_bit_depth_chroma_minus1 + 1 : 0;
	hw_ps->pcm_loop_filter_disabled_flag =
		!!(sps->flags & V4L2_HEVC_SPS_FLAG_PCM_LOOP_FILTER_DISABLED);
	hw_ps->diff_max_min_pcm_luma_coding_block_size =
		sps->log2_diff_max_min_pcm_luma_coding_block_size;
	hw_ps->min_pcm_luma_coding_block_size =
		pcm_enabled ? sps->log2_min_pcm_luma_coding_block_size_minus3 + 3 : 0;
	hw_ps->num_short_term_ref_pic_sets = sps->num_short_term_ref_pic_sets;
	hw_ps->long_term_ref_pics_present_flag =
		!!(sps->flags & V4L2_HEVC_SPS_FLAG_LONG_TERM_REF_PICS_PRESENT);
	hw_ps->num_long_term_ref_pics_sps = sps->num_long_term_ref_pics_sps;
	hw_ps->sps_temporal_mvp_enabled_flag =
		!!(sps->flags & V4L2_HEVC_SPS_FLAG_SPS_TEMPORAL_MVP_ENABLED);
	hw_ps->strong_intra_smoothing_enabled_flag =
		!!(sps->flags & V4L2_HEVC_SPS_FLAG_STRONG_INTRA_SMOOTHING_ENABLED);
	hw_ps->sps_max_dec_pic_buffering_minus1 = sps->sps_max_dec_pic_buffering_minus1;

	/* write pps */
	hw_ps->picture_parameters_set_id = pps->pic_parameter_set_id;
	hw_ps->seq_parameters_set_id_pps = sps->seq_parameter_set_id;
	hw_ps->dependent_slice_segments_enabled_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_DEPENDENT_SLICE_SEGMENT_ENABLED);
	hw_ps->output_flag_present_flag = !!(pps->flags & V4L2_HEVC_PPS_FLAG_OUTPUT_FLAG_PRESENT);
	hw_ps->num_extra_slice_header_bits = pps->num_extra_slice_header_bits;
	hw_ps->sign_data_hiding_enabled_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_SIGN_DATA_HIDING_ENABLED);
	hw_ps->cabac_init_present_flag = !!(pps->flags & V4L2_HEVC_PPS_FLAG_CABAC_INIT_PRESENT);
	hw_ps->num_ref_idx_l0_default_active = pps->num_ref_idx_l0_default_active_minus1 + 1;
	hw_ps->num_ref_idx_l1_default_active = pps->num_ref_idx_l1_default_active_minus1 + 1;
	hw_ps->init_qp_minus26 = pps->init_qp_minus26;
	hw_ps->constrained_intra_pred_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_CONSTRAINED_INTRA_PRED);
	hw_ps->transform_skip_enabled_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_TRANSFORM_SKIP_ENABLED);
	hw_ps->cu_qp_delta_enabled_flag = !!(pps->flags & V4L2_HEVC_PPS_FLAG_CU_QP_DELTA_ENABLED);
	hw_ps->log2_min_cb_size = log2_min_cb_size +
				  sps->log2_diff_max_min_luma_coding_block_size -
				  pps->diff_cu_qp_delta_depth;
	hw_ps->pps_cb_qp_offset = pps->pps_cb_qp_offset;
	hw_ps->pps_cr_qp_offset = pps->pps_cr_qp_offset;
	hw_ps->pps_slice_chroma_qp_offsets_present_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT);
	hw_ps->weighted_pred_flag = !!(pps->flags & V4L2_HEVC_PPS_FLAG_WEIGHTED_PRED);
	hw_ps->weighted_bipred_flag = !!(pps->flags & V4L2_HEVC_PPS_FLAG_WEIGHTED_BIPRED);
	hw_ps->transquant_bypass_enabled_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_TRANSQUANT_BYPASS_ENABLED);
	tiles_enabled = !!(pps->flags & V4L2_HEVC_PPS_FLAG_TILES_ENABLED);
	hw_ps->tiles_enabled_flag = tiles_enabled;
	hw_ps->entropy_coding_sync_enabled_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED);
	hw_ps->pps_loop_filter_across_slices_enabled_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED);
	hw_ps->loop_filter_across_tiles_enabled_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_LOOP_FILTER_ACROSS_TILES_ENABLED);
	hw_ps->deblocking_filter_override_enabled_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_OVERRIDE_ENABLED);
	hw_ps->pps_deblocking_filter_disabled_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_PPS_DISABLE_DEBLOCKING_FILTER);
	hw_ps->pps_beta_offset_div2 = pps->pps_beta_offset_div2;
	hw_ps->pps_tc_offset_div2 = pps->pps_tc_offset_div2;
	hw_ps->lists_modification_present_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_LISTS_MODIFICATION_PRESENT);
	hw_ps->log2_parallel_merge_level = pps->log2_parallel_merge_level_minus2 + 2;
	hw_ps->slice_segment_header_extension_present_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_SLICE_SEGMENT_HEADER_EXTENSION_PRESENT);
	hw_ps->num_tile_columns = tiles_enabled ? pps->num_tile_columns_minus1 + 1 : 1;
	hw_ps->num_tile_rows = tiles_enabled ? pps->num_tile_rows_minus1 + 1 : 1;
	hw_ps->mvc_ff = 0xffff;

	// Setup tiles information
	memset(column_width, 0, sizeof(column_width));
	memset(row_height, 0, sizeof(row_height));

	max_cu_width = 1 << (sps->log2_diff_max_min_luma_coding_block_size + log2_min_cb_size);
	pic_in_cts_width = (width + max_cu_width - 1) / max_cu_width;
	pic_in_cts_height = (height + max_cu_width - 1) / max_cu_width;

	if (tiles_enabled) {
		if (pps->flags & V4L2_HEVC_PPS_FLAG_UNIFORM_SPACING) {
			compute_tiles_uniform(run, log2_min_cb_size, width, height,
					      pic_in_cts_width, pic_in_cts_height,
					      column_width, row_height);
		} else {
			compute_tiles_non_uniform(run, log2_min_cb_size, width, height,
						  pic_in_cts_width, pic_in_cts_height,
						  column_width, row_height);
		}
	} else {
		column_width[0] = (width + max_cu_width - 1) / max_cu_width;
		row_height[0] = (height + max_cu_width - 1) / max_cu_width;
	}

	set_column_row(hw_ps, column_width, row_height);

	// Setup POC information
	hw_ps->current_poc = dec_params->pic_order_cnt_val;

	set_pps_ref_pic_poc(hw_ps, dec_params->dpb);
	for (i = 0; i < ARRAY_SIZE(dec_params->dpb); i++) {
		u32 valid = !!(dec_params->num_active_dpb_entries > i);
		hw_ps->ref_is_valid |= valid << i;
	}
}

static void rkvdec_write_regs(struct rkvdec_ctx *ctx)
{
	struct rkvdec_dev *rkvdec = ctx->dev;
	struct rkvdec_hevc_ctx *h265_ctx = ctx->priv;

	rkvdec_memcpy_toio(rkvdec->regs + VDPU383_OFFSET_COMMON_REGS,
			   &h265_ctx->regs.common,
			   sizeof(h265_ctx->regs.common));
	rkvdec_memcpy_toio(rkvdec->regs + VDPU383_OFFSET_COMMON_ADDR_REGS,
			   &h265_ctx->regs.common_addr,
			   sizeof(h265_ctx->regs.common_addr));
	rkvdec_memcpy_toio(rkvdec->regs + VDPU383_OFFSET_CODEC_PARAMS_REGS,
			   &h265_ctx->regs.h26x_params,
			   sizeof(h265_ctx->regs.h26x_params));
	rkvdec_memcpy_toio(rkvdec->regs + VDPU383_OFFSET_CODEC_ADDR_REGS,
			   &h265_ctx->regs.h26x_addr,
			   sizeof(h265_ctx->regs.h26x_addr));
}

static void config_registers(struct rkvdec_ctx *ctx,
			     struct rkvdec_hevc_run *run)
{
	const struct v4l2_ctrl_hevc_decode_params *dec_params = run->decode_params;
	struct rkvdec_hevc_ctx *h265_ctx = ctx->priv;
	const struct v4l2_ctrl_hevc_sps *sps = run->sps;
	dma_addr_t priv_start_addr = h265_ctx->priv_tbl.dma;
	const struct v4l2_pix_format_mplane *dst_fmt;
	struct vb2_v4l2_buffer *src_buf = run->base.bufs.src;
	struct vb2_v4l2_buffer *dst_buf = run->base.bufs.dst;
	struct vdpu383_regs_h26x *regs = &h265_ctx->regs;
	const struct v4l2_format *f;
	dma_addr_t rlc_addr;
	dma_addr_t dst_addr;
	u32 hor_virstride;
	u32 ver_virstride;
	u32 y_virstride;
	u32 offset;
	u32 pixels;
	u32 i;

	memset(regs, 0, sizeof(*regs));

	/* Set HEVC mode */
	regs->common.reg008_dec_mode = VDPU383_MODE_HEVC;

	/* Set input stream length */
	regs->h26x_params.reg066_stream_len = vb2_get_plane_payload(&src_buf->vb2_buf, 0);

	/* Set strides */
	f = &ctx->decoded_fmt;
	dst_fmt = &f->fmt.pix_mp;
	hor_virstride = dst_fmt->plane_fmt[0].bytesperline;
	ver_virstride = dst_fmt->height;
	y_virstride = hor_virstride * ver_virstride;

	pixels = dst_fmt->height * dst_fmt->width;

	regs->h26x_params.reg068_hor_virstride = hor_virstride / 16;
	regs->h26x_params.reg069_raster_uv_hor_virstride = hor_virstride / 16;
	regs->h26x_params.reg070_y_virstride = y_virstride / 16;

	/* Activate block gating */
	regs->common.reg010_block_gating_en.strmd_auto_gating_e      = 1;
	regs->common.reg010_block_gating_en.inter_auto_gating_e      = 1;
	regs->common.reg010_block_gating_en.intra_auto_gating_e      = 1;
	regs->common.reg010_block_gating_en.transd_auto_gating_e     = 1;
	regs->common.reg010_block_gating_en.recon_auto_gating_e      = 1;
	regs->common.reg010_block_gating_en.filterd_auto_gating_e    = 1;
	regs->common.reg010_block_gating_en.bus_auto_gating_e	     = 1;
	regs->common.reg010_block_gating_en.ctrl_auto_gating_e       = 1;
	regs->common.reg010_block_gating_en.rcb_auto_gating_e	     = 1;
	regs->common.reg010_block_gating_en.err_prc_auto_gating_e    = 1;

	/* Set timeout threshold */
	if (pixels < RKVDEC_1080P_PIXELS)
		regs->common.reg013_core_timeout_threshold = VDPU383_TIMEOUT_1080p;
	else if (pixels < RKVDEC_4K_PIXELS)
		regs->common.reg013_core_timeout_threshold = VDPU383_TIMEOUT_4K;
	else if (pixels < RKVDEC_8K_PIXELS)
		regs->common.reg013_core_timeout_threshold = VDPU383_TIMEOUT_8K;
	else
		regs->common.reg013_core_timeout_threshold = VDPU383_TIMEOUT_MAX;

	regs->common.reg016_error_ctrl_set.error_proc_disable = 1;

	/* Set ref pic address & poc */
	for (i = 0; i < ARRAY_SIZE(dec_params->dpb) - 1; i++) {
		struct vb2_buffer *vb_buf = get_ref_buf(ctx, run, i);
		dma_addr_t buf_dma;

		buf_dma = vb2_dma_contig_plane_dma_addr(vb_buf, 0);

		/* Set reference addresses */
		regs->h26x_addr.reg170_185_ref_base[i] = buf_dma;
		regs->h26x_addr.reg195_210_payload_st_ref_base[i] = buf_dma;

		/* Set COLMV addresses */
		regs->h26x_addr.reg217_232_colmv_ref_base[i] = buf_dma + ctx->colmv_offset;
	}

	/* Set rlc base address (input stream) */
	rlc_addr = vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);
	regs->common_addr.reg128_strm_base = rlc_addr;

	/* Set output base address */
	dst_addr = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);
	regs->h26x_addr.reg168_decout_base = dst_addr;
	regs->h26x_addr.reg169_error_ref_base = dst_addr;
	regs->h26x_addr.reg192_payload_st_cur_base = dst_addr;

	/* Set colmv address */
	regs->h26x_addr.reg216_colmv_cur_base = dst_addr + ctx->colmv_offset;

	/* Set RCB addresses */
	for (i = 0; i < rkvdec_rcb_buf_count(ctx); i++) {
		regs->common_addr.reg140_162_rcb_info[i].offset = rkvdec_rcb_buf_dma_addr(ctx, i);
		regs->common_addr.reg140_162_rcb_info[i].size = rkvdec_rcb_buf_size(ctx, i);
	}

	if (sps->flags & V4L2_HEVC_SPS_FLAG_SCALING_LIST_ENABLED) {
		/* Set scaling matrix */
		offset = offsetof(struct rkvdec_hevc_priv_tbl, scaling_list);
		regs->common_addr.reg132_scanlist_addr = priv_start_addr + offset;
	}

	/* Set hw pps address */
	offset = offsetof(struct rkvdec_hevc_priv_tbl, param_set);
	regs->common_addr.reg131_gbl_base = priv_start_addr + offset;
	regs->h26x_params.reg067_global_len = sizeof(struct rkvdec_hevc_sps_pps) / 16;

	/* Set hw rps address */
	offset = offsetof(struct rkvdec_hevc_priv_tbl, rps);
	regs->common_addr.reg129_rps_base = priv_start_addr + offset;

	/* Set cabac table */
	offset = offsetof(struct rkvdec_hevc_priv_tbl, cabac_table);
	regs->common_addr.reg130_cabactbl_base = priv_start_addr + offset;

	rkvdec_write_regs(ctx);
}

static int rkvdec_hevc_validate_sps(struct rkvdec_ctx *ctx,
				    const struct v4l2_ctrl_hevc_sps *sps)
{
	if (sps->chroma_format_idc != 1)
		/* Only 4:2:0 is supported */
		return -EINVAL;

	if (sps->bit_depth_luma_minus8 != sps->bit_depth_chroma_minus8)
		/* Luma and chroma bit depth mismatch */
		return -EINVAL;

	if (sps->bit_depth_luma_minus8 != 0 && sps->bit_depth_luma_minus8 != 2)
		/* Only 8-bit and 10-bit are supported */
		return -EINVAL;

	if (sps->pic_width_in_luma_samples > ctx->coded_fmt.fmt.pix_mp.width ||
	    sps->pic_height_in_luma_samples > ctx->coded_fmt.fmt.pix_mp.height)
		return -EINVAL;

	return 0;
}

static int rkvdec_hevc_start(struct rkvdec_ctx *ctx)
{
	struct rkvdec_dev *rkvdec = ctx->dev;
	struct rkvdec_hevc_priv_tbl *priv_tbl;
	struct rkvdec_hevc_ctx *hevc_ctx;
	struct v4l2_ctrl *ctrl;
	int ret;

	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_HEVC_SPS);
	if (!ctrl)
		return -EINVAL;

	ret = rkvdec_hevc_validate_sps(ctx, ctrl->p_new.p_hevc_sps);
	if (ret)
		return ret;

	hevc_ctx = kzalloc(sizeof(*hevc_ctx), GFP_KERNEL);
	if (!hevc_ctx)
		return -ENOMEM;

	priv_tbl = dma_alloc_coherent(rkvdec->dev, sizeof(*priv_tbl),
				      &hevc_ctx->priv_tbl.dma, GFP_KERNEL);
	if (!priv_tbl) {
		ret = -ENOMEM;
		goto err_free_ctx;
	}

	hevc_ctx->priv_tbl.size = sizeof(*priv_tbl);
	hevc_ctx->priv_tbl.cpu = priv_tbl;
	memcpy(priv_tbl->cabac_table, rkvdec_hevc_cabac_table,
	       sizeof(rkvdec_hevc_cabac_table));

	ctx->priv = hevc_ctx;
	return 0;

err_free_ctx:
	kfree(hevc_ctx);
	return ret;
}

static void rkvdec_hevc_stop(struct rkvdec_ctx *ctx)
{
	struct rkvdec_hevc_ctx *hevc_ctx = ctx->priv;
	struct rkvdec_dev *rkvdec = ctx->dev;

	dma_free_coherent(rkvdec->dev, hevc_ctx->priv_tbl.size,
			  hevc_ctx->priv_tbl.cpu, hevc_ctx->priv_tbl.dma);
	kfree(hevc_ctx);
}

static int rkvdec_hevc_run(struct rkvdec_ctx *ctx)
{
	struct rkvdec_dev *rkvdec = ctx->dev;
	struct rkvdec_hevc_run run;
	struct rkvdec_hevc_ctx *hevc_ctx = ctx->priv;
	struct rkvdec_hevc_priv_tbl *tbl = hevc_ctx->priv_tbl.cpu;
	u32 timeout_threshold;

	rkvdec_hevc_run_preamble(ctx, &run);

	/*
	 * On vdpu383, not setting the long and short term ref sets leads to IOMMU page faults.
	 * To be on the safe side for this new v4l2 control, write an error in the log and mark
	 * the buffer as failed by returning an error here.
	 */
	if ((!ctx->has_sps_lt_rps && run.sps->num_long_term_ref_pics_sps) ||
	    (!ctx->has_sps_st_rps && run.sps->num_short_term_ref_pic_sets)) {
		dev_err_ratelimited(rkvdec->dev, "Long and short term RPS not set\n");
		return -EINVAL;
	}

	rkvdec_hevc_assemble_hw_scaling_list(ctx, &run, &tbl->scaling_list,
					     &hevc_ctx->scaling_matrix_cache);
	assemble_hw_pps(ctx, &run);
	rkvdec_hevc_assemble_hw_rps(&run, &tbl->rps, &hevc_ctx->st_cache);

	config_registers(ctx, &run);

	rkvdec_run_postamble(ctx, &run.base);

	timeout_threshold = hevc_ctx->regs.common.reg013_core_timeout_threshold;
	rkvdec_schedule_watchdog(rkvdec, timeout_threshold);

	/* Start decoding! */
	writel(timeout_threshold, rkvdec->link + VDPU383_LINK_TIMEOUT_THRESHOLD);
	writel(VDPU383_IP_CRU_MODE, rkvdec->link + VDPU383_LINK_IP_ENABLE);
	writel(VDPU383_DEC_E_BIT, rkvdec->link + VDPU383_LINK_DEC_ENABLE);

	return 0;
}

static int rkvdec_hevc_try_ctrl(struct rkvdec_ctx *ctx, struct v4l2_ctrl *ctrl)
{
	if (ctrl->id == V4L2_CID_STATELESS_HEVC_SPS)
		return rkvdec_hevc_validate_sps(ctx, ctrl->p_new.p_hevc_sps);

	return 0;
}

const struct rkvdec_coded_fmt_ops rkvdec_vdpu383_hevc_fmt_ops = {
	.adjust_fmt = rkvdec_hevc_adjust_fmt,
	.start = rkvdec_hevc_start,
	.stop = rkvdec_hevc_stop,
	.run = rkvdec_hevc_run,
	.try_ctrl = rkvdec_hevc_try_ctrl,
	.get_image_fmt = rkvdec_hevc_get_image_fmt,
};
