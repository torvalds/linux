// SPDX-License-Identifier: GPL-2.0
#include <linux/acpi.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/node.h>
#include <linux/nodemask.h>
#include <linux/percpu.h>
#include <asm/bootinfo.h>

#include <acpi/processor.h>

#ifdef CONFIG_HOTPLUG_CPU
bool arch_cpu_is_hotpluggable(int cpu)
{
	return !io_master(cpu);
}
#endif
