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

#ifdef CONFIG_SOC_OMAP2420
struct resource omap2420_mcbsp_res[][6] = {
	{
		{
			.start = OMAP24XX_MCBSP1_BASE,
			.end   = OMAP24XX_MCBSP1_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_24XX_MCBSP1_IRQ_RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_24XX_MCBSP1_IRQ_TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = OMAP24XX_DMA_MCBSP1_RX,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = OMAP24XX_DMA_MCBSP1_TX,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.start = OMAP24XX_MCBSP2_BASE,
			.end   = OMAP24XX_MCBSP2_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_24XX_MCBSP2_IRQ_RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_24XX_MCBSP2_IRQ_TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = OMAP24XX_DMA_MCBSP2_RX,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = OMAP24XX_DMA_MCBSP2_TX,
			.flags = IORESOURCE_DMA,
		},
	},
};
#define OMAP2420_MCBSP_RES_SZ		ARRAY_SIZE(omap2420_mcbsp_res[1])
#define OMAP2420_MCBSP_COUNT		ARRAY_SIZE(omap2420_mcbsp_res)
#else
#define omap2420_mcbsp_res		NULL
#define OMAP2420_MCBSP_RES_SZ		0
#define OMAP2420_MCBSP_COUNT		0
#endif

#define omap2420_mcbsp_pdata		NULL

#ifdef CONFIG_SOC_OMAP2430
struct resource omap2430_mcbsp_res[][6] = {
	{
		{
			.start = OMAP24XX_MCBSP1_BASE,
			.end   = OMAP24XX_MCBSP1_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_24XX_MCBSP1_IRQ_RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_24XX_MCBSP1_IRQ_TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = OMAP24XX_DMA_MCBSP1_RX,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = OMAP24XX_DMA_MCBSP1_TX,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.start = OMAP24XX_MCBSP2_BASE,
			.end   = OMAP24XX_MCBSP2_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_24XX_MCBSP2_IRQ_RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_24XX_MCBSP2_IRQ_TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = OMAP24XX_DMA_MCBSP2_RX,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = OMAP24XX_DMA_MCBSP2_TX,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.start = OMAP2430_MCBSP3_BASE,
			.end   = OMAP2430_MCBSP3_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_24XX_MCBSP3_IRQ_RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_24XX_MCBSP3_IRQ_TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = OMAP24XX_DMA_MCBSP3_RX,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = OMAP24XX_DMA_MCBSP3_TX,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.start = OMAP2430_MCBSP4_BASE,
			.end   = OMAP2430_MCBSP4_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_24XX_MCBSP4_IRQ_RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_24XX_MCBSP4_IRQ_TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = OMAP24XX_DMA_MCBSP4_RX,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = OMAP24XX_DMA_MCBSP4_TX,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.start = OMAP2430_MCBSP5_BASE,
			.end   = OMAP2430_MCBSP5_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_24XX_MCBSP5_IRQ_RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_24XX_MCBSP5_IRQ_TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = OMAP24XX_DMA_MCBSP5_RX,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = OMAP24XX_DMA_MCBSP5_TX,
			.flags = IORESOURCE_DMA,
		},
	},
};
#define OMAP2430_MCBSP_RES_SZ		ARRAY_SIZE(omap2430_mcbsp_res[1])
#define OMAP2430_MCBSP_COUNT		ARRAY_SIZE(omap2430_mcbsp_res)
#else
#define omap2430_mcbsp_res		NULL
#define OMAP2430_MCBSP_RES_SZ		0
#define OMAP2430_MCBSP_COUNT		0
#endif

#define omap2430_mcbsp_pdata		NULL

#ifdef CONFIG_ARCH_OMAP3
struct resource omap34xx_mcbsp_res[][7] = {
	{
		{
			.start = OMAP34XX_MCBSP1_BASE,
			.end   = OMAP34XX_MCBSP1_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_24XX_MCBSP1_IRQ_RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_24XX_MCBSP1_IRQ_TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = OMAP24XX_DMA_MCBSP1_RX,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = OMAP24XX_DMA_MCBSP1_TX,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.start = OMAP34XX_MCBSP2_BASE,
			.end   = OMAP34XX_MCBSP2_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "sidetone",
			.start = OMAP34XX_MCBSP2_ST_BASE,
			.end   = OMAP34XX_MCBSP2_ST_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_24XX_MCBSP2_IRQ_RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_24XX_MCBSP2_IRQ_TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = OMAP24XX_DMA_MCBSP2_RX,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = OMAP24XX_DMA_MCBSP2_TX,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.start = OMAP34XX_MCBSP3_BASE,
			.end   = OMAP34XX_MCBSP3_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "sidetone",
			.start = OMAP34XX_MCBSP3_ST_BASE,
			.end   = OMAP34XX_MCBSP3_ST_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_24XX_MCBSP3_IRQ_RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_24XX_MCBSP3_IRQ_TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = OMAP24XX_DMA_MCBSP3_RX,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = OMAP24XX_DMA_MCBSP3_TX,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.start = OMAP34XX_MCBSP4_BASE,
			.end   = OMAP34XX_MCBSP4_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_24XX_MCBSP4_IRQ_RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_24XX_MCBSP4_IRQ_TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = OMAP24XX_DMA_MCBSP4_RX,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = OMAP24XX_DMA_MCBSP4_TX,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.start = OMAP34XX_MCBSP5_BASE,
			.end   = OMAP34XX_MCBSP5_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_24XX_MCBSP5_IRQ_RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_24XX_MCBSP5_IRQ_TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = OMAP24XX_DMA_MCBSP5_RX,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = OMAP24XX_DMA_MCBSP5_TX,
			.flags = IORESOURCE_DMA,
		},
	},
};

static struct omap_mcbsp_platform_data omap34xx_mcbsp_pdata[] = {
	{
		.buffer_size	= 0x80, /* The FIFO has 128 locations */
	},
	{
		.buffer_size	= 0x500, /* The FIFO has 1024 + 256 locations */
	},
	{
		.buffer_size	= 0x80, /* The FIFO has 128 locations */
	},
	{
		.buffer_size	= 0x80, /* The FIFO has 128 locations */
	},
	{
		.buffer_size	= 0x80, /* The FIFO has 128 locations */
	},
};
#define OMAP34XX_MCBSP_RES_SZ		ARRAY_SIZE(omap34xx_mcbsp_res[1])
#define OMAP34XX_MCBSP_COUNT		ARRAY_SIZE(omap34xx_mcbsp_res)
#else
#define omap34xx_mcbsp_pdata		NULL
#define omap34XX_mcbsp_res		NULL
#define OMAP34XX_MCBSP_RES_SZ		0
#define OMAP34XX_MCBSP_COUNT		0
#endif

struct resource omap44xx_mcbsp_res[][6] = {
	{
		{
			.name  = "mpu",
			.start = OMAP44XX_MCBSP1_BASE,
			.end   = OMAP44XX_MCBSP1_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "dma",
			.start = OMAP44XX_MCBSP1_DMA_BASE,
			.end   = OMAP44XX_MCBSP1_DMA_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = 0,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = OMAP44XX_IRQ_MCBSP1,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = OMAP44XX_DMA_MCBSP1_RX,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = OMAP44XX_DMA_MCBSP1_TX,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.name  = "mpu",
			.start = OMAP44XX_MCBSP2_BASE,
			.end   = OMAP44XX_MCBSP2_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "dma",
			.start = OMAP44XX_MCBSP2_DMA_BASE,
			.end   = OMAP44XX_MCBSP2_DMA_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = 0,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = OMAP44XX_IRQ_MCBSP2,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = OMAP44XX_DMA_MCBSP2_RX,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = OMAP44XX_DMA_MCBSP2_TX,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.name  = "mpu",
			.start = OMAP44XX_MCBSP3_BASE,
			.end   = OMAP44XX_MCBSP3_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "dma",
			.start = OMAP44XX_MCBSP3_DMA_BASE,
			.end   = OMAP44XX_MCBSP3_DMA_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = 0,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = OMAP44XX_IRQ_MCBSP3,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = OMAP44XX_DMA_MCBSP3_RX,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = OMAP44XX_DMA_MCBSP3_TX,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.start = OMAP44XX_MCBSP4_BASE,
			.end   = OMAP44XX_MCBSP4_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = 0,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = OMAP44XX_IRQ_MCBSP4,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = OMAP44XX_DMA_MCBSP4_RX,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = OMAP44XX_DMA_MCBSP4_TX,
			.flags = IORESOURCE_DMA,
		},
	},
};
#define omap44xx_mcbsp_pdata		NULL
#define OMAP44XX_MCBSP_RES_SZ		ARRAY_SIZE(omap44xx_mcbsp_res[1])
#define OMAP44XX_MCBSP_COUNT		ARRAY_SIZE(omap44xx_mcbsp_res)

static int __init omap2_mcbsp_init(void)
{
	if (cpu_is_omap2420())
		omap_mcbsp_count = OMAP2420_MCBSP_COUNT;
	else if (cpu_is_omap2430())
		omap_mcbsp_count = OMAP2430_MCBSP_COUNT;
	else if (cpu_is_omap34xx())
		omap_mcbsp_count = OMAP34XX_MCBSP_COUNT;
	else if (cpu_is_omap44xx())
		omap_mcbsp_count = OMAP44XX_MCBSP_COUNT;

	mcbsp_ptr = kzalloc(omap_mcbsp_count * sizeof(struct omap_mcbsp *),
								GFP_KERNEL);
	if (!mcbsp_ptr)
		return -ENOMEM;

	if (cpu_is_omap2420())
		omap_mcbsp_register_board_cfg(omap2420_mcbsp_res[0],
					OMAP2420_MCBSP_RES_SZ,
					omap2420_mcbsp_pdata,
					OMAP2420_MCBSP_COUNT);
	if (cpu_is_omap2430())
		omap_mcbsp_register_board_cfg(omap2430_mcbsp_res[0],
					OMAP2420_MCBSP_RES_SZ,
					omap2430_mcbsp_pdata,
					OMAP2430_MCBSP_COUNT);
	if (cpu_is_omap34xx())
		omap_mcbsp_register_board_cfg(omap34xx_mcbsp_res[0],
					OMAP34XX_MCBSP_RES_SZ,
					omap34xx_mcbsp_pdata,
					OMAP34XX_MCBSP_COUNT);
	if (cpu_is_omap44xx())
		omap_mcbsp_register_board_cfg(omap44xx_mcbsp_res[0],
					OMAP44XX_MCBSP_RES_SZ,
					omap44xx_mcbsp_pdata,
					OMAP44XX_MCBSP_COUNT);

	return omap_mcbsp_init();
}
arch_initcall(omap2_mcbsp_init);
