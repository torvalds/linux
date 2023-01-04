// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <linux/dma-fence.h>
#include <linux/slab.h>

#include <drm/ttm/ttm_bo.h>

#include "i915_deps.h"

/**
 * DOC: Set of utilities to dynamically collect dependencies into a
 * structure which is fed into the GT migration code.
 *
 * Once we can do async unbinding, this is also needed to coalesce
 * the migration fence with the unbind fences if these are coalesced
 * post-migration.
 *
 * While collecting the individual dependencies, we store the refcounted
 * struct dma_fence pointers in a realloc-managed pointer array, since
 * that can be easily fed into a dma_fence_array. Other options are
 * available, like for example an xarray for similarity with drm/sched.
 * Can be changed easily if needed.
 *
 * A struct i915_deps need to be initialized using i915_deps_init().
 * If i915_deps_add_dependency() or i915_deps_add_resv() return an
 * error code they will internally call i915_deps_fini(), which frees
 * all internal references and allocations.
 */

/* Min number of fence pointers in the array when an allocation occurs. */
#define I915_DEPS_MIN_ALLOC_CHUNK 8U

static void i915_deps_reset_fences(struct i915_deps *deps)
{
	if (deps->fences != &deps->single)
		kfree(deps->fences);
	deps->num_deps = 0;
	deps->fences_size = 1;
	deps->fences = &deps->single;
}

/**
 * i915_deps_init - Initialize an i915_deps structure
 * @deps: Pointer to the i915_deps structure to initialize.
 * @gfp: The allocation mode for subsequenst allocations.
 */
void i915_deps_init(struct i915_deps *deps, gfp_t gfp)
{
	deps->fences = NULL;
	deps->gfp = gfp;
	i915_deps_reset_fences(deps);
}

/**
 * i915_deps_fini - Finalize an i915_deps structure
 * @deps: Pointer to the i915_deps structure to finalize.
 *
 * This function drops all fence references taken, conditionally frees and
 * then resets the fences array.
 */
void i915_deps_fini(struct i915_deps *deps)
{
	unsigned int i;

	for (i = 0; i < deps->num_deps; ++i)
		dma_fence_put(deps->fences[i]);

	if (deps->fences != &deps->single)
		kfree(deps->fences);
}

static int i915_deps_grow(struct i915_deps *deps, struct dma_fence *fence,
			  const struct ttm_operation_ctx *ctx)
{
	int ret;

	if (deps->num_deps >= deps->fences_size) {
		unsigned int new_size = 2 * deps->fences_size;
		struct dma_fence **new_fences;

		new_size = max(new_size, I915_DEPS_MIN_ALLOC_CHUNK);
		new_fences = kmalloc_array(new_size, sizeof(*new_fences), deps->gfp);
		if (!new_fences)
			goto sync;

		memcpy(new_fences, deps->fences,
		       deps->fences_size * sizeof(*new_fences));
		swap(new_fences, deps->fences);
		if (new_fences != &deps->single)
			kfree(new_fences);
		deps->fences_size = new_size;
	}
	deps->fences[deps->num_deps++] = dma_fence_get(fence);
	return 0;

sync:
	if (ctx->no_wait_gpu && !dma_fence_is_signaled(fence)) {
		ret = -EBUSY;
		goto unref;
	}

	ret = dma_fence_wait(fence, ctx->interruptible);
	if (ret)
		goto unref;

	ret = fence->error;
	if (ret)
		goto unref;

	return 0;

unref:
	i915_deps_fini(deps);
	return ret;
}

/**
 * i915_deps_sync - Wait for all the fences in the dependency collection
 * @deps: Pointer to the i915_deps structure the fences of which to wait for.
 * @ctx: Pointer to a struct ttm_operation_ctx indicating how the waits
 * should be performed.
 *
 * This function waits for fences in the dependency collection. If it
 * encounters an error during the wait or a fence error, the wait for
 * further fences is aborted and the error returned.
 *
 * Return: Zero if successful, Negative error code on error.
 */
int i915_deps_sync(const struct i915_deps *deps, const struct ttm_operation_ctx *ctx)
{
	struct dma_fence **fences = deps->fences;
	unsigned int i;
	int ret = 0;

	for (i = 0; i < deps->num_deps; ++i, ++fences) {
		if (ctx->no_wait_gpu && !dma_fence_is_signaled(*fences)) {
			ret = -EBUSY;
			break;
		}

		ret = dma_fence_wait(*fences, ctx->interruptible);
		if (!ret)
			ret = (*fences)->error;
		if (ret)
			break;
	}

	return ret;
}

/**
 * i915_deps_add_dependency - Add a fence to the dependency collection
 * @deps: Pointer to the i915_deps structure a fence is to be added to.
 * @fence: The fence to add.
 * @ctx: Pointer to a struct ttm_operation_ctx indicating how waits are to
 * be performed if waiting.
 *
 * Adds a fence to the dependency collection, and takes a reference on it.
 * If the fence context is not zero and there was a later fence from the
 * same fence context already added, then the fence is not added to the
 * dependency collection. If the fence context is not zero and there was
 * an earlier fence already added, then the fence will replace the older
 * fence from the same context and the reference on the earlier fence will
 * be dropped.
 * If there is a failure to allocate memory to accommodate the new fence to
 * be added, the new fence will instead be waited for and an error may
 * be returned; depending on the value of @ctx, or if there was a fence
 * error. If an error was returned, the dependency collection will be
 * finalized and all fence reference dropped.
 *
 * Return: 0 if success. Negative error code on error.
 */
int i915_deps_add_dependency(struct i915_deps *deps,
			     struct dma_fence *fence,
			     const struct ttm_operation_ctx *ctx)
{
	unsigned int i;
	int ret;

	if (!fence)
		return 0;

	if (dma_fence_is_signaled(fence)) {
		ret = fence->error;
		if (ret)
			i915_deps_fini(deps);
		return ret;
	}

	for (i = 0; i < deps->num_deps; ++i) {
		struct dma_fence *entry = deps->fences[i];

		if (!entry->context || entry->context != fence->context)
			continue;

		if (dma_fence_is_later(fence, entry)) {
			dma_fence_put(entry);
			deps->fences[i] = dma_fence_get(fence);
		}

		return 0;
	}

	return i915_deps_grow(deps, fence, ctx);
}

/**
 * i915_deps_add_resv - Add the fences of a reservation object to a dependency
 * collection.
 * @deps: Pointer to the i915_deps structure a fence is to be added to.
 * @resv: The reservation object, then fences of which to add.
 * @ctx: Pointer to a struct ttm_operation_ctx indicating how waits are to
 * be performed if waiting.
 *
 * Calls i915_deps_add_depencency() on the indicated fences of @resv.
 *
 * Return: Zero on success. Negative error code on error.
 */
int i915_deps_add_resv(struct i915_deps *deps, struct dma_resv *resv,
		       const struct ttm_operation_ctx *ctx)
{
	struct dma_resv_iter iter;
	struct dma_fence *fence;

	dma_resv_assert_held(resv);
	dma_resv_for_each_fence(&iter, resv, dma_resv_usage_rw(true), fence) {
		int ret = i915_deps_add_dependency(deps, fence, ctx);

		if (ret)
			return ret;
	}

	return 0;
}
