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

static const struct csi_format csi_formats_st7110[] = {
	{ MEDIA_BUS_FMT_YUYV8_2X8, 16},
	{ MEDIA_BUS_FMT_RGB565_2X8_LE, 16},
	{ MEDIA_BUS_FMT_SRGGB10_1X10, 10},
	{ MEDIA_BUS_FMT_SGRBG10_1X10, 10},
	{ MEDIA_BUS_FMT_SGBRG10_1X10, 10},
	{ MEDIA_BUS_FMT_SBGGR10_1X10, 10},
};

static int csi_find_format(u32 code,
		const struct csi_format *formats,
		unsigned int nformats)
{
	int i;

	for (i = 0; i < nformats; i++)
		if (formats[i].code == code)
			return i;
	return -EINVAL;
}

int stf_csi_subdev_init(struct stfcamss *stfcamss)
{
	struct stf_csi_dev *csi_dev = stfcamss->csi_dev;

	csi_dev->s_type = SENSOR_VIN;
	csi_dev->hw_ops = &csi_ops;
	csi_dev->stfcamss = stfcamss;
	csi_dev->formats = csi_formats_st7110;
	csi_dev->nformats = ARRAY_SIZE(csi_formats_st7110);
	mutex_init(&csi_dev->stream_lock);
	return 0;
}

static int csi_set_power(struct v4l2_subdev *sd, int on)
{
	struct stf_csi_dev *csi_dev = v4l2_get_subdevdata(sd);

	csi_dev->hw_ops->csi_power_on(csi_dev, (u8)on);
	return 0;
}

static struct v4l2_mbus_framefmt *
__csi_get_format(struct stf_csi_dev *csi_dev,
		struct v4l2_subdev_state *state,
		unsigned int pad,
		enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&csi_dev->subdev, state, pad);

	return &csi_dev->fmt[pad];
}

static u32 code_to_data_type(int code)
{
	switch (code) {
	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SBGGR10_1X10:
		return 0x2b;
	case MEDIA_BUS_FMT_YUYV8_2X8:
		return 0x1E;
	case MEDIA_BUS_FMT_RGB565_2X8_LE:
		return 0x22;
	default:
		return 0x2b;
	}
}

static int csi_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct stf_csi_dev *csi_dev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;
	int ret = 0;
	u32 code, width, dt;

	format = __csi_get_format(csi_dev, NULL, STF_CSI_PAD_SRC,
				V4L2_SUBDEV_FORMAT_ACTIVE);
	if (format == NULL)
		return -EINVAL;

	width = format->width;

	ret = csi_find_format(format->code,
				csi_dev->formats,
				csi_dev->nformats);
	if (ret < 0)
		return ret;

	code = csi_dev->formats[ret].code;
	dt = code_to_data_type(code);

	mutex_lock(&csi_dev->stream_lock);
	if (enable) {
		if (csi_dev->stream_count == 0) {
			csi_dev->hw_ops->csi_clk_enable(csi_dev);
			csi_dev->hw_ops->csi_stream_set(csi_dev, enable, dt, width);
		}
		csi_dev->stream_count++;
	} else {
		if (csi_dev->stream_count == 0)
			goto exit;
		if (csi_dev->stream_count == 1) {
			csi_dev->hw_ops->csi_stream_set(csi_dev, enable, dt, width);
			csi_dev->hw_ops->csi_clk_disable(csi_dev);
		}
		csi_dev->stream_count--;
	}
exit:
	mutex_unlock(&csi_dev->stream_lock);
	return 0;
}

static void csi_try_format(struct stf_csi_dev *csi_dev,
			struct v4l2_subdev_state *state,
			unsigned int pad,
			struct v4l2_mbus_framefmt *fmt,
			enum v4l2_subdev_format_whence which)
{
	unsigned int i;

	switch (pad) {
	case STF_CSI_PAD_SINK:
		/* Set format on sink pad */

		for (i = 0; i < csi_dev->nformats; i++)
			if (fmt->code == csi_dev->formats[i].code)
				break;

		if (i >= csi_dev->nformats)
			fmt->code = csi_dev->formats[0].code;

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

	case STF_CSI_PAD_SRC:

		*fmt = *__csi_get_format(csi_dev, state, STF_CSI_PAD_SINK, which);

		break;
	}
}

static int csi_enum_mbus_code(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_mbus_code_enum *code)
{
	struct stf_csi_dev *csi_dev = v4l2_get_subdevdata(sd);

	if (code->index >= csi_dev->nformats)
		return -EINVAL;
	if (code->pad == STF_CSI_PAD_SINK) {
		code->code = csi_dev->formats[code->index].code;
	} else {
		struct v4l2_mbus_framefmt *sink_fmt;

		sink_fmt = __csi_get_format(csi_dev, state, STF_CSI_PAD_SINK,
						code->which);

		code->code = sink_fmt->code;
		if (!code->code)
			return -EINVAL;
	}
	code->flags = 0;

	return 0;
}

static int csi_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_frame_size_enum *fse)
{
	struct stf_csi_dev *csi_dev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt format;

	if (fse->index != 0)
		return -EINVAL;

	format.code = fse->code;
	format.width = 1;
	format.height = 1;
	csi_try_format(csi_dev, state, fse->pad, &format, fse->which);
	fse->min_width = format.width;
	fse->min_height = format.height;

	if (format.code != fse->code)
		return -EINVAL;

	format.code = fse->code;
	format.width = -1;
	format.height = -1;
	csi_try_format(csi_dev, state, fse->pad, &format, fse->which);
	fse->max_width = format.width;
	fse->max_height = format.height;

	return 0;
}

static int csi_get_format(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *fmt)
{
	struct stf_csi_dev *csi_dev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __csi_get_format(csi_dev, state, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	fmt->format = *format;

	return 0;
}

static int csi_set_format(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *fmt)
{
	struct stf_csi_dev *csi_dev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __csi_get_format(csi_dev, state, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	mutex_lock(&csi_dev->stream_lock);
	if (csi_dev->stream_count) {
		fmt->format = *format;
		mutex_unlock(&csi_dev->stream_lock);
		goto out;
	} else {
		csi_try_format(csi_dev, state, fmt->pad, &fmt->format, fmt->which);
		*format = fmt->format;
	}
	mutex_unlock(&csi_dev->stream_lock);

	/* Propagate the format from sink to source */
	if (fmt->pad == STF_CSI_PAD_SINK) {
		format = __csi_get_format(csi_dev, state, STF_CSI_PAD_SRC,
					fmt->which);

		*format = fmt->format;
		csi_try_format(csi_dev, state, STF_CSI_PAD_SRC, format,
					fmt->which);
	}
out:
	return 0;
}

static int csi_init_formats(struct v4l2_subdev *sd,
			struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format = {
		.pad = STF_CSI_PAD_SINK,
		.which = fh ? V4L2_SUBDEV_FORMAT_TRY :
				V4L2_SUBDEV_FORMAT_ACTIVE,
		.format = {
			.code = MEDIA_BUS_FMT_RGB565_2X8_LE,
			.width = 1920,
			.height = 1080
		}
	};

	return csi_set_format(sd, fh ? fh->state : NULL, &format);
}

static int csi_link_setup(struct media_entity *entity,
			const struct media_pad *local,
			const struct media_pad *remote, u32 flags)
{
	if ((local->flags & MEDIA_PAD_FL_SOURCE) &&
		(flags & MEDIA_LNK_FL_ENABLED)) {
		struct v4l2_subdev *sd;
		struct stf_csi_dev *csi_dev;
		struct vin_line *line;

		if (media_entity_remote_pad(local))
			return -EBUSY;

		sd = media_entity_to_v4l2_subdev(entity);
		csi_dev = v4l2_get_subdevdata(sd);

		sd = media_entity_to_v4l2_subdev(remote->entity);
		line = v4l2_get_subdevdata(sd);
		if (line->sdev_type == VIN_DEV_TYPE)
			csi_dev->s_type = SENSOR_VIN;
		if (line->sdev_type == ISP_DEV_TYPE)
			csi_dev->s_type = SENSOR_ISP;
		st_info(ST_CSI, "CSI device sensor type: %d\n", csi_dev->s_type);
	}

	if ((local->flags & MEDIA_PAD_FL_SINK) &&
		(flags & MEDIA_LNK_FL_ENABLED)) {
		struct v4l2_subdev *sd;
		struct stf_csi_dev *csi_dev;
		struct stf_csiphy_dev *csiphy_dev;

		if (media_entity_remote_pad(local))
			return -EBUSY;

		sd = media_entity_to_v4l2_subdev(entity);
		csi_dev = v4l2_get_subdevdata(sd);

		sd = media_entity_to_v4l2_subdev(remote->entity);
		csiphy_dev = v4l2_get_subdevdata(sd);

		st_info(ST_CSI, "CSI0 link to csiphy0\n");
	}

	return 0;
}

static const struct v4l2_subdev_core_ops csi_core_ops = {
	.s_power = csi_set_power,
};

static const struct v4l2_subdev_video_ops csi_video_ops = {
	.s_stream = csi_set_stream,
};

static const struct v4l2_subdev_pad_ops csi_pad_ops = {
	.enum_mbus_code = csi_enum_mbus_code,
	.enum_frame_size = csi_enum_frame_size,
	.get_fmt = csi_get_format,
	.set_fmt = csi_set_format,
};

static const struct v4l2_subdev_ops csi_v4l2_ops = {
	.core = &csi_core_ops,
	.video = &csi_video_ops,
	.pad = &csi_pad_ops,
};

static const struct v4l2_subdev_internal_ops csi_v4l2_internal_ops = {
	.open = csi_init_formats,
};

static const struct media_entity_operations csi_media_ops = {
	.link_setup = csi_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

int stf_csi_register(struct stf_csi_dev *csi_dev, struct v4l2_device *v4l2_dev)
{
	struct v4l2_subdev *sd = &csi_dev->subdev;
	struct device *dev = csi_dev->stfcamss->dev;
	struct media_pad *pads = csi_dev->pads;
	int ret;

	csi_dev->mipirx_0p9 = devm_regulator_get(dev, "mipi_0p9");
	if (IS_ERR(csi_dev->mipirx_0p9))
		return PTR_ERR(csi_dev->mipirx_0p9);

	v4l2_subdev_init(sd, &csi_v4l2_ops);
	sd->internal_ops = &csi_v4l2_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, ARRAY_SIZE(sd->name), "%s%d",
		STF_CSI_NAME, 0);
	v4l2_set_subdevdata(sd, csi_dev);

	ret = csi_init_formats(sd, NULL);
	if (ret < 0) {
		dev_err(dev, "Failed to init format: %d\n", ret);
		return ret;
	}

	pads[STF_CSI_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[STF_CSI_PAD_SRC].flags = MEDIA_PAD_FL_SOURCE;

	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	sd->entity.ops = &csi_media_ops;
	ret = media_entity_pads_init(&sd->entity, STF_CSI_PADS_NUM, pads);
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

int stf_csi_unregister(struct stf_csi_dev *csi_dev)
{
	v4l2_device_unregister_subdev(&csi_dev->subdev);
	media_entity_cleanup(&csi_dev->subdev.entity);
	mutex_destroy(&csi_dev->stream_lock);
	return 0;
}
