/* linux/arch/arm/plat-s3c64xx/clock.c
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * S3C64XX Base clock support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/map.h>

#include <mach/regs-sys.h>
#include <mach/regs-clock.h>
#include <mach/pll.h>

#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/cpu-freq.h>
#include <plat/clock.h>
#include <plat/clock-clksrc.h>

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

struct clk clk_h2 = {
	.name		= "hclk2",
	.id		= -1,
	.rate		= 0,
};

struct clk clk_27m = {
	.name		= "clk_27m",
	.id		= -1,
	.rate		= 27000000,
};

static int clk_48m_ctrl(struct clk *clk, int enable)
{
	unsigned long flags;
	u32 val;

	/* can't rely on clock lock, this register has other usages */
	local_irq_save(flags);

	val = __raw_readl(S3C64XX_OTHERS);
	if (enable)
		val |= S3C64XX_OTHERS_USBMASK;
	else
		val &= ~S3C64XX_OTHERS_USBMASK;

	__raw_writel(val, S3C64XX_OTHERS);
	local_irq_restore(flags);

	return 0;
}

struct clk clk_48m = {
	.name		= "clk_48m",
	.id		= -1,
	.rate		= 48000000,
	.enable		= clk_48m_ctrl,
};

struct clk clk_xusbxti = {
	.name		= "xusbxti",
	.id		= -1,
	.rate		= 48000000,
};

static int inline s3c64xx_gate(void __iomem *reg,
				struct clk *clk,
				int enable)
{
	unsigned int ctrlbit = clk->ctrlbit;
	u32 con;

	con = __raw_readl(reg);

	if (enable)
		con |= ctrlbit;
	else
		con &= ~ctrlbit;

	__raw_writel(con, reg);
	return 0;
}

static int s3c64xx_pclk_ctrl(struct clk *clk, int enable)
{
	return s3c64xx_gate(S3C_PCLK_GATE, clk, enable);
}

static int s3c64xx_hclk_ctrl(struct clk *clk, int enable)
{
	return s3c64xx_gate(S3C_HCLK_GATE, clk, enable);
}

int s3c64xx_sclk_ctrl(struct clk *clk, int enable)
{
	return s3c64xx_gate(S3C_SCLK_GATE, clk, enable);
}

static struct clk init_clocks_off[] = {
	{
		.name		= "nand",
		.id		= -1,
		.parent		= &clk_h,
	}, {
		.name		= "rtc",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c64xx_pclk_ctrl,
		.ctrlbit	= S3C_CLKCON_PCLK_RTC,
	}, {
		.name		= "adc",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c64xx_pclk_ctrl,
		.ctrlbit	= S3C_CLKCON_PCLK_TSADC,
	}, {
		.name		= "i2c",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c64xx_pclk_ctrl,
		.ctrlbit	= S3C_CLKCON_PCLK_IIC,
	}, {
		.name		= "iis",
		.id		= 0,
		.parent		= &clk_p,
		.enable		= s3c64xx_pclk_ctrl,
		.ctrlbit	= S3C_CLKCON_PCLK_IIS0,
	}, {
		.name		= "iis",
		.id		= 1,
		.parent		= &clk_p,
		.enable		= s3c64xx_pclk_ctrl,
		.ctrlbit	= S3C_CLKCON_PCLK_IIS1,
	}, {
#ifdef CONFIG_CPU_S3C6410
		.name		= "iis",
		.id		= -1,  /* There's only one IISv4 port */
		.parent		= &clk_p,
		.enable		= s3c64xx_pclk_ctrl,
		.ctrlbit	= S3C6410_CLKCON_PCLK_IIS2,
	}, {
#endif
		.name		= "keypad",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c64xx_pclk_ctrl,
		.ctrlbit	= S3C_CLKCON_PCLK_KEYPAD,
	}, {
		.name		= "spi",
		.id		= 0,
		.parent		= &clk_p,
		.enable		= s3c64xx_pclk_ctrl,
		.ctrlbit	= S3C_CLKCON_PCLK_SPI0,
	}, {
		.name		= "spi",
		.id		= 1,
		.parent		= &clk_p,
		.enable		= s3c64xx_pclk_ctrl,
		.ctrlbit	= S3C_CLKCON_PCLK_SPI1,
	}, {
		.name		= "spi_48m",
		.id		= 0,
		.parent		= &clk_48m,
		.enable		= s3c64xx_sclk_ctrl,
		.ctrlbit	= S3C_CLKCON_SCLK_SPI0_48,
	}, {
		.name		= "spi_48m",
		.id		= 1,
		.parent		= &clk_48m,
		.enable		= s3c64xx_sclk_ctrl,
		.ctrlbit	= S3C_CLKCON_SCLK_SPI1_48,
	}, {
		.name		= "48m",
		.id		= 0,
		.parent		= &clk_48m,
		.enable		= s3c64xx_sclk_ctrl,
		.ctrlbit	= S3C_CLKCON_SCLK_MMC0_48,
	}, {
		.name		= "48m",
		.id		= 1,
		.parent		= &clk_48m,
		.enable		= s3c64xx_sclk_ctrl,
		.ctrlbit	= S3C_CLKCON_SCLK_MMC1_48,
	}, {
		.name		= "48m",
		.id		= 2,
		.parent		= &clk_48m,
		.enable		= s3c64xx_sclk_ctrl,
		.ctrlbit	= S3C_CLKCON_SCLK_MMC2_48,
	}, {
		.name		= "dma0",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c64xx_hclk_ctrl,
		.ctrlbit	= S3C_CLKCON_HCLK_DMA0,
	}, {
		.name		= "dma1",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c64xx_hclk_ctrl,
		.ctrlbit	= S3C_CLKCON_HCLK_DMA1,
	},
};

static struct clk init_clocks[] = {
	{
		.name		= "lcd",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c64xx_hclk_ctrl,
		.ctrlbit	= S3C_CLKCON_HCLK_LCD,
	}, {
		.name		= "gpio",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c64xx_pclk_ctrl,
		.ctrlbit	= S3C_CLKCON_PCLK_GPIO,
	}, {
		.name		= "usb-host",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c64xx_hclk_ctrl,
		.ctrlbit	= S3C_CLKCON_HCLK_UHOST,
	}, {
		.name		= "hsmmc",
		.id		= 0,
		.parent		= &clk_h,
		.enable		= s3c64xx_hclk_ctrl,
		.ctrlbit	= S3C_CLKCON_HCLK_HSMMC0,
	}, {
		.name		= "hsmmc",
		.id		= 1,
		.parent		= &clk_h,
		.enable		= s3c64xx_hclk_ctrl,
		.ctrlbit	= S3C_CLKCON_HCLK_HSMMC1,
	}, {
		.name		= "hsmmc",
		.id		= 2,
		.parent		= &clk_h,
		.enable		= s3c64xx_hclk_ctrl,
		.ctrlbit	= S3C_CLKCON_HCLK_HSMMC2,
	}, {
		.name		= "otg",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c64xx_hclk_ctrl,
		.ctrlbit	= S3C_CLKCON_HCLK_USB,
	}, {
		.name		= "timers",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c64xx_pclk_ctrl,
		.ctrlbit	= S3C_CLKCON_PCLK_PWM,
	}, {
		.name		= "uart",
		.id		= 0,
		.parent		= &clk_p,
		.enable		= s3c64xx_pclk_ctrl,
		.ctrlbit	= S3C_CLKCON_PCLK_UART0,
	}, {
		.name		= "uart",
		.id		= 1,
		.parent		= &clk_p,
		.enable		= s3c64xx_pclk_ctrl,
		.ctrlbit	= S3C_CLKCON_PCLK_UART1,
	}, {
		.name		= "uart",
		.id		= 2,
		.parent		= &clk_p,
		.enable		= s3c64xx_pclk_ctrl,
		.ctrlbit	= S3C_CLKCON_PCLK_UART2,
	}, {
		.name		= "uart",
		.id		= 3,
		.parent		= &clk_p,
		.enable		= s3c64xx_pclk_ctrl,
		.ctrlbit	= S3C_CLKCON_PCLK_UART3,
	}, {
		.name		= "watchdog",
		.id		= -1,
		.parent		= &clk_p,
		.ctrlbit	= S3C_CLKCON_PCLK_WDT,
	}, {
		.name		= "ac97",
		.id		= -1,
		.parent		= &clk_p,
		.ctrlbit	= S3C_CLKCON_PCLK_AC97,
	}, {
		.name		= "cfcon",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c64xx_hclk_ctrl,
		.ctrlbit	= S3C_CLKCON_HCLK_IHOST,
	}
};


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

static struct clk clk_iisv4_cd = {
	.name		= "iis_cdclk_v4",
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

static struct clk *clkset_audio2_list[] = {
	[0] = &clk_mout_epll.clk,
	[1] = &clk_dout_mpll,
	[2] = &clk_fin_epll,
	[3] = &clk_iisv4_cd,
	[4] = &clk_pcm_cd,
};

static struct clksrc_sources clkset_audio2 = {
	.sources	= clkset_audio2_list,
	.nr_sources	= ARRAY_SIZE(clkset_audio2_list),
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
			.name		= "audio-bus",
			.id		= 2,
			.ctrlbit        = S3C6410_CLKCON_SCLK_AUDIO2,
			.enable		= s3c64xx_sclk_ctrl,
		},
		.reg_src	= { .reg = S3C6410_CLK_SRC2, .shift = 0, .size = 3  },
		.reg_div	= { .reg = S3C_CLK_DIV2, .shift = 24, .size = 4  },
		.sources	= &clkset_audio2,
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

static struct clk *clks1[] __initdata = {
	&clk_ext_xtal_mux,
	&clk_iis_cd0,
	&clk_iis_cd1,
	&clk_iisv4_cd,
	&clk_pcm_cd,
	&clk_mout_epll.clk,
	&clk_mout_mpll.clk,
	&clk_dout_mpll,
	&clk_arm,
};

static struct clk *clks[] __initdata = {
	&clk_ext,
	&clk_epll,
	&clk_27m,
	&clk_48m,
	&clk_h2,
	&clk_xusbxti,
};

/**
 * s3c64xx_register_clocks - register clocks for s3c6400 and s3c6410
 * @xtal: The rate for the clock crystal feeding the PLLs.
 * @armclk_divlimit: Divisor mask for ARMCLK.
 *
 * Register the clocks for the S3C6400 and S3C6410 SoC range, such
 * as ARMCLK as well as the necessary parent clocks.
 *
 * This call does not setup the clocks, which is left to the
 * s3c6400_setup_clocks() call which may be needed by the cpufreq
 * or resume code to re-set the clocks if the bootloader has changed
 * them.
 */
void __init s3c64xx_register_clocks(unsigned long xtal, 
				    unsigned armclk_divlimit)
{
	armclk_mask = armclk_divlimit;

	s3c24xx_register_baseclocks(xtal);
	s3c24xx_register_clocks(clks, ARRAY_SIZE(clks));

	s3c_register_clocks(init_clocks, ARRAY_SIZE(init_clocks));

	s3c_register_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));
	s3c_disable_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));

	s3c24xx_register_clocks(clks1, ARRAY_SIZE(clks1));
	s3c_register_clksrc(clksrcs, ARRAY_SIZE(clksrcs));
	s3c_pwmclk_init();
}
