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

#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "i915_gem_render_state.h"
#include "i915_vgpu.h"
#include "intel_lrc_reg.h"
#include "intel_mocs.h"
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

/* Typical size of the average request (2 pipecontrols and a MI_BB) */
#define EXECLISTS_REQUEST_SIZE 64 /* bytes */
#define WA_TAIL_DWORDS 2
#define WA_TAIL_BYTES (sizeof(u32) * WA_TAIL_DWORDS)

static int execlists_context_deferred_alloc(struct i915_gem_context *ctx,
					    struct intel_engine_cs *engine,
					    struct intel_context *ce);
static void execlists_init_reg_state(u32 *reg_state,
				     struct i915_gem_context *ctx,
				     struct intel_engine_cs *engine,
				     struct intel_ring *ring);

static inline struct i915_priolist *to_priolist(struct rb_node *rb)
{
	return rb_entry(rb, struct i915_priolist, node);
}

static inline int rq_prio(const struct i915_request *rq)
{
	return rq->sched.attr.priority;
}

static inline bool need_preempt(const struct intel_engine_cs *engine,
				const struct i915_request *last,
				int prio)
{
	return (intel_engine_has_preemption(engine) &&
		__execlists_need_preempt(prio, rq_prio(last)) &&
		!i915_request_completed(last));
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
static void
intel_lr_context_descriptor_update(struct i915_gem_context *ctx,
				   struct intel_engine_cs *engine,
				   struct intel_context *ce)
{
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
	if (INTEL_GEN(ctx->i915) >= 11) {
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

	ce->lrc_desc = desc;
}

static struct i915_priolist *
lookup_priolist(struct intel_engine_cs *engine, int prio)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;
	struct i915_priolist *p;
	struct rb_node **parent, *rb;
	bool first = true;

	if (unlikely(execlists->no_priolist))
		prio = I915_PRIORITY_NORMAL;

find_priolist:
	/* most positive priority is scheduled first, equal priorities fifo */
	rb = NULL;
	parent = &execlists->queue.rb_root.rb_node;
	while (*parent) {
		rb = *parent;
		p = to_priolist(rb);
		if (prio > p->priority) {
			parent = &rb->rb_left;
		} else if (prio < p->priority) {
			parent = &rb->rb_right;
			first = false;
		} else {
			return p;
		}
	}

	if (prio == I915_PRIORITY_NORMAL) {
		p = &execlists->default_priolist;
	} else {
		p = kmem_cache_alloc(engine->i915->priorities, GFP_ATOMIC);
		/* Convert an allocation failure to a priority bump */
		if (unlikely(!p)) {
			prio = I915_PRIORITY_NORMAL; /* recurses just once */

			/* To maintain ordering with all rendering, after an
			 * allocation failure we have to disable all scheduling.
			 * Requests will then be executed in fifo, and schedule
			 * will ensure that dependencies are emitted in fifo.
			 * There will be still some reordering with existing
			 * requests, so if userspace lied about their
			 * dependencies that reordering may be visible.
			 */
			execlists->no_priolist = true;
			goto find_priolist;
		}
	}

	p->priority = prio;
	INIT_LIST_HEAD(&p->requests);
	rb_link_node(&p->node, rb, parent);
	rb_insert_color_cached(&p->node, &execlists->queue, first);

	return p;
}

static void unwind_wa_tail(struct i915_request *rq)
{
	rq->tail = intel_ring_wrap(rq->ring, rq->wa_tail - WA_TAIL_BYTES);
	assert_ring_tail_valid(rq->ring, rq->tail);
}

static void __unwind_incomplete_requests(struct intel_engine_cs *engine)
{
	struct i915_request *rq, *rn;
	struct i915_priolist *uninitialized_var(p);
	int last_prio = I915_PRIORITY_INVALID;

	lockdep_assert_held(&engine->timeline.lock);

	list_for_each_entry_safe_reverse(rq, rn,
					 &engine->timeline.requests,
					 link) {
		if (i915_request_completed(rq))
			return;

		__i915_request_unsubmit(rq);
		unwind_wa_tail(rq);

		GEM_BUG_ON(rq_prio(rq) == I915_PRIORITY_INVALID);
		if (rq_prio(rq) != last_prio) {
			last_prio = rq_prio(rq);
			p = lookup_priolist(engine, last_prio);
		}

		GEM_BUG_ON(p->priority != rq_prio(rq));
		list_add(&rq->sched.link, &p->requests);
	}
}

void
execlists_unwind_incomplete_requests(struct intel_engine_execlists *execlists)
{
	struct intel_engine_cs *engine =
		container_of(execlists, typeof(*engine), execlists);
	unsigned long flags;

	spin_lock_irqsave(&engine->timeline.lock, flags);

	__unwind_incomplete_requests(engine);

	spin_unlock_irqrestore(&engine->timeline.lock, flags);
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

inline void
execlists_user_begin(struct intel_engine_execlists *execlists,
		     const struct execlist_port *port)
{
	execlists_set_active_once(execlists, EXECLISTS_ACTIVE_USER);
}

inline void
execlists_user_end(struct intel_engine_execlists *execlists)
{
	execlists_clear_active(execlists, EXECLISTS_ACTIVE_USER);
}

static inline void
execlists_context_schedule_in(struct i915_request *rq)
{
	execlists_context_status_change(rq, INTEL_CONTEXT_SCHEDULE_IN);
	intel_engine_context_in(rq->engine);
}

static inline void
execlists_context_schedule_out(struct i915_request *rq, unsigned long status)
{
	intel_engine_context_out(rq->engine);
	execlists_context_status_change(rq, status);
	trace_i915_request_out(rq);
}

static void
execlists_update_context_pdps(struct i915_hw_ppgtt *ppgtt, u32 *reg_state)
{
	ASSIGN_CTX_PDP(ppgtt, reg_state, 3);
	ASSIGN_CTX_PDP(ppgtt, reg_state, 2);
	ASSIGN_CTX_PDP(ppgtt, reg_state, 1);
	ASSIGN_CTX_PDP(ppgtt, reg_state, 0);
}

static u64 execlists_update_context(struct i915_request *rq)
{
	struct intel_context *ce = rq->hw_context;
	struct i915_hw_ppgtt *ppgtt =
		rq->gem_context->ppgtt ?: rq->i915->mm.aliasing_ppgtt;
	u32 *reg_state = ce->lrc_reg_state;

	reg_state[CTX_RING_TAIL+1] = intel_ring_set_tail(rq->ring, rq->tail);

	/* True 32b PPGTT with dynamic page allocation: update PDP
	 * registers and point the unallocated PDPs to scratch page.
	 * PML4 is allocated during ppgtt init, so this is not needed
	 * in 48-bit mode.
	 */
	if (ppgtt && !i915_vm_is_48bit(&ppgtt->vm))
		execlists_update_context_pdps(ppgtt, reg_state);

	return ce->lrc_desc;
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

static void execlists_submit_ports(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists *execlists = &engine->execlists;
	struct execlist_port *port = execlists->port;
	unsigned int n;

	/*
	 * We can skip acquiring intel_runtime_pm_get() here as it was taken
	 * on our behalf by the request (see i915_gem_mark_busy()) and it will
	 * not be relinquished until the device is idle (see
	 * i915_gem_idle_work_handler()). As a precaution, we make sure
	 * that all ELSP are drained i.e. we have processed the CSB,
	 * before allowing ourselves to idle and calling intel_runtime_pm_put().
	 */
	GEM_BUG_ON(!engine->i915->gt.awake);

	/*
	 * ELSQ note: the submit queue is not cleared after being submitted
	 * to the HW so we need to make sure we always clean it up. This is
	 * currently ensured by the fact that we always write the same number
	 * of elsq entries, keep this in mind before changing the loop below.
	 */
	for (n = execlists_num_ports(execlists); n--; ) {
		struct i915_request *rq;
		unsigned int count;
		u64 desc;

		rq = port_unpack(&port[n], &count);
		if (rq) {
			GEM_BUG_ON(count > !n);
			if (!count++)
				execlists_context_schedule_in(rq);
			port_set(&port[n], port_pack(rq, count));
			desc = execlists_update_context(rq);
			GEM_DEBUG_EXEC(port[n].context_id = upper_32_bits(desc));

			GEM_TRACE("%s in[%d]:  ctx=%d.%d, global=%d (fence %llx:%d) (current %d), prio=%d\n",
				  engine->name, n,
				  port[n].context_id, count,
				  rq->global_seqno,
				  rq->fence.context, rq->fence.seqno,
				  intel_engine_get_seqno(engine),
				  rq_prio(rq));
		} else {
			GEM_BUG_ON(!n);
			desc = 0;
		}

		write_desc(execlists, desc, n);
	}

	/* we need to manually load the submit queue */
	if (execlists->ctrl_reg)
		writel(EL_CTRL_LOAD, execlists->ctrl_reg);

	execlists_clear_active(execlists, EXECLISTS_ACTIVE_HWACK);
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

static void port_assign(struct execlist_port *port, struct i915_request *rq)
{
	GEM_BUG_ON(rq == port_request(port));

	if (port_isset(port))
		i915_request_put(port_request(port));

	port_set(port, port_pack(i915_request_get(rq), port_count(port)));
}

static void inject_preempt_context(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists *execlists = &engine->execlists;
	struct intel_context *ce =
		to_intel_context(engine->i915->preempt_context, engine);
	unsigned int n;

	GEM_BUG_ON(execlists->preempt_complete_status !=
		   upper_32_bits(ce->lrc_desc));

	/*
	 * Switch to our empty preempt context so
	 * the state of the GPU is known (idle).
	 */
	GEM_TRACE("%s\n", engine->name);
	for (n = execlists_num_ports(execlists); --n; )
		write_desc(execlists, 0, n);

	write_desc(execlists, ce->lrc_desc, n);

	/* we need to manually load the submit queue */
	if (execlists->ctrl_reg)
		writel(EL_CTRL_LOAD, execlists->ctrl_reg);

	execlists_clear_active(execlists, EXECLISTS_ACTIVE_HWACK);
	execlists_set_active(execlists, EXECLISTS_ACTIVE_PREEMPT);
}

static void complete_preempt_context(struct intel_engine_execlists *execlists)
{
	GEM_BUG_ON(!execlists_is_active(execlists, EXECLISTS_ACTIVE_PREEMPT));

	if (inject_preempt_hang(execlists))
		return;

	execlists_cancel_port_requests(execlists);
	__unwind_incomplete_requests(container_of(execlists,
						  struct intel_engine_cs,
						  execlists));
}

static void execlists_dequeue(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;
	struct execlist_port *port = execlists->port;
	const struct execlist_port * const last_port =
		&execlists->port[execlists->port_mask];
	struct i915_request *last = port_request(port);
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

	if (last) {
		/*
		 * Don't resubmit or switch until all outstanding
		 * preemptions (lite-restore) are seen. Then we
		 * know the next preemption status we see corresponds
		 * to this ELSP update.
		 */
		GEM_BUG_ON(!execlists_is_active(execlists,
						EXECLISTS_ACTIVE_USER));
		GEM_BUG_ON(!port_count(&port[0]));

		/*
		 * If we write to ELSP a second time before the HW has had
		 * a chance to respond to the previous write, we can confuse
		 * the HW and hit "undefined behaviour". After writing to ELSP,
		 * we must then wait until we see a context-switch event from
		 * the HW to indicate that it has had a chance to respond.
		 */
		if (!execlists_is_active(execlists, EXECLISTS_ACTIVE_HWACK))
			return;

		if (need_preempt(engine, last, execlists->queue_priority)) {
			inject_preempt_context(engine);
			return;
		}

		/*
		 * In theory, we could coalesce more requests onto
		 * the second port (the first port is active, with
		 * no preemptions pending). However, that means we
		 * then have to deal with the possible lite-restore
		 * of the second port (as we submit the ELSP, there
		 * may be a context-switch) but also we may complete
		 * the resubmission before the context-switch. Ergo,
		 * coalescing onto the second port will cause a
		 * preemption event, but we cannot predict whether
		 * that will affect port[0] or port[1].
		 *
		 * If the second port is already active, we can wait
		 * until the next context-switch before contemplating
		 * new requests. The GPU will be busy and we should be
		 * able to resubmit the new ELSP before it idles,
		 * avoiding pipeline bubbles (momentary pauses where
		 * the driver is unable to keep up the supply of new
		 * work). However, we have to double check that the
		 * priorities of the ports haven't been switch.
		 */
		if (port_count(&port[1]))
			return;

		/*
		 * WaIdleLiteRestore:bdw,skl
		 * Apply the wa NOOPs to prevent
		 * ring:HEAD == rq:TAIL as we resubmit the
		 * request. See gen8_emit_breadcrumb() for
		 * where we prepare the padding after the
		 * end of the request.
		 */
		last->tail = last->wa_tail;
	}

	while ((rb = rb_first_cached(&execlists->queue))) {
		struct i915_priolist *p = to_priolist(rb);
		struct i915_request *rq, *rn;

		list_for_each_entry_safe(rq, rn, &p->requests, sched.link) {
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
			if (last &&
			    !can_merge_ctx(rq->hw_context, last->hw_context)) {
				/*
				 * If we are on the second port and cannot
				 * combine this request with the last, then we
				 * are done.
				 */
				if (port == last_port) {
					__list_del_many(&p->requests,
							&rq->sched.link);
					goto done;
				}

				/*
				 * If GVT overrides us we only ever submit
				 * port[0], leaving port[1] empty. Note that we
				 * also have to be careful that we don't queue
				 * the same context (even though a different
				 * request) to the second port.
				 */
				if (ctx_single_port_submission(last->hw_context) ||
				    ctx_single_port_submission(rq->hw_context)) {
					__list_del_many(&p->requests,
							&rq->sched.link);
					goto done;
				}

				GEM_BUG_ON(last->hw_context == rq->hw_context);

				if (submit)
					port_assign(port, last);
				port++;

				GEM_BUG_ON(port_isset(port));
			}

			INIT_LIST_HEAD(&rq->sched.link);
			__i915_request_submit(rq);
			trace_i915_request_in(rq, port_index(port, execlists));
			last = rq;
			submit = true;
		}

		rb_erase_cached(&p->node, &execlists->queue);
		INIT_LIST_HEAD(&p->requests);
		if (p->priority != I915_PRIORITY_NORMAL)
			kmem_cache_free(engine->i915->priorities, p);
	}

done:
	/*
	 * Here be a bit of magic! Or sleight-of-hand, whichever you prefer.
	 *
	 * We choose queue_priority such that if we add a request of greater
	 * priority than this, we kick the submission tasklet to decide on
	 * the right order of submitting the requests to hardware. We must
	 * also be prepared to reorder requests as they are in-flight on the
	 * HW. We derive the queue_priority then as the first "hole" in
	 * the HW submission ports and if there are no available slots,
	 * the priority of the lowest executing request, i.e. last.
	 *
	 * When we do receive a higher priority request ready to run from the
	 * user, see queue_request(), the queue_priority is bumped to that
	 * request triggering preemption on the next dequeue (or subsequent
	 * interrupt for secondary ports).
	 */
	execlists->queue_priority =
		port != execlists->port ? rq_prio(last) : INT_MIN;

	if (submit) {
		port_assign(port, last);
		execlists_submit_ports(engine);
	}

	/* We must always keep the beast fed if we have work piled up */
	GEM_BUG_ON(rb_first_cached(&execlists->queue) &&
		   !port_isset(execlists->port));

	/* Re-evaluate the executing context setup after each preemptive kick */
	if (last)
		execlists_user_begin(execlists, execlists->port);

	/* If the engine is now idle, so should be the flag; and vice versa. */
	GEM_BUG_ON(execlists_is_active(&engine->execlists,
				       EXECLISTS_ACTIVE_USER) ==
		   !port_isset(engine->execlists.port));
}

void
execlists_cancel_port_requests(struct intel_engine_execlists * const execlists)
{
	struct execlist_port *port = execlists->port;
	unsigned int num_ports = execlists_num_ports(execlists);

	while (num_ports-- && port_isset(port)) {
		struct i915_request *rq = port_request(port);

		GEM_TRACE("%s:port%u global=%d (fence %llx:%d), (current %d)\n",
			  rq->engine->name,
			  (unsigned int)(port - execlists->port),
			  rq->global_seqno,
			  rq->fence.context, rq->fence.seqno,
			  intel_engine_get_seqno(rq->engine));

		GEM_BUG_ON(!execlists->active);
		execlists_context_schedule_out(rq,
					       i915_request_completed(rq) ?
					       INTEL_CONTEXT_SCHEDULE_OUT :
					       INTEL_CONTEXT_SCHEDULE_PREEMPTED);

		i915_request_put(rq);

		memset(port, 0, sizeof(*port));
		port++;
	}

	execlists_clear_all_active(execlists);
}

static void reset_csb_pointers(struct intel_engine_execlists *execlists)
{
	/*
	 * After a reset, the HW starts writing into CSB entry [0]. We
	 * therefore have to set our HEAD pointer back one entry so that
	 * the *first* entry we check is entry 0. To complicate this further,
	 * as we don't wait for the first interrupt after reset, we have to
	 * fake the HW write to point back to the last entry so that our
	 * inline comparison of our cached head position against the last HW
	 * write works even before the first interrupt.
	 */
	execlists->csb_head = execlists->csb_write_reset;
	WRITE_ONCE(*execlists->csb_write, execlists->csb_write_reset);
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

	GEM_TRACE("%s current %d\n",
		  engine->name, intel_engine_get_seqno(engine));

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
	spin_lock_irqsave(&engine->timeline.lock, flags);

	/* Cancel the requests on the HW and clear the ELSP tracker. */
	execlists_cancel_port_requests(execlists);
	execlists_user_end(execlists);

	/* Mark all executing requests as skipped. */
	list_for_each_entry(rq, &engine->timeline.requests, link) {
		GEM_BUG_ON(!rq->global_seqno);
		if (!i915_request_completed(rq))
			dma_fence_set_error(&rq->fence, -EIO);
	}

	/* Flush the queued requests to the timeline list (for retiring). */
	while ((rb = rb_first_cached(&execlists->queue))) {
		struct i915_priolist *p = to_priolist(rb);

		list_for_each_entry_safe(rq, rn, &p->requests, sched.link) {
			INIT_LIST_HEAD(&rq->sched.link);

			dma_fence_set_error(&rq->fence, -EIO);
			__i915_request_submit(rq);
		}

		rb_erase_cached(&p->node, &execlists->queue);
		INIT_LIST_HEAD(&p->requests);
		if (p->priority != I915_PRIORITY_NORMAL)
			kmem_cache_free(engine->i915->priorities, p);
	}

	/* Remaining _unready_ requests will be nop'ed when submitted */

	execlists->queue_priority = INT_MIN;
	execlists->queue = RB_ROOT_CACHED;
	GEM_BUG_ON(port_isset(execlists->port));

	GEM_BUG_ON(__tasklet_is_enabled(&execlists->tasklet));
	execlists->tasklet.func = nop_submission_tasklet;

	spin_unlock_irqrestore(&engine->timeline.lock, flags);
}

static inline bool
reset_in_progress(const struct intel_engine_execlists *execlists)
{
	return unlikely(!__tasklet_is_enabled(&execlists->tasklet));
}

static void process_csb(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;
	struct execlist_port *port = execlists->port;
	const u32 * const buf = execlists->csb_status;
	u8 head, tail;

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
		struct i915_request *rq;
		unsigned int status;
		unsigned int count;

		if (++head == GEN8_CSB_ENTRIES)
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

		GEM_TRACE("%s csb[%d]: status=0x%08x:0x%08x, active=0x%x\n",
			  engine->name, head,
			  buf[2 * head + 0], buf[2 * head + 1],
			  execlists->active);

		status = buf[2 * head];
		if (status & (GEN8_CTX_STATUS_IDLE_ACTIVE |
			      GEN8_CTX_STATUS_PREEMPTED))
			execlists_set_active(execlists,
					     EXECLISTS_ACTIVE_HWACK);
		if (status & GEN8_CTX_STATUS_ACTIVE_IDLE)
			execlists_clear_active(execlists,
					       EXECLISTS_ACTIVE_HWACK);

		if (!(status & GEN8_CTX_STATUS_COMPLETED_MASK))
			continue;

		/* We should never get a COMPLETED | IDLE_ACTIVE! */
		GEM_BUG_ON(status & GEN8_CTX_STATUS_IDLE_ACTIVE);

		if (status & GEN8_CTX_STATUS_COMPLETE &&
		    buf[2*head + 1] == execlists->preempt_complete_status) {
			GEM_TRACE("%s preempt-idle\n", engine->name);
			complete_preempt_context(execlists);
			continue;
		}

		if (status & GEN8_CTX_STATUS_PREEMPTED &&
		    execlists_is_active(execlists,
					EXECLISTS_ACTIVE_PREEMPT))
			continue;

		GEM_BUG_ON(!execlists_is_active(execlists,
						EXECLISTS_ACTIVE_USER));

		rq = port_unpack(port, &count);
		GEM_TRACE("%s out[0]: ctx=%d.%d, global=%d (fence %llx:%d) (current %d), prio=%d\n",
			  engine->name,
			  port->context_id, count,
			  rq ? rq->global_seqno : 0,
			  rq ? rq->fence.context : 0,
			  rq ? rq->fence.seqno : 0,
			  intel_engine_get_seqno(engine),
			  rq ? rq_prio(rq) : 0);

		/* Check the context/desc id for this event matches */
		GEM_DEBUG_BUG_ON(buf[2 * head + 1] != port->context_id);

		GEM_BUG_ON(count == 0);
		if (--count == 0) {
			/*
			 * On the final event corresponding to the
			 * submission of this context, we expect either
			 * an element-switch event or a completion
			 * event (and on completion, the active-idle
			 * marker). No more preemptions, lite-restore
			 * or otherwise.
			 */
			GEM_BUG_ON(status & GEN8_CTX_STATUS_PREEMPTED);
			GEM_BUG_ON(port_isset(&port[1]) &&
				   !(status & GEN8_CTX_STATUS_ELEMENT_SWITCH));
			GEM_BUG_ON(!port_isset(&port[1]) &&
				   !(status & GEN8_CTX_STATUS_ACTIVE_IDLE));

			/*
			 * We rely on the hardware being strongly
			 * ordered, that the breadcrumb write is
			 * coherent (visible from the CPU) before the
			 * user interrupt and CSB is processed.
			 */
			GEM_BUG_ON(!i915_request_completed(rq));

			execlists_context_schedule_out(rq,
						       INTEL_CONTEXT_SCHEDULE_OUT);
			i915_request_put(rq);

			GEM_TRACE("%s completed ctx=%d\n",
				  engine->name, port->context_id);

			port = execlists_port_complete(execlists, port);
			if (port_isset(port))
				execlists_user_begin(execlists, port);
			else
				execlists_user_end(execlists);
		} else {
			port_set(port, port_pack(rq, count));
		}
	} while (head != tail);

	execlists->csb_head = head;
}

static void __execlists_submission_tasklet(struct intel_engine_cs *const engine)
{
	lockdep_assert_held(&engine->timeline.lock);

	process_csb(engine);
	if (!execlists_is_active(&engine->execlists, EXECLISTS_ACTIVE_PREEMPT))
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

	GEM_TRACE("%s awake?=%d, active=%x\n",
		  engine->name,
		  engine->i915->gt.awake,
		  engine->execlists.active);

	spin_lock_irqsave(&engine->timeline.lock, flags);
	__execlists_submission_tasklet(engine);
	spin_unlock_irqrestore(&engine->timeline.lock, flags);
}

static void queue_request(struct intel_engine_cs *engine,
			  struct i915_sched_node *node,
			  int prio)
{
	list_add_tail(&node->link,
		      &lookup_priolist(engine, prio)->requests);
}

static void __update_queue(struct intel_engine_cs *engine, int prio)
{
	engine->execlists.queue_priority = prio;
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

static void submit_queue(struct intel_engine_cs *engine, int prio)
{
	if (prio > engine->execlists.queue_priority) {
		__update_queue(engine, prio);
		__submit_queue_imm(engine);
	}
}

static void execlists_submit_request(struct i915_request *request)
{
	struct intel_engine_cs *engine = request->engine;
	unsigned long flags;

	/* Will be called from irq-context when using foreign fences. */
	spin_lock_irqsave(&engine->timeline.lock, flags);

	queue_request(engine, &request->sched, rq_prio(request));

	GEM_BUG_ON(RB_EMPTY_ROOT(&engine->execlists.queue.rb_root));
	GEM_BUG_ON(list_empty(&request->sched.link));

	submit_queue(engine, rq_prio(request));

	spin_unlock_irqrestore(&engine->timeline.lock, flags);
}

static struct i915_request *sched_to_request(struct i915_sched_node *node)
{
	return container_of(node, struct i915_request, sched);
}

static struct intel_engine_cs *
sched_lock_engine(struct i915_sched_node *node, struct intel_engine_cs *locked)
{
	struct intel_engine_cs *engine = sched_to_request(node)->engine;

	GEM_BUG_ON(!locked);

	if (engine != locked) {
		spin_unlock(&locked->timeline.lock);
		spin_lock(&engine->timeline.lock);
	}

	return engine;
}

static void execlists_schedule(struct i915_request *request,
			       const struct i915_sched_attr *attr)
{
	struct i915_priolist *uninitialized_var(pl);
	struct intel_engine_cs *engine, *last;
	struct i915_dependency *dep, *p;
	struct i915_dependency stack;
	const int prio = attr->priority;
	LIST_HEAD(dfs);

	GEM_BUG_ON(prio == I915_PRIORITY_INVALID);

	if (i915_request_completed(request))
		return;

	if (prio <= READ_ONCE(request->sched.attr.priority))
		return;

	/* Need BKL in order to use the temporary link inside i915_dependency */
	lockdep_assert_held(&request->i915->drm.struct_mutex);

	stack.signaler = &request->sched;
	list_add(&stack.dfs_link, &dfs);

	/*
	 * Recursively bump all dependent priorities to match the new request.
	 *
	 * A naive approach would be to use recursion:
	 * static void update_priorities(struct i915_sched_node *node, prio) {
	 *	list_for_each_entry(dep, &node->signalers_list, signal_link)
	 *		update_priorities(dep->signal, prio)
	 *	queue_request(node);
	 * }
	 * but that may have unlimited recursion depth and so runs a very
	 * real risk of overunning the kernel stack. Instead, we build
	 * a flat list of all dependencies starting with the current request.
	 * As we walk the list of dependencies, we add all of its dependencies
	 * to the end of the list (this may include an already visited
	 * request) and continue to walk onwards onto the new dependencies. The
	 * end result is a topological list of requests in reverse order, the
	 * last element in the list is the request we must execute first.
	 */
	list_for_each_entry(dep, &dfs, dfs_link) {
		struct i915_sched_node *node = dep->signaler;

		/*
		 * Within an engine, there can be no cycle, but we may
		 * refer to the same dependency chain multiple times
		 * (redundant dependencies are not eliminated) and across
		 * engines.
		 */
		list_for_each_entry(p, &node->signalers_list, signal_link) {
			GEM_BUG_ON(p == dep); /* no cycles! */

			if (i915_sched_node_signaled(p->signaler))
				continue;

			GEM_BUG_ON(p->signaler->attr.priority < node->attr.priority);
			if (prio > READ_ONCE(p->signaler->attr.priority))
				list_move_tail(&p->dfs_link, &dfs);
		}
	}

	/*
	 * If we didn't need to bump any existing priorities, and we haven't
	 * yet submitted this request (i.e. there is no potential race with
	 * execlists_submit_request()), we can set our own priority and skip
	 * acquiring the engine locks.
	 */
	if (request->sched.attr.priority == I915_PRIORITY_INVALID) {
		GEM_BUG_ON(!list_empty(&request->sched.link));
		request->sched.attr = *attr;
		if (stack.dfs_link.next == stack.dfs_link.prev)
			return;
		__list_del_entry(&stack.dfs_link);
	}

	last = NULL;
	engine = request->engine;
	spin_lock_irq(&engine->timeline.lock);

	/* Fifo and depth-first replacement ensure our deps execute before us */
	list_for_each_entry_safe_reverse(dep, p, &dfs, dfs_link) {
		struct i915_sched_node *node = dep->signaler;

		INIT_LIST_HEAD(&dep->dfs_link);

		engine = sched_lock_engine(node, engine);

		if (prio <= node->attr.priority)
			continue;

		node->attr.priority = prio;
		if (!list_empty(&node->link)) {
			if (last != engine) {
				pl = lookup_priolist(engine, prio);
				last = engine;
			}
			GEM_BUG_ON(pl->priority != prio);
			list_move_tail(&node->link, &pl->requests);
		}

		if (prio > engine->execlists.queue_priority &&
		    i915_sw_fence_done(&sched_to_request(node)->submit)) {
			/* defer submission until after all of our updates */
			__update_queue(engine, prio);
			tasklet_hi_schedule(&engine->execlists.tasklet);
		}
	}

	spin_unlock_irq(&engine->timeline.lock);
}

static void execlists_context_destroy(struct intel_context *ce)
{
	GEM_BUG_ON(ce->pin_count);

	if (!ce->state)
		return;

	intel_ring_free(ce->ring);

	GEM_BUG_ON(i915_gem_object_is_active(ce->state->obj));
	i915_gem_object_put(ce->state->obj);
}

static void execlists_context_unpin(struct intel_context *ce)
{
	i915_gem_context_unpin_hw_id(ce->gem_context);

	intel_ring_unpin(ce->ring);

	ce->state->obj->pin_global--;
	i915_gem_object_unpin_map(ce->state->obj);
	i915_vma_unpin(ce->state);

	i915_gem_context_put(ce->gem_context);
}

static int __context_pin(struct i915_gem_context *ctx, struct i915_vma *vma)
{
	unsigned int flags;
	int err;

	/*
	 * Clear this page out of any CPU caches for coherent swap-in/out.
	 * We only want to do this on the first bind so that we do not stall
	 * on an active context (which by nature is already on the GPU).
	 */
	if (!(vma->flags & I915_VMA_GLOBAL_BIND)) {
		err = i915_gem_object_set_to_wc_domain(vma->obj, true);
		if (err)
			return err;
	}

	flags = PIN_GLOBAL | PIN_HIGH;
	flags |= PIN_OFFSET_BIAS | i915_ggtt_pin_bias(vma);

	return i915_vma_pin(vma, 0, 0, flags);
}

static struct intel_context *
__execlists_context_pin(struct intel_engine_cs *engine,
			struct i915_gem_context *ctx,
			struct intel_context *ce)
{
	void *vaddr;
	int ret;

	ret = execlists_context_deferred_alloc(ctx, engine, ce);
	if (ret)
		goto err;
	GEM_BUG_ON(!ce->state);

	ret = __context_pin(ctx, ce->state);
	if (ret)
		goto err;

	vaddr = i915_gem_object_pin_map(ce->state->obj,
					i915_coherent_map_type(ctx->i915) |
					I915_MAP_OVERRIDE);
	if (IS_ERR(vaddr)) {
		ret = PTR_ERR(vaddr);
		goto unpin_vma;
	}

	ret = intel_ring_pin(ce->ring);
	if (ret)
		goto unpin_map;

	ret = i915_gem_context_pin_hw_id(ctx);
	if (ret)
		goto unpin_ring;

	intel_lr_context_descriptor_update(ctx, engine, ce);

	GEM_BUG_ON(!intel_ring_offset_valid(ce->ring, ce->ring->head));

	ce->lrc_reg_state = vaddr + LRC_STATE_PN * PAGE_SIZE;
	ce->lrc_reg_state[CTX_RING_BUFFER_START+1] =
		i915_ggtt_offset(ce->ring->vma);
	ce->lrc_reg_state[CTX_RING_HEAD + 1] = ce->ring->head;
	ce->lrc_reg_state[CTX_RING_TAIL + 1] = ce->ring->tail;

	ce->state->obj->pin_global++;
	i915_gem_context_get(ctx);
	return ce;

unpin_ring:
	intel_ring_unpin(ce->ring);
unpin_map:
	i915_gem_object_unpin_map(ce->state->obj);
unpin_vma:
	__i915_vma_unpin(ce->state);
err:
	ce->pin_count = 0;
	return ERR_PTR(ret);
}

static const struct intel_context_ops execlists_context_ops = {
	.unpin = execlists_context_unpin,
	.destroy = execlists_context_destroy,
};

static struct intel_context *
execlists_context_pin(struct intel_engine_cs *engine,
		      struct i915_gem_context *ctx)
{
	struct intel_context *ce = to_intel_context(ctx, engine);

	lockdep_assert_held(&ctx->i915->drm.struct_mutex);

	if (likely(ce->pin_count++))
		return ce;
	GEM_BUG_ON(!ce->pin_count); /* no overflow please! */

	ce->ops = &execlists_context_ops;

	return __execlists_context_pin(engine, ctx, ce);
}

static int execlists_request_alloc(struct i915_request *request)
{
	int ret;

	GEM_BUG_ON(!request->hw_context->pin_count);

	/* Flush enough space to reduce the likelihood of waiting after
	 * we start building the request - in which case we will just
	 * have to repeat work.
	 */
	request->reserved_space += EXECLISTS_REQUEST_SIZE;

	ret = intel_ring_wait_for_space(request->ring, request->reserved_space);
	if (ret)
		return ret;

	/* Note that after this point, we have committed to using
	 * this request as it is being used to both track the
	 * state of engine initialisation and liveness of the
	 * golden renderstate above. Think twice before you try
	 * to cancel/unwind this request now.
	 */

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
	*batch++ = MI_STORE_REGISTER_MEM_GEN8 | MI_SRM_LRM_GLOBAL_GTT;
	*batch++ = i915_mmio_reg_offset(GEN8_L3SQCREG4);
	*batch++ = i915_ggtt_offset(engine->scratch) + 256;
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
	*batch++ = i915_ggtt_offset(engine->scratch) + 256;
	*batch++ = 0;

	return batch;
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
				       i915_ggtt_offset(engine->scratch) +
				       2 * CACHELINE_BYTES);

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

	/* WaClearSlmSpaceAtContextSwitch:kbl */
	/* Actual scratch location is at 128 bytes offset */
	if (IS_KBL_REVID(engine->i915, 0, KBL_REVID_A0)) {
		batch = gen8_emit_pipe_control(batch,
					       PIPE_CONTROL_FLUSH_L3 |
					       PIPE_CONTROL_GLOBAL_GTT_IVB |
					       PIPE_CONTROL_CS_STALL |
					       PIPE_CONTROL_QW_WRITE,
					       i915_ggtt_offset(engine->scratch)
					       + 2 * CACHELINE_BYTES);
	}

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

	obj = i915_gem_object_create(engine->i915, CTX_WA_BB_OBJ_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	vma = i915_vma_instance(obj, &engine->i915->ggtt.vm, NULL);
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

	if (GEM_WARN_ON(engine->id != RCS))
		return -EINVAL;

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
		if (GEM_WARN_ON(!IS_ALIGNED(wa_bb[i]->offset,
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
	struct drm_i915_private *dev_priv = engine->i915;

	I915_WRITE(RING_HWSTAM(engine->mmio_base), 0xffffffff);

	/*
	 * Make sure we're not enabling the new 12-deep CSB
	 * FIFO as that requires a slightly updated handling
	 * in the ctx switch irq. Since we're currently only
	 * using only 2 elements of the enhanced execlists the
	 * deeper FIFO it's not needed and it's not worth adding
	 * more statements to the irq handler to support it.
	 */
	if (INTEL_GEN(dev_priv) >= 11)
		I915_WRITE(RING_MODE_GEN7(engine),
			   _MASKED_BIT_DISABLE(GEN11_GFX_DISABLE_LEGACY_MODE));
	else
		I915_WRITE(RING_MODE_GEN7(engine),
			   _MASKED_BIT_ENABLE(GFX_RUN_LIST_ENABLE));

	I915_WRITE(RING_MI_MODE(engine->mmio_base),
		   _MASKED_BIT_DISABLE(STOP_RING));

	I915_WRITE(RING_HWS_PGA(engine->mmio_base),
		   engine->status_page.ggtt_offset);
	POSTING_READ(RING_HWS_PGA(engine->mmio_base));
}

static bool unexpected_starting_state(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;
	bool unexpected = false;

	if (I915_READ(RING_MI_MODE(engine->mmio_base)) & STOP_RING) {
		DRM_DEBUG_DRIVER("STOP_RING still set in RING_MI_MODE\n");
		unexpected = true;
	}

	return unexpected;
}

static int gen8_init_common_ring(struct intel_engine_cs *engine)
{
	intel_mocs_init_engine(engine);

	intel_engine_reset_breadcrumbs(engine);

	if (GEM_SHOW_DEBUG() && unexpected_starting_state(engine)) {
		struct drm_printer p = drm_debug_printer(__func__);

		intel_engine_dump(engine, &p, NULL);
	}

	enable_execlists(engine);

	return 0;
}

static int gen8_init_render_ring(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;
	int ret;

	ret = gen8_init_common_ring(engine);
	if (ret)
		return ret;

	intel_whitelist_workarounds_apply(engine);

	/* We need to disable the AsyncFlip performance optimisations in order
	 * to use MI_WAIT_FOR_EVENT within the CS. It should already be
	 * programmed to '1' on all products.
	 *
	 * WaDisableAsyncFlipPerfMode:snb,ivb,hsw,vlv,bdw,chv
	 */
	I915_WRITE(MI_MODE, _MASKED_BIT_ENABLE(ASYNC_FLIP_PERF_DISABLE));

	I915_WRITE(INSTPM, _MASKED_BIT_ENABLE(INSTPM_FORCE_ORDERING));

	return 0;
}

static int gen9_init_render_ring(struct intel_engine_cs *engine)
{
	int ret;

	ret = gen8_init_common_ring(engine);
	if (ret)
		return ret;

	intel_whitelist_workarounds_apply(engine);

	return 0;
}

static struct i915_request *
execlists_reset_prepare(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;
	struct i915_request *request, *active;
	unsigned long flags;

	GEM_TRACE("%s: depth<-%d\n", engine->name,
		  atomic_read(&execlists->tasklet.count));

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

	spin_lock_irqsave(&engine->timeline.lock, flags);

	/*
	 * We want to flush the pending context switches, having disabled
	 * the tasklet above, we can assume exclusive access to the execlists.
	 * For this allows us to catch up with an inflight preemption event,
	 * and avoid blaming an innocent request if the stall was due to the
	 * preemption itself.
	 */
	process_csb(engine);

	/*
	 * The last active request can then be no later than the last request
	 * now in ELSP[0]. So search backwards from there, so that if the GPU
	 * has advanced beyond the last CSB update, it will be pardoned.
	 */
	active = NULL;
	request = port_request(execlists->port);
	if (request) {
		/*
		 * Prevent the breadcrumb from advancing before we decide
		 * which request is currently active.
		 */
		intel_engine_stop_cs(engine);

		list_for_each_entry_from_reverse(request,
						 &engine->timeline.requests,
						 link) {
			if (__i915_request_completed(request,
						     request->global_seqno))
				break;

			active = request;
		}
	}

	spin_unlock_irqrestore(&engine->timeline.lock, flags);

	return active;
}

static void execlists_reset(struct intel_engine_cs *engine,
			    struct i915_request *request)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;
	unsigned long flags;
	u32 *regs;

	GEM_TRACE("%s request global=%x, current=%d\n",
		  engine->name, request ? request->global_seqno : 0,
		  intel_engine_get_seqno(engine));

	spin_lock_irqsave(&engine->timeline.lock, flags);

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

	/* Push back any incomplete requests for replay after the reset. */
	__unwind_incomplete_requests(engine);

	/* Following the reset, we need to reload the CSB read/write pointers */
	reset_csb_pointers(&engine->execlists);

	spin_unlock_irqrestore(&engine->timeline.lock, flags);

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
	if (!request || request->fence.error != -EIO)
		return;

	/*
	 * We want a simple context + ring to execute the breadcrumb update.
	 * We cannot rely on the context being intact across the GPU hang,
	 * so clear it and rebuild just what we need for the breadcrumb.
	 * All pending requests for this context will be zapped, and any
	 * future request will be after userspace has had the opportunity
	 * to recreate its own state.
	 */
	regs = request->hw_context->lrc_reg_state;
	if (engine->pinned_default_state) {
		memcpy(regs, /* skip restoring the vanilla PPHWSP */
		       engine->pinned_default_state + LRC_STATE_PN * PAGE_SIZE,
		       engine->context_size - PAGE_SIZE);
	}
	execlists_init_reg_state(regs,
				 request->gem_context, engine, request->ring);

	/* Move the RING_HEAD onto the breadcrumb, past the hanging batch */
	regs[CTX_RING_BUFFER_START + 1] = i915_ggtt_offset(request->ring->vma);

	request->ring->head = intel_ring_wrap(request->ring, request->postfix);
	regs[CTX_RING_HEAD + 1] = request->ring->head;

	intel_ring_update_space(request->ring);

	/* Reset WaIdleLiteRestore:bdw,skl as well */
	unwind_wa_tail(request);
}

static void execlists_reset_finish(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;

	/*
	 * After a GPU reset, we may have requests to replay. Do so now while
	 * we still have the forcewake to be sure that the GPU is not allowed
	 * to sleep before we restart and reload a context.
	 *
	 */
	if (!RB_EMPTY_ROOT(&execlists->queue.rb_root))
		execlists->tasklet.func(execlists->tasklet.data);

	tasklet_enable(&execlists->tasklet);
	GEM_TRACE("%s: depth->%d\n", engine->name,
		  atomic_read(&execlists->tasklet.count));
}

static int intel_logical_ring_emit_pdps(struct i915_request *rq)
{
	struct i915_hw_ppgtt *ppgtt = rq->gem_context->ppgtt;
	struct intel_engine_cs *engine = rq->engine;
	const int num_lri_cmds = GEN8_3LVL_PDPES * 2;
	u32 *cs;
	int i;

	cs = intel_ring_begin(rq, num_lri_cmds * 2 + 2);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_LOAD_REGISTER_IMM(num_lri_cmds);
	for (i = GEN8_3LVL_PDPES - 1; i >= 0; i--) {
		const dma_addr_t pd_daddr = i915_page_dir_dma_addr(ppgtt, i);

		*cs++ = i915_mmio_reg_offset(GEN8_RING_PDP_UDW(engine, i));
		*cs++ = upper_32_bits(pd_daddr);
		*cs++ = i915_mmio_reg_offset(GEN8_RING_PDP_LDW(engine, i));
		*cs++ = lower_32_bits(pd_daddr);
	}

	*cs++ = MI_NOOP;
	intel_ring_advance(rq, cs);

	return 0;
}

static int gen8_emit_bb_start(struct i915_request *rq,
			      u64 offset, u32 len,
			      const unsigned int flags)
{
	u32 *cs;
	int ret;

	/* Don't rely in hw updating PDPs, specially in lite-restore.
	 * Ideally, we should set Force PD Restore in ctx descriptor,
	 * but we can't. Force Restore would be a second option, but
	 * it is unsafe in case of lite-restore (because the ctx is
	 * not idle). PML4 is allocated during ppgtt init so this is
	 * not needed in 48-bit.*/
	if (rq->gem_context->ppgtt &&
	    (intel_engine_flag(rq->engine) & rq->gem_context->ppgtt->pd_dirty_rings) &&
	    !i915_vm_is_48bit(&rq->gem_context->ppgtt->vm) &&
	    !intel_vgpu_active(rq->i915)) {
		ret = intel_logical_ring_emit_pdps(rq);
		if (ret)
			return ret;

		rq->gem_context->ppgtt->pd_dirty_rings &= ~intel_engine_flag(rq->engine);
	}

	cs = intel_ring_begin(rq, 6);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	/*
	 * WaDisableCtxRestoreArbitration:bdw,chv
	 *
	 * We don't need to perform MI_ARB_ENABLE as often as we do (in
	 * particular all the gen that do not need the w/a at all!), if we
	 * took care to make sure that on every switch into this context
	 * (both ordinary and for preemption) that arbitrartion was enabled
	 * we would be fine. However, there doesn't seem to be a downside to
	 * being paranoid and making sure it is set before each batch and
	 * every context-switch.
	 *
	 * Note that if we fail to enable arbitration before the request
	 * is complete, then we do not see the context-switch interrupt and
	 * the engine hangs (with RING_HEAD == RING_TAIL).
	 *
	 * That satisfies both the GPGPU w/a and our heavy-handed paranoia.
	 */
	*cs++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;

	/* FIXME(BDW): Address space and security selectors. */
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
	struct drm_i915_private *dev_priv = engine->i915;
	I915_WRITE_IMR(engine,
		       ~(engine->irq_enable_mask | engine->irq_keep_mask));
	POSTING_READ_FW(RING_IMR(engine->mmio_base));
}

static void gen8_logical_ring_disable_irq(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;
	I915_WRITE_IMR(engine, ~engine->irq_keep_mask);
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
		if (request->engine->id == VCS)
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
		i915_ggtt_offset(engine->scratch) + 2 * CACHELINE_BYTES;
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
		if (IS_GEN9(request->i915))
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
static void gen8_emit_wa_tail(struct i915_request *request, u32 *cs)
{
	/* Ensure there's always at least one preemption point per-request. */
	*cs++ = MI_ARB_CHECK;
	*cs++ = MI_NOOP;
	request->wa_tail = intel_ring_offset(request, cs);
}

static void gen8_emit_breadcrumb(struct i915_request *request, u32 *cs)
{
	/* w/a: bit 5 needs to be zero for MI_FLUSH_DW address. */
	BUILD_BUG_ON(I915_GEM_HWS_INDEX_ADDR & (1 << 5));

	cs = gen8_emit_ggtt_write(cs, request->global_seqno,
				  intel_hws_seqno_address(request->engine));
	*cs++ = MI_USER_INTERRUPT;
	*cs++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;
	request->tail = intel_ring_offset(request, cs);
	assert_ring_tail_valid(request->ring, request->tail);

	gen8_emit_wa_tail(request, cs);
}
static const int gen8_emit_breadcrumb_sz = 6 + WA_TAIL_DWORDS;

static void gen8_emit_breadcrumb_rcs(struct i915_request *request, u32 *cs)
{
	/* We're using qword write, seqno should be aligned to 8 bytes. */
	BUILD_BUG_ON(I915_GEM_HWS_INDEX & 1);

	cs = gen8_emit_ggtt_write_rcs(cs, request->global_seqno,
				      intel_hws_seqno_address(request->engine));
	*cs++ = MI_USER_INTERRUPT;
	*cs++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;
	request->tail = intel_ring_offset(request, cs);
	assert_ring_tail_valid(request->ring, request->tail);

	gen8_emit_wa_tail(request, cs);
}
static const int gen8_emit_breadcrumb_rcs_sz = 8 + WA_TAIL_DWORDS;

static int gen8_init_rcs_context(struct i915_request *rq)
{
	int ret;

	ret = intel_ctx_workarounds_emit(rq);
	if (ret)
		return ret;

	ret = intel_rcs_context_init_mocs(rq);
	/*
	 * Failing to program the MOCS is non-fatal.The system will not
	 * run at peak performance. So generate an error and carry on.
	 */
	if (ret)
		DRM_ERROR("MOCS failed to program: expect performance issues.\n");

	return i915_gem_render_state_emit(rq);
}

/**
 * intel_logical_ring_cleanup() - deallocate the Engine Command Streamer
 * @engine: Engine Command Streamer.
 */
void intel_logical_ring_cleanup(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv;

	/*
	 * Tasklet cannot be active at this point due intel_mark_active/idle
	 * so this is just for documentation.
	 */
	if (WARN_ON(test_bit(TASKLET_STATE_SCHED,
			     &engine->execlists.tasklet.state)))
		tasklet_kill(&engine->execlists.tasklet);

	dev_priv = engine->i915;

	if (engine->buffer) {
		WARN_ON((I915_READ_MODE(engine) & MODE_IDLE) == 0);
	}

	if (engine->cleanup)
		engine->cleanup(engine);

	intel_engine_cleanup_common(engine);

	lrc_destroy_wa_ctx(engine);

	engine->i915 = NULL;
	dev_priv->engine[engine->id] = NULL;
	kfree(engine);
}

void intel_execlists_set_default_submission(struct intel_engine_cs *engine)
{
	engine->submit_request = execlists_submit_request;
	engine->cancel_requests = execlists_cancel_requests;
	engine->schedule = execlists_schedule;
	engine->execlists.tasklet.func = execlists_submission_tasklet;

	engine->reset.prepare = execlists_reset_prepare;

	engine->park = NULL;
	engine->unpark = NULL;

	engine->flags |= I915_ENGINE_SUPPORTS_STATS;
	if (engine->i915->preempt_context)
		engine->flags |= I915_ENGINE_HAS_PREEMPTION;

	engine->i915->caps.scheduler =
		I915_SCHEDULER_CAP_ENABLED |
		I915_SCHEDULER_CAP_PRIORITY;
	if (intel_engine_has_preemption(engine))
		engine->i915->caps.scheduler |= I915_SCHEDULER_CAP_PREEMPTION;
}

static void
logical_ring_default_vfuncs(struct intel_engine_cs *engine)
{
	/* Default vfuncs which can be overriden by each engine. */
	engine->init_hw = gen8_init_common_ring;

	engine->reset.prepare = execlists_reset_prepare;
	engine->reset.reset = execlists_reset;
	engine->reset.finish = execlists_reset_finish;

	engine->context_pin = execlists_context_pin;
	engine->request_alloc = execlists_request_alloc;

	engine->emit_flush = gen8_emit_flush;
	engine->emit_breadcrumb = gen8_emit_breadcrumb;
	engine->emit_breadcrumb_sz = gen8_emit_breadcrumb_sz;

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
	engine->emit_bb_start = gen8_emit_bb_start;
}

static inline void
logical_ring_default_irqs(struct intel_engine_cs *engine)
{
	unsigned int shift = 0;

	if (INTEL_GEN(engine->i915) < 11) {
		const u8 irq_shifts[] = {
			[RCS]  = GEN8_RCS_IRQ_SHIFT,
			[BCS]  = GEN8_BCS_IRQ_SHIFT,
			[VCS]  = GEN8_VCS1_IRQ_SHIFT,
			[VCS2] = GEN8_VCS2_IRQ_SHIFT,
			[VECS] = GEN8_VECS_IRQ_SHIFT,
		};

		shift = irq_shifts[engine->id];
	}

	engine->irq_enable_mask = GT_RENDER_USER_INTERRUPT << shift;
	engine->irq_keep_mask = GT_CONTEXT_SWITCH_INTERRUPT << shift;
}

static void
logical_ring_setup(struct intel_engine_cs *engine)
{
	intel_engine_setup_common(engine);

	/* Intentionally left blank. */
	engine->buffer = NULL;

	tasklet_init(&engine->execlists.tasklet,
		     execlists_submission_tasklet, (unsigned long)engine);

	logical_ring_default_vfuncs(engine);
	logical_ring_default_irqs(engine);
}

static bool csb_force_mmio(struct drm_i915_private *i915)
{
	/* Older GVT emulation depends upon intercepting CSB mmio */
	return intel_vgpu_active(i915) && !intel_vgpu_has_hwsp_emulation(i915);
}

static int logical_ring_init(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;
	struct intel_engine_execlists * const execlists = &engine->execlists;
	int ret;

	ret = intel_engine_init_common(engine);
	if (ret)
		return ret;

	if (HAS_LOGICAL_RING_ELSQ(i915)) {
		execlists->submit_reg = i915->regs +
			i915_mmio_reg_offset(RING_EXECLIST_SQ_CONTENTS(engine));
		execlists->ctrl_reg = i915->regs +
			i915_mmio_reg_offset(RING_EXECLIST_CONTROL(engine));
	} else {
		execlists->submit_reg = i915->regs +
			i915_mmio_reg_offset(RING_ELSP(engine));
	}

	execlists->preempt_complete_status = ~0u;
	if (i915->preempt_context) {
		struct intel_context *ce =
			to_intel_context(i915->preempt_context, engine);

		execlists->preempt_complete_status =
			upper_32_bits(ce->lrc_desc);
	}

	execlists->csb_read =
		i915->regs + i915_mmio_reg_offset(RING_CONTEXT_STATUS_PTR(engine));
	if (csb_force_mmio(i915)) {
		execlists->csb_status = (u32 __force *)
			(i915->regs + i915_mmio_reg_offset(RING_CONTEXT_STATUS_BUF_LO(engine, 0)));

		execlists->csb_write = (u32 __force *)execlists->csb_read;
		execlists->csb_write_reset =
			_MASKED_FIELD(GEN8_CSB_WRITE_PTR_MASK,
				      GEN8_CSB_ENTRIES - 1);
	} else {
		execlists->csb_status =
			&engine->status_page.page_addr[I915_HWS_CSB_BUF0_INDEX];

		execlists->csb_write =
			&engine->status_page.page_addr[intel_hws_csb_write_index(i915)];
		execlists->csb_write_reset = GEN8_CSB_ENTRIES - 1;
	}
	reset_csb_pointers(execlists);

	return 0;
}

int logical_render_ring_init(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;
	int ret;

	logical_ring_setup(engine);

	if (HAS_L3_DPF(dev_priv))
		engine->irq_keep_mask |= GT_RENDER_L3_PARITY_ERROR_INTERRUPT;

	/* Override some for render ring. */
	if (INTEL_GEN(dev_priv) >= 9)
		engine->init_hw = gen9_init_render_ring;
	else
		engine->init_hw = gen8_init_render_ring;
	engine->init_context = gen8_init_rcs_context;
	engine->emit_flush = gen8_emit_flush_render;
	engine->emit_breadcrumb = gen8_emit_breadcrumb_rcs;
	engine->emit_breadcrumb_sz = gen8_emit_breadcrumb_rcs_sz;

	ret = logical_ring_init(engine);
	if (ret)
		return ret;

	ret = intel_engine_create_scratch(engine, PAGE_SIZE);
	if (ret)
		goto err_cleanup_common;

	ret = intel_init_workaround_bb(engine);
	if (ret) {
		/*
		 * We continue even if we fail to initialize WA batch
		 * because we only expect rare glitches but nothing
		 * critical to prevent us from using GPU
		 */
		DRM_ERROR("WA batch buffer initialization failed: %d\n",
			  ret);
	}

	return 0;

err_cleanup_common:
	intel_engine_cleanup_common(engine);
	return ret;
}

int logical_xcs_ring_init(struct intel_engine_cs *engine)
{
	logical_ring_setup(engine);

	return logical_ring_init(engine);
}

static u32
make_rpcs(struct drm_i915_private *dev_priv)
{
	bool subslice_pg = INTEL_INFO(dev_priv)->sseu.has_subslice_pg;
	u8 slices = hweight8(INTEL_INFO(dev_priv)->sseu.slice_mask);
	u8 subslices = hweight8(INTEL_INFO(dev_priv)->sseu.subslice_mask[0]);
	u32 rpcs = 0;

	/*
	 * No explicit RPCS request is needed to ensure full
	 * slice/subslice/EU enablement prior to Gen9.
	*/
	if (INTEL_GEN(dev_priv) < 9)
		return 0;

	/*
	 * Since the SScount bitfield in GEN8_R_PWR_CLK_STATE is only three bits
	 * wide and Icelake has up to eight subslices, specfial programming is
	 * needed in order to correctly enable all subslices.
	 *
	 * According to documentation software must consider the configuration
	 * as 2x4x8 and hardware will translate this to 1x8x8.
	 *
	 * Furthemore, even though SScount is three bits, maximum documented
	 * value for it is four. From this some rules/restrictions follow:
	 *
	 * 1.
	 * If enabled subslice count is greater than four, two whole slices must
	 * be enabled instead.
	 *
	 * 2.
	 * When more than one slice is enabled, hardware ignores the subslice
	 * count altogether.
	 *
	 * From these restrictions it follows that it is not possible to enable
	 * a count of subslices between the SScount maximum of four restriction,
	 * and the maximum available number on a particular SKU. Either all
	 * subslices are enabled, or a count between one and four on the first
	 * slice.
	 */
	if (IS_GEN11(dev_priv) && slices == 1 && subslices >= 4) {
		GEM_BUG_ON(subslices & 1);

		subslice_pg = false;
		slices *= 2;
	}

	/*
	 * Starting in Gen9, render power gating can leave
	 * slice/subslice/EU in a partially enabled state. We
	 * must make an explicit request through RPCS for full
	 * enablement.
	*/
	if (INTEL_INFO(dev_priv)->sseu.has_slice_pg) {
		u32 mask, val = slices;

		if (INTEL_GEN(dev_priv) >= 11) {
			mask = GEN11_RPCS_S_CNT_MASK;
			val <<= GEN11_RPCS_S_CNT_SHIFT;
		} else {
			mask = GEN8_RPCS_S_CNT_MASK;
			val <<= GEN8_RPCS_S_CNT_SHIFT;
		}

		GEM_BUG_ON(val & ~mask);
		val &= mask;

		rpcs |= GEN8_RPCS_ENABLE | GEN8_RPCS_S_CNT_ENABLE | val;
	}

	if (subslice_pg) {
		u32 val = subslices;

		val <<= GEN8_RPCS_SS_CNT_SHIFT;

		GEM_BUG_ON(val & ~GEN8_RPCS_SS_CNT_MASK);
		val &= GEN8_RPCS_SS_CNT_MASK;

		rpcs |= GEN8_RPCS_ENABLE | GEN8_RPCS_SS_CNT_ENABLE | val;
	}

	if (INTEL_INFO(dev_priv)->sseu.has_eu_pg) {
		u32 val;

		val = INTEL_INFO(dev_priv)->sseu.eu_per_subslice <<
		      GEN8_RPCS_EU_MIN_SHIFT;
		GEM_BUG_ON(val & ~GEN8_RPCS_EU_MIN_MASK);
		val &= GEN8_RPCS_EU_MIN_MASK;

		rpcs |= val;

		val = INTEL_INFO(dev_priv)->sseu.eu_per_subslice <<
		      GEN8_RPCS_EU_MAX_SHIFT;
		GEM_BUG_ON(val & ~GEN8_RPCS_EU_MAX_MASK);
		val &= GEN8_RPCS_EU_MAX_MASK;

		rpcs |= val;

		rpcs |= GEN8_RPCS_ENABLE;
	}

	return rpcs;
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
				     struct i915_gem_context *ctx,
				     struct intel_engine_cs *engine,
				     struct intel_ring *ring)
{
	struct drm_i915_private *dev_priv = engine->i915;
	struct i915_hw_ppgtt *ppgtt = ctx->ppgtt ?: dev_priv->mm.aliasing_ppgtt;
	u32 base = engine->mmio_base;
	bool rcs = engine->class == RENDER_CLASS;

	/* A context is actually a big batch buffer with several
	 * MI_LOAD_REGISTER_IMM commands followed by (reg, value) pairs. The
	 * values we are setting here are only for the first context restore:
	 * on a subsequent save, the GPU will recreate this batchbuffer with new
	 * values (including all the missing MI_LOAD_REGISTER_IMM commands that
	 * we are not initializing here).
	 */
	regs[CTX_LRI_HEADER_0] = MI_LOAD_REGISTER_IMM(rcs ? 14 : 11) |
				 MI_LRI_FORCE_POSTED;

	CTX_REG(regs, CTX_CONTEXT_CONTROL, RING_CONTEXT_CONTROL(engine),
		_MASKED_BIT_DISABLE(CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT) |
		_MASKED_BIT_ENABLE(CTX_CTRL_INHIBIT_SYN_CTX_SWITCH));
	if (INTEL_GEN(dev_priv) < 11) {
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
	CTX_REG(regs, CTX_PDP3_UDW, GEN8_RING_PDP_UDW(engine, 3), 0);
	CTX_REG(regs, CTX_PDP3_LDW, GEN8_RING_PDP_LDW(engine, 3), 0);
	CTX_REG(regs, CTX_PDP2_UDW, GEN8_RING_PDP_UDW(engine, 2), 0);
	CTX_REG(regs, CTX_PDP2_LDW, GEN8_RING_PDP_LDW(engine, 2), 0);
	CTX_REG(regs, CTX_PDP1_UDW, GEN8_RING_PDP_UDW(engine, 1), 0);
	CTX_REG(regs, CTX_PDP1_LDW, GEN8_RING_PDP_LDW(engine, 1), 0);
	CTX_REG(regs, CTX_PDP0_UDW, GEN8_RING_PDP_UDW(engine, 0), 0);
	CTX_REG(regs, CTX_PDP0_LDW, GEN8_RING_PDP_LDW(engine, 0), 0);

	if (ppgtt && i915_vm_is_48bit(&ppgtt->vm)) {
		/* 64b PPGTT (48bit canonical)
		 * PDP0_DESCRIPTOR contains the base address to PML4 and
		 * other PDP Descriptors are ignored.
		 */
		ASSIGN_CTX_PML4(ppgtt, regs);
	}

	if (rcs) {
		regs[CTX_LRI_HEADER_2] = MI_LOAD_REGISTER_IMM(1);
		CTX_REG(regs, CTX_R_PWR_CLK_STATE, GEN8_R_PWR_CLK_STATE,
			make_rpcs(dev_priv));

		i915_oa_init_reg_state(engine, ctx, regs);
	}

	regs[CTX_END] = MI_BATCH_BUFFER_END;
	if (INTEL_GEN(dev_priv) >= 10)
		regs[CTX_END] |= BIT(0);
}

static int
populate_lr_context(struct i915_gem_context *ctx,
		    struct drm_i915_gem_object *ctx_obj,
		    struct intel_engine_cs *engine,
		    struct intel_ring *ring)
{
	void *vaddr;
	u32 *regs;
	int ret;

	ret = i915_gem_object_set_to_cpu_domain(ctx_obj, true);
	if (ret) {
		DRM_DEBUG_DRIVER("Could not set to CPU domain\n");
		return ret;
	}

	vaddr = i915_gem_object_pin_map(ctx_obj, I915_MAP_WB);
	if (IS_ERR(vaddr)) {
		ret = PTR_ERR(vaddr);
		DRM_DEBUG_DRIVER("Could not map object pages! (%d)\n", ret);
		return ret;
	}
	ctx_obj->mm.dirty = true;

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
	execlists_init_reg_state(regs, ctx, engine, ring);
	if (!engine->default_state)
		regs[CTX_CONTEXT_CONTROL + 1] |=
			_MASKED_BIT_ENABLE(CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT);
	if (ctx == ctx->i915->preempt_context && INTEL_GEN(engine->i915) < 11)
		regs[CTX_CONTEXT_CONTROL + 1] |=
			_MASKED_BIT_ENABLE(CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT |
					   CTX_CTRL_ENGINE_CTX_SAVE_INHIBIT);

err_unpin_ctx:
	i915_gem_object_unpin_map(ctx_obj);
	return ret;
}

static int execlists_context_deferred_alloc(struct i915_gem_context *ctx,
					    struct intel_engine_cs *engine,
					    struct intel_context *ce)
{
	struct drm_i915_gem_object *ctx_obj;
	struct i915_vma *vma;
	uint32_t context_size;
	struct intel_ring *ring;
	struct i915_timeline *timeline;
	int ret;

	if (ce->state)
		return 0;

	context_size = round_up(engine->context_size, I915_GTT_PAGE_SIZE);

	/*
	 * Before the actual start of the context image, we insert a few pages
	 * for our own use and for sharing with the GuC.
	 */
	context_size += LRC_HEADER_PAGES * PAGE_SIZE;

	ctx_obj = i915_gem_object_create(ctx->i915, context_size);
	if (IS_ERR(ctx_obj))
		return PTR_ERR(ctx_obj);

	vma = i915_vma_instance(ctx_obj, &ctx->i915->ggtt.vm, NULL);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto error_deref_obj;
	}

	timeline = i915_timeline_create(ctx->i915, ctx->name);
	if (IS_ERR(timeline)) {
		ret = PTR_ERR(timeline);
		goto error_deref_obj;
	}

	ring = intel_engine_create_ring(engine, timeline, ctx->ring_size);
	i915_timeline_put(timeline);
	if (IS_ERR(ring)) {
		ret = PTR_ERR(ring);
		goto error_deref_obj;
	}

	ret = populate_lr_context(ctx, ctx_obj, engine, ring);
	if (ret) {
		DRM_DEBUG_DRIVER("Failed to populate LRC: %d\n", ret);
		goto error_ring_free;
	}

	ce->ring = ring;
	ce->state = vma;

	return 0;

error_ring_free:
	intel_ring_free(ring);
error_deref_obj:
	i915_gem_object_put(ctx_obj);
	return ret;
}

void intel_lr_context_resume(struct drm_i915_private *i915)
{
	struct intel_engine_cs *engine;
	struct i915_gem_context *ctx;
	enum intel_engine_id id;

	/*
	 * Because we emit WA_TAIL_DWORDS there may be a disparity
	 * between our bookkeeping in ce->ring->head and ce->ring->tail and
	 * that stored in context. As we only write new commands from
	 * ce->ring->tail onwards, everything before that is junk. If the GPU
	 * starts reading from its RING_HEAD from the context, it may try to
	 * execute that junk and die.
	 *
	 * So to avoid that we reset the context images upon resume. For
	 * simplicity, we just zero everything out.
	 */
	list_for_each_entry(ctx, &i915->contexts.list, link) {
		for_each_engine(engine, i915, id) {
			struct intel_context *ce =
				to_intel_context(ctx, engine);

			if (!ce->state)
				continue;

			intel_ring_reset(ce->ring, 0);

			if (ce->pin_count) { /* otherwise done in context_pin */
				u32 *regs = ce->lrc_reg_state;

				regs[CTX_RING_HEAD + 1] = ce->ring->head;
				regs[CTX_RING_TAIL + 1] = ce->ring->tail;
			}
		}
	}
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/intel_lrc.c"
#endif
