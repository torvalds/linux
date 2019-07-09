/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Ben Widawsky <ben@bwidawsk.net>
 *    Michel Thierry <michel.thierry@intel.com>
 *    Thomas Daniel <thomas.daniel@intel.com>
 *    Oscar Mateo <oscar.mateo@intel.com>
 *
 */

/**
 * DOC: Logical Rings, Logical Ring Contexts and Execlists
 *
 * Motivation:
 * GEN8 brings an expansion of the HW contexts: "Logical Ring Contexts".
 * These expanded contexts enable a number of new abilities, especially
 * "Execlists" (also implemented in this file).
 *
 * One of the main differences with the legacy HW contexts is that logical
 * ring contexts incorporate many more things to the context's state, like
 * PDPs or ringbuffer control registers:
 *
 * The reason why PDPs are included in the context is straightforward: as
 * PPGTTs (per-process GTTs) are actually per-context, having the PDPs
 * contained there mean you don't need to do a ppgtt->switch_mm yourself,
 * instead, the GPU will do it for you on the context switch.
 *
 * But, what about the ringbuffer control registers (head, tail, etc..)?
 * shouldn't we just need a set of those per engine command streamer? This is
 * where the name "Logical Rings" starts to make sense: by virtualizing the
 * rings, the engine cs shifts to a new "ring buffer" with every context
 * switch. When you want to submit a workload to the GPU you: A) choose your
 * context, B) find its appropriate virtualized ring, C) write commands to it
 * and then, finally, D) tell the GPU to switch to that context.
 *
 * Instead of the legacy MI_SET_CONTEXT, the way you tell the GPU to switch
 * to a contexts is via a context execution list, ergo "Execlists".
 *
 * LRC implementation:
 * Regarding the creation of contexts, we have:
 *
 * - One global default context.
 * - One local default context for each opened fd.
 * - One local extra context for each context create ioctl call.
 *
 * Now that ringbuffers belong per-context (and not per-engine, like before)
 * and that contexts are uniquely tied to a given engine (and not reusable,
 * like before) we need:
 *
 * - One ringbuffer per-engine inside each context.
 * - One backing object per-engine inside each context.
 *
 * The global default context starts its life with these new objects fully
 * allocated and populated. The local default context for each opened fd is
 * more complex, because we don't know at creation time which engine is going
 * to use them. To handle this, we have implemented a deferred creation of LR
 * contexts:
 *
 * The local context starts its life as a hollow or blank holder, that only
 * gets populated for a given engine once we receive an execbuffer. If later
 * on we receive another execbuffer ioctl for the same context but a different
 * engine, we allocate/populate a new ringbuffer and context backing object and
 * so on.
 *
 * Finally, regarding local contexts created using the ioctl call: as they are
 * only allowed with the render ring, we can allocate & populate them right
 * away (no need to defer anything, at least for now).
 *
 * Execlists implementation:
 * Execlists are the new method by which, on gen8+ hardware, workloads are
 * submitted for execution (as opposed to the legacy, ringbuffer-based, method).
 * This method works as follows:
 *
 * When a request is committed, its commands (the BB start and any leading or
 * trailing commands, like the seqno breadcrumbs) are placed in the ringbuffer
 * for the appropriate context. The tail pointer in the hardware context is not
 * updated at this time, but instead, kept by the driver in the ringbuffer
 * structure. A structure representing this request is added to a request queue
 * for the appropriate engine: this structure contains a copy of the context's
 * tail after the request was written to the ring buffer and a pointer to the
 * context itself.
 *
 * If the engine's request queue was empty before the request was added, the
 * queue is processed immediately. Otherwise the queue will be processed during
 * a context switch interrupt. In any case, elements on the queue will get sent
 * (in pairs) to the GPU's ExecLists Submit Port (ELSP, for short) with a
 * globally unique 20-bits submission ID.
 *
 * When execution of a request completes, the GPU updates the context status
 * buffer with a context complete event and generates a context switch interrupt.
 * During the interrupt handling, the driver examines the events in the buffer:
 * for each context complete event, if the announced ID matches that on the head
 * of the request queue, then that request is retired and removed from the queue.
 *
 * After processing, if any requests were retired and the queue is not empty
 * then a new execution list can be submitted. The two requests at the front of
 * the queue are next to be submitted but since a context may not occur twice in
 * an execution list, if subsequent requests have the same ID as the first then
 * the two requests must be combined. This is done simply by discarding requests
 * at the head of the queue until either only one requests is left (in which case
 * we use a NULL second context) or the first two requests have unique IDs.
 *
 * By always executing the first two requests in the queue the driver ensures
 * that the GPU is kept as busy as possible. In the case where a single context
 * completes but a second context is still executing, the request for this second
 * context will be at the head of the queue when we remove the first one. This
 * request will then be resubmitted along with a new request for a different context,
 * which will cause the hardware to continue executing the second request and queue
 * the new request (the GPU detects the condition of a context getting preempted
 * with the same context and optimizes the context switch flow by not doing
 * preemption, but just sampling the new tail pointer).
 *
 */
#include <linux/interrupt.h>

#include "gem/i915_gem_context.h"

#include "i915_drv.h"
#include "i915_vgpu.h"
#include "intel_engine_pm.h"
#include "intel_gt.h"
#include "intel_lrc_reg.h"
#include "intel_mocs.h"
#include "intel_renderstate.h"
#include "intel_reset.h"
#include "intel_workarounds.h"

#define RING_EXECLIST_QFULL		(1 << 0x2)
#define RING_EXECLIST1_VALID		(1 << 0x3)
#define RING_EXECLIST0_VALID		(1 << 0x4)
#define RING_EXECLIST_ACTIVE_STATUS	(3 << 0xE)
#define RING_EXECLIST1_ACTIVE		(1 << 0x11)
#define RING_EXECLIST0_ACTIVE		(1 << 0x12)

#define GEN8_CTX_STATUS_IDLE_ACTIVE	(1 << 0)
#define GEN8_CTX_STATUS_PREEMPTED	(1 << 1)
#define GEN8_CTX_STATUS_ELEMENT_SWITCH	(1 << 2)
#define GEN8_CTX_STATUS_ACTIVE_IDLE	(1 << 3)
#define GEN8_CTX_STATUS_COMPLETE	(1 << 4)
#define GEN8_CTX_STATUS_LITE_RESTORE	(1 << 15)

#define GEN8_CTX_STATUS_COMPLETED_MASK \
	 (GEN8_CTX_STATUS_COMPLETE | GEN8_CTX_STATUS_PREEMPTED)

#define CTX_DESC_FORCE_RESTORE BIT_ULL(2)

/* Typical size of the average request (2 pipecontrols and a MI_BB) */
#define EXECLISTS_REQUEST_SIZE 64 /* bytes */
#define WA_TAIL_DWORDS 2
#define WA_TAIL_BYTES (sizeof(u32) * WA_TAIL_DWORDS)

struct virtual_engine {
	struct intel_engine_cs base;
	struct intel_context context;

	/*
	 * We allow only a single request through the virtual engine at a time
	 * (each request in the timeline waits for the completion fence of
	 * the previous before being submitted). By restricting ourselves to
	 * only submitting a single request, each request is placed on to a
	 * physical to maximise load spreading (by virtue of the late greedy
	 * scheduling -- each real engine takes the next available request
	 * upon idling).
	 */
	struct i915_request *request;

	/*
	 * We keep a rbtree of available virtual engines inside each physical
	 * engine, sorted by priority. Here we preallocate the nodes we need
	 * for the virtual engine, indexed by physical_engine->id.
	 */
	struct ve_node {
		struct rb_node rb;
		int prio;
	} nodes[I915_NUM_ENGINES];

	/*
	 * Keep track of bonded pairs -- restrictions upon on our selection
	 * of physical engines any particular request may be submitted to.
	 * If we receive a submit-fence from a master engine, we will only
	 * use one of sibling_mask physical engines.
	 */
	struct ve_bond {
		const struct intel_engine_cs *master;
		intel_engine_mask_t sibling_mask;
	} *bonds;
	unsigned int num_bonds;

	/* And finally, which physical engines this virtual engine maps onto. */
	unsigned int num_siblings;
	struct intel_engine_cs *siblings[0];
};

static struct virtual_engine *to_virtual_engine(struct intel_engine_cs *engine)
{
	GEM_BUG_ON(!intel_engine_is_virtual(engine));
	return container_of(engine, struct virtual_engine, base);
}

static int execlists_context_deferred_alloc(struct intel_context *ce,
					    struct intel_engine_cs *engine);
static void execlists_init_reg_state(u32 *reg_state,
				     struct intel_context *ce,
				     struct intel_engine_cs *engine,
				     struct intel_ring *ring);

static inline u32 intel_hws_preempt_address(struct intel_engine_cs *engine)
{
	return (i915_ggtt_offset(engine->status_page.vma) +
		I915_GEM_HWS_PREEMPT_ADDR);
}

static inline void
ring_set_paused(const struct intel_engine_cs *engine, int state)
{
	/*
	 * We inspect HWS_PREEMPT with a semaphore inside
	 * engine->emit_fini_breadcrumb. If the dword is true,
	 * the ring is paused as the semaphore will busywait
	 * until the dword is false.
	 */
	engine->status_page.addr[I915_GEM_HWS_PREEMPT] = state;
	if (state)
		wmb();
}

static inline struct i915_priolist *to_priolist(struct rb_node *rb)
{
	return rb_entry(rb, struct i915_priolist, node);
}

static inline int rq_prio(const struct i915_request *rq)
{
	return rq->sched.attr.priority;
}

static int effective_prio(const struct i915_request *rq)
{
	int prio = rq_prio(rq);

	/*
	 * On unwinding the active request, we give it a priority bump
	 * if it has completed waiting on any semaphore. If we know that
	 * the request has already started, we can prevent an unwanted
	 * preempt-to-idle cycle by taking that into account now.
	 */
	if (__i915_request_has_started(rq))
		prio |= I915_PRIORITY_NOSEMAPHORE;

	/* Restrict mere WAIT boosts from triggering preemption */
	BUILD_BUG_ON(__NO_PREEMPTION & ~I915_PRIORITY_MASK); /* only internal */
	return prio | __NO_PREEMPTION;
}

static int queue_prio(const struct intel_engine_execlists *execlists)
{
	struct i915_priolist *p;
	struct rb_node *rb;

	rb = rb_first_cached(&execlists->queue);
	if (!rb)
		return INT_MIN;

	/*
	 * As the priolist[] are inverted, with the highest priority in [0],
	 * we have to flip the index value to become priority.
	 */
	p = to_priolist(rb);
	return ((p->priority + 1) << I915_USER_PRIORITY_SHIFT) - ffs(p->used);
}

static inline bool need_preempt(const struct intel_engine_cs *engine,
				const struct i915_request *rq,
				struct rb_node *rb)
{
	int last_prio;

	/*
	 * Check if the current priority hint merits a preemption attempt.
	 *
	 * We record the highest value priority we saw during rescheduling
	 * prior to this dequeue, therefore we know that if it is strictly
	 * less than the current tail of ESLP[0], we do not need to force
	 * a preempt-to-idle cycle.
	 *
	 * However, the priority hint is a mere hint that we may need to
	 * preempt. If that hint is stale or we may be trying to preempt
	 * ourselves, ignore the request.
	 */
	last_prio = effective_prio(rq);
	if (!i915_scheduler_need_preempt(engine->execlists.queue_priority_hint,
					 last_prio))
		return false;

	/*
	 * Check against the first request in ELSP[1], it will, thanks to the
	 * power of PI, be the highest priority of that context.
	 */
	if (!list_is_last(&rq->sched.link, &engine->active.requests) &&
	    rq_prio(list_next_entry(rq, sched.link)) > last_prio)
		return true;

	if (rb) {
		struct virtual_engine *ve =
			rb_entry(rb, typeof(*ve), nodes[engine->id].rb);
		bool preempt = false;

		if (engine == ve->siblings[0]) { /* only preempt one sibling */
			struct i915_request *next;

			rcu_read_lock();
			next = READ_ONCE(ve->request);
			if (next)
				preempt = rq_prio(next) > last_prio;
			rcu_read_unlock();
		}

		if (preempt)
			return preempt;
	}

	/*
	 * If the inflight context did not trigger the preemption, then maybe
	 * it was the set of queued requests? Pick the highest priority in
	 * the queue (the first active priolist) and see if it deserves to be
	 * running instead of ELSP[0].
	 *
	 * The highest priority request in the queue can not be either
	 * ELSP[0] or ELSP[1] as, thanks again to PI, if it was the same
	 * context, it's priority would not exceed ELSP[0] aka last_prio.
	 */
	return queue_prio(&engine->execlists) > last_prio;
}

__maybe_unused static inline bool
assert_priority_queue(const struct i915_request *prev,
		      const struct i915_request *next)
{
	/*
	 * Without preemption, the prev may refer to the still active element
	 * which we refuse to let go.
	 *
	 * Even with preemption, there are times when we think it is better not
	 * to preempt and leave an ostensibly lower priority request in flight.
	 */
	if (i915_request_is_active(prev))
		return true;

	return rq_prio(prev) >= rq_prio(next);
}

/*
 * The context descriptor encodes various attributes of a context,
 * including its GTT address and some flags. Because it's fairly
 * expensive to calculate, we'll just do it once and cache the result,
 * which remains valid until the context is unpinned.
 *
 * This is what a descriptor looks like, from LSB to MSB::
 *
 *      bits  0-11:    flags, GEN8_CTX_* (cached in ctx->desc_template)
 *      bits 12-31:    LRCA, GTT address of (the HWSP of) this context
 *      bits 32-52:    ctx ID, a globally unique tag (highest bit used by GuC)
 *      bits 53-54:    mbz, reserved for use by hardware
 *      bits 55-63:    group ID, currently unused and set to 0
 *
 * Starting from Gen11, the upper dword of the descriptor has a new format:
 *
 *      bits 32-36:    reserved
 *      bits 37-47:    SW context ID
 *      bits 48:53:    engine instance
 *      bit 54:        mbz, reserved for use by hardware
 *      bits 55-60:    SW counter
 *      bits 61-63:    engine class
 *
 * engine info, SW context ID and SW counter need to form a unique number
 * (Context ID) per lrc.
 */
static u64
lrc_descriptor(struct intel_context *ce, struct intel_engine_cs *engine)
{
	struct i915_gem_context *ctx = ce->gem_context;
	u64 desc;

	BUILD_BUG_ON(MAX_CONTEXT_HW_ID > (BIT(GEN8_CTX_ID_WIDTH)));
	BUILD_BUG_ON(GEN11_MAX_CONTEXT_HW_ID > (BIT(GEN11_SW_CTX_ID_WIDTH)));

	desc = ctx->desc_template;				/* bits  0-11 */
	GEM_BUG_ON(desc & GENMASK_ULL(63, 12));

	desc |= i915_ggtt_offset(ce->state) + LRC_HEADER_PAGES * PAGE_SIZE;
								/* bits 12-31 */
	GEM_BUG_ON(desc & GENMASK_ULL(63, 32));

	/*
	 * The following 32bits are copied into the OA reports (dword 2).
	 * Consider updating oa_get_render_ctx_id in i915_perf.c when changing
	 * anything below.
	 */
	if (INTEL_GEN(engine->i915) >= 11) {
		GEM_BUG_ON(ctx->hw_id >= BIT(GEN11_SW_CTX_ID_WIDTH));
		desc |= (u64)ctx->hw_id << GEN11_SW_CTX_ID_SHIFT;
								/* bits 37-47 */

		desc |= (u64)engine->instance << GEN11_ENGINE_INSTANCE_SHIFT;
								/* bits 48-53 */

		/* TODO: decide what to do with SW counter (bits 55-60) */

		desc |= (u64)engine->class << GEN11_ENGINE_CLASS_SHIFT;
								/* bits 61-63 */
	} else {
		GEM_BUG_ON(ctx->hw_id >= BIT(GEN8_CTX_ID_WIDTH));
		desc |= (u64)ctx->hw_id << GEN8_CTX_ID_SHIFT;	/* bits 32-52 */
	}

	return desc;
}

static void unwind_wa_tail(struct i915_request *rq)
{
	rq->tail = intel_ring_wrap(rq->ring, rq->wa_tail - WA_TAIL_BYTES);
	assert_ring_tail_valid(rq->ring, rq->tail);
}

static struct i915_request *
__unwind_incomplete_requests(struct intel_engine_cs *engine)
{
	struct i915_request *rq, *rn, *active = NULL;
	struct list_head *uninitialized_var(pl);
	int prio = I915_PRIORITY_INVALID;

	lockdep_assert_held(&engine->active.lock);

	list_for_each_entry_safe_reverse(rq, rn,
					 &engine->active.requests,
					 sched.link) {
		struct intel_engine_cs *owner;

		if (i915_request_completed(rq))
			continue; /* XXX */

		__i915_request_unsubmit(rq);
		unwind_wa_tail(rq);

		/*
		 * Push the request back into the queue for later resubmission.
		 * If this request is not native to this physical engine (i.e.
		 * it came from a virtual source), push it back onto the virtual
		 * engine so that it can be moved across onto another physical
		 * engine as load dictates.
		 */
		owner = rq->hw_context->engine;
		if (likely(owner == engine)) {
			GEM_BUG_ON(rq_prio(rq) == I915_PRIORITY_INVALID);
			if (rq_prio(rq) != prio) {
				prio = rq_prio(rq);
				pl = i915_sched_lookup_priolist(engine, prio);
			}
			GEM_BUG_ON(RB_EMPTY_ROOT(&engine->execlists.queue.rb_root));

			list_move(&rq->sched.link, pl);
			active = rq;
		} else {
			rq->engine = owner;
			owner->submit_request(rq);
			active = NULL;
		}
	}

	return active;
}

struct i915_request *
execlists_unwind_incomplete_requests(struct intel_engine_execlists *execlists)
{
	struct intel_engine_cs *engine =
		container_of(execlists, typeof(*engine), execlists);

	return __unwind_incomplete_requests(engine);
}

static inline void
execlists_context_status_change(struct i915_request *rq, unsigned long status)
{
	/*
	 * Only used when GVT-g is enabled now. When GVT-g is disabled,
	 * The compiler should eliminate this function as dead-code.
	 */
	if (!IS_ENABLED(CONFIG_DRM_I915_GVT))
		return;

	atomic_notifier_call_chain(&rq->engine->context_status_notifier,
				   status, rq);
}

static inline struct i915_request *
execlists_schedule_in(struct i915_request *rq, int idx)
{
	struct intel_context *ce = rq->hw_context;
	int count;

	trace_i915_request_in(rq, idx);

	count = intel_context_inflight_count(ce);
	if (!count) {
		intel_context_get(ce);
		ce->inflight = rq->engine;

		execlists_context_status_change(rq, INTEL_CONTEXT_SCHEDULE_IN);
		intel_engine_context_in(ce->inflight);
	}

	intel_context_inflight_inc(ce);
	GEM_BUG_ON(intel_context_inflight(ce) != rq->engine);

	return i915_request_get(rq);
}

static void kick_siblings(struct i915_request *rq, struct intel_context *ce)
{
	struct virtual_engine *ve = container_of(ce, typeof(*ve), context);
	struct i915_request *next = READ_ONCE(ve->request);

	if (next && next->execution_mask & ~rq->execution_mask)
		tasklet_schedule(&ve->base.execlists.tasklet);
}

static inline void
execlists_schedule_out(struct i915_request *rq)
{
	struct intel_context *ce = rq->hw_context;

	GEM_BUG_ON(!intel_context_inflight_count(ce));

	trace_i915_request_out(rq);

	intel_context_inflight_dec(ce);
	if (!intel_context_inflight_count(ce)) {
		intel_engine_context_out(ce->inflight);
		execlists_context_status_change(rq, INTEL_CONTEXT_SCHEDULE_OUT);

		/*
		 * If this is part of a virtual engine, its next request may
		 * have been blocked waiting for access to the active context.
		 * We have to kick all the siblings again in case we need to
		 * switch (e.g. the next request is not runnable on this
		 * engine). Hopefully, we will already have submitted the next
		 * request before the tasklet runs and do not need to rebuild
		 * each virtual tree and kick everyone again.
		 */
		ce->inflight = NULL;
		if (rq->engine != ce->engine)
			kick_siblings(rq, ce);

		intel_context_put(ce);
	}

	i915_request_put(rq);
}

static u64 execlists_update_context(const struct i915_request *rq)
{
	struct intel_context *ce = rq->hw_context;
	u64 desc;

	ce->lrc_reg_state[CTX_RING_TAIL + 1] =
		intel_ring_set_tail(rq->ring, rq->tail);

	/*
	 * Make sure the context image is complete before we submit it to HW.
	 *
	 * Ostensibly, writes (including the WCB) should be flushed prior to
	 * an uncached write such as our mmio register access, the empirical
	 * evidence (esp. on Braswell) suggests that the WC write into memory
	 * may not be visible to the HW prior to the completion of the UC
	 * register write and that we may begin execution from the context
	 * before its image is complete leading to invalid PD chasing.
	 *
	 * Furthermore, Braswell, at least, wants a full mb to be sure that
	 * the writes are coherent in memory (visible to the GPU) prior to
	 * execution, and not just visible to other CPUs (as is the result of
	 * wmb).
	 */
	mb();

	desc = ce->lrc_desc;
	ce->lrc_desc &= ~CTX_DESC_FORCE_RESTORE;

	return desc;
}

static inline void write_desc(struct intel_engine_execlists *execlists, u64 desc, u32 port)
{
	if (execlists->ctrl_reg) {
		writel(lower_32_bits(desc), execlists->submit_reg + port * 2);
		writel(upper_32_bits(desc), execlists->submit_reg + port * 2 + 1);
	} else {
		writel(upper_32_bits(desc), execlists->submit_reg);
		writel(lower_32_bits(desc), execlists->submit_reg);
	}
}

static __maybe_unused void
trace_ports(const struct intel_engine_execlists *execlists,
	    const char *msg,
	    struct i915_request * const *ports)
{
	const struct intel_engine_cs *engine =
		container_of(execlists, typeof(*engine), execlists);

	GEM_TRACE("%s: %s { %llx:%lld%s, %llx:%lld }\n",
		  engine->name, msg,
		  ports[0]->fence.context,
		  ports[0]->fence.seqno,
		  i915_request_completed(ports[0]) ? "!" :
		  i915_request_started(ports[0]) ? "*" :
		  "",
		  ports[1] ? ports[1]->fence.context : 0,
		  ports[1] ? ports[1]->fence.seqno : 0);
}

static __maybe_unused bool
assert_pending_valid(const struct intel_engine_execlists *execlists,
		     const char *msg)
{
	struct i915_request * const *port, *rq;
	struct intel_context *ce = NULL;

	trace_ports(execlists, msg, execlists->pending);

	if (execlists->pending[execlists_num_ports(execlists)])
		return false;

	for (port = execlists->pending; (rq = *port); port++) {
		if (ce == rq->hw_context)
			return false;

		ce = rq->hw_context;
		if (i915_request_completed(rq))
			continue;

		if (i915_active_is_idle(&ce->active))
			return false;

		if (!i915_vma_is_pinned(ce->state))
			return false;
	}

	return ce;
}

static void execlists_submit_ports(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists *execlists = &engine->execlists;
	unsigned int n;

	GEM_BUG_ON(!assert_pending_valid(execlists, "submit"));

	/*
	 * We can skip acquiring intel_runtime_pm_get() here as it was taken
	 * on our behalf by the request (see i915_gem_mark_busy()) and it will
	 * not be relinquished until the device is idle (see
	 * i915_gem_idle_work_handler()). As a precaution, we make sure
	 * that all ELSP are drained i.e. we have processed the CSB,
	 * before allowing ourselves to idle and calling intel_runtime_pm_put().
	 */
	GEM_BUG_ON(!intel_engine_pm_is_awake(engine));

	/*
	 * ELSQ note: the submit queue is not cleared after being submitted
	 * to the HW so we need to make sure we always clean it up. This is
	 * currently ensured by the fact that we always write the same number
	 * of elsq entries, keep this in mind before changing the loop below.
	 */
	for (n = execlists_num_ports(execlists); n--; ) {
		struct i915_request *rq = execlists->pending[n];

		write_desc(execlists,
			   rq ? execlists_update_context(rq) : 0,
			   n);
	}

	/* we need to manually load the submit queue */
	if (execlists->ctrl_reg)
		writel(EL_CTRL_LOAD, execlists->ctrl_reg);
}

static bool ctx_single_port_submission(const struct intel_context *ce)
{
	return (IS_ENABLED(CONFIG_DRM_I915_GVT) &&
		i915_gem_context_force_single_submission(ce->gem_context));
}

static bool can_merge_ctx(const struct intel_context *prev,
			  const struct intel_context *next)
{
	if (prev != next)
		return false;

	if (ctx_single_port_submission(prev))
		return false;

	return true;
}

static bool can_merge_rq(const struct i915_request *prev,
			 const struct i915_request *next)
{
	GEM_BUG_ON(prev == next);
	GEM_BUG_ON(!assert_priority_queue(prev, next));

	if (!can_merge_ctx(prev->hw_context, next->hw_context))
		return false;

	return true;
}

static void virtual_update_register_offsets(u32 *regs,
					    struct intel_engine_cs *engine)
{
	u32 base = engine->mmio_base;

	/* Must match execlists_init_reg_state()! */

	regs[CTX_CONTEXT_CONTROL] =
		i915_mmio_reg_offset(RING_CONTEXT_CONTROL(base));
	regs[CTX_RING_HEAD] = i915_mmio_reg_offset(RING_HEAD(base));
	regs[CTX_RING_TAIL] = i915_mmio_reg_offset(RING_TAIL(base));
	regs[CTX_RING_BUFFER_START] = i915_mmio_reg_offset(RING_START(base));
	regs[CTX_RING_BUFFER_CONTROL] = i915_mmio_reg_offset(RING_CTL(base));

	regs[CTX_BB_HEAD_U] = i915_mmio_reg_offset(RING_BBADDR_UDW(base));
	regs[CTX_BB_HEAD_L] = i915_mmio_reg_offset(RING_BBADDR(base));
	regs[CTX_BB_STATE] = i915_mmio_reg_offset(RING_BBSTATE(base));
	regs[CTX_SECOND_BB_HEAD_U] =
		i915_mmio_reg_offset(RING_SBBADDR_UDW(base));
	regs[CTX_SECOND_BB_HEAD_L] = i915_mmio_reg_offset(RING_SBBADDR(base));
	regs[CTX_SECOND_BB_STATE] = i915_mmio_reg_offset(RING_SBBSTATE(base));

	regs[CTX_CTX_TIMESTAMP] =
		i915_mmio_reg_offset(RING_CTX_TIMESTAMP(base));
	regs[CTX_PDP3_UDW] = i915_mmio_reg_offset(GEN8_RING_PDP_UDW(base, 3));
	regs[CTX_PDP3_LDW] = i915_mmio_reg_offset(GEN8_RING_PDP_LDW(base, 3));
	regs[CTX_PDP2_UDW] = i915_mmio_reg_offset(GEN8_RING_PDP_UDW(base, 2));
	regs[CTX_PDP2_LDW] = i915_mmio_reg_offset(GEN8_RING_PDP_LDW(base, 2));
	regs[CTX_PDP1_UDW] = i915_mmio_reg_offset(GEN8_RING_PDP_UDW(base, 1));
	regs[CTX_PDP1_LDW] = i915_mmio_reg_offset(GEN8_RING_PDP_LDW(base, 1));
	regs[CTX_PDP0_UDW] = i915_mmio_reg_offset(GEN8_RING_PDP_UDW(base, 0));
	regs[CTX_PDP0_LDW] = i915_mmio_reg_offset(GEN8_RING_PDP_LDW(base, 0));

	if (engine->class == RENDER_CLASS) {
		regs[CTX_RCS_INDIRECT_CTX] =
			i915_mmio_reg_offset(RING_INDIRECT_CTX(base));
		regs[CTX_RCS_INDIRECT_CTX_OFFSET] =
			i915_mmio_reg_offset(RING_INDIRECT_CTX_OFFSET(base));
		regs[CTX_BB_PER_CTX_PTR] =
			i915_mmio_reg_offset(RING_BB_PER_CTX_PTR(base));

		regs[CTX_R_PWR_CLK_STATE] =
			i915_mmio_reg_offset(GEN8_R_PWR_CLK_STATE);
	}
}

static bool virtual_matches(const struct virtual_engine *ve,
			    const struct i915_request *rq,
			    const struct intel_engine_cs *engine)
{
	const struct intel_engine_cs *inflight;

	if (!(rq->execution_mask & engine->mask)) /* We peeked too soon! */
		return false;

	/*
	 * We track when the HW has completed saving the context image
	 * (i.e. when we have seen the final CS event switching out of
	 * the context) and must not overwrite the context image before
	 * then. This restricts us to only using the active engine
	 * while the previous virtualized request is inflight (so
	 * we reuse the register offsets). This is a very small
	 * hystersis on the greedy seelction algorithm.
	 */
	inflight = intel_context_inflight(&ve->context);
	if (inflight && inflight != engine)
		return false;

	return true;
}

static void virtual_xfer_breadcrumbs(struct virtual_engine *ve,
				     struct intel_engine_cs *engine)
{
	struct intel_engine_cs *old = ve->siblings[0];

	/* All unattached (rq->engine == old) must already be completed */

	spin_lock(&old->breadcrumbs.irq_lock);
	if (!list_empty(&ve->context.signal_link)) {
		list_move_tail(&ve->context.signal_link,
			       &engine->breadcrumbs.signalers);
		intel_engine_queue_breadcrumbs(engine);
	}
	spin_unlock(&old->breadcrumbs.irq_lock);
}

static struct i915_request *
last_active(const struct intel_engine_execlists *execlists)
{
	struct i915_request * const *last = execlists->active;

	while (*last && i915_request_completed(*last))
		last++;

	return *last;
}

static void defer_request(struct i915_request *rq, struct list_head * const pl)
{
	LIST_HEAD(list);

	/*
	 * We want to move the interrupted request to the back of
	 * the round-robin list (i.e. its priority level), but
	 * in doing so, we must then move all requests that were in
	 * flight and were waiting for the interrupted request to
	 * be run after it again.
	 */
	do {
		struct i915_dependency *p;

		GEM_BUG_ON(i915_request_is_active(rq));
		list_move_tail(&rq->sched.link, pl);

		list_for_each_entry(p, &rq->sched.waiters_list, wait_link) {
			struct i915_request *w =
				container_of(p->waiter, typeof(*w), sched);

			/* Leave semaphores spinning on the other engines */
			if (w->engine != rq->engine)
				continue;

			/* No waiter should start before its signaler */
			GEM_BUG_ON(i915_request_started(w) &&
				   !i915_request_completed(rq));

			GEM_BUG_ON(i915_request_is_active(w));
			if (list_empty(&w->sched.link))
				continue; /* Not yet submitted; unready */

			if (rq_prio(w) < rq_prio(rq))
				continue;

			GEM_BUG_ON(rq_prio(w) > rq_prio(rq));
			list_move_tail(&w->sched.link, &list);
		}

		rq = list_first_entry_or_null(&list, typeof(*rq), sched.link);
	} while (rq);
}

static void defer_active(struct intel_engine_cs *engine)
{
	struct i915_request *rq;

	rq = __unwind_incomplete_requests(engine);
	if (!rq)
		return;

	defer_request(rq, i915_sched_lookup_priolist(engine, rq_prio(rq)));
}

static bool
need_timeslice(struct intel_engine_cs *engine, const struct i915_request *rq)
{
	int hint;

	if (list_is_last(&rq->sched.link, &engine->active.requests))
		return false;

	hint = max(rq_prio(list_next_entry(rq, sched.link)),
		   engine->execlists.queue_priority_hint);

	return hint >= effective_prio(rq);
}

static bool
enable_timeslice(struct intel_engine_cs *engine)
{
	struct i915_request *last = last_active(&engine->execlists);

	return last && need_timeslice(engine, last);
}

static void execlists_dequeue(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;
	struct i915_request **port = execlists->pending;
	struct i915_request ** const last_port = port + execlists->port_mask;
	struct i915_request *last;
	struct rb_node *rb;
	bool submit = false;

	/*
	 * Hardware submission is through 2 ports. Conceptually each port
	 * has a (RING_START, RING_HEAD, RING_TAIL) tuple. RING_START is
	 * static for a context, and unique to each, so we only execute
	 * requests belonging to a single context from each ring. RING_HEAD
	 * is maintained by the CS in the context image, it marks the place
	 * where it got up to last time, and through RING_TAIL we tell the CS
	 * where we want to execute up to this time.
	 *
	 * In this list the requests are in order of execution. Consecutive
	 * requests from the same context are adjacent in the ringbuffer. We
	 * can combine these requests into a single RING_TAIL update:
	 *
	 *              RING_HEAD...req1...req2
	 *                                    ^- RING_TAIL
	 * since to execute req2 the CS must first execute req1.
	 *
	 * Our goal then is to point each port to the end of a consecutive
	 * sequence of requests as being the most optimal (fewest wake ups
	 * and context switches) submission.
	 */

	for (rb = rb_first_cached(&execlists->virtual); rb; ) {
		struct virtual_engine *ve =
			rb_entry(rb, typeof(*ve), nodes[engine->id].rb);
		struct i915_request *rq = READ_ONCE(ve->request);

		if (!rq) { /* lazily cleanup after another engine handled rq */
			rb_erase_cached(rb, &execlists->virtual);
			RB_CLEAR_NODE(rb);
			rb = rb_first_cached(&execlists->virtual);
			continue;
		}

		if (!virtual_matches(ve, rq, engine)) {
			rb = rb_next(rb);
			continue;
		}

		break;
	}

	/*
	 * If the queue is higher priority than the last
	 * request in the currently active context, submit afresh.
	 * We will resubmit again afterwards in case we need to split
	 * the active context to interject the preemption request,
	 * i.e. we will retrigger preemption following the ack in case
	 * of trouble.
	 */
	last = last_active(execlists);
	if (last) {
		if (need_preempt(engine, last, rb)) {
			GEM_TRACE("%s: preempting last=%llx:%lld, prio=%d, hint=%d\n",
				  engine->name,
				  last->fence.context,
				  last->fence.seqno,
				  last->sched.attr.priority,
				  execlists->queue_priority_hint);
			/*
			 * Don't let the RING_HEAD advance past the breadcrumb
			 * as we unwind (and until we resubmit) so that we do
			 * not accidentally tell it to go backwards.
			 */
			ring_set_paused(engine, 1);

			/*
			 * Note that we have not stopped the GPU at this point,
			 * so we are unwinding the incomplete requests as they
			 * remain inflight and so by the time we do complete
			 * the preemption, some of the unwound requests may
			 * complete!
			 */
			__unwind_incomplete_requests(engine);

			/*
			 * If we need to return to the preempted context, we
			 * need to skip the lite-restore and force it to
			 * reload the RING_TAIL. Otherwise, the HW has a
			 * tendency to ignore us rewinding the TAIL to the
			 * end of an earlier request.
			 */
			last->hw_context->lrc_desc |= CTX_DESC_FORCE_RESTORE;
			last = NULL;
		} else if (need_timeslice(engine, last) &&
			   !timer_pending(&engine->execlists.timer)) {
			GEM_TRACE("%s: expired last=%llx:%lld, prio=%d, hint=%d\n",
				  engine->name,
				  last->fence.context,
				  last->fence.seqno,
				  last->sched.attr.priority,
				  execlists->queue_priority_hint);

			ring_set_paused(engine, 1);
			defer_active(engine);

			/*
			 * Unlike for preemption, if we rewind and continue
			 * executing the same context as previously active,
			 * the order of execution will remain the same and
			 * the tail will only advance. We do not need to
			 * force a full context restore, as a lite-restore
			 * is sufficient to resample the monotonic TAIL.
			 *
			 * If we switch to any other context, similarly we
			 * will not rewind TAIL of current context, and
			 * normal save/restore will preserve state and allow
			 * us to later continue executing the same request.
			 */
			last = NULL;
		} else {
			/*
			 * Otherwise if we already have a request pending
			 * for execution after the current one, we can
			 * just wait until the next CS event before
			 * queuing more. In either case we will force a
			 * lite-restore preemption event, but if we wait
			 * we hopefully coalesce several updates into a single
			 * submission.
			 */
			if (!list_is_last(&last->sched.link,
					  &engine->active.requests))
				return;

			/*
			 * WaIdleLiteRestore:bdw,skl
			 * Apply the wa NOOPs to prevent
			 * ring:HEAD == rq:TAIL as we resubmit the
			 * request. See gen8_emit_fini_breadcrumb() for
			 * where we prepare the padding after the
			 * end of the request.
			 */
			last->tail = last->wa_tail;
		}
	}

	while (rb) { /* XXX virtual is always taking precedence */
		struct virtual_engine *ve =
			rb_entry(rb, typeof(*ve), nodes[engine->id].rb);
		struct i915_request *rq;

		spin_lock(&ve->base.active.lock);

		rq = ve->request;
		if (unlikely(!rq)) { /* lost the race to a sibling */
			spin_unlock(&ve->base.active.lock);
			rb_erase_cached(rb, &execlists->virtual);
			RB_CLEAR_NODE(rb);
			rb = rb_first_cached(&execlists->virtual);
			continue;
		}

		GEM_BUG_ON(rq != ve->request);
		GEM_BUG_ON(rq->engine != &ve->base);
		GEM_BUG_ON(rq->hw_context != &ve->context);

		if (rq_prio(rq) >= queue_prio(execlists)) {
			if (!virtual_matches(ve, rq, engine)) {
				spin_unlock(&ve->base.active.lock);
				rb = rb_next(rb);
				continue;
			}

			if (i915_request_completed(rq)) {
				ve->request = NULL;
				ve->base.execlists.queue_priority_hint = INT_MIN;
				rb_erase_cached(rb, &execlists->virtual);
				RB_CLEAR_NODE(rb);

				rq->engine = engine;
				__i915_request_submit(rq);

				spin_unlock(&ve->base.active.lock);

				rb = rb_first_cached(&execlists->virtual);
				continue;
			}

			if (last && !can_merge_rq(last, rq)) {
				spin_unlock(&ve->base.active.lock);
				return; /* leave this for another */
			}

			GEM_TRACE("%s: virtual rq=%llx:%lld%s, new engine? %s\n",
				  engine->name,
				  rq->fence.context,
				  rq->fence.seqno,
				  i915_request_completed(rq) ? "!" :
				  i915_request_started(rq) ? "*" :
				  "",
				  yesno(engine != ve->siblings[0]));

			ve->request = NULL;
			ve->base.execlists.queue_priority_hint = INT_MIN;
			rb_erase_cached(rb, &execlists->virtual);
			RB_CLEAR_NODE(rb);

			GEM_BUG_ON(!(rq->execution_mask & engine->mask));
			rq->engine = engine;

			if (engine != ve->siblings[0]) {
				u32 *regs = ve->context.lrc_reg_state;
				unsigned int n;

				GEM_BUG_ON(READ_ONCE(ve->context.inflight));
				virtual_update_register_offsets(regs, engine);

				if (!list_empty(&ve->context.signals))
					virtual_xfer_breadcrumbs(ve, engine);

				/*
				 * Move the bound engine to the top of the list
				 * for future execution. We then kick this
				 * tasklet first before checking others, so that
				 * we preferentially reuse this set of bound
				 * registers.
				 */
				for (n = 1; n < ve->num_siblings; n++) {
					if (ve->siblings[n] == engine) {
						swap(ve->siblings[n],
						     ve->siblings[0]);
						break;
					}
				}

				GEM_BUG_ON(ve->siblings[0] != engine);
			}

			__i915_request_submit(rq);
			if (!i915_request_completed(rq)) {
				submit = true;
				last = rq;
			}
		}

		spin_unlock(&ve->base.active.lock);
		break;
	}

	while ((rb = rb_first_cached(&execlists->queue))) {
		struct i915_priolist *p = to_priolist(rb);
		struct i915_request *rq, *rn;
		int i;

		priolist_for_each_request_consume(rq, rn, p, i) {
			if (i915_request_completed(rq))
				goto skip;

			/*
			 * Can we combine this request with the current port?
			 * It has to be the same context/ringbuffer and not
			 * have any exceptions (e.g. GVT saying never to
			 * combine contexts).
			 *
			 * If we can combine the requests, we can execute both
			 * by updating the RING_TAIL to point to the end of the
			 * second request, and so we never need to tell the
			 * hardware about the first.
			 */
			if (last && !can_merge_rq(last, rq)) {
				/*
				 * If we are on the second port and cannot
				 * combine this request with the last, then we
				 * are done.
				 */
				if (port == last_port)
					goto done;

				/*
				 * We must not populate both ELSP[] with the
				 * same LRCA, i.e. we must submit 2 different
				 * contexts if we submit 2 ELSP.
				 */
				if (last->hw_context == rq->hw_context)
					goto done;

				/*
				 * If GVT overrides us we only ever submit
				 * port[0], leaving port[1] empty. Note that we
				 * also have to be careful that we don't queue
				 * the same context (even though a different
				 * request) to the second port.
				 */
				if (ctx_single_port_submission(last->hw_context) ||
				    ctx_single_port_submission(rq->hw_context))
					goto done;

				*port = execlists_schedule_in(last, port - execlists->pending);
				port++;
			}

			last = rq;
			submit = true;
skip:
			__i915_request_submit(rq);
		}

		rb_erase_cached(&p->node, &execlists->queue);
		i915_priolist_free(p);
	}

done:
	/*
	 * Here be a bit of magic! Or sleight-of-hand, whichever you prefer.
	 *
	 * We choose the priority hint such that if we add a request of greater
	 * priority than this, we kick the submission tasklet to decide on
	 * the right order of submitting the requests to hardware. We must
	 * also be prepared to reorder requests as they are in-flight on the
	 * HW. We derive the priority hint then as the first "hole" in
	 * the HW submission ports and if there are no available slots,
	 * the priority of the lowest executing request, i.e. last.
	 *
	 * When we do receive a higher priority request ready to run from the
	 * user, see queue_request(), the priority hint is bumped to that
	 * request triggering preemption on the next dequeue (or subsequent
	 * interrupt for secondary ports).
	 */
	execlists->queue_priority_hint = queue_prio(execlists);
	GEM_TRACE("%s: queue_priority_hint:%d, submit:%s\n",
		  engine->name, execlists->queue_priority_hint,
		  yesno(submit));

	if (submit) {
		*port = execlists_schedule_in(last, port - execlists->pending);
		memset(port + 1, 0, (last_port - port) * sizeof(*port));
		execlists_submit_ports(engine);
	} else {
		ring_set_paused(engine, 0);
	}
}

void
execlists_cancel_port_requests(struct intel_engine_execlists * const execlists)
{
	struct i915_request * const *port, *rq;

	for (port = execlists->pending; (rq = *port); port++)
		execlists_schedule_out(rq);
	memset(execlists->pending, 0, sizeof(execlists->pending));

	for (port = execlists->active; (rq = *port); port++)
		execlists_schedule_out(rq);
	execlists->active =
		memset(execlists->inflight, 0, sizeof(execlists->inflight));
}

static inline void
invalidate_csb_entries(const u32 *first, const u32 *last)
{
	clflush((void *)first);
	clflush((void *)last);
}

static inline bool
reset_in_progress(const struct intel_engine_execlists *execlists)
{
	return unlikely(!__tasklet_is_enabled(&execlists->tasklet));
}

enum csb_step {
	CSB_NOP,
	CSB_PROMOTE,
	CSB_PREEMPT,
	CSB_COMPLETE,
};

static inline enum csb_step
csb_parse(const struct intel_engine_execlists *execlists, const u32 *csb)
{
	unsigned int status = *csb;

	if (status & GEN8_CTX_STATUS_IDLE_ACTIVE)
		return CSB_PROMOTE;

	if (status & GEN8_CTX_STATUS_PREEMPTED)
		return CSB_PREEMPT;

	if (*execlists->active)
		return CSB_COMPLETE;

	return CSB_NOP;
}

static void process_csb(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;
	const u32 * const buf = execlists->csb_status;
	const u8 num_entries = execlists->csb_size;
	u8 head, tail;

	lockdep_assert_held(&engine->active.lock);
	GEM_BUG_ON(USES_GUC_SUBMISSION(engine->i915));

	/*
	 * Note that csb_write, csb_status may be either in HWSP or mmio.
	 * When reading from the csb_write mmio register, we have to be
	 * careful to only use the GEN8_CSB_WRITE_PTR portion, which is
	 * the low 4bits. As it happens we know the next 4bits are always
	 * zero and so we can simply masked off the low u8 of the register
	 * and treat it identically to reading from the HWSP (without having
	 * to use explicit shifting and masking, and probably bifurcating
	 * the code to handle the legacy mmio read).
	 */
	head = execlists->csb_head;
	tail = READ_ONCE(*execlists->csb_write);
	GEM_TRACE("%s cs-irq head=%d, tail=%d\n", engine->name, head, tail);
	if (unlikely(head == tail))
		return;

	/*
	 * Hopefully paired with a wmb() in HW!
	 *
	 * We must complete the read of the write pointer before any reads
	 * from the CSB, so that we do not see stale values. Without an rmb
	 * (lfence) the HW may speculatively perform the CSB[] reads *before*
	 * we perform the READ_ONCE(*csb_write).
	 */
	rmb();

	do {
		if (++head == num_entries)
			head = 0;

		/*
		 * We are flying near dragons again.
		 *
		 * We hold a reference to the request in execlist_port[]
		 * but no more than that. We are operating in softirq
		 * context and so cannot hold any mutex or sleep. That
		 * prevents us stopping the requests we are processing
		 * in port[] from being retired simultaneously (the
		 * breadcrumb will be complete before we see the
		 * context-switch). As we only hold the reference to the
		 * request, any pointer chasing underneath the request
		 * is subject to a potential use-after-free. Thus we
		 * store all of the bookkeeping within port[] as
		 * required, and avoid using unguarded pointers beneath
		 * request itself. The same applies to the atomic
		 * status notifier.
		 */

		GEM_TRACE("%s csb[%d]: status=0x%08x:0x%08x\n",
			  engine->name, head,
			  buf[2 * head + 0], buf[2 * head + 1]);

		switch (csb_parse(execlists, buf + 2 * head)) {
		case CSB_PREEMPT: /* cancel old inflight, prepare for switch */
			trace_ports(execlists, "preempted", execlists->active);

			while (*execlists->active)
				execlists_schedule_out(*execlists->active++);

			/* fallthrough */
		case CSB_PROMOTE: /* switch pending to inflight */
			GEM_BUG_ON(*execlists->active);
			GEM_BUG_ON(!assert_pending_valid(execlists, "promote"));
			execlists->active =
				memcpy(execlists->inflight,
				       execlists->pending,
				       execlists_num_ports(execlists) *
				       sizeof(*execlists->pending));
			execlists->pending[0] = NULL;

			trace_ports(execlists, "promoted", execlists->active);

			if (enable_timeslice(engine))
				mod_timer(&execlists->timer, jiffies + 1);

			if (!inject_preempt_hang(execlists))
				ring_set_paused(engine, 0);
			break;

		case CSB_COMPLETE: /* port0 completed, advanced to port1 */
			trace_ports(execlists, "completed", execlists->active);

			/*
			 * We rely on the hardware being strongly
			 * ordered, that the breadcrumb write is
			 * coherent (visible from the CPU) before the
			 * user interrupt and CSB is processed.
			 */
			GEM_BUG_ON(!i915_request_completed(*execlists->active));
			execlists_schedule_out(*execlists->active++);

			GEM_BUG_ON(execlists->active - execlists->inflight >
				   execlists_num_ports(execlists));
			break;

		case CSB_NOP:
			break;
		}
	} while (head != tail);

	execlists->csb_head = head;

	/*
	 * Gen11 has proven to fail wrt global observation point between
	 * entry and tail update, failing on the ordering and thus
	 * we see an old entry in the context status buffer.
	 *
	 * Forcibly evict out entries for the next gpu csb update,
	 * to increase the odds that we get a fresh entries with non
	 * working hardware. The cost for doing so comes out mostly with
	 * the wash as hardware, working or not, will need to do the
	 * invalidation before.
	 */
	invalidate_csb_entries(&buf[0], &buf[num_entries - 1]);
}

static void __execlists_submission_tasklet(struct intel_engine_cs *const engine)
{
	lockdep_assert_held(&engine->active.lock);

	process_csb(engine);
	if (!engine->execlists.pending[0])
		execlists_dequeue(engine);
}

/*
 * Check the unread Context Status Buffers and manage the submission of new
 * contexts to the ELSP accordingly.
 */
static void execlists_submission_tasklet(unsigned long data)
{
	struct intel_engine_cs * const engine = (struct intel_engine_cs *)data;
	unsigned long flags;

	spin_lock_irqsave(&engine->active.lock, flags);
	__execlists_submission_tasklet(engine);
	spin_unlock_irqrestore(&engine->active.lock, flags);
}

static void execlists_submission_timer(struct timer_list *timer)
{
	struct intel_engine_cs *engine =
		from_timer(engine, timer, execlists.timer);

	/* Kick the tasklet for some interrupt coalescing and reset handling */
	tasklet_hi_schedule(&engine->execlists.tasklet);
}

static void queue_request(struct intel_engine_cs *engine,
			  struct i915_sched_node *node,
			  int prio)
{
	GEM_BUG_ON(!list_empty(&node->link));
	list_add_tail(&node->link, i915_sched_lookup_priolist(engine, prio));
}

static void __submit_queue_imm(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;

	if (reset_in_progress(execlists))
		return; /* defer until we restart the engine following reset */

	if (execlists->tasklet.func == execlists_submission_tasklet)
		__execlists_submission_tasklet(engine);
	else
		tasklet_hi_schedule(&execlists->tasklet);
}

static void submit_queue(struct intel_engine_cs *engine,
			 const struct i915_request *rq)
{
	struct intel_engine_execlists *execlists = &engine->execlists;

	if (rq_prio(rq) <= execlists->queue_priority_hint)
		return;

	execlists->queue_priority_hint = rq_prio(rq);
	__submit_queue_imm(engine);
}

static void execlists_submit_request(struct i915_request *request)
{
	struct intel_engine_cs *engine = request->engine;
	unsigned long flags;

	/* Will be called from irq-context when using foreign fences. */
	spin_lock_irqsave(&engine->active.lock, flags);

	queue_request(engine, &request->sched, rq_prio(request));

	GEM_BUG_ON(RB_EMPTY_ROOT(&engine->execlists.queue.rb_root));
	GEM_BUG_ON(list_empty(&request->sched.link));

	submit_queue(engine, request);

	spin_unlock_irqrestore(&engine->active.lock, flags);
}

static void __execlists_context_fini(struct intel_context *ce)
{
	intel_ring_put(ce->ring);
	i915_vma_put(ce->state);
}

static void execlists_context_destroy(struct kref *kref)
{
	struct intel_context *ce = container_of(kref, typeof(*ce), ref);

	GEM_BUG_ON(!i915_active_is_idle(&ce->active));
	GEM_BUG_ON(intel_context_is_pinned(ce));

	if (ce->state)
		__execlists_context_fini(ce);

	intel_context_free(ce);
}

static void execlists_context_unpin(struct intel_context *ce)
{
	i915_gem_context_unpin_hw_id(ce->gem_context);
	i915_gem_object_unpin_map(ce->state->obj);
}

static void
__execlists_update_reg_state(struct intel_context *ce,
			     struct intel_engine_cs *engine)
{
	struct intel_ring *ring = ce->ring;
	u32 *regs = ce->lrc_reg_state;

	GEM_BUG_ON(!intel_ring_offset_valid(ring, ring->head));
	GEM_BUG_ON(!intel_ring_offset_valid(ring, ring->tail));

	regs[CTX_RING_BUFFER_START + 1] = i915_ggtt_offset(ring->vma);
	regs[CTX_RING_HEAD + 1] = ring->head;
	regs[CTX_RING_TAIL + 1] = ring->tail;

	/* RPCS */
	if (engine->class == RENDER_CLASS)
		regs[CTX_R_PWR_CLK_STATE + 1] =
			intel_sseu_make_rpcs(engine->i915, &ce->sseu);
}

static int
__execlists_context_pin(struct intel_context *ce,
			struct intel_engine_cs *engine)
{
	void *vaddr;
	int ret;

	GEM_BUG_ON(!ce->gem_context->vm);

	ret = execlists_context_deferred_alloc(ce, engine);
	if (ret)
		goto err;
	GEM_BUG_ON(!ce->state);

	ret = intel_context_active_acquire(ce);
	if (ret)
		goto err;
	GEM_BUG_ON(!i915_vma_is_pinned(ce->state));

	vaddr = i915_gem_object_pin_map(ce->state->obj,
					i915_coherent_map_type(engine->i915) |
					I915_MAP_OVERRIDE);
	if (IS_ERR(vaddr)) {
		ret = PTR_ERR(vaddr);
		goto unpin_active;
	}

	ret = i915_gem_context_pin_hw_id(ce->gem_context);
	if (ret)
		goto unpin_map;

	ce->lrc_desc = lrc_descriptor(ce, engine);
	ce->lrc_reg_state = vaddr + LRC_STATE_PN * PAGE_SIZE;
	__execlists_update_reg_state(ce, engine);

	return 0;

unpin_map:
	i915_gem_object_unpin_map(ce->state->obj);
unpin_active:
	intel_context_active_release(ce);
err:
	return ret;
}

static int execlists_context_pin(struct intel_context *ce)
{
	return __execlists_context_pin(ce, ce->engine);
}

static void execlists_context_reset(struct intel_context *ce)
{
	/*
	 * Because we emit WA_TAIL_DWORDS there may be a disparity
	 * between our bookkeeping in ce->ring->head and ce->ring->tail and
	 * that stored in context. As we only write new commands from
	 * ce->ring->tail onwards, everything before that is junk. If the GPU
	 * starts reading from its RING_HEAD from the context, it may try to
	 * execute that junk and die.
	 *
	 * The contexts that are stilled pinned on resume belong to the
	 * kernel, and are local to each engine. All other contexts will
	 * have their head/tail sanitized upon pinning before use, so they
	 * will never see garbage,
	 *
	 * So to avoid that we reset the context images upon resume. For
	 * simplicity, we just zero everything out.
	 */
	intel_ring_reset(ce->ring, 0);
	__execlists_update_reg_state(ce, ce->engine);
}

static const struct intel_context_ops execlists_context_ops = {
	.pin = execlists_context_pin,
	.unpin = execlists_context_unpin,

	.enter = intel_context_enter_engine,
	.exit = intel_context_exit_engine,

	.reset = execlists_context_reset,
	.destroy = execlists_context_destroy,
};

static int gen8_emit_init_breadcrumb(struct i915_request *rq)
{
	u32 *cs;

	GEM_BUG_ON(!rq->timeline->has_initial_breadcrumb);

	cs = intel_ring_begin(rq, 6);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	/*
	 * Check if we have been preempted before we even get started.
	 *
	 * After this point i915_request_started() reports true, even if
	 * we get preempted and so are no longer running.
	 */
	*cs++ = MI_ARB_CHECK;
	*cs++ = MI_NOOP;

	*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
	*cs++ = rq->timeline->hwsp_offset;
	*cs++ = 0;
	*cs++ = rq->fence.seqno - 1;

	intel_ring_advance(rq, cs);

	/* Record the updated position of the request's payload */
	rq->infix = intel_ring_offset(rq, cs);

	return 0;
}

static int emit_pdps(struct i915_request *rq)
{
	const struct intel_engine_cs * const engine = rq->engine;
	struct i915_ppgtt * const ppgtt =
		i915_vm_to_ppgtt(rq->gem_context->vm);
	int err, i;
	u32 *cs;

	GEM_BUG_ON(intel_vgpu_active(rq->i915));

	/*
	 * Beware ye of the dragons, this sequence is magic!
	 *
	 * Small changes to this sequence can cause anything from
	 * GPU hangs to forcewake errors and machine lockups!
	 */

	/* Flush any residual operations from the context load */
	err = engine->emit_flush(rq, EMIT_FLUSH);
	if (err)
		return err;

	/* Magic required to prevent forcewake errors! */
	err = engine->emit_flush(rq, EMIT_INVALIDATE);
	if (err)
		return err;

	cs = intel_ring_begin(rq, 4 * GEN8_3LVL_PDPES + 2);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	/* Ensure the LRI have landed before we invalidate & continue */
	*cs++ = MI_LOAD_REGISTER_IMM(2 * GEN8_3LVL_PDPES) | MI_LRI_FORCE_POSTED;
	for (i = GEN8_3LVL_PDPES; i--; ) {
		const dma_addr_t pd_daddr = i915_page_dir_dma_addr(ppgtt, i);
		u32 base = engine->mmio_base;

		*cs++ = i915_mmio_reg_offset(GEN8_RING_PDP_UDW(base, i));
		*cs++ = upper_32_bits(pd_daddr);
		*cs++ = i915_mmio_reg_offset(GEN8_RING_PDP_LDW(base, i));
		*cs++ = lower_32_bits(pd_daddr);
	}
	*cs++ = MI_NOOP;

	intel_ring_advance(rq, cs);

	/* Be doubly sure the LRI have landed before proceeding */
	err = engine->emit_flush(rq, EMIT_FLUSH);
	if (err)
		return err;

	/* Re-invalidate the TLB for luck */
	return engine->emit_flush(rq, EMIT_INVALIDATE);
}

static int execlists_request_alloc(struct i915_request *request)
{
	int ret;

	GEM_BUG_ON(!intel_context_is_pinned(request->hw_context));

	/*
	 * Flush enough space to reduce the likelihood of waiting after
	 * we start building the request - in which case we will just
	 * have to repeat work.
	 */
	request->reserved_space += EXECLISTS_REQUEST_SIZE;

	/*
	 * Note that after this point, we have committed to using
	 * this request as it is being used to both track the
	 * state of engine initialisation and liveness of the
	 * golden renderstate above. Think twice before you try
	 * to cancel/unwind this request now.
	 */

	/* Unconditionally invalidate GPU caches and TLBs. */
	if (i915_vm_is_4lvl(request->gem_context->vm))
		ret = request->engine->emit_flush(request, EMIT_INVALIDATE);
	else
		ret = emit_pdps(request);
	if (ret)
		return ret;

	request->reserved_space -= EXECLISTS_REQUEST_SIZE;
	return 0;
}

/*
 * In this WA we need to set GEN8_L3SQCREG4[21:21] and reset it after
 * PIPE_CONTROL instruction. This is required for the flush to happen correctly
 * but there is a slight complication as this is applied in WA batch where the
 * values are only initialized once so we cannot take register value at the
 * beginning and reuse it further; hence we save its value to memory, upload a
 * constant value with bit21 set and then we restore it back with the saved value.
 * To simplify the WA, a constant value is formed by using the default value
 * of this register. This shouldn't be a problem because we are only modifying
 * it for a short period and this batch in non-premptible. We can ofcourse
 * use additional instructions that read the actual value of the register
 * at that time and set our bit of interest but it makes the WA complicated.
 *
 * This WA is also required for Gen9 so extracting as a function avoids
 * code duplication.
 */
static u32 *
gen8_emit_flush_coherentl3_wa(struct intel_engine_cs *engine, u32 *batch)
{
	/* NB no one else is allowed to scribble over scratch + 256! */
	*batch++ = MI_STORE_REGISTER_MEM_GEN8 | MI_SRM_LRM_GLOBAL_GTT;
	*batch++ = i915_mmio_reg_offset(GEN8_L3SQCREG4);
	*batch++ = intel_gt_scratch_offset(engine->gt,
					   INTEL_GT_SCRATCH_FIELD_COHERENTL3_WA);
	*batch++ = 0;

	*batch++ = MI_LOAD_REGISTER_IMM(1);
	*batch++ = i915_mmio_reg_offset(GEN8_L3SQCREG4);
	*batch++ = 0x40400000 | GEN8_LQSC_FLUSH_COHERENT_LINES;

	batch = gen8_emit_pipe_control(batch,
				       PIPE_CONTROL_CS_STALL |
				       PIPE_CONTROL_DC_FLUSH_ENABLE,
				       0);

	*batch++ = MI_LOAD_REGISTER_MEM_GEN8 | MI_SRM_LRM_GLOBAL_GTT;
	*batch++ = i915_mmio_reg_offset(GEN8_L3SQCREG4);
	*batch++ = intel_gt_scratch_offset(engine->gt,
					   INTEL_GT_SCRATCH_FIELD_COHERENTL3_WA);
	*batch++ = 0;

	return batch;
}

static u32 slm_offset(struct intel_engine_cs *engine)
{
	return intel_gt_scratch_offset(engine->gt,
				       INTEL_GT_SCRATCH_FIELD_CLEAR_SLM_WA);
}

/*
 * Typically we only have one indirect_ctx and per_ctx batch buffer which are
 * initialized at the beginning and shared across all contexts but this field
 * helps us to have multiple batches at different offsets and select them based
 * on a criteria. At the moment this batch always start at the beginning of the page
 * and at this point we don't have multiple wa_ctx batch buffers.
 *
 * The number of WA applied are not known at the beginning; we use this field
 * to return the no of DWORDS written.
 *
 * It is to be noted that this batch does not contain MI_BATCH_BUFFER_END
 * so it adds NOOPs as padding to make it cacheline aligned.
 * MI_BATCH_BUFFER_END will be added to perctx batch and both of them together
 * makes a complete batch buffer.
 */
static u32 *gen8_init_indirectctx_bb(struct intel_engine_cs *engine, u32 *batch)
{
	/* WaDisableCtxRestoreArbitration:bdw,chv */
	*batch++ = MI_ARB_ON_OFF | MI_ARB_DISABLE;

	/* WaFlushCoherentL3CacheLinesAtContextSwitch:bdw */
	if (IS_BROADWELL(engine->i915))
		batch = gen8_emit_flush_coherentl3_wa(engine, batch);

	/* WaClearSlmSpaceAtContextSwitch:bdw,chv */
	/* Actual scratch location is at 128 bytes offset */
	batch = gen8_emit_pipe_control(batch,
				       PIPE_CONTROL_FLUSH_L3 |
				       PIPE_CONTROL_GLOBAL_GTT_IVB |
				       PIPE_CONTROL_CS_STALL |
				       PIPE_CONTROL_QW_WRITE,
				       slm_offset(engine));

	*batch++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;

	/* Pad to end of cacheline */
	while ((unsigned long)batch % CACHELINE_BYTES)
		*batch++ = MI_NOOP;

	/*
	 * MI_BATCH_BUFFER_END is not required in Indirect ctx BB because
	 * execution depends on the length specified in terms of cache lines
	 * in the register CTX_RCS_INDIRECT_CTX
	 */

	return batch;
}

struct lri {
	i915_reg_t reg;
	u32 value;
};

static u32 *emit_lri(u32 *batch, const struct lri *lri, unsigned int count)
{
	GEM_BUG_ON(!count || count > 63);

	*batch++ = MI_LOAD_REGISTER_IMM(count);
	do {
		*batch++ = i915_mmio_reg_offset(lri->reg);
		*batch++ = lri->value;
	} while (lri++, --count);
	*batch++ = MI_NOOP;

	return batch;
}

static u32 *gen9_init_indirectctx_bb(struct intel_engine_cs *engine, u32 *batch)
{
	static const struct lri lri[] = {
		/* WaDisableGatherAtSetShaderCommonSlice:skl,bxt,kbl,glk */
		{
			COMMON_SLICE_CHICKEN2,
			__MASKED_FIELD(GEN9_DISABLE_GATHER_AT_SET_SHADER_COMMON_SLICE,
				       0),
		},

		/* BSpec: 11391 */
		{
			FF_SLICE_CHICKEN,
			__MASKED_FIELD(FF_SLICE_CHICKEN_CL_PROVOKING_VERTEX_FIX,
				       FF_SLICE_CHICKEN_CL_PROVOKING_VERTEX_FIX),
		},

		/* BSpec: 11299 */
		{
			_3D_CHICKEN3,
			__MASKED_FIELD(_3D_CHICKEN_SF_PROVOKING_VERTEX_FIX,
				       _3D_CHICKEN_SF_PROVOKING_VERTEX_FIX),
		}
	};

	*batch++ = MI_ARB_ON_OFF | MI_ARB_DISABLE;

	/* WaFlushCoherentL3CacheLinesAtContextSwitch:skl,bxt,glk */
	batch = gen8_emit_flush_coherentl3_wa(engine, batch);

	batch = emit_lri(batch, lri, ARRAY_SIZE(lri));

	/* WaMediaPoolStateCmdInWABB:bxt,glk */
	if (HAS_POOLED_EU(engine->i915)) {
		/*
		 * EU pool configuration is setup along with golden context
		 * during context initialization. This value depends on
		 * device type (2x6 or 3x6) and needs to be updated based
		 * on which subslice is disabled especially for 2x6
		 * devices, however it is safe to load default
		 * configuration of 3x6 device instead of masking off
		 * corresponding bits because HW ignores bits of a disabled
		 * subslice and drops down to appropriate config. Please
		 * see render_state_setup() in i915_gem_render_state.c for
		 * possible configurations, to avoid duplication they are
		 * not shown here again.
		 */
		*batch++ = GEN9_MEDIA_POOL_STATE;
		*batch++ = GEN9_MEDIA_POOL_ENABLE;
		*batch++ = 0x00777000;
		*batch++ = 0;
		*batch++ = 0;
		*batch++ = 0;
	}

	*batch++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;

	/* Pad to end of cacheline */
	while ((unsigned long)batch % CACHELINE_BYTES)
		*batch++ = MI_NOOP;

	return batch;
}

static u32 *
gen10_init_indirectctx_bb(struct intel_engine_cs *engine, u32 *batch)
{
	int i;

	/*
	 * WaPipeControlBefore3DStateSamplePattern: cnl
	 *
	 * Ensure the engine is idle prior to programming a
	 * 3DSTATE_SAMPLE_PATTERN during a context restore.
	 */
	batch = gen8_emit_pipe_control(batch,
				       PIPE_CONTROL_CS_STALL,
				       0);
	/*
	 * WaPipeControlBefore3DStateSamplePattern says we need 4 dwords for
	 * the PIPE_CONTROL followed by 12 dwords of 0x0, so 16 dwords in
	 * total. However, a PIPE_CONTROL is 6 dwords long, not 4, which is
	 * confusing. Since gen8_emit_pipe_control() already advances the
	 * batch by 6 dwords, we advance the other 10 here, completing a
	 * cacheline. It's not clear if the workaround requires this padding
	 * before other commands, or if it's just the regular padding we would
	 * already have for the workaround bb, so leave it here for now.
	 */
	for (i = 0; i < 10; i++)
		*batch++ = MI_NOOP;

	/* Pad to end of cacheline */
	while ((unsigned long)batch % CACHELINE_BYTES)
		*batch++ = MI_NOOP;

	return batch;
}

#define CTX_WA_BB_OBJ_SIZE (PAGE_SIZE)

static int lrc_setup_wa_ctx(struct intel_engine_cs *engine)
{
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int err;

	obj = i915_gem_object_create_shmem(engine->i915, CTX_WA_BB_OBJ_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	vma = i915_vma_instance(obj, &engine->gt->ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_GLOBAL | PIN_HIGH);
	if (err)
		goto err;

	engine->wa_ctx.vma = vma;
	return 0;

err:
	i915_gem_object_put(obj);
	return err;
}

static void lrc_destroy_wa_ctx(struct intel_engine_cs *engine)
{
	i915_vma_unpin_and_release(&engine->wa_ctx.vma, 0);
}

typedef u32 *(*wa_bb_func_t)(struct intel_engine_cs *engine, u32 *batch);

static int intel_init_workaround_bb(struct intel_engine_cs *engine)
{
	struct i915_ctx_workarounds *wa_ctx = &engine->wa_ctx;
	struct i915_wa_ctx_bb *wa_bb[2] = { &wa_ctx->indirect_ctx,
					    &wa_ctx->per_ctx };
	wa_bb_func_t wa_bb_fn[2];
	struct page *page;
	void *batch, *batch_ptr;
	unsigned int i;
	int ret;

	if (engine->class != RENDER_CLASS)
		return 0;

	switch (INTEL_GEN(engine->i915)) {
	case 11:
		return 0;
	case 10:
		wa_bb_fn[0] = gen10_init_indirectctx_bb;
		wa_bb_fn[1] = NULL;
		break;
	case 9:
		wa_bb_fn[0] = gen9_init_indirectctx_bb;
		wa_bb_fn[1] = NULL;
		break;
	case 8:
		wa_bb_fn[0] = gen8_init_indirectctx_bb;
		wa_bb_fn[1] = NULL;
		break;
	default:
		MISSING_CASE(INTEL_GEN(engine->i915));
		return 0;
	}

	ret = lrc_setup_wa_ctx(engine);
	if (ret) {
		DRM_DEBUG_DRIVER("Failed to setup context WA page: %d\n", ret);
		return ret;
	}

	page = i915_gem_object_get_dirty_page(wa_ctx->vma->obj, 0);
	batch = batch_ptr = kmap_atomic(page);

	/*
	 * Emit the two workaround batch buffers, recording the offset from the
	 * start of the workaround batch buffer object for each and their
	 * respective sizes.
	 */
	for (i = 0; i < ARRAY_SIZE(wa_bb_fn); i++) {
		wa_bb[i]->offset = batch_ptr - batch;
		if (GEM_DEBUG_WARN_ON(!IS_ALIGNED(wa_bb[i]->offset,
						  CACHELINE_BYTES))) {
			ret = -EINVAL;
			break;
		}
		if (wa_bb_fn[i])
			batch_ptr = wa_bb_fn[i](engine, batch_ptr);
		wa_bb[i]->size = batch_ptr - (batch + wa_bb[i]->offset);
	}

	BUG_ON(batch_ptr - batch > CTX_WA_BB_OBJ_SIZE);

	kunmap_atomic(batch);
	if (ret)
		lrc_destroy_wa_ctx(engine);

	return ret;
}

static void enable_execlists(struct intel_engine_cs *engine)
{
	u32 mode;

	assert_forcewakes_active(engine->uncore, FORCEWAKE_ALL);

	intel_engine_set_hwsp_writemask(engine, ~0u); /* HWSTAM */

	if (INTEL_GEN(engine->i915) >= 11)
		mode = _MASKED_BIT_ENABLE(GEN11_GFX_DISABLE_LEGACY_MODE);
	else
		mode = _MASKED_BIT_ENABLE(GFX_RUN_LIST_ENABLE);
	ENGINE_WRITE_FW(engine, RING_MODE_GEN7, mode);

	ENGINE_WRITE_FW(engine, RING_MI_MODE, _MASKED_BIT_DISABLE(STOP_RING));

	ENGINE_WRITE_FW(engine,
			RING_HWS_PGA,
			i915_ggtt_offset(engine->status_page.vma));
	ENGINE_POSTING_READ(engine, RING_HWS_PGA);
}

static bool unexpected_starting_state(struct intel_engine_cs *engine)
{
	bool unexpected = false;

	if (ENGINE_READ_FW(engine, RING_MI_MODE) & STOP_RING) {
		DRM_DEBUG_DRIVER("STOP_RING still set in RING_MI_MODE\n");
		unexpected = true;
	}

	return unexpected;
}

static int execlists_resume(struct intel_engine_cs *engine)
{
	intel_engine_apply_workarounds(engine);
	intel_engine_apply_whitelist(engine);

	intel_mocs_init_engine(engine);

	intel_engine_reset_breadcrumbs(engine);

	if (GEM_SHOW_DEBUG() && unexpected_starting_state(engine)) {
		struct drm_printer p = drm_debug_printer(__func__);

		intel_engine_dump(engine, &p, NULL);
	}

	enable_execlists(engine);

	return 0;
}

static void execlists_reset_prepare(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;
	unsigned long flags;

	GEM_TRACE("%s: depth<-%d\n", engine->name,
		  atomic_read(&execlists->tasklet.count));

	/*
	 * Prevent request submission to the hardware until we have
	 * completed the reset in i915_gem_reset_finish(). If a request
	 * is completed by one engine, it may then queue a request
	 * to a second via its execlists->tasklet *just* as we are
	 * calling engine->resume() and also writing the ELSP.
	 * Turning off the execlists->tasklet until the reset is over
	 * prevents the race.
	 */
	__tasklet_disable_sync_once(&execlists->tasklet);
	GEM_BUG_ON(!reset_in_progress(execlists));

	intel_engine_stop_cs(engine);

	/* And flush any current direct submission. */
	spin_lock_irqsave(&engine->active.lock, flags);
	spin_unlock_irqrestore(&engine->active.lock, flags);
}

static void reset_csb_pointers(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;
	const unsigned int reset_value = execlists->csb_size - 1;

	ring_set_paused(engine, 0);

	/*
	 * After a reset, the HW starts writing into CSB entry [0]. We
	 * therefore have to set our HEAD pointer back one entry so that
	 * the *first* entry we check is entry 0. To complicate this further,
	 * as we don't wait for the first interrupt after reset, we have to
	 * fake the HW write to point back to the last entry so that our
	 * inline comparison of our cached head position against the last HW
	 * write works even before the first interrupt.
	 */
	execlists->csb_head = reset_value;
	WRITE_ONCE(*execlists->csb_write, reset_value);
	wmb(); /* Make sure this is visible to HW (paranoia?) */

	invalidate_csb_entries(&execlists->csb_status[0],
			       &execlists->csb_status[reset_value]);
}

static struct i915_request *active_request(struct i915_request *rq)
{
	const struct list_head * const list = &rq->engine->active.requests;
	const struct intel_context * const context = rq->hw_context;
	struct i915_request *active = NULL;

	list_for_each_entry_from_reverse(rq, list, sched.link) {
		if (i915_request_completed(rq))
			break;

		if (rq->hw_context != context)
			break;

		active = rq;
	}

	return active;
}

static void __execlists_reset(struct intel_engine_cs *engine, bool stalled)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;
	struct intel_context *ce;
	struct i915_request *rq;
	u32 *regs;

	process_csb(engine); /* drain preemption events */

	/* Following the reset, we need to reload the CSB read/write pointers */
	reset_csb_pointers(engine);

	/*
	 * Save the currently executing context, even if we completed
	 * its request, it was still running at the time of the
	 * reset and will have been clobbered.
	 */
	rq = execlists_active(execlists);
	if (!rq)
		return;

	ce = rq->hw_context;
	GEM_BUG_ON(i915_active_is_idle(&ce->active));
	GEM_BUG_ON(!i915_vma_is_pinned(ce->state));
	rq = active_request(rq);

	/*
	 * Catch up with any missed context-switch interrupts.
	 *
	 * Ideally we would just read the remaining CSB entries now that we
	 * know the gpu is idle. However, the CSB registers are sometimes^W
	 * often trashed across a GPU reset! Instead we have to rely on
	 * guessing the missed context-switch events by looking at what
	 * requests were completed.
	 */
	execlists_cancel_port_requests(execlists);

	if (!rq) {
		ce->ring->head = ce->ring->tail;
		goto out_replay;
	}

	ce->ring->head = intel_ring_wrap(ce->ring, rq->head);

	/*
	 * If this request hasn't started yet, e.g. it is waiting on a
	 * semaphore, we need to avoid skipping the request or else we
	 * break the signaling chain. However, if the context is corrupt
	 * the request will not restart and we will be stuck with a wedged
	 * device. It is quite often the case that if we issue a reset
	 * while the GPU is loading the context image, that the context
	 * image becomes corrupt.
	 *
	 * Otherwise, if we have not started yet, the request should replay
	 * perfectly and we do not need to flag the result as being erroneous.
	 */
	if (!i915_request_started(rq))
		goto out_replay;

	/*
	 * If the request was innocent, we leave the request in the ELSP
	 * and will try to replay it on restarting. The context image may
	 * have been corrupted by the reset, in which case we may have
	 * to service a new GPU hang, but more likely we can continue on
	 * without impact.
	 *
	 * If the request was guilty, we presume the context is corrupt
	 * and have to at least restore the RING register in the context
	 * image back to the expected values to skip over the guilty request.
	 */
	i915_reset_request(rq, stalled);
	if (!stalled)
		goto out_replay;

	/*
	 * We want a simple context + ring to execute the breadcrumb update.
	 * We cannot rely on the context being intact across the GPU hang,
	 * so clear it and rebuild just what we need for the breadcrumb.
	 * All pending requests for this context will be zapped, and any
	 * future request will be after userspace has had the opportunity
	 * to recreate its own state.
	 */
	regs = ce->lrc_reg_state;
	if (engine->pinned_default_state) {
		memcpy(regs, /* skip restoring the vanilla PPHWSP */
		       engine->pinned_default_state + LRC_STATE_PN * PAGE_SIZE,
		       engine->context_size - PAGE_SIZE);
	}
	execlists_init_reg_state(regs, ce, engine, ce->ring);

out_replay:
	GEM_TRACE("%s replay {head:%04x, tail:%04x\n",
		  engine->name, ce->ring->head, ce->ring->tail);
	intel_ring_update_space(ce->ring);
	__execlists_update_reg_state(ce, engine);

	/* Push back any incomplete requests for replay after the reset. */
	__unwind_incomplete_requests(engine);
}

static void execlists_reset(struct intel_engine_cs *engine, bool stalled)
{
	unsigned long flags;

	GEM_TRACE("%s\n", engine->name);

	spin_lock_irqsave(&engine->active.lock, flags);

	__execlists_reset(engine, stalled);

	spin_unlock_irqrestore(&engine->active.lock, flags);
}

static void nop_submission_tasklet(unsigned long data)
{
	/* The driver is wedged; don't process any more events. */
}

static void execlists_cancel_requests(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;
	struct i915_request *rq, *rn;
	struct rb_node *rb;
	unsigned long flags;

	GEM_TRACE("%s\n", engine->name);

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

	__execlists_reset(engine, true);

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

	/* Cancel all attached virtual engines */
	while ((rb = rb_first_cached(&execlists->virtual))) {
		struct virtual_engine *ve =
			rb_entry(rb, typeof(*ve), nodes[engine->id].rb);

		rb_erase_cached(rb, &execlists->virtual);
		RB_CLEAR_NODE(rb);

		spin_lock(&ve->base.active.lock);
		if (ve->request) {
			ve->request->engine = engine;
			__i915_request_submit(ve->request);
			dma_fence_set_error(&ve->request->fence, -EIO);
			i915_request_mark_complete(ve->request);
			ve->base.execlists.queue_priority_hint = INT_MIN;
			ve->request = NULL;
		}
		spin_unlock(&ve->base.active.lock);
	}

	/* Remaining _unready_ requests will be nop'ed when submitted */

	execlists->queue_priority_hint = INT_MIN;
	execlists->queue = RB_ROOT_CACHED;

	GEM_BUG_ON(__tasklet_is_enabled(&execlists->tasklet));
	execlists->tasklet.func = nop_submission_tasklet;

	spin_unlock_irqrestore(&engine->active.lock, flags);
}

static void execlists_reset_finish(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;

	/*
	 * After a GPU reset, we may have requests to replay. Do so now while
	 * we still have the forcewake to be sure that the GPU is not allowed
	 * to sleep before we restart and reload a context.
	 */
	GEM_BUG_ON(!reset_in_progress(execlists));
	if (!RB_EMPTY_ROOT(&execlists->queue.rb_root))
		execlists->tasklet.func(execlists->tasklet.data);

	if (__tasklet_enable(&execlists->tasklet))
		/* And kick in case we missed a new request submission. */
		tasklet_hi_schedule(&execlists->tasklet);
	GEM_TRACE("%s: depth->%d\n", engine->name,
		  atomic_read(&execlists->tasklet.count));
}

static int gen8_emit_bb_start(struct i915_request *rq,
			      u64 offset, u32 len,
			      const unsigned int flags)
{
	u32 *cs;

	cs = intel_ring_begin(rq, 4);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	/*
	 * WaDisableCtxRestoreArbitration:bdw,chv
	 *
	 * We don't need to perform MI_ARB_ENABLE as often as we do (in
	 * particular all the gen that do not need the w/a at all!), if we
	 * took care to make sure that on every switch into this context
	 * (both ordinary and for preemption) that arbitrartion was enabled
	 * we would be fine.  However, for gen8 there is another w/a that
	 * requires us to not preempt inside GPGPU execution, so we keep
	 * arbitration disabled for gen8 batches. Arbitration will be
	 * re-enabled before we close the request
	 * (engine->emit_fini_breadcrumb).
	 */
	*cs++ = MI_ARB_ON_OFF | MI_ARB_DISABLE;

	/* FIXME(BDW+): Address space and security selectors. */
	*cs++ = MI_BATCH_BUFFER_START_GEN8 |
		(flags & I915_DISPATCH_SECURE ? 0 : BIT(8));
	*cs++ = lower_32_bits(offset);
	*cs++ = upper_32_bits(offset);

	intel_ring_advance(rq, cs);

	return 0;
}

static int gen9_emit_bb_start(struct i915_request *rq,
			      u64 offset, u32 len,
			      const unsigned int flags)
{
	u32 *cs;

	cs = intel_ring_begin(rq, 6);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;

	*cs++ = MI_BATCH_BUFFER_START_GEN8 |
		(flags & I915_DISPATCH_SECURE ? 0 : BIT(8));
	*cs++ = lower_32_bits(offset);
	*cs++ = upper_32_bits(offset);

	*cs++ = MI_ARB_ON_OFF | MI_ARB_DISABLE;
	*cs++ = MI_NOOP;

	intel_ring_advance(rq, cs);

	return 0;
}

static void gen8_logical_ring_enable_irq(struct intel_engine_cs *engine)
{
	ENGINE_WRITE(engine, RING_IMR,
		     ~(engine->irq_enable_mask | engine->irq_keep_mask));
	ENGINE_POSTING_READ(engine, RING_IMR);
}

static void gen8_logical_ring_disable_irq(struct intel_engine_cs *engine)
{
	ENGINE_WRITE(engine, RING_IMR, ~engine->irq_keep_mask);
}

static int gen8_emit_flush(struct i915_request *request, u32 mode)
{
	u32 cmd, *cs;

	cs = intel_ring_begin(request, 4);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	cmd = MI_FLUSH_DW + 1;

	/* We always require a command barrier so that subsequent
	 * commands, such as breadcrumb interrupts, are strictly ordered
	 * wrt the contents of the write cache being flushed to memory
	 * (and thus being coherent from the CPU).
	 */
	cmd |= MI_FLUSH_DW_STORE_INDEX | MI_FLUSH_DW_OP_STOREDW;

	if (mode & EMIT_INVALIDATE) {
		cmd |= MI_INVALIDATE_TLB;
		if (request->engine->class == VIDEO_DECODE_CLASS)
			cmd |= MI_INVALIDATE_BSD;
	}

	*cs++ = cmd;
	*cs++ = I915_GEM_HWS_SCRATCH_ADDR | MI_FLUSH_DW_USE_GTT;
	*cs++ = 0; /* upper addr */
	*cs++ = 0; /* value */
	intel_ring_advance(request, cs);

	return 0;
}

static int gen8_emit_flush_render(struct i915_request *request,
				  u32 mode)
{
	struct intel_engine_cs *engine = request->engine;
	u32 scratch_addr =
		intel_gt_scratch_offset(engine->gt,
					INTEL_GT_SCRATCH_FIELD_RENDER_FLUSH);
	bool vf_flush_wa = false, dc_flush_wa = false;
	u32 *cs, flags = 0;
	int len;

	flags |= PIPE_CONTROL_CS_STALL;

	if (mode & EMIT_FLUSH) {
		flags |= PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH;
		flags |= PIPE_CONTROL_DEPTH_CACHE_FLUSH;
		flags |= PIPE_CONTROL_DC_FLUSH_ENABLE;
		flags |= PIPE_CONTROL_FLUSH_ENABLE;
	}

	if (mode & EMIT_INVALIDATE) {
		flags |= PIPE_CONTROL_TLB_INVALIDATE;
		flags |= PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_VF_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_CONST_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_STATE_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_QW_WRITE;
		flags |= PIPE_CONTROL_GLOBAL_GTT_IVB;

		/*
		 * On GEN9: before VF_CACHE_INVALIDATE we need to emit a NULL
		 * pipe control.
		 */
		if (IS_GEN(request->i915, 9))
			vf_flush_wa = true;

		/* WaForGAMHang:kbl */
		if (IS_KBL_REVID(request->i915, 0, KBL_REVID_B0))
			dc_flush_wa = true;
	}

	len = 6;

	if (vf_flush_wa)
		len += 6;

	if (dc_flush_wa)
		len += 12;

	cs = intel_ring_begin(request, len);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	if (vf_flush_wa)
		cs = gen8_emit_pipe_control(cs, 0, 0);

	if (dc_flush_wa)
		cs = gen8_emit_pipe_control(cs, PIPE_CONTROL_DC_FLUSH_ENABLE,
					    0);

	cs = gen8_emit_pipe_control(cs, flags, scratch_addr);

	if (dc_flush_wa)
		cs = gen8_emit_pipe_control(cs, PIPE_CONTROL_CS_STALL, 0);

	intel_ring_advance(request, cs);

	return 0;
}

/*
 * Reserve space for 2 NOOPs at the end of each request to be
 * used as a workaround for not being allowed to do lite
 * restore with HEAD==TAIL (WaIdleLiteRestore).
 */
static u32 *gen8_emit_wa_tail(struct i915_request *request, u32 *cs)
{
	/* Ensure there's always at least one preemption point per-request. */
	*cs++ = MI_ARB_CHECK;
	*cs++ = MI_NOOP;
	request->wa_tail = intel_ring_offset(request, cs);

	return cs;
}

static u32 *emit_preempt_busywait(struct i915_request *request, u32 *cs)
{
	*cs++ = MI_SEMAPHORE_WAIT |
		MI_SEMAPHORE_GLOBAL_GTT |
		MI_SEMAPHORE_POLL |
		MI_SEMAPHORE_SAD_EQ_SDD;
	*cs++ = 0;
	*cs++ = intel_hws_preempt_address(request->engine);
	*cs++ = 0;

	return cs;
}

static u32 *gen8_emit_fini_breadcrumb(struct i915_request *request, u32 *cs)
{
	cs = gen8_emit_ggtt_write(cs,
				  request->fence.seqno,
				  request->timeline->hwsp_offset,
				  0);
	*cs++ = MI_USER_INTERRUPT;

	*cs++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;
	cs = emit_preempt_busywait(request, cs);

	request->tail = intel_ring_offset(request, cs);
	assert_ring_tail_valid(request->ring, request->tail);

	return gen8_emit_wa_tail(request, cs);
}

static u32 *gen8_emit_fini_breadcrumb_rcs(struct i915_request *request, u32 *cs)
{
	/* XXX flush+write+CS_STALL all in one upsets gem_concurrent_blt:kbl */
	cs = gen8_emit_ggtt_write_rcs(cs,
				      request->fence.seqno,
				      request->timeline->hwsp_offset,
				      PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH |
				      PIPE_CONTROL_DEPTH_CACHE_FLUSH |
				      PIPE_CONTROL_DC_FLUSH_ENABLE);
	cs = gen8_emit_pipe_control(cs,
				    PIPE_CONTROL_FLUSH_ENABLE |
				    PIPE_CONTROL_CS_STALL,
				    0);
	*cs++ = MI_USER_INTERRUPT;

	*cs++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;
	cs = emit_preempt_busywait(request, cs);

	request->tail = intel_ring_offset(request, cs);
	assert_ring_tail_valid(request->ring, request->tail);

	return gen8_emit_wa_tail(request, cs);
}

static int gen8_init_rcs_context(struct i915_request *rq)
{
	int ret;

	ret = intel_engine_emit_ctx_wa(rq);
	if (ret)
		return ret;

	ret = intel_rcs_context_init_mocs(rq);
	/*
	 * Failing to program the MOCS is non-fatal.The system will not
	 * run at peak performance. So generate an error and carry on.
	 */
	if (ret)
		DRM_ERROR("MOCS failed to program: expect performance issues.\n");

	return intel_renderstate_emit(rq);
}

static void execlists_park(struct intel_engine_cs *engine)
{
	del_timer_sync(&engine->execlists.timer);
	intel_engine_park(engine);
}

void intel_execlists_set_default_submission(struct intel_engine_cs *engine)
{
	engine->submit_request = execlists_submit_request;
	engine->cancel_requests = execlists_cancel_requests;
	engine->schedule = i915_schedule;
	engine->execlists.tasklet.func = execlists_submission_tasklet;

	engine->reset.prepare = execlists_reset_prepare;
	engine->reset.reset = execlists_reset;
	engine->reset.finish = execlists_reset_finish;

	engine->park = execlists_park;
	engine->unpark = NULL;

	engine->flags |= I915_ENGINE_SUPPORTS_STATS;
	if (!intel_vgpu_active(engine->i915))
		engine->flags |= I915_ENGINE_HAS_SEMAPHORES;
	if (HAS_LOGICAL_RING_PREEMPTION(engine->i915))
		engine->flags |= I915_ENGINE_HAS_PREEMPTION;
}

static void execlists_destroy(struct intel_engine_cs *engine)
{
	intel_engine_cleanup_common(engine);
	lrc_destroy_wa_ctx(engine);
	kfree(engine);
}

static void
logical_ring_default_vfuncs(struct intel_engine_cs *engine)
{
	/* Default vfuncs which can be overriden by each engine. */

	engine->destroy = execlists_destroy;
	engine->resume = execlists_resume;

	engine->reset.prepare = execlists_reset_prepare;
	engine->reset.reset = execlists_reset;
	engine->reset.finish = execlists_reset_finish;

	engine->cops = &execlists_context_ops;
	engine->request_alloc = execlists_request_alloc;

	engine->emit_flush = gen8_emit_flush;
	engine->emit_init_breadcrumb = gen8_emit_init_breadcrumb;
	engine->emit_fini_breadcrumb = gen8_emit_fini_breadcrumb;

	engine->set_default_submission = intel_execlists_set_default_submission;

	if (INTEL_GEN(engine->i915) < 11) {
		engine->irq_enable = gen8_logical_ring_enable_irq;
		engine->irq_disable = gen8_logical_ring_disable_irq;
	} else {
		/*
		 * TODO: On Gen11 interrupt masks need to be clear
		 * to allow C6 entry. Keep interrupts enabled at
		 * and take the hit of generating extra interrupts
		 * until a more refined solution exists.
		 */
	}
	if (IS_GEN(engine->i915, 8))
		engine->emit_bb_start = gen8_emit_bb_start;
	else
		engine->emit_bb_start = gen9_emit_bb_start;
}

static inline void
logical_ring_default_irqs(struct intel_engine_cs *engine)
{
	unsigned int shift = 0;

	if (INTEL_GEN(engine->i915) < 11) {
		const u8 irq_shifts[] = {
			[RCS0]  = GEN8_RCS_IRQ_SHIFT,
			[BCS0]  = GEN8_BCS_IRQ_SHIFT,
			[VCS0]  = GEN8_VCS0_IRQ_SHIFT,
			[VCS1]  = GEN8_VCS1_IRQ_SHIFT,
			[VECS0] = GEN8_VECS_IRQ_SHIFT,
		};

		shift = irq_shifts[engine->id];
	}

	engine->irq_enable_mask = GT_RENDER_USER_INTERRUPT << shift;
	engine->irq_keep_mask = GT_CONTEXT_SWITCH_INTERRUPT << shift;
}

int intel_execlists_submission_setup(struct intel_engine_cs *engine)
{
	/* Intentionally left blank. */
	engine->buffer = NULL;

	tasklet_init(&engine->execlists.tasklet,
		     execlists_submission_tasklet, (unsigned long)engine);
	timer_setup(&engine->execlists.timer, execlists_submission_timer, 0);

	logical_ring_default_vfuncs(engine);
	logical_ring_default_irqs(engine);

	if (engine->class == RENDER_CLASS) {
		engine->init_context = gen8_init_rcs_context;
		engine->emit_flush = gen8_emit_flush_render;
		engine->emit_fini_breadcrumb = gen8_emit_fini_breadcrumb_rcs;
	}

	return 0;
}

int intel_execlists_submission_init(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;
	struct drm_i915_private *i915 = engine->i915;
	struct intel_uncore *uncore = engine->uncore;
	u32 base = engine->mmio_base;
	int ret;

	ret = intel_engine_init_common(engine);
	if (ret)
		return ret;

	if (intel_init_workaround_bb(engine))
		/*
		 * We continue even if we fail to initialize WA batch
		 * because we only expect rare glitches but nothing
		 * critical to prevent us from using GPU
		 */
		DRM_ERROR("WA batch buffer initialization failed\n");

	if (HAS_LOGICAL_RING_ELSQ(i915)) {
		execlists->submit_reg = uncore->regs +
			i915_mmio_reg_offset(RING_EXECLIST_SQ_CONTENTS(base));
		execlists->ctrl_reg = uncore->regs +
			i915_mmio_reg_offset(RING_EXECLIST_CONTROL(base));
	} else {
		execlists->submit_reg = uncore->regs +
			i915_mmio_reg_offset(RING_ELSP(base));
	}

	execlists->csb_status =
		&engine->status_page.addr[I915_HWS_CSB_BUF0_INDEX];

	execlists->csb_write =
		&engine->status_page.addr[intel_hws_csb_write_index(i915)];

	if (INTEL_GEN(i915) < 11)
		execlists->csb_size = GEN8_CSB_ENTRIES;
	else
		execlists->csb_size = GEN11_CSB_ENTRIES;

	reset_csb_pointers(engine);

	return 0;
}

static u32 intel_lr_indirect_ctx_offset(struct intel_engine_cs *engine)
{
	u32 indirect_ctx_offset;

	switch (INTEL_GEN(engine->i915)) {
	default:
		MISSING_CASE(INTEL_GEN(engine->i915));
		/* fall through */
	case 11:
		indirect_ctx_offset =
			GEN11_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT;
		break;
	case 10:
		indirect_ctx_offset =
			GEN10_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT;
		break;
	case 9:
		indirect_ctx_offset =
			GEN9_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT;
		break;
	case 8:
		indirect_ctx_offset =
			GEN8_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT;
		break;
	}

	return indirect_ctx_offset;
}

static void execlists_init_reg_state(u32 *regs,
				     struct intel_context *ce,
				     struct intel_engine_cs *engine,
				     struct intel_ring *ring)
{
	struct i915_ppgtt *ppgtt = i915_vm_to_ppgtt(ce->gem_context->vm);
	bool rcs = engine->class == RENDER_CLASS;
	u32 base = engine->mmio_base;

	/*
	 * A context is actually a big batch buffer with several
	 * MI_LOAD_REGISTER_IMM commands followed by (reg, value) pairs. The
	 * values we are setting here are only for the first context restore:
	 * on a subsequent save, the GPU will recreate this batchbuffer with new
	 * values (including all the missing MI_LOAD_REGISTER_IMM commands that
	 * we are not initializing here).
	 *
	 * Must keep consistent with virtual_update_register_offsets().
	 */
	regs[CTX_LRI_HEADER_0] = MI_LOAD_REGISTER_IMM(rcs ? 14 : 11) |
				 MI_LRI_FORCE_POSTED;

	CTX_REG(regs, CTX_CONTEXT_CONTROL, RING_CONTEXT_CONTROL(base),
		_MASKED_BIT_DISABLE(CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT) |
		_MASKED_BIT_ENABLE(CTX_CTRL_INHIBIT_SYN_CTX_SWITCH));
	if (INTEL_GEN(engine->i915) < 11) {
		regs[CTX_CONTEXT_CONTROL + 1] |=
			_MASKED_BIT_DISABLE(CTX_CTRL_ENGINE_CTX_SAVE_INHIBIT |
					    CTX_CTRL_RS_CTX_ENABLE);
	}
	CTX_REG(regs, CTX_RING_HEAD, RING_HEAD(base), 0);
	CTX_REG(regs, CTX_RING_TAIL, RING_TAIL(base), 0);
	CTX_REG(regs, CTX_RING_BUFFER_START, RING_START(base), 0);
	CTX_REG(regs, CTX_RING_BUFFER_CONTROL, RING_CTL(base),
		RING_CTL_SIZE(ring->size) | RING_VALID);
	CTX_REG(regs, CTX_BB_HEAD_U, RING_BBADDR_UDW(base), 0);
	CTX_REG(regs, CTX_BB_HEAD_L, RING_BBADDR(base), 0);
	CTX_REG(regs, CTX_BB_STATE, RING_BBSTATE(base), RING_BB_PPGTT);
	CTX_REG(regs, CTX_SECOND_BB_HEAD_U, RING_SBBADDR_UDW(base), 0);
	CTX_REG(regs, CTX_SECOND_BB_HEAD_L, RING_SBBADDR(base), 0);
	CTX_REG(regs, CTX_SECOND_BB_STATE, RING_SBBSTATE(base), 0);
	if (rcs) {
		struct i915_ctx_workarounds *wa_ctx = &engine->wa_ctx;

		CTX_REG(regs, CTX_RCS_INDIRECT_CTX, RING_INDIRECT_CTX(base), 0);
		CTX_REG(regs, CTX_RCS_INDIRECT_CTX_OFFSET,
			RING_INDIRECT_CTX_OFFSET(base), 0);
		if (wa_ctx->indirect_ctx.size) {
			u32 ggtt_offset = i915_ggtt_offset(wa_ctx->vma);

			regs[CTX_RCS_INDIRECT_CTX + 1] =
				(ggtt_offset + wa_ctx->indirect_ctx.offset) |
				(wa_ctx->indirect_ctx.size / CACHELINE_BYTES);

			regs[CTX_RCS_INDIRECT_CTX_OFFSET + 1] =
				intel_lr_indirect_ctx_offset(engine) << 6;
		}

		CTX_REG(regs, CTX_BB_PER_CTX_PTR, RING_BB_PER_CTX_PTR(base), 0);
		if (wa_ctx->per_ctx.size) {
			u32 ggtt_offset = i915_ggtt_offset(wa_ctx->vma);

			regs[CTX_BB_PER_CTX_PTR + 1] =
				(ggtt_offset + wa_ctx->per_ctx.offset) | 0x01;
		}
	}

	regs[CTX_LRI_HEADER_1] = MI_LOAD_REGISTER_IMM(9) | MI_LRI_FORCE_POSTED;

	CTX_REG(regs, CTX_CTX_TIMESTAMP, RING_CTX_TIMESTAMP(base), 0);
	/* PDP values well be assigned later if needed */
	CTX_REG(regs, CTX_PDP3_UDW, GEN8_RING_PDP_UDW(base, 3), 0);
	CTX_REG(regs, CTX_PDP3_LDW, GEN8_RING_PDP_LDW(base, 3), 0);
	CTX_REG(regs, CTX_PDP2_UDW, GEN8_RING_PDP_UDW(base, 2), 0);
	CTX_REG(regs, CTX_PDP2_LDW, GEN8_RING_PDP_LDW(base, 2), 0);
	CTX_REG(regs, CTX_PDP1_UDW, GEN8_RING_PDP_UDW(base, 1), 0);
	CTX_REG(regs, CTX_PDP1_LDW, GEN8_RING_PDP_LDW(base, 1), 0);
	CTX_REG(regs, CTX_PDP0_UDW, GEN8_RING_PDP_UDW(base, 0), 0);
	CTX_REG(regs, CTX_PDP0_LDW, GEN8_RING_PDP_LDW(base, 0), 0);

	if (i915_vm_is_4lvl(&ppgtt->vm)) {
		/* 64b PPGTT (48bit canonical)
		 * PDP0_DESCRIPTOR contains the base address to PML4 and
		 * other PDP Descriptors are ignored.
		 */
		ASSIGN_CTX_PML4(ppgtt, regs);
	} else {
		ASSIGN_CTX_PDP(ppgtt, regs, 3);
		ASSIGN_CTX_PDP(ppgtt, regs, 2);
		ASSIGN_CTX_PDP(ppgtt, regs, 1);
		ASSIGN_CTX_PDP(ppgtt, regs, 0);
	}

	if (rcs) {
		regs[CTX_LRI_HEADER_2] = MI_LOAD_REGISTER_IMM(1);
		CTX_REG(regs, CTX_R_PWR_CLK_STATE, GEN8_R_PWR_CLK_STATE, 0);

		i915_oa_init_reg_state(engine, ce, regs);
	}

	regs[CTX_END] = MI_BATCH_BUFFER_END;
	if (INTEL_GEN(engine->i915) >= 10)
		regs[CTX_END] |= BIT(0);
}

static int
populate_lr_context(struct intel_context *ce,
		    struct drm_i915_gem_object *ctx_obj,
		    struct intel_engine_cs *engine,
		    struct intel_ring *ring)
{
	void *vaddr;
	u32 *regs;
	int ret;

	vaddr = i915_gem_object_pin_map(ctx_obj, I915_MAP_WB);
	if (IS_ERR(vaddr)) {
		ret = PTR_ERR(vaddr);
		DRM_DEBUG_DRIVER("Could not map object pages! (%d)\n", ret);
		return ret;
	}

	if (engine->default_state) {
		/*
		 * We only want to copy over the template context state;
		 * skipping over the headers reserved for GuC communication,
		 * leaving those as zero.
		 */
		const unsigned long start = LRC_HEADER_PAGES * PAGE_SIZE;
		void *defaults;

		defaults = i915_gem_object_pin_map(engine->default_state,
						   I915_MAP_WB);
		if (IS_ERR(defaults)) {
			ret = PTR_ERR(defaults);
			goto err_unpin_ctx;
		}

		memcpy(vaddr + start, defaults + start, engine->context_size);
		i915_gem_object_unpin_map(engine->default_state);
	}

	/* The second page of the context object contains some fields which must
	 * be set up prior to the first execution. */
	regs = vaddr + LRC_STATE_PN * PAGE_SIZE;
	execlists_init_reg_state(regs, ce, engine, ring);
	if (!engine->default_state)
		regs[CTX_CONTEXT_CONTROL + 1] |=
			_MASKED_BIT_ENABLE(CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT);

	ret = 0;
err_unpin_ctx:
	__i915_gem_object_flush_map(ctx_obj,
				    LRC_HEADER_PAGES * PAGE_SIZE,
				    engine->context_size);
	i915_gem_object_unpin_map(ctx_obj);
	return ret;
}

static struct intel_timeline *
get_timeline(struct i915_gem_context *ctx, struct intel_gt *gt)
{
	if (ctx->timeline)
		return intel_timeline_get(ctx->timeline);
	else
		return intel_timeline_create(gt, NULL);
}

static int execlists_context_deferred_alloc(struct intel_context *ce,
					    struct intel_engine_cs *engine)
{
	struct drm_i915_gem_object *ctx_obj;
	struct i915_vma *vma;
	u32 context_size;
	struct intel_ring *ring;
	struct intel_timeline *timeline;
	int ret;

	if (ce->state)
		return 0;

	context_size = round_up(engine->context_size, I915_GTT_PAGE_SIZE);

	/*
	 * Before the actual start of the context image, we insert a few pages
	 * for our own use and for sharing with the GuC.
	 */
	context_size += LRC_HEADER_PAGES * PAGE_SIZE;

	ctx_obj = i915_gem_object_create_shmem(engine->i915, context_size);
	if (IS_ERR(ctx_obj))
		return PTR_ERR(ctx_obj);

	vma = i915_vma_instance(ctx_obj, &engine->gt->ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto error_deref_obj;
	}

	timeline = get_timeline(ce->gem_context, engine->gt);
	if (IS_ERR(timeline)) {
		ret = PTR_ERR(timeline);
		goto error_deref_obj;
	}

	ring = intel_engine_create_ring(engine,
					timeline,
					ce->gem_context->ring_size);
	intel_timeline_put(timeline);
	if (IS_ERR(ring)) {
		ret = PTR_ERR(ring);
		goto error_deref_obj;
	}

	ret = populate_lr_context(ce, ctx_obj, engine, ring);
	if (ret) {
		DRM_DEBUG_DRIVER("Failed to populate LRC: %d\n", ret);
		goto error_ring_free;
	}

	ce->ring = ring;
	ce->state = vma;

	return 0;

error_ring_free:
	intel_ring_put(ring);
error_deref_obj:
	i915_gem_object_put(ctx_obj);
	return ret;
}

static struct list_head *virtual_queue(struct virtual_engine *ve)
{
	return &ve->base.execlists.default_priolist.requests[0];
}

static void virtual_context_destroy(struct kref *kref)
{
	struct virtual_engine *ve =
		container_of(kref, typeof(*ve), context.ref);
	unsigned int n;

	GEM_BUG_ON(!list_empty(virtual_queue(ve)));
	GEM_BUG_ON(ve->request);
	GEM_BUG_ON(ve->context.inflight);

	for (n = 0; n < ve->num_siblings; n++) {
		struct intel_engine_cs *sibling = ve->siblings[n];
		struct rb_node *node = &ve->nodes[sibling->id].rb;

		if (RB_EMPTY_NODE(node))
			continue;

		spin_lock_irq(&sibling->active.lock);

		/* Detachment is lazily performed in the execlists tasklet */
		if (!RB_EMPTY_NODE(node))
			rb_erase_cached(node, &sibling->execlists.virtual);

		spin_unlock_irq(&sibling->active.lock);
	}
	GEM_BUG_ON(__tasklet_is_scheduled(&ve->base.execlists.tasklet));

	if (ve->context.state)
		__execlists_context_fini(&ve->context);

	kfree(ve->bonds);
	kfree(ve);
}

static void virtual_engine_initial_hint(struct virtual_engine *ve)
{
	int swp;

	/*
	 * Pick a random sibling on starting to help spread the load around.
	 *
	 * New contexts are typically created with exactly the same order
	 * of siblings, and often started in batches. Due to the way we iterate
	 * the array of sibling when submitting requests, sibling[0] is
	 * prioritised for dequeuing. If we make sure that sibling[0] is fairly
	 * randomised across the system, we also help spread the load by the
	 * first engine we inspect being different each time.
	 *
	 * NB This does not force us to execute on this engine, it will just
	 * typically be the first we inspect for submission.
	 */
	swp = prandom_u32_max(ve->num_siblings);
	if (!swp)
		return;

	swap(ve->siblings[swp], ve->siblings[0]);
	virtual_update_register_offsets(ve->context.lrc_reg_state,
					ve->siblings[0]);
}

static int virtual_context_pin(struct intel_context *ce)
{
	struct virtual_engine *ve = container_of(ce, typeof(*ve), context);
	int err;

	/* Note: we must use a real engine class for setting up reg state */
	err = __execlists_context_pin(ce, ve->siblings[0]);
	if (err)
		return err;

	virtual_engine_initial_hint(ve);
	return 0;
}

static void virtual_context_enter(struct intel_context *ce)
{
	struct virtual_engine *ve = container_of(ce, typeof(*ve), context);
	unsigned int n;

	for (n = 0; n < ve->num_siblings; n++)
		intel_engine_pm_get(ve->siblings[n]);
}

static void virtual_context_exit(struct intel_context *ce)
{
	struct virtual_engine *ve = container_of(ce, typeof(*ve), context);
	unsigned int n;

	for (n = 0; n < ve->num_siblings; n++)
		intel_engine_pm_put(ve->siblings[n]);
}

static const struct intel_context_ops virtual_context_ops = {
	.pin = virtual_context_pin,
	.unpin = execlists_context_unpin,

	.enter = virtual_context_enter,
	.exit = virtual_context_exit,

	.destroy = virtual_context_destroy,
};

static intel_engine_mask_t virtual_submission_mask(struct virtual_engine *ve)
{
	struct i915_request *rq;
	intel_engine_mask_t mask;

	rq = READ_ONCE(ve->request);
	if (!rq)
		return 0;

	/* The rq is ready for submission; rq->execution_mask is now stable. */
	mask = rq->execution_mask;
	if (unlikely(!mask)) {
		/* Invalid selection, submit to a random engine in error */
		i915_request_skip(rq, -ENODEV);
		mask = ve->siblings[0]->mask;
	}

	GEM_TRACE("%s: rq=%llx:%lld, mask=%x, prio=%d\n",
		  ve->base.name,
		  rq->fence.context, rq->fence.seqno,
		  mask, ve->base.execlists.queue_priority_hint);

	return mask;
}

static void virtual_submission_tasklet(unsigned long data)
{
	struct virtual_engine * const ve = (struct virtual_engine *)data;
	const int prio = ve->base.execlists.queue_priority_hint;
	intel_engine_mask_t mask;
	unsigned int n;

	rcu_read_lock();
	mask = virtual_submission_mask(ve);
	rcu_read_unlock();
	if (unlikely(!mask))
		return;

	local_irq_disable();
	for (n = 0; READ_ONCE(ve->request) && n < ve->num_siblings; n++) {
		struct intel_engine_cs *sibling = ve->siblings[n];
		struct ve_node * const node = &ve->nodes[sibling->id];
		struct rb_node **parent, *rb;
		bool first;

		if (unlikely(!(mask & sibling->mask))) {
			if (!RB_EMPTY_NODE(&node->rb)) {
				spin_lock(&sibling->active.lock);
				rb_erase_cached(&node->rb,
						&sibling->execlists.virtual);
				RB_CLEAR_NODE(&node->rb);
				spin_unlock(&sibling->active.lock);
			}
			continue;
		}

		spin_lock(&sibling->active.lock);

		if (!RB_EMPTY_NODE(&node->rb)) {
			/*
			 * Cheat and avoid rebalancing the tree if we can
			 * reuse this node in situ.
			 */
			first = rb_first_cached(&sibling->execlists.virtual) ==
				&node->rb;
			if (prio == node->prio || (prio > node->prio && first))
				goto submit_engine;

			rb_erase_cached(&node->rb, &sibling->execlists.virtual);
		}

		rb = NULL;
		first = true;
		parent = &sibling->execlists.virtual.rb_root.rb_node;
		while (*parent) {
			struct ve_node *other;

			rb = *parent;
			other = rb_entry(rb, typeof(*other), rb);
			if (prio > other->prio) {
				parent = &rb->rb_left;
			} else {
				parent = &rb->rb_right;
				first = false;
			}
		}

		rb_link_node(&node->rb, rb, parent);
		rb_insert_color_cached(&node->rb,
				       &sibling->execlists.virtual,
				       first);

submit_engine:
		GEM_BUG_ON(RB_EMPTY_NODE(&node->rb));
		node->prio = prio;
		if (first && prio > sibling->execlists.queue_priority_hint) {
			sibling->execlists.queue_priority_hint = prio;
			tasklet_hi_schedule(&sibling->execlists.tasklet);
		}

		spin_unlock(&sibling->active.lock);
	}
	local_irq_enable();
}

static void virtual_submit_request(struct i915_request *rq)
{
	struct virtual_engine *ve = to_virtual_engine(rq->engine);

	GEM_TRACE("%s: rq=%llx:%lld\n",
		  ve->base.name,
		  rq->fence.context,
		  rq->fence.seqno);

	GEM_BUG_ON(ve->base.submit_request != virtual_submit_request);

	GEM_BUG_ON(ve->request);
	GEM_BUG_ON(!list_empty(virtual_queue(ve)));

	ve->base.execlists.queue_priority_hint = rq_prio(rq);
	WRITE_ONCE(ve->request, rq);

	list_move_tail(&rq->sched.link, virtual_queue(ve));

	tasklet_schedule(&ve->base.execlists.tasklet);
}

static struct ve_bond *
virtual_find_bond(struct virtual_engine *ve,
		  const struct intel_engine_cs *master)
{
	int i;

	for (i = 0; i < ve->num_bonds; i++) {
		if (ve->bonds[i].master == master)
			return &ve->bonds[i];
	}

	return NULL;
}

static void
virtual_bond_execute(struct i915_request *rq, struct dma_fence *signal)
{
	struct virtual_engine *ve = to_virtual_engine(rq->engine);
	struct ve_bond *bond;

	bond = virtual_find_bond(ve, to_request(signal)->engine);
	if (bond) {
		intel_engine_mask_t old, new, cmp;

		cmp = READ_ONCE(rq->execution_mask);
		do {
			old = cmp;
			new = cmp & bond->sibling_mask;
		} while ((cmp = cmpxchg(&rq->execution_mask, old, new)) != old);
	}
}

struct intel_context *
intel_execlists_create_virtual(struct i915_gem_context *ctx,
			       struct intel_engine_cs **siblings,
			       unsigned int count)
{
	struct virtual_engine *ve;
	unsigned int n;
	int err;

	if (count == 0)
		return ERR_PTR(-EINVAL);

	if (count == 1)
		return intel_context_create(ctx, siblings[0]);

	ve = kzalloc(struct_size(ve, siblings, count), GFP_KERNEL);
	if (!ve)
		return ERR_PTR(-ENOMEM);

	ve->base.i915 = ctx->i915;
	ve->base.gt = siblings[0]->gt;
	ve->base.id = -1;
	ve->base.class = OTHER_CLASS;
	ve->base.uabi_class = I915_ENGINE_CLASS_INVALID;
	ve->base.instance = I915_ENGINE_CLASS_INVALID_VIRTUAL;
	ve->base.flags = I915_ENGINE_IS_VIRTUAL;

	/*
	 * The decision on whether to submit a request using semaphores
	 * depends on the saturated state of the engine. We only compute
	 * this during HW submission of the request, and we need for this
	 * state to be globally applied to all requests being submitted
	 * to this engine. Virtual engines encompass more than one physical
	 * engine and so we cannot accurately tell in advance if one of those
	 * engines is already saturated and so cannot afford to use a semaphore
	 * and be pessimized in priority for doing so -- if we are the only
	 * context using semaphores after all other clients have stopped, we
	 * will be starved on the saturated system. Such a global switch for
	 * semaphores is less than ideal, but alas is the current compromise.
	 */
	ve->base.saturated = ALL_ENGINES;

	snprintf(ve->base.name, sizeof(ve->base.name), "virtual");

	intel_engine_init_active(&ve->base, ENGINE_VIRTUAL);

	intel_engine_init_execlists(&ve->base);

	ve->base.cops = &virtual_context_ops;
	ve->base.request_alloc = execlists_request_alloc;

	ve->base.schedule = i915_schedule;
	ve->base.submit_request = virtual_submit_request;
	ve->base.bond_execute = virtual_bond_execute;

	INIT_LIST_HEAD(virtual_queue(ve));
	ve->base.execlists.queue_priority_hint = INT_MIN;
	tasklet_init(&ve->base.execlists.tasklet,
		     virtual_submission_tasklet,
		     (unsigned long)ve);

	intel_context_init(&ve->context, ctx, &ve->base);

	for (n = 0; n < count; n++) {
		struct intel_engine_cs *sibling = siblings[n];

		GEM_BUG_ON(!is_power_of_2(sibling->mask));
		if (sibling->mask & ve->base.mask) {
			DRM_DEBUG("duplicate %s entry in load balancer\n",
				  sibling->name);
			err = -EINVAL;
			goto err_put;
		}

		/*
		 * The virtual engine implementation is tightly coupled to
		 * the execlists backend -- we push out request directly
		 * into a tree inside each physical engine. We could support
		 * layering if we handle cloning of the requests and
		 * submitting a copy into each backend.
		 */
		if (sibling->execlists.tasklet.func !=
		    execlists_submission_tasklet) {
			err = -ENODEV;
			goto err_put;
		}

		GEM_BUG_ON(RB_EMPTY_NODE(&ve->nodes[sibling->id].rb));
		RB_CLEAR_NODE(&ve->nodes[sibling->id].rb);

		ve->siblings[ve->num_siblings++] = sibling;
		ve->base.mask |= sibling->mask;

		/*
		 * All physical engines must be compatible for their emission
		 * functions (as we build the instructions during request
		 * construction and do not alter them before submission
		 * on the physical engine). We use the engine class as a guide
		 * here, although that could be refined.
		 */
		if (ve->base.class != OTHER_CLASS) {
			if (ve->base.class != sibling->class) {
				DRM_DEBUG("invalid mixing of engine class, sibling %d, already %d\n",
					  sibling->class, ve->base.class);
				err = -EINVAL;
				goto err_put;
			}
			continue;
		}

		ve->base.class = sibling->class;
		ve->base.uabi_class = sibling->uabi_class;
		snprintf(ve->base.name, sizeof(ve->base.name),
			 "v%dx%d", ve->base.class, count);
		ve->base.context_size = sibling->context_size;

		ve->base.emit_bb_start = sibling->emit_bb_start;
		ve->base.emit_flush = sibling->emit_flush;
		ve->base.emit_init_breadcrumb = sibling->emit_init_breadcrumb;
		ve->base.emit_fini_breadcrumb = sibling->emit_fini_breadcrumb;
		ve->base.emit_fini_breadcrumb_dw =
			sibling->emit_fini_breadcrumb_dw;
	}

	return &ve->context;

err_put:
	intel_context_put(&ve->context);
	return ERR_PTR(err);
}

struct intel_context *
intel_execlists_clone_virtual(struct i915_gem_context *ctx,
			      struct intel_engine_cs *src)
{
	struct virtual_engine *se = to_virtual_engine(src);
	struct intel_context *dst;

	dst = intel_execlists_create_virtual(ctx,
					     se->siblings,
					     se->num_siblings);
	if (IS_ERR(dst))
		return dst;

	if (se->num_bonds) {
		struct virtual_engine *de = to_virtual_engine(dst->engine);

		de->bonds = kmemdup(se->bonds,
				    sizeof(*se->bonds) * se->num_bonds,
				    GFP_KERNEL);
		if (!de->bonds) {
			intel_context_put(dst);
			return ERR_PTR(-ENOMEM);
		}

		de->num_bonds = se->num_bonds;
	}

	return dst;
}

int intel_virtual_engine_attach_bond(struct intel_engine_cs *engine,
				     const struct intel_engine_cs *master,
				     const struct intel_engine_cs *sibling)
{
	struct virtual_engine *ve = to_virtual_engine(engine);
	struct ve_bond *bond;
	int n;

	/* Sanity check the sibling is part of the virtual engine */
	for (n = 0; n < ve->num_siblings; n++)
		if (sibling == ve->siblings[n])
			break;
	if (n == ve->num_siblings)
		return -EINVAL;

	bond = virtual_find_bond(ve, master);
	if (bond) {
		bond->sibling_mask |= sibling->mask;
		return 0;
	}

	bond = krealloc(ve->bonds,
			sizeof(*bond) * (ve->num_bonds + 1),
			GFP_KERNEL);
	if (!bond)
		return -ENOMEM;

	bond[ve->num_bonds].master = master;
	bond[ve->num_bonds].sibling_mask = sibling->mask;

	ve->bonds = bond;
	ve->num_bonds++;

	return 0;
}

void intel_execlists_show_requests(struct intel_engine_cs *engine,
				   struct drm_printer *m,
				   void (*show_request)(struct drm_printer *m,
							struct i915_request *rq,
							const char *prefix),
				   unsigned int max)
{
	const struct intel_engine_execlists *execlists = &engine->execlists;
	struct i915_request *rq, *last;
	unsigned long flags;
	unsigned int count;
	struct rb_node *rb;

	spin_lock_irqsave(&engine->active.lock, flags);

	last = NULL;
	count = 0;
	list_for_each_entry(rq, &engine->active.requests, sched.link) {
		if (count++ < max - 1)
			show_request(m, rq, "\t\tE ");
		else
			last = rq;
	}
	if (last) {
		if (count > max) {
			drm_printf(m,
				   "\t\t...skipping %d executing requests...\n",
				   count - max);
		}
		show_request(m, last, "\t\tE ");
	}

	last = NULL;
	count = 0;
	if (execlists->queue_priority_hint != INT_MIN)
		drm_printf(m, "\t\tQueue priority hint: %d\n",
			   execlists->queue_priority_hint);
	for (rb = rb_first_cached(&execlists->queue); rb; rb = rb_next(rb)) {
		struct i915_priolist *p = rb_entry(rb, typeof(*p), node);
		int i;

		priolist_for_each_request(rq, p, i) {
			if (count++ < max - 1)
				show_request(m, rq, "\t\tQ ");
			else
				last = rq;
		}
	}
	if (last) {
		if (count > max) {
			drm_printf(m,
				   "\t\t...skipping %d queued requests...\n",
				   count - max);
		}
		show_request(m, last, "\t\tQ ");
	}

	last = NULL;
	count = 0;
	for (rb = rb_first_cached(&execlists->virtual); rb; rb = rb_next(rb)) {
		struct virtual_engine *ve =
			rb_entry(rb, typeof(*ve), nodes[engine->id].rb);
		struct i915_request *rq = READ_ONCE(ve->request);

		if (rq) {
			if (count++ < max - 1)
				show_request(m, rq, "\t\tV ");
			else
				last = rq;
		}
	}
	if (last) {
		if (count > max) {
			drm_printf(m,
				   "\t\t...skipping %d virtual requests...\n",
				   count - max);
		}
		show_request(m, last, "\t\tV ");
	}

	spin_unlock_irqrestore(&engine->active.lock, flags);
}

void intel_lr_context_reset(struct intel_engine_cs *engine,
			    struct intel_context *ce,
			    u32 head,
			    bool scrub)
{
	/*
	 * We want a simple context + ring to execute the breadcrumb update.
	 * We cannot rely on the context being intact across the GPU hang,
	 * so clear it and rebuild just what we need for the breadcrumb.
	 * All pending requests for this context will be zapped, and any
	 * future request will be after userspace has had the opportunity
	 * to recreate its own state.
	 */
	if (scrub) {
		u32 *regs = ce->lrc_reg_state;

		if (engine->pinned_default_state) {
			memcpy(regs, /* skip restoring the vanilla PPHWSP */
			       engine->pinned_default_state + LRC_STATE_PN * PAGE_SIZE,
			       engine->context_size - PAGE_SIZE);
		}
		execlists_init_reg_state(regs, ce, engine, ce->ring);
	}

	/* Rerun the request; its payload has been neutered (if guilty). */
	ce->ring->head = head;
	intel_ring_update_space(ce->ring);

	__execlists_update_reg_state(ce, engine);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftest_lrc.c"
#endif
