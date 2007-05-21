/*
 *  linux/drivers/mmc/sdio.c
 *
 *  Copyright 2006-2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/err.h>

#include <linux/mmc/host.h>
#include <linux/mmc/card.h>

#include "core.h"
#include "bus.h"
#include "mmc_ops.h"
#include "sd_ops.h"
#include "sdio_ops.h"

/*
 * Host is being removed. Free up the current card.
 */
static void mmc_sdio_remove(struct mmc_host *host)
{
	BUG_ON(!host);
	BUG_ON(!host->card);

	mmc_remove_card(host->card);
	host->card = NULL;
}

/*
 * Card detection callback from host.
 */
static void mmc_sdio_detect(struct mmc_host *host)
{
	int err;

	BUG_ON(!host);
	BUG_ON(!host->card);

	mmc_claim_host(host);

	/*
	 * Just check if our card has been removed.
	 */
	err = mmc_select_card(host->card);

	mmc_release_host(host);

	if (err) {
		mmc_sdio_remove(host);

		mmc_claim_host(host);
		mmc_detach_bus(host);
		mmc_release_host(host);
	}
}


static const struct mmc_bus_ops mmc_sdio_ops = {
	.remove = mmc_sdio_remove,
	.detect = mmc_sdio_detect,
};


/*
 * Starting point for SDIO card init.
 */
int mmc_attach_sdio(struct mmc_host *host, u32 ocr)
{
	int err;
	int funcs;
	struct mmc_card *card;

	BUG_ON(!host);
	BUG_ON(!host->claimed);

	mmc_attach_bus(host, &mmc_sdio_ops);

	/*
	 * Sanity check the voltages that the card claims to
	 * support.
	 */
	if (ocr & 0x7F) {
		printk(KERN_WARNING "%s: card claims to support voltages "
		       "below the defined range. These will be ignored.\n",
		       mmc_hostname(host));
		ocr &= ~0x7F;
	}

	if (ocr & MMC_VDD_165_195) {
		printk(KERN_WARNING "%s: SDIO card claims to support the "
		       "incompletely defined 'low voltage range'. This "
		       "will be ignored.\n", mmc_hostname(host));
		ocr &= ~MMC_VDD_165_195;
	}

	host->ocr = mmc_select_voltage(host, ocr);

	/*
	 * Can we support the voltage(s) of the card(s)?
	 */
	if (!host->ocr) {
		err = -EINVAL;
		goto err;
	}

	/*
	 * Inform the card of the voltage
	 */
	err = mmc_send_io_op_cond(host, host->ocr, &ocr);
	if (err)
		goto err;

	/*
	 * The number of functions on the card is encoded inside
	 * the ocr.
	 */
	funcs = (ocr & 0x70000000) >> 28;

	/*
	 * Allocate card structure.
	 */
	card = mmc_alloc_card(host);
	if (IS_ERR(card)) {
		err = PTR_ERR(card);
		goto err;
	}

	card->type = MMC_TYPE_SDIO;

	/*
	 * Set card RCA.
	 */
	err = mmc_send_relative_addr(host, &card->rca);
	if (err)
		goto free_card;

	mmc_set_bus_mode(host, MMC_BUSMODE_PUSHPULL);

	/*
	 * Select card, as all following commands rely on that.
	 */
	err = mmc_select_card(card);
	if (err)
		goto free_card;

	host->card = card;

	mmc_release_host(host);

	err = mmc_add_card(host->card);
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

	printk(KERN_ERR "%s: error %d whilst initialising SDIO card\n",
		mmc_hostname(host), err);

	return err;
}

