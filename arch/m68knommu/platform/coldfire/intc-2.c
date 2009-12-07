/*
 * intc-1.c
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
 *	Each vector needs a unique priority and level asscoiated with it.
 *	We don't really care so much what they are, we don't rely on the
 *	tranditional priority interrupt scheme of the m68k/ColdFire.
 */
static u8 intc_intpri = 0x36;

static void intc_irq_mask(unsigned int irq)
{
	if ((irq >= MCFINT_VECBASE) && (irq <= MCFINT_VECBASE + 128)) {
		unsigned long imraddr;
		u32 val, imrbit;

		irq -= MCFINT_VECBASE;
		imraddr = MCF_IPSBAR;
		imraddr += (irq & 0x40) ? MCFICM_INTC1 : MCFICM_INTC0;
		imraddr += (irq & 0x20) ? MCFINTC_IMRH : MCFINTC_IMRL;
		imrbit = 0x1 << (irq & 0x1f);

		val = __raw_readl(imraddr);
		__raw_writel(val | imrbit, imraddr);
	}
}

static void intc_irq_unmask(unsigned int irq)
{
	if ((irq >= MCFINT_VECBASE) && (irq <= MCFINT_VECBASE + 128)) {
		unsigned long intaddr, imraddr, icraddr;
		u32 val, imrbit;

		irq -= MCFINT_VECBASE;
		intaddr = MCF_IPSBAR;
		intaddr += (irq & 0x40) ? MCFICM_INTC1 : MCFICM_INTC0;
		imraddr = intaddr + ((irq & 0x20) ? MCFINTC_IMRH : MCFINTC_IMRL);
		icraddr = intaddr + MCFINTC_ICR0 + (irq & 0x3f);
		imrbit = 0x1 << (irq & 0x1f);

		/* Don't set the "maskall" bit! */
		if ((irq & 0x20) == 0)
			imrbit |= 0x1;

		if (__raw_readb(icraddr) == 0)
			__raw_writeb(intc_intpri--, icraddr);

		val = __raw_readl(imraddr);
		__raw_writel(val & ~imrbit, imraddr);
	}
}

static struct irq_chip intc_irq_chip = {
	.name		= "CF-INTC",
	.mask		= intc_irq_mask,
	.unmask		= intc_irq_unmask,
};

void __init init_IRQ(void)
{
	int irq;

	init_vectors();

	/* Mask all interrupt sources */
	__raw_writel(0x1, MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_IMRL);
	__raw_writel(0x1, MCF_IPSBAR + MCFICM_INTC1 + MCFINTC_IMRL);

	for (irq = 0; (irq < NR_IRQS); irq++) {
		irq_desc[irq].status = IRQ_DISABLED;
		irq_desc[irq].action = NULL;
		irq_desc[irq].depth = 1;
		irq_desc[irq].chip = &intc_irq_chip;
	}
}

