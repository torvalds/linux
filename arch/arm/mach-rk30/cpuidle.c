/*
 * Copyright (C) 2012 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) "cpuidle: %s: " fmt, __func__

#include <linux/pm.h>
#include <linux/cpuidle.h>
#include <linux/suspend.h>
#include <linux/err.h>

#include <asm/hardware/gic.h>
#include <asm/io.h>

static bool rk30_gic_interrupt_pending(void)
{
	return (readl_relaxed(RK30_GICC_BASE + GIC_CPU_HIGHPRI) != 0x3FF);
}

static void rk30_wfi_until_interrupt(void)
{
retry:
	cpu_do_idle();

	if (!rk30_gic_interrupt_pending())
		goto retry;
}

static int rk30_idle(struct cpuidle_device *dev, struct cpuidle_state *state)
{
	ktime_t preidle, postidle;

	local_fiq_disable();

	preidle = ktime_get();

	rk30_wfi_until_interrupt();

	postidle = ktime_get();

	local_fiq_enable();
	local_irq_enable();

	return ktime_to_us(ktime_sub(postidle, preidle));
}

static DEFINE_PER_CPU(struct cpuidle_device, rk30_cpuidle_device);

static __initdata struct cpuidle_state rk30_cpuidle_states[] = {
	{
		.name = "C1",
		.desc = "idle",
		.flags = CPUIDLE_FLAG_TIME_VALID,
		.exit_latency = 0,
		.target_residency = 0,
		.enter = rk30_idle,
	},
};

static struct cpuidle_driver rk30_cpuidle_driver = {
	.name = "rk30_cpuidle",
	.owner = THIS_MODULE,
};

static int __init rk30_cpuidle_init(void)
{
	struct cpuidle_device *dev;
	unsigned int cpu;
	int ret;

	ret = cpuidle_register_driver(&rk30_cpuidle_driver);
	if (ret) {
		pr_err("failed to register cpuidle driver: %d\n", ret);
		return ret;
	}

	for_each_possible_cpu(cpu) {
		dev = &per_cpu(rk30_cpuidle_device, cpu);
		dev->cpu = cpu;
		dev->state_count = ARRAY_SIZE(rk30_cpuidle_states);
		memcpy(dev->states, rk30_cpuidle_states, sizeof(rk30_cpuidle_states));
		dev->safe_state = &dev->states[0];

		ret = cpuidle_register_device(dev);
		if (ret) {
			pr_err("failed to register cpuidle device for cpu %u: %d\n", cpu, ret);
			return ret;
		}
	}

	return 0;
}
late_initcall(rk30_cpuidle_init);
