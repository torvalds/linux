/*
 * linux/arch/xtensa/kernel/irq.c
 *
 * Xtensa built-in interrupt controller and some generic functions copied
 * from i386.
 *
 * Copyright (C) 2002 - 2005 Tensilica, Inc.
 * Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 *
 * Chris Zankel <chris@zankel.net>
 * Kevin Chea
 *
 */

#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>

#include <asm/uaccess.h>
#include <asm/platform.h>

static void enable_xtensa_irq(unsigned int irq);
static void disable_xtensa_irq(unsigned int irq);
static void mask_and_ack_xtensa(unsigned int irq);
static void end_xtensa_irq(unsigned int irq);

static unsigned int cached_irq_mask;

atomic_t irq_err_count;

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
void ack_bad_irq(unsigned int irq)
{
          printk("unexpected IRQ trap at vector %02x\n", irq);
}

/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */

unsigned int  do_IRQ(int irq, struct pt_regs *regs)
{
	irq_enter();

#ifdef CONFIG_DEBUG_STACKOVERFLOW
	/* Debugging check for stack overflow: is there less than 1KB free? */
	{
		unsigned long sp;

		__asm__ __volatile__ ("mov %0, a1\n" : "=a" (sp));
		sp &= THREAD_SIZE - 1;

		if (unlikely(sp < (sizeof(thread_info) + 1024)))
			printk("Stack overflow in do_IRQ: %ld\n",
			       sp - sizeof(struct thread_info));
	}
#endif

	__do_IRQ(irq, regs);

	irq_exit();

	return 1;
}

/*
 * Generic, controller-independent functions:
 */

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j;
	struct irqaction * action;
	unsigned long flags;

	if (i == 0) {
		seq_printf(p, "           ");
		for_each_online_cpu(j)
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
		for_each_online_cpu(j)
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
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", nmi_count(j));
		seq_putc(p, '\n');
		seq_printf(p, "ERR: %10u\n", atomic_read(&irq_err_count));
	}
	return 0;
}
/* shutdown is same as "disable" */
#define shutdown_xtensa_irq disable_xtensa_irq

static unsigned int startup_xtensa_irq(unsigned int irq)
{
	enable_xtensa_irq(irq);
	return 0;               /* never anything pending */
}

static struct hw_interrupt_type xtensa_irq_type = {
	"Xtensa-IRQ",
	startup_xtensa_irq,
	shutdown_xtensa_irq,
	enable_xtensa_irq,
	disable_xtensa_irq,
	mask_and_ack_xtensa,
	end_xtensa_irq
};

static inline void mask_irq(unsigned int irq)
{
	cached_irq_mask &= ~(1 << irq);
	set_sr (cached_irq_mask, INTENABLE);
}

static inline void unmask_irq(unsigned int irq)
{
	cached_irq_mask |= 1 << irq;
	set_sr (cached_irq_mask, INTENABLE);
}

static void disable_xtensa_irq(unsigned int irq)
{
	unsigned long flags;
	local_save_flags(flags);
	mask_irq(irq);
	local_irq_restore(flags);
}

static void enable_xtensa_irq(unsigned int irq)
{
	unsigned long flags;
	local_save_flags(flags);
	unmask_irq(irq);
	local_irq_restore(flags);
}

static void mask_and_ack_xtensa(unsigned int irq)
{
        disable_xtensa_irq(irq);
}

static void end_xtensa_irq(unsigned int irq)
{
        enable_xtensa_irq(irq);
}


void __init init_IRQ(void)
{
	int i;

	for (i=0; i < XTENSA_NR_IRQS; i++)
		irq_desc[i].handler = &xtensa_irq_type;

	cached_irq_mask = 0;

	platform_init_irq();
}
