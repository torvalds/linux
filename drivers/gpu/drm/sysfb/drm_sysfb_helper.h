/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef DRM_SYSFB_HELPER_H
#define DRM_SYSFB_HELPER_H

#include <linux/container_of.h>
#include <linux/iosys-map.h>

#include <drm/drm_device.h>
#include <drm/drm_modes.h>

struct drm_format_info;

struct drm_display_mode drm_sysfb_mode(unsigned int width,
				       unsigned int height,
				       unsigned int width_mm,
				       unsigned int height_mm);

/*
 * Device
 */

struct drm_sysfb_device {
	struct drm_device dev;

	/* hardware settings */
	struct drm_display_mode fb_mode;
	const struct drm_format_info *fb_format;
	unsigned int fb_pitch;

	/* hardware-framebuffer kernel address */
	struct iosys_map fb_addr;
};

static inline struct drm_sysfb_device *to_drm_sysfb_device(struct drm_device *dev)
{
	return container_of(dev, struct drm_sysfb_device, dev);
}

/*
 * Connector
 */

int drm_sysfb_connector_helper_get_modes(struct drm_connector *connector);

#define DRM_SYSFB_CONNECTOR_HELPER_FUNCS \
	.get_modes = drm_sysfb_connector_helper_get_modes

#define DRM_SYSFB_CONNECTOR_FUNCS \
	.reset = drm_atomic_helper_connector_reset, \
	.fill_modes = drm_helper_probe_single_connector_modes, \
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state, \
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state

/*
 * Mode config
 */

#define DRM_SYSFB_MODE_CONFIG_FUNCS \
	.fb_create = drm_gem_fb_create_with_dirty, \
	.atomic_check = drm_atomic_helper_check, \
	.atomic_commit = drm_atomic_helper_commit

#endif
