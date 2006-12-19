/*
 * cx2341x - generic code for cx23415/6 based devices
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <linux/i2c.h>

#include <media/tuner.h>
#include <media/cx2341x.h>
#include <media/v4l2-common.h>

MODULE_DESCRIPTION("cx23415/6 driver");
MODULE_AUTHOR("Hans Verkuil");
MODULE_LICENSE("GPL");

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

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
	V4L2_CID_MPEG_VIDEO_ENCODING,
	V4L2_CID_MPEG_VIDEO_ASPECT,
	V4L2_CID_MPEG_VIDEO_B_FRAMES,
	V4L2_CID_MPEG_VIDEO_GOP_SIZE,
	V4L2_CID_MPEG_VIDEO_GOP_CLOSURE,
	V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
	V4L2_CID_MPEG_VIDEO_BITRATE,
	V4L2_CID_MPEG_VIDEO_BITRATE_PEAK,
	V4L2_CID_MPEG_VIDEO_TEMPORAL_DECIMATION,
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
	0
};


/* Map the control ID to the correct field in the cx2341x_mpeg_params
   struct. Return -EINVAL if the ID is unknown, else return 0. */
static int cx2341x_get_ctrl(struct cx2341x_mpeg_params *params,
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
	default:
		return -EINVAL;
	}
	return 0;
}

/* Map the control ID to the correct field in the cx2341x_mpeg_params
   struct. Return -EINVAL if the ID is unknown, else return 0. */
static int cx2341x_set_ctrl(struct cx2341x_mpeg_params *params,
		struct v4l2_ext_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ:
		params->audio_sampling_freq = ctrl->value;
		break;
	case V4L2_CID_MPEG_AUDIO_ENCODING:
		params->audio_encoding = ctrl->value;
		break;
	case V4L2_CID_MPEG_AUDIO_L2_BITRATE:
		params->audio_l2_bitrate = ctrl->value;
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
		/* MPEG-1 only allows CBR */
		if (params->video_encoding == V4L2_MPEG_VIDEO_ENCODING_MPEG_1 &&
		    ctrl->value != V4L2_MPEG_VIDEO_BITRATE_MODE_CBR)
			return -EINVAL;
		params->video_bitrate_mode = ctrl->value;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		params->video_bitrate = ctrl->value;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE_PEAK:
		params->video_bitrate_peak = ctrl->value;
		break;
	case V4L2_CID_MPEG_VIDEO_TEMPORAL_DECIMATION:
		params->video_temporal_decimation = ctrl->value;
		break;
	case V4L2_CID_MPEG_STREAM_TYPE:
		params->stream_type = ctrl->value;
		params->video_encoding =
			(params->stream_type == V4L2_MPEG_STREAM_TYPE_MPEG1_SS ||
			 params->stream_type == V4L2_MPEG_STREAM_TYPE_MPEG1_VCD) ?
			V4L2_MPEG_VIDEO_ENCODING_MPEG_1 : V4L2_MPEG_VIDEO_ENCODING_MPEG_2;
		if (params->video_encoding == V4L2_MPEG_VIDEO_ENCODING_MPEG_1) {
			/* MPEG-1 implies CBR */
			params->video_bitrate_mode = V4L2_MPEG_VIDEO_BITRATE_MODE_CBR;
		}
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
	default:
		return -EINVAL;
	}
	return 0;
}

static int cx2341x_ctrl_query_fill(struct v4l2_queryctrl *qctrl, s32 min, s32 max, s32 step, s32 def)
{
	const char *name;

	qctrl->flags = 0;
	switch (qctrl->id) {
	/* MPEG controls */
	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE:
		name = "Spatial Filter Mode";
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER:
		name = "Spatial Filter";
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE:
		name = "Spatial Luma Filter Type";
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE:
		name = "Spatial Chroma Filter Type";
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE:
		name = "Temporal Filter Mode";
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER:
		name = "Temporal Filter";
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE:
		name = "Median Filter Type";
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_TOP:
		name = "Median Luma Filter Maximum";
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_BOTTOM:
		name = "Median Luma Filter Minimum";
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_TOP:
		name = "Median Chroma Filter Maximum";
		break;
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_BOTTOM:
		name = "Median Chroma Filter Minimum";
		break;

	default:
		return v4l2_ctrl_query_fill(qctrl, min, max, step, def);
	}
	switch (qctrl->id) {
	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE:
		qctrl->type = V4L2_CTRL_TYPE_MENU;
		min = 0;
		step = 1;
		break;
	default:
		qctrl->type = V4L2_CTRL_TYPE_INTEGER;
		break;
	}
	switch (qctrl->id) {
	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE:
	case V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE:
		qctrl->flags |= V4L2_CTRL_FLAG_UPDATE;
		break;
	}
	qctrl->minimum = min;
	qctrl->maximum = max;
	qctrl->step = step;
	qctrl->default_value = def;
	qctrl->reserved[0] = qctrl->reserved[1] = 0;
	snprintf(qctrl->name, sizeof(qctrl->name), name);
	return 0;
}

int cx2341x_ctrl_query(struct cx2341x_mpeg_params *params, struct v4l2_queryctrl *qctrl)
{
	int err;

	switch (qctrl->id) {
	case V4L2_CID_MPEG_AUDIO_ENCODING:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_AUDIO_ENCODING_LAYER_2,
				V4L2_MPEG_AUDIO_ENCODING_LAYER_2, 1,
				V4L2_MPEG_AUDIO_ENCODING_LAYER_2);

	case V4L2_CID_MPEG_AUDIO_L2_BITRATE:
		return v4l2_ctrl_query_fill(qctrl,
				V4L2_MPEG_AUDIO_L2_BITRATE_192K,
				V4L2_MPEG_AUDIO_L2_BITRATE_384K, 1,
				V4L2_MPEG_AUDIO_L2_BITRATE_224K);

	case V4L2_CID_MPEG_AUDIO_L1_BITRATE:
	case V4L2_CID_MPEG_AUDIO_L3_BITRATE:
		return -EINVAL;

	case V4L2_CID_MPEG_AUDIO_MODE_EXTENSION:
		err = v4l2_ctrl_query_fill_std(qctrl);
		if (err == 0 && params->audio_mode != V4L2_MPEG_AUDIO_MODE_JOINT_STEREO)
			qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return err;

	case V4L2_CID_MPEG_VIDEO_ENCODING:
		/* this setting is read-only for the cx2341x since the
		   V4L2_CID_MPEG_STREAM_TYPE really determines the
		   MPEG-1/2 setting */
		err = v4l2_ctrl_query_fill_std(qctrl);
		if (err == 0)
			qctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
		return err;

	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		err = v4l2_ctrl_query_fill_std(qctrl);
		if (err == 0 && params->video_encoding == V4L2_MPEG_VIDEO_ENCODING_MPEG_1)
			qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return err;

	case V4L2_CID_MPEG_VIDEO_BITRATE_PEAK:
		err = v4l2_ctrl_query_fill_std(qctrl);
		if (err == 0 && params->video_bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_CBR)
			qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return err;

	case V4L2_CID_MPEG_STREAM_VBI_FMT:
		if (params->capabilities & CX2341X_CAP_HAS_SLICED_VBI)
			return v4l2_ctrl_query_fill_std(qctrl);
		return cx2341x_ctrl_query_fill(qctrl,
				V4L2_MPEG_STREAM_VBI_FMT_NONE,
				V4L2_MPEG_STREAM_VBI_FMT_NONE, 1,
				V4L2_MPEG_STREAM_VBI_FMT_NONE);

	/* CX23415/6 specific */
	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE:
		return cx2341x_ctrl_query_fill(qctrl,
				V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_MANUAL,
				V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_AUTO, 1,
				V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_MANUAL);

	case V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER:
		cx2341x_ctrl_query_fill(qctrl, 0, 15, 1, 0);
		qctrl->flags |= V4L2_CTRL_FLAG_SLIDER;
		if (params->video_spatial_filter_mode == V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_AUTO)
		       qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return 0;

	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE:
		cx2341x_ctrl_query_fill(qctrl,
				V4L2_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE_OFF,
				V4L2_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE_2D_SYM_NON_SEPARABLE, 1,
				V4L2_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE_OFF);
		if (params->video_spatial_filter_mode == V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_AUTO)
		       qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return 0;

	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE:
		cx2341x_ctrl_query_fill(qctrl,
				V4L2_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE_OFF,
				V4L2_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE_1D_HOR, 1,
				V4L2_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE_OFF);
		if (params->video_spatial_filter_mode == V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_AUTO)
		       qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return 0;

	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE:
		return cx2341x_ctrl_query_fill(qctrl,
				V4L2_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE_MANUAL,
				V4L2_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE_AUTO, 1,
				V4L2_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE_MANUAL);

	case V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER:
		cx2341x_ctrl_query_fill(qctrl, 0, 31, 1, 0);
		qctrl->flags |= V4L2_CTRL_FLAG_SLIDER;
		if (params->video_temporal_filter_mode == V4L2_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE_AUTO)
		       qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return 0;

	case V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE:
		return cx2341x_ctrl_query_fill(qctrl,
				V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_OFF,
				V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_DIAG, 1,
				V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_OFF);

	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_TOP:
		cx2341x_ctrl_query_fill(qctrl, 0, 255, 1, 255);
		qctrl->flags |= V4L2_CTRL_FLAG_SLIDER;
		if (params->video_median_filter_type == V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_OFF)
		       qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return 0;

	case V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_BOTTOM:
		cx2341x_ctrl_query_fill(qctrl, 0, 255, 1, 0);
		qctrl->flags |= V4L2_CTRL_FLAG_SLIDER;
		if (params->video_median_filter_type == V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_OFF)
		       qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return 0;

	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_TOP:
		cx2341x_ctrl_query_fill(qctrl, 0, 255, 1, 255);
		qctrl->flags |= V4L2_CTRL_FLAG_SLIDER;
		if (params->video_median_filter_type == V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_OFF)
		       qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return 0;

	case V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_BOTTOM:
		cx2341x_ctrl_query_fill(qctrl, 0, 255, 1, 0);
		qctrl->flags |= V4L2_CTRL_FLAG_SLIDER;
		if (params->video_median_filter_type == V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_OFF)
		       qctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
		return 0;

	default:
		return v4l2_ctrl_query_fill_std(qctrl);

	}
}

const char **cx2341x_ctrl_get_menu(u32 id)
{
	static const char *mpeg_stream_type[] = {
		"MPEG-2 Program Stream",
		"",
		"MPEG-1 System Stream",
		"MPEG-2 DVD-compatible Stream",
		"MPEG-1 VCD-compatible Stream",
		"MPEG-2 SVCD-compatible Stream",
		NULL
	};

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
	case V4L2_CID_MPEG_STREAM_TYPE:
		return mpeg_stream_type;
	case V4L2_CID_MPEG_AUDIO_L1_BITRATE:
	case V4L2_CID_MPEG_AUDIO_L3_BITRATE:
		return NULL;
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
	default:
		return v4l2_ctrl_get_menu(id);
	}
}

static void cx2341x_calc_audio_properties(struct cx2341x_mpeg_params *params)
{
	params->audio_properties = (params->audio_sampling_freq << 0) |
		((3 - params->audio_encoding) << 2) |
		((1 + params->audio_l2_bitrate) << 4) |
		(params->audio_mode << 8) |
		(params->audio_mode_extension << 10) |
		(((params->audio_emphasis == V4L2_MPEG_AUDIO_EMPHASIS_CCITT_J17) ?
		  3 :
		  params->audio_emphasis) << 12) |
		(params->audio_crc << 14);
}

int cx2341x_ext_ctrls(struct cx2341x_mpeg_params *params,
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
		const char **menu_items = NULL;

		qctrl.id = ctrl->id;
		err = cx2341x_ctrl_query(params, &qctrl);
		if (err)
			break;
		if (qctrl.type == V4L2_CTRL_TYPE_MENU)
			menu_items = cx2341x_ctrl_get_menu(qctrl.id);
		err = v4l2_ctrl_check(ctrl, &qctrl, menu_items);
		if (err)
			break;
		err = cx2341x_set_ctrl(params, ctrl);
		if (err)
			break;
	}
	if (err == 0 && params->video_bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR &&
			params->video_bitrate_peak < params->video_bitrate) {
		err = -ERANGE;
		ctrls->error_idx = ctrls->count;
	}
	if (err) {
		ctrls->error_idx = i;
	}
	else {
		cx2341x_calc_audio_properties(params);
	}
	return err;
}

void cx2341x_fill_defaults(struct cx2341x_mpeg_params *p)
{
	static struct cx2341x_mpeg_params default_params = {
	/* misc */
	.capabilities = 0,
	.port = CX2341X_PORT_MEMORY,
	.width = 720,
	.height = 480,
	.is_50hz = 0,

	/* stream */
	.stream_type = V4L2_MPEG_STREAM_TYPE_MPEG2_PS,
	.stream_vbi_fmt = V4L2_MPEG_STREAM_VBI_FMT_NONE,

	/* audio */
	.audio_sampling_freq = V4L2_MPEG_AUDIO_SAMPLING_FREQ_48000,
	.audio_encoding = V4L2_MPEG_AUDIO_ENCODING_LAYER_2,
	.audio_l2_bitrate = V4L2_MPEG_AUDIO_L2_BITRATE_224K,
	.audio_mode = V4L2_MPEG_AUDIO_MODE_STEREO,
	.audio_mode_extension = V4L2_MPEG_AUDIO_MODE_EXTENSION_BOUND_4,
	.audio_emphasis = V4L2_MPEG_AUDIO_EMPHASIS_NONE,
	.audio_crc = V4L2_MPEG_AUDIO_CRC_NONE,

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

	/* encoding filters */
	.video_spatial_filter_mode = V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_MANUAL,
	.video_spatial_filter = 0,
	.video_luma_spatial_filter_type = V4L2_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE_1D_HOR,
	.video_chroma_spatial_filter_type = V4L2_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE_1D_HOR,
	.video_temporal_filter_mode = V4L2_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE_MANUAL,
	.video_temporal_filter = 8,
	.video_median_filter_type = V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_OFF,
	.video_luma_median_filter_top = 255,
	.video_luma_median_filter_bottom = 0,
	.video_chroma_median_filter_top = 255,
	.video_chroma_median_filter_bottom = 0,
	};

	*p = default_params;
	cx2341x_calc_audio_properties(p);
}

static int cx2341x_api(void *priv, cx2341x_mbox_func func, int cmd, int args, ...)
{
	u32 data[CX2341X_MBOX_MAX_DATA];
	va_list vargs;
	int i;

	va_start(vargs, args);

	for (i = 0; i < args; i++) {
		data[i] = va_arg(vargs, int);
	}
	va_end(vargs);
	return func(priv, cmd, args, 0, data);
}

int cx2341x_update(void *priv, cx2341x_mbox_func func,
		const struct cx2341x_mpeg_params *old, const struct cx2341x_mpeg_params *new)
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
	u16 temporal = new->video_temporal_filter;

	cx2341x_api(priv, func, CX2341X_ENC_SET_OUTPUT_PORT, 2, new->port, 0);

	if (old == NULL || old->is_50hz != new->is_50hz) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_FRAME_RATE, 1, new->is_50hz);
		if (err) return err;
	}

	if (old == NULL || old->width != new->width || old->height != new->height ||
			old->video_encoding != new->video_encoding) {
		u16 w = new->width;
		u16 h = new->height;

		if (new->video_encoding == V4L2_MPEG_VIDEO_ENCODING_MPEG_1) {
			w /= 2;
			h /= 2;
		}
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_FRAME_SIZE, 2, h, w);
		if (err) return err;
	}

	if (new->width != 720 || new->height != (new->is_50hz ? 576 : 480)) {
		/* Adjust temporal filter if necessary. The problem with the temporal
		   filter is that it works well with full resolution capturing, but
		   not when the capture window is scaled (the filter introduces
		   a ghosting effect). So if the capture window is scaled, then
		   force the filter to 0.

		   For full resolution the filter really improves the video
		   quality, especially if the original video quality is suboptimal. */
		temporal = 0;
	}

	if (old == NULL || old->stream_type != new->stream_type) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_STREAM_TYPE, 1, mpeg_stream_type[new->stream_type]);
		if (err) return err;
	}
	if (old == NULL || old->video_aspect != new->video_aspect) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_ASPECT_RATIO, 1, 1 + new->video_aspect);
		if (err) return err;
	}
	if (old == NULL || old->video_b_frames != new->video_b_frames ||
		old->video_gop_size != new->video_gop_size) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_GOP_PROPERTIES, 2,
				new->video_gop_size, new->video_b_frames + 1);
		if (err) return err;
	}
	if (old == NULL || old->video_gop_closure != new->video_gop_closure) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_GOP_CLOSURE, 1, new->video_gop_closure);
		if (err) return err;
	}
	if (old == NULL || old->audio_properties != new->audio_properties) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_AUDIO_PROPERTIES, 1, new->audio_properties);
		if (err) return err;
	}
	if (old == NULL || old->video_bitrate_mode != new->video_bitrate_mode ||
		old->video_bitrate != new->video_bitrate ||
		old->video_bitrate_peak != new->video_bitrate_peak) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_BIT_RATE, 5,
				new->video_bitrate_mode, new->video_bitrate,
				new->video_bitrate_peak / 400, 0, 0);
		if (err) return err;
	}
	if (old == NULL || old->video_spatial_filter_mode != new->video_spatial_filter_mode ||
		old->video_temporal_filter_mode != new->video_temporal_filter_mode ||
		old->video_median_filter_type != new->video_median_filter_type) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_DNR_FILTER_MODE, 2,
				new->video_spatial_filter_mode | (new->video_temporal_filter_mode << 1),
				new->video_median_filter_type);
		if (err) return err;
	}
	if (old == NULL ||
		old->video_luma_median_filter_bottom != new->video_luma_median_filter_bottom ||
		old->video_luma_median_filter_top != new->video_luma_median_filter_top ||
		old->video_chroma_median_filter_bottom != new->video_chroma_median_filter_bottom ||
		old->video_chroma_median_filter_top != new->video_chroma_median_filter_top) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_CORING_LEVELS, 4,
				new->video_luma_median_filter_bottom,
				new->video_luma_median_filter_top,
				new->video_chroma_median_filter_bottom,
				new->video_chroma_median_filter_top);
		if (err) return err;
	}
	if (old == NULL ||
		old->video_luma_spatial_filter_type != new->video_luma_spatial_filter_type ||
		old->video_chroma_spatial_filter_type != new->video_chroma_spatial_filter_type) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_SPATIAL_FILTER_TYPE, 2,
			new->video_luma_spatial_filter_type, new->video_chroma_spatial_filter_type);
		if (err) return err;
	}
	if (old == NULL ||
		old->video_spatial_filter != new->video_spatial_filter ||
		old->video_temporal_filter != temporal) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_DNR_FILTER_PROPS, 2,
			new->video_spatial_filter, temporal);
		if (err) return err;
	}
	if (old == NULL || old->video_temporal_decimation != new->video_temporal_decimation) {
		err = cx2341x_api(priv, func, CX2341X_ENC_SET_FRAME_DROP_RATE, 1,
			new->video_temporal_decimation);
		if (err) return err;
	}
	return 0;
}

static const char *cx2341x_menu_item(struct cx2341x_mpeg_params *p, u32 id)
{
	const char **menu = cx2341x_ctrl_get_menu(id);
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

void cx2341x_log_status(struct cx2341x_mpeg_params *p, const char *prefix)
{
	int is_mpeg1 = p->video_encoding == V4L2_MPEG_VIDEO_ENCODING_MPEG_1;
	int temporal = p->video_temporal_filter;

	/* Stream */
	printk(KERN_INFO "%s: Stream: %s\n",
		prefix,
		cx2341x_menu_item(p, V4L2_CID_MPEG_STREAM_TYPE));
	printk(KERN_INFO "%s: VBI Format: %s\n",
		prefix,
		cx2341x_menu_item(p, V4L2_CID_MPEG_STREAM_VBI_FMT));

	/* Video */
	printk(KERN_INFO "%s: Video:  %dx%d, %d fps\n",
		prefix,
		p->width / (is_mpeg1 ? 2 : 1), p->height / (is_mpeg1 ? 2 : 1),
		p->is_50hz ? 25 : 30);
	printk(KERN_INFO "%s: Video:  %s, %s, %s, %d",
		prefix,
		cx2341x_menu_item(p, V4L2_CID_MPEG_VIDEO_ENCODING),
		cx2341x_menu_item(p, V4L2_CID_MPEG_VIDEO_ASPECT),
		cx2341x_menu_item(p, V4L2_CID_MPEG_VIDEO_BITRATE_MODE),
		p->video_bitrate);
	if (p->video_bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR) {
		printk(", Peak %d", p->video_bitrate_peak);
	}
	printk("\n");
	printk(KERN_INFO "%s: Video:  GOP Size %d, %d B-Frames, %sGOP Closure\n",
		prefix,
		p->video_gop_size, p->video_b_frames,
		p->video_gop_closure ? "" : "No ");
	if (p->video_temporal_decimation) {
		printk(KERN_INFO "%s: Video: Temporal Decimation %d\n",
			prefix, p->video_temporal_decimation);
	}

	/* Audio */
	printk(KERN_INFO "%s: Audio:  %s, %s, %s, %s",
		prefix,
		cx2341x_menu_item(p, V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ),
		cx2341x_menu_item(p, V4L2_CID_MPEG_AUDIO_ENCODING),
		cx2341x_menu_item(p, V4L2_CID_MPEG_AUDIO_L2_BITRATE),
		cx2341x_menu_item(p, V4L2_CID_MPEG_AUDIO_MODE));
	if (p->audio_mode == V4L2_MPEG_AUDIO_MODE_JOINT_STEREO) {
		printk(", %s",
			cx2341x_menu_item(p, V4L2_CID_MPEG_AUDIO_MODE_EXTENSION));
	}
	printk(", %s, %s\n",
		cx2341x_menu_item(p, V4L2_CID_MPEG_AUDIO_EMPHASIS),
		cx2341x_menu_item(p, V4L2_CID_MPEG_AUDIO_CRC));

	/* Encoding filters */
	printk(KERN_INFO "%s: Spatial Filter:  %s, Luma %s, Chroma %s, %d\n",
		prefix,
		cx2341x_menu_item(p, V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE),
		cx2341x_menu_item(p, V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE),
		cx2341x_menu_item(p, V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE),
		p->video_spatial_filter);
	if (p->width != 720 || p->height != (p->is_50hz ? 576 : 480)) {
		temporal = 0;
	}
	printk(KERN_INFO "%s: Temporal Filter: %s, %d\n",
		prefix,
		cx2341x_menu_item(p, V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE),
		temporal);
	printk(KERN_INFO "%s: Median Filter:   %s, Luma [%d, %d], Chroma [%d, %d]\n",
		prefix,
		cx2341x_menu_item(p, V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE),
		p->video_luma_median_filter_bottom,
		p->video_luma_median_filter_top,
		p->video_chroma_median_filter_bottom,
		p->video_chroma_median_filter_top);
}

EXPORT_SYMBOL(cx2341x_fill_defaults);
EXPORT_SYMBOL(cx2341x_ctrl_query);
EXPORT_SYMBOL(cx2341x_ctrl_get_menu);
EXPORT_SYMBOL(cx2341x_ext_ctrls);
EXPORT_SYMBOL(cx2341x_update);
EXPORT_SYMBOL(cx2341x_log_status);
EXPORT_SYMBOL(cx2341x_mpeg_ctrls);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

