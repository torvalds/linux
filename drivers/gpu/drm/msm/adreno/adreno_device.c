/*
 * Copyright (C) 2013-2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
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
