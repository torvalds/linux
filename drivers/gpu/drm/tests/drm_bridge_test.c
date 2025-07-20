// SPDX-License-Identifier: GPL-2.0
/*
 * Kunit test for drm_bridge functions
 */
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_bridge_helper.h>
#include <drm/drm_kunit_helpers.h>

#include <kunit/test.h>

struct drm_bridge_init_priv {
	struct drm_device drm;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	struct drm_encoder encoder;
	struct drm_bridge bridge;
	struct drm_connector *connector;
	unsigned int enable_count;
	unsigned int disable_count;
};

static void drm_test_bridge_enable(struct drm_bridge *bridge)
{
	struct drm_bridge_init_priv *priv =
		container_of(bridge, struct drm_bridge_init_priv, bridge);

	priv->enable_count++;
}

static void drm_test_bridge_disable(struct drm_bridge *bridge)
{
	struct drm_bridge_init_priv *priv =
		container_of(bridge, struct drm_bridge_init_priv, bridge);

	priv->disable_count++;
}

static const struct drm_bridge_funcs drm_test_bridge_legacy_funcs = {
	.enable			= drm_test_bridge_enable,
	.disable		= drm_test_bridge_disable,
};

static void drm_test_bridge_atomic_enable(struct drm_bridge *bridge,
					  struct drm_atomic_state *state)
{
	struct drm_bridge_init_priv *priv =
		container_of(bridge, struct drm_bridge_init_priv, bridge);

	priv->enable_count++;
}

static void drm_test_bridge_atomic_disable(struct drm_bridge *bridge,
					   struct drm_atomic_state *state)
{
	struct drm_bridge_init_priv *priv =
		container_of(bridge, struct drm_bridge_init_priv, bridge);

	priv->disable_count++;
}

static const struct drm_bridge_funcs drm_test_bridge_atomic_funcs = {
	.atomic_enable		= drm_test_bridge_atomic_enable,
	.atomic_disable		= drm_test_bridge_atomic_disable,
	.atomic_destroy_state	= drm_atomic_helper_bridge_destroy_state,
	.atomic_duplicate_state	= drm_atomic_helper_bridge_duplicate_state,
	.atomic_reset		= drm_atomic_helper_bridge_reset,
};

KUNIT_DEFINE_ACTION_WRAPPER(drm_bridge_remove_wrapper,
			    drm_bridge_remove,
			    struct drm_bridge *);

static int drm_kunit_bridge_add(struct kunit *test,
				struct drm_bridge *bridge)
{
	drm_bridge_add(bridge);

	return kunit_add_action_or_reset(test,
					 drm_bridge_remove_wrapper,
					 bridge);
}

static struct drm_bridge_init_priv *
drm_test_bridge_init(struct kunit *test, const struct drm_bridge_funcs *funcs)
{
	struct drm_bridge_init_priv *priv;
	struct drm_encoder *enc;
	struct drm_bridge *bridge;
	struct drm_device *drm;
	struct device *dev;
	int ret;

	dev = drm_kunit_helper_alloc_device(test);
	if (IS_ERR(dev))
		return ERR_CAST(dev);

	priv = drm_kunit_helper_alloc_drm_device(test, dev,
						 struct drm_bridge_init_priv, drm,
						 DRIVER_MODESET | DRIVER_ATOMIC);
	if (IS_ERR(priv))
		return ERR_CAST(priv);

	drm = &priv->drm;
	priv->plane = drm_kunit_helper_create_primary_plane(test, drm,
							    NULL,
							    NULL,
							    NULL, 0,
							    NULL);
	if (IS_ERR(priv->plane))
		return ERR_CAST(priv->plane);

	priv->crtc = drm_kunit_helper_create_crtc(test, drm,
						  priv->plane, NULL,
						  NULL,
						  NULL);
	if (IS_ERR(priv->crtc))
		return ERR_CAST(priv->crtc);

	enc = &priv->encoder;
	ret = drmm_encoder_init(drm, enc, NULL, DRM_MODE_ENCODER_TMDS, NULL);
	if (ret)
		return ERR_PTR(ret);

	enc->possible_crtcs = drm_crtc_mask(priv->crtc);

	bridge = &priv->bridge;
	bridge->type = DRM_MODE_CONNECTOR_VIRTUAL;
	bridge->funcs = funcs;

	ret = drm_kunit_bridge_add(test, bridge);
	if (ret)
		return ERR_PTR(ret);

	ret = drm_bridge_attach(enc, bridge, NULL, 0);
	if (ret)
		return ERR_PTR(ret);

	priv->connector = drm_bridge_connector_init(drm, enc);
	if (IS_ERR(priv->connector))
		return ERR_CAST(priv->connector);

	drm_connector_attach_encoder(priv->connector, enc);

	drm_mode_config_reset(drm);

	return priv;
}

/*
 * Test that drm_bridge_get_current_state() returns the last committed
 * state for an atomic bridge.
 */
static void drm_test_drm_bridge_get_current_state_atomic(struct kunit *test)
{
	struct drm_modeset_acquire_ctx ctx;
	struct drm_bridge_init_priv *priv;
	struct drm_bridge_state *curr_bridge_state;
	struct drm_bridge_state *bridge_state;
	struct drm_atomic_state *state;
	struct drm_bridge *bridge;
	struct drm_device *drm;
	int ret;

	priv = drm_test_bridge_init(test, &drm_test_bridge_atomic_funcs);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);

	drm_modeset_acquire_init(&ctx, 0);

	drm = &priv->drm;
	state = drm_kunit_helper_atomic_state_alloc(test, drm, &ctx);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, state);

retry_commit:
	bridge = &priv->bridge;
	bridge_state = drm_atomic_get_bridge_state(state, bridge);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bridge_state);

	ret = drm_atomic_commit(state);
	if (ret == -EDEADLK) {
		drm_atomic_state_clear(state);
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	drm_modeset_acquire_init(&ctx, 0);

retry_state:
	ret = drm_modeset_lock(&bridge->base.lock, &ctx);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_state;
	}

	curr_bridge_state = drm_bridge_get_current_state(bridge);
	KUNIT_EXPECT_PTR_EQ(test, curr_bridge_state, bridge_state);

	drm_modeset_unlock(&bridge->base.lock);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
}

/*
 * Test that drm_bridge_get_current_state() returns NULL for a
 * non-atomic bridge.
 */
static void drm_test_drm_bridge_get_current_state_legacy(struct kunit *test)
{
	struct drm_bridge_init_priv *priv;
	struct drm_bridge *bridge;

	priv = drm_test_bridge_init(test, &drm_test_bridge_legacy_funcs);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);

	/*
	 * NOTE: Strictly speaking, we should take the bridge->base.lock
	 * before calling that function. However, bridge->base is only
	 * initialized if the bridge is atomic, while we explicitly
	 * initialize one that isn't there.
	 *
	 * In order to avoid unnecessary warnings, let's skip the
	 * locking. The function would return NULL in all cases anyway,
	 * so we don't really have any concurrency to worry about.
	 */
	bridge = &priv->bridge;
	KUNIT_EXPECT_NULL(test, drm_bridge_get_current_state(bridge));
}

static struct kunit_case drm_bridge_get_current_state_tests[] = {
	KUNIT_CASE(drm_test_drm_bridge_get_current_state_atomic),
	KUNIT_CASE(drm_test_drm_bridge_get_current_state_legacy),
	{ }
};


static struct kunit_suite drm_bridge_get_current_state_test_suite = {
	.name = "drm_test_bridge_get_current_state",
	.test_cases = drm_bridge_get_current_state_tests,
};

/*
 * Test that an atomic bridge is properly power-cycled when calling
 * drm_bridge_helper_reset_crtc().
 */
static void drm_test_drm_bridge_helper_reset_crtc_atomic(struct kunit *test)
{
	struct drm_modeset_acquire_ctx ctx;
	struct drm_bridge_init_priv *priv;
	struct drm_display_mode *mode;
	struct drm_bridge *bridge;
	int ret;

	priv = drm_test_bridge_init(test, &drm_test_bridge_atomic_funcs);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);

	mode = drm_kunit_display_mode_from_cea_vic(test, &priv->drm, 16);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, mode);

	drm_modeset_acquire_init(&ctx, 0);

retry_commit:
	ret = drm_kunit_helper_enable_crtc_connector(test,
						     &priv->drm, priv->crtc,
						     priv->connector,
						     mode,
						     &ctx);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	bridge = &priv->bridge;
	KUNIT_ASSERT_EQ(test, priv->enable_count, 1);
	KUNIT_ASSERT_EQ(test, priv->disable_count, 0);

	drm_modeset_acquire_init(&ctx, 0);

retry_reset:
	ret = drm_bridge_helper_reset_crtc(bridge, &ctx);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_reset;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	KUNIT_EXPECT_EQ(test, priv->enable_count, 2);
	KUNIT_EXPECT_EQ(test, priv->disable_count, 1);
}

/*
 * Test that calling drm_bridge_helper_reset_crtc() on a disabled atomic
 * bridge will fail and not call the enable / disable callbacks
 */
static void drm_test_drm_bridge_helper_reset_crtc_atomic_disabled(struct kunit *test)
{
	struct drm_modeset_acquire_ctx ctx;
	struct drm_bridge_init_priv *priv;
	struct drm_display_mode *mode;
	struct drm_bridge *bridge;
	int ret;

	priv = drm_test_bridge_init(test, &drm_test_bridge_atomic_funcs);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);

	mode = drm_kunit_display_mode_from_cea_vic(test, &priv->drm, 16);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, mode);

	bridge = &priv->bridge;
	KUNIT_ASSERT_EQ(test, priv->enable_count, 0);
	KUNIT_ASSERT_EQ(test, priv->disable_count, 0);

	drm_modeset_acquire_init(&ctx, 0);

retry_reset:
	ret = drm_bridge_helper_reset_crtc(bridge, &ctx);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_reset;
	}
	KUNIT_EXPECT_LT(test, ret, 0);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	KUNIT_EXPECT_EQ(test, priv->enable_count, 0);
	KUNIT_EXPECT_EQ(test, priv->disable_count, 0);
}

/*
 * Test that a non-atomic bridge is properly power-cycled when calling
 * drm_bridge_helper_reset_crtc().
 */
static void drm_test_drm_bridge_helper_reset_crtc_legacy(struct kunit *test)
{
	struct drm_modeset_acquire_ctx ctx;
	struct drm_bridge_init_priv *priv;
	struct drm_display_mode *mode;
	struct drm_bridge *bridge;
	int ret;

	priv = drm_test_bridge_init(test, &drm_test_bridge_legacy_funcs);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);

	mode = drm_kunit_display_mode_from_cea_vic(test, &priv->drm, 16);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, mode);

	drm_modeset_acquire_init(&ctx, 0);

retry_commit:
	ret = drm_kunit_helper_enable_crtc_connector(test,
						     &priv->drm, priv->crtc,
						     priv->connector,
						     mode,
						     &ctx);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	bridge = &priv->bridge;
	KUNIT_ASSERT_EQ(test, priv->enable_count, 1);
	KUNIT_ASSERT_EQ(test, priv->disable_count, 0);

	drm_modeset_acquire_init(&ctx, 0);

retry_reset:
	ret = drm_bridge_helper_reset_crtc(bridge, &ctx);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_reset;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	KUNIT_EXPECT_EQ(test, priv->enable_count, 2);
	KUNIT_EXPECT_EQ(test, priv->disable_count, 1);
}

static struct kunit_case drm_bridge_helper_reset_crtc_tests[] = {
	KUNIT_CASE(drm_test_drm_bridge_helper_reset_crtc_atomic),
	KUNIT_CASE(drm_test_drm_bridge_helper_reset_crtc_atomic_disabled),
	KUNIT_CASE(drm_test_drm_bridge_helper_reset_crtc_legacy),
	{ }
};

static struct kunit_suite drm_bridge_helper_reset_crtc_test_suite = {
	.name = "drm_test_bridge_helper_reset_crtc",
	.test_cases = drm_bridge_helper_reset_crtc_tests,
};

kunit_test_suites(
	&drm_bridge_get_current_state_test_suite,
	&drm_bridge_helper_reset_crtc_test_suite,
);

MODULE_AUTHOR("Maxime Ripard <mripard@kernel.org>");
MODULE_DESCRIPTION("Kunit test for drm_bridge functions");
MODULE_LICENSE("GPL");
