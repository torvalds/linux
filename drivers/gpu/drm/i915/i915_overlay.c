// SPDX-License-Identifier: MIT
/*
 * Copyright 2026, Intel Corporation.
 */

#include <drm/drm_print.h>

#include <drm/intel/display_parent_interface.h>
#include <drm/intel/intel_gmd_interrupt_regs.h>

#include "gem/i915_gem_internal.h"
#include "gem/i915_gem_object_frontbuffer.h"
#include "gem/i915_gem_pm.h"

#include "gt/intel_gpu_commands.h"
#include "gt/intel_ring.h"

#include "i915_drv.h"
#include "i915_overlay.h"
#include "i915_reg.h"
#include "intel_pci_config.h"

#include "display/intel_frontbuffer.h"

/* overlay flip addr flag */
#define OFC_UPDATE		0x1

struct i915_overlay {
	struct drm_i915_private *i915;
	struct intel_context *context;
	struct i915_vma *vma;
	struct i915_vma *old_vma;
	struct intel_frontbuffer *frontbuffer;
	/* register access */
	struct drm_i915_gem_object *reg_bo;
	void __iomem *regs;
	u32 flip_addr;
	u32 frontbuffer_bits;
	/* flip handling */
	struct i915_active last_flip;
	void (*flip_complete)(struct i915_overlay *overlay);
};

static void i830_overlay_clock_gating(struct drm_i915_private *i915,
				      bool enable)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	u8 val;

	/*
	 * WA_OVERLAY_CLKGATE:alm
	 *
	 * FIXME should perhaps be done on the display side?
	 */
	if (enable)
		intel_uncore_write(&i915->uncore, DSPCLK_GATE_D, 0);
	else
		intel_uncore_write(&i915->uncore, DSPCLK_GATE_D, OVRUNIT_CLOCK_GATE_DISABLE);

	/* WA_DISABLE_L2CACHE_CLOCK_GATING:alm */
	pci_bus_read_config_byte(pdev->bus,
				 PCI_DEVFN(0, 0), I830_CLOCK_GATE, &val);
	if (enable)
		val &= ~I830_L2_CACHE_CLOCK_GATE_DISABLE;
	else
		val |= I830_L2_CACHE_CLOCK_GATE_DISABLE;
	pci_bus_write_config_byte(pdev->bus,
				  PCI_DEVFN(0, 0), I830_CLOCK_GATE, val);
}

static struct i915_request *
alloc_request(struct i915_overlay *overlay, void (*fn)(struct i915_overlay *))
{
	struct i915_request *rq;
	int err;

	overlay->flip_complete = fn;

	rq = i915_request_create(overlay->context);
	if (IS_ERR(rq))
		return rq;

	err = i915_active_add_request(&overlay->last_flip, rq);
	if (err) {
		i915_request_add(rq);
		return ERR_PTR(err);
	}

	return rq;
}

static bool i915_overlay_is_active(struct drm_device *drm)
{
	struct drm_i915_private *i915 = to_i915(drm);
	struct i915_overlay *overlay = i915->overlay;

	return overlay->frontbuffer_bits;
}

/* overlay needs to be disable in OCMD reg */
static int i915_overlay_on(struct drm_device *drm,
			   u32 frontbuffer_bits)
{
	struct drm_i915_private *i915 = to_i915(drm);
	struct i915_overlay *overlay = i915->overlay;
	struct i915_request *rq;
	u32 *cs;

	drm_WARN_ON(drm, i915_overlay_is_active(drm));

	rq = alloc_request(overlay, NULL);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	cs = intel_ring_begin(rq, 4);
	if (IS_ERR(cs)) {
		i915_request_add(rq);
		return PTR_ERR(cs);
	}

	overlay->frontbuffer_bits = frontbuffer_bits;

	if (IS_I830(i915))
		i830_overlay_clock_gating(i915, false);

	*cs++ = MI_OVERLAY_FLIP | MI_OVERLAY_ON;
	*cs++ = overlay->flip_addr | OFC_UPDATE;
	*cs++ = MI_WAIT_FOR_EVENT | MI_WAIT_FOR_OVERLAY_FLIP;
	*cs++ = MI_NOOP;
	intel_ring_advance(rq, cs);

	i915_request_add(rq);

	return i915_active_wait(&overlay->last_flip);
}

static void i915_overlay_flip_prepare(struct i915_overlay *overlay,
				      struct i915_vma *vma)
{
	struct drm_i915_private *i915 = overlay->i915;
	struct intel_frontbuffer *frontbuffer = NULL;

	drm_WARN_ON(&i915->drm, overlay->old_vma);

	if (vma)
		frontbuffer = intel_frontbuffer_get(intel_bo_to_drm_bo(vma->obj));

	intel_frontbuffer_track(overlay->frontbuffer, frontbuffer,
				overlay->frontbuffer_bits);

	if (overlay->frontbuffer)
		intel_frontbuffer_put(overlay->frontbuffer);
	overlay->frontbuffer = frontbuffer;

	overlay->old_vma = overlay->vma;
	if (vma)
		overlay->vma = i915_vma_get(vma);
	else
		overlay->vma = NULL;
}

/* overlay needs to be enabled in OCMD reg */
static int i915_overlay_continue(struct drm_device *drm,
				 struct i915_vma *vma,
				 bool load_polyphase_filter)
{
	struct drm_i915_private *i915 = to_i915(drm);
	struct i915_overlay *overlay = i915->overlay;
	struct i915_request *rq;
	u32 flip_addr = overlay->flip_addr;
	u32 *cs;

	drm_WARN_ON(drm, !i915_overlay_is_active(drm));

	if (load_polyphase_filter)
		flip_addr |= OFC_UPDATE;

	rq = alloc_request(overlay, NULL);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	cs = intel_ring_begin(rq, 2);
	if (IS_ERR(cs)) {
		i915_request_add(rq);
		return PTR_ERR(cs);
	}

	*cs++ = MI_OVERLAY_FLIP | MI_OVERLAY_CONTINUE;
	*cs++ = flip_addr;
	intel_ring_advance(rq, cs);

	i915_overlay_flip_prepare(overlay, vma);
	i915_request_add(rq);

	return 0;
}

static void i915_overlay_release_old_vma(struct i915_overlay *overlay)
{
	struct drm_i915_private *i915 = overlay->i915;
	struct intel_display *display = i915->display;
	struct i915_vma *vma;

	vma = fetch_and_zero(&overlay->old_vma);
	if (drm_WARN_ON(&i915->drm, !vma))
		return;

	intel_frontbuffer_flip(display, overlay->frontbuffer_bits);

	i915_vma_unpin(vma);
	i915_vma_put(vma);
}

static void
i915_overlay_release_old_vid_tail(struct i915_overlay *overlay)
{
	i915_overlay_release_old_vma(overlay);
}

static void i915_overlay_off_tail(struct i915_overlay *overlay)
{
	struct drm_i915_private *i915 = overlay->i915;

	i915_overlay_release_old_vma(overlay);

	overlay->frontbuffer_bits = 0;

	if (IS_I830(i915))
		i830_overlay_clock_gating(i915, true);
}

static void i915_overlay_last_flip_retire(struct i915_active *active)
{
	struct i915_overlay *overlay =
		container_of(active, typeof(*overlay), last_flip);

	if (overlay->flip_complete)
		overlay->flip_complete(overlay);
}

/* overlay needs to be disabled in OCMD reg */
static int i915_overlay_off(struct drm_device *drm)
{
	struct drm_i915_private *i915 = to_i915(drm);
	struct i915_overlay *overlay = i915->overlay;
	struct i915_request *rq;
	u32 *cs, flip_addr = overlay->flip_addr;

	drm_WARN_ON(drm, !i915_overlay_is_active(drm));

	/*
	 * According to intel docs the overlay hw may hang (when switching
	 * off) without loading the filter coeffs. It is however unclear whether
	 * this applies to the disabling of the overlay or to the switching off
	 * of the hw. Do it in both cases.
	 */
	flip_addr |= OFC_UPDATE;

	rq = alloc_request(overlay, i915_overlay_off_tail);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	cs = intel_ring_begin(rq, 6);
	if (IS_ERR(cs)) {
		i915_request_add(rq);
		return PTR_ERR(cs);
	}

	/* wait for overlay to go idle */
	*cs++ = MI_OVERLAY_FLIP | MI_OVERLAY_CONTINUE;
	*cs++ = flip_addr;
	*cs++ = MI_WAIT_FOR_EVENT | MI_WAIT_FOR_OVERLAY_FLIP;

	/* turn overlay off */
	*cs++ = MI_OVERLAY_FLIP | MI_OVERLAY_OFF;
	*cs++ = flip_addr;
	*cs++ = MI_WAIT_FOR_EVENT | MI_WAIT_FOR_OVERLAY_FLIP;

	intel_ring_advance(rq, cs);

	i915_overlay_flip_prepare(overlay, NULL);
	i915_request_add(rq);

	return i915_active_wait(&overlay->last_flip);
}

/*
 * Recover from an interruption due to a signal.
 * We have to be careful not to repeat work forever an make forward progress.
 */
static int i915_overlay_recover_from_interrupt(struct drm_device *drm)
{
	struct drm_i915_private *i915 = to_i915(drm);
	struct i915_overlay *overlay = i915->overlay;

	return i915_active_wait(&overlay->last_flip);
}

/*
 * Wait for pending overlay flip and release old frame.
 * Needs to be called before the overlay register are changed
 * via intel_overlay_(un)map_regs.
 */
static int i915_overlay_release_old_vid(struct drm_device *drm)
{
	struct drm_i915_private *i915 = to_i915(drm);
	struct i915_overlay *overlay = i915->overlay;
	struct i915_request *rq;
	u32 *cs;

	/*
	 * Only wait if there is actually an old frame to release to
	 * guarantee forward progress.
	 */
	if (!overlay->old_vma)
		return 0;

	if (!(intel_uncore_read(&i915->uncore, GEN2_ISR) & I915_OVERLAY_PLANE_FLIP_PENDING_INTERRUPT)) {
		i915_overlay_release_old_vid_tail(overlay);
		return 0;
	}

	rq = alloc_request(overlay, i915_overlay_release_old_vid_tail);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	cs = intel_ring_begin(rq, 2);
	if (IS_ERR(cs)) {
		i915_request_add(rq);
		return PTR_ERR(cs);
	}

	*cs++ = MI_WAIT_FOR_EVENT | MI_WAIT_FOR_OVERLAY_FLIP;
	*cs++ = MI_NOOP;
	intel_ring_advance(rq, cs);

	i915_request_add(rq);

	return i915_active_wait(&overlay->last_flip);
}

static void i915_overlay_reset(struct drm_device *drm)
{
	struct drm_i915_private *i915 = to_i915(drm);
	struct i915_overlay *overlay = i915->overlay;

	if (!overlay)
		return;

	overlay->frontbuffer_bits = 0;
}

static struct i915_vma *i915_overlay_pin_fb(struct drm_device *drm,
					    struct drm_gem_object *obj,
					    u32 *offset)
{
	struct drm_i915_gem_object *new_bo = to_intel_bo(obj);
	struct i915_gem_ww_ctx ww;
	struct i915_vma *vma;
	int ret;

	i915_gem_ww_ctx_init(&ww, true);
retry:
	ret = i915_gem_object_lock(new_bo, &ww);
	if (!ret) {
		vma = i915_gem_object_pin_to_display_plane(new_bo, &ww, 0, 0,
							   NULL, PIN_MAPPABLE);
		ret = PTR_ERR_OR_ZERO(vma);
	}
	if (ret == -EDEADLK) {
		ret = i915_gem_ww_ctx_backoff(&ww);
		if (!ret)
			goto retry;
	}
	i915_gem_ww_ctx_fini(&ww);
	if (ret)
		return ERR_PTR(ret);

	*offset = i915_ggtt_offset(vma);

	return vma;
}

static void i915_overlay_unpin_fb(struct drm_device *drm,
				  struct i915_vma *vma)
{
	i915_vma_unpin(vma);
}

static struct drm_gem_object *
i915_overlay_obj_lookup(struct drm_device *drm,
			struct drm_file *file_priv,
			u32 handle)
{
	struct drm_i915_gem_object *bo;

	bo = i915_gem_object_lookup(file_priv, handle);
	if (!bo)
		return ERR_PTR(-ENOENT);

	if (i915_gem_object_is_tiled(bo)) {
		drm_dbg(drm, "buffer used for overlay image can not be tiled\n");
		i915_gem_object_put(bo);
		return ERR_PTR(-EINVAL);
	}

	return intel_bo_to_drm_bo(bo);
}

static int get_registers(struct i915_overlay *overlay, bool use_phys)
{
	struct drm_i915_private *i915 = overlay->i915;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int err;

	obj = i915_gem_object_create_stolen(i915, PAGE_SIZE);
	if (IS_ERR(obj))
		obj = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0, PIN_MAPPABLE);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err_put_bo;
	}

	if (use_phys)
		overlay->flip_addr = sg_dma_address(obj->mm.pages->sgl);
	else
		overlay->flip_addr = i915_ggtt_offset(vma);
	overlay->regs = i915_vma_pin_iomap(vma);
	i915_vma_unpin(vma);

	if (IS_ERR(overlay->regs)) {
		err = PTR_ERR(overlay->regs);
		goto err_put_bo;
	}

	overlay->reg_bo = obj;
	return 0;

err_put_bo:
	i915_gem_object_put(obj);
	return err;
}

static void __iomem *i915_overlay_setup(struct drm_device *drm,
					bool needs_physical)
{
	struct drm_i915_private *i915 = to_i915(drm);
	struct intel_engine_cs *engine;
	struct i915_overlay *overlay;
	int ret;

	engine = to_gt(i915)->engine[RCS0];
	if (!engine || !engine->kernel_context)
		return ERR_PTR(-ENOENT);

	overlay = kzalloc_obj(*overlay);
	if (!overlay)
		return ERR_PTR(-ENOMEM);

	overlay->i915 = i915;
	overlay->context = engine->kernel_context;

	i915_active_init(&overlay->last_flip,
			 NULL, i915_overlay_last_flip_retire, 0);

	ret = get_registers(overlay, needs_physical);
	if (ret) {
		kfree(overlay);
		return ERR_PTR(ret);
	}

	i915->overlay = overlay;

	return overlay->regs;
}

static void i915_overlay_cleanup(struct drm_device *drm)
{
	struct drm_i915_private *i915 = to_i915(drm);
	struct i915_overlay *overlay = i915->overlay;

	if (!overlay)
		return;

	/*
	 * The bo's should be free'd by the generic code already.
	 * Furthermore modesetting teardown happens beforehand so the
	 * hardware should be off already.
	 */
	drm_WARN_ON(drm, i915_overlay_is_active(drm));

	i915_gem_object_put(overlay->reg_bo);
	i915_active_fini(&overlay->last_flip);

	kfree(overlay);
	i915->overlay = NULL;
}

const struct intel_display_overlay_interface i915_display_overlay_interface = {
	.is_active = i915_overlay_is_active,
	.overlay_on = i915_overlay_on,
	.overlay_continue = i915_overlay_continue,
	.overlay_off = i915_overlay_off,
	.recover_from_interrupt = i915_overlay_recover_from_interrupt,
	.release_old_vid = i915_overlay_release_old_vid,
	.reset = i915_overlay_reset,
	.obj_lookup = i915_overlay_obj_lookup,
	.pin_fb = i915_overlay_pin_fb,
	.unpin_fb = i915_overlay_unpin_fb,
	.setup = i915_overlay_setup,
	.cleanup = i915_overlay_cleanup,
};
