/* exynos_drm_encoder.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_encoder.h"
#include "exynos_drm_crtc.h"

static bool
exynos_drm_encoder_mode_fixup(struct drm_encoder *encoder,
			       const struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct exynos_drm_encoder *exynos_encoder = to_exynos_encoder(encoder);
	struct drm_connector *connector;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (connector->encoder != encoder)
			continue;

		if (exynos_encoder->ops->mode_fixup)
			exynos_encoder->ops->mode_fixup(exynos_encoder,
							connector, mode,
							adjusted_mode);
	}

	return true;
}

static void exynos_drm_encoder_mode_set(struct drm_encoder *encoder,
					 struct drm_display_mode *mode,
					 struct drm_display_mode *adjusted_mode)
{
	struct exynos_drm_encoder *exynos_encoder = to_exynos_encoder(encoder);

	if (exynos_encoder->ops->mode_set)
		exynos_encoder->ops->mode_set(exynos_encoder, adjusted_mode);
}

static void exynos_drm_encoder_enable(struct drm_encoder *encoder)
{
	struct exynos_drm_encoder *exynos_encoder = to_exynos_encoder(encoder);

	if (exynos_encoder->ops->enable)
		exynos_encoder->ops->enable(exynos_encoder);
}

static void exynos_drm_encoder_disable(struct drm_encoder *encoder)
{
	struct exynos_drm_encoder *exynos_encoder = to_exynos_encoder(encoder);

	if (exynos_encoder->ops->disable)
		exynos_encoder->ops->disable(exynos_encoder);
}

static struct drm_encoder_helper_funcs exynos_encoder_helper_funcs = {
	.mode_fixup	= exynos_drm_encoder_mode_fixup,
	.mode_set	= exynos_drm_encoder_mode_set,
	.enable		= exynos_drm_encoder_enable,
	.disable	= exynos_drm_encoder_disable,
};

static struct drm_encoder_funcs exynos_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

int exynos_drm_encoder_create(struct drm_device *dev,
			      struct exynos_drm_encoder *exynos_encoder,
			      enum exynos_drm_output_type type)
{
	struct drm_encoder *encoder;
	int pipe;

	pipe = exynos_drm_crtc_get_pipe_from_type(dev, type);
	if (pipe < 0)
		return pipe;

	encoder = &exynos_encoder->base;
	encoder->possible_crtcs = 1 << pipe;

	DRM_DEBUG_KMS("possible_crtcs = 0x%x\n", encoder->possible_crtcs);

	drm_encoder_init(dev, encoder, &exynos_encoder_funcs,
			DRM_MODE_ENCODER_TMDS);

	drm_encoder_helper_add(encoder, &exynos_encoder_helper_funcs);

	DRM_DEBUG_KMS("encoder has been created\n");

	return 0;
}
