/*
 * Actions Semi Owl Smart Power System (SPS) shared helpers
 *
 * Copyright 2012 Actions Semi Inc.
 * Author: Actions Semi, Inc.
 *
 * Copyright (c) 2017 Andreas Färber
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/delay.h>
#include <linux/io.h>

#define OWL_SPS_PG_CTL	0x0

int owl_sps_set_pg(void __iomem *base, u32 pwr_mask, u32 ack_mask, bool enable)
{
	u32 val;
	bool ack;
	int timeout;

	val = readl(base + OWL_SPS_PG_CTL);
	ack = val & ack_mask;
	if (ack == enable)
		return 0;

	if (enable)
		val |= pwr_mask;
	else
		val &= ~pwr_mask;

	writel(val, base + OWL_SPS_PG_CTL);

	for (timeout = 5000; timeout > 0; timeout -= 50) {
		val = readl(base + OWL_SPS_PG_CTL);
		if ((val & ack_mask) == (enable ? ack_mask : 0))
			break;
		udelay(50);
	}
	if (timeout <= 0)
		return -ETIMEDOUT;

	udelay(10);

	return 0;
}
EXPORT_SYMBOL_GPL(owl_sps_set_pg);
