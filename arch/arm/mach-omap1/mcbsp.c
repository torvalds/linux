// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm/mach-omap1/mcbsp.c
 *
 * Copyright (C) 2008 Instituto Nokia de Tecnologia
 * Contact: Eduardo Valentin <eduardo.valentin@indt.org.br>
 *
 * Multichannel mode not supported.
 */
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/omap-dma.h>
#include <linux/soc/ti/omap1-io.h>
#include <linux/platform_data/asoc-ti-mcbsp.h>

#include "mux.h"
#include "soc.h"
#include "irqs.h"
#include "iomap.h"

#define DPS_RSTCT2_PER_EN	(1 << 0)
#define DSP_RSTCT2_WD_PER_EN	(1 << 1)

static int dsp_use;
static struct clk *api_clk;
static struct clk *dsp_clk;
static struct platform_device **omap_mcbsp_devices;

static void omap1_mcbsp_request(unsigned int id)
{
	/*
	 * On 1510, 1610 and 1710, McBSP1 and McBSP3
	 * are DSP public peripherals.
	 */
	if (id == 0 || id == 2) {
		if (dsp_use++ == 0) {
			api_clk = clk_get(NULL, "api_ck");
			dsp_clk = clk_get(NULL, "dsp_ck");
			if (!IS_ERR(api_clk) && !IS_ERR(dsp_clk)) {
				clk_prepare_enable(api_clk);
				clk_prepare_enable(dsp_clk);

				/*
				 * DSP external peripheral reset
				 * FIXME: This should be moved to dsp code
				 */
				__raw_writew(__raw_readw(DSP_RSTCT2) | DPS_RSTCT2_PER_EN |
						DSP_RSTCT2_WD_PER_EN, DSP_RSTCT2);
			}
		}
	}
}

static void omap1_mcbsp_free(unsigned int id)
{
	if (id == 0 || id == 2) {
		if (--dsp_use == 0) {
			if (!IS_ERR(api_clk)) {
				clk_disable_unprepare(api_clk);
				clk_put(api_clk);
			}
			if (!IS_ERR(dsp_clk)) {
				clk_disable_unprepare(dsp_clk);
				clk_put(dsp_clk);
			}
		}
	}
}

static struct omap_mcbsp_ops omap1_mcbsp_ops = {
	.request	= omap1_mcbsp_request,
	.free		= omap1_mcbsp_free,
};

#define OMAP7XX_MCBSP1_BASE	0xfffb1000
#define OMAP7XX_MCBSP2_BASE	0xfffb1800

#define OMAP1510_MCBSP1_BASE	0xe1011800
#define OMAP1510_MCBSP2_BASE	0xfffb1000
#define OMAP1510_MCBSP3_BASE	0xe1017000

#define OMAP1610_MCBSP1_BASE	0xe1011800
#define OMAP1610_MCBSP2_BASE	0xfffb1000
#define OMAP1610_MCBSP3_BASE	0xe1017000

struct resource omap15xx_mcbsp_res[][6] = {
	{
		{
			.start = OMAP1510_MCBSP1_BASE,
			.end   = OMAP1510_MCBSP1_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_McBSP1RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_McBSP1TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = 9,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = 8,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.start = OMAP1510_MCBSP2_BASE,
			.end   = OMAP1510_MCBSP2_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_1510_SPI_RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_1510_SPI_TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = 17,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = 16,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.start = OMAP1510_MCBSP3_BASE,
			.end   = OMAP1510_MCBSP3_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_McBSP3RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_McBSP3TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = 11,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = 10,
			.flags = IORESOURCE_DMA,
		},
	},
};

#define omap15xx_mcbsp_res_0		omap15xx_mcbsp_res[0]

static struct omap_mcbsp_platform_data omap15xx_mcbsp_pdata[] = {
	{
		.ops		= &omap1_mcbsp_ops,
	},
	{
		.ops		= &omap1_mcbsp_ops,
	},
	{
		.ops		= &omap1_mcbsp_ops,
	},
};
#define OMAP15XX_MCBSP_RES_SZ		ARRAY_SIZE(omap15xx_mcbsp_res[1])
#define OMAP15XX_MCBSP_COUNT		ARRAY_SIZE(omap15xx_mcbsp_res)

struct resource omap16xx_mcbsp_res[][6] = {
	{
		{
			.start = OMAP1610_MCBSP1_BASE,
			.end   = OMAP1610_MCBSP1_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_McBSP1RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_McBSP1TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = 9,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = 8,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.start = OMAP1610_MCBSP2_BASE,
			.end   = OMAP1610_MCBSP2_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_1610_McBSP2_RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_1610_McBSP2_TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = 17,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = 16,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.start = OMAP1610_MCBSP3_BASE,
			.end   = OMAP1610_MCBSP3_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_McBSP3RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_McBSP3TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = 11,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = 10,
			.flags = IORESOURCE_DMA,
		},
	},
};

#define omap16xx_mcbsp_res_0		omap16xx_mcbsp_res[0]

static struct omap_mcbsp_platform_data omap16xx_mcbsp_pdata[] = {
	{
		.ops		= &omap1_mcbsp_ops,
	},
	{
		.ops		= &omap1_mcbsp_ops,
	},
	{
		.ops		= &omap1_mcbsp_ops,
	},
};
#define OMAP16XX_MCBSP_RES_SZ		ARRAY_SIZE(omap16xx_mcbsp_res[1])
#define OMAP16XX_MCBSP_COUNT		ARRAY_SIZE(omap16xx_mcbsp_res)

static void omap_mcbsp_register_board_cfg(struct resource *res, int res_count,
			struct omap_mcbsp_platform_data *config, int size)
{
	int i;

	omap_mcbsp_devices = kcalloc(size, sizeof(struct platform_device *),
				     GFP_KERNEL);
	if (!omap_mcbsp_devices) {
		printk(KERN_ERR "Could not register McBSP devices\n");
		return;
	}

	for (i = 0; i < size; i++) {
		struct platform_device *new_mcbsp;
		int ret;

		new_mcbsp = platform_device_alloc("omap-mcbsp", i + 1);
		if (!new_mcbsp)
			continue;
		platform_device_add_resources(new_mcbsp, &res[i * res_count],
					res_count);
		config[i].reg_size = 2;
		config[i].reg_step = 2;
		new_mcbsp->dev.platform_data = &config[i];
		ret = platform_device_add(new_mcbsp);
		if (ret) {
			platform_device_put(new_mcbsp);
			continue;
		}
		omap_mcbsp_devices[i] = new_mcbsp;
	}
}

static int __init omap1_mcbsp_init(void)
{
	if (!cpu_class_is_omap1())
		return -ENODEV;

	if (cpu_is_omap15xx())
		omap_mcbsp_register_board_cfg(omap15xx_mcbsp_res_0,
					OMAP15XX_MCBSP_RES_SZ,
					omap15xx_mcbsp_pdata,
					OMAP15XX_MCBSP_COUNT);

	if (cpu_is_omap16xx())
		omap_mcbsp_register_board_cfg(omap16xx_mcbsp_res_0,
					OMAP16XX_MCBSP_RES_SZ,
					omap16xx_mcbsp_pdata,
					OMAP16XX_MCBSP_COUNT);

	return 0;
}

arch_initcall(omap1_mcbsp_init);
