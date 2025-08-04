// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013-2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Copyright (c) 2014,2017 The Linux Foundation. All rights reserved.
 */

#include "adreno_gpu.h"

static const struct adreno_info a4xx_gpus[] = {
	{
		.chip_ids = ADRENO_CHIP_IDS(0x04000500),
		.family = ADRENO_4XX,
		.revn  = 405,
		.fw = {
			[ADRENO_FW_PM4] = "a420_pm4.fw",
			[ADRENO_FW_PFP] = "a420_pfp.fw",
		},
		.gmem  = SZ_256K,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.init  = a4xx_gpu_init,
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x04020000),
		.family = ADRENO_4XX,
		.revn  = 420,
		.fw = {
			[ADRENO_FW_PM4] = "a420_pm4.fw",
			[ADRENO_FW_PFP] = "a420_pfp.fw",
		},
		.gmem  = (SZ_1M + SZ_512K),
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.init  = a4xx_gpu_init,
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x04030002),
		.family = ADRENO_4XX,
		.revn  = 430,
		.fw = {
			[ADRENO_FW_PM4] = "a420_pm4.fw",
			[ADRENO_FW_PFP] = "a420_pfp.fw",
		},
		.gmem  = (SZ_1M + SZ_512K),
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.init  = a4xx_gpu_init,
	}
};
DECLARE_ADRENO_GPULIST(a4xx);
