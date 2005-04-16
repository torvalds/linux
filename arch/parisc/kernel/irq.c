/* 
 * Code to handle x86 style IRQs plus some generic interrupt stuff.
 *
 * Copyright (C) 1992 Linus Torvalds
 * Copyright (C) 1994, 1995, 1996, 1997, 1998 Ralf Baechle
 * Copyright (C) 1999 SuSE GmbH (Philipp Rumpf, prumpf@tux.org)
 * Copyright (C) 1999-2000 Grant Grundler
 * Copyright (c) 2005 Matthew Wilcox
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/bitops.h>
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#undef PARISC_IRQ_CR16_COUNTS

extern irqreturn_t timer_interrupt(int, void *, struct pt_regs *);
extern irqreturn_t ipi_interrupt(int, void *, struct pt_regs *);

#define EIEM_MASK(irq)       (1UL<<(CPU_IRQ_MAX - irq))

/* Bits in EIEM correlate with cpu_irq_action[].
** Numbered *Big Endian*! (ie bit 0 is MSB)
*/
static volatile unsigned long cpu_eiem = 0;

static void cpu_set_eiem(void *info)
{
	set_eiem((unsigned long) info);
}

static inline void cpu_disable_irq(unsigned int irq)
{
	unsigned long eirr_bit = EIEM_MASK(irq);

	cpu_eiem &= ~eirr_bit;
        on_each_cpu(cpu_set_eiem, (void *) cpu_eiem, 1, 1);
}

static void cpu_enable_irq(unsigned int irq)
{
	unsigned long eirr_bit = EIEM_MASK(irq);

	mtctl(eirr_bit, 23);	/* clear EIRR bit before unmasking */
	cpu_eiem |= eirr_bit;
        on_each_cpu(cpu_set_eiem, (void *) cpu_eiem, 1, 1);
}

static unsigned int cpu_startup_irq(unsigned int irq)
{
	cpu_enable_irq(irq);
	return 0;
}

void no_ack_irq(unsigned int irq) { }
void no_end_irq(unsigned int irq) { }

static struct hw_interrupt_type cpu_interrupt_type = {
	.typename	= "CPU",
	.startup	= cpu_startup_irq,
	.shutdown	= cpu_disable_irq,
	.enable		= cpu_enable_irq,
	.disable	= cpu_disable_irq,
	.ack		= no_ack_irq,
	.end		= no_end_irq,
//	.set_affinity	= cpu_set_affinity_irq,
};

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j;
	unsigned long flags;

	if (i == 0) {
		seq_puts(p, "    ");
		for_each_online_cpu(j)
			seq_printf(p, "       CPU%d", j);

#ifdef PARISC_IRQ_CR16_COUNTS
		seq_printf(p, " [min/avg/max] (CPU cycle counts)");
#endif
		seq_putc(p, '\n');
	}

	if (i < NR_IRQS) {
		struct irqaction *action;

		spin_lock_irqsave(&irq_desc[i].lock, flags);
		action = irq_desc[i].action;
		if (!action)
			goto skip;
		seq_printf(p, "%3d: ", i);
#ifdef CONFIG_SMP
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", kstat_cpu(j).irqs[i]);
#else
		seq_printf(p, "%10u ", kstat_irqs(i));
#endif

		seq_printf(p, " %14s", irq_desc[i].handler->typename);
#ifndef PARISC_IRQ_CR16_COUNTS
		seq_printf(p, "  %s", action->name);

		while ((action = action->next))
			seq_printf(p, ", %s", action->name);
#else
		for ( ;action; action = action->next) {
			unsigned int k, avg, min, max;

			min = max = action->cr16_hist[0];

			for (avg = k = 0; k < PARISC_CR16_HIST_SIZE; k++) {
				int hist = action->cr16_hist[k];

				if (hist) {
					avg += hist;
				} else
					break;

				if (hist > max) max = hist;
				if (hist < min) min = hist;
			}

			avg /= k;
			seq_printf(p, " %s[%d/%d/%d]", action->name,
					min,avg,max);
		}
#endif

		seq_putc(p, '\n');
 skip:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	}

	return 0;
}



/*
** The following form a "set": Virtual IRQ, Transaction Address, Trans Data.
** Respectively, these map to IRQ region+EIRR, Processor HPA, EIRR bit.
**
** To use txn_XXX() interfaces, get a Virtual IRQ first.
** Then use that to get the Transaction address and data.
*/

int cpu_claim_irq(unsigned int irq, struct hw_interrupt_type *type, void *data)
{
	if (irq_desc[irq].action)
		return -EBUSY;
	if (irq_desc[irq].handler != &cpu_interrupt_type)
		return -EBUSY;

	if (type) {
		irq_desc[irq].handler = type;
		irq_desc[irq].handler_data = data;
		cpu_interrupt_type.enable(irq);
	}
	return 0;
}

int txn_claim_irq(int irq)
{
	return cpu_claim_irq(irq, NULL, NULL) ? -1 : irq;
}

/*
 * The bits_wide parameter accommodates the limitations of the HW/SW which
 * use these bits:
 * Legacy PA I/O (GSC/NIO): 5 bits (architected EIM register)
 * V-class (EPIC):          6 bits
 * N/L/A-class (iosapic):   8 bits
 * PCI 2.2 MSI:            16 bits
 * Some PCI devices:       32 bits (Symbios SCSI/ATM/HyperFabric)
 *
 * On the service provider side:
 * o PA 1.1 (and PA2.0 narrow mode)     5-bits (width of EIR register)
 * o PA 2.0 wide mode                   6-bits (per processor)
 * o IA64                               8-bits (0-256 total)
 *
 * So a Legacy PA I/O device on a PA 2.0 box can't use all the bits supported
 * by the processor...and the N/L-class I/O subsystem supports more bits than
 * PA2.0 has. The first case is the problem.
 */
int txn_alloc_irq(unsigned int bits_wide)
{
	int irq;

	/* never return irq 0 cause that's the interval timer */
	for (irq = CPU_IRQ_BASE + 1; irq <= CPU_IRQ_MAX; irq++) {
		if (cpu_claim_irq(irq, NULL, NULL) < 0)
			continue;
		if ((irq - CPU_IRQ_BASE) >= (1 << bits_wide))
			continue;
		return irq;
	}

	/* unlikely, but be prepared */
	return -1;
}

unsigned long txn_alloc_addr(unsigned int virt_irq)
{
	static int next_cpu = -1;

	next_cpu++; /* assign to "next" CPU we want this bugger on */

	/* validate entry */
	while ((next_cpu < NR_CPUS) && (!cpu_data[next_cpu].txn_addr || 
		!cpu_online(next_cpu)))
		next_cpu++;

	if (next_cpu >= NR_CPUS) 
		next_cpu = 0;	/* nothing else, assign monarch */

	return cpu_data[next_cpu].txn_addr;
}


unsigned int txn_alloc_data(unsigned int virt_irq)
{
	return virt_irq - CPU_IRQ_BASE;
}

/* ONLY called from entry.S:intr_extint() */
void do_cpu_irq_mask(struct pt_regs *regs)
{
	unsigned long eirr_val;

	irq_enter();

	/*
	 * Only allow interrupt processing to be interrupted by the
	 * timer tick
	 */
	set_eiem(EIEM_MASK(TIMER_IRQ));

	/* 1) only process IRQs that are enabled/unmasked (cpu_eiem)
	 * 2) We loop here on EIRR contents in order to avoid
	 *    nested interrupts or having to take another interrupt
	 *    when we could have just handled it right away.
	 */
	for (;;) {
		unsigned long bit = (1UL << (BITS_PER_LONG - 1));
		unsigned int irq;
		eirr_val = mfctl(23) & cpu_eiem;
		if (!eirr_val)
			break;

		if (eirr_val & EIEM_MASK(TIMER_IRQ))
			set_eiem(0);

		mtctl(eirr_val, 23); /* reset bits we are going to process */

		/* Work our way from MSb to LSb...same order we alloc EIRs */
		for (irq = TIMER_IRQ; eirr_val && bit; bit>>=1, irq++) {
			if (!(bit & eirr_val))
				continue;

			/* clear bit in mask - can exit loop sooner */
			eirr_val &= ~bit;

			__do_IRQ(irq, regs);
		}
	}
	set_eiem(cpu_eiem);
	irq_exit();
}


static struct irqaction timer_action = {
	.handler = timer_interrupt,
	.name = "timer",
};

#ifdef CONFIG_SMP
static struct irqaction ipi_action = {
	.handler = ipi_interrupt,
	.name = "IPI",
};
#endif

static void claim_cpu_irqs(void)
{
	int i;
	for (i = CPU_IRQ_BASE; i <= CPU_IRQ_MAX; i++) {
		irq_desc[i].handler = &cpu_interrupt_type;
	}

	irq_desc[TIMER_IRQ].action = &timer_action;
	irq_desc[TIMER_IRQ].status |= IRQ_PER_CPU;
#ifdef CONFIG_SMP
	irq_desc[IPI_IRQ].action = &ipi_action;
	irq_desc[IPI_IRQ].status = IRQ_PER_CPU;
#endif
}

void __init init_IRQ(void)
{
	local_irq_disable();	/* PARANOID - should already be disabled */
	mtctl(~0UL, 23);	/* EIRR : clear all pending external intr */
	claim_cpu_irqs();
#ifdef CONFIG_SMP
	if (!cpu_eiem)
		cpu_eiem = EIEM_MASK(IPI_IRQ) | EIEM_MASK(TIMER_IRQ);
#else
	cpu_eiem = EIEM_MASK(TIMER_IRQ);
#endif
        set_eiem(cpu_eiem);	/* EIEM : enable all external intr */

}

void hw_resend_irq(struct hw_interrupt_type *type, unsigned int irq)
{
	/* XXX: Needs to be written.  We managed without it so far, but
	 * we really ought to write it.
	 */
}

void ack_bad_irq(unsigned int irq)
{
	printk("unexpected IRQ %d\n", irq);
}
