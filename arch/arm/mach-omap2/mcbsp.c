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
#include <linux/slab.h>

#include <mach/irqs.h>
#include <plat/dma.h>
#include <plat/cpu.h>
#include <plat/mcbsp.h>

#include "control.h"


/* McBSP internal signal muxing functions */

void omap2_mcbsp1_mux_clkr_src(u8 mux)
{
	u32 v;

	v = omap_ctrl_readl(OMAP2_CONTROL_DEVCONF0);
	if (mux == CLKR_SRC_CLKR)
		v &= ~OMAP2_MCBSP1_CLKR_MASK;
	else if (mux == CLKR_SRC_CLKX)
		v |= OMAP2_MCBSP1_CLKR_MASK;
	omap_ctrl_writel(v, OMAP2_CONTROL_DEVCONF0);
}
EXPORT_SYMBOL(omap2_mcbsp1_mux_clkr_src);

void omap2_mcbsp1_mux_fsr_src(u8 mux)
{
	u32 v;

	v = omap_ctrl_readl(OMAP2_CONTROL_DEVCONF0);
	if (mux == FSR_SRC_FSR)
		v &= ~OMAP2_MCBSP1_FSR_MASK;
	else if (mux == FSR_SRC_FSX)
		v |= OMAP2_MCBSP1_FSR_MASK;
	omap_ctrl_writel(v, OMAP2_CONTROL_DEVCONF0);
}
EXPORT_SYMBOL(omap2_mcbsp1_mux_fsr_src);

/* McBSP CLKS source switching function */

int omap2_mcbsp_set_clks_src(u8 id, u8 fck_src_id)
{
	struct omap_mcbsp *mcbsp;
	struct clk *fck_src;
	char *fck_src_name;
	int r;

	if (!omap_mcbsp_check_valid_id(id)) {
		pr_err("%s: Invalid id (%d)\n", __func__, id + 1);
		return -EINVAL;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	if (fck_src_id == MCBSP_CLKS_PAD_SRC)
		fck_src_name = "pad_fck";
	else if (fck_src_id == MCBSP_CLKS_PRCM_SRC)
		fck_src_name = "prcm_fck";
	else
		return -EINVAL;

	fck_src = clk_get(mcbsp->dev, fck_src_name);
	if (IS_ERR_OR_NULL(fck_src)) {
		pr_err("omap-mcbsp: %s: could not clk_get() %s\n", "clks",
		       fck_src_name);
		return -EINVAL;
	}

	clk_disable(mcbsp->fclk);

	r = clk_set_parent(mcbsp->fclk, fck_src);
	if (IS_ERR_VALUE(r)) {
		pr_err("omap-mcbsp: %s: could not clk_set_parent() to %s\n",
		       "clks", fck_src_name);
		clk_put(fck_src);
		return -EINVAL;
	}

	clk_enable(mcbsp->fclk);

	clk_put(fck_src);

	return 0;
}
EXPORT_SYMBOL(omap2_mcbsp_set_clks_src);


/* Platform data */

#ifdef CONFIG_ARCH_OMAP2420
static struct omap_mcbsp_platform_data omap2420_mcbsp_pdata[] = {
	{
		.phys_base	= OMAP24XX_MCBSP1_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP1_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP1_TX,
		.rx_irq		= INT_24XX_MCBSP1_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP1_IRQ_TX,
	},
	{
		.phys_base	= OMAP24XX_MCBSP2_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP2_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP2_TX,
		.rx_irq		= INT_24XX_MCBSP2_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP2_IRQ_TX,
	},
};
#define OMAP2420_MCBSP_PDATA_SZ		ARRAY_SIZE(omap2420_mcbsp_pdata)
#define OMAP2420_MCBSP_REG_NUM		(OMAP_MCBSP_REG_RCCR / sizeof(u32) + 1)
#else
#define omap2420_mcbsp_pdata		NULL
#define OMAP2420_MCBSP_PDATA_SZ		0
#define OMAP2420_MCBSP_REG_NUM		0
#endif

#ifdef CONFIG_ARCH_OMAP2430
static struct omap_mcbsp_platform_data omap2430_mcbsp_pdata[] = {
	{
		.phys_base	= OMAP24XX_MCBSP1_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP1_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP1_TX,
		.rx_irq		= INT_24XX_MCBSP1_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP1_IRQ_TX,
	},
	{
		.phys_base	= OMAP24XX_MCBSP2_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP2_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP2_TX,
		.rx_irq		= INT_24XX_MCBSP2_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP2_IRQ_TX,
	},
	{
		.phys_base	= OMAP2430_MCBSP3_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP3_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP3_TX,
		.rx_irq		= INT_24XX_MCBSP3_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP3_IRQ_TX,
	},
	{
		.phys_base	= OMAP2430_MCBSP4_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP4_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP4_TX,
		.rx_irq		= INT_24XX_MCBSP4_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP4_IRQ_TX,
	},
	{
		.phys_base	= OMAP2430_MCBSP5_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP5_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP5_TX,
		.rx_irq		= INT_24XX_MCBSP5_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP5_IRQ_TX,
	},
};
#define OMAP2430_MCBSP_PDATA_SZ		ARRAY_SIZE(omap2430_mcbsp_pdata)
#define OMAP2430_MCBSP_REG_NUM		(OMAP_MCBSP_REG_RCCR / sizeof(u32) + 1)
#else
#define omap2430_mcbsp_pdata		NULL
#define OMAP2430_MCBSP_PDATA_SZ		0
#define OMAP2430_MCBSP_REG_NUM		0
#endif

#ifdef CONFIG_ARCH_OMAP3
static struct omap_mcbsp_platform_data omap34xx_mcbsp_pdata[] = {
	{
		.phys_base	= OMAP34XX_MCBSP1_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP1_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP1_TX,
		.rx_irq		= INT_24XX_MCBSP1_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP1_IRQ_TX,
		.buffer_size	= 0x80, /* The FIFO has 128 locations */
	},
	{
		.phys_base	= OMAP34XX_MCBSP2_BASE,
		.phys_base_st	= OMAP34XX_MCBSP2_ST_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP2_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP2_TX,
		.rx_irq		= INT_24XX_MCBSP2_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP2_IRQ_TX,
		.buffer_size	= 0x500, /* The FIFO has 1024 + 256 locations */
	},
	{
		.phys_base	= OMAP34XX_MCBSP3_BASE,
		.phys_base_st	= OMAP34XX_MCBSP3_ST_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP3_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP3_TX,
		.rx_irq		= INT_24XX_MCBSP3_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP3_IRQ_TX,
		.buffer_size	= 0x80, /* The FIFO has 128 locations */
	},
	{
		.phys_base	= OMAP34XX_MCBSP4_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP4_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP4_TX,
		.rx_irq		= INT_24XX_MCBSP4_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP4_IRQ_TX,
		.buffer_size	= 0x80, /* The FIFO has 128 locations */
	},
	{
		.phys_base	= OMAP34XX_MCBSP5_BASE,
		.dma_rx_sync	= OMAP24XX_DMA_MCBSP5_RX,
		.dma_tx_sync	= OMAP24XX_DMA_MCBSP5_TX,
		.rx_irq		= INT_24XX_MCBSP5_IRQ_RX,
		.tx_irq		= INT_24XX_MCBSP5_IRQ_TX,
		.buffer_size	= 0x80, /* The FIFO has 128 locations */
	},
};
#define OMAP34XX_MCBSP_PDATA_SZ		ARRAY_SIZE(omap34xx_mcbsp_pdata)
#define OMAP34XX_MCBSP_REG_NUM		(OMAP_MCBSP_REG_RCCR / sizeof(u32) + 1)
#else
#define omap34xx_mcbsp_pdata		NULL
#define OMAP34XX_MCBSP_PDATA_SZ		0
#define OMAP34XX_MCBSP_REG_NUM		0
#endif

static struct omap_mcbsp_platform_data omap44xx_mcbsp_pdata[] = {
	{
		.phys_base      = OMAP44XX_MCBSP1_BASE,
		.dma_rx_sync    = OMAP44XX_DMA_MCBSP1_RX,
		.dma_tx_sync    = OMAP44XX_DMA_MCBSP1_TX,
		.tx_irq         = OMAP44XX_IRQ_MCBSP1,
	},
	{
		.phys_base      = OMAP44XX_MCBSP2_BASE,
		.dma_rx_sync    = OMAP44XX_DMA_MCBSP2_RX,
		.dma_tx_sync    = OMAP44XX_DMA_MCBSP2_TX,
		.tx_irq         = OMAP44XX_IRQ_MCBSP2,
	},
	{
		.phys_base      = OMAP44XX_MCBSP3_BASE,
		.dma_rx_sync    = OMAP44XX_DMA_MCBSP3_RX,
		.dma_tx_sync    = OMAP44XX_DMA_MCBSP3_TX,
		.tx_irq         = OMAP44XX_IRQ_MCBSP3,
	},
	{
		.phys_base      = OMAP44XX_MCBSP4_BASE,
		.dma_rx_sync    = OMAP44XX_DMA_MCBSP4_RX,
		.dma_tx_sync    = OMAP44XX_DMA_MCBSP4_TX,
		.tx_irq         = OMAP44XX_IRQ_MCBSP4,
	},
};
#define OMAP44XX_MCBSP_PDATA_SZ		ARRAY_SIZE(omap44xx_mcbsp_pdata)
#define OMAP44XX_MCBSP_REG_NUM		(OMAP_MCBSP_REG_RCCR / sizeof(u32) + 1)

static int __init omap2_mcbsp_init(void)
{
	if (cpu_is_omap2420()) {
		omap_mcbsp_count = OMAP2420_MCBSP_PDATA_SZ;
		omap_mcbsp_cache_size = OMAP2420_MCBSP_REG_NUM * sizeof(u16);
	} else if (cpu_is_omap2430()) {
		omap_mcbsp_count = OMAP2430_MCBSP_PDATA_SZ;
		omap_mcbsp_cache_size = OMAP2430_MCBSP_REG_NUM * sizeof(u32);
	} else if (cpu_is_omap34xx()) {
		omap_mcbsp_count = OMAP34XX_MCBSP_PDATA_SZ;
		omap_mcbsp_cache_size = OMAP34XX_MCBSP_REG_NUM * sizeof(u32);
	} else if (cpu_is_omap44xx()) {
		omap_mcbsp_count = OMAP44XX_MCBSP_PDATA_SZ;
		omap_mcbsp_cache_size = OMAP44XX_MCBSP_REG_NUM * sizeof(u32);
	}

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
	if (cpu_is_omap44xx())
		omap_mcbsp_register_board_cfg(omap44xx_mcbsp_pdata,
						OMAP44XX_MCBSP_PDATA_SZ);

	return omap_mcbsp_init();
}
arch_initcall(omap2_mcbsp_init);
