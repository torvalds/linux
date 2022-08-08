// SPDX-License-Identifier: GPL-2.0-only
/*
 * IOMMU API for Graphics Address Relocation Table on Tegra20
 *
 * Copyright (c) 2010-2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author: Hiroshi DOYU <hdoyu@nvidia.com>
 */

#define dev_fmt(fmt)	"gart: " fmt

#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>

#include <soc/tegra/mc.h>

#define GART_REG_BASE		0x24
#define GART_CONFIG		(0x24 - GART_REG_BASE)
#define GART_ENTRY_ADDR		(0x28 - GART_REG_BASE)
#define GART_ENTRY_DATA		(0x2c - GART_REG_BASE)

#define GART_ENTRY_PHYS_ADDR_VALID	BIT(31)

#define GART_PAGE_SHIFT		12
#define GART_PAGE_SIZE		(1 << GART_PAGE_SHIFT)
#define GART_PAGE_MASK		GENMASK(30, GART_PAGE_SHIFT)

/* bitmap of the page sizes currently supported */
#define GART_IOMMU_PGSIZES	(GART_PAGE_SIZE)

struct gart_device {
	void __iomem		*regs;
	u32			*savedata;
	unsigned long		iovmm_base;	/* offset to vmm_area start */
	unsigned long		iovmm_end;	/* offset to vmm_area end */
	spinlock_t		pte_lock;	/* for pagetable */
	spinlock_t		dom_lock;	/* for active domain */
	unsigned int		active_devices;	/* number of active devices */
	struct iommu_domain	*active_domain;	/* current active domain */
	struct iommu_device	iommu;		/* IOMMU Core handle */
	struct device		*dev;
};

static struct gart_device *gart_handle; /* unique for a system */

static bool gart_debug;

/*
 * Any interaction between any block on PPSB and a block on APB or AHB
 * must have these read-back to ensure the APB/AHB bus transaction is
 * complete before initiating activity on the PPSB block.
 */
#define FLUSH_GART_REGS(gart)	readl_relaxed((gart)->regs + GART_CONFIG)

#define for_each_gart_pte(gart, iova)					\
	for (iova = gart->iovmm_base;					\
	     iova < gart->iovmm_end;					\
	     iova += GART_PAGE_SIZE)

static inline void gart_set_pte(struct gart_device *gart,
				unsigned long iova, unsigned long pte)
{
	writel_relaxed(iova, gart->regs + GART_ENTRY_ADDR);
	writel_relaxed(pte, gart->regs + GART_ENTRY_DATA);
}

static inline unsigned long gart_read_pte(struct gart_device *gart,
					  unsigned long iova)
{
	unsigned long pte;

	writel_relaxed(iova, gart->regs + GART_ENTRY_ADDR);
	pte = readl_relaxed(gart->regs + GART_ENTRY_DATA);

	return pte;
}

static void do_gart_setup(struct gart_device *gart, const u32 *data)
{
	unsigned long iova;

	for_each_gart_pte(gart, iova)
		gart_set_pte(gart, iova, data ? *(data++) : 0);

	writel_relaxed(1, gart->regs + GART_CONFIG);
	FLUSH_GART_REGS(gart);
}

static inline bool gart_iova_range_invalid(struct gart_device *gart,
					   unsigned long iova, size_t bytes)
{
	return unlikely(iova < gart->iovmm_base || bytes != GART_PAGE_SIZE ||
			iova + bytes > gart->iovmm_end);
}

static inline bool gart_pte_valid(struct gart_device *gart, unsigned long iova)
{
	return !!(gart_read_pte(gart, iova) & GART_ENTRY_PHYS_ADDR_VALID);
}

static int gart_iommu_attach_dev(struct iommu_domain *domain,
				 struct device *dev)
{
	struct gart_device *gart = gart_handle;
	int ret = 0;

	spin_lock(&gart->dom_lock);

	if (gart->active_domain && gart->active_domain != domain) {
		ret = -EBUSY;
	} else if (dev_iommu_priv_get(dev) != domain) {
		dev_iommu_priv_set(dev, domain);
		gart->active_domain = domain;
		gart->active_devices++;
	}

	spin_unlock(&gart->dom_lock);

	return ret;
}

static void gart_iommu_detach_dev(struct iommu_domain *domain,
				  struct device *dev)
{
	struct gart_device *gart = gart_handle;

	spin_lock(&gart->dom_lock);

	if (dev_iommu_priv_get(dev) == domain) {
		dev_iommu_priv_set(dev, NULL);

		if (--gart->active_devices == 0)
			gart->active_domain = NULL;
	}

	spin_unlock(&gart->dom_lock);
}

static struct iommu_domain *gart_iommu_domain_alloc(unsigned type)
{
	struct iommu_domain *domain;

	if (type != IOMMU_DOMAIN_UNMANAGED)
		return NULL;

	domain = kzalloc(sizeof(*domain), GFP_KERNEL);
	if (domain) {
		domain->geometry.aperture_start = gart_handle->iovmm_base;
		domain->geometry.aperture_end = gart_handle->iovmm_end - 1;
		domain->geometry.force_aperture = true;
	}

	return domain;
}

static void gart_iommu_domain_free(struct iommu_domain *domain)
{
	WARN_ON(gart_handle->active_domain == domain);
	kfree(domain);
}

static inline int __gart_iommu_map(struct gart_device *gart, unsigned long iova,
				   unsigned long pa)
{
	if (unlikely(gart_debug && gart_pte_valid(gart, iova))) {
		dev_err(gart->dev, "Page entry is in-use\n");
		return -EINVAL;
	}

	gart_set_pte(gart, iova, GART_ENTRY_PHYS_ADDR_VALID | pa);

	return 0;
}

static int gart_iommu_map(struct iommu_domain *domain, unsigned long iova,
			  phys_addr_t pa, size_t bytes, int prot, gfp_t gfp)
{
	struct gart_device *gart = gart_handle;
	int ret;

	if (gart_iova_range_invalid(gart, iova, bytes))
		return -EINVAL;

	spin_lock(&gart->pte_lock);
	ret = __gart_iommu_map(gart, iova, (unsigned long)pa);
	spin_unlock(&gart->pte_lock);

	return ret;
}

static inline int __gart_iommu_unmap(struct gart_device *gart,
				     unsigned long iova)
{
	if (unlikely(gart_debug && !gart_pte_valid(gart, iova))) {
		dev_err(gart->dev, "Page entry is invalid\n");
		return -EINVAL;
	}

	gart_set_pte(gart, iova, 0);

	return 0;
}

static size_t gart_iommu_unmap(struct iommu_domain *domain, unsigned long iova,
			       size_t bytes, struct iommu_iotlb_gather *gather)
{
	struct gart_device *gart = gart_handle;
	int err;

	if (gart_iova_range_invalid(gart, iova, bytes))
		return 0;

	spin_lock(&gart->pte_lock);
	err = __gart_iommu_unmap(gart, iova);
	spin_unlock(&gart->pte_lock);

	return err ? 0 : bytes;
}

static phys_addr_t gart_iommu_iova_to_phys(struct iommu_domain *domain,
					   dma_addr_t iova)
{
	struct gart_device *gart = gart_handle;
	unsigned long pte;

	if (gart_iova_range_invalid(gart, iova, GART_PAGE_SIZE))
		return -EINVAL;

	spin_lock(&gart->pte_lock);
	pte = gart_read_pte(gart, iova);
	spin_unlock(&gart->pte_lock);

	return pte & GART_PAGE_MASK;
}

static bool gart_iommu_capable(enum iommu_cap cap)
{
	return false;
}

static struct iommu_device *gart_iommu_probe_device(struct device *dev)
{
	if (!dev_iommu_fwspec_get(dev))
		return ERR_PTR(-ENODEV);

	return &gart_handle->iommu;
}

static void gart_iommu_release_device(struct device *dev)
{
}

static int gart_iommu_of_xlate(struct device *dev,
			       struct of_phandle_args *args)
{
	return 0;
}

static void gart_iommu_sync_map(struct iommu_domain *domain, unsigned long iova,
				size_t size)
{
	FLUSH_GART_REGS(gart_handle);
}

static void gart_iommu_sync(struct iommu_domain *domain,
			    struct iommu_iotlb_gather *gather)
{
	size_t length = gather->end - gather->start + 1;

	gart_iommu_sync_map(domain, gather->start, length);
}

static const struct iommu_ops gart_iommu_ops = {
	.capable	= gart_iommu_capable,
	.domain_alloc	= gart_iommu_domain_alloc,
	.domain_free	= gart_iommu_domain_free,
	.attach_dev	= gart_iommu_attach_dev,
	.detach_dev	= gart_iommu_detach_dev,
	.probe_device	= gart_iommu_probe_device,
	.release_device	= gart_iommu_release_device,
	.device_group	= generic_device_group,
	.map		= gart_iommu_map,
	.unmap		= gart_iommu_unmap,
	.iova_to_phys	= gart_iommu_iova_to_phys,
	.pgsize_bitmap	= GART_IOMMU_PGSIZES,
	.of_xlate	= gart_iommu_of_xlate,
	.iotlb_sync_map	= gart_iommu_sync_map,
	.iotlb_sync	= gart_iommu_sync,
};

int tegra_gart_suspend(struct gart_device *gart)
{
	u32 *data = gart->savedata;
	unsigned long iova;

	/*
	 * All GART users shall be suspended at this point. Disable
	 * address translation to trap all GART accesses as invalid
	 * memory accesses.
	 */
	writel_relaxed(0, gart->regs + GART_CONFIG);
	FLUSH_GART_REGS(gart);

	for_each_gart_pte(gart, iova)
		*(data++) = gart_read_pte(gart, iova);

	return 0;
}

int tegra_gart_resume(struct gart_device *gart)
{
	do_gart_setup(gart, gart->savedata);

	return 0;
}

struct gart_device *tegra_gart_probe(struct device *dev, struct tegra_mc *mc)
{
	struct gart_device *gart;
	struct resource *res;
	int err;

	BUILD_BUG_ON(PAGE_SHIFT != GART_PAGE_SHIFT);

	/* the GART memory aperture is required */
	res = platform_get_resource(to_platform_device(dev), IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(dev, "Memory aperture resource unavailable\n");
		return ERR_PTR(-ENXIO);
	}

	gart = kzalloc(sizeof(*gart), GFP_KERNEL);
	if (!gart)
		return ERR_PTR(-ENOMEM);

	gart_handle = gart;

	gart->dev = dev;
	gart->regs = mc->regs + GART_REG_BASE;
	gart->iovmm_base = res->start;
	gart->iovmm_end = res->end + 1;
	spin_lock_init(&gart->pte_lock);
	spin_lock_init(&gart->dom_lock);

	do_gart_setup(gart, NULL);

	err = iommu_device_sysfs_add(&gart->iommu, dev, NULL, "gart");
	if (err)
		goto free_gart;

	iommu_device_set_ops(&gart->iommu, &gart_iommu_ops);
	iommu_device_set_fwnode(&gart->iommu, dev->fwnode);

	err = iommu_device_register(&gart->iommu);
	if (err)
		goto remove_sysfs;

	gart->savedata = vmalloc(resource_size(res) / GART_PAGE_SIZE *
				 sizeof(u32));
	if (!gart->savedata) {
		err = -ENOMEM;
		goto unregister_iommu;
	}

	return gart;

unregister_iommu:
	iommu_device_unregister(&gart->iommu);
remove_sysfs:
	iommu_device_sysfs_remove(&gart->iommu);
free_gart:
	kfree(gart);

	return ERR_PTR(err);
}

module_param(gart_debug, bool, 0644);
MODULE_PARM_DESC(gart_debug, "Enable GART debugging");
