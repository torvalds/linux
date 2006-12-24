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

#define CMD_RETRIES	3

/*
 * OCR Bit positions to 10s of Vdd mV.
 */
static const unsigned short mmc_ocr_bit_to_vdd[] = {
	150,	155,	160,	165,	170,	180,	190,	200,
	210,	220,	230,	240,	250,	260,	270,	280,
	290,	300,	310,	320,	330,	340,	350,	360
};

static const unsigned int tran_exp[] = {
	10000,		100000,		1000000,	10000000,
	0,		0,		0,		0
};

static const unsigned char tran_mant[] = {
	0,	10,	12,	13,	15,	20,	25,	30,
	35,	40,	45,	50,	55,	60,	70,	80,
};

static const unsigned int tacc_exp[] = {
	1,	10,	100,	1000,	10000,	100000,	1000000, 10000000,
};

static const unsigned int tacc_mant[] = {
	0,	10,	12,	13,	15,	20,	25,	30,
	35,	40,	45,	50,	55,	60,	70,	80,
};


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

void mmc_set_chip_select(struct mmc_host *host, int mode)
{
	host->ios.chip_select = mode;
	mmc_set_ios(host);
}

/*
 * Mask off any voltages we don't support and select
 * the lowest voltage
 */
static u32 mmc_select_voltage(struct mmc_host *host, u32 ocr)
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

#define UNSTUFF_BITS(resp,start,size)					\
	({								\
		const int __size = size;				\
		const u32 __mask = (__size < 32 ? 1 << __size : 0) - 1;	\
		const int __off = 3 - ((start) / 32);			\
		const int __shft = (start) & 31;			\
		u32 __res;						\
									\
		__res = resp[__off] >> __shft;				\
		if (__size + __shft > 32)				\
			__res |= resp[__off-1] << ((32 - __shft) % 32);	\
		__res & __mask;						\
	})

/*
 * Given the decoded CSD structure, decode the raw CID to our CID structure.
 */
static void mmc_decode_cid(struct mmc_card *card)
{
	u32 *resp = card->raw_cid;

	memset(&card->cid, 0, sizeof(struct mmc_cid));

	if (mmc_card_sd(card)) {
		/*
		 * SD doesn't currently have a version field so we will
		 * have to assume we can parse this.
		 */
		card->cid.manfid		= UNSTUFF_BITS(resp, 120, 8);
		card->cid.oemid			= UNSTUFF_BITS(resp, 104, 16);
		card->cid.prod_name[0]		= UNSTUFF_BITS(resp, 96, 8);
		card->cid.prod_name[1]		= UNSTUFF_BITS(resp, 88, 8);
		card->cid.prod_name[2]		= UNSTUFF_BITS(resp, 80, 8);
		card->cid.prod_name[3]		= UNSTUFF_BITS(resp, 72, 8);
		card->cid.prod_name[4]		= UNSTUFF_BITS(resp, 64, 8);
		card->cid.hwrev			= UNSTUFF_BITS(resp, 60, 4);
		card->cid.fwrev			= UNSTUFF_BITS(resp, 56, 4);
		card->cid.serial		= UNSTUFF_BITS(resp, 24, 32);
		card->cid.year			= UNSTUFF_BITS(resp, 12, 8);
		card->cid.month			= UNSTUFF_BITS(resp, 8, 4);

		card->cid.year += 2000; /* SD cards year offset */
	} else {
		/*
		 * The selection of the format here is based upon published
		 * specs from sandisk and from what people have reported.
		 */
		switch (card->csd.mmca_vsn) {
		case 0: /* MMC v1.0 - v1.2 */
		case 1: /* MMC v1.4 */
			card->cid.manfid	= UNSTUFF_BITS(resp, 104, 24);
			card->cid.prod_name[0]	= UNSTUFF_BITS(resp, 96, 8);
			card->cid.prod_name[1]	= UNSTUFF_BITS(resp, 88, 8);
			card->cid.prod_name[2]	= UNSTUFF_BITS(resp, 80, 8);
			card->cid.prod_name[3]	= UNSTUFF_BITS(resp, 72, 8);
			card->cid.prod_name[4]	= UNSTUFF_BITS(resp, 64, 8);
			card->cid.prod_name[5]	= UNSTUFF_BITS(resp, 56, 8);
			card->cid.prod_name[6]	= UNSTUFF_BITS(resp, 48, 8);
			card->cid.hwrev		= UNSTUFF_BITS(resp, 44, 4);
			card->cid.fwrev		= UNSTUFF_BITS(resp, 40, 4);
			card->cid.serial	= UNSTUFF_BITS(resp, 16, 24);
			card->cid.month		= UNSTUFF_BITS(resp, 12, 4);
			card->cid.year		= UNSTUFF_BITS(resp, 8, 4) + 1997;
			break;

		case 2: /* MMC v2.0 - v2.2 */
		case 3: /* MMC v3.1 - v3.3 */
		case 4: /* MMC v4 */
			card->cid.manfid	= UNSTUFF_BITS(resp, 120, 8);
			card->cid.oemid		= UNSTUFF_BITS(resp, 104, 16);
			card->cid.prod_name[0]	= UNSTUFF_BITS(resp, 96, 8);
			card->cid.prod_name[1]	= UNSTUFF_BITS(resp, 88, 8);
			card->cid.prod_name[2]	= UNSTUFF_BITS(resp, 80, 8);
			card->cid.prod_name[3]	= UNSTUFF_BITS(resp, 72, 8);
			card->cid.prod_name[4]	= UNSTUFF_BITS(resp, 64, 8);
			card->cid.prod_name[5]	= UNSTUFF_BITS(resp, 56, 8);
			card->cid.serial	= UNSTUFF_BITS(resp, 16, 32);
			card->cid.month		= UNSTUFF_BITS(resp, 12, 4);
			card->cid.year		= UNSTUFF_BITS(resp, 8, 4) + 1997;
			break;

		default:
			printk("%s: card has unknown MMCA version %d\n",
				mmc_hostname(card->host), card->csd.mmca_vsn);
			mmc_card_set_bad(card);
			break;
		}
	}
}

/*
 * Given a 128-bit response, decode to our card CSD structure.
 */
static void mmc_decode_csd(struct mmc_card *card)
{
	struct mmc_csd *csd = &card->csd;
	unsigned int e, m, csd_struct;
	u32 *resp = card->raw_csd;

	if (mmc_card_sd(card)) {
		csd_struct = UNSTUFF_BITS(resp, 126, 2);

		switch (csd_struct) {
		case 0:
			m = UNSTUFF_BITS(resp, 115, 4);
			e = UNSTUFF_BITS(resp, 112, 3);
			csd->tacc_ns	 = (tacc_exp[e] * tacc_mant[m] + 9) / 10;
			csd->tacc_clks	 = UNSTUFF_BITS(resp, 104, 8) * 100;

			m = UNSTUFF_BITS(resp, 99, 4);
			e = UNSTUFF_BITS(resp, 96, 3);
			csd->max_dtr	  = tran_exp[e] * tran_mant[m];
			csd->cmdclass	  = UNSTUFF_BITS(resp, 84, 12);

			e = UNSTUFF_BITS(resp, 47, 3);
			m = UNSTUFF_BITS(resp, 62, 12);
			csd->capacity	  = (1 + m) << (e + 2);

			csd->read_blkbits = UNSTUFF_BITS(resp, 80, 4);
			csd->read_partial = UNSTUFF_BITS(resp, 79, 1);
			csd->write_misalign = UNSTUFF_BITS(resp, 78, 1);
			csd->read_misalign = UNSTUFF_BITS(resp, 77, 1);
			csd->r2w_factor = UNSTUFF_BITS(resp, 26, 3);
			csd->write_blkbits = UNSTUFF_BITS(resp, 22, 4);
			csd->write_partial = UNSTUFF_BITS(resp, 21, 1);
			break;
		case 1:
			/*
			 * This is a block-addressed SDHC card. Most
			 * interesting fields are unused and have fixed
			 * values. To avoid getting tripped by buggy cards,
			 * we assume those fixed values ourselves.
			 */
			mmc_card_set_blockaddr(card);

			csd->tacc_ns	 = 0; /* Unused */
			csd->tacc_clks	 = 0; /* Unused */

			m = UNSTUFF_BITS(resp, 99, 4);
			e = UNSTUFF_BITS(resp, 96, 3);
			csd->max_dtr	  = tran_exp[e] * tran_mant[m];
			csd->cmdclass	  = UNSTUFF_BITS(resp, 84, 12);

			m = UNSTUFF_BITS(resp, 48, 22);
			csd->capacity     = (1 + m) << 10;

			csd->read_blkbits = 9;
			csd->read_partial = 0;
			csd->write_misalign = 0;
			csd->read_misalign = 0;
			csd->r2w_factor = 4; /* Unused */
			csd->write_blkbits = 9;
			csd->write_partial = 0;
			break;
		default:
			printk("%s: unrecognised CSD structure version %d\n",
				mmc_hostname(card->host), csd_struct);
			mmc_card_set_bad(card);
			return;
		}
	} else {
		/*
		 * We only understand CSD structure v1.1 and v1.2.
		 * v1.2 has extra information in bits 15, 11 and 10.
		 */
		csd_struct = UNSTUFF_BITS(resp, 126, 2);
		if (csd_struct != 1 && csd_struct != 2) {
			printk("%s: unrecognised CSD structure version %d\n",
				mmc_hostname(card->host), csd_struct);
			mmc_card_set_bad(card);
			return;
		}

		csd->mmca_vsn	 = UNSTUFF_BITS(resp, 122, 4);
		m = UNSTUFF_BITS(resp, 115, 4);
		e = UNSTUFF_BITS(resp, 112, 3);
		csd->tacc_ns	 = (tacc_exp[e] * tacc_mant[m] + 9) / 10;
		csd->tacc_clks	 = UNSTUFF_BITS(resp, 104, 8) * 100;

		m = UNSTUFF_BITS(resp, 99, 4);
		e = UNSTUFF_BITS(resp, 96, 3);
		csd->max_dtr	  = tran_exp[e] * tran_mant[m];
		csd->cmdclass	  = UNSTUFF_BITS(resp, 84, 12);

		e = UNSTUFF_BITS(resp, 47, 3);
		m = UNSTUFF_BITS(resp, 62, 12);
		csd->capacity	  = (1 + m) << (e + 2);

		csd->read_blkbits = UNSTUFF_BITS(resp, 80, 4);
		csd->read_partial = UNSTUFF_BITS(resp, 79, 1);
		csd->write_misalign = UNSTUFF_BITS(resp, 78, 1);
		csd->read_misalign = UNSTUFF_BITS(resp, 77, 1);
		csd->r2w_factor = UNSTUFF_BITS(resp, 26, 3);
		csd->write_blkbits = UNSTUFF_BITS(resp, 22, 4);
		csd->write_partial = UNSTUFF_BITS(resp, 21, 1);
	}
}

/*
 * Given a 64-bit response, decode to our card SCR structure.
 */
static void mmc_decode_scr(struct mmc_card *card)
{
	struct sd_scr *scr = &card->scr;
	unsigned int scr_struct;
	u32 resp[4];

	BUG_ON(!mmc_card_sd(card));

	resp[3] = card->raw_scr[1];
	resp[2] = card->raw_scr[0];

	scr_struct = UNSTUFF_BITS(resp, 60, 4);
	if (scr_struct != 0) {
		printk("%s: unrecognised SCR structure version %d\n",
			mmc_hostname(card->host), scr_struct);
		mmc_card_set_bad(card);
		return;
	}

	scr->sda_vsn = UNSTUFF_BITS(resp, 56, 4);
	scr->bus_widths = UNSTUFF_BITS(resp, 48, 4);
}

/*
 * Allocate a new MMC card
 */
static struct mmc_card *
mmc_alloc_card(struct mmc_host *host, u32 *raw_cid)
{
	struct mmc_card *card;

	card = kmalloc(sizeof(struct mmc_card), GFP_KERNEL);
	if (!card)
		return ERR_PTR(-ENOMEM);

	mmc_init_card(card, host);
	memcpy(card->raw_cid, raw_cid, sizeof(card->raw_cid));

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
 * Discover the card by requesting its CID.
 *
 * Create a mmc_card entry for the discovered card, assigning
 * it an RCA, and save the raw CID for decoding later.
 */
static void mmc_discover_card(struct mmc_host *host)
{
	unsigned int err;
	u32 cid[4];

	BUG_ON(host->card);

	err = mmc_all_send_cid(host, cid);
	if (err != MMC_ERR_NONE) {
		printk(KERN_ERR "%s: error requesting CID: %d\n",
			mmc_hostname(host), err);
		return;
	}

	host->card = mmc_alloc_card(host, cid);
	if (IS_ERR(host->card)) {
		err = PTR_ERR(host->card);
		host->card = NULL;
		return;
	}

	if (host->mode == MMC_MODE_SD) {
		host->card->type = MMC_TYPE_SD;

		err = mmc_send_relative_addr(host, &host->card->rca);
		if (err != MMC_ERR_NONE)
			mmc_card_set_dead(host->card);
		else {
			if (!host->ops->get_ro) {
				printk(KERN_WARNING "%s: host does not "
					"support reading read-only "
					"switch. assuming write-enable.\n",
					mmc_hostname(host));
			} else {
				if (host->ops->get_ro(host))
					mmc_card_set_readonly(host->card);
			}
		}
	} else {
		host->card->type = MMC_TYPE_MMC;
		host->card->rca = 1;

		err = mmc_set_relative_addr(host->card);
		if (err != MMC_ERR_NONE)
			mmc_card_set_dead(host->card);
	}
}

static void mmc_read_csd(struct mmc_host *host)
{
	int err;

	if (!host->card)
		return;
	if (mmc_card_dead(host->card))
		return;

	err = mmc_send_csd(host->card, host->card->raw_csd);
	if (err != MMC_ERR_NONE) {
		mmc_card_set_dead(host->card);
		return;
	}

	mmc_decode_csd(host->card);
	mmc_decode_cid(host->card);
}

static void mmc_process_ext_csd(struct mmc_host *host)
{
	int err;
	u8 *ext_csd;

	if (!host->card)
		return;
	if (mmc_card_dead(host->card))
		return;
	if (mmc_card_sd(host->card))
		return;
	if (host->card->csd.mmca_vsn < CSD_SPEC_VER_4)
		return;

	/*
	 * As the ext_csd is so large and mostly unused, we don't store the
	 * raw block in mmc_card.
	 */
	ext_csd = kmalloc(512, GFP_KERNEL);
	if (!ext_csd) {
		printk("%s: could not allocate a buffer to receive the ext_csd."
		       "mmc v4 cards will be treated as v3.\n",
			mmc_hostname(host));
		return;
	}

	err = mmc_send_ext_csd(host->card, ext_csd);
	if (err != MMC_ERR_NONE) {
		if (host->card->csd.capacity == (4096 * 512)) {
			printk(KERN_ERR "%s: unable to read EXT_CSD "
				"on a possible high capacity card. "
				"Card will be ignored.\n",
				mmc_hostname(host));
			mmc_card_set_dead(host->card);
		} else {
			printk(KERN_WARNING "%s: unable to read "
				"EXT_CSD, performance might "
				"suffer.\n",
				mmc_hostname(host));
		}
		goto out;
	}

	host->card->ext_csd.sectors =
		ext_csd[EXT_CSD_SEC_CNT + 0] << 0 |
		ext_csd[EXT_CSD_SEC_CNT + 1] << 8 |
		ext_csd[EXT_CSD_SEC_CNT + 2] << 16 |
		ext_csd[EXT_CSD_SEC_CNT + 3] << 24;
	if (host->card->ext_csd.sectors)
		mmc_card_set_blockaddr(host->card);

	switch (ext_csd[EXT_CSD_CARD_TYPE]) {
	case EXT_CSD_CARD_TYPE_52 | EXT_CSD_CARD_TYPE_26:
		host->card->ext_csd.hs_max_dtr = 52000000;
		break;
	case EXT_CSD_CARD_TYPE_26:
		host->card->ext_csd.hs_max_dtr = 26000000;
		break;
	default:
		/* MMC v4 spec says this cannot happen */
		printk("%s: card is mmc v4 but doesn't support "
		       "any high-speed modes.\n",
			mmc_hostname(host));
		goto out;
	}

	if (host->caps & MMC_CAP_MMC_HIGHSPEED) {
		/* Activate highspeed support. */
		err = mmc_switch(host->card, MMC_SWITCH_MODE_WRITE_BYTE,
			EXT_CSD_HS_TIMING, 1);
		if (err != MMC_ERR_NONE) {
			printk("%s: failed to switch card to mmc v4 "
			       "high-speed mode.\n",
			       mmc_hostname(host));
			goto out;
		}

		mmc_card_set_highspeed(host->card);

		host->ios.timing = MMC_TIMING_MMC_HS;
		mmc_set_ios(host);
	}

	/* Check for host support for wide-bus modes. */
	if (host->caps & MMC_CAP_4_BIT_DATA) {
		/* Activate 4-bit support. */
		err = mmc_switch(host->card, MMC_SWITCH_MODE_WRITE_BYTE,
			EXT_CSD_BUS_WIDTH, EXT_CSD_BUS_WIDTH_4 |
			EXT_CSD_CMD_SET_NORMAL);
		if (err != MMC_ERR_NONE) {
			printk("%s: failed to switch card to "
			       "mmc v4 4-bit bus mode.\n",
			       mmc_hostname(host));
			goto out;
		}

		host->ios.bus_width = MMC_BUS_WIDTH_4;
		mmc_set_ios(host);
	}

out:
	kfree(ext_csd);
}

static void mmc_read_scr(struct mmc_host *host)
{
	int err;

	if (!host->card)
		return;
	if (mmc_card_dead(host->card))
		return;
	if (!mmc_card_sd(host->card))
		return;

	err = mmc_app_send_scr(host->card, host->card->raw_scr);
	if (err != MMC_ERR_NONE) {
		mmc_card_set_dead(host->card);
		return;
	}

	mmc_decode_scr(host->card);
}

static void mmc_read_switch_caps(struct mmc_host *host)
{
	int err;
	unsigned char *status;

	if (!(host->caps & MMC_CAP_SD_HIGHSPEED))
		return;

	if (!host->card)
		return;
	if (mmc_card_dead(host->card))
		return;
	if (!mmc_card_sd(host->card))
		return;
	if (host->card->scr.sda_vsn < SCR_SPEC_VER_1)
		return;

	status = kmalloc(64, GFP_KERNEL);
	if (!status) {
		printk(KERN_WARNING "%s: Unable to allocate buffer for "
			"reading switch capabilities.\n",
			mmc_hostname(host));
		return;
	}

	err = mmc_sd_switch(host->card, SD_SWITCH_CHECK,
		SD_SWITCH_GRP_ACCESS, SD_SWITCH_ACCESS_HS, status);
	if (err != MMC_ERR_NONE) {
		printk("%s: unable to read switch capabilities, "
			"performance might suffer.\n",
			mmc_hostname(host));
		goto out;
	}

	if (status[13] & 0x02)
		host->card->sw_caps.hs_max_dtr = 50000000;

	err = mmc_sd_switch(host->card, SD_SWITCH_SET,
		SD_SWITCH_GRP_ACCESS, SD_SWITCH_ACCESS_HS, status);
	if (err != MMC_ERR_NONE || (status[16] & 0xF) != 1) {
		printk(KERN_WARNING "%s: Problem switching card "
			"into high-speed mode!\n",
			mmc_hostname(host));
		goto out;
	}

	mmc_card_set_highspeed(host->card);

	host->ios.timing = MMC_TIMING_SD_HS;
	mmc_set_ios(host);

out:
	kfree(status);
}

static unsigned int mmc_calculate_clock(struct mmc_host *host)
{
	unsigned int max_dtr = host->f_max;

	if (host->card && !mmc_card_dead(host->card)) {
		if (mmc_card_highspeed(host->card) && mmc_card_sd(host->card)) {
			if (max_dtr > host->card->sw_caps.hs_max_dtr)
				max_dtr = host->card->sw_caps.hs_max_dtr;
		} else if (mmc_card_highspeed(host->card) && !mmc_card_sd(host->card)) {
			if (max_dtr > host->card->ext_csd.hs_max_dtr)
				max_dtr = host->card->ext_csd.hs_max_dtr;
		} else if (max_dtr > host->card->csd.max_dtr) {
			max_dtr = host->card->csd.max_dtr;
		}
	}

	pr_debug("%s: selected %d.%03dMHz transfer rate\n",
		 mmc_hostname(host),
		 max_dtr / 1000000, (max_dtr / 1000) % 1000);

	return max_dtr;
}

/*
 * Check whether cards we already know about are still present.
 * We do this by requesting status, and checking whether a card
 * responds.
 *
 * A request for status does not cause a state change in data
 * transfer mode.
 */
static void mmc_check_card(struct mmc_card *card)
{
	int err;

	BUG_ON(!card);

	err = mmc_send_status(card, NULL);
	if (err == MMC_ERR_NONE)
		return;

	mmc_card_set_dead(card);
}

static void mmc_setup(struct mmc_host *host)
{
	int err;
	u32 ocr;

	host->mode = MMC_MODE_SD;

	mmc_power_up(host);
	mmc_go_idle(host);

	err = mmc_send_if_cond(host, host->ocr_avail);
	if (err != MMC_ERR_NONE) {
		return;
	}
	err = mmc_send_app_op_cond(host, 0, &ocr);

	/*
	 * If we fail to detect any SD cards then try
	 * searching for MMC cards.
	 */
	if (err != MMC_ERR_NONE) {
		host->mode = MMC_MODE_MMC;

		err = mmc_send_op_cond(host, 0, &ocr);
		if (err != MMC_ERR_NONE)
			return;
	}

	host->ocr = mmc_select_voltage(host, ocr);

	if (host->ocr == 0)
		return;

	/*
	 * Since we're changing the OCR value, we seem to
	 * need to tell some cards to go back to the idle
	 * state.  We wait 1ms to give cards time to
	 * respond.
	 */
	mmc_go_idle(host);

	/*
	 * Send the selected OCR multiple times... until the cards
	 * all get the idea that they should be ready for CMD2.
	 * (My SanDisk card seems to need this.)
	 */
	if (host->mode == MMC_MODE_SD) {
		/*
		 * If SD_SEND_IF_COND indicates an SD 2.0
		 * compliant card and we should set bit 30
		 * of the ocr to indicate that we can handle
		 * block-addressed SDHC cards.
		 */
		err = mmc_send_if_cond(host, host->ocr);
		if (err == MMC_ERR_NONE)
			ocr = host->ocr | (1 << 30);

		mmc_send_app_op_cond(host, ocr, NULL);
	} else {
		/* The extra bit indicates that we support high capacity */
		mmc_send_op_cond(host, host->ocr | (1 << 30), NULL);
	}

	mmc_discover_card(host);

	/*
	 * Ok, now switch to push-pull mode.
	 */
	host->ios.bus_mode = MMC_BUSMODE_PUSHPULL;
	mmc_set_ios(host);

	mmc_read_csd(host);

	if (host->card && !mmc_card_dead(host->card)) {
		err = mmc_select_card(host->card);
		if (err != MMC_ERR_NONE)
			mmc_card_set_dead(host->card);
	}

	/*
	 * The card is in 1 bit mode by default so
	 * we only need to change if it supports the
	 * wider version.
	 */
	if (host->card && !mmc_card_dead(host->card) && 
		mmc_card_sd(host->card) &&
		(host->card->scr.bus_widths & SD_SCR_BUS_WIDTH_4) &&
		(host->card->host->caps & MMC_CAP_4_BIT_DATA)) {
		err = mmc_app_set_bus_width(host->card, SD_BUS_WIDTH_4);
		if (err != MMC_ERR_NONE)
			mmc_card_set_dead(host->card);
		else {
			host->ios.bus_width = MMC_BUS_WIDTH_4;
			mmc_set_ios(host);
		}
	}

	if (host->mode == MMC_MODE_SD) {
		mmc_read_scr(host);
		mmc_read_switch_caps(host);
	} else
		mmc_process_ext_csd(host);
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
	mmc_claim_host(host);
	BUG_ON(host->removed);
	mmc_release_host(host);
#endif

	mmc_schedule_delayed_work(&host->detect, delay);
}

EXPORT_SYMBOL(mmc_detect_change);


static void mmc_rescan(struct work_struct *work)
{
	struct mmc_host *host =
		container_of(work, struct mmc_host, detect.work);

	mmc_claim_host(host);

	/*
	 * Check for removed card and newly inserted ones. We check for
	 * removed cards first so we can intelligently re-select the VDD.
	 */
	if (host->card) {
		mmc_check_card(host->card);

		mmc_release_host(host);

		if (mmc_card_dead(host->card)) {
			mmc_remove_card(host->card);
			host->card = NULL;
		}

		goto out;
	}

	mmc_setup(host);

	if (host->card && !mmc_card_dead(host->card)) {
		/*
		 * (Re-)calculate the fastest clock rate which the
		 * attached cards and the host support.
		 */
		host->ios.clock = mmc_calculate_clock(host);
		mmc_set_ios(host);
	}

	mmc_release_host(host);

	/*
	 * If this is a new and good card, register it.
	 */
	if (host->card && !mmc_card_dead(host->card)) {
		if (mmc_register_card(host->card))
			mmc_card_set_dead(host->card);
	}

	/*
	 * If this card is dead, destroy it.
	 */
	if (host->card && mmc_card_dead(host->card)) {
		mmc_remove_card(host->card);
		host->card = NULL;
	}

out:
	/*
	 * If we discover that there are no cards on the
	 * bus, turn off the clock and power down.
	 */
	if (!host->card)
		mmc_power_off(host);
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
	mmc_claim_host(host);
	host->removed = 1;
	mmc_release_host(host);
#endif

	mmc_flush_scheduled_work();

	if (host->card) {
		mmc_remove_card(host->card);
		host->card = NULL;
	}

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

	if (host->card) {
		mmc_remove_card(host->card);
		host->card = NULL;
	}

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
	mmc_rescan(&host->detect.work);

	return 0;
}

EXPORT_SYMBOL(mmc_resume_host);

#endif

MODULE_LICENSE("GPL");
