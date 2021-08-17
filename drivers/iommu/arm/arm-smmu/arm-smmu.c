// SPDX-License-Identifier: GPL-2.0-only
/*
 * IOMMU API for ARM architected SMMU implementations.
 *
 * Copyright (C) 2013 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 *
 * This driver currently supports:
 *	- SMMUv1 and v2 implementations
 *	- Stream-matching and stream-indexing
 *	- v7/v8 long-descriptor format
 *	- Non-secure access to the SMMU
 *	- Context fault reporting
 *	- Extended Stream ID (16 bit)
 */

#define pr_fmt(fmt) "arm-smmu: " fmt

#include <linux/acpi.h>
#include <linux/acpi_iort.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/dma-iommu.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/ratelimit.h>
#include <linux/slab.h>

#include <linux/amba/bus.h>
#include <linux/fsl/mc.h>

#include "arm-smmu.h"

/*
 * Apparently, some Qualcomm arm64 platforms which appear to expose their SMMU
 * global register space are still, in fact, using a hypervisor to mediate it
 * by trapping and emulating register accesses. Sadly, some deployed versions
 * of said trapping code have bugs wherein they go horribly wrong for stores
 * using r31 (i.e. XZR/WZR) as the source register.
 */
#define QCOM_DUMMY_VAL -1

#define MSI_IOVA_BASE			0x8000000
#define MSI_IOVA_LENGTH			0x100000

static int force_stage;
module_param(force_stage, int, S_IRUGO);
MODULE_PARM_DESC(force_stage,
	"Force SMMU mappings to be installed at a particular stage of translation. A value of '1' or '2' forces the corresponding stage. All other values are ignored (i.e. no stage is forced). Note that selecting a specific stage will disable support for nested translation.");
static bool disable_bypass =
	IS_ENABLED(CONFIG_ARM_SMMU_DISABLE_BYPASS_BY_DEFAULT);
module_param(disable_bypass, bool, S_IRUGO);
MODULE_PARM_DESC(disable_bypass,
	"Disable bypass streams such that incoming transactions from devices that are not attached to an iommu domain will report an abort back to the device and will not be allowed to pass through the SMMU.");

#define s2cr_init_val (struct arm_smmu_s2cr){				\
	.type = disable_bypass ? S2CR_TYPE_FAULT : S2CR_TYPE_BYPASS,	\
}

static bool using_legacy_binding, using_generic_binding;

static inline int arm_smmu_rpm_get(struct arm_smmu_device *smmu)
{
	if (pm_runtime_enabled(smmu->dev))
		return pm_runtime_resume_and_get(smmu->dev);

	return 0;
}

static inline void arm_smmu_rpm_put(struct arm_smmu_device *smmu)
{
	if (pm_runtime_enabled(smmu->dev))
		pm_runtime_put_autosuspend(smmu->dev);
}

static struct arm_smmu_domain *to_smmu_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct arm_smmu_domain, domain);
}

static struct platform_driver arm_smmu_driver;
static struct iommu_ops arm_smmu_ops;

#ifdef CONFIG_ARM_SMMU_LEGACY_DT_BINDINGS
static int arm_smmu_bus_init(struct iommu_ops *ops);

static struct device_node *dev_get_dev_node(struct device *dev)
{
	if (dev_is_pci(dev)) {
		struct pci_bus *bus = to_pci_dev(dev)->bus;

		while (!pci_is_root_bus(bus))
			bus = bus->parent;
		return of_node_get(bus->bridge->parent->of_node);
	}

	return of_node_get(dev->of_node);
}

static int __arm_smmu_get_pci_sid(struct pci_dev *pdev, u16 alias, void *data)
{
	*((__be32 *)data) = cpu_to_be32(alias);
	return 0; /* Continue walking */
}

static int __find_legacy_master_phandle(struct device *dev, void *data)
{
	struct of_phandle_iterator *it = *(void **)data;
	struct device_node *np = it->node;
	int err;

	of_for_each_phandle(it, err, dev->of_node, "mmu-masters",
			    "#stream-id-cells", -1)
		if (it->node == np) {
			*(void **)data = dev;
			return 1;
		}
	it->node = np;
	return err == -ENOENT ? 0 : err;
}

static int arm_smmu_register_legacy_master(struct device *dev,
					   struct arm_smmu_device **smmu)
{
	struct device *smmu_dev;
	struct device_node *np;
	struct of_phandle_iterator it;
	void *data = &it;
	u32 *sids;
	__be32 pci_sid;
	int err;

	np = dev_get_dev_node(dev);
	if (!np || !of_find_property(np, "#stream-id-cells", NULL)) {
		of_node_put(np);
		return -ENODEV;
	}

	it.node = np;
	err = driver_for_each_device(&arm_smmu_driver.driver, NULL, &data,
				     __find_legacy_master_phandle);
	smmu_dev = data;
	of_node_put(np);
	if (err == 0)
		return -ENODEV;
	if (err < 0)
		return err;

	if (dev_is_pci(dev)) {
		/* "mmu-masters" assumes Stream ID == Requester ID */
		pci_for_each_dma_alias(to_pci_dev(dev), __arm_smmu_get_pci_sid,
				       &pci_sid);
		it.cur = &pci_sid;
		it.cur_count = 1;
	}

	err = iommu_fwspec_init(dev, &smmu_dev->of_node->fwnode,
				&arm_smmu_ops);
	if (err)
		return err;

	sids = kcalloc(it.cur_count, sizeof(*sids), GFP_KERNEL);
	if (!sids)
		return -ENOMEM;

	*smmu = dev_get_drvdata(smmu_dev);
	of_phandle_iterator_args(&it, sids, it.cur_count);
	err = iommu_fwspec_add_ids(dev, sids, it.cur_count);
	kfree(sids);
	return err;
}

/*
 * With the legacy DT binding in play, we have no guarantees about
 * probe order, but then we're also not doing default domains, so we can
 * delay setting bus ops until we're sure every possible SMMU is ready,
 * and that way ensure that no probe_device() calls get missed.
 */
static int arm_smmu_legacy_bus_init(void)
{
	if (using_legacy_binding)
		return arm_smmu_bus_init(&arm_smmu_ops);
	return 0;
}
device_initcall_sync(arm_smmu_legacy_bus_init);
#else
static int arm_smmu_register_legacy_master(struct device *dev,
					   struct arm_smmu_device **smmu)
{
	return -ENODEV;
}
#endif /* CONFIG_ARM_SMMU_LEGACY_DT_BINDINGS */

static void __arm_smmu_free_bitmap(unsigned long *map, int idx)
{
	clear_bit(idx, map);
}

/* Wait for any pending TLB invalidations to complete */
static void __arm_smmu_tlb_sync(struct arm_smmu_device *smmu, int page,
				int sync, int status)
{
	unsigned int spin_cnt, delay;
	u32 reg;

	if (smmu->impl && unlikely(smmu->impl->tlb_sync))
		return smmu->impl->tlb_sync(smmu, page, sync, status);

	arm_smmu_writel(smmu, page, sync, QCOM_DUMMY_VAL);
	for (delay = 1; delay < TLB_LOOP_TIMEOUT; delay *= 2) {
		for (spin_cnt = TLB_SPIN_COUNT; spin_cnt > 0; spin_cnt--) {
			reg = arm_smmu_readl(smmu, page, status);
			if (!(reg & ARM_SMMU_sTLBGSTATUS_GSACTIVE))
				return;
			cpu_relax();
		}
		udelay(delay);
	}
	dev_err_ratelimited(smmu->dev,
			    "TLB sync timed out -- SMMU may be deadlocked\n");
}

static void arm_smmu_tlb_sync_global(struct arm_smmu_device *smmu)
{
	unsigned long flags;

	spin_lock_irqsave(&smmu->global_sync_lock, flags);
	__arm_smmu_tlb_sync(smmu, ARM_SMMU_GR0, ARM_SMMU_GR0_sTLBGSYNC,
			    ARM_SMMU_GR0_sTLBGSTATUS);
	spin_unlock_irqrestore(&smmu->global_sync_lock, flags);
}

static void arm_smmu_tlb_sync_context(struct arm_smmu_domain *smmu_domain)
{
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	unsigned long flags;

	spin_lock_irqsave(&smmu_domain->cb_lock, flags);
	__arm_smmu_tlb_sync(smmu, ARM_SMMU_CB(smmu, smmu_domain->cfg.cbndx),
			    ARM_SMMU_CB_TLBSYNC, ARM_SMMU_CB_TLBSTATUS);
	spin_unlock_irqrestore(&smmu_domain->cb_lock, flags);
}

static void arm_smmu_tlb_inv_context_s1(void *cookie)
{
	struct arm_smmu_domain *smmu_domain = cookie;
	/*
	 * The TLBI write may be relaxed, so ensure that PTEs cleared by the
	 * current CPU are visible beforehand.
	 */
	wmb();
	arm_smmu_cb_write(smmu_domain->smmu, smmu_domain->cfg.cbndx,
			  ARM_SMMU_CB_S1_TLBIASID, smmu_domain->cfg.asid);
	arm_smmu_tlb_sync_context(smmu_domain);
}

static void arm_smmu_tlb_inv_context_s2(void *cookie)
{
	struct arm_smmu_domain *smmu_domain = cookie;
	struct arm_smmu_device *smmu = smmu_domain->smmu;

	/* See above */
	wmb();
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_TLBIVMID, smmu_domain->cfg.vmid);
	arm_smmu_tlb_sync_global(smmu);
}

static void arm_smmu_tlb_inv_range_s1(unsigned long iova, size_t size,
				      size_t granule, void *cookie, int reg)
{
	struct arm_smmu_domain *smmu_domain = cookie;
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	int idx = cfg->cbndx;

	if (smmu->features & ARM_SMMU_FEAT_COHERENT_WALK)
		wmb();

	if (cfg->fmt != ARM_SMMU_CTX_FMT_AARCH64) {
		iova = (iova >> 12) << 12;
		iova |= cfg->asid;
		do {
			arm_smmu_cb_write(smmu, idx, reg, iova);
			iova += granule;
		} while (size -= granule);
	} else {
		iova >>= 12;
		iova |= (u64)cfg->asid << 48;
		do {
			arm_smmu_cb_writeq(smmu, idx, reg, iova);
			iova += granule >> 12;
		} while (size -= granule);
	}
}

static void arm_smmu_tlb_inv_range_s2(unsigned long iova, size_t size,
				      size_t granule, void *cookie, int reg)
{
	struct arm_smmu_domain *smmu_domain = cookie;
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	int idx = smmu_domain->cfg.cbndx;

	if (smmu->features & ARM_SMMU_FEAT_COHERENT_WALK)
		wmb();

	iova >>= 12;
	do {
		if (smmu_domain->cfg.fmt == ARM_SMMU_CTX_FMT_AARCH64)
			arm_smmu_cb_writeq(smmu, idx, reg, iova);
		else
			arm_smmu_cb_write(smmu, idx, reg, iova);
		iova += granule >> 12;
	} while (size -= granule);
}

static void arm_smmu_tlb_inv_walk_s1(unsigned long iova, size_t size,
				     size_t granule, void *cookie)
{
	arm_smmu_tlb_inv_range_s1(iova, size, granule, cookie,
				  ARM_SMMU_CB_S1_TLBIVA);
	arm_smmu_tlb_sync_context(cookie);
}

static void arm_smmu_tlb_add_page_s1(struct iommu_iotlb_gather *gather,
				     unsigned long iova, size_t granule,
				     void *cookie)
{
	arm_smmu_tlb_inv_range_s1(iova, granule, granule, cookie,
				  ARM_SMMU_CB_S1_TLBIVAL);
}

static void arm_smmu_tlb_inv_walk_s2(unsigned long iova, size_t size,
				     size_t granule, void *cookie)
{
	arm_smmu_tlb_inv_range_s2(iova, size, granule, cookie,
				  ARM_SMMU_CB_S2_TLBIIPAS2);
	arm_smmu_tlb_sync_context(cookie);
}

static void arm_smmu_tlb_add_page_s2(struct iommu_iotlb_gather *gather,
				     unsigned long iova, size_t granule,
				     void *cookie)
{
	arm_smmu_tlb_inv_range_s2(iova, granule, granule, cookie,
				  ARM_SMMU_CB_S2_TLBIIPAS2L);
}

static void arm_smmu_tlb_inv_walk_s2_v1(unsigned long iova, size_t size,
					size_t granule, void *cookie)
{
	arm_smmu_tlb_inv_context_s2(cookie);
}
/*
 * On MMU-401 at least, the cost of firing off multiple TLBIVMIDs appears
 * almost negligible, but the benefit of getting the first one in as far ahead
 * of the sync as possible is significant, hence we don't just make this a
 * no-op and call arm_smmu_tlb_inv_context_s2() from .iotlb_sync as you might
 * think.
 */
static void arm_smmu_tlb_add_page_s2_v1(struct iommu_iotlb_gather *gather,
					unsigned long iova, size_t granule,
					void *cookie)
{
	struct arm_smmu_domain *smmu_domain = cookie;
	struct arm_smmu_device *smmu = smmu_domain->smmu;

	if (smmu->features & ARM_SMMU_FEAT_COHERENT_WALK)
		wmb();

	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_TLBIVMID, smmu_domain->cfg.vmid);
}

static const struct iommu_flush_ops arm_smmu_s1_tlb_ops = {
	.tlb_flush_all	= arm_smmu_tlb_inv_context_s1,
	.tlb_flush_walk	= arm_smmu_tlb_inv_walk_s1,
	.tlb_add_page	= arm_smmu_tlb_add_page_s1,
};

static const struct iommu_flush_ops arm_smmu_s2_tlb_ops_v2 = {
	.tlb_flush_all	= arm_smmu_tlb_inv_context_s2,
	.tlb_flush_walk	= arm_smmu_tlb_inv_walk_s2,
	.tlb_add_page	= arm_smmu_tlb_add_page_s2,
};

static const struct iommu_flush_ops arm_smmu_s2_tlb_ops_v1 = {
	.tlb_flush_all	= arm_smmu_tlb_inv_context_s2,
	.tlb_flush_walk	= arm_smmu_tlb_inv_walk_s2_v1,
	.tlb_add_page	= arm_smmu_tlb_add_page_s2_v1,
};

static irqreturn_t arm_smmu_context_fault(int irq, void *dev)
{
	u32 fsr, fsynr, cbfrsynra;
	unsigned long iova;
	struct iommu_domain *domain = dev;
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	int idx = smmu_domain->cfg.cbndx;
	int ret;

	fsr = arm_smmu_cb_read(smmu, idx, ARM_SMMU_CB_FSR);
	if (!(fsr & ARM_SMMU_FSR_FAULT))
		return IRQ_NONE;

	fsynr = arm_smmu_cb_read(smmu, idx, ARM_SMMU_CB_FSYNR0);
	iova = arm_smmu_cb_readq(smmu, idx, ARM_SMMU_CB_FAR);
	cbfrsynra = arm_smmu_gr1_read(smmu, ARM_SMMU_GR1_CBFRSYNRA(idx));

	ret = report_iommu_fault(domain, NULL, iova,
		fsynr & ARM_SMMU_FSYNR0_WNR ? IOMMU_FAULT_WRITE : IOMMU_FAULT_READ);

	if (ret == -ENOSYS)
		dev_err_ratelimited(smmu->dev,
		"Unhandled context fault: fsr=0x%x, iova=0x%08lx, fsynr=0x%x, cbfrsynra=0x%x, cb=%d\n",
			    fsr, iova, fsynr, cbfrsynra, idx);

	arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_FSR, fsr);
	return IRQ_HANDLED;
}

static irqreturn_t arm_smmu_global_fault(int irq, void *dev)
{
	u32 gfsr, gfsynr0, gfsynr1, gfsynr2;
	struct arm_smmu_device *smmu = dev;
	static DEFINE_RATELIMIT_STATE(rs, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);

	gfsr = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_sGFSR);
	gfsynr0 = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_sGFSYNR0);
	gfsynr1 = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_sGFSYNR1);
	gfsynr2 = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_sGFSYNR2);

	if (!gfsr)
		return IRQ_NONE;

	if (__ratelimit(&rs)) {
		if (IS_ENABLED(CONFIG_ARM_SMMU_DISABLE_BYPASS_BY_DEFAULT) &&
		    (gfsr & ARM_SMMU_sGFSR_USF))
			dev_err(smmu->dev,
				"Blocked unknown Stream ID 0x%hx; boot with \"arm-smmu.disable_bypass=0\" to allow, but this may have security implications\n",
				(u16)gfsynr1);
		else
			dev_err(smmu->dev,
				"Unexpected global fault, this could be serious\n");
		dev_err(smmu->dev,
			"\tGFSR 0x%08x, GFSYNR0 0x%08x, GFSYNR1 0x%08x, GFSYNR2 0x%08x\n",
			gfsr, gfsynr0, gfsynr1, gfsynr2);
	}

	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_sGFSR, gfsr);
	return IRQ_HANDLED;
}

static void arm_smmu_init_context_bank(struct arm_smmu_domain *smmu_domain,
				       struct io_pgtable_cfg *pgtbl_cfg)
{
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	struct arm_smmu_cb *cb = &smmu_domain->smmu->cbs[cfg->cbndx];
	bool stage1 = cfg->cbar != CBAR_TYPE_S2_TRANS;

	cb->cfg = cfg;

	/* TCR */
	if (stage1) {
		if (cfg->fmt == ARM_SMMU_CTX_FMT_AARCH32_S) {
			cb->tcr[0] = pgtbl_cfg->arm_v7s_cfg.tcr;
		} else {
			cb->tcr[0] = arm_smmu_lpae_tcr(pgtbl_cfg);
			cb->tcr[1] = arm_smmu_lpae_tcr2(pgtbl_cfg);
			if (cfg->fmt == ARM_SMMU_CTX_FMT_AARCH64)
				cb->tcr[1] |= ARM_SMMU_TCR2_AS;
			else
				cb->tcr[0] |= ARM_SMMU_TCR_EAE;
		}
	} else {
		cb->tcr[0] = arm_smmu_lpae_vtcr(pgtbl_cfg);
	}

	/* TTBRs */
	if (stage1) {
		if (cfg->fmt == ARM_SMMU_CTX_FMT_AARCH32_S) {
			cb->ttbr[0] = pgtbl_cfg->arm_v7s_cfg.ttbr;
			cb->ttbr[1] = 0;
		} else {
			cb->ttbr[0] = FIELD_PREP(ARM_SMMU_TTBRn_ASID,
						 cfg->asid);
			cb->ttbr[1] = FIELD_PREP(ARM_SMMU_TTBRn_ASID,
						 cfg->asid);

			if (pgtbl_cfg->quirks & IO_PGTABLE_QUIRK_ARM_TTBR1)
				cb->ttbr[1] |= pgtbl_cfg->arm_lpae_s1_cfg.ttbr;
			else
				cb->ttbr[0] |= pgtbl_cfg->arm_lpae_s1_cfg.ttbr;
		}
	} else {
		cb->ttbr[0] = pgtbl_cfg->arm_lpae_s2_cfg.vttbr;
	}

	/* MAIRs (stage-1 only) */
	if (stage1) {
		if (cfg->fmt == ARM_SMMU_CTX_FMT_AARCH32_S) {
			cb->mair[0] = pgtbl_cfg->arm_v7s_cfg.prrr;
			cb->mair[1] = pgtbl_cfg->arm_v7s_cfg.nmrr;
		} else {
			cb->mair[0] = pgtbl_cfg->arm_lpae_s1_cfg.mair;
			cb->mair[1] = pgtbl_cfg->arm_lpae_s1_cfg.mair >> 32;
		}
	}
}

void arm_smmu_write_context_bank(struct arm_smmu_device *smmu, int idx)
{
	u32 reg;
	bool stage1;
	struct arm_smmu_cb *cb = &smmu->cbs[idx];
	struct arm_smmu_cfg *cfg = cb->cfg;

	/* Unassigned context banks only need disabling */
	if (!cfg) {
		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_SCTLR, 0);
		return;
	}

	stage1 = cfg->cbar != CBAR_TYPE_S2_TRANS;

	/* CBA2R */
	if (smmu->version > ARM_SMMU_V1) {
		if (cfg->fmt == ARM_SMMU_CTX_FMT_AARCH64)
			reg = ARM_SMMU_CBA2R_VA64;
		else
			reg = 0;
		/* 16-bit VMIDs live in CBA2R */
		if (smmu->features & ARM_SMMU_FEAT_VMID16)
			reg |= FIELD_PREP(ARM_SMMU_CBA2R_VMID16, cfg->vmid);

		arm_smmu_gr1_write(smmu, ARM_SMMU_GR1_CBA2R(idx), reg);
	}

	/* CBAR */
	reg = FIELD_PREP(ARM_SMMU_CBAR_TYPE, cfg->cbar);
	if (smmu->version < ARM_SMMU_V2)
		reg |= FIELD_PREP(ARM_SMMU_CBAR_IRPTNDX, cfg->irptndx);

	/*
	 * Use the weakest shareability/memory types, so they are
	 * overridden by the ttbcr/pte.
	 */
	if (stage1) {
		reg |= FIELD_PREP(ARM_SMMU_CBAR_S1_BPSHCFG,
				  ARM_SMMU_CBAR_S1_BPSHCFG_NSH) |
		       FIELD_PREP(ARM_SMMU_CBAR_S1_MEMATTR,
				  ARM_SMMU_CBAR_S1_MEMATTR_WB);
	} else if (!(smmu->features & ARM_SMMU_FEAT_VMID16)) {
		/* 8-bit VMIDs live in CBAR */
		reg |= FIELD_PREP(ARM_SMMU_CBAR_VMID, cfg->vmid);
	}
	arm_smmu_gr1_write(smmu, ARM_SMMU_GR1_CBAR(idx), reg);

	/*
	 * TCR
	 * We must write this before the TTBRs, since it determines the
	 * access behaviour of some fields (in particular, ASID[15:8]).
	 */
	if (stage1 && smmu->version > ARM_SMMU_V1)
		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_TCR2, cb->tcr[1]);
	arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_TCR, cb->tcr[0]);

	/* TTBRs */
	if (cfg->fmt == ARM_SMMU_CTX_FMT_AARCH32_S) {
		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_CONTEXTIDR, cfg->asid);
		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_TTBR0, cb->ttbr[0]);
		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_TTBR1, cb->ttbr[1]);
	} else {
		arm_smmu_cb_writeq(smmu, idx, ARM_SMMU_CB_TTBR0, cb->ttbr[0]);
		if (stage1)
			arm_smmu_cb_writeq(smmu, idx, ARM_SMMU_CB_TTBR1,
					   cb->ttbr[1]);
	}

	/* MAIRs (stage-1 only) */
	if (stage1) {
		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_S1_MAIR0, cb->mair[0]);
		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_S1_MAIR1, cb->mair[1]);
	}

	/* SCTLR */
	reg = ARM_SMMU_SCTLR_CFIE | ARM_SMMU_SCTLR_CFRE | ARM_SMMU_SCTLR_AFE |
	      ARM_SMMU_SCTLR_TRE | ARM_SMMU_SCTLR_M;
	if (stage1)
		reg |= ARM_SMMU_SCTLR_S1_ASIDPNE;
	if (IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		reg |= ARM_SMMU_SCTLR_E;

	if (smmu->impl && smmu->impl->write_sctlr)
		smmu->impl->write_sctlr(smmu, idx, reg);
	else
		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_SCTLR, reg);
}

static int arm_smmu_alloc_context_bank(struct arm_smmu_domain *smmu_domain,
				       struct arm_smmu_device *smmu,
				       struct device *dev, unsigned int start)
{
	if (smmu->impl && smmu->impl->alloc_context_bank)
		return smmu->impl->alloc_context_bank(smmu_domain, smmu, dev, start);

	return __arm_smmu_alloc_bitmap(smmu->context_map, start, smmu->num_context_banks);
}

static int arm_smmu_init_domain_context(struct iommu_domain *domain,
					struct arm_smmu_device *smmu,
					struct device *dev)
{
	int irq, start, ret = 0;
	unsigned long ias, oas;
	struct io_pgtable_ops *pgtbl_ops;
	struct io_pgtable_cfg pgtbl_cfg;
	enum io_pgtable_fmt fmt;
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	irqreturn_t (*context_fault)(int irq, void *dev);

	mutex_lock(&smmu_domain->init_mutex);
	if (smmu_domain->smmu)
		goto out_unlock;

	if (domain->type == IOMMU_DOMAIN_IDENTITY) {
		smmu_domain->stage = ARM_SMMU_DOMAIN_BYPASS;
		smmu_domain->smmu = smmu;
		goto out_unlock;
	}

	/*
	 * Mapping the requested stage onto what we support is surprisingly
	 * complicated, mainly because the spec allows S1+S2 SMMUs without
	 * support for nested translation. That means we end up with the
	 * following table:
	 *
	 * Requested        Supported        Actual
	 *     S1               N              S1
	 *     S1             S1+S2            S1
	 *     S1               S2             S2
	 *     S1               S1             S1
	 *     N                N              N
	 *     N              S1+S2            S2
	 *     N                S2             S2
	 *     N                S1             S1
	 *
	 * Note that you can't actually request stage-2 mappings.
	 */
	if (!(smmu->features & ARM_SMMU_FEAT_TRANS_S1))
		smmu_domain->stage = ARM_SMMU_DOMAIN_S2;
	if (!(smmu->features & ARM_SMMU_FEAT_TRANS_S2))
		smmu_domain->stage = ARM_SMMU_DOMAIN_S1;

	/*
	 * Choosing a suitable context format is even more fiddly. Until we
	 * grow some way for the caller to express a preference, and/or move
	 * the decision into the io-pgtable code where it arguably belongs,
	 * just aim for the closest thing to the rest of the system, and hope
	 * that the hardware isn't esoteric enough that we can't assume AArch64
	 * support to be a superset of AArch32 support...
	 */
	if (smmu->features & ARM_SMMU_FEAT_FMT_AARCH32_L)
		cfg->fmt = ARM_SMMU_CTX_FMT_AARCH32_L;
	if (IS_ENABLED(CONFIG_IOMMU_IO_PGTABLE_ARMV7S) &&
	    !IS_ENABLED(CONFIG_64BIT) && !IS_ENABLED(CONFIG_ARM_LPAE) &&
	    (smmu->features & ARM_SMMU_FEAT_FMT_AARCH32_S) &&
	    (smmu_domain->stage == ARM_SMMU_DOMAIN_S1))
		cfg->fmt = ARM_SMMU_CTX_FMT_AARCH32_S;
	if ((IS_ENABLED(CONFIG_64BIT) || cfg->fmt == ARM_SMMU_CTX_FMT_NONE) &&
	    (smmu->features & (ARM_SMMU_FEAT_FMT_AARCH64_64K |
			       ARM_SMMU_FEAT_FMT_AARCH64_16K |
			       ARM_SMMU_FEAT_FMT_AARCH64_4K)))
		cfg->fmt = ARM_SMMU_CTX_FMT_AARCH64;

	if (cfg->fmt == ARM_SMMU_CTX_FMT_NONE) {
		ret = -EINVAL;
		goto out_unlock;
	}

	switch (smmu_domain->stage) {
	case ARM_SMMU_DOMAIN_S1:
		cfg->cbar = CBAR_TYPE_S1_TRANS_S2_BYPASS;
		start = smmu->num_s2_context_banks;
		ias = smmu->va_size;
		oas = smmu->ipa_size;
		if (cfg->fmt == ARM_SMMU_CTX_FMT_AARCH64) {
			fmt = ARM_64_LPAE_S1;
		} else if (cfg->fmt == ARM_SMMU_CTX_FMT_AARCH32_L) {
			fmt = ARM_32_LPAE_S1;
			ias = min(ias, 32UL);
			oas = min(oas, 40UL);
		} else {
			fmt = ARM_V7S;
			ias = min(ias, 32UL);
			oas = min(oas, 32UL);
		}
		smmu_domain->flush_ops = &arm_smmu_s1_tlb_ops;
		break;
	case ARM_SMMU_DOMAIN_NESTED:
		/*
		 * We will likely want to change this if/when KVM gets
		 * involved.
		 */
	case ARM_SMMU_DOMAIN_S2:
		cfg->cbar = CBAR_TYPE_S2_TRANS;
		start = 0;
		ias = smmu->ipa_size;
		oas = smmu->pa_size;
		if (cfg->fmt == ARM_SMMU_CTX_FMT_AARCH64) {
			fmt = ARM_64_LPAE_S2;
		} else {
			fmt = ARM_32_LPAE_S2;
			ias = min(ias, 40UL);
			oas = min(oas, 40UL);
		}
		if (smmu->version == ARM_SMMU_V2)
			smmu_domain->flush_ops = &arm_smmu_s2_tlb_ops_v2;
		else
			smmu_domain->flush_ops = &arm_smmu_s2_tlb_ops_v1;
		break;
	default:
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = arm_smmu_alloc_context_bank(smmu_domain, smmu, dev, start);
	if (ret < 0) {
		goto out_unlock;
	}

	smmu_domain->smmu = smmu;

	cfg->cbndx = ret;
	if (smmu->version < ARM_SMMU_V2) {
		cfg->irptndx = atomic_inc_return(&smmu->irptndx);
		cfg->irptndx %= smmu->num_context_irqs;
	} else {
		cfg->irptndx = cfg->cbndx;
	}

	if (smmu_domain->stage == ARM_SMMU_DOMAIN_S2)
		cfg->vmid = cfg->cbndx + 1;
	else
		cfg->asid = cfg->cbndx;

	pgtbl_cfg = (struct io_pgtable_cfg) {
		.pgsize_bitmap	= smmu->pgsize_bitmap,
		.ias		= ias,
		.oas		= oas,
		.coherent_walk	= smmu->features & ARM_SMMU_FEAT_COHERENT_WALK,
		.tlb		= smmu_domain->flush_ops,
		.iommu_dev	= smmu->dev,
	};

	if (!iommu_get_dma_strict(domain))
		pgtbl_cfg.quirks |= IO_PGTABLE_QUIRK_NON_STRICT;

	if (smmu->impl && smmu->impl->init_context) {
		ret = smmu->impl->init_context(smmu_domain, &pgtbl_cfg, dev);
		if (ret)
			goto out_clear_smmu;
	}

	if (smmu_domain->pgtbl_quirks)
		pgtbl_cfg.quirks |= smmu_domain->pgtbl_quirks;

	pgtbl_ops = alloc_io_pgtable_ops(fmt, &pgtbl_cfg, smmu_domain);
	if (!pgtbl_ops) {
		ret = -ENOMEM;
		goto out_clear_smmu;
	}

	/* Update the domain's page sizes to reflect the page table format */
	domain->pgsize_bitmap = pgtbl_cfg.pgsize_bitmap;

	if (pgtbl_cfg.quirks & IO_PGTABLE_QUIRK_ARM_TTBR1) {
		domain->geometry.aperture_start = ~0UL << ias;
		domain->geometry.aperture_end = ~0UL;
	} else {
		domain->geometry.aperture_end = (1UL << ias) - 1;
	}

	domain->geometry.force_aperture = true;

	/* Initialise the context bank with our page table cfg */
	arm_smmu_init_context_bank(smmu_domain, &pgtbl_cfg);
	arm_smmu_write_context_bank(smmu, cfg->cbndx);

	/*
	 * Request context fault interrupt. Do this last to avoid the
	 * handler seeing a half-initialised domain state.
	 */
	irq = smmu->irqs[smmu->num_global_irqs + cfg->irptndx];

	if (smmu->impl && smmu->impl->context_fault)
		context_fault = smmu->impl->context_fault;
	else
		context_fault = arm_smmu_context_fault;

	ret = devm_request_irq(smmu->dev, irq, context_fault,
			       IRQF_SHARED, "arm-smmu-context-fault", domain);
	if (ret < 0) {
		dev_err(smmu->dev, "failed to request context IRQ %d (%u)\n",
			cfg->irptndx, irq);
		cfg->irptndx = ARM_SMMU_INVALID_IRPTNDX;
	}

	mutex_unlock(&smmu_domain->init_mutex);

	/* Publish page table ops for map/unmap */
	smmu_domain->pgtbl_ops = pgtbl_ops;
	return 0;

out_clear_smmu:
	__arm_smmu_free_bitmap(smmu->context_map, cfg->cbndx);
	smmu_domain->smmu = NULL;
out_unlock:
	mutex_unlock(&smmu_domain->init_mutex);
	return ret;
}

static void arm_smmu_destroy_domain_context(struct iommu_domain *domain)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	int ret, irq;

	if (!smmu || domain->type == IOMMU_DOMAIN_IDENTITY)
		return;

	ret = arm_smmu_rpm_get(smmu);
	if (ret < 0)
		return;

	/*
	 * Disable the context bank and free the page tables before freeing
	 * it.
	 */
	smmu->cbs[cfg->cbndx].cfg = NULL;
	arm_smmu_write_context_bank(smmu, cfg->cbndx);

	if (cfg->irptndx != ARM_SMMU_INVALID_IRPTNDX) {
		irq = smmu->irqs[smmu->num_global_irqs + cfg->irptndx];
		devm_free_irq(smmu->dev, irq, domain);
	}

	free_io_pgtable_ops(smmu_domain->pgtbl_ops);
	__arm_smmu_free_bitmap(smmu->context_map, cfg->cbndx);

	arm_smmu_rpm_put(smmu);
}

static struct iommu_domain *arm_smmu_domain_alloc(unsigned type)
{
	struct arm_smmu_domain *smmu_domain;

	if (type != IOMMU_DOMAIN_UNMANAGED &&
	    type != IOMMU_DOMAIN_DMA &&
	    type != IOMMU_DOMAIN_IDENTITY)
		return NULL;
	/*
	 * Allocate the domain and initialise some of its data structures.
	 * We can't really do anything meaningful until we've added a
	 * master.
	 */
	smmu_domain = kzalloc(sizeof(*smmu_domain), GFP_KERNEL);
	if (!smmu_domain)
		return NULL;

	if (type == IOMMU_DOMAIN_DMA && (using_legacy_binding ||
	    iommu_get_dma_cookie(&smmu_domain->domain))) {
		kfree(smmu_domain);
		return NULL;
	}

	mutex_init(&smmu_domain->init_mutex);
	spin_lock_init(&smmu_domain->cb_lock);

	return &smmu_domain->domain;
}

static void arm_smmu_domain_free(struct iommu_domain *domain)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);

	/*
	 * Free the domain resources. We assume that all devices have
	 * already been detached.
	 */
	iommu_put_dma_cookie(domain);
	arm_smmu_destroy_domain_context(domain);
	kfree(smmu_domain);
}

static void arm_smmu_write_smr(struct arm_smmu_device *smmu, int idx)
{
	struct arm_smmu_smr *smr = smmu->smrs + idx;
	u32 reg = FIELD_PREP(ARM_SMMU_SMR_ID, smr->id) |
		  FIELD_PREP(ARM_SMMU_SMR_MASK, smr->mask);

	if (!(smmu->features & ARM_SMMU_FEAT_EXIDS) && smr->valid)
		reg |= ARM_SMMU_SMR_VALID;
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_SMR(idx), reg);
}

static void arm_smmu_write_s2cr(struct arm_smmu_device *smmu, int idx)
{
	struct arm_smmu_s2cr *s2cr = smmu->s2crs + idx;
	u32 reg;

	if (smmu->impl && smmu->impl->write_s2cr) {
		smmu->impl->write_s2cr(smmu, idx);
		return;
	}

	reg = FIELD_PREP(ARM_SMMU_S2CR_TYPE, s2cr->type) |
	      FIELD_PREP(ARM_SMMU_S2CR_CBNDX, s2cr->cbndx) |
	      FIELD_PREP(ARM_SMMU_S2CR_PRIVCFG, s2cr->privcfg);

	if (smmu->features & ARM_SMMU_FEAT_EXIDS && smmu->smrs &&
	    smmu->smrs[idx].valid)
		reg |= ARM_SMMU_S2CR_EXIDVALID;
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_S2CR(idx), reg);
}

static void arm_smmu_write_sme(struct arm_smmu_device *smmu, int idx)
{
	arm_smmu_write_s2cr(smmu, idx);
	if (smmu->smrs)
		arm_smmu_write_smr(smmu, idx);
}

/*
 * The width of SMR's mask field depends on sCR0_EXIDENABLE, so this function
 * should be called after sCR0 is written.
 */
static void arm_smmu_test_smr_masks(struct arm_smmu_device *smmu)
{
	u32 smr;
	int i;

	if (!smmu->smrs)
		return;
	/*
	 * If we've had to accommodate firmware memory regions, we may
	 * have live SMRs by now; tread carefully...
	 *
	 * Somewhat perversely, not having a free SMR for this test implies we
	 * can get away without it anyway, as we'll only be able to 'allocate'
	 * these SMRs for the ID/mask values we're already trusting to be OK.
	 */
	for (i = 0; i < smmu->num_mapping_groups; i++)
		if (!smmu->smrs[i].valid)
			goto smr_ok;
	return;
smr_ok:
	/*
	 * SMR.ID bits may not be preserved if the corresponding MASK
	 * bits are set, so check each one separately. We can reject
	 * masters later if they try to claim IDs outside these masks.
	 */
	smr = FIELD_PREP(ARM_SMMU_SMR_ID, smmu->streamid_mask);
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_SMR(i), smr);
	smr = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_SMR(i));
	smmu->streamid_mask = FIELD_GET(ARM_SMMU_SMR_ID, smr);

	smr = FIELD_PREP(ARM_SMMU_SMR_MASK, smmu->streamid_mask);
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_SMR(i), smr);
	smr = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_SMR(i));
	smmu->smr_mask_mask = FIELD_GET(ARM_SMMU_SMR_MASK, smr);
}

static int arm_smmu_find_sme(struct arm_smmu_device *smmu, u16 id, u16 mask)
{
	struct arm_smmu_smr *smrs = smmu->smrs;
	int i, free_idx = -ENOSPC;

	/* Stream indexing is blissfully easy */
	if (!smrs)
		return id;

	/* Validating SMRs is... less so */
	for (i = 0; i < smmu->num_mapping_groups; ++i) {
		if (!smrs[i].valid) {
			/*
			 * Note the first free entry we come across, which
			 * we'll claim in the end if nothing else matches.
			 */
			if (free_idx < 0)
				free_idx = i;
			continue;
		}
		/*
		 * If the new entry is _entirely_ matched by an existing entry,
		 * then reuse that, with the guarantee that there also cannot
		 * be any subsequent conflicting entries. In normal use we'd
		 * expect simply identical entries for this case, but there's
		 * no harm in accommodating the generalisation.
		 */
		if ((mask & smrs[i].mask) == mask &&
		    !((id ^ smrs[i].id) & ~smrs[i].mask))
			return i;
		/*
		 * If the new entry has any other overlap with an existing one,
		 * though, then there always exists at least one stream ID
		 * which would cause a conflict, and we can't allow that risk.
		 */
		if (!((id ^ smrs[i].id) & ~(smrs[i].mask | mask)))
			return -EINVAL;
	}

	return free_idx;
}

static bool arm_smmu_free_sme(struct arm_smmu_device *smmu, int idx)
{
	if (--smmu->s2crs[idx].count)
		return false;

	smmu->s2crs[idx] = s2cr_init_val;
	if (smmu->smrs)
		smmu->smrs[idx].valid = false;

	return true;
}

static int arm_smmu_master_alloc_smes(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct arm_smmu_master_cfg *cfg = dev_iommu_priv_get(dev);
	struct arm_smmu_device *smmu = cfg->smmu;
	struct arm_smmu_smr *smrs = smmu->smrs;
	int i, idx, ret;

	mutex_lock(&smmu->stream_map_mutex);
	/* Figure out a viable stream map entry allocation */
	for_each_cfg_sme(cfg, fwspec, i, idx) {
		u16 sid = FIELD_GET(ARM_SMMU_SMR_ID, fwspec->ids[i]);
		u16 mask = FIELD_GET(ARM_SMMU_SMR_MASK, fwspec->ids[i]);

		if (idx != INVALID_SMENDX) {
			ret = -EEXIST;
			goto out_err;
		}

		ret = arm_smmu_find_sme(smmu, sid, mask);
		if (ret < 0)
			goto out_err;

		idx = ret;
		if (smrs && smmu->s2crs[idx].count == 0) {
			smrs[idx].id = sid;
			smrs[idx].mask = mask;
			smrs[idx].valid = true;
		}
		smmu->s2crs[idx].count++;
		cfg->smendx[i] = (s16)idx;
	}

	/* It worked! Now, poke the actual hardware */
	for_each_cfg_sme(cfg, fwspec, i, idx)
		arm_smmu_write_sme(smmu, idx);

	mutex_unlock(&smmu->stream_map_mutex);
	return 0;

out_err:
	while (i--) {
		arm_smmu_free_sme(smmu, cfg->smendx[i]);
		cfg->smendx[i] = INVALID_SMENDX;
	}
	mutex_unlock(&smmu->stream_map_mutex);
	return ret;
}

static void arm_smmu_master_free_smes(struct arm_smmu_master_cfg *cfg,
				      struct iommu_fwspec *fwspec)
{
	struct arm_smmu_device *smmu = cfg->smmu;
	int i, idx;

	mutex_lock(&smmu->stream_map_mutex);
	for_each_cfg_sme(cfg, fwspec, i, idx) {
		if (arm_smmu_free_sme(smmu, idx))
			arm_smmu_write_sme(smmu, idx);
		cfg->smendx[i] = INVALID_SMENDX;
	}
	mutex_unlock(&smmu->stream_map_mutex);
}

static int arm_smmu_domain_add_master(struct arm_smmu_domain *smmu_domain,
				      struct arm_smmu_master_cfg *cfg,
				      struct iommu_fwspec *fwspec)
{
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	struct arm_smmu_s2cr *s2cr = smmu->s2crs;
	u8 cbndx = smmu_domain->cfg.cbndx;
	enum arm_smmu_s2cr_type type;
	int i, idx;

	if (smmu_domain->stage == ARM_SMMU_DOMAIN_BYPASS)
		type = S2CR_TYPE_BYPASS;
	else
		type = S2CR_TYPE_TRANS;

	for_each_cfg_sme(cfg, fwspec, i, idx) {
		if (type == s2cr[idx].type && cbndx == s2cr[idx].cbndx)
			continue;

		s2cr[idx].type = type;
		s2cr[idx].privcfg = S2CR_PRIVCFG_DEFAULT;
		s2cr[idx].cbndx = cbndx;
		arm_smmu_write_s2cr(smmu, idx);
	}
	return 0;
}

static int arm_smmu_attach_dev(struct iommu_domain *domain, struct device *dev)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct arm_smmu_master_cfg *cfg;
	struct arm_smmu_device *smmu;
	int ret;

	if (!fwspec || fwspec->ops != &arm_smmu_ops) {
		dev_err(dev, "cannot attach to SMMU, is it on the same bus?\n");
		return -ENXIO;
	}

	/*
	 * FIXME: The arch/arm DMA API code tries to attach devices to its own
	 * domains between of_xlate() and probe_device() - we have no way to cope
	 * with that, so until ARM gets converted to rely on groups and default
	 * domains, just say no (but more politely than by dereferencing NULL).
	 * This should be at least a WARN_ON once that's sorted.
	 */
	cfg = dev_iommu_priv_get(dev);
	if (!cfg)
		return -ENODEV;

	smmu = cfg->smmu;

	ret = arm_smmu_rpm_get(smmu);
	if (ret < 0)
		return ret;

	/* Ensure that the domain is finalised */
	ret = arm_smmu_init_domain_context(domain, smmu, dev);
	if (ret < 0)
		goto rpm_put;

	/*
	 * Sanity check the domain. We don't support domains across
	 * different SMMUs.
	 */
	if (smmu_domain->smmu != smmu) {
		dev_err(dev,
			"cannot attach to SMMU %s whilst already attached to domain on SMMU %s\n",
			dev_name(smmu_domain->smmu->dev), dev_name(smmu->dev));
		ret = -EINVAL;
		goto rpm_put;
	}

	/* Looks ok, so add the device to the domain */
	ret = arm_smmu_domain_add_master(smmu_domain, cfg, fwspec);

	/*
	 * Setup an autosuspend delay to avoid bouncing runpm state.
	 * Otherwise, if a driver for a suspended consumer device
	 * unmaps buffers, it will runpm resume/suspend for each one.
	 *
	 * For example, when used by a GPU device, when an application
	 * or game exits, it can trigger unmapping 100s or 1000s of
	 * buffers.  With a runpm cycle for each buffer, that adds up
	 * to 5-10sec worth of reprogramming the context bank, while
	 * the system appears to be locked up to the user.
	 */
	pm_runtime_set_autosuspend_delay(smmu->dev, 20);
	pm_runtime_use_autosuspend(smmu->dev);

rpm_put:
	arm_smmu_rpm_put(smmu);
	return ret;
}

static int arm_smmu_map(struct iommu_domain *domain, unsigned long iova,
			phys_addr_t paddr, size_t size, int prot, gfp_t gfp)
{
	struct io_pgtable_ops *ops = to_smmu_domain(domain)->pgtbl_ops;
	struct arm_smmu_device *smmu = to_smmu_domain(domain)->smmu;
	int ret;

	if (!ops)
		return -ENODEV;

	arm_smmu_rpm_get(smmu);
	ret = ops->map(ops, iova, paddr, size, prot, gfp);
	arm_smmu_rpm_put(smmu);

	return ret;
}

static size_t arm_smmu_unmap(struct iommu_domain *domain, unsigned long iova,
			     size_t size, struct iommu_iotlb_gather *gather)
{
	struct io_pgtable_ops *ops = to_smmu_domain(domain)->pgtbl_ops;
	struct arm_smmu_device *smmu = to_smmu_domain(domain)->smmu;
	size_t ret;

	if (!ops)
		return 0;

	arm_smmu_rpm_get(smmu);
	ret = ops->unmap(ops, iova, size, gather);
	arm_smmu_rpm_put(smmu);

	return ret;
}

static void arm_smmu_flush_iotlb_all(struct iommu_domain *domain)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct arm_smmu_device *smmu = smmu_domain->smmu;

	if (smmu_domain->flush_ops) {
		arm_smmu_rpm_get(smmu);
		smmu_domain->flush_ops->tlb_flush_all(smmu_domain);
		arm_smmu_rpm_put(smmu);
	}
}

static void arm_smmu_iotlb_sync(struct iommu_domain *domain,
				struct iommu_iotlb_gather *gather)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct arm_smmu_device *smmu = smmu_domain->smmu;

	if (!smmu)
		return;

	arm_smmu_rpm_get(smmu);
	if (smmu->version == ARM_SMMU_V2 ||
	    smmu_domain->stage == ARM_SMMU_DOMAIN_S1)
		arm_smmu_tlb_sync_context(smmu_domain);
	else
		arm_smmu_tlb_sync_global(smmu);
	arm_smmu_rpm_put(smmu);
}

static phys_addr_t arm_smmu_iova_to_phys_hard(struct iommu_domain *domain,
					      dma_addr_t iova)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	struct io_pgtable_ops *ops= smmu_domain->pgtbl_ops;
	struct device *dev = smmu->dev;
	void __iomem *reg;
	u32 tmp;
	u64 phys;
	unsigned long va, flags;
	int ret, idx = cfg->cbndx;
	phys_addr_t addr = 0;

	ret = arm_smmu_rpm_get(smmu);
	if (ret < 0)
		return 0;

	spin_lock_irqsave(&smmu_domain->cb_lock, flags);
	va = iova & ~0xfffUL;
	if (cfg->fmt == ARM_SMMU_CTX_FMT_AARCH64)
		arm_smmu_cb_writeq(smmu, idx, ARM_SMMU_CB_ATS1PR, va);
	else
		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_ATS1PR, va);

	reg = arm_smmu_page(smmu, ARM_SMMU_CB(smmu, idx)) + ARM_SMMU_CB_ATSR;
	if (readl_poll_timeout_atomic(reg, tmp, !(tmp & ARM_SMMU_ATSR_ACTIVE),
				      5, 50)) {
		spin_unlock_irqrestore(&smmu_domain->cb_lock, flags);
		dev_err(dev,
			"iova to phys timed out on %pad. Falling back to software table walk.\n",
			&iova);
		arm_smmu_rpm_put(smmu);
		return ops->iova_to_phys(ops, iova);
	}

	phys = arm_smmu_cb_readq(smmu, idx, ARM_SMMU_CB_PAR);
	spin_unlock_irqrestore(&smmu_domain->cb_lock, flags);
	if (phys & ARM_SMMU_CB_PAR_F) {
		dev_err(dev, "translation fault!\n");
		dev_err(dev, "PAR = 0x%llx\n", phys);
		goto out;
	}

	addr = (phys & GENMASK_ULL(39, 12)) | (iova & 0xfff);
out:
	arm_smmu_rpm_put(smmu);

	return addr;
}

static phys_addr_t arm_smmu_iova_to_phys(struct iommu_domain *domain,
					dma_addr_t iova)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct io_pgtable_ops *ops = smmu_domain->pgtbl_ops;

	if (domain->type == IOMMU_DOMAIN_IDENTITY)
		return iova;

	if (!ops)
		return 0;

	if (smmu_domain->smmu->features & ARM_SMMU_FEAT_TRANS_OPS &&
			smmu_domain->stage == ARM_SMMU_DOMAIN_S1)
		return arm_smmu_iova_to_phys_hard(domain, iova);

	return ops->iova_to_phys(ops, iova);
}

static bool arm_smmu_capable(enum iommu_cap cap)
{
	switch (cap) {
	case IOMMU_CAP_CACHE_COHERENCY:
		/*
		 * Return true here as the SMMU can always send out coherent
		 * requests.
		 */
		return true;
	case IOMMU_CAP_NOEXEC:
		return true;
	default:
		return false;
	}
}

static
struct arm_smmu_device *arm_smmu_get_by_fwnode(struct fwnode_handle *fwnode)
{
	struct device *dev = driver_find_device_by_fwnode(&arm_smmu_driver.driver,
							  fwnode);
	put_device(dev);
	return dev ? dev_get_drvdata(dev) : NULL;
}

static struct iommu_device *arm_smmu_probe_device(struct device *dev)
{
	struct arm_smmu_device *smmu = NULL;
	struct arm_smmu_master_cfg *cfg;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	int i, ret;

	if (using_legacy_binding) {
		ret = arm_smmu_register_legacy_master(dev, &smmu);

		/*
		 * If dev->iommu_fwspec is initally NULL, arm_smmu_register_legacy_master()
		 * will allocate/initialise a new one. Thus we need to update fwspec for
		 * later use.
		 */
		fwspec = dev_iommu_fwspec_get(dev);
		if (ret)
			goto out_free;
	} else if (fwspec && fwspec->ops == &arm_smmu_ops) {
		smmu = arm_smmu_get_by_fwnode(fwspec->iommu_fwnode);
	} else {
		return ERR_PTR(-ENODEV);
	}

	ret = -EINVAL;
	for (i = 0; i < fwspec->num_ids; i++) {
		u16 sid = FIELD_GET(ARM_SMMU_SMR_ID, fwspec->ids[i]);
		u16 mask = FIELD_GET(ARM_SMMU_SMR_MASK, fwspec->ids[i]);

		if (sid & ~smmu->streamid_mask) {
			dev_err(dev, "stream ID 0x%x out of range for SMMU (0x%x)\n",
				sid, smmu->streamid_mask);
			goto out_free;
		}
		if (mask & ~smmu->smr_mask_mask) {
			dev_err(dev, "SMR mask 0x%x out of range for SMMU (0x%x)\n",
				mask, smmu->smr_mask_mask);
			goto out_free;
		}
	}

	ret = -ENOMEM;
	cfg = kzalloc(offsetof(struct arm_smmu_master_cfg, smendx[i]),
		      GFP_KERNEL);
	if (!cfg)
		goto out_free;

	cfg->smmu = smmu;
	dev_iommu_priv_set(dev, cfg);
	while (i--)
		cfg->smendx[i] = INVALID_SMENDX;

	ret = arm_smmu_rpm_get(smmu);
	if (ret < 0)
		goto out_cfg_free;

	ret = arm_smmu_master_alloc_smes(dev);
	arm_smmu_rpm_put(smmu);

	if (ret)
		goto out_cfg_free;

	device_link_add(dev, smmu->dev,
			DL_FLAG_PM_RUNTIME | DL_FLAG_AUTOREMOVE_SUPPLIER);

	return &smmu->iommu;

out_cfg_free:
	kfree(cfg);
out_free:
	iommu_fwspec_free(dev);
	return ERR_PTR(ret);
}

static void arm_smmu_release_device(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct arm_smmu_master_cfg *cfg;
	struct arm_smmu_device *smmu;
	int ret;

	if (!fwspec || fwspec->ops != &arm_smmu_ops)
		return;

	cfg  = dev_iommu_priv_get(dev);
	smmu = cfg->smmu;

	ret = arm_smmu_rpm_get(smmu);
	if (ret < 0)
		return;

	arm_smmu_master_free_smes(cfg, fwspec);

	arm_smmu_rpm_put(smmu);

	dev_iommu_priv_set(dev, NULL);
	kfree(cfg);
	iommu_fwspec_free(dev);
}

static void arm_smmu_probe_finalize(struct device *dev)
{
	struct arm_smmu_master_cfg *cfg;
	struct arm_smmu_device *smmu;

	cfg = dev_iommu_priv_get(dev);
	smmu = cfg->smmu;

	if (smmu->impl && smmu->impl->probe_finalize)
		smmu->impl->probe_finalize(smmu, dev);
}

static struct iommu_group *arm_smmu_device_group(struct device *dev)
{
	struct arm_smmu_master_cfg *cfg = dev_iommu_priv_get(dev);
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct arm_smmu_device *smmu = cfg->smmu;
	struct iommu_group *group = NULL;
	int i, idx;

	for_each_cfg_sme(cfg, fwspec, i, idx) {
		if (group && smmu->s2crs[idx].group &&
		    group != smmu->s2crs[idx].group)
			return ERR_PTR(-EINVAL);

		group = smmu->s2crs[idx].group;
	}

	if (group)
		return iommu_group_ref_get(group);

	if (dev_is_pci(dev))
		group = pci_device_group(dev);
	else if (dev_is_fsl_mc(dev))
		group = fsl_mc_device_group(dev);
	else
		group = generic_device_group(dev);

	/* Remember group for faster lookups */
	if (!IS_ERR(group))
		for_each_cfg_sme(cfg, fwspec, i, idx)
			smmu->s2crs[idx].group = group;

	return group;
}

static int arm_smmu_enable_nesting(struct iommu_domain *domain)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	int ret = 0;

	mutex_lock(&smmu_domain->init_mutex);
	if (smmu_domain->smmu)
		ret = -EPERM;
	else
		smmu_domain->stage = ARM_SMMU_DOMAIN_NESTED;
	mutex_unlock(&smmu_domain->init_mutex);

	return ret;
}

static int arm_smmu_set_pgtable_quirks(struct iommu_domain *domain,
		unsigned long quirks)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	int ret = 0;

	mutex_lock(&smmu_domain->init_mutex);
	if (smmu_domain->smmu)
		ret = -EPERM;
	else
		smmu_domain->pgtbl_quirks = quirks;
	mutex_unlock(&smmu_domain->init_mutex);

	return ret;
}

static int arm_smmu_of_xlate(struct device *dev, struct of_phandle_args *args)
{
	u32 mask, fwid = 0;

	if (args->args_count > 0)
		fwid |= FIELD_PREP(ARM_SMMU_SMR_ID, args->args[0]);

	if (args->args_count > 1)
		fwid |= FIELD_PREP(ARM_SMMU_SMR_MASK, args->args[1]);
	else if (!of_property_read_u32(args->np, "stream-match-mask", &mask))
		fwid |= FIELD_PREP(ARM_SMMU_SMR_MASK, mask);

	return iommu_fwspec_add_ids(dev, &fwid, 1);
}

static void arm_smmu_get_resv_regions(struct device *dev,
				      struct list_head *head)
{
	struct iommu_resv_region *region;
	int prot = IOMMU_WRITE | IOMMU_NOEXEC | IOMMU_MMIO;

	region = iommu_alloc_resv_region(MSI_IOVA_BASE, MSI_IOVA_LENGTH,
					 prot, IOMMU_RESV_SW_MSI);
	if (!region)
		return;

	list_add_tail(&region->list, head);

	iommu_dma_get_resv_regions(dev, head);
}

static int arm_smmu_def_domain_type(struct device *dev)
{
	struct arm_smmu_master_cfg *cfg = dev_iommu_priv_get(dev);
	const struct arm_smmu_impl *impl = cfg->smmu->impl;

	if (impl && impl->def_domain_type)
		return impl->def_domain_type(dev);

	return 0;
}

static struct iommu_ops arm_smmu_ops = {
	.capable		= arm_smmu_capable,
	.domain_alloc		= arm_smmu_domain_alloc,
	.domain_free		= arm_smmu_domain_free,
	.attach_dev		= arm_smmu_attach_dev,
	.map			= arm_smmu_map,
	.unmap			= arm_smmu_unmap,
	.flush_iotlb_all	= arm_smmu_flush_iotlb_all,
	.iotlb_sync		= arm_smmu_iotlb_sync,
	.iova_to_phys		= arm_smmu_iova_to_phys,
	.probe_device		= arm_smmu_probe_device,
	.release_device		= arm_smmu_release_device,
	.probe_finalize		= arm_smmu_probe_finalize,
	.device_group		= arm_smmu_device_group,
	.enable_nesting		= arm_smmu_enable_nesting,
	.set_pgtable_quirks	= arm_smmu_set_pgtable_quirks,
	.of_xlate		= arm_smmu_of_xlate,
	.get_resv_regions	= arm_smmu_get_resv_regions,
	.put_resv_regions	= generic_iommu_put_resv_regions,
	.def_domain_type	= arm_smmu_def_domain_type,
	.pgsize_bitmap		= -1UL, /* Restricted during device attach */
	.owner			= THIS_MODULE,
};

static void arm_smmu_device_reset(struct arm_smmu_device *smmu)
{
	int i;
	u32 reg;

	/* clear global FSR */
	reg = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_sGFSR);
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_sGFSR, reg);

	/*
	 * Reset stream mapping groups: Initial values mark all SMRn as
	 * invalid and all S2CRn as bypass unless overridden.
	 */
	for (i = 0; i < smmu->num_mapping_groups; ++i)
		arm_smmu_write_sme(smmu, i);

	/* Make sure all context banks are disabled and clear CB_FSR  */
	for (i = 0; i < smmu->num_context_banks; ++i) {
		arm_smmu_write_context_bank(smmu, i);
		arm_smmu_cb_write(smmu, i, ARM_SMMU_CB_FSR, ARM_SMMU_FSR_FAULT);
	}

	/* Invalidate the TLB, just in case */
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_TLBIALLH, QCOM_DUMMY_VAL);
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_TLBIALLNSNH, QCOM_DUMMY_VAL);

	reg = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_sCR0);

	/* Enable fault reporting */
	reg |= (ARM_SMMU_sCR0_GFRE | ARM_SMMU_sCR0_GFIE |
		ARM_SMMU_sCR0_GCFGFRE | ARM_SMMU_sCR0_GCFGFIE);

	/* Disable TLB broadcasting. */
	reg |= (ARM_SMMU_sCR0_VMIDPNE | ARM_SMMU_sCR0_PTM);

	/* Enable client access, handling unmatched streams as appropriate */
	reg &= ~ARM_SMMU_sCR0_CLIENTPD;
	if (disable_bypass)
		reg |= ARM_SMMU_sCR0_USFCFG;
	else
		reg &= ~ARM_SMMU_sCR0_USFCFG;

	/* Disable forced broadcasting */
	reg &= ~ARM_SMMU_sCR0_FB;

	/* Don't upgrade barriers */
	reg &= ~(ARM_SMMU_sCR0_BSU);

	if (smmu->features & ARM_SMMU_FEAT_VMID16)
		reg |= ARM_SMMU_sCR0_VMID16EN;

	if (smmu->features & ARM_SMMU_FEAT_EXIDS)
		reg |= ARM_SMMU_sCR0_EXIDENABLE;

	if (smmu->impl && smmu->impl->reset)
		smmu->impl->reset(smmu);

	/* Push the button */
	arm_smmu_tlb_sync_global(smmu);
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_sCR0, reg);
}

static int arm_smmu_id_size_to_bits(int size)
{
	switch (size) {
	case 0:
		return 32;
	case 1:
		return 36;
	case 2:
		return 40;
	case 3:
		return 42;
	case 4:
		return 44;
	case 5:
	default:
		return 48;
	}
}

static int arm_smmu_device_cfg_probe(struct arm_smmu_device *smmu)
{
	unsigned int size;
	u32 id;
	bool cttw_reg, cttw_fw = smmu->features & ARM_SMMU_FEAT_COHERENT_WALK;
	int i, ret;

	dev_notice(smmu->dev, "probing hardware configuration...\n");
	dev_notice(smmu->dev, "SMMUv%d with:\n",
			smmu->version == ARM_SMMU_V2 ? 2 : 1);

	/* ID0 */
	id = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_ID0);

	/* Restrict available stages based on module parameter */
	if (force_stage == 1)
		id &= ~(ARM_SMMU_ID0_S2TS | ARM_SMMU_ID0_NTS);
	else if (force_stage == 2)
		id &= ~(ARM_SMMU_ID0_S1TS | ARM_SMMU_ID0_NTS);

	if (id & ARM_SMMU_ID0_S1TS) {
		smmu->features |= ARM_SMMU_FEAT_TRANS_S1;
		dev_notice(smmu->dev, "\tstage 1 translation\n");
	}

	if (id & ARM_SMMU_ID0_S2TS) {
		smmu->features |= ARM_SMMU_FEAT_TRANS_S2;
		dev_notice(smmu->dev, "\tstage 2 translation\n");
	}

	if (id & ARM_SMMU_ID0_NTS) {
		smmu->features |= ARM_SMMU_FEAT_TRANS_NESTED;
		dev_notice(smmu->dev, "\tnested translation\n");
	}

	if (!(smmu->features &
		(ARM_SMMU_FEAT_TRANS_S1 | ARM_SMMU_FEAT_TRANS_S2))) {
		dev_err(smmu->dev, "\tno translation support!\n");
		return -ENODEV;
	}

	if ((id & ARM_SMMU_ID0_S1TS) &&
	    ((smmu->version < ARM_SMMU_V2) || !(id & ARM_SMMU_ID0_ATOSNS))) {
		smmu->features |= ARM_SMMU_FEAT_TRANS_OPS;
		dev_notice(smmu->dev, "\taddress translation ops\n");
	}

	/*
	 * In order for DMA API calls to work properly, we must defer to what
	 * the FW says about coherency, regardless of what the hardware claims.
	 * Fortunately, this also opens up a workaround for systems where the
	 * ID register value has ended up configured incorrectly.
	 */
	cttw_reg = !!(id & ARM_SMMU_ID0_CTTW);
	if (cttw_fw || cttw_reg)
		dev_notice(smmu->dev, "\t%scoherent table walk\n",
			   cttw_fw ? "" : "non-");
	if (cttw_fw != cttw_reg)
		dev_notice(smmu->dev,
			   "\t(IDR0.CTTW overridden by FW configuration)\n");

	/* Max. number of entries we have for stream matching/indexing */
	if (smmu->version == ARM_SMMU_V2 && id & ARM_SMMU_ID0_EXIDS) {
		smmu->features |= ARM_SMMU_FEAT_EXIDS;
		size = 1 << 16;
	} else {
		size = 1 << FIELD_GET(ARM_SMMU_ID0_NUMSIDB, id);
	}
	smmu->streamid_mask = size - 1;
	if (id & ARM_SMMU_ID0_SMS) {
		smmu->features |= ARM_SMMU_FEAT_STREAM_MATCH;
		size = FIELD_GET(ARM_SMMU_ID0_NUMSMRG, id);
		if (size == 0) {
			dev_err(smmu->dev,
				"stream-matching supported, but no SMRs present!\n");
			return -ENODEV;
		}

		/* Zero-initialised to mark as invalid */
		smmu->smrs = devm_kcalloc(smmu->dev, size, sizeof(*smmu->smrs),
					  GFP_KERNEL);
		if (!smmu->smrs)
			return -ENOMEM;

		dev_notice(smmu->dev,
			   "\tstream matching with %u register groups", size);
	}
	/* s2cr->type == 0 means translation, so initialise explicitly */
	smmu->s2crs = devm_kmalloc_array(smmu->dev, size, sizeof(*smmu->s2crs),
					 GFP_KERNEL);
	if (!smmu->s2crs)
		return -ENOMEM;
	for (i = 0; i < size; i++)
		smmu->s2crs[i] = s2cr_init_val;

	smmu->num_mapping_groups = size;
	mutex_init(&smmu->stream_map_mutex);
	spin_lock_init(&smmu->global_sync_lock);

	if (smmu->version < ARM_SMMU_V2 ||
	    !(id & ARM_SMMU_ID0_PTFS_NO_AARCH32)) {
		smmu->features |= ARM_SMMU_FEAT_FMT_AARCH32_L;
		if (!(id & ARM_SMMU_ID0_PTFS_NO_AARCH32S))
			smmu->features |= ARM_SMMU_FEAT_FMT_AARCH32_S;
	}

	/* ID1 */
	id = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_ID1);
	smmu->pgshift = (id & ARM_SMMU_ID1_PAGESIZE) ? 16 : 12;

	/* Check for size mismatch of SMMU address space from mapped region */
	size = 1 << (FIELD_GET(ARM_SMMU_ID1_NUMPAGENDXB, id) + 1);
	if (smmu->numpage != 2 * size << smmu->pgshift)
		dev_warn(smmu->dev,
			"SMMU address space size (0x%x) differs from mapped region size (0x%x)!\n",
			2 * size << smmu->pgshift, smmu->numpage);
	/* Now properly encode NUMPAGE to subsequently derive SMMU_CB_BASE */
	smmu->numpage = size;

	smmu->num_s2_context_banks = FIELD_GET(ARM_SMMU_ID1_NUMS2CB, id);
	smmu->num_context_banks = FIELD_GET(ARM_SMMU_ID1_NUMCB, id);
	if (smmu->num_s2_context_banks > smmu->num_context_banks) {
		dev_err(smmu->dev, "impossible number of S2 context banks!\n");
		return -ENODEV;
	}
	dev_notice(smmu->dev, "\t%u context banks (%u stage-2 only)\n",
		   smmu->num_context_banks, smmu->num_s2_context_banks);
	smmu->cbs = devm_kcalloc(smmu->dev, smmu->num_context_banks,
				 sizeof(*smmu->cbs), GFP_KERNEL);
	if (!smmu->cbs)
		return -ENOMEM;

	/* ID2 */
	id = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_ID2);
	size = arm_smmu_id_size_to_bits(FIELD_GET(ARM_SMMU_ID2_IAS, id));
	smmu->ipa_size = size;

	/* The output mask is also applied for bypass */
	size = arm_smmu_id_size_to_bits(FIELD_GET(ARM_SMMU_ID2_OAS, id));
	smmu->pa_size = size;

	if (id & ARM_SMMU_ID2_VMID16)
		smmu->features |= ARM_SMMU_FEAT_VMID16;

	/*
	 * What the page table walker can address actually depends on which
	 * descriptor format is in use, but since a) we don't know that yet,
	 * and b) it can vary per context bank, this will have to do...
	 */
	if (dma_set_mask_and_coherent(smmu->dev, DMA_BIT_MASK(size)))
		dev_warn(smmu->dev,
			 "failed to set DMA mask for table walker\n");

	if (smmu->version < ARM_SMMU_V2) {
		smmu->va_size = smmu->ipa_size;
		if (smmu->version == ARM_SMMU_V1_64K)
			smmu->features |= ARM_SMMU_FEAT_FMT_AARCH64_64K;
	} else {
		size = FIELD_GET(ARM_SMMU_ID2_UBS, id);
		smmu->va_size = arm_smmu_id_size_to_bits(size);
		if (id & ARM_SMMU_ID2_PTFS_4K)
			smmu->features |= ARM_SMMU_FEAT_FMT_AARCH64_4K;
		if (id & ARM_SMMU_ID2_PTFS_16K)
			smmu->features |= ARM_SMMU_FEAT_FMT_AARCH64_16K;
		if (id & ARM_SMMU_ID2_PTFS_64K)
			smmu->features |= ARM_SMMU_FEAT_FMT_AARCH64_64K;
	}

	if (smmu->impl && smmu->impl->cfg_probe) {
		ret = smmu->impl->cfg_probe(smmu);
		if (ret)
			return ret;
	}

	/* Now we've corralled the various formats, what'll it do? */
	if (smmu->features & ARM_SMMU_FEAT_FMT_AARCH32_S)
		smmu->pgsize_bitmap |= SZ_4K | SZ_64K | SZ_1M | SZ_16M;
	if (smmu->features &
	    (ARM_SMMU_FEAT_FMT_AARCH32_L | ARM_SMMU_FEAT_FMT_AARCH64_4K))
		smmu->pgsize_bitmap |= SZ_4K | SZ_2M | SZ_1G;
	if (smmu->features & ARM_SMMU_FEAT_FMT_AARCH64_16K)
		smmu->pgsize_bitmap |= SZ_16K | SZ_32M;
	if (smmu->features & ARM_SMMU_FEAT_FMT_AARCH64_64K)
		smmu->pgsize_bitmap |= SZ_64K | SZ_512M;

	if (arm_smmu_ops.pgsize_bitmap == -1UL)
		arm_smmu_ops.pgsize_bitmap = smmu->pgsize_bitmap;
	else
		arm_smmu_ops.pgsize_bitmap |= smmu->pgsize_bitmap;
	dev_notice(smmu->dev, "\tSupported page sizes: 0x%08lx\n",
		   smmu->pgsize_bitmap);


	if (smmu->features & ARM_SMMU_FEAT_TRANS_S1)
		dev_notice(smmu->dev, "\tStage-1: %lu-bit VA -> %lu-bit IPA\n",
			   smmu->va_size, smmu->ipa_size);

	if (smmu->features & ARM_SMMU_FEAT_TRANS_S2)
		dev_notice(smmu->dev, "\tStage-2: %lu-bit IPA -> %lu-bit PA\n",
			   smmu->ipa_size, smmu->pa_size);

	return 0;
}

struct arm_smmu_match_data {
	enum arm_smmu_arch_version version;
	enum arm_smmu_implementation model;
};

#define ARM_SMMU_MATCH_DATA(name, ver, imp)	\
static const struct arm_smmu_match_data name = { .version = ver, .model = imp }

ARM_SMMU_MATCH_DATA(smmu_generic_v1, ARM_SMMU_V1, GENERIC_SMMU);
ARM_SMMU_MATCH_DATA(smmu_generic_v2, ARM_SMMU_V2, GENERIC_SMMU);
ARM_SMMU_MATCH_DATA(arm_mmu401, ARM_SMMU_V1_64K, GENERIC_SMMU);
ARM_SMMU_MATCH_DATA(arm_mmu500, ARM_SMMU_V2, ARM_MMU500);
ARM_SMMU_MATCH_DATA(cavium_smmuv2, ARM_SMMU_V2, CAVIUM_SMMUV2);
ARM_SMMU_MATCH_DATA(qcom_smmuv2, ARM_SMMU_V2, QCOM_SMMUV2);

static const struct of_device_id arm_smmu_of_match[] = {
	{ .compatible = "arm,smmu-v1", .data = &smmu_generic_v1 },
	{ .compatible = "arm,smmu-v2", .data = &smmu_generic_v2 },
	{ .compatible = "arm,mmu-400", .data = &smmu_generic_v1 },
	{ .compatible = "arm,mmu-401", .data = &arm_mmu401 },
	{ .compatible = "arm,mmu-500", .data = &arm_mmu500 },
	{ .compatible = "cavium,smmu-v2", .data = &cavium_smmuv2 },
	{ .compatible = "nvidia,smmu-500", .data = &arm_mmu500 },
	{ .compatible = "qcom,smmu-v2", .data = &qcom_smmuv2 },
	{ },
};
MODULE_DEVICE_TABLE(of, arm_smmu_of_match);

#ifdef CONFIG_ACPI
static int acpi_smmu_get_data(u32 model, struct arm_smmu_device *smmu)
{
	int ret = 0;

	switch (model) {
	case ACPI_IORT_SMMU_V1:
	case ACPI_IORT_SMMU_CORELINK_MMU400:
		smmu->version = ARM_SMMU_V1;
		smmu->model = GENERIC_SMMU;
		break;
	case ACPI_IORT_SMMU_CORELINK_MMU401:
		smmu->version = ARM_SMMU_V1_64K;
		smmu->model = GENERIC_SMMU;
		break;
	case ACPI_IORT_SMMU_V2:
		smmu->version = ARM_SMMU_V2;
		smmu->model = GENERIC_SMMU;
		break;
	case ACPI_IORT_SMMU_CORELINK_MMU500:
		smmu->version = ARM_SMMU_V2;
		smmu->model = ARM_MMU500;
		break;
	case ACPI_IORT_SMMU_CAVIUM_THUNDERX:
		smmu->version = ARM_SMMU_V2;
		smmu->model = CAVIUM_SMMUV2;
		break;
	default:
		ret = -ENODEV;
	}

	return ret;
}

static int arm_smmu_device_acpi_probe(struct platform_device *pdev,
				      struct arm_smmu_device *smmu)
{
	struct device *dev = smmu->dev;
	struct acpi_iort_node *node =
		*(struct acpi_iort_node **)dev_get_platdata(dev);
	struct acpi_iort_smmu *iort_smmu;
	int ret;

	/* Retrieve SMMU1/2 specific data */
	iort_smmu = (struct acpi_iort_smmu *)node->node_data;

	ret = acpi_smmu_get_data(iort_smmu->model, smmu);
	if (ret < 0)
		return ret;

	/* Ignore the configuration access interrupt */
	smmu->num_global_irqs = 1;

	if (iort_smmu->flags & ACPI_IORT_SMMU_COHERENT_WALK)
		smmu->features |= ARM_SMMU_FEAT_COHERENT_WALK;

	return 0;
}
#else
static inline int arm_smmu_device_acpi_probe(struct platform_device *pdev,
					     struct arm_smmu_device *smmu)
{
	return -ENODEV;
}
#endif

static int arm_smmu_device_dt_probe(struct platform_device *pdev,
				    struct arm_smmu_device *smmu)
{
	const struct arm_smmu_match_data *data;
	struct device *dev = &pdev->dev;
	bool legacy_binding;

	if (of_property_read_u32(dev->of_node, "#global-interrupts",
				 &smmu->num_global_irqs)) {
		dev_err(dev, "missing #global-interrupts property\n");
		return -ENODEV;
	}

	data = of_device_get_match_data(dev);
	smmu->version = data->version;
	smmu->model = data->model;

	legacy_binding = of_find_property(dev->of_node, "mmu-masters", NULL);
	if (legacy_binding && !using_generic_binding) {
		if (!using_legacy_binding) {
			pr_notice("deprecated \"mmu-masters\" DT property in use; %s support unavailable\n",
				  IS_ENABLED(CONFIG_ARM_SMMU_LEGACY_DT_BINDINGS) ? "DMA API" : "SMMU");
		}
		using_legacy_binding = true;
	} else if (!legacy_binding && !using_legacy_binding) {
		using_generic_binding = true;
	} else {
		dev_err(dev, "not probing due to mismatched DT properties\n");
		return -ENODEV;
	}

	if (of_dma_is_coherent(dev->of_node))
		smmu->features |= ARM_SMMU_FEAT_COHERENT_WALK;

	return 0;
}

static int arm_smmu_bus_init(struct iommu_ops *ops)
{
	int err;

	/* Oh, for a proper bus abstraction */
	if (!iommu_present(&platform_bus_type)) {
		err = bus_set_iommu(&platform_bus_type, ops);
		if (err)
			return err;
	}
#ifdef CONFIG_ARM_AMBA
	if (!iommu_present(&amba_bustype)) {
		err = bus_set_iommu(&amba_bustype, ops);
		if (err)
			goto err_reset_platform_ops;
	}
#endif
#ifdef CONFIG_PCI
	if (!iommu_present(&pci_bus_type)) {
		err = bus_set_iommu(&pci_bus_type, ops);
		if (err)
			goto err_reset_amba_ops;
	}
#endif
#ifdef CONFIG_FSL_MC_BUS
	if (!iommu_present(&fsl_mc_bus_type)) {
		err = bus_set_iommu(&fsl_mc_bus_type, ops);
		if (err)
			goto err_reset_pci_ops;
	}
#endif
	return 0;

err_reset_pci_ops: __maybe_unused;
#ifdef CONFIG_PCI
	bus_set_iommu(&pci_bus_type, NULL);
#endif
err_reset_amba_ops: __maybe_unused;
#ifdef CONFIG_ARM_AMBA
	bus_set_iommu(&amba_bustype, NULL);
#endif
err_reset_platform_ops: __maybe_unused;
	bus_set_iommu(&platform_bus_type, NULL);
	return err;
}

static int arm_smmu_device_probe(struct platform_device *pdev)
{
	struct resource *res;
	resource_size_t ioaddr;
	struct arm_smmu_device *smmu;
	struct device *dev = &pdev->dev;
	int num_irqs, i, err;
	irqreturn_t (*global_fault)(int irq, void *dev);

	smmu = devm_kzalloc(dev, sizeof(*smmu), GFP_KERNEL);
	if (!smmu) {
		dev_err(dev, "failed to allocate arm_smmu_device\n");
		return -ENOMEM;
	}
	smmu->dev = dev;

	if (dev->of_node)
		err = arm_smmu_device_dt_probe(pdev, smmu);
	else
		err = arm_smmu_device_acpi_probe(pdev, smmu);

	if (err)
		return err;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ioaddr = res->start;
	smmu->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(smmu->base))
		return PTR_ERR(smmu->base);
	/*
	 * The resource size should effectively match the value of SMMU_TOP;
	 * stash that temporarily until we know PAGESIZE to validate it with.
	 */
	smmu->numpage = resource_size(res);

	smmu = arm_smmu_impl_init(smmu);
	if (IS_ERR(smmu))
		return PTR_ERR(smmu);

	num_irqs = 0;
	while ((res = platform_get_resource(pdev, IORESOURCE_IRQ, num_irqs))) {
		num_irqs++;
		if (num_irqs > smmu->num_global_irqs)
			smmu->num_context_irqs++;
	}

	if (!smmu->num_context_irqs) {
		dev_err(dev, "found %d interrupts but expected at least %d\n",
			num_irqs, smmu->num_global_irqs + 1);
		return -ENODEV;
	}

	smmu->irqs = devm_kcalloc(dev, num_irqs, sizeof(*smmu->irqs),
				  GFP_KERNEL);
	if (!smmu->irqs) {
		dev_err(dev, "failed to allocate %d irqs\n", num_irqs);
		return -ENOMEM;
	}

	for (i = 0; i < num_irqs; ++i) {
		int irq = platform_get_irq(pdev, i);

		if (irq < 0)
			return -ENODEV;
		smmu->irqs[i] = irq;
	}

	err = devm_clk_bulk_get_all(dev, &smmu->clks);
	if (err < 0) {
		dev_err(dev, "failed to get clocks %d\n", err);
		return err;
	}
	smmu->num_clks = err;

	err = clk_bulk_prepare_enable(smmu->num_clks, smmu->clks);
	if (err)
		return err;

	err = arm_smmu_device_cfg_probe(smmu);
	if (err)
		return err;

	if (smmu->version == ARM_SMMU_V2) {
		if (smmu->num_context_banks > smmu->num_context_irqs) {
			dev_err(dev,
			      "found only %d context irq(s) but %d required\n",
			      smmu->num_context_irqs, smmu->num_context_banks);
			return -ENODEV;
		}

		/* Ignore superfluous interrupts */
		smmu->num_context_irqs = smmu->num_context_banks;
	}

	if (smmu->impl && smmu->impl->global_fault)
		global_fault = smmu->impl->global_fault;
	else
		global_fault = arm_smmu_global_fault;

	for (i = 0; i < smmu->num_global_irqs; ++i) {
		err = devm_request_irq(smmu->dev, smmu->irqs[i],
				       global_fault,
				       IRQF_SHARED,
				       "arm-smmu global fault",
				       smmu);
		if (err) {
			dev_err(dev, "failed to request global IRQ %d (%u)\n",
				i, smmu->irqs[i]);
			return err;
		}
	}

	err = iommu_device_sysfs_add(&smmu->iommu, smmu->dev, NULL,
				     "smmu.%pa", &ioaddr);
	if (err) {
		dev_err(dev, "Failed to register iommu in sysfs\n");
		return err;
	}

	err = iommu_device_register(&smmu->iommu, &arm_smmu_ops, dev);
	if (err) {
		dev_err(dev, "Failed to register iommu\n");
		goto err_sysfs_remove;
	}

	platform_set_drvdata(pdev, smmu);
	arm_smmu_device_reset(smmu);
	arm_smmu_test_smr_masks(smmu);

	/*
	 * We want to avoid touching dev->power.lock in fastpaths unless
	 * it's really going to do something useful - pm_runtime_enabled()
	 * can serve as an ideal proxy for that decision. So, conditionally
	 * enable pm_runtime.
	 */
	if (dev->pm_domain) {
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}

	/*
	 * For ACPI and generic DT bindings, an SMMU will be probed before
	 * any device which might need it, so we want the bus ops in place
	 * ready to handle default domain setup as soon as any SMMU exists.
	 */
	if (!using_legacy_binding) {
		err = arm_smmu_bus_init(&arm_smmu_ops);
		if (err)
			goto err_unregister_device;
	}

	return 0;

err_unregister_device:
	iommu_device_unregister(&smmu->iommu);
err_sysfs_remove:
	iommu_device_sysfs_remove(&smmu->iommu);
	return err;
}

static int arm_smmu_device_remove(struct platform_device *pdev)
{
	struct arm_smmu_device *smmu = platform_get_drvdata(pdev);

	if (!smmu)
		return -ENODEV;

	if (!bitmap_empty(smmu->context_map, ARM_SMMU_MAX_CBS))
		dev_notice(&pdev->dev, "disabling translation\n");

	arm_smmu_bus_init(NULL);
	iommu_device_unregister(&smmu->iommu);
	iommu_device_sysfs_remove(&smmu->iommu);

	arm_smmu_rpm_get(smmu);
	/* Turn the thing off */
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_sCR0, ARM_SMMU_sCR0_CLIENTPD);
	arm_smmu_rpm_put(smmu);

	if (pm_runtime_enabled(smmu->dev))
		pm_runtime_force_suspend(smmu->dev);
	else
		clk_bulk_disable(smmu->num_clks, smmu->clks);

	clk_bulk_unprepare(smmu->num_clks, smmu->clks);
	return 0;
}

static void arm_smmu_device_shutdown(struct platform_device *pdev)
{
	arm_smmu_device_remove(pdev);
}

static int __maybe_unused arm_smmu_runtime_resume(struct device *dev)
{
	struct arm_smmu_device *smmu = dev_get_drvdata(dev);
	int ret;

	ret = clk_bulk_enable(smmu->num_clks, smmu->clks);
	if (ret)
		return ret;

	arm_smmu_device_reset(smmu);

	return 0;
}

static int __maybe_unused arm_smmu_runtime_suspend(struct device *dev)
{
	struct arm_smmu_device *smmu = dev_get_drvdata(dev);

	clk_bulk_disable(smmu->num_clks, smmu->clks);

	return 0;
}

static int __maybe_unused arm_smmu_pm_resume(struct device *dev)
{
	if (pm_runtime_suspended(dev))
		return 0;

	return arm_smmu_runtime_resume(dev);
}

static int __maybe_unused arm_smmu_pm_suspend(struct device *dev)
{
	if (pm_runtime_suspended(dev))
		return 0;

	return arm_smmu_runtime_suspend(dev);
}

static const struct dev_pm_ops arm_smmu_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(arm_smmu_pm_suspend, arm_smmu_pm_resume)
	SET_RUNTIME_PM_OPS(arm_smmu_runtime_suspend,
			   arm_smmu_runtime_resume, NULL)
};

static struct platform_driver arm_smmu_driver = {
	.driver	= {
		.name			= "arm-smmu",
		.of_match_table		= arm_smmu_of_match,
		.pm			= &arm_smmu_pm_ops,
		.suppress_bind_attrs    = true,
	},
	.probe	= arm_smmu_device_probe,
	.remove	= arm_smmu_device_remove,
	.shutdown = arm_smmu_device_shutdown,
};
module_platform_driver(arm_smmu_driver);

MODULE_DESCRIPTION("IOMMU API for ARM architected SMMU implementations");
MODULE_AUTHOR("Will Deacon <will@kernel.org>");
MODULE_ALIAS("platform:arm-smmu");
MODULE_LICENSE("GPL v2");
