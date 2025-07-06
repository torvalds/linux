// SPDX-License-Identifier: GPL-2.0
/*
 * Platform driver for the Synopsys DesignWare DMA Controller
 *
 * Copyright (C) 2007-2008 Atmel Corporation
 * Copyright (C) 2010-2011 ST Microelectronics
 * Copyright (C) 2013 Intel Corporation
 *
 * Some parts of this driver are derived from the original dw_dmac.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/acpi.h>

#include "internal.h"

#define DRV_NAME	"dw_dmac"

static int dw_probe(struct platform_device *pdev)
{
	const struct dw_dma_chip_pdata *match;
	struct dw_dma_chip_pdata *data;
	struct dw_dma_chip *chip;
	struct device *dev = &pdev->dev;
	int ret;

	match = device_get_match_data(dev);
	if (!match)
		return -ENODEV;

	data = devm_kmemdup(&pdev->dev, match, sizeof(*match), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->irq = platform_get_irq(pdev, 0);
	if (chip->irq < 0)
		return chip->irq;

	chip->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(chip->regs))
		return PTR_ERR(chip->regs);

	ret = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	if (!data->pdata)
		data->pdata = dev_get_platdata(dev);
	if (!data->pdata)
		data->pdata = dw_dma_parse_dt(pdev);

	chip->dev = dev;
	chip->id = pdev->id;
	chip->pdata = data->pdata;

	data->chip = chip;

	chip->clk = devm_clk_get_optional(chip->dev, "hclk");
	if (IS_ERR(chip->clk))
		return PTR_ERR(chip->clk);
	ret = clk_prepare_enable(chip->clk);
	if (ret)
		return ret;

	pm_runtime_enable(&pdev->dev);

	ret = data->probe(chip);
	if (ret)
		goto err_dw_dma_probe;

	platform_set_drvdata(pdev, data);

	dw_dma_of_controller_register(chip->dw);

	dw_dma_acpi_controller_register(chip->dw);

	return 0;

err_dw_dma_probe:
	pm_runtime_disable(&pdev->dev);
	clk_disable_unprepare(chip->clk);
	return ret;
}

static void dw_remove(struct platform_device *pdev)
{
	struct dw_dma_chip_pdata *data = platform_get_drvdata(pdev);
	struct dw_dma_chip *chip = data->chip;
	int ret;

	dw_dma_acpi_controller_free(chip->dw);

	dw_dma_of_controller_free(chip->dw);

	ret = data->remove(chip);
	if (ret)
		dev_warn(chip->dev, "can't remove device properly: %d\n", ret);

	pm_runtime_disable(&pdev->dev);
	clk_disable_unprepare(chip->clk);
}

static void dw_shutdown(struct platform_device *pdev)
{
	struct dw_dma_chip_pdata *data = platform_get_drvdata(pdev);
	struct dw_dma_chip *chip = data->chip;

	/*
	 * We have to call do_dw_dma_disable() to stop any ongoing transfer. On
	 * some platforms we can't do that since DMA device is powered off.
	 * Moreover we have no possibility to check if the platform is affected
	 * or not. That's why we call pm_runtime_get_sync() / pm_runtime_put()
	 * unconditionally. On the other hand we can't use
	 * pm_runtime_suspended() because runtime PM framework is not fully
	 * used by the driver.
	 */
	pm_runtime_get_sync(chip->dev);
	do_dw_dma_disable(chip);
	pm_runtime_put_sync_suspend(chip->dev);

	clk_disable_unprepare(chip->clk);
}

#ifdef CONFIG_OF
static const struct of_device_id dw_dma_of_id_table[] = {
	{ .compatible = "snps,dma-spear1340", .data = &dw_dma_chip_pdata },
	{ .compatible = "renesas,rzn1-dma", .data = &dw_dma_chip_pdata },
	{}
};
MODULE_DEVICE_TABLE(of, dw_dma_of_id_table);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id dw_dma_acpi_id_table[] = {
	{ "INTL9C60", (kernel_ulong_t)&dw_dma_chip_pdata },
	{ "80862286", (kernel_ulong_t)&dw_dma_chip_pdata },
	{ "808622C0", (kernel_ulong_t)&dw_dma_chip_pdata },

	/* Elkhart Lake iDMA 32-bit (PSE DMA) */
	{ "80864BB4", (kernel_ulong_t)&xbar_chip_pdata },
	{ "80864BB5", (kernel_ulong_t)&xbar_chip_pdata },
	{ "80864BB6", (kernel_ulong_t)&xbar_chip_pdata },

	{ }
};
MODULE_DEVICE_TABLE(acpi, dw_dma_acpi_id_table);
#endif

static int dw_suspend_late(struct device *dev)
{
	struct dw_dma_chip_pdata *data = dev_get_drvdata(dev);
	struct dw_dma_chip *chip = data->chip;

	do_dw_dma_disable(chip);
	clk_disable_unprepare(chip->clk);

	return 0;
}

static int dw_resume_early(struct device *dev)
{
	struct dw_dma_chip_pdata *data = dev_get_drvdata(dev);
	struct dw_dma_chip *chip = data->chip;
	int ret;

	ret = clk_prepare_enable(chip->clk);
	if (ret)
		return ret;

	return do_dw_dma_enable(chip);
}

static const struct dev_pm_ops dw_dev_pm_ops = {
	LATE_SYSTEM_SLEEP_PM_OPS(dw_suspend_late, dw_resume_early)
};

static struct platform_driver dw_driver = {
	.probe		= dw_probe,
	.remove		= dw_remove,
	.shutdown       = dw_shutdown,
	.driver = {
		.name	= DRV_NAME,
		.pm	= pm_sleep_ptr(&dw_dev_pm_ops),
		.of_match_table = of_match_ptr(dw_dma_of_id_table),
		.acpi_match_table = ACPI_PTR(dw_dma_acpi_id_table),
	},
};

static int __init dw_init(void)
{
	return platform_driver_register(&dw_driver);
}
subsys_initcall(dw_init);

static void __exit dw_exit(void)
{
	platform_driver_unregister(&dw_driver);
}
module_exit(dw_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Synopsys DesignWare DMA Controller platform driver");
MODULE_ALIAS("platform:" DRV_NAME);
