// SPDX-License-Identifier: GPL-2.0-only
// Miscellaneous Arm SMMU implementation and integration quirks
// Copyright (C) 2019 Arm Limited

#define pr_fmt(fmt) "arm-smmu: " fmt

#include <linux/bitfield.h>
#include <linux/of.h>

#include "arm-smmu.h"


static int arm_smmu_gr0_ns(int offset)
{
	switch (offset) {
	case ARM_SMMU_GR0_sCR0:
	case ARM_SMMU_GR0_sACR:
	case ARM_SMMU_GR0_sGFSR:
	case ARM_SMMU_GR0_sGFSYNR0:
	case ARM_SMMU_GR0_sGFSYNR1:
	case ARM_SMMU_GR0_sGFSYNR2:
		return offset + 0x400;
	default:
		return offset;
	}
}

static u32 arm_smmu_read_ns(struct arm_smmu_device *smmu, int page,
			    int offset)
{
	if (page == ARM_SMMU_GR0)
		offset = arm_smmu_gr0_ns(offset);
	return readl_relaxed(arm_smmu_page(smmu, page) + offset);
}

static void arm_smmu_write_ns(struct arm_smmu_device *smmu, int page,
			      int offset, u32 val)
{
	if (page == ARM_SMMU_GR0)
		offset = arm_smmu_gr0_ns(offset);
	writel_relaxed(val, arm_smmu_page(smmu, page) + offset);
}

/* Since we don't care for sGFAR, we can do without 64-bit accessors */
static const struct arm_smmu_impl calxeda_impl = {
	.read_reg = arm_smmu_read_ns,
	.write_reg = arm_smmu_write_ns,
};


struct cavium_smmu {
	struct arm_smmu_device smmu;
	u32 id_base;
};

static int cavium_cfg_probe(struct arm_smmu_device *smmu)
{
	static atomic_t context_count = ATOMIC_INIT(0);
	struct cavium_smmu *cs = container_of(smmu, struct cavium_smmu, smmu);
	/*
	 * Cavium CN88xx erratum #27704.
	 * Ensure ASID and VMID allocation is unique across all SMMUs in
	 * the system.
	 */
	cs->id_base = atomic_fetch_add(smmu->num_context_banks, &context_count);
	dev_notice(smmu->dev, "\tenabling workaround for Cavium erratum 27704\n");

	return 0;
}

static int cavium_init_context(struct arm_smmu_domain *smmu_domain,
		struct io_pgtable_cfg *pgtbl_cfg, struct device *dev)
{
	struct cavium_smmu *cs = container_of(smmu_domain->smmu,
					      struct cavium_smmu, smmu);

	if (smmu_domain->stage == ARM_SMMU_DOMAIN_S2)
		smmu_domain->cfg.vmid += cs->id_base;
	else
		smmu_domain->cfg.asid += cs->id_base;

	return 0;
}

static const struct arm_smmu_impl cavium_impl = {
	.cfg_probe = cavium_cfg_probe,
	.init_context = cavium_init_context,
};

static struct arm_smmu_device *cavium_smmu_impl_init(struct arm_smmu_device *smmu)
{
	struct cavium_smmu *cs;

	cs = devm_krealloc(smmu->dev, smmu, sizeof(*cs), GFP_KERNEL);
	if (!cs)
		return ERR_PTR(-ENOMEM);

	cs->smmu.impl = &cavium_impl;

	return &cs->smmu;
}


#define ARM_MMU500_ACTLR_CPRE		(1 << 1)

#define ARM_MMU500_ACR_CACHE_LOCK	(1 << 26)
#define ARM_MMU500_ACR_S2CRB_TLBEN	(1 << 10)
#define ARM_MMU500_ACR_SMTNMB_TLBEN	(1 << 8)

int arm_mmu500_reset(struct arm_smmu_device *smmu)
{
	u32 reg, major;
	int i;
	/*
	 * On MMU-500 r2p0 onwards we need to clear ACR.CACHE_LOCK before
	 * writes to the context bank ACTLRs will stick. And we just hope that
	 * Secure has also cleared SACR.CACHE_LOCK for this to take effect...
	 */
	reg = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_ID7);
	major = FIELD_GET(ARM_SMMU_ID7_MAJOR, reg);
	reg = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_sACR);
	if (major >= 2)
		reg &= ~ARM_MMU500_ACR_CACHE_LOCK;
	/*
	 * Allow unmatched Stream IDs to allocate bypass
	 * TLB entries for reduced latency.
	 */
	reg |= ARM_MMU500_ACR_SMTNMB_TLBEN | ARM_MMU500_ACR_S2CRB_TLBEN;
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_sACR, reg);

	/*
	 * Disable MMU-500's not-particularly-beneficial next-page
	 * prefetcher for the sake of errata #841119 and #826419.
	 */
	for (i = 0; i < smmu->num_context_banks; ++i) {
		reg = arm_smmu_cb_read(smmu, i, ARM_SMMU_CB_ACTLR);
		reg &= ~ARM_MMU500_ACTLR_CPRE;
		arm_smmu_cb_write(smmu, i, ARM_SMMU_CB_ACTLR, reg);
		reg = arm_smmu_cb_read(smmu, i, ARM_SMMU_CB_ACTLR);
		if (reg & ARM_MMU500_ACTLR_CPRE)
			dev_warn_once(smmu->dev, "Failed to disable prefetcher [errata #841119 and #826419], check ACR.CACHE_LOCK\n");
	}

	return 0;
}

static const struct arm_smmu_impl arm_mmu500_impl = {
	.reset = arm_mmu500_reset,
};

static u64 mrvl_mmu500_readq(struct arm_smmu_device *smmu, int page, int off)
{
	/*
	 * Marvell Armada-AP806 erratum #582743.
	 * Split all the readq to double readl
	 */
	return hi_lo_readq_relaxed(arm_smmu_page(smmu, page) + off);
}

static void mrvl_mmu500_writeq(struct arm_smmu_device *smmu, int page, int off,
			       u64 val)
{
	/*
	 * Marvell Armada-AP806 erratum #582743.
	 * Split all the writeq to double writel
	 */
	hi_lo_writeq_relaxed(val, arm_smmu_page(smmu, page) + off);
}

static int mrvl_mmu500_cfg_probe(struct arm_smmu_device *smmu)
{

	/*
	 * Armada-AP806 erratum #582743.
	 * Hide the SMMU_IDR2.PTFSv8 fields to sidestep the AArch64
	 * formats altogether and allow using 32 bits access on the
	 * interconnect.
	 */
	smmu->features &= ~(ARM_SMMU_FEAT_FMT_AARCH64_4K |
			    ARM_SMMU_FEAT_FMT_AARCH64_16K |
			    ARM_SMMU_FEAT_FMT_AARCH64_64K);

	return 0;
}

static const struct arm_smmu_impl mrvl_mmu500_impl = {
	.read_reg64 = mrvl_mmu500_readq,
	.write_reg64 = mrvl_mmu500_writeq,
	.cfg_probe = mrvl_mmu500_cfg_probe,
	.reset = arm_mmu500_reset,
};


struct arm_smmu_device *arm_smmu_impl_init(struct arm_smmu_device *smmu)
{
	const struct device_node *np = smmu->dev->of_node;

	/*
	 * Set the impl for model-specific implementation quirks first,
	 * such that platform integration quirks can pick it up and
	 * inherit from it if necessary.
	 */
	switch (smmu->model) {
	case ARM_MMU500:
		smmu->impl = &arm_mmu500_impl;
		break;
	case CAVIUM_SMMUV2:
		return cavium_smmu_impl_init(smmu);
	default:
		break;
	}

	/* This is implicitly MMU-400 */
	if (of_property_read_bool(np, "calxeda,smmu-secure-config-access"))
		smmu->impl = &calxeda_impl;

	if (of_device_is_compatible(np, "nvidia,tegra234-smmu") ||
	    of_device_is_compatible(np, "nvidia,tegra194-smmu") ||
	    of_device_is_compatible(np, "nvidia,tegra186-smmu"))
		return nvidia_smmu_impl_init(smmu);

	if (IS_ENABLED(CONFIG_ARM_SMMU_QCOM))
		smmu = qcom_smmu_impl_init(smmu);

	if (of_device_is_compatible(np, "marvell,ap806-smmu-500"))
		smmu->impl = &mrvl_mmu500_impl;

	return smmu;
}
