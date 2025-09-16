/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_FBDEV_H__
#define __INTEL_FBDEV_H__

#include <linux/types.h>

struct drm_fb_helper;
struct drm_fb_helper_surface_size;
struct intel_display;
struct intel_fbdev;
struct intel_framebuffer;
struct iosys_map;

#ifdef CONFIG_DRM_FBDEV_EMULATION
int intel_fbdev_driver_fbdev_probe(struct drm_fb_helper *helper,
				   struct drm_fb_helper_surface_size *sizes);
#define INTEL_FBDEV_DRIVER_OPS \
	.fbdev_probe = intel_fbdev_driver_fbdev_probe
void intel_fbdev_setup(struct intel_display *display);
struct intel_framebuffer *intel_fbdev_framebuffer(struct intel_fbdev *fbdev);
struct i915_vma *intel_fbdev_vma_pointer(struct intel_fbdev *fbdev);
void intel_fbdev_get_map(struct intel_fbdev *fbdev, struct iosys_map *map);
#else
#define INTEL_FBDEV_DRIVER_OPS \
	.fbdev_probe = NULL
static inline void intel_fbdev_setup(struct intel_display *display)
{
}
static inline struct intel_framebuffer *intel_fbdev_framebuffer(struct intel_fbdev *fbdev)
{
	return NULL;
}

static inline struct i915_vma *intel_fbdev_vma_pointer(struct intel_fbdev *fbdev)
{
	return NULL;
}

static inline void intel_fbdev_get_map(struct intel_fbdev *fbdev, struct iosys_map *map)
{
}
#endif

#endif /* __INTEL_FBDEV_H__ */
