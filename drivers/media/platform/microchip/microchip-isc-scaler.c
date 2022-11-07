// SPDX-License-Identifier: GPL-2.0-only
/*
 * Microchip Image Sensor Controller (ISC) Scaler entity support
 *
 * Copyright (C) 2022 Microchip Technology, Inc.
 *
 * Author: Eugen Hristev <eugen.hristev@microchip.com>
 *
 */

#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include "microchip-isc-regs.h"
#include "microchip-isc.h"

static void isc_scaler_prepare_fmt(struct v4l2_mbus_framefmt *framefmt)
{
	framefmt->colorspace = V4L2_COLORSPACE_SRGB;
	framefmt->field = V4L2_FIELD_NONE;
	framefmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	framefmt->quantization = V4L2_QUANTIZATION_DEFAULT;
	framefmt->xfer_func = V4L2_XFER_FUNC_DEFAULT;
};

static int isc_scaler_get_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_format *format)
{
	struct isc_device *isc = container_of(sd, struct isc_device, scaler_sd);
	struct v4l2_mbus_framefmt *v4l2_try_fmt;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		v4l2_try_fmt = v4l2_subdev_get_try_format(sd, sd_state,
							  format->pad);
		format->format = *v4l2_try_fmt;

		return 0;
	}

	format->format = isc->scaler_format[format->pad];

	return 0;
}

static int isc_scaler_set_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_format *req_fmt)
{
	struct isc_device *isc = container_of(sd, struct isc_device, scaler_sd);
	struct v4l2_mbus_framefmt *v4l2_try_fmt;
	struct isc_format *fmt;
	unsigned int i;

	/* Source format is fixed, we cannot change it */
	if (req_fmt->pad == ISC_SCALER_PAD_SOURCE) {
		req_fmt->format = isc->scaler_format[ISC_SCALER_PAD_SOURCE];
		return 0;
	}

	/* There is no limit on the frame size on the sink pad */
	v4l_bound_align_image(&req_fmt->format.width, 16, UINT_MAX, 0,
			      &req_fmt->format.height, 16, UINT_MAX, 0, 0);

	isc_scaler_prepare_fmt(&req_fmt->format);

	fmt = isc_find_format_by_code(isc, req_fmt->format.code, &i);

	if (!fmt)
		fmt = &isc->formats_list[0];

	req_fmt->format.code = fmt->mbus_code;

	if (req_fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		v4l2_try_fmt = v4l2_subdev_get_try_format(sd, sd_state,
							  req_fmt->pad);
		*v4l2_try_fmt = req_fmt->format;
		/* Trying on the sink pad makes the source pad change too */
		v4l2_try_fmt = v4l2_subdev_get_try_format(sd, sd_state,
							  ISC_SCALER_PAD_SOURCE);
		*v4l2_try_fmt = req_fmt->format;

		v4l_bound_align_image(&v4l2_try_fmt->width,
				      16, isc->max_width, 0,
				      &v4l2_try_fmt->height,
				      16, isc->max_height, 0, 0);
		/* if we are just trying, we are done */
		return 0;
	}

	isc->scaler_format[ISC_SCALER_PAD_SINK] = req_fmt->format;

	/* The source pad is the same as the sink, but we have to crop it */
	isc->scaler_format[ISC_SCALER_PAD_SOURCE] =
		isc->scaler_format[ISC_SCALER_PAD_SINK];
	v4l_bound_align_image
		(&isc->scaler_format[ISC_SCALER_PAD_SOURCE].width, 16,
		 isc->max_width, 0,
		 &isc->scaler_format[ISC_SCALER_PAD_SOURCE].height, 16,
		 isc->max_height, 0, 0);

	return 0;
}

static int isc_scaler_enum_mbus_code(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *sd_state,
				     struct v4l2_subdev_mbus_code_enum *code)
{
	struct isc_device *isc = container_of(sd, struct isc_device, scaler_sd);

	/*
	 * All formats supported by the ISC are supported by the scaler.
	 * Advertise the formats which the ISC can take as input, as the scaler
	 * entity cropping is part of the PFE module (parallel front end)
	 */
	if (code->index < isc->formats_list_size) {
		code->code = isc->formats_list[code->index].mbus_code;
		return 0;
	}

	return -EINVAL;
}

static int isc_scaler_g_sel(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *sd_state,
			    struct v4l2_subdev_selection *sel)
{
	struct isc_device *isc = container_of(sd, struct isc_device, scaler_sd);

	if (sel->pad == ISC_SCALER_PAD_SOURCE)
		return -EINVAL;

	if (sel->target != V4L2_SEL_TGT_CROP_BOUNDS &&
	    sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	sel->r.height = isc->scaler_format[ISC_SCALER_PAD_SOURCE].height;
	sel->r.width = isc->scaler_format[ISC_SCALER_PAD_SOURCE].width;

	sel->r.left = 0;
	sel->r.top = 0;

	return 0;
}

static int isc_scaler_init_cfg(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state)
{
	struct v4l2_mbus_framefmt *v4l2_try_fmt =
		v4l2_subdev_get_try_format(sd, sd_state, 0);
	struct v4l2_rect *try_crop;
	struct isc_device *isc = container_of(sd, struct isc_device, scaler_sd);

	*v4l2_try_fmt = isc->scaler_format[ISC_SCALER_PAD_SOURCE];

	try_crop = v4l2_subdev_get_try_crop(sd, sd_state, 0);

	try_crop->top = 0;
	try_crop->left = 0;
	try_crop->width = v4l2_try_fmt->width;
	try_crop->height = v4l2_try_fmt->height;

	return 0;
}

static const struct v4l2_subdev_pad_ops isc_scaler_pad_ops = {
	.enum_mbus_code = isc_scaler_enum_mbus_code,
	.set_fmt = isc_scaler_set_fmt,
	.get_fmt = isc_scaler_get_fmt,
	.get_selection = isc_scaler_g_sel,
	.init_cfg = isc_scaler_init_cfg,
};

static const struct media_entity_operations isc_scaler_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_ops xisc_scaler_subdev_ops = {
	.pad = &isc_scaler_pad_ops,
};

int isc_scaler_init(struct isc_device *isc)
{
	int ret;

	v4l2_subdev_init(&isc->scaler_sd, &xisc_scaler_subdev_ops);

	isc->scaler_sd.owner = THIS_MODULE;
	isc->scaler_sd.dev = isc->dev;
	snprintf(isc->scaler_sd.name, sizeof(isc->scaler_sd.name),
		 "microchip_isc_scaler");

	isc->scaler_sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	isc->scaler_sd.entity.function = MEDIA_ENT_F_PROC_VIDEO_SCALER;
	isc->scaler_sd.entity.ops = &isc_scaler_entity_ops;
	isc->scaler_pads[ISC_SCALER_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	isc->scaler_pads[ISC_SCALER_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	isc_scaler_prepare_fmt(&isc->scaler_format[ISC_SCALER_PAD_SOURCE]);
	isc->scaler_format[ISC_SCALER_PAD_SOURCE].height = isc->max_height;
	isc->scaler_format[ISC_SCALER_PAD_SOURCE].width = isc->max_width;
	isc->scaler_format[ISC_SCALER_PAD_SOURCE].code =
		 isc->formats_list[0].mbus_code;

	isc->scaler_format[ISC_SCALER_PAD_SINK] =
		 isc->scaler_format[ISC_SCALER_PAD_SOURCE];

	ret = media_entity_pads_init(&isc->scaler_sd.entity,
				     ISC_SCALER_PADS_NUM,
				     isc->scaler_pads);
	if (ret < 0) {
		dev_err(isc->dev, "scaler sd media entity init failed\n");
		return ret;
	}

	ret = v4l2_device_register_subdev(&isc->v4l2_dev, &isc->scaler_sd);
	if (ret < 0) {
		dev_err(isc->dev, "scaler sd failed to register subdev\n");
		return ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(isc_scaler_init);

int isc_scaler_link(struct isc_device *isc)
{
	int ret;

	ret = media_create_pad_link(&isc->current_subdev->sd->entity,
				    isc->remote_pad, &isc->scaler_sd.entity,
				    ISC_SCALER_PAD_SINK,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);

	if (ret < 0) {
		dev_err(isc->dev, "Failed to create pad link: %s to %s\n",
			isc->current_subdev->sd->entity.name,
			isc->scaler_sd.entity.name);
		return ret;
	}

	dev_dbg(isc->dev, "link with %s pad: %d\n",
		isc->current_subdev->sd->name, isc->remote_pad);

	ret = media_create_pad_link(&isc->scaler_sd.entity,
				    ISC_SCALER_PAD_SOURCE,
				    &isc->video_dev.entity, ISC_PAD_SINK,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);

	if (ret < 0) {
		dev_err(isc->dev, "Failed to create pad link: %s to %s\n",
			isc->scaler_sd.entity.name,
			isc->video_dev.entity.name);
		return ret;
	}

	dev_dbg(isc->dev, "link with %s pad: %d\n", isc->scaler_sd.name,
		ISC_SCALER_PAD_SOURCE);

	return ret;
}
EXPORT_SYMBOL_GPL(isc_scaler_link);

