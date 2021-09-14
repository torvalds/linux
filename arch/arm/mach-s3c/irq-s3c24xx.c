// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * S3C24XX IRQ handling
 *
 * Copyright (c) 2003-2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 * Copyright (c) 2012 Heiko Stuebner <heiko@sntech.de>
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
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/spi/s3c24xx.h>

#include <asm/exception.h>
#include <asm/mach/irq.h>

#include <mach/irqs.h>
#include "regs-irq.h"
#include "regs-gpio.h"

#include "cpu.h"
#include "regs-irqtype.h"
#include "pm.h"
#include "s3c24xx.h"

#define S3C_IRQTYPE_NONE	0
#define S3C_IRQTYPE_EINT	1
#define S3C_IRQTYPE_EDGE	2
#define S3C_IRQTYPE_LEVEL	3

struct s3c_irq_data {
	unsigned int type;
	unsigned long offset;
	unsigned long parent_irq;

	/* data gets filled during init */
	struct s3c_irq_intc *intc;
	unsigned long sub_bits;
	struct s3c_irq_intc *sub_intc;
};

/*
 * Structure holding the controller data
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

/*
 * Array holding pointers to the global controller structs
 * [0] ... main_intc
 * [1] ... sub_intc
 * [2] ... main_intc2 on s3c2416
 */
static struct s3c_irq_intc *s3c_intc[3];

static void s3c_irq_mask(struct irq_data *data)
{
	struct s3c_irq_data *irq_data = irq_data_get_irq_chip_data(data);
	struct s3c_irq_intc *intc = irq_data->intc;
	struct s3c_irq_intc *parent_intc = intc->parent;
	struct s3c_irq_data *parent_data;
	unsigned long mask;
	unsigned int irqno;

	mask = readl_relaxed(intc->reg_mask);
	mask |= (1UL << irq_data->offset);
	writel_relaxed(mask, intc->reg_mask);

	if (parent_intc) {
		parent_data = &parent_intc->irqs[irq_data->parent_irq];

		/* check to see if we need to mask the parent IRQ
		 * The parent_irq is always in main_intc, so the hwirq
		 * for find_mapping does not need an offset in any case.
		 */
		if ((mask & parent_data->sub_bits) == parent_data->sub_bits) {
			irqno = irq_find_mapping(parent_intc->domain,
					 irq_data->parent_irq);
			s3c_irq_mask(irq_get_irq_data(irqno));
		}
	}
}

static void s3c_irq_unmask(struct irq_data *data)
{
	struct s3c_irq_data *irq_data = irq_data_get_irq_chip_data(data);
	struct s3c_irq_intc *intc = irq_data->intc;
	struct s3c_irq_intc *parent_intc = intc->parent;
	unsigned long mask;
	unsigned int irqno;

	mask = readl_relaxed(intc->reg_mask);
	mask &= ~(1UL << irq_data->offset);
	writel_relaxed(mask, intc->reg_mask);

	if (parent_intc) {
		irqno = irq_find_mapping(parent_intc->domain,
					 irq_data->parent_irq);
		s3c_irq_unmask(irq_get_irq_data(irqno));
	}
}

static inline void s3c_irq_ack(struct irq_data *data)
{
	struct s3c_irq_data *irq_data = irq_data_get_irq_chip_data(data);
	struct s3c_irq_intc *intc = irq_data->intc;
	unsigned long bitval = 1UL << irq_data->offset;

	writel_relaxed(bitval, intc->reg_pending);
	if (intc->reg_intpnd)
		writel_relaxed(bitval, intc->reg_intpnd);
}

static int s3c_irq_type(struct irq_data *data, unsigned int type)
{
	switch (type) {
	case IRQ_TYPE_NONE:
		break;
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_EDGE_BOTH:
		irq_set_handler(data->irq, handle_edge_irq);
		break;
	case IRQ_TYPE_LEVEL_LOW:
	case IRQ_TYPE_LEVEL_HIGH:
		irq_set_handler(data->irq, handle_level_irq);
		break;
	default:
		pr_err("No such irq type %d\n", type);
		return -EINVAL;
	}

	return 0;
}

static int s3c_irqext_type_set(void __iomem *gpcon_reg,
			       void __iomem *extint_reg,
			       unsigned long gpcon_offset,
			       unsigned long extint_offset,
			       unsigned int type)
{
	unsigned long newvalue = 0, value;

	/* Set the GPIO to external interrupt mode */
	value = readl_relaxed(gpcon_reg);
	value = (value & ~(3 << gpcon_offset)) | (0x02 << gpcon_offset);
	writel_relaxed(value, gpcon_reg);

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
			pr_err("No such irq type %d\n", type);
			return -EINVAL;
	}

	value = readl_relaxed(extint_reg);
	value = (value & ~(7 << extint_offset)) | (newvalue << extint_offset);
	writel_relaxed(value, extint_reg);

	return 0;
}

static int s3c_irqext_type(struct irq_data *data, unsigned int type)
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

	if (data->hwirq <= 3) {
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

static struct irq_chip s3c_irq_chip = {
	.name		= "s3c",
	.irq_ack	= s3c_irq_ack,
	.irq_mask	= s3c_irq_mask,
	.irq_unmask	= s3c_irq_unmask,
	.irq_set_type	= s3c_irq_type,
	.irq_set_wake	= s3c_irq_wake
};

static struct irq_chip s3c_irq_level_chip = {
	.name		= "s3c-level",
	.irq_mask	= s3c_irq_mask,
	.irq_unmask	= s3c_irq_unmask,
	.irq_ack	= s3c_irq_ack,
	.irq_set_type	= s3c_irq_type,
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

static void s3c_irq_demux(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct s3c_irq_data *irq_data = irq_desc_get_chip_data(desc);
	struct s3c_irq_intc *intc = irq_data->intc;
	struct s3c_irq_intc *sub_intc = irq_data->sub_intc;
	unsigned int n, offset;
	unsigned long src, msk;

	/* we're using individual domains for the non-dt case
	 * and one big domain for the dt case where the subintc
	 * starts at hwirq number 32.
	 */
	offset = irq_domain_get_of_node(intc->domain) ? 32 : 0;

	chained_irq_enter(chip, desc);

	src = readl_relaxed(sub_intc->reg_pending);
	msk = readl_relaxed(sub_intc->reg_mask);

	src &= ~msk;
	src &= irq_data->sub_bits;

	while (src) {
		n = __ffs(src);
		src &= ~(1 << n);
		generic_handle_domain_irq(sub_intc->domain, offset + n);
	}

	chained_irq_exit(chip, desc);
}

static inline int s3c24xx_handle_intc(struct s3c_irq_intc *intc,
				      struct pt_regs *regs, int intc_offset)
{
	int pnd;
	int offset;

	pnd = readl_relaxed(intc->reg_intpnd);
	if (!pnd)
		return false;

	/* non-dt machines use individual domains */
	if (!irq_domain_get_of_node(intc->domain))
		intc_offset = 0;

	/* We have a problem that the INTOFFSET register does not always
	 * show one interrupt. Occasionally we get two interrupts through
	 * the prioritiser, and this causes the INTOFFSET register to show
	 * what looks like the logical-or of the two interrupt numbers.
	 *
	 * Thanks to Klaus, Shannon, et al for helping to debug this problem
	 */
	offset = readl_relaxed(intc->reg_intpnd + 4);

	/* Find the bit manually, when the offset is wrong.
	 * The pending register only ever contains the one bit of the next
	 * interrupt to handle.
	 */
	if (!(pnd & (1 << offset)))
		offset =  __ffs(pnd);

	handle_domain_irq(intc->domain, intc_offset + offset, regs);
	return true;
}

static asmlinkage void __exception_irq_entry s3c24xx_handle_irq(struct pt_regs *regs)
{
	do {
		if (likely(s3c_intc[0]))
			if (s3c24xx_handle_intc(s3c_intc[0], regs, 0))
				continue;

		if (s3c_intc[2])
			if (s3c24xx_handle_intc(s3c_intc[2], regs, 64))
				continue;

		break;
	} while (1);
}

#ifdef CONFIG_FIQ
/**
 * s3c24xx_set_fiq - set the FIQ routing
 * @irq: IRQ number to route to FIQ on processor.
 * @ack_ptr: pointer to a location for storing the bit mask
 * @on: Whether to route @irq to the FIQ, or to remove the FIQ routing.
 *
 * Change the state of the IRQ to FIQ routing depending on @irq and @on. If
 * @on is true, the @irq is checked to see if it can be routed and the
 * interrupt controller updated to route the IRQ. If @on is false, the FIQ
 * routing is cleared, regardless of which @irq is specified.
 *
 * returns the mask value for the register.
 */
int s3c24xx_set_fiq(unsigned int irq, u32 *ack_ptr, bool on)
{
	u32 intmod;
	unsigned offs;

	if (on) {
		offs = irq - FIQ_START;
		if (offs > 31)
			return 0;

		intmod = 1 << offs;
	} else {
		intmod = 0;
	}

	if (ack_ptr)
		*ack_ptr = intmod;
	writel_relaxed(intmod, S3C2410_INTMOD);

	return intmod;
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

	/* attach controller pointer to irq_data */
	irq_data->intc = intc;
	irq_data->offset = hw;

	parent_intc = intc->parent;

	/* set handler and flags */
	switch (irq_data->type) {
	case S3C_IRQTYPE_NONE:
		return 0;
	case S3C_IRQTYPE_EINT:
		/* On the S3C2412, the EINT0to3 have a parent irq
		 * but need the s3c_irq_eint0t4 chip
		 */
		if (parent_intc && (!soc_is_s3c2412() || hw >= 4))
			irq_set_chip_and_handler(virq, &s3c_irqext_chip,
						 handle_edge_irq);
		else
			irq_set_chip_and_handler(virq, &s3c_irq_eint0t4,
						 handle_edge_irq);
		break;
	case S3C_IRQTYPE_EDGE:
		if (parent_intc || intc->reg_pending == S3C2416_SRCPND2)
			irq_set_chip_and_handler(virq, &s3c_irq_level_chip,
						 handle_edge_irq);
		else
			irq_set_chip_and_handler(virq, &s3c_irq_chip,
						 handle_edge_irq);
		break;
	case S3C_IRQTYPE_LEVEL:
		if (parent_intc)
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

	irq_set_chip_data(virq, irq_data);

	if (parent_intc && irq_data->type != S3C_IRQTYPE_NONE) {
		if (irq_data->parent_irq > 31) {
			pr_err("irq-s3c24xx: parent irq %lu is out of range\n",
			       irq_data->parent_irq);
			return -EINVAL;
		}

		parent_irq_data = &parent_intc->irqs[irq_data->parent_irq];
		parent_irq_data->sub_intc = intc;
		parent_irq_data->sub_bits |= (1UL << hw);

		/* attach the demuxer to the parent irq */
		irqno = irq_find_mapping(parent_intc->domain,
					 irq_data->parent_irq);
		if (!irqno) {
			pr_err("irq-s3c24xx: could not find mapping for parent irq %lu\n",
			       irq_data->parent_irq);
			return -EINVAL;
		}
		irq_set_chained_handler(irqno, s3c_irq_demux);
	}

	return 0;
}

static const struct irq_domain_ops s3c24xx_irq_ops = {
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
		pend = readl_relaxed(reg_source);

		if (pend == 0 || pend == last)
			break;

		writel_relaxed(pend, intc->reg_pending);
		if (intc->reg_intpnd)
			writel_relaxed(pend, intc->reg_intpnd);

		pr_info("irq: clearing pending status %08x\n", (int)pend);
		last = pend;
	}
}

static struct s3c_irq_intc * __init s3c24xx_init_intc(struct device_node *np,
				       struct s3c_irq_data *irq_data,
				       struct s3c_irq_intc *parent,
				       unsigned long address)
{
	struct s3c_irq_intc *intc;
	void __iomem *base = (void *)0xf6000000; /* static mapping */
	int irq_num;
	int irq_start;
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
		break;
	case 0x4a000018:
		pr_debug("irq: found subintc\n");
		intc->reg_pending = base + 0x18;
		intc->reg_mask = base + 0x1c;
		irq_num = 29;
		irq_start = S3C2410_IRQSUB(0);
		break;
	case 0x4a000040:
		pr_debug("irq: found intc2\n");
		intc->reg_pending = base + 0x40;
		intc->reg_mask = base + 0x48;
		intc->reg_intpnd = base + 0x50;
		irq_num = 8;
		irq_start = S3C2416_IRQ(0);
		break;
	case 0x560000a4:
		pr_debug("irq: found eintc\n");
		base = (void *)0xfd000000;

		intc->reg_mask = base + 0xa4;
		intc->reg_pending = base + 0xa8;
		irq_num = 24;
		irq_start = S3C2410_IRQ(32);
		break;
	default:
		pr_err("irq: unsupported controller address\n");
		ret = -EINVAL;
		goto err;
	}

	/* now that all the data is complete, init the irq-domain */
	s3c24xx_clear_intc(intc);
	intc->domain = irq_domain_add_legacy(np, irq_num, irq_start,
					     0, &s3c24xx_irq_ops,
					     intc);
	if (!intc->domain) {
		pr_err("irq: could not create irq-domain\n");
		ret = -EINVAL;
		goto err;
	}

	set_handle_irq(s3c24xx_handle_irq);

	return intc;

err:
	kfree(intc);
	return ERR_PTR(ret);
}

static struct s3c_irq_data __maybe_unused init_eint[32] = {
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

#ifdef CONFIG_CPU_S3C2410
static struct s3c_irq_data init_s3c2410base[32] = {
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

static struct s3c_irq_data init_s3c2410subint[32] = {
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

void __init s3c2410_init_irq(void)
{
#ifdef CONFIG_FIQ
	init_FIQ(FIQ_START);
#endif

	s3c_intc[0] = s3c24xx_init_intc(NULL, &init_s3c2410base[0], NULL,
					0x4a000000);
	if (IS_ERR(s3c_intc[0])) {
		pr_err("irq: could not create main interrupt controller\n");
		return;
	}

	s3c_intc[1] = s3c24xx_init_intc(NULL, &init_s3c2410subint[0],
					s3c_intc[0], 0x4a000018);
	s3c24xx_init_intc(NULL, &init_eint[0], s3c_intc[0], 0x560000a4);
}
#endif

#ifdef CONFIG_CPU_S3C2412
static struct s3c_irq_data init_s3c2412base[32] = {
	{ .type = S3C_IRQTYPE_LEVEL, }, /* EINT0 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* EINT1 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* EINT2 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* EINT3 */
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
	{ .type = S3C_IRQTYPE_LEVEL, }, /* SDI/CF */
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

static struct s3c_irq_data init_s3c2412eint[32] = {
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 0 }, /* EINT0 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 1 }, /* EINT1 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 2 }, /* EINT2 */
	{ .type = S3C_IRQTYPE_EINT, .parent_irq = 3 }, /* EINT3 */
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

static struct s3c_irq_data init_s3c2412subint[32] = {
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
	{ .type = S3C_IRQTYPE_NONE, },
	{ .type = S3C_IRQTYPE_NONE, },
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 21 }, /* SDI */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 21 }, /* CF */
};

void __init s3c2412_init_irq(void)
{
	pr_info("S3C2412: IRQ Support\n");

#ifdef CONFIG_FIQ
	init_FIQ(FIQ_START);
#endif

	s3c_intc[0] = s3c24xx_init_intc(NULL, &init_s3c2412base[0], NULL,
					0x4a000000);
	if (IS_ERR(s3c_intc[0])) {
		pr_err("irq: could not create main interrupt controller\n");
		return;
	}

	s3c24xx_init_intc(NULL, &init_s3c2412eint[0], s3c_intc[0], 0x560000a4);
	s3c_intc[1] = s3c24xx_init_intc(NULL, &init_s3c2412subint[0],
					s3c_intc[0], 0x4a000018);
}
#endif

#ifdef CONFIG_CPU_S3C2416
static struct s3c_irq_data init_s3c2416base[32] = {
	{ .type = S3C_IRQTYPE_EINT, }, /* EINT0 */
	{ .type = S3C_IRQTYPE_EINT, }, /* EINT1 */
	{ .type = S3C_IRQTYPE_EINT, }, /* EINT2 */
	{ .type = S3C_IRQTYPE_EINT, }, /* EINT3 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* EINT4to7 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* EINT8to23 */
	{ .type = S3C_IRQTYPE_NONE, }, /* reserved */
	{ .type = S3C_IRQTYPE_EDGE, }, /* nBATT_FLT */
	{ .type = S3C_IRQTYPE_EDGE, }, /* TICK */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* WDT/AC97 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* TIMER0 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* TIMER1 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* TIMER2 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* TIMER3 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* TIMER4 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* UART2 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* LCD */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* DMA */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* UART3 */
	{ .type = S3C_IRQTYPE_NONE, }, /* reserved */
	{ .type = S3C_IRQTYPE_EDGE, }, /* SDI1 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* SDI0 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* SPI0 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* UART1 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* NAND */
	{ .type = S3C_IRQTYPE_EDGE, }, /* USBD */
	{ .type = S3C_IRQTYPE_EDGE, }, /* USBH */
	{ .type = S3C_IRQTYPE_EDGE, }, /* IIC */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* UART0 */
	{ .type = S3C_IRQTYPE_NONE, },
	{ .type = S3C_IRQTYPE_EDGE, }, /* RTC */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* ADCPARENT */
};

static struct s3c_irq_data init_s3c2416subint[32] = {
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
	{ .type = S3C_IRQTYPE_NONE }, /* reserved */
	{ .type = S3C_IRQTYPE_NONE }, /* reserved */
	{ .type = S3C_IRQTYPE_NONE }, /* reserved */
	{ .type = S3C_IRQTYPE_NONE }, /* reserved */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 16 }, /* LCD2 */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 16 }, /* LCD3 */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 16 }, /* LCD4 */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 17 }, /* DMA0 */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 17 }, /* DMA1 */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 17 }, /* DMA2 */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 17 }, /* DMA3 */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 17 }, /* DMA4 */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 17 }, /* DMA5 */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 18 }, /* UART3-RX */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 18 }, /* UART3-TX */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 18 }, /* UART3-ERR */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 9 }, /* WDT */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 9 }, /* AC97 */
};

static struct s3c_irq_data init_s3c2416_second[32] = {
	{ .type = S3C_IRQTYPE_EDGE }, /* 2D */
	{ .type = S3C_IRQTYPE_NONE }, /* reserved */
	{ .type = S3C_IRQTYPE_NONE }, /* reserved */
	{ .type = S3C_IRQTYPE_NONE }, /* reserved */
	{ .type = S3C_IRQTYPE_EDGE }, /* PCM0 */
	{ .type = S3C_IRQTYPE_NONE }, /* reserved */
	{ .type = S3C_IRQTYPE_EDGE }, /* I2S0 */
};

void __init s3c2416_init_irq(void)
{
	pr_info("S3C2416: IRQ Support\n");

#ifdef CONFIG_FIQ
	init_FIQ(FIQ_START);
#endif

	s3c_intc[0] = s3c24xx_init_intc(NULL, &init_s3c2416base[0], NULL,
					0x4a000000);
	if (IS_ERR(s3c_intc[0])) {
		pr_err("irq: could not create main interrupt controller\n");
		return;
	}

	s3c24xx_init_intc(NULL, &init_eint[0], s3c_intc[0], 0x560000a4);
	s3c_intc[1] = s3c24xx_init_intc(NULL, &init_s3c2416subint[0],
					s3c_intc[0], 0x4a000018);

	s3c_intc[2] = s3c24xx_init_intc(NULL, &init_s3c2416_second[0],
					NULL, 0x4a000040);
}

#endif

#ifdef CONFIG_CPU_S3C2440
static struct s3c_irq_data init_s3c2440base[32] = {
	{ .type = S3C_IRQTYPE_EINT, }, /* EINT0 */
	{ .type = S3C_IRQTYPE_EINT, }, /* EINT1 */
	{ .type = S3C_IRQTYPE_EINT, }, /* EINT2 */
	{ .type = S3C_IRQTYPE_EINT, }, /* EINT3 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* EINT4to7 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* EINT8to23 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* CAM */
	{ .type = S3C_IRQTYPE_EDGE, }, /* nBATT_FLT */
	{ .type = S3C_IRQTYPE_EDGE, }, /* TICK */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* WDT/AC97 */
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
	{ .type = S3C_IRQTYPE_LEVEL, }, /* NFCON */
	{ .type = S3C_IRQTYPE_EDGE, }, /* USBD */
	{ .type = S3C_IRQTYPE_EDGE, }, /* USBH */
	{ .type = S3C_IRQTYPE_EDGE, }, /* IIC */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* UART0 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* SPI1 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* RTC */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* ADCPARENT */
};

static struct s3c_irq_data init_s3c2440subint[32] = {
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
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 6 }, /* CAM_C */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 6 }, /* CAM_P */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 9 }, /* WDT */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 9 }, /* AC97 */
};

void __init s3c2440_init_irq(void)
{
	pr_info("S3C2440: IRQ Support\n");

#ifdef CONFIG_FIQ
	init_FIQ(FIQ_START);
#endif

	s3c_intc[0] = s3c24xx_init_intc(NULL, &init_s3c2440base[0], NULL,
					0x4a000000);
	if (IS_ERR(s3c_intc[0])) {
		pr_err("irq: could not create main interrupt controller\n");
		return;
	}

	s3c24xx_init_intc(NULL, &init_eint[0], s3c_intc[0], 0x560000a4);
	s3c_intc[1] = s3c24xx_init_intc(NULL, &init_s3c2440subint[0],
					s3c_intc[0], 0x4a000018);
}
#endif

#ifdef CONFIG_CPU_S3C2442
static struct s3c_irq_data init_s3c2442base[32] = {
	{ .type = S3C_IRQTYPE_EINT, }, /* EINT0 */
	{ .type = S3C_IRQTYPE_EINT, }, /* EINT1 */
	{ .type = S3C_IRQTYPE_EINT, }, /* EINT2 */
	{ .type = S3C_IRQTYPE_EINT, }, /* EINT3 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* EINT4to7 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* EINT8to23 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* CAM */
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
	{ .type = S3C_IRQTYPE_LEVEL, }, /* NFCON */
	{ .type = S3C_IRQTYPE_EDGE, }, /* USBD */
	{ .type = S3C_IRQTYPE_EDGE, }, /* USBH */
	{ .type = S3C_IRQTYPE_EDGE, }, /* IIC */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* UART0 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* SPI1 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* RTC */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* ADCPARENT */
};

static struct s3c_irq_data init_s3c2442subint[32] = {
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
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 6 }, /* CAM_C */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 6 }, /* CAM_P */
};

void __init s3c2442_init_irq(void)
{
	pr_info("S3C2442: IRQ Support\n");

#ifdef CONFIG_FIQ
	init_FIQ(FIQ_START);
#endif

	s3c_intc[0] = s3c24xx_init_intc(NULL, &init_s3c2442base[0], NULL,
					0x4a000000);
	if (IS_ERR(s3c_intc[0])) {
		pr_err("irq: could not create main interrupt controller\n");
		return;
	}

	s3c24xx_init_intc(NULL, &init_eint[0], s3c_intc[0], 0x560000a4);
	s3c_intc[1] = s3c24xx_init_intc(NULL, &init_s3c2442subint[0],
					s3c_intc[0], 0x4a000018);
}
#endif

#ifdef CONFIG_CPU_S3C2443
static struct s3c_irq_data init_s3c2443base[32] = {
	{ .type = S3C_IRQTYPE_EINT, }, /* EINT0 */
	{ .type = S3C_IRQTYPE_EINT, }, /* EINT1 */
	{ .type = S3C_IRQTYPE_EINT, }, /* EINT2 */
	{ .type = S3C_IRQTYPE_EINT, }, /* EINT3 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* EINT4to7 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* EINT8to23 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* CAM */
	{ .type = S3C_IRQTYPE_EDGE, }, /* nBATT_FLT */
	{ .type = S3C_IRQTYPE_EDGE, }, /* TICK */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* WDT/AC97 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* TIMER0 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* TIMER1 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* TIMER2 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* TIMER3 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* TIMER4 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* UART2 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* LCD */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* DMA */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* UART3 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* CFON */
	{ .type = S3C_IRQTYPE_EDGE, }, /* SDI1 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* SDI0 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* SPI0 */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* UART1 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* NAND */
	{ .type = S3C_IRQTYPE_EDGE, }, /* USBD */
	{ .type = S3C_IRQTYPE_EDGE, }, /* USBH */
	{ .type = S3C_IRQTYPE_EDGE, }, /* IIC */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* UART0 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* SPI1 */
	{ .type = S3C_IRQTYPE_EDGE, }, /* RTC */
	{ .type = S3C_IRQTYPE_LEVEL, }, /* ADCPARENT */
};


static struct s3c_irq_data init_s3c2443subint[32] = {
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
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 6 }, /* CAM_C */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 6 }, /* CAM_P */
	{ .type = S3C_IRQTYPE_NONE }, /* reserved */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 16 }, /* LCD1 */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 16 }, /* LCD2 */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 16 }, /* LCD3 */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 16 }, /* LCD4 */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 17 }, /* DMA0 */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 17 }, /* DMA1 */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 17 }, /* DMA2 */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 17 }, /* DMA3 */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 17 }, /* DMA4 */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 17 }, /* DMA5 */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 18 }, /* UART3-RX */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 18 }, /* UART3-TX */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 18 }, /* UART3-ERR */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 9 }, /* WDT */
	{ .type = S3C_IRQTYPE_LEVEL, .parent_irq = 9 }, /* AC97 */
};

void __init s3c2443_init_irq(void)
{
	pr_info("S3C2443: IRQ Support\n");

#ifdef CONFIG_FIQ
	init_FIQ(FIQ_START);
#endif

	s3c_intc[0] = s3c24xx_init_intc(NULL, &init_s3c2443base[0], NULL,
					0x4a000000);
	if (IS_ERR(s3c_intc[0])) {
		pr_err("irq: could not create main interrupt controller\n");
		return;
	}

	s3c24xx_init_intc(NULL, &init_eint[0], s3c_intc[0], 0x560000a4);
	s3c_intc[1] = s3c24xx_init_intc(NULL, &init_s3c2443subint[0],
					s3c_intc[0], 0x4a000018);
}
#endif

#ifdef CONFIG_OF
static int s3c24xx_irq_map_of(struct irq_domain *h, unsigned int virq,
							irq_hw_number_t hw)
{
	unsigned int ctrl_num = hw / 32;
	unsigned int intc_hw = hw % 32;
	struct s3c_irq_intc *intc = s3c_intc[ctrl_num];
	struct s3c_irq_intc *parent_intc = intc->parent;
	struct s3c_irq_data *irq_data = &intc->irqs[intc_hw];

	/* attach controller pointer to irq_data */
	irq_data->intc = intc;
	irq_data->offset = intc_hw;

	if (!parent_intc)
		irq_set_chip_and_handler(virq, &s3c_irq_chip, handle_edge_irq);
	else
		irq_set_chip_and_handler(virq, &s3c_irq_level_chip,
					 handle_edge_irq);

	irq_set_chip_data(virq, irq_data);

	return 0;
}

/* Translate our of irq notation
 * format: <ctrl_num ctrl_irq parent_irq type>
 */
static int s3c24xx_irq_xlate_of(struct irq_domain *d, struct device_node *n,
			const u32 *intspec, unsigned int intsize,
			irq_hw_number_t *out_hwirq, unsigned int *out_type)
{
	struct s3c_irq_intc *intc;
	struct s3c_irq_intc *parent_intc;
	struct s3c_irq_data *irq_data;
	struct s3c_irq_data *parent_irq_data;
	int irqno;

	if (WARN_ON(intsize < 4))
		return -EINVAL;

	if (intspec[0] > 2 || !s3c_intc[intspec[0]]) {
		pr_err("controller number %d invalid\n", intspec[0]);
		return -EINVAL;
	}
	intc = s3c_intc[intspec[0]];

	*out_hwirq = intspec[0] * 32 + intspec[2];
	*out_type = intspec[3] & IRQ_TYPE_SENSE_MASK;

	parent_intc = intc->parent;
	if (parent_intc) {
		irq_data = &intc->irqs[intspec[2]];
		irq_data->parent_irq = intspec[1];
		parent_irq_data = &parent_intc->irqs[irq_data->parent_irq];
		parent_irq_data->sub_intc = intc;
		parent_irq_data->sub_bits |= (1UL << intspec[2]);

		/* parent_intc is always s3c_intc[0], so no offset */
		irqno = irq_create_mapping(parent_intc->domain, intspec[1]);
		if (irqno < 0) {
			pr_err("irq: could not map parent interrupt\n");
			return irqno;
		}

		irq_set_chained_handler(irqno, s3c_irq_demux);
	}

	return 0;
}

static const struct irq_domain_ops s3c24xx_irq_ops_of = {
	.map = s3c24xx_irq_map_of,
	.xlate = s3c24xx_irq_xlate_of,
};

struct s3c24xx_irq_of_ctrl {
	char			*name;
	unsigned long		offset;
	struct s3c_irq_intc	**handle;
	struct s3c_irq_intc	**parent;
	struct irq_domain_ops	*ops;
};

static int __init s3c_init_intc_of(struct device_node *np,
			struct device_node *interrupt_parent,
			struct s3c24xx_irq_of_ctrl *s3c_ctrl, int num_ctrl)
{
	struct s3c_irq_intc *intc;
	struct s3c24xx_irq_of_ctrl *ctrl;
	struct irq_domain *domain;
	void __iomem *reg_base;
	int i;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("irq-s3c24xx: could not map irq registers\n");
		return -EINVAL;
	}

	domain = irq_domain_add_linear(np, num_ctrl * 32,
						     &s3c24xx_irq_ops_of, NULL);
	if (!domain) {
		pr_err("irq: could not create irq-domain\n");
		return -EINVAL;
	}

	for (i = 0; i < num_ctrl; i++) {
		ctrl = &s3c_ctrl[i];

		pr_debug("irq: found controller %s\n", ctrl->name);

		intc = kzalloc(sizeof(struct s3c_irq_intc), GFP_KERNEL);
		if (!intc)
			return -ENOMEM;

		intc->domain = domain;
		intc->irqs = kcalloc(32, sizeof(struct s3c_irq_data),
				     GFP_KERNEL);
		if (!intc->irqs) {
			kfree(intc);
			return -ENOMEM;
		}

		if (ctrl->parent) {
			intc->reg_pending = reg_base + ctrl->offset;
			intc->reg_mask = reg_base + ctrl->offset + 0x4;

			if (*(ctrl->parent)) {
				intc->parent = *(ctrl->parent);
			} else {
				pr_warn("irq: parent of %s missing\n",
					ctrl->name);
				kfree(intc->irqs);
				kfree(intc);
				continue;
			}
		} else {
			intc->reg_pending = reg_base + ctrl->offset;
			intc->reg_mask = reg_base + ctrl->offset + 0x08;
			intc->reg_intpnd = reg_base + ctrl->offset + 0x10;
		}

		s3c24xx_clear_intc(intc);
		s3c_intc[i] = intc;
	}

	set_handle_irq(s3c24xx_handle_irq);

	return 0;
}

static struct s3c24xx_irq_of_ctrl s3c2410_ctrl[] = {
	{
		.name = "intc",
		.offset = 0,
	}, {
		.name = "subintc",
		.offset = 0x18,
		.parent = &s3c_intc[0],
	}
};

static int __init s3c2410_init_intc_of(struct device_node *np,
			struct device_node *interrupt_parent)
{
	return s3c_init_intc_of(np, interrupt_parent,
				s3c2410_ctrl, ARRAY_SIZE(s3c2410_ctrl));
}
IRQCHIP_DECLARE(s3c2410_irq, "samsung,s3c2410-irq", s3c2410_init_intc_of);

static struct s3c24xx_irq_of_ctrl s3c2416_ctrl[] = {
	{
		.name = "intc",
		.offset = 0,
	}, {
		.name = "subintc",
		.offset = 0x18,
		.parent = &s3c_intc[0],
	}, {
		.name = "intc2",
		.offset = 0x40,
	}
};

static int __init s3c2416_init_intc_of(struct device_node *np,
			struct device_node *interrupt_parent)
{
	return s3c_init_intc_of(np, interrupt_parent,
				s3c2416_ctrl, ARRAY_SIZE(s3c2416_ctrl));
}
IRQCHIP_DECLARE(s3c2416_irq, "samsung,s3c2416-irq", s3c2416_init_intc_of);
#endif
