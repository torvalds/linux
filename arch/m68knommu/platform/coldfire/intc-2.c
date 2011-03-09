/*
 * intc-2.c
 *
 * General interrupt controller code for the many ColdFire cores that use
 * interrupt controllers with 63 interrupt sources, organized as 56 fully-
 * programmable + 7 fixed-level interrupt sources. This includes the 523x
 * family, the 5270, 5271, 5274, 5275, and the 528x family which have two such
 * controllers, and the 547x and 548x families which have only one of them.
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
 * Bit definitions for the ICR family of registers.
 */
#define MCFSIM_ICR_LEVEL(l)	((l)<<3)	/* Level l intr */
#define MCFSIM_ICR_PRI(p)	(p)		/* Priority p intr */

/*
 *	Each vector needs a unique priority and level associated with it.
 *	We don't really care so much what they are, we don't rely on the
 *	traditional priority interrupt scheme of the m68k/ColdFire.
 */
static u8 intc_intpri = MCFSIM_ICR_LEVEL(6) | MCFSIM_ICR_PRI(6);

#ifdef MCFICM_INTC1
#define NR_VECS	128
#else
#define NR_VECS	64
#endif

static void intc_irq_mask(unsigned int irq)
{
	if ((irq >= MCFINT_VECBASE) && (irq <= MCFINT_VECBASE + NR_VECS)) {
		unsigned long imraddr;
		u32 val, imrbit;

		irq -= MCFINT_VECBASE;
		imraddr = MCF_IPSBAR;
#ifdef MCFICM_INTC1
		imraddr += (irq & 0x40) ? MCFICM_INTC1 : MCFICM_INTC0;
#else
		imraddr += MCFICM_INTC0;
#endif
		imraddr += (irq & 0x20) ? MCFINTC_IMRH : MCFINTC_IMRL;
		imrbit = 0x1 << (irq & 0x1f);

		val = __raw_readl(imraddr);
		__raw_writel(val | imrbit, imraddr);
	}
}

static void intc_irq_unmask(unsigned int irq)
{
	if ((irq >= MCFINT_VECBASE) && (irq <= MCFINT_VECBASE + NR_VECS)) {
		unsigned long intaddr, imraddr, icraddr;
		u32 val, imrbit;

		irq -= MCFINT_VECBASE;
		intaddr = MCF_IPSBAR;
#ifdef MCFICM_INTC1
		intaddr += (irq & 0x40) ? MCFICM_INTC1 : MCFICM_INTC0;
#else
		intaddr += MCFICM_INTC0;
#endif
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

	/* Mask all interrupt sources */
	__raw_writel(0x1, MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_IMRL);
#ifdef MCFICM_INTC1
	__raw_writel(0x1, MCF_IPSBAR + MCFICM_INTC1 + MCFINTC_IMRL);
#endif

	for (irq = 0; (irq < NR_IRQS); irq++) {
		set_irq_chip(irq, &intc_irq_chip);
		set_irq_type(irq, IRQ_TYPE_LEVEL_HIGH);
		set_irq_handler(irq, handle_level_irq);
	}
}

