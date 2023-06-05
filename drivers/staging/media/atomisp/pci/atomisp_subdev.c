// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <media/v4l2-event.h>
#include <media/v4l2-mediabus.h>
#include <media/videobuf2-vmalloc.h>
#include "atomisp_cmd.h"
#include "atomisp_common.h"
#include "atomisp_compat.h"
#include "atomisp_fops.h"
#include "atomisp_internal.h"

const struct atomisp_in_fmt_conv atomisp_in_fmt_conv[] = {
	{ MEDIA_BUS_FMT_SBGGR8_1X8, 8, 8, ATOMISP_INPUT_FORMAT_RAW_8, IA_CSS_BAYER_ORDER_BGGR },
	{ MEDIA_BUS_FMT_SGBRG8_1X8, 8, 8, ATOMISP_INPUT_FORMAT_RAW_8, IA_CSS_BAYER_ORDER_GBRG },
	{ MEDIA_BUS_FMT_SGRBG8_1X8, 8, 8, ATOMISP_INPUT_FORMAT_RAW_8, IA_CSS_BAYER_ORDER_GRBG },
	{ MEDIA_BUS_FMT_SRGGB8_1X8, 8, 8, ATOMISP_INPUT_FORMAT_RAW_8, IA_CSS_BAYER_ORDER_RGGB },
	{ MEDIA_BUS_FMT_SBGGR10_1X10, 10, 10, ATOMISP_INPUT_FORMAT_RAW_10, IA_CSS_BAYER_ORDER_BGGR },
	{ MEDIA_BUS_FMT_SGBRG10_1X10, 10, 10, ATOMISP_INPUT_FORMAT_RAW_10, IA_CSS_BAYER_ORDER_GBRG },
	{ MEDIA_BUS_FMT_SGRBG10_1X10, 10, 10, ATOMISP_INPUT_FORMAT_RAW_10, IA_CSS_BAYER_ORDER_GRBG },
	{ MEDIA_BUS_FMT_SRGGB10_1X10, 10, 10, ATOMISP_INPUT_FORMAT_RAW_10, IA_CSS_BAYER_ORDER_RGGB },
	{ MEDIA_BUS_FMT_SBGGR12_1X12, 12, 12, ATOMISP_INPUT_FORMAT_RAW_12, IA_CSS_BAYER_ORDER_BGGR },
	{ MEDIA_BUS_FMT_SGBRG12_1X12, 12, 12, ATOMISP_INPUT_FORMAT_RAW_12, IA_CSS_BAYER_ORDER_GBRG },
	{ MEDIA_BUS_FMT_SGRBG12_1X12, 12, 12, ATOMISP_INPUT_FORMAT_RAW_12, IA_CSS_BAYER_ORDER_GRBG },
	{ MEDIA_BUS_FMT_SRGGB12_1X12, 12, 12, ATOMISP_INPUT_FORMAT_RAW_12, IA_CSS_BAYER_ORDER_RGGB },
	{ MEDIA_BUS_FMT_UYVY8_1X16, 8, 8, ATOMISP_INPUT_FORMAT_YUV422_8, 0 },
	{ MEDIA_BUS_FMT_YUYV8_1X16, 8, 8, ATOMISP_INPUT_FORMAT_YUV422_8, 0 },
#if 0 // disabled due to clang warnings
	{ MEDIA_BUS_FMT_JPEG_1X8, 8, 8, IA_CSS_FRAME_FORMAT_BINARY_8, 0 },
	{ V4L2_MBUS_FMT_CUSTOM_NV12, 12, 12, IA_CSS_FRAME_FORMAT_NV12, 0 },
	{ V4L2_MBUS_FMT_CUSTOM_NV21, 12, 12, IA_CSS_FRAME_FORMAT_NV21, 0 },
#endif
	{ V4L2_MBUS_FMT_CUSTOM_YUV420, 12, 12, ATOMISP_INPUT_FORMAT_YUV420_8_LEGACY, 0 },
#if 0
	{ V4L2_MBUS_FMT_CUSTOM_M10MO_RAW, 8, 8, IA_CSS_FRAME_FORMAT_BINARY_8, 0 },
#endif
	/* no valid V4L2 MBUS code for metadata format, so leave it 0. */
	{ 0, 0, 0, ATOMISP_INPUT_FORMAT_EMBEDDED, 0 },
	{}
};

static const struct {
	u32 code;
	u32 compressed;
} compressed_codes[] = {
	{ MEDIA_BUS_FMT_SBGGR10_1X10, MEDIA_BUS_FMT_SBGGR10_DPCM8_1X8 },
	{ MEDIA_BUS_FMT_SGBRG10_1X10, MEDIA_BUS_FMT_SGBRG10_DPCM8_1X8 },
	{ MEDIA_BUS_FMT_SGRBG10_1X10, MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8 },
	{ MEDIA_BUS_FMT_SRGGB10_1X10, MEDIA_BUS_FMT_SRGGB10_DPCM8_1X8 },
};

u32 atomisp_subdev_uncompressed_code(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(compressed_codes); i++)
		if (code == compressed_codes[i].compressed)
			return compressed_codes[i].code;

	return code;
}

bool atomisp_subdev_is_compressed(u32 code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(atomisp_in_fmt_conv) - 1; i++)
		if (code == atomisp_in_fmt_conv[i].code)
			return atomisp_in_fmt_conv[i].bpp !=
			       atomisp_in_fmt_conv[i].depth;

	return false;
}

const struct atomisp_in_fmt_conv *atomisp_find_in_fmt_conv(u32 code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(atomisp_in_fmt_conv) - 1; i++)
		if (code == atomisp_in_fmt_conv[i].code)
			return atomisp_in_fmt_conv + i;

	return NULL;
}

const struct atomisp_in_fmt_conv *atomisp_find_in_fmt_conv_by_atomisp_in_fmt(
    enum atomisp_input_format atomisp_in_fmt)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(atomisp_in_fmt_conv) - 1; i++)
		if (atomisp_in_fmt_conv[i].atomisp_in_fmt == atomisp_in_fmt)
			return atomisp_in_fmt_conv + i;

	return NULL;
}

bool atomisp_subdev_format_conversion(struct atomisp_sub_device *asd,
				      unsigned int source_pad)
{
	struct v4l2_mbus_framefmt *sink, *src;

	sink = atomisp_subdev_get_ffmt(&asd->subdev, NULL,
				       V4L2_SUBDEV_FORMAT_ACTIVE,
				       ATOMISP_SUBDEV_PAD_SINK);
	src = atomisp_subdev_get_ffmt(&asd->subdev, NULL,
				      V4L2_SUBDEV_FORMAT_ACTIVE, source_pad);

	return atomisp_is_mbuscode_raw(sink->code)
	       && !atomisp_is_mbuscode_raw(src->code);
}

uint16_t atomisp_subdev_source_pad(struct video_device *vdev)
{
	struct media_link *link;
	u16 ret = 0;

	list_for_each_entry(link, &vdev->entity.links, list) {
		if (link->source) {
			ret = link->source->index;
			break;
		}
	}
	return ret;
}

/*
 * V4L2 subdev operations
 */

/*
 * isp_subdev_ioctl - CCDC module private ioctl's
 * @sd: ISP V4L2 subdevice
 * @cmd: ioctl command
 * @arg: ioctl argument
 *
 * Return 0 on success or a negative error code otherwise.
 */
static long isp_subdev_ioctl(struct v4l2_subdev *sd,
			     unsigned int cmd, void *arg)
{
	return 0;
}

/*
 * isp_subdev_set_power - Power on/off the CCDC module
 * @sd: ISP V4L2 subdevice
 * @on: power on/off
 *
 * Return 0 on success or a negative error code otherwise.
 */
static int isp_subdev_set_power(struct v4l2_subdev *sd, int on)
{
	return 0;
}

static int isp_subdev_subscribe_event(struct v4l2_subdev *sd,
				      struct v4l2_fh *fh,
				      struct v4l2_event_subscription *sub)
{
	struct atomisp_sub_device *isp_sd = v4l2_get_subdevdata(sd);
	struct atomisp_device *isp = isp_sd->isp;

	if (sub->type != V4L2_EVENT_FRAME_SYNC &&
	    sub->type != V4L2_EVENT_FRAME_END &&
	    sub->type != V4L2_EVENT_ATOMISP_3A_STATS_READY &&
	    sub->type != V4L2_EVENT_ATOMISP_METADATA_READY &&
	    sub->type != V4L2_EVENT_ATOMISP_PAUSE_BUFFER &&
	    sub->type != V4L2_EVENT_ATOMISP_CSS_RESET &&
	    sub->type != V4L2_EVENT_ATOMISP_ACC_COMPLETE)
		return -EINVAL;

	if (sub->type == V4L2_EVENT_FRAME_SYNC &&
	    !atomisp_css_valid_sof(isp))
		return -EINVAL;

	return v4l2_event_subscribe(fh, sub, 16, NULL);
}

static int isp_subdev_unsubscribe_event(struct v4l2_subdev *sd,
					struct v4l2_fh *fh,
					struct v4l2_event_subscription *sub)
{
	return v4l2_event_unsubscribe(fh, sub);
}

/*
 * isp_subdev_enum_mbus_code - Handle pixel format enumeration
 * @sd: pointer to v4l2 subdev structure
 * @fh : V4L2 subdev file handle
 * @code: pointer to v4l2_subdev_pad_mbus_code_enum structure
 * return -EINVAL or zero on success
 */
static int isp_subdev_enum_mbus_code(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *sd_state,
				     struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(atomisp_in_fmt_conv) - 1)
		return -EINVAL;

	code->code = atomisp_in_fmt_conv[code->index].code;

	return 0;
}

static int isp_subdev_validate_rect(struct v4l2_subdev *sd, uint32_t pad,
				    uint32_t target)
{
	switch (pad) {
	case ATOMISP_SUBDEV_PAD_SINK:
		switch (target) {
		case V4L2_SEL_TGT_CROP:
			return 0;
		}
		break;
	default:
		switch (target) {
		case V4L2_SEL_TGT_COMPOSE:
			return 0;
		}
		break;
	}

	return -EINVAL;
}

struct v4l2_rect *atomisp_subdev_get_rect(struct v4l2_subdev *sd,
	struct v4l2_subdev_state *sd_state,
	u32 which, uint32_t pad,
	uint32_t target)
{
	struct atomisp_sub_device *isp_sd = v4l2_get_subdevdata(sd);

	if (which == V4L2_SUBDEV_FORMAT_TRY) {
		switch (target) {
		case V4L2_SEL_TGT_CROP:
			return v4l2_subdev_get_try_crop(sd, sd_state, pad);
		case V4L2_SEL_TGT_COMPOSE:
			return v4l2_subdev_get_try_compose(sd, sd_state, pad);
		}
	}

	switch (target) {
	case V4L2_SEL_TGT_CROP:
		return &isp_sd->fmt[pad].crop;
	case V4L2_SEL_TGT_COMPOSE:
		return &isp_sd->fmt[pad].compose;
	}

	return NULL;
}

struct v4l2_mbus_framefmt
*atomisp_subdev_get_ffmt(struct v4l2_subdev *sd,
			 struct v4l2_subdev_state *sd_state, uint32_t which,
			 uint32_t pad)
{
	struct atomisp_sub_device *isp_sd = v4l2_get_subdevdata(sd);

	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(sd, sd_state, pad);

	return &isp_sd->fmt[pad].fmt;
}

static void isp_get_fmt_rect(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state,
			     uint32_t which,
			     struct v4l2_mbus_framefmt **ffmt,
			     struct v4l2_rect *crop[ATOMISP_SUBDEV_PADS_NUM],
			     struct v4l2_rect *comp[ATOMISP_SUBDEV_PADS_NUM])
{
	unsigned int i;

	for (i = 0; i < ATOMISP_SUBDEV_PADS_NUM; i++) {
		ffmt[i] = atomisp_subdev_get_ffmt(sd, sd_state, which, i);
		crop[i] = atomisp_subdev_get_rect(sd, sd_state, which, i,
						  V4L2_SEL_TGT_CROP);
		comp[i] = atomisp_subdev_get_rect(sd, sd_state, which, i,
						  V4L2_SEL_TGT_COMPOSE);
	}
}

static void isp_subdev_propagate(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 u32 which, uint32_t pad, uint32_t target,
				 uint32_t flags)
{
	struct v4l2_mbus_framefmt *ffmt[ATOMISP_SUBDEV_PADS_NUM];
	struct v4l2_rect *crop[ATOMISP_SUBDEV_PADS_NUM],
		       *comp[ATOMISP_SUBDEV_PADS_NUM];

	if (flags & V4L2_SEL_FLAG_KEEP_CONFIG)
		return;

	isp_get_fmt_rect(sd, sd_state, which, ffmt, crop, comp);

	switch (pad) {
	case ATOMISP_SUBDEV_PAD_SINK: {
		struct v4l2_rect r = {0};

		/* Only crop target supported on sink pad. */
		r.width = ffmt[pad]->width;
		r.height = ffmt[pad]->height;

		atomisp_subdev_set_selection(sd, sd_state, which, pad,
					     target, flags, &r);
		break;
	}
	}
}

static int isp_subdev_get_selection(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *sd_state,
				    struct v4l2_subdev_selection *sel)
{
	struct v4l2_rect *rec;
	int rval = isp_subdev_validate_rect(sd, sel->pad, sel->target);

	if (rval)
		return rval;

	rec = atomisp_subdev_get_rect(sd, sd_state, sel->which, sel->pad,
				      sel->target);
	if (!rec)
		return -EINVAL;

	sel->r = *rec;
	return 0;
}

static const char *atomisp_pad_str(unsigned int pad)
{
	static const char *const pad_str[] = {
		"ATOMISP_SUBDEV_PAD_SINK",
		"ATOMISP_SUBDEV_PAD_SOURCE_CAPTURE",
		"ATOMISP_SUBDEV_PAD_SOURCE_VF",
		"ATOMISP_SUBDEV_PAD_SOURCE_PREVIEW",
		"ATOMISP_SUBDEV_PAD_SOURCE_VIDEO",
	};

	if (pad >= ARRAY_SIZE(pad_str))
		return "ATOMISP_INVALID_PAD";
	return pad_str[pad];
}

int atomisp_subdev_set_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 u32 which, uint32_t pad, uint32_t target,
				 u32 flags, struct v4l2_rect *r)
{
	struct atomisp_sub_device *isp_sd = v4l2_get_subdevdata(sd);
	struct atomisp_device *isp = isp_sd->isp;
	struct v4l2_mbus_framefmt *ffmt[ATOMISP_SUBDEV_PADS_NUM];
	struct v4l2_rect *crop[ATOMISP_SUBDEV_PADS_NUM],
		       *comp[ATOMISP_SUBDEV_PADS_NUM];
	unsigned int i;
	unsigned int padding_w = pad_w;
	unsigned int padding_h = pad_h;

	isp_get_fmt_rect(sd, sd_state, which, ffmt, crop, comp);

	dev_dbg(isp->dev,
		"sel: pad %s tgt %s l %d t %d w %d h %d which %s f 0x%8.8x\n",
		atomisp_pad_str(pad), target == V4L2_SEL_TGT_CROP
		? "V4L2_SEL_TGT_CROP" : "V4L2_SEL_TGT_COMPOSE",
		r->left, r->top, r->width, r->height,
		which == V4L2_SUBDEV_FORMAT_TRY ? "V4L2_SUBDEV_FORMAT_TRY"
		: "V4L2_SUBDEV_FORMAT_ACTIVE", flags);

	r->width = rounddown(r->width, ATOM_ISP_STEP_WIDTH);
	r->height = rounddown(r->height, ATOM_ISP_STEP_HEIGHT);

	switch (pad) {
	case ATOMISP_SUBDEV_PAD_SINK: {
		/* Only crop target supported on sink pad. */
		unsigned int dvs_w, dvs_h;

		crop[pad]->width = ffmt[pad]->width;
		crop[pad]->height = ffmt[pad]->height;

		/* Workaround for BYT 1080p perfectshot since the maxinum resolution of
		 * front camera ov2722 is 1932x1092 and cannot use pad_w > 12*/
		if (!strncmp(isp->inputs[isp_sd->input_curr].camera->name,
			     "ov2722", 6) && crop[pad]->height == 1092) {
			padding_w = 12;
			padding_h = 12;
		}

		if (atomisp_subdev_format_conversion(isp_sd,
						     isp_sd->capture_pad)
		    && crop[pad]->width && crop[pad]->height) {
			crop[pad]->width -= padding_w;
			crop[pad]->height -= padding_h;
		}

		if (isp_sd->params.video_dis_en &&
		    isp_sd->run_mode->val == ATOMISP_RUN_MODE_VIDEO) {
			/* This resolution contains 20 % of DVS slack
			 * (of the desired captured image before
			 * scaling, or 1 / 6 of what we get from the
			 * sensor) in both width and height. Remove
			 * it. */
			crop[pad]->width = roundup(crop[pad]->width * 5 / 6,
						   ATOM_ISP_STEP_WIDTH);
			crop[pad]->height = roundup(crop[pad]->height * 5 / 6,
						    ATOM_ISP_STEP_HEIGHT);
		}

		crop[pad]->width = min(crop[pad]->width, r->width);
		crop[pad]->height = min(crop[pad]->height, r->height);

		if (!(flags & V4L2_SEL_FLAG_KEEP_CONFIG)) {
			for (i = ATOMISP_SUBDEV_PAD_SOURCE_CAPTURE;
			     i < ATOMISP_SUBDEV_PADS_NUM; i++) {
				struct v4l2_rect tmp = *crop[pad];

				atomisp_subdev_set_selection(
				    sd, sd_state, which, i,
				    V4L2_SEL_TGT_COMPOSE,
				    flags, &tmp);
			}
		}

		if (which == V4L2_SUBDEV_FORMAT_TRY)
			break;

		if (isp_sd->params.video_dis_en &&
		    isp_sd->run_mode->val == ATOMISP_RUN_MODE_VIDEO) {
			dvs_w = rounddown(crop[pad]->width / 5,
					  ATOM_ISP_STEP_WIDTH);
			dvs_h = rounddown(crop[pad]->height / 5,
					  ATOM_ISP_STEP_HEIGHT);
		} else if (!isp_sd->params.video_dis_en &&
			   isp_sd->run_mode->val == ATOMISP_RUN_MODE_VIDEO) {
			/*
			 * For CSS2.0, digital zoom needs to set dvs envelope to 12
			 * when dvs is disabled.
			 */
			dvs_w = dvs_h = 12;
		} else {
			dvs_w = dvs_h = 0;
		}
		atomisp_css_video_set_dis_envelope(isp_sd, dvs_w, dvs_h);
		atomisp_css_input_set_effective_resolution(isp_sd,
							   ATOMISP_INPUT_STREAM_GENERAL,
							   crop[pad]->width,
							   crop[pad]->height);
		break;
	}
	case ATOMISP_SUBDEV_PAD_SOURCE_CAPTURE:
	case ATOMISP_SUBDEV_PAD_SOURCE_VIDEO: {
		/* Only compose target is supported on source pads. */

		if (isp_sd->vfpp->val == ATOMISP_VFPP_DISABLE_LOWLAT) {
			/* Scaling is disabled in this mode */
			r->width = crop[ATOMISP_SUBDEV_PAD_SINK]->width;
			r->height = crop[ATOMISP_SUBDEV_PAD_SINK]->height;
		}

		if (crop[ATOMISP_SUBDEV_PAD_SINK]->width == r->width
		    && crop[ATOMISP_SUBDEV_PAD_SINK]->height == r->height)
			isp_sd->params.yuv_ds_en = false;
		else
			isp_sd->params.yuv_ds_en = true;

		comp[pad]->width = r->width;
		comp[pad]->height = r->height;

		if (r->width == 0 || r->height == 0 ||
		    crop[ATOMISP_SUBDEV_PAD_SINK]->width == 0 ||
		    crop[ATOMISP_SUBDEV_PAD_SINK]->height == 0)
			break;
		/*
		 * do cropping on sensor input if ratio of required resolution
		 * is different with sensor output resolution ratio:
		 *
		 * ratio = width / height
		 *
		 * if ratio_output < ratio_sensor:
		 *	effect_width = sensor_height * out_width / out_height;
		 *	effect_height = sensor_height;
		 * else
		 *	effect_width = sensor_width;
		 *	effect_height = sensor_width * out_height / out_width;
		 *
		 */
		if (r->width * crop[ATOMISP_SUBDEV_PAD_SINK]->height <
		    crop[ATOMISP_SUBDEV_PAD_SINK]->width * r->height)
			atomisp_css_input_set_effective_resolution(isp_sd,
				ATOMISP_INPUT_STREAM_GENERAL,
				rounddown(crop[ATOMISP_SUBDEV_PAD_SINK]->
					  height * r->width / r->height,
					  ATOM_ISP_STEP_WIDTH),
				crop[ATOMISP_SUBDEV_PAD_SINK]->height);
		else
			atomisp_css_input_set_effective_resolution(isp_sd,
				ATOMISP_INPUT_STREAM_GENERAL,
				crop[ATOMISP_SUBDEV_PAD_SINK]->width,
				rounddown(crop[ATOMISP_SUBDEV_PAD_SINK]->
					  width * r->height / r->width,
					  ATOM_ISP_STEP_WIDTH));

		break;
	}
	case ATOMISP_SUBDEV_PAD_SOURCE_VF:
	case ATOMISP_SUBDEV_PAD_SOURCE_PREVIEW:
		comp[pad]->width = r->width;
		comp[pad]->height = r->height;
		break;
	default:
		return -EINVAL;
	}

	/* Set format dimensions on non-sink pads as well. */
	if (pad != ATOMISP_SUBDEV_PAD_SINK) {
		ffmt[pad]->width = comp[pad]->width;
		ffmt[pad]->height = comp[pad]->height;
	}

	if (!atomisp_subdev_get_rect(sd, sd_state, which, pad, target))
		return -EINVAL;
	*r = *atomisp_subdev_get_rect(sd, sd_state, which, pad, target);

	dev_dbg(isp->dev, "sel actual: l %d t %d w %d h %d\n",
		r->left, r->top, r->width, r->height);

	return 0;
}

static int isp_subdev_set_selection(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *sd_state,
				    struct v4l2_subdev_selection *sel)
{
	int rval = isp_subdev_validate_rect(sd, sel->pad, sel->target);

	if (rval)
		return rval;

	return atomisp_subdev_set_selection(sd, sd_state, sel->which,
					    sel->pad,
					    sel->target, sel->flags, &sel->r);
}

void atomisp_subdev_set_ffmt(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state,
			     uint32_t which,
			     u32 pad, struct v4l2_mbus_framefmt *ffmt)
{
	struct atomisp_sub_device *isp_sd = v4l2_get_subdevdata(sd);
	struct atomisp_device *isp = isp_sd->isp;
	struct v4l2_mbus_framefmt *__ffmt =
	    atomisp_subdev_get_ffmt(sd, sd_state, which, pad);

	dev_dbg(isp->dev, "ffmt: pad %s w %d h %d code 0x%8.8x which %s\n",
		atomisp_pad_str(pad), ffmt->width, ffmt->height, ffmt->code,
		which == V4L2_SUBDEV_FORMAT_TRY ? "V4L2_SUBDEV_FORMAT_TRY"
		: "V4L2_SUBDEV_FORMAT_ACTIVE");

	switch (pad) {
	case ATOMISP_SUBDEV_PAD_SINK: {
		const struct atomisp_in_fmt_conv *fc =
		    atomisp_find_in_fmt_conv(ffmt->code);

		if (!fc) {
			fc = atomisp_in_fmt_conv;
			ffmt->code = fc->code;
			dev_dbg(isp->dev, "using 0x%8.8x instead\n",
				ffmt->code);
		}

		*__ffmt = *ffmt;

		isp_subdev_propagate(sd, sd_state, which, pad,
				     V4L2_SEL_TGT_CROP, 0);

		if (which == V4L2_SUBDEV_FORMAT_ACTIVE) {
			atomisp_css_input_set_resolution(isp_sd,
							 ATOMISP_INPUT_STREAM_GENERAL, ffmt);
			atomisp_css_input_set_binning_factor(isp_sd,
							     ATOMISP_INPUT_STREAM_GENERAL,
							     0);
			atomisp_css_input_set_bayer_order(isp_sd, ATOMISP_INPUT_STREAM_GENERAL,
							  fc->bayer_order);
			atomisp_css_input_set_format(isp_sd, ATOMISP_INPUT_STREAM_GENERAL,
						     fc->atomisp_in_fmt);
			atomisp_css_set_default_isys_config(isp_sd, ATOMISP_INPUT_STREAM_GENERAL,
							    ffmt);
		}

		break;
	}
	case ATOMISP_SUBDEV_PAD_SOURCE_CAPTURE:
	case ATOMISP_SUBDEV_PAD_SOURCE_PREVIEW:
	case ATOMISP_SUBDEV_PAD_SOURCE_VF:
	case ATOMISP_SUBDEV_PAD_SOURCE_VIDEO:
		__ffmt->code = ffmt->code;
		break;
	}
}

/*
 * isp_subdev_get_format - Retrieve the video format on a pad
 * @sd : ISP V4L2 subdevice
 * @fh : V4L2 subdev file handle
 * @pad: Pad number
 * @fmt: Format
 *
 * Return 0 on success or -EINVAL if the pad is invalid or doesn't correspond
 * to the format type.
 */
static int isp_subdev_get_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	fmt->format = *atomisp_subdev_get_ffmt(sd, sd_state, fmt->which,
					       fmt->pad);

	return 0;
}

/*
 * isp_subdev_set_format - Set the video format on a pad
 * @sd : ISP subdev V4L2 subdevice
 * @fh : V4L2 subdev file handle
 * @pad: Pad number
 * @fmt: Format
 *
 * Return 0 on success or -EINVAL if the pad is invalid or doesn't correspond
 * to the format type.
 */
static int isp_subdev_set_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	atomisp_subdev_set_ffmt(sd, sd_state, fmt->which, fmt->pad,
				&fmt->format);

	return 0;
}

/* V4L2 subdev core operations */
static const struct v4l2_subdev_core_ops isp_subdev_v4l2_core_ops = {
	.ioctl = isp_subdev_ioctl, .s_power = isp_subdev_set_power,
	.subscribe_event = isp_subdev_subscribe_event,
	.unsubscribe_event = isp_subdev_unsubscribe_event,
};

/* V4L2 subdev pad operations */
static const struct v4l2_subdev_pad_ops isp_subdev_v4l2_pad_ops = {
	.enum_mbus_code = isp_subdev_enum_mbus_code,
	.get_fmt = isp_subdev_get_format,
	.set_fmt = isp_subdev_set_format,
	.get_selection = isp_subdev_get_selection,
	.set_selection = isp_subdev_set_selection,
	.link_validate = v4l2_subdev_link_validate_default,
};

/* V4L2 subdev operations */
static const struct v4l2_subdev_ops isp_subdev_v4l2_ops = {
	.core = &isp_subdev_v4l2_core_ops,
	.pad = &isp_subdev_v4l2_pad_ops,
};

static void isp_subdev_init_params(struct atomisp_sub_device *asd)
{
	unsigned int i;

	/* parameters initialization */
	INIT_LIST_HEAD(&asd->s3a_stats);
	INIT_LIST_HEAD(&asd->s3a_stats_in_css);
	INIT_LIST_HEAD(&asd->s3a_stats_ready);
	INIT_LIST_HEAD(&asd->dis_stats);
	INIT_LIST_HEAD(&asd->dis_stats_in_css);
	spin_lock_init(&asd->dis_stats_lock);
	for (i = 0; i < ATOMISP_METADATA_TYPE_NUM; i++) {
		INIT_LIST_HEAD(&asd->metadata[i]);
		INIT_LIST_HEAD(&asd->metadata_in_css[i]);
		INIT_LIST_HEAD(&asd->metadata_ready[i]);
	}
}

/* media operations */
static const struct media_entity_operations isp_subdev_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
	/*	 .set_power = v4l2_subdev_set_power,	*/
};

static int __atomisp_update_run_mode(struct atomisp_sub_device *asd)
{
	struct atomisp_device *isp = asd->isp;
	struct v4l2_ctrl *ctrl = asd->run_mode;
	struct v4l2_ctrl *c;
	s32 mode;

	mode = ctrl->val;

	c = v4l2_ctrl_find(
		isp->inputs[asd->input_curr].camera->ctrl_handler,
		V4L2_CID_RUN_MODE);

	if (c)
		return v4l2_ctrl_s_ctrl(c, mode);

	return 0;
}

int atomisp_update_run_mode(struct atomisp_sub_device *asd)
{
	int rval;

	mutex_lock(asd->ctrl_handler.lock);
	rval = __atomisp_update_run_mode(asd);
	mutex_unlock(asd->ctrl_handler.lock);

	return rval;
}

static int s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct atomisp_sub_device *asd = container_of(
					     ctrl->handler, struct atomisp_sub_device, ctrl_handler);
	switch (ctrl->id) {
	case V4L2_CID_RUN_MODE:
		return __atomisp_update_run_mode(asd);
	}

	return 0;
}

static const struct v4l2_ctrl_ops ctrl_ops = {
	.s_ctrl = &s_ctrl,
};

static const char *const ctrl_run_mode_menu[] = {
	NULL,
	"Video",
	"Still capture",
	"Continuous capture",
	"Preview",
};

static const struct v4l2_ctrl_config ctrl_run_mode = {
	.ops = &ctrl_ops,
	.id = V4L2_CID_RUN_MODE,
	.name = "Atomisp run mode",
	.type = V4L2_CTRL_TYPE_MENU,
	.min = 1,
	.def = 1,
	.max = 4,
	.qmenu = ctrl_run_mode_menu,
};

static const char *const ctrl_vfpp_mode_menu[] = {
	"Enable",			/* vfpp always enabled */
	"Disable to scaler mode",	/* CSS into video mode and disable */
	"Disable to low latency mode",	/* CSS into still mode and disable */
};

static const struct v4l2_ctrl_config ctrl_vfpp = {
	.id = V4L2_CID_VFPP,
	.name = "Atomisp vf postprocess",
	.type = V4L2_CTRL_TYPE_MENU,
	.min = 0,
	.def = 0,
	.max = 2,
	.qmenu = ctrl_vfpp_mode_menu,
};

/*
 * Control for continuous mode raw buffer size
 *
 * The size of the RAW ringbuffer sets limit on how much
 * back in time application can go when requesting capture
 * frames to be rendered, and how many frames can be rendered
 * in a burst at full sensor rate.
 *
 * Note: this setting has a big impact on memory consumption of
 * the CSS subsystem.
 */
static const struct v4l2_ctrl_config ctrl_continuous_raw_buffer_size = {
	.ops = &ctrl_ops,
	.id = V4L2_CID_ATOMISP_CONTINUOUS_RAW_BUFFER_SIZE,
	.type = V4L2_CTRL_TYPE_INTEGER,
	.name = "Continuous raw ringbuffer size",
	.min = 1,
	.max = 100, /* depends on CSS version, runtime checked */
	.step = 1,
	.def = 3,
};

/*
 * Control for enabling continuous viewfinder
 *
 * When enabled, and ISP is in continuous mode (see ctrl_continuous_mode ),
 * preview pipeline continues concurrently with capture
 * processing. When disabled, and continuous mode is used,
 * preview is paused while captures are processed, but
 * full pipeline restart is not needed.
 *
 * By setting this to disabled, capture processing is
 * essentially given priority over preview, and the effective
 * capture output rate may be higher than with continuous
 * viewfinder enabled.
 */
static const struct v4l2_ctrl_config ctrl_continuous_viewfinder = {
	.id = V4L2_CID_ATOMISP_CONTINUOUS_VIEWFINDER,
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.name = "Continuous viewfinder",
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 0,
};

/*
 * Control for enabling Lock&Unlock Raw Buffer mechanism
 *
 * When enabled, Raw Buffer can be locked and unlocked.
 * Application can hold the exp_id of Raw Buffer
 * and unlock it when no longer needed.
 * Note: Make sure set this configuration before creating stream.
 */
static const struct v4l2_ctrl_config ctrl_enable_raw_buffer_lock = {
	.id = V4L2_CID_ENABLE_RAW_BUFFER_LOCK,
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.name = "Lock Unlock Raw Buffer",
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 0,
};

/*
 * Control to disable digital zoom of the whole stream
 *
 * When it is true, pipe configuration enable_dz will be set to false.
 * This can help get a better performance by disabling pp binary.
 *
 * Note: Make sure set this configuration before creating stream.
 */
static const struct v4l2_ctrl_config ctrl_disable_dz = {
	.id = V4L2_CID_DISABLE_DZ,
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.name = "Disable digital zoom",
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 0,
};

static int atomisp_init_subdev_pipe(struct atomisp_sub_device *asd,
				    struct atomisp_video_pipe *pipe, enum v4l2_buf_type buf_type)
{
	int ret;

	pipe->type = buf_type;
	pipe->asd = asd;
	pipe->isp = asd->isp;
	spin_lock_init(&pipe->irq_lock);
	mutex_init(&pipe->vb_queue_mutex);

	/* Init videobuf2 queue structure */
	pipe->vb_queue.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	pipe->vb_queue.io_modes = VB2_MMAP | VB2_USERPTR;
	pipe->vb_queue.buf_struct_size = sizeof(struct ia_css_frame);
	pipe->vb_queue.ops = &atomisp_vb2_ops;
	pipe->vb_queue.mem_ops = &vb2_vmalloc_memops;
	pipe->vb_queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	ret = vb2_queue_init(&pipe->vb_queue);
	if (ret)
		return ret;

	pipe->vdev.queue = &pipe->vb_queue;
	pipe->vdev.queue->lock = &pipe->vb_queue_mutex;

	INIT_LIST_HEAD(&pipe->buffers_in_css);
	INIT_LIST_HEAD(&pipe->activeq);
	INIT_LIST_HEAD(&pipe->buffers_waiting_for_param);
	INIT_LIST_HEAD(&pipe->per_frame_params);

	return 0;
}

/*
 * isp_subdev_init_entities - Initialize V4L2 subdev and media entity
 * @asd: ISP CCDC module
 *
 * Return 0 on success and a negative error code on failure.
 */
static int isp_subdev_init_entities(struct atomisp_sub_device *asd)
{
	struct v4l2_subdev *sd = &asd->subdev;
	struct media_pad *pads = asd->pads;
	struct media_entity *me = &sd->entity;
	int ret;

	v4l2_subdev_init(sd, &isp_subdev_v4l2_ops);
	sprintf(sd->name, "ATOMISP_SUBDEV");
	v4l2_set_subdevdata(sd, asd);
	sd->flags |= V4L2_SUBDEV_FL_HAS_EVENTS | V4L2_SUBDEV_FL_HAS_DEVNODE;

	pads[ATOMISP_SUBDEV_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[ATOMISP_SUBDEV_PAD_SOURCE_PREVIEW].flags = MEDIA_PAD_FL_SOURCE;
	pads[ATOMISP_SUBDEV_PAD_SOURCE_VF].flags = MEDIA_PAD_FL_SOURCE;
	pads[ATOMISP_SUBDEV_PAD_SOURCE_CAPTURE].flags = MEDIA_PAD_FL_SOURCE;
	pads[ATOMISP_SUBDEV_PAD_SOURCE_VIDEO].flags = MEDIA_PAD_FL_SOURCE;

	asd->fmt[ATOMISP_SUBDEV_PAD_SINK].fmt.code =
	    MEDIA_BUS_FMT_SBGGR10_1X10;
	asd->fmt[ATOMISP_SUBDEV_PAD_SOURCE_PREVIEW].fmt.code =
	    MEDIA_BUS_FMT_SBGGR10_1X10;
	asd->fmt[ATOMISP_SUBDEV_PAD_SOURCE_VF].fmt.code =
	    MEDIA_BUS_FMT_SBGGR10_1X10;
	asd->fmt[ATOMISP_SUBDEV_PAD_SOURCE_CAPTURE].fmt.code =
	    MEDIA_BUS_FMT_SBGGR10_1X10;
	asd->fmt[ATOMISP_SUBDEV_PAD_SOURCE_VIDEO].fmt.code =
	    MEDIA_BUS_FMT_SBGGR10_1X10;

	me->ops = &isp_subdev_media_ops;
	me->function = MEDIA_ENT_F_PROC_VIDEO_ISP;
	ret = media_entity_pads_init(me, ATOMISP_SUBDEV_PADS_NUM, pads);
	if (ret < 0)
		return ret;

	ret = atomisp_init_subdev_pipe(asd, &asd->video_out_preview,
				       V4L2_BUF_TYPE_VIDEO_CAPTURE);
	if (ret)
		return ret;

	ret = atomisp_init_subdev_pipe(asd, &asd->video_out_vf,
				       V4L2_BUF_TYPE_VIDEO_CAPTURE);
	if (ret)
		return ret;

	ret = atomisp_init_subdev_pipe(asd, &asd->video_out_capture,
				       V4L2_BUF_TYPE_VIDEO_CAPTURE);
	if (ret)
		return ret;

	ret = atomisp_init_subdev_pipe(asd, &asd->video_out_video_capture,
				       V4L2_BUF_TYPE_VIDEO_CAPTURE);
	if (ret)
		return ret;

	ret = atomisp_video_init(&asd->video_out_capture, "CAPTURE",
				 ATOMISP_RUN_MODE_STILL_CAPTURE);
	if (ret < 0)
		return ret;

	ret = atomisp_video_init(&asd->video_out_vf, "VIEWFINDER",
				 ATOMISP_RUN_MODE_CONTINUOUS_CAPTURE);
	if (ret < 0)
		return ret;

	ret = atomisp_video_init(&asd->video_out_preview, "PREVIEW",
				 ATOMISP_RUN_MODE_PREVIEW);
	if (ret < 0)
		return ret;

	ret = atomisp_video_init(&asd->video_out_video_capture, "VIDEO",
				 ATOMISP_RUN_MODE_VIDEO);
	if (ret < 0)
		return ret;

	ret = v4l2_ctrl_handler_init(&asd->ctrl_handler, 1);
	if (ret)
		return ret;

	asd->run_mode = v4l2_ctrl_new_custom(&asd->ctrl_handler,
					     &ctrl_run_mode, NULL);
	asd->vfpp = v4l2_ctrl_new_custom(&asd->ctrl_handler,
					 &ctrl_vfpp, NULL);
	asd->continuous_viewfinder = v4l2_ctrl_new_custom(&asd->ctrl_handler,
				     &ctrl_continuous_viewfinder,
				     NULL);
	asd->continuous_raw_buffer_size =
	    v4l2_ctrl_new_custom(&asd->ctrl_handler,
				 &ctrl_continuous_raw_buffer_size,
				 NULL);

	asd->enable_raw_buffer_lock =
	    v4l2_ctrl_new_custom(&asd->ctrl_handler,
				 &ctrl_enable_raw_buffer_lock,
				 NULL);
	asd->disable_dz =
	    v4l2_ctrl_new_custom(&asd->ctrl_handler,
				 &ctrl_disable_dz,
				 NULL);

	/* Make controls visible on subdev as well. */
	asd->subdev.ctrl_handler = &asd->ctrl_handler;
	spin_lock_init(&asd->raw_buffer_bitmap_lock);
	return asd->ctrl_handler.error;
}

int atomisp_create_pads_links(struct atomisp_device *isp)
{
	int i, ret;

	for (i = 0; i < ATOMISP_CAMERA_NR_PORTS; i++) {
		ret = media_create_pad_link(&isp->csi2_port[i].subdev.entity,
					    CSI2_PAD_SOURCE, &isp->asd.subdev.entity,
					    ATOMISP_SUBDEV_PAD_SINK, 0);
		if (ret < 0)
			return ret;
	}

	for (i = 0; i < isp->input_cnt; i++) {
		/* Don't create links for the test-pattern-generator */
		if (isp->inputs[i].type == TEST_PATTERN)
			continue;

		ret = media_create_pad_link(&isp->inputs[i].camera->entity, 0,
					    &isp->csi2_port[isp->inputs[i].
						    port].subdev.entity,
					    CSI2_PAD_SINK,
					    MEDIA_LNK_FL_ENABLED |
					    MEDIA_LNK_FL_IMMUTABLE);
		if (ret < 0)
			return ret;
	}

	ret = media_create_pad_link(&isp->asd.subdev.entity,
				    ATOMISP_SUBDEV_PAD_SOURCE_PREVIEW,
				    &isp->asd.video_out_preview.vdev.entity, 0, 0);
	if (ret < 0)
		return ret;
	ret = media_create_pad_link(&isp->asd.subdev.entity,
				    ATOMISP_SUBDEV_PAD_SOURCE_VF,
				    &isp->asd.video_out_vf.vdev.entity, 0, 0);
	if (ret < 0)
		return ret;
	ret = media_create_pad_link(&isp->asd.subdev.entity,
				    ATOMISP_SUBDEV_PAD_SOURCE_CAPTURE,
				    &isp->asd.video_out_capture.vdev.entity, 0, 0);
	if (ret < 0)
		return ret;
	ret = media_create_pad_link(&isp->asd.subdev.entity,
				    ATOMISP_SUBDEV_PAD_SOURCE_VIDEO,
				    &isp->asd.video_out_video_capture.vdev.entity, 0, 0);
	if (ret < 0)
		return ret;

	return 0;
}

static void atomisp_subdev_cleanup_entities(struct atomisp_sub_device *asd)
{
	v4l2_ctrl_handler_free(&asd->ctrl_handler);

	media_entity_cleanup(&asd->subdev.entity);
}

void atomisp_subdev_cleanup_pending_events(struct atomisp_sub_device *asd)
{
	struct v4l2_fh *fh, *fh_tmp;
	struct v4l2_event event;
	unsigned int i, pending_event;

	list_for_each_entry_safe(fh, fh_tmp,
				 &asd->subdev.devnode->fh_list, list) {
		pending_event = v4l2_event_pending(fh);
		for (i = 0; i < pending_event; i++)
			v4l2_event_dequeue(fh, &event, 1);
	}
}

void atomisp_subdev_unregister_entities(struct atomisp_sub_device *asd)
{
	atomisp_subdev_cleanup_entities(asd);
	v4l2_device_unregister_subdev(&asd->subdev);
	atomisp_video_unregister(&asd->video_out_preview);
	atomisp_video_unregister(&asd->video_out_vf);
	atomisp_video_unregister(&asd->video_out_capture);
	atomisp_video_unregister(&asd->video_out_video_capture);
}

int atomisp_subdev_register_subdev(struct atomisp_sub_device *asd,
				   struct v4l2_device *vdev)
{
	return v4l2_device_register_subdev(vdev, &asd->subdev);
}

int atomisp_subdev_register_video_nodes(struct atomisp_sub_device *asd,
					struct v4l2_device *vdev)
{
	int ret;

	/*
	 * FIXME: check if all device caps are properly initialized.
	 * Should any of those use V4L2_CAP_META_CAPTURE? Probably yes.
	 */

	asd->video_out_preview.vdev.v4l2_dev = vdev;
	asd->video_out_preview.vdev.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	ret = video_register_device(&asd->video_out_preview.vdev,
				    VFL_TYPE_VIDEO, -1);
	if (ret < 0)
		goto error;

	asd->video_out_capture.vdev.v4l2_dev = vdev;
	asd->video_out_capture.vdev.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	ret = video_register_device(&asd->video_out_capture.vdev,
				    VFL_TYPE_VIDEO, -1);
	if (ret < 0)
		goto error;

	asd->video_out_vf.vdev.v4l2_dev = vdev;
	asd->video_out_vf.vdev.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	ret = video_register_device(&asd->video_out_vf.vdev,
				    VFL_TYPE_VIDEO, -1);
	if (ret < 0)
		goto error;

	asd->video_out_video_capture.vdev.v4l2_dev = vdev;
	asd->video_out_video_capture.vdev.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	ret = video_register_device(&asd->video_out_video_capture.vdev,
				    VFL_TYPE_VIDEO, -1);
	if (ret < 0)
		goto error;

	return 0;

error:
	atomisp_subdev_unregister_entities(asd);
	return ret;
}

/*
 * atomisp_subdev_init - ISP Subdevice  initialization.
 * @dev: Device pointer specific to the ATOM ISP.
 *
 * TODO: Get the initialisation values from platform data.
 *
 * Return 0 on success or a negative error code otherwise.
 */
int atomisp_subdev_init(struct atomisp_device *isp)
{
	int ret;

	isp->asd.isp = isp;
	isp_subdev_init_params(&isp->asd);
	ret = isp_subdev_init_entities(&isp->asd);
	if (ret < 0)
		atomisp_subdev_cleanup_entities(&isp->asd);

	return ret;
}
