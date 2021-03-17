// SPDX-License-Identifier: GPL-2.0-only
/*
 * Broadcom BCM7120 style Level 2 interrupt controller driver
 *
 * Copyright (C) 2014 Broadcom Corporation
 */

#define pr_fmt(fmt)	KBUILD_MODNAME	": " fmt

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/irqdomain.h>
#include <linux/reboot.h>
#include <linux/bitops.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>

/* Register offset in the L2 interrupt controller */
#define IRQEN		0x00
#define IRQSTAT		0x04

#define MAX_WORDS	4
#define MAX_MAPPINGS	(MAX_WORDS * 2)
#define IRQS_PER_WORD	32

struct bcm7120_l1_intc_data {
	struct bcm7120_l2_intc_data *b;
	u32 irq_map_mask[MAX_WORDS];
};

struct bcm7120_l2_intc_data {
	unsigned int n_words;
	void __iomem *map_base[MAX_MAPPINGS];
	void __iomem *pair_base[MAX_WORDS];
	int en_offset[MAX_WORDS];
	int stat_offset[MAX_WORDS];
	struct irq_domain *domain;
	bool can_wake;
	u32 irq_fwd_mask[MAX_WORDS];
	struct bcm7120_l1_intc_data *l1_data;
	int num_parent_irqs;
	const __be32 *map_mask_prop;
};

static void bcm7120_l2_intc_irq_handle(struct irq_desc *desc)
{
	struct bcm7120_l1_intc_data *data = irq_desc_get_handler_data(desc);
	struct bcm7120_l2_intc_data *b = data->b;
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int idx;

	chained_irq_enter(chip, desc);

	for (idx = 0; idx < b->n_words; idx++) {
		int base = idx * IRQS_PER_WORD;
		struct irq_chip_generic *gc =
			irq_get_domain_generic_chip(b->domain, base);
		unsigned long pending;
		int hwirq;

		irq_gc_lock(gc);
		pending = irq_reg_readl(gc, b->stat_offset[idx]) &
					    gc->mask_cache &
					    data->irq_map_mask[idx];
		irq_gc_unlock(gc);

		for_each_set_bit(hwirq, &pending, IRQS_PER_WORD) {
			generic_handle_irq(irq_find_mapping(b->domain,
					   base + hwirq));
		}
	}

	chained_irq_exit(chip, desc);
}

static void bcm7120_l2_intc_suspend(struct irq_chip_generic *gc)
{
	struct bcm7120_l2_intc_data *b = gc->private;
	struct irq_chip_type *ct = gc->chip_types;

	irq_gc_lock(gc);
	if (b->can_wake)
		irq_reg_writel(gc, gc->mask_cache | gc->wake_active,
			       ct->regs.mask);
	irq_gc_unlock(gc);
}

static void bcm7120_l2_intc_resume(struct irq_chip_generic *gc)
{
	struct irq_chip_type *ct = gc->chip_types;

	/* Restore the saved mask */
	irq_gc_lock(gc);
	irq_reg_writel(gc, gc->mask_cache, ct->regs.mask);
	irq_gc_unlock(gc);
}

static int bcm7120_l2_intc_init_one(struct device_node *dn,
					struct bcm7120_l2_intc_data *data,
					int irq, u32 *valid_mask)
{
	struct bcm7120_l1_intc_data *l1_data = &data->l1_data[irq];
	int parent_irq;
	unsigned int idx;

	parent_irq = irq_of_parse_and_map(dn, irq);
	if (!parent_irq) {
		pr_err("failed to map interrupt %d\n", irq);
		return -EINVAL;
	}

	/* For multiple parent IRQs with multiple words, this looks like:
	 * <irq0_w0 irq0_w1 irq1_w0 irq1_w1 ...>
	 *
	 * We need to associate a given parent interrupt with its corresponding
	 * map_mask in order to mask the status register with it because we
	 * have the same handler being called for multiple parent interrupts.
	 *
	 * This is typically something needed on BCM7xxx (STB chips).
	 */
	for (idx = 0; idx < data->n_words; idx++) {
		if (data->map_mask_prop) {
			l1_data->irq_map_mask[idx] |=
				be32_to_cpup(data->map_mask_prop +
					     irq * data->n_words + idx);
		} else {
			l1_data->irq_map_mask[idx] = 0xffffffff;
		}
		valid_mask[idx] |= l1_data->irq_map_mask[idx];
	}

	l1_data->b = data;

	irq_set_chained_handler_and_data(parent_irq,
					 bcm7120_l2_intc_irq_handle, l1_data);
	if (data->can_wake)
		enable_irq_wake(parent_irq);

	return 0;
}

static int __init bcm7120_l2_intc_iomap_7120(struct device_node *dn,
					     struct bcm7120_l2_intc_data *data)
{
	int ret;

	data->map_base[0] = of_iomap(dn, 0);
	if (!data->map_base[0]) {
		pr_err("unable to map registers\n");
		return -ENOMEM;
	}

	data->pair_base[0] = data->map_base[0];
	data->en_offset[0] = IRQEN;
	data->stat_offset[0] = IRQSTAT;
	data->n_words = 1;

	ret = of_property_read_u32_array(dn, "brcm,int-fwd-mask",
					 data->irq_fwd_mask, data->n_words);
	if (ret != 0 && ret != -EINVAL) {
		/* property exists but has the wrong number of words */
		pr_err("invalid brcm,int-fwd-mask property\n");
		return -EINVAL;
	}

	data->map_mask_prop = of_get_property(dn, "brcm,int-map-mask", &ret);
	if (!data->map_mask_prop ||
	    (ret != (sizeof(__be32) * data->num_parent_irqs * data->n_words))) {
		pr_err("invalid brcm,int-map-mask property\n");
		return -EINVAL;
	}

	return 0;
}

static int __init bcm7120_l2_intc_iomap_3380(struct device_node *dn,
					     struct bcm7120_l2_intc_data *data)
{
	unsigned int gc_idx;

	for (gc_idx = 0; gc_idx < MAX_WORDS; gc_idx++) {
		unsigned int map_idx = gc_idx * 2;
		void __iomem *en = of_iomap(dn, map_idx + 0);
		void __iomem *stat = of_iomap(dn, map_idx + 1);
		void __iomem *base = min(en, stat);

		data->map_base[map_idx + 0] = en;
		data->map_base[map_idx + 1] = stat;

		if (!base)
			break;

		data->pair_base[gc_idx] = base;
		data->en_offset[gc_idx] = en - base;
		data->stat_offset[gc_idx] = stat - base;
	}

	if (!gc_idx) {
		pr_err("unable to map registers\n");
		return -EINVAL;
	}

	data->n_words = gc_idx;
	return 0;
}

static int __init bcm7120_l2_intc_probe(struct device_node *dn,
				 struct device_node *parent,
				 int (*iomap_regs_fn)(struct device_node *,
					struct bcm7120_l2_intc_data *),
				 const char *intc_name)
{
	unsigned int clr = IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN;
	struct bcm7120_l2_intc_data *data;
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	int ret = 0;
	unsigned int idx, irq, flags;
	u32 valid_mask[MAX_WORDS] = { };

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->num_parent_irqs = of_irq_count(dn);
	if (data->num_parent_irqs <= 0) {
		pr_err("invalid number of parent interrupts\n");
		ret = -ENOMEM;
		goto out_unmap;
	}

	data->l1_data = kcalloc(data->num_parent_irqs, sizeof(*data->l1_data),
				GFP_KERNEL);
	if (!data->l1_data) {
		ret = -ENOMEM;
		goto out_free_l1_data;
	}

	ret = iomap_regs_fn(dn, data);
	if (ret < 0)
		goto out_free_l1_data;

	data->can_wake = of_property_read_bool(dn, "brcm,irq-can-wake");

	for (irq = 0; irq < data->num_parent_irqs; irq++) {
		ret = bcm7120_l2_intc_init_one(dn, data, irq, valid_mask);
		if (ret)
			goto out_free_l1_data;
	}

	data->domain = irq_domain_add_linear(dn, IRQS_PER_WORD * data->n_words,
					     &irq_generic_chip_ops, NULL);
	if (!data->domain) {
		ret = -ENOMEM;
		goto out_free_l1_data;
	}

	/* MIPS chips strapped for BE will automagically configure the
	 * peripheral registers for CPU-native byte order.
	 */
	flags = IRQ_GC_INIT_MASK_CACHE;
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		flags |= IRQ_GC_BE_IO;

	ret = irq_alloc_domain_generic_chips(data->domain, IRQS_PER_WORD, 1,
				dn->full_name, handle_level_irq, clr, 0, flags);
	if (ret) {
		pr_err("failed to allocate generic irq chip\n");
		goto out_free_domain;
	}

	for (idx = 0; idx < data->n_words; idx++) {
		irq = idx * IRQS_PER_WORD;
		gc = irq_get_domain_generic_chip(data->domain, irq);

		gc->unused = 0xffffffff & ~valid_mask[idx];
		gc->private = data;
		ct = gc->chip_types;

		gc->reg_base = data->pair_base[idx];
		ct->regs.mask = data->en_offset[idx];

		/* gc->reg_base is defined and so is gc->writel */
		irq_reg_writel(gc, data->irq_fwd_mask[idx],
			       data->en_offset[idx]);

		ct->chip.irq_mask = irq_gc_mask_clr_bit;
		ct->chip.irq_unmask = irq_gc_mask_set_bit;
		ct->chip.irq_ack = irq_gc_noop;
		gc->suspend = bcm7120_l2_intc_suspend;
		gc->resume = bcm7120_l2_intc_resume;

		/*
		 * Initialize mask-cache, in case we need it for
		 * saving/restoring fwd mask even w/o any child interrupts
		 * installed
		 */
		gc->mask_cache = irq_reg_readl(gc, ct->regs.mask);

		if (data->can_wake) {
			/* This IRQ chip can wake the system, set all
			 * relevant child interupts in wake_enabled mask
			 */
			gc->wake_enabled = 0xffffffff;
			gc->wake_enabled &= ~gc->unused;
			ct->chip.irq_set_wake = irq_gc_set_wake;
		}
	}

	pr_info("registered %s intc (%pOF, parent IRQ(s): %d)\n",
		intc_name, dn, data->num_parent_irqs);

	return 0;

out_free_domain:
	irq_domain_remove(data->domain);
out_free_l1_data:
	kfree(data->l1_data);
out_unmap:
	for (idx = 0; idx < MAX_MAPPINGS; idx++) {
		if (data->map_base[idx])
			iounmap(data->map_base[idx]);
	}
	kfree(data);
	return ret;
}

static int __init bcm7120_l2_intc_probe_7120(struct device_node *dn,
					     struct device_node *parent)
{
	return bcm7120_l2_intc_probe(dn, parent, bcm7120_l2_intc_iomap_7120,
				     "BCM7120 L2");
}

static int __init bcm7120_l2_intc_probe_3380(struct device_node *dn,
					     struct device_node *parent)
{
	return bcm7120_l2_intc_probe(dn, parent, bcm7120_l2_intc_iomap_3380,
				     "BCM3380 L2");
}

IRQCHIP_DECLARE(bcm7120_l2_intc, "brcm,bcm7120-l2-intc",
		bcm7120_l2_intc_probe_7120);

IRQCHIP_DECLARE(bcm3380_l2_intc, "brcm,bcm3380-l2-intc",
		bcm7120_l2_intc_probe_3380);
