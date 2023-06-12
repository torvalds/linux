// SPDX-License-Identifier: GPL-2.0+
/*
 * NVIDIA Tegra Video decoder driver
 *
 * Copyright (C) 2016-2017 Dmitry Osipenko <digetx@gmail.com>
 *
 */

#include <linux/clk.h>
#include <linux/dma-buf.h>
#include <linux/genalloc.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <soc/tegra/common.h>
#include <soc/tegra/pmc.h>

#include "vde.h"

#define CREATE_TRACE_POINTS
#include "trace.h"

void tegra_vde_writel(struct tegra_vde *vde, u32 value,
		      void __iomem *base, u32 offset)
{
	trace_vde_writel(vde, base, offset, value);

	writel_relaxed(value, base + offset);
}

u32 tegra_vde_readl(struct tegra_vde *vde, void __iomem *base, u32 offset)
{
	u32 value = readl_relaxed(base + offset);

	trace_vde_readl(vde, base, offset, value);

	return value;
}

void tegra_vde_set_bits(struct tegra_vde *vde, u32 mask,
			void __iomem *base, u32 offset)
{
	u32 value = tegra_vde_readl(vde, base, offset);

	tegra_vde_writel(vde, value | mask, base, offset);
}

int tegra_vde_alloc_bo(struct tegra_vde *vde,
		       struct tegra_vde_bo **ret_bo,
		       enum dma_data_direction dma_dir,
		       size_t size)
{
	struct device *dev = vde->dev;
	struct tegra_vde_bo *bo;
	int err;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return -ENOMEM;

	bo->vde = vde;
	bo->size = size;
	bo->dma_dir = dma_dir;
	bo->dma_attrs = DMA_ATTR_WRITE_COMBINE |
			DMA_ATTR_NO_KERNEL_MAPPING;

	if (!vde->domain)
		bo->dma_attrs |= DMA_ATTR_FORCE_CONTIGUOUS;

	bo->dma_cookie = dma_alloc_attrs(dev, bo->size, &bo->dma_handle,
					 GFP_KERNEL, bo->dma_attrs);
	if (!bo->dma_cookie) {
		dev_err(dev, "Failed to allocate DMA buffer of size: %zu\n",
			bo->size);
		err = -ENOMEM;
		goto free_bo;
	}

	err = dma_get_sgtable_attrs(dev, &bo->sgt, bo->dma_cookie,
				    bo->dma_handle, bo->size, bo->dma_attrs);
	if (err) {
		dev_err(dev, "Failed to get DMA buffer SG table: %d\n", err);
		goto free_attrs;
	}

	err = dma_map_sgtable(dev, &bo->sgt, bo->dma_dir, bo->dma_attrs);
	if (err) {
		dev_err(dev, "Failed to map DMA buffer SG table: %d\n", err);
		goto free_table;
	}

	if (vde->domain) {
		err = tegra_vde_iommu_map(vde, &bo->sgt, &bo->iova, bo->size);
		if (err) {
			dev_err(dev, "Failed to map DMA buffer IOVA: %d\n", err);
			goto unmap_sgtable;
		}

		bo->dma_addr = iova_dma_addr(&vde->iova, bo->iova);
	} else {
		bo->dma_addr = sg_dma_address(bo->sgt.sgl);
	}

	*ret_bo = bo;

	return 0;

unmap_sgtable:
	dma_unmap_sgtable(dev, &bo->sgt, bo->dma_dir, bo->dma_attrs);
free_table:
	sg_free_table(&bo->sgt);
free_attrs:
	dma_free_attrs(dev, bo->size, bo->dma_cookie, bo->dma_handle,
		       bo->dma_attrs);
free_bo:
	kfree(bo);

	return err;
}

void tegra_vde_free_bo(struct tegra_vde_bo *bo)
{
	struct tegra_vde *vde = bo->vde;
	struct device *dev = vde->dev;

	if (vde->domain)
		tegra_vde_iommu_unmap(vde, bo->iova);

	dma_unmap_sgtable(dev, &bo->sgt, bo->dma_dir, bo->dma_attrs);

	sg_free_table(&bo->sgt);

	dma_free_attrs(dev, bo->size, bo->dma_cookie, bo->dma_handle,
		       bo->dma_attrs);
	kfree(bo);
}

static irqreturn_t tegra_vde_isr(int irq, void *data)
{
	struct tegra_vde *vde = data;

	if (completion_done(&vde->decode_completion))
		return IRQ_NONE;

	tegra_vde_set_bits(vde, 0, vde->frameid, 0x208);
	complete(&vde->decode_completion);

	return IRQ_HANDLED;
}

static __maybe_unused int tegra_vde_runtime_suspend(struct device *dev)
{
	struct tegra_vde *vde = dev_get_drvdata(dev);
	int err;

	if (!dev->pm_domain) {
		err = tegra_powergate_power_off(TEGRA_POWERGATE_VDEC);
		if (err) {
			dev_err(dev, "Failed to power down HW: %d\n", err);
			return err;
		}
	}

	clk_disable_unprepare(vde->clk);
	reset_control_release(vde->rst);
	reset_control_release(vde->rst_mc);

	return 0;
}

static __maybe_unused int tegra_vde_runtime_resume(struct device *dev)
{
	struct tegra_vde *vde = dev_get_drvdata(dev);
	int err;

	err = reset_control_acquire(vde->rst_mc);
	if (err) {
		dev_err(dev, "Failed to acquire mc reset: %d\n", err);
		return err;
	}

	err = reset_control_acquire(vde->rst);
	if (err) {
		dev_err(dev, "Failed to acquire reset: %d\n", err);
		goto release_mc_reset;
	}

	if (!dev->pm_domain) {
		err = tegra_powergate_sequence_power_up(TEGRA_POWERGATE_VDEC,
							vde->clk, vde->rst);
		if (err) {
			dev_err(dev, "Failed to power up HW : %d\n", err);
			goto release_reset;
		}
	} else {
		/*
		 * tegra_powergate_sequence_power_up() leaves clocks enabled,
		 * while GENPD not.
		 */
		err = clk_prepare_enable(vde->clk);
		if (err) {
			dev_err(dev, "Failed to enable clock: %d\n", err);
			goto release_reset;
		}
	}

	return 0;

release_reset:
	reset_control_release(vde->rst);
release_mc_reset:
	reset_control_release(vde->rst_mc);

	return err;
}

static int tegra_vde_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra_vde *vde;
	int irq, err;

	vde = devm_kzalloc(dev, sizeof(*vde), GFP_KERNEL);
	if (!vde)
		return -ENOMEM;

	platform_set_drvdata(pdev, vde);

	vde->soc = of_device_get_match_data(&pdev->dev);
	vde->dev = dev;

	vde->sxe = devm_platform_ioremap_resource_byname(pdev, "sxe");
	if (IS_ERR(vde->sxe))
		return PTR_ERR(vde->sxe);

	vde->bsev = devm_platform_ioremap_resource_byname(pdev, "bsev");
	if (IS_ERR(vde->bsev))
		return PTR_ERR(vde->bsev);

	vde->mbe = devm_platform_ioremap_resource_byname(pdev, "mbe");
	if (IS_ERR(vde->mbe))
		return PTR_ERR(vde->mbe);

	vde->ppe = devm_platform_ioremap_resource_byname(pdev, "ppe");
	if (IS_ERR(vde->ppe))
		return PTR_ERR(vde->ppe);

	vde->mce = devm_platform_ioremap_resource_byname(pdev, "mce");
	if (IS_ERR(vde->mce))
		return PTR_ERR(vde->mce);

	vde->tfe = devm_platform_ioremap_resource_byname(pdev, "tfe");
	if (IS_ERR(vde->tfe))
		return PTR_ERR(vde->tfe);

	vde->ppb = devm_platform_ioremap_resource_byname(pdev, "ppb");
	if (IS_ERR(vde->ppb))
		return PTR_ERR(vde->ppb);

	vde->vdma = devm_platform_ioremap_resource_byname(pdev, "vdma");
	if (IS_ERR(vde->vdma))
		return PTR_ERR(vde->vdma);

	vde->frameid = devm_platform_ioremap_resource_byname(pdev, "frameid");
	if (IS_ERR(vde->frameid))
		return PTR_ERR(vde->frameid);

	vde->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(vde->clk)) {
		err = PTR_ERR(vde->clk);
		dev_err(dev, "Could not get VDE clk %d\n", err);
		return err;
	}

	vde->rst = devm_reset_control_get_exclusive_released(dev, NULL);
	if (IS_ERR(vde->rst)) {
		err = PTR_ERR(vde->rst);
		dev_err(dev, "Could not get VDE reset %d\n", err);
		return err;
	}

	vde->rst_mc = devm_reset_control_get_optional_exclusive_released(dev, "mc");
	if (IS_ERR(vde->rst_mc)) {
		err = PTR_ERR(vde->rst_mc);
		dev_err(dev, "Could not get MC reset %d\n", err);
		return err;
	}

	irq = platform_get_irq_byname(pdev, "sync-token");
	if (irq < 0)
		return irq;

	err = devm_request_irq(dev, irq, tegra_vde_isr, 0,
			       dev_name(dev), vde);
	if (err) {
		dev_err(dev, "Could not request IRQ %d\n", err);
		return err;
	}

	err = devm_tegra_core_dev_init_opp_table_common(dev);
	if (err) {
		dev_err(dev, "Could initialize OPP table %d\n", err);
		return err;
	}

	vde->iram_pool = of_gen_pool_get(dev->of_node, "iram", 0);
	if (!vde->iram_pool) {
		dev_err(dev, "Could not get IRAM pool\n");
		return -EPROBE_DEFER;
	}

	vde->iram = gen_pool_dma_alloc(vde->iram_pool,
				       gen_pool_size(vde->iram_pool),
				       &vde->iram_lists_addr);
	if (!vde->iram) {
		dev_err(dev, "Could not reserve IRAM\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&vde->map_list);
	mutex_init(&vde->map_lock);
	mutex_init(&vde->lock);
	init_completion(&vde->decode_completion);

	err = tegra_vde_iommu_init(vde);
	if (err) {
		dev_err(dev, "Failed to initialize IOMMU: %d\n", err);
		goto err_gen_free;
	}

	pm_runtime_enable(dev);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, 300);

	/*
	 * VDE partition may be left ON after bootloader, hence let's
	 * power-cycle it in order to put hardware into a predictable lower
	 * power state.
	 */
	err = pm_runtime_resume_and_get(dev);
	if (err)
		goto err_pm_runtime;

	pm_runtime_put(dev);

	err = tegra_vde_alloc_bo(vde, &vde->secure_bo, DMA_FROM_DEVICE, 4096);
	if (err) {
		dev_err(dev, "Failed to allocate secure BO: %d\n", err);
		goto err_pm_runtime;
	}

	err = tegra_vde_v4l2_init(vde);
	if (err) {
		dev_err(dev, "Failed to initialize V4L2: %d\n", err);
		goto err_free_secure_bo;
	}

	return 0;

err_free_secure_bo:
	tegra_vde_free_bo(vde->secure_bo);
err_pm_runtime:
	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_disable(dev);

	tegra_vde_iommu_deinit(vde);

err_gen_free:
	gen_pool_free(vde->iram_pool, (unsigned long)vde->iram,
		      gen_pool_size(vde->iram_pool));

	return err;
}

static void tegra_vde_remove(struct platform_device *pdev)
{
	struct tegra_vde *vde = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	tegra_vde_v4l2_deinit(vde);
	tegra_vde_free_bo(vde->secure_bo);

	/*
	 * As it increments RPM usage_count even on errors, we don't need to
	 * check the returned code here.
	 */
	pm_runtime_get_sync(dev);

	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_disable(dev);

	/*
	 * Balance RPM state, the VDE power domain is left ON and hardware
	 * is clock-gated. It's safe to reboot machine now.
	 */
	pm_runtime_put_noidle(dev);
	clk_disable_unprepare(vde->clk);

	tegra_vde_dmabuf_cache_unmap_all(vde);
	tegra_vde_iommu_deinit(vde);

	gen_pool_free(vde->iram_pool, (unsigned long)vde->iram,
		      gen_pool_size(vde->iram_pool));
}

static void tegra_vde_shutdown(struct platform_device *pdev)
{
	/*
	 * On some devices bootloader isn't ready to a power-gated VDE on
	 * a warm-reboot, machine will hang in that case.
	 */
	pm_runtime_get_sync(&pdev->dev);
}

static __maybe_unused int tegra_vde_pm_suspend(struct device *dev)
{
	struct tegra_vde *vde = dev_get_drvdata(dev);
	int err;

	mutex_lock(&vde->lock);

	err = pm_runtime_force_suspend(dev);
	if (err < 0)
		return err;

	return 0;
}

static __maybe_unused int tegra_vde_pm_resume(struct device *dev)
{
	struct tegra_vde *vde = dev_get_drvdata(dev);
	int err;

	err = pm_runtime_force_resume(dev);
	if (err < 0)
		return err;

	mutex_unlock(&vde->lock);

	return 0;
}

static const struct dev_pm_ops tegra_vde_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_vde_runtime_suspend,
			   tegra_vde_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(tegra_vde_pm_suspend,
				tegra_vde_pm_resume)
};

static const u32 tegra124_decoded_fmts[] = {
	/* TBD: T124 supports only a non-standard Tegra tiled format */
};

static const struct tegra_coded_fmt_desc tegra124_coded_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_H264_SLICE,
		.frmsize = {
			.min_width = 16,
			.max_width = 1920,
			.step_width = 16,
			.min_height = 16,
			.max_height = 2032,
			.step_height = 16,
		},
		.num_decoded_fmts = ARRAY_SIZE(tegra124_decoded_fmts),
		.decoded_fmts = tegra124_decoded_fmts,
		.decode_run = tegra_vde_h264_decode_run,
		.decode_wait = tegra_vde_h264_decode_wait,
	},
};

static const u32 tegra20_decoded_fmts[] = {
	V4L2_PIX_FMT_YUV420M,
	V4L2_PIX_FMT_YVU420M,
};

static const struct tegra_coded_fmt_desc tegra20_coded_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_H264_SLICE,
		.frmsize = {
			.min_width = 16,
			.max_width = 1920,
			.step_width = 16,
			.min_height = 16,
			.max_height = 2032,
			.step_height = 16,
		},
		.num_decoded_fmts = ARRAY_SIZE(tegra20_decoded_fmts),
		.decoded_fmts = tegra20_decoded_fmts,
		.decode_run = tegra_vde_h264_decode_run,
		.decode_wait = tegra_vde_h264_decode_wait,
	},
};

static const struct tegra_vde_soc tegra124_vde_soc = {
	.supports_ref_pic_marking = true,
	.coded_fmts = tegra124_coded_fmts,
	.num_coded_fmts = ARRAY_SIZE(tegra124_coded_fmts),
};

static const struct tegra_vde_soc tegra114_vde_soc = {
	.supports_ref_pic_marking = true,
	.coded_fmts = tegra20_coded_fmts,
	.num_coded_fmts = ARRAY_SIZE(tegra20_coded_fmts),
};

static const struct tegra_vde_soc tegra30_vde_soc = {
	.supports_ref_pic_marking = false,
	.coded_fmts = tegra20_coded_fmts,
	.num_coded_fmts = ARRAY_SIZE(tegra20_coded_fmts),
};

static const struct tegra_vde_soc tegra20_vde_soc = {
	.supports_ref_pic_marking = false,
	.coded_fmts = tegra20_coded_fmts,
	.num_coded_fmts = ARRAY_SIZE(tegra20_coded_fmts),
};

static const struct of_device_id tegra_vde_of_match[] = {
	{ .compatible = "nvidia,tegra124-vde", .data = &tegra124_vde_soc },
	{ .compatible = "nvidia,tegra114-vde", .data = &tegra114_vde_soc },
	{ .compatible = "nvidia,tegra30-vde", .data = &tegra30_vde_soc },
	{ .compatible = "nvidia,tegra20-vde", .data = &tegra20_vde_soc },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_vde_of_match);

static struct platform_driver tegra_vde_driver = {
	.probe		= tegra_vde_probe,
	.remove_new	= tegra_vde_remove,
	.shutdown	= tegra_vde_shutdown,
	.driver		= {
		.name		= "tegra-vde",
		.of_match_table = tegra_vde_of_match,
		.pm		= &tegra_vde_pm_ops,
	},
};
module_platform_driver(tegra_vde_driver);

MODULE_DESCRIPTION("NVIDIA Tegra Video Decoder driver");
MODULE_AUTHOR("Dmitry Osipenko <digetx@gmail.com>");
MODULE_LICENSE("GPL");
