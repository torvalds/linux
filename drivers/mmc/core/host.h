/*
 *  linux/drivers/mmc/core/host.h
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *  Copyright 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _MMC_CORE_HOST_H
#define _MMC_CORE_HOST_H
#include <linux/mmc/host.h>

int mmc_register_host_class(void);
void mmc_unregister_host_class(void);

#ifdef CONFIG_MMC_CLKGATE
void mmc_host_clk_ungate(struct mmc_host *host);
void mmc_host_clk_gate(struct mmc_host *host);
unsigned int mmc_host_clk_rate(struct mmc_host *host);

#else
static inline void mmc_host_clk_ungate(struct mmc_host *host)
{
}

static inline void mmc_host_clk_gate(struct mmc_host *host)
{
}

static inline unsigned int mmc_host_clk_rate(struct mmc_host *host)
{
	return host->ios.clock;
}
#endif

void mmc_host_deeper_disable(struct work_struct *work);

#endif

