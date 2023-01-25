// SPDX-License-Identifier: GPL-2.0

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_modeset_helper_vtables.h>

#include <kunit/test.h>

#include "vc4_mock.h"

static const struct drm_crtc_helper_funcs vc4_dummy_crtc_helper_funcs = {
	.atomic_check	= vc4_crtc_atomic_check,
};

static const struct drm_crtc_funcs vc4_dummy_crtc_funcs = {
	.atomic_destroy_state	= vc4_crtc_destroy_state,
	.atomic_duplicate_state	= vc4_crtc_duplicate_state,
	.reset			= vc4_crtc_reset,
};

struct vc4_dummy_crtc *vc4_mock_pv(struct kunit *test,
				   struct drm_device *drm,
				   struct drm_plane *plane,
				   const struct vc4_crtc_data *data)
{
	struct vc4_dummy_crtc *dummy_crtc;
	struct vc4_crtc *vc4_crtc;
	int ret;

	dummy_crtc = kunit_kzalloc(test, sizeof(*dummy_crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dummy_crtc);

	vc4_crtc = &dummy_crtc->crtc;
	ret = __vc4_crtc_init(drm, NULL,
			      vc4_crtc, data, plane,
			      &vc4_dummy_crtc_funcs,
			      &vc4_dummy_crtc_helper_funcs,
			      false);
	KUNIT_ASSERT_EQ(test, ret, 0);

	return dummy_crtc;
}
