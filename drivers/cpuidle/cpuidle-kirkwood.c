// SPDX-License-Identifier: GPL-2.0-only
/*
 * CPU idle Marvell Kirkwood SoCs
 *
 * The cpu idle uses wait-for-interrupt and DDR self refresh in order
 * to implement two idle states -
 * #1 wait-for-interrupt
 * #2 wait-for-interrupt and DDR self refresh
 *
 * Maintainer: Jason Cooper <jason@lakedaemon.net>
 * Maintainer: Andrew Lunn <andrew@lunn.ch>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/cpuidle.h>
#include <linux/io.h>
#include <linux/export.h>
#include <asm/cpuidle.h>

#define KIRKWOOD_MAX_STATES	2

static void __iomem *ddr_operation_base;

/* Actual code that puts the SoC in different idle states */
static int kirkwood_enter_idle(struct cpuidle_device *dev,
			       struct cpuidle_driver *drv,
			       int index)
{
	writel(0x7, ddr_operation_base);
	cpu_do_idle();

	return index;
}

static struct cpuidle_driver kirkwood_idle_driver = {
	.name			= "kirkwood_idle",
	.owner			= THIS_MODULE,
	.states[0]		= ARM_CPUIDLE_WFI_STATE,
	.states[1]		= {
		.enter			= kirkwood_enter_idle,
		.exit_latency		= 10,
		.target_residency	= 100000,
		.name			= "DDR SR",
		.desc			= "WFI and DDR Self Refresh",
	},
	.state_count = KIRKWOOD_MAX_STATES,
};

/* Initialize CPU idle by registering the idle states */
static int kirkwood_cpuidle_probe(struct platform_device *pdev)
{
	ddr_operation_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ddr_operation_base))
		return PTR_ERR(ddr_operation_base);

	return cpuidle_register(&kirkwood_idle_driver, NULL);
}

static void kirkwood_cpuidle_remove(struct platform_device *pdev)
{
	cpuidle_unregister(&kirkwood_idle_driver);
}

static struct platform_driver kirkwood_cpuidle_driver = {
	.probe = kirkwood_cpuidle_probe,
	.remove_new = kirkwood_cpuidle_remove,
	.driver = {
		   .name = "kirkwood_cpuidle",
		   },
};

module_platform_driver(kirkwood_cpuidle_driver);

MODULE_AUTHOR("Andrew Lunn <andrew@lunn.ch>");
MODULE_DESCRIPTION("Kirkwood cpu idle driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:kirkwood-cpuidle");
