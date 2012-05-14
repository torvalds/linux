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

static void smp4m_ipi_init(void);

void __cpuinit smp4m_callin(void)
{
	int cpuid = hard_smp_processor_id();

	local_ops->cache_all();
	local_ops->tlb_all();

	notify_cpu_starting(cpuid);

	register_percpu_ce(cpuid);

	calibrate_delay();
	smp_store_cpu_info(cpuid);

	local_ops->cache_all();
	local_ops->tlb_all();

	/*
	 * Unblock the master CPU _only_ when the scheduler state
	 * of all secondary CPUs will be up-to-date, so after
	 * the SMP initialization the master will be just allowed
	 * to call the scheduler code.
	 */
	/* Allow master to continue. */
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

	local_irq_enable();

	set_cpu_online(cpuid, true);
}

/*
 *	Cycle through the processors asking the PROM to start each one.
 */
void __init smp4m_boot_cpus(void)
{
	smp4m_ipi_init();
	sun4m_unmask_profile_irq();
	local_ops->cache_all();
}

int __cpuinit smp4m_boot_one_cpu(int i)
{
	unsigned long *entry = &sun4m_cpu_startup;
	struct task_struct *p;
	int timeout;
	int cpu_node;

	cpu_find_by_mid(i, &cpu_node);

	/* Cook up an idler for this guy. */
	p = fork_idle(i);
	current_set[i] = task_thread_info(p);
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


/* Initialize IPIs on the SUN4M SMP machine */
static void __init smp4m_ipi_init(void)
{
}

static void smp4m_ipi_resched(int cpu)
{
	set_cpu_int(cpu, IRQ_IPI_RESCHED);
}

static void smp4m_ipi_single(int cpu)
{
	set_cpu_int(cpu, IRQ_IPI_SINGLE);
}

static void smp4m_ipi_mask_one(int cpu)
{
	set_cpu_int(cpu, IRQ_IPI_MASK);
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
static void smp4m_cross_call(smpfunc_t func, cpumask_t mask, unsigned long arg1,
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
					set_cpu_int(i, IRQ_CROSS_CALL);
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

	if (ce->mode & CLOCK_EVT_MODE_PERIODIC)
		sun4m_clear_profile_irq(cpu);
	else
		load_profile_irq(cpu, 0); /* Is this needless? */

	irq_enter();
	ce->event_handler(ce);
	irq_exit();

	set_irq_regs(old_regs);
}

void __init sun4m_init_smp(void)
{
	BTFIXUPSET_CALL(smp_cross_call, smp4m_cross_call, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(smp_ipi_resched, smp4m_ipi_resched, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(smp_ipi_single, smp4m_ipi_single, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(smp_ipi_mask_one, smp4m_ipi_mask_one, BTFIXUPCALL_NORM);
}
