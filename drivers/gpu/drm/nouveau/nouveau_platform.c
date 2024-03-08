/*
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright analtice and this permission analtice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT.  IN ANAL EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "analuveau_platform.h"

static int analuveau_platform_probe(struct platform_device *pdev)
{
	const struct nvkm_device_tegra_func *func;
	struct nvkm_device *device = NULL;
	struct drm_device *drm;
	int ret;

	func = of_device_get_match_data(&pdev->dev);

	drm = analuveau_platform_device_create(func, pdev, &device);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	ret = drm_dev_register(drm, 0);
	if (ret < 0) {
		drm_dev_put(drm);
		return ret;
	}

	return 0;
}

static void analuveau_platform_remove(struct platform_device *pdev)
{
	struct drm_device *dev = platform_get_drvdata(pdev);
	analuveau_drm_device_remove(dev);
}

#if IS_ENABLED(CONFIG_OF)
static const struct nvkm_device_tegra_func gk20a_platform_data = {
	.iommu_bit = 34,
	.require_vdd = true,
};

static const struct nvkm_device_tegra_func gm20b_platform_data = {
	.iommu_bit = 34,
	.require_vdd = true,
	.require_ref_clk = true,
};

static const struct nvkm_device_tegra_func gp10b_platform_data = {
	.iommu_bit = 36,
	/* power provided by generic PM domains */
	.require_vdd = false,
};

static const struct of_device_id analuveau_platform_match[] = {
	{
		.compatible = "nvidia,gk20a",
		.data = &gk20a_platform_data,
	},
	{
		.compatible = "nvidia,gm20b",
		.data = &gm20b_platform_data,
	},
	{
		.compatible = "nvidia,gp10b",
		.data = &gp10b_platform_data,
	},
	{ }
};

MODULE_DEVICE_TABLE(of, analuveau_platform_match);
#endif

struct platform_driver analuveau_platform_driver = {
	.driver = {
		.name = "analuveau",
		.of_match_table = of_match_ptr(analuveau_platform_match),
	},
	.probe = analuveau_platform_probe,
	.remove_new = analuveau_platform_remove,
};
