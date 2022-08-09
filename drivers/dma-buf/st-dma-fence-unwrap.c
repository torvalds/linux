// SPDX-License-Identifier: MIT

/*
 * Copyright (C) 2022 Advanced Micro Devices, Inc.
 */

#include <linux/dma-fence-unwrap.h>
#if 0
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/random.h>
#endif

#include "selftest.h"

#define CHAIN_SZ (4 << 10)

static inline struct mock_fence {
	struct dma_fence base;
	spinlock_t lock;
} *to_mock_fence(struct dma_fence *f) {
	return container_of(f, struct mock_fence, base);
}

static const char *mock_name(struct dma_fence *f)
{
	return "mock";
}

static const struct dma_fence_ops mock_ops = {
	.get_driver_name = mock_name,
	.get_timeline_name = mock_name,
};

static struct dma_fence *mock_fence(void)
{
	struct mock_fence *f;

	f = kmalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return NULL;

	spin_lock_init(&f->lock);
	dma_fence_init(&f->base, &mock_ops, &f->lock, 0, 0);

	return &f->base;
}

static struct dma_fence *mock_array(unsigned int num_fences, ...)
{
	struct dma_fence_array *array;
	struct dma_fence **fences;
	va_list valist;
	int i;

	fences = kcalloc(num_fences, sizeof(*fences), GFP_KERNEL);
	if (!fences)
		return NULL;

	va_start(valist, num_fences);
	for (i = 0; i < num_fences; ++i)
		fences[i] = va_arg(valist, typeof(*fences));
	va_end(valist);

	array = dma_fence_array_create(num_fences, fences,
				       dma_fence_context_alloc(1),
				       1, false);
	if (!array)
		goto cleanup;
	return &array->base;

cleanup:
	for (i = 0; i < num_fences; ++i)
		dma_fence_put(fences[i]);
	kfree(fences);
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

	array = mock_array(1, f);
	if (!array)
		return -ENOMEM;

	chain = mock_chain(NULL, array);
	if (!chain)
		return -ENOMEM;

	dma_fence_signal(f);
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

	f2 = mock_fence();
	if (!f2) {
		dma_fence_put(f1);
		return -ENOMEM;
	}

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

	dma_fence_signal(f1);
	dma_fence_signal(f2);
	dma_fence_put(array);
	return 0;
}

static int unwrap_chain(void *arg)
{
	struct dma_fence *fence, *f1, *f2, *chain;
	struct dma_fence_unwrap iter;
	int err = 0;

	f1 = mock_fence();
	if (!f1)
		return -ENOMEM;

	f2 = mock_fence();
	if (!f2) {
		dma_fence_put(f1);
		return -ENOMEM;
	}

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

	dma_fence_signal(f1);
	dma_fence_signal(f2);
	dma_fence_put(chain);
	return 0;
}

static int unwrap_chain_array(void *arg)
{
	struct dma_fence *fence, *f1, *f2, *array, *chain;
	struct dma_fence_unwrap iter;
	int err = 0;

	f1 = mock_fence();
	if (!f1)
		return -ENOMEM;

	f2 = mock_fence();
	if (!f2) {
		dma_fence_put(f1);
		return -ENOMEM;
	}

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

	dma_fence_signal(f1);
	dma_fence_signal(f2);
	dma_fence_put(chain);
	return 0;
}

int dma_fence_unwrap(void)
{
	static const struct subtest tests[] = {
		SUBTEST(sanitycheck),
		SUBTEST(unwrap_array),
		SUBTEST(unwrap_chain),
		SUBTEST(unwrap_chain_array),
	};

	return subtests(tests, NULL);
}
