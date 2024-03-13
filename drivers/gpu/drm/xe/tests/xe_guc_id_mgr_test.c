// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <kunit/test.h>

#include "xe_device.h"
#include "xe_kunit_helpers.h"

static int guc_id_mgr_test_init(struct kunit *test)
{
	struct xe_guc_id_mgr *idm;

	xe_kunit_helper_xe_device_test_init(test);
	idm = &xe_device_get_gt(test->priv, 0)->uc.guc.submission_state.idm;

	mutex_init(idm_mutex(idm));
	test->priv = idm;
	return 0;
}

static void bad_init(struct kunit *test)
{
	struct xe_guc_id_mgr *idm = test->priv;

	KUNIT_EXPECT_EQ(test, -EINVAL, xe_guc_id_mgr_init(idm, 0));
	KUNIT_EXPECT_EQ(test, -ERANGE, xe_guc_id_mgr_init(idm, GUC_ID_MAX + 1));
}

static void no_init(struct kunit *test)
{
	struct xe_guc_id_mgr *idm = test->priv;

	mutex_lock(idm_mutex(idm));
	KUNIT_EXPECT_EQ(test, -ENODATA, xe_guc_id_mgr_reserve_locked(idm, 0));
	mutex_unlock(idm_mutex(idm));

	KUNIT_EXPECT_EQ(test, -ENODATA, xe_guc_id_mgr_reserve(idm, 1, 1));
}

static void init_fini(struct kunit *test)
{
	struct xe_guc_id_mgr *idm = test->priv;

	KUNIT_ASSERT_EQ(test, 0, xe_guc_id_mgr_init(idm, -1));
	KUNIT_EXPECT_NOT_NULL(test, idm->bitmap);
	KUNIT_EXPECT_EQ(test, idm->total, GUC_ID_MAX);
	__fini_idm(NULL, idm);
	KUNIT_EXPECT_NULL(test, idm->bitmap);
	KUNIT_EXPECT_EQ(test, idm->total, 0);
}

static void check_used(struct kunit *test)
{
	struct xe_guc_id_mgr *idm = test->priv;
	unsigned int n;

	KUNIT_ASSERT_EQ(test, 0, xe_guc_id_mgr_init(idm, 2));

	mutex_lock(idm_mutex(idm));

	for (n = 0; n < idm->total; n++) {
		kunit_info(test, "n=%u", n);
		KUNIT_EXPECT_EQ(test, idm->used, n);
		KUNIT_EXPECT_GE(test, idm_reserve_chunk_locked(idm, 1, 0), 0);
		KUNIT_EXPECT_EQ(test, idm->used, n + 1);
	}
	KUNIT_EXPECT_EQ(test, idm->used, idm->total);
	idm_release_chunk_locked(idm, 0, idm->used);
	KUNIT_EXPECT_EQ(test, idm->used, 0);

	mutex_unlock(idm_mutex(idm));
}

static void check_quota(struct kunit *test)
{
	struct xe_guc_id_mgr *idm = test->priv;
	unsigned int n;

	KUNIT_ASSERT_EQ(test, 0, xe_guc_id_mgr_init(idm, 2));

	mutex_lock(idm_mutex(idm));

	for (n = 0; n < idm->total - 1; n++) {
		kunit_info(test, "n=%u", n);
		KUNIT_EXPECT_EQ(test, idm_reserve_chunk_locked(idm, 1, idm->total), -EDQUOT);
		KUNIT_EXPECT_EQ(test, idm_reserve_chunk_locked(idm, 1, idm->total - n), -EDQUOT);
		KUNIT_EXPECT_EQ(test, idm_reserve_chunk_locked(idm, idm->total - n, 1), -EDQUOT);
		KUNIT_EXPECT_GE(test, idm_reserve_chunk_locked(idm, 1, 1), 0);
	}
	KUNIT_EXPECT_LE(test, 0, idm_reserve_chunk_locked(idm, 1, 0));
	KUNIT_EXPECT_EQ(test, idm->used, idm->total);
	idm_release_chunk_locked(idm, 0, idm->total);
	KUNIT_EXPECT_EQ(test, idm->used, 0);

	mutex_unlock(idm_mutex(idm));
}

static void check_all(struct kunit *test)
{
	struct xe_guc_id_mgr *idm = test->priv;
	unsigned int n;

	KUNIT_ASSERT_EQ(test, 0, xe_guc_id_mgr_init(idm, -1));

	mutex_lock(idm_mutex(idm));

	for (n = 0; n < idm->total; n++)
		KUNIT_EXPECT_LE(test, 0, idm_reserve_chunk_locked(idm, 1, 0));
	KUNIT_EXPECT_EQ(test, idm->used, idm->total);
	for (n = 0; n < idm->total; n++)
		idm_release_chunk_locked(idm, n, 1);

	mutex_unlock(idm_mutex(idm));
}

static struct kunit_case guc_id_mgr_test_cases[] = {
	KUNIT_CASE(bad_init),
	KUNIT_CASE(no_init),
	KUNIT_CASE(init_fini),
	KUNIT_CASE(check_used),
	KUNIT_CASE(check_quota),
	KUNIT_CASE_SLOW(check_all),
	{}
};

static struct kunit_suite guc_id_mgr_suite = {
	.name = "guc_idm",
	.test_cases = guc_id_mgr_test_cases,

	.init = guc_id_mgr_test_init,
	.exit = NULL,
};

kunit_test_suites(&guc_id_mgr_suite);
