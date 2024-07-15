// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <kunit/test.h>

#include "xe_device.h"
#include "xe_kunit_helpers.h"

static int guc_dbm_test_init(struct kunit *test)
{
	struct xe_guc_db_mgr *dbm;

	xe_kunit_helper_xe_device_test_init(test);
	dbm = &xe_device_get_gt(test->priv, 0)->uc.guc.dbm;

	mutex_init(dbm_mutex(dbm));
	test->priv = dbm;
	return 0;
}

static void test_empty(struct kunit *test)
{
	struct xe_guc_db_mgr *dbm = test->priv;

	KUNIT_ASSERT_EQ(test, xe_guc_db_mgr_init(dbm, 0), 0);
	KUNIT_ASSERT_EQ(test, dbm->count, 0);

	mutex_lock(dbm_mutex(dbm));
	KUNIT_EXPECT_LT(test, xe_guc_db_mgr_reserve_id_locked(dbm), 0);
	mutex_unlock(dbm_mutex(dbm));

	KUNIT_EXPECT_LT(test, xe_guc_db_mgr_reserve_range(dbm, 1, 0), 0);
}

static void test_default(struct kunit *test)
{
	struct xe_guc_db_mgr *dbm = test->priv;

	KUNIT_ASSERT_EQ(test, xe_guc_db_mgr_init(dbm, ~0), 0);
	KUNIT_ASSERT_EQ(test, dbm->count, GUC_NUM_DOORBELLS);
}

static const unsigned int guc_dbm_params[] = {
	GUC_NUM_DOORBELLS / 64,
	GUC_NUM_DOORBELLS / 32,
	GUC_NUM_DOORBELLS / 8,
	GUC_NUM_DOORBELLS,
};

static void uint_param_get_desc(const unsigned int *p, char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "%u", *p);
}

KUNIT_ARRAY_PARAM(guc_dbm, guc_dbm_params, uint_param_get_desc);

static void test_size(struct kunit *test)
{
	const unsigned int *p = test->param_value;
	struct xe_guc_db_mgr *dbm = test->priv;
	unsigned int n;
	int id;

	KUNIT_ASSERT_EQ(test, xe_guc_db_mgr_init(dbm, *p), 0);
	KUNIT_ASSERT_EQ(test, dbm->count, *p);

	mutex_lock(dbm_mutex(dbm));
	for (n = 0; n < *p; n++) {
		KUNIT_EXPECT_GE(test, id = xe_guc_db_mgr_reserve_id_locked(dbm), 0);
		KUNIT_EXPECT_LT(test, id, dbm->count);
	}
	KUNIT_EXPECT_LT(test, xe_guc_db_mgr_reserve_id_locked(dbm), 0);
	mutex_unlock(dbm_mutex(dbm));

	mutex_lock(dbm_mutex(dbm));
	for (n = 0; n < *p; n++)
		xe_guc_db_mgr_release_id_locked(dbm, n);
	mutex_unlock(dbm_mutex(dbm));
}

static void test_reuse(struct kunit *test)
{
	const unsigned int *p = test->param_value;
	struct xe_guc_db_mgr *dbm = test->priv;
	unsigned int n;

	KUNIT_ASSERT_EQ(test, xe_guc_db_mgr_init(dbm, *p), 0);

	mutex_lock(dbm_mutex(dbm));
	for (n = 0; n < *p; n++)
		KUNIT_EXPECT_GE(test, xe_guc_db_mgr_reserve_id_locked(dbm), 0);
	KUNIT_EXPECT_LT(test, xe_guc_db_mgr_reserve_id_locked(dbm), 0);
	mutex_unlock(dbm_mutex(dbm));

	mutex_lock(dbm_mutex(dbm));
	for (n = 0; n < *p; n++) {
		xe_guc_db_mgr_release_id_locked(dbm, n);
		KUNIT_EXPECT_EQ(test, xe_guc_db_mgr_reserve_id_locked(dbm), n);
	}
	KUNIT_EXPECT_LT(test, xe_guc_db_mgr_reserve_id_locked(dbm), 0);
	mutex_unlock(dbm_mutex(dbm));

	mutex_lock(dbm_mutex(dbm));
	for (n = 0; n < *p; n++)
		xe_guc_db_mgr_release_id_locked(dbm, n);
	mutex_unlock(dbm_mutex(dbm));
}

static void test_range_overlap(struct kunit *test)
{
	const unsigned int *p = test->param_value;
	struct xe_guc_db_mgr *dbm = test->priv;
	int id1, id2, id3;
	unsigned int n;

	KUNIT_ASSERT_EQ(test, xe_guc_db_mgr_init(dbm, ~0), 0);
	KUNIT_ASSERT_LE(test, *p, dbm->count);

	KUNIT_ASSERT_GE(test, id1 = xe_guc_db_mgr_reserve_range(dbm, *p, 0), 0);
	for (n = 0; n < dbm->count - *p; n++) {
		KUNIT_ASSERT_GE(test, id2 = xe_guc_db_mgr_reserve_range(dbm, 1, 0), 0);
		KUNIT_ASSERT_NE(test, id2, id1);
		KUNIT_ASSERT_NE_MSG(test, id2 < id1, id2 > id1 + *p - 1,
				    "id1=%d id2=%d", id1, id2);
	}
	KUNIT_ASSERT_LT(test, xe_guc_db_mgr_reserve_range(dbm, 1, 0), 0);
	xe_guc_db_mgr_release_range(dbm, 0, dbm->count);

	if (*p >= 1) {
		KUNIT_ASSERT_GE(test, id1 = xe_guc_db_mgr_reserve_range(dbm, 1, 0), 0);
		KUNIT_ASSERT_GE(test, id2 = xe_guc_db_mgr_reserve_range(dbm, *p - 1, 0), 0);
		KUNIT_ASSERT_NE(test, id2, id1);
		KUNIT_ASSERT_NE_MSG(test, id1 < id2, id1 > id2 + *p - 2,
				    "id1=%d id2=%d", id1, id2);
		for (n = 0; n < dbm->count - *p; n++) {
			KUNIT_ASSERT_GE(test, id3 = xe_guc_db_mgr_reserve_range(dbm, 1, 0), 0);
			KUNIT_ASSERT_NE(test, id3, id1);
			KUNIT_ASSERT_NE(test, id3, id2);
			KUNIT_ASSERT_NE_MSG(test, id3 < id2, id3 > id2 + *p - 2,
					    "id3=%d id2=%d", id3, id2);
		}
		KUNIT_ASSERT_LT(test, xe_guc_db_mgr_reserve_range(dbm, 1, 0), 0);
		xe_guc_db_mgr_release_range(dbm, 0, dbm->count);
	}
}

static void test_range_compact(struct kunit *test)
{
	const unsigned int *p = test->param_value;
	struct xe_guc_db_mgr *dbm = test->priv;
	unsigned int n;

	KUNIT_ASSERT_EQ(test, xe_guc_db_mgr_init(dbm, ~0), 0);
	KUNIT_ASSERT_NE(test, *p, 0);
	KUNIT_ASSERT_LE(test, *p, dbm->count);
	if (dbm->count % *p)
		kunit_skip(test, "must be divisible");

	KUNIT_ASSERT_GE(test, xe_guc_db_mgr_reserve_range(dbm, *p, 0), 0);
	for (n = 1; n < dbm->count / *p; n++)
		KUNIT_ASSERT_GE(test, xe_guc_db_mgr_reserve_range(dbm, *p, 0), 0);
	KUNIT_ASSERT_LT(test, xe_guc_db_mgr_reserve_range(dbm, 1, 0), 0);
	xe_guc_db_mgr_release_range(dbm, 0, dbm->count);
}

static void test_range_spare(struct kunit *test)
{
	const unsigned int *p = test->param_value;
	struct xe_guc_db_mgr *dbm = test->priv;
	int id;

	KUNIT_ASSERT_EQ(test, xe_guc_db_mgr_init(dbm, ~0), 0);
	KUNIT_ASSERT_LE(test, *p, dbm->count);

	KUNIT_ASSERT_LT(test, xe_guc_db_mgr_reserve_range(dbm, *p, dbm->count), 0);
	KUNIT_ASSERT_LT(test, xe_guc_db_mgr_reserve_range(dbm, *p, dbm->count - *p + 1), 0);
	KUNIT_ASSERT_EQ(test, id = xe_guc_db_mgr_reserve_range(dbm, *p, dbm->count - *p), 0);
	KUNIT_ASSERT_LT(test, xe_guc_db_mgr_reserve_range(dbm, 1, dbm->count - *p), 0);
	xe_guc_db_mgr_release_range(dbm, id, *p);
}

static struct kunit_case guc_dbm_test_cases[] = {
	KUNIT_CASE(test_empty),
	KUNIT_CASE(test_default),
	KUNIT_CASE_PARAM(test_size, guc_dbm_gen_params),
	KUNIT_CASE_PARAM(test_reuse, guc_dbm_gen_params),
	KUNIT_CASE_PARAM(test_range_overlap, guc_dbm_gen_params),
	KUNIT_CASE_PARAM(test_range_compact, guc_dbm_gen_params),
	KUNIT_CASE_PARAM(test_range_spare, guc_dbm_gen_params),
	{}
};

static struct kunit_suite guc_dbm_suite = {
	.name = "guc_dbm",
	.test_cases = guc_dbm_test_cases,
	.init = guc_dbm_test_init,
};

kunit_test_suites(&guc_dbm_suite);
