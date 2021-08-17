// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
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

#include <media/v4l2-event.h>
#include <media/v4l2-mediabus.h>
#include "atomisp_internal.h"
#include "atomisp_tpg.h"

static int tpg_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static int tpg_get_fmt(struct v4l2_subdev *sd,
		       struct v4l2_subdev_state *sd_state,
		       struct v4l2_subdev_format *format)
{
	/*to fake*/
	return 0;
}

static int tpg_set_fmt(struct v4l2_subdev *sd,
		       struct v4l2_subdev_state *sd_state,
		       struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;

	if (format->pad)
		return -EINVAL;
	/* only raw8 grbg is supported by TPG */
	fmt->code = MEDIA_BUS_FMT_SGRBG8_1X8;
	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		sd_state->pads->try_fmt = *fmt;
		return 0;
	}
	return 0;
}

static int tpg_log_status(struct v4l2_subdev *sd)
{
	/*to fake*/
	return 0;
}

static int tpg_s_power(struct v4l2_subdev *sd, int on)
{
	return 0;
}

static int tpg_enum_mbus_code(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	/*to fake*/
	return 0;
}

static int tpg_enum_frame_size(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	/*to fake*/
	return 0;
}

static int tpg_enum_frame_ival(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state,
			       struct v4l2_subdev_frame_interval_enum *fie)
{
	/*to fake*/
	return 0;
}

static const struct v4l2_subdev_video_ops tpg_video_ops = {
	.s_stream = tpg_s_stream,
};

static const struct v4l2_subdev_core_ops tpg_core_ops = {
	.log_status = tpg_log_status,
	.s_power = tpg_s_power,
};

static const struct v4l2_subdev_pad_ops tpg_pad_ops = {
	.enum_mbus_code = tpg_enum_mbus_code,
	.enum_frame_size = tpg_enum_frame_size,
	.enum_frame_interval = tpg_enum_frame_ival,
	.get_fmt = tpg_get_fmt,
	.set_fmt = tpg_set_fmt,
};

static const struct v4l2_subdev_ops tpg_ops = {
	.core = &tpg_core_ops,
	.video = &tpg_video_ops,
	.pad = &tpg_pad_ops,
};

void atomisp_tpg_unregister_entities(struct atomisp_tpg_device *tpg)
{
	media_entity_cleanup(&tpg->sd.entity);
	v4l2_device_unregister_subdev(&tpg->sd);
}

int atomisp_tpg_register_entities(struct atomisp_tpg_device *tpg,
				  struct v4l2_device *vdev)
{
	int ret;
	/* Register the subdev and video nodes. */
	ret = v4l2_device_register_subdev(vdev, &tpg->sd);
	if (ret < 0)
		goto error;

	return 0;

error:
	atomisp_tpg_unregister_entities(tpg);
	return ret;
}

void atomisp_tpg_cleanup(struct atomisp_device *isp)
{
}

int atomisp_tpg_init(struct atomisp_device *isp)
{
	struct atomisp_tpg_device *tpg = &isp->tpg;
	struct v4l2_subdev *sd = &tpg->sd;
	struct media_pad *pads = tpg->pads;
	struct media_entity *me = &sd->entity;
	int ret;

	tpg->isp = isp;
	v4l2_subdev_init(sd, &tpg_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	strscpy(sd->name, "tpg_subdev", sizeof(sd->name));
	v4l2_set_subdevdata(sd, tpg);

	pads[0].flags = MEDIA_PAD_FL_SINK;
	me->function = MEDIA_ENT_F_V4L2_SUBDEV_UNKNOWN;

	ret = media_entity_pads_init(me, 1, pads);
	if (ret < 0)
		goto fail;
	return 0;
fail:
	atomisp_tpg_cleanup(isp);
	return ret;
}
