/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include "devices/devices-common.h"

extern const struct imx_fec_data imx25_fec_data;
#define imx25_add_fec(pdata)	\
	imx_add_fec(&imx25_fec_data, pdata)

extern const struct imx_flexcan_data imx25_flexcan_data[];
#define imx25_add_flexcan(id, pdata)	\
	imx_add_flexcan(&imx25_flexcan_data[id], pdata)
#define imx25_add_flexcan0(pdata)	imx25_add_flexcan(0, pdata)
#define imx25_add_flexcan1(pdata)	imx25_add_flexcan(1, pdata)

extern const struct imx_fsl_usb2_udc_data imx25_fsl_usb2_udc_data;
#define imx25_add_fsl_usb2_udc(pdata)	\
	imx_add_fsl_usb2_udc(&imx25_fsl_usb2_udc_data, pdata)

extern struct imx_imxdi_rtc_data imx25_imxdi_rtc_data;
#define imx25_add_imxdi_rtc()	\
	imx_add_imxdi_rtc(&imx25_imxdi_rtc_data)

extern const struct imx_imx2_wdt_data imx25_imx2_wdt_data;
#define imx25_add_imx2_wdt()	\
	imx_add_imx2_wdt(&imx25_imx2_wdt_data)

extern const struct imx_imx_fb_data imx25_imx_fb_data;
#define imx25_add_imx_fb(pdata)	\
	imx_add_imx_fb(&imx25_imx_fb_data, pdata)

extern const struct imx_imx_i2c_data imx25_imx_i2c_data[];
#define imx25_add_imx_i2c(id, pdata)	\
	imx_add_imx_i2c(&imx25_imx_i2c_data[id], pdata)
#define imx25_add_imx_i2c0(pdata)	imx25_add_imx_i2c(0, pdata)
#define imx25_add_imx_i2c1(pdata)	imx25_add_imx_i2c(1, pdata)
#define imx25_add_imx_i2c2(pdata)	imx25_add_imx_i2c(2, pdata)

extern const struct imx_imx_keypad_data imx25_imx_keypad_data;
#define imx25_add_imx_keypad(pdata)	\
	imx_add_imx_keypad(&imx25_imx_keypad_data, pdata)

extern const struct imx_imx_ssi_data imx25_imx_ssi_data[];
#define imx25_add_imx_ssi(id, pdata)	\
	imx_add_imx_ssi(&imx25_imx_ssi_data[id], pdata)

extern const struct imx_imx_uart_1irq_data imx25_imx_uart_data[];
#define imx25_add_imx_uart(id, pdata)	\
	imx_add_imx_uart_1irq(&imx25_imx_uart_data[id], pdata)
#define imx25_add_imx_uart0(pdata)	imx25_add_imx_uart(0, pdata)
#define imx25_add_imx_uart1(pdata)	imx25_add_imx_uart(1, pdata)
#define imx25_add_imx_uart2(pdata)	imx25_add_imx_uart(2, pdata)
#define imx25_add_imx_uart3(pdata)	imx25_add_imx_uart(3, pdata)
#define imx25_add_imx_uart4(pdata)	imx25_add_imx_uart(4, pdata)

extern const struct imx_mx2_camera_data imx25_mx2_camera_data;
#define imx25_add_mx2_camera(pdata)	\
	imx_add_mx2_camera(&imx25_mx2_camera_data, pdata)

extern const struct imx_mxc_ehci_data imx25_mxc_ehci_otg_data;
#define imx25_add_mxc_ehci_otg(pdata)	\
	imx_add_mxc_ehci(&imx25_mxc_ehci_otg_data, pdata)
extern const struct imx_mxc_ehci_data imx25_mxc_ehci_hs_data;
#define imx25_add_mxc_ehci_hs(pdata)	\
	imx_add_mxc_ehci(&imx25_mxc_ehci_hs_data, pdata)

extern const struct imx_mxc_nand_data imx25_mxc_nand_data;
#define imx25_add_mxc_nand(pdata)	\
	imx_add_mxc_nand(&imx25_mxc_nand_data, pdata)

extern const struct imx_sdhci_esdhc_imx_data imx25_sdhci_esdhc_imx_data[];
#define imx25_add_sdhci_esdhc_imx(id, pdata)	\
	imx_add_sdhci_esdhc_imx(&imx25_sdhci_esdhc_imx_data[id], pdata)

extern const struct imx_spi_imx_data imx25_cspi_data[];
#define imx25_add_spi_imx(id, pdata)	\
	imx_add_spi_imx(&imx25_cspi_data[id], pdata)
#define imx25_add_spi_imx0(pdata)	imx25_add_spi_imx(0, pdata)
#define imx25_add_spi_imx1(pdata)	imx25_add_spi_imx(1, pdata)
#define imx25_add_spi_imx2(pdata)	imx25_add_spi_imx(2, pdata)

extern struct imx_mxc_pwm_data imx25_mxc_pwm_data[];
#define imx25_add_mxc_pwm(id)	\
	imx_add_mxc_pwm(&imx25_mxc_pwm_data[id])
