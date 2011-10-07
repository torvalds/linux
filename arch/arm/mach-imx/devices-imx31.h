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

extern const struct imx_fsl_usb2_udc_data imx31_fsl_usb2_udc_data;
#define imx31_add_fsl_usb2_udc(pdata)	\
	imx_add_fsl_usb2_udc(&imx31_fsl_usb2_udc_data, pdata)

extern const struct imx_imx2_wdt_data imx31_imx2_wdt_data;
#define imx31_add_imx2_wdt(pdata)       \
	imx_add_imx2_wdt(&imx31_imx2_wdt_data)

extern const struct imx_imx_i2c_data imx31_imx_i2c_data[];
#define imx31_add_imx_i2c(id, pdata)	\
	imx_add_imx_i2c(&imx31_imx_i2c_data[id], pdata)
#define imx31_add_imx_i2c0(pdata)	imx31_add_imx_i2c(0, pdata)
#define imx31_add_imx_i2c1(pdata)	imx31_add_imx_i2c(1, pdata)
#define imx31_add_imx_i2c2(pdata)	imx31_add_imx_i2c(2, pdata)

extern const struct imx_imx_keypad_data imx31_imx_keypad_data;
#define imx31_add_imx_keypad(pdata)	\
	imx_add_imx_keypad(&imx31_imx_keypad_data, pdata)

extern const struct imx_imx_ssi_data imx31_imx_ssi_data[];
#define imx31_add_imx_ssi(id, pdata)    \
	imx_add_imx_ssi(&imx31_imx_ssi_data[id], pdata)

extern const struct imx_imx_uart_1irq_data imx31_imx_uart_data[];
#define imx31_add_imx_uart(id, pdata)	\
	imx_add_imx_uart_1irq(&imx31_imx_uart_data[id], pdata)
#define imx31_add_imx_uart0(pdata)	imx31_add_imx_uart(0, pdata)
#define imx31_add_imx_uart1(pdata)	imx31_add_imx_uart(1, pdata)
#define imx31_add_imx_uart2(pdata)	imx31_add_imx_uart(2, pdata)
#define imx31_add_imx_uart3(pdata)	imx31_add_imx_uart(3, pdata)
#define imx31_add_imx_uart4(pdata)	imx31_add_imx_uart(4, pdata)

extern const struct imx_ipu_core_data imx31_ipu_core_data;
#define imx31_add_ipu_core(pdata)	\
	imx_add_ipu_core(&imx31_ipu_core_data, pdata)
#define imx31_alloc_mx3_camera(pdata)	\
	imx_alloc_mx3_camera(&imx31_ipu_core_data, pdata)
#define imx31_add_mx3_sdc_fb(pdata)	\
	imx_add_mx3_sdc_fb(&imx31_ipu_core_data, pdata)

extern const struct imx_mxc_ehci_data imx31_mxc_ehci_otg_data;
#define imx31_add_mxc_ehci_otg(pdata)	\
	imx_add_mxc_ehci(&imx31_mxc_ehci_otg_data, pdata)
extern const struct imx_mxc_ehci_data imx31_mxc_ehci_hs_data[];
#define imx31_add_mxc_ehci_hs(id, pdata)	\
	imx_add_mxc_ehci(&imx31_mxc_ehci_hs_data[id - 1], pdata)

extern const struct imx_mxc_mmc_data imx31_mxc_mmc_data[];
#define imx31_add_mxc_mmc(id, pdata)	\
	imx_add_mxc_mmc(&imx31_mxc_mmc_data[id], pdata)

extern const struct imx_mxc_nand_data imx31_mxc_nand_data;
#define imx31_add_mxc_nand(pdata)	\
	imx_add_mxc_nand(&imx31_mxc_nand_data, pdata)

extern const struct imx_mxc_rtc_data imx31_mxc_rtc_data;
#define imx31_add_mxc_rtc(pdata)	\
	imx_add_mxc_rtc(&imx31_mxc_rtc_data)

extern const struct imx_mxc_w1_data imx31_mxc_w1_data;
#define imx31_add_mxc_w1(pdata)	\
	imx_add_mxc_w1(&imx31_mxc_w1_data)

extern const struct imx_spi_imx_data imx31_cspi_data[];
#define imx31_add_cspi(id, pdata)	\
	imx_add_spi_imx(&imx31_cspi_data[id], pdata)
#define imx31_add_spi_imx0(pdata)	imx31_add_cspi(0, pdata)
#define imx31_add_spi_imx1(pdata)	imx31_add_cspi(1, pdata)
#define imx31_add_spi_imx2(pdata)	imx31_add_cspi(2, pdata)

extern const struct imx_pata_imx_data imx31_pata_imx_data;
#define imx31_add_pata_imx() \
	imx_add_pata_imx(&imx31_pata_imx_data)
