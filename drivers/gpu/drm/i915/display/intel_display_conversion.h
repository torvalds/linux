/* SPDX-License-Identifier: MIT */
/* Copyright © 2024 Intel Corporation */

/*
 * This header is for transitional struct intel_display conversion helpers only.
 */

#ifndef __INTEL_DISPLAY_CONVERSION__
#define __INTEL_DISPLAY_CONVERSION__

struct drm_device;
struct drm_i915_private;
struct intel_display;

struct intel_display *__i915_to_display(struct drm_i915_private *i915);
struct intel_display *__drm_to_display(struct drm_device *drm);
/*
 * Transitional macro to optionally convert struct drm_i915_private * to struct
 * intel_display *, also accepting the latter.
 */
#define __to_intel_display(p)						\
	_Generic(p,							\
		 const struct drm_i915_private *: __i915_to_display((struct drm_i915_private *)(p)), \
		 struct drm_i915_private *: __i915_to_display((struct drm_i915_private *)(p)), \
		 const struct intel_display *: (p),			\
		 struct intel_display *: (p))

#endif /* __INTEL_DISPLAY_CONVERSION__ */
