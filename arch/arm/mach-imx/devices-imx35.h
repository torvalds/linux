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

extern const struct imx_fec_data imx35_fec_data;
#define imx35_add_fec(pdata)	\
	imx_add_fec(&imx35_fec_data, pdata)

extern const struct imx_fsl_usb2_udc_data imx35_fsl_usb2_udc_data;
#define imx35_add_fsl_usb2_udc(pdata)	\
	imx_add_fsl_usb2_udc(&imx35_fsl_usb2_udc_data, pdata)

extern const struct imx_flexcan_data imx35_flexcan_data[];
#define imx35_add_flexcan(id, pdata)	\
	imx_add_flexcan(&imx35_flexcan_data[id], pdata)
#define imx35_add_flexcan0(pdata)	imx35_add_flexcan(0, pdata)
#define imx35_add_flexcan1(pdata)	imx35_add_flexcan(1, pdata)

extern const struct imx_imx2_wdt_data imx35_imx2_wdt_data;
#define imx35_add_imx2_wdt(pdata)       \
	imx_add_imx2_wdt(&imx35_imx2_wdt_data)

extern const struct imx_imx_i2c_data imx35_imx_i2c_data[];
#define imx35_add_imx_i2c(id, pdata)	\
	imx_add_imx_i2c(&imx35_imx_i2c_data[id], pdata)
#define imx35_add_imx_i2c0(pdata)	imx35_add_imx_i2c(0, pdata)
#define imx35_add_imx_i2c1(pdata)	imx35_add_imx_i2c(1, pdata)
#define imx35_add_imx_i2c2(pdata)	imx35_add_imx_i2c(2, pdata)

extern const struct imx_imx_keypad_data imx35_imx_keypad_data;
#define imx35_add_imx_keypad(pdata)	\
	imx_add_imx_keypad(&imx35_imx_keypad_data, pdata)

extern const struct imx_imx_ssi_data imx35_imx_ssi_data[];
#define imx35_add_imx_ssi(id, pdata)    \
	imx_add_imx_ssi(&imx35_imx_ssi_data[id], pdata)

extern const struct imx_imx_uart_1irq_data imx35_imx_uart_data[];
#define imx35_add_imx_uart(id, pdata)	\
	imx_add_imx_uart_1irq(&imx35_imx_uart_data[id], pdata)
#define imx35_add_imx_uart0(pdata)	imx35_add_imx_uart(0, pdata)
#define imx35_add_imx_uart1(pdata)	imx35_add_imx_uart(1, pdata)
#define imx35_add_imx_uart2(pdata)	imx35_add_imx_uart(2, pdata)

extern const struct imx_ipu_core_data imx35_ipu_core_data;
#define imx35_add_ipu_core(pdata)	\
	imx_add_ipu_core(&imx35_ipu_core_data, pdata)
#define imx35_alloc_mx3_camera(pdata)	\
	imx_alloc_mx3_camera(&imx35_ipu_core_data, pdata)
#define imx35_add_mx3_sdc_fb(pdata)	\
	imx_add_mx3_sdc_fb(&imx35_ipu_core_data, pdata)

extern const struct imx_mxc_ehci_data imx35_mxc_ehci_otg_data;
#define imx35_add_mxc_ehci_otg(pdata)	\
	imx_add_mxc_ehci(&imx35_mxc_ehci_otg_data, pdata)
extern const struct imx_mxc_ehci_data imx35_mxc_ehci_hs_data;
#define imx35_add_mxc_ehci_hs(pdata)	\
	imx_add_mxc_ehci(&imx35_mxc_ehci_hs_data, pdata)

extern const struct imx_mxc_nand_data imx35_mxc_nand_data;
#define imx35_add_mxc_nand(pdata)	\
	imx_add_mxc_nand(&imx35_mxc_nand_data, pdata)

extern const struct imx_mxc_w1_data imx35_mxc_w1_data;
#define imx35_add_mxc_w1(pdata)	\
	imx_add_mxc_w1(&imx35_mxc_w1_data)

extern const struct imx_sdhci_esdhc_imx_data imx35_sdhci_esdhc_imx_data[];
#define imx35_add_sdhci_esdhc_imx(id, pdata)	\
	imx_add_sdhci_esdhc_imx(&imx35_sdhci_esdhc_imx_data[id], pdata)

extern const struct imx_spi_imx_data imx35_cspi_data[];
#define imx35_add_cspi(id, pdata)	\
	imx_add_spi_imx(&imx35_cspi_data[id], pdata)
#define imx35_add_spi_imx0(pdata)	imx35_add_cspi(0, pdata)
#define imx35_add_spi_imx1(pdata)	imx35_add_cspi(1, pdata)
