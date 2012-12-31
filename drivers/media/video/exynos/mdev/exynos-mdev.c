/* drviers/media/video/exynos/mdev/exynos-mdev.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS5 SoC series media device driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/bug.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <media/v4l2-ctrls.h>
#include <media/media-device.h>
#include <media/exynos_mc.h>

static int __devinit mdev_probe(struct platform_device *pdev)
{
	struct v4l2_device *v4l2_dev;
	struct exynos_md *mdev;
	int ret;

	mdev = kzalloc(sizeof(struct exynos_md), GFP_KERNEL);
	if (!mdev)
		return -ENOMEM;

	mdev->id = pdev->id;
	mdev->pdev = pdev;
	spin_lock_init(&mdev->slock);

	snprintf(mdev->media_dev.model, sizeof(mdev->media_dev.model), "%s%d",
		 dev_name(&pdev->dev), mdev->id);

	mdev->media_dev.dev = &pdev->dev;

	v4l2_dev = &mdev->v4l2_dev;
	v4l2_dev->mdev = &mdev->media_dev;
	snprintf(v4l2_dev->name, sizeof(v4l2_dev->name), "%s",
		 dev_name(&pdev->dev));

	ret = v4l2_device_register(&pdev->dev, &mdev->v4l2_dev);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "Failed to register v4l2_device: %d\n", ret);
		goto err_v4l2_reg;
	}
	ret = media_device_register(&mdev->media_dev);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "Failed to register media device: %d\n", ret);
		goto err_mdev_reg;
	}

	platform_set_drvdata(pdev, mdev);
	v4l2_info(v4l2_dev, "Media%d[0x%08x] was registered successfully\n",
		  mdev->id, (unsigned int)mdev);
	return 0;

err_mdev_reg:
	v4l2_device_unregister(&mdev->v4l2_dev);
err_v4l2_reg:
	kfree(mdev);
	return ret;
}

static int __devexit mdev_remove(struct platform_device *pdev)
{
	struct exynos_md *mdev = platform_get_drvdata(pdev);

	if (!mdev)
		return 0;
	media_device_unregister(&mdev->media_dev);
	v4l2_device_unregister(&mdev->v4l2_dev);
	kfree(mdev);
	return 0;
}

static struct platform_driver mdev_driver = {
	.probe		= mdev_probe,
	.remove		= __devexit_p(mdev_remove),
	.driver = {
		.name	= MDEV_MODULE_NAME,
		.owner	= THIS_MODULE,
	}
};

int __init mdev_init(void)
{
	int ret = platform_driver_register(&mdev_driver);
	if (ret)
		err("platform_driver_register failed: %d\n", ret);
	return ret;
}

void __exit mdev_exit(void)
{
	platform_driver_unregister(&mdev_driver);
}

module_init(mdev_init);
module_exit(mdev_exit);

MODULE_AUTHOR("Hyunwoong Kim <khw0178.kim@samsung.com>");
MODULE_DESCRIPTION("EXYNOS5 SoC series media device driver");
MODULE_LICENSE("GPL");
