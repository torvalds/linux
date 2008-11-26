/*
 * Toshiba RBTX4939 interrupt routines
 * Based on linux/arch/mips/txx9/rbtx4938/irq.c,
 *	    and RBTX49xx patch from CELF patch archive.
 *
 * Copyright (C) 2000-2001,2005-2006 Toshiba Corporation
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/mipsregs.h>
#include <asm/txx9/rbtx4939.h>

/*
 * RBTX4939 IOC controller definition
 */

static void rbtx4939_ioc_irq_unmask(unsigned int irq)
{
	int ioc_nr = irq - RBTX4939_IRQ_IOC;

	writeb(readb(rbtx4939_ien_addr) | (1 << ioc_nr), rbtx4939_ien_addr);
}

static void rbtx4939_ioc_irq_mask(unsigned int irq)
{
	int ioc_nr = irq - RBTX4939_IRQ_IOC;

	writeb(readb(rbtx4939_ien_addr) & ~(1 << ioc_nr), rbtx4939_ien_addr);
	mmiowb();
}

static struct irq_chip rbtx4939_ioc_irq_chip = {
	.name		= "IOC",
	.ack		= rbtx4939_ioc_irq_mask,
	.mask		= rbtx4939_ioc_irq_mask,
	.mask_ack	= rbtx4939_ioc_irq_mask,
	.unmask		= rbtx4939_ioc_irq_unmask,
};


static inline int rbtx4939_ioc_irqroute(void)
{
	unsigned char istat = readb(rbtx4939_ifac2_addr);

	if (unlikely(istat == 0))
		return -1;
	return RBTX4939_IRQ_IOC + __fls8(istat);
}

static int rbtx4939_irq_dispatch(int pending)
{
	int irq;

	if (pending & CAUSEF_IP7)
		return MIPS_CPU_IRQ_BASE + 7;
	irq = tx4939_irq();
	if (likely(irq >= 0)) {
		/* redirect IOC interrupts */
		switch (irq) {
		case RBTX4939_IRQ_IOCINT:
			irq = rbtx4939_ioc_irqroute();
			break;
		}
	} else if (pending & CAUSEF_IP0)
		irq = MIPS_CPU_IRQ_BASE + 0;
	else if (pending & CAUSEF_IP1)
		irq = MIPS_CPU_IRQ_BASE + 1;
	else
		irq = -1;
	return irq;
}

void __init rbtx4939_irq_setup(void)
{
	int i;

	/* mask all IOC interrupts */
	writeb(0, rbtx4939_ien_addr);

	/* clear SoftInt interrupts */
	writeb(0, rbtx4939_softint_addr);

	txx9_irq_dispatch = rbtx4939_irq_dispatch;

	tx4939_irq_init();
	for (i = RBTX4939_IRQ_IOC;
	     i < RBTX4939_IRQ_IOC + RBTX4939_NR_IRQ_IOC; i++)
		set_irq_chip_and_handler(i, &rbtx4939_ioc_irq_chip,
					 handle_level_irq);

	set_irq_chained_handler(RBTX4939_IRQ_IOCINT, handle_simple_irq);
}
