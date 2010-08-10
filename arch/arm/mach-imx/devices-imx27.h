/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/mx27.h>
#include <mach/devices-common.h>

#define imx27_add_i2c_imx0(pdata)	\
	imx_add_imx_i2c(0, MX27_I2C1_BASE_ADDR, SZ_4K, MX27_INT_I2C1, pdata)
#define imx27_add_i2c_imx1(pdata)	\
	imx_add_imx_i2c(1, MX27_I2C2_BASE_ADDR, SZ_4K, MX27_INT_I2C2, pdata)

extern const struct imx_imx_uart_1irq_data imx27_imx_uart_data[] __initconst;
#define imx27_add_imx_uart(id, pdata)	\
	imx_add_imx_uart_1irq(&imx27_imx_uart_data[id], pdata)
#define imx27_add_imx_uart0(pdata)	imx27_add_imx_uart(0, pdata)
#define imx27_add_imx_uart1(pdata)	imx27_add_imx_uart(1, pdata)
#define imx27_add_imx_uart2(pdata)	imx27_add_imx_uart(2, pdata)
#define imx27_add_imx_uart3(pdata)	imx27_add_imx_uart(3, pdata)
#define imx27_add_imx_uart4(pdata)	imx27_add_imx_uart(4, pdata)
#define imx27_add_imx_uart5(pdata)	imx27_add_imx_uart(5, pdata)

#define imx27_add_mxc_nand(pdata)	\
	imx_add_mxc_nand_v1(MX27_NFC_BASE_ADDR, MX27_INT_NANDFC, pdata)

extern const struct imx_spi_imx_data imx27_cspi_data[] __initconst;
#define imx27_add_cspi(id, pdata)	\
	imx_add_spi_imx(&imx27_cspi_data[id], pdata)
#define imx27_add_spi_imx0(pdata)	imx27_add_cspi(0, pdata)
#define imx27_add_spi_imx1(pdata)	imx27_add_cspi(1, pdata)
#define imx27_add_spi_imx2(pdata)	imx27_add_cspi(2, pdata)
