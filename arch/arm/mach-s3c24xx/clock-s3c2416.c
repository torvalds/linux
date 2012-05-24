/* linux/arch/arm/mach-s3c2416/clock.c
 *
 * Copyright (c) 2010 Simtec Electronics
 * Copyright (c) 2010 Ben Dooks <ben-linux@fluff.org>
 *
 * S3C2416 Clock control support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/clk.h>

#include <plat/s3c2416.h>
#include <plat/clock.h>
#include <plat/clock-clksrc.h>
#include <plat/cpu.h>

#include <plat/cpu-freq.h>
#include <plat/pll.h>

#include <asm/mach/map.h>

#include <mach/regs-clock.h>
#include <mach/regs-s3c2443-clock.h>

/* armdiv
 *
 * this clock is sourced from msysclk and can have a number of
 * divider values applied to it to then be fed into armclk.
 * The real clock definition is done in s3c2443-clock.c,
 * only the armdiv divisor table must be defined here.
*/

static unsigned int armdiv[8] = {
	[0] = 1,
	[1] = 2,
	[2] = 3,
	[3] = 4,
	[5] = 6,
	[7] = 8,
};

static struct clksrc_clk hsspi_eplldiv = {
	.clk = {
		.name	= "hsspi-eplldiv",
		.parent	= &clk_esysclk.clk,
		.ctrlbit = (1 << 14),
		.enable = s3c2443_clkcon_enable_s,
	},
	.reg_div = { .reg = S3C2443_CLKDIV1, .size = 2, .shift = 24 },
};

static struct clk *hsspi_sources[] = {
	[0] = &hsspi_eplldiv.clk,
	[1] = NULL, /* to fix */
};

static struct clksrc_clk hsspi_mux = {
	.clk	= {
		.name	= "hsspi-if",
	},
	.sources = &(struct clksrc_sources) {
		.sources = hsspi_sources,
		.nr_sources = ARRAY_SIZE(hsspi_sources),
	},
	.reg_src = { .reg = S3C2443_CLKSRC, .size = 1, .shift = 18 },
};

static struct clksrc_clk hsmmc_div[] = {
	[0] = {
		.clk = {
			.name	= "hsmmc-div",
			.devname	= "s3c-sdhci.0",
			.parent	= &clk_esysclk.clk,
		},
		.reg_div = { .reg = S3C2416_CLKDIV2, .size = 2, .shift = 6 },
	},
	[1] = {
		.clk = {
			.name	= "hsmmc-div",
			.devname	= "s3c-sdhci.1",
			.parent	= &clk_esysclk.clk,
		},
		.reg_div = { .reg = S3C2443_CLKDIV1, .size = 2, .shift = 6 },
	},
};

static struct clksrc_clk hsmmc_mux0 = {
	.clk	= {
		.name		= "hsmmc-if",
		.devname	= "s3c-sdhci.0",
		.ctrlbit	= (1 << 6),
		.enable		= s3c2443_clkcon_enable_s,
	},
	.sources	= &(struct clksrc_sources) {
		.nr_sources	= 2,
		.sources	= (struct clk * []) {
			[0]	= &hsmmc_div[0].clk,
			[1]	= NULL, /* to fix */
		},
	},
	.reg_src = { .reg = S3C2443_CLKSRC, .size = 1, .shift = 16 },
};

static struct clksrc_clk hsmmc_mux1 = {
	.clk	= {
		.name		= "hsmmc-if",
		.devname	= "s3c-sdhci.1",
		.ctrlbit	= (1 << 12),
		.enable		= s3c2443_clkcon_enable_s,
	},
	.sources	= &(struct clksrc_sources) {
		.nr_sources	= 2,
		.sources	= (struct clk * []) {
			[0]	= &hsmmc_div[1].clk,
			[1]	= NULL, /* to fix */
		},
	},
	.reg_src = { .reg = S3C2443_CLKSRC, .size = 1, .shift = 17 },
};

static struct clk hsmmc0_clk = {
	.name		= "hsmmc",
	.devname	= "s3c-sdhci.0",
	.parent		= &clk_h,
	.enable		= s3c2443_clkcon_enable_h,
	.ctrlbit	= S3C2416_HCLKCON_HSMMC0,
};

static struct clksrc_clk *clksrcs[] __initdata = {
	&hsspi_eplldiv,
	&hsspi_mux,
	&hsmmc_div[0],
	&hsmmc_div[1],
	&hsmmc_mux0,
	&hsmmc_mux1,
};

static struct clk_lookup s3c2416_clk_lookup[] = {
	CLKDEV_INIT("s3c-sdhci.0", "mmc_busclk.0", &hsmmc0_clk),
	CLKDEV_INIT("s3c-sdhci.0", "mmc_busclk.2", &hsmmc_mux0.clk),
	CLKDEV_INIT("s3c-sdhci.1", "mmc_busclk.2", &hsmmc_mux1.clk),
};

void __init s3c2416_init_clocks(int xtal)
{
	u32 epllcon = __raw_readl(S3C2443_EPLLCON);
	u32 epllcon1 = __raw_readl(S3C2443_EPLLCON+4);
	int ptr;

	/* s3c2416 EPLL compatible with s3c64xx */
	clk_epll.rate = s3c_get_pll6553x(xtal, epllcon, epllcon1);

	clk_epll.parent = &clk_epllref.clk;

	s3c2443_common_init_clocks(xtal, s3c2416_get_pll,
				   armdiv, ARRAY_SIZE(armdiv),
				   S3C2416_CLKDIV0_ARMDIV_MASK);

	for (ptr = 0; ptr < ARRAY_SIZE(clksrcs); ptr++)
		s3c_register_clksrc(clksrcs[ptr], 1);

	s3c24xx_register_clock(&hsmmc0_clk);
	clkdev_add_table(s3c2416_clk_lookup, ARRAY_SIZE(s3c2416_clk_lookup));

	s3c_pwmclk_init();

}
