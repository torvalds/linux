/*
 * Copyright (C) 2013-2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Copyright (c) 2014,2017 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/pm_opp.h>
#include "adreno_gpu.h"

#define ANY_ID 0xff

bool hang_debug = false;
MODULE_PARM_DESC(hang_debug, "Dump registers when hang is detected (can be slow!)");
module_param_named(hang_debug, hang_debug, bool, 0600);

static const struct adreno_info gpulist[] = {
	{
		.rev   = ADRENO_REV(3, 0, 5, ANY_ID),
		.revn  = 305,
		.name  = "A305",
		.pm4fw = "a300_pm4.fw",
		.pfpfw = "a300_pfp.fw",
		.gmem  = SZ_256K,
		.init  = a3xx_gpu_init,
	}, {
		.rev   = ADRENO_REV(3, 0, 6, 0),
		.revn  = 307,        /* because a305c is revn==306 */
		.name  = "A306",
		.pm4fw = "a300_pm4.fw",
		.pfpfw = "a300_pfp.fw",
		.gmem  = SZ_128K,
		.init  = a3xx_gpu_init,
	}, {
		.rev   = ADRENO_REV(3, 2, ANY_ID, ANY_ID),
		.revn  = 320,
		.name  = "A320",
		.pm4fw = "a300_pm4.fw",
		.pfpfw = "a300_pfp.fw",
		.gmem  = SZ_512K,
		.init  = a3xx_gpu_init,
	}, {
		.rev   = ADRENO_REV(3, 3, 0, ANY_ID),
		.revn  = 330,
		.name  = "A330",
		.pm4fw = "a330_pm4.fw",
		.pfpfw = "a330_pfp.fw",
		.gmem  = SZ_1M,
		.init  = a3xx_gpu_init,
	}, {
		.rev   = ADRENO_REV(4, 2, 0, ANY_ID),
		.revn  = 420,
		.name  = "A420",
		.pm4fw = "a420_pm4.fw",
		.pfpfw = "a420_pfp.fw",
		.gmem  = (SZ_1M + SZ_512K),
		.init  = a4xx_gpu_init,
	}, {
		.rev   = ADRENO_REV(4, 3, 0, ANY_ID),
		.revn  = 430,
		.name  = "A430",
		.pm4fw = "a420_pm4.fw",
		.pfpfw = "a420_pfp.fw",
		.gmem  = (SZ_1M + SZ_512K),
		.init  = a4xx_gpu_init,
	}, {
		.rev = ADRENO_REV(5, 3, 0, 2),
		.revn = 530,
		.name = "A530",
		.pm4fw = "a530_pm4.fw",
		.pfpfw = "a530_pfp.fw",
		.gmem = SZ_1M,
		.quirks = ADRENO_QUIRK_TWO_PASS_USE_WFI |
			ADRENO_QUIRK_FAULT_DETECT_MASK,
		.init = a5xx_gpu_init,
		.gpmufw = "a530v3_gpmu.fw2",
		.zapfw = "a530_zap.mdt",
	},
};

MODULE_FIRMWARE("a300_pm4.fw");
MODULE_FIRMWARE("a300_pfp.fw");
MODULE_FIRMWARE("a330_pm4.fw");
MODULE_FIRMWARE("a330_pfp.fw");
MODULE_FIRMWARE("a420_pm4.fw");
MODULE_FIRMWARE("a420_pfp.fw");
MODULE_FIRMWARE("a530_fm4.fw");
MODULE_FIRMWARE("a530_pfp.fw");

static inline bool _rev_match(uint8_t entry, uint8_t id)
{
	return (entry == ANY_ID) || (entry == id);
}

const struct adreno_info *adreno_info(struct adreno_rev rev)
{
	int i;

	/* identify gpu: */
	for (i = 0; i < ARRAY_SIZE(gpulist); i++) {
		const struct adreno_info *info = &gpulist[i];
		if (_rev_match(info->rev.core, rev.core) &&
				_rev_match(info->rev.major, rev.major) &&
				_rev_match(info->rev.minor, rev.minor) &&
				_rev_match(info->rev.patchid, rev.patchid))
			return info;
	}

	return NULL;
}

struct msm_gpu *adreno_load_gpu(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct platform_device *pdev = priv->gpu_pdev;
	struct adreno_platform_config *config;
	struct adreno_rev rev;
	const struct adreno_info *info;
	struct msm_gpu *gpu = NULL;

	if (!pdev) {
		dev_err(dev->dev, "no adreno device\n");
		return NULL;
	}

	config = pdev->dev.platform_data;
	rev = config->rev;
	info = adreno_info(config->rev);

	if (!info) {
		dev_warn(dev->dev, "Unknown GPU revision: %u.%u.%u.%u\n",
				rev.core, rev.major, rev.minor, rev.patchid);
		return NULL;
	}

	DBG("Found GPU: %u.%u.%u.%u",  rev.core, rev.major,
			rev.minor, rev.patchid);

	gpu = info->init(dev);
	if (IS_ERR(gpu)) {
		dev_warn(dev->dev, "failed to load adreno gpu\n");
		gpu = NULL;
		/* not fatal */
	}

	if (gpu) {
		int ret;

		pm_runtime_get_sync(&pdev->dev);
		mutex_lock(&dev->struct_mutex);
		ret = msm_gpu_hw_init(gpu);
		mutex_unlock(&dev->struct_mutex);
		pm_runtime_put_sync(&pdev->dev);
		if (ret) {
			dev_err(dev->dev, "gpu hw init failed: %d\n", ret);
			gpu->funcs->destroy(gpu);
			gpu = NULL;
		}
	}

	return gpu;
}

static void set_gpu_pdev(struct drm_device *dev,
		struct platform_device *pdev)
{
	struct msm_drm_private *priv = dev->dev_private;
	priv->gpu_pdev = pdev;
}

static int find_chipid(struct device *dev, u32 *chipid)
{
	struct device_node *node = dev->of_node;
	const char *compat;
	int ret;

	/* first search the compat strings for qcom,adreno-XYZ.W: */
	ret = of_property_read_string_index(node, "compatible", 0, &compat);
	if (ret == 0) {
		unsigned rev, patch;

		if (sscanf(compat, "qcom,adreno-%u.%u", &rev, &patch) == 2) {
			*chipid = 0;
			*chipid |= (rev / 100) << 24;  /* core */
			rev %= 100;
			*chipid |= (rev / 10) << 16;   /* major */
			rev %= 10;
			*chipid |= rev << 8;           /* minor */
			*chipid |= patch;

			return 0;
		}
	}

	/* and if that fails, fall back to legacy "qcom,chipid" property: */
	ret = of_property_read_u32(node, "qcom,chipid", chipid);
	if (ret)
		return ret;

	dev_warn(dev, "Using legacy qcom,chipid binding!\n");
	dev_warn(dev, "Use compatible qcom,adreno-%u%u%u.%u instead.\n",
			(*chipid >> 24) & 0xff, (*chipid >> 16) & 0xff,
			(*chipid >> 8) & 0xff, *chipid & 0xff);

	return 0;
}

/* Get legacy powerlevels from qcom,gpu-pwrlevels and populate the opp table */
static int adreno_get_legacy_pwrlevels(struct device *dev)
{
	struct device_node *child, *node;
	int ret;

	node = of_find_compatible_node(dev->of_node, NULL,
		"qcom,gpu-pwrlevels");
	if (!node) {
		dev_err(dev, "Could not find the GPU powerlevels\n");
		return -ENXIO;
	}

	for_each_child_of_node(node, child) {
		unsigned int val;

		ret = of_property_read_u32(child, "qcom,gpu-freq", &val);
		if (ret)
			continue;

		/*
		 * Skip the intentionally bogus clock value found at the bottom
		 * of most legacy frequency tables
		 */
		if (val != 27000000)
			dev_pm_opp_add(dev, val, 0);
	}

	return 0;
}

static int adreno_get_pwrlevels(struct device *dev,
		struct adreno_platform_config *config)
{
	unsigned long freq = ULONG_MAX;
	struct dev_pm_opp *opp;
	int ret;

	/* You down with OPP? */
	if (!of_find_property(dev->of_node, "operating-points-v2", NULL))
		ret = adreno_get_legacy_pwrlevels(dev);
	else
		ret = dev_pm_opp_of_add_table(dev);

	if (ret)
		return ret;

	/* Find the fastest defined rate */
	opp = dev_pm_opp_find_freq_floor(dev, &freq);
	if (!IS_ERR(opp))
		config->fast_rate = dev_pm_opp_get_freq(opp);

	if (!config->fast_rate) {
		DRM_DEV_INFO(dev,
			"Could not find clock rate. Using default\n");
		/* Pick a suitably safe clock speed for any target */
		config->fast_rate = 200000000;
	}

	return 0;
}

static int adreno_bind(struct device *dev, struct device *master, void *data)
{
	static struct adreno_platform_config config = {};
	u32 val;
	int ret;

	ret = find_chipid(dev, &val);
	if (ret) {
		dev_err(dev, "could not find chipid: %d\n", ret);
		return ret;
	}

	config.rev = ADRENO_REV((val >> 24) & 0xff,
			(val >> 16) & 0xff, (val >> 8) & 0xff, val & 0xff);

	/* find clock rates: */
	config.fast_rate = 0;

	ret = adreno_get_pwrlevels(dev, &config);
	if (ret)
		return ret;

	dev->platform_data = &config;
	set_gpu_pdev(dev_get_drvdata(master), to_platform_device(dev));
	return 0;
}

static void adreno_unbind(struct device *dev, struct device *master,
		void *data)
{
	set_gpu_pdev(dev_get_drvdata(master), NULL);
}

static const struct component_ops a3xx_ops = {
		.bind   = adreno_bind,
		.unbind = adreno_unbind,
};

static int adreno_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &a3xx_ops);
}

static int adreno_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &a3xx_ops);
	return 0;
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "qcom,adreno" },
	{ .compatible = "qcom,adreno-3xx" },
	/* for backwards compat w/ downstream kgsl DT files: */
	{ .compatible = "qcom,kgsl-3d0" },
	{}
};

#ifdef CONFIG_PM
static int adreno_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_gpu *gpu = platform_get_drvdata(pdev);

	return gpu->funcs->pm_resume(gpu);
}

static int adreno_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_gpu *gpu = platform_get_drvdata(pdev);

	return gpu->funcs->pm_suspend(gpu);
}
#endif

static const struct dev_pm_ops adreno_pm_ops = {
	SET_RUNTIME_PM_OPS(adreno_suspend, adreno_resume, NULL)
};

static struct platform_driver adreno_driver = {
	.probe = adreno_probe,
	.remove = adreno_remove,
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
