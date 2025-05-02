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
#include <linux/of_graph.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
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

	if (!dpsub->drm)
		return 0;

	return drm_mode_config_helper_suspend(&dpsub->drm->dev);
}

static int __maybe_unused zynqmp_dpsub_resume(struct device *dev)
{
	struct zynqmp_dpsub *dpsub = dev_get_drvdata(dev);

	if (!dpsub->drm)
		return 0;

	return drm_mode_config_helper_resume(&dpsub->drm->dev);
}

static const struct dev_pm_ops zynqmp_dpsub_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(zynqmp_dpsub_suspend, zynqmp_dpsub_resume)
};

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

static int zynqmp_dpsub_parse_dt(struct zynqmp_dpsub *dpsub)
{
	struct device_node *np;
	unsigned int i;

	/*
	 * For backward compatibility with old device trees that don't contain
	 * ports, consider that only the DP output port is connected if no
	 * ports child no exists.
	 */
	np = of_get_child_by_name(dpsub->dev->of_node, "ports");
	of_node_put(np);
	if (!np) {
		dev_warn(dpsub->dev, "missing ports, update DT bindings\n");
		dpsub->connected_ports = BIT(ZYNQMP_DPSUB_PORT_OUT_DP);
		dpsub->dma_enabled = true;
		return 0;
	}

	/* Check which ports are connected. */
	for (i = 0; i < ZYNQMP_DPSUB_NUM_PORTS; ++i) {
		struct device_node *np;

		np = of_graph_get_remote_node(dpsub->dev->of_node, i, -1);
		if (np) {
			dpsub->connected_ports |= BIT(i);
			of_node_put(np);
		}
	}

	/* Sanity checks. */
	if ((dpsub->connected_ports & BIT(ZYNQMP_DPSUB_PORT_LIVE_VIDEO)) &&
	    (dpsub->connected_ports & BIT(ZYNQMP_DPSUB_PORT_LIVE_GFX))) {
		dev_err(dpsub->dev, "only one live video input is supported\n");
		return -EINVAL;
	}

	if ((dpsub->connected_ports & BIT(ZYNQMP_DPSUB_PORT_LIVE_VIDEO)) ||
	    (dpsub->connected_ports & BIT(ZYNQMP_DPSUB_PORT_LIVE_GFX))) {
		if (dpsub->vid_clk_from_ps) {
			dev_err(dpsub->dev,
				"live video input requires PL clock\n");
			return -EINVAL;
		}
	} else {
		dpsub->dma_enabled = true;
	}

	if (dpsub->connected_ports & BIT(ZYNQMP_DPSUB_PORT_LIVE_AUDIO))
		dev_warn(dpsub->dev, "live audio unsupported, ignoring\n");

	if ((dpsub->connected_ports & BIT(ZYNQMP_DPSUB_PORT_OUT_VIDEO)) ||
	    (dpsub->connected_ports & BIT(ZYNQMP_DPSUB_PORT_OUT_AUDIO)))
		dev_warn(dpsub->dev, "output to PL unsupported, ignoring\n");

	if (!(dpsub->connected_ports & BIT(ZYNQMP_DPSUB_PORT_OUT_DP))) {
		dev_err(dpsub->dev, "DP output port not connected\n");
		return -EINVAL;
	}

	return 0;
}

void zynqmp_dpsub_release(struct zynqmp_dpsub *dpsub)
{
	kfree(dpsub->disp);
	kfree(dpsub->dp);
	kfree(dpsub);
}

static int zynqmp_dpsub_probe(struct platform_device *pdev)
{
	struct zynqmp_dpsub *dpsub;
	int ret;

	/* Allocate private data. */
	dpsub = kzalloc(sizeof(*dpsub), GFP_KERNEL);
	if (!dpsub)
		return -ENOMEM;

	dpsub->dev = &pdev->dev;
	platform_set_drvdata(pdev, dpsub);

	ret = dma_set_mask(dpsub->dev, DMA_BIT_MASK(ZYNQMP_DISP_MAX_DMA_BIT));
	if (ret)
		return ret;

	dma_set_max_seg_size(&pdev->dev, DMA_BIT_MASK(32));

	/* Try the reserved memory. Proceed if there's none. */
	of_reserved_mem_device_init(&pdev->dev);

	ret = zynqmp_dpsub_init_clocks(dpsub);
	if (ret < 0)
		goto err_mem;

	ret = zynqmp_dpsub_parse_dt(dpsub);
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

	drm_bridge_add(dpsub->bridge);

	if (dpsub->dma_enabled) {
		ret = zynqmp_dpsub_drm_init(dpsub);
		if (ret)
			goto err_disp;
	}

	ret = zynqmp_audio_init(dpsub);
	if (ret)
		goto err_drm_cleanup;

	dev_info(&pdev->dev, "ZynqMP DisplayPort Subsystem driver probed");

	return 0;

err_drm_cleanup:
	if (dpsub->drm)
		zynqmp_dpsub_drm_cleanup(dpsub);
err_disp:
	drm_bridge_remove(dpsub->bridge);
	zynqmp_disp_remove(dpsub);
err_dp:
	zynqmp_dp_remove(dpsub);
err_pm:
	pm_runtime_disable(&pdev->dev);
	clk_disable_unprepare(dpsub->apb_clk);
err_mem:
	of_reserved_mem_device_release(&pdev->dev);
	if (!dpsub->drm)
		zynqmp_dpsub_release(dpsub);
	return ret;
}

static void zynqmp_dpsub_remove(struct platform_device *pdev)
{
	struct zynqmp_dpsub *dpsub = platform_get_drvdata(pdev);

	zynqmp_audio_uninit(dpsub);

	if (dpsub->drm)
		zynqmp_dpsub_drm_cleanup(dpsub);

	drm_bridge_remove(dpsub->bridge);
	zynqmp_disp_remove(dpsub);
	zynqmp_dp_remove(dpsub);

	pm_runtime_disable(&pdev->dev);
	clk_disable_unprepare(dpsub->apb_clk);
	of_reserved_mem_device_release(&pdev->dev);

	if (!dpsub->drm)
		zynqmp_dpsub_release(dpsub);
}

static void zynqmp_dpsub_shutdown(struct platform_device *pdev)
{
	struct zynqmp_dpsub *dpsub = platform_get_drvdata(pdev);

	if (!dpsub->drm)
		return;

	drm_atomic_helper_shutdown(&dpsub->drm->dev);
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
