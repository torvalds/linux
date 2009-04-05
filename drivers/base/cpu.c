/*
 * drivers/base/cpu.c - basic CPU class support
 */

#include <linux/sysdev.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/cpu.h>
#include <linux/topology.h>
#include <linux/device.h>
#include <linux/node.h>

#include "base.h"

struct sysdev_class cpu_sysdev_class = {
	.name = "cpu",
};
EXPORT_SYMBOL(cpu_sysdev_class);

static DEFINE_PER_CPU(struct sys_device *, cpu_sys_devices);

#ifdef CONFIG_HOTPLUG_CPU
static ssize_t show_online(struct sys_device *dev, struct sysdev_attribute *attr,
			   char *buf)
{
	struct cpu *cpu = container_of(dev, struct cpu, sysdev);

	return sprintf(buf, "%u\n", !!cpu_online(cpu->sysdev.id));
}

static ssize_t __ref store_online(struct sys_device *dev, struct sysdev_attribute *attr,
				 const char *buf, size_t count)
{
	struct cpu *cpu = container_of(dev, struct cpu, sysdev);
	ssize_t ret;

	switch (buf[0]) {
	case '0':
		ret = cpu_down(cpu->sysdev.id);
		if (!ret)
			kobject_uevent(&dev->kobj, KOBJ_OFFLINE);
		break;
	case '1':
		ret = cpu_up(cpu->sysdev.id);
		if (!ret)
			kobject_uevent(&dev->kobj, KOBJ_ONLINE);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret >= 0)
		ret = count;
	return ret;
}
static SYSDEV_ATTR(online, 0644, show_online, store_online);

static void __cpuinit register_cpu_control(struct cpu *cpu)
{
	sysdev_create_file(&cpu->sysdev, &attr_online);
}
void unregister_cpu(struct cpu *cpu)
{
	int logical_cpu = cpu->sysdev.id;

	unregister_cpu_under_node(logical_cpu, cpu_to_node(logical_cpu));

	sysdev_remove_file(&cpu->sysdev, &attr_online);

	sysdev_unregister(&cpu->sysdev);
	per_cpu(cpu_sys_devices, logical_cpu) = NULL;
	return;
}
#else /* ... !CONFIG_HOTPLUG_CPU */
static inline void register_cpu_control(struct cpu *cpu)
{
}
#endif /* CONFIG_HOTPLUG_CPU */

#ifdef CONFIG_KEXEC
#include <linux/kexec.h>

static ssize_t show_crash_notes(struct sys_device *dev, struct sysdev_attribute *attr,
				char *buf)
{
	struct cpu *cpu = container_of(dev, struct cpu, sysdev);
	ssize_t rc;
	unsigned long long addr;
	int cpunum;

	cpunum = cpu->sysdev.id;

	/*
	 * Might be reading other cpu's data based on which cpu read thread
	 * has been scheduled. But cpu data (memory) is allocated once during
	 * boot up and this data does not change there after. Hence this
	 * operation should be safe. No locking required.
	 */
	addr = __pa(per_cpu_ptr(crash_notes, cpunum));
	rc = sprintf(buf, "%Lx\n", addr);
	return rc;
}
static SYSDEV_ATTR(crash_notes, 0400, show_crash_notes, NULL);
#endif

/*
 * Print cpu online, possible, present, and system maps
 */
static ssize_t print_cpus_map(char *buf, const struct cpumask *map)
{
	int n = cpulist_scnprintf(buf, PAGE_SIZE-2, map);

	buf[n++] = '\n';
	buf[n] = '\0';
	return n;
}

#define	print_cpus_func(type) \
static ssize_t print_cpus_##type(struct sysdev_class *class, char *buf)	\
{									\
	return print_cpus_map(buf, cpu_##type##_mask);			\
}									\
static struct sysdev_class_attribute attr_##type##_map = 		\
	_SYSDEV_CLASS_ATTR(type, 0444, print_cpus_##type, NULL)

print_cpus_func(online);
print_cpus_func(possible);
print_cpus_func(present);

/*
 * Print values for NR_CPUS and offlined cpus
 */
static ssize_t print_cpus_kernel_max(struct sysdev_class *class, char *buf)
{
	int n = snprintf(buf, PAGE_SIZE-2, "%d\n", NR_CPUS - 1);
	return n;
}
static SYSDEV_CLASS_ATTR(kernel_max, 0444, print_cpus_kernel_max, NULL);

/* arch-optional setting to enable display of offline cpus >= nr_cpu_ids */
unsigned int total_cpus;

static ssize_t print_cpus_offline(struct sysdev_class *class, char *buf)
{
	int n = 0, len = PAGE_SIZE-2;
	cpumask_var_t offline;

	/* display offline cpus < nr_cpu_ids */
	if (!alloc_cpumask_var(&offline, GFP_KERNEL))
		return -ENOMEM;
	cpumask_complement(offline, cpu_online_mask);
	n = cpulist_scnprintf(buf, len, offline);
	free_cpumask_var(offline);

	/* display offline cpus >= nr_cpu_ids */
	if (total_cpus && nr_cpu_ids < total_cpus) {
		if (n && n < len)
			buf[n++] = ',';

		if (nr_cpu_ids == total_cpus-1)
			n += snprintf(&buf[n], len - n, "%d", nr_cpu_ids);
		else
			n += snprintf(&buf[n], len - n, "%d-%d",
						      nr_cpu_ids, total_cpus-1);
	}

	n += snprintf(&buf[n], len - n, "\n");
	return n;
}
static SYSDEV_CLASS_ATTR(offline, 0444, print_cpus_offline, NULL);

static struct sysdev_class_attribute *cpu_state_attr[] = {
	&attr_online_map,
	&attr_possible_map,
	&attr_present_map,
	&attr_kernel_max,
	&attr_offline,
};

static int cpu_states_init(void)
{
	int i;
	int err = 0;

	for (i = 0;  i < ARRAY_SIZE(cpu_state_attr); i++) {
		int ret;
		ret = sysdev_class_create_file(&cpu_sysdev_class,
						cpu_state_attr[i]);
		if (!err)
			err = ret;
	}
	return err;
}

/*
 * register_cpu - Setup a sysfs device for a CPU.
 * @cpu - cpu->hotpluggable field set to 1 will generate a control file in
 *	  sysfs for this CPU.
 * @num - CPU number to use when creating the device.
 *
 * Initialize and register the CPU device.
 */
int __cpuinit register_cpu(struct cpu *cpu, int num)
{
	int error;
	cpu->node_id = cpu_to_node(num);
	cpu->sysdev.id = num;
	cpu->sysdev.cls = &cpu_sysdev_class;

	error = sysdev_register(&cpu->sysdev);

	if (!error && cpu->hotpluggable)
		register_cpu_control(cpu);
	if (!error)
		per_cpu(cpu_sys_devices, num) = &cpu->sysdev;
	if (!error)
		register_cpu_under_node(num, cpu_to_node(num));

#ifdef CONFIG_KEXEC
	if (!error)
		error = sysdev_create_file(&cpu->sysdev, &attr_crash_notes);
#endif
	return error;
}

struct sys_device *get_cpu_sysdev(unsigned cpu)
{
	if (cpu < nr_cpu_ids && cpu_possible(cpu))
		return per_cpu(cpu_sys_devices, cpu);
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(get_cpu_sysdev);

int __init cpu_dev_init(void)
{
	int err;

	err = sysdev_class_register(&cpu_sysdev_class);
	if (!err)
		err = cpu_states_init();

#if defined(CONFIG_SCHED_MC) || defined(CONFIG_SCHED_SMT)
	if (!err)
		err = sched_create_sysfs_power_savings_entries(&cpu_sysdev_class);
#endif

	return err;
}
