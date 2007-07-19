/*
 *  linux/drivers/mmc/core/sysfs.c
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *  Copyright 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  MMC sysfs/driver model support.
 */
#include <linux/device.h>

#include <linux/mmc/card.h>

#include "sysfs.h"

int mmc_add_attrs(struct mmc_card *card, struct device_attribute *attrs)
{
	int error = 0;
	int i;

	for (i = 0; attr_name(attrs[i]); i++) {
		error = device_create_file(&card->dev, &attrs[i]);
		if (error) {
			while (--i >= 0)
				device_remove_file(&card->dev, &attrs[i]);
			break;
		}
	}

	return error;
}

void mmc_remove_attrs(struct mmc_card *card, struct device_attribute *attrs)
{
	int i;

	for (i = 0; attr_name(attrs[i]); i++)
		device_remove_file(&card->dev, &attrs[i]);
}

