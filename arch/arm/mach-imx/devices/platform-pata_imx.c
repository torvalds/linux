// SPDX-License-Identifier: GPL-2.0-only
#include "../hardware.h"
#include "devices-common.h"

#define imx_pata_imx_data_entry_single(soc, _size)			\
	{								\
		.iobase = soc ## _ATA_BASE_ADDR,			\
		.iosize = _size,					\
		.irq = soc ## _INT_ATA,					\
	}

#ifdef CONFIG_SOC_IMX27
const struct imx_pata_imx_data imx27_pata_imx_data __initconst =
	imx_pata_imx_data_entry_single(MX27, SZ_4K);
#endif /* ifdef CONFIG_SOC_IMX27 */

#ifdef CONFIG_SOC_IMX31
const struct imx_pata_imx_data imx31_pata_imx_data __initconst =
	imx_pata_imx_data_entry_single(MX31, SZ_16K);
#endif /* ifdef CONFIG_SOC_IMX31 */

#ifdef CONFIG_SOC_IMX35
const struct imx_pata_imx_data imx35_pata_imx_data __initconst =
	imx_pata_imx_data_entry_single(MX35, SZ_16K);
#endif /* ifdef CONFIG_SOC_IMX35 */

struct platform_device *__init imx_add_pata_imx(
		const struct imx_pata_imx_data *data)
{
	struct resource res[] = {
		{
			.start = data->iobase,
			.end = data->iobase + data->iosize - 1,
			.flags = IORESOURCE_MEM,
		},
		{
			.start = data->irq,
			.end = data->irq,
			.flags = IORESOURCE_IRQ,
		},
	};
	return imx_add_platform_device("pata_imx", -1,
			res, ARRAY_SIZE(res), NULL, 0);
}

