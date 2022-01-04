// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2021 ROHM Semiconductors
// regulator IRQ based event notification helpers
//
// Logic has been partially adapted from qcom-labibb driver.
//
// Author: Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>

#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/regulator/driver.h>

#include "internal.h"

#define REGULATOR_FORCED_SAFETY_SHUTDOWN_WAIT_MS 10000

struct regulator_irq {
	struct regulator_irq_data rdata;
	struct regulator_irq_desc desc;
	int irq;
	int retry_cnt;
	struct delayed_work isr_work;
};

/*
 * Should only be called from threaded handler to prevent potential deadlock
 */
static void rdev_flag_err(struct regulator_dev *rdev, int err)
{
	spin_lock(&rdev->err_lock);
	rdev->cached_err |= err;
	spin_unlock(&rdev->err_lock);
}

static void rdev_clear_err(struct regulator_dev *rdev, int err)
{
	spin_lock(&rdev->err_lock);
	rdev->cached_err &= ~err;
	spin_unlock(&rdev->err_lock);
}

static void regulator_notifier_isr_work(struct work_struct *work)
{
	struct regulator_irq *h;
	struct regulator_irq_desc *d;
	struct regulator_irq_data *rid;
	int ret = 0;
	int tmo, i;
	int num_rdevs;

	h = container_of(work, struct regulator_irq,
			    isr_work.work);
	d = &h->desc;
	rid = &h->rdata;
	num_rdevs = rid->num_states;

reread:
	if (d->fatal_cnt && h->retry_cnt > d->fatal_cnt) {
		if (!d->die)
			return hw_protection_shutdown("Regulator HW failure? - no IC recovery",
						      REGULATOR_FORCED_SAFETY_SHUTDOWN_WAIT_MS);
		ret = d->die(rid);
		/*
		 * If the 'last resort' IC recovery failed we will have
		 * nothing else left to do...
		 */
		if (ret)
			return hw_protection_shutdown("Regulator HW failure. IC recovery failed",
						      REGULATOR_FORCED_SAFETY_SHUTDOWN_WAIT_MS);

		/*
		 * If h->die() was implemented we assume recovery has been
		 * attempted (probably regulator was shut down) and we
		 * just enable IRQ and bail-out.
		 */
		goto enable_out;
	}
	if (d->renable) {
		ret = d->renable(rid);

		if (ret == REGULATOR_FAILED_RETRY) {
			/* Driver could not get current status */
			h->retry_cnt++;
			if (!d->reread_ms)
				goto reread;

			tmo = d->reread_ms;
			goto reschedule;
		}

		if (ret) {
			/*
			 * IC status reading succeeded. update error info
			 * just in case the renable changed it.
			 */
			for (i = 0; i < num_rdevs; i++) {
				struct regulator_err_state *stat;
				struct regulator_dev *rdev;

				stat = &rid->states[i];
				rdev = stat->rdev;
				rdev_clear_err(rdev, (~stat->errors) &
						      stat->possible_errs);
			}
			h->retry_cnt++;
			/*
			 * The IC indicated problem is still ON - no point in
			 * re-enabling the IRQ. Retry later.
			 */
			tmo = d->irq_off_ms;
			goto reschedule;
		}
	}

	/*
	 * Either IC reported problem cleared or no status checker was provided.
	 * If problems are gone - good. If not - then the IRQ will fire again
	 * and we'll have a new nice loop. In any case we should clear error
	 * flags here and re-enable IRQs.
	 */
	for (i = 0; i < num_rdevs; i++) {
		struct regulator_err_state *stat;
		struct regulator_dev *rdev;

		stat = &rid->states[i];
		rdev = stat->rdev;
		rdev_clear_err(rdev, stat->possible_errs);
	}

	/*
	 * Things have been seemingly successful => zero retry-counter.
	 */
	h->retry_cnt = 0;

enable_out:
	enable_irq(h->irq);

	return;

reschedule:
	if (!d->high_prio)
		mod_delayed_work(system_wq, &h->isr_work,
				 msecs_to_jiffies(tmo));
	else
		mod_delayed_work(system_highpri_wq, &h->isr_work,
				 msecs_to_jiffies(tmo));
}

static irqreturn_t regulator_notifier_isr(int irq, void *data)
{
	struct regulator_irq *h = data;
	struct regulator_irq_desc *d;
	struct regulator_irq_data *rid;
	unsigned long rdev_map = 0;
	int num_rdevs;
	int ret, i;

	d = &h->desc;
	rid = &h->rdata;
	num_rdevs = rid->num_states;

	if (d->fatal_cnt)
		h->retry_cnt++;

	/*
	 * we spare a few cycles by not clearing statuses prior to this call.
	 * The IC driver must initialize the status buffers for rdevs
	 * which it indicates having active events via rdev_map.
	 *
	 * Maybe we should just to be on a safer side(?)
	 */
	ret = d->map_event(irq, rid, &rdev_map);

	/*
	 * If status reading fails (which is unlikely) we don't ack/disable
	 * IRQ but just increase fail count and retry when IRQ fires again.
	 * If retry_count exceeds the given safety limit we call IC specific die
	 * handler which can try disabling regulator(s).
	 *
	 * If no die handler is given we will just power-off as a last resort.
	 *
	 * We could try disabling all associated rdevs - but we might shoot
	 * ourselves in the head and leave the problematic regulator enabled. So
	 * if IC has no die-handler populated we just assume the regulator
	 * can't be disabled.
	 */
	if (unlikely(ret == REGULATOR_FAILED_RETRY))
		goto fail_out;

	h->retry_cnt = 0;
	/*
	 * Let's not disable IRQ if there were no status bits for us. We'd
	 * better leave spurious IRQ handling to genirq
	 */
	if (ret || !rdev_map)
		return IRQ_NONE;

	/*
	 * Some events are bogus if the regulator is disabled. Skip such events
	 * if all relevant regulators are disabled
	 */
	if (d->skip_off) {
		for_each_set_bit(i, &rdev_map, num_rdevs) {
			struct regulator_dev *rdev;
			const struct regulator_ops *ops;

			rdev = rid->states[i].rdev;
			ops = rdev->desc->ops;

			/*
			 * If any of the flagged regulators is enabled we do
			 * handle this
			 */
			if (ops->is_enabled(rdev))
				break;
		}
		if (i == num_rdevs)
			return IRQ_NONE;
	}

	/* Disable IRQ if HW keeps line asserted */
	if (d->irq_off_ms)
		disable_irq_nosync(irq);

	/*
	 * IRQ seems to be for us. Let's fire correct notifiers / store error
	 * flags
	 */
	for_each_set_bit(i, &rdev_map, num_rdevs) {
		struct regulator_err_state *stat;
		struct regulator_dev *rdev;

		stat = &rid->states[i];
		rdev = stat->rdev;

		rdev_dbg(rdev, "Sending regulator notification EVT 0x%lx\n",
			 stat->notifs);

		regulator_notifier_call_chain(rdev, stat->notifs, NULL);
		rdev_flag_err(rdev, stat->errors);
	}

	if (d->irq_off_ms) {
		if (!d->high_prio)
			schedule_delayed_work(&h->isr_work,
					      msecs_to_jiffies(d->irq_off_ms));
		else
			mod_delayed_work(system_highpri_wq,
					 &h->isr_work,
					 msecs_to_jiffies(d->irq_off_ms));
	}

	return IRQ_HANDLED;

fail_out:
	if (d->fatal_cnt && h->retry_cnt > d->fatal_cnt) {
		/* If we have no recovery, just try shut down straight away */
		if (!d->die) {
			hw_protection_shutdown("Regulator failure. Retry count exceeded",
					       REGULATOR_FORCED_SAFETY_SHUTDOWN_WAIT_MS);
		} else {
			ret = d->die(rid);
			/* If die() failed shut down as a last attempt to save the HW */
			if (ret)
				hw_protection_shutdown("Regulator failure. Recovery failed",
						       REGULATOR_FORCED_SAFETY_SHUTDOWN_WAIT_MS);
		}
	}

	return IRQ_NONE;
}

static int init_rdev_state(struct device *dev, struct regulator_irq *h,
			   struct regulator_dev **rdev, int common_err,
			   int *rdev_err, int rdev_amount)
{
	int i;

	h->rdata.states = devm_kzalloc(dev, sizeof(*h->rdata.states) *
				       rdev_amount, GFP_KERNEL);
	if (!h->rdata.states)
		return -ENOMEM;

	h->rdata.num_states = rdev_amount;
	h->rdata.data = h->desc.data;

	for (i = 0; i < rdev_amount; i++) {
		h->rdata.states[i].possible_errs = common_err;
		if (rdev_err)
			h->rdata.states[i].possible_errs |= *rdev_err++;
		h->rdata.states[i].rdev = *rdev++;
	}

	return 0;
}

static void init_rdev_errors(struct regulator_irq *h)
{
	int i;

	for (i = 0; i < h->rdata.num_states; i++)
		if (h->rdata.states[i].possible_errs)
			h->rdata.states[i].rdev->use_cached_err = true;
}

/**
 * regulator_irq_helper - register IRQ based regulator event/error notifier
 *
 * @dev:		device providing the IRQs
 * @d:			IRQ helper descriptor.
 * @irq:		IRQ used to inform events/errors to be notified.
 * @irq_flags:		Extra IRQ flags to be OR'ed with the default
 *			IRQF_ONESHOT when requesting the (threaded) irq.
 * @common_errs:	Errors which can be flagged by this IRQ for all rdevs.
 *			When IRQ is re-enabled these errors will be cleared
 *			from all associated regulators
 * @per_rdev_errs:	Optional error flag array describing errors specific
 *			for only some of the regulators. These errors will be
 *			or'ed with common errors. If this is given the array
 *			should contain rdev_amount flags. Can be set to NULL
 *			if there is no regulator specific error flags for this
 *			IRQ.
 * @rdev:		Array of pointers to regulators associated with this
 *			IRQ.
 * @rdev_amount:	Amount of regulators associated with this IRQ.
 *
 * Return: handle to irq_helper or an ERR_PTR() encoded error code.
 */
void *regulator_irq_helper(struct device *dev,
			   const struct regulator_irq_desc *d, int irq,
			   int irq_flags, int common_errs, int *per_rdev_errs,
			   struct regulator_dev **rdev, int rdev_amount)
{
	struct regulator_irq *h;
	int ret;

	if (!rdev_amount || !d || !d->map_event || !d->name)
		return ERR_PTR(-EINVAL);

	h = devm_kzalloc(dev, sizeof(*h), GFP_KERNEL);
	if (!h)
		return ERR_PTR(-ENOMEM);

	h->irq = irq;
	h->desc = *d;

	ret = init_rdev_state(dev, h, rdev, common_errs, per_rdev_errs,
			      rdev_amount);
	if (ret)
		return ERR_PTR(ret);

	init_rdev_errors(h);

	if (h->desc.irq_off_ms)
		INIT_DELAYED_WORK(&h->isr_work, regulator_notifier_isr_work);

	ret = request_threaded_irq(h->irq, NULL, regulator_notifier_isr,
				   IRQF_ONESHOT | irq_flags, h->desc.name, h);
	if (ret) {
		dev_err(dev, "Failed to request IRQ %d\n", irq);

		return ERR_PTR(ret);
	}

	return h;
}
EXPORT_SYMBOL_GPL(regulator_irq_helper);

/**
 * regulator_irq_helper_cancel - drop IRQ based regulator event/error notifier
 *
 * @handle:		Pointer to handle returned by a successful call to
 *			regulator_irq_helper(). Will be NULLed upon return.
 *
 * The associated IRQ is released and work is cancelled when the function
 * returns.
 */
void regulator_irq_helper_cancel(void **handle)
{
	if (handle && *handle) {
		struct regulator_irq *h = *handle;

		free_irq(h->irq, h);
		if (h->desc.irq_off_ms)
			cancel_delayed_work_sync(&h->isr_work);

		h = NULL;
	}
}
EXPORT_SYMBOL_GPL(regulator_irq_helper_cancel);
