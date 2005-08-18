/*
 *	linux/arch/i386/kernel/irq.c
 *
 *	Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 * This file contains the lowest level x86-specific interrupt
 * entry, irq-stacks and irq statistics code. All the remaining
 * irq logic is done by the generic kernel/irq/ code and
 * by the x86-specific irq controller code. (e.g. i8259.c and
 * io_apic.c.)
 */

#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/delay.h>

DEFINE_PER_CPU(irq_cpustat_t, irq_stat) ____cacheline_maxaligned_in_smp;
EXPORT_PER_CPU_SYMBOL(irq_stat);

#ifndef CONFIG_X86_LOCAL_APIC
/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
void ack_bad_irq(unsigned int irq)
{
	printk("unexpected IRQ trap at vector %02x\n", irq);
}
#endif

#ifdef CONFIG_4KSTACKS
/*
 * per-CPU IRQ handling contexts (thread information and stack)
 */
union irq_ctx {
	struct thread_info      tinfo;
	u32                     stack[THREAD_SIZE/sizeof(u32)];
};

static union irq_ctx *hardirq_ctx[NR_CPUS];
static union irq_ctx *softirq_ctx[NR_CPUS];
#endif

/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */
fastcall unsigned int do_IRQ(struct pt_regs *regs)
{	
	/* high bits used in ret_from_ code */
	int irq = regs->orig_eax & 0xff;
#ifdef CONFIG_4KSTACKS
	union irq_ctx *curctx, *irqctx;
	u32 *isp;
#endif

	irq_enter();
#ifdef CONFIG_DEBUG_STACKOVERFLOW
	/* Debugging check for stack overflow: is there less than 1KB free? */
	{
		long esp;

		__asm__ __volatile__("andl %%esp,%0" :
					"=r" (esp) : "0" (THREAD_SIZE - 1));
		if (unlikely(esp < (sizeof(struct thread_info) + STACK_WARN))) {
			printk("do_IRQ: stack overflow: %ld\n",
				esp - sizeof(struct thread_info));
			dump_stack();
		}
	}
#endif

#ifdef CONFIG_4KSTACKS

	curctx = (union irq_ctx *) current_thread_info();
	irqctx = hardirq_ctx[smp_processor_id()];

	/*
	 * this is where we switch to the IRQ stack. However, if we are
	 * already using the IRQ stack (because we interrupted a hardirq
	 * handler) we can't do that and just have to keep using the
	 * current stack (which is the irq stack already after all)
	 */
	if (curctx != irqctx) {
		int arg1, arg2, ebx;

		/* build the stack frame on the IRQ stack */
		isp = (u32*) ((char*)irqctx + sizeof(*irqctx));
		irqctx->tinfo.task = curctx->tinfo.task;
		irqctx->tinfo.previous_esp = current_stack_pointer;

		asm volatile(
			"       xchgl   %%ebx,%%esp      \n"
			"       call    __do_IRQ         \n"
			"       movl   %%ebx,%%esp      \n"
			: "=a" (arg1), "=d" (arg2), "=b" (ebx)
			:  "0" (irq),   "1" (regs),  "2" (isp)
			: "memory", "cc", "ecx"
		);
	} else
#endif
		__do_IRQ(irq, regs);

	irq_exit();

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

	irqctx = (union irq_ctx*) &hardirq_stack[cpu*THREAD_SIZE];
	irqctx->tinfo.task              = NULL;
	irqctx->tinfo.exec_domain       = NULL;
	irqctx->tinfo.cpu               = cpu;
	irqctx->tinfo.preempt_count     = HARDIRQ_OFFSET;
	irqctx->tinfo.addr_limit        = MAKE_MM_SEG(0);

	hardirq_ctx[cpu] = irqctx;

	irqctx = (union irq_ctx*) &softirq_stack[cpu*THREAD_SIZE];
	irqctx->tinfo.task              = NULL;
	irqctx->tinfo.exec_domain       = NULL;
	irqctx->tinfo.cpu               = cpu;
	irqctx->tinfo.preempt_count     = SOFTIRQ_OFFSET;
	irqctx->tinfo.addr_limit        = MAKE_MM_SEG(0);

	softirq_ctx[cpu] = irqctx;

	printk("CPU %u irqstacks, hard=%p soft=%p\n",
		cpu,hardirq_ctx[cpu],softirq_ctx[cpu]);
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
		irqctx->tinfo.previous_esp = current_stack_pointer;

		/* build the stack frame on the softirq stack */
		isp = (u32*) ((char*)irqctx + sizeof(*irqctx));

		asm volatile(
			"       xchgl   %%ebx,%%esp     \n"
			"       call    __do_softirq    \n"
			"       movl    %%ebx,%%esp     \n"
			: "=b"(isp)
			: "0"(isp)
			: "memory", "cc", "edx", "ecx", "eax"
		);
	}

	local_irq_restore(flags);
}

EXPORT_SYMBOL(do_softirq);
#endif

/*
 * Interrupt statistics:
 */

atomic_t irq_err_count;

/*
 * /proc/interrupts printing:
 */

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j;
	struct irqaction * action;
	unsigned long flags;

	if (i == 0) {
		seq_printf(p, "           ");
		for_each_cpu(j)
			seq_printf(p, "CPU%d       ",j);
		seq_putc(p, '\n');
	}

	if (i < NR_IRQS) {
		spin_lock_irqsave(&irq_desc[i].lock, flags);
		action = irq_desc[i].action;
		if (!action)
			goto skip;
		seq_printf(p, "%3d: ",i);
#ifndef CONFIG_SMP
		seq_printf(p, "%10u ", kstat_irqs(i));
#else
		for_each_cpu(j)
			seq_printf(p, "%10u ", kstat_cpu(j).irqs[i]);
#endif
		seq_printf(p, " %14s", irq_desc[i].handler->typename);
		seq_printf(p, "  %s", action->name);

		for (action=action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);

		seq_putc(p, '\n');
skip:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	} else if (i == NR_IRQS) {
		seq_printf(p, "NMI: ");
		for_each_cpu(j)
			seq_printf(p, "%10u ", nmi_count(j));
		seq_putc(p, '\n');
#ifdef CONFIG_X86_LOCAL_APIC
		seq_printf(p, "LOC: ");
		for_each_cpu(j)
			seq_printf(p, "%10u ",
				per_cpu(irq_stat,j).apic_timer_irqs);
		seq_putc(p, '\n');
#endif
		seq_printf(p, "ERR: %10u\n", atomic_read(&irq_err_count));
#if defined(CONFIG_X86_IO_APIC)
		seq_printf(p, "MIS: %10u\n", atomic_read(&irq_mis_count));
#endif
	}
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
#include <mach_apic.h>

void fixup_irqs(cpumask_t map)
{
	unsigned int irq;
	static int warned;

	for (irq = 0; irq < NR_IRQS; irq++) {
		cpumask_t mask;
		if (irq == 2)
			continue;

		cpus_and(mask, irq_affinity[irq], map);
		if (any_online_cpu(mask) == NR_CPUS) {
			printk("Breaking affinity for irq %i\n", irq);
			mask = map;
		}
		if (irq_desc[irq].handler->set_affinity)
			irq_desc[irq].handler->set_affinity(irq, mask);
		else if (irq_desc[irq].action && !(warned++))
			printk("Cannot set affinity for irq %i\n", irq);
	}

#if 0
	barrier();
	/* Ingo Molnar says: "after the IO-APIC masks have been redirected
	   [note the nop - the interrupt-enable boundary on x86 is two
	   instructions from sti] - to flush out pending hardirqs and
	   IPIs. After this point nothing is supposed to reach this CPU." */
	__asm__ __volatile__("sti; nop; cli");
	barrier();
#else
	/* That doesn't seem sufficient.  Give it 1ms. */
	local_irq_enable();
	mdelay(1);
	local_irq_disable();
#endif
}
#endif

