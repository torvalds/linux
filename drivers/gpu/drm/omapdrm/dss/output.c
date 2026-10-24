// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Archit Taneja <archit@ti.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_graph.h>

#include <drm/drm_bridge.h>

#include "dss.h"
#include "omapdss.h"

int omapdss_device_init_output(struct omap_dss_device *out,
			       struct drm_bridge *local_bridge)
{
	struct drm_bridge *bridge;
	int ret;

	bridge = of_drm_get_bridge_by_endpoint(out->dev->of_node, out->of_port, 0);
	if (IS_ERR(bridge)) {
		dev_dbg(out->dev, "failed to find video sink\n");
		return 0;
	}

	out->bridge = bridge;

	if (local_bridge) {
		if (!out->bridge) {
			ret = -EPROBE_DEFER;
			goto error;
		}

		out->next_bridge = out->bridge;
		out->bridge = drm_bridge_get(local_bridge);
	}

	return 0;

error:
	omapdss_device_cleanup_output(out);
	return ret;
}

void omapdss_device_cleanup_output(struct omap_dss_device *out)
{
	drm_bridge_put(out->next_bridge);
	drm_bridge_put(out->bridge);
}

void dss_mgr_set_timings(struct omap_dss_device *dssdev,
			 const struct videomode *vm)
{
	omap_crtc_dss_set_timings(dssdev->dss->mgr_ops_priv,
					  dssdev->dispc_channel, vm);
}

void dss_mgr_set_lcd_config(struct omap_dss_device *dssdev,
		const struct dss_lcd_mgr_config *config)
{
	omap_crtc_dss_set_lcd_config(dssdev->dss->mgr_ops_priv,
					     dssdev->dispc_channel, config);
}

int dss_mgr_enable(struct omap_dss_device *dssdev)
{
	return omap_crtc_dss_enable(dssdev->dss->mgr_ops_priv,
					    dssdev->dispc_channel);
}

void dss_mgr_disable(struct omap_dss_device *dssdev)
{
	omap_crtc_dss_disable(dssdev->dss->mgr_ops_priv,
				      dssdev->dispc_channel);
}

void dss_mgr_start_update(struct omap_dss_device *dssdev)
{
	omap_crtc_dss_start_update(dssdev->dss->mgr_ops_priv,
					   dssdev->dispc_channel);
}

int dss_mgr_register_framedone_handler(struct omap_dss_device *dssdev,
		void (*handler)(void *), void *data)
{
	struct dss_device *dss = dssdev->dss;

	return omap_crtc_dss_register_framedone(dss->mgr_ops_priv,
							dssdev->dispc_channel,
							handler, data);
}

void dss_mgr_unregister_framedone_handler(struct omap_dss_device *dssdev,
		void (*handler)(void *), void *data)
{
	struct dss_device *dss = dssdev->dss;

	omap_crtc_dss_unregister_framedone(dss->mgr_ops_priv,
						   dssdev->dispc_channel,
						   handler, data);
}
