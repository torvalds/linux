/* SPDX-License-Identifier: GPL-2.0-or-later */
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

#ifndef HIBMC_DRM_DRV_H
#define HIBMC_DRM_DRV_H

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_vram_helper.h>
#include <drm/drm_vram_mm_helper.h>

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

	/* fbdev */
	struct hibmc_fbdev *fbdev;
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

extern const struct drm_mode_config_funcs hibmc_mode_funcs;

#endif
