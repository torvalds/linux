// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <drm/drm_print.h>
#include <drm/intel/display_parent_interface.h>

#include "gem/i915_gem_domain.h"
#include "gem/i915_gem_object.h"

#include "i915_fb_pin.h"
#include "i915_dpt.h"
#include "i915_drv.h"
#include "i915_vma.h"

static struct i915_vma *
intel_fb_pin_to_dpt(struct drm_gem_object *_obj, struct intel_dpt *dpt,
		    const struct intel_fb_pin_params *pin_params)
{
	struct drm_i915_private *i915 = to_i915(_obj->dev);
	struct drm_i915_gem_object *obj = to_intel_bo(_obj);
	struct i915_address_space *vm = i915_dpt_to_vm(dpt);
	struct i915_gem_ww_ctx ww;
	struct i915_vma *vma;
	int ret;

	/*
	 * We are not syncing against the binding (and potential migrations)
	 * below, so this vm must never be async.
	 */
	if (drm_WARN_ON(&i915->drm, vm->bind_async_flags))
		return ERR_PTR(-EINVAL);

	if (WARN_ON(!i915_gem_object_is_framebuffer(obj)))
		return ERR_PTR(-EINVAL);

	atomic_inc(&i915->pending_fb_pin);

	for_i915_gem_ww(&ww, ret, true) {
		ret = i915_gem_object_lock(obj, &ww);
		if (ret)
			continue;

		if (HAS_LMEM(i915)) {
			unsigned int flags = obj->flags;

			/*
			 * For this type of buffer we need to able to read from the CPU
			 * the clear color value found in the buffer, hence we need to
			 * ensure it is always in the mappable part of lmem, if this is
			 * a small-bar device.
			 */
			if (pin_params->needs_cpu_lmem_access)
				flags &= ~I915_BO_ALLOC_GPU_ONLY;
			ret = __i915_gem_object_migrate(obj, &ww, INTEL_REGION_LMEM_0,
							flags);
			if (ret)
				continue;
		}

		ret = i915_gem_object_set_cache_level(obj, I915_CACHE_NONE);
		if (ret)
			continue;

		vma = i915_vma_instance(obj, vm, pin_params->view);
		if (IS_ERR(vma)) {
			ret = PTR_ERR(vma);
			continue;
		}

		if (i915_vma_misplaced(vma, 0, pin_params->alignment, 0)) {
			ret = i915_vma_unbind(vma);
			if (ret)
				continue;
		}

		ret = i915_vma_pin_ww(vma, &ww, 0, pin_params->alignment,
				      PIN_GLOBAL);
		if (ret)
			continue;
	}
	if (ret) {
		vma = ERR_PTR(ret);
		goto err;
	}

	vma->display_alignment = max(vma->display_alignment,
				     pin_params->alignment);

	i915_gem_object_flush_if_display(obj);

	i915_vma_get(vma);

	/*
	 * The DPT object contains only one vma, and there is no VT-d
	 * guard, so the VMA's offset within the DPT is always 0.
	 */
	drm_WARN_ON(&i915->drm, i915_dpt_offset(vma));
err:
	atomic_dec(&i915->pending_fb_pin);

	return vma;
}

static struct i915_vma *
intel_fb_pin_to_ggtt(struct drm_gem_object *_obj,
		     const struct intel_fb_pin_params *pin_params,
		     int *out_fence_id)
{
	struct drm_i915_private *i915 = to_i915(_obj->dev);
	struct drm_i915_gem_object *obj = to_intel_bo(_obj);
	intel_wakeref_t wakeref;
	struct i915_gem_ww_ctx ww;
	struct i915_vma *vma;
	unsigned int pinctl;
	int ret;

	if (drm_WARN_ON(&i915->drm, !i915_gem_object_is_framebuffer(obj)))
		return ERR_PTR(-EINVAL);

	if (drm_WARN_ON(&i915->drm, pin_params->alignment &&
			!is_power_of_2(pin_params->alignment)))
		return ERR_PTR(-EINVAL);

	/*
	 * Global gtt pte registers are special registers which actually forward
	 * writes to a chunk of system memory. Which means that there is no risk
	 * that the register values disappear as soon as we call
	 * intel_runtime_pm_put(), so it is correct to wrap only the
	 * pin/unpin/fence and not more.
	 */
	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	atomic_inc(&i915->pending_fb_pin);

	pinctl = 0;
	/* PIN_MAPPABLE limits the address to GMADR size */
	if (pin_params->needs_low_address)
		pinctl |= PIN_MAPPABLE;

	i915_gem_ww_ctx_init(&ww, true);
retry:
	ret = i915_gem_object_lock(obj, &ww);
	if (!ret && pin_params->needs_physical)
		ret = i915_gem_object_attach_phys(obj, pin_params->phys_alignment);
	else if (!ret && HAS_LMEM(i915))
		ret = i915_gem_object_migrate(obj, &ww, INTEL_REGION_LMEM_0);
	if (!ret)
		ret = i915_gem_object_pin_pages(obj);
	if (ret)
		goto err;

	vma = i915_gem_object_pin_to_display_plane(obj, &ww,
						   pin_params->alignment,
						   pin_params->vtd_guard,
						   pin_params->view, pinctl);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto err_unpin;
	}

	if (out_fence_id)
		*out_fence_id = -1;

	if (out_fence_id && i915_vma_is_map_and_fenceable(vma)) {
		/*
		 * Install a fence for tiled scan-out. Pre-i965 always needs a
		 * fence, whereas 965+ only requires a fence if using
		 * framebuffer compression.  For simplicity, we always, when
		 * possible, install a fence as the cost is not that onerous.
		 *
		 * If we fail to fence the tiled scanout, then either the
		 * modeset will reject the change (which is highly unlikely as
		 * the affected systems, all but one, do not have unmappable
		 * space) or we will not be able to enable full powersaving
		 * techniques (also likely not to apply due to various limits
		 * FBC and the like impose on the size of the buffer, which
		 * presumably we violated anyway with this unmappable buffer).
		 * Anyway, it is presumably better to stumble onwards with
		 * something and try to run the system in a "less than optimal"
		 * mode that matches the user configuration.
		 */
		ret = i915_vma_pin_fence(vma);
		if (ret != 0 && pin_params->needs_fence) {
			i915_vma_unpin(vma);
			goto err_unpin;
		}
		ret = 0;

		if (vma->fence)
			*out_fence_id = vma->fence->id;
	}

	i915_vma_get(vma);

err_unpin:
	i915_gem_object_unpin_pages(obj);
err:
	if (ret == -EDEADLK) {
		ret = i915_gem_ww_ctx_backoff(&ww);
		if (!ret)
			goto retry;
	}
	i915_gem_ww_ctx_fini(&ww);
	if (ret)
		vma = ERR_PTR(ret);

	atomic_dec(&i915->pending_fb_pin);
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	return vma;
}

static void intel_fb_unpin_vma(struct i915_vma *vma, int fence_id)
{
	if (fence_id >= 0)
		i915_vma_unpin_fence(vma);
	i915_vma_unpin(vma);
	i915_vma_put(vma);
}

static int i915_fb_pin_ggtt_pin(struct drm_gem_object *obj,
				const struct intel_fb_pin_params *pin_params,
				struct i915_vma **out_ggtt_vma,
				u32 *out_offset,
				int *out_fence_id)
{
	struct i915_vma *ggtt_vma;

	ggtt_vma = intel_fb_pin_to_ggtt(obj, pin_params, out_fence_id);
	if (IS_ERR(ggtt_vma))
		return PTR_ERR(ggtt_vma);

	*out_ggtt_vma = ggtt_vma;

	/*
	 * Pre-populate the dma address before we enter the vblank
	 * evade critical section as i915_gem_object_get_dma_address()
	 * will trigger might_sleep() even if it won't actually sleep,
	 * which is the case when the fb has already been pinned.
	 */
	if (pin_params->needs_physical)
		*out_offset = i915_gem_object_get_dma_address(to_intel_bo(obj), 0);
	else
		*out_offset = i915_ggtt_offset(ggtt_vma);

	return 0;
}

static void i915_fb_pin_ggtt_unpin(struct i915_vma *ggtt_vma,
				   int fence_id)
{
	if (ggtt_vma)
		intel_fb_unpin_vma(ggtt_vma, fence_id);
}

static int i915_fb_pin_dpt_pin(struct drm_gem_object *obj, struct intel_dpt *dpt,
			       const struct intel_fb_pin_params *pin_params,
			       struct i915_vma **out_dpt_vma,
			       struct i915_vma **out_ggtt_vma,
			       u32 *out_offset)
{
	struct i915_vma *ggtt_vma, *dpt_vma;

	WARN_ON(!dpt);

	ggtt_vma = i915_dpt_pin_to_ggtt(dpt, pin_params->alignment / 512);
	if (IS_ERR(ggtt_vma))
		return PTR_ERR(ggtt_vma);

	dpt_vma = intel_fb_pin_to_dpt(obj, dpt, pin_params);
	if (IS_ERR(dpt_vma)) {
		i915_dpt_unpin_from_ggtt(dpt);
		return PTR_ERR(dpt_vma);
	}

	drm_WARN_ON(obj->dev, ggtt_vma == dpt_vma);

	*out_ggtt_vma = ggtt_vma;
	*out_dpt_vma = dpt_vma;

	*out_offset = i915_ggtt_offset(ggtt_vma);

	return 0;
}

static void i915_fb_pin_dpt_unpin(struct intel_dpt *dpt,
				  struct i915_vma *dpt_vma,
				  struct i915_vma *ggtt_vma)
{
	WARN_ON(!dpt);
	WARN_ON(!!dpt_vma != !!ggtt_vma);

	if (dpt_vma)
		intel_fb_unpin_vma(dpt_vma, -1);
	if (ggtt_vma)
		i915_dpt_unpin_from_ggtt(dpt);
}

static void i915_fb_pin_get_map(struct i915_vma *vma, struct iosys_map *map)
{
	iosys_map_set_vaddr_iomem(map, i915_vma_get_iomap(vma));
}

const struct intel_display_fb_pin_interface i915_display_fb_pin_interface = {
	.ggtt_pin = i915_fb_pin_ggtt_pin,
	.ggtt_unpin = i915_fb_pin_ggtt_unpin,
	.dpt_pin = i915_fb_pin_dpt_pin,
	.dpt_unpin = i915_fb_pin_dpt_unpin,
	.get_map = i915_fb_pin_get_map,
};
