/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/mx21.h>
#include <mach/devices-common.h>

#define imx21_add_i2c_imx(pdata)	\
	imx_add_imx_i2c(0, MX2x_I2C_BASE_ADDR, SZ_4K, MX2x_INT_I2C, pdata)

#define imx21_add_imx_uart0(pdata)	\
	imx_add_imx_uart_1irq(0, MX21_UART1_BASE_ADDR, SZ_4K, MX21_INT_UART1, pdata)
#define imx21_add_imx_uart1(pdata)	\
	imx_add_imx_uart_1irq(1, MX21_UART2_BASE_ADDR, SZ_4K, MX21_INT_UART2, pdata)
#define imx21_add_imx_uart2(pdata)	\
	imx_add_imx_uart_1irq(2, MX21_UART3_BASE_ADDR, SZ_4K, MX21_INT_UART3, pdata)
#define imx21_add_imx_uart3(pdata)	\
	imx_add_imx_uart_1irq(3, MX21_UART4_BASE_ADDR, SZ_4K, MX21_INT_UART4, pdata)

#define imx21_add_mxc_nand(pdata)	\
	imx_add_mxc_nand_v1(MX21_NFC_BASE_ADDR, MX21_INT_NANDFC, pdata)

#define imx21_add_spi_imx0(pdata)	\
	imx_add_spi_imx(0, MX21_CSPI1_BASE_ADDR, SZ_4K, MX21_INT_CSPI1, pdata)
#define imx21_add_spi_imx1(pdata)	\
	imx_add_spi_imx(1, MX21_CSPI2_BASE_ADDR, SZ_4K, MX21_INT_CSPI2, pdata)
