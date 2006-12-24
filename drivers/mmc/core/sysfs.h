/*
 *  linux/drivers/mmc/core/sysfs.h
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *  Copyright 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _MMC_CORE_SYSFS_H
#define _MMC_CORE_SYSFS_H

void mmc_init_card(struct mmc_card *card, struct mmc_host *host);
int mmc_register_card(struct mmc_card *card);
void mmc_remove_card(struct mmc_card *card);

struct mmc_host *mmc_alloc_host_sysfs(int extra, struct device *dev);
int mmc_add_host_sysfs(struct mmc_host *host);
void mmc_remove_host_sysfs(struct mmc_host *host);
void mmc_free_host_sysfs(struct mmc_host *host);

int mmc_schedule_work(struct work_struct *work);
int mmc_schedule_delayed_work(struct delayed_work *work, unsigned long delay);
void mmc_flush_scheduled_work(void);

#endif
