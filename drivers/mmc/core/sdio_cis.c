/*
 * linux/drivers/mmc/core/sdio_cis.c
 *
 * Author:	Nicolas Pitre
 * Created:	June 11, 2007
 * Copyright:	MontaVista Software Inc.
 *
 * Copyright 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/slab.h>

#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>

#include "sdio_cis.h"
#include "sdio_ops.h"

static int cistpl_vers_1(struct mmc_card *card, struct sdio_func *func,
			 const unsigned char *buf, unsigned size)
{
	unsigned i, nr_strings;
	char **buffer, *string;

	/* Find all null-terminated (including zero length) strings in
	   the TPLLV1_INFO field. Trailing garbage is ignored. */
	buf += 2;
	size -= 2;

	nr_strings = 0;
	for (i = 0; i < size; i++) {
		if (buf[i] == 0xff)
			break;
		if (buf[i] == 0)
			nr_strings++;
	}
	if (nr_strings == 0)
		return 0;

	size = i;

	buffer = kzalloc(sizeof(char*) * nr_strings + size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	string = (char*)(buffer + nr_strings);

	for (i = 0; i < nr_strings; i++) {
		buffer[i] = string;
		strcpy(string, buf);
		string += strlen(string) + 1;
		buf += strlen(buf) + 1;
	}

	if (func) {
		func->num_info = nr_strings;
		func->info = (const char**)buffer;
	} else {
		card->num_info = nr_strings;
		card->info = (const char**)buffer;
	}

	return 0;
}

static int cistpl_manfid(struct mmc_card *card, struct sdio_func *func,
			 const unsigned char *buf, unsigned size)
{
	unsigned int vendor, device;

	/* TPLMID_MANF */
	vendor = buf[0] | (buf[1] << 8);

	/* TPLMID_CARD */
	device = buf[2] | (buf[3] << 8);

	if (func) {
		func->vendor = vendor;
		func->device = device;
	} else {
		card->cis.vendor = vendor;
		card->cis.device = device;
	}

	return 0;
}

static const unsigned char speed_val[16] =
	{ 0, 10, 12, 13, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 70, 80 };
static const unsigned int speed_unit[8] =
	{ 10000, 100000, 1000000, 10000000, 0, 0, 0, 0 };


typedef int (tpl_parse_t)(struct mmc_card *, struct sdio_func *,
			   const unsigned char *, unsigned);

struct cis_tpl {
	unsigned char code;
	unsigned char min_size;
	tpl_parse_t *parse;
};

static int cis_tpl_parse(struct mmc_card *card, struct sdio_func *func,
			 const char *tpl_descr,
			 const struct cis_tpl *tpl, int tpl_count,
			 unsigned char code,
			 const unsigned char *buf, unsigned size)
{
	int i, ret;

	/* look for a matching code in the table */
	for (i = 0; i < tpl_count; i++, tpl++) {
		if (tpl->code == code)
			break;
	}
	if (i < tpl_count) {
		if (size >= tpl->min_size) {
			if (tpl->parse)
				ret = tpl->parse(card, func, buf, size);
			else
				ret = -EILSEQ;	/* known tuple, not parsed */
		} else {
			/* invalid tuple */
			ret = -EINVAL;
		}
		if (ret && ret != -EILSEQ && ret != -ENOENT) {
			pr_err("%s: bad %s tuple 0x%02x (%u bytes)\n",
			       mmc_hostname(card->host), tpl_descr, code, size);
		}
	} else {
		/* unknown tuple */
		ret = -ENOENT;
	}

	return ret;
}

static int cistpl_funce_common(struct mmc_card *card, struct sdio_func *func,
			       const unsigned char *buf, unsigned size)
{
	/* Only valid for the common CIS (function 0) */
	if (func)
		return -EINVAL;

	/* TPLFE_FN0_BLK_SIZE */
	card->cis.blksize = buf[1] | (buf[2] << 8);

	/* TPLFE_MAX_TRAN_SPEED */
	card->cis.max_dtr = speed_val[(buf[3] >> 3) & 15] *
			    speed_unit[buf[3] & 7];

	return 0;
}

static int cistpl_funce_func(struct mmc_card *card, struct sdio_func *func,
			     const unsigned char *buf, unsigned size)
{
	unsigned vsn;
	unsigned min_size;

	/* Only valid for the individual function's CIS (1-7) */
	if (!func)
		return -EINVAL;

	/*
	 * This tuple has a different length depending on the SDIO spec
	 * version.
	 */
	vsn = func->card->cccr.sdio_vsn;
	min_size = (vsn == SDIO_SDIO_REV_1_00) ? 28 : 42;

	if (size == 28 && vsn == SDIO_SDIO_REV_1_10) {
		pr_warn("%s: card has broken SDIO 1.1 CIS, forcing SDIO 1.0\n",
			mmc_hostname(card->host));
		vsn = SDIO_SDIO_REV_1_00;
	} else if (size < min_size) {
		return -EINVAL;
	}

	/* TPLFE_MAX_BLK_SIZE */
	func->max_blksize = buf[12] | (buf[13] << 8);

	/* TPLFE_ENABLE_TIMEOUT_VAL, present in ver 1.1 and above */
	if (vsn > SDIO_SDIO_REV_1_00)
		func->enable_timeout = (buf[28] | (buf[29] << 8)) * 10;
	else
		func->enable_timeout = jiffies_to_msecs(HZ);

	return 0;
}

/*
 * Known TPLFE_TYPEs table for CISTPL_FUNCE tuples.
 *
 * Note that, unlike PCMCIA, CISTPL_FUNCE tuples are not parsed depending
 * on the TPLFID_FUNCTION value of the previous CISTPL_FUNCID as on SDIO
 * TPLFID_FUNCTION is always hardcoded to 0x0C.
 */
static const struct cis_tpl cis_tpl_funce_list[] = {
	{	0x00,	4,	cistpl_funce_common		},
	{	0x01,	0,	cistpl_funce_func		},
	{	0x04,	1+1+6,	/* CISTPL_FUNCE_LAN_NODE_ID */	},
};

static int cistpl_funce(struct mmc_card *card, struct sdio_func *func,
			const unsigned char *buf, unsigned size)
{
	if (size < 1)
		return -EINVAL;

	return cis_tpl_parse(card, func, "CISTPL_FUNCE",
			     cis_tpl_funce_list,
			     ARRAY_SIZE(cis_tpl_funce_list),
			     buf[0], buf, size);
}

/* Known TPL_CODEs table for CIS tuples */
static const struct cis_tpl cis_tpl_list[] = {
	{	0x15,	3,	cistpl_vers_1		},
	{	0x20,	4,	cistpl_manfid		},
	{	0x21,	2,	/* cistpl_funcid */	},
	{	0x22,	0,	cistpl_funce		},
	{	0x91,	2,	/* cistpl_sdio_std */	},
};

static int sdio_read_cis(struct mmc_card *card, struct sdio_func *func)
{
	int ret;
	struct sdio_func_tuple *this, **prev;
	unsigned i, ptr = 0;

	/*
	 * Note that this works for the common CIS (function number 0) as
	 * well as a function's CIS * since SDIO_CCCR_CIS and SDIO_FBR_CIS
	 * have the same offset.
	 */
	for (i = 0; i < 3; i++) {
		unsigned char x, fn;

		if (func)
			fn = func->num;
		else
			fn = 0;

		ret = mmc_io_rw_direct(card, 0, 0,
			SDIO_FBR_BASE(fn) + SDIO_FBR_CIS + i, 0, &x);
		if (ret)
			return ret;
		ptr |= x << (i * 8);
	}

	if (func)
		prev = &func->tuples;
	else
		prev = &card->tuples;

	if (*prev)
		return -EINVAL;

	do {
		unsigned char tpl_code, tpl_link;

		ret = mmc_io_rw_direct(card, 0, 0, ptr++, 0, &tpl_code);
		if (ret)
			break;

		/* 0xff means we're done */
		if (tpl_code == 0xff)
			break;

		/* null entries have no link field or data */
		if (tpl_code == 0x00)
			continue;

		ret = mmc_io_rw_direct(card, 0, 0, ptr++, 0, &tpl_link);
		if (ret)
			break;

		/* a size of 0xff also means we're done */
		if (tpl_link == 0xff)
			break;

		this = kmalloc(sizeof(*this) + tpl_link, GFP_KERNEL);
		if (!this)
			return -ENOMEM;

		for (i = 0; i < tpl_link; i++) {
			ret = mmc_io_rw_direct(card, 0, 0,
					       ptr + i, 0, &this->data[i]);
			if (ret)
				break;
		}
		if (ret) {
			kfree(this);
			break;
		}

		/* Try to parse the CIS tuple */
		ret = cis_tpl_parse(card, func, "CIS",
				    cis_tpl_list, ARRAY_SIZE(cis_tpl_list),
				    tpl_code, this->data, tpl_link);
		if (ret == -EILSEQ || ret == -ENOENT) {
			/*
			 * The tuple is unknown or known but not parsed.
			 * Queue the tuple for the function driver.
			 */
			this->next = NULL;
			this->code = tpl_code;
			this->size = tpl_link;
			*prev = this;
			prev = &this->next;

			if (ret == -ENOENT) {
				/* warn about unknown tuples */
				pr_warn_ratelimited("%s: queuing unknown"
				       " CIS tuple 0x%02x (%u bytes)\n",
				       mmc_hostname(card->host),
				       tpl_code, tpl_link);
			}

			/* keep on analyzing tuples */
			ret = 0;
		} else {
			/*
			 * We don't need the tuple anymore if it was
			 * successfully parsed by the SDIO core or if it is
			 * not going to be queued for a driver.
			 */
			kfree(this);
		}

		ptr += tpl_link;
	} while (!ret);

	/*
	 * Link in all unknown tuples found in the common CIS so that
	 * drivers don't have to go digging in two places.
	 */
	if (func)
		*prev = card->tuples;

	return ret;
}

int sdio_read_common_cis(struct mmc_card *card)
{
	return sdio_read_cis(card, NULL);
}

void sdio_free_common_cis(struct mmc_card *card)
{
	struct sdio_func_tuple *tuple, *victim;

	tuple = card->tuples;

	while (tuple) {
		victim = tuple;
		tuple = tuple->next;
		kfree(victim);
	}

	card->tuples = NULL;
}

int sdio_read_func_cis(struct sdio_func *func)
{
	int ret;

	ret = sdio_read_cis(func->card, func);
	if (ret)
		return ret;

	/*
	 * Since we've linked to tuples in the card structure,
	 * we must make sure we have a reference to it.
	 */
	get_device(&func->card->dev);

	/*
	 * Vendor/device id is optional for function CIS, so
	 * copy it from the card structure as needed.
	 */
	if (func->vendor == 0) {
		func->vendor = func->card->cis.vendor;
		func->device = func->card->cis.device;
	}

	return 0;
}

void sdio_free_func_cis(struct sdio_func *func)
{
	struct sdio_func_tuple *tuple, *victim;

	tuple = func->tuples;

	while (tuple && tuple != func->card->tuples) {
		victim = tuple;
		tuple = tuple->next;
		kfree(victim);
	}

	func->tuples = NULL;

	/*
	 * We have now removed the link to the tuples in the
	 * card structure, so remove the reference.
	 */
	put_device(&func->card->dev);
}

