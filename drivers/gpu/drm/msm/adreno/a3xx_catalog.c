// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013-2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Copyright (c) 2014,2017 The Linux Foundation. All rights reserved.
 */

#include "adreno_gpu.h"

static const struct adreno_info a3xx_gpus[] = {
	{
		.chip_ids = ADRENO_CHIP_IDS(0x03000512),
		.family = ADRENO_3XX,
		.fw = {
			[ADRENO_FW_PM4] = "a330_pm4.fw",
			[ADRENO_FW_PFP] = "a330_pfp.fw",
		},
		.gmem  = SZ_128K,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.init  = a3xx_gpu_init,
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x03000520),
		.family = ADRENO_3XX,
		.revn  = 305,
		.fw = {
			[ADRENO_FW_PM4] = "a300_pm4.fw",
			[ADRENO_FW_PFP] = "a300_pfp.fw",
		},
		.gmem  = SZ_256K,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.init  = a3xx_gpu_init,
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x03000600),
		.family = ADRENO_3XX,
		.revn  = 307,        /* because a305c is revn==306 */
		.fw = {
			[ADRENO_FW_PM4] = "a300_pm4.fw",
			[ADRENO_FW_PFP] = "a300_pfp.fw",
		},
		.gmem  = SZ_128K,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.init  = a3xx_gpu_init,
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x03000620),
		.family = ADRENO_3XX,
		.revn = 308,
		.fw = {
			[ADRENO_FW_PM4] = "a300_pm4.fw",
			[ADRENO_FW_PFP] = "a300_pfp.fw",
		},
		.gmem = SZ_128K,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.init = a3xx_gpu_init,
	}, {
		.chip_ids = ADRENO_CHIP_IDS(
			0x03020000,
			0x03020001,
			0x03020002
		),
		.family = ADRENO_3XX,
		.revn  = 320,
		.fw = {
			[ADRENO_FW_PM4] = "a300_pm4.fw",
			[ADRENO_FW_PFP] = "a300_pfp.fw",
		},
		.gmem  = SZ_512K,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.init  = a3xx_gpu_init,
	}, {
		.chip_ids = ADRENO_CHIP_IDS(
			0x03030000,
			0x03030001,
			0x03030002
		),
		.family = ADRENO_3XX,
		.revn  = 330,
		.fw = {
			[ADRENO_FW_PM4] = "a330_pm4.fw",
			[ADRENO_FW_PFP] = "a330_pfp.fw",
		},
		.gmem  = SZ_1M,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.init  = a3xx_gpu_init,
	}
};
DECLARE_ADRENO_GPULIST(a3xx);

MODULE_FIRMWARE("qcom/a300_pm4.fw");
MODULE_FIRMWARE("qcom/a300_pfp.fw");
MODULE_FIRMWARE("qcom/a330_pm4.fw");
MODULE_FIRMWARE("qcom/a330_pfp.fw");
