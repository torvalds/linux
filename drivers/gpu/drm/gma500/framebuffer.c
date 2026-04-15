// SPDX-License-Identifier: GPL-2.0-only
/**************************************************************************
 * Copyright (c) 2007-2011, Intel Corporation.
 * All Rights Reserved.
 *
 **************************************************************************/

#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modeset_helper.h>

#include "framebuffer.h"
#include "psb_drv.h"

static struct drm_framebuffer *
psb_user_framebuffer_create(struct drm_device *dev, struct drm_file *filp,
			    const struct drm_format_info *info,
			    const struct drm_mode_fb_cmd2 *cmd)
{
	/*
	 * Reject unknown formats, YUV formats, and formats with more than
	 * 4 bytes per pixel.
	 */
	if (!info->depth || info->cpp[0] > 4)
		return ERR_PTR(-EINVAL);

	/*
	 * Pitch must be aligned to 64 bytes.
	 */
	if (cmd->pitches[0] & 63)
		return ERR_PTR(-EINVAL);

	return drm_gem_fb_create(dev, filp, info, cmd);
}

static const struct drm_mode_config_funcs psb_mode_funcs = {
	.fb_create = psb_user_framebuffer_create,
};

static void psb_setup_outputs(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;

	drm_mode_create_scaling_mode_property(dev);

	/* It is ok for this to fail - we just don't get backlight control */
	if (!dev_priv->backlight_property)
		dev_priv->backlight_property = drm_property_create_range(dev, 0,
							"backlight", 0, 100);
	dev_priv->ops->output_init(dev);

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct gma_encoder *gma_encoder = gma_attached_encoder(connector);
		struct drm_encoder *encoder = &gma_encoder->base;
		int crtc_mask = 0, clone_mask = 0;

		/* valid crtcs */
		switch (gma_encoder->type) {
		case INTEL_OUTPUT_ANALOG:
			crtc_mask = (1 << 0);
			clone_mask = (1 << INTEL_OUTPUT_ANALOG);
			break;
		case INTEL_OUTPUT_SDVO:
			crtc_mask = dev_priv->ops->sdvo_mask;
			clone_mask = 0;
			break;
		case INTEL_OUTPUT_LVDS:
			crtc_mask = dev_priv->ops->lvds_mask;
			clone_mask = 0;
			break;
		case INTEL_OUTPUT_MIPI:
			crtc_mask = (1 << 0);
			clone_mask = 0;
			break;
		case INTEL_OUTPUT_MIPI2:
			crtc_mask = (1 << 2);
			clone_mask = 0;
			break;
		case INTEL_OUTPUT_HDMI:
			crtc_mask = dev_priv->ops->hdmi_mask;
			clone_mask = (1 << INTEL_OUTPUT_HDMI);
			break;
		case INTEL_OUTPUT_DISPLAYPORT:
			crtc_mask = (1 << 0) | (1 << 1);
			clone_mask = 0;
			break;
		case INTEL_OUTPUT_EDP:
			crtc_mask = (1 << 1);
			clone_mask = 0;
		}
		encoder->possible_crtcs = crtc_mask;
		encoder->possible_clones =
		    gma_connector_clones(dev, clone_mask);
	}
	drm_connector_list_iter_end(&conn_iter);
}

void psb_modeset_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct psb_intel_mode_device *mode_dev = &dev_priv->mode_dev;
	int i;

	if (drmm_mode_config_init(dev))
		return;

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	dev->mode_config.funcs = &psb_mode_funcs;

	/* num pipes is 2 for PSB but 1 for Mrst */
	for (i = 0; i < dev_priv->num_pipe; i++)
		psb_intel_crtc_init(dev, i, mode_dev);

	dev->mode_config.max_width = 4096;
	dev->mode_config.max_height = 4096;

	psb_setup_outputs(dev);

	if (dev_priv->ops->errata)
	        dev_priv->ops->errata(dev);

        dev_priv->modeset = true;
}

void psb_modeset_cleanup(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	if (dev_priv->modeset) {
		drm_kms_helper_poll_fini(dev);
	}
}
