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

static int context_switch_events[] = {
	[RCS] = RCS_AS_CONTEXT_SWITCH,
	[BCS] = BCS_AS_CONTEXT_SWITCH,
	[VCS] = VCS_AS_CONTEXT_SWITCH,
	[VCS2] = VCS2_AS_CONTEXT_SWITCH,
	[VECS] = VECS_AS_CONTEXT_SWITCH,
};

static int ring_id_to_context_switch_event(int ring_id)
{
	if (WARN_ON(ring_id < RCS && ring_id >
				ARRAY_SIZE(context_switch_events)))
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
	struct intel_vgpu_execlist_slot *running = execlist->running_slot;
	struct intel_vgpu_execlist_slot *pending = execlist->pending_slot;
	struct execlist_ctx_descriptor_format *ctx0 = &running->ctx[0];
	struct execlist_ctx_descriptor_format *ctx1 = &running->ctx[1];
	struct execlist_context_status_format status;

	memset(&status, 0, sizeof(status));

	gvt_dbg_el("schedule out context id %x\n", ctx->context_id);

	if (WARN_ON(!same_context(ctx, execlist->running_context))) {
		gvt_err("schedule out context is not running context,"
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
		gvt_err("virtual execlist slots are full\n");
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

	gvt_dbg_el("emulate schedule-in\n");

	if (!slot) {
		gvt_err("no available execlist slot\n");
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


#define BATCH_BUFFER_ADDR_MASK ((1UL << 32) - (1U << 2))
#define BATCH_BUFFER_ADDR_HIGH_MASK ((1UL << 16) - (1U))
static int set_gma_to_bb_cmd(struct intel_shadow_bb_entry *entry_obj,
			     unsigned long add, int gmadr_bytes)
{
	if (WARN_ON(gmadr_bytes != 4 && gmadr_bytes != 8))
		return -1;

	*((u32 *)(entry_obj->bb_start_cmd_va + (1 << 2))) = add &
		BATCH_BUFFER_ADDR_MASK;
	if (gmadr_bytes == 8) {
		*((u32 *)(entry_obj->bb_start_cmd_va + (2 << 2))) =
			add & BATCH_BUFFER_ADDR_HIGH_MASK;
	}

	return 0;
}

static void prepare_shadow_batch_buffer(struct intel_vgpu_workload *workload)
{
	int gmadr_bytes = workload->vgpu->gvt->device_info.gmadr_bytes_in_cmd;
	struct i915_vma *vma;
	unsigned long gma;

	/* pin the gem object to ggtt */
	if (!list_empty(&workload->shadow_bb)) {
		struct intel_shadow_bb_entry *entry_obj =
			list_first_entry(&workload->shadow_bb,
					 struct intel_shadow_bb_entry,
					 list);
		struct intel_shadow_bb_entry *temp;

		list_for_each_entry_safe(entry_obj, temp, &workload->shadow_bb,
				list) {
			vma = i915_gem_object_ggtt_pin(entry_obj->obj, NULL, 0,
					0, 0);
			if (IS_ERR(vma)) {
				gvt_err("Cannot pin\n");
				return;
			}
			i915_gem_object_unpin_pages(entry_obj->obj);

			/* update the relocate gma with shadow batch buffer*/
			gma = i915_gem_object_ggtt_offset(entry_obj->obj, NULL);
			WARN_ON(!IS_ALIGNED(gma, 4));
			set_gma_to_bb_cmd(entry_obj, gma, gmadr_bytes);
		}
	}
}

static int update_wa_ctx_2_shadow_ctx(struct intel_shadow_wa_ctx *wa_ctx)
{
	int ring_id = wa_ctx->workload->ring_id;
	struct i915_gem_context *shadow_ctx =
		wa_ctx->workload->vgpu->shadow_ctx;
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

static void prepare_shadow_wa_ctx(struct intel_shadow_wa_ctx *wa_ctx)
{
	struct i915_vma *vma;
	unsigned long gma;
	unsigned char *per_ctx_va =
		(unsigned char *)wa_ctx->indirect_ctx.shadow_va +
		wa_ctx->indirect_ctx.size;

	if (wa_ctx->indirect_ctx.size == 0)
		return;

	vma = i915_gem_object_ggtt_pin(wa_ctx->indirect_ctx.obj, NULL, 0, 0, 0);
	if (IS_ERR(vma)) {
		gvt_err("Cannot pin indirect ctx obj\n");
		return;
	}
	i915_gem_object_unpin_pages(wa_ctx->indirect_ctx.obj);

	gma = i915_gem_object_ggtt_offset(wa_ctx->indirect_ctx.obj, NULL);
	WARN_ON(!IS_ALIGNED(gma, CACHELINE_BYTES));
	wa_ctx->indirect_ctx.shadow_gma = gma;

	wa_ctx->per_ctx.shadow_gma = *((unsigned int *)per_ctx_va + 1);
	memset(per_ctx_va, 0, CACHELINE_BYTES);

	update_wa_ctx_2_shadow_ctx(wa_ctx);
}

static int prepare_execlist_workload(struct intel_vgpu_workload *workload)
{
	struct intel_vgpu *vgpu = workload->vgpu;
	struct execlist_ctx_descriptor_format ctx[2];
	int ring_id = workload->ring_id;

	intel_vgpu_pin_mm(workload->shadow_mm);
	intel_vgpu_sync_oos_pages(workload->vgpu);
	intel_vgpu_flush_post_shadow(workload->vgpu);
	prepare_shadow_batch_buffer(workload);
	prepare_shadow_wa_ctx(&workload->wa_ctx);
	if (!workload->emulate_schedule_in)
		return 0;

	ctx[0] = *get_desc_from_elsp_dwords(&workload->elsp_dwords, 1);
	ctx[1] = *get_desc_from_elsp_dwords(&workload->elsp_dwords, 0);

	return emulate_execlist_schedule_in(&vgpu->execlist[ring_id], ctx);
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
			i915_gem_object_put(entry_obj->obj);
			kvfree(entry_obj->va);
			list_del(&entry_obj->list);
			kfree(entry_obj);
		}
	}
}

static void release_shadow_wa_ctx(struct intel_shadow_wa_ctx *wa_ctx)
{
	if (wa_ctx->indirect_ctx.size == 0)
		return;

	i915_gem_object_put(wa_ctx->indirect_ctx.obj);
	kvfree(wa_ctx->indirect_ctx.shadow_va);
}

static int complete_execlist_workload(struct intel_vgpu_workload *workload)
{
	struct intel_vgpu *vgpu = workload->vgpu;
	struct intel_vgpu_execlist *execlist =
		&vgpu->execlist[workload->ring_id];
	struct intel_vgpu_workload *next_workload;
	struct list_head *next = workload_q_head(vgpu, workload->ring_id)->next;
	bool lite_restore = false;
	int ret;

	gvt_dbg_el("complete workload %p status %d\n", workload,
			workload->status);

	release_shadow_batch_buffer(workload);
	release_shadow_wa_ctx(&workload->wa_ctx);

	if (workload->status || vgpu->resetting)
		goto out;

	if (!list_empty(workload_q_head(vgpu, workload->ring_id))) {
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
	int page_table_level;
	u32 pdp[8];

	if (desc->addressing_mode == 1) { /* legacy 32-bit */
		page_table_level = 3;
	} else if (desc->addressing_mode == 3) { /* legacy 64 bit */
		page_table_level = 4;
	} else {
		gvt_err("Advanced Context mode(SVM) is not supported!\n");
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
			gvt_err("fail to create mm object.\n");
			return PTR_ERR(mm);
		}
	}
	workload->shadow_mm = mm;
	return 0;
}

#define get_last_workload(q) \
	(list_empty(q) ? NULL : container_of(q->prev, \
	struct intel_vgpu_workload, list))

bool submit_context(struct intel_vgpu *vgpu, int ring_id,
		struct execlist_ctx_descriptor_format *desc,
		bool emulate_schedule_in)
{
	struct list_head *q = workload_q_head(vgpu, ring_id);
	struct intel_vgpu_workload *last_workload = get_last_workload(q);
	struct intel_vgpu_workload *workload = NULL;
	u64 ring_context_gpa;
	u32 head, tail, start, ctl, ctx_ctl, per_ctx, indirect_ctx;
	int ret;

	ring_context_gpa = intel_vgpu_gma_to_gpa(vgpu->gtt.ggtt_mm,
			(u32)((desc->lrca + 1) << GTT_PAGE_SHIFT));
	if (ring_context_gpa == INTEL_GVT_INVALID_ADDR) {
		gvt_err("invalid guest context LRCA: %x\n", desc->lrca);
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
		workload->wa_ctx.workload = workload;

		WARN_ON(workload->wa_ctx.indirect_ctx.size && !(per_ctx & 0x1));
	}

	if (emulate_schedule_in)
		memcpy(&workload->elsp_dwords,
				&vgpu->execlist[ring_id].elsp_dwords,
				sizeof(workload->elsp_dwords));

	gvt_dbg_el("workload %p ring id %d head %x tail %x start %x ctl %x\n",
			workload, ring_id, head, tail, start, ctl);

	gvt_dbg_el("workload %p emulate schedule_in %d\n", workload,
			emulate_schedule_in);

	ret = prepare_mm(workload);
	if (ret) {
		kmem_cache_free(vgpu->workloads, workload);
		return ret;
	}

	queue_workload(workload);
	return 0;
}

int intel_vgpu_submit_execlist(struct intel_vgpu *vgpu, int ring_id)
{
	struct intel_vgpu_execlist *execlist = &vgpu->execlist[ring_id];
	struct execlist_ctx_descriptor_format *desc[2], valid_desc[2];
	unsigned long valid_desc_bitmap = 0;
	bool emulate_schedule_in = true;
	int ret;
	int i;

	memset(valid_desc, 0, sizeof(valid_desc));

	desc[0] = get_desc_from_elsp_dwords(&execlist->elsp_dwords, 1);
	desc[1] = get_desc_from_elsp_dwords(&execlist->elsp_dwords, 0);

	for (i = 0; i < 2; i++) {
		if (!desc[i]->valid)
			continue;

		if (!desc[i]->privilege_access) {
			gvt_err("vgpu%d: unexpected GGTT elsp submission\n",
					vgpu->id);
			return -EINVAL;
		}

		/* TODO: add another guest context checks here. */
		set_bit(i, &valid_desc_bitmap);
		valid_desc[i] = *desc[i];
	}

	if (!valid_desc_bitmap) {
		gvt_err("vgpu%d: no valid desc in a elsp submission\n",
				vgpu->id);
		return -EINVAL;
	}

	if (!test_bit(0, (void *)&valid_desc_bitmap) &&
			test_bit(1, (void *)&valid_desc_bitmap)) {
		gvt_err("vgpu%d: weird elsp submission, desc 0 is not valid\n",
				vgpu->id);
		return -EINVAL;
	}

	/* submit workload */
	for_each_set_bit(i, (void *)&valid_desc_bitmap, 2) {
		ret = submit_context(vgpu, ring_id, &valid_desc[i],
				emulate_schedule_in);
		if (ret) {
			gvt_err("vgpu%d: fail to schedule workload\n",
					vgpu->id);
			return ret;
		}
		emulate_schedule_in = false;
	}
	return 0;
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
	ctx_status_ptr.read_ptr = ctx_status_ptr.write_ptr = 0x7;
	vgpu_vreg(vgpu, ctx_status_ptr_reg) = ctx_status_ptr.dw;
}

void intel_vgpu_clean_execlist(struct intel_vgpu *vgpu)
{
	kmem_cache_destroy(vgpu->workloads);
}

int intel_vgpu_init_execlist(struct intel_vgpu *vgpu)
{
	int i;

	/* each ring has a virtual execlist engine */
	for (i = 0; i < I915_NUM_ENGINES; i++) {
		init_vgpu_execlist(vgpu, i);
		INIT_LIST_HEAD(&vgpu->workload_q_head[i]);
	}

	vgpu->workloads = kmem_cache_create("gvt-g vgpu workload",
			sizeof(struct intel_vgpu_workload), 0,
			SLAB_HWCACHE_ALIGN,
			NULL);

	if (!vgpu->workloads)
		return -ENOMEM;

	return 0;
}

void intel_vgpu_reset_execlist(struct intel_vgpu *vgpu,
		unsigned long ring_bitmap)
{
	int bit;
	struct list_head *pos, *n;
	struct intel_vgpu_workload *workload = NULL;

	for_each_set_bit(bit, &ring_bitmap, sizeof(ring_bitmap) * 8) {
		if (bit >= I915_NUM_ENGINES)
			break;
		/* free the unsubmited workload in the queue */
		list_for_each_safe(pos, n, &vgpu->workload_q_head[bit]) {
			workload = container_of(pos,
					struct intel_vgpu_workload, list);
			list_del_init(&workload->list);
			free_workload(workload);
		}

		init_vgpu_execlist(vgpu, bit);
	}
}
