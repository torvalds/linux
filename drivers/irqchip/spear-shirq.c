/*
 * SPEAr platform shared irq layer source file
 *
 * Copyright (C) 2009-2012 ST Microelectronics
 * Viresh Kumar <vireshk@kernel.org>
 *
 * Copyright (C) 2012 ST Microelectronics
 * Shiraz Hashim <shiraz.linux.kernel@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/err.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/spinlock.h>

/*
 * struct spear_shirq: shared irq structure
 *
 * base:	Base register address
 * status_reg:	Status register offset for chained interrupt handler
 * mask_reg:	Mask register offset for irq chip
 * mask:	Mask to apply to the status register
 * virq_base:	Base virtual interrupt number
 * nr_irqs:	Number of interrupts handled by this block
 * offset:	Bit offset of the first interrupt
 * irq_chip:	Interrupt controller chip used for this instance,
 *		if NULL group is disabled, but accounted
 */
struct spear_shirq {
	void __iomem		*base;
	u32			status_reg;
	u32			mask_reg;
	u32			mask;
	u32			virq_base;
	u32			nr_irqs;
	u32			offset;
	struct irq_chip		*irq_chip;
};

/* spear300 shared irq registers offsets and masks */
#define SPEAR300_INT_ENB_MASK_REG	0x54
#define SPEAR300_INT_STS_MASK_REG	0x58

static DEFINE_RAW_SPINLOCK(shirq_lock);

static void shirq_irq_mask(struct irq_data *d)
{
	struct spear_shirq *shirq = irq_data_get_irq_chip_data(d);
	u32 val, shift = d->irq - shirq->virq_base + shirq->offset;
	u32 __iomem *reg = shirq->base + shirq->mask_reg;

	raw_spin_lock(&shirq_lock);
	val = readl(reg) & ~(0x1 << shift);
	writel(val, reg);
	raw_spin_unlock(&shirq_lock);
}

static void shirq_irq_unmask(struct irq_data *d)
{
	struct spear_shirq *shirq = irq_data_get_irq_chip_data(d);
	u32 val, shift = d->irq - shirq->virq_base + shirq->offset;
	u32 __iomem *reg = shirq->base + shirq->mask_reg;

	raw_spin_lock(&shirq_lock);
	val = readl(reg) | (0x1 << shift);
	writel(val, reg);
	raw_spin_unlock(&shirq_lock);
}

static struct irq_chip shirq_chip = {
	.name		= "spear-shirq",
	.irq_mask	= shirq_irq_mask,
	.irq_unmask	= shirq_irq_unmask,
};

static struct spear_shirq spear300_shirq_ras1 = {
	.offset		= 0,
	.nr_irqs	= 9,
	.mask		= ((0x1 << 9) - 1) << 0,
	.irq_chip	= &shirq_chip,
	.status_reg	= SPEAR300_INT_STS_MASK_REG,
	.mask_reg	= SPEAR300_INT_ENB_MASK_REG,
};

static struct spear_shirq *spear300_shirq_blocks[] = {
	&spear300_shirq_ras1,
};

/* spear310 shared irq registers offsets and masks */
#define SPEAR310_INT_STS_MASK_REG	0x04

static struct spear_shirq spear310_shirq_ras1 = {
	.offset		= 0,
	.nr_irqs	= 8,
	.mask		= ((0x1 << 8) - 1) << 0,
	.irq_chip	= &dummy_irq_chip,
	.status_reg	= SPEAR310_INT_STS_MASK_REG,
};

static struct spear_shirq spear310_shirq_ras2 = {
	.offset		= 8,
	.nr_irqs	= 5,
	.mask		= ((0x1 << 5) - 1) << 8,
	.irq_chip	= &dummy_irq_chip,
	.status_reg	= SPEAR310_INT_STS_MASK_REG,
};

static struct spear_shirq spear310_shirq_ras3 = {
	.offset		= 13,
	.nr_irqs	= 1,
	.mask		= ((0x1 << 1) - 1) << 13,
	.irq_chip	= &dummy_irq_chip,
	.status_reg	= SPEAR310_INT_STS_MASK_REG,
};

static struct spear_shirq spear310_shirq_intrcomm_ras = {
	.offset		= 14,
	.nr_irqs	= 3,
	.mask		= ((0x1 << 3) - 1) << 14,
	.irq_chip	= &dummy_irq_chip,
	.status_reg	= SPEAR310_INT_STS_MASK_REG,
};

static struct spear_shirq *spear310_shirq_blocks[] = {
	&spear310_shirq_ras1,
	&spear310_shirq_ras2,
	&spear310_shirq_ras3,
	&spear310_shirq_intrcomm_ras,
};

/* spear320 shared irq registers offsets and masks */
#define SPEAR320_INT_STS_MASK_REG		0x04
#define SPEAR320_INT_CLR_MASK_REG		0x04
#define SPEAR320_INT_ENB_MASK_REG		0x08

static struct spear_shirq spear320_shirq_ras3 = {
	.offset		= 0,
	.nr_irqs	= 7,
	.mask		= ((0x1 << 7) - 1) << 0,
	.irq_chip	= &dummy_irq_chip,
	.status_reg	= SPEAR320_INT_STS_MASK_REG,
};

static struct spear_shirq spear320_shirq_ras1 = {
	.offset		= 7,
	.nr_irqs	= 3,
	.mask		= ((0x1 << 3) - 1) << 7,
	.irq_chip	= &dummy_irq_chip,
	.status_reg	= SPEAR320_INT_STS_MASK_REG,
};

static struct spear_shirq spear320_shirq_ras2 = {
	.offset		= 10,
	.nr_irqs	= 1,
	.mask		= ((0x1 << 1) - 1) << 10,
	.irq_chip	= &dummy_irq_chip,
	.status_reg	= SPEAR320_INT_STS_MASK_REG,
};

static struct spear_shirq spear320_shirq_intrcomm_ras = {
	.offset		= 11,
	.nr_irqs	= 11,
	.mask		= ((0x1 << 11) - 1) << 11,
	.irq_chip	= &dummy_irq_chip,
	.status_reg	= SPEAR320_INT_STS_MASK_REG,
};

static struct spear_shirq *spear320_shirq_blocks[] = {
	&spear320_shirq_ras3,
	&spear320_shirq_ras1,
	&spear320_shirq_ras2,
	&spear320_shirq_intrcomm_ras,
};

static void shirq_handler(struct irq_desc *desc)
{
	struct spear_shirq *shirq = irq_desc_get_handler_data(desc);
	u32 pend;

	pend = readl(shirq->base + shirq->status_reg) & shirq->mask;
	pend >>= shirq->offset;

	while (pend) {
		int irq = __ffs(pend);

		pend &= ~(0x1 << irq);
		generic_handle_irq(shirq->virq_base + irq);
	}
}

static void __init spear_shirq_register(struct spear_shirq *shirq,
					int parent_irq)
{
	int i;

	if (!shirq->irq_chip)
		return;

	irq_set_chained_handler_and_data(parent_irq, shirq_handler, shirq);

	for (i = 0; i < shirq->nr_irqs; i++) {
		irq_set_chip_and_handler(shirq->virq_base + i,
					 shirq->irq_chip, handle_simple_irq);
		irq_set_chip_data(shirq->virq_base + i, shirq);
	}
}

static int __init shirq_init(struct spear_shirq **shirq_blocks, int block_nr,
		struct device_node *np)
{
	int i, parent_irq, virq_base, hwirq = 0, nr_irqs = 0;
	struct irq_domain *shirq_domain;
	void __iomem *base;

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("%s: failed to map shirq registers\n", __func__);
		return -ENXIO;
	}

	for (i = 0; i < block_nr; i++)
		nr_irqs += shirq_blocks[i]->nr_irqs;

	virq_base = irq_alloc_descs(-1, 0, nr_irqs, 0);
	if (virq_base < 0) {
		pr_err("%s: irq desc alloc failed\n", __func__);
		goto err_unmap;
	}

	shirq_domain = irq_domain_create_legacy(of_fwnode_handle(np), nr_irqs, virq_base, 0,
			&irq_domain_simple_ops, NULL);
	if (WARN_ON(!shirq_domain)) {
		pr_warn("%s: irq domain init failed\n", __func__);
		goto err_free_desc;
	}

	for (i = 0; i < block_nr; i++) {
		shirq_blocks[i]->base = base;
		shirq_blocks[i]->virq_base = irq_find_mapping(shirq_domain,
				hwirq);

		parent_irq = irq_of_parse_and_map(np, i);
		spear_shirq_register(shirq_blocks[i], parent_irq);
		hwirq += shirq_blocks[i]->nr_irqs;
	}

	return 0;

err_free_desc:
	irq_free_descs(virq_base, nr_irqs);
err_unmap:
	iounmap(base);
	return -ENXIO;
}

static int __init spear300_shirq_of_init(struct device_node *np,
					 struct device_node *parent)
{
	return shirq_init(spear300_shirq_blocks,
			ARRAY_SIZE(spear300_shirq_blocks), np);
}
IRQCHIP_DECLARE(spear300_shirq, "st,spear300-shirq", spear300_shirq_of_init);

static int __init spear310_shirq_of_init(struct device_node *np,
					 struct device_node *parent)
{
	return shirq_init(spear310_shirq_blocks,
			ARRAY_SIZE(spear310_shirq_blocks), np);
}
IRQCHIP_DECLARE(spear310_shirq, "st,spear310-shirq", spear310_shirq_of_init);

static int __init spear320_shirq_of_init(struct device_node *np,
					 struct device_node *parent)
{
	return shirq_init(spear320_shirq_blocks,
			ARRAY_SIZE(spear320_shirq_blocks), np);
}
IRQCHIP_DECLARE(spear320_shirq, "st,spear320-shirq", spear320_shirq_of_init);
