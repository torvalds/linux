// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Ant Group
 * Author: Tiwei Bie <tiwei.btw@antgroup.com>
 *
 * Based on the previous implementation in TT mode
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/module.h>
#include <linux/processor.h>
#include <linux/threads.h>
#include <linux/cpu.h>
#include <linux/hardirq.h>
#include <linux/smp.h>
#include <linux/smp-internal.h>
#include <init.h>
#include <kern.h>
#include <os.h>
#include <smp.h>

enum {
	UML_IPI_RES = 0,
	UML_IPI_CALL_SINGLE,
	UML_IPI_CALL,
	UML_IPI_STOP,
};

void arch_smp_send_reschedule(int cpu)
{
	os_send_ipi(cpu, UML_IPI_RES);
}

void arch_send_call_function_single_ipi(int cpu)
{
	os_send_ipi(cpu, UML_IPI_CALL_SINGLE);
}

void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	int cpu;

	for_each_cpu(cpu, mask)
		os_send_ipi(cpu, UML_IPI_CALL);
}

void smp_send_stop(void)
{
	int cpu, me = smp_processor_id();

	for_each_online_cpu(cpu) {
		if (cpu == me)
			continue;
		os_send_ipi(cpu, UML_IPI_STOP);
	}
}

static void ipi_handler(int vector, struct uml_pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs((struct pt_regs *)regs);
	int cpu = raw_smp_processor_id();

	irq_enter();

	if (current->mm)
		os_alarm_process(current->mm->context.id.pid);

	switch (vector) {
	case UML_IPI_RES:
		inc_irq_stat(irq_resched_count);
		scheduler_ipi();
		break;

	case UML_IPI_CALL_SINGLE:
		inc_irq_stat(irq_call_count);
		generic_smp_call_function_single_interrupt();
		break;

	case UML_IPI_CALL:
		inc_irq_stat(irq_call_count);
		generic_smp_call_function_interrupt();
		break;

	case UML_IPI_STOP:
		set_cpu_online(cpu, false);
		while (1)
			pause();
		break;

	default:
		pr_err("CPU#%d received unknown IPI (vector=%d)!\n", cpu, vector);
		break;
	}

	irq_exit();
	set_irq_regs(old_regs);
}

void uml_ipi_handler(int vector)
{
	struct uml_pt_regs r = { .is_user = 0 };

	preempt_disable();
	ipi_handler(vector, &r);
	preempt_enable();
}

/* AP states used only during CPU startup */
enum {
	UML_CPU_PAUSED = 0,
	UML_CPU_RUNNING,
};

static int cpu_states[NR_CPUS];

static int start_secondary(void *unused)
{
	int err, cpu = raw_smp_processor_id();

	notify_cpu_starting(cpu);
	set_cpu_online(cpu, true);

	err = um_setup_timer();
	if (err)
		panic("CPU#%d failed to setup timer, err = %d", cpu, err);

	local_irq_enable();

	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);

	return 0;
}

void uml_start_secondary(void *opaque)
{
	int cpu = raw_smp_processor_id();
	struct mm_struct *mm = &init_mm;
	struct task_struct *idle;

	stack_protections((unsigned long) &cpu_irqstacks[cpu]);
	set_sigstack(&cpu_irqstacks[cpu], THREAD_SIZE);

	set_cpu_present(cpu, true);
	os_futex_wait(&cpu_states[cpu], UML_CPU_PAUSED);

	smp_rmb(); /* paired with smp_wmb() in __cpu_up() */

	idle = cpu_tasks[cpu];
	idle->thread_info.cpu = cpu;

	mmgrab(mm);
	idle->active_mm = mm;

	idle->thread.request.thread.proc = start_secondary;
	idle->thread.request.thread.arg = NULL;

	new_thread(task_stack_page(idle), &idle->thread.switch_buf,
		   new_thread_handler);
	os_start_secondary(opaque, &idle->thread.switch_buf);
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	int err, cpu, me = smp_processor_id();
	unsigned long deadline;

	os_init_smp();

	for_each_possible_cpu(cpu) {
		if (cpu == me)
			continue;

		pr_debug("Booting processor %d...\n", cpu);
		err = os_start_cpu_thread(cpu);
		if (err) {
			pr_crit("CPU#%d failed to start cpu thread, err = %d",
				cpu, err);
			continue;
		}

		deadline = jiffies + msecs_to_jiffies(1000);
		spin_until_cond(cpu_present(cpu) ||
				time_is_before_jiffies(deadline));

		if (!cpu_present(cpu))
			pr_crit("CPU#%d failed to boot\n", cpu);
	}
}

int __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	cpu_tasks[cpu] = tidle;
	smp_wmb(); /* paired with smp_rmb() in uml_start_secondary() */
	cpu_states[cpu] = UML_CPU_RUNNING;
	os_futex_wake(&cpu_states[cpu]);
	spin_until_cond(cpu_online(cpu));

	return 0;
}

void __init smp_cpus_done(unsigned int max_cpus)
{
}

/* Set in uml_ncpus_setup */
int uml_ncpus = 1;

void __init prefill_possible_map(void)
{
	int cpu;

	for (cpu = 0; cpu < uml_ncpus; cpu++)
		set_cpu_possible(cpu, true);
	for (; cpu < NR_CPUS; cpu++)
		set_cpu_possible(cpu, false);
}

static int __init uml_ncpus_setup(char *line, int *add)
{
	*add = 0;

	if (kstrtoint(line, 10, &uml_ncpus)) {
		os_warn("%s: Couldn't parse '%s'\n", __func__, line);
		return -1;
	}

	uml_ncpus = clamp(uml_ncpus, 1, NR_CPUS);

	return 0;
}

__uml_setup("ncpus=", uml_ncpus_setup,
"ncpus=<# of desired CPUs>\n"
"    This tells UML how many virtual processors to start. The maximum\n"
"    number of supported virtual processors can be obtained by querying\n"
"    the CONFIG_NR_CPUS option using --showconfig.\n\n"
);

EXPORT_SYMBOL(uml_curr_cpu);
