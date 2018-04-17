/*
 * Copyright(c) 2011-2016 Intel Corporation. All rights reserved.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Zhiyuan Lv <zhiyuan.lv@intel.com>
 *    Zhi Wang <zhi.a.wang@intel.com>
 *
 * Contributors:
 *    Min He <min.he@intel.com>
 *    Bing Niu <bing.niu@intel.com>
 *    Ping Gao <ping.a.gao@intel.com>
 *    Tina Zhang <tina.zhang@intel.com>
 *
 */

#include "i915_drv.h"
#include "gvt.h"

#define _EL_OFFSET_STATUS       0x234
#define _EL_OFFSET_STATUS_BUF   0x370
#define _EL_OFFSET_STATUS_PTR   0x3A0

#define execlist_ring_mmio(gvt, ring_id, offset) \
	(gvt->dev_priv->engine[ring_id]->mmio_base + (offset))

#define valid_context(ctx) ((ctx)->valid)
#define same_context(a, b) (((a)->context_id == (b)->context_id) && \
		((a)->lrca == (b)->lrca))

static void clean_workloads(struct intel_vgpu *vgpu, unsigned long engine_mask);

static int context_switch_events[] = {
	[RCS] = RCS_AS_CONTEXT_SWITCH,
	[BCS] = BCS_AS_CONTEXT_SWITCH,
	[VCS] = VCS_AS_CONTEXT_SWITCH,
	[VCS2] = VCS2_AS_CONTEXT_SWITCH,
	[VECS] = VECS_AS_CONTEXT_SWITCH,
};

static int ring_id_to_context_switch_event(int ring_id)
{
	if (WARN_ON(ring_id < RCS ||
		    ring_id >= ARRAY_SIZE(context_switch_events)))
		return -EINVAL;

	return context_switch_events[ring_id];
}

static void switch_virtual_execlist_slot(struct intel_vgpu_execlist *execlist)
{
	gvt_dbg_el("[before] running slot %d/context %x pending slot %d\n",
			execlist->running_slot ?
			execlist->running_slot->index : -1,
			execlist->running_context ?
			execlist->running_context->context_id : 0,
			execlist->pending_slot ?
			execlist->pending_slot->index : -1);

	execlist->running_slot = execlist->pending_slot;
	execlist->pending_slot = NULL;
	execlist->running_context = execlist->running_context ?
		&execlist->running_slot->ctx[0] : NULL;

	gvt_dbg_el("[after] running slot %d/context %x pending slot %d\n",
			execlist->running_slot ?
			execlist->running_slot->index : -1,
			execlist->running_context ?
			execlist->running_context->context_id : 0,
			execlist->pending_slot ?
			execlist->pending_slot->index : -1);
}

static void emulate_execlist_status(struct intel_vgpu_execlist *execlist)
{
	struct intel_vgpu_execlist_slot *running = execlist->running_slot;
	struct intel_vgpu_execlist_slot *pending = execlist->pending_slot;
	struct execlist_ctx_descriptor_format *desc = execlist->running_context;
	struct intel_vgpu *vgpu = execlist->vgpu;
	struct execlist_status_format status;
	int ring_id = execlist->ring_id;
	u32 status_reg = execlist_ring_mmio(vgpu->gvt,
			ring_id, _EL_OFFSET_STATUS);

	status.ldw = vgpu_vreg(vgpu, status_reg);
	status.udw = vgpu_vreg(vgpu, status_reg + 4);

	if (running) {
		status.current_execlist_pointer = !!running->index;
		status.execlist_write_pointer = !!!running->index;
		status.execlist_0_active = status.execlist_0_valid =
			!!!(running->index);
		status.execlist_1_active = status.execlist_1_valid =
			!!(running->index);
	} else {
		status.context_id = 0;
		status.execlist_0_active = status.execlist_0_valid = 0;
		status.execlist_1_active = status.execlist_1_valid = 0;
	}

	status.context_id = desc ? desc->context_id : 0;
	status.execlist_queue_full = !!(pending);

	vgpu_vreg(vgpu, status_reg) = status.ldw;
	vgpu_vreg(vgpu, status_reg + 4) = status.udw;

	gvt_dbg_el("vgpu%d: status reg offset %x ldw %x udw %x\n",
		vgpu->id, status_reg, status.ldw, status.udw);
}

static void emulate_csb_update(struct intel_vgpu_execlist *execlist,
		struct execlist_context_status_format *status,
		bool trigger_interrupt_later)
{
	struct intel_vgpu *vgpu = execlist->vgpu;
	int ring_id = execlist->ring_id;
	struct execlist_context_status_pointer_format ctx_status_ptr;
	u32 write_pointer;
	u32 ctx_status_ptr_reg, ctx_status_buf_reg, offset;

	ctx_status_ptr_reg = execlist_ring_mmio(vgpu->gvt, ring_id,
			_EL_OFFSET_STATUS_PTR);
	ctx_status_buf_reg = execlist_ring_mmio(vgpu->gvt, ring_id,
			_EL_OFFSET_STATUS_BUF);

	ctx_status_ptr.dw = vgpu_vreg(vgpu, ctx_status_ptr_reg);

	write_pointer = ctx_status_ptr.write_ptr;

	if (write_pointer == 0x7)
		write_pointer = 0;
	else {
		++write_pointer;
		write_pointer %= 0x6;
	}

	offset = ctx_status_buf_reg + write_pointer * 8;

	vgpu_vreg(vgpu, offset) = status->ldw;
	vgpu_vreg(vgpu, offset + 4) = status->udw;

	ctx_status_ptr.write_ptr = write_pointer;
	vgpu_vreg(vgpu, ctx_status_ptr_reg) = ctx_status_ptr.dw;

	gvt_dbg_el("vgpu%d: w pointer %u reg %x csb l %x csb h %x\n",
		vgpu->id, write_pointer, offset, status->ldw, status->udw);

	if (trigger_interrupt_later)
		return;

	intel_vgpu_trigger_virtual_event(vgpu,
			ring_id_to_context_switch_event(execlist->ring_id));
}

static int emulate_execlist_ctx_schedule_out(
		struct intel_vgpu_execlist *execlist,
		struct execlist_ctx_descriptor_format *ctx)
{
	struct intel_vgpu *vgpu = execlist->vgpu;
	struct intel_vgpu_execlist_slot *running = execlist->running_slot;
	struct intel_vgpu_execlist_slot *pending = execlist->pending_slot;
	struct execlist_ctx_descriptor_format *ctx0 = &running->ctx[0];
	struct execlist_ctx_descriptor_format *ctx1 = &running->ctx[1];
	struct execlist_context_status_format status;

	memset(&status, 0, sizeof(status));

	gvt_dbg_el("schedule out context id %x\n", ctx->context_id);

	if (WARN_ON(!same_context(ctx, execlist->running_context))) {
		gvt_vgpu_err("schedule out context is not running context,"
				"ctx id %x running ctx id %x\n",
				ctx->context_id,
				execlist->running_context->context_id);
		return -EINVAL;
	}

	/* ctx1 is valid, ctx0/ctx is scheduled-out -> element switch */
	if (valid_context(ctx1) && same_context(ctx0, ctx)) {
		gvt_dbg_el("ctx 1 valid, ctx/ctx 0 is scheduled-out\n");

		execlist->running_context = ctx1;

		emulate_execlist_status(execlist);

		status.context_complete = status.element_switch = 1;
		status.context_id = ctx->context_id;

		emulate_csb_update(execlist, &status, false);
		/*
		 * ctx1 is not valid, ctx == ctx0
		 * ctx1 is valid, ctx1 == ctx
		 *	--> last element is finished
		 * emulate:
		 *	active-to-idle if there is *no* pending execlist
		 *	context-complete if there *is* pending execlist
		 */
	} else if ((!valid_context(ctx1) && same_context(ctx0, ctx))
			|| (valid_context(ctx1) && same_context(ctx1, ctx))) {
		gvt_dbg_el("need to switch virtual execlist slot\n");

		switch_virtual_execlist_slot(execlist);

		emulate_execlist_status(execlist);

		status.context_complete = status.active_to_idle = 1;
		status.context_id = ctx->context_id;

		if (!pending) {
			emulate_csb_update(execlist, &status, false);
		} else {
			emulate_csb_update(execlist, &status, true);

			memset(&status, 0, sizeof(status));

			status.idle_to_active = 1;
			status.context_id = 0;

			emulate_csb_update(execlist, &status, false);
		}
	} else {
		WARN_ON(1);
		return -EINVAL;
	}

	return 0;
}

static struct intel_vgpu_execlist_slot *get_next_execlist_slot(
		struct intel_vgpu_execlist *execlist)
{
	struct intel_vgpu *vgpu = execlist->vgpu;
	int ring_id = execlist->ring_id;
	u32 status_reg = execlist_ring_mmio(vgpu->gvt, ring_id,
			_EL_OFFSET_STATUS);
	struct execlist_status_format status;

	status.ldw = vgpu_vreg(vgpu, status_reg);
	status.udw = vgpu_vreg(vgpu, status_reg + 4);

	if (status.execlist_queue_full) {
		gvt_vgpu_err("virtual execlist slots are full\n");
		return NULL;
	}

	return &execlist->slot[status.execlist_write_pointer];
}

static int emulate_execlist_schedule_in(struct intel_vgpu_execlist *execlist,
		struct execlist_ctx_descriptor_format ctx[2])
{
	struct intel_vgpu_execlist_slot *running = execlist->running_slot;
	struct intel_vgpu_execlist_slot *slot =
		get_next_execlist_slot(execlist);

	struct execlist_ctx_descriptor_format *ctx0, *ctx1;
	struct execlist_context_status_format status;
	struct intel_vgpu *vgpu = execlist->vgpu;

	gvt_dbg_el("emulate schedule-in\n");

	if (!slot) {
		gvt_vgpu_err("no available execlist slot\n");
		return -EINVAL;
	}

	memset(&status, 0, sizeof(status));
	memset(slot->ctx, 0, sizeof(slot->ctx));

	slot->ctx[0] = ctx[0];
	slot->ctx[1] = ctx[1];

	gvt_dbg_el("alloc slot index %d ctx 0 %x ctx 1 %x\n",
			slot->index, ctx[0].context_id,
			ctx[1].context_id);

	/*
	 * no running execlist, make this write bundle as running execlist
	 * -> idle-to-active
	 */
	if (!running) {
		gvt_dbg_el("no current running execlist\n");

		execlist->running_slot = slot;
		execlist->pending_slot = NULL;
		execlist->running_context = &slot->ctx[0];

		gvt_dbg_el("running slot index %d running context %x\n",
				execlist->running_slot->index,
				execlist->running_context->context_id);

		emulate_execlist_status(execlist);

		status.idle_to_active = 1;
		status.context_id = 0;

		emulate_csb_update(execlist, &status, false);
		return 0;
	}

	ctx0 = &running->ctx[0];
	ctx1 = &running->ctx[1];

	gvt_dbg_el("current running slot index %d ctx 0 %x ctx 1 %x\n",
		running->index, ctx0->context_id, ctx1->context_id);

	/*
	 * already has an running execlist
	 *	a. running ctx1 is valid,
	 *	   ctx0 is finished, and running ctx1 == new execlist ctx[0]
	 *	b. running ctx1 is not valid,
	 *	   ctx0 == new execlist ctx[0]
	 * ----> lite-restore + preempted
	 */
	if ((valid_context(ctx1) && same_context(ctx1, &slot->ctx[0]) &&
		/* condition a */
		(!same_context(ctx0, execlist->running_context))) ||
			(!valid_context(ctx1) &&
			 same_context(ctx0, &slot->ctx[0]))) { /* condition b */
		gvt_dbg_el("need to switch virtual execlist slot\n");

		execlist->pending_slot = slot;
		switch_virtual_execlist_slot(execlist);

		emulate_execlist_status(execlist);

		status.lite_restore = status.preempted = 1;
		status.context_id = ctx[0].context_id;

		emulate_csb_update(execlist, &status, false);
	} else {
		gvt_dbg_el("emulate as pending slot\n");
		/*
		 * otherwise
		 * --> emulate pending execlist exist + but no preemption case
		 */
		execlist->pending_slot = slot;
		emulate_execlist_status(execlist);
	}
	return 0;
}

static void free_workload(struct intel_vgpu_workload *workload)
{
	intel_vgpu_unpin_mm(workload->shadow_mm);
	intel_gvt_mm_unreference(workload->shadow_mm);
	kmem_cache_free(workload->vgpu->workloads, workload);
}

#define get_desc_from_elsp_dwords(ed, i) \
	((struct execlist_ctx_descriptor_format *)&((ed)->data[i * 2]))

static int prepare_shadow_batch_buffer(struct intel_vgpu_workload *workload)
{
	const int gmadr_bytes = workload->vgpu->gvt->device_info.gmadr_bytes_in_cmd;
	struct intel_shadow_bb_entry *entry_obj;

	/* pin the gem object to ggtt */
	list_for_each_entry(entry_obj, &workload->shadow_bb, list) {
		struct i915_vma *vma;

		vma = i915_gem_object_ggtt_pin(entry_obj->obj, NULL, 0, 4, 0);
		if (IS_ERR(vma)) {
			return PTR_ERR(vma);
		}

		/* FIXME: we are not tracking our pinned VMA leaving it
		 * up to the core to fix up the stray pin_count upon
		 * free.
		 */

		/* update the relocate gma with shadow batch buffer*/
		entry_obj->bb_start_cmd_va[1] = i915_ggtt_offset(vma);
		if (gmadr_bytes == 8)
			entry_obj->bb_start_cmd_va[2] = 0;
	}
	return 0;
}

static int update_wa_ctx_2_shadow_ctx(struct intel_shadow_wa_ctx *wa_ctx)
{
	struct intel_vgpu_workload *workload = container_of(wa_ctx,
					struct intel_vgpu_workload,
					wa_ctx);
	int ring_id = workload->ring_id;
	struct i915_gem_context *shadow_ctx = workload->vgpu->shadow_ctx;
	struct drm_i915_gem_object *ctx_obj =
		shadow_ctx->engine[ring_id].state->obj;
	struct execlist_ring_context *shadow_ring_context;
	struct page *page;

	page = i915_gem_object_get_page(ctx_obj, LRC_STATE_PN);
	shadow_ring_context = kmap_atomic(page);

	shadow_ring_context->bb_per_ctx_ptr.val =
		(shadow_ring_context->bb_per_ctx_ptr.val &
		(~PER_CTX_ADDR_MASK)) | wa_ctx->per_ctx.shadow_gma;
	shadow_ring_context->rcs_indirect_ctx.val =
		(shadow_ring_context->rcs_indirect_ctx.val &
		(~INDIRECT_CTX_ADDR_MASK)) | wa_ctx->indirect_ctx.shadow_gma;

	kunmap_atomic(shadow_ring_context);
	return 0;
}

static int prepare_shadow_wa_ctx(struct intel_shadow_wa_ctx *wa_ctx)
{
	struct i915_vma *vma;
	unsigned char *per_ctx_va =
		(unsigned char *)wa_ctx->indirect_ctx.shadow_va +
		wa_ctx->indirect_ctx.size;

	if (wa_ctx->indirect_ctx.size == 0)
		return 0;

	vma = i915_gem_object_ggtt_pin(wa_ctx->indirect_ctx.obj, NULL,
				       0, CACHELINE_BYTES, 0);
	if (IS_ERR(vma)) {
		return PTR_ERR(vma);
	}

	/* FIXME: we are not tracking our pinned VMA leaving it
	 * up to the core to fix up the stray pin_count upon
	 * free.
	 */

	wa_ctx->indirect_ctx.shadow_gma = i915_ggtt_offset(vma);

	wa_ctx->per_ctx.shadow_gma = *((unsigned int *)per_ctx_va + 1);
	memset(per_ctx_va, 0, CACHELINE_BYTES);

	update_wa_ctx_2_shadow_ctx(wa_ctx);
	return 0;
}

static void release_shadow_batch_buffer(struct intel_vgpu_workload *workload)
{
	/* release all the shadow batch buffer */
	if (!list_empty(&workload->shadow_bb)) {
		struct intel_shadow_bb_entry *entry_obj =
			list_first_entry(&workload->shadow_bb,
					 struct intel_shadow_bb_entry,
					 list);
		struct intel_shadow_bb_entry *temp;

		list_for_each_entry_safe(entry_obj, temp, &workload->shadow_bb,
					 list) {
			i915_gem_object_unpin_map(entry_obj->obj);
			i915_gem_object_put(entry_obj->obj);
			list_del(&entry_obj->list);
			kfree(entry_obj);
		}
	}
}

static int prepare_execlist_workload(struct intel_vgpu_workload *workload)
{
	struct intel_vgpu *vgpu = workload->vgpu;
	struct execlist_ctx_descriptor_format ctx[2];
	int ring_id = workload->ring_id;
	int ret;

	ret = intel_vgpu_pin_mm(workload->shadow_mm);
	if (ret) {
		gvt_vgpu_err("fail to vgpu pin mm\n");
		goto out;
	}

	ret = intel_vgpu_sync_oos_pages(workload->vgpu);
	if (ret) {
		gvt_vgpu_err("fail to vgpu sync oos pages\n");
		goto err_unpin_mm;
	}

	ret = intel_vgpu_flush_post_shadow(workload->vgpu);
	if (ret) {
		gvt_vgpu_err("fail to flush post shadow\n");
		goto err_unpin_mm;
	}

	ret = intel_gvt_generate_request(workload);
	if (ret) {
		gvt_vgpu_err("fail to generate request\n");
		goto err_unpin_mm;
	}

	ret = prepare_shadow_batch_buffer(workload);
	if (ret) {
		gvt_vgpu_err("fail to prepare_shadow_batch_buffer\n");
		goto err_unpin_mm;
	}

	ret = prepare_shadow_wa_ctx(&workload->wa_ctx);
	if (ret) {
		gvt_vgpu_err("fail to prepare_shadow_wa_ctx\n");
		goto err_shadow_batch;
	}

	if (!workload->emulate_schedule_in)
		return 0;

	ctx[0] = *get_desc_from_elsp_dwords(&workload->elsp_dwords, 1);
	ctx[1] = *get_desc_from_elsp_dwords(&workload->elsp_dwords, 0);

	ret = emulate_execlist_schedule_in(&vgpu->execlist[ring_id], ctx);
	if (!ret)
		goto out;
	else
		gvt_vgpu_err("fail to emulate execlist schedule in\n");

	release_shadow_wa_ctx(&workload->wa_ctx);
err_shadow_batch:
	release_shadow_batch_buffer(workload);
err_unpin_mm:
	intel_vgpu_unpin_mm(workload->shadow_mm);
out:
	return ret;
}

static int complete_execlist_workload(struct intel_vgpu_workload *workload)
{
	struct intel_vgpu *vgpu = workload->vgpu;
	int ring_id = workload->ring_id;
	struct intel_vgpu_execlist *execlist = &vgpu->execlist[ring_id];
	struct intel_vgpu_workload *next_workload;
	struct list_head *next = workload_q_head(vgpu, ring_id)->next;
	bool lite_restore = false;
	int ret;

	gvt_dbg_el("complete workload %p status %d\n", workload,
			workload->status);

	if (!workload->status) {
		release_shadow_batch_buffer(workload);
		release_shadow_wa_ctx(&workload->wa_ctx);
	}

	if (workload->status || (vgpu->resetting_eng & ENGINE_MASK(ring_id))) {
		/* if workload->status is not successful means HW GPU
		 * has occurred GPU hang or something wrong with i915/GVT,
		 * and GVT won't inject context switch interrupt to guest.
		 * So this error is a vGPU hang actually to the guest.
		 * According to this we should emunlate a vGPU hang. If
		 * there are pending workloads which are already submitted
		 * from guest, we should clean them up like HW GPU does.
		 *
		 * if it is in middle of engine resetting, the pending
		 * workloads won't be submitted to HW GPU and will be
		 * cleaned up during the resetting process later, so doing
		 * the workload clean up here doesn't have any impact.
		 **/
		clean_workloads(vgpu, ENGINE_MASK(ring_id));
		goto out;
	}

	if (!list_empty(workload_q_head(vgpu, ring_id))) {
		struct execlist_ctx_descriptor_format *this_desc, *next_desc;

		next_workload = container_of(next,
				struct intel_vgpu_workload, list);
		this_desc = &workload->ctx_desc;
		next_desc = &next_workload->ctx_desc;

		lite_restore = same_context(this_desc, next_desc);
	}

	if (lite_restore) {
		gvt_dbg_el("next context == current - no schedule-out\n");
		free_workload(workload);
		return 0;
	}

	ret = emulate_execlist_ctx_schedule_out(execlist, &workload->ctx_desc);
	if (ret)
		goto err;
out:
	free_workload(workload);
	return 0;
err:
	free_workload(workload);
	return ret;
}

#define RING_CTX_OFF(x) \
	offsetof(struct execlist_ring_context, x)

static void read_guest_pdps(struct intel_vgpu *vgpu,
		u64 ring_context_gpa, u32 pdp[8])
{
	u64 gpa;
	int i;

	gpa = ring_context_gpa + RING_CTX_OFF(pdp3_UDW.val);

	for (i = 0; i < 8; i++)
		intel_gvt_hypervisor_read_gpa(vgpu,
				gpa + i * 8, &pdp[7 - i], 4);
}

static int prepare_mm(struct intel_vgpu_workload *workload)
{
	struct execlist_ctx_descriptor_format *desc = &workload->ctx_desc;
	struct intel_vgpu_mm *mm;
	struct intel_vgpu *vgpu = workload->vgpu;
	int page_table_level;
	u32 pdp[8];

	if (desc->addressing_mode == 1) { /* legacy 32-bit */
		page_table_level = 3;
	} else if (desc->addressing_mode == 3) { /* legacy 64 bit */
		page_table_level = 4;
	} else {
		gvt_vgpu_err("Advanced Context mode(SVM) is not supported!\n");
		return -EINVAL;
	}

	read_guest_pdps(workload->vgpu, workload->ring_context_gpa, pdp);

	mm = intel_vgpu_find_ppgtt_mm(workload->vgpu, page_table_level, pdp);
	if (mm) {
		intel_gvt_mm_reference(mm);
	} else {

		mm = intel_vgpu_create_mm(workload->vgpu, INTEL_GVT_MM_PPGTT,
				pdp, page_table_level, 0);
		if (IS_ERR(mm)) {
			gvt_vgpu_err("fail to create mm object.\n");
			return PTR_ERR(mm);
		}
	}
	workload->shadow_mm = mm;
	return 0;
}

#define get_last_workload(q) \
	(list_empty(q) ? NULL : container_of(q->prev, \
	struct intel_vgpu_workload, list))

static int submit_context(struct intel_vgpu *vgpu, int ring_id,
		struct execlist_ctx_descriptor_format *desc,
		bool emulate_schedule_in)
{
	struct list_head *q = workload_q_head(vgpu, ring_id);
	struct intel_vgpu_workload *last_workload = get_last_workload(q);
	struct intel_vgpu_workload *workload = NULL;
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;
	u64 ring_context_gpa;
	u32 head, tail, start, ctl, ctx_ctl, per_ctx, indirect_ctx;
	int ret;

	ring_context_gpa = intel_vgpu_gma_to_gpa(vgpu->gtt.ggtt_mm,
			(u32)((desc->lrca + 1) << GTT_PAGE_SHIFT));
	if (ring_context_gpa == INTEL_GVT_INVALID_ADDR) {
		gvt_vgpu_err("invalid guest context LRCA: %x\n", desc->lrca);
		return -EINVAL;
	}

	intel_gvt_hypervisor_read_gpa(vgpu, ring_context_gpa +
			RING_CTX_OFF(ring_header.val), &head, 4);

	intel_gvt_hypervisor_read_gpa(vgpu, ring_context_gpa +
			RING_CTX_OFF(ring_tail.val), &tail, 4);

	head &= RB_HEAD_OFF_MASK;
	tail &= RB_TAIL_OFF_MASK;

	if (last_workload && same_context(&last_workload->ctx_desc, desc)) {
		gvt_dbg_el("ring id %d cur workload == last\n", ring_id);
		gvt_dbg_el("ctx head %x real head %lx\n", head,
				last_workload->rb_tail);
		/*
		 * cannot use guest context head pointer here,
		 * as it might not be updated at this time
		 */
		head = last_workload->rb_tail;
	}

	gvt_dbg_el("ring id %d begin a new workload\n", ring_id);

	workload = kmem_cache_zalloc(vgpu->workloads, GFP_KERNEL);
	if (!workload)
		return -ENOMEM;

	/* record some ring buffer register values for scan and shadow */
	intel_gvt_hypervisor_read_gpa(vgpu, ring_context_gpa +
			RING_CTX_OFF(rb_start.val), &start, 4);
	intel_gvt_hypervisor_read_gpa(vgpu, ring_context_gpa +
			RING_CTX_OFF(rb_ctrl.val), &ctl, 4);
	intel_gvt_hypervisor_read_gpa(vgpu, ring_context_gpa +
			RING_CTX_OFF(ctx_ctrl.val), &ctx_ctl, 4);

	INIT_LIST_HEAD(&workload->list);
	INIT_LIST_HEAD(&workload->shadow_bb);

	init_waitqueue_head(&workload->shadow_ctx_status_wq);
	atomic_set(&workload->shadow_ctx_active, 0);

	workload->vgpu = vgpu;
	workload->ring_id = ring_id;
	workload->ctx_desc = *desc;
	workload->ring_context_gpa = ring_context_gpa;
	workload->rb_head = head;
	workload->rb_tail = tail;
	workload->rb_start = start;
	workload->rb_ctl = ctl;
	workload->prepare = prepare_execlist_workload;
	workload->complete = complete_execlist_workload;
	workload->status = -EINPROGRESS;
	workload->emulate_schedule_in = emulate_schedule_in;
	workload->shadowed = false;

	if (ring_id == RCS) {
		intel_gvt_hypervisor_read_gpa(vgpu, ring_context_gpa +
			RING_CTX_OFF(bb_per_ctx_ptr.val), &per_ctx, 4);
		intel_gvt_hypervisor_read_gpa(vgpu, ring_context_gpa +
			RING_CTX_OFF(rcs_indirect_ctx.val), &indirect_ctx, 4);

		workload->wa_ctx.indirect_ctx.guest_gma =
			indirect_ctx & INDIRECT_CTX_ADDR_MASK;
		workload->wa_ctx.indirect_ctx.size =
			(indirect_ctx & INDIRECT_CTX_SIZE_MASK) *
			CACHELINE_BYTES;
		workload->wa_ctx.per_ctx.guest_gma =
			per_ctx & PER_CTX_ADDR_MASK;
		workload->wa_ctx.per_ctx.valid = per_ctx & 1;
	}

	if (emulate_schedule_in)
		workload->elsp_dwords = vgpu->execlist[ring_id].elsp_dwords;

	gvt_dbg_el("workload %p ring id %d head %x tail %x start %x ctl %x\n",
			workload, ring_id, head, tail, start, ctl);

	gvt_dbg_el("workload %p emulate schedule_in %d\n", workload,
			emulate_schedule_in);

	ret = prepare_mm(workload);
	if (ret) {
		kmem_cache_free(vgpu->workloads, workload);
		return ret;
	}

	/* Only scan and shadow the first workload in the queue
	 * as there is only one pre-allocated buf-obj for shadow.
	 */
	if (list_empty(workload_q_head(vgpu, ring_id))) {
		intel_runtime_pm_get(dev_priv);
		mutex_lock(&dev_priv->drm.struct_mutex);
		intel_gvt_scan_and_shadow_workload(workload);
		mutex_unlock(&dev_priv->drm.struct_mutex);
		intel_runtime_pm_put(dev_priv);
	}

	queue_workload(workload);
	return 0;
}

int intel_vgpu_submit_execlist(struct intel_vgpu *vgpu, int ring_id)
{
	struct intel_vgpu_execlist *execlist = &vgpu->execlist[ring_id];
	struct execlist_ctx_descriptor_format desc[2];
	int i, ret;

	desc[0] = *get_desc_from_elsp_dwords(&execlist->elsp_dwords, 1);
	desc[1] = *get_desc_from_elsp_dwords(&execlist->elsp_dwords, 0);

	if (!desc[0].valid) {
		gvt_vgpu_err("invalid elsp submission, desc0 is invalid\n");
		goto inv_desc;
	}

	for (i = 0; i < ARRAY_SIZE(desc); i++) {
		if (!desc[i].valid)
			continue;
		if (!desc[i].privilege_access) {
			gvt_vgpu_err("unexpected GGTT elsp submission\n");
			goto inv_desc;
		}
	}

	/* submit workload */
	for (i = 0; i < ARRAY_SIZE(desc); i++) {
		if (!desc[i].valid)
			continue;
		ret = submit_context(vgpu, ring_id, &desc[i], i == 0);
		if (ret) {
			gvt_vgpu_err("failed to submit desc %d\n", i);
			return ret;
		}
	}

	return 0;

inv_desc:
	gvt_vgpu_err("descriptors content: desc0 %08x %08x desc1 %08x %08x\n",
		     desc[0].udw, desc[0].ldw, desc[1].udw, desc[1].ldw);
	return -EINVAL;
}

static void init_vgpu_execlist(struct intel_vgpu *vgpu, int ring_id)
{
	struct intel_vgpu_execlist *execlist = &vgpu->execlist[ring_id];
	struct execlist_context_status_pointer_format ctx_status_ptr;
	u32 ctx_status_ptr_reg;

	memset(execlist, 0, sizeof(*execlist));

	execlist->vgpu = vgpu;
	execlist->ring_id = ring_id;
	execlist->slot[0].index = 0;
	execlist->slot[1].index = 1;

	ctx_status_ptr_reg = execlist_ring_mmio(vgpu->gvt, ring_id,
			_EL_OFFSET_STATUS_PTR);

	ctx_status_ptr.dw = vgpu_vreg(vgpu, ctx_status_ptr_reg);
	ctx_status_ptr.read_ptr = 0;
	ctx_status_ptr.write_ptr = 0x7;
	vgpu_vreg(vgpu, ctx_status_ptr_reg) = ctx_status_ptr.dw;
}

static void clean_workloads(struct intel_vgpu *vgpu, unsigned long engine_mask)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;
	struct intel_engine_cs *engine;
	struct intel_vgpu_workload *pos, *n;
	unsigned int tmp;

	/* free the unsubmited workloads in the queues. */
	for_each_engine_masked(engine, dev_priv, engine_mask, tmp) {
		list_for_each_entry_safe(pos, n,
			&vgpu->workload_q_head[engine->id], list) {
			list_del_init(&pos->list);
			free_workload(pos);
		}

		clear_bit(engine->id, vgpu->shadow_ctx_desc_updated);
	}
}

void intel_vgpu_clean_execlist(struct intel_vgpu *vgpu)
{
	enum intel_engine_id i;
	struct intel_engine_cs *engine;

	clean_workloads(vgpu, ALL_ENGINES);
	kmem_cache_destroy(vgpu->workloads);

	for_each_engine(engine, vgpu->gvt->dev_priv, i) {
		kfree(vgpu->reserve_ring_buffer_va[i]);
		vgpu->reserve_ring_buffer_va[i] = NULL;
		vgpu->reserve_ring_buffer_size[i] = 0;
	}

}

#define RESERVE_RING_BUFFER_SIZE		((1 * PAGE_SIZE)/8)
int intel_vgpu_init_execlist(struct intel_vgpu *vgpu)
{
	enum intel_engine_id i;
	struct intel_engine_cs *engine;

	/* each ring has a virtual execlist engine */
	for_each_engine(engine, vgpu->gvt->dev_priv, i) {
		init_vgpu_execlist(vgpu, i);
		INIT_LIST_HEAD(&vgpu->workload_q_head[i]);
	}

	vgpu->workloads = kmem_cache_create("gvt-g_vgpu_workload",
			sizeof(struct intel_vgpu_workload), 0,
			SLAB_HWCACHE_ALIGN,
			NULL);

	if (!vgpu->workloads)
		return -ENOMEM;

	/* each ring has a shadow ring buffer until vgpu destroyed */
	for_each_engine(engine, vgpu->gvt->dev_priv, i) {
		vgpu->reserve_ring_buffer_va[i] =
			kmalloc(RESERVE_RING_BUFFER_SIZE, GFP_KERNEL);
		if (!vgpu->reserve_ring_buffer_va[i]) {
			gvt_vgpu_err("fail to alloc reserve ring buffer\n");
			goto out;
		}
		vgpu->reserve_ring_buffer_size[i] = RESERVE_RING_BUFFER_SIZE;
	}
	return 0;
out:
	for_each_engine(engine, vgpu->gvt->dev_priv, i) {
		if (vgpu->reserve_ring_buffer_size[i]) {
			kfree(vgpu->reserve_ring_buffer_va[i]);
			vgpu->reserve_ring_buffer_va[i] = NULL;
			vgpu->reserve_ring_buffer_size[i] = 0;
		}
	}
	return -ENOMEM;
}

void intel_vgpu_reset_execlist(struct intel_vgpu *vgpu,
		unsigned long engine_mask)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;
	struct intel_engine_cs *engine;
	unsigned int tmp;

	clean_workloads(vgpu, engine_mask);
	for_each_engine_masked(engine, dev_priv, engine_mask, tmp)
		init_vgpu_execlist(vgpu, engine->id);
}
