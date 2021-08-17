// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019-2020 Pengutronix, Michael Tretter <kernel@pengutronix.de>
 *
 * Convert NAL units between raw byte sequence payloads (RBSP) and C structs.
 *
 * The conversion is defined in "ITU-T Rec. H.265 (02/2018) high efficiency
 * video coding". Decoder drivers may use the parser to parse RBSP from
 * encoded streams and configure the hardware, if the hardware is not able to
 * parse RBSP itself. Encoder drivers may use the generator to generate the
 * RBSP for VPS/SPS/PPS nal units and add them to the encoded stream if the
 * hardware does not generate the units.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/v4l2-controls.h>

#include <linux/device.h>
#include <linux/export.h>
#include <linux/log2.h>

#include "nal-hevc.h"
#include "nal-rbsp.h"

/*
 * See Rec. ITU-T H.265 (02/2018) Table 7-1 - NAL unit type codes and NAL unit
 * type classes
 */
enum nal_unit_type {
	VPS_NUT = 32,
	SPS_NUT = 33,
	PPS_NUT = 34,
	FD_NUT = 38,
};

int nal_hevc_profile_from_v4l2(enum v4l2_mpeg_video_hevc_profile profile)
{
	switch (profile) {
	case V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN:
		return 1;
	case V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10:
		return 2;
	case V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_STILL_PICTURE:
		return 3;
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(nal_hevc_profile_from_v4l2);

int nal_hevc_tier_from_v4l2(enum v4l2_mpeg_video_hevc_tier tier)
{
	switch (tier) {
	case V4L2_MPEG_VIDEO_HEVC_TIER_MAIN:
		return 0;
	case V4L2_MPEG_VIDEO_HEVC_TIER_HIGH:
		return 1;
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(nal_hevc_tier_from_v4l2);

int nal_hevc_level_from_v4l2(enum v4l2_mpeg_video_hevc_level level)
{
	/*
	 * T-Rec-H.265 p. 280: general_level_idc and sub_layer_level_idc[ i ]
	 * shall be set equal to a value of 30 times the level number
	 * specified in Table A.6.
	 */
	int factor = 30 / 10;

	switch (level) {
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_1:
		return factor * 10;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_2:
		return factor * 20;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_2_1:
		return factor * 21;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_3:
		return factor * 30;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_3_1:
		return factor * 31;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_4:
		return factor * 40;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1:
		return factor * 41;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_5:
		return factor * 50;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1:
		return factor * 51;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_5_2:
		return factor * 52;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_6:
		return factor * 60;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_6_1:
		return factor * 61;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_6_2:
		return factor * 62;
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(nal_hevc_level_from_v4l2);

static void nal_hevc_write_start_code_prefix(struct rbsp *rbsp)
{
	u8 *p = rbsp->data + DIV_ROUND_UP(rbsp->pos, 8);
	int i = 4;

	if (DIV_ROUND_UP(rbsp->pos, 8) + i > rbsp->size) {
		rbsp->error = -EINVAL;
		return;
	}

	p[0] = 0x00;
	p[1] = 0x00;
	p[2] = 0x00;
	p[3] = 0x01;

	rbsp->pos += i * 8;
}

static void nal_hevc_read_start_code_prefix(struct rbsp *rbsp)
{
	u8 *p = rbsp->data + DIV_ROUND_UP(rbsp->pos, 8);
	int i = 4;

	if (DIV_ROUND_UP(rbsp->pos, 8) + i > rbsp->size) {
		rbsp->error = -EINVAL;
		return;
	}

	if (p[0] != 0x00 || p[1] != 0x00 || p[2] != 0x00 || p[3] != 0x01) {
		rbsp->error = -EINVAL;
		return;
	}

	rbsp->pos += i * 8;
}

static void nal_hevc_write_filler_data(struct rbsp *rbsp)
{
	u8 *p = rbsp->data + DIV_ROUND_UP(rbsp->pos, 8);
	int i;

	/* Keep 1 byte extra for terminating the NAL unit */
	i = rbsp->size - DIV_ROUND_UP(rbsp->pos, 8) - 1;
	memset(p, 0xff, i);
	rbsp->pos += i * 8;
}

static void nal_hevc_read_filler_data(struct rbsp *rbsp)
{
	u8 *p = rbsp->data + DIV_ROUND_UP(rbsp->pos, 8);

	while (*p == 0xff) {
		if (DIV_ROUND_UP(rbsp->pos, 8) > rbsp->size) {
			rbsp->error = -EINVAL;
			return;
		}

		p++;
		rbsp->pos += 8;
	}
}

static void nal_hevc_rbsp_profile_tier_level(struct rbsp *rbsp,
					     struct nal_hevc_profile_tier_level *ptl)
{
	unsigned int i;
	unsigned int max_num_sub_layers_minus_1 = 0;

	rbsp_bits(rbsp, 2, &ptl->general_profile_space);
	rbsp_bit(rbsp, &ptl->general_tier_flag);
	rbsp_bits(rbsp, 5, &ptl->general_profile_idc);
	for (i = 0; i < 32; i++)
		rbsp_bit(rbsp, &ptl->general_profile_compatibility_flag[i]);
	rbsp_bit(rbsp, &ptl->general_progressive_source_flag);
	rbsp_bit(rbsp, &ptl->general_interlaced_source_flag);
	rbsp_bit(rbsp, &ptl->general_non_packed_constraint_flag);
	rbsp_bit(rbsp, &ptl->general_frame_only_constraint_flag);
	if (ptl->general_profile_idc == 4 ||
	    ptl->general_profile_compatibility_flag[4] ||
	    ptl->general_profile_idc == 5 ||
	    ptl->general_profile_compatibility_flag[5] ||
	    ptl->general_profile_idc == 6 ||
	    ptl->general_profile_compatibility_flag[6] ||
	    ptl->general_profile_idc == 7 ||
	    ptl->general_profile_compatibility_flag[7] ||
	    ptl->general_profile_idc == 8 ||
	    ptl->general_profile_compatibility_flag[8] ||
	    ptl->general_profile_idc == 9 ||
	    ptl->general_profile_compatibility_flag[9] ||
	    ptl->general_profile_idc == 10 ||
	    ptl->general_profile_compatibility_flag[10]) {
		rbsp_bit(rbsp, &ptl->general_max_12bit_constraint_flag);
		rbsp_bit(rbsp, &ptl->general_max_10bit_constraint_flag);
		rbsp_bit(rbsp, &ptl->general_max_8bit_constraint_flag);
		rbsp_bit(rbsp, &ptl->general_max_422chroma_constraint_flag);
		rbsp_bit(rbsp, &ptl->general_max_420chroma_constraint_flag);
		rbsp_bit(rbsp, &ptl->general_max_monochrome_constraint_flag);
		rbsp_bit(rbsp, &ptl->general_intra_constraint_flag);
		rbsp_bit(rbsp, &ptl->general_one_picture_only_constraint_flag);
		rbsp_bit(rbsp, &ptl->general_lower_bit_rate_constraint_flag);
		if (ptl->general_profile_idc == 5 ||
		    ptl->general_profile_compatibility_flag[5] ||
		    ptl->general_profile_idc == 9 ||
		    ptl->general_profile_compatibility_flag[9] ||
		    ptl->general_profile_idc == 10 ||
		    ptl->general_profile_compatibility_flag[10]) {
			rbsp_bit(rbsp, &ptl->general_max_14bit_constraint_flag);
			rbsp_bits(rbsp, 32, &ptl->general_reserved_zero_33bits);
			rbsp_bits(rbsp, 33 - 32, &ptl->general_reserved_zero_33bits);
		} else {
			rbsp_bits(rbsp, 32, &ptl->general_reserved_zero_34bits);
			rbsp_bits(rbsp, 34 - 2, &ptl->general_reserved_zero_34bits);
		}
	} else if (ptl->general_profile_idc == 2 ||
		   ptl->general_profile_compatibility_flag[2]) {
		rbsp_bits(rbsp, 7, &ptl->general_reserved_zero_7bits);
		rbsp_bit(rbsp, &ptl->general_one_picture_only_constraint_flag);
		rbsp_bits(rbsp, 32, &ptl->general_reserved_zero_35bits);
		rbsp_bits(rbsp, 35 - 32, &ptl->general_reserved_zero_35bits);
	} else {
		rbsp_bits(rbsp, 32, &ptl->general_reserved_zero_43bits);
		rbsp_bits(rbsp, 43 - 32, &ptl->general_reserved_zero_43bits);
	}
	if ((ptl->general_profile_idc >= 1 && ptl->general_profile_idc <= 5) ||
	    ptl->general_profile_idc == 9 ||
	    ptl->general_profile_compatibility_flag[1] ||
	    ptl->general_profile_compatibility_flag[2] ||
	    ptl->general_profile_compatibility_flag[3] ||
	    ptl->general_profile_compatibility_flag[4] ||
	    ptl->general_profile_compatibility_flag[5] ||
	    ptl->general_profile_compatibility_flag[9])
		rbsp_bit(rbsp, &ptl->general_inbld_flag);
	else
		rbsp_bit(rbsp, &ptl->general_reserved_zero_bit);
	rbsp_bits(rbsp, 8, &ptl->general_level_idc);
	if (max_num_sub_layers_minus_1 > 0)
		rbsp_unsupported(rbsp);
}

static void nal_hevc_rbsp_vps(struct rbsp *rbsp, struct nal_hevc_vps *vps)
{
	unsigned int i, j;
	unsigned int reserved_0xffff_16bits = 0xffff;

	rbsp_bits(rbsp, 4, &vps->video_parameter_set_id);
	rbsp_bit(rbsp, &vps->base_layer_internal_flag);
	rbsp_bit(rbsp, &vps->base_layer_available_flag);
	rbsp_bits(rbsp, 6, &vps->max_layers_minus1);
	rbsp_bits(rbsp, 3, &vps->max_sub_layers_minus1);
	rbsp_bits(rbsp, 1, &vps->temporal_id_nesting_flag);
	rbsp_bits(rbsp, 16, &reserved_0xffff_16bits);
	nal_hevc_rbsp_profile_tier_level(rbsp, &vps->profile_tier_level);
	rbsp_bit(rbsp, &vps->sub_layer_ordering_info_present_flag);
	for (i = vps->sub_layer_ordering_info_present_flag ? 0 : vps->max_sub_layers_minus1;
	     i <= vps->max_sub_layers_minus1; i++) {
		rbsp_uev(rbsp, &vps->max_dec_pic_buffering_minus1[i]);
		rbsp_uev(rbsp, &vps->max_num_reorder_pics[i]);
		rbsp_uev(rbsp, &vps->max_latency_increase_plus1[i]);
	}
	rbsp_bits(rbsp, 6, &vps->max_layer_id);
	rbsp_uev(rbsp, &vps->num_layer_sets_minus1);
	for (i = 0; i <= vps->num_layer_sets_minus1; i++)
		for (j = 0; j <= vps->max_layer_id; j++)
			rbsp_bit(rbsp, &vps->layer_id_included_flag[i][j]);
	rbsp_bit(rbsp, &vps->timing_info_present_flag);
	if (vps->timing_info_present_flag)
		rbsp_unsupported(rbsp);
	rbsp_bit(rbsp, &vps->extension_flag);
	if (vps->extension_flag)
		rbsp_unsupported(rbsp);
}

static void nal_hevc_rbsp_sps(struct rbsp *rbsp, struct nal_hevc_sps *sps)
{
	unsigned int i;

	rbsp_bits(rbsp, 4, &sps->video_parameter_set_id);
	rbsp_bits(rbsp, 3, &sps->max_sub_layers_minus1);
	rbsp_bit(rbsp, &sps->temporal_id_nesting_flag);
	nal_hevc_rbsp_profile_tier_level(rbsp, &sps->profile_tier_level);
	rbsp_uev(rbsp, &sps->seq_parameter_set_id);

	rbsp_uev(rbsp, &sps->chroma_format_idc);
	if (sps->chroma_format_idc == 3)
		rbsp_bit(rbsp, &sps->separate_colour_plane_flag);
	rbsp_uev(rbsp, &sps->pic_width_in_luma_samples);
	rbsp_uev(rbsp, &sps->pic_height_in_luma_samples);
	rbsp_bit(rbsp, &sps->conformance_window_flag);
	if (sps->conformance_window_flag) {
		rbsp_uev(rbsp, &sps->conf_win_left_offset);
		rbsp_uev(rbsp, &sps->conf_win_right_offset);
		rbsp_uev(rbsp, &sps->conf_win_top_offset);
		rbsp_uev(rbsp, &sps->conf_win_bottom_offset);
	}
	rbsp_uev(rbsp, &sps->bit_depth_luma_minus8);
	rbsp_uev(rbsp, &sps->bit_depth_chroma_minus8);

	rbsp_uev(rbsp, &sps->log2_max_pic_order_cnt_lsb_minus4);

	rbsp_bit(rbsp, &sps->sub_layer_ordering_info_present_flag);
	for (i = (sps->sub_layer_ordering_info_present_flag ? 0 : sps->max_sub_layers_minus1);
	     i <= sps->max_sub_layers_minus1; i++) {
		rbsp_uev(rbsp, &sps->max_dec_pic_buffering_minus1[i]);
		rbsp_uev(rbsp, &sps->max_num_reorder_pics[i]);
		rbsp_uev(rbsp, &sps->max_latency_increase_plus1[i]);
	}
	rbsp_uev(rbsp, &sps->log2_min_luma_coding_block_size_minus3);
	rbsp_uev(rbsp, &sps->log2_diff_max_min_luma_coding_block_size);
	rbsp_uev(rbsp, &sps->log2_min_luma_transform_block_size_minus2);
	rbsp_uev(rbsp, &sps->log2_diff_max_min_luma_transform_block_size);
	rbsp_uev(rbsp, &sps->max_transform_hierarchy_depth_inter);
	rbsp_uev(rbsp, &sps->max_transform_hierarchy_depth_intra);

	rbsp_bit(rbsp, &sps->scaling_list_enabled_flag);
	if (sps->scaling_list_enabled_flag)
		rbsp_unsupported(rbsp);

	rbsp_bit(rbsp, &sps->amp_enabled_flag);
	rbsp_bit(rbsp, &sps->sample_adaptive_offset_enabled_flag);
	rbsp_bit(rbsp, &sps->pcm_enabled_flag);
	if (sps->pcm_enabled_flag) {
		rbsp_bits(rbsp, 4, &sps->pcm_sample_bit_depth_luma_minus1);
		rbsp_bits(rbsp, 4, &sps->pcm_sample_bit_depth_chroma_minus1);
		rbsp_uev(rbsp, &sps->log2_min_pcm_luma_coding_block_size_minus3);
		rbsp_uev(rbsp, &sps->log2_diff_max_min_pcm_luma_coding_block_size);
		rbsp_bit(rbsp, &sps->pcm_loop_filter_disabled_flag);
	}

	rbsp_uev(rbsp, &sps->num_short_term_ref_pic_sets);
	if (sps->num_short_term_ref_pic_sets > 0)
		rbsp_unsupported(rbsp);

	rbsp_bit(rbsp, &sps->long_term_ref_pics_present_flag);
	if (sps->long_term_ref_pics_present_flag)
		rbsp_unsupported(rbsp);

	rbsp_bit(rbsp, &sps->sps_temporal_mvp_enabled_flag);
	rbsp_bit(rbsp, &sps->strong_intra_smoothing_enabled_flag);
	rbsp_bit(rbsp, &sps->vui_parameters_present_flag);
	if (sps->vui_parameters_present_flag)
		rbsp_unsupported(rbsp);

	rbsp_bit(rbsp, &sps->extension_present_flag);
	if (sps->extension_present_flag) {
		rbsp_bit(rbsp, &sps->sps_range_extension_flag);
		rbsp_bit(rbsp, &sps->sps_multilayer_extension_flag);
		rbsp_bit(rbsp, &sps->sps_3d_extension_flag);
		rbsp_bit(rbsp, &sps->sps_scc_extension_flag);
		rbsp_bits(rbsp, 5, &sps->sps_extension_4bits);
	}
	if (sps->sps_range_extension_flag)
		rbsp_unsupported(rbsp);
	if (sps->sps_multilayer_extension_flag)
		rbsp_unsupported(rbsp);
	if (sps->sps_3d_extension_flag)
		rbsp_unsupported(rbsp);
	if (sps->sps_scc_extension_flag)
		rbsp_unsupported(rbsp);
	if (sps->sps_extension_4bits)
		rbsp_unsupported(rbsp);
}

static void nal_hevc_rbsp_pps(struct rbsp *rbsp, struct nal_hevc_pps *pps)
{
	unsigned int i;

	rbsp_uev(rbsp, &pps->pps_pic_parameter_set_id);
	rbsp_uev(rbsp, &pps->pps_seq_parameter_set_id);
	rbsp_bit(rbsp, &pps->dependent_slice_segments_enabled_flag);
	rbsp_bit(rbsp, &pps->output_flag_present_flag);
	rbsp_bits(rbsp, 3, &pps->num_extra_slice_header_bits);
	rbsp_bit(rbsp, &pps->sign_data_hiding_enabled_flag);
	rbsp_bit(rbsp, &pps->cabac_init_present_flag);
	rbsp_uev(rbsp, &pps->num_ref_idx_l0_default_active_minus1);
	rbsp_uev(rbsp, &pps->num_ref_idx_l1_default_active_minus1);
	rbsp_sev(rbsp, &pps->init_qp_minus26);
	rbsp_bit(rbsp, &pps->constrained_intra_pred_flag);
	rbsp_bit(rbsp, &pps->transform_skip_enabled_flag);
	rbsp_bit(rbsp, &pps->cu_qp_delta_enabled_flag);
	if (pps->cu_qp_delta_enabled_flag)
		rbsp_uev(rbsp, &pps->diff_cu_qp_delta_depth);
	rbsp_sev(rbsp, &pps->pps_cb_qp_offset);
	rbsp_sev(rbsp, &pps->pps_cr_qp_offset);
	rbsp_bit(rbsp, &pps->pps_slice_chroma_qp_offsets_present_flag);
	rbsp_bit(rbsp, &pps->weighted_pred_flag);
	rbsp_bit(rbsp, &pps->weighted_bipred_flag);
	rbsp_bit(rbsp, &pps->transquant_bypass_enabled_flag);
	rbsp_bit(rbsp, &pps->tiles_enabled_flag);
	rbsp_bit(rbsp, &pps->entropy_coding_sync_enabled_flag);
	if (pps->tiles_enabled_flag) {
		rbsp_uev(rbsp, &pps->num_tile_columns_minus1);
		rbsp_uev(rbsp, &pps->num_tile_rows_minus1);
		rbsp_bit(rbsp, &pps->uniform_spacing_flag);
		if (!pps->uniform_spacing_flag) {
			for (i = 0; i < pps->num_tile_columns_minus1; i++)
				rbsp_uev(rbsp, &pps->column_width_minus1[i]);
			for (i = 0; i < pps->num_tile_rows_minus1; i++)
				rbsp_uev(rbsp, &pps->row_height_minus1[i]);
		}
		rbsp_bit(rbsp, &pps->loop_filter_across_tiles_enabled_flag);
	}
	rbsp_bit(rbsp, &pps->pps_loop_filter_across_slices_enabled_flag);
	rbsp_bit(rbsp, &pps->deblocking_filter_control_present_flag);
	if (pps->deblocking_filter_control_present_flag) {
		rbsp_bit(rbsp, &pps->deblocking_filter_override_enabled_flag);
		rbsp_bit(rbsp, &pps->pps_deblocking_filter_disabled_flag);
		if (!pps->pps_deblocking_filter_disabled_flag) {
			rbsp_sev(rbsp, &pps->pps_beta_offset_div2);
			rbsp_sev(rbsp, &pps->pps_tc_offset_div2);
		}
	}
	rbsp_bit(rbsp, &pps->pps_scaling_list_data_present_flag);
	if (pps->pps_scaling_list_data_present_flag)
		rbsp_unsupported(rbsp);
	rbsp_bit(rbsp, &pps->lists_modification_present_flag);
	rbsp_uev(rbsp, &pps->log2_parallel_merge_level_minus2);
	rbsp_bit(rbsp, &pps->slice_segment_header_extension_present_flag);
	rbsp_bit(rbsp, &pps->pps_extension_present_flag);
	if (pps->pps_extension_present_flag) {
		rbsp_bit(rbsp, &pps->pps_range_extension_flag);
		rbsp_bit(rbsp, &pps->pps_multilayer_extension_flag);
		rbsp_bit(rbsp, &pps->pps_3d_extension_flag);
		rbsp_bit(rbsp, &pps->pps_scc_extension_flag);
		rbsp_bits(rbsp, 4, &pps->pps_extension_4bits);
	}
	if (pps->pps_range_extension_flag)
		rbsp_unsupported(rbsp);
	if (pps->pps_multilayer_extension_flag)
		rbsp_unsupported(rbsp);
	if (pps->pps_3d_extension_flag)
		rbsp_unsupported(rbsp);
	if (pps->pps_scc_extension_flag)
		rbsp_unsupported(rbsp);
	if (pps->pps_extension_4bits)
		rbsp_unsupported(rbsp);
}

/**
 * nal_hevc_write_vps() - Write PPS NAL unit into RBSP format
 * @dev: device pointer
 * @dest: the buffer that is filled with RBSP data
 * @n: maximum size of @dest in bytes
 * @vps: &struct nal_hevc_vps to convert to RBSP
 *
 * Convert @vps to RBSP data and write it into @dest.
 *
 * The size of the VPS NAL unit is not known in advance and this function will
 * fail, if @dest does not hold sufficient space for the VPS NAL unit.
 *
 * Return: number of bytes written to @dest or negative error code
 */
ssize_t nal_hevc_write_vps(const struct device *dev,
			   void *dest, size_t n, struct nal_hevc_vps *vps)
{
	struct rbsp rbsp;
	unsigned int forbidden_zero_bit = 0;
	unsigned int nal_unit_type = VPS_NUT;
	unsigned int nuh_layer_id = 0;
	unsigned int nuh_temporal_id_plus1 = 1;

	if (!dest)
		return -EINVAL;

	rbsp_init(&rbsp, dest, n, &write);

	nal_hevc_write_start_code_prefix(&rbsp);

	/* NAL unit header */
	rbsp_bit(&rbsp, &forbidden_zero_bit);
	rbsp_bits(&rbsp, 6, &nal_unit_type);
	rbsp_bits(&rbsp, 6, &nuh_layer_id);
	rbsp_bits(&rbsp, 3, &nuh_temporal_id_plus1);

	nal_hevc_rbsp_vps(&rbsp, vps);

	rbsp_trailing_bits(&rbsp);

	if (rbsp.error)
		return rbsp.error;

	return DIV_ROUND_UP(rbsp.pos, 8);
}
EXPORT_SYMBOL_GPL(nal_hevc_write_vps);

/**
 * nal_hevc_read_vps() - Read VPS NAL unit from RBSP format
 * @dev: device pointer
 * @vps: the &struct nal_hevc_vps to fill from the RBSP data
 * @src: the buffer that contains the RBSP data
 * @n: size of @src in bytes
 *
 * Read RBSP data from @src and use it to fill @vps.
 *
 * Return: number of bytes read from @src or negative error code
 */
ssize_t nal_hevc_read_vps(const struct device *dev,
			  struct nal_hevc_vps *vps, void *src, size_t n)
{
	struct rbsp rbsp;
	unsigned int forbidden_zero_bit;
	unsigned int nal_unit_type;
	unsigned int nuh_layer_id;
	unsigned int nuh_temporal_id_plus1;

	if (!src)
		return -EINVAL;

	rbsp_init(&rbsp, src, n, &read);

	nal_hevc_read_start_code_prefix(&rbsp);

	rbsp_bit(&rbsp, &forbidden_zero_bit);
	rbsp_bits(&rbsp, 6, &nal_unit_type);
	rbsp_bits(&rbsp, 6, &nuh_layer_id);
	rbsp_bits(&rbsp, 3, &nuh_temporal_id_plus1);

	if (rbsp.error ||
	    forbidden_zero_bit != 0 ||
	    nal_unit_type != VPS_NUT)
		return -EINVAL;

	nal_hevc_rbsp_vps(&rbsp, vps);

	rbsp_trailing_bits(&rbsp);

	if (rbsp.error)
		return rbsp.error;

	return DIV_ROUND_UP(rbsp.pos, 8);
}
EXPORT_SYMBOL_GPL(nal_hevc_read_vps);

/**
 * nal_hevc_write_sps() - Write SPS NAL unit into RBSP format
 * @dev: device pointer
 * @dest: the buffer that is filled with RBSP data
 * @n: maximum size of @dest in bytes
 * @sps: &struct nal_hevc_sps to convert to RBSP
 *
 * Convert @sps to RBSP data and write it into @dest.
 *
 * The size of the SPS NAL unit is not known in advance and this function will
 * fail, if @dest does not hold sufficient space for the SPS NAL unit.
 *
 * Return: number of bytes written to @dest or negative error code
 */
ssize_t nal_hevc_write_sps(const struct device *dev,
			   void *dest, size_t n, struct nal_hevc_sps *sps)
{
	struct rbsp rbsp;
	unsigned int forbidden_zero_bit = 0;
	unsigned int nal_unit_type = SPS_NUT;
	unsigned int nuh_layer_id = 0;
	unsigned int nuh_temporal_id_plus1 = 1;

	if (!dest)
		return -EINVAL;

	rbsp_init(&rbsp, dest, n, &write);

	nal_hevc_write_start_code_prefix(&rbsp);

	/* NAL unit header */
	rbsp_bit(&rbsp, &forbidden_zero_bit);
	rbsp_bits(&rbsp, 6, &nal_unit_type);
	rbsp_bits(&rbsp, 6, &nuh_layer_id);
	rbsp_bits(&rbsp, 3, &nuh_temporal_id_plus1);

	nal_hevc_rbsp_sps(&rbsp, sps);

	rbsp_trailing_bits(&rbsp);

	if (rbsp.error)
		return rbsp.error;

	return DIV_ROUND_UP(rbsp.pos, 8);
}
EXPORT_SYMBOL_GPL(nal_hevc_write_sps);

/**
 * nal_hevc_read_sps() - Read SPS NAL unit from RBSP format
 * @dev: device pointer
 * @sps: the &struct nal_hevc_sps to fill from the RBSP data
 * @src: the buffer that contains the RBSP data
 * @n: size of @src in bytes
 *
 * Read RBSP data from @src and use it to fill @sps.
 *
 * Return: number of bytes read from @src or negative error code
 */
ssize_t nal_hevc_read_sps(const struct device *dev,
			  struct nal_hevc_sps *sps, void *src, size_t n)
{
	struct rbsp rbsp;
	unsigned int forbidden_zero_bit;
	unsigned int nal_unit_type;
	unsigned int nuh_layer_id;
	unsigned int nuh_temporal_id_plus1;

	if (!src)
		return -EINVAL;

	rbsp_init(&rbsp, src, n, &read);

	nal_hevc_read_start_code_prefix(&rbsp);

	rbsp_bit(&rbsp, &forbidden_zero_bit);
	rbsp_bits(&rbsp, 6, &nal_unit_type);
	rbsp_bits(&rbsp, 6, &nuh_layer_id);
	rbsp_bits(&rbsp, 3, &nuh_temporal_id_plus1);

	if (rbsp.error ||
	    forbidden_zero_bit != 0 ||
	    nal_unit_type != SPS_NUT)
		return -EINVAL;

	nal_hevc_rbsp_sps(&rbsp, sps);

	rbsp_trailing_bits(&rbsp);

	if (rbsp.error)
		return rbsp.error;

	return DIV_ROUND_UP(rbsp.pos, 8);
}
EXPORT_SYMBOL_GPL(nal_hevc_read_sps);

/**
 * nal_hevc_write_pps() - Write PPS NAL unit into RBSP format
 * @dev: device pointer
 * @dest: the buffer that is filled with RBSP data
 * @n: maximum size of @dest in bytes
 * @pps: &struct nal_hevc_pps to convert to RBSP
 *
 * Convert @pps to RBSP data and write it into @dest.
 *
 * The size of the PPS NAL unit is not known in advance and this function will
 * fail, if @dest does not hold sufficient space for the PPS NAL unit.
 *
 * Return: number of bytes written to @dest or negative error code
 */
ssize_t nal_hevc_write_pps(const struct device *dev,
			   void *dest, size_t n, struct nal_hevc_pps *pps)
{
	struct rbsp rbsp;
	unsigned int forbidden_zero_bit = 0;
	unsigned int nal_unit_type = PPS_NUT;
	unsigned int nuh_layer_id = 0;
	unsigned int nuh_temporal_id_plus1 = 1;

	if (!dest)
		return -EINVAL;

	rbsp_init(&rbsp, dest, n, &write);

	nal_hevc_write_start_code_prefix(&rbsp);

	/* NAL unit header */
	rbsp_bit(&rbsp, &forbidden_zero_bit);
	rbsp_bits(&rbsp, 6, &nal_unit_type);
	rbsp_bits(&rbsp, 6, &nuh_layer_id);
	rbsp_bits(&rbsp, 3, &nuh_temporal_id_plus1);

	nal_hevc_rbsp_pps(&rbsp, pps);

	rbsp_trailing_bits(&rbsp);

	if (rbsp.error)
		return rbsp.error;

	return DIV_ROUND_UP(rbsp.pos, 8);
}
EXPORT_SYMBOL_GPL(nal_hevc_write_pps);

/**
 * nal_hevc_read_pps() - Read PPS NAL unit from RBSP format
 * @dev: device pointer
 * @pps: the &struct nal_hevc_pps to fill from the RBSP data
 * @src: the buffer that contains the RBSP data
 * @n: size of @src in bytes
 *
 * Read RBSP data from @src and use it to fill @pps.
 *
 * Return: number of bytes read from @src or negative error code
 */
ssize_t nal_hevc_read_pps(const struct device *dev,
			  struct nal_hevc_pps *pps, void *src, size_t n)
{
	struct rbsp rbsp;
	unsigned int forbidden_zero_bit;
	unsigned int nal_unit_type;
	unsigned int nuh_layer_id;
	unsigned int nuh_temporal_id_plus1;

	if (!src)
		return -EINVAL;

	rbsp_init(&rbsp, src, n, &read);

	nal_hevc_read_start_code_prefix(&rbsp);

	/* NAL unit header */
	rbsp_bit(&rbsp, &forbidden_zero_bit);
	rbsp_bits(&rbsp, 6, &nal_unit_type);
	rbsp_bits(&rbsp, 6, &nuh_layer_id);
	rbsp_bits(&rbsp, 3, &nuh_temporal_id_plus1);

	nal_hevc_rbsp_pps(&rbsp, pps);

	rbsp_trailing_bits(&rbsp);

	if (rbsp.error)
		return rbsp.error;

	return DIV_ROUND_UP(rbsp.pos, 8);
}
EXPORT_SYMBOL_GPL(nal_hevc_read_pps);

/**
 * nal_hevc_write_filler() - Write filler data RBSP
 * @dev: device pointer
 * @dest: buffer to fill with filler data
 * @n: size of the buffer to fill with filler data
 *
 * Write a filler data RBSP to @dest with a size of @n bytes and return the
 * number of written filler data bytes.
 *
 * Use this function to generate dummy data in an RBSP data stream that can be
 * safely ignored by hevc decoders.
 *
 * The RBSP format of the filler data is specified in Rec. ITU-T H.265
 * (02/2018) 7.3.2.8 Filler data RBSP syntax.
 *
 * Return: number of filler data bytes (including marker) or negative error
 */
ssize_t nal_hevc_write_filler(const struct device *dev, void *dest, size_t n)
{
	struct rbsp rbsp;
	unsigned int forbidden_zero_bit = 0;
	unsigned int nal_unit_type = FD_NUT;
	unsigned int nuh_layer_id = 0;
	unsigned int nuh_temporal_id_plus1 = 1;

	if (!dest)
		return -EINVAL;

	rbsp_init(&rbsp, dest, n, &write);

	nal_hevc_write_start_code_prefix(&rbsp);

	rbsp_bit(&rbsp, &forbidden_zero_bit);
	rbsp_bits(&rbsp, 6, &nal_unit_type);
	rbsp_bits(&rbsp, 6, &nuh_layer_id);
	rbsp_bits(&rbsp, 3, &nuh_temporal_id_plus1);

	nal_hevc_write_filler_data(&rbsp);
	rbsp_trailing_bits(&rbsp);

	if (rbsp.error)
		return rbsp.error;

	return DIV_ROUND_UP(rbsp.pos, 8);
}
EXPORT_SYMBOL_GPL(nal_hevc_write_filler);

/**
 * nal_hevc_read_filler() - Read filler data RBSP
 * @dev: device pointer
 * @src: buffer with RBSP data that is read
 * @n: maximum size of src that shall be read
 *
 * Read a filler data RBSP from @src up to a maximum size of @n bytes and
 * return the size of the filler data in bytes including the marker.
 *
 * This function is used to parse filler data and skip the respective bytes in
 * the RBSP data.
 *
 * The RBSP format of the filler data is specified in Rec. ITU-T H.265
 * (02/2018) 7.3.2.8 Filler data RBSP syntax.
 *
 * Return: number of filler data bytes (including marker) or negative error
 */
ssize_t nal_hevc_read_filler(const struct device *dev, void *src, size_t n)
{
	struct rbsp rbsp;
	unsigned int forbidden_zero_bit;
	unsigned int nal_unit_type;
	unsigned int nuh_layer_id;
	unsigned int nuh_temporal_id_plus1;

	if (!src)
		return -EINVAL;

	rbsp_init(&rbsp, src, n, &read);

	nal_hevc_read_start_code_prefix(&rbsp);

	rbsp_bit(&rbsp, &forbidden_zero_bit);
	rbsp_bits(&rbsp, 6, &nal_unit_type);
	rbsp_bits(&rbsp, 6, &nuh_layer_id);
	rbsp_bits(&rbsp, 3, &nuh_temporal_id_plus1);

	if (rbsp.error)
		return rbsp.error;
	if (forbidden_zero_bit != 0 ||
	    nal_unit_type != FD_NUT)
		return -EINVAL;

	nal_hevc_read_filler_data(&rbsp);
	rbsp_trailing_bits(&rbsp);

	if (rbsp.error)
		return rbsp.error;

	return DIV_ROUND_UP(rbsp.pos, 8);
}
EXPORT_SYMBOL_GPL(nal_hevc_read_filler);
