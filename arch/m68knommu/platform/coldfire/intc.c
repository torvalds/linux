/*
 * intc.c  -- support for the old ColdFire interrupt controller
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
#include <asm/traps.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>

/*
 * Define the vector numbers for the basic 7 interrupt sources.
 * These are often referred to as the "external" interrupts in
 * the ColdFire documentation (for the early ColdFire cores at least).
 */
#define	EIRQ1	25
#define	EIRQ7	31

/*
 * In the early version 2 core ColdFire parts the IMR register was 16 bits
 * in size. Version 3 (and later version 2) core parts have a 32 bit
 * sized IMR register. Provide some size independant methods to access the
 * IMR register.
 */
#ifdef MCFSIM_IMR_IS_16BITS

void mcf_setimr(int index)
{
        u16 imr;
        imr = __raw_readw(MCF_MBAR + MCFSIM_IMR);
	__raw_writew(imr | (0x1 << index), MCF_MBAR + MCFSIM_IMR);
}

void mcf_clrimr(int index)
{
        u16 imr;
        imr = __raw_readw(MCF_MBAR + MCFSIM_IMR);
	__raw_writew(imr & ~(0x1 << index), MCF_MBAR + MCFSIM_IMR);
}

void mcf_maskimr(unsigned int mask)
{
	u16 imr;
        imr = __raw_readw(MCF_MBAR + MCFSIM_IMR);
	imr |= mask;
	__raw_writew(imr, MCF_MBAR + MCFSIM_IMR);
}

#else

void mcf_setimr(int index)
{
        u32 imr;
        imr = __raw_readl(MCF_MBAR + MCFSIM_IMR);
	__raw_writel(imr | (0x1 << index), MCF_MBAR + MCFSIM_IMR);
}

void mcf_clrimr(int index)
{
        u32 imr;
        imr = __raw_readl(MCF_MBAR + MCFSIM_IMR);
	__raw_writel(imr & ~(0x1 << index), MCF_MBAR + MCFSIM_IMR);
}

void mcf_maskimr(unsigned int mask)
{
	u32 imr;
        imr = __raw_readl(MCF_MBAR + MCFSIM_IMR);
	imr |= mask;
	__raw_writel(imr, MCF_MBAR + MCFSIM_IMR);
}

#endif

/*
 * Interrupts can be "vectored" on the ColdFire cores that support this old
 * interrupt controller. That is, the device raising the interrupt can also
 * supply the vector number to interrupt through. The AVR register of the
 * interrupt controller enables or disables this for each external interrupt,
 * so provide generic support for this. Setting this up is out-of-band for
 * the interrupt system API's, and needs to be done by the driver that
 * supports this device. Very few devices actually use this.
 */
void mcf_autovector(int irq)
{
	if ((irq >= EIRQ1) && (irq <= EIRQ7)) {
		u8 avec;
		avec = __raw_readb(MCF_MBAR + MCFSIM_AVR);
		avec |= (0x1 << (irq - EIRQ1 + 1));
		__raw_writeb(avec, MCF_MBAR + MCFSIM_AVR);
	}
}

static void intc_irq_mask(unsigned int irq)
{
}

static void intc_irq_unmask(unsigned int irq)
{
}

static int intc_irq_set_type(unsigned int irq, unsigned int type)
{
	return 0;
}

static struct irq_chip intc_irq_chip = {
	.name		= "CF-INTC",
	.mask		= intc_irq_mask,
	.unmask		= intc_irq_unmask,
	.set_type	= intc_irq_set_type,
};

void __init init_IRQ(void)
{
	int irq;

	init_vectors();
	mcf_maskimr(0xffffffff);

	for (irq = 0; (irq < NR_IRQS); irq++) {
		irq_desc[irq].status = IRQ_DISABLED;
		irq_desc[irq].action = NULL;
		irq_desc[irq].depth = 1;
		irq_desc[irq].chip = &intc_irq_chip;
		intc_irq_set_type(irq, 0);
	}
}

