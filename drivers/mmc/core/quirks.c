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
#include <linux/mod_devicetable.h>

/*
 *  The world is not perfect and supplies us with broken mmc/sdio devices.
 *  For at least a part of these bugs we need a work-around
 */

struct mmc_fixup {
	u16 vendor, device;	/* You can use SDIO_ANY_ID here of course */
	void (*vendor_fixup)(struct mmc_card *card, int data);
	int data;
};

/*
 * This hook just adds a quirk unconditionnally
 */
static void __maybe_unused add_quirk(struct mmc_card *card, int data)
{
	card->quirks |= data;
}

/*
 * This hook just removes a quirk unconditionnally
 */
static void __maybe_unused remove_quirk(struct mmc_card *card, int data)
{
	card->quirks &= ~data;
}

/*
 * This hook just adds a quirk for all sdio devices
 */
static void add_quirk_for_sdio_devices(struct mmc_card *card, int data)
{
	if (mmc_card_sdio(card))
		card->quirks |= data;
}

#ifndef SDIO_VENDOR_ID_TI
#define SDIO_VENDOR_ID_TI		0x0097
#endif

#ifndef SDIO_DEVICE_ID_TI_WL1271
#define SDIO_DEVICE_ID_TI_WL1271	0x4076
#endif

static const struct mmc_fixup mmc_fixup_methods[] = {
	/* by default sdio devices are considered CLK_GATING broken */
	/* good cards will be whitelisted as they are tested */
	{ SDIO_ANY_ID, SDIO_ANY_ID,
		add_quirk_for_sdio_devices, MMC_QUIRK_BROKEN_CLK_GATING },
	{ SDIO_VENDOR_ID_TI, SDIO_DEVICE_ID_TI_WL1271,
		remove_quirk, MMC_QUIRK_BROKEN_CLK_GATING },
	{ 0 }
};

void mmc_fixup_device(struct mmc_card *card)
{
	const struct mmc_fixup *f;

	for (f = mmc_fixup_methods; f->vendor_fixup; f++) {
		if ((f->vendor == card->cis.vendor
		     || f->vendor == (u16) SDIO_ANY_ID) &&
		    (f->device == card->cis.device
		     || f->device == (u16) SDIO_ANY_ID)) {
			dev_dbg(&card->dev, "calling %pF\n", f->vendor_fixup);
			f->vendor_fixup(card, f->data);
		}
	}
}
EXPORT_SYMBOL(mmc_fixup_device);
