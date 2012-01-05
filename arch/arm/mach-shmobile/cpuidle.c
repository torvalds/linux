/*
 * CPUIdle support code for SH-Mobile ARM
 *
 *  Copyright (C) 2011 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/pm.h>
#include <linux/cpuidle.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/err.h>
#include <asm/system.h>
#include <asm/io.h>

static void shmobile_enter_wfi(void)
{
	cpu_do_idle();
}

void (*shmobile_cpuidle_modes[CPUIDLE_STATE_MAX])(void) = {
	shmobile_enter_wfi, /* regular sleep mode */
};

static int shmobile_cpuidle_enter(struct cpuidle_device *dev,
				  struct cpuidle_driver *drv,
				  int index)
{
	ktime_t before, after;

	before = ktime_get();

	local_irq_disable();
	local_fiq_disable();

	shmobile_cpuidle_modes[index]();

	local_irq_enable();
	local_fiq_enable();

	after = ktime_get();
	dev->last_residency = ktime_to_ns(ktime_sub(after, before)) >> 10;

	return index;
}

static struct cpuidle_device shmobile_cpuidle_dev;
static struct cpuidle_driver shmobile_cpuidle_driver = {
	.name =		"shmobile_cpuidle",
	.owner =	THIS_MODULE,
	.states[0] = {
		.name = "C1",
		.desc = "WFI",
		.exit_latency = 1,
		.target_residency = 1 * 2,
		.flags = CPUIDLE_FLAG_TIME_VALID,
	},
	.safe_state_index = 0, /* C1 */
	.state_count = 1,
};

void (*shmobile_cpuidle_setup)(struct cpuidle_driver *drv);

static int shmobile_cpuidle_init(void)
{
	struct cpuidle_device *dev = &shmobile_cpuidle_dev;
	struct cpuidle_driver *drv = &shmobile_cpuidle_driver;
	int i;

	for (i = 0; i < CPUIDLE_STATE_MAX; i++)
		drv->states[i].enter = shmobile_cpuidle_enter;

	if (shmobile_cpuidle_setup)
		shmobile_cpuidle_setup(drv);

	cpuidle_register_driver(drv);

	dev->state_count = drv->state_count;
	cpuidle_register_device(dev);

	return 0;
}
late_initcall(shmobile_cpuidle_init);
