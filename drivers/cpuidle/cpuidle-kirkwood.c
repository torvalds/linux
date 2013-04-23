/*
 * arch/arm/mach-kirkwood/cpuidle.c
 *
 * CPU idle Marvell Kirkwood SoCs
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * The cpu idle uses wait-for-interrupt and DDR self refresh in order
 * to implement two idle states -
 * #1 wait-for-interrupt
 * #2 wait-for-interrupt and DDR self refresh
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/cpuidle.h>
#include <linux/io.h>
#include <linux/export.h>
#include <asm/proc-fns.h>
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
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "DDR SR",
		.desc			= "WFI and DDR Self Refresh",
	},
	.state_count = KIRKWOOD_MAX_STATES,
};
static struct cpuidle_device *device;

static DEFINE_PER_CPU(struct cpuidle_device, kirkwood_cpuidle_device);

/* Initialize CPU idle by registering the idle states */
static int kirkwood_cpuidle_probe(struct platform_device *pdev)
{
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -EINVAL;

	ddr_operation_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ddr_operation_base))
		return PTR_ERR(ddr_operation_base);

	device = &per_cpu(kirkwood_cpuidle_device, smp_processor_id());
	device->state_count = KIRKWOOD_MAX_STATES;

	cpuidle_register_driver(&kirkwood_idle_driver);
	if (cpuidle_register_device(device)) {
		pr_err("kirkwood_init_cpuidle: Failed registering\n");
		return -EIO;
	}
	return 0;
}

int kirkwood_cpuidle_remove(struct platform_device *pdev)
{
	cpuidle_unregister_device(device);
	cpuidle_unregister_driver(&kirkwood_idle_driver);

	return 0;
}

static struct platform_driver kirkwood_cpuidle_driver = {
	.probe = kirkwood_cpuidle_probe,
	.remove = kirkwood_cpuidle_remove,
	.driver = {
		   .name = "kirkwood_cpuidle",
		   .owner = THIS_MODULE,
		   },
};

module_platform_driver(kirkwood_cpuidle_driver);

MODULE_AUTHOR("Andrew Lunn <andrew@lunn.ch>");
MODULE_DESCRIPTION("Kirkwood cpu idle driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:kirkwood-cpuidle");
