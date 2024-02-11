// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013-2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Copyright (c) 2014,2017 The Linux Foundation. All rights reserved.
 */

#include "adreno_gpu.h"

bool hang_debug = false;
MODULE_PARM_DESC(hang_debug, "Dump registers when hang is detected (can be slow!)");
module_param_named(hang_debug, hang_debug, bool, 0600);

bool snapshot_debugbus = false;
MODULE_PARM_DESC(snapshot_debugbus, "Include debugbus sections in GPU devcoredump (if not fused off)");
module_param_named(snapshot_debugbus, snapshot_debugbus, bool, 0600);

bool allow_vram_carveout = false;
MODULE_PARM_DESC(allow_vram_carveout, "Allow using VRAM Carveout, in place of IOMMU");
module_param_named(allow_vram_carveout, allow_vram_carveout, bool, 0600);

static const struct adreno_info gpulist[] = {
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
	}, {
		.chip_ids = ADRENO_CHIP_IDS(
			0x03000512,
			0x03000520
		),
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
	}, {
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
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x05000600),
		.family = ADRENO_5XX,
		.revn = 506,
		.fw = {
			[ADRENO_FW_PM4] = "a530_pm4.fw",
			[ADRENO_FW_PFP] = "a530_pfp.fw",
		},
		.gmem = (SZ_128K + SZ_8K),
		/*
		 * Increase inactive period to 250 to avoid bouncing
		 * the GDSC which appears to make it grumpy
		 */
		.inactive_period = 250,
		.quirks = ADRENO_QUIRK_TWO_PASS_USE_WFI |
			  ADRENO_QUIRK_LMLOADKILL_DISABLE,
		.init = a5xx_gpu_init,
		.zapfw = "a506_zap.mdt",
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x05000800),
		.family = ADRENO_5XX,
		.revn = 508,
		.fw = {
			[ADRENO_FW_PM4] = "a530_pm4.fw",
			[ADRENO_FW_PFP] = "a530_pfp.fw",
		},
		.gmem = (SZ_128K + SZ_8K),
		/*
		 * Increase inactive period to 250 to avoid bouncing
		 * the GDSC which appears to make it grumpy
		 */
		.inactive_period = 250,
		.quirks = ADRENO_QUIRK_LMLOADKILL_DISABLE,
		.init = a5xx_gpu_init,
		.zapfw = "a508_zap.mdt",
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x05000900),
		.family = ADRENO_5XX,
		.revn = 509,
		.fw = {
			[ADRENO_FW_PM4] = "a530_pm4.fw",
			[ADRENO_FW_PFP] = "a530_pfp.fw",
		},
		.gmem = (SZ_256K + SZ_16K),
		/*
		 * Increase inactive period to 250 to avoid bouncing
		 * the GDSC which appears to make it grumpy
		 */
		.inactive_period = 250,
		.quirks = ADRENO_QUIRK_LMLOADKILL_DISABLE,
		.init = a5xx_gpu_init,
		/* Adreno 509 uses the same ZAP as 512 */
		.zapfw = "a512_zap.mdt",
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x05010000),
		.family = ADRENO_5XX,
		.revn = 510,
		.fw = {
			[ADRENO_FW_PM4] = "a530_pm4.fw",
			[ADRENO_FW_PFP] = "a530_pfp.fw",
		},
		.gmem = SZ_256K,
		/*
		 * Increase inactive period to 250 to avoid bouncing
		 * the GDSC which appears to make it grumpy
		 */
		.inactive_period = 250,
		.init = a5xx_gpu_init,
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x05010200),
		.family = ADRENO_5XX,
		.revn = 512,
		.fw = {
			[ADRENO_FW_PM4] = "a530_pm4.fw",
			[ADRENO_FW_PFP] = "a530_pfp.fw",
		},
		.gmem = (SZ_256K + SZ_16K),
		/*
		 * Increase inactive period to 250 to avoid bouncing
		 * the GDSC which appears to make it grumpy
		 */
		.inactive_period = 250,
		.quirks = ADRENO_QUIRK_LMLOADKILL_DISABLE,
		.init = a5xx_gpu_init,
		.zapfw = "a512_zap.mdt",
	}, {
		.chip_ids = ADRENO_CHIP_IDS(
			0x05030002,
			0x05030004
		),
		.family = ADRENO_5XX,
		.revn = 530,
		.fw = {
			[ADRENO_FW_PM4] = "a530_pm4.fw",
			[ADRENO_FW_PFP] = "a530_pfp.fw",
			[ADRENO_FW_GPMU] = "a530v3_gpmu.fw2",
		},
		.gmem = SZ_1M,
		/*
		 * Increase inactive period to 250 to avoid bouncing
		 * the GDSC which appears to make it grumpy
		 */
		.inactive_period = 250,
		.quirks = ADRENO_QUIRK_TWO_PASS_USE_WFI |
			ADRENO_QUIRK_FAULT_DETECT_MASK,
		.init = a5xx_gpu_init,
		.zapfw = "a530_zap.mdt",
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x05040001),
		.family = ADRENO_5XX,
		.revn = 540,
		.fw = {
			[ADRENO_FW_PM4] = "a530_pm4.fw",
			[ADRENO_FW_PFP] = "a530_pfp.fw",
			[ADRENO_FW_GPMU] = "a540_gpmu.fw2",
		},
		.gmem = SZ_1M,
		/*
		 * Increase inactive period to 250 to avoid bouncing
		 * the GDSC which appears to make it grumpy
		 */
		.inactive_period = 250,
		.quirks = ADRENO_QUIRK_LMLOADKILL_DISABLE,
		.init = a5xx_gpu_init,
		.zapfw = "a540_zap.mdt",
	}, {
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
		.hwcg = a660_hwcg,
		.address_space_size = SZ_16G,
		.speedbins = ADRENO_SPEEDBINS(
			{ 0,   0 },
			{ 117, 0 },
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
	},
};

MODULE_FIRMWARE("qcom/a300_pm4.fw");
MODULE_FIRMWARE("qcom/a300_pfp.fw");
MODULE_FIRMWARE("qcom/a330_pm4.fw");
MODULE_FIRMWARE("qcom/a330_pfp.fw");
MODULE_FIRMWARE("qcom/a420_pm4.fw");
MODULE_FIRMWARE("qcom/a420_pfp.fw");
MODULE_FIRMWARE("qcom/a530_pm4.fw");
MODULE_FIRMWARE("qcom/a530_pfp.fw");
MODULE_FIRMWARE("qcom/a530v3_gpmu.fw2");
MODULE_FIRMWARE("qcom/a530_zap.mdt");
MODULE_FIRMWARE("qcom/a530_zap.b00");
MODULE_FIRMWARE("qcom/a530_zap.b01");
MODULE_FIRMWARE("qcom/a530_zap.b02");
MODULE_FIRMWARE("qcom/a540_gpmu.fw2");
MODULE_FIRMWARE("qcom/a619_gmu.bin");
MODULE_FIRMWARE("qcom/a630_sqe.fw");
MODULE_FIRMWARE("qcom/a630_gmu.bin");
MODULE_FIRMWARE("qcom/a630_zap.mbn");
MODULE_FIRMWARE("qcom/a640_gmu.bin");
MODULE_FIRMWARE("qcom/a650_gmu.bin");
MODULE_FIRMWARE("qcom/a650_sqe.fw");
MODULE_FIRMWARE("qcom/a660_gmu.bin");
MODULE_FIRMWARE("qcom/a660_sqe.fw");
MODULE_FIRMWARE("qcom/leia_pfp_470.fw");
MODULE_FIRMWARE("qcom/leia_pm4_470.fw");
MODULE_FIRMWARE("qcom/yamato_pfp.fw");
MODULE_FIRMWARE("qcom/yamato_pm4.fw");

static const struct adreno_info *adreno_info(uint32_t chip_id)
{
	/* identify gpu: */
	for (int i = 0; i < ARRAY_SIZE(gpulist); i++) {
		const struct adreno_info *info = &gpulist[i];
		if (info->machine && !of_machine_is_compatible(info->machine))
			continue;
		for (int j = 0; info->chip_ids[j]; j++)
			if (info->chip_ids[j] == chip_id)
				return info;
	}

	return NULL;
}

struct msm_gpu *adreno_load_gpu(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct platform_device *pdev = priv->gpu_pdev;
	struct msm_gpu *gpu = NULL;
	struct adreno_gpu *adreno_gpu;
	int ret;

	if (pdev)
		gpu = dev_to_gpu(&pdev->dev);

	if (!gpu) {
		dev_err_once(dev->dev, "no GPU device was found\n");
		return NULL;
	}

	adreno_gpu = to_adreno_gpu(gpu);

	/*
	 * The number one reason for HW init to fail is if the firmware isn't
	 * loaded yet. Try that first and don't bother continuing on
	 * otherwise
	 */

	ret = adreno_load_fw(adreno_gpu);
	if (ret)
		return NULL;

	if (gpu->funcs->ucode_load) {
		ret = gpu->funcs->ucode_load(gpu);
		if (ret)
			return NULL;
	}

	/*
	 * Now that we have firmware loaded, and are ready to begin
	 * booting the gpu, go ahead and enable runpm:
	 */
	pm_runtime_enable(&pdev->dev);

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(&pdev->dev);
		DRM_DEV_ERROR(dev->dev, "Couldn't power up the GPU: %d\n", ret);
		goto err_disable_rpm;
	}

	mutex_lock(&gpu->lock);
	ret = msm_gpu_hw_init(gpu);
	mutex_unlock(&gpu->lock);
	if (ret) {
		DRM_DEV_ERROR(dev->dev, "gpu hw init failed: %d\n", ret);
		goto err_put_rpm;
	}

	pm_runtime_put_autosuspend(&pdev->dev);

#ifdef CONFIG_DEBUG_FS
	if (gpu->funcs->debugfs_init) {
		gpu->funcs->debugfs_init(gpu, dev->primary);
		gpu->funcs->debugfs_init(gpu, dev->render);
	}
#endif

	return gpu;

err_put_rpm:
	pm_runtime_put_sync_suspend(&pdev->dev);
err_disable_rpm:
	pm_runtime_disable(&pdev->dev);

	return NULL;
}

static int find_chipid(struct device *dev, uint32_t *chipid)
{
	struct device_node *node = dev->of_node;
	const char *compat;
	int ret;

	/* first search the compat strings for qcom,adreno-XYZ.W: */
	ret = of_property_read_string_index(node, "compatible", 0, &compat);
	if (ret == 0) {
		unsigned int r, patch;

		if (sscanf(compat, "qcom,adreno-%u.%u", &r, &patch) == 2 ||
		    sscanf(compat, "amd,imageon-%u.%u", &r, &patch) == 2) {
			uint32_t core, major, minor;

			core = r / 100;
			r %= 100;
			major = r / 10;
			r %= 10;
			minor = r;

			*chipid = (core << 24) |
				(major << 16) |
				(minor << 8) |
				patch;

			return 0;
		}

		if (sscanf(compat, "qcom,adreno-%08x", chipid) == 1)
			return 0;
	}

	/* and if that fails, fall back to legacy "qcom,chipid" property: */
	ret = of_property_read_u32(node, "qcom,chipid", chipid);
	if (ret) {
		DRM_DEV_ERROR(dev, "could not parse qcom,chipid: %d\n", ret);
		return ret;
	}

	dev_warn(dev, "Using legacy qcom,chipid binding!\n");

	return 0;
}

static int adreno_bind(struct device *dev, struct device *master, void *data)
{
	static struct adreno_platform_config config = {};
	const struct adreno_info *info;
	struct msm_drm_private *priv = dev_get_drvdata(master);
	struct drm_device *drm = priv->dev;
	struct msm_gpu *gpu;
	int ret;

	ret = find_chipid(dev, &config.chip_id);
	if (ret)
		return ret;

	dev->platform_data = &config;
	priv->gpu_pdev = to_platform_device(dev);

	info = adreno_info(config.chip_id);
	if (!info) {
		dev_warn(drm->dev, "Unknown GPU revision: %"ADRENO_CHIPID_FMT"\n",
			ADRENO_CHIPID_ARGS(config.chip_id));
		return -ENXIO;
	}

	config.info = info;

	DBG("Found GPU: %"ADRENO_CHIPID_FMT, ADRENO_CHIPID_ARGS(config.chip_id));

	priv->is_a2xx = info->family < ADRENO_3XX;
	priv->has_cached_coherent =
		!!(info->quirks & ADRENO_QUIRK_HAS_CACHED_COHERENT);

	gpu = info->init(drm);
	if (IS_ERR(gpu)) {
		dev_warn(drm->dev, "failed to load adreno gpu\n");
		return PTR_ERR(gpu);
	}

	ret = dev_pm_opp_of_find_icc_paths(dev, NULL);
	if (ret)
		return ret;

	return 0;
}

static int adreno_system_suspend(struct device *dev);
static void adreno_unbind(struct device *dev, struct device *master,
		void *data)
{
	struct msm_drm_private *priv = dev_get_drvdata(master);
	struct msm_gpu *gpu = dev_to_gpu(dev);

	if (pm_runtime_enabled(dev))
		WARN_ON_ONCE(adreno_system_suspend(dev));
	gpu->funcs->destroy(gpu);

	priv->gpu_pdev = NULL;
}

static const struct component_ops a3xx_ops = {
	.bind   = adreno_bind,
	.unbind = adreno_unbind,
};

static void adreno_device_register_headless(void)
{
	/* on imx5, we don't have a top-level mdp/dpu node
	 * this creates a dummy node for the driver for that case
	 */
	struct platform_device_info dummy_info = {
		.parent = NULL,
		.name = "msm",
		.id = -1,
		.res = NULL,
		.num_res = 0,
		.data = NULL,
		.size_data = 0,
		.dma_mask = ~0,
	};
	platform_device_register_full(&dummy_info);
}

static int adreno_probe(struct platform_device *pdev)
{

	int ret;

	ret = component_add(&pdev->dev, &a3xx_ops);
	if (ret)
		return ret;

	if (of_device_is_compatible(pdev->dev.of_node, "amd,imageon"))
		adreno_device_register_headless();

	return 0;
}

static int adreno_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &a3xx_ops);
	return 0;
}

static void adreno_shutdown(struct platform_device *pdev)
{
	WARN_ON_ONCE(adreno_system_suspend(&pdev->dev));
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "qcom,adreno" },
	{ .compatible = "qcom,adreno-3xx" },
	/* for compatibility with imx5 gpu: */
	{ .compatible = "amd,imageon" },
	/* for backwards compat w/ downstream kgsl DT files: */
	{ .compatible = "qcom,kgsl-3d0" },
	{}
};

static int adreno_runtime_resume(struct device *dev)
{
	struct msm_gpu *gpu = dev_to_gpu(dev);

	return gpu->funcs->pm_resume(gpu);
}

static int adreno_runtime_suspend(struct device *dev)
{
	struct msm_gpu *gpu = dev_to_gpu(dev);

	/*
	 * We should be holding a runpm ref, which will prevent
	 * runtime suspend.  In the system suspend path, we've
	 * already waited for active jobs to complete.
	 */
	WARN_ON_ONCE(gpu->active_submits);

	return gpu->funcs->pm_suspend(gpu);
}

static void suspend_scheduler(struct msm_gpu *gpu)
{
	int i;

	/*
	 * Shut down the scheduler before we force suspend, so that
	 * suspend isn't racing with scheduler kthread feeding us
	 * more work.
	 *
	 * Note, we just want to park the thread, and let any jobs
	 * that are already on the hw queue complete normally, as
	 * opposed to the drm_sched_stop() path used for handling
	 * faulting/timed-out jobs.  We can't really cancel any jobs
	 * already on the hw queue without racing with the GPU.
	 */
	for (i = 0; i < gpu->nr_rings; i++) {
		struct drm_gpu_scheduler *sched = &gpu->rb[i]->sched;
		kthread_park(sched->thread);
	}
}

static void resume_scheduler(struct msm_gpu *gpu)
{
	int i;

	for (i = 0; i < gpu->nr_rings; i++) {
		struct drm_gpu_scheduler *sched = &gpu->rb[i]->sched;
		kthread_unpark(sched->thread);
	}
}

static int adreno_system_suspend(struct device *dev)
{
	struct msm_gpu *gpu = dev_to_gpu(dev);
	int remaining, ret;

	if (!gpu)
		return 0;

	suspend_scheduler(gpu);

	remaining = wait_event_timeout(gpu->retire_event,
				       gpu->active_submits == 0,
				       msecs_to_jiffies(1000));
	if (remaining == 0) {
		dev_err(dev, "Timeout waiting for GPU to suspend\n");
		ret = -EBUSY;
		goto out;
	}

	ret = pm_runtime_force_suspend(dev);
out:
	if (ret)
		resume_scheduler(gpu);

	return ret;
}

static int adreno_system_resume(struct device *dev)
{
	struct msm_gpu *gpu = dev_to_gpu(dev);

	if (!gpu)
		return 0;

	resume_scheduler(gpu);
	return pm_runtime_force_resume(dev);
}

static const struct dev_pm_ops adreno_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(adreno_system_suspend, adreno_system_resume)
	RUNTIME_PM_OPS(adreno_runtime_suspend, adreno_runtime_resume, NULL)
};

static struct platform_driver adreno_driver = {
	.probe = adreno_probe,
	.remove = adreno_remove,
	.shutdown = adreno_shutdown,
	.driver = {
		.name = "adreno",
		.of_match_table = dt_match,
		.pm = &adreno_pm_ops,
	},
};

void __init adreno_register(void)
{
	platform_driver_register(&adreno_driver);
}

void __exit adreno_unregister(void)
{
	platform_driver_unregister(&adreno_driver);
}
