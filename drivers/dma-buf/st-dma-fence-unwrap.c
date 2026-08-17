// SPDX-License-Identifier: MIT

/*
 * Copyright (C) 2022 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>
#include <linux/dma-fence.h>
#include <linux/dma-fence-array.h>
#include <linux/dma-fence-chain.h>
#include <linux/dma-fence-unwrap.h>

#define CHAIN_SZ (4 << 10)

struct mock_fence {
	struct dma_fence base;
	spinlock_t lock;
};

static const char *mock_name(struct dma_fence *f)
{
	return "mock";
}

static const struct dma_fence_ops mock_ops = {
	.get_driver_name = mock_name,
	.get_timeline_name = mock_name,
};

static struct dma_fence *__mock_fence(u64 context, u64 seqno)
{
	struct mock_fence *f;

	f = kmalloc_obj(*f);
	if (!f)
		return NULL;

	spin_lock_init(&f->lock);
	dma_fence_init(&f->base, &mock_ops, &f->lock, context, seqno);

	return &f->base;
}

static struct dma_fence *mock_fence(void)
{
	return __mock_fence(dma_fence_context_alloc(1), 1);
}

static struct dma_fence *mock_array(unsigned int num_fences, ...)
{
	struct dma_fence_array *array;
	struct dma_fence **fences;
	va_list valist;
	int i;

	fences = kzalloc_objs(*fences, num_fences);
	if (!fences)
		goto error_put;

	va_start(valist, num_fences);
	for (i = 0; i < num_fences; ++i)
		fences[i] = va_arg(valist, typeof(*fences));
	va_end(valist);

	array = dma_fence_array_create(num_fences, fences,
				       dma_fence_context_alloc(1),
				       1);
	if (!array)
		goto error_free;
	return &array->base;

error_free:
	kfree(fences);

error_put:
	va_start(valist, num_fences);
	for (i = 0; i < num_fences; ++i)
		dma_fence_put(va_arg(valist, typeof(*fences)));
	va_end(valist);
	return NULL;
}

static struct dma_fence *mock_chain(struct dma_fence *prev,
				    struct dma_fence *fence)
{
	struct dma_fence_chain *f;

	f = dma_fence_chain_alloc();
	if (!f) {
		dma_fence_put(prev);
		dma_fence_put(fence);
		return NULL;
	}

	dma_fence_chain_init(f, prev, fence, 1);
	return &f->base;
}

static void test_sanitycheck(struct kunit *test)
{
	struct dma_fence *f, *chain, *array;

	f = mock_fence();
	KUNIT_ASSERT_NOT_NULL(test, f);

	dma_fence_enable_sw_signaling(f);

	array = mock_array(1, f);
	KUNIT_ASSERT_NOT_NULL(test, array);

	chain = mock_chain(NULL, array);
	KUNIT_ASSERT_NOT_NULL(test, chain);

	dma_fence_put(chain);
}

static void test_unwrap_array(struct kunit *test)
{
	struct dma_fence *fence, *f1, *f2, *array;
	struct dma_fence_unwrap iter;

	f1 = mock_fence();
	KUNIT_ASSERT_NOT_NULL(test, f1);

	dma_fence_enable_sw_signaling(f1);

	f2 = mock_fence();
	if (!f2) {
		KUNIT_FAIL(test, "Failed to create mock fence");
		dma_fence_put(f1);
		return;
	}

	dma_fence_enable_sw_signaling(f2);

	array = mock_array(2, f1, f2);
	KUNIT_ASSERT_NOT_NULL(test, array);

	dma_fence_unwrap_for_each(fence, &iter, array) {
		if (fence == f1) {
			f1 = NULL;
		} else if (fence == f2) {
			f2 = NULL;
		} else {
			KUNIT_FAIL(test, "Unexpected fence!");
		}
	}

	if (f1 || f2)
		KUNIT_FAIL(test, "Not all fences seen!");

	dma_fence_put(array);
}

static void test_unwrap_chain(struct kunit *test)
{
	struct dma_fence *fence, *f1, *f2, *chain;
	struct dma_fence_unwrap iter;

	f1 = mock_fence();
	KUNIT_ASSERT_NOT_NULL(test, f1);

	dma_fence_enable_sw_signaling(f1);

	f2 = mock_fence();
	if (!f2) {
		KUNIT_FAIL(test, "Failed to create mock fence");
		dma_fence_put(f1);
		return;
	}

	dma_fence_enable_sw_signaling(f2);

	chain = mock_chain(f1, f2);
	KUNIT_ASSERT_NOT_NULL(test, chain);

	dma_fence_unwrap_for_each(fence, &iter, chain) {
		if (fence == f1) {
			f1 = NULL;
		} else if (fence == f2) {
			f2 = NULL;
		} else {
			KUNIT_FAIL(test, "Unexpected fence!");
		}
	}

	if (f1 || f2)
		KUNIT_FAIL(test, "Not all fences seen!");

	dma_fence_put(chain);
}

static void test_unwrap_chain_array(struct kunit *test)
{
	struct dma_fence *fence, *f1, *f2, *array, *chain;
	struct dma_fence_unwrap iter;

	f1 = mock_fence();
	KUNIT_ASSERT_NOT_NULL(test, f1);

	dma_fence_enable_sw_signaling(f1);

	f2 = mock_fence();
	if (!f2) {
		KUNIT_FAIL(test, "Failed to create mock fence");
		dma_fence_put(f1);
		return;
	}

	dma_fence_enable_sw_signaling(f2);

	array = mock_array(2, f1, f2);
	KUNIT_ASSERT_NOT_NULL(test, array);

	chain = mock_chain(NULL, array);
	KUNIT_ASSERT_NOT_NULL(test, chain);

	dma_fence_unwrap_for_each(fence, &iter, chain) {
		if (fence == f1) {
			f1 = NULL;
		} else if (fence == f2) {
			f2 = NULL;
		} else {
			KUNIT_FAIL(test, "Unexpected fence!");
		}
	}

	if (f1 || f2)
		KUNIT_FAIL(test, "Not all fences seen!");

	dma_fence_put(chain);
}

static void test_unwrap_merge(struct kunit *test)
{
	struct dma_fence *fence, *f1, *f2, *f3;
	struct dma_fence_unwrap iter;

	f1 = mock_fence();
	KUNIT_ASSERT_NOT_NULL(test, f1);

	dma_fence_enable_sw_signaling(f1);

	f2 = mock_fence();
	if (!f2) {
		KUNIT_FAIL(test, "Failed to create mock fence");
		goto error_put_f1;
	}

	dma_fence_enable_sw_signaling(f2);

	f3 = dma_fence_unwrap_merge(f1, f2);
	if (!f3) {
		KUNIT_FAIL(test, "Failed to merge fences");
		goto error_put_f2;
	}

	dma_fence_unwrap_for_each(fence, &iter, f3) {
		if (fence == f1) {
			dma_fence_put(f1);
			f1 = NULL;
		} else if (fence == f2) {
			dma_fence_put(f2);
			f2 = NULL;
		} else {
			KUNIT_FAIL(test, "Unexpected fence!");
		}
	}

	if (f1 || f2)
		KUNIT_FAIL(test, "Not all fences seen!");

	dma_fence_put(f3);
error_put_f2:
	dma_fence_put(f2);
error_put_f1:
	dma_fence_put(f1);
}

static void test_unwrap_merge_duplicate(struct kunit *test)
{
	struct dma_fence *fence, *f1, *f2;
	struct dma_fence_unwrap iter;

	f1 = mock_fence();
	KUNIT_ASSERT_NOT_NULL(test, f1);

	dma_fence_enable_sw_signaling(f1);

	f2 = dma_fence_unwrap_merge(f1, f1);
	if (!f2) {
		KUNIT_FAIL(test, "Failed to merge fences");
		goto error_put_f1;
	}

	dma_fence_unwrap_for_each(fence, &iter, f2) {
		if (fence == f1) {
			dma_fence_put(f1);
			f1 = NULL;
		} else {
			KUNIT_FAIL(test, "Unexpected fence!");
		}
	}

	if (f1)
		KUNIT_FAIL(test, "Not all fences seen!");

	dma_fence_put(f2);
error_put_f1:
	dma_fence_put(f1);
}

static void test_unwrap_merge_seqno(struct kunit *test)
{
	struct dma_fence *fence, *f1, *f2, *f3, *f4;
	struct dma_fence_unwrap iter;
	u64 ctx[2];

	ctx[0] = dma_fence_context_alloc(1);
	ctx[1] = dma_fence_context_alloc(1);

	f1 = __mock_fence(ctx[1], 1);
	KUNIT_ASSERT_NOT_NULL(test, f1);

	dma_fence_enable_sw_signaling(f1);

	f2 = __mock_fence(ctx[1], 2);
	if (!f2) {
		KUNIT_FAIL(test, "Failed to create mock fence");
		goto error_put_f1;
	}

	dma_fence_enable_sw_signaling(f2);

	f3 = __mock_fence(ctx[0], 1);
	if (!f3) {
		KUNIT_FAIL(test, "Failed to create mock fence");
		goto error_put_f2;
	}

	dma_fence_enable_sw_signaling(f3);

	f4 = dma_fence_unwrap_merge(f1, f2, f3);
	if (!f4) {
		KUNIT_FAIL(test, "Failed to merge fences");
		goto error_put_f3;
	}

	dma_fence_unwrap_for_each(fence, &iter, f4) {
		if (fence == f3 && f2) {
			dma_fence_put(f3);
			f3 = NULL;
		} else if (fence == f2 && !f3) {
			dma_fence_put(f2);
			f2 = NULL;
		} else {
			KUNIT_FAIL(test, "Unexpected fence!");
		}
	}

	if (f2 || f3)
		KUNIT_FAIL(test, "Not all fences seen!");

	dma_fence_put(f4);
error_put_f3:
	dma_fence_put(f3);
error_put_f2:
	dma_fence_put(f2);
error_put_f1:
	dma_fence_put(f1);
}

static void test_unwrap_merge_order(struct kunit *test)
{
	struct dma_fence *fence, *f1, *f2, *a1, *a2, *c1, *c2;
	struct dma_fence_unwrap iter;

	f1 = mock_fence();
	KUNIT_ASSERT_NOT_NULL(test, f1);

	dma_fence_enable_sw_signaling(f1);

	f2 = mock_fence();
	if (!f2) {
		KUNIT_FAIL(test, "Failed to create mock fence");
		dma_fence_put(f1);
		return;
	}

	dma_fence_enable_sw_signaling(f2);

	a1 = mock_array(2, f1, f2);
	KUNIT_ASSERT_NOT_NULL(test, a1);

	c1 = mock_chain(NULL, dma_fence_get(f1));
	if (!c1) {
		KUNIT_FAIL(test, "Failed to create chain");
		goto error_put_a1;
	}

	c2 = mock_chain(c1, dma_fence_get(f2));
	if (!c2) {
		KUNIT_FAIL(test, "Failed to create chain");
		goto error_put_a1;
	}

	/*
	 * The fences in the chain are the same as in a1 but in oposite order,
	 * the dma_fence_merge() function should be able to handle that.
	 */
	a2 = dma_fence_unwrap_merge(a1, c2);

	dma_fence_unwrap_for_each(fence, &iter, a2) {
		if (fence == f1) {
			f1 = NULL;
			if (!f2)
				KUNIT_FAIL(test, "Unexpected order!");
		} else if (fence == f2) {
			f2 = NULL;
			if (f1)
				KUNIT_FAIL(test, "Unexpected order!");
		} else {
			KUNIT_FAIL(test, "Unexpected fence!");
		}
	}

	if (f1 || f2)
		KUNIT_FAIL(test, "Not all fences seen!");

	dma_fence_put(a2);
	return;

error_put_a1:
	dma_fence_put(a1);
}

static void test_unwrap_merge_complex(struct kunit *test)
{
	struct dma_fence *fence, *f1, *f2, *f3, *f4, *f5;
	struct dma_fence_unwrap iter;

	f1 = mock_fence();
	KUNIT_ASSERT_NOT_NULL(test, f1);

	dma_fence_enable_sw_signaling(f1);

	f2 = mock_fence();
	if (!f2) {
		KUNIT_FAIL(test, "Failed to create mock fence");
		goto error_put_f1;
	}

	dma_fence_enable_sw_signaling(f2);

	f3 = dma_fence_unwrap_merge(f1, f2);
	if (!f3) {
		KUNIT_FAIL(test, "Failed to merge fences");
		goto error_put_f2;
	}

	/* The resulting array has the fences in reverse */
	f4 = mock_array(2, dma_fence_get(f2), dma_fence_get(f1));
	if (!f4) {
		KUNIT_FAIL(test, "Failed to create array");
		goto error_put_f3;
	}

	/* Signaled fences should be filtered, the two arrays merged. */
	f5 = dma_fence_unwrap_merge(f3, f4, dma_fence_get_stub());
	if (!f5) {
		KUNIT_FAIL(test, "Failed to merge fences");
		goto error_put_f4;
	}

	dma_fence_unwrap_for_each(fence, &iter, f5) {
		if (fence == f1) {
			dma_fence_put(f1);
			f1 = NULL;
		} else if (fence == f2) {
			dma_fence_put(f2);
			f2 = NULL;
		} else {
			KUNIT_FAIL(test, "Unexpected fence!");
		}
	}

	if (f1 || f2)
		KUNIT_FAIL(test, "Not all fences seen!");

	dma_fence_put(f5);
error_put_f4:
	dma_fence_put(f4);
error_put_f3:
	dma_fence_put(f3);
error_put_f2:
	dma_fence_put(f2);
error_put_f1:
	dma_fence_put(f1);
}

static void test_unwrap_merge_complex_seqno(struct kunit *test)
{
	struct dma_fence *fence, *f1, *f2, *f3, *f4, *f5, *f6, *f7;
	struct dma_fence_unwrap iter;
	u64 ctx[2];

	ctx[0] = dma_fence_context_alloc(1);
	ctx[1] = dma_fence_context_alloc(1);

	f1 = __mock_fence(ctx[0], 2);
	KUNIT_ASSERT_NOT_NULL(test, f1);

	dma_fence_enable_sw_signaling(f1);

	f2 = __mock_fence(ctx[1], 1);
	if (!f2) {
		KUNIT_FAIL(test, "Failed to create mock fence");
		goto error_put_f1;
	}

	dma_fence_enable_sw_signaling(f2);

	f3 = __mock_fence(ctx[0], 1);
	if (!f3) {
		KUNIT_FAIL(test, "Failed to create mock fence");
		goto error_put_f2;
	}

	dma_fence_enable_sw_signaling(f3);

	f4 = __mock_fence(ctx[1], 2);
	if (!f4) {
		KUNIT_FAIL(test, "Failed to create mock fence");
		goto error_put_f3;
	}

	dma_fence_enable_sw_signaling(f4);

	f5 = mock_array(2, dma_fence_get(f1), dma_fence_get(f2));
	if (!f5) {
		KUNIT_FAIL(test, "Failed to create array");
		goto error_put_f4;
	}

	f6 = mock_array(2, dma_fence_get(f3), dma_fence_get(f4));
	if (!f6) {
		KUNIT_FAIL(test, "Failed to create array");
		goto error_put_f5;
	}

	f7 = dma_fence_unwrap_merge(f5, f6);
	if (!f7) {
		KUNIT_FAIL(test, "Failed to merge fences");
		goto error_put_f6;
	}

	dma_fence_unwrap_for_each(fence, &iter, f7) {
		if (fence == f1 && f4) {
			dma_fence_put(f1);
			f1 = NULL;
		} else if (fence == f4 && !f1) {
			dma_fence_put(f4);
			f4 = NULL;
		} else {
			KUNIT_FAIL(test, "Unexpected fence!");
		}
	}

	if (f1 || f4)
		KUNIT_FAIL(test, "Not all fences seen!");

	dma_fence_put(f7);
error_put_f6:
	dma_fence_put(f6);
error_put_f5:
	dma_fence_put(f5);
error_put_f4:
	dma_fence_put(f4);
error_put_f3:
	dma_fence_put(f3);
error_put_f2:
	dma_fence_put(f2);
error_put_f1:
	dma_fence_put(f1);
}

static struct kunit_case dma_fence_unwrap_cases[] = {
	KUNIT_CASE(test_sanitycheck),
	KUNIT_CASE(test_unwrap_array),
	KUNIT_CASE(test_unwrap_chain),
	KUNIT_CASE(test_unwrap_chain_array),
	KUNIT_CASE(test_unwrap_merge),
	KUNIT_CASE(test_unwrap_merge_duplicate),
	KUNIT_CASE(test_unwrap_merge_seqno),
	KUNIT_CASE(test_unwrap_merge_order),
	KUNIT_CASE(test_unwrap_merge_complex),
	KUNIT_CASE(test_unwrap_merge_complex_seqno),
	{}
};

static struct kunit_suite dma_fence_unwrap_test_suite = {
	.name = "dma-buf-fence-unwrap",
	.test_cases = dma_fence_unwrap_cases,
};

kunit_test_suite(dma_fence_unwrap_test_suite);
