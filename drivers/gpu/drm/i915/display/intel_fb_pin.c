// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

/**
 * DOC: display pinning helpers
 */

#include "gem/i915_gem_domain.h"
#include "gem/i915_gem_object.h"

#include "i915_drv.h"
#include "intel_display_types.h"
#include "intel_dpt.h"
#include "intel_fb.h"
#include "intel_fb_pin.h"

static struct i915_vma *
intel_pin_fb_obj_dpt(struct drm_framebuffer *fb,
		     const struct i915_gtt_view *view,
		     bool uses_fence,
		     unsigned long *out_flags,
		     struct i915_address_space *vm)
{
	struct drm_device *dev = fb->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	struct i915_gem_ww_ctx ww;
	struct i915_vma *vma;
	u32 alignment;
	int ret;

	/*
	 * We are not syncing against the binding (and potential migrations)
	 * below, so this vm must never be async.
	 */
	GEM_WARN_ON(vm->bind_async_flags);

	if (WARN_ON(!i915_gem_object_is_framebuffer(obj)))
		return ERR_PTR(-EINVAL);

	alignment = 4096 * 512;

	atomic_inc(&dev_priv->gpu_error.pending_fb_pin);

	for_i915_gem_ww(&ww, ret, true) {
		ret = i915_gem_object_lock(obj, &ww);
		if (ret)
			continue;

		if (HAS_LMEM(dev_priv)) {
			unsigned int flags = obj->flags;

			/*
			 * For this type of buffer we need to able to read from the CPU
			 * the clear color value found in the buffer, hence we need to
			 * ensure it is always in the mappable part of lmem, if this is
			 * a small-bar device.
			 */
			if (intel_fb_rc_ccs_cc_plane(fb) >= 0)
				flags &= ~I915_BO_ALLOC_GPU_ONLY;
			ret = __i915_gem_object_migrate(obj, &ww, INTEL_REGION_LMEM_0,
							flags);
			if (ret)
				continue;
		}

		ret = i915_gem_object_set_cache_level(obj, I915_CACHE_NONE);
		if (ret)
			continue;

		vma = i915_vma_instance(obj, vm, view);
		if (IS_ERR(vma)) {
			ret = PTR_ERR(vma);
			continue;
		}

		if (i915_vma_misplaced(vma, 0, alignment, 0)) {
			ret = i915_vma_unbind(vma);
			if (ret)
				continue;
		}

		ret = i915_vma_pin_ww(vma, &ww, 0, alignment, PIN_GLOBAL);
		if (ret)
			continue;
	}
	if (ret) {
		vma = ERR_PTR(ret);
		goto err;
	}

	vma->display_alignment = max(vma->display_alignment, alignment);

	i915_gem_object_flush_if_display(obj);

	i915_vma_get(vma);
err:
	atomic_dec(&dev_priv->gpu_error.pending_fb_pin);

	return vma;
}

struct i915_vma *
intel_pin_and_fence_fb_obj(struct drm_framebuffer *fb,
			   bool phys_cursor,
			   const struct i915_gtt_view *view,
			   bool uses_fence,
			   unsigned long *out_flags)
{
	struct drm_device *dev = fb->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	intel_wakeref_t wakeref;
	struct i915_gem_ww_ctx ww;
	struct i915_vma *vma;
	unsigned int pinctl;
	u32 alignment;
	int ret;

	if (drm_WARN_ON(dev, !i915_gem_object_is_framebuffer(obj)))
		return ERR_PTR(-EINVAL);

	if (phys_cursor)
		alignment = intel_cursor_alignment(dev_priv);
	else
		alignment = intel_surf_alignment(fb, 0);
	if (drm_WARN_ON(dev, alignment && !is_power_of_2(alignment)))
		return ERR_PTR(-EINVAL);

	/* Note that the w/a also requires 64 PTE of padding following the
	 * bo. We currently fill all unused PTE with the shadow page and so
	 * we should always have valid PTE following the scanout preventing
	 * the VT-d warning.
	 */
	if (intel_scanout_needs_vtd_wa(dev_priv) && alignment < 256 * 1024)
		alignment = 256 * 1024;

	/*
	 * Global gtt pte registers are special registers which actually forward
	 * writes to a chunk of system memory. Which means that there is no risk
	 * that the register values disappear as soon as we call
	 * intel_runtime_pm_put(), so it is correct to wrap only the
	 * pin/unpin/fence and not more.
	 */
	wakeref = intel_runtime_pm_get(&dev_priv->runtime_pm);

	atomic_inc(&dev_priv->gpu_error.pending_fb_pin);

	/*
	 * Valleyview is definitely limited to scanning out the first
	 * 512MiB. Lets presume this behaviour was inherited from the
	 * g4x display engine and that all earlier gen are similarly
	 * limited. Testing suggests that it is a little more
	 * complicated than this. For example, Cherryview appears quite
	 * happy to scanout from anywhere within its global aperture.
	 */
	pinctl = 0;
	if (HAS_GMCH(dev_priv))
		pinctl |= PIN_MAPPABLE;

	i915_gem_ww_ctx_init(&ww, true);
retry:
	ret = i915_gem_object_lock(obj, &ww);
	if (!ret && phys_cursor)
		ret = i915_gem_object_attach_phys(obj, alignment);
	else if (!ret && HAS_LMEM(dev_priv))
		ret = i915_gem_object_migrate(obj, &ww, INTEL_REGION_LMEM_0);
	if (!ret)
		ret = i915_gem_object_pin_pages(obj);
	if (ret)
		goto err;

	vma = i915_gem_object_pin_to_display_plane(obj, &ww, alignment,
						   view, pinctl);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto err_unpin;
	}

	if (uses_fence && i915_vma_is_map_and_fenceable(vma)) {
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
		if (ret != 0 && DISPLAY_VER(dev_priv) < 4) {
			i915_vma_unpin(vma);
			goto err_unpin;
		}
		ret = 0;

		if (vma->fence)
			*out_flags |= PLANE_HAS_FENCE;
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

	atomic_dec(&dev_priv->gpu_error.pending_fb_pin);
	intel_runtime_pm_put(&dev_priv->runtime_pm, wakeref);
	return vma;
}

void intel_unpin_fb_vma(struct i915_vma *vma, unsigned long flags)
{
	if (flags & PLANE_HAS_FENCE)
		i915_vma_unpin_fence(vma);
	i915_vma_unpin(vma);
	i915_vma_put(vma);
}

int intel_plane_pin_fb(struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	struct drm_framebuffer *fb = plane_state->hw.fb;
	struct i915_vma *vma;
	bool phys_cursor =
		plane->id == PLANE_CURSOR &&
		DISPLAY_INFO(dev_priv)->cursor_needs_physical;

	if (!intel_fb_uses_dpt(fb)) {
		vma = intel_pin_and_fence_fb_obj(fb, phys_cursor,
						 &plane_state->view.gtt,
						 intel_plane_uses_fence(plane_state),
						 &plane_state->flags);
		if (IS_ERR(vma))
			return PTR_ERR(vma);

		plane_state->ggtt_vma = vma;
	} else {
		struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);

		vma = intel_dpt_pin(intel_fb->dpt_vm);
		if (IS_ERR(vma))
			return PTR_ERR(vma);

		plane_state->ggtt_vma = vma;

		vma = intel_pin_fb_obj_dpt(fb, &plane_state->view.gtt, false,
					   &plane_state->flags, intel_fb->dpt_vm);
		if (IS_ERR(vma)) {
			intel_dpt_unpin(intel_fb->dpt_vm);
			plane_state->ggtt_vma = NULL;
			return PTR_ERR(vma);
		}

		plane_state->dpt_vma = vma;

		WARN_ON(plane_state->ggtt_vma == plane_state->dpt_vma);
	}

	return 0;
}

void intel_plane_unpin_fb(struct intel_plane_state *old_plane_state)
{
	struct drm_framebuffer *fb = old_plane_state->hw.fb;
	struct i915_vma *vma;

	if (!intel_fb_uses_dpt(fb)) {
		vma = fetch_and_zero(&old_plane_state->ggtt_vma);
		if (vma)
			intel_unpin_fb_vma(vma, old_plane_state->flags);
	} else {
		struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);

		vma = fetch_and_zero(&old_plane_state->dpt_vma);
		if (vma)
			intel_unpin_fb_vma(vma, old_plane_state->flags);

		vma = fetch_and_zero(&old_plane_state->ggtt_vma);
		if (vma)
			intel_dpt_unpin(intel_fb->dpt_vm);
	}
}
