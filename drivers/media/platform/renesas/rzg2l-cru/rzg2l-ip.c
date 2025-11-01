// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Renesas RZ/G2L CRU
 *
 * Copyright (C) 2022 Renesas Electronics Corp.
 */

#include <linux/delay.h>
#include <media/mipi-csi2.h>

#include "rzg2l-cru.h"
#include "rzg2l-cru-regs.h"

static const struct rzg2l_cru_ip_format rzg2l_cru_ip_formats[] = {
	{
		.codes = {
			MEDIA_BUS_FMT_UYVY8_1X16,
		},
		.datatype = MIPI_CSI2_DT_YUV422_8B,
		.format = V4L2_PIX_FMT_UYVY,
		.icndmr = ICnDMR_YCMODE_UYVY,
		.yuv = true,
	},
	{
		.codes = {
			MEDIA_BUS_FMT_SBGGR8_1X8,
		},
		.format = V4L2_PIX_FMT_SBGGR8,
		.datatype = MIPI_CSI2_DT_RAW8,
		.icndmr = 0,
		.yuv = false,
	},
	{
		.codes = {
			MEDIA_BUS_FMT_SGBRG8_1X8,
		},
		.format = V4L2_PIX_FMT_SGBRG8,
		.datatype = MIPI_CSI2_DT_RAW8,
		.icndmr = 0,
		.yuv = false,
	},
	{
		.codes = {
			MEDIA_BUS_FMT_SGRBG8_1X8,
		},
		.format = V4L2_PIX_FMT_SGRBG8,
		.datatype = MIPI_CSI2_DT_RAW8,
		.icndmr = 0,
		.yuv = false,
	},
	{
		.codes = {
			MEDIA_BUS_FMT_SRGGB8_1X8,
		},
		.format = V4L2_PIX_FMT_SRGGB8,
		.datatype = MIPI_CSI2_DT_RAW8,
		.icndmr = 0,
		.yuv = false,
	},
	{
		.codes = {
			MEDIA_BUS_FMT_SBGGR10_1X10,
			MEDIA_BUS_FMT_SGBRG10_1X10,
			MEDIA_BUS_FMT_SGRBG10_1X10,
			MEDIA_BUS_FMT_SRGGB10_1X10
		},
		.format = V4L2_PIX_FMT_RAW_CRU10,
		.datatype = MIPI_CSI2_DT_RAW10,
		.icndmr = 0,
		.yuv = false,
	},
	{
		.codes = {
			MEDIA_BUS_FMT_SBGGR12_1X12,
			MEDIA_BUS_FMT_SGBRG12_1X12,
			MEDIA_BUS_FMT_SGRBG12_1X12,
			MEDIA_BUS_FMT_SRGGB12_1X12
		},
		.format = V4L2_PIX_FMT_RAW_CRU12,
		.datatype = MIPI_CSI2_DT_RAW12,
		.icndmr = 0,
		.yuv = false,
	},
	{
		.codes = {
			MEDIA_BUS_FMT_SBGGR14_1X14,
			MEDIA_BUS_FMT_SGBRG14_1X14,
			MEDIA_BUS_FMT_SGRBG14_1X14,
			MEDIA_BUS_FMT_SRGGB14_1X14
		},
		.format = V4L2_PIX_FMT_RAW_CRU14,
		.datatype = MIPI_CSI2_DT_RAW14,
		.icndmr = 0,
		.yuv = false,
	},
};

const struct rzg2l_cru_ip_format *rzg2l_cru_ip_code_to_fmt(unsigned int code)
{
	unsigned int i, j;

	for (i = 0; i < ARRAY_SIZE(rzg2l_cru_ip_formats); i++) {
		for (j = 0; j < ARRAY_SIZE(rzg2l_cru_ip_formats[i].codes); j++) {
			if (rzg2l_cru_ip_formats[i].codes[j] == code)
				return &rzg2l_cru_ip_formats[i];
		}
	}

	return NULL;
}

const struct rzg2l_cru_ip_format *rzg2l_cru_ip_format_to_fmt(u32 format)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rzg2l_cru_ip_formats); i++) {
		if (rzg2l_cru_ip_formats[i].format == format)
			return &rzg2l_cru_ip_formats[i];
	}

	return NULL;
}

const struct rzg2l_cru_ip_format *rzg2l_cru_ip_index_to_fmt(u32 index)
{
	if (index >= ARRAY_SIZE(rzg2l_cru_ip_formats))
		return NULL;

	return &rzg2l_cru_ip_formats[index];
}

bool rzg2l_cru_ip_fmt_supports_mbus_code(const struct rzg2l_cru_ip_format *fmt,
					 unsigned int code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(fmt->codes); i++)
		if (fmt->codes[i] == code)
			return true;

	return false;
}
struct v4l2_mbus_framefmt *rzg2l_cru_ip_get_src_fmt(struct rzg2l_cru_dev *cru)
{
	struct v4l2_subdev_state *state;
	struct v4l2_mbus_framefmt *fmt;

	state = v4l2_subdev_lock_and_get_active_state(&cru->ip.subdev);
	fmt = v4l2_subdev_state_get_format(state, 1);
	v4l2_subdev_unlock_state(state);

	return fmt;
}

static int rzg2l_cru_ip_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct rzg2l_cru_dev *cru;
	int s_stream_ret = 0;
	int ret;

	cru = v4l2_get_subdevdata(sd);

	if (!enable) {
		ret = v4l2_subdev_call(cru->ip.remote, video, s_stream, enable);
		if (ret)
			s_stream_ret = ret;

		ret = v4l2_subdev_call(cru->ip.remote, video, post_streamoff);
		if (ret == -ENOIOCTLCMD)
			ret = 0;
		if (ret && !s_stream_ret)
			s_stream_ret = ret;
		rzg2l_cru_stop_image_processing(cru);
	} else {
		ret = v4l2_subdev_call(cru->ip.remote, video, pre_streamon, 0);
		if (ret == -ENOIOCTLCMD)
			ret = 0;
		if (ret)
			return ret;

		fsleep(1000);

		ret = rzg2l_cru_start_image_processing(cru);
		if (ret) {
			v4l2_subdev_call(cru->ip.remote, video, post_streamoff);
			return ret;
		}

		ret = v4l2_subdev_call(cru->ip.remote, video, s_stream, enable);
		if (!ret || ret == -ENOIOCTLCMD)
			return 0;

		s_stream_ret = ret;

		v4l2_subdev_call(cru->ip.remote, video, post_streamoff);
		rzg2l_cru_stop_image_processing(cru);
	}

	return s_stream_ret;
}

static int rzg2l_cru_ip_set_format(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_format *fmt)
{
	struct rzg2l_cru_dev *cru = v4l2_get_subdevdata(sd);
	const struct rzg2l_cru_info *info = cru->info;
	struct v4l2_mbus_framefmt *src_format;
	struct v4l2_mbus_framefmt *sink_format;

	src_format = v4l2_subdev_state_get_format(state, RZG2L_CRU_IP_SOURCE);
	if (fmt->pad == RZG2L_CRU_IP_SOURCE) {
		fmt->format = *src_format;
		return 0;
	}

	sink_format = v4l2_subdev_state_get_format(state, fmt->pad);

	if (!rzg2l_cru_ip_code_to_fmt(fmt->format.code))
		sink_format->code = rzg2l_cru_ip_formats[0].codes[0];
	else
		sink_format->code = fmt->format.code;

	sink_format->field = V4L2_FIELD_NONE;
	sink_format->colorspace = fmt->format.colorspace;
	sink_format->xfer_func = fmt->format.xfer_func;
	sink_format->ycbcr_enc = fmt->format.ycbcr_enc;
	sink_format->quantization = fmt->format.quantization;
	sink_format->width = clamp_t(u32, fmt->format.width,
				     RZG2L_CRU_MIN_INPUT_WIDTH, info->max_width);
	sink_format->height = clamp_t(u32, fmt->format.height,
				      RZG2L_CRU_MIN_INPUT_HEIGHT, info->max_height);

	fmt->format = *sink_format;

	/* propagate format to source pad */
	*src_format = *sink_format;

	return 0;
}

static int rzg2l_cru_ip_enum_mbus_code(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state,
				       struct v4l2_subdev_mbus_code_enum *code)
{
	unsigned int index = code->index;
	unsigned int i, j;

	for (i = 0; i < ARRAY_SIZE(rzg2l_cru_ip_formats); i++) {
		const struct rzg2l_cru_ip_format *fmt = &rzg2l_cru_ip_formats[i];

		for (j = 0; j < ARRAY_SIZE(fmt->codes); j++) {
			if (!fmt->codes[j])
				continue;

			if (!index) {
				code->code = fmt->codes[j];
				return 0;
			}

			index--;
		}
	}

	return -EINVAL;
}

static int rzg2l_cru_ip_enum_frame_size(struct v4l2_subdev *sd,
					struct v4l2_subdev_state *state,
					struct v4l2_subdev_frame_size_enum *fse)
{
	struct rzg2l_cru_dev *cru = v4l2_get_subdevdata(sd);
	const struct rzg2l_cru_info *info = cru->info;

	if (fse->index != 0)
		return -EINVAL;

	if (!rzg2l_cru_ip_code_to_fmt(fse->code))
		return -EINVAL;

	fse->min_width = RZG2L_CRU_MIN_INPUT_WIDTH;
	fse->min_height = RZG2L_CRU_MIN_INPUT_HEIGHT;
	fse->max_width = info->max_width;
	fse->max_height = info->max_height;

	return 0;
}

static int rzg2l_cru_ip_init_state(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state)
{
	struct v4l2_subdev_format fmt = { .pad = RZG2L_CRU_IP_SINK, };

	fmt.format.width = RZG2L_CRU_MIN_INPUT_WIDTH;
	fmt.format.height = RZG2L_CRU_MIN_INPUT_HEIGHT;
	fmt.format.field = V4L2_FIELD_NONE;
	fmt.format.code = MEDIA_BUS_FMT_UYVY8_1X16;
	fmt.format.colorspace = V4L2_COLORSPACE_SRGB;
	fmt.format.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt.format.quantization = V4L2_QUANTIZATION_DEFAULT;
	fmt.format.xfer_func = V4L2_XFER_FUNC_DEFAULT;

	return rzg2l_cru_ip_set_format(sd, sd_state, &fmt);
}

static const struct v4l2_subdev_video_ops rzg2l_cru_ip_video_ops = {
	.s_stream = rzg2l_cru_ip_s_stream,
};

static const struct v4l2_subdev_pad_ops rzg2l_cru_ip_pad_ops = {
	.enum_mbus_code = rzg2l_cru_ip_enum_mbus_code,
	.enum_frame_size = rzg2l_cru_ip_enum_frame_size,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = rzg2l_cru_ip_set_format,
};

static const struct v4l2_subdev_ops rzg2l_cru_ip_subdev_ops = {
	.video = &rzg2l_cru_ip_video_ops,
	.pad = &rzg2l_cru_ip_pad_ops,
};

static const struct v4l2_subdev_internal_ops rzg2l_cru_ip_internal_ops = {
	.init_state = rzg2l_cru_ip_init_state,
};

static const struct media_entity_operations rzg2l_cru_ip_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

int rzg2l_cru_ip_subdev_register(struct rzg2l_cru_dev *cru)
{
	struct rzg2l_cru_ip *ip = &cru->ip;
	int ret;

	ip->subdev.dev = cru->dev;
	v4l2_subdev_init(&ip->subdev, &rzg2l_cru_ip_subdev_ops);
	ip->subdev.internal_ops = &rzg2l_cru_ip_internal_ops;
	v4l2_set_subdevdata(&ip->subdev, cru);
	snprintf(ip->subdev.name, sizeof(ip->subdev.name),
		 "cru-ip-%s", dev_name(cru->dev));
	ip->subdev.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;

	ip->subdev.entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	ip->subdev.entity.ops = &rzg2l_cru_ip_entity_ops;

	ip->pads[RZG2L_CRU_IP_SINK].flags = MEDIA_PAD_FL_SINK |
					    MEDIA_PAD_FL_MUST_CONNECT;
	ip->pads[RZG2L_CRU_IP_SOURCE].flags = MEDIA_PAD_FL_SOURCE |
					      MEDIA_PAD_FL_MUST_CONNECT;

	ret = media_entity_pads_init(&ip->subdev.entity, 2, ip->pads);
	if (ret)
		return ret;

	ret = v4l2_subdev_init_finalize(&ip->subdev);
	if (ret < 0)
		goto entity_cleanup;

	ret = v4l2_device_register_subdev(&cru->v4l2_dev, &ip->subdev);
	if (ret < 0)
		goto error_subdev;

	return 0;
error_subdev:
	v4l2_subdev_cleanup(&ip->subdev);
entity_cleanup:
	media_entity_cleanup(&ip->subdev.entity);

	return ret;
}

void rzg2l_cru_ip_subdev_unregister(struct rzg2l_cru_dev *cru)
{
	struct rzg2l_cru_ip *ip = &cru->ip;

	media_entity_cleanup(&ip->subdev.entity);
	v4l2_subdev_cleanup(&ip->subdev);
	v4l2_device_unregister_subdev(&ip->subdev);
}
