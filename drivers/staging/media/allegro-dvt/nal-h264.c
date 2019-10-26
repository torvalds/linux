// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Pengutronix, Michael Tretter <kernel@pengutronix.de>
 *
 * Convert NAL units between raw byte sequence payloads (RBSP) and C structs
 *
 * The conversion is defined in "ITU-T Rec. H.264 (04/2017) Advanced video
 * coding for generic audiovisual services". Decoder drivers may use the
 * parser to parse RBSP from encoded streams and configure the hardware, if
 * the hardware is not able to parse RBSP itself.  Encoder drivers may use the
 * generator to generate the RBSP for SPS/PPS nal units and add them to the
 * encoded stream if the hardware does not generate the units.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/v4l2-controls.h>

#include <linux/device.h>
#include <linux/export.h>
#include <linux/log2.h>

#include "nal-h264.h"

/*
 * See Rec. ITU-T H.264 (04/2017) Table 7-1 – NAL unit type codes, syntax
 * element categories, and NAL unit type classes
 */
enum nal_unit_type {
	SEQUENCE_PARAMETER_SET = 7,
	PICTURE_PARAMETER_SET = 8,
	FILLER_DATA = 12,
};

struct rbsp;

struct nal_h264_ops {
	int (*rbsp_bit)(struct rbsp *rbsp, int *val);
	int (*rbsp_bits)(struct rbsp *rbsp, int n, unsigned int *val);
	int (*rbsp_uev)(struct rbsp *rbsp, unsigned int *val);
	int (*rbsp_sev)(struct rbsp *rbsp, int *val);
};

/**
 * struct rbsp - State object for handling a raw byte sequence payload
 * @data: pointer to the data of the rbsp
 * @size: maximum size of the data of the rbsp
 * @pos: current bit position inside the rbsp
 * @num_consecutive_zeros: number of zeros before @pos
 * @ops: per datatype functions for interacting with the rbsp
 * @error: an error occurred while handling the rbsp
 *
 * This struct is passed around the various parsing functions and tracks the
 * current position within the raw byte sequence payload.
 *
 * The @ops field allows to separate the operation, i.e., reading/writing a
 * value from/to that rbsp, from the structure of the NAL unit. This allows to
 * have a single function for iterating the NAL unit, while @ops has function
 * pointers for handling each type in the rbsp.
 */
struct rbsp {
	u8 *data;
	size_t size;
	unsigned int pos;
	unsigned int num_consecutive_zeros;
	struct nal_h264_ops *ops;
	int error;
};

static void rbsp_init(struct rbsp *rbsp, void *addr, size_t size,
		      struct nal_h264_ops *ops)
{
	if (!rbsp)
		return;

	rbsp->data = addr;
	rbsp->size = size;
	rbsp->pos = 0;
	rbsp->ops = ops;
	rbsp->error = 0;
}

/**
 * nal_h264_profile_from_v4l2() - Get profile_idc for v4l2 h264 profile
 * @profile: the profile as &enum v4l2_mpeg_video_h264_profile
 *
 * Convert the &enum v4l2_mpeg_video_h264_profile to profile_idc as specified
 * in Rec. ITU-T H.264 (04/2017) A.2.
 *
 * Return: the profile_idc for the passed level
 */
int nal_h264_profile_from_v4l2(enum v4l2_mpeg_video_h264_profile profile)
{
	switch (profile) {
	case V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE:
		return 66;
	case V4L2_MPEG_VIDEO_H264_PROFILE_MAIN:
		return 77;
	case V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED:
		return 88;
	case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH:
		return 100;
	default:
		return -EINVAL;
	}
}

/**
 * nal_h264_level_from_v4l2() - Get level_idc for v4l2 h264 level
 * @level: the level as &enum v4l2_mpeg_video_h264_level
 *
 * Convert the &enum v4l2_mpeg_video_h264_level to level_idc as specified in
 * Rec. ITU-T H.264 (04/2017) A.3.2.
 *
 * Return: the level_idc for the passed level
 */
int nal_h264_level_from_v4l2(enum v4l2_mpeg_video_h264_level level)
{
	switch (level) {
	case V4L2_MPEG_VIDEO_H264_LEVEL_1_0:
		return 10;
	case V4L2_MPEG_VIDEO_H264_LEVEL_1B:
		return 9;
	case V4L2_MPEG_VIDEO_H264_LEVEL_1_1:
		return 11;
	case V4L2_MPEG_VIDEO_H264_LEVEL_1_2:
		return 12;
	case V4L2_MPEG_VIDEO_H264_LEVEL_1_3:
		return 13;
	case V4L2_MPEG_VIDEO_H264_LEVEL_2_0:
		return 20;
	case V4L2_MPEG_VIDEO_H264_LEVEL_2_1:
		return 21;
	case V4L2_MPEG_VIDEO_H264_LEVEL_2_2:
		return 22;
	case V4L2_MPEG_VIDEO_H264_LEVEL_3_0:
		return 30;
	case V4L2_MPEG_VIDEO_H264_LEVEL_3_1:
		return 31;
	case V4L2_MPEG_VIDEO_H264_LEVEL_3_2:
		return 32;
	case V4L2_MPEG_VIDEO_H264_LEVEL_4_0:
		return 40;
	case V4L2_MPEG_VIDEO_H264_LEVEL_4_1:
		return 41;
	case V4L2_MPEG_VIDEO_H264_LEVEL_4_2:
		return 42;
	case V4L2_MPEG_VIDEO_H264_LEVEL_5_0:
		return 50;
	case V4L2_MPEG_VIDEO_H264_LEVEL_5_1:
		return 51;
	default:
		return -EINVAL;
	}
}

static int rbsp_read_bits(struct rbsp *rbsp, int n, unsigned int *value);
static int rbsp_write_bits(struct rbsp *rbsp, int n, unsigned int value);

/*
 * When reading or writing, the emulation_prevention_three_byte is detected
 * only when the 2 one bits need to be inserted. Therefore, we are not
 * actually adding the 0x3 byte, but the 2 one bits and the six 0 bits of the
 * next byte.
 */
#define EMULATION_PREVENTION_THREE_BYTE (0x3 << 6)

static int add_emulation_prevention_three_byte(struct rbsp *rbsp)
{
	rbsp->num_consecutive_zeros = 0;
	rbsp_write_bits(rbsp, 8, EMULATION_PREVENTION_THREE_BYTE);

	return 0;
}

static int discard_emulation_prevention_three_byte(struct rbsp *rbsp)
{
	unsigned int tmp = 0;

	rbsp->num_consecutive_zeros = 0;
	rbsp_read_bits(rbsp, 8, &tmp);
	if (tmp != EMULATION_PREVENTION_THREE_BYTE)
		return -EINVAL;

	return 0;
}

static inline int rbsp_read_bit(struct rbsp *rbsp)
{
	int shift;
	int ofs;
	int bit;
	int err;

	if (rbsp->num_consecutive_zeros == 22) {
		err = discard_emulation_prevention_three_byte(rbsp);
		if (err)
			return err;
	}

	shift = 7 - (rbsp->pos % 8);
	ofs = rbsp->pos / 8;
	if (ofs >= rbsp->size)
		return -EINVAL;

	bit = (rbsp->data[ofs] >> shift) & 1;

	rbsp->pos++;

	if (bit == 1 ||
	    (rbsp->num_consecutive_zeros < 7 && (rbsp->pos % 8 == 0)))
		rbsp->num_consecutive_zeros = 0;
	else
		rbsp->num_consecutive_zeros++;

	return bit;
}

static inline int rbsp_write_bit(struct rbsp *rbsp, bool value)
{
	int shift;
	int ofs;

	if (rbsp->num_consecutive_zeros == 22)
		add_emulation_prevention_three_byte(rbsp);

	shift = 7 - (rbsp->pos % 8);
	ofs = rbsp->pos / 8;
	if (ofs >= rbsp->size)
		return -EINVAL;

	rbsp->data[ofs] &= ~(1 << shift);
	rbsp->data[ofs] |= value << shift;

	rbsp->pos++;

	if (value ||
	    (rbsp->num_consecutive_zeros < 7 && (rbsp->pos % 8 == 0))) {
		rbsp->num_consecutive_zeros = 0;
	} else {
		rbsp->num_consecutive_zeros++;
	}

	return 0;
}

static inline int rbsp_read_bits(struct rbsp *rbsp, int n, unsigned int *value)
{
	int i;
	int bit;
	unsigned int tmp = 0;

	if (n > 8 * sizeof(*value))
		return -EINVAL;

	for (i = n; i > 0; i--) {
		bit = rbsp_read_bit(rbsp);
		if (bit < 0)
			return bit;
		tmp |= bit << (i - 1);
	}

	if (value)
		*value = tmp;

	return 0;
}

static int rbsp_write_bits(struct rbsp *rbsp, int n, unsigned int value)
{
	int ret;

	if (n > 8 * sizeof(value))
		return -EINVAL;

	while (n--) {
		ret = rbsp_write_bit(rbsp, (value >> n) & 1);
		if (ret)
			return ret;
	}

	return 0;
}

static int rbsp_read_uev(struct rbsp *rbsp, unsigned int *value)
{
	int leading_zero_bits = 0;
	unsigned int tmp = 0;
	int ret;

	while ((ret = rbsp_read_bit(rbsp)) == 0)
		leading_zero_bits++;
	if (ret < 0)
		return ret;

	if (leading_zero_bits > 0) {
		ret = rbsp_read_bits(rbsp, leading_zero_bits, &tmp);
		if (ret)
			return ret;
	}

	if (value)
		*value = (1 << leading_zero_bits) - 1 + tmp;

	return 0;
}

static int rbsp_write_uev(struct rbsp *rbsp, unsigned int *value)
{
	int ret;
	int leading_zero_bits;

	if (!value)
		return -EINVAL;

	leading_zero_bits = ilog2(*value + 1);

	ret = rbsp_write_bits(rbsp, leading_zero_bits, 0);
	if (ret)
		return ret;

	return rbsp_write_bits(rbsp, leading_zero_bits + 1, *value + 1);
}

static int rbsp_read_sev(struct rbsp *rbsp, int *value)
{
	int ret;
	unsigned int tmp;

	ret = rbsp_read_uev(rbsp, &tmp);
	if (ret)
		return ret;

	if (value) {
		if (tmp & 1)
			*value = (tmp + 1) / 2;
		else
			*value = -(tmp / 2);
	}

	return 0;
}

static int rbsp_write_sev(struct rbsp *rbsp, int *value)
{
	unsigned int tmp;

	if (!value)
		return -EINVAL;

	if (*value > 0)
		tmp = (2 * (*value)) | 1;
	else
		tmp = -2 * (*value);

	return rbsp_write_uev(rbsp, &tmp);
}

static int __rbsp_write_bit(struct rbsp *rbsp, int *value)
{
	return rbsp_write_bit(rbsp, *value);
}

static int __rbsp_write_bits(struct rbsp *rbsp, int n, unsigned int *value)
{
	return rbsp_write_bits(rbsp, n, *value);
}

static struct nal_h264_ops write = {
	.rbsp_bit = __rbsp_write_bit,
	.rbsp_bits = __rbsp_write_bits,
	.rbsp_uev = rbsp_write_uev,
	.rbsp_sev = rbsp_write_sev,
};

static int __rbsp_read_bit(struct rbsp *rbsp, int *value)
{
	int tmp = rbsp_read_bit(rbsp);

	if (tmp < 0)
		return tmp;
	*value = tmp;

	return 0;
}

static struct nal_h264_ops read = {
	.rbsp_bit = __rbsp_read_bit,
	.rbsp_bits = rbsp_read_bits,
	.rbsp_uev = rbsp_read_uev,
	.rbsp_sev = rbsp_read_sev,
};

static inline void rbsp_bit(struct rbsp *rbsp, int *value)
{
	if (rbsp->error)
		return;
	rbsp->error = rbsp->ops->rbsp_bit(rbsp, value);
}

static inline void rbsp_bits(struct rbsp *rbsp, int n, int *value)
{
	if (rbsp->error)
		return;
	rbsp->error = rbsp->ops->rbsp_bits(rbsp, n, value);
}

static inline void rbsp_uev(struct rbsp *rbsp, unsigned int *value)
{
	if (rbsp->error)
		return;
	rbsp->error = rbsp->ops->rbsp_uev(rbsp, value);
}

static inline void rbsp_sev(struct rbsp *rbsp, int *value)
{
	if (rbsp->error)
		return;
	rbsp->error = rbsp->ops->rbsp_sev(rbsp, value);
}

static void nal_h264_rbsp_trailing_bits(struct rbsp *rbsp)
{
	unsigned int rbsp_stop_one_bit = 1;
	unsigned int rbsp_alignment_zero_bit = 0;

	rbsp_bit(rbsp, &rbsp_stop_one_bit);
	rbsp_bits(rbsp, round_up(rbsp->pos, 8) - rbsp->pos,
		  &rbsp_alignment_zero_bit);
}

static void nal_h264_write_start_code_prefix(struct rbsp *rbsp)
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

static void nal_h264_read_start_code_prefix(struct rbsp *rbsp)
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

static void nal_h264_write_filler_data(struct rbsp *rbsp)
{
	u8 *p = rbsp->data + DIV_ROUND_UP(rbsp->pos, 8);
	int i;

	/* Keep 1 byte extra for terminating the NAL unit */
	i = rbsp->size - DIV_ROUND_UP(rbsp->pos, 8) - 1;
	memset(p, 0xff, i);
	rbsp->pos += i * 8;
}

static void nal_h264_read_filler_data(struct rbsp *rbsp)
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

static void nal_h264_rbsp_hrd_parameters(struct rbsp *rbsp,
					 struct nal_h264_hrd_parameters *hrd)
{
	unsigned int i;

	if (!hrd) {
		rbsp->error = -EINVAL;
		return;
	}

	rbsp_uev(rbsp, &hrd->cpb_cnt_minus1);
	rbsp_bits(rbsp, 4, &hrd->bit_rate_scale);
	rbsp_bits(rbsp, 4, &hrd->cpb_size_scale);

	for (i = 0; i <= hrd->cpb_cnt_minus1; i++) {
		rbsp_uev(rbsp, &hrd->bit_rate_value_minus1[i]);
		rbsp_uev(rbsp, &hrd->cpb_size_value_minus1[i]);
		rbsp_bit(rbsp, &hrd->cbr_flag[i]);
	}

	rbsp_bits(rbsp, 5, &hrd->initial_cpb_removal_delay_length_minus1);
	rbsp_bits(rbsp, 5, &hrd->cpb_removal_delay_length_minus1);
	rbsp_bits(rbsp, 5, &hrd->dpb_output_delay_length_minus1);
	rbsp_bits(rbsp, 5, &hrd->time_offset_length);
}

static void nal_h264_rbsp_vui_parameters(struct rbsp *rbsp,
					 struct nal_h264_vui_parameters *vui)
{
	if (!vui) {
		rbsp->error = -EINVAL;
		return;
	}

	rbsp_bit(rbsp, &vui->aspect_ratio_info_present_flag);
	if (vui->aspect_ratio_info_present_flag) {
		rbsp_bits(rbsp, 8, &vui->aspect_ratio_idc);
		if (vui->aspect_ratio_idc == 255) {
			rbsp_bits(rbsp, 16, &vui->sar_width);
			rbsp_bits(rbsp, 16, &vui->sar_height);
		}
	}

	rbsp_bit(rbsp, &vui->overscan_info_present_flag);
	if (vui->overscan_info_present_flag)
		rbsp_bit(rbsp, &vui->overscan_appropriate_flag);

	rbsp_bit(rbsp, &vui->video_signal_type_present_flag);
	if (vui->video_signal_type_present_flag) {
		rbsp_bits(rbsp, 3, &vui->video_format);
		rbsp_bit(rbsp, &vui->video_full_range_flag);

		rbsp_bit(rbsp, &vui->colour_description_present_flag);
		if (vui->colour_description_present_flag) {
			rbsp_bits(rbsp, 8, &vui->colour_primaries);
			rbsp_bits(rbsp, 8, &vui->transfer_characteristics);
			rbsp_bits(rbsp, 8, &vui->matrix_coefficients);
		}
	}

	rbsp_bit(rbsp, &vui->chroma_loc_info_present_flag);
	if (vui->chroma_loc_info_present_flag) {
		rbsp_uev(rbsp, &vui->chroma_sample_loc_type_top_field);
		rbsp_uev(rbsp, &vui->chroma_sample_loc_type_bottom_field);
	}

	rbsp_bit(rbsp, &vui->timing_info_present_flag);
	if (vui->timing_info_present_flag) {
		rbsp_bits(rbsp, 32, &vui->num_units_in_tick);
		rbsp_bits(rbsp, 32, &vui->time_scale);
		rbsp_bit(rbsp, &vui->fixed_frame_rate_flag);
	}

	rbsp_bit(rbsp, &vui->nal_hrd_parameters_present_flag);
	if (vui->nal_hrd_parameters_present_flag)
		nal_h264_rbsp_hrd_parameters(rbsp, &vui->nal_hrd_parameters);

	rbsp_bit(rbsp, &vui->vcl_hrd_parameters_present_flag);
	if (vui->vcl_hrd_parameters_present_flag)
		nal_h264_rbsp_hrd_parameters(rbsp, &vui->vcl_hrd_parameters);

	if (vui->nal_hrd_parameters_present_flag ||
	    vui->vcl_hrd_parameters_present_flag)
		rbsp_bit(rbsp, &vui->low_delay_hrd_flag);

	rbsp_bit(rbsp, &vui->pic_struct_present_flag);

	rbsp_bit(rbsp, &vui->bitstream_restriction_flag);
	if (vui->bitstream_restriction_flag) {
		rbsp_bit(rbsp, &vui->motion_vectors_over_pic_boundaries_flag);
		rbsp_uev(rbsp, &vui->max_bytes_per_pic_denom);
		rbsp_uev(rbsp, &vui->max_bits_per_mb_denom);
		rbsp_uev(rbsp, &vui->log2_max_mv_length_horizontal);
		rbsp_uev(rbsp, &vui->log21_max_mv_length_vertical);
		rbsp_uev(rbsp, &vui->max_num_reorder_frames);
		rbsp_uev(rbsp, &vui->max_dec_frame_buffering);
	}
}

static void nal_h264_rbsp_sps(struct rbsp *rbsp, struct nal_h264_sps *sps)
{
	unsigned int i;

	if (!sps) {
		rbsp->error = -EINVAL;
		return;
	}

	rbsp_bits(rbsp, 8, &sps->profile_idc);
	rbsp_bit(rbsp, &sps->constraint_set0_flag);
	rbsp_bit(rbsp, &sps->constraint_set1_flag);
	rbsp_bit(rbsp, &sps->constraint_set2_flag);
	rbsp_bit(rbsp, &sps->constraint_set3_flag);
	rbsp_bit(rbsp, &sps->constraint_set4_flag);
	rbsp_bit(rbsp, &sps->constraint_set5_flag);
	rbsp_bits(rbsp, 2, &sps->reserved_zero_2bits);
	rbsp_bits(rbsp, 8, &sps->level_idc);

	rbsp_uev(rbsp, &sps->seq_parameter_set_id);

	if (sps->profile_idc == 100 || sps->profile_idc == 110 ||
	    sps->profile_idc == 122 || sps->profile_idc == 244 ||
	    sps->profile_idc == 44 || sps->profile_idc == 83 ||
	    sps->profile_idc == 86 || sps->profile_idc == 118 ||
	    sps->profile_idc == 128 || sps->profile_idc == 138 ||
	    sps->profile_idc == 139 || sps->profile_idc == 134 ||
	    sps->profile_idc == 135) {
		rbsp_uev(rbsp, &sps->chroma_format_idc);

		if (sps->chroma_format_idc == 3)
			rbsp_bit(rbsp, &sps->separate_colour_plane_flag);
		rbsp_uev(rbsp, &sps->bit_depth_luma_minus8);
		rbsp_uev(rbsp, &sps->bit_depth_chroma_minus8);
		rbsp_bit(rbsp, &sps->qpprime_y_zero_transform_bypass_flag);
		rbsp_bit(rbsp, &sps->seq_scaling_matrix_present_flag);
		if (sps->seq_scaling_matrix_present_flag)
			rbsp->error = -EINVAL;
	}

	rbsp_uev(rbsp, &sps->log2_max_frame_num_minus4);

	rbsp_uev(rbsp, &sps->pic_order_cnt_type);
	switch (sps->pic_order_cnt_type) {
	case 0:
		rbsp_uev(rbsp, &sps->log2_max_pic_order_cnt_lsb_minus4);
		break;
	case 1:
		rbsp_bit(rbsp, &sps->delta_pic_order_always_zero_flag);
		rbsp_sev(rbsp, &sps->offset_for_non_ref_pic);
		rbsp_sev(rbsp, &sps->offset_for_top_to_bottom_field);

		rbsp_uev(rbsp, &sps->num_ref_frames_in_pic_order_cnt_cycle);
		for (i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++)
			rbsp_sev(rbsp, &sps->offset_for_ref_frame[i]);
		break;
	default:
		rbsp->error = -EINVAL;
		break;
	}

	rbsp_uev(rbsp, &sps->max_num_ref_frames);
	rbsp_bit(rbsp, &sps->gaps_in_frame_num_value_allowed_flag);
	rbsp_uev(rbsp, &sps->pic_width_in_mbs_minus1);
	rbsp_uev(rbsp, &sps->pic_height_in_map_units_minus1);

	rbsp_bit(rbsp, &sps->frame_mbs_only_flag);
	if (!sps->frame_mbs_only_flag)
		rbsp_bit(rbsp, &sps->mb_adaptive_frame_field_flag);

	rbsp_bit(rbsp, &sps->direct_8x8_inference_flag);

	rbsp_bit(rbsp, &sps->frame_cropping_flag);
	if (sps->frame_cropping_flag) {
		rbsp_uev(rbsp, &sps->crop_left);
		rbsp_uev(rbsp, &sps->crop_right);
		rbsp_uev(rbsp, &sps->crop_top);
		rbsp_uev(rbsp, &sps->crop_bottom);
	}

	rbsp_bit(rbsp, &sps->vui_parameters_present_flag);
	if (sps->vui_parameters_present_flag)
		nal_h264_rbsp_vui_parameters(rbsp, &sps->vui);
}

static void nal_h264_rbsp_pps(struct rbsp *rbsp, struct nal_h264_pps *pps)
{
	int i;

	rbsp_uev(rbsp, &pps->pic_parameter_set_id);
	rbsp_uev(rbsp, &pps->seq_parameter_set_id);
	rbsp_bit(rbsp, &pps->entropy_coding_mode_flag);
	rbsp_bit(rbsp, &pps->bottom_field_pic_order_in_frame_present_flag);
	rbsp_uev(rbsp, &pps->num_slice_groups_minus1);
	if (pps->num_slice_groups_minus1 > 0) {
		rbsp_uev(rbsp, &pps->slice_group_map_type);
		switch (pps->slice_group_map_type) {
		case 0:
			for (i = 0; i < pps->num_slice_groups_minus1; i++)
				rbsp_uev(rbsp, &pps->run_length_minus1[i]);
			break;
		case 2:
			for (i = 0; i < pps->num_slice_groups_minus1; i++) {
				rbsp_uev(rbsp, &pps->top_left[i]);
				rbsp_uev(rbsp, &pps->bottom_right[i]);
			}
			break;
		case 3: case 4: case 5:
			rbsp_bit(rbsp, &pps->slice_group_change_direction_flag);
			rbsp_uev(rbsp, &pps->slice_group_change_rate_minus1);
			break;
		case 6:
			rbsp_uev(rbsp, &pps->pic_size_in_map_units_minus1);
			for (i = 0; i < pps->pic_size_in_map_units_minus1; i++)
				rbsp_bits(rbsp,
					  order_base_2(pps->num_slice_groups_minus1 + 1),
					  &pps->slice_group_id[i]);
			break;
		default:
			break;
		}
	}
	rbsp_uev(rbsp, &pps->num_ref_idx_l0_default_active_minus1);
	rbsp_uev(rbsp, &pps->num_ref_idx_l1_default_active_minus1);
	rbsp_bit(rbsp, &pps->weighted_pred_flag);
	rbsp_bits(rbsp, 2, &pps->weighted_bipred_idc);
	rbsp_sev(rbsp, &pps->pic_init_qp_minus26);
	rbsp_sev(rbsp, &pps->pic_init_qs_minus26);
	rbsp_sev(rbsp, &pps->chroma_qp_index_offset);
	rbsp_bit(rbsp, &pps->deblocking_filter_control_present_flag);
	rbsp_bit(rbsp, &pps->constrained_intra_pred_flag);
	rbsp_bit(rbsp, &pps->redundant_pic_cnt_present_flag);
	if (/* more_rbsp_data() */ false) {
		rbsp_bit(rbsp, &pps->transform_8x8_mode_flag);
		rbsp_bit(rbsp, &pps->pic_scaling_matrix_present_flag);
		if (pps->pic_scaling_matrix_present_flag)
			rbsp->error = -EINVAL;
		rbsp_sev(rbsp, &pps->second_chroma_qp_index_offset);
	}
}

/**
 * nal_h264_write_sps() - Write SPS NAL unit into RBSP format
 * @dev: device pointer
 * @dest: the buffer that is filled with RBSP data
 * @n: maximum size of @dest in bytes
 * @sps: &struct nal_h264_sps to convert to RBSP
 *
 * Convert @sps to RBSP data and write it into @dest.
 *
 * The size of the SPS NAL unit is not known in advance and this function will
 * fail, if @dest does not hold sufficient space for the SPS NAL unit.
 *
 * Return: number of bytes written to @dest or negative error code
 */
ssize_t nal_h264_write_sps(const struct device *dev,
			   void *dest, size_t n, struct nal_h264_sps *sps)
{
	struct rbsp rbsp;
	unsigned int forbidden_zero_bit = 0;
	unsigned int nal_ref_idc = 0;
	unsigned int nal_unit_type = SEQUENCE_PARAMETER_SET;

	if (!dest)
		return -EINVAL;

	rbsp_init(&rbsp, dest, n, &write);

	nal_h264_write_start_code_prefix(&rbsp);

	rbsp_bit(&rbsp, &forbidden_zero_bit);
	rbsp_bits(&rbsp, 2, &nal_ref_idc);
	rbsp_bits(&rbsp, 5, &nal_unit_type);

	nal_h264_rbsp_sps(&rbsp, sps);

	nal_h264_rbsp_trailing_bits(&rbsp);

	if (rbsp.error)
		return rbsp.error;

	return DIV_ROUND_UP(rbsp.pos, 8);
}
EXPORT_SYMBOL_GPL(nal_h264_write_sps);

/**
 * nal_h264_read_sps() - Read SPS NAL unit from RBSP format
 * @dev: device pointer
 * @sps: the &struct nal_h264_sps to fill from the RBSP data
 * @src: the buffer that contains the RBSP data
 * @n: size of @src in bytes
 *
 * Read RBSP data from @src and use it to fill @sps.
 *
 * Return: number of bytes read from @src or negative error code
 */
ssize_t nal_h264_read_sps(const struct device *dev,
			  struct nal_h264_sps *sps, void *src, size_t n)
{
	struct rbsp rbsp;
	unsigned int forbidden_zero_bit;
	unsigned int nal_ref_idc;
	unsigned int nal_unit_type;

	if (!src)
		return -EINVAL;

	rbsp_init(&rbsp, src, n, &read);

	nal_h264_read_start_code_prefix(&rbsp);

	rbsp_bit(&rbsp, &forbidden_zero_bit);
	rbsp_bits(&rbsp, 2, &nal_ref_idc);
	rbsp_bits(&rbsp, 5, &nal_unit_type);

	if (rbsp.error ||
	    forbidden_zero_bit != 0 ||
	    nal_ref_idc != 0 ||
	    nal_unit_type != SEQUENCE_PARAMETER_SET)
		return -EINVAL;

	nal_h264_rbsp_sps(&rbsp, sps);

	nal_h264_rbsp_trailing_bits(&rbsp);

	if (rbsp.error)
		return rbsp.error;

	return DIV_ROUND_UP(rbsp.pos, 8);
}
EXPORT_SYMBOL_GPL(nal_h264_read_sps);

/**
 * nal_h264_write_pps() - Write PPS NAL unit into RBSP format
 * @dev: device pointer
 * @dest: the buffer that is filled with RBSP data
 * @n: maximum size of @dest in bytes
 * @pps: &struct nal_h264_pps to convert to RBSP
 *
 * Convert @pps to RBSP data and write it into @dest.
 *
 * The size of the PPS NAL unit is not known in advance and this function will
 * fail, if @dest does not hold sufficient space for the PPS NAL unit.
 *
 * Return: number of bytes written to @dest or negative error code
 */
ssize_t nal_h264_write_pps(const struct device *dev,
			   void *dest, size_t n, struct nal_h264_pps *pps)
{
	struct rbsp rbsp;
	unsigned int forbidden_zero_bit = 0;
	unsigned int nal_ref_idc = 0;
	unsigned int nal_unit_type = PICTURE_PARAMETER_SET;

	if (!dest)
		return -EINVAL;

	rbsp_init(&rbsp, dest, n, &write);

	nal_h264_write_start_code_prefix(&rbsp);

	/* NAL unit header */
	rbsp_bit(&rbsp, &forbidden_zero_bit);
	rbsp_bits(&rbsp, 2, &nal_ref_idc);
	rbsp_bits(&rbsp, 5, &nal_unit_type);

	nal_h264_rbsp_pps(&rbsp, pps);

	nal_h264_rbsp_trailing_bits(&rbsp);

	if (rbsp.error)
		return rbsp.error;

	return DIV_ROUND_UP(rbsp.pos, 8);
}
EXPORT_SYMBOL_GPL(nal_h264_write_pps);

/**
 * nal_h264_read_pps() - Read PPS NAL unit from RBSP format
 * @dev: device pointer
 * @pps: the &struct nal_h264_pps to fill from the RBSP data
 * @src: the buffer that contains the RBSP data
 * @n: size of @src in bytes
 *
 * Read RBSP data from @src and use it to fill @pps.
 *
 * Return: number of bytes read from @src or negative error code
 */
ssize_t nal_h264_read_pps(const struct device *dev,
			  struct nal_h264_pps *pps, void *src, size_t n)
{
	struct rbsp rbsp;

	if (!src)
		return -EINVAL;

	rbsp_init(&rbsp, src, n, &read);

	nal_h264_read_start_code_prefix(&rbsp);

	/* NAL unit header */
	rbsp.pos += 8;

	nal_h264_rbsp_pps(&rbsp, pps);

	nal_h264_rbsp_trailing_bits(&rbsp);

	if (rbsp.error)
		return rbsp.error;

	return DIV_ROUND_UP(rbsp.pos, 8);
}
EXPORT_SYMBOL_GPL(nal_h264_read_pps);

/**
 * nal_h264_write_filler() - Write filler data RBSP
 * @dev: device pointer
 * @dest: buffer to fill with filler data
 * @n: size of the buffer to fill with filler data
 *
 * Write a filler data RBSP to @dest with a size of @n bytes and return the
 * number of written filler data bytes.
 *
 * Use this function to generate dummy data in an RBSP data stream that can be
 * safely ignored by h264 decoders.
 *
 * The RBSP format of the filler data is specified in Rec. ITU-T H.264
 * (04/2017) 7.3.2.7 Filler data RBSP syntax.
 *
 * Return: number of filler data bytes (including marker) or negative error
 */
ssize_t nal_h264_write_filler(const struct device *dev, void *dest, size_t n)
{
	struct rbsp rbsp;
	unsigned int forbidden_zero_bit = 0;
	unsigned int nal_ref_idc = 0;
	unsigned int nal_unit_type = FILLER_DATA;

	if (!dest)
		return -EINVAL;

	rbsp_init(&rbsp, dest, n, &write);

	nal_h264_write_start_code_prefix(&rbsp);

	rbsp_bit(&rbsp, &forbidden_zero_bit);
	rbsp_bits(&rbsp, 2, &nal_ref_idc);
	rbsp_bits(&rbsp, 5, &nal_unit_type);

	nal_h264_write_filler_data(&rbsp);

	nal_h264_rbsp_trailing_bits(&rbsp);

	return DIV_ROUND_UP(rbsp.pos, 8);
}
EXPORT_SYMBOL_GPL(nal_h264_write_filler);

/**
 * nal_h264_read_filler() - Read filler data RBSP
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
 * The RBSP format of the filler data is specified in Rec. ITU-T H.264
 * (04/2017) 7.3.2.7 Filler data RBSP syntax.
 *
 * Return: number of filler data bytes (including marker) or negative error
 */
ssize_t nal_h264_read_filler(const struct device *dev, void *src, size_t n)
{
	struct rbsp rbsp;
	unsigned int forbidden_zero_bit;
	unsigned int nal_ref_idc;
	unsigned int nal_unit_type;

	if (!src)
		return -EINVAL;

	rbsp_init(&rbsp, src, n, &read);

	nal_h264_read_start_code_prefix(&rbsp);

	rbsp_bit(&rbsp, &forbidden_zero_bit);
	rbsp_bits(&rbsp, 2, &nal_ref_idc);
	rbsp_bits(&rbsp, 5, &nal_unit_type);

	if (rbsp.error)
		return rbsp.error;
	if (forbidden_zero_bit != 0 ||
	    nal_ref_idc != 0 ||
	    nal_unit_type != FILLER_DATA)
		return -EINVAL;

	nal_h264_read_filler_data(&rbsp);
	nal_h264_rbsp_trailing_bits(&rbsp);

	if (rbsp.error)
		return rbsp.error;

	return DIV_ROUND_UP(rbsp.pos, 8);
}
EXPORT_SYMBOL_GPL(nal_h264_read_filler);
