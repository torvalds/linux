// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2026 Intel Corporation
 */

#include <kunit/test.h>

#include "tests/xe_kunit_helpers.h"
#include "tests/xe_pci_test.h"
#include "xe_any.h"
#include "xe_device.h"

static void test_to_xe(struct kunit *test)
{
	struct xe_device *xe = test->priv;
	struct xe_tile *tile = xe_device_get_root_tile(xe);
	struct xe_gt *gt = tile->primary_gt;
	struct drm_device *drm = &xe->drm;
	struct device *dev = xe->drm.dev;
	struct pci_dev *pdev = to_pci_dev(dev);
	const struct xe_device *cxe = xe;
	const struct xe_tile *ctile = tile;
	const struct xe_gt *cgt = gt;

	KUNIT_EXPECT_PTR_EQ(test, xe, xe_any_to_xe(xe));
	KUNIT_EXPECT_PTR_EQ(test, xe, xe_any_to_xe(tile));
	KUNIT_EXPECT_PTR_EQ(test, xe, xe_any_to_xe(gt));
	KUNIT_EXPECT_PTR_EQ(test, xe, xe_any_to_xe(drm));
	KUNIT_EXPECT_PTR_EQ(test, xe, xe_any_to_xe(dev));
	KUNIT_EXPECT_PTR_EQ(test, xe, xe_any_to_xe(pdev));

	KUNIT_EXPECT_PTR_EQ(test, cxe, xe_any_to_xe(cxe));
	KUNIT_EXPECT_PTR_EQ(test, cxe, xe_any_to_xe(ctile));
	KUNIT_EXPECT_PTR_EQ(test, cxe, xe_any_to_xe(cgt));
}

static void test_to_pdev(struct kunit *test)
{
	struct xe_device *xe = test->priv;
	struct xe_tile *tile = xe_device_get_root_tile(xe);
	struct xe_gt *gt = tile->primary_gt;
	struct drm_device *drm = &xe->drm;
	struct device *dev = xe->drm.dev;
	struct pci_dev *pdev = to_pci_dev(dev);

	KUNIT_EXPECT_PTR_EQ(test, pdev, xe_any_to_pdev(xe));
	KUNIT_EXPECT_PTR_EQ(test, pdev, xe_any_to_pdev(tile));
	KUNIT_EXPECT_PTR_EQ(test, pdev, xe_any_to_pdev(gt));
	KUNIT_EXPECT_PTR_EQ(test, pdev, xe_any_to_pdev(drm));
	KUNIT_EXPECT_PTR_EQ(test, pdev, xe_any_to_pdev(dev));
	KUNIT_EXPECT_PTR_EQ(test, pdev, xe_any_to_pdev(pdev));

	/* mimic early probe stage */
	dev_set_drvdata(xe->drm.dev, NULL);
	KUNIT_EXPECT_PTR_EQ(test, pdev, xe_any_to_pdev(dev));
	KUNIT_EXPECT_PTR_EQ(test, pdev, xe_any_to_pdev(pdev));
}

static void test_to_dev(struct kunit *test)
{
	struct xe_device *xe = test->priv;
	struct xe_tile *tile = xe_device_get_root_tile(xe);
	struct xe_gt *gt = tile->primary_gt;
	struct drm_device *drm = &xe->drm;
	struct device *dev = xe->drm.dev;
	struct pci_dev *pdev = to_pci_dev(dev);

	KUNIT_EXPECT_PTR_EQ(test, dev, xe_any_to_dev(xe));
	KUNIT_EXPECT_PTR_EQ(test, dev, xe_any_to_dev(tile));
	KUNIT_EXPECT_PTR_EQ(test, dev, xe_any_to_dev(gt));
	KUNIT_EXPECT_PTR_EQ(test, dev, xe_any_to_dev(drm));
	KUNIT_EXPECT_PTR_EQ(test, dev, xe_any_to_dev(dev));
	KUNIT_EXPECT_PTR_EQ(test, dev, xe_any_to_dev(pdev));

	/* mimic early probe stage */
	dev_set_drvdata(xe->drm.dev, NULL);
	KUNIT_EXPECT_PTR_EQ(test, dev, xe_any_to_dev(dev));
	KUNIT_EXPECT_PTR_EQ(test, dev, xe_any_to_dev(pdev));
}

static void test_to_drm(struct kunit *test)
{
	struct xe_device *xe = test->priv;
	struct xe_tile *tile = xe_device_get_root_tile(xe);
	struct xe_gt *gt = tile->primary_gt;
	struct drm_device *drm = &xe->drm;
	struct device *dev = xe->drm.dev;
	struct pci_dev *pdev = to_pci_dev(dev);

	KUNIT_EXPECT_PTR_EQ(test, drm, xe_any_to_drm(xe));
	KUNIT_EXPECT_PTR_EQ(test, drm, xe_any_to_drm(tile));
	KUNIT_EXPECT_PTR_EQ(test, drm, xe_any_to_drm(gt));
	KUNIT_EXPECT_PTR_EQ(test, drm, xe_any_to_drm(drm));
	KUNIT_EXPECT_PTR_EQ(test, drm, xe_any_to_drm(dev));
	KUNIT_EXPECT_PTR_EQ(test, drm, xe_any_to_drm(pdev));
}

static void test_if_pdev(struct kunit *test)
{
	struct xe_device *xe = test->priv;
	struct xe_tile *tile = xe_device_get_root_tile(xe);
	struct xe_gt *gt = tile->primary_gt;
	struct drm_device *drm = &xe->drm;
	struct device *dev = xe->drm.dev;
	struct pci_dev *pdev = to_pci_dev(dev);

	KUNIT_EXPECT_TRUE(test, xe && drm && tile && gt && dev && pdev);
	KUNIT_EXPECT_NULL(test, xe_any_if_pdev(xe));
	KUNIT_EXPECT_NULL(test, xe_any_if_pdev(tile));
	KUNIT_EXPECT_NULL(test, xe_any_if_pdev(gt));
	KUNIT_EXPECT_NULL(test, xe_any_if_pdev(drm));
	KUNIT_EXPECT_NULL(test, xe_any_if_pdev(dev));
	KUNIT_EXPECT_PTR_EQ(test, pdev, xe_any_if_pdev(pdev));
}

static void test_if_xe(struct kunit *test)
{
	struct xe_device *xe = test->priv;
	struct xe_tile *tile = xe_device_get_root_tile(xe);
	struct xe_gt *gt = tile->primary_gt;
	struct drm_device *drm = &xe->drm;
	struct device *dev = xe->drm.dev;
	struct pci_dev *pdev = to_pci_dev(dev);

	KUNIT_EXPECT_TRUE(test, xe && drm && tile && gt && dev && pdev);
	KUNIT_EXPECT_PTR_EQ(test, xe, xe_any_if_xe(xe));
	KUNIT_EXPECT_NULL(test, xe_any_if_xe(tile));
	KUNIT_EXPECT_NULL(test, xe_any_if_xe(gt));
	KUNIT_EXPECT_NULL(test, xe_any_if_xe(drm));
	KUNIT_EXPECT_NULL(test, xe_any_if_xe(dev));
	KUNIT_EXPECT_NULL(test, xe_any_if_xe(pdev));
}

static void test_if_tile(struct kunit *test)
{
	struct xe_device *xe = test->priv;
	struct xe_tile *tile = xe_device_get_root_tile(xe);
	struct xe_gt *gt = tile->primary_gt;
	struct drm_device *drm = &xe->drm;
	struct device *dev = xe->drm.dev;
	struct pci_dev *pdev = to_pci_dev(dev);

	KUNIT_EXPECT_TRUE(test, xe && drm && tile && gt && dev && pdev);
	KUNIT_EXPECT_NULL(test, xe_any_if_tile(xe));
	KUNIT_EXPECT_PTR_EQ(test, tile, xe_any_if_tile(tile));
	KUNIT_EXPECT_NULL(test, xe_any_if_tile(gt));
	KUNIT_EXPECT_NULL(test, xe_any_if_tile(drm));
	KUNIT_EXPECT_NULL(test, xe_any_if_tile(dev));
	KUNIT_EXPECT_NULL(test, xe_any_if_tile(pdev));
}

static void test_if_gt(struct kunit *test)
{
	struct xe_device *xe = test->priv;
	struct xe_tile *tile = xe_device_get_root_tile(xe);
	struct xe_gt *gt = tile->primary_gt;
	struct drm_device *drm = &xe->drm;
	struct device *dev = xe->drm.dev;
	struct pci_dev *pdev = to_pci_dev(dev);

	KUNIT_EXPECT_TRUE(test, xe && drm && tile && gt && dev && pdev);
	KUNIT_EXPECT_NULL(test, xe_any_if_gt(xe));
	KUNIT_EXPECT_NULL(test, xe_any_if_gt(tile));
	KUNIT_EXPECT_PTR_EQ(test, gt, xe_any_if_gt(gt));
	KUNIT_EXPECT_NULL(test, xe_any_if_gt(drm));
	KUNIT_EXPECT_NULL(test, xe_any_if_gt(dev));
	KUNIT_EXPECT_NULL(test, xe_any_if_gt(pdev));
}

static void test_to_id(struct kunit *test)
{
	struct xe_device *xe = test->priv;
	struct xe_tile *tile = xe_device_get_root_tile(xe);
	struct xe_gt *gt = tile->primary_gt;
	struct device *dev = xe->drm.dev;
	struct pci_dev *pdev = to_pci_dev(dev);
	const struct xe_device *cxe = xe;
	const struct xe_tile *ctile = tile;
	const struct xe_gt *cgt = gt;

	tile->id = 1;
	gt->info.id = 2;

	KUNIT_EXPECT_EQ(test, 0, xe_any_id(xe));
	KUNIT_EXPECT_EQ(test, 1, xe_any_id(tile));
	KUNIT_EXPECT_EQ(test, 2, xe_any_id(gt));
	KUNIT_EXPECT_EQ(test, 0, xe_any_id(dev));
	KUNIT_EXPECT_EQ(test, 0, xe_any_id(pdev));
	KUNIT_EXPECT_EQ(test, 0, xe_any_id(cxe));
	KUNIT_EXPECT_EQ(test, 1, xe_any_id(ctile));
	KUNIT_EXPECT_EQ(test, 2, xe_any_id(cgt));
}

static struct kunit_case xe_any_tests[] = {
	KUNIT_CASE(test_to_xe),
	KUNIT_CASE(test_to_dev),
	KUNIT_CASE(test_to_pdev),
	KUNIT_CASE(test_to_drm),
	KUNIT_CASE(test_if_pdev),
	KUNIT_CASE(test_if_xe),
	KUNIT_CASE(test_if_tile),
	KUNIT_CASE(test_if_gt),
	KUNIT_CASE(test_to_id),
	{}
};

static struct kunit_suite xe_any_test_suite = {
	.name = "xe_any",
	.test_cases = xe_any_tests,
	.init = xe_kunit_helper_xe_device_test_init,
};

kunit_test_suite(xe_any_test_suite);
