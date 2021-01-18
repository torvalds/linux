// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip Image Sensor Controller (ISC) driver
 *
 * Copyright (C) 2016-2019 Microchip Technology, Inc.
 *
 * Author: Songjun Wu
 * Author: Eugen Hristev <eugen.hristev@microchip.com>
 *
 *
 * Sensor-->PFE-->WB-->CFA-->CC-->GAM-->CSC-->CBC-->SUB-->RLP-->DMA
 *
 * ISC video pipeline integrates the following submodules:
 * PFE: Parallel Front End to sample the camera sensor input stream
 *  WB: Programmable white balance in the Bayer domain
 * CFA: Color filter array interpolation module
 *  CC: Programmable color correction
 * GAM: Gamma correction
 * CSC: Programmable color space conversion
 * CBC: Contrast and Brightness control
 * SUB: This module performs YCbCr444 to YCbCr420 chrominance subsampling
 * RLP: This module performs rounding, range limiting
 *      and packing of the incoming data
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/videodev2.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>

#include "atmel-isc-regs.h"
#include "atmel-isc.h"

#define ISC_MAX_SUPPORT_WIDTH   2592
#define ISC_MAX_SUPPORT_HEIGHT  1944

#define ISC_CLK_MAX_DIV		255

static int isc_parse_dt(struct device *dev, struct isc_device *isc)
{
	struct device_node *np = dev->of_node;
	struct device_node *epn = NULL;
	struct isc_subdev_entity *subdev_entity;
	unsigned int flags;
	int ret;

	INIT_LIST_HEAD(&isc->subdev_entities);

	while (1) {
		struct v4l2_fwnode_endpoint v4l2_epn = { .bus_type = 0 };

		epn = of_graph_get_next_endpoint(np, epn);
		if (!epn)
			return 0;

		ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(epn),
						 &v4l2_epn);
		if (ret) {
			ret = -EINVAL;
			dev_err(dev, "Could not parse the endpoint\n");
			break;
		}

		subdev_entity = devm_kzalloc(dev, sizeof(*subdev_entity),
					     GFP_KERNEL);
		if (!subdev_entity) {
			ret = -ENOMEM;
			break;
		}
		subdev_entity->epn = epn;

		flags = v4l2_epn.bus.parallel.flags;

		if (flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
			subdev_entity->pfe_cfg0 = ISC_PFE_CFG0_HPOL_LOW;

		if (flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
			subdev_entity->pfe_cfg0 |= ISC_PFE_CFG0_VPOL_LOW;

		if (flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)
			subdev_entity->pfe_cfg0 |= ISC_PFE_CFG0_PPOL_LOW;

		if (v4l2_epn.bus_type == V4L2_MBUS_BT656)
			subdev_entity->pfe_cfg0 |= ISC_PFE_CFG0_CCIR_CRC |
					ISC_PFE_CFG0_CCIR656;

		list_add_tail(&subdev_entity->list, &isc->subdev_entities);
	}
	of_node_put(epn);

	return ret;
}

static int atmel_isc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct isc_device *isc;
	struct resource *res;
	void __iomem *io_base;
	struct isc_subdev_entity *subdev_entity;
	int irq;
	int ret;

	isc = devm_kzalloc(dev, sizeof(*isc), GFP_KERNEL);
	if (!isc)
		return -ENOMEM;

	platform_set_drvdata(pdev, isc);
	isc->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	io_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

	isc->regmap = devm_regmap_init_mmio(dev, io_base, &isc_regmap_config);
	if (IS_ERR(isc->regmap)) {
		ret = PTR_ERR(isc->regmap);
		dev_err(dev, "failed to init register map: %d\n", ret);
		return ret;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, isc_interrupt, 0,
			       ATMEL_ISC_NAME, isc);
	if (ret < 0) {
		dev_err(dev, "can't register ISR for IRQ %u (ret=%i)\n",
			irq, ret);
		return ret;
	}

	ret = isc_pipeline_init(isc);
	if (ret)
		return ret;

	isc->hclock = devm_clk_get(dev, "hclock");
	if (IS_ERR(isc->hclock)) {
		ret = PTR_ERR(isc->hclock);
		dev_err(dev, "failed to get hclock: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(isc->hclock);
	if (ret) {
		dev_err(dev, "failed to enable hclock: %d\n", ret);
		return ret;
	}

	ret = isc_clk_init(isc);
	if (ret) {
		dev_err(dev, "failed to init isc clock: %d\n", ret);
		goto unprepare_hclk;
	}

	isc->ispck = isc->isc_clks[ISC_ISPCK].clk;

	ret = clk_prepare_enable(isc->ispck);
	if (ret) {
		dev_err(dev, "failed to enable ispck: %d\n", ret);
		goto unprepare_hclk;
	}

	/* ispck should be greater or equal to hclock */
	ret = clk_set_rate(isc->ispck, clk_get_rate(isc->hclock));
	if (ret) {
		dev_err(dev, "failed to set ispck rate: %d\n", ret);
		goto unprepare_clk;
	}

	ret = v4l2_device_register(dev, &isc->v4l2_dev);
	if (ret) {
		dev_err(dev, "unable to register v4l2 device.\n");
		goto unprepare_clk;
	}

	ret = isc_parse_dt(dev, isc);
	if (ret) {
		dev_err(dev, "fail to parse device tree\n");
		goto unregister_v4l2_device;
	}

	if (list_empty(&isc->subdev_entities)) {
		dev_err(dev, "no subdev found\n");
		ret = -ENODEV;
		goto unregister_v4l2_device;
	}

	list_for_each_entry(subdev_entity, &isc->subdev_entities, list) {
		struct v4l2_async_subdev *asd;

		v4l2_async_notifier_init(&subdev_entity->notifier);

		asd = v4l2_async_notifier_add_fwnode_remote_subdev(
					&subdev_entity->notifier,
					of_fwnode_handle(subdev_entity->epn),
					struct v4l2_async_subdev);

		of_node_put(subdev_entity->epn);
		subdev_entity->epn = NULL;

		if (IS_ERR(asd)) {
			ret = PTR_ERR(asd);
			goto cleanup_subdev;
		}

		subdev_entity->notifier.ops = &isc_async_ops;

		ret = v4l2_async_notifier_register(&isc->v4l2_dev,
						   &subdev_entity->notifier);
		if (ret) {
			dev_err(dev, "fail to register async notifier\n");
			goto cleanup_subdev;
		}

		if (video_is_registered(&isc->video_dev))
			break;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_request_idle(dev);

	return 0;

cleanup_subdev:
	isc_subdev_cleanup(isc);

unregister_v4l2_device:
	v4l2_device_unregister(&isc->v4l2_dev);

unprepare_clk:
	clk_disable_unprepare(isc->ispck);
unprepare_hclk:
	clk_disable_unprepare(isc->hclock);

	isc_clk_cleanup(isc);

	return ret;
}

static int atmel_isc_remove(struct platform_device *pdev)
{
	struct isc_device *isc = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	isc_subdev_cleanup(isc);

	v4l2_device_unregister(&isc->v4l2_dev);

	clk_disable_unprepare(isc->ispck);
	clk_disable_unprepare(isc->hclock);

	isc_clk_cleanup(isc);

	return 0;
}

static int __maybe_unused isc_runtime_suspend(struct device *dev)
{
	struct isc_device *isc = dev_get_drvdata(dev);

	clk_disable_unprepare(isc->ispck);
	clk_disable_unprepare(isc->hclock);

	return 0;
}

static int __maybe_unused isc_runtime_resume(struct device *dev)
{
	struct isc_device *isc = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(isc->hclock);
	if (ret)
		return ret;

	ret = clk_prepare_enable(isc->ispck);
	if (ret)
		clk_disable_unprepare(isc->hclock);

	return ret;
}

static const struct dev_pm_ops atmel_isc_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(isc_runtime_suspend, isc_runtime_resume, NULL)
};

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id atmel_isc_of_match[] = {
	{ .compatible = "atmel,sama5d2-isc" },
	{ }
};
MODULE_DEVICE_TABLE(of, atmel_isc_of_match);
#endif

static struct platform_driver atmel_isc_driver = {
	.probe	= atmel_isc_probe,
	.remove	= atmel_isc_remove,
	.driver	= {
		.name		= ATMEL_ISC_NAME,
		.pm		= &atmel_isc_dev_pm_ops,
		.of_match_table = of_match_ptr(atmel_isc_of_match),
	},
};

module_platform_driver(atmel_isc_driver);

MODULE_AUTHOR("Songjun Wu");
MODULE_DESCRIPTION("The V4L2 driver for Atmel-ISC");
MODULE_LICENSE("GPL v2");
MODULE_SUPPORTED_DEVICE("video");
