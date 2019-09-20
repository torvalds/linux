// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 *
 */

#include "i915_drv.h"
#include "intel_display_types.h"

#define DSB_BUF_SIZE    (2 * PAGE_SIZE)

struct intel_dsb *
intel_dsb_get(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *i915 = to_i915(dev);
	struct intel_dsb *dsb = &crtc->dsb;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	intel_wakeref_t wakeref;

	if (!HAS_DSB(i915))
		return dsb;

	if (atomic_add_return(1, &dsb->refcount) != 1)
		return dsb;

	dsb->id = DSB1;
	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	obj = i915_gem_object_create_internal(i915, DSB_BUF_SIZE);
	if (IS_ERR(obj)) {
		DRM_ERROR("Gem object creation failed\n");
		goto err;
	}

	mutex_lock(&i915->drm.struct_mutex);
	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0, PIN_MAPPABLE);
	mutex_unlock(&i915->drm.struct_mutex);
	if (IS_ERR(vma)) {
		DRM_ERROR("Vma creation failed\n");
		i915_gem_object_put(obj);
		atomic_dec(&dsb->refcount);
		goto err;
	}

	dsb->cmd_buf = i915_gem_object_pin_map(vma->obj, I915_MAP_WC);
	if (IS_ERR(dsb->cmd_buf)) {
		DRM_ERROR("Command buffer creation failed\n");
		i915_vma_unpin_and_release(&vma, 0);
		dsb->cmd_buf = NULL;
		atomic_dec(&dsb->refcount);
		goto err;
	}
	dsb->vma = vma;

err:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	return dsb;
}

void intel_dsb_put(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = container_of(dsb, typeof(*crtc), dsb);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	if (!HAS_DSB(i915))
		return;

	if (WARN_ON(atomic_read(&dsb->refcount) == 0))
		return;

	if (atomic_dec_and_test(&dsb->refcount)) {
		mutex_lock(&i915->drm.struct_mutex);
		i915_gem_object_unpin_map(dsb->vma->obj);
		i915_vma_unpin_and_release(&dsb->vma, 0);
		mutex_unlock(&i915->drm.struct_mutex);
		dsb->cmd_buf = NULL;
	}
}
