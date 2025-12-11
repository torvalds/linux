// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include <drm/drm_panic.h>
#include <drm/intel/display_parent_interface.h>

#include "display/intel_display_types.h"
#include "display/intel_fb.h"
#include "gem/i915_gem_object.h"

#include "i915_panic.h"

static struct intel_panic *intel_panic_alloc(void)
{
	return i915_gem_object_alloc_panic();
}

static int intel_panic_setup(struct intel_panic *panic, struct drm_scanout_buffer *sb)
{
	struct intel_framebuffer *fb = sb->private;
	struct drm_gem_object *obj = intel_fb_bo(&fb->base);

	return i915_gem_object_panic_setup(panic, sb, obj, fb->panic_tiling);
}

static void intel_panic_finish(struct intel_panic *panic)
{
	return i915_gem_object_panic_finish(panic);
}

const struct intel_display_panic_interface i915_display_panic_interface = {
	.alloc = intel_panic_alloc,
	.setup = intel_panic_setup,
	.finish = intel_panic_finish,
};
