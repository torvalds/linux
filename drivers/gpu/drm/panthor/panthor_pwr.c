// SPDX-License-Identifier: GPL-2.0 or MIT
/* Copyright 2025 ARM Limited. All rights reserved. */

#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/wait.h>

#include <drm/drm_managed.h>
#include <drm/drm_print.h>

#include "panthor_device.h"
#include "panthor_hw.h"
#include "panthor_pwr.h"
#include "panthor_regs.h"

#define PWR_INTERRUPTS_MASK \
	(PWR_IRQ_POWER_CHANGED_SINGLE | \
	 PWR_IRQ_POWER_CHANGED_ALL | \
	 PWR_IRQ_DELEGATION_CHANGED | \
	 PWR_IRQ_RESET_COMPLETED | \
	 PWR_IRQ_RETRACT_COMPLETED | \
	 PWR_IRQ_INSPECT_COMPLETED | \
	 PWR_IRQ_COMMAND_NOT_ALLOWED | \
	 PWR_IRQ_COMMAND_INVALID)

/**
 * struct panthor_pwr - PWR_CONTROL block management data.
 */
struct panthor_pwr {
	/** @irq: PWR irq. */
	struct panthor_irq irq;

	/** @reqs_lock: Lock protecting access to pending_reqs. */
	spinlock_t reqs_lock;

	/** @pending_reqs: Pending PWR requests. */
	u32 pending_reqs;

	/** @reqs_acked: PWR request wait queue. */
	wait_queue_head_t reqs_acked;
};

static void panthor_pwr_irq_handler(struct panthor_device *ptdev, u32 status)
{
	spin_lock(&ptdev->pwr->reqs_lock);
	gpu_write(ptdev, PWR_INT_CLEAR, status);

	if (unlikely(status & PWR_IRQ_COMMAND_NOT_ALLOWED))
		drm_err(&ptdev->base, "PWR_IRQ: COMMAND_NOT_ALLOWED");

	if (unlikely(status & PWR_IRQ_COMMAND_INVALID))
		drm_err(&ptdev->base, "PWR_IRQ: COMMAND_INVALID");

	if (status & ptdev->pwr->pending_reqs) {
		ptdev->pwr->pending_reqs &= ~status;
		wake_up_all(&ptdev->pwr->reqs_acked);
	}
	spin_unlock(&ptdev->pwr->reqs_lock);
}
PANTHOR_IRQ_HANDLER(pwr, PWR, panthor_pwr_irq_handler);

void panthor_pwr_unplug(struct panthor_device *ptdev)
{
	unsigned long flags;

	if (!ptdev->pwr)
		return;

	/* Make sure the IRQ handler is not running after that point. */
	panthor_pwr_irq_suspend(&ptdev->pwr->irq);

	/* Wake-up all waiters. */
	spin_lock_irqsave(&ptdev->pwr->reqs_lock, flags);
	ptdev->pwr->pending_reqs = 0;
	wake_up_all(&ptdev->pwr->reqs_acked);
	spin_unlock_irqrestore(&ptdev->pwr->reqs_lock, flags);
}

int panthor_pwr_init(struct panthor_device *ptdev)
{
	struct panthor_pwr *pwr;
	int err, irq;

	if (!panthor_hw_has_pwr_ctrl(ptdev))
		return 0;

	pwr = drmm_kzalloc(&ptdev->base, sizeof(*pwr), GFP_KERNEL);
	if (!pwr)
		return -ENOMEM;

	spin_lock_init(&pwr->reqs_lock);
	init_waitqueue_head(&pwr->reqs_acked);
	ptdev->pwr = pwr;

	irq = platform_get_irq_byname(to_platform_device(ptdev->base.dev), "gpu");
	if (irq < 0)
		return irq;

	err = panthor_request_pwr_irq(ptdev, &pwr->irq, irq, PWR_INTERRUPTS_MASK);
	if (err)
		return err;

	return 0;
}

void panthor_pwr_suspend(struct panthor_device *ptdev)
{
	if (!ptdev->pwr)
		return;

	panthor_pwr_irq_suspend(&ptdev->pwr->irq);
}

void panthor_pwr_resume(struct panthor_device *ptdev)
{
	if (!ptdev->pwr)
		return;

	panthor_pwr_irq_resume(&ptdev->pwr->irq, PWR_INTERRUPTS_MASK);
}
