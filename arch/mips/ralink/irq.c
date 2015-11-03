/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Copyright (C) 2009 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2013 John Crispin <blogic@openwrt.org>
 */

#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>

#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>

#include "common.h"

#define INTC_INT_GLOBAL		BIT(31)

#define RALINK_CPU_IRQ_INTC	(MIPS_CPU_IRQ_BASE + 2)
#define RALINK_CPU_IRQ_PCI	(MIPS_CPU_IRQ_BASE + 4)
#define RALINK_CPU_IRQ_FE	(MIPS_CPU_IRQ_BASE + 5)
#define RALINK_CPU_IRQ_WIFI	(MIPS_CPU_IRQ_BASE + 6)
#define RALINK_CPU_IRQ_COUNTER	(MIPS_CPU_IRQ_BASE + 7)

/* we have a cascade of 8 irqs */
#define RALINK_INTC_IRQ_BASE	8

/* we have 32 SoC irqs */
#define RALINK_INTC_IRQ_COUNT	32

#define RALINK_INTC_IRQ_PERFC   (RALINK_INTC_IRQ_BASE + 9)

enum rt_intc_regs_enum {
	INTC_REG_STATUS0 = 0,
	INTC_REG_STATUS1,
	INTC_REG_TYPE,
	INTC_REG_RAW_STATUS,
	INTC_REG_ENABLE,
	INTC_REG_DISABLE,
};

static u32 rt_intc_regs[] = {
	[INTC_REG_STATUS0] = 0x00,
	[INTC_REG_STATUS1] = 0x04,
	[INTC_REG_TYPE] = 0x20,
	[INTC_REG_RAW_STATUS] = 0x30,
	[INTC_REG_ENABLE] = 0x34,
	[INTC_REG_DISABLE] = 0x38,
};

static void __iomem *rt_intc_membase;

static int rt_perfcount_irq;

static inline void rt_intc_w32(u32 val, unsigned reg)
{
	__raw_writel(val, rt_intc_membase + rt_intc_regs[reg]);
}

static inline u32 rt_intc_r32(unsigned reg)
{
	return __raw_readl(rt_intc_membase + rt_intc_regs[reg]);
}

static void ralink_intc_irq_unmask(struct irq_data *d)
{
	rt_intc_w32(BIT(d->hwirq), INTC_REG_ENABLE);
}

static void ralink_intc_irq_mask(struct irq_data *d)
{
	rt_intc_w32(BIT(d->hwirq), INTC_REG_DISABLE);
}

static struct irq_chip ralink_intc_irq_chip = {
	.name		= "INTC",
	.irq_unmask	= ralink_intc_irq_unmask,
	.irq_mask	= ralink_intc_irq_mask,
	.irq_mask_ack	= ralink_intc_irq_mask,
};

int get_c0_perfcount_int(void)
{
	return rt_perfcount_irq;
}
EXPORT_SYMBOL_GPL(get_c0_perfcount_int);

unsigned int get_c0_compare_int(void)
{
	return CP0_LEGACY_COMPARE_IRQ;
}

static void ralink_intc_irq_handler(struct irq_desc *desc)
{
	u32 pending = rt_intc_r32(INTC_REG_STATUS0);

	if (pending) {
		struct irq_domain *domain = irq_desc_get_handler_data(desc);
		generic_handle_irq(irq_find_mapping(domain, __ffs(pending)));
	} else {
		spurious_interrupt();
	}
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned long pending;

	pending = read_c0_status() & read_c0_cause() & ST0_IM;

	if (pending & STATUSF_IP7)
		do_IRQ(RALINK_CPU_IRQ_COUNTER);

	else if (pending & STATUSF_IP5)
		do_IRQ(RALINK_CPU_IRQ_FE);

	else if (pending & STATUSF_IP6)
		do_IRQ(RALINK_CPU_IRQ_WIFI);

	else if (pending & STATUSF_IP4)
		do_IRQ(RALINK_CPU_IRQ_PCI);

	else if (pending & STATUSF_IP2)
		do_IRQ(RALINK_CPU_IRQ_INTC);

	else
		spurious_interrupt();
}

static int intc_map(struct irq_domain *d, unsigned int irq, irq_hw_number_t hw)
{
	irq_set_chip_and_handler(irq, &ralink_intc_irq_chip, handle_level_irq);

	return 0;
}

static const struct irq_domain_ops irq_domain_ops = {
	.xlate = irq_domain_xlate_onecell,
	.map = intc_map,
};

static int __init intc_of_init(struct device_node *node,
			       struct device_node *parent)
{
	struct resource res;
	struct irq_domain *domain;
	int irq;

	if (!of_property_read_u32_array(node, "ralink,intc-registers",
					rt_intc_regs, 6))
		pr_info("intc: using register map from devicetree\n");

	irq = irq_of_parse_and_map(node, 0);
	if (!irq)
		panic("Failed to get INTC IRQ");

	if (of_address_to_resource(node, 0, &res))
		panic("Failed to get intc memory range");

	if (request_mem_region(res.start, resource_size(&res),
				res.name) < 0)
		pr_err("Failed to request intc memory");

	rt_intc_membase = ioremap_nocache(res.start,
					resource_size(&res));
	if (!rt_intc_membase)
		panic("Failed to remap intc memory");

	/* disable all interrupts */
	rt_intc_w32(~0, INTC_REG_DISABLE);

	/* route all INTC interrupts to MIPS HW0 interrupt */
	rt_intc_w32(0, INTC_REG_TYPE);

	domain = irq_domain_add_legacy(node, RALINK_INTC_IRQ_COUNT,
			RALINK_INTC_IRQ_BASE, 0, &irq_domain_ops, NULL);
	if (!domain)
		panic("Failed to add irqdomain");

	rt_intc_w32(INTC_INT_GLOBAL, INTC_REG_ENABLE);

	irq_set_chained_handler_and_data(irq, ralink_intc_irq_handler, domain);

	/* tell the kernel which irq is used for performance monitoring */
	rt_perfcount_irq = irq_create_mapping(domain, 9);

	return 0;
}

static struct of_device_id __initdata of_irq_ids[] = {
	{ .compatible = "mti,cpu-interrupt-controller", .data = mips_cpu_irq_of_init },
	{ .compatible = "ralink,rt2880-intc", .data = intc_of_init },
	{},
};

void __init arch_init_irq(void)
{
	of_irq_init(of_irq_ids);
}

