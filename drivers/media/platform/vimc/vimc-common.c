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

#include "vimc-common.h"

static const struct vimc_pix_map vimc_pix_map_list[] = {
	/* TODO: add all missing formats */

	/* RGB formats */
	{
		.code = MEDIA_BUS_FMT_BGR888_1X24,
		.pixelformat = V4L2_PIX_FMT_BGR24,
		.bpp = 3,
	},
	{
		.code = MEDIA_BUS_FMT_RGB888_1X24,
		.pixelformat = V4L2_PIX_FMT_RGB24,
		.bpp = 3,
	},
	{
		.code = MEDIA_BUS_FMT_ARGB8888_1X32,
		.pixelformat = V4L2_PIX_FMT_ARGB32,
		.bpp = 4,
	},

	/* Bayer formats */
	{
		.code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.pixelformat = V4L2_PIX_FMT_SBGGR8,
		.bpp = 1,
	},
	{
		.code = MEDIA_BUS_FMT_SGBRG8_1X8,
		.pixelformat = V4L2_PIX_FMT_SGBRG8,
		.bpp = 1,
	},
	{
		.code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.pixelformat = V4L2_PIX_FMT_SGRBG8,
		.bpp = 1,
	},
	{
		.code = MEDIA_BUS_FMT_SRGGB8_1X8,
		.pixelformat = V4L2_PIX_FMT_SRGGB8,
		.bpp = 1,
	},
	{
		.code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.pixelformat = V4L2_PIX_FMT_SBGGR10,
		.bpp = 2,
	},
	{
		.code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.pixelformat = V4L2_PIX_FMT_SGBRG10,
		.bpp = 2,
	},
	{
		.code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.pixelformat = V4L2_PIX_FMT_SGRBG10,
		.bpp = 2,
	},
	{
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.pixelformat = V4L2_PIX_FMT_SRGGB10,
		.bpp = 2,
	},

	/* 10bit raw bayer a-law compressed to 8 bits */
	{
		.code = MEDIA_BUS_FMT_SBGGR10_ALAW8_1X8,
		.pixelformat = V4L2_PIX_FMT_SBGGR10ALAW8,
		.bpp = 1,
	},
	{
		.code = MEDIA_BUS_FMT_SGBRG10_ALAW8_1X8,
		.pixelformat = V4L2_PIX_FMT_SGBRG10ALAW8,
		.bpp = 1,
	},
	{
		.code = MEDIA_BUS_FMT_SGRBG10_ALAW8_1X8,
		.pixelformat = V4L2_PIX_FMT_SGRBG10ALAW8,
		.bpp = 1,
	},
	{
		.code = MEDIA_BUS_FMT_SRGGB10_ALAW8_1X8,
		.pixelformat = V4L2_PIX_FMT_SRGGB10ALAW8,
		.bpp = 1,
	},

	/* 10bit raw bayer DPCM compressed to 8 bits */
	{
		.code = MEDIA_BUS_FMT_SBGGR10_DPCM8_1X8,
		.pixelformat = V4L2_PIX_FMT_SBGGR10DPCM8,
		.bpp = 1,
	},
	{
		.code = MEDIA_BUS_FMT_SGBRG10_DPCM8_1X8,
		.pixelformat = V4L2_PIX_FMT_SGBRG10DPCM8,
		.bpp = 1,
	},
	{
		.code = MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8,
		.pixelformat = V4L2_PIX_FMT_SGRBG10DPCM8,
		.bpp = 1,
	},
	{
		.code = MEDIA_BUS_FMT_SRGGB10_DPCM8_1X8,
		.pixelformat = V4L2_PIX_FMT_SRGGB10DPCM8,
		.bpp = 1,
	},
	{
		.code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.pixelformat = V4L2_PIX_FMT_SBGGR12,
		.bpp = 2,
	},
	{
		.code = MEDIA_BUS_FMT_SGBRG12_1X12,
		.pixelformat = V4L2_PIX_FMT_SGBRG12,
		.bpp = 2,
	},
	{
		.code = MEDIA_BUS_FMT_SGRBG12_1X12,
		.pixelformat = V4L2_PIX_FMT_SGRBG12,
		.bpp = 2,
	},
	{
		.code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.pixelformat = V4L2_PIX_FMT_SRGGB12,
		.bpp = 2,
	},
};

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

int vimc_propagate_frame(struct media_pad *src, const void *frame)
{
	struct media_link *link;

	if (!(src->flags & MEDIA_PAD_FL_SOURCE))
		return -EINVAL;

	/* Send this frame to all sink pads that are direct linked */
	list_for_each_entry(link, &src->entity->links, list) {
		if (link->source == src &&
		    (link->flags & MEDIA_LNK_FL_ENABLED)) {
			struct vimc_ent_device *ved = NULL;
			struct media_entity *entity = link->sink->entity;

			if (is_media_entity_v4l2_subdev(entity)) {
				struct v4l2_subdev *sd =
					container_of(entity, struct v4l2_subdev,
						     entity);
				ved = v4l2_get_subdevdata(sd);
			} else if (is_media_entity_v4l2_video_device(entity)) {
				struct video_device *vdev =
					container_of(entity,
						     struct video_device,
						     entity);
				ved = video_get_drvdata(vdev);
			}
			if (ved && ved->process_frame)
				ved->process_frame(ved, link->sink, frame);
		}
	}

	return 0;
}

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

static const struct media_entity_operations vimc_ent_sd_mops = {
	.link_validate = v4l2_subdev_link_validate,
};

int vimc_ent_sd_register(struct vimc_ent_device *ved,
			 struct v4l2_subdev *sd,
			 struct v4l2_device *v4l2_dev,
			 const char *const name,
			 u32 function,
			 u16 num_pads,
			 const unsigned long *pads_flag,
			 const struct v4l2_subdev_ops *sd_ops,
			 void (*sd_destroy)(struct vimc_ent_device *))
{
	int ret;

	/* Allocate the pads */
	ved->pads = vimc_pads_init(num_pads, pads_flag);
	if (IS_ERR(ved->pads))
		return PTR_ERR(ved->pads);

	/* Fill the vimc_ent_device struct */
	ved->destroy = sd_destroy;
	ved->ent = &sd->entity;

	/* Initialize the subdev */
	v4l2_subdev_init(sd, sd_ops);
	sd->entity.function = function;
	sd->entity.ops = &vimc_ent_sd_mops;
	sd->owner = THIS_MODULE;
	strlcpy(sd->name, name, sizeof(sd->name));
	v4l2_set_subdevdata(sd, ved);

	/* Expose this subdev to user space */
	sd->flags = V4L2_SUBDEV_FL_HAS_DEVNODE;

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

void vimc_ent_sd_unregister(struct vimc_ent_device *ved, struct v4l2_subdev *sd)
{
	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(ved->ent);
	vimc_pads_cleanup(ved->pads);
}
