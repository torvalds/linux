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

#if defined (CONFIG_CAN_FLEXCAN) || defined (CONFIG_CAN_FLEXCAN_MODULE)
#include <linux/can/platform/flexcan.h>
struct platform_device *__init imx_add_flexcan(int id,
		resource_size_t iobase, resource_size_t iosize,
		resource_size_t irq,
		const struct flexcan_platform_data *pdata);
#else
/* the ifdef can be removed once the flexcan driver has been merged */
struct flexcan_platform_data;
static inline struct platform_device *__init imx_add_flexcan(int id,
		resource_size_t iobase, resource_size_t iosize,
		resource_size_t irq,
		const struct flexcan_platform_data *pdata)
{
	return NULL;
}
#endif

#include <mach/i2c.h>
struct platform_device *__init imx_add_imx_i2c(int id,
		resource_size_t iobase, resource_size_t iosize, int irq,
		const struct imxi2c_platform_data *pdata);

#include <mach/imx-uart.h>
struct platform_device *__init imx_add_imx_uart_3irq(int id,
		resource_size_t iobase, resource_size_t iosize,
		resource_size_t irqrx, resource_size_t irqtx,
		resource_size_t irqrts,
		const struct imxuart_platform_data *pdata);
struct platform_device *__init imx_add_imx_uart_1irq(int id,
		resource_size_t iobase, resource_size_t iosize,
		resource_size_t irq,
		const struct imxuart_platform_data *pdata);

#include <mach/mxc_nand.h>
struct platform_device *__init imx_add_mxc_nand_v1(resource_size_t iobase,
		int irq, const struct mxc_nand_platform_data *pdata);
struct platform_device *__init imx_add_mxc_nand_v21(resource_size_t iobase,
		int irq, const struct mxc_nand_platform_data *pdata);

#include <mach/spi.h>
struct platform_device *__init imx_add_spi_imx(int id,
		resource_size_t iobase, resource_size_t iosize, int irq,
		const struct spi_imx_master *pdata);
