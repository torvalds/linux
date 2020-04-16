// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * vimc-common.c Virtual Media Controller Driver
 *
 * Copyright (C) 2015-2017 Helen Koike <helen.fornazier@gmail.com>
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

bool vimc_is_source(struct media_entity *ent)
{
	unsigned int i;

	for (i = 0; i < ent->num_pads; i++)
		if (ent->pads[i].flags & MEDIA_PAD_FL_SINK)
			return false;
	return true;
}

const struct vimc_pix_map *vimc_pix_map_by_index(unsigned int i)
{
	if (i >= ARRAY_SIZE(vimc_pix_map_list))
		return NULL;

	return &vimc_pix_map_list[i];
}

const struct vimc_pix_map *vimc_pix_map_by_code(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(vimc_pix_map_list); i++) {
		if (vimc_pix_map_list[i].code == code)
			return &vimc_pix_map_list[i];
	}
	return NULL;
}

const struct vimc_pix_map *vimc_pix_map_by_pixelformat(u32 pixelformat)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(vimc_pix_map_list); i++) {
		if (vimc_pix_map_list[i].pixelformat == pixelformat)
			return &vimc_pix_map_list[i];
	}
	return NULL;
}

static int vimc_get_pix_format(struct media_pad *pad,
			       struct v4l2_pix_format *fmt)
{
	if (is_media_entity_v4l2_subdev(pad->entity)) {
		struct v4l2_subdev *sd =
			media_entity_to_v4l2_subdev(pad->entity);
		struct v4l2_subdev_format sd_fmt;
		const struct vimc_pix_map *pix_map;
		int ret;

		sd_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		sd_fmt.pad = pad->index;

		ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &sd_fmt);
		if (ret)
			return ret;

		v4l2_fill_pix_format(fmt, &sd_fmt.format);
		pix_map = vimc_pix_map_by_code(sd_fmt.format.code);
		fmt->pixelformat = pix_map->pixelformat;
	} else if (is_media_entity_v4l2_video_device(pad->entity)) {
		struct video_device *vdev = container_of(pad->entity,
							 struct video_device,
							 entity);
		struct vimc_ent_device *ved = video_get_drvdata(vdev);

		if (!ved->vdev_get_format)
			return -ENOIOCTLCMD;

		ved->vdev_get_format(ved, fmt);
	} else {
		return -EINVAL;
	}

	return 0;
}

int vimc_vdev_link_validate(struct media_link *link)
{
	struct v4l2_pix_format source_fmt, sink_fmt;
	int ret;

	ret = vimc_get_pix_format(link->source, &source_fmt);
	if (ret)
		return ret;

	ret = vimc_get_pix_format(link->sink, &sink_fmt);
	if (ret)
		return ret;

	pr_info("vimc link validate: "
		"%s:src:%dx%d (0x%x, %d, %d, %d, %d) "
		"%s:snk:%dx%d (0x%x, %d, %d, %d, %d)\n",
		/* src */
		link->source->entity->name,
		source_fmt.width, source_fmt.height,
		source_fmt.pixelformat, source_fmt.colorspace,
		source_fmt.quantization, source_fmt.xfer_func,
		source_fmt.ycbcr_enc,
		/* sink */
		link->sink->entity->name,
		sink_fmt.width, sink_fmt.height,
		sink_fmt.pixelformat, sink_fmt.colorspace,
		sink_fmt.quantization, sink_fmt.xfer_func,
		sink_fmt.ycbcr_enc);

	/* The width, height and pixelformat must match. */
	if (source_fmt.width != sink_fmt.width ||
	    source_fmt.height != sink_fmt.height ||
	    source_fmt.pixelformat != sink_fmt.pixelformat)
		return -EPIPE;

	/*
	 * The field order must match, or the sink field order must be NONE
	 * to support interlaced hardware connected to bridges that support
	 * progressive formats only.
	 */
	if (source_fmt.field != sink_fmt.field &&
	    sink_fmt.field != V4L2_FIELD_NONE)
		return -EPIPE;

	/*
	 * If colorspace is DEFAULT, then assume all the colorimetry is also
	 * DEFAULT, return 0 to skip comparing the other colorimetry parameters
	 */
	if (source_fmt.colorspace == V4L2_COLORSPACE_DEFAULT ||
	    sink_fmt.colorspace == V4L2_COLORSPACE_DEFAULT)
		return 0;

	/* Colorspace must match. */
	if (source_fmt.colorspace != sink_fmt.colorspace)
		return -EPIPE;

	/* Colorimetry must match if they are not set to DEFAULT */
	if (source_fmt.ycbcr_enc != V4L2_YCBCR_ENC_DEFAULT &&
	    sink_fmt.ycbcr_enc != V4L2_YCBCR_ENC_DEFAULT &&
	    source_fmt.ycbcr_enc != sink_fmt.ycbcr_enc)
		return -EPIPE;

	if (source_fmt.quantization != V4L2_QUANTIZATION_DEFAULT &&
	    sink_fmt.quantization != V4L2_QUANTIZATION_DEFAULT &&
	    source_fmt.quantization != sink_fmt.quantization)
		return -EPIPE;

	if (source_fmt.xfer_func != V4L2_XFER_FUNC_DEFAULT &&
	    sink_fmt.xfer_func != V4L2_XFER_FUNC_DEFAULT &&
	    source_fmt.xfer_func != sink_fmt.xfer_func)
		return -EPIPE;

	return 0;
}

static const struct media_entity_operations vimc_ent_sd_mops = {
	.link_validate = v4l2_subdev_link_validate,
};

int vimc_ent_sd_register(struct vimc_ent_device *ved,
			 struct v4l2_subdev *sd,
			 struct v4l2_device *v4l2_dev,
			 const char *const name,
			 u32 function,
			 u16 num_pads,
			 struct media_pad *pads,
			 const struct v4l2_subdev_ops *sd_ops)
{
	int ret;

	/* Fill the vimc_ent_device struct */
	ved->ent = &sd->entity;

	/* Initialize the subdev */
	v4l2_subdev_init(sd, sd_ops);
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
	ret = media_entity_pads_init(&sd->entity, num_pads, pads);
	if (ret)
		return ret;

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
	return ret;
}
