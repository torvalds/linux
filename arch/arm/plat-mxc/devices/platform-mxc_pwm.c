/*
 * Copyright (C) 2009-2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/hardware.h>
#include <mach/devices-common.h>

#define imx_mxc_pwm_data_entry_single(soc, _id, _hwid, _size)		\
	{								\
		.id = _id,						\
		.iobase = soc ## _PWM ## _hwid ## _BASE_ADDR,		\
		.iosize = _size,					\
		.irq = soc ## _INT_PWM ## _hwid,			\
	}
#define imx_mxc_pwm_data_entry(soc, _id, _hwid, _size)			\
	[_id] = imx_mxc_pwm_data_entry_single(soc, _id, _hwid, _size)

#ifdef CONFIG_SOC_IMX21
const struct imx_mxc_pwm_data imx21_mxc_pwm_data __initconst =
	imx_mxc_pwm_data_entry_single(MX21, 0, , SZ_4K);
#endif /* ifdef CONFIG_SOC_IMX21 */

#ifdef CONFIG_SOC_IMX25
const struct imx_mxc_pwm_data imx25_mxc_pwm_data[] __initconst = {
#define imx25_mxc_pwm_data_entry(_id, _hwid)				\
	imx_mxc_pwm_data_entry(MX25, _id, _hwid, SZ_16K)
	imx25_mxc_pwm_data_entry(0, 1),
	imx25_mxc_pwm_data_entry(1, 2),
	imx25_mxc_pwm_data_entry(2, 3),
	imx25_mxc_pwm_data_entry(3, 4),
};
#endif /* ifdef CONFIG_SOC_IMX25 */

#ifdef CONFIG_SOC_IMX27
const struct imx_mxc_pwm_data imx27_mxc_pwm_data __initconst =
	imx_mxc_pwm_data_entry_single(MX27, 0, , SZ_4K);
#endif /* ifdef CONFIG_SOC_IMX27 */

#ifdef CONFIG_SOC_IMX51
const struct imx_mxc_pwm_data imx51_mxc_pwm_data[] __initconst = {
#define imx51_mxc_pwm_data_entry(_id, _hwid)				\
	imx_mxc_pwm_data_entry(MX51, _id, _hwid, SZ_16K)
	imx51_mxc_pwm_data_entry(0, 1),
	imx51_mxc_pwm_data_entry(1, 2),
};
#endif /* ifdef CONFIG_SOC_IMX51 */

struct platform_device *__init imx_add_mxc_pwm(
		const struct imx_mxc_pwm_data *data)
{
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + data->iosize - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = data->irq,
			.end = data->irq,
			.flags = IORESOURCE_IRQ,
		},
	};

	return imx_add_platform_device("mxc_pwm", data->id,
			res, ARRAY_SIZE(res), NULL, 0);
}
