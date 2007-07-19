/*
 *  linux/drivers/mmc/core/bus.h
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *  Copyright 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _MMC_CORE_BUS_H
#define _MMC_CORE_BUS_H

struct mmc_card *mmc_alloc_card(struct mmc_host *host);
int mmc_add_card(struct mmc_card *card);
void mmc_remove_card(struct mmc_card *card);

int mmc_register_bus(void);
void mmc_unregister_bus(void);

#endif

