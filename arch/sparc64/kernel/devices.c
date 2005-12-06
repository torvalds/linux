/* devices.c: Initial scan of the prom device tree for important
 *            Sparc device nodes which we need to find.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/errno.h>

#include <asm/page.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <asm/smp.h>
#include <asm/spitfire.h>
#include <asm/timer.h>
#include <asm/cpudata.h>

/* Used to synchronize acceses to NatSemi SUPER I/O chip configure
 * operations in asm/ns87303.h
 */
DEFINE_SPINLOCK(ns87303_lock);

extern void cpu_probe(void);
extern void central_probe(void);

static char *cpu_mid_prop(void)
{
	if (tlb_type == spitfire)
		return "upa-portid";
	return "portid";
}

static int check_cpu_node(int nd, int *cur_inst,
			  int (*compare)(int, int, void *), void *compare_arg,
			  int *prom_node, int *mid)
{
	char node_str[128];

	prom_getstring(nd, "device_type", node_str, sizeof(node_str));
	if (strcmp(node_str, "cpu"))
		return -ENODEV;

	if (!compare(nd, *cur_inst, compare_arg)) {
		if (prom_node)
			*prom_node = nd;
		if (mid)
			*mid = prom_getintdefault(nd, cpu_mid_prop(), 0);
		return 0;
	}

	(*cur_inst)++;

	return -ENODEV;
}

static int __cpu_find_by(int (*compare)(int, int, void *), void *compare_arg,
			 int *prom_node, int *mid)
{
	int nd, cur_inst, err;

	nd = prom_root_node;
	cur_inst = 0;

	err = check_cpu_node(nd, &cur_inst,
			     compare, compare_arg,
			     prom_node, mid);
	if (err == 0)
		return 0;

	nd = prom_getchild(nd);
	while ((nd = prom_getsibling(nd)) != 0) {
		err = check_cpu_node(nd, &cur_inst,
				     compare, compare_arg,
				     prom_node, mid);
		if (err == 0)
			return 0;
	}

	return -ENODEV;
}

static int cpu_instance_compare(int nd, int instance, void *_arg)
{
	int desired_instance = (int) (long) _arg;

	if (instance == desired_instance)
		return 0;
	return -ENODEV;
}

int cpu_find_by_instance(int instance, int *prom_node, int *mid)
{
	return __cpu_find_by(cpu_instance_compare, (void *)(long)instance,
			     prom_node, mid);
}

static int cpu_mid_compare(int nd, int instance, void *_arg)
{
	int desired_mid = (int) (long) _arg;
	int this_mid;

	this_mid = prom_getintdefault(nd, cpu_mid_prop(), 0);
	if (this_mid == desired_mid)
		return 0;
	return -ENODEV;
}

int cpu_find_by_mid(int mid, int *prom_node)
{
	return __cpu_find_by(cpu_mid_compare, (void *)(long)mid,
			     prom_node, NULL);
}

void __init device_scan(void)
{
	/* FIX ME FAST... -DaveM */
	ioport_resource.end = 0xffffffffffffffffUL;

	prom_printf("Booting Linux...\n");

#ifndef CONFIG_SMP
	{
		int err, cpu_node;
		err = cpu_find_by_instance(0, &cpu_node, NULL);
		if (err) {
			prom_printf("No cpu nodes, cannot continue\n");
			prom_halt();
		}
		cpu_data(0).clock_tick = prom_getintdefault(cpu_node,
							    "clock-frequency",
							    0);
		cpu_data(0).dcache_size = prom_getintdefault(cpu_node,
							     "dcache-size",
							     16 * 1024);
		cpu_data(0).dcache_line_size =
			prom_getintdefault(cpu_node, "dcache-line-size", 32);
		cpu_data(0).icache_size = prom_getintdefault(cpu_node,
							     "icache-size",
							     16 * 1024);
		cpu_data(0).icache_line_size =
			prom_getintdefault(cpu_node, "icache-line-size", 32);
		cpu_data(0).ecache_size = prom_getintdefault(cpu_node,
							     "ecache-size",
							     4 * 1024 * 1024);
		cpu_data(0).ecache_line_size =
			prom_getintdefault(cpu_node, "ecache-line-size", 64);
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
