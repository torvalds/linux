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

#define imx_mxc_pwm_data_entry_single(soc)				\
	{								\
		.iobase = soc ## _PWM_BASE_ADDR,			\
		.irq = soc ## _INT_PWM,					\
	}

#ifdef CONFIG_SOC_IMX21
const struct imx_mxc_pwm_data imx21_mxc_pwm_data __initconst =
	imx_mxc_pwm_data_entry_single(MX21);
#endif /* ifdef CONFIG_SOC_IMX21 */

#ifdef CONFIG_SOC_IMX27
const struct imx_mxc_pwm_data imx27_mxc_pwm_data __initconst =
	imx_mxc_pwm_data_entry_single(MX27);
#endif /* ifdef CONFIG_SOC_IMX27 */

struct platform_device *__init imx_add_mxc_pwm(
		const struct imx_mxc_pwm_data *data)
{
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + SZ_4K - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = data->irq,
			.end = data->irq,
			.flags = IORESOURCE_IRQ,
		},
	};

	return imx_add_platform_device("mxc_pwm", 0,
			res, ARRAY_SIZE(res), NULL, 0);
}
