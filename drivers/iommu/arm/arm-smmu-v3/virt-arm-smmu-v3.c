// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * IOMMU API for ARM architected SMMUv3 implementations.
 *
 *
 * Author: Will Deacon <will.deacon@arm.com>
 *
 * This driver is powered by bad coffee and bombay mix.
 */

#include <linux/acpi.h>
#include <linux/acpi_iort.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/crash_dump.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io-pgtable.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_iommu.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/pci-ats.h>
#include <linux/platform_device.h>
#include <linux/qcom_scm.h>
#include <linux/amba/bus.h>
#include <linux/qcom-iommu-util.h>
#include <trace/hooks/iommu.h>

#include "../../dma-iommu.h"
#define MSI_IOVA_BASE                     0x8000000
#define MSI_IOVA_LENGTH                   0x100000

/*
 * Stream table.
 *
 * Linear: Enough to cover 1 << IDR1.SIDSIZE entries
 * 2lvl: 128k L1 entries,
 *       256 lazy entries per table (each table covers a PCI bus)
 */
#define STRTAB_L1_SZ_SHIFT		20
#define STRTAB_SPLIT			8

#define STRTAB_L1_DESC_DWORDS		1
#define STRTAB_L1_DESC_SPAN		GENMASK_ULL(4, 0)
#define STRTAB_L1_DESC_L2PTR_MASK	GENMASK_ULL(51, 6)

#define STRTAB_STE_DWORDS		8
#define STRTAB_STE_0_V			(1UL << 0)
#define STRTAB_STE_0_CFG		GENMASK_ULL(3, 1)
#define STRTAB_STE_0_CFG_ABORT		0
#define STRTAB_STE_0_CFG_BYPASS		4
#define STRTAB_STE_0_CFG_S1_TRANS	5
#define STRTAB_STE_0_CFG_S2_TRANS	6

#define STRTAB_STE_0_S1FMT		GENMASK_ULL(5, 4)
#define STRTAB_STE_0_S1FMT_LINEAR	0
#define STRTAB_STE_0_S1CTXPTR_MASK	GENMASK_ULL(51, 6)
#define STRTAB_STE_0_S1CDMAX		GENMASK_ULL(63, 59)

#define STRTAB_STE_1_S1C_CACHE_NC	0UL
#define STRTAB_STE_1_S1C_CACHE_WBRA	1UL
#define STRTAB_STE_1_S1C_CACHE_WT	2UL
#define STRTAB_STE_1_S1C_CACHE_WB	3UL
#define STRTAB_STE_1_S1CIR		GENMASK_ULL(3, 2)
#define STRTAB_STE_1_S1COR		GENMASK_ULL(5, 4)
#define STRTAB_STE_1_S1CSH		GENMASK_ULL(7, 6)

#define STRTAB_STE_1_S1STALLD		(1UL << 27)

#define STRTAB_STE_1_EATS		GENMASK_ULL(29, 28)
#define STRTAB_STE_1_EATS_ABT		0UL
#define STRTAB_STE_1_EATS_TRANS		1UL
#define STRTAB_STE_1_EATS_S1CHK		2UL

#define STRTAB_STE_1_STRW		GENMASK_ULL(31, 30)
#define STRTAB_STE_1_STRW_NSEL1		0UL
#define STRTAB_STE_1_STRW_EL2		2UL

#define STRTAB_STE_1_SHCFG		GENMASK_ULL(45, 44)
#define STRTAB_STE_1_SHCFG_INCOMING	1UL

#define STRTAB_STE_2_S2VMID		GENMASK_ULL(15, 0)
#define STRTAB_STE_2_VTCR		GENMASK_ULL(50, 32)
#define STRTAB_STE_2_S2AA64		(1UL << 51)
#define STRTAB_STE_2_S2ENDI		(1UL << 52)
#define STRTAB_STE_2_S2PTW		(1UL << 54)
#define STRTAB_STE_2_S2R		(1UL << 58)

#define STRTAB_STE_3_S2TTB_MASK		GENMASK_ULL(51, 4)

/* Context descriptor (stage-1 only) */
#define CTXDESC_CD_DWORDS		8
#define CTXDESC_CD_0_TCR_T0SZ		GENMASK_ULL(5, 0)
#define ARM64_TCR_T0SZ			GENMASK_ULL(5, 0)
#define CTXDESC_CD_0_TCR_TG0		GENMASK_ULL(7, 6)
#define ARM64_TCR_TG0			GENMASK_ULL(15, 14)
#define CTXDESC_CD_0_TCR_IRGN0		GENMASK_ULL(9, 8)
#define ARM64_TCR_IRGN0			GENMASK_ULL(9, 8)
#define CTXDESC_CD_0_TCR_ORGN0		GENMASK_ULL(11, 10)
#define ARM64_TCR_ORGN0			GENMASK_ULL(11, 10)
#define CTXDESC_CD_0_TCR_SH0		GENMASK_ULL(13, 12)
#define ARM64_TCR_SH0			GENMASK_ULL(13, 12)
#define CTXDESC_CD_0_TCR_EPD0		(1ULL << 14)
#define ARM64_TCR_EPD0			(1ULL << 7)
#define CTXDESC_CD_0_TCR_EPD1		(1ULL << 30)
#define ARM64_TCR_EPD1			(1ULL << 23)

#define CTXDESC_CD_0_ENDI		(1UL << 15)
#define CTXDESC_CD_0_V			(1UL << 31)

#define CTXDESC_CD_0_TCR_IPS		GENMASK_ULL(34, 32)
#define ARM64_TCR_IPS			GENMASK_ULL(34, 32)
#define CTXDESC_CD_0_TCR_TBI0		(1ULL << 38)
#define ARM64_TCR_TBI0			(1ULL << 37)

#define CTXDESC_CD_0_AA64		(1UL << 41)
#define CTXDESC_CD_0_S			(1UL << 44)
#define CTXDESC_CD_0_R			(1UL << 45)
#define CTXDESC_CD_0_A			(1UL << 46)
#define CTXDESC_CD_0_ASET		(1UL << 47)
#define CTXDESC_CD_0_ASID		GENMASK_ULL(63, 48)

#define CTXDESC_CD_1_TTB0_MASK		GENMASK_ULL(51, 4)

/* Common memory attribute values */
#define ARM_SMMU_SH_NSH			0
#define ARM_SMMU_SH_OSH			2
#define ARM_SMMU_SH_ISH			3
#define ARM_SMMU_MEMATTR_DEVICE_nGnRE	0x1
#define ARM_SMMU_MEMATTR_OIWB		0xf

struct msm_io_pgtable_info {
	int (*map_sg)(struct io_pgtable_ops *ops, unsigned long iova,
			struct scatterlist *sg, unsigned int nents, int prot,
			size_t *size);
	bool (*is_iova_coherent)(struct io_pgtable_ops *ops,
			unsigned long iova);
	uint64_t (*iova_to_pte)(struct io_pgtable_ops *ops, unsigned long iova);
	struct io_pgtable_cfg pgtbl_cfg;
	dma_addr_t      iova_base;
	dma_addr_t      iova_end;
};

struct msm_iommu_ops {
	size_t (*map_sg)(struct iommu_domain *domain, unsigned long iova,
			struct scatterlist *sg, unsigned int nents, int prot);
	phys_addr_t (*iova_to_phys_hard)(struct iommu_domain *domain,
					dma_addr_t iova,
					unsigned long trans_flags);
	bool (*is_iova_coherent)(struct iommu_domain *domain, dma_addr_t iova);
	void (*tlbi_domain)(struct iommu_domain *domain);
	uint64_t (*iova_to_pte)(struct iommu_domain *domain, dma_addr_t iova);
	struct iommu_ops iommu_ops;
};

struct arm_smmu_s1_cfg {
	__le64				*cdptr;
	dma_addr_t			cdptr_dma;

	struct arm_smmu_ctx_desc {
		u16	asid;
		u64	ttbr;
		u64	tcr;
		u64	mair;
	} cd;
};

struct arm_smmu_ste_cfg {
	__le64				*ste;
	dma_addr_t			stedma;
	u32				sid;
};

struct virt_arm_smmu_device {
	struct device		*dev;
	unsigned long		ias;
	unsigned long		oas;
#define ARM_SMMU_MAX_ASIDS              (1 << 16)
	unsigned int		asid_bits;
	DECLARE_BITMAP(asid_map, ARM_SMMU_MAX_ASIDS);

#define ARM_SMMU_MAX_VMIDS              (1 << 16)
	unsigned int		vmid_bits;
	DECLARE_BITMAP(vmid_map, ARM_SMMU_MAX_VMIDS);
	unsigned long		pgsize_bitmap;

	struct iommu_device	iommu;
};

struct virt_arm_smmu_domain {
	struct virt_arm_smmu_device	*smmu;
	struct arm_smmu_s1_cfg		s1_cfg;
	struct mutex			init_mutex;
	struct msm_io_pgtable_info      pgtbl_info;
	struct io_pgtable_ops		*pgtbl_ops;
	struct iommu_domain		domain;
	struct list_head		devices;
	spinlock_t			devices_lock;
};

struct virt_arm_smmu_master {
	struct virt_arm_smmu_device	*smmu;
	struct device			*dev;
	struct virt_arm_smmu_domain	*domain;
	struct arm_smmu_ste_cfg		*ste_cfg;
	struct list_head domain_head;
	u32				*sids;
	unsigned int			num_sids;
};

static int arm_smmu_bitmap_alloc(unsigned long *map, int span)
{
	int idx, size = 1 << span;

	do {
		idx = find_first_zero_bit(map, size);
		if (idx == size)
			return -ENOSPC;
	} while (test_and_set_bit(idx, map));

	return idx;
}

static void arm_smmu_bitmap_free(unsigned long *map, int idx)
{
	clear_bit(idx, map);
}

static struct virt_arm_smmu_domain *to_smmu_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct virt_arm_smmu_domain, domain);
}

static int virt_arm_smmu_domain_finalise_s1(struct virt_arm_smmu_domain *smmu_domain,
				       struct io_pgtable_cfg *pgtbl_cfg)
{
	int ret;
	int asid;
	struct virt_arm_smmu_device *smmu = smmu_domain->smmu;
	struct arm_smmu_s1_cfg *cfg = &smmu_domain->s1_cfg;
	typeof(&pgtbl_cfg->arm_lpae_s1_cfg.tcr) tcr = &pgtbl_cfg->arm_lpae_s1_cfg.tcr;

	asid = arm_smmu_bitmap_alloc(smmu->asid_map, smmu->asid_bits);
	if (asid < 0)
		return asid;

	cfg->cdptr = dmam_alloc_coherent(smmu->dev, CTXDESC_CD_DWORDS << 3,
					 &cfg->cdptr_dma,
					 GFP_KERNEL | __GFP_ZERO);
	if (!cfg->cdptr) {
		dev_warn(smmu->dev, "failed to allocate context descriptor\n");
		ret = -ENOMEM;
		goto out_free_asid;
	}

	cfg->cd.asid	= (u16)asid;
	cfg->cd.ttbr	= pgtbl_cfg->arm_lpae_s1_cfg.ttbr;
	cfg->cd.tcr	= FIELD_PREP(CTXDESC_CD_0_TCR_T0SZ, tcr->tsz) |
			  FIELD_PREP(CTXDESC_CD_0_TCR_TG0, tcr->tg) |
			  FIELD_PREP(CTXDESC_CD_0_TCR_IRGN0, tcr->irgn) |
			  FIELD_PREP(CTXDESC_CD_0_TCR_ORGN0, tcr->orgn) |
			  FIELD_PREP(CTXDESC_CD_0_TCR_SH0, tcr->sh) |
			  FIELD_PREP(CTXDESC_CD_0_TCR_IPS, tcr->ips) |
			  CTXDESC_CD_0_TCR_EPD1 | CTXDESC_CD_0_AA64;
	cfg->cd.mair	= pgtbl_cfg->arm_lpae_s1_cfg.mair;
	return 0;

out_free_asid:
	arm_smmu_bitmap_free(smmu->asid_map, asid);
	return ret;
}

static void virt_arm_smmu_write_ctx_desc(struct virt_arm_smmu_device *smmu,
				    struct arm_smmu_s1_cfg *cfg)
{
	u64 val;

	val = cfg->cd.tcr |
#ifdef __BIG_ENDIAN
	      CTXDESC_CD_0_ENDI |
#endif
	      CTXDESC_CD_0_R | CTXDESC_CD_0_A | CTXDESC_CD_0_ASET |
	      CTXDESC_CD_0_AA64 | FIELD_PREP(CTXDESC_CD_0_ASID, cfg->cd.asid) |
	      CTXDESC_CD_0_V;

	cfg->cdptr[0] = cpu_to_le64(val);

	val = cfg->cd.ttbr & CTXDESC_CD_1_TTB0_MASK;
	cfg->cdptr[1] = cpu_to_le64(val);

	cfg->cdptr[3] = cpu_to_le64(cfg->cd.mair);
}

static const struct iommu_flush_ops virt_arm_smmu_flush_ops;

static int arm_smmu_domain_finalise(struct iommu_domain *domain)
{
	int ret;
	unsigned long ias, oas;
	enum io_pgtable_fmt fmt;
	struct io_pgtable_ops *pgtbl_ops;
	int (*finalise_stage_fn)(struct virt_arm_smmu_domain *func,
				 struct io_pgtable_cfg *cfg);
	struct virt_arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct virt_arm_smmu_device *smmu = smmu_domain->smmu;
	struct msm_io_pgtable_info *pgtbl_info = &smmu_domain->pgtbl_info;

	if (domain->type == IOMMU_DOMAIN_IDENTITY)
		return 0;

	ias = min_t(unsigned long, 48, VA_BITS);
	oas = smmu->ias;
	fmt = ARM_64_LPAE_S1;
	finalise_stage_fn = virt_arm_smmu_domain_finalise_s1;
	pgtbl_info->iova_base = 0;
	pgtbl_info->iova_end = SZ_4G - 1;
	pgtbl_info->pgtbl_cfg = (struct io_pgtable_cfg) {
		.pgsize_bitmap	= smmu->pgsize_bitmap,
		.ias		= ias,
		.oas		= oas,
		.coherent_walk	= false,
		.tlb		= &virt_arm_smmu_flush_ops,
		.iommu_dev	= smmu->dev,
	};

	pgtbl_ops = alloc_io_pgtable_ops(fmt, &pgtbl_info->pgtbl_cfg, smmu_domain);
	if (!pgtbl_ops)
		return -ENOMEM;

	domain->pgsize_bitmap = pgtbl_info->pgtbl_cfg.pgsize_bitmap;
	domain->geometry.aperture_end = (1UL << pgtbl_info->pgtbl_cfg.ias) - 1;
	domain->geometry.force_aperture = true;

	ret = finalise_stage_fn(smmu_domain, &pgtbl_info->pgtbl_cfg);
	if (ret < 0) {
		free_io_pgtable_ops(pgtbl_ops);
		return ret;
	}

	smmu_domain->pgtbl_ops = pgtbl_ops;
	return 0;
}

static struct platform_driver virt_arm_smmu_driver;

static struct virt_arm_smmu_device *virt_arm_smmu_get_by_fwnode(struct fwnode_handle *fwnode)
{
	struct device *dev = driver_find_device_by_fwnode(&virt_arm_smmu_driver.driver,
							  fwnode);
	put_device(dev);
	return dev ? dev_get_drvdata(dev) : NULL;
}

static void virt_arm_smmu_get_resv_regions(struct device *dev,
				      struct list_head *head)
{
	struct iommu_resv_region *region;
	int prot = IOMMU_WRITE | IOMMU_NOEXEC | IOMMU_MMIO;

	region = iommu_alloc_resv_region(MSI_IOVA_BASE, MSI_IOVA_LENGTH,
						prot, IOMMU_RESV_SW_MSI, GFP_KERNEL);
	if (!region)
		return;

	list_add_tail(&region->list, head);

	iommu_dma_get_resv_regions(dev, head);

	qcom_iommu_generate_resv_regions(dev, head);

}

static int virt_arm_smmu_of_xlate(struct device *dev, struct of_phandle_args *args)
{
	return iommu_fwspec_add_ids(dev, args->args, 1);
}

static struct iommu_group *virt_arm_smmu_device_group(struct device *dev)
{
	struct iommu_group *group;

	if (dev_is_pci(dev))
		group = pci_device_group(dev);
	else
		group = generic_device_group(dev);

	return group;
}

static void remove_ste_for_dev(struct virt_arm_smmu_master *master)
{
	struct virt_arm_smmu_device *smmu = master->smmu;
	struct arm_smmu_ste_cfg *iter;
	int i;

	iter = master->ste_cfg;

	for (i = 0; i < master->num_sids; i++) {
		if (qcom_scm_paravirt_smmu_detach(iter->sid))
			pr_err("Failed to detach SID:0x%x\n", iter->sid);
		if (iter->ste)
			dmam_free_coherent(smmu->dev, (STRTAB_STE_DWORDS << 3),
					iter->ste, iter->stedma);
		iter++;
	}
	kfree(master->ste_cfg);
}

static struct iommu_ops virt_arm_smmu_ops;

static void virt_arm_smmu_detach_dev(struct iommu_domain *domain,
					struct device *dev)
{
	unsigned long flags;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct virt_arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct virt_arm_smmu_master *master;

	if (!fwspec || !smmu_domain)
		return;
	master = dev_iommu_priv_get(dev);

	remove_ste_for_dev(master);
	spin_lock_irqsave(&smmu_domain->devices_lock, flags);
	list_del(&master->domain_head);
	spin_unlock_irqrestore(&smmu_domain->devices_lock, flags);
	master->domain = NULL;
}

static void virt_arm_smmu_remove_device(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct virt_arm_smmu_master *master;
	struct virt_arm_smmu_device *smmu;

	if (!fwspec || fwspec->ops != &virt_arm_smmu_ops)
		return;

	master = dev_iommu_priv_get(dev);
	smmu = master->smmu;
	virt_arm_smmu_detach_dev(&master->domain->domain, dev);
	iommu_group_remove_device(dev);
	iommu_device_unlink(&smmu->iommu, dev);
	kfree(master);
	iommu_fwspec_free(dev);
}

static struct iommu_device *virt_arm_smmu_add_device(struct device *dev)
{
	struct virt_arm_smmu_device *smmu;
	struct virt_arm_smmu_master *master;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	if (!fwspec || fwspec->ops != &virt_arm_smmu_ops)
		return ERR_PTR(-ENODEV);
	if (WARN_ON_ONCE(dev_iommu_priv_get(dev))) {
		master = dev_iommu_priv_get(dev);
		smmu = master->smmu;
	} else {
		smmu = virt_arm_smmu_get_by_fwnode(fwspec->iommu_fwnode);
		if (!smmu)
			return ERR_PTR(-ENODEV);
		master = kzalloc(sizeof(*master), GFP_KERNEL);
		if (!master)
			return ERR_PTR(-ENOMEM);

		master->dev = dev;
		master->smmu = smmu;
		master->sids = fwspec->ids;
		master->num_sids = fwspec->num_ids;
		dev_iommu_priv_set(dev, master);
	}

	return &smmu->iommu;
}

static phys_addr_t virt_arm_smmu_iova_to_phys(struct iommu_domain *domain, dma_addr_t iova)
{
	struct io_pgtable_ops *ops = to_smmu_domain(domain)->pgtbl_ops;

	if (domain->type == IOMMU_DOMAIN_IDENTITY)
		return iova;

	if (!ops)
		return 0;

	return ops->iova_to_phys(ops, iova);
}

static unsigned long get_sid_from_smmu_domain(struct virt_arm_smmu_domain *smmu_domain)
{
	struct virt_arm_smmu_master *master;
	unsigned long flags;
	u64 sid;
	u16 i;

	spin_lock_irqsave(&smmu_domain->devices_lock, flags);
	list_for_each_entry(master, &smmu_domain->devices, domain_head) {
		for (i = 0; i < master->num_sids; i++) {
			if (master->domain == smmu_domain)
				sid = master->ste_cfg->sid;
		}
	}
	spin_unlock_irqrestore(&smmu_domain->devices_lock, flags);

	return sid;
}

static void virt_arm_smmu_tlb_inv_sync(unsigned long iova, size_t size,
				   size_t granule, bool leaf,
				   struct virt_arm_smmu_domain *smmu_domain)
{
	u32 asid;
	u64 sid;

	if (!size)
		return;
	asid = smmu_domain->s1_cfg.cd.asid;
	sid = get_sid_from_smmu_domain(smmu_domain);

	if (qcom_scm_paravirt_tlb_inv(asid, sid))
		pr_err("SCM called failed for TLB inv: asid:0x%x and sid is 0x%x\n", asid, sid);
}


static void virt_arm_smmu_tlb_inv_context(void *cookie)
{
	struct virt_arm_smmu_domain *smmu_domain = cookie;
	u32 asid = smmu_domain->s1_cfg.cd.asid;
	u64 sid;

	sid = get_sid_from_smmu_domain(smmu_domain);
	if (qcom_scm_paravirt_tlb_inv(asid, sid))
		pr_err("SCM called failed for TLB inv: asid:0x%x and sid is 0x%x\n", asid, sid);
}

static void virt_arm_smmu_iotlb_sync(struct iommu_domain *domain,
				struct iommu_iotlb_gather *gather)
{
	struct virt_arm_smmu_domain *smmu_domain = to_smmu_domain(domain);

	virt_arm_smmu_tlb_inv_sync(gather->start, gather->end - gather->start,
			       gather->pgsize, true, smmu_domain);
}

static void virt_arm_smmu_flush_iotlb_all(struct iommu_domain *domain)
{
	struct virt_arm_smmu_domain *smmu_domain = to_smmu_domain(domain);

	if (smmu_domain->smmu)
		virt_arm_smmu_tlb_inv_context(smmu_domain);
}

static size_t virt_arm_smmu_unmap(struct iommu_domain *domain, unsigned long iova,
			     size_t size, struct iommu_iotlb_gather *gather)
{
	struct virt_arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct io_pgtable_ops *ops = smmu_domain->pgtbl_ops;

	if (!ops)
		return 0;

	return ops->unmap(ops, iova, size, gather);
}

static int virt_arm_smmu_map(struct iommu_domain *domain, unsigned long iova,
			phys_addr_t paddr, size_t size, int prot, gfp_t gfp)
{
	struct io_pgtable_ops *ops = to_smmu_domain(domain)->pgtbl_ops;

	if (!ops)
		return -ENODEV;

	return ops->map(ops, iova, paddr, size, prot, gfp);
}

static int virt_arm_smmu_write_strtab_ent(struct virt_arm_smmu_master *master,
					 struct arm_smmu_ste_cfg *ste_cfg)
{
	u64 val = le64_to_cpu(ste_cfg->ste[0]);
	struct virt_arm_smmu_device *smmu = NULL;
	struct arm_smmu_s1_cfg *s1_cfg = NULL;
	struct virt_arm_smmu_domain *smmu_domain = NULL;

	if (master) {
		smmu_domain = master->domain;
		smmu = master->smmu;
	}
	if (!smmu_domain)
		return -EINVAL;

	s1_cfg = &smmu_domain->s1_cfg;
	val = STRTAB_STE_0_V;

	if (s1_cfg) {
		ste_cfg->ste[1] = cpu_to_le64(
			 FIELD_PREP(STRTAB_STE_1_S1CIR, STRTAB_STE_1_S1C_CACHE_WBRA) |
			 FIELD_PREP(STRTAB_STE_1_S1COR, STRTAB_STE_1_S1C_CACHE_WBRA) |
			 FIELD_PREP(STRTAB_STE_1_S1CSH, ARM_SMMU_SH_ISH) |
			 FIELD_PREP(STRTAB_STE_1_STRW, STRTAB_STE_1_STRW_NSEL1));

		ste_cfg->ste[1] |= cpu_to_le64(STRTAB_STE_1_S1STALLD);

		/* S1 Translate S2 Bypass only supported*/
		val |= (s1_cfg->cdptr_dma & STRTAB_STE_0_S1CTXPTR_MASK) |
			FIELD_PREP(STRTAB_STE_0_CFG, STRTAB_STE_0_CFG_S1_TRANS);
	}
	WRITE_ONCE(ste_cfg->ste[0], cpu_to_le64(val));
	if (qcom_scm_paravirt_smmu_attach(ste_cfg->sid, 0, ste_cfg->stedma,
		(STRTAB_STE_DWORDS << 3), s1_cfg->cdptr_dma, (CTXDESC_CD_DWORDS << 3))) {
		pr_err("SCM call failed to attach for SID:0x%x\n", ste_cfg->sid);
		return -EINVAL;
	}
	return 0;
}

static int virt_arm_smmu_install_ste_for_dev(struct virt_arm_smmu_master *master)
{
	struct virt_arm_smmu_device *smmu = master->smmu;
	int i, j;
	struct arm_smmu_ste_cfg *iter;

	struct arm_smmu_ste_cfg *ste_cfg = kzalloc(sizeof(*ste_cfg) * master->num_sids,
						GFP_KERNEL);
	if (!ste_cfg)
		return -ENOMEM;

	iter = ste_cfg;

	for (i = 0; i < master->num_sids; i++) {
		iter->ste = dmam_alloc_coherent(smmu->dev,
				(STRTAB_STE_DWORDS << 3), &iter->stedma,
				GFP_KERNEL | __GFP_ZERO);
		if (!iter->ste) {
			dev_err(smmu->dev, "failed to allocate memory for stream table entry\n");
			goto free_mem;
		}
		iter->sid = master->sids[i];
		if (virt_arm_smmu_write_strtab_ent(master, iter))
			goto free_mem;
		iter++;
	}
	master->ste_cfg = ste_cfg;
	return 0;
free_mem:
	iter = ste_cfg;
	for (j = 0; j <= i; j++) {
		if (iter->ste)
			dmam_free_coherent(smmu->dev, (STRTAB_STE_DWORDS << 3),
				iter->ste, iter->stedma);
		iter++;
	}
	kfree(ste_cfg);
	return -ENOMEM;
}

static int virt_arm_smmu_attach_dev(struct iommu_domain *domain, struct device *dev)
{
	int ret = 0;
	unsigned long flags;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct virt_arm_smmu_device *smmu;
	struct virt_arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct virt_arm_smmu_master *master;

	if (!fwspec)
		return -ENOENT;

	master = dev_iommu_priv_get(dev);
	smmu = master->smmu;
	mutex_lock(&smmu_domain->init_mutex);

	if (!smmu_domain->smmu) {
		smmu_domain->smmu = smmu;
		ret = arm_smmu_domain_finalise(domain);
		if (ret) {
			smmu_domain->smmu = NULL;
			goto out_unlock;
		}
	} else if (smmu_domain->smmu != smmu) {
		dev_err(dev,
			"cannot attach to SMMU %s (upstream of %s)\n",
			dev_name(smmu_domain->smmu->dev),
			dev_name(smmu->dev));
		ret = -ENXIO;
		goto out_unlock;
	}

	master->domain = smmu_domain;

	virt_arm_smmu_write_ctx_desc(smmu, &smmu_domain->s1_cfg);
	ret = virt_arm_smmu_install_ste_for_dev(master);
	if (ret)
		goto out_unlock;

	spin_lock_irqsave(&smmu_domain->devices_lock, flags);
	list_add(&master->domain_head, &smmu_domain->devices);
	spin_unlock_irqrestore(&smmu_domain->devices_lock, flags);

out_unlock:
	mutex_unlock(&smmu_domain->init_mutex);
	return ret;
}

static void virt_arm_smmu_domain_free(struct iommu_domain *domain)
{
	struct virt_arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct virt_arm_smmu_device *smmu = smmu_domain->smmu;
	struct arm_smmu_s1_cfg *cfg = &smmu_domain->s1_cfg;

	free_io_pgtable_ops(smmu_domain->pgtbl_ops);

	if (cfg->cdptr) {
		dmam_free_coherent(smmu_domain->smmu->dev,
				   CTXDESC_CD_DWORDS << 3,
				   cfg->cdptr,
				   cfg->cdptr_dma);

		arm_smmu_bitmap_free(smmu->asid_map, cfg->cd.asid);
	}
	kfree(smmu_domain);
}

static struct iommu_domain *virt_arm_smmu_domain_alloc(unsigned int type)
{
	struct virt_arm_smmu_domain *smmu_domain;

	if (type != IOMMU_DOMAIN_UNMANAGED &&
	    type != IOMMU_DOMAIN_DMA &&
	    type != IOMMU_DOMAIN_IDENTITY)
		return NULL;

	smmu_domain = kzalloc(sizeof(*smmu_domain), GFP_KERNEL);
	if (!smmu_domain)
		return NULL;

	mutex_init(&smmu_domain->init_mutex);
	INIT_LIST_HEAD(&smmu_domain->devices);
	spin_lock_init(&smmu_domain->devices_lock);

	return &smmu_domain->domain;
}

static void virt_arm_smmu_tlb_inv_page_nosync(struct iommu_iotlb_gather *gather,
					 unsigned long iova, size_t granule,
					 void *cookie)
{
	struct virt_arm_smmu_domain *smmu_domain = cookie;
	struct iommu_domain *domain = &smmu_domain->domain;

	iommu_iotlb_gather_add_page(domain, gather, iova, granule);
}

static void virt_arm_smmu_tlb_inv_walk(unsigned long iova, size_t size,
				  size_t granule, void *cookie)
{
	virt_arm_smmu_tlb_inv_sync(iova, size, granule, false, cookie);
}

static const struct iommu_flush_ops virt_arm_smmu_flush_ops = {
	.tlb_flush_all	= virt_arm_smmu_tlb_inv_context,
	.tlb_flush_walk = virt_arm_smmu_tlb_inv_walk,
	.tlb_add_page	= virt_arm_smmu_tlb_inv_page_nosync,
};

static bool virt_arm_smmu_capable(struct device *dev, enum iommu_cap cap)
{
	switch (cap) {
	case IOMMU_CAP_CACHE_COHERENCY:
		return true;
	case IOMMU_CAP_NOEXEC:
		return true;
	default:
		return false;
	}
}

static int virt_arm_smmu_def_domain_type(struct device *dev)
{
	return IOMMU_DOMAIN_DMA;
}

#define MAX_MAP_SG_BATCH_SIZE (SZ_4M)

static struct iommu_ops  virt_arm_smmu_ops = {
		.capable		= virt_arm_smmu_capable,
		.domain_alloc		= virt_arm_smmu_domain_alloc,
		.probe_device		= virt_arm_smmu_add_device,
		.release_device		= virt_arm_smmu_remove_device,
		.device_group		= virt_arm_smmu_device_group,
		.of_xlate		= virt_arm_smmu_of_xlate,
		.get_resv_regions	= virt_arm_smmu_get_resv_regions,
		.pgsize_bitmap		= -1UL, /* Restricted during device attach */
		.def_domain_type        = virt_arm_smmu_def_domain_type,
		.owner                  = THIS_MODULE,
		.default_domain_ops = &(const struct iommu_domain_ops) {
			.attach_dev		= virt_arm_smmu_attach_dev,
			.detach_dev             = virt_arm_smmu_detach_dev,
			.free                   = virt_arm_smmu_domain_free,
			.map                    = virt_arm_smmu_map,
			.unmap                  = virt_arm_smmu_unmap,
			.flush_iotlb_all        = virt_arm_smmu_flush_iotlb_all,
			.iotlb_sync             = virt_arm_smmu_iotlb_sync,
			.iova_to_phys           = virt_arm_smmu_iova_to_phys,
	}
};

static void virt_arm_smmu_iommu_pcie_device_probe(void *data, struct iommu_device *iommu,
					struct bus_type *bus, bool *skip)
{
	if (iommu != (struct iommu_device *)data)
		return;
	*skip = strcmp(bus->name, "pci");
}

static int virt_arm_smmu_device_probe(struct platform_device *pdev)
{
	int ret;
	struct virt_arm_smmu_device *smmu;
	struct device *dev = &pdev->dev;

	smmu = devm_kzalloc(dev, sizeof(*smmu), GFP_KERNEL);
	if (!smmu)
		return -ENOMEM;
	smmu->dev = dev;
	smmu->ias = 48;
	smmu->oas = 48;
	smmu->asid_bits = 16;
	smmu->vmid_bits = 16;
	smmu->pgsize_bitmap |= SZ_4K | SZ_2M | SZ_1G;
	virt_arm_smmu_ops.pgsize_bitmap = smmu->pgsize_bitmap;
	if (dma_set_mask_and_coherent(smmu->dev, DMA_BIT_MASK(smmu->oas)))
		dev_warn(smmu->dev, "failed to set DMA mask for table walker\n");
	/* Record our private device structure */
	platform_set_drvdata(pdev, smmu);
	ret = iommu_device_sysfs_add(&smmu->iommu, dev, NULL,
				     "virt-smmuv3");
	if (ret)
		return ret;

	register_trace_android_vh_bus_iommu_probe(virt_arm_smmu_iommu_pcie_device_probe,
						(void *)&smmu->iommu);

	ret = iommu_device_register(&smmu->iommu, &virt_arm_smmu_ops, dev);
	if (ret) {
		dev_err(dev, "Failed to register iommu\n");
		return ret;
	}
	platform_set_drvdata(pdev, smmu);
	return 0;
}

static int virt_arm_smmu_device_remove(struct platform_device *pdev)
{
	struct virt_arm_smmu_device *smmu = platform_get_drvdata(pdev);

	iommu_device_unregister(&smmu->iommu);
	iommu_device_sysfs_remove(&smmu->iommu);

	return 0;
}

static const struct of_device_id virt_arm_smmu_of_match[] = {
	{ .compatible = "arm,virt-smmu-v3", },
	{ },
};
MODULE_DEVICE_TABLE(of, virt_arm_smmu_of_match);

static struct platform_driver virt_arm_smmu_driver = {
	.driver	= {
		.name			= "arm,virt-smmu-v3",
		.of_match_table		= of_match_ptr(virt_arm_smmu_of_match),
		.suppress_bind_attrs    = true,
	},
	.probe	= virt_arm_smmu_device_probe,
	.remove	= virt_arm_smmu_device_remove,
};

static int __init virt_arm_smmu_init(void)
{
	return platform_driver_register(&virt_arm_smmu_driver);
}
arch_initcall(virt_arm_smmu_init);

static void __exit virt_arm_smmu_exit(void)
{
	platform_driver_unregister(&virt_arm_smmu_driver);
}
module_exit(virt_arm_smmu_exit);

MODULE_DESCRIPTION("Paravirtualized-IOMMU API for ARM architected SMMU-v3 implementations");
MODULE_LICENSE("GPL");
