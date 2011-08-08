/* linux/arch/arm/mach-s3c2443/clock.c
 *
 * Copyright (c) 2007, 2010 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2443 Clock control support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/init.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/sysdev.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/serial_core.h>
#include <linux/io.h>

#include <asm/mach/map.h>

#include <mach/hardware.h>

#include <mach/regs-s3c2443-clock.h>

#include <plat/cpu-freq.h>

#include <plat/s3c2443.h>
#include <plat/clock.h>
#include <plat/clock-clksrc.h>
#include <plat/cpu.h>

/* We currently have to assume that the system is running
 * from the XTPll input, and that all ***REFCLKs are being
 * fed from it, as we cannot read the state of OM[4] from
 * software.
 *
 * It would be possible for each board initialisation to
 * set the correct muxing at initialisation
*/

/* clock selections */

static struct clk clk_i2s_ext = {
	.name		= "i2s-ext",
};

/* armdiv
 *
 * this clock is sourced from msysclk and can have a number of
 * divider values applied to it to then be fed into armclk.
*/

/* armdiv divisor table */

static unsigned int armdiv[16] = {
	[S3C2443_CLKDIV0_ARMDIV_1 >> S3C2443_CLKDIV0_ARMDIV_SHIFT]	= 1,
	[S3C2443_CLKDIV0_ARMDIV_2 >> S3C2443_CLKDIV0_ARMDIV_SHIFT]	= 2,
	[S3C2443_CLKDIV0_ARMDIV_3 >> S3C2443_CLKDIV0_ARMDIV_SHIFT]	= 3,
	[S3C2443_CLKDIV0_ARMDIV_4 >> S3C2443_CLKDIV0_ARMDIV_SHIFT]	= 4,
	[S3C2443_CLKDIV0_ARMDIV_6 >> S3C2443_CLKDIV0_ARMDIV_SHIFT]	= 6,
	[S3C2443_CLKDIV0_ARMDIV_8 >> S3C2443_CLKDIV0_ARMDIV_SHIFT]	= 8,
	[S3C2443_CLKDIV0_ARMDIV_12 >> S3C2443_CLKDIV0_ARMDIV_SHIFT]	= 12,
	[S3C2443_CLKDIV0_ARMDIV_16 >> S3C2443_CLKDIV0_ARMDIV_SHIFT]	= 16,
};

static inline unsigned int s3c2443_fclk_div(unsigned long clkcon0)
{
	clkcon0 &= S3C2443_CLKDIV0_ARMDIV_MASK;

	return armdiv[clkcon0 >> S3C2443_CLKDIV0_ARMDIV_SHIFT];
}

static unsigned long s3c2443_armclk_roundrate(struct clk *clk,
					      unsigned long rate)
{
	unsigned long parent = clk_get_rate(clk->parent);
	unsigned long calc;
	unsigned best = 256; /* bigger than any value */
	unsigned div;
	int ptr;

	for (ptr = 0; ptr < ARRAY_SIZE(armdiv); ptr++) {
		div = armdiv[ptr];
		calc = parent / div;
		if (calc <= rate && div < best)
			best = div;
	}

	return parent / best;
}

static int s3c2443_armclk_setrate(struct clk *clk, unsigned long rate)
{
	unsigned long parent = clk_get_rate(clk->parent);
	unsigned long calc;
	unsigned div;
	unsigned best = 256; /* bigger than any value */
	int ptr;
	int val = -1;

	for (ptr = 0; ptr < ARRAY_SIZE(armdiv); ptr++) {
		div = armdiv[ptr];
		calc = parent / div;
		if (calc <= rate && div < best) {
			best = div;
			val = ptr;
		}
	}

	if (val >= 0) {
		unsigned long clkcon0;

		clkcon0 = __raw_readl(S3C2443_CLKDIV0);
		clkcon0 &= S3C2443_CLKDIV0_ARMDIV_MASK;
		clkcon0 |= val << S3C2443_CLKDIV0_ARMDIV_SHIFT;
		__raw_writel(clkcon0, S3C2443_CLKDIV0);
	}

	return (val == -1) ? -EINVAL : 0;
}

static struct clk clk_armdiv = {
	.name		= "armdiv",
	.parent		= &clk_msysclk.clk,
	.ops		= &(struct clk_ops) {
		.round_rate = s3c2443_armclk_roundrate,
		.set_rate = s3c2443_armclk_setrate,
	},
};

/* armclk
 *
 * this is the clock fed into the ARM core itself, from armdiv or from hclk.
 */

static struct clk *clk_arm_sources[] = {
	[0] = &clk_armdiv,
	[1] = &clk_h,
};

static struct clksrc_clk clk_arm = {
	.clk	= {
		.name		= "armclk",
	},
	.sources = &(struct clksrc_sources) {
		.sources = clk_arm_sources,
		.nr_sources = ARRAY_SIZE(clk_arm_sources),
	},
	.reg_src = { .reg = S3C2443_CLKDIV0, .size = 1, .shift = 13 },
};

/* hsspi
 *
 * high-speed spi clock, sourced from esysclk
*/

static struct clksrc_clk clk_hsspi = {
	.clk	= {
		.name		= "hsspi",
		.parent		= &clk_esysclk.clk,
		.ctrlbit	= S3C2443_SCLKCON_HSSPICLK,
		.enable		= s3c2443_clkcon_enable_s,
	},
	.reg_div = { .reg = S3C2443_CLKDIV1, .size = 2, .shift = 4 },
};


/* clk_hsmcc_div
 *
 * this clock is sourced from epll, and is fed through a divider,
 * to a mux controlled by sclkcon where either it or a extclk can
 * be fed to the hsmmc block
*/

static struct clksrc_clk clk_hsmmc_div = {
	.clk	= {
		.name		= "hsmmc-div",
		.devname	= "s3c-sdhci.1",
		.parent		= &clk_esysclk.clk,
	},
	.reg_div = { .reg = S3C2443_CLKDIV1, .size = 2, .shift = 6 },
};

static int s3c2443_setparent_hsmmc(struct clk *clk, struct clk *parent)
{
	unsigned long clksrc = __raw_readl(S3C2443_SCLKCON);

	clksrc &= ~(S3C2443_SCLKCON_HSMMCCLK_EXT |
		    S3C2443_SCLKCON_HSMMCCLK_EPLL);

	if (parent == &clk_epll)
		clksrc |= S3C2443_SCLKCON_HSMMCCLK_EPLL;
	else if (parent == &clk_ext)
		clksrc |= S3C2443_SCLKCON_HSMMCCLK_EXT;
	else
		return -EINVAL;

	if (clk->usage > 0) {
		__raw_writel(clksrc, S3C2443_SCLKCON);
	}

	clk->parent = parent;
	return 0;
}

static int s3c2443_enable_hsmmc(struct clk *clk, int enable)
{
	return s3c2443_setparent_hsmmc(clk, clk->parent);
}

static struct clk clk_hsmmc = {
	.name		= "hsmmc-if",
	.devname	= "s3c-sdhci.1",
	.parent		= &clk_hsmmc_div.clk,
	.enable		= s3c2443_enable_hsmmc,
	.ops		= &(struct clk_ops) {
		.set_parent	= s3c2443_setparent_hsmmc,
	},
};

/* i2s_eplldiv
 *
 * This clock is the output from the I2S divisor of ESYSCLK, and is separate
 * from the mux that comes after it (cannot merge into one single clock)
*/

static struct clksrc_clk clk_i2s_eplldiv = {
	.clk	= {
		.name		= "i2s-eplldiv",
		.parent		= &clk_esysclk.clk,
	},
	.reg_div = { .reg = S3C2443_CLKDIV1, .size = 4, .shift = 12, },
};

/* i2s-ref
 *
 * i2s bus reference clock, selectable from external, esysclk or epllref
 *
 * Note, this used to be two clocks, but was compressed into one.
*/

struct clk *clk_i2s_srclist[] = {
	[0] = &clk_i2s_eplldiv.clk,
	[1] = &clk_i2s_ext,
	[2] = &clk_epllref.clk,
	[3] = &clk_epllref.clk,
};

static struct clksrc_clk clk_i2s = {
	.clk	= {
		.name		= "i2s-if",
		.ctrlbit	= S3C2443_SCLKCON_I2SCLK,
		.enable		= s3c2443_clkcon_enable_s,

	},
	.sources = &(struct clksrc_sources) {
		.sources = clk_i2s_srclist,
		.nr_sources = ARRAY_SIZE(clk_i2s_srclist),
	},
	.reg_src = { .reg = S3C2443_CLKSRC, .size = 2, .shift = 14 },
};

/* standard clock definitions */

static struct clk init_clocks_off[] = {
	{
		.name		= "sdi",
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_SDI,
	}, {
		.name		= "iis",
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_IIS,
	}, {
		.name		= "spi",
		.devname	= "s3c2410-spi.0",
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_SPI0,
	}, {
		.name		= "spi",
		.devname	= "s3c2410-spi.1",
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_SPI1,
	}
};

static struct clk init_clocks[] = {
};

/* clocks to add straight away */

static struct clksrc_clk *clksrcs[] __initdata = {
	&clk_arm,
	&clk_i2s_eplldiv,
	&clk_i2s,
	&clk_hsspi,
	&clk_hsmmc_div,
};

static struct clk *clks[] __initdata = {
	&clk_hsmmc,
	&clk_armdiv,
};

void __init_or_cpufreq s3c2443_setup_clocks(void)
{
	s3c2443_common_setup_clocks(s3c2443_get_mpll, s3c2443_fclk_div);
}

void __init s3c2443_init_clocks(int xtal)
{
	unsigned long epllcon = __raw_readl(S3C2443_EPLLCON);
	int ptr;

	clk_epll.rate = s3c2443_get_epll(epllcon, xtal);
	clk_epll.parent = &clk_epllref.clk;

	s3c2443_common_init_clocks(xtal, s3c2443_get_mpll, s3c2443_fclk_div);

	s3c2443_setup_clocks();

	s3c24xx_register_clocks(clks, ARRAY_SIZE(clks));

	for (ptr = 0; ptr < ARRAY_SIZE(clksrcs); ptr++)
		s3c_register_clksrc(clksrcs[ptr], 1);

	/* register clocks from clock array */

	s3c_register_clocks(init_clocks, ARRAY_SIZE(init_clocks));

	/* We must be careful disabling the clocks we are not intending to
	 * be using at boot time, as subsystems such as the LCD which do
	 * their own DMA requests to the bus can cause the system to lockup
	 * if they where in the middle of requesting bus access.
	 *
	 * Disabling the LCD clock if the LCD is active is very dangerous,
	 * and therefore the bootloader should be careful to not enable
	 * the LCD clock if it is not needed.
	*/

	/* install (and disable) the clocks we do not need immediately */

	s3c_register_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));
	s3c_disable_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));

	s3c_pwmclk_init();
}
