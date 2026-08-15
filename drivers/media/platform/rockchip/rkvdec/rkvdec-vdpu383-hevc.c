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
#include "rkvdec-bitwriter.h"

#define VIDEO_PARAMETER_SET_ID				BW_FIELD(0, 4)
#define SEQ_PARAMETER_SET_ID				BW_FIELD(4, 4)
#define CHROMA_FORMAT_IDC				BW_FIELD(8, 2)
#define PIC_WIDTH_IN_LUMA_SAMPLES			BW_FIELD(10, 16)
#define PIC_HEIGHT_IN_LUMA_SAMPLES			BW_FIELD(26, 16)
#define BIT_DEPTH_LUMA					BW_FIELD(42, 3)
#define BIT_DEPTH_CHROMA				BW_FIELD(45, 3)
#define LOG2_MAX_PIC_ORDER_CNT_LSB			BW_FIELD(48, 5)
#define LOG2_DIFF_MAX_MIN_LUMA_CODING_BLOCK_SIZE	BW_FIELD(53, 2)
#define LOG2_MIN_LUMA_CODING_BLOCK_SIZE			BW_FIELD(55, 3)
#define LOG2_MIN_TRANSFORM_BLOCK_SIZE			BW_FIELD(58, 3)
#define LOG2_DIFF_MAX_MIN_LUMA_TRANSFORM_BLOCK_SIZE	BW_FIELD(61, 2)
#define MAX_TRANSFORM_HIERARCHY_DEPTH_INTER		BW_FIELD(63, 3)
#define MAX_TRANSFORM_HIERARCHY_DEPTH_INTRA		BW_FIELD(66, 3)
#define SCALING_LIST_ENABLED_FLAG			BW_FIELD(69, 1)
#define AMP_ENABLED_FLAG				BW_FIELD(70, 1)
#define SAMPLE_ADAPTIVE_OFFSET_ENABLED_FLAG		BW_FIELD(71, 1)
#define PCM_ENABLED_FLAG				BW_FIELD(72, 1)
#define PCM_SAMPLE_BIT_DEPTH_LUMA			BW_FIELD(73, 4)
#define PCM_SAMPLE_BIT_DEPTH_CHROMA			BW_FIELD(77, 4)
#define PCM_LOOP_FILTER_DISABLED_FLAG			BW_FIELD(81, 1)
#define LOG2_DIFF_MAX_MIN_PCM_LUMA_CODING_BLOCK_SIZE	BW_FIELD(82, 3)
#define LOG2_MIN_PCM_LUMA_CODING_BLOCK_SIZE		BW_FIELD(85, 3)
#define NUM_SHORT_TERM_REF_PIC_SETS			BW_FIELD(88, 7)
#define LONG_TERM_REF_PICS_PRESENT_FLAG			BW_FIELD(95, 1)
#define NUM_LONG_TERM_REF_PICS_SPS			BW_FIELD(96, 6)
#define SPS_TEMPORAL_MVP_ENABLED_FLAG			BW_FIELD(102, 1)
#define STRONG_INTRA_SMOOTHING_ENABLED_FLAG		BW_FIELD(103, 1)
#define SPS_MAX_DEC_PIC_BUFFERING_MINUS1		BW_FIELD(111, 4)
#define SEPARATE_COLOUR_PLANE_FLAG			BW_FIELD(115, 1)
#define HIGH_PRECISION_OFFSETS_ENABLED_FLAG		BW_FIELD(116, 1)
#define PERSISTENT_RICE_ADAPTATION_ENABLED_FLAG		BW_FIELD(117, 1)

/* PPS */
#define PIC_PARAMETER_SET_ID				BW_FIELD(118, 6)
#define PPS_SEQ_PARAMETER_SET_ID			BW_FIELD(124, 4)
#define DEPENDENT_SLICE_SEGMENTS_ENABLED_FLAG		BW_FIELD(128, 1)
#define OUTPUT_FLAG_PRESENT_FLAG			BW_FIELD(129, 1)
#define NUM_EXTRA_SLICE_HEADER_BITS			BW_FIELD(130, 13)
#define SIGN_DATA_HIDING_ENABLED_FLAG			BW_FIELD(143, 1)
#define CABAC_INIT_PRESENT_FLAG				BW_FIELD(144, 1)
#define NUM_REF_IDX_L0_DEFAULT_ACTIVE			BW_FIELD(145, 4)
#define NUM_REF_IDX_L1_DEFAULT_ACTIVE			BW_FIELD(149, 4)
#define INIT_QP_MINUS26					BW_FIELD(153, 7)
#define CONSTRAINED_INTRA_PRED_FLAG			BW_FIELD(160, 1)
#define TRANSFORM_SKIP_ENABLED_FLAG			BW_FIELD(161, 1)
#define CU_QP_DELTA_ENABLED_FLAG			BW_FIELD(162, 1)
#define LOG2_MIN_CU_QP_DELTA_SIZE			BW_FIELD(163, 3)
#define PPS_CB_QP_OFFSET				BW_FIELD(166, 5)
#define PPS_CR_QP_OFFSET				BW_FIELD(171, 5)
#define PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT_FLAG	BW_FIELD(176, 1)
#define WEIGHTED_PRED_FLAG				BW_FIELD(177, 1)
#define WEIGHTED_BIPRED_FLAG				BW_FIELD(178, 1)
#define TRANSQUANT_BYPASS_ENABLED_FLAG			BW_FIELD(179, 1)
#define TILES_ENABLED_FLAG				BW_FIELD(180, 1)
#define ENTROPY_CODING_SYNC_ENABLED_FLAG		BW_FIELD(181, 1)
#define PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG	BW_FIELD(182, 1)
#define LOOP_FILTER_ACROSS_TILES_ENABLED_FLAG		BW_FIELD(183, 1)
#define DEBLOCKING_FILTER_OVERRIDE_ENABLED_FLAG		BW_FIELD(184, 1)
#define PPS_DEBLOCKING_FILTER_DISABLED_FLAG		BW_FIELD(185, 1)
#define PPS_BETA_OFFSET_DIV2				BW_FIELD(186, 4)
#define PPS_TC_OFFSET_DIV2				BW_FIELD(190, 4)
#define LISTS_MODIFICATION_PRESENT_FLAG			BW_FIELD(194, 1)
#define LOG2_PARALLEL_MERGE_LEVEL			BW_FIELD(195, 3)
#define SLICE_SEGMENT_HEADER_EXTENSION_PRESENT_FLAG	BW_FIELD(198, 1)

/* pps extensions */
#define LOG2_MAX_TRANSFORM_SKIP_BLOCK_SIZE		BW_FIELD(202, 2)
#define CROSS_COMPONENT_PREDICTION_ENABLED_FLAG		BW_FIELD(204, 1)
#define CHROMA_QP_OFFSET_LIST_ENABLED_FLAG		BW_FIELD(205, 1)
#define LOG2_MIN_CU_CHROMA_QP_DELTA_SIZE		BW_FIELD(206, 3)
#define CB_QP_OFFSET_LIST(i)				BW_FIELD(209 + (i) * 5, 5) // i: 0-5
#define CB_CR_OFFSET_LIST(i)				BW_FIELD(239 + (i) * 5, 5) // i: 0-5
#define CHROMA_QP_OFFSET_LIST_LEN_MINUS1		BW_FIELD(269, 3)

/* mvc0 && mvc1 */
#define MVC_FF						BW_FIELD(272, 16)
#define MVC_00						BW_FIELD(288, 9)

/* poc info */
#define RESERVED2					BW_FIELD(297, 3)
#define CURRENT_POC					BW_FIELD(300, 32)
#define REF_PIC_POC(i)					BW_FIELD(332 + (i) * 32, 32) // i: 0-14
#define RESERVED3					BW_FIELD(812, 32)
#define REF_IS_VALID(i)					BW_FIELD(844 + (i), 1) // i: 0-14
#define RESERVED4					BW_FIELD(859, 1)

/* tile info*/
#define NUM_TILE_COLUMNS				BW_FIELD(860, 5)
#define NUM_TILE_ROWS					BW_FIELD(865, 5)
#define COLUMN_WIDTH(i)					BW_FIELD(870 + (i) * 12, 12) // i: 0-19
#define ROW_HEIGHT(i)					BW_FIELD(1110 + (i) * 12, 12) // i: 0-21

#define HEVC_SPS_SIZE					ALIGN(1110 + 22 * 12, 256)

struct rkvdec_hevc_sps_pps {
	u32 info[HEVC_SPS_SIZE / 8 / 4];
};

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
	rkvdec_set_bw_field(hw_ps->info, VIDEO_PARAMETER_SET_ID, sps->video_parameter_set_id);
	rkvdec_set_bw_field(hw_ps->info, SEQ_PARAMETER_SET_ID, sps->seq_parameter_set_id);
	rkvdec_set_bw_field(hw_ps->info, CHROMA_FORMAT_IDC, sps->chroma_format_idc);

	log2_min_cb_size = sps->log2_min_luma_coding_block_size_minus3 + 3;
	width = sps->pic_width_in_luma_samples;
	height = sps->pic_height_in_luma_samples;

	rkvdec_set_bw_field(hw_ps->info, PIC_WIDTH_IN_LUMA_SAMPLES, width);
	rkvdec_set_bw_field(hw_ps->info, PIC_HEIGHT_IN_LUMA_SAMPLES, height);
	rkvdec_set_bw_field(hw_ps->info, BIT_DEPTH_LUMA, sps->bit_depth_luma_minus8 + 8);
	rkvdec_set_bw_field(hw_ps->info, BIT_DEPTH_CHROMA, sps->bit_depth_chroma_minus8 + 8);
	rkvdec_set_bw_field(hw_ps->info, LOG2_MAX_PIC_ORDER_CNT_LSB,
			    sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
	rkvdec_set_bw_field(hw_ps->info, LOG2_DIFF_MAX_MIN_LUMA_CODING_BLOCK_SIZE,
			    sps->log2_diff_max_min_luma_coding_block_size);
	rkvdec_set_bw_field(hw_ps->info, LOG2_MIN_LUMA_CODING_BLOCK_SIZE,
			    sps->log2_min_luma_coding_block_size_minus3 + 3);
	rkvdec_set_bw_field(hw_ps->info, LOG2_MIN_TRANSFORM_BLOCK_SIZE,
			    sps->log2_min_luma_transform_block_size_minus2 + 2);
	rkvdec_set_bw_field(hw_ps->info, LOG2_DIFF_MAX_MIN_LUMA_TRANSFORM_BLOCK_SIZE,
			    sps->log2_diff_max_min_luma_transform_block_size);
	rkvdec_set_bw_field(hw_ps->info, MAX_TRANSFORM_HIERARCHY_DEPTH_INTER,
			    sps->max_transform_hierarchy_depth_inter);
	rkvdec_set_bw_field(hw_ps->info, MAX_TRANSFORM_HIERARCHY_DEPTH_INTRA,
			    sps->max_transform_hierarchy_depth_intra);
	rkvdec_set_bw_field(hw_ps->info, SCALING_LIST_ENABLED_FLAG,
			    !!(sps->flags & V4L2_HEVC_SPS_FLAG_SCALING_LIST_ENABLED));
	rkvdec_set_bw_field(hw_ps->info, AMP_ENABLED_FLAG,
			    !!(sps->flags & V4L2_HEVC_SPS_FLAG_AMP_ENABLED));
	rkvdec_set_bw_field(hw_ps->info, SAMPLE_ADAPTIVE_OFFSET_ENABLED_FLAG,
			    !!(sps->flags & V4L2_HEVC_SPS_FLAG_SAMPLE_ADAPTIVE_OFFSET));

	pcm_enabled = !!(sps->flags & V4L2_HEVC_SPS_FLAG_PCM_ENABLED);
	rkvdec_set_bw_field(hw_ps->info, PCM_ENABLED_FLAG, pcm_enabled);
	rkvdec_set_bw_field(hw_ps->info, PCM_SAMPLE_BIT_DEPTH_LUMA,
			    pcm_enabled ? sps->pcm_sample_bit_depth_luma_minus1 + 1 : 0);
	rkvdec_set_bw_field(hw_ps->info, PCM_SAMPLE_BIT_DEPTH_CHROMA,
			    pcm_enabled ? sps->pcm_sample_bit_depth_chroma_minus1 + 1 : 0);
	rkvdec_set_bw_field(hw_ps->info, PCM_LOOP_FILTER_DISABLED_FLAG,
			    !!(sps->flags & V4L2_HEVC_SPS_FLAG_PCM_LOOP_FILTER_DISABLED));
	rkvdec_set_bw_field(hw_ps->info, LOG2_DIFF_MAX_MIN_PCM_LUMA_CODING_BLOCK_SIZE,
			    sps->log2_diff_max_min_pcm_luma_coding_block_size);
	rkvdec_set_bw_field(hw_ps->info, LOG2_MIN_PCM_LUMA_CODING_BLOCK_SIZE,
			    pcm_enabled ? sps->log2_min_pcm_luma_coding_block_size_minus3 + 3 : 0);
	rkvdec_set_bw_field(hw_ps->info, NUM_SHORT_TERM_REF_PIC_SETS,
			    sps->num_short_term_ref_pic_sets);
	rkvdec_set_bw_field(hw_ps->info, LONG_TERM_REF_PICS_PRESENT_FLAG,
			    !!(sps->flags & V4L2_HEVC_SPS_FLAG_LONG_TERM_REF_PICS_PRESENT));
	rkvdec_set_bw_field(hw_ps->info, NUM_LONG_TERM_REF_PICS_SPS,
			    sps->num_long_term_ref_pics_sps);
	rkvdec_set_bw_field(hw_ps->info, SPS_TEMPORAL_MVP_ENABLED_FLAG,
			    !!(sps->flags & V4L2_HEVC_SPS_FLAG_SPS_TEMPORAL_MVP_ENABLED));
	rkvdec_set_bw_field(hw_ps->info, STRONG_INTRA_SMOOTHING_ENABLED_FLAG,
			    !!(sps->flags & V4L2_HEVC_SPS_FLAG_STRONG_INTRA_SMOOTHING_ENABLED));
	rkvdec_set_bw_field(hw_ps->info, SPS_MAX_DEC_PIC_BUFFERING_MINUS1,
			    sps->sps_max_dec_pic_buffering_minus1);

	/* write pps */
	rkvdec_set_bw_field(hw_ps->info, PIC_PARAMETER_SET_ID, pps->pic_parameter_set_id);
	rkvdec_set_bw_field(hw_ps->info, SEQ_PARAMETER_SET_ID, sps->seq_parameter_set_id);
	rkvdec_set_bw_field(hw_ps->info, DEPENDENT_SLICE_SEGMENTS_ENABLED_FLAG,
			    !!(pps->flags & V4L2_HEVC_PPS_FLAG_DEPENDENT_SLICE_SEGMENT_ENABLED));
	rkvdec_set_bw_field(hw_ps->info, OUTPUT_FLAG_PRESENT_FLAG,
			    !!(pps->flags & V4L2_HEVC_PPS_FLAG_OUTPUT_FLAG_PRESENT));
	rkvdec_set_bw_field(hw_ps->info, NUM_EXTRA_SLICE_HEADER_BITS,
			    pps->num_extra_slice_header_bits);
	rkvdec_set_bw_field(hw_ps->info, SIGN_DATA_HIDING_ENABLED_FLAG,
			    !!(pps->flags & V4L2_HEVC_PPS_FLAG_SIGN_DATA_HIDING_ENABLED));
	rkvdec_set_bw_field(hw_ps->info, CABAC_INIT_PRESENT_FLAG,
			    !!(pps->flags & V4L2_HEVC_PPS_FLAG_CABAC_INIT_PRESENT));
	rkvdec_set_bw_field(hw_ps->info, NUM_REF_IDX_L0_DEFAULT_ACTIVE,
			    pps->num_ref_idx_l0_default_active_minus1 + 1);
	rkvdec_set_bw_field(hw_ps->info, NUM_REF_IDX_L1_DEFAULT_ACTIVE,
			    pps->num_ref_idx_l1_default_active_minus1 + 1);
	rkvdec_set_bw_field(hw_ps->info, INIT_QP_MINUS26, pps->init_qp_minus26);
	rkvdec_set_bw_field(hw_ps->info, CONSTRAINED_INTRA_PRED_FLAG,
			    !!(pps->flags & V4L2_HEVC_PPS_FLAG_CONSTRAINED_INTRA_PRED));
	rkvdec_set_bw_field(hw_ps->info, TRANSFORM_SKIP_ENABLED_FLAG,
			    !!(pps->flags & V4L2_HEVC_PPS_FLAG_TRANSFORM_SKIP_ENABLED));
	rkvdec_set_bw_field(hw_ps->info, CU_QP_DELTA_ENABLED_FLAG,
			    !!(pps->flags & V4L2_HEVC_PPS_FLAG_CU_QP_DELTA_ENABLED));
	rkvdec_set_bw_field(hw_ps->info, LOG2_MIN_CU_QP_DELTA_SIZE, log2_min_cb_size +
			    sps->log2_diff_max_min_luma_coding_block_size -
			    pps->diff_cu_qp_delta_depth);
	rkvdec_set_bw_field(hw_ps->info, PPS_CB_QP_OFFSET, pps->pps_cb_qp_offset);
	rkvdec_set_bw_field(hw_ps->info, PPS_CR_QP_OFFSET, pps->pps_cr_qp_offset);
	rkvdec_set_bw_field(hw_ps->info, PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT_FLAG,
			    !!(pps->flags &
			       V4L2_HEVC_PPS_FLAG_PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT));
	rkvdec_set_bw_field(hw_ps->info, WEIGHTED_PRED_FLAG,
			    !!(pps->flags & V4L2_HEVC_PPS_FLAG_WEIGHTED_PRED));
	rkvdec_set_bw_field(hw_ps->info, WEIGHTED_BIPRED_FLAG,
			    !!(pps->flags & V4L2_HEVC_PPS_FLAG_WEIGHTED_BIPRED));
	rkvdec_set_bw_field(hw_ps->info, TRANSQUANT_BYPASS_ENABLED_FLAG,
			    !!(pps->flags & V4L2_HEVC_PPS_FLAG_TRANSQUANT_BYPASS_ENABLED));
	tiles_enabled = !!(pps->flags & V4L2_HEVC_PPS_FLAG_TILES_ENABLED);
	rkvdec_set_bw_field(hw_ps->info, TILES_ENABLED_FLAG, tiles_enabled);
	rkvdec_set_bw_field(hw_ps->info, ENTROPY_CODING_SYNC_ENABLED_FLAG,
			    !!(pps->flags & V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED));
	rkvdec_set_bw_field(hw_ps->info, PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG,
			    !!(pps->flags &
			       V4L2_HEVC_PPS_FLAG_PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED));
	rkvdec_set_bw_field(hw_ps->info, LOOP_FILTER_ACROSS_TILES_ENABLED_FLAG,
			    !!(pps->flags & V4L2_HEVC_PPS_FLAG_LOOP_FILTER_ACROSS_TILES_ENABLED));
	rkvdec_set_bw_field(hw_ps->info, DEBLOCKING_FILTER_OVERRIDE_ENABLED_FLAG,
			    !!(pps->flags &
			       V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_OVERRIDE_ENABLED));
	rkvdec_set_bw_field(hw_ps->info, PPS_DEBLOCKING_FILTER_DISABLED_FLAG,
			    !!(pps->flags & V4L2_HEVC_PPS_FLAG_PPS_DISABLE_DEBLOCKING_FILTER));
	rkvdec_set_bw_field(hw_ps->info, PPS_BETA_OFFSET_DIV2, pps->pps_beta_offset_div2);
	rkvdec_set_bw_field(hw_ps->info, PPS_TC_OFFSET_DIV2, pps->pps_tc_offset_div2);
	rkvdec_set_bw_field(hw_ps->info, LISTS_MODIFICATION_PRESENT_FLAG,
			    !!(pps->flags & V4L2_HEVC_PPS_FLAG_LISTS_MODIFICATION_PRESENT));
	rkvdec_set_bw_field(hw_ps->info, LOG2_PARALLEL_MERGE_LEVEL,
			    pps->log2_parallel_merge_level_minus2 + 2);
	rkvdec_set_bw_field(hw_ps->info, SLICE_SEGMENT_HEADER_EXTENSION_PRESENT_FLAG,
			    !!(pps->flags &
			       V4L2_HEVC_PPS_FLAG_SLICE_SEGMENT_HEADER_EXTENSION_PRESENT));
	rkvdec_set_bw_field(hw_ps->info, NUM_TILE_COLUMNS,
			    tiles_enabled ? pps->num_tile_columns_minus1 + 1 : 1);
	rkvdec_set_bw_field(hw_ps->info, NUM_TILE_ROWS,
			    tiles_enabled ? pps->num_tile_rows_minus1 + 1 : 1);
	rkvdec_set_bw_field(hw_ps->info, MVC_FF, 0xffff);

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

	for (i = 0; i < 20; i++)
		rkvdec_set_bw_field(hw_ps->info, COLUMN_WIDTH(i), column_width[i]);
	for (i = 0; i < 22; i++)
		rkvdec_set_bw_field(hw_ps->info, ROW_HEIGHT(i), row_height[i]);

	// Setup POC information
	rkvdec_set_bw_field(hw_ps->info, CURRENT_POC, dec_params->pic_order_cnt_val);

	for (i = 0; i < ARRAY_SIZE(dec_params->dpb); i++) {
		rkvdec_set_bw_field(hw_ps->info, REF_IS_VALID(i),
				    !!(dec_params->num_active_dpb_entries > i));
		rkvdec_set_bw_field(hw_ps->info, REF_PIC_POC(i),
				    dec_params->dpb[i].pic_order_cnt_val);
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

	hevc_ctx = kzalloc_obj(*hevc_ctx);
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
