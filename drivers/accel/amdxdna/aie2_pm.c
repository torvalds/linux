// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024, Advanced Micro Devices, Inc.
 */

#include <drm/amdxdna_accel.h>
#include <drm/drm_device.h>
#include <drm/drm_print.h>
#include <drm/gpu_scheduler.h>

#include "aie2_pci.h"
#include "amdxdna_pci_drv.h"

#define AIE2_CLK_GATING_ENABLE	1
#define AIE2_CLK_GATING_DISABLE	0

static int aie2_pm_set_clk_gating(struct amdxdna_dev_hdl *ndev, u32 val)
{
	int ret;

	ret = aie2_runtime_cfg(ndev, AIE2_RT_CFG_CLK_GATING, &val);
	if (ret)
		return ret;

	ndev->clk_gating = val;
	return 0;
}

int aie2_pm_init(struct amdxdna_dev_hdl *ndev)
{
	int ret;

	if (ndev->dev_status != AIE2_DEV_UNINIT) {
		/* Resume device */
		ret = ndev->priv->hw_ops.set_dpm(ndev, ndev->dpm_level);
		if (ret)
			return ret;

		ret = aie2_pm_set_clk_gating(ndev, ndev->clk_gating);
		if (ret)
			return ret;

		return 0;
	}

	while (ndev->priv->dpm_clk_tbl[ndev->max_dpm_level].hclk)
		ndev->max_dpm_level++;
	ndev->max_dpm_level--;

	ret = ndev->priv->hw_ops.set_dpm(ndev, ndev->max_dpm_level);
	if (ret)
		return ret;

	ret = aie2_pm_set_clk_gating(ndev, AIE2_CLK_GATING_ENABLE);
	if (ret)
		return ret;

	ndev->pw_mode = POWER_MODE_DEFAULT;
	ndev->dft_dpm_level = ndev->max_dpm_level;

	return 0;
}

int aie2_pm_set_mode(struct amdxdna_dev_hdl *ndev, enum amdxdna_power_mode_type target)
{
	struct amdxdna_dev *xdna = ndev->xdna;
	u32 clk_gating, dpm_level;
	int ret;

	drm_WARN_ON(&xdna->ddev, !mutex_is_locked(&xdna->dev_lock));

	if (ndev->pw_mode == target)
		return 0;

	switch (target) {
	case POWER_MODE_TURBO:
		if (ndev->hwctx_num) {
			XDNA_ERR(xdna, "Can not set turbo when there is active hwctx");
			return -EINVAL;
		}

		clk_gating = AIE2_CLK_GATING_DISABLE;
		dpm_level = ndev->max_dpm_level;
		break;
	case POWER_MODE_HIGH:
		clk_gating = AIE2_CLK_GATING_ENABLE;
		dpm_level = ndev->max_dpm_level;
		break;
	case POWER_MODE_DEFAULT:
		clk_gating = AIE2_CLK_GATING_ENABLE;
		dpm_level = ndev->dft_dpm_level;
		break;
	default:
		return -EOPNOTSUPP;
	}

	ret = ndev->priv->hw_ops.set_dpm(ndev, dpm_level);
	if (ret)
		return ret;

	ret = aie2_pm_set_clk_gating(ndev, clk_gating);
	if (ret)
		return ret;

	ndev->pw_mode = target;

	return 0;
}
