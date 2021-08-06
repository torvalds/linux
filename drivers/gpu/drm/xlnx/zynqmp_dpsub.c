// SPDX-License-Identifier: GPL-2.0
/*
 * ZynqMP DisplayPort Subsystem Driver
 *
 * Copyright (C) 2017 - 2020 Xilinx, Inc.
 *
 * Authors:
 * - Hyun Woo Kwon <hyun.kwon@xilinx.com>
 * - Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_module.h>

#include "zynqmp_disp.h"
#include "zynqmp_dp.h"
#include "zynqmp_dpsub.h"
#include "zynqmp_kms.h"

/* -----------------------------------------------------------------------------
 * Power Management
 */

static int __maybe_unused zynqmp_dpsub_suspend(struct device *dev)
{
	struct zynqmp_dpsub *dpsub = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(&dpsub->drm);
}

static int __maybe_unused zynqmp_dpsub_resume(struct device *dev)
{
	struct zynqmp_dpsub *dpsub = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(&dpsub->drm);
}

static const struct dev_pm_ops zynqmp_dpsub_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(zynqmp_dpsub_suspend, zynqmp_dpsub_resume)
};

/* -----------------------------------------------------------------------------
 * DPSUB Configuration
 */

/**
 * zynqmp_dpsub_audio_enabled - If the audio is enabled
 * @dpsub: DisplayPort subsystem
 *
 * Return if the audio is enabled depending on the audio clock.
 *
 * Return: true if audio is enabled, or false.
 */
bool zynqmp_dpsub_audio_enabled(struct zynqmp_dpsub *dpsub)
{
	return !!dpsub->aud_clk;
}

/**
 * zynqmp_dpsub_get_audio_clk_rate - Get the current audio clock rate
 * @dpsub: DisplayPort subsystem
 *
 * Return: the current audio clock rate.
 */
unsigned int zynqmp_dpsub_get_audio_clk_rate(struct zynqmp_dpsub *dpsub)
{
	if (zynqmp_dpsub_audio_enabled(dpsub))
		return 0;
	return clk_get_rate(dpsub->aud_clk);
}

/* -----------------------------------------------------------------------------
 * Probe & Remove
 */

static int zynqmp_dpsub_init_clocks(struct zynqmp_dpsub *dpsub)
{
	int ret;

	dpsub->apb_clk = devm_clk_get(dpsub->dev, "dp_apb_clk");
	if (IS_ERR(dpsub->apb_clk))
		return PTR_ERR(dpsub->apb_clk);

	ret = clk_prepare_enable(dpsub->apb_clk);
	if (ret) {
		dev_err(dpsub->dev, "failed to enable the APB clock\n");
		return ret;
	}

	/*
	 * Try the live PL video clock, and fall back to the PS clock if the
	 * live PL video clock isn't valid.
	 */
	dpsub->vid_clk = devm_clk_get(dpsub->dev, "dp_live_video_in_clk");
	if (!IS_ERR(dpsub->vid_clk))
		dpsub->vid_clk_from_ps = false;
	else if (PTR_ERR(dpsub->vid_clk) == -EPROBE_DEFER)
		return PTR_ERR(dpsub->vid_clk);

	if (IS_ERR_OR_NULL(dpsub->vid_clk)) {
		dpsub->vid_clk = devm_clk_get(dpsub->dev, "dp_vtc_pixel_clk_in");
		if (IS_ERR(dpsub->vid_clk)) {
			dev_err(dpsub->dev, "failed to init any video clock\n");
			return PTR_ERR(dpsub->vid_clk);
		}
		dpsub->vid_clk_from_ps = true;
	}

	/*
	 * Try the live PL audio clock, and fall back to the PS clock if the
	 * live PL audio clock isn't valid. Missing audio clock disables audio
	 * but isn't an error.
	 */
	dpsub->aud_clk = devm_clk_get(dpsub->dev, "dp_live_audio_aclk");
	if (!IS_ERR(dpsub->aud_clk)) {
		dpsub->aud_clk_from_ps = false;
		return 0;
	}

	dpsub->aud_clk = devm_clk_get(dpsub->dev, "dp_aud_clk");
	if (!IS_ERR(dpsub->aud_clk)) {
		dpsub->aud_clk_from_ps = true;
		return 0;
	}

	dev_info(dpsub->dev, "audio disabled due to missing clock\n");
	return 0;
}

static void zynqmp_dpsub_release(struct drm_device *drm, void *res)
{
	struct zynqmp_dpsub *dpsub = res;

	kfree(dpsub->disp);
	kfree(dpsub->dp);
}

static int zynqmp_dpsub_probe(struct platform_device *pdev)
{
	struct zynqmp_dpsub *dpsub;
	int ret;

	/* Allocate private data. */
	dpsub = devm_drm_dev_alloc(&pdev->dev, &zynqmp_dpsub_drm_driver,
				   struct zynqmp_dpsub, drm);
	if (IS_ERR(dpsub))
		return PTR_ERR(dpsub);

	ret = drmm_add_action(&dpsub->drm, zynqmp_dpsub_release, dpsub);
	if (ret < 0)
		return ret;

	dpsub->dev = &pdev->dev;
	platform_set_drvdata(pdev, dpsub);

	dma_set_mask(dpsub->dev, DMA_BIT_MASK(ZYNQMP_DISP_MAX_DMA_BIT));

	/* Try the reserved memory. Proceed if there's none. */
	of_reserved_mem_device_init(&pdev->dev);

	ret = zynqmp_dpsub_init_clocks(dpsub);
	if (ret < 0)
		goto err_mem;

	pm_runtime_enable(&pdev->dev);

	/*
	 * DP should be probed first so that the zynqmp_disp can set the output
	 * format accordingly.
	 */
	ret = zynqmp_dp_probe(dpsub);
	if (ret)
		goto err_pm;

	ret = zynqmp_disp_probe(dpsub);
	if (ret)
		goto err_dp;

	ret = zynqmp_dpsub_drm_init(dpsub);
	if (ret)
		goto err_disp;

	dev_info(&pdev->dev, "ZynqMP DisplayPort Subsystem driver probed");

	return 0;

err_disp:
	zynqmp_disp_remove(dpsub);
err_dp:
	zynqmp_dp_remove(dpsub);
err_pm:
	pm_runtime_disable(&pdev->dev);
	clk_disable_unprepare(dpsub->apb_clk);
err_mem:
	of_reserved_mem_device_release(&pdev->dev);
	return ret;
}

static int zynqmp_dpsub_remove(struct platform_device *pdev)
{
	struct zynqmp_dpsub *dpsub = platform_get_drvdata(pdev);

	zynqmp_dpsub_drm_cleanup(dpsub);

	zynqmp_disp_remove(dpsub);
	zynqmp_dp_remove(dpsub);

	pm_runtime_disable(&pdev->dev);
	clk_disable_unprepare(dpsub->apb_clk);
	of_reserved_mem_device_release(&pdev->dev);

	return 0;
}

static void zynqmp_dpsub_shutdown(struct platform_device *pdev)
{
	struct zynqmp_dpsub *dpsub = platform_get_drvdata(pdev);

	drm_atomic_helper_shutdown(&dpsub->drm);
}

static const struct of_device_id zynqmp_dpsub_of_match[] = {
	{ .compatible = "xlnx,zynqmp-dpsub-1.7", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, zynqmp_dpsub_of_match);

static struct platform_driver zynqmp_dpsub_driver = {
	.probe			= zynqmp_dpsub_probe,
	.remove			= zynqmp_dpsub_remove,
	.shutdown		= zynqmp_dpsub_shutdown,
	.driver			= {
		.name		= "zynqmp-dpsub",
		.pm		= &zynqmp_dpsub_pm_ops,
		.of_match_table	= zynqmp_dpsub_of_match,
	},
};

drm_module_platform_driver(zynqmp_dpsub_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("ZynqMP DP Subsystem Driver");
MODULE_LICENSE("GPL v2");
