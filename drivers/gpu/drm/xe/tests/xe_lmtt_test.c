// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <kunit/test.h>

static const struct lmtt_ops_param {
	const char *desc;
	const struct xe_lmtt_ops *ops;
} lmtt_ops_params[] = {
	{ "2-level", &lmtt_2l_ops, },
	{ "multi-level", &lmtt_ml_ops, },
};

static void lmtt_ops_param_get_desc(const struct lmtt_ops_param *p, char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "%s", p->desc);
}

KUNIT_ARRAY_PARAM(lmtt_ops, lmtt_ops_params, lmtt_ops_param_get_desc);

static void test_ops(struct kunit *test)
{
	const struct lmtt_ops_param *p = test->param_value;
	const struct xe_lmtt_ops *ops = p->ops;
	unsigned int n;

	KUNIT_ASSERT_NOT_NULL(test, ops->lmtt_root_pd_level);
	KUNIT_ASSERT_NOT_NULL(test, ops->lmtt_pte_num);
	KUNIT_ASSERT_NOT_NULL(test, ops->lmtt_pte_size);
	KUNIT_ASSERT_NOT_NULL(test, ops->lmtt_pte_shift);
	KUNIT_ASSERT_NOT_NULL(test, ops->lmtt_pte_index);
	KUNIT_ASSERT_NOT_NULL(test, ops->lmtt_pte_encode);

	KUNIT_EXPECT_NE(test, ops->lmtt_root_pd_level(), 0);

	for (n = 0; n <= ops->lmtt_root_pd_level(); n++) {
		KUNIT_EXPECT_NE_MSG(test, ops->lmtt_pte_num(n), 0,
				    "level=%u", n);
		KUNIT_EXPECT_NE_MSG(test, ops->lmtt_pte_size(n), 0,
				    "level=%u", n);
		KUNIT_EXPECT_NE_MSG(test, ops->lmtt_pte_encode(0, n), LMTT_PTE_INVALID,
				    "level=%u", n);
	}

	for (n = 0; n < ops->lmtt_root_pd_level(); n++) {
		u64 addr = BIT_ULL(ops->lmtt_pte_shift(n));

		KUNIT_EXPECT_NE_MSG(test, ops->lmtt_pte_shift(n), 0,
				    "level=%u", n);
		KUNIT_EXPECT_EQ_MSG(test, ops->lmtt_pte_index(addr - 1, n), 0,
				    "addr=%#llx level=%u", addr, n);
		KUNIT_EXPECT_EQ_MSG(test, ops->lmtt_pte_index(addr + 1, n), 1,
				    "addr=%#llx level=%u", addr, n);
		KUNIT_EXPECT_EQ_MSG(test, ops->lmtt_pte_index(addr * 2 - 1, n), 1,
				    "addr=%#llx level=%u", addr, n);
		KUNIT_EXPECT_EQ_MSG(test, ops->lmtt_pte_index(addr * 2, n), 2,
				    "addr=%#llx level=%u", addr, n);
	}
}

static struct kunit_case lmtt_test_cases[] = {
	KUNIT_CASE_PARAM(test_ops, lmtt_ops_gen_params),
	{}
};

static struct kunit_suite lmtt_suite = {
	.name = "lmtt",
	.test_cases = lmtt_test_cases,
};

kunit_test_suites(&lmtt_suite);
