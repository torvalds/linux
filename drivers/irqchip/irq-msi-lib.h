// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2022 Linutronix GmbH
// Copyright (C) 2022 Intel

#ifndef _DRIVERS_IRQCHIP_IRQ_MSI_LIB_H
#define _DRIVERS_IRQCHIP_IRQ_MSI_LIB_H

#include <linux/bits.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>

#ifdef CONFIG_PCI_MSI
#define MATCH_PCI_MSI		BIT(DOMAIN_BUS_PCI_MSI)
#else
#define MATCH_PCI_MSI		(0)
#endif

#define MATCH_PLATFORM_MSI	BIT(DOMAIN_BUS_PLATFORM_MSI)

int msi_lib_irq_domain_select(struct irq_domain *d, struct irq_fwspec *fwspec,
			      enum irq_domain_bus_token bus_token);

bool msi_lib_init_dev_msi_info(struct device *dev, struct irq_domain *domain,
			       struct irq_domain *real_parent,
			       struct msi_domain_info *info);

#endif /* _DRIVERS_IRQCHIP_IRQ_MSI_LIB_H */
