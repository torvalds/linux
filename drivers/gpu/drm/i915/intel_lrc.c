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

#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"

#define GEN8_LR_CONTEXT_RENDER_SIZE (20 * PAGE_SIZE)
#define GEN8_LR_CONTEXT_OTHER_SIZE (2 * PAGE_SIZE)

#define GEN8_LR_CONTEXT_ALIGN 4096

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

#define CTX_LRI_HEADER_0		0x01
#define CTX_CONTEXT_CONTROL		0x02
#define CTX_RING_HEAD			0x04
#define CTX_RING_TAIL			0x06
#define CTX_RING_BUFFER_START		0x08
#define CTX_RING_BUFFER_CONTROL		0x0a
#define CTX_BB_HEAD_U			0x0c
#define CTX_BB_HEAD_L			0x0e
#define CTX_BB_STATE			0x10
#define CTX_SECOND_BB_HEAD_U		0x12
#define CTX_SECOND_BB_HEAD_L		0x14
#define CTX_SECOND_BB_STATE		0x16
#define CTX_BB_PER_CTX_PTR		0x18
#define CTX_RCS_INDIRECT_CTX		0x1a
#define CTX_RCS_INDIRECT_CTX_OFFSET	0x1c
#define CTX_LRI_HEADER_1		0x21
#define CTX_CTX_TIMESTAMP		0x22
#define CTX_PDP3_UDW			0x24
#define CTX_PDP3_LDW			0x26
#define CTX_PDP2_UDW			0x28
#define CTX_PDP2_LDW			0x2a
#define CTX_PDP1_UDW			0x2c
#define CTX_PDP1_LDW			0x2e
#define CTX_PDP0_UDW			0x30
#define CTX_PDP0_LDW			0x32
#define CTX_LRI_HEADER_2		0x41
#define CTX_R_PWR_CLK_STATE		0x42
#define CTX_GPGPU_CSR_BASE_ADDRESS	0x44

#define GEN8_CTX_VALID (1<<0)
#define GEN8_CTX_FORCE_PD_RESTORE (1<<1)
#define GEN8_CTX_FORCE_RESTORE (1<<2)
#define GEN8_CTX_L3LLC_COHERENT (1<<5)
#define GEN8_CTX_PRIVILEGE (1<<8)
enum {
	ADVANCED_CONTEXT = 0,
	LEGACY_CONTEXT,
	ADVANCED_AD_CONTEXT,
	LEGACY_64B_CONTEXT
};
#define GEN8_CTX_MODE_SHIFT 3
enum {
	FAULT_AND_HANG = 0,
	FAULT_AND_HALT, /* Debug only */
	FAULT_AND_STREAM,
	FAULT_AND_CONTINUE /* Unsupported */
};
#define GEN8_CTX_ID_SHIFT 32

/**
 * intel_sanitize_enable_execlists() - sanitize i915.enable_execlists
 * @dev: DRM device.
 * @enable_execlists: value of i915.enable_execlists module parameter.
 *
 * Only certain platforms support Execlists (the prerequisites being
 * support for Logical Ring Contexts and Aliasing PPGTT or better),
 * and only when enabled via module parameter.
 *
 * Return: 1 if Execlists is supported and has to be enabled.
 */
int intel_sanitize_enable_execlists(struct drm_device *dev, int enable_execlists)
{
	WARN_ON(i915.enable_ppgtt == -1);

	if (enable_execlists == 0)
		return 0;

	if (HAS_LOGICAL_RING_CONTEXTS(dev) && USES_PPGTT(dev) &&
	    i915.use_mmio_flip >= 0)
		return 1;

	return 0;
}

/**
 * intel_execlists_ctx_id() - get the Execlists Context ID
 * @ctx_obj: Logical Ring Context backing object.
 *
 * Do not confuse with ctx->id! Unfortunately we have a name overload
 * here: the old context ID we pass to userspace as a handler so that
 * they can refer to a context, and the new context ID we pass to the
 * ELSP so that the GPU can inform us of the context status via
 * interrupts.
 *
 * Return: 20-bits globally unique context ID.
 */
u32 intel_execlists_ctx_id(struct drm_i915_gem_object *ctx_obj)
{
	u32 lrca = i915_gem_obj_ggtt_offset(ctx_obj);

	/* LRCA is required to be 4K aligned so the more significant 20 bits
	 * are globally unique */
	return lrca >> 12;
}

static uint64_t execlists_ctx_descriptor(struct drm_i915_gem_object *ctx_obj)
{
	uint64_t desc;
	uint64_t lrca = i915_gem_obj_ggtt_offset(ctx_obj);

	WARN_ON(lrca & 0xFFFFFFFF00000FFFULL);

	desc = GEN8_CTX_VALID;
	desc |= LEGACY_CONTEXT << GEN8_CTX_MODE_SHIFT;
	desc |= GEN8_CTX_L3LLC_COHERENT;
	desc |= GEN8_CTX_PRIVILEGE;
	desc |= lrca;
	desc |= (u64)intel_execlists_ctx_id(ctx_obj) << GEN8_CTX_ID_SHIFT;

	/* TODO: WaDisableLiteRestore when we start using semaphore
	 * signalling between Command Streamers */
	/* desc |= GEN8_CTX_FORCE_RESTORE; */

	return desc;
}

static void execlists_elsp_write(struct intel_engine_cs *ring,
				 struct drm_i915_gem_object *ctx_obj0,
				 struct drm_i915_gem_object *ctx_obj1)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	uint64_t temp = 0;
	uint32_t desc[4];
	unsigned long flags;

	/* XXX: You must always write both descriptors in the order below. */
	if (ctx_obj1)
		temp = execlists_ctx_descriptor(ctx_obj1);
	else
		temp = 0;
	desc[1] = (u32)(temp >> 32);
	desc[0] = (u32)temp;

	temp = execlists_ctx_descriptor(ctx_obj0);
	desc[3] = (u32)(temp >> 32);
	desc[2] = (u32)temp;

	/* Set Force Wakeup bit to prevent GT from entering C6 while ELSP writes
	 * are in progress.
	 *
	 * The other problem is that we can't just call gen6_gt_force_wake_get()
	 * because that function calls intel_runtime_pm_get(), which might sleep.
	 * Instead, we do the runtime_pm_get/put when creating/destroying requests.
	 */
	spin_lock_irqsave(&dev_priv->uncore.lock, flags);
	if (IS_CHERRYVIEW(dev_priv->dev)) {
		if (dev_priv->uncore.fw_rendercount++ == 0)
			dev_priv->uncore.funcs.force_wake_get(dev_priv,
							      FORCEWAKE_RENDER);
		if (dev_priv->uncore.fw_mediacount++ == 0)
			dev_priv->uncore.funcs.force_wake_get(dev_priv,
							      FORCEWAKE_MEDIA);
	} else {
		if (dev_priv->uncore.forcewake_count++ == 0)
			dev_priv->uncore.funcs.force_wake_get(dev_priv,
							      FORCEWAKE_ALL);
	}
	spin_unlock_irqrestore(&dev_priv->uncore.lock, flags);

	I915_WRITE(RING_ELSP(ring), desc[1]);
	I915_WRITE(RING_ELSP(ring), desc[0]);
	I915_WRITE(RING_ELSP(ring), desc[3]);
	/* The context is automatically loaded after the following */
	I915_WRITE(RING_ELSP(ring), desc[2]);

	/* ELSP is a wo register, so use another nearby reg for posting instead */
	POSTING_READ(RING_EXECLIST_STATUS(ring));

	/* Release Force Wakeup (see the big comment above). */
	spin_lock_irqsave(&dev_priv->uncore.lock, flags);
	if (IS_CHERRYVIEW(dev_priv->dev)) {
		if (--dev_priv->uncore.fw_rendercount == 0)
			dev_priv->uncore.funcs.force_wake_put(dev_priv,
							      FORCEWAKE_RENDER);
		if (--dev_priv->uncore.fw_mediacount == 0)
			dev_priv->uncore.funcs.force_wake_put(dev_priv,
							      FORCEWAKE_MEDIA);
	} else {
		if (--dev_priv->uncore.forcewake_count == 0)
			dev_priv->uncore.funcs.force_wake_put(dev_priv,
							      FORCEWAKE_ALL);
	}

	spin_unlock_irqrestore(&dev_priv->uncore.lock, flags);
}

static int execlists_ctx_write_tail(struct drm_i915_gem_object *ctx_obj, u32 tail)
{
	struct page *page;
	uint32_t *reg_state;

	page = i915_gem_object_get_page(ctx_obj, 1);
	reg_state = kmap_atomic(page);

	reg_state[CTX_RING_TAIL+1] = tail;

	kunmap_atomic(reg_state);

	return 0;
}

static int execlists_submit_context(struct intel_engine_cs *ring,
				    struct intel_context *to0, u32 tail0,
				    struct intel_context *to1, u32 tail1)
{
	struct drm_i915_gem_object *ctx_obj0;
	struct drm_i915_gem_object *ctx_obj1 = NULL;

	ctx_obj0 = to0->engine[ring->id].state;
	BUG_ON(!ctx_obj0);
	WARN_ON(!i915_gem_obj_is_pinned(ctx_obj0));

	execlists_ctx_write_tail(ctx_obj0, tail0);

	if (to1) {
		ctx_obj1 = to1->engine[ring->id].state;
		BUG_ON(!ctx_obj1);
		WARN_ON(!i915_gem_obj_is_pinned(ctx_obj1));

		execlists_ctx_write_tail(ctx_obj1, tail1);
	}

	execlists_elsp_write(ring, ctx_obj0, ctx_obj1);

	return 0;
}

static void execlists_context_unqueue(struct intel_engine_cs *ring)
{
	struct intel_ctx_submit_request *req0 = NULL, *req1 = NULL;
	struct intel_ctx_submit_request *cursor = NULL, *tmp = NULL;
	struct drm_i915_private *dev_priv = ring->dev->dev_private;

	assert_spin_locked(&ring->execlist_lock);

	if (list_empty(&ring->execlist_queue))
		return;

	/* Try to read in pairs */
	list_for_each_entry_safe(cursor, tmp, &ring->execlist_queue,
				 execlist_link) {
		if (!req0) {
			req0 = cursor;
		} else if (req0->ctx == cursor->ctx) {
			/* Same ctx: ignore first request, as second request
			 * will update tail past first request's workload */
			cursor->elsp_submitted = req0->elsp_submitted;
			list_del(&req0->execlist_link);
			queue_work(dev_priv->wq, &req0->work);
			req0 = cursor;
		} else {
			req1 = cursor;
			break;
		}
	}

	WARN_ON(req1 && req1->elsp_submitted);

	WARN_ON(execlists_submit_context(ring, req0->ctx, req0->tail,
					 req1 ? req1->ctx : NULL,
					 req1 ? req1->tail : 0));

	req0->elsp_submitted++;
	if (req1)
		req1->elsp_submitted++;
}

static bool execlists_check_remove_request(struct intel_engine_cs *ring,
					   u32 request_id)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	struct intel_ctx_submit_request *head_req;

	assert_spin_locked(&ring->execlist_lock);

	head_req = list_first_entry_or_null(&ring->execlist_queue,
					    struct intel_ctx_submit_request,
					    execlist_link);

	if (head_req != NULL) {
		struct drm_i915_gem_object *ctx_obj =
				head_req->ctx->engine[ring->id].state;
		if (intel_execlists_ctx_id(ctx_obj) == request_id) {
			WARN(head_req->elsp_submitted == 0,
			     "Never submitted head request\n");

			if (--head_req->elsp_submitted <= 0) {
				list_del(&head_req->execlist_link);
				queue_work(dev_priv->wq, &head_req->work);
				return true;
			}
		}
	}

	return false;
}

/**
 * intel_execlists_handle_ctx_events() - handle Context Switch interrupts
 * @ring: Engine Command Streamer to handle.
 *
 * Check the unread Context Status Buffers and manage the submission of new
 * contexts to the ELSP accordingly.
 */
void intel_execlists_handle_ctx_events(struct intel_engine_cs *ring)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	u32 status_pointer;
	u8 read_pointer;
	u8 write_pointer;
	u32 status;
	u32 status_id;
	u32 submit_contexts = 0;

	status_pointer = I915_READ(RING_CONTEXT_STATUS_PTR(ring));

	read_pointer = ring->next_context_status_buffer;
	write_pointer = status_pointer & 0x07;
	if (read_pointer > write_pointer)
		write_pointer += 6;

	spin_lock(&ring->execlist_lock);

	while (read_pointer < write_pointer) {
		read_pointer++;
		status = I915_READ(RING_CONTEXT_STATUS_BUF(ring) +
				(read_pointer % 6) * 8);
		status_id = I915_READ(RING_CONTEXT_STATUS_BUF(ring) +
				(read_pointer % 6) * 8 + 4);

		if (status & GEN8_CTX_STATUS_PREEMPTED) {
			if (status & GEN8_CTX_STATUS_LITE_RESTORE) {
				if (execlists_check_remove_request(ring, status_id))
					WARN(1, "Lite Restored request removed from queue\n");
			} else
				WARN(1, "Preemption without Lite Restore\n");
		}

		 if ((status & GEN8_CTX_STATUS_ACTIVE_IDLE) ||
		     (status & GEN8_CTX_STATUS_ELEMENT_SWITCH)) {
			if (execlists_check_remove_request(ring, status_id))
				submit_contexts++;
		}
	}

	if (submit_contexts != 0)
		execlists_context_unqueue(ring);

	spin_unlock(&ring->execlist_lock);

	WARN(submit_contexts > 2, "More than two context complete events?\n");
	ring->next_context_status_buffer = write_pointer % 6;

	I915_WRITE(RING_CONTEXT_STATUS_PTR(ring),
		   ((u32)ring->next_context_status_buffer & 0x07) << 8);
}

static void execlists_free_request_task(struct work_struct *work)
{
	struct intel_ctx_submit_request *req =
		container_of(work, struct intel_ctx_submit_request, work);
	struct drm_device *dev = req->ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	intel_runtime_pm_put(dev_priv);

	mutex_lock(&dev->struct_mutex);
	i915_gem_context_unreference(req->ctx);
	mutex_unlock(&dev->struct_mutex);

	kfree(req);
}

static int execlists_context_queue(struct intel_engine_cs *ring,
				   struct intel_context *to,
				   u32 tail)
{
	struct intel_ctx_submit_request *req = NULL, *cursor;
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	unsigned long flags;
	int num_elements = 0;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (req == NULL)
		return -ENOMEM;
	req->ctx = to;
	i915_gem_context_reference(req->ctx);
	req->ring = ring;
	req->tail = tail;
	INIT_WORK(&req->work, execlists_free_request_task);

	intel_runtime_pm_get(dev_priv);

	spin_lock_irqsave(&ring->execlist_lock, flags);

	list_for_each_entry(cursor, &ring->execlist_queue, execlist_link)
		if (++num_elements > 2)
			break;

	if (num_elements > 2) {
		struct intel_ctx_submit_request *tail_req;

		tail_req = list_last_entry(&ring->execlist_queue,
					   struct intel_ctx_submit_request,
					   execlist_link);

		if (to == tail_req->ctx) {
			WARN(tail_req->elsp_submitted != 0,
			     "More than 2 already-submitted reqs queued\n");
			list_del(&tail_req->execlist_link);
			queue_work(dev_priv->wq, &tail_req->work);
		}
	}

	list_add_tail(&req->execlist_link, &ring->execlist_queue);
	if (num_elements == 0)
		execlists_context_unqueue(ring);

	spin_unlock_irqrestore(&ring->execlist_lock, flags);

	return 0;
}

static int logical_ring_invalidate_all_caches(struct intel_ringbuffer *ringbuf)
{
	struct intel_engine_cs *ring = ringbuf->ring;
	uint32_t flush_domains;
	int ret;

	flush_domains = 0;
	if (ring->gpu_caches_dirty)
		flush_domains = I915_GEM_GPU_DOMAINS;

	ret = ring->emit_flush(ringbuf, I915_GEM_GPU_DOMAINS, flush_domains);
	if (ret)
		return ret;

	ring->gpu_caches_dirty = false;
	return 0;
}

static int execlists_move_to_gpu(struct intel_ringbuffer *ringbuf,
				 struct list_head *vmas)
{
	struct intel_engine_cs *ring = ringbuf->ring;
	struct i915_vma *vma;
	uint32_t flush_domains = 0;
	bool flush_chipset = false;
	int ret;

	list_for_each_entry(vma, vmas, exec_list) {
		struct drm_i915_gem_object *obj = vma->obj;

		ret = i915_gem_object_sync(obj, ring);
		if (ret)
			return ret;

		if (obj->base.write_domain & I915_GEM_DOMAIN_CPU)
			flush_chipset |= i915_gem_clflush_object(obj, false);

		flush_domains |= obj->base.write_domain;
	}

	if (flush_domains & I915_GEM_DOMAIN_GTT)
		wmb();

	/* Unconditionally invalidate gpu caches and ensure that we do flush
	 * any residual writes from the previous batch.
	 */
	return logical_ring_invalidate_all_caches(ringbuf);
}

/**
 * execlists_submission() - submit a batchbuffer for execution, Execlists style
 * @dev: DRM device.
 * @file: DRM file.
 * @ring: Engine Command Streamer to submit to.
 * @ctx: Context to employ for this submission.
 * @args: execbuffer call arguments.
 * @vmas: list of vmas.
 * @batch_obj: the batchbuffer to submit.
 * @exec_start: batchbuffer start virtual address pointer.
 * @flags: translated execbuffer call flags.
 *
 * This is the evil twin version of i915_gem_ringbuffer_submission. It abstracts
 * away the submission details of the execbuffer ioctl call.
 *
 * Return: non-zero if the submission fails.
 */
int intel_execlists_submission(struct drm_device *dev, struct drm_file *file,
			       struct intel_engine_cs *ring,
			       struct intel_context *ctx,
			       struct drm_i915_gem_execbuffer2 *args,
			       struct list_head *vmas,
			       struct drm_i915_gem_object *batch_obj,
			       u64 exec_start, u32 flags)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_ringbuffer *ringbuf = ctx->engine[ring->id].ringbuf;
	int instp_mode;
	u32 instp_mask;
	int ret;

	instp_mode = args->flags & I915_EXEC_CONSTANTS_MASK;
	instp_mask = I915_EXEC_CONSTANTS_MASK;
	switch (instp_mode) {
	case I915_EXEC_CONSTANTS_REL_GENERAL:
	case I915_EXEC_CONSTANTS_ABSOLUTE:
	case I915_EXEC_CONSTANTS_REL_SURFACE:
		if (instp_mode != 0 && ring != &dev_priv->ring[RCS]) {
			DRM_DEBUG("non-0 rel constants mode on non-RCS\n");
			return -EINVAL;
		}

		if (instp_mode != dev_priv->relative_constants_mode) {
			if (instp_mode == I915_EXEC_CONSTANTS_REL_SURFACE) {
				DRM_DEBUG("rel surface constants mode invalid on gen5+\n");
				return -EINVAL;
			}

			/* The HW changed the meaning on this bit on gen6 */
			instp_mask &= ~I915_EXEC_CONSTANTS_REL_SURFACE;
		}
		break;
	default:
		DRM_DEBUG("execbuf with unknown constants: %d\n", instp_mode);
		return -EINVAL;
	}

	if (args->num_cliprects != 0) {
		DRM_DEBUG("clip rectangles are only valid on pre-gen5\n");
		return -EINVAL;
	} else {
		if (args->DR4 == 0xffffffff) {
			DRM_DEBUG("UXA submitting garbage DR4, fixing up\n");
			args->DR4 = 0;
		}

		if (args->DR1 || args->DR4 || args->cliprects_ptr) {
			DRM_DEBUG("0 cliprects but dirt in cliprects fields\n");
			return -EINVAL;
		}
	}

	if (args->flags & I915_EXEC_GEN7_SOL_RESET) {
		DRM_DEBUG("sol reset is gen7 only\n");
		return -EINVAL;
	}

	ret = execlists_move_to_gpu(ringbuf, vmas);
	if (ret)
		return ret;

	if (ring == &dev_priv->ring[RCS] &&
	    instp_mode != dev_priv->relative_constants_mode) {
		ret = intel_logical_ring_begin(ringbuf, 4);
		if (ret)
			return ret;

		intel_logical_ring_emit(ringbuf, MI_NOOP);
		intel_logical_ring_emit(ringbuf, MI_LOAD_REGISTER_IMM(1));
		intel_logical_ring_emit(ringbuf, INSTPM);
		intel_logical_ring_emit(ringbuf, instp_mask << 16 | instp_mode);
		intel_logical_ring_advance(ringbuf);

		dev_priv->relative_constants_mode = instp_mode;
	}

	ret = ring->emit_bb_start(ringbuf, exec_start, flags);
	if (ret)
		return ret;

	i915_gem_execbuffer_move_to_active(vmas, ring);
	i915_gem_execbuffer_retire_commands(dev, file, ring, batch_obj);

	return 0;
}

void intel_logical_ring_stop(struct intel_engine_cs *ring)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	int ret;

	if (!intel_ring_initialized(ring))
		return;

	ret = intel_ring_idle(ring);
	if (ret && !i915_reset_in_progress(&to_i915(ring->dev)->gpu_error))
		DRM_ERROR("failed to quiesce %s whilst cleaning up: %d\n",
			  ring->name, ret);

	/* TODO: Is this correct with Execlists enabled? */
	I915_WRITE_MODE(ring, _MASKED_BIT_ENABLE(STOP_RING));
	if (wait_for_atomic((I915_READ_MODE(ring) & MODE_IDLE) != 0, 1000)) {
		DRM_ERROR("%s :timed out trying to stop ring\n", ring->name);
		return;
	}
	I915_WRITE_MODE(ring, _MASKED_BIT_DISABLE(STOP_RING));
}

int logical_ring_flush_all_caches(struct intel_ringbuffer *ringbuf)
{
	struct intel_engine_cs *ring = ringbuf->ring;
	int ret;

	if (!ring->gpu_caches_dirty)
		return 0;

	ret = ring->emit_flush(ringbuf, 0, I915_GEM_GPU_DOMAINS);
	if (ret)
		return ret;

	ring->gpu_caches_dirty = false;
	return 0;
}

/**
 * intel_logical_ring_advance_and_submit() - advance the tail and submit the workload
 * @ringbuf: Logical Ringbuffer to advance.
 *
 * The tail is updated in our logical ringbuffer struct, not in the actual context. What
 * really happens during submission is that the context and current tail will be placed
 * on a queue waiting for the ELSP to be ready to accept a new context submission. At that
 * point, the tail *inside* the context is updated and the ELSP written to.
 */
void intel_logical_ring_advance_and_submit(struct intel_ringbuffer *ringbuf)
{
	struct intel_engine_cs *ring = ringbuf->ring;
	struct intel_context *ctx = ringbuf->FIXME_lrc_ctx;

	intel_logical_ring_advance(ringbuf);

	if (intel_ring_stopped(ring))
		return;

	execlists_context_queue(ring, ctx, ringbuf->tail);
}

static int logical_ring_alloc_seqno(struct intel_engine_cs *ring,
				    struct intel_context *ctx)
{
	if (ring->outstanding_lazy_seqno)
		return 0;

	if (ring->preallocated_lazy_request == NULL) {
		struct drm_i915_gem_request *request;

		request = kmalloc(sizeof(*request), GFP_KERNEL);
		if (request == NULL)
			return -ENOMEM;

		/* Hold a reference to the context this request belongs to
		 * (we will need it when the time comes to emit/retire the
		 * request).
		 */
		request->ctx = ctx;
		i915_gem_context_reference(request->ctx);

		ring->preallocated_lazy_request = request;
	}

	return i915_gem_get_seqno(ring->dev, &ring->outstanding_lazy_seqno);
}

static int logical_ring_wait_request(struct intel_ringbuffer *ringbuf,
				     int bytes)
{
	struct intel_engine_cs *ring = ringbuf->ring;
	struct drm_i915_gem_request *request;
	u32 seqno = 0;
	int ret;

	if (ringbuf->last_retired_head != -1) {
		ringbuf->head = ringbuf->last_retired_head;
		ringbuf->last_retired_head = -1;

		ringbuf->space = intel_ring_space(ringbuf);
		if (ringbuf->space >= bytes)
			return 0;
	}

	list_for_each_entry(request, &ring->request_list, list) {
		if (__intel_ring_space(request->tail, ringbuf->tail,
				       ringbuf->size) >= bytes) {
			seqno = request->seqno;
			break;
		}
	}

	if (seqno == 0)
		return -ENOSPC;

	ret = i915_wait_seqno(ring, seqno);
	if (ret)
		return ret;

	i915_gem_retire_requests_ring(ring);
	ringbuf->head = ringbuf->last_retired_head;
	ringbuf->last_retired_head = -1;

	ringbuf->space = intel_ring_space(ringbuf);
	return 0;
}

static int logical_ring_wait_for_space(struct intel_ringbuffer *ringbuf,
				       int bytes)
{
	struct intel_engine_cs *ring = ringbuf->ring;
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long end;
	int ret;

	ret = logical_ring_wait_request(ringbuf, bytes);
	if (ret != -ENOSPC)
		return ret;

	/* Force the context submission in case we have been skipping it */
	intel_logical_ring_advance_and_submit(ringbuf);

	/* With GEM the hangcheck timer should kick us out of the loop,
	 * leaving it early runs the risk of corrupting GEM state (due
	 * to running on almost untested codepaths). But on resume
	 * timers don't work yet, so prevent a complete hang in that
	 * case by choosing an insanely large timeout. */
	end = jiffies + 60 * HZ;

	do {
		ringbuf->head = I915_READ_HEAD(ring);
		ringbuf->space = intel_ring_space(ringbuf);
		if (ringbuf->space >= bytes) {
			ret = 0;
			break;
		}

		msleep(1);

		if (dev_priv->mm.interruptible && signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		ret = i915_gem_check_wedge(&dev_priv->gpu_error,
					   dev_priv->mm.interruptible);
		if (ret)
			break;

		if (time_after(jiffies, end)) {
			ret = -EBUSY;
			break;
		}
	} while (1);

	return ret;
}

static int logical_ring_wrap_buffer(struct intel_ringbuffer *ringbuf)
{
	uint32_t __iomem *virt;
	int rem = ringbuf->size - ringbuf->tail;

	if (ringbuf->space < rem) {
		int ret = logical_ring_wait_for_space(ringbuf, rem);

		if (ret)
			return ret;
	}

	virt = ringbuf->virtual_start + ringbuf->tail;
	rem /= 4;
	while (rem--)
		iowrite32(MI_NOOP, virt++);

	ringbuf->tail = 0;
	ringbuf->space = intel_ring_space(ringbuf);

	return 0;
}

static int logical_ring_prepare(struct intel_ringbuffer *ringbuf, int bytes)
{
	int ret;

	if (unlikely(ringbuf->tail + bytes > ringbuf->effective_size)) {
		ret = logical_ring_wrap_buffer(ringbuf);
		if (unlikely(ret))
			return ret;
	}

	if (unlikely(ringbuf->space < bytes)) {
		ret = logical_ring_wait_for_space(ringbuf, bytes);
		if (unlikely(ret))
			return ret;
	}

	return 0;
}

/**
 * intel_logical_ring_begin() - prepare the logical ringbuffer to accept some commands
 *
 * @ringbuf: Logical ringbuffer.
 * @num_dwords: number of DWORDs that we plan to write to the ringbuffer.
 *
 * The ringbuffer might not be ready to accept the commands right away (maybe it needs to
 * be wrapped, or wait a bit for the tail to be updated). This function takes care of that
 * and also preallocates a request (every workload submission is still mediated through
 * requests, same as it did with legacy ringbuffer submission).
 *
 * Return: non-zero if the ringbuffer is not ready to be written to.
 */
int intel_logical_ring_begin(struct intel_ringbuffer *ringbuf, int num_dwords)
{
	struct intel_engine_cs *ring = ringbuf->ring;
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	ret = i915_gem_check_wedge(&dev_priv->gpu_error,
				   dev_priv->mm.interruptible);
	if (ret)
		return ret;

	ret = logical_ring_prepare(ringbuf, num_dwords * sizeof(uint32_t));
	if (ret)
		return ret;

	/* Preallocate the olr before touching the ring */
	ret = logical_ring_alloc_seqno(ring, ringbuf->FIXME_lrc_ctx);
	if (ret)
		return ret;

	ringbuf->space -= num_dwords * sizeof(uint32_t);
	return 0;
}

static int gen8_init_common_ring(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE_IMR(ring, ~(ring->irq_enable_mask | ring->irq_keep_mask));
	I915_WRITE(RING_HWSTAM(ring->mmio_base), 0xffffffff);

	I915_WRITE(RING_MODE_GEN7(ring),
		   _MASKED_BIT_DISABLE(GFX_REPLAY_MODE) |
		   _MASKED_BIT_ENABLE(GFX_RUN_LIST_ENABLE));
	POSTING_READ(RING_MODE_GEN7(ring));
	DRM_DEBUG_DRIVER("Execlists enabled for %s\n", ring->name);

	memset(&ring->hangcheck, 0, sizeof(ring->hangcheck));

	return 0;
}

static int gen8_init_render_ring(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	ret = gen8_init_common_ring(ring);
	if (ret)
		return ret;

	/* We need to disable the AsyncFlip performance optimisations in order
	 * to use MI_WAIT_FOR_EVENT within the CS. It should already be
	 * programmed to '1' on all products.
	 *
	 * WaDisableAsyncFlipPerfMode:snb,ivb,hsw,vlv,bdw,chv
	 */
	I915_WRITE(MI_MODE, _MASKED_BIT_ENABLE(ASYNC_FLIP_PERF_DISABLE));

	ret = intel_init_pipe_control(ring);
	if (ret)
		return ret;

	I915_WRITE(INSTPM, _MASKED_BIT_ENABLE(INSTPM_FORCE_ORDERING));

	return ret;
}

static int gen8_emit_bb_start(struct intel_ringbuffer *ringbuf,
			      u64 offset, unsigned flags)
{
	bool ppgtt = !(flags & I915_DISPATCH_SECURE);
	int ret;

	ret = intel_logical_ring_begin(ringbuf, 4);
	if (ret)
		return ret;

	/* FIXME(BDW): Address space and security selectors. */
	intel_logical_ring_emit(ringbuf, MI_BATCH_BUFFER_START_GEN8 | (ppgtt<<8));
	intel_logical_ring_emit(ringbuf, lower_32_bits(offset));
	intel_logical_ring_emit(ringbuf, upper_32_bits(offset));
	intel_logical_ring_emit(ringbuf, MI_NOOP);
	intel_logical_ring_advance(ringbuf);

	return 0;
}

static bool gen8_logical_ring_get_irq(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long flags;

	if (!dev->irq_enabled)
		return false;

	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	if (ring->irq_refcount++ == 0) {
		I915_WRITE_IMR(ring, ~(ring->irq_enable_mask | ring->irq_keep_mask));
		POSTING_READ(RING_IMR(ring->mmio_base));
	}
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);

	return true;
}

static void gen8_logical_ring_put_irq(struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	if (--ring->irq_refcount == 0) {
		I915_WRITE_IMR(ring, ~ring->irq_keep_mask);
		POSTING_READ(RING_IMR(ring->mmio_base));
	}
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);
}

static int gen8_emit_flush(struct intel_ringbuffer *ringbuf,
			   u32 invalidate_domains,
			   u32 unused)
{
	struct intel_engine_cs *ring = ringbuf->ring;
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t cmd;
	int ret;

	ret = intel_logical_ring_begin(ringbuf, 4);
	if (ret)
		return ret;

	cmd = MI_FLUSH_DW + 1;

	if (ring == &dev_priv->ring[VCS]) {
		if (invalidate_domains & I915_GEM_GPU_DOMAINS)
			cmd |= MI_INVALIDATE_TLB | MI_INVALIDATE_BSD |
				MI_FLUSH_DW_STORE_INDEX |
				MI_FLUSH_DW_OP_STOREDW;
	} else {
		if (invalidate_domains & I915_GEM_DOMAIN_RENDER)
			cmd |= MI_INVALIDATE_TLB | MI_FLUSH_DW_STORE_INDEX |
				MI_FLUSH_DW_OP_STOREDW;
	}

	intel_logical_ring_emit(ringbuf, cmd);
	intel_logical_ring_emit(ringbuf,
				I915_GEM_HWS_SCRATCH_ADDR |
				MI_FLUSH_DW_USE_GTT);
	intel_logical_ring_emit(ringbuf, 0); /* upper addr */
	intel_logical_ring_emit(ringbuf, 0); /* value */
	intel_logical_ring_advance(ringbuf);

	return 0;
}

static int gen8_emit_flush_render(struct intel_ringbuffer *ringbuf,
				  u32 invalidate_domains,
				  u32 flush_domains)
{
	struct intel_engine_cs *ring = ringbuf->ring;
	u32 scratch_addr = ring->scratch.gtt_offset + 2 * CACHELINE_BYTES;
	u32 flags = 0;
	int ret;

	flags |= PIPE_CONTROL_CS_STALL;

	if (flush_domains) {
		flags |= PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH;
		flags |= PIPE_CONTROL_DEPTH_CACHE_FLUSH;
	}

	if (invalidate_domains) {
		flags |= PIPE_CONTROL_TLB_INVALIDATE;
		flags |= PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_VF_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_CONST_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_STATE_CACHE_INVALIDATE;
		flags |= PIPE_CONTROL_QW_WRITE;
		flags |= PIPE_CONTROL_GLOBAL_GTT_IVB;
	}

	ret = intel_logical_ring_begin(ringbuf, 6);
	if (ret)
		return ret;

	intel_logical_ring_emit(ringbuf, GFX_OP_PIPE_CONTROL(6));
	intel_logical_ring_emit(ringbuf, flags);
	intel_logical_ring_emit(ringbuf, scratch_addr);
	intel_logical_ring_emit(ringbuf, 0);
	intel_logical_ring_emit(ringbuf, 0);
	intel_logical_ring_emit(ringbuf, 0);
	intel_logical_ring_advance(ringbuf);

	return 0;
}

static u32 gen8_get_seqno(struct intel_engine_cs *ring, bool lazy_coherency)
{
	return intel_read_status_page(ring, I915_GEM_HWS_INDEX);
}

static void gen8_set_seqno(struct intel_engine_cs *ring, u32 seqno)
{
	intel_write_status_page(ring, I915_GEM_HWS_INDEX, seqno);
}

static int gen8_emit_request(struct intel_ringbuffer *ringbuf)
{
	struct intel_engine_cs *ring = ringbuf->ring;
	u32 cmd;
	int ret;

	ret = intel_logical_ring_begin(ringbuf, 6);
	if (ret)
		return ret;

	cmd = MI_STORE_DWORD_IMM_GEN8;
	cmd |= MI_GLOBAL_GTT;

	intel_logical_ring_emit(ringbuf, cmd);
	intel_logical_ring_emit(ringbuf,
				(ring->status_page.gfx_addr +
				(I915_GEM_HWS_INDEX << MI_STORE_DWORD_INDEX_SHIFT)));
	intel_logical_ring_emit(ringbuf, 0);
	intel_logical_ring_emit(ringbuf, ring->outstanding_lazy_seqno);
	intel_logical_ring_emit(ringbuf, MI_USER_INTERRUPT);
	intel_logical_ring_emit(ringbuf, MI_NOOP);
	intel_logical_ring_advance_and_submit(ringbuf);

	return 0;
}

/**
 * intel_logical_ring_cleanup() - deallocate the Engine Command Streamer
 *
 * @ring: Engine Command Streamer.
 *
 */
void intel_logical_ring_cleanup(struct intel_engine_cs *ring)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;

	if (!intel_ring_initialized(ring))
		return;

	intel_logical_ring_stop(ring);
	WARN_ON((I915_READ_MODE(ring) & MODE_IDLE) == 0);
	ring->preallocated_lazy_request = NULL;
	ring->outstanding_lazy_seqno = 0;

	if (ring->cleanup)
		ring->cleanup(ring);

	i915_cmd_parser_fini_ring(ring);

	if (ring->status_page.obj) {
		kunmap(sg_page(ring->status_page.obj->pages->sgl));
		ring->status_page.obj = NULL;
	}
}

static int logical_ring_init(struct drm_device *dev, struct intel_engine_cs *ring)
{
	int ret;

	/* Intentionally left blank. */
	ring->buffer = NULL;

	ring->dev = dev;
	INIT_LIST_HEAD(&ring->active_list);
	INIT_LIST_HEAD(&ring->request_list);
	init_waitqueue_head(&ring->irq_queue);

	INIT_LIST_HEAD(&ring->execlist_queue);
	spin_lock_init(&ring->execlist_lock);
	ring->next_context_status_buffer = 0;

	ret = i915_cmd_parser_init_ring(ring);
	if (ret)
		return ret;

	if (ring->init) {
		ret = ring->init(ring);
		if (ret)
			return ret;
	}

	ret = intel_lr_context_deferred_create(ring->default_context, ring);

	return ret;
}

static int logical_render_ring_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring = &dev_priv->ring[RCS];

	ring->name = "render ring";
	ring->id = RCS;
	ring->mmio_base = RENDER_RING_BASE;
	ring->irq_enable_mask =
		GT_RENDER_USER_INTERRUPT << GEN8_RCS_IRQ_SHIFT;
	ring->irq_keep_mask =
		GT_CONTEXT_SWITCH_INTERRUPT << GEN8_RCS_IRQ_SHIFT;
	if (HAS_L3_DPF(dev))
		ring->irq_keep_mask |= GT_RENDER_L3_PARITY_ERROR_INTERRUPT;

	ring->init = gen8_init_render_ring;
	ring->cleanup = intel_fini_pipe_control;
	ring->get_seqno = gen8_get_seqno;
	ring->set_seqno = gen8_set_seqno;
	ring->emit_request = gen8_emit_request;
	ring->emit_flush = gen8_emit_flush_render;
	ring->irq_get = gen8_logical_ring_get_irq;
	ring->irq_put = gen8_logical_ring_put_irq;
	ring->emit_bb_start = gen8_emit_bb_start;

	return logical_ring_init(dev, ring);
}

static int logical_bsd_ring_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring = &dev_priv->ring[VCS];

	ring->name = "bsd ring";
	ring->id = VCS;
	ring->mmio_base = GEN6_BSD_RING_BASE;
	ring->irq_enable_mask =
		GT_RENDER_USER_INTERRUPT << GEN8_VCS1_IRQ_SHIFT;
	ring->irq_keep_mask =
		GT_CONTEXT_SWITCH_INTERRUPT << GEN8_VCS1_IRQ_SHIFT;

	ring->init = gen8_init_common_ring;
	ring->get_seqno = gen8_get_seqno;
	ring->set_seqno = gen8_set_seqno;
	ring->emit_request = gen8_emit_request;
	ring->emit_flush = gen8_emit_flush;
	ring->irq_get = gen8_logical_ring_get_irq;
	ring->irq_put = gen8_logical_ring_put_irq;
	ring->emit_bb_start = gen8_emit_bb_start;

	return logical_ring_init(dev, ring);
}

static int logical_bsd2_ring_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring = &dev_priv->ring[VCS2];

	ring->name = "bds2 ring";
	ring->id = VCS2;
	ring->mmio_base = GEN8_BSD2_RING_BASE;
	ring->irq_enable_mask =
		GT_RENDER_USER_INTERRUPT << GEN8_VCS2_IRQ_SHIFT;
	ring->irq_keep_mask =
		GT_CONTEXT_SWITCH_INTERRUPT << GEN8_VCS2_IRQ_SHIFT;

	ring->init = gen8_init_common_ring;
	ring->get_seqno = gen8_get_seqno;
	ring->set_seqno = gen8_set_seqno;
	ring->emit_request = gen8_emit_request;
	ring->emit_flush = gen8_emit_flush;
	ring->irq_get = gen8_logical_ring_get_irq;
	ring->irq_put = gen8_logical_ring_put_irq;
	ring->emit_bb_start = gen8_emit_bb_start;

	return logical_ring_init(dev, ring);
}

static int logical_blt_ring_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring = &dev_priv->ring[BCS];

	ring->name = "blitter ring";
	ring->id = BCS;
	ring->mmio_base = BLT_RING_BASE;
	ring->irq_enable_mask =
		GT_RENDER_USER_INTERRUPT << GEN8_BCS_IRQ_SHIFT;
	ring->irq_keep_mask =
		GT_CONTEXT_SWITCH_INTERRUPT << GEN8_BCS_IRQ_SHIFT;

	ring->init = gen8_init_common_ring;
	ring->get_seqno = gen8_get_seqno;
	ring->set_seqno = gen8_set_seqno;
	ring->emit_request = gen8_emit_request;
	ring->emit_flush = gen8_emit_flush;
	ring->irq_get = gen8_logical_ring_get_irq;
	ring->irq_put = gen8_logical_ring_put_irq;
	ring->emit_bb_start = gen8_emit_bb_start;

	return logical_ring_init(dev, ring);
}

static int logical_vebox_ring_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_engine_cs *ring = &dev_priv->ring[VECS];

	ring->name = "video enhancement ring";
	ring->id = VECS;
	ring->mmio_base = VEBOX_RING_BASE;
	ring->irq_enable_mask =
		GT_RENDER_USER_INTERRUPT << GEN8_VECS_IRQ_SHIFT;
	ring->irq_keep_mask =
		GT_CONTEXT_SWITCH_INTERRUPT << GEN8_VECS_IRQ_SHIFT;

	ring->init = gen8_init_common_ring;
	ring->get_seqno = gen8_get_seqno;
	ring->set_seqno = gen8_set_seqno;
	ring->emit_request = gen8_emit_request;
	ring->emit_flush = gen8_emit_flush;
	ring->irq_get = gen8_logical_ring_get_irq;
	ring->irq_put = gen8_logical_ring_put_irq;
	ring->emit_bb_start = gen8_emit_bb_start;

	return logical_ring_init(dev, ring);
}

/**
 * intel_logical_rings_init() - allocate, populate and init the Engine Command Streamers
 * @dev: DRM device.
 *
 * This function inits the engines for an Execlists submission style (the equivalent in the
 * legacy ringbuffer submission world would be i915_gem_init_rings). It does it only for
 * those engines that are present in the hardware.
 *
 * Return: non-zero if the initialization failed.
 */
int intel_logical_rings_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	ret = logical_render_ring_init(dev);
	if (ret)
		return ret;

	if (HAS_BSD(dev)) {
		ret = logical_bsd_ring_init(dev);
		if (ret)
			goto cleanup_render_ring;
	}

	if (HAS_BLT(dev)) {
		ret = logical_blt_ring_init(dev);
		if (ret)
			goto cleanup_bsd_ring;
	}

	if (HAS_VEBOX(dev)) {
		ret = logical_vebox_ring_init(dev);
		if (ret)
			goto cleanup_blt_ring;
	}

	if (HAS_BSD2(dev)) {
		ret = logical_bsd2_ring_init(dev);
		if (ret)
			goto cleanup_vebox_ring;
	}

	ret = i915_gem_set_seqno(dev, ((u32)~0 - 0x1000));
	if (ret)
		goto cleanup_bsd2_ring;

	return 0;

cleanup_bsd2_ring:
	intel_logical_ring_cleanup(&dev_priv->ring[VCS2]);
cleanup_vebox_ring:
	intel_logical_ring_cleanup(&dev_priv->ring[VECS]);
cleanup_blt_ring:
	intel_logical_ring_cleanup(&dev_priv->ring[BCS]);
cleanup_bsd_ring:
	intel_logical_ring_cleanup(&dev_priv->ring[VCS]);
cleanup_render_ring:
	intel_logical_ring_cleanup(&dev_priv->ring[RCS]);

	return ret;
}

int intel_lr_context_render_state_init(struct intel_engine_cs *ring,
				       struct intel_context *ctx)
{
	struct intel_ringbuffer *ringbuf = ctx->engine[ring->id].ringbuf;
	struct render_state so;
	struct drm_i915_file_private *file_priv = ctx->file_priv;
	struct drm_file *file = file_priv ? file_priv->file : NULL;
	int ret;

	ret = i915_gem_render_state_prepare(ring, &so);
	if (ret)
		return ret;

	if (so.rodata == NULL)
		return 0;

	ret = ring->emit_bb_start(ringbuf,
			so.ggtt_offset,
			I915_DISPATCH_SECURE);
	if (ret)
		goto out;

	i915_vma_move_to_active(i915_gem_obj_to_ggtt(so.obj), ring);

	ret = __i915_add_request(ring, file, so.obj, NULL);
	/* intel_logical_ring_add_request moves object to inactive if it
	 * fails */
out:
	i915_gem_render_state_fini(&so);
	return ret;
}

static int
populate_lr_context(struct intel_context *ctx, struct drm_i915_gem_object *ctx_obj,
		    struct intel_engine_cs *ring, struct intel_ringbuffer *ringbuf)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *ring_obj = ringbuf->obj;
	struct i915_hw_ppgtt *ppgtt = ctx->ppgtt;
	struct page *page;
	uint32_t *reg_state;
	int ret;

	if (!ppgtt)
		ppgtt = dev_priv->mm.aliasing_ppgtt;

	ret = i915_gem_object_set_to_cpu_domain(ctx_obj, true);
	if (ret) {
		DRM_DEBUG_DRIVER("Could not set to CPU domain\n");
		return ret;
	}

	ret = i915_gem_object_get_pages(ctx_obj);
	if (ret) {
		DRM_DEBUG_DRIVER("Could not get object pages\n");
		return ret;
	}

	i915_gem_object_pin_pages(ctx_obj);

	/* The second page of the context object contains some fields which must
	 * be set up prior to the first execution. */
	page = i915_gem_object_get_page(ctx_obj, 1);
	reg_state = kmap_atomic(page);

	/* A context is actually a big batch buffer with several MI_LOAD_REGISTER_IMM
	 * commands followed by (reg, value) pairs. The values we are setting here are
	 * only for the first context restore: on a subsequent save, the GPU will
	 * recreate this batchbuffer with new values (including all the missing
	 * MI_LOAD_REGISTER_IMM commands that we are not initializing here). */
	if (ring->id == RCS)
		reg_state[CTX_LRI_HEADER_0] = MI_LOAD_REGISTER_IMM(14);
	else
		reg_state[CTX_LRI_HEADER_0] = MI_LOAD_REGISTER_IMM(11);
	reg_state[CTX_LRI_HEADER_0] |= MI_LRI_FORCE_POSTED;
	reg_state[CTX_CONTEXT_CONTROL] = RING_CONTEXT_CONTROL(ring);
	reg_state[CTX_CONTEXT_CONTROL+1] =
			_MASKED_BIT_ENABLE((1<<3) | MI_RESTORE_INHIBIT);
	reg_state[CTX_RING_HEAD] = RING_HEAD(ring->mmio_base);
	reg_state[CTX_RING_HEAD+1] = 0;
	reg_state[CTX_RING_TAIL] = RING_TAIL(ring->mmio_base);
	reg_state[CTX_RING_TAIL+1] = 0;
	reg_state[CTX_RING_BUFFER_START] = RING_START(ring->mmio_base);
	reg_state[CTX_RING_BUFFER_START+1] = i915_gem_obj_ggtt_offset(ring_obj);
	reg_state[CTX_RING_BUFFER_CONTROL] = RING_CTL(ring->mmio_base);
	reg_state[CTX_RING_BUFFER_CONTROL+1] =
			((ringbuf->size - PAGE_SIZE) & RING_NR_PAGES) | RING_VALID;
	reg_state[CTX_BB_HEAD_U] = ring->mmio_base + 0x168;
	reg_state[CTX_BB_HEAD_U+1] = 0;
	reg_state[CTX_BB_HEAD_L] = ring->mmio_base + 0x140;
	reg_state[CTX_BB_HEAD_L+1] = 0;
	reg_state[CTX_BB_STATE] = ring->mmio_base + 0x110;
	reg_state[CTX_BB_STATE+1] = (1<<5);
	reg_state[CTX_SECOND_BB_HEAD_U] = ring->mmio_base + 0x11c;
	reg_state[CTX_SECOND_BB_HEAD_U+1] = 0;
	reg_state[CTX_SECOND_BB_HEAD_L] = ring->mmio_base + 0x114;
	reg_state[CTX_SECOND_BB_HEAD_L+1] = 0;
	reg_state[CTX_SECOND_BB_STATE] = ring->mmio_base + 0x118;
	reg_state[CTX_SECOND_BB_STATE+1] = 0;
	if (ring->id == RCS) {
		/* TODO: according to BSpec, the register state context
		 * for CHV does not have these. OTOH, these registers do
		 * exist in CHV. I'm waiting for a clarification */
		reg_state[CTX_BB_PER_CTX_PTR] = ring->mmio_base + 0x1c0;
		reg_state[CTX_BB_PER_CTX_PTR+1] = 0;
		reg_state[CTX_RCS_INDIRECT_CTX] = ring->mmio_base + 0x1c4;
		reg_state[CTX_RCS_INDIRECT_CTX+1] = 0;
		reg_state[CTX_RCS_INDIRECT_CTX_OFFSET] = ring->mmio_base + 0x1c8;
		reg_state[CTX_RCS_INDIRECT_CTX_OFFSET+1] = 0;
	}
	reg_state[CTX_LRI_HEADER_1] = MI_LOAD_REGISTER_IMM(9);
	reg_state[CTX_LRI_HEADER_1] |= MI_LRI_FORCE_POSTED;
	reg_state[CTX_CTX_TIMESTAMP] = ring->mmio_base + 0x3a8;
	reg_state[CTX_CTX_TIMESTAMP+1] = 0;
	reg_state[CTX_PDP3_UDW] = GEN8_RING_PDP_UDW(ring, 3);
	reg_state[CTX_PDP3_LDW] = GEN8_RING_PDP_LDW(ring, 3);
	reg_state[CTX_PDP2_UDW] = GEN8_RING_PDP_UDW(ring, 2);
	reg_state[CTX_PDP2_LDW] = GEN8_RING_PDP_LDW(ring, 2);
	reg_state[CTX_PDP1_UDW] = GEN8_RING_PDP_UDW(ring, 1);
	reg_state[CTX_PDP1_LDW] = GEN8_RING_PDP_LDW(ring, 1);
	reg_state[CTX_PDP0_UDW] = GEN8_RING_PDP_UDW(ring, 0);
	reg_state[CTX_PDP0_LDW] = GEN8_RING_PDP_LDW(ring, 0);
	reg_state[CTX_PDP3_UDW+1] = upper_32_bits(ppgtt->pd_dma_addr[3]);
	reg_state[CTX_PDP3_LDW+1] = lower_32_bits(ppgtt->pd_dma_addr[3]);
	reg_state[CTX_PDP2_UDW+1] = upper_32_bits(ppgtt->pd_dma_addr[2]);
	reg_state[CTX_PDP2_LDW+1] = lower_32_bits(ppgtt->pd_dma_addr[2]);
	reg_state[CTX_PDP1_UDW+1] = upper_32_bits(ppgtt->pd_dma_addr[1]);
	reg_state[CTX_PDP1_LDW+1] = lower_32_bits(ppgtt->pd_dma_addr[1]);
	reg_state[CTX_PDP0_UDW+1] = upper_32_bits(ppgtt->pd_dma_addr[0]);
	reg_state[CTX_PDP0_LDW+1] = lower_32_bits(ppgtt->pd_dma_addr[0]);
	if (ring->id == RCS) {
		reg_state[CTX_LRI_HEADER_2] = MI_LOAD_REGISTER_IMM(1);
		reg_state[CTX_R_PWR_CLK_STATE] = 0x20c8;
		reg_state[CTX_R_PWR_CLK_STATE+1] = 0;
	}

	kunmap_atomic(reg_state);

	ctx_obj->dirty = 1;
	set_page_dirty(page);
	i915_gem_object_unpin_pages(ctx_obj);

	return 0;
}

/**
 * intel_lr_context_free() - free the LRC specific bits of a context
 * @ctx: the LR context to free.
 *
 * The real context freeing is done in i915_gem_context_free: this only
 * takes care of the bits that are LRC related: the per-engine backing
 * objects and the logical ringbuffer.
 */
void intel_lr_context_free(struct intel_context *ctx)
{
	int i;

	for (i = 0; i < I915_NUM_RINGS; i++) {
		struct drm_i915_gem_object *ctx_obj = ctx->engine[i].state;
		struct intel_ringbuffer *ringbuf = ctx->engine[i].ringbuf;

		if (ctx_obj) {
			intel_destroy_ringbuffer_obj(ringbuf);
			kfree(ringbuf);
			i915_gem_object_ggtt_unpin(ctx_obj);
			drm_gem_object_unreference(&ctx_obj->base);
		}
	}
}

static uint32_t get_lr_context_size(struct intel_engine_cs *ring)
{
	int ret = 0;

	WARN_ON(INTEL_INFO(ring->dev)->gen != 8);

	switch (ring->id) {
	case RCS:
		ret = GEN8_LR_CONTEXT_RENDER_SIZE;
		break;
	case VCS:
	case BCS:
	case VECS:
	case VCS2:
		ret = GEN8_LR_CONTEXT_OTHER_SIZE;
		break;
	}

	return ret;
}

/**
 * intel_lr_context_deferred_create() - create the LRC specific bits of a context
 * @ctx: LR context to create.
 * @ring: engine to be used with the context.
 *
 * This function can be called more than once, with different engines, if we plan
 * to use the context with them. The context backing objects and the ringbuffers
 * (specially the ringbuffer backing objects) suck a lot of memory up, and that's why
 * the creation is a deferred call: it's better to make sure first that we need to use
 * a given ring with the context.
 *
 * Return: non-zero on eror.
 */
int intel_lr_context_deferred_create(struct intel_context *ctx,
				     struct intel_engine_cs *ring)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_gem_object *ctx_obj;
	uint32_t context_size;
	struct intel_ringbuffer *ringbuf;
	int ret;

	WARN_ON(ctx->legacy_hw_ctx.rcs_state != NULL);
	if (ctx->engine[ring->id].state)
		return 0;

	context_size = round_up(get_lr_context_size(ring), 4096);

	ctx_obj = i915_gem_alloc_context_obj(dev, context_size);
	if (IS_ERR(ctx_obj)) {
		ret = PTR_ERR(ctx_obj);
		DRM_DEBUG_DRIVER("Alloc LRC backing obj failed: %d\n", ret);
		return ret;
	}

	ret = i915_gem_obj_ggtt_pin(ctx_obj, GEN8_LR_CONTEXT_ALIGN, 0);
	if (ret) {
		DRM_DEBUG_DRIVER("Pin LRC backing obj failed: %d\n", ret);
		drm_gem_object_unreference(&ctx_obj->base);
		return ret;
	}

	ringbuf = kzalloc(sizeof(*ringbuf), GFP_KERNEL);
	if (!ringbuf) {
		DRM_DEBUG_DRIVER("Failed to allocate ringbuffer %s\n",
				ring->name);
		i915_gem_object_ggtt_unpin(ctx_obj);
		drm_gem_object_unreference(&ctx_obj->base);
		ret = -ENOMEM;
		return ret;
	}

	ringbuf->ring = ring;
	ringbuf->FIXME_lrc_ctx = ctx;

	ringbuf->size = 32 * PAGE_SIZE;
	ringbuf->effective_size = ringbuf->size;
	ringbuf->head = 0;
	ringbuf->tail = 0;
	ringbuf->space = ringbuf->size;
	ringbuf->last_retired_head = -1;

	/* TODO: For now we put this in the mappable region so that we can reuse
	 * the existing ringbuffer code which ioremaps it. When we start
	 * creating many contexts, this will no longer work and we must switch
	 * to a kmapish interface.
	 */
	ret = intel_alloc_ringbuffer_obj(dev, ringbuf);
	if (ret) {
		DRM_DEBUG_DRIVER("Failed to allocate ringbuffer obj %s: %d\n",
				ring->name, ret);
		goto error;
	}

	ret = populate_lr_context(ctx, ctx_obj, ring, ringbuf);
	if (ret) {
		DRM_DEBUG_DRIVER("Failed to populate LRC: %d\n", ret);
		intel_destroy_ringbuffer_obj(ringbuf);
		goto error;
	}

	ctx->engine[ring->id].ringbuf = ringbuf;
	ctx->engine[ring->id].state = ctx_obj;

	if (ctx == ring->default_context) {
		/* The status page is offset 0 from the default context object
		 * in LRC mode. */
		ring->status_page.gfx_addr = i915_gem_obj_ggtt_offset(ctx_obj);
		ring->status_page.page_addr =
				kmap(sg_page(ctx_obj->pages->sgl));
		if (ring->status_page.page_addr == NULL)
			return -ENOMEM;
		ring->status_page.obj = ctx_obj;
	}

	if (ring->id == RCS && !ctx->rcs_initialized) {
		ret = intel_lr_context_render_state_init(ring, ctx);
		if (ret) {
			DRM_ERROR("Init render state failed: %d\n", ret);
			ctx->engine[ring->id].ringbuf = NULL;
			ctx->engine[ring->id].state = NULL;
			intel_destroy_ringbuffer_obj(ringbuf);
			goto error;
		}
		ctx->rcs_initialized = true;
	}

	return 0;

error:
	kfree(ringbuf);
	i915_gem_object_ggtt_unpin(ctx_obj);
	drm_gem_object_unreference(&ctx_obj->base);
	return ret;
}
