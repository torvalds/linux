// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <kunit/test.h>
#include <kunit/static_stub.h>
#include <kunit/visibility.h>

#include <drm/drm_drv.h>
#include <drm/drm_kunit_helpers.h>

#include "tests/xe_kunit_helpers.h"
#include "tests/xe_pci_test.h"
#include "xe_device_types.h"

/**
 * xe_kunit_helper_alloc_xe_device - Allocate a &xe_device for a KUnit test.
 * @test: the &kunit where this &xe_device will be used
 * @dev: The parent device object
 *
 * This function allocates xe_device using drm_kunit_helper_alloc_device().
 * The xe_device allocation is managed by the test.
 *
 * @dev should be allocated using drm_kunit_helper_alloc_device().
 *
 * This function uses KUNIT_ASSERT to detect any allocation failures.
 *
 * Return: A pointer to the new &xe_device.
 */
struct xe_device *xe_kunit_helper_alloc_xe_device(struct kunit *test,
						  struct device *dev)
{
	struct xe_device *xe;

	xe = drm_kunit_helper_alloc_drm_device(test, dev,
					       struct xe_device,
					       drm, DRIVER_GEM);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xe);
	return xe;
}
EXPORT_SYMBOL_IF_KUNIT(xe_kunit_helper_alloc_xe_device);

static void kunit_action_restore_priv(void *priv)
{
	struct kunit *test = kunit_get_current_test();

	test->priv = priv;
}

/**
 * xe_kunit_helper_xe_device_test_init - Prepare a &xe_device for a KUnit test.
 * @test: the &kunit where this fake &xe_device will be used
 *
 * This function allocates and initializes a fake &xe_device and stores its
 * pointer as &kunit.priv to allow the test code to access it.
 *
 * This function can be directly used as custom implementation of
 * &kunit_suite.init.
 *
 * It is possible to prepare specific variant of the fake &xe_device by passing
 * in &kunit.priv pointer to the struct xe_pci_fake_data supplemented with
 * desired parameters prior to calling this function.
 *
 * This function uses KUNIT_ASSERT to detect any failures.
 *
 * Return: Always 0.
 */
int xe_kunit_helper_xe_device_test_init(struct kunit *test)
{
	struct xe_device *xe;
	struct device *dev;
	int err;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	xe = xe_kunit_helper_alloc_xe_device(test, dev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xe);

	err = xe_pci_fake_device_init(xe);
	KUNIT_ASSERT_EQ(test, err, 0);

	err = kunit_add_action_or_reset(test, kunit_action_restore_priv, test->priv);
	KUNIT_ASSERT_EQ(test, err, 0);

	test->priv = xe;
	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(xe_kunit_helper_xe_device_test_init);
