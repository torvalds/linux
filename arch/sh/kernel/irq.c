/*
 * linux/arch/sh/kernel/irq.c
 *
 *	Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 *
 * SuperH version:  Copyright (C) 1999  Niibe Yutaka
 */
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kernel_stat.h>
#include <linux/seq_file.h>
#include <linux/ftrace.h>
#include <linux/delay.h>
#include <linux/ratelimit.h>
#include <asm/processor.h>
#include <asm/machvec.h>
#include <asm/uaccess.h>
#include <asm/thread_info.h>
#include <cpu/mmu_context.h>

atomic_t irq_err_count;

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves, it doesn't deserve
 * a generic callback i think.
 */
void ack_bad_irq(unsigned int irq)
{
	atomic_inc(&irq_err_count);
	printk("unexpected IRQ trap at vector %02x\n", irq);
}

#if defined(CONFIG_PROC_FS)
/*
 * /proc/interrupts printing for arch specific interrupts
 */
int arch_show_interrupts(struct seq_file *p, int prec)
{
	int j;

	seq_printf(p, "%*s: ", prec, "NMI");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stat[j].__nmi_count);
	seq_printf(p, "  Non-maskable interrupts\n");

	seq_printf(p, "%*s: %10u\n", prec, "ERR", atomic_read(&irq_err_count));

	return 0;
}
#endif

#ifdef CONFIG_IRQSTACKS
/*
 * per-CPU IRQ handling contexts (thread information and stack)
 */
union irq_ctx {
	struct thread_info	tinfo;
	u32			stack[THREAD_SIZE/sizeof(u32)];
};

static union irq_ctx *hardirq_ctx[NR_CPUS] __read_mostly;
static union irq_ctx *softirq_ctx[NR_CPUS] __read_mostly;

static char softirq_stack[NR_CPUS * THREAD_SIZE] __page_aligned_bss;
static char hardirq_stack[NR_CPUS * THREAD_SIZE] __page_aligned_bss;

static inline void handle_one_irq(unsigned int irq)
{
	union irq_ctx *curctx, *irqctx;

	curctx = (union irq_ctx *)current_thread_info();
	irqctx = hardirq_ctx[smp_processor_id()];

	/*
	 * this is where we switch to the IRQ stack. However, if we are
	 * already using the IRQ stack (because we interrupted a hardirq
	 * handler) we can't do that and just have to keep using the
	 * current stack (which is the irq stack already after all)
	 */
	if (curctx != irqctx) {
		u32 *isp;

		isp = (u32 *)((char *)irqctx + sizeof(*irqctx));
		irqctx->tinfo.task = curctx->tinfo.task;
		irqctx->tinfo.previous_sp = current_stack_pointer;

		/*
		 * Copy the softirq bits in preempt_count so that the
		 * softirq checks work in the hardirq context.
		 */
		irqctx->tinfo.preempt_count =
			(irqctx->tinfo.preempt_count & ~SOFTIRQ_MASK) |
			(curctx->tinfo.preempt_count & SOFTIRQ_MASK);

		__asm__ __volatile__ (
			"mov	%0, r4		\n"
			"mov	r15, r8		\n"
			"jsr	@%1		\n"
			/* swith to the irq stack */
			" mov	%2, r15		\n"
			/* restore the stack (ring zero) */
			"mov	r8, r15		\n"
			: /* no outputs */
			: "r" (irq), "r" (generic_handle_irq), "r" (isp)
			: "memory", "r0", "r1", "r2", "r3", "r4",
			  "r5", "r6", "r7", "r8", "t", "pr"
		);
	} else
		generic_handle_irq(irq);
}

/*
 * allocate per-cpu stacks for hardirq and for softirq processing
 */
void irq_ctx_init(int cpu)
{
	union irq_ctx *irqctx;

	if (hardirq_ctx[cpu])
		return;

	irqctx = (union irq_ctx *)&hardirq_stack[cpu * THREAD_SIZE];
	irqctx->tinfo.task		= NULL;
	irqctx->tinfo.exec_domain	= NULL;
	irqctx->tinfo.cpu		= cpu;
	irqctx->tinfo.preempt_count	= HARDIRQ_OFFSET;
	irqctx->tinfo.addr_limit	= MAKE_MM_SEG(0);

	hardirq_ctx[cpu] = irqctx;

	irqctx = (union irq_ctx *)&softirq_stack[cpu * THREAD_SIZE];
	irqctx->tinfo.task		= NULL;
	irqctx->tinfo.exec_domain	= NULL;
	irqctx->tinfo.cpu		= cpu;
	irqctx->tinfo.preempt_count	= 0;
	irqctx->tinfo.addr_limit	= MAKE_MM_SEG(0);

	softirq_ctx[cpu] = irqctx;

	printk("CPU %u irqstacks, hard=%p soft=%p\n",
		cpu, hardirq_ctx[cpu], softirq_ctx[cpu]);
}

void irq_ctx_exit(int cpu)
{
	hardirq_ctx[cpu] = NULL;
}

asmlinkage void do_softirq(void)
{
	unsigned long flags;
	struct thread_info *curctx;
	union irq_ctx *irqctx;
	u32 *isp;

	if (in_interrupt())
		return;

	local_irq_save(flags);

	if (local_softirq_pending()) {
		curctx = current_thread_info();
		irqctx = softirq_ctx[smp_processor_id()];
		irqctx->tinfo.task = curctx->task;
		irqctx->tinfo.previous_sp = current_stack_pointer;

		/* build the stack frame on the softirq stack */
		isp = (u32 *)((char *)irqctx + sizeof(*irqctx));

		__asm__ __volatile__ (
			"mov	r15, r9		\n"
			"jsr	@%0		\n"
			/* switch to the softirq stack */
			" mov	%1, r15		\n"
			/* restore the thread stack */
			"mov	r9, r15		\n"
			: /* no outputs */
			: "r" (__do_softirq), "r" (isp)
			: "memory", "r0", "r1", "r2", "r3", "r4",
			  "r5", "r6", "r7", "r8", "r9", "r15", "t", "pr"
		);

		/*
		 * Shouldn't happen, we returned above if in_interrupt():
		 */
		WARN_ON_ONCE(softirq_count());
	}

	local_irq_restore(flags);
}
#else
static inline void handle_one_irq(unsigned int irq)
{
	generic_handle_irq(irq);
}
#endif

asmlinkage __irq_entry int do_IRQ(unsigned int irq, struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	irq_enter();

	irq = irq_demux(irq_lookup(irq));

	if (irq != NO_IRQ_IGNORE) {
		handle_one_irq(irq);
		irq_finish(irq);
	}

	irq_exit();

	set_irq_regs(old_regs);

	return IRQ_HANDLED;
}

void __init init_IRQ(void)
{
	plat_irq_setup();

	/* Perform the machine specific initialisation */
	if (sh_mv.mv_init_irq)
		sh_mv.mv_init_irq();

	intc_finalize();

	irq_ctx_init(smp_processor_id());
}

#ifdef CONFIG_SPARSE_IRQ
int __init arch_probe_nr_irqs(void)
{
	/*
	 * No pre-allocated IRQs.
	 */
	return 0;
}
#endif

#ifdef CONFIG_HOTPLUG_CPU
static void route_irq(struct irq_data *data, unsigned int irq, unsigned int cpu)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irq_chip *chip = irq_data_get_irq_chip(data);

	printk(KERN_INFO "IRQ%u: moving from cpu%u to cpu%u\n",
	       irq, data->node, cpu);

	raw_spin_lock_irq(&desc->lock);
	chip->irq_set_affinity(data, cpumask_of(cpu), false);
	raw_spin_unlock_irq(&desc->lock);
}

/*
 * The CPU has been marked offline.  Migrate IRQs off this CPU.  If
 * the affinity settings do not allow other CPUs, force them onto any
 * available CPU.
 */
void migrate_irqs(void)
{
	unsigned int irq, cpu = smp_processor_id();

	for_each_active_irq(irq) {
		struct irq_data *data = irq_get_irq_data(irq);

		if (data->node == cpu) {
			unsigned int newcpu = cpumask_any_and(data->affinity,
							      cpu_online_mask);
			if (newcpu >= nr_cpu_ids) {
				pr_info_ratelimited("IRQ%u no longer affine to CPU%u\n",
						    irq, cpu);

				cpumask_setall(data->affinity);
				newcpu = cpumask_any_and(data->affinity,
							 cpu_online_mask);
			}

			route_irq(data, irq, newcpu);
		}
	}
}
#endif
