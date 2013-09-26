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
#include <linux/platform_device.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/acpi.h>
#include <linux/acpi_dma.h>

#include "internal.h"

struct dw_dma_of_filter_args {
	struct dw_dma *dw;
	unsigned int req;
	unsigned int src;
	unsigned int dst;
};

static bool dw_dma_of_filter(struct dma_chan *chan, void *param)
{
	struct dw_dma_chan *dwc = to_dw_dma_chan(chan);
	struct dw_dma_of_filter_args *fargs = param;

	/* Ensure the device matches our channel */
	if (chan->device != &fargs->dw->dma)
		return false;

	dwc->request_line = fargs->req;
	dwc->src_master	= fargs->src;
	dwc->dst_master	= fargs->dst;

	return true;
}

static struct dma_chan *dw_dma_of_xlate(struct of_phandle_args *dma_spec,
					struct of_dma *ofdma)
{
	struct dw_dma *dw = ofdma->of_dma_data;
	struct dw_dma_of_filter_args fargs = {
		.dw = dw,
	};
	dma_cap_mask_t cap;

	if (dma_spec->args_count != 3)
		return NULL;

	fargs.req = dma_spec->args[0];
	fargs.src = dma_spec->args[1];
	fargs.dst = dma_spec->args[2];

	if (WARN_ON(fargs.req >= DW_DMA_MAX_NR_REQUESTS ||
		    fargs.src >= dw->nr_masters ||
		    fargs.dst >= dw->nr_masters))
		return NULL;

	dma_cap_zero(cap);
	dma_cap_set(DMA_SLAVE, cap);

	/* TODO: there should be a simpler way to do this */
	return dma_request_channel(cap, dw_dma_of_filter, &fargs);
}

#ifdef CONFIG_ACPI
static bool dw_dma_acpi_filter(struct dma_chan *chan, void *param)
{
	struct dw_dma_chan *dwc = to_dw_dma_chan(chan);
	struct acpi_dma_spec *dma_spec = param;

	if (chan->device->dev != dma_spec->dev ||
	    chan->chan_id != dma_spec->chan_id)
		return false;

	dwc->request_line = dma_spec->slave_id;
	dwc->src_master = dwc_get_sms(NULL);
	dwc->dst_master = dwc_get_dms(NULL);

	return true;
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
	u32 tmp, arr[4];

	if (!np) {
		dev_err(&pdev->dev, "Missing DT data\n");
		return NULL;
	}

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	if (of_property_read_u32(np, "dma-channels", &pdata->nr_channels))
		return NULL;

	if (of_property_read_bool(np, "is_private"))
		pdata->is_private = true;

	if (!of_property_read_u32(np, "chan_allocation_order", &tmp))
		pdata->chan_allocation_order = (unsigned char)tmp;

	if (!of_property_read_u32(np, "chan_priority", &tmp))
		pdata->chan_priority = tmp;

	if (!of_property_read_u32(np, "block_size", &tmp))
		pdata->block_size = tmp;

	if (!of_property_read_u32(np, "dma-masters", &tmp)) {
		if (tmp > 4)
			return NULL;

		pdata->nr_masters = tmp;
	}

	if (!of_property_read_u32_array(np, "data_width", arr,
				pdata->nr_masters))
		for (tmp = 0; tmp < pdata->nr_masters; tmp++)
			pdata->data_width[tmp] = arr[tmp];

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
	struct dw_dma_platform_data *pdata;
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

	/* Apply default dma_mask if needed */
	if (!dev->dma_mask) {
		dev->dma_mask = &dev->coherent_dma_mask;
		dev->coherent_dma_mask = DMA_BIT_MASK(32);
	}

	pdata = dev_get_platdata(dev);
	if (!pdata)
		pdata = dw_dma_parse_dt(pdev);

	chip->dev = dev;

	err = dw_dma_probe(chip, pdata);
	if (err)
		return err;

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
}

static int dw_remove(struct platform_device *pdev)
{
	struct dw_dma_chip *chip = platform_get_drvdata(pdev);

	if (pdev->dev.of_node)
		of_dma_controller_free(pdev->dev.of_node);

	return dw_dma_remove(chip);
}

static void dw_shutdown(struct platform_device *pdev)
{
	struct dw_dma_chip *chip = platform_get_drvdata(pdev);

	dw_dma_shutdown(chip);
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

static int dw_suspend_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dw_dma_chip *chip = platform_get_drvdata(pdev);

	return dw_dma_suspend(chip);
}

static int dw_resume_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dw_dma_chip *chip = platform_get_drvdata(pdev);

	return dw_dma_resume(chip);
}

#else /* !CONFIG_PM_SLEEP */

#define dw_suspend_noirq	NULL
#define dw_resume_noirq		NULL

#endif /* !CONFIG_PM_SLEEP */

static const struct dev_pm_ops dw_dev_pm_ops = {
	.suspend_noirq = dw_suspend_noirq,
	.resume_noirq = dw_resume_noirq,
	.freeze_noirq = dw_suspend_noirq,
	.thaw_noirq = dw_resume_noirq,
	.restore_noirq = dw_resume_noirq,
	.poweroff_noirq = dw_suspend_noirq,
};

static struct platform_driver dw_driver = {
	.probe		= dw_probe,
	.remove		= dw_remove,
	.shutdown	= dw_shutdown,
	.driver = {
		.name	= "dw_dmac",
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
