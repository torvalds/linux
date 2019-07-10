// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/arm/mach-lpc32xx/common.c
 *
 * Author: Kevin Wells <kevin.wells@nxp.com>
 *
 * Copyright (C) 2010 NXP Semiconductors
 */

#include <linux/init.h>

#include <asm/mach/map.h>
#include <asm/system_info.h>

#include <mach/hardware.h>
#include <mach/platform.h>
#include "common.h"

/*
 * Returns the unique ID for the device
 */
void lpc32xx_get_uid(u32 devid[4])
{
	int i;

	for (i = 0; i < 4; i++)
		devid[i] = __raw_readl(LPC32XX_CLKPWR_DEVID(i << 2));
}

/*
 * Detects and returns IRAM size for the device variation
 */
#define LPC32XX_IRAM_BANK_SIZE SZ_128K
static u32 iram_size;
u32 lpc32xx_return_iram_size(void)
{
	if (iram_size == 0) {
		u32 savedval1, savedval2;
		void __iomem *iramptr1, *iramptr2;

		iramptr1 = io_p2v(LPC32XX_IRAM_BASE);
		iramptr2 = io_p2v(LPC32XX_IRAM_BASE + LPC32XX_IRAM_BANK_SIZE);
		savedval1 = __raw_readl(iramptr1);
		savedval2 = __raw_readl(iramptr2);

		if (savedval1 == savedval2) {
			__raw_writel(savedval2 + 1, iramptr2);
			if (__raw_readl(iramptr1) == savedval2 + 1)
				iram_size = LPC32XX_IRAM_BANK_SIZE;
			else
				iram_size = LPC32XX_IRAM_BANK_SIZE * 2;
			__raw_writel(savedval2, iramptr2);
		} else
			iram_size = LPC32XX_IRAM_BANK_SIZE * 2;
	}

	return iram_size;
}
EXPORT_SYMBOL_GPL(lpc32xx_return_iram_size);

static struct map_desc lpc32xx_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long)IO_ADDRESS(LPC32XX_AHB0_START),
		.pfn		= __phys_to_pfn(LPC32XX_AHB0_START),
		.length		= LPC32XX_AHB0_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= (unsigned long)IO_ADDRESS(LPC32XX_AHB1_START),
		.pfn		= __phys_to_pfn(LPC32XX_AHB1_START),
		.length		= LPC32XX_AHB1_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= (unsigned long)IO_ADDRESS(LPC32XX_FABAPB_START),
		.pfn		= __phys_to_pfn(LPC32XX_FABAPB_START),
		.length		= LPC32XX_FABAPB_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= (unsigned long)IO_ADDRESS(LPC32XX_IRAM_BASE),
		.pfn		= __phys_to_pfn(LPC32XX_IRAM_BASE),
		.length		= (LPC32XX_IRAM_BANK_SIZE * 2),
		.type		= MT_DEVICE
	},
};

void __init lpc32xx_map_io(void)
{
	iotable_init(lpc32xx_io_desc, ARRAY_SIZE(lpc32xx_io_desc));
}

static int __init lpc32xx_check_uid(void)
{
	u32 uid[4];

	lpc32xx_get_uid(uid);

	printk(KERN_INFO "LPC32XX unique ID: %08x%08x%08x%08x\n",
		uid[3], uid[2], uid[1], uid[0]);

	if (!system_serial_low && !system_serial_high) {
		system_serial_low = uid[0];
		system_serial_high = uid[1];
	}

	return 1;
}
arch_initcall(lpc32xx_check_uid);
