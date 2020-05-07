// SPDX-License-Identifier: GPL-2.0-or-later
/* Hisilicon Hibmc SoC drm driver
 *
 * Based on the bochs drm driver.
 *
 * Copyright (c) 2016 Huawei Limited.
 *
 * Author:
 *	Rongrong Zou <zourongrong@huawei.com>
 *	Rongrong Zou <zourongrong@gmail.com>
 *	Jianhua Li <lijianhua@huawei.com>
 */

#include <linux/pci.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_vram_helper.h>
#include <drm/drm_print.h>

#include "hibmc_drm_drv.h"

int hibmc_mm_init(struct hibmc_drm_private *hibmc)
{
	struct drm_vram_mm *vmm;
	int ret;
	struct drm_device *dev = hibmc->dev;

	vmm = drm_vram_helper_alloc_mm(dev,
				       pci_resource_start(dev->pdev, 0),
				       hibmc->fb_size);
	if (IS_ERR(vmm)) {
		ret = PTR_ERR(vmm);
		DRM_ERROR("Error initializing VRAM MM; %d\n", ret);
		return ret;
	}

	return 0;
}

void hibmc_mm_fini(struct hibmc_drm_private *hibmc)
{
	if (!hibmc->dev->vram_mm)
		return;

	drm_vram_helper_release_mm(hibmc->dev);
}

int hibmc_dumb_create(struct drm_file *file, struct drm_device *dev,
		      struct drm_mode_create_dumb *args)
{
	return drm_gem_vram_fill_create_dumb(file, dev, 0, 16, args);
}

const struct drm_mode_config_funcs hibmc_mode_funcs = {
	.mode_valid = drm_vram_helper_mode_valid,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
	.fb_create = drm_gem_fb_create,
};
