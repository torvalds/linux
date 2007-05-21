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

int mmc_send_io_op_cond(struct mmc_host *host, u32 ocr, u32 *rocr);

#endif

