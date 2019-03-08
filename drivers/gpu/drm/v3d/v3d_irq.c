// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2014-2018 Broadcom */

/**
 * DOC: Interrupt management for the V3D engine
 *
 * When we take a bin, render, or TFU done interrupt, we need to
 * signal the fence for that job so that the scheduler can queue up
 * the next one and unblock any waiters.
 *
 * When we take the binner out of memory interrupt, we need to
 * allocate some new memory and pass it to the binner so that the
 * current job can make progress.
 */

#include "v3d_drv.h"
#include "v3d_regs.h"
#include "v3d_trace.h"

#define V3D_CORE_IRQS ((u32)(V3D_INT_OUTOMEM |	\
			     V3D_INT_FLDONE |	\
			     V3D_INT_FRDONE |	\
			     V3D_INT_GMPV))

#define V3D_HUB_IRQS ((u32)(V3D_HUB_INT_MMU_WRV |	\
			    V3D_HUB_INT_MMU_PTI |	\
			    V3D_HUB_INT_MMU_CAP |	\
			    V3D_HUB_INT_TFUC))

static void
v3d_overflow_mem_work(struct work_struct *work)
{
	struct v3d_dev *v3d =
		container_of(work, struct v3d_dev, overflow_mem_work);
	struct drm_device *dev = &v3d->drm;
	struct v3d_bo *bo = v3d_bo_create(dev, NULL /* XXX: GMP */, 256 * 1024);
	unsigned long irqflags;

	if (IS_ERR(bo)) {
		DRM_ERROR("Couldn't allocate binner overflow mem\n");
		return;
	}

	/* We lost a race, and our work task came in after the bin job
	 * completed and exited.  This can happen because the HW
	 * signals OOM before it's fully OOM, so the binner might just
	 * barely complete.
	 *
	 * If we lose the race and our work task comes in after a new
	 * bin job got scheduled, that's fine.  We'll just give them
	 * some binner pool anyway.
	 */
	spin_lock_irqsave(&v3d->job_lock, irqflags);
	if (!v3d->bin_job) {
		spin_unlock_irqrestore(&v3d->job_lock, irqflags);
		goto out;
	}

	drm_gem_object_get(&bo->base);
	list_add_tail(&bo->unref_head, &v3d->bin_job->unref_list);
	spin_unlock_irqrestore(&v3d->job_lock, irqflags);

	V3D_CORE_WRITE(0, V3D_PTB_BPOA, bo->node.start << PAGE_SHIFT);
	V3D_CORE_WRITE(0, V3D_PTB_BPOS, bo->base.size);

out:
	drm_gem_object_put_unlocked(&bo->base);
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
		 * add more memory.
		 */
		schedule_work(&v3d->overflow_mem_work);
		status = IRQ_HANDLED;
	}

	if (intsts & V3D_INT_FLDONE) {
		struct v3d_fence *fence =
			to_v3d_fence(v3d->bin_job->bin.done_fence);

		trace_v3d_bcl_irq(&v3d->drm, fence->seqno);
		dma_fence_signal(&fence->base);
		status = IRQ_HANDLED;
	}

	if (intsts & V3D_INT_FRDONE) {
		struct v3d_fence *fence =
			to_v3d_fence(v3d->render_job->render.done_fence);

		trace_v3d_rcl_irq(&v3d->drm, fence->seqno);
		dma_fence_signal(&fence->base);
		status = IRQ_HANDLED;
	}

	/* We shouldn't be triggering these if we have GMP in
	 * always-allowed mode.
	 */
	if (intsts & V3D_INT_GMPV)
		dev_err(v3d->dev, "GMP violation\n");

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
		struct v3d_fence *fence =
			to_v3d_fence(v3d->tfu_job->done_fence);

		trace_v3d_tfu_irq(&v3d->drm, fence->seqno);
		dma_fence_signal(&fence->base);
		status = IRQ_HANDLED;
	}

	if (intsts & (V3D_HUB_INT_MMU_WRV |
		      V3D_HUB_INT_MMU_PTI |
		      V3D_HUB_INT_MMU_CAP)) {
		u32 axi_id = V3D_READ(V3D_MMU_VIO_ID);
		u64 vio_addr = (u64)V3D_READ(V3D_MMU_VIO_ADDR) << 8;

		dev_err(v3d->dev, "MMU error from client %d at 0x%08llx%s%s%s\n",
			axi_id, (long long)vio_addr,
			((intsts & V3D_HUB_INT_MMU_WRV) ?
			 ", write violation" : ""),
			((intsts & V3D_HUB_INT_MMU_PTI) ?
			 ", pte invalid" : ""),
			((intsts & V3D_HUB_INT_MMU_CAP) ?
			 ", cap exceeded" : ""));
		status = IRQ_HANDLED;
	}

	return status;
}

int
v3d_irq_init(struct v3d_dev *v3d)
{
	int ret, core;

	INIT_WORK(&v3d->overflow_mem_work, v3d_overflow_mem_work);

	/* Clear any pending interrupts someone might have left around
	 * for us.
	 */
	for (core = 0; core < v3d->cores; core++)
		V3D_CORE_WRITE(core, V3D_CTL_INT_CLR, V3D_CORE_IRQS);
	V3D_WRITE(V3D_HUB_INT_CLR, V3D_HUB_IRQS);

	ret = devm_request_irq(v3d->dev, platform_get_irq(v3d->pdev, 0),
			       v3d_hub_irq, IRQF_SHARED,
			       "v3d_hub", v3d);
	if (ret)
		goto fail;

	ret = devm_request_irq(v3d->dev, platform_get_irq(v3d->pdev, 1),
			       v3d_irq, IRQF_SHARED,
			       "v3d_core0", v3d);
	if (ret)
		goto fail;

	v3d_irq_enable(v3d);
	return 0;

fail:
	if (ret != -EPROBE_DEFER)
		dev_err(v3d->dev, "IRQ setup failed: %d\n", ret);
	return ret;
}

void
v3d_irq_enable(struct v3d_dev *v3d)
{
	int core;

	/* Enable our set of interrupts, masking out any others. */
	for (core = 0; core < v3d->cores; core++) {
		V3D_CORE_WRITE(core, V3D_CTL_INT_MSK_SET, ~V3D_CORE_IRQS);
		V3D_CORE_WRITE(core, V3D_CTL_INT_MSK_CLR, V3D_CORE_IRQS);
	}

	V3D_WRITE(V3D_HUB_INT_MSK_SET, ~V3D_HUB_IRQS);
	V3D_WRITE(V3D_HUB_INT_MSK_CLR, V3D_HUB_IRQS);
}

void
v3d_irq_disable(struct v3d_dev *v3d)
{
	int core;

	/* Disable all interrupts. */
	for (core = 0; core < v3d->cores; core++)
		V3D_CORE_WRITE(core, V3D_CTL_INT_MSK_SET, ~0);
	V3D_WRITE(V3D_HUB_INT_MSK_SET, ~0);

	/* Clear any pending interrupts we might have left. */
	for (core = 0; core < v3d->cores; core++)
		V3D_CORE_WRITE(core, V3D_CTL_INT_CLR, V3D_CORE_IRQS);
	V3D_WRITE(V3D_HUB_INT_CLR, V3D_HUB_IRQS);

	cancel_work_sync(&v3d->overflow_mem_work);
}

/** Reinitializes interrupt registers when a GPU reset is performed. */
void v3d_irq_reset(struct v3d_dev *v3d)
{
	v3d_irq_enable(v3d);
}
