// SPDX-License-Identifier: GPL-2.0

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_plane.h>

#include <kunit/test.h>

#include "vc4_mock.h"

static const struct drm_plane_helper_funcs vc4_dummy_plane_helper_funcs = {
};

static const struct drm_plane_funcs vc4_dummy_plane_funcs = {
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.reset			= drm_atomic_helper_plane_reset,
};

static const uint32_t vc4_dummy_plane_formats[] = {
	DRM_FORMAT_XRGB8888,
};

struct vc4_dummy_plane *vc4_dummy_plane(struct kunit *test,
					struct drm_device *drm,
					enum drm_plane_type type)
{
	struct vc4_dummy_plane *dummy_plane;
	struct drm_plane *plane;

	dummy_plane = drmm_universal_plane_alloc(drm,
						 struct vc4_dummy_plane, plane.base,
						 0,
						 &vc4_dummy_plane_funcs,
						 vc4_dummy_plane_formats,
						 ARRAY_SIZE(vc4_dummy_plane_formats),
						 NULL,
						 DRM_PLANE_TYPE_PRIMARY,
						 NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dummy_plane);

	plane = &dummy_plane->plane.base;
	drm_plane_helper_add(plane, &vc4_dummy_plane_helper_funcs);

	return dummy_plane;
}
