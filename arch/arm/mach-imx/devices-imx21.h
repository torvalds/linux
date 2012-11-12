/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include "devices/devices-common.h"

extern const struct imx_imx21_hcd_data imx21_imx21_hcd_data;
#define imx21_add_imx21_hcd(pdata)	\
	imx_add_imx21_hcd(&imx21_imx21_hcd_data, pdata)

extern const struct imx_imx2_wdt_data imx21_imx2_wdt_data;
#define imx21_add_imx2_wdt()	\
	imx_add_imx2_wdt(&imx21_imx2_wdt_data)

extern const struct imx_imx_fb_data imx21_imx_fb_data;
#define imx21_add_imx_fb(pdata)	\
	imx_add_imx_fb(&imx21_imx_fb_data, pdata)

extern const struct imx_imx_i2c_data imx21_imx_i2c_data;
#define imx21_add_imx_i2c(pdata)	\
	imx_add_imx_i2c(&imx21_imx_i2c_data, pdata)

extern const struct imx_imx_keypad_data imx21_imx_keypad_data;
#define imx21_add_imx_keypad(pdata)	\
	imx_add_imx_keypad(&imx21_imx_keypad_data, pdata)

extern const struct imx_imx_ssi_data imx21_imx_ssi_data[];
#define imx21_add_imx_ssi(id, pdata)	\
	imx_add_imx_ssi(&imx21_imx_ssi_data[id], pdata)

extern const struct imx_imx_uart_1irq_data imx21_imx_uart_data[];
#define imx21_add_imx_uart(id, pdata)	\
	imx_add_imx_uart_1irq(&imx21_imx_uart_data[id], pdata)
#define imx21_add_imx_uart0(pdata)	imx21_add_imx_uart(0, pdata)
#define imx21_add_imx_uart1(pdata)	imx21_add_imx_uart(1, pdata)
#define imx21_add_imx_uart2(pdata)	imx21_add_imx_uart(2, pdata)
#define imx21_add_imx_uart3(pdata)	imx21_add_imx_uart(3, pdata)

extern const struct imx_mxc_mmc_data imx21_mxc_mmc_data[];
#define imx21_add_mxc_mmc(id, pdata)	\
	imx_add_mxc_mmc(&imx21_mxc_mmc_data[id], pdata)

extern const struct imx_mxc_nand_data imx21_mxc_nand_data;
#define imx21_add_mxc_nand(pdata)	\
	imx_add_mxc_nand(&imx21_mxc_nand_data, pdata)

extern const struct imx_mxc_w1_data imx21_mxc_w1_data;
#define imx21_add_mxc_w1()	\
	imx_add_mxc_w1(&imx21_mxc_w1_data)

extern const struct imx_spi_imx_data imx21_cspi_data[];
#define imx21_add_cspi(id, pdata)	\
	imx_add_spi_imx(&imx21_cspi_data[id], pdata)
#define imx21_add_spi_imx0(pdata)	imx21_add_cspi(0, pdata)
#define imx21_add_spi_imx1(pdata)	imx21_add_cspi(1, pdata)
