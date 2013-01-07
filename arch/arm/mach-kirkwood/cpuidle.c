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
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/cpuidle.h>
#include <linux/io.h>
#include <linux/export.h>
#include <asm/proc-fns.h>
#include <asm/cpuidle.h>
#include <mach/kirkwood.h>

#define KIRKWOOD_MAX_STATES	2

/* Actual code that puts the SoC in different idle states */
static int kirkwood_enter_idle(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
			       int index)
{
	writel(0x7, DDR_OPERATION_BASE);
	cpu_do_idle();

	return index;
}

static struct cpuidle_driver kirkwood_idle_driver = {
	.name			= "kirkwood_idle",
	.owner			= THIS_MODULE,
	.en_core_tk_irqen	= 1,
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

static DEFINE_PER_CPU(struct cpuidle_device, kirkwood_cpuidle_device);

/* Initialize CPU idle by registering the idle states */
static int kirkwood_init_cpuidle(void)
{
	struct cpuidle_device *device;

	device = &per_cpu(kirkwood_cpuidle_device, smp_processor_id());
	device->state_count = KIRKWOOD_MAX_STATES;

	cpuidle_register_driver(&kirkwood_idle_driver);
	if (cpuidle_register_device(device)) {
		pr_err("kirkwood_init_cpuidle: Failed registering\n");
		return -EIO;
	}
	return 0;
}

device_initcall(kirkwood_init_cpuidle);
