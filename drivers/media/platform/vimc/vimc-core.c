/*
 * vimc-core.c Virtual Media Controller Driver
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
#include <linux/platform_device.h>
#include <media/media-device.h>
#include <media/v4l2-device.h>

#include "vimc-capture.h"
#include "vimc-core.h"
#include "vimc-sensor.h"

#define VIMC_PDEV_NAME "vimc"
#define VIMC_MDEV_MODEL_NAME "VIMC MDEV"

#define VIMC_ENT_LINK(src, srcpad, sink, sinkpad, link_flags) {	\
	.src_ent = src,						\
	.src_pad = srcpad,					\
	.sink_ent = sink,					\
	.sink_pad = sinkpad,					\
	.flags = link_flags,					\
}

struct vimc_device {
	/*
	 * The pipeline configuration
	 * (filled before calling vimc_device_register)
	 */
	const struct vimc_pipeline_config *pipe_cfg;

	/* The Associated media_device parent */
	struct media_device mdev;

	/* Internal v4l2 parent device*/
	struct v4l2_device v4l2_dev;

	/* Internal topology */
	struct vimc_ent_device **ved;
};

/**
 * enum vimc_ent_node - Select the functionality of a node in the topology
 * @VIMC_ENT_NODE_SENSOR:	A node of type SENSOR simulates a camera sensor
 *				generating internal images in bayer format and
 *				propagating those images through the pipeline
 * @VIMC_ENT_NODE_CAPTURE:	A node of type CAPTURE is a v4l2 video_device
 *				that exposes the received image from the
 *				pipeline to the user space
 * @VIMC_ENT_NODE_INPUT:	A node of type INPUT is a v4l2 video_device that
 *				receives images from the user space and
 *				propagates them through the pipeline
 * @VIMC_ENT_NODE_DEBAYER:	A node type DEBAYER expects to receive a frame
 *				in bayer format converts it to RGB
 * @VIMC_ENT_NODE_SCALER:	A node of type SCALER scales the received image
 *				by a given multiplier
 *
 * This enum is used in the entity configuration struct to allow the definition
 * of a custom topology specifying the role of each node on it.
 */
enum vimc_ent_node {
	VIMC_ENT_NODE_SENSOR,
	VIMC_ENT_NODE_CAPTURE,
	VIMC_ENT_NODE_INPUT,
	VIMC_ENT_NODE_DEBAYER,
	VIMC_ENT_NODE_SCALER,
};

/* Structure which describes individual configuration for each entity */
struct vimc_ent_config {
	const char *name;
	size_t pads_qty;
	const unsigned long *pads_flag;
	enum vimc_ent_node node;
};

/* Structure which describes links between entities */
struct vimc_ent_link {
	unsigned int src_ent;
	u16 src_pad;
	unsigned int sink_ent;
	u16 sink_pad;
	u32 flags;
};

/* Structure which describes the whole topology */
struct vimc_pipeline_config {
	const struct vimc_ent_config *ents;
	size_t num_ents;
	const struct vimc_ent_link *links;
	size_t num_links;
};

/* --------------------------------------------------------------------------
 * Topology Configuration
 */

static const struct vimc_ent_config ent_config[] = {
	{
		.name = "Sensor A",
		.pads_qty = 1,
		.pads_flag = (const unsigned long[]){MEDIA_PAD_FL_SOURCE},
		.node = VIMC_ENT_NODE_SENSOR,
	},
	{
		.name = "Sensor B",
		.pads_qty = 1,
		.pads_flag = (const unsigned long[]){MEDIA_PAD_FL_SOURCE},
		.node = VIMC_ENT_NODE_SENSOR,
	},
	{
		.name = "Debayer A",
		.pads_qty = 2,
		.pads_flag = (const unsigned long[]){MEDIA_PAD_FL_SINK,
						     MEDIA_PAD_FL_SOURCE},
		.node = VIMC_ENT_NODE_DEBAYER,
	},
	{
		.name = "Debayer B",
		.pads_qty = 2,
		.pads_flag = (const unsigned long[]){MEDIA_PAD_FL_SINK,
						     MEDIA_PAD_FL_SOURCE},
		.node = VIMC_ENT_NODE_DEBAYER,
	},
	{
		.name = "Raw Capture 0",
		.pads_qty = 1,
		.pads_flag = (const unsigned long[]){MEDIA_PAD_FL_SINK},
		.node = VIMC_ENT_NODE_CAPTURE,
	},
	{
		.name = "Raw Capture 1",
		.pads_qty = 1,
		.pads_flag = (const unsigned long[]){MEDIA_PAD_FL_SINK},
		.node = VIMC_ENT_NODE_CAPTURE,
	},
	{
		.name = "RGB/YUV Input",
		.pads_qty = 1,
		.pads_flag = (const unsigned long[]){MEDIA_PAD_FL_SOURCE},
		.node = VIMC_ENT_NODE_INPUT,
	},
	{
		.name = "Scaler",
		.pads_qty = 2,
		.pads_flag = (const unsigned long[]){MEDIA_PAD_FL_SINK,
						     MEDIA_PAD_FL_SOURCE},
		.node = VIMC_ENT_NODE_SCALER,
	},
	{
		.name = "RGB/YUV Capture",
		.pads_qty = 1,
		.pads_flag = (const unsigned long[]){MEDIA_PAD_FL_SINK},
		.node = VIMC_ENT_NODE_CAPTURE,
	},
};

static const struct vimc_ent_link ent_links[] = {
	/* Link: Sensor A (Pad 0)->(Pad 0) Debayer A */
	VIMC_ENT_LINK(0, 0, 2, 0, MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE),
	/* Link: Sensor A (Pad 0)->(Pad 0) Raw Capture 0 */
	VIMC_ENT_LINK(0, 0, 4, 0, MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE),
	/* Link: Sensor B (Pad 0)->(Pad 0) Debayer B */
	VIMC_ENT_LINK(1, 0, 3, 0, MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE),
	/* Link: Sensor B (Pad 0)->(Pad 0) Raw Capture 1 */
	VIMC_ENT_LINK(1, 0, 5, 0, MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE),
	/* Link: Debayer A (Pad 1)->(Pad 0) Scaler */
	VIMC_ENT_LINK(2, 1, 7, 0, MEDIA_LNK_FL_ENABLED),
	/* Link: Debayer B (Pad 1)->(Pad 0) Scaler */
	VIMC_ENT_LINK(3, 1, 7, 0, 0),
	/* Link: RGB/YUV Input (Pad 0)->(Pad 0) Scaler */
	VIMC_ENT_LINK(6, 0, 7, 0, 0),
	/* Link: Scaler (Pad 1)->(Pad 0) RGB/YUV Capture */
	VIMC_ENT_LINK(7, 1, 8, 0, MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE),
};

static const struct vimc_pipeline_config pipe_cfg = {
	.ents		= ent_config,
	.num_ents	= ARRAY_SIZE(ent_config),
	.links		= ent_links,
	.num_links	= ARRAY_SIZE(ent_links)
};

/* -------------------------------------------------------------------------- */

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

static void vimc_device_unregister(struct vimc_device *vimc)
{
	unsigned int i;

	media_device_unregister(&vimc->mdev);
	/* Cleanup (only initialized) entities */
	for (i = 0; i < vimc->pipe_cfg->num_ents; i++) {
		if (vimc->ved[i] && vimc->ved[i]->destroy)
			vimc->ved[i]->destroy(vimc->ved[i]);

		vimc->ved[i] = NULL;
	}
	v4l2_device_unregister(&vimc->v4l2_dev);
	media_device_cleanup(&vimc->mdev);
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

/*
 * TODO: remove this function when all the
 * entities specific code are implemented
 */
static void vimc_raw_destroy(struct vimc_ent_device *ved)
{
	media_device_unregister_entity(ved->ent);

	media_entity_cleanup(ved->ent);

	vimc_pads_cleanup(ved->pads);

	kfree(ved->ent);

	kfree(ved);
}

/*
 * TODO: remove this function when all the
 * entities specific code are implemented
 */
static struct vimc_ent_device *vimc_raw_create(struct v4l2_device *v4l2_dev,
					       const char *const name,
					       u16 num_pads,
					       const unsigned long *pads_flag)
{
	struct vimc_ent_device *ved;
	int ret;

	/* Allocate the main ved struct */
	ved = kzalloc(sizeof(*ved), GFP_KERNEL);
	if (!ved)
		return ERR_PTR(-ENOMEM);

	/* Allocate the media entity */
	ved->ent = kzalloc(sizeof(*ved->ent), GFP_KERNEL);
	if (!ved->ent) {
		ret = -ENOMEM;
		goto err_free_ved;
	}

	/* Allocate the pads */
	ved->pads = vimc_pads_init(num_pads, pads_flag);
	if (IS_ERR(ved->pads)) {
		ret = PTR_ERR(ved->pads);
		goto err_free_ent;
	}

	/* Initialize the media entity */
	ved->ent->name = name;
	ved->ent->function = MEDIA_ENT_F_V4L2_SUBDEV_UNKNOWN;
	ret = media_entity_pads_init(ved->ent, num_pads, ved->pads);
	if (ret)
		goto err_cleanup_pads;

	/* Register the media entity */
	ret = media_device_register_entity(v4l2_dev->mdev, ved->ent);
	if (ret)
		goto err_cleanup_entity;

	/* Fill out the destroy function and return */
	ved->destroy = vimc_raw_destroy;
	return ved;

err_cleanup_entity:
	media_entity_cleanup(ved->ent);
err_cleanup_pads:
	vimc_pads_cleanup(ved->pads);
err_free_ent:
	kfree(ved->ent);
err_free_ved:
	kfree(ved);

	return ERR_PTR(ret);
}

static int vimc_device_register(struct vimc_device *vimc)
{
	unsigned int i;
	int ret;

	/* Allocate memory for the vimc_ent_devices pointers */
	vimc->ved = devm_kcalloc(vimc->mdev.dev, vimc->pipe_cfg->num_ents,
				 sizeof(*vimc->ved), GFP_KERNEL);
	if (!vimc->ved)
		return -ENOMEM;

	/* Link the media device within the v4l2_device */
	vimc->v4l2_dev.mdev = &vimc->mdev;

	/* Register the v4l2 struct */
	ret = v4l2_device_register(vimc->mdev.dev, &vimc->v4l2_dev);
	if (ret) {
		dev_err(vimc->mdev.dev,
			"v4l2 device register failed (err=%d)\n", ret);
		return ret;
	}

	/* Initialize entities */
	for (i = 0; i < vimc->pipe_cfg->num_ents; i++) {
		struct vimc_ent_device *(*create_func)(struct v4l2_device *,
						       const char *const,
						       u16,
						       const unsigned long *);

		/* Register the specific node */
		switch (vimc->pipe_cfg->ents[i].node) {
		case VIMC_ENT_NODE_SENSOR:
			create_func = vimc_sen_create;
			break;

		case VIMC_ENT_NODE_CAPTURE:
			create_func = vimc_cap_create;
			break;

		/* TODO: Instantiate the specific topology node */
		case VIMC_ENT_NODE_INPUT:
		case VIMC_ENT_NODE_DEBAYER:
		case VIMC_ENT_NODE_SCALER:
		default:
			/*
			 * TODO: remove this when all the entities specific
			 * code are implemented
			 */
			create_func = vimc_raw_create;
			break;
		}

		vimc->ved[i] = create_func(&vimc->v4l2_dev,
					   vimc->pipe_cfg->ents[i].name,
					   vimc->pipe_cfg->ents[i].pads_qty,
					   vimc->pipe_cfg->ents[i].pads_flag);
		if (IS_ERR(vimc->ved[i])) {
			ret = PTR_ERR(vimc->ved[i]);
			vimc->ved[i] = NULL;
			goto err;
		}
	}

	/* Initialize the links between entities */
	for (i = 0; i < vimc->pipe_cfg->num_links; i++) {
		const struct vimc_ent_link *link = &vimc->pipe_cfg->links[i];

		ret = media_create_pad_link(vimc->ved[link->src_ent]->ent,
					    link->src_pad,
					    vimc->ved[link->sink_ent]->ent,
					    link->sink_pad,
					    link->flags);
		if (ret)
			goto err;
	}

	/* Register the media device */
	ret = media_device_register(&vimc->mdev);
	if (ret) {
		dev_err(vimc->mdev.dev,
			"media device register failed (err=%d)\n", ret);
		return ret;
	}

	/* Expose all subdev's nodes*/
	ret = v4l2_device_register_subdev_nodes(&vimc->v4l2_dev);
	if (ret) {
		dev_err(vimc->mdev.dev,
			"vimc subdev nodes registration failed (err=%d)\n",
			ret);
		goto err;
	}

	return 0;

err:
	/* Destroy the so far created topology */
	vimc_device_unregister(vimc);

	return ret;
}

static int vimc_probe(struct platform_device *pdev)
{
	struct vimc_device *vimc;
	int ret;

	/* Prepare the vimc topology structure */

	/* Allocate memory for the vimc structure */
	vimc = kzalloc(sizeof(*vimc), GFP_KERNEL);
	if (!vimc)
		return -ENOMEM;

	/* Set the pipeline configuration struct */
	vimc->pipe_cfg = &pipe_cfg;

	/* Initialize media device */
	strlcpy(vimc->mdev.model, VIMC_MDEV_MODEL_NAME,
		sizeof(vimc->mdev.model));
	vimc->mdev.dev = &pdev->dev;
	media_device_init(&vimc->mdev);

	/* Create vimc topology */
	ret = vimc_device_register(vimc);
	if (ret) {
		dev_err(vimc->mdev.dev,
			"vimc device registration failed (err=%d)\n", ret);
		kfree(vimc);
		return ret;
	}

	/* Link the topology object with the platform device object */
	platform_set_drvdata(pdev, vimc);

	return 0;
}

static int vimc_remove(struct platform_device *pdev)
{
	struct vimc_device *vimc = platform_get_drvdata(pdev);

	/* Destroy all the topology */
	vimc_device_unregister(vimc);
	kfree(vimc);

	return 0;
}

static void vimc_dev_release(struct device *dev)
{
}

static struct platform_device vimc_pdev = {
	.name		= VIMC_PDEV_NAME,
	.dev.release	= vimc_dev_release,
};

static struct platform_driver vimc_pdrv = {
	.probe		= vimc_probe,
	.remove		= vimc_remove,
	.driver		= {
		.name	= VIMC_PDEV_NAME,
	},
};

static int __init vimc_init(void)
{
	int ret;

	ret = platform_device_register(&vimc_pdev);
	if (ret) {
		dev_err(&vimc_pdev.dev,
			"platform device registration failed (err=%d)\n", ret);
		return ret;
	}

	ret = platform_driver_register(&vimc_pdrv);
	if (ret) {
		dev_err(&vimc_pdev.dev,
			"platform driver registration failed (err=%d)\n", ret);

		platform_device_unregister(&vimc_pdev);
	}

	return ret;
}

static void __exit vimc_exit(void)
{
	platform_driver_unregister(&vimc_pdrv);

	platform_device_unregister(&vimc_pdev);
}

module_init(vimc_init);
module_exit(vimc_exit);

MODULE_DESCRIPTION("Virtual Media Controller Driver (VIMC)");
MODULE_AUTHOR("Helen Fornazier <helen.fornazier@gmail.com>");
MODULE_LICENSE("GPL");
