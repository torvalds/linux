// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/mach-spear3xx/spear3xx.c
 *
 * SPEAr3XX machines common source file
 *
 * Copyright (C) 2009-2012 ST Microelectronics
 * Viresh Kumar <vireshk@kernel.org>
 */

#define pr_fmt(fmt) "SPEAr3xx: " fmt

#include <linux/amba/pl022.h>
#include <linux/amba/pl080.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <asm/mach/map.h>
#include "pl080.h"
#include "generic.h"
#include "spear.h"
#include "misc_regs.h"

/* ssp device registration */
struct pl022_ssp_controller pl022_plat_data = {
	.bus_id = 0,
	.enable_dma = 1,
	.dma_filter = pl08x_filter_id,
	.dma_tx_param = "ssp0_tx",
	.dma_rx_param = "ssp0_rx",
};

/* dmac device registration */
struct pl08x_platform_data pl080_plat_data = {
	.memcpy_burst_size = PL08X_BURST_SZ_16,
	.memcpy_bus_width = PL08X_BUS_WIDTH_32_BITS,
	.memcpy_prot_buff = true,
	.memcpy_prot_cache = true,
	.lli_buses = PL08X_AHB1,
	.mem_buses = PL08X_AHB1,
	.get_xfer_signal = pl080_get_signal,
	.put_xfer_signal = pl080_put_signal,
};

/*
 * Following will create 16MB static virtual/physical mappings
 * PHYSICAL		VIRTUAL
 * 0xD0000000		0xFD000000
 * 0xFC000000		0xFC000000
 */
struct map_desc spear3xx_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long)VA_SPEAR_ICM1_2_BASE,
		.pfn		= __phys_to_pfn(SPEAR_ICM1_2_BASE),
		.length		= SZ_16M,
		.type		= MT_DEVICE
	}, {
		.virtual	= (unsigned long)VA_SPEAR_ICM3_SMI_CTRL_BASE,
		.pfn		= __phys_to_pfn(SPEAR_ICM3_SMI_CTRL_BASE),
		.length		= SZ_16M,
		.type		= MT_DEVICE
	},
};

/* This will create static memory mapping for selected devices */
void __init spear3xx_map_io(void)
{
	iotable_init(spear3xx_io_desc, ARRAY_SIZE(spear3xx_io_desc));
}

void __init spear3xx_timer_init(void)
{
	char pclk_name[] = "pll3_clk";
	struct clk *gpt_clk, *pclk;

	spear3xx_clk_init(MISC_BASE, VA_SPEAR320_SOC_CONFIG_BASE);

	/* get the system timer clock */
	gpt_clk = clk_get_sys("gpt0", NULL);
	if (IS_ERR(gpt_clk)) {
		pr_err("%s:couldn't get clk for gpt\n", __func__);
		BUG();
	}

	/* get the suitable parent clock for timer*/
	pclk = clk_get(NULL, pclk_name);
	if (IS_ERR(pclk)) {
		pr_err("%s:couldn't get %s as parent for gpt\n",
				__func__, pclk_name);
		BUG();
	}

	clk_set_parent(gpt_clk, pclk);
	clk_put(gpt_clk);
	clk_put(pclk);

	spear_setup_of_timer();
}
