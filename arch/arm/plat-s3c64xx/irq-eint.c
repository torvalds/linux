/* arch/arm/plat-s3c64xx/irq-eint.c
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C64XX - Interrupt handling for IRQ_EINT(x)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/hardware/vic.h>

#include <plat/regs-irqtype.h>

#include <mach/map.h>
#include <plat/cpu.h>

/* GPIO is 0x7F008xxx, */
#define S3C64XX_GPIOREG(x)	(S3C64XX_VA_GPIO + (x))

#define S3C64XX_EINT0CON0	S3C64XX_GPIOREG(0x900)
#define S3C64XX_EINT0CON1	S3C64XX_GPIOREG(0x904)
#define S3C64XX_EINT0FLTCON0	S3C64XX_GPIOREG(0x910)
#define S3C64XX_EINT0FLTCON1	S3C64XX_GPIOREG(0x914)
#define S3C64XX_EINT0FLTCON2	S3C64XX_GPIOREG(0x918)
#define S3C64XX_EINT0FLTCON3	S3C64XX_GPIOREG(0x91C)

#define S3C64XX_EINT0MASK	S3C64XX_GPIOREG(0x920)
#define S3C64XX_EINT0PEND	S3C64XX_GPIOREG(0x924)


#define eint_offset(irq)	((irq) - IRQ_EINT(0))
#define eint_irq_to_bit(irq)	(1 << eint_offset(irq))

static inline void s3c_irq_eint_mask(unsigned int irq)
{
	u32 mask;

	mask = __raw_readl(S3C64XX_EINT0MASK);
	mask |= eint_irq_to_bit(irq);
	__raw_writel(mask, S3C64XX_EINT0MASK);
}

static void s3c_irq_eint_unmask(unsigned int irq)
{
	u32 mask;

	mask = __raw_readl(S3C64XX_EINT0MASK);
	mask &= ~eint_irq_to_bit(irq);
	__raw_writel(mask, S3C64XX_EINT0MASK);
}

static inline void s3c_irq_eint_ack(unsigned int irq)
{
	__raw_writel(eint_irq_to_bit(irq), S3C64XX_EINT0PEND);
}

static void s3c_irq_eint_maskack(unsigned int irq)
{
	/* compiler should in-line these */
	s3c_irq_eint_mask(irq);
	s3c_irq_eint_ack(irq);
}

static int s3c_irq_eint_set_type(unsigned int irq, unsigned int type)
{
	int offs = eint_offset(irq);
	int shift;
	u32 ctrl, mask;
	u32 newvalue = 0;
	void __iomem *reg;

	if (offs > 27)
		return -EINVAL;

	if (offs <= 15)
		reg = S3C64XX_EINT0CON0;
	else
		reg = S3C64XX_EINT0CON1;

	switch (type) {
	case IRQ_TYPE_NONE:
		printk(KERN_WARNING "No edge setting!\n");
		break;

	case IRQ_TYPE_EDGE_RISING:
		newvalue = S3C2410_EXTINT_RISEEDGE;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		newvalue = S3C2410_EXTINT_FALLEDGE;
		break;

	case IRQ_TYPE_EDGE_BOTH:
		newvalue = S3C2410_EXTINT_BOTHEDGE;
		break;

	case IRQ_TYPE_LEVEL_LOW:
		newvalue = S3C2410_EXTINT_LOWLEV;
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		newvalue = S3C2410_EXTINT_HILEV;
		break;

	default:
		printk(KERN_ERR "No such irq type %d", type);
		return -1;
	}

	shift = (offs / 2) * 4;
	mask = 0x7 << shift;

	ctrl = __raw_readl(reg);
	ctrl &= ~mask;
	ctrl |= newvalue << shift;
	__raw_writel(ctrl, reg);

	return 0;
}

static struct irq_chip s3c_irq_eint = {
	.name		= "s3c-eint",
	.mask		= s3c_irq_eint_mask,
	.unmask		= s3c_irq_eint_unmask,
	.mask_ack	= s3c_irq_eint_maskack,
	.ack		= s3c_irq_eint_ack,
	.set_type	= s3c_irq_eint_set_type,
};

/* s3c_irq_demux_eint
 *
 * This function demuxes the IRQ from the group0 external interrupts,
 * from IRQ_EINT(0) to IRQ_EINT(27). It is designed to be inlined into
 * the specific handlers s3c_irq_demux_eintX_Y.
 */
static inline void s3c_irq_demux_eint(unsigned int start, unsigned int end)
{
	u32 status = __raw_readl(S3C64XX_EINT0PEND);
	u32 mask = __raw_readl(S3C64XX_EINT0MASK);
	unsigned int irq;

	status &= ~mask;
	status >>= start;
	status &= (1 << (end - start + 1)) - 1;

	for (irq = IRQ_EINT(start); irq <= IRQ_EINT(end); irq++) {
		if (status & 1)
			generic_handle_irq(irq);

		status >>= 1;
	}
}

static void s3c_irq_demux_eint0_3(unsigned int irq, struct irq_desc *desc)
{
	s3c_irq_demux_eint(0, 3);
}

static void s3c_irq_demux_eint4_11(unsigned int irq, struct irq_desc *desc)
{
	s3c_irq_demux_eint(4, 11);
}

static void s3c_irq_demux_eint12_19(unsigned int irq, struct irq_desc *desc)
{
	s3c_irq_demux_eint(12, 19);
}

static void s3c_irq_demux_eint20_27(unsigned int irq, struct irq_desc *desc)
{
	s3c_irq_demux_eint(20, 27);
}

int __init s3c64xx_init_irq_eint(void)
{
	int irq;

	for (irq = IRQ_EINT(0); irq <= IRQ_EINT(27); irq++) {
		set_irq_chip(irq, &s3c_irq_eint);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}

	set_irq_chained_handler(IRQ_EINT0_3, s3c_irq_demux_eint0_3);
	set_irq_chained_handler(IRQ_EINT4_11, s3c_irq_demux_eint4_11);
	set_irq_chained_handler(IRQ_EINT12_19, s3c_irq_demux_eint12_19);
	set_irq_chained_handler(IRQ_EINT20_27, s3c_irq_demux_eint20_27);

	return 0;
}

arch_initcall(s3c64xx_init_irq_eint);
