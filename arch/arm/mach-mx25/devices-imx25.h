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

extern const struct imx_fec_data imx25_fec_data __initconst;
#define imx25_add_fec(pdata)	\
	imx_add_fec(&imx25_fec_data, pdata)

#define imx25_add_flexcan0(pdata)	\
	imx_add_flexcan(0, MX25_CAN1_BASE_ADDR, SZ_16K, MX25_INT_CAN1, pdata)
#define imx25_add_flexcan1(pdata)	\
	imx_add_flexcan(1, MX25_CAN2_BASE_ADDR, SZ_16K, MX25_INT_CAN2, pdata)

extern const struct imx_imx_i2c_data imx25_imx_i2c_data[] __initconst;
#define imx25_add_imx_i2c(id, pdata)	\
	imx_add_imx_i2c(&imx25_imx_i2c_data[id], pdata)
#define imx25_add_imx_i2c0(pdata)	imx25_add_imx_i2c(0, pdata)
#define imx25_add_imx_i2c1(pdata)	imx25_add_imx_i2c(1, pdata)
#define imx25_add_imx_i2c2(pdata)	imx25_add_imx_i2c(2, pdata)

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

extern const struct imx_mxc_nand_data imx25_mxc_nand_data __initconst;
#define imx25_add_mxc_nand(pdata)	\
	imx_add_mxc_nand(&imx25_mxc_nand_data, pdata)

extern const struct imx_spi_imx_data imx25_cspi_data[] __initconst;
#define imx25_add_spi_imx(id, pdata)	\
	imx_add_spi_imx(&imx25_cspi_data[id], pdata)
#define imx25_add_spi_imx0(pdata)	imx25_add_spi_imx(0, pdata)
#define imx25_add_spi_imx1(pdata)	imx25_add_spi_imx(1, pdata)
#define imx25_add_spi_imx2(pdata)	imx25_add_spi_imx(2, pdata)

extern const struct imx_esdhc_imx_data imx25_esdhc_data[] __initconst;
#define imx25_add_esdhc(id, pdata)	\
	imx_add_esdhc(&imx25_esdhc_data[id], pdata)
