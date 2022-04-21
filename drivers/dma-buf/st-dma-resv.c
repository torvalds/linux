/* SPDX-License-Identifier: MIT */

/*
* Copyright © 2019 Intel Corporation
* Copyright © 2021 Advanced Micro Devices, Inc.
*/

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/dma-resv.h>

#include "selftest.h"

static struct spinlock fence_lock;

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

	f = kmalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return NULL;

	dma_fence_init(f, &fence_ops, &fence_lock, 0, 0);
	return f;
}

static int sanitycheck(void *arg)
{
	struct dma_resv resv;
	struct dma_fence *f;
	int r;

	f = alloc_fence();
	if (!f)
		return -ENOMEM;

	dma_fence_signal(f);
	dma_fence_put(f);

	dma_resv_init(&resv);
	r = dma_resv_lock(&resv, NULL);
	if (r)
		pr_err("Resv locking failed\n");
	else
		dma_resv_unlock(&resv);
	dma_resv_fini(&resv);
	return r;
}

static int test_signaling(void *arg, bool shared)
{
	struct dma_resv resv;
	struct dma_fence *f;
	int r;

	f = alloc_fence();
	if (!f)
		return -ENOMEM;

	dma_resv_init(&resv);
	r = dma_resv_lock(&resv, NULL);
	if (r) {
		pr_err("Resv locking failed\n");
		goto err_free;
	}

	if (shared) {
		r = dma_resv_reserve_shared(&resv, 1);
		if (r) {
			pr_err("Resv shared slot allocation failed\n");
			goto err_unlock;
		}

		dma_resv_add_shared_fence(&resv, f);
	} else {
		dma_resv_add_excl_fence(&resv, f);
	}

	if (dma_resv_test_signaled(&resv, shared)) {
		pr_err("Resv unexpectedly signaled\n");
		r = -EINVAL;
		goto err_unlock;
	}
	dma_fence_signal(f);
	if (!dma_resv_test_signaled(&resv, shared)) {
		pr_err("Resv not reporting signaled\n");
		r = -EINVAL;
		goto err_unlock;
	}
err_unlock:
	dma_resv_unlock(&resv);
err_free:
	dma_resv_fini(&resv);
	dma_fence_put(f);
	return r;
}

static int test_excl_signaling(void *arg)
{
	return test_signaling(arg, false);
}

static int test_shared_signaling(void *arg)
{
	return test_signaling(arg, true);
}

static int test_for_each(void *arg, bool shared)
{
	struct dma_resv_iter cursor;
	struct dma_fence *f, *fence;
	struct dma_resv resv;
	int r;

	f = alloc_fence();
	if (!f)
		return -ENOMEM;

	dma_resv_init(&resv);
	r = dma_resv_lock(&resv, NULL);
	if (r) {
		pr_err("Resv locking failed\n");
		goto err_free;
	}

	if (shared) {
		r = dma_resv_reserve_shared(&resv, 1);
		if (r) {
			pr_err("Resv shared slot allocation failed\n");
			goto err_unlock;
		}

		dma_resv_add_shared_fence(&resv, f);
	} else {
		dma_resv_add_excl_fence(&resv, f);
	}

	r = -ENOENT;
	dma_resv_for_each_fence(&cursor, &resv, shared, fence) {
		if (!r) {
			pr_err("More than one fence found\n");
			r = -EINVAL;
			goto err_unlock;
		}
		if (f != fence) {
			pr_err("Unexpected fence\n");
			r = -EINVAL;
			goto err_unlock;
		}
		if (dma_resv_iter_is_exclusive(&cursor) != !shared) {
			pr_err("Unexpected fence usage\n");
			r = -EINVAL;
			goto err_unlock;
		}
		r = 0;
	}
	if (r) {
		pr_err("No fence found\n");
		goto err_unlock;
	}
	dma_fence_signal(f);
err_unlock:
	dma_resv_unlock(&resv);
err_free:
	dma_resv_fini(&resv);
	dma_fence_put(f);
	return r;
}

static int test_excl_for_each(void *arg)
{
	return test_for_each(arg, false);
}

static int test_shared_for_each(void *arg)
{
	return test_for_each(arg, true);
}

static int test_for_each_unlocked(void *arg, bool shared)
{
	struct dma_resv_iter cursor;
	struct dma_fence *f, *fence;
	struct dma_resv resv;
	int r;

	f = alloc_fence();
	if (!f)
		return -ENOMEM;

	dma_resv_init(&resv);
	r = dma_resv_lock(&resv, NULL);
	if (r) {
		pr_err("Resv locking failed\n");
		goto err_free;
	}

	if (shared) {
		r = dma_resv_reserve_shared(&resv, 1);
		if (r) {
			pr_err("Resv shared slot allocation failed\n");
			dma_resv_unlock(&resv);
			goto err_free;
		}

		dma_resv_add_shared_fence(&resv, f);
	} else {
		dma_resv_add_excl_fence(&resv, f);
	}
	dma_resv_unlock(&resv);

	r = -ENOENT;
	dma_resv_iter_begin(&cursor, &resv, shared);
	dma_resv_for_each_fence_unlocked(&cursor, fence) {
		if (!r) {
			pr_err("More than one fence found\n");
			r = -EINVAL;
			goto err_iter_end;
		}
		if (!dma_resv_iter_is_restarted(&cursor)) {
			pr_err("No restart flag\n");
			goto err_iter_end;
		}
		if (f != fence) {
			pr_err("Unexpected fence\n");
			r = -EINVAL;
			goto err_iter_end;
		}
		if (dma_resv_iter_is_exclusive(&cursor) != !shared) {
			pr_err("Unexpected fence usage\n");
			r = -EINVAL;
			goto err_iter_end;
		}

		/* We use r as state here */
		if (r == -ENOENT) {
			r = -EINVAL;
			/* That should trigger an restart */
			cursor.seq--;
		} else if (r == -EINVAL) {
			r = 0;
		}
	}
	if (r)
		pr_err("No fence found\n");
err_iter_end:
	dma_resv_iter_end(&cursor);
	dma_fence_signal(f);
err_free:
	dma_resv_fini(&resv);
	dma_fence_put(f);
	return r;
}

static int test_excl_for_each_unlocked(void *arg)
{
	return test_for_each_unlocked(arg, false);
}

static int test_shared_for_each_unlocked(void *arg)
{
	return test_for_each_unlocked(arg, true);
}

static int test_get_fences(void *arg, bool shared)
{
	struct dma_fence *f, **fences = NULL;
	struct dma_resv resv;
	int r, i;

	f = alloc_fence();
	if (!f)
		return -ENOMEM;

	dma_resv_init(&resv);
	r = dma_resv_lock(&resv, NULL);
	if (r) {
		pr_err("Resv locking failed\n");
		goto err_resv;
	}

	if (shared) {
		r = dma_resv_reserve_shared(&resv, 1);
		if (r) {
			pr_err("Resv shared slot allocation failed\n");
			dma_resv_unlock(&resv);
			goto err_resv;
		}

		dma_resv_add_shared_fence(&resv, f);
	} else {
		dma_resv_add_excl_fence(&resv, f);
	}
	dma_resv_unlock(&resv);

	r = dma_resv_get_fences(&resv, shared, &i, &fences);
	if (r) {
		pr_err("get_fences failed\n");
		goto err_free;
	}

	if (i != 1 || fences[0] != f) {
		pr_err("get_fences returned unexpected fence\n");
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
	return r;
}

static int test_excl_get_fences(void *arg)
{
	return test_get_fences(arg, false);
}

static int test_shared_get_fences(void *arg)
{
	return test_get_fences(arg, true);
}

int dma_resv(void)
{
	static const struct subtest tests[] = {
		SUBTEST(sanitycheck),
		SUBTEST(test_excl_signaling),
		SUBTEST(test_shared_signaling),
		SUBTEST(test_excl_for_each),
		SUBTEST(test_shared_for_each),
		SUBTEST(test_excl_for_each_unlocked),
		SUBTEST(test_shared_for_each_unlocked),
		SUBTEST(test_excl_get_fences),
		SUBTEST(test_shared_get_fences),
	};

	spin_lock_init(&fence_lock);
	return subtests(tests, NULL);
}
