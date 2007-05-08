/*
 *  linux/drivers/mmc/sd_ops.h
 *
 *  Copyright 2006-2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef _MMC_SD_OPS_H
#define _MMC_SD_OPS_H

int mmc_app_cmd(struct mmc_host *host, struct mmc_card *card);
int mmc_app_set_bus_width(struct mmc_card *card, int width);
int mmc_send_app_op_cond(struct mmc_host *host, u32 ocr, u32 *rocr);
int mmc_send_if_cond(struct mmc_host *host, u32 ocr);
int mmc_send_relative_addr(struct mmc_host *host, unsigned int *rca);
int mmc_app_send_scr(struct mmc_card *card, u32 *scr);
int mmc_sd_switch(struct mmc_card *card, int mode, int group,
	u8 value, u8 *resp);

#endif

