// SPDX-License-Identifier: MIT
/* Copyright © 2026 Intel Corporation */

#include <drm/drm_gem.h>
#include <drm/intel/display_parent_interface.h>

#include "intel_frontbuffer.h"
#include "xe_frontbuffer.h"

struct xe_frontbuffer {
	struct intel_frontbuffer base;
	struct drm_gem_object *obj;
	struct kref ref;
};

static struct intel_frontbuffer *xe_frontbuffer_get(struct drm_gem_object *obj)
{
	struct xe_frontbuffer *front;

	front = kmalloc_obj(*front);
	if (!front)
		return NULL;

	intel_frontbuffer_init(&front->base, obj->dev);

	kref_init(&front->ref);

	drm_gem_object_get(obj);
	front->obj = obj;

	return &front->base;
}

static void xe_frontbuffer_ref(struct intel_frontbuffer *_front)
{
	struct xe_frontbuffer *front =
		container_of(_front, typeof(*front), base);

	kref_get(&front->ref);
}

static void frontbuffer_release(struct kref *ref)
{
	struct xe_frontbuffer *front =
		container_of(ref, typeof(*front), ref);

	intel_frontbuffer_fini(&front->base);

	drm_gem_object_put(front->obj);

	kfree(front);
}

static void xe_frontbuffer_put(struct intel_frontbuffer *_front)
{
	struct xe_frontbuffer *front =
		container_of(_front, typeof(*front), base);

	kref_put(&front->ref, frontbuffer_release);
}

static void xe_frontbuffer_flush_for_display(struct intel_frontbuffer *front)
{
}

const struct intel_display_frontbuffer_interface xe_display_frontbuffer_interface = {
	.get = xe_frontbuffer_get,
	.ref = xe_frontbuffer_ref,
	.put = xe_frontbuffer_put,
	.flush_for_display = xe_frontbuffer_flush_for_display,
};
