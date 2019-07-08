// SPDX-License-Identifier: GPL-2.0-only
/*
 * SMP initialisation and IPI support
 * Based on arch/arm64/kernel/smp.c
 *
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2015 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

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
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/sbi.h>

void *__cpu_up_stack_pointer[NR_CPUS];
void *__cpu_up_task_pointer[NR_CPUS];
static DECLARE_COMPLETION(cpu_running);

void __init smp_prepare_boot_cpu(void)
{
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	int cpuid;

	/* This covers non-smp usecase mandated by "nosmp" option */
	if (max_cpus == 0)
		return;

	for_each_possible_cpu(cpuid) {
		if (cpuid == smp_processor_id())
			continue;
		set_cpu_present(cpuid, true);
	}
}

void __init setup_smp(void)
{
	struct device_node *dn;
	int hart;
	bool found_boot_cpu = false;
	int cpuid = 1;

	for_each_of_cpu_node(dn) {
		hart = riscv_of_processor_hartid(dn);
		if (hart < 0)
			continue;

		if (hart == cpuid_to_hartid_map(0)) {
			BUG_ON(found_boot_cpu);
			found_boot_cpu = 1;
			continue;
		}
		if (cpuid >= NR_CPUS) {
			pr_warn("Invalid cpuid [%d] for hartid [%d]\n",
				cpuid, hart);
			break;
		}

		cpuid_to_hartid_map(cpuid) = hart;
		cpuid++;
	}

	BUG_ON(!found_boot_cpu);

	if (cpuid > nr_cpu_ids)
		pr_warn("Total number of cpus [%d] is greater than nr_cpus option value [%d]\n",
			cpuid, nr_cpu_ids);

	for (cpuid = 1; cpuid < nr_cpu_ids; cpuid++) {
		if (cpuid_to_hartid_map(cpuid) != INVALID_HARTID)
			set_cpu_possible(cpuid, true);
	}
}

int __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	int ret = 0;
	int hartid = cpuid_to_hartid_map(cpu);
	tidle->thread_info.cpu = cpu;

	/*
	 * On RISC-V systems, all harts boot on their own accord.  Our _start
	 * selects the first hart to boot the kernel and causes the remainder
	 * of the harts to spin in a loop waiting for their stack pointer to be
	 * setup by that main hart.  Writing __cpu_up_stack_pointer signals to
	 * the spinning harts that they can continue the boot process.
	 */
	smp_mb();
	WRITE_ONCE(__cpu_up_stack_pointer[hartid],
		  task_stack_page(tidle) + THREAD_SIZE);
	WRITE_ONCE(__cpu_up_task_pointer[hartid], tidle);

	lockdep_assert_held(&cpu_running);
	wait_for_completion_timeout(&cpu_running,
					    msecs_to_jiffies(1000));

	if (!cpu_online(cpu)) {
		pr_crit("CPU%u: failed to come online\n", cpu);
		ret = -EIO;
	}

	return ret;
}

void __init smp_cpus_done(unsigned int max_cpus)
{
}

/*
 * C entry point for a secondary processor.
 */
asmlinkage void __init smp_callin(void)
{
	struct mm_struct *mm = &init_mm;

	/* All kernel threads share the same mm context.  */
	mmgrab(mm);
	current->active_mm = mm;

	trap_init();
	notify_cpu_starting(smp_processor_id());
	set_cpu_online(smp_processor_id(), 1);
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
	preempt_disable();
	local_irq_enable();
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}
