/*
 * Copyright (C) 2000, 2001, 2002, 2003 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/kernel_stat.h>

#include <asm/errno.h>
#include <asm/signal.h>
#include <asm/system.h>
#include <asm/ptrace.h>
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


#define shutdown_sb1250_irq	disable_sb1250_irq
static void end_sb1250_irq(unsigned int irq);
static void enable_sb1250_irq(unsigned int irq);
static void disable_sb1250_irq(unsigned int irq);
static unsigned int startup_sb1250_irq(unsigned int irq);
static void ack_sb1250_irq(unsigned int irq);
#ifdef CONFIG_SMP
static void sb1250_set_affinity(unsigned int irq, unsigned long mask);
#endif

#ifdef CONFIG_SIBYTE_HAS_LDT
extern unsigned long ldt_eoi_space;
#endif

#ifdef CONFIG_KGDB
static int kgdb_irq;

/* Default to UART1 */
int kgdb_port = 1;
#ifdef CONFIG_SIBYTE_SB1250_DUART
extern char sb1250_duart_present[];
#endif
#endif

static struct hw_interrupt_type sb1250_irq_type = {
	"SB1250-IMR",
	startup_sb1250_irq,
	shutdown_sb1250_irq,
	enable_sb1250_irq,
	disable_sb1250_irq,
	ack_sb1250_irq,
	end_sb1250_irq,
#ifdef CONFIG_SMP
	sb1250_set_affinity
#else
	NULL
#endif
};

/* Store the CPU id (not the logical number) */
int sb1250_irq_owner[SB1250_NR_IRQS];

DEFINE_SPINLOCK(sb1250_imr_lock);

void sb1250_mask_irq(int cpu, int irq)
{
	unsigned long flags;
	u64 cur_ints;

	spin_lock_irqsave(&sb1250_imr_lock, flags);
	cur_ints = __bus_readq(IOADDR(A_IMR_MAPPER(cpu) +
				      R_IMR_INTERRUPT_MASK));
	cur_ints |= (((u64) 1) << irq);
	__bus_writeq(cur_ints, IOADDR(A_IMR_MAPPER(cpu) +
				      R_IMR_INTERRUPT_MASK));
	spin_unlock_irqrestore(&sb1250_imr_lock, flags);
}

void sb1250_unmask_irq(int cpu, int irq)
{
	unsigned long flags;
	u64 cur_ints;

	spin_lock_irqsave(&sb1250_imr_lock, flags);
	cur_ints = __bus_readq(IOADDR(A_IMR_MAPPER(cpu) +
				      R_IMR_INTERRUPT_MASK));
	cur_ints &= ~(((u64) 1) << irq);
	__bus_writeq(cur_ints, IOADDR(A_IMR_MAPPER(cpu) +
				      R_IMR_INTERRUPT_MASK));
	spin_unlock_irqrestore(&sb1250_imr_lock, flags);
}

#ifdef CONFIG_SMP
static void sb1250_set_affinity(unsigned int irq, unsigned long mask)
{
	int i = 0, old_cpu, cpu, int_on;
	u64 cur_ints;
	irq_desc_t *desc = irq_desc + irq;
	unsigned long flags;

	while (mask) {
		if (mask & 1) {
			mask >>= 1;
			break;
		}
		mask >>= 1;
		i++;
	}

	if (mask) {
		printk("attempted to set irq affinity for irq %d to multiple CPUs\n", irq);
		return;
	}

	/* Convert logical CPU to physical CPU */
	cpu = cpu_logical_map(i);

	/* Protect against other affinity changers and IMR manipulation */
	spin_lock_irqsave(&desc->lock, flags);
	spin_lock(&sb1250_imr_lock);

	/* Swizzle each CPU's IMR (but leave the IP selection alone) */
	old_cpu = sb1250_irq_owner[irq];
	cur_ints = __bus_readq(IOADDR(A_IMR_MAPPER(old_cpu) +
			       R_IMR_INTERRUPT_MASK));
	int_on = !(cur_ints & (((u64) 1) << irq));
	if (int_on) {
		/* If it was on, mask it */
		cur_ints |= (((u64) 1) << irq);
		__bus_writeq(cur_ints, IOADDR(A_IMR_MAPPER(old_cpu) +
					      R_IMR_INTERRUPT_MASK));
	}
	sb1250_irq_owner[irq] = cpu;
	if (int_on) {
		/* unmask for the new CPU */
		cur_ints = __bus_readq(IOADDR(A_IMR_MAPPER(cpu) +
				       R_IMR_INTERRUPT_MASK));
		cur_ints &= ~(((u64) 1) << irq);
		__bus_writeq(cur_ints, IOADDR(A_IMR_MAPPER(cpu) +
					      R_IMR_INTERRUPT_MASK));
	}
	spin_unlock(&sb1250_imr_lock);
	spin_unlock_irqrestore(&desc->lock, flags);
}
#endif


/* Defined in arch/mips/sibyte/sb1250/irq_handler.S */
extern void sb1250_irq_handler(void);

/*****************************************************************************/

static unsigned int startup_sb1250_irq(unsigned int irq)
{
	sb1250_unmask_irq(sb1250_irq_owner[irq], irq);

	return 0;		/* never anything pending */
}


static void disable_sb1250_irq(unsigned int irq)
{
	sb1250_mask_irq(sb1250_irq_owner[irq], irq);
}

static void enable_sb1250_irq(unsigned int irq)
{
	sb1250_unmask_irq(sb1250_irq_owner[irq], irq);
}


static void ack_sb1250_irq(unsigned int irq)
{
#ifdef CONFIG_SIBYTE_HAS_LDT
	u64 pending;

	/*
	 * If the interrupt was an HT interrupt, now is the time to
	 * clear it.  NOTE: we assume the HT bridge was set up to
	 * deliver the interrupts to all CPUs (which makes affinity
	 * changing easier for us)
	 */
	pending = bus_readq(IOADDR(A_IMR_REGISTER(sb1250_irq_owner[irq],
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
			bus_writeq(pending,
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


static void end_sb1250_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS))) {
		sb1250_unmask_irq(sb1250_irq_owner[irq], irq);
	}
}


void __init init_sb1250_irqs(void)
{
	int i;

	for (i = 0; i < NR_IRQS; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = 0;
		irq_desc[i].depth = 1;
		if (i < SB1250_NR_IRQS) {
			irq_desc[i].handler = &sb1250_irq_type;
			sb1250_irq_owner[i] = 0;
		} else {
			irq_desc[i].handler = &no_irq_type;
		}
	}
}


static irqreturn_t  sb1250_dummy_handler(int irq, void *dev_id,
	struct pt_regs *regs)
{
	return IRQ_NONE;
}

static struct irqaction sb1250_dummy_action = {
	.handler = sb1250_dummy_handler,
	.flags   = 0,
	.mask    = CPU_MASK_NONE,
	.name    = "sb1250-private",
	.next    = NULL,
	.dev_id  = 0
};

int sb1250_steal_irq(int irq)
{
	irq_desc_t *desc = irq_desc + irq;
	unsigned long flags;
	int retval = 0;

	if (irq >= SB1250_NR_IRQS)
		return -EINVAL;

	spin_lock_irqsave(&desc->lock,flags);
	/* Don't allow sharing at all for these */
	if (desc->action != NULL)
		retval = -EBUSY;
	else {
		desc->action = &sb1250_dummy_action;
		desc->depth = 0;
	}
	spin_unlock_irqrestore(&desc->lock,flags);
	return 0;
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
 * can do cross-cpu function calls, as requred by SMP
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
		bus_writeq(IMR_IP2_VAL,
			   IOADDR(A_IMR_REGISTER(0, R_IMR_INTERRUPT_MAP_BASE) +
				  (i << 3)));
		bus_writeq(IMR_IP2_VAL,
			   IOADDR(A_IMR_REGISTER(1, R_IMR_INTERRUPT_MAP_BASE) +
				  (i << 3)));
	}

	init_sb1250_irqs();

	/*
	 * Map the high 16 bits of the mailbox registers to IP[3], for
	 * inter-cpu messages
	 */
	/* Was I1 */
	bus_writeq(IMR_IP3_VAL,
		   IOADDR(A_IMR_REGISTER(0, R_IMR_INTERRUPT_MAP_BASE) +
			  (K_INT_MBOX_0 << 3)));
	bus_writeq(IMR_IP3_VAL,
		   IOADDR(A_IMR_REGISTER(1, R_IMR_INTERRUPT_MAP_BASE) +
			  (K_INT_MBOX_0 << 3)));

	/* Clear the mailboxes.  The firmware may leave them dirty */
	bus_writeq(0xffffffffffffffffULL,
		   IOADDR(A_IMR_REGISTER(0, R_IMR_MAILBOX_CLR_CPU)));
	bus_writeq(0xffffffffffffffffULL,
		   IOADDR(A_IMR_REGISTER(1, R_IMR_MAILBOX_CLR_CPU)));

	/* Mask everything except the mailbox registers for both cpus */
	tmp = ~((u64) 0) ^ (((u64) 1) << K_INT_MBOX_0);
	bus_writeq(tmp, IOADDR(A_IMR_REGISTER(0, R_IMR_INTERRUPT_MASK)));
	bus_writeq(tmp, IOADDR(A_IMR_REGISTER(1, R_IMR_INTERRUPT_MASK)));

	sb1250_steal_irq(K_INT_MBOX_0);

	/*
	 * Note that the timer interrupts are also mapped, but this is
	 * done in sb1250_time_init().  Also, the profiling driver 
	 * does its own management of IP7.
	 */

#ifdef CONFIG_KGDB
	imask |= STATUSF_IP6;
#endif
	/* Enable necessary IPs, disable the rest */
	change_c0_status(ST0_IM, imask);
	set_except_vector(0, sb1250_irq_handler);

#ifdef CONFIG_KGDB
	if (kgdb_flag) {
		kgdb_irq = K_INT_UART_0 + kgdb_port;

#ifdef CONFIG_SIBYTE_SB1250_DUART	
		sb1250_duart_present[kgdb_port] = 0;
#endif
		/* Setup uart 1 settings, mapper */
		bus_writeq(M_DUART_IMR_BRK, IOADDR(A_DUART_IMRREG(kgdb_port)));

		sb1250_steal_irq(kgdb_irq);
		bus_writeq(IMR_IP6_VAL,
			   IOADDR(A_IMR_REGISTER(0, R_IMR_INTERRUPT_MAP_BASE) +
				  (kgdb_irq<<3)));
		sb1250_unmask_irq(0, kgdb_irq);
	}
#endif
}

#ifdef CONFIG_KGDB

#include <linux/delay.h>

#define duart_out(reg, val)     csr_out32(val, IOADDR(A_DUART_CHANREG(kgdb_port,reg)))
#define duart_in(reg)           csr_in32(IOADDR(A_DUART_CHANREG(kgdb_port,reg)))

void sb1250_kgdb_interrupt(struct pt_regs *regs)
{
	/*
	 * Clear break-change status (allow some time for the remote
	 * host to stop the break, since we would see another
	 * interrupt on the end-of-break too)
	 */
	kstat_this_cpu.irqs[kgdb_irq]++;
	mdelay(500);
	duart_out(R_DUART_CMD, V_DUART_MISC_CMD_RESET_BREAK_INT |
				M_DUART_RX_EN | M_DUART_TX_EN);
	set_async_breakpoint(&regs->cp0_epc);
}

#endif 	/* CONFIG_KGDB */
