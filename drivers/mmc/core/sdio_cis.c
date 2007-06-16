/*
 * linux/drivers/mmc/core/sdio_cis.c
 *
 * Author:	Nicolas Pitre
 * Created:	June 11, 2007
 * Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/kernel.h>

#include <linux/mmc/host.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>

#include "sdio_cis.h"
#include "sdio_ops.h"

static int cistpl_manfid(struct sdio_func *func,
			 const unsigned char *buf,
			 unsigned size)
{
	/* TPLMID_MANF */
	func->vendor = buf[0] | (buf[1] << 8);

	/* TPLMID_CARD */
	func->device = buf[2] | (buf[3] << 8);

	return 0;
}

struct cis_tpl {
	unsigned char code;
	unsigned char min_size;
	int (*parse)(struct sdio_func *, const unsigned char *buf, unsigned size);
};

static const struct cis_tpl cis_tpl_list[] = {
	{	0x15,	3,	/* cistpl_vers_1 */	},
	{	0x20,	4,	cistpl_manfid		},
	{	0x21,	2,	/* cistpl_funcid */	},
	{	0x22,	0,	/* cistpl_funce */	},
};

int sdio_read_cis(struct sdio_func *func)
{
	int ret;
	unsigned char *buf;
	unsigned i, ptr = 0;

	for (i = 0; i < 3; i++) {
		unsigned char x;
		ret = mmc_io_rw_direct(func->card, 0, 0,
				func->num * 0x100 + SDIO_FBR_CIS + i, 0, &x);
		if (ret)
			return ret;
		ptr |= x << (i * 8);
	}

	buf = kmalloc(256, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	do {
		unsigned char tpl_code, tpl_link;
		const struct cis_tpl *tpl;

		ret = mmc_io_rw_direct(func->card, 0, 0, ptr++, 0, &tpl_code);
		if (ret)
			break;

		/* 0xff means we're done */
		if (tpl_code == 0xff)
			break;

		ret = mmc_io_rw_direct(func->card, 0, 0, ptr++, 0, &tpl_link);
		if (ret)
			break;

		for (i = 0; i < ARRAY_SIZE(cis_tpl_list); i++)
			if (cis_tpl_list[i].code == tpl_code)
				break;
		if (i >= ARRAY_SIZE(cis_tpl_list)) {
			printk(KERN_WARNING
			       "%s: unknown CIS tuple 0x%02x of length %u\n",
			       sdio_func_id(func), tpl_code, tpl_link);
			ptr += tpl_link;
			continue;
		}
		tpl = cis_tpl_list + i;

		if (tpl_link < tpl->min_size) {
			printk(KERN_ERR
			       "%s: bad CIS tuple 0x%02x (length = %u, expected >= %u\n",
			       sdio_func_id(func), tpl_code, tpl_link, tpl->min_size);
			ret = -EINVAL;
			break;
		}

		for (i = 0; i < tpl_link; i++) {
			ret = mmc_io_rw_direct(func->card, 0, 0, ptr + i, 0, &buf[i]);
			if (ret)
				break;
		}
		if (ret)
			break;
		ptr += tpl_link;

		if (tpl->parse)
			ret = tpl->parse(func, buf, tpl_link);
	} while (!ret);

	kfree(buf);
	return ret;
}
