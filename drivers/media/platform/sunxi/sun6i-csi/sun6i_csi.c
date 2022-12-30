// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2011-2018 Magewell Electronics Co., Ltd. (Nanjing)
 * Author: Yong Deng <yong.deng@magewell.com>
 * Copyright 2021-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mc.h>

#include "sun6i_csi.h"
#include "sun6i_csi_bridge.h"
#include "sun6i_csi_capture.h"
#include "sun6i_csi_reg.h"

/* ISP */

int sun6i_csi_isp_complete(struct sun6i_csi_device *csi_dev,
			   struct v4l2_device *v4l2_dev)
{
	if (csi_dev->v4l2_dev && csi_dev->v4l2_dev != v4l2_dev)
		return -EINVAL;

	csi_dev->v4l2_dev = v4l2_dev;
	csi_dev->media_dev = v4l2_dev->mdev;

	return sun6i_csi_capture_setup(csi_dev);
}

static int sun6i_csi_isp_detect(struct sun6i_csi_device *csi_dev)
{
	struct device *dev = csi_dev->dev;
	struct fwnode_handle *handle;

	/*
	 * ISP is not available if not connected via fwnode graph.
	 * This will also check that the remote parent node is available.
	 */
	handle = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev),
						 SUN6I_CSI_PORT_ISP, 0,
						 FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!handle)
		return 0;

	fwnode_handle_put(handle);

	if (!IS_ENABLED(CONFIG_VIDEO_SUN6I_ISP)) {
		dev_warn(dev,
			 "ISP link is detected but not enabled in kernel config!");
		return 0;
	}

	csi_dev->isp_available = true;

	return 0;
}

/* Media */

static const struct media_device_ops sun6i_csi_media_ops = {
	.link_notify = v4l2_pipeline_link_notify,
};

/* V4L2 */

static int sun6i_csi_v4l2_setup(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_v4l2 *v4l2 = &csi_dev->v4l2;
	struct media_device *media_dev = &v4l2->media_dev;
	struct v4l2_device *v4l2_dev = &v4l2->v4l2_dev;
	struct device *dev = csi_dev->dev;
	int ret;

	/* Media Device */

	strscpy(media_dev->model, SUN6I_CSI_DESCRIPTION,
		sizeof(media_dev->model));
	media_dev->hw_revision = 0;
	media_dev->ops = &sun6i_csi_media_ops;
	media_dev->dev = dev;

	media_device_init(media_dev);

	ret = media_device_register(media_dev);
	if (ret) {
		dev_err(dev, "failed to register media device: %d\n", ret);
		goto error_media;
	}

	/* V4L2 Device */

	v4l2_dev->mdev = media_dev;

	ret = v4l2_device_register(dev, v4l2_dev);
	if (ret) {
		dev_err(dev, "failed to register v4l2 device: %d\n", ret);
		goto error_media;
	}

	csi_dev->v4l2_dev = v4l2_dev;
	csi_dev->media_dev = media_dev;

	return 0;

error_media:
	media_device_unregister(media_dev);
	media_device_cleanup(media_dev);

	return ret;
}

static void sun6i_csi_v4l2_cleanup(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_v4l2 *v4l2 = &csi_dev->v4l2;

	media_device_unregister(&v4l2->media_dev);
	v4l2_device_unregister(&v4l2->v4l2_dev);
	media_device_cleanup(&v4l2->media_dev);
}

/* Platform */

static irqreturn_t sun6i_csi_interrupt(int irq, void *private)
{
	struct sun6i_csi_device *csi_dev = private;
	bool capture_streaming = csi_dev->capture.state.streaming;
	struct regmap *regmap = csi_dev->regmap;
	u32 status = 0, enable = 0;

	regmap_read(regmap, SUN6I_CSI_CH_INT_STA_REG, &status);
	regmap_read(regmap, SUN6I_CSI_CH_INT_EN_REG, &enable);

	if (!status)
		return IRQ_NONE;
	else if (!(status & enable) || !capture_streaming)
		goto complete;

	if ((status & SUN6I_CSI_CH_INT_STA_FIFO0_OF) ||
	    (status & SUN6I_CSI_CH_INT_STA_FIFO1_OF) ||
	    (status & SUN6I_CSI_CH_INT_STA_FIFO2_OF) ||
	    (status & SUN6I_CSI_CH_INT_STA_HB_OF)) {
		regmap_write(regmap, SUN6I_CSI_CH_INT_STA_REG, status);

		regmap_update_bits(regmap, SUN6I_CSI_EN_REG,
				   SUN6I_CSI_EN_CSI_EN, 0);
		regmap_update_bits(regmap, SUN6I_CSI_EN_REG,
				   SUN6I_CSI_EN_CSI_EN, SUN6I_CSI_EN_CSI_EN);
		return IRQ_HANDLED;
	}

	if (status & SUN6I_CSI_CH_INT_STA_FD)
		sun6i_csi_capture_frame_done(csi_dev);

	if (status & SUN6I_CSI_CH_INT_STA_VS)
		sun6i_csi_capture_sync(csi_dev);

complete:
	regmap_write(regmap, SUN6I_CSI_CH_INT_STA_REG, status);

	return IRQ_HANDLED;
}

static int sun6i_csi_suspend(struct device *dev)
{
	struct sun6i_csi_device *csi_dev = dev_get_drvdata(dev);

	reset_control_assert(csi_dev->reset);
	clk_disable_unprepare(csi_dev->clock_ram);
	clk_disable_unprepare(csi_dev->clock_mod);

	return 0;
}

static int sun6i_csi_resume(struct device *dev)
{
	struct sun6i_csi_device *csi_dev = dev_get_drvdata(dev);
	int ret;

	ret = reset_control_deassert(csi_dev->reset);
	if (ret) {
		dev_err(dev, "failed to deassert reset\n");
		return ret;
	}

	ret = clk_prepare_enable(csi_dev->clock_mod);
	if (ret) {
		dev_err(dev, "failed to enable module clock\n");
		goto error_reset;
	}

	ret = clk_prepare_enable(csi_dev->clock_ram);
	if (ret) {
		dev_err(dev, "failed to enable ram clock\n");
		goto error_clock_mod;
	}

	return 0;

error_clock_mod:
	clk_disable_unprepare(csi_dev->clock_mod);

error_reset:
	reset_control_assert(csi_dev->reset);

	return ret;
}

static const struct dev_pm_ops sun6i_csi_pm_ops = {
	.runtime_suspend	= sun6i_csi_suspend,
	.runtime_resume		= sun6i_csi_resume,
};

static const struct regmap_config sun6i_csi_regmap_config = {
	.reg_bits       = 32,
	.reg_stride     = 4,
	.val_bits       = 32,
	.max_register	= 0x9c,
};

static int sun6i_csi_resources_setup(struct sun6i_csi_device *csi_dev,
				     struct platform_device *platform_dev)
{
	struct device *dev = csi_dev->dev;
	const struct sun6i_csi_variant *variant;
	void __iomem *io_base;
	int ret;
	int irq;

	variant = of_device_get_match_data(dev);
	if (!variant)
		return -EINVAL;

	/* Registers */

	io_base = devm_platform_ioremap_resource(platform_dev, 0);
	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

	csi_dev->regmap = devm_regmap_init_mmio_clk(dev, "bus", io_base,
						    &sun6i_csi_regmap_config);
	if (IS_ERR(csi_dev->regmap)) {
		dev_err(dev, "failed to init register map\n");
		return PTR_ERR(csi_dev->regmap);
	}

	/* Clocks */

	csi_dev->clock_mod = devm_clk_get(dev, "mod");
	if (IS_ERR(csi_dev->clock_mod)) {
		dev_err(dev, "failed to acquire module clock\n");
		return PTR_ERR(csi_dev->clock_mod);
	}

	csi_dev->clock_ram = devm_clk_get(dev, "ram");
	if (IS_ERR(csi_dev->clock_ram)) {
		dev_err(dev, "failed to acquire ram clock\n");
		return PTR_ERR(csi_dev->clock_ram);
	}

	ret = clk_set_rate_exclusive(csi_dev->clock_mod,
				     variant->clock_mod_rate);
	if (ret) {
		dev_err(dev, "failed to set mod clock rate\n");
		return ret;
	}

	/* Reset */

	csi_dev->reset = devm_reset_control_get_shared(dev, NULL);
	if (IS_ERR(csi_dev->reset)) {
		dev_err(dev, "failed to acquire reset\n");
		ret = PTR_ERR(csi_dev->reset);
		goto error_clock_rate_exclusive;
	}

	/* Interrupt */

	irq = platform_get_irq(platform_dev, 0);
	if (irq < 0) {
		ret = -ENXIO;
		goto error_clock_rate_exclusive;
	}

	ret = devm_request_irq(dev, irq, sun6i_csi_interrupt, IRQF_SHARED,
			       SUN6I_CSI_NAME, csi_dev);
	if (ret) {
		dev_err(dev, "failed to request interrupt\n");
		goto error_clock_rate_exclusive;
	}

	/* Runtime PM */

	pm_runtime_enable(dev);

	return 0;

error_clock_rate_exclusive:
	clk_rate_exclusive_put(csi_dev->clock_mod);

	return ret;
}

static void sun6i_csi_resources_cleanup(struct sun6i_csi_device *csi_dev)
{
	pm_runtime_disable(csi_dev->dev);
	clk_rate_exclusive_put(csi_dev->clock_mod);
}

static int sun6i_csi_probe(struct platform_device *platform_dev)
{
	struct sun6i_csi_device *csi_dev;
	struct device *dev = &platform_dev->dev;
	int ret;

	csi_dev = devm_kzalloc(dev, sizeof(*csi_dev), GFP_KERNEL);
	if (!csi_dev)
		return -ENOMEM;

	csi_dev->dev = &platform_dev->dev;
	platform_set_drvdata(platform_dev, csi_dev);

	ret = sun6i_csi_resources_setup(csi_dev, platform_dev);
	if (ret)
		return ret;

	ret = sun6i_csi_isp_detect(csi_dev);
	if (ret)
		goto error_resources;

	/*
	 * Register our own v4l2 and media devices when there is no ISP around.
	 * Otherwise the ISP will use async subdev registration with our bridge,
	 * which will provide v4l2 and media devices that are used to register
	 * the video interface.
	 */
	if (!csi_dev->isp_available) {
		ret = sun6i_csi_v4l2_setup(csi_dev);
		if (ret)
			goto error_resources;
	}

	ret = sun6i_csi_bridge_setup(csi_dev);
	if (ret)
		goto error_v4l2;

	if (!csi_dev->isp_available) {
		ret = sun6i_csi_capture_setup(csi_dev);
		if (ret)
			goto error_bridge;
	}

	return 0;

error_bridge:
	sun6i_csi_bridge_cleanup(csi_dev);

error_v4l2:
	if (!csi_dev->isp_available)
		sun6i_csi_v4l2_cleanup(csi_dev);

error_resources:
	sun6i_csi_resources_cleanup(csi_dev);

	return ret;
}

static int sun6i_csi_remove(struct platform_device *pdev)
{
	struct sun6i_csi_device *csi_dev = platform_get_drvdata(pdev);

	sun6i_csi_capture_cleanup(csi_dev);
	sun6i_csi_bridge_cleanup(csi_dev);

	if (!csi_dev->isp_available)
		sun6i_csi_v4l2_cleanup(csi_dev);

	sun6i_csi_resources_cleanup(csi_dev);

	return 0;
}

static const struct sun6i_csi_variant sun6i_a31_csi_variant = {
	.clock_mod_rate	= 297000000,
};

static const struct sun6i_csi_variant sun50i_a64_csi_variant = {
	.clock_mod_rate	= 300000000,
};

static const struct of_device_id sun6i_csi_of_match[] = {
	{
		.compatible	= "allwinner,sun6i-a31-csi",
		.data		= &sun6i_a31_csi_variant,
	},
	{
		.compatible	= "allwinner,sun8i-a83t-csi",
		.data		= &sun6i_a31_csi_variant,
	},
	{
		.compatible	= "allwinner,sun8i-h3-csi",
		.data		= &sun6i_a31_csi_variant,
	},
	{
		.compatible	= "allwinner,sun8i-v3s-csi",
		.data		= &sun6i_a31_csi_variant,
	},
	{
		.compatible	= "allwinner,sun50i-a64-csi",
		.data		= &sun50i_a64_csi_variant,
	},
	{},
};

MODULE_DEVICE_TABLE(of, sun6i_csi_of_match);

static struct platform_driver sun6i_csi_platform_driver = {
	.probe	= sun6i_csi_probe,
	.remove	= sun6i_csi_remove,
	.driver	= {
		.name		= SUN6I_CSI_NAME,
		.of_match_table	= of_match_ptr(sun6i_csi_of_match),
		.pm		= &sun6i_csi_pm_ops,
	},
};

module_platform_driver(sun6i_csi_platform_driver);

MODULE_DESCRIPTION("Allwinner A31 Camera Sensor Interface driver");
MODULE_AUTHOR("Yong Deng <yong.deng@magewell.com>");
MODULE_AUTHOR("Paul Kocialkowski <paul.kocialkowski@bootlin.com>");
MODULE_LICENSE("GPL");
