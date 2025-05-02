// SPDX-License-Identifier: MIT

/*
 * Copyright (C) 2022 Advanced Micro Devices, Inc.
 */

#include <linux/dma-fence.h>
#include <linux/dma-fence-array.h>
#include <linux/dma-fence-chain.h>
#include <linux/dma-fence-unwrap.h>

#include "selftest.h"

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

	f = kmalloc(sizeof(*f), GFP_KERNEL);
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

	fences = kcalloc(num_fences, sizeof(*fences), GFP_KERNEL);
	if (!fences)
		goto error_put;

	va_start(valist, num_fences);
	for (i = 0; i < num_fences; ++i)
		fences[i] = va_arg(valist, typeof(*fences));
	va_end(valist);

	array = dma_fence_array_create(num_fences, fences,
				       dma_fence_context_alloc(1),
				       1, false);
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

static int sanitycheck(void *arg)
{
	struct dma_fence *f, *chain, *array;
	int err = 0;

	f = mock_fence();
	if (!f)
		return -ENOMEM;

	dma_fence_enable_sw_signaling(f);

	array = mock_array(1, f);
	if (!array)
		return -ENOMEM;

	chain = mock_chain(NULL, array);
	if (!chain)
		return -ENOMEM;

	dma_fence_put(chain);
	return err;
}

static int unwrap_array(void *arg)
{
	struct dma_fence *fence, *f1, *f2, *array;
	struct dma_fence_unwrap iter;
	int err = 0;

	f1 = mock_fence();
	if (!f1)
		return -ENOMEM;

	dma_fence_enable_sw_signaling(f1);

	f2 = mock_fence();
	if (!f2) {
		dma_fence_put(f1);
		return -ENOMEM;
	}

	dma_fence_enable_sw_signaling(f2);

	array = mock_array(2, f1, f2);
	if (!array)
		return -ENOMEM;

	dma_fence_unwrap_for_each(fence, &iter, array) {
		if (fence == f1) {
			f1 = NULL;
		} else if (fence == f2) {
			f2 = NULL;
		} else {
			pr_err("Unexpected fence!\n");
			err = -EINVAL;
		}
	}

	if (f1 || f2) {
		pr_err("Not all fences seen!\n");
		err = -EINVAL;
	}

	dma_fence_put(array);
	return err;
}

static int unwrap_chain(void *arg)
{
	struct dma_fence *fence, *f1, *f2, *chain;
	struct dma_fence_unwrap iter;
	int err = 0;

	f1 = mock_fence();
	if (!f1)
		return -ENOMEM;

	dma_fence_enable_sw_signaling(f1);

	f2 = mock_fence();
	if (!f2) {
		dma_fence_put(f1);
		return -ENOMEM;
	}

	dma_fence_enable_sw_signaling(f2);

	chain = mock_chain(f1, f2);
	if (!chain)
		return -ENOMEM;

	dma_fence_unwrap_for_each(fence, &iter, chain) {
		if (fence == f1) {
			f1 = NULL;
		} else if (fence == f2) {
			f2 = NULL;
		} else {
			pr_err("Unexpected fence!\n");
			err = -EINVAL;
		}
	}

	if (f1 || f2) {
		pr_err("Not all fences seen!\n");
		err = -EINVAL;
	}

	dma_fence_put(chain);
	return err;
}

static int unwrap_chain_array(void *arg)
{
	struct dma_fence *fence, *f1, *f2, *array, *chain;
	struct dma_fence_unwrap iter;
	int err = 0;

	f1 = mock_fence();
	if (!f1)
		return -ENOMEM;

	dma_fence_enable_sw_signaling(f1);

	f2 = mock_fence();
	if (!f2) {
		dma_fence_put(f1);
		return -ENOMEM;
	}

	dma_fence_enable_sw_signaling(f2);

	array = mock_array(2, f1, f2);
	if (!array)
		return -ENOMEM;

	chain = mock_chain(NULL, array);
	if (!chain)
		return -ENOMEM;

	dma_fence_unwrap_for_each(fence, &iter, chain) {
		if (fence == f1) {
			f1 = NULL;
		} else if (fence == f2) {
			f2 = NULL;
		} else {
			pr_err("Unexpected fence!\n");
			err = -EINVAL;
		}
	}

	if (f1 || f2) {
		pr_err("Not all fences seen!\n");
		err = -EINVAL;
	}

	dma_fence_put(chain);
	return err;
}

static int unwrap_merge(void *arg)
{
	struct dma_fence *fence, *f1, *f2, *f3;
	struct dma_fence_unwrap iter;
	int err = 0;

	f1 = mock_fence();
	if (!f1)
		return -ENOMEM;

	dma_fence_enable_sw_signaling(f1);

	f2 = mock_fence();
	if (!f2) {
		err = -ENOMEM;
		goto error_put_f1;
	}

	dma_fence_enable_sw_signaling(f2);

	f3 = dma_fence_unwrap_merge(f1, f2);
	if (!f3) {
		err = -ENOMEM;
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
			pr_err("Unexpected fence!\n");
			err = -EINVAL;
		}
	}

	if (f1 || f2) {
		pr_err("Not all fences seen!\n");
		err = -EINVAL;
	}

	dma_fence_put(f3);
error_put_f2:
	dma_fence_put(f2);
error_put_f1:
	dma_fence_put(f1);
	return err;
}

static int unwrap_merge_duplicate(void *arg)
{
	struct dma_fence *fence, *f1, *f2;
	struct dma_fence_unwrap iter;
	int err = 0;

	f1 = mock_fence();
	if (!f1)
		return -ENOMEM;

	dma_fence_enable_sw_signaling(f1);

	f2 = dma_fence_unwrap_merge(f1, f1);
	if (!f2) {
		err = -ENOMEM;
		goto error_put_f1;
	}

	dma_fence_unwrap_for_each(fence, &iter, f2) {
		if (fence == f1) {
			dma_fence_put(f1);
			f1 = NULL;
		} else {
			pr_err("Unexpected fence!\n");
			err = -EINVAL;
		}
	}

	if (f1) {
		pr_err("Not all fences seen!\n");
		err = -EINVAL;
	}

	dma_fence_put(f2);
error_put_f1:
	dma_fence_put(f1);
	return err;
}

static int unwrap_merge_seqno(void *arg)
{
	struct dma_fence *fence, *f1, *f2, *f3, *f4;
	struct dma_fence_unwrap iter;
	int err = 0;
	u64 ctx[2];

	ctx[0] = dma_fence_context_alloc(1);
	ctx[1] = dma_fence_context_alloc(1);

	f1 = __mock_fence(ctx[1], 1);
	if (!f1)
		return -ENOMEM;

	dma_fence_enable_sw_signaling(f1);

	f2 = __mock_fence(ctx[1], 2);
	if (!f2) {
		err = -ENOMEM;
		goto error_put_f1;
	}

	dma_fence_enable_sw_signaling(f2);

	f3 = __mock_fence(ctx[0], 1);
	if (!f3) {
		err = -ENOMEM;
		goto error_put_f2;
	}

	dma_fence_enable_sw_signaling(f3);

	f4 = dma_fence_unwrap_merge(f1, f2, f3);
	if (!f4) {
		err = -ENOMEM;
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
			pr_err("Unexpected fence!\n");
			err = -EINVAL;
		}
	}

	if (f2 || f3) {
		pr_err("Not all fences seen!\n");
		err = -EINVAL;
	}

	dma_fence_put(f4);
error_put_f3:
	dma_fence_put(f3);
error_put_f2:
	dma_fence_put(f2);
error_put_f1:
	dma_fence_put(f1);
	return err;
}

static int unwrap_merge_order(void *arg)
{
	struct dma_fence *fence, *f1, *f2, *a1, *a2, *c1, *c2;
	struct dma_fence_unwrap iter;
	int err = 0;

	f1 = mock_fence();
	if (!f1)
		return -ENOMEM;

	dma_fence_enable_sw_signaling(f1);

	f2 = mock_fence();
	if (!f2) {
		dma_fence_put(f1);
		return -ENOMEM;
	}

	dma_fence_enable_sw_signaling(f2);

	a1 = mock_array(2, f1, f2);
	if (!a1)
		return -ENOMEM;

	c1 = mock_chain(NULL, dma_fence_get(f1));
	if (!c1)
		goto error_put_a1;

	c2 = mock_chain(c1, dma_fence_get(f2));
	if (!c2)
		goto error_put_a1;

	/*
	 * The fences in the chain are the same as in a1 but in oposite order,
	 * the dma_fence_merge() function should be able to handle that.
	 */
	a2 = dma_fence_unwrap_merge(a1, c2);

	dma_fence_unwrap_for_each(fence, &iter, a2) {
		if (fence == f1) {
			f1 = NULL;
			if (!f2)
				pr_err("Unexpected order!\n");
		} else if (fence == f2) {
			f2 = NULL;
			if (f1)
				pr_err("Unexpected order!\n");
		} else {
			pr_err("Unexpected fence!\n");
			err = -EINVAL;
		}
	}

	if (f1 || f2) {
		pr_err("Not all fences seen!\n");
		err = -EINVAL;
	}

	dma_fence_put(a2);
	return err;

error_put_a1:
	dma_fence_put(a1);
	return -ENOMEM;
}

static int unwrap_merge_complex(void *arg)
{
	struct dma_fence *fence, *f1, *f2, *f3, *f4, *f5;
	struct dma_fence_unwrap iter;
	int err = -ENOMEM;

	f1 = mock_fence();
	if (!f1)
		return -ENOMEM;

	dma_fence_enable_sw_signaling(f1);

	f2 = mock_fence();
	if (!f2)
		goto error_put_f1;

	dma_fence_enable_sw_signaling(f2);

	f3 = dma_fence_unwrap_merge(f1, f2);
	if (!f3)
		goto error_put_f2;

	/* The resulting array has the fences in reverse */
	f4 = mock_array(2, dma_fence_get(f2), dma_fence_get(f1));
	if (!f4)
		goto error_put_f3;

	/* Signaled fences should be filtered, the two arrays merged. */
	f5 = dma_fence_unwrap_merge(f3, f4, dma_fence_get_stub());
	if (!f5)
		goto error_put_f4;

	err = 0;
	dma_fence_unwrap_for_each(fence, &iter, f5) {
		if (fence == f1) {
			dma_fence_put(f1);
			f1 = NULL;
		} else if (fence == f2) {
			dma_fence_put(f2);
			f2 = NULL;
		} else {
			pr_err("Unexpected fence!\n");
			err = -EINVAL;
		}
	}

	if (f1 || f2) {
		pr_err("Not all fences seen!\n");
		err = -EINVAL;
	}

	dma_fence_put(f5);
error_put_f4:
	dma_fence_put(f4);
error_put_f3:
	dma_fence_put(f3);
error_put_f2:
	dma_fence_put(f2);
error_put_f1:
	dma_fence_put(f1);
	return err;
}

static int unwrap_merge_complex_seqno(void *arg)
{
	struct dma_fence *fence, *f1, *f2, *f3, *f4, *f5, *f6, *f7;
	struct dma_fence_unwrap iter;
	int err = -ENOMEM;
	u64 ctx[2];

	ctx[0] = dma_fence_context_alloc(1);
	ctx[1] = dma_fence_context_alloc(1);

	f1 = __mock_fence(ctx[0], 2);
	if (!f1)
		return -ENOMEM;

	dma_fence_enable_sw_signaling(f1);

	f2 = __mock_fence(ctx[1], 1);
	if (!f2)
		goto error_put_f1;

	dma_fence_enable_sw_signaling(f2);

	f3 = __mock_fence(ctx[0], 1);
	if (!f3)
		goto error_put_f2;

	dma_fence_enable_sw_signaling(f3);

	f4 = __mock_fence(ctx[1], 2);
	if (!f4)
		goto error_put_f3;

	dma_fence_enable_sw_signaling(f4);

	f5 = mock_array(2, dma_fence_get(f1), dma_fence_get(f2));
	if (!f5)
		goto error_put_f4;

	f6 = mock_array(2, dma_fence_get(f3), dma_fence_get(f4));
	if (!f6)
		goto error_put_f5;

	f7 = dma_fence_unwrap_merge(f5, f6);
	if (!f7)
		goto error_put_f6;

	err = 0;
	dma_fence_unwrap_for_each(fence, &iter, f7) {
		if (fence == f1 && f4) {
			dma_fence_put(f1);
			f1 = NULL;
		} else if (fence == f4 && !f1) {
			dma_fence_put(f4);
			f4 = NULL;
		} else {
			pr_err("Unexpected fence!\n");
			err = -EINVAL;
		}
	}

	if (f1 || f4) {
		pr_err("Not all fences seen!\n");
		err = -EINVAL;
	}

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
	return err;
}

int dma_fence_unwrap(void)
{
	static const struct subtest tests[] = {
		SUBTEST(sanitycheck),
		SUBTEST(unwrap_array),
		SUBTEST(unwrap_chain),
		SUBTEST(unwrap_chain_array),
		SUBTEST(unwrap_merge),
		SUBTEST(unwrap_merge_duplicate),
		SUBTEST(unwrap_merge_seqno),
		SUBTEST(unwrap_merge_order),
		SUBTEST(unwrap_merge_complex),
		SUBTEST(unwrap_merge_complex_seqno),
	};

	return subtests(tests, NULL);
}
