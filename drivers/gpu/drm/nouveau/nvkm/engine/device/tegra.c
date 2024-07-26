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
#include <core/tegra.h>
#ifdef CONFIG_NOUVEAU_PLATFORM_DRIVER
#include "priv.h"

#if IS_ENABLED(CONFIG_ARM_DMA_USE_IOMMU)
#include <asm/dma-iommu.h>
#endif

static int
nvkm_device_tegra_power_up(struct nvkm_device_tegra *tdev)
{
	int ret;

	if (tdev->vdd) {
		ret = regulator_enable(tdev->vdd);
		if (ret)
			goto err_power;
	}

	ret = clk_prepare_enable(tdev->clk);
	if (ret)
		goto err_clk;
	ret = clk_prepare_enable(tdev->clk_ref);
	if (ret)
		goto err_clk_ref;
	ret = clk_prepare_enable(tdev->clk_pwr);
	if (ret)
		goto err_clk_pwr;
	clk_set_rate(tdev->clk_pwr, 204000000);
	udelay(10);

	if (!tdev->pdev->dev.pm_domain) {
		reset_control_assert(tdev->rst);
		udelay(10);

		ret = tegra_powergate_remove_clamping(TEGRA_POWERGATE_3D);
		if (ret)
			goto err_clamp;
		udelay(10);

		reset_control_deassert(tdev->rst);
		udelay(10);
	}

	return 0;

err_clamp:
	clk_disable_unprepare(tdev->clk_pwr);
err_clk_pwr:
	clk_disable_unprepare(tdev->clk_ref);
err_clk_ref:
	clk_disable_unprepare(tdev->clk);
err_clk:
	if (tdev->vdd)
		regulator_disable(tdev->vdd);
err_power:
	return ret;
}

static int
nvkm_device_tegra_power_down(struct nvkm_device_tegra *tdev)
{
	int ret;

	clk_disable_unprepare(tdev->clk_pwr);
	clk_disable_unprepare(tdev->clk_ref);
	clk_disable_unprepare(tdev->clk);
	udelay(10);

	if (tdev->vdd) {
		ret = regulator_disable(tdev->vdd);
		if (ret)
			return ret;
	}

	return 0;
}

static void
nvkm_device_tegra_probe_iommu(struct nvkm_device_tegra *tdev)
{
#if IS_ENABLED(CONFIG_IOMMU_API)
	struct device *dev = &tdev->pdev->dev;
	unsigned long pgsize_bitmap;
	int ret;

#if IS_ENABLED(CONFIG_ARM_DMA_USE_IOMMU)
	if (dev->archdata.mapping) {
		struct dma_iommu_mapping *mapping = to_dma_iommu_mapping(dev);

		arm_iommu_detach_device(dev);
		arm_iommu_release_mapping(mapping);
	}
#endif

	if (!tdev->func->iommu_bit)
		return;

	mutex_init(&tdev->iommu.mutex);

	if (device_iommu_mapped(dev)) {
		tdev->iommu.domain = iommu_domain_alloc(&platform_bus_type);
		if (!tdev->iommu.domain)
			goto error;

		/*
		 * A IOMMU is only usable if it supports page sizes smaller
		 * or equal to the system's PAGE_SIZE, with a preference if
		 * both are equal.
		 */
		pgsize_bitmap = tdev->iommu.domain->pgsize_bitmap;
		if (pgsize_bitmap & PAGE_SIZE) {
			tdev->iommu.pgshift = PAGE_SHIFT;
		} else {
			tdev->iommu.pgshift = fls(pgsize_bitmap & ~PAGE_MASK);
			if (tdev->iommu.pgshift == 0) {
				dev_warn(dev, "unsupported IOMMU page size\n");
				goto free_domain;
			}
			tdev->iommu.pgshift -= 1;
		}

		ret = iommu_attach_device(tdev->iommu.domain, dev);
		if (ret)
			goto free_domain;

		ret = nvkm_mm_init(&tdev->iommu.mm, 0, 0,
				   (1ULL << tdev->func->iommu_bit) >>
				   tdev->iommu.pgshift, 1);
		if (ret)
			goto detach_device;
	}

	return;

detach_device:
	iommu_detach_device(tdev->iommu.domain, dev);

free_domain:
	iommu_domain_free(tdev->iommu.domain);

error:
	tdev->iommu.domain = NULL;
	tdev->iommu.pgshift = 0;
	dev_err(dev, "cannot initialize IOMMU MM\n");
#endif
}

static void
nvkm_device_tegra_remove_iommu(struct nvkm_device_tegra *tdev)
{
#if IS_ENABLED(CONFIG_IOMMU_API)
	if (tdev->iommu.domain) {
		nvkm_mm_fini(&tdev->iommu.mm);
		iommu_detach_device(tdev->iommu.domain, tdev->device.dev);
		iommu_domain_free(tdev->iommu.domain);
	}
#endif
}

static struct nvkm_device_tegra *
nvkm_device_tegra(struct nvkm_device *device)
{
	return container_of(device, struct nvkm_device_tegra, device);
}

static struct resource *
nvkm_device_tegra_resource(struct nvkm_device *device, unsigned bar)
{
	struct nvkm_device_tegra *tdev = nvkm_device_tegra(device);
	return platform_get_resource(tdev->pdev, IORESOURCE_MEM, bar);
}

static resource_size_t
nvkm_device_tegra_resource_addr(struct nvkm_device *device, unsigned bar)
{
	struct resource *res = nvkm_device_tegra_resource(device, bar);
	return res ? res->start : 0;
}

static resource_size_t
nvkm_device_tegra_resource_size(struct nvkm_device *device, unsigned bar)
{
	struct resource *res = nvkm_device_tegra_resource(device, bar);
	return res ? resource_size(res) : 0;
}

static int
nvkm_device_tegra_irq(struct nvkm_device *device)
{
	struct nvkm_device_tegra *tdev = nvkm_device_tegra(device);

	return platform_get_irq_byname(tdev->pdev, "stall");
}

static void *
nvkm_device_tegra_dtor(struct nvkm_device *device)
{
	struct nvkm_device_tegra *tdev = nvkm_device_tegra(device);
	nvkm_device_tegra_power_down(tdev);
	nvkm_device_tegra_remove_iommu(tdev);
	return tdev;
}

static const struct nvkm_device_func
nvkm_device_tegra_func = {
	.tegra = nvkm_device_tegra,
	.dtor = nvkm_device_tegra_dtor,
	.irq = nvkm_device_tegra_irq,
	.resource_addr = nvkm_device_tegra_resource_addr,
	.resource_size = nvkm_device_tegra_resource_size,
	.cpu_coherent = false,
};

int
nvkm_device_tegra_new(const struct nvkm_device_tegra_func *func,
		      struct platform_device *pdev,
		      const char *cfg, const char *dbg,
		      struct nvkm_device **pdevice)
{
	struct nvkm_device_tegra *tdev;
	unsigned long rate;
	int ret;

	if (!(tdev = kzalloc(sizeof(*tdev), GFP_KERNEL)))
		return -ENOMEM;

	tdev->func = func;
	tdev->pdev = pdev;

	if (func->require_vdd) {
		tdev->vdd = devm_regulator_get(&pdev->dev, "vdd");
		if (IS_ERR(tdev->vdd)) {
			ret = PTR_ERR(tdev->vdd);
			goto free;
		}
	}

	tdev->rst = devm_reset_control_get(&pdev->dev, "gpu");
	if (IS_ERR(tdev->rst)) {
		ret = PTR_ERR(tdev->rst);
		goto free;
	}

	tdev->clk = devm_clk_get(&pdev->dev, "gpu");
	if (IS_ERR(tdev->clk)) {
		ret = PTR_ERR(tdev->clk);
		goto free;
	}

	rate = clk_get_rate(tdev->clk);
	if (rate == 0) {
		ret = clk_set_rate(tdev->clk, ULONG_MAX);
		if (ret < 0)
			goto free;

		rate = clk_get_rate(tdev->clk);

		dev_dbg(&pdev->dev, "GPU clock set to %lu\n", rate);
	}

	if (func->require_ref_clk)
		tdev->clk_ref = devm_clk_get(&pdev->dev, "ref");
	if (IS_ERR(tdev->clk_ref)) {
		ret = PTR_ERR(tdev->clk_ref);
		goto free;
	}

	tdev->clk_pwr = devm_clk_get(&pdev->dev, "pwr");
	if (IS_ERR(tdev->clk_pwr)) {
		ret = PTR_ERR(tdev->clk_pwr);
		goto free;
	}

	/**
	 * The IOMMU bit defines the upper limit of the GPU-addressable space.
	 */
	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(tdev->func->iommu_bit));
	if (ret)
		goto free;

	nvkm_device_tegra_probe_iommu(tdev);

	ret = nvkm_device_tegra_power_up(tdev);
	if (ret)
		goto remove;

	tdev->gpu_speedo = tegra_sku_info.gpu_speedo_value;
	tdev->gpu_speedo_id = tegra_sku_info.gpu_speedo_id;
	ret = nvkm_device_ctor(&nvkm_device_tegra_func, NULL, &pdev->dev,
			       NVKM_DEVICE_TEGRA, pdev->id, NULL,
			       cfg, dbg, &tdev->device);
	if (ret)
		goto powerdown;

	*pdevice = &tdev->device;

	return 0;

powerdown:
	nvkm_device_tegra_power_down(tdev);
remove:
	nvkm_device_tegra_remove_iommu(tdev);
free:
	kfree(tdev);
	return ret;
}
#else
int
nvkm_device_tegra_new(const struct nvkm_device_tegra_func *func,
		      struct platform_device *pdev,
		      const char *cfg, const char *dbg,
		      struct nvkm_device **pdevice)
{
	return -ENOSYS;
}
#endif
