// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2026 Google LLC
 *
 * Kunit tests for the revocable API.
 *
 * The test cases cover the following scenarios:
 *
 * - Basic: Verifies that a consumer can successfully access the resource
 *   provided via the provider.
 *
 * - Revocation: Verifies that after the provider revokes the resource,
 *   the consumer correctly receives a NULL pointer on a subsequent access.
 *
 * - Try Access Macro: Same as "Revocation" but uses the
 *   REVOCABLE_TRY_ACCESS_WITH() and REVOCABLE_TRY_ACCESS_SCOPED().
 *
 * - Provider Use-after-free: Verifies revocable_init() correctly handles
 *   race conditions where the provider is being released.
 */

#include <kunit/test.h>
#include <linux/refcount.h>
#include <linux/revocable.h>

static void revocable_test_basic(struct kunit *test)
{
	struct revocable_provider __rcu *rp;
	struct revocable rev;
	void *real_res = (void *)0x12345678, *res;
	int ret;

	rp = revocable_provider_alloc(real_res);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rp);

	ret = revocable_init(rp, &rev);
	KUNIT_ASSERT_EQ(test, ret, 0);

	res = revocable_try_access(&rev);
	KUNIT_EXPECT_PTR_EQ(test, res, real_res);
	revocable_withdraw_access(&rev);

	revocable_deinit(&rev);
	revocable_provider_revoke(&rp);
	KUNIT_EXPECT_PTR_EQ(test, unrcu_pointer(rp), NULL);
}

static void revocable_test_revocation(struct kunit *test)
{
	struct revocable_provider __rcu *rp;
	struct revocable rev;
	void *real_res = (void *)0x12345678, *res;
	int ret;

	rp = revocable_provider_alloc(real_res);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rp);

	ret = revocable_init(rp, &rev);
	KUNIT_ASSERT_EQ(test, ret, 0);

	res = revocable_try_access(&rev);
	KUNIT_EXPECT_PTR_EQ(test, res, real_res);
	revocable_withdraw_access(&rev);

	revocable_provider_revoke(&rp);
	KUNIT_EXPECT_PTR_EQ(test, unrcu_pointer(rp), NULL);

	res = revocable_try_access(&rev);
	KUNIT_EXPECT_PTR_EQ(test, res, NULL);
	revocable_withdraw_access(&rev);

	revocable_deinit(&rev);
}

static void revocable_test_try_access_macro(struct kunit *test)
{
	struct revocable_provider __rcu *rp;
	void *real_res = (void *)0x12345678, *res;

	rp = revocable_provider_alloc(real_res);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rp);

	{
		REVOCABLE_TRY_ACCESS_WITH(rp, res);
		KUNIT_EXPECT_PTR_EQ(test, res, real_res);
	}

	revocable_provider_revoke(&rp);
	KUNIT_EXPECT_PTR_EQ(test, unrcu_pointer(rp), NULL);

	{
		REVOCABLE_TRY_ACCESS_WITH(rp, res);
		KUNIT_EXPECT_PTR_EQ(test, res, NULL);
	}
}

static void revocable_test_try_access_macro2(struct kunit *test)
{
	struct revocable_provider __rcu *rp;
	void *real_res = (void *)0x12345678, *res;
	bool accessed;

	rp = revocable_provider_alloc(real_res);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rp);

	accessed = false;
	REVOCABLE_TRY_ACCESS_SCOPED(rp, res) {
		KUNIT_EXPECT_PTR_EQ(test, res, real_res);
		accessed = true;
	}
	KUNIT_EXPECT_TRUE(test, accessed);

	revocable_provider_revoke(&rp);
	KUNIT_EXPECT_PTR_EQ(test, unrcu_pointer(rp), NULL);

	accessed = false;
	REVOCABLE_TRY_ACCESS_SCOPED(rp, res) {
		KUNIT_EXPECT_PTR_EQ(test, res, NULL);
		accessed = true;
	}
	KUNIT_EXPECT_TRUE(test, accessed);
}

static void revocable_test_provider_use_after_free(struct kunit *test)
{
	struct revocable_provider __rcu *rp;
	struct revocable_provider *old_rp;
	struct revocable rev;
	void *real_res = (void *)0x12345678;
	int ret;

	rp = revocable_provider_alloc(real_res);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rp);

	ret = revocable_init(NULL, &rev);
	KUNIT_EXPECT_NE(test, ret, 0);

	/* Simulate the provider has been freed. */
	old_rp = rcu_replace_pointer(rp, NULL, 1);
	ret = revocable_init(rp, &rev);
	KUNIT_EXPECT_NE(test, ret, 0);
	rcu_replace_pointer(rp, old_rp, 1);

	struct {
		struct srcu_struct srcu;
		void __rcu *res;
		struct kref kref;
		struct rcu_head rcu;
	} *rp_internal = (void *)rp;

	/* Simulate the provider is releasing. */
	refcount_set(&rp_internal->kref.refcount, 0);
	ret = revocable_init(rp, &rev);
	KUNIT_EXPECT_NE(test, ret, 0);
	refcount_set(&rp_internal->kref.refcount, 1);

	revocable_provider_revoke(&rp);
	KUNIT_EXPECT_PTR_EQ(test, unrcu_pointer(rp), NULL);
	ret = revocable_init(rp, &rev);
	KUNIT_EXPECT_NE(test, ret, 0);
}

static struct kunit_case revocable_test_cases[] = {
	KUNIT_CASE(revocable_test_basic),
	KUNIT_CASE(revocable_test_revocation),
	KUNIT_CASE(revocable_test_try_access_macro),
	KUNIT_CASE(revocable_test_try_access_macro2),
	KUNIT_CASE(revocable_test_provider_use_after_free),
	{}
};

static struct kunit_suite revocable_test_suite = {
	.name = "revocable_test",
	.test_cases = revocable_test_cases,
};

kunit_test_suite(revocable_test_suite);

MODULE_DESCRIPTION("KUnit tests for the revocable API");
MODULE_LICENSE("GPL");
