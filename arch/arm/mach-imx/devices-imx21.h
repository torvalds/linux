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

extern const struct imx_imx_uart_1irq_data imx21_imx_uart_data[] __initconst;
#define imx21_add_imx_uart(id, pdata)	\
	imx_add_imx_uart_1irq(&imx21_imx_uart_data[id], pdata)
#define imx21_add_imx_uart0(pdata)	imx21_add_imx_uart(0, pdata)
#define imx21_add_imx_uart1(pdata)	imx21_add_imx_uart(1, pdata)
#define imx21_add_imx_uart2(pdata)	imx21_add_imx_uart(2, pdata)
#define imx21_add_imx_uart3(pdata)	imx21_add_imx_uart(3, pdata)

#define imx21_add_mxc_nand(pdata)	\
	imx_add_mxc_nand_v1(MX21_NFC_BASE_ADDR, MX21_INT_NANDFC, pdata)

extern const struct imx_spi_imx_data imx21_cspi_data[] __initconst;
#define imx21_add_cspi(id, pdata)	\
	imx_add_spi_imx(&imx21_cspi_data[id], pdata)
#define imx21_add_spi_imx0(pdata)	imx21_add_cspi(0, pdata)
#define imx21_add_spi_imx1(pdata)	imx21_add_cspi(1, pdata)
