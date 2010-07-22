/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/mx35.h>
#include <mach/devices-common.h>

#define imx35_add_flexcan0(pdata)	\
	imx_add_flexcan(0, MX35_CAN1_BASE_ADDR, SZ_16K, MX35_INT_CAN1, pdata)
#define imx35_add_flexcan1(pdata)	\
	imx_add_flexcan(1, MX35_CAN2_BASE_ADDR, SZ_16K, MX35_INT_CAN2, pdata)

#define imx35_add_imx_i2c0(pdata)	\
	imx_add_imx_i2c(0, MX35_I2C1_BASE_ADDR, SZ_4K, MX35_INT_I2C1, pdata)
#define imx35_add_imx_i2c1(pdata)	\
	imx_add_imx_i2c(1, MX35_I2C2_BASE_ADDR, SZ_4K, MX35_INT_I2C2, pdata)
#define imx35_add_imx_i2c2(pdata)	\
	imx_add_imx_i2c(2, MX35_I2C3_BASE_ADDR, SZ_4K, MX35_INT_I2C3, pdata)

#define imx35_add_imx_uart0(pdata)	\
	imx_add_imx_uart_1irq(0, MX35_UART1_BASE_ADDR, SZ_16K, MX35_INT_UART1, pdata)
#define imx35_add_imx_uart1(pdata)	\
	imx_add_imx_uart_1irq(1, MX35_UART2_BASE_ADDR, SZ_16K, MX35_INT_UART2, pdata)
#define imx35_add_imx_uart2(pdata)	\
	imx_add_imx_uart_1irq(2, MX35_UART3_BASE_ADDR, SZ_16K, MX35_INT_UART3, pdata)

#define imx35_add_mxc_nand(pdata)	\
	imx_add_mxc_nand_v21(MX35_NFC_BASE_ADDR, MX35_INT_NANDFC, pdata)

#define imx35_add_spi_imx0(pdata)	\
	imx_add_spi_imx(0, MX35_CSPI1_BASE_ADDR, SZ_4K, MX35_INT_CSPI1, pdata)
#define imx35_add_spi_imx1(pdata)	\
	imx_add_spi_imx(1, MX35_CSPI2_BASE_ADDR, SZ_4K, MX35_INT_CSPI2, pdata)
