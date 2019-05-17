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

#include <linux/component.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <media/media-device.h>
#include <media/v4l2-device.h>

#include "vimc-common.h"

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
	/* The platform device */
	struct platform_device pdev;

	/* The pipeline configuration */
	const struct vimc_pipeline_config *pipe_cfg;

	/* The Associated media_device parent */
	struct media_device mdev;

	/* Internal v4l2 parent device*/
	struct v4l2_device v4l2_dev;

	/* Subdevices */
	struct platform_device **subdevs;
};

/* Structure which describes individual configuration for each entity */
struct vimc_ent_config {
	const char *name;
	const char *drv;
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
		.drv = "vimc-sensor",
	},
	{
		.name = "Sensor B",
		.drv = "vimc-sensor",
	},
	{
		.name = "Debayer A",
		.drv = "vimc-debayer",
	},
	{
		.name = "Debayer B",
		.drv = "vimc-debayer",
	},
	{
		.name = "Raw Capture 0",
		.drv = "vimc-capture",
	},
	{
		.name = "Raw Capture 1",
		.drv = "vimc-capture",
	},
	{
		.name = "RGB/YUV Input",
		/* TODO: change this to vimc-input when it is implemented */
		.drv = "vimc-sensor",
	},
	{
		.name = "Scaler",
		.drv = "vimc-scaler",
	},
	{
		.name = "RGB/YUV Capture",
		.drv = "vimc-capture",
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

static int vimc_create_links(struct vimc_device *vimc)
{
	unsigned int i;
	int ret;

	/* Initialize the links between entities */
	for (i = 0; i < vimc->pipe_cfg->num_links; i++) {
		const struct vimc_ent_link *link = &vimc->pipe_cfg->links[i];
		/*
		 * TODO: Check another way of retrieving ved struct without
		 * relying on platform_get_drvdata
		 */
		struct vimc_ent_device *ved_src =
			platform_get_drvdata(vimc->subdevs[link->src_ent]);
		struct vimc_ent_device *ved_sink =
			platform_get_drvdata(vimc->subdevs[link->sink_ent]);

		ret = media_create_pad_link(ved_src->ent, link->src_pad,
					    ved_sink->ent, link->sink_pad,
					    link->flags);
		if (ret)
			return ret;
	}

	return 0;
}

static int vimc_comp_bind(struct device *master)
{
	struct vimc_device *vimc = container_of(to_platform_device(master),
						struct vimc_device, pdev);
	int ret;

	dev_dbg(master, "bind");

	/* Register the v4l2 struct */
	ret = v4l2_device_register(vimc->mdev.dev, &vimc->v4l2_dev);
	if (ret) {
		dev_err(vimc->mdev.dev,
			"v4l2 device register failed (err=%d)\n", ret);
		return ret;
	}

	/* Bind subdevices */
	ret = component_bind_all(master, &vimc->v4l2_dev);
	if (ret)
		goto err_v4l2_unregister;

	/* Initialize links */
	ret = vimc_create_links(vimc);
	if (ret)
		goto err_comp_unbind_all;

	/* Register the media device */
	ret = media_device_register(&vimc->mdev);
	if (ret) {
		dev_err(vimc->mdev.dev,
			"media device register failed (err=%d)\n", ret);
		goto err_comp_unbind_all;
	}

	/* Expose all subdev's nodes*/
	ret = v4l2_device_register_subdev_nodes(&vimc->v4l2_dev);
	if (ret) {
		dev_err(vimc->mdev.dev,
			"vimc subdev nodes registration failed (err=%d)\n",
			ret);
		goto err_mdev_unregister;
	}

	return 0;

err_mdev_unregister:
	media_device_unregister(&vimc->mdev);
err_comp_unbind_all:
	component_unbind_all(master, NULL);
err_v4l2_unregister:
	v4l2_device_unregister(&vimc->v4l2_dev);

	return ret;
}

static void vimc_comp_unbind(struct device *master)
{
	struct vimc_device *vimc = container_of(to_platform_device(master),
						struct vimc_device, pdev);

	dev_dbg(master, "unbind");

	media_device_unregister(&vimc->mdev);
	component_unbind_all(master, NULL);
	v4l2_device_unregister(&vimc->v4l2_dev);
}

static int vimc_comp_compare(struct device *comp, void *data)
{
	return comp == data;
}

static struct component_match *vimc_add_subdevs(struct vimc_device *vimc)
{
	struct component_match *match = NULL;
	struct vimc_platform_data pdata;
	int i;

	for (i = 0; i < vimc->pipe_cfg->num_ents; i++) {
		dev_dbg(&vimc->pdev.dev, "new pdev for %s\n",
			vimc->pipe_cfg->ents[i].drv);

		strlcpy(pdata.entity_name, vimc->pipe_cfg->ents[i].name,
			sizeof(pdata.entity_name));

		vimc->subdevs[i] = platform_device_register_data(&vimc->pdev.dev,
						vimc->pipe_cfg->ents[i].drv,
						PLATFORM_DEVID_AUTO,
						&pdata,
						sizeof(pdata));
		if (IS_ERR(vimc->subdevs[i])) {
			match = ERR_CAST(vimc->subdevs[i]);
			while (--i >= 0)
				platform_device_unregister(vimc->subdevs[i]);

			return match;
		}

		component_match_add(&vimc->pdev.dev, &match, vimc_comp_compare,
				    &vimc->subdevs[i]->dev);
	}

	return match;
}

static void vimc_rm_subdevs(struct vimc_device *vimc)
{
	unsigned int i;

	for (i = 0; i < vimc->pipe_cfg->num_ents; i++)
		platform_device_unregister(vimc->subdevs[i]);
}

static const struct component_master_ops vimc_comp_ops = {
	.bind = vimc_comp_bind,
	.unbind = vimc_comp_unbind,
};

static int vimc_probe(struct platform_device *pdev)
{
	struct vimc_device *vimc = container_of(pdev, struct vimc_device, pdev);
	struct component_match *match = NULL;
	int ret;

	dev_dbg(&pdev->dev, "probe");

	memset(&vimc->mdev, 0, sizeof(vimc->mdev));

	/* Create platform_device for each entity in the topology*/
	vimc->subdevs = devm_kcalloc(&vimc->pdev.dev, vimc->pipe_cfg->num_ents,
				     sizeof(*vimc->subdevs), GFP_KERNEL);
	if (!vimc->subdevs)
		return -ENOMEM;

	match = vimc_add_subdevs(vimc);
	if (IS_ERR(match))
		return PTR_ERR(match);

	/* Link the media device within the v4l2_device */
	vimc->v4l2_dev.mdev = &vimc->mdev;

	/* Initialize media device */
	strlcpy(vimc->mdev.model, VIMC_MDEV_MODEL_NAME,
		sizeof(vimc->mdev.model));
	vimc->mdev.dev = &pdev->dev;
	media_device_init(&vimc->mdev);

	/* Add self to the component system */
	ret = component_master_add_with_match(&pdev->dev, &vimc_comp_ops,
					      match);
	if (ret) {
		media_device_cleanup(&vimc->mdev);
		vimc_rm_subdevs(vimc);
		return ret;
	}

	return 0;
}

static int vimc_remove(struct platform_device *pdev)
{
	struct vimc_device *vimc = container_of(pdev, struct vimc_device, pdev);

	dev_dbg(&pdev->dev, "remove");

	component_master_del(&pdev->dev, &vimc_comp_ops);
	vimc_rm_subdevs(vimc);

	return 0;
}

static void vimc_dev_release(struct device *dev)
{
}

static struct vimc_device vimc_dev = {
	.pipe_cfg = &pipe_cfg,
	.pdev = {
		.name = VIMC_PDEV_NAME,
		.dev.release = vimc_dev_release,
	}
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

	ret = platform_device_register(&vimc_dev.pdev);
	if (ret) {
		dev_err(&vimc_dev.pdev.dev,
			"platform device registration failed (err=%d)\n", ret);
		return ret;
	}

	ret = platform_driver_register(&vimc_pdrv);
	if (ret) {
		dev_err(&vimc_dev.pdev.dev,
			"platform driver registration failed (err=%d)\n", ret);
		platform_driver_unregister(&vimc_pdrv);
		return ret;
	}

	return 0;
}

static void __exit vimc_exit(void)
{
	platform_driver_unregister(&vimc_pdrv);

	platform_device_unregister(&vimc_dev.pdev);
}

module_init(vimc_init);
module_exit(vimc_exit);

MODULE_DESCRIPTION("Virtual Media Controller Driver (VIMC)");
MODULE_AUTHOR("Helen Fornazier <helen.fornazier@gmail.com>");
MODULE_LICENSE("GPL");
