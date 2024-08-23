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

#ifdef CONFIG_MIPS
#include <loongson.h>
#else
#include <asm/loongson.h>
#endif

#include "irq-loongson.h"

#define LIOINTC_CHIP_IRQ	32
#define LIOINTC_NUM_PARENT	4
#define LIOINTC_NUM_CORES	4

#define LIOINTC_INTC_CHIP_START	0x20

#define LIOINTC_REG_INTC_STATUS(core)	(LIOINTC_INTC_CHIP_START + 0x20 + (core) * 8)
#define LIOINTC_REG_INTC_EN_STATUS	(LIOINTC_INTC_CHIP_START + 0x04)
#define LIOINTC_REG_INTC_ENABLE	(LIOINTC_INTC_CHIP_START + 0x08)
#define LIOINTC_REG_INTC_DISABLE	(LIOINTC_INTC_CHIP_START + 0x0c)
/*
 * LIOINTC_REG_INTC_POL register is only valid for Loongson-2K series, and
 * Loongson-3 series behave as noops.
 */
#define LIOINTC_REG_INTC_POL	(LIOINTC_INTC_CHIP_START + 0x10)
#define LIOINTC_REG_INTC_EDGE	(LIOINTC_INTC_CHIP_START + 0x14)

#define LIOINTC_SHIFT_INTx	4

#define LIOINTC_ERRATA_IRQ	10

#if defined(CONFIG_MIPS)
#define liointc_core_id get_ebase_cpunum()
#else
#define liointc_core_id get_csr_cpuid()
#endif

struct liointc_handler_data {
	struct liointc_priv	*priv;
	u32			parent_int_map;
};

struct liointc_priv {
	struct irq_chip_generic		*gc;
	struct liointc_handler_data	handler[LIOINTC_NUM_PARENT];
	void __iomem			*core_isr[LIOINTC_NUM_CORES];
	u8				map_cache[LIOINTC_CHIP_IRQ];
	u32				int_pol;
	u32				int_edge;
	bool				has_lpc_irq_errata;
};

struct fwnode_handle *liointc_handle;

static void liointc_chained_handle_irq(struct irq_desc *desc)
{
	struct liointc_handler_data *handler = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct irq_chip_generic *gc = handler->priv->gc;
	int core = liointc_core_id % LIOINTC_NUM_CORES;
	u32 pending;

	chained_irq_enter(chip, desc);

	pending = readl(handler->priv->core_isr[core]);

	if (!pending) {
		/* Always blame LPC IRQ if we have that bug */
		if (handler->priv->has_lpc_irq_errata &&
			(handler->parent_int_map & gc->mask_cache &
			BIT(LIOINTC_ERRATA_IRQ)))
			pending = BIT(LIOINTC_ERRATA_IRQ);
		else
			spurious_interrupt();
	}

	while (pending) {
		int bit = __ffs(pending);

		generic_handle_domain_irq(gc->domain, bit);
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
		liointc_set_bit(gc, LIOINTC_REG_INTC_POL, mask, false);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		liointc_set_bit(gc, LIOINTC_REG_INTC_EDGE, mask, false);
		liointc_set_bit(gc, LIOINTC_REG_INTC_POL, mask, true);
		break;
	case IRQ_TYPE_EDGE_RISING:
		liointc_set_bit(gc, LIOINTC_REG_INTC_EDGE, mask, true);
		liointc_set_bit(gc, LIOINTC_REG_INTC_POL, mask, false);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		liointc_set_bit(gc, LIOINTC_REG_INTC_EDGE, mask, true);
		liointc_set_bit(gc, LIOINTC_REG_INTC_POL, mask, true);
		break;
	default:
		irq_gc_unlock_irqrestore(gc, flags);
		return -EINVAL;
	}
	irq_gc_unlock_irqrestore(gc, flags);

	irqd_set_trigger_type(data, type);
	return 0;
}

static void liointc_suspend(struct irq_chip_generic *gc)
{
	struct liointc_priv *priv = gc->private;

	priv->int_pol = readl(gc->reg_base + LIOINTC_REG_INTC_POL);
	priv->int_edge = readl(gc->reg_base + LIOINTC_REG_INTC_EDGE);
}

static void liointc_resume(struct irq_chip_generic *gc)
{
	struct liointc_priv *priv = gc->private;
	unsigned long flags;
	int i;

	irq_gc_lock_irqsave(gc, flags);
	/* Disable all at first */
	writel(0xffffffff, gc->reg_base + LIOINTC_REG_INTC_DISABLE);
	/* Restore map cache */
	for (i = 0; i < LIOINTC_CHIP_IRQ; i++)
		writeb(priv->map_cache[i], gc->reg_base + i);
	writel(priv->int_pol, gc->reg_base + LIOINTC_REG_INTC_POL);
	writel(priv->int_edge, gc->reg_base + LIOINTC_REG_INTC_EDGE);
	/* Restore mask cache */
	writel(gc->mask_cache, gc->reg_base + LIOINTC_REG_INTC_ENABLE);
	irq_gc_unlock_irqrestore(gc, flags);
}

static int parent_irq[LIOINTC_NUM_PARENT];
static u32 parent_int_map[LIOINTC_NUM_PARENT];
static const char *const parent_names[] = {"int0", "int1", "int2", "int3"};
static const char *const core_reg_names[] = {"isr0", "isr1", "isr2", "isr3"};

static int liointc_domain_xlate(struct irq_domain *d, struct device_node *ctrlr,
			     const u32 *intspec, unsigned int intsize,
			     unsigned long *out_hwirq, unsigned int *out_type)
{
	if (WARN_ON(intsize < 1))
		return -EINVAL;
	*out_hwirq = intspec[0] - GSI_MIN_CPU_IRQ;

	if (intsize > 1)
		*out_type = intspec[1] & IRQ_TYPE_SENSE_MASK;
	else
		*out_type = IRQ_TYPE_NONE;

	return 0;
}

static const struct irq_domain_ops acpi_irq_gc_ops = {
	.map	= irq_map_generic_chip,
	.unmap  = irq_unmap_generic_chip,
	.xlate	= liointc_domain_xlate,
};

static int liointc_init(phys_addr_t addr, unsigned long size, int revision,
		struct fwnode_handle *domain_handle, struct device_node *node)
{
	int i, err;
	void __iomem *base;
	struct irq_chip_type *ct;
	struct irq_chip_generic *gc;
	struct irq_domain *domain;
	struct liointc_priv *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	base = ioremap(addr, size);
	if (!base)
		goto out_free_priv;

	for (i = 0; i < LIOINTC_NUM_CORES; i++)
		priv->core_isr[i] = base + LIOINTC_REG_INTC_STATUS(i);

	for (i = 0; i < LIOINTC_NUM_PARENT; i++)
		priv->handler[i].parent_int_map = parent_int_map[i];

	if (revision > 1) {
		for (i = 0; i < LIOINTC_NUM_CORES; i++) {
			int index = of_property_match_string(node,
					"reg-names", core_reg_names[i]);

			if (index < 0)
				continue;

			priv->core_isr[i] = of_iomap(node, index);
		}

		if (!priv->core_isr[0])
			goto out_iounmap;
	}

	/* Setup IRQ domain */
	if (!acpi_disabled)
		domain = irq_domain_create_linear(domain_handle, LIOINTC_CHIP_IRQ,
					&acpi_irq_gc_ops, priv);
	else
		domain = irq_domain_create_linear(domain_handle, LIOINTC_CHIP_IRQ,
					&irq_generic_chip_ops, priv);
	if (!domain) {
		pr_err("loongson-liointc: cannot add IRQ domain\n");
		goto out_iounmap;
	}

	err = irq_alloc_domain_generic_chips(domain, LIOINTC_CHIP_IRQ, 1,
					(node ? node->full_name : "LIOINTC"),
					handle_level_irq, 0, IRQ_NOPROBE, 0);
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
	gc->suspend = liointc_suspend;
	gc->resume = liointc_resume;

	ct = gc->chip_types;
	ct->regs.enable = LIOINTC_REG_INTC_ENABLE;
	ct->regs.disable = LIOINTC_REG_INTC_DISABLE;
	ct->chip.irq_unmask = irq_gc_unmask_enable_reg;
	ct->chip.irq_mask = irq_gc_mask_disable_reg;
	ct->chip.irq_mask_ack = irq_gc_mask_disable_reg;
	ct->chip.irq_set_type = liointc_set_type;
	ct->chip.flags = IRQCHIP_SKIP_SET_WAKE;

	gc->mask_cache = 0;
	priv->gc = gc;

	for (i = 0; i < LIOINTC_NUM_PARENT; i++) {
		if (parent_irq[i] <= 0)
			continue;

		priv->handler[i].priv = priv;
		irq_set_chained_handler_and_data(parent_irq[i],
				liointc_chained_handle_irq, &priv->handler[i]);
	}

	liointc_handle = domain_handle;
	return 0;

out_free_domain:
	irq_domain_remove(domain);
out_iounmap:
	iounmap(base);
out_free_priv:
	kfree(priv);

	return -EINVAL;
}

#ifdef CONFIG_OF

static int __init liointc_of_init(struct device_node *node,
				  struct device_node *parent)
{
	bool have_parent = FALSE;
	int sz, i, index, revision, err = 0;
	struct resource res;

	if (!of_device_is_compatible(node, "loongson,liointc-2.0")) {
		index = 0;
		revision = 1;
	} else {
		index = of_property_match_string(node, "reg-names", "main");
		revision = 2;
	}

	if (of_address_to_resource(node, index, &res))
		return -EINVAL;

	for (i = 0; i < LIOINTC_NUM_PARENT; i++) {
		parent_irq[i] = of_irq_get_byname(node, parent_names[i]);
		if (parent_irq[i] > 0)
			have_parent = TRUE;
	}
	if (!have_parent)
		return -ENODEV;

	sz = of_property_read_variable_u32_array(node,
						"loongson,parent_int_map",
						&parent_int_map[0],
						LIOINTC_NUM_PARENT,
						LIOINTC_NUM_PARENT);
	if (sz < 4) {
		pr_err("loongson-liointc: No parent_int_map\n");
		return -ENODEV;
	}

	err = liointc_init(res.start, resource_size(&res),
			revision, of_node_to_fwnode(node), node);
	if (err < 0)
		return err;

	return 0;
}

IRQCHIP_DECLARE(loongson_liointc_1_0, "loongson,liointc-1.0", liointc_of_init);
IRQCHIP_DECLARE(loongson_liointc_1_0a, "loongson,liointc-1.0a", liointc_of_init);
IRQCHIP_DECLARE(loongson_liointc_2_0, "loongson,liointc-2.0", liointc_of_init);

#endif

#ifdef CONFIG_ACPI
static int __init htintc_parse_madt(union acpi_subtable_headers *header,
					const unsigned long end)
{
	struct acpi_madt_ht_pic *htintc_entry = (struct acpi_madt_ht_pic *)header;
	struct irq_domain *parent = irq_find_matching_fwnode(liointc_handle, DOMAIN_BUS_ANY);

	return htvec_acpi_init(parent, htintc_entry);
}

static int __init acpi_cascade_irqdomain_init(void)
{
	int r;

	r = acpi_table_parse_madt(ACPI_MADT_TYPE_HT_PIC, htintc_parse_madt, 0);
	if (r < 0)
		return r;

	return 0;
}

int __init liointc_acpi_init(struct irq_domain *parent, struct acpi_madt_lio_pic *acpi_liointc)
{
	int ret;
	struct fwnode_handle *domain_handle;

	parent_int_map[0] = acpi_liointc->cascade_map[0];
	parent_int_map[1] = acpi_liointc->cascade_map[1];

	parent_irq[0] = irq_create_mapping(parent, acpi_liointc->cascade[0]);
	parent_irq[1] = irq_create_mapping(parent, acpi_liointc->cascade[1]);

	domain_handle = irq_domain_alloc_fwnode(&acpi_liointc->address);
	if (!domain_handle) {
		pr_err("Unable to allocate domain handle\n");
		return -ENOMEM;
	}

	ret = liointc_init(acpi_liointc->address, acpi_liointc->size,
			   1, domain_handle, NULL);
	if (ret == 0)
		ret = acpi_cascade_irqdomain_init();
	else
		irq_domain_free_fwnode(domain_handle);

	return ret;
}
#endif
