// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip VDPU381 HEVC backend
 *
 * Copyright (C) 2025 Collabora, Ltd.
 *  Detlev Casanova <detlev.casanova@collabora.com>
 */

#include <media/v4l2-mem2mem.h>

#include "rkvdec.h"
#include "rkvdec-cabac.h"
#include "rkvdec-rcb.h"
#include "rkvdec-hevc-common.h"
#include "rkvdec-vdpu381-regs.h"

// SPS
struct rkvdec_hevc_sps {
	u16 video_parameters_set_id			: 4;
	u16 seq_parameters_set_id_sps			: 4;
	u16 chroma_format_idc				: 2;
	u16 width					: 16;
	u16 height					: 16;
	u16 bit_depth_luma				: 4;
	u16 bit_depth_chroma				: 4;
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
	u16 reserved_0					: 7;
	u16 sps_max_dec_pic_buffering_minus1		: 4;
	u16 reserved_0_2				: 3;
	u16 reserved_f					: 8;
} __packed;

//PPS
struct rkvdec_hevc_pps {
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
	u16 zeroes					: 3;
	u16 num_tile_columns				: 5;
	u16 num_tile_rows				: 5;
	u16 sps_pps_mode				: 4;
	u16 reserved_bits				: 14;
	u16 reserved;
} __packed;

struct rkvdec_hevc_tile {
	u16 value0	: 12;
	u16 value1	: 12;
} __packed;

struct rkvdec_sps_pps_packet {
	struct rkvdec_hevc_sps sps;
	struct rkvdec_hevc_pps pps;
	struct rkvdec_hevc_tile column_width[10];
	struct rkvdec_hevc_tile row_height[11];
	u32 zeroes[3];
	u32 zeroes_bits		: 6;
	u32 padding_bits	: 2;
	u32 padding;
} __packed;

struct rkvdec_hevc_priv_tbl {
	struct rkvdec_sps_pps_packet param_set[64];
	struct rkvdec_rps rps;
	struct scaling_factor scaling_list;
	u8 cabac_table[27456];
};

struct rkvdec_hevc_ctx {
	struct rkvdec_aux_buf			priv_tbl;
	struct v4l2_ctrl_hevc_scaling_matrix	scaling_matrix_cache;
	struct v4l2_ctrl_hevc_ext_sps_st_rps	st_cache;
	struct rkvdec_vdpu381_regs_hevc		regs;
};

static void assemble_hw_pps(struct rkvdec_ctx *ctx,
			    struct rkvdec_hevc_run *run)
{
	struct rkvdec_hevc_ctx *hevc_ctx = ctx->priv;
	const struct v4l2_ctrl_hevc_sps *sps = run->sps;
	const struct v4l2_ctrl_hevc_pps *pps = run->pps;
	struct rkvdec_hevc_priv_tbl *priv_tbl = hevc_ctx->priv_tbl.cpu;
	struct rkvdec_sps_pps_packet *hw_ps;
	bool tiles_enabled;
	s32 max_cu_width;
	s32 pic_in_cts_width;
	s32 pic_in_cts_height;
	u16 log2_min_cb_size, width, height;
	u16 column_width[20];
	u16 row_height[22];
	u8 pcm_enabled;
	u32 i;

	/*
	 * HW read the SPS/PPS information from PPS packet index by PPS id.
	 * offset from the base can be calculated by PPS_id * 32 (size per PPS
	 * packet unit). so the driver copy SPS/PPS information to the exact PPS
	 * packet unit for HW accessing.
	 */
	hw_ps = &priv_tbl->param_set[pps->pic_parameter_set_id];
	memset(hw_ps, 0, sizeof(*hw_ps));

	/* write sps */
	hw_ps->sps.video_parameters_set_id = sps->video_parameter_set_id;
	hw_ps->sps.seq_parameters_set_id_sps = sps->seq_parameter_set_id;
	hw_ps->sps.chroma_format_idc = sps->chroma_format_idc;

	log2_min_cb_size = sps->log2_min_luma_coding_block_size_minus3 + 3;
	width = sps->pic_width_in_luma_samples;
	height = sps->pic_height_in_luma_samples;
	hw_ps->sps.width = width;
	hw_ps->sps.height = height;
	hw_ps->sps.bit_depth_luma = sps->bit_depth_luma_minus8 + 8;
	hw_ps->sps.bit_depth_chroma = sps->bit_depth_chroma_minus8 + 8;
	hw_ps->sps.max_pic_order_count_lsb = sps->log2_max_pic_order_cnt_lsb_minus4 + 4;
	hw_ps->sps.diff_max_min_luma_coding_block_size =
		sps->log2_diff_max_min_luma_coding_block_size;
	hw_ps->sps.min_luma_coding_block_size = sps->log2_min_luma_coding_block_size_minus3 + 3;
	hw_ps->sps.min_transform_block_size = sps->log2_min_luma_transform_block_size_minus2 + 2;
	hw_ps->sps.diff_max_min_transform_block_size =
		sps->log2_diff_max_min_luma_transform_block_size;
	hw_ps->sps.max_transform_hierarchy_depth_inter = sps->max_transform_hierarchy_depth_inter;
	hw_ps->sps.max_transform_hierarchy_depth_intra = sps->max_transform_hierarchy_depth_intra;
	hw_ps->sps.scaling_list_enabled_flag =
		!!(sps->flags & V4L2_HEVC_SPS_FLAG_SCALING_LIST_ENABLED);
	hw_ps->sps.amp_enabled_flag = !!(sps->flags & V4L2_HEVC_SPS_FLAG_AMP_ENABLED);
	hw_ps->sps.sample_adaptive_offset_enabled_flag =
		!!(sps->flags & V4L2_HEVC_SPS_FLAG_SAMPLE_ADAPTIVE_OFFSET);

	pcm_enabled = !!(sps->flags & V4L2_HEVC_SPS_FLAG_PCM_ENABLED);
	hw_ps->sps.pcm_enabled_flag = pcm_enabled;
	hw_ps->sps.pcm_sample_bit_depth_luma =
		pcm_enabled ? sps->pcm_sample_bit_depth_luma_minus1 + 1 : 0;
	hw_ps->sps.pcm_sample_bit_depth_chroma =
		pcm_enabled ? sps->pcm_sample_bit_depth_chroma_minus1 + 1 : 0;
	hw_ps->sps.pcm_loop_filter_disabled_flag =
		!!(sps->flags & V4L2_HEVC_SPS_FLAG_PCM_LOOP_FILTER_DISABLED);
	hw_ps->sps.diff_max_min_pcm_luma_coding_block_size =
		sps->log2_diff_max_min_pcm_luma_coding_block_size;
	hw_ps->sps.min_pcm_luma_coding_block_size =
		pcm_enabled ? sps->log2_min_pcm_luma_coding_block_size_minus3 + 3 : 0;
	hw_ps->sps.num_short_term_ref_pic_sets = sps->num_short_term_ref_pic_sets;
	hw_ps->sps.long_term_ref_pics_present_flag =
		!!(sps->flags & V4L2_HEVC_SPS_FLAG_LONG_TERM_REF_PICS_PRESENT);
	hw_ps->sps.num_long_term_ref_pics_sps = sps->num_long_term_ref_pics_sps;
	hw_ps->sps.sps_temporal_mvp_enabled_flag =
		!!(sps->flags & V4L2_HEVC_SPS_FLAG_SPS_TEMPORAL_MVP_ENABLED);
	hw_ps->sps.strong_intra_smoothing_enabled_flag =
		!!(sps->flags & V4L2_HEVC_SPS_FLAG_STRONG_INTRA_SMOOTHING_ENABLED);
	hw_ps->sps.sps_max_dec_pic_buffering_minus1 = sps->sps_max_dec_pic_buffering_minus1;
	hw_ps->sps.reserved_f = 0xff;

	/* write pps */
	hw_ps->pps.picture_parameters_set_id = pps->pic_parameter_set_id;
	hw_ps->pps.seq_parameters_set_id_pps = sps->seq_parameter_set_id;
	hw_ps->pps.dependent_slice_segments_enabled_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_DEPENDENT_SLICE_SEGMENT_ENABLED);
	hw_ps->pps.output_flag_present_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_OUTPUT_FLAG_PRESENT);
	hw_ps->pps.num_extra_slice_header_bits = pps->num_extra_slice_header_bits;
	hw_ps->pps.sign_data_hiding_enabled_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_SIGN_DATA_HIDING_ENABLED);
	hw_ps->pps.cabac_init_present_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_CABAC_INIT_PRESENT);
	hw_ps->pps.num_ref_idx_l0_default_active = pps->num_ref_idx_l0_default_active_minus1 + 1;
	hw_ps->pps.num_ref_idx_l1_default_active = pps->num_ref_idx_l1_default_active_minus1 + 1;
	hw_ps->pps.init_qp_minus26 = pps->init_qp_minus26;
	hw_ps->pps.constrained_intra_pred_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_CONSTRAINED_INTRA_PRED);
	hw_ps->pps.transform_skip_enabled_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_TRANSFORM_SKIP_ENABLED);
	hw_ps->pps.cu_qp_delta_enabled_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_CU_QP_DELTA_ENABLED);
	hw_ps->pps.log2_min_cb_size = log2_min_cb_size +
				      sps->log2_diff_max_min_luma_coding_block_size -
				      pps->diff_cu_qp_delta_depth;
	hw_ps->pps.pps_cb_qp_offset = pps->pps_cb_qp_offset;
	hw_ps->pps.pps_cr_qp_offset = pps->pps_cr_qp_offset;
	hw_ps->pps.pps_slice_chroma_qp_offsets_present_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT);
	hw_ps->pps.weighted_pred_flag = !!(pps->flags & V4L2_HEVC_PPS_FLAG_WEIGHTED_PRED);
	hw_ps->pps.weighted_bipred_flag = !!(pps->flags & V4L2_HEVC_PPS_FLAG_WEIGHTED_BIPRED);
	hw_ps->pps.transquant_bypass_enabled_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_TRANSQUANT_BYPASS_ENABLED);

	tiles_enabled = !!(pps->flags & V4L2_HEVC_PPS_FLAG_TILES_ENABLED);
	hw_ps->pps.tiles_enabled_flag = tiles_enabled;
	hw_ps->pps.entropy_coding_sync_enabled_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED);
	hw_ps->pps.pps_loop_filter_across_slices_enabled_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED);
	hw_ps->pps.loop_filter_across_tiles_enabled_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_LOOP_FILTER_ACROSS_TILES_ENABLED);
	hw_ps->pps.deblocking_filter_override_enabled_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_OVERRIDE_ENABLED);
	hw_ps->pps.pps_deblocking_filter_disabled_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_PPS_DISABLE_DEBLOCKING_FILTER);
	hw_ps->pps.pps_beta_offset_div2 = pps->pps_beta_offset_div2;
	hw_ps->pps.pps_tc_offset_div2 = pps->pps_tc_offset_div2;
	hw_ps->pps.lists_modification_present_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_LISTS_MODIFICATION_PRESENT);
	hw_ps->pps.log2_parallel_merge_level = pps->log2_parallel_merge_level_minus2 + 2;
	hw_ps->pps.slice_segment_header_extension_present_flag =
		!!(pps->flags & V4L2_HEVC_PPS_FLAG_SLICE_SEGMENT_HEADER_EXTENSION_PRESENT);
	hw_ps->pps.num_tile_columns = tiles_enabled ? pps->num_tile_columns_minus1 + 1 : 0;
	hw_ps->pps.num_tile_rows = tiles_enabled ? pps->num_tile_rows_minus1 + 1 : 0;
	hw_ps->pps.sps_pps_mode = 0;
	hw_ps->pps.reserved_bits = 0x3fff;
	hw_ps->pps.reserved = 0xffff;

	// Setup tiles information
	memset(column_width, 0, sizeof(column_width));
	memset(row_height, 0, sizeof(row_height));

	max_cu_width = 1 << (sps->log2_diff_max_min_luma_coding_block_size + log2_min_cb_size);
	pic_in_cts_width = (width + max_cu_width - 1) / max_cu_width;
	pic_in_cts_height = (height + max_cu_width - 1) / max_cu_width;

	if (pps->flags & V4L2_HEVC_PPS_FLAG_TILES_ENABLED) {
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

	for (i = 0; i < 20; i++) {
		if (column_width[i] > 0)
			column_width[i]--;

		if (i & 1)
			hw_ps->column_width[i / 2].value1 = column_width[i];
		else
			hw_ps->column_width[i / 2].value0 = column_width[i];
	}

	for (i = 0; i < 22; i++) {
		if (row_height[i] > 0)
			row_height[i]--;

		if (i & 1)
			hw_ps->row_height[i / 2].value1 = row_height[i];
		else
			hw_ps->row_height[i / 2].value0 = row_height[i];
	}

	hw_ps->padding = 0xffffffff;
	hw_ps->padding_bits = 0x3;
}

static void set_ref_valid(struct rkvdec_vdpu381_regs_hevc *regs, int id, u32 valid)
{
	switch (id) {
	case 0:
		regs->hevc_param.reg099_hevc_ref_valid.hevc_ref_valid_0 = valid;
		break;
	case 1:
		regs->hevc_param.reg099_hevc_ref_valid.hevc_ref_valid_1 = valid;
		break;
	case 2:
		regs->hevc_param.reg099_hevc_ref_valid.hevc_ref_valid_2 = valid;
		break;
	case 3:
		regs->hevc_param.reg099_hevc_ref_valid.hevc_ref_valid_3 = valid;
		break;
	case 4:
		regs->hevc_param.reg099_hevc_ref_valid.hevc_ref_valid_4 = valid;
		break;
	case 5:
		regs->hevc_param.reg099_hevc_ref_valid.hevc_ref_valid_5 = valid;
		break;
	case 6:
		regs->hevc_param.reg099_hevc_ref_valid.hevc_ref_valid_6 = valid;
		break;
	case 7:
		regs->hevc_param.reg099_hevc_ref_valid.hevc_ref_valid_7 = valid;
		break;
	case 8:
		regs->hevc_param.reg099_hevc_ref_valid.hevc_ref_valid_8 = valid;
		break;
	case 9:
		regs->hevc_param.reg099_hevc_ref_valid.hevc_ref_valid_9 = valid;
		break;
	case 10:
		regs->hevc_param.reg099_hevc_ref_valid.hevc_ref_valid_10 = valid;
		break;
	case 11:
		regs->hevc_param.reg099_hevc_ref_valid.hevc_ref_valid_11 = valid;
		break;
	case 12:
		regs->hevc_param.reg099_hevc_ref_valid.hevc_ref_valid_12 = valid;
		break;
	case 13:
		regs->hevc_param.reg099_hevc_ref_valid.hevc_ref_valid_13 = valid;
		break;
	case 14:
		regs->hevc_param.reg099_hevc_ref_valid.hevc_ref_valid_14 = valid;
		break;
	}
}

static void rkvdec_write_regs(struct rkvdec_ctx *ctx)
{
	struct rkvdec_dev *rkvdec = ctx->dev;
	struct rkvdec_hevc_ctx *hevc_ctx = ctx->priv;

	rkvdec_memcpy_toio(rkvdec->regs + OFFSET_COMMON_REGS,
			   &hevc_ctx->regs.common,
			   sizeof(hevc_ctx->regs.common));
	rkvdec_memcpy_toio(rkvdec->regs + OFFSET_CODEC_PARAMS_REGS,
			   &hevc_ctx->regs.hevc_param,
			   sizeof(hevc_ctx->regs.hevc_param));
	rkvdec_memcpy_toio(rkvdec->regs + OFFSET_COMMON_ADDR_REGS,
			   &hevc_ctx->regs.common_addr,
			   sizeof(hevc_ctx->regs.common_addr));
	rkvdec_memcpy_toio(rkvdec->regs + OFFSET_CODEC_ADDR_REGS,
			   &hevc_ctx->regs.hevc_addr,
			   sizeof(hevc_ctx->regs.hevc_addr));
	rkvdec_memcpy_toio(rkvdec->regs + OFFSET_POC_HIGHBIT_REGS,
			   &hevc_ctx->regs.hevc_highpoc,
			   sizeof(hevc_ctx->regs.hevc_highpoc));
}

static void config_registers(struct rkvdec_ctx *ctx,
			     struct rkvdec_hevc_run *run)
{
	const struct v4l2_ctrl_hevc_decode_params *dec_params = run->decode_params;
	const struct v4l2_hevc_dpb_entry *dpb = dec_params->dpb;
	struct rkvdec_hevc_ctx *hevc_ctx = ctx->priv;
	struct rkvdec_vdpu381_regs_hevc *regs = &hevc_ctx->regs;
	dma_addr_t priv_start_addr = hevc_ctx->priv_tbl.dma;
	const struct v4l2_pix_format_mplane *dst_fmt;
	struct vb2_v4l2_buffer *src_buf = run->base.bufs.src;
	struct vb2_v4l2_buffer *dst_buf = run->base.bufs.dst;
	const struct v4l2_format *f;
	dma_addr_t rlc_addr;
	u32 hor_virstride = 0;
	u32 ver_virstride = 0;
	u32 y_virstride = 0;
	u32 offset;
	u32 pixels;
	dma_addr_t dst_addr;
	u32 i;

	memset(regs, 0, sizeof(*regs));

	/* Set HEVC mode */
	regs->common.reg009_dec_mode.dec_mode = VDPU381_MODE_HEVC;

	/* Set config */
	regs->common.reg011_important_en.buf_empty_en = 1;
	regs->common.reg011_important_en.dec_clkgate_e = 1;
	regs->common.reg011_important_en.dec_timeout_e = 1;
	regs->common.reg011_important_en.pix_range_det_e = 1;

	/* Set IDR flag */
	regs->common.reg013_en_mode_set.cur_pic_is_idr =
		!!(dec_params->flags & V4L2_HEVC_DECODE_PARAM_FLAG_IDR_PIC);

	/* Set input stream length */
	regs->common.reg016_stream_len = vb2_get_plane_payload(&src_buf->vb2_buf, 0);

	/* Set max slice number */
	regs->common.reg017_slice_number.slice_num = 1;

	/* Set strides */
	f = &ctx->decoded_fmt;
	dst_fmt = &f->fmt.pix_mp;
	hor_virstride = dst_fmt->plane_fmt[0].bytesperline;
	ver_virstride = dst_fmt->height;
	y_virstride = hor_virstride * ver_virstride;

	regs->common.reg018_y_hor_stride.y_hor_virstride = hor_virstride / 16;
	regs->common.reg019_uv_hor_stride.uv_hor_virstride = hor_virstride / 16;
	regs->common.reg020_y_stride.y_virstride = y_virstride / 16;

	/* Activate block gating */
	regs->common.reg026_block_gating_en.inter_auto_gating_e = 1;
	regs->common.reg026_block_gating_en.filterd_auto_gating_e = 1;
	regs->common.reg026_block_gating_en.strmd_auto_gating_e = 1;
	regs->common.reg026_block_gating_en.mcp_auto_gating_e = 1;
	regs->common.reg026_block_gating_en.busifd_auto_gating_e = 0;
	regs->common.reg026_block_gating_en.dec_ctrl_auto_gating_e = 1;
	regs->common.reg026_block_gating_en.intra_auto_gating_e = 1;
	regs->common.reg026_block_gating_en.mc_auto_gating_e = 1;
	regs->common.reg026_block_gating_en.transd_auto_gating_e = 1;
	regs->common.reg026_block_gating_en.sram_auto_gating_e = 1;
	regs->common.reg026_block_gating_en.cru_auto_gating_e = 1;
	regs->common.reg026_block_gating_en.reg_cfg_gating_en = 1;

	/* Set timeout threshold */
	pixels = dst_fmt->height * dst_fmt->width;
	if (pixels < RKVDEC_1080P_PIXELS)
		regs->common.reg032_timeout_threshold = RKVDEC_TIMEOUT_1080p;
	else if (pixels < RKVDEC_4K_PIXELS)
		regs->common.reg032_timeout_threshold = RKVDEC_TIMEOUT_4K;
	else if (pixels < RKVDEC_8K_PIXELS)
		regs->common.reg032_timeout_threshold = RKVDEC_TIMEOUT_8K;
	else
		regs->common.reg032_timeout_threshold = RKVDEC_TIMEOUT_MAX;

	/* Set POC val */
	regs->hevc_param.reg065_cur_top_poc = dec_params->pic_order_cnt_val;

	/* Set ref pic address & poc */
	for (i = 0; i < ARRAY_SIZE(dec_params->dpb); i++) {
		struct vb2_buffer *vb_buf = get_ref_buf(ctx, run, i);
		dma_addr_t buf_dma = vb2_dma_contig_plane_dma_addr(vb_buf, 0);
		u32 valid = !!(dec_params->num_active_dpb_entries > i);

		/* Set reference addresses */
		regs->hevc_addr.reg164_180_ref_base[i] = buf_dma;

		/* Set COLMV addresses */
		regs->hevc_addr.reg182_198_colmv_base[i] = buf_dma + ctx->colmv_offset;

		regs->hevc_param.reg067_082_ref_poc[i] =
			dpb[i].pic_order_cnt_val;

		set_ref_valid(regs, i, valid);
		regs->hevc_param.reg103_hevc_mvc0.ref_pic_layer_same_with_cur |= 1 << i;
	}

	/* Set rlc base address (input stream) */
	rlc_addr = vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);
	regs->common_addr.rlc_base = rlc_addr;
	regs->common_addr.rlcwrite_base = rlc_addr;

	/* Set output base address */
	dst_addr = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);
	regs->common_addr.decout_base = dst_addr;
	regs->common_addr.error_ref_base = dst_addr;

	/* Set colmv address */
	regs->common_addr.colmv_cur_base = dst_addr + ctx->colmv_offset;

	/* Set RCB addresses */
	for (i = 0; i < rkvdec_rcb_buf_count(ctx); i++)
		regs->common_addr.rcb_base[i] = rkvdec_rcb_buf_dma_addr(ctx, i);

	/* Set hw pps address */
	offset = offsetof(struct rkvdec_hevc_priv_tbl, param_set);
	regs->hevc_addr.reg161_pps_base = priv_start_addr + offset;

	/* Set hw rps address */
	offset = offsetof(struct rkvdec_hevc_priv_tbl, rps);
	regs->hevc_addr.reg163_rps_base = priv_start_addr + offset;

	/* Set cabac table */
	offset = offsetof(struct rkvdec_hevc_priv_tbl, cabac_table);
	regs->hevc_addr.reg199_cabactbl_base = priv_start_addr + offset;

	/* Set scaling matrix */
	offset = offsetof(struct rkvdec_hevc_priv_tbl, scaling_list);
	regs->hevc_addr.reg181_scanlist_addr = priv_start_addr + offset;

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

	rkvdec_hevc_run_preamble(ctx, &run);

	rkvdec_hevc_assemble_hw_scaling_list(ctx, &run, &tbl->scaling_list,
					     &hevc_ctx->scaling_matrix_cache);
	assemble_hw_pps(ctx, &run);

	/*
	 * On vdpu381, not setting the long and short term ref sets will just output wrong frames.
	 * Let's just warn about it and let the decoder run anyway.
	 */
	if ((!ctx->has_sps_lt_rps && run.sps->num_long_term_ref_pics_sps) ||
	    (!ctx->has_sps_st_rps && run.sps->num_short_term_ref_pic_sets)) {
		dev_warn_ratelimited(rkvdec->dev, "Long and short term RPS not set\n");
	} else {
		rkvdec_hevc_assemble_hw_rps(&run, &tbl->rps, &hevc_ctx->st_cache);
	}

	config_registers(ctx, &run);

	rkvdec_run_postamble(ctx, &run.base);

	rkvdec_schedule_watchdog(rkvdec, hevc_ctx->regs.common.reg032_timeout_threshold);

	/* Start decoding! */
	writel(VDPU381_DEC_E_BIT, rkvdec->regs + VDPU381_REG_DEC_E);

	return 0;
}

static int rkvdec_hevc_try_ctrl(struct rkvdec_ctx *ctx, struct v4l2_ctrl *ctrl)
{
	if (ctrl->id == V4L2_CID_STATELESS_HEVC_SPS)
		return rkvdec_hevc_validate_sps(ctx, ctrl->p_new.p_hevc_sps);

	return 0;
}

const struct rkvdec_coded_fmt_ops rkvdec_vdpu381_hevc_fmt_ops = {
	.adjust_fmt = rkvdec_hevc_adjust_fmt,
	.start = rkvdec_hevc_start,
	.stop = rkvdec_hevc_stop,
	.run = rkvdec_hevc_run,
	.try_ctrl = rkvdec_hevc_try_ctrl,
	.get_image_fmt = rkvdec_hevc_get_image_fmt,
};
