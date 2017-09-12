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

#define get_desc_from_elsp_dwords(ed, i) \
	((struct execlist_ctx_descriptor_format *)&((ed)->data[i * 2]))

static int prepare_execlist_workload(struct intel_vgpu_workload *workload)
{
	struct intel_vgpu *vgpu = workload->vgpu;
	struct intel_vgpu_submission *s = &vgpu->submission;
	struct execlist_ctx_descriptor_format ctx[2];
	int ring_id = workload->ring_id;
	int ret;

	if (!workload->emulate_schedule_in)
		return 0;

	ctx[0] = *get_desc_from_elsp_dwords(&workload->elsp_dwords, 0);
	ctx[1] = *get_desc_from_elsp_dwords(&workload->elsp_dwords, 1);

	ret = emulate_execlist_schedule_in(&s->execlist[ring_id], ctx);
	if (ret) {
		gvt_vgpu_err("fail to emulate execlist schedule in\n");
		return ret;
	}
	return 0;
}

static int complete_execlist_workload(struct intel_vgpu_workload *workload)
{
	struct intel_vgpu *vgpu = workload->vgpu;
	int ring_id = workload->ring_id;
	struct intel_vgpu_submission *s = &vgpu->submission;
	struct intel_vgpu_execlist *execlist = &s->execlist[ring_id];
	struct intel_vgpu_workload *next_workload;
	struct list_head *next = workload_q_head(vgpu, ring_id)->next;
	bool lite_restore = false;
	int ret = 0;

	gvt_dbg_el("complete workload %p status %d\n", workload,
			workload->status);

	if (workload->status || (vgpu->resetting_eng & ENGINE_MASK(ring_id)))
		goto out;

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
		goto out;
	}

	ret = emulate_execlist_ctx_schedule_out(execlist, &workload->ctx_desc);
out:
	intel_vgpu_unpin_mm(workload->shadow_mm);
	intel_vgpu_destroy_workload(workload);
	return ret;
}

static int submit_context(struct intel_vgpu *vgpu, int ring_id,
		struct execlist_ctx_descriptor_format *desc,
		bool emulate_schedule_in)
{
	struct intel_vgpu_submission *s = &vgpu->submission;
	struct intel_vgpu_workload *workload = NULL;

	workload = intel_vgpu_create_workload(vgpu, ring_id, desc);
	if (IS_ERR(workload))
		return PTR_ERR(workload);

	workload->prepare = prepare_execlist_workload;
	workload->complete = complete_execlist_workload;
	workload->emulate_schedule_in = emulate_schedule_in;

	if (emulate_schedule_in)
		workload->elsp_dwords = s->execlist[ring_id].elsp_dwords;

	gvt_dbg_el("workload %p emulate schedule_in %d\n", workload,
			emulate_schedule_in);

	queue_workload(workload);
	return 0;
}

int intel_vgpu_submit_execlist(struct intel_vgpu *vgpu, int ring_id)
{
	struct intel_vgpu_submission *s = &vgpu->submission;
	struct intel_vgpu_execlist *execlist = &s->execlist[ring_id];
	struct execlist_ctx_descriptor_format *desc[2];
	int i, ret;

	desc[0] = get_desc_from_elsp_dwords(&execlist->elsp_dwords, 0);
	desc[1] = get_desc_from_elsp_dwords(&execlist->elsp_dwords, 1);

	if (!desc[0]->valid) {
		gvt_vgpu_err("invalid elsp submission, desc0 is invalid\n");
		goto inv_desc;
	}

	for (i = 0; i < ARRAY_SIZE(desc); i++) {
		if (!desc[i]->valid)
			continue;
		if (!desc[i]->privilege_access) {
			gvt_vgpu_err("unexpected GGTT elsp submission\n");
			goto inv_desc;
		}
	}

	/* submit workload */
	for (i = 0; i < ARRAY_SIZE(desc); i++) {
		if (!desc[i]->valid)
			continue;
		ret = submit_context(vgpu, ring_id, desc[i], i == 0);
		if (ret) {
			gvt_vgpu_err("failed to submit desc %d\n", i);
			return ret;
		}
	}

	return 0;

inv_desc:
	gvt_vgpu_err("descriptors content: desc0 %08x %08x desc1 %08x %08x\n",
		     desc[0]->udw, desc[0]->ldw, desc[1]->udw, desc[1]->ldw);
	return -EINVAL;
}

static void init_vgpu_execlist(struct intel_vgpu *vgpu, int ring_id)
{
	struct intel_vgpu_submission *s = &vgpu->submission;
	struct intel_vgpu_execlist *execlist = &s->execlist[ring_id];
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

void clean_execlist(struct intel_vgpu *vgpu)
{
	enum intel_engine_id i;
	struct intel_engine_cs *engine;

	for_each_engine(engine, vgpu->gvt->dev_priv, i) {
		struct intel_vgpu_submission *s = &vgpu->submission;

		kfree(s->ring_scan_buffer[i]);
		s->ring_scan_buffer[i] = NULL;
		s->ring_scan_buffer_size[i] = 0;
	}
}

void reset_execlist(struct intel_vgpu *vgpu,
		unsigned long engine_mask)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;
	struct intel_engine_cs *engine;
	unsigned int tmp;

	for_each_engine_masked(engine, dev_priv, engine_mask, tmp)
		init_vgpu_execlist(vgpu, engine->id);
}

int init_execlist(struct intel_vgpu *vgpu)
{
	reset_execlist(vgpu, ALL_ENGINES);
	return 0;
}

const struct intel_vgpu_submission_ops intel_vgpu_execlist_submission_ops = {
	.name = "execlist",
	.init = init_execlist,
	.reset = reset_execlist,
	.clean = clean_execlist,
};
