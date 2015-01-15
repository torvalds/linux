/*
 * Copyright (C) 2013-2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Copyright (c) 2014 The Linux Foundation. All rights reserved.
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

#include "adreno_gpu.h"

#if defined(CONFIG_MSM_BUS_SCALING) && !defined(CONFIG_OF)
#  include <mach/kgsl.h>
#endif

#define ANY_ID 0xff

bool hang_debug = false;
MODULE_PARM_DESC(hang_debug, "Dump registers when hang is detected (can be slow!)");
module_param_named(hang_debug, hang_debug, bool, 0600);

struct msm_gpu *a3xx_gpu_init(struct drm_device *dev);
struct msm_gpu *a4xx_gpu_init(struct drm_device *dev);

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
	},
};

MODULE_FIRMWARE("a300_pm4.fw");
MODULE_FIRMWARE("a300_pfp.fw");
MODULE_FIRMWARE("a330_pm4.fw");
MODULE_FIRMWARE("a330_pfp.fw");
MODULE_FIRMWARE("a420_pm4.fw");
MODULE_FIRMWARE("a420_pfp.fw");

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
		mutex_lock(&dev->struct_mutex);
		gpu->funcs->pm_resume(gpu);
		mutex_unlock(&dev->struct_mutex);
		ret = gpu->funcs->hw_init(gpu);
		if (ret) {
			dev_err(dev->dev, "gpu hw init failed: %d\n", ret);
			gpu->funcs->destroy(gpu);
			gpu = NULL;
		} else {
			/* give inactive pm a chance to kick in: */
			msm_gpu_retire(gpu);
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

static int adreno_bind(struct device *dev, struct device *master, void *data)
{
	static struct adreno_platform_config config = {};
#ifdef CONFIG_OF
	struct device_node *child, *node = dev->of_node;
	u32 val;
	int ret;

	ret = of_property_read_u32(node, "qcom,chipid", &val);
	if (ret) {
		dev_err(dev, "could not find chipid: %d\n", ret);
		return ret;
	}

	config.rev = ADRENO_REV((val >> 24) & 0xff,
			(val >> 16) & 0xff, (val >> 8) & 0xff, val & 0xff);

	/* find clock rates: */
	config.fast_rate = 0;
	config.slow_rate = ~0;
	for_each_child_of_node(node, child) {
		if (of_device_is_compatible(child, "qcom,gpu-pwrlevels")) {
			struct device_node *pwrlvl;
			for_each_child_of_node(child, pwrlvl) {
				ret = of_property_read_u32(pwrlvl, "qcom,gpu-freq", &val);
				if (ret) {
					dev_err(dev, "could not find gpu-freq: %d\n", ret);
					return ret;
				}
				config.fast_rate = max(config.fast_rate, val);
				config.slow_rate = min(config.slow_rate, val);
			}
		}
	}

	if (!config.fast_rate) {
		dev_err(dev, "could not find clk rates\n");
		return -ENXIO;
	}

#else
	struct kgsl_device_platform_data *pdata = dev->platform_data;
	uint32_t version = socinfo_get_version();
	if (cpu_is_apq8064ab()) {
		config.fast_rate = 450000000;
		config.slow_rate = 27000000;
		config.bus_freq  = 4;
		config.rev = ADRENO_REV(3, 2, 1, 0);
	} else if (cpu_is_apq8064()) {
		config.fast_rate = 400000000;
		config.slow_rate = 27000000;
		config.bus_freq  = 4;

		if (SOCINFO_VERSION_MAJOR(version) == 2)
			config.rev = ADRENO_REV(3, 2, 0, 2);
		else if ((SOCINFO_VERSION_MAJOR(version) == 1) &&
				(SOCINFO_VERSION_MINOR(version) == 1))
			config.rev = ADRENO_REV(3, 2, 0, 1);
		else
			config.rev = ADRENO_REV(3, 2, 0, 0);

	} else if (cpu_is_msm8960ab()) {
		config.fast_rate = 400000000;
		config.slow_rate = 320000000;
		config.bus_freq  = 4;

		if (SOCINFO_VERSION_MINOR(version) == 0)
			config.rev = ADRENO_REV(3, 2, 1, 0);
		else
			config.rev = ADRENO_REV(3, 2, 1, 1);

	} else if (cpu_is_msm8930()) {
		config.fast_rate = 400000000;
		config.slow_rate = 27000000;
		config.bus_freq  = 3;

		if ((SOCINFO_VERSION_MAJOR(version) == 1) &&
			(SOCINFO_VERSION_MINOR(version) == 2))
			config.rev = ADRENO_REV(3, 0, 5, 2);
		else
			config.rev = ADRENO_REV(3, 0, 5, 0);

	}
#  ifdef CONFIG_MSM_BUS_SCALING
	config.bus_scale_table = pdata->bus_scale_table;
#  endif
#endif
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
	{ .compatible = "qcom,adreno-3xx" },
	/* for backwards compat w/ downstream kgsl DT files: */
	{ .compatible = "qcom,kgsl-3d0" },
	{}
};

static struct platform_driver adreno_driver = {
	.probe = adreno_probe,
	.remove = adreno_remove,
	.driver = {
		.name = "adreno",
		.of_match_table = dt_match,
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
