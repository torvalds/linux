// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 */

/*
 * i.MX27 specific CPU detection code
 */

#include <linux/io.h>
#include <linux/module.h>

#include "hardware.h"

static int mx27_cpu_rev = -1;
static int mx27_cpu_partnumber;

#define SYS_CHIP_ID             0x00    /* The offset of CHIP ID register */

static int mx27_read_cpu_rev(void)
{
	u32 val;
	/*
	 * now we have access to the IO registers. As we need
	 * the silicon revision very early we read it here to
	 * avoid any further hooks
	*/
	val = imx_readl(MX27_IO_ADDRESS(MX27_SYSCTRL_BASE_ADDR + SYS_CHIP_ID));

	mx27_cpu_partnumber = (int)((val >> 12) & 0xFFFF);

	switch (val >> 28) {
	case 0:
		return IMX_CHIP_REVISION_1_0;
	case 1:
		return IMX_CHIP_REVISION_2_0;
	case 2:
		return IMX_CHIP_REVISION_2_1;
	default:
		return IMX_CHIP_REVISION_UNKNOWN;
	}
}

/*
 * Returns:
 *	the silicon revision of the cpu
 *	-EINVAL - not a mx27
 */
int mx27_revision(void)
{
	if (mx27_cpu_rev == -1)
		mx27_cpu_rev = mx27_read_cpu_rev();

	if (mx27_cpu_partnumber != 0x8821)
		return -EINVAL;

	return mx27_cpu_rev;
}
EXPORT_SYMBOL(mx27_revision);
