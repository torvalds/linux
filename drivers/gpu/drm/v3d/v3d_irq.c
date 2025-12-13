// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2014-2018 Broadcom */

/**
 * DOC: Interrupt management for the V3D engine
 *
 * When we take a bin, render, TFU done, or CSD done interrupt, we
 * need to signal the fence for that job so that the scheduler can
 * queue up the next one and unblock any waiters.
 *
 * When we take the binner out of memory interrupt, we need to
 * allocate some new memory and pass it to the binner so that the
 * current job can make progress.
 */

#include <linux/platform_device.h>
#include <linux/sched/clock.h>

#include "v3d_drv.h"
#include "v3d_regs.h"
#include "v3d_trace.h"

#define V3D_CORE_IRQS(ver) ((u32)(V3D_INT_OUTOMEM |	\
				  V3D_INT_FLDONE |	\
				  V3D_INT_FRDONE |	\
				  V3D_INT_CSDDONE(ver) |	\
				  (ver < 71 ? V3D_INT_GMPV : 0)))

#define V3D_HUB_IRQS(ver) ((u32)(V3D_HUB_INT_MMU_WRV |	\
				 V3D_HUB_INT_MMU_PTI |	\
				 V3D_HUB_INT_MMU_CAP |	\
				 V3D_HUB_INT_TFUC |		\
				 (ver >= 71 ? V3D_V7_HUB_INT_GMPV : 0)))

static irqreturn_t
v3d_hub_irq(int irq, void *arg);

static void
v3d_overflow_mem_work(struct work_struct *work)
{
	struct v3d_dev *v3d =
		container_of(work, struct v3d_dev, overflow_mem_work);
	struct drm_device *dev = &v3d->drm;
	struct v3d_bo *bo = v3d_bo_create(dev, NULL /* XXX: GMP */, 256 * 1024);
	struct v3d_queue_state *queue = &v3d->queue[V3D_BIN];
	struct v3d_bin_job *bin_job;
	struct drm_gem_object *obj;
	unsigned long irqflags;

	if (IS_ERR(bo)) {
		DRM_ERROR("Couldn't allocate binner overflow mem\n");
		return;
	}
	obj = &bo->base.base;

	/* We lost a race, and our work task came in after the bin job
	 * completed and exited.  This can happen because the HW
	 * signals OOM before it's fully OOM, so the binner might just
	 * barely complete.
	 *
	 * If we lose the race and our work task comes in after a new
	 * bin job got scheduled, that's fine.  We'll just give them
	 * some binner pool anyway.
	 */
	spin_lock_irqsave(&queue->queue_lock, irqflags);
	bin_job = (struct v3d_bin_job *)queue->active_job;

	if (!bin_job) {
		spin_unlock_irqrestore(&queue->queue_lock, irqflags);
		goto out;
	}

	drm_gem_object_get(obj);
	list_add_tail(&bo->unref_head, &bin_job->render->unref_list);
	spin_unlock_irqrestore(&queue->queue_lock, irqflags);

	v3d_mmu_flush_all(v3d);

	V3D_CORE_WRITE(0, V3D_PTB_BPOA, bo->node.start << V3D_MMU_PAGE_SHIFT);
	V3D_CORE_WRITE(0, V3D_PTB_BPOS, obj->size);

out:
	drm_gem_object_put(obj);
}

static void
v3d_irq_signal_fence(struct v3d_dev *v3d, enum v3d_queue q,
		     void (*trace_irq)(struct drm_device *, uint64_t))
{
	struct v3d_queue_state *queue = &v3d->queue[q];
	struct v3d_fence *fence = to_v3d_fence(queue->active_job->irq_fence);

	v3d_job_update_stats(queue->active_job, q);
	trace_irq(&v3d->drm, fence->seqno);

	queue->active_job = NULL;
	dma_fence_signal(&fence->base);
}

static irqreturn_t
v3d_irq(int irq, void *arg)
{
	struct v3d_dev *v3d = arg;
	u32 intsts;
	irqreturn_t status = IRQ_NONE;

	intsts = V3D_CORE_READ(0, V3D_CTL_INT_STS);

	/* Acknowledge the interrupts we're handling here. */
	V3D_CORE_WRITE(0, V3D_CTL_INT_CLR, intsts);

	if (intsts & V3D_INT_OUTOMEM) {
		/* Note that the OOM status is edge signaled, so the
		 * interrupt won't happen again until the we actually
		 * add more memory.  Also, as of V3D 4.1, FLDONE won't
		 * be reported until any OOM state has been cleared.
		 */
		schedule_work(&v3d->overflow_mem_work);
		status = IRQ_HANDLED;
	}

	if (intsts & V3D_INT_FLDONE) {
		v3d_irq_signal_fence(v3d, V3D_BIN, trace_v3d_bcl_irq);
		status = IRQ_HANDLED;
	}

	if (intsts & V3D_INT_FRDONE) {
		v3d_irq_signal_fence(v3d, V3D_RENDER, trace_v3d_rcl_irq);
		status = IRQ_HANDLED;
	}

	if (intsts & V3D_INT_CSDDONE(v3d->ver)) {
		v3d_irq_signal_fence(v3d, V3D_CSD, trace_v3d_csd_irq);
		status = IRQ_HANDLED;
	}

	/* We shouldn't be triggering these if we have GMP in
	 * always-allowed mode.
	 */
	if (v3d->ver < V3D_GEN_71 && (intsts & V3D_INT_GMPV))
		dev_err(v3d->drm.dev, "GMP violation\n");

	/* V3D 4.2 wires the hub and core IRQs together, so if we &
	 * didn't see the common one then check hub for MMU IRQs.
	 */
	if (v3d->single_irq_line && status == IRQ_NONE)
		return v3d_hub_irq(irq, arg);

	return status;
}

static irqreturn_t
v3d_hub_irq(int irq, void *arg)
{
	struct v3d_dev *v3d = arg;
	u32 intsts;
	irqreturn_t status = IRQ_NONE;

	intsts = V3D_READ(V3D_HUB_INT_STS);

	/* Acknowledge the interrupts we're handling here. */
	V3D_WRITE(V3D_HUB_INT_CLR, intsts);

	if (intsts & V3D_HUB_INT_TFUC) {
		v3d_irq_signal_fence(v3d, V3D_TFU, trace_v3d_tfu_irq);
		status = IRQ_HANDLED;
	}

	if (intsts & (V3D_HUB_INT_MMU_WRV |
		      V3D_HUB_INT_MMU_PTI |
		      V3D_HUB_INT_MMU_CAP)) {
		u32 axi_id = V3D_READ(V3D_MMU_VIO_ID);
		u64 vio_addr = ((u64)V3D_READ(V3D_MMU_VIO_ADDR) <<
				(v3d->va_width - 32));
		static const struct {
			u32 begin;
			u32 end;
			const char *client;
		} v3d41_axi_ids[] = {
			{0x00, 0x20, "L2T"},
			{0x20, 0x21, "PTB"},
			{0x40, 0x41, "PSE"},
			{0x60, 0x80, "TLB"},
			{0x80, 0x88, "CLE"},
			{0xA0, 0xA1, "TFU"},
			{0xC0, 0xE0, "MMU"},
			{0xE0, 0xE1, "GMP"},
		}, v3d71_axi_ids[] = {
			{0x00, 0x30, "L2T"},
			{0x30, 0x38, "CLE"},
			{0x38, 0x39, "PTB"},
			{0x39, 0x3A, "PSE"},
			{0x3A, 0x3B, "CSD"},
			{0x40, 0x60, "TLB"},
			{0x60, 0x70, "MMU"},
			{0x7C, 0x7E, "TFU"},
			{0x7F, 0x80, "GMP"},
		};
		const char *client = "?";

		V3D_WRITE(V3D_MMU_CTL, V3D_READ(V3D_MMU_CTL));

		if (v3d->ver >= V3D_GEN_71) {
			size_t i;

			axi_id = axi_id & 0x7F;
			for (i = 0; i < ARRAY_SIZE(v3d71_axi_ids); i++) {
				if (axi_id >= v3d71_axi_ids[i].begin &&
				    axi_id < v3d71_axi_ids[i].end) {
					client = v3d71_axi_ids[i].client;
					break;
				}
			}
		} else if (v3d->ver >= V3D_GEN_41) {
			size_t i;

			axi_id = axi_id & 0xFF;
			for (i = 0; i < ARRAY_SIZE(v3d41_axi_ids); i++) {
				if (axi_id >= v3d41_axi_ids[i].begin &&
				    axi_id < v3d41_axi_ids[i].end) {
					client = v3d41_axi_ids[i].client;
					break;
				}
			}
		}

		dev_err(v3d->drm.dev, "MMU error from client %s (0x%x) at 0x%llx%s%s%s\n",
			client, axi_id, (long long)vio_addr,
			((intsts & V3D_HUB_INT_MMU_WRV) ?
			 ", write violation" : ""),
			((intsts & V3D_HUB_INT_MMU_PTI) ?
			 ", pte invalid" : ""),
			((intsts & V3D_HUB_INT_MMU_CAP) ?
			 ", cap exceeded" : ""));
		status = IRQ_HANDLED;
	}

	if (v3d->ver >= V3D_GEN_71 && (intsts & V3D_V7_HUB_INT_GMPV)) {
		dev_err(v3d->drm.dev, "GMP Violation\n");
		status = IRQ_HANDLED;
	}

	return status;
}

int
v3d_irq_init(struct v3d_dev *v3d)
{
	int irq, ret, core;

	INIT_WORK(&v3d->overflow_mem_work, v3d_overflow_mem_work);

	/* Clear any pending interrupts someone might have left around
	 * for us.
	 */
	for (core = 0; core < v3d->cores; core++)
		V3D_CORE_WRITE(core, V3D_CTL_INT_CLR, V3D_CORE_IRQS(v3d->ver));
	V3D_WRITE(V3D_HUB_INT_CLR, V3D_HUB_IRQS(v3d->ver));

	irq = platform_get_irq_optional(v3d_to_pdev(v3d), 1);
	if (irq == -EPROBE_DEFER)
		return irq;
	if (irq > 0) {
		v3d->irq[V3D_CORE_IRQ] = irq;

		ret = devm_request_irq(v3d->drm.dev, v3d->irq[V3D_CORE_IRQ],
				       v3d_irq, IRQF_SHARED,
				       "v3d_core0", v3d);
		if (ret)
			goto fail;

		irq = platform_get_irq(v3d_to_pdev(v3d), 0);
		if (irq < 0)
			return irq;
		v3d->irq[V3D_HUB_IRQ] = irq;

		ret = devm_request_irq(v3d->drm.dev, v3d->irq[V3D_HUB_IRQ],
				       v3d_hub_irq, IRQF_SHARED,
				       "v3d_hub", v3d);
		if (ret)
			goto fail;
	} else {
		v3d->single_irq_line = true;

		irq = platform_get_irq(v3d_to_pdev(v3d), 0);
		if (irq < 0)
			return irq;
		v3d->irq[V3D_CORE_IRQ] = irq;

		ret = devm_request_irq(v3d->drm.dev, v3d->irq[V3D_CORE_IRQ],
				       v3d_irq, IRQF_SHARED,
				       "v3d", v3d);
		if (ret)
			goto fail;
	}

	v3d_irq_enable(v3d);
	return 0;

fail:
	if (ret != -EPROBE_DEFER)
		dev_err(v3d->drm.dev, "IRQ setup failed: %d\n", ret);
	return ret;
}

void
v3d_irq_enable(struct v3d_dev *v3d)
{
	int core;

	/* Enable our set of interrupts, masking out any others. */
	for (core = 0; core < v3d->cores; core++) {
		V3D_CORE_WRITE(core, V3D_CTL_INT_MSK_SET, ~V3D_CORE_IRQS(v3d->ver));
		V3D_CORE_WRITE(core, V3D_CTL_INT_MSK_CLR, V3D_CORE_IRQS(v3d->ver));
	}

	V3D_WRITE(V3D_HUB_INT_MSK_SET, ~V3D_HUB_IRQS(v3d->ver));
	V3D_WRITE(V3D_HUB_INT_MSK_CLR, V3D_HUB_IRQS(v3d->ver));
}

void
v3d_irq_disable(struct v3d_dev *v3d)
{
	int core;

	/* Disable all interrupts. */
	for (core = 0; core < v3d->cores; core++)
		V3D_CORE_WRITE(core, V3D_CTL_INT_MSK_SET, ~0);
	V3D_WRITE(V3D_HUB_INT_MSK_SET, ~0);

	/* Finish any interrupt handler still in flight. */
	for (int i = 0; i < V3D_MAX_IRQS; i++) {
		if (v3d->irq[i])
			synchronize_irq(v3d->irq[i]);
	}

	/* Clear any pending interrupts we might have left. */
	for (core = 0; core < v3d->cores; core++)
		V3D_CORE_WRITE(core, V3D_CTL_INT_CLR, V3D_CORE_IRQS(v3d->ver));
	V3D_WRITE(V3D_HUB_INT_CLR, V3D_HUB_IRQS(v3d->ver));

	cancel_work_sync(&v3d->overflow_mem_work);
}

/** Reinitializes interrupt registers when a GPU reset is performed. */
void v3d_irq_reset(struct v3d_dev *v3d)
{
	v3d_irq_enable(v3d);
}
