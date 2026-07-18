// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Aspeed AST2700 Interrupt Controller.
 *
 *  Copyright (C) 2026 ASPEED Technology Inc.
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/spinlock.h>

#include "irq-ast2700.h"

#define INTC1_IER			0x100
#define INTC1_ISR			0x104
#define INTC1_BANK_SIZE		0x10
#define INTC1_SEL_BASE			0x80
#define INTC1_SEL_BANK_SIZE		0x4
#define INTC1_SEL_ROUTE_SIZE	0x20
#define INTC1_IRQS_PER_BANK		32
#define INTC1_BANK_NUM			6
#define INTC1_ROUTE_NUM			7
#define INTC1_IN_NUM			192
#define INTC1_BOOTMCU_ROUTE		6
#define INTC1_ROUTE_SELECTOR_BITS	3
#define INTC1_ROUTE_IRQS_PER_GROUP	32
#define INTC1_ROUTE_SHIFT		5

struct aspeed_intc1 {
	struct device				*dev;
	void __iomem				*base;
	raw_spinlock_t				intc_lock;
	struct irq_domain			*local;
	struct irq_domain			*upstream;
	struct aspeed_intc_interrupt_ranges	ranges;
};

static void aspeed_intc1_disable_int(struct aspeed_intc1 *intc1)
{
	for (int i = 0; i < INTC1_BANK_NUM; i++)
		writel(0, intc1->base + INTC1_IER + (INTC1_BANK_SIZE * i));
}

static void aspeed_intc1_irq_handler(struct irq_desc *desc)
{
	struct aspeed_intc1 *intc1 = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned long bit, status;

	chained_irq_enter(chip, desc);

	for (int bank = 0; bank < INTC1_BANK_NUM; bank++) {
		status = readl(intc1->base + INTC1_ISR + (INTC1_BANK_SIZE * bank));
		if (!status)
			continue;

		for_each_set_bit(bit, &status, INTC1_IRQS_PER_BANK) {
			generic_handle_domain_irq(intc1->local, (bank * INTC1_IRQS_PER_BANK) + bit);
			writel(BIT(bit), intc1->base + INTC1_ISR + (INTC1_BANK_SIZE * bank));
		}
	}

	chained_irq_exit(chip, desc);
}

static void aspeed_intc1_irq_mask(struct irq_data *data)
{
	struct aspeed_intc1 *intc1 = irq_data_get_irq_chip_data(data);
	int bank = data->hwirq / INTC1_IRQS_PER_BANK;
	int bit = data->hwirq % INTC1_IRQS_PER_BANK;
	u32 ier;

	guard(raw_spinlock)(&intc1->intc_lock);
	ier = readl(intc1->base + INTC1_IER + (INTC1_BANK_SIZE * bank)) & ~BIT(bit);
	writel(ier, intc1->base + INTC1_IER + (INTC1_BANK_SIZE * bank));
}

static void aspeed_intc1_irq_unmask(struct irq_data *data)
{
	struct aspeed_intc1 *intc1 = irq_data_get_irq_chip_data(data);
	int bank = data->hwirq / INTC1_IRQS_PER_BANK;
	int bit = data->hwirq % INTC1_IRQS_PER_BANK;
	u32 ier;

	guard(raw_spinlock)(&intc1->intc_lock);
	ier = readl(intc1->base + INTC1_IER + (INTC1_BANK_SIZE * bank)) | BIT(bit);
	writel(ier, intc1->base + INTC1_IER + (INTC1_BANK_SIZE * bank));
}

static struct irq_chip aspeed_intc_chip = {
	.name		= "ASPEED INTC1",
	.irq_mask	= aspeed_intc1_irq_mask,
	.irq_unmask	= aspeed_intc1_irq_unmask,
};

static int aspeed_intc1_irq_domain_translate(struct irq_domain *domain,
					     struct irq_fwspec *fwspec,
					     unsigned long *hwirq,
					     unsigned int *type)
{
	if (fwspec->param_count != 1)
		return -EINVAL;

	*hwirq = fwspec->param[0];
	*type = IRQ_TYPE_LEVEL_HIGH;
	return 0;
}

static int aspeed_intc1_map_irq_domain(struct irq_domain *domain,
				       unsigned int irq,
				       irq_hw_number_t hwirq)
{
	irq_domain_set_info(domain, irq, hwirq, &aspeed_intc_chip,
			    domain->host_data, handle_level_irq, NULL, NULL);
	return 0;
}

/*
 * In-bound interrupts are progressively merged into one out-bound interrupt in
 * groups of 32. Apply this fact to compress the route table in corresponding
 * groups of 32.
 */
static const u32
aspeed_intc1_routes[INTC1_IN_NUM / INTC1_ROUTE_IRQS_PER_GROUP][INTC1_ROUTE_NUM] = {
	{ 0, AST2700_INTC_INVALID_ROUTE, 10, 20, 30, 40, 50 },
	{ 1, AST2700_INTC_INVALID_ROUTE, 11, 21, 31, 41, 50 },
	{ 2, AST2700_INTC_INVALID_ROUTE, 12, 22, 32, 42, 50 },
	{ 3, AST2700_INTC_INVALID_ROUTE, 13, 23, 33, 43, 50 },
	{ 4, AST2700_INTC_INVALID_ROUTE, 14, 24, 34, 44, 50 },
	{ 5, AST2700_INTC_INVALID_ROUTE, 15, 25, 35, 45, 50 },
};

static int aspeed_intc1_irq_domain_activate(struct irq_domain *domain,
					    struct irq_data *data, bool reserve)
{
	struct aspeed_intc1 *intc1 = irq_data_get_irq_chip_data(data);
	struct aspeed_intc_interrupt_range resolved;
	int rc, bank, bit;
	u32 mask;

	if (WARN_ON_ONCE((data->hwirq >> INTC1_ROUTE_SHIFT) >= ARRAY_SIZE(aspeed_intc1_routes)))
		return -EINVAL;

	/*
	 * outpin may be an error if the upstream is the BootMCU APLIC node, or
	 * anything except a valid intc0 driver instance
	 */
	rc = aspeed_intc0_resolve_route(intc1->upstream, INTC1_ROUTE_NUM,
					aspeed_intc1_routes[data->hwirq >> INTC1_ROUTE_SHIFT],
					intc1->ranges.nranges,
					intc1->ranges.ranges, &resolved);
	if (rc < 0) {
		if (!fwnode_device_is_compatible(intc1->upstream->fwnode, "riscv,aplic")) {
			dev_warn(intc1->dev,
				 "Failed to resolve interrupt route for hwirq %lu in domain %s\n",
				 data->hwirq, domain->name);
			return rc;
		}
		rc = INTC1_BOOTMCU_ROUTE;
	}

	bank = data->hwirq / INTC1_IRQS_PER_BANK;
	bit = data->hwirq % INTC1_IRQS_PER_BANK;
	mask = BIT(bit);

	guard(raw_spinlock)(&intc1->intc_lock);
	for (int i = 0; i < INTC1_ROUTE_SELECTOR_BITS; i++) {
		void __iomem *sel = intc1->base + INTC1_SEL_BASE +
				    (bank * INTC1_SEL_BANK_SIZE) +
				    (INTC1_SEL_ROUTE_SIZE * i);
		u32 reg = readl(sel);

		if (rc & BIT(i))
			reg |= mask;
		else
			reg &= ~mask;

		writel(reg, sel);
		if (readl(sel) != reg)
			return -EACCES;
	}

	return 0;
}

static const struct irq_domain_ops aspeed_intc1_irq_domain_ops = {
	.map		= aspeed_intc1_map_irq_domain,
	.translate	= aspeed_intc1_irq_domain_translate,
	.activate	= aspeed_intc1_irq_domain_activate,
};

static void aspeed_intc1_request_interrupts(struct aspeed_intc1 *intc1)
{
	for (unsigned int i = 0; i < intc1->ranges.nranges; i++) {
		struct aspeed_intc_interrupt_range *r =
			&intc1->ranges.ranges[i];

		if (intc1->upstream != r->domain)
			continue;

		for (u32 k = 0; k < r->count; k++) {
			struct of_phandle_args parent_irq;
			int irq;

			parent_irq.np = to_of_node(r->upstream.fwnode);
			parent_irq.args_count = 1;
			parent_irq.args[0] =
				intc1->ranges.ranges[i].upstream.param[ASPEED_INTC_RANGES_BASE] + k;

			irq = irq_create_of_mapping(&parent_irq);
			if (!irq)
				continue;

			irq_set_chained_handler_and_data(irq,
							 aspeed_intc1_irq_handler, intc1);
		}
	}
}

static int aspeed_intc1_probe(struct platform_device *pdev,
			      struct device_node *parent)
{
	struct device_node *node = pdev->dev.of_node;
	struct aspeed_intc1 *intc1;
	struct irq_domain *host;
	int ret;

	if (!parent) {
		dev_err(&pdev->dev, "missing parent interrupt node\n");
		return -ENODEV;
	}

	if (!of_device_is_compatible(parent, "aspeed,ast2700-intc0"))
		return -ENODEV;

	host = irq_find_host(parent);
	if (!host)
		return -ENODEV;

	intc1 = devm_kzalloc(&pdev->dev, sizeof(*intc1), GFP_KERNEL);
	if (!intc1)
		return -ENOMEM;

	intc1->dev = &pdev->dev;
	intc1->upstream = host;
	intc1->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(intc1->base))
		return PTR_ERR(intc1->base);

	aspeed_intc1_disable_int(intc1);

	raw_spin_lock_init(&intc1->intc_lock);

	intc1->local = irq_domain_create_linear(of_fwnode_handle(node),
						INTC1_BANK_NUM * INTC1_IRQS_PER_BANK,
						&aspeed_intc1_irq_domain_ops, intc1);
	if (!intc1->local)
		return -ENOMEM;

	ret = aspeed_intc_populate_ranges(&pdev->dev, &intc1->ranges);
	if (ret < 0) {
		irq_domain_remove(intc1->local);
		return ret;
	}

	aspeed_intc1_request_interrupts(intc1);

	return 0;
}

IRQCHIP_PLATFORM_DRIVER_BEGIN(ast2700_intc1)
IRQCHIP_MATCH("aspeed,ast2700-intc1", aspeed_intc1_probe)
IRQCHIP_PLATFORM_DRIVER_END(ast2700_intc1)
