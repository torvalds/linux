// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#include "drm/amdxdna_accel.h"
#include <drm/drm_device.h>
#include <drm/drm_managed.h>
#include <drm/drm_print.h>
#include <drm/gpu_scheduler.h>
#include <linux/iopoll.h>

#include "aie.h"

#define SMU_RESULT_OK   1

/* SMU commands */
#define AIE_SMU_POWER_ON                0x3
#define AIE_SMU_POWER_OFF               0x4
#define AIE_SMU_SET_MPNPUCLK_FREQ       0x5
#define AIE_SMU_SET_HCLK_FREQ           0x6
#define AIE_SMU_SET_SOFT_DPMLEVEL       0x7
#define AIE_SMU_SET_HARD_DPMLEVEL       0x8

#define SMU_REG(s, reg) ((s)->smu_regs[reg])

struct smu_device {
	struct drm_device	*ddev;
	struct smu_config	conf;
	void __iomem		*smu_regs[SMU_MAX_REGS];
};

static int aie_smu_exec(struct smu_device *smu, u32 reg_cmd, u32 reg_arg, u32 *out)
{
	u32 resp;
	int ret;

	writel(0, SMU_REG(smu, SMU_RESP_REG));
	writel(reg_arg, SMU_REG(smu, SMU_ARG_REG));
	writel(reg_cmd, SMU_REG(smu, SMU_CMD_REG));

	/* Clear and set SMU_INTR_REG to kick off */
	writel(0, SMU_REG(smu, SMU_INTR_REG));
	writel(1, SMU_REG(smu, SMU_INTR_REG));

	ret = readx_poll_timeout(readl, SMU_REG(smu, SMU_RESP_REG), resp,
				 resp, AIE_INTERVAL, AIE_TIMEOUT);
	if (ret) {
		drm_err(smu->ddev, "smu cmd %d timed out", reg_cmd);
		return ret;
	}

	if (out)
		*out = readl(SMU_REG(smu, SMU_OUT_REG));

	if (resp != SMU_RESULT_OK) {
		drm_err(smu->ddev, "smu cmd %d failed, 0x%x", reg_cmd, resp);
		return -EINVAL;
	}

	return 0;
}

int aie_smu_init(struct smu_device *smu)
{
	int ret;

	/*
	 * Failing to set power off indicates an unrecoverable hardware or
	 * firmware error.
	 */
	ret = aie_smu_exec(smu, AIE_SMU_POWER_OFF, 0, NULL);
	if (ret) {
		drm_err(smu->ddev, "Access power failed, ret %d", ret);
		return ret;
	}

	ret = aie_smu_exec(smu, AIE_SMU_POWER_ON, 0, NULL);
	if (ret) {
		drm_err(smu->ddev, "Power on failed, ret %d", ret);
		return ret;
	}

	return 0;
}

void aie_smu_fini(struct smu_device *smu)
{
	int ret;

	ret = aie_smu_exec(smu, AIE_SMU_POWER_OFF, 0, NULL);
	if (ret)
		drm_err(smu->ddev, "Power off failed, ret %d", ret);
}

int aie_smu_set_clocks(struct smu_device *smu, u32 *npuclk, u32 *hclk)
{
	int ret;

	if (npuclk) {
		ret = aie_smu_exec(smu, AIE_SMU_SET_MPNPUCLK_FREQ, *npuclk, npuclk);
		if (ret) {
			drm_err(smu->ddev, "Set mpnpu clock to %d failed, ret %d", *npuclk, ret);
			return ret;
		}
	}

	if (hclk) {
		ret = aie_smu_exec(smu, AIE_SMU_SET_HCLK_FREQ, *hclk, hclk);
		if (ret) {
			drm_err(smu->ddev, "Set hclock to %d failed, ret %d",
				*hclk, ret);
			return ret;
		}
	}

	return 0;
}

int aie_smu_set_dpm(struct smu_device *smu, u32 dpm_level)
{
	int ret;

	ret = aie_smu_exec(smu, AIE_SMU_SET_HARD_DPMLEVEL, dpm_level, NULL);
	if (ret) {
		drm_err(smu->ddev, "Set hard dpm level %d failed, ret %d",
			dpm_level, ret);
		return ret;
	}

	ret = aie_smu_exec(smu, AIE_SMU_SET_SOFT_DPMLEVEL, dpm_level, NULL);
	if (ret) {
		drm_err(smu->ddev, "Set soft dpm level %d failed, ret %d",
			dpm_level, ret);
		return ret;
	}

	return 0;
}

struct smu_device *aiem_smu_create(struct drm_device *ddev, struct smu_config *conf)
{
	struct smu_device *smu;

	smu = drmm_kzalloc(ddev, sizeof(*smu), GFP_KERNEL);
	if (!smu)
		return NULL;

	smu->ddev = ddev;
	memcpy(smu->smu_regs, conf->smu_regs, sizeof(smu->smu_regs));

	return smu;
}
