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

#include <mach/dma.h>
#include <mach/mux.h>
#include <mach/cpu.h>
#include <mach/mcbsp.h>
#include <mach/dsp_common.h>

#define DPS_RSTCT2_PER_EN	(1 << 0)
#define DSP_RSTCT2_WD_PER_EN	(1 << 1)

struct mcbsp_internal_clk {
	struct clk clk;
	struct clk **childs;
	int n_childs;
};

#if defined(CONFIG_ARCH_OMAP15XX) || defined(CONFIG_ARCH_OMAP16XX)
static void omap_mcbsp_clk_init(struct mcbsp_internal_clk *mclk)
{
	const char *clk_names[] = { "dsp_ck", "api_ck", "dspxor_ck" };
	int i;

	mclk->n_childs = ARRAY_SIZE(clk_names);
	mclk->childs = kzalloc(mclk->n_childs * sizeof(struct clk *),
				GFP_KERNEL);

	for (i = 0; i < mclk->n_childs; i++) {
		/* We fake a platform device to get correct device id */
		struct platform_device pdev;

		pdev.dev.bus = &platform_bus_type;
		pdev.id = mclk->clk.id;
		mclk->childs[i] = clk_get(&pdev.dev, clk_names[i]);
		if (IS_ERR(mclk->childs[i]))
			printk(KERN_ERR "Could not get clock %s (%d).\n",
				clk_names[i], mclk->clk.id);
	}
}

static int omap_mcbsp_clk_enable(struct clk *clk)
{
	struct mcbsp_internal_clk *mclk = container_of(clk,
					struct mcbsp_internal_clk, clk);
	int i;

	for (i = 0; i < mclk->n_childs; i++)
		clk_enable(mclk->childs[i]);
	return 0;
}

static void omap_mcbsp_clk_disable(struct clk *clk)
{
	struct mcbsp_internal_clk *mclk = container_of(clk,
					struct mcbsp_internal_clk, clk);
	int i;

	for (i = 0; i < mclk->n_childs; i++)
		clk_disable(mclk->childs[i]);
}

static struct mcbsp_internal_clk omap_mcbsp_clks[] = {
	{
		.clk = {
			.name 		= "mcbsp_clk",
			.id		= 1,
			.enable		= omap_mcbsp_clk_enable,
			.disable	= omap_mcbsp_clk_disable,
		},
	},
	{
		.clk = {
			.name 		= "mcbsp_clk",
			.id		= 3,
			.enable		= omap_mcbsp_clk_enable,
			.disable	= omap_mcbsp_clk_disable,
		},
	},
};

#define omap_mcbsp_clks_size	ARRAY_SIZE(omap_mcbsp_clks)
#else
#define omap_mcbsp_clks_size	0
static struct mcbsp_internal_clk __initdata *omap_mcbsp_clks;
static inline void omap_mcbsp_clk_init(struct mcbsp_internal_clk *mclk)
{ }
#endif

static int omap1_mcbsp_check(unsigned int id)
{
	/* REVISIT: Check correctly for number of registered McBSPs */
	if (cpu_is_omap730()) {
		if (id > OMAP_MAX_MCBSP_COUNT - 2) {
		       printk(KERN_ERR "OMAP-McBSP: McBSP%d doesn't exist\n",
				id + 1);
		       return -ENODEV;
		}
		return 0;
	}

	if (cpu_is_omap15xx() || cpu_is_omap16xx()) {
		if (id > OMAP_MAX_MCBSP_COUNT - 1) {
			printk(KERN_ERR "OMAP-McBSP: McBSP%d doesn't exist\n",
				id + 1);
			return -ENODEV;
		}
		return 0;
	}

	return -ENODEV;
}

static void omap1_mcbsp_request(unsigned int id)
{
	/*
	 * On 1510, 1610 and 1710, McBSP1 and McBSP3
	 * are DSP public peripherals.
	 */
	if (id == OMAP_MCBSP1 || id == OMAP_MCBSP3) {
		omap_dsp_request_mem();
		/*
		 * DSP external peripheral reset
		 * FIXME: This should be moved to dsp code
		 */
		__raw_writew(__raw_readw(DSP_RSTCT2) | DPS_RSTCT2_PER_EN |
				DSP_RSTCT2_WD_PER_EN, DSP_RSTCT2);
	}
}

static void omap1_mcbsp_free(unsigned int id)
{
	if (id == OMAP_MCBSP1 || id == OMAP_MCBSP3)
		omap_dsp_release_mem();
}

static struct omap_mcbsp_ops omap1_mcbsp_ops = {
	.check		= omap1_mcbsp_check,
	.request	= omap1_mcbsp_request,
	.free		= omap1_mcbsp_free,
};

#ifdef CONFIG_ARCH_OMAP730
static struct omap_mcbsp_platform_data omap730_mcbsp_pdata[] = {
	{
		.phys_base	= OMAP730_MCBSP1_BASE,
		.dma_rx_sync	= OMAP_DMA_MCBSP1_RX,
		.dma_tx_sync	= OMAP_DMA_MCBSP1_TX,
		.rx_irq		= INT_730_McBSP1RX,
		.tx_irq		= INT_730_McBSP1TX,
		.ops		= &omap1_mcbsp_ops,
	},
	{
		.phys_base	= OMAP730_MCBSP2_BASE,
		.dma_rx_sync	= OMAP_DMA_MCBSP3_RX,
		.dma_tx_sync	= OMAP_DMA_MCBSP3_TX,
		.rx_irq		= INT_730_McBSP2RX,
		.tx_irq		= INT_730_McBSP2TX,
		.ops		= &omap1_mcbsp_ops,
	},
};
#define OMAP730_MCBSP_PDATA_SZ		ARRAY_SIZE(omap730_mcbsp_pdata)
#else
#define omap730_mcbsp_pdata		NULL
#define OMAP730_MCBSP_PDATA_SZ		0
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
		.clk_name	= "mcbsp_clk",
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
		.clk_name	= "mcbsp_clk",
	},
};
#define OMAP15XX_MCBSP_PDATA_SZ		ARRAY_SIZE(omap15xx_mcbsp_pdata)
#else
#define omap15xx_mcbsp_pdata		NULL
#define OMAP15XX_MCBSP_PDATA_SZ		0
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
		.clk_name	= "mcbsp_clk",
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
		.clk_name	= "mcbsp_clk",
	},
};
#define OMAP16XX_MCBSP_PDATA_SZ		ARRAY_SIZE(omap16xx_mcbsp_pdata)
#else
#define omap16xx_mcbsp_pdata		NULL
#define OMAP16XX_MCBSP_PDATA_SZ		0
#endif

int __init omap1_mcbsp_init(void)
{
	int i;

	for (i = 0; i < omap_mcbsp_clks_size; i++) {
		if (cpu_is_omap15xx() || cpu_is_omap16xx()) {
			omap_mcbsp_clk_init(&omap_mcbsp_clks[i]);
			clk_register(&omap_mcbsp_clks[i].clk);
		}
	}

	if (cpu_is_omap730())
		omap_mcbsp_register_board_cfg(omap730_mcbsp_pdata,
						OMAP730_MCBSP_PDATA_SZ);

	if (cpu_is_omap15xx())
		omap_mcbsp_register_board_cfg(omap15xx_mcbsp_pdata,
						OMAP15XX_MCBSP_PDATA_SZ);

	if (cpu_is_omap16xx())
		omap_mcbsp_register_board_cfg(omap16xx_mcbsp_pdata,
						OMAP16XX_MCBSP_PDATA_SZ);

	return omap_mcbsp_init();
}

arch_initcall(omap1_mcbsp_init);
