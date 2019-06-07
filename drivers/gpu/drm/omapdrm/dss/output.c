/*
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Archit Taneja <archit@ti.com>
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_graph.h>

#include <drm/drm_panel.h>

#include "dss.h"
#include "omapdss.h"

int omapdss_device_init_output(struct omap_dss_device *out)
{
	struct device_node *remote_node;

	remote_node = of_graph_get_remote_node(out->dev->of_node, 0, 0);
	if (!remote_node) {
		dev_dbg(out->dev, "failed to find video sink\n");
		return 0;
	}

	out->next = omapdss_find_device_by_node(remote_node);
	out->bridge = of_drm_find_bridge(remote_node);
	out->panel = of_drm_find_panel(remote_node);
	if (IS_ERR(out->panel))
		out->panel = NULL;

	of_node_put(remote_node);

	if (out->next && out->type != out->next->type) {
		dev_err(out->dev, "output type and display type don't match\n");
		omapdss_device_put(out->next);
		out->next = NULL;
		return -EINVAL;
	}

	return out->next || out->bridge || out->panel ? 0 : -EPROBE_DEFER;
}
EXPORT_SYMBOL(omapdss_device_init_output);

void omapdss_device_cleanup_output(struct omap_dss_device *out)
{
	if (out->next)
		omapdss_device_put(out->next);
}
EXPORT_SYMBOL(omapdss_device_cleanup_output);

int dss_install_mgr_ops(struct dss_device *dss,
			const struct dss_mgr_ops *mgr_ops,
			struct omap_drm_private *priv)
{
	if (dss->mgr_ops)
		return -EBUSY;

	dss->mgr_ops = mgr_ops;
	dss->mgr_ops_priv = priv;

	return 0;
}
EXPORT_SYMBOL(dss_install_mgr_ops);

void dss_uninstall_mgr_ops(struct dss_device *dss)
{
	dss->mgr_ops = NULL;
	dss->mgr_ops_priv = NULL;
}
EXPORT_SYMBOL(dss_uninstall_mgr_ops);

void dss_mgr_set_timings(struct omap_dss_device *dssdev,
			 const struct videomode *vm)
{
	dssdev->dss->mgr_ops->set_timings(dssdev->dss->mgr_ops_priv,
					  dssdev->dispc_channel, vm);
}
EXPORT_SYMBOL(dss_mgr_set_timings);

void dss_mgr_set_lcd_config(struct omap_dss_device *dssdev,
		const struct dss_lcd_mgr_config *config)
{
	dssdev->dss->mgr_ops->set_lcd_config(dssdev->dss->mgr_ops_priv,
					     dssdev->dispc_channel, config);
}
EXPORT_SYMBOL(dss_mgr_set_lcd_config);

int dss_mgr_enable(struct omap_dss_device *dssdev)
{
	return dssdev->dss->mgr_ops->enable(dssdev->dss->mgr_ops_priv,
					    dssdev->dispc_channel);
}
EXPORT_SYMBOL(dss_mgr_enable);

void dss_mgr_disable(struct omap_dss_device *dssdev)
{
	dssdev->dss->mgr_ops->disable(dssdev->dss->mgr_ops_priv,
				      dssdev->dispc_channel);
}
EXPORT_SYMBOL(dss_mgr_disable);

void dss_mgr_start_update(struct omap_dss_device *dssdev)
{
	dssdev->dss->mgr_ops->start_update(dssdev->dss->mgr_ops_priv,
					   dssdev->dispc_channel);
}
EXPORT_SYMBOL(dss_mgr_start_update);

int dss_mgr_register_framedone_handler(struct omap_dss_device *dssdev,
		void (*handler)(void *), void *data)
{
	struct dss_device *dss = dssdev->dss;

	return dss->mgr_ops->register_framedone_handler(dss->mgr_ops_priv,
							dssdev->dispc_channel,
							handler, data);
}
EXPORT_SYMBOL(dss_mgr_register_framedone_handler);

void dss_mgr_unregister_framedone_handler(struct omap_dss_device *dssdev,
		void (*handler)(void *), void *data)
{
	struct dss_device *dss = dssdev->dss;

	dss->mgr_ops->unregister_framedone_handler(dss->mgr_ops_priv,
						   dssdev->dispc_channel,
						   handler, data);
}
EXPORT_SYMBOL(dss_mgr_unregister_framedone_handler);
