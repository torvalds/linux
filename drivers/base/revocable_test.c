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
 *
 * - Concurrent Access: Verifies multiple threads can access the resource.
 */

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/kthread.h>
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

struct test_concurrent_access_context {
	struct kunit *test;
	struct revocable_provider __rcu *rp;
	struct revocable rev;
	struct completion started, enter, exit;
	struct task_struct *thread;
	void *expected_res;
};

static int test_concurrent_access_provider(void *data)
{
	struct test_concurrent_access_context *ctx = data;

	complete(&ctx->started);

	wait_for_completion(&ctx->enter);
	revocable_provider_revoke(&ctx->rp);
	KUNIT_EXPECT_PTR_EQ(ctx->test, unrcu_pointer(ctx->rp), NULL);

	return 0;
}

static int test_concurrent_access_consumer(void *data)
{
	struct test_concurrent_access_context *ctx = data;
	void *res;

	complete(&ctx->started);

	wait_for_completion(&ctx->enter);
	res = revocable_try_access(&ctx->rev);
	KUNIT_EXPECT_PTR_EQ(ctx->test, res, ctx->expected_res);

	wait_for_completion(&ctx->exit);
	revocable_withdraw_access(&ctx->rev);

	return 0;
}

static void revocable_test_concurrent_access(struct kunit *test)
{
	struct revocable_provider __rcu *rp;
	void *real_res = (void *)0x12345678;
	struct test_concurrent_access_context *ctx;
	int ret, i;

	rp = revocable_provider_alloc(real_res);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, rp);

	ctx = kunit_kmalloc_array(test, 3, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	for (i = 0; i < 3; ++i) {
		ctx[i].test = test;
		init_completion(&ctx[i].started);
		init_completion(&ctx[i].enter);
		init_completion(&ctx[i].exit);

		if (i == 0) {
			ctx[i].rp = rp;
			ctx[i].thread = kthread_run(
				test_concurrent_access_provider, ctx + i,
				"revocable_provider_%d", i);
		} else {
			ret = revocable_init(rp, &ctx[i].rev);
			KUNIT_ASSERT_EQ(test, ret, 0);

			ctx[i].thread = kthread_run(
				test_concurrent_access_consumer, ctx + i,
				"revocable_consumer_%d", i);
		}
		KUNIT_ASSERT_FALSE(test, IS_ERR(ctx[i].thread));

		wait_for_completion(&ctx[i].started);
	}
	ctx[1].expected_res = real_res;
	ctx[2].expected_res = NULL;

	/* consumer1 enters read-side critical section */
	complete(&ctx[1].enter);
	msleep(100);
	/* provider0 revokes the resource */
	complete(&ctx[0].enter);
	msleep(100);
	/* consumer2 enters read-side critical section */
	complete(&ctx[2].enter);
	msleep(100);

	/* consumer{1,2} exit read-side critical section */
	complete(&ctx[1].exit);
	complete(&ctx[2].exit);

	for (i = 0; i < 3; ++i)
		kthread_stop(ctx[i].thread);
	for (i = 1; i < 3; ++i)
		revocable_deinit(&ctx[i].rev);
}

static struct kunit_case revocable_test_cases[] = {
	KUNIT_CASE(revocable_test_basic),
	KUNIT_CASE(revocable_test_revocation),
	KUNIT_CASE(revocable_test_try_access_macro),
	KUNIT_CASE(revocable_test_try_access_macro2),
	KUNIT_CASE(revocable_test_provider_use_after_free),
	KUNIT_CASE(revocable_test_concurrent_access),
	{}
};

static struct kunit_suite revocable_test_suite = {
	.name = "revocable_test",
	.test_cases = revocable_test_cases,
};

kunit_test_suite(revocable_test_suite);

MODULE_DESCRIPTION("KUnit tests for the revocable API");
MODULE_LICENSE("GPL");
