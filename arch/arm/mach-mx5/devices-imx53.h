/*
 * Copyright (C) 2010 Yong Shen. <Yong.Shen@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/mx53.h>
#include <mach/devices-common.h>

extern const struct imx_fec_data imx53_fec_data __initconst;
#define imx53_add_fec(pdata)   \
	imx_add_fec(&imx53_fec_data, pdata)

extern const struct imx_imx_uart_1irq_data imx53_imx_uart_data[] __initconst;
#define imx53_add_imx_uart(id, pdata)	\
	imx_add_imx_uart_1irq(&imx53_imx_uart_data[id], pdata)


extern const struct imx_imx_i2c_data imx53_imx_i2c_data[] __initconst;
#define imx53_add_imx_i2c(id, pdata)	\
	imx_add_imx_i2c(&imx53_imx_i2c_data[id], pdata)
