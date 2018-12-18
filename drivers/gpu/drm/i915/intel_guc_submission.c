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
 */

#include <linux/circ_buf.h>
#include <trace/events/dma_fence.h>

#include "intel_guc_submission.h"
#include "intel_lrc_reg.h"
#include "i915_drv.h"

#define GUC_PREEMPT_FINISHED		0x1
#define GUC_PREEMPT_BREADCRUMB_DWORDS	0x8
#define GUC_PREEMPT_BREADCRUMB_BYTES	\
	(sizeof(u32) * GUC_PREEMPT_BREADCRUMB_DWORDS)

/**
 * DOC: GuC-based command submission
 *
 * GuC client:
 * A intel_guc_client refers to a submission path through GuC. Currently, there
 * are two clients. One of them (the execbuf_client) is charged with all
 * submissions to the GuC, the other one (preempt_client) is responsible for
 * preempting the execbuf_client. This struct is the owner of a doorbell, a
 * process descriptor and a workqueue (all of them inside a single gem object
 * that contains all required pages for these elements).
 *
 * GuC stage descriptor:
 * During initialization, the driver allocates a static pool of 1024 such
 * descriptors, and shares them with the GuC.
 * Currently, there exists a 1:1 mapping between a intel_guc_client and a
 * guc_stage_desc (via the client's stage_id), so effectively only one
 * gets used. This stage descriptor lets the GuC know about the doorbell,
 * workqueue and process descriptor. Theoretically, it also lets the GuC
 * know about our HW contexts (context ID, etc...), but we actually
 * employ a kind of submission where the GuC uses the LRCA sent via the work
 * item instead (the single guc_stage_desc associated to execbuf client
 * contains information about the default kernel context only, but this is
 * essentially unused). This is called a "proxy" submission.
 *
 * The Scratch registers:
 * There are 16 MMIO-based registers start from 0xC180. The kernel driver writes
 * a value to the action register (SOFT_SCRATCH_0) along with any data. It then
 * triggers an interrupt on the GuC via another register write (0xC4C8).
 * Firmware writes a success/fail code back to the action register after
 * processes the request. The kernel driver polls waiting for this update and
 * then proceeds.
 * See intel_guc_send()
 *
 * Doorbells:
 * Doorbells are interrupts to uKernel. A doorbell is a single cache line (QW)
 * mapped into process space.
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

static inline bool is_high_priority(struct intel_guc_client *client)
{
	return (client->priority == GUC_CLIENT_PRIORITY_KMD_HIGH ||
		client->priority == GUC_CLIENT_PRIORITY_HIGH);
}

static int reserve_doorbell(struct intel_guc_client *client)
{
	unsigned long offset;
	unsigned long end;
	u16 id;

	GEM_BUG_ON(client->doorbell_id != GUC_DOORBELL_INVALID);

	/*
	 * The bitmap tracks which doorbell registers are currently in use.
	 * It is split into two halves; the first half is used for normal
	 * priority contexts, the second half for high-priority ones.
	 */
	offset = 0;
	end = GUC_NUM_DOORBELLS / 2;
	if (is_high_priority(client)) {
		offset = end;
		end += offset;
	}

	id = find_next_zero_bit(client->guc->doorbell_bitmap, end, offset);
	if (id == end)
		return -ENOSPC;

	__set_bit(id, client->guc->doorbell_bitmap);
	client->doorbell_id = id;
	DRM_DEBUG_DRIVER("client %u (high prio=%s) reserved doorbell: %d\n",
			 client->stage_id, yesno(is_high_priority(client)),
			 id);
	return 0;
}

static bool has_doorbell(struct intel_guc_client *client)
{
	if (client->doorbell_id == GUC_DOORBELL_INVALID)
		return false;

	return test_bit(client->doorbell_id, client->guc->doorbell_bitmap);
}

static void unreserve_doorbell(struct intel_guc_client *client)
{
	GEM_BUG_ON(!has_doorbell(client));

	__clear_bit(client->doorbell_id, client->guc->doorbell_bitmap);
	client->doorbell_id = GUC_DOORBELL_INVALID;
}

/*
 * Tell the GuC to allocate or deallocate a specific doorbell
 */

static int __guc_allocate_doorbell(struct intel_guc *guc, u32 stage_id)
{
	u32 action[] = {
		INTEL_GUC_ACTION_ALLOCATE_DOORBELL,
		stage_id
	};

	return intel_guc_send(guc, action, ARRAY_SIZE(action));
}

static int __guc_deallocate_doorbell(struct intel_guc *guc, u32 stage_id)
{
	u32 action[] = {
		INTEL_GUC_ACTION_DEALLOCATE_DOORBELL,
		stage_id
	};

	return intel_guc_send(guc, action, ARRAY_SIZE(action));
}

static struct guc_stage_desc *__get_stage_desc(struct intel_guc_client *client)
{
	struct guc_stage_desc *base = client->guc->stage_desc_pool_vaddr;

	return &base[client->stage_id];
}

/*
 * Initialise, update, or clear doorbell data shared with the GuC
 *
 * These functions modify shared data and so need access to the mapped
 * client object which contains the page being used for the doorbell
 */

static void __update_doorbell_desc(struct intel_guc_client *client, u16 new_id)
{
	struct guc_stage_desc *desc;

	/* Update the GuC's idea of the doorbell ID */
	desc = __get_stage_desc(client);
	desc->db_id = new_id;
}

static struct guc_doorbell_info *__get_doorbell(struct intel_guc_client *client)
{
	return client->vaddr + client->doorbell_offset;
}

static void __create_doorbell(struct intel_guc_client *client)
{
	struct guc_doorbell_info *doorbell;

	doorbell = __get_doorbell(client);
	doorbell->db_status = GUC_DOORBELL_ENABLED;
	doorbell->cookie = 0;
}

static void __destroy_doorbell(struct intel_guc_client *client)
{
	struct drm_i915_private *dev_priv = guc_to_i915(client->guc);
	struct guc_doorbell_info *doorbell;
	u16 db_id = client->doorbell_id;

	doorbell = __get_doorbell(client);
	doorbell->db_status = GUC_DOORBELL_DISABLED;
	doorbell->cookie = 0;

	/* Doorbell release flow requires that we wait for GEN8_DRB_VALID bit
	 * to go to zero after updating db_status before we call the GuC to
	 * release the doorbell
	 */
	if (wait_for_us(!(I915_READ(GEN8_DRBREGL(db_id)) & GEN8_DRB_VALID), 10))
		WARN_ONCE(true, "Doorbell never became invalid after disable\n");
}

static int create_doorbell(struct intel_guc_client *client)
{
	int ret;

	if (WARN_ON(!has_doorbell(client)))
		return -ENODEV; /* internal setup error, should never happen */

	__update_doorbell_desc(client, client->doorbell_id);
	__create_doorbell(client);

	ret = __guc_allocate_doorbell(client->guc, client->stage_id);
	if (ret) {
		__destroy_doorbell(client);
		__update_doorbell_desc(client, GUC_DOORBELL_INVALID);
		DRM_DEBUG_DRIVER("Couldn't create client %u doorbell: %d\n",
				 client->stage_id, ret);
		return ret;
	}

	return 0;
}

static int destroy_doorbell(struct intel_guc_client *client)
{
	int ret;

	GEM_BUG_ON(!has_doorbell(client));

	__destroy_doorbell(client);
	ret = __guc_deallocate_doorbell(client->guc, client->stage_id);
	if (ret)
		DRM_ERROR("Couldn't destroy client %u doorbell: %d\n",
			  client->stage_id, ret);

	__update_doorbell_desc(client, GUC_DOORBELL_INVALID);

	return ret;
}

static unsigned long __select_cacheline(struct intel_guc *guc)
{
	unsigned long offset;

	/* Doorbell uses a single cache line within a page */
	offset = offset_in_page(guc->db_cacheline);

	/* Moving to next cache line to reduce contention */
	guc->db_cacheline += cache_line_size();

	DRM_DEBUG_DRIVER("reserved cacheline 0x%lx, next 0x%x, linesize %u\n",
			 offset, guc->db_cacheline, cache_line_size());
	return offset;
}

static inline struct guc_process_desc *
__get_process_desc(struct intel_guc_client *client)
{
	return client->vaddr + client->proc_desc_offset;
}

/*
 * Initialise the process descriptor shared with the GuC firmware.
 */
static void guc_proc_desc_init(struct intel_guc *guc,
			       struct intel_guc_client *client)
{
	struct guc_process_desc *desc;

	desc = memset(__get_process_desc(client), 0, sizeof(*desc));

	/*
	 * XXX: pDoorbell and WQVBaseAddress are pointers in process address
	 * space for ring3 clients (set them as in mmap_ioctl) or kernel
	 * space for kernel clients (map on demand instead? May make debug
	 * easier to have it mapped).
	 */
	desc->wq_base_addr = 0;
	desc->db_base_addr = 0;

	desc->stage_id = client->stage_id;
	desc->wq_size_bytes = GUC_WQ_SIZE;
	desc->wq_status = WQ_STATUS_ACTIVE;
	desc->priority = client->priority;
}

static int guc_stage_desc_pool_create(struct intel_guc *guc)
{
	struct i915_vma *vma;
	void *vaddr;

	vma = intel_guc_allocate_vma(guc,
				     PAGE_ALIGN(sizeof(struct guc_stage_desc) *
				     GUC_MAX_STAGE_DESCRIPTORS));
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	vaddr = i915_gem_object_pin_map(vma->obj, I915_MAP_WB);
	if (IS_ERR(vaddr)) {
		i915_vma_unpin_and_release(&vma);
		return PTR_ERR(vaddr);
	}

	guc->stage_desc_pool = vma;
	guc->stage_desc_pool_vaddr = vaddr;
	ida_init(&guc->stage_ids);

	return 0;
}

static void guc_stage_desc_pool_destroy(struct intel_guc *guc)
{
	ida_destroy(&guc->stage_ids);
	i915_gem_object_unpin_map(guc->stage_desc_pool->obj);
	i915_vma_unpin_and_release(&guc->stage_desc_pool);
}

/*
 * Initialise/clear the stage descriptor shared with the GuC firmware.
 *
 * This descriptor tells the GuC where (in GGTT space) to find the important
 * data structures relating to this client (doorbell, process descriptor,
 * write queue, etc).
 */
static void guc_stage_desc_init(struct intel_guc *guc,
				struct intel_guc_client *client)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	struct intel_engine_cs *engine;
	struct i915_gem_context *ctx = client->owner;
	struct guc_stage_desc *desc;
	unsigned int tmp;
	u32 gfx_addr;

	desc = __get_stage_desc(client);
	memset(desc, 0, sizeof(*desc));

	desc->attribute = GUC_STAGE_DESC_ATTR_ACTIVE |
			  GUC_STAGE_DESC_ATTR_KERNEL;
	if (is_high_priority(client))
		desc->attribute |= GUC_STAGE_DESC_ATTR_PREEMPT;
	desc->stage_id = client->stage_id;
	desc->priority = client->priority;
	desc->db_id = client->doorbell_id;

	for_each_engine_masked(engine, dev_priv, client->engines, tmp) {
		struct intel_context *ce = to_intel_context(ctx, engine);
		u32 guc_engine_id = engine->guc_id;
		struct guc_execlist_context *lrc = &desc->lrc[guc_engine_id];

		/* TODO: We have a design issue to be solved here. Only when we
		 * receive the first batch, we know which engine is used by the
		 * user. But here GuC expects the lrc and ring to be pinned. It
		 * is not an issue for default context, which is the only one
		 * for now who owns a GuC client. But for future owner of GuC
		 * client, need to make sure lrc is pinned prior to enter here.
		 */
		if (!ce->state)
			break;	/* XXX: continue? */

		/*
		 * XXX: When this is a GUC_STAGE_DESC_ATTR_KERNEL client (proxy
		 * submission or, in other words, not using a direct submission
		 * model) the KMD's LRCA is not used for any work submission.
		 * Instead, the GuC uses the LRCA of the user mode context (see
		 * guc_add_request below).
		 */
		lrc->context_desc = lower_32_bits(ce->lrc_desc);

		/* The state page is after PPHWSP */
		lrc->ring_lrca = intel_guc_ggtt_offset(guc, ce->state) +
				 LRC_STATE_PN * PAGE_SIZE;

		/* XXX: In direct submission, the GuC wants the HW context id
		 * here. In proxy submission, it wants the stage id
		 */
		lrc->context_id = (client->stage_id << GUC_ELC_CTXID_OFFSET) |
				(guc_engine_id << GUC_ELC_ENGINE_OFFSET);

		lrc->ring_begin = intel_guc_ggtt_offset(guc, ce->ring->vma);
		lrc->ring_end = lrc->ring_begin + ce->ring->size - 1;
		lrc->ring_next_free_location = lrc->ring_begin;
		lrc->ring_current_tail_pointer_value = 0;

		desc->engines_used |= (1 << guc_engine_id);
	}

	DRM_DEBUG_DRIVER("Host engines 0x%x => GuC engines used 0x%x\n",
			 client->engines, desc->engines_used);
	WARN_ON(desc->engines_used == 0);

	/*
	 * The doorbell, process descriptor, and workqueue are all parts
	 * of the client object, which the GuC will reference via the GGTT
	 */
	gfx_addr = intel_guc_ggtt_offset(guc, client->vma);
	desc->db_trigger_phy = sg_dma_address(client->vma->pages->sgl) +
				client->doorbell_offset;
	desc->db_trigger_cpu = ptr_to_u64(__get_doorbell(client));
	desc->db_trigger_uk = gfx_addr + client->doorbell_offset;
	desc->process_desc = gfx_addr + client->proc_desc_offset;
	desc->wq_addr = gfx_addr + GUC_DB_SIZE;
	desc->wq_size = GUC_WQ_SIZE;

	desc->desc_private = ptr_to_u64(client);
}

static void guc_stage_desc_fini(struct intel_guc *guc,
				struct intel_guc_client *client)
{
	struct guc_stage_desc *desc;

	desc = __get_stage_desc(client);
	memset(desc, 0, sizeof(*desc));
}

/* Construct a Work Item and append it to the GuC's Work Queue */
static void guc_wq_item_append(struct intel_guc_client *client,
			       u32 target_engine, u32 context_desc,
			       u32 ring_tail, u32 fence_id)
{
	/* wqi_len is in DWords, and does not include the one-word header */
	const size_t wqi_size = sizeof(struct guc_wq_item);
	const u32 wqi_len = wqi_size / sizeof(u32) - 1;
	struct guc_process_desc *desc = __get_process_desc(client);
	struct guc_wq_item *wqi;
	u32 wq_off;

	lockdep_assert_held(&client->wq_lock);

	/* For now workqueue item is 4 DWs; workqueue buffer is 2 pages. So we
	 * should not have the case where structure wqi is across page, neither
	 * wrapped to the beginning. This simplifies the implementation below.
	 *
	 * XXX: if not the case, we need save data to a temp wqi and copy it to
	 * workqueue buffer dw by dw.
	 */
	BUILD_BUG_ON(wqi_size != 16);

	/* Free space is guaranteed. */
	wq_off = READ_ONCE(desc->tail);
	GEM_BUG_ON(CIRC_SPACE(wq_off, READ_ONCE(desc->head),
			      GUC_WQ_SIZE) < wqi_size);
	GEM_BUG_ON(wq_off & (wqi_size - 1));

	/* WQ starts from the page after doorbell / process_desc */
	wqi = client->vaddr + wq_off + GUC_DB_SIZE;

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

static void guc_reset_wq(struct intel_guc_client *client)
{
	struct guc_process_desc *desc = __get_process_desc(client);

	desc->head = 0;
	desc->tail = 0;
}

static void guc_ring_doorbell(struct intel_guc_client *client)
{
	struct guc_doorbell_info *db;
	u32 cookie;

	lockdep_assert_held(&client->wq_lock);

	/* pointer of current doorbell cacheline */
	db = __get_doorbell(client);

	/*
	 * We're not expecting the doorbell cookie to change behind our back,
	 * we also need to treat 0 as a reserved value.
	 */
	cookie = READ_ONCE(db->cookie);
	WARN_ON_ONCE(xchg(&db->cookie, cookie + 1 ?: cookie + 2) != cookie);

	/* XXX: doorbell was lost and need to acquire it again */
	GEM_BUG_ON(db->db_status != GUC_DOORBELL_ENABLED);
}

static void guc_add_request(struct intel_guc *guc, struct i915_request *rq)
{
	struct intel_guc_client *client = guc->execbuf_client;
	struct intel_engine_cs *engine = rq->engine;
	u32 ctx_desc = lower_32_bits(rq->hw_context->lrc_desc);
	u32 ring_tail = intel_ring_set_tail(rq->ring, rq->tail) / sizeof(u64);

	spin_lock(&client->wq_lock);

	guc_wq_item_append(client, engine->guc_id, ctx_desc,
			   ring_tail, rq->global_seqno);
	guc_ring_doorbell(client);

	client->submissions[engine->id] += 1;

	spin_unlock(&client->wq_lock);
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
	struct drm_i915_private *dev_priv = vma->vm->i915;

	if (i915_vma_is_map_and_fenceable(vma))
		POSTING_READ_FW(GUC_STATUS);
}

static void inject_preempt_context(struct work_struct *work)
{
	struct guc_preempt_work *preempt_work =
		container_of(work, typeof(*preempt_work), work);
	struct intel_engine_cs *engine = preempt_work->engine;
	struct intel_guc *guc = container_of(preempt_work, typeof(*guc),
					     preempt_work[engine->id]);
	struct intel_guc_client *client = guc->preempt_client;
	struct guc_stage_desc *stage_desc = __get_stage_desc(client);
	u32 ctx_desc = lower_32_bits(to_intel_context(client->owner,
						      engine)->lrc_desc);
	u32 data[7];

	/*
	 * The ring contains commands to write GUC_PREEMPT_FINISHED into HWSP.
	 * See guc_fill_preempt_context().
	 */
	spin_lock_irq(&client->wq_lock);
	guc_wq_item_append(client, engine->guc_id, ctx_desc,
			   GUC_PREEMPT_BREADCRUMB_BYTES / sizeof(u64), 0);
	spin_unlock_irq(&client->wq_lock);

	/*
	 * If GuC firmware performs an engine reset while that engine had
	 * a preemption pending, it will set the terminated attribute bit
	 * on our preemption stage descriptor. GuC firmware retains all
	 * pending work items for a high-priority GuC client, unlike the
	 * normal-priority GuC client where work items are dropped. It
	 * wants to make sure the preempt-to-idle work doesn't run when
	 * scheduling resumes, and uses this bit to inform its scheduler
	 * and presumably us as well. Our job is to clear it for the next
	 * preemption after reset, otherwise that and future preemptions
	 * will never complete. We'll just clear it every time.
	 */
	stage_desc->attribute &= ~GUC_STAGE_DESC_ATTR_TERMINATED;

	data[0] = INTEL_GUC_ACTION_REQUEST_PREEMPTION;
	data[1] = client->stage_id;
	data[2] = INTEL_GUC_PREEMPT_OPTION_DROP_WORK_Q |
		  INTEL_GUC_PREEMPT_OPTION_DROP_SUBMIT_Q;
	data[3] = engine->guc_id;
	data[4] = guc->execbuf_client->priority;
	data[5] = guc->execbuf_client->stage_id;
	data[6] = intel_guc_ggtt_offset(guc, guc->shared_data);

	if (WARN_ON(intel_guc_send(guc, data, ARRAY_SIZE(data)))) {
		execlists_clear_active(&engine->execlists,
				       EXECLISTS_ACTIVE_PREEMPT);
		tasklet_schedule(&engine->execlists.tasklet);
	}
}

/*
 * We're using user interrupt and HWSP value to mark that preemption has
 * finished and GPU is idle. Normally, we could unwind and continue similar to
 * execlists submission path. Unfortunately, with GuC we also need to wait for
 * it to finish its own postprocessing, before attempting to submit. Otherwise
 * GuC may silently ignore our submissions, and thus we risk losing request at
 * best, executing out-of-order and causing kernel panic at worst.
 */
#define GUC_PREEMPT_POSTPROCESS_DELAY_MS 10
static void wait_for_guc_preempt_report(struct intel_engine_cs *engine)
{
	struct intel_guc *guc = &engine->i915->guc;
	struct guc_shared_ctx_data *data = guc->shared_data_vaddr;
	struct guc_ctx_report *report =
		&data->preempt_ctx_report[engine->guc_id];

	WARN_ON(wait_for_atomic(report->report_return_status ==
				INTEL_GUC_REPORT_STATUS_COMPLETE,
				GUC_PREEMPT_POSTPROCESS_DELAY_MS));
	/*
	 * GuC is expecting that we're also going to clear the affected context
	 * counter, let's also reset the return status to not depend on GuC
	 * resetting it after recieving another preempt action
	 */
	report->affected_count = 0;
	report->report_return_status = INTEL_GUC_REPORT_STATUS_UNKNOWN;
}

static void complete_preempt_context(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists *execlists = &engine->execlists;

	GEM_BUG_ON(!execlists_is_active(execlists, EXECLISTS_ACTIVE_PREEMPT));

	if (inject_preempt_hang(execlists))
		return;

	execlists_cancel_port_requests(execlists);
	execlists_unwind_incomplete_requests(execlists);

	wait_for_guc_preempt_report(engine);
	intel_write_status_page(engine, I915_GEM_HWS_PREEMPT_INDEX, 0);
}

/**
 * guc_submit() - Submit commands through GuC
 * @engine: engine associated with the commands
 *
 * The only error here arises if the doorbell hardware isn't functioning
 * as expected, which really shouln't happen.
 */
static void guc_submit(struct intel_engine_cs *engine)
{
	struct intel_guc *guc = &engine->i915->guc;
	struct intel_engine_execlists * const execlists = &engine->execlists;
	struct execlist_port *port = execlists->port;
	unsigned int n;

	for (n = 0; n < execlists_num_ports(execlists); n++) {
		struct i915_request *rq;
		unsigned int count;

		rq = port_unpack(&port[n], &count);
		if (rq && count == 0) {
			port_set(&port[n], port_pack(rq, ++count));

			flush_ggtt_writes(rq->ring->vma);

			guc_add_request(guc, rq);
		}
	}
}

static void port_assign(struct execlist_port *port, struct i915_request *rq)
{
	GEM_BUG_ON(port_isset(port));

	port_set(port, i915_request_get(rq));
}

static inline int rq_prio(const struct i915_request *rq)
{
	return rq->sched.attr.priority;
}

static inline int port_prio(const struct execlist_port *port)
{
	return rq_prio(port_request(port));
}

static bool __guc_dequeue(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;
	struct execlist_port *port = execlists->port;
	struct i915_request *last = NULL;
	const struct execlist_port * const last_port =
		&execlists->port[execlists->port_mask];
	bool submit = false;
	struct rb_node *rb;

	lockdep_assert_held(&engine->timeline.lock);

	if (port_isset(port)) {
		if (intel_engine_has_preemption(engine)) {
			struct guc_preempt_work *preempt_work =
				&engine->i915->guc.preempt_work[engine->id];
			int prio = execlists->queue_priority;

			if (__execlists_need_preempt(prio, port_prio(port))) {
				execlists_set_active(execlists,
						     EXECLISTS_ACTIVE_PREEMPT);
				queue_work(engine->i915->guc.preempt_wq,
					   &preempt_work->work);
				return false;
			}
		}

		port++;
		if (port_isset(port))
			return false;
	}
	GEM_BUG_ON(port_isset(port));

	while ((rb = rb_first_cached(&execlists->queue))) {
		struct i915_priolist *p = to_priolist(rb);
		struct i915_request *rq, *rn;

		list_for_each_entry_safe(rq, rn, &p->requests, sched.link) {
			if (last && rq->hw_context != last->hw_context) {
				if (port == last_port) {
					__list_del_many(&p->requests,
							&rq->sched.link);
					goto done;
				}

				if (submit)
					port_assign(port, last);
				port++;
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
	execlists->queue_priority = rb ? to_priolist(rb)->priority : INT_MIN;
	if (submit)
		port_assign(port, last);
	if (last)
		execlists_user_begin(execlists, execlists->port);

	/* We must always keep the beast fed if we have work piled up */
	GEM_BUG_ON(port_isset(execlists->port) &&
		   !execlists_is_active(execlists, EXECLISTS_ACTIVE_USER));
	GEM_BUG_ON(rb_first_cached(&execlists->queue) &&
		   !port_isset(execlists->port));

	return submit;
}

static void guc_dequeue(struct intel_engine_cs *engine)
{
	unsigned long flags;
	bool submit;

	local_irq_save(flags);

	spin_lock(&engine->timeline.lock);
	submit = __guc_dequeue(engine);
	spin_unlock(&engine->timeline.lock);

	if (submit)
		guc_submit(engine);

	local_irq_restore(flags);
}

static void guc_submission_tasklet(unsigned long data)
{
	struct intel_engine_cs * const engine = (struct intel_engine_cs *)data;
	struct intel_engine_execlists * const execlists = &engine->execlists;
	struct execlist_port *port = execlists->port;
	struct i915_request *rq;

	rq = port_request(port);
	while (rq && i915_request_completed(rq)) {
		trace_i915_request_out(rq);
		i915_request_put(rq);

		port = execlists_port_complete(execlists, port);
		if (port_isset(port)) {
			execlists_user_begin(execlists, port);
			rq = port_request(port);
		} else {
			execlists_user_end(execlists);
			rq = NULL;
		}
	}

	if (execlists_is_active(execlists, EXECLISTS_ACTIVE_PREEMPT) &&
	    intel_read_status_page(engine, I915_GEM_HWS_PREEMPT_INDEX) ==
	    GUC_PREEMPT_FINISHED)
		complete_preempt_context(engine);

	if (!execlists_is_active(execlists, EXECLISTS_ACTIVE_PREEMPT))
		guc_dequeue(engine);
}

static struct i915_request *
guc_reset_prepare(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;

	GEM_TRACE("%s\n", engine->name);

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

	/*
	 * We're using worker to queue preemption requests from the tasklet in
	 * GuC submission mode.
	 * Even though tasklet was disabled, we may still have a worker queued.
	 * Let's make sure that all workers scheduled before disabling the
	 * tasklet are completed before continuing with the reset.
	 */
	if (engine->i915->guc.preempt_wq)
		flush_workqueue(engine->i915->guc.preempt_wq);

	return i915_gem_find_active_request(engine);
}

/*
 * Everything below here is concerned with setup & teardown, and is
 * therefore not part of the somewhat time-critical batch-submission
 * path of guc_submit() above.
 */

/* Check that a doorbell register is in the expected state */
static bool doorbell_ok(struct intel_guc *guc, u16 db_id)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	u32 drbregl;
	bool valid;

	GEM_BUG_ON(db_id >= GUC_DOORBELL_INVALID);

	drbregl = I915_READ(GEN8_DRBREGL(db_id));
	valid = drbregl & GEN8_DRB_VALID;

	if (test_bit(db_id, guc->doorbell_bitmap) == valid)
		return true;

	DRM_DEBUG_DRIVER("Doorbell %d has unexpected state (0x%x): valid=%s\n",
			 db_id, drbregl, yesno(valid));

	return false;
}

static bool guc_verify_doorbells(struct intel_guc *guc)
{
	u16 db_id;

	for (db_id = 0; db_id < GUC_NUM_DOORBELLS; ++db_id)
		if (!doorbell_ok(guc, db_id))
			return false;

	return true;
}

static int guc_clients_doorbell_init(struct intel_guc *guc)
{
	int ret;

	ret = create_doorbell(guc->execbuf_client);
	if (ret)
		return ret;

	if (guc->preempt_client) {
		ret = create_doorbell(guc->preempt_client);
		if (ret) {
			destroy_doorbell(guc->execbuf_client);
			return ret;
		}
	}

	return 0;
}

static void guc_clients_doorbell_fini(struct intel_guc *guc)
{
	/*
	 * By the time we're here, GuC has already been reset.
	 * Instead of trying (in vain) to communicate with it, let's just
	 * cleanup the doorbell HW and our internal state.
	 */
	if (guc->preempt_client) {
		__destroy_doorbell(guc->preempt_client);
		__update_doorbell_desc(guc->preempt_client,
				       GUC_DOORBELL_INVALID);
	}

	if (guc->execbuf_client) {
		__destroy_doorbell(guc->execbuf_client);
		__update_doorbell_desc(guc->execbuf_client,
				       GUC_DOORBELL_INVALID);
	}
}

/**
 * guc_client_alloc() - Allocate an intel_guc_client
 * @dev_priv:	driver private data structure
 * @engines:	The set of engines to enable for this client
 * @priority:	four levels priority _CRITICAL, _HIGH, _NORMAL and _LOW
 *		The kernel client to replace ExecList submission is created with
 *		NORMAL priority. Priority of a client for scheduler can be HIGH,
 *		while a preemption context can use CRITICAL.
 * @ctx:	the context that owns the client (we use the default render
 *		context)
 *
 * Return:	An intel_guc_client object if success, else NULL.
 */
static struct intel_guc_client *
guc_client_alloc(struct drm_i915_private *dev_priv,
		 u32 engines,
		 u32 priority,
		 struct i915_gem_context *ctx)
{
	struct intel_guc_client *client;
	struct intel_guc *guc = &dev_priv->guc;
	struct i915_vma *vma;
	void *vaddr;
	int ret;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return ERR_PTR(-ENOMEM);

	client->guc = guc;
	client->owner = ctx;
	client->engines = engines;
	client->priority = priority;
	client->doorbell_id = GUC_DOORBELL_INVALID;
	spin_lock_init(&client->wq_lock);

	ret = ida_simple_get(&guc->stage_ids, 0, GUC_MAX_STAGE_DESCRIPTORS,
			     GFP_KERNEL);
	if (ret < 0)
		goto err_client;

	client->stage_id = ret;

	/* The first page is doorbell/proc_desc. Two followed pages are wq. */
	vma = intel_guc_allocate_vma(guc, GUC_DB_SIZE + GUC_WQ_SIZE);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto err_id;
	}

	/* We'll keep just the first (doorbell/proc) page permanently kmap'd. */
	client->vma = vma;

	vaddr = i915_gem_object_pin_map(vma->obj, I915_MAP_WB);
	if (IS_ERR(vaddr)) {
		ret = PTR_ERR(vaddr);
		goto err_vma;
	}
	client->vaddr = vaddr;

	client->doorbell_offset = __select_cacheline(guc);

	/*
	 * Since the doorbell only requires a single cacheline, we can save
	 * space by putting the application process descriptor in the same
	 * page. Use the half of the page that doesn't include the doorbell.
	 */
	if (client->doorbell_offset >= (GUC_DB_SIZE / 2))
		client->proc_desc_offset = 0;
	else
		client->proc_desc_offset = (GUC_DB_SIZE / 2);

	guc_proc_desc_init(guc, client);
	guc_stage_desc_init(guc, client);

	ret = reserve_doorbell(client);
	if (ret)
		goto err_vaddr;

	DRM_DEBUG_DRIVER("new priority %u client %p for engine(s) 0x%x: stage_id %u\n",
			 priority, client, client->engines, client->stage_id);
	DRM_DEBUG_DRIVER("doorbell id %u, cacheline offset 0x%lx\n",
			 client->doorbell_id, client->doorbell_offset);

	return client;

err_vaddr:
	i915_gem_object_unpin_map(client->vma->obj);
err_vma:
	i915_vma_unpin_and_release(&client->vma);
err_id:
	ida_simple_remove(&guc->stage_ids, client->stage_id);
err_client:
	kfree(client);
	return ERR_PTR(ret);
}

static void guc_client_free(struct intel_guc_client *client)
{
	unreserve_doorbell(client);
	guc_stage_desc_fini(client->guc, client);
	i915_gem_object_unpin_map(client->vma->obj);
	i915_vma_unpin_and_release(&client->vma);
	ida_simple_remove(&client->guc->stage_ids, client->stage_id);
	kfree(client);
}

static inline bool ctx_save_restore_disabled(struct intel_context *ce)
{
	u32 sr = ce->lrc_reg_state[CTX_CONTEXT_CONTROL + 1];

#define SR_DISABLED \
	_MASKED_BIT_ENABLE(CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT | \
			   CTX_CTRL_ENGINE_CTX_SAVE_INHIBIT)

	return (sr & SR_DISABLED) == SR_DISABLED;

#undef SR_DISABLED
}

static void guc_fill_preempt_context(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	struct intel_guc_client *client = guc->preempt_client;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, dev_priv, id) {
		struct intel_context *ce =
			to_intel_context(client->owner, engine);
		u32 addr = intel_hws_preempt_done_address(engine);
		u32 *cs;

		GEM_BUG_ON(!ce->pin_count);

		/*
		 * We rely on this context image *not* being saved after
		 * preemption. This ensures that the RING_HEAD / RING_TAIL
		 * remain pointing at initial values forever.
		 */
		GEM_BUG_ON(!ctx_save_restore_disabled(ce));

		cs = ce->ring->vaddr;
		if (id == RCS) {
			cs = gen8_emit_ggtt_write_rcs(cs,
						      GUC_PREEMPT_FINISHED,
						      addr);
		} else {
			cs = gen8_emit_ggtt_write(cs,
						  GUC_PREEMPT_FINISHED,
						  addr);
			*cs++ = MI_NOOP;
			*cs++ = MI_NOOP;
		}
		*cs++ = MI_USER_INTERRUPT;
		*cs++ = MI_NOOP;

		GEM_BUG_ON((void *)cs - ce->ring->vaddr !=
			   GUC_PREEMPT_BREADCRUMB_BYTES);

		flush_ggtt_writes(ce->ring->vma);
	}
}

static int guc_clients_create(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	struct intel_guc_client *client;

	GEM_BUG_ON(guc->execbuf_client);
	GEM_BUG_ON(guc->preempt_client);

	client = guc_client_alloc(dev_priv,
				  INTEL_INFO(dev_priv)->ring_mask,
				  GUC_CLIENT_PRIORITY_KMD_NORMAL,
				  dev_priv->kernel_context);
	if (IS_ERR(client)) {
		DRM_ERROR("Failed to create GuC client for submission!\n");
		return PTR_ERR(client);
	}
	guc->execbuf_client = client;

	if (dev_priv->preempt_context) {
		client = guc_client_alloc(dev_priv,
					  INTEL_INFO(dev_priv)->ring_mask,
					  GUC_CLIENT_PRIORITY_KMD_HIGH,
					  dev_priv->preempt_context);
		if (IS_ERR(client)) {
			DRM_ERROR("Failed to create GuC client for preemption!\n");
			guc_client_free(guc->execbuf_client);
			guc->execbuf_client = NULL;
			return PTR_ERR(client);
		}
		guc->preempt_client = client;

		guc_fill_preempt_context(guc);
	}

	return 0;
}

static void guc_clients_destroy(struct intel_guc *guc)
{
	struct intel_guc_client *client;

	client = fetch_and_zero(&guc->preempt_client);
	if (client)
		guc_client_free(client);

	client = fetch_and_zero(&guc->execbuf_client);
	if (client)
		guc_client_free(client);
}

/*
 * Set up the memory resources to be shared with the GuC (via the GGTT)
 * at firmware loading time.
 */
int intel_guc_submission_init(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
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

	WARN_ON(!guc_verify_doorbells(guc));
	ret = guc_clients_create(guc);
	if (ret)
		goto err_pool;

	for_each_engine(engine, dev_priv, id) {
		guc->preempt_work[id].engine = engine;
		INIT_WORK(&guc->preempt_work[id].work, inject_preempt_context);
	}

	return 0;

err_pool:
	guc_stage_desc_pool_destroy(guc);
	return ret;
}

void intel_guc_submission_fini(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, dev_priv, id)
		cancel_work_sync(&guc->preempt_work[id].work);

	guc_clients_destroy(guc);
	WARN_ON(!guc_verify_doorbells(guc));

	if (guc->stage_desc_pool)
		guc_stage_desc_pool_destroy(guc);
}

static void guc_interrupts_capture(struct drm_i915_private *dev_priv)
{
	struct intel_rps *rps = &dev_priv->gt_pm.rps;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int irqs;

	/* tell all command streamers to forward interrupts (but not vblank)
	 * to GuC
	 */
	irqs = _MASKED_BIT_ENABLE(GFX_INTERRUPT_STEERING);
	for_each_engine(engine, dev_priv, id)
		I915_WRITE(RING_MODE_GEN7(engine), irqs);

	/* route USER_INTERRUPT to Host, all others are sent to GuC. */
	irqs = GT_RENDER_USER_INTERRUPT << GEN8_RCS_IRQ_SHIFT |
	       GT_RENDER_USER_INTERRUPT << GEN8_BCS_IRQ_SHIFT;
	/* These three registers have the same bit definitions */
	I915_WRITE(GUC_BCS_RCS_IER, ~irqs);
	I915_WRITE(GUC_VCS2_VCS1_IER, ~irqs);
	I915_WRITE(GUC_WD_VECS_IER, ~irqs);

	/*
	 * The REDIRECT_TO_GUC bit of the PMINTRMSK register directs all
	 * (unmasked) PM interrupts to the GuC. All other bits of this
	 * register *disable* generation of a specific interrupt.
	 *
	 * 'pm_intrmsk_mbz' indicates bits that are NOT to be set when
	 * writing to the PM interrupt mask register, i.e. interrupts
	 * that must not be disabled.
	 *
	 * If the GuC is handling these interrupts, then we must not let
	 * the PM code disable ANY interrupt that the GuC is expecting.
	 * So for each ENABLED (0) bit in this register, we must SET the
	 * bit in pm_intrmsk_mbz so that it's left enabled for the GuC.
	 * GuC needs ARAT expired interrupt unmasked hence it is set in
	 * pm_intrmsk_mbz.
	 *
	 * Here we CLEAR REDIRECT_TO_GUC bit in pm_intrmsk_mbz, which will
	 * result in the register bit being left SET!
	 */
	rps->pm_intrmsk_mbz |= ARAT_EXPIRED_INTRMSK;
	rps->pm_intrmsk_mbz &= ~GEN8_PMINTR_DISABLE_REDIRECT_TO_GUC;
}

static void guc_interrupts_release(struct drm_i915_private *dev_priv)
{
	struct intel_rps *rps = &dev_priv->gt_pm.rps;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int irqs;

	/*
	 * tell all command streamers NOT to forward interrupts or vblank
	 * to GuC.
	 */
	irqs = _MASKED_FIELD(GFX_FORWARD_VBLANK_MASK, GFX_FORWARD_VBLANK_NEVER);
	irqs |= _MASKED_BIT_DISABLE(GFX_INTERRUPT_STEERING);
	for_each_engine(engine, dev_priv, id)
		I915_WRITE(RING_MODE_GEN7(engine), irqs);

	/* route all GT interrupts to the host */
	I915_WRITE(GUC_BCS_RCS_IER, 0);
	I915_WRITE(GUC_VCS2_VCS1_IER, 0);
	I915_WRITE(GUC_WD_VECS_IER, 0);

	rps->pm_intrmsk_mbz |= GEN8_PMINTR_DISABLE_REDIRECT_TO_GUC;
	rps->pm_intrmsk_mbz &= ~ARAT_EXPIRED_INTRMSK;
}

static void guc_submission_park(struct intel_engine_cs *engine)
{
	intel_engine_unpin_breadcrumbs_irq(engine);
}

static void guc_submission_unpark(struct intel_engine_cs *engine)
{
	intel_engine_pin_breadcrumbs_irq(engine);
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

	engine->park = guc_submission_park;
	engine->unpark = guc_submission_unpark;

	engine->reset.prepare = guc_reset_prepare;

	engine->flags &= ~I915_ENGINE_SUPPORTS_STATS;
}

int intel_guc_submission_enable(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err;

	/*
	 * We're using GuC work items for submitting work through GuC. Since
	 * we're coalescing multiple requests from a single context into a
	 * single work item prior to assigning it to execlist_port, we can
	 * never have more work items than the total number of ports (for all
	 * engines). The GuC firmware is controlling the HEAD of work queue,
	 * and it is guaranteed that it will remove the work item from the
	 * queue before our request is completed.
	 */
	BUILD_BUG_ON(ARRAY_SIZE(engine->execlists.port) *
		     sizeof(struct guc_wq_item) *
		     I915_NUM_ENGINES > GUC_WQ_SIZE);

	GEM_BUG_ON(!guc->execbuf_client);

	guc_reset_wq(guc->execbuf_client);
	if (guc->preempt_client)
		guc_reset_wq(guc->preempt_client);

	err = intel_guc_sample_forcewake(guc);
	if (err)
		return err;

	err = guc_clients_doorbell_init(guc);
	if (err)
		return err;

	/* Take over from manual control of ELSP (execlists) */
	guc_interrupts_capture(dev_priv);

	for_each_engine(engine, dev_priv, id) {
		engine->set_default_submission = guc_set_default_submission;
		engine->set_default_submission(engine);
	}

	return 0;
}

void intel_guc_submission_disable(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);

	GEM_BUG_ON(dev_priv->gt.awake); /* GT should be parked first */

	guc_interrupts_release(dev_priv);
	guc_clients_doorbell_fini(guc);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/intel_guc.c"
#endif
