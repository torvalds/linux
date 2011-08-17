/* linux/arch/arm/mach-exynos4/cpuidle.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cpuidle.h>
#include <linux/io.h>

#include <asm/proc-fns.h>

static int exynos4_enter_idle(struct cpuidle_device *dev,
			      struct cpuidle_state *state);

static struct cpuidle_state exynos4_cpuidle_set[] = {
	[0] = {
		.enter			= exynos4_enter_idle,
		.exit_latency		= 1,
		.target_residency	= 100000,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "IDLE",
		.desc			= "ARM clock gating(WFI)",
	},
};

static DEFINE_PER_CPU(struct cpuidle_device, exynos4_cpuidle_device);

static struct cpuidle_driver exynos4_idle_driver = {
	.name		= "exynos4_idle",
	.owner		= THIS_MODULE,
};

static int exynos4_enter_idle(struct cpuidle_device *dev,
			      struct cpuidle_state *state)
{
	struct timeval before, after;
	int idle_time;

	local_irq_disable();
	do_gettimeofday(&before);

	cpu_do_idle();

	do_gettimeofday(&after);
	local_irq_enable();
	idle_time = (after.tv_sec - before.tv_sec) * USEC_PER_SEC +
		    (after.tv_usec - before.tv_usec);

	return idle_time;
}

static int __init exynos4_init_cpuidle(void)
{
	int i, max_cpuidle_state, cpu_id;
	struct cpuidle_device *device;

	cpuidle_register_driver(&exynos4_idle_driver);

	for_each_cpu(cpu_id, cpu_online_mask) {
		device = &per_cpu(exynos4_cpuidle_device, cpu_id);
		device->cpu = cpu_id;

		device->state_count = (sizeof(exynos4_cpuidle_set) /
					       sizeof(struct cpuidle_state));

		max_cpuidle_state = device->state_count;

		for (i = 0; i < max_cpuidle_state; i++) {
			memcpy(&device->states[i], &exynos4_cpuidle_set[i],
					sizeof(struct cpuidle_state));
		}

		if (cpuidle_register_device(device)) {
			printk(KERN_ERR "CPUidle register device failed\n,");
			return -EIO;
		}
	}
	return 0;
}
device_initcall(exynos4_init_cpuidle);
