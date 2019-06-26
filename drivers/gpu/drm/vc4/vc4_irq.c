/*
 * Copyright © 2014 Broadcom
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
 */

/**
 * DOC: Interrupt management for the V3D engine
 *
 * We have an interrupt status register (V3D_INTCTL) which reports
 * interrupts, and where writing 1 bits clears those interrupts.
 * There are also a pair of interrupt registers
 * (V3D_INTENA/V3D_INTDIS) where writing a 1 to their bits enables or
 * disables that specific interrupt, and 0s written are ignored
 * (reading either one returns the set of enabled interrupts).
 *
 * When we take a binning flush done interrupt, we need to submit the
 * next frame for binning and move the finished frame to the render
 * thread.
 *
 * When we take a render frame interrupt, we need to wake the
 * processes waiting for some frame to be done, and get the next frame
 * submitted ASAP (so the hardware doesn't sit idle when there's work
 * to do).
 *
 * When we take the binner out of memory interrupt, we need to
 * allocate some new memory and pass it to the binner so that the
 * current job can make progress.
 */

#include "vc4_drv.h"
#include "vc4_regs.h"

#define V3D_DRIVER_IRQS (V3D_INT_OUTOMEM | \
			 V3D_INT_FLDONE | \
			 V3D_INT_FRDONE)

DECLARE_WAIT_QUEUE_HEAD(render_wait);

static void
vc4_overflow_mem_work(struct work_struct *work)
{
	struct vc4_dev *vc4 =
		container_of(work, struct vc4_dev, overflow_mem_work);
	struct vc4_bo *bo;
	int bin_bo_slot;
	struct vc4_exec_info *exec;
	unsigned long irqflags;

	mutex_lock(&vc4->bin_bo_lock);

	if (!vc4->bin_bo)
		goto complete;

	bo = vc4->bin_bo;

	bin_bo_slot = vc4_v3d_get_bin_slot(vc4);
	if (bin_bo_slot < 0) {
		DRM_ERROR("Couldn't allocate binner overflow mem\n");
		goto complete;
	}

	spin_lock_irqsave(&vc4->job_lock, irqflags);

	if (vc4->bin_alloc_overflow) {
		/* If we had overflow memory allocated previously,
		 * then that chunk will free when the current bin job
		 * is done.  If we don't have a bin job running, then
		 * the chunk will be done whenever the list of render
		 * jobs has drained.
		 */
		exec = vc4_first_bin_job(vc4);
		if (!exec)
			exec = vc4_last_render_job(vc4);
		if (exec) {
			exec->bin_slots |= vc4->bin_alloc_overflow;
		} else {
			/* There's nothing queued in the hardware, so
			 * the old slot is free immediately.
			 */
			vc4->bin_alloc_used &= ~vc4->bin_alloc_overflow;
		}
	}
	vc4->bin_alloc_overflow = BIT(bin_bo_slot);

	V3D_WRITE(V3D_BPOA, bo->base.paddr + bin_bo_slot * vc4->bin_alloc_size);
	V3D_WRITE(V3D_BPOS, bo->base.base.size);
	V3D_WRITE(V3D_INTCTL, V3D_INT_OUTOMEM);
	V3D_WRITE(V3D_INTENA, V3D_INT_OUTOMEM);
	spin_unlock_irqrestore(&vc4->job_lock, irqflags);

complete:
	mutex_unlock(&vc4->bin_bo_lock);
}

static void
vc4_irq_finish_bin_job(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_exec_info *next, *exec = vc4_first_bin_job(vc4);

	if (!exec)
		return;

	vc4_move_job_to_render(dev, exec);
	next = vc4_first_bin_job(vc4);

	/* Only submit the next job in the bin list if it matches the perfmon
	 * attached to the one that just finished (or if both jobs don't have
	 * perfmon attached to them).
	 */
	if (next && next->perfmon == exec->perfmon)
		vc4_submit_next_bin_job(dev);
}

static void
vc4_cancel_bin_job(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_exec_info *exec = vc4_first_bin_job(vc4);

	if (!exec)
		return;

	/* Stop the perfmon so that the next bin job can be started. */
	if (exec->perfmon)
		vc4_perfmon_stop(vc4, exec->perfmon, false);

	list_move_tail(&exec->head, &vc4->bin_job_list);
	vc4_submit_next_bin_job(dev);
}

static void
vc4_irq_finish_render_job(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_exec_info *exec = vc4_first_render_job(vc4);
	struct vc4_exec_info *nextbin, *nextrender;

	if (!exec)
		return;

	vc4->finished_seqno++;
	list_move_tail(&exec->head, &vc4->job_done_list);

	nextbin = vc4_first_bin_job(vc4);
	nextrender = vc4_first_render_job(vc4);

	/* Only stop the perfmon if following jobs in the queue don't expect it
	 * to be enabled.
	 */
	if (exec->perfmon && !nextrender &&
	    (!nextbin || nextbin->perfmon != exec->perfmon))
		vc4_perfmon_stop(vc4, exec->perfmon, true);

	/* If there's a render job waiting, start it. If this is not the case
	 * we may have to unblock the binner if it's been stalled because of
	 * perfmon (this can be checked by comparing the perfmon attached to
	 * the finished renderjob to the one attached to the next bin job: if
	 * they don't match, this means the binner is stalled and should be
	 * restarted).
	 */
	if (nextrender)
		vc4_submit_next_render_job(dev);
	else if (nextbin && nextbin->perfmon != exec->perfmon)
		vc4_submit_next_bin_job(dev);

	if (exec->fence) {
		dma_fence_signal_locked(exec->fence);
		dma_fence_put(exec->fence);
		exec->fence = NULL;
	}

	wake_up_all(&vc4->job_wait_queue);
	schedule_work(&vc4->job_done_work);
}

irqreturn_t
vc4_irq(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	uint32_t intctl;
	irqreturn_t status = IRQ_NONE;

	barrier();
	intctl = V3D_READ(V3D_INTCTL);

	/* Acknowledge the interrupts we're handling here. The binner
	 * last flush / render frame done interrupt will be cleared,
	 * while OUTOMEM will stay high until the underlying cause is
	 * cleared.
	 */
	V3D_WRITE(V3D_INTCTL, intctl);

	if (intctl & V3D_INT_OUTOMEM) {
		/* Disable OUTOMEM until the work is done. */
		V3D_WRITE(V3D_INTDIS, V3D_INT_OUTOMEM);
		schedule_work(&vc4->overflow_mem_work);
		status = IRQ_HANDLED;
	}

	if (intctl & V3D_INT_FLDONE) {
		spin_lock(&vc4->job_lock);
		vc4_irq_finish_bin_job(dev);
		spin_unlock(&vc4->job_lock);
		status = IRQ_HANDLED;
	}

	if (intctl & V3D_INT_FRDONE) {
		spin_lock(&vc4->job_lock);
		vc4_irq_finish_render_job(dev);
		spin_unlock(&vc4->job_lock);
		status = IRQ_HANDLED;
	}

	return status;
}

void
vc4_irq_preinstall(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	if (!vc4->v3d)
		return;

	init_waitqueue_head(&vc4->job_wait_queue);
	INIT_WORK(&vc4->overflow_mem_work, vc4_overflow_mem_work);

	/* Clear any pending interrupts someone might have left around
	 * for us.
	 */
	V3D_WRITE(V3D_INTCTL, V3D_DRIVER_IRQS);
}

int
vc4_irq_postinstall(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	if (!vc4->v3d)
		return 0;

	/* Enable the render done interrupts. The out-of-memory interrupt is
	 * enabled as soon as we have a binner BO allocated.
	 */
	V3D_WRITE(V3D_INTENA, V3D_INT_FLDONE | V3D_INT_FRDONE);

	return 0;
}

void
vc4_irq_uninstall(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	if (!vc4->v3d)
		return;

	/* Disable sending interrupts for our driver's IRQs. */
	V3D_WRITE(V3D_INTDIS, V3D_DRIVER_IRQS);

	/* Clear any pending interrupts we might have left. */
	V3D_WRITE(V3D_INTCTL, V3D_DRIVER_IRQS);

	/* Finish any interrupt handler still in flight. */
	disable_irq(dev->irq);

	cancel_work_sync(&vc4->overflow_mem_work);
}

/** Reinitializes interrupt registers when a GPU reset is performed. */
void vc4_irq_reset(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	unsigned long irqflags;

	/* Acknowledge any stale IRQs. */
	V3D_WRITE(V3D_INTCTL, V3D_DRIVER_IRQS);

	/*
	 * Turn all our interrupts on.  Binner out of memory is the
	 * only one we expect to trigger at this point, since we've
	 * just come from poweron and haven't supplied any overflow
	 * memory yet.
	 */
	V3D_WRITE(V3D_INTENA, V3D_DRIVER_IRQS);

	spin_lock_irqsave(&vc4->job_lock, irqflags);
	vc4_cancel_bin_job(dev);
	vc4_irq_finish_render_job(dev);
	spin_unlock_irqrestore(&vc4->job_lock, irqflags);
}
