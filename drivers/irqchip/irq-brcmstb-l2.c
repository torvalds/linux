/*
 * Generic Broadcom Set Top Box Level 2 Interrupt controller driver
 *
 * Copyright (C) 2014-2017 Broadcom
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
#include <linux/spinlock.h>
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

struct brcmstb_intc_init_params {
	irq_flow_handler_t handler;
	int cpu_status;
	int cpu_clear;
	int cpu_mask_status;
	int cpu_mask_set;
	int cpu_mask_clear;
};

/* Register offsets in the L2 latched interrupt controller */
static const struct brcmstb_intc_init_params l2_edge_intc_init = {
	.handler		= handle_edge_irq,
	.cpu_status		= 0x00,
	.cpu_clear		= 0x08,
	.cpu_mask_status	= 0x0c,
	.cpu_mask_set		= 0x10,
	.cpu_mask_clear		= 0x14
};

/* Register offsets in the L2 level interrupt controller */
static const struct brcmstb_intc_init_params l2_lvl_intc_init = {
	.handler		= handle_level_irq,
	.cpu_status		= 0x00,
	.cpu_clear		= -1, /* Register not present */
	.cpu_mask_status	= 0x04,
	.cpu_mask_set		= 0x08,
	.cpu_mask_clear		= 0x0C
};

/* L2 intc private data structure */
struct brcmstb_l2_intc_data {
	struct irq_domain *domain;
	struct irq_chip_generic *gc;
	int status_offset;
	int mask_offset;
	bool can_wake;
	u32 saved_mask; /* for suspend/resume */
};

/**
 * brcmstb_l2_mask_and_ack - Mask and ack pending interrupt
 * @d: irq_data
 *
 * Chip has separate enable/disable registers instead of a single mask
 * register and pending interrupt is acknowledged by setting a bit.
 *
 * Note: This function is generic and could easily be added to the
 * generic irqchip implementation if there ever becomes a will to do so.
 * Perhaps with a name like irq_gc_mask_disable_and_ack_set().
 *
 * e.g.: https://patchwork.kernel.org/patch/9831047/
 */
static void brcmstb_l2_mask_and_ack(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	u32 mask = d->mask;

	irq_gc_lock(gc);
	irq_reg_writel(gc, mask, ct->regs.disable);
	*ct->mask_cache &= ~mask;
	irq_reg_writel(gc, mask, ct->regs.ack);
	irq_gc_unlock(gc);
}

static void brcmstb_l2_intc_irq_handle(struct irq_desc *desc)
{
	struct brcmstb_l2_intc_data *b = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int irq;
	u32 status;

	chained_irq_enter(chip, desc);

	status = irq_reg_readl(b->gc, b->status_offset) &
		~(irq_reg_readl(b->gc, b->mask_offset));

	if (status == 0) {
		raw_spin_lock(&desc->lock);
		handle_bad_irq(desc);
		raw_spin_unlock(&desc->lock);
		goto out;
	}

	do {
		irq = ffs(status) - 1;
		status &= ~(1 << irq);
		generic_handle_irq(irq_linear_revmap(b->domain, irq));
	} while (status);
out:
	chained_irq_exit(chip, desc);
}

static void brcmstb_l2_intc_suspend(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	struct brcmstb_l2_intc_data *b = gc->private;
	unsigned long flags;

	irq_gc_lock_irqsave(gc, flags);
	/* Save the current mask */
	b->saved_mask = irq_reg_readl(gc, ct->regs.mask);

	if (b->can_wake) {
		/* Program the wakeup mask */
		irq_reg_writel(gc, ~gc->wake_active, ct->regs.disable);
		irq_reg_writel(gc, gc->wake_active, ct->regs.enable);
	}
	irq_gc_unlock_irqrestore(gc, flags);
}

static void brcmstb_l2_intc_resume(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	struct brcmstb_l2_intc_data *b = gc->private;
	unsigned long flags;

	irq_gc_lock_irqsave(gc, flags);
	if (ct->chip.irq_ack) {
		/* Clear unmasked non-wakeup interrupts */
		irq_reg_writel(gc, ~b->saved_mask & ~gc->wake_active,
				ct->regs.ack);
	}

	/* Restore the saved mask */
	irq_reg_writel(gc, b->saved_mask, ct->regs.disable);
	irq_reg_writel(gc, ~b->saved_mask, ct->regs.enable);
	irq_gc_unlock_irqrestore(gc, flags);
}

static int __init brcmstb_l2_intc_of_init(struct device_node *np,
					  struct device_node *parent,
					  const struct brcmstb_intc_init_params
					  *init_params)
{
	unsigned int clr = IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN;
	struct brcmstb_l2_intc_data *data;
	struct irq_chip_type *ct;
	int ret;
	unsigned int flags;
	int parent_irq;
	void __iomem *base;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("failed to remap intc L2 registers\n");
		ret = -ENOMEM;
		goto out_free;
	}

	/* Disable all interrupts by default */
	writel(0xffffffff, base + init_params->cpu_mask_set);

	/* Wakeup interrupts may be retained from S5 (cold boot) */
	data->can_wake = of_property_read_bool(np, "brcm,irq-can-wake");
	if (!data->can_wake && (init_params->cpu_clear >= 0))
		writel(0xffffffff, base + init_params->cpu_clear);

	parent_irq = irq_of_parse_and_map(np, 0);
	if (!parent_irq) {
		pr_err("failed to find parent interrupt\n");
		ret = -EINVAL;
		goto out_unmap;
	}

	data->domain = irq_domain_add_linear(np, 32,
				&irq_generic_chip_ops, NULL);
	if (!data->domain) {
		ret = -ENOMEM;
		goto out_unmap;
	}

	/* MIPS chips strapped for BE will automagically configure the
	 * peripheral registers for CPU-native byte order.
	 */
	flags = 0;
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		flags |= IRQ_GC_BE_IO;

	/* Allocate a single Generic IRQ chip for this node */
	ret = irq_alloc_domain_generic_chips(data->domain, 32, 1,
			np->full_name, init_params->handler, clr, 0, flags);
	if (ret) {
		pr_err("failed to allocate generic irq chip\n");
		goto out_free_domain;
	}

	/* Set the IRQ chaining logic */
	irq_set_chained_handler_and_data(parent_irq,
					 brcmstb_l2_intc_irq_handle, data);

	data->gc = irq_get_domain_generic_chip(data->domain, 0);
	data->gc->reg_base = base;
	data->gc->private = data;
	data->status_offset = init_params->cpu_status;
	data->mask_offset = init_params->cpu_mask_status;

	ct = data->gc->chip_types;

	if (init_params->cpu_clear >= 0) {
		ct->regs.ack = init_params->cpu_clear;
		ct->chip.irq_ack = irq_gc_ack_set_bit;
		ct->chip.irq_mask_ack = brcmstb_l2_mask_and_ack;
	} else {
		/* No Ack - but still slightly more efficient to define this */
		ct->chip.irq_mask_ack = irq_gc_mask_disable_reg;
	}

	ct->chip.irq_mask = irq_gc_mask_disable_reg;
	ct->regs.disable = init_params->cpu_mask_set;
	ct->regs.mask = init_params->cpu_mask_status;

	ct->chip.irq_unmask = irq_gc_unmask_enable_reg;
	ct->regs.enable = init_params->cpu_mask_clear;

	ct->chip.irq_suspend = brcmstb_l2_intc_suspend;
	ct->chip.irq_resume = brcmstb_l2_intc_resume;
	ct->chip.irq_pm_shutdown = brcmstb_l2_intc_suspend;

	if (data->can_wake) {
		/* This IRQ chip can wake the system, set all child interrupts
		 * in wake_enabled mask
		 */
		data->gc->wake_enabled = 0xffffffff;
		ct->chip.irq_set_wake = irq_gc_set_wake;
	}

	return 0;

out_free_domain:
	irq_domain_remove(data->domain);
out_unmap:
	iounmap(base);
out_free:
	kfree(data);
	return ret;
}

int __init brcmstb_l2_edge_intc_of_init(struct device_node *np,
	struct device_node *parent)
{
	return brcmstb_l2_intc_of_init(np, parent, &l2_edge_intc_init);
}
IRQCHIP_DECLARE(brcmstb_l2_intc, "brcm,l2-intc", brcmstb_l2_edge_intc_of_init);

int __init brcmstb_l2_lvl_intc_of_init(struct device_node *np,
	struct device_node *parent)
{
	return brcmstb_l2_intc_of_init(np, parent, &l2_lvl_intc_init);
}
IRQCHIP_DECLARE(bcm7271_l2_intc, "brcm,bcm7271-l2-intc",
	brcmstb_l2_lvl_intc_of_init);
