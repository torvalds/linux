// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021-2023 StarFive Technology Co., Ltd.
 *
 */

#include "stfcamss.h"
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

static const struct dvp_format dvp_formats_st7110[] = {
	{ MEDIA_BUS_FMT_YUYV8_2X8, 8},
	{ MEDIA_BUS_FMT_RGB565_2X8_LE, 8},
	{ MEDIA_BUS_FMT_SRGGB8_1X8, 8},
	{ MEDIA_BUS_FMT_SGRBG8_1X8, 8},
	{ MEDIA_BUS_FMT_SGBRG8_1X8, 8},
	{ MEDIA_BUS_FMT_SBGGR8_1X8, 8},
	{ MEDIA_BUS_FMT_SRGGB10_1X10, 8},
	{ MEDIA_BUS_FMT_SGRBG10_1X10, 8},
	{ MEDIA_BUS_FMT_SGBRG10_1X10, 8},
	{ MEDIA_BUS_FMT_SBGGR10_1X10, 8},
};

static int dvp_find_format(u32 code,
		const struct dvp_format *formats,
		unsigned int nformats)
{
	int i;

	for (i = 0; i < nformats; i++)
		if (formats[i].code == code)
			return i;
	return -EINVAL;
}

int stf_dvp_subdev_init(struct stfcamss *stfcamss)
{
	struct stf_dvp_dev *dvp_dev = stfcamss->dvp_dev;

	dvp_dev->s_type = SENSOR_VIN;
	dvp_dev->hw_ops = &dvp_ops;
	dvp_dev->stfcamss = stfcamss;
	dvp_dev->formats = dvp_formats_st7110;
	dvp_dev->nformats = ARRAY_SIZE(dvp_formats_st7110);
	mutex_init(&dvp_dev->stream_lock);
	dvp_dev->stream_count = 0;
	return 0;
}

static int dvp_set_power(struct v4l2_subdev *sd, int on)
{
	return 0;
}

static struct v4l2_mbus_framefmt *
__dvp_get_format(struct stf_dvp_dev *dvp_dev,
		struct v4l2_subdev_state *state,
		unsigned int pad,
		enum v4l2_subdev_format_whence which)
{

	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(
			&dvp_dev->subdev, state, pad);
	return &dvp_dev->fmt[pad];
}

static int dvp_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct stf_dvp_dev *dvp_dev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;
	int ret = 0;

	format = __dvp_get_format(dvp_dev, NULL, STF_DVP_PAD_SRC,
				V4L2_SUBDEV_FORMAT_ACTIVE);
	if (format == NULL)
		return -EINVAL;
	ret = dvp_find_format(format->code,
				dvp_dev->formats,
				dvp_dev->nformats);
	if (ret < 0)
		return ret;

	mutex_lock(&dvp_dev->stream_lock);
	if (enable) {
		if (dvp_dev->stream_count == 0) {
			dvp_dev->hw_ops->dvp_clk_enable(dvp_dev);
			dvp_dev->hw_ops->dvp_config_set(dvp_dev);
			dvp_dev->hw_ops->dvp_set_format(dvp_dev,
				format->width, dvp_dev->formats[ret].bpp);
			dvp_dev->hw_ops->dvp_stream_set(dvp_dev, 1);
		}
		dvp_dev->stream_count++;
	} else {
		if (dvp_dev->stream_count == 0)
			goto exit;
		if (dvp_dev->stream_count == 1) {
			dvp_dev->hw_ops->dvp_stream_set(dvp_dev, 0);
			dvp_dev->hw_ops->dvp_clk_disable(dvp_dev);
		}
		dvp_dev->stream_count--;
	}
exit:
	mutex_unlock(&dvp_dev->stream_lock);
	return 0;
}

static void dvp_try_format(struct stf_dvp_dev *dvp_dev,
			struct v4l2_subdev_state *state,
			unsigned int pad,
			struct v4l2_mbus_framefmt *fmt,
			enum v4l2_subdev_format_whence which)
{
	unsigned int i;

	switch (pad) {
	case STF_DVP_PAD_SINK:
		/* Set format on sink pad */

		for (i = 0; i < dvp_dev->nformats; i++)
			if (fmt->code == dvp_dev->formats[i].code)
				break;

		if (i >= dvp_dev->nformats)
			fmt->code = dvp_dev->formats[0].code;

		fmt->width = clamp_t(u32,
				fmt->width, STFCAMSS_FRAME_MIN_WIDTH,
				STFCAMSS_FRAME_MAX_WIDTH);
		fmt->height = clamp_t(u32,
				fmt->height, STFCAMSS_FRAME_MIN_HEIGHT,
				STFCAMSS_FRAME_MAX_HEIGHT);

		fmt->field = V4L2_FIELD_NONE;
		fmt->colorspace = V4L2_COLORSPACE_SRGB;
		fmt->flags = 0;

		break;

	case STF_DVP_PAD_SRC:

		*fmt = *__dvp_get_format(dvp_dev, state, STF_DVP_PAD_SINK, which);

		break;
	}
}

static int dvp_enum_mbus_code(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_mbus_code_enum *code)
{
	struct stf_dvp_dev *dvp_dev = v4l2_get_subdevdata(sd);

	if (code->index >= dvp_dev->nformats)
		return -EINVAL;

	if (code->pad == STF_DVP_PAD_SINK) {
		code->code = dvp_dev->formats[code->index].code;
	} else {
		struct v4l2_mbus_framefmt *sink_fmt;

		sink_fmt = __dvp_get_format(dvp_dev, state, STF_DVP_PAD_SINK,
					code->which);

		code->code = sink_fmt->code;
		if (!code->code)
			return -EINVAL;
	}
	code->flags = 0;

	return 0;
}

static int dvp_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_frame_size_enum *fse)
{
	struct stf_dvp_dev *dvp_dev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt format;

	if (fse->index != 0)
		return -EINVAL;

	format.code = fse->code;
	format.width = 1;
	format.height = 1;
	dvp_try_format(dvp_dev, state, fse->pad, &format, fse->which);
	fse->min_width = format.width;
	fse->min_height = format.height;

	if (format.code != fse->code)
		return -EINVAL;

	format.code = fse->code;
	format.width = -1;
	format.height = -1;
	dvp_try_format(dvp_dev, state, fse->pad, &format, fse->which);
	fse->max_width = format.width;
	fse->max_height = format.height;

	return 0;
}

static int dvp_get_format(struct v4l2_subdev *sd,
			 struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *fmt)
{
	struct stf_dvp_dev *dvp_dev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __dvp_get_format(dvp_dev, state, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	fmt->format = *format;

	return 0;
}

static int dvp_set_format(struct v4l2_subdev *sd,
			 struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *fmt)
{
	struct stf_dvp_dev *dvp_dev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __dvp_get_format(dvp_dev, state, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	mutex_lock(&dvp_dev->stream_lock);
	if (dvp_dev->stream_count) {
		fmt->format = *format;
		mutex_unlock(&dvp_dev->stream_lock);
		goto out;
	} else {
		dvp_try_format(dvp_dev, state, fmt->pad, &fmt->format, fmt->which);
		*format = fmt->format;
	}
	mutex_unlock(&dvp_dev->stream_lock);

	/* Propagate the format from sink to source */
	if (fmt->pad == STF_DVP_PAD_SINK) {
		format = __dvp_get_format(dvp_dev, state, STF_DVP_PAD_SRC,
					fmt->which);

		*format = fmt->format;
		dvp_try_format(dvp_dev, state, STF_DVP_PAD_SRC, format,
					fmt->which);
	}

out:
	return 0;
}

static int dvp_init_formats(struct v4l2_subdev *sd,
			struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format = {
		.pad = STF_DVP_PAD_SINK,
		.which = fh ? V4L2_SUBDEV_FORMAT_TRY :
				V4L2_SUBDEV_FORMAT_ACTIVE,
		.format = {
			.code = MEDIA_BUS_FMT_RGB565_2X8_LE,
			.width = 1920,
			.height = 1080
		}
	};

	return dvp_set_format(sd, fh ? fh->state : NULL, &format);
}

static int dvp_link_setup(struct media_entity *entity,
			const struct media_pad *local,
			const struct media_pad *remote, u32 flags)
{
	if ((local->flags & MEDIA_PAD_FL_SOURCE) &&
		(flags & MEDIA_LNK_FL_ENABLED)) {
		struct v4l2_subdev *sd;
		struct stf_dvp_dev *dvp_dev;
		struct vin_line *line;

		if (media_entity_remote_pad(local))
			return -EBUSY;

		sd = media_entity_to_v4l2_subdev(entity);
		dvp_dev = v4l2_get_subdevdata(sd);

		sd = media_entity_to_v4l2_subdev(remote->entity);
		line = v4l2_get_subdevdata(sd);
		if (line->sdev_type == VIN_DEV_TYPE)
			dvp_dev->s_type = SENSOR_VIN;
		if (line->sdev_type == ISP_DEV_TYPE)
			dvp_dev->s_type = SENSOR_ISP;
		st_info(ST_DVP, "DVP device sensor type: %d\n", dvp_dev->s_type);
	}

	return 0;
}

static const struct v4l2_subdev_core_ops dvp_core_ops = {
	.s_power = dvp_set_power,
};

static const struct v4l2_subdev_video_ops dvp_video_ops = {
	.s_stream = dvp_set_stream,
};

static const struct v4l2_subdev_pad_ops dvp_pad_ops = {
	.enum_mbus_code = dvp_enum_mbus_code,
	.enum_frame_size = dvp_enum_frame_size,
	.get_fmt = dvp_get_format,
	.set_fmt = dvp_set_format,
};

static const struct v4l2_subdev_ops dvp_v4l2_ops = {
	.core = &dvp_core_ops,
	.video = &dvp_video_ops,
	.pad = &dvp_pad_ops,
};

static const struct v4l2_subdev_internal_ops dvp_v4l2_internal_ops = {
	.open = dvp_init_formats,
};

static const struct media_entity_operations dvp_media_ops = {
	.link_setup = dvp_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

int stf_dvp_register(struct stf_dvp_dev *dvp_dev,
		struct v4l2_device *v4l2_dev)
{
	struct v4l2_subdev *sd = &dvp_dev->subdev;
	struct media_pad *pads = dvp_dev->pads;
	int ret;

	v4l2_subdev_init(sd, &dvp_v4l2_ops);
	sd->internal_ops = &dvp_v4l2_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, ARRAY_SIZE(sd->name), "%s%d",
		STF_DVP_NAME, 0);
	v4l2_set_subdevdata(sd, dvp_dev);

	ret = dvp_init_formats(sd, NULL);
	if (ret < 0) {
		st_err(ST_DVP, "Failed to init format: %d\n", ret);
		return ret;
	}

	pads[STF_DVP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[STF_DVP_PAD_SRC].flags = MEDIA_PAD_FL_SOURCE;

	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	sd->entity.ops = &dvp_media_ops;
	ret = media_entity_pads_init(&sd->entity, STF_DVP_PADS_NUM, pads);
	if (ret < 0) {
		st_err(ST_DVP, "Failed to init media entity: %d\n", ret);
		return ret;
	}

	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret < 0) {
		st_err(ST_DVP, "Failed to register subdev: %d\n", ret);
		goto err_sreg;
	}

	return 0;

err_sreg:
	media_entity_cleanup(&sd->entity);
	return ret;
}

int stf_dvp_unregister(struct stf_dvp_dev *dvp_dev)
{
	v4l2_device_unregister_subdev(&dvp_dev->subdev);
	media_entity_cleanup(&dvp_dev->subdev.entity);
	mutex_destroy(&dvp_dev->stream_lock);
	return 0;
}
