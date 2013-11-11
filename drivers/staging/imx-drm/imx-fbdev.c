/*
 * i.MX drm driver
 *
 * Copyright (C) 2012 Sascha Hauer, Pengutronix
 *
 * Based on Samsung Exynos code
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/module.h>
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>

#include "imx-drm.h"

#define MAX_CONNECTOR		4
#define PREFERRED_BPP		16

static struct drm_fbdev_cma *fbdev_cma;

static int legacyfb_depth = 16;

module_param(legacyfb_depth, int, 0444);

static int __init imx_fb_helper_init(void)
{
	struct drm_device *drm = imx_drm_device_get();

	if (!drm)
		return -EINVAL;

	if (legacyfb_depth != 16 && legacyfb_depth != 32) {
		pr_warn("i.MX legacyfb: invalid legacyfb_depth setting. defaulting to 16bpp\n");
		legacyfb_depth = 16;
	}

	fbdev_cma = drm_fbdev_cma_init(drm, legacyfb_depth,
			drm->mode_config.num_crtc, MAX_CONNECTOR);

	if (IS_ERR(fbdev_cma)) {
		imx_drm_device_put();
		return PTR_ERR(fbdev_cma);
	}

	imx_drm_fb_helper_set(fbdev_cma);

	return 0;
}

static void __exit imx_fb_helper_exit(void)
{
	imx_drm_fb_helper_set(NULL);
	drm_fbdev_cma_fini(fbdev_cma);
	imx_drm_device_put();
}

late_initcall(imx_fb_helper_init);
module_exit(imx_fb_helper_exit);

MODULE_DESCRIPTION("Freescale i.MX legacy fb driver");
MODULE_AUTHOR("Sascha Hauer, Pengutronix");
MODULE_LICENSE("GPL");
