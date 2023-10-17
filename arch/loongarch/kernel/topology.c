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

static DEFINE_PER_CPU(struct cpu, cpu_devices);

#ifdef CONFIG_HOTPLUG_CPU
int arch_register_cpu(int cpu)
{
	int ret;
	struct cpu *c = &per_cpu(cpu_devices, cpu);

	c->hotpluggable = 1;
	ret = register_cpu(c, cpu);
	if (ret < 0)
		pr_warn("register_cpu %d failed (%d)\n", cpu, ret);

	return ret;
}
EXPORT_SYMBOL(arch_register_cpu);

void arch_unregister_cpu(int cpu)
{
	struct cpu *c = &per_cpu(cpu_devices, cpu);

	c->hotpluggable = 0;
	unregister_cpu(c);
}
EXPORT_SYMBOL(arch_unregister_cpu);
#endif

static int __init topology_init(void)
{
	int i, ret;

	for_each_present_cpu(i) {
		struct cpu *c = &per_cpu(cpu_devices, i);

		c->hotpluggable = !io_master(i);
		ret = register_cpu(c, i);
		if (ret < 0)
			pr_warn("topology_init: register_cpu %d failed (%d)\n", i, ret);
	}

	return 0;
}

subsys_initcall(topology_init);
