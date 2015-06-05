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
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/seq_file.h>
#include <linux/types.h>
#include <asm/io.h>

#include <asm/smp.h>
#include <asm/ldcw.h>

#undef PARISC_IRQ_CR16_COUNTS

extern irqreturn_t timer_interrupt(int, void *);
extern irqreturn_t ipi_interrupt(int, void *);

#define EIEM_MASK(irq)       (1UL<<(CPU_IRQ_MAX - irq))

/* Bits in EIEM correlate with cpu_irq_action[].
** Numbered *Big Endian*! (ie bit 0 is MSB)
*/
static volatile unsigned long cpu_eiem = 0;

/*
** local ACK bitmap ... habitually set to 1, but reset to zero
** between ->ack() and ->end() of the interrupt to prevent
** re-interruption of a processing interrupt.
*/
static DEFINE_PER_CPU(unsigned long, local_ack_eiem) = ~0UL;

static void cpu_mask_irq(struct irq_data *d)
{
	unsigned long eirr_bit = EIEM_MASK(d->irq);

	cpu_eiem &= ~eirr_bit;
	/* Do nothing on the other CPUs.  If they get this interrupt,
	 * The & cpu_eiem in the do_cpu_irq_mask() ensures they won't
	 * handle it, and the set_eiem() at the bottom will ensure it
	 * then gets disabled */
}

static void __cpu_unmask_irq(unsigned int irq)
{
	unsigned long eirr_bit = EIEM_MASK(irq);

	cpu_eiem |= eirr_bit;

	/* This is just a simple NOP IPI.  But what it does is cause
	 * all the other CPUs to do a set_eiem(cpu_eiem) at the end
	 * of the interrupt handler */
	smp_send_all_nop();
}

static void cpu_unmask_irq(struct irq_data *d)
{
	__cpu_unmask_irq(d->irq);
}

void cpu_ack_irq(struct irq_data *d)
{
	unsigned long mask = EIEM_MASK(d->irq);
	int cpu = smp_processor_id();

	/* Clear in EIEM so we can no longer process */
	per_cpu(local_ack_eiem, cpu) &= ~mask;

	/* disable the interrupt */
	set_eiem(cpu_eiem & per_cpu(local_ack_eiem, cpu));

	/* and now ack it */
	mtctl(mask, 23);
}

void cpu_eoi_irq(struct irq_data *d)
{
	unsigned long mask = EIEM_MASK(d->irq);
	int cpu = smp_processor_id();

	/* set it in the eiems---it's no longer in process */
	per_cpu(local_ack_eiem, cpu) |= mask;

	/* enable the interrupt */
	set_eiem(cpu_eiem & per_cpu(local_ack_eiem, cpu));
}

#ifdef CONFIG_SMP
int cpu_check_affinity(struct irq_data *d, const struct cpumask *dest)
{
	int cpu_dest;

	/* timer and ipi have to always be received on all CPUs */
	if (irqd_is_per_cpu(d))
		return -EINVAL;

	/* whatever mask they set, we just allow one CPU */
	cpu_dest = cpumask_first_and(dest, cpu_online_mask);

	return cpu_dest;
}

static int cpu_set_affinity_irq(struct irq_data *d, const struct cpumask *dest,
				bool force)
{
	int cpu_dest;

	cpu_dest = cpu_check_affinity(d, dest);
	if (cpu_dest < 0)
		return -1;

	cpumask_copy(d->affinity, dest);

	return 0;
}
#endif

static struct irq_chip cpu_interrupt_type = {
	.name			= "CPU",
	.irq_mask		= cpu_mask_irq,
	.irq_unmask		= cpu_unmask_irq,
	.irq_ack		= cpu_ack_irq,
	.irq_eoi		= cpu_eoi_irq,
#ifdef CONFIG_SMP
	.irq_set_affinity	= cpu_set_affinity_irq,
#endif
	/* XXX: Needs to be written.  We managed without it so far, but
	 * we really ought to write it.
	 */
	.irq_retrigger	= NULL,
};

DEFINE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);
#define irq_stats(x)		(&per_cpu(irq_stat, x))

/*
 * /proc/interrupts printing for arch specific interrupts
 */
int arch_show_interrupts(struct seq_file *p, int prec)
{
	int j;

#ifdef CONFIG_DEBUG_STACKOVERFLOW
	seq_printf(p, "%*s: ", prec, "STK");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->kernel_stack_usage);
	seq_puts(p, "  Kernel stack usage\n");
# ifdef CONFIG_IRQSTACKS
	seq_printf(p, "%*s: ", prec, "IST");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_stack_usage);
	seq_puts(p, "  Interrupt stack usage\n");
# endif
#endif
#ifdef CONFIG_SMP
	seq_printf(p, "%*s: ", prec, "RES");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_resched_count);
	seq_puts(p, "  Rescheduling interrupts\n");
#endif
	seq_printf(p, "%*s: ", prec, "UAH");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_unaligned_count);
	seq_puts(p, "  Unaligned access handler traps\n");
	seq_printf(p, "%*s: ", prec, "FPA");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_fpassist_count);
	seq_puts(p, "  Floating point assist traps\n");
	seq_printf(p, "%*s: ", prec, "TLB");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_tlb_count);
	seq_puts(p, "  TLB shootdowns\n");
	return 0;
}

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
		struct irq_desc *desc = irq_to_desc(i);
		struct irqaction *action;

		raw_spin_lock_irqsave(&desc->lock, flags);
		action = desc->action;
		if (!action)
			goto skip;
		seq_printf(p, "%3d: ", i);
#ifdef CONFIG_SMP
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", kstat_irqs_cpu(i, j));
#else
		seq_printf(p, "%10u ", kstat_irqs(i));
#endif

		seq_printf(p, " %14s", irq_desc_get_chip(desc)->name);
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
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}

	if (i == NR_IRQS)
		arch_show_interrupts(p, 3);

	return 0;
}



/*
** The following form a "set": Virtual IRQ, Transaction Address, Trans Data.
** Respectively, these map to IRQ region+EIRR, Processor HPA, EIRR bit.
**
** To use txn_XXX() interfaces, get a Virtual IRQ first.
** Then use that to get the Transaction address and data.
*/

int cpu_claim_irq(unsigned int irq, struct irq_chip *type, void *data)
{
	if (irq_has_action(irq))
		return -EBUSY;
	if (irq_get_chip(irq) != &cpu_interrupt_type)
		return -EBUSY;

	/* for iosapic interrupts */
	if (type) {
		irq_set_chip_and_handler(irq, type, handle_percpu_irq);
		irq_set_chip_data(irq, data);
		__cpu_unmask_irq(irq);
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


unsigned long txn_affinity_addr(unsigned int irq, int cpu)
{
#ifdef CONFIG_SMP
	struct irq_data *d = irq_get_irq_data(irq);
	cpumask_copy(d->affinity, cpumask_of(cpu));
#endif

	return per_cpu(cpu_data, cpu).txn_addr;
}


unsigned long txn_alloc_addr(unsigned int virt_irq)
{
	static int next_cpu = -1;

	next_cpu++; /* assign to "next" CPU we want this bugger on */

	/* validate entry */
	while ((next_cpu < nr_cpu_ids) &&
		(!per_cpu(cpu_data, next_cpu).txn_addr ||
		 !cpu_online(next_cpu)))
		next_cpu++;

	if (next_cpu >= nr_cpu_ids) 
		next_cpu = 0;	/* nothing else, assign monarch */

	return txn_affinity_addr(virt_irq, next_cpu);
}


unsigned int txn_alloc_data(unsigned int virt_irq)
{
	return virt_irq - CPU_IRQ_BASE;
}

static inline int eirr_to_irq(unsigned long eirr)
{
	int bit = fls_long(eirr);
	return (BITS_PER_LONG - bit) + TIMER_IRQ;
}

#ifdef CONFIG_IRQSTACKS
/*
 * IRQ STACK - used for irq handler
 */
#define IRQ_STACK_SIZE      (4096 << 2) /* 16k irq stack size */

union irq_stack_union {
	unsigned long stack[IRQ_STACK_SIZE/sizeof(unsigned long)];
	volatile unsigned int slock[4];
	volatile unsigned int lock[1];
};

DEFINE_PER_CPU(union irq_stack_union, irq_stack_union) = {
		.slock = { 1,1,1,1 },
	};
#endif


int sysctl_panic_on_stackoverflow = 1;

static inline void stack_overflow_check(struct pt_regs *regs)
{
#ifdef CONFIG_DEBUG_STACKOVERFLOW
	#define STACK_MARGIN	(256*6)

	/* Our stack starts directly behind the thread_info struct. */
	unsigned long stack_start = (unsigned long) current_thread_info();
	unsigned long sp = regs->gr[30];
	unsigned long stack_usage;
	unsigned int *last_usage;
	int cpu = smp_processor_id();

	/* if sr7 != 0, we interrupted a userspace process which we do not want
	 * to check for stack overflow. We will only check the kernel stack. */
	if (regs->sr[7])
		return;

	/* calculate kernel stack usage */
	stack_usage = sp - stack_start;
#ifdef CONFIG_IRQSTACKS
	if (likely(stack_usage <= THREAD_SIZE))
		goto check_kernel_stack; /* found kernel stack */

	/* check irq stack usage */
	stack_start = (unsigned long) &per_cpu(irq_stack_union, cpu).stack;
	stack_usage = sp - stack_start;

	last_usage = &per_cpu(irq_stat.irq_stack_usage, cpu);
	if (unlikely(stack_usage > *last_usage))
		*last_usage = stack_usage;

	if (likely(stack_usage < (IRQ_STACK_SIZE - STACK_MARGIN)))
		return;

	pr_emerg("stackcheck: %s will most likely overflow irq stack "
		 "(sp:%lx, stk bottom-top:%lx-%lx)\n",
		current->comm, sp, stack_start, stack_start + IRQ_STACK_SIZE);
	goto panic_check;

check_kernel_stack:
#endif

	/* check kernel stack usage */
	last_usage = &per_cpu(irq_stat.kernel_stack_usage, cpu);

	if (unlikely(stack_usage > *last_usage))
		*last_usage = stack_usage;

	if (likely(stack_usage < (THREAD_SIZE - STACK_MARGIN)))
		return;

	pr_emerg("stackcheck: %s will most likely overflow kernel stack "
		 "(sp:%lx, stk bottom-top:%lx-%lx)\n",
		current->comm, sp, stack_start, stack_start + THREAD_SIZE);

#ifdef CONFIG_IRQSTACKS
panic_check:
#endif
	if (sysctl_panic_on_stackoverflow)
		panic("low stack detected by irq handler - check messages\n");
#endif
}

#ifdef CONFIG_IRQSTACKS
/* in entry.S: */
void call_on_stack(unsigned long p1, void *func, unsigned long new_stack);

static void execute_on_irq_stack(void *func, unsigned long param1)
{
	union irq_stack_union *union_ptr;
	unsigned long irq_stack;
	volatile unsigned int *irq_stack_in_use;

	union_ptr = &per_cpu(irq_stack_union, smp_processor_id());
	irq_stack = (unsigned long) &union_ptr->stack;
	irq_stack = ALIGN(irq_stack + sizeof(irq_stack_union.slock),
			 64); /* align for stack frame usage */

	/* We may be called recursive. If we are already using the irq stack,
	 * just continue to use it. Use spinlocks to serialize
	 * the irq stack usage.
	 */
	irq_stack_in_use = (volatile unsigned int *)__ldcw_align(union_ptr);
	if (!__ldcw(irq_stack_in_use)) {
		void (*direct_call)(unsigned long p1) = func;

		/* We are using the IRQ stack already.
		 * Do direct call on current stack. */
		direct_call(param1);
		return;
	}

	/* This is where we switch to the IRQ stack. */
	call_on_stack(param1, func, irq_stack);

	/* free up irq stack usage. */
	*irq_stack_in_use = 1;
}

void do_softirq_own_stack(void)
{
	execute_on_irq_stack(__do_softirq, 0);
}
#endif /* CONFIG_IRQSTACKS */

/* ONLY called from entry.S:intr_extint() */
void do_cpu_irq_mask(struct pt_regs *regs)
{
	struct pt_regs *old_regs;
	unsigned long eirr_val;
	int irq, cpu = smp_processor_id();
#ifdef CONFIG_SMP
	struct irq_desc *desc;
	cpumask_t dest;
#endif

	old_regs = set_irq_regs(regs);
	local_irq_disable();
	irq_enter();

	eirr_val = mfctl(23) & cpu_eiem & per_cpu(local_ack_eiem, cpu);
	if (!eirr_val)
		goto set_out;
	irq = eirr_to_irq(eirr_val);

#ifdef CONFIG_SMP
	desc = irq_to_desc(irq);
	cpumask_copy(&dest, desc->irq_data.affinity);
	if (irqd_is_per_cpu(&desc->irq_data) &&
	    !cpumask_test_cpu(smp_processor_id(), &dest)) {
		int cpu = cpumask_first(&dest);

		printk(KERN_DEBUG "redirecting irq %d from CPU %d to %d\n",
		       irq, smp_processor_id(), cpu);
		gsc_writel(irq + CPU_IRQ_BASE,
			   per_cpu(cpu_data, cpu).hpa);
		goto set_out;
	}
#endif
	stack_overflow_check(regs);

#ifdef CONFIG_IRQSTACKS
	execute_on_irq_stack(&generic_handle_irq, irq);
#else
	generic_handle_irq(irq);
#endif /* CONFIG_IRQSTACKS */

 out:
	irq_exit();
	set_irq_regs(old_regs);
	return;

 set_out:
	set_eiem(cpu_eiem & per_cpu(local_ack_eiem, cpu));
	goto out;
}

static struct irqaction timer_action = {
	.handler = timer_interrupt,
	.name = "timer",
	.flags = IRQF_TIMER | IRQF_PERCPU | IRQF_IRQPOLL,
};

#ifdef CONFIG_SMP
static struct irqaction ipi_action = {
	.handler = ipi_interrupt,
	.name = "IPI",
	.flags = IRQF_PERCPU,
};
#endif

static void claim_cpu_irqs(void)
{
	int i;
	for (i = CPU_IRQ_BASE; i <= CPU_IRQ_MAX; i++) {
		irq_set_chip_and_handler(i, &cpu_interrupt_type,
					 handle_percpu_irq);
	}

	irq_set_handler(TIMER_IRQ, handle_percpu_irq);
	setup_irq(TIMER_IRQ, &timer_action);
#ifdef CONFIG_SMP
	irq_set_handler(IPI_IRQ, handle_percpu_irq);
	setup_irq(IPI_IRQ, &ipi_action);
#endif
}

void __init init_IRQ(void)
{
	local_irq_disable();	/* PARANOID - should already be disabled */
	mtctl(~0UL, 23);	/* EIRR : clear all pending external intr */
#ifdef CONFIG_SMP
	if (!cpu_eiem) {
		claim_cpu_irqs();
		cpu_eiem = EIEM_MASK(IPI_IRQ) | EIEM_MASK(TIMER_IRQ);
	}
#else
	claim_cpu_irqs();
	cpu_eiem = EIEM_MASK(TIMER_IRQ);
#endif
        set_eiem(cpu_eiem);	/* EIEM : enable all external intr */
}
