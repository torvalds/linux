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

extern const struct imx_imx_i2c_data imx51_imx_i2c_data[] __initconst;
#define imx51_add_imx_i2c(id, pdata)	\
	imx_add_imx_i2c(&imx51_imx_i2c_data[id], pdata)

extern const struct imx_imx_uart_1irq_data imx51_imx_uart_data[] __initconst;
#define imx51_add_imx_uart(id, pdata)	\
	imx_add_imx_uart_1irq(&imx51_imx_uart_data[id], pdata)

extern const struct imx_spi_imx_data imx51_cspi_data __initconst;
#define imx51_add_cspi(pdata)	\
	imx_add_spi_imx(&imx51_cspi_data, pdata)

extern const struct imx_spi_imx_data imx51_ecspi_data[] __initconst;
#define imx51_add_ecspi(id, pdata)	\
	imx_add_spi_imx(&imx51_ecspi_data[id], pdata)
