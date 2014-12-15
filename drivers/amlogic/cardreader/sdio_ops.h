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

#ifndef _MMC_SDIO_OPS_H
#define _MMC_SDIO_OPS_H

int sdio_send_io_op_cond(struct card_host *host, u32 ocr, u32 *rocr);
int sdio_io_rw_direct(struct memory_card *card, int write, unsigned fn,
	unsigned addr, u8 in, u8* out);
int sdio_io_rw_extended(struct memory_card *card, int write, unsigned fn,
	unsigned addr, int incr_addr, u8 *buf, unsigned blocks, unsigned blksz);
int sdio_reset(struct card_host *host);

#endif

