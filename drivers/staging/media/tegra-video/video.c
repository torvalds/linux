// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/host1x.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "video.h"

static void tegra_v4l2_dev_release(struct v4l2_device *v4l2_dev)
{
	struct tegra_video_device *vid;

	vid = container_of(v4l2_dev, struct tegra_video_device, v4l2_dev);

	/* cleanup channels here as all video device nodes are released */
	tegra_channels_cleanup(vid->vi);

	v4l2_device_unregister(v4l2_dev);
	media_device_unregister(&vid->media_dev);
	media_device_cleanup(&vid->media_dev);
	kfree(vid);
}

static int host1x_video_probe(struct host1x_device *dev)
{
	struct tegra_video_device *vid;
	int ret;

	vid = kzalloc(sizeof(*vid), GFP_KERNEL);
	if (!vid)
		return -ENOMEM;

	dev_set_drvdata(&dev->dev, vid);

	vid->media_dev.dev = &dev->dev;
	strscpy(vid->media_dev.model, "NVIDIA Tegra Video Input Device",
		sizeof(vid->media_dev.model));

	media_device_init(&vid->media_dev);
	ret = media_device_register(&vid->media_dev);
	if (ret < 0) {
		dev_err(&dev->dev,
			"failed to register media device: %d\n", ret);
		goto cleanup;
	}

	vid->v4l2_dev.mdev = &vid->media_dev;
	vid->v4l2_dev.release = tegra_v4l2_dev_release;
	ret = v4l2_device_register(&dev->dev, &vid->v4l2_dev);
	if (ret < 0) {
		dev_err(&dev->dev,
			"V4L2 device registration failed: %d\n", ret);
		goto unregister_media;
	}

	ret = host1x_device_init(dev);
	if (ret < 0)
		goto unregister_v4l2;

	if (IS_ENABLED(CONFIG_VIDEO_TEGRA_TPG)) {
		/*
		 * Both vi and csi channels are available now.
		 * Register v4l2 nodes and create media links for TPG.
		 */
		ret = tegra_v4l2_nodes_setup_tpg(vid);
		if (ret < 0) {
			dev_err(&dev->dev,
				"failed to setup tpg graph: %d\n", ret);
			goto device_exit;
		}
	}

	return 0;

device_exit:
	host1x_device_exit(dev);
	/* vi exit ops does not clean channels, so clean them here */
	tegra_channels_cleanup(vid->vi);
unregister_v4l2:
	v4l2_device_unregister(&vid->v4l2_dev);
unregister_media:
	media_device_unregister(&vid->media_dev);
cleanup:
	media_device_cleanup(&vid->media_dev);
	kfree(vid);
	return ret;
}

static int host1x_video_remove(struct host1x_device *dev)
{
	struct tegra_video_device *vid = dev_get_drvdata(&dev->dev);

	if (IS_ENABLED(CONFIG_VIDEO_TEGRA_TPG))
		tegra_v4l2_nodes_cleanup_tpg(vid);

	host1x_device_exit(dev);

	/* This calls v4l2_dev release callback on last reference */
	v4l2_device_put(&vid->v4l2_dev);

	return 0;
}

static const struct of_device_id host1x_video_subdevs[] = {
#if defined(CONFIG_ARCH_TEGRA_210_SOC)
	{ .compatible = "nvidia,tegra210-csi", },
	{ .compatible = "nvidia,tegra210-vi", },
#endif
	{ }
};

static struct host1x_driver host1x_video_driver = {
	.driver = {
		.name = "tegra-video",
	},
	.probe = host1x_video_probe,
	.remove = host1x_video_remove,
	.subdevs = host1x_video_subdevs,
};

static struct platform_driver * const drivers[] = {
	&tegra_csi_driver,
	&tegra_vi_driver,
};

static int __init host1x_video_init(void)
{
	int err;

	err = host1x_driver_register(&host1x_video_driver);
	if (err < 0)
		return err;

	err = platform_register_drivers(drivers, ARRAY_SIZE(drivers));
	if (err < 0)
		goto unregister_host1x;

	return 0;

unregister_host1x:
	host1x_driver_unregister(&host1x_video_driver);
	return err;
}
module_init(host1x_video_init);

static void __exit host1x_video_exit(void)
{
	platform_unregister_drivers(drivers, ARRAY_SIZE(drivers));
	host1x_driver_unregister(&host1x_video_driver);
}
module_exit(host1x_video_exit);

MODULE_AUTHOR("Sowjanya Komatineni <skomatineni@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra Host1x Video driver");
MODULE_LICENSE("GPL v2");
