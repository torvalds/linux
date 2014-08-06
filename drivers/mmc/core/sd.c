/*
 *  linux/drivers/mmc/core/sd.c
 *
 *  Copyright (C) 2003-2004 Russell King, All Rights Reserved.
 *  SD support Copyright (C) 2004 Ian Molton, All Rights Reserved.
 *  Copyright (C) 2005-2007 Pierre Ossman, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/pm_runtime.h>

#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>

#include "core.h"
#include "bus.h"
#include "mmc_ops.h"
#include "sd.h"
#include "sd_ops.h"

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

static const unsigned int sd_au_size[] = {
	0,		SZ_16K / 512,		SZ_32K / 512,	SZ_64K / 512,
	SZ_128K / 512,	SZ_256K / 512,		SZ_512K / 512,	SZ_1M / 512,
	SZ_2M / 512,	SZ_4M / 512,		SZ_8M / 512,	(SZ_8M + SZ_4M) / 512,
	SZ_16M / 512,	(SZ_16M + SZ_8M) / 512,	SZ_32M / 512,	SZ_64M / 512,
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
void mmc_decode_cid(struct mmc_card *card)
{
	u32 *resp = card->raw_cid;

	memset(&card->cid, 0, sizeof(struct mmc_cid));

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
}

/*
 * Given a 128-bit response, decode to our card CSD structure.
 */
static int mmc_decode_csd(struct mmc_card *card)
{
	struct mmc_csd *csd = &card->csd;
	unsigned int e, m, csd_struct;
	u32 *resp = card->raw_csd;

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
		csd->dsr_imp = UNSTUFF_BITS(resp, 76, 1);
		csd->r2w_factor = UNSTUFF_BITS(resp, 26, 3);
		csd->write_blkbits = UNSTUFF_BITS(resp, 22, 4);
		csd->write_partial = UNSTUFF_BITS(resp, 21, 1);

		if (UNSTUFF_BITS(resp, 46, 1)) {
			csd->erase_size = 1;
		} else if (csd->write_blkbits >= 9) {
			csd->erase_size = UNSTUFF_BITS(resp, 39, 7) + 1;
			csd->erase_size <<= csd->write_blkbits - 9;
		}
		break;
	case 1:
		/*
		 * This is a block-addressed SDHC or SDXC card. Most
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
		csd->c_size	  = UNSTUFF_BITS(resp, 48, 22);

		/* SDXC cards have a minimum C_SIZE of 0x00FFFF */
		if (csd->c_size >= 0xFFFF)
			mmc_card_set_ext_capacity(card);

		m = UNSTUFF_BITS(resp, 48, 22);
		csd->capacity     = (1 + m) << 10;

		csd->read_blkbits = 9;
		csd->read_partial = 0;
		csd->write_misalign = 0;
		csd->read_misalign = 0;
		csd->r2w_factor = 4; /* Unused */
		csd->write_blkbits = 9;
		csd->write_partial = 0;
		csd->erase_size = 1;
		break;
	default:
		pr_err("%s: unrecognised CSD structure version %d\n",
			mmc_hostname(card->host), csd_struct);
		return -EINVAL;
	}

	card->erase_size = csd->erase_size;

	return 0;
}

/*
 * Given a 64-bit response, decode to our card SCR structure.
 */
static int mmc_decode_scr(struct mmc_card *card)
{
	struct sd_scr *scr = &card->scr;
	unsigned int scr_struct;
	u32 resp[4];

	resp[3] = card->raw_scr[1];
	resp[2] = card->raw_scr[0];

	scr_struct = UNSTUFF_BITS(resp, 60, 4);
	if (scr_struct != 0) {
		pr_err("%s: unrecognised SCR structure version %d\n",
			mmc_hostname(card->host), scr_struct);
		return -EINVAL;
	}

	scr->sda_vsn = UNSTUFF_BITS(resp, 56, 4);
	scr->bus_widths = UNSTUFF_BITS(resp, 48, 4);
	if (scr->sda_vsn == SCR_SPEC_VER_2)
		/* Check if Physical Layer Spec v3.0 is supported */
		scr->sda_spec3 = UNSTUFF_BITS(resp, 47, 1);

	if (UNSTUFF_BITS(resp, 55, 1))
		card->erased_byte = 0xFF;
	else
		card->erased_byte = 0x0;

	if (scr->sda_spec3)
		scr->cmds = UNSTUFF_BITS(resp, 32, 2);
	return 0;
}

/*
 * Fetch and process SD Status register.
 */
static int mmc_read_ssr(struct mmc_card *card)
{
	unsigned int au, es, et, eo;
	int err, i;
	u32 *ssr;

	if (!(card->csd.cmdclass & CCC_APP_SPEC)) {
		pr_warn("%s: card lacks mandatory SD Status function\n",
			mmc_hostname(card->host));
		return 0;
	}

	ssr = kmalloc(64, GFP_KERNEL);
	if (!ssr)
		return -ENOMEM;

	err = mmc_app_sd_status(card, ssr);
	if (err) {
		pr_warn("%s: problem reading SD Status register\n",
			mmc_hostname(card->host));
		err = 0;
		goto out;
	}

	for (i = 0; i < 16; i++)
		ssr[i] = be32_to_cpu(ssr[i]);

	/*
	 * UNSTUFF_BITS only works with four u32s so we have to offset the
	 * bitfield positions accordingly.
	 */
	au = UNSTUFF_BITS(ssr, 428 - 384, 4);
	if (au) {
		if (au <= 9 || card->scr.sda_spec3) {
			card->ssr.au = sd_au_size[au];
			es = UNSTUFF_BITS(ssr, 408 - 384, 16);
			et = UNSTUFF_BITS(ssr, 402 - 384, 6);
			if (es && et) {
				eo = UNSTUFF_BITS(ssr, 400 - 384, 2);
				card->ssr.erase_timeout = (et * 1000) / es;
				card->ssr.erase_offset = eo * 1000;
			}
		} else {
			pr_warn("%s: SD Status: Invalid Allocation Unit size\n",
				mmc_hostname(card->host));
		}
	}
out:
	kfree(ssr);
	return err;
}

/*
 * Fetches and decodes switch information
 */
static int mmc_read_switch(struct mmc_card *card)
{
	int err;
	u8 *status;

	if (card->scr.sda_vsn < SCR_SPEC_VER_1)
		return 0;

	if (!(card->csd.cmdclass & CCC_SWITCH)) {
		pr_warn("%s: card lacks mandatory switch function, performance might suffer\n",
			mmc_hostname(card->host));
		return 0;
	}

	err = -EIO;

	status = kmalloc(64, GFP_KERNEL);
	if (!status) {
		pr_err("%s: could not allocate a buffer for "
			"switch capabilities.\n",
			mmc_hostname(card->host));
		return -ENOMEM;
	}

	/*
	 * Find out the card's support bits with a mode 0 operation.
	 * The argument does not matter, as the support bits do not
	 * change with the arguments.
	 */
	err = mmc_sd_switch(card, 0, 0, 0, status);
	if (err) {
		/*
		 * If the host or the card can't do the switch,
		 * fail more gracefully.
		 */
		if (err != -EINVAL && err != -ENOSYS && err != -EFAULT)
			goto out;

		pr_warn("%s: problem reading Bus Speed modes\n",
			mmc_hostname(card->host));
		err = 0;

		goto out;
	}

	if (status[13] & SD_MODE_HIGH_SPEED)
		card->sw_caps.hs_max_dtr = HIGH_SPEED_MAX_DTR;

	if (card->scr.sda_spec3) {
		card->sw_caps.sd3_bus_mode = status[13];
		/* Driver Strengths supported by the card */
		card->sw_caps.sd3_drv_type = status[9];
	}

out:
	kfree(status);

	return err;
}

/*
 * Test if the card supports high-speed mode and, if so, switch to it.
 */
int mmc_sd_switch_hs(struct mmc_card *card)
{
	int err;
	u8 *status;

	if (card->scr.sda_vsn < SCR_SPEC_VER_1)
		return 0;

	if (!(card->csd.cmdclass & CCC_SWITCH))
		return 0;

	if (!(card->host->caps & MMC_CAP_SD_HIGHSPEED))
		return 0;

	if (card->sw_caps.hs_max_dtr == 0)
		return 0;

	err = -EIO;

	status = kmalloc(64, GFP_KERNEL);
	if (!status) {
		pr_err("%s: could not allocate a buffer for "
			"switch capabilities.\n", mmc_hostname(card->host));
		return -ENOMEM;
	}

	err = mmc_sd_switch(card, 1, 0, 1, status);
	if (err)
		goto out;

	if ((status[16] & 0xF) != 1) {
		pr_warn("%s: Problem switching card into high-speed mode!\n",
			mmc_hostname(card->host));
		err = 0;
	} else {
		err = 1;
	}

out:
	kfree(status);

	return err;
}

static int sd_select_driver_type(struct mmc_card *card, u8 *status)
{
	int card_drv_type, drive_strength, drv_type;
	int err;

	card->drive_strength = 0;

	card_drv_type = card->sw_caps.sd3_drv_type | SD_DRIVER_TYPE_B;

	drive_strength = mmc_select_drive_strength(card,
						   card->sw_caps.uhs_max_dtr,
						   card_drv_type, &drv_type);

	if (drive_strength) {
		err = mmc_sd_switch(card, 1, 2, drive_strength, status);
		if (err)
			return err;
		if ((status[15] & 0xF) != drive_strength) {
			pr_warn("%s: Problem setting drive strength!\n",
				mmc_hostname(card->host));
			return 0;
		}
		card->drive_strength = drive_strength;
	}

	if (drv_type)
		mmc_set_driver_type(card->host, drv_type);

	return 0;
}

static void sd_update_bus_speed_mode(struct mmc_card *card)
{
	/*
	 * If the host doesn't support any of the UHS-I modes, fallback on
	 * default speed.
	 */
	if (!mmc_host_uhs(card->host)) {
		card->sd_bus_speed = 0;
		return;
	}

	if ((card->host->caps & MMC_CAP_UHS_SDR104) &&
	    (card->sw_caps.sd3_bus_mode & SD_MODE_UHS_SDR104)) {
			card->sd_bus_speed = UHS_SDR104_BUS_SPEED;
	} else if ((card->host->caps & MMC_CAP_UHS_DDR50) &&
		   (card->sw_caps.sd3_bus_mode & SD_MODE_UHS_DDR50)) {
			card->sd_bus_speed = UHS_DDR50_BUS_SPEED;
	} else if ((card->host->caps & (MMC_CAP_UHS_SDR104 |
		    MMC_CAP_UHS_SDR50)) && (card->sw_caps.sd3_bus_mode &
		    SD_MODE_UHS_SDR50)) {
			card->sd_bus_speed = UHS_SDR50_BUS_SPEED;
	} else if ((card->host->caps & (MMC_CAP_UHS_SDR104 |
		    MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_SDR25)) &&
		   (card->sw_caps.sd3_bus_mode & SD_MODE_UHS_SDR25)) {
			card->sd_bus_speed = UHS_SDR25_BUS_SPEED;
	} else if ((card->host->caps & (MMC_CAP_UHS_SDR104 |
		    MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_SDR25 |
		    MMC_CAP_UHS_SDR12)) && (card->sw_caps.sd3_bus_mode &
		    SD_MODE_UHS_SDR12)) {
			card->sd_bus_speed = UHS_SDR12_BUS_SPEED;
	}
}

static int sd_set_bus_speed_mode(struct mmc_card *card, u8 *status)
{
	int err;
	unsigned int timing = 0;

	switch (card->sd_bus_speed) {
	case UHS_SDR104_BUS_SPEED:
		timing = MMC_TIMING_UHS_SDR104;
		card->sw_caps.uhs_max_dtr = UHS_SDR104_MAX_DTR;
		break;
	case UHS_DDR50_BUS_SPEED:
		timing = MMC_TIMING_UHS_DDR50;
		card->sw_caps.uhs_max_dtr = UHS_DDR50_MAX_DTR;
		break;
	case UHS_SDR50_BUS_SPEED:
		timing = MMC_TIMING_UHS_SDR50;
		card->sw_caps.uhs_max_dtr = UHS_SDR50_MAX_DTR;
		break;
	case UHS_SDR25_BUS_SPEED:
		timing = MMC_TIMING_UHS_SDR25;
		card->sw_caps.uhs_max_dtr = UHS_SDR25_MAX_DTR;
		break;
	case UHS_SDR12_BUS_SPEED:
		timing = MMC_TIMING_UHS_SDR12;
		card->sw_caps.uhs_max_dtr = UHS_SDR12_MAX_DTR;
		break;
	default:
		return 0;
	}

	err = mmc_sd_switch(card, 1, 0, card->sd_bus_speed, status);
	if (err)
		return err;

	if ((status[16] & 0xF) != card->sd_bus_speed)
		pr_warn("%s: Problem setting bus speed mode!\n",
			mmc_hostname(card->host));
	else {
		mmc_set_timing(card->host, timing);
		mmc_set_clock(card->host, card->sw_caps.uhs_max_dtr);

		/*
		 * FIXME: Sandisk SD3.0 cards DDR50 mode requires such
		 * delay to get stable, without this delay we may encounter
		 * CRC errors after switch to DDR50 mode
		 */
		mmc_delay(100);
	}

	return 0;
}

/* Get host's max current setting at its current voltage */
static u32 sd_get_host_max_current(struct mmc_host *host)
{
	u32 voltage, max_current;

	voltage = 1 << host->ios.vdd;
	switch (voltage) {
	case MMC_VDD_165_195:
		max_current = host->max_current_180;
		break;
	case MMC_VDD_29_30:
	case MMC_VDD_30_31:
		max_current = host->max_current_300;
		break;
	case MMC_VDD_32_33:
	case MMC_VDD_33_34:
		max_current = host->max_current_330;
		break;
	default:
		max_current = 0;
	}

	return max_current;
}

static int sd_set_current_limit(struct mmc_card *card, u8 *status)
{
	int current_limit = SD_SET_CURRENT_NO_CHANGE;
	int err;
	u32 max_current;

	/*
	 * Current limit switch is only defined for SDR50, SDR104, and DDR50
	 * bus speed modes. For other bus speed modes, we do not change the
	 * current limit.
	 */
	if ((card->sd_bus_speed != UHS_SDR50_BUS_SPEED) &&
	    (card->sd_bus_speed != UHS_SDR104_BUS_SPEED) &&
	    (card->sd_bus_speed != UHS_DDR50_BUS_SPEED))
		return 0;

	/*
	 * Host has different current capabilities when operating at
	 * different voltages, so find out its max current first.
	 */
	max_current = sd_get_host_max_current(card->host);

	/*
	 * We only check host's capability here, if we set a limit that is
	 * higher than the card's maximum current, the card will be using its
	 * maximum current, e.g. if the card's maximum current is 300ma, and
	 * when we set current limit to 200ma, the card will draw 200ma, and
	 * when we set current limit to 400/600/800ma, the card will draw its
	 * maximum 300ma from the host.
	 */
	if (max_current >= 800)
		current_limit = SD_SET_CURRENT_LIMIT_800;
	else if (max_current >= 600)
		current_limit = SD_SET_CURRENT_LIMIT_600;
	else if (max_current >= 400)
		current_limit = SD_SET_CURRENT_LIMIT_400;
	else if (max_current >= 200)
		current_limit = SD_SET_CURRENT_LIMIT_200;

	if (current_limit != SD_SET_CURRENT_NO_CHANGE) {
		err = mmc_sd_switch(card, 1, 3, current_limit, status);
		if (err)
			return err;

		if (((status[15] >> 4) & 0x0F) != current_limit)
			pr_warn("%s: Problem setting current limit!\n",
				mmc_hostname(card->host));

	}

	return 0;
}

/*
 * UHS-I specific initialization procedure
 */
static int mmc_sd_init_uhs_card(struct mmc_card *card)
{
	int err;
	u8 *status;

	if (!card->scr.sda_spec3)
		return 0;

	if (!(card->csd.cmdclass & CCC_SWITCH))
		return 0;

	status = kmalloc(64, GFP_KERNEL);
	if (!status) {
		pr_err("%s: could not allocate a buffer for "
			"switch capabilities.\n", mmc_hostname(card->host));
		return -ENOMEM;
	}

	/* Set 4-bit bus width */
	if ((card->host->caps & MMC_CAP_4_BIT_DATA) &&
	    (card->scr.bus_widths & SD_SCR_BUS_WIDTH_4)) {
		err = mmc_app_set_bus_width(card, MMC_BUS_WIDTH_4);
		if (err)
			goto out;

		mmc_set_bus_width(card->host, MMC_BUS_WIDTH_4);
	}

	/*
	 * Select the bus speed mode depending on host
	 * and card capability.
	 */
	sd_update_bus_speed_mode(card);

	/* Set the driver strength for the card */
	err = sd_select_driver_type(card, status);
	if (err)
		goto out;

	/* Set current limit for the card */
	err = sd_set_current_limit(card, status);
	if (err)
		goto out;

	/* Set bus speed mode of the card */
	err = sd_set_bus_speed_mode(card, status);
	if (err)
		goto out;

	/*
	 * SPI mode doesn't define CMD19 and tuning is only valid for SDR50 and
	 * SDR104 mode SD-cards. Note that tuning is mandatory for SDR104.
	 */
	if (!mmc_host_is_spi(card->host) &&
	    (card->sd_bus_speed == UHS_SDR50_BUS_SPEED ||
	     card->sd_bus_speed == UHS_SDR104_BUS_SPEED))
		err = mmc_execute_tuning(card);
out:
	kfree(status);

	return err;
}

MMC_DEV_ATTR(cid, "%08x%08x%08x%08x\n", card->raw_cid[0], card->raw_cid[1],
	card->raw_cid[2], card->raw_cid[3]);
MMC_DEV_ATTR(csd, "%08x%08x%08x%08x\n", card->raw_csd[0], card->raw_csd[1],
	card->raw_csd[2], card->raw_csd[3]);
MMC_DEV_ATTR(scr, "%08x%08x\n", card->raw_scr[0], card->raw_scr[1]);
MMC_DEV_ATTR(date, "%02d/%04d\n", card->cid.month, card->cid.year);
MMC_DEV_ATTR(erase_size, "%u\n", card->erase_size << 9);
MMC_DEV_ATTR(preferred_erase_size, "%u\n", card->pref_erase << 9);
MMC_DEV_ATTR(fwrev, "0x%x\n", card->cid.fwrev);
MMC_DEV_ATTR(hwrev, "0x%x\n", card->cid.hwrev);
MMC_DEV_ATTR(manfid, "0x%06x\n", card->cid.manfid);
MMC_DEV_ATTR(name, "%s\n", card->cid.prod_name);
MMC_DEV_ATTR(oemid, "0x%04x\n", card->cid.oemid);
MMC_DEV_ATTR(serial, "0x%08x\n", card->cid.serial);


static struct attribute *sd_std_attrs[] = {
	&dev_attr_cid.attr,
	&dev_attr_csd.attr,
	&dev_attr_scr.attr,
	&dev_attr_date.attr,
	&dev_attr_erase_size.attr,
	&dev_attr_preferred_erase_size.attr,
	&dev_attr_fwrev.attr,
	&dev_attr_hwrev.attr,
	&dev_attr_manfid.attr,
	&dev_attr_name.attr,
	&dev_attr_oemid.attr,
	&dev_attr_serial.attr,
	NULL,
};
ATTRIBUTE_GROUPS(sd_std);

struct device_type sd_type = {
	.groups = sd_std_groups,
};

/*
 * Fetch CID from card.
 */
int mmc_sd_get_cid(struct mmc_host *host, u32 ocr, u32 *cid, u32 *rocr)
{
	int err;
	u32 max_current;
	int retries = 10;
	u32 pocr = ocr;

try_again:
	if (!retries) {
		ocr &= ~SD_OCR_S18R;
		pr_warn("%s: Skipping voltage switch\n", mmc_hostname(host));
	}

	/*
	 * Since we're changing the OCR value, we seem to
	 * need to tell some cards to go back to the idle
	 * state.  We wait 1ms to give cards time to
	 * respond.
	 */
	mmc_go_idle(host);

	/*
	 * If SD_SEND_IF_COND indicates an SD 2.0
	 * compliant card and we should set bit 30
	 * of the ocr to indicate that we can handle
	 * block-addressed SDHC cards.
	 */
	err = mmc_send_if_cond(host, ocr);
	if (!err)
		ocr |= SD_OCR_CCS;

	/*
	 * If the host supports one of UHS-I modes, request the card
	 * to switch to 1.8V signaling level. If the card has failed
	 * repeatedly to switch however, skip this.
	 */
	if (retries && mmc_host_uhs(host))
		ocr |= SD_OCR_S18R;

	/*
	 * If the host can supply more than 150mA at current voltage,
	 * XPC should be set to 1.
	 */
	max_current = sd_get_host_max_current(host);
	if (max_current > 150)
		ocr |= SD_OCR_XPC;

	err = mmc_send_app_op_cond(host, ocr, rocr);
	if (err)
		return err;

	/*
	 * In case CCS and S18A in the response is set, start Signal Voltage
	 * Switch procedure. SPI mode doesn't support CMD11.
	 */
	if (!mmc_host_is_spi(host) && rocr &&
	   ((*rocr & 0x41000000) == 0x41000000)) {
		err = mmc_set_signal_voltage(host, MMC_SIGNAL_VOLTAGE_180,
					pocr);
		if (err == -EAGAIN) {
			retries--;
			goto try_again;
		} else if (err) {
			retries = 0;
			goto try_again;
		}
	}

	if (mmc_host_is_spi(host))
		err = mmc_send_cid(host, cid);
	else
		err = mmc_all_send_cid(host, cid);

	return err;
}

int mmc_sd_get_csd(struct mmc_host *host, struct mmc_card *card)
{
	int err;

	/*
	 * Fetch CSD from card.
	 */
	err = mmc_send_csd(card, card->raw_csd);
	if (err)
		return err;

	err = mmc_decode_csd(card);
	if (err)
		return err;

	return 0;
}

static int mmc_sd_get_ro(struct mmc_host *host)
{
	int ro;

	/*
	 * Some systems don't feature a write-protect pin and don't need one.
	 * E.g. because they only have micro-SD card slot. For those systems
	 * assume that the SD card is always read-write.
	 */
	if (host->caps2 & MMC_CAP2_NO_WRITE_PROTECT)
		return 0;

	if (!host->ops->get_ro)
		return -1;

	mmc_host_clk_hold(host);
	ro = host->ops->get_ro(host);
	mmc_host_clk_release(host);

	return ro;
}

int mmc_sd_setup_card(struct mmc_host *host, struct mmc_card *card,
	bool reinit)
{
	int err;

	if (!reinit) {
		/*
		 * Fetch SCR from card.
		 */
		err = mmc_app_send_scr(card, card->raw_scr);
		if (err)
			return err;

		err = mmc_decode_scr(card);
		if (err)
			return err;

		/*
		 * Fetch and process SD Status register.
		 */
		err = mmc_read_ssr(card);
		if (err)
			return err;

		/* Erase init depends on CSD and SSR */
		mmc_init_erase(card);

		/*
		 * Fetch switch information from card.
		 */
		err = mmc_read_switch(card);
		if (err)
			return err;
	}

	/*
	 * For SPI, enable CRC as appropriate.
	 * This CRC enable is located AFTER the reading of the
	 * card registers because some SDHC cards are not able
	 * to provide valid CRCs for non-512-byte blocks.
	 */
	if (mmc_host_is_spi(host)) {
		err = mmc_spi_set_crc(host, use_spi_crc);
		if (err)
			return err;
	}

	/*
	 * Check if read-only switch is active.
	 */
	if (!reinit) {
		int ro = mmc_sd_get_ro(host);

		if (ro < 0) {
			pr_warn("%s: host does not support reading read-only switch, assuming write-enable\n",
				mmc_hostname(host));
		} else if (ro > 0) {
			mmc_card_set_readonly(card);
		}
	}

	return 0;
}

unsigned mmc_sd_get_max_clock(struct mmc_card *card)
{
	unsigned max_dtr = (unsigned int)-1;

	if (mmc_card_hs(card)) {
		if (max_dtr > card->sw_caps.hs_max_dtr)
			max_dtr = card->sw_caps.hs_max_dtr;
	} else if (max_dtr > card->csd.max_dtr) {
		max_dtr = card->csd.max_dtr;
	}

	return max_dtr;
}

/*
 * Handle the detection and initialisation of a card.
 *
 * In the case of a resume, "oldcard" will contain the card
 * we're trying to reinitialise.
 */
static int mmc_sd_init_card(struct mmc_host *host, u32 ocr,
	struct mmc_card *oldcard)
{
	struct mmc_card *card;
	int err;
	u32 cid[4];
	u32 rocr = 0;

	BUG_ON(!host);
	WARN_ON(!host->claimed);

	err = mmc_sd_get_cid(host, ocr, cid, &rocr);
	if (err)
		return err;

	if (oldcard) {
		if (memcmp(cid, oldcard->raw_cid, sizeof(cid)) != 0)
			return -ENOENT;

		card = oldcard;
	} else {
		/*
		 * Allocate card structure.
		 */
		card = mmc_alloc_card(host, &sd_type);
		if (IS_ERR(card))
			return PTR_ERR(card);

		card->ocr = ocr;
		card->type = MMC_TYPE_SD;
		memcpy(card->raw_cid, cid, sizeof(card->raw_cid));
	}

	/*
	 * Call the optional HC's init_card function to handle quirks.
	 */
	if (host->ops->init_card)
		host->ops->init_card(host, card);

	/*
	 * For native busses:  get card RCA and quit open drain mode.
	 */
	if (!mmc_host_is_spi(host)) {
		err = mmc_send_relative_addr(host, &card->rca);
		if (err)
			goto free_card;
	}

	if (!oldcard) {
		err = mmc_sd_get_csd(host, card);
		if (err)
			goto free_card;

		mmc_decode_cid(card);
	}

	/*
	 * handling only for cards supporting DSR and hosts requesting
	 * DSR configuration
	 */
	if (card->csd.dsr_imp && host->dsr_req)
		mmc_set_dsr(host);

	/*
	 * Select card, as all following commands rely on that.
	 */
	if (!mmc_host_is_spi(host)) {
		err = mmc_select_card(card);
		if (err)
			goto free_card;
	}

	err = mmc_sd_setup_card(host, card, oldcard != NULL);
	if (err)
		goto free_card;

	/* Initialization sequence for UHS-I cards */
	if (rocr & SD_ROCR_S18A) {
		err = mmc_sd_init_uhs_card(card);
		if (err)
			goto free_card;
	} else {
		/*
		 * Attempt to change to high-speed (if supported)
		 */
		err = mmc_sd_switch_hs(card);
		if (err > 0)
			mmc_set_timing(card->host, MMC_TIMING_SD_HS);
		else if (err)
			goto free_card;

		/*
		 * Set bus speed.
		 */
		mmc_set_clock(host, mmc_sd_get_max_clock(card));

		/*
		 * Switch to wider bus (if supported).
		 */
		if ((host->caps & MMC_CAP_4_BIT_DATA) &&
			(card->scr.bus_widths & SD_SCR_BUS_WIDTH_4)) {
			err = mmc_app_set_bus_width(card, MMC_BUS_WIDTH_4);
			if (err)
				goto free_card;

			mmc_set_bus_width(host, MMC_BUS_WIDTH_4);
		}
	}

	host->card = card;
	return 0;

free_card:
	if (!oldcard)
		mmc_remove_card(card);

	return err;
}

/*
 * Host is being removed. Free up the current card.
 */
static void mmc_sd_remove(struct mmc_host *host)
{
	BUG_ON(!host);
	BUG_ON(!host->card);

	mmc_remove_card(host->card);
	host->card = NULL;
}

/*
 * Card detection - card is alive.
 */
static int mmc_sd_alive(struct mmc_host *host)
{
	return mmc_send_status(host->card, NULL);
}

/*
 * Card detection callback from host.
 */
static void mmc_sd_detect(struct mmc_host *host)
{
	int err;

	BUG_ON(!host);
	BUG_ON(!host->card);

	mmc_get_card(host->card);

	/*
	 * Just check if our card has been removed.
	 */
	err = _mmc_detect_card_removed(host);

	mmc_put_card(host->card);

	if (err) {
		mmc_sd_remove(host);

		mmc_claim_host(host);
		mmc_detach_bus(host);
		mmc_power_off(host);
		mmc_release_host(host);
	}
}

static int _mmc_sd_suspend(struct mmc_host *host)
{
	int err = 0;

	BUG_ON(!host);
	BUG_ON(!host->card);

	mmc_claim_host(host);

	if (mmc_card_suspended(host->card))
		goto out;

	if (!mmc_host_is_spi(host))
		err = mmc_deselect_cards(host);

	if (!err) {
		mmc_power_off(host);
		mmc_card_set_suspended(host->card);
	}

out:
	mmc_release_host(host);
	return err;
}

/*
 * Callback for suspend
 */
static int mmc_sd_suspend(struct mmc_host *host)
{
	int err;

	err = _mmc_sd_suspend(host);
	if (!err) {
		pm_runtime_disable(&host->card->dev);
		pm_runtime_set_suspended(&host->card->dev);
	}

	return err;
}

/*
 * This function tries to determine if the same card is still present
 * and, if so, restore all state to it.
 */
static int _mmc_sd_resume(struct mmc_host *host)
{
	int err = 0;

	BUG_ON(!host);
	BUG_ON(!host->card);

	mmc_claim_host(host);

	if (!mmc_card_suspended(host->card))
		goto out;

	mmc_power_up(host, host->card->ocr);
	err = mmc_sd_init_card(host, host->card->ocr, host->card);
	mmc_card_clr_suspended(host->card);

out:
	mmc_release_host(host);
	return err;
}

/*
 * Callback for resume
 */
static int mmc_sd_resume(struct mmc_host *host)
{
	int err = 0;

	if (!(host->caps & MMC_CAP_RUNTIME_RESUME)) {
		err = _mmc_sd_resume(host);
		pm_runtime_set_active(&host->card->dev);
		pm_runtime_mark_last_busy(&host->card->dev);
	}
	pm_runtime_enable(&host->card->dev);

	return err;
}

/*
 * Callback for runtime_suspend.
 */
static int mmc_sd_runtime_suspend(struct mmc_host *host)
{
	int err;

	if (!(host->caps & MMC_CAP_AGGRESSIVE_PM))
		return 0;

	err = _mmc_sd_suspend(host);
	if (err)
		pr_err("%s: error %d doing aggressive suspend\n",
			mmc_hostname(host), err);

	return err;
}

/*
 * Callback for runtime_resume.
 */
static int mmc_sd_runtime_resume(struct mmc_host *host)
{
	int err;

	if (!(host->caps & (MMC_CAP_AGGRESSIVE_PM | MMC_CAP_RUNTIME_RESUME)))
		return 0;

	err = _mmc_sd_resume(host);
	if (err)
		pr_err("%s: error %d doing aggressive resume\n",
			mmc_hostname(host), err);

	return 0;
}

static int mmc_sd_reset(struct mmc_host *host)
{
	mmc_power_cycle(host, host->card->ocr);
	return mmc_sd_init_card(host, host->card->ocr, host->card);
}

static const struct mmc_bus_ops mmc_sd_ops = {
	.remove = mmc_sd_remove,
	.detect = mmc_sd_detect,
	.runtime_suspend = mmc_sd_runtime_suspend,
	.runtime_resume = mmc_sd_runtime_resume,
	.suspend = mmc_sd_suspend,
	.resume = mmc_sd_resume,
	.alive = mmc_sd_alive,
	.shutdown = mmc_sd_suspend,
	.reset = mmc_sd_reset,
};

/*
 * Starting point for SD card init.
 */
int mmc_attach_sd(struct mmc_host *host)
{
	int err;
	u32 ocr, rocr;

	BUG_ON(!host);
	WARN_ON(!host->claimed);

	err = mmc_send_app_op_cond(host, 0, &ocr);
	if (err)
		return err;

	mmc_attach_bus(host, &mmc_sd_ops);
	if (host->ocr_avail_sd)
		host->ocr_avail = host->ocr_avail_sd;

	/*
	 * We need to get OCR a different way for SPI.
	 */
	if (mmc_host_is_spi(host)) {
		mmc_go_idle(host);

		err = mmc_spi_read_ocr(host, 0, &ocr);
		if (err)
			goto err;
	}

	rocr = mmc_select_voltage(host, ocr);

	/*
	 * Can we support the voltage(s) of the card(s)?
	 */
	if (!rocr) {
		err = -EINVAL;
		goto err;
	}

	/*
	 * Detect and init the card.
	 */
	err = mmc_sd_init_card(host, rocr, NULL);
	if (err)
		goto err;

	mmc_release_host(host);
	err = mmc_add_card(host->card);
	mmc_claim_host(host);
	if (err)
		goto remove_card;

	return 0;

remove_card:
	mmc_release_host(host);
	mmc_remove_card(host->card);
	host->card = NULL;
	mmc_claim_host(host);
err:
	mmc_detach_bus(host);

	pr_err("%s: error %d whilst initialising SD card\n",
		mmc_hostname(host), err);

	return err;
}
