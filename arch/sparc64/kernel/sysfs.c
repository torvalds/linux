/* sysfs.c: Toplogy sysfs support code for sparc64.
 *
 * Copyright (C) 2007 David S. Miller <davem@davemloft.net>
 */
#include <linux/sysdev.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <linux/init.h>

static DEFINE_PER_CPU(struct cpu, cpu_devices);

#define SHOW_ULONG_NAME(NAME, MEMBER) \
static ssize_t show_##NAME(struct sys_device *dev, char *buf) \
{ \
	struct cpu *cpu = container_of(dev, struct cpu, sysdev); \
	cpuinfo_sparc *c = &cpu_data(cpu->sysdev.id); \
	return sprintf(buf, "%lu\n", c->MEMBER); \
}

#define SHOW_UINT_NAME(NAME, MEMBER) \
static ssize_t show_##NAME(struct sys_device *dev, char *buf) \
{ \
	struct cpu *cpu = container_of(dev, struct cpu, sysdev); \
	cpuinfo_sparc *c = &cpu_data(cpu->sysdev.id); \
	return sprintf(buf, "%u\n", c->MEMBER); \
}

SHOW_ULONG_NAME(clock_tick, clock_tick);
SHOW_ULONG_NAME(udelay_val, udelay_val);
SHOW_UINT_NAME(l1_dcache_size, dcache_size);
SHOW_UINT_NAME(l1_dcache_line_size, dcache_line_size);
SHOW_UINT_NAME(l1_icache_size, icache_size);
SHOW_UINT_NAME(l1_icache_line_size, icache_line_size);
SHOW_UINT_NAME(l2_cache_size, ecache_size);
SHOW_UINT_NAME(l2_cache_line_size, ecache_line_size);

static struct sysdev_attribute cpu_core_attrs[] = {
	_SYSDEV_ATTR(clock_tick,          0444, show_clock_tick, NULL),
	_SYSDEV_ATTR(udelay_val,          0444, show_udelay_val, NULL),
	_SYSDEV_ATTR(l1_dcache_size,      0444, show_l1_dcache_size, NULL),
	_SYSDEV_ATTR(l1_dcache_line_size, 0444, show_l1_dcache_line_size, NULL),
	_SYSDEV_ATTR(l1_icache_size,      0444, show_l1_icache_size, NULL),
	_SYSDEV_ATTR(l1_icache_line_size, 0444, show_l1_icache_line_size, NULL),
	_SYSDEV_ATTR(l2_cache_size,       0444, show_l2_cache_size, NULL),
	_SYSDEV_ATTR(l2_cache_line_size,  0444, show_l2_cache_line_size, NULL),
};

static void register_cpu_online(unsigned int cpu)
{
	struct cpu *c = &per_cpu(cpu_devices, cpu);
	struct sys_device *s = &c->sysdev;
	int i;

	for (i = 0; i < ARRAY_SIZE(cpu_core_attrs); i++)
		sysdev_create_file(s, &cpu_core_attrs[i]);
}

#ifdef CONFIG_HOTPLUG_CPU
static void unregister_cpu_online(unsigned int cpu)
{
	struct cpu *c = &per_cpu(cpu_devices, cpu);
	struct sys_device *s = &c->sysdev;
	int i;

	for (i = 0; i < ARRAY_SIZE(cpu_core_attrs); i++)
		sysdev_remove_file(s, &cpu_core_attrs[i]);
}
#endif

static int __cpuinit sysfs_cpu_notify(struct notifier_block *self,
				      unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned int)(long)hcpu;

	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		register_cpu_online(cpu);
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		unregister_cpu_online(cpu);
		break;
#endif
	}
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata sysfs_cpu_nb = {
	.notifier_call	= sysfs_cpu_notify,
};

static int __init topology_init(void)
{
	int cpu;

	register_cpu_notifier(&sysfs_cpu_nb);

	for_each_possible_cpu(cpu) {
		struct cpu *c = &per_cpu(cpu_devices, cpu);

		register_cpu(c, cpu);
		if (cpu_online(cpu))
			register_cpu_online(cpu);
	}

	return 0;
}

subsys_initcall(topology_init);
