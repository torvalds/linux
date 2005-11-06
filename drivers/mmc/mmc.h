/*
 *  linux/drivers/mmc/mmc.h
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _MMC_H
#define _MMC_H
/* core-internal functions */
void mmc_init_card(struct mmc_card *card, struct mmc_host *host);
int mmc_register_card(struct mmc_card *card);
void mmc_remove_card(struct mmc_card *card);

struct mmc_host *mmc_alloc_host_sysfs(int extra, struct device *dev);
int mmc_add_host_sysfs(struct mmc_host *host);
void mmc_remove_host_sysfs(struct mmc_host *host);
void mmc_free_host_sysfs(struct mmc_host *host);
#endif
