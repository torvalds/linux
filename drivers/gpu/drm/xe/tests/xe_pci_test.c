// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_drv.h>
#include <drm/drm_kunit_helpers.h>

#include <kunit/test.h>

#include "tests/xe_test.h"

#include "xe_device.h"
#include "xe_pci_test.h"
#include "xe_pci_types.h"

static void check_graphics_ip(struct kunit *test)
{
	const struct xe_ip *param = test->param_value;
	const struct xe_graphics_desc *graphics = param->desc;
	u64 mask = graphics->hw_engine_mask;

	/* RCS, CCS, and BCS engines are allowed on the graphics IP */
	mask &= ~(XE_HW_ENGINE_RCS_MASK |
		  XE_HW_ENGINE_CCS_MASK |
		  XE_HW_ENGINE_BCS_MASK);

	/* Any remaining engines are an error */
	KUNIT_ASSERT_EQ(test, mask, 0);
}

static void check_media_ip(struct kunit *test)
{
	const struct xe_ip *param = test->param_value;
	const struct xe_media_desc *media = param->desc;
	u64 mask = media->hw_engine_mask;

	/* VCS, VECS and GSCCS engines are allowed on the media IP */
	mask &= ~(XE_HW_ENGINE_VCS_MASK |
		  XE_HW_ENGINE_VECS_MASK |
		  XE_HW_ENGINE_GSCCS_MASK);

	/* Any remaining engines are an error */
	KUNIT_ASSERT_EQ(test, mask, 0);
}

static void check_platform_gt_count(struct kunit *test)
{
	const struct pci_device_id *pci = test->param_value;
	const struct xe_device_desc *desc =
		(const struct xe_device_desc *)pci->driver_data;
	int max_gt = desc->max_gt_per_tile;

	KUNIT_ASSERT_GT(test, max_gt, 0);
	KUNIT_ASSERT_LE(test, max_gt, XE_MAX_GT_PER_TILE);
}

static struct kunit_case xe_pci_tests[] = {
	KUNIT_CASE_PARAM(check_graphics_ip, xe_pci_graphics_ip_gen_param),
	KUNIT_CASE_PARAM(check_media_ip, xe_pci_media_ip_gen_param),
	KUNIT_CASE_PARAM(check_platform_gt_count, xe_pci_id_gen_param),
	{}
};

static struct kunit_suite xe_pci_test_suite = {
	.name = "xe_pci",
	.test_cases = xe_pci_tests,
};

kunit_test_suite(xe_pci_test_suite);
