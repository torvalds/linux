// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 * Author: Varun Sethi <varun.sethi@freescale.com>
 */

#define pr_fmt(fmt)    "fsl-pamu-domain: %s: " fmt, __func__

#include "fsl_pamu_domain.h"

#include <linux/platform_device.h>
#include <sysdev/fsl_pci.h>

/*
 * Global spinlock that needs to be held while
 * configuring PAMU.
 */
static DEFINE_SPINLOCK(iommu_lock);

static struct kmem_cache *fsl_pamu_domain_cache;
static struct kmem_cache *iommu_devinfo_cache;
static DEFINE_SPINLOCK(device_domain_lock);

struct iommu_device pamu_iommu;	/* IOMMU core code handle */

static struct fsl_dma_domain *to_fsl_dma_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct fsl_dma_domain, iommu_domain);
}

static int __init iommu_init_mempool(void)
{
	fsl_pamu_domain_cache = kmem_cache_create("fsl_pamu_domain",
						  sizeof(struct fsl_dma_domain),
						  0,
						  SLAB_HWCACHE_ALIGN,
						  NULL);
	if (!fsl_pamu_domain_cache) {
		pr_debug("Couldn't create fsl iommu_domain cache\n");
		return -ENOMEM;
	}

	iommu_devinfo_cache = kmem_cache_create("iommu_devinfo",
						sizeof(struct device_domain_info),
						0,
						SLAB_HWCACHE_ALIGN,
						NULL);
	if (!iommu_devinfo_cache) {
		pr_debug("Couldn't create devinfo cache\n");
		kmem_cache_destroy(fsl_pamu_domain_cache);
		return -ENOMEM;
	}

	return 0;
}

static int update_liodn_stash(int liodn, struct fsl_dma_domain *dma_domain,
			      u32 val)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&iommu_lock, flags);
	ret = pamu_update_paace_stash(liodn, val);
	if (ret) {
		pr_debug("Failed to update SPAACE for liodn %d\n ", liodn);
		spin_unlock_irqrestore(&iommu_lock, flags);
		return ret;
	}

	spin_unlock_irqrestore(&iommu_lock, flags);

	return ret;
}

/* Set the geometry parameters for a LIODN */
static int pamu_set_liodn(struct fsl_dma_domain *dma_domain, struct device *dev,
			  int liodn)
{
	u32 omi_index = ~(u32)0;
	unsigned long flags;
	int ret;

	/*
	 * Configure the omi_index at the geometry setup time.
	 * This is a static value which depends on the type of
	 * device and would not change thereafter.
	 */
	get_ome_index(&omi_index, dev);

	spin_lock_irqsave(&iommu_lock, flags);
	ret = pamu_disable_liodn(liodn);
	if (ret)
		goto out_unlock;
	ret = pamu_config_ppaace(liodn, omi_index, dma_domain->stash_id, 0);
	if (ret)
		goto out_unlock;
	ret = pamu_config_ppaace(liodn, ~(u32)0, dma_domain->stash_id,
				 PAACE_AP_PERMS_QUERY | PAACE_AP_PERMS_UPDATE);
out_unlock:
	spin_unlock_irqrestore(&iommu_lock, flags);
	if (ret) {
		pr_debug("PAACE configuration failed for liodn %d\n",
			 liodn);
	}
	return ret;
}

static void remove_device_ref(struct device_domain_info *info)
{
	unsigned long flags;

	list_del(&info->link);
	spin_lock_irqsave(&iommu_lock, flags);
	pamu_disable_liodn(info->liodn);
	spin_unlock_irqrestore(&iommu_lock, flags);
	spin_lock_irqsave(&device_domain_lock, flags);
	dev_iommu_priv_set(info->dev, NULL);
	kmem_cache_free(iommu_devinfo_cache, info);
	spin_unlock_irqrestore(&device_domain_lock, flags);
}

static void detach_device(struct device *dev, struct fsl_dma_domain *dma_domain)
{
	struct device_domain_info *info, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&dma_domain->domain_lock, flags);
	/* Remove the device from the domain device list */
	list_for_each_entry_safe(info, tmp, &dma_domain->devices, link) {
		if (!dev || (info->dev == dev))
			remove_device_ref(info);
	}
	spin_unlock_irqrestore(&dma_domain->domain_lock, flags);
}

static void attach_device(struct fsl_dma_domain *dma_domain, int liodn, struct device *dev)
{
	struct device_domain_info *info, *old_domain_info;
	unsigned long flags;

	spin_lock_irqsave(&device_domain_lock, flags);
	/*
	 * Check here if the device is already attached to domain or not.
	 * If the device is already attached to a domain detach it.
	 */
	old_domain_info = dev_iommu_priv_get(dev);
	if (old_domain_info && old_domain_info->domain != dma_domain) {
		spin_unlock_irqrestore(&device_domain_lock, flags);
		detach_device(dev, old_domain_info->domain);
		spin_lock_irqsave(&device_domain_lock, flags);
	}

	info = kmem_cache_zalloc(iommu_devinfo_cache, GFP_ATOMIC);

	info->dev = dev;
	info->liodn = liodn;
	info->domain = dma_domain;

	list_add(&info->link, &dma_domain->devices);
	/*
	 * In case of devices with multiple LIODNs just store
	 * the info for the first LIODN as all
	 * LIODNs share the same domain
	 */
	if (!dev_iommu_priv_get(dev))
		dev_iommu_priv_set(dev, info);
	spin_unlock_irqrestore(&device_domain_lock, flags);
}

static phys_addr_t fsl_pamu_iova_to_phys(struct iommu_domain *domain,
					 dma_addr_t iova)
{
	if (iova < domain->geometry.aperture_start ||
	    iova > domain->geometry.aperture_end)
		return 0;
	return iova;
}

static bool fsl_pamu_capable(struct device *dev, enum iommu_cap cap)
{
	return cap == IOMMU_CAP_CACHE_COHERENCY;
}

static void fsl_pamu_domain_free(struct iommu_domain *domain)
{
	struct fsl_dma_domain *dma_domain = to_fsl_dma_domain(domain);

	/* remove all the devices from the device list */
	detach_device(NULL, dma_domain);
	kmem_cache_free(fsl_pamu_domain_cache, dma_domain);
}

static struct iommu_domain *fsl_pamu_domain_alloc(unsigned type)
{
	struct fsl_dma_domain *dma_domain;

	if (type != IOMMU_DOMAIN_UNMANAGED)
		return NULL;

	dma_domain = kmem_cache_zalloc(fsl_pamu_domain_cache, GFP_KERNEL);
	if (!dma_domain)
		return NULL;

	dma_domain->stash_id = ~(u32)0;
	INIT_LIST_HEAD(&dma_domain->devices);
	spin_lock_init(&dma_domain->domain_lock);

	/* default geometry 64 GB i.e. maximum system address */
	dma_domain->iommu_domain. geometry.aperture_start = 0;
	dma_domain->iommu_domain.geometry.aperture_end = (1ULL << 36) - 1;
	dma_domain->iommu_domain.geometry.force_aperture = true;

	return &dma_domain->iommu_domain;
}

/* Update stash destination for all LIODNs associated with the domain */
static int update_domain_stash(struct fsl_dma_domain *dma_domain, u32 val)
{
	struct device_domain_info *info;
	int ret = 0;

	list_for_each_entry(info, &dma_domain->devices, link) {
		ret = update_liodn_stash(info->liodn, dma_domain, val);
		if (ret)
			break;
	}

	return ret;
}

static int fsl_pamu_attach_device(struct iommu_domain *domain,
				  struct device *dev)
{
	struct fsl_dma_domain *dma_domain = to_fsl_dma_domain(domain);
	unsigned long flags;
	int len, ret = 0, i;
	const u32 *liodn;
	struct pci_dev *pdev = NULL;
	struct pci_controller *pci_ctl;

	/*
	 * Use LIODN of the PCI controller while attaching a
	 * PCI device.
	 */
	if (dev_is_pci(dev)) {
		pdev = to_pci_dev(dev);
		pci_ctl = pci_bus_to_host(pdev->bus);
		/*
		 * make dev point to pci controller device
		 * so we can get the LIODN programmed by
		 * u-boot.
		 */
		dev = pci_ctl->parent;
	}

	liodn = of_get_property(dev->of_node, "fsl,liodn", &len);
	if (!liodn) {
		pr_debug("missing fsl,liodn property at %pOF\n", dev->of_node);
		return -ENODEV;
	}

	spin_lock_irqsave(&dma_domain->domain_lock, flags);
	for (i = 0; i < len / sizeof(u32); i++) {
		/* Ensure that LIODN value is valid */
		if (liodn[i] >= PAACE_NUMBER_ENTRIES) {
			pr_debug("Invalid liodn %d, attach device failed for %pOF\n",
				 liodn[i], dev->of_node);
			ret = -ENODEV;
			break;
		}

		attach_device(dma_domain, liodn[i], dev);
		ret = pamu_set_liodn(dma_domain, dev, liodn[i]);
		if (ret)
			break;
		ret = pamu_enable_liodn(liodn[i]);
		if (ret)
			break;
	}
	spin_unlock_irqrestore(&dma_domain->domain_lock, flags);
	return ret;
}

static void fsl_pamu_detach_device(struct iommu_domain *domain,
				   struct device *dev)
{
	struct fsl_dma_domain *dma_domain = to_fsl_dma_domain(domain);
	const u32 *prop;
	int len;
	struct pci_dev *pdev = NULL;
	struct pci_controller *pci_ctl;

	/*
	 * Use LIODN of the PCI controller while detaching a
	 * PCI device.
	 */
	if (dev_is_pci(dev)) {
		pdev = to_pci_dev(dev);
		pci_ctl = pci_bus_to_host(pdev->bus);
		/*
		 * make dev point to pci controller device
		 * so we can get the LIODN programmed by
		 * u-boot.
		 */
		dev = pci_ctl->parent;
	}

	prop = of_get_property(dev->of_node, "fsl,liodn", &len);
	if (prop)
		detach_device(dev, dma_domain);
	else
		pr_debug("missing fsl,liodn property at %pOF\n", dev->of_node);
}

/* Set the domain stash attribute */
int fsl_pamu_configure_l1_stash(struct iommu_domain *domain, u32 cpu)
{
	struct fsl_dma_domain *dma_domain = to_fsl_dma_domain(domain);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&dma_domain->domain_lock, flags);
	dma_domain->stash_id = get_stash_id(PAMU_ATTR_CACHE_L1, cpu);
	if (dma_domain->stash_id == ~(u32)0) {
		pr_debug("Invalid stash attributes\n");
		spin_unlock_irqrestore(&dma_domain->domain_lock, flags);
		return -EINVAL;
	}
	ret = update_domain_stash(dma_domain, dma_domain->stash_id);
	spin_unlock_irqrestore(&dma_domain->domain_lock, flags);

	return ret;
}

static struct iommu_group *get_device_iommu_group(struct device *dev)
{
	struct iommu_group *group;

	group = iommu_group_get(dev);
	if (!group)
		group = iommu_group_alloc();

	return group;
}

static  bool check_pci_ctl_endpt_part(struct pci_controller *pci_ctl)
{
	u32 version;

	/* Check the PCI controller version number by readding BRR1 register */
	version = in_be32(pci_ctl->cfg_addr + (PCI_FSL_BRR1 >> 2));
	version &= PCI_FSL_BRR1_VER;
	/* If PCI controller version is >= 0x204 we can partition endpoints */
	return version >= 0x204;
}

/* Get iommu group information from peer devices or devices on the parent bus */
static struct iommu_group *get_shared_pci_device_group(struct pci_dev *pdev)
{
	struct pci_dev *tmp;
	struct iommu_group *group;
	struct pci_bus *bus = pdev->bus;

	/*
	 * Traverese the pci bus device list to get
	 * the shared iommu group.
	 */
	while (bus) {
		list_for_each_entry(tmp, &bus->devices, bus_list) {
			if (tmp == pdev)
				continue;
			group = iommu_group_get(&tmp->dev);
			if (group)
				return group;
		}

		bus = bus->parent;
	}

	return NULL;
}

static struct iommu_group *get_pci_device_group(struct pci_dev *pdev)
{
	struct pci_controller *pci_ctl;
	bool pci_endpt_partitioning;
	struct iommu_group *group = NULL;

	pci_ctl = pci_bus_to_host(pdev->bus);
	pci_endpt_partitioning = check_pci_ctl_endpt_part(pci_ctl);
	/* We can partition PCIe devices so assign device group to the device */
	if (pci_endpt_partitioning) {
		group = pci_device_group(&pdev->dev);

		/*
		 * PCIe controller is not a paritionable entity
		 * free the controller device iommu_group.
		 */
		if (pci_ctl->parent->iommu_group)
			iommu_group_remove_device(pci_ctl->parent);
	} else {
		/*
		 * All devices connected to the controller will share the
		 * PCI controllers device group. If this is the first
		 * device to be probed for the pci controller, copy the
		 * device group information from the PCI controller device
		 * node and remove the PCI controller iommu group.
		 * For subsequent devices, the iommu group information can
		 * be obtained from sibling devices (i.e. from the bus_devices
		 * link list).
		 */
		if (pci_ctl->parent->iommu_group) {
			group = get_device_iommu_group(pci_ctl->parent);
			iommu_group_remove_device(pci_ctl->parent);
		} else {
			group = get_shared_pci_device_group(pdev);
		}
	}

	if (!group)
		group = ERR_PTR(-ENODEV);

	return group;
}

static struct iommu_group *fsl_pamu_device_group(struct device *dev)
{
	struct iommu_group *group = ERR_PTR(-ENODEV);
	int len;

	/*
	 * For platform devices we allocate a separate group for
	 * each of the devices.
	 */
	if (dev_is_pci(dev))
		group = get_pci_device_group(to_pci_dev(dev));
	else if (of_get_property(dev->of_node, "fsl,liodn", &len))
		group = get_device_iommu_group(dev);

	return group;
}

static struct iommu_device *fsl_pamu_probe_device(struct device *dev)
{
	return &pamu_iommu;
}

static const struct iommu_ops fsl_pamu_ops = {
	.capable	= fsl_pamu_capable,
	.domain_alloc	= fsl_pamu_domain_alloc,
	.probe_device	= fsl_pamu_probe_device,
	.device_group   = fsl_pamu_device_group,
	.default_domain_ops = &(const struct iommu_domain_ops) {
		.attach_dev	= fsl_pamu_attach_device,
		.detach_dev	= fsl_pamu_detach_device,
		.iova_to_phys	= fsl_pamu_iova_to_phys,
		.free		= fsl_pamu_domain_free,
	}
};

int __init pamu_domain_init(void)
{
	int ret = 0;

	ret = iommu_init_mempool();
	if (ret)
		return ret;

	ret = iommu_device_sysfs_add(&pamu_iommu, NULL, NULL, "iommu0");
	if (ret)
		return ret;

	ret = iommu_device_register(&pamu_iommu, &fsl_pamu_ops, NULL);
	if (ret) {
		iommu_device_sysfs_remove(&pamu_iommu);
		pr_err("Can't register iommu device\n");
	}

	return ret;
}
