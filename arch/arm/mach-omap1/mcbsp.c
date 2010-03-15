/*
 * linux/arch/arm/mach-omap1/mcbsp.c
 *
 * Copyright (C) 2008 Instituto Nokia de Tecnologia
 * Contact: Eduardo Valentin <eduardo.valentin@indt.org.br>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Multichannel mode not supported.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>

#include <mach/irqs.h>
#include <plat/dma.h>
#include <plat/mux.h>
#include <plat/cpu.h>
#include <plat/mcbsp.h>
#include <plat/dsp_common.h>

#define DPS_RSTCT2_PER_EN	(1 << 0)
#define DSP_RSTCT2_WD_PER_EN	(1 << 1)

static int dsp_use;
static struct clk *api_clk;
static struct clk *dsp_clk;

static void omap1_mcbsp_request(unsigned int id)
{
	/*
	 * On 1510, 1610 and 1710, McBSP1 and McBSP3
	 * are DSP public peripherals.
	 */
	if (id == OMAP_MCBSP1 || id == OMAP_MCBSP3) {
		if (dsp_use++ == 0) {
			api_clk = clk_get(NULL, "api_ck");
			dsp_clk = clk_get(NULL, "dsp_ck");
			if (!IS_ERR(api_clk) && !IS_ERR(dsp_clk)) {
				clk_enable(api_clk);
				clk_enable(dsp_clk);

				omap_dsp_request_mem();
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
	if (id == OMAP_MCBSP1 || id == OMAP_MCBSP3) {
		if (--dsp_use == 0) {
			omap_dsp_release_mem();
			if (!IS_ERR(api_clk)) {
				clk_disable(api_clk);
				clk_put(api_clk);
			}
			if (!IS_ERR(dsp_clk)) {
				clk_disable(dsp_clk);
				clk_put(dsp_clk);
			}
		}
	}
}

static struct omap_mcbsp_ops omap1_mcbsp_ops = {
	.request	= omap1_mcbsp_request,
	.free		= omap1_mcbsp_free,
};

#if defined(CONFIG_ARCH_OMAP730) || defined(CONFIG_ARCH_OMAP850)
static struct omap_mcbsp_platform_data omap7xx_mcbsp_pdata[] = {
	{
		.phys_base	= OMAP7XX_MCBSP1_BASE,
		.dma_rx_sync	= OMAP_DMA_MCBSP1_RX,
		.dma_tx_sync	= OMAP_DMA_MCBSP1_TX,
		.rx_irq		= INT_7XX_McBSP1RX,
		.tx_irq		= INT_7XX_McBSP1TX,
		.ops		= &omap1_mcbsp_ops,
	},
	{
		.phys_base	= OMAP7XX_MCBSP2_BASE,
		.dma_rx_sync	= OMAP_DMA_MCBSP3_RX,
		.dma_tx_sync	= OMAP_DMA_MCBSP3_TX,
		.rx_irq		= INT_7XX_McBSP2RX,
		.tx_irq		= INT_7XX_McBSP2TX,
		.ops		= &omap1_mcbsp_ops,
	},
};
#define OMAP7XX_MCBSP_PDATA_SZ		ARRAY_SIZE(omap7xx_mcbsp_pdata)
#define OMAP7XX_MCBSP_REG_NUM		(OMAP_MCBSP_REG_XCERH / sizeof(u16) + 1)
#else
#define omap7xx_mcbsp_pdata		NULL
#define OMAP7XX_MCBSP_PDATA_SZ		0
#define OMAP7XX_MCBSP_REG_NUM		0
#endif

#ifdef CONFIG_ARCH_OMAP15XX
static struct omap_mcbsp_platform_data omap15xx_mcbsp_pdata[] = {
	{
		.phys_base	= OMAP1510_MCBSP1_BASE,
		.dma_rx_sync	= OMAP_DMA_MCBSP1_RX,
		.dma_tx_sync	= OMAP_DMA_MCBSP1_TX,
		.rx_irq		= INT_McBSP1RX,
		.tx_irq		= INT_McBSP1TX,
		.ops		= &omap1_mcbsp_ops,
	},
	{
		.phys_base	= OMAP1510_MCBSP2_BASE,
		.dma_rx_sync	= OMAP_DMA_MCBSP2_RX,
		.dma_tx_sync	= OMAP_DMA_MCBSP2_TX,
		.rx_irq		= INT_1510_SPI_RX,
		.tx_irq		= INT_1510_SPI_TX,
		.ops		= &omap1_mcbsp_ops,
	},
	{
		.phys_base	= OMAP1510_MCBSP3_BASE,
		.dma_rx_sync	= OMAP_DMA_MCBSP3_RX,
		.dma_tx_sync	= OMAP_DMA_MCBSP3_TX,
		.rx_irq		= INT_McBSP3RX,
		.tx_irq		= INT_McBSP3TX,
		.ops		= &omap1_mcbsp_ops,
	},
};
#define OMAP15XX_MCBSP_PDATA_SZ		ARRAY_SIZE(omap15xx_mcbsp_pdata)
#define OMAP15XX_MCBSP_REG_NUM		(OMAP_MCBSP_REG_XCERH / sizeof(u16) + 1)
#else
#define omap15xx_mcbsp_pdata		NULL
#define OMAP15XX_MCBSP_PDATA_SZ		0
#define OMAP15XX_MCBSP_REG_NUM		0
#endif

#ifdef CONFIG_ARCH_OMAP16XX
static struct omap_mcbsp_platform_data omap16xx_mcbsp_pdata[] = {
	{
		.phys_base	= OMAP1610_MCBSP1_BASE,
		.dma_rx_sync	= OMAP_DMA_MCBSP1_RX,
		.dma_tx_sync	= OMAP_DMA_MCBSP1_TX,
		.rx_irq		= INT_McBSP1RX,
		.tx_irq		= INT_McBSP1TX,
		.ops		= &omap1_mcbsp_ops,
	},
	{
		.phys_base	= OMAP1610_MCBSP2_BASE,
		.dma_rx_sync	= OMAP_DMA_MCBSP2_RX,
		.dma_tx_sync	= OMAP_DMA_MCBSP2_TX,
		.rx_irq		= INT_1610_McBSP2_RX,
		.tx_irq		= INT_1610_McBSP2_TX,
		.ops		= &omap1_mcbsp_ops,
	},
	{
		.phys_base	= OMAP1610_MCBSP3_BASE,
		.dma_rx_sync	= OMAP_DMA_MCBSP3_RX,
		.dma_tx_sync	= OMAP_DMA_MCBSP3_TX,
		.rx_irq		= INT_McBSP3RX,
		.tx_irq		= INT_McBSP3TX,
		.ops		= &omap1_mcbsp_ops,
	},
};
#define OMAP16XX_MCBSP_PDATA_SZ		ARRAY_SIZE(omap16xx_mcbsp_pdata)
#define OMAP16XX_MCBSP_REG_NUM		(OMAP_MCBSP_REG_XCERH / sizeof(u16) + 1)
#else
#define omap16xx_mcbsp_pdata		NULL
#define OMAP16XX_MCBSP_PDATA_SZ		0
#define OMAP16XX_MCBSP_REG_NUM		0
#endif

int __init omap1_mcbsp_init(void)
{
	if (cpu_is_omap7xx()) {
		omap_mcbsp_count = OMAP7XX_MCBSP_PDATA_SZ;
		omap_mcbsp_cache_size = OMAP7XX_MCBSP_REG_NUM * sizeof(u16);
	} else if (cpu_is_omap15xx()) {
		omap_mcbsp_count = OMAP15XX_MCBSP_PDATA_SZ;
		omap_mcbsp_cache_size = OMAP15XX_MCBSP_REG_NUM * sizeof(u16);
	} else if (cpu_is_omap16xx()) {
		omap_mcbsp_count = OMAP16XX_MCBSP_PDATA_SZ;
		omap_mcbsp_cache_size = OMAP16XX_MCBSP_REG_NUM * sizeof(u16);
	}

	mcbsp_ptr = kzalloc(omap_mcbsp_count * sizeof(struct omap_mcbsp *),
								GFP_KERNEL);
	if (!mcbsp_ptr)
		return -ENOMEM;

	if (cpu_is_omap7xx())
		omap_mcbsp_register_board_cfg(omap7xx_mcbsp_pdata,
						OMAP7XX_MCBSP_PDATA_SZ);

	if (cpu_is_omap15xx())
		omap_mcbsp_register_board_cfg(omap15xx_mcbsp_pdata,
						OMAP15XX_MCBSP_PDATA_SZ);

	if (cpu_is_omap16xx())
		omap_mcbsp_register_board_cfg(omap16xx_mcbsp_pdata,
						OMAP16XX_MCBSP_PDATA_SZ);

	return omap_mcbsp_init();
}

arch_initcall(omap1_mcbsp_init);
