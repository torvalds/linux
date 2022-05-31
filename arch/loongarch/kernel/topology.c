// SPDX-License-Identifier: GPL-2.0
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/percpu.h>

static struct cpu cpu_device;

static int __init topology_init(void)
{
	return register_cpu(&cpu_device, 0);
}

subsys_initcall(topology_init);
