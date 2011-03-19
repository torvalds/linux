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

#ifdef CONFIG_SOC_IMX25
struct imx_imx_sdma_data imx25_imx_sdma_data __initconst =
	imx_imx_sdma_data_entry_single(MX25, 1, "imx25", 0);
#endif /* ifdef CONFIG_SOC_IMX25 */

#ifdef CONFIG_SOC_IMX31
struct imx_imx_sdma_data imx31_imx_sdma_data __initdata =
	imx_imx_sdma_data_entry_single(MX31, 1, "imx31", 0);
#endif /* ifdef CONFIG_SOC_IMX31 */

#ifdef CONFIG_SOC_IMX35
struct imx_imx_sdma_data imx35_imx_sdma_data __initdata =
	imx_imx_sdma_data_entry_single(MX35, 2, "imx35", 0);
#endif /* ifdef CONFIG_SOC_IMX35 */

#ifdef CONFIG_SOC_IMX51
struct imx_imx_sdma_data imx51_imx_sdma_data __initconst =
	imx_imx_sdma_data_entry_single(MX51, 2, "imx51", 0);
#endif /* ifdef CONFIG_SOC_IMX51 */

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

#ifdef CONFIG_ARCH_MX25
static struct sdma_script_start_addrs addr_imx25_to1 = {
	.ap_2_ap_addr = 729,
	.uart_2_mcu_addr = 904,
	.per_2_app_addr = 1255,
	.mcu_2_app_addr = 834,
	.uartsh_2_mcu_addr = 1120,
	.per_2_shp_addr = 1329,
	.mcu_2_shp_addr = 1048,
	.ata_2_mcu_addr = 1560,
	.mcu_2_ata_addr = 1479,
	.app_2_per_addr = 1189,
	.app_2_mcu_addr = 770,
	.shp_2_per_addr = 1407,
	.shp_2_mcu_addr = 979,
};
#endif

#ifdef CONFIG_ARCH_MX31
static struct sdma_script_start_addrs addr_imx31_to1 = {
	.per_2_per_addr = 1677,
};

static struct sdma_script_start_addrs addr_imx31_to2 = {
	.ap_2_ap_addr = 423,
	.ap_2_bp_addr = 829,
	.bp_2_ap_addr = 1029,
};
#endif

#ifdef CONFIG_ARCH_MX35
static struct sdma_script_start_addrs addr_imx35_to1 = {
	.ap_2_ap_addr = 642,
	.uart_2_mcu_addr = 817,
	.mcu_2_app_addr = 747,
	.uartsh_2_mcu_addr = 1183,
	.per_2_shp_addr = 1033,
	.mcu_2_shp_addr = 961,
	.ata_2_mcu_addr = 1333,
	.mcu_2_ata_addr = 1252,
	.app_2_mcu_addr = 683,
	.shp_2_per_addr = 1111,
	.shp_2_mcu_addr = 892,
};

static struct sdma_script_start_addrs addr_imx35_to2 = {
	.ap_2_ap_addr = 729,
	.uart_2_mcu_addr = 904,
	.per_2_app_addr = 1597,
	.mcu_2_app_addr = 834,
	.uartsh_2_mcu_addr = 1270,
	.per_2_shp_addr = 1120,
	.mcu_2_shp_addr = 1048,
	.ata_2_mcu_addr = 1429,
	.mcu_2_ata_addr = 1339,
	.app_2_per_addr = 1531,
	.app_2_mcu_addr = 770,
	.shp_2_per_addr = 1198,
	.shp_2_mcu_addr = 979,
};
#endif

#ifdef CONFIG_SOC_IMX51
static struct sdma_script_start_addrs addr_imx51_to1 = {
	.ap_2_ap_addr = 642,
	.uart_2_mcu_addr = 817,
	.mcu_2_app_addr = 747,
	.mcu_2_shp_addr = 961,
	.ata_2_mcu_addr = 1473,
	.mcu_2_ata_addr = 1392,
	.app_2_per_addr = 1033,
	.app_2_mcu_addr = 683,
	.shp_2_per_addr = 1251,
	.shp_2_mcu_addr = 892,
};
#endif

static int __init imxXX_add_imx_dma(void)
{
	struct platform_device *ret;

#if defined(CONFIG_SOC_IMX21) || defined(CONFIG_SOC_IMX27)
	if (cpu_is_mx21() || cpu_is_mx27())
		ret = imx_add_imx_dma();
	else
#endif

#if defined(CONFIG_SOC_IMX25)
	if (cpu_is_mx25()) {
		imx25_imx_sdma_data.pdata.script_addrs = &addr_imx25_to1;
		ret = imx_add_imx_sdma(&imx25_imx_sdma_data);
	} else
#endif

#if defined(CONFIG_SOC_IMX31)
	if (cpu_is_mx31()) {
		int to_version = mx31_revision() >> 4;
		imx31_imx_sdma_data.pdata.to_version = to_version;
		if (to_version == 1)
			imx31_imx_sdma_data.pdata.script_addrs = &addr_imx31_to1;
		else
			imx31_imx_sdma_data.pdata.script_addrs = &addr_imx31_to2;
		ret = imx_add_imx_sdma(&imx31_imx_sdma_data);
	} else
#endif

#if defined(CONFIG_SOC_IMX35)
	if (cpu_is_mx35()) {
		int to_version = mx35_revision() >> 4;
		imx35_imx_sdma_data.pdata.to_version = to_version;
		if (to_version == 1)
			imx35_imx_sdma_data.pdata.script_addrs = &addr_imx35_to1;
		else
			imx35_imx_sdma_data.pdata.script_addrs = &addr_imx35_to2;
		ret = imx_add_imx_sdma(&imx35_imx_sdma_data);
	} else
#endif

#if defined(CONFIG_ARCH_MX51)
	if (cpu_is_mx51()) {
		imx51_imx_sdma_data.pdata.script_addrs = &addr_imx51_to1;
		ret = imx_add_imx_sdma(&imx51_imx_sdma_data);
	} else
#endif
		ret = ERR_PTR(-ENODEV);

	if (IS_ERR(ret))
		return PTR_ERR(ret);

	return 0;
}
arch_initcall(imxXX_add_imx_dma);
