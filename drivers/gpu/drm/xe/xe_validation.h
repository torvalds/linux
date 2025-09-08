/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */
#ifndef _XE_VALIDATION_H_
#define _XE_VALIDATION_H_

#include <linux/dma-resv.h>
#include <linux/types.h>

struct drm_exec;
struct drm_gem_object;
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

#endif
