// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Linus Walleij <linus.walleij@linaro.org>
 * Parts of this file were based on the MCDE driver by Marcus Lorentzon
 * (C) ST-Ericsson SA 2013
 */

/**
 * DOC: ST-Ericsson MCDE Driver
 *
 * The MCDE (short for multi-channel display engine) is a graphics
 * controller found in the Ux500 chipsets, such as NovaThor U8500.
 * It was initially conceptualized by ST Microelectronics for the
 * successor of the Nomadik line, STn8500 but productified in the
 * ST-Ericsson U8500 where is was used for mass-market deployments
 * in Android phones from Samsung and Sony Ericsson.
 *
 * It can do 1080p30 on SDTV CCIR656, DPI-2, DBI-2 or DSI for
 * panels with or without frame buffering and can convert most
 * input formats including most variants of RGB and YUV.
 *
 * The hardware has four display pipes, and the layout is a little
 * bit like this::
 *
 *   Memory     -> Overlay -> Channel -> FIFO -> 5 formatters -> DSI/DPI
 *   External      0..5       0..3       A,B,    3 x DSI         bridge
 *   source 0..9                         C0,C1   2 x DPI
 *
 * FIFOs A and B are for LCD and HDMI while FIFO CO/C1 are for
 * panels with embedded buffer.
 * 3 of the formatters are for DSI.
 * 2 of the formatters are for DPI.
 *
 * Behind the formatters are the DSI or DPI ports that route to
 * the external pins of the chip. As there are 3 DSI ports and one
 * DPI port, it is possible to configure up to 4 display pipelines
 * (effectively using channels 0..3) for concurrent use.
 *
 * In the current DRM/KMS setup, we use one external source, one overlay,
 * one FIFO and one formatter which we connect to the simple CMA framebuffer
 * helpers. We then provide a bridge to the DSI port, and on the DSI port
 * bridge we connect hang a panel bridge or other bridge. This may be subject
 * to change as we exploit more of the hardware capabilities.
 *
 * TODO:
 *
 * - Enabled damaged rectangles using drm_plane_enable_fb_damage_clips()
 *   so we can selectively just transmit the damaged area to a
 *   command-only display.
 * - Enable mixing of more planes, possibly at the cost of moving away
 *   from using the simple framebuffer pipeline.
 * - Enable output to bridges such as the AV8100 HDMI encoder from
 *   the DSI bridge.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/dma-buf.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_vblank.h>

#include "mcde_drm.h"

#define DRIVER_DESC	"DRM module for MCDE"

#define MCDE_PID 0x000001FC
#define MCDE_PID_METALFIX_VERSION_SHIFT 0
#define MCDE_PID_METALFIX_VERSION_MASK 0x000000FF
#define MCDE_PID_DEVELOPMENT_VERSION_SHIFT 8
#define MCDE_PID_DEVELOPMENT_VERSION_MASK 0x0000FF00
#define MCDE_PID_MINOR_VERSION_SHIFT 16
#define MCDE_PID_MINOR_VERSION_MASK 0x00FF0000
#define MCDE_PID_MAJOR_VERSION_SHIFT 24
#define MCDE_PID_MAJOR_VERSION_MASK 0xFF000000

static const struct drm_mode_config_funcs mcde_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static const struct drm_mode_config_helper_funcs mcde_mode_config_helpers = {
	/*
	 * Using this function is necessary to commit atomic updates
	 * that need the CRTC to be enabled before a commit, as is
	 * the case with e.g. DSI displays.
	 */
	.atomic_commit_tail = drm_atomic_helper_commit_tail_rpm,
};

static irqreturn_t mcde_irq(int irq, void *data)
{
	struct mcde *mcde = data;
	u32 val;

	val = readl(mcde->regs + MCDE_MISERR);

	mcde_display_irq(mcde);

	if (val)
		dev_info(mcde->dev, "some error IRQ\n");
	writel(val, mcde->regs + MCDE_RISERR);

	return IRQ_HANDLED;
}

static int mcde_modeset_init(struct drm_device *drm)
{
	struct drm_mode_config *mode_config;
	struct mcde *mcde = to_mcde(drm);
	int ret;

	if (!mcde->bridge) {
		dev_err(drm->dev, "no display output bridge yet\n");
		return -EPROBE_DEFER;
	}

	mode_config = &drm->mode_config;
	mode_config->funcs = &mcde_mode_config_funcs;
	mode_config->helper_private = &mcde_mode_config_helpers;
	/* This hardware can do 1080p */
	mode_config->min_width = 1;
	mode_config->max_width = 1920;
	mode_config->min_height = 1;
	mode_config->max_height = 1080;

	ret = drm_vblank_init(drm, 1);
	if (ret) {
		dev_err(drm->dev, "failed to init vblank\n");
		return ret;
	}

	ret = mcde_display_init(drm);
	if (ret) {
		dev_err(drm->dev, "failed to init display\n");
		return ret;
	}

	/*
	 * Attach the DSI bridge
	 *
	 * TODO: when adding support for the DPI bridge or several DSI bridges,
	 * we selectively connect the bridge(s) here instead of this simple
	 * attachment.
	 */
	ret = drm_simple_display_pipe_attach_bridge(&mcde->pipe,
						    mcde->bridge);
	if (ret) {
		dev_err(drm->dev, "failed to attach display output bridge\n");
		return ret;
	}

	drm_mode_config_reset(drm);
	drm_kms_helper_poll_init(drm);

	return 0;
}

DEFINE_DRM_GEM_CMA_FOPS(drm_fops);

static const struct drm_driver mcde_drm_driver = {
	.driver_features =
		DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.lastclose = drm_fb_helper_lastclose,
	.ioctls = NULL,
	.fops = &drm_fops,
	.name = "mcde",
	.desc = DRIVER_DESC,
	.date = "20180529",
	.major = 1,
	.minor = 0,
	.patchlevel = 0,
	DRM_GEM_CMA_DRIVER_OPS,
};

static int mcde_drm_bind(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	int ret;

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	ret = component_bind_all(drm->dev, drm);
	if (ret) {
		dev_err(dev, "can't bind component devices\n");
		return ret;
	}

	ret = mcde_modeset_init(drm);
	if (ret)
		goto unbind;

	ret = drm_dev_register(drm, 0);
	if (ret < 0)
		goto unbind;

	drm_fbdev_generic_setup(drm, 32);

	return 0;

unbind:
	component_unbind_all(drm->dev, drm);
	return ret;
}

static void mcde_drm_unbind(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	drm_dev_unregister(drm);
	drm_atomic_helper_shutdown(drm);
	component_unbind_all(drm->dev, drm);
}

static const struct component_master_ops mcde_drm_comp_ops = {
	.bind = mcde_drm_bind,
	.unbind = mcde_drm_unbind,
};

static struct platform_driver *const mcde_component_drivers[] = {
	&mcde_dsi_driver,
};

static int mcde_compare_dev(struct device *dev, void *data)
{
	return dev == data;
}

static int mcde_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct drm_device *drm;
	struct mcde *mcde;
	struct component_match *match = NULL;
	struct resource *res;
	u32 pid;
	int irq;
	int ret;
	int i;

	mcde = devm_drm_dev_alloc(dev, &mcde_drm_driver, struct mcde, drm);
	if (IS_ERR(mcde))
		return PTR_ERR(mcde);
	drm = &mcde->drm;
	mcde->dev = dev;
	platform_set_drvdata(pdev, drm);

	/* First obtain and turn on the main power */
	mcde->epod = devm_regulator_get(dev, "epod");
	if (IS_ERR(mcde->epod)) {
		ret = PTR_ERR(mcde->epod);
		dev_err(dev, "can't get EPOD regulator\n");
		return ret;
	}
	ret = regulator_enable(mcde->epod);
	if (ret) {
		dev_err(dev, "can't enable EPOD regulator\n");
		return ret;
	}
	mcde->vana = devm_regulator_get(dev, "vana");
	if (IS_ERR(mcde->vana)) {
		ret = PTR_ERR(mcde->vana);
		dev_err(dev, "can't get VANA regulator\n");
		goto regulator_epod_off;
	}
	ret = regulator_enable(mcde->vana);
	if (ret) {
		dev_err(dev, "can't enable VANA regulator\n");
		goto regulator_epod_off;
	}
	/*
	 * The vendor code uses ESRAM (onchip RAM) and need to activate
	 * the v-esram34 regulator, but we don't use that yet
	 */

	/* Clock the silicon so we can access the registers */
	mcde->mcde_clk = devm_clk_get(dev, "mcde");
	if (IS_ERR(mcde->mcde_clk)) {
		dev_err(dev, "unable to get MCDE main clock\n");
		ret = PTR_ERR(mcde->mcde_clk);
		goto regulator_off;
	}
	ret = clk_prepare_enable(mcde->mcde_clk);
	if (ret) {
		dev_err(dev, "failed to enable MCDE main clock\n");
		goto regulator_off;
	}
	dev_info(dev, "MCDE clk rate %lu Hz\n", clk_get_rate(mcde->mcde_clk));

	mcde->lcd_clk = devm_clk_get(dev, "lcd");
	if (IS_ERR(mcde->lcd_clk)) {
		dev_err(dev, "unable to get LCD clock\n");
		ret = PTR_ERR(mcde->lcd_clk);
		goto clk_disable;
	}
	mcde->hdmi_clk = devm_clk_get(dev, "hdmi");
	if (IS_ERR(mcde->hdmi_clk)) {
		dev_err(dev, "unable to get HDMI clock\n");
		ret = PTR_ERR(mcde->hdmi_clk);
		goto clk_disable;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mcde->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(mcde->regs)) {
		dev_err(dev, "no MCDE regs\n");
		ret = -EINVAL;
		goto clk_disable;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto clk_disable;
	}

	ret = devm_request_irq(dev, irq, mcde_irq, 0, "mcde", mcde);
	if (ret) {
		dev_err(dev, "failed to request irq %d\n", ret);
		goto clk_disable;
	}

	/*
	 * Check hardware revision, we only support U8500v2 version
	 * as this was the only version used for mass market deployment,
	 * but surely you can add more versions if you have them and
	 * need them.
	 */
	pid = readl(mcde->regs + MCDE_PID);
	dev_info(dev, "found MCDE HW revision %d.%d (dev %d, metal fix %d)\n",
		 (pid & MCDE_PID_MAJOR_VERSION_MASK)
		 >> MCDE_PID_MAJOR_VERSION_SHIFT,
		 (pid & MCDE_PID_MINOR_VERSION_MASK)
		 >> MCDE_PID_MINOR_VERSION_SHIFT,
		 (pid & MCDE_PID_DEVELOPMENT_VERSION_MASK)
		 >> MCDE_PID_DEVELOPMENT_VERSION_SHIFT,
		 (pid & MCDE_PID_METALFIX_VERSION_MASK)
		 >> MCDE_PID_METALFIX_VERSION_SHIFT);
	if (pid != 0x03000800) {
		dev_err(dev, "unsupported hardware revision\n");
		ret = -ENODEV;
		goto clk_disable;
	}

	/* Disable and clear any pending interrupts */
	mcde_display_disable_irqs(mcde);
	writel(0, mcde->regs + MCDE_IMSCERR);
	writel(0xFFFFFFFF, mcde->regs + MCDE_RISERR);

	/* Spawn child devices for the DSI ports */
	devm_of_platform_populate(dev);

	/* Create something that will match the subdrivers when we bind */
	for (i = 0; i < ARRAY_SIZE(mcde_component_drivers); i++) {
		struct device_driver *drv = &mcde_component_drivers[i]->driver;
		struct device *p = NULL, *d;

		while ((d = platform_find_device_by_driver(p, drv))) {
			put_device(p);
			component_match_add(dev, &match, mcde_compare_dev, d);
			p = d;
		}
		put_device(p);
	}
	if (!match) {
		dev_err(dev, "no matching components\n");
		ret = -ENODEV;
		goto clk_disable;
	}
	if (IS_ERR(match)) {
		dev_err(dev, "could not create component match\n");
		ret = PTR_ERR(match);
		goto clk_disable;
	}

	/*
	 * Perform an invasive reset of the MCDE and all blocks by
	 * cutting the power to the subsystem, then bring it back up
	 * later when we enable the display as a result of
	 * component_master_add_with_match().
	 */
	ret = regulator_disable(mcde->epod);
	if (ret) {
		dev_err(dev, "can't disable EPOD regulator\n");
		return ret;
	}
	/* Wait 50 ms so we are sure we cut the power */
	usleep_range(50000, 70000);

	ret = component_master_add_with_match(&pdev->dev, &mcde_drm_comp_ops,
					      match);
	if (ret) {
		dev_err(dev, "failed to add component master\n");
		/*
		 * The EPOD regulator is already disabled at this point so some
		 * special errorpath code is needed
		 */
		clk_disable_unprepare(mcde->mcde_clk);
		regulator_disable(mcde->vana);
		return ret;
	}

	return 0;

clk_disable:
	clk_disable_unprepare(mcde->mcde_clk);
regulator_off:
	regulator_disable(mcde->vana);
regulator_epod_off:
	regulator_disable(mcde->epod);
	return ret;

}

static int mcde_remove(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);
	struct mcde *mcde = to_mcde(drm);

	component_master_del(&pdev->dev, &mcde_drm_comp_ops);
	clk_disable_unprepare(mcde->mcde_clk);
	regulator_disable(mcde->vana);
	regulator_disable(mcde->epod);

	return 0;
}

static const struct of_device_id mcde_of_match[] = {
	{
		.compatible = "ste,mcde",
	},
	{},
};

static struct platform_driver mcde_driver = {
	.driver = {
		.name           = "mcde",
		.of_match_table = of_match_ptr(mcde_of_match),
	},
	.probe = mcde_probe,
	.remove = mcde_remove,
};

static struct platform_driver *const component_drivers[] = {
	&mcde_dsi_driver,
};

static int __init mcde_drm_register(void)
{
	int ret;

	ret = platform_register_drivers(component_drivers,
					ARRAY_SIZE(component_drivers));
	if (ret)
		return ret;

	return platform_driver_register(&mcde_driver);
}

static void __exit mcde_drm_unregister(void)
{
	platform_unregister_drivers(component_drivers,
				    ARRAY_SIZE(component_drivers));
	platform_driver_unregister(&mcde_driver);
}

module_init(mcde_drm_register);
module_exit(mcde_drm_unregister);

MODULE_ALIAS("platform:mcde-drm");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_LICENSE("GPL");
