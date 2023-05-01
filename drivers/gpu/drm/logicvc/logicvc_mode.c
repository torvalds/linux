// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#include <linux/types.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_mode_config.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "logicvc_drm.h"
#include "logicvc_interface.h"
#include "logicvc_layer.h"
#include "logicvc_mode.h"

static const struct drm_mode_config_funcs logicvc_mode_config_funcs = {
	.fb_create		= drm_gem_fb_create,
	.atomic_check		= drm_atomic_helper_check,
	.atomic_commit		= drm_atomic_helper_commit,
};

int logicvc_mode_init(struct logicvc_drm *logicvc)
{
	struct drm_device *drm_dev = &logicvc->drm_dev;
	struct drm_mode_config *mode_config = &drm_dev->mode_config;
	struct logicvc_layer *layer_primary;
	uint32_t preferred_depth;
	int ret;

	ret = drm_vblank_init(drm_dev, mode_config->num_crtc);
	if (ret) {
		drm_err(drm_dev, "Failed to initialize vblank\n");
		return ret;
	}

	layer_primary = logicvc_layer_get_primary(logicvc);
	if (!layer_primary) {
		drm_err(drm_dev, "Failed to get primary layer\n");
		return -EINVAL;
	}

	preferred_depth = layer_primary->formats->depth;

	/* DRM counts alpha in depth, our driver doesn't. */
	if (layer_primary->formats->alpha)
		preferred_depth += 8;

	mode_config->min_width = 64;
	mode_config->max_width = 2048;
	mode_config->min_height = 1;
	mode_config->max_height = 2048;
	mode_config->preferred_depth = preferred_depth;
	mode_config->funcs = &logicvc_mode_config_funcs;

	drm_mode_config_reset(drm_dev);

	drm_kms_helper_poll_init(drm_dev);

	return 0;
}

void logicvc_mode_fini(struct logicvc_drm *logicvc)
{
	struct drm_device *drm_dev = &logicvc->drm_dev;

	drm_kms_helper_poll_fini(drm_dev);
}
