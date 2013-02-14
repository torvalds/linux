/*
 *  SMP Support for A2 platforms
 *
 *  Copyright 2007 Benjamin Herrenschmidt, IBM Corp.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/smp.h>

#include <asm/dbell.h>
#include <asm/machdep.h>
#include <asm/xics.h>

#include "ics.h"
#include "wsp.h"

static void smp_a2_setup_cpu(int cpu)
{
	doorbell_setup_this_cpu();

	if (cpu != boot_cpuid)
		xics_setup_cpu();
}

int smp_a2_kick_cpu(int nr)
{
	const char *enable_method;
	struct device_node *np;
	int thr_idx;

	if (nr < 0 || nr >= NR_CPUS)
		return -ENOENT;

	np = of_get_cpu_node(nr, &thr_idx);
	if (!np)
		return -ENODEV;

	enable_method = of_get_property(np, "enable-method", NULL);
	pr_devel("CPU%d has enable-method: \"%s\"\n", nr, enable_method);

	if (!enable_method) {
                printk(KERN_ERR "CPU%d has no enable-method\n", nr);
		return -ENOENT;
	} else if (strcmp(enable_method, "ibm,a2-scom") == 0) {
		if (a2_scom_startup_cpu(nr, thr_idx, np))
			return -1;
	} else {
		printk(KERN_ERR "CPU%d: Don't understand enable-method \"%s\"\n",
                       nr, enable_method);
		return -EINVAL;
	}

	/*
	 * The processor is currently spinning, waiting for the
	 * cpu_start field to become non-zero After we set cpu_start,
	 * the processor will continue on to secondary_start
	 */
	paca[nr].cpu_start = 1;

	return 0;
}

static int __init smp_a2_probe(void)
{
	return num_possible_cpus();
}

static struct smp_ops_t a2_smp_ops = {
	.message_pass	= NULL,	/* Use smp_muxed_ipi_message_pass */
	.cause_ipi	= doorbell_cause_ipi,
	.probe		= smp_a2_probe,
	.kick_cpu	= smp_a2_kick_cpu,
	.setup_cpu	= smp_a2_setup_cpu,
};

void __init a2_setup_smp(void)
{
	smp_ops = &a2_smp_ops;
}
