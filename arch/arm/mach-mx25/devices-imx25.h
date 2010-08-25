/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/mx25.h>
#include <mach/devices-common.h>

#define imx25_add_flexcan0(pdata)	\
	imx_add_flexcan(0, MX25_CAN1_BASE_ADDR, SZ_16K, MX25_INT_CAN1, pdata)
#define imx25_add_flexcan1(pdata)	\
	imx_add_flexcan(1, MX25_CAN2_BASE_ADDR, SZ_16K, MX25_INT_CAN2, pdata)

#define imx25_add_imx_i2c0(pdata)	\
	imx_add_imx_i2c(0, MX25_I2C1_BASE_ADDR, SZ_16K, MX25_INT_I2C1, pdata)
#define imx25_add_imx_i2c1(pdata)	\
	imx_add_imx_i2c(1, MX25_I2C2_BASE_ADDR, SZ_16K, MX25_INT_I2C2, pdata)
#define imx25_add_imx_i2c2(pdata)	\
	imx_add_imx_i2c(2, MX25_I2C3_BASE_ADDR, SZ_16K, MX25_INT_I2C3, pdata)

extern const struct imx_imx_ssi_data imx25_imx_ssi_data[] __initconst;
#define imx25_add_imx_ssi(id, pdata)	\
	imx_add_imx_ssi(&imx25_imx_ssi_data[id], pdata)

extern const struct imx_imx_uart_1irq_data imx25_imx_uart_data[] __initconst;
#define imx25_add_imx_uart(id, pdata)	\
	imx_add_imx_uart_1irq(&imx25_imx_uart_data[id], pdata)
#define imx25_add_imx_uart0(pdata)	imx25_add_imx_uart(0, pdata)
#define imx25_add_imx_uart1(pdata)	imx25_add_imx_uart(1, pdata)
#define imx25_add_imx_uart2(pdata)	imx25_add_imx_uart(2, pdata)
#define imx25_add_imx_uart3(pdata)	imx25_add_imx_uart(3, pdata)
#define imx25_add_imx_uart4(pdata)	imx25_add_imx_uart(4, pdata)

#define imx25_add_mxc_nand(pdata)	\
	imx_add_mxc_nand_v21(MX25_NFC_BASE_ADDR, MX25_INT_NANDFC, pdata)

extern const struct imx_spi_imx_data imx25_spi_imx_data[] __initconst;
#define imx25_add_spi_imx(id, pdata)	\
	imx_add_spi_imx(&imx25_spi_imx_data[id], pdata)
#define imx25_add_spi_imx0(pdata)	imx25_add_spi_imx(0, pdata)
#define imx25_add_spi_imx1(pdata)	imx25_add_spi_imx(1, pdata)
#define imx25_add_spi_imx2(pdata)	imx25_add_spi_imx(2, pdata)
