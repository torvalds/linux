/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include "../hardware.h"
#include "devices-common.h"

#define imx_imx_keypad_data_entry_single(soc, _size)			\
	{								\
		.iobase = soc ## _KPP_BASE_ADDR,			\
		.iosize = _size,					\
		.irq = soc ## _INT_KPP,					\
	}

#ifdef CONFIG_SOC_IMX21
const struct imx_imx_keypad_data imx21_imx_keypad_data __initconst =
	imx_imx_keypad_data_entry_single(MX21, SZ_16);
#endif /* ifdef CONFIG_SOC_IMX21 */

#ifdef CONFIG_SOC_IMX25
const struct imx_imx_keypad_data imx25_imx_keypad_data __initconst =
	imx_imx_keypad_data_entry_single(MX25, SZ_16K);
#endif /* ifdef CONFIG_SOC_IMX25 */

#ifdef CONFIG_SOC_IMX27
const struct imx_imx_keypad_data imx27_imx_keypad_data __initconst =
	imx_imx_keypad_data_entry_single(MX27, SZ_16);
#endif /* ifdef CONFIG_SOC_IMX27 */

#ifdef CONFIG_SOC_IMX31
const struct imx_imx_keypad_data imx31_imx_keypad_data __initconst =
	imx_imx_keypad_data_entry_single(MX31, SZ_16);
#endif /* ifdef CONFIG_SOC_IMX31 */

#ifdef CONFIG_SOC_IMX35
const struct imx_imx_keypad_data imx35_imx_keypad_data __initconst =
	imx_imx_keypad_data_entry_single(MX35, SZ_16);
#endif /* ifdef CONFIG_SOC_IMX35 */

#ifdef CONFIG_SOC_IMX51
const struct imx_imx_keypad_data imx51_imx_keypad_data __initconst =
	imx_imx_keypad_data_entry_single(MX51, SZ_16);
#endif /* ifdef CONFIG_SOC_IMX51 */

#ifdef CONFIG_SOC_IMX53
const struct imx_imx_keypad_data imx53_imx_keypad_data __initconst =
	imx_imx_keypad_data_entry_single(MX53, SZ_16);
#endif /* ifdef CONFIG_SOC_IMX53 */

struct platform_device *__init imx_add_imx_keypad(
		const struct imx_imx_keypad_data *data,
		const struct matrix_keymap_data *pdata)
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

	return imx_add_platform_device("imx-keypad", -1,
			res, ARRAY_SIZE(res), pdata, sizeof(*pdata));
}
