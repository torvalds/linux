// SPDX-License-Identifier: GPL-2.0

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
#include <linux/irq_work.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/seq_file.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/mm.h>
#include <linux/sched/hotplug.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/sections.h>
#include <asm/mmu_context.h>
#ifdef CONFIG_CPU_HAS_FPU
#include <abi/fpu.h>
#endif

enum ipi_message_type {
	IPI_EMPTY,
	IPI_RESCHEDULE,
	IPI_CALL_FUNC,
	IPI_IRQ_WORK,
	IPI_MAX
};

struct ipi_data_struct {
	unsigned long bits ____cacheline_aligned;
	unsigned long stats[IPI_MAX] ____cacheline_aligned;
};
static DEFINE_PER_CPU(struct ipi_data_struct, ipi_data);

static irqreturn_t handle_ipi(int irq, void *dev)
{
	unsigned long *stats = this_cpu_ptr(&ipi_data)->stats;

	while (true) {
		unsigned long ops;

		ops = xchg(&this_cpu_ptr(&ipi_data)->bits, 0);
		if (ops == 0)
			return IRQ_HANDLED;

		if (ops & (1 << IPI_RESCHEDULE)) {
			stats[IPI_RESCHEDULE]++;
			scheduler_ipi();
		}

		if (ops & (1 << IPI_CALL_FUNC)) {
			stats[IPI_CALL_FUNC]++;
			generic_smp_call_function_interrupt();
		}

		if (ops & (1 << IPI_IRQ_WORK)) {
			stats[IPI_IRQ_WORK]++;
			irq_work_run();
		}

		BUG_ON((ops >> IPI_MAX) != 0);
	}

	return IRQ_HANDLED;
}

static void (*send_arch_ipi)(const struct cpumask *mask);

static int ipi_irq;
void __init set_send_ipi(void (*func)(const struct cpumask *mask), int irq)
{
	if (send_arch_ipi)
		return;

	send_arch_ipi = func;
	ipi_irq = irq;
}

static void
send_ipi_message(const struct cpumask *to_whom, enum ipi_message_type operation)
{
	int i;

	for_each_cpu(i, to_whom)
		set_bit(operation, &per_cpu_ptr(&ipi_data, i)->bits);

	smp_mb();
	send_arch_ipi(to_whom);
}

static const char * const ipi_names[] = {
	[IPI_EMPTY]		= "Empty interrupts",
	[IPI_RESCHEDULE]	= "Rescheduling interrupts",
	[IPI_CALL_FUNC]		= "Function call interrupts",
	[IPI_IRQ_WORK]		= "Irq work interrupts",
};

int arch_show_interrupts(struct seq_file *p, int prec)
{
	unsigned int cpu, i;

	for (i = 0; i < IPI_MAX; i++) {
		seq_printf(p, "%*s%u:%s", prec - 1, "IPI", i,
			   prec >= 4 ? " " : "");
		for_each_online_cpu(cpu)
			seq_printf(p, "%10lu ",
				per_cpu_ptr(&ipi_data, cpu)->stats[i]);
		seq_printf(p, " %s\n", ipi_names[i]);
	}

	return 0;
}

void arch_send_call_function_ipi_mask(struct cpumask *mask)
{
	send_ipi_message(mask, IPI_CALL_FUNC);
}

void arch_send_call_function_single_ipi(int cpu)
{
	send_ipi_message(cpumask_of(cpu), IPI_CALL_FUNC);
}

static void ipi_stop(void *unused)
{
	while (1);
}

void smp_send_stop(void)
{
	on_each_cpu(ipi_stop, NULL, 1);
}

void arch_smp_send_reschedule(int cpu)
{
	send_ipi_message(cpumask_of(cpu), IPI_RESCHEDULE);
}

#ifdef CONFIG_IRQ_WORK
void arch_irq_work_raise(void)
{
	send_ipi_message(cpumask_of(smp_processor_id()), IPI_IRQ_WORK);
}
#endif

void __init smp_prepare_boot_cpu(void)
{
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
}

static int ipi_dummy_dev;

void __init setup_smp_ipi(void)
{
	int rc;

	if (ipi_irq == 0)
		return;

	rc = request_percpu_irq(ipi_irq, handle_ipi, "IPI Interrupt",
				&ipi_dummy_dev);
	if (rc)
		panic("%s IRQ request failed\n", __func__);

	enable_percpu_irq(ipi_irq, 0);
}

void __init setup_smp(void)
{
	struct device_node *node = NULL;
	unsigned int cpu;

	for_each_of_cpu_node(node) {
		if (!of_device_is_available(node))
			continue;

		cpu = of_get_cpu_hwid(node, 0);
		if (cpu >= NR_CPUS)
			continue;

		set_cpu_possible(cpu, true);
		set_cpu_present(cpu, true);
	}
}

extern void _start_smp_secondary(void);

volatile unsigned int secondary_hint;
volatile unsigned int secondary_hint2;
volatile unsigned int secondary_ccr;
volatile unsigned int secondary_stack;
volatile unsigned int secondary_msa1;
volatile unsigned int secondary_pgd;

int __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	unsigned long mask = 1 << cpu;

	secondary_stack =
		(unsigned int) task_stack_page(tidle) + THREAD_SIZE - 8;
	secondary_hint = mfcr("cr31");
	secondary_hint2 = mfcr("cr<21, 1>");
	secondary_ccr  = mfcr("cr18");
	secondary_msa1 = read_mmu_msa1();
	secondary_pgd = mfcr("cr<29, 15>");

	/*
	 * Because other CPUs are in reset status, we must flush data
	 * from cache to out and secondary CPUs use them in
	 * csky_start_secondary(void)
	 */
	mtcr("cr17", 0x22);

	if (mask & mfcr("cr<29, 0>")) {
		send_arch_ipi(cpumask_of(cpu));
	} else {
		/* Enable cpu in SMP reset ctrl reg */
		mask |= mfcr("cr<29, 0>");
		mtcr("cr<29, 0>", mask);
	}

	/* Wait for the cpu online */
	while (!cpu_online(cpu));

	secondary_stack = 0;

	return 0;
}

void __init smp_cpus_done(unsigned int max_cpus)
{
}

void csky_start_secondary(void)
{
	struct mm_struct *mm = &init_mm;
	unsigned int cpu = smp_processor_id();

	mtcr("cr31", secondary_hint);
	mtcr("cr<21, 1>", secondary_hint2);
	mtcr("cr18", secondary_ccr);

	mtcr("vbr", vec_base);

	flush_tlb_all();
	write_mmu_pagemask(0);

#ifdef CONFIG_CPU_HAS_FPU
	init_fpu();
#endif

	enable_percpu_irq(ipi_irq, 0);

	mmget(mm);
	mmgrab(mm);
	current->active_mm = mm;
	cpumask_set_cpu(cpu, mm_cpumask(mm));

	notify_cpu_starting(cpu);
	set_cpu_online(cpu, true);

	pr_info("CPU%u Online: %s...\n", cpu, __func__);

	local_irq_enable();
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}

#ifdef CONFIG_HOTPLUG_CPU
int __cpu_disable(void)
{
	unsigned int cpu = smp_processor_id();

	set_cpu_online(cpu, false);

	irq_migrate_all_off_this_cpu();

	clear_tasks_mm_cpumask(cpu);

	return 0;
}

void arch_cpuhp_cleanup_dead_cpu(unsigned int cpu)
{
	pr_notice("CPU%u: shutdown\n", cpu);
}

void __noreturn arch_cpu_idle_dead(void)
{
	idle_task_exit();

	cpuhp_ap_report_dead();

	while (!secondary_stack)
		arch_cpu_idle();

	raw_local_irq_disable();

	asm volatile(
		"mov	sp, %0\n"
		"mov	r8, %0\n"
		"jmpi	csky_start_secondary"
		:
		: "r" (secondary_stack));

	BUG();
}
#endif
