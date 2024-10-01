// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2013-2015 ARM Limited, All Rights Reserved.
// Author: Marc Zyngier <marc.zyngier@arm.com>
// Copyright (C) 2022 Linutronix GmbH
// Copyright (C) 2022 Intel

#include <linux/acpi_iort.h>
#include <linux/pci.h>

#include "irq-gic-common.h"
#include "irq-msi-lib.h"

#define ITS_MSI_FLAGS_REQUIRED  (MSI_FLAG_USE_DEF_DOM_OPS |	\
				 MSI_FLAG_USE_DEF_CHIP_OPS |	\
				 MSI_FLAG_PCI_MSI_MASK_PARENT)

#define ITS_MSI_FLAGS_SUPPORTED (MSI_GENERIC_FLAGS_MASK |	\
				 MSI_FLAG_PCI_MSIX      |	\
				 MSI_FLAG_MULTI_PCI_MSI)

#ifdef CONFIG_PCI_MSI
static int its_pci_msi_vec_count(struct pci_dev *pdev, void *data)
{
	int msi, msix, *count = data;

	msi = max(pci_msi_vec_count(pdev), 0);
	msix = max(pci_msix_vec_count(pdev), 0);
	*count += max(msi, msix);

	return 0;
}

static int its_get_pci_alias(struct pci_dev *pdev, u16 alias, void *data)
{
	struct pci_dev **alias_dev = data;

	*alias_dev = pdev;

	return 0;
}

static int its_pci_msi_prepare(struct irq_domain *domain, struct device *dev,
			       int nvec, msi_alloc_info_t *info)
{
	struct pci_dev *pdev, *alias_dev;
	struct msi_domain_info *msi_info;
	int alias_count = 0, minnvec = 1;

	if (!dev_is_pci(dev))
		return -EINVAL;

	pdev = to_pci_dev(dev);
	/*
	 * If pdev is downstream of any aliasing bridges, take an upper
	 * bound of how many other vectors could map to the same DevID.
	 * Also tell the ITS that the signalling will come from a proxy
	 * device, and that special allocation rules apply.
	 */
	pci_for_each_dma_alias(pdev, its_get_pci_alias, &alias_dev);
	if (alias_dev != pdev) {
		if (alias_dev->subordinate)
			pci_walk_bus(alias_dev->subordinate,
				     its_pci_msi_vec_count, &alias_count);
		info->flags |= MSI_ALLOC_FLAGS_PROXY_DEVICE;
	}

	/* ITS specific DeviceID, as the core ITS ignores dev. */
	info->scratchpad[0].ul = pci_msi_domain_get_msi_rid(domain->parent, pdev);

	/*
	 * @domain->msi_domain_info->hwsize contains the size of the
	 * MSI[-X] domain, but vector allocation happens one by one. This
	 * needs some thought when MSI comes into play as the size of MSI
	 * might be unknown at domain creation time and therefore set to
	 * MSI_MAX_INDEX.
	 */
	msi_info = msi_get_domain_info(domain);
	if (msi_info->hwsize > nvec)
		nvec = msi_info->hwsize;

	/*
	 * Always allocate a power of 2, and special case device 0 for
	 * broken systems where the DevID is not wired (and all devices
	 * appear as DevID 0). For that reason, we generously allocate a
	 * minimum of 32 MSIs for DevID 0. If you want more because all
	 * your devices are aliasing to DevID 0, consider fixing your HW.
	 */
	nvec = max(nvec, alias_count);
	if (!info->scratchpad[0].ul)
		minnvec = 32;
	nvec = max_t(int, minnvec, roundup_pow_of_two(nvec));

	msi_info = msi_get_domain_info(domain->parent);
	return msi_info->ops->msi_prepare(domain->parent, dev, nvec, info);
}
#else /* CONFIG_PCI_MSI */
#define its_pci_msi_prepare	NULL
#endif /* !CONFIG_PCI_MSI */

static int of_pmsi_get_dev_id(struct irq_domain *domain, struct device *dev,
				  u32 *dev_id)
{
	int ret, index = 0;

	/* Suck the DeviceID out of the msi-parent property */
	do {
		struct of_phandle_args args;

		ret = of_parse_phandle_with_args(dev->of_node,
						 "msi-parent", "#msi-cells",
						 index, &args);
		if (args.np == irq_domain_get_of_node(domain)) {
			if (WARN_ON(args.args_count != 1))
				return -EINVAL;
			*dev_id = args.args[0];
			break;
		}
		index++;
	} while (!ret);

	return ret;
}

int __weak iort_pmsi_get_dev_id(struct device *dev, u32 *dev_id)
{
	return -1;
}

static int its_pmsi_prepare(struct irq_domain *domain, struct device *dev,
			    int nvec, msi_alloc_info_t *info)
{
	struct msi_domain_info *msi_info;
	u32 dev_id;
	int ret;

	if (dev->of_node)
		ret = of_pmsi_get_dev_id(domain->parent, dev, &dev_id);
	else
		ret = iort_pmsi_get_dev_id(dev, &dev_id);
	if (ret)
		return ret;

	/* ITS specific DeviceID, as the core ITS ignores dev. */
	info->scratchpad[0].ul = dev_id;

	/*
	 * @domain->msi_domain_info->hwsize contains the size of the device
	 * domain, but vector allocation happens one by one.
	 */
	msi_info = msi_get_domain_info(domain);
	if (msi_info->hwsize > nvec)
		nvec = msi_info->hwsize;

	/* Allocate at least 32 MSIs, and always as a power of 2 */
	nvec = max_t(int, 32, roundup_pow_of_two(nvec));

	msi_info = msi_get_domain_info(domain->parent);
	return msi_info->ops->msi_prepare(domain->parent,
					  dev, nvec, info);
}

static bool its_init_dev_msi_info(struct device *dev, struct irq_domain *domain,
				  struct irq_domain *real_parent, struct msi_domain_info *info)
{
	if (!msi_lib_init_dev_msi_info(dev, domain, real_parent, info))
		return false;

	switch(info->bus_token) {
	case DOMAIN_BUS_PCI_DEVICE_MSI:
	case DOMAIN_BUS_PCI_DEVICE_MSIX:
		/*
		 * FIXME: This probably should be done after a (not yet
		 * existing) post domain creation callback once to make
		 * support for dynamic post-enable MSI-X allocations
		 * work without having to reevaluate the domain size
		 * over and over. It is known already at allocation
		 * time via info->hwsize.
		 *
		 * That should work perfectly fine for MSI/MSI-X but needs
		 * some thoughts for purely software managed MSI domains
		 * where the index space is only limited artificially via
		 * %MSI_MAX_INDEX.
		 */
		info->ops->msi_prepare = its_pci_msi_prepare;
		break;
	case DOMAIN_BUS_DEVICE_MSI:
	case DOMAIN_BUS_WIRED_TO_MSI:
		/*
		 * FIXME: See the above PCI prepare comment. The domain
		 * size is also known at domain creation time.
		 */
		info->ops->msi_prepare = its_pmsi_prepare;
		break;
	default:
		/* Confused. How did the lib return true? */
		WARN_ON_ONCE(1);
		return false;
	}

	return true;
}

const struct msi_parent_ops gic_v3_its_msi_parent_ops = {
	.supported_flags	= ITS_MSI_FLAGS_SUPPORTED,
	.required_flags		= ITS_MSI_FLAGS_REQUIRED,
	.bus_select_token	= DOMAIN_BUS_NEXUS,
	.bus_select_mask	= MATCH_PCI_MSI | MATCH_PLATFORM_MSI,
	.prefix			= "ITS-",
	.init_dev_msi_info	= its_init_dev_msi_info,
};
