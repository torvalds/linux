/*
 * cx2341x - generic code for cx23415/6/8 based devices
 *
 * Copyright (C) 2006 Hans Verkuil <hverkuil@xs4all.nl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/videodev2.h>

#include <media/tuner.h>
#include <media/drv-intf/cx2341x.h>
#include <media/v4l2-common.h>

MODULE_DESCRIPTION("cx23415/6/8 driver");
MODULE_AUTHOR("Hans Verkuil");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

/********************** COMMON CODE *********************/

/* definitions for audio properties bits 29-28 */
#define CX2341X_AUDIO_ENCODING_METHOD_MPEG	0
#define CX2341X_AUDIO_ENCODING_METHOD_AC3	1
#define CX2341X_AUDIO_ENCODING_METHOD_LPCM	2

static const char *cx2341x_get_name(u32 id)
{
	switch (id) {
	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE:
		return "Spatial Filter Mode";
	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER:
		return "Spatial Filter";
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE:
		return "Spatial Luma Filter Type";
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE:
		return "Spatial Chroma Filter Type";
	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE:
		return "Temporal Filter Mode";
	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER:
		return "Temporal Filter";
	case V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE:
		return "Median Filter Type";
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_TOP:
		return "Median Luma Filter Maximum";
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_BOTTOM:
		return "Median Luma Filter Minimum";
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_TOP:
		return "Median Chroma Filter Maximum";
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_BOTTOM:
		return "Median Chroma Filter Minimum";
	case V4L2_CID_MPEG_CX2341X_STREAM_INSERT_NAV_PACKETS:
		return "Insert Navigation Packets";
	}
	return NULL;
}

static const char **cx2341x_get_menu(u32 id)
{
	static const char *cx2341x_video_spatial_filter_mode_menu[] = {
		"Manual",
		"Auto",
		NULL
	};

	static const char *cx2341x_video_luma_spatial_filter_type_menu[] = {
		"Off",
		"1D Horizontal",
		"1D Vertical",
		"2D H/V Separable",
		"2D Symmetric non-separable",
		NULL
	};

	static const char *cx2341x_video_chroma_spatial_filter_type_menu[] = {
		"Off",
		"1D Horizontal",
		NULL
	};

	static const char *cx2341x_video_temporal_filter_mode_menu[] = {
		"Manual",
		"Auto",
		NULL
	};

	static const char *cx2341x_video_median_filter_type_menu[] = {
		"Off",
		"Horizontal",
		"Vertical",
		"Horizontal/Vertical",
		"Diagonal",
		NULL
	};

	switch (id) {
	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE:
		return cx2341x_video_spatial_filter_mode_menu;
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE:
		return cx2341x_video_luma_spatial_filter_type_menu;
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE:
		return cx2341x_video_chroma_spatial_filter_type_menu;
	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE:
		return cx2341x_video_temporal_filter_mode_menu;
	case V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE:
		return cx2341x_video_median_filter_type_menu;
	}
	return NULL;
}

static void cx2341x_ctrl_fill(u32 id, const char **name, enum v4l2_ctrl_type *type,
		    s32 *min, s32 *max, s32 *step, s32 *def, u32 *flags)
{
	*name = cx2341x_get_name(id);
	*flags = 0;

	switch (id) {
	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE:
		*type = V4L2_CTRL_TYPE_MENU;
		*min = 0;
		*step = 0;
		break;
	case V4L2_CID_MPEG_CX2341X_STREAM_INSERT_NAV_PACKETS:
		*type = V4L2_CTRL_TYPE_BOOLEAN;
		*min = 0;
		*max = *step = 1;
		break;
	default:
		*type = V4L2_CTRL_TYPE_INTEGER;
		break;
	}
	switch (id) {
	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE:
		*flags |= V4L2_CTRL_FLAG_UPDATE;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER:
	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER:
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_TOP:
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_BOTTOM:
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_TOP:
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_BOTTOM:
		*flags |= V4L2_CTRL_FLAG_SLIDER;
		break;
	case V4L2_CID_MPEG_VIDEO_ENCODING:
		*flags |= V4L2_CTRL_FLAG_READ_ONLY;
		break;
	}
}


/********************** OLD CODE *********************/

/* Must be sorted from low to high control ID! */
const u32 cx2341x_mpeg_ctrls[] = {
	V4L2_CID_MPEG_CLASS,
	V4L2_CID_MPEG_STREAM_TYPE,
	V4L2_CID_MPEG_STREAM_VBI_FMT,
	V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ,
	V4L2_CID_MPEG_AUDIO_ENCODING,
	V4L2_CID_MPEG_AUDIO_L2_BITRATE,
	V4L2_CID_MPEG_AUDIO_MODE,
	V4L2_CID_MPEG_AUDIO_MODE_EXTENSION,
	V4L2_CID_MPEG_AUDIO_EMPHASIS,
	V4L2_CID_MPEG_AUDIO_CRC,
	V4L2_CID_MPEG_AUDIO_MUTE,
	V4L2_CID_MPEG_AUDIO_AC3_BITRATE,
	V4L2_CID_MPEG_VIDEO_ENCODING,
	V4L2_CID_MPEG_VIDEO_ASPECT,
	V4L2_CID_MPEG_VIDEO_B_FRAMES,
	V4L2_CID_MPEG_VIDEO_GOP_SIZE,
	V4L2_CID_MPEG_VIDEO_GOP_CLOSURE,
	V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
	V4L2_CID_MPEG_VIDEO_BITRATE,
	V4L2_CID_MPEG_VIDEO_BITRATE_PEAK,
	V4L2_CID_MPEG_VIDEO_TEMPORAL_DECIMATION,
	V4L2_CID_MPEG_VIDEO_MUTE,
	V4L2_CID_MPEG_VIDEO_MUTE_YUV,
	V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE,
	V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER,
	V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE,
	V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE,
	V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE,
	V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER,
	V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE,
	V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_BOTTOM,
	V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_TOP,
	V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_BOTTOM,
	V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_TOP,
	V4L2_CID_MPEG_CX2341X_STREAM_INSERT_NAV_PACKETS,
	0
};
EXPORT_SYMBOL(cx2341x_mpeg_ctrls);

static const struct cx2341x_mpeg_params default_params = {
	/* misc */
	.capabilities = 0,
	.port = CX2341X_PORT_MEMORY,
	.width = 720,
	.height = 480,
	.is_50hz = 0,

	/* stream */
	.stream_type = V4L2_MPEG_STREAM_TYPE_MPEG2_PS,
	.stream_vbi_fmt = V4L2_MPEG_STREAM_VBI_FMT_NONE,
	.stream_insert_nav_packets = 0,

	/* audio */
	.audio_sampling_freq = V4L2_MPEG_AUDIO_SAMPLING_FREQ_48000,
	.audio_encoding = V4L2_MPEG_AUDIO_ENCODING_LAYER_2,
	.audio_l2_bitrate = V4L2_MPEG_AUDIO_L2_BITRATE_224K,
	.audio_ac3_bitrate = V4L2_MPEG_AUDIO_AC3_BITRATE_224K,
	.audio_mode = V4L2_MPEG_AUDIO_MODE_STEREO,
	.audio_mode_extension = V4L2_MPEG_AUDIO_MODE_EXTENSION_BOUND_4,
	.audio_emphasis = V4L2_MPEG_AUDIO_EMPHASIS_NONE,
	.audio_crc = V4L2_MPEG_AUDIO_CRC_NONE,
	.audio_mute = 0,

	/* video */
	.video_encoding = V4L2_MPEG_VIDEO_ENCODING_MPEG_2,
	.video_aspect = V4L2_MPEG_VIDEO_ASPECT_4x3,
	.video_b_frames = 2,
	.video_gop_size = 12,
	.video_gop_closure = 1,
	.video_bitrate_mode = V4L2_MPEG_VIDEO_BITRATE_MODE_VBR,
	.video_bitrate = 6000000,
	.video_bitrate_peak = 8000000,
	.video_temporal_decimation = 0,
	.video_mute = 0,
	.video_mute_yuv = 0x008080,  /* YCbCr value for black */

	/* encoding filters */
	.video_spatial_filter_mode =
		V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_MANUAL,
	.video_spatial_filter = 0,
	.video_luma_spatial_filter_type =
		V4L2_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE_1D_HOR,
	.video_chroma_spatial_filter_type =
		V4L2_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE_1D_HOR,
	.video_temporal_filter_mode =
		V4L2_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE_MANUAL,
	.video_temporal_filter = 8,
	.video_median_filter_type =
		V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_OFF,
	.video_luma_median_filter_top = 255,
	.video_luma_median_filter_bottom = 0,
	.video_chroma_median_filter_top = 255,
	.video_chroma_median_filter_bottom = 0,
};
/* Map the control ID to the correct field in the cx2341x_mpeg_params
   struct. Return -EINVAL if the ID is unknown, else return 0. */
static int cx2341x_get_ctrl(const struct cx2341x_mpeg_params *params,
		struct v4l2_ext_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ:
		ctrl->value = params->audio_sampling_freq;
		break;
	case V4L2_CID_MPEG_AUDIO_ENCODING:
		ctrl->value = params->audio_encoding;
		break;
	case V4L2_CID_MPEG_AUDIO_L2_BITRATE:
		ctrl->value = params->audio_l2_bitrate;
		break;
	case V4L2_CID_MPEG_AUDIO_AC3_BITRATE:
		ctrl->value = params->audio_ac3_bitrate;
		break;
	case V4L2_CID_MPEG_AUDIO_MODE:
		ctrl->value = params->audio_mode;
		break;
	case V4L2_CID_MPEG_AUDIO_MODE_EXTENSION:
		ctrl->value = params->audio_mode_extension;
		break;
	case V4L2_CID_MPEG_AUDIO_EMPHASIS:
		ctrl->value = params->audio_emphasis;
		break;
	case V4L2_CID_MPEG_AUDIO_CRC:
		ctrl->value = params->audio_crc;
		break;
	case V4L2_CID_MPEG_AUDIO_MUTE:
		ctrl->value = params->audio_mute;
		break;
	case V4L2_CID_MPEG_VIDEO_ENCODING:
		ctrl->value = params->video_encoding;
		break;
	case V4L2_CID_MPEG_VIDEO_ASPECT:
		ctrl->value = params->video_aspect;
		break;
	case V4L2_CID_MPEG_VIDEO_B_FRAMES:
		ctrl->value = params->video_b_frames;
		break;
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		ctrl->value = params->video_gop_size;
		break;
	case V4L2_CID_MPEG_VIDEO_GOP_CLOSURE:
		ctrl->value = params->video_gop_closure;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		ctrl->value = params->video_bitrate_mode;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		ctrl->value = params->video_bitrate;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE_PEAK:
		ctrl->value = params->video_bitrate_peak;
		break;
	case V4L2_CID_MPEG_VIDEO_TEMPORAL_DECIMATION:
		ctrl->value = params->video_temporal_decimation;
		break;
	case V4L2_CID_MPEG_VIDEO_MUTE:
		ctrl->value = params->video_mute;
		break;
	case V4L2_CID_MPEG_VIDEO_MUTE_YUV:
		ctrl->value = params->video_mute_yuv;
		break;
	case V4L2_CID_MPEG_STREAM_TYPE:
		ctrl->value = params->stream_type;
		break;
	case V4L2_CID_MPEG_STREAM_VBI_FMT:
		ctrl->value = params->stream_vbi_fmt;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE:
		ctrl->value = params->video_spatial_filter_mode;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER:
		ctrl->value = params->video_spatial_filter;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE:
		ctrl->value = params->video_luma_spatial_filter_type;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE:
		ctrl->value = params->video_chroma_spatial_filter_type;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE:
		ctrl->value = params->video_temporal_filter_mode;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER:
		ctrl->value = params->video_temporal_filter;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE:
		ctrl->value = params->video_median_filter_type;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_TOP:
		ctrl->value = params->video_luma_median_filter_top;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_BOTTOM:
		ctrl->value = params->video_luma_median_filter_bottom;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_TOP:
		ctrl->value = params->video_chroma_median_filter_top;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_BOTTOM:
		ctrl->value = params->video_chroma_median_filter_bottom;
		break;
	case V4L2_CID_MPEG_CX2341X_STREAM_INSERT_NAV_PACKETS:
		ctrl->value = params->stream_insert_nav_packets;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/* Map the control ID to the correct field in the cx2341x_mpeg_params
   struct. Return -EINVAL if the ID is unknown, else return 0. */
static int cx2341x_set_ctrl(struct cx2341x_mpeg_params *params, int busy,
		struct v4l2_ext_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ:
		if (busy)
			return -EBUSY;
		params->audio_sampling_freq = ctrl->value;
		break;
	case V4L2_CID_MPEG_AUDIO_ENCODING:
		if (busy)
			return -EBUSY;
		if (params->capabilities & CX2341X_CAP_HAS_AC3)
			if (ctrl->value != V4L2_MPEG_AUDIO_ENCODING_LAYER_2 &&
			    ctrl->value != V4L2_MPEG_AUDIO_ENCODING_AC3)
				return -ERANGE;
		params->audio_encoding = ctrl->value;
		break;
	case V4L2_CID_MPEG_AUDIO_L2_BITRATE:
		if (busy)
			return -EBUSY;
		params->audio_l2_bitrate = ctrl->value;
		break;
	case V4L2_CID_MPEG_AUDIO_AC3_BITRATE:
		if (busy)
			return -EBUSY;
		if (!(params->capabilities & CX2341X_CAP_HAS_AC3))
			return -EINVAL;
		params->audio_ac3_bitrate = ctrl->value;
		break;
	case V4L2_CID_MPEG_AUDIO_MODE:
		params->audio_mode = ctrl->value;
		break;
	case V4L2_CID_MPEG_AUDIO_MODE_EXTENSION:
		params->audio_mode_extension = ctrl->value;
		break;
	case V4L2_CID_MPEG_AUDIO_EMPHASIS:
		params->audio_emphasis = ctrl->value;
		break;
	case V4L2_CID_MPEG_AUDIO_CRC:
		params->audio_crc = ctrl->value;
		break;
	case V4L2_CID_MPEG_AUDIO_MUTE:
		params->audio_mute = ctrl->value;
		break;
	case V4L2_CID_MPEG_VIDEO_ASPECT:
		params->video_aspect = ctrl->value;
		break;
	case V4L2_CID_MPEG_VIDEO_B_FRAMES: {
		int b = ctrl->value + 1;
		int gop = params->video_gop_size;
		params->video_b_frames = ctrl->value;
		params->video_gop_size = b * ((gop + b - 1) / b);
		/* Max GOP size = 34 */
		while (params->video_gop_size > 34)
			params->video_gop_size -= b;
		break;
	}
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE: {
		int b = params->video_b_frames + 1;
		int gop = ctrl->value;
		params->video_gop_size = b * ((gop + b - 1) / b);
		/* Max GOP size = 34 */
		while (params->video_gop_size > 34)
			params->video_gop_size -= b;
		ctrl->value = params->video_gop_size;
		break;
	}
	case V4L2_CID_MPEG_VIDEO_GOP_CLOSURE:
		params->video_gop_closure = ctrl->value;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		if (busy)
			return -EBUSY;
		/* MPEG-1 only allows CBR */
		if (params->video_encoding == V4L2_MPEG_VIDEO_ENCODING_MPEG_1 &&
		    ctrl->value != V4L2_MPEG_VIDEO_BITRATE_MODE_CBR)
			return -EINVAL;
		params->video_bitrate_mode = ctrl->value;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		if (busy)
			return -EBUSY;
		params->video_bitrate = ctrl->value;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE_PEAK:
		if (busy)
			return -EBUSY;
		params->video_bitrate_peak = ctrl->value;
		break;
	case V4L2_CID_MPEG_VIDEO_TEMPORAL_DECIMATION:
		params->video_temporal_decimation = ctrl->value;
		break;
	case V4L2_CID_MPEG_VIDEO_MUTE:
		params->video_mute = (ctrl->value != 0);
		break;
	case V4L2_CID_MPEG_VIDEO_MUTE_YUV:
		params->video_mute_yuv = ctrl->value;
		break;
	case V4L2_CID_MPEG_STREAM_TYPE:
		if (busy)
			return -EBUSY;
		params->stream_type = ctrl->value;
		params->video_encoding =
		    (params->stream_type == V4L2_MPEG_STREAM_TYPE_MPEG1_SS ||
		     params->stream_type == V4L2_MPEG_STREAM_TYPE_MPEG1_VCD) ?
			V4L2_MPEG_VIDEO_ENCODING_MPEG_1 :
			V4L2_MPEG_VIDEO_ENCODING_MPEG_2;
		if (params->video_encoding == V4L2_MPEG_VIDEO_ENCODING_MPEG_1)
			/* MPEG-1 implies CBR */
			params->video_bitrate_mode =
				V4L2_MPEG_VIDEO_BITRATE_MODE_CBR;
		break;
	case V4L2_CID_MPEG_STREAM_VBI_FMT:
		params->stream_vbi_fmt = ctrl->value;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE:
		params->video_spatial_filter_mode = ctrl->value;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER:
		params->video_spatial_filter = ctrl->value;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE:
		params->video_luma_spatial_filter_type = ctrl->value;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE:
		params->video_chroma_spatial_filter_type = ctrl->value;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE:
		params->video_temporal_filter_mode = ctrl->value;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER:
		params->video_temporal_filter = ctrl->value;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE:
		params->video_median_filter_type = ctrl->value;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_TOP:
		params->video_luma_median_filter_top = ctrl->value;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_BOTTOM:
		params->video_luma_median_filter_bottom = ctrl->value;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_TOP:
		params->video_chroma_median_filter_top = ctrl->value;
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_BOTTOM:
		params->video_chroma_median_filter_bottom = ctrl->value;
		break;
	case V4L2_CID_MPEG_CX2341X_STREAM_INSERT_NAV_PACKETS:
		params->stream_insert_nav_packets = ctrl->value;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int cx2341x_ctrl_query_fill(struct v4l2_queryctrl *qctrl,
				   s32 min, s32 max, s32 step, s32 def)
{
	const char *name;

	switch (qctrl->id) {
	/* MPEG controls */
	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER:
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER:
	case V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_TOP:
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_BOTTOM:
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_TOP:
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_BOTTOM:
	case V4L2_CID_MPEG_CX2341X_STREAM_INSERT_NAV_PACKETS:
		cx2341x_ctrl_fill(qctrl->id, &name, &qctrl->type,
				&min, &max, &step, &def, &qctrl->flags);
		qctrl->minimum = min;
		qctrl->maximum = max;
		qctrl->step = step;
		qctrl->default_value = def;
		qctrl->reserved[0] = qctrl->reserved[1] = 0;
		strlcpy(qctrl->name, name, sizeof(qctrl->name));
		return 0;

	default:
		return v4l2_ctrl_query_fill(qctrl, min, max, step, def);
	}
}

int cx2341x_ctrl_query(const struct cx2341x_mpeg_params *params,
		       struct v4l2_queryctrl *qctrl)
{
	int err;

	switch (qctrl->id) {
	case V4L2_CID_MPEG_CLASS:
		return v4l2_ctrl_query_fill(qctrl, 0, 0, 0, 0);
	case V4L2_CID_MPEG_STREAM_TYPE:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_STREAM_TYPE_MPEG2_PS,
				V4L2_MPEG_STREAM_TYPE_MPEG2_SVCD, 1,
				V4L2_MPEG_STREAM_TYPE_MPEG2_PS);

	case V4L2_CID_MPEG_STREAM_VBI_FMT:
		if (params->capabilities & CX2341X_CAP_HAS_SLICED_VBI)
			return v4l2_ctrl_query_fill(qctrl,
					V4L2_MPEG_STREAM_VBI_FMT_NONE,
					V4L2_MPEG_STREAM_VBI_FMT_IVTV, 1,
					V4L2_MPEG_STREAM_VBI_FMT_NONE);
		return cx2341x_ctrl_query_fill(qctrl,
				V4L2_MPEG_STREAM_VBI_FMT_NONE,
				V4L2_MPEG_STREAM_VBI_FMT_NONE, 1,
				default_params.stream_vbi_fmt);

	case V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_AUDIO_SAMPLING_FREQ_44100,
				V4L2_MPEG_AUDIO_SAMPLING_FREQ_32000, 1,
				V4L2_MPEG_AUDIO_SAMPLING_FREQ_48000);

	case V4L2_CID_MPEG_AUDIO_ENCODING:
		if (params->capabilities & CX2341X_CAP_HAS_AC3) {
			/*
			 * The state of L2 & AC3 bitrate controls can change
			 * when this control changes, but v4l2_ctrl_query_fill()
			 * already sets V4L2_CTRL_FLAG_UPDATE for
			 * V4L2_CID_MPEG_AUDIO_ENCODING, so we don't here.
			 */
			return v4l2_ctrl_query_fill(qctrl,
					V4L2_MPEG_AUDIO_ENCODING_LAYER_2,
					V4L2_MPEG_AUDIO_ENCODING_AC3, 1,
					default_params.audio_encoding);
		}

		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_AUDIO_ENCODING_LAYER_2,
				V4L2_MPEG_AUDIO_ENCODING_LAYER_2, 1,
				default_params.audio_encoding);

	case V4L2_CID_MPEG_AUDIO_L2_BITRATE:
		err = v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_AUDIO_L2_BITRATE_192K,
				V4L2_MPEG_AUDIO_L2_BITRATE_384K, 1,
				default_params.audio_l2_bitrate);
		if (err)
			return err;
		if (params->capabilities & CX2341X_CAP_HAS_AC3 &&
		    params->audio_encoding != V4L2_MPEG_AUDIO_ENCODING_LAYER_2)
			qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return 0;

	case V4L2_CID_MPEG_AUDIO_MODE:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_AUDIO_MODE_STEREO,
				V4L2_MPEG_AUDIO_MODE_MONO, 1,
				V4L2_MPEG_AUDIO_MODE_STEREO);

	case V4L2_CID_MPEG_AUDIO_MODE_EXTENSION:
		err = v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_AUDIO_MODE_EXTENSION_BOUND_4,
				V4L2_MPEG_AUDIO_MODE_EXTENSION_BOUND_16, 1,
				V4L2_MPEG_AUDIO_MODE_EXTENSION_BOUND_4);
		if (err == 0 &&
		    params->audio_mode != V4L2_MPEG_AUDIO_MODE_JOINT_STEREO)
			qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return err;

	case V4L2_CID_MPEG_AUDIO_EMPHASIS:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_AUDIO_EMPHASIS_NONE,
				V4L2_MPEG_AUDIO_EMPHASIS_CCITT_J17, 1,
				V4L2_MPEG_AUDIO_EMPHASIS_NONE);

	case V4L2_CID_MPEG_AUDIO_CRC:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_AUDIO_CRC_NONE,
				V4L2_MPEG_AUDIO_CRC_CRC16, 1,
				V4L2_MPEG_AUDIO_CRC_NONE);

	case V4L2_CID_MPEG_AUDIO_MUTE:
		return v4l2_ctrl_query_fill(qctrl, 0, 1, 1, 0);

	case V4L2_CID_MPEG_AUDIO_AC3_BITRATE:
		err = v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_AUDIO_AC3_BITRATE_48K,
				V4L2_MPEG_AUDIO_AC3_BITRATE_448K, 1,
				default_params.audio_ac3_bitrate);
		if (err)
			return err;
		if (params->capabilities & CX2341X_CAP_HAS_AC3) {
			if (params->audio_encoding !=
						   V4L2_MPEG_AUDIO_ENCODING_AC3)
				qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		} else
			qctrl->flags |= V4L2_CTRL_FLAG_DISABLED;
		return 0;

	case V4L2_CID_MPEG_VIDEO_ENCODING:
		/* this setting is read-only for the cx2341x since the
		   V4L2_CID_MPEG_STREAM_TYPE really determines the
		   MPEG-1/2 setting */
		err = v4l2_ctrl_query_fill(qctrl,
					   V4L2_MPEG_VIDEO_ENCODING_MPEG_1,
					   V4L2_MPEG_VIDEO_ENCODING_MPEG_2, 1,
					   V4L2_MPEG_VIDEO_ENCODING_MPEG_2);
		if (err == 0)
			qctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
		return err;

	case V4L2_CID_MPEG_VIDEO_ASPECT:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_VIDEO_ASPECT_1x1,
				V4L2_MPEG_VIDEO_ASPECT_221x100, 1,
				V4L2_MPEG_VIDEO_ASPECT_4x3);

	case V4L2_CID_MPEG_VIDEO_B_FRAMES:
		return v4l2_ctrl_query_fill(qctrl, 0, 33, 1, 2);

	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		return v4l2_ctrl_query_fill(qctrl, 1, 34, 1,
				params->is_50hz ? 12 : 15);

	case V4L2_CID_MPEG_VIDEO_GOP_CLOSURE:
		return v4l2_ctrl_query_fill(qctrl, 0, 1, 1, 1);

	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		err = v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_VIDEO_BITRATE_MODE_VBR,
				V4L2_MPEG_VIDEO_BITRATE_MODE_CBR, 1,
				V4L2_MPEG_VIDEO_BITRATE_MODE_VBR);
		if (err == 0 &&
		    params->video_encoding == V4L2_MPEG_VIDEO_ENCODING_MPEG_1)
			qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return err;

	case V4L2_CID_MPEG_VIDEO_BITRATE:
		return v4l2_ctrl_query_fill(qctrl, 0, 27000000, 1, 6000000);

	case V4L2_CID_MPEG_VIDEO_BITRATE_PEAK:
		err = v4l2_ctrl_query_fill(qctrl, 0, 27000000, 1, 8000000);
		if (err == 0 &&
		    params->video_bitrate_mode ==
				V4L2_MPEG_VIDEO_BITRATE_MODE_CBR)
			qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return err;

	case V4L2_CID_MPEG_VIDEO_TEMPORAL_DECIMATION:
		return v4l2_ctrl_query_fill(qctrl, 0, 255, 1, 0);

	case V4L2_CID_MPEG_VIDEO_MUTE:
		return v4l2_ctrl_query_fill(qctrl, 0, 1, 1, 0);

	case V4L2_CID_MPEG_VIDEO_MUTE_YUV:  /* Init YUV (really YCbCr) to black */
		return v4l2_ctrl_query_fill(qctrl, 0, 0xffffff, 1, 0x008080);

	/* CX23415/6 specific */
	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE:
		return cx2341x_ctrl_query_fill(qctrl,
			V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_MANUAL,
			V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_AUTO, 1,
			default_params.video_spatial_filter_mode);

	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER:
		cx2341x_ctrl_query_fill(qctrl, 0, 15, 1,
				default_params.video_spatial_filter);
		qctrl->flags |= V4L2_CTRL_FLAG_SLIDER;
		if (params->video_spatial_filter_mode ==
			    V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_AUTO)
			qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return 0;

	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE:
		cx2341x_ctrl_query_fill(qctrl,
			V4L2_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE_OFF,
			V4L2_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE_2D_SYM_NON_SEPARABLE,
			1,
			default_params.video_luma_spatial_filter_type);
		if (params->video_spatial_filter_mode ==
			    V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_AUTO)
			qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return 0;

	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE:
		cx2341x_ctrl_query_fill(qctrl,
		    V4L2_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE_OFF,
		    V4L2_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE_1D_HOR,
		    1,
		    default_params.video_chroma_spatial_filter_type);
		if (params->video_spatial_filter_mode ==
			V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_AUTO)
			qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return 0;

	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE:
		return cx2341x_ctrl_query_fill(qctrl,
			V4L2_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE_MANUAL,
			V4L2_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE_AUTO, 1,
			default_params.video_temporal_filter_mode);

	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER:
		cx2341x_ctrl_query_fill(qctrl, 0, 31, 1,
				default_params.video_temporal_filter);
		qctrl->flags |= V4L2_CTRL_FLAG_SLIDER;
		if (params->video_temporal_filter_mode ==
			V4L2_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE_AUTO)
			qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return 0;

	case V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE:
		return cx2341x_ctrl_query_fill(qctrl,
			V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_OFF,
			V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_DIAG, 1,
			default_params.video_median_filter_type);

	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_TOP:
		cx2341x_ctrl_query_fill(qctrl, 0, 255, 1,
				default_params.video_luma_median_filter_top);
		qctrl->flags |= V4L2_CTRL_FLAG_SLIDER;
		if (params->video_median_filter_type ==
				V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_OFF)
			qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return 0;

	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_BOTTOM:
		cx2341x_ctrl_query_fill(qctrl, 0, 255, 1,
				default_params.video_luma_median_filter_bottom);
		qctrl->flags |= V4L2_CTRL_FLAG_SLIDER;
		if (params->video_median_filter_type ==
				V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_OFF)
			qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return 0;

	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_TOP:
		cx2341x_ctrl_query_fill(qctrl, 0, 255, 1,
				default_params.video_chroma_median_filter_top);
		qctrl->flags |= V4L2_CTRL_FLAG_SLIDER;
		if (params->video_median_filter_type ==
				V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_OFF)
			qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return 0;

	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_BOTTOM:
		cx2341x_ctrl_query_fill(qctrl, 0, 255, 1,
			default_params.video_chroma_median_filter_bottom);
		qctrl->flags |= V4L2_CTRL_FLAG_SLIDER;
		if (params->video_median_filter_type ==
				V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_OFF)
			qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return 0;

	case V4L2_CID_MPEG_CX2341X_STREAM_INSERT_NAV_PACKETS:
		return cx2341x_ctrl_query_fill(qctrl, 0, 1, 1,
				default_params.stream_insert_nav_packets);

	default:
		return -EINVAL;

	}
}
EXPORT_SYMBOL(cx2341x_ctrl_query);

const char * const *cx2341x_ctrl_get_menu(const struct cx2341x_mpeg_params *p, u32 id)
{
	static const char * const mpeg_stream_type_without_ts[] = {
		"MPEG-2 Program Stream",
		"",
		"MPEG-1 System Stream",
		"MPEG-2 DVD-compatible Stream",
		"MPEG-1 VCD-compatible Stream",
		"MPEG-2 SVCD-compatible Stream",
		NULL
	};

	static const char *mpeg_stream_type_with_ts[] = {
		"MPEG-2 Program Stream",
		"MPEG-2 Transport Stream",
		"MPEG-1 System Stream",
		"MPEG-2 DVD-compatible Stream",
		"MPEG-1 VCD-compatible Stream",
		"MPEG-2 SVCD-compatible Stream",
		NULL
	};

	static const char *mpeg_audio_encoding_l2_ac3[] = {
		"",
		"MPEG-1/2 Layer II",
		"",
		"",
		"AC-3",
		NULL
	};

	switch (id) {
	case V4L2_CID_MPEG_STREAM_TYPE:
		return (p->capabilities & CX2341X_CAP_HAS_TS) ?
			mpeg_stream_type_with_ts : mpeg_stream_type_without_ts;
	case V4L2_CID_MPEG_AUDIO_ENCODING:
		return (p->capabilities & CX2341X_CAP_HAS_AC3) ?
			mpeg_audio_encoding_l2_ac3 : v4l2_ctrl_get_menu(id);
	case V4L2_CID_MPEG_AUDIO_L1_BITRATE:
	case V4L2_CID_MPEG_AUDIO_L3_BITRATE:
		return NULL;
	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE:
		return cx2341x_get_menu(id);
	default:
		return v4l2_ctrl_get_menu(id);
	}
}
EXPORT_SYMBOL(cx2341x_ctrl_get_menu);

static void cx2341x_calc_audio_properties(struct cx2341x_mpeg_params *params)
{
	params->audio_properties =
		(params->audio_sampling_freq << 0) |
		(params->audio_mode << 8) |
		(params->audio_mode_extension << 10) |
		(((params->audio_emphasis == V4L2_MPEG_AUDIO_EMPHASIS_CCITT_J17)
		  ? 3 : params->audio_emphasis) << 12) |
		(params->audio_crc << 14);

	if ((params->capabilities & CX2341X_CAP_HAS_AC3) &&
	    params->audio_encoding == V4L2_MPEG_AUDIO_ENCODING_AC3) {
		params->audio_properties |=
			/* Not sure if this MPEG Layer II setting is required */
			((3 - V4L2_MPEG_AUDIO_ENCODING_LAYER_2) << 2) |
			(params->audio_ac3_bitrate << 4) |
			(CX2341X_AUDIO_ENCODING_METHOD_AC3 << 28);
	} else {
		/* Assuming MPEG Layer II */
		params->audio_properties |=
			((3 - params->audio_encoding) << 2) |
			((1 + params->audio_l2_bitrate) << 4);
	}
}

/* Check for correctness of the ctrl's value based on the data from
   struct v4l2_queryctrl and the available menu items. Note that
   menu_items may be NULL, in that case it is ignored. */
static int v4l2_ctrl_check(struct v4l2_ext_control *ctrl, struct v4l2_queryctrl *qctrl,
		const char * const *menu_items)
{
	if (qctrl->flags & V4L2_CTRL_FLAG_DISABLED)
		return -EINVAL;
	if (qctrl->flags & V4L2_CTRL_FLAG_GRABBED)
		return -EBUSY;
	if (qctrl->type == V4L2_CTRL_TYPE_STRING)
		return 0;
	if (qctrl->type == V4L2_CTRL_TYPE_BUTTON ||
	    qctrl->type == V4L2_CTRL_TYPE_INTEGER64 ||
	    qctrl->type == V4L2_CTRL_TYPE_CTRL_CLASS)
		return 0;
	if (ctrl->value < qctrl->minimum || ctrl->value > qctrl->maximum)
		return -ERANGE;
	if (qctrl->type == V4L2_CTRL_TYPE_MENU && menu_items != NULL) {
		if (menu_items[ctrl->value] == NULL ||
		    menu_items[ctrl->value][0] == '\0')
			return -EINVAL;
	}
	if (qctrl->type == V4L2_CTRL_TYPE_BITMASK &&
			(ctrl->value & ~qctrl->maximum))
		return -ERANGE;
	return 0;
}

int cx2341x_ext_ctrls(struct cx2341x_mpeg_params *params, int busy,
		  struct v4l2_ext_controls *ctrls, unsigned int cmd)
{
	int err = 0;
	int i;

	if (cmd == VIDIOC_G_EXT_CTRLS) {
		for (i = 0; i < ctrls->count; i++) {
			struct v4l2_ext_control *ctrl = ctrls->controls + i;

			err = cx2341x_get_ctrl(params, ctrl);
			if (err) {
				ctrls->error_idx = i;
				break;
			}
		}
		return err;
	}
	for (i = 0; i < ctrls->count; i++) {
		struct v4l2_ext_control *ctrl = ctrls->controls + i;
		struct v4l2_queryctrl qctrl;
		const char * const *menu_items = NULL;

		qctrl.id = ctrl->id;
		err = cx2341x_ctrl_query(params, &qctrl);
		if (err)
			break;
		if (qctrl.type == V4L2_CTRL_TYPE_MENU)
			menu_items = cx2341x_ctrl_get_menu(params, qctrl.id);
		err = v4l2_ctrl_check(ctrl, &qctrl, menu_items);
		if (err)
			break;
		err = cx2341x_set_ctrl(params, busy, ctrl);
		if (err)
			break;
	}
	if (err == 0 &&
	    params->video_bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR &&
	    params->video_bitrate_peak < params->video_bitrate) {
		err = -ERANGE;
		ctrls->error_idx = ctrls->count;
	}
	if (err)
		ctrls->error_idx = i;
	else
		cx2341x_calc_audio_properties(params);
	return err;
}
EXPORT_SYMBOL(cx2341x_ext_ctrls);

void cx2341x_fill_defaults(struct cx2341x_mpeg_params *p)
{
	*p = default_params;
	cx2341x_calc_audio_properties(p);
}
EXPORT_SYMBOL(cx2341x_fill_defaults);

static int cx2341x_api(void *priv, cx2341x_mbox_func func,
		       u32 cmd, int args, ...)
{
	u32 data[CX2341X_MBOX_MAX_DATA];
	va_list vargs;
	int i;

	va_start(vargs, args);

	for (i = 0; i < args; i++)
		data[i] = va_arg(vargs, int);
	va_end(vargs);
	return func(priv, cmd, args, 0, data);
}

#define NEQ(field) (old->field != new->field)

int cx2341x_update(void *priv, cx2341x_mbox_func func,
		   const struct cx2341x_mpeg_params *old,
		   const struct cx2341x_mpeg_params *new)
{
	static int mpeg_stream_type[] = {
		0,	/* MPEG-2 PS */
		1,	/* MPEG-2 TS */
		2,	/* MPEG-1 SS */
		14,	/* DVD */
		11,	/* VCD */
		12,	/* SVCD */
	};

	int err = 0;
	int force = (old == NULL);
	u16 temporal = new->video_temporal_filter;

	cx2341x_api(priv, func, CX2341X_ENC_SET_OUTPUT_PORT, 2, new->port, 0);

	if (force || NEQ(is_50hz)) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_FRAME_RATE, 1,
				  new->is_50hz);
		if (err) return err;
	}

	if (force || NEQ(width) || NEQ(height) || NEQ(video_encoding)) {
		u16 w = new->width;
		u16 h = new->height;

		if (new->video_encoding == V4L2_MPEG_VIDEO_ENCODING_MPEG_1) {
			w /= 2;
			h /= 2;
		}
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_FRAME_SIZE, 2,
				  h, w);
		if (err) return err;
	}
	if (force || NEQ(stream_type)) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_STREAM_TYPE, 1,
				  mpeg_stream_type[new->stream_type]);
		if (err) return err;
	}
	if (force || NEQ(video_aspect)) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_ASPECT_RATIO, 1,
				  1 + new->video_aspect);
		if (err) return err;
	}
	if (force || NEQ(video_b_frames) || NEQ(video_gop_size)) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_GOP_PROPERTIES, 2,
				new->video_gop_size, new->video_b_frames + 1);
		if (err) return err;
	}
	if (force || NEQ(video_gop_closure)) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_GOP_CLOSURE, 1,
				  new->video_gop_closure);
		if (err) return err;
	}
	if (force || NEQ(audio_properties)) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_AUDIO_PROPERTIES,
				  1, new->audio_properties);
		if (err) return err;
	}
	if (force || NEQ(audio_mute)) {
		err = cx2341x_api(priv, func, CX2341X_ENC_MUTE_AUDIO, 1,
				  new->audio_mute);
		if (err) return err;
	}
	if (force || NEQ(video_bitrate_mode) || NEQ(video_bitrate) ||
						NEQ(video_bitrate_peak)) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_BIT_RATE, 5,
				new->video_bitrate_mode, new->video_bitrate,
				new->video_bitrate_peak / 400, 0, 0);
		if (err) return err;
	}
	if (force || NEQ(video_spatial_filter_mode) ||
		     NEQ(video_temporal_filter_mode) ||
		     NEQ(video_median_filter_type)) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_DNR_FILTER_MODE,
				  2, new->video_spatial_filter_mode |
					(new->video_temporal_filter_mode << 1),
				new->video_median_filter_type);
		if (err) return err;
	}
	if (force || NEQ(video_luma_median_filter_bottom) ||
		     NEQ(video_luma_median_filter_top) ||
		     NEQ(video_chroma_median_filter_bottom) ||
		     NEQ(video_chroma_median_filter_top)) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_CORING_LEVELS, 4,
				new->video_luma_median_filter_bottom,
				new->video_luma_median_filter_top,
				new->video_chroma_median_filter_bottom,
				new->video_chroma_median_filter_top);
		if (err) return err;
	}
	if (force || NEQ(video_luma_spatial_filter_type) ||
		     NEQ(video_chroma_spatial_filter_type)) {
		err = cx2341x_api(priv, func,
				  CX2341X_ENC_SET_SPATIAL_FILTER_TYPE,
				  2, new->video_luma_spatial_filter_type,
				  new->video_chroma_spatial_filter_type);
		if (err) return err;
	}
	if (force || NEQ(video_spatial_filter) ||
		     old->video_temporal_filter != temporal) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_DNR_FILTER_PROPS,
				  2, new->video_spatial_filter, temporal);
		if (err) return err;
	}
	if (force || NEQ(video_temporal_decimation)) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_FRAME_DROP_RATE,
				  1, new->video_temporal_decimation);
		if (err) return err;
	}
	if (force || NEQ(video_mute) ||
		(new->video_mute && NEQ(video_mute_yuv))) {
		err = cx2341x_api(priv, func, CX2341X_ENC_MUTE_VIDEO, 1,
				new->video_mute | (new->video_mute_yuv << 8));
		if (err) return err;
	}
	if (force || NEQ(stream_insert_nav_packets)) {
		err = cx2341x_api(priv, func, CX2341X_ENC_MISC, 2,
				7, new->stream_insert_nav_packets);
		if (err) return err;
	}
	return 0;
}
EXPORT_SYMBOL(cx2341x_update);

static const char *cx2341x_menu_item(const struct cx2341x_mpeg_params *p, u32 id)
{
	const char * const *menu = cx2341x_ctrl_get_menu(p, id);
	struct v4l2_ext_control ctrl;

	if (menu == NULL)
		goto invalid;
	ctrl.id = id;
	if (cx2341x_get_ctrl(p, &ctrl))
		goto invalid;
	while (ctrl.value-- && *menu) menu++;
	if (*menu == NULL)
		goto invalid;
	return *menu;

invalid:
	return "<invalid>";
}

void cx2341x_log_status(const struct cx2341x_mpeg_params *p, const char *prefix)
{
	int is_mpeg1 = p->video_encoding == V4L2_MPEG_VIDEO_ENCODING_MPEG_1;

	/* Stream */
	printk(KERN_INFO "%s: Stream: %s",
		prefix,
		cx2341x_menu_item(p, V4L2_CID_MPEG_STREAM_TYPE));
	if (p->stream_insert_nav_packets)
		printk(KERN_CONT " (with navigation packets)");
	printk(KERN_CONT "\n");
	printk(KERN_INFO "%s: VBI Format: %s\n",
		prefix,
		cx2341x_menu_item(p, V4L2_CID_MPEG_STREAM_VBI_FMT));

	/* Video */
	printk(KERN_INFO "%s: Video:  %dx%d, %d fps%s\n",
		prefix,
		p->width / (is_mpeg1 ? 2 : 1), p->height / (is_mpeg1 ? 2 : 1),
		p->is_50hz ? 25 : 30,
		(p->video_mute) ? " (muted)" : "");
	printk(KERN_INFO "%s: Video:  %s, %s, %s, %d",
		prefix,
		cx2341x_menu_item(p, V4L2_CID_MPEG_VIDEO_ENCODING),
		cx2341x_menu_item(p, V4L2_CID_MPEG_VIDEO_ASPECT),
		cx2341x_menu_item(p, V4L2_CID_MPEG_VIDEO_BITRATE_MODE),
		p->video_bitrate);
	if (p->video_bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR)
		printk(KERN_CONT ", Peak %d", p->video_bitrate_peak);
	printk(KERN_CONT "\n");
	printk(KERN_INFO
		"%s: Video:  GOP Size %d, %d B-Frames, %sGOP Closure\n",
		prefix,
		p->video_gop_size, p->video_b_frames,
		p->video_gop_closure ? "" : "No ");
	if (p->video_temporal_decimation)
		printk(KERN_INFO "%s: Video: Temporal Decimation %d\n",
			prefix, p->video_temporal_decimation);

	/* Audio */
	printk(KERN_INFO "%s: Audio:  %s, %s, %s, %s%s",
		prefix,
		cx2341x_menu_item(p, V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ),
		cx2341x_menu_item(p, V4L2_CID_MPEG_AUDIO_ENCODING),
		cx2341x_menu_item(p,
			   p->audio_encoding == V4L2_MPEG_AUDIO_ENCODING_AC3
					      ? V4L2_CID_MPEG_AUDIO_AC3_BITRATE
					      : V4L2_CID_MPEG_AUDIO_L2_BITRATE),
		cx2341x_menu_item(p, V4L2_CID_MPEG_AUDIO_MODE),
		p->audio_mute ? " (muted)" : "");
	if (p->audio_mode == V4L2_MPEG_AUDIO_MODE_JOINT_STEREO)
		printk(KERN_CONT ", %s", cx2341x_menu_item(p,
				V4L2_CID_MPEG_AUDIO_MODE_EXTENSION));
	printk(KERN_CONT ", %s, %s\n",
		cx2341x_menu_item(p, V4L2_CID_MPEG_AUDIO_EMPHASIS),
		cx2341x_menu_item(p, V4L2_CID_MPEG_AUDIO_CRC));

	/* Encoding filters */
	printk(KERN_INFO "%s: Spatial Filter:  %s, Luma %s, Chroma %s, %d\n",
		prefix,
		cx2341x_menu_item(p,
		    V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE),
		cx2341x_menu_item(p,
		    V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE),
		cx2341x_menu_item(p,
		    V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE),
		p->video_spatial_filter);

	printk(KERN_INFO "%s: Temporal Filter: %s, %d\n",
		prefix,
		cx2341x_menu_item(p,
			V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE),
		p->video_temporal_filter);
	printk(KERN_INFO
		"%s: Median Filter:   %s, Luma [%d, %d], Chroma [%d, %d]\n",
		prefix,
		cx2341x_menu_item(p,
			V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE),
		p->video_luma_median_filter_bottom,
		p->video_luma_median_filter_top,
		p->video_chroma_median_filter_bottom,
		p->video_chroma_median_filter_top);
}
EXPORT_SYMBOL(cx2341x_log_status);



/********************** NEW CODE *********************/

static inline struct cx2341x_handler *to_cxhdl(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct cx2341x_handler, hdl);
}

static int cx2341x_hdl_api(struct cx2341x_handler *hdl,
		       u32 cmd, int args, ...)
{
	u32 data[CX2341X_MBOX_MAX_DATA];
	va_list vargs;
	int i;

	va_start(vargs, args);

	for (i = 0; i < args; i++)
		data[i] = va_arg(vargs, int);
	va_end(vargs);
	return hdl->func(hdl->priv, cmd, args, 0, data);
}

/* ctrl->handler->lock is held, so it is safe to access cur.val */
static inline int cx2341x_neq(struct v4l2_ctrl *ctrl)
{
	return ctrl && ctrl->val != ctrl->cur.val;
}

static int cx2341x_try_ctrl(struct v4l2_ctrl *ctrl)
{
	struct cx2341x_handler *hdl = to_cxhdl(ctrl);
	s32 val = ctrl->val;

	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_B_FRAMES: {
		/* video gop cluster */
		int b = val + 1;
		int gop = hdl->video_gop_size->val;

		gop = b * ((gop + b - 1) / b);

		/* Max GOP size = 34 */
		while (gop > 34)
			gop -= b;
		hdl->video_gop_size->val = gop;
		break;
	}

	case V4L2_CID_MPEG_STREAM_TYPE:
		/* stream type cluster */
		hdl->video_encoding->val =
		    (hdl->stream_type->val == V4L2_MPEG_STREAM_TYPE_MPEG1_SS ||
		     hdl->stream_type->val == V4L2_MPEG_STREAM_TYPE_MPEG1_VCD) ?
			V4L2_MPEG_VIDEO_ENCODING_MPEG_1 :
			V4L2_MPEG_VIDEO_ENCODING_MPEG_2;
		if (hdl->video_encoding->val == V4L2_MPEG_VIDEO_ENCODING_MPEG_1)
			/* MPEG-1 implies CBR */
			hdl->video_bitrate_mode->val =
				V4L2_MPEG_VIDEO_BITRATE_MODE_CBR;
		/* peak bitrate shall be >= normal bitrate */
		if (hdl->video_bitrate_mode->val == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR &&
		    hdl->video_bitrate_peak->val < hdl->video_bitrate->val)
			hdl->video_bitrate_peak->val = hdl->video_bitrate->val;
		break;
	}
	return 0;
}

static int cx2341x_s_ctrl(struct v4l2_ctrl *ctrl)
{
	static const int mpeg_stream_type[] = {
		0,	/* MPEG-2 PS */
		1,	/* MPEG-2 TS */
		2,	/* MPEG-1 SS */
		14,	/* DVD */
		11,	/* VCD */
		12,	/* SVCD */
	};
	struct cx2341x_handler *hdl = to_cxhdl(ctrl);
	s32 val = ctrl->val;
	u32 props;
	int err;

	switch (ctrl->id) {
	case V4L2_CID_MPEG_STREAM_VBI_FMT:
		if (hdl->ops && hdl->ops->s_stream_vbi_fmt)
			return hdl->ops->s_stream_vbi_fmt(hdl, val);
		return 0;

	case V4L2_CID_MPEG_VIDEO_ASPECT:
		return cx2341x_hdl_api(hdl,
			CX2341X_ENC_SET_ASPECT_RATIO, 1, val + 1);

	case V4L2_CID_MPEG_VIDEO_GOP_CLOSURE:
		return cx2341x_hdl_api(hdl, CX2341X_ENC_SET_GOP_CLOSURE, 1, val);

	case V4L2_CID_MPEG_AUDIO_MUTE:
		return cx2341x_hdl_api(hdl, CX2341X_ENC_MUTE_AUDIO, 1, val);

	case V4L2_CID_MPEG_VIDEO_TEMPORAL_DECIMATION:
		return cx2341x_hdl_api(hdl,
			CX2341X_ENC_SET_FRAME_DROP_RATE, 1, val);

	case V4L2_CID_MPEG_CX2341X_STREAM_INSERT_NAV_PACKETS:
		return cx2341x_hdl_api(hdl, CX2341X_ENC_MISC, 2, 7, val);

	case V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ:
		/* audio properties cluster */
		props = (hdl->audio_sampling_freq->val << 0) |
			(hdl->audio_mode->val << 8) |
			(hdl->audio_mode_extension->val << 10) |
			(hdl->audio_crc->val << 14);
		if (hdl->audio_emphasis->val == V4L2_MPEG_AUDIO_EMPHASIS_CCITT_J17)
			props |= 3 << 12;
		else
			props |= hdl->audio_emphasis->val << 12;

		if (hdl->audio_encoding->val == V4L2_MPEG_AUDIO_ENCODING_AC3) {
			props |=
#if 1
				/* Not sure if this MPEG Layer II setting is required */
				((3 - V4L2_MPEG_AUDIO_ENCODING_LAYER_2) << 2) |
#endif
				(hdl->audio_ac3_bitrate->val << 4) |
				(CX2341X_AUDIO_ENCODING_METHOD_AC3 << 28);
		} else {
			/* Assuming MPEG Layer II */
			props |=
				((3 - hdl->audio_encoding->val) << 2) |
				((1 + hdl->audio_l2_bitrate->val) << 4);
		}
		err = cx2341x_hdl_api(hdl,
				CX2341X_ENC_SET_AUDIO_PROPERTIES, 1, props);
		if (err)
			return err;

		hdl->audio_properties = props;
		if (hdl->audio_ac3_bitrate) {
			int is_ac3 = hdl->audio_encoding->val ==
						V4L2_MPEG_AUDIO_ENCODING_AC3;

			v4l2_ctrl_activate(hdl->audio_ac3_bitrate, is_ac3);
			v4l2_ctrl_activate(hdl->audio_l2_bitrate, !is_ac3);
		}
		v4l2_ctrl_activate(hdl->audio_mode_extension,
			hdl->audio_mode->val == V4L2_MPEG_AUDIO_MODE_JOINT_STEREO);
		if (cx2341x_neq(hdl->audio_sampling_freq) &&
		    hdl->ops && hdl->ops->s_audio_sampling_freq)
			return hdl->ops->s_audio_sampling_freq(hdl, hdl->audio_sampling_freq->val);
		if (cx2341x_neq(hdl->audio_mode) &&
		    hdl->ops && hdl->ops->s_audio_mode)
			return hdl->ops->s_audio_mode(hdl, hdl->audio_mode->val);
		return 0;

	case V4L2_CID_MPEG_VIDEO_B_FRAMES:
		/* video gop cluster */
		return cx2341x_hdl_api(hdl, CX2341X_ENC_SET_GOP_PROPERTIES, 2,
				hdl->video_gop_size->val,
				hdl->video_b_frames->val + 1);

	case V4L2_CID_MPEG_STREAM_TYPE:
		/* stream type cluster */
		err = cx2341x_hdl_api(hdl,
			CX2341X_ENC_SET_STREAM_TYPE, 1, mpeg_stream_type[val]);
		if (err)
			return err;

		err = cx2341x_hdl_api(hdl, CX2341X_ENC_SET_BIT_RATE, 5,
				hdl->video_bitrate_mode->val,
				hdl->video_bitrate->val,
				hdl->video_bitrate_peak->val / 400, 0, 0);
		if (err)
			return err;

		v4l2_ctrl_activate(hdl->video_bitrate_mode,
			hdl->video_encoding->val != V4L2_MPEG_VIDEO_ENCODING_MPEG_1);
		v4l2_ctrl_activate(hdl->video_bitrate_peak,
			hdl->video_bitrate_mode->val != V4L2_MPEG_VIDEO_BITRATE_MODE_CBR);
		if (cx2341x_neq(hdl->video_encoding) &&
		    hdl->ops && hdl->ops->s_video_encoding)
			return hdl->ops->s_video_encoding(hdl, hdl->video_encoding->val);
		return 0;

	case V4L2_CID_MPEG_VIDEO_MUTE:
		/* video mute cluster */
		return cx2341x_hdl_api(hdl, CX2341X_ENC_MUTE_VIDEO, 1,
				hdl->video_mute->val |
					(hdl->video_mute_yuv->val << 8));

	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE: {
		int active_filter;

		/* video filter mode */
		err = cx2341x_hdl_api(hdl, CX2341X_ENC_SET_DNR_FILTER_MODE, 2,
				hdl->video_spatial_filter_mode->val |
					(hdl->video_temporal_filter_mode->val << 1),
				hdl->video_median_filter_type->val);
		if (err)
			return err;

		active_filter = hdl->video_spatial_filter_mode->val !=
				V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_AUTO;
		v4l2_ctrl_activate(hdl->video_spatial_filter, active_filter);
		v4l2_ctrl_activate(hdl->video_luma_spatial_filter_type, active_filter);
		v4l2_ctrl_activate(hdl->video_chroma_spatial_filter_type, active_filter);
		active_filter = hdl->video_temporal_filter_mode->val !=
				V4L2_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE_AUTO;
		v4l2_ctrl_activate(hdl->video_temporal_filter, active_filter);
		active_filter = hdl->video_median_filter_type->val !=
				V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_OFF;
		v4l2_ctrl_activate(hdl->video_luma_median_filter_bottom, active_filter);
		v4l2_ctrl_activate(hdl->video_luma_median_filter_top, active_filter);
		v4l2_ctrl_activate(hdl->video_chroma_median_filter_bottom, active_filter);
		v4l2_ctrl_activate(hdl->video_chroma_median_filter_top, active_filter);
		return 0;
	}

	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE:
		/* video filter type cluster */
		return cx2341x_hdl_api(hdl,
				CX2341X_ENC_SET_SPATIAL_FILTER_TYPE, 2,
				hdl->video_luma_spatial_filter_type->val,
				hdl->video_chroma_spatial_filter_type->val);

	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER:
		/* video filter cluster */
		return cx2341x_hdl_api(hdl, CX2341X_ENC_SET_DNR_FILTER_PROPS, 2,
				hdl->video_spatial_filter->val,
				hdl->video_temporal_filter->val);

	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_TOP:
		/* video median cluster */
		return cx2341x_hdl_api(hdl, CX2341X_ENC_SET_CORING_LEVELS, 4,
				hdl->video_luma_median_filter_bottom->val,
				hdl->video_luma_median_filter_top->val,
				hdl->video_chroma_median_filter_bottom->val,
				hdl->video_chroma_median_filter_top->val);
	}
	return -EINVAL;
}

static const struct v4l2_ctrl_ops cx2341x_ops = {
	.try_ctrl = cx2341x_try_ctrl,
	.s_ctrl = cx2341x_s_ctrl,
};

static struct v4l2_ctrl *cx2341x_ctrl_new_custom(struct v4l2_ctrl_handler *hdl,
			u32 id, s32 min, s32 max, s32 step, s32 def)
{
	struct v4l2_ctrl_config cfg;

	memset(&cfg, 0, sizeof(cfg));
	cx2341x_ctrl_fill(id, &cfg.name, &cfg.type, &min, &max, &step, &def, &cfg.flags);
	cfg.ops = &cx2341x_ops;
	cfg.id = id;
	cfg.min = min;
	cfg.max = max;
	cfg.def = def;
	if (cfg.type == V4L2_CTRL_TYPE_MENU) {
		cfg.step = 0;
		cfg.menu_skip_mask = step;
		cfg.qmenu = cx2341x_get_menu(id);
	} else {
		cfg.step = step;
		cfg.menu_skip_mask = 0;
	}
	return v4l2_ctrl_new_custom(hdl, &cfg, NULL);
}

static struct v4l2_ctrl *cx2341x_ctrl_new_std(struct v4l2_ctrl_handler *hdl,
			u32 id, s32 min, s32 max, s32 step, s32 def)
{
	return v4l2_ctrl_new_std(hdl, &cx2341x_ops, id, min, max, step, def);
}

static struct v4l2_ctrl *cx2341x_ctrl_new_menu(struct v4l2_ctrl_handler *hdl,
			u32 id, s32 max, s32 mask, s32 def)
{
	return v4l2_ctrl_new_std_menu(hdl, &cx2341x_ops, id, max, mask, def);
}

int cx2341x_handler_init(struct cx2341x_handler *cxhdl,
			 unsigned nr_of_controls_hint)
{
	struct v4l2_ctrl_handler *hdl = &cxhdl->hdl;
	u32 caps = cxhdl->capabilities;
	int has_sliced_vbi = caps & CX2341X_CAP_HAS_SLICED_VBI;
	int has_ac3 = caps & CX2341X_CAP_HAS_AC3;
	int has_ts = caps & CX2341X_CAP_HAS_TS;

	cxhdl->width = 720;
	cxhdl->height = 480;

	v4l2_ctrl_handler_init(hdl, nr_of_controls_hint);

	/* Add controls in ascending control ID order for fastest
	   insertion time. */
	cxhdl->stream_type = cx2341x_ctrl_new_menu(hdl,
			V4L2_CID_MPEG_STREAM_TYPE,
			V4L2_MPEG_STREAM_TYPE_MPEG2_SVCD, has_ts ? 0 : 2,
			V4L2_MPEG_STREAM_TYPE_MPEG2_PS);
	cxhdl->stream_vbi_fmt = cx2341x_ctrl_new_menu(hdl,
			V4L2_CID_MPEG_STREAM_VBI_FMT,
			V4L2_MPEG_STREAM_VBI_FMT_IVTV, has_sliced_vbi ? 0 : 2,
			V4L2_MPEG_STREAM_VBI_FMT_NONE);
	cxhdl->audio_sampling_freq = cx2341x_ctrl_new_menu(hdl,
			V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ,
			V4L2_MPEG_AUDIO_SAMPLING_FREQ_32000, 0,
			V4L2_MPEG_AUDIO_SAMPLING_FREQ_48000);
	cxhdl->audio_encoding = cx2341x_ctrl_new_menu(hdl,
			V4L2_CID_MPEG_AUDIO_ENCODING,
			V4L2_MPEG_AUDIO_ENCODING_AC3, has_ac3 ? ~0x12 : ~0x2,
			V4L2_MPEG_AUDIO_ENCODING_LAYER_2);
	cxhdl->audio_l2_bitrate = cx2341x_ctrl_new_menu(hdl,
			V4L2_CID_MPEG_AUDIO_L2_BITRATE,
			V4L2_MPEG_AUDIO_L2_BITRATE_384K, 0x1ff,
			V4L2_MPEG_AUDIO_L2_BITRATE_224K);
	cxhdl->audio_mode = cx2341x_ctrl_new_menu(hdl,
			V4L2_CID_MPEG_AUDIO_MODE,
			V4L2_MPEG_AUDIO_MODE_MONO, 0,
			V4L2_MPEG_AUDIO_MODE_STEREO);
	cxhdl->audio_mode_extension = cx2341x_ctrl_new_menu(hdl,
			V4L2_CID_MPEG_AUDIO_MODE_EXTENSION,
			V4L2_MPEG_AUDIO_MODE_EXTENSION_BOUND_16, 0,
			V4L2_MPEG_AUDIO_MODE_EXTENSION_BOUND_4);
	cxhdl->audio_emphasis = cx2341x_ctrl_new_menu(hdl,
			V4L2_CID_MPEG_AUDIO_EMPHASIS,
			V4L2_MPEG_AUDIO_EMPHASIS_CCITT_J17, 0,
			V4L2_MPEG_AUDIO_EMPHASIS_NONE);
	cxhdl->audio_crc = cx2341x_ctrl_new_menu(hdl,
			V4L2_CID_MPEG_AUDIO_CRC,
			V4L2_MPEG_AUDIO_CRC_CRC16, 0,
			V4L2_MPEG_AUDIO_CRC_NONE);

	cx2341x_ctrl_new_std(hdl, V4L2_CID_MPEG_AUDIO_MUTE, 0, 1, 1, 0);
	if (has_ac3)
		cxhdl->audio_ac3_bitrate = cx2341x_ctrl_new_menu(hdl,
				V4L2_CID_MPEG_AUDIO_AC3_BITRATE,
				V4L2_MPEG_AUDIO_AC3_BITRATE_448K, 0x03,
				V4L2_MPEG_AUDIO_AC3_BITRATE_224K);
	cxhdl->video_encoding = cx2341x_ctrl_new_menu(hdl,
			V4L2_CID_MPEG_VIDEO_ENCODING,
			V4L2_MPEG_VIDEO_ENCODING_MPEG_2, 0,
			V4L2_MPEG_VIDEO_ENCODING_MPEG_2);
	cx2341x_ctrl_new_menu(hdl,
			V4L2_CID_MPEG_VIDEO_ASPECT,
			V4L2_MPEG_VIDEO_ASPECT_221x100, 0,
			V4L2_MPEG_VIDEO_ASPECT_4x3);
	cxhdl->video_b_frames = cx2341x_ctrl_new_std(hdl,
			V4L2_CID_MPEG_VIDEO_B_FRAMES, 0, 33, 1, 2);
	cxhdl->video_gop_size = cx2341x_ctrl_new_std(hdl,
			V4L2_CID_MPEG_VIDEO_GOP_SIZE,
			1, 34, 1, cxhdl->is_50hz ? 12 : 15);
	cx2341x_ctrl_new_std(hdl, V4L2_CID_MPEG_VIDEO_GOP_CLOSURE, 0, 1, 1, 1);
	cxhdl->video_bitrate_mode = cx2341x_ctrl_new_menu(hdl,
			V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
			V4L2_MPEG_VIDEO_BITRATE_MODE_CBR, 0,
			V4L2_MPEG_VIDEO_BITRATE_MODE_VBR);
	cxhdl->video_bitrate = cx2341x_ctrl_new_std(hdl,
			V4L2_CID_MPEG_VIDEO_BITRATE,
			0, 27000000, 1, 6000000);
	cxhdl->video_bitrate_peak = cx2341x_ctrl_new_std(hdl,
			V4L2_CID_MPEG_VIDEO_BITRATE_PEAK,
			0, 27000000, 1, 8000000);
	cx2341x_ctrl_new_std(hdl,
			V4L2_CID_MPEG_VIDEO_TEMPORAL_DECIMATION, 0, 255, 1, 0);
	cxhdl->video_mute = cx2341x_ctrl_new_std(hdl,
			V4L2_CID_MPEG_VIDEO_MUTE, 0, 1, 1, 0);
	cxhdl->video_mute_yuv = cx2341x_ctrl_new_std(hdl,
			V4L2_CID_MPEG_VIDEO_MUTE_YUV, 0, 0xffffff, 1, 0x008080);

	/* CX23415/6 specific */
	cxhdl->video_spatial_filter_mode = cx2341x_ctrl_new_custom(hdl,
			V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE,
			V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_MANUAL,
			V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_AUTO, 0,
			V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_MANUAL);
	cxhdl->video_spatial_filter = cx2341x_ctrl_new_custom(hdl,
			V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER,
			0, 15, 1, 0);
	cxhdl->video_luma_spatial_filter_type = cx2341x_ctrl_new_custom(hdl,
			V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE,
			V4L2_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE_OFF,
			V4L2_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE_2D_SYM_NON_SEPARABLE,
			0,
			V4L2_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE_1D_HOR);
	cxhdl->video_chroma_spatial_filter_type = cx2341x_ctrl_new_custom(hdl,
			V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE,
			V4L2_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE_OFF,
			V4L2_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE_1D_HOR,
			0,
			V4L2_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE_1D_HOR);
	cxhdl->video_temporal_filter_mode = cx2341x_ctrl_new_custom(hdl,
			V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE,
			V4L2_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE_MANUAL,
			V4L2_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE_AUTO,
			0,
			V4L2_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE_MANUAL);
	cxhdl->video_temporal_filter = cx2341x_ctrl_new_custom(hdl,
			V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER,
			0, 31, 1, 8);
	cxhdl->video_median_filter_type = cx2341x_ctrl_new_custom(hdl,
			V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE,
			V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_OFF,
			V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_DIAG,
			0,
			V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_OFF);
	cxhdl->video_luma_median_filter_bottom = cx2341x_ctrl_new_custom(hdl,
			V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_BOTTOM,
			0, 255, 1, 0);
	cxhdl->video_luma_median_filter_top = cx2341x_ctrl_new_custom(hdl,
			V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_TOP,
			0, 255, 1, 255);
	cxhdl->video_chroma_median_filter_bottom = cx2341x_ctrl_new_custom(hdl,
			V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_BOTTOM,
			0, 255, 1, 0);
	cxhdl->video_chroma_median_filter_top = cx2341x_ctrl_new_custom(hdl,
			V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_TOP,
			0, 255, 1, 255);
	cx2341x_ctrl_new_custom(hdl, V4L2_CID_MPEG_CX2341X_STREAM_INSERT_NAV_PACKETS,
			0, 1, 1, 0);

	if (hdl->error) {
		int err = hdl->error;

		v4l2_ctrl_handler_free(hdl);
		return err;
	}

	v4l2_ctrl_cluster(8, &cxhdl->audio_sampling_freq);
	v4l2_ctrl_cluster(2, &cxhdl->video_b_frames);
	v4l2_ctrl_cluster(5, &cxhdl->stream_type);
	v4l2_ctrl_cluster(2, &cxhdl->video_mute);
	v4l2_ctrl_cluster(3, &cxhdl->video_spatial_filter_mode);
	v4l2_ctrl_cluster(2, &cxhdl->video_luma_spatial_filter_type);
	v4l2_ctrl_cluster(2, &cxhdl->video_spatial_filter);
	v4l2_ctrl_cluster(4, &cxhdl->video_luma_median_filter_top);

	return 0;
}
EXPORT_SYMBOL(cx2341x_handler_init);

void cx2341x_handler_set_50hz(struct cx2341x_handler *cxhdl, int is_50hz)
{
	cxhdl->is_50hz = is_50hz;
	cxhdl->video_gop_size->default_value = cxhdl->is_50hz ? 12 : 15;
}
EXPORT_SYMBOL(cx2341x_handler_set_50hz);

int cx2341x_handler_setup(struct cx2341x_handler *cxhdl)
{
	int h = cxhdl->height;
	int w = cxhdl->width;
	int err;

	err = cx2341x_hdl_api(cxhdl, CX2341X_ENC_SET_OUTPUT_PORT, 2, cxhdl->port, 0);
	if (err)
		return err;
	err = cx2341x_hdl_api(cxhdl, CX2341X_ENC_SET_FRAME_RATE, 1, cxhdl->is_50hz);
	if (err)
		return err;

	if (v4l2_ctrl_g_ctrl(cxhdl->video_encoding) == V4L2_MPEG_VIDEO_ENCODING_MPEG_1) {
		w /= 2;
		h /= 2;
	}
	err = cx2341x_hdl_api(cxhdl, CX2341X_ENC_SET_FRAME_SIZE, 2, h, w);
	if (err)
		return err;
	return v4l2_ctrl_handler_setup(&cxhdl->hdl);
}
EXPORT_SYMBOL(cx2341x_handler_setup);

void cx2341x_handler_set_busy(struct cx2341x_handler *cxhdl, int busy)
{
	v4l2_ctrl_grab(cxhdl->audio_sampling_freq, busy);
	v4l2_ctrl_grab(cxhdl->audio_encoding, busy);
	v4l2_ctrl_grab(cxhdl->audio_l2_bitrate, busy);
	v4l2_ctrl_grab(cxhdl->audio_ac3_bitrate, busy);
	v4l2_ctrl_grab(cxhdl->stream_vbi_fmt, busy);
	v4l2_ctrl_grab(cxhdl->stream_type, busy);
	v4l2_ctrl_grab(cxhdl->video_bitrate_mode, busy);
	v4l2_ctrl_grab(cxhdl->video_bitrate, busy);
	v4l2_ctrl_grab(cxhdl->video_bitrate_peak, busy);
}
EXPORT_SYMBOL(cx2341x_handler_set_busy);
