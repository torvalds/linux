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

int enable_preemption = -1;
MODULE_PARM_DESC(enable_preemption, "Enable preemption (A7xx only) (1=on , 0=disable, -1=auto (default))");
module_param(enable_preemption, int, 0600);

extern const struct adreno_gpulist a2xx_gpulist;
extern const struct adreno_gpulist a3xx_gpulist;
extern const struct adreno_gpulist a4xx_gpulist;
extern const struct adreno_gpulist a5xx_gpulist;
extern const struct adreno_gpulist a6xx_gpulist;
extern const struct adreno_gpulist a7xx_gpulist;

static const struct adreno_gpulist *gpulists[] = {
	&a2xx_gpulist,
	&a3xx_gpulist,
	&a4xx_gpulist,
	&a5xx_gpulist,
	&a6xx_gpulist,
	&a7xx_gpulist,
};

static const struct adreno_info *adreno_info(uint32_t chip_id)
{
	/* identify gpu: */
	for (int i = 0; i < ARRAY_SIZE(gpulists); i++) {
		for (int j = 0; j < gpulists[i]->gpus_count; j++) {
			const struct adreno_info *info = &gpulists[i]->gpus[j];

			if (info->machine && !of_machine_is_compatible(info->machine))
				continue;

			for (int k = 0; info->chip_ids[k]; k++)
				if (info->chip_ids[k] == chip_id)
					return info;
		}
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

static void adreno_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &a3xx_ops);
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

		drm_sched_wqueue_stop(sched);
	}
}

static void resume_scheduler(struct msm_gpu *gpu)
{
	int i;

	for (i = 0; i < gpu->nr_rings; i++) {
		struct drm_gpu_scheduler *sched = &gpu->rb[i]->sched;

		drm_sched_wqueue_start(sched);
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
