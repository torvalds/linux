// SPDX-License-Identifier: GPL-2.0-only

#include <linux/module.h>

#include <drm/drm_probe_helper.h>

#include "drm_sysfb_helper.h"

MODULE_DESCRIPTION("Helpers for DRM sysfb drivers");
MODULE_LICENSE("GPL");

struct drm_display_mode drm_sysfb_mode(unsigned int width,
				       unsigned int height,
				       unsigned int width_mm,
				       unsigned int height_mm)
{
	/*
	 * Assume a monitor resolution of 96 dpi to
	 * get a somewhat reasonable screen size.
	 */
	if (!width_mm)
		width_mm = DRM_MODE_RES_MM(width, 96ul);
	if (!height_mm)
		height_mm = DRM_MODE_RES_MM(height, 96ul);

	{
		const struct drm_display_mode mode = {
			DRM_MODE_INIT(60, width, height, width_mm, height_mm)
		};

		return mode;
	}
}
EXPORT_SYMBOL(drm_sysfb_mode);

/*
 * Connector
 */

int drm_sysfb_connector_helper_get_modes(struct drm_connector *connector)
{
	struct drm_sysfb_device *sysfb = to_drm_sysfb_device(connector->dev);

	return drm_connector_helper_get_modes_fixed(connector, &sysfb->fb_mode);
}
EXPORT_SYMBOL(drm_sysfb_connector_helper_get_modes);
