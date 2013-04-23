/*
 * based on arch/arm/mach-kirkwood/cpuidle.c
 *
 * CPU idle support for AT91 SoC
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * The cpu idle uses wait-for-interrupt and RAM self refresh in order
 * to implement two idle states -
 * #1 wait-for-interrupt
 * #2 wait-for-interrupt and RAM self refresh
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/cpuidle.h>
#include <linux/io.h>
#include <linux/export.h>
#include <asm/proc-fns.h>
#include <asm/cpuidle.h>
#include <mach/cpu.h>

#include "pm.h"

#define AT91_MAX_STATES	2

static DEFINE_PER_CPU(struct cpuidle_device, at91_cpuidle_device);

/* Actual code that puts the SoC in different idle states */
static int at91_enter_idle(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			       int index)
{
	if (cpu_is_at91rm9200())
		at91rm9200_standby();
	else if (cpu_is_at91sam9g45())
		at91sam9g45_standby();
	else
		at91sam9_standby();

	return index;
}

static struct cpuidle_driver at91_idle_driver = {
	.name			= "at91_idle",
	.owner			= THIS_MODULE,
	.states[0]		= ARM_CPUIDLE_WFI_STATE,
	.states[1]		= {
		.enter			= at91_enter_idle,
		.exit_latency		= 10,
		.target_residency	= 100000,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "RAM_SR",
		.desc			= "WFI and DDR Self Refresh",
	},
	.state_count = AT91_MAX_STATES,
};

/* Initialize CPU idle by registering the idle states */
static int at91_init_cpuidle(void)
{
	struct cpuidle_device *device;

	device = &per_cpu(at91_cpuidle_device, smp_processor_id());
	device->state_count = AT91_MAX_STATES;

	cpuidle_register_driver(&at91_idle_driver);

	if (cpuidle_register_device(device)) {
		printk(KERN_ERR "at91_init_cpuidle: Failed registering\n");
		return -EIO;
	}
	return 0;
}

device_initcall(at91_init_cpuidle);
