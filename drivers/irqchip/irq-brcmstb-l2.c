/*
 * Generic Broadcom Set Top Box Level 2 Interrupt controller driver
 *
 * Copyright (C) 2014 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME	": " fmt

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/irqdomain.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>

#include <asm/mach/irq.h>

#include "irqchip.h"

/* Register offsets in the L2 interrupt controller */
#define CPU_STATUS	0x00
#define CPU_SET		0x04
#define CPU_CLEAR	0x08
#define CPU_MASK_STATUS	0x0c
#define CPU_MASK_SET	0x10
#define CPU_MASK_CLEAR	0x14

/* L2 intc private data structure */
struct brcmstb_l2_intc_data {
	int parent_irq;
	void __iomem *base;
	struct irq_domain *domain;
	bool can_wake;
	u32 saved_mask; /* for suspend/resume */
};

static void brcmstb_l2_intc_irq_handle(unsigned int irq, struct irq_desc *desc)
{
	struct brcmstb_l2_intc_data *b = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	u32 status;

	chained_irq_enter(chip, desc);

	status = __raw_readl(b->base + CPU_STATUS) &
		~(__raw_readl(b->base + CPU_MASK_STATUS));

	if (status == 0) {
		do_bad_IRQ(irq, desc);
		goto out;
	}

	do {
		irq = ffs(status) - 1;
		/* ack at our level */
		__raw_writel(1 << irq, b->base + CPU_CLEAR);
		status &= ~(1 << irq);
		generic_handle_irq(irq_find_mapping(b->domain, irq));
	} while (status);
out:
	chained_irq_exit(chip, desc);
}

static void brcmstb_l2_intc_suspend(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct brcmstb_l2_intc_data *b = gc->private;

	irq_gc_lock(gc);
	/* Save the current mask */
	b->saved_mask = __raw_readl(b->base + CPU_MASK_STATUS);

	if (b->can_wake) {
		/* Program the wakeup mask */
		__raw_writel(~gc->wake_active, b->base + CPU_MASK_SET);
		__raw_writel(gc->wake_active, b->base + CPU_MASK_CLEAR);
	}
	irq_gc_unlock(gc);
}

static void brcmstb_l2_intc_resume(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct brcmstb_l2_intc_data *b = gc->private;

	irq_gc_lock(gc);
	/* Clear unmasked non-wakeup interrupts */
	__raw_writel(~b->saved_mask & ~gc->wake_active, b->base + CPU_CLEAR);

	/* Restore the saved mask */
	__raw_writel(b->saved_mask, b->base + CPU_MASK_SET);
	__raw_writel(~b->saved_mask, b->base + CPU_MASK_CLEAR);
	irq_gc_unlock(gc);
}

int __init brcmstb_l2_intc_of_init(struct device_node *np,
					struct device_node *parent)
{
	unsigned int clr = IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN;
	struct brcmstb_l2_intc_data *data;
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	int ret;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->base = of_iomap(np, 0);
	if (!data->base) {
		pr_err("failed to remap intc L2 registers\n");
		ret = -ENOMEM;
		goto out_free;
	}

	/* Disable all interrupts by default */
	__raw_writel(0xffffffff, data->base + CPU_MASK_SET);
	__raw_writel(0xffffffff, data->base + CPU_CLEAR);

	data->parent_irq = irq_of_parse_and_map(np, 0);
	if (data->parent_irq < 0) {
		pr_err("failed to find parent interrupt\n");
		ret = data->parent_irq;
		goto out_unmap;
	}

	data->domain = irq_domain_add_linear(np, 32,
				&irq_generic_chip_ops, NULL);
	if (!data->domain) {
		ret = -ENOMEM;
		goto out_unmap;
	}

	/* Allocate a single Generic IRQ chip for this node */
	ret = irq_alloc_domain_generic_chips(data->domain, 32, 1,
				np->full_name, handle_edge_irq, clr, 0, 0);
	if (ret) {
		pr_err("failed to allocate generic irq chip\n");
		goto out_free_domain;
	}

	/* Set the IRQ chaining logic */
	irq_set_handler_data(data->parent_irq, data);
	irq_set_chained_handler(data->parent_irq, brcmstb_l2_intc_irq_handle);

	gc = irq_get_domain_generic_chip(data->domain, 0);
	gc->reg_base = data->base;
	gc->private = data;
	ct = gc->chip_types;

	ct->chip.irq_ack = irq_gc_ack_set_bit;
	ct->regs.ack = CPU_CLEAR;

	ct->chip.irq_mask = irq_gc_mask_disable_reg;
	ct->regs.disable = CPU_MASK_SET;

	ct->chip.irq_unmask = irq_gc_unmask_enable_reg;
	ct->regs.enable = CPU_MASK_CLEAR;

	ct->chip.irq_suspend = brcmstb_l2_intc_suspend;
	ct->chip.irq_resume = brcmstb_l2_intc_resume;

	if (of_property_read_bool(np, "brcm,irq-can-wake")) {
		data->can_wake = true;
		/* This IRQ chip can wake the system, set all child interrupts
		 * in wake_enabled mask
		 */
		gc->wake_enabled = 0xffffffff;
		ct->chip.irq_set_wake = irq_gc_set_wake;
	}

	pr_info("registered L2 intc (mem: 0x%p, parent irq: %d)\n",
			data->base, data->parent_irq);

	return 0;

out_free_domain:
	irq_domain_remove(data->domain);
out_unmap:
	iounmap(data->base);
out_free:
	kfree(data);
	return ret;
}
IRQCHIP_DECLARE(brcmstb_l2_intc, "brcm,l2-intc", brcmstb_l2_intc_of_init);
