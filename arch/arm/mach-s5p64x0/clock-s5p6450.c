/* linux/arch/arm/mach-s5p64x0/clock-s5p6450.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5P6450 - Clock support
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
#include <linux/device.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/s5p64x0-clock.h>

#include <plat/cpu-freq.h>
#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/pll.h>
#include <plat/s5p-clock.h>
#include <plat/clock-clksrc.h>

#include "common.h"

static struct clksrc_clk clk_mout_dpll = {
	.clk	= {
		.name		= "mout_dpll",
	},
	.sources	= &clk_src_dpll,
	.reg_src	= { .reg = S5P64X0_CLK_SRC0, .shift = 5, .size = 1 },
};

static u32 epll_div[][5] = {
	{ 133000000,	27307,	55, 2, 2 },
	{ 100000000,	43691,	41, 2, 2 },
	{ 480000000,	0,	80, 2, 0 },
};

static int s5p6450_epll_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned int epll_con, epll_con_k;
	unsigned int i;

	if (clk->rate == rate)	/* Return if nothing changed */
		return 0;

	epll_con = __raw_readl(S5P64X0_EPLL_CON);
	epll_con_k = __raw_readl(S5P64X0_EPLL_CON_K);

	epll_con_k &= ~(PLL90XX_KDIV_MASK);
	epll_con &= ~(PLL90XX_MDIV_MASK | PLL90XX_PDIV_MASK | PLL90XX_SDIV_MASK);

	for (i = 0; i < ARRAY_SIZE(epll_div); i++) {
		 if (epll_div[i][0] == rate) {
			epll_con_k |= (epll_div[i][1] << PLL90XX_KDIV_SHIFT);
			epll_con |= (epll_div[i][2] << PLL90XX_MDIV_SHIFT) |
				    (epll_div[i][3] << PLL90XX_PDIV_SHIFT) |
				    (epll_div[i][4] << PLL90XX_SDIV_SHIFT);
			break;
		}
	}

	if (i == ARRAY_SIZE(epll_div)) {
		printk(KERN_ERR "%s: Invalid Clock EPLL Frequency\n", __func__);
		return -EINVAL;
	}

	__raw_writel(epll_con, S5P64X0_EPLL_CON);
	__raw_writel(epll_con_k, S5P64X0_EPLL_CON_K);

	printk(KERN_WARNING "EPLL Rate changes from %lu to %lu\n",
			clk->rate, rate);

	clk->rate = rate;

	return 0;
}

static struct clk_ops s5p6450_epll_ops = {
	.get_rate = s5p_epll_get_rate,
	.set_rate = s5p6450_epll_set_rate,
};

static struct clksrc_clk clk_dout_epll = {
	.clk	= {
		.name		= "dout_epll",
		.parent		= &clk_mout_epll.clk,
	},
	.reg_div	= { .reg = S5P64X0_CLK_DIV1, .shift = 24, .size = 4 },
};

static struct clksrc_clk clk_mout_hclk_sel = {
	.clk	= {
		.name		= "mout_hclk_sel",
	},
	.sources	= &clkset_hclk_low,
	.reg_src	= { .reg = S5P64X0_OTHERS, .shift = 15, .size = 1 },
};

static struct clk *clkset_hclk_list[] = {
	&clk_mout_hclk_sel.clk,
	&clk_armclk.clk,
};

static struct clksrc_sources clkset_hclk = {
	.sources	= clkset_hclk_list,
	.nr_sources	= ARRAY_SIZE(clkset_hclk_list),
};

static struct clksrc_clk clk_hclk = {
	.clk	= {
		.name		= "clk_hclk",
	},
	.sources	= &clkset_hclk,
	.reg_src	= { .reg = S5P64X0_OTHERS, .shift = 14, .size = 1 },
	.reg_div	= { .reg = S5P64X0_CLK_DIV0, .shift = 8, .size = 4 },
};

static struct clksrc_clk clk_pclk = {
	.clk	= {
		.name		= "clk_pclk",
		.parent		= &clk_hclk.clk,
	},
	.reg_div = { .reg = S5P64X0_CLK_DIV0, .shift = 12, .size = 4 },
};
static struct clksrc_clk clk_dout_pwm_ratio0 = {
	.clk	= {
		.name		= "clk_dout_pwm_ratio0",
		.parent		= &clk_mout_hclk_sel.clk,
	},
	.reg_div	= { .reg = S5P64X0_CLK_DIV3, .shift = 16, .size = 4 },
};

static struct clksrc_clk clk_pclk_to_wdt_pwm = {
	.clk	= {
		.name		= "clk_pclk_to_wdt_pwm",
		.parent		= &clk_dout_pwm_ratio0.clk,
	},
	.reg_div	= { .reg = S5P64X0_CLK_DIV3, .shift = 20, .size = 4 },
};

static struct clksrc_clk clk_hclk_low = {
	.clk	= {
		.name		= "clk_hclk_low",
	},
	.sources	= &clkset_hclk_low,
	.reg_src	= { .reg = S5P64X0_OTHERS, .shift = 6, .size = 1 },
	.reg_div	= { .reg = S5P64X0_CLK_DIV3, .shift = 8, .size = 4 },
};

static struct clksrc_clk clk_pclk_low = {
	.clk	= {
		.name		= "clk_pclk_low",
		.parent		= &clk_hclk_low.clk,
	},
	.reg_div	= { .reg = S5P64X0_CLK_DIV3, .shift = 12, .size = 4 },
};

/*
 * The following clocks will be disabled during clock initialization. It is
 * recommended to keep the following clocks disabled until the driver requests
 * for enabling the clock.
 */
static struct clk init_clocks_off[] = {
	{
		.name		= "usbhost",
		.parent		= &clk_hclk_low.clk,
		.enable		= s5p64x0_hclk0_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "dma",
		.devname	= "dma-pl330",
		.parent		= &clk_hclk_low.clk,
		.enable		= s5p64x0_hclk0_ctrl,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "hsmmc",
		.devname	= "s3c-sdhci.0",
		.parent		= &clk_hclk_low.clk,
		.enable		= s5p64x0_hclk0_ctrl,
		.ctrlbit	= (1 << 17),
	}, {
		.name		= "hsmmc",
		.devname	= "s3c-sdhci.1",
		.parent		= &clk_hclk_low.clk,
		.enable		= s5p64x0_hclk0_ctrl,
		.ctrlbit	= (1 << 18),
	}, {
		.name		= "hsmmc",
		.devname	= "s3c-sdhci.2",
		.parent		= &clk_hclk_low.clk,
		.enable		= s5p64x0_hclk0_ctrl,
		.ctrlbit	= (1 << 19),
	}, {
		.name		= "usbotg",
		.parent		= &clk_hclk_low.clk,
		.enable		= s5p64x0_hclk0_ctrl,
		.ctrlbit	= (1 << 20),
	}, {
		.name		= "lcd",
		.parent		= &clk_h,
		.enable		= s5p64x0_hclk1_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "watchdog",
		.parent		= &clk_pclk_low.clk,
		.enable		= s5p64x0_pclk_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "rtc",
		.parent		= &clk_pclk_low.clk,
		.enable		= s5p64x0_pclk_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "adc",
		.parent		= &clk_pclk_low.clk,
		.enable		= s5p64x0_pclk_ctrl,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.0",
		.parent		= &clk_pclk_low.clk,
		.enable		= s5p64x0_pclk_ctrl,
		.ctrlbit	= (1 << 17),
	}, {
		.name		= "spi",
		.devname	= "s5p64x0-spi.0",
		.parent		= &clk_pclk_low.clk,
		.enable		= s5p64x0_pclk_ctrl,
		.ctrlbit	= (1 << 21),
	}, {
		.name		= "spi",
		.devname	= "s5p64x0-spi.1",
		.parent		= &clk_pclk_low.clk,
		.enable		= s5p64x0_pclk_ctrl,
		.ctrlbit	= (1 << 22),
	}, {
		.name		= "iis",
		.devname	= "samsung-i2s.0",
		.parent		= &clk_pclk_low.clk,
		.enable		= s5p64x0_pclk_ctrl,
		.ctrlbit	= (1 << 26),
	}, {
		.name		= "iis",
		.devname	= "samsung-i2s.1",
		.parent		= &clk_pclk_low.clk,
		.enable		= s5p64x0_pclk_ctrl,
		.ctrlbit	= (1 << 15),
	}, {
		.name		= "iis",
		.devname	= "samsung-i2s.2",
		.parent		= &clk_pclk_low.clk,
		.enable		= s5p64x0_pclk_ctrl,
		.ctrlbit	= (1 << 16),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.1",
		.parent		= &clk_pclk_low.clk,
		.enable		= s5p64x0_pclk_ctrl,
		.ctrlbit	= (1 << 27),
	}, {
		.name		= "dmc0",
		.parent		= &clk_pclk.clk,
		.enable		= s5p64x0_pclk_ctrl,
		.ctrlbit	= (1 << 30),
	}
};

/*
 * The following clocks will be enabled during clock initialization.
 */
static struct clk init_clocks[] = {
	{
		.name		= "intc",
		.parent		= &clk_hclk.clk,
		.enable		= s5p64x0_hclk0_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "mem",
		.parent		= &clk_hclk.clk,
		.enable		= s5p64x0_hclk0_ctrl,
		.ctrlbit	= (1 << 21),
	}, {
		.name		= "uart",
		.devname	= "s3c6400-uart.0",
		.parent		= &clk_pclk_low.clk,
		.enable		= s5p64x0_pclk_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "uart",
		.devname	= "s3c6400-uart.1",
		.parent		= &clk_pclk_low.clk,
		.enable		= s5p64x0_pclk_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "uart",
		.devname	= "s3c6400-uart.2",
		.parent		= &clk_pclk_low.clk,
		.enable		= s5p64x0_pclk_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "uart",
		.devname	= "s3c6400-uart.3",
		.parent		= &clk_pclk_low.clk,
		.enable		= s5p64x0_pclk_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "timers",
		.parent		= &clk_pclk_to_wdt_pwm.clk,
		.enable		= s5p64x0_pclk_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "gpio",
		.parent		= &clk_pclk_low.clk,
		.enable		= s5p64x0_pclk_ctrl,
		.ctrlbit	= (1 << 18),
	},
};

static struct clk *clkset_uart_list[] = {
	&clk_dout_epll.clk,
	&clk_dout_mpll.clk,
};

static struct clksrc_sources clkset_uart = {
	.sources	= clkset_uart_list,
	.nr_sources	= ARRAY_SIZE(clkset_uart_list),
};

static struct clk *clkset_mali_list[] = {
	&clk_mout_epll.clk,
	&clk_mout_apll.clk,
	&clk_mout_mpll.clk,
};

static struct clksrc_sources clkset_mali = {
	.sources	= clkset_mali_list,
	.nr_sources	= ARRAY_SIZE(clkset_mali_list),
};

static struct clk *clkset_group2_list[] = {
	&clk_dout_epll.clk,
	&clk_dout_mpll.clk,
	&clk_ext_xtal_mux,
};

static struct clksrc_sources clkset_group2 = {
	.sources	= clkset_group2_list,
	.nr_sources	= ARRAY_SIZE(clkset_group2_list),
};

static struct clk *clkset_dispcon_list[] = {
	&clk_dout_epll.clk,
	&clk_dout_mpll.clk,
	&clk_ext_xtal_mux,
	&clk_mout_dpll.clk,
};

static struct clksrc_sources clkset_dispcon = {
	.sources	= clkset_dispcon_list,
	.nr_sources	= ARRAY_SIZE(clkset_dispcon_list),
};

static struct clk *clkset_hsmmc44_list[] = {
	&clk_dout_epll.clk,
	&clk_dout_mpll.clk,
	&clk_ext_xtal_mux,
	&s5p_clk_27m,
	&clk_48m,
};

static struct clksrc_sources clkset_hsmmc44 = {
	.sources	= clkset_hsmmc44_list,
	.nr_sources	= ARRAY_SIZE(clkset_hsmmc44_list),
};

static struct clk *clkset_sclk_audio0_list[] = {
	[0] = &clk_dout_epll.clk,
	[1] = &clk_dout_mpll.clk,
	[2] = &clk_ext_xtal_mux,
	[3] = NULL,
	[4] = NULL,
};

static struct clksrc_sources clkset_sclk_audio0 = {
	.sources	= clkset_sclk_audio0_list,
	.nr_sources	= ARRAY_SIZE(clkset_sclk_audio0_list),
};

static struct clksrc_clk clk_sclk_audio0 = {
	.clk		= {
		.name		= "audio-bus",
		.enable		= s5p64x0_sclk_ctrl,
		.ctrlbit	= (1 << 8),
		.parent		= &clk_dout_epll.clk,
	},
	.sources	= &clkset_sclk_audio0,
	.reg_src	= { .reg = S5P64X0_CLK_SRC1, .shift = 10, .size = 3 },
	.reg_div	= { .reg = S5P64X0_CLK_DIV2, .shift = 8, .size = 4 },
};

static struct clksrc_clk clksrcs[] = {
	{
		.clk	= {
			.name		= "sclk_fimc",
			.ctrlbit	= (1 << 10),
			.enable		= s5p64x0_sclk_ctrl,
		},
		.sources = &clkset_group2,
		.reg_src = { .reg = S5P64X0_CLK_SRC0, .shift = 26, .size = 2 },
		.reg_div = { .reg = S5P64X0_CLK_DIV1, .shift = 12, .size = 4 },
	}, {
		.clk	= {
			.name		= "aclk_mali",
			.ctrlbit	= (1 << 2),
			.enable		= s5p64x0_sclk1_ctrl,
		},
		.sources = &clkset_mali,
		.reg_src = { .reg = S5P64X0_CLK_SRC1, .shift = 8, .size = 2 },
		.reg_div = { .reg = S5P64X0_CLK_DIV3, .shift = 4, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_2d",
			.ctrlbit	= (1 << 12),
			.enable		= s5p64x0_sclk_ctrl,
		},
		.sources = &clkset_mali,
		.reg_src = { .reg = S5P64X0_CLK_SRC0, .shift = 30, .size = 2 },
		.reg_div = { .reg = S5P64X0_CLK_DIV2, .shift = 20, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_usi",
			.ctrlbit	= (1 << 7),
			.enable		= s5p64x0_sclk_ctrl,
		},
		.sources = &clkset_group2,
		.reg_src = { .reg = S5P64X0_CLK_SRC0, .shift = 10, .size = 2 },
		.reg_div = { .reg = S5P64X0_CLK_DIV1, .shift = 16, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_camif",
			.ctrlbit	= (1 << 6),
			.enable		= s5p64x0_sclk_ctrl,
		},
		.sources = &clkset_group2,
		.reg_src = { .reg = S5P64X0_CLK_SRC0, .shift = 28, .size = 2 },
		.reg_div = { .reg = S5P64X0_CLK_DIV1, .shift = 20, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_dispcon",
			.ctrlbit	= (1 << 1),
			.enable		= s5p64x0_sclk1_ctrl,
		},
		.sources = &clkset_dispcon,
		.reg_src = { .reg = S5P64X0_CLK_SRC1, .shift = 4, .size = 2 },
		.reg_div = { .reg = S5P64X0_CLK_DIV3, .shift = 0, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_hsmmc44",
			.ctrlbit	= (1 << 30),
			.enable		= s5p64x0_sclk_ctrl,
		},
		.sources = &clkset_hsmmc44,
		.reg_src = { .reg = S5P64X0_CLK_SRC0, .shift = 6, .size = 3 },
		.reg_div = { .reg = S5P64X0_CLK_DIV1, .shift = 28, .size = 4 },
	},
};

static struct clksrc_clk clk_sclk_mmc0 = {
	.clk	= {
		.name		= "sclk_mmc",
		.devname	= "s3c-sdhci.0",
		.ctrlbit	= (1 << 24),
		.enable		= s5p64x0_sclk_ctrl,
	},
	.sources = &clkset_group2,
	.reg_src = { .reg = S5P64X0_CLK_SRC0, .shift = 18, .size = 2 },
	.reg_div = { .reg = S5P64X0_CLK_DIV1, .shift = 0, .size = 4 },
};

static struct clksrc_clk clk_sclk_mmc1 = {
	.clk	= {
		.name		= "sclk_mmc",
		.devname	= "s3c-sdhci.1",
		.ctrlbit	= (1 << 25),
		.enable		= s5p64x0_sclk_ctrl,
	},
	.sources = &clkset_group2,
	.reg_src = { .reg = S5P64X0_CLK_SRC0, .shift = 20, .size = 2 },
	.reg_div = { .reg = S5P64X0_CLK_DIV1, .shift = 4, .size = 4 },
};

static struct clksrc_clk clk_sclk_mmc2 = {
	.clk	= {
		.name		= "sclk_mmc",
		.devname	= "s3c-sdhci.2",
		.ctrlbit	= (1 << 26),
		.enable		= s5p64x0_sclk_ctrl,
	},
	.sources = &clkset_group2,
	.reg_src = { .reg = S5P64X0_CLK_SRC0, .shift = 22, .size = 2 },
	.reg_div = { .reg = S5P64X0_CLK_DIV1, .shift = 8, .size = 4 },
};

static struct clksrc_clk clk_sclk_uclk = {
	.clk	= {
		.name		= "uclk1",
		.ctrlbit	= (1 << 5),
		.enable		= s5p64x0_sclk_ctrl,
	},
	.sources = &clkset_uart,
	.reg_src = { .reg = S5P64X0_CLK_SRC0, .shift = 13, .size = 1 },
	.reg_div = { .reg = S5P64X0_CLK_DIV2, .shift = 16, .size = 4 },
};

static struct clksrc_clk clk_sclk_spi0 = {
	.clk	= {
		.name		= "sclk_spi",
		.devname	= "s5p64x0-spi.0",
		.ctrlbit	= (1 << 20),
		.enable		= s5p64x0_sclk_ctrl,
	},
	.sources = &clkset_group2,
	.reg_src = { .reg = S5P64X0_CLK_SRC0, .shift = 14, .size = 2 },
	.reg_div = { .reg = S5P64X0_CLK_DIV2, .shift = 0, .size = 4 },
};

static struct clksrc_clk clk_sclk_spi1 = {
	.clk	= {
		.name		= "sclk_spi",
		.devname	= "s5p64x0-spi.1",
		.ctrlbit	= (1 << 21),
		.enable		= s5p64x0_sclk_ctrl,
	},
	.sources = &clkset_group2,
	.reg_src = { .reg = S5P64X0_CLK_SRC0, .shift = 16, .size = 2 },
	.reg_div = { .reg = S5P64X0_CLK_DIV2, .shift = 4, .size = 4 },
};

static struct clksrc_clk *clksrc_cdev[] = {
	&clk_sclk_uclk,
	&clk_sclk_spi0,
	&clk_sclk_spi1,
	&clk_sclk_mmc0,
	&clk_sclk_mmc1,
	&clk_sclk_mmc2,
};

static struct clk_lookup s5p6450_clk_lookup[] = {
	CLKDEV_INIT(NULL, "clk_uart_baud2", &clk_pclk_low.clk),
	CLKDEV_INIT(NULL, "clk_uart_baud3", &clk_sclk_uclk.clk),
	CLKDEV_INIT(NULL, "spi_busclk0", &clk_p),
	CLKDEV_INIT("s5p64x0-spi.0", "spi_busclk1", &clk_sclk_spi0.clk),
	CLKDEV_INIT("s5p64x0-spi.1", "spi_busclk1", &clk_sclk_spi1.clk),
	CLKDEV_INIT("s3c-sdhci.0", "mmc_busclk.2", &clk_sclk_mmc0.clk),
	CLKDEV_INIT("s3c-sdhci.1", "mmc_busclk.2", &clk_sclk_mmc1.clk),
	CLKDEV_INIT("s3c-sdhci.2", "mmc_busclk.2", &clk_sclk_mmc2.clk),
};

/* Clock initialization code */
static struct clksrc_clk *sysclks[] = {
	&clk_mout_apll,
	&clk_mout_epll,
	&clk_dout_epll,
	&clk_mout_mpll,
	&clk_dout_mpll,
	&clk_armclk,
	&clk_mout_hclk_sel,
	&clk_dout_pwm_ratio0,
	&clk_pclk_to_wdt_pwm,
	&clk_hclk,
	&clk_pclk,
	&clk_hclk_low,
	&clk_pclk_low,
	&clk_sclk_audio0,
};

static struct clk dummy_apb_pclk = {
	.name		= "apb_pclk",
	.id		= -1,
};

void __init_or_cpufreq s5p6450_setup_clocks(void)
{
	struct clk *xtal_clk;

	unsigned long xtal;
	unsigned long fclk;
	unsigned long hclk;
	unsigned long hclk_low;
	unsigned long pclk;
	unsigned long pclk_low;

	unsigned long apll;
	unsigned long mpll;
	unsigned long epll;
	unsigned long dpll;
	unsigned int ptr;

	/* Set S5P6450 functions for clk_fout_epll */

	clk_fout_epll.enable = s5p_epll_enable;
	clk_fout_epll.ops = &s5p6450_epll_ops;

	clk_48m.enable = s5p64x0_clk48m_ctrl;

	xtal_clk = clk_get(NULL, "ext_xtal");
	BUG_ON(IS_ERR(xtal_clk));

	xtal = clk_get_rate(xtal_clk);
	clk_put(xtal_clk);

	apll = s5p_get_pll45xx(xtal, __raw_readl(S5P64X0_APLL_CON), pll_4502);
	mpll = s5p_get_pll45xx(xtal, __raw_readl(S5P64X0_MPLL_CON), pll_4502);
	epll = s5p_get_pll90xx(xtal, __raw_readl(S5P64X0_EPLL_CON),
				__raw_readl(S5P64X0_EPLL_CON_K));
	dpll = s5p_get_pll46xx(xtal, __raw_readl(S5P6450_DPLL_CON),
				__raw_readl(S5P6450_DPLL_CON_K), pll_4650c);

	clk_fout_apll.rate = apll;
	clk_fout_mpll.rate = mpll;
	clk_fout_epll.rate = epll;
	clk_fout_dpll.rate = dpll;

	printk(KERN_INFO "S5P6450: PLL settings, A=%ld.%ldMHz, M=%ld.%ldMHz," \
			" E=%ld.%ldMHz, D=%ld.%ldMHz\n",
			print_mhz(apll), print_mhz(mpll), print_mhz(epll),
			print_mhz(dpll));

	fclk = clk_get_rate(&clk_armclk.clk);
	hclk = clk_get_rate(&clk_hclk.clk);
	pclk = clk_get_rate(&clk_pclk.clk);
	hclk_low = clk_get_rate(&clk_hclk_low.clk);
	pclk_low = clk_get_rate(&clk_pclk_low.clk);

	printk(KERN_INFO "S5P6450: HCLK=%ld.%ldMHz, HCLK_LOW=%ld.%ldMHz," \
			" PCLK=%ld.%ldMHz, PCLK_LOW=%ld.%ldMHz\n",
			print_mhz(hclk), print_mhz(hclk_low),
			print_mhz(pclk), print_mhz(pclk_low));

	clk_f.rate = fclk;
	clk_h.rate = hclk;
	clk_p.rate = pclk;

	for (ptr = 0; ptr < ARRAY_SIZE(clksrcs); ptr++)
		s3c_set_clksrc(&clksrcs[ptr], true);
}

void __init s5p6450_register_clocks(void)
{
	int ptr;

	for (ptr = 0; ptr < ARRAY_SIZE(sysclks); ptr++)
		s3c_register_clksrc(sysclks[ptr], 1);

	s3c_register_clksrc(clksrcs, ARRAY_SIZE(clksrcs));
	s3c_register_clocks(init_clocks, ARRAY_SIZE(init_clocks));
	for (ptr = 0; ptr < ARRAY_SIZE(clksrc_cdev); ptr++)
		s3c_register_clksrc(clksrc_cdev[ptr], 1);

	s3c_register_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));
	s3c_disable_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));
	clkdev_add_table(s5p6450_clk_lookup, ARRAY_SIZE(s5p6450_clk_lookup));

	s3c24xx_register_clock(&dummy_apb_pclk);

	s3c_pwmclk_init();
}
