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
 */

#include <kunit/test.h>
#include <linux/revocable.h>

static void revocable_test_basic(struct kunit *test)
{
	struct revocable_provider *rp;
	struct revocable *rev;
	void *real_res = (void *)0x12345678, *res;

	rp = revocable_provider_alloc(real_res);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rp);

	rev = revocable_alloc(rp);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rev);

	res = revocable_try_access(rev);
	KUNIT_EXPECT_PTR_EQ(test, res, real_res);
	revocable_withdraw_access(rev);

	revocable_free(rev);
	revocable_provider_revoke(rp);
}

static void revocable_test_revocation(struct kunit *test)
{
	struct revocable_provider *rp;
	struct revocable *rev;
	void *real_res = (void *)0x12345678, *res;

	rp = revocable_provider_alloc(real_res);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rp);

	rev = revocable_alloc(rp);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rev);

	res = revocable_try_access(rev);
	KUNIT_EXPECT_PTR_EQ(test, res, real_res);
	revocable_withdraw_access(rev);

	revocable_provider_revoke(rp);

	res = revocable_try_access(rev);
	KUNIT_EXPECT_PTR_EQ(test, res, NULL);
	revocable_withdraw_access(rev);

	revocable_free(rev);
}

static void revocable_test_try_access_macro(struct kunit *test)
{
	struct revocable_provider *rp;
	struct revocable *rev;
	void *real_res = (void *)0x12345678, *res;

	rp = revocable_provider_alloc(real_res);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rp);

	rev = revocable_alloc(rp);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rev);

	{
		REVOCABLE_TRY_ACCESS_WITH(rev, res);
		KUNIT_EXPECT_PTR_EQ(test, res, real_res);
	}

	revocable_provider_revoke(rp);

	{
		REVOCABLE_TRY_ACCESS_WITH(rev, res);
		KUNIT_EXPECT_PTR_EQ(test, res, NULL);
	}

	revocable_free(rev);
}

static void revocable_test_try_access_macro2(struct kunit *test)
{
	struct revocable_provider *rp;
	struct revocable *rev;
	void *real_res = (void *)0x12345678, *res;
	bool accessed;

	rp = revocable_provider_alloc(real_res);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rp);

	rev = revocable_alloc(rp);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rev);

	accessed = false;
	REVOCABLE_TRY_ACCESS_SCOPED(rev, res) {
		KUNIT_EXPECT_PTR_EQ(test, res, real_res);
		accessed = true;
	}
	KUNIT_EXPECT_TRUE(test, accessed);

	revocable_provider_revoke(rp);

	accessed = false;
	REVOCABLE_TRY_ACCESS_SCOPED(rev, res) {
		KUNIT_EXPECT_PTR_EQ(test, res, NULL);
		accessed = true;
	}
	KUNIT_EXPECT_TRUE(test, accessed);

	revocable_free(rev);
}

static struct kunit_case revocable_test_cases[] = {
	KUNIT_CASE(revocable_test_basic),
	KUNIT_CASE(revocable_test_revocation),
	KUNIT_CASE(revocable_test_try_access_macro),
	KUNIT_CASE(revocable_test_try_access_macro2),
	{}
};

static struct kunit_suite revocable_test_suite = {
	.name = "revocable_test",
	.test_cases = revocable_test_cases,
};

kunit_test_suite(revocable_test_suite);
