/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#include <mach/clk.h>
#include <linux/dma-mapping.h>
#include <asm/dma-iommu.h>

#include "drm.h"

#define DRIVER_NAME "tegra"
#define DRIVER_DESC "NVIDIA Tegra graphics"
#define DRIVER_DATE "20120330"
#define DRIVER_MAJOR 0
#define DRIVER_MINOR 0
#define DRIVER_PATCHLEVEL 0

static int tegra_drm_load(struct drm_device *drm, unsigned long flags)
{
	struct device *dev = drm->dev;
	struct host1x *host1x;
	int err;

	host1x = dev_get_drvdata(dev);
	drm->dev_private = host1x;
	host1x->drm = drm;

	drm_mode_config_init(drm);

	err = host1x_drm_init(host1x, drm);
	if (err < 0)
		return err;

	err = tegra_drm_fb_init(drm);
	if (err < 0)
		return err;

	drm_kms_helper_poll_init(drm);

	return 0;
}

static int tegra_drm_unload(struct drm_device *drm)
{
	drm_kms_helper_poll_fini(drm);
	tegra_drm_fb_exit(drm);

	drm_mode_config_cleanup(drm);

	return 0;
}

static int tegra_drm_open(struct drm_device *drm, struct drm_file *filp)
{
	return 0;
}

static void tegra_drm_lastclose(struct drm_device *drm)
{
	struct host1x *host1x = drm->dev_private;

	drm_fbdev_cma_restore_mode(host1x->fbdev);
}

static struct drm_ioctl_desc tegra_drm_ioctls[] = {
};

static const struct file_operations tegra_drm_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = drm_gem_cma_mmap,
	.poll = drm_poll,
	.fasync = drm_fasync,
	.read = drm_read,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.llseek = noop_llseek,
};

struct drm_driver tegra_drm_driver = {
	.driver_features = DRIVER_BUS_PLATFORM | DRIVER_MODESET | DRIVER_GEM,
	.load = tegra_drm_load,
	.unload = tegra_drm_unload,
	.open = tegra_drm_open,
	.lastclose = tegra_drm_lastclose,

	.gem_free_object = drm_gem_cma_free_object,
	.gem_vm_ops = &drm_gem_cma_vm_ops,
	.dumb_create = drm_gem_cma_dumb_create,
	.dumb_map_offset = drm_gem_cma_dumb_map_offset,
	.dumb_destroy = drm_gem_cma_dumb_destroy,

	.ioctls = tegra_drm_ioctls,
	.num_ioctls = ARRAY_SIZE(tegra_drm_ioctls),
	.fops = &tegra_drm_fops,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};
