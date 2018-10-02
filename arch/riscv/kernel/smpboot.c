/*
 * SMP initialisation and IPI support
 * Based on arch/arm64/kernel/smp.c
 *
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2015 Regents of the University of California
 * Copyright (C) 2017 SiFive
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/sbi.h>

void *__cpu_up_stack_pointer[NR_CPUS];
void *__cpu_up_task_pointer[NR_CPUS];

void __init smp_prepare_boot_cpu(void)
{
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
}

void __init setup_smp(void)
{
	struct device_node *dn = NULL;
	int hart, im_okay_therefore_i_am = 0;

	while ((dn = of_find_node_by_type(dn, "cpu"))) {
		hart = riscv_of_processor_hart(dn);
		if (hart >= 0) {
			set_cpu_possible(hart, true);
			set_cpu_present(hart, true);
			if (hart == smp_processor_id()) {
				BUG_ON(im_okay_therefore_i_am);
				im_okay_therefore_i_am = 1;
			}
		}
	}

	BUG_ON(!im_okay_therefore_i_am);
}

int __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	tidle->thread_info.cpu = cpu;

	/*
	 * On RISC-V systems, all harts boot on their own accord.  Our _start
	 * selects the first hart to boot the kernel and causes the remainder
	 * of the harts to spin in a loop waiting for their stack pointer to be
	 * setup by that main hart.  Writing __cpu_up_stack_pointer signals to
	 * the spinning harts that they can continue the boot process.
	 */
	smp_mb();
	__cpu_up_stack_pointer[cpu] = task_stack_page(tidle) + THREAD_SIZE;
	__cpu_up_task_pointer[cpu] = tidle;

	while (!cpu_online(cpu))
		cpu_relax();

	return 0;
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
	atomic_inc(&mm->mm_count);
	current->active_mm = mm;

	trap_init();
	notify_cpu_starting(smp_processor_id());
	set_cpu_online(smp_processor_id(), 1);
	/*
	 * Remote TLB flushes are ignored while the CPU is offline, so emit
	 * a local TLB flush right now just in case.
	 */
	local_flush_tlb_all();
	/*
	 * Disable preemption before enabling interrupts, so we don't try to
	 * schedule a CPU that hasn't actually started yet.
	 */
	preempt_disable();
	local_irq_enable();
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}
