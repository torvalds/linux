// SPDX-License-Identifier: MIT
/* Copyright © 2024 Intel Corporation */

#include <drm/intel/display_member.h>

#include "intel_display_conversion.h"

struct intel_display *__drm_to_display(struct drm_device *drm)
{
	/*
	 * Note: This relies on both struct drm_i915_private and struct
	 * xe_device having the struct drm_device and struct intel_display *
	 * members at the same relative offsets, as defined by struct
	 * __intel_generic_device.
	 *
	 * See also INTEL_DISPLAY_MEMBER_STATIC_ASSERT().
	 */
	struct __intel_generic_device *d = container_of(drm, struct __intel_generic_device, drm);

	return d->display;
}
