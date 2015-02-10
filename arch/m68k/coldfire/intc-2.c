/*
 * intc-2.c
 *
 * General interrupt controller code for the many ColdFire cores that use
 * interrupt controllers with 63 interrupt sources, organized as 56 fully-
 * programmable + 7 fixed-level interrupt sources. This includes the 523x
 * family, the 5270, 5271, 5274, 5275, and the 528x family which have two such
 * controllers, and the 547x and 548x families which have only one of them.
 *
 * The external 7 fixed interrupts are part the the Edge Port unit of these
 * ColdFire parts. They can be configured as level or edge triggered.
 *
 * (C) Copyright 2009-2011, Greg Ungerer <gerg@snapgear.com>
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
 *	The EDGE Port interrupts are the fixed 7 external interrupts.
 *	They need some special treatment, for example they need to be acked.
 */
#define	EINT0	64	/* Is not actually used, but spot reserved for it */
#define	EINT1	65	/* EDGE Port interrupt 1 */
#define	EINT7	71	/* EDGE Port interrupt 7 */

#ifdef MCFICM_INTC1
#define NR_VECS	128
#else
#define NR_VECS	64
#endif

static void intc_irq_mask(struct irq_data *d)
{
	unsigned int irq = d->irq - MCFINT_VECBASE;
	unsigned long imraddr;
	u32 val, imrbit;

#ifdef MCFICM_INTC1
	imraddr = (irq & 0x40) ? MCFICM_INTC1 : MCFICM_INTC0;
#else
	imraddr = MCFICM_INTC0;
#endif
	imraddr += (irq & 0x20) ? MCFINTC_IMRH : MCFINTC_IMRL;
	imrbit = 0x1 << (irq & 0x1f);

	val = __raw_readl(imraddr);
	__raw_writel(val | imrbit, imraddr);
}

static void intc_irq_unmask(struct irq_data *d)
{
	unsigned int irq = d->irq - MCFINT_VECBASE;
	unsigned long imraddr;
	u32 val, imrbit;

#ifdef MCFICM_INTC1
	imraddr = (irq & 0x40) ? MCFICM_INTC1 : MCFICM_INTC0;
#else
	imraddr = MCFICM_INTC0;
#endif
	imraddr += ((irq & 0x20) ? MCFINTC_IMRH : MCFINTC_IMRL);
	imrbit = 0x1 << (irq & 0x1f);

	/* Don't set the "maskall" bit! */
	if ((irq & 0x20) == 0)
		imrbit |= 0x1;

	val = __raw_readl(imraddr);
	__raw_writel(val & ~imrbit, imraddr);
}

/*
 *	Only the external (or EDGE Port) interrupts need to be acknowledged
 *	here, as part of the IRQ handler. They only really need to be ack'ed
 *	if they are in edge triggered mode, but there is no harm in doing it
 *	for all types.
 */
static void intc_irq_ack(struct irq_data *d)
{
	unsigned int irq = d->irq;

	__raw_writeb(0x1 << (irq - EINT0), MCFEPORT_EPFR);
}

/*
 *	Each vector needs a unique priority and level associated with it.
 *	We don't really care so much what they are, we don't rely on the
 *	traditional priority interrupt scheme of the m68k/ColdFire. This
 *	only needs to be set once for an interrupt, and we will never change
 *	these values once we have set them.
 */
static u8 intc_intpri = MCFSIM_ICR_LEVEL(6) | MCFSIM_ICR_PRI(6);

static unsigned int intc_irq_startup(struct irq_data *d)
{
	unsigned int irq = d->irq - MCFINT_VECBASE;
	unsigned long icraddr;

#ifdef MCFICM_INTC1
	icraddr = (irq & 0x40) ? MCFICM_INTC1 : MCFICM_INTC0;
#else
	icraddr = MCFICM_INTC0;
#endif
	icraddr += MCFINTC_ICR0 + (irq & 0x3f);
	if (__raw_readb(icraddr) == 0)
		__raw_writeb(intc_intpri--, icraddr);

	irq = d->irq;
	if ((irq >= EINT1) && (irq <= EINT7)) {
		u8 v;

		irq -= EINT0;

		/* Set EPORT line as input */
		v = __raw_readb(MCFEPORT_EPDDR);
		__raw_writeb(v & ~(0x1 << irq), MCFEPORT_EPDDR);

		/* Set EPORT line as interrupt source */
		v = __raw_readb(MCFEPORT_EPIER);
		__raw_writeb(v | (0x1 << irq), MCFEPORT_EPIER);
	}

	intc_irq_unmask(d);
	return 0;
}

static int intc_irq_set_type(struct irq_data *d, unsigned int type)
{
	unsigned int irq = d->irq;
	u16 pa, tb;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		tb = 0x1;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		tb = 0x2;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		tb = 0x3;
		break;
	default:
		/* Level triggered */
		tb = 0;
		break;
	}

	if (tb)
		irq_set_handler(irq, handle_edge_irq);

	irq -= EINT0;
	pa = __raw_readw(MCFEPORT_EPPAR);
	pa = (pa & ~(0x3 << (irq * 2))) | (tb << (irq * 2));
	__raw_writew(pa, MCFEPORT_EPPAR);
	
	return 0;
}

static struct irq_chip intc_irq_chip = {
	.name		= "CF-INTC",
	.irq_startup	= intc_irq_startup,
	.irq_mask	= intc_irq_mask,
	.irq_unmask	= intc_irq_unmask,
};

static struct irq_chip intc_irq_chip_edge_port = {
	.name		= "CF-INTC-EP",
	.irq_startup	= intc_irq_startup,
	.irq_mask	= intc_irq_mask,
	.irq_unmask	= intc_irq_unmask,
	.irq_ack	= intc_irq_ack,
	.irq_set_type	= intc_irq_set_type,
};

void __init init_IRQ(void)
{
	int irq;

	/* Mask all interrupt sources */
	__raw_writel(0x1, MCFICM_INTC0 + MCFINTC_IMRL);
#ifdef MCFICM_INTC1
	__raw_writel(0x1, MCFICM_INTC1 + MCFINTC_IMRL);
#endif

	for (irq = MCFINT_VECBASE; (irq < MCFINT_VECBASE + NR_VECS); irq++) {
		if ((irq >= EINT1) && (irq <=EINT7))
			irq_set_chip(irq, &intc_irq_chip_edge_port);
		else
			irq_set_chip(irq, &intc_irq_chip);
		irq_set_irq_type(irq, IRQ_TYPE_LEVEL_HIGH);
		irq_set_handler(irq, handle_level_irq);
	}
}

