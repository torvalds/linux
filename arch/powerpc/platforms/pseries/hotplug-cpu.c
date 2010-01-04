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
#include "plpar_wrappers.h"
#include "offline_states.h"

/* This version can't take the spinlock, because it never returns */
static struct rtas_args rtas_stop_self_args = {
	.token = RTAS_UNKNOWN_SERVICE,
	.nargs = 0,
	.nret = 1,
	.rets = &rtas_stop_self_args.args[0],
};

static DEFINE_PER_CPU(enum cpu_state_vals, preferred_offline_state) =
							CPU_STATE_OFFLINE;
static DEFINE_PER_CPU(enum cpu_state_vals, current_state) = CPU_STATE_OFFLINE;

static enum cpu_state_vals default_offline_state = CPU_STATE_OFFLINE;

static int cede_offline_enabled __read_mostly = 1;

/*
 * Enable/disable cede_offline when available.
 */
static int __init setup_cede_offline(char *str)
{
	if (!strcmp(str, "off"))
		cede_offline_enabled = 0;
	else if (!strcmp(str, "on"))
		cede_offline_enabled = 1;
	else
		return 0;
	return 1;
}

__setup("cede_offline=", setup_cede_offline);

enum cpu_state_vals get_cpu_current_state(int cpu)
{
	return per_cpu(current_state, cpu);
}

void set_cpu_current_state(int cpu, enum cpu_state_vals state)
{
	per_cpu(current_state, cpu) = state;
}

enum cpu_state_vals get_preferred_offline_state(int cpu)
{
	return per_cpu(preferred_offline_state, cpu);
}

void set_preferred_offline_state(int cpu, enum cpu_state_vals state)
{
	per_cpu(preferred_offline_state, cpu) = state;
}

void set_default_offline_state(int cpu)
{
	per_cpu(preferred_offline_state, cpu) = default_offline_state;
}

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
	unsigned int cpu = smp_processor_id();
	unsigned int hwcpu = hard_smp_processor_id();
	u8 cede_latency_hint = 0;

	local_irq_disable();
	idle_task_exit();
	xics_teardown_cpu();

	if (get_preferred_offline_state(cpu) == CPU_STATE_INACTIVE) {
		set_cpu_current_state(cpu, CPU_STATE_INACTIVE);
		cede_latency_hint = 2;

		get_lppaca()->idle = 1;
		if (!get_lppaca()->shared_proc)
			get_lppaca()->donate_dedicated_cpu = 1;

		printk(KERN_INFO
			"cpu %u (hwid %u) ceding for offline with hint %d\n",
			cpu, hwcpu, cede_latency_hint);
		while (get_preferred_offline_state(cpu) == CPU_STATE_INACTIVE) {
			extended_cede_processor(cede_latency_hint);
			printk(KERN_INFO "cpu %u (hwid %u) returned from cede.\n",
				cpu, hwcpu);
			printk(KERN_INFO
			"Decrementer value = %x Timebase value = %llx\n",
			get_dec(), get_tb());
		}

		printk(KERN_INFO "cpu %u (hwid %u) got prodded to go online\n",
			cpu, hwcpu);

		if (!get_lppaca()->shared_proc)
			get_lppaca()->donate_dedicated_cpu = 0;
		get_lppaca()->idle = 0;
	}

	if (get_preferred_offline_state(cpu) == CPU_STATE_ONLINE) {
		unregister_slb_shadow(hwcpu, __pa(get_slb_shadow()));

		/*
		 * NOTE: Calling start_secondary() here for now to
		 * start new context.
		 * However, need to do it cleanly by resetting the
		 * stack pointer.
		 */
		start_secondary();

	} else if (get_preferred_offline_state(cpu) == CPU_STATE_OFFLINE) {

		set_cpu_current_state(cpu, CPU_STATE_OFFLINE);
		unregister_slb_shadow(hard_smp_processor_id(),
					__pa(get_slb_shadow()));
		rtas_stop_self();
	}

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

	set_cpu_online(cpu, false);
	vdso_data->processorCount--;

	/*fix boot_cpuid here*/
	if (cpu == boot_cpuid)
		boot_cpuid = any_online_cpu(cpu_online_map);

	/* FIXME: abstract this to not be platform specific later on */
	xics_migrate_irqs_away();
	return 0;
}

/*
 * pseries_cpu_die: Wait for the cpu to die.
 * @cpu: logical processor id of the CPU whose death we're awaiting.
 *
 * This function is called from the context of the thread which is performing
 * the cpu-offline. Here we wait for long enough to allow the cpu in question
 * to self-destroy so that the cpu-offline thread can send the CPU_DEAD
 * notifications.
 *
 * OTOH, pseries_mach_cpu_die() is called by the @cpu when it wants to
 * self-destruct.
 */
static void pseries_cpu_die(unsigned int cpu)
{
	int tries;
	int cpu_status = 1;
	unsigned int pcpu = get_hard_smp_processor_id(cpu);

	if (get_preferred_offline_state(cpu) == CPU_STATE_INACTIVE) {
		cpu_status = 1;
		for (tries = 0; tries < 1000; tries++) {
			if (get_cpu_current_state(cpu) == CPU_STATE_INACTIVE) {
				cpu_status = 0;
				break;
			}
			cpu_relax();
		}
	} else if (get_preferred_offline_state(cpu) == CPU_STATE_OFFLINE) {

		for (tries = 0; tries < 25; tries++) {
			cpu_status = query_cpu_stopped(pcpu);
			if (cpu_status == 0 || cpu_status == -1)
				break;
			cpu_relax();
		}
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

	cpu_maps_update_begin();

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
		set_cpu_present(cpu, true);
		set_hard_smp_processor_id(cpu, *intserv++);
	}
	err = 0;
out_unlock:
	cpu_maps_update_done();
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

	cpu_maps_update_begin();
	for (i = 0; i < nthreads; i++) {
		for_each_present_cpu(cpu) {
			if (get_hard_smp_processor_id(cpu) != intserv[i])
				continue;
			BUG_ON(cpu_online(cpu));
			set_cpu_present(cpu, false);
			set_hard_smp_processor_id(cpu, -1);
			break;
		}
		if (cpu == NR_CPUS)
			printk(KERN_WARNING "Could not find cpu to remove "
			       "with physical id 0x%x\n", intserv[i]);
	}
	cpu_maps_update_done();
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

#define MAX_CEDE_LATENCY_LEVELS		4
#define	CEDE_LATENCY_PARAM_LENGTH	10
#define CEDE_LATENCY_PARAM_MAX_LENGTH	\
	(MAX_CEDE_LATENCY_LEVELS * CEDE_LATENCY_PARAM_LENGTH * sizeof(char))
#define CEDE_LATENCY_TOKEN		45

static char cede_parameters[CEDE_LATENCY_PARAM_MAX_LENGTH];

static int parse_cede_parameters(void)
{
	int call_status;

	memset(cede_parameters, 0, CEDE_LATENCY_PARAM_MAX_LENGTH);
	call_status = rtas_call(rtas_token("ibm,get-system-parameter"), 3, 1,
				NULL,
				CEDE_LATENCY_TOKEN,
				__pa(cede_parameters),
				CEDE_LATENCY_PARAM_MAX_LENGTH);

	if (call_status != 0)
		printk(KERN_INFO "CEDE_LATENCY: \
			%s %s Error calling get-system-parameter(0x%x)\n",
			__FILE__, __func__, call_status);
	else
		printk(KERN_INFO "CEDE_LATENCY: \
			get-system-parameter successful.\n");

	return call_status;
}

static int __init pseries_cpu_hotplug_init(void)
{
	struct device_node *np;
	const char *typep;
	int cpu;

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
	if (firmware_has_feature(FW_FEATURE_LPAR)) {
		pSeries_reconfig_notifier_register(&pseries_smp_nb);
		cpu_maps_update_begin();
		if (cede_offline_enabled && parse_cede_parameters() == 0) {
			default_offline_state = CPU_STATE_INACTIVE;
			for_each_online_cpu(cpu)
				set_default_offline_state(cpu);
		}
		cpu_maps_update_done();
	}

	return 0;
}
arch_initcall(pseries_cpu_hotplug_init);
