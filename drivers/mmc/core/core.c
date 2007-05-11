/*
 *  linux/drivers/mmc/core/core.c
 *
 *  Copyright (C) 2003-2004 Russell King, All Rights Reserved.
 *  SD support Copyright (C) 2004 Ian Molton, All Rights Reserved.
 *  Copyright (C) 2005-2007 Pierre Ossman, All Rights Reserved.
 *  MMCv4 support Copyright (C) 2006 Philip Langdale, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/pagemap.h>
#include <linux/err.h>
#include <asm/scatterlist.h>
#include <linux/scatterlist.h>

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>

#include "core.h"
#include "sysfs.h"

#include "mmc_ops.h"
#include "sd_ops.h"

extern int mmc_attach_mmc(struct mmc_host *host, u32 ocr);
extern int mmc_attach_sd(struct mmc_host *host, u32 ocr);

/**
 *	mmc_request_done - finish processing an MMC request
 *	@host: MMC host which completed request
 *	@mrq: MMC request which request
 *
 *	MMC drivers should call this function when they have completed
 *	their processing of a request.
 */
void mmc_request_done(struct mmc_host *host, struct mmc_request *mrq)
{
	struct mmc_command *cmd = mrq->cmd;
	int err = cmd->error;

	pr_debug("%s: req done (CMD%u): %d/%d/%d: %08x %08x %08x %08x\n",
		 mmc_hostname(host), cmd->opcode, err,
		 mrq->data ? mrq->data->error : 0,
		 mrq->stop ? mrq->stop->error : 0,
		 cmd->resp[0], cmd->resp[1], cmd->resp[2], cmd->resp[3]);

	if (err && cmd->retries) {
		cmd->retries--;
		cmd->error = 0;
		host->ops->request(host, mrq);
	} else if (mrq->done) {
		mrq->done(mrq);
	}
}

EXPORT_SYMBOL(mmc_request_done);

/**
 *	mmc_start_request - start a command on a host
 *	@host: MMC host to start command on
 *	@mrq: MMC request to start
 *
 *	Queue a command on the specified host.  We expect the
 *	caller to be holding the host lock with interrupts disabled.
 */
void
mmc_start_request(struct mmc_host *host, struct mmc_request *mrq)
{
#ifdef CONFIG_MMC_DEBUG
	unsigned int i, sz;
#endif

	pr_debug("%s: starting CMD%u arg %08x flags %08x\n",
		 mmc_hostname(host), mrq->cmd->opcode,
		 mrq->cmd->arg, mrq->cmd->flags);

	WARN_ON(!host->claimed);

	mrq->cmd->error = 0;
	mrq->cmd->mrq = mrq;
	if (mrq->data) {
		BUG_ON(mrq->data->blksz > host->max_blk_size);
		BUG_ON(mrq->data->blocks > host->max_blk_count);
		BUG_ON(mrq->data->blocks * mrq->data->blksz >
			host->max_req_size);

#ifdef CONFIG_MMC_DEBUG
		sz = 0;
		for (i = 0;i < mrq->data->sg_len;i++)
			sz += mrq->data->sg[i].length;
		BUG_ON(sz != mrq->data->blocks * mrq->data->blksz);
#endif

		mrq->cmd->data = mrq->data;
		mrq->data->error = 0;
		mrq->data->mrq = mrq;
		if (mrq->stop) {
			mrq->data->stop = mrq->stop;
			mrq->stop->error = 0;
			mrq->stop->mrq = mrq;
		}
	}
	host->ops->request(host, mrq);
}

EXPORT_SYMBOL(mmc_start_request);

static void mmc_wait_done(struct mmc_request *mrq)
{
	complete(mrq->done_data);
}

int mmc_wait_for_req(struct mmc_host *host, struct mmc_request *mrq)
{
	DECLARE_COMPLETION_ONSTACK(complete);

	mrq->done_data = &complete;
	mrq->done = mmc_wait_done;

	mmc_start_request(host, mrq);

	wait_for_completion(&complete);

	return 0;
}

EXPORT_SYMBOL(mmc_wait_for_req);

/**
 *	mmc_wait_for_cmd - start a command and wait for completion
 *	@host: MMC host to start command
 *	@cmd: MMC command to start
 *	@retries: maximum number of retries
 *
 *	Start a new MMC command for a host, and wait for the command
 *	to complete.  Return any error that occurred while the command
 *	was executing.  Do not attempt to parse the response.
 */
int mmc_wait_for_cmd(struct mmc_host *host, struct mmc_command *cmd, int retries)
{
	struct mmc_request mrq;

	BUG_ON(!host->claimed);

	memset(&mrq, 0, sizeof(struct mmc_request));

	memset(cmd->resp, 0, sizeof(cmd->resp));
	cmd->retries = retries;

	mrq.cmd = cmd;
	cmd->data = NULL;

	mmc_wait_for_req(host, &mrq);

	return cmd->error;
}

EXPORT_SYMBOL(mmc_wait_for_cmd);

/**
 *	mmc_set_data_timeout - set the timeout for a data command
 *	@data: data phase for command
 *	@card: the MMC card associated with the data transfer
 *	@write: flag to differentiate reads from writes
 */
void mmc_set_data_timeout(struct mmc_data *data, const struct mmc_card *card,
			  int write)
{
	unsigned int mult;

	/*
	 * SD cards use a 100 multiplier rather than 10
	 */
	mult = mmc_card_sd(card) ? 100 : 10;

	/*
	 * Scale up the multiplier (and therefore the timeout) by
	 * the r2w factor for writes.
	 */
	if (write)
		mult <<= card->csd.r2w_factor;

	data->timeout_ns = card->csd.tacc_ns * mult;
	data->timeout_clks = card->csd.tacc_clks * mult;

	/*
	 * SD cards also have an upper limit on the timeout.
	 */
	if (mmc_card_sd(card)) {
		unsigned int timeout_us, limit_us;

		timeout_us = data->timeout_ns / 1000;
		timeout_us += data->timeout_clks * 1000 /
			(card->host->ios.clock / 1000);

		if (write)
			limit_us = 250000;
		else
			limit_us = 100000;

		/*
		 * SDHC cards always use these fixed values.
		 */
		if (timeout_us > limit_us || mmc_card_blockaddr(card)) {
			data->timeout_ns = limit_us * 1000;
			data->timeout_clks = 0;
		}
	}
}
EXPORT_SYMBOL(mmc_set_data_timeout);

/**
 *	__mmc_claim_host - exclusively claim a host
 *	@host: mmc host to claim
 *	@card: mmc card to claim host for
 *
 *	Claim a host for a set of operations.  If a valid card
 *	is passed and this wasn't the last card selected, select
 *	the card before returning.
 *
 *	Note: you should use mmc_card_claim_host or mmc_claim_host.
 */
void mmc_claim_host(struct mmc_host *host)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;

	add_wait_queue(&host->wq, &wait);
	spin_lock_irqsave(&host->lock, flags);
	while (1) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (!host->claimed)
			break;
		spin_unlock_irqrestore(&host->lock, flags);
		schedule();
		spin_lock_irqsave(&host->lock, flags);
	}
	set_current_state(TASK_RUNNING);
	host->claimed = 1;
	spin_unlock_irqrestore(&host->lock, flags);
	remove_wait_queue(&host->wq, &wait);
}

EXPORT_SYMBOL(mmc_claim_host);

/**
 *	mmc_release_host - release a host
 *	@host: mmc host to release
 *
 *	Release a MMC host, allowing others to claim the host
 *	for their operations.
 */
void mmc_release_host(struct mmc_host *host)
{
	unsigned long flags;

	BUG_ON(!host->claimed);

	spin_lock_irqsave(&host->lock, flags);
	host->claimed = 0;
	spin_unlock_irqrestore(&host->lock, flags);

	wake_up(&host->wq);
}

EXPORT_SYMBOL(mmc_release_host);

/*
 * Internal function that does the actual ios call to the host driver,
 * optionally printing some debug output.
 */
static inline void mmc_set_ios(struct mmc_host *host)
{
	struct mmc_ios *ios = &host->ios;

	pr_debug("%s: clock %uHz busmode %u powermode %u cs %u Vdd %u "
		"width %u timing %u\n",
		 mmc_hostname(host), ios->clock, ios->bus_mode,
		 ios->power_mode, ios->chip_select, ios->vdd,
		 ios->bus_width, ios->timing);

	host->ops->set_ios(host, ios);
}

/*
 * Control chip select pin on a host.
 */
void mmc_set_chip_select(struct mmc_host *host, int mode)
{
	host->ios.chip_select = mode;
	mmc_set_ios(host);
}

/*
 * Sets the host clock to the highest possible frequency that
 * is below "hz".
 */
void mmc_set_clock(struct mmc_host *host, unsigned int hz)
{
	WARN_ON(hz < host->f_min);

	if (hz > host->f_max)
		hz = host->f_max;

	host->ios.clock = hz;
	mmc_set_ios(host);
}

/*
 * Change the bus mode (open drain/push-pull) of a host.
 */
void mmc_set_bus_mode(struct mmc_host *host, unsigned int mode)
{
	host->ios.bus_mode = mode;
	mmc_set_ios(host);
}

/*
 * Change data bus width of a host.
 */
void mmc_set_bus_width(struct mmc_host *host, unsigned int width)
{
	host->ios.bus_width = width;
	mmc_set_ios(host);
}

/*
 * Mask off any voltages we don't support and select
 * the lowest voltage
 */
u32 mmc_select_voltage(struct mmc_host *host, u32 ocr)
{
	int bit;

	ocr &= host->ocr_avail;

	bit = ffs(ocr);
	if (bit) {
		bit -= 1;

		ocr &= 3 << bit;

		host->ios.vdd = bit;
		mmc_set_ios(host);
	} else {
		ocr = 0;
	}

	return ocr;
}

/*
 * Select timing parameters for host.
 */
void mmc_set_timing(struct mmc_host *host, unsigned int timing)
{
	host->ios.timing = timing;
	mmc_set_ios(host);
}

/*
 * Allocate a new MMC card
 */
struct mmc_card *mmc_alloc_card(struct mmc_host *host)
{
	struct mmc_card *card;

	card = kmalloc(sizeof(struct mmc_card), GFP_KERNEL);
	if (!card)
		return ERR_PTR(-ENOMEM);

	mmc_init_card(card, host);

	return card;
}

/*
 * Apply power to the MMC stack.  This is a two-stage process.
 * First, we enable power to the card without the clock running.
 * We then wait a bit for the power to stabilise.  Finally,
 * enable the bus drivers and clock to the card.
 *
 * We must _NOT_ enable the clock prior to power stablising.
 *
 * If a host does all the power sequencing itself, ignore the
 * initial MMC_POWER_UP stage.
 */
static void mmc_power_up(struct mmc_host *host)
{
	int bit = fls(host->ocr_avail) - 1;

	host->ios.vdd = bit;
	host->ios.bus_mode = MMC_BUSMODE_OPENDRAIN;
	host->ios.chip_select = MMC_CS_DONTCARE;
	host->ios.power_mode = MMC_POWER_UP;
	host->ios.bus_width = MMC_BUS_WIDTH_1;
	host->ios.timing = MMC_TIMING_LEGACY;
	mmc_set_ios(host);

	mmc_delay(1);

	host->ios.clock = host->f_min;
	host->ios.power_mode = MMC_POWER_ON;
	mmc_set_ios(host);

	mmc_delay(2);
}

static void mmc_power_off(struct mmc_host *host)
{
	host->ios.clock = 0;
	host->ios.vdd = 0;
	host->ios.bus_mode = MMC_BUSMODE_OPENDRAIN;
	host->ios.chip_select = MMC_CS_DONTCARE;
	host->ios.power_mode = MMC_POWER_OFF;
	host->ios.bus_width = MMC_BUS_WIDTH_1;
	host->ios.timing = MMC_TIMING_LEGACY;
	mmc_set_ios(host);
}

/*
 * Assign a mmc bus handler to a host. Only one bus handler may control a
 * host at any given time.
 */
void mmc_attach_bus(struct mmc_host *host, const struct mmc_bus_ops *ops)
{
	unsigned long flags;

	BUG_ON(!host);
	BUG_ON(!ops);

	BUG_ON(!host->claimed);

	spin_lock_irqsave(&host->lock, flags);

	BUG_ON(host->bus_ops);
	BUG_ON(host->bus_refs);

	host->bus_ops = ops;
	host->bus_refs = 1;
	host->bus_dead = 0;

	spin_unlock_irqrestore(&host->lock, flags);
}

/*
 * Remove the current bus handler from a host. Assumes that there are
 * no interesting cards left, so the bus is powered down.
 */
void mmc_detach_bus(struct mmc_host *host)
{
	unsigned long flags;

	BUG_ON(!host);

	BUG_ON(!host->claimed);
	BUG_ON(!host->bus_ops);

	spin_lock_irqsave(&host->lock, flags);

	host->bus_dead = 1;

	spin_unlock_irqrestore(&host->lock, flags);

	mmc_power_off(host);

	mmc_bus_put(host);
}

/*
 * Cleanup when the last reference to the bus operator is dropped.
 */
void __mmc_release_bus(struct mmc_host *host)
{
	BUG_ON(!host);
	BUG_ON(host->bus_refs);
	BUG_ON(!host->bus_dead);

	host->bus_ops = NULL;
}

/**
 *	mmc_detect_change - process change of state on a MMC socket
 *	@host: host which changed state.
 *	@delay: optional delay to wait before detection (jiffies)
 *
 *	All we know is that card(s) have been inserted or removed
 *	from the socket(s).  We don't know which socket or cards.
 */
void mmc_detect_change(struct mmc_host *host, unsigned long delay)
{
#ifdef CONFIG_MMC_DEBUG
	unsigned long flags;
	spin_lock_irqsave(&host->lock, flags);
	BUG_ON(host->removed);
	spin_unlock_irqrestore(&host->lock, flags);
#endif

	mmc_schedule_delayed_work(&host->detect, delay);
}

EXPORT_SYMBOL(mmc_detect_change);


static void mmc_rescan(struct work_struct *work)
{
	struct mmc_host *host =
		container_of(work, struct mmc_host, detect.work);
	u32 ocr;
	int err;

	mmc_bus_get(host);

	if (host->bus_ops == NULL) {
		/*
		 * Only we can add a new handler, so it's safe to
		 * release the lock here.
		 */
		mmc_bus_put(host);

		mmc_claim_host(host);

		mmc_power_up(host);
		mmc_go_idle(host);

		mmc_send_if_cond(host, host->ocr_avail);

		err = mmc_send_app_op_cond(host, 0, &ocr);
		if (err == MMC_ERR_NONE) {
			if (mmc_attach_sd(host, ocr))
				mmc_power_off(host);
		} else {
			/*
			 * If we fail to detect any SD cards then try
			 * searching for MMC cards.
			 */
			err = mmc_send_op_cond(host, 0, &ocr);
			if (err == MMC_ERR_NONE) {
				if (mmc_attach_mmc(host, ocr))
					mmc_power_off(host);
			} else {
				mmc_power_off(host);
				mmc_release_host(host);
			}
		}
	} else {
		if (host->bus_ops->detect && !host->bus_dead)
			host->bus_ops->detect(host);

		mmc_bus_put(host);
	}
}


/**
 *	mmc_alloc_host - initialise the per-host structure.
 *	@extra: sizeof private data structure
 *	@dev: pointer to host device model structure
 *
 *	Initialise the per-host structure.
 */
struct mmc_host *mmc_alloc_host(int extra, struct device *dev)
{
	struct mmc_host *host;

	host = mmc_alloc_host_sysfs(extra, dev);
	if (host) {
		spin_lock_init(&host->lock);
		init_waitqueue_head(&host->wq);
		INIT_DELAYED_WORK(&host->detect, mmc_rescan);

		/*
		 * By default, hosts do not support SGIO or large requests.
		 * They have to set these according to their abilities.
		 */
		host->max_hw_segs = 1;
		host->max_phys_segs = 1;
		host->max_seg_size = PAGE_CACHE_SIZE;

		host->max_req_size = PAGE_CACHE_SIZE;
		host->max_blk_size = 512;
		host->max_blk_count = PAGE_CACHE_SIZE / 512;
	}

	return host;
}

EXPORT_SYMBOL(mmc_alloc_host);

/**
 *	mmc_add_host - initialise host hardware
 *	@host: mmc host
 */
int mmc_add_host(struct mmc_host *host)
{
	int ret;

	ret = mmc_add_host_sysfs(host);
	if (ret == 0) {
		mmc_power_off(host);
		mmc_detect_change(host, 0);
	}

	return ret;
}

EXPORT_SYMBOL(mmc_add_host);

/**
 *	mmc_remove_host - remove host hardware
 *	@host: mmc host
 *
 *	Unregister and remove all cards associated with this host,
 *	and power down the MMC bus.
 */
void mmc_remove_host(struct mmc_host *host)
{
#ifdef CONFIG_MMC_DEBUG
	unsigned long flags;
	spin_lock_irqsave(&host->lock, flags);
	host->removed = 1;
	spin_unlock_irqrestore(&host->lock, flags);
#endif

	mmc_flush_scheduled_work();

	mmc_bus_get(host);
	if (host->bus_ops && !host->bus_dead) {
		if (host->bus_ops->remove)
			host->bus_ops->remove(host);

		mmc_claim_host(host);
		mmc_detach_bus(host);
		mmc_release_host(host);
	}
	mmc_bus_put(host);

	BUG_ON(host->card);

	mmc_power_off(host);
	mmc_remove_host_sysfs(host);
}

EXPORT_SYMBOL(mmc_remove_host);

/**
 *	mmc_free_host - free the host structure
 *	@host: mmc host
 *
 *	Free the host once all references to it have been dropped.
 */
void mmc_free_host(struct mmc_host *host)
{
	mmc_free_host_sysfs(host);
}

EXPORT_SYMBOL(mmc_free_host);

#ifdef CONFIG_PM

/**
 *	mmc_suspend_host - suspend a host
 *	@host: mmc host
 *	@state: suspend mode (PM_SUSPEND_xxx)
 */
int mmc_suspend_host(struct mmc_host *host, pm_message_t state)
{
	mmc_flush_scheduled_work();

	mmc_bus_get(host);
	if (host->bus_ops && !host->bus_dead) {
		if (host->bus_ops->suspend)
			host->bus_ops->suspend(host);
		if (!host->bus_ops->resume) {
			if (host->bus_ops->remove)
				host->bus_ops->remove(host);

			mmc_claim_host(host);
			mmc_detach_bus(host);
			mmc_release_host(host);
		}
	}
	mmc_bus_put(host);

	mmc_power_off(host);

	return 0;
}

EXPORT_SYMBOL(mmc_suspend_host);

/**
 *	mmc_resume_host - resume a previously suspended host
 *	@host: mmc host
 */
int mmc_resume_host(struct mmc_host *host)
{
	mmc_bus_get(host);
	if (host->bus_ops && !host->bus_dead) {
		mmc_power_up(host);
		BUG_ON(!host->bus_ops->resume);
		host->bus_ops->resume(host);
	}
	mmc_bus_put(host);

	/*
	 * We add a slight delay here so that resume can progress
	 * in parallel.
	 */
	mmc_detect_change(host, 1);

	return 0;
}

EXPORT_SYMBOL(mmc_resume_host);

#endif

MODULE_LICENSE("GPL");
