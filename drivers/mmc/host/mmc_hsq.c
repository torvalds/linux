// SPDX-License-Identifier: GPL-2.0
/*
 *
 * MMC software queue support based on command queue interfaces
 *
 * Copyright (C) 2019 Linaro, Inc.
 * Author: Baolin Wang <baolin.wang@linaro.org>
 */

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/module.h>

#include "mmc_hsq.h"

#define HSQ_NUM_SLOTS	64
#define HSQ_INVALID_TAG	HSQ_NUM_SLOTS

static void mmc_hsq_retry_handler(struct work_struct *work)
{
	struct mmc_hsq *hsq = container_of(work, struct mmc_hsq, retry_work);
	struct mmc_host *mmc = hsq->mmc;

	mmc->ops->request(mmc, hsq->mrq);
}

static void mmc_hsq_pump_requests(struct mmc_hsq *hsq)
{
	struct mmc_host *mmc = hsq->mmc;
	struct hsq_slot *slot;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&hsq->lock, flags);

	/* Make sure we are not already running a request now */
	if (hsq->mrq || hsq->recovery_halt) {
		spin_unlock_irqrestore(&hsq->lock, flags);
		return;
	}

	/* Make sure there are remain requests need to pump */
	if (!hsq->qcnt || !hsq->enabled) {
		spin_unlock_irqrestore(&hsq->lock, flags);
		return;
	}

	slot = &hsq->slot[hsq->next_tag];
	hsq->mrq = slot->mrq;
	hsq->qcnt--;

	spin_unlock_irqrestore(&hsq->lock, flags);

	if (mmc->ops->request_atomic)
		ret = mmc->ops->request_atomic(mmc, hsq->mrq);
	else
		mmc->ops->request(mmc, hsq->mrq);

	/*
	 * If returning BUSY from request_atomic(), which means the card
	 * may be busy now, and we should change to non-atomic context to
	 * try again for this unusual case, to avoid time-consuming operations
	 * in the atomic context.
	 *
	 * Note: we just give a warning for other error cases, since the host
	 * driver will handle them.
	 */
	if (ret == -EBUSY)
		schedule_work(&hsq->retry_work);
	else
		WARN_ON_ONCE(ret);
}

static void mmc_hsq_update_next_tag(struct mmc_hsq *hsq, int remains)
{
	struct hsq_slot *slot;
	int tag;

	/*
	 * If there are no remain requests in software queue, then set a invalid
	 * tag.
	 */
	if (!remains) {
		hsq->next_tag = HSQ_INVALID_TAG;
		return;
	}

	/*
	 * Increasing the next tag and check if the corresponding request is
	 * available, if yes, then we found a candidate request.
	 */
	if (++hsq->next_tag != HSQ_INVALID_TAG) {
		slot = &hsq->slot[hsq->next_tag];
		if (slot->mrq)
			return;
	}

	/* Othersie we should iterate all slots to find a available tag. */
	for (tag = 0; tag < HSQ_NUM_SLOTS; tag++) {
		slot = &hsq->slot[tag];
		if (slot->mrq)
			break;
	}

	if (tag == HSQ_NUM_SLOTS)
		tag = HSQ_INVALID_TAG;

	hsq->next_tag = tag;
}

static void mmc_hsq_post_request(struct mmc_hsq *hsq)
{
	unsigned long flags;
	int remains;

	spin_lock_irqsave(&hsq->lock, flags);

	remains = hsq->qcnt;
	hsq->mrq = NULL;

	/* Update the next available tag to be queued. */
	mmc_hsq_update_next_tag(hsq, remains);

	if (hsq->waiting_for_idle && !remains) {
		hsq->waiting_for_idle = false;
		wake_up(&hsq->wait_queue);
	}

	/* Do not pump new request in recovery mode. */
	if (hsq->recovery_halt) {
		spin_unlock_irqrestore(&hsq->lock, flags);
		return;
	}

	spin_unlock_irqrestore(&hsq->lock, flags);

	 /*
	  * Try to pump new request to host controller as fast as possible,
	  * after completing previous request.
	  */
	if (remains > 0)
		mmc_hsq_pump_requests(hsq);
}

/**
 * mmc_hsq_finalize_request - finalize one request if the request is done
 * @mmc: the host controller
 * @mrq: the request need to be finalized
 *
 * Return true if we finalized the corresponding request in software queue,
 * otherwise return false.
 */
bool mmc_hsq_finalize_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct mmc_hsq *hsq = mmc->cqe_private;
	unsigned long flags;

	spin_lock_irqsave(&hsq->lock, flags);

	if (!hsq->enabled || !hsq->mrq || hsq->mrq != mrq) {
		spin_unlock_irqrestore(&hsq->lock, flags);
		return false;
	}

	/*
	 * Clear current completed slot request to make a room for new request.
	 */
	hsq->slot[hsq->next_tag].mrq = NULL;

	spin_unlock_irqrestore(&hsq->lock, flags);

	mmc_cqe_request_done(mmc, hsq->mrq);

	mmc_hsq_post_request(hsq);

	return true;
}
EXPORT_SYMBOL_GPL(mmc_hsq_finalize_request);

static void mmc_hsq_recovery_start(struct mmc_host *mmc)
{
	struct mmc_hsq *hsq = mmc->cqe_private;
	unsigned long flags;

	spin_lock_irqsave(&hsq->lock, flags);

	hsq->recovery_halt = true;

	spin_unlock_irqrestore(&hsq->lock, flags);
}

static void mmc_hsq_recovery_finish(struct mmc_host *mmc)
{
	struct mmc_hsq *hsq = mmc->cqe_private;
	int remains;

	spin_lock_irq(&hsq->lock);

	hsq->recovery_halt = false;
	remains = hsq->qcnt;

	spin_unlock_irq(&hsq->lock);

	/*
	 * Try to pump new request if there are request pending in software
	 * queue after finishing recovery.
	 */
	if (remains > 0)
		mmc_hsq_pump_requests(hsq);
}

static int mmc_hsq_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct mmc_hsq *hsq = mmc->cqe_private;
	int tag = mrq->tag;

	spin_lock_irq(&hsq->lock);

	if (!hsq->enabled) {
		spin_unlock_irq(&hsq->lock);
		return -ESHUTDOWN;
	}

	/* Do not queue any new requests in recovery mode. */
	if (hsq->recovery_halt) {
		spin_unlock_irq(&hsq->lock);
		return -EBUSY;
	}

	hsq->slot[tag].mrq = mrq;

	/*
	 * Set the next tag as current request tag if no available
	 * next tag.
	 */
	if (hsq->next_tag == HSQ_INVALID_TAG)
		hsq->next_tag = tag;

	hsq->qcnt++;

	spin_unlock_irq(&hsq->lock);

	mmc_hsq_pump_requests(hsq);

	return 0;
}

static void mmc_hsq_post_req(struct mmc_host *mmc, struct mmc_request *mrq)
{
	if (mmc->ops->post_req)
		mmc->ops->post_req(mmc, mrq, 0);
}

static bool mmc_hsq_queue_is_idle(struct mmc_hsq *hsq, int *ret)
{
	bool is_idle;

	spin_lock_irq(&hsq->lock);

	is_idle = (!hsq->mrq && !hsq->qcnt) ||
		hsq->recovery_halt;

	*ret = hsq->recovery_halt ? -EBUSY : 0;
	hsq->waiting_for_idle = !is_idle;

	spin_unlock_irq(&hsq->lock);

	return is_idle;
}

static int mmc_hsq_wait_for_idle(struct mmc_host *mmc)
{
	struct mmc_hsq *hsq = mmc->cqe_private;
	int ret;

	wait_event(hsq->wait_queue,
		   mmc_hsq_queue_is_idle(hsq, &ret));

	return ret;
}

static void mmc_hsq_disable(struct mmc_host *mmc)
{
	struct mmc_hsq *hsq = mmc->cqe_private;
	u32 timeout = 500;
	int ret;

	spin_lock_irq(&hsq->lock);

	if (!hsq->enabled) {
		spin_unlock_irq(&hsq->lock);
		return;
	}

	spin_unlock_irq(&hsq->lock);

	ret = wait_event_timeout(hsq->wait_queue,
				 mmc_hsq_queue_is_idle(hsq, &ret),
				 msecs_to_jiffies(timeout));
	if (ret == 0) {
		pr_warn("could not stop mmc software queue\n");
		return;
	}

	spin_lock_irq(&hsq->lock);

	hsq->enabled = false;

	spin_unlock_irq(&hsq->lock);
}

static int mmc_hsq_enable(struct mmc_host *mmc, struct mmc_card *card)
{
	struct mmc_hsq *hsq = mmc->cqe_private;

	spin_lock_irq(&hsq->lock);

	if (hsq->enabled) {
		spin_unlock_irq(&hsq->lock);
		return -EBUSY;
	}

	hsq->enabled = true;

	spin_unlock_irq(&hsq->lock);

	return 0;
}

static const struct mmc_cqe_ops mmc_hsq_ops = {
	.cqe_enable = mmc_hsq_enable,
	.cqe_disable = mmc_hsq_disable,
	.cqe_request = mmc_hsq_request,
	.cqe_post_req = mmc_hsq_post_req,
	.cqe_wait_for_idle = mmc_hsq_wait_for_idle,
	.cqe_recovery_start = mmc_hsq_recovery_start,
	.cqe_recovery_finish = mmc_hsq_recovery_finish,
};

int mmc_hsq_init(struct mmc_hsq *hsq, struct mmc_host *mmc)
{
	hsq->num_slots = HSQ_NUM_SLOTS;
	hsq->next_tag = HSQ_INVALID_TAG;

	hsq->slot = devm_kcalloc(mmc_dev(mmc), hsq->num_slots,
				 sizeof(struct hsq_slot), GFP_KERNEL);
	if (!hsq->slot)
		return -ENOMEM;

	hsq->mmc = mmc;
	hsq->mmc->cqe_private = hsq;
	mmc->cqe_ops = &mmc_hsq_ops;

	INIT_WORK(&hsq->retry_work, mmc_hsq_retry_handler);
	spin_lock_init(&hsq->lock);
	init_waitqueue_head(&hsq->wait_queue);

	return 0;
}
EXPORT_SYMBOL_GPL(mmc_hsq_init);

void mmc_hsq_suspend(struct mmc_host *mmc)
{
	mmc_hsq_disable(mmc);
}
EXPORT_SYMBOL_GPL(mmc_hsq_suspend);

int mmc_hsq_resume(struct mmc_host *mmc)
{
	return mmc_hsq_enable(mmc, NULL);
}
EXPORT_SYMBOL_GPL(mmc_hsq_resume);

MODULE_DESCRIPTION("MMC Host Software Queue support");
MODULE_LICENSE("GPL v2");
