// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * vimc-core.c Virtual Media Controller Driver
 *
 * Copyright (C) 2015-2017 Helen Koike <helen.fornazier@gmail.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <media/media-device.h>
#include <media/v4l2-device.h>

#include "vimc-common.h"

#define VIMC_MDEV_MODEL_NAME "VIMC MDEV"

#define VIMC_ENT_LINK(src, srcpad, sink, sinkpad, link_flags) {	\
	.src_ent = src,						\
	.src_pad = srcpad,					\
	.sink_ent = sink,					\
	.sink_pad = sinkpad,					\
	.flags = link_flags,					\
}

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

static struct vimc_ent_config ent_config[] = {
	{
		.name = "Sensor A",
		.add = vimc_sen_add,
		.rm = vimc_sen_rm,
	},
	{
		.name = "Sensor B",
		.add = vimc_sen_add,
		.rm = vimc_sen_rm,
	},
	{
		.name = "Debayer A",
		.add = vimc_deb_add,
		.rm = vimc_deb_rm,
	},
	{
		.name = "Debayer B",
		.add = vimc_deb_add,
		.rm = vimc_deb_rm,
	},
	{
		.name = "Raw Capture 0",
		.add = vimc_cap_add,
		.rm = vimc_cap_rm,
	},
	{
		.name = "Raw Capture 1",
		.add = vimc_cap_add,
		.rm = vimc_cap_rm,
	},
	{
		/* TODO: change this to vimc-input when it is implemented */
		.name = "RGB/YUV Input",
		.add = vimc_sen_add,
		.rm = vimc_sen_rm,
	},
	{
		.name = "Scaler",
		.add = vimc_sca_add,
		.rm = vimc_sca_rm,
	},
	{
		.name = "RGB/YUV Capture",
		.add = vimc_cap_add,
		.rm = vimc_cap_rm,
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

static struct vimc_pipeline_config pipe_cfg = {
	.ents		= ent_config,
	.num_ents	= ARRAY_SIZE(ent_config),
	.links		= ent_links,
	.num_links	= ARRAY_SIZE(ent_links)
};

/* -------------------------------------------------------------------------- */

static void vimc_rm_links(struct vimc_device *vimc)
{
	unsigned int i;

	for (i = 0; i < vimc->pipe_cfg->num_ents; i++)
		media_entity_remove_links(vimc->ent_devs[i]->ent);
}

static int vimc_create_links(struct vimc_device *vimc)
{
	unsigned int i;
	int ret;

	/* Initialize the links between entities */
	for (i = 0; i < vimc->pipe_cfg->num_links; i++) {
		const struct vimc_ent_link *link = &vimc->pipe_cfg->links[i];

		struct vimc_ent_device *ved_src =
			vimc->ent_devs[link->src_ent];
		struct vimc_ent_device *ved_sink =
			vimc->ent_devs[link->sink_ent];

		ret = media_create_pad_link(ved_src->ent, link->src_pad,
					    ved_sink->ent, link->sink_pad,
					    link->flags);
		if (ret)
			goto err_rm_links;
	}

	return 0;

err_rm_links:
	vimc_rm_links(vimc);
	return ret;
}

static int vimc_add_subdevs(struct vimc_device *vimc)
{
	unsigned int i;

	for (i = 0; i < vimc->pipe_cfg->num_ents; i++) {
		dev_dbg(&vimc->pdev.dev, "new entity for %s\n",
			vimc->pipe_cfg->ents[i].name);
		vimc->ent_devs[i] = vimc->pipe_cfg->ents[i].add(vimc,
					vimc->pipe_cfg->ents[i].name);
		if (!vimc->ent_devs[i]) {
			dev_err(&vimc->pdev.dev, "add new entity for %s\n",
				vimc->pipe_cfg->ents[i].name);
			return -EINVAL;
		}
	}
	return 0;
}

static void vimc_rm_subdevs(struct vimc_device *vimc)
{
	unsigned int i;

	for (i = 0; i < vimc->pipe_cfg->num_ents; i++)
		if (vimc->ent_devs[i])
			vimc->pipe_cfg->ents[i].rm(vimc, vimc->ent_devs[i]);
}

static int vimc_register_devices(struct vimc_device *vimc)
{
	int ret;

	/* Register the v4l2 struct */
	ret = v4l2_device_register(vimc->mdev.dev, &vimc->v4l2_dev);
	if (ret) {
		dev_err(vimc->mdev.dev,
			"v4l2 device register failed (err=%d)\n", ret);
		return ret;
	}

	/* allocate ent_devs */
	vimc->ent_devs = kcalloc(vimc->pipe_cfg->num_ents,
				 sizeof(*vimc->ent_devs), GFP_KERNEL);
	if (!vimc->ent_devs) {
		ret = -ENOMEM;
		goto err_v4l2_unregister;
	}

	/* Invoke entity config hooks to initialize and register subdevs */
	ret = vimc_add_subdevs(vimc);
	if (ret)
		/* remove sundevs that got added */
		goto err_rm_subdevs;

	/* Initialize links */
	ret = vimc_create_links(vimc);
	if (ret)
		goto err_rm_subdevs;

	/* Register the media device */
	ret = media_device_register(&vimc->mdev);
	if (ret) {
		dev_err(vimc->mdev.dev,
			"media device register failed (err=%d)\n", ret);
		goto err_rm_subdevs;
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
	media_device_cleanup(&vimc->mdev);
err_rm_subdevs:
	vimc_rm_subdevs(vimc);
	kfree(vimc->ent_devs);
err_v4l2_unregister:
	v4l2_device_unregister(&vimc->v4l2_dev);

	return ret;
}

static void vimc_unregister(struct vimc_device *vimc)
{
	media_device_unregister(&vimc->mdev);
	media_device_cleanup(&vimc->mdev);
	v4l2_device_unregister(&vimc->v4l2_dev);
	kfree(vimc->ent_devs);
}

static int vimc_probe(struct platform_device *pdev)
{
	struct vimc_device *vimc = container_of(pdev, struct vimc_device, pdev);
	int ret;

	dev_dbg(&pdev->dev, "probe");

	memset(&vimc->mdev, 0, sizeof(vimc->mdev));

	/* Link the media device within the v4l2_device */
	vimc->v4l2_dev.mdev = &vimc->mdev;

	/* Initialize media device */
	strscpy(vimc->mdev.model, VIMC_MDEV_MODEL_NAME,
		sizeof(vimc->mdev.model));
	snprintf(vimc->mdev.bus_info, sizeof(vimc->mdev.bus_info),
		 "platform:%s", VIMC_PDEV_NAME);
	vimc->mdev.dev = &pdev->dev;
	media_device_init(&vimc->mdev);

	ret = vimc_register_devices(vimc);
	if (ret) {
		media_device_cleanup(&vimc->mdev);
		return ret;
	}

	return 0;
}

static int vimc_remove(struct platform_device *pdev)
{
	struct vimc_device *vimc = container_of(pdev, struct vimc_device, pdev);

	dev_dbg(&pdev->dev, "remove");

	vimc_rm_subdevs(vimc);
	vimc_unregister(vimc);

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
