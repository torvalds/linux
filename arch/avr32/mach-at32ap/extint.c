/*
 * External interrupt handling for AT32AP CPUs
 *
 * Copyright (C) 2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/random.h>

#include <asm/io.h>

/* EIC register offsets */
#define EIC_IER					0x0000
#define EIC_IDR					0x0004
#define EIC_IMR					0x0008
#define EIC_ISR					0x000c
#define EIC_ICR					0x0010
#define EIC_MODE				0x0014
#define EIC_EDGE				0x0018
#define EIC_LEVEL				0x001c
#define EIC_TEST				0x0020
#define EIC_NMIC				0x0024

/* Bitfields in TEST */
#define EIC_TESTEN_OFFSET			31
#define EIC_TESTEN_SIZE				1

/* Bitfields in NMIC */
#define EIC_EN_OFFSET				0
#define EIC_EN_SIZE				1

/* Bit manipulation macros */
#define EIC_BIT(name)					\
	(1 << EIC_##name##_OFFSET)
#define EIC_BF(name,value)				\
	(((value) & ((1 << EIC_##name##_SIZE) - 1))	\
	 << EIC_##name##_OFFSET)
#define EIC_BFEXT(name,value)				\
	(((value) >> EIC_##name##_OFFSET)		\
	 & ((1 << EIC_##name##_SIZE) - 1))
#define EIC_BFINS(name,value,old)			\
	(((old) & ~(((1 << EIC_##name##_SIZE) - 1)	\
		    << EIC_##name##_OFFSET))		\
	 | EIC_BF(name,value))

/* Register access macros */
#define eic_readl(port,reg)				\
	__raw_readl((port)->regs + EIC_##reg)
#define eic_writel(port,reg,value)			\
	__raw_writel((value), (port)->regs + EIC_##reg)

struct eic {
	void __iomem *regs;
	struct irq_chip *chip;
	unsigned int first_irq;
};

static void eic_ack_irq(unsigned int irq)
{
	struct eic *eic = get_irq_chip_data(irq);
	eic_writel(eic, ICR, 1 << (irq - eic->first_irq));
}

static void eic_mask_irq(unsigned int irq)
{
	struct eic *eic = get_irq_chip_data(irq);
	eic_writel(eic, IDR, 1 << (irq - eic->first_irq));
}

static void eic_mask_ack_irq(unsigned int irq)
{
	struct eic *eic = get_irq_chip_data(irq);
	eic_writel(eic, ICR, 1 << (irq - eic->first_irq));
	eic_writel(eic, IDR, 1 << (irq - eic->first_irq));
}

static void eic_unmask_irq(unsigned int irq)
{
	struct eic *eic = get_irq_chip_data(irq);
	eic_writel(eic, IER, 1 << (irq - eic->first_irq));
}

static int eic_set_irq_type(unsigned int irq, unsigned int flow_type)
{
	struct eic *eic = get_irq_chip_data(irq);
	struct irq_desc *desc;
	unsigned int i = irq - eic->first_irq;
	u32 mode, edge, level;
	int ret = 0;

	flow_type &= IRQ_TYPE_SENSE_MASK;
	if (flow_type == IRQ_TYPE_NONE)
		flow_type = IRQ_TYPE_LEVEL_LOW;

	desc = &irq_desc[irq];

	mode = eic_readl(eic, MODE);
	edge = eic_readl(eic, EDGE);
	level = eic_readl(eic, LEVEL);

	switch (flow_type) {
	case IRQ_TYPE_LEVEL_LOW:
		mode |= 1 << i;
		level &= ~(1 << i);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		mode |= 1 << i;
		level |= 1 << i;
		break;
	case IRQ_TYPE_EDGE_RISING:
		mode &= ~(1 << i);
		edge |= 1 << i;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		mode &= ~(1 << i);
		edge &= ~(1 << i);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret == 0) {
		eic_writel(eic, MODE, mode);
		eic_writel(eic, EDGE, edge);
		eic_writel(eic, LEVEL, level);

		if (flow_type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH))
			flow_type |= IRQ_LEVEL;
		desc->status &= ~(IRQ_TYPE_SENSE_MASK | IRQ_LEVEL);
		desc->status |= flow_type;
	}

	return ret;
}

static struct irq_chip eic_chip = {
	.name		= "eic",
	.ack		= eic_ack_irq,
	.mask		= eic_mask_irq,
	.mask_ack	= eic_mask_ack_irq,
	.unmask		= eic_unmask_irq,
	.set_type	= eic_set_irq_type,
};

static void demux_eic_irq(unsigned int irq, struct irq_desc *desc)
{
	struct eic *eic = desc->handler_data;
	struct irq_desc *ext_desc;
	unsigned long status, pending;
	unsigned int i, ext_irq;

	status = eic_readl(eic, ISR);
	pending = status & eic_readl(eic, IMR);

	while (pending) {
		i = fls(pending) - 1;
		pending &= ~(1 << i);

		ext_irq = i + eic->first_irq;
		ext_desc = irq_desc + ext_irq;
		if (ext_desc->status & IRQ_LEVEL)
			handle_level_irq(ext_irq, ext_desc);
		else
			handle_edge_irq(ext_irq, ext_desc);
	}
}

static int __init eic_probe(struct platform_device *pdev)
{
	struct eic *eic;
	struct resource *regs;
	unsigned int i;
	unsigned int nr_irqs;
	unsigned int int_irq;
	int ret;
	u32 pattern;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	int_irq = platform_get_irq(pdev, 0);
	if (!regs || !int_irq) {
		dev_dbg(&pdev->dev, "missing regs and/or irq resource\n");
		return -ENXIO;
	}

	ret = -ENOMEM;
	eic = kzalloc(sizeof(struct eic), GFP_KERNEL);
	if (!eic) {
		dev_dbg(&pdev->dev, "no memory for eic structure\n");
		goto err_kzalloc;
	}

	eic->first_irq = EIM_IRQ_BASE + 32 * pdev->id;
	eic->regs = ioremap(regs->start, regs->end - regs->start + 1);
	if (!eic->regs) {
		dev_dbg(&pdev->dev, "failed to map regs\n");
		goto err_ioremap;
	}

	/*
	 * Find out how many interrupt lines that are actually
	 * implemented in hardware.
	 */
	eic_writel(eic, IDR, ~0UL);
	eic_writel(eic, MODE, ~0UL);
	pattern = eic_readl(eic, MODE);
	nr_irqs = fls(pattern);

	/* Trigger on falling edge unless overridden by driver */
	eic_writel(eic, MODE, 0UL);
	eic_writel(eic, EDGE, 0UL);

	eic->chip = &eic_chip;

	for (i = 0; i < nr_irqs; i++) {
		/* NOTE the handler we set here is ignored by the demux */
		set_irq_chip_and_handler(eic->first_irq + i, &eic_chip,
					 handle_level_irq);
		set_irq_chip_data(eic->first_irq + i, eic);
	}

	set_irq_chained_handler(int_irq, demux_eic_irq);
	set_irq_data(int_irq, eic);

	dev_info(&pdev->dev,
		 "External Interrupt Controller at 0x%p, IRQ %u\n",
		 eic->regs, int_irq);
	dev_info(&pdev->dev,
		 "Handling %u external IRQs, starting with IRQ %u\n",
		 nr_irqs, eic->first_irq);

	return 0;

err_ioremap:
	kfree(eic);
err_kzalloc:
	return ret;
}

static struct platform_driver eic_driver = {
	.driver = {
		.name = "at32_eic",
	},
};

static int __init eic_init(void)
{
	return platform_driver_probe(&eic_driver, eic_probe);
}
arch_initcall(eic_init);
