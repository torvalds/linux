/* devices.c: Initial scan of the prom device tree for important
 *            Sparc device nodes which we need to find.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/bootmem.h>

#include <asm/page.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <asm/smp.h>
#include <asm/spitfire.h>
#include <asm/timer.h>
#include <asm/cpudata.h>

/* Used to synchronize accesses to NatSemi SUPER I/O chip configure
 * operations in asm/ns87303.h
 */
DEFINE_SPINLOCK(ns87303_lock);

extern void cpu_probe(void);
extern void central_probe(void);

static const char *cpu_mid_prop(void)
{
	if (tlb_type == spitfire)
		return "upa-portid";
	return "portid";
}

static int get_cpu_mid(struct device_node *dp)
{
	struct property *prop;

	if (tlb_type == hypervisor) {
		struct linux_prom64_registers *reg;
		int len;

		prop = of_find_property(dp, "cpuid", &len);
		if (prop && len == 4)
			return *(int *) prop->value;

		prop = of_find_property(dp, "reg", NULL);
		reg = prop->value;
		return (reg[0].phys_addr >> 32) & 0x0fffffffUL;
	} else {
		const char *prop_name = cpu_mid_prop();

		prop = of_find_property(dp, prop_name, NULL);
		if (prop)
			return *(int *) prop->value;
		return 0;
	}
}

static int check_cpu_node(struct device_node *dp, int *cur_inst,
			  int (*compare)(struct device_node *, int, void *),
			  void *compare_arg,
			  struct device_node **dev_node, int *mid)
{
	if (!compare(dp, *cur_inst, compare_arg)) {
		if (dev_node)
			*dev_node = dp;
		if (mid)
			*mid = get_cpu_mid(dp);
		return 0;
	}

	(*cur_inst)++;

	return -ENODEV;
}

static int __cpu_find_by(int (*compare)(struct device_node *, int, void *),
			 void *compare_arg,
			 struct device_node **dev_node, int *mid)
{
	struct device_node *dp;
	int cur_inst;

	cur_inst = 0;
	for_each_node_by_type(dp, "cpu") {
		int err = check_cpu_node(dp, &cur_inst,
					 compare, compare_arg,
					 dev_node, mid);
		if (err == 0)
			return 0;
	}

	return -ENODEV;
}

static int cpu_instance_compare(struct device_node *dp, int instance, void *_arg)
{
	int desired_instance = (int) (long) _arg;

	if (instance == desired_instance)
		return 0;
	return -ENODEV;
}

int cpu_find_by_instance(int instance, struct device_node **dev_node, int *mid)
{
	return __cpu_find_by(cpu_instance_compare, (void *)(long)instance,
			     dev_node, mid);
}

static int cpu_mid_compare(struct device_node *dp, int instance, void *_arg)
{
	int desired_mid = (int) (long) _arg;
	int this_mid;

	this_mid = get_cpu_mid(dp);
	if (this_mid == desired_mid)
		return 0;
	return -ENODEV;
}

int cpu_find_by_mid(int mid, struct device_node **dev_node)
{
	return __cpu_find_by(cpu_mid_compare, (void *)(long)mid,
			     dev_node, NULL);
}

void __init device_scan(void)
{
	/* FIX ME FAST... -DaveM */
	ioport_resource.end = 0xffffffffffffffffUL;

	prom_printf("Booting Linux...\n");

#ifndef CONFIG_SMP
	{
		struct device_node *dp;
		int err, def;

		err = cpu_find_by_instance(0, &dp, NULL);
		if (err) {
			prom_printf("No cpu nodes, cannot continue\n");
			prom_halt();
		}
		cpu_data(0).clock_tick =
			of_getintprop_default(dp, "clock-frequency", 0);

		def = ((tlb_type == hypervisor) ?
		       (8 * 1024) :
		       (16 * 1024));
		cpu_data(0).dcache_size = of_getintprop_default(dp,
								"dcache-size",
								def);

		def = 32;
		cpu_data(0).dcache_line_size =
			of_getintprop_default(dp, "dcache-line-size", def);

		def = 16 * 1024;
		cpu_data(0).icache_size = of_getintprop_default(dp,
								"icache-size",
								def);

		def = 32;
		cpu_data(0).icache_line_size =
			of_getintprop_default(dp, "icache-line-size", def);

		def = ((tlb_type == hypervisor) ?
		       (3 * 1024 * 1024) :
		       (4 * 1024 * 1024));
		cpu_data(0).ecache_size = of_getintprop_default(dp,
								"ecache-size",
								def);

		def = 64;
		cpu_data(0).ecache_line_size =
			of_getintprop_default(dp, "ecache-line-size", def);
		printk("CPU[0]: Caches "
		       "D[sz(%d):line_sz(%d)] "
		       "I[sz(%d):line_sz(%d)] "
		       "E[sz(%d):line_sz(%d)]\n",
		       cpu_data(0).dcache_size, cpu_data(0).dcache_line_size,
		       cpu_data(0).icache_size, cpu_data(0).icache_line_size,
		       cpu_data(0).ecache_size, cpu_data(0).ecache_line_size);
	}
#endif

	central_probe();

	cpu_probe();
}
