// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */
#include "xe_bo.h"
#include <drm/drm_exec.h>
#include <drm/drm_gem.h>
#include <drm/drm_gpuvm.h>

#include "xe_assert.h"
#include "xe_validation.h"

#ifdef CONFIG_DRM_XE_DEBUG
/**
 * xe_validation_assert_exec() - Assert that the drm_exec pointer is suitable
 * for validation.
 * @xe: Pointer to the xe device.
 * @exec: The drm_exec pointer to check.
 * @obj: Pointer to the object subject to validation.
 *
 * NULL exec pointers are not allowed.
 * For XE_VALIDATION_UNIMPLEMENTED, no checking.
 * For XE_VLIDATION_OPT_OUT, check that the caller is a kunit test
 * For XE_VALIDATION_UNSUPPORTED, check that the object subject to
 * validation is a dma-buf, for which support for ww locking is
 * not in place in the dma-buf layer.
 */
void xe_validation_assert_exec(const struct xe_device *xe,
			       const struct drm_exec *exec,
			       const struct drm_gem_object *obj)
{
	xe_assert(xe, exec);
	if (IS_ERR(exec)) {
		switch (PTR_ERR(exec)) {
		case __XE_VAL_UNIMPLEMENTED:
			break;
		case __XE_VAL_UNSUPPORTED:
			xe_assert(xe, !!obj->dma_buf);
			break;
#if IS_ENABLED(CONFIG_KUNIT)
		case __XE_VAL_OPT_OUT:
			xe_assert(xe, current->kunit_test);
			break;
#endif
		default:
			xe_assert(xe, false);
		}
	}
}
#endif

static int xe_validation_lock(struct xe_validation_ctx *ctx)
{
	struct xe_validation_device *val = ctx->val;
	int ret = 0;

	if (ctx->val_flags.interruptible) {
		if (ctx->request_exclusive)
			ret = down_write_killable(&val->lock);
		else
			ret = down_read_interruptible(&val->lock);
	} else {
		if (ctx->request_exclusive)
			down_write(&val->lock);
		else
			down_read(&val->lock);
	}

	if (!ret) {
		ctx->lock_held = true;
		ctx->lock_held_exclusive = ctx->request_exclusive;
	}

	return ret;
}

static int xe_validation_trylock(struct xe_validation_ctx *ctx)
{
	struct xe_validation_device *val = ctx->val;
	bool locked;

	if (ctx->request_exclusive)
		locked = down_write_trylock(&val->lock);
	else
		locked = down_read_trylock(&val->lock);

	if (locked) {
		ctx->lock_held = true;
		ctx->lock_held_exclusive = ctx->request_exclusive;
	}

	return locked ? 0 : -EWOULDBLOCK;
}

static void xe_validation_unlock(struct xe_validation_ctx *ctx)
{
	if (!ctx->lock_held)
		return;

	if (ctx->lock_held_exclusive)
		up_write(&ctx->val->lock);
	else
		up_read(&ctx->val->lock);

	ctx->lock_held = false;
}

/**
 * xe_validation_ctx_init() - Initialize an xe_validation_ctx
 * @ctx: The xe_validation_ctx to initialize.
 * @val: The xe_validation_device representing the validation domain.
 * @exec: The struct drm_exec to use for the transaction. May be NULL.
 * @flags: The flags to use for initialization.
 *
 * Initialize and lock a an xe_validation transaction using the validation domain
 * represented by @val. Also initialize the drm_exec object forwarding parts of
 * @flags to the drm_exec initialization. The @flags.exclusive flag should
 * typically be set to false to avoid locking out other validators from the
 * domain until an OOM is hit. For testing- or final attempt purposes it can,
 * however, be set to true.
 *
 * Return: %0 on success, %-EINTR if interruptible initial locking failed with a
 * signal pending. If @flags.no_block is set to true, a failed trylock
 * returns %-EWOULDBLOCK.
 */
int xe_validation_ctx_init(struct xe_validation_ctx *ctx, struct xe_validation_device *val,
			   struct drm_exec *exec, const struct xe_val_flags flags)
{
	int ret;

	ctx->exec = exec;
	ctx->val = val;
	ctx->lock_held = false;
	ctx->lock_held_exclusive = false;
	ctx->request_exclusive = flags.exclusive;
	ctx->val_flags = flags;
	ctx->exec_flags = 0;
	ctx->nr = 0;

	if (flags.no_block)
		ret = xe_validation_trylock(ctx);
	else
		ret = xe_validation_lock(ctx);
	if (ret)
		return ret;

	if (exec) {
		if (flags.interruptible)
			ctx->exec_flags |= DRM_EXEC_INTERRUPTIBLE_WAIT;
		if (flags.exec_ignore_duplicates)
			ctx->exec_flags |= DRM_EXEC_IGNORE_DUPLICATES;
		drm_exec_init(exec, ctx->exec_flags, ctx->nr);
	}

	return 0;
}

#ifdef CONFIG_DEBUG_WW_MUTEX_SLOWPATH
/*
 * This abuses both drm_exec and ww_mutex internals and should be
 * replaced by checking for -EDEADLK when we can make TTM
 * stop converting -EDEADLK to -ENOMEM.
 * An alternative is to not have exhaustive eviction with
 * CONFIG_DEBUG_WW_MUTEX_SLOWPATH until that happens.
 */
static bool xe_validation_contention_injected(struct drm_exec *exec)
{
	return !!exec->ticket.contending_lock;
}

#else

static bool xe_validation_contention_injected(struct drm_exec *exec)
{
	return false;
}

#endif

static bool __xe_validation_should_retry(struct xe_validation_ctx *ctx, int ret)
{
	if (ret == -ENOMEM &&
	    ((ctx->request_exclusive &&
	      xe_validation_contention_injected(ctx->exec)) ||
	     !ctx->request_exclusive)) {
		ctx->request_exclusive = true;
		return true;
	}

	return false;
}

/**
 * xe_validation_exec_lock() - Perform drm_gpuvm_exec_lock within a validation
 * transaction.
 * @ctx: An uninitialized xe_validation_ctx.
 * @vm_exec: An initialized struct vm_exec.
 * @val: The validation domain.
 *
 * The drm_gpuvm_exec_lock() function internally initializes its drm_exec
 * transaction and therefore doesn't lend itself very well to be using
 * xe_validation_ctx_init(). Provide a helper that takes an uninitialized
 * xe_validation_ctx and calls drm_gpuvm_exec_lock() with OOM retry.
 *
 * Return: %0 on success, negative error code on failure.
 */
int xe_validation_exec_lock(struct xe_validation_ctx *ctx,
			    struct drm_gpuvm_exec *vm_exec,
			    struct xe_validation_device *val)
{
	int ret;

	memset(ctx, 0, sizeof(*ctx));
	ctx->exec = &vm_exec->exec;
	ctx->exec_flags = vm_exec->flags;
	ctx->val = val;
	if (ctx->exec_flags & DRM_EXEC_INTERRUPTIBLE_WAIT)
		ctx->val_flags.interruptible = 1;
	if (ctx->exec_flags & DRM_EXEC_IGNORE_DUPLICATES)
		ctx->val_flags.exec_ignore_duplicates = 1;
retry:
	ret = xe_validation_lock(ctx);
	if (ret)
		return ret;

	ret = drm_gpuvm_exec_lock(vm_exec);
	if (ret) {
		xe_validation_unlock(ctx);
		if (__xe_validation_should_retry(ctx, ret))
			goto retry;
	}

	return ret;
}

/**
 * xe_validation_ctx_fini() - Finalize a validation transaction
 * @ctx: The Validation transaction to finalize.
 *
 * Finalize a validation transaction and its related drm_exec transaction.
 */
void xe_validation_ctx_fini(struct xe_validation_ctx *ctx)
{
	if (ctx->exec)
		drm_exec_fini(ctx->exec);
	xe_validation_unlock(ctx);
}

/**
 * xe_validation_should_retry() - Determine if a validation transaction should retry
 * @ctx: The validation transaction.
 * @ret: Pointer to a return value variable.
 *
 * Determines whether a validation transaction should retry based on the
 * internal transaction state and the return value pointed to by @ret.
 * If a validation should be retried, the transaction is prepared for that,
 * and the validation locked might be re-locked in exclusive mode, and *@ret
 * is set to %0. If the re-locking errors, typically due to interruptible
 * locking with signal pending, *@ret is instead set to -EINTR and the
 * function returns %false.
 *
 * Return: %true if validation should be retried, %false otherwise.
 */
bool xe_validation_should_retry(struct xe_validation_ctx *ctx, int *ret)
{
	if (__xe_validation_should_retry(ctx, *ret)) {
		drm_exec_fini(ctx->exec);
		*ret = 0;
		if (ctx->request_exclusive != ctx->lock_held_exclusive) {
			xe_validation_unlock(ctx);
			*ret = xe_validation_lock(ctx);
		}
		drm_exec_init(ctx->exec, ctx->exec_flags, ctx->nr);
		return !*ret;
	}

	return false;
}
