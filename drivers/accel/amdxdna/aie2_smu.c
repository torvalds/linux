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

static int aie2_smu_exec(struct amdxdna_dev_hdl *ndev, u32 reg_cmd, u32 reg_arg)
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

	if (resp != SMU_RESULT_OK) {
		XDNA_ERR(ndev->xdna, "smu cmd %d failed, 0x%x", reg_cmd, resp);
		return -EINVAL;
	}

	return 0;
}

static int aie2_smu_set_mpnpu_clock_freq(struct amdxdna_dev_hdl *ndev, u32 freq_mhz)
{
	int ret;

	if (!freq_mhz || freq_mhz > SMU_MPNPUCLK_FREQ_MAX(ndev)) {
		XDNA_ERR(ndev->xdna, "invalid mpnpu clock freq %d", freq_mhz);
		return -EINVAL;
	}

	ndev->mp_npu_clock.freq_mhz = freq_mhz;
	ret = aie2_smu_exec(ndev, AIE2_SMU_SET_MPNPUCLK_FREQ, freq_mhz);
	if (!ret)
		XDNA_INFO_ONCE(ndev->xdna, "set mpnpu_clock = %d mhz", freq_mhz);

	return ret;
}

static int aie2_smu_set_hclock_freq(struct amdxdna_dev_hdl *ndev, u32 freq_mhz)
{
	int ret;

	if (!freq_mhz || freq_mhz > SMU_HCLK_FREQ_MAX(ndev)) {
		XDNA_ERR(ndev->xdna, "invalid hclock freq %d", freq_mhz);
		return -EINVAL;
	}

	ndev->h_clock.freq_mhz = freq_mhz;
	ret = aie2_smu_exec(ndev, AIE2_SMU_SET_HCLK_FREQ, freq_mhz);
	if (!ret)
		XDNA_INFO_ONCE(ndev->xdna, "set npu_hclock = %d mhz", freq_mhz);

	return ret;
}

int aie2_smu_init(struct amdxdna_dev_hdl *ndev)
{
	int ret;

	ret = aie2_smu_exec(ndev, AIE2_SMU_POWER_ON, 0);
	if (ret) {
		XDNA_ERR(ndev->xdna, "Power on failed, ret %d", ret);
		return ret;
	}

	ret = aie2_smu_set_mpnpu_clock_freq(ndev, SMU_MPNPUCLK_FREQ_MAX(ndev));
	if (ret) {
		XDNA_ERR(ndev->xdna, "Set mpnpu clk freq failed, ret %d", ret);
		return ret;
	}
	snprintf(ndev->mp_npu_clock.name, sizeof(ndev->mp_npu_clock.name), "MP-NPU Clock");

	ret = aie2_smu_set_hclock_freq(ndev, SMU_HCLK_FREQ_MAX(ndev));
	if (ret) {
		XDNA_ERR(ndev->xdna, "Set hclk freq failed, ret %d", ret);
		return ret;
	}
	snprintf(ndev->h_clock.name, sizeof(ndev->h_clock.name), "H Clock");

	return 0;
}

void aie2_smu_fini(struct amdxdna_dev_hdl *ndev)
{
	int ret;

	ret = aie2_smu_exec(ndev, AIE2_SMU_POWER_OFF, 0);
	if (ret)
		XDNA_ERR(ndev->xdna, "Power off failed, ret %d", ret);
}
