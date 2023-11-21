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
int arch_register_cpu(int cpu)
{
	struct cpu *c = &per_cpu(cpu_devices, cpu);

	c->hotpluggable = !io_master(cpu);
	return register_cpu(c, cpu);
}

void arch_unregister_cpu(int cpu)
{
	struct cpu *c = &per_cpu(cpu_devices, cpu);

	c->hotpluggable = 0;
	unregister_cpu(c);
}
#endif
