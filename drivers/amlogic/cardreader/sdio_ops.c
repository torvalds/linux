/*
 *  linux/drivers/mmc/sdio_ops.c
 *
 *  Copyright 2006-2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/err.h>
#include <linux/device.h>
#include <linux/pagemap.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/cardreader/sdio.h>
#include <linux/cardreader/card_block.h>

#include "sdio_ops.h"

static int sdio_io_rw_direct_host(struct card_host *host, int write, unsigned fn,
	unsigned addr, u8 in, u8 *out)
{
	struct card_blk_request brq;

	BUG_ON(!host);
	BUG_ON(fn > 7);

	/* sanity check */
	if (addr & ~0x1FFFF)
		return -EINVAL;

	if (write)
		brq.crq.cmd = WRITE;
	else
		brq.crq.cmd = READ;
	if (out)
		brq.crq.cmd |= READ_AFTER_WRITE;
	brq.crq.cmd |= SDIO_OPS_REG;

	brq.crq.buf = &in;
	brq.crq.back_buf = out;
	brq.card_data.lba = addr;
	brq.card_data.flags = fn;
	brq.card_data.blk_size = 1;
	brq.card_data.blk_nums = 1;						// for read reg just one byte

	card_wait_for_req(host, &brq);

	return brq.card_data.error;
}

int sdio_io_rw_direct(struct memory_card *card, int write, unsigned fn,
	unsigned addr, u8 in, u8 *out)
{
	BUG_ON(!card);
	card->host->card_busy = card;
	return sdio_io_rw_direct_host(card->host, write, fn, addr, in, out);
}

EXPORT_SYMBOL(sdio_io_rw_direct);

int sdio_io_rw_extended(struct memory_card *card, int write, unsigned fn,
	unsigned addr, int incr_addr, u8 *buf, unsigned blocks, unsigned blksz)
{
	struct card_blk_request brq;

	BUG_ON(!card);
	BUG_ON(fn > 7);
	BUG_ON(blocks == 1 && blksz > 512);
	WARN_ON(blocks == 0);
	WARN_ON(blksz == 0);

	/* sanity check */
	if (addr & ~0x1FFFF)
		return -EINVAL;

	card->host->card_busy = card;
	if (write)
		brq.crq.cmd = WRITE;
	else
		brq.crq.cmd = READ;
	if (incr_addr)
		brq.crq.cmd |= SDIO_FIFO_ADDR;

	brq.crq.buf = buf;
	brq.card_data.lba = addr;
	brq.card_data.blk_size = blksz;
	brq.card_data.blk_nums = blocks;
	brq.card_data.flags = fn;

	card_wait_for_req(card->host, &brq);

	return brq.card_data.error;
}

EXPORT_SYMBOL(sdio_io_rw_extended);

int sdio_reset(struct card_host *host)
{
	int ret;
	u8 abort;

	/* SDIO Simplified Specification V2.0, 4.4 Reset for SDIO */
	host->card_busy = host->card;
	ret = sdio_io_rw_direct_host(host, 0, 0, SDIO_CCCR_ABORT, 0, &abort);
	if (ret)
		abort = 0x08;
	else
		abort |= 0x08;

	ret = sdio_io_rw_direct_host(host, 1, 0, SDIO_CCCR_ABORT, abort, NULL);
	return ret;
}

