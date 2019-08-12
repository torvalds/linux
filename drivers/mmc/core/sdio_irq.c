// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * linux/drivers/mmc/core/sdio_irq.c
 *
 * Author:      Nicolas Pitre
 * Created:     June 18, 2007
 * Copyright:   MontaVista Software Inc.
 *
 * Copyright 2008 Pierre Ossman
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <uapi/linux/sched/types.h>
#include <linux/kthread.h>
#include <linux/export.h>
#include <linux/wait.h>
#include <linux/delay.h>

#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>

#include "sdio_ops.h"
#include "core.h"
#include "card.h"

static int process_sdio_pending_irqs(struct mmc_host *host)
{
	struct mmc_card *card = host->card;
	int i, ret, count;
	unsigned char pending;
	struct sdio_func *func;

	/* Don't process SDIO IRQs if the card is suspended. */
	if (mmc_card_suspended(card))
		return 0;

	/*
	 * Optimization, if there is only 1 function interrupt registered
	 * and we know an IRQ was signaled then call irq handler directly.
	 * Otherwise do the full probe.
	 */
	func = card->sdio_single_irq;
	if (func && host->sdio_irq_pending) {
		func->irq_handler(func);
		return 1;
	}

	ret = mmc_io_rw_direct(card, 0, 0, SDIO_CCCR_INTx, 0, &pending);
	if (ret) {
		pr_debug("%s: error %d reading SDIO_CCCR_INTx\n",
		       mmc_card_id(card), ret);
		return ret;
	}

	if (pending && mmc_card_broken_irq_polling(card) &&
	    !(host->caps & MMC_CAP_SDIO_IRQ)) {
		unsigned char dummy;

		/* A fake interrupt could be created when we poll SDIO_CCCR_INTx
		 * register with a Marvell SD8797 card. A dummy CMD52 read to
		 * function 0 register 0xff can avoid this.
		 */
		mmc_io_rw_direct(card, 0, 0, 0xff, 0, &dummy);
	}

	count = 0;
	for (i = 1; i <= 7; i++) {
		if (pending & (1 << i)) {
			func = card->sdio_func[i - 1];
			if (!func) {
				pr_warn("%s: pending IRQ for non-existent function\n",
					mmc_card_id(card));
				ret = -EINVAL;
			} else if (func->irq_handler) {
				func->irq_handler(func);
				count++;
			} else {
				pr_warn("%s: pending IRQ with no handler\n",
					sdio_func_id(func));
				ret = -EINVAL;
			}
		}
	}

	if (count)
		return count;

	return ret;
}

static void sdio_run_irqs(struct mmc_host *host)
{
	mmc_claim_host(host);
	if (host->sdio_irqs) {
		host->sdio_irq_pending = true;
		process_sdio_pending_irqs(host);
		if (host->ops->ack_sdio_irq)
			host->ops->ack_sdio_irq(host);
	}
	mmc_release_host(host);
}

void sdio_irq_work(struct work_struct *work)
{
	struct mmc_host *host =
		container_of(work, struct mmc_host, sdio_irq_work.work);

	sdio_run_irqs(host);
}

void sdio_signal_irq(struct mmc_host *host)
{
	queue_delayed_work(system_wq, &host->sdio_irq_work, 0);
}
EXPORT_SYMBOL_GPL(sdio_signal_irq);

static int sdio_irq_thread(void *_host)
{
	struct mmc_host *host = _host;
	struct sched_param param = { .sched_priority = 1 };
	unsigned long period, idle_period;
	int ret;

	sched_setscheduler(current, SCHED_FIFO, &param);

	/*
	 * We want to allow for SDIO cards to work even on non SDIO
	 * aware hosts.  One thing that non SDIO host cannot do is
	 * asynchronous notification of pending SDIO card interrupts
	 * hence we poll for them in that case.
	 */
	idle_period = msecs_to_jiffies(10);
	period = (host->caps & MMC_CAP_SDIO_IRQ) ?
		MAX_SCHEDULE_TIMEOUT : idle_period;

	pr_debug("%s: IRQ thread started (poll period = %lu jiffies)\n",
		 mmc_hostname(host), period);

	do {
		/*
		 * We claim the host here on drivers behalf for a couple
		 * reasons:
		 *
		 * 1) it is already needed to retrieve the CCCR_INTx;
		 * 2) we want the driver(s) to clear the IRQ condition ASAP;
		 * 3) we need to control the abort condition locally.
		 *
		 * Just like traditional hard IRQ handlers, we expect SDIO
		 * IRQ handlers to be quick and to the point, so that the
		 * holding of the host lock does not cover too much work
		 * that doesn't require that lock to be held.
		 */
		ret = __mmc_claim_host(host, NULL,
				       &host->sdio_irq_thread_abort);
		if (ret)
			break;
		ret = process_sdio_pending_irqs(host);
		host->sdio_irq_pending = false;
		mmc_release_host(host);

		/*
		 * Give other threads a chance to run in the presence of
		 * errors.
		 */
		if (ret < 0) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (!kthread_should_stop())
				schedule_timeout(HZ);
			set_current_state(TASK_RUNNING);
		}

		/*
		 * Adaptive polling frequency based on the assumption
		 * that an interrupt will be closely followed by more.
		 * This has a substantial benefit for network devices.
		 */
		if (!(host->caps & MMC_CAP_SDIO_IRQ)) {
			if (ret > 0)
				period /= 2;
			else {
				period++;
				if (period > idle_period)
					period = idle_period;
			}
		}

		set_current_state(TASK_INTERRUPTIBLE);
		if (host->caps & MMC_CAP_SDIO_IRQ)
			host->ops->enable_sdio_irq(host, 1);
		if (!kthread_should_stop())
			schedule_timeout(period);
		set_current_state(TASK_RUNNING);
	} while (!kthread_should_stop());

	if (host->caps & MMC_CAP_SDIO_IRQ)
		host->ops->enable_sdio_irq(host, 0);

	pr_debug("%s: IRQ thread exiting with code %d\n",
		 mmc_hostname(host), ret);

	return ret;
}

static int sdio_card_irq_get(struct mmc_card *card)
{
	struct mmc_host *host = card->host;

	WARN_ON(!host->claimed);

	if (!host->sdio_irqs++) {
		if (!(host->caps2 & MMC_CAP2_SDIO_IRQ_NOTHREAD)) {
			atomic_set(&host->sdio_irq_thread_abort, 0);
			host->sdio_irq_thread =
				kthread_run(sdio_irq_thread, host,
					    "ksdioirqd/%s", mmc_hostname(host));
			if (IS_ERR(host->sdio_irq_thread)) {
				int err = PTR_ERR(host->sdio_irq_thread);
				host->sdio_irqs--;
				return err;
			}
		} else if (host->caps & MMC_CAP_SDIO_IRQ) {
			host->ops->enable_sdio_irq(host, 1);
		}
	}

	return 0;
}

static int sdio_card_irq_put(struct mmc_card *card)
{
	struct mmc_host *host = card->host;

	WARN_ON(!host->claimed);

	if (host->sdio_irqs < 1)
		return -EINVAL;

	if (!--host->sdio_irqs) {
		if (!(host->caps2 & MMC_CAP2_SDIO_IRQ_NOTHREAD)) {
			atomic_set(&host->sdio_irq_thread_abort, 1);
			kthread_stop(host->sdio_irq_thread);
		} else if (host->caps & MMC_CAP_SDIO_IRQ) {
			host->ops->enable_sdio_irq(host, 0);
		}
	}

	return 0;
}

/* If there is only 1 function registered set sdio_single_irq */
static void sdio_single_irq_set(struct mmc_card *card)
{
	struct sdio_func *func;
	int i;

	card->sdio_single_irq = NULL;
	if ((card->host->caps & MMC_CAP_SDIO_IRQ) &&
	    card->host->sdio_irqs == 1)
		for (i = 0; i < card->sdio_funcs; i++) {
		       func = card->sdio_func[i];
		       if (func && func->irq_handler) {
			       card->sdio_single_irq = func;
			       break;
		       }
	       }
}

/**
 *	sdio_claim_irq - claim the IRQ for a SDIO function
 *	@func: SDIO function
 *	@handler: IRQ handler callback
 *
 *	Claim and activate the IRQ for the given SDIO function. The provided
 *	handler will be called when that IRQ is asserted.  The host is always
 *	claimed already when the handler is called so the handler should not
 *	call sdio_claim_host() or sdio_release_host().
 */
int sdio_claim_irq(struct sdio_func *func, sdio_irq_handler_t *handler)
{
	int ret;
	unsigned char reg;

	if (!func)
		return -EINVAL;

	pr_debug("SDIO: Enabling IRQ for %s...\n", sdio_func_id(func));

	if (func->irq_handler) {
		pr_debug("SDIO: IRQ for %s already in use.\n", sdio_func_id(func));
		return -EBUSY;
	}

	ret = mmc_io_rw_direct(func->card, 0, 0, SDIO_CCCR_IENx, 0, &reg);
	if (ret)
		return ret;

	reg |= 1 << func->num;

	reg |= 1; /* Master interrupt enable */

	ret = mmc_io_rw_direct(func->card, 1, 0, SDIO_CCCR_IENx, reg, NULL);
	if (ret)
		return ret;

	func->irq_handler = handler;
	ret = sdio_card_irq_get(func->card);
	if (ret)
		func->irq_handler = NULL;
	sdio_single_irq_set(func->card);

	return ret;
}
EXPORT_SYMBOL_GPL(sdio_claim_irq);

/**
 *	sdio_release_irq - release the IRQ for a SDIO function
 *	@func: SDIO function
 *
 *	Disable and release the IRQ for the given SDIO function.
 */
int sdio_release_irq(struct sdio_func *func)
{
	int ret;
	unsigned char reg;

	if (!func)
		return -EINVAL;

	pr_debug("SDIO: Disabling IRQ for %s...\n", sdio_func_id(func));

	if (func->irq_handler) {
		func->irq_handler = NULL;
		sdio_card_irq_put(func->card);
		sdio_single_irq_set(func->card);
	}

	ret = mmc_io_rw_direct(func->card, 0, 0, SDIO_CCCR_IENx, 0, &reg);
	if (ret)
		return ret;

	reg &= ~(1 << func->num);

	/* Disable master interrupt with the last function interrupt */
	if (!(reg & 0xFE))
		reg = 0;

	ret = mmc_io_rw_direct(func->card, 1, 0, SDIO_CCCR_IENx, reg, NULL);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(sdio_release_irq);

