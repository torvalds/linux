// SPDX-License-Identifier: GPL-2.0
/* devices.c: Initial scan of the prom device tree for important
 *	      Sparc device yesdes which we need to find.
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
#include <linux/erryes.h>

#include <asm/page.h>
#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/cpudata.h>
#include <asm/cpu_type.h>
#include <asm/setup.h>

#include "kernel.h"

static char *cpu_mid_prop(void)
{
	if (sparc_cpu_model == sun4d)
		return "cpu-id";
	return "mid";
}

static int check_cpu_yesde(phandle nd, int *cur_inst,
		int (*compare)(phandle, int, void *), void *compare_arg,
		phandle *prom_yesde, int *mid)
{
	if (!compare(nd, *cur_inst, compare_arg)) {
		if (prom_yesde)
			*prom_yesde = nd;
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
		void *compare_arg, phandle *prom_yesde, int *mid)
{
	struct device_yesde *dp;
	int cur_inst;

	cur_inst = 0;
	for_each_yesde_by_type(dp, "cpu") {
		int err = check_cpu_yesde(dp->phandle, &cur_inst,
					 compare, compare_arg,
					 prom_yesde, mid);
		if (!err) {
			of_yesde_put(dp);
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

int cpu_find_by_instance(int instance, phandle *prom_yesde, int *mid)
{
	return __cpu_find_by(cpu_instance_compare, (void *)instance,
			     prom_yesde, mid);
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

int cpu_find_by_mid(int mid, phandle *prom_yesde)
{
	return __cpu_find_by(cpu_mid_compare, (void *)mid,
			     prom_yesde, NULL);
}

/* sun4m uses truncated mids since we base the cpuid on the ttable/irqset
 * address (0-3).  This gives us the true hardware mid, which might have
 * some other bits set.  On 4d hardware and software mids are the same.
 */
int cpu_get_hwmid(phandle prom_yesde)
{
	return prom_getintdefault(prom_yesde, cpu_mid_prop(), -ENODEV);
}

void __init device_scan(void)
{
	printk(KERN_NOTICE "Booting Linux...\n");

#ifndef CONFIG_SMP
	{
		phandle cpu_yesde;
		int err;
		err = cpu_find_by_instance(0, &cpu_yesde, NULL);
		if (err) {
			/* Probably a sun4e, Sun is trying to trick us ;-) */
			prom_printf("No cpu yesdes, canyest continue\n");
			prom_halt();
		}
		cpu_data(0).clock_tick = prom_getintdefault(cpu_yesde,
							    "clock-frequency",
							    0);
	}
#endif /* !CONFIG_SMP */

	auxio_probe();
	auxio_power_probe();
}
