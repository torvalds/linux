/*
 * Copyright (C) 2012 Texas Instruments Ltd
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

#include <video/omapdss.h>

#include "dss.h"

static LIST_HEAD(output_list);
static DEFINE_MUTEX(output_lock);

int omapdss_output_set_device(struct omap_dss_output *out,
		struct omap_dss_device *dssdev)
{
	int r;

	mutex_lock(&output_lock);

	if (out->device) {
		DSSERR("output already has device %s connected to it\n",
			out->device->name);
		r = -EINVAL;
		goto err;
	}

	if (out->type != dssdev->type) {
		DSSERR("output type and display type don't match\n");
		r = -EINVAL;
		goto err;
	}

	out->device = dssdev;
	dssdev->output = out;

	mutex_unlock(&output_lock);

	return 0;
err:
	mutex_unlock(&output_lock);

	return r;
}
EXPORT_SYMBOL(omapdss_output_set_device);

int omapdss_output_unset_device(struct omap_dss_output *out)
{
	int r;

	mutex_lock(&output_lock);

	if (!out->device) {
		DSSERR("output doesn't have a device connected to it\n");
		r = -EINVAL;
		goto err;
	}

	if (out->device->state != OMAP_DSS_DISPLAY_DISABLED) {
		DSSERR("device %s is not disabled, cannot unset device\n",
				out->device->name);
		r = -EINVAL;
		goto err;
	}

	out->device->output = NULL;
	out->device = NULL;

	mutex_unlock(&output_lock);

	return 0;
err:
	mutex_unlock(&output_lock);

	return r;
}
EXPORT_SYMBOL(omapdss_output_unset_device);

void dss_register_output(struct omap_dss_output *out)
{
	list_add_tail(&out->list, &output_list);
}

void dss_unregister_output(struct omap_dss_output *out)
{
	list_del(&out->list);
}

struct omap_dss_output *omap_dss_get_output(enum omap_dss_output_id id)
{
	struct omap_dss_output *out;

	list_for_each_entry(out, &output_list, list) {
		if (out->id == id)
			return out;
	}

	return NULL;
}

struct omap_dss_output *omapdss_get_output_from_dssdev(struct omap_dss_device *dssdev)
{
	struct omap_dss_output *out = NULL;
	enum omap_dss_output_id id;

	switch (dssdev->type) {
	case OMAP_DISPLAY_TYPE_DPI:
		out = omap_dss_get_output(OMAP_DSS_OUTPUT_DPI);
		break;
	case OMAP_DISPLAY_TYPE_DBI:
		out = omap_dss_get_output(OMAP_DSS_OUTPUT_DBI);
		break;
	case OMAP_DISPLAY_TYPE_SDI:
		out = omap_dss_get_output(OMAP_DSS_OUTPUT_SDI);
		break;
	case OMAP_DISPLAY_TYPE_VENC:
		out = omap_dss_get_output(OMAP_DSS_OUTPUT_VENC);
		break;
	case OMAP_DISPLAY_TYPE_HDMI:
		out = omap_dss_get_output(OMAP_DSS_OUTPUT_HDMI);
		break;
	case OMAP_DISPLAY_TYPE_DSI:
		id = dssdev->phy.dsi.module == 0 ? OMAP_DSS_OUTPUT_DSI1 :
					OMAP_DSS_OUTPUT_DSI2;
		out = omap_dss_get_output(id);
		break;
	default:
		break;
	}

	return out;
}
