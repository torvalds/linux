// SPDX-License-Identifier: GPL-2.0-only
/*
 * IOMMU driver for Broadcom BCM2712
 *
 * Copyright (c) 2023-2025 Raspberry Pi Ltd.
 * Copyright (c) 2026 Daniel Drake
 *
 * Each BCM2712 IOMMU has multiple devices hardwired into it, whose
 * DMA transactions all route through the IOMMU. There is no stream ID tagging
 * or any other kind of segmentation to differentiate between requests from
 * different devices. It is also not possible to toggle a specific device
 * between iommu-mapped and bypass modes.
 *
 * The hardware supports the simultaneous configuration of a bypass window
 * (where memory accesses operate in identity mode) and a translation aperture
 * (supporting page mapping). While combining these two would allow for
 * GART-like operation, this driver's paging domain implementation configures
 * only the translation aperture (starting at IOVA 0) offering a degree of
 * memory protection.
 *
 * The page table format is a two-level format handled by generic_pt/bcm2712.
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/sizes.h>
#include <linux/generic_pt/iommu.h>

#include "bcm2712-iommu-cache.h"
#include "dma-iommu.h"
#include "iommu-pages.h"

/* BCM2712 IOMMU is organized around 4Kbyte pages */
#define IOMMU_PAGE_SHIFT       12
#define IOMMU_PAGE_SIZE        (1ul << IOMMU_PAGE_SHIFT)
/* A PTE is 4 bytes */
#define PTE_SIZE_SHIFT         2
/* L1/L2 table sizing (IOMMU hardware pages): 1024 entries per page */
#define PTES_PER_IOPG_SHIFT    (IOMMU_PAGE_SHIFT - PTE_SIZE_SHIFT)

#define MMMU_CTRL_OFFSET                       0x00
#define MMMU_CTRL_CAP_EXCEEDED                 BIT(27)
#define MMMU_CTRL_CAP_EXCEEDED_ABORT_EN        BIT(26)
#define MMMU_CTRL_CAP_EXCEEDED_INT_EN          BIT(25)
#define MMMU_CTRL_CAP_EXCEEDED_EXCEPTION_EN    BIT(24)
#define MMMU_CTRL_PT_INVALID                   BIT(20)
#define MMMU_CTRL_PT_INVALID_ABORT_EN          BIT(19)
#define MMMU_CTRL_PT_INVALID_INT_EN            BIT(18)
#define MMMU_CTRL_PT_INVALID_EXCEPTION_EN      BIT(17)
#define MMMU_CTRL_PT_INVALID_EN                BIT(16)
#define MMMU_CTRL_WRITE_VIOLATION              BIT(12)
#define MMMU_CTRL_WRITE_VIOLATION_ABORT_EN     BIT(11)
#define MMMU_CTRL_WRITE_VIOLATION_INT_EN       BIT(10)
#define MMMU_CTRL_WRITE_VIOLATION_EXCEPTION_EN BIT(9)
#define MMMU_CTRL_BYPASS                       BIT(8)
#define MMMU_CTRL_TLB_CLEARING                 BIT(7)
#define MMMU_CTRL_STATS_CLEAR                  BIT(3)
#define MMMU_CTRL_TLB_CLEAR                    BIT(2)
#define MMMU_CTRL_STATS_ENABLE                 BIT(1)
#define MMMU_CTRL_ENABLE                       BIT(0)

#define MMMU_CTRL_OPERATING_FLAGS (\
	MMMU_CTRL_CAP_EXCEEDED_ABORT_EN    | \
	MMMU_CTRL_PT_INVALID_ABORT_EN      | \
	MMMU_CTRL_PT_INVALID_EN            | \
	MMMU_CTRL_WRITE_VIOLATION_ABORT_EN | \
	MMMU_CTRL_STATS_ENABLE             | \
	MMMU_CTRL_ENABLE)

#define MMMU_PT_PA_BASE_OFFSET                 0x04

#define MMMU_ADDR_CAP_OFFSET                   0x14
#define MMMU_ADDR_CAP_ENABLE                   BIT(31)
#define ADDR_CAP_SHIFT                         ilog2(SZ_256M)

#define MMMU_SHOOT_DOWN_OFFSET                 0x18
#define MMMU_SHOOT_DOWN_SHOOTING               BIT(31)
#define MMMU_SHOOT_DOWN_SHOOT                  BIT(30)

#define MMMU_BYPASS_START_OFFSET               0x1c
#define MMMU_BYPASS_START_ENABLE               BIT(31)

#define MMMU_BYPASS_END_OFFSET                 0x20
#define MMMU_BYPASS_END_ENABLE                 BIT(31)

#define MMMU_MISC_OFFSET                       0x24
#define MMMU_MISC_SINGLE_TABLE                 BIT(31)

#define MMMU_ILLEGAL_ADR_OFFSET                0x30
#define MMMU_ILLEGAL_ADR_ENABLE                BIT(31)

#define MMMU_DEBUG_INFO_OFFSET                 0x38
#define MMMU_DEBUG_INFO_VERSION_MASK           0x0000000Fu
#define MMMU_DEBUG_INFO_VA_WIDTH_MASK          0x000000F0u
#define MMMU_DEBUG_INFO_PA_WIDTH_MASK          0x00000F00u
#define MMMU_DEBUG_INFO_BIGPAGE_WIDTH_MASK     0x000FF000u
#define MMMU_DEBUG_INFO_SUPERPAGE_WIDTH_MASK   0x0FF00000u
#define MMMU_DEBUG_INFO_BYPASS_4M              BIT(28)
#define MMMU_DEBUG_INFO_BYPASS                 BIT(29)

struct bcm2712_iommu {
	struct device *dev;
	struct iommu_device iommu;
	struct bcm2712_iommu_domain *domain;
	struct bcm2712_iommu_cache *cache;
	void __iomem *reg_base;
	spinlock_t hw_lock;
	size_t bigpage_size;
	size_t superpage_size;
};

struct bcm2712_iommu_domain {
	union {
		struct iommu_domain base;
		struct pt_iommu_bcm2712 pt;
	};
	struct bcm2712_iommu *mmu;
	void *default_page;
};

static inline void bcm2712_iommu_writel(struct bcm2712_iommu *mmu,
					unsigned int offset, u32 val)
{
	writel(val, mmu->reg_base + offset);
}

static inline u32 bcm2712_iommu_readl(struct bcm2712_iommu *mmu,
				      unsigned int offset)
{
	return readl(mmu->reg_base + offset);
}

static struct bcm2712_iommu_domain *
to_bcm2712_domain(struct iommu_domain *domain)
{
	return container_of(domain, struct bcm2712_iommu_domain, base);
}

static void bcm2712_iommu_init(struct bcm2712_iommu *mmu)
{
	unsigned int bigpage_width, superpage_width;
	u32 u = bcm2712_iommu_readl(mmu, MMMU_DEBUG_INFO_OFFSET);
	u32 pa_width = FIELD_GET(MMMU_DEBUG_INFO_PA_WIDTH_MASK, u);

	dev_dbg(mmu->dev, "DEBUG_INFO = 0x%08x\n", u);
	WARN_ON(FIELD_GET(MMMU_DEBUG_INFO_VERSION_MASK, u) < 4 ||
		FIELD_GET(MMMU_DEBUG_INFO_VA_WIDTH_MASK, u) < 6 ||
		pa_width < 6 || !(u & MMMU_DEBUG_INFO_BYPASS));

	dma_set_mask_and_coherent(mmu->dev, DMA_BIT_MASK(pa_width + 30u));

	bigpage_width = FIELD_GET(MMMU_DEBUG_INFO_BIGPAGE_WIDTH_MASK, u);
	if (bigpage_width)
		mmu->bigpage_size = IOMMU_PAGE_SIZE << bigpage_width;

	superpage_width = FIELD_GET(MMMU_DEBUG_INFO_SUPERPAGE_WIDTH_MASK, u);
	if (superpage_width)
		mmu->superpage_size = IOMMU_PAGE_SIZE << superpage_width;

	/* Disable MMU and clear sticky flags */
	bcm2712_iommu_writel(mmu, MMMU_CTRL_OFFSET,
			     MMMU_CTRL_CAP_EXCEEDED | MMMU_CTRL_PT_INVALID |
			     MMMU_CTRL_WRITE_VIOLATION |
			     MMMU_CTRL_STATS_CLEAR);

	/* Put MMU into 2-level mode */
	bcm2712_iommu_writel(mmu, MMMU_MISC_OFFSET,
			     bcm2712_iommu_readl(mmu, MMMU_MISC_OFFSET) &
				     ~MMMU_MISC_SINGLE_TABLE);
}

static int bcm2712_iommu_identity_attach(struct iommu_domain *identity_domain,
					 struct device *dev,
					 struct iommu_domain *old)
{
	struct bcm2712_iommu *mmu = dev_iommu_priv_get(dev);

	guard(spinlock_irqsave)(&mmu->hw_lock);
	bcm2712_iommu_writel(mmu, MMMU_CTRL_OFFSET, 0);
	mmu->domain = NULL;

	return 0;
}

static struct iommu_domain bcm2712_identity_domain = {
	.type = IOMMU_DOMAIN_IDENTITY,
	.ops = &(const struct iommu_domain_ops) {
		.attach_dev = bcm2712_iommu_identity_attach,
	},
};

static int bcm2712_iommu_clear_and_enable(struct bcm2712_iommu *mmu)
	__must_hold(&mmu->hw_lock)
{
	u32 val;

	bcm2712_iommu_cache_flush(mmu->cache);
	bcm2712_iommu_writel(mmu, MMMU_CTRL_OFFSET,
			     MMMU_CTRL_OPERATING_FLAGS | MMMU_CTRL_TLB_CLEAR);
	return readl_poll_timeout_atomic(mmu->reg_base + MMMU_CTRL_OFFSET, val,
					 !(val & MMMU_CTRL_TLB_CLEARING), 0,
					 50);
}


static int bcm2712_iommu_attach_dev(struct iommu_domain *domain,
				    struct device *dev,
				    struct iommu_domain *old)
{
	struct bcm2712_iommu *mmu = dev_iommu_priv_get(dev);
	struct bcm2712_iommu_domain *mydomain = to_bcm2712_domain(domain);
	struct pt_iommu_bcm2712_hw_info info;
	u32 default_page_pfn;
	int ret = 0;

	/* all attached devices must belong to the same IOMMU instance */
	if (mydomain->mmu != mmu)
		return -EINVAL;

	scoped_guard(spinlock_irqsave, &mmu->hw_lock) {
		if (mmu->domain == mydomain)
			break;

		/* Configure translation aperture */
		bcm2712_iommu_writel(mmu, MMMU_ADDR_CAP_OFFSET,
				     MMMU_ADDR_CAP_ENABLE |
					     (domain->geometry.aperture_end >>
					      ADDR_CAP_SHIFT));

		/*
		 * When the IOMMU handles a request, it adds the
		 * PT_PA_BASE_OFFSET to (IOVA>>32) to calculate the PFN of the
		 * corresponding L1 directory page.
		 * IOVA bits [31:22] are then used to fetch the L1 descriptor
		 * within (which in turn points to the L2 table).
		 * This clever logic would allow for a L1 table larger than 4kb
		 * (and hence a larger aperture).
		 */
		pt_iommu_bcm2712_hw_info(&mydomain->pt, &info);
		bcm2712_iommu_writel(mmu, MMMU_PT_PA_BASE_OFFSET,
				     info.pt_base >> IOMMU_PAGE_SHIFT);

		/* Disable bypass window */
		bcm2712_iommu_writel(mmu, MMMU_BYPASS_START_OFFSET, 0);
		bcm2712_iommu_writel(mmu, MMMU_BYPASS_END_OFFSET, 0);

		/*
		 * A default (error) page is used to catch illegal reads/writes.
		 */
		default_page_pfn = virt_to_phys(mydomain->default_page) >>
				   IOMMU_PAGE_SHIFT;
		bcm2712_iommu_writel(mmu, MMMU_ILLEGAL_ADR_OFFSET,
				     MMMU_ILLEGAL_ADR_ENABLE |
					     default_page_pfn);

		ret = bcm2712_iommu_clear_and_enable(mmu);
		if (ret) {
			bcm2712_iommu_writel(mmu, MMMU_CTRL_OFFSET, 0);
			mmu->domain = NULL;
			break;
		}

		mmu->domain = mydomain;
	}

	if (ret)
		dev_err_ratelimited(mmu->dev,
				    "TLB clear timed out during attach\n");

	return ret;
}

static int bcm2712_iommu_shootdown_range(struct bcm2712_iommu *mmu,
					 unsigned long iova, size_t size)
	__must_hold(&mmu->hw_lock)
{
	unsigned long start_pfn = iova >> IOMMU_PAGE_SHIFT;
	unsigned long end_pfn = (iova + size - 1) >> IOMMU_PAGE_SHIFT;
	unsigned long pfn;
	u32 val;
	int ret;

	/* Shootdown register deals with 4 pages at a time */
	for (pfn = round_down(start_pfn, 4); pfn <= end_pfn; pfn += 4) {
		bcm2712_iommu_writel(mmu, MMMU_SHOOT_DOWN_OFFSET,
				     MMMU_SHOOT_DOWN_SHOOT | pfn);
		ret = readl_poll_timeout_atomic(
			mmu->reg_base + MMMU_SHOOT_DOWN_OFFSET, val,
			!(val & MMMU_SHOOT_DOWN_SHOOTING), 0, 50);
		if (ret)
			return ret;
	}

	return 0;
}

static int bcm2712_iommu_sync_range(struct iommu_domain *domain,
				    unsigned long iova, size_t size)
{
	struct bcm2712_iommu_domain *mydomain = to_bcm2712_domain(domain);
	struct bcm2712_iommu *mmu = mydomain->mmu;
	int ret = 0;

	if (!size)
		return 0;

	scoped_guard(spinlock_irqsave, &mmu->hw_lock) {
		if (mmu->domain != mydomain)
			return 0;

		/* If invalidating more than 1MB, just do a full TLB clear */
		if (size >= SZ_1M) {
			ret = bcm2712_iommu_clear_and_enable(mmu);
		} else {
			bcm2712_iommu_cache_flush(mmu->cache);
			ret = bcm2712_iommu_shootdown_range(mmu, iova, size);
		}
	}

	if (ret)
		dev_err_ratelimited(mmu->dev,
				    "TLB sync timed out (size=%#zx)\n", size);

	return ret;
}

static void bcm2712_iommu_sync(struct iommu_domain *domain,
			       struct iommu_iotlb_gather *gather)
{
	bcm2712_iommu_sync_range(domain, gather->start,
				 gather->end - gather->start + 1);
	iommu_put_pages_list(&gather->freelist);
}

static int bcm2712_iommu_sync_map(struct iommu_domain *domain,
				  unsigned long iova, size_t size)
{
	return bcm2712_iommu_sync_range(domain, iova, size);
}

static void bcm2712_iommu_sync_all(struct iommu_domain *domain)
{
	size_t aperture_size = domain->geometry.aperture_end -
			       domain->geometry.aperture_start + 1;

	bcm2712_iommu_sync_range(domain, domain->geometry.aperture_start,
				 aperture_size);
}

static void bcm2712_iommu_domain_free(struct iommu_domain *domain)
{
	struct bcm2712_iommu_domain *mydomain = to_bcm2712_domain(domain);
	struct bcm2712_iommu *mmu = mydomain->mmu;

	scoped_guard(spinlock_irqsave, &mmu->hw_lock) {
		if (mmu->domain == mydomain) {
			bcm2712_iommu_writel(mmu, MMMU_CTRL_OFFSET, 0);
			mmu->domain = NULL;
		}
	}

	pt_iommu_deinit(&mydomain->pt.iommu);
	if (mydomain->default_page)
		iommu_free_pages(mydomain->default_page);
	kfree(mydomain);
}

static const struct iommu_domain_ops bcm2712_paging_domain_ops = {
	IOMMU_PT_DOMAIN_OPS(bcm2712),
	.attach_dev	 = bcm2712_iommu_attach_dev,
	.iotlb_sync      = bcm2712_iommu_sync,
	.iotlb_sync_map  = bcm2712_iommu_sync_map,
	.flush_iotlb_all = bcm2712_iommu_sync_all,
	.free		 = bcm2712_iommu_domain_free,
};

static struct iommu_domain *bcm2712_iommu_domain_alloc(struct device *dev)
{
	struct bcm2712_iommu *mmu = dev_iommu_priv_get(dev);
	struct bcm2712_iommu_domain *domain;
	struct pt_iommu_bcm2712_cfg cfg = {};
	int ret;

	domain = kzalloc_obj(*domain);
	if (!domain)
		return NULL;

	domain->mmu = mmu;
	domain->pt.iommu.iommu_device = mmu->dev;
	cfg.common.features = BIT(PT_FEAT_DMA_INCOHERENT);

	/* Bigpage and superpage sizes are typically 64K and 1M, but may vary */
	if (mmu->bigpage_size)
		cfg.bigpage_lg2 = ilog2(mmu->bigpage_size);
	if (mmu->superpage_size)
		cfg.superpage_lg2 = ilog2(mmu->superpage_size);

	/* 2-level format: 10-bit L1 + 10-bit L2 + 12-bit page offset */
	cfg.common.hw_max_vasz_lg2 =
		(2 * PTES_PER_IOPG_SHIFT) + IOMMU_PAGE_SHIFT;

	/* PTEs encode a 25-bit output address PFN */
	cfg.common.hw_max_oasz_lg2 = 25 + IOMMU_PAGE_SHIFT;

	ret = pt_iommu_bcm2712_init(&domain->pt, &cfg, GFP_KERNEL);
	if (ret)
		goto err;

	/* Set up a default (error) page used to catch illegal reads/writes */
	domain->default_page = iommu_alloc_pages_sz(GFP_KERNEL, PAGE_SIZE);
	if (!domain->default_page)
		goto err;

	domain->base.ops = &bcm2712_paging_domain_ops;
	return &domain->base;

err:
	bcm2712_iommu_domain_free(&domain->base);
	return NULL;
}

static struct bcm2712_iommu *
bcm2712_iommu_get_by_fwnode(struct fwnode_handle *fwnode)
{
	struct device *dev __free(put_device) =
		bus_find_device_by_fwnode(&platform_bus_type, fwnode);

	return dev ? dev_get_drvdata(dev) : NULL;
}

static struct iommu_device *bcm2712_iommu_probe_device(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct bcm2712_iommu *mmu;

	if (!fwspec || !fwspec->iommu_fwnode)
		return ERR_PTR(-ENODEV);

	mmu = bcm2712_iommu_get_by_fwnode(fwspec->iommu_fwnode);
	if (!mmu)
		return ERR_PTR(-ENODEV);

	dev_iommu_priv_set(dev, mmu);

	return &mmu->iommu;
}

static int bcm2712_iommu_of_xlate(struct device *dev,
				  const struct of_phandle_args *args)
{
	return iommu_fwspec_add_ids(dev, args->args, 0);
}

static const struct iommu_ops bcm2712_iommu_ops = {
	.identity_domain     = &bcm2712_identity_domain,
	.domain_alloc_paging = bcm2712_iommu_domain_alloc,
	.probe_device        = bcm2712_iommu_probe_device,
	.device_group        = generic_single_device_group,
	.get_resv_regions    = iommu_dma_get_resv_regions,
	.of_xlate            = bcm2712_iommu_of_xlate,
};

static const struct of_device_id bcm2712_iommu_of_match[] = {
	{ .compatible = "brcm,bcm2712-iommu" },
	{ /* sentinel */ }
};

static int bcm2712_iommu_init_cache(struct bcm2712_iommu *mmu,
				    struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	struct device_node *cache_np __free(device_node) =
		of_parse_phandle(dev->of_node, "brcm,iommu-cache", 0);
	if (!cache_np)
		return dev_err_probe(dev, -ENOENT,
				     "missing brcm,iommu-cache property\n");

	struct platform_device *cache_pdev __free(platform_device_put) =
		of_find_device_by_node(cache_np);
	if (!cache_pdev)
		return dev_err_probe(dev, -EPROBE_DEFER,
				     "waiting for cache device\n");

	mmu->cache = platform_get_drvdata(cache_pdev);
	if (!mmu->cache)
		return dev_err_probe(dev, -EPROBE_DEFER,
				     "waiting for cache driver probe\n");

	return 0;
}

static int bcm2712_iommu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bcm2712_iommu *mmu;
	int ret;

	mmu = devm_kzalloc(dev, sizeof(*mmu), GFP_KERNEL);
	if (!mmu)
		return -ENOMEM;

	mmu->dev = dev;
	spin_lock_init(&mmu->hw_lock);

	mmu->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mmu->reg_base))
		return PTR_ERR(mmu->reg_base);

	ret = bcm2712_iommu_init_cache(mmu, pdev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, mmu);
	bcm2712_iommu_init(mmu);

	ret = iommu_device_sysfs_add(&mmu->iommu, dev, NULL, "%s",
				     dev_name(dev));
	if (ret)
		return ret;

	ret = iommu_device_register(&mmu->iommu, &bcm2712_iommu_ops, dev);
	if (ret) {
		iommu_device_sysfs_remove(&mmu->iommu);
		return ret;
	}

	return 0;
}

static struct platform_driver bcm2712_iommu_driver = {
	.driver	= {
		.name		     = "bcm2712-iommu",
		.of_match_table	     = bcm2712_iommu_of_match,
		.suppress_bind_attrs = true,
	},
	.probe	= bcm2712_iommu_probe,
};
builtin_platform_driver(bcm2712_iommu_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Drake <dan@reactivated.net>");
MODULE_DESCRIPTION("Broadcom BCM2712 IOMMU driver");
