// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * vimc-common.c Virtual Media Controller Driver
 *
 * Copyright (C) 2015-2017 Helen Koike <helen.fornazier@gmail.com>
 */

#include <linux/init.h>
#include <linux/module.h>

#include "vimc-common.h"

static const __u32 vimc_mbus_list[] = {
	MEDIA_BUS_FMT_FIXED,
	MEDIA_BUS_FMT_RGB444_1X12,
	MEDIA_BUS_FMT_RGB444_2X8_PADHI_BE,
	MEDIA_BUS_FMT_RGB444_2X8_PADHI_LE,
	MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE,
	MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE,
	MEDIA_BUS_FMT_RGB565_1X16,
	MEDIA_BUS_FMT_BGR565_2X8_BE,
	MEDIA_BUS_FMT_BGR565_2X8_LE,
	MEDIA_BUS_FMT_RGB565_2X8_BE,
	MEDIA_BUS_FMT_RGB565_2X8_LE,
	MEDIA_BUS_FMT_RGB666_1X18,
	MEDIA_BUS_FMT_RBG888_1X24,
	MEDIA_BUS_FMT_RGB666_1X24_CPADHI,
	MEDIA_BUS_FMT_RGB666_1X7X3_SPWG,
	MEDIA_BUS_FMT_BGR888_1X24,
	MEDIA_BUS_FMT_GBR888_1X24,
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_RGB888_2X12_BE,
	MEDIA_BUS_FMT_RGB888_2X12_LE,
	MEDIA_BUS_FMT_RGB888_1X7X4_SPWG,
	MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA,
	MEDIA_BUS_FMT_ARGB8888_1X32,
	MEDIA_BUS_FMT_RGB888_1X32_PADHI,
	MEDIA_BUS_FMT_RGB101010_1X30,
	MEDIA_BUS_FMT_RGB121212_1X36,
	MEDIA_BUS_FMT_RGB161616_1X48,
	MEDIA_BUS_FMT_Y8_1X8,
	MEDIA_BUS_FMT_UV8_1X8,
	MEDIA_BUS_FMT_UYVY8_1_5X8,
	MEDIA_BUS_FMT_VYUY8_1_5X8,
	MEDIA_BUS_FMT_YUYV8_1_5X8,
	MEDIA_BUS_FMT_YVYU8_1_5X8,
	MEDIA_BUS_FMT_UYVY8_2X8,
	MEDIA_BUS_FMT_VYUY8_2X8,
	MEDIA_BUS_FMT_YUYV8_2X8,
	MEDIA_BUS_FMT_YVYU8_2X8,
	MEDIA_BUS_FMT_Y10_1X10,
	MEDIA_BUS_FMT_Y10_2X8_PADHI_LE,
	MEDIA_BUS_FMT_UYVY10_2X10,
	MEDIA_BUS_FMT_VYUY10_2X10,
	MEDIA_BUS_FMT_YUYV10_2X10,
	MEDIA_BUS_FMT_YVYU10_2X10,
	MEDIA_BUS_FMT_Y12_1X12,
	MEDIA_BUS_FMT_UYVY12_2X12,
	MEDIA_BUS_FMT_VYUY12_2X12,
	MEDIA_BUS_FMT_YUYV12_2X12,
	MEDIA_BUS_FMT_YVYU12_2X12,
	MEDIA_BUS_FMT_UYVY8_1X16,
	MEDIA_BUS_FMT_VYUY8_1X16,
	MEDIA_BUS_FMT_YUYV8_1X16,
	MEDIA_BUS_FMT_YVYU8_1X16,
	MEDIA_BUS_FMT_YDYUYDYV8_1X16,
	MEDIA_BUS_FMT_UYVY10_1X20,
	MEDIA_BUS_FMT_VYUY10_1X20,
	MEDIA_BUS_FMT_YUYV10_1X20,
	MEDIA_BUS_FMT_YVYU10_1X20,
	MEDIA_BUS_FMT_VUY8_1X24,
	MEDIA_BUS_FMT_YUV8_1X24,
	MEDIA_BUS_FMT_UYYVYY8_0_5X24,
	MEDIA_BUS_FMT_UYVY12_1X24,
	MEDIA_BUS_FMT_VYUY12_1X24,
	MEDIA_BUS_FMT_YUYV12_1X24,
	MEDIA_BUS_FMT_YVYU12_1X24,
	MEDIA_BUS_FMT_YUV10_1X30,
	MEDIA_BUS_FMT_UYYVYY10_0_5X30,
	MEDIA_BUS_FMT_AYUV8_1X32,
	MEDIA_BUS_FMT_UYYVYY12_0_5X36,
	MEDIA_BUS_FMT_YUV12_1X36,
	MEDIA_BUS_FMT_YUV16_1X48,
	MEDIA_BUS_FMT_UYYVYY16_0_5X48,
	MEDIA_BUS_FMT_SBGGR8_1X8,
	MEDIA_BUS_FMT_SGBRG8_1X8,
	MEDIA_BUS_FMT_SGRBG8_1X8,
	MEDIA_BUS_FMT_SRGGB8_1X8,
	MEDIA_BUS_FMT_SBGGR10_ALAW8_1X8,
	MEDIA_BUS_FMT_SGBRG10_ALAW8_1X8,
	MEDIA_BUS_FMT_SGRBG10_ALAW8_1X8,
	MEDIA_BUS_FMT_SRGGB10_ALAW8_1X8,
	MEDIA_BUS_FMT_SBGGR10_DPCM8_1X8,
	MEDIA_BUS_FMT_SGBRG10_DPCM8_1X8,
	MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8,
	MEDIA_BUS_FMT_SRGGB10_DPCM8_1X8,
	MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_BE,
	MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_LE,
	MEDIA_BUS_FMT_SBGGR10_2X8_PADLO_BE,
	MEDIA_BUS_FMT_SBGGR10_2X8_PADLO_LE,
	MEDIA_BUS_FMT_SBGGR10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SBGGR12_1X12,
	MEDIA_BUS_FMT_SGBRG12_1X12,
	MEDIA_BUS_FMT_SGRBG12_1X12,
	MEDIA_BUS_FMT_SRGGB12_1X12,
	MEDIA_BUS_FMT_SBGGR14_1X14,
	MEDIA_BUS_FMT_SGBRG14_1X14,
	MEDIA_BUS_FMT_SGRBG14_1X14,
	MEDIA_BUS_FMT_SRGGB14_1X14,
	MEDIA_BUS_FMT_SBGGR16_1X16,
	MEDIA_BUS_FMT_SGBRG16_1X16,
	MEDIA_BUS_FMT_SGRBG16_1X16,
	MEDIA_BUS_FMT_SRGGB16_1X16,
	MEDIA_BUS_FMT_JPEG_1X8,
	MEDIA_BUS_FMT_S5C_UYVY_JPEG_1X8,
	MEDIA_BUS_FMT_AHSV8888_1X32,
};

/* Helper function to check mbus codes */
bool vimc_mbus_code_supported(__u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(vimc_mbus_list); i++)
		if (code == vimc_mbus_list[i])
			return true;
	return false;
}
EXPORT_SYMBOL_GPL(vimc_mbus_code_supported);

/* Helper function to enumerate mbus codes */
int vimc_enum_mbus_code(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(vimc_mbus_list))
		return -EINVAL;

	code->code = vimc_mbus_list[code->index];
	return 0;
}
EXPORT_SYMBOL_GPL(vimc_enum_mbus_code);

/* Helper function to allocate and initialize pads */
struct media_pad *vimc_pads_init(u16 num_pads, const unsigned long *pads_flag)
{
	struct media_pad *pads;
	unsigned int i;

	/* Allocate memory for the pads */
	pads = kcalloc(num_pads, sizeof(*pads), GFP_KERNEL);
	if (!pads)
		return ERR_PTR(-ENOMEM);

	/* Initialize the pads */
	for (i = 0; i < num_pads; i++) {
		pads[i].index = i;
		pads[i].flags = pads_flag[i];
	}

	return pads;
}
EXPORT_SYMBOL_GPL(vimc_pads_init);

int vimc_pipeline_s_stream(struct media_entity *ent, int enable)
{
	struct v4l2_subdev *sd;
	struct media_pad *pad;
	unsigned int i;
	int ret;

	for (i = 0; i < ent->num_pads; i++) {
		if (ent->pads[i].flags & MEDIA_PAD_FL_SOURCE)
			continue;

		/* Start the stream in the subdevice direct connected */
		pad = media_entity_remote_pad(&ent->pads[i]);
		if (!pad)
			continue;

		if (!is_media_entity_v4l2_subdev(pad->entity))
			return -EINVAL;

		sd = media_entity_to_v4l2_subdev(pad->entity);
		ret = v4l2_subdev_call(sd, video, s_stream, enable);
		if (ret && ret != -ENOIOCTLCMD)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(vimc_pipeline_s_stream);

static int vimc_get_mbus_format(struct media_pad *pad,
				struct v4l2_subdev_format *fmt)
{
	if (is_media_entity_v4l2_subdev(pad->entity)) {
		struct v4l2_subdev *sd =
			media_entity_to_v4l2_subdev(pad->entity);
		int ret;

		fmt->which = V4L2_SUBDEV_FORMAT_ACTIVE;
		fmt->pad = pad->index;

		ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, fmt);
		if (ret)
			return ret;

	} else if (is_media_entity_v4l2_video_device(pad->entity)) {
		struct video_device *vdev = container_of(pad->entity,
							 struct video_device,
							 entity);
		struct vimc_ent_device *ved = video_get_drvdata(vdev);
		struct v4l2_pix_format vdev_fmt;

		if (!ved->vdev_get_format)
			return -ENOIOCTLCMD;

		ved->vdev_get_format(ved, &vdev_fmt);
		v4l2_fill_mbus_format(&fmt->format, &vdev_fmt, 0);
	} else {
		return -EINVAL;
	}

	return 0;
}

int vimc_link_validate(struct media_link *link)
{
	struct v4l2_subdev_format source_fmt, sink_fmt;
	int ret;

	ret = vimc_get_mbus_format(link->source, &source_fmt);
	if (ret)
		return ret;

	ret = vimc_get_mbus_format(link->sink, &sink_fmt);
	if (ret)
		return ret;

	pr_info("vimc link validate: "
		"%s:src:%dx%d (0x%x, %d, %d, %d, %d) "
		"%s:snk:%dx%d (0x%x, %d, %d, %d, %d)\n",
		/* src */
		link->source->entity->name,
		source_fmt.format.width, source_fmt.format.height,
		source_fmt.format.code, source_fmt.format.colorspace,
		source_fmt.format.quantization, source_fmt.format.xfer_func,
		source_fmt.format.ycbcr_enc,
		/* sink */
		link->sink->entity->name,
		sink_fmt.format.width, sink_fmt.format.height,
		sink_fmt.format.code, sink_fmt.format.colorspace,
		sink_fmt.format.quantization, sink_fmt.format.xfer_func,
		sink_fmt.format.ycbcr_enc);

	/* The width, height and code must match. */
	if (source_fmt.format.width != sink_fmt.format.width
	    || source_fmt.format.height != sink_fmt.format.height
	    || (source_fmt.format.code && sink_fmt.format.code &&
		source_fmt.format.code != sink_fmt.format.code)) {
		pr_err("vimc: format doesn't match in link %s->%s\n",
			link->source->entity->name, link->sink->entity->name);
		return -EPIPE;
	}

	/*
	 * The field order must match, or the sink field order must be NONE
	 * to support interlaced hardware connected to bridges that support
	 * progressive formats only.
	 */
	if (source_fmt.format.field != sink_fmt.format.field &&
	    sink_fmt.format.field != V4L2_FIELD_NONE)
		return -EPIPE;

	/*
	 * If colorspace is DEFAULT, then assume all the colorimetry is also
	 * DEFAULT, return 0 to skip comparing the other colorimetry parameters
	 */
	if (source_fmt.format.colorspace == V4L2_COLORSPACE_DEFAULT
	    || sink_fmt.format.colorspace == V4L2_COLORSPACE_DEFAULT)
		return 0;

	/* Colorspace must match. */
	if (source_fmt.format.colorspace != sink_fmt.format.colorspace)
		return -EPIPE;

	/* Colorimetry must match if they are not set to DEFAULT */
	if (source_fmt.format.ycbcr_enc != V4L2_YCBCR_ENC_DEFAULT
	    && sink_fmt.format.ycbcr_enc != V4L2_YCBCR_ENC_DEFAULT
	    && source_fmt.format.ycbcr_enc != sink_fmt.format.ycbcr_enc)
		return -EPIPE;

	if (source_fmt.format.quantization != V4L2_QUANTIZATION_DEFAULT
	    && sink_fmt.format.quantization != V4L2_QUANTIZATION_DEFAULT
	    && source_fmt.format.quantization != sink_fmt.format.quantization)
		return -EPIPE;

	if (source_fmt.format.xfer_func != V4L2_XFER_FUNC_DEFAULT
	    && sink_fmt.format.xfer_func != V4L2_XFER_FUNC_DEFAULT
	    && source_fmt.format.xfer_func != sink_fmt.format.xfer_func)
		return -EPIPE;

	return 0;
}
EXPORT_SYMBOL_GPL(vimc_link_validate);

static const struct media_entity_operations vimc_ent_sd_mops = {
	.link_validate = vimc_link_validate,
};

int vimc_ent_sd_register(struct vimc_ent_device *ved,
			 struct v4l2_subdev *sd,
			 struct v4l2_device *v4l2_dev,
			 const char *const name,
			 u32 function,
			 u16 num_pads,
			 const unsigned long *pads_flag,
			 const struct v4l2_subdev_internal_ops *sd_int_ops,
			 const struct v4l2_subdev_ops *sd_ops)
{
	int ret;

	/* Allocate the pads */
	ved->pads = vimc_pads_init(num_pads, pads_flag);
	if (IS_ERR(ved->pads))
		return PTR_ERR(ved->pads);

	/* Fill the vimc_ent_device struct */
	ved->ent = &sd->entity;

	/* Initialize the subdev */
	v4l2_subdev_init(sd, sd_ops);
	sd->internal_ops = sd_int_ops;
	sd->entity.function = function;
	sd->entity.ops = &vimc_ent_sd_mops;
	sd->owner = THIS_MODULE;
	strscpy(sd->name, name, sizeof(sd->name));
	v4l2_set_subdevdata(sd, ved);

	/* Expose this subdev to user space */
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	if (sd->ctrl_handler)
		sd->flags |= V4L2_SUBDEV_FL_HAS_EVENTS;

	/* Initialize the media entity */
	ret = media_entity_pads_init(&sd->entity, num_pads, ved->pads);
	if (ret)
		goto err_clean_pads;

	/* Register the subdev with the v4l2 and the media framework */
	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret) {
		dev_err(v4l2_dev->dev,
			"%s: subdev register failed (err=%d)\n",
			name, ret);
		goto err_clean_m_ent;
	}

	return 0;

err_clean_m_ent:
	media_entity_cleanup(&sd->entity);
err_clean_pads:
	vimc_pads_cleanup(ved->pads);
	return ret;
}
EXPORT_SYMBOL_GPL(vimc_ent_sd_register);

void vimc_ent_sd_unregister(struct vimc_ent_device *ved, struct v4l2_subdev *sd)
{
	media_entity_cleanup(ved->ent);
	vimc_pads_cleanup(ved->pads);
	v4l2_device_unregister_subdev(sd);
}
EXPORT_SYMBOL_GPL(vimc_ent_sd_unregister);
