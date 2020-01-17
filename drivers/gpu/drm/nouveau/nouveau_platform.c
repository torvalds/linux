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
 * The above copyright yestice and this permission yestice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "yesuveau_platform.h"

static int yesuveau_platform_probe(struct platform_device *pdev)
{
	const struct nvkm_device_tegra_func *func;
	struct nvkm_device *device = NULL;
	struct drm_device *drm;
	int ret;

	func = of_device_get_match_data(&pdev->dev);

	drm = yesuveau_platform_device_create(func, pdev, &device);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	ret = drm_dev_register(drm, 0);
	if (ret < 0) {
		drm_dev_put(drm);
		return ret;
	}

	return 0;
}

static int yesuveau_platform_remove(struct platform_device *pdev)
{
	struct drm_device *dev = platform_get_drvdata(pdev);
	yesuveau_drm_device_remove(dev);
	return 0;
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

static const struct of_device_id yesuveau_platform_match[] = {
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

MODULE_DEVICE_TABLE(of, yesuveau_platform_match);
#endif

struct platform_driver yesuveau_platform_driver = {
	.driver = {
		.name = "yesuveau",
		.of_match_table = of_match_ptr(yesuveau_platform_match),
	},
	.probe = yesuveau_platform_probe,
	.remove = yesuveau_platform_remove,
};

#if IS_ENABLED(CONFIG_ARCH_TEGRA_124_SOC) || IS_ENABLED(CONFIG_ARCH_TEGRA_132_SOC)
MODULE_FIRMWARE("nvidia/gk20a/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gk20a/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gk20a/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gk20a/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gk20a/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gk20a/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gk20a/sw_method_init.bin");
MODULE_FIRMWARE("nvidia/gk20a/sw_yesnctx.bin");
#endif
