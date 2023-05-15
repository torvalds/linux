/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2002 ARM Limited, All Rights Reserved.
 */

#ifndef _IRQ_GIC_COMMON_H
#define _IRQ_GIC_COMMON_H

#include <linux/of.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/arm-gic-common.h>

struct gic_quirk {
	const char *desc;
	const char *compatible;
	const char *property;
	bool (*init)(void *data);
	u32 iidr;
	u32 mask;
};

int gic_configure_irq(unsigned int irq, unsigned int type,
                       void __iomem *base, void (*sync_access)(void));
void gic_dist_config(void __iomem *base, int gic_irqs,
		     void (*sync_access)(void));
void gic_cpu_config(void __iomem *base, int nr, void (*sync_access)(void));
void gic_enable_quirks(u32 iidr, const struct gic_quirk *quirks,
		void *data);
void gic_enable_of_quirks(const struct device_node *np,
			  const struct gic_quirk *quirks, void *data);

void gic_set_kvm_info(const struct gic_kvm_info *info);

#endif /* _IRQ_GIC_COMMON_H */
