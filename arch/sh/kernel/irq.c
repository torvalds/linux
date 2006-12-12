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
#include <linux/io.h>
#include <linux/irq.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/thread_info.h>
#include <asm/cpu/mmu_context.h>

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
int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j;
	struct irqaction * action;
	unsigned long flags;

	if (i == 0) {
		seq_puts(p, "           ");
		for_each_online_cpu(j)
			seq_printf(p, "CPU%d       ",j);
		seq_putc(p, '\n');
	}

	if (i < NR_IRQS) {
		spin_lock_irqsave(&irq_desc[i].lock, flags);
		action = irq_desc[i].action;
		if (!action)
			goto unlock;
		seq_printf(p, "%3d: ",i);
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", kstat_cpu(j).irqs[i]);
		seq_printf(p, " %14s", irq_desc[i].chip->name);
		seq_printf(p, "-%-8s", irq_desc[i].name);
		seq_printf(p, "  %s", action->name);

		for (action=action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);
		seq_putc(p, '\n');
unlock:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	} else if (i == NR_IRQS)
		seq_printf(p, "Err: %10u\n", atomic_read(&irq_err_count));

	return 0;
}
#endif

#ifdef CONFIG_4KSTACKS
/*
 * per-CPU IRQ handling contexts (thread information and stack)
 */
union irq_ctx {
	struct thread_info	tinfo;
	u32			stack[THREAD_SIZE/sizeof(u32)];
};

static union irq_ctx *hardirq_ctx[NR_CPUS] __read_mostly;
static union irq_ctx *softirq_ctx[NR_CPUS] __read_mostly;
#endif

asmlinkage int do_IRQ(unsigned long r4, unsigned long r5,
		      unsigned long r6, unsigned long r7,
		      struct pt_regs __regs)
{
	struct pt_regs *regs = RELOC_HIDE(&__regs, 0);
	struct pt_regs *old_regs = set_irq_regs(regs);
	int irq;
#ifdef CONFIG_4KSTACKS
	union irq_ctx *curctx, *irqctx;
#endif

	irq_enter();

#ifdef CONFIG_DEBUG_STACKOVERFLOW
	/* Debugging check for stack overflow: is there less than 1KB free? */
	{
		long sp;

		__asm__ __volatile__ ("and r15, %0" :
					"=r" (sp) : "0" (THREAD_SIZE - 1));

		if (unlikely(sp < (sizeof(struct thread_info) + STACK_WARN))) {
			printk("do_IRQ: stack overflow: %ld\n",
			       sp - sizeof(struct thread_info));
			dump_stack();
		}
	}
#endif

#ifdef CONFIG_CPU_HAS_INTEVT
	irq = evt2irq(ctrl_inl(INTEVT));
#else
	irq = r4;
#endif

	irq = irq_demux(irq);

#ifdef CONFIG_4KSTACKS
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
#endif
		generic_handle_irq(irq);

	irq_exit();

	set_irq_regs(old_regs);
	return 1;
}

#ifdef CONFIG_4KSTACKS
/*
 * These should really be __section__(".bss.page_aligned") as well, but
 * gcc's 3.0 and earlier don't handle that correctly.
 */
static char softirq_stack[NR_CPUS * THREAD_SIZE]
		__attribute__((__aligned__(THREAD_SIZE)));

static char hardirq_stack[NR_CPUS * THREAD_SIZE]
		__attribute__((__aligned__(THREAD_SIZE)));

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

extern asmlinkage void __do_softirq(void);

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
		 * Shouldnt happen, we returned above if in_interrupt():
		 */
		WARN_ON_ONCE(softirq_count());
	}

	local_irq_restore(flags);
}
EXPORT_SYMBOL(do_softirq);
#endif

void __init init_IRQ(void)
{
#ifdef CONFIG_CPU_HAS_PINT_IRQ
	init_IRQ_pint();
#endif

#ifdef CONFIG_CPU_HAS_INTC2_IRQ
	init_IRQ_intc2();
#endif

#ifdef CONFIG_CPU_HAS_IPR_IRQ
	init_IRQ_ipr();
#endif

	/* Perform the machine specific initialisation */
	if (sh_mv.mv_init_irq)
		sh_mv.mv_init_irq();

	irq_ctx_init(smp_processor_id());
}
