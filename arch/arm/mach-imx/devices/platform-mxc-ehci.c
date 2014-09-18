/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <linux/dma-mapping.h>

#include "../hardware.h"
#include "devices-common.h"

#define imx_mxc_ehci_data_entry_single(soc, _id, hs)			\
	{								\
		.id = _id,						\
		.iobase = soc ## _USB_ ## hs ## _BASE_ADDR,		\
		.irq = soc ## _INT_USB_ ## hs,				\
	}

#ifdef CONFIG_SOC_IMX25
const struct imx_mxc_ehci_data imx25_mxc_ehci_otg_data __initconst =
	imx_mxc_ehci_data_entry_single(MX25, 0, OTG);
const struct imx_mxc_ehci_data imx25_mxc_ehci_hs_data __initconst =
	imx_mxc_ehci_data_entry_single(MX25, 1, HS);
#endif /* ifdef CONFIG_SOC_IMX25 */

#ifdef CONFIG_SOC_IMX27
const struct imx_mxc_ehci_data imx27_mxc_ehci_otg_data __initconst =
	imx_mxc_ehci_data_entry_single(MX27, 0, OTG);
const struct imx_mxc_ehci_data imx27_mxc_ehci_hs_data[] __initconst = {
	imx_mxc_ehci_data_entry_single(MX27, 1, HS1),
	imx_mxc_ehci_data_entry_single(MX27, 2, HS2),
};
#endif /* ifdef CONFIG_SOC_IMX27 */

#ifdef CONFIG_SOC_IMX31
const struct imx_mxc_ehci_data imx31_mxc_ehci_otg_data __initconst =
	imx_mxc_ehci_data_entry_single(MX31, 0, OTG);
const struct imx_mxc_ehci_data imx31_mxc_ehci_hs_data[] __initconst = {
	imx_mxc_ehci_data_entry_single(MX31, 1, HS1),
	imx_mxc_ehci_data_entry_single(MX31, 2, HS2),
};
#endif /* ifdef CONFIG_SOC_IMX31 */

#ifdef CONFIG_SOC_IMX35
const struct imx_mxc_ehci_data imx35_mxc_ehci_otg_data __initconst =
	imx_mxc_ehci_data_entry_single(MX35, 0, OTG);
const struct imx_mxc_ehci_data imx35_mxc_ehci_hs_data __initconst =
	imx_mxc_ehci_data_entry_single(MX35, 1, HS);
#endif /* ifdef CONFIG_SOC_IMX35 */

struct platform_device *__init imx_add_mxc_ehci(
		const struct imx_mxc_ehci_data *data,
		const struct mxc_usbh_platform_data *pdata)
{
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + SZ_512 - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = data->irq,
			.end = data->irq,
			.flags = IORESOURCE_IRQ,
		},
	};
	return imx_add_platform_device_dmamask("mxc-ehci", data->id,
			res, ARRAY_SIZE(res),
			pdata, sizeof(*pdata), DMA_BIT_MASK(32));
}
