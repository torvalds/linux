// SPDX-License-Identifier: GPL-2.0+
/*
 * Actions Semi Owl Smart Power System (SPS) shared helpers
 *
 * Copyright 2012 Actions Semi Inc.
 * Author: Actions Semi, Inc.
 *
 * Copyright (c) 2017 Andreas Färber
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/soc/actions/owl-sps.h>

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
