// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011-2014 NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/iommu.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>

#include <soc/tegra/ahb.h>
#include <soc/tegra/mc.h>

struct tegra_smmu_group {
	struct list_head list;
	struct tegra_smmu *smmu;
	const struct tegra_smmu_group_soc *soc;
	struct iommu_group *group;
	unsigned int swgroup;
};

struct tegra_smmu {
	void __iomem *regs;
	struct device *dev;

	struct tegra_mc *mc;
	const struct tegra_smmu_soc *soc;

	struct list_head groups;

	unsigned long pfn_mask;
	unsigned long tlb_mask;

	unsigned long *asids;
	struct mutex lock;

	struct list_head list;

	struct dentry *debugfs;

	struct iommu_device iommu;	/* IOMMU Core code handle */
};

struct tegra_smmu_as {
	struct iommu_domain domain;
	struct tegra_smmu *smmu;
	unsigned int use_count;
	spinlock_t lock;
	u32 *count;
	struct page **pts;
	struct page *pd;
	dma_addr_t pd_dma;
	unsigned id;
	u32 attr;
};

static struct tegra_smmu_as *to_smmu_as(struct iommu_domain *dom)
{
	return container_of(dom, struct tegra_smmu_as, domain);
}

static inline void smmu_writel(struct tegra_smmu *smmu, u32 value,
			       unsigned long offset)
{
	writel(value, smmu->regs + offset);
}

static inline u32 smmu_readl(struct tegra_smmu *smmu, unsigned long offset)
{
	return readl(smmu->regs + offset);
}

#define SMMU_CONFIG 0x010
#define  SMMU_CONFIG_ENABLE (1 << 0)

#define SMMU_TLB_CONFIG 0x14
#define  SMMU_TLB_CONFIG_HIT_UNDER_MISS (1 << 29)
#define  SMMU_TLB_CONFIG_ROUND_ROBIN_ARBITRATION (1 << 28)
#define  SMMU_TLB_CONFIG_ACTIVE_LINES(smmu) \
	((smmu)->soc->num_tlb_lines & (smmu)->tlb_mask)

#define SMMU_PTC_CONFIG 0x18
#define  SMMU_PTC_CONFIG_ENABLE (1 << 29)
#define  SMMU_PTC_CONFIG_REQ_LIMIT(x) (((x) & 0x0f) << 24)
#define  SMMU_PTC_CONFIG_INDEX_MAP(x) ((x) & 0x3f)

#define SMMU_PTB_ASID 0x01c
#define  SMMU_PTB_ASID_VALUE(x) ((x) & 0x7f)

#define SMMU_PTB_DATA 0x020
#define  SMMU_PTB_DATA_VALUE(dma, attr) ((dma) >> 12 | (attr))

#define SMMU_MK_PDE(dma, attr) ((dma) >> SMMU_PTE_SHIFT | (attr))

#define SMMU_TLB_FLUSH 0x030
#define  SMMU_TLB_FLUSH_VA_MATCH_ALL     (0 << 0)
#define  SMMU_TLB_FLUSH_VA_MATCH_SECTION (2 << 0)
#define  SMMU_TLB_FLUSH_VA_MATCH_GROUP   (3 << 0)
#define  SMMU_TLB_FLUSH_VA_SECTION(addr) ((((addr) & 0xffc00000) >> 12) | \
					  SMMU_TLB_FLUSH_VA_MATCH_SECTION)
#define  SMMU_TLB_FLUSH_VA_GROUP(addr)   ((((addr) & 0xffffc000) >> 12) | \
					  SMMU_TLB_FLUSH_VA_MATCH_GROUP)
#define  SMMU_TLB_FLUSH_ASID_MATCH       (1 << 31)

#define SMMU_PTC_FLUSH 0x034
#define  SMMU_PTC_FLUSH_TYPE_ALL (0 << 0)
#define  SMMU_PTC_FLUSH_TYPE_ADR (1 << 0)

#define SMMU_PTC_FLUSH_HI 0x9b8
#define  SMMU_PTC_FLUSH_HI_MASK 0x3

/* per-SWGROUP SMMU_*_ASID register */
#define SMMU_ASID_ENABLE (1 << 31)
#define SMMU_ASID_MASK 0x7f
#define SMMU_ASID_VALUE(x) ((x) & SMMU_ASID_MASK)

/* page table definitions */
#define SMMU_NUM_PDE 1024
#define SMMU_NUM_PTE 1024

#define SMMU_SIZE_PD (SMMU_NUM_PDE * 4)
#define SMMU_SIZE_PT (SMMU_NUM_PTE * 4)

#define SMMU_PDE_SHIFT 22
#define SMMU_PTE_SHIFT 12

#define SMMU_PAGE_MASK		(~(SMMU_SIZE_PT-1))
#define SMMU_OFFSET_IN_PAGE(x)	((unsigned long)(x) & ~SMMU_PAGE_MASK)
#define SMMU_PFN_PHYS(x)	((phys_addr_t)(x) << SMMU_PTE_SHIFT)
#define SMMU_PHYS_PFN(x)	((unsigned long)((x) >> SMMU_PTE_SHIFT))

#define SMMU_PD_READABLE	(1 << 31)
#define SMMU_PD_WRITABLE	(1 << 30)
#define SMMU_PD_NONSECURE	(1 << 29)

#define SMMU_PDE_READABLE	(1 << 31)
#define SMMU_PDE_WRITABLE	(1 << 30)
#define SMMU_PDE_NONSECURE	(1 << 29)
#define SMMU_PDE_NEXT		(1 << 28)

#define SMMU_PTE_READABLE	(1 << 31)
#define SMMU_PTE_WRITABLE	(1 << 30)
#define SMMU_PTE_NONSECURE	(1 << 29)

#define SMMU_PDE_ATTR		(SMMU_PDE_READABLE | SMMU_PDE_WRITABLE | \
				 SMMU_PDE_NONSECURE)

static unsigned int iova_pd_index(unsigned long iova)
{
	return (iova >> SMMU_PDE_SHIFT) & (SMMU_NUM_PDE - 1);
}

static unsigned int iova_pt_index(unsigned long iova)
{
	return (iova >> SMMU_PTE_SHIFT) & (SMMU_NUM_PTE - 1);
}

static bool smmu_dma_addr_valid(struct tegra_smmu *smmu, dma_addr_t addr)
{
	addr >>= 12;
	return (addr & smmu->pfn_mask) == addr;
}

static dma_addr_t smmu_pde_to_dma(struct tegra_smmu *smmu, u32 pde)
{
	return (dma_addr_t)(pde & smmu->pfn_mask) << 12;
}

static void smmu_flush_ptc_all(struct tegra_smmu *smmu)
{
	smmu_writel(smmu, SMMU_PTC_FLUSH_TYPE_ALL, SMMU_PTC_FLUSH);
}

static inline void smmu_flush_ptc(struct tegra_smmu *smmu, dma_addr_t dma,
				  unsigned long offset)
{
	u32 value;

	offset &= ~(smmu->mc->soc->atom_size - 1);

	if (smmu->mc->soc->num_address_bits > 32) {
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
		value = (dma >> 32) & SMMU_PTC_FLUSH_HI_MASK;
#else
		value = 0;
#endif
		smmu_writel(smmu, value, SMMU_PTC_FLUSH_HI);
	}

	value = (dma + offset) | SMMU_PTC_FLUSH_TYPE_ADR;
	smmu_writel(smmu, value, SMMU_PTC_FLUSH);
}

static inline void smmu_flush_tlb(struct tegra_smmu *smmu)
{
	smmu_writel(smmu, SMMU_TLB_FLUSH_VA_MATCH_ALL, SMMU_TLB_FLUSH);
}

static inline void smmu_flush_tlb_asid(struct tegra_smmu *smmu,
				       unsigned long asid)
{
	u32 value;

	if (smmu->soc->num_asids == 4)
		value = (asid & 0x3) << 29;
	else
		value = (asid & 0x7f) << 24;

	value |= SMMU_TLB_FLUSH_ASID_MATCH | SMMU_TLB_FLUSH_VA_MATCH_ALL;
	smmu_writel(smmu, value, SMMU_TLB_FLUSH);
}

static inline void smmu_flush_tlb_section(struct tegra_smmu *smmu,
					  unsigned long asid,
					  unsigned long iova)
{
	u32 value;

	if (smmu->soc->num_asids == 4)
		value = (asid & 0x3) << 29;
	else
		value = (asid & 0x7f) << 24;

	value |= SMMU_TLB_FLUSH_ASID_MATCH | SMMU_TLB_FLUSH_VA_SECTION(iova);
	smmu_writel(smmu, value, SMMU_TLB_FLUSH);
}

static inline void smmu_flush_tlb_group(struct tegra_smmu *smmu,
					unsigned long asid,
					unsigned long iova)
{
	u32 value;

	if (smmu->soc->num_asids == 4)
		value = (asid & 0x3) << 29;
	else
		value = (asid & 0x7f) << 24;

	value |= SMMU_TLB_FLUSH_ASID_MATCH | SMMU_TLB_FLUSH_VA_GROUP(iova);
	smmu_writel(smmu, value, SMMU_TLB_FLUSH);
}

static inline void smmu_flush(struct tegra_smmu *smmu)
{
	smmu_readl(smmu, SMMU_PTB_ASID);
}

static int tegra_smmu_alloc_asid(struct tegra_smmu *smmu, unsigned int *idp)
{
	unsigned long id;

	id = find_first_zero_bit(smmu->asids, smmu->soc->num_asids);
	if (id >= smmu->soc->num_asids)
		return -ENOSPC;

	set_bit(id, smmu->asids);
	*idp = id;

	return 0;
}

static void tegra_smmu_free_asid(struct tegra_smmu *smmu, unsigned int id)
{
	clear_bit(id, smmu->asids);
}

static struct iommu_domain *tegra_smmu_domain_alloc(unsigned type)
{
	struct tegra_smmu_as *as;

	if (type != IOMMU_DOMAIN_UNMANAGED)
		return NULL;

	as = kzalloc(sizeof(*as), GFP_KERNEL);
	if (!as)
		return NULL;

	as->attr = SMMU_PD_READABLE | SMMU_PD_WRITABLE | SMMU_PD_NONSECURE;

	as->pd = alloc_page(GFP_KERNEL | __GFP_DMA | __GFP_ZERO);
	if (!as->pd) {
		kfree(as);
		return NULL;
	}

	as->count = kcalloc(SMMU_NUM_PDE, sizeof(u32), GFP_KERNEL);
	if (!as->count) {
		__free_page(as->pd);
		kfree(as);
		return NULL;
	}

	as->pts = kcalloc(SMMU_NUM_PDE, sizeof(*as->pts), GFP_KERNEL);
	if (!as->pts) {
		kfree(as->count);
		__free_page(as->pd);
		kfree(as);
		return NULL;
	}

	spin_lock_init(&as->lock);

	/* setup aperture */
	as->domain.geometry.aperture_start = 0;
	as->domain.geometry.aperture_end = 0xffffffff;
	as->domain.geometry.force_aperture = true;

	return &as->domain;
}

static void tegra_smmu_domain_free(struct iommu_domain *domain)
{
	struct tegra_smmu_as *as = to_smmu_as(domain);

	/* TODO: free page directory and page tables */

	WARN_ON_ONCE(as->use_count);
	kfree(as->count);
	kfree(as->pts);
	kfree(as);
}

static const struct tegra_smmu_swgroup *
tegra_smmu_find_swgroup(struct tegra_smmu *smmu, unsigned int swgroup)
{
	const struct tegra_smmu_swgroup *group = NULL;
	unsigned int i;

	for (i = 0; i < smmu->soc->num_swgroups; i++) {
		if (smmu->soc->swgroups[i].swgroup == swgroup) {
			group = &smmu->soc->swgroups[i];
			break;
		}
	}

	return group;
}

static void tegra_smmu_enable(struct tegra_smmu *smmu, unsigned int swgroup,
			      unsigned int asid)
{
	const struct tegra_smmu_swgroup *group;
	unsigned int i;
	u32 value;

	group = tegra_smmu_find_swgroup(smmu, swgroup);
	if (group) {
		value = smmu_readl(smmu, group->reg);
		value &= ~SMMU_ASID_MASK;
		value |= SMMU_ASID_VALUE(asid);
		value |= SMMU_ASID_ENABLE;
		smmu_writel(smmu, value, group->reg);
	} else {
		pr_warn("%s group from swgroup %u not found\n", __func__,
				swgroup);
		/* No point moving ahead if group was not found */
		return;
	}

	for (i = 0; i < smmu->soc->num_clients; i++) {
		const struct tegra_mc_client *client = &smmu->soc->clients[i];

		if (client->swgroup != swgroup)
			continue;

		value = smmu_readl(smmu, client->regs.smmu.reg);
		value |= BIT(client->regs.smmu.bit);
		smmu_writel(smmu, value, client->regs.smmu.reg);
	}
}

static void tegra_smmu_disable(struct tegra_smmu *smmu, unsigned int swgroup,
			       unsigned int asid)
{
	const struct tegra_smmu_swgroup *group;
	unsigned int i;
	u32 value;

	group = tegra_smmu_find_swgroup(smmu, swgroup);
	if (group) {
		value = smmu_readl(smmu, group->reg);
		value &= ~SMMU_ASID_MASK;
		value |= SMMU_ASID_VALUE(asid);
		value &= ~SMMU_ASID_ENABLE;
		smmu_writel(smmu, value, group->reg);
	}

	for (i = 0; i < smmu->soc->num_clients; i++) {
		const struct tegra_mc_client *client = &smmu->soc->clients[i];

		if (client->swgroup != swgroup)
			continue;

		value = smmu_readl(smmu, client->regs.smmu.reg);
		value &= ~BIT(client->regs.smmu.bit);
		smmu_writel(smmu, value, client->regs.smmu.reg);
	}
}

static int tegra_smmu_as_prepare(struct tegra_smmu *smmu,
				 struct tegra_smmu_as *as)
{
	u32 value;
	int err = 0;

	mutex_lock(&smmu->lock);

	if (as->use_count > 0) {
		as->use_count++;
		goto unlock;
	}

	as->pd_dma = dma_map_page(smmu->dev, as->pd, 0, SMMU_SIZE_PD,
				  DMA_TO_DEVICE);
	if (dma_mapping_error(smmu->dev, as->pd_dma)) {
		err = -ENOMEM;
		goto unlock;
	}

	/* We can't handle 64-bit DMA addresses */
	if (!smmu_dma_addr_valid(smmu, as->pd_dma)) {
		err = -ENOMEM;
		goto err_unmap;
	}

	err = tegra_smmu_alloc_asid(smmu, &as->id);
	if (err < 0)
		goto err_unmap;

	smmu_flush_ptc(smmu, as->pd_dma, 0);
	smmu_flush_tlb_asid(smmu, as->id);

	smmu_writel(smmu, as->id & 0x7f, SMMU_PTB_ASID);
	value = SMMU_PTB_DATA_VALUE(as->pd_dma, as->attr);
	smmu_writel(smmu, value, SMMU_PTB_DATA);
	smmu_flush(smmu);

	as->smmu = smmu;
	as->use_count++;

	mutex_unlock(&smmu->lock);

	return 0;

err_unmap:
	dma_unmap_page(smmu->dev, as->pd_dma, SMMU_SIZE_PD, DMA_TO_DEVICE);
unlock:
	mutex_unlock(&smmu->lock);

	return err;
}

static void tegra_smmu_as_unprepare(struct tegra_smmu *smmu,
				    struct tegra_smmu_as *as)
{
	mutex_lock(&smmu->lock);

	if (--as->use_count > 0) {
		mutex_unlock(&smmu->lock);
		return;
	}

	tegra_smmu_free_asid(smmu, as->id);

	dma_unmap_page(smmu->dev, as->pd_dma, SMMU_SIZE_PD, DMA_TO_DEVICE);

	as->smmu = NULL;

	mutex_unlock(&smmu->lock);
}

static int tegra_smmu_attach_dev(struct iommu_domain *domain,
				 struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct tegra_smmu *smmu = dev_iommu_priv_get(dev);
	struct tegra_smmu_as *as = to_smmu_as(domain);
	unsigned int index;
	int err;

	if (!fwspec)
		return -ENOENT;

	for (index = 0; index < fwspec->num_ids; index++) {
		err = tegra_smmu_as_prepare(smmu, as);
		if (err)
			goto disable;

		tegra_smmu_enable(smmu, fwspec->ids[index], as->id);
	}

	if (index == 0)
		return -ENODEV;

	return 0;

disable:
	while (index--) {
		tegra_smmu_disable(smmu, fwspec->ids[index], as->id);
		tegra_smmu_as_unprepare(smmu, as);
	}

	return err;
}

static void tegra_smmu_detach_dev(struct iommu_domain *domain, struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct tegra_smmu_as *as = to_smmu_as(domain);
	struct tegra_smmu *smmu = as->smmu;
	unsigned int index;

	if (!fwspec)
		return;

	for (index = 0; index < fwspec->num_ids; index++) {
		tegra_smmu_disable(smmu, fwspec->ids[index], as->id);
		tegra_smmu_as_unprepare(smmu, as);
	}
}

static void tegra_smmu_set_pde(struct tegra_smmu_as *as, unsigned long iova,
			       u32 value)
{
	unsigned int pd_index = iova_pd_index(iova);
	struct tegra_smmu *smmu = as->smmu;
	u32 *pd = page_address(as->pd);
	unsigned long offset = pd_index * sizeof(*pd);

	/* Set the page directory entry first */
	pd[pd_index] = value;

	/* The flush the page directory entry from caches */
	dma_sync_single_range_for_device(smmu->dev, as->pd_dma, offset,
					 sizeof(*pd), DMA_TO_DEVICE);

	/* And flush the iommu */
	smmu_flush_ptc(smmu, as->pd_dma, offset);
	smmu_flush_tlb_section(smmu, as->id, iova);
	smmu_flush(smmu);
}

static u32 *tegra_smmu_pte_offset(struct page *pt_page, unsigned long iova)
{
	u32 *pt = page_address(pt_page);

	return pt + iova_pt_index(iova);
}

static u32 *tegra_smmu_pte_lookup(struct tegra_smmu_as *as, unsigned long iova,
				  dma_addr_t *dmap)
{
	unsigned int pd_index = iova_pd_index(iova);
	struct tegra_smmu *smmu = as->smmu;
	struct page *pt_page;
	u32 *pd;

	pt_page = as->pts[pd_index];
	if (!pt_page)
		return NULL;

	pd = page_address(as->pd);
	*dmap = smmu_pde_to_dma(smmu, pd[pd_index]);

	return tegra_smmu_pte_offset(pt_page, iova);
}

static u32 *as_get_pte(struct tegra_smmu_as *as, dma_addr_t iova,
		       dma_addr_t *dmap, struct page *page)
{
	unsigned int pde = iova_pd_index(iova);
	struct tegra_smmu *smmu = as->smmu;

	if (!as->pts[pde]) {
		dma_addr_t dma;

		dma = dma_map_page(smmu->dev, page, 0, SMMU_SIZE_PT,
				   DMA_TO_DEVICE);
		if (dma_mapping_error(smmu->dev, dma)) {
			__free_page(page);
			return NULL;
		}

		if (!smmu_dma_addr_valid(smmu, dma)) {
			dma_unmap_page(smmu->dev, dma, SMMU_SIZE_PT,
				       DMA_TO_DEVICE);
			__free_page(page);
			return NULL;
		}

		as->pts[pde] = page;

		tegra_smmu_set_pde(as, iova, SMMU_MK_PDE(dma, SMMU_PDE_ATTR |
							      SMMU_PDE_NEXT));

		*dmap = dma;
	} else {
		u32 *pd = page_address(as->pd);

		*dmap = smmu_pde_to_dma(smmu, pd[pde]);
	}

	return tegra_smmu_pte_offset(as->pts[pde], iova);
}

static void tegra_smmu_pte_get_use(struct tegra_smmu_as *as, unsigned long iova)
{
	unsigned int pd_index = iova_pd_index(iova);

	as->count[pd_index]++;
}

static void tegra_smmu_pte_put_use(struct tegra_smmu_as *as, unsigned long iova)
{
	unsigned int pde = iova_pd_index(iova);
	struct page *page = as->pts[pde];

	/*
	 * When no entries in this page table are used anymore, return the
	 * memory page to the system.
	 */
	if (--as->count[pde] == 0) {
		struct tegra_smmu *smmu = as->smmu;
		u32 *pd = page_address(as->pd);
		dma_addr_t pte_dma = smmu_pde_to_dma(smmu, pd[pde]);

		tegra_smmu_set_pde(as, iova, 0);

		dma_unmap_page(smmu->dev, pte_dma, SMMU_SIZE_PT, DMA_TO_DEVICE);
		__free_page(page);
		as->pts[pde] = NULL;
	}
}

static void tegra_smmu_set_pte(struct tegra_smmu_as *as, unsigned long iova,
			       u32 *pte, dma_addr_t pte_dma, u32 val)
{
	struct tegra_smmu *smmu = as->smmu;
	unsigned long offset = SMMU_OFFSET_IN_PAGE(pte);

	*pte = val;

	dma_sync_single_range_for_device(smmu->dev, pte_dma, offset,
					 4, DMA_TO_DEVICE);
	smmu_flush_ptc(smmu, pte_dma, offset);
	smmu_flush_tlb_group(smmu, as->id, iova);
	smmu_flush(smmu);
}

static struct page *as_get_pde_page(struct tegra_smmu_as *as,
				    unsigned long iova, gfp_t gfp,
				    unsigned long *flags)
{
	unsigned int pde = iova_pd_index(iova);
	struct page *page = as->pts[pde];

	/* at first check whether allocation needs to be done at all */
	if (page)
		return page;

	/*
	 * In order to prevent exhaustion of the atomic memory pool, we
	 * allocate page in a sleeping context if GFP flags permit. Hence
	 * spinlock needs to be unlocked and re-locked after allocation.
	 */
	if (!(gfp & __GFP_ATOMIC))
		spin_unlock_irqrestore(&as->lock, *flags);

	page = alloc_page(gfp | __GFP_DMA | __GFP_ZERO);

	if (!(gfp & __GFP_ATOMIC))
		spin_lock_irqsave(&as->lock, *flags);

	/*
	 * In a case of blocking allocation, a concurrent mapping may win
	 * the PDE allocation. In this case the allocated page isn't needed
	 * if allocation succeeded and the allocation failure isn't fatal.
	 */
	if (as->pts[pde]) {
		if (page)
			__free_page(page);

		page = as->pts[pde];
	}

	return page;
}

static int
__tegra_smmu_map(struct iommu_domain *domain, unsigned long iova,
		 phys_addr_t paddr, size_t size, int prot, gfp_t gfp,
		 unsigned long *flags)
{
	struct tegra_smmu_as *as = to_smmu_as(domain);
	dma_addr_t pte_dma;
	struct page *page;
	u32 pte_attrs;
	u32 *pte;

	page = as_get_pde_page(as, iova, gfp, flags);
	if (!page)
		return -ENOMEM;

	pte = as_get_pte(as, iova, &pte_dma, page);
	if (!pte)
		return -ENOMEM;

	/* If we aren't overwriting a pre-existing entry, increment use */
	if (*pte == 0)
		tegra_smmu_pte_get_use(as, iova);

	pte_attrs = SMMU_PTE_NONSECURE;

	if (prot & IOMMU_READ)
		pte_attrs |= SMMU_PTE_READABLE;

	if (prot & IOMMU_WRITE)
		pte_attrs |= SMMU_PTE_WRITABLE;

	tegra_smmu_set_pte(as, iova, pte, pte_dma,
			   SMMU_PHYS_PFN(paddr) | pte_attrs);

	return 0;
}

static size_t
__tegra_smmu_unmap(struct iommu_domain *domain, unsigned long iova,
		   size_t size, struct iommu_iotlb_gather *gather)
{
	struct tegra_smmu_as *as = to_smmu_as(domain);
	dma_addr_t pte_dma;
	u32 *pte;

	pte = tegra_smmu_pte_lookup(as, iova, &pte_dma);
	if (!pte || !*pte)
		return 0;

	tegra_smmu_set_pte(as, iova, pte, pte_dma, 0);
	tegra_smmu_pte_put_use(as, iova);

	return size;
}

static int tegra_smmu_map(struct iommu_domain *domain, unsigned long iova,
			  phys_addr_t paddr, size_t size, int prot, gfp_t gfp)
{
	struct tegra_smmu_as *as = to_smmu_as(domain);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&as->lock, flags);
	ret = __tegra_smmu_map(domain, iova, paddr, size, prot, gfp, &flags);
	spin_unlock_irqrestore(&as->lock, flags);

	return ret;
}

static size_t tegra_smmu_unmap(struct iommu_domain *domain, unsigned long iova,
			       size_t size, struct iommu_iotlb_gather *gather)
{
	struct tegra_smmu_as *as = to_smmu_as(domain);
	unsigned long flags;

	spin_lock_irqsave(&as->lock, flags);
	size = __tegra_smmu_unmap(domain, iova, size, gather);
	spin_unlock_irqrestore(&as->lock, flags);

	return size;
}

static phys_addr_t tegra_smmu_iova_to_phys(struct iommu_domain *domain,
					   dma_addr_t iova)
{
	struct tegra_smmu_as *as = to_smmu_as(domain);
	unsigned long pfn;
	dma_addr_t pte_dma;
	u32 *pte;

	pte = tegra_smmu_pte_lookup(as, iova, &pte_dma);
	if (!pte || !*pte)
		return 0;

	pfn = *pte & as->smmu->pfn_mask;

	return SMMU_PFN_PHYS(pfn) + SMMU_OFFSET_IN_PAGE(iova);
}

static struct tegra_smmu *tegra_smmu_find(struct device_node *np)
{
	struct platform_device *pdev;
	struct tegra_mc *mc;

	pdev = of_find_device_by_node(np);
	if (!pdev)
		return NULL;

	mc = platform_get_drvdata(pdev);
	if (!mc) {
		put_device(&pdev->dev);
		return NULL;
	}

	return mc->smmu;
}

static int tegra_smmu_configure(struct tegra_smmu *smmu, struct device *dev,
				struct of_phandle_args *args)
{
	const struct iommu_ops *ops = smmu->iommu.ops;
	int err;

	err = iommu_fwspec_init(dev, &dev->of_node->fwnode, ops);
	if (err < 0) {
		dev_err(dev, "failed to initialize fwspec: %d\n", err);
		return err;
	}

	err = ops->of_xlate(dev, args);
	if (err < 0) {
		dev_err(dev, "failed to parse SW group ID: %d\n", err);
		iommu_fwspec_free(dev);
		return err;
	}

	return 0;
}

static struct iommu_device *tegra_smmu_probe_device(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct tegra_smmu *smmu = NULL;
	struct of_phandle_args args;
	unsigned int index = 0;
	int err;

	while (of_parse_phandle_with_args(np, "iommus", "#iommu-cells", index,
					  &args) == 0) {
		smmu = tegra_smmu_find(args.np);
		if (smmu) {
			err = tegra_smmu_configure(smmu, dev, &args);

			if (err < 0) {
				of_node_put(args.np);
				return ERR_PTR(err);
			}
		}

		of_node_put(args.np);
		index++;
	}

	smmu = dev_iommu_priv_get(dev);
	if (!smmu)
		return ERR_PTR(-ENODEV);

	return &smmu->iommu;
}

static const struct tegra_smmu_group_soc *
tegra_smmu_find_group(struct tegra_smmu *smmu, unsigned int swgroup)
{
	unsigned int i, j;

	for (i = 0; i < smmu->soc->num_groups; i++)
		for (j = 0; j < smmu->soc->groups[i].num_swgroups; j++)
			if (smmu->soc->groups[i].swgroups[j] == swgroup)
				return &smmu->soc->groups[i];

	return NULL;
}

static void tegra_smmu_group_release(void *iommu_data)
{
	struct tegra_smmu_group *group = iommu_data;
	struct tegra_smmu *smmu = group->smmu;

	mutex_lock(&smmu->lock);
	list_del(&group->list);
	mutex_unlock(&smmu->lock);
}

static struct iommu_group *tegra_smmu_device_group(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct tegra_smmu *smmu = dev_iommu_priv_get(dev);
	const struct tegra_smmu_group_soc *soc;
	unsigned int swgroup = fwspec->ids[0];
	struct tegra_smmu_group *group;
	struct iommu_group *grp;

	/* Find group_soc associating with swgroup */
	soc = tegra_smmu_find_group(smmu, swgroup);

	mutex_lock(&smmu->lock);

	/* Find existing iommu_group associating with swgroup or group_soc */
	list_for_each_entry(group, &smmu->groups, list)
		if ((group->swgroup == swgroup) || (soc && group->soc == soc)) {
			grp = iommu_group_ref_get(group->group);
			mutex_unlock(&smmu->lock);
			return grp;
		}

	group = devm_kzalloc(smmu->dev, sizeof(*group), GFP_KERNEL);
	if (!group) {
		mutex_unlock(&smmu->lock);
		return NULL;
	}

	INIT_LIST_HEAD(&group->list);
	group->swgroup = swgroup;
	group->smmu = smmu;
	group->soc = soc;

	if (dev_is_pci(dev))
		group->group = pci_device_group(dev);
	else
		group->group = generic_device_group(dev);

	if (IS_ERR(group->group)) {
		devm_kfree(smmu->dev, group);
		mutex_unlock(&smmu->lock);
		return NULL;
	}

	iommu_group_set_iommudata(group->group, group, tegra_smmu_group_release);
	if (soc)
		iommu_group_set_name(group->group, soc->name);
	list_add_tail(&group->list, &smmu->groups);
	mutex_unlock(&smmu->lock);

	return group->group;
}

static int tegra_smmu_of_xlate(struct device *dev,
			       struct of_phandle_args *args)
{
	struct platform_device *iommu_pdev = of_find_device_by_node(args->np);
	struct tegra_mc *mc = platform_get_drvdata(iommu_pdev);
	u32 id = args->args[0];

	/*
	 * Note: we are here releasing the reference of &iommu_pdev->dev, which
	 * is mc->dev. Although some functions in tegra_smmu_ops may keep using
	 * its private data beyond this point, it's still safe to do so because
	 * the SMMU parent device is the same as the MC, so the reference count
	 * isn't strictly necessary.
	 */
	put_device(&iommu_pdev->dev);

	dev_iommu_priv_set(dev, mc->smmu);

	return iommu_fwspec_add_ids(dev, &id, 1);
}

static const struct iommu_ops tegra_smmu_ops = {
	.domain_alloc = tegra_smmu_domain_alloc,
	.probe_device = tegra_smmu_probe_device,
	.device_group = tegra_smmu_device_group,
	.of_xlate = tegra_smmu_of_xlate,
	.pgsize_bitmap = SZ_4K,
	.default_domain_ops = &(const struct iommu_domain_ops) {
		.attach_dev	= tegra_smmu_attach_dev,
		.detach_dev	= tegra_smmu_detach_dev,
		.map		= tegra_smmu_map,
		.unmap		= tegra_smmu_unmap,
		.iova_to_phys	= tegra_smmu_iova_to_phys,
		.free		= tegra_smmu_domain_free,
	}
};

static void tegra_smmu_ahb_enable(void)
{
	static const struct of_device_id ahb_match[] = {
		{ .compatible = "nvidia,tegra30-ahb", },
		{ }
	};
	struct device_node *ahb;

	ahb = of_find_matching_node(NULL, ahb_match);
	if (ahb) {
		tegra_ahb_enable_smmu(ahb);
		of_node_put(ahb);
	}
}

static int tegra_smmu_swgroups_show(struct seq_file *s, void *data)
{
	struct tegra_smmu *smmu = s->private;
	unsigned int i;
	u32 value;

	seq_printf(s, "swgroup    enabled  ASID\n");
	seq_printf(s, "------------------------\n");

	for (i = 0; i < smmu->soc->num_swgroups; i++) {
		const struct tegra_smmu_swgroup *group = &smmu->soc->swgroups[i];
		const char *status;
		unsigned int asid;

		value = smmu_readl(smmu, group->reg);

		if (value & SMMU_ASID_ENABLE)
			status = "yes";
		else
			status = "no";

		asid = value & SMMU_ASID_MASK;

		seq_printf(s, "%-9s  %-7s  %#04x\n", group->name, status,
			   asid);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(tegra_smmu_swgroups);

static int tegra_smmu_clients_show(struct seq_file *s, void *data)
{
	struct tegra_smmu *smmu = s->private;
	unsigned int i;
	u32 value;

	seq_printf(s, "client       enabled\n");
	seq_printf(s, "--------------------\n");

	for (i = 0; i < smmu->soc->num_clients; i++) {
		const struct tegra_mc_client *client = &smmu->soc->clients[i];
		const char *status;

		value = smmu_readl(smmu, client->regs.smmu.reg);

		if (value & BIT(client->regs.smmu.bit))
			status = "yes";
		else
			status = "no";

		seq_printf(s, "%-12s %s\n", client->name, status);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(tegra_smmu_clients);

static void tegra_smmu_debugfs_init(struct tegra_smmu *smmu)
{
	smmu->debugfs = debugfs_create_dir("smmu", NULL);
	if (!smmu->debugfs)
		return;

	debugfs_create_file("swgroups", S_IRUGO, smmu->debugfs, smmu,
			    &tegra_smmu_swgroups_fops);
	debugfs_create_file("clients", S_IRUGO, smmu->debugfs, smmu,
			    &tegra_smmu_clients_fops);
}

static void tegra_smmu_debugfs_exit(struct tegra_smmu *smmu)
{
	debugfs_remove_recursive(smmu->debugfs);
}

struct tegra_smmu *tegra_smmu_probe(struct device *dev,
				    const struct tegra_smmu_soc *soc,
				    struct tegra_mc *mc)
{
	struct tegra_smmu *smmu;
	u32 value;
	int err;

	smmu = devm_kzalloc(dev, sizeof(*smmu), GFP_KERNEL);
	if (!smmu)
		return ERR_PTR(-ENOMEM);

	/*
	 * This is a bit of a hack. Ideally we'd want to simply return this
	 * value. However the IOMMU registration process will attempt to add
	 * all devices to the IOMMU when bus_set_iommu() is called. In order
	 * not to rely on global variables to track the IOMMU instance, we
	 * set it here so that it can be looked up from the .probe_device()
	 * callback via the IOMMU device's .drvdata field.
	 */
	mc->smmu = smmu;

	smmu->asids = devm_bitmap_zalloc(dev, soc->num_asids, GFP_KERNEL);
	if (!smmu->asids)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&smmu->groups);
	mutex_init(&smmu->lock);

	smmu->regs = mc->regs;
	smmu->soc = soc;
	smmu->dev = dev;
	smmu->mc = mc;

	smmu->pfn_mask =
		BIT_MASK(mc->soc->num_address_bits - SMMU_PTE_SHIFT) - 1;
	dev_dbg(dev, "address bits: %u, PFN mask: %#lx\n",
		mc->soc->num_address_bits, smmu->pfn_mask);
	smmu->tlb_mask = (1 << fls(smmu->soc->num_tlb_lines)) - 1;
	dev_dbg(dev, "TLB lines: %u, mask: %#lx\n", smmu->soc->num_tlb_lines,
		smmu->tlb_mask);

	value = SMMU_PTC_CONFIG_ENABLE | SMMU_PTC_CONFIG_INDEX_MAP(0x3f);

	if (soc->supports_request_limit)
		value |= SMMU_PTC_CONFIG_REQ_LIMIT(8);

	smmu_writel(smmu, value, SMMU_PTC_CONFIG);

	value = SMMU_TLB_CONFIG_HIT_UNDER_MISS |
		SMMU_TLB_CONFIG_ACTIVE_LINES(smmu);

	if (soc->supports_round_robin_arbitration)
		value |= SMMU_TLB_CONFIG_ROUND_ROBIN_ARBITRATION;

	smmu_writel(smmu, value, SMMU_TLB_CONFIG);

	smmu_flush_ptc_all(smmu);
	smmu_flush_tlb(smmu);
	smmu_writel(smmu, SMMU_CONFIG_ENABLE, SMMU_CONFIG);
	smmu_flush(smmu);

	tegra_smmu_ahb_enable();

	err = iommu_device_sysfs_add(&smmu->iommu, dev, NULL, dev_name(dev));
	if (err)
		return ERR_PTR(err);

	err = iommu_device_register(&smmu->iommu, &tegra_smmu_ops, dev);
	if (err)
		goto remove_sysfs;

	err = bus_set_iommu(&platform_bus_type, &tegra_smmu_ops);
	if (err < 0)
		goto unregister;

#ifdef CONFIG_PCI
	err = bus_set_iommu(&pci_bus_type, &tegra_smmu_ops);
	if (err < 0)
		goto unset_platform_bus;
#endif

	if (IS_ENABLED(CONFIG_DEBUG_FS))
		tegra_smmu_debugfs_init(smmu);

	return smmu;

unset_platform_bus: __maybe_unused;
	bus_set_iommu(&platform_bus_type, NULL);
unregister:
	iommu_device_unregister(&smmu->iommu);
remove_sysfs:
	iommu_device_sysfs_remove(&smmu->iommu);

	return ERR_PTR(err);
}

void tegra_smmu_remove(struct tegra_smmu *smmu)
{
	iommu_device_unregister(&smmu->iommu);
	iommu_device_sysfs_remove(&smmu->iommu);

	if (IS_ENABLED(CONFIG_DEBUG_FS))
		tegra_smmu_debugfs_exit(smmu);
}
