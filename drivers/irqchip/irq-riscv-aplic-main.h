/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 * Copyright (C) 2022 Ventana Micro Systems Inc.
 */

#ifndef _IRQ_RISCV_APLIC_MAIN_H
#define _IRQ_RISCV_APLIC_MAIN_H

#include <linux/device.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/fwnode.h>

#define APLIC_DEFAULT_PRIORITY		1

struct aplic_msicfg {
	phys_addr_t		base_ppn;
	u32			hhxs;
	u32			hhxw;
	u32			lhxs;
	u32			lhxw;
};

struct aplic_priv {
	struct device		*dev;
	u32			gsi_base;
	u32			nr_irqs;
	u32			nr_idcs;
	void __iomem		*regs;
	struct aplic_msicfg	msicfg;
};

void aplic_irq_unmask(struct irq_data *d);
void aplic_irq_mask(struct irq_data *d);
int aplic_irq_set_type(struct irq_data *d, unsigned int type);
int aplic_irqdomain_translate(struct irq_fwspec *fwspec, u32 gsi_base,
			      unsigned long *hwirq, unsigned int *type);
void aplic_init_hw_global(struct aplic_priv *priv, bool msi_mode);
int aplic_setup_priv(struct aplic_priv *priv, struct device *dev, void __iomem *regs);
int aplic_direct_setup(struct device *dev, void __iomem *regs);
#ifdef CONFIG_RISCV_APLIC_MSI
int aplic_msi_setup(struct device *dev, void __iomem *regs);
#else
static inline int aplic_msi_setup(struct device *dev, void __iomem *regs)
{
	return -ENODEV;
}
#endif

#endif
