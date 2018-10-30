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

void mmc_retune_enable(struct mmc_host *host);
void mmc_retune_disable(struct mmc_host *host);
void mmc_retune_hold(struct mmc_host *host);
void mmc_retune_release(struct mmc_host *host);
int mmc_retune(struct mmc_host *host);
void mmc_retune_pause(struct mmc_host *host);
void mmc_retune_unpause(struct mmc_host *host);

static inline void mmc_retune_hold_now(struct mmc_host *host)
{
	host->retune_now = 0;
	host->hold_retune += 1;
}

static inline void mmc_retune_recheck(struct mmc_host *host)
{
	if (host->hold_retune <= 1)
		host->retune_now = 1;
}

static inline int mmc_host_cmd23(struct mmc_host *host)
{
	return host->caps & MMC_CAP_CMD23;
}

static inline bool mmc_host_done_complete(struct mmc_host *host)
{
	return host->caps & MMC_CAP_DONE_COMPLETE;
}

static inline int mmc_boot_partition_access(struct mmc_host *host)
{
	return !(host->caps2 & MMC_CAP2_BOOTPART_NOACC);
}

static inline int mmc_host_uhs(struct mmc_host *host)
{
	return host->caps &
		(MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 |
		 MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_SDR104 |
		 MMC_CAP_UHS_DDR50) &&
	       host->caps & MMC_CAP_4_BIT_DATA;
}

static inline bool mmc_card_hs200(struct mmc_card *card)
{
	return card->host->ios.timing == MMC_TIMING_MMC_HS200;
}

static inline bool mmc_card_ddr52(struct mmc_card *card)
{
	return card->host->ios.timing == MMC_TIMING_MMC_DDR52;
}

static inline bool mmc_card_hs400(struct mmc_card *card)
{
	return card->host->ios.timing == MMC_TIMING_MMC_HS400;
}

static inline bool mmc_card_hs400es(struct mmc_card *card)
{
	return card->host->ios.enhanced_strobe;
}

#endif

