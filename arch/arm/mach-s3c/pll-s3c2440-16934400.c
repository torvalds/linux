// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2006-2008 Simtec Electronics
//	http://armlinux.simtec.co.uk/
//	Ben Dooks <ben@simtec.co.uk>
//	Vincent Sanders <vince@arm.linux.org.uk>
//
// S3C2440/S3C2442 CPU PLL tables (16.93444MHz Crystal)

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <linux/soc/samsung/s3c-cpufreq-core.h>
#include <linux/soc/samsung/s3c-pm.h>

/* This array should be sorted in ascending order of the frequencies */
static struct cpufreq_frequency_table s3c2440_plls_169344[] = {
	{ .frequency = 78019200,	.driver_data = PLLVAL(121, 5, 3), 	}, 	/* FVco 624.153600 */
	{ .frequency = 84067200,	.driver_data = PLLVAL(131, 5, 3), 	}, 	/* FVco 672.537600 */
	{ .frequency = 90115200,	.driver_data = PLLVAL(141, 5, 3), 	}, 	/* FVco 720.921600 */
	{ .frequency = 96163200,	.driver_data = PLLVAL(151, 5, 3), 	}, 	/* FVco 769.305600 */
	{ .frequency = 102135600,	.driver_data = PLLVAL(185, 6, 3), 	}, 	/* FVco 817.084800 */
	{ .frequency = 108259200,	.driver_data = PLLVAL(171, 5, 3), 	}, 	/* FVco 866.073600 */
	{ .frequency = 114307200,	.driver_data = PLLVAL(127, 3, 3), 	}, 	/* FVco 914.457600 */
	{ .frequency = 120234240,	.driver_data = PLLVAL(134, 3, 3), 	}, 	/* FVco 961.873920 */
	{ .frequency = 126161280,	.driver_data = PLLVAL(141, 3, 3), 	}, 	/* FVco 1009.290240 */
	{ .frequency = 132088320,	.driver_data = PLLVAL(148, 3, 3), 	}, 	/* FVco 1056.706560 */
	{ .frequency = 138015360,	.driver_data = PLLVAL(155, 3, 3), 	}, 	/* FVco 1104.122880 */
	{ .frequency = 144789120,	.driver_data = PLLVAL(163, 3, 3), 	}, 	/* FVco 1158.312960 */
	{ .frequency = 150100363,	.driver_data = PLLVAL(187, 9, 2), 	}, 	/* FVco 600.401454 */
	{ .frequency = 156038400,	.driver_data = PLLVAL(121, 5, 2), 	}, 	/* FVco 624.153600 */
	{ .frequency = 162086400,	.driver_data = PLLVAL(126, 5, 2), 	}, 	/* FVco 648.345600 */
	{ .frequency = 168134400,	.driver_data = PLLVAL(131, 5, 2), 	}, 	/* FVco 672.537600 */
	{ .frequency = 174048000,	.driver_data = PLLVAL(177, 7, 2), 	}, 	/* FVco 696.192000 */
	{ .frequency = 180230400,	.driver_data = PLLVAL(141, 5, 2), 	}, 	/* FVco 720.921600 */
	{ .frequency = 186278400,	.driver_data = PLLVAL(124, 4, 2), 	}, 	/* FVco 745.113600 */
	{ .frequency = 192326400,	.driver_data = PLLVAL(151, 5, 2), 	}, 	/* FVco 769.305600 */
	{ .frequency = 198132480,	.driver_data = PLLVAL(109, 3, 2), 	}, 	/* FVco 792.529920 */
	{ .frequency = 204271200,	.driver_data = PLLVAL(185, 6, 2), 	}, 	/* FVco 817.084800 */
	{ .frequency = 210268800,	.driver_data = PLLVAL(141, 4, 2), 	}, 	/* FVco 841.075200 */
	{ .frequency = 216518400,	.driver_data = PLLVAL(171, 5, 2), 	}, 	/* FVco 866.073600 */
	{ .frequency = 222264000,	.driver_data = PLLVAL(97, 2, 2), 	}, 	/* FVco 889.056000 */
	{ .frequency = 228614400,	.driver_data = PLLVAL(127, 3, 2), 	}, 	/* FVco 914.457600 */
	{ .frequency = 234259200,	.driver_data = PLLVAL(158, 4, 2), 	}, 	/* FVco 937.036800 */
	{ .frequency = 240468480,	.driver_data = PLLVAL(134, 3, 2), 	}, 	/* FVco 961.873920 */
	{ .frequency = 246960000,	.driver_data = PLLVAL(167, 4, 2), 	}, 	/* FVco 987.840000 */
	{ .frequency = 252322560,	.driver_data = PLLVAL(141, 3, 2), 	}, 	/* FVco 1009.290240 */
	{ .frequency = 258249600,	.driver_data = PLLVAL(114, 2, 2), 	}, 	/* FVco 1032.998400 */
	{ .frequency = 264176640,	.driver_data = PLLVAL(148, 3, 2), 	}, 	/* FVco 1056.706560 */
	{ .frequency = 270950400,	.driver_data = PLLVAL(120, 2, 2), 	}, 	/* FVco 1083.801600 */
	{ .frequency = 276030720,	.driver_data = PLLVAL(155, 3, 2), 	}, 	/* FVco 1104.122880 */
	{ .frequency = 282240000,	.driver_data = PLLVAL(92, 1, 2), 	}, 	/* FVco 1128.960000 */
	{ .frequency = 289578240,	.driver_data = PLLVAL(163, 3, 2), 	}, 	/* FVco 1158.312960 */
	{ .frequency = 294235200,	.driver_data = PLLVAL(131, 2, 2), 	}, 	/* FVco 1176.940800 */
	{ .frequency = 300200727,	.driver_data = PLLVAL(187, 9, 1), 	}, 	/* FVco 600.401454 */
	{ .frequency = 306358690,	.driver_data = PLLVAL(191, 9, 1), 	}, 	/* FVco 612.717380 */
	{ .frequency = 312076800,	.driver_data = PLLVAL(121, 5, 1), 	}, 	/* FVco 624.153600 */
	{ .frequency = 318366720,	.driver_data = PLLVAL(86, 3, 1), 	}, 	/* FVco 636.733440 */
	{ .frequency = 324172800,	.driver_data = PLLVAL(126, 5, 1), 	}, 	/* FVco 648.345600 */
	{ .frequency = 330220800,	.driver_data = PLLVAL(109, 4, 1), 	}, 	/* FVco 660.441600 */
	{ .frequency = 336268800,	.driver_data = PLLVAL(131, 5, 1), 	}, 	/* FVco 672.537600 */
	{ .frequency = 342074880,	.driver_data = PLLVAL(93, 3, 1), 	}, 	/* FVco 684.149760 */
	{ .frequency = 348096000,	.driver_data = PLLVAL(177, 7, 1), 	}, 	/* FVco 696.192000 */
	{ .frequency = 355622400,	.driver_data = PLLVAL(118, 4, 1), 	}, 	/* FVco 711.244800 */
	{ .frequency = 360460800,	.driver_data = PLLVAL(141, 5, 1), 	}, 	/* FVco 720.921600 */
	{ .frequency = 366206400,	.driver_data = PLLVAL(165, 6, 1), 	}, 	/* FVco 732.412800 */
	{ .frequency = 372556800,	.driver_data = PLLVAL(124, 4, 1), 	}, 	/* FVco 745.113600 */
	{ .frequency = 378201600,	.driver_data = PLLVAL(126, 4, 1), 	}, 	/* FVco 756.403200 */
	{ .frequency = 384652800,	.driver_data = PLLVAL(151, 5, 1), 	}, 	/* FVco 769.305600 */
	{ .frequency = 391608000,	.driver_data = PLLVAL(177, 6, 1), 	}, 	/* FVco 783.216000 */
	{ .frequency = 396264960,	.driver_data = PLLVAL(109, 3, 1), 	}, 	/* FVco 792.529920 */
	{ .frequency = 402192000,	.driver_data = PLLVAL(87, 2, 1), 	}, 	/* FVco 804.384000 */
};

static int s3c2440_plls169344_add(struct device *dev,
				  struct subsys_interface *sif)
{
	struct clk *xtal_clk;
	unsigned long xtal;

	xtal_clk = clk_get(NULL, "xtal");
	if (IS_ERR(xtal_clk))
		return PTR_ERR(xtal_clk);

	xtal = clk_get_rate(xtal_clk);
	clk_put(xtal_clk);

	if (xtal == 169344000) {
		printk(KERN_INFO "Using PLL table for 16.9344MHz crystal\n");
		return s3c_plltab_register(s3c2440_plls_169344,
					   ARRAY_SIZE(s3c2440_plls_169344));
	}

	return 0;
}

static struct subsys_interface s3c2440_plls169344_interface = {
	.name		= "s3c2440_plls169344",
	.subsys		= &s3c2440_subsys,
	.add_dev	= s3c2440_plls169344_add,
};

static int __init s3c2440_pll_16934400(void)
{
	return subsys_interface_register(&s3c2440_plls169344_interface);
}
arch_initcall(s3c2440_pll_16934400);

static struct subsys_interface s3c2442_plls169344_interface = {
	.name		= "s3c2442_plls169344",
	.subsys		= &s3c2442_subsys,
	.add_dev	= s3c2440_plls169344_add,
};

static int __init s3c2442_pll_16934400(void)
{
	return subsys_interface_register(&s3c2442_plls169344_interface);
}
arch_initcall(s3c2442_pll_16934400);
