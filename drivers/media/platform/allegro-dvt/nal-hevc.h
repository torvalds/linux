/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 Pengutronix, Michael Tretter <kernel@pengutronix.de>
 *
 * Convert NAL units between raw byte sequence payloads (RBSP) and C structs.
 */

#ifndef __NAL_HEVC_H__
#define __NAL_HEVC_H__

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/v4l2-controls.h>
#include <linux/videodev2.h>

struct nal_hevc_profile_tier_level {
	unsigned int general_profile_space;
	unsigned int general_tier_flag;
	unsigned int general_profile_idc;
	unsigned int general_profile_compatibility_flag[32];
	unsigned int general_progressive_source_flag;
	unsigned int general_interlaced_source_flag;
	unsigned int general_non_packed_constraint_flag;
	unsigned int general_frame_only_constraint_flag;
	union {
		struct {
			unsigned int general_max_12bit_constraint_flag;
			unsigned int general_max_10bit_constraint_flag;
			unsigned int general_max_8bit_constraint_flag;
			unsigned int general_max_422chroma_constraint_flag;
			unsigned int general_max_420chroma_constraint_flag;
			unsigned int general_max_monochrome_constraint_flag;
			unsigned int general_intra_constraint_flag;
			unsigned int general_one_picture_only_constraint_flag;
			unsigned int general_lower_bit_rate_constraint_flag;
			union {
				struct {
					unsigned int general_max_14bit_constraint_flag;
					unsigned int general_reserved_zero_33bits;
				};
				unsigned int general_reserved_zero_34bits;
			};
		};
		struct {
			unsigned int general_reserved_zero_7bits;
			/* unsigned int general_one_picture_only_constraint_flag; */
			unsigned int general_reserved_zero_35bits;
		};
		unsigned int general_reserved_zero_43bits;
	};
	union {
		unsigned int general_inbld_flag;
		unsigned int general_reserved_zero_bit;
	};
	unsigned int general_level_idc;
};

/*
 * struct nal_hevc_vps - Video parameter set
 *
 * C struct representation of the video parameter set NAL unit as defined by
 * Rec. ITU-T H.265 (02/2018) 7.3.2.1 Video parameter set RBSP syntax
 */
struct nal_hevc_vps {
	unsigned int video_parameter_set_id;
	unsigned int base_layer_internal_flag;
	unsigned int base_layer_available_flag;
	unsigned int max_layers_minus1;
	unsigned int max_sub_layers_minus1;
	unsigned int temporal_id_nesting_flag;
	struct nal_hevc_profile_tier_level profile_tier_level;
	unsigned int sub_layer_ordering_info_present_flag;
	struct {
		unsigned int max_dec_pic_buffering_minus1[7];
		unsigned int max_num_reorder_pics[7];
		unsigned int max_latency_increase_plus1[7];
	};
	unsigned int max_layer_id;
	unsigned int num_layer_sets_minus1;
	unsigned int layer_id_included_flag[1024][64];
	unsigned int timing_info_present_flag;
	struct {
		unsigned int num_units_in_tick;
		unsigned int time_scale;
		unsigned int poc_proportional_to_timing_flag;
		unsigned int num_ticks_poc_diff_one_minus1;
		unsigned int num_hrd_parameters;
		struct {
			unsigned int hrd_layer_set_idx[0];
			unsigned int cprms_present_flag[0];
		};
		/* hrd_parameters( cprms_present_flag[ i ], max_sub_layers_minus1 ) */
	};
	unsigned int extension_flag;
	unsigned int extension_data_flag;
};

struct nal_hevc_sub_layer_hrd_parameters {
	unsigned int bit_rate_value_minus1[1];
	unsigned int cpb_size_value_minus1[1];
	unsigned int cbr_flag[1];
};

struct nal_hevc_hrd_parameters {
	unsigned int nal_hrd_parameters_present_flag;
	unsigned int vcl_hrd_parameters_present_flag;
	struct {
		unsigned int sub_pic_hrd_params_present_flag;
		struct {
			unsigned int tick_divisor_minus2;
			unsigned int du_cpb_removal_delay_increment_length_minus1;
			unsigned int sub_pic_cpb_params_in_pic_timing_sei_flag;
			unsigned int dpb_output_delay_du_length_minus1;
		};
		unsigned int bit_rate_scale;
		unsigned int cpb_size_scale;
		unsigned int cpb_size_du_scale;
		unsigned int initial_cpb_removal_delay_length_minus1;
		unsigned int au_cpb_removal_delay_length_minus1;
		unsigned int dpb_output_delay_length_minus1;
	};
	struct {
		unsigned int fixed_pic_rate_general_flag[1];
		unsigned int fixed_pic_rate_within_cvs_flag[1];
		unsigned int elemental_duration_in_tc_minus1[1];
		unsigned int low_delay_hrd_flag[1];
		unsigned int cpb_cnt_minus1[1];
		struct nal_hevc_sub_layer_hrd_parameters nal_hrd[1];
		struct nal_hevc_sub_layer_hrd_parameters vcl_hrd[1];
	};
};

/*
 * struct nal_hevc_vui_parameters - VUI parameters
 *
 * C struct representation of the VUI parameters as defined by Rec. ITU-T
 * H.265 (02/2018) E.2.1 VUI parameters syntax.
 */
struct nal_hevc_vui_parameters {
	unsigned int aspect_ratio_info_present_flag;
	struct {
		unsigned int aspect_ratio_idc;
		unsigned int sar_width;
		unsigned int sar_height;
	};
	unsigned int overscan_info_present_flag;
	unsigned int overscan_appropriate_flag;
	unsigned int video_signal_type_present_flag;
	struct {
		unsigned int video_format;
		unsigned int video_full_range_flag;
		unsigned int colour_description_present_flag;
		struct {
			unsigned int colour_primaries;
			unsigned int transfer_characteristics;
			unsigned int matrix_coeffs;
		};
	};
	unsigned int chroma_loc_info_present_flag;
	struct {
		unsigned int chroma_sample_loc_type_top_field;
		unsigned int chroma_sample_loc_type_bottom_field;
	};
	unsigned int neutral_chroma_indication_flag;
	unsigned int field_seq_flag;
	unsigned int frame_field_info_present_flag;
	unsigned int default_display_window_flag;
	struct {
		unsigned int def_disp_win_left_offset;
		unsigned int def_disp_win_right_offset;
		unsigned int def_disp_win_top_offset;
		unsigned int def_disp_win_bottom_offset;
	};
	unsigned int vui_timing_info_present_flag;
	struct {
		unsigned int vui_num_units_in_tick;
		unsigned int vui_time_scale;
		unsigned int vui_poc_proportional_to_timing_flag;
		unsigned int vui_num_ticks_poc_diff_one_minus1;
		unsigned int vui_hrd_parameters_present_flag;
		struct nal_hevc_hrd_parameters nal_hrd_parameters;
	};
	unsigned int bitstream_restriction_flag;
	struct {
		unsigned int tiles_fixed_structure_flag;
		unsigned int motion_vectors_over_pic_boundaries_flag;
		unsigned int restricted_ref_pic_lists_flag;
		unsigned int min_spatial_segmentation_idc;
		unsigned int max_bytes_per_pic_denom;
		unsigned int max_bits_per_min_cu_denom;
		unsigned int log2_max_mv_length_horizontal;
		unsigned int log2_max_mv_length_vertical;
	};
};

/*
 * struct nal_hevc_sps - Sequence parameter set
 *
 * C struct representation of the video parameter set NAL unit as defined by
 * Rec. ITU-T H.265 (02/2018) 7.3.2.2 Sequence parameter set RBSP syntax
 */
struct nal_hevc_sps {
	unsigned int video_parameter_set_id;
	unsigned int max_sub_layers_minus1;
	unsigned int temporal_id_nesting_flag;
	struct nal_hevc_profile_tier_level profile_tier_level;
	unsigned int seq_parameter_set_id;
	unsigned int chroma_format_idc;
	unsigned int separate_colour_plane_flag;
	unsigned int pic_width_in_luma_samples;
	unsigned int pic_height_in_luma_samples;
	unsigned int conformance_window_flag;
	struct {
		unsigned int conf_win_left_offset;
		unsigned int conf_win_right_offset;
		unsigned int conf_win_top_offset;
		unsigned int conf_win_bottom_offset;
	};

	unsigned int bit_depth_luma_minus8;
	unsigned int bit_depth_chroma_minus8;
	unsigned int log2_max_pic_order_cnt_lsb_minus4;
	unsigned int sub_layer_ordering_info_present_flag;
	struct {
		unsigned int max_dec_pic_buffering_minus1[7];
		unsigned int max_num_reorder_pics[7];
		unsigned int max_latency_increase_plus1[7];
	};
	unsigned int log2_min_luma_coding_block_size_minus3;
	unsigned int log2_diff_max_min_luma_coding_block_size;
	unsigned int log2_min_luma_transform_block_size_minus2;
	unsigned int log2_diff_max_min_luma_transform_block_size;
	unsigned int max_transform_hierarchy_depth_inter;
	unsigned int max_transform_hierarchy_depth_intra;

	unsigned int scaling_list_enabled_flag;
	unsigned int scaling_list_data_present_flag;
	unsigned int amp_enabled_flag;
	unsigned int sample_adaptive_offset_enabled_flag;
	unsigned int pcm_enabled_flag;
	struct {
		unsigned int pcm_sample_bit_depth_luma_minus1;
		unsigned int pcm_sample_bit_depth_chroma_minus1;
		unsigned int log2_min_pcm_luma_coding_block_size_minus3;
		unsigned int log2_diff_max_min_pcm_luma_coding_block_size;
		unsigned int pcm_loop_filter_disabled_flag;
	};

	unsigned int num_short_term_ref_pic_sets;
	unsigned int long_term_ref_pics_present_flag;
	unsigned int sps_temporal_mvp_enabled_flag;
	unsigned int strong_intra_smoothing_enabled_flag;
	unsigned int vui_parameters_present_flag;
	struct nal_hevc_vui_parameters vui;
	unsigned int extension_present_flag;
	struct {
		unsigned int sps_range_extension_flag;
		unsigned int sps_multilayer_extension_flag;
		unsigned int sps_3d_extension_flag;
		unsigned int sps_scc_extension_flag;
		unsigned int sps_extension_4bits;
	};
};

struct nal_hevc_pps {
	unsigned int pps_pic_parameter_set_id;
	unsigned int pps_seq_parameter_set_id;
	unsigned int dependent_slice_segments_enabled_flag;
	unsigned int output_flag_present_flag;
	unsigned int num_extra_slice_header_bits;
	unsigned int sign_data_hiding_enabled_flag;
	unsigned int cabac_init_present_flag;
	unsigned int num_ref_idx_l0_default_active_minus1;
	unsigned int num_ref_idx_l1_default_active_minus1;
	int init_qp_minus26;
	unsigned int constrained_intra_pred_flag;
	unsigned int transform_skip_enabled_flag;
	unsigned int cu_qp_delta_enabled_flag;
	unsigned int diff_cu_qp_delta_depth;
	int pps_cb_qp_offset;
	int pps_cr_qp_offset;
	unsigned int pps_slice_chroma_qp_offsets_present_flag;
	unsigned int weighted_pred_flag;
	unsigned int weighted_bipred_flag;
	unsigned int transquant_bypass_enabled_flag;
	unsigned int tiles_enabled_flag;
	unsigned int entropy_coding_sync_enabled_flag;
	struct {
		unsigned int num_tile_columns_minus1;
		unsigned int num_tile_rows_minus1;
		unsigned int uniform_spacing_flag;
		struct {
			unsigned int column_width_minus1[1];
			unsigned int row_height_minus1[1];
		};
		unsigned int loop_filter_across_tiles_enabled_flag;
	};
	unsigned int pps_loop_filter_across_slices_enabled_flag;
	unsigned int deblocking_filter_control_present_flag;
	struct {
		unsigned int deblocking_filter_override_enabled_flag;
		unsigned int pps_deblocking_filter_disabled_flag;
		struct {
			int pps_beta_offset_div2;
			int pps_tc_offset_div2;
		};
	};
	unsigned int pps_scaling_list_data_present_flag;
	unsigned int lists_modification_present_flag;
	unsigned int log2_parallel_merge_level_minus2;
	unsigned int slice_segment_header_extension_present_flag;
	unsigned int pps_extension_present_flag;
	struct {
		unsigned int pps_range_extension_flag;
		unsigned int pps_multilayer_extension_flag;
		unsigned int pps_3d_extension_flag;
		unsigned int pps_scc_extension_flag;
		unsigned int pps_extension_4bits;
	};
};

/**
 * nal_hevc_profile() - Get profile_idc for v4l2 hevc profile
 * @profile: the profile as &enum v4l2_mpeg_video_hevc_profile
 *
 * Convert the &enum v4l2_mpeg_video_hevc_profile to profile_idc as specified
 * in Rec. ITU-T H.265 (02/2018) A.3.
 *
 * Return: the profile_idc for the passed level
 */
static inline int nal_hevc_profile(enum v4l2_mpeg_video_hevc_profile profile)
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

/**
 * nal_hevc_tier() - Get tier_flag for v4l2 hevc tier
 * @tier: the tier as &enum v4l2_mpeg_video_hevc_tier
 *
 * Convert the &enum v4l2_mpeg_video_hevc_tier to tier_flag as specified
 * in Rec. ITU-T H.265 (02/2018) A.4.1.
 *
 * Return: the tier_flag for the passed tier
 */
static inline int nal_hevc_tier(enum v4l2_mpeg_video_hevc_tier tier)
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

/**
 * nal_hevc_level() - Get level_idc for v4l2 hevc level
 * @level: the level as &enum v4l2_mpeg_video_hevc_level
 *
 * Convert the &enum v4l2_mpeg_video_hevc_level to level_idc as specified in
 * Rec. ITU-T H.265 (02/2018) A.4.1.
 *
 * Return: the level_idc for the passed level
 */
static inline int nal_hevc_level(enum v4l2_mpeg_video_hevc_level level)
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

static inline int nal_hevc_full_range(enum v4l2_quantization quantization)
{
	switch (quantization) {
	case V4L2_QUANTIZATION_FULL_RANGE:
		return 1;
	case V4L2_QUANTIZATION_LIM_RANGE:
		return 0;
	default:
		break;
	}

	return 0;
}

static inline int nal_hevc_color_primaries(enum v4l2_colorspace colorspace)
{
	switch (colorspace) {
	case V4L2_COLORSPACE_SMPTE170M:
		return 6;
	case V4L2_COLORSPACE_SMPTE240M:
		return 7;
	case V4L2_COLORSPACE_REC709:
		return 1;
	case V4L2_COLORSPACE_470_SYSTEM_M:
		return 4;
	case V4L2_COLORSPACE_JPEG:
	case V4L2_COLORSPACE_SRGB:
	case V4L2_COLORSPACE_470_SYSTEM_BG:
		return 5;
	case V4L2_COLORSPACE_BT2020:
		return 9;
	case V4L2_COLORSPACE_DEFAULT:
	case V4L2_COLORSPACE_OPRGB:
	case V4L2_COLORSPACE_RAW:
	case V4L2_COLORSPACE_DCI_P3:
	default:
		return 2;
	}
}

static inline int nal_hevc_transfer_characteristics(enum v4l2_colorspace colorspace,
						    enum v4l2_xfer_func xfer_func)
{
	if (xfer_func == V4L2_XFER_FUNC_DEFAULT)
		xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(colorspace);

	switch (xfer_func) {
	case V4L2_XFER_FUNC_709:
		return 6;
	case V4L2_XFER_FUNC_SMPTE2084:
		return 16;
	case V4L2_XFER_FUNC_SRGB:
	case V4L2_XFER_FUNC_OPRGB:
	case V4L2_XFER_FUNC_NONE:
	case V4L2_XFER_FUNC_DCI_P3:
	case V4L2_XFER_FUNC_SMPTE240M:
	default:
		return 2;
	}
}

static inline int nal_hevc_matrix_coeffs(enum v4l2_colorspace colorspace,
					 enum v4l2_ycbcr_encoding ycbcr_encoding)
{
	if (ycbcr_encoding == V4L2_YCBCR_ENC_DEFAULT)
		ycbcr_encoding = V4L2_MAP_YCBCR_ENC_DEFAULT(colorspace);

	switch (ycbcr_encoding) {
	case V4L2_YCBCR_ENC_601:
	case V4L2_YCBCR_ENC_XV601:
		return 5;
	case V4L2_YCBCR_ENC_709:
	case V4L2_YCBCR_ENC_XV709:
		return 1;
	case V4L2_YCBCR_ENC_BT2020:
		return 9;
	case V4L2_YCBCR_ENC_BT2020_CONST_LUM:
		return 10;
	case V4L2_YCBCR_ENC_SMPTE240M:
	default:
		return 2;
	}
}

ssize_t nal_hevc_write_vps(const struct device *dev,
			   void *dest, size_t n, struct nal_hevc_vps *vps);
ssize_t nal_hevc_read_vps(const struct device *dev,
			  struct nal_hevc_vps *vps, void *src, size_t n);

ssize_t nal_hevc_write_sps(const struct device *dev,
			   void *dest, size_t n, struct nal_hevc_sps *sps);
ssize_t nal_hevc_read_sps(const struct device *dev,
			  struct nal_hevc_sps *sps, void *src, size_t n);

ssize_t nal_hevc_write_pps(const struct device *dev,
			   void *dest, size_t n, struct nal_hevc_pps *pps);
ssize_t nal_hevc_read_pps(const struct device *dev,
			  struct nal_hevc_pps *pps, void *src, size_t n);

ssize_t nal_hevc_write_filler(const struct device *dev, void *dest, size_t n);
ssize_t nal_hevc_read_filler(const struct device *dev, void *src, size_t n);

#endif /* __NAL_HEVC_H__ */
