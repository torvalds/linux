// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/of_device.h>
#include <linux/qcom_scm.h>
#include <linux/ratelimit.h>

#include "arm-smmu.h"
#include "arm-smmu-qcom.h"

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
