// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2021 Aspeed Technology Inc.
 */
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/dma-mapping.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "aspeed-espi-ioc.h"
#include "aspeed-espi-ctrl.h"
#include "aspeed-espi-perif.h"
#include "aspeed-espi-vw.h"
#include "aspeed-espi-oob.h"
#include "aspeed-espi-flash.h"

#define DEVICE_NAME "aspeed-espi-ctrl"

static irqreturn_t aspeed_espi_ctrl_isr(int irq, void *arg)
{
	uint32_t sts;
	struct aspeed_espi_ctrl *espi_ctrl = (struct aspeed_espi_ctrl *)arg;

	regmap_read(espi_ctrl->map, ESPI_INT_STS, &sts);

	if (sts & ESPI_INT_STS_PERIF_BITS) {
		aspeed_espi_perif_event(sts, espi_ctrl->perif);
		regmap_write(espi_ctrl->map, ESPI_INT_STS, sts & ESPI_INT_STS_PERIF_BITS);
	}

	if (sts & ESPI_INT_STS_VW_BITS) {
		aspeed_espi_vw_event(sts, espi_ctrl->vw);
		regmap_write(espi_ctrl->map, ESPI_INT_STS, sts & ESPI_INT_STS_VW_BITS);
	}

	if (sts & (ESPI_INT_STS_OOB_BITS)) {
		aspeed_espi_oob_event(sts, espi_ctrl->oob);
		regmap_write(espi_ctrl->map, ESPI_INT_STS, sts & ESPI_INT_STS_OOB_BITS);
	}

	if (sts & ESPI_INT_STS_FLASH_BITS) {
		aspeed_espi_flash_event(sts, espi_ctrl->flash);
		regmap_write(espi_ctrl->map, ESPI_INT_STS, sts & ESPI_INT_STS_FLASH_BITS);
	}

	if (sts & ESPI_INT_STS_HW_RST_DEASSERT) {
		aspeed_espi_perif_enable(espi_ctrl->perif);
		aspeed_espi_vw_enable(espi_ctrl->vw);
		aspeed_espi_oob_enable(espi_ctrl->oob);
		aspeed_espi_flash_enable(espi_ctrl->flash);

		regmap_write(espi_ctrl->map, ESPI_SYSEVT_INT_T0, 0x0);
		regmap_write(espi_ctrl->map, ESPI_SYSEVT_INT_T1, 0x0);
		regmap_write(espi_ctrl->map, ESPI_SYSEVT_INT_EN, 0xffffffff);

		regmap_write(espi_ctrl->map, ESPI_SYSEVT1_INT_T0, 0x1);
		regmap_write(espi_ctrl->map, ESPI_SYSEVT1_INT_EN, 0x1);

		if (espi_ctrl->model->version == ESPI_AST2500)
			regmap_write(espi_ctrl->map, ESPI_SYSEVT_INT_T2,
				     ESPI_SYSEVT_INT_T2_HOST_RST_WARN |
				     ESPI_SYSEVT_INT_T2_OOB_RST_WARN);

		regmap_update_bits(espi_ctrl->map, ESPI_INT_EN,
				   ESPI_INT_EN_HW_RST_DEASSERT,
				   ESPI_INT_EN_HW_RST_DEASSERT);

		regmap_update_bits(espi_ctrl->map, ESPI_SYSEVT,
				   ESPI_SYSEVT_SLV_BOOT_STS | ESPI_SYSEVT_SLV_BOOT_DONE,
				   ESPI_SYSEVT_SLV_BOOT_STS | ESPI_SYSEVT_SLV_BOOT_DONE);

		regmap_write(espi_ctrl->map, ESPI_INT_STS, ESPI_INT_STS_HW_RST_DEASSERT);
	}

	return IRQ_HANDLED;
}

static int aspeed_espi_ctrl_probe(struct platform_device *pdev)
{
	int rc = 0;
	uint32_t reg;
	struct aspeed_espi_ctrl *espi_ctrl;
	struct device *dev = &pdev->dev;
	struct regmap *scu;

	espi_ctrl = devm_kzalloc(dev, sizeof(*espi_ctrl), GFP_KERNEL);
	if (!espi_ctrl)
		return -ENOMEM;

	espi_ctrl->model = of_device_get_match_data(dev);

	scu = syscon_regmap_lookup_by_phandle(dev->of_node, "aspeed,scu");
	if (IS_ERR(scu)) {
		dev_err(dev, "cannot to find SCU regmap\n");
		return -ENODEV;
	}

	if (espi_ctrl->model->version == ESPI_AST2500) {
		regmap_read(scu, 0x70, &reg);
		if ((reg & 0x2000000) == 0)
			return -EPERM;
	} else if (espi_ctrl->model->version == ESPI_AST2600) {
		regmap_read(scu, 0x510, &reg);
		if (reg & 0x40)
			return -EPERM;
	} else {
		dev_err(dev, "unknown eSPI version\n");
		return -EINVAL;
	}

	espi_ctrl->map = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(espi_ctrl->map)) {
		dev_err(dev, "cannot get remap\n");
		return -ENODEV;
	}

	espi_ctrl->irq = platform_get_irq(pdev, 0);
	if (espi_ctrl->irq < 0)
		return espi_ctrl->irq;

	espi_ctrl->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(espi_ctrl->clk)) {
		dev_err(dev, "cannot get clock\n");
		return -ENODEV;
	}

	rc = clk_prepare_enable(espi_ctrl->clk);
	if (rc) {
		dev_err(dev, "cannot enable clock\n");
		return rc;
	}

	espi_ctrl->perif = aspeed_espi_perif_alloc(dev, espi_ctrl);
	if (IS_ERR(espi_ctrl->perif)) {
		dev_err(dev, "failed to allocate peripheral channel\n");
		return PTR_ERR(espi_ctrl->perif);
	}

	espi_ctrl->vw = aspeed_espi_vw_alloc(dev, espi_ctrl);
	if (IS_ERR(espi_ctrl->vw)) {
		dev_err(dev, "failed to allocate virtual wire channel\n");
		return PTR_ERR(espi_ctrl->vw);
	}

	espi_ctrl->oob = aspeed_espi_oob_alloc(dev, espi_ctrl);
	if (IS_ERR(espi_ctrl->oob)) {
		dev_err(dev, "failed to allocate out-of-band channel\n");
		return PTR_ERR(espi_ctrl->oob);
	}

	espi_ctrl->flash = aspeed_espi_flash_alloc(dev, espi_ctrl);
	if (rc) {
		dev_err(dev, "failed to allocate flash channel\n");
		return PTR_ERR(espi_ctrl->flash);
	}

	regmap_update_bits(espi_ctrl->map, ESPI_CTRL2, BIT(30), 0);

	regmap_write(espi_ctrl->map, ESPI_SYSEVT_INT_T0, 0x0);
	regmap_write(espi_ctrl->map, ESPI_SYSEVT_INT_T1, 0x0);
	regmap_write(espi_ctrl->map, ESPI_SYSEVT_INT_EN, 0xffffffff);

	regmap_write(espi_ctrl->map, ESPI_SYSEVT1_INT_T0, 0x1);
	regmap_write(espi_ctrl->map, ESPI_SYSEVT1_INT_EN, 0x1);

	rc = devm_request_irq(dev, espi_ctrl->irq,
			      aspeed_espi_ctrl_isr,
			      0, DEVICE_NAME, espi_ctrl);
	if (rc) {
		dev_err(dev, "failed to request IRQ\n");
		return rc;
	}

	regmap_update_bits(espi_ctrl->map, ESPI_INT_EN,
			   ESPI_INT_EN_HW_RST_DEASSERT,
			   ESPI_INT_EN_HW_RST_DEASSERT);

	dev_set_drvdata(dev, espi_ctrl);

	dev_info(dev, "module loaded\n");

	return 0;
}

static int aspeed_espi_ctrl_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct aspeed_espi_ctrl *espi_ctrl = dev_get_drvdata(dev);

	aspeed_espi_perif_free(dev, espi_ctrl->perif);
	aspeed_espi_vw_free(dev, espi_ctrl->vw);
	aspeed_espi_oob_free(dev, espi_ctrl->oob);
	aspeed_espi_flash_free(dev, espi_ctrl->flash);

	return 0;
}

static const struct aspeed_espi_model ast2500_model = {
	.version = ESPI_AST2500,
};

static const struct aspeed_espi_model ast2600_model = {
	.version = ESPI_AST2600,
};

static const struct of_device_id aspeed_espi_ctrl_of_matches[] = {
	{ .compatible = "aspeed,ast2500-espi-ctrl",
	  .data = &ast2500_model },
	{ .compatible = "aspeed,ast2600-espi-ctrl",
	  .data = &ast2600_model },
	{ },
};

static struct platform_driver aspeed_espi_ctrl_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.of_match_table = aspeed_espi_ctrl_of_matches,
	},
	.probe = aspeed_espi_ctrl_probe,
	.remove = aspeed_espi_ctrl_remove,
};

module_platform_driver(aspeed_espi_ctrl_driver);

MODULE_AUTHOR("Chia-Wei Wang <chiawei_wang@aspeedtech.com>");
MODULE_AUTHOR("Ryan Chen <ryan_chen@aspeedtech.com>");
MODULE_DESCRIPTION("Control of Aspeed eSPI Slave Device");
MODULE_LICENSE("GPL v2");
