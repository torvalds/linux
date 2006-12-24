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

void mmc_set_chip_select(struct mmc_host *host, int mode);

static inline void mmc_delay(unsigned int ms)
{
	if (ms < 1000 / HZ) {
		cond_resched();
		mdelay(ms);
	} else {
		msleep(ms);
	}
}

#endif

