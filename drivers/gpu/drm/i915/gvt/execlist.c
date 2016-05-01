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

#define _EL_OFFSET_STATUS       0x234
#define _EL_OFFSET_STATUS_BUF   0x370
#define _EL_OFFSET_STATUS_PTR   0x3A0

#define execlist_ring_mmio(gvt, ring_id, offset) \
	(gvt->dev_priv->engine[ring_id].mmio_base + (offset))

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

int emulate_execlist_ctx_schedule_out(
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

int emulate_execlist_schedule_in(struct intel_vgpu_execlist *execlist,
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

int intel_vgpu_init_execlist(struct intel_vgpu *vgpu)
{
	int i;

	/* each ring has a virtual execlist engine */
	for (i = 0; i < I915_NUM_ENGINES; i++)
		init_vgpu_execlist(vgpu, i);

	return 0;
}
