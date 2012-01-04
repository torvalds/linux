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
#include <mach/kirkwood.h>

#define KIRKWOOD_MAX_STATES	2

static struct cpuidle_driver kirkwood_idle_driver = {
	.name =         "kirkwood_idle",
	.owner =        THIS_MODULE,
};

static DEFINE_PER_CPU(struct cpuidle_device, kirkwood_cpuidle_device);

/* Actual code that puts the SoC in different idle states */
static int kirkwood_enter_idle(struct cpuidle_device *dev,
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
	else if (index == 1) {
		/*
		 * Following write will put DDR in self refresh.
		 * Note that we have 256 cycles before DDR puts it
		 * self in self-refresh, so the wait-for-interrupt
		 * call afterwards won't get the DDR from self refresh
		 * mode.
		 */
		writel(0x7, DDR_OPERATION_BASE);
		cpu_do_idle();
	}
	do_gettimeofday(&after);
	local_irq_enable();
	idle_time = (after.tv_sec - before.tv_sec) * USEC_PER_SEC +
			(after.tv_usec - before.tv_usec);

	/* Update last residency */
	dev->last_residency = idle_time;

	return index;
}

/* Initialize CPU idle by registering the idle states */
static int kirkwood_init_cpuidle(void)
{
	struct cpuidle_device *device;
	struct cpuidle_driver *driver = &kirkwood_idle_driver;

	device = &per_cpu(kirkwood_cpuidle_device, smp_processor_id());
	device->state_count = KIRKWOOD_MAX_STATES;
	driver->state_count = KIRKWOOD_MAX_STATES;

	/* Wait for interrupt state */
	driver->states[0].enter = kirkwood_enter_idle;
	driver->states[0].exit_latency = 1;
	driver->states[0].target_residency = 10000;
	driver->states[0].flags = CPUIDLE_FLAG_TIME_VALID;
	strcpy(driver->states[0].name, "WFI");
	strcpy(driver->states[0].desc, "Wait for interrupt");

	/* Wait for interrupt and DDR self refresh state */
	driver->states[1].enter = kirkwood_enter_idle;
	driver->states[1].exit_latency = 10;
	driver->states[1].target_residency = 10000;
	driver->states[1].flags = CPUIDLE_FLAG_TIME_VALID;
	strcpy(driver->states[1].name, "DDR SR");
	strcpy(driver->states[1].desc, "WFI and DDR Self Refresh");

	cpuidle_register_driver(&kirkwood_idle_driver);
	if (cpuidle_register_device(device)) {
		printk(KERN_ERR "kirkwood_init_cpuidle: Failed registering\n");
		return -EIO;
	}
	return 0;
}

device_initcall(kirkwood_init_cpuidle);
