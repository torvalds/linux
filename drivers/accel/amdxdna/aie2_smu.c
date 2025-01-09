// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022-2024, Advanced Micro Devices, Inc.
 */

#include <drm/drm_device.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_print.h>
#include <drm/gpu_scheduler.h>
#include <linux/iopoll.h>

#include "aie2_pci.h"
#include "amdxdna_pci_drv.h"

#define SMU_RESULT_OK		1

/* SMU commands */
#define AIE2_SMU_POWER_ON		0x3
#define AIE2_SMU_POWER_OFF		0x4
#define AIE2_SMU_SET_MPNPUCLK_FREQ	0x5
#define AIE2_SMU_SET_HCLK_FREQ		0x6
#define AIE2_SMU_SET_SOFT_DPMLEVEL	0x7
#define AIE2_SMU_SET_HARD_DPMLEVEL	0x8

static int aie2_smu_exec(struct amdxdna_dev_hdl *ndev, u32 reg_cmd,
			 u32 reg_arg, u32 *out)
{
	u32 resp;
	int ret;

	writel(0, SMU_REG(ndev, SMU_RESP_REG));
	writel(reg_arg, SMU_REG(ndev, SMU_ARG_REG));
	writel(reg_cmd, SMU_REG(ndev, SMU_CMD_REG));

	/* Clear and set SMU_INTR_REG to kick off */
	writel(0, SMU_REG(ndev, SMU_INTR_REG));
	writel(1, SMU_REG(ndev, SMU_INTR_REG));

	ret = readx_poll_timeout(readl, SMU_REG(ndev, SMU_RESP_REG), resp,
				 resp, AIE2_INTERVAL, AIE2_TIMEOUT);
	if (ret) {
		XDNA_ERR(ndev->xdna, "smu cmd %d timed out", reg_cmd);
		return ret;
	}

	if (out)
		*out = readl(SMU_REG(ndev, SMU_OUT_REG));

	if (resp != SMU_RESULT_OK) {
		XDNA_ERR(ndev->xdna, "smu cmd %d failed, 0x%x", reg_cmd, resp);
		return -EINVAL;
	}

	return 0;
}

int npu1_set_dpm(struct amdxdna_dev_hdl *ndev, u32 dpm_level)
{
	u32 freq;
	int ret;

	ret = aie2_smu_exec(ndev, AIE2_SMU_SET_MPNPUCLK_FREQ,
			    ndev->priv->dpm_clk_tbl[dpm_level].npuclk, &freq);
	if (ret) {
		XDNA_ERR(ndev->xdna, "Set npu clock to %d failed, ret %d\n",
			 ndev->priv->dpm_clk_tbl[dpm_level].npuclk, ret);
		return ret;
	}
	ndev->npuclk_freq = freq;

	ret = aie2_smu_exec(ndev, AIE2_SMU_SET_HCLK_FREQ,
			    ndev->priv->dpm_clk_tbl[dpm_level].hclk, &freq);
	if (ret) {
		XDNA_ERR(ndev->xdna, "Set h clock to %d failed, ret %d\n",
			 ndev->priv->dpm_clk_tbl[dpm_level].hclk, ret);
		return ret;
	}
	ndev->hclk_freq = freq;
	ndev->dpm_level = dpm_level;

	XDNA_DBG(ndev->xdna, "MP-NPU clock %d, H clock %d\n",
		 ndev->npuclk_freq, ndev->hclk_freq);

	return 0;
}

int npu4_set_dpm(struct amdxdna_dev_hdl *ndev, u32 dpm_level)
{
	int ret;

	ret = aie2_smu_exec(ndev, AIE2_SMU_SET_HARD_DPMLEVEL, dpm_level, NULL);
	if (ret) {
		XDNA_ERR(ndev->xdna, "Set hard dpm level %d failed, ret %d ",
			 dpm_level, ret);
		return ret;
	}

	ret = aie2_smu_exec(ndev, AIE2_SMU_SET_SOFT_DPMLEVEL, dpm_level, NULL);
	if (ret) {
		XDNA_ERR(ndev->xdna, "Set soft dpm level %d failed, ret %d",
			 dpm_level, ret);
		return ret;
	}

	ndev->npuclk_freq = ndev->priv->dpm_clk_tbl[dpm_level].npuclk;
	ndev->hclk_freq = ndev->priv->dpm_clk_tbl[dpm_level].hclk;
	ndev->dpm_level = dpm_level;

	XDNA_DBG(ndev->xdna, "MP-NPU clock %d, H clock %d\n",
		 ndev->npuclk_freq, ndev->hclk_freq);

	return 0;
}

int aie2_smu_init(struct amdxdna_dev_hdl *ndev)
{
	int ret;

	ret = aie2_smu_exec(ndev, AIE2_SMU_POWER_ON, 0, NULL);
	if (ret) {
		XDNA_ERR(ndev->xdna, "Power on failed, ret %d", ret);
		return ret;
	}

	return 0;
}

void aie2_smu_fini(struct amdxdna_dev_hdl *ndev)
{
	int ret;

	ndev->priv->hw_ops.set_dpm(ndev, 0);
	ret = aie2_smu_exec(ndev, AIE2_SMU_POWER_OFF, 0, NULL);
	if (ret)
		XDNA_ERR(ndev->xdna, "Power off failed, ret %d", ret);
}
