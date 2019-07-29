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
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/mm.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/sections.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>

struct ipi_data_struct {
	unsigned long bits ____cacheline_aligned;
};
static DEFINE_PER_CPU(struct ipi_data_struct, ipi_data);

enum ipi_message_type {
	IPI_EMPTY,
	IPI_RESCHEDULE,
	IPI_CALL_FUNC,
	IPI_MAX
};

static irqreturn_t handle_ipi(int irq, void *dev)
{
	while (true) {
		unsigned long ops;

		ops = xchg(&this_cpu_ptr(&ipi_data)->bits, 0);
		if (ops == 0)
			return IRQ_HANDLED;

		if (ops & (1 << IPI_RESCHEDULE))
			scheduler_ipi();

		if (ops & (1 << IPI_CALL_FUNC))
			generic_smp_call_function_interrupt();

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

void smp_send_reschedule(int cpu)
{
	send_ipi_message(cpumask_of(cpu), IPI_RESCHEDULE);
}

void __init smp_prepare_boot_cpu(void)
{
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
}

static void __init enable_smp_ipi(void)
{
	enable_percpu_irq(ipi_irq, 0);
}

static int ipi_dummy_dev;
void __init setup_smp_ipi(void)
{
	int rc;

	if (ipi_irq == 0)
		panic("%s IRQ mapping failed\n", __func__);

	rc = request_percpu_irq(ipi_irq, handle_ipi, "IPI Interrupt",
				&ipi_dummy_dev);
	if (rc)
		panic("%s IRQ request failed\n", __func__);

	enable_smp_ipi();
}

void __init setup_smp(void)
{
	struct device_node *node = NULL;
	int cpu;

	while ((node = of_find_node_by_type(node, "cpu"))) {
		if (!of_device_is_available(node))
			continue;

		if (of_property_read_u32(node, "reg", &cpu))
			continue;

		if (cpu >= NR_CPUS)
			continue;

		set_cpu_possible(cpu, true);
		set_cpu_present(cpu, true);
	}
}

extern void _start_smp_secondary(void);

volatile unsigned int secondary_hint;
volatile unsigned int secondary_ccr;
volatile unsigned int secondary_stack;

int __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	unsigned int tmp;

	secondary_stack = (unsigned int)tidle->stack + THREAD_SIZE;

	secondary_hint = mfcr("cr31");

	secondary_ccr  = mfcr("cr18");

	/*
	 * Because other CPUs are in reset status, we must flush data
	 * from cache to out and secondary CPUs use them in
	 * csky_start_secondary(void)
	 */
	mtcr("cr17", 0x22);

	/* Enable cpu in SMP reset ctrl reg */
	tmp = mfcr("cr<29, 0>");
	tmp |= 1 << cpu;
	mtcr("cr<29, 0>", tmp);

	/* Wait for the cpu online */
	while (!cpu_online(cpu));

	secondary_stack = 0;

	return 0;
}

void __init smp_cpus_done(unsigned int max_cpus)
{
}

int setup_profiling_timer(unsigned int multiplier)
{
	return -EINVAL;
}

void csky_start_secondary(void)
{
	struct mm_struct *mm = &init_mm;
	unsigned int cpu = smp_processor_id();

	mtcr("cr31", secondary_hint);
	mtcr("cr18", secondary_ccr);

	mtcr("vbr", vec_base);

	flush_tlb_all();
	write_mmu_pagemask(0);
	TLBMISS_HANDLER_SETUP_PGD(swapper_pg_dir);
	TLBMISS_HANDLER_SETUP_PGD_KERNEL(swapper_pg_dir);

	asid_cache(smp_processor_id()) = ASID_FIRST_VERSION;

#ifdef CONFIG_CPU_HAS_FPU
	init_fpu();
#endif

	enable_smp_ipi();

	mmget(mm);
	mmgrab(mm);
	current->active_mm = mm;
	cpumask_set_cpu(cpu, mm_cpumask(mm));

	notify_cpu_starting(cpu);
	set_cpu_online(cpu, true);

	pr_info("CPU%u Online: %s...\n", cpu, __func__);

	local_irq_enable();
	preempt_disable();
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}
