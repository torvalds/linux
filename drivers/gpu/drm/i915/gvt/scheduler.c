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
 *    Zhi Wang <zhi.a.wang@intel.com>
 *
 * Contributors:
 *    Ping Gao <ping.a.gao@intel.com>
 *    Tina Zhang <tina.zhang@intel.com>
 *    Chanbin Du <changbin.du@intel.com>
 *    Min He <min.he@intel.com>
 *    Bing Niu <bing.niu@intel.com>
 *    Zhenyu Wang <zhenyuw@linux.intel.com>
 *
 */

#include <linux/kthread.h>

#include "i915_drv.h"
#include "gvt.h"

#define RING_CTX_OFF(x) \
	offsetof(struct execlist_ring_context, x)

static void set_context_pdp_root_pointer(
		struct execlist_ring_context *ring_context,
		u32 pdp[8])
{
	struct execlist_mmio_pair *pdp_pair = &ring_context->pdp3_UDW;
	int i;

	for (i = 0; i < 8; i++)
		pdp_pair[i].val = pdp[7 - i];
}

static int populate_shadow_context(struct intel_vgpu_workload *workload)
{
	struct intel_vgpu *vgpu = workload->vgpu;
	struct intel_gvt *gvt = vgpu->gvt;
	int ring_id = workload->ring_id;
	struct i915_gem_context *shadow_ctx = workload->vgpu->shadow_ctx;
	struct drm_i915_gem_object *ctx_obj =
		shadow_ctx->engine[ring_id].state->obj;
	struct execlist_ring_context *shadow_ring_context;
	struct page *page;
	void *dst;
	unsigned long context_gpa, context_page_num;
	int i;

	gvt_dbg_sched("ring id %d workload lrca %x", ring_id,
			workload->ctx_desc.lrca);

	context_page_num = intel_lr_context_size(
			gvt->dev_priv->engine[ring_id]);

	context_page_num = context_page_num >> PAGE_SHIFT;

	if (IS_BROADWELL(gvt->dev_priv) && ring_id == RCS)
		context_page_num = 19;

	i = 2;

	while (i < context_page_num) {
		context_gpa = intel_vgpu_gma_to_gpa(vgpu->gtt.ggtt_mm,
				(u32)((workload->ctx_desc.lrca + i) <<
				GTT_PAGE_SHIFT));
		if (context_gpa == INTEL_GVT_INVALID_ADDR) {
			gvt_err("Invalid guest context descriptor\n");
			return -EINVAL;
		}

		page = i915_gem_object_get_page(ctx_obj, LRC_PPHWSP_PN + i);
		dst = kmap(page);
		intel_gvt_hypervisor_read_gpa(vgpu, context_gpa, dst,
				GTT_PAGE_SIZE);
		kunmap(page);
		i++;
	}

	page = i915_gem_object_get_page(ctx_obj, LRC_STATE_PN);
	shadow_ring_context = kmap(page);

#define COPY_REG(name) \
	intel_gvt_hypervisor_read_gpa(vgpu, workload->ring_context_gpa \
		+ RING_CTX_OFF(name.val), &shadow_ring_context->name.val, 4)

	COPY_REG(ctx_ctrl);
	COPY_REG(ctx_timestamp);

	if (ring_id == RCS) {
		COPY_REG(bb_per_ctx_ptr);
		COPY_REG(rcs_indirect_ctx);
		COPY_REG(rcs_indirect_ctx_offset);
	}
#undef COPY_REG

	set_context_pdp_root_pointer(shadow_ring_context,
				     workload->shadow_mm->shadow_page_table);

	intel_gvt_hypervisor_read_gpa(vgpu,
			workload->ring_context_gpa +
			sizeof(*shadow_ring_context),
			(void *)shadow_ring_context +
			sizeof(*shadow_ring_context),
			GTT_PAGE_SIZE - sizeof(*shadow_ring_context));

	kunmap(page);
	return 0;
}

static int shadow_context_status_change(struct notifier_block *nb,
		unsigned long action, void *data)
{
	struct intel_vgpu *vgpu = container_of(nb,
			struct intel_vgpu, shadow_ctx_notifier_block);
	struct drm_i915_gem_request *req =
		(struct drm_i915_gem_request *)data;
	struct intel_gvt_workload_scheduler *scheduler =
		&vgpu->gvt->scheduler;
	struct intel_vgpu_workload *workload =
		scheduler->current_workload[req->engine->id];

	switch (action) {
	case INTEL_CONTEXT_SCHEDULE_IN:
		intel_gvt_load_render_mmio(workload->vgpu,
					   workload->ring_id);
		atomic_set(&workload->shadow_ctx_active, 1);
		break;
	case INTEL_CONTEXT_SCHEDULE_OUT:
		intel_gvt_restore_render_mmio(workload->vgpu,
					      workload->ring_id);
		atomic_set(&workload->shadow_ctx_active, 0);
		break;
	default:
		WARN_ON(1);
		return NOTIFY_OK;
	}
	wake_up(&workload->shadow_ctx_status_wq);
	return NOTIFY_OK;
}

static int dispatch_workload(struct intel_vgpu_workload *workload)
{
	int ring_id = workload->ring_id;
	struct i915_gem_context *shadow_ctx = workload->vgpu->shadow_ctx;
	struct drm_i915_private *dev_priv = workload->vgpu->gvt->dev_priv;
	struct drm_i915_gem_request *rq;
	int ret;

	gvt_dbg_sched("ring id %d prepare to dispatch workload %p\n",
		ring_id, workload);

	shadow_ctx->desc_template = workload->ctx_desc.addressing_mode <<
				    GEN8_CTX_ADDRESSING_MODE_SHIFT;

	mutex_lock(&dev_priv->drm.struct_mutex);

	rq = i915_gem_request_alloc(dev_priv->engine[ring_id], shadow_ctx);
	if (IS_ERR(rq)) {
		gvt_err("fail to allocate gem request\n");
		ret = PTR_ERR(rq);
		goto out;
	}

	gvt_dbg_sched("ring id %d get i915 gem request %p\n", ring_id, rq);

	workload->req = i915_gem_request_get(rq);

	ret = intel_gvt_scan_and_shadow_workload(workload);
	if (ret)
		goto out;

	ret = intel_gvt_scan_and_shadow_wa_ctx(&workload->wa_ctx);
	if (ret)
		goto out;

	ret = populate_shadow_context(workload);
	if (ret)
		goto out;

	if (workload->prepare) {
		ret = workload->prepare(workload);
		if (ret)
			goto out;
	}

	gvt_dbg_sched("ring id %d submit workload to i915 %p\n",
			ring_id, workload->req);

	ret = 0;
	workload->dispatched = true;
out:
	if (ret)
		workload->status = ret;

	if (!IS_ERR_OR_NULL(rq))
		i915_add_request_no_flush(rq);
	mutex_unlock(&dev_priv->drm.struct_mutex);
	return ret;
}

static struct intel_vgpu_workload *pick_next_workload(
		struct intel_gvt *gvt, int ring_id)
{
	struct intel_gvt_workload_scheduler *scheduler = &gvt->scheduler;
	struct intel_vgpu_workload *workload = NULL;

	mutex_lock(&gvt->lock);

	/*
	 * no current vgpu / will be scheduled out / no workload
	 * bail out
	 */
	if (!scheduler->current_vgpu) {
		gvt_dbg_sched("ring id %d stop - no current vgpu\n", ring_id);
		goto out;
	}

	if (scheduler->need_reschedule) {
		gvt_dbg_sched("ring id %d stop - will reschedule\n", ring_id);
		goto out;
	}

	if (list_empty(workload_q_head(scheduler->current_vgpu, ring_id))) {
		gvt_dbg_sched("ring id %d stop - no available workload\n",
				ring_id);
		goto out;
	}

	/*
	 * still have current workload, maybe the workload disptacher
	 * fail to submit it for some reason, resubmit it.
	 */
	if (scheduler->current_workload[ring_id]) {
		workload = scheduler->current_workload[ring_id];
		gvt_dbg_sched("ring id %d still have current workload %p\n",
				ring_id, workload);
		goto out;
	}

	/*
	 * pick a workload as current workload
	 * once current workload is set, schedule policy routines
	 * will wait the current workload is finished when trying to
	 * schedule out a vgpu.
	 */
	scheduler->current_workload[ring_id] = container_of(
			workload_q_head(scheduler->current_vgpu, ring_id)->next,
			struct intel_vgpu_workload, list);

	workload = scheduler->current_workload[ring_id];

	gvt_dbg_sched("ring id %d pick new workload %p\n", ring_id, workload);

	atomic_inc(&workload->vgpu->running_workload_num);
out:
	mutex_unlock(&gvt->lock);
	return workload;
}

static void update_guest_context(struct intel_vgpu_workload *workload)
{
	struct intel_vgpu *vgpu = workload->vgpu;
	struct intel_gvt *gvt = vgpu->gvt;
	int ring_id = workload->ring_id;
	struct i915_gem_context *shadow_ctx = workload->vgpu->shadow_ctx;
	struct drm_i915_gem_object *ctx_obj =
		shadow_ctx->engine[ring_id].state->obj;
	struct execlist_ring_context *shadow_ring_context;
	struct page *page;
	void *src;
	unsigned long context_gpa, context_page_num;
	int i;

	gvt_dbg_sched("ring id %d workload lrca %x\n", ring_id,
			workload->ctx_desc.lrca);

	context_page_num = intel_lr_context_size(
			gvt->dev_priv->engine[ring_id]);

	context_page_num = context_page_num >> PAGE_SHIFT;

	if (IS_BROADWELL(gvt->dev_priv) && ring_id == RCS)
		context_page_num = 19;

	i = 2;

	while (i < context_page_num) {
		context_gpa = intel_vgpu_gma_to_gpa(vgpu->gtt.ggtt_mm,
				(u32)((workload->ctx_desc.lrca + i) <<
					GTT_PAGE_SHIFT));
		if (context_gpa == INTEL_GVT_INVALID_ADDR) {
			gvt_err("invalid guest context descriptor\n");
			return;
		}

		page = i915_gem_object_get_page(ctx_obj, LRC_PPHWSP_PN + i);
		src = kmap(page);
		intel_gvt_hypervisor_write_gpa(vgpu, context_gpa, src,
				GTT_PAGE_SIZE);
		kunmap(page);
		i++;
	}

	intel_gvt_hypervisor_write_gpa(vgpu, workload->ring_context_gpa +
		RING_CTX_OFF(ring_header.val), &workload->rb_tail, 4);

	page = i915_gem_object_get_page(ctx_obj, LRC_STATE_PN);
	shadow_ring_context = kmap(page);

#define COPY_REG(name) \
	intel_gvt_hypervisor_write_gpa(vgpu, workload->ring_context_gpa + \
		RING_CTX_OFF(name.val), &shadow_ring_context->name.val, 4)

	COPY_REG(ctx_ctrl);
	COPY_REG(ctx_timestamp);

#undef COPY_REG

	intel_gvt_hypervisor_write_gpa(vgpu,
			workload->ring_context_gpa +
			sizeof(*shadow_ring_context),
			(void *)shadow_ring_context +
			sizeof(*shadow_ring_context),
			GTT_PAGE_SIZE - sizeof(*shadow_ring_context));

	kunmap(page);
}

static void complete_current_workload(struct intel_gvt *gvt, int ring_id)
{
	struct intel_gvt_workload_scheduler *scheduler = &gvt->scheduler;
	struct intel_vgpu_workload *workload;
	int event;

	mutex_lock(&gvt->lock);

	workload = scheduler->current_workload[ring_id];

	if (!workload->status && !workload->vgpu->resetting) {
		wait_event(workload->shadow_ctx_status_wq,
			   !atomic_read(&workload->shadow_ctx_active));

		update_guest_context(workload);

		for_each_set_bit(event, workload->pending_events,
				 INTEL_GVT_EVENT_MAX)
			intel_vgpu_trigger_virtual_event(workload->vgpu,
					event);
	}

	gvt_dbg_sched("ring id %d complete workload %p status %d\n",
			ring_id, workload, workload->status);

	scheduler->current_workload[ring_id] = NULL;

	atomic_dec(&workload->vgpu->running_workload_num);

	list_del_init(&workload->list);
	workload->complete(workload);

	wake_up(&scheduler->workload_complete_wq);
	mutex_unlock(&gvt->lock);
}

struct workload_thread_param {
	struct intel_gvt *gvt;
	int ring_id;
};

static DEFINE_MUTEX(scheduler_mutex);

static int workload_thread(void *priv)
{
	struct workload_thread_param *p = (struct workload_thread_param *)priv;
	struct intel_gvt *gvt = p->gvt;
	int ring_id = p->ring_id;
	struct intel_gvt_workload_scheduler *scheduler = &gvt->scheduler;
	struct intel_vgpu_workload *workload = NULL;
	long lret;
	int ret;
	bool need_force_wake = IS_SKYLAKE(gvt->dev_priv);
	DEFINE_WAIT_FUNC(wait, woken_wake_function);

	kfree(p);

	gvt_dbg_core("workload thread for ring %d started\n", ring_id);

	while (!kthread_should_stop()) {
		add_wait_queue(&scheduler->waitq[ring_id], &wait);
		do {
			workload = pick_next_workload(gvt, ring_id);
			if (workload)
				break;
			wait_woken(&wait, TASK_INTERRUPTIBLE,
				   MAX_SCHEDULE_TIMEOUT);
		} while (!kthread_should_stop());
		remove_wait_queue(&scheduler->waitq[ring_id], &wait);

		if (!workload)
			break;

		mutex_lock(&scheduler_mutex);

		gvt_dbg_sched("ring id %d next workload %p vgpu %d\n",
				workload->ring_id, workload,
				workload->vgpu->id);

		intel_runtime_pm_get(gvt->dev_priv);

		gvt_dbg_sched("ring id %d will dispatch workload %p\n",
				workload->ring_id, workload);

		if (need_force_wake)
			intel_uncore_forcewake_get(gvt->dev_priv,
					FORCEWAKE_ALL);

		mutex_lock(&gvt->lock);
		ret = dispatch_workload(workload);
		mutex_unlock(&gvt->lock);

		if (ret) {
			gvt_err("fail to dispatch workload, skip\n");
			goto complete;
		}

		gvt_dbg_sched("ring id %d wait workload %p\n",
				workload->ring_id, workload);

		lret = i915_wait_request(workload->req,
					 0, MAX_SCHEDULE_TIMEOUT);
		if (lret < 0) {
			workload->status = lret;
			gvt_err("fail to wait workload, skip\n");
		} else {
			workload->status = 0;
		}

complete:
		gvt_dbg_sched("will complete workload %p\n, status: %d\n",
				workload, workload->status);

		complete_current_workload(gvt, ring_id);

		if (workload->req)
			i915_gem_request_put(fetch_and_zero(&workload->req));

		if (need_force_wake)
			intel_uncore_forcewake_put(gvt->dev_priv,
					FORCEWAKE_ALL);

		intel_runtime_pm_put(gvt->dev_priv);

		mutex_unlock(&scheduler_mutex);

	}
	return 0;
}

void intel_gvt_wait_vgpu_idle(struct intel_vgpu *vgpu)
{
	struct intel_gvt *gvt = vgpu->gvt;
	struct intel_gvt_workload_scheduler *scheduler = &gvt->scheduler;

	if (atomic_read(&vgpu->running_workload_num)) {
		gvt_dbg_sched("wait vgpu idle\n");

		wait_event(scheduler->workload_complete_wq,
				!atomic_read(&vgpu->running_workload_num));
	}
}

void intel_gvt_clean_workload_scheduler(struct intel_gvt *gvt)
{
	struct intel_gvt_workload_scheduler *scheduler = &gvt->scheduler;
	int i;

	gvt_dbg_core("clean workload scheduler\n");

	for (i = 0; i < I915_NUM_ENGINES; i++) {
		if (scheduler->thread[i]) {
			kthread_stop(scheduler->thread[i]);
			scheduler->thread[i] = NULL;
		}
	}
}

int intel_gvt_init_workload_scheduler(struct intel_gvt *gvt)
{
	struct intel_gvt_workload_scheduler *scheduler = &gvt->scheduler;
	struct workload_thread_param *param = NULL;
	int ret;
	int i;

	gvt_dbg_core("init workload scheduler\n");

	init_waitqueue_head(&scheduler->workload_complete_wq);

	for (i = 0; i < I915_NUM_ENGINES; i++) {
		/* check ring mask at init time */
		if (!HAS_ENGINE(gvt->dev_priv, i))
			continue;

		init_waitqueue_head(&scheduler->waitq[i]);

		param = kzalloc(sizeof(*param), GFP_KERNEL);
		if (!param) {
			ret = -ENOMEM;
			goto err;
		}

		param->gvt = gvt;
		param->ring_id = i;

		scheduler->thread[i] = kthread_run(workload_thread, param,
			"gvt workload %d", i);
		if (IS_ERR(scheduler->thread[i])) {
			gvt_err("fail to create workload thread\n");
			ret = PTR_ERR(scheduler->thread[i]);
			goto err;
		}
	}
	return 0;
err:
	intel_gvt_clean_workload_scheduler(gvt);
	kfree(param);
	param = NULL;
	return ret;
}

void intel_vgpu_clean_gvt_context(struct intel_vgpu *vgpu)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;

	atomic_notifier_chain_unregister(&vgpu->shadow_ctx->status_notifier,
			&vgpu->shadow_ctx_notifier_block);

	mutex_lock(&dev_priv->drm.struct_mutex);

	/* a little hacky to mark as ctx closed */
	vgpu->shadow_ctx->closed = true;
	i915_gem_context_put(vgpu->shadow_ctx);

	mutex_unlock(&dev_priv->drm.struct_mutex);
}

int intel_vgpu_init_gvt_context(struct intel_vgpu *vgpu)
{
	atomic_set(&vgpu->running_workload_num, 0);

	vgpu->shadow_ctx = i915_gem_context_create_gvt(
			&vgpu->gvt->dev_priv->drm);
	if (IS_ERR(vgpu->shadow_ctx))
		return PTR_ERR(vgpu->shadow_ctx);

	vgpu->shadow_ctx->engine[RCS].initialised = true;

	vgpu->shadow_ctx_notifier_block.notifier_call =
		shadow_context_status_change;

	atomic_notifier_chain_register(&vgpu->shadow_ctx->status_notifier,
				       &vgpu->shadow_ctx_notifier_block);
	return 0;
}
