/*
 * S3C24XX IRQ handling
 *
 * Copyright (c) 2003-2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 * Copyright (c) 2012 Heiko Stuebner <heiko@sntech.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
*/

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/irqdomain.h>

#include <asm/mach/irq.h>

#include <mach/regs-irq.h>
#include <mach/regs-gpio.h>

#include <plat/cpu.h>
#include <plat/regs-irqtype.h>
#include <plat/pm.h>
#include <plat/irq.h>

#define S3C_IRQTYPE_NONE	0
#define S3C_IRQTYPE_EINT	1
#define S3C_IRQTYPE_EDGE	2
#define S3C_IRQTYPE_LEVEL	3

struct s3c_irq_data {
	unsigned int type;
	unsigned long parent_irq;

	/* data gets filled during init */
	struct s3c_irq_intc *intc;
	unsigned long sub_bits;
	struct s3c_irq_intc *sub_intc;
};

/*
 * Sructure holding the controller data
 * @reg_pending		register holding pending irqs
 * @reg_intpnd		special register intpnd in main intc
 * @reg_mask		mask register
 * @domain		irq_domain of the controller
 * @parent		parent controller for ext and sub irqs
 * @irqs		irq-data, always s3c_irq_data[32]
 */
struct s3c_irq_intc {
	void __iomem		*reg_pending;
	void __iomem		*reg_intpnd;
	void __iomem		*reg_mask;
	struct irq_domain	*domain;
	struct s3c_irq_intc	*parent;
	struct s3c_irq_data	*irqs;
};

static void s3c_irq_mask(struct irq_data *data)
{
	struct s3c_irq_intc *intc = data->domain->host_data;
	struct s3c_irq_intc *parent_intc = intc->parent;
	struct s3c_irq_data *irq_data = &intc->irqs[data->hwirq];
	struct s3c_irq_data *parent_data;
	unsigned long mask;
	unsigned int irqno;

	mask = __raw_readl(intc->reg_mask);
	mask |= (1UL << data->hwirq);
	__raw_writel(mask, intc->reg_mask);

	if (parent_intc && irq_data->parent_irq) {
		parent_data = &parent_intc->irqs[irq_data->parent_irq];

		/* check to see if we need to mask the parent IRQ */
		if ((mask & parent_data->sub_bits) == parent_data->sub_bits) {
			irqno = irq_find_mapping(parent_intc->domain,
					 irq_data->parent_irq);
			s3c_irq_mask(irq_get_irq_data(irqno));
		}
	}
}

static void s3c_irq_unmask(struct irq_data *data)
{
	struct s3c_irq_intc *intc = data->domain->host_data;
	struct s3c_irq_intc *parent_intc = intc->parent;
	struct s3c_irq_data *irq_data = &intc->irqs[data->hwirq];
	unsigned long mask;
	unsigned int irqno;

	mask = __raw_readl(intc->reg_mask);
	mask &= ~(1UL << data->hwirq);
	__raw_writel(mask, intc->reg_mask);

	if (parent_intc && irq_data->parent_irq) {
		irqno = irq_find_mapping(parent_intc->domain,
					 irq_data->parent_irq);
		s3c_irq_unmask(irq_get_irq_data(irqno));
	}
}

static inline void s3c_irq_ack(struct irq_data *data)
{
	struct s3c_irq_intc *intc = data->domain->host_data;
	unsigned long bitval = 1UL << data->hwirq;

	__raw_writel(bitval, intc->reg_pending);
	if (intc->reg_intpnd)
		__raw_writel(bitval, intc->reg_intpnd);
}

static int s3c_irqext_type_set(void __iomem *gpcon_reg,
			       void __iomem *extint_reg,
			       unsigned long gpcon_offset,
			       unsigned long extint_offset,
			       unsigned int type)
{
	unsigned long newvalue = 0, value;

	/* Set the GPIO to external interrupt mode */
	value = __raw_readl(gpcon_reg);
	value = (value & ~(3 << gpcon_offset)) | (0x02 << gpcon_offset);
	__raw_writel(value, gpcon_reg);

	/* Set the external interrupt to pointed trigger type */
	switch (type)
	{
		case IRQ_TYPE_NONE:
			pr_warn("No edge setting!\n");
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
			pr_err("No such irq type %d", type);
			return -EINVAL;
	}

	value = __raw_readl(extint_reg);
	value = (value & ~(7 << extint_offset)) | (newvalue << extint_offset);
	__raw_writel(value, extint_reg);

	return 0;
}

/* FIXME: make static when it's out of plat-samsung/irq.h */
int s3c_irqext_type(struct irq_data *data, unsigned int type)
{
	void __iomem *extint_reg;
	void __iomem *gpcon_reg;
	unsigned long gpcon_offset, extint_offset;

	if ((data->hwirq >= 4) && (data->hwirq <= 7)) {
		gpcon_reg = S3C2410_GPFCON;
		extint_reg = S3C24XX_EXTINT0;
		gpcon_offset = (data->hwirq) * 2;
		extint_offset = (data->hwirq) * 4;
	} else if ((data->hwirq >= 8) && (data->hwirq <= 15)) {
		gpcon_reg = S3C2410_GPGCON;
		extint_reg = S3C24XX_EXTINT1;
		gpcon_offset = (data->hwirq - 8) * 2;
		extint_offset = (data->hwirq - 8) * 4;
	} else if ((data->hwirq >= 16) && (data->hwirq <= 23)) {
		gpcon_reg = S3C2410_GPGCON;
		extint_reg = S3C24XX_EXTINT2;
		gpcon_offset = (data->hwirq - 8) * 2;
		extint_offset = (data->hwirq - 16) * 4;
	} else {
		return -EINVAL;
	}

	return s3c_irqext_type_set(gpcon_reg, extint_reg, gpcon_offset,
				   extint_offset, type);
}

static int s3c_irqext0_type(struct irq_data *data, unsigned int type)
{
	void __iomem *extint_reg;
	void __iomem *gpcon_reg;
	unsigned long gpcon_offset, extint_offset;

	if ((data->hwirq >= 0) && (data->hwirq <= 3)) {
		gpcon_reg = S3C2410_GPFCON;
		extint_reg = S3C24XX_EXTINT0;
		gpcon_offset = (data->hwirq) * 2;
		extint_offset = (data->hwirq) * 4;
	} else {
		return -EINVAL;
	}

	return s3c_irqext_type_set(gpcon_reg, extint_reg, gpcon_offset,
				   extint_offset, type);
}

struct irq_chip s3c_irq_chip = {
	.name		= "s3c",
	.irq_ack	= s3c_irq_ack,
	.irq_mask	= s3c_irq_mask,
	.irq_unmask	= s3c_irq_unmask,
	.irq_set_wake	= s3c_irq_wake
};

struct irq_chip s3c_irq_level_chip = {
	.name		= "s3c-level",
	.irq_mask	= s3c_irq_mask,
	.irq_unmask	= s3c_irq_unmask,
	.irq_ack	= s3c_irq_ack,
};

static struct irq_chip s3c_irqext_chip = {
	.name		= "s3c-ext",
	.irq_mask	= s3c_irq_mask,
	.irq_unmask	= s3c_irq_unmask,
	.irq_ack	= s3c_irq_ack,
	.irq_set_type	= s3c_irqext_type,
	.irq_set_wake	= s3c_irqext_wake
};

static struct irq_chip s3c_irq_eint0t4 = {
	.name		= "s3c-ext0",
	.irq_ack	= s3c_irq_ack,
	.irq_mask	= s3c_irq_mask,
	.irq_unmask	= s3c_irq_unmask,
	.irq_set_wake	= s3c_irq_wake,
	.irq_set_type	= s3c_irqext0_type,
};

static void s3c_irq_demux(unsigned int irq, struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct s3c_irq_intc *intc = desc->irq_data.domain->host_data;
	struct s3c_irq_data *irq_data = &intc->irqs[desc->irq_data.hwirq];
	struct s3c_irq_intc *sub_intc = irq_data->sub_intc;
	unsigned long src;
	unsigned long msk;
	unsigned int n;

	chained_irq_enter(chip, desc);

	src = __raw_readl(sub_intc->reg_pending);
	msk = __raw_readl(sub_intc->reg_mask);

	src &= ~msk;
	src &= irq_data->sub_bits;

	while (src) {
		n = __ffs(src);
		src &= ~(1 << n);
		generic_handle_irq(irq_find_mapping(sub_intc->domain, n));
	}

	chained_irq_exit(chip, desc);
}

#ifdef CONFIG_FIQ
/**
 * s3c24xx_set_fiq - set the FIQ routing
 * @irq: IRQ number to route to FIQ on processor.
 * @on: Whether to route @irq to the FIQ, or to remove the FIQ routing.
 *
 * Change the state of the IRQ to FIQ routing depending on @irq and @on. If
 * @on is true, the @irq is checked to see if it can be routed and the
 * interrupt controller updated to route the IRQ. If @on is false, the FIQ
 * routing is cleared, regardless of which @irq is specified.
 */
int s3c24xx_set_fiq(unsigned int irq, bool on)
{
	u32 intmod;
	unsigned offs;

	if (on) {
		offs = irq - FIQ_START;
		if (offs > 31)
			return -EINVAL;

		intmod = 1 << offs;
	} else {
		intmod = 0;
	}

	__raw_writel(intmod, S3C2410_INTMOD);
	return 0;
}

EXPORT_SYMBOL_GPL(s3c24xx_set_fiq);
#endif

static int s3c24xx_irq_map(struct irq_domain *h, unsigned int virq,
							irq_hw_number_t hw)
{
	struct s3c_irq_intc *intc = h->host_data;
	struct s3c_irq_data *irq_data = &intc->irqs[hw];
	struct s3c_irq_intc *parent_intc;
	struct s3c_irq_data *parent_irq_data;
	unsigned int irqno;

	if (!intc) {
		pr_err("irq-s3c24xx: no controller found for hwirq %lu\n", hw);
		return -EINVAL;
	}

	if (!irq_data) {
		pr_err("irq-s3c24xx: no irq data found for hwirq %lu\n", hw);
		return -EINVAL;
	}

	/* attach controller pointer to irq_data */
	irq_data->intc = intc;

	/* set handler and flags */
	switch (irq_data->type) {
	case S3C_IRQTYPE_NONE:
		return 0;
	case S3C_IRQTYPE_EINT:
		if (irq_data->parent_irq)
			irq_set_chip_and_handler(virq, &s3c_irqext_chip,
						 handle_edge_irq);
		else
			irq_set_chip_and_handler(virq, &s3c_irq_eint0t4,
						 handle_edge_irq);
		break;
	case S3C_IRQTYPE_EDGE:
		if (irq_data->parent_irq)
			irq_set_chip_and_handler(virq, &s3c_irq_level_chip,
						 handle_edge_irq);
		else
			irq_set_chip_and_handler(virq, &s3c_irq_chip,
						 handle_edge_irq);
		break;
	case S3C_IRQTYPE_LEVEL:
		if (irq_data->parent_irq)
			irq_set_chip_and_handler(virq, &s3c_irq_level_chip,
						 handle_level_irq);
		else
			irq_set_chip_and_handler(virq, &s3c_irq_chip,
						 handle_level_irq);
		break;
	default:
		pr_err("irq-s3c24xx: unsupported irqtype %d\n", irq_data->type);
		return -EINVAL;
	}
	set_irq_flags(virq, IRQF_VALID);

	if (irq_data->parent_irq) {
		parent_intc = intc->parent;
		if (!parent_intc) {
			pr_err("irq-s3c24xx: no parent controller found for hwirq %lu\n",
			       hw);
			goto err;
		}

		parent_irq_data = &parent_intc->irqs[irq_data->parent_irq];
		if (!irq_data) {
			pr_err("irq-s3c24xx: no irq data found for hwirq %lu\n",
			       hw);
			goto err;
		}

		parent_irq_data->sub_intc = intc;
		parent_irq_data->sub_bits |= (1UL << hw);

		/* attach the demuxer to the parent irq */
		irqno = irq_find_mapping(parent_intc->domain,
					 irq_data->parent_irq);
		if (!irqno) {
			pr_err("irq-s3c24xx: could not find mapping for parent irq %lu\n",
			       irq_data->parent_irq);
			goto err;
		}
		irq_set_chained_handler(irqno, s3c_irq_demux);
	}

	return 0;

err:
	set_irq_flags(virq, 0);

	/* the only error can result from bad mapping data*/
	return -EINVAL;
}

static struct irq_domain_ops s3c24xx_irq_ops = {
	.map = s3c24xx_irq_map,
	.xlate = irq_domain_xlate_twocell,
};

static void s3c24xx_clear_intc(struct s3c_irq_intc *intc)
{
	void __iomem *reg_source;
	unsigned long pend;
	unsigned long last;
	int i;

	/* if intpnd is set, read the next pending irq from there */
	reg_source = intc->reg_intpnd ? intc->reg_intpnd : intc->reg_pending;

	last = 0;
	for (i = 0; i < 4; i++) {
		pend = __raw_readl(reg_source);

		if (pend == 0 || pend == last)
			break;

		__raw_writel(pend, intc->reg_pending);
		if (intc->reg_intpnd)
			__raw_writel(pend, intc->reg_intpnd);

		pr_info("irq: clearing pending status %08x\n", (int)pend);
		last = pend;
	}
}

struct s3c_irq_intc *s3c24xx_init_intc(struct device_node *np,
				       struct s3c_irq_data *irq_data,
				       struct s3c_irq_intc *parent,
				       unsigned long address)
{
	struct s3c_irq_intc *intc;
	void __iomem *base = (void *)0xf6000000; /* static mapping */
	int irq_num;
	int irq_start;
	int irq_offset;
	int ret;

	intc = kzalloc(sizeof(struct s3c_irq_intc), GFP_KERNEL);
	if (!intc)
		return ERR_PTR(-ENOMEM);

	intc->irqs = irq_data;

	if (parent)
		intc->parent = parent;

	/* select the correct data for the controller.
	 * Need to hard code the irq num start and offset
	 * to preserve the static mapping for now
	 */
	switch (address) {
	case 0x4a000000:
		pr_debug("irq: found main intc\n");
		intc->reg_pending = base;
		intc->reg_mask = base + 0x08;
		intc->reg_intpnd = base + 0x10;
		irq_num = 32;
		irq_start = S3C2410_IRQ(0);
		irq_offset = 0;
		break;
	case 0x4a000018:
		pr_debug("irq: found subintc\n");
		intc->reg_pending = base + 0x18;
		intc->reg_mask = base + 0x1c;
		irq_num = 29;
		irq_start = S3C2410_IRQSUB(0);
		irq_offset = 0;
		break;
	case 0x4a000040:
		pr_debug("irq: found intc2\n");
		intc->reg_pending = base + 0x40;
		intc->reg_mask = base + 0x48;
		intc->reg_intpnd = base + 0x50;
		irq_num = 8;
		irq_start = S3C2416_IRQ(0);
		irq_offset = 0;
		break;
	case 0x560000a4:
		pr_debug("irq: found eintc\n");
		base = (void *)0xfd000000;

		intc->reg_mask = base + 0xa4;
		intc->reg_pending = base + 0x08;
		irq_num = 20;
		irq_start = S3C2410_IRQ(32);
		irq_offset = 4;
		break;
	default:
		pr_err("irq: unsupported controller address\n");
		ret = -EINVAL;
		goto err;
	}

	/* now that all the data is complete, init the irq-domain */
	s3c24xx_clear_intc(intc);
	intc->domain = irq_domain_add_legacy(np, irq_num, irq_start,
					     irq_offset, &s3c24xx_irq_ops,
					     intc);
	if (!intc->domain) {
		pr_err("irq: could not create irq-domain\n");
		ret = -EINVAL;
		goto err;
	}

	return intc;

err:
	kfree(intc);
	return ERR_PTR(ret);
}

/* s3c24xx_init_irq
 *
 * Initialise S3C2410 IRQ system
*/

static struct s3c_irq_data init_base[32] = {
	{ .type = S3C_IRQTYPE_EINT, }, /* EINT0 */
	{ .type = S3C_IRQTYPE_EINT, }, /* EINT1 */
	{ .type = S3C_IRQTYPE_EINT, }, /* EINT2 */
	{ .type = S3C_IRQTYPE_EINT, }, /* EINT3 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* EINT4to7 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* EINT8to23 */
	{ .type = S3C_IRQTYPE_NONE, }, /* reserved */
	{ .type = S3C_IRQTYPE_EDGE, }, /* nBATT_FLT */
	{ .type = S3C_IRQTYPE_EDGE, }, /* TICK */
	{ .type = S3C_IRQTYPE_EDGE, }, /* WDT */
	{ .type = S3C_IRQTYPE_EDGE, }, /* TIMER0 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* TIMER1 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* TIMER2 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* TIMER3 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* TIMER4 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* UART2 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* LCD */
	{ .type = S3C_IRQTYPE_EDGE, }, /* DMA0 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* DMA1 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* DMA2 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* DMA3 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* SDI */
	{ .type = S3C_IRQTYPE_EDGE, }, /* SPI0 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* UART1 */
	{ .type = S3C_IRQTYPE_NONE, }, /* reserved */
	{ .type = S3C_IRQTYPE_EDGE, }, /* USBD */
	{ .type = S3C_IRQTYPE_EDGE, }, /* USBH */
	{ .type = S3C_IRQTYPE_EDGE, }, /* IIC */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* UART0 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* SPI1 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* RTC */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* ADCPARENT */
};

static struct s3c_irq_data init_eint[32] = {
	{ .type = S3C_IRQTYPE_NONE, }, /* reserved */
	{ .type = S3C_IRQTYPE_NONE, }, /* reserved */
	{ .type = S3C_IRQTYPE_NONE, }, /* reserved */
	{ .type = S3C_IRQTYPE_NONE, }, /* reserved */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 4 }, /* EINT4 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 4 }, /* EINT5 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 4 }, /* EINT6 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 4 }, /* EINT7 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 5 }, /* EINT8 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 5 }, /* EINT9 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 5 }, /* EINT10 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 5 }, /* EINT11 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 5 }, /* EINT12 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 5 }, /* EINT13 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 5 }, /* EINT14 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 5 }, /* EINT15 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 5 }, /* EINT16 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 5 }, /* EINT17 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 5 }, /* EINT18 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 5 }, /* EINT19 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 5 }, /* EINT20 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 5 }, /* EINT21 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 5 }, /* EINT22 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 5 }, /* EINT23 */
};

static struct s3c_irq_data init_subint[32] = {
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 28 }, /* UART0-RX */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 28 }, /* UART0-TX */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 28 }, /* UART0-ERR */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 23 }, /* UART1-RX */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 23 }, /* UART1-TX */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 23 }, /* UART1-ERR */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 15 }, /* UART2-RX */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 15 }, /* UART2-TX */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 15 }, /* UART2-ERR */
	{ .type = S3C_IRQTYPE_EDGE, .parent_irq = 31 }, /* TC */
	{ .type = S3C_IRQTYPE_EDGE, .parent_irq = 31 }, /* ADC */
};

void __init s3c24xx_init_irq(void)
{
	struct s3c_irq_intc *main_intc;

#ifdef CONFIG_FIQ
	init_FIQ(FIQ_START);
#endif

	main_intc = s3c24xx_init_intc(NULL, &init_base[0], NULL, 0x4a000000);
	if (IS_ERR(main_intc)) {
		pr_err("irq: could not create main interrupt controller\n");
		return;
	}

	s3c24xx_init_intc(NULL, &init_subint[0], main_intc, 0x4a000018);
	s3c24xx_init_intc(NULL, &init_eint[0], main_intc, 0x560000a4);
}

#ifdef CONFIG_CPU_S3C2416
#define INTMSK(start, end) ((1 << ((end) + 1 - (start))) - 1)

static inline void s3c2416_irq_demux(unsigned int irq, unsigned int len)
{
	unsigned int subsrc, submsk;
	unsigned int end;

	/* read the current pending interrupts, and the mask
	 * for what it is available */

	subsrc = __raw_readl(S3C2410_SUBSRCPND);
	submsk = __raw_readl(S3C2410_INTSUBMSK);

	subsrc  &= ~submsk;
	subsrc >>= (irq - S3C2410_IRQSUB(0));
	subsrc  &= (1 << len)-1;

	end = len + irq;

	for (; irq < end && subsrc; irq++) {
		if (subsrc & 1)
			generic_handle_irq(irq);

		subsrc >>= 1;
	}
}

/* WDT/AC97 sub interrupts */

static void s3c2416_irq_demux_wdtac97(unsigned int irq, struct irq_desc *desc)
{
	s3c2416_irq_demux(IRQ_S3C2443_WDT, 4);
}

#define INTMSK_WDTAC97	(1UL << (IRQ_WDT - IRQ_EINT0))
#define SUBMSK_WDTAC97	INTMSK(IRQ_S3C2443_WDT, IRQ_S3C2443_AC97)

static void s3c2416_irq_wdtac97_mask(struct irq_data *data)
{
	s3c_irqsub_mask(data->irq, INTMSK_WDTAC97, SUBMSK_WDTAC97);
}

static void s3c2416_irq_wdtac97_unmask(struct irq_data *data)
{
	s3c_irqsub_unmask(data->irq, INTMSK_WDTAC97);
}

static void s3c2416_irq_wdtac97_ack(struct irq_data *data)
{
	s3c_irqsub_maskack(data->irq, INTMSK_WDTAC97, SUBMSK_WDTAC97);
}

static struct irq_chip s3c2416_irq_wdtac97 = {
	.irq_mask	= s3c2416_irq_wdtac97_mask,
	.irq_unmask	= s3c2416_irq_wdtac97_unmask,
	.irq_ack	= s3c2416_irq_wdtac97_ack,
};

/* LCD sub interrupts */

static void s3c2416_irq_demux_lcd(unsigned int irq, struct irq_desc *desc)
{
	s3c2416_irq_demux(IRQ_S3C2443_LCD1, 4);
}

#define INTMSK_LCD	(1UL << (IRQ_LCD - IRQ_EINT0))
#define SUBMSK_LCD	INTMSK(IRQ_S3C2443_LCD1, IRQ_S3C2443_LCD4)

static void s3c2416_irq_lcd_mask(struct irq_data *data)
{
	s3c_irqsub_mask(data->irq, INTMSK_LCD, SUBMSK_LCD);
}

static void s3c2416_irq_lcd_unmask(struct irq_data *data)
{
	s3c_irqsub_unmask(data->irq, INTMSK_LCD);
}

static void s3c2416_irq_lcd_ack(struct irq_data *data)
{
	s3c_irqsub_maskack(data->irq, INTMSK_LCD, SUBMSK_LCD);
}

static struct irq_chip s3c2416_irq_lcd = {
	.irq_mask	= s3c2416_irq_lcd_mask,
	.irq_unmask	= s3c2416_irq_lcd_unmask,
	.irq_ack	= s3c2416_irq_lcd_ack,
};

/* DMA sub interrupts */

static void s3c2416_irq_demux_dma(unsigned int irq, struct irq_desc *desc)
{
	s3c2416_irq_demux(IRQ_S3C2443_DMA0, 6);
}

#define INTMSK_DMA	(1UL << (IRQ_S3C2443_DMA - IRQ_EINT0))
#define SUBMSK_DMA	INTMSK(IRQ_S3C2443_DMA0, IRQ_S3C2443_DMA5)


static void s3c2416_irq_dma_mask(struct irq_data *data)
{
	s3c_irqsub_mask(data->irq, INTMSK_DMA, SUBMSK_DMA);
}

static void s3c2416_irq_dma_unmask(struct irq_data *data)
{
	s3c_irqsub_unmask(data->irq, INTMSK_DMA);
}

static void s3c2416_irq_dma_ack(struct irq_data *data)
{
	s3c_irqsub_maskack(data->irq, INTMSK_DMA, SUBMSK_DMA);
}

static struct irq_chip s3c2416_irq_dma = {
	.irq_mask	= s3c2416_irq_dma_mask,
	.irq_unmask	= s3c2416_irq_dma_unmask,
	.irq_ack	= s3c2416_irq_dma_ack,
};

/* UART3 sub interrupts */

static void s3c2416_irq_demux_uart3(unsigned int irq, struct irq_desc *desc)
{
	s3c2416_irq_demux(IRQ_S3C2443_RX3, 3);
}

#define INTMSK_UART3	(1UL << (IRQ_S3C2443_UART3 - IRQ_EINT0))
#define SUBMSK_UART3	(0x7 << (IRQ_S3C2443_RX3 - S3C2410_IRQSUB(0)))

static void s3c2416_irq_uart3_mask(struct irq_data *data)
{
	s3c_irqsub_mask(data->irq, INTMSK_UART3, SUBMSK_UART3);
}

static void s3c2416_irq_uart3_unmask(struct irq_data *data)
{
	s3c_irqsub_unmask(data->irq, INTMSK_UART3);
}

static void s3c2416_irq_uart3_ack(struct irq_data *data)
{
	s3c_irqsub_maskack(data->irq, INTMSK_UART3, SUBMSK_UART3);
}

static struct irq_chip s3c2416_irq_uart3 = {
	.irq_mask	= s3c2416_irq_uart3_mask,
	.irq_unmask	= s3c2416_irq_uart3_unmask,
	.irq_ack	= s3c2416_irq_uart3_ack,
};

/* second interrupt register */

static inline void s3c2416_irq_ack_second(struct irq_data *data)
{
	unsigned long bitval = 1UL << (data->irq - IRQ_S3C2416_2D);

	__raw_writel(bitval, S3C2416_SRCPND2);
	__raw_writel(bitval, S3C2416_INTPND2);
}

static void s3c2416_irq_mask_second(struct irq_data *data)
{
	unsigned long bitval = 1UL << (data->irq - IRQ_S3C2416_2D);
	unsigned long mask;

	mask = __raw_readl(S3C2416_INTMSK2);
	mask |= bitval;
	__raw_writel(mask, S3C2416_INTMSK2);
}

static void s3c2416_irq_unmask_second(struct irq_data *data)
{
	unsigned long bitval = 1UL << (data->irq - IRQ_S3C2416_2D);
	unsigned long mask;

	mask = __raw_readl(S3C2416_INTMSK2);
	mask &= ~bitval;
	__raw_writel(mask, S3C2416_INTMSK2);
}

static struct irq_chip s3c2416_irq_second = {
	.irq_ack	= s3c2416_irq_ack_second,
	.irq_mask	= s3c2416_irq_mask_second,
	.irq_unmask	= s3c2416_irq_unmask_second,
};


/* IRQ initialisation code */

static int s3c2416_add_sub(unsigned int base,
				   void (*demux)(unsigned int,
						 struct irq_desc *),
				   struct irq_chip *chip,
				   unsigned int start, unsigned int end)
{
	unsigned int irqno;

	irq_set_chip_and_handler(base, &s3c_irq_level_chip, handle_level_irq);
	irq_set_chained_handler(base, demux);

	for (irqno = start; irqno <= end; irqno++) {
		irq_set_chip_and_handler(irqno, chip, handle_level_irq);
		set_irq_flags(irqno, IRQF_VALID);
	}

	return 0;
}

static void s3c2416_irq_add_second(void)
{
	unsigned long pend;
	unsigned long last;
	int irqno;
	int i;

	/* first, clear all interrupts pending... */
	last = 0;
	for (i = 0; i < 4; i++) {
		pend = __raw_readl(S3C2416_INTPND2);

		if (pend == 0 || pend == last)
			break;

		__raw_writel(pend, S3C2416_SRCPND2);
		__raw_writel(pend, S3C2416_INTPND2);
		printk(KERN_INFO "irq: clearing pending status %08x\n",
		       (int)pend);
		last = pend;
	}

	for (irqno = IRQ_S3C2416_2D; irqno <= IRQ_S3C2416_I2S1; irqno++) {
		switch (irqno) {
		case IRQ_S3C2416_RESERVED2:
		case IRQ_S3C2416_RESERVED3:
			/* no IRQ here */
			break;
		default:
			irq_set_chip_and_handler(irqno, &s3c2416_irq_second,
						 handle_edge_irq);
			set_irq_flags(irqno, IRQF_VALID);
		}
	}
}

void __init s3c2416_init_irq(void)
{
	pr_info("S3C2416: IRQ Support\n");

	s3c24xx_init_irq();

	s3c2416_add_sub(IRQ_LCD, s3c2416_irq_demux_lcd, &s3c2416_irq_lcd,
			IRQ_S3C2443_LCD2, IRQ_S3C2443_LCD4);

	s3c2416_add_sub(IRQ_S3C2443_DMA, s3c2416_irq_demux_dma,
			&s3c2416_irq_dma, IRQ_S3C2443_DMA0, IRQ_S3C2443_DMA5);

	s3c2416_add_sub(IRQ_S3C2443_UART3, s3c2416_irq_demux_uart3,
			&s3c2416_irq_uart3,
			IRQ_S3C2443_RX3, IRQ_S3C2443_ERR3);

	s3c2416_add_sub(IRQ_WDT, s3c2416_irq_demux_wdtac97,
			&s3c2416_irq_wdtac97,
			IRQ_S3C2443_WDT, IRQ_S3C2443_AC97);

	s3c2416_irq_add_second();
}

#endif
