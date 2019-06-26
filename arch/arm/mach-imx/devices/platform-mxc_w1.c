// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 */
#include "../hardware.h"
#include "devices-common.h"

#define imx_mxc_w1_data_entry_single(soc)				\
	{								\
		.iobase = soc ## _OWIRE_BASE_ADDR,			\
	}

#ifdef CONFIG_SOC_IMX21
const struct imx_mxc_w1_data imx21_mxc_w1_data __initconst =
	imx_mxc_w1_data_entry_single(MX21);
#endif /* ifdef CONFIG_SOC_IMX21 */

#ifdef CONFIG_SOC_IMX27
const struct imx_mxc_w1_data imx27_mxc_w1_data __initconst =
	imx_mxc_w1_data_entry_single(MX27);
#endif /* ifdef CONFIG_SOC_IMX27 */

#ifdef CONFIG_SOC_IMX31
const struct imx_mxc_w1_data imx31_mxc_w1_data __initconst =
	imx_mxc_w1_data_entry_single(MX31);
#endif /* ifdef CONFIG_SOC_IMX31 */

#ifdef CONFIG_SOC_IMX35
const struct imx_mxc_w1_data imx35_mxc_w1_data __initconst =
	imx_mxc_w1_data_entry_single(MX35);
#endif /* ifdef CONFIG_SOC_IMX35 */

struct platform_device *__init imx_add_mxc_w1(
		const struct imx_mxc_w1_data *data)
{
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + SZ_4K - 1,
			.flags = IORESOURCE_MEM,
		},
	};

	return imx_add_platform_device("mxc_w1", 0,
			res, ARRAY_SIZE(res), NULL, 0);
}
