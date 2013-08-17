/*
 * MX25 CPU type detection
 *
 * Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 * Copyright (C) 2011 Freescale Semiconductor, Inc. All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/io.h>
#include <mach/hardware.h>
#include <mach/iim.h>

static int mx25_cpu_rev = -1;

static int mx25_read_cpu_rev(void)
{
	u32 rev;

	rev = __raw_readl(MX25_IO_ADDRESS(MX25_IIM_BASE_ADDR + MXC_IIMSREV));
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
