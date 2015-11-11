/*
 *  sun4m SMP support.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/profile.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/cpu.h>

#include <asm/cacheflush.h>
#include <asm/switch_to.h>
#include <asm/tlbflush.h>
#include <asm/timer.h>
#include <asm/oplib.h>

#include "irq.h"
#include "kernel.h"

#define IRQ_IPI_SINGLE		12
#define IRQ_IPI_MASK		13
#define IRQ_IPI_RESCHED		14
#define IRQ_CROSS_CALL		15

static inline unsigned long
swap_ulong(volatile unsigned long *ptr, unsigned long val)
{
	__asm__ __volatile__("swap [%1], %0\n\t" :
			     "=&r" (val), "=&r" (ptr) :
			     "0" (val), "1" (ptr));
	return val;
}

void sun4m_cpu_pre_starting(void *arg)
{
}

void sun4m_cpu_pre_online(void *arg)
{
	int cpuid = hard_smp_processor_id();

	/* Allow master to continue. The master will then give us the
	 * go-ahead by setting the smp_commenced_mask and will wait without
	 * timeouts until our setup is completed fully (signified by
	 * our bit being set in the cpu_online_mask).
	 */
	swap_ulong(&cpu_callin_map[cpuid], 1);

	/* XXX: What's up with all the flushes? */
	local_ops->cache_all();
	local_ops->tlb_all();

	/* Fix idle thread fields. */
	__asm__ __volatile__("ld [%0], %%g6\n\t"
			     : : "r" (&current_set[cpuid])
			     : "memory" /* paranoid */);

	/* Attach to the address space of init_task. */
	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;

	while (!cpumask_test_cpu(cpuid, &smp_commenced_mask))
		mb();
}

/*
 *	Cycle through the processors asking the PROM to start each one.
 */
void __init smp4m_boot_cpus(void)
{
	sun4m_unmask_profile_irq();
	local_ops->cache_all();
}

int smp4m_boot_one_cpu(int i, struct task_struct *idle)
{
	unsigned long *entry = &sun4m_cpu_startup;
	int timeout;
	int cpu_node;

	cpu_find_by_mid(i, &cpu_node);
	current_set[i] = task_thread_info(idle);

	/* See trampoline.S for details... */
	entry += ((i - 1) * 3);

	/*
	 * Initialize the contexts table
	 * Since the call to prom_startcpu() trashes the structure,
	 * we need to re-initialize it for each cpu
	 */
	smp_penguin_ctable.which_io = 0;
	smp_penguin_ctable.phys_addr = (unsigned int) srmmu_ctx_table_phys;
	smp_penguin_ctable.reg_size = 0;

	/* whirrr, whirrr, whirrrrrrrrr... */
	printk(KERN_INFO "Starting CPU %d at %p\n", i, entry);
	local_ops->cache_all();
	prom_startcpu(cpu_node, &smp_penguin_ctable, 0, (char *)entry);

	/* wheee... it's going... */
	for (timeout = 0; timeout < 10000; timeout++) {
		if (cpu_callin_map[i])
			break;
		udelay(200);
	}

	if (!(cpu_callin_map[i])) {
		printk(KERN_ERR "Processor %d is stuck.\n", i);
		return -ENODEV;
	}

	local_ops->cache_all();
	return 0;
}

void __init smp4m_smp_done(void)
{
	int i, first;
	int *prev;

	/* setup cpu list for irq rotation */
	first = 0;
	prev = &first;
	for_each_online_cpu(i) {
		*prev = i;
		prev = &cpu_data(i).next;
	}
	*prev = first;
	local_ops->cache_all();

	/* Ok, they are spinning and ready to go. */
}

static void sun4m_send_ipi(int cpu, int level)
{
	sbus_writel(SUN4M_SOFT_INT(level), &sun4m_irq_percpu[cpu]->set);
}

static void sun4m_ipi_resched(int cpu)
{
	sun4m_send_ipi(cpu, IRQ_IPI_RESCHED);
}

static void sun4m_ipi_single(int cpu)
{
	sun4m_send_ipi(cpu, IRQ_IPI_SINGLE);
}

static void sun4m_ipi_mask_one(int cpu)
{
	sun4m_send_ipi(cpu, IRQ_IPI_MASK);
}

static struct smp_funcall {
	smpfunc_t func;
	unsigned long arg1;
	unsigned long arg2;
	unsigned long arg3;
	unsigned long arg4;
	unsigned long arg5;
	unsigned long processors_in[SUN4M_NCPUS];  /* Set when ipi entered. */
	unsigned long processors_out[SUN4M_NCPUS]; /* Set when ipi exited. */
} ccall_info;

static DEFINE_SPINLOCK(cross_call_lock);

/* Cross calls must be serialized, at least currently. */
static void sun4m_cross_call(smpfunc_t func, cpumask_t mask, unsigned long arg1,
			     unsigned long arg2, unsigned long arg3,
			     unsigned long arg4)
{
		register int ncpus = SUN4M_NCPUS;
		unsigned long flags;

		spin_lock_irqsave(&cross_call_lock, flags);

		/* Init function glue. */
		ccall_info.func = func;
		ccall_info.arg1 = arg1;
		ccall_info.arg2 = arg2;
		ccall_info.arg3 = arg3;
		ccall_info.arg4 = arg4;
		ccall_info.arg5 = 0;

		/* Init receive/complete mapping, plus fire the IPI's off. */
		{
			register int i;

			cpumask_clear_cpu(smp_processor_id(), &mask);
			cpumask_and(&mask, cpu_online_mask, &mask);
			for (i = 0; i < ncpus; i++) {
				if (cpumask_test_cpu(i, &mask)) {
					ccall_info.processors_in[i] = 0;
					ccall_info.processors_out[i] = 0;
					sun4m_send_ipi(i, IRQ_CROSS_CALL);
				} else {
					ccall_info.processors_in[i] = 1;
					ccall_info.processors_out[i] = 1;
				}
			}
		}

		{
			register int i;

			i = 0;
			do {
				if (!cpumask_test_cpu(i, &mask))
					continue;
				while (!ccall_info.processors_in[i])
					barrier();
			} while (++i < ncpus);

			i = 0;
			do {
				if (!cpumask_test_cpu(i, &mask))
					continue;
				while (!ccall_info.processors_out[i])
					barrier();
			} while (++i < ncpus);
		}
		spin_unlock_irqrestore(&cross_call_lock, flags);
}

/* Running cross calls. */
void smp4m_cross_call_irq(void)
{
	int i = smp_processor_id();

	ccall_info.processors_in[i] = 1;
	ccall_info.func(ccall_info.arg1, ccall_info.arg2, ccall_info.arg3,
			ccall_info.arg4, ccall_info.arg5);
	ccall_info.processors_out[i] = 1;
}

void smp4m_percpu_timer_interrupt(struct pt_regs *regs)
{
	struct pt_regs *old_regs;
	struct clock_event_device *ce;
	int cpu = smp_processor_id();

	old_regs = set_irq_regs(regs);

	ce = &per_cpu(sparc32_clockevent, cpu);

	if (clockevent_state_periodic(ce))
		sun4m_clear_profile_irq(cpu);
	else
		sparc_config.load_profile_irq(cpu, 0); /* Is this needless? */

	irq_enter();
	ce->event_handler(ce);
	irq_exit();

	set_irq_regs(old_regs);
}

static const struct sparc32_ipi_ops sun4m_ipi_ops = {
	.cross_call = sun4m_cross_call,
	.resched    = sun4m_ipi_resched,
	.single     = sun4m_ipi_single,
	.mask_one   = sun4m_ipi_mask_one,
};

void __init sun4m_init_smp(void)
{
	sparc32_ipi_ops = &sun4m_ipi_ops;
}
