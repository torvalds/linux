/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/init.h>

#include <mach/hardware.h>
#include <mach/devices-common.h>
#include <mach/sdma.h>

struct imx_imx_sdma_data {
	resource_size_t iobase;
	resource_size_t irq;
	struct sdma_platform_data pdata;
};

#define imx_imx_sdma_data_entry_single(soc, _sdma_version, _cpu_name, _to_version)\
	{								\
		.iobase = soc ## _SDMA ## _BASE_ADDR,			\
		.irq = soc ## _INT_SDMA,				\
		.pdata = {						\
			.sdma_version = _sdma_version,			\
			.cpu_name = _cpu_name,				\
			.to_version = _to_version,			\
		},							\
	}

#ifdef CONFIG_ARCH_MX25
const struct imx_imx_sdma_data imx25_imx_sdma_data __initconst =
	imx_imx_sdma_data_entry_single(MX25, 1, "imx25", 0);
#endif /* ifdef CONFIG_ARCH_MX25 */

#ifdef CONFIG_ARCH_MX31
struct imx_imx_sdma_data imx31_imx_sdma_data __initdata =
	imx_imx_sdma_data_entry_single(MX31, 1, "imx31", 0);
#endif /* ifdef CONFIG_ARCH_MX31 */

#ifdef CONFIG_ARCH_MX35
struct imx_imx_sdma_data imx35_imx_sdma_data __initdata =
	imx_imx_sdma_data_entry_single(MX35, 2, "imx35", 0);
#endif /* ifdef CONFIG_ARCH_MX35 */

#ifdef CONFIG_ARCH_MX51
const struct imx_imx_sdma_data imx51_imx_sdma_data __initconst =
	imx_imx_sdma_data_entry_single(MX51, 2, "imx51", 0);
#endif /* ifdef CONFIG_ARCH_MX51 */

static struct platform_device __init __maybe_unused *imx_add_imx_sdma(
		const struct imx_imx_sdma_data *data)
{
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + SZ_4K - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = data->irq,
			.end = data->irq,
			.flags = IORESOURCE_IRQ,
		},
	};

	return imx_add_platform_device("imx-sdma", -1,
			res, ARRAY_SIZE(res),
			&data->pdata, sizeof(data->pdata));
}

static struct platform_device __init __maybe_unused *imx_add_imx_dma(void)
{
	return imx_add_platform_device("imx-dma", -1, NULL, 0, NULL, 0);
}

static int __init imxXX_add_imx_dma(void)
{
	struct platform_device *ret;

#if defined(CONFIG_SOC_IMX21) || defined(CONFIG_SOC_IMX27)
	if (cpu_is_mx21() || cpu_is_mx27())
		ret = imx_add_imx_dma();
	else
#endif

#if defined(CONFIG_ARCH_MX25)
	if (cpu_is_mx25())
		ret = imx_add_imx_sdma(&imx25_imx_sdma_data);
	else
#endif

#if defined(CONFIG_ARCH_MX31)
	if (cpu_is_mx31()) {
		imx31_imx_sdma_data.pdata.to_version = mx31_revision() >> 4;
		ret = imx_add_imx_sdma(&imx31_imx_sdma_data);
	} else
#endif

#if defined(CONFIG_ARCH_MX35)
	if (cpu_is_mx35()) {
		imx35_imx_sdma_data.pdata.to_version = mx35_revision() >> 4;
		ret = imx_add_imx_sdma(&imx35_imx_sdma_data);
	} else
#endif

#if defined(CONFIG_ARCH_MX51)
	if (cpu_is_mx51())
		ret = imx_add_imx_sdma(&imx51_imx_sdma_data);
	else
#endif
		ret = ERR_PTR(-ENODEV);

	if (IS_ERR(ret))
		return PTR_ERR(ret);

	return 0;
}
arch_initcall(imxXX_add_imx_dma);
