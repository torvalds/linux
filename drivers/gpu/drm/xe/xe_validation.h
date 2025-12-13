/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */
#ifndef _XE_VALIDATION_H_
#define _XE_VALIDATION_H_

#include <linux/dma-resv.h>
#include <linux/types.h>
#include <linux/rwsem.h>

struct drm_exec;
struct drm_gem_object;
struct drm_gpuvm_exec;
struct xe_device;

#ifdef CONFIG_PROVE_LOCKING
/**
 * xe_validation_lockdep() - Assert that a drm_exec locking transaction can
 * be initialized at this point.
 */
static inline void xe_validation_lockdep(void)
{
	struct ww_acquire_ctx ticket;

	ww_acquire_init(&ticket, &reservation_ww_class);
	ww_acquire_fini(&ticket);
}
#else
static inline void xe_validation_lockdep(void)
{
}
#endif

/*
 * Various values of the drm_exec pointer where we've not (yet)
 * implemented full ww locking.
 *
 * XE_VALIDATION_UNIMPLEMENTED means implementation is pending.
 * A lockdep check is made to assure that a drm_exec locking
 * transaction can actually take place where the macro is
 * used. If this asserts, the exec pointer needs to be assigned
 * higher up in the callchain and passed down.
 *
 * XE_VALIDATION_UNSUPPORTED is for dma-buf code only where
 * the dma-buf layer doesn't support WW locking.
 *
 * XE_VALIDATION_OPT_OUT is for simplification of kunit tests where
 * exhaustive eviction isn't necessary.
 */
#define __XE_VAL_UNIMPLEMENTED -EINVAL
#define XE_VALIDATION_UNIMPLEMENTED (xe_validation_lockdep(),		\
				     (struct drm_exec *)ERR_PTR(__XE_VAL_UNIMPLEMENTED))

#define __XE_VAL_UNSUPPORTED -EOPNOTSUPP
#define XE_VALIDATION_UNSUPPORTED ((struct drm_exec *)ERR_PTR(__XE_VAL_UNSUPPORTED))

#define __XE_VAL_OPT_OUT -ENOMEM
#define XE_VALIDATION_OPT_OUT (xe_validation_lockdep(), \
			       (struct drm_exec *)ERR_PTR(__XE_VAL_OPT_OUT))
#ifdef CONFIG_DRM_XE_DEBUG
void xe_validation_assert_exec(const struct xe_device *xe, const struct drm_exec *exec,
			       const struct drm_gem_object *obj);
#else
#define xe_validation_assert_exec(_xe, _exec, _obj)	\
	do {						\
		(void)_xe; (void)_exec; (void)_obj;	\
	} while (0)
#endif

/**
 * struct xe_validation_device - The domain for exhaustive eviction
 * @lock: The lock used to exclude other processes from allocating graphics memory
 *
 * The struct xe_validation_device represents the domain for which we want to use
 * exhaustive eviction. The @lock is typically grabbed in read mode for allocations
 * but when graphics memory allocation fails, it is retried with the write mode held.
 */
struct xe_validation_device {
	struct rw_semaphore lock;
};

/**
 * struct xe_val_flags - Flags for xe_validation_ctx_init().
 * @exclusive: Start the validation transaction by locking out all other validators.
 * @no_block:  Don't block on initialization.
 * @interruptible: Block interruptible if blocking. Implies initializing the drm_exec
 * context with the DRM_EXEC_INTERRUPTIBLE_WAIT flag.
 * @exec_ignore_duplicates: Initialize the drm_exec context with the
 * DRM_EXEC_IGNORE_DUPLICATES flag.
 */
struct xe_val_flags {
	u32 exclusive :1;
	u32 no_block :1;
	u32 interruptible :1;
	u32 exec_ignore_duplicates :1;
};

/**
 * struct xe_validation_ctx - A struct drm_exec subclass with support for
 * exhaustive eviction
 * @exec: The drm_exec object base class. Note that we use a pointer instead of
 * embedding to avoid diamond inheritance.
 * @val: The exhaustive eviction domain.
 * @val_flags: Copy of the struct xe_val_flags passed to xe_validation_ctx_init.
 * @lock_held: Whether The domain lock is currently held.
 * @lock_held_exclusive: Whether the domain lock is held in exclusive mode.
 * @request_exclusive: Whether to lock exclusively (write mode) the next time
 * the domain lock is locked.
 * @exec_flags: The drm_exec flags used for drm_exec (re-)initialization.
 * @nr: The drm_exec nr parameter used for drm_exec (re-)initializaiton.
 */
struct xe_validation_ctx {
	struct drm_exec *exec;
	struct xe_validation_device *val;
	struct xe_val_flags val_flags;
	bool lock_held;
	bool lock_held_exclusive;
	bool request_exclusive;
	u32 exec_flags;
	unsigned int nr;
};

int xe_validation_ctx_init(struct xe_validation_ctx *ctx, struct xe_validation_device *val,
			   struct drm_exec *exec, const struct xe_val_flags flags);

int xe_validation_exec_lock(struct xe_validation_ctx *ctx, struct drm_gpuvm_exec *vm_exec,
			    struct xe_validation_device *val);

void xe_validation_ctx_fini(struct xe_validation_ctx *ctx);

bool xe_validation_should_retry(struct xe_validation_ctx *ctx, int *ret);

/**
 * xe_validation_retry_on_oom() - Retry on oom in an xe_validaton transaction
 * @_ctx: Pointer to the xe_validation_ctx
 * @_ret: The current error value possibly holding -ENOMEM
 *
 * Use this in way similar to drm_exec_retry_on_contention().
 * If @_ret contains -ENOMEM the tranaction is restarted once in a way that
 * blocks other transactions and allows exhastive eviction. If the transaction
 * was already restarted once, Just return the -ENOMEM. May also set
 * _ret to -EINTR if not retrying and waits are interruptible.
 * May only be used within a drm_exec_until_all_locked() loop.
 */
#define xe_validation_retry_on_oom(_ctx, _ret)				\
	do {								\
		if (xe_validation_should_retry(_ctx, _ret))		\
			goto *__drm_exec_retry_ptr;			\
	} while (0)

/**
 * xe_validation_device_init - Initialize a struct xe_validation_device
 * @val: The xe_validation_device to init.
 */
static inline void
xe_validation_device_init(struct xe_validation_device *val)
{
	init_rwsem(&val->lock);
}

/*
 * Make guard() and scoped_guard() work with xe_validation_ctx
 * so that we can exit transactions without caring about the
 * cleanup.
 */
DEFINE_CLASS(xe_validation, struct xe_validation_ctx *,
	     if (_T) xe_validation_ctx_fini(_T);,
	     ({*_ret = xe_validation_ctx_init(_ctx, _val, _exec, _flags);
	       *_ret ? NULL : _ctx; }),
	     struct xe_validation_ctx *_ctx, struct xe_validation_device *_val,
	     struct drm_exec *_exec, const struct xe_val_flags _flags, int *_ret);
static inline void *class_xe_validation_lock_ptr(class_xe_validation_t *_T)
{return *_T; }
#define class_xe_validation_is_conditional true

/**
 * xe_validation_guard() - An auto-cleanup xe_validation_ctx transaction
 * @_ctx: The xe_validation_ctx.
 * @_val: The xe_validation_device.
 * @_exec: The struct drm_exec object
 * @_flags: Flags for the xe_validation_ctx initialization.
 * @_ret: Return in / out parameter. May be set by this macro. Typicall 0 when called.
 *
 * This macro is will initiate a drm_exec transaction with additional support for
 * exhaustive eviction.
 */
#define xe_validation_guard(_ctx, _val, _exec, _flags, _ret)		\
	scoped_guard(xe_validation, _ctx, _val, _exec, _flags, &_ret) \
	drm_exec_until_all_locked(_exec)

#endif
