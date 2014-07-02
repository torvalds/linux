/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2013 Cavium, Inc.
 */

#include <linux/interrupt.h>
#include <linux/cpumask.h>
#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/mipsregs.h>
#include <asm/setup.h>
#include <asm/time.h>
#include <asm/smp.h>

/*
 * Writing the sp releases the CPU, so writes must be ordered, gp
 * first, then sp.
 */
unsigned long paravirt_smp_sp[NR_CPUS];
unsigned long paravirt_smp_gp[NR_CPUS];

static int numcpus = 1;

static int __init set_numcpus(char *str)
{
	int newval;

	if (get_option(&str, &newval)) {
		if (newval < 1 || newval >= NR_CPUS)
			goto bad;
		numcpus = newval;
		return 0;
	}
bad:
	return -EINVAL;
}
early_param("numcpus", set_numcpus);


static void paravirt_smp_setup(void)
{
	int id;
	unsigned int cpunum = get_ebase_cpunum();

	if (WARN_ON(cpunum >= NR_CPUS))
		return;

	/* The present CPUs are initially just the boot cpu (CPU 0). */
	for (id = 0; id < NR_CPUS; id++) {
		set_cpu_possible(id, id == 0);
		set_cpu_present(id, id == 0);
	}
	__cpu_number_map[cpunum] = 0;
	__cpu_logical_map[0] = cpunum;

	for (id = 0; id < numcpus; id++) {
		set_cpu_possible(id, true);
		set_cpu_present(id, true);
		__cpu_number_map[id] = id;
		__cpu_logical_map[id] = id;
	}
}

void irq_mbox_ipi(int cpu, unsigned int actions);
static void paravirt_send_ipi_single(int cpu, unsigned int action)
{
	irq_mbox_ipi(cpu, action);
}

static void paravirt_send_ipi_mask(const struct cpumask *mask, unsigned int action)
{
	unsigned int cpu;

	for_each_cpu_mask(cpu, *mask)
		paravirt_send_ipi_single(cpu, action);
}

static void paravirt_init_secondary(void)
{
	unsigned int sr;

	sr = set_c0_status(ST0_BEV);
	write_c0_ebase((u32)ebase);

	sr |= STATUSF_IP2; /* Interrupt controller on IP2 */
	write_c0_status(sr);

	irq_cpu_online();
}

static void paravirt_smp_finish(void)
{
	/* to generate the first CPU timer interrupt */
	write_c0_compare(read_c0_count() + mips_hpt_frequency / HZ);
	local_irq_enable();
}

static void paravirt_boot_secondary(int cpu, struct task_struct *idle)
{
	paravirt_smp_gp[cpu] = (unsigned long)task_thread_info(idle);
	smp_wmb();
	paravirt_smp_sp[cpu] = __KSTK_TOS(idle);
}

static irqreturn_t paravirt_reched_interrupt(int irq, void *dev_id)
{
	scheduler_ipi();
	return IRQ_HANDLED;
}

static irqreturn_t paravirt_function_interrupt(int irq, void *dev_id)
{
	smp_call_function_interrupt();
	return IRQ_HANDLED;
}

static void paravirt_prepare_cpus(unsigned int max_cpus)
{
	if (request_irq(MIPS_IRQ_MBOX0, paravirt_reched_interrupt,
			IRQF_PERCPU | IRQF_NO_THREAD, "Scheduler",
			paravirt_reched_interrupt)) {
		panic("Cannot request_irq for SchedulerIPI");
	}
	if (request_irq(MIPS_IRQ_MBOX1, paravirt_function_interrupt,
			IRQF_PERCPU | IRQF_NO_THREAD, "SMP-Call",
			paravirt_function_interrupt)) {
		panic("Cannot request_irq for SMP-Call");
	}
}

struct plat_smp_ops paravirt_smp_ops = {
	.send_ipi_single	= paravirt_send_ipi_single,
	.send_ipi_mask		= paravirt_send_ipi_mask,
	.init_secondary		= paravirt_init_secondary,
	.smp_finish		= paravirt_smp_finish,
	.boot_secondary		= paravirt_boot_secondary,
	.smp_setup		= paravirt_smp_setup,
	.prepare_cpus		= paravirt_prepare_cpus,
};
