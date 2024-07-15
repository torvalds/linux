// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Coda multi-standard codec IP - H.264 helper functions
 *
 * Copyright (C) 2012 Vista Silicon S.L.
 *    Javier Martin, <javier.martin@vista-silicon.com>
 *    Xavier Duret
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/videodev2.h>

#include "coda.h"

static const u8 coda_filler_size[8] = { 0, 7, 14, 13, 12, 11, 10, 9 };

static const u8 *coda_find_nal_header(const u8 *buf, const u8 *end)
{
	u32 val = 0xffffffff;

	do {
		val = val << 8 | *buf++;
		if (buf >= end)
			return NULL;
	} while (val != 0x00000001);

	return buf;
}

int coda_sps_parse_profile(struct coda_ctx *ctx, struct vb2_buffer *vb)
{
	const u8 *buf = vb2_plane_vaddr(vb, 0);
	const u8 *end = buf + vb2_get_plane_payload(vb, 0);

	/* Find SPS header */
	do {
		buf = coda_find_nal_header(buf, end);
		if (!buf)
			return -EINVAL;
	} while ((*buf++ & 0x1f) != 0x7);

	ctx->params.h264_profile_idc = buf[0];
	ctx->params.h264_level_idc = buf[2];

	return 0;
}

int coda_h264_filler_nal(int size, char *p)
{
	if (size < 6)
		return -EINVAL;

	p[0] = 0x00;
	p[1] = 0x00;
	p[2] = 0x00;
	p[3] = 0x01;
	p[4] = 0x0c;
	memset(p + 5, 0xff, size - 6);
	/* Add rbsp stop bit and trailing at the end */
	p[size - 1] = 0x80;

	return 0;
}

int coda_h264_padding(int size, char *p)
{
	int nal_size;
	int diff;

	diff = size - (size & ~0x7);
	if (diff == 0)
		return 0;

	nal_size = coda_filler_size[diff];
	coda_h264_filler_nal(nal_size, p);

	return nal_size;
}

int coda_h264_profile(int profile_idc)
{
	switch (profile_idc) {
	case 66: return V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;
	case 77: return V4L2_MPEG_VIDEO_H264_PROFILE_MAIN;
	case 88: return V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED;
	case 100: return V4L2_MPEG_VIDEO_H264_PROFILE_HIGH;
	default: return -EINVAL;
	}
}

int coda_h264_level(int level_idc)
{
	switch (level_idc) {
	case 10: return V4L2_MPEG_VIDEO_H264_LEVEL_1_0;
	case 9:  return V4L2_MPEG_VIDEO_H264_LEVEL_1B;
	case 11: return V4L2_MPEG_VIDEO_H264_LEVEL_1_1;
	case 12: return V4L2_MPEG_VIDEO_H264_LEVEL_1_2;
	case 13: return V4L2_MPEG_VIDEO_H264_LEVEL_1_3;
	case 20: return V4L2_MPEG_VIDEO_H264_LEVEL_2_0;
	case 21: return V4L2_MPEG_VIDEO_H264_LEVEL_2_1;
	case 22: return V4L2_MPEG_VIDEO_H264_LEVEL_2_2;
	case 30: return V4L2_MPEG_VIDEO_H264_LEVEL_3_0;
	case 31: return V4L2_MPEG_VIDEO_H264_LEVEL_3_1;
	case 32: return V4L2_MPEG_VIDEO_H264_LEVEL_3_2;
	case 40: return V4L2_MPEG_VIDEO_H264_LEVEL_4_0;
	case 41: return V4L2_MPEG_VIDEO_H264_LEVEL_4_1;
	case 42: return V4L2_MPEG_VIDEO_H264_LEVEL_4_2;
	case 50: return V4L2_MPEG_VIDEO_H264_LEVEL_5_0;
	case 51: return V4L2_MPEG_VIDEO_H264_LEVEL_5_1;
	default: return -EINVAL;
	}
}

struct rbsp {
	char *buf;
	int size;
	int pos;
};

static inline int rbsp_read_bit(struct rbsp *rbsp)
{
	int shift = 7 - (rbsp->pos % 8);
	int ofs = rbsp->pos++ / 8;

	if (ofs >= rbsp->size)
		return -EINVAL;

	return (rbsp->buf[ofs] >> shift) & 1;
}

static inline int rbsp_write_bit(struct rbsp *rbsp, int bit)
{
	int shift = 7 - (rbsp->pos % 8);
	int ofs = rbsp->pos++ / 8;

	if (ofs >= rbsp->size)
		return -EINVAL;

	rbsp->buf[ofs] &= ~(1 << shift);
	rbsp->buf[ofs] |= bit << shift;

	return 0;
}

static inline int rbsp_read_bits(struct rbsp *rbsp, int num, int *val)
{
	int i, ret;
	int tmp = 0;

	if (num > 32)
		return -EINVAL;

	for (i = 0; i < num; i++) {
		ret = rbsp_read_bit(rbsp);
		if (ret < 0)
			return ret;
		tmp |= ret << (num - i - 1);
	}

	if (val)
		*val = tmp;

	return 0;
}

static int rbsp_write_bits(struct rbsp *rbsp, int num, int value)
{
	int ret;

	while (num--) {
		ret = rbsp_write_bit(rbsp, (value >> num) & 1);
		if (ret)
			return ret;
	}

	return 0;
}

static int rbsp_read_uev(struct rbsp *rbsp, unsigned int *val)
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

	if (val)
		*val = (1 << leading_zero_bits) - 1 + tmp;

	return 0;
}

static int rbsp_write_uev(struct rbsp *rbsp, unsigned int value)
{
	int i;
	int ret;
	int tmp = value + 1;
	int leading_zero_bits = fls(tmp) - 1;

	for (i = 0; i < leading_zero_bits; i++) {
		ret = rbsp_write_bit(rbsp, 0);
		if (ret)
			return ret;
	}

	return rbsp_write_bits(rbsp, leading_zero_bits + 1, tmp);
}

static int rbsp_read_sev(struct rbsp *rbsp, int *val)
{
	unsigned int tmp;
	int ret;

	ret = rbsp_read_uev(rbsp, &tmp);
	if (ret)
		return ret;

	if (val) {
		if (tmp & 1)
			*val = (tmp + 1) / 2;
		else
			*val = -(tmp / 2);
	}

	return 0;
}

/**
 * coda_h264_sps_fixup - fixes frame cropping values in h.264 SPS
 * @ctx: encoder context
 * @width: visible width
 * @height: visible height
 * @buf: buffer containing h.264 SPS RBSP, starting with NAL header
 * @size: modified RBSP size return value
 * @max_size: available size in buf
 *
 * Rewrites the frame cropping values in an h.264 SPS RBSP correctly for the
 * given visible width and height.
 */
int coda_h264_sps_fixup(struct coda_ctx *ctx, int width, int height, char *buf,
			int *size, int max_size)
{
	int profile_idc;
	unsigned int pic_order_cnt_type;
	int pic_width_in_mbs_minus1, pic_height_in_map_units_minus1;
	int frame_mbs_only_flag, frame_cropping_flag;
	int vui_parameters_present_flag;
	unsigned int crop_right, crop_bottom;
	struct rbsp sps;
	int pos;
	int ret;

	if (*size < 8 || *size >= max_size)
		return -EINVAL;

	sps.buf = buf + 5; /* Skip NAL header */
	sps.size = *size - 5;

	profile_idc = sps.buf[0];
	/* Skip constraint_set[0-5]_flag, reserved_zero_2bits */
	/* Skip level_idc */
	sps.pos = 24;

	/* seq_parameter_set_id */
	ret = rbsp_read_uev(&sps, NULL);
	if (ret)
		return ret;

	if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
	    profile_idc == 244 || profile_idc == 44 || profile_idc == 83 ||
	    profile_idc == 86 || profile_idc == 118 || profile_idc == 128 ||
	    profile_idc == 138 || profile_idc == 139 || profile_idc == 134 ||
	    profile_idc == 135) {
		dev_err(ctx->fh.vdev->dev_parent,
			"%s: Handling profile_idc %d not implemented\n",
			__func__, profile_idc);
		return -EINVAL;
	}

	/* log2_max_frame_num_minus4 */
	ret = rbsp_read_uev(&sps, NULL);
	if (ret)
		return ret;

	ret = rbsp_read_uev(&sps, &pic_order_cnt_type);
	if (ret)
		return ret;

	if (pic_order_cnt_type == 0) {
		/* log2_max_pic_order_cnt_lsb_minus4 */
		ret = rbsp_read_uev(&sps, NULL);
		if (ret)
			return ret;
	} else if (pic_order_cnt_type == 1) {
		unsigned int i, num_ref_frames_in_pic_order_cnt_cycle;

		/* delta_pic_order_always_zero_flag */
		ret = rbsp_read_bit(&sps);
		if (ret < 0)
			return ret;
		/* offset_for_non_ref_pic */
		ret = rbsp_read_sev(&sps, NULL);
		if (ret)
			return ret;
		/* offset_for_top_to_bottom_field */
		ret = rbsp_read_sev(&sps, NULL);
		if (ret)
			return ret;

		ret = rbsp_read_uev(&sps,
				    &num_ref_frames_in_pic_order_cnt_cycle);
		if (ret)
			return ret;
		for (i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++) {
			/* offset_for_ref_frame */
			ret = rbsp_read_sev(&sps, NULL);
			if (ret)
				return ret;
		}
	}

	/* max_num_ref_frames */
	ret = rbsp_read_uev(&sps, NULL);
	if (ret)
		return ret;

	/* gaps_in_frame_num_value_allowed_flag */
	ret = rbsp_read_bit(&sps);
	if (ret < 0)
		return ret;
	ret = rbsp_read_uev(&sps, &pic_width_in_mbs_minus1);
	if (ret)
		return ret;
	ret = rbsp_read_uev(&sps, &pic_height_in_map_units_minus1);
	if (ret)
		return ret;
	frame_mbs_only_flag = ret = rbsp_read_bit(&sps);
	if (ret < 0)
		return ret;
	if (!frame_mbs_only_flag) {
		/* mb_adaptive_frame_field_flag */
		ret = rbsp_read_bit(&sps);
		if (ret < 0)
			return ret;
	}
	/* direct_8x8_inference_flag */
	ret = rbsp_read_bit(&sps);
	if (ret < 0)
		return ret;

	/* Mark position of the frame cropping flag */
	pos = sps.pos;
	frame_cropping_flag = ret = rbsp_read_bit(&sps);
	if (ret < 0)
		return ret;
	if (frame_cropping_flag) {
		unsigned int crop_left, crop_top;

		ret = rbsp_read_uev(&sps, &crop_left);
		if (ret)
			return ret;
		ret = rbsp_read_uev(&sps, &crop_right);
		if (ret)
			return ret;
		ret = rbsp_read_uev(&sps, &crop_top);
		if (ret)
			return ret;
		ret = rbsp_read_uev(&sps, &crop_bottom);
		if (ret)
			return ret;
	}
	vui_parameters_present_flag = ret = rbsp_read_bit(&sps);
	if (ret < 0)
		return ret;
	if (vui_parameters_present_flag) {
		dev_err(ctx->fh.vdev->dev_parent,
			"%s: Handling vui_parameters not implemented\n",
			__func__);
		return -EINVAL;
	}

	crop_right = round_up(width, 16) - width;
	crop_bottom = round_up(height, 16) - height;
	crop_right /= 2;
	if (frame_mbs_only_flag)
		crop_bottom /= 2;
	else
		crop_bottom /= 4;


	sps.size = max_size - 5;
	sps.pos = pos;
	frame_cropping_flag = 1;
	ret = rbsp_write_bit(&sps, frame_cropping_flag);
	if (ret)
		return ret;
	ret = rbsp_write_uev(&sps, 0); /* crop_left */
	if (ret)
		return ret;
	ret = rbsp_write_uev(&sps, crop_right);
	if (ret)
		return ret;
	ret = rbsp_write_uev(&sps, 0); /* crop_top */
	if (ret)
		return ret;
	ret = rbsp_write_uev(&sps, crop_bottom);
	if (ret)
		return ret;
	ret = rbsp_write_bit(&sps, 0); /* vui_parameters_present_flag */
	if (ret)
		return ret;
	ret = rbsp_write_bit(&sps, 1);
	if (ret)
		return ret;

	*size = 5 + DIV_ROUND_UP(sps.pos, 8);

	return 0;
}
