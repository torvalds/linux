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

#include "dma-iommu.h"

static const struct iommu_ops s390_iommu_ops;

static struct kmem_cache *dma_region_table_cache;
static struct kmem_cache *dma_page_table_cache;

static u64 s390_iommu_aperture;
static u32 s390_iommu_aperture_factor = 1;

struct s390_domain {
	struct iommu_domain	domain;
	struct list_head	devices;
	struct zpci_iommu_ctrs	ctrs;
	unsigned long		*dma_table;
	spinlock_t		list_lock;
	struct rcu_head		rcu;
};

static inline unsigned int calc_rtx(dma_addr_t ptr)
{
	return ((unsigned long)ptr >> ZPCI_RT_SHIFT) & ZPCI_INDEX_MASK;
}

static inline unsigned int calc_sx(dma_addr_t ptr)
{
	return ((unsigned long)ptr >> ZPCI_ST_SHIFT) & ZPCI_INDEX_MASK;
}

static inline unsigned int calc_px(dma_addr_t ptr)
{
	return ((unsigned long)ptr >> PAGE_SHIFT) & ZPCI_PT_MASK;
}

static inline void set_pt_pfaa(unsigned long *entry, phys_addr_t pfaa)
{
	*entry &= ZPCI_PTE_FLAG_MASK;
	*entry |= (pfaa & ZPCI_PTE_ADDR_MASK);
}

static inline void set_rt_sto(unsigned long *entry, phys_addr_t sto)
{
	*entry &= ZPCI_RTE_FLAG_MASK;
	*entry |= (sto & ZPCI_RTE_ADDR_MASK);
	*entry |= ZPCI_TABLE_TYPE_RTX;
}

static inline void set_st_pto(unsigned long *entry, phys_addr_t pto)
{
	*entry &= ZPCI_STE_FLAG_MASK;
	*entry |= (pto & ZPCI_STE_ADDR_MASK);
	*entry |= ZPCI_TABLE_TYPE_SX;
}

static inline void validate_rt_entry(unsigned long *entry)
{
	*entry &= ~ZPCI_TABLE_VALID_MASK;
	*entry &= ~ZPCI_TABLE_OFFSET_MASK;
	*entry |= ZPCI_TABLE_VALID;
	*entry |= ZPCI_TABLE_LEN_RTX;
}

static inline void validate_st_entry(unsigned long *entry)
{
	*entry &= ~ZPCI_TABLE_VALID_MASK;
	*entry |= ZPCI_TABLE_VALID;
}

static inline void invalidate_pt_entry(unsigned long *entry)
{
	WARN_ON_ONCE((*entry & ZPCI_PTE_VALID_MASK) == ZPCI_PTE_INVALID);
	*entry &= ~ZPCI_PTE_VALID_MASK;
	*entry |= ZPCI_PTE_INVALID;
}

static inline void validate_pt_entry(unsigned long *entry)
{
	WARN_ON_ONCE((*entry & ZPCI_PTE_VALID_MASK) == ZPCI_PTE_VALID);
	*entry &= ~ZPCI_PTE_VALID_MASK;
	*entry |= ZPCI_PTE_VALID;
}

static inline void entry_set_protected(unsigned long *entry)
{
	*entry &= ~ZPCI_TABLE_PROT_MASK;
	*entry |= ZPCI_TABLE_PROTECTED;
}

static inline void entry_clr_protected(unsigned long *entry)
{
	*entry &= ~ZPCI_TABLE_PROT_MASK;
	*entry |= ZPCI_TABLE_UNPROTECTED;
}

static inline int reg_entry_isvalid(unsigned long entry)
{
	return (entry & ZPCI_TABLE_VALID_MASK) == ZPCI_TABLE_VALID;
}

static inline int pt_entry_isvalid(unsigned long entry)
{
	return (entry & ZPCI_PTE_VALID_MASK) == ZPCI_PTE_VALID;
}

static inline unsigned long *get_rt_sto(unsigned long entry)
{
	if ((entry & ZPCI_TABLE_TYPE_MASK) == ZPCI_TABLE_TYPE_RTX)
		return phys_to_virt(entry & ZPCI_RTE_ADDR_MASK);
	else
		return NULL;
}

static inline unsigned long *get_st_pto(unsigned long entry)
{
	if ((entry & ZPCI_TABLE_TYPE_MASK) == ZPCI_TABLE_TYPE_SX)
		return phys_to_virt(entry & ZPCI_STE_ADDR_MASK);
	else
		return NULL;
}

static int __init dma_alloc_cpu_table_caches(void)
{
	dma_region_table_cache = kmem_cache_create("PCI_DMA_region_tables",
						   ZPCI_TABLE_SIZE,
						   ZPCI_TABLE_ALIGN,
						   0, NULL);
	if (!dma_region_table_cache)
		return -ENOMEM;

	dma_page_table_cache = kmem_cache_create("PCI_DMA_page_tables",
						 ZPCI_PT_SIZE,
						 ZPCI_PT_ALIGN,
						 0, NULL);
	if (!dma_page_table_cache) {
		kmem_cache_destroy(dma_region_table_cache);
		return -ENOMEM;
	}
	return 0;
}

static unsigned long *dma_alloc_cpu_table(gfp_t gfp)
{
	unsigned long *table, *entry;

	table = kmem_cache_alloc(dma_region_table_cache, gfp);
	if (!table)
		return NULL;

	for (entry = table; entry < table + ZPCI_TABLE_ENTRIES; entry++)
		*entry = ZPCI_TABLE_INVALID;
	return table;
}

static void dma_free_cpu_table(void *table)
{
	kmem_cache_free(dma_region_table_cache, table);
}

static void dma_free_page_table(void *table)
{
	kmem_cache_free(dma_page_table_cache, table);
}

static void dma_free_seg_table(unsigned long entry)
{
	unsigned long *sto = get_rt_sto(entry);
	int sx;

	for (sx = 0; sx < ZPCI_TABLE_ENTRIES; sx++)
		if (reg_entry_isvalid(sto[sx]))
			dma_free_page_table(get_st_pto(sto[sx]));

	dma_free_cpu_table(sto);
}

static void dma_cleanup_tables(unsigned long *table)
{
	int rtx;

	if (!table)
		return;

	for (rtx = 0; rtx < ZPCI_TABLE_ENTRIES; rtx++)
		if (reg_entry_isvalid(table[rtx]))
			dma_free_seg_table(table[rtx]);

	dma_free_cpu_table(table);
}

static unsigned long *dma_alloc_page_table(gfp_t gfp)
{
	unsigned long *table, *entry;

	table = kmem_cache_alloc(dma_page_table_cache, gfp);
	if (!table)
		return NULL;

	for (entry = table; entry < table + ZPCI_PT_ENTRIES; entry++)
		*entry = ZPCI_PTE_INVALID;
	return table;
}

static unsigned long *dma_get_seg_table_origin(unsigned long *rtep, gfp_t gfp)
{
	unsigned long old_rte, rte;
	unsigned long *sto;

	rte = READ_ONCE(*rtep);
	if (reg_entry_isvalid(rte)) {
		sto = get_rt_sto(rte);
	} else {
		sto = dma_alloc_cpu_table(gfp);
		if (!sto)
			return NULL;

		set_rt_sto(&rte, virt_to_phys(sto));
		validate_rt_entry(&rte);
		entry_clr_protected(&rte);

		old_rte = cmpxchg(rtep, ZPCI_TABLE_INVALID, rte);
		if (old_rte != ZPCI_TABLE_INVALID) {
			/* Somone else was faster, use theirs */
			dma_free_cpu_table(sto);
			sto = get_rt_sto(old_rte);
		}
	}
	return sto;
}

static unsigned long *dma_get_page_table_origin(unsigned long *step, gfp_t gfp)
{
	unsigned long old_ste, ste;
	unsigned long *pto;

	ste = READ_ONCE(*step);
	if (reg_entry_isvalid(ste)) {
		pto = get_st_pto(ste);
	} else {
		pto = dma_alloc_page_table(gfp);
		if (!pto)
			return NULL;
		set_st_pto(&ste, virt_to_phys(pto));
		validate_st_entry(&ste);
		entry_clr_protected(&ste);

		old_ste = cmpxchg(step, ZPCI_TABLE_INVALID, ste);
		if (old_ste != ZPCI_TABLE_INVALID) {
			/* Somone else was faster, use theirs */
			dma_free_page_table(pto);
			pto = get_st_pto(old_ste);
		}
	}
	return pto;
}

static unsigned long *dma_walk_cpu_trans(unsigned long *rto, dma_addr_t dma_addr, gfp_t gfp)
{
	unsigned long *sto, *pto;
	unsigned int rtx, sx, px;

	rtx = calc_rtx(dma_addr);
	sto = dma_get_seg_table_origin(&rto[rtx], gfp);
	if (!sto)
		return NULL;

	sx = calc_sx(dma_addr);
	pto = dma_get_page_table_origin(&sto[sx], gfp);
	if (!pto)
		return NULL;

	px = calc_px(dma_addr);
	return &pto[px];
}

static void dma_update_cpu_trans(unsigned long *ptep, phys_addr_t page_addr, int flags)
{
	unsigned long pte;

	pte = READ_ONCE(*ptep);
	if (flags & ZPCI_PTE_INVALID) {
		invalidate_pt_entry(&pte);
	} else {
		set_pt_pfaa(&pte, page_addr);
		validate_pt_entry(&pte);
	}

	if (flags & ZPCI_TABLE_PROTECTED)
		entry_set_protected(&pte);
	else
		entry_clr_protected(&pte);

	xchg(ptep, pte);
}

static struct s390_domain *to_s390_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct s390_domain, domain);
}

static bool s390_iommu_capable(struct device *dev, enum iommu_cap cap)
{
	struct zpci_dev *zdev = to_zpci_dev(dev);

	switch (cap) {
	case IOMMU_CAP_CACHE_COHERENCY:
		return true;
	case IOMMU_CAP_DEFERRED_FLUSH:
		return zdev->pft != PCI_FUNC_TYPE_ISM;
	default:
		return false;
	}
}

static struct iommu_domain *s390_domain_alloc_paging(struct device *dev)
{
	struct s390_domain *s390_domain;

	s390_domain = kzalloc(sizeof(*s390_domain), GFP_KERNEL);
	if (!s390_domain)
		return NULL;

	s390_domain->dma_table = dma_alloc_cpu_table(GFP_KERNEL);
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

static void s390_iommu_detach_device(struct iommu_domain *domain,
				     struct device *dev)
{
	struct s390_domain *s390_domain = to_s390_domain(domain);
	struct zpci_dev *zdev = to_zpci_dev(dev);
	unsigned long flags;

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
		s390_iommu_detach_device(&zdev->s390_domain->domain, dev);

	cc = zpci_register_ioat(zdev, 0, zdev->start_dma, zdev->end_dma,
				virt_to_phys(s390_domain->dma_table), &status);
	/*
	 * If the device is undergoing error recovery the reset code
	 * will re-establish the new domain.
	 */
	if (cc && status != ZPCI_PCI_ST_FUNC_NOT_AVAIL)
		return -EIO;

	zdev->dma_table = s390_domain->dma_table;
	zdev->s390_domain = s390_domain;

	spin_lock_irqsave(&s390_domain->list_lock, flags);
	list_add_rcu(&zdev->iommu_list, &s390_domain->devices);
	spin_unlock_irqrestore(&s390_domain->list_lock, flags);

	return 0;
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

	if (zdev->tlb_refresh)
		dev->iommu->shadow_on_flush = 1;

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
		s390_iommu_detach_device(&zdev->s390_domain->domain, dev);
}

static int zpci_refresh_all(struct zpci_dev *zdev)
{
	return zpci_refresh_trans((u64)zdev->fh << 32, zdev->start_dma,
				  zdev->end_dma - zdev->start_dma + 1);
}

static void s390_iommu_flush_iotlb_all(struct iommu_domain *domain)
{
	struct s390_domain *s390_domain = to_s390_domain(domain);
	struct zpci_dev *zdev;

	rcu_read_lock();
	list_for_each_entry_rcu(zdev, &s390_domain->devices, iommu_list) {
		atomic64_inc(&s390_domain->ctrs.global_rpcits);
		zpci_refresh_all(zdev);
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
		atomic64_inc(&s390_domain->ctrs.sync_rpcits);
		zpci_refresh_trans((u64)zdev->fh << 32, gather->start,
				   size);
	}
	rcu_read_unlock();
}

static int s390_iommu_iotlb_sync_map(struct iommu_domain *domain,
				     unsigned long iova, size_t size)
{
	struct s390_domain *s390_domain = to_s390_domain(domain);
	struct zpci_dev *zdev;
	int ret = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(zdev, &s390_domain->devices, iommu_list) {
		if (!zdev->tlb_refresh)
			continue;
		atomic64_inc(&s390_domain->ctrs.sync_map_rpcits);
		ret = zpci_refresh_trans((u64)zdev->fh << 32,
					 iova, size);
		/*
		 * let the hypervisor discover invalidated entries
		 * allowing it to free IOVAs and unpin pages
		 */
		if (ret == -ENOMEM) {
			ret = zpci_refresh_all(zdev);
			if (ret)
				break;
		}
	}
	rcu_read_unlock();

	return ret;
}

static int s390_iommu_validate_trans(struct s390_domain *s390_domain,
				     phys_addr_t pa, dma_addr_t dma_addr,
				     unsigned long nr_pages, int flags,
				     gfp_t gfp)
{
	phys_addr_t page_addr = pa & PAGE_MASK;
	unsigned long *entry;
	unsigned long i;
	int rc;

	for (i = 0; i < nr_pages; i++) {
		entry = dma_walk_cpu_trans(s390_domain->dma_table, dma_addr,
					   gfp);
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
					   dma_addr, gfp);
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
		entry = dma_walk_cpu_trans(s390_domain->dma_table, dma_addr,
					   GFP_ATOMIC);
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

	if (!(prot & IOMMU_WRITE))
		flags |= ZPCI_TABLE_PROTECTED;

	rc = s390_iommu_validate_trans(s390_domain, paddr, iova,
				     pgcount, flags, gfp);
	if (!rc) {
		*mapped = size;
		atomic64_add(pgcount, &s390_domain->ctrs.mapped_pages);
	}

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
	atomic64_add(pgcount, &s390_domain->ctrs.unmapped_pages);

	return size;
}

struct zpci_iommu_ctrs *zpci_get_iommu_ctrs(struct zpci_dev *zdev)
{
	if (!zdev || !zdev->s390_domain)
		return NULL;
	return &zdev->s390_domain->ctrs;
}

int zpci_init_iommu(struct zpci_dev *zdev)
{
	u64 aperture_size;
	int rc = 0;

	rc = iommu_device_sysfs_add(&zdev->iommu_dev, NULL, NULL,
				    "s390-iommu.%08x", zdev->fid);
	if (rc)
		goto out_err;

	rc = iommu_device_register(&zdev->iommu_dev, &s390_iommu_ops, NULL);
	if (rc)
		goto out_sysfs;

	zdev->start_dma = PAGE_ALIGN(zdev->start_dma);
	aperture_size = min3(s390_iommu_aperture,
			     ZPCI_TABLE_SIZE_RT - zdev->start_dma,
			     zdev->end_dma - zdev->start_dma + 1);
	zdev->end_dma = zdev->start_dma + aperture_size - 1;

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

static int __init s390_iommu_setup(char *str)
{
	if (!strcmp(str, "strict")) {
		pr_warn("s390_iommu=strict deprecated; use iommu.strict=1 instead\n");
		iommu_set_dma_strict();
	}
	return 1;
}

__setup("s390_iommu=", s390_iommu_setup);

static int __init s390_iommu_aperture_setup(char *str)
{
	if (kstrtou32(str, 10, &s390_iommu_aperture_factor))
		s390_iommu_aperture_factor = 1;
	return 1;
}

__setup("s390_iommu_aperture=", s390_iommu_aperture_setup);

static int __init s390_iommu_init(void)
{
	int rc;

	iommu_dma_forcedac = true;
	s390_iommu_aperture = (u64)virt_to_phys(high_memory);
	if (!s390_iommu_aperture_factor)
		s390_iommu_aperture = ULONG_MAX;
	else
		s390_iommu_aperture *= s390_iommu_aperture_factor;

	rc = dma_alloc_cpu_table_caches();
	if (rc)
		return rc;

	return rc;
}
subsys_initcall(s390_iommu_init);

static const struct iommu_ops s390_iommu_ops = {
	.capable = s390_iommu_capable,
	.domain_alloc_paging = s390_domain_alloc_paging,
	.probe_device = s390_iommu_probe_device,
	.release_device = s390_iommu_release_device,
	.device_group = generic_device_group,
	.pgsize_bitmap = SZ_4K,
	.get_resv_regions = s390_iommu_get_resv_regions,
	.default_domain_ops = &(const struct iommu_domain_ops) {
		.attach_dev	= s390_iommu_attach_device,
		.map_pages	= s390_iommu_map_pages,
		.unmap_pages	= s390_iommu_unmap_pages,
		.flush_iotlb_all = s390_iommu_flush_iotlb_all,
		.iotlb_sync      = s390_iommu_iotlb_sync,
		.iotlb_sync_map  = s390_iommu_iotlb_sync_map,
		.iova_to_phys	= s390_iommu_iova_to_phys,
		.free		= s390_domain_free,
	}
};
