// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_drv.h>
#include <drm/drm_kunit_helpers.h>

#include <kunit/test.h>

#include "xe_device.h"
#include "xe_kunit_helpers.h"
#include "xe_pci_test.h"
#include "xe_reg_sr.h"
#include "xe_tuning.h"
#include "xe_wa.h"

static int xe_wa_test_init(struct kunit *test)
{
	const struct xe_pci_fake_data *param = test->param_value;
	struct xe_pci_fake_data data = *param;
	struct xe_device *xe;
	struct device *dev;
	int ret;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	xe = xe_kunit_helper_alloc_xe_device(test, dev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xe);

	test->priv = &data;
	ret = xe_pci_fake_device_init(xe);
	KUNIT_ASSERT_EQ(test, ret, 0);

	if (!param->graphics_verx100)
		xe->info.step = param->step;

	/* TODO: init hw engines for engine/LRC WAs */
	xe->drm.dev = dev;
	test->priv = xe;

	return 0;
}

static void xe_wa_gt(struct kunit *test)
{
	struct xe_device *xe = test->priv;
	struct xe_gt *gt;
	int id;

	for_each_gt(gt, xe, id) {
		xe_reg_sr_init(&gt->reg_sr, "GT", xe);

		xe_wa_process_gt(gt);
		xe_tuning_process_gt(gt);

		KUNIT_ASSERT_EQ(test, gt->reg_sr.errors, 0);
	}
}

static struct kunit_case xe_wa_tests[] = {
	KUNIT_CASE_PARAM(xe_wa_gt, xe_pci_fake_data_gen_params),
	{}
};

static struct kunit_suite xe_rtp_test_suite = {
	.name = "xe_wa",
	.init = xe_wa_test_init,
	.test_cases = xe_wa_tests,
};

kunit_test_suite(xe_rtp_test_suite);
