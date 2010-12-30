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

#define MMC_DEV_ATTR(name, fmt, args...)					\
static ssize_t mmc_##name##_show (struct device *dev, struct device_attribute *attr, char *buf)	\
{										\
	struct mmc_card *card = mmc_dev_to_card(dev);				\
	return sprintf(buf, fmt, args);						\
}										\
static DEVICE_ATTR(name, S_IRUGO, mmc_##name##_show, NULL)

struct mmc_card *mmc_alloc_card(struct mmc_host *host,
	struct device_type *type);
int mmc_add_card(struct mmc_card *card);
void mmc_remove_card(struct mmc_card *card);

int mmc_register_bus(void);
void mmc_unregister_bus(void);

#endif

