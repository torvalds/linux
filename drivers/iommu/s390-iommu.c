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

struct s390_domain_device {
	struct list_head	list;
	struct zpci_dev		*zdev;
};

static struct s390_domain *to_s390_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct s390_domain, domain);
}

static bool s390_iommu_capable(enum iommu_cap cap)
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

	spin_lock_init(&s390_domain->dma_table_lock);
	spin_lock_init(&s390_domain->list_lock);
	INIT_LIST_HEAD(&s390_domain->devices);

	return &s390_domain->domain;
}

static void s390_domain_free(struct iommu_domain *domain)
{
	struct s390_domain *s390_domain = to_s390_domain(domain);

	dma_cleanup_tables(s390_domain->dma_table);
	kfree(s390_domain);
}

static int s390_iommu_attach_device(struct iommu_domain *domain,
				    struct device *dev)
{
	struct s390_domain *s390_domain = to_s390_domain(domain);
	struct zpci_dev *zdev = to_zpci_dev(dev);
	struct s390_domain_device *domain_device;
	unsigned long flags;
	int rc;

	if (!zdev)
		return -ENODEV;

	domain_device = kzalloc(sizeof(*domain_device), GFP_KERNEL);
	if (!domain_device)
		return -ENOMEM;

	if (zdev->dma_table)
		zpci_dma_exit_device(zdev);

	zdev->dma_table = s390_domain->dma_table;
	rc = zpci_register_ioat(zdev, 0, zdev->start_dma, zdev->end_dma,
				(u64) zdev->dma_table);
	if (rc)
		goto out_restore;

	spin_lock_irqsave(&s390_domain->list_lock, flags);
	/* First device defines the DMA range limits */
	if (list_empty(&s390_domain->devices)) {
		domain->geometry.aperture_start = zdev->start_dma;
		domain->geometry.aperture_end = zdev->end_dma;
		domain->geometry.force_aperture = true;
	/* Allow only devices with identical DMA range limits */
	} else if (domain->geometry.aperture_start != zdev->start_dma ||
		   domain->geometry.aperture_end != zdev->end_dma) {
		rc = -EINVAL;
		spin_unlock_irqrestore(&s390_domain->list_lock, flags);
		goto out_restore;
	}
	domain_device->zdev = zdev;
	zdev->s390_domain = s390_domain;
	list_add(&domain_device->list, &s390_domain->devices);
	spin_unlock_irqrestore(&s390_domain->list_lock, flags);

	return 0;

out_restore:
	zpci_dma_init_device(zdev);
	kfree(domain_device);

	return rc;
}

static void s390_iommu_detach_device(struct iommu_domain *domain,
				     struct device *dev)
{
	struct s390_domain *s390_domain = to_s390_domain(domain);
	struct zpci_dev *zdev = to_zpci_dev(dev);
	struct s390_domain_device *domain_device, *tmp;
	unsigned long flags;
	int found = 0;

	if (!zdev)
		return;

	spin_lock_irqsave(&s390_domain->list_lock, flags);
	list_for_each_entry_safe(domain_device, tmp, &s390_domain->devices,
				 list) {
		if (domain_device->zdev == zdev) {
			list_del(&domain_device->list);
			kfree(domain_device);
			found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&s390_domain->list_lock, flags);

	if (found) {
		zdev->s390_domain = NULL;
		zpci_unregister_ioat(zdev, 0);
		zpci_dma_init_device(zdev);
	}
}

static struct iommu_device *s390_iommu_probe_device(struct device *dev)
{
	struct zpci_dev *zdev = to_zpci_dev(dev);

	return &zdev->iommu_dev;
}

static void s390_iommu_release_device(struct device *dev)
{
	struct zpci_dev *zdev = to_zpci_dev(dev);
	struct iommu_domain *domain;

	/*
	 * This is a workaround for a scenario where the IOMMU API common code
	 * "forgets" to call the detach_dev callback: After binding a device
	 * to vfio-pci and completing the VFIO_SET_IOMMU ioctl (which triggers
	 * the attach_dev), removing the device via
	 * "echo 1 > /sys/bus/pci/devices/.../remove" won't trigger detach_dev,
	 * only release_device will be called via the BUS_NOTIFY_REMOVED_DEVICE
	 * notifier.
	 *
	 * So let's call detach_dev from here if it hasn't been called before.
	 */
	if (zdev && zdev->s390_domain) {
		domain = iommu_get_domain_for_dev(dev);
		if (domain)
			s390_iommu_detach_device(domain, dev);
	}
}

static int s390_iommu_update_trans(struct s390_domain *s390_domain,
				   unsigned long pa, dma_addr_t dma_addr,
				   size_t size, int flags)
{
	struct s390_domain_device *domain_device;
	u8 *page_addr = (u8 *) (pa & PAGE_MASK);
	dma_addr_t start_dma_addr = dma_addr;
	unsigned long irq_flags, nr_pages, i;
	unsigned long *entry;
	int rc = 0;

	if (dma_addr < s390_domain->domain.geometry.aperture_start ||
	    dma_addr + size > s390_domain->domain.geometry.aperture_end)
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
	list_for_each_entry(domain_device, &s390_domain->devices, list) {
		rc = zpci_refresh_trans((u64) domain_device->zdev->fh << 32,
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

	rc = s390_iommu_update_trans(s390_domain, (unsigned long) paddr, iova,
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

	rc = s390_iommu_update_trans(s390_domain, (unsigned long) paddr, iova,
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
	.domain_free = s390_domain_free,
	.attach_dev = s390_iommu_attach_device,
	.detach_dev = s390_iommu_detach_device,
	.map = s390_iommu_map,
	.unmap = s390_iommu_unmap,
	.iova_to_phys = s390_iommu_iova_to_phys,
	.probe_device = s390_iommu_probe_device,
	.release_device = s390_iommu_release_device,
	.device_group = generic_device_group,
	.pgsize_bitmap = S390_IOMMU_PGSIZES,
};

static int __init s390_iommu_init(void)
{
	return bus_set_iommu(&pci_bus_type, &s390_iommu_ops);
}
subsys_initcall(s390_iommu_init);
