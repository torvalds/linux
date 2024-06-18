// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013-2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Copyright (c) 2014,2017 The Linux Foundation. All rights reserved.
 */

#include "adreno_gpu.h"

static const struct adreno_info a6xx_gpus[] = {
	{
		.chip_ids = ADRENO_CHIP_IDS(0x06010000),
		.family = ADRENO_6XX_GEN1,
		.revn = 610,
		.fw = {
			[ADRENO_FW_SQE] = "a630_sqe.fw",
		},
		.gmem = (SZ_128K + SZ_4K),
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.init = a6xx_gpu_init,
		.zapfw = "a610_zap.mdt",
		.hwcg = a612_hwcg,
		/*
		 * There are (at least) three SoCs implementing A610: SM6125
		 * (trinket), SM6115 (bengal) and SM6225 (khaje). Trinket does
		 * not have speedbinning, as only a single SKU exists and we
		 * don't support khaje upstream yet.  Hence, this matching
		 * table is only valid for bengal.
		 */
		.speedbins = ADRENO_SPEEDBINS(
			{ 0,   0 },
			{ 206, 1 },
			{ 200, 2 },
			{ 157, 3 },
			{ 127, 4 },
		),
	}, {
		.machine = "qcom,sm7150",
		.chip_ids = ADRENO_CHIP_IDS(0x06010800),
		.family = ADRENO_6XX_GEN1,
		.fw = {
			[ADRENO_FW_SQE] = "a630_sqe.fw",
			[ADRENO_FW_GMU] = "a630_gmu.bin",
		},
		.gmem = SZ_512K,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.quirks = ADRENO_QUIRK_HAS_CACHED_COHERENT,
		.init = a6xx_gpu_init,
		.zapfw = "a615_zap.mbn",
		.hwcg = a615_hwcg,
		.speedbins = ADRENO_SPEEDBINS(
			{ 0,   0 },
			{ 128, 1 },
			{ 146, 2 },
			{ 167, 3 },
			{ 172, 4 },
		),
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x06010800),
		.family = ADRENO_6XX_GEN1,
		.revn = 618,
		.fw = {
			[ADRENO_FW_SQE] = "a630_sqe.fw",
			[ADRENO_FW_GMU] = "a630_gmu.bin",
		},
		.gmem = SZ_512K,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.quirks = ADRENO_QUIRK_HAS_CACHED_COHERENT,
		.init = a6xx_gpu_init,
		.speedbins = ADRENO_SPEEDBINS(
			{ 0,   0 },
			{ 169, 1 },
			{ 174, 2 },
		),
	}, {
		.machine = "qcom,sm4350",
		.chip_ids = ADRENO_CHIP_IDS(0x06010900),
		.family = ADRENO_6XX_GEN1,
		.revn = 619,
		.fw = {
			[ADRENO_FW_SQE] = "a630_sqe.fw",
			[ADRENO_FW_GMU] = "a619_gmu.bin",
		},
		.gmem = SZ_512K,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.init = a6xx_gpu_init,
		.zapfw = "a615_zap.mdt",
		.hwcg = a615_hwcg,
		.speedbins = ADRENO_SPEEDBINS(
			{ 0,   0 },
			{ 138, 1 },
			{ 92,  2 },
		),
	}, {
		.machine = "qcom,sm6375",
		.chip_ids = ADRENO_CHIP_IDS(0x06010901),
		.family = ADRENO_6XX_GEN1,
		.revn = 619,
		.fw = {
			[ADRENO_FW_SQE] = "a630_sqe.fw",
			[ADRENO_FW_GMU] = "a619_gmu.bin",
		},
		.gmem = SZ_512K,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.init = a6xx_gpu_init,
		.zapfw = "a615_zap.mdt",
		.hwcg = a615_hwcg,
		.speedbins = ADRENO_SPEEDBINS(
			{ 0,   0 },
			{ 190, 1 },
			{ 177, 2 },
		),
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x06010900),
		.family = ADRENO_6XX_GEN1,
		.revn = 619,
		.fw = {
			[ADRENO_FW_SQE] = "a630_sqe.fw",
			[ADRENO_FW_GMU] = "a619_gmu.bin",
		},
		.gmem = SZ_512K,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.quirks = ADRENO_QUIRK_HAS_CACHED_COHERENT,
		.init = a6xx_gpu_init,
		.zapfw = "a615_zap.mdt",
		.hwcg = a615_hwcg,
		.speedbins = ADRENO_SPEEDBINS(
			{ 0,   0 },
			{ 120, 4 },
			{ 138, 3 },
			{ 169, 2 },
			{ 180, 1 },
		),
	}, {
		.chip_ids = ADRENO_CHIP_IDS(
			0x06030001,
			0x06030002
		),
		.family = ADRENO_6XX_GEN1,
		.revn = 630,
		.fw = {
			[ADRENO_FW_SQE] = "a630_sqe.fw",
			[ADRENO_FW_GMU] = "a630_gmu.bin",
		},
		.gmem = SZ_1M,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.quirks = ADRENO_QUIRK_HAS_CACHED_COHERENT,
		.init = a6xx_gpu_init,
		.zapfw = "a630_zap.mdt",
		.hwcg = a630_hwcg,
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x06040001),
		.family = ADRENO_6XX_GEN2,
		.revn = 640,
		.fw = {
			[ADRENO_FW_SQE] = "a630_sqe.fw",
			[ADRENO_FW_GMU] = "a640_gmu.bin",
		},
		.gmem = SZ_1M,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.quirks = ADRENO_QUIRK_HAS_CACHED_COHERENT,
		.init = a6xx_gpu_init,
		.zapfw = "a640_zap.mdt",
		.hwcg = a640_hwcg,
		.speedbins = ADRENO_SPEEDBINS(
			{ 0, 0 },
			{ 1, 1 },
		),
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x06050002),
		.family = ADRENO_6XX_GEN3,
		.revn = 650,
		.fw = {
			[ADRENO_FW_SQE] = "a650_sqe.fw",
			[ADRENO_FW_GMU] = "a650_gmu.bin",
		},
		.gmem = SZ_1M + SZ_128K,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.quirks = ADRENO_QUIRK_HAS_CACHED_COHERENT |
			ADRENO_QUIRK_HAS_HW_APRIV,
		.init = a6xx_gpu_init,
		.zapfw = "a650_zap.mdt",
		.hwcg = a650_hwcg,
		.address_space_size = SZ_16G,
		.speedbins = ADRENO_SPEEDBINS(
			{ 0, 0 },
			{ 1, 1 },
			{ 2, 3 }, /* Yep, 2 and 3 are swapped! :/ */
			{ 3, 2 },
		),
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x06060001),
		.family = ADRENO_6XX_GEN4,
		.revn = 660,
		.fw = {
			[ADRENO_FW_SQE] = "a660_sqe.fw",
			[ADRENO_FW_GMU] = "a660_gmu.bin",
		},
		.gmem = SZ_1M + SZ_512K,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.quirks = ADRENO_QUIRK_HAS_CACHED_COHERENT |
			ADRENO_QUIRK_HAS_HW_APRIV,
		.init = a6xx_gpu_init,
		.zapfw = "a660_zap.mdt",
		.hwcg = a660_hwcg,
		.address_space_size = SZ_16G,
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x06030500),
		.family = ADRENO_6XX_GEN4,
		.fw = {
			[ADRENO_FW_SQE] = "a660_sqe.fw",
			[ADRENO_FW_GMU] = "a660_gmu.bin",
		},
		.gmem = SZ_512K,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.quirks = ADRENO_QUIRK_HAS_CACHED_COHERENT |
			ADRENO_QUIRK_HAS_HW_APRIV,
		.init = a6xx_gpu_init,
		.zapfw = "a660_zap.mbn",
		.hwcg = a660_hwcg,
		.address_space_size = SZ_16G,
		.speedbins = ADRENO_SPEEDBINS(
			{ 0,   0 },
			{ 117, 0 },
			{ 172, 2 }, /* Called speedbin 1 downstream, but let's not break things! */
			{ 190, 1 },
		),
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x06080001),
		.family = ADRENO_6XX_GEN2,
		.revn = 680,
		.fw = {
			[ADRENO_FW_SQE] = "a630_sqe.fw",
			[ADRENO_FW_GMU] = "a640_gmu.bin",
		},
		.gmem = SZ_2M,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.quirks = ADRENO_QUIRK_HAS_CACHED_COHERENT,
		.init = a6xx_gpu_init,
		.zapfw = "a640_zap.mdt",
		.hwcg = a640_hwcg,
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x06090000),
		.family = ADRENO_6XX_GEN4,
		.fw = {
			[ADRENO_FW_SQE] = "a660_sqe.fw",
			[ADRENO_FW_GMU] = "a660_gmu.bin",
		},
		.gmem = SZ_4M,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.quirks = ADRENO_QUIRK_HAS_CACHED_COHERENT |
			ADRENO_QUIRK_HAS_HW_APRIV,
		.init = a6xx_gpu_init,
		.zapfw = "a690_zap.mdt",
		.hwcg = a690_hwcg,
		.address_space_size = SZ_16G,
	}
};
DECLARE_ADRENO_GPULIST(a6xx);

MODULE_FIRMWARE("qcom/a615_zap.mbn");
MODULE_FIRMWARE("qcom/a619_gmu.bin");
MODULE_FIRMWARE("qcom/a630_sqe.fw");
MODULE_FIRMWARE("qcom/a630_gmu.bin");
MODULE_FIRMWARE("qcom/a630_zap.mbn");
MODULE_FIRMWARE("qcom/a640_gmu.bin");
MODULE_FIRMWARE("qcom/a650_gmu.bin");
MODULE_FIRMWARE("qcom/a650_sqe.fw");
MODULE_FIRMWARE("qcom/a660_gmu.bin");
MODULE_FIRMWARE("qcom/a660_sqe.fw");

static const struct adreno_info a7xx_gpus[] = {
	{
		.chip_ids = ADRENO_CHIP_IDS(0x07000200),
		.family = ADRENO_6XX_GEN1, /* NOT a mistake! */
		.fw = {
			[ADRENO_FW_SQE] = "a702_sqe.fw",
		},
		.gmem = SZ_128K,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.quirks = ADRENO_QUIRK_HAS_HW_APRIV,
		.init = a6xx_gpu_init,
		.zapfw = "a702_zap.mbn",
		.hwcg = a702_hwcg,
		.speedbins = ADRENO_SPEEDBINS(
			{ 0,   0 },
			{ 236, 1 },
			{ 178, 2 },
			{ 142, 3 },
		),
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x07030001),
		.family = ADRENO_7XX_GEN1,
		.fw = {
			[ADRENO_FW_SQE] = "a730_sqe.fw",
			[ADRENO_FW_GMU] = "gmu_gen70000.bin",
		},
		.gmem = SZ_2M,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.quirks = ADRENO_QUIRK_HAS_CACHED_COHERENT |
			  ADRENO_QUIRK_HAS_HW_APRIV,
		.init = a6xx_gpu_init,
		.zapfw = "a730_zap.mdt",
		.hwcg = a730_hwcg,
		.address_space_size = SZ_16G,
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x43050a01), /* "C510v2" */
		.family = ADRENO_7XX_GEN2,
		.fw = {
			[ADRENO_FW_SQE] = "a740_sqe.fw",
			[ADRENO_FW_GMU] = "gmu_gen70200.bin",
		},
		.gmem = 3 * SZ_1M,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.quirks = ADRENO_QUIRK_HAS_CACHED_COHERENT |
			  ADRENO_QUIRK_HAS_HW_APRIV,
		.init = a6xx_gpu_init,
		.zapfw = "a740_zap.mdt",
		.hwcg = a740_hwcg,
		.address_space_size = SZ_16G,
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x43051401), /* "C520v2" */
		.family = ADRENO_7XX_GEN3,
		.fw = {
			[ADRENO_FW_SQE] = "gen70900_sqe.fw",
			[ADRENO_FW_GMU] = "gmu_gen70900.bin",
		},
		.gmem = 3 * SZ_1M,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.quirks = ADRENO_QUIRK_HAS_CACHED_COHERENT |
			  ADRENO_QUIRK_HAS_HW_APRIV,
		.init = a6xx_gpu_init,
		.zapfw = "gen70900_zap.mbn",
		.address_space_size = SZ_16G,
	}
};
DECLARE_ADRENO_GPULIST(a7xx);
