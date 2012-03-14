/*
 * Copyright (C) 2010 Yong Shen. <Yong.Shen@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/mx53.h>
#include <mach/devices-common.h>

extern const struct imx_fec_data imx53_fec_data;
#define imx53_add_fec(pdata)   \
	imx_add_fec(&imx53_fec_data, pdata)

extern const struct imx_imx_uart_1irq_data imx53_imx_uart_data[];
#define imx53_add_imx_uart(id, pdata)	\
	imx_add_imx_uart_1irq(&imx53_imx_uart_data[id], pdata)


extern const struct imx_imx_i2c_data imx53_imx_i2c_data[];
#define imx53_add_imx_i2c(id, pdata)	\
	imx_add_imx_i2c(&imx53_imx_i2c_data[id], pdata)

extern const struct imx_sdhci_esdhc_imx_data imx53_sdhci_esdhc_imx_data[];
#define imx53_add_sdhci_esdhc_imx(id, pdata)	\
	imx_add_sdhci_esdhc_imx(&imx53_sdhci_esdhc_imx_data[id], pdata)

extern const struct imx_spi_imx_data imx53_ecspi_data[];
#define imx53_add_ecspi(id, pdata)	\
	imx_add_spi_imx(&imx53_ecspi_data[id], pdata)

extern const struct imx_imx2_wdt_data imx53_imx2_wdt_data[];
#define imx53_add_imx2_wdt(id, pdata)	\
	imx_add_imx2_wdt(&imx53_imx2_wdt_data[id])

extern const struct imx_imx_ssi_data imx53_imx_ssi_data[];
#define imx53_add_imx_ssi(id, pdata)	\
	imx_add_imx_ssi(&imx53_imx_ssi_data[id], pdata)

extern const struct imx_imx_keypad_data imx53_imx_keypad_data;
#define imx53_add_imx_keypad(pdata)	\
	imx_add_imx_keypad(&imx53_imx_keypad_data, pdata)

extern const struct imx_pata_imx_data imx53_pata_imx_data;
#define imx53_add_pata_imx() \
	imx_add_pata_imx(&imx53_pata_imx_data)

extern struct platform_device *__init imx53_add_ahci_imx(void);
