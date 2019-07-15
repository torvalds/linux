// SPDX-License-Identifier: GPL-2.0+
// Copyright 2018 IBM Corporation

#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_probe_helper.h>

#include "aspeed_gfx.h"

static int aspeed_gfx_get_modes(struct drm_connector *connector)
{
	return drm_add_modes_noedid(connector, 800, 600);
}

static const struct
drm_connector_helper_funcs aspeed_gfx_connector_helper_funcs = {
	.get_modes = aspeed_gfx_get_modes,
};

static const struct drm_connector_funcs aspeed_gfx_connector_funcs = {
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= drm_connector_cleanup,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

int aspeed_gfx_create_output(struct drm_device *drm)
{
	struct aspeed_gfx *priv = drm->dev_private;
	int ret;

	priv->connector.dpms = DRM_MODE_DPMS_OFF;
	priv->connector.polled = 0;
	drm_connector_helper_add(&priv->connector,
				 &aspeed_gfx_connector_helper_funcs);
	ret = drm_connector_init(drm, &priv->connector,
				 &aspeed_gfx_connector_funcs,
				 DRM_MODE_CONNECTOR_Unknown);
	return ret;
}
