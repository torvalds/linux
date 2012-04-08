/*
 * Copyright (C) 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <asm/sizes.h>
#include <mach/hardware.h>
#include <mach/devices-common.h>

#define imx_ahci_imx_data_entry_single(soc, _devid)		\
	{								\
		.devid = _devid,					\
		.iobase = soc ## _SATA_BASE_ADDR,			\
		.irq = soc ## _INT_SATA,				\
	}

#ifdef CONFIG_SOC_IMX53
const struct imx_ahci_imx_data imx53_ahci_imx_data __initconst =
	imx_ahci_imx_data_entry_single(MX53, "imx53-ahci");
#endif

enum {
	HOST_CAP = 0x00,
	HOST_CAP_SSS = (1 << 27), /* Staggered Spin-up */
	HOST_PORTS_IMPL	= 0x0c,
	HOST_TIMER1MS = 0xe0, /* Timer 1-ms */
};

static struct clk *sata_clk, *sata_ref_clk;

/* AHCI module Initialization, if return 0, initialization is successful. */
static int imx_sata_init(struct device *dev, void __iomem *addr)
{
	u32 tmpdata;
	int ret = 0;
	struct clk *clk;

	sata_clk = clk_get(dev, "ahci");
	if (IS_ERR(sata_clk)) {
		dev_err(dev, "no sata clock.\n");
		return PTR_ERR(sata_clk);
	}
	ret = clk_prepare_enable(sata_clk);
	if (ret) {
		dev_err(dev, "can't prepare/enable sata clock.\n");
		goto put_sata_clk;
	}

	/* Get the AHCI SATA PHY CLK */
	sata_ref_clk = clk_get(dev, "ahci_phy");
	if (IS_ERR(sata_ref_clk)) {
		dev_err(dev, "no sata ref clock.\n");
		ret = PTR_ERR(sata_ref_clk);
		goto release_sata_clk;
	}
	ret = clk_prepare_enable(sata_ref_clk);
	if (ret) {
		dev_err(dev, "can't prepare/enable sata ref clock.\n");
		goto put_sata_ref_clk;
	}

	/* Get the AHB clock rate, and configure the TIMER1MS reg later */
	clk = clk_get(dev, "ahci_dma");
	if (IS_ERR(clk)) {
		dev_err(dev, "no dma clock.\n");
		ret = PTR_ERR(clk);
		goto release_sata_ref_clk;
	}
	tmpdata = clk_get_rate(clk) / 1000;
	clk_put(clk);

	writel(tmpdata, addr + HOST_TIMER1MS);

	tmpdata = readl(addr + HOST_CAP);
	if (!(tmpdata & HOST_CAP_SSS)) {
		tmpdata |= HOST_CAP_SSS;
		writel(tmpdata, addr + HOST_CAP);
	}

	if (!(readl(addr + HOST_PORTS_IMPL) & 0x1))
		writel((readl(addr + HOST_PORTS_IMPL) | 0x1),
			addr + HOST_PORTS_IMPL);

	return 0;

release_sata_ref_clk:
	clk_disable_unprepare(sata_ref_clk);
put_sata_ref_clk:
	clk_put(sata_ref_clk);
release_sata_clk:
	clk_disable_unprepare(sata_clk);
put_sata_clk:
	clk_put(sata_clk);

	return ret;
}

static void imx_sata_exit(struct device *dev)
{
	clk_disable_unprepare(sata_ref_clk);
	clk_put(sata_ref_clk);

	clk_disable_unprepare(sata_clk);
	clk_put(sata_clk);

}
struct platform_device *__init imx_add_ahci_imx(
		const struct imx_ahci_imx_data *data,
		const struct ahci_platform_data *pdata)
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

	return imx_add_platform_device_dmamask(data->devid, 0,
			res, ARRAY_SIZE(res),
			pdata, sizeof(*pdata),  DMA_BIT_MASK(32));
}

struct platform_device *__init imx53_add_ahci_imx(void)
{
	struct ahci_platform_data pdata = {
		.init = imx_sata_init,
		.exit = imx_sata_exit,
	};

	return imx_add_ahci_imx(&imx53_ahci_imx_data, &pdata);
}
