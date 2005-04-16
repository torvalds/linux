/*
 * arch/sh/boards/saturn/irq.c
 *
 * Copyright (C) 2002 Paul Mundt
 *
 * Released under the terms of the GNU GPL v2.0.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <asm/io.h>

/*
 * Interrupts map out as follows:
 *
 *  Vector	Name		Mask
 *
 * 	64	VBLANKIN	0x0001
 * 	65	VBLANKOUT	0x0002
 *	66	HBLANKIN	0x0004
 *	67	TIMER0		0x0008
 *	68	TIMER1		0x0010
 *	69	DSPEND		0x0020
 *	70	SOUNDREQUEST	0x0040
 *	71	SYSTEMMANAGER	0x0080
 *	72	PAD		0x0100
 *	73	LEVEL2DMAEND	0x0200
 *	74	LEVEL1DMAEND	0x0400
 *	75	LEVEL0DMAEND	0x0800
 *	76	DMAILLEGAL	0x1000
 *	77	SRITEDRAWEND	0x2000
 *	78	ABUS		0x8000
 *
 */
#define SATURN_IRQ_MIN		64	/* VBLANKIN */
#define SATURN_IRQ_MAX		78	/* ABUS */

#define SATURN_IRQ_MASK		0xbfff

static inline u32 saturn_irq_mask(unsigned int irq_nr)
{
	u32 mask;

	mask = (1 << (irq_nr - SATURN_IRQ_MIN));
	mask <<= (irq_nr == SATURN_IRQ_MAX);
	mask &= SATURN_IRQ_MASK;

	return mask;
}

static inline void mask_saturn_irq(unsigned int irq_nr)
{
	u32 mask;

	mask = ctrl_inl(SATURN_IMR);
	mask |= saturn_irq_mask(irq_nr);
	ctrl_outl(mask, SATURN_IMR);
}

static inline void unmask_saturn_irq(unsigned int irq_nr)
{
	u32 mask;

	mask = ctrl_inl(SATURN_IMR);
	mask &= ~saturn_irq_mask(irq_nr);
	ctrl_outl(mask, SATURN_IMR);
}

static void disable_saturn_irq(unsigned int irq_nr)
{
	mask_saturn_irq(irq_nr);
}

static void enable_saturn_irq(unsigned int irq_nr)
{
	unmask_saturn_irq(irq_nr);
}

static void mask_and_ack_saturn_irq(unsigned int irq_nr)
{
	mask_saturn_irq(irq_nr);
}

static void end_saturn_irq(unsigned int irq_nr)
{
	if (!(irq_desc[irq_nr].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		unmask_saturn_irq(irq_nr);
}

static unsigned int startup_saturn_irq(unsigned int irq_nr)
{
	unmask_saturn_irq(irq_nr);

	return 0;
}

static void shutdown_saturn_irq(unsigned int irq_nr)
{
	mask_saturn_irq(irq_nr);
}

static struct hw_interrupt_type saturn_int = {
	.typename	= "Saturn",
	.enable		= enable_saturn_irq,
	.disable	= disable_saturn_irq,
	.ack		= mask_and_ack_saturn_irq,
	.end		= end_saturn_irq,
	.startup	= startup_saturn_irq,
	.shutdown	= shutdown_saturn_irq,
};

int saturn_irq_demux(int irq_nr)
{
	/* FIXME */
	return irq_nr;
}

