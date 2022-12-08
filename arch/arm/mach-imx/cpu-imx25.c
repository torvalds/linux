// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MX25 CPU type detection
 *
 * Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 * Copyright (C) 2011 Freescale Semiconductor, Inc. All Rights Reserved
 */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "iim.h"
#include "hardware.h"

static int mx25_cpu_rev = -1;

static int mx25_read_cpu_rev(void)
{
	u32 rev;
	void __iomem *iim_base;
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx25-iim");
	iim_base = of_iomap(np, 0);
	of_node_put(np);
	BUG_ON(!iim_base);
	rev = readl(iim_base + MXC_IIMSREV);
	iounmap(iim_base);

	switch (rev) {
	case 0x00:
		return IMX_CHIP_REVISION_1_0;
	case 0x01:
		return IMX_CHIP_REVISION_1_1;
	default:
		return IMX_CHIP_REVISION_UNKNOWN;
	}
}

int mx25_revision(void)
{
	if (mx25_cpu_rev == -1)
		mx25_cpu_rev = mx25_read_cpu_rev();

	return mx25_cpu_rev;
}
EXPORT_SYMBOL(mx25_revision);
