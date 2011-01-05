/*
 * Copyright (C) 2009-2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/hardware.h>
#include <mach/devices-common.h>

#define imx_imx_uart_3irq_data_entry(soc, _id, _hwid, _size)		\
	[_id] = {							\
		.id = _id,						\
		.iobase = soc ## _UART ## _hwid ## _BASE_ADDR,		\
		.iosize = _size,					\
		.irqrx = soc ## _INT_UART ## _hwid ## RX,		\
		.irqtx = soc ## _INT_UART ## _hwid ## TX,		\
		.irqrts = soc ## _INT_UART ## _hwid ## RTS,		\
	}

#define imx_imx_uart_1irq_data_entry(soc, _id, _hwid, _size)		\
	[_id] = {							\
		.id = _id,						\
		.iobase = soc ## _UART ## _hwid ## _BASE_ADDR,		\
		.iosize = _size,					\
		.irq = soc ## _INT_UART ## _hwid,			\
	}

#ifdef CONFIG_SOC_IMX1
const struct imx_imx_uart_3irq_data imx1_imx_uart_data[] __initconst = {
#define imx1_imx_uart_data_entry(_id, _hwid)				\
	imx_imx_uart_3irq_data_entry(MX1, _id, _hwid, 0xd0)
	imx1_imx_uart_data_entry(0, 1),
	imx1_imx_uart_data_entry(1, 2),
};
#endif /* ifdef CONFIG_SOC_IMX1 */

#ifdef CONFIG_SOC_IMX21
const struct imx_imx_uart_1irq_data imx21_imx_uart_data[] __initconst = {
#define imx21_imx_uart_data_entry(_id, _hwid)				\
	imx_imx_uart_1irq_data_entry(MX21, _id, _hwid, SZ_4K)
	imx21_imx_uart_data_entry(0, 1),
	imx21_imx_uart_data_entry(1, 2),
	imx21_imx_uart_data_entry(2, 3),
	imx21_imx_uart_data_entry(3, 4),
};
#endif

#ifdef CONFIG_SOC_IMX25
const struct imx_imx_uart_1irq_data imx25_imx_uart_data[] __initconst = {
#define imx25_imx_uart_data_entry(_id, _hwid)				\
	imx_imx_uart_1irq_data_entry(MX25, _id, _hwid, SZ_16K)
	imx25_imx_uart_data_entry(0, 1),
	imx25_imx_uart_data_entry(1, 2),
	imx25_imx_uart_data_entry(2, 3),
	imx25_imx_uart_data_entry(3, 4),
	imx25_imx_uart_data_entry(4, 5),
};
#endif /* ifdef CONFIG_SOC_IMX25 */

#ifdef CONFIG_SOC_IMX27
const struct imx_imx_uart_1irq_data imx27_imx_uart_data[] __initconst = {
#define imx27_imx_uart_data_entry(_id, _hwid)				\
	imx_imx_uart_1irq_data_entry(MX27, _id, _hwid, SZ_4K)
	imx27_imx_uart_data_entry(0, 1),
	imx27_imx_uart_data_entry(1, 2),
	imx27_imx_uart_data_entry(2, 3),
	imx27_imx_uart_data_entry(3, 4),
	imx27_imx_uart_data_entry(4, 5),
	imx27_imx_uart_data_entry(5, 6),
};
#endif /* ifdef CONFIG_SOC_IMX27 */

#ifdef CONFIG_SOC_IMX31
const struct imx_imx_uart_1irq_data imx31_imx_uart_data[] __initconst = {
#define imx31_imx_uart_data_entry(_id, _hwid)				\
	imx_imx_uart_1irq_data_entry(MX31, _id, _hwid, SZ_4K)
	imx31_imx_uart_data_entry(0, 1),
	imx31_imx_uart_data_entry(1, 2),
	imx31_imx_uart_data_entry(2, 3),
	imx31_imx_uart_data_entry(3, 4),
	imx31_imx_uart_data_entry(4, 5),
};
#endif /* ifdef CONFIG_SOC_IMX31 */

#ifdef CONFIG_SOC_IMX35
const struct imx_imx_uart_1irq_data imx35_imx_uart_data[] __initconst = {
#define imx35_imx_uart_data_entry(_id, _hwid)				\
	imx_imx_uart_1irq_data_entry(MX31, _id, _hwid, SZ_16K)
	imx35_imx_uart_data_entry(0, 1),
	imx35_imx_uart_data_entry(1, 2),
	imx35_imx_uart_data_entry(2, 3),
};
#endif /* ifdef CONFIG_SOC_IMX35 */

#ifdef CONFIG_SOC_IMX50
const struct imx_imx_uart_1irq_data imx50_imx_uart_data[] __initconst = {
#define imx50_imx_uart_data_entry(_id, _hwid)				\
	imx_imx_uart_1irq_data_entry(MX50, _id, _hwid, SZ_4K)
	imx50_imx_uart_data_entry(0, 1),
	imx50_imx_uart_data_entry(1, 2),
	imx50_imx_uart_data_entry(2, 3),
	imx50_imx_uart_data_entry(3, 4),
	imx50_imx_uart_data_entry(4, 5),
};
#endif /* ifdef CONFIG_SOC_IMX50 */

#ifdef CONFIG_SOC_IMX51
const struct imx_imx_uart_1irq_data imx51_imx_uart_data[] __initconst = {
#define imx51_imx_uart_data_entry(_id, _hwid)				\
	imx_imx_uart_1irq_data_entry(MX51, _id, _hwid, SZ_4K)
	imx51_imx_uart_data_entry(0, 1),
	imx51_imx_uart_data_entry(1, 2),
	imx51_imx_uart_data_entry(2, 3),
};
#endif /* ifdef CONFIG_SOC_IMX51 */

#ifdef CONFIG_SOC_IMX53
const struct imx_imx_uart_1irq_data imx53_imx_uart_data[] __initconst = {
#define imx53_imx_uart_data_entry(_id, _hwid)				\
	imx_imx_uart_1irq_data_entry(MX53, _id, _hwid, SZ_4K)
	imx53_imx_uart_data_entry(0, 1),
	imx53_imx_uart_data_entry(1, 2),
	imx53_imx_uart_data_entry(2, 3),
};
#endif /* ifdef CONFIG_SOC_IMX53 */

struct platform_device *__init imx_add_imx_uart_3irq(
		const struct imx_imx_uart_3irq_data *data,
		const struct imxuart_platform_data *pdata)
{
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + data->iosize - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = data->irqrx,
			.end = data->irqrx,
			.flags = IORESOURCE_IRQ,
		}, {
			.start = data->irqtx,
			.end = data->irqtx,
			.flags = IORESOURCE_IRQ,
		}, {
			.start = data->irqrts,
			.end = data->irqrx,
			.flags = IORESOURCE_IRQ,
		},
	};

	return imx_add_platform_device("imx-uart", data->id, res,
			ARRAY_SIZE(res), pdata, sizeof(*pdata));
}

struct platform_device *__init imx_add_imx_uart_1irq(
		const struct imx_imx_uart_1irq_data *data,
		const struct imxuart_platform_data *pdata)
{
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + data->iosize - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = data->irq,
			.end = data->irq,
			.flags = IORESOURCE_IRQ,
		},
	};

	return imx_add_platform_device("imx-uart", data->id, res, ARRAY_SIZE(res),
			pdata, sizeof(*pdata));
}
