/*
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
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

#define DSS_SUBSYS_NAME "DISPLAY"

#include <linux/kernel.h>
#include <linux/of.h>

#include <drm/drm_connector.h>
#include <drm/drm_modes.h>

#include "omapdss.h"

static int disp_num_counter;

void omapdss_display_init(struct omap_dss_device *dssdev)
{
	int id;

	/*
	 * Note: this presumes that all displays either have an DT alias, or
	 * none has.
	 */
	id = of_alias_get_id(dssdev->dev->of_node, "display");
	if (id < 0)
		id = disp_num_counter++;

	/* Use 'label' property for name, if it exists */
	of_property_read_string(dssdev->dev->of_node, "label", &dssdev->name);

	if (dssdev->name == NULL)
		dssdev->name = devm_kasprintf(dssdev->dev, GFP_KERNEL,
					      "display%u", id);
}
EXPORT_SYMBOL_GPL(omapdss_display_init);

struct omap_dss_device *omapdss_display_get(struct omap_dss_device *output)
{
	while (output->next)
		output = output->next;

	return omapdss_device_get(output);
}
EXPORT_SYMBOL_GPL(omapdss_display_get);

int omapdss_display_get_modes(struct drm_connector *connector,
			      const struct videomode *vm)
{
	struct drm_display_mode *mode;

	mode = drm_mode_create(connector->dev);
	if (!mode)
		return 0;

	drm_display_mode_from_videomode(vm, mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	return 1;
}
EXPORT_SYMBOL_GPL(omapdss_display_get_modes);
