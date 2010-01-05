/*
 * linux/arch/arm/plat-s5pc1xx/irq-eint.c
 *
 *  Copyright 2009 Samsung Electronics Co.
 *  Byungho Min <bhmin@samsung.com>
 *  Kyungin Park <kyungmin.park@samsung.com>
 *
 * Based on plat-s3c64xx/irq-eint.c
 *
 * S5PC1XX - Interrupt handling for IRQ_EINT(x)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/sysdev.h>
#include <linux/pm.h>
#include <linux/gpio.h>

#include <asm/hardware/vic.h>

#include <mach/map.h>

#include <plat/gpio-cfg.h>
#include <plat/gpio-ext.h>
#include <plat/pm.h>
#include <plat/regs-gpio.h>
#include <plat/regs-irqtype.h>

/*
 * bank is a group of external interrupt
 * bank0 means EINT0 ... EINT7
 * bank1 means EINT8 ... EINT15
 * bank2 means EINT16 ... EINT23
 * bank3 means EINT24 ... EINT31
 */

static inline int s3c_get_eint(unsigned int irq)
{
	int real;

	if (irq < IRQ_EINT16_31)
		real = (irq - IRQ_EINT0);
	else
		real = (irq - S3C_IRQ_EINT_BASE) + IRQ_EINT16_31 - IRQ_EINT0;

	return real;
}

static inline int s3c_get_bank(unsigned int irq)
{
	return s3c_get_eint(irq) >> 3;
}

static inline int s3c_eint_to_bit(unsigned int irq)
{
	int real, bit;

	real = s3c_get_eint(irq);
	bit = 1 << (real & (8 - 1));

	return bit;
}

static inline void s3c_irq_eint_mask(unsigned int irq)
{
	u32 mask;
	u32 bank = s3c_get_bank(irq);

	mask = __raw_readl(S5PC1XX_WKUP_INT_MASK(bank));
	mask |= s3c_eint_to_bit(irq);
	__raw_writel(mask, S5PC1XX_WKUP_INT_MASK(bank));
}

static void s3c_irq_eint_unmask(unsigned int irq)
{
	u32 mask;
	u32 bank = s3c_get_bank(irq);

	mask = __raw_readl(S5PC1XX_WKUP_INT_MASK(bank));
	mask &= ~(s3c_eint_to_bit(irq));
	__raw_writel(mask, S5PC1XX_WKUP_INT_MASK(bank));
}

static inline void s3c_irq_eint_ack(unsigned int irq)
{
	u32 bank = s3c_get_bank(irq);

	__raw_writel(s3c_eint_to_bit(irq), S5PC1XX_WKUP_INT_PEND(bank));
}

static void s3c_irq_eint_maskack(unsigned int irq)
{
	/* compiler should in-line these */
	s3c_irq_eint_mask(irq);
	s3c_irq_eint_ack(irq);
}

static int s3c_irq_eint_set_type(unsigned int irq, unsigned int type)
{
	u32 bank = s3c_get_bank(irq);
	int real = s3c_get_eint(irq);
	int gpio, shift, sfn;
	u32 ctrl, con = 0;

	switch (type) {
	case IRQ_TYPE_NONE:
		printk(KERN_WARNING "No edge setting!\n");
		break;

	case IRQ_TYPE_EDGE_RISING:
		con = S5PC1XX_WKUP_INT_RISEEDGE;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		con = S5PC1XX_WKUP_INT_FALLEDGE;
		break;

	case IRQ_TYPE_EDGE_BOTH:
		con = S5PC1XX_WKUP_INT_BOTHEDGE;
		break;

	case IRQ_TYPE_LEVEL_LOW:
		con = S5PC1XX_WKUP_INT_LOWLEV;
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		con = S5PC1XX_WKUP_INT_HILEV;
		break;

	default:
		printk(KERN_ERR "No such irq type %d", type);
		return -EINVAL;
	}

	gpio = real & (8 - 1);
	shift = gpio << 2;

	ctrl = __raw_readl(S5PC1XX_WKUP_INT_CON(bank));
	ctrl &= ~(0x7 << shift);
	ctrl |= con << shift;
	__raw_writel(ctrl, S5PC1XX_WKUP_INT_CON(bank));

	switch (real) {
	case 0 ... 7:
			gpio = S5PC100_GPH0(gpio);
		break;
	case 8 ... 15:
			gpio = S5PC100_GPH1(gpio);
		break;
	case 16 ... 23:
			gpio = S5PC100_GPH2(gpio);
		break;
	case 24 ... 31:
			gpio = S5PC100_GPH3(gpio);
		break;
	default:
		return -EINVAL;
	}

	sfn = S3C_GPIO_SFN(0x2);
	s3c_gpio_cfgpin(gpio, sfn);

	return 0;
}

static struct irq_chip s3c_irq_eint = {
	.name		= "EINT",
	.mask		= s3c_irq_eint_mask,
	.unmask		= s3c_irq_eint_unmask,
	.mask_ack	= s3c_irq_eint_maskack,
	.ack		= s3c_irq_eint_ack,
	.set_type	= s3c_irq_eint_set_type,
	.set_wake	= s3c_irqext_wake,
};

/* s3c_irq_demux_eint
 *
 * This function demuxes the IRQ from external interrupts,
 * from IRQ_EINT(16) to IRQ_EINT(31). It is designed to be inlined into
 * the specific handlers s3c_irq_demux_eintX_Y.
 */
static inline void s3c_irq_demux_eint(unsigned int start, unsigned int end)
{
	u32 status = __raw_readl(S5PC1XX_WKUP_INT_PEND((start >> 3)));
	u32 mask = __raw_readl(S5PC1XX_WKUP_INT_MASK((start >> 3)));
	unsigned int irq;

	status &= ~mask;
	status &= (1 << (end - start + 1)) - 1;

	for (irq = IRQ_EINT(start); irq <= IRQ_EINT(end); irq++) {
		if (status & 1)
			generic_handle_irq(irq);

		status >>= 1;
	}
}

static void s3c_irq_demux_eint16_31(unsigned int irq, struct irq_desc *desc)
{
	s3c_irq_demux_eint(16, 23);
	s3c_irq_demux_eint(24, 31);
}

/*
 * Handle EINT0 ... EINT15 at VIC directly
 */
static void s3c_irq_vic_eint_mask(unsigned int irq)
{
	void __iomem *base = get_irq_chip_data(irq);
	unsigned int real;

	s3c_irq_eint_mask(irq);
	real = s3c_get_eint(irq);
	writel(1 << real, base + VIC_INT_ENABLE_CLEAR);
}

static void s3c_irq_vic_eint_unmask(unsigned int irq)
{
	void __iomem *base = get_irq_chip_data(irq);
	unsigned int real;

	s3c_irq_eint_unmask(irq);
	real = s3c_get_eint(irq);
	writel(1 << real, base + VIC_INT_ENABLE);
}

static inline void s3c_irq_vic_eint_ack(unsigned int irq)
{
	u32 bit;
	u32 bank = s3c_get_bank(irq);

	bit = s3c_eint_to_bit(irq);
	__raw_writel(bit, S5PC1XX_WKUP_INT_PEND(bank));
}

static void s3c_irq_vic_eint_maskack(unsigned int irq)
{
	/* compiler should in-line these */
	s3c_irq_vic_eint_mask(irq);
	s3c_irq_vic_eint_ack(irq);
}

static struct irq_chip s3c_irq_vic_eint = {
	.name		= "EINT",
	.mask		= s3c_irq_vic_eint_mask,
	.unmask		= s3c_irq_vic_eint_unmask,
	.mask_ack	= s3c_irq_vic_eint_maskack,
	.ack		= s3c_irq_vic_eint_ack,
	.set_type	= s3c_irq_eint_set_type,
	.set_wake	= s3c_irqext_wake,
};

static int __init s5pc1xx_init_irq_eint(void)
{
	int irq;

	for (irq = IRQ_EINT0; irq <= IRQ_EINT15; irq++) {
		set_irq_chip(irq, &s3c_irq_vic_eint);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}

	for (irq = IRQ_EINT(16); irq <= IRQ_EINT(31); irq++) {
		set_irq_chip(irq, &s3c_irq_eint);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}

	set_irq_chained_handler(IRQ_EINT16_31, s3c_irq_demux_eint16_31);

	return 0;
}

arch_initcall(s5pc1xx_init_irq_eint);
