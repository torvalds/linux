/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef DRM_SYSFB_HELPER_H
#define DRM_SYSFB_HELPER_H

#include <linux/container_of.h>
#include <linux/iosys-map.h>

#include <drm/drm_device.h>
#include <drm/drm_modes.h>

struct drm_format_info;

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

#endif
