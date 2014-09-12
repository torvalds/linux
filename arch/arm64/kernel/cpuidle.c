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

int cpu_init_idle(unsigned int cpu)
{
	int ret = -EOPNOTSUPP;
	struct device_node *cpu_node = of_cpu_device_node_get(cpu);

	if (!cpu_node)
		return -ENODEV;

	if (cpu_ops[cpu] && cpu_ops[cpu]->cpu_init_idle)
		ret = cpu_ops[cpu]->cpu_init_idle(cpu_node, cpu);

	of_node_put(cpu_node);
	return ret;
}
