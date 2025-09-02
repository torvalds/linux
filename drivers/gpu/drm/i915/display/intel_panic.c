// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include "gem/i915_gem_object.h"
#include "intel_panic.h"

struct intel_panic *intel_panic_alloc(void)
{
	return i915_gem_object_alloc_panic();
}

int intel_panic_setup(struct drm_scanout_buffer *sb)
{
	return i915_gem_object_panic_setup(sb);
}

void intel_panic_finish(struct intel_framebuffer *fb)
{
	return i915_gem_object_panic_finish(fb);
}
