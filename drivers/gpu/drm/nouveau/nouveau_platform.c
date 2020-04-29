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
 * The above copyright notice and this permission notice shall be included in
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
#include "nouveau_platform.h"

static int nouveau_platform_probe(struct platform_device *pdev)
{
	const struct nvkm_device_tegra_func *func;
	struct nvkm_device *device = NULL;
	struct drm_device *drm;
	int ret;

	func = of_device_get_match_data(&pdev->dev);

	drm = nouveau_platform_device_create(func, pdev, &device);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	ret = drm_dev_register(drm, 0);
	if (ret < 0) {
		drm_dev_put(drm);
		return ret;
	}

	return 0;
}

static int nouveau_platform_remove(struct platform_device *pdev)
{
	struct drm_device *dev = platform_get_drvdata(pdev);
	nouveau_drm_device_remove(dev);
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

static const struct of_device_id nouveau_platform_match[] = {
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

MODULE_DEVICE_TABLE(of, nouveau_platform_match);
#endif

struct platform_driver nouveau_platform_driver = {
	.driver = {
		.name = "nouveau",
		.of_match_table = of_match_ptr(nouveau_platform_match),
	},
	.probe = nouveau_platform_probe,
	.remove = nouveau_platform_remove,
};

#if IS_ENABLED(CONFIG_ARCH_TEGRA_124_SOC) || IS_ENABLED(CONFIG_ARCH_TEGRA_132_SOC)
MODULE_FIRMWARE("nvidia/gk20a/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gk20a/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gk20a/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gk20a/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gk20a/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gk20a/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gk20a/sw_method_init.bin");
MODULE_FIRMWARE("nvidia/gk20a/sw_nonctx.bin");
#endif
