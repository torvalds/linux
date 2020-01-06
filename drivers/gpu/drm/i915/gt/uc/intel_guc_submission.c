// SPDX-License-Identifier: MIT
/*
 * Copyright © 2014 Intel Corporation
 */

#include <linux/circ_buf.h>

#include "gem/i915_gem_context.h"
#include "gt/intel_context.h"
#include "gt/intel_engine_pm.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_pm.h"
#include "gt/intel_lrc_reg.h"
#include "gt/intel_ring.h"

#include "intel_guc_submission.h"

#include "i915_drv.h"
#include "i915_trace.h"

/**
 * DOC: GuC-based command submission
 *
 * IMPORTANT NOTE: GuC submission is currently not supported in i915. The GuC
 * firmware is moving to an updated submission interface and we plan to
 * turn submission back on when that lands. The below documentation (and related
 * code) matches the old submission model and will be updated as part of the
 * upgrade to the new flow.
 *
 * GuC stage descriptor:
 * During initialization, the driver allocates a static pool of 1024 such
 * descriptors, and shares them with the GuC. Currently, we only use one
 * descriptor. This stage descriptor lets the GuC know about the workqueue and
 * process descriptor. Theoretically, it also lets the GuC know about our HW
 * contexts (context ID, etc...), but we actually employ a kind of submission
 * where the GuC uses the LRCA sent via the work item instead. This is called
 * a "proxy" submission.
 *
 * The Scratch registers:
 * There are 16 MMIO-based registers start from 0xC180. The kernel driver writes
 * a value to the action register (SOFT_SCRATCH_0) along with any data. It then
 * triggers an interrupt on the GuC via another register write (0xC4C8).
 * Firmware writes a success/fail code back to the action register after
 * processes the request. The kernel driver polls waiting for this update and
 * then proceeds.
 *
 * Work Items:
 * There are several types of work items that the host may place into a
 * workqueue, each with its own requirements and limitations. Currently only
 * WQ_TYPE_INORDER is needed to support legacy submission via GuC, which
 * represents in-order queue. The kernel driver packs ring tail pointer and an
 * ELSP context descriptor dword into Work Item.
 * See guc_add_request()
 *
 */

static inline struct i915_priolist *to_priolist(struct rb_node *rb)
{
	return rb_entry(rb, struct i915_priolist, node);
}

static struct guc_stage_desc *__get_stage_desc(struct intel_guc *guc, u32 id)
{
	struct guc_stage_desc *base = guc->stage_desc_pool_vaddr;

	return &base[id];
}

static int guc_workqueue_create(struct intel_guc *guc)
{
	return intel_guc_allocate_and_map_vma(guc, GUC_WQ_SIZE, &guc->workqueue,
					      &guc->workqueue_vaddr);
}

static void guc_workqueue_destroy(struct intel_guc *guc)
{
	i915_vma_unpin_and_release(&guc->workqueue, I915_VMA_RELEASE_MAP);
}

/*
 * Initialise the process descriptor shared with the GuC firmware.
 */
static int guc_proc_desc_create(struct intel_guc *guc)
{
	const u32 size = PAGE_ALIGN(sizeof(struct guc_process_desc));

	return intel_guc_allocate_and_map_vma(guc, size, &guc->proc_desc,
					      &guc->proc_desc_vaddr);
}

static void guc_proc_desc_destroy(struct intel_guc *guc)
{
	i915_vma_unpin_and_release(&guc->proc_desc, I915_VMA_RELEASE_MAP);
}

static void guc_proc_desc_init(struct intel_guc *guc)
{
	struct guc_process_desc *desc;

	desc = memset(guc->proc_desc_vaddr, 0, sizeof(*desc));

	/*
	 * XXX: pDoorbell and WQVBaseAddress are pointers in process address
	 * space for ring3 clients (set them as in mmap_ioctl) or kernel
	 * space for kernel clients (map on demand instead? May make debug
	 * easier to have it mapped).
	 */
	desc->wq_base_addr = 0;
	desc->db_base_addr = 0;

	desc->wq_size_bytes = GUC_WQ_SIZE;
	desc->wq_status = WQ_STATUS_ACTIVE;
	desc->priority = GUC_CLIENT_PRIORITY_KMD_NORMAL;
}

static void guc_proc_desc_fini(struct intel_guc *guc)
{
	memset(guc->proc_desc_vaddr, 0, sizeof(struct guc_process_desc));
}

static int guc_stage_desc_pool_create(struct intel_guc *guc)
{
	u32 size = PAGE_ALIGN(sizeof(struct guc_stage_desc) *
			      GUC_MAX_STAGE_DESCRIPTORS);

	return intel_guc_allocate_and_map_vma(guc, size, &guc->stage_desc_pool,
					      &guc->stage_desc_pool_vaddr);
}

static void guc_stage_desc_pool_destroy(struct intel_guc *guc)
{
	i915_vma_unpin_and_release(&guc->stage_desc_pool, I915_VMA_RELEASE_MAP);
}

/*
 * Initialise/clear the stage descriptor shared with the GuC firmware.
 *
 * This descriptor tells the GuC where (in GGTT space) to find the important
 * data structures related to work submission (process descriptor, write queue,
 * etc).
 */
static void guc_stage_desc_init(struct intel_guc *guc)
{
	struct guc_stage_desc *desc;

	/* we only use 1 stage desc, so hardcode it to 0 */
	desc = __get_stage_desc(guc, 0);
	memset(desc, 0, sizeof(*desc));

	desc->attribute = GUC_STAGE_DESC_ATTR_ACTIVE |
			  GUC_STAGE_DESC_ATTR_KERNEL;

	desc->stage_id = 0;
	desc->priority = GUC_CLIENT_PRIORITY_KMD_NORMAL;

	desc->process_desc = intel_guc_ggtt_offset(guc, guc->proc_desc);
	desc->wq_addr = intel_guc_ggtt_offset(guc, guc->workqueue);
	desc->wq_size = GUC_WQ_SIZE;
}

static void guc_stage_desc_fini(struct intel_guc *guc)
{
	struct guc_stage_desc *desc;

	desc = __get_stage_desc(guc, 0);
	memset(desc, 0, sizeof(*desc));
}

/* Construct a Work Item and append it to the GuC's Work Queue */
static void guc_wq_item_append(struct intel_guc *guc,
			       u32 target_engine, u32 context_desc,
			       u32 ring_tail, u32 fence_id)
{
	/* wqi_len is in DWords, and does not include the one-word header */
	const size_t wqi_size = sizeof(struct guc_wq_item);
	const u32 wqi_len = wqi_size / sizeof(u32) - 1;
	struct guc_process_desc *desc = guc->proc_desc_vaddr;
	struct guc_wq_item *wqi;
	u32 wq_off;

	lockdep_assert_held(&guc->wq_lock);

	/* For now workqueue item is 4 DWs; workqueue buffer is 2 pages. So we
	 * should not have the case where structure wqi is across page, neither
	 * wrapped to the beginning. This simplifies the implementation below.
	 *
	 * XXX: if not the case, we need save data to a temp wqi and copy it to
	 * workqueue buffer dw by dw.
	 */
	BUILD_BUG_ON(wqi_size != 16);

	/* We expect the WQ to be active if we're appending items to it */
	GEM_BUG_ON(desc->wq_status != WQ_STATUS_ACTIVE);

	/* Free space is guaranteed. */
	wq_off = READ_ONCE(desc->tail);
	GEM_BUG_ON(CIRC_SPACE(wq_off, READ_ONCE(desc->head),
			      GUC_WQ_SIZE) < wqi_size);
	GEM_BUG_ON(wq_off & (wqi_size - 1));

	wqi = guc->workqueue_vaddr + wq_off;

	/* Now fill in the 4-word work queue item */
	wqi->header = WQ_TYPE_INORDER |
		      (wqi_len << WQ_LEN_SHIFT) |
		      (target_engine << WQ_TARGET_SHIFT) |
		      WQ_NO_WCFLUSH_WAIT;
	wqi->context_desc = context_desc;
	wqi->submit_element_info = ring_tail << WQ_RING_TAIL_SHIFT;
	GEM_BUG_ON(ring_tail > WQ_RING_TAIL_MAX);
	wqi->fence_id = fence_id;

	/* Make the update visible to GuC */
	WRITE_ONCE(desc->tail, (wq_off + wqi_size) & (GUC_WQ_SIZE - 1));
}

static void guc_add_request(struct intel_guc *guc, struct i915_request *rq)
{
	struct intel_engine_cs *engine = rq->engine;
	u32 ctx_desc = lower_32_bits(rq->context->lrc_desc);
	u32 ring_tail = intel_ring_set_tail(rq->ring, rq->tail) / sizeof(u64);

	guc_wq_item_append(guc, engine->guc_id, ctx_desc,
			   ring_tail, rq->fence.seqno);
}

/*
 * When we're doing submissions using regular execlists backend, writing to
 * ELSP from CPU side is enough to make sure that writes to ringbuffer pages
 * pinned in mappable aperture portion of GGTT are visible to command streamer.
 * Writes done by GuC on our behalf are not guaranteeing such ordering,
 * therefore, to ensure the flush, we're issuing a POSTING READ.
 */
static void flush_ggtt_writes(struct i915_vma *vma)
{
	if (i915_vma_is_map_and_fenceable(vma))
		intel_uncore_posting_read_fw(vma->vm->gt->uncore,
					     GUC_STATUS);
}

static void guc_submit(struct intel_engine_cs *engine,
		       struct i915_request **out,
		       struct i915_request **end)
{
	struct intel_guc *guc = &engine->gt->uc.guc;

	spin_lock(&guc->wq_lock);

	do {
		struct i915_request *rq = *out++;

		flush_ggtt_writes(rq->ring->vma);
		guc_add_request(guc, rq);
	} while (out != end);

	spin_unlock(&guc->wq_lock);
}

static inline int rq_prio(const struct i915_request *rq)
{
	return rq->sched.attr.priority | __NO_PREEMPTION;
}

static struct i915_request *schedule_in(struct i915_request *rq, int idx)
{
	trace_i915_request_in(rq, idx);

	/*
	 * Currently we are not tracking the rq->context being inflight
	 * (ce->inflight = rq->engine). It is only used by the execlists
	 * backend at the moment, a similar counting strategy would be
	 * required if we generalise the inflight tracking.
	 */

	__intel_gt_pm_get(rq->engine->gt);
	return i915_request_get(rq);
}

static void schedule_out(struct i915_request *rq)
{
	trace_i915_request_out(rq);

	intel_gt_pm_put_async(rq->engine->gt);
	i915_request_put(rq);
}

static void __guc_dequeue(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;
	struct i915_request **first = execlists->inflight;
	struct i915_request ** const last_port = first + execlists->port_mask;
	struct i915_request *last = first[0];
	struct i915_request **port;
	bool submit = false;
	struct rb_node *rb;

	lockdep_assert_held(&engine->active.lock);

	if (last) {
		if (*++first)
			return;

		last = NULL;
	}

	/*
	 * We write directly into the execlists->inflight queue and don't use
	 * the execlists->pending queue, as we don't have a distinct switch
	 * event.
	 */
	port = first;
	while ((rb = rb_first_cached(&execlists->queue))) {
		struct i915_priolist *p = to_priolist(rb);
		struct i915_request *rq, *rn;
		int i;

		priolist_for_each_request_consume(rq, rn, p, i) {
			if (last && rq->context != last->context) {
				if (port == last_port)
					goto done;

				*port = schedule_in(last,
						    port - execlists->inflight);
				port++;
			}

			list_del_init(&rq->sched.link);
			__i915_request_submit(rq);
			submit = true;
			last = rq;
		}

		rb_erase_cached(&p->node, &execlists->queue);
		i915_priolist_free(p);
	}
done:
	execlists->queue_priority_hint =
		rb ? to_priolist(rb)->priority : INT_MIN;
	if (submit) {
		*port = schedule_in(last, port - execlists->inflight);
		*++port = NULL;
		guc_submit(engine, first, port);
	}
	execlists->active = execlists->inflight;
}

static void guc_submission_tasklet(unsigned long data)
{
	struct intel_engine_cs * const engine = (struct intel_engine_cs *)data;
	struct intel_engine_execlists * const execlists = &engine->execlists;
	struct i915_request **port, *rq;
	unsigned long flags;

	spin_lock_irqsave(&engine->active.lock, flags);

	for (port = execlists->inflight; (rq = *port); port++) {
		if (!i915_request_completed(rq))
			break;

		schedule_out(rq);
	}
	if (port != execlists->inflight) {
		int idx = port - execlists->inflight;
		int rem = ARRAY_SIZE(execlists->inflight) - idx;
		memmove(execlists->inflight, port, rem * sizeof(*port));
	}

	__guc_dequeue(engine);

	spin_unlock_irqrestore(&engine->active.lock, flags);
}

static void guc_reset_prepare(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;

	ENGINE_TRACE(engine, "\n");

	/*
	 * Prevent request submission to the hardware until we have
	 * completed the reset in i915_gem_reset_finish(). If a request
	 * is completed by one engine, it may then queue a request
	 * to a second via its execlists->tasklet *just* as we are
	 * calling engine->init_hw() and also writing the ELSP.
	 * Turning off the execlists->tasklet until the reset is over
	 * prevents the race.
	 */
	__tasklet_disable_sync_once(&execlists->tasklet);
}

static void
cancel_port_requests(struct intel_engine_execlists * const execlists)
{
	struct i915_request * const *port, *rq;

	/* Note we are only using the inflight and not the pending queue */

	for (port = execlists->active; (rq = *port); port++)
		schedule_out(rq);
	execlists->active =
		memset(execlists->inflight, 0, sizeof(execlists->inflight));
}

static void guc_reset_rewind(struct intel_engine_cs *engine, bool stalled)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;
	struct i915_request *rq;
	unsigned long flags;

	spin_lock_irqsave(&engine->active.lock, flags);

	cancel_port_requests(execlists);

	/* Push back any incomplete requests for replay after the reset. */
	rq = execlists_unwind_incomplete_requests(execlists);
	if (!rq)
		goto out_unlock;

	if (!i915_request_started(rq))
		stalled = false;

	__i915_request_reset(rq, stalled);
	intel_lr_context_reset(engine, rq->context, rq->head, stalled);

out_unlock:
	spin_unlock_irqrestore(&engine->active.lock, flags);
}

static void guc_reset_cancel(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;
	struct i915_request *rq, *rn;
	struct rb_node *rb;
	unsigned long flags;

	ENGINE_TRACE(engine, "\n");

	/*
	 * Before we call engine->cancel_requests(), we should have exclusive
	 * access to the submission state. This is arranged for us by the
	 * caller disabling the interrupt generation, the tasklet and other
	 * threads that may then access the same state, giving us a free hand
	 * to reset state. However, we still need to let lockdep be aware that
	 * we know this state may be accessed in hardirq context, so we
	 * disable the irq around this manipulation and we want to keep
	 * the spinlock focused on its duties and not accidentally conflate
	 * coverage to the submission's irq state. (Similarly, although we
	 * shouldn't need to disable irq around the manipulation of the
	 * submission's irq state, we also wish to remind ourselves that
	 * it is irq state.)
	 */
	spin_lock_irqsave(&engine->active.lock, flags);

	/* Cancel the requests on the HW and clear the ELSP tracker. */
	cancel_port_requests(execlists);

	/* Mark all executing requests as skipped. */
	list_for_each_entry(rq, &engine->active.requests, sched.link) {
		if (!i915_request_signaled(rq))
			dma_fence_set_error(&rq->fence, -EIO);

		i915_request_mark_complete(rq);
	}

	/* Flush the queued requests to the timeline list (for retiring). */
	while ((rb = rb_first_cached(&execlists->queue))) {
		struct i915_priolist *p = to_priolist(rb);
		int i;

		priolist_for_each_request_consume(rq, rn, p, i) {
			list_del_init(&rq->sched.link);
			__i915_request_submit(rq);
			dma_fence_set_error(&rq->fence, -EIO);
			i915_request_mark_complete(rq);
		}

		rb_erase_cached(&p->node, &execlists->queue);
		i915_priolist_free(p);
	}

	/* Remaining _unready_ requests will be nop'ed when submitted */

	execlists->queue_priority_hint = INT_MIN;
	execlists->queue = RB_ROOT_CACHED;

	spin_unlock_irqrestore(&engine->active.lock, flags);
}

static void guc_reset_finish(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;

	if (__tasklet_enable(&execlists->tasklet))
		/* And kick in case we missed a new request submission. */
		tasklet_hi_schedule(&execlists->tasklet);

	ENGINE_TRACE(engine, "depth->%d\n",
		     atomic_read(&execlists->tasklet.count));
}

/*
 * Everything below here is concerned with setup & teardown, and is
 * therefore not part of the somewhat time-critical batch-submission
 * path of guc_submit() above.
 */

/*
 * Set up the memory resources to be shared with the GuC (via the GGTT)
 * at firmware loading time.
 */
int intel_guc_submission_init(struct intel_guc *guc)
{
	int ret;

	if (guc->stage_desc_pool)
		return 0;

	ret = guc_stage_desc_pool_create(guc);
	if (ret)
		return ret;
	/*
	 * Keep static analysers happy, let them know that we allocated the
	 * vma after testing that it didn't exist earlier.
	 */
	GEM_BUG_ON(!guc->stage_desc_pool);

	ret = guc_workqueue_create(guc);
	if (ret)
		goto err_pool;

	ret = guc_proc_desc_create(guc);
	if (ret)
		goto err_workqueue;

	spin_lock_init(&guc->wq_lock);

	return 0;

err_workqueue:
	guc_workqueue_destroy(guc);
err_pool:
	guc_stage_desc_pool_destroy(guc);
	return ret;
}

void intel_guc_submission_fini(struct intel_guc *guc)
{
	if (guc->stage_desc_pool) {
		guc_proc_desc_destroy(guc);
		guc_workqueue_destroy(guc);
		guc_stage_desc_pool_destroy(guc);
	}
}

static void guc_interrupts_capture(struct intel_gt *gt)
{
	struct intel_uncore *uncore = gt->uncore;
	u32 irqs = GT_CONTEXT_SWITCH_INTERRUPT;
	u32 dmask = irqs << 16 | irqs;

	GEM_BUG_ON(INTEL_GEN(gt->i915) < 11);

	/* Don't handle the ctx switch interrupt in GuC submission mode */
	intel_uncore_rmw(uncore, GEN11_RENDER_COPY_INTR_ENABLE, dmask, 0);
	intel_uncore_rmw(uncore, GEN11_VCS_VECS_INTR_ENABLE, dmask, 0);
}

static void guc_interrupts_release(struct intel_gt *gt)
{
	struct intel_uncore *uncore = gt->uncore;
	u32 irqs = GT_CONTEXT_SWITCH_INTERRUPT;
	u32 dmask = irqs << 16 | irqs;

	GEM_BUG_ON(INTEL_GEN(gt->i915) < 11);

	/* Handle ctx switch interrupts again */
	intel_uncore_rmw(uncore, GEN11_RENDER_COPY_INTR_ENABLE, 0, dmask);
	intel_uncore_rmw(uncore, GEN11_VCS_VECS_INTR_ENABLE, 0, dmask);
}

static void guc_set_default_submission(struct intel_engine_cs *engine)
{
	/*
	 * We inherit a bunch of functions from execlists that we'd like
	 * to keep using:
	 *
	 *    engine->submit_request = execlists_submit_request;
	 *    engine->cancel_requests = execlists_cancel_requests;
	 *    engine->schedule = execlists_schedule;
	 *
	 * But we need to override the actual submission backend in order
	 * to talk to the GuC.
	 */
	intel_execlists_set_default_submission(engine);

	engine->execlists.tasklet.func = guc_submission_tasklet;

	/* do not use execlists park/unpark */
	engine->park = engine->unpark = NULL;

	engine->reset.prepare = guc_reset_prepare;
	engine->reset.rewind = guc_reset_rewind;
	engine->reset.cancel = guc_reset_cancel;
	engine->reset.finish = guc_reset_finish;

	engine->flags &= ~I915_ENGINE_SUPPORTS_STATS;
	engine->flags |= I915_ENGINE_NEEDS_BREADCRUMB_TASKLET;

	/*
	 * For the breadcrumb irq to work we need the interrupts to stay
	 * enabled. However, on all platforms on which we'll have support for
	 * GuC submission we don't allow disabling the interrupts at runtime, so
	 * we're always safe with the current flow.
	 */
	GEM_BUG_ON(engine->irq_enable || engine->irq_disable);
}

void intel_guc_submission_enable(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/*
	 * We're using GuC work items for submitting work through GuC. Since
	 * we're coalescing multiple requests from a single context into a
	 * single work item prior to assigning it to execlist_port, we can
	 * never have more work items than the total number of ports (for all
	 * engines). The GuC firmware is controlling the HEAD of work queue,
	 * and it is guaranteed that it will remove the work item from the
	 * queue before our request is completed.
	 */
	BUILD_BUG_ON(ARRAY_SIZE(engine->execlists.inflight) *
		     sizeof(struct guc_wq_item) *
		     I915_NUM_ENGINES > GUC_WQ_SIZE);

	guc_proc_desc_init(guc);
	guc_stage_desc_init(guc);

	/* Take over from manual control of ELSP (execlists) */
	guc_interrupts_capture(gt);

	for_each_engine(engine, gt, id) {
		engine->set_default_submission = guc_set_default_submission;
		engine->set_default_submission(engine);
	}
}

void intel_guc_submission_disable(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);

	GEM_BUG_ON(gt->awake); /* GT should be parked first */

	/* Note: By the time we're here, GuC may have already been reset */

	guc_interrupts_release(gt);

	guc_stage_desc_fini(guc);
	guc_proc_desc_fini(guc);
}

static bool __guc_submission_support(struct intel_guc *guc)
{
	/* XXX: GuC submission is unavailable for now */
	return false;

	if (!intel_guc_is_supported(guc))
		return false;

	return i915_modparams.enable_guc & ENABLE_GUC_SUBMISSION;
}

void intel_guc_submission_init_early(struct intel_guc *guc)
{
	guc->submission_supported = __guc_submission_support(guc);
}

bool intel_engine_in_guc_submission_mode(const struct intel_engine_cs *engine)
{
	return engine->set_default_submission == guc_set_default_submission;
}
