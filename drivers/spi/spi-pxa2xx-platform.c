// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/types.h>

#include "spi-pxa2xx.h"

static bool pxa2xx_spi_idma_filter(struct dma_chan *chan, void *param)
{
	return param == chan->device->dev;
}

static int
pxa2xx_spi_init_ssp(struct platform_device *pdev, struct ssp_device *ssp, enum pxa_ssp_type type)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int status;
	u64 uid;

	ssp->mmio_base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(ssp->mmio_base))
		return PTR_ERR(ssp->mmio_base);

	ssp->phys_base = res->start;

	ssp->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ssp->clk))
		return PTR_ERR(ssp->clk);

	ssp->irq = platform_get_irq(pdev, 0);
	if (ssp->irq < 0)
		return ssp->irq;

	ssp->type = type;
	ssp->dev = dev;

	status = acpi_dev_uid_to_integer(ACPI_COMPANION(dev), &uid);
	if (status)
		ssp->port_id = -1;
	else
		ssp->port_id = uid;

	return 0;
}

static void pxa2xx_spi_ssp_release(void *ssp)
{
	pxa_ssp_free(ssp);
}

static struct ssp_device *pxa2xx_spi_ssp_request(struct platform_device *pdev)
{
	struct ssp_device *ssp;
	int status;

	ssp = pxa_ssp_request(pdev->id, pdev->name);
	if (!ssp)
		return NULL;

	status = devm_add_action_or_reset(&pdev->dev, pxa2xx_spi_ssp_release, ssp);
	if (status)
		return ERR_PTR(status);

	return ssp;
}

static struct pxa2xx_spi_controller *
pxa2xx_spi_init_pdata(struct platform_device *pdev)
{
	struct pxa2xx_spi_controller *pdata;
	struct device *dev = &pdev->dev;
	struct device *parent = dev->parent;
	const void *match = device_get_match_data(dev);
	enum pxa_ssp_type type = SSP_UNDEFINED;
	struct ssp_device *ssp;
	bool is_lpss_priv;
	u32 num_cs = 1;
	int status;

	ssp = pxa2xx_spi_ssp_request(pdev);
	if (IS_ERR(ssp))
		return ERR_CAST(ssp);
	if (ssp) {
		type = ssp->type;
	} else if (match) {
		type = (enum pxa_ssp_type)(uintptr_t)match;
	} else {
		u32 value;

		status = device_property_read_u32(dev, "intel,spi-pxa2xx-type", &value);
		if (status)
			return ERR_PTR(status);

		type = (enum pxa_ssp_type)value;
	}

	/* Validate the SSP type correctness */
	if (!(type > SSP_UNDEFINED && type < SSP_MAX))
		return ERR_PTR(-EINVAL);

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	/* Platforms with iDMA 64-bit */
	is_lpss_priv = platform_get_resource_byname(pdev, IORESOURCE_MEM, "lpss_priv");
	if (is_lpss_priv) {
		pdata->tx_param = parent;
		pdata->rx_param = parent;
		pdata->dma_filter = pxa2xx_spi_idma_filter;
	}

	/* Read number of chip select pins, if provided */
	device_property_read_u32(dev, "num-cs", &num_cs);

	pdata->num_chipselect = num_cs;
	pdata->is_target = device_property_read_bool(dev, "spi-slave");
	pdata->enable_dma = true;
	pdata->dma_burst_size = 1;

	/* If SSP has been already enumerated, use it */
	if (ssp)
		return pdata;

	status = pxa2xx_spi_init_ssp(pdev, &pdata->ssp, type);
	if (status)
		return ERR_PTR(status);

	return pdata;
}

static int pxa2xx_spi_platform_probe(struct platform_device *pdev)
{
	struct pxa2xx_spi_controller *platform_info;
	struct device *dev = &pdev->dev;
	struct ssp_device *ssp;
	int ret;

	platform_info = dev_get_platdata(dev);
	if (!platform_info) {
		platform_info = pxa2xx_spi_init_pdata(pdev);
		if (IS_ERR(platform_info))
			return dev_err_probe(dev, PTR_ERR(platform_info), "missing platform data\n");
	}

	ssp = pxa2xx_spi_ssp_request(pdev);
	if (IS_ERR(ssp))
		return PTR_ERR(ssp);
	if (!ssp)
		ssp = &platform_info->ssp;

	pm_runtime_set_autosuspend_delay(dev, 50);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	ret = pxa2xx_spi_probe(dev, ssp, platform_info);
	if (ret)
		pm_runtime_disable(dev);

	return ret;
}

static void pxa2xx_spi_platform_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	pm_runtime_get_sync(dev);

	pxa2xx_spi_remove(dev);

	pm_runtime_put_noidle(dev);
	pm_runtime_disable(dev);
}

static const struct acpi_device_id pxa2xx_spi_acpi_match[] = {
	{ "80860F0E" },
	{ "8086228E" },
	{ "INT33C0" },
	{ "INT33C1" },
	{ "INT3430" },
	{ "INT3431" },
	{}
};
MODULE_DEVICE_TABLE(acpi, pxa2xx_spi_acpi_match);

static const struct of_device_id pxa2xx_spi_of_match[] = {
	{ .compatible = "marvell,mmp2-ssp", .data = (void *)MMP2_SSP },
	{}
};
MODULE_DEVICE_TABLE(of, pxa2xx_spi_of_match);

static struct platform_driver driver = {
	.driver = {
		.name	= "pxa2xx-spi",
		.pm	= pm_ptr(&pxa2xx_spi_pm_ops),
		.acpi_match_table = pxa2xx_spi_acpi_match,
		.of_match_table = pxa2xx_spi_of_match,
	},
	.probe = pxa2xx_spi_platform_probe,
	.remove = pxa2xx_spi_platform_remove,
};

static int __init pxa2xx_spi_init(void)
{
	return platform_driver_register(&driver);
}
subsys_initcall(pxa2xx_spi_init);

static void __exit pxa2xx_spi_exit(void)
{
	platform_driver_unregister(&driver);
}
module_exit(pxa2xx_spi_exit);

MODULE_AUTHOR("Stephen Street");
MODULE_DESCRIPTION("PXA2xx SSP SPI Controller platform driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(SPI_PXA2xx);
MODULE_ALIAS("platform:pxa2xx-spi");
MODULE_SOFTDEP("pre: dw_dmac");
