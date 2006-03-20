/*
 * Copyright (C) 2000,2001,2002,2003,2004 Broadcom Corporation
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
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/kernel_stat.h>

#include <asm/errno.h>
#include <asm/signal.h>
#include <asm/system.h>
#include <asm/ptrace.h>
#include <asm/io.h>

#include <asm/sibyte/bcm1480_regs.h>
#include <asm/sibyte/bcm1480_int.h>
#include <asm/sibyte/bcm1480_scd.h>

#include <asm/sibyte/sb1250_uart.h>
#include <asm/sibyte/sb1250.h>

/*
 * These are the routines that handle all the low level interrupt stuff.
 * Actions handled here are: initialization of the interrupt map, requesting of
 * interrupt lines by handlers, dispatching if interrupts to handlers, probing
 * for interrupt lines
 */


#define shutdown_bcm1480_irq	disable_bcm1480_irq
static void end_bcm1480_irq(unsigned int irq);
static void enable_bcm1480_irq(unsigned int irq);
static void disable_bcm1480_irq(unsigned int irq);
static unsigned int startup_bcm1480_irq(unsigned int irq);
static void ack_bcm1480_irq(unsigned int irq);
#ifdef CONFIG_SMP
static void bcm1480_set_affinity(unsigned int irq, cpumask_t mask);
#endif

#ifdef CONFIG_PCI
extern unsigned long ht_eoi_space;
#endif

#ifdef CONFIG_KGDB
#include <asm/gdb-stub.h>
extern void breakpoint(void);
static int kgdb_irq;
#ifdef CONFIG_GDB_CONSOLE
extern void register_gdb_console(void);
#endif

/* kgdb is on when configured.  Pass "nokgdb" kernel arg to turn it off */
static int kgdb_flag = 1;
static int __init nokgdb(char *str)
{
	kgdb_flag = 0;
	return 1;
}
__setup("nokgdb", nokgdb);

/* Default to UART1 */
int kgdb_port = 1;
#ifdef CONFIG_SIBYTE_SB1250_DUART
extern char sb1250_duart_present[];
#endif
#endif

static struct hw_interrupt_type bcm1480_irq_type = {
	.typename = "BCM1480-IMR",
	.startup = startup_bcm1480_irq,
	.shutdown = shutdown_bcm1480_irq,
	.enable = enable_bcm1480_irq,
	.disable = disable_bcm1480_irq,
	.ack = ack_bcm1480_irq,
	.end = end_bcm1480_irq,
#ifdef CONFIG_SMP
	.set_affinity = bcm1480_set_affinity
#endif
};

/* Store the CPU id (not the logical number) */
int bcm1480_irq_owner[BCM1480_NR_IRQS];

DEFINE_SPINLOCK(bcm1480_imr_lock);

void bcm1480_mask_irq(int cpu, int irq)
{
	unsigned long flags;
	u64 cur_ints,hl_spacing;

	spin_lock_irqsave(&bcm1480_imr_lock, flags);
	hl_spacing = 0;
	if ((irq >= BCM1480_NR_IRQS_HALF) && (irq <= BCM1480_NR_IRQS)) {
		hl_spacing = BCM1480_IMR_HL_SPACING;
		irq -= BCM1480_NR_IRQS_HALF;
	}
	cur_ints = ____raw_readq(IOADDR(A_BCM1480_IMR_MAPPER(cpu) + R_BCM1480_IMR_INTERRUPT_MASK_H + hl_spacing));
	cur_ints |= (((u64) 1) << irq);
	____raw_writeq(cur_ints, IOADDR(A_BCM1480_IMR_MAPPER(cpu) + R_BCM1480_IMR_INTERRUPT_MASK_H + hl_spacing));
	spin_unlock_irqrestore(&bcm1480_imr_lock, flags);
}

void bcm1480_unmask_irq(int cpu, int irq)
{
	unsigned long flags;
	u64 cur_ints,hl_spacing;

	spin_lock_irqsave(&bcm1480_imr_lock, flags);
	hl_spacing = 0;
	if ((irq >= BCM1480_NR_IRQS_HALF) && (irq <= BCM1480_NR_IRQS)) {
		hl_spacing = BCM1480_IMR_HL_SPACING;
		irq -= BCM1480_NR_IRQS_HALF;
	}
	cur_ints = ____raw_readq(IOADDR(A_BCM1480_IMR_MAPPER(cpu) + R_BCM1480_IMR_INTERRUPT_MASK_H + hl_spacing));
	cur_ints &= ~(((u64) 1) << irq);
	____raw_writeq(cur_ints, IOADDR(A_BCM1480_IMR_MAPPER(cpu) + R_BCM1480_IMR_INTERRUPT_MASK_H + hl_spacing));
	spin_unlock_irqrestore(&bcm1480_imr_lock, flags);
}

#ifdef CONFIG_SMP
static void bcm1480_set_affinity(unsigned int irq, cpumask_t mask)
{
	int i = 0, old_cpu, cpu, int_on, k;
	u64 cur_ints;
	irq_desc_t *desc = irq_desc + irq;
	unsigned long flags;
	unsigned int irq_dirty;

	i = first_cpu(mask);
	if (next_cpu(i, mask) <= NR_CPUS) {
		printk("attempted to set irq affinity for irq %d to multiple CPUs\n", irq);
		return;
	}

	/* Convert logical CPU to physical CPU */
	cpu = cpu_logical_map(i);

	/* Protect against other affinity changers and IMR manipulation */
	spin_lock_irqsave(&desc->lock, flags);
	spin_lock(&bcm1480_imr_lock);

	/* Swizzle each CPU's IMR (but leave the IP selection alone) */
	old_cpu = bcm1480_irq_owner[irq];
	irq_dirty = irq;
	if ((irq_dirty >= BCM1480_NR_IRQS_HALF) && (irq_dirty <= BCM1480_NR_IRQS)) {
		irq_dirty -= BCM1480_NR_IRQS_HALF;
	}

	for (k=0; k<2; k++) { /* Loop through high and low interrupt mask register */
		cur_ints = ____raw_readq(IOADDR(A_BCM1480_IMR_MAPPER(old_cpu) + R_BCM1480_IMR_INTERRUPT_MASK_H + (k*BCM1480_IMR_HL_SPACING)));
		int_on = !(cur_ints & (((u64) 1) << irq_dirty));
		if (int_on) {
			/* If it was on, mask it */
			cur_ints |= (((u64) 1) << irq_dirty);
			____raw_writeq(cur_ints, IOADDR(A_BCM1480_IMR_MAPPER(old_cpu) + R_BCM1480_IMR_INTERRUPT_MASK_H + (k*BCM1480_IMR_HL_SPACING)));
		}
		bcm1480_irq_owner[irq] = cpu;
		if (int_on) {
			/* unmask for the new CPU */
			cur_ints = ____raw_readq(IOADDR(A_BCM1480_IMR_MAPPER(cpu) + R_BCM1480_IMR_INTERRUPT_MASK_H + (k*BCM1480_IMR_HL_SPACING)));
			cur_ints &= ~(((u64) 1) << irq_dirty);
			____raw_writeq(cur_ints, IOADDR(A_BCM1480_IMR_MAPPER(cpu) + R_BCM1480_IMR_INTERRUPT_MASK_H + (k*BCM1480_IMR_HL_SPACING)));
		}
	}
	spin_unlock(&bcm1480_imr_lock);
	spin_unlock_irqrestore(&desc->lock, flags);
}
#endif


/* Defined in arch/mips/sibyte/bcm1480/irq_handler.S */
extern void bcm1480_irq_handler(void);

/*****************************************************************************/

static unsigned int startup_bcm1480_irq(unsigned int irq)
{
	bcm1480_unmask_irq(bcm1480_irq_owner[irq], irq);

	return 0;		/* never anything pending */
}


static void disable_bcm1480_irq(unsigned int irq)
{
	bcm1480_mask_irq(bcm1480_irq_owner[irq], irq);
}

static void enable_bcm1480_irq(unsigned int irq)
{
	bcm1480_unmask_irq(bcm1480_irq_owner[irq], irq);
}


static void ack_bcm1480_irq(unsigned int irq)
{
	u64 pending;
	unsigned int irq_dirty;
	int k;

	/*
	 * If the interrupt was an HT interrupt, now is the time to
	 * clear it.  NOTE: we assume the HT bridge was set up to
	 * deliver the interrupts to all CPUs (which makes affinity
	 * changing easier for us)
	 */
	irq_dirty = irq;
	if ((irq_dirty >= BCM1480_NR_IRQS_HALF) && (irq_dirty <= BCM1480_NR_IRQS)) {
		irq_dirty -= BCM1480_NR_IRQS_HALF;
	}
	for (k=0; k<2; k++) { /* Loop through high and low LDT interrupts */
		pending = __raw_readq(IOADDR(A_BCM1480_IMR_REGISTER(bcm1480_irq_owner[irq],
						R_BCM1480_IMR_LDT_INTERRUPT_H + (k*BCM1480_IMR_HL_SPACING))));
		pending &= ((u64)1 << (irq_dirty));
		if (pending) {
#ifdef CONFIG_SMP
			int i;
			for (i=0; i<NR_CPUS; i++) {
				/*
				 * Clear for all CPUs so an affinity switch
				 * doesn't find an old status
				 */
				__raw_writeq(pending, IOADDR(A_BCM1480_IMR_REGISTER(cpu_logical_map(i),
								R_BCM1480_IMR_LDT_INTERRUPT_CLR_H + (k*BCM1480_IMR_HL_SPACING))));
			}
#else
			__raw_writeq(pending, IOADDR(A_BCM1480_IMR_REGISTER(0, R_BCM1480_IMR_LDT_INTERRUPT_CLR_H + (k*BCM1480_IMR_HL_SPACING))));
#endif

			/*
			 * Generate EOI.  For Pass 1 parts, EOI is a nop.  For
			 * Pass 2, the LDT world may be edge-triggered, but
			 * this EOI shouldn't hurt.  If they are
			 * level-sensitive, the EOI is required.
			 */
#ifdef CONFIG_PCI
			if (ht_eoi_space)
				*(uint32_t *)(ht_eoi_space+(irq<<16)+(7<<2)) = 0;
#endif
		}
	}
	bcm1480_mask_irq(bcm1480_irq_owner[irq], irq);
}


static void end_bcm1480_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS))) {
		bcm1480_unmask_irq(bcm1480_irq_owner[irq], irq);
	}
}


void __init init_bcm1480_irqs(void)
{
	int i;

	for (i = 0; i < NR_IRQS; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = 0;
		irq_desc[i].depth = 1;
		if (i < BCM1480_NR_IRQS) {
			irq_desc[i].handler = &bcm1480_irq_type;
			bcm1480_irq_owner[i] = 0;
		} else {
			irq_desc[i].handler = &no_irq_type;
		}
	}
}


static irqreturn_t bcm1480_dummy_handler(int irq, void *dev_id,
	struct pt_regs *regs)
{
	return IRQ_NONE;
}

static struct irqaction bcm1480_dummy_action = {
	.handler = bcm1480_dummy_handler,
	.flags   = 0,
	.mask    = CPU_MASK_NONE,
	.name    = "bcm1480-private",
	.next    = NULL,
	.dev_id  = 0
};

int bcm1480_steal_irq(int irq)
{
	irq_desc_t *desc = irq_desc + irq;
	unsigned long flags;
	int retval = 0;

	if (irq >= BCM1480_NR_IRQS)
		return -EINVAL;

	spin_lock_irqsave(&desc->lock,flags);
	/* Don't allow sharing at all for these */
	if (desc->action != NULL)
		retval = -EBUSY;
	else {
		desc->action = &bcm1480_dummy_action;
		desc->depth = 0;
	}
	spin_unlock_irqrestore(&desc->lock,flags);
	return 0;
}

/*
 *  init_IRQ is called early in the boot sequence from init/main.c.  It
 *  is responsible for setting up the interrupt mapper and installing the
 *  handler that will be responsible for dispatching interrupts to the
 *  "right" place.
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

#define IMR_IP2_VAL	K_BCM1480_INT_MAP_I0
#define IMR_IP3_VAL	K_BCM1480_INT_MAP_I1
#define IMR_IP4_VAL	K_BCM1480_INT_MAP_I2
#define IMR_IP5_VAL	K_BCM1480_INT_MAP_I3
#define IMR_IP6_VAL	K_BCM1480_INT_MAP_I4

void __init arch_init_irq(void)
{

	unsigned int i, cpu;
	u64 tmp;
	unsigned int imask = STATUSF_IP4 | STATUSF_IP3 | STATUSF_IP2 |
		STATUSF_IP1 | STATUSF_IP0;

	/* Default everything to IP2 */
	/* Start with _high registers which has no bit 0 interrupt source */
	for (i = 1; i < BCM1480_NR_IRQS_HALF; i++) {	/* was I0 */
		for (cpu = 0; cpu < 4; cpu++) {
			__raw_writeq(IMR_IP2_VAL,
				     IOADDR(A_BCM1480_IMR_REGISTER(cpu,
								   R_BCM1480_IMR_INTERRUPT_MAP_BASE_H) + (i << 3)));
		}
	}

	/* Now do _low registers */
	for (i = 0; i < BCM1480_NR_IRQS_HALF; i++) {
		for (cpu = 0; cpu < 4; cpu++) {
			__raw_writeq(IMR_IP2_VAL,
				     IOADDR(A_BCM1480_IMR_REGISTER(cpu,
								   R_BCM1480_IMR_INTERRUPT_MAP_BASE_L) + (i << 3)));
		}
	}

	init_bcm1480_irqs();

	/*
	 * Map the high 16 bits of mailbox_0 registers to IP[3], for
	 * inter-cpu messages
	 */
	/* Was I1 */
	for (cpu = 0; cpu < 4; cpu++) {
		__raw_writeq(IMR_IP3_VAL, IOADDR(A_BCM1480_IMR_REGISTER(cpu, R_BCM1480_IMR_INTERRUPT_MAP_BASE_H) +
						 (K_BCM1480_INT_MBOX_0_0 << 3)));
        }


	/* Clear the mailboxes.  The firmware may leave them dirty */
	for (cpu = 0; cpu < 4; cpu++) {
		__raw_writeq(0xffffffffffffffffULL,
			     IOADDR(A_BCM1480_IMR_REGISTER(cpu, R_BCM1480_IMR_MAILBOX_0_CLR_CPU)));
		__raw_writeq(0xffffffffffffffffULL,
			     IOADDR(A_BCM1480_IMR_REGISTER(cpu, R_BCM1480_IMR_MAILBOX_1_CLR_CPU)));
	}


	/* Mask everything except the high 16 bit of mailbox_0 registers for all cpus */
	tmp = ~((u64) 0) ^ ( (((u64) 1) << K_BCM1480_INT_MBOX_0_0));
	for (cpu = 0; cpu < 4; cpu++) {
		__raw_writeq(tmp, IOADDR(A_BCM1480_IMR_REGISTER(cpu, R_BCM1480_IMR_INTERRUPT_MASK_H)));
	}
	tmp = ~((u64) 0);
	for (cpu = 0; cpu < 4; cpu++) {
		__raw_writeq(tmp, IOADDR(A_BCM1480_IMR_REGISTER(cpu, R_BCM1480_IMR_INTERRUPT_MASK_L)));
	}

	bcm1480_steal_irq(K_BCM1480_INT_MBOX_0_0);

	/*
	 * Note that the timer interrupts are also mapped, but this is
	 * done in bcm1480_time_init().  Also, the profiling driver
	 * does its own management of IP7.
	 */

#ifdef CONFIG_KGDB
	imask |= STATUSF_IP6;
#endif
	/* Enable necessary IPs, disable the rest */
	change_c0_status(ST0_IM, imask);
	set_except_vector(0, bcm1480_irq_handler);

#ifdef CONFIG_KGDB
	if (kgdb_flag) {
		kgdb_irq = K_BCM1480_INT_UART_0 + kgdb_port;

#ifdef CONFIG_SIBYTE_SB1250_DUART
		sb1250_duart_present[kgdb_port] = 0;
#endif
		/* Setup uart 1 settings, mapper */
		/* QQQ FIXME */
		__raw_writeq(M_DUART_IMR_BRK, IO_SPACE_BASE + A_DUART_IMRREG(kgdb_port));

		bcm1480_steal_irq(kgdb_irq);
		__raw_writeq(IMR_IP6_VAL,
			     IO_SPACE_BASE + A_BCM1480_IMR_REGISTER(0, R_BCM1480_IMR_INTERRUPT_MAP_BASE_H) +
			     (kgdb_irq<<3));
		bcm1480_unmask_irq(0, kgdb_irq);

#ifdef CONFIG_GDB_CONSOLE
		register_gdb_console();
#endif
		prom_printf("Waiting for GDB on UART port %d\n", kgdb_port);
		set_debug_traps();
		breakpoint();
	}
#endif
}

#ifdef CONFIG_KGDB

#include <linux/delay.h>

#define duart_out(reg, val)     csr_out32(val, IOADDR(A_DUART_CHANREG(kgdb_port,reg)))
#define duart_in(reg)           csr_in32(IOADDR(A_DUART_CHANREG(kgdb_port,reg)))

void bcm1480_kgdb_interrupt(struct pt_regs *regs)
{
	/*
	 * Clear break-change status (allow some time for the remote
	 * host to stop the break, since we would see another
	 * interrupt on the end-of-break too)
	 */
	kstat.irqs[smp_processor_id()][kgdb_irq]++;
	mdelay(500);
	duart_out(R_DUART_CMD, V_DUART_MISC_CMD_RESET_BREAK_INT |
				M_DUART_RX_EN | M_DUART_TX_EN);
	set_async_breakpoint(&regs->cp0_epc);
}

#endif 	/* CONFIG_KGDB */
