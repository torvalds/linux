// SPDX-License-Identifier: GPL-2.0-only

#include <linux/export.h>
#include <linux/slab.h>
#include <linux/module.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_print.h>
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
 * CRTC
 */

static void drm_sysfb_crtc_state_destroy(struct drm_sysfb_crtc_state *sysfb_crtc_state)
{
	__drm_atomic_helper_crtc_destroy_state(&sysfb_crtc_state->base);

	kfree(sysfb_crtc_state);
}

enum drm_mode_status drm_sysfb_crtc_helper_mode_valid(struct drm_crtc *crtc,
						      const struct drm_display_mode *mode)
{
	struct drm_sysfb_device *sysfb = to_drm_sysfb_device(crtc->dev);

	return drm_crtc_helper_mode_valid_fixed(crtc, mode, &sysfb->fb_mode);
}
EXPORT_SYMBOL(drm_sysfb_crtc_helper_mode_valid);

int drm_sysfb_crtc_helper_atomic_check(struct drm_crtc *crtc, struct drm_atomic_state *new_state)
{
	struct drm_device *dev = crtc->dev;
	struct drm_sysfb_device *sysfb = to_drm_sysfb_device(dev);
	struct drm_crtc_state *new_crtc_state = drm_atomic_get_new_crtc_state(new_state, crtc);
	int ret;

	if (!new_crtc_state->enable)
		return 0;

	ret = drm_atomic_helper_check_crtc_primary_plane(new_crtc_state);
	if (ret)
		return ret;

	if (new_crtc_state->color_mgmt_changed) {
		const size_t gamma_lut_length =
			sysfb->fb_gamma_lut_size * sizeof(struct drm_color_lut);
		const struct drm_property_blob *gamma_lut = new_crtc_state->gamma_lut;

		if (gamma_lut && (gamma_lut->length != gamma_lut_length)) {
			drm_dbg(dev, "Incorrect gamma_lut length %zu\n", gamma_lut->length);
			return -EINVAL;
		}
	}

	return 0;
}
EXPORT_SYMBOL(drm_sysfb_crtc_helper_atomic_check);

void drm_sysfb_crtc_reset(struct drm_crtc *crtc)
{
	struct drm_sysfb_crtc_state *sysfb_crtc_state;

	if (crtc->state)
		drm_sysfb_crtc_state_destroy(to_drm_sysfb_crtc_state(crtc->state));

	sysfb_crtc_state = kzalloc(sizeof(*sysfb_crtc_state), GFP_KERNEL);
	if (sysfb_crtc_state)
		__drm_atomic_helper_crtc_reset(crtc, &sysfb_crtc_state->base);
	else
		__drm_atomic_helper_crtc_reset(crtc, NULL);
}
EXPORT_SYMBOL(drm_sysfb_crtc_reset);

struct drm_crtc_state *drm_sysfb_crtc_atomic_duplicate_state(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_crtc_state *crtc_state = crtc->state;
	struct drm_sysfb_crtc_state *new_sysfb_crtc_state;
	struct drm_sysfb_crtc_state *sysfb_crtc_state;

	if (drm_WARN_ON(dev, !crtc_state))
		return NULL;

	new_sysfb_crtc_state = kzalloc(sizeof(*new_sysfb_crtc_state), GFP_KERNEL);
	if (!new_sysfb_crtc_state)
		return NULL;

	sysfb_crtc_state = to_drm_sysfb_crtc_state(crtc_state);

	__drm_atomic_helper_crtc_duplicate_state(crtc, &new_sysfb_crtc_state->base);
	new_sysfb_crtc_state->format = sysfb_crtc_state->format;

	return &new_sysfb_crtc_state->base;
}
EXPORT_SYMBOL(drm_sysfb_crtc_atomic_duplicate_state);

void drm_sysfb_crtc_atomic_destroy_state(struct drm_crtc *crtc, struct drm_crtc_state *crtc_state)
{
	drm_sysfb_crtc_state_destroy(to_drm_sysfb_crtc_state(crtc_state));
}
EXPORT_SYMBOL(drm_sysfb_crtc_atomic_destroy_state);

/*
 * Connector
 */

int drm_sysfb_connector_helper_get_modes(struct drm_connector *connector)
{
	struct drm_sysfb_device *sysfb = to_drm_sysfb_device(connector->dev);

	return drm_connector_helper_get_modes_fixed(connector, &sysfb->fb_mode);
}
EXPORT_SYMBOL(drm_sysfb_connector_helper_get_modes);
