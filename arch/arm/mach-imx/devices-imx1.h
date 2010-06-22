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

#define imx1_add_i2c_imx(pdata)		\
	imx_add_imx_i2c(0, MX1_I2C_BASE_ADDR, SZ_4K, MX1_INT_I2C, pdata)

#define imx1_add_imx_uart0(pdata)	\
	imx_add_imx_uart_3irq(0, MX1_UART1_BASE_ADDR, 0xd0, MX1_INT_UART1RX, MX1_INT_UART1TX, MX1_INT_UART1RTS, pdata)
#define imx1_add_imx_uart1(pdata)	\
	imx_add_imx_uart_3irq(0, MX1_UART2_BASE_ADDR, 0xd0, MX1_INT_UART2RX, MX1_INT_UART2TX, MX1_INT_UART2RTS, pdata)
