/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2011 Florian Fainelli <florian@openwrt.org>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <bcm63xx_cpu.h>

static struct resource rng_resources[] = {
	{
		.start		= -1, /* filled at runtime */
		.end		= -1, /* filled at runtime */
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device bcm63xx_rng_device = {
	.name		= "bcm63xx-rng",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rng_resources),
	.resource	= rng_resources,
};

int __init bcm63xx_rng_register(void)
{
	if (!BCMCPU_IS_6368())
		return -ENODEV;

	rng_resources[0].start = bcm63xx_regset_address(RSET_RNG);
	rng_resources[0].end = rng_resources[0].start;
	rng_resources[0].end += RSET_RNG_SIZE - 1;

	return platform_device_register(&bcm63xx_rng_device);
}
arch_initcall(bcm63xx_rng_register);
