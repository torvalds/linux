// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  CLPS711X CPU idle driver
 *
 *  Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 */

#include <linux/cpuidle.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#define CLPS711X_CPUIDLE_NAME	"clps711x-cpuidle"

static void __iomem *clps711x_halt;

static int clps711x_cpuidle_halt(struct cpuidle_device *dev,
				 struct cpuidle_driver *drv, int index)
{
	writel(0xaa, clps711x_halt);

	return index;
}

static struct cpuidle_driver clps711x_idle_driver = {
	.name		= CLPS711X_CPUIDLE_NAME,
	.owner		= THIS_MODULE,
	.states[0]	= {
		.name		= "HALT",
		.desc		= "CLPS711X HALT",
		.enter		= clps711x_cpuidle_halt,
		.exit_latency	= 1,
	},
	.state_count	= 1,
};

static int __init clps711x_cpuidle_probe(struct platform_device *pdev)
{
	clps711x_halt = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(clps711x_halt))
		return PTR_ERR(clps711x_halt);

	return cpuidle_register(&clps711x_idle_driver, NULL);
}

static struct platform_driver clps711x_cpuidle_driver = {
	.driver	= {
		.name	= CLPS711X_CPUIDLE_NAME,
	},
};
builtin_platform_driver_probe(clps711x_cpuidle_driver, clps711x_cpuidle_probe);
