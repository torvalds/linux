/*
 * Copyright (C) 2009-2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include "../hardware.h"
#include "devices-common.h"

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
	imx_imx_uart_1irq_data_entry(MX35, _id, _hwid, SZ_16K)
	imx35_imx_uart_data_entry(0, 1),
	imx35_imx_uart_data_entry(1, 2),
	imx35_imx_uart_data_entry(2, 3),
};
#endif /* ifdef CONFIG_SOC_IMX35 */

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

	/* i.mx21 type uart runs on all i.mx except i.mx1 */
	return imx_add_platform_device("imx21-uart", data->id,
			res, ARRAY_SIZE(res), pdata, sizeof(*pdata));
}
