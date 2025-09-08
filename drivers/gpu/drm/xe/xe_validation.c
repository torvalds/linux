// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */
#include "xe_bo.h"
#include <drm/drm_exec.h>
#include <drm/drm_gem.h>

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
