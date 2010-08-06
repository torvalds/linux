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
#include <plat/s3c2443.h>
#include <plat/clock.h>
#include <plat/clock-clksrc.h>
#include <plat/cpu.h>

#include <plat/cpu-freq.h>
#include <plat/pll6553x.h>
#include <plat/pll.h>

#include <asm/mach/map.h>

#include <mach/regs-clock.h>
#include <mach/regs-s3c2443-clock.h>

static unsigned int armdiv[8] = {
	[0] = 1,
	[1] = 2,
	[2] = 3,
	[3] = 4,
	[5] = 6,
	[7] = 8,
};

/* ID to hardware numbering, 0 is HSMMC1, 1 is HSMMC0 */
static struct clksrc_clk hsmmc_div[] = {
	[0] = {
		.clk = {
			.name	= "hsmmc-div",
			.id	= 1,
			.parent	= &clk_esysclk.clk,
		},
		.reg_div = { .reg = S3C2416_CLKDIV2, .size = 2, .shift = 6 },
	},
	[1] = {
		.clk = {
			.name	= "hsmmc-div",
			.id	= 0,
			.parent	= &clk_esysclk.clk,
		},
		.reg_div = { .reg = S3C2443_CLKDIV1, .size = 2, .shift = 6 },
	},
};

static struct clksrc_clk hsmmc_mux[] = {
	[0] = {
		.clk	= {
			.id	= 1,
			.name	= "hsmmc-if",
			.ctrlbit = (1 << 6),
			.enable = s3c2443_clkcon_enable_s,
		},
		.sources = &(struct clksrc_sources) {
			.nr_sources = 2,
			.sources = (struct clk *[]) {
				[0] = &hsmmc_div[0].clk,
				[1] = NULL, /* to fix */
			},
		},
		.reg_src = { .reg = S3C2443_CLKSRC, .size = 1, .shift = 16 },
	},
	[1] = {
		.clk	= {
			.id	= 0,
			.name	= "hsmmc-if",
			.ctrlbit = (1 << 12),
			.enable = s3c2443_clkcon_enable_s,
		},
		.sources = &(struct clksrc_sources) {
			.nr_sources = 2,
			.sources = (struct clk *[]) {
				[0] = &hsmmc_div[1].clk,
				[1] = NULL, /* to fix */
			},
		},
		.reg_src = { .reg = S3C2443_CLKSRC, .size = 1, .shift = 17 },
	},
};


static inline unsigned int s3c2416_fclk_div(unsigned long clkcon0)
{
	clkcon0 &= 7 << S3C2443_CLKDIV0_ARMDIV_SHIFT;

	return armdiv[clkcon0 >> S3C2443_CLKDIV0_ARMDIV_SHIFT];
}

void __init_or_cpufreq s3c2416_setup_clocks(void)
{
	s3c2443_common_setup_clocks(s3c2416_get_pll, s3c2416_fclk_div);
}


static struct clksrc_clk *clksrcs[] __initdata = {
	&hsmmc_div[0],
	&hsmmc_div[1],
	&hsmmc_mux[0],
	&hsmmc_mux[1],
};

void __init s3c2416_init_clocks(int xtal)
{
	u32 epllcon = __raw_readl(S3C2443_EPLLCON);
	u32 epllcon1 = __raw_readl(S3C2443_EPLLCON+4);
	int ptr;

	/* s3c2416 EPLL compatible with s3c64xx */
	clk_epll.rate = s3c_get_pll6553x(xtal, epllcon, epllcon1);

	clk_epll.parent = &clk_epllref.clk;

	s3c2443_common_init_clocks(xtal, s3c2416_get_pll, s3c2416_fclk_div);

	for (ptr = 0; ptr < ARRAY_SIZE(clksrcs); ptr++)
		s3c_register_clksrc(clksrcs[ptr], 1);

	s3c_pwmclk_init();

}
