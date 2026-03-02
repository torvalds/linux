// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Texas Instruments
 * Author: Jyri Sarha <jsarha@ti.com>
 */

#include <linux/of_graph.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_of.h>
#include <drm/drm_simple_kms_helper.h>

#include "tilcdc_drv.h"
#include "tilcdc_encoder.h"

static
int tilcdc_attach_bridge(struct drm_device *ddev, struct drm_bridge *bridge)
{
	struct tilcdc_drm_private *priv = ddev_to_tilcdc_priv(ddev);
	struct drm_connector *connector;
	int ret;

	priv->encoder->base.possible_crtcs = BIT(0);

	ret = drm_bridge_attach(&priv->encoder->base, bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret)
		return ret;

	connector = drm_bridge_connector_init(ddev, &priv->encoder->base);
	if (IS_ERR(connector)) {
		drm_err(ddev, "bridge_connector create failed\n");
		return PTR_ERR(connector);
	}

	ret = drm_connector_attach_encoder(connector, &priv->encoder->base);
	if (ret) {
		drm_err(ddev, "attaching encoder to connector failed\n");
		return ret;
	}

	priv->connector = connector;
	return 0;
}

int tilcdc_encoder_create(struct drm_device *ddev)
{
	struct tilcdc_drm_private *priv = ddev_to_tilcdc_priv(ddev);
	struct tilcdc_encoder *encoder;
	struct drm_bridge *bridge;

	bridge = devm_drm_of_get_bridge(ddev->dev, ddev->dev->of_node, 0, 0);
	if (PTR_ERR(bridge) == -ENODEV)
		return 0;
	else if (IS_ERR(bridge))
		return PTR_ERR(bridge);

	encoder = drmm_simple_encoder_alloc(ddev, struct tilcdc_encoder,
					    base, DRM_MODE_ENCODER_NONE);
	if (IS_ERR(encoder)) {
		drm_err(ddev, "drm_encoder_init() failed %pe\n", encoder);
		return PTR_ERR(encoder);
	}
	priv->encoder = encoder;

	return tilcdc_attach_bridge(ddev, bridge);
}
