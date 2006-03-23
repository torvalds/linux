/*
 *
 * Copyright (C) 2005 Embedded Alley Solutions, Inc
 * Ported to 2.6.
 *
 * Per Hallsmark, per.hallsmark@mvista.com
 * Copyright (C) 2000, 2001 MIPS Technologies, Inc.
 * Copyright (C) 2001 Ralf Baechle
 *
 * Cleaned up and bug fixing: Pete Popov, ppopov@embeddedalley.com
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/random.h>
#include <linux/module.h>

#include <asm/io.h>
#include <asm/gdb-stub.h>
#include <int.h>
#include <uart.h>

extern asmlinkage void cp0_irqdispatch(void);

static DEFINE_SPINLOCK(irq_lock);

/* default prio for interrupts */
/* first one is a no-no so therefore always prio 0 (disabled) */
static char gic_prio[PNX8550_INT_GIC_TOTINT] = {
	0, 1, 1, 1, 1, 15, 1, 1, 1, 1,	//   0 -  9
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	//  10 - 19
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	//  20 - 29
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	//  30 - 39
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	//  40 - 49
	1, 1, 1, 1, 1, 1, 1, 1, 2, 1,	//  50 - 59
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	//  60 - 69
	1			//  70
};

void hw0_irqdispatch(int irq, struct pt_regs *regs)
{
	/* find out which interrupt */
	irq = PNX8550_GIC_VECTOR_0 >> 3;

	if (irq == 0) {
		printk("hw0_irqdispatch: irq 0, spurious interrupt?\n");
		return;
	}
	do_IRQ(PNX8550_INT_GIC_MIN + irq, regs);
}


void timer_irqdispatch(int irq, struct pt_regs *regs)
{
	irq = (0x01c0 & read_c0_config7()) >> 6;

	if (irq == 0) {
		printk("timer_irqdispatch: irq 0, spurious interrupt?\n");
		return;
	}

	if (irq & 0x1) {
		do_IRQ(PNX8550_INT_TIMER1, regs);
	}
	if (irq & 0x2) {
		do_IRQ(PNX8550_INT_TIMER2, regs);
	}
	if (irq & 0x4) {
		do_IRQ(PNX8550_INT_TIMER3, regs);
	}
}

static inline void modify_cp0_intmask(unsigned clr_mask, unsigned set_mask)
{
	unsigned long status = read_c0_status();

	status &= ~((clr_mask & 0xFF) << 8);
	status |= (set_mask & 0xFF) << 8;

	write_c0_status(status);
}

static inline void mask_gic_int(unsigned int irq_nr)
{
	/* interrupt disabled, bit 26(WE_ENABLE)=1 and bit 16(enable)=0 */
	PNX8550_GIC_REQ(irq_nr) = 1<<28; /* set priority to 0 */
}

static inline void unmask_gic_int(unsigned int irq_nr)
{
	/* set prio mask to lower four bits and enable interrupt */
	PNX8550_GIC_REQ(irq_nr) = (1<<26 | 1<<16) | (1<<28) | gic_prio[irq_nr];
}

static inline void mask_irq(unsigned int irq_nr)
{
	if ((PNX8550_INT_CP0_MIN <= irq_nr) && (irq_nr <= PNX8550_INT_CP0_MAX)) {
		modify_cp0_intmask(1 << irq_nr, 0);
	} else if ((PNX8550_INT_GIC_MIN <= irq_nr) &&
		(irq_nr <= PNX8550_INT_GIC_MAX)) {
		mask_gic_int(irq_nr - PNX8550_INT_GIC_MIN);
	} else if ((PNX8550_INT_TIMER_MIN <= irq_nr) &&
		(irq_nr <= PNX8550_INT_TIMER_MAX)) {
		modify_cp0_intmask(1 << 7, 0);
	} else {
		printk("mask_irq: irq %d doesn't exist!\n", irq_nr);
	}
}

static inline void unmask_irq(unsigned int irq_nr)
{
	if ((PNX8550_INT_CP0_MIN <= irq_nr) && (irq_nr <= PNX8550_INT_CP0_MAX)) {
		modify_cp0_intmask(0, 1 << irq_nr);
	} else if ((PNX8550_INT_GIC_MIN <= irq_nr) &&
		(irq_nr <= PNX8550_INT_GIC_MAX)) {
		unmask_gic_int(irq_nr - PNX8550_INT_GIC_MIN);
	} else if ((PNX8550_INT_TIMER_MIN <= irq_nr) &&
		(irq_nr <= PNX8550_INT_TIMER_MAX)) {
		modify_cp0_intmask(0, 1 << 7);
	} else {
		printk("mask_irq: irq %d doesn't exist!\n", irq_nr);
	}
}

#define pnx8550_disable pnx8550_ack
static void pnx8550_ack(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_lock, flags);
	mask_irq(irq);
	spin_unlock_irqrestore(&irq_lock, flags);
}

#define pnx8550_enable pnx8550_unmask
static void pnx8550_unmask(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_lock, flags);
	unmask_irq(irq);
	spin_unlock_irqrestore(&irq_lock, flags);
}

static unsigned int startup_irq(unsigned int irq_nr)
{
	pnx8550_unmask(irq_nr);
	return 0;
}

static void shutdown_irq(unsigned int irq_nr)
{
	pnx8550_ack(irq_nr);
	return;
}

int pnx8550_set_gic_priority(int irq, int priority)
{
	int gic_irq = irq-PNX8550_INT_GIC_MIN;
	int prev_priority = PNX8550_GIC_REQ(gic_irq) & 0xf;

        gic_prio[gic_irq] = priority;
	PNX8550_GIC_REQ(gic_irq) |= (0x10000000 | gic_prio[gic_irq]);

	return prev_priority;
}

static inline void mask_and_ack_level_irq(unsigned int irq)
{
	pnx8550_disable(irq);
	return;
}

static void end_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS))) {
		pnx8550_enable(irq);
	}
}

static struct hw_interrupt_type level_irq_type = {
	.typename =	"PNX Level IRQ",
	.startup =	startup_irq,
	.shutdown =	shutdown_irq,
	.enable =	pnx8550_enable,
	.disable =	pnx8550_disable,
	.ack =		mask_and_ack_level_irq,
	.end =		end_irq,
};

static struct irqaction gic_action = {
	.handler =	no_action,
	.flags =	SA_INTERRUPT,
	.name =		"GIC",
};

static struct irqaction timer_action = {
	.handler =	no_action,
	.flags =	SA_INTERRUPT,
	.name =		"Timer",
};

void __init arch_init_irq(void)
{
	int i;
	int configPR;

	/* init of cp0 interrupts */
	set_except_vector(0, cp0_irqdispatch);

	for (i = 0; i < PNX8550_INT_CP0_TOTINT; i++) {
		irq_desc[i].handler = &level_irq_type;
		pnx8550_ack(i);	/* mask the irq just in case  */
	}

	/* init of GIC/IPC interrupts */
	/* should be done before cp0 since cp0 init enables the GIC int */
	for (i = PNX8550_INT_GIC_MIN; i <= PNX8550_INT_GIC_MAX; i++) {
		int gic_int_line = i - PNX8550_INT_GIC_MIN;
		if (gic_int_line == 0 )
			continue;	// don't fiddle with int 0
		/*
		 * enable change of TARGET, ENABLE and ACTIVE_LOW bits
		 * set TARGET        0 to route through hw0 interrupt
		 * set ACTIVE_LOW    0 active high  (correct?)
		 *
		 * We really should setup an interrupt description table
		 * to do this nicely.
		 * Note, PCI INTA is active low on the bus, but inverted
		 * in the GIC, so to us it's active high.
		 */
#ifdef CONFIG_PNX8550_V2PCI
		if (gic_int_line == (PNX8550_INT_GPIO0 - PNX8550_INT_GIC_MIN)) {
			/* PCI INT through gpio 8, which is setup in
			 * pnx8550_setup.c and routed to GPIO
			 * Interrupt Level 0 (GPIO Connection 58).
			 * Set it active low. */

			PNX8550_GIC_REQ(gic_int_line) = 0x1E020000;
		} else
#endif
		{
			PNX8550_GIC_REQ(i - PNX8550_INT_GIC_MIN) = 0x1E000000;
		}

		/* mask/priority is still 0 so we will not get any
		 * interrupts until it is unmasked */

		irq_desc[i].handler = &level_irq_type;
	}

	/* Priority level 0 */
	PNX8550_GIC_PRIMASK_0 = PNX8550_GIC_PRIMASK_1 = 0;

	/* Set int vector table address */
	PNX8550_GIC_VECTOR_0 = PNX8550_GIC_VECTOR_1 = 0;

	irq_desc[MIPS_CPU_GIC_IRQ].handler = &level_irq_type;
	setup_irq(MIPS_CPU_GIC_IRQ, &gic_action);

	/* init of Timer interrupts */
	for (i = PNX8550_INT_TIMER_MIN; i <= PNX8550_INT_TIMER_MAX; i++) {
		irq_desc[i].handler = &level_irq_type;
	}

	/* Stop Timer 1-3 */
	configPR = read_c0_config7();
	configPR |= 0x00000038;
	write_c0_config7(configPR);

	irq_desc[MIPS_CPU_TIMER_IRQ].handler = &level_irq_type;
	setup_irq(MIPS_CPU_TIMER_IRQ, &timer_action);
}

EXPORT_SYMBOL(pnx8550_set_gic_priority);
