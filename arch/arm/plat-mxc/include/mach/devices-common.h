/*
 * Copyright (C) 2009-2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/init.h>

struct platform_device *imx_add_platform_device(const char *name, int id,
		const struct resource *res, unsigned int num_resources,
		const void *data, size_t size_data);

#include <linux/can/platform/flexcan.h>
struct platform_device *__init imx_add_flexcan(int id,
		resource_size_t iobase, resource_size_t iosize,
		resource_size_t irq,
		const struct flexcan_platform_data *pdata);

#include <mach/i2c.h>
struct imx_imx_i2c_data {
	int id;
	resource_size_t iobase;
	resource_size_t iosize;
	resource_size_t irq;
};
struct platform_device *__init imx_add_imx_i2c(
		const struct imx_imx_i2c_data *data,
		const struct imxi2c_platform_data *pdata);

#include <mach/ssi.h>
struct imx_imx_ssi_data {
	int id;
	resource_size_t iobase;
	resource_size_t iosize;
	resource_size_t irq;
	resource_size_t dmatx0;
	resource_size_t dmarx0;
	resource_size_t dmatx1;
	resource_size_t dmarx1;
};
struct platform_device *__init imx_add_imx_ssi(
		const struct imx_imx_ssi_data *data,
		const struct imx_ssi_platform_data *pdata);

#include <mach/imx-uart.h>
struct imx_imx_uart_3irq_data {
	int id;
	resource_size_t iobase;
	resource_size_t iosize;
	resource_size_t irqrx;
	resource_size_t irqtx;
	resource_size_t irqrts;
};
struct platform_device *__init imx_add_imx_uart_3irq(
		const struct imx_imx_uart_3irq_data *data,
		const struct imxuart_platform_data *pdata);

struct imx_imx_uart_1irq_data {
	int id;
	resource_size_t iobase;
	resource_size_t iosize;
	resource_size_t irq;
};
struct platform_device *__init imx_add_imx_uart_1irq(
		const struct imx_imx_uart_1irq_data *data,
		const struct imxuart_platform_data *pdata);

#include <mach/mxc_nand.h>
struct imx_mxc_nand_data {
	resource_size_t iobase;
	resource_size_t iosize;
	resource_size_t irq;
};
struct platform_device *__init imx_add_mxc_nand(
		const struct imx_mxc_nand_data *data,
		const struct mxc_nand_platform_data *pdata);

#include <mach/spi.h>
struct imx_spi_imx_data {
	const char *devid;
	int id;
	resource_size_t iobase;
	resource_size_t iosize;
	int irq;
};
struct platform_device *__init imx_add_spi_imx(
		const struct imx_spi_imx_data *data,
		const struct spi_imx_master *pdata);
