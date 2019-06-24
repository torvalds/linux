// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2000, 2001, 2002, 2003 Broadcom Corporation
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/mm.h>
#include <linux/kernel_stat.h>

#include <asm/errno.h>
#include <asm/signal.h>
#include <asm/time.h>
#include <asm/io.h>

#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_int.h>
#include <asm/sibyte/sb1250_uart.h>
#include <asm/sibyte/sb1250_scd.h>
#include <asm/sibyte/sb1250.h>

/*
 * These are the routines that handle all the low level interrupt stuff.
 * Actions handled here are: initialization of the interrupt map, requesting of
 * interrupt lines by handlers, dispatching if interrupts to handlers, probing
 * for interrupt lines
 */

#ifdef CONFIG_SIBYTE_HAS_LDT
extern unsigned long ldt_eoi_space;
#endif

/* Store the CPU id (not the logical number) */
int sb1250_irq_owner[SB1250_NR_IRQS];

static DEFINE_RAW_SPINLOCK(sb1250_imr_lock);

void sb1250_mask_irq(int cpu, int irq)
{
	unsigned long flags;
	u64 cur_ints;

	raw_spin_lock_irqsave(&sb1250_imr_lock, flags);
	cur_ints = ____raw_readq(IOADDR(A_IMR_MAPPER(cpu) +
					R_IMR_INTERRUPT_MASK));
	cur_ints |= (((u64) 1) << irq);
	____raw_writeq(cur_ints, IOADDR(A_IMR_MAPPER(cpu) +
					R_IMR_INTERRUPT_MASK));
	raw_spin_unlock_irqrestore(&sb1250_imr_lock, flags);
}

void sb1250_unmask_irq(int cpu, int irq)
{
	unsigned long flags;
	u64 cur_ints;

	raw_spin_lock_irqsave(&sb1250_imr_lock, flags);
	cur_ints = ____raw_readq(IOADDR(A_IMR_MAPPER(cpu) +
					R_IMR_INTERRUPT_MASK));
	cur_ints &= ~(((u64) 1) << irq);
	____raw_writeq(cur_ints, IOADDR(A_IMR_MAPPER(cpu) +
					R_IMR_INTERRUPT_MASK));
	raw_spin_unlock_irqrestore(&sb1250_imr_lock, flags);
}

#ifdef CONFIG_SMP
static int sb1250_set_affinity(struct irq_data *d, const struct cpumask *mask,
			       bool force)
{
	int i = 0, old_cpu, cpu, int_on;
	unsigned int irq = d->irq;
	u64 cur_ints;
	unsigned long flags;

	i = cpumask_first_and(mask, cpu_online_mask);

	/* Convert logical CPU to physical CPU */
	cpu = cpu_logical_map(i);

	/* Protect against other affinity changers and IMR manipulation */
	raw_spin_lock_irqsave(&sb1250_imr_lock, flags);

	/* Swizzle each CPU's IMR (but leave the IP selection alone) */
	old_cpu = sb1250_irq_owner[irq];
	cur_ints = ____raw_readq(IOADDR(A_IMR_MAPPER(old_cpu) +
					R_IMR_INTERRUPT_MASK));
	int_on = !(cur_ints & (((u64) 1) << irq));
	if (int_on) {
		/* If it was on, mask it */
		cur_ints |= (((u64) 1) << irq);
		____raw_writeq(cur_ints, IOADDR(A_IMR_MAPPER(old_cpu) +
					R_IMR_INTERRUPT_MASK));
	}
	sb1250_irq_owner[irq] = cpu;
	if (int_on) {
		/* unmask for the new CPU */
		cur_ints = ____raw_readq(IOADDR(A_IMR_MAPPER(cpu) +
					R_IMR_INTERRUPT_MASK));
		cur_ints &= ~(((u64) 1) << irq);
		____raw_writeq(cur_ints, IOADDR(A_IMR_MAPPER(cpu) +
					R_IMR_INTERRUPT_MASK));
	}
	raw_spin_unlock_irqrestore(&sb1250_imr_lock, flags);

	return 0;
}
#endif

static void disable_sb1250_irq(struct irq_data *d)
{
	unsigned int irq = d->irq;

	sb1250_mask_irq(sb1250_irq_owner[irq], irq);
}

static void enable_sb1250_irq(struct irq_data *d)
{
	unsigned int irq = d->irq;

	sb1250_unmask_irq(sb1250_irq_owner[irq], irq);
}


static void ack_sb1250_irq(struct irq_data *d)
{
	unsigned int irq = d->irq;
#ifdef CONFIG_SIBYTE_HAS_LDT
	u64 pending;

	/*
	 * If the interrupt was an HT interrupt, now is the time to
	 * clear it.  NOTE: we assume the HT bridge was set up to
	 * deliver the interrupts to all CPUs (which makes affinity
	 * changing easier for us)
	 */
	pending = __raw_readq(IOADDR(A_IMR_REGISTER(sb1250_irq_owner[irq],
						    R_IMR_LDT_INTERRUPT)));
	pending &= ((u64)1 << (irq));
	if (pending) {
		int i;
		for (i=0; i<NR_CPUS; i++) {
			int cpu;
#ifdef CONFIG_SMP
			cpu = cpu_logical_map(i);
#else
			cpu = i;
#endif
			/*
			 * Clear for all CPUs so an affinity switch
			 * doesn't find an old status
			 */
			__raw_writeq(pending,
				     IOADDR(A_IMR_REGISTER(cpu,
						R_IMR_LDT_INTERRUPT_CLR)));
		}

		/*
		 * Generate EOI.  For Pass 1 parts, EOI is a nop.  For
		 * Pass 2, the LDT world may be edge-triggered, but
		 * this EOI shouldn't hurt.  If they are
		 * level-sensitive, the EOI is required.
		 */
		*(uint32_t *)(ldt_eoi_space+(irq<<16)+(7<<2)) = 0;
	}
#endif
	sb1250_mask_irq(sb1250_irq_owner[irq], irq);
}

static struct irq_chip sb1250_irq_type = {
	.name = "SB1250-IMR",
	.irq_mask_ack = ack_sb1250_irq,
	.irq_unmask = enable_sb1250_irq,
	.irq_mask = disable_sb1250_irq,
#ifdef CONFIG_SMP
	.irq_set_affinity = sb1250_set_affinity
#endif
};

void __init init_sb1250_irqs(void)
{
	int i;

	for (i = 0; i < SB1250_NR_IRQS; i++) {
		irq_set_chip_and_handler(i, &sb1250_irq_type,
					 handle_level_irq);
		sb1250_irq_owner[i] = 0;
	}
}


/*
 *  arch_init_irq is called early in the boot sequence from init/main.c via
 *  init_IRQ.  It is responsible for setting up the interrupt mapper and
 *  installing the handler that will be responsible for dispatching interrupts
 *  to the "right" place.
 */
/*
 * For now, map all interrupts to IP[2].  We could save
 * some cycles by parceling out system interrupts to different
 * IP lines, but keep it simple for bringup.  We'll also direct
 * all interrupts to a single CPU; we should probably route
 * PCI and LDT to one cpu and everything else to the other
 * to balance the load a bit.
 *
 * On the second cpu, everything is set to IP5, which is
 * ignored, EXCEPT the mailbox interrupt.  That one is
 * set to IP[2] so it is handled.  This is needed so we
 * can do cross-cpu function calls, as required by SMP
 */

#define IMR_IP2_VAL	K_INT_MAP_I0
#define IMR_IP3_VAL	K_INT_MAP_I1
#define IMR_IP4_VAL	K_INT_MAP_I2
#define IMR_IP5_VAL	K_INT_MAP_I3
#define IMR_IP6_VAL	K_INT_MAP_I4

void __init arch_init_irq(void)
{

	unsigned int i;
	u64 tmp;
	unsigned int imask = STATUSF_IP4 | STATUSF_IP3 | STATUSF_IP2 |
		STATUSF_IP1 | STATUSF_IP0;

	/* Default everything to IP2 */
	for (i = 0; i < SB1250_NR_IRQS; i++) {	/* was I0 */
		__raw_writeq(IMR_IP2_VAL,
			     IOADDR(A_IMR_REGISTER(0,
						   R_IMR_INTERRUPT_MAP_BASE) +
				    (i << 3)));
		__raw_writeq(IMR_IP2_VAL,
			     IOADDR(A_IMR_REGISTER(1,
						   R_IMR_INTERRUPT_MAP_BASE) +
				    (i << 3)));
	}

	init_sb1250_irqs();

	/*
	 * Map the high 16 bits of the mailbox registers to IP[3], for
	 * inter-cpu messages
	 */
	/* Was I1 */
	__raw_writeq(IMR_IP3_VAL,
		     IOADDR(A_IMR_REGISTER(0, R_IMR_INTERRUPT_MAP_BASE) +
			    (K_INT_MBOX_0 << 3)));
	__raw_writeq(IMR_IP3_VAL,
		     IOADDR(A_IMR_REGISTER(1, R_IMR_INTERRUPT_MAP_BASE) +
			    (K_INT_MBOX_0 << 3)));

	/* Clear the mailboxes.	 The firmware may leave them dirty */
	__raw_writeq(0xffffffffffffffffULL,
		     IOADDR(A_IMR_REGISTER(0, R_IMR_MAILBOX_CLR_CPU)));
	__raw_writeq(0xffffffffffffffffULL,
		     IOADDR(A_IMR_REGISTER(1, R_IMR_MAILBOX_CLR_CPU)));

	/* Mask everything except the mailbox registers for both cpus */
	tmp = ~((u64) 0) ^ (((u64) 1) << K_INT_MBOX_0);
	__raw_writeq(tmp, IOADDR(A_IMR_REGISTER(0, R_IMR_INTERRUPT_MASK)));
	__raw_writeq(tmp, IOADDR(A_IMR_REGISTER(1, R_IMR_INTERRUPT_MASK)));

	/*
	 * Note that the timer interrupts are also mapped, but this is
	 * done in sb1250_time_init().	Also, the profiling driver
	 * does its own management of IP7.
	 */

	/* Enable necessary IPs, disable the rest */
	change_c0_status(ST0_IM, imask);
}

extern void sb1250_mailbox_interrupt(void);

static inline void dispatch_ip2(void)
{
	unsigned int cpu = smp_processor_id();
	unsigned long long mask;

	/*
	 * Default...we've hit an IP[2] interrupt, which means we've got to
	 * check the 1250 interrupt registers to figure out what to do.	 Need
	 * to detect which CPU we're on, now that smp_affinity is supported.
	 */
	mask = __raw_readq(IOADDR(A_IMR_REGISTER(cpu,
				  R_IMR_INTERRUPT_STATUS_BASE)));
	if (mask)
		do_IRQ(fls64(mask) - 1);
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int cpu = smp_processor_id();
	unsigned int pending;

	/*
	 * What a pain. We have to be really careful saving the upper 32 bits
	 * of any * register across function calls if we don't want them
	 * trashed--since were running in -o32, the calling routing never saves
	 * the full 64 bits of a register across a function call.  Being the
	 * interrupt handler, we're guaranteed that interrupts are disabled
	 * during this code so we don't have to worry about random interrupts
	 * blasting the high 32 bits.
	 */

	pending = read_c0_cause() & read_c0_status() & ST0_IM;

	if (pending & CAUSEF_IP7) /* CPU performance counter interrupt */
		do_IRQ(MIPS_CPU_IRQ_BASE + 7);
	else if (pending & CAUSEF_IP4)
		do_IRQ(K_INT_TIMER_0 + cpu);	/* sb1250_timer_interrupt() */

#ifdef CONFIG_SMP
	else if (pending & CAUSEF_IP3)
		sb1250_mailbox_interrupt();
#endif

	else if (pending & CAUSEF_IP2)
		dispatch_ip2();
	else
		spurious_interrupt();
}
