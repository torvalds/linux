/*
 * Broadcom BCM7120 style Level 2 interrupt controller driver
 *
 * Copyright (C) 2014 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME	": " fmt

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kconfig.h>
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
#include <linux/irqchip/chained_irq.h>

#include "irqchip.h"

/* Register offset in the L2 interrupt controller */
#define IRQEN		0x00
#define IRQSTAT		0x04

#define MAX_WORDS	4
#define IRQS_PER_WORD	32

struct bcm7120_l2_intc_data {
	unsigned int n_words;
	void __iomem *base[MAX_WORDS];
	struct irq_domain *domain;
	bool can_wake;
	u32 irq_fwd_mask[MAX_WORDS];
	u32 irq_map_mask[MAX_WORDS];
};

static void bcm7120_l2_intc_irq_handle(unsigned int irq, struct irq_desc *desc)
{
	struct bcm7120_l2_intc_data *b = irq_desc_get_handler_data(desc);
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
		pending = irq_reg_readl(gc, IRQSTAT) & gc->mask_cache;
		irq_gc_unlock(gc);

		for_each_set_bit(hwirq, &pending, IRQS_PER_WORD) {
			generic_handle_irq(irq_find_mapping(b->domain,
					   base + hwirq));
		}
	}

	chained_irq_exit(chip, desc);
}

static void bcm7120_l2_intc_suspend(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct bcm7120_l2_intc_data *b = gc->private;

	irq_gc_lock(gc);
	if (b->can_wake)
		irq_reg_writel(gc, gc->mask_cache | gc->wake_active, IRQEN);
	irq_gc_unlock(gc);
}

static void bcm7120_l2_intc_resume(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);

	/* Restore the saved mask */
	irq_gc_lock(gc);
	irq_reg_writel(gc, gc->mask_cache, IRQEN);
	irq_gc_unlock(gc);
}

static int bcm7120_l2_intc_init_one(struct device_node *dn,
					struct bcm7120_l2_intc_data *data,
					int irq, const __be32 *map_mask)
{
	int parent_irq;
	unsigned int idx;

	parent_irq = irq_of_parse_and_map(dn, irq);
	if (!parent_irq) {
		pr_err("failed to map interrupt %d\n", irq);
		return -EINVAL;
	}

	/* For multiple parent IRQs with multiple words, this looks like:
	 * <irq0_w0 irq0_w1 irq1_w0 irq1_w1 ...>
	 */
	for (idx = 0; idx < data->n_words; idx++)
		data->irq_map_mask[idx] |=
			be32_to_cpup(map_mask + irq * data->n_words + idx);

	irq_set_handler_data(parent_irq, data);
	irq_set_chained_handler(parent_irq, bcm7120_l2_intc_irq_handle);

	return 0;
}

int __init bcm7120_l2_intc_of_init(struct device_node *dn,
					struct device_node *parent)
{
	unsigned int clr = IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN;
	struct bcm7120_l2_intc_data *data;
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	const __be32 *map_mask;
	int num_parent_irqs;
	int ret = 0, len;
	unsigned int idx, irq, flags;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	for (idx = 0; idx < MAX_WORDS; idx++) {
		data->base[idx] = of_iomap(dn, idx);
		if (!data->base[idx])
			break;
		data->n_words = idx + 1;
	}
	if (!data->n_words) {
		pr_err("failed to remap intc L2 registers\n");
		ret = -ENOMEM;
		goto out_unmap;
	}

	/* Enable all interrupts specified in the interrupt forward mask;
	 * disable all others.  If the property doesn't exist (-EINVAL),
	 * assume all zeroes.
	 */
	ret = of_property_read_u32_array(dn, "brcm,int-fwd-mask",
					 data->irq_fwd_mask, data->n_words);
	if (ret == 0 || ret == -EINVAL) {
		for (idx = 0; idx < data->n_words; idx++)
			__raw_writel(data->irq_fwd_mask[idx],
				     data->base[idx] + IRQEN);
	} else {
		/* property exists but has the wrong number of words */
		pr_err("invalid int-fwd-mask property\n");
		ret = -EINVAL;
		goto out_unmap;
	}

	num_parent_irqs = of_irq_count(dn);
	if (num_parent_irqs <= 0) {
		pr_err("invalid number of parent interrupts\n");
		ret = -ENOMEM;
		goto out_unmap;
	}

	map_mask = of_get_property(dn, "brcm,int-map-mask", &len);
	if (!map_mask ||
	    (len != (sizeof(*map_mask) * num_parent_irqs * data->n_words))) {
		pr_err("invalid brcm,int-map-mask property\n");
		ret = -EINVAL;
		goto out_unmap;
	}

	for (irq = 0; irq < num_parent_irqs; irq++) {
		ret = bcm7120_l2_intc_init_one(dn, data, irq, map_mask);
		if (ret)
			goto out_unmap;
	}

	data->domain = irq_domain_add_linear(dn, IRQS_PER_WORD * data->n_words,
					     &irq_generic_chip_ops, NULL);
	if (!data->domain) {
		ret = -ENOMEM;
		goto out_unmap;
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

	if (of_property_read_bool(dn, "brcm,irq-can-wake"))
		data->can_wake = true;

	for (idx = 0; idx < data->n_words; idx++) {
		irq = idx * IRQS_PER_WORD;
		gc = irq_get_domain_generic_chip(data->domain, irq);

		gc->unused = 0xffffffff & ~data->irq_map_mask[idx];
		gc->reg_base = data->base[idx];
		gc->private = data;
		ct = gc->chip_types;

		ct->regs.mask = IRQEN;
		ct->chip.irq_mask = irq_gc_mask_clr_bit;
		ct->chip.irq_unmask = irq_gc_mask_set_bit;
		ct->chip.irq_ack = irq_gc_noop;
		ct->chip.irq_suspend = bcm7120_l2_intc_suspend;
		ct->chip.irq_resume = bcm7120_l2_intc_resume;

		if (data->can_wake) {
			/* This IRQ chip can wake the system, set all
			 * relevant child interupts in wake_enabled mask
			 */
			gc->wake_enabled = 0xffffffff;
			gc->wake_enabled &= ~gc->unused;
			ct->chip.irq_set_wake = irq_gc_set_wake;
		}
	}

	pr_info("registered BCM7120 L2 intc (mem: 0x%p, parent IRQ(s): %d)\n",
			data->base[0], num_parent_irqs);

	return 0;

out_free_domain:
	irq_domain_remove(data->domain);
out_unmap:
	for (idx = 0; idx < MAX_WORDS; idx++) {
		if (data->base[idx])
			iounmap(data->base[idx]);
	}
	kfree(data);
	return ret;
}
IRQCHIP_DECLARE(bcm7120_l2_intc, "brcm,bcm7120-l2-intc",
		bcm7120_l2_intc_of_init);
