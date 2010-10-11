/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/mx51.h>
#include <mach/devices-common.h>

extern const struct imx_fec_data imx51_fec_data __initconst;
#define imx51_add_fec(pdata)	\
	imx_add_fec(&imx51_fec_data, pdata)

extern const struct imx_imx_i2c_data imx51_imx_i2c_data[] __initconst;
#define imx51_add_imx_i2c(id, pdata)	\
	imx_add_imx_i2c(&imx51_imx_i2c_data[id], pdata)

extern const struct imx_imx_ssi_data imx51_imx_ssi_data[] __initconst;
#define imx51_add_imx_ssi(id, pdata)	\
	imx_add_imx_ssi(&imx51_imx_ssi_data[id], pdata)

extern const struct imx_imx_uart_1irq_data imx51_imx_uart_data[] __initconst;
#define imx51_add_imx_uart(id, pdata)	\
	imx_add_imx_uart_1irq(&imx51_imx_uart_data[id], pdata)

extern const struct imx_mxc_nand_data imx51_mxc_nand_data __initconst;
#define imx51_add_mxc_nand(pdata)	\
	imx_add_mxc_nand(&imx51_mxc_nand_data, pdata)

extern const struct imx_spi_imx_data imx51_cspi_data __initconst;
#define imx51_add_cspi(pdata)	\
	imx_add_spi_imx(&imx51_cspi_data, pdata)

extern const struct imx_spi_imx_data imx51_ecspi_data[] __initconst;
#define imx51_add_ecspi(id, pdata)	\
	imx_add_spi_imx(&imx51_ecspi_data[id], pdata)

#define imx51_add_esdhc0(pdata)	\
	imx_add_esdhc(0, MX51_MMC_SDHC1_BASE_ADDR, SZ_16K, MX51_MXC_INT_MMC_SDHC1, pdata)
#define imx51_add_esdhc1(pdata)	\
	imx_add_esdhc(1, MX51_MMC_SDHC2_BASE_ADDR, SZ_16K, MX51_MXC_INT_MMC_SDHC2, pdata)
#define imx51_add_esdhc2(pdata)	\
	imx_add_esdhc(2, MX51_MMC_SDHC3_BASE_ADDR, SZ_16K, MX51_MXC_INT_MMC_SDHC3, pdata)
#define imx51_add_esdhc3(pdata)	\
	imx_add_esdhc(3, MX51_MMC_SDHC4_BASE_ADDR, SZ_16K, MX51_MXC_INT_MMC_SDHC4, pdata)
