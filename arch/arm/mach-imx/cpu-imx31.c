// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MX31 CPU type detection
 *
 * Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 */

#include <linux/module.h>
#include <linux/io.h>

#include "common.h"
#include "hardware.h"
#include "iim.h"

static int mx31_cpu_rev = -1;

static struct {
	u8 srev;
	const char *name;
	unsigned int rev;
} mx31_cpu_type[] = {
	{ .srev = 0x00, .name = "i.MX31(L)", .rev = IMX_CHIP_REVISION_1_0 },
	{ .srev = 0x10, .name = "i.MX31",    .rev = IMX_CHIP_REVISION_1_1 },
	{ .srev = 0x11, .name = "i.MX31L",   .rev = IMX_CHIP_REVISION_1_1 },
	{ .srev = 0x12, .name = "i.MX31",    .rev = IMX_CHIP_REVISION_1_1 },
	{ .srev = 0x13, .name = "i.MX31L",   .rev = IMX_CHIP_REVISION_1_1 },
	{ .srev = 0x14, .name = "i.MX31",    .rev = IMX_CHIP_REVISION_1_2 },
	{ .srev = 0x15, .name = "i.MX31L",   .rev = IMX_CHIP_REVISION_1_2 },
	{ .srev = 0x28, .name = "i.MX31",    .rev = IMX_CHIP_REVISION_2_0 },
	{ .srev = 0x29, .name = "i.MX31L",   .rev = IMX_CHIP_REVISION_2_0 },
};

static int mx31_read_cpu_rev(void)
{
	u32 i, srev;

	/* read SREV register from IIM module */
	srev = imx_readl(MX31_IO_ADDRESS(MX31_IIM_BASE_ADDR + MXC_IIMSREV));
	srev &= 0xff;

	for (i = 0; i < ARRAY_SIZE(mx31_cpu_type); i++)
		if (srev == mx31_cpu_type[i].srev) {
			imx_print_silicon_rev(mx31_cpu_type[i].name,
						mx31_cpu_type[i].rev);
			return mx31_cpu_type[i].rev;
		}

	imx_print_silicon_rev("i.MX31", IMX_CHIP_REVISION_UNKNOWN);
	return IMX_CHIP_REVISION_UNKNOWN;
}

int mx31_revision(void)
{
	if (mx31_cpu_rev == -1)
		mx31_cpu_rev = mx31_read_cpu_rev();

	return mx31_cpu_rev;
}
EXPORT_SYMBOL(mx31_revision);
