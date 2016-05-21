/*
 * Platform driver for the Synopsys DesignWare DMA Controller
 *
 * Copyright (C) 2007-2008 Atmel Corporation
 * Copyright (C) 2010-2011 ST Microelectronics
 * Copyright (C) 2013 Intel Corporation
 *
 * Some parts of this driver are derived from the original dw_dmac.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/acpi.h>
#include <linux/acpi_dma.h>

#include "internal.h"

#define DRV_NAME	"dw_dmac"

static struct dma_chan *dw_dma_of_xlate(struct of_phandle_args *dma_spec,
					struct of_dma *ofdma)
{
	struct dw_dma *dw = ofdma->of_dma_data;
	struct dw_dma_slave slave = {
		.dma_dev = dw->dma.dev,
	};
	dma_cap_mask_t cap;

	if (dma_spec->args_count != 3)
		return NULL;

	slave.src_id = dma_spec->args[0];
	slave.dst_id = dma_spec->args[0];
	slave.m_master = dma_spec->args[1];
	slave.p_master = dma_spec->args[2];

	if (WARN_ON(slave.src_id >= DW_DMA_MAX_NR_REQUESTS ||
		    slave.dst_id >= DW_DMA_MAX_NR_REQUESTS ||
		    slave.m_master >= dw->pdata->nr_masters ||
		    slave.p_master >= dw->pdata->nr_masters))
		return NULL;

	dma_cap_zero(cap);
	dma_cap_set(DMA_SLAVE, cap);

	/* TODO: there should be a simpler way to do this */
	return dma_request_channel(cap, dw_dma_filter, &slave);
}

#ifdef CONFIG_ACPI
static bool dw_dma_acpi_filter(struct dma_chan *chan, void *param)
{
	struct acpi_dma_spec *dma_spec = param;
	struct dw_dma_slave slave = {
		.dma_dev = dma_spec->dev,
		.src_id = dma_spec->slave_id,
		.dst_id = dma_spec->slave_id,
		.m_master = 0,
		.p_master = 1,
	};

	return dw_dma_filter(chan, &slave);
}

static void dw_dma_acpi_controller_register(struct dw_dma *dw)
{
	struct device *dev = dw->dma.dev;
	struct acpi_dma_filter_info *info;
	int ret;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return;

	dma_cap_zero(info->dma_cap);
	dma_cap_set(DMA_SLAVE, info->dma_cap);
	info->filter_fn = dw_dma_acpi_filter;

	ret = devm_acpi_dma_controller_register(dev, acpi_dma_simple_xlate,
						info);
	if (ret)
		dev_err(dev, "could not register acpi_dma_controller\n");
}
#else /* !CONFIG_ACPI */
static inline void dw_dma_acpi_controller_register(struct dw_dma *dw) {}
#endif /* !CONFIG_ACPI */

#ifdef CONFIG_OF
static struct dw_dma_platform_data *
dw_dma_parse_dt(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct dw_dma_platform_data *pdata;
	u32 tmp, arr[DW_DMA_MAX_NR_MASTERS];
	u32 nr_masters;
	u32 nr_channels;

	if (!np) {
		dev_err(&pdev->dev, "Missing DT data\n");
		return NULL;
	}

	if (of_property_read_u32(np, "dma-masters", &nr_masters))
		return NULL;
	if (nr_masters < 1 || nr_masters > DW_DMA_MAX_NR_MASTERS)
		return NULL;

	if (of_property_read_u32(np, "dma-channels", &nr_channels))
		return NULL;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	pdata->nr_masters = nr_masters;
	pdata->nr_channels = nr_channels;

	if (of_property_read_bool(np, "is_private"))
		pdata->is_private = true;

	if (!of_property_read_u32(np, "chan_allocation_order", &tmp))
		pdata->chan_allocation_order = (unsigned char)tmp;

	if (!of_property_read_u32(np, "chan_priority", &tmp))
		pdata->chan_priority = tmp;

	if (!of_property_read_u32(np, "block_size", &tmp))
		pdata->block_size = tmp;

	if (!of_property_read_u32_array(np, "data-width", arr, nr_masters)) {
		for (tmp = 0; tmp < nr_masters; tmp++)
			pdata->data_width[tmp] = arr[tmp];
	} else if (!of_property_read_u32_array(np, "data_width", arr, nr_masters)) {
		for (tmp = 0; tmp < nr_masters; tmp++)
			pdata->data_width[tmp] = BIT(arr[tmp] & 0x07);
	}

	return pdata;
}
#else
static inline struct dw_dma_platform_data *
dw_dma_parse_dt(struct platform_device *pdev)
{
	return NULL;
}
#endif

static int dw_probe(struct platform_device *pdev)
{
	struct dw_dma_chip *chip;
	struct device *dev = &pdev->dev;
	struct resource *mem;
	const struct dw_dma_platform_data *pdata;
	int err;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->irq = platform_get_irq(pdev, 0);
	if (chip->irq < 0)
		return chip->irq;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	chip->regs = devm_ioremap_resource(dev, mem);
	if (IS_ERR(chip->regs))
		return PTR_ERR(chip->regs);

	err = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err)
		return err;

	pdata = dev_get_platdata(dev);
	if (!pdata)
		pdata = dw_dma_parse_dt(pdev);

	chip->dev = dev;
	chip->pdata = pdata;

	chip->clk = devm_clk_get(chip->dev, "hclk");
	if (IS_ERR(chip->clk))
		return PTR_ERR(chip->clk);
	err = clk_prepare_enable(chip->clk);
	if (err)
		return err;

	pm_runtime_enable(&pdev->dev);

	err = dw_dma_probe(chip);
	if (err)
		goto err_dw_dma_probe;

	platform_set_drvdata(pdev, chip);

	if (pdev->dev.of_node) {
		err = of_dma_controller_register(pdev->dev.of_node,
						 dw_dma_of_xlate, chip->dw);
		if (err)
			dev_err(&pdev->dev,
				"could not register of_dma_controller\n");
	}

	if (ACPI_HANDLE(&pdev->dev))
		dw_dma_acpi_controller_register(chip->dw);

	return 0;

err_dw_dma_probe:
	pm_runtime_disable(&pdev->dev);
	clk_disable_unprepare(chip->clk);
	return err;
}

static int dw_remove(struct platform_device *pdev)
{
	struct dw_dma_chip *chip = platform_get_drvdata(pdev);

	if (pdev->dev.of_node)
		of_dma_controller_free(pdev->dev.of_node);

	dw_dma_remove(chip);
	pm_runtime_disable(&pdev->dev);
	clk_disable_unprepare(chip->clk);

	return 0;
}

static void dw_shutdown(struct platform_device *pdev)
{
	struct dw_dma_chip *chip = platform_get_drvdata(pdev);

	/*
	 * We have to call dw_dma_disable() to stop any ongoing transfer. On
	 * some platforms we can't do that since DMA device is powered off.
	 * Moreover we have no possibility to check if the platform is affected
	 * or not. That's why we call pm_runtime_get_sync() / pm_runtime_put()
	 * unconditionally. On the other hand we can't use
	 * pm_runtime_suspended() because runtime PM framework is not fully
	 * used by the driver.
	 */
	pm_runtime_get_sync(chip->dev);
	dw_dma_disable(chip);
	pm_runtime_put_sync_suspend(chip->dev);

	clk_disable_unprepare(chip->clk);
}

#ifdef CONFIG_OF
static const struct of_device_id dw_dma_of_id_table[] = {
	{ .compatible = "snps,dma-spear1340" },
	{}
};
MODULE_DEVICE_TABLE(of, dw_dma_of_id_table);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id dw_dma_acpi_id_table[] = {
	{ "INTL9C60", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, dw_dma_acpi_id_table);
#endif

#ifdef CONFIG_PM_SLEEP

static int dw_suspend_late(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dw_dma_chip *chip = platform_get_drvdata(pdev);

	dw_dma_disable(chip);
	clk_disable_unprepare(chip->clk);

	return 0;
}

static int dw_resume_early(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dw_dma_chip *chip = platform_get_drvdata(pdev);

	clk_prepare_enable(chip->clk);
	return dw_dma_enable(chip);
}

#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops dw_dev_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(dw_suspend_late, dw_resume_early)
};

static struct platform_driver dw_driver = {
	.probe		= dw_probe,
	.remove		= dw_remove,
	.shutdown       = dw_shutdown,
	.driver = {
		.name	= DRV_NAME,
		.pm	= &dw_dev_pm_ops,
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
