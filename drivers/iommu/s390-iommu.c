// SPDX-License-Identifier: GPL-2.0
/*
 * IOMMU API for s390 PCI devices
 *
 * Copyright IBM Corp. 2015
 * Author(s): Gerald Schaefer <gerald.schaefer@de.ibm.com>
 */

#include <linux/pci.h>
#include <linux/iommu.h>
#include <linux/iommu-helper.h>
#include <linux/sizes.h>
#include <asm/pci_dma.h>

/*
 * Physically contiguous memory regions can be mapped with 4 KiB alignment,
 * we allow all page sizes that are an order of 4KiB (no special large page
 * support so far).
 */
#define S390_IOMMU_PGSIZES	(~0xFFFUL)

static const struct iommu_ops s390_iommu_ops;

struct s390_domain {
	struct iommu_domain	domain;
	struct list_head	devices;
	unsigned long		*dma_table;
	spinlock_t		dma_table_lock;
	spinlock_t		list_lock;
};

static struct s390_domain *to_s390_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct s390_domain, domain);
}

static bool s390_iommu_capable(struct device *dev, enum iommu_cap cap)
{
	switch (cap) {
	case IOMMU_CAP_CACHE_COHERENCY:
		return true;
	case IOMMU_CAP_INTR_REMAP:
		return true;
	default:
		return false;
	}
}

static struct iommu_domain *s390_domain_alloc(unsigned domain_type)
{
	struct s390_domain *s390_domain;

	if (domain_type != IOMMU_DOMAIN_UNMANAGED)
		return NULL;

	s390_domain = kzalloc(sizeof(*s390_domain), GFP_KERNEL);
	if (!s390_domain)
		return NULL;

	s390_domain->dma_table = dma_alloc_cpu_table();
	if (!s390_domain->dma_table) {
		kfree(s390_domain);
		return NULL;
	}
	s390_domain->domain.geometry.force_aperture = true;
	s390_domain->domain.geometry.aperture_start = 0;
	s390_domain->domain.geometry.aperture_end = ZPCI_TABLE_SIZE_RT - 1;

	spin_lock_init(&s390_domain->dma_table_lock);
	spin_lock_init(&s390_domain->list_lock);
	INIT_LIST_HEAD(&s390_domain->devices);

	return &s390_domain->domain;
}

static void s390_domain_free(struct iommu_domain *domain)
{
	struct s390_domain *s390_domain = to_s390_domain(domain);

	WARN_ON(!list_empty(&s390_domain->devices));
	dma_cleanup_tables(s390_domain->dma_table);
	kfree(s390_domain);
}

static void __s390_iommu_detach_device(struct zpci_dev *zdev)
{
	struct s390_domain *s390_domain = zdev->s390_domain;
	unsigned long flags;

	if (!s390_domain)
		return;

	spin_lock_irqsave(&s390_domain->list_lock, flags);
	list_del_init(&zdev->iommu_list);
	spin_unlock_irqrestore(&s390_domain->list_lock, flags);

	zpci_unregister_ioat(zdev, 0);
	zdev->s390_domain = NULL;
	zdev->dma_table = NULL;
}

static int s390_iommu_attach_device(struct iommu_domain *domain,
				    struct device *dev)
{
	struct s390_domain *s390_domain = to_s390_domain(domain);
	struct zpci_dev *zdev = to_zpci_dev(dev);
	unsigned long flags;
	int cc;

	if (!zdev)
		return -ENODEV;

	if (WARN_ON(domain->geometry.aperture_start > zdev->end_dma ||
		domain->geometry.aperture_end < zdev->start_dma))
		return -EINVAL;

	if (zdev->s390_domain)
		__s390_iommu_detach_device(zdev);
	else if (zdev->dma_table)
		zpci_dma_exit_device(zdev);

	cc = zpci_register_ioat(zdev, 0, zdev->start_dma, zdev->end_dma,
				virt_to_phys(s390_domain->dma_table));
	if (cc)
		return -EIO;
	zdev->dma_table = s390_domain->dma_table;

	zdev->dma_table = s390_domain->dma_table;
	zdev->s390_domain = s390_domain;

	spin_lock_irqsave(&s390_domain->list_lock, flags);
	list_add(&zdev->iommu_list, &s390_domain->devices);
	spin_unlock_irqrestore(&s390_domain->list_lock, flags);

	return 0;
}

static void s390_iommu_detach_device(struct iommu_domain *domain,
				     struct device *dev)
{
	struct zpci_dev *zdev = to_zpci_dev(dev);

	WARN_ON(zdev->s390_domain != to_s390_domain(domain));

	__s390_iommu_detach_device(zdev);
	zpci_dma_init_device(zdev);
}

static void s390_iommu_get_resv_regions(struct device *dev,
					struct list_head *list)
{
	struct zpci_dev *zdev = to_zpci_dev(dev);
	struct iommu_resv_region *region;

	if (zdev->start_dma) {
		region = iommu_alloc_resv_region(0, zdev->start_dma, 0,
						 IOMMU_RESV_RESERVED, GFP_KERNEL);
		if (!region)
			return;
		list_add_tail(&region->list, list);
	}

	if (zdev->end_dma < ZPCI_TABLE_SIZE_RT - 1) {
		region = iommu_alloc_resv_region(zdev->end_dma + 1,
						 ZPCI_TABLE_SIZE_RT - zdev->end_dma - 1,
						 0, IOMMU_RESV_RESERVED, GFP_KERNEL);
		if (!region)
			return;
		list_add_tail(&region->list, list);
	}
}

static struct iommu_device *s390_iommu_probe_device(struct device *dev)
{
	struct zpci_dev *zdev;

	if (!dev_is_pci(dev))
		return ERR_PTR(-ENODEV);

	zdev = to_zpci_dev(dev);

	if (zdev->start_dma > zdev->end_dma ||
	    zdev->start_dma > ZPCI_TABLE_SIZE_RT - 1)
		return ERR_PTR(-EINVAL);

	if (zdev->end_dma > ZPCI_TABLE_SIZE_RT - 1)
		zdev->end_dma = ZPCI_TABLE_SIZE_RT - 1;

	return &zdev->iommu_dev;
}

static void s390_iommu_release_device(struct device *dev)
{
	struct zpci_dev *zdev = to_zpci_dev(dev);

	/*
	 * release_device is expected to detach any domain currently attached
	 * to the device, but keep it attached to other devices in the group.
	 */
	if (zdev)
		__s390_iommu_detach_device(zdev);
}

static int s390_iommu_update_trans(struct s390_domain *s390_domain,
				   phys_addr_t pa, dma_addr_t dma_addr,
				   size_t size, int flags)
{
	phys_addr_t page_addr = pa & PAGE_MASK;
	dma_addr_t start_dma_addr = dma_addr;
	unsigned long irq_flags, nr_pages, i;
	struct zpci_dev *zdev;
	unsigned long *entry;
	int rc = 0;

	if (dma_addr < s390_domain->domain.geometry.aperture_start ||
	    (dma_addr + size - 1) > s390_domain->domain.geometry.aperture_end)
		return -EINVAL;

	nr_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
	if (!nr_pages)
		return 0;

	spin_lock_irqsave(&s390_domain->dma_table_lock, irq_flags);
	for (i = 0; i < nr_pages; i++) {
		entry = dma_walk_cpu_trans(s390_domain->dma_table, dma_addr);
		if (!entry) {
			rc = -ENOMEM;
			goto undo_cpu_trans;
		}
		dma_update_cpu_trans(entry, page_addr, flags);
		page_addr += PAGE_SIZE;
		dma_addr += PAGE_SIZE;
	}

	spin_lock(&s390_domain->list_lock);
	list_for_each_entry(zdev, &s390_domain->devices, iommu_list) {
		rc = zpci_refresh_trans((u64)zdev->fh << 32,
					start_dma_addr, nr_pages * PAGE_SIZE);
		if (rc)
			break;
	}
	spin_unlock(&s390_domain->list_lock);

undo_cpu_trans:
	if (rc && ((flags & ZPCI_PTE_VALID_MASK) == ZPCI_PTE_VALID)) {
		flags = ZPCI_PTE_INVALID;
		while (i-- > 0) {
			page_addr -= PAGE_SIZE;
			dma_addr -= PAGE_SIZE;
			entry = dma_walk_cpu_trans(s390_domain->dma_table,
						   dma_addr);
			if (!entry)
				break;
			dma_update_cpu_trans(entry, page_addr, flags);
		}
	}
	spin_unlock_irqrestore(&s390_domain->dma_table_lock, irq_flags);

	return rc;
}

static int s390_iommu_map(struct iommu_domain *domain, unsigned long iova,
			  phys_addr_t paddr, size_t size, int prot, gfp_t gfp)
{
	struct s390_domain *s390_domain = to_s390_domain(domain);
	int flags = ZPCI_PTE_VALID, rc = 0;

	if (!(prot & IOMMU_READ))
		return -EINVAL;

	if (!(prot & IOMMU_WRITE))
		flags |= ZPCI_TABLE_PROTECTED;

	rc = s390_iommu_update_trans(s390_domain, paddr, iova,
				     size, flags);

	return rc;
}

static phys_addr_t s390_iommu_iova_to_phys(struct iommu_domain *domain,
					   dma_addr_t iova)
{
	struct s390_domain *s390_domain = to_s390_domain(domain);
	unsigned long *sto, *pto, *rto, flags;
	unsigned int rtx, sx, px;
	phys_addr_t phys = 0;

	if (iova < domain->geometry.aperture_start ||
	    iova > domain->geometry.aperture_end)
		return 0;

	rtx = calc_rtx(iova);
	sx = calc_sx(iova);
	px = calc_px(iova);
	rto = s390_domain->dma_table;

	spin_lock_irqsave(&s390_domain->dma_table_lock, flags);
	if (rto && reg_entry_isvalid(rto[rtx])) {
		sto = get_rt_sto(rto[rtx]);
		if (sto && reg_entry_isvalid(sto[sx])) {
			pto = get_st_pto(sto[sx]);
			if (pto && pt_entry_isvalid(pto[px]))
				phys = pto[px] & ZPCI_PTE_ADDR_MASK;
		}
	}
	spin_unlock_irqrestore(&s390_domain->dma_table_lock, flags);

	return phys;
}

static size_t s390_iommu_unmap(struct iommu_domain *domain,
			       unsigned long iova, size_t size,
			       struct iommu_iotlb_gather *gather)
{
	struct s390_domain *s390_domain = to_s390_domain(domain);
	int flags = ZPCI_PTE_INVALID;
	phys_addr_t paddr;
	int rc;

	paddr = s390_iommu_iova_to_phys(domain, iova);
	if (!paddr)
		return 0;

	rc = s390_iommu_update_trans(s390_domain, paddr, iova,
				     size, flags);
	if (rc)
		return 0;

	return size;
}

int zpci_init_iommu(struct zpci_dev *zdev)
{
	int rc = 0;

	rc = iommu_device_sysfs_add(&zdev->iommu_dev, NULL, NULL,
				    "s390-iommu.%08x", zdev->fid);
	if (rc)
		goto out_err;

	rc = iommu_device_register(&zdev->iommu_dev, &s390_iommu_ops, NULL);
	if (rc)
		goto out_sysfs;

	return 0;

out_sysfs:
	iommu_device_sysfs_remove(&zdev->iommu_dev);

out_err:
	return rc;
}

void zpci_destroy_iommu(struct zpci_dev *zdev)
{
	iommu_device_unregister(&zdev->iommu_dev);
	iommu_device_sysfs_remove(&zdev->iommu_dev);
}

static const struct iommu_ops s390_iommu_ops = {
	.capable = s390_iommu_capable,
	.domain_alloc = s390_domain_alloc,
	.probe_device = s390_iommu_probe_device,
	.release_device = s390_iommu_release_device,
	.device_group = generic_device_group,
	.pgsize_bitmap = S390_IOMMU_PGSIZES,
	.get_resv_regions = s390_iommu_get_resv_regions,
	.default_domain_ops = &(const struct iommu_domain_ops) {
		.attach_dev	= s390_iommu_attach_device,
		.detach_dev	= s390_iommu_detach_device,
		.map		= s390_iommu_map,
		.unmap		= s390_iommu_unmap,
		.iova_to_phys	= s390_iommu_iova_to_phys,
		.free		= s390_domain_free,
	}
};
