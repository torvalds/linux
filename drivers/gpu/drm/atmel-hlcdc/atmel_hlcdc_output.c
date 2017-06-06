/*
 * Copyright (C) 2014 Traphandler
 * Copyright (C) 2014 Free Electrons
 * Copyright (C) 2014 Atmel
 *
 * Author: Jean-Jacques Hiblot <jjhiblot@traphandler.com>
 * Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/of_graph.h>

#include <drm/drmP.h>
#include <drm/drm_of.h>
#include <drm/drm_bridge.h>

#include "atmel_hlcdc_dc.h"

static const struct drm_encoder_funcs atmel_hlcdc_panel_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int atmel_hlcdc_attach_endpoint(struct drm_device *dev,
				       const struct device_node *np)
{
	struct drm_encoder *encoder;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
	int ret;

	encoder = devm_kzalloc(dev->dev, sizeof(*encoder), GFP_KERNEL);
	if (!encoder)
		return -EINVAL;

	ret = drm_encoder_init(dev, encoder,
			       &atmel_hlcdc_panel_encoder_funcs,
			       DRM_MODE_ENCODER_NONE, NULL);
	if (ret)
		return ret;

	encoder->possible_crtcs = 0x1;

	ret = drm_of_find_panel_or_bridge(np, 0, 0, &panel, &bridge);
	if (ret)
		return ret;

	if (panel) {
		bridge = drm_panel_bridge_add(panel, DRM_MODE_CONNECTOR_Unknown);
		if (IS_ERR(bridge))
			return PTR_ERR(bridge);
	}

	if (bridge) {
		ret = drm_bridge_attach(encoder, bridge, NULL);
		if (!ret)
			return 0;

		if (panel)
			drm_panel_bridge_remove(bridge);
	}

	drm_encoder_cleanup(encoder);

	return ret;
}

int atmel_hlcdc_create_outputs(struct drm_device *dev)
{
	struct device_node *remote;
	int ret = -ENODEV;
	int endpoint = 0;

	while (true) {
		/* Loop thru possible multiple connections to the output */
		remote = of_graph_get_remote_node(dev->dev->of_node, 0,
						  endpoint++);
		if (!remote)
			break;

		ret = atmel_hlcdc_attach_endpoint(dev, remote);
		of_node_put(remote);
		if (ret)
			return ret;
	}

	return ret;
}
