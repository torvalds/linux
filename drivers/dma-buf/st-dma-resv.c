/* SPDX-License-Identifier: MIT */

/*
* Copyright © 2019 Intel Corporation
* Copyright © 2021 Advanced Micro Devices, Inc.
*/

#include <kunit/test.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/dma-resv.h>

static DEFINE_SPINLOCK(fence_lock);

struct dma_resv_usage_param {
	enum dma_resv_usage usage;
	const char *desc;
};

static const char *fence_name(struct dma_fence *f)
{
	return "selftest";
}

static const struct dma_fence_ops fence_ops = {
	.get_driver_name = fence_name,
	.get_timeline_name = fence_name,
};

static struct dma_fence *alloc_fence(void)
{
	struct dma_fence *f;

	f = kmalloc_obj(*f);
	if (!f)
		return NULL;

	dma_fence_init(f, &fence_ops, &fence_lock, 0, 0);
	return f;
}

static void test_sanitycheck(struct kunit *test)
{
	struct dma_resv resv;
	struct dma_fence *f;
	int r;

	f = alloc_fence();
	KUNIT_ASSERT_NOT_NULL(test, f);

	dma_fence_enable_sw_signaling(f);

	dma_fence_signal(f);
	dma_fence_put(f);

	dma_resv_init(&resv);
	r = dma_resv_lock(&resv, NULL);
	if (r)
		KUNIT_FAIL(test, "Resv locking failed\n");
	else
		dma_resv_unlock(&resv);
	dma_resv_fini(&resv);
}

static void test_signaling(struct kunit *test)
{
	const struct dma_resv_usage_param *param = test->param_value;
	enum dma_resv_usage usage = param->usage;
	struct dma_resv resv;
	struct dma_fence *f;
	int r;

	f = alloc_fence();
	KUNIT_ASSERT_NOT_NULL(test, f);

	dma_fence_enable_sw_signaling(f);

	dma_resv_init(&resv);
	r = dma_resv_lock(&resv, NULL);
	if (r) {
		KUNIT_FAIL(test, "Resv locking failed");
		goto err_free;
	}

	r = dma_resv_reserve_fences(&resv, 1);
	if (r) {
		KUNIT_FAIL(test, "Resv shared slot allocation failed");
		goto err_unlock;
	}

	dma_resv_add_fence(&resv, f, usage);
	if (dma_resv_test_signaled(&resv, usage)) {
		KUNIT_FAIL(test, "Resv unexpectedly signaled");
		goto err_unlock;
	}
	dma_fence_signal(f);
	if (!dma_resv_test_signaled(&resv, usage)) {
		KUNIT_FAIL(test, "Resv not reporting signaled");
		goto err_unlock;
	}
err_unlock:
	dma_resv_unlock(&resv);
err_free:
	dma_resv_fini(&resv);
	dma_fence_put(f);
}

static void test_for_each(struct kunit *test)
{
	const struct dma_resv_usage_param *param = test->param_value;
	enum dma_resv_usage usage = param->usage;
	struct dma_resv_iter cursor;
	struct dma_fence *f, *fence;
	struct dma_resv resv;
	int r;

	f = alloc_fence();
	KUNIT_ASSERT_NOT_NULL(test, f);

	dma_fence_enable_sw_signaling(f);

	dma_resv_init(&resv);
	r = dma_resv_lock(&resv, NULL);
	if (r) {
		KUNIT_FAIL(test, "Resv locking failed");
		goto err_free;
	}

	r = dma_resv_reserve_fences(&resv, 1);
	if (r) {
		KUNIT_FAIL(test, "Resv shared slot allocation failed");
		goto err_unlock;
	}

	dma_resv_add_fence(&resv, f, usage);

	r = -ENOENT;
	dma_resv_for_each_fence(&cursor, &resv, usage, fence) {
		if (!r) {
			KUNIT_FAIL(test, "More than one fence found");
			goto err_unlock;
		}
		if (f != fence) {
			KUNIT_FAIL(test, "Unexpected fence");
			r = -EINVAL;
			goto err_unlock;
		}
		if (dma_resv_iter_usage(&cursor) != usage) {
			KUNIT_FAIL(test, "Unexpected fence usage");
			r = -EINVAL;
			goto err_unlock;
		}
		r = 0;
	}
	if (r) {
		KUNIT_FAIL(test, "No fence found");
		goto err_unlock;
	}
	dma_fence_signal(f);
err_unlock:
	dma_resv_unlock(&resv);
err_free:
	dma_resv_fini(&resv);
	dma_fence_put(f);
}

static void test_for_each_unlocked(struct kunit *test)
{
	const struct dma_resv_usage_param *param = test->param_value;
	enum dma_resv_usage usage = param->usage;
	struct dma_resv_iter cursor;
	struct dma_fence *f, *fence;
	struct dma_resv resv;
	int r;

	f = alloc_fence();
	KUNIT_ASSERT_NOT_NULL(test, f);

	dma_fence_enable_sw_signaling(f);

	dma_resv_init(&resv);
	r = dma_resv_lock(&resv, NULL);
	if (r) {
		KUNIT_FAIL(test, "Resv locking failed");
		goto err_free;
	}

	r = dma_resv_reserve_fences(&resv, 1);
	if (r) {
		KUNIT_FAIL(test, "Resv shared slot allocation failed");
		dma_resv_unlock(&resv);
		goto err_free;
	}

	dma_resv_add_fence(&resv, f, usage);
	dma_resv_unlock(&resv);

	r = -ENOENT;
	dma_resv_iter_begin(&cursor, &resv, usage);
	dma_resv_for_each_fence_unlocked(&cursor, fence) {
		if (!r) {
			KUNIT_FAIL(test, "More than one fence found");
			goto err_iter_end;
		}
		if (!dma_resv_iter_is_restarted(&cursor)) {
			KUNIT_FAIL(test, "No restart flag");
			goto err_iter_end;
		}
		if (f != fence) {
			KUNIT_FAIL(test, "Unexpected fence");
			r = -EINVAL;
			goto err_iter_end;
		}
		if (dma_resv_iter_usage(&cursor) != usage) {
			KUNIT_FAIL(test, "Unexpected fence usage");
			r = -EINVAL;
			goto err_iter_end;
		}

		/* We use r as state here */
		if (r == -ENOENT) {
			r = -EINVAL;
			/* That should trigger an restart */
			cursor.fences = (void*)~0;
		} else if (r == -EINVAL) {
			r = 0;
		}
	}
	KUNIT_EXPECT_EQ(test, r, 0);
err_iter_end:
	dma_resv_iter_end(&cursor);
	dma_fence_signal(f);
err_free:
	dma_resv_fini(&resv);
	dma_fence_put(f);
}

static void test_get_fences(struct kunit *test)
{
	const struct dma_resv_usage_param *param = test->param_value;
	enum dma_resv_usage usage = param->usage;
	struct dma_fence *f, **fences = NULL;
	struct dma_resv resv;
	int r, i;

	f = alloc_fence();
	KUNIT_ASSERT_NOT_NULL(test, f);

	dma_fence_enable_sw_signaling(f);

	dma_resv_init(&resv);
	r = dma_resv_lock(&resv, NULL);
	if (r) {
		KUNIT_FAIL(test, "Resv locking failed");
		goto err_resv;
	}

	r = dma_resv_reserve_fences(&resv, 1);
	if (r) {
		KUNIT_FAIL(test, "Resv shared slot allocation failed");
		dma_resv_unlock(&resv);
		goto err_resv;
	}

	dma_resv_add_fence(&resv, f, usage);
	dma_resv_unlock(&resv);

	r = dma_resv_get_fences(&resv, usage, &i, &fences);
	if (r) {
		KUNIT_FAIL(test, "get_fences failed");
		goto err_free;
	}

	if (i != 1 || fences[0] != f) {
		KUNIT_FAIL(test, "get_fences returned unexpected fence");
		goto err_free;
	}

	dma_fence_signal(f);
err_free:
	while (i--)
		dma_fence_put(fences[i]);
	kfree(fences);
err_resv:
	dma_resv_fini(&resv);
	dma_fence_put(f);
}

static const struct dma_resv_usage_param dma_resv_usage_params[] = {
	{ DMA_RESV_USAGE_KERNEL, "kernel" },
	{ DMA_RESV_USAGE_WRITE, "write" },
	{ DMA_RESV_USAGE_READ, "read" },
	{ DMA_RESV_USAGE_BOOKKEEP, "bookkeep" },
};

KUNIT_ARRAY_PARAM_DESC(dma_resv_usage, dma_resv_usage_params, desc);

static struct kunit_case dma_resv_cases[] = {
	KUNIT_CASE(test_sanitycheck),
	KUNIT_CASE_PARAM(test_signaling, dma_resv_usage_gen_params),
	KUNIT_CASE_PARAM(test_for_each, dma_resv_usage_gen_params),
	KUNIT_CASE_PARAM(test_for_each_unlocked, dma_resv_usage_gen_params),
	KUNIT_CASE_PARAM(test_get_fences, dma_resv_usage_gen_params),
	{}
};

static struct kunit_suite dma_resv_test_suite = {
	.name = "dma-buf-resv",
	.test_cases = dma_resv_cases,
};

kunit_test_suite(dma_resv_test_suite);

MODULE_DESCRIPTION("KUnit tests for DMA-BUF");
MODULE_LICENSE("GPL");
