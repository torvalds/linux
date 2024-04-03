/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Florian Fainelli <florian@openwrt.org>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/platform_data/bcm7038_wdt.h>
#include <bcm63xx_cpu.h>

static struct resource wdt_resources[] = {
	{
		.start		= -1, /* filled at runtime */
		.end		= -1, /* filled at runtime */
		.flags		= IORESOURCE_MEM,
	},
};

static struct bcm7038_wdt_platform_data bcm63xx_wdt_pdata = {
	.clk_name	= "periph",
};

static struct platform_device bcm63xx_wdt_device = {
	.name		= "bcm63xx-wdt",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(wdt_resources),
	.resource	= wdt_resources,
	.dev		= {
		.platform_data = &bcm63xx_wdt_pdata,
	},
};

static int __init bcm63xx_wdt_register(void)
{
	wdt_resources[0].start = bcm63xx_regset_address(RSET_WDT);
	wdt_resources[0].end = wdt_resources[0].start;
	wdt_resources[0].end += RSET_WDT_SIZE - 1;

	return platform_device_register(&bcm63xx_wdt_device);
}
arch_initcall(bcm63xx_wdt_register);
