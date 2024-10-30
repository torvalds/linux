// SPDX-License-Identifier: GPL-2.0

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_kunit_helpers.h>
#include <drm/drm_managed.h>

#include <kunit/device.h>
#include <kunit/resource.h>

#include <linux/device.h>
#include <linux/platform_device.h>

#define KUNIT_DEVICE_NAME	"drm-kunit-mock-device"

static const struct drm_mode_config_funcs drm_mode_config_funcs = {
	.atomic_check	= drm_atomic_helper_check,
	.atomic_commit	= drm_atomic_helper_commit,
};

/**
 * drm_kunit_helper_alloc_device - Allocate a mock device for a KUnit test
 * @test: The test context object
 *
 * This allocates a fake struct &device to create a mock for a KUnit
 * test. The device will also be bound to a fake driver. It will thus be
 * able to leverage the usual infrastructure and most notably the
 * device-managed resources just like a "real" device.
 *
 * Resources will be cleaned up automatically, but the removal can be
 * forced using @drm_kunit_helper_free_device.
 *
 * Returns:
 * A pointer to the new device, or an ERR_PTR() otherwise.
 */
struct device *drm_kunit_helper_alloc_device(struct kunit *test)
{
	return kunit_device_register(test, KUNIT_DEVICE_NAME);
}
EXPORT_SYMBOL_GPL(drm_kunit_helper_alloc_device);

/**
 * drm_kunit_helper_free_device - Frees a mock device
 * @test: The test context object
 * @dev: The device to free
 *
 * Frees a device allocated with drm_kunit_helper_alloc_device().
 */
void drm_kunit_helper_free_device(struct kunit *test, struct device *dev)
{
	kunit_device_unregister(test, dev);
}
EXPORT_SYMBOL_GPL(drm_kunit_helper_free_device);

struct drm_device *
__drm_kunit_helper_alloc_drm_device_with_driver(struct kunit *test,
						struct device *dev,
						size_t size, size_t offset,
						const struct drm_driver *driver)
{
	struct drm_device *drm;
	void *container;
	int ret;

	container = __devm_drm_dev_alloc(dev, driver, size, offset);
	if (IS_ERR(container))
		return ERR_CAST(container);

	drm = container + offset;
	drm->mode_config.funcs = &drm_mode_config_funcs;

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ERR_PTR(ret);

	return drm;
}
EXPORT_SYMBOL_GPL(__drm_kunit_helper_alloc_drm_device_with_driver);

static void action_drm_release_context(void *ptr)
{
	struct drm_modeset_acquire_ctx *ctx = ptr;

	drm_modeset_drop_locks(ctx);
	drm_modeset_acquire_fini(ctx);
}

/**
 * drm_kunit_helper_acquire_ctx_alloc - Allocates an acquire context
 * @test: The test context object
 *
 * Allocates and initializes a modeset acquire context.
 *
 * The context is tied to the kunit test context, so we must not call
 * drm_modeset_acquire_fini() on it, it will be done so automatically.
 *
 * Returns:
 * An ERR_PTR on error, a pointer to the newly allocated context otherwise
 */
struct drm_modeset_acquire_ctx *
drm_kunit_helper_acquire_ctx_alloc(struct kunit *test)
{
	struct drm_modeset_acquire_ctx *ctx;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	drm_modeset_acquire_init(ctx, 0);

	ret = kunit_add_action_or_reset(test,
					action_drm_release_context,
					ctx);
	if (ret)
		return ERR_PTR(ret);

	return ctx;
}
EXPORT_SYMBOL_GPL(drm_kunit_helper_acquire_ctx_alloc);

static void kunit_action_drm_atomic_state_put(void *ptr)
{
	struct drm_atomic_state *state = ptr;

	drm_atomic_state_put(state);
}

/**
 * drm_kunit_helper_atomic_state_alloc - Allocates an atomic state
 * @test: The test context object
 * @drm: The device to alloc the state for
 * @ctx: Locking context for that atomic update
 *
 * Allocates a empty atomic state.
 *
 * The state is tied to the kunit test context, so we must not call
 * drm_atomic_state_put() on it, it will be done so automatically.
 *
 * Returns:
 * An ERR_PTR on error, a pointer to the newly allocated state otherwise
 */
struct drm_atomic_state *
drm_kunit_helper_atomic_state_alloc(struct kunit *test,
				    struct drm_device *drm,
				    struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	int ret;

	state = drm_atomic_state_alloc(drm);
	if (!state)
		return ERR_PTR(-ENOMEM);

	ret = kunit_add_action_or_reset(test,
					kunit_action_drm_atomic_state_put,
					state);
	if (ret)
		return ERR_PTR(ret);

	state->acquire_ctx = ctx;

	return state;
}
EXPORT_SYMBOL_GPL(drm_kunit_helper_atomic_state_alloc);

static const uint32_t default_plane_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const uint64_t default_plane_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

static const struct drm_plane_helper_funcs default_plane_helper_funcs = {
};

static const struct drm_plane_funcs default_plane_funcs = {
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.reset			= drm_atomic_helper_plane_reset,
};

/**
 * drm_kunit_helper_create_primary_plane - Creates a mock primary plane for a KUnit test
 * @test: The test context object
 * @drm: The device to alloc the plane for
 * @funcs: Callbacks for the new plane. Optional.
 * @helper_funcs: Helpers callbacks for the new plane. Optional.
 * @formats: array of supported formats (DRM_FORMAT\_\*). Optional.
 * @num_formats: number of elements in @formats
 * @modifiers: array of struct drm_format modifiers terminated by
 *             DRM_FORMAT_MOD_INVALID. Optional.
 *
 * This allocates and initializes a mock struct &drm_plane meant to be
 * part of a mock device for a KUnit test.
 *
 * Resources will be cleaned up automatically.
 *
 * @funcs will default to the default helpers implementations.
 * @helper_funcs will default to an empty implementation. @formats will
 * default to XRGB8888 only. @modifiers will default to a linear
 * modifier only.
 *
 * Returns:
 * A pointer to the new plane, or an ERR_PTR() otherwise.
 */
struct drm_plane *
drm_kunit_helper_create_primary_plane(struct kunit *test,
				      struct drm_device *drm,
				      const struct drm_plane_funcs *funcs,
				      const struct drm_plane_helper_funcs *helper_funcs,
				      const uint32_t *formats,
				      unsigned int num_formats,
				      const uint64_t *modifiers)
{
	struct drm_plane *plane;

	if (!funcs)
		funcs = &default_plane_funcs;

	if (!helper_funcs)
		helper_funcs = &default_plane_helper_funcs;

	if (!formats || !num_formats) {
		formats = default_plane_formats;
		num_formats = ARRAY_SIZE(default_plane_formats);
	}

	if (!modifiers)
		modifiers = default_plane_modifiers;

	plane = __drmm_universal_plane_alloc(drm,
					     sizeof(struct drm_plane), 0,
					     0,
					     funcs,
					     formats,
					     num_formats,
					     default_plane_modifiers,
					     DRM_PLANE_TYPE_PRIMARY,
					     NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane);

	drm_plane_helper_add(plane, helper_funcs);

	return plane;
}
EXPORT_SYMBOL_GPL(drm_kunit_helper_create_primary_plane);

static const struct drm_crtc_helper_funcs default_crtc_helper_funcs = {
};

static const struct drm_crtc_funcs default_crtc_funcs = {
	.atomic_destroy_state   = drm_atomic_helper_crtc_destroy_state,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.reset                  = drm_atomic_helper_crtc_reset,
};

/**
 * drm_kunit_helper_create_crtc - Creates a mock CRTC for a KUnit test
 * @test: The test context object
 * @drm: The device to alloc the plane for
 * @primary: Primary plane for CRTC
 * @cursor: Cursor plane for CRTC. Optional.
 * @funcs: Callbacks for the new plane. Optional.
 * @helper_funcs: Helpers callbacks for the new plane. Optional.
 *
 * This allocates and initializes a mock struct &drm_crtc meant to be
 * part of a mock device for a KUnit test.
 *
 * Resources will be cleaned up automatically.
 *
 * @funcs will default to the default helpers implementations.
 * @helper_funcs will default to an empty implementation.
 *
 * Returns:
 * A pointer to the new CRTC, or an ERR_PTR() otherwise.
 */
struct drm_crtc *
drm_kunit_helper_create_crtc(struct kunit *test,
			     struct drm_device *drm,
			     struct drm_plane *primary,
			     struct drm_plane *cursor,
			     const struct drm_crtc_funcs *funcs,
			     const struct drm_crtc_helper_funcs *helper_funcs)
{
	struct drm_crtc *crtc;
	int ret;

	if (!funcs)
		funcs = &default_crtc_funcs;

	if (!helper_funcs)
		helper_funcs = &default_crtc_helper_funcs;

	crtc = drmm_kzalloc(drm, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc);

	ret = drmm_crtc_init_with_planes(drm, crtc,
					 primary,
					 cursor,
					 funcs,
					 NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	drm_crtc_helper_add(crtc, helper_funcs);

	return crtc;
}
EXPORT_SYMBOL_GPL(drm_kunit_helper_create_crtc);

static void kunit_action_drm_mode_destroy(void *ptr)
{
	struct drm_display_mode *mode = ptr;

	drm_mode_destroy(NULL, mode);
}

/**
 * drm_kunit_display_mode_from_cea_vic() - return a mode for CEA VIC
					   for a KUnit test
 * @test: The test context object
 * @dev: DRM device
 * @video_code: CEA VIC of the mode
 *
 * Creates a new mode matching the specified CEA VIC for a KUnit test.
 *
 * Resources will be cleaned up automatically.
 *
 * Returns: A new drm_display_mode on success or NULL on failure
 */
struct drm_display_mode *
drm_kunit_display_mode_from_cea_vic(struct kunit *test, struct drm_device *dev,
				    u8 video_code)
{
	struct drm_display_mode *mode;
	int ret;

	mode = drm_display_mode_from_cea_vic(dev, video_code);
	if (!mode)
		return NULL;

	ret = kunit_add_action_or_reset(test,
					kunit_action_drm_mode_destroy,
					mode);
	if (ret)
		return NULL;

	return mode;
}
EXPORT_SYMBOL_GPL(drm_kunit_display_mode_from_cea_vic);

MODULE_AUTHOR("Maxime Ripard <maxime@cerno.tech>");
MODULE_DESCRIPTION("KUnit test suite helper functions");
MODULE_LICENSE("GPL");
