/* devices.c: Initial scan of the prom device tree for important
 *	      Sparc device nodes which we need to find.
 *
 * This is based on the sparc64 version, but sun4m doesn't always use
 * the hardware MIDs, so be careful.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/errno.h>

#include <asm/page.h>
#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/system.h>
#include <asm/cpudata.h>

extern void cpu_probe(void);
extern void clock_stop_probe(void); /* tadpole.c */
extern void sun4c_probe_memerr_reg(void);

static char *cpu_mid_prop(void)
{
	if (sparc_cpu_model == sun4d)
		return "cpu-id";
	return "mid";
}

static int check_cpu_node(phandle nd, int *cur_inst,
		int (*compare)(phandle, int, void *), void *compare_arg,
		phandle *prom_node, int *mid)
{
	if (!compare(nd, *cur_inst, compare_arg)) {
		if (prom_node)
			*prom_node = nd;
		if (mid) {
			*mid = prom_getintdefault(nd, cpu_mid_prop(), 0);
			if (sparc_cpu_model == sun4m)
				*mid &= 3;
		}
		return 0;
	}

	(*cur_inst)++;

	return -ENODEV;
}

static int __cpu_find_by(int (*compare)(phandle, int, void *),
		void *compare_arg, phandle *prom_node, int *mid)
{
	struct device_node *dp;
	int cur_inst;

	cur_inst = 0;
	for_each_node_by_type(dp, "cpu") {
		int err = check_cpu_node(dp->phandle, &cur_inst,
					 compare, compare_arg,
					 prom_node, mid);
		if (!err) {
			of_node_put(dp);
			return 0;
		}
	}

	return -ENODEV;
}

static int cpu_instance_compare(phandle nd, int instance, void *_arg)
{
	int desired_instance = (int) _arg;

	if (instance == desired_instance)
		return 0;
	return -ENODEV;
}

int cpu_find_by_instance(int instance, phandle *prom_node, int *mid)
{
	return __cpu_find_by(cpu_instance_compare, (void *)instance,
			     prom_node, mid);
}

static int cpu_mid_compare(phandle nd, int instance, void *_arg)
{
	int desired_mid = (int) _arg;
	int this_mid;

	this_mid = prom_getintdefault(nd, cpu_mid_prop(), 0);
	if (this_mid == desired_mid
	    || (sparc_cpu_model == sun4m && (this_mid & 3) == desired_mid))
		return 0;
	return -ENODEV;
}

int cpu_find_by_mid(int mid, phandle *prom_node)
{
	return __cpu_find_by(cpu_mid_compare, (void *)mid,
			     prom_node, NULL);
}

/* sun4m uses truncated mids since we base the cpuid on the ttable/irqset
 * address (0-3).  This gives us the true hardware mid, which might have
 * some other bits set.  On 4d hardware and software mids are the same.
 */
int cpu_get_hwmid(phandle prom_node)
{
	return prom_getintdefault(prom_node, cpu_mid_prop(), -ENODEV);
}

void __init device_scan(void)
{
	prom_printf("Booting Linux...\n");

#ifndef CONFIG_SMP
	{
		phandle cpu_node;
		int err;
		err = cpu_find_by_instance(0, &cpu_node, NULL);
		if (err) {
			/* Probably a sun4e, Sun is trying to trick us ;-) */
			prom_printf("No cpu nodes, cannot continue\n");
			prom_halt();
		}
		cpu_data(0).clock_tick = prom_getintdefault(cpu_node,
							    "clock-frequency",
							    0);
	}
#endif /* !CONFIG_SMP */

	cpu_probe();
	{
		extern void auxio_probe(void);
		extern void auxio_power_probe(void);
		auxio_probe();
		auxio_power_probe();
	}
	clock_stop_probe();

	if (ARCH_SUN4C)
		sun4c_probe_memerr_reg();
}
