// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 */

#include <linux/bitfield.h>
#include <linux/of_device.h>
#include <linux/qcom_scm.h>

#include "arm-smmu.h"

struct qcom_smmu {
	struct arm_smmu_device smmu;
};

static int qcom_sdm845_smmu500_cfg_probe(struct arm_smmu_device *smmu)
{
	u32 s2cr;
	u32 smr;
	int i;

	for (i = 0; i < smmu->num_mapping_groups; i++) {
		smr = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_SMR(i));
		s2cr = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_S2CR(i));

		smmu->smrs[i].mask = FIELD_GET(ARM_SMMU_SMR_MASK, smr);
		smmu->smrs[i].id = FIELD_GET(ARM_SMMU_SMR_ID, smr);
		if (smmu->features & ARM_SMMU_FEAT_EXIDS)
			smmu->smrs[i].valid = FIELD_GET(
						ARM_SMMU_S2CR_EXIDVALID,
						s2cr);
		else
			smmu->smrs[i].valid = FIELD_GET(
						ARM_SMMU_SMR_VALID,
						smr);

		smmu->s2crs[i].group = NULL;
		smmu->s2crs[i].count = 0;
		smmu->s2crs[i].type = FIELD_GET(ARM_SMMU_S2CR_TYPE, s2cr);
		smmu->s2crs[i].privcfg = FIELD_GET(ARM_SMMU_S2CR_PRIVCFG, s2cr);
		smmu->s2crs[i].cbndx = FIELD_GET(ARM_SMMU_S2CR_CBNDX, s2cr);

		if (!smmu->smrs[i].valid)
			continue;

		smmu->s2crs[i].pinned = true;
		bitmap_set(smmu->context_map, smmu->s2crs[i].cbndx, 1);
	}

	return 0;
}

static const struct of_device_id qcom_smmu_client_of_match[] __maybe_unused = {
	{ .compatible = "qcom,adreno" },
	{ .compatible = "qcom,mdp4" },
	{ .compatible = "qcom,mdss" },
	{ .compatible = "qcom,sc7180-mdss" },
	{ .compatible = "qcom,sc7180-mss-pil" },
	{ .compatible = "qcom,sdm845-mdss" },
	{ .compatible = "qcom,sdm845-mss-pil" },
	{ }
};

static int qcom_smmu_def_domain_type(struct device *dev)
{
	const struct of_device_id *match =
		of_match_device(qcom_smmu_client_of_match, dev);

	return match ? IOMMU_DOMAIN_IDENTITY : 0;
}

static int qcom_sdm845_smmu500_reset(struct arm_smmu_device *smmu)
{
	int ret;

	/*
	 * To address performance degradation in non-real time clients,
	 * such as USB and UFS, turn off wait-for-safe on sdm845 based boards,
	 * such as MTP and db845, whose firmwares implement secure monitor
	 * call handlers to turn on/off the wait-for-safe logic.
	 */
	ret = qcom_scm_qsmmu500_wait_safe_toggle(0);
	if (ret)
		dev_warn(smmu->dev, "Failed to turn off SAFE logic\n");

	return ret;
}

static int qcom_smmu500_reset(struct arm_smmu_device *smmu)
{
	const struct device_node *np = smmu->dev->of_node;

	arm_mmu500_reset(smmu);

	if (of_device_is_compatible(np, "qcom,sdm845-smmu-500"))
		return qcom_sdm845_smmu500_reset(smmu);

	return 0;
}

static const struct arm_smmu_impl qcom_smmu_impl = {
	.def_domain_type = qcom_smmu_def_domain_type,
	.cfg_probe = qcom_sdm845_smmu500_cfg_probe,
	.reset = qcom_smmu500_reset,
};

struct arm_smmu_device *qcom_smmu_impl_init(struct arm_smmu_device *smmu)
{
	struct qcom_smmu *qsmmu;

	qsmmu = devm_kzalloc(smmu->dev, sizeof(*qsmmu), GFP_KERNEL);
	if (!qsmmu)
		return ERR_PTR(-ENOMEM);

	qsmmu->smmu = *smmu;

	qsmmu->smmu.impl = &qcom_smmu_impl;
	devm_kfree(smmu->dev, smmu);

	return &qsmmu->smmu;
}
