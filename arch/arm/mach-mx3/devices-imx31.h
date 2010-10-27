/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/mx31.h>
#include <mach/devices-common.h>

#define imx31_add_imx_i2c0(pdata)	\
	imx_add_imx_i2c(0, MX31_I2C1_BASE_ADDR, SZ_4K, MX31_INT_I2C1, pdata)
#define imx31_add_imx_i2c1(pdata)	\
	imx_add_imx_i2c(1, MX31_I2C2_BASE_ADDR, SZ_4K, MX31_INT_I2C2, pdata)
#define imx31_add_imx_i2c2(pdata)	\
	imx_add_imx_i2c(2, MX31_I2C3_BASE_ADDR, SZ_4K, MX31_INT_I2C3, pdata)

#define imx31_add_imx_uart0(pdata)	\
	imx_add_imx_uart_1irq(0, MX31_UART1_BASE_ADDR, SZ_16K, MX31_INT_UART1, pdata)
#define imx31_add_imx_uart1(pdata)	\
	imx_add_imx_uart_1irq(1, MX31_UART2_BASE_ADDR, SZ_16K, MX31_INT_UART2, pdata)
#define imx31_add_imx_uart2(pdata)	\
	imx_add_imx_uart_1irq(2, MX31_UART3_BASE_ADDR, SZ_16K, MX31_INT_UART3, pdata)
#define imx31_add_imx_uart3(pdata)	\
	imx_add_imx_uart_1irq(3, MX31_UART4_BASE_ADDR, SZ_16K, MX31_INT_UART4, pdata)
#define imx31_add_imx_uart4(pdata)	\
	imx_add_imx_uart_1irq(4, MX31_UART5_BASE_ADDR, SZ_16K, MX31_INT_UART5, pdata)

#define imx31_add_mxc_nand(pdata)	\
	imx_add_mxc_nand_v1(MX31_NFC_BASE_ADDR, MX31_INT_NANDFC, pdata)

#define imx31_add_spi_imx0(pdata)	\
	imx_add_spi_imx(0, MX31_CSPI1_BASE_ADDR, SZ_4K, MX31_INT_CSPI1, pdata)
#define imx31_add_spi_imx1(pdata)	\
	imx_add_spi_imx(1, MX31_CSPI2_BASE_ADDR, SZ_4K, MX31_INT_CSPI2, pdata)
#define imx31_add_spi_imx2(pdata)	\
	imx_add_spi_imx(2, MX31_CSPI3_BASE_ADDR, SZ_4K, MX31_INT_CSPI3, pdata)
