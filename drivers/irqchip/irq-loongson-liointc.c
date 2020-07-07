// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2020, Jiaxun Yang <jiaxun.yang@flygoat.com>
 *  Loongson Local IO Interrupt Controller support
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/irqchip.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/smp.h>
#include <linux/irqchip/chained_irq.h>

#include <boot_param.h>

#define LIOINTC_CHIP_IRQ	32
#define LIOINTC_NUM_PARENT 4

#define LIOINTC_INTC_CHIP_START	0x20

#define LIOINTC_REG_INTC_STATUS	(LIOINTC_INTC_CHIP_START + 0x20)
#define LIOINTC_REG_INTC_EN_STATUS	(LIOINTC_INTC_CHIP_START + 0x04)
#define LIOINTC_REG_INTC_ENABLE	(LIOINTC_INTC_CHIP_START + 0x08)
#define LIOINTC_REG_INTC_DISABLE	(LIOINTC_INTC_CHIP_START + 0x0c)
#define LIOINTC_REG_INTC_POL	(LIOINTC_INTC_CHIP_START + 0x10)
#define LIOINTC_REG_INTC_EDGE	(LIOINTC_INTC_CHIP_START + 0x14)

#define LIOINTC_SHIFT_INTx	4

#define LIOINTC_ERRATA_IRQ	10

struct liointc_handler_data {
	struct liointc_priv	*priv;
	u32			parent_int_map;
};

struct liointc_priv {
	struct irq_chip_generic		*gc;
	struct liointc_handler_data	handler[LIOINTC_NUM_PARENT];
	u8				map_cache[LIOINTC_CHIP_IRQ];
	bool				has_lpc_irq_errata;
};

static void liointc_chained_handle_irq(struct irq_desc *desc)
{
	struct liointc_handler_data *handler = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct irq_chip_generic *gc = handler->priv->gc;
	u32 pending;

	chained_irq_enter(chip, desc);

	pending = readl(gc->reg_base + LIOINTC_REG_INTC_STATUS);

	if (!pending) {
		/* Always blame LPC IRQ if we have that bug */
		if (handler->priv->has_lpc_irq_errata &&
			(handler->parent_int_map & ~gc->mask_cache &
			BIT(LIOINTC_ERRATA_IRQ)))
			pending = BIT(LIOINTC_ERRATA_IRQ);
		else
			spurious_interrupt();
	}

	while (pending) {
		int bit = __ffs(pending);

		generic_handle_irq(irq_find_mapping(gc->domain, bit));
		pending &= ~BIT(bit);
	}

	chained_irq_exit(chip, desc);
}

static void liointc_set_bit(struct irq_chip_generic *gc,
				unsigned int offset,
				u32 mask, bool set)
{
	if (set)
		writel(readl(gc->reg_base + offset) | mask,
				gc->reg_base + offset);
	else
		writel(readl(gc->reg_base + offset) & ~mask,
				gc->reg_base + offset);
}

static int liointc_set_type(struct irq_data *data, unsigned int type)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);
	u32 mask = data->mask;
	unsigned long flags;

	irq_gc_lock_irqsave(gc, flags);
	switch (type) {
	case IRQ_TYPE_LEVEL_HIGH:
		liointc_set_bit(gc, LIOINTC_REG_INTC_EDGE, mask, false);
		liointc_set_bit(gc, LIOINTC_REG_INTC_POL, mask, true);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		liointc_set_bit(gc, LIOINTC_REG_INTC_EDGE, mask, false);
		liointc_set_bit(gc, LIOINTC_REG_INTC_POL, mask, false);
		break;
	case IRQ_TYPE_EDGE_RISING:
		liointc_set_bit(gc, LIOINTC_REG_INTC_EDGE, mask, true);
		liointc_set_bit(gc, LIOINTC_REG_INTC_POL, mask, true);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		liointc_set_bit(gc, LIOINTC_REG_INTC_EDGE, mask, true);
		liointc_set_bit(gc, LIOINTC_REG_INTC_POL, mask, false);
		break;
	default:
		irq_gc_unlock_irqrestore(gc, flags);
		return -EINVAL;
	}
	irq_gc_unlock_irqrestore(gc, flags);

	irqd_set_trigger_type(data, type);
	return 0;
}

static void liointc_resume(struct irq_chip_generic *gc)
{
	struct liointc_priv *priv = gc->private;
	unsigned long flags;
	int i;

	irq_gc_lock_irqsave(gc, flags);
	/* Disable all at first */
	writel(0xffffffff, gc->reg_base + LIOINTC_REG_INTC_DISABLE);
	/* Revert map cache */
	for (i = 0; i < LIOINTC_CHIP_IRQ; i++)
		writeb(priv->map_cache[i], gc->reg_base + i);
	/* Revert mask cache */
	writel(~gc->mask_cache, gc->reg_base + LIOINTC_REG_INTC_ENABLE);
	irq_gc_unlock_irqrestore(gc, flags);
}

static const char * const parent_names[] = {"int0", "int1", "int2", "int3"};

int __init liointc_of_init(struct device_node *node,
				struct device_node *parent)
{
	struct irq_chip_generic *gc;
	struct irq_domain *domain;
	struct irq_chip_type *ct;
	struct liointc_priv *priv;
	void __iomem *base;
	u32 of_parent_int_map[LIOINTC_NUM_PARENT];
	int parent_irq[LIOINTC_NUM_PARENT];
	bool have_parent = FALSE;
	int sz, i, err = 0;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	base = of_iomap(node, 0);
	if (!base) {
		err = -ENODEV;
		goto out_free_priv;
	}

	for (i = 0; i < LIOINTC_NUM_PARENT; i++) {
		parent_irq[i] = of_irq_get_byname(node, parent_names[i]);
		if (parent_irq[i] > 0)
			have_parent = TRUE;
	}
	if (!have_parent) {
		err = -ENODEV;
		goto out_iounmap;
	}

	sz = of_property_read_variable_u32_array(node,
						"loongson,parent_int_map",
						&of_parent_int_map[0],
						LIOINTC_NUM_PARENT,
						LIOINTC_NUM_PARENT);
	if (sz < 4) {
		pr_err("loongson-liointc: No parent_int_map\n");
		err = -ENODEV;
		goto out_iounmap;
	}

	for (i = 0; i < LIOINTC_NUM_PARENT; i++)
		priv->handler[i].parent_int_map = of_parent_int_map[i];

	/* Setup IRQ domain */
	domain = irq_domain_add_linear(node, 32,
					&irq_generic_chip_ops, priv);
	if (!domain) {
		pr_err("loongson-liointc: cannot add IRQ domain\n");
		err = -EINVAL;
		goto out_iounmap;
	}

	err = irq_alloc_domain_generic_chips(domain, 32, 1,
					node->full_name, handle_level_irq,
					IRQ_NOPROBE, 0, 0);
	if (err) {
		pr_err("loongson-liointc: unable to register IRQ domain\n");
		goto out_free_domain;
	}


	/* Disable all IRQs */
	writel(0xffffffff, base + LIOINTC_REG_INTC_DISABLE);
	/* Set to level triggered */
	writel(0x0, base + LIOINTC_REG_INTC_EDGE);

	/* Generate parent INT part of map cache */
	for (i = 0; i < LIOINTC_NUM_PARENT; i++) {
		u32 pending = priv->handler[i].parent_int_map;

		while (pending) {
			int bit = __ffs(pending);

			priv->map_cache[bit] = BIT(i) << LIOINTC_SHIFT_INTx;
			pending &= ~BIT(bit);
		}
	}

	for (i = 0; i < LIOINTC_CHIP_IRQ; i++) {
		/* Generate core part of map cache */
		priv->map_cache[i] |= BIT(loongson_sysconf.boot_cpu_id);
		writeb(priv->map_cache[i], base + i);
	}

	gc = irq_get_domain_generic_chip(domain, 0);
	gc->private = priv;
	gc->reg_base = base;
	gc->domain = domain;
	gc->resume = liointc_resume;

	ct = gc->chip_types;
	ct->regs.enable = LIOINTC_REG_INTC_ENABLE;
	ct->regs.disable = LIOINTC_REG_INTC_DISABLE;
	ct->chip.irq_unmask = irq_gc_unmask_enable_reg;
	ct->chip.irq_mask = irq_gc_mask_disable_reg;
	ct->chip.irq_mask_ack = irq_gc_mask_disable_reg;
	ct->chip.irq_set_type = liointc_set_type;

	gc->mask_cache = 0xffffffff;
	priv->gc = gc;

	for (i = 0; i < LIOINTC_NUM_PARENT; i++) {
		if (parent_irq[i] <= 0)
			continue;

		priv->handler[i].priv = priv;
		irq_set_chained_handler_and_data(parent_irq[i],
				liointc_chained_handle_irq, &priv->handler[i]);
	}

	return 0;

out_free_domain:
	irq_domain_remove(domain);
out_iounmap:
	iounmap(base);
out_free_priv:
	kfree(priv);

	return err;
}

IRQCHIP_DECLARE(loongson_liointc_1_0, "loongson,liointc-1.0", liointc_of_init);
IRQCHIP_DECLARE(loongson_liointc_1_0a, "loongson,liointc-1.0a", liointc_of_init);
