// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include <drm/drm_panic.h>

#include "gem/i915_gem_object.h"
#include "intel_display_types.h"
#include "intel_fb.h"
#include "intel_panic.h"

struct intel_panic *intel_panic_alloc(void)
{
	return i915_gem_object_alloc_panic();
}

int intel_panic_setup(struct intel_panic *panic, struct drm_scanout_buffer *sb)
{
	struct intel_framebuffer *fb = sb->private;
	struct drm_gem_object *obj = intel_fb_bo(&fb->base);

	return i915_gem_object_panic_setup(panic, sb, obj, fb->panic_tiling);
}

void intel_panic_finish(struct intel_panic *panic)
{
	return i915_gem_object_panic_finish(panic);
}
