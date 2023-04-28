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

static const struct csiphy_format csiphy_formats_st7110[] = {
	{ MEDIA_BUS_FMT_YUYV8_2X8, 16},
	{ MEDIA_BUS_FMT_RGB565_2X8_LE, 16},
	{ MEDIA_BUS_FMT_SRGGB10_1X10, 10},
	{ MEDIA_BUS_FMT_SGRBG10_1X10, 10},
	{ MEDIA_BUS_FMT_SGBRG10_1X10, 10},
	{ MEDIA_BUS_FMT_SBGGR10_1X10, 10},
};

int stf_csiphy_subdev_init(struct stfcamss *stfcamss)
{
	struct stf_csiphy_dev *csiphy_dev = stfcamss->csiphy_dev;

	csiphy_dev->hw_ops = &csiphy_ops;
	csiphy_dev->stfcamss = stfcamss;
	csiphy_dev->formats = csiphy_formats_st7110;
	csiphy_dev->nformats = ARRAY_SIZE(csiphy_formats_st7110);
	mutex_init(&csiphy_dev->stream_lock);
	return 0;
}

static int csiphy_set_power(struct v4l2_subdev *sd, int on)
{
	return 0;
}

static int csiphy_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct stf_csiphy_dev *csiphy_dev = v4l2_get_subdevdata(sd);

	mutex_lock(&csiphy_dev->stream_lock);
	if (enable) {
		if (csiphy_dev->stream_count == 0) {
			csiphy_dev->hw_ops->csiphy_clk_enable(csiphy_dev);
			csiphy_dev->hw_ops->csiphy_config_set(csiphy_dev);
			csiphy_dev->hw_ops->csiphy_stream_set(csiphy_dev, 1);
		}
		csiphy_dev->stream_count++;
	} else {
		if (csiphy_dev->stream_count == 0)
			goto exit;
		if (csiphy_dev->stream_count == 1) {
			csiphy_dev->hw_ops->csiphy_clk_disable(csiphy_dev);
			csiphy_dev->hw_ops->csiphy_stream_set(csiphy_dev, 0);
		}
		csiphy_dev->stream_count--;
	}
exit:
	mutex_unlock(&csiphy_dev->stream_lock);

	return 0;
}

static struct v4l2_mbus_framefmt *
__csiphy_get_format(struct stf_csiphy_dev *csiphy_dev,
		struct v4l2_subdev_state *state,
		unsigned int pad,
		enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(
				&csiphy_dev->subdev,
				state,
				pad);

	return &csiphy_dev->fmt[pad];
}

static void csiphy_try_format(struct stf_csiphy_dev *csiphy_dev,
			struct v4l2_subdev_state *state,
			unsigned int pad,
			struct v4l2_mbus_framefmt *fmt,
			enum v4l2_subdev_format_whence which)
{
	unsigned int i;

	switch (pad) {
	case STF_CSIPHY_PAD_SINK:
		/* Set format on sink pad */

		for (i = 0; i < csiphy_dev->nformats; i++)
			if (fmt->code == csiphy_dev->formats[i].code)
				break;

		if (i >= csiphy_dev->nformats)
			fmt->code = csiphy_dev->formats[0].code;

		fmt->width = clamp_t(u32,
				fmt->width,
				STFCAMSS_FRAME_MIN_WIDTH,
				STFCAMSS_FRAME_MAX_WIDTH);
		fmt->height = clamp_t(u32,
				fmt->height,
				STFCAMSS_FRAME_MIN_HEIGHT,
				STFCAMSS_FRAME_MAX_HEIGHT);

		fmt->field = V4L2_FIELD_NONE;
		fmt->colorspace = V4L2_COLORSPACE_SRGB;
		fmt->flags = 0;

		break;

	case STF_CSIPHY_PAD_SRC:

		*fmt = *__csiphy_get_format(csiphy_dev,
				state,
				STF_CSIPHY_PAD_SINK, which);

		break;
	}
}

static int csiphy_enum_mbus_code(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_mbus_code_enum *code)
{
	struct stf_csiphy_dev *csiphy_dev = v4l2_get_subdevdata(sd);

	if (code->index >= csiphy_dev->nformats)
		return -EINVAL;

	if (code->pad == STF_CSIPHY_PAD_SINK) {
		code->code = csiphy_dev->formats[code->index].code;
	} else {
		struct v4l2_mbus_framefmt *sink_fmt;

		sink_fmt = __csiphy_get_format(csiphy_dev, state,
					STF_CSIPHY_PAD_SINK,
					code->which);

		code->code = sink_fmt->code;
		if (!code->code)
			return -EINVAL;
	}
	code->flags = 0;
	return 0;
}

static int csiphy_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_frame_size_enum *fse)
{
	struct stf_csiphy_dev *csiphy_dev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt format;

	if (fse->index != 0)
		return -EINVAL;

	format.code = fse->code;
	format.width = 1;
	format.height = 1;
	csiphy_try_format(csiphy_dev, state, fse->pad, &format, fse->which);
	fse->min_width = format.width;
	fse->min_height = format.height;

	if (format.code != fse->code)
		return -EINVAL;

	format.code = fse->code;
	format.width = -1;
	format.height = -1;
	csiphy_try_format(csiphy_dev, state, fse->pad, &format, fse->which);
	fse->max_width = format.width;
	fse->max_height = format.height;

	return 0;
}

static int csiphy_get_format(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *fmt)
{
	struct stf_csiphy_dev *csiphy_dev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __csiphy_get_format(csiphy_dev, state, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	fmt->format = *format;

	return 0;
}

static int csiphy_set_format(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *fmt)
{
	struct stf_csiphy_dev *csiphy_dev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __csiphy_get_format(csiphy_dev, state, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	mutex_lock(&csiphy_dev->stream_lock);
	if (csiphy_dev->stream_count) {
		fmt->format = *format;
		mutex_unlock(&csiphy_dev->stream_lock);
		goto out;
	} else {
		csiphy_try_format(csiphy_dev, state, fmt->pad, &fmt->format, fmt->which);
		*format = fmt->format;
	}
	mutex_unlock(&csiphy_dev->stream_lock);

	/* Propagate the format from sink to source */
	if (fmt->pad == STF_CSIPHY_PAD_SINK) {
		format = __csiphy_get_format(csiphy_dev,
					state,
					STF_CSIPHY_PAD_SRC,
					fmt->which);

		*format = fmt->format;
		csiphy_try_format(csiphy_dev, state, STF_CSIPHY_PAD_SRC, format,
					fmt->which);
	}
out:
	return 0;
}

static int csiphy_init_formats(struct v4l2_subdev *sd,
			struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format = {
		.pad = STF_CSIPHY_PAD_SINK,
		.which = fh ? V4L2_SUBDEV_FORMAT_TRY :
				V4L2_SUBDEV_FORMAT_ACTIVE,
		.format = {
			.code = MEDIA_BUS_FMT_RGB565_2X8_LE,
			.width = 1920,
			.height = 1080
		}
	};

	return csiphy_set_format(sd, fh ? fh->state : NULL, &format);
}

static int csiphy_link_setup(struct media_entity *entity,
			const struct media_pad *local,
			const struct media_pad *remote, u32 flags)
{
	if ((local->flags & MEDIA_PAD_FL_SOURCE) &&
		(flags & MEDIA_LNK_FL_ENABLED)) {
		struct v4l2_subdev *sd;
		struct stf_csiphy_dev *csiphy_dev;
		struct stf_csi_dev *csi_dev;

		if (media_entity_remote_pad(local))
			return -EBUSY;

		sd = media_entity_to_v4l2_subdev(entity);
		csiphy_dev = v4l2_get_subdevdata(sd);

		sd = media_entity_to_v4l2_subdev(remote->entity);
		csi_dev = v4l2_get_subdevdata(sd);
		st_info(ST_CSIPHY, "CSIPHY0 link to CSI0\n");
	}

	return 0;
}

static const struct v4l2_subdev_core_ops csiphy_core_ops = {
	.s_power = csiphy_set_power,
};

static const struct v4l2_subdev_video_ops csiphy_video_ops = {
	.s_stream = csiphy_set_stream,
};

static const struct v4l2_subdev_pad_ops csiphy_pad_ops = {
	.enum_mbus_code = csiphy_enum_mbus_code,
	.enum_frame_size = csiphy_enum_frame_size,
	.get_fmt = csiphy_get_format,
	.set_fmt = csiphy_set_format,
};

static const struct v4l2_subdev_ops csiphy_v4l2_ops = {
	.core = &csiphy_core_ops,
	.video = &csiphy_video_ops,
	.pad = &csiphy_pad_ops,
};

static const struct v4l2_subdev_internal_ops csiphy_v4l2_internal_ops = {
	.open = csiphy_init_formats,
};

static const struct media_entity_operations csiphy_media_ops = {
	.link_setup = csiphy_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

int stf_csiphy_register(struct stf_csiphy_dev *csiphy_dev,
			struct v4l2_device *v4l2_dev)
{
	struct v4l2_subdev *sd = &csiphy_dev->subdev;
	struct device *dev = csiphy_dev->stfcamss->dev;
	struct media_pad *pads = csiphy_dev->pads;
	int ret;

	v4l2_subdev_init(sd, &csiphy_v4l2_ops);
	sd->internal_ops = &csiphy_v4l2_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, ARRAY_SIZE(sd->name), "%s%d",
		STF_CSIPHY_NAME, 0);
	v4l2_set_subdevdata(sd, csiphy_dev);

	ret = csiphy_init_formats(sd, NULL);
	if (ret < 0) {
		dev_err(dev, "Failed to init format: %d\n", ret);
		return ret;
	}

	pads[STF_CSIPHY_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[STF_CSIPHY_PAD_SRC].flags = MEDIA_PAD_FL_SOURCE;

	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	sd->entity.ops = &csiphy_media_ops;
	ret = media_entity_pads_init(&sd->entity, STF_CSIPHY_PADS_NUM, pads);
	if (ret < 0) {
		dev_err(dev, "Failed to init media entity: %d\n", ret);
		return ret;
	}

	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret < 0) {
		dev_err(dev, "Failed to register subdev: %d\n", ret);
		goto err_sreg;
	}

	return 0;

err_sreg:
	media_entity_cleanup(&sd->entity);
	return ret;
}

int stf_csiphy_unregister(struct stf_csiphy_dev *csiphy_dev)
{
	v4l2_device_unregister_subdev(&csiphy_dev->subdev);
	media_entity_cleanup(&csiphy_dev->subdev.entity);
	mutex_destroy(&csiphy_dev->stream_lock);
	return 0;
}
