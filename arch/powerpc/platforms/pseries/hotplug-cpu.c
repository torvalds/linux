/*
 * pseries CPU Hotplug infrastructure.
 *
 * Split out from arch/powerpc/platforms/pseries/setup.c
 *  arch/powerpc/kernel/rtas.c, and arch/powerpc/platforms/pseries/smp.c
 *
 * Peter Bergner, IBM	March 2001.
 * Copyright (C) 2001 IBM.
 * Dave Engebretsen, Peter Bergner, and
 * Mike Corrigan {engebret|bergner|mikec}@us.ibm.com
 * Plus various changes from other IBM teams...
 *
 * Copyright (C) 2006 Michael Ellerman, IBM Corporation
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <asm/system.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/firmware.h>
#include <asm/machdep.h>
#include <asm/vdso_datapage.h>
#include <asm/pSeries_reconfig.h>
#include "xics.h"

/* This version can't take the spinlock, because it never returns */
static struct rtas_args rtas_stop_self_args = {
	.token = RTAS_UNKNOWN_SERVICE,
	.nargs = 0,
	.nret = 1,
	.rets = &rtas_stop_self_args.args[0],
};

static void rtas_stop_self(void)
{
	struct rtas_args *args = &rtas_stop_self_args;

	local_irq_disable();

	BUG_ON(args->token == RTAS_UNKNOWN_SERVICE);

	printk("cpu %u (hwid %u) Ready to die...\n",
	       smp_processor_id(), hard_smp_processor_id());
	enter_rtas(__pa(args));

	panic("Alas, I survived.\n");
}

static void pseries_mach_cpu_die(void)
{
	local_irq_disable();
	idle_task_exit();
	xics_teardown_cpu(0);
	rtas_stop_self();
	/* Should never get here... */
	BUG();
	for(;;);
}

static int qcss_tok;	/* query-cpu-stopped-state token */

/* Get state of physical CPU.
 * Return codes:
 *	0	- The processor is in the RTAS stopped state
 *	1	- stop-self is in progress
 *	2	- The processor is not in the RTAS stopped state
 *	-1	- Hardware Error
 *	-2	- Hardware Busy, Try again later.
 */
static int query_cpu_stopped(unsigned int pcpu)
{
	int cpu_status, status;

	status = rtas_call(qcss_tok, 1, 2, &cpu_status, pcpu);
	if (status != 0) {
		printk(KERN_ERR
		       "RTAS query-cpu-stopped-state failed: %i\n", status);
		return status;
	}

	return cpu_status;
}

static int pseries_cpu_disable(void)
{
	int cpu = smp_processor_id();

	cpu_clear(cpu, cpu_online_map);
	vdso_data->processorCount--;

	/*fix boot_cpuid here*/
	if (cpu == boot_cpuid)
		boot_cpuid = any_online_cpu(cpu_online_map);

	/* FIXME: abstract this to not be platform specific later on */
	xics_migrate_irqs_away();
	return 0;
}

static void pseries_cpu_die(unsigned int cpu)
{
	int tries;
	int cpu_status;
	unsigned int pcpu = get_hard_smp_processor_id(cpu);

	for (tries = 0; tries < 25; tries++) {
		cpu_status = query_cpu_stopped(pcpu);
		if (cpu_status == 0 || cpu_status == -1)
			break;
		msleep(200);
	}
	if (cpu_status != 0) {
		printk("Querying DEAD? cpu %i (%i) shows %i\n",
		       cpu, pcpu, cpu_status);
	}

	/* Isolation and deallocation are definatly done by
	 * drslot_chrp_cpu.  If they were not they would be
	 * done here.  Change isolate state to Isolate and
	 * change allocation-state to Unusable.
	 */
	paca[cpu].cpu_start = 0;
}

/*
 * Update cpu_present_map and paca(s) for a new cpu node.  The wrinkle
 * here is that a cpu device node may represent up to two logical cpus
 * in the SMT case.  We must honor the assumption in other code that
 * the logical ids for sibling SMT threads x and y are adjacent, such
 * that x^1 == y and y^1 == x.
 */
static int pseries_add_processor(struct device_node *np)
{
	unsigned int cpu;
	cpumask_t candidate_map, tmp = CPU_MASK_NONE;
	int err = -ENOSPC, len, nthreads, i;
	const u32 *intserv;

	intserv = of_get_property(np, "ibm,ppc-interrupt-server#s", &len);
	if (!intserv)
		return 0;

	nthreads = len / sizeof(u32);
	for (i = 0; i < nthreads; i++)
		cpu_set(i, tmp);

	lock_cpu_hotplug();

	BUG_ON(!cpus_subset(cpu_present_map, cpu_possible_map));

	/* Get a bitmap of unoccupied slots. */
	cpus_xor(candidate_map, cpu_possible_map, cpu_present_map);
	if (cpus_empty(candidate_map)) {
		/* If we get here, it most likely means that NR_CPUS is
		 * less than the partition's max processors setting.
		 */
		printk(KERN_ERR "Cannot add cpu %s; this system configuration"
		       " supports %d logical cpus.\n", np->full_name,
		       cpus_weight(cpu_possible_map));
		goto out_unlock;
	}

	while (!cpus_empty(tmp))
		if (cpus_subset(tmp, candidate_map))
			/* Found a range where we can insert the new cpu(s) */
			break;
		else
			cpus_shift_left(tmp, tmp, nthreads);

	if (cpus_empty(tmp)) {
		printk(KERN_ERR "Unable to find space in cpu_present_map for"
		       " processor %s with %d thread(s)\n", np->name,
		       nthreads);
		goto out_unlock;
	}

	for_each_cpu_mask(cpu, tmp) {
		BUG_ON(cpu_isset(cpu, cpu_present_map));
		cpu_set(cpu, cpu_present_map);
		set_hard_smp_processor_id(cpu, *intserv++);
	}
	err = 0;
out_unlock:
	unlock_cpu_hotplug();
	return err;
}

/*
 * Update the present map for a cpu node which is going away, and set
 * the hard id in the paca(s) to -1 to be consistent with boot time
 * convention for non-present cpus.
 */
static void pseries_remove_processor(struct device_node *np)
{
	unsigned int cpu;
	int len, nthreads, i;
	const u32 *intserv;

	intserv = of_get_property(np, "ibm,ppc-interrupt-server#s", &len);
	if (!intserv)
		return;

	nthreads = len / sizeof(u32);

	lock_cpu_hotplug();
	for (i = 0; i < nthreads; i++) {
		for_each_present_cpu(cpu) {
			if (get_hard_smp_processor_id(cpu) != intserv[i])
				continue;
			BUG_ON(cpu_online(cpu));
			cpu_clear(cpu, cpu_present_map);
			set_hard_smp_processor_id(cpu, -1);
			break;
		}
		if (cpu == NR_CPUS)
			printk(KERN_WARNING "Could not find cpu to remove "
			       "with physical id 0x%x\n", intserv[i]);
	}
	unlock_cpu_hotplug();
}

static int pseries_smp_notifier(struct notifier_block *nb,
				unsigned long action, void *node)
{
	int err = NOTIFY_OK;

	switch (action) {
	case PSERIES_RECONFIG_ADD:
		if (pseries_add_processor(node))
			err = NOTIFY_BAD;
		break;
	case PSERIES_RECONFIG_REMOVE:
		pseries_remove_processor(node);
		break;
	default:
		err = NOTIFY_DONE;
		break;
	}
	return err;
}

static struct notifier_block pseries_smp_nb = {
	.notifier_call = pseries_smp_notifier,
};

static int __init pseries_cpu_hotplug_init(void)
{
	struct device_node *np;
	const char *typep;

	for_each_node_by_name(np, "interrupt-controller") {
		typep = of_get_property(np, "compatible", NULL);
		if (strstr(typep, "open-pic")) {
			of_node_put(np);

			printk(KERN_INFO "CPU Hotplug not supported on "
				"systems using MPIC\n");
			return 0;
		}
	}

	rtas_stop_self_args.token = rtas_token("stop-self");
	qcss_tok = rtas_token("query-cpu-stopped-state");

	if (rtas_stop_self_args.token == RTAS_UNKNOWN_SERVICE ||
			qcss_tok == RTAS_UNKNOWN_SERVICE) {
		printk(KERN_INFO "CPU Hotplug not supported by firmware "
				"- disabling.\n");
		return 0;
	}

	ppc_md.cpu_die = pseries_mach_cpu_die;
	smp_ops->cpu_disable = pseries_cpu_disable;
	smp_ops->cpu_die = pseries_cpu_die;

	/* Processors can be added/removed only on LPAR */
	if (firmware_has_feature(FW_FEATURE_LPAR))
		pSeries_reconfig_notifier_register(&pseries_smp_nb);

	return 0;
}
arch_initcall(pseries_cpu_hotplug_init);
