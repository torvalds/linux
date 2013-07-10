/*
 * vsp1_uds.c  --  R-Car VSP1 Up and Down Scaler
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/gfp.h>

#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_uds.h"

#define UDS_MIN_SIZE				4U
#define UDS_MAX_SIZE				8190U

#define UDS_MIN_FACTOR				0x0100
#define UDS_MAX_FACTOR				0xffff

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline u32 vsp1_uds_read(struct vsp1_uds *uds, u32 reg)
{
	return vsp1_read(uds->entity.vsp1,
			 reg + uds->entity.index * VI6_UDS_OFFSET);
}

static inline void vsp1_uds_write(struct vsp1_uds *uds, u32 reg, u32 data)
{
	vsp1_write(uds->entity.vsp1,
		   reg + uds->entity.index * VI6_UDS_OFFSET, data);
}

/* -----------------------------------------------------------------------------
 * Scaling Computation
 */

/*
 * uds_output_size - Return the output size for an input size and scaling ratio
 * @input: input size in pixels
 * @ratio: scaling ratio in U4.12 fixed-point format
 */
static unsigned int uds_output_size(unsigned int input, unsigned int ratio)
{
	if (ratio > 4096) {
		/* Down-scaling */
		unsigned int mp;

		mp = ratio / 4096;
		mp = mp < 4 ? 1 : (mp < 8 ? 2 : 4);

		return (input - 1) / mp * mp * 4096 / ratio + 1;
	} else {
		/* Up-scaling */
		return (input - 1) * 4096 / ratio + 1;
	}
}

/*
 * uds_output_limits - Return the min and max output sizes for an input size
 * @input: input size in pixels
 * @minimum: minimum output size (returned)
 * @maximum: maximum output size (returned)
 */
static void uds_output_limits(unsigned int input,
			      unsigned int *minimum, unsigned int *maximum)
{
	*minimum = max(uds_output_size(input, UDS_MAX_FACTOR), UDS_MIN_SIZE);
	*maximum = min(uds_output_size(input, UDS_MIN_FACTOR), UDS_MAX_SIZE);
}

/*
 * uds_passband_width - Return the passband filter width for a scaling ratio
 * @ratio: scaling ratio in U4.12 fixed-point format
 */
static unsigned int uds_passband_width(unsigned int ratio)
{
	if (ratio >= 4096) {
		/* Down-scaling */
		unsigned int mp;

		mp = ratio / 4096;
		mp = mp < 4 ? 1 : (mp < 8 ? 2 : 4);

		return 64 * 4096 * mp / ratio;
	} else {
		/* Up-scaling */
		return 64;
	}
}

static unsigned int uds_compute_ratio(unsigned int input, unsigned int output)
{
	/* TODO: This is an approximation that will need to be refined. */
	return (input - 1) * 4096 / (output - 1);
}

static void uds_compute_ratios(struct vsp1_uds *uds)
{
	struct v4l2_mbus_framefmt *input = &uds->entity.formats[UDS_PAD_SINK];
	struct v4l2_mbus_framefmt *output =
		&uds->entity.formats[UDS_PAD_SOURCE];

	uds->hscale = uds_compute_ratio(input->width, output->width);
	uds->vscale = uds_compute_ratio(input->height, output->height);

	dev_dbg(uds->entity.vsp1->dev, "hscale %u vscale %u\n",
		uds->hscale, uds->vscale);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Core Operations
 */

static int uds_s_stream(struct v4l2_subdev *subdev, int enable)
{
	const struct v4l2_mbus_framefmt *format;
	struct vsp1_uds *uds = to_uds(subdev);

	if (!enable)
		return 0;

	/* Enable multi-tap scaling. */
	vsp1_uds_write(uds, VI6_UDS_CTRL, VI6_UDS_CTRL_AON | VI6_UDS_CTRL_BC);

	vsp1_uds_write(uds, VI6_UDS_PASS_BWIDTH,
		       (uds_passband_width(uds->hscale)
				<< VI6_UDS_PASS_BWIDTH_H_SHIFT) |
		       (uds_passband_width(uds->vscale)
				<< VI6_UDS_PASS_BWIDTH_V_SHIFT));

	/* Set the scaling ratios and the output size. */
	format = &uds->entity.formats[UDS_PAD_SOURCE];

	vsp1_uds_write(uds, VI6_UDS_SCALE,
		       (uds->hscale << VI6_UDS_SCALE_HFRAC_SHIFT) |
		       (uds->vscale << VI6_UDS_SCALE_VFRAC_SHIFT));
	vsp1_uds_write(uds, VI6_UDS_CLIP_SIZE,
		       (format->width << VI6_UDS_CLIP_SIZE_HSIZE_SHIFT) |
		       (format->height << VI6_UDS_CLIP_SIZE_VSIZE_SHIFT));

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

static int uds_enum_mbus_code(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_fh *fh,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	static const unsigned int codes[] = {
		V4L2_MBUS_FMT_ARGB8888_1X32,
		V4L2_MBUS_FMT_AYUV8_1X32,
	};

	if (code->pad == UDS_PAD_SINK) {
		if (code->index >= ARRAY_SIZE(codes))
			return -EINVAL;

		code->code = codes[code->index];
	} else {
		struct v4l2_mbus_framefmt *format;

		/* The UDS can't perform format conversion, the sink format is
		 * always identical to the source format.
		 */
		if (code->index)
			return -EINVAL;

		format = v4l2_subdev_get_try_format(fh, UDS_PAD_SINK);
		code->code = format->code;
	}

	return 0;
}

static int uds_enum_frame_size(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_fh *fh,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(fh, UDS_PAD_SINK);

	if (fse->index || fse->code != format->code)
		return -EINVAL;

	if (fse->pad == UDS_PAD_SINK) {
		fse->min_width = UDS_MIN_SIZE;
		fse->max_width = UDS_MAX_SIZE;
		fse->min_height = UDS_MIN_SIZE;
		fse->max_height = UDS_MAX_SIZE;
	} else {
		uds_output_limits(format->width, &fse->min_width,
				  &fse->max_width);
		uds_output_limits(format->height, &fse->min_height,
				  &fse->max_height);
	}

	return 0;
}

static int uds_get_format(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh,
			  struct v4l2_subdev_format *fmt)
{
	struct vsp1_uds *uds = to_uds(subdev);

	fmt->format = *vsp1_entity_get_pad_format(&uds->entity, fh, fmt->pad,
						  fmt->which);

	return 0;
}

static void uds_try_format(struct vsp1_uds *uds, struct v4l2_subdev_fh *fh,
			   unsigned int pad, struct v4l2_mbus_framefmt *fmt,
			   enum v4l2_subdev_format_whence which)
{
	struct v4l2_mbus_framefmt *format;
	unsigned int minimum;
	unsigned int maximum;

	switch (pad) {
	case UDS_PAD_SINK:
		/* Default to YUV if the requested format is not supported. */
		if (fmt->code != V4L2_MBUS_FMT_ARGB8888_1X32 &&
		    fmt->code != V4L2_MBUS_FMT_AYUV8_1X32)
			fmt->code = V4L2_MBUS_FMT_AYUV8_1X32;

		fmt->width = clamp(fmt->width, UDS_MIN_SIZE, UDS_MAX_SIZE);
		fmt->height = clamp(fmt->height, UDS_MIN_SIZE, UDS_MAX_SIZE);
		break;

	case UDS_PAD_SOURCE:
		/* The UDS scales but can't perform format conversion. */
		format = vsp1_entity_get_pad_format(&uds->entity, fh,
						    UDS_PAD_SINK, which);
		fmt->code = format->code;

		uds_output_limits(format->width, &minimum, &maximum);
		fmt->width = clamp(fmt->width, minimum, maximum);
		uds_output_limits(format->height, &minimum, &maximum);
		fmt->height = clamp(fmt->height, minimum, maximum);
		break;
	}

	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
}

static int uds_set_format(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh,
			  struct v4l2_subdev_format *fmt)
{
	struct vsp1_uds *uds = to_uds(subdev);
	struct v4l2_mbus_framefmt *format;

	uds_try_format(uds, fh, fmt->pad, &fmt->format, fmt->which);

	format = vsp1_entity_get_pad_format(&uds->entity, fh, fmt->pad,
					    fmt->which);
	*format = fmt->format;

	if (fmt->pad == UDS_PAD_SINK) {
		/* Propagate the format to the source pad. */
		format = vsp1_entity_get_pad_format(&uds->entity, fh,
						    UDS_PAD_SOURCE, fmt->which);
		*format = fmt->format;

		uds_try_format(uds, fh, UDS_PAD_SOURCE, format, fmt->which);
	}

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		uds_compute_ratios(uds);

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static struct v4l2_subdev_video_ops uds_video_ops = {
	.s_stream = uds_s_stream,
};

static struct v4l2_subdev_pad_ops uds_pad_ops = {
	.enum_mbus_code = uds_enum_mbus_code,
	.enum_frame_size = uds_enum_frame_size,
	.get_fmt = uds_get_format,
	.set_fmt = uds_set_format,
};

static struct v4l2_subdev_ops uds_ops = {
	.video	= &uds_video_ops,
	.pad    = &uds_pad_ops,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_uds *vsp1_uds_create(struct vsp1_device *vsp1, unsigned int index)
{
	struct v4l2_subdev *subdev;
	struct vsp1_uds *uds;
	int ret;

	uds = devm_kzalloc(vsp1->dev, sizeof(*uds), GFP_KERNEL);
	if (uds == NULL)
		return ERR_PTR(-ENOMEM);

	uds->entity.type = VSP1_ENTITY_UDS;
	uds->entity.index = index;

	ret = vsp1_entity_init(vsp1, &uds->entity, 2);
	if (ret < 0)
		return ERR_PTR(ret);

	/* Initialize the V4L2 subdev. */
	subdev = &uds->entity.subdev;
	v4l2_subdev_init(subdev, &uds_ops);

	subdev->entity.ops = &vsp1_media_ops;
	subdev->internal_ops = &vsp1_subdev_internal_ops;
	snprintf(subdev->name, sizeof(subdev->name), "%s uds.%u",
		 dev_name(vsp1->dev), index);
	v4l2_set_subdevdata(subdev, uds);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	vsp1_entity_init_formats(subdev, NULL);

	return uds;
}
