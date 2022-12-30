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
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <asm/pci_dma.h>

static const struct iommu_ops s390_iommu_ops;

struct s390_domain {
	struct iommu_domain	domain;
	struct list_head	devices;
	unsigned long		*dma_table;
	spinlock_t		list_lock;
	struct rcu_head		rcu;
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

	spin_lock_init(&s390_domain->list_lock);
	INIT_LIST_HEAD_RCU(&s390_domain->devices);

	return &s390_domain->domain;
}

static void s390_iommu_rcu_free_domain(struct rcu_head *head)
{
	struct s390_domain *s390_domain = container_of(head, struct s390_domain, rcu);

	dma_cleanup_tables(s390_domain->dma_table);
	kfree(s390_domain);
}

static void s390_domain_free(struct iommu_domain *domain)
{
	struct s390_domain *s390_domain = to_s390_domain(domain);

	rcu_read_lock();
	WARN_ON(!list_empty(&s390_domain->devices));
	rcu_read_unlock();

	call_rcu(&s390_domain->rcu, s390_iommu_rcu_free_domain);
}

static void __s390_iommu_detach_device(struct zpci_dev *zdev)
{
	struct s390_domain *s390_domain = zdev->s390_domain;
	unsigned long flags;

	if (!s390_domain)
		return;

	spin_lock_irqsave(&s390_domain->list_lock, flags);
	list_del_rcu(&zdev->iommu_list);
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
	u8 status;
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
				virt_to_phys(s390_domain->dma_table), &status);
	/*
	 * If the device is undergoing error recovery the reset code
	 * will re-establish the new domain.
	 */
	if (cc && status != ZPCI_PCI_ST_FUNC_NOT_AVAIL)
		return -EIO;
	zdev->dma_table = s390_domain->dma_table;

	zdev->dma_table = s390_domain->dma_table;
	zdev->s390_domain = s390_domain;

	spin_lock_irqsave(&s390_domain->list_lock, flags);
	list_add_rcu(&zdev->iommu_list, &s390_domain->devices);
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

static void s390_iommu_flush_iotlb_all(struct iommu_domain *domain)
{
	struct s390_domain *s390_domain = to_s390_domain(domain);
	struct zpci_dev *zdev;

	rcu_read_lock();
	list_for_each_entry_rcu(zdev, &s390_domain->devices, iommu_list) {
		zpci_refresh_trans((u64)zdev->fh << 32, zdev->start_dma,
				   zdev->end_dma - zdev->start_dma + 1);
	}
	rcu_read_unlock();
}

static void s390_iommu_iotlb_sync(struct iommu_domain *domain,
				  struct iommu_iotlb_gather *gather)
{
	struct s390_domain *s390_domain = to_s390_domain(domain);
	size_t size = gather->end - gather->start + 1;
	struct zpci_dev *zdev;

	/* If gather was never added to there is nothing to flush */
	if (!gather->end)
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(zdev, &s390_domain->devices, iommu_list) {
		zpci_refresh_trans((u64)zdev->fh << 32, gather->start,
				   size);
	}
	rcu_read_unlock();
}

static void s390_iommu_iotlb_sync_map(struct iommu_domain *domain,
				      unsigned long iova, size_t size)
{
	struct s390_domain *s390_domain = to_s390_domain(domain);
	struct zpci_dev *zdev;

	rcu_read_lock();
	list_for_each_entry_rcu(zdev, &s390_domain->devices, iommu_list) {
		if (!zdev->tlb_refresh)
			continue;
		zpci_refresh_trans((u64)zdev->fh << 32,
				   iova, size);
	}
	rcu_read_unlock();
}

static int s390_iommu_validate_trans(struct s390_domain *s390_domain,
				     phys_addr_t pa, dma_addr_t dma_addr,
				     unsigned long nr_pages, int flags)
{
	phys_addr_t page_addr = pa & PAGE_MASK;
	unsigned long *entry;
	unsigned long i;
	int rc;

	for (i = 0; i < nr_pages; i++) {
		entry = dma_walk_cpu_trans(s390_domain->dma_table, dma_addr);
		if (unlikely(!entry)) {
			rc = -ENOMEM;
			goto undo_cpu_trans;
		}
		dma_update_cpu_trans(entry, page_addr, flags);
		page_addr += PAGE_SIZE;
		dma_addr += PAGE_SIZE;
	}

	return 0;

undo_cpu_trans:
	while (i-- > 0) {
		dma_addr -= PAGE_SIZE;
		entry = dma_walk_cpu_trans(s390_domain->dma_table,
					   dma_addr);
		if (!entry)
			break;
		dma_update_cpu_trans(entry, 0, ZPCI_PTE_INVALID);
	}

	return rc;
}

static int s390_iommu_invalidate_trans(struct s390_domain *s390_domain,
				       dma_addr_t dma_addr, unsigned long nr_pages)
{
	unsigned long *entry;
	unsigned long i;
	int rc = 0;

	for (i = 0; i < nr_pages; i++) {
		entry = dma_walk_cpu_trans(s390_domain->dma_table, dma_addr);
		if (unlikely(!entry)) {
			rc = -EINVAL;
			break;
		}
		dma_update_cpu_trans(entry, 0, ZPCI_PTE_INVALID);
		dma_addr += PAGE_SIZE;
	}

	return rc;
}

static int s390_iommu_map_pages(struct iommu_domain *domain,
				unsigned long iova, phys_addr_t paddr,
				size_t pgsize, size_t pgcount,
				int prot, gfp_t gfp, size_t *mapped)
{
	struct s390_domain *s390_domain = to_s390_domain(domain);
	size_t size = pgcount << __ffs(pgsize);
	int flags = ZPCI_PTE_VALID, rc = 0;

	if (pgsize != SZ_4K)
		return -EINVAL;

	if (iova < s390_domain->domain.geometry.aperture_start ||
	    (iova + size - 1) > s390_domain->domain.geometry.aperture_end)
		return -EINVAL;

	if (!IS_ALIGNED(iova | paddr, pgsize))
		return -EINVAL;

	if (!(prot & IOMMU_READ))
		return -EINVAL;

	if (!(prot & IOMMU_WRITE))
		flags |= ZPCI_TABLE_PROTECTED;

	rc = s390_iommu_validate_trans(s390_domain, paddr, iova,
				       pgcount, flags);
	if (!rc)
		*mapped = size;

	return rc;
}

static phys_addr_t s390_iommu_iova_to_phys(struct iommu_domain *domain,
					   dma_addr_t iova)
{
	struct s390_domain *s390_domain = to_s390_domain(domain);
	unsigned long *rto, *sto, *pto;
	unsigned long ste, pte, rte;
	unsigned int rtx, sx, px;
	phys_addr_t phys = 0;

	if (iova < domain->geometry.aperture_start ||
	    iova > domain->geometry.aperture_end)
		return 0;

	rtx = calc_rtx(iova);
	sx = calc_sx(iova);
	px = calc_px(iova);
	rto = s390_domain->dma_table;

	rte = READ_ONCE(rto[rtx]);
	if (reg_entry_isvalid(rte)) {
		sto = get_rt_sto(rte);
		ste = READ_ONCE(sto[sx]);
		if (reg_entry_isvalid(ste)) {
			pto = get_st_pto(ste);
			pte = READ_ONCE(pto[px]);
			if (pt_entry_isvalid(pte))
				phys = pte & ZPCI_PTE_ADDR_MASK;
		}
	}

	return phys;
}

static size_t s390_iommu_unmap_pages(struct iommu_domain *domain,
				     unsigned long iova,
				     size_t pgsize, size_t pgcount,
				     struct iommu_iotlb_gather *gather)
{
	struct s390_domain *s390_domain = to_s390_domain(domain);
	size_t size = pgcount << __ffs(pgsize);
	int rc;

	if (WARN_ON(iova < s390_domain->domain.geometry.aperture_start ||
	    (iova + size - 1) > s390_domain->domain.geometry.aperture_end))
		return 0;

	rc = s390_iommu_invalidate_trans(s390_domain, iova, pgcount);
	if (rc)
		return 0;

	iommu_iotlb_gather_add_range(gather, iova, size);

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
	.pgsize_bitmap = SZ_4K,
	.get_resv_regions = s390_iommu_get_resv_regions,
	.default_domain_ops = &(const struct iommu_domain_ops) {
		.attach_dev	= s390_iommu_attach_device,
		.detach_dev	= s390_iommu_detach_device,
		.map_pages	= s390_iommu_map_pages,
		.unmap_pages	= s390_iommu_unmap_pages,
		.flush_iotlb_all = s390_iommu_flush_iotlb_all,
		.iotlb_sync      = s390_iommu_iotlb_sync,
		.iotlb_sync_map  = s390_iommu_iotlb_sync_map,
		.iova_to_phys	= s390_iommu_iova_to_phys,
		.free		= s390_domain_free,
	}
};
