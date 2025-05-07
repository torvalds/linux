// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013-2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Copyright (c) 2014,2017 The Linux Foundation. All rights reserved.
 */

#include "adreno_gpu.h"

static const struct adreno_info a2xx_gpus[] = {
	{
		.chip_ids = ADRENO_CHIP_IDS(0x02000000),
		.family = ADRENO_2XX_GEN1,
		.revn  = 200,
		.fw = {
			[ADRENO_FW_PM4] = "yamato_pm4.fw",
			[ADRENO_FW_PFP] = "yamato_pfp.fw",
		},
		.gmem  = SZ_256K,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.init  = a2xx_gpu_init,
	}, { /* a200 on i.mx51 has only 128kib gmem */
		.chip_ids = ADRENO_CHIP_IDS(0x02000001),
		.family = ADRENO_2XX_GEN1,
		.revn  = 201,
		.fw = {
			[ADRENO_FW_PM4] = "yamato_pm4.fw",
			[ADRENO_FW_PFP] = "yamato_pfp.fw",
		},
		.gmem  = SZ_128K,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.init  = a2xx_gpu_init,
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x02020000),
		.family = ADRENO_2XX_GEN2,
		.revn  = 220,
		.fw = {
			[ADRENO_FW_PM4] = "leia_pm4_470.fw",
			[ADRENO_FW_PFP] = "leia_pfp_470.fw",
		},
		.gmem  = SZ_512K,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.init  = a2xx_gpu_init,
	}
};
DECLARE_ADRENO_GPULIST(a2xx);
