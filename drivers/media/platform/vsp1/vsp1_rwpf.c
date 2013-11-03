/*
 * vsp1_rwpf.c  --  R-Car VSP1 Read and Write Pixel Formatters
 *
 * Copyright (C) 2013 Renesas Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_rwpf.h"
#include "vsp1_video.h"

#define RWPF_MIN_WIDTH				1
#define RWPF_MIN_HEIGHT				1

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

int vsp1_rwpf_enum_mbus_code(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_fh *fh,
			     struct v4l2_subdev_mbus_code_enum *code)
{
	static const unsigned int codes[] = {
		V4L2_MBUS_FMT_ARGB8888_1X32,
		V4L2_MBUS_FMT_AYUV8_1X32,
	};

	if (code->index >= ARRAY_SIZE(codes))
		return -EINVAL;

	code->code = codes[code->index];

	return 0;
}

int vsp1_rwpf_enum_frame_size(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_fh *fh,
			      struct v4l2_subdev_frame_size_enum *fse)
{
	struct vsp1_rwpf *rwpf = to_rwpf(subdev);
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(fh, fse->pad);

	if (fse->index || fse->code != format->code)
		return -EINVAL;

	if (fse->pad == RWPF_PAD_SINK) {
		fse->min_width = RWPF_MIN_WIDTH;
		fse->max_width = rwpf->max_width;
		fse->min_height = RWPF_MIN_HEIGHT;
		fse->max_height = rwpf->max_height;
	} else {
		/* The size on the source pad are fixed and always identical to
		 * the size on the sink pad.
		 */
		fse->min_width = format->width;
		fse->max_width = format->width;
		fse->min_height = format->height;
		fse->max_height = format->height;
	}

	return 0;
}

int vsp1_rwpf_get_format(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh,
			 struct v4l2_subdev_format *fmt)
{
	struct vsp1_rwpf *rwpf = to_rwpf(subdev);

	fmt->format = *vsp1_entity_get_pad_format(&rwpf->entity, fh, fmt->pad,
						  fmt->which);

	return 0;
}

int vsp1_rwpf_set_format(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh,
			 struct v4l2_subdev_format *fmt)
{
	struct vsp1_rwpf *rwpf = to_rwpf(subdev);
	struct v4l2_mbus_framefmt *format;

	/* Default to YUV if the requested format is not supported. */
	if (fmt->format.code != V4L2_MBUS_FMT_ARGB8888_1X32 &&
	    fmt->format.code != V4L2_MBUS_FMT_AYUV8_1X32)
		fmt->format.code = V4L2_MBUS_FMT_AYUV8_1X32;

	format = vsp1_entity_get_pad_format(&rwpf->entity, fh, fmt->pad,
					    fmt->which);

	if (fmt->pad == RWPF_PAD_SOURCE) {
		/* The RWPF performs format conversion but can't scale, only the
		 * format code can be changed on the source pad.
		 */
		format->code = fmt->format.code;
		fmt->format = *format;
		return 0;
	}

	format->code = fmt->format.code;
	format->width = clamp_t(unsigned int, fmt->format.width,
				RWPF_MIN_WIDTH, rwpf->max_width);
	format->height = clamp_t(unsigned int, fmt->format.height,
				 RWPF_MIN_HEIGHT, rwpf->max_height);
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_SRGB;

	fmt->format = *format;

	/* Propagate the format to the source pad. */
	format = vsp1_entity_get_pad_format(&rwpf->entity, fh, RWPF_PAD_SOURCE,
					    fmt->which);
	*format = fmt->format;

	return 0;
}
