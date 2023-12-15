// SPDX-License-Identifier: GPL-2.0

#include <drm/drm_atomic.h>
#include <drm/drm_drv.h>
#include <drm/drm_kunit_helpers.h>
#include <drm/drm_managed.h>

#include <kunit/device.h>
#include <kunit/resource.h>

#include <linux/device.h>
#include <linux/platform_device.h>

#define KUNIT_DEVICE_NAME	"drm-kunit-mock-device"

static const struct drm_mode_config_funcs drm_mode_config_funcs = {
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

MODULE_AUTHOR("Maxime Ripard <maxime@cerno.tech>");
MODULE_LICENSE("GPL");
