/*
 *  linux/drivers/mmc/mmc.c
 *
 *  Copyright (C) 2003-2004 Russell King, All Rights Reserved.
 *  Copyright (C) 2005-2007 Pierre Ossman, All Rights Reserved.
 *  MMCv4 support Copyright (C) 2006 Philip Langdale, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>

#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>

#include "core.h"
#include "sysfs.h"
#include "mmc_ops.h"

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

/*
 * Given a 128-bit response, decode to our card CSD structure.
 */
static void mmc_decode_csd(struct mmc_card *card)
{
	struct mmc_csd *csd = &card->csd;
	unsigned int e, m, csd_struct;
	u32 *resp = card->raw_csd;

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

/*
 * Read and decode extended CSD. Switch to high-speed and wide bus
 * if supported.
 */
static int mmc_process_ext_csd(struct mmc_card *card)
{
	int err;
	u8 *ext_csd;

	BUG_ON(!card);

	err = MMC_ERR_FAILED;

	if (card->csd.mmca_vsn < CSD_SPEC_VER_4)
		return MMC_ERR_NONE;

	/*
	 * As the ext_csd is so large and mostly unused, we don't store the
	 * raw block in mmc_card.
	 */
	ext_csd = kmalloc(512, GFP_KERNEL);
	if (!ext_csd) {
		printk(KERN_ERR "%s: could not allocate a buffer to "
			"receive the ext_csd. mmc v4 cards will be "
			"treated as v3.\n", mmc_hostname(card->host));
		return MMC_ERR_FAILED;
	}

	err = mmc_send_ext_csd(card, ext_csd);
	if (err != MMC_ERR_NONE) {
		/*
		 * High capacity cards should have this "magic" size
		 * stored in their CSD.
		 */
		if (card->csd.capacity == (4096 * 512)) {
			printk(KERN_ERR "%s: unable to read EXT_CSD "
				"on a possible high capacity card. "
				"Card will be ignored.\n",
				mmc_hostname(card->host));
		} else {
			printk(KERN_WARNING "%s: unable to read "
				"EXT_CSD, performance might "
				"suffer.\n",
				mmc_hostname(card->host));
			err = MMC_ERR_NONE;
		}
		goto out;
	}

	card->ext_csd.sectors =
		ext_csd[EXT_CSD_SEC_CNT + 0] << 0 |
		ext_csd[EXT_CSD_SEC_CNT + 1] << 8 |
		ext_csd[EXT_CSD_SEC_CNT + 2] << 16 |
		ext_csd[EXT_CSD_SEC_CNT + 3] << 24;
	if (card->ext_csd.sectors)
		mmc_card_set_blockaddr(card);

	switch (ext_csd[EXT_CSD_CARD_TYPE]) {
	case EXT_CSD_CARD_TYPE_52 | EXT_CSD_CARD_TYPE_26:
		card->ext_csd.hs_max_dtr = 52000000;
		break;
	case EXT_CSD_CARD_TYPE_26:
		card->ext_csd.hs_max_dtr = 26000000;
		break;
	default:
		/* MMC v4 spec says this cannot happen */
		printk(KERN_WARNING "%s: card is mmc v4 but doesn't "
			"support any high-speed modes.\n",
			mmc_hostname(card->host));
		goto out;
	}

	if (card->host->caps & MMC_CAP_MMC_HIGHSPEED) {
		/* Activate highspeed support. */
		err = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
			EXT_CSD_HS_TIMING, 1);
		if (err != MMC_ERR_NONE) {
			printk(KERN_WARNING "%s: failed to switch "
				"card to mmc v4 high-speed mode.\n",
			       mmc_hostname(card->host));
			err = MMC_ERR_NONE;
			goto out;
		}

		mmc_card_set_highspeed(card);

		mmc_set_timing(card->host, MMC_TIMING_MMC_HS);
	}

	/* Check for host support for wide-bus modes. */
	if (card->host->caps & MMC_CAP_4_BIT_DATA) {
		/* Activate 4-bit support. */
		err = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
			EXT_CSD_BUS_WIDTH, EXT_CSD_BUS_WIDTH_4);
		if (err != MMC_ERR_NONE) {
			printk(KERN_WARNING "%s: failed to switch "
				"card to mmc v4 4-bit bus mode.\n",
			       mmc_hostname(card->host));
			err = MMC_ERR_NONE;
			goto out;
		}

		mmc_set_bus_width(card->host, MMC_BUS_WIDTH_4);
	}

out:
	kfree(ext_csd);

	return err;
}

/*
 * Host is being removed. Free up the current card.
 */
static void mmc_remove(struct mmc_host *host)
{
	BUG_ON(!host);
	BUG_ON(!host->card);

	mmc_remove_card(host->card);
	host->card = NULL;
}

/*
 * Card detection callback from host.
 */
static void mmc_detect(struct mmc_host *host)
{
	int err;

	BUG_ON(!host);
	BUG_ON(!host->card);

	mmc_claim_host(host);

	/*
	 * Just check if our card has been removed.
	 */
	err = mmc_send_status(host->card, NULL);

	mmc_release_host(host);

	if (err != MMC_ERR_NONE) {
		mmc_remove_card(host->card);
		host->card = NULL;

		mmc_claim_host(host);
		mmc_detach_bus(host);
		mmc_release_host(host);
	}
}

static const struct mmc_bus_ops mmc_ops = {
	.remove = mmc_remove,
	.detect = mmc_detect,
};

/*
 * Starting point for MMC card init.
 */
int mmc_attach_mmc(struct mmc_host *host, u32 ocr)
{
	struct mmc_card *card;
	int err;
	u32 cid[4];
	unsigned int max_dtr;

	BUG_ON(!host);
	BUG_ON(!host->claimed);

	mmc_attach_bus(host, &mmc_ops);

	host->ocr = mmc_select_voltage(host, ocr);

	/*
	 * Can we support the voltage of the card?
	 */
	if (!host->ocr)
		goto err;

	/*
	 * Since we're changing the OCR value, we seem to
	 * need to tell some cards to go back to the idle
	 * state.  We wait 1ms to give cards time to
	 * respond.
	 */
	mmc_go_idle(host);

	/* The extra bit indicates that we support high capacity */
	mmc_send_op_cond(host, host->ocr | (1 << 30), NULL);

	/*
	 * Fetch CID from card.
	 */
	err = mmc_all_send_cid(host, cid);
	if (err != MMC_ERR_NONE)
		goto err;

	/*
	 * Allocate card structure.
	 */
	card = mmc_alloc_card(host);
	if (IS_ERR(card))
		goto err;

	card->type = MMC_TYPE_MMC;
	card->rca = 1;
	memcpy(card->raw_cid, cid, sizeof(card->raw_cid));

	/*
	 * Set card RCA.
	 */
	err = mmc_set_relative_addr(card);
	if (err != MMC_ERR_NONE)
		goto free_card;

	mmc_set_bus_mode(host, MMC_BUSMODE_PUSHPULL);

	/*
	 * Fetch CSD from card.
	 */
	err = mmc_send_csd(card, card->raw_csd);
	if (err != MMC_ERR_NONE)
		goto free_card;

	mmc_decode_csd(card);
	mmc_decode_cid(card);

	/*
	 * Fetch and process extened CSD.
	 * This will switch into high-speed and wide bus modes,
	 * as available.
	 */
	err = mmc_select_card(card);
	if (err != MMC_ERR_NONE)
		goto free_card;

	err = mmc_process_ext_csd(card);
	if (err != MMC_ERR_NONE)
		goto free_card;

	/*
	 * Compute bus speed.
	 */
	max_dtr = (unsigned int)-1;

	if (mmc_card_highspeed(card)) {
		if (max_dtr > card->ext_csd.hs_max_dtr)
			max_dtr = card->ext_csd.hs_max_dtr;
	} else if (max_dtr > card->csd.max_dtr) {
		max_dtr = card->csd.max_dtr;
	}

	mmc_set_clock(host, max_dtr);

	host->card = card;

	mmc_release_host(host);

	err = mmc_register_card(card);
	if (err)
		goto reclaim_host;

	return 0;

reclaim_host:
	mmc_claim_host(host);
free_card:
	mmc_remove_card(card);
	host->card = NULL;
err:
	mmc_detach_bus(host);
	mmc_release_host(host);

	return 0;
}

