// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2026 Morse Micro
 */
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include "hif.h"
#include "skbq.h"
#include "mac.h"
#include "bus.h"
#include "ps.h"

static void mm81x_ps_wakeup(struct mm81x_ps *mps)
{
	struct mm81x *mors = container_of(mps, struct mm81x, ps);

	if (!mps->enable || !mps->suspended)
		return;

	mm81x_set_bus_enable(mors, true);
	mps->suspended = false;
}

static void mm81x_ps_sleep(struct mm81x_ps *mps)
{
	struct mm81x *mors = container_of(mps, struct mm81x, ps);

	if (!mps->enable || mps->suspended)
		return;

	mps->suspended = true;
	mm81x_set_bus_enable(mors, false);
}

static void mm81x_ps_evaluate(struct mm81x_ps *mps)
{
	struct mm81x *mors = container_of(mps, struct mm81x, ps);
	bool needs_wake = false;
	unsigned long flags_on_entry =
		(mors->hif.event_flags &
		 ~BIT(MM81X_HIF_EVT_DATA_TRAFFIC_PAUSE_PEND));

	if (!mps->enable)
		return;

	needs_wake = (mps->wakers > 0);
	needs_wake |= (flags_on_entry > 0);
	needs_wake |= (mm81x_hif_get_tx_buffered_count(mors) > 0);

	if (needs_wake) {
		mm81x_ps_wakeup(mps);
		return;
	}

	mm81x_ps_sleep(mps);
}

static void mm81x_ps_evaluate_work(struct work_struct *work)
{
	struct mm81x_ps *mps =
		container_of(work, struct mm81x_ps, delayed_eval_work.work);

	if (mps->enable) {
		mutex_lock(&mps->lock);
		mm81x_ps_evaluate(mps);
		mutex_unlock(&mps->lock);
	}
}

void mm81x_ps_enable(struct mm81x *mors)
{
	struct mm81x_ps *mps = &mors->ps;

	if (mps->enable) {
		mutex_lock(&mps->lock);
		if (mps->wakers == 0) {
			WARN_ON_ONCE(1);
		} else {
			mps->wakers--;
			mm81x_ps_evaluate(mps);
		}
		mutex_unlock(&mps->lock);
	}
}

void mm81x_ps_disable(struct mm81x *mors)
{
	struct mm81x_ps *mps = &mors->ps;

	if (mps->enable) {
		mutex_lock(&mps->lock);
		mps->wakers++;
		mm81x_ps_evaluate(mps);
		mutex_unlock(&mps->lock);
	}
}

int mm81x_ps_init(struct mm81x *mors)
{
	struct mm81x_ps *mps = &mors->ps;

	mps->enable = (mors->bus_type == MM81X_BUS_TYPE_USB);
	mps->suspended = true;
	mps->wakers = 1; /* we default to being on */
	mutex_init(&mps->lock);
	INIT_DELAYED_WORK(&mps->delayed_eval_work, mm81x_ps_evaluate_work);

	return 0;
}

void mm81x_ps_finish(struct mm81x *mors)
{
	struct mm81x_ps *mps = &mors->ps;

	if (mps->enable) {
		mps->enable = false;
		cancel_delayed_work_sync(&mps->delayed_eval_work);
	}
}
