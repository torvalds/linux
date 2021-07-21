// SPDX-License-Identifier: MIT
/*
 * Copyright © 2014 Intel Corporation
 */

#include <linux/circ_buf.h>

#include "gem/i915_gem_context.h"
#include "gt/gen8_engine_cs.h"
#include "gt/intel_breadcrumbs.h"
#include "gt/intel_context.h"
#include "gt/intel_engine_pm.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_irq.h"
#include "gt/intel_gt_pm.h"
#include "gt/intel_gt_requests.h"
#include "gt/intel_lrc.h"
#include "gt/intel_lrc_reg.h"
#include "gt/intel_mocs.h"
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

#define GUC_REQUEST_SIZE 64 /* bytes */

/*
 * Below is a set of functions which control the GuC scheduling state which do
 * not require a lock as all state transitions are mutually exclusive. i.e. It
 * is not possible for the context pinning code and submission, for the same
 * context, to be executing simultaneously. We still need an atomic as it is
 * possible for some of the bits to changing at the same time though.
 */
#define SCHED_STATE_NO_LOCK_ENABLED			BIT(0)
static inline bool context_enabled(struct intel_context *ce)
{
	return (atomic_read(&ce->guc_sched_state_no_lock) &
		SCHED_STATE_NO_LOCK_ENABLED);
}

static inline void set_context_enabled(struct intel_context *ce)
{
	atomic_or(SCHED_STATE_NO_LOCK_ENABLED, &ce->guc_sched_state_no_lock);
}

static inline void clr_context_enabled(struct intel_context *ce)
{
	atomic_and((u32)~SCHED_STATE_NO_LOCK_ENABLED,
		   &ce->guc_sched_state_no_lock);
}

/*
 * Below is a set of functions which control the GuC scheduling state which
 * require a lock, aside from the special case where the functions are called
 * from guc_lrc_desc_pin(). In that case it isn't possible for any other code
 * path to be executing on the context.
 */
#define SCHED_STATE_WAIT_FOR_DEREGISTER_TO_REGISTER	BIT(0)
#define SCHED_STATE_DESTROYED				BIT(1)
static inline void init_sched_state(struct intel_context *ce)
{
	/* Only should be called from guc_lrc_desc_pin() */
	atomic_set(&ce->guc_sched_state_no_lock, 0);
	ce->guc_state.sched_state = 0;
}

static inline bool
context_wait_for_deregister_to_register(struct intel_context *ce)
{
	return ce->guc_state.sched_state &
		SCHED_STATE_WAIT_FOR_DEREGISTER_TO_REGISTER;
}

static inline void
set_context_wait_for_deregister_to_register(struct intel_context *ce)
{
	/* Only should be called from guc_lrc_desc_pin() */
	ce->guc_state.sched_state |=
		SCHED_STATE_WAIT_FOR_DEREGISTER_TO_REGISTER;
}

static inline void
clr_context_wait_for_deregister_to_register(struct intel_context *ce)
{
	lockdep_assert_held(&ce->guc_state.lock);
	ce->guc_state.sched_state &=
		~SCHED_STATE_WAIT_FOR_DEREGISTER_TO_REGISTER;
}

static inline bool
context_destroyed(struct intel_context *ce)
{
	return ce->guc_state.sched_state & SCHED_STATE_DESTROYED;
}

static inline void
set_context_destroyed(struct intel_context *ce)
{
	lockdep_assert_held(&ce->guc_state.lock);
	ce->guc_state.sched_state |= SCHED_STATE_DESTROYED;
}

static inline bool context_guc_id_invalid(struct intel_context *ce)
{
	return ce->guc_id == GUC_INVALID_LRC_ID;
}

static inline void set_context_guc_id_invalid(struct intel_context *ce)
{
	ce->guc_id = GUC_INVALID_LRC_ID;
}

static inline struct intel_guc *ce_to_guc(struct intel_context *ce)
{
	return &ce->engine->gt->uc.guc;
}

static inline struct i915_priolist *to_priolist(struct rb_node *rb)
{
	return rb_entry(rb, struct i915_priolist, node);
}

static struct guc_lrc_desc *__get_lrc_desc(struct intel_guc *guc, u32 index)
{
	struct guc_lrc_desc *base = guc->lrc_desc_pool_vaddr;

	GEM_BUG_ON(index >= GUC_MAX_LRC_DESCRIPTORS);

	return &base[index];
}

static inline struct intel_context *__get_context(struct intel_guc *guc, u32 id)
{
	struct intel_context *ce = xa_load(&guc->context_lookup, id);

	GEM_BUG_ON(id >= GUC_MAX_LRC_DESCRIPTORS);

	return ce;
}

static int guc_lrc_desc_pool_create(struct intel_guc *guc)
{
	u32 size;
	int ret;

	size = PAGE_ALIGN(sizeof(struct guc_lrc_desc) *
			  GUC_MAX_LRC_DESCRIPTORS);
	ret = intel_guc_allocate_and_map_vma(guc, size, &guc->lrc_desc_pool,
					     (void **)&guc->lrc_desc_pool_vaddr);
	if (ret)
		return ret;

	return 0;
}

static void guc_lrc_desc_pool_destroy(struct intel_guc *guc)
{
	i915_vma_unpin_and_release(&guc->lrc_desc_pool, I915_VMA_RELEASE_MAP);
}

static inline void reset_lrc_desc(struct intel_guc *guc, u32 id)
{
	struct guc_lrc_desc *desc = __get_lrc_desc(guc, id);

	memset(desc, 0, sizeof(*desc));
	xa_erase_irq(&guc->context_lookup, id);
}

static inline bool lrc_desc_registered(struct intel_guc *guc, u32 id)
{
	return __get_context(guc, id);
}

static inline void set_lrc_desc_registered(struct intel_guc *guc, u32 id,
					   struct intel_context *ce)
{
	xa_store_irq(&guc->context_lookup, id, ce, GFP_ATOMIC);
}

static int guc_add_request(struct intel_guc *guc, struct i915_request *rq)
{
	int err;
	struct intel_context *ce = rq->context;
	u32 action[3];
	int len = 0;
	bool enabled = context_enabled(ce);

	GEM_BUG_ON(!atomic_read(&ce->guc_id_ref));
	GEM_BUG_ON(context_guc_id_invalid(ce));

	if (!enabled) {
		action[len++] = INTEL_GUC_ACTION_SCHED_CONTEXT_MODE_SET;
		action[len++] = ce->guc_id;
		action[len++] = GUC_CONTEXT_ENABLE;
	} else {
		action[len++] = INTEL_GUC_ACTION_SCHED_CONTEXT;
		action[len++] = ce->guc_id;
	}

	err = intel_guc_send_nb(guc, action, len);

	if (!enabled && !err)
		set_context_enabled(ce);

	return err;
}

static inline void guc_set_lrc_tail(struct i915_request *rq)
{
	rq->context->lrc_reg_state[CTX_RING_TAIL] =
		intel_ring_set_tail(rq->ring, rq->tail);
}

static inline int rq_prio(const struct i915_request *rq)
{
	return rq->sched.attr.priority;
}

static int guc_dequeue_one_context(struct intel_guc *guc)
{
	struct i915_sched_engine * const sched_engine = guc->sched_engine;
	struct i915_request *last = NULL;
	bool submit = false;
	struct rb_node *rb;
	int ret;

	lockdep_assert_held(&sched_engine->lock);

	if (guc->stalled_request) {
		submit = true;
		last = guc->stalled_request;
		goto resubmit;
	}

	while ((rb = rb_first_cached(&sched_engine->queue))) {
		struct i915_priolist *p = to_priolist(rb);
		struct i915_request *rq, *rn;

		priolist_for_each_request_consume(rq, rn, p) {
			if (last && rq->context != last->context)
				goto done;

			list_del_init(&rq->sched.link);

			__i915_request_submit(rq);

			trace_i915_request_in(rq, 0);
			last = rq;
			submit = true;
		}

		rb_erase_cached(&p->node, &sched_engine->queue);
		i915_priolist_free(p);
	}
done:
	if (submit) {
		guc_set_lrc_tail(last);
resubmit:
		/*
		 * We only check for -EBUSY here even though it is possible for
		 * -EDEADLK to be returned. If -EDEADLK is returned, the GuC has
		 * died and a full GT reset needs to be done. The hangcheck will
		 * eventually detect that the GuC has died and trigger this
		 * reset so no need to handle -EDEADLK here.
		 */
		ret = guc_add_request(guc, last);
		if (ret == -EBUSY) {
			tasklet_schedule(&sched_engine->tasklet);
			guc->stalled_request = last;
			return false;
		}
	}

	guc->stalled_request = NULL;
	return submit;
}

static void guc_submission_tasklet(struct tasklet_struct *t)
{
	struct i915_sched_engine *sched_engine =
		from_tasklet(sched_engine, t, tasklet);
	unsigned long flags;
	bool loop;

	spin_lock_irqsave(&sched_engine->lock, flags);

	do {
		loop = guc_dequeue_one_context(sched_engine->private_data);
	} while (loop);

	i915_sched_engine_reset_on_empty(sched_engine);

	spin_unlock_irqrestore(&sched_engine->lock, flags);
}

static void cs_irq_handler(struct intel_engine_cs *engine, u16 iir)
{
	if (iir & GT_RENDER_USER_INTERRUPT)
		intel_engine_signal_breadcrumbs(engine);
}

static void guc_reset_prepare(struct intel_engine_cs *engine)
{
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
	__tasklet_disable_sync_once(&engine->sched_engine->tasklet);
}

static void guc_reset_state(struct intel_context *ce,
			    struct intel_engine_cs *engine,
			    u32 head,
			    bool scrub)
{
	GEM_BUG_ON(!intel_context_is_pinned(ce));

	/*
	 * We want a simple context + ring to execute the breadcrumb update.
	 * We cannot rely on the context being intact across the GPU hang,
	 * so clear it and rebuild just what we need for the breadcrumb.
	 * All pending requests for this context will be zapped, and any
	 * future request will be after userspace has had the opportunity
	 * to recreate its own state.
	 */
	if (scrub)
		lrc_init_regs(ce, engine, true);

	/* Rerun the request; its payload has been neutered (if guilty). */
	lrc_update_regs(ce, engine, head);
}

static void guc_reset_rewind(struct intel_engine_cs *engine, bool stalled)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;
	struct i915_request *rq;
	unsigned long flags;

	spin_lock_irqsave(&engine->sched_engine->lock, flags);

	/* Push back any incomplete requests for replay after the reset. */
	rq = execlists_unwind_incomplete_requests(execlists);
	if (!rq)
		goto out_unlock;

	if (!i915_request_started(rq))
		stalled = false;

	__i915_request_reset(rq, stalled);
	guc_reset_state(rq->context, engine, rq->head, stalled);

out_unlock:
	spin_unlock_irqrestore(&engine->sched_engine->lock, flags);
}

static void guc_reset_cancel(struct intel_engine_cs *engine)
{
	struct i915_sched_engine * const sched_engine = engine->sched_engine;
	struct i915_request *rq, *rn;
	struct rb_node *rb;
	unsigned long flags;

	/* Can be called during boot if GuC fails to load */
	if (!engine->gt)
		return;

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
	spin_lock_irqsave(&sched_engine->lock, flags);

	/* Mark all executing requests as skipped. */
	list_for_each_entry(rq, &sched_engine->requests, sched.link) {
		i915_request_set_error_once(rq, -EIO);
		i915_request_mark_complete(rq);
	}

	/* Flush the queued requests to the timeline list (for retiring). */
	while ((rb = rb_first_cached(&sched_engine->queue))) {
		struct i915_priolist *p = to_priolist(rb);

		priolist_for_each_request_consume(rq, rn, p) {
			list_del_init(&rq->sched.link);
			__i915_request_submit(rq);
			dma_fence_set_error(&rq->fence, -EIO);
			i915_request_mark_complete(rq);
		}

		rb_erase_cached(&p->node, &sched_engine->queue);
		i915_priolist_free(p);
	}

	/* Remaining _unready_ requests will be nop'ed when submitted */

	sched_engine->queue_priority_hint = INT_MIN;
	sched_engine->queue = RB_ROOT_CACHED;

	spin_unlock_irqrestore(&sched_engine->lock, flags);
}

static void guc_reset_finish(struct intel_engine_cs *engine)
{
	if (__tasklet_enable(&engine->sched_engine->tasklet))
		/* And kick in case we missed a new request submission. */
		tasklet_hi_schedule(&engine->sched_engine->tasklet);

	ENGINE_TRACE(engine, "depth->%d\n",
		     atomic_read(&engine->sched_engine->tasklet.count));
}

/*
 * Set up the memory resources to be shared with the GuC (via the GGTT)
 * at firmware loading time.
 */
int intel_guc_submission_init(struct intel_guc *guc)
{
	int ret;

	if (guc->lrc_desc_pool)
		return 0;

	ret = guc_lrc_desc_pool_create(guc);
	if (ret)
		return ret;
	/*
	 * Keep static analysers happy, let them know that we allocated the
	 * vma after testing that it didn't exist earlier.
	 */
	GEM_BUG_ON(!guc->lrc_desc_pool);

	xa_init_flags(&guc->context_lookup, XA_FLAGS_LOCK_IRQ);

	spin_lock_init(&guc->contexts_lock);
	INIT_LIST_HEAD(&guc->guc_id_list);
	ida_init(&guc->guc_ids);

	return 0;
}

void intel_guc_submission_fini(struct intel_guc *guc)
{
	if (!guc->lrc_desc_pool)
		return;

	guc_lrc_desc_pool_destroy(guc);
	i915_sched_engine_put(guc->sched_engine);
}

static inline void queue_request(struct i915_sched_engine *sched_engine,
				 struct i915_request *rq,
				 int prio)
{
	GEM_BUG_ON(!list_empty(&rq->sched.link));
	list_add_tail(&rq->sched.link,
		      i915_sched_lookup_priolist(sched_engine, prio));
	set_bit(I915_FENCE_FLAG_PQUEUE, &rq->fence.flags);
}

static int guc_bypass_tasklet_submit(struct intel_guc *guc,
				     struct i915_request *rq)
{
	int ret;

	__i915_request_submit(rq);

	trace_i915_request_in(rq, 0);

	guc_set_lrc_tail(rq);
	ret = guc_add_request(guc, rq);
	if (ret == -EBUSY)
		guc->stalled_request = rq;

	return ret;
}

static void guc_submit_request(struct i915_request *rq)
{
	struct i915_sched_engine *sched_engine = rq->engine->sched_engine;
	struct intel_guc *guc = &rq->engine->gt->uc.guc;
	unsigned long flags;

	/* Will be called from irq-context when using foreign fences. */
	spin_lock_irqsave(&sched_engine->lock, flags);

	if (guc->stalled_request || !i915_sched_engine_is_empty(sched_engine))
		queue_request(sched_engine, rq, rq_prio(rq));
	else if (guc_bypass_tasklet_submit(guc, rq) == -EBUSY)
		tasklet_hi_schedule(&sched_engine->tasklet);

	spin_unlock_irqrestore(&sched_engine->lock, flags);
}

static int new_guc_id(struct intel_guc *guc)
{
	return ida_simple_get(&guc->guc_ids, 0,
			      GUC_MAX_LRC_DESCRIPTORS, GFP_KERNEL |
			      __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
}

static void __release_guc_id(struct intel_guc *guc, struct intel_context *ce)
{
	if (!context_guc_id_invalid(ce)) {
		ida_simple_remove(&guc->guc_ids, ce->guc_id);
		reset_lrc_desc(guc, ce->guc_id);
		set_context_guc_id_invalid(ce);
	}
	if (!list_empty(&ce->guc_id_link))
		list_del_init(&ce->guc_id_link);
}

static void release_guc_id(struct intel_guc *guc, struct intel_context *ce)
{
	unsigned long flags;

	spin_lock_irqsave(&guc->contexts_lock, flags);
	__release_guc_id(guc, ce);
	spin_unlock_irqrestore(&guc->contexts_lock, flags);
}

static int steal_guc_id(struct intel_guc *guc)
{
	struct intel_context *ce;
	int guc_id;

	lockdep_assert_held(&guc->contexts_lock);

	if (!list_empty(&guc->guc_id_list)) {
		ce = list_first_entry(&guc->guc_id_list,
				      struct intel_context,
				      guc_id_link);

		GEM_BUG_ON(atomic_read(&ce->guc_id_ref));
		GEM_BUG_ON(context_guc_id_invalid(ce));

		list_del_init(&ce->guc_id_link);
		guc_id = ce->guc_id;
		set_context_guc_id_invalid(ce);
		return guc_id;
	} else {
		return -EAGAIN;
	}
}

static int assign_guc_id(struct intel_guc *guc, u16 *out)
{
	int ret;

	lockdep_assert_held(&guc->contexts_lock);

	ret = new_guc_id(guc);
	if (unlikely(ret < 0)) {
		ret = steal_guc_id(guc);
		if (ret < 0)
			return ret;
	}

	*out = ret;
	return 0;
}

#define PIN_GUC_ID_TRIES	4
static int pin_guc_id(struct intel_guc *guc, struct intel_context *ce)
{
	int ret = 0;
	unsigned long flags, tries = PIN_GUC_ID_TRIES;

	GEM_BUG_ON(atomic_read(&ce->guc_id_ref));

try_again:
	spin_lock_irqsave(&guc->contexts_lock, flags);

	if (context_guc_id_invalid(ce)) {
		ret = assign_guc_id(guc, &ce->guc_id);
		if (ret)
			goto out_unlock;
		ret = 1;	/* Indidcates newly assigned guc_id */
	}
	if (!list_empty(&ce->guc_id_link))
		list_del_init(&ce->guc_id_link);
	atomic_inc(&ce->guc_id_ref);

out_unlock:
	spin_unlock_irqrestore(&guc->contexts_lock, flags);

	/*
	 * -EAGAIN indicates no guc_ids are available, let's retire any
	 * outstanding requests to see if that frees up a guc_id. If the first
	 * retire didn't help, insert a sleep with the timeslice duration before
	 * attempting to retire more requests. Double the sleep period each
	 * subsequent pass before finally giving up. The sleep period has max of
	 * 100ms and minimum of 1ms.
	 */
	if (ret == -EAGAIN && --tries) {
		if (PIN_GUC_ID_TRIES - tries > 1) {
			unsigned int timeslice_shifted =
				ce->engine->props.timeslice_duration_ms <<
				(PIN_GUC_ID_TRIES - tries - 2);
			unsigned int max = min_t(unsigned int, 100,
						 timeslice_shifted);

			msleep(max_t(unsigned int, max, 1));
		}
		intel_gt_retire_requests(guc_to_gt(guc));
		goto try_again;
	}

	return ret;
}

static void unpin_guc_id(struct intel_guc *guc, struct intel_context *ce)
{
	unsigned long flags;

	GEM_BUG_ON(atomic_read(&ce->guc_id_ref) < 0);

	if (unlikely(context_guc_id_invalid(ce)))
		return;

	spin_lock_irqsave(&guc->contexts_lock, flags);
	if (!context_guc_id_invalid(ce) && list_empty(&ce->guc_id_link) &&
	    !atomic_read(&ce->guc_id_ref))
		list_add_tail(&ce->guc_id_link, &guc->guc_id_list);
	spin_unlock_irqrestore(&guc->contexts_lock, flags);
}

static int __guc_action_register_context(struct intel_guc *guc,
					 u32 guc_id,
					 u32 offset)
{
	u32 action[] = {
		INTEL_GUC_ACTION_REGISTER_CONTEXT,
		guc_id,
		offset,
	};

	return intel_guc_send_busy_loop(guc, action, ARRAY_SIZE(action), true);
}

static int register_context(struct intel_context *ce)
{
	struct intel_guc *guc = ce_to_guc(ce);
	u32 offset = intel_guc_ggtt_offset(guc, guc->lrc_desc_pool) +
		ce->guc_id * sizeof(struct guc_lrc_desc);

	return __guc_action_register_context(guc, ce->guc_id, offset);
}

static int __guc_action_deregister_context(struct intel_guc *guc,
					   u32 guc_id)
{
	u32 action[] = {
		INTEL_GUC_ACTION_DEREGISTER_CONTEXT,
		guc_id,
	};

	return intel_guc_send_busy_loop(guc, action, ARRAY_SIZE(action), true);
}

static int deregister_context(struct intel_context *ce, u32 guc_id)
{
	struct intel_guc *guc = ce_to_guc(ce);

	return __guc_action_deregister_context(guc, guc_id);
}

static intel_engine_mask_t adjust_engine_mask(u8 class, intel_engine_mask_t mask)
{
	switch (class) {
	case RENDER_CLASS:
		return mask >> RCS0;
	case VIDEO_ENHANCEMENT_CLASS:
		return mask >> VECS0;
	case VIDEO_DECODE_CLASS:
		return mask >> VCS0;
	case COPY_ENGINE_CLASS:
		return mask >> BCS0;
	default:
		MISSING_CASE(class);
		return 0;
	}
}

static void guc_context_policy_init(struct intel_engine_cs *engine,
				    struct guc_lrc_desc *desc)
{
	desc->policy_flags = 0;

	desc->execution_quantum = CONTEXT_POLICY_DEFAULT_EXECUTION_QUANTUM_US;
	desc->preemption_timeout = CONTEXT_POLICY_DEFAULT_PREEMPTION_TIME_US;
}

static int guc_lrc_desc_pin(struct intel_context *ce)
{
	struct intel_engine_cs *engine = ce->engine;
	struct intel_runtime_pm *runtime_pm = engine->uncore->rpm;
	struct intel_guc *guc = &engine->gt->uc.guc;
	u32 desc_idx = ce->guc_id;
	struct guc_lrc_desc *desc;
	bool context_registered;
	intel_wakeref_t wakeref;
	int ret = 0;

	GEM_BUG_ON(!engine->mask);

	/*
	 * Ensure LRC + CT vmas are is same region as write barrier is done
	 * based on CT vma region.
	 */
	GEM_BUG_ON(i915_gem_object_is_lmem(guc->ct.vma->obj) !=
		   i915_gem_object_is_lmem(ce->ring->vma->obj));

	context_registered = lrc_desc_registered(guc, desc_idx);

	reset_lrc_desc(guc, desc_idx);
	set_lrc_desc_registered(guc, desc_idx, ce);

	desc = __get_lrc_desc(guc, desc_idx);
	desc->engine_class = engine_class_to_guc_class(engine->class);
	desc->engine_submit_mask = adjust_engine_mask(engine->class,
						      engine->mask);
	desc->hw_context_desc = ce->lrc.lrca;
	desc->priority = GUC_CLIENT_PRIORITY_KMD_NORMAL;
	desc->context_flags = CONTEXT_REGISTRATION_FLAG_KMD;
	guc_context_policy_init(engine, desc);
	init_sched_state(ce);

	/*
	 * The context_lookup xarray is used to determine if the hardware
	 * context is currently registered. There are two cases in which it
	 * could be registered either the guc_id has been stolen from another
	 * context or the lrc descriptor address of this context has changed. In
	 * either case the context needs to be deregistered with the GuC before
	 * registering this context.
	 */
	if (context_registered) {
		set_context_wait_for_deregister_to_register(ce);
		intel_context_get(ce);

		/*
		 * If stealing the guc_id, this ce has the same guc_id as the
		 * context whose guc_id was stolen.
		 */
		with_intel_runtime_pm(runtime_pm, wakeref)
			ret = deregister_context(ce, ce->guc_id);
	} else {
		with_intel_runtime_pm(runtime_pm, wakeref)
			ret = register_context(ce);
	}

	return ret;
}

static int guc_context_pre_pin(struct intel_context *ce,
			       struct i915_gem_ww_ctx *ww,
			       void **vaddr)
{
	return lrc_pre_pin(ce, ce->engine, ww, vaddr);
}

static int guc_context_pin(struct intel_context *ce, void *vaddr)
{
	if (i915_ggtt_offset(ce->state) !=
	    (ce->lrc.lrca & CTX_GTT_ADDRESS_MASK))
		set_bit(CONTEXT_LRCA_DIRTY, &ce->flags);

	/*
	 * GuC context gets pinned in guc_request_alloc. See that function for
	 * explaination of why.
	 */

	return lrc_pin(ce, ce->engine, vaddr);
}

static void guc_context_unpin(struct intel_context *ce)
{
	struct intel_guc *guc = ce_to_guc(ce);

	unpin_guc_id(guc, ce);
	lrc_unpin(ce);
}

static void guc_context_post_unpin(struct intel_context *ce)
{
	lrc_post_unpin(ce);
}

static inline void guc_lrc_desc_unpin(struct intel_context *ce)
{
	struct intel_guc *guc = ce_to_guc(ce);
	unsigned long flags;

	GEM_BUG_ON(!lrc_desc_registered(guc, ce->guc_id));
	GEM_BUG_ON(ce != __get_context(guc, ce->guc_id));

	spin_lock_irqsave(&ce->guc_state.lock, flags);
	set_context_destroyed(ce);
	spin_unlock_irqrestore(&ce->guc_state.lock, flags);

	deregister_context(ce, ce->guc_id);
}

static void guc_context_destroy(struct kref *kref)
{
	struct intel_context *ce = container_of(kref, typeof(*ce), ref);
	struct intel_runtime_pm *runtime_pm = ce->engine->uncore->rpm;
	struct intel_guc *guc = ce_to_guc(ce);
	intel_wakeref_t wakeref;
	unsigned long flags;

	/*
	 * If the guc_id is invalid this context has been stolen and we can free
	 * it immediately. Also can be freed immediately if the context is not
	 * registered with the GuC.
	 */
	if (context_guc_id_invalid(ce)) {
		lrc_destroy(kref);
		return;
	} else if (!lrc_desc_registered(guc, ce->guc_id)) {
		release_guc_id(guc, ce);
		lrc_destroy(kref);
		return;
	}

	/*
	 * We have to acquire the context spinlock and check guc_id again, if it
	 * is valid it hasn't been stolen and needs to be deregistered. We
	 * delete this context from the list of unpinned guc_ids available to
	 * steal to seal a race with guc_lrc_desc_pin(). When the G2H CTB
	 * returns indicating this context has been deregistered the guc_id is
	 * returned to the pool of available guc_ids.
	 */
	spin_lock_irqsave(&guc->contexts_lock, flags);
	if (context_guc_id_invalid(ce)) {
		spin_unlock_irqrestore(&guc->contexts_lock, flags);
		lrc_destroy(kref);
		return;
	}

	if (!list_empty(&ce->guc_id_link))
		list_del_init(&ce->guc_id_link);
	spin_unlock_irqrestore(&guc->contexts_lock, flags);

	/*
	 * We defer GuC context deregistration until the context is destroyed
	 * in order to save on CTBs. With this optimization ideally we only need
	 * 1 CTB to register the context during the first pin and 1 CTB to
	 * deregister the context when the context is destroyed. Without this
	 * optimization, a CTB would be needed every pin & unpin.
	 *
	 * XXX: Need to acqiure the runtime wakeref as this can be triggered
	 * from context_free_worker when runtime wakeref is not held.
	 * guc_lrc_desc_unpin requires the runtime as a GuC register is written
	 * in H2G CTB to deregister the context. A future patch may defer this
	 * H2G CTB if the runtime wakeref is zero.
	 */
	with_intel_runtime_pm(runtime_pm, wakeref)
		guc_lrc_desc_unpin(ce);
}

static int guc_context_alloc(struct intel_context *ce)
{
	return lrc_alloc(ce, ce->engine);
}

static const struct intel_context_ops guc_context_ops = {
	.alloc = guc_context_alloc,

	.pre_pin = guc_context_pre_pin,
	.pin = guc_context_pin,
	.unpin = guc_context_unpin,
	.post_unpin = guc_context_post_unpin,

	.enter = intel_context_enter_engine,
	.exit = intel_context_exit_engine,

	.reset = lrc_reset,
	.destroy = guc_context_destroy,
};

static bool context_needs_register(struct intel_context *ce, bool new_guc_id)
{
	return new_guc_id || test_bit(CONTEXT_LRCA_DIRTY, &ce->flags) ||
		!lrc_desc_registered(ce_to_guc(ce), ce->guc_id);
}

static int guc_request_alloc(struct i915_request *rq)
{
	struct intel_context *ce = rq->context;
	struct intel_guc *guc = ce_to_guc(ce);
	int ret;

	GEM_BUG_ON(!intel_context_is_pinned(rq->context));

	/*
	 * Flush enough space to reduce the likelihood of waiting after
	 * we start building the request - in which case we will just
	 * have to repeat work.
	 */
	rq->reserved_space += GUC_REQUEST_SIZE;

	/*
	 * Note that after this point, we have committed to using
	 * this request as it is being used to both track the
	 * state of engine initialisation and liveness of the
	 * golden renderstate above. Think twice before you try
	 * to cancel/unwind this request now.
	 */

	/* Unconditionally invalidate GPU caches and TLBs. */
	ret = rq->engine->emit_flush(rq, EMIT_INVALIDATE);
	if (ret)
		return ret;

	rq->reserved_space -= GUC_REQUEST_SIZE;

	/*
	 * Call pin_guc_id here rather than in the pinning step as with
	 * dma_resv, contexts can be repeatedly pinned / unpinned trashing the
	 * guc_ids and creating horrible race conditions. This is especially bad
	 * when guc_ids are being stolen due to over subscription. By the time
	 * this function is reached, it is guaranteed that the guc_id will be
	 * persistent until the generated request is retired. Thus, sealing these
	 * race conditions. It is still safe to fail here if guc_ids are
	 * exhausted and return -EAGAIN to the user indicating that they can try
	 * again in the future.
	 *
	 * There is no need for a lock here as the timeline mutex ensures at
	 * most one context can be executing this code path at once. The
	 * guc_id_ref is incremented once for every request in flight and
	 * decremented on each retire. When it is zero, a lock around the
	 * increment (in pin_guc_id) is needed to seal a race with unpin_guc_id.
	 */
	if (atomic_add_unless(&ce->guc_id_ref, 1, 0))
		return 0;

	ret = pin_guc_id(guc, ce);	/* returns 1 if new guc_id assigned */
	if (unlikely(ret < 0))
		return ret;
	if (context_needs_register(ce, !!ret)) {
		ret = guc_lrc_desc_pin(ce);
		if (unlikely(ret)) {	/* unwind */
			atomic_dec(&ce->guc_id_ref);
			unpin_guc_id(guc, ce);
			return ret;
		}
	}

	clear_bit(CONTEXT_LRCA_DIRTY, &ce->flags);

	return 0;
}

static void sanitize_hwsp(struct intel_engine_cs *engine)
{
	struct intel_timeline *tl;

	list_for_each_entry(tl, &engine->status_page.timelines, engine_link)
		intel_timeline_reset_seqno(tl);
}

static void guc_sanitize(struct intel_engine_cs *engine)
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
	clflush_cache_range(engine->status_page.addr, PAGE_SIZE);
}

static void setup_hwsp(struct intel_engine_cs *engine)
{
	intel_engine_set_hwsp_writemask(engine, ~0u); /* HWSTAM */

	ENGINE_WRITE_FW(engine,
			RING_HWS_PGA,
			i915_ggtt_offset(engine->status_page.vma));
}

static void start_engine(struct intel_engine_cs *engine)
{
	ENGINE_WRITE_FW(engine,
			RING_MODE_GEN7,
			_MASKED_BIT_ENABLE(GEN11_GFX_DISABLE_LEGACY_MODE));

	ENGINE_WRITE_FW(engine, RING_MI_MODE, _MASKED_BIT_DISABLE(STOP_RING));
	ENGINE_POSTING_READ(engine, RING_MI_MODE);
}

static int guc_resume(struct intel_engine_cs *engine)
{
	assert_forcewakes_active(engine->uncore, FORCEWAKE_ALL);

	intel_mocs_init_engine(engine);

	intel_breadcrumbs_reset(engine->breadcrumbs);

	setup_hwsp(engine);
	start_engine(engine);

	return 0;
}

static void guc_set_default_submission(struct intel_engine_cs *engine)
{
	engine->submit_request = guc_submit_request;
}

static inline void guc_kernel_context_pin(struct intel_guc *guc,
					  struct intel_context *ce)
{
	if (context_guc_id_invalid(ce))
		pin_guc_id(guc, ce);
	guc_lrc_desc_pin(ce);
}

static inline void guc_init_lrc_mapping(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/* make sure all descriptors are clean... */
	xa_destroy(&guc->context_lookup);

	/*
	 * Some contexts might have been pinned before we enabled GuC
	 * submission, so we need to add them to the GuC bookeeping.
	 * Also, after a reset the of the GuC we want to make sure that the
	 * information shared with GuC is properly reset. The kernel LRCs are
	 * not attached to the gem_context, so they need to be added separately.
	 *
	 * Note: we purposefully do not check the return of guc_lrc_desc_pin,
	 * because that function can only fail if a reset is just starting. This
	 * is at the end of reset so presumably another reset isn't happening
	 * and even it did this code would be run again.
	 */

	for_each_engine(engine, gt, id)
		if (engine->kernel_context)
			guc_kernel_context_pin(guc, engine->kernel_context);
}

static void guc_release(struct intel_engine_cs *engine)
{
	engine->sanitize = NULL; /* no longer in control, nothing to sanitize */

	intel_engine_cleanup_common(engine);
	lrc_fini_wa_ctx(engine);
}

static void guc_default_vfuncs(struct intel_engine_cs *engine)
{
	/* Default vfuncs which can be overridden by each engine. */

	engine->resume = guc_resume;

	engine->cops = &guc_context_ops;
	engine->request_alloc = guc_request_alloc;

	engine->sched_engine->schedule = i915_schedule;

	engine->reset.prepare = guc_reset_prepare;
	engine->reset.rewind = guc_reset_rewind;
	engine->reset.cancel = guc_reset_cancel;
	engine->reset.finish = guc_reset_finish;

	engine->emit_flush = gen8_emit_flush_xcs;
	engine->emit_init_breadcrumb = gen8_emit_init_breadcrumb;
	engine->emit_fini_breadcrumb = gen8_emit_fini_breadcrumb_xcs;
	if (GRAPHICS_VER(engine->i915) >= 12) {
		engine->emit_fini_breadcrumb = gen12_emit_fini_breadcrumb_xcs;
		engine->emit_flush = gen12_emit_flush_xcs;
	}
	engine->set_default_submission = guc_set_default_submission;

	engine->flags |= I915_ENGINE_HAS_PREEMPTION;

	/*
	 * TODO: GuC supports timeslicing and semaphores as well, but they're
	 * handled by the firmware so some minor tweaks are required before
	 * enabling.
	 *
	 * engine->flags |= I915_ENGINE_HAS_TIMESLICES;
	 * engine->flags |= I915_ENGINE_HAS_SEMAPHORES;
	 */

	engine->emit_bb_start = gen8_emit_bb_start;
}

static void rcs_submission_override(struct intel_engine_cs *engine)
{
	switch (GRAPHICS_VER(engine->i915)) {
	case 12:
		engine->emit_flush = gen12_emit_flush_rcs;
		engine->emit_fini_breadcrumb = gen12_emit_fini_breadcrumb_rcs;
		break;
	case 11:
		engine->emit_flush = gen11_emit_flush_rcs;
		engine->emit_fini_breadcrumb = gen11_emit_fini_breadcrumb_rcs;
		break;
	default:
		engine->emit_flush = gen8_emit_flush_rcs;
		engine->emit_fini_breadcrumb = gen8_emit_fini_breadcrumb_rcs;
		break;
	}
}

static inline void guc_default_irqs(struct intel_engine_cs *engine)
{
	engine->irq_keep_mask = GT_RENDER_USER_INTERRUPT;
	intel_engine_set_irq_handler(engine, cs_irq_handler);
}

int intel_guc_submission_setup(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;
	struct intel_guc *guc = &engine->gt->uc.guc;

	/*
	 * The setup relies on several assumptions (e.g. irqs always enabled)
	 * that are only valid on gen11+
	 */
	GEM_BUG_ON(GRAPHICS_VER(i915) < 11);

	if (!guc->sched_engine) {
		guc->sched_engine = i915_sched_engine_create(ENGINE_VIRTUAL);
		if (!guc->sched_engine)
			return -ENOMEM;

		guc->sched_engine->schedule = i915_schedule;
		guc->sched_engine->private_data = guc;
		tasklet_setup(&guc->sched_engine->tasklet,
			      guc_submission_tasklet);
	}
	i915_sched_engine_put(engine->sched_engine);
	engine->sched_engine = i915_sched_engine_get(guc->sched_engine);

	guc_default_vfuncs(engine);
	guc_default_irqs(engine);

	if (engine->class == RENDER_CLASS)
		rcs_submission_override(engine);

	lrc_init_wa_ctx(engine);

	/* Finally, take ownership and responsibility for cleanup! */
	engine->sanitize = guc_sanitize;
	engine->release = guc_release;

	return 0;
}

void intel_guc_submission_enable(struct intel_guc *guc)
{
	guc_init_lrc_mapping(guc);
}

void intel_guc_submission_disable(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);

	GEM_BUG_ON(gt->awake); /* GT should be parked first */

	/* Note: By the time we're here, GuC may have already been reset */
}

static bool __guc_submission_selected(struct intel_guc *guc)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;

	if (!intel_guc_submission_is_supported(guc))
		return false;

	return i915->params.enable_guc & ENABLE_GUC_SUBMISSION;
}

void intel_guc_submission_init_early(struct intel_guc *guc)
{
	guc->submission_selected = __guc_submission_selected(guc);
}

static inline struct intel_context *
g2h_context_lookup(struct intel_guc *guc, u32 desc_idx)
{
	struct intel_context *ce;

	if (unlikely(desc_idx >= GUC_MAX_LRC_DESCRIPTORS)) {
		drm_err(&guc_to_gt(guc)->i915->drm,
			"Invalid desc_idx %u", desc_idx);
		return NULL;
	}

	ce = __get_context(guc, desc_idx);
	if (unlikely(!ce)) {
		drm_err(&guc_to_gt(guc)->i915->drm,
			"Context is NULL, desc_idx %u", desc_idx);
		return NULL;
	}

	return ce;
}

int intel_guc_deregister_done_process_msg(struct intel_guc *guc,
					  const u32 *msg,
					  u32 len)
{
	struct intel_context *ce;
	u32 desc_idx = msg[0];

	if (unlikely(len < 1)) {
		drm_err(&guc_to_gt(guc)->i915->drm, "Invalid length %u", len);
		return -EPROTO;
	}

	ce = g2h_context_lookup(guc, desc_idx);
	if (unlikely(!ce))
		return -EPROTO;

	if (context_wait_for_deregister_to_register(ce)) {
		struct intel_runtime_pm *runtime_pm =
			&ce->engine->gt->i915->runtime_pm;
		intel_wakeref_t wakeref;

		/*
		 * Previous owner of this guc_id has been deregistered, now safe
		 * register this context.
		 */
		with_intel_runtime_pm(runtime_pm, wakeref)
			register_context(ce);
		clr_context_wait_for_deregister_to_register(ce);
		intel_context_put(ce);
	} else if (context_destroyed(ce)) {
		/* Context has been destroyed */
		release_guc_id(guc, ce);
		lrc_destroy(&ce->ref);
	}

	return 0;
}
