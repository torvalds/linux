// SPDX-License-Identifier: GPL-2.0

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_modeset_helper_vtables.h>

#include <kunit/test.h>

#include "vc4_mock.h"

static const struct drm_connector_helper_funcs vc4_dummy_connector_helper_funcs = {
};

static const struct drm_connector_funcs vc4_dummy_connector_funcs = {
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.reset			= drm_atomic_helper_connector_reset,
};

struct vc4_dummy_output *vc4_dummy_output(struct kunit *test,
					  struct drm_device *drm,
					  struct drm_crtc *crtc,
					  enum vc4_encoder_type vc4_encoder_type,
					  unsigned int kms_encoder_type,
					  unsigned int connector_type)
{
	struct vc4_dummy_output *dummy_output;
	struct drm_connector *conn;
	struct drm_encoder *enc;
	int ret;

	dummy_output = kunit_kzalloc(test, sizeof(*dummy_output), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dummy_output);
	dummy_output->encoder.type = vc4_encoder_type;

	enc = &dummy_output->encoder.base;
	ret = drmm_encoder_init(drm, enc,
				NULL,
				kms_encoder_type,
				NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);
	enc->possible_crtcs = drm_crtc_mask(crtc);

	conn = &dummy_output->connector;
	ret = drmm_connector_init(drm, conn,
				  &vc4_dummy_connector_funcs,
				  connector_type,
				  NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	drm_connector_helper_add(conn, &vc4_dummy_connector_helper_funcs);
	drm_connector_attach_encoder(conn, enc);

	return dummy_output;
}

static const struct drm_display_mode default_mode = {
	DRM_SIMPLE_MODE(640, 480, 64, 48)
};

int vc4_mock_atomic_add_output(struct kunit *test, struct drm_device *drm,
			       enum vc4_encoder_type type,
			       struct drm_atomic_state *state)
{
	struct vc4_dummy_output *output;
	struct drm_connector *conn;
	struct drm_connector_state *conn_state;
	struct drm_encoder *encoder;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	int ret;

	encoder = vc4_find_encoder_by_type(drm, type);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, encoder);

	crtc = vc4_find_crtc_for_encoder(test, drm, encoder);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc);

	output = container_of(encoder, struct vc4_dummy_output, encoder.base);
	conn = &output->connector;
	conn_state = drm_atomic_get_connector_state(state, conn);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, conn_state);

	ret = drm_atomic_set_crtc_for_connector(conn_state, crtc);
	KUNIT_EXPECT_EQ(test, ret, 0);

	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_state);

	ret = drm_atomic_set_mode_for_crtc(crtc_state, &default_mode);
	KUNIT_EXPECT_EQ(test, ret, 0);

	crtc_state->active = true;

	return 0;
}
