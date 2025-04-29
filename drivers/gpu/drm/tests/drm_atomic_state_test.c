// SPDX-License-Identifier: GPL-2.0
/*
 * Test cases for the drm_atomic_state helpers
 *
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_kunit_helpers.h>
#include <drm/drm_probe_helper.h>

#define DRM_TEST_ENC_0 BIT(0)
#define DRM_TEST_ENC_1 BIT(1)
#define DRM_TEST_ENC_2 BIT(2)

#define DRM_TEST_CONN_0 BIT(0)

struct drm_clone_mode_test {
	const char *name;
	u32 encoder_mask;
	int expected_result;
};

static const struct drm_display_mode drm_atomic_test_mode = {
	DRM_MODE("1024x768", 0, 65000, 1024, 1048,
		 1184, 1344, 0, 768, 771, 777, 806, 0,
		 DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC)
};

struct drm_atomic_test_priv {
	struct drm_device drm;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	struct drm_encoder encoders[3];
	struct drm_connector connectors[2];
};

static int modeset_counter;

static void drm_test_encoder_mode_set(struct drm_encoder *encoder,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state)
{
	modeset_counter++;
}

static const struct drm_encoder_helper_funcs drm_atomic_test_encoder_funcs = {
	.atomic_mode_set	= drm_test_encoder_mode_set,
};

static const struct drm_connector_funcs dummy_connector_funcs = {
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.reset			= drm_atomic_helper_connector_reset,
};

static int drm_atomic_test_dummy_get_modes(struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector,
						    &drm_atomic_test_mode);
}

static const struct drm_connector_helper_funcs dummy_connector_helper_funcs = {
	.get_modes	= drm_atomic_test_dummy_get_modes,
};

static struct drm_atomic_test_priv *
drm_atomic_test_init_drm_components(struct kunit *test, bool has_connectors)
{
	struct drm_atomic_test_priv *priv;
	struct drm_encoder *enc;
	struct drm_connector *conn;
	struct drm_device *drm;
	struct device *dev;
	int ret;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	priv = drm_kunit_helper_alloc_drm_device(test, dev,
						 struct drm_atomic_test_priv,
						 drm,
						 DRIVER_MODESET | DRIVER_ATOMIC);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);
	test->priv = priv;

	drm = &priv->drm;
	priv->plane = drm_kunit_helper_create_primary_plane(test, drm,
							    NULL,
							    NULL,
							    NULL, 0,
							    NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv->plane);

	priv->crtc = drm_kunit_helper_create_crtc(test, drm,
						  priv->plane, NULL,
						  NULL,
						  NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv->crtc);

	for (int i = 0; i < ARRAY_SIZE(priv->encoders); i++) {
		enc = &priv->encoders[i];

		ret = drmm_encoder_init(drm, enc, NULL,
					DRM_MODE_ENCODER_DSI, NULL);
		KUNIT_ASSERT_EQ(test, ret, 0);

		enc->possible_crtcs = drm_crtc_mask(priv->crtc);
	}

	priv->encoders[0].possible_clones = DRM_TEST_ENC_0 | DRM_TEST_ENC_1;
	priv->encoders[1].possible_clones = DRM_TEST_ENC_0 | DRM_TEST_ENC_1;
	priv->encoders[2].possible_clones = DRM_TEST_ENC_2;

	if (!has_connectors)
		goto done;

	BUILD_BUG_ON(ARRAY_SIZE(priv->connectors) > ARRAY_SIZE(priv->encoders));

	for (int i = 0; i < ARRAY_SIZE(priv->connectors); i++) {
		conn = &priv->connectors[i];

		ret = drmm_connector_init(drm, conn, &dummy_connector_funcs,
					  DRM_MODE_CONNECTOR_DSI, NULL);
		KUNIT_ASSERT_EQ(test, ret, 0);

		drm_connector_helper_add(conn, &dummy_connector_helper_funcs);
		drm_encoder_helper_add(&priv->encoders[i],
				       &drm_atomic_test_encoder_funcs);

		drm_connector_attach_encoder(conn, &priv->encoders[i]);
	}

done:
	drm_mode_config_reset(drm);

	return priv;
}

static int set_up_atomic_state(struct kunit *test,
			       struct drm_atomic_test_priv *priv,
			       struct drm_connector *connector,
			       struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_device *drm = &priv->drm;
	struct drm_crtc *crtc = priv->crtc;
	struct drm_atomic_state *state;
	struct drm_connector_state *conn_state;
	struct drm_crtc_state *crtc_state;
	int ret;

	state = drm_kunit_helper_atomic_state_alloc(test, drm, ctx);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, state);

	if (connector) {
		conn_state = drm_atomic_get_connector_state(state, connector);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, conn_state);

		ret = drm_atomic_set_crtc_for_connector(conn_state, crtc);
		KUNIT_EXPECT_EQ(test, ret, 0);
	}

	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_state);

	ret = drm_atomic_set_mode_for_crtc(crtc_state, &drm_atomic_test_mode);
	KUNIT_EXPECT_EQ(test, ret, 0);

	crtc_state->enable = true;
	crtc_state->active = true;

	if (connector) {
		ret = drm_atomic_commit(state);
		KUNIT_ASSERT_EQ(test, ret, 0);
	} else {
		// dummy connector mask
		crtc_state->connector_mask = DRM_TEST_CONN_0;
	}

	return 0;
}

/*
 * Test that the DRM encoder mode_set() is called when the atomic state
 * connectors are changed but the CRTC mode is not.
 */
static void drm_test_check_connector_changed_modeset(struct kunit *test)
{
	struct drm_atomic_test_priv *priv;
	struct drm_modeset_acquire_ctx ctx;
	struct drm_connector *old_conn, *new_conn;
	struct drm_atomic_state *state;
	struct drm_device *drm;
	struct drm_connector_state *new_conn_state, *old_conn_state;
	int ret, initial_modeset_count;

	priv = drm_atomic_test_init_drm_components(test, true);
	KUNIT_ASSERT_NOT_NULL(test, priv);

	drm = &priv->drm;
	old_conn = &priv->connectors[0];
	new_conn = &priv->connectors[1];

	drm_modeset_acquire_init(&ctx, 0);

	// first modeset to enable
	ret = set_up_atomic_state(test, priv, old_conn, &ctx);
	KUNIT_ASSERT_EQ(test, ret, 0);

	state = drm_kunit_helper_atomic_state_alloc(test, drm, &ctx);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, state);

	new_conn_state = drm_atomic_get_connector_state(state, new_conn);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, new_conn_state);

	old_conn_state = drm_atomic_get_connector_state(state, old_conn);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, old_conn_state);

	ret = drm_atomic_set_crtc_for_connector(old_conn_state, NULL);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = drm_atomic_set_crtc_for_connector(new_conn_state, priv->crtc);
	KUNIT_EXPECT_EQ(test, ret, 0);

	initial_modeset_count = modeset_counter;

	// modeset_disables is called as part of the atomic commit tail
	ret = drm_atomic_commit(state);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_ASSERT_EQ(test, modeset_counter, initial_modeset_count + 1);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
}

/*
 * Test that the drm_crtc_in_clone_mode() helper can detect if a given CRTC
 * state is in clone mode
 */
static void drm_test_check_in_clone_mode(struct kunit *test)
{
	bool ret;
	const struct drm_clone_mode_test *param = test->param_value;
	struct drm_crtc_state *crtc_state;

	crtc_state = kunit_kzalloc(test, sizeof(*crtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc_state);

	crtc_state->encoder_mask = param->encoder_mask;

	ret = drm_crtc_in_clone_mode(crtc_state);

	KUNIT_ASSERT_EQ(test, ret, param->expected_result);
}

/*
 * Test that the atomic commit path will succeed for valid clones (or non-cloned
 * states) and fail for states where the cloned encoders are not possible_clones
 * of each other.
 */
static void drm_test_check_valid_clones(struct kunit *test)
{
	int ret;
	const struct drm_clone_mode_test *param = test->param_value;
	struct drm_atomic_test_priv *priv;
	struct drm_modeset_acquire_ctx ctx;
	struct drm_device *drm;
	struct drm_atomic_state *state;
	struct drm_crtc_state *crtc_state;

	priv = drm_atomic_test_init_drm_components(test, false);
	KUNIT_ASSERT_NOT_NULL(test, priv);

	drm = &priv->drm;

	drm_modeset_acquire_init(&ctx, 0);

	ret = set_up_atomic_state(test, priv, NULL, &ctx);
	KUNIT_ASSERT_EQ(test, ret, 0);

	state = drm_kunit_helper_atomic_state_alloc(test, drm, &ctx);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, state);

	crtc_state = drm_atomic_get_crtc_state(state, priv->crtc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_state);

	crtc_state->encoder_mask = param->encoder_mask;

	// force modeset
	crtc_state->mode_changed = true;

	ret = drm_atomic_helper_check_modeset(drm, state);
	KUNIT_ASSERT_EQ(test, ret, param->expected_result);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
}

static void drm_check_in_clone_mode_desc(const struct drm_clone_mode_test *t,
				      char *desc)
{
	sprintf(desc, "%s", t->name);
}

static void drm_check_valid_clones_desc(const struct drm_clone_mode_test *t,
				      char *desc)
{
	sprintf(desc, "%s", t->name);
}

static const struct drm_clone_mode_test drm_clone_mode_tests[] = {
	{
		.name = "in_clone_mode",
		.encoder_mask = DRM_TEST_ENC_0 | DRM_TEST_ENC_1,
		.expected_result = true,
	},
	{
		.name = "not_in_clone_mode",
		.encoder_mask = DRM_TEST_ENC_0,
		.expected_result = false,
	},
};

static const struct drm_clone_mode_test drm_valid_clone_mode_tests[] = {
	{
		.name = "not_in_clone_mode",
		.encoder_mask = DRM_TEST_ENC_0,
		.expected_result = 0,
	},

	{
		.name = "valid_clone",
		.encoder_mask = DRM_TEST_ENC_0 | DRM_TEST_ENC_1,
		.expected_result = 0,
	},
	{
		.name = "invalid_clone",
		.encoder_mask = DRM_TEST_ENC_0 | DRM_TEST_ENC_2,
		.expected_result = -EINVAL,
	},
};

KUNIT_ARRAY_PARAM(drm_check_in_clone_mode, drm_clone_mode_tests,
		  drm_check_in_clone_mode_desc);

KUNIT_ARRAY_PARAM(drm_check_valid_clones, drm_valid_clone_mode_tests,
		  drm_check_valid_clones_desc);

static struct kunit_case drm_test_check_modeset_test[] = {
	KUNIT_CASE(drm_test_check_connector_changed_modeset),
	{}
};

static struct kunit_case drm_in_clone_mode_check_test[] = {
	KUNIT_CASE_PARAM(drm_test_check_in_clone_mode,
			 drm_check_in_clone_mode_gen_params),
	KUNIT_CASE_PARAM(drm_test_check_valid_clones,
			 drm_check_valid_clones_gen_params),
	{}
};

static struct kunit_suite drm_test_check_modeset_test_suite = {
	.name = "drm_validate_modeset",
	.test_cases = drm_test_check_modeset_test,
};

static struct kunit_suite drm_in_clone_mode_check_test_suite = {
	.name = "drm_validate_clone_mode",
	.test_cases = drm_in_clone_mode_check_test,
};

kunit_test_suites(&drm_in_clone_mode_check_test_suite,
		  &drm_test_check_modeset_test_suite);

MODULE_AUTHOR("Jessica Zhang <quic_jesszhan@quicinc.com");
MODULE_DESCRIPTION("Test cases for the drm_atomic_helper functions");
MODULE_LICENSE("GPL");
