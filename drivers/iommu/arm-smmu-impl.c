// SPDX-License-Identifier: GPL-2.0-only
// Miscellaneous Arm SMMU implementation and integration quirks
// Copyright (C) 2019 Arm Limited

#define pr_fmt(fmt) "arm-smmu: " fmt

#include <linux/of.h>

#include "arm-smmu.h"


static int arm_smmu_gr0_ns(int offset)
{
	switch(offset) {
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
const struct arm_smmu_impl calxeda_impl = {
	.read_reg = arm_smmu_read_ns,
	.write_reg = arm_smmu_write_ns,
};


static int cavium_cfg_probe(struct arm_smmu_device *smmu)
{
	static atomic_t context_count = ATOMIC_INIT(0);
	/*
	 * Cavium CN88xx erratum #27704.
	 * Ensure ASID and VMID allocation is unique across all SMMUs in
	 * the system.
	 */
	smmu->cavium_id_base = atomic_fetch_add(smmu->num_context_banks,
						   &context_count);
	dev_notice(smmu->dev, "\tenabling workaround for Cavium erratum 27704\n");

	return 0;
}

const struct arm_smmu_impl cavium_impl = {
	.cfg_probe = cavium_cfg_probe,
};


struct arm_smmu_device *arm_smmu_impl_init(struct arm_smmu_device *smmu)
{
	/*
	 * We will inevitably have to combine model-specific implementation
	 * quirks with platform-specific integration quirks, but everything
	 * we currently support happens to work out as straightforward
	 * mutually-exclusive assignments.
	 */
	switch (smmu->model) {
	case CAVIUM_SMMUV2:
		smmu->impl = &cavium_impl;
		break;
	default:
		break;
	}

	if (of_property_read_bool(smmu->dev->of_node,
				  "calxeda,smmu-secure-config-access"))
		smmu->impl = &calxeda_impl;

	return smmu;
}
