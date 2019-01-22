/*
 * vimc-common.c Virtual Media Controller Driver
 *
 * Copyright (C) 2015-2017 Helen Koike <helen.fornazier@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/module.h>

#include "vimc-common.h"

/*
 * NOTE: non-bayer formats need to come first (necessary for enum_mbus_code
 * in the scaler)
 */
static const struct vimc_pix_map vimc_pix_map_list[] = {
	/* TODO: add all missing formats */

	/* RGB formats */
	{
		.code = MEDIA_BUS_FMT_BGR888_1X24,
		.pixelformat = V4L2_PIX_FMT_BGR24,
		.bpp = 3,
		.bayer = false,
	},
	{
		.code = MEDIA_BUS_FMT_RGB888_1X24,
		.pixelformat = V4L2_PIX_FMT_RGB24,
		.bpp = 3,
		.bayer = false,
	},
	{
		.code = MEDIA_BUS_FMT_ARGB8888_1X32,
		.pixelformat = V4L2_PIX_FMT_ARGB32,
		.bpp = 4,
		.bayer = false,
	},

	/* Bayer formats */
	{
		.code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.pixelformat = V4L2_PIX_FMT_SBGGR8,
		.bpp = 1,
		.bayer = true,
	},
	{
		.code = MEDIA_BUS_FMT_SGBRG8_1X8,
		.pixelformat = V4L2_PIX_FMT_SGBRG8,
		.bpp = 1,
		.bayer = true,
	},
	{
		.code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.pixelformat = V4L2_PIX_FMT_SGRBG8,
		.bpp = 1,
		.bayer = true,
	},
	{
		.code = MEDIA_BUS_FMT_SRGGB8_1X8,
		.pixelformat = V4L2_PIX_FMT_SRGGB8,
		.bpp = 1,
		.bayer = true,
	},
	{
		.code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.pixelformat = V4L2_PIX_FMT_SBGGR10,
		.bpp = 2,
		.bayer = true,
	},
	{
		.code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.pixelformat = V4L2_PIX_FMT_SGBRG10,
		.bpp = 2,
		.bayer = true,
	},
	{
		.code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.pixelformat = V4L2_PIX_FMT_SGRBG10,
		.bpp = 2,
		.bayer = true,
	},
	{
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.pixelformat = V4L2_PIX_FMT_SRGGB10,
		.bpp = 2,
		.bayer = true,
	},

	/* 10bit raw bayer a-law compressed to 8 bits */
	{
		.code = MEDIA_BUS_FMT_SBGGR10_ALAW8_1X8,
		.pixelformat = V4L2_PIX_FMT_SBGGR10ALAW8,
		.bpp = 1,
		.bayer = true,
	},
	{
		.code = MEDIA_BUS_FMT_SGBRG10_ALAW8_1X8,
		.pixelformat = V4L2_PIX_FMT_SGBRG10ALAW8,
		.bpp = 1,
		.bayer = true,
	},
	{
		.code = MEDIA_BUS_FMT_SGRBG10_ALAW8_1X8,
		.pixelformat = V4L2_PIX_FMT_SGRBG10ALAW8,
		.bpp = 1,
		.bayer = true,
	},
	{
		.code = MEDIA_BUS_FMT_SRGGB10_ALAW8_1X8,
		.pixelformat = V4L2_PIX_FMT_SRGGB10ALAW8,
		.bpp = 1,
		.bayer = true,
	},

	/* 10bit raw bayer DPCM compressed to 8 bits */
	{
		.code = MEDIA_BUS_FMT_SBGGR10_DPCM8_1X8,
		.pixelformat = V4L2_PIX_FMT_SBGGR10DPCM8,
		.bpp = 1,
		.bayer = true,
	},
	{
		.code = MEDIA_BUS_FMT_SGBRG10_DPCM8_1X8,
		.pixelformat = V4L2_PIX_FMT_SGBRG10DPCM8,
		.bpp = 1,
		.bayer = true,
	},
	{
		.code = MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8,
		.pixelformat = V4L2_PIX_FMT_SGRBG10DPCM8,
		.bpp = 1,
		.bayer = true,
	},
	{
		.code = MEDIA_BUS_FMT_SRGGB10_DPCM8_1X8,
		.pixelformat = V4L2_PIX_FMT_SRGGB10DPCM8,
		.bpp = 1,
		.bayer = true,
	},
	{
		.code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.pixelformat = V4L2_PIX_FMT_SBGGR12,
		.bpp = 2,
		.bayer = true,
	},
	{
		.code = MEDIA_BUS_FMT_SGBRG12_1X12,
		.pixelformat = V4L2_PIX_FMT_SGBRG12,
		.bpp = 2,
		.bayer = true,
	},
	{
		.code = MEDIA_BUS_FMT_SGRBG12_1X12,
		.pixelformat = V4L2_PIX_FMT_SGRBG12,
		.bpp = 2,
		.bayer = true,
	},
	{
		.code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.pixelformat = V4L2_PIX_FMT_SRGGB12,
		.bpp = 2,
		.bayer = true,
	},
};

const struct vimc_pix_map *vimc_pix_map_by_index(unsigned int i)
{
	if (i >= ARRAY_SIZE(vimc_pix_map_list))
		return NULL;

	return &vimc_pix_map_list[i];
}
EXPORT_SYMBOL_GPL(vimc_pix_map_by_index);

const struct vimc_pix_map *vimc_pix_map_by_code(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(vimc_pix_map_list); i++) {
		if (vimc_pix_map_list[i].code == code)
			return &vimc_pix_map_list[i];
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(vimc_pix_map_by_code);

const struct vimc_pix_map *vimc_pix_map_by_pixelformat(u32 pixelformat)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(vimc_pix_map_list); i++) {
		if (vimc_pix_map_list[i].pixelformat == pixelformat)
			return &vimc_pix_map_list[i];
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(vimc_pix_map_by_pixelformat);

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
		const struct vimc_pix_map *vpix;
		struct v4l2_pix_format vdev_fmt;

		if (!ved->vdev_get_format)
			return -ENOIOCTLCMD;

		ved->vdev_get_format(ved, &vdev_fmt);
		vpix = vimc_pix_map_by_pixelformat(vdev_fmt.pixelformat);
		v4l2_fill_mbus_format(&fmt->format, &vdev_fmt, vpix->code);
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
	    || source_fmt.format.code != sink_fmt.format.code)
		return -EPIPE;

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
	sd->entity.function = function;
	sd->entity.ops = &vimc_ent_sd_mops;
	sd->owner = THIS_MODULE;
	strlcpy(sd->name, name, sizeof(sd->name));
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
	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(ved->ent);
	vimc_pads_cleanup(ved->pads);
}
EXPORT_SYMBOL_GPL(vimc_ent_sd_unregister);

MODULE_DESCRIPTION("Virtual Media Controller Driver (VIMC) Common");
MODULE_AUTHOR("Helen Koike <helen.fornazier@gmail.com>");
MODULE_LICENSE("GPL");
