/*
 * linux/arch/arm/mach-omap2/mcbsp.c
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

struct mcbsp_internal_clk {
	struct clk clk;
	struct clk **childs;
	int n_childs;
};

#if defined(CONFIG_ARCH_OMAP24XX) || defined(CONFIG_ARCH_OMAP34XX)
static void omap_mcbsp_clk_init(struct mcbsp_internal_clk *mclk)
{
	const char *clk_names[] = { "mcbsp_ick", "mcbsp_fck" };
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
			.id		= 2,
			.enable		= omap_mcbsp_clk_enable,
			.disable	= omap_mcbsp_clk_disable,
		},
	},
	{
		.clk = {
			.name		= "mcbsp_clk",
			.id		= 3,
			.enable		= omap_mcbsp_clk_enable,
			.disable	= omap_mcbsp_clk_disable,
		},
	},
	{
		.clk = {
			.name		= "mcbsp_clk",
			.id		= 4,
			.enable		= omap_mcbsp_clk_enable,
			.disable	= omap_mcbsp_clk_disable,
		},
	},
	{
		.clk = {
			.name		= "mcbsp_clk",
			.id		= 5,
			.enable		= omap_mcbsp_clk_enable,
			.disable	= omap_mcbsp_clk_disable,
		},
	},
};

#define omap_mcbsp_clks_size	ARRAY_SIZE(omap_mcbsp_clks)
#else
#define omap_mcbsp_clks_size	0
static struct mcbsp_internal_clk __initdata *omap_mcbsp_clks;
static inline void omap_mcbsp_clk_init(struct clk *clk)
{ }
#endif

static void omap2_mcbsp2_mux_setup(void)
{
	omap_cfg_reg(Y15_24XX_MCBSP2_CLKX);
	omap_cfg_reg(R14_24XX_MCBSP2_FSX);
	omap_cfg_reg(W15_24XX_MCBSP2_DR);
	omap_cfg_reg(V15_24XX_MCBSP2_DX);
	omap_cfg_reg(V14_24XX_GPIO117);
	/*
	 * TODO: Need to add MUX settings for OMAP 2430 SDP
	 */
}

static void omap2_mcbsp_request(unsigned int id)
{
	if (cpu_is_omap2420() && (id == OMAP_MCBSP2))
		omap2_mcbsp2_mux_setup();
}

static struct omap_mcbsp_ops omap2_mcbsp_ops = {
	.request	= omap2_mcbsp_request,
};

#ifdef CONFIG_ARCH_OMAP2420
static struct omap_mcbsp_platform_data omap2420_mcbsp_pdata[] = {
	{
		.phys_base	= OMAP24XX_MCBSP1_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP1_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP1_TX,
		.rx_irq		= INT_24XX_MCBSP1_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP1_IRQ_TX,
		.ops		= &omap2_mcbsp_ops,
		.clk_name	= "mcbsp_clk",
	},
	{
		.phys_base	= OMAP24XX_MCBSP2_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP2_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP2_TX,
		.rx_irq		= INT_24XX_MCBSP2_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP2_IRQ_TX,
		.ops		= &omap2_mcbsp_ops,
		.clk_name	= "mcbsp_clk",
	},
};
#define OMAP2420_MCBSP_PDATA_SZ		ARRAY_SIZE(omap2420_mcbsp_pdata)
#else
#define omap2420_mcbsp_pdata		NULL
#define OMAP2420_MCBSP_PDATA_SZ		0
#endif

#ifdef CONFIG_ARCH_OMAP2430
static struct omap_mcbsp_platform_data omap2430_mcbsp_pdata[] = {
	{
		.phys_base	= OMAP24XX_MCBSP1_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP1_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP1_TX,
		.rx_irq		= INT_24XX_MCBSP1_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP1_IRQ_TX,
		.ops		= &omap2_mcbsp_ops,
		.clk_name	= "mcbsp_clk",
	},
	{
		.phys_base	= OMAP24XX_MCBSP2_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP2_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP2_TX,
		.rx_irq		= INT_24XX_MCBSP2_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP2_IRQ_TX,
		.ops		= &omap2_mcbsp_ops,
		.clk_name	= "mcbsp_clk",
	},
	{
		.phys_base	= OMAP2430_MCBSP3_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP3_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP3_TX,
		.rx_irq		= INT_24XX_MCBSP3_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP3_IRQ_TX,
		.ops		= &omap2_mcbsp_ops,
		.clk_name	= "mcbsp_clk",
	},
	{
		.phys_base	= OMAP2430_MCBSP4_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP4_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP4_TX,
		.rx_irq		= INT_24XX_MCBSP4_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP4_IRQ_TX,
		.ops		= &omap2_mcbsp_ops,
		.clk_name	= "mcbsp_clk",
	},
	{
		.phys_base	= OMAP2430_MCBSP5_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP5_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP5_TX,
		.rx_irq		= INT_24XX_MCBSP5_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP5_IRQ_TX,
		.ops		= &omap2_mcbsp_ops,
		.clk_name	= "mcbsp_clk",
	},
};
#define OMAP2430_MCBSP_PDATA_SZ		ARRAY_SIZE(omap2430_mcbsp_pdata)
#else
#define omap2430_mcbsp_pdata		NULL
#define OMAP2430_MCBSP_PDATA_SZ		0
#endif

#ifdef CONFIG_ARCH_OMAP34XX
static struct omap_mcbsp_platform_data omap34xx_mcbsp_pdata[] = {
	{
		.phys_base	= OMAP34XX_MCBSP1_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP1_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP1_TX,
		.rx_irq		= INT_24XX_MCBSP1_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP1_IRQ_TX,
		.ops		= &omap2_mcbsp_ops,
		.clk_name	= "mcbsp_clk",
	},
	{
		.phys_base	= OMAP34XX_MCBSP2_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP2_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP2_TX,
		.rx_irq		= INT_24XX_MCBSP2_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP2_IRQ_TX,
		.ops		= &omap2_mcbsp_ops,
		.clk_name	= "mcbsp_clk",
	},
	{
		.phys_base	= OMAP34XX_MCBSP3_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP3_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP3_TX,
		.rx_irq		= INT_24XX_MCBSP3_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP3_IRQ_TX,
		.ops		= &omap2_mcbsp_ops,
		.clk_name	= "mcbsp_clk",
	},
	{
		.phys_base	= OMAP34XX_MCBSP4_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP4_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP4_TX,
		.rx_irq		= INT_24XX_MCBSP4_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP4_IRQ_TX,
		.ops		= &omap2_mcbsp_ops,
		.clk_name	= "mcbsp_clk",
	},
	{
		.phys_base	= OMAP34XX_MCBSP5_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP5_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP5_TX,
		.rx_irq		= INT_24XX_MCBSP5_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP5_IRQ_TX,
		.ops		= &omap2_mcbsp_ops,
		.clk_name	= "mcbsp_clk",
	},
};
#define OMAP34XX_MCBSP_PDATA_SZ		ARRAY_SIZE(omap34xx_mcbsp_pdata)
#else
#define omap34xx_mcbsp_pdata		NULL
#define OMAP34XX_MCBSP_PDATA_SZ		0
#endif

static int __init omap2_mcbsp_init(void)
{
	int i;

	for (i = 0; i < omap_mcbsp_clks_size; i++) {
		/* Once we call clk_get inside init, we do not register it */
		omap_mcbsp_clk_init(&omap_mcbsp_clks[i]);
		clk_register(&omap_mcbsp_clks[i].clk);
	}

	if (cpu_is_omap2420())
		omap_mcbsp_count = OMAP2420_MCBSP_PDATA_SZ;
	if (cpu_is_omap2430())
		omap_mcbsp_count = OMAP2430_MCBSP_PDATA_SZ;
	if (cpu_is_omap34xx())
		omap_mcbsp_count = OMAP34XX_MCBSP_PDATA_SZ;

	mcbsp_ptr = kzalloc(omap_mcbsp_count * sizeof(struct omap_mcbsp *),
								GFP_KERNEL);
	if (!mcbsp_ptr)
		return -ENOMEM;

	if (cpu_is_omap2420())
		omap_mcbsp_register_board_cfg(omap2420_mcbsp_pdata,
						OMAP2420_MCBSP_PDATA_SZ);
	if (cpu_is_omap2430())
		omap_mcbsp_register_board_cfg(omap2430_mcbsp_pdata,
						OMAP2430_MCBSP_PDATA_SZ);
	if (cpu_is_omap34xx())
		omap_mcbsp_register_board_cfg(omap34xx_mcbsp_pdata,
						OMAP34XX_MCBSP_PDATA_SZ);

	return omap_mcbsp_init();
}
arch_initcall(omap2_mcbsp_init);
