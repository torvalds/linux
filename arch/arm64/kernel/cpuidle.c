/*
 * ARM64 CPU idle arch support
 *
 * Copyright (C) 2014 ARM Ltd.
 * Author: Lorenzo Pieralisi <lorenzo.pieralisi@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/of.h>
#include <linux/of_device.h>

#include <asm/cpuidle.h>
#include <asm/cpu_ops.h>

int arm_cpuidle_init(unsigned int cpu)
{
	int ret = -EOPNOTSUPP;

	if (cpu_ops[cpu] && cpu_ops[cpu]->cpu_init_idle)
		ret = cpu_ops[cpu]->cpu_init_idle(cpu);

	return ret;
}

/**
 * cpu_suspend() - function to enter a low-power idle state
 * @arg: argument to pass to CPU suspend operations
 *
 * Return: 0 on success, -EOPNOTSUPP if CPU suspend hook not initialized, CPU
 * operations back-end error code otherwise.
 */
int arm_cpuidle_suspend(int index)
{
	int cpu = smp_processor_id();

	/*
	 * If cpu_ops have not been registered or suspend
	 * has not been initialized, cpu_suspend call fails early.
	 */
	if (!cpu_ops[cpu] || !cpu_ops[cpu]->cpu_suspend)
		return -EOPNOTSUPP;
	return cpu_ops[cpu]->cpu_suspend(index);
}
