/*
 *  arch/ppc/kernel/irq.c
 *
 *  Derived from arch/i386/kernel/irq.c
 *    Copyright (C) 1992 Linus Torvalds
 *  Adapted from arch/i386 by Gary Thomas
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Updated and modified by Cort Dougan <cort@fsmlabs.com>
 *    Copyright (C) 1996-2001 Cort Dougan
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 *
 * The MPC8xx has an interrupt mask in the SIU.  If a bit is set, the
 * interrupt is _enabled_.  As expected, IRQ0 is bit 0 in the 32-bit
 * mask register (of which only 16 are defined), hence the weird shifting
 * and complement of the cached_irq_mask.  I want to be able to stuff
 * this right into the SIU SMASK register.
 * Many of the prep/chrp functions are conditional compiled on CONFIG_8xx
 * to reduce code space and undefined function references.
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/threads.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/proc_fs.h>
#include <linux/random.h>
#include <linux/seq_file.h>
#include <linux/cpumask.h>
#include <linux/profile.h>
#include <linux/bitops.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/cache.h>
#include <asm/prom.h>
#include <asm/ptrace.h>

#define NR_MASK_WORDS	((NR_IRQS + 31) / 32)

extern atomic_t ipi_recv;
extern atomic_t ipi_sent;

#define MAXCOUNT 10000000

int ppc_spurious_interrupts = 0;
struct irqaction *ppc_irq_action[NR_IRQS];
unsigned long ppc_cached_irq_mask[NR_MASK_WORDS];
unsigned long ppc_lost_interrupts[NR_MASK_WORDS];
atomic_t ppc_n_lost_interrupts;

#ifdef CONFIG_TAU_INT
extern int tau_initialized;
extern int tau_interrupts(int);
#endif

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j;
	struct irqaction * action;
	unsigned long flags;

	if (i == 0) {
		seq_puts(p, "           ");
		for (j=0; j<NR_CPUS; j++)
			if (cpu_online(j))
				seq_printf(p, "CPU%d       ", j);
		seq_putc(p, '\n');
	}

	if (i < NR_IRQS) {
		spin_lock_irqsave(&irq_desc[i].lock, flags);
		action = irq_desc[i].action;
		if ( !action || !action->handler )
			goto skip;
		seq_printf(p, "%3d: ", i);
#ifdef CONFIG_SMP
		for (j = 0; j < NR_CPUS; j++)
			if (cpu_online(j))
				seq_printf(p, "%10u ",
					   kstat_cpu(j).irqs[i]);
#else
		seq_printf(p, "%10u ", kstat_irqs(i));
#endif /* CONFIG_SMP */
		if (irq_desc[i].handler)
			seq_printf(p, " %s ", irq_desc[i].handler->typename);
		else
			seq_puts(p, "  None      ");
		seq_printf(p, "%s", (irq_desc[i].status & IRQ_LEVEL) ? "Level " : "Edge  ");
		seq_printf(p, "    %s", action->name);
		for (action = action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);
		seq_putc(p, '\n');
skip:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	} else if (i == NR_IRQS) {
#ifdef CONFIG_TAU_INT
		if (tau_initialized){
			seq_puts(p, "TAU: ");
			for (j = 0; j < NR_CPUS; j++)
				if (cpu_online(j))
					seq_printf(p, "%10u ", tau_interrupts(j));
			seq_puts(p, "  PowerPC             Thermal Assist (cpu temp)\n");
		}
#endif
#ifdef CONFIG_SMP
		/* should this be per processor send/receive? */
		seq_printf(p, "IPI (recv/sent): %10u/%u\n",
				atomic_read(&ipi_recv), atomic_read(&ipi_sent));
#endif
		seq_printf(p, "BAD: %10u\n", ppc_spurious_interrupts);
	}
	return 0;
}

void do_IRQ(struct pt_regs *regs)
{
	int irq, first = 1;
        irq_enter();

	/*
	 * Every platform is required to implement ppc_md.get_irq.
	 * This function will either return an irq number or -1 to
	 * indicate there are no more pending.  But the first time
	 * through the loop this means there wasn't and IRQ pending.
	 * The value -2 is for buggy hardware and means that this IRQ
	 * has already been handled. -- Tom
	 */
	while ((irq = ppc_md.get_irq(regs)) >= 0) {
		__do_IRQ(irq, regs);
		first = 0;
	}
	if (irq != -2 && first)
		/* That's not SMP safe ... but who cares ? */
		ppc_spurious_interrupts++;
        irq_exit();
}

void __init init_IRQ(void)
{
	ppc_md.init_IRQ();
}
