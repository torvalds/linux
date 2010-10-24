/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/mx1.h>
#include <mach/devices-common.h>

extern const struct imx_imx_i2c_data imx1_imx_i2c_data __initconst;
#define imx1_add_imx_i2c(pdata)		\
	imx_add_imx_i2c(&imx1_imx_i2c_data, pdata)

extern const struct imx_imx_uart_3irq_data imx1_imx_uart_data[] __initconst;
#define imx1_add_imx_uart(id, pdata)	\
	imx_add_imx_uart_3irq(&imx1_imx_uart_data[id], pdata)
#define imx1_add_imx_uart0(pdata)	imx1_add_imx_uart(0, pdata)
#define imx1_add_imx_uart1(pdata)	imx1_add_imx_uart(1, pdata)
