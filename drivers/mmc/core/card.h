/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Private header for the mmc subsystem
 *
 * Copyright (C) 2016 Linaro Ltd
 *
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 */

#ifndef _MMC_CORE_CARD_H
#define _MMC_CORE_CARD_H

#include <linux/mmc/card.h>

#define mmc_card_name(c)	((c)->cid.prod_name)
#define mmc_card_id(c)		(dev_name(&(c)->dev))
#define mmc_dev_to_card(d)	container_of(d, struct mmc_card, dev)

/* Card states */
#define MMC_STATE_PRESENT	(1<<0)		/* present in sysfs */
#define MMC_STATE_READONLY	(1<<1)		/* card is read-only */
#define MMC_STATE_BLOCKADDR	(1<<2)		/* card uses block-addressing */
#define MMC_CARD_SDXC		(1<<3)		/* card is SDXC */
#define MMC_CARD_REMOVED	(1<<4)		/* card has been removed */
#define MMC_STATE_SUSPENDED	(1<<5)		/* card is suspended */

#define mmc_card_present(c)	((c)->state & MMC_STATE_PRESENT)
#define mmc_card_readonly(c)	((c)->state & MMC_STATE_READONLY)
#define mmc_card_blockaddr(c)	((c)->state & MMC_STATE_BLOCKADDR)
#define mmc_card_ext_capacity(c) ((c)->state & MMC_CARD_SDXC)
#define mmc_card_removed(c)	((c) && ((c)->state & MMC_CARD_REMOVED))
#define mmc_card_suspended(c)	((c)->state & MMC_STATE_SUSPENDED)

#define mmc_card_set_present(c)	((c)->state |= MMC_STATE_PRESENT)
#define mmc_card_set_readonly(c) ((c)->state |= MMC_STATE_READONLY)
#define mmc_card_set_blockaddr(c) ((c)->state |= MMC_STATE_BLOCKADDR)
#define mmc_card_set_ext_capacity(c) ((c)->state |= MMC_CARD_SDXC)
#define mmc_card_set_removed(c) ((c)->state |= MMC_CARD_REMOVED)
#define mmc_card_set_suspended(c) ((c)->state |= MMC_STATE_SUSPENDED)
#define mmc_card_clr_suspended(c) ((c)->state &= ~MMC_STATE_SUSPENDED)

/*
 * The world is not perfect and supplies us with broken mmc/sdio devices.
 * For at least some of these bugs we need a work-around.
 */
struct mmc_fixup {
	/* CID-specific fields. */
	const char *name;

	/* Valid revision range */
	u64 rev_start, rev_end;

	unsigned int manfid;
	unsigned short oemid;

	/* Manufacturing date */
	unsigned short year;
	unsigned char month;

	/* SDIO-specific fields. You can use SDIO_ANY_ID here of course */
	u16 cis_vendor, cis_device;

	/* for MMC cards */
	unsigned int ext_csd_rev;

	/* Match against functions declared in device tree */
	const char *of_compatible;

	void (*vendor_fixup)(struct mmc_card *card, int data);
	int data;
};

#define CID_MANFID_ANY (-1u)
#define CID_OEMID_ANY ((unsigned short) -1)
#define CID_YEAR_ANY ((unsigned short) -1)
#define CID_MONTH_ANY ((unsigned char) -1)
#define CID_NAME_ANY (NULL)

#define EXT_CSD_REV_ANY (-1u)

#define CID_MANFID_SANDISK      0x2
#define CID_MANFID_SANDISK_SD   0x3
#define CID_MANFID_ATP          0x9
#define CID_MANFID_TOSHIBA      0x11
#define CID_MANFID_MICRON       0x13
#define CID_MANFID_SAMSUNG      0x15
#define CID_MANFID_APACER       0x27
#define CID_MANFID_KINGSTON     0x70
#define CID_MANFID_HYNIX	0x90
#define CID_MANFID_KINGSTON_SD	0x9F
#define CID_MANFID_NUMONYX	0xFE

#define END_FIXUP { NULL }

#define _FIXUP_EXT(_name, _manfid, _oemid, _year, _month,	\
		   _rev_start, _rev_end,			\
		   _cis_vendor, _cis_device,			\
		   _fixup, _data, _ext_csd_rev)			\
	{						\
		.name = (_name),			\
		.manfid = (_manfid),			\
		.oemid = (_oemid),			\
		.year = (_year),			\
		.month = (_month),			\
		.rev_start = (_rev_start),		\
		.rev_end = (_rev_end),			\
		.cis_vendor = (_cis_vendor),		\
		.cis_device = (_cis_device),		\
		.vendor_fixup = (_fixup),		\
		.data = (_data),			\
		.ext_csd_rev = (_ext_csd_rev),		\
	}

#define MMC_FIXUP_REV(_name, _manfid, _oemid, _rev_start, _rev_end,	\
		      _fixup, _data, _ext_csd_rev)			\
	_FIXUP_EXT(_name, _manfid, _oemid, CID_YEAR_ANY, CID_MONTH_ANY,	\
		   _rev_start, _rev_end,				\
		   SDIO_ANY_ID, SDIO_ANY_ID,				\
		   _fixup, _data, _ext_csd_rev)				\

#define MMC_FIXUP(_name, _manfid, _oemid, _fixup, _data) \
	MMC_FIXUP_REV(_name, _manfid, _oemid, 0, -1ull, _fixup, _data,	\
		      EXT_CSD_REV_ANY)

#define MMC_FIXUP_EXT_CSD_REV(_name, _manfid, _oemid, _fixup, _data,	\
			      _ext_csd_rev)				\
	MMC_FIXUP_REV(_name, _manfid, _oemid, 0, -1ull, _fixup, _data,	\
		      _ext_csd_rev)

#define SDIO_FIXUP(_vendor, _device, _fixup, _data)			\
	_FIXUP_EXT(CID_NAME_ANY, CID_MANFID_ANY, CID_OEMID_ANY,		\
		   CID_YEAR_ANY, CID_MONTH_ANY,				\
		   0, -1ull,						\
		   _vendor, _device,					\
		   _fixup, _data, EXT_CSD_REV_ANY)			\

#define SDIO_FIXUP_COMPATIBLE(_compatible, _fixup, _data)		\
	{						\
		.name = CID_NAME_ANY,			\
		.manfid = CID_MANFID_ANY,		\
		.oemid = CID_OEMID_ANY,			\
		.rev_start = 0,				\
		.rev_end = -1ull,			\
		.cis_vendor = SDIO_ANY_ID,		\
		.cis_device = SDIO_ANY_ID,		\
		.vendor_fixup = (_fixup),		\
		.data = (_data),			\
		.ext_csd_rev = EXT_CSD_REV_ANY,		\
		.of_compatible = _compatible,	\
	}

#define cid_rev(hwrev, fwrev, year, month)	\
	(((u64) hwrev) << 40 |			\
	 ((u64) fwrev) << 32 |			\
	 ((u64) year) << 16 |			\
	 ((u64) month))

#define cid_rev_card(card)			\
	cid_rev(card->cid.hwrev,		\
		    card->cid.fwrev,		\
		    card->cid.year,		\
		    card->cid.month)

/*
 * Unconditionally quirk add/remove.
 */
static inline void __maybe_unused add_quirk(struct mmc_card *card, int data)
{
	card->quirks |= data;
}

static inline void __maybe_unused remove_quirk(struct mmc_card *card, int data)
{
	card->quirks &= ~data;
}

static inline void __maybe_unused add_limit_rate_quirk(struct mmc_card *card,
						       int data)
{
	card->quirk_max_rate = data;
}

static inline void __maybe_unused wl1251_quirk(struct mmc_card *card,
					       int data)
{
	/*
	 * We have TI wl1251 attached to this mmc. Pass this
	 * information to the SDIO core because it can't be
	 * probed by normal methods.
	 */

	dev_info(card->host->parent, "found wl1251\n");
	card->quirks |= MMC_QUIRK_NONSTD_SDIO;
	card->cccr.wide_bus = 1;
	card->cis.vendor = 0x104c;
	card->cis.device = 0x9066;
	card->cis.blksize = 512;
	card->cis.max_dtr = 24000000;
}

/*
 * Quirk add/remove for MMC products.
 */
static inline void __maybe_unused add_quirk_mmc(struct mmc_card *card, int data)
{
	if (mmc_card_mmc(card))
		card->quirks |= data;
}

static inline void __maybe_unused remove_quirk_mmc(struct mmc_card *card,
						   int data)
{
	if (mmc_card_mmc(card))
		card->quirks &= ~data;
}

/*
 * Quirk add/remove for SD products.
 */
static inline void __maybe_unused add_quirk_sd(struct mmc_card *card, int data)
{
	if (mmc_card_sd(card))
		card->quirks |= data;
}

static inline void __maybe_unused remove_quirk_sd(struct mmc_card *card,
						   int data)
{
	if (mmc_card_sd(card))
		card->quirks &= ~data;
}

static inline int mmc_card_lenient_fn0(const struct mmc_card *c)
{
	return c->quirks & MMC_QUIRK_LENIENT_FN0;
}

static inline int mmc_blksz_for_byte_mode(const struct mmc_card *c)
{
	return c->quirks & MMC_QUIRK_BLKSZ_FOR_BYTE_MODE;
}

static inline int mmc_card_disable_cd(const struct mmc_card *c)
{
	return c->quirks & MMC_QUIRK_DISABLE_CD;
}

static inline int mmc_card_nonstd_func_interface(const struct mmc_card *c)
{
	return c->quirks & MMC_QUIRK_NONSTD_FUNC_IF;
}

static inline int mmc_card_broken_byte_mode_512(const struct mmc_card *c)
{
	return c->quirks & MMC_QUIRK_BROKEN_BYTE_MODE_512;
}

static inline int mmc_card_long_read_time(const struct mmc_card *c)
{
	return c->quirks & MMC_QUIRK_LONG_READ_TIME;
}

static inline int mmc_card_broken_irq_polling(const struct mmc_card *c)
{
	return c->quirks & MMC_QUIRK_BROKEN_IRQ_POLLING;
}

static inline int mmc_card_broken_hpi(const struct mmc_card *c)
{
	return c->quirks & MMC_QUIRK_BROKEN_HPI;
}

static inline int mmc_card_broken_sd_discard(const struct mmc_card *c)
{
	return c->quirks & MMC_QUIRK_BROKEN_SD_DISCARD;
}

static inline int mmc_card_broken_sd_cache(const struct mmc_card *c)
{
	return c->quirks & MMC_QUIRK_BROKEN_SD_CACHE;
}

static inline int mmc_card_broken_cache_flush(const struct mmc_card *c)
{
	return c->quirks & MMC_QUIRK_BROKEN_CACHE_FLUSH;
}
#endif
