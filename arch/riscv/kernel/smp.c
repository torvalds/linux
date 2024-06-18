// SPDX-License-Identifier: GPL-2.0-only
/*
 * SMP initialisation and IPI support
 * Based on arch/arm64/kernel/smp.c
 *
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2015 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#include <linux/cpu.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kexec.h>
#include <linux/percpu.h>
#include <linux/profile.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/irq_work.h>

#include <asm/tlbflush.h>
#include <asm/cacheflush.h>
#include <asm/cpu_ops.h>

enum ipi_message_type {
	IPI_RESCHEDULE,
	IPI_CALL_FUNC,
	IPI_CPU_STOP,
	IPI_CPU_CRASH_STOP,
	IPI_IRQ_WORK,
	IPI_TIMER,
	IPI_MAX
};

unsigned long __cpuid_to_hartid_map[NR_CPUS] __ro_after_init = {
	[0 ... NR_CPUS-1] = INVALID_HARTID
};

void __init smp_setup_processor_id(void)
{
	cpuid_to_hartid_map(0) = boot_cpu_hartid;
}

static DEFINE_PER_CPU_READ_MOSTLY(int, ipi_dummy_dev);
static int ipi_virq_base __ro_after_init;
static int nr_ipi __ro_after_init = IPI_MAX;
static struct irq_desc *ipi_desc[IPI_MAX] __read_mostly;

int riscv_hartid_to_cpuid(unsigned long hartid)
{
	int i;

	for (i = 0; i < NR_CPUS; i++)
		if (cpuid_to_hartid_map(i) == hartid)
			return i;

	return -ENOENT;
}

static void ipi_stop(void)
{
	set_cpu_online(smp_processor_id(), false);
	while (1)
		wait_for_interrupt();
}

#ifdef CONFIG_KEXEC_CORE
static atomic_t waiting_for_crash_ipi = ATOMIC_INIT(0);

static inline void ipi_cpu_crash_stop(unsigned int cpu, struct pt_regs *regs)
{
	crash_save_cpu(regs, cpu);

	atomic_dec(&waiting_for_crash_ipi);

	local_irq_disable();

#ifdef CONFIG_HOTPLUG_CPU
	if (cpu_has_hotplug(cpu))
		cpu_ops->cpu_stop();
#endif

	for(;;)
		wait_for_interrupt();
}
#else
static inline void ipi_cpu_crash_stop(unsigned int cpu, struct pt_regs *regs)
{
	unreachable();
}
#endif

static void send_ipi_mask(const struct cpumask *mask, enum ipi_message_type op)
{
	__ipi_send_mask(ipi_desc[op], mask);
}

static void send_ipi_single(int cpu, enum ipi_message_type op)
{
	__ipi_send_mask(ipi_desc[op], cpumask_of(cpu));
}

#ifdef CONFIG_IRQ_WORK
void arch_irq_work_raise(void)
{
	send_ipi_single(smp_processor_id(), IPI_IRQ_WORK);
}
#endif

static irqreturn_t handle_IPI(int irq, void *data)
{
	int ipi = irq - ipi_virq_base;

	switch (ipi) {
	case IPI_RESCHEDULE:
		scheduler_ipi();
		break;
	case IPI_CALL_FUNC:
		generic_smp_call_function_interrupt();
		break;
	case IPI_CPU_STOP:
		ipi_stop();
		break;
	case IPI_CPU_CRASH_STOP:
		ipi_cpu_crash_stop(smp_processor_id(), get_irq_regs());
		break;
	case IPI_IRQ_WORK:
		irq_work_run();
		break;
#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
	case IPI_TIMER:
		tick_receive_broadcast();
		break;
#endif
	default:
		pr_warn("CPU%d: unhandled IPI%d\n", smp_processor_id(), ipi);
		break;
	}

	return IRQ_HANDLED;
}

void riscv_ipi_enable(void)
{
	int i;

	if (WARN_ON_ONCE(!ipi_virq_base))
		return;

	for (i = 0; i < nr_ipi; i++)
		enable_percpu_irq(ipi_virq_base + i, 0);
}

void riscv_ipi_disable(void)
{
	int i;

	if (WARN_ON_ONCE(!ipi_virq_base))
		return;

	for (i = 0; i < nr_ipi; i++)
		disable_percpu_irq(ipi_virq_base + i);
}

bool riscv_ipi_have_virq_range(void)
{
	return (ipi_virq_base) ? true : false;
}

void riscv_ipi_set_virq_range(int virq, int nr)
{
	int i, err;

	if (WARN_ON(ipi_virq_base))
		return;

	WARN_ON(nr < IPI_MAX);
	nr_ipi = min(nr, IPI_MAX);
	ipi_virq_base = virq;

	/* Request IPIs */
	for (i = 0; i < nr_ipi; i++) {
		err = request_percpu_irq(ipi_virq_base + i, handle_IPI,
					 "IPI", &ipi_dummy_dev);
		WARN_ON(err);

		ipi_desc[i] = irq_to_desc(ipi_virq_base + i);
		irq_set_status_flags(ipi_virq_base + i, IRQ_HIDDEN);
	}

	/* Enabled IPIs for boot CPU immediately */
	riscv_ipi_enable();
}

static const char * const ipi_names[] = {
	[IPI_RESCHEDULE]	= "Rescheduling interrupts",
	[IPI_CALL_FUNC]		= "Function call interrupts",
	[IPI_CPU_STOP]		= "CPU stop interrupts",
	[IPI_CPU_CRASH_STOP]	= "CPU stop (for crash dump) interrupts",
	[IPI_IRQ_WORK]		= "IRQ work interrupts",
	[IPI_TIMER]		= "Timer broadcast interrupts",
};

void show_ipi_stats(struct seq_file *p, int prec)
{
	unsigned int cpu, i;

	for (i = 0; i < IPI_MAX; i++) {
		seq_printf(p, "%*s%u:%s", prec - 1, "IPI", i,
			   prec >= 4 ? " " : "");
		for_each_online_cpu(cpu)
			seq_printf(p, "%10u ", irq_desc_kstat_cpu(ipi_desc[i], cpu));
		seq_printf(p, " %s\n", ipi_names[i]);
	}
}

void arch_send_call_function_ipi_mask(struct cpumask *mask)
{
	send_ipi_mask(mask, IPI_CALL_FUNC);
}

void arch_send_call_function_single_ipi(int cpu)
{
	send_ipi_single(cpu, IPI_CALL_FUNC);
}

#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
void tick_broadcast(const struct cpumask *mask)
{
	send_ipi_mask(mask, IPI_TIMER);
}
#endif

void smp_send_stop(void)
{
	unsigned long timeout;

	if (num_online_cpus() > 1) {
		cpumask_t mask;

		cpumask_copy(&mask, cpu_online_mask);
		cpumask_clear_cpu(smp_processor_id(), &mask);

		if (system_state <= SYSTEM_RUNNING)
			pr_crit("SMP: stopping secondary CPUs\n");
		send_ipi_mask(&mask, IPI_CPU_STOP);
	}

	/* Wait up to one second for other CPUs to stop */
	timeout = USEC_PER_SEC;
	while (num_online_cpus() > 1 && timeout--)
		udelay(1);

	if (num_online_cpus() > 1)
		pr_warn("SMP: failed to stop secondary CPUs %*pbl\n",
			   cpumask_pr_args(cpu_online_mask));
}

#ifdef CONFIG_KEXEC_CORE
/*
 * The number of CPUs online, not counting this CPU (which may not be
 * fully online and so not counted in num_online_cpus()).
 */
static inline unsigned int num_other_online_cpus(void)
{
	unsigned int this_cpu_online = cpu_online(smp_processor_id());

	return num_online_cpus() - this_cpu_online;
}

void crash_smp_send_stop(void)
{
	static int cpus_stopped;
	cpumask_t mask;
	unsigned long timeout;

	/*
	 * This function can be called twice in panic path, but obviously
	 * we execute this only once.
	 */
	if (cpus_stopped)
		return;

	cpus_stopped = 1;

	/*
	 * If this cpu is the only one alive at this point in time, online or
	 * not, there are no stop messages to be sent around, so just back out.
	 */
	if (num_other_online_cpus() == 0)
		return;

	cpumask_copy(&mask, cpu_online_mask);
	cpumask_clear_cpu(smp_processor_id(), &mask);

	atomic_set(&waiting_for_crash_ipi, num_other_online_cpus());

	pr_crit("SMP: stopping secondary CPUs\n");
	send_ipi_mask(&mask, IPI_CPU_CRASH_STOP);

	/* Wait up to one second for other CPUs to stop */
	timeout = USEC_PER_SEC;
	while ((atomic_read(&waiting_for_crash_ipi) > 0) && timeout--)
		udelay(1);

	if (atomic_read(&waiting_for_crash_ipi) > 0)
		pr_warn("SMP: failed to stop secondary CPUs %*pbl\n",
			cpumask_pr_args(&mask));
}

bool smp_crash_stop_failed(void)
{
	return (atomic_read(&waiting_for_crash_ipi) > 0);
}
#endif

void arch_smp_send_reschedule(int cpu)
{
	send_ipi_single(cpu, IPI_RESCHEDULE);
}
EXPORT_SYMBOL_GPL(arch_smp_send_reschedule);
