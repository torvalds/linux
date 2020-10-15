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

#include <drm/drm_atomic_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_mode_config.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "zynqmp_disp.h"
#include "zynqmp_dp.h"
#include "zynqmp_dpsub.h"

/* -----------------------------------------------------------------------------
 * Dumb Buffer & Framebuffer Allocation
 */

static int zynqmp_dpsub_dumb_create(struct drm_file *file_priv,
				    struct drm_device *drm,
				    struct drm_mode_create_dumb *args)
{
	struct zynqmp_dpsub *dpsub = to_zynqmp_dpsub(drm);
	unsigned int pitch = DIV_ROUND_UP(args->width * args->bpp, 8);

	/* Enforce the alignment constraints of the DMA engine. */
	args->pitch = ALIGN(pitch, dpsub->dma_align);

	return drm_gem_cma_dumb_create_internal(file_priv, drm, args);
}

static struct drm_framebuffer *
zynqmp_dpsub_fb_create(struct drm_device *drm, struct drm_file *file_priv,
		       const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct zynqmp_dpsub *dpsub = to_zynqmp_dpsub(drm);
	struct drm_mode_fb_cmd2 cmd = *mode_cmd;
	unsigned int i;

	/* Enforce the alignment constraints of the DMA engine. */
	for (i = 0; i < ARRAY_SIZE(cmd.pitches); ++i)
		cmd.pitches[i] = ALIGN(cmd.pitches[i], dpsub->dma_align);

	return drm_gem_fb_create(drm, file_priv, &cmd);
}

static const struct drm_mode_config_funcs zynqmp_dpsub_mode_config_funcs = {
	.fb_create		= zynqmp_dpsub_fb_create,
	.atomic_check		= drm_atomic_helper_check,
	.atomic_commit		= drm_atomic_helper_commit,
};

/* -----------------------------------------------------------------------------
 * DRM/KMS Driver
 */

DEFINE_DRM_GEM_CMA_FOPS(zynqmp_dpsub_drm_fops);

static struct drm_driver zynqmp_dpsub_drm_driver = {
	.driver_features		= DRIVER_MODESET | DRIVER_GEM |
					  DRIVER_ATOMIC,

	.prime_handle_to_fd		= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle		= drm_gem_prime_fd_to_handle,
	.gem_prime_export		= drm_gem_prime_export,
	.gem_prime_import		= drm_gem_prime_import,
	.gem_prime_get_sg_table		= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table	= drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap			= drm_gem_cma_prime_vmap,
	.gem_prime_vunmap		= drm_gem_cma_prime_vunmap,
	.gem_prime_mmap			= drm_gem_cma_prime_mmap,
	.gem_free_object_unlocked	= drm_gem_cma_free_object,
	.gem_vm_ops			= &drm_gem_cma_vm_ops,
	.dumb_create			= zynqmp_dpsub_dumb_create,
	.dumb_destroy			= drm_gem_dumb_destroy,

	.fops				= &zynqmp_dpsub_drm_fops,

	.name				= "zynqmp-dpsub",
	.desc				= "Xilinx DisplayPort Subsystem Driver",
	.date				= "20130509",
	.major				= 1,
	.minor				= 0,
};

static int zynqmp_dpsub_drm_init(struct zynqmp_dpsub *dpsub)
{
	struct drm_device *drm = &dpsub->drm;
	int ret;

	/* Initialize mode config, vblank and the KMS poll helper. */
	ret = drmm_mode_config_init(drm);
	if (ret < 0)
		return ret;

	drm->mode_config.funcs = &zynqmp_dpsub_mode_config_funcs;
	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_width = ZYNQMP_DISP_MAX_WIDTH;
	drm->mode_config.max_height = ZYNQMP_DISP_MAX_HEIGHT;

	ret = drm_vblank_init(drm, 1);
	if (ret)
		return ret;

	drm->irq_enabled = 1;

	drm_kms_helper_poll_init(drm);

	/*
	 * Initialize the DISP and DP components. This will creates planes,
	 * CRTC, encoder and connector. The DISP should be initialized first as
	 * the DP encoder needs the CRTC.
	 */
	ret = zynqmp_disp_drm_init(dpsub);
	if (ret)
		goto err_poll_fini;

	ret = zynqmp_dp_drm_init(dpsub);
	if (ret)
		goto err_poll_fini;

	/* Reset all components and register the DRM device. */
	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret < 0)
		goto err_poll_fini;

	/* Initialize fbdev generic emulation. */
	drm_fbdev_generic_setup(drm, 24);

	return 0;

err_poll_fini:
	drm_kms_helper_poll_fini(drm);
	return ret;
}

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

	return 0;
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
	ret = zynqmp_dp_probe(dpsub, &dpsub->drm);
	if (ret)
		goto err_pm;

	ret = zynqmp_disp_probe(dpsub, &dpsub->drm);
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
	struct drm_device *drm = &dpsub->drm;

	drm_dev_unregister(drm);
	drm_atomic_helper_shutdown(drm);
	drm_kms_helper_poll_fini(drm);

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

module_platform_driver(zynqmp_dpsub_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("ZynqMP DP Subsystem Driver");
MODULE_LICENSE("GPL v2");
