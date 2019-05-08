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
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef HIBMC_DRM_DRV_H
#define HIBMC_DRM_DRV_H

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_vram_helper.h>
#include <drm/ttm/ttm_bo_driver.h>

struct hibmc_framebuffer {
	struct drm_framebuffer fb;
	struct drm_gem_object *obj;
};

struct hibmc_fbdev {
	struct drm_fb_helper helper; /* must be first */
	struct hibmc_framebuffer *fb;
	int size;
};

struct hibmc_drm_private {
	/* hw */
	void __iomem   *mmio;
	void __iomem   *fb_map;
	unsigned long  fb_base;
	unsigned long  fb_size;
	bool msi_enabled;

	/* drm */
	struct drm_device  *dev;
	bool mode_config_initialized;

	/* ttm */
	struct ttm_bo_device bdev;
	bool initialized;

	/* fbdev */
	struct hibmc_fbdev *fbdev;
	bool mm_inited;
};

#define to_hibmc_framebuffer(x) container_of(x, struct hibmc_framebuffer, fb)

void hibmc_set_power_mode(struct hibmc_drm_private *priv,
			  unsigned int power_mode);
void hibmc_set_current_gate(struct hibmc_drm_private *priv,
			    unsigned int gate);

int hibmc_de_init(struct hibmc_drm_private *priv);
int hibmc_vdac_init(struct hibmc_drm_private *priv);
int hibmc_fbdev_init(struct hibmc_drm_private *priv);
void hibmc_fbdev_fini(struct hibmc_drm_private *priv);

int hibmc_gem_create(struct drm_device *dev, u32 size, bool iskernel,
		     struct drm_gem_object **obj);
struct hibmc_framebuffer *
hibmc_framebuffer_init(struct drm_device *dev,
		       const struct drm_mode_fb_cmd2 *mode_cmd,
		       struct drm_gem_object *obj);

int hibmc_mm_init(struct hibmc_drm_private *hibmc);
void hibmc_mm_fini(struct hibmc_drm_private *hibmc);
int hibmc_dumb_create(struct drm_file *file, struct drm_device *dev,
		      struct drm_mode_create_dumb *args);
int hibmc_mmap(struct file *filp, struct vm_area_struct *vma);

extern const struct drm_mode_config_funcs hibmc_mode_funcs;

#endif
