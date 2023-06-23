// SPDX-License-Identifier: GPL-2.0-only
/*
 * SMP initialisation and IPI support
 * Based on arch/arm64/kernel/smp.c
 *
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2015 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#include <linux/arch_topology.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/percpu.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/mm.h>
#include <asm/cpu_ops.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/numa.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/sbi.h>
#include <asm/smp.h>

#include "head.h"

static DECLARE_COMPLETION(cpu_running);

void __init smp_prepare_boot_cpu(void)
{
	init_cpu_topology();
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	int cpuid;
	int ret;
	unsigned int curr_cpuid;

	curr_cpuid = smp_processor_id();
	numa_store_cpu_info(curr_cpuid);
	numa_add_cpu(curr_cpuid);

	/* This covers non-smp usecase mandated by "nosmp" option */
	if (max_cpus == 0)
		return;

	for_each_possible_cpu(cpuid) {
		if (cpuid == curr_cpuid)
			continue;
		if (cpu_ops[cpuid]->cpu_prepare) {
			ret = cpu_ops[cpuid]->cpu_prepare(cpuid);
			if (ret)
				continue;
		}
		set_cpu_present(cpuid, true);
		numa_store_cpu_info(cpuid);
	}
}

void __init setup_smp(void)
{
	struct device_node *dn;
	unsigned long hart;
	bool found_boot_cpu = false;
	int cpuid = 1;
	int rc;

	cpu_set_ops(0);

	for_each_of_cpu_node(dn) {
		rc = riscv_of_processor_hartid(dn, &hart);
		if (rc < 0)
			continue;

		if (hart == cpuid_to_hartid_map(0)) {
			BUG_ON(found_boot_cpu);
			found_boot_cpu = 1;
			early_map_cpu_to_node(0, of_node_to_nid(dn));
			continue;
		}
		if (cpuid >= NR_CPUS) {
			pr_warn("Invalid cpuid [%d] for hartid [%lu]\n",
				cpuid, hart);
			continue;
		}

		cpuid_to_hartid_map(cpuid) = hart;
		early_map_cpu_to_node(cpuid, of_node_to_nid(dn));
		cpuid++;
	}

	BUG_ON(!found_boot_cpu);

	if (cpuid > nr_cpu_ids)
		pr_warn("Total number of cpus [%d] is greater than nr_cpus option value [%d]\n",
			cpuid, nr_cpu_ids);

	for (cpuid = 1; cpuid < nr_cpu_ids; cpuid++) {
		if (cpuid_to_hartid_map(cpuid) != INVALID_HARTID) {
			cpu_set_ops(cpuid);
			set_cpu_possible(cpuid, true);
		}
	}
}

static int start_secondary_cpu(int cpu, struct task_struct *tidle)
{
	if (cpu_ops[cpu]->cpu_start)
		return cpu_ops[cpu]->cpu_start(cpu, tidle);

	return -EOPNOTSUPP;
}

int __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	int ret = 0;
	tidle->thread_info.cpu = cpu;

	ret = start_secondary_cpu(cpu, tidle);
	if (!ret) {
		wait_for_completion_timeout(&cpu_running,
					    msecs_to_jiffies(1000));

		if (!cpu_online(cpu)) {
			pr_crit("CPU%u: failed to come online\n", cpu);
			ret = -EIO;
		}
	} else {
		pr_crit("CPU%u: failed to start\n", cpu);
	}

	return ret;
}

void __init smp_cpus_done(unsigned int max_cpus)
{
}

/*
 * C entry point for a secondary processor.
 */
asmlinkage __visible void smp_callin(void)
{
	struct mm_struct *mm = &init_mm;
	unsigned int curr_cpuid = smp_processor_id();

	riscv_clear_ipi();

	/* All kernel threads share the same mm context.  */
	mmgrab(mm);
	current->active_mm = mm;

	notify_cpu_starting(curr_cpuid);
	numa_add_cpu(curr_cpuid);
	update_siblings_masks(curr_cpuid);
	set_cpu_online(curr_cpuid, 1);

	/*
	 * Remote TLB flushes are ignored while the CPU is offline, so emit
	 * a local TLB flush right now just in case.
	 */
	local_flush_tlb_all();
	complete(&cpu_running);
	/*
	 * Disable preemption before enabling interrupts, so we don't try to
	 * schedule a CPU that hasn't actually started yet.
	 */
	local_irq_enable();
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}
