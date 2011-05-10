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
#include <linux/sysdev.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/hardware/vic.h>

#include <plat/regs-irqtype.h>
#include <mach/regs-gpio.h>
#include <plat/gpio-cfg.h>

#include <mach/map.h>
#include <plat/cpu.h>
#include <plat/pm.h>

#define eint_offset(irq)	((irq) - IRQ_EINT(0))
#define eint_irq_to_bit(irq)	((u32)(1 << eint_offset(irq)))

static inline void s3c_irq_eint_mask(struct irq_data *data)
{
	u32 mask;

	mask = __raw_readl(S3C64XX_EINT0MASK);
	mask |= (u32)data->chip_data;
	__raw_writel(mask, S3C64XX_EINT0MASK);
}

static void s3c_irq_eint_unmask(struct irq_data *data)
{
	u32 mask;

	mask = __raw_readl(S3C64XX_EINT0MASK);
	mask &= ~((u32)data->chip_data);
	__raw_writel(mask, S3C64XX_EINT0MASK);
}

static inline void s3c_irq_eint_ack(struct irq_data *data)
{
	__raw_writel((u32)data->chip_data, S3C64XX_EINT0PEND);
}

static void s3c_irq_eint_maskack(struct irq_data *data)
{
	/* compiler should in-line these */
	s3c_irq_eint_mask(data);
	s3c_irq_eint_ack(data);
}

static int s3c_irq_eint_set_type(struct irq_data *data, unsigned int type)
{
	int offs = eint_offset(data->irq);
	int pin, pin_val;
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

	if (offs <= 15)
		shift = (offs / 2) * 4;
	else
		shift = ((offs - 16) / 2) * 4;
	mask = 0x7 << shift;

	ctrl = __raw_readl(reg);
	ctrl &= ~mask;
	ctrl |= newvalue << shift;
	__raw_writel(ctrl, reg);

	/* set the GPIO pin appropriately */

	if (offs < 16) {
		pin = S3C64XX_GPN(offs);
		pin_val = S3C_GPIO_SFN(2);
	} else if (offs < 23) {
		pin = S3C64XX_GPL(offs + 8 - 16);
		pin_val = S3C_GPIO_SFN(3);
	} else {
		pin = S3C64XX_GPM(offs - 23);
		pin_val = S3C_GPIO_SFN(3);
	}

	s3c_gpio_cfgpin(pin, pin_val);

	return 0;
}

static struct irq_chip s3c_irq_eint = {
	.name		= "s3c-eint",
	.irq_mask	= s3c_irq_eint_mask,
	.irq_unmask	= s3c_irq_eint_unmask,
	.irq_mask_ack	= s3c_irq_eint_maskack,
	.irq_ack	= s3c_irq_eint_ack,
	.irq_set_type	= s3c_irq_eint_set_type,
	.irq_set_wake	= s3c_irqext_wake,
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

static int __init s3c64xx_init_irq_eint(void)
{
	int irq;

	for (irq = IRQ_EINT(0); irq <= IRQ_EINT(27); irq++) {
		irq_set_chip_and_handler(irq, &s3c_irq_eint, handle_level_irq);
		irq_set_chip_data(irq, (void *)eint_irq_to_bit(irq));
		set_irq_flags(irq, IRQF_VALID);
	}

	irq_set_chained_handler(IRQ_EINT0_3, s3c_irq_demux_eint0_3);
	irq_set_chained_handler(IRQ_EINT4_11, s3c_irq_demux_eint4_11);
	irq_set_chained_handler(IRQ_EINT12_19, s3c_irq_demux_eint12_19);
	irq_set_chained_handler(IRQ_EINT20_27, s3c_irq_demux_eint20_27);

	return 0;
}

arch_initcall(s3c64xx_init_irq_eint);
