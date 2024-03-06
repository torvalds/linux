// SPDX-License-Identifier: GPL-2.0-only
/*
 * dma-fence-util: misc functions for dma_fence objects
 *
 * Copyright (C) 2022 Advanced Micro Devices, Inc.
 * Authors:
 *	Christian König <christian.koenig@amd.com>
 */

#include <linux/dma-fence.h>
#include <linux/dma-fence-array.h>
#include <linux/dma-fence-chain.h>
#include <linux/dma-fence-unwrap.h>
#include <linux/slab.h>

/* Internal helper to start new array iteration, don't use directly */
static struct dma_fence *
__dma_fence_unwrap_array(struct dma_fence_unwrap *cursor)
{
	cursor->array = dma_fence_chain_contained(cursor->chain);
	cursor->index = 0;
	return dma_fence_array_first(cursor->array);
}

/**
 * dma_fence_unwrap_first - return the first fence from fence containers
 * @head: the entrypoint into the containers
 * @cursor: current position inside the containers
 *
 * Unwraps potential dma_fence_chain/dma_fence_array containers and return the
 * first fence.
 */
struct dma_fence *dma_fence_unwrap_first(struct dma_fence *head,
					 struct dma_fence_unwrap *cursor)
{
	cursor->chain = dma_fence_get(head);
	return __dma_fence_unwrap_array(cursor);
}
EXPORT_SYMBOL_GPL(dma_fence_unwrap_first);

/**
 * dma_fence_unwrap_next - return the next fence from a fence containers
 * @cursor: current position inside the containers
 *
 * Continue unwrapping the dma_fence_chain/dma_fence_array containers and return
 * the next fence from them.
 */
struct dma_fence *dma_fence_unwrap_next(struct dma_fence_unwrap *cursor)
{
	struct dma_fence *tmp;

	++cursor->index;
	tmp = dma_fence_array_next(cursor->array, cursor->index);
	if (tmp)
		return tmp;

	cursor->chain = dma_fence_chain_walk(cursor->chain);
	return __dma_fence_unwrap_array(cursor);
}
EXPORT_SYMBOL_GPL(dma_fence_unwrap_next);

/* Implementation for the dma_fence_merge() marco, don't use directly */
struct dma_fence *__dma_fence_unwrap_merge(unsigned int num_fences,
					   struct dma_fence **fences,
					   struct dma_fence_unwrap *iter)
{
	struct dma_fence_array *result;
	struct dma_fence *tmp, **array;
	ktime_t timestamp;
	unsigned int i;
	size_t count;

	count = 0;
	timestamp = ns_to_ktime(0);
	for (i = 0; i < num_fences; ++i) {
		dma_fence_unwrap_for_each(tmp, &iter[i], fences[i]) {
			if (!dma_fence_is_signaled(tmp)) {
				++count;
			} else {
				ktime_t t = dma_fence_timestamp(tmp);

				if (ktime_after(t, timestamp))
					timestamp = t;
			}
		}
	}

	/*
	 * If we couldn't find a pending fence just return a private signaled
	 * fence with the timestamp of the last signaled one.
	 */
	if (count == 0)
		return dma_fence_allocate_private_stub(timestamp);

	array = kmalloc_array(count, sizeof(*array), GFP_KERNEL);
	if (!array)
		return NULL;

	/*
	 * This trashes the input fence array and uses it as position for the
	 * following merge loop. This works because the dma_fence_merge()
	 * wrapper macro is creating this temporary array on the stack together
	 * with the iterators.
	 */
	for (i = 0; i < num_fences; ++i)
		fences[i] = dma_fence_unwrap_first(fences[i], &iter[i]);

	count = 0;
	do {
		unsigned int sel;

restart:
		tmp = NULL;
		for (i = 0; i < num_fences; ++i) {
			struct dma_fence *next;

			while (fences[i] && dma_fence_is_signaled(fences[i]))
				fences[i] = dma_fence_unwrap_next(&iter[i]);

			next = fences[i];
			if (!next)
				continue;

			/*
			 * We can't guarantee that inpute fences are ordered by
			 * context, but it is still quite likely when this
			 * function is used multiple times. So attempt to order
			 * the fences by context as we pass over them and merge
			 * fences with the same context.
			 */
			if (!tmp || tmp->context > next->context) {
				tmp = next;
				sel = i;

			} else if (tmp->context < next->context) {
				continue;

			} else if (dma_fence_is_later(tmp, next)) {
				fences[i] = dma_fence_unwrap_next(&iter[i]);
				goto restart;
			} else {
				fences[sel] = dma_fence_unwrap_next(&iter[sel]);
				goto restart;
			}
		}

		if (tmp) {
			array[count++] = dma_fence_get(tmp);
			fences[sel] = dma_fence_unwrap_next(&iter[sel]);
		}
	} while (tmp);

	if (count == 0) {
		tmp = dma_fence_allocate_private_stub(ktime_get());
		goto return_tmp;
	}

	if (count == 1) {
		tmp = array[0];
		goto return_tmp;
	}

	result = dma_fence_array_create(count, array,
					dma_fence_context_alloc(1),
					1, false);
	if (!result) {
		tmp = NULL;
		goto return_tmp;
	}
	return &result->base;

return_tmp:
	kfree(array);
	return tmp;
}
EXPORT_SYMBOL_GPL(__dma_fence_unwrap_merge);
