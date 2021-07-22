// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MX35 CPU type detection
 *
 * Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 */
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/io.h>

#include "hardware.h"
#include "iim.h"

static int mx35_cpu_rev = -1;

static int mx35_read_cpu_rev(void)
{
	void __iomem *iim_base;
	struct device_node *np;
	u32 rev;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx35-iim");
	iim_base = of_iomap(np, 0);
	BUG_ON(!iim_base);

	rev = imx_readl(iim_base + MXC_IIMSREV);
	switch (rev) {
	case 0x00:
		return IMX_CHIP_REVISION_1_0;
	case 0x10:
		return IMX_CHIP_REVISION_2_0;
	case 0x11:
		return IMX_CHIP_REVISION_2_1;
	default:
		return IMX_CHIP_REVISION_UNKNOWN;
	}
}

int mx35_revision(void)
{
	if (mx35_cpu_rev == -1)
		mx35_cpu_rev = mx35_read_cpu_rev();

	return mx35_cpu_rev;
}
EXPORT_SYMBOL(mx35_revision);
