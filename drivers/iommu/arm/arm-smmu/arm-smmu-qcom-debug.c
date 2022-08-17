// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/of_device.h>
#include <linux/qcom_scm.h>
#include <linux/ratelimit.h>

#include "arm-smmu.h"
#include "arm-smmu-qcom.h"

enum qcom_smmu_impl_reg_offset {
	QCOM_SMMU_TBU_PWR_STATUS,
	QCOM_SMMU_STATS_SYNC_INV_TBU_ACK,
	QCOM_SMMU_MMU2QSS_AND_SAFE_WAIT_CNTR,
};

struct qcom_smmu_config {
	const u32 *reg_offset;
};

void qcom_smmu_tlb_sync_debug(struct arm_smmu_device *smmu)
{
	int ret;
	u32 tbu_pwr_status, sync_inv_ack, sync_inv_progress;
	struct qcom_smmu *qsmmu = container_of(smmu, struct qcom_smmu, smmu);
	const struct qcom_smmu_config *cfg;
	static DEFINE_RATELIMIT_STATE(rs, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);

	if (__ratelimit(&rs)) {
		dev_err(smmu->dev, "TLB sync timed out -- SMMU may be deadlocked\n");

		cfg = qsmmu->cfg;
		if (!cfg)
			return;

		ret = qcom_scm_io_readl(smmu->ioaddr + cfg->reg_offset[QCOM_SMMU_TBU_PWR_STATUS],
					&tbu_pwr_status);
		if (ret)
			dev_err(smmu->dev,
				"Failed to read TBU power status: %d\n", ret);

		ret = qcom_scm_io_readl(smmu->ioaddr + cfg->reg_offset[QCOM_SMMU_STATS_SYNC_INV_TBU_ACK],
					&sync_inv_ack);
		if (ret)
			dev_err(smmu->dev,
				"Failed to read TBU sync/inv ack status: %d\n", ret);

		ret = qcom_scm_io_readl(smmu->ioaddr + cfg->reg_offset[QCOM_SMMU_MMU2QSS_AND_SAFE_WAIT_CNTR],
					&sync_inv_progress);
		if (ret)
			dev_err(smmu->dev,
				"Failed to read TCU syn/inv progress: %d\n", ret);

		dev_err(smmu->dev,
			"TBU: power_status %#x sync_inv_ack %#x sync_inv_progress %#x\n",
			tbu_pwr_status, sync_inv_ack, sync_inv_progress);
	}
}

/* Implementation Defined Register Space 0 register offsets */
static const u32 qcom_smmu_impl0_reg_offset[] = {
	[QCOM_SMMU_TBU_PWR_STATUS]		= 0x2204,
	[QCOM_SMMU_STATS_SYNC_INV_TBU_ACK]	= 0x25dc,
	[QCOM_SMMU_MMU2QSS_AND_SAFE_WAIT_CNTR]	= 0x2670,
};

static const struct qcom_smmu_config qcm2290_smmu_cfg = {
	.reg_offset = qcom_smmu_impl0_reg_offset,
};

static const struct qcom_smmu_config sc7180_smmu_cfg = {
	.reg_offset = qcom_smmu_impl0_reg_offset,
};

static const struct qcom_smmu_config sc7280_smmu_cfg = {
	.reg_offset = qcom_smmu_impl0_reg_offset,
};

static const struct qcom_smmu_config sc8180x_smmu_cfg = {
	.reg_offset = qcom_smmu_impl0_reg_offset,
};

static const struct qcom_smmu_config sc8280xp_smmu_cfg = {
	.reg_offset = qcom_smmu_impl0_reg_offset,
};

static const struct qcom_smmu_config sm6125_smmu_cfg = {
	.reg_offset = qcom_smmu_impl0_reg_offset,
};

static const struct qcom_smmu_config sm6350_smmu_cfg = {
	.reg_offset = qcom_smmu_impl0_reg_offset,
};

static const struct qcom_smmu_config sm8150_smmu_cfg = {
	.reg_offset = qcom_smmu_impl0_reg_offset,
};

static const struct qcom_smmu_config sm8250_smmu_cfg = {
	.reg_offset = qcom_smmu_impl0_reg_offset,
};

static const struct qcom_smmu_config sm8350_smmu_cfg = {
	.reg_offset = qcom_smmu_impl0_reg_offset,
};

static const struct qcom_smmu_config sm8450_smmu_cfg = {
	.reg_offset = qcom_smmu_impl0_reg_offset,
};

static const struct of_device_id __maybe_unused qcom_smmu_impl_debug_match[] = {
	{ .compatible = "qcom,msm8998-smmu-v2" },
	{ .compatible = "qcom,qcm2290-smmu-500", .data = &qcm2290_smmu_cfg },
	{ .compatible = "qcom,sc7180-smmu-500", .data = &sc7180_smmu_cfg },
	{ .compatible = "qcom,sc7280-smmu-500", .data = &sc7280_smmu_cfg},
	{ .compatible = "qcom,sc8180x-smmu-500", .data = &sc8180x_smmu_cfg },
	{ .compatible = "qcom,sc8280xp-smmu-500", .data = &sc8280xp_smmu_cfg },
	{ .compatible = "qcom,sdm630-smmu-v2" },
	{ .compatible = "qcom,sdm845-smmu-500" },
	{ .compatible = "qcom,sm6125-smmu-500", .data = &sm6125_smmu_cfg},
	{ .compatible = "qcom,sm6350-smmu-500", .data = &sm6350_smmu_cfg},
	{ .compatible = "qcom,sm8150-smmu-500", .data = &sm8150_smmu_cfg },
	{ .compatible = "qcom,sm8250-smmu-500", .data = &sm8250_smmu_cfg },
	{ .compatible = "qcom,sm8350-smmu-500", .data = &sm8350_smmu_cfg },
	{ .compatible = "qcom,sm8450-smmu-500", .data = &sm8450_smmu_cfg },
	{ }
};

const void *qcom_smmu_impl_data(struct arm_smmu_device *smmu)
{
	const struct of_device_id *match;
	const struct device_node *np = smmu->dev->of_node;

	match = of_match_node(qcom_smmu_impl_debug_match, np);
	if (!match)
		return NULL;

	return match->data;
}
