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
#include <asm/proc-fns.h>
#include <linux/io.h>
#include <linux/export.h>

#include "pm.h"

#define AT91_MAX_STATES	2

static DEFINE_PER_CPU(struct cpuidle_device, at91_cpuidle_device);

static struct cpuidle_driver at91_idle_driver = {
	.name =         "at91_idle",
	.owner =        THIS_MODULE,
};

/* Actual code that puts the SoC in different idle states */
static int at91_enter_idle(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			       int index)
{
	struct timeval before, after;
	int idle_time;

	local_irq_disable();
	do_gettimeofday(&before);
	if (index == 0)
		/* Wait for interrupt state */
		cpu_do_idle();
	else if (index == 1)
		at91_standby();

	do_gettimeofday(&after);
	local_irq_enable();
	idle_time = (after.tv_sec - before.tv_sec) * USEC_PER_SEC +
			(after.tv_usec - before.tv_usec);

	dev->last_residency = idle_time;
	return index;
}

/* Initialize CPU idle by registering the idle states */
static int at91_init_cpuidle(void)
{
	struct cpuidle_device *device;
	struct cpuidle_driver *driver = &at91_idle_driver;

	device = &per_cpu(at91_cpuidle_device, smp_processor_id());
	device->state_count = AT91_MAX_STATES;
	driver->state_count = AT91_MAX_STATES;

	/* Wait for interrupt state */
	driver->states[0].enter = at91_enter_idle;
	driver->states[0].exit_latency = 1;
	driver->states[0].target_residency = 10000;
	driver->states[0].flags = CPUIDLE_FLAG_TIME_VALID;
	strcpy(driver->states[0].name, "WFI");
	strcpy(driver->states[0].desc, "Wait for interrupt");

	/* Wait for interrupt and RAM self refresh state */
	driver->states[1].enter = at91_enter_idle;
	driver->states[1].exit_latency = 10;
	driver->states[1].target_residency = 10000;
	driver->states[1].flags = CPUIDLE_FLAG_TIME_VALID;
	strcpy(driver->states[1].name, "RAM_SR");
	strcpy(driver->states[1].desc, "WFI and RAM Self Refresh");

	cpuidle_register_driver(&at91_idle_driver);

	if (cpuidle_register_device(device)) {
		printk(KERN_ERR "at91_init_cpuidle: Failed registering\n");
		return -EIO;
	}
	return 0;
}

device_initcall(at91_init_cpuidle);
