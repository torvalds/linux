// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2022 Linutronix GmbH
// Copyright (C) 2022 Intel

#include "irq-gic-common.h"
#include "irq-msi-lib.h"

#define ITS_MSI_FLAGS_REQUIRED  (MSI_FLAG_USE_DEF_DOM_OPS |	\
				 MSI_FLAG_USE_DEF_CHIP_OPS)

#define ITS_MSI_FLAGS_SUPPORTED (MSI_GENERIC_FLAGS_MASK |	\
				 MSI_FLAG_PCI_MSIX      |	\
				 MSI_FLAG_MULTI_PCI_MSI |	\
				 MSI_FLAG_PCI_MSI_MASK_PARENT)

static bool its_init_dev_msi_info(struct device *dev, struct irq_domain *domain,
				  struct irq_domain *real_parent, struct msi_domain_info *info)
{
	if (!msi_lib_init_dev_msi_info(dev, domain, real_parent, info))
		return false;

	return true;
}

const struct msi_parent_ops gic_v3_its_msi_parent_ops = {
	.supported_flags	= ITS_MSI_FLAGS_SUPPORTED,
	.required_flags		= ITS_MSI_FLAGS_REQUIRED,
	.bus_select_token	= DOMAIN_BUS_NEXUS,
	.prefix			= "ITS-",
	.init_dev_msi_info	= its_init_dev_msi_info,
};
