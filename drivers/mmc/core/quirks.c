/*
 *  This file contains work-arounds for many known sdio hardware
 *  bugs.
 *
 *  Copyright (c) 2011 Pierre Tardy <tardyp@gmail.com>
 *  Inspired from pci fixup code:
 *  Copyright (c) 1999 Martin Mares <mj@ucw.cz>
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mmc/card.h>

static const struct mmc_fixup mmc_fixup_methods[] = {
	END_FIXUP
};

void mmc_fixup_device(struct mmc_card *card,
		      const struct mmc_fixup *table)
{
	const struct mmc_fixup *f;
	u64 rev = cid_rev_card(card);

	/* Non-core specific workarounds. */
	if (!table)
		table = mmc_fixup_methods;

	for (f = table; f->vendor_fixup; f++) {
		if ((f->manfid == CID_MANFID_ANY
		     || f->manfid == card->cid.manfid) &&
		    (f->oemid == CID_OEMID_ANY
		     || f->oemid == card->cid.oemid) &&
		    (f->name == CID_NAME_ANY
		     || !strcmp(f->name, card->cid.prod_name)) &&
		    (f->cis_vendor == card->cis.vendor
		     || f->cis_vendor == (u16) SDIO_ANY_ID) &&
		    (f->cis_device == card->cis.device
		    || f->cis_device == (u16) SDIO_ANY_ID) &&
		    rev >= f->rev_start &&
		    rev <= f->rev_end) {
			dev_dbg(&card->dev, "calling %pF\n", f->vendor_fixup);
			f->vendor_fixup(card, f->data);
		}
	}
}
EXPORT_SYMBOL(mmc_fixup_device);

