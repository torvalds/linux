/* linux/drivers/iommu/exynos_iommu.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifdef CONFIG_S5P_SYSTEM_MMU_DEBUG
#define DEBUG
#endif

#include <linux/io.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/iommu.h>
#include <linux/errno.h>
#include <linux/list.h>

#include <asm/cacheflush.h>

#include <plat/s5p-sysmmu.h>

#ifdef CONFIG_S5P_SYSTEM_MMU_DEBUG
#define DEBUG /* for dev_dbg() */
#endif

/* We does not consider super section mapping (16MB) */
#define S5P_SPAGE_SHIFT		12
#define S5P_LPAGE_SHIFT		16
#define S5P_SECTION_SHIFT	20

#define S5P_SPAGE_SIZE		(1 << S5P_SPAGE_SHIFT)
#define S5P_LPAGE_SIZE		(1 << S5P_LPAGE_SHIFT)
#define S5P_SECTION_SIZE	(1 << S5P_SECTION_SHIFT)

#define S5P_SPAGE_MASK		(~(S5P_SPAGE_SIZE - 1))
#define S5P_LPAGE_MASK		(~(S5P_LPAGE_SIZE - 1))
#define S5P_SECTION_MASK	(~(S5P_SECTION_SIZE - 1))

#define S5P_SPAGE_ORDER		(S5P_SPAGE_SHIFT - PAGE_SHIFT)
#define S5P_LPAGE_ORDER		(S5P_LPAGE_SHIFT - S5P_SPAGE_SHIFT)
#define S5P_SECTION_ORDER	(S5P_SECTION_SHIFT - S5P_SPAGE_SHIFT)

#define S5P_LV1TABLE_ENTRIES	(1 << (BITS_PER_LONG - S5P_SECTION_SHIFT))
#define S5P_LV1TABLE_ORDER	2 /* get_order(S5P_LV1TABLE_ENTRIES) */

#define S5P_LV2TABLE_ENTRIES	(1 << S5P_SECTION_ORDER)
#define S5P_LV2TABLE_SIZE	(S5P_LV2TABLE_ENTRIES * sizeof(long))
#define S5P_LV2TABLE_MASK	(~(S5P_LV2TABLE_SIZE - 1)) /* 0xFFFFFC00 */

#define S5P_SECTION_LV1_ENTRY(entry)	((entry & 0x40003) == 2)
#define S5P_SUPSECT_LV1_ENTRY(entry)	((entry & 0x40003) == 0x40002)
#define S5P_PAGE_LV1_ENTRY(entry)	((entry & 3) == 1)
#define S5P_FAULT_LV1_ENTRY(entry) (((entry & 3) == 0) || (entry & 3) == 3)

#define S5P_LPAGE_LV2_ENTRY(entry)	((entry & 3) == 1)
#define S5P_SPAGE_LV2_ENTRY(entry)	((entry & 2) == 2)
#define S5P_FAULT_LV2_ENTRY(entry)	((entry & 3) == 0)

#define MAKE_FAULT_ENTRY(entry)		do { entry = 0; } while (0)
#define MAKE_SECTION_ENTRY(entry, pa)	do { entry = pa | 2; } while (0)
#define MAKE_SUPSECT_ENTRY(entry, pa)	do { entry = pa | 0x40002; } while (0)
#define MAKE_LV2TABLE_ENTRY(entry, pa)	do { entry = pa | 1; } while (0)

#define MAKE_LPAGE_ENTRY(entry, pa)	do { entry = pa | 1; } while (0)
#define MAKE_SPAGE_ENTRY(entry, pa)	do { entry = pa | 3; } while (0)

#define GET_LV2ENTRY(entry, iova) (\
	(unsigned long *)phys_to_virt(entry & S5P_LV2TABLE_MASK) +\
	((iova & (~S5P_SECTION_MASK)) >> S5P_SPAGE_SHIFT))

struct s5p_iommu_domain {
	struct device *dev;
	unsigned long *pgtable;
	struct mutex lock;
};

/* slab cache for level 2 page tables */
static struct kmem_cache *l2table_cachep;

static inline void pgtable_flush(void *vastart, void *vaend)
{
	dmac_flush_range(vastart, vaend);
	outer_flush_range(virt_to_phys(vastart),
				virt_to_phys(vaend));
}

static int s5p_iommu_domain_init(struct iommu_domain *domain)
{
	struct s5p_iommu_domain *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->pgtable = (unsigned long *)__get_free_pages(GFP_KERNEL,
							S5P_LV1TABLE_ORDER);
	if (!priv->pgtable) {
		kfree(priv);
		return -ENOMEM;
	}

	memset(priv->pgtable, 0, S5P_LV1TABLE_ENTRIES * sizeof(unsigned long));
	pgtable_flush(priv->pgtable, priv->pgtable + S5P_LV1TABLE_ENTRIES);

	mutex_init(&priv->lock);

	domain->priv = priv;
	pr_debug("%s: Allocated IOMMU domain %p with pgtable @ %#lx\n",
			__func__, domain, __pa(priv->pgtable));
	return 0;
}

static void s5p_iommu_domain_destroy(struct iommu_domain *domain)
{
	struct s5p_iommu_domain *priv = domain->priv;

	free_pages((unsigned long)priv->pgtable, S5P_LV1TABLE_ORDER);
	kfree(domain->priv);
	domain->priv = NULL;
}

#ifdef CONFIG_DRM_EXYNOS_IOMMU
static int s5p_iommu_attach_device(struct iommu_domain *domain,
				   struct device *dev)
{
	int ret;
	struct s5p_iommu_domain *s5p_domain = domain->priv;
	struct sysmmu_drvdata *data = NULL;

	mutex_lock(&s5p_domain->lock);

	/*
	 * get sysmmu_drvdata to dev.
	 * owner device was set to sysmmu->platform_data at machine code.
	 */
	data = get_sysmmu_data(dev, data);
	if (!data)
		return -EFAULT;

	mutex_unlock(&s5p_domain->lock);

	ret = s5p_sysmmu_enable(dev, virt_to_phys(s5p_domain->pgtable));
	if (ret)
		return ret;

	return 0;
}

static void s5p_iommu_detach_device(struct iommu_domain *domain,
				    struct device *dev)
{
	struct sysmmu_drvdata *data = NULL;
	struct s5p_iommu_domain *s5p_domain = domain->priv;

	mutex_lock(&s5p_domain->lock);

	/*
	 * get sysmmu_drvdata to dev.
	 * owner device was set to sysmmu->platform_data at machine code.
	 */
	data = get_sysmmu_data(dev, data);
	if (!data) {
		dev_err(dev, "failed to detach device.\n");
		return;
	}

	s5p_sysmmu_disable(dev);

	mutex_unlock(&s5p_domain->lock);
}
#else
static int s5p_iommu_attach_device(struct iommu_domain *domain,
				   struct device *dev)
{
	int ret;
	struct s5p_iommu_domain *s5p_domain = domain->priv;

	if (s5p_domain->dev) {
		pr_debug("%s: %s is already attached to doamin %p\n", __func__,
				dev_name(s5p_domain->dev), domain);
		BUG_ON(s5p_domain->dev != dev);
		return -EBUSY;
	}

	ret = s5p_sysmmu_enable(dev, virt_to_phys(s5p_domain->pgtable));
	if (ret)
		return ret;

	mutex_lock(&s5p_domain->lock);
	s5p_domain->dev = dev;
	mutex_unlock(&s5p_domain->lock);

	return 0;
}

static void s5p_iommu_detach_device(struct iommu_domain *domain,
				    struct device *dev)
{
	struct s5p_iommu_domain *s5p_domain = domain->priv;

	mutex_lock(&s5p_domain->lock);

	if (s5p_domain->dev == dev) {
		mutex_unlock(&s5p_domain->lock);

		s5p_sysmmu_disable(s5p_domain->dev);

		s5p_domain->dev = NULL;
	} else {
		pr_debug("%s: %s is not attached to domain of pgtable @ %#lx\n",
			__func__, dev_name(dev), __pa(s5p_domain->pgtable));
		mutex_unlock(&s5p_domain->lock);
	}

}
#endif

static bool section_available(struct iommu_domain *domain,
			      unsigned long *lv1entry)
{
	struct s5p_iommu_domain *s5p_domain = domain->priv;

	if (S5P_SECTION_LV1_ENTRY(*lv1entry)) {
		pr_err("1MB entry alread exists at %#x // pgtable %#lx\n",
				(lv1entry - s5p_domain->pgtable) * SZ_1M,
				__pa(s5p_domain->pgtable));
		return false;
	}

	if (S5P_PAGE_LV1_ENTRY(*lv1entry)) {
		unsigned long *lv2end, *lv2base;

		lv2base = phys_to_virt(*lv1entry & S5P_LV2TABLE_MASK);
		lv2end = lv2base + S5P_LV2TABLE_ENTRIES;
		while (lv2base != lv2end) {
			if (!S5P_FAULT_LV2_ENTRY(*lv2base)) {
				pr_err("Failed to free L2 page table for"
					"section mapping. // pgtalle %#lx\n",
					__pa(s5p_domain->pgtable));
				return false;
			}
			lv2base++;
		}

		kmem_cache_free(l2table_cachep,
				phys_to_virt(*lv1entry & S5P_LV2TABLE_MASK));

		MAKE_FAULT_ENTRY(*lv1entry);
	}

	return true;
}

static bool write_lpage(unsigned long *head_entry, unsigned long phys_addr)
{
	unsigned long *entry, *end;

	entry = head_entry;
	end = entry + (1 << S5P_LPAGE_ORDER);

	while (entry != end) {
		if (!S5P_FAULT_LV2_ENTRY(*entry))
			break;

		MAKE_LPAGE_ENTRY(*entry, phys_addr);

		entry++;
	}

	if (entry != end) {
		end = entry;
		while (entry != head_entry)
			MAKE_FAULT_ENTRY(*(--entry));

		return false;
	}

	return true;
}

static int s5p_iommu_map(struct iommu_domain *domain, unsigned long iova,
			 phys_addr_t paddr, int gfp_order, int prot)
{
	struct s5p_iommu_domain *s5p_domain = domain->priv;
	unsigned long *start_entry, *entry, *end_entry;
	int num_entry;
	int ret = 0;

	BUG_ON(s5p_domain->pgtable== NULL);

	mutex_lock(&s5p_domain->lock);

	start_entry = entry = s5p_domain->pgtable + (iova >> S5P_SECTION_SHIFT);

	if (gfp_order >= S5P_SECTION_ORDER) {
		BUG_ON((paddr | iova) & ~S5P_SECTION_MASK);
		/* 1MiB mapping */

		num_entry = 1 << (gfp_order - S5P_SECTION_ORDER);
		end_entry = entry + num_entry;

		while (entry != end_entry) {
			if (!section_available(domain, entry))
				break;

			MAKE_SECTION_ENTRY(*entry, paddr);

			paddr += S5P_SECTION_SIZE;
			entry++;
		}

		if (entry != end_entry)
			goto mapping_error;

		pgtable_flush(start_entry, entry);
		goto mapping_done;
	}

	if (S5P_FAULT_LV1_ENTRY(*entry)) {
		unsigned long *l2table;

		l2table = kmem_cache_zalloc(l2table_cachep, GFP_KERNEL);
		if (!l2table) {
			ret = -ENOMEM;
			goto nomem_error;
		}

		pgtable_flush(l2table, l2table + S5P_LV2TABLE_ENTRIES);

		MAKE_LV2TABLE_ENTRY(*entry, virt_to_phys(l2table));
		pgtable_flush(entry, entry + 1);
	}

	/* 'entry' points level 2 entries, hereafter */
	entry = GET_LV2ENTRY(*entry, iova);

	start_entry = entry;
	num_entry = 1 << gfp_order;
	end_entry = entry + num_entry;

	if (gfp_order >= S5P_LPAGE_ORDER) {
		/* large page(64KiB) mapping */
		BUG_ON((paddr | iova) & ~S5P_LPAGE_MASK);

		while (entry != end_entry) {
			if (!write_lpage(entry, paddr)) {
				pr_err("%s: Failed to allocate large page"
						"for IOVA %#lx entry.\n",
						__func__, iova);
				ret = -EADDRINUSE;
				break;
			}

			paddr += S5P_LPAGE_SIZE;
			entry += (1 << S5P_LPAGE_ORDER);
		}

		if (entry != end_entry) {
			entry -= 1 << S5P_LPAGE_ORDER;
			goto mapping_error;
		}
	} else {
		/* page (4KiB) mapping */
		while (entry != end_entry && S5P_FAULT_LV2_ENTRY(*entry)) {

			MAKE_SPAGE_ENTRY(*entry, paddr);

			entry++;
			paddr += S5P_SPAGE_SIZE;
		}

		if (entry != end_entry) {
			pr_err("%s: Failed to allocate small page entry"
					" for IOVA %#lx.\n", __func__, iova);
			ret = -EADDRINUSE;

			goto mapping_error;
		}
	}

	pgtable_flush(start_entry, entry);
mapping_error:
	if (entry != end_entry) {
		unsigned long *current_entry = entry;
		while (entry != start_entry)
			MAKE_FAULT_ENTRY(*(--entry));
		pgtable_flush(start_entry, current_entry);
		ret = -EADDRINUSE;
	}

nomem_error:
mapping_done:
	mutex_unlock(&s5p_domain->lock);

	return ret;
}

#ifdef CONFIG_DRM_EXYNOS_IOMMU
static int s5p_iommu_unmap(struct iommu_domain *domain, unsigned long iova,
			   int gfp_order)
{
	struct s5p_iommu_domain *s5p_domain = domain->priv;
	struct sysmmu_drvdata *data;
	struct list_head *sysmmu_list, *pos;
	unsigned long *entry;
	int num_entry;

	BUG_ON(s5p_domain->pgtable == NULL);

	mutex_lock(&s5p_domain->lock);

	entry = s5p_domain->pgtable + (iova >> S5P_SECTION_SHIFT);

	if (gfp_order >= S5P_SECTION_ORDER) {
		num_entry = 1 << (gfp_order - S5P_SECTION_ORDER);
		while (num_entry--) {
			if (S5P_SECTION_LV1_ENTRY(*entry)) {
				MAKE_FAULT_ENTRY(*entry);
			} else if (S5P_PAGE_LV1_ENTRY(*entry)) {
				unsigned long *lv2beg, *lv2end;
				lv2beg = phys_to_virt(
						*entry & S5P_LV2TABLE_MASK);
				lv2end = lv2beg + S5P_LV2TABLE_ENTRIES;
				while (lv2beg != lv2end) {
					MAKE_FAULT_ENTRY(*lv2beg);
					lv2beg++;
				}
			}
			entry++;
		}
	} else {
		entry = GET_LV2ENTRY(*entry, iova);

		BUG_ON(S5P_LPAGE_LV2_ENTRY(*entry) &&
						(gfp_order < S5P_LPAGE_ORDER));

		num_entry = 1 << gfp_order;

		while (num_entry--) {
			MAKE_FAULT_ENTRY(*entry);
			entry++;
		}
	}

	sysmmu_list = get_sysmmu_list();

	/*
	 * invalidate tlb entries to iova(device address) to each iommu
	 * registered in sysmmu_list.
	 *
	 * P.S. a device using iommu was set to data->owner at machine code
	 * and enabled iommu was added in sysmmu_list at sysmmu probe
	 */
	list_for_each(pos, sysmmu_list) {
		unsigned int page_size, count;

		/*
		 * get entry count and page size to device address space
		 * mapped with iommu page table and invalidate each entry.
		 */
		if (gfp_order >= S5P_SECTION_ORDER) {
			count = 1 << (gfp_order - S5P_SECTION_ORDER);
			page_size = S5P_SECTION_SIZE;
		} else if (gfp_order >= S5P_LPAGE_ORDER) {
			count = 1 << (gfp_order - S5P_LPAGE_ORDER);
			page_size = S5P_LPAGE_SIZE;
		} else {
			count = 1 << (gfp_order - S5P_SPAGE_ORDER);
			page_size = S5P_SPAGE_SIZE;
		}

		data = list_entry(pos, struct sysmmu_drvdata, node);
		if (data)
			s5p_sysmmu_tlb_invalidate_entry(data->owner, iova,
							count, page_size);
	}

	mutex_unlock(&s5p_domain->lock);

	return 0;
}
#else

static int s5p_iommu_unmap(struct iommu_domain *domain, unsigned long iova,
			   int gfp_order)
{
	struct s5p_iommu_domain *s5p_domain = domain->priv;
	unsigned long *entry;
	int num_entry;

	BUG_ON(s5p_domain->pgtable == NULL);

	mutex_lock(&s5p_domain->lock);

	entry = s5p_domain->pgtable + (iova >> S5P_SECTION_SHIFT);

	if (gfp_order >= S5P_SECTION_ORDER) {
		num_entry = 1 << (gfp_order - S5P_SECTION_ORDER);
		while (num_entry--) {
			if (S5P_SECTION_LV1_ENTRY(*entry)) {
				MAKE_FAULT_ENTRY(*entry);
			} else if (S5P_PAGE_LV1_ENTRY(*entry)) {
				unsigned long *lv2beg, *lv2end;
				lv2beg = phys_to_virt(
						*entry & S5P_LV2TABLE_MASK);
				lv2end = lv2beg + S5P_LV2TABLE_ENTRIES;
				while (lv2beg != lv2end) {
					MAKE_FAULT_ENTRY(*lv2beg);
					lv2beg++;
				}
			}
			entry++;
		}
	} else {
		entry = GET_LV2ENTRY(*entry, iova);

		BUG_ON(S5P_LPAGE_LV2_ENTRY(*entry) &&
						(gfp_order < S5P_LPAGE_ORDER));

		num_entry = 1 << gfp_order;

		while (num_entry--) {
			MAKE_FAULT_ENTRY(*entry);
			entry++;
		}
	}

	mutex_unlock(&s5p_domain->lock);

	if (s5p_domain->dev)
		s5p_sysmmu_tlb_invalidate(s5p_domain->dev);

	return 0;
}
#endif

static phys_addr_t s5p_iommu_iova_to_phys(struct iommu_domain *domain,
					  unsigned long iova)
{
	struct s5p_iommu_domain *s5p_domain = domain->priv;
	unsigned long *entry;
	unsigned long offset;

	entry = s5p_domain->pgtable + (iova >> S5P_SECTION_SHIFT);

	if (S5P_FAULT_LV1_ENTRY(*entry))
		return 0;

	offset = iova & ~S5P_SECTION_MASK;

	if (S5P_SECTION_LV1_ENTRY(*entry))
		return (*entry & S5P_SECTION_MASK) + offset;

	entry = GET_LV2ENTRY(*entry, iova);

	if (S5P_SPAGE_LV2_ENTRY(*entry))
		return (*entry & S5P_SPAGE_MASK) + (iova & ~S5P_SPAGE_MASK);

	if (S5P_LPAGE_LV2_ENTRY(*entry))
		return (*entry & S5P_LPAGE_MASK) + (iova & ~S5P_LPAGE_MASK);

	return 0;
}

static int s5p_iommu_domain_has_cap(struct iommu_domain *domain,
				    unsigned long cap)
{
	return 0;
}

static struct iommu_ops s5p_iommu_ops = {
	.domain_init = &s5p_iommu_domain_init,
	.domain_destroy = &s5p_iommu_domain_destroy,
	.attach_dev = &s5p_iommu_attach_device,
	.detach_dev = &s5p_iommu_detach_device,
	.map = &s5p_iommu_map,
	.unmap = &s5p_iommu_unmap,
	.iova_to_phys = &s5p_iommu_iova_to_phys,
	.domain_has_cap = &s5p_iommu_domain_has_cap,
};

static int __init s5p_iommu_init(void)
{
	l2table_cachep = kmem_cache_create("SysMMU Lv2 Tables",
				S5P_LV2TABLE_SIZE, S5P_LV2TABLE_SIZE, 0, NULL);
	if (!l2table_cachep)
		return -ENOMEM;

	register_iommu(&s5p_iommu_ops);
	return 0;
}
arch_initcall(s5p_iommu_init);
