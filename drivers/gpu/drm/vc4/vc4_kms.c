/*
 * Copyright (C) 2015 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/**
 * DOC: VC4 KMS
 *
 * This is the general code for implementing KMS mode setting that
 * doesn't clearly associate with any of the other objects (plane,
 * crtc, HDMI encoder).
 */

#include "drm_crtc.h"
#include "drm_atomic_helper.h"
#include "drm_crtc_helper.h"
#include "drm_plane_helper.h"
#include "drm_fb_cma_helper.h"
#include "vc4_drv.h"

static void vc4_output_poll_changed(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	if (vc4->fbdev)
		drm_fbdev_cma_hotplug_event(vc4->fbdev);
}

static const struct drm_mode_config_funcs vc4_mode_funcs = {
	.output_poll_changed = vc4_output_poll_changed,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
	.fb_create = drm_fb_cma_create,
};

int vc4_kms_load(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	int ret;

	ret = drm_vblank_init(dev, dev->mode_config.num_crtc);
	if (ret < 0) {
		dev_err(dev->dev, "failed to initialize vblank\n");
		return ret;
	}

	dev->mode_config.max_width = 2048;
	dev->mode_config.max_height = 2048;
	dev->mode_config.funcs = &vc4_mode_funcs;
	dev->mode_config.preferred_depth = 24;

	drm_mode_config_reset(dev);

	vc4->fbdev = drm_fbdev_cma_init(dev, 32,
					dev->mode_config.num_crtc,
					dev->mode_config.num_connector);
	if (IS_ERR(vc4->fbdev))
		vc4->fbdev = NULL;

	drm_kms_helper_poll_init(dev);

	return 0;
}
