/*
 * Copyright (C) 2015 Free Electrons
 * Copyright (C) 2015 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drmP.h>

#include "sun4i_drv.h"
#include "sun4i_framebuffer.h"

static void sun4i_de_output_poll_changed(struct drm_device *drm)
{
	struct sun4i_drv *drv = drm->dev_private;

	drm_fbdev_cma_hotplug_event(drv->fbdev);
}

static const struct drm_mode_config_funcs sun4i_de_mode_config_funcs = {
	.output_poll_changed	= sun4i_de_output_poll_changed,
	.atomic_check		= drm_atomic_helper_check,
	.atomic_commit		= drm_atomic_helper_commit,
	.fb_create		= drm_gem_fb_create,
};

struct drm_fbdev_cma *sun4i_framebuffer_init(struct drm_device *drm)
{
	drm_mode_config_reset(drm);

	drm->mode_config.max_width = 8192;
	drm->mode_config.max_height = 8192;

	drm->mode_config.funcs = &sun4i_de_mode_config_funcs;

	return drm_fbdev_cma_init(drm, 32, drm->mode_config.num_connector);
}

void sun4i_framebuffer_free(struct drm_device *drm)
{
	struct sun4i_drv *drv = drm->dev_private;

	drm_fbdev_cma_fini(drv->fbdev);
}
