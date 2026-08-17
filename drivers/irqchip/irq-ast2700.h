/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Aspeed AST2700 Interrupt Controller.
 *
 *  Copyright (C) 2026 ASPEED Technology Inc.
 */
#ifndef DRIVERS_IRQCHIP_AST2700
#define DRIVERS_IRQCHIP_AST2700

#include <linux/device.h>
#include <linux/irqdomain.h>

#define AST2700_INTC_INVALID_ROUTE (~0U)
#define ASPEED_INTC_RANGES_BASE		0U
#define ASPEED_INTC_RANGES_COUNT	1U

struct aspeed_intc_interrupt_range {
	u32               start;
	u32               count;
	struct irq_fwspec upstream;
	struct irq_domain *domain;
};

struct aspeed_intc_interrupt_ranges {
	struct aspeed_intc_interrupt_range *ranges;
	unsigned int                       nranges;
};

struct aspeed_intc0 {
	struct device				*dev;
	void __iomem				*base;
	raw_spinlock_t				intc_lock;
	struct irq_domain			*local;
	struct device_node			*parent;
	struct aspeed_intc_interrupt_ranges	ranges;
};

int aspeed_intc_populate_ranges(struct device *dev,
				struct aspeed_intc_interrupt_ranges *ranges);

int aspeed_intc0_resolve_route(const struct irq_domain *c0domain,
			       size_t nc1outs,
			       const u32 *c1outs,
			       size_t nc1ranges,
			       const struct aspeed_intc_interrupt_range *c1ranges,
			       struct aspeed_intc_interrupt_range *resolved);

#endif
