/*
 * intc.c  --  interrupt controller or ColdFire 5272 SoC
 *
 * (C) Copyright 2009, Greg Ungerer <gerg@snapgear.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/traps.h>

/*
 * The 5272 ColdFire interrupt controller is nothing like any other
 * ColdFire interrupt controller - it truly is completely different.
 * Given its age it is unlikely to be used on any other ColdFire CPU.
 */

/*
 * The masking and priproty setting of interrupts on the 5272 is done
 * via a set of 4 "Interrupt Controller Registers" (ICR). There is a
 * loose mapping of vector number to register and internal bits, but
 * a table is the easiest and quickest way to map them.
 */
struct irqmap {
	unsigned char	icr;
	unsigned char	index;
	unsigned char	ack;
};

static struct irqmap intc_irqmap[MCFINT_VECMAX - MCFINT_VECBASE] = {
	/*MCF_IRQ_SPURIOUS*/	{ .icr = 0,           .index = 0,  .ack = 0, },
	/*MCF_IRQ_EINT1*/	{ .icr = MCFSIM_ICR1, .index = 28, .ack = 1, },
	/*MCF_IRQ_EINT2*/	{ .icr = MCFSIM_ICR1, .index = 24, .ack = 1, },
	/*MCF_IRQ_EINT3*/	{ .icr = MCFSIM_ICR1, .index = 20, .ack = 1, },
	/*MCF_IRQ_EINT4*/	{ .icr = MCFSIM_ICR1, .index = 16, .ack = 1, },
	/*MCF_IRQ_TIMER1*/	{ .icr = MCFSIM_ICR1, .index = 12, .ack = 0, },
	/*MCF_IRQ_TIMER2*/	{ .icr = MCFSIM_ICR1, .index = 8,  .ack = 0, },
	/*MCF_IRQ_TIMER3*/	{ .icr = MCFSIM_ICR1, .index = 4,  .ack = 0, },
	/*MCF_IRQ_TIMER4*/	{ .icr = MCFSIM_ICR1, .index = 0,  .ack = 0, },
	/*MCF_IRQ_UART1*/	{ .icr = MCFSIM_ICR2, .index = 28, .ack = 0, },
	/*MCF_IRQ_UART2*/	{ .icr = MCFSIM_ICR2, .index = 24, .ack = 0, },
	/*MCF_IRQ_PLIP*/	{ .icr = MCFSIM_ICR2, .index = 20, .ack = 0, },
	/*MCF_IRQ_PLIA*/	{ .icr = MCFSIM_ICR2, .index = 16, .ack = 0, },
	/*MCF_IRQ_USB0*/	{ .icr = MCFSIM_ICR2, .index = 12, .ack = 0, },
	/*MCF_IRQ_USB1*/	{ .icr = MCFSIM_ICR2, .index = 8,  .ack = 0, },
	/*MCF_IRQ_USB2*/	{ .icr = MCFSIM_ICR2, .index = 4,  .ack = 0, },
	/*MCF_IRQ_USB3*/	{ .icr = MCFSIM_ICR2, .index = 0,  .ack = 0, },
	/*MCF_IRQ_USB4*/	{ .icr = MCFSIM_ICR3, .index = 28, .ack = 0, },
	/*MCF_IRQ_USB5*/	{ .icr = MCFSIM_ICR3, .index = 24, .ack = 0, },
	/*MCF_IRQ_USB6*/	{ .icr = MCFSIM_ICR3, .index = 20, .ack = 0, },
	/*MCF_IRQ_USB7*/	{ .icr = MCFSIM_ICR3, .index = 16, .ack = 0, },
	/*MCF_IRQ_DMA*/		{ .icr = MCFSIM_ICR3, .index = 12, .ack = 0, },
	/*MCF_IRQ_ERX*/		{ .icr = MCFSIM_ICR3, .index = 8,  .ack = 0, },
	/*MCF_IRQ_ETX*/		{ .icr = MCFSIM_ICR3, .index = 4,  .ack = 0, },
	/*MCF_IRQ_ENTC*/	{ .icr = MCFSIM_ICR3, .index = 0,  .ack = 0, },
	/*MCF_IRQ_QSPI*/	{ .icr = MCFSIM_ICR4, .index = 28, .ack = 0, },
	/*MCF_IRQ_EINT5*/	{ .icr = MCFSIM_ICR4, .index = 24, .ack = 1, },
	/*MCF_IRQ_EINT6*/	{ .icr = MCFSIM_ICR4, .index = 20, .ack = 1, },
	/*MCF_IRQ_SWTO*/	{ .icr = MCFSIM_ICR4, .index = 16, .ack = 0, },
};

static void intc_irq_mask(unsigned int irq)
{
	if ((irq >= MCFINT_VECBASE) && (irq <= MCFINT_VECMAX)) {
		u32 v;
		irq -= MCFINT_VECBASE;
		v = 0x8 << intc_irqmap[irq].index;
		writel(v, MCF_MBAR + intc_irqmap[irq].icr);
	}
}

static void intc_irq_unmask(unsigned int irq)
{
	if ((irq >= MCFINT_VECBASE) && (irq <= MCFINT_VECMAX)) {
		u32 v;
		irq -= MCFINT_VECBASE;
		v = 0xd << intc_irqmap[irq].index;
		writel(v, MCF_MBAR + intc_irqmap[irq].icr);
	}
}

static void intc_irq_ack(unsigned int irq)
{
	/* Only external interrupts are acked */
	if ((irq >= MCFINT_VECBASE) && (irq <= MCFINT_VECMAX)) {
		irq -= MCFINT_VECBASE;
		if (intc_irqmap[irq].ack) {
			u32 v;
			v = 0xd << intc_irqmap[irq].index;
			writel(v, MCF_MBAR + intc_irqmap[irq].icr);
		}
	}
}

static int intc_irq_set_type(unsigned int irq, unsigned int type)
{
	/* We can set the edge type here for external interrupts */
	return 0;
}

static struct irq_chip intc_irq_chip = {
	.name		= "CF-INTC",
	.mask		= intc_irq_mask,
	.unmask		= intc_irq_unmask,
	.ack		= intc_irq_ack,
	.set_type	= intc_irq_set_type,
};

void __init init_IRQ(void)
{
	int irq;

	init_vectors();

	/* Mask all interrupt sources */
	writel(0x88888888, MCF_MBAR + MCFSIM_ICR1);
	writel(0x88888888, MCF_MBAR + MCFSIM_ICR2);
	writel(0x88888888, MCF_MBAR + MCFSIM_ICR3);
	writel(0x88888888, MCF_MBAR + MCFSIM_ICR4);

	for (irq = 0; (irq < NR_IRQS); irq++) {
		irq_desc[irq].status = IRQ_DISABLED;
		irq_desc[irq].action = NULL;
		irq_desc[irq].depth = 1;
		irq_desc[irq].chip = &intc_irq_chip;
		intc_irq_set_type(irq, 0);
	}
}

