/*
 *  linux/drivers/mmc/core/core.h
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *  Copyright 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _MMC_CORE_CORE_H
#define _MMC_CORE_CORE_H

#include <linux/delay.h>

#define MMC_CMD_RETRIES        3

struct mmc_bus_ops {
	void (*remove)(struct mmc_host *);
	void (*detect)(struct mmc_host *);
	int (*sysfs_add)(struct mmc_host *, struct mmc_card *card);
	void (*sysfs_remove)(struct mmc_host *, struct mmc_card *card);
	void (*suspend)(struct mmc_host *);
	void (*resume)(struct mmc_host *);
};

void mmc_attach_bus(struct mmc_host *host, const struct mmc_bus_ops *ops);
void mmc_detach_bus(struct mmc_host *host);

void __mmc_release_bus(struct mmc_host *host);

static inline void mmc_bus_get(struct mmc_host *host)
{
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	host->bus_refs++;
	spin_unlock_irqrestore(&host->lock, flags);
}

static inline void mmc_bus_put(struct mmc_host *host)
{
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	host->bus_refs--;
	if ((host->bus_refs == 0) && host->bus_ops)
		__mmc_release_bus(host);
	spin_unlock_irqrestore(&host->lock, flags);
}

void mmc_set_chip_select(struct mmc_host *host, int mode);
void mmc_set_clock(struct mmc_host *host, unsigned int hz);
void mmc_set_bus_mode(struct mmc_host *host, unsigned int mode);
void mmc_set_bus_width(struct mmc_host *host, unsigned int width);
u32 mmc_select_voltage(struct mmc_host *host, u32 ocr);
void mmc_set_timing(struct mmc_host *host, unsigned int timing);

static inline void mmc_delay(unsigned int ms)
{
	if (ms < 1000 / HZ) {
		cond_resched();
		mdelay(ms);
	} else {
		msleep(ms);
	}
}

void mmc_rescan(struct work_struct *work);
void mmc_start_host(struct mmc_host *host);
void mmc_stop_host(struct mmc_host *host);

#endif

