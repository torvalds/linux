// SPDX-License-Identifier: MIT
/*
 * Copyright © 2008-2021 Intel Corporation
 */

#include "gen2_engine_cs.h"
#include "gen6_engine_cs.h"
#include "gen6_ppgtt.h"
#include "gen7_renderclear.h"
#include "i915_drv.h"
#include "i915_mitigations.h"
#include "intel_breadcrumbs.h"
#include "intel_context.h"
#include "intel_engine_regs.h"
#include "intel_gt.h"
#include "intel_gt_irq.h"
#include "intel_gt_regs.h"
#include "intel_reset.h"
#include "intel_ring.h"
#include "shmem_utils.h"
#include "intel_engine_heartbeat.h"
#include "intel_engine_pm.h"

/* Rough estimate of the typical request size, performing a flush,
 * set-context and then emitting the batch.
 */
#define LEGACY_REQUEST_SIZE 200

static void set_hwstam(struct intel_engine_cs *engine, u32 mask)
{
	/*
	 * Keep the render interrupt unmasked as this papers over
	 * lost interrupts following a reset.
	 */
	if (engine->class == RENDER_CLASS) {
		if (GRAPHICS_VER(engine->i915) >= 6)
			mask &= ~BIT(0);
		else
			mask &= ~I915_USER_INTERRUPT;
	}

	intel_engine_set_hwsp_writemask(engine, mask);
}

static void set_hws_pga(struct intel_engine_cs *engine, phys_addr_t phys)
{
	u32 addr;

	addr = lower_32_bits(phys);
	if (GRAPHICS_VER(engine->i915) >= 4)
		addr |= (phys >> 28) & 0xf0;

	intel_uncore_write(engine->uncore, HWS_PGA, addr);
}

static struct page *status_page(struct intel_engine_cs *engine)
{
	struct drm_i915_gem_object *obj = engine->status_page.vma->obj;

	GEM_BUG_ON(!i915_gem_object_has_pinned_pages(obj));
	return sg_page(obj->mm.pages->sgl);
}

static void ring_setup_phys_status_page(struct intel_engine_cs *engine)
{
	set_hws_pga(engine, PFN_PHYS(page_to_pfn(status_page(engine))));
	set_hwstam(engine, ~0u);
}

static void set_hwsp(struct intel_engine_cs *engine, u32 offset)
{
	i915_reg_t hwsp;

	/*
	 * The ring status page addresses are no longer next to the rest of
	 * the ring registers as of gen7.
	 */
	if (GRAPHICS_VER(engine->i915) == 7) {
		switch (engine->id) {
		/*
		 * No more rings exist on Gen7. Default case is only to shut up
		 * gcc switch check warning.
		 */
		default:
			GEM_BUG_ON(engine->id);
			fallthrough;
		case RCS0:
			hwsp = RENDER_HWS_PGA_GEN7;
			break;
		case BCS0:
			hwsp = BLT_HWS_PGA_GEN7;
			break;
		case VCS0:
			hwsp = BSD_HWS_PGA_GEN7;
			break;
		case VECS0:
			hwsp = VEBOX_HWS_PGA_GEN7;
			break;
		}
	} else if (GRAPHICS_VER(engine->i915) == 6) {
		hwsp = RING_HWS_PGA_GEN6(engine->mmio_base);
	} else {
		hwsp = RING_HWS_PGA(engine->mmio_base);
	}

	intel_uncore_write_fw(engine->uncore, hwsp, offset);
	intel_uncore_posting_read_fw(engine->uncore, hwsp);
}

static void flush_cs_tlb(struct intel_engine_cs *engine)
{
	if (!IS_GRAPHICS_VER(engine->i915, 6, 7))
		return;

	/* ring should be idle before issuing a sync flush*/
	GEM_DEBUG_WARN_ON((ENGINE_READ(engine, RING_MI_MODE) & MODE_IDLE) == 0);

	ENGINE_WRITE_FW(engine, RING_INSTPM,
			_MASKED_BIT_ENABLE(INSTPM_TLB_INVALIDATE |
					   INSTPM_SYNC_FLUSH));
	if (__intel_wait_for_register_fw(engine->uncore,
					 RING_INSTPM(engine->mmio_base),
					 INSTPM_SYNC_FLUSH, 0,
					 2000, 0, NULL))
		ENGINE_TRACE(engine,
			     "wait for SyncFlush to complete for TLB invalidation timed out\n");
}

static void ring_setup_status_page(struct intel_engine_cs *engine)
{
	set_hwsp(engine, i915_ggtt_offset(engine->status_page.vma));
	set_hwstam(engine, ~0u);

	flush_cs_tlb(engine);
}

static struct i915_address_space *vm_alias(struct i915_address_space *vm)
{
	if (i915_is_ggtt(vm))
		vm = &i915_vm_to_ggtt(vm)->alias->vm;

	return vm;
}

static u32 pp_dir(struct i915_address_space *vm)
{
	return to_gen6_ppgtt(i915_vm_to_ppgtt(vm))->pp_dir;
}

static void set_pp_dir(struct intel_engine_cs *engine)
{
	struct i915_address_space *vm = vm_alias(engine->gt->vm);

	if (!vm)
		return;

	ENGINE_WRITE_FW(engine, RING_PP_DIR_DCLV, PP_DIR_DCLV_2G);
	ENGINE_WRITE_FW(engine, RING_PP_DIR_BASE, pp_dir(vm));

	if (GRAPHICS_VER(engine->i915) >= 7) {
		ENGINE_WRITE_FW(engine,
				RING_MODE_GEN7,
				_MASKED_BIT_ENABLE(GFX_PPGTT_ENABLE));
	}
}

static bool stop_ring(struct intel_engine_cs *engine)
{
	/* Empty the ring by skipping to the end */
	ENGINE_WRITE_FW(engine, RING_HEAD, ENGINE_READ_FW(engine, RING_TAIL));
	ENGINE_POSTING_READ(engine, RING_HEAD);

	/* The ring must be empty before it is disabled */
	ENGINE_WRITE_FW(engine, RING_CTL, 0);
	ENGINE_POSTING_READ(engine, RING_CTL);

	/* Then reset the disabled ring */
	ENGINE_WRITE_FW(engine, RING_HEAD, 0);
	ENGINE_WRITE_FW(engine, RING_TAIL, 0);

	return (ENGINE_READ_FW(engine, RING_HEAD) & HEAD_ADDR) == 0;
}

static int xcs_resume(struct intel_engine_cs *engine)
{
	struct intel_ring *ring = engine->legacy.ring;

	ENGINE_TRACE(engine, "ring:{HEAD:%04x, TAIL:%04x}\n",
		     ring->head, ring->tail);

	/*
	 * Double check the ring is empty & disabled before we resume. Called
	 * from atomic context during PCI probe, so _hardirq().
	 */
	intel_synchronize_hardirq(engine->i915);
	if (!stop_ring(engine))
		goto err;

	if (HWS_NEEDS_PHYSICAL(engine->i915))
		ring_setup_phys_status_page(engine);
	else
		ring_setup_status_page(engine);

	intel_breadcrumbs_reset(engine->breadcrumbs);

	/* Enforce ordering by reading HEAD register back */
	ENGINE_POSTING_READ(engine, RING_HEAD);

	/*
	 * Initialize the ring. This must happen _after_ we've cleared the ring
	 * registers with the above sequence (the readback of the HEAD registers
	 * also enforces ordering), otherwise the hw might lose the new ring
	 * register values.
	 */
	ENGINE_WRITE_FW(engine, RING_START, i915_ggtt_offset(ring->vma));

	/* Check that the ring offsets point within the ring! */
	GEM_BUG_ON(!intel_ring_offset_valid(ring, ring->head));
	GEM_BUG_ON(!intel_ring_offset_valid(ring, ring->tail));
	intel_ring_update_space(ring);

	set_pp_dir(engine);

	/* First wake the ring up to an empty/idle ring */
	ENGINE_WRITE_FW(engine, RING_HEAD, ring->head);
	ENGINE_WRITE_FW(engine, RING_TAIL, ring->head);
	ENGINE_POSTING_READ(engine, RING_TAIL);

	ENGINE_WRITE_FW(engine, RING_CTL,
			RING_CTL_SIZE(ring->size) | RING_VALID);

	/* If the head is still not zero, the ring is dead */
	if (__intel_wait_for_register_fw(engine->uncore,
					 RING_CTL(engine->mmio_base),
					 RING_VALID, RING_VALID,
					 5000, 0, NULL))
		goto err;

	if (GRAPHICS_VER(engine->i915) > 2)
		ENGINE_WRITE_FW(engine,
				RING_MI_MODE, _MASKED_BIT_DISABLE(STOP_RING));

	/* Now awake, let it get started */
	if (ring->tail != ring->head) {
		ENGINE_WRITE_FW(engine, RING_TAIL, ring->tail);
		ENGINE_POSTING_READ(engine, RING_TAIL);
	}

	/* Papering over lost _interrupts_ immediately following the restart */
	intel_engine_signal_breadcrumbs(engine);
	return 0;

err:
	drm_err(&engine->i915->drm,
		"%s initialization failed; "
		"ctl %08x (valid? %d) head %08x [%08x] tail %08x [%08x] start %08x [expected %08x]\n",
		engine->name,
		ENGINE_READ(engine, RING_CTL),
		ENGINE_READ(engine, RING_CTL) & RING_VALID,
		ENGINE_READ(engine, RING_HEAD), ring->head,
		ENGINE_READ(engine, RING_TAIL), ring->tail,
		ENGINE_READ(engine, RING_START),
		i915_ggtt_offset(ring->vma));
	return -EIO;
}

static void sanitize_hwsp(struct intel_engine_cs *engine)
{
	struct intel_timeline *tl;

	list_for_each_entry(tl, &engine->status_page.timelines, engine_link)
		intel_timeline_reset_seqno(tl);
}

static void xcs_sanitize(struct intel_engine_cs *engine)
{
	/*
	 * Poison residual state on resume, in case the suspend didn't!
	 *
	 * We have to assume that across suspend/resume (or other loss
	 * of control) that the contents of our pinned buffers has been
	 * lost, replaced by garbage. Since this doesn't always happen,
	 * let's poison such state so that we more quickly spot when
	 * we falsely assume it has been preserved.
	 */
	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM))
		memset(engine->status_page.addr, POISON_INUSE, PAGE_SIZE);

	/*
	 * The kernel_context HWSP is stored in the status_page. As above,
	 * that may be lost on resume/initialisation, and so we need to
	 * reset the value in the HWSP.
	 */
	sanitize_hwsp(engine);

	/* And scrub the dirty cachelines for the HWSP */
	drm_clflush_virt_range(engine->status_page.addr, PAGE_SIZE);

	intel_engine_reset_pinned_contexts(engine);
}

static void reset_prepare(struct intel_engine_cs *engine)
{
	/*
	 * We stop engines, otherwise we might get failed reset and a
	 * dead gpu (on elk). Also as modern gpu as kbl can suffer
	 * from system hang if batchbuffer is progressing when
	 * the reset is issued, regardless of READY_TO_RESET ack.
	 * Thus assume it is best to stop engines on all gens
	 * where we have a gpu reset.
	 *
	 * WaKBLVECSSemaphoreWaitPoll:kbl (on ALL_ENGINES)
	 *
	 * WaMediaResetMainRingCleanup:ctg,elk (presumably)
	 * WaClearRingBufHeadRegAtInit:ctg,elk
	 *
	 * FIXME: Wa for more modern gens needs to be validated
	 */
	ENGINE_TRACE(engine, "\n");
	intel_engine_stop_cs(engine);

	if (!stop_ring(engine)) {
		/* G45 ring initialization often fails to reset head to zero */
		ENGINE_TRACE(engine,
			     "HEAD not reset to zero, "
			     "{ CTL:%08x, HEAD:%08x, TAIL:%08x, START:%08x }\n",
			     ENGINE_READ_FW(engine, RING_CTL),
			     ENGINE_READ_FW(engine, RING_HEAD),
			     ENGINE_READ_FW(engine, RING_TAIL),
			     ENGINE_READ_FW(engine, RING_START));
		if (!stop_ring(engine)) {
			drm_err(&engine->i915->drm,
				"failed to set %s head to zero "
				"ctl %08x head %08x tail %08x start %08x\n",
				engine->name,
				ENGINE_READ_FW(engine, RING_CTL),
				ENGINE_READ_FW(engine, RING_HEAD),
				ENGINE_READ_FW(engine, RING_TAIL),
				ENGINE_READ_FW(engine, RING_START));
		}
	}
}

static void reset_rewind(struct intel_engine_cs *engine, bool stalled)
{
	struct i915_request *pos, *rq;
	unsigned long flags;
	u32 head;

	rq = NULL;
	spin_lock_irqsave(&engine->sched_engine->lock, flags);
	rcu_read_lock();
	list_for_each_entry(pos, &engine->sched_engine->requests, sched.link) {
		if (!__i915_request_is_complete(pos)) {
			rq = pos;
			break;
		}
	}
	rcu_read_unlock();

	/*
	 * The guilty request will get skipped on a hung engine.
	 *
	 * Users of client default contexts do not rely on logical
	 * state preserved between batches so it is safe to execute
	 * queued requests following the hang. Non default contexts
	 * rely on preserved state, so skipping a batch loses the
	 * evolution of the state and it needs to be considered corrupted.
	 * Executing more queued batches on top of corrupted state is
	 * risky. But we take the risk by trying to advance through
	 * the queued requests in order to make the client behaviour
	 * more predictable around resets, by not throwing away random
	 * amount of batches it has prepared for execution. Sophisticated
	 * clients can use gem_reset_stats_ioctl and dma fence status
	 * (exported via sync_file info ioctl on explicit fences) to observe
	 * when it loses the context state and should rebuild accordingly.
	 *
	 * The context ban, and ultimately the client ban, mechanism are safety
	 * valves if client submission ends up resulting in nothing more than
	 * subsequent hangs.
	 */

	if (rq) {
		/*
		 * Try to restore the logical GPU state to match the
		 * continuation of the request queue. If we skip the
		 * context/PD restore, then the next request may try to execute
		 * assuming that its context is valid and loaded on the GPU and
		 * so may try to access invalid memory, prompting repeated GPU
		 * hangs.
		 *
		 * If the request was guilty, we still restore the logical
		 * state in case the next request requires it (e.g. the
		 * aliasing ppgtt), but skip over the hung batch.
		 *
		 * If the request was innocent, we try to replay the request
		 * with the restored context.
		 */
		__i915_request_reset(rq, stalled);

		GEM_BUG_ON(rq->ring != engine->legacy.ring);
		head = rq->head;
	} else {
		head = engine->legacy.ring->tail;
	}
	engine->legacy.ring->head = intel_ring_wrap(engine->legacy.ring, head);

	spin_unlock_irqrestore(&engine->sched_engine->lock, flags);
}

static void reset_finish(struct intel_engine_cs *engine)
{
}

static void reset_cancel(struct intel_engine_cs *engine)
{
	struct i915_request *request;
	unsigned long flags;

	spin_lock_irqsave(&engine->sched_engine->lock, flags);

	/* Mark all submitted requests as skipped. */
	list_for_each_entry(request, &engine->sched_engine->requests, sched.link)
		i915_request_put(i915_request_mark_eio(request));
	intel_engine_signal_breadcrumbs(engine);

	/* Remaining _unready_ requests will be nop'ed when submitted */

	spin_unlock_irqrestore(&engine->sched_engine->lock, flags);
}

static void i9xx_submit_request(struct i915_request *request)
{
	i915_request_submit(request);
	wmb(); /* paranoid flush writes out of the WCB before mmio */

	ENGINE_WRITE(request->engine, RING_TAIL,
		     intel_ring_set_tail(request->ring, request->tail));
}

static void __ring_context_fini(struct intel_context *ce)
{
	i915_vma_put(ce->state);
}

static void ring_context_destroy(struct kref *ref)
{
	struct intel_context *ce = container_of(ref, typeof(*ce), ref);

	GEM_BUG_ON(intel_context_is_pinned(ce));

	if (ce->state)
		__ring_context_fini(ce);

	intel_context_fini(ce);
	intel_context_free(ce);
}

static int ring_context_init_default_state(struct intel_context *ce,
					   struct i915_gem_ww_ctx *ww)
{
	struct drm_i915_gem_object *obj = ce->state->obj;
	void *vaddr;

	vaddr = i915_gem_object_pin_map(obj, I915_MAP_WB);
	if (IS_ERR(vaddr))
		return PTR_ERR(vaddr);

	shmem_read(ce->engine->default_state, 0,
		   vaddr, ce->engine->context_size);

	i915_gem_object_flush_map(obj);
	__i915_gem_object_release_map(obj);

	__set_bit(CONTEXT_VALID_BIT, &ce->flags);
	return 0;
}

static int ring_context_pre_pin(struct intel_context *ce,
				struct i915_gem_ww_ctx *ww,
				void **unused)
{
	struct i915_address_space *vm;
	int err = 0;

	if (ce->engine->default_state &&
	    !test_bit(CONTEXT_VALID_BIT, &ce->flags)) {
		err = ring_context_init_default_state(ce, ww);
		if (err)
			return err;
	}

	vm = vm_alias(ce->vm);
	if (vm)
		err = gen6_ppgtt_pin(i915_vm_to_ppgtt((vm)), ww);

	return err;
}

static void __context_unpin_ppgtt(struct intel_context *ce)
{
	struct i915_address_space *vm;

	vm = vm_alias(ce->vm);
	if (vm)
		gen6_ppgtt_unpin(i915_vm_to_ppgtt(vm));
}

static void ring_context_unpin(struct intel_context *ce)
{
}

static void ring_context_post_unpin(struct intel_context *ce)
{
	__context_unpin_ppgtt(ce);
}

static struct i915_vma *
alloc_context_vma(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int err;

	obj = i915_gem_object_create_shmem(i915, engine->context_size);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	/*
	 * Try to make the context utilize L3 as well as LLC.
	 *
	 * On VLV we don't have L3 controls in the PTEs so we
	 * shouldn't touch the cache level, especially as that
	 * would make the object snooped which might have a
	 * negative performance impact.
	 *
	 * Snooping is required on non-llc platforms in execlist
	 * mode, but since all GGTT accesses use PAT entry 0 we
	 * get snooping anyway regardless of cache_level.
	 *
	 * This is only applicable for Ivy Bridge devices since
	 * later platforms don't have L3 control bits in the PTE.
	 */
	if (IS_IVYBRIDGE(i915))
		i915_gem_object_set_cache_coherency(obj, I915_CACHE_L3_LLC);

	vma = i915_vma_instance(obj, &engine->gt->ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err_obj;
	}

	return vma;

err_obj:
	i915_gem_object_put(obj);
	return ERR_PTR(err);
}

static int ring_context_alloc(struct intel_context *ce)
{
	struct intel_engine_cs *engine = ce->engine;

	/* One ringbuffer to rule them all */
	GEM_BUG_ON(!engine->legacy.ring);
	ce->ring = engine->legacy.ring;
	ce->timeline = intel_timeline_get(engine->legacy.timeline);

	GEM_BUG_ON(ce->state);
	if (engine->context_size) {
		struct i915_vma *vma;

		vma = alloc_context_vma(engine);
		if (IS_ERR(vma))
			return PTR_ERR(vma);

		ce->state = vma;
	}

	return 0;
}

static int ring_context_pin(struct intel_context *ce, void *unused)
{
	return 0;
}

static void ring_context_reset(struct intel_context *ce)
{
	intel_ring_reset(ce->ring, ce->ring->emit);
	clear_bit(CONTEXT_VALID_BIT, &ce->flags);
}

static void ring_context_ban(struct intel_context *ce,
			     struct i915_request *rq)
{
	struct intel_engine_cs *engine;

	if (!rq || !i915_request_is_active(rq))
		return;

	engine = rq->engine;
	lockdep_assert_held(&engine->sched_engine->lock);
	list_for_each_entry_continue(rq, &engine->sched_engine->requests,
				     sched.link)
		if (rq->context == ce) {
			i915_request_set_error_once(rq, -EIO);
			__i915_request_skip(rq);
		}
}

static void ring_context_cancel_request(struct intel_context *ce,
					struct i915_request *rq)
{
	struct intel_engine_cs *engine = NULL;

	i915_request_active_engine(rq, &engine);

	if (engine && intel_engine_pulse(engine))
		intel_gt_handle_error(engine->gt, engine->mask, 0,
				      "request cancellation by %s",
				      current->comm);
}

static const struct intel_context_ops ring_context_ops = {
	.alloc = ring_context_alloc,

	.cancel_request = ring_context_cancel_request,

	.ban = ring_context_ban,

	.pre_pin = ring_context_pre_pin,
	.pin = ring_context_pin,
	.unpin = ring_context_unpin,
	.post_unpin = ring_context_post_unpin,

	.enter = intel_context_enter_engine,
	.exit = intel_context_exit_engine,

	.reset = ring_context_reset,
	.destroy = ring_context_destroy,
};

static int load_pd_dir(struct i915_request *rq,
		       struct i915_address_space *vm,
		       u32 valid)
{
	const struct intel_engine_cs * const engine = rq->engine;
	u32 *cs;

	cs = intel_ring_begin(rq, 12);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_LOAD_REGISTER_IMM(1);
	*cs++ = i915_mmio_reg_offset(RING_PP_DIR_DCLV(engine->mmio_base));
	*cs++ = valid;

	*cs++ = MI_LOAD_REGISTER_IMM(1);
	*cs++ = i915_mmio_reg_offset(RING_PP_DIR_BASE(engine->mmio_base));
	*cs++ = pp_dir(vm);

	/* Stall until the page table load is complete? */
	*cs++ = MI_STORE_REGISTER_MEM | MI_SRM_LRM_GLOBAL_GTT;
	*cs++ = i915_mmio_reg_offset(RING_PP_DIR_BASE(engine->mmio_base));
	*cs++ = intel_gt_scratch_offset(engine->gt,
					INTEL_GT_SCRATCH_FIELD_DEFAULT);

	*cs++ = MI_LOAD_REGISTER_IMM(1);
	*cs++ = i915_mmio_reg_offset(RING_INSTPM(engine->mmio_base));
	*cs++ = _MASKED_BIT_ENABLE(INSTPM_TLB_INVALIDATE);

	intel_ring_advance(rq, cs);

	return rq->engine->emit_flush(rq, EMIT_FLUSH);
}

static int mi_set_context(struct i915_request *rq,
			  struct intel_context *ce,
			  u32 flags)
{
	struct intel_engine_cs *engine = rq->engine;
	struct drm_i915_private *i915 = engine->i915;
	enum intel_engine_id id;
	const int num_engines =
		IS_HASWELL(i915) ? engine->gt->info.num_engines - 1 : 0;
	bool force_restore = false;
	int len;
	u32 *cs;

	len = 4;
	if (GRAPHICS_VER(i915) == 7)
		len += 2 + (num_engines ? 4 * num_engines + 6 : 0);
	else if (GRAPHICS_VER(i915) == 5)
		len += 2;
	if (flags & MI_FORCE_RESTORE) {
		GEM_BUG_ON(flags & MI_RESTORE_INHIBIT);
		flags &= ~MI_FORCE_RESTORE;
		force_restore = true;
		len += 2;
	}

	cs = intel_ring_begin(rq, len);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	/* WaProgramMiArbOnOffAroundMiSetContext:ivb,vlv,hsw,bdw,chv */
	if (GRAPHICS_VER(i915) == 7) {
		*cs++ = MI_ARB_ON_OFF | MI_ARB_DISABLE;
		if (num_engines) {
			struct intel_engine_cs *signaller;

			*cs++ = MI_LOAD_REGISTER_IMM(num_engines);
			for_each_engine(signaller, engine->gt, id) {
				if (signaller == engine)
					continue;

				*cs++ = i915_mmio_reg_offset(
					   RING_PSMI_CTL(signaller->mmio_base));
				*cs++ = _MASKED_BIT_ENABLE(
						GEN6_PSMI_SLEEP_MSG_DISABLE);
			}
		}
	} else if (GRAPHICS_VER(i915) == 5) {
		/*
		 * This w/a is only listed for pre-production ilk a/b steppings,
		 * but is also mentioned for programming the powerctx. To be
		 * safe, just apply the workaround; we do not use SyncFlush so
		 * this should never take effect and so be a no-op!
		 */
		*cs++ = MI_SUSPEND_FLUSH | MI_SUSPEND_FLUSH_EN;
	}

	if (force_restore) {
		/*
		 * The HW doesn't handle being told to restore the current
		 * context very well. Quite often it likes goes to go off and
		 * sulk, especially when it is meant to be reloading PP_DIR.
		 * A very simple fix to force the reload is to simply switch
		 * away from the current context and back again.
		 *
		 * Note that the kernel_context will contain random state
		 * following the INHIBIT_RESTORE. We accept this since we
		 * never use the kernel_context state; it is merely a
		 * placeholder we use to flush other contexts.
		 */
		*cs++ = MI_SET_CONTEXT;
		*cs++ = i915_ggtt_offset(engine->kernel_context->state) |
			MI_MM_SPACE_GTT |
			MI_RESTORE_INHIBIT;
	}

	*cs++ = MI_NOOP;
	*cs++ = MI_SET_CONTEXT;
	*cs++ = i915_ggtt_offset(ce->state) | flags;
	/*
	 * w/a: MI_SET_CONTEXT must always be followed by MI_NOOP
	 * WaMiSetContext_Hang:snb,ivb,vlv
	 */
	*cs++ = MI_NOOP;

	if (GRAPHICS_VER(i915) == 7) {
		if (num_engines) {
			struct intel_engine_cs *signaller;
			i915_reg_t last_reg = {}; /* keep gcc quiet */

			*cs++ = MI_LOAD_REGISTER_IMM(num_engines);
			for_each_engine(signaller, engine->gt, id) {
				if (signaller == engine)
					continue;

				last_reg = RING_PSMI_CTL(signaller->mmio_base);
				*cs++ = i915_mmio_reg_offset(last_reg);
				*cs++ = _MASKED_BIT_DISABLE(
						GEN6_PSMI_SLEEP_MSG_DISABLE);
			}

			/* Insert a delay before the next switch! */
			*cs++ = MI_STORE_REGISTER_MEM | MI_SRM_LRM_GLOBAL_GTT;
			*cs++ = i915_mmio_reg_offset(last_reg);
			*cs++ = intel_gt_scratch_offset(engine->gt,
							INTEL_GT_SCRATCH_FIELD_DEFAULT);
			*cs++ = MI_NOOP;
		}
		*cs++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;
	} else if (GRAPHICS_VER(i915) == 5) {
		*cs++ = MI_SUSPEND_FLUSH;
	}

	intel_ring_advance(rq, cs);

	return 0;
}

static int remap_l3_slice(struct i915_request *rq, int slice)
{
#define L3LOG_DW (GEN7_L3LOG_SIZE / sizeof(u32))
	u32 *cs, *remap_info = rq->engine->i915->l3_parity.remap_info[slice];
	int i;

	if (!remap_info)
		return 0;

	cs = intel_ring_begin(rq, L3LOG_DW * 2 + 2);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	/*
	 * Note: We do not worry about the concurrent register cacheline hang
	 * here because no other code should access these registers other than
	 * at initialization time.
	 */
	*cs++ = MI_LOAD_REGISTER_IMM(L3LOG_DW);
	for (i = 0; i < L3LOG_DW; i++) {
		*cs++ = i915_mmio_reg_offset(GEN7_L3LOG(slice, i));
		*cs++ = remap_info[i];
	}
	*cs++ = MI_NOOP;
	intel_ring_advance(rq, cs);

	return 0;
#undef L3LOG_DW
}

static int remap_l3(struct i915_request *rq)
{
	struct i915_gem_context *ctx = i915_request_gem_context(rq);
	int i, err;

	if (!ctx || !ctx->remap_slice)
		return 0;

	for (i = 0; i < MAX_L3_SLICES; i++) {
		if (!(ctx->remap_slice & BIT(i)))
			continue;

		err = remap_l3_slice(rq, i);
		if (err)
			return err;
	}

	ctx->remap_slice = 0;
	return 0;
}

static int switch_mm(struct i915_request *rq, struct i915_address_space *vm)
{
	int ret;

	if (!vm)
		return 0;

	ret = rq->engine->emit_flush(rq, EMIT_FLUSH);
	if (ret)
		return ret;

	/*
	 * Not only do we need a full barrier (post-sync write) after
	 * invalidating the TLBs, but we need to wait a little bit
	 * longer. Whether this is merely delaying us, or the
	 * subsequent flush is a key part of serialising with the
	 * post-sync op, this extra pass appears vital before a
	 * mm switch!
	 */
	ret = load_pd_dir(rq, vm, PP_DIR_DCLV_2G);
	if (ret)
		return ret;

	return rq->engine->emit_flush(rq, EMIT_INVALIDATE);
}

static int clear_residuals(struct i915_request *rq)
{
	struct intel_engine_cs *engine = rq->engine;
	int ret;

	ret = switch_mm(rq, vm_alias(engine->kernel_context->vm));
	if (ret)
		return ret;

	if (engine->kernel_context->state) {
		ret = mi_set_context(rq,
				     engine->kernel_context,
				     MI_MM_SPACE_GTT | MI_RESTORE_INHIBIT);
		if (ret)
			return ret;
	}

	ret = engine->emit_bb_start(rq,
				    engine->wa_ctx.vma->node.start, 0,
				    0);
	if (ret)
		return ret;

	ret = engine->emit_flush(rq, EMIT_FLUSH);
	if (ret)
		return ret;

	/* Always invalidate before the next switch_mm() */
	return engine->emit_flush(rq, EMIT_INVALIDATE);
}

static int switch_context(struct i915_request *rq)
{
	struct intel_engine_cs *engine = rq->engine;
	struct intel_context *ce = rq->context;
	void **residuals = NULL;
	int ret;

	GEM_BUG_ON(HAS_EXECLISTS(engine->i915));

	if (engine->wa_ctx.vma && ce != engine->kernel_context) {
		if (engine->wa_ctx.vma->private != ce &&
		    i915_mitigate_clear_residuals()) {
			ret = clear_residuals(rq);
			if (ret)
				return ret;

			residuals = &engine->wa_ctx.vma->private;
		}
	}

	ret = switch_mm(rq, vm_alias(ce->vm));
	if (ret)
		return ret;

	if (ce->state) {
		u32 flags;

		GEM_BUG_ON(engine->id != RCS0);

		/* For resource streamer on HSW+ and power context elsewhere */
		BUILD_BUG_ON(HSW_MI_RS_SAVE_STATE_EN != MI_SAVE_EXT_STATE_EN);
		BUILD_BUG_ON(HSW_MI_RS_RESTORE_STATE_EN != MI_RESTORE_EXT_STATE_EN);

		flags = MI_SAVE_EXT_STATE_EN | MI_MM_SPACE_GTT;
		if (test_bit(CONTEXT_VALID_BIT, &ce->flags))
			flags |= MI_RESTORE_EXT_STATE_EN;
		else
			flags |= MI_RESTORE_INHIBIT;

		ret = mi_set_context(rq, ce, flags);
		if (ret)
			return ret;
	}

	ret = remap_l3(rq);
	if (ret)
		return ret;

	/*
	 * Now past the point of no return, this request _will_ be emitted.
	 *
	 * Or at least this preamble will be emitted, the request may be
	 * interrupted prior to submitting the user payload. If so, we
	 * still submit the "empty" request in order to preserve global
	 * state tracking such as this, our tracking of the current
	 * dirty context.
	 */
	if (residuals) {
		intel_context_put(*residuals);
		*residuals = intel_context_get(ce);
	}

	return 0;
}

static int ring_request_alloc(struct i915_request *request)
{
	int ret;

	GEM_BUG_ON(!intel_context_is_pinned(request->context));
	GEM_BUG_ON(i915_request_timeline(request)->has_initial_breadcrumb);

	/*
	 * Flush enough space to reduce the likelihood of waiting after
	 * we start building the request - in which case we will just
	 * have to repeat work.
	 */
	request->reserved_space += LEGACY_REQUEST_SIZE;

	/* Unconditionally invalidate GPU caches and TLBs. */
	ret = request->engine->emit_flush(request, EMIT_INVALIDATE);
	if (ret)
		return ret;

	ret = switch_context(request);
	if (ret)
		return ret;

	request->reserved_space -= LEGACY_REQUEST_SIZE;
	return 0;
}

static void gen6_bsd_submit_request(struct i915_request *request)
{
	struct intel_uncore *uncore = request->engine->uncore;

	intel_uncore_forcewake_get(uncore, FORCEWAKE_ALL);

       /* Every tail move must follow the sequence below */

	/* Disable notification that the ring is IDLE. The GT
	 * will then assume that it is busy and bring it out of rc6.
	 */
	intel_uncore_write_fw(uncore, RING_PSMI_CTL(GEN6_BSD_RING_BASE),
			      _MASKED_BIT_ENABLE(GEN6_PSMI_SLEEP_MSG_DISABLE));

	/* Clear the context id. Here be magic! */
	intel_uncore_write64_fw(uncore, GEN6_BSD_RNCID, 0x0);

	/* Wait for the ring not to be idle, i.e. for it to wake up. */
	if (__intel_wait_for_register_fw(uncore,
					 RING_PSMI_CTL(GEN6_BSD_RING_BASE),
					 GEN6_BSD_SLEEP_INDICATOR,
					 0,
					 1000, 0, NULL))
		drm_err(&uncore->i915->drm,
			"timed out waiting for the BSD ring to wake up\n");

	/* Now that the ring is fully powered up, update the tail */
	i9xx_submit_request(request);

	/* Let the ring send IDLE messages to the GT again,
	 * and so let it sleep to conserve power when idle.
	 */
	intel_uncore_write_fw(uncore, RING_PSMI_CTL(GEN6_BSD_RING_BASE),
			      _MASKED_BIT_DISABLE(GEN6_PSMI_SLEEP_MSG_DISABLE));

	intel_uncore_forcewake_put(uncore, FORCEWAKE_ALL);
}

static void i9xx_set_default_submission(struct intel_engine_cs *engine)
{
	engine->submit_request = i9xx_submit_request;
}

static void gen6_bsd_set_default_submission(struct intel_engine_cs *engine)
{
	engine->submit_request = gen6_bsd_submit_request;
}

static void ring_release(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;

	drm_WARN_ON(&dev_priv->drm, GRAPHICS_VER(dev_priv) > 2 &&
		    (ENGINE_READ(engine, RING_MI_MODE) & MODE_IDLE) == 0);

	intel_engine_cleanup_common(engine);

	if (engine->wa_ctx.vma) {
		intel_context_put(engine->wa_ctx.vma->private);
		i915_vma_unpin_and_release(&engine->wa_ctx.vma, 0);
	}

	intel_ring_unpin(engine->legacy.ring);
	intel_ring_put(engine->legacy.ring);

	intel_timeline_unpin(engine->legacy.timeline);
	intel_timeline_put(engine->legacy.timeline);
}

static void irq_handler(struct intel_engine_cs *engine, u16 iir)
{
	intel_engine_signal_breadcrumbs(engine);
}

static void setup_irq(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;

	intel_engine_set_irq_handler(engine, irq_handler);

	if (GRAPHICS_VER(i915) >= 6) {
		engine->irq_enable = gen6_irq_enable;
		engine->irq_disable = gen6_irq_disable;
	} else if (GRAPHICS_VER(i915) >= 5) {
		engine->irq_enable = gen5_irq_enable;
		engine->irq_disable = gen5_irq_disable;
	} else if (GRAPHICS_VER(i915) >= 3) {
		engine->irq_enable = gen3_irq_enable;
		engine->irq_disable = gen3_irq_disable;
	} else {
		engine->irq_enable = gen2_irq_enable;
		engine->irq_disable = gen2_irq_disable;
	}
}

static void add_to_engine(struct i915_request *rq)
{
	lockdep_assert_held(&rq->engine->sched_engine->lock);
	list_move_tail(&rq->sched.link, &rq->engine->sched_engine->requests);
}

static void remove_from_engine(struct i915_request *rq)
{
	spin_lock_irq(&rq->engine->sched_engine->lock);
	list_del_init(&rq->sched.link);

	/* Prevent further __await_execution() registering a cb, then flush */
	set_bit(I915_FENCE_FLAG_ACTIVE, &rq->fence.flags);

	spin_unlock_irq(&rq->engine->sched_engine->lock);

	i915_request_notify_execute_cb_imm(rq);
}

static void setup_common(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;

	/* gen8+ are only supported with execlists */
	GEM_BUG_ON(GRAPHICS_VER(i915) >= 8);

	setup_irq(engine);

	engine->resume = xcs_resume;
	engine->sanitize = xcs_sanitize;

	engine->reset.prepare = reset_prepare;
	engine->reset.rewind = reset_rewind;
	engine->reset.cancel = reset_cancel;
	engine->reset.finish = reset_finish;

	engine->add_active_request = add_to_engine;
	engine->remove_active_request = remove_from_engine;

	engine->cops = &ring_context_ops;
	engine->request_alloc = ring_request_alloc;

	/*
	 * Using a global execution timeline; the previous final breadcrumb is
	 * equivalent to our next initial bread so we can elide
	 * engine->emit_init_breadcrumb().
	 */
	engine->emit_fini_breadcrumb = gen3_emit_breadcrumb;
	if (GRAPHICS_VER(i915) == 5)
		engine->emit_fini_breadcrumb = gen5_emit_breadcrumb;

	engine->set_default_submission = i9xx_set_default_submission;

	if (GRAPHICS_VER(i915) >= 6)
		engine->emit_bb_start = gen6_emit_bb_start;
	else if (GRAPHICS_VER(i915) >= 4)
		engine->emit_bb_start = gen4_emit_bb_start;
	else if (IS_I830(i915) || IS_I845G(i915))
		engine->emit_bb_start = i830_emit_bb_start;
	else
		engine->emit_bb_start = gen3_emit_bb_start;
}

static void setup_rcs(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;

	if (HAS_L3_DPF(i915))
		engine->irq_keep_mask = GT_RENDER_L3_PARITY_ERROR_INTERRUPT;

	engine->irq_enable_mask = GT_RENDER_USER_INTERRUPT;

	if (GRAPHICS_VER(i915) >= 7) {
		engine->emit_flush = gen7_emit_flush_rcs;
		engine->emit_fini_breadcrumb = gen7_emit_breadcrumb_rcs;
	} else if (GRAPHICS_VER(i915) == 6) {
		engine->emit_flush = gen6_emit_flush_rcs;
		engine->emit_fini_breadcrumb = gen6_emit_breadcrumb_rcs;
	} else if (GRAPHICS_VER(i915) == 5) {
		engine->emit_flush = gen4_emit_flush_rcs;
	} else {
		if (GRAPHICS_VER(i915) < 4)
			engine->emit_flush = gen2_emit_flush;
		else
			engine->emit_flush = gen4_emit_flush_rcs;
		engine->irq_enable_mask = I915_USER_INTERRUPT;
	}

	if (IS_HASWELL(i915))
		engine->emit_bb_start = hsw_emit_bb_start;
}

static void setup_vcs(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;

	if (GRAPHICS_VER(i915) >= 6) {
		/* gen6 bsd needs a special wa for tail updates */
		if (GRAPHICS_VER(i915) == 6)
			engine->set_default_submission = gen6_bsd_set_default_submission;
		engine->emit_flush = gen6_emit_flush_vcs;
		engine->irq_enable_mask = GT_BSD_USER_INTERRUPT;

		if (GRAPHICS_VER(i915) == 6)
			engine->emit_fini_breadcrumb = gen6_emit_breadcrumb_xcs;
		else
			engine->emit_fini_breadcrumb = gen7_emit_breadcrumb_xcs;
	} else {
		engine->emit_flush = gen4_emit_flush_vcs;
		if (GRAPHICS_VER(i915) == 5)
			engine->irq_enable_mask = ILK_BSD_USER_INTERRUPT;
		else
			engine->irq_enable_mask = I915_BSD_USER_INTERRUPT;
	}
}

static void setup_bcs(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;

	engine->emit_flush = gen6_emit_flush_xcs;
	engine->irq_enable_mask = GT_BLT_USER_INTERRUPT;

	if (GRAPHICS_VER(i915) == 6)
		engine->emit_fini_breadcrumb = gen6_emit_breadcrumb_xcs;
	else
		engine->emit_fini_breadcrumb = gen7_emit_breadcrumb_xcs;
}

static void setup_vecs(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;

	GEM_BUG_ON(GRAPHICS_VER(i915) < 7);

	engine->emit_flush = gen6_emit_flush_xcs;
	engine->irq_enable_mask = PM_VEBOX_USER_INTERRUPT;
	engine->irq_enable = hsw_irq_enable_vecs;
	engine->irq_disable = hsw_irq_disable_vecs;

	engine->emit_fini_breadcrumb = gen7_emit_breadcrumb_xcs;
}

static int gen7_ctx_switch_bb_setup(struct intel_engine_cs * const engine,
				    struct i915_vma * const vma)
{
	return gen7_setup_clear_gpr_bb(engine, vma);
}

static int gen7_ctx_switch_bb_init(struct intel_engine_cs *engine,
				   struct i915_gem_ww_ctx *ww,
				   struct i915_vma *vma)
{
	int err;

	err = i915_vma_pin_ww(vma, ww, 0, 0, PIN_USER | PIN_HIGH);
	if (err)
		return err;

	err = i915_vma_sync(vma);
	if (err)
		goto err_unpin;

	err = gen7_ctx_switch_bb_setup(engine, vma);
	if (err)
		goto err_unpin;

	engine->wa_ctx.vma = vma;
	return 0;

err_unpin:
	i915_vma_unpin(vma);
	return err;
}

static struct i915_vma *gen7_ctx_vma(struct intel_engine_cs *engine)
{
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int size, err;

	if (GRAPHICS_VER(engine->i915) != 7 || engine->class != RENDER_CLASS)
		return NULL;

	err = gen7_ctx_switch_bb_setup(engine, NULL /* probe size */);
	if (err < 0)
		return ERR_PTR(err);
	if (!err)
		return NULL;

	size = ALIGN(err, PAGE_SIZE);

	obj = i915_gem_object_create_internal(engine->i915, size);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	vma = i915_vma_instance(obj, engine->gt->vm, NULL);
	if (IS_ERR(vma)) {
		i915_gem_object_put(obj);
		return ERR_CAST(vma);
	}

	vma->private = intel_context_create(engine); /* dummy residuals */
	if (IS_ERR(vma->private)) {
		err = PTR_ERR(vma->private);
		vma->private = NULL;
		i915_gem_object_put(obj);
		return ERR_PTR(err);
	}

	return vma;
}

int intel_ring_submission_setup(struct intel_engine_cs *engine)
{
	struct i915_gem_ww_ctx ww;
	struct intel_timeline *timeline;
	struct intel_ring *ring;
	struct i915_vma *gen7_wa_vma;
	int err;

	setup_common(engine);

	switch (engine->class) {
	case RENDER_CLASS:
		setup_rcs(engine);
		break;
	case VIDEO_DECODE_CLASS:
		setup_vcs(engine);
		break;
	case COPY_ENGINE_CLASS:
		setup_bcs(engine);
		break;
	case VIDEO_ENHANCEMENT_CLASS:
		setup_vecs(engine);
		break;
	default:
		MISSING_CASE(engine->class);
		return -ENODEV;
	}

	timeline = intel_timeline_create_from_engine(engine,
						     I915_GEM_HWS_SEQNO_ADDR);
	if (IS_ERR(timeline)) {
		err = PTR_ERR(timeline);
		goto err;
	}
	GEM_BUG_ON(timeline->has_initial_breadcrumb);

	ring = intel_engine_create_ring(engine, SZ_16K);
	if (IS_ERR(ring)) {
		err = PTR_ERR(ring);
		goto err_timeline;
	}

	GEM_BUG_ON(engine->legacy.ring);
	engine->legacy.ring = ring;
	engine->legacy.timeline = timeline;

	gen7_wa_vma = gen7_ctx_vma(engine);
	if (IS_ERR(gen7_wa_vma)) {
		err = PTR_ERR(gen7_wa_vma);
		goto err_ring;
	}

	i915_gem_ww_ctx_init(&ww, false);

retry:
	err = i915_gem_object_lock(timeline->hwsp_ggtt->obj, &ww);
	if (!err && gen7_wa_vma)
		err = i915_gem_object_lock(gen7_wa_vma->obj, &ww);
	if (!err)
		err = i915_gem_object_lock(engine->legacy.ring->vma->obj, &ww);
	if (!err)
		err = intel_timeline_pin(timeline, &ww);
	if (!err) {
		err = intel_ring_pin(ring, &ww);
		if (err)
			intel_timeline_unpin(timeline);
	}
	if (err)
		goto out;

	GEM_BUG_ON(timeline->hwsp_ggtt != engine->status_page.vma);

	if (gen7_wa_vma) {
		err = gen7_ctx_switch_bb_init(engine, &ww, gen7_wa_vma);
		if (err) {
			intel_ring_unpin(ring);
			intel_timeline_unpin(timeline);
		}
	}

out:
	if (err == -EDEADLK) {
		err = i915_gem_ww_ctx_backoff(&ww);
		if (!err)
			goto retry;
	}
	i915_gem_ww_ctx_fini(&ww);
	if (err)
		goto err_gen7_put;

	/* Finally, take ownership and responsibility for cleanup! */
	engine->release = ring_release;

	return 0;

err_gen7_put:
	if (gen7_wa_vma) {
		intel_context_put(gen7_wa_vma->private);
		i915_gem_object_put(gen7_wa_vma->obj);
	}
err_ring:
	intel_ring_put(ring);
err_timeline:
	intel_timeline_put(timeline);
err:
	intel_engine_cleanup_common(engine);
	return err;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftest_ring_submission.c"
#endif
