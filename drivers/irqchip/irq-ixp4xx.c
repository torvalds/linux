// SPDX-License-Identifier: GPL-2.0
/*
 * irqchip for the IXP4xx interrupt controller
 * Copyright (C) 2019 Linus Walleij <linus.walleij@linaro.org>
 *
 * Based on arch/arm/mach-ixp4xx/common.c
 * Copyright 2002 (C) Intel Corporation
 * Copyright 2003-2004 (C) MontaVista, Software, Inc.
 * Copyright (C) Deepak Saxena <dsaxena@plexity.net>
 */
#include <linux/bitops.h>
#include <linux/gpio/driver.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqchip/irq-ixp4xx.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/cpu.h>

#include <asm/exception.h>
#include <asm/mach/irq.h>

#define IXP4XX_ICPR	0x00 /* Interrupt Status */
#define IXP4XX_ICMR	0x04 /* Interrupt Enable */
#define IXP4XX_ICLR	0x08 /* Interrupt IRQ/FIQ Select */
#define IXP4XX_ICIP	0x0C /* IRQ Status */
#define IXP4XX_ICFP	0x10 /* FIQ Status */
#define IXP4XX_ICHR	0x14 /* Interrupt Priority */
#define IXP4XX_ICIH	0x18 /* IRQ Highest Pri Int */
#define IXP4XX_ICFH	0x1C /* FIQ Highest Pri Int */

/* IXP43x and IXP46x-only */
#define	IXP4XX_ICPR2	0x20 /* Interrupt Status 2 */
#define	IXP4XX_ICMR2	0x24 /* Interrupt Enable 2 */
#define	IXP4XX_ICLR2	0x28 /* Interrupt IRQ/FIQ Select 2 */
#define IXP4XX_ICIP2	0x2C /* IRQ Status */
#define IXP4XX_ICFP2	0x30 /* FIQ Status */
#define IXP4XX_ICEEN	0x34 /* Error High Pri Enable */

/**
 * struct ixp4xx_irq - state container for the Faraday IRQ controller
 * @irqbase: IRQ controller memory base in virtual memory
 * @is_356: if this is an IXP43x, IXP45x or IX46x SoC (with 64 IRQs)
 * @irqchip: irqchip for this instance
 * @domain: IRQ domain for this instance
 */
struct ixp4xx_irq {
	void __iomem *irqbase;
	bool is_356;
	struct irq_chip irqchip;
	struct irq_domain *domain;
};

/* Local static state container */
static struct ixp4xx_irq ixirq;

/* GPIO Clocks */
#define IXP4XX_GPIO_CLK_0		14
#define IXP4XX_GPIO_CLK_1		15

static int ixp4xx_set_irq_type(struct irq_data *d, unsigned int type)
{
	/* All are level active high (asserted) here */
	if (type != IRQ_TYPE_LEVEL_HIGH)
		return -EINVAL;
	return 0;
}

static void ixp4xx_irq_mask(struct irq_data *d)
{
	struct ixp4xx_irq *ixi = irq_data_get_irq_chip_data(d);
	u32 val;

	if (ixi->is_356 && d->hwirq >= 32) {
		val = __raw_readl(ixi->irqbase + IXP4XX_ICMR2);
		val &= ~BIT(d->hwirq - 32);
		__raw_writel(val, ixi->irqbase + IXP4XX_ICMR2);
	} else {
		val = __raw_readl(ixi->irqbase + IXP4XX_ICMR);
		val &= ~BIT(d->hwirq);
		__raw_writel(val, ixi->irqbase + IXP4XX_ICMR);
	}
}

/*
 * Level triggered interrupts on GPIO lines can only be cleared when the
 * interrupt condition disappears.
 */
static void ixp4xx_irq_unmask(struct irq_data *d)
{
	struct ixp4xx_irq *ixi = irq_data_get_irq_chip_data(d);
	u32 val;

	if (ixi->is_356 && d->hwirq >= 32) {
		val = __raw_readl(ixi->irqbase + IXP4XX_ICMR2);
		val |= BIT(d->hwirq - 32);
		__raw_writel(val, ixi->irqbase + IXP4XX_ICMR2);
	} else {
		val = __raw_readl(ixi->irqbase + IXP4XX_ICMR);
		val |= BIT(d->hwirq);
		__raw_writel(val, ixi->irqbase + IXP4XX_ICMR);
	}
}

asmlinkage void __exception_irq_entry ixp4xx_handle_irq(struct pt_regs *regs)
{
	struct ixp4xx_irq *ixi = &ixirq;
	unsigned long status;
	int i;

	status = __raw_readl(ixi->irqbase + IXP4XX_ICIP);
	for_each_set_bit(i, &status, 32)
		handle_domain_irq(ixi->domain, i, regs);

	/*
	 * IXP465/IXP435 has an upper IRQ status register
	 */
	if (ixi->is_356) {
		status = __raw_readl(ixi->irqbase + IXP4XX_ICIP2);
		for_each_set_bit(i, &status, 32)
			handle_domain_irq(ixi->domain, i + 32, regs);
	}
}

static int ixp4xx_irq_domain_translate(struct irq_domain *domain,
				       struct irq_fwspec *fwspec,
				       unsigned long *hwirq,
				       unsigned int *type)
{
	/* We support standard DT translation */
	if (is_of_node(fwspec->fwnode) && fwspec->param_count == 2) {
		*hwirq = fwspec->param[0];
		*type = fwspec->param[1];
		return 0;
	}

	if (is_fwnode_irqchip(fwspec->fwnode)) {
		if (fwspec->param_count != 2)
			return -EINVAL;
		*hwirq = fwspec->param[0];
		*type = fwspec->param[1];
		WARN_ON(*type == IRQ_TYPE_NONE);
		return 0;
	}

	return -EINVAL;
}

static int ixp4xx_irq_domain_alloc(struct irq_domain *d,
				   unsigned int irq, unsigned int nr_irqs,
				   void *data)
{
	struct ixp4xx_irq *ixi = d->host_data;
	irq_hw_number_t hwirq;
	unsigned int type = IRQ_TYPE_NONE;
	struct irq_fwspec *fwspec = data;
	int ret;
	int i;

	ret = ixp4xx_irq_domain_translate(d, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	for (i = 0; i < nr_irqs; i++) {
		/*
		 * TODO: after converting IXP4xx to only device tree, set
		 * handle_bad_irq as default handler and assume all consumers
		 * call .set_type() as this is provided in the second cell in
		 * the device tree phandle.
		 */
		irq_domain_set_info(d,
				    irq + i,
				    hwirq + i,
				    &ixi->irqchip,
				    ixi,
				    handle_level_irq,
				    NULL, NULL);
		irq_set_probe(irq + i);
	}

	return 0;
}

/*
 * This needs to be a hierarchical irqdomain to work well with the
 * GPIO irqchip (which is lower in the hierarchy)
 */
static const struct irq_domain_ops ixp4xx_irqdomain_ops = {
	.translate = ixp4xx_irq_domain_translate,
	.alloc = ixp4xx_irq_domain_alloc,
	.free = irq_domain_free_irqs_common,
};

/**
 * ixp4xx_get_irq_domain() - retrieve the ixp4xx irq domain
 *
 * This function will go away when we transition to DT probing.
 */
struct irq_domain *ixp4xx_get_irq_domain(void)
{
	struct ixp4xx_irq *ixi = &ixirq;

	return ixi->domain;
}
EXPORT_SYMBOL_GPL(ixp4xx_get_irq_domain);

/*
 * This is the Linux IRQ to hwirq mapping table. This goes away when
 * we have DT support as all IRQ resources are defined in the device
 * tree. It will register all the IRQs that are not used by the hierarchical
 * GPIO IRQ chip. The "holes" inbetween these IRQs will be requested by
 * the GPIO driver using . This is a step-gap solution.
 */
struct ixp4xx_irq_chunk {
	int irq;
	int hwirq;
	int nr_irqs;
};

static const struct ixp4xx_irq_chunk ixp4xx_irq_chunks[] = {
	{
		.irq = 16,
		.hwirq = 0,
		.nr_irqs = 6,
	},
	{
		.irq = 24,
		.hwirq = 8,
		.nr_irqs = 11,
	},
	{
		.irq = 46,
		.hwirq = 30,
		.nr_irqs = 2,
	},
	/* Only on the 436 variants */
	{
		.irq = 48,
		.hwirq = 32,
		.nr_irqs = 10,
	},
};

/**
 * ixp4x_irq_setup() - Common setup code for the IXP4xx interrupt controller
 * @ixi: State container
 * @irqbase: Virtual memory base for the interrupt controller
 * @fwnode: Corresponding fwnode abstraction for this controller
 * @is_356: if this is an IXP43x, IXP45x or IXP46x SoC variant
 */
static int __init ixp4xx_irq_setup(struct ixp4xx_irq *ixi,
				   void __iomem *irqbase,
				   struct fwnode_handle *fwnode,
				   bool is_356)
{
	int nr_irqs;

	ixi->irqbase = irqbase;
	ixi->is_356 = is_356;

	/* Route all sources to IRQ instead of FIQ */
	__raw_writel(0x0, ixi->irqbase + IXP4XX_ICLR);

	/* Disable all interrupts */
	__raw_writel(0x0, ixi->irqbase + IXP4XX_ICMR);

	if (is_356) {
		/* Route upper 32 sources to IRQ instead of FIQ */
		__raw_writel(0x0, ixi->irqbase + IXP4XX_ICLR2);

		/* Disable upper 32 interrupts */
		__raw_writel(0x0, ixi->irqbase + IXP4XX_ICMR2);

		nr_irqs = 64;
	} else {
		nr_irqs = 32;
	}

	ixi->irqchip.name = "IXP4xx";
	ixi->irqchip.irq_mask = ixp4xx_irq_mask;
	ixi->irqchip.irq_unmask	= ixp4xx_irq_unmask;
	ixi->irqchip.irq_set_type = ixp4xx_set_irq_type;

	ixi->domain = irq_domain_create_linear(fwnode, nr_irqs,
					       &ixp4xx_irqdomain_ops,
					       ixi);
	if (!ixi->domain) {
		pr_crit("IXP4XX: can not add primary irqdomain\n");
		return -ENODEV;
	}

	set_handle_irq(ixp4xx_handle_irq);

	return 0;
}

/**
 * ixp4xx_irq_init() - Function to initialize the irqchip from boardfiles
 * @irqbase: physical base for the irq controller
 * @is_356: if this is an IXP43x, IXP45x or IXP46x SoC variant
 */
void __init ixp4xx_irq_init(resource_size_t irqbase,
			    bool is_356)
{
	struct ixp4xx_irq *ixi = &ixirq;
	void __iomem *base;
	struct fwnode_handle *fwnode;
	struct irq_fwspec fwspec;
	int nr_chunks;
	int ret;
	int i;

	base = ioremap(irqbase, 0x100);
	if (!base) {
		pr_crit("IXP4XX: could not ioremap interrupt controller\n");
		return;
	}
	fwnode = irq_domain_alloc_fwnode(&irqbase);
	if (!fwnode) {
		pr_crit("IXP4XX: no domain handle\n");
		return;
	}
	ret = ixp4xx_irq_setup(ixi, base, fwnode, is_356);
	if (ret) {
		pr_crit("IXP4XX: failed to set up irqchip\n");
		irq_domain_free_fwnode(fwnode);
	}

	nr_chunks = ARRAY_SIZE(ixp4xx_irq_chunks);
	if (!is_356)
		nr_chunks--;

	/*
	 * After adding OF support, this is no longer needed: irqs
	 * will be allocated for the respective fwnodes.
	 */
	for (i = 0; i < nr_chunks; i++) {
		const struct ixp4xx_irq_chunk *chunk = &ixp4xx_irq_chunks[i];

		pr_info("Allocate Linux IRQs %d..%d HW IRQs %d..%d\n",
			chunk->irq, chunk->irq + chunk->nr_irqs - 1,
			chunk->hwirq, chunk->hwirq + chunk->nr_irqs - 1);
		fwspec.fwnode = fwnode;
		fwspec.param[0] = chunk->hwirq;
		fwspec.param[1] = IRQ_TYPE_LEVEL_HIGH;
		fwspec.param_count = 2;
		ret = __irq_domain_alloc_irqs(ixi->domain,
					      chunk->irq,
					      chunk->nr_irqs,
					      NUMA_NO_NODE,
					      &fwspec,
					      false,
					      NULL);
		if (ret < 0) {
			pr_crit("IXP4XX: can not allocate irqs in hierarchy %d\n",
				ret);
			return;
		}
	}
}
EXPORT_SYMBOL_GPL(ixp4xx_irq_init);

#ifdef CONFIG_OF
int __init ixp4xx_of_init_irq(struct device_node *np,
			      struct device_node *parent)
{
	struct ixp4xx_irq *ixi = &ixirq;
	void __iomem *base;
	struct fwnode_handle *fwnode;
	bool is_356;
	int ret;

	base = of_iomap(np, 0);
	if (!base) {
		pr_crit("IXP4XX: could not ioremap interrupt controller\n");
		return -ENODEV;
	}
	fwnode = of_node_to_fwnode(np);

	/* These chip variants have 64 interrupts */
	is_356 = of_device_is_compatible(np, "intel,ixp43x-interrupt") ||
		of_device_is_compatible(np, "intel,ixp45x-interrupt") ||
		of_device_is_compatible(np, "intel,ixp46x-interrupt");

	ret = ixp4xx_irq_setup(ixi, base, fwnode, is_356);
	if (ret)
		pr_crit("IXP4XX: failed to set up irqchip\n");

	return ret;
}
IRQCHIP_DECLARE(ixp42x, "intel,ixp42x-interrupt",
		ixp4xx_of_init_irq);
IRQCHIP_DECLARE(ixp43x, "intel,ixp43x-interrupt",
		ixp4xx_of_init_irq);
IRQCHIP_DECLARE(ixp45x, "intel,ixp45x-interrupt",
		ixp4xx_of_init_irq);
IRQCHIP_DECLARE(ixp46x, "intel,ixp46x-interrupt",
		ixp4xx_of_init_irq);
#endif
