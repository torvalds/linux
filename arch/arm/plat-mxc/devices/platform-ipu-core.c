/*
 * Copyright (C) 2011 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/hardware.h>
#include <mach/devices-common.h>

#define imx_ipu_core_entry_single(soc)					\
{									\
	.iobase = soc ## _IPU_CTRL_BASE_ADDR,				\
	.synirq = soc ## _INT_IPU_SYN,					\
	.errirq = soc ## _INT_IPU_ERR,					\
}

#ifdef CONFIG_SOC_IMX31
const struct imx_ipu_core_data imx31_ipu_core_data __initconst =
	imx_ipu_core_entry_single(MX31);
#endif

#ifdef CONFIG_SOC_IMX35
const struct imx_ipu_core_data imx35_ipu_core_data __initconst =
	imx_ipu_core_entry_single(MX35);
#endif

static struct platform_device *imx_ipu_coredev __initdata;

struct platform_device *__init imx_add_ipu_core(
		const struct imx_ipu_core_data *data,
		const struct ipu_platform_data *pdata)
{
	/* The resource order is important! */
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + 0x5f,
			.flags = IORESOURCE_MEM,
		}, {
			.start = data->iobase + 0x88,
			.end = data->iobase + 0xb3,
			.flags = IORESOURCE_MEM,
		}, {
			.start = data->synirq,
			.end = data->synirq,
			.flags = IORESOURCE_IRQ,
		}, {
			.start = data->errirq,
			.end = data->errirq,
			.flags = IORESOURCE_IRQ,
		},
	};

	return imx_ipu_coredev = imx_add_platform_device("ipu-core", -1,
			res, ARRAY_SIZE(res), pdata, sizeof(*pdata));
}

struct platform_device *__init imx_alloc_mx3_camera(
		const struct imx_ipu_core_data *data,
		const struct mx3_camera_pdata *pdata)
{
	struct resource res[] = {
		{
			.start = data->iobase + 0x60,
			.end = data->iobase + 0x87,
			.flags = IORESOURCE_MEM,
		},
	};
	int ret = -ENOMEM;
	struct platform_device *pdev;

	if (IS_ERR_OR_NULL(imx_ipu_coredev))
		return ERR_PTR(-ENODEV);

	pdev = platform_device_alloc("mx3-camera", 0);
	if (!pdev)
		goto err;

	pdev->dev.dma_mask = kmalloc(sizeof(*pdev->dev.dma_mask), GFP_KERNEL);
	if (!pdev->dev.dma_mask)
		goto err;

	*pdev->dev.dma_mask = DMA_BIT_MASK(32);
	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	ret = platform_device_add_resources(pdev, res, ARRAY_SIZE(res));
	if (ret)
		goto err;

	if (pdata) {
		struct mx3_camera_pdata *copied_pdata;

		ret = platform_device_add_data(pdev, pdata, sizeof(*pdata));
		if (ret) {
err:
			kfree(pdev->dev.dma_mask);
			platform_device_put(pdev);
			return ERR_PTR(-ENODEV);
		}
		copied_pdata = dev_get_platdata(&pdev->dev);
		copied_pdata->dma_dev = &imx_ipu_coredev->dev;
	}

	return pdev;
}

struct platform_device *__init imx_add_mx3_sdc_fb(
		const struct imx_ipu_core_data *data,
		struct mx3fb_platform_data *pdata)
{
	struct resource res[] = {
		{
			.start = data->iobase + 0xb4,
			.end = data->iobase + 0x1bf,
			.flags = IORESOURCE_MEM,
		},
	};

	if (IS_ERR_OR_NULL(imx_ipu_coredev))
		return ERR_PTR(-ENODEV);

	pdata->dma_dev = &imx_ipu_coredev->dev;

	return imx_add_platform_device_dmamask("mx3_sdc_fb", -1,
			res, ARRAY_SIZE(res), pdata, sizeof(*pdata),
			DMA_BIT_MASK(32));
}
