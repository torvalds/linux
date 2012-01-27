/* arch/arm/mach-s3c2440/s3c2440-pll-12000000.c
 *
 * Copyright (c) 2006-2007 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *	Vincent Sanders <vince@arm.linux.org.uk>
 *
 * S3C2440/S3C2442 CPU PLL tables (12MHz Crystal)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <plat/cpu.h>
#include <plat/cpu-freq-core.h>

static struct cpufreq_frequency_table s3c2440_plls_12[] __initdata = {
	{ .frequency = 75000000,	.index = PLLVAL(0x75, 3, 3),  }, 	/* FVco 600.000000 */
	{ .frequency = 80000000,	.index = PLLVAL(0x98, 4, 3),  }, 	/* FVco 640.000000 */
	{ .frequency = 90000000,	.index = PLLVAL(0x70, 2, 3),  }, 	/* FVco 720.000000 */
	{ .frequency = 100000000,	.index = PLLVAL(0x5c, 1, 3),  }, 	/* FVco 800.000000 */
	{ .frequency = 110000000,	.index = PLLVAL(0x66, 1, 3),  }, 	/* FVco 880.000000 */
	{ .frequency = 120000000,	.index = PLLVAL(0x70, 1, 3),  }, 	/* FVco 960.000000 */
	{ .frequency = 150000000,	.index = PLLVAL(0x75, 3, 2),  }, 	/* FVco 600.000000 */
	{ .frequency = 160000000,	.index = PLLVAL(0x98, 4, 2),  }, 	/* FVco 640.000000 */
	{ .frequency = 170000000,	.index = PLLVAL(0x4d, 1, 2),  }, 	/* FVco 680.000000 */
	{ .frequency = 180000000,	.index = PLLVAL(0x70, 2, 2),  }, 	/* FVco 720.000000 */
	{ .frequency = 190000000,	.index = PLLVAL(0x57, 1, 2),  }, 	/* FVco 760.000000 */
	{ .frequency = 200000000,	.index = PLLVAL(0x5c, 1, 2),  }, 	/* FVco 800.000000 */
	{ .frequency = 210000000,	.index = PLLVAL(0x84, 2, 2),  }, 	/* FVco 840.000000 */
	{ .frequency = 220000000,	.index = PLLVAL(0x66, 1, 2),  }, 	/* FVco 880.000000 */
	{ .frequency = 230000000,	.index = PLLVAL(0x6b, 1, 2),  }, 	/* FVco 920.000000 */
	{ .frequency = 240000000,	.index = PLLVAL(0x70, 1, 2),  }, 	/* FVco 960.000000 */
	{ .frequency = 300000000,	.index = PLLVAL(0x75, 3, 1),  }, 	/* FVco 600.000000 */
	{ .frequency = 310000000,	.index = PLLVAL(0x93, 4, 1),  }, 	/* FVco 620.000000 */
	{ .frequency = 320000000,	.index = PLLVAL(0x98, 4, 1),  }, 	/* FVco 640.000000 */
	{ .frequency = 330000000,	.index = PLLVAL(0x66, 2, 1),  }, 	/* FVco 660.000000 */
	{ .frequency = 340000000,	.index = PLLVAL(0x4d, 1, 1),  }, 	/* FVco 680.000000 */
	{ .frequency = 350000000,	.index = PLLVAL(0xa7, 4, 1),  }, 	/* FVco 700.000000 */
	{ .frequency = 360000000,	.index = PLLVAL(0x70, 2, 1),  }, 	/* FVco 720.000000 */
	{ .frequency = 370000000,	.index = PLLVAL(0xb1, 4, 1),  }, 	/* FVco 740.000000 */
	{ .frequency = 380000000,	.index = PLLVAL(0x57, 1, 1),  }, 	/* FVco 760.000000 */
	{ .frequency = 390000000,	.index = PLLVAL(0x7a, 2, 1),  }, 	/* FVco 780.000000 */
	{ .frequency = 400000000,	.index = PLLVAL(0x5c, 1, 1),  }, 	/* FVco 800.000000 */
};

static int s3c2440_plls12_add(struct device *dev, struct subsys_interface *sif)
{
	struct clk *xtal_clk;
	unsigned long xtal;

	xtal_clk = clk_get(NULL, "xtal");
	if (IS_ERR(xtal_clk))
		return PTR_ERR(xtal_clk);

	xtal = clk_get_rate(xtal_clk);
	clk_put(xtal_clk);

	if (xtal == 12000000) {
		printk(KERN_INFO "Using PLL table for 12MHz crystal\n");
		return s3c_plltab_register(s3c2440_plls_12,
					   ARRAY_SIZE(s3c2440_plls_12));
	}

	return 0;
}

static struct subsys_interface s3c2440_plls12_interface = {
	.name		= "s3c2440_plls12",
	.subsys		= &s3c2440_subsys,
	.add_dev	= s3c2440_plls12_add,
};

static int __init s3c2440_pll_12mhz(void)
{
	return subsys_interface_register(&s3c2440_plls12_interface);

}

arch_initcall(s3c2440_pll_12mhz);

static struct subsys_interface s3c2442_plls12_interface = {
	.name		= "s3c2442_plls12",
	.subsys		= &s3c2442_subsys,
	.add_dev	= s3c2440_plls12_add,
};

static int __init s3c2442_pll_12mhz(void)
{
	return subsys_interface_register(&s3c2442_plls12_interface);

}

arch_initcall(s3c2442_pll_12mhz);
