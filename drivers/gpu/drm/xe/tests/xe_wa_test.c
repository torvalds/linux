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

struct platform_test_case {
	const char *name;
	enum xe_platform platform;
	enum xe_subplatform subplatform;
	u32 graphics_verx100;
	u32 media_verx100;
	struct xe_step_info step;
};

#define PLATFORM_CASE(platform__, graphics_step__)				\
	{									\
		.name = #platform__ " (" #graphics_step__ ")",			\
		.platform = XE_ ## platform__,					\
		.subplatform = XE_SUBPLATFORM_NONE,				\
		.step = { .graphics = STEP_ ## graphics_step__ }		\
	}


#define SUBPLATFORM_CASE(platform__, subplatform__, graphics_step__)			\
	{										\
		.name = #platform__ "_" #subplatform__ " (" #graphics_step__ ")",	\
		.platform = XE_ ## platform__,						\
		.subplatform = XE_SUBPLATFORM_ ## platform__ ## _ ## subplatform__,	\
		.step = { .graphics = STEP_ ## graphics_step__ }			\
	}

#define GMDID_CASE(platform__, graphics_verx100__, graphics_step__,		\
		   media_verx100__, media_step__)				\
	{									\
		.name = #platform__ " (g:" #graphics_step__ ", m:" #media_step__ ")",\
		.platform = XE_ ## platform__,					\
		.subplatform = XE_SUBPLATFORM_NONE,				\
		.graphics_verx100 = graphics_verx100__,				\
		.media_verx100 = media_verx100__,				\
		.step = { .graphics = STEP_ ## graphics_step__,			\
			   .media = STEP_ ## media_step__ }			\
	}

static const struct platform_test_case cases[] = {
	PLATFORM_CASE(TIGERLAKE, B0),
	PLATFORM_CASE(DG1, A0),
	PLATFORM_CASE(DG1, B0),
	PLATFORM_CASE(ALDERLAKE_S, A0),
	PLATFORM_CASE(ALDERLAKE_S, B0),
	PLATFORM_CASE(ALDERLAKE_S, C0),
	PLATFORM_CASE(ALDERLAKE_S, D0),
	PLATFORM_CASE(ALDERLAKE_P, A0),
	PLATFORM_CASE(ALDERLAKE_P, B0),
	PLATFORM_CASE(ALDERLAKE_P, C0),
	SUBPLATFORM_CASE(ALDERLAKE_S, RPLS, D0),
	SUBPLATFORM_CASE(ALDERLAKE_P, RPLU, E0),
	SUBPLATFORM_CASE(DG2, G10, C0),
	SUBPLATFORM_CASE(DG2, G11, B1),
	SUBPLATFORM_CASE(DG2, G12, A1),
	GMDID_CASE(METEORLAKE, 1270, A0, 1300, A0),
	GMDID_CASE(METEORLAKE, 1271, A0, 1300, A0),
	GMDID_CASE(METEORLAKE, 1274, A0, 1300, A0),
	GMDID_CASE(LUNARLAKE, 2004, A0, 2000, A0),
	GMDID_CASE(LUNARLAKE, 2004, B0, 2000, A0),
};

static void platform_desc(const struct platform_test_case *t, char *desc)
{
	strscpy(desc, t->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(platform, cases, platform_desc);

static int xe_wa_test_init(struct kunit *test)
{
	const struct platform_test_case *param = test->param_value;
	struct xe_pci_fake_data data = {
		.platform = param->platform,
		.subplatform = param->subplatform,
		.graphics_verx100 = param->graphics_verx100,
		.media_verx100 = param->media_verx100,
		.graphics_step = param->step.graphics,
		.media_step = param->step.media,
	};
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

static void xe_wa_test_exit(struct kunit *test)
{
	struct xe_device *xe = test->priv;

	drm_kunit_helper_free_device(test, xe->drm.dev);
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
	KUNIT_CASE_PARAM(xe_wa_gt, platform_gen_params),
	{}
};

static struct kunit_suite xe_rtp_test_suite = {
	.name = "xe_wa",
	.init = xe_wa_test_init,
	.exit = xe_wa_test_exit,
	.test_cases = xe_wa_tests,
};

kunit_test_suite(xe_rtp_test_suite);
