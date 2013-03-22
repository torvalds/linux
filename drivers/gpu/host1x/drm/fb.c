/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "drm.h"

static void tegra_drm_fb_output_poll_changed(struct drm_device *drm)
{
	struct host1x_drm *host1x = drm->dev_private;

	drm_fbdev_cma_hotplug_event(host1x->fbdev);
}

static const struct drm_mode_config_funcs tegra_drm_mode_funcs = {
	.fb_create = drm_fb_cma_create,
	.output_poll_changed = tegra_drm_fb_output_poll_changed,
};

int tegra_drm_fb_init(struct drm_device *drm)
{
	struct host1x_drm *host1x = drm->dev_private;
	struct drm_fbdev_cma *fbdev;

	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;

	drm->mode_config.max_width = 4096;
	drm->mode_config.max_height = 4096;

	drm->mode_config.funcs = &tegra_drm_mode_funcs;

	fbdev = drm_fbdev_cma_init(drm, 32, drm->mode_config.num_crtc,
				   drm->mode_config.num_connector);
	if (IS_ERR(fbdev))
		return PTR_ERR(fbdev);

	host1x->fbdev = fbdev;

	return 0;
}

void tegra_drm_fb_exit(struct drm_device *drm)
{
	struct host1x_drm *host1x = drm->dev_private;

	drm_fbdev_cma_fini(host1x->fbdev);
}
