/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_OVERLAY_H__
#define __INTEL_OVERLAY_H__

#include <linux/types.h>

struct drm_device;
struct drm_file;
struct drm_printer;
struct intel_display;
struct intel_overlay;

#ifdef I915
void intel_overlay_setup(struct intel_display *display);
bool intel_overlay_available(struct intel_display *display);
void intel_overlay_cleanup(struct intel_display *display);
int intel_overlay_switch_off(struct intel_overlay *overlay);
int intel_overlay_put_image_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv);
int intel_overlay_attrs_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
void intel_overlay_reset(struct intel_display *display);
#else
static inline void intel_overlay_setup(struct intel_display *display)
{
}
static inline bool intel_overlay_available(struct intel_display *display)
{
	return false;
}
static inline void intel_overlay_cleanup(struct intel_display *display)
{
}
static inline int intel_overlay_switch_off(struct intel_overlay *overlay)
{
	return 0;
}
static inline int intel_overlay_put_image_ioctl(struct drm_device *dev, void *data,
						struct drm_file *file_priv)
{
	return 0;
}
static inline int intel_overlay_attrs_ioctl(struct drm_device *dev, void *data,
					    struct drm_file *file_priv)
{
	return 0;
}
static inline void intel_overlay_reset(struct intel_display *display)
{
}
#endif

#endif /* __INTEL_OVERLAY_H__ */
