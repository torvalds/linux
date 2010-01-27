/* linux/arch/arm/plat-s3c64xx/s3c6400-clock.c
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * S3C6400 based common clock support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/sysdev.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/map.h>

#include <plat/cpu-freq.h>

#include <plat/regs-clock.h>
#include <plat/clock.h>
#include <plat/clock-clksrc.h>
#include <plat/cpu.h>
#include <plat/pll.h>

/* fin_apll, fin_mpll and fin_epll are all the same clock, which we call
 * ext_xtal_mux for want of an actual name from the manual.
*/

static struct clk clk_ext_xtal_mux = {
	.name		= "ext_xtal",
	.id		= -1,
};

#define clk_fin_apll clk_ext_xtal_mux
#define clk_fin_mpll clk_ext_xtal_mux
#define clk_fin_epll clk_ext_xtal_mux

#define clk_fout_mpll	clk_mpll
#define clk_fout_epll	clk_epll

static struct clk clk_fout_apll = {
	.name		= "fout_apll",
	.id		= -1,
};

static struct clk *clk_src_apll_list[] = {
	[0] = &clk_fin_apll,
	[1] = &clk_fout_apll,
};

static struct clksrc_sources clk_src_apll = {
	.sources	= clk_src_apll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_apll_list),
};

static struct clksrc_clk clk_mout_apll = {
	.clk	= {
		.name		= "mout_apll",
		.id		= -1,
	},
	.reg_src	= { .reg = S3C_CLK_SRC, .shift = 0, .size = 1  },
	.sources	= &clk_src_apll,
};

static struct clk *clk_src_epll_list[] = {
	[0] = &clk_fin_epll,
	[1] = &clk_fout_epll,
};

static struct clksrc_sources clk_src_epll = {
	.sources	= clk_src_epll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_epll_list),
};

static struct clksrc_clk clk_mout_epll = {
	.clk	= {
		.name		= "mout_epll",
		.id		= -1,
	},
	.reg_src	= { .reg = S3C_CLK_SRC, .shift = 2, .size = 1  },
	.sources	= &clk_src_epll,
};

static struct clk *clk_src_mpll_list[] = {
	[0] = &clk_fin_mpll,
	[1] = &clk_fout_mpll,
};

static struct clksrc_sources clk_src_mpll = {
	.sources	= clk_src_mpll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_mpll_list),
};

static struct clksrc_clk clk_mout_mpll = {
	.clk = {
		.name		= "mout_mpll",
		.id		= -1,
	},
	.reg_src	= { .reg = S3C_CLK_SRC, .shift = 1, .size = 1  },
	.sources	= &clk_src_mpll,
};

static unsigned int armclk_mask;

static unsigned long s3c64xx_clk_arm_get_rate(struct clk *clk)
{
	unsigned long rate = clk_get_rate(clk->parent);
	u32 clkdiv;

	/* divisor mask starts at bit0, so no need to shift */
	clkdiv = __raw_readl(S3C_CLK_DIV0) & armclk_mask;

	return rate / (clkdiv + 1);
}

static unsigned long s3c64xx_clk_arm_round_rate(struct clk *clk,
						unsigned long rate)
{
	unsigned long parent = clk_get_rate(clk->parent);
	u32 div;

	if (parent < rate)
		return parent;

	div = (parent / rate) - 1;
	if (div > armclk_mask)
		div = armclk_mask;

	return parent / (div + 1);
}

static int s3c64xx_clk_arm_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long parent = clk_get_rate(clk->parent);
	u32 div;
	u32 val;

	if (rate < parent / (armclk_mask + 1))
		return -EINVAL;

	rate = clk_round_rate(clk, rate);
	div = clk_get_rate(clk->parent) / rate;

	val = __raw_readl(S3C_CLK_DIV0);
	val &= ~armclk_mask;
	val |= (div - 1);
	__raw_writel(val, S3C_CLK_DIV0);

	return 0;

}

static struct clk clk_arm = {
	.name		= "armclk",
	.id		= -1,
	.parent		= &clk_mout_apll.clk,
	.ops		= &(struct clk_ops) {
		.get_rate	= s3c64xx_clk_arm_get_rate,
		.set_rate	= s3c64xx_clk_arm_set_rate,
		.round_rate	= s3c64xx_clk_arm_round_rate,
	},
};

static unsigned long s3c64xx_clk_doutmpll_get_rate(struct clk *clk)
{
	unsigned long rate = clk_get_rate(clk->parent);

	printk(KERN_DEBUG "%s: parent is %ld\n", __func__, rate);

	if (__raw_readl(S3C_CLK_DIV0) & S3C6400_CLKDIV0_MPLL_MASK)
		rate /= 2;

	return rate;
}

static struct clk_ops clk_dout_ops = {
	.get_rate	= s3c64xx_clk_doutmpll_get_rate,
};

static struct clk clk_dout_mpll = {
	.name		= "dout_mpll",
	.id		= -1,
	.parent		= &clk_mout_mpll.clk,
	.ops		= &clk_dout_ops,
};

static struct clk *clkset_spi_mmc_list[] = {
	&clk_mout_epll.clk,
	&clk_dout_mpll,
	&clk_fin_epll,
	&clk_27m,
};

static struct clksrc_sources clkset_spi_mmc = {
	.sources	= clkset_spi_mmc_list,
	.nr_sources	= ARRAY_SIZE(clkset_spi_mmc_list),
};

static struct clk *clkset_irda_list[] = {
	&clk_mout_epll.clk,
	&clk_dout_mpll,
	NULL,
	&clk_27m,
};

static struct clksrc_sources clkset_irda = {
	.sources	= clkset_irda_list,
	.nr_sources	= ARRAY_SIZE(clkset_irda_list),
};

static struct clk *clkset_uart_list[] = {
	&clk_mout_epll.clk,
	&clk_dout_mpll,
	NULL,
	NULL
};

static struct clksrc_sources clkset_uart = {
	.sources	= clkset_uart_list,
	.nr_sources	= ARRAY_SIZE(clkset_uart_list),
};

static struct clk *clkset_uhost_list[] = {
	&clk_48m,
	&clk_mout_epll.clk,
	&clk_dout_mpll,
	&clk_fin_epll,
};

static struct clksrc_sources clkset_uhost = {
	.sources	= clkset_uhost_list,
	.nr_sources	= ARRAY_SIZE(clkset_uhost_list),
};

/* The peripheral clocks are all controlled via clocksource followed
 * by an optional divider and gate stage. We currently roll this into
 * one clock which hides the intermediate clock from the mux.
 *
 * Note, the JPEG clock can only be an even divider...
 *
 * The scaler and LCD clocks depend on the S3C64XX version, and also
 * have a common parent divisor so are not included here.
 */

/* clocks that feed other parts of the clock source tree */

static struct clk clk_iis_cd0 = {
	.name		= "iis_cdclk0",
	.id		= -1,
};

static struct clk clk_iis_cd1 = {
	.name		= "iis_cdclk1",
	.id		= -1,
};

static struct clk clk_pcm_cd = {
	.name		= "pcm_cdclk",
	.id		= -1,
};

static struct clk *clkset_audio0_list[] = {
	[0] = &clk_mout_epll.clk,
	[1] = &clk_dout_mpll,
	[2] = &clk_fin_epll,
	[3] = &clk_iis_cd0,
	[4] = &clk_pcm_cd,
};

static struct clksrc_sources clkset_audio0 = {
	.sources	= clkset_audio0_list,
	.nr_sources	= ARRAY_SIZE(clkset_audio0_list),
};

static struct clk *clkset_audio1_list[] = {
	[0] = &clk_mout_epll.clk,
	[1] = &clk_dout_mpll,
	[2] = &clk_fin_epll,
	[3] = &clk_iis_cd1,
	[4] = &clk_pcm_cd,
};

static struct clksrc_sources clkset_audio1 = {
	.sources	= clkset_audio1_list,
	.nr_sources	= ARRAY_SIZE(clkset_audio1_list),
};

static struct clk *clkset_camif_list[] = {
	&clk_h2,
};

static struct clksrc_sources clkset_camif = {
	.sources	= clkset_camif_list,
	.nr_sources	= ARRAY_SIZE(clkset_camif_list),
};

static struct clksrc_clk clksrcs[] = {
	{
		.clk	= {
			.name		= "mmc_bus",
			.id		= 0,
			.ctrlbit        = S3C_CLKCON_SCLK_MMC0,
			.enable		= s3c64xx_sclk_ctrl,
		},
		.reg_src	= { .reg = S3C_CLK_SRC, .shift = 18, .size = 2  },
		.reg_div	= { .reg = S3C_CLK_DIV1, .shift = 0, .size = 4  },
		.sources	= &clkset_spi_mmc,
	}, {
		.clk	= {
			.name		= "mmc_bus",
			.id		= 1,
			.ctrlbit        = S3C_CLKCON_SCLK_MMC1,
			.enable		= s3c64xx_sclk_ctrl,
		},
		.reg_src	= { .reg = S3C_CLK_SRC, .shift = 20, .size = 2  },
		.reg_div	= { .reg = S3C_CLK_DIV1, .shift = 4, .size = 4  },
		.sources	= &clkset_spi_mmc,
	}, {
		.clk	= {
			.name		= "mmc_bus",
			.id		= 2,
			.ctrlbit        = S3C_CLKCON_SCLK_MMC2,
			.enable		= s3c64xx_sclk_ctrl,
		},
		.reg_src	= { .reg = S3C_CLK_SRC, .shift = 22, .size = 2  },
		.reg_div 	= { .reg = S3C_CLK_DIV1, .shift = 8, .size = 4  },
		.sources	= &clkset_spi_mmc,
	}, {
		.clk	= {
			.name		= "usb-bus-host",
			.id		= -1,
			.ctrlbit        = S3C_CLKCON_SCLK_UHOST,
			.enable		= s3c64xx_sclk_ctrl,
		},
		.reg_src 	= { .reg = S3C_CLK_SRC, .shift = 5, .size = 2  },
		.reg_div	= { .reg = S3C_CLK_DIV1, .shift = 20, .size = 4  },
		.sources	= &clkset_uhost,
	}, {
		.clk	= {
			.name		= "uclk1",
			.id		= -1,
			.ctrlbit        = S3C_CLKCON_SCLK_UART,
			.enable		= s3c64xx_sclk_ctrl,
		},
		.reg_src	= { .reg = S3C_CLK_SRC, .shift = 13, .size = 1  },
		.reg_div	= { .reg = S3C_CLK_DIV2, .shift = 16, .size = 4  },
		.sources	= &clkset_uart,
	}, {
/* Where does UCLK0 come from? */
		.clk	= {
			.name		= "spi-bus",
			.id		= 0,
			.ctrlbit        = S3C_CLKCON_SCLK_SPI0,
			.enable		= s3c64xx_sclk_ctrl,
		},
		.reg_src	= { .reg = S3C_CLK_SRC, .shift = 14, .size = 2  },
		.reg_div	= { .reg = S3C_CLK_DIV2, .shift = 0, .size = 4  },
		.sources	= &clkset_spi_mmc,
	}, {
		.clk	= {
			.name		= "spi-bus",
			.id		= 1,
			.ctrlbit        = S3C_CLKCON_SCLK_SPI1,
			.enable		= s3c64xx_sclk_ctrl,
		},
		.reg_src	= { .reg = S3C_CLK_SRC, .shift = 16, .size = 2  },
		.reg_div	= { .reg = S3C_CLK_DIV2, .shift = 4, .size = 4  },
		.sources	= &clkset_spi_mmc,
	}, {
		.clk	= {
			.name		= "audio-bus",
			.id		= 0,
			.ctrlbit        = S3C_CLKCON_SCLK_AUDIO0,
			.enable		= s3c64xx_sclk_ctrl,
		},
		.reg_src	= { .reg = S3C_CLK_SRC, .shift = 7, .size = 3  },
		.reg_div	= { .reg = S3C_CLK_DIV2, .shift = 8, .size = 4  },
		.sources	= &clkset_audio0,
	}, {
		.clk	= {
			.name		= "audio-bus",
			.id		= 1,
			.ctrlbit        = S3C_CLKCON_SCLK_AUDIO1,
			.enable		= s3c64xx_sclk_ctrl,
		},
		.reg_src	= { .reg = S3C_CLK_SRC, .shift = 10, .size = 3  },
		.reg_div	= { .reg = S3C_CLK_DIV2, .shift = 12, .size = 4  },
		.sources	= &clkset_audio1,
	}, {
		.clk	= {
			.name		= "irda-bus",
			.id		= 0,
			.ctrlbit        = S3C_CLKCON_SCLK_IRDA,
			.enable		= s3c64xx_sclk_ctrl,
		},
		.reg_src	= { .reg = S3C_CLK_SRC, .shift = 24, .size = 2  },
		.reg_div	= { .reg = S3C_CLK_DIV2, .shift = 20, .size = 4  },
		.sources	= &clkset_irda,
	}, {
		.clk	= {
			.name		= "camera",
			.id		= -1,
			.ctrlbit        = S3C_CLKCON_SCLK_CAM,
			.enable		= s3c64xx_sclk_ctrl,
		},
		.reg_div	= { .reg = S3C_CLK_DIV0, .shift = 20, .size = 4  },
		.reg_src	= { .reg = NULL, .shift = 0, .size = 0  },
		.sources	= &clkset_camif,
	},
};

/* Clock initialisation code */

static struct clksrc_clk *init_parents[] = {
	&clk_mout_apll,
	&clk_mout_epll,
	&clk_mout_mpll,
};

#define GET_DIV(clk, field) ((((clk) & field##_MASK) >> field##_SHIFT) + 1)

void __init_or_cpufreq s3c6400_setup_clocks(void)
{
	struct clk *xtal_clk;
	unsigned long xtal;
	unsigned long fclk;
	unsigned long hclk;
	unsigned long hclk2;
	unsigned long pclk;
	unsigned long epll;
	unsigned long apll;
	unsigned long mpll;
	unsigned int ptr;
	u32 clkdiv0;

	printk(KERN_DEBUG "%s: registering clocks\n", __func__);

	clkdiv0 = __raw_readl(S3C_CLK_DIV0);
	printk(KERN_DEBUG "%s: clkdiv0 = %08x\n", __func__, clkdiv0);

	xtal_clk = clk_get(NULL, "xtal");
	BUG_ON(IS_ERR(xtal_clk));

	xtal = clk_get_rate(xtal_clk);
	clk_put(xtal_clk);

	printk(KERN_DEBUG "%s: xtal is %ld\n", __func__, xtal);

	/* For now assume the mux always selects the crystal */
	clk_ext_xtal_mux.parent = xtal_clk;

	epll = s3c6400_get_epll(xtal);
	mpll = s3c6400_get_pll(xtal, __raw_readl(S3C_MPLL_CON));
	apll = s3c6400_get_pll(xtal, __raw_readl(S3C_APLL_CON));

	fclk = mpll;

	printk(KERN_INFO "S3C64XX: PLL settings, A=%ld, M=%ld, E=%ld\n",
	       apll, mpll, epll);

	hclk2 = mpll / GET_DIV(clkdiv0, S3C6400_CLKDIV0_HCLK2);
	hclk = hclk2 / GET_DIV(clkdiv0, S3C6400_CLKDIV0_HCLK);
	pclk = hclk2 / GET_DIV(clkdiv0, S3C6400_CLKDIV0_PCLK);

	printk(KERN_INFO "S3C64XX: HCLK2=%ld, HCLK=%ld, PCLK=%ld\n",
	       hclk2, hclk, pclk);

	clk_fout_mpll.rate = mpll;
	clk_fout_epll.rate = epll;
	clk_fout_apll.rate = apll;

	clk_h2.rate = hclk2;
	clk_h.rate = hclk;
	clk_p.rate = pclk;
	clk_f.rate = fclk;

	for (ptr = 0; ptr < ARRAY_SIZE(init_parents); ptr++)
		s3c_set_clksrc(init_parents[ptr], true);

	for (ptr = 0; ptr < ARRAY_SIZE(clksrcs); ptr++)
		s3c_set_clksrc(&clksrcs[ptr], true);
}

static struct clk *clks[] __initdata = {
	&clk_ext_xtal_mux,
	&clk_iis_cd0,
	&clk_iis_cd1,
	&clk_pcm_cd,
	&clk_mout_epll.clk,
	&clk_mout_mpll.clk,
	&clk_dout_mpll,
	&clk_arm,
};

/**
 * s3c6400_register_clocks - register clocks for s3c6400 and above
 * @armclk_divlimit: Divisor mask for ARMCLK
 *
 * Register the clocks for the S3C6400 and above SoC range, such
 * as ARMCLK and the clocks which have divider chains attached.
 *
 * This call does not setup the clocks, which is left to the
 * s3c6400_setup_clocks() call which may be needed by the cpufreq
 * or resume code to re-set the clocks if the bootloader has changed
 * them.
 */
void __init s3c6400_register_clocks(unsigned armclk_divlimit)
{
	struct clk *clkp;
	int ret;
	int ptr;

	armclk_mask = armclk_divlimit;

	for (ptr = 0; ptr < ARRAY_SIZE(clks); ptr++) {
		clkp = clks[ptr];
		ret = s3c24xx_register_clock(clkp);
		if (ret < 0) {
			printk(KERN_ERR "Failed to register clock %s (%d)\n",
			       clkp->name, ret);
		}
	}

	s3c_register_clksrc(clksrcs, ARRAY_SIZE(clksrcs));
}
