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

static int s3c2443_gate(void __iomem *reg, struct clk *clk, int enable)
{
	u32 ctrlbit = clk->ctrlbit;
	u32 con = __raw_readl(reg);

	if (enable)
		con |= ctrlbit;
	else
		con &= ~ctrlbit;

	__raw_writel(con, reg);
	return 0;
}

static int s3c2443_clkcon_enable_h(struct clk *clk, int enable)
{
	return s3c2443_gate(S3C2443_HCLKCON, clk, enable);
}

static int s3c2443_clkcon_enable_p(struct clk *clk, int enable)
{
	return s3c2443_gate(S3C2443_PCLKCON, clk, enable);
}

static int s3c2443_clkcon_enable_s(struct clk *clk, int enable)
{
	return s3c2443_gate(S3C2443_SCLKCON, clk, enable);
}

/* clock selections */

/* mpllref is a direct descendant of clk_xtal by default, but it is not
 * elided as the EPLL can be either sourced by the XTAL or EXTCLK and as
 * such directly equating the two source clocks is impossible.
 */
static struct clk clk_mpllref = {
	.name		= "mpllref",
	.parent		= &clk_xtal,
	.id		= -1,
};

static struct clk clk_i2s_ext = {
	.name		= "i2s-ext",
	.id		= -1,
};

static struct clk *clk_epllref_sources[] = {
	[0] = &clk_mpllref,
	[1] = &clk_mpllref,
	[2] = &clk_xtal,
	[3] = &clk_ext,
};

static struct clksrc_clk clk_epllref = {
	.clk	= {
		.name		= "epllref",
		.id		= -1,
	},
	.sources = &(struct clksrc_sources) {
		.sources = clk_epllref_sources,
		.nr_sources = ARRAY_SIZE(clk_epllref_sources),
	},
	.reg_src = { .reg = S3C2443_CLKSRC, .size = 2, .shift = 7 },
};

static unsigned long s3c2443_getrate_mdivclk(struct clk *clk)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long div = __raw_readl(S3C2443_CLKDIV0);

	div  &= S3C2443_CLKDIV0_EXTDIV_MASK;
	div >>= (S3C2443_CLKDIV0_EXTDIV_SHIFT-1);	/* x2 */

	return parent_rate / (div + 1);
}

static struct clk clk_mdivclk = {
	.name		= "mdivclk",
	.parent		= &clk_mpllref,
	.id		= -1,
	.ops		= &(struct clk_ops) {
		.get_rate	= s3c2443_getrate_mdivclk,
	},
};

static struct clk *clk_msysclk_sources[] = {
	[0] = &clk_mpllref,
	[1] = &clk_mpll,
	[2] = &clk_mdivclk,
	[3] = &clk_mpllref,
};

static struct clksrc_clk clk_msysclk = {
	.clk	= {
		.name		= "msysclk",
		.parent		= &clk_xtal,
		.id		= -1,
	},
	.sources = &(struct clksrc_sources) {
		.sources = clk_msysclk_sources,
		.nr_sources = ARRAY_SIZE(clk_msysclk_sources),
	},
	.reg_src = { .reg = S3C2443_CLKSRC, .size = 2, .shift = 3 },
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
	.id		= -1,
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
		.id		= -1,
	},
	.sources = &(struct clksrc_sources) {
		.sources = clk_arm_sources,
		.nr_sources = ARRAY_SIZE(clk_arm_sources),
	},
	.reg_src = { .reg = S3C2443_CLKDIV0, .size = 1, .shift = 13 },
};

/* esysclk
 *
 * this is sourced from either the EPLL or the EPLLref clock
*/

static struct clk *clk_sysclk_sources[] = {
	[0] = &clk_epllref.clk,
	[1] = &clk_epll,
};

static struct clksrc_clk clk_esysclk = {
	.clk	= {
		.name		= "esysclk",
		.parent		= &clk_epll,
		.id		= -1,
	},
	.sources = &(struct clksrc_sources) {
		.sources = clk_sysclk_sources,
		.nr_sources = ARRAY_SIZE(clk_sysclk_sources),
	},
	.reg_src = { .reg = S3C2443_CLKSRC, .size = 1, .shift = 6 },
};

/* uartclk
 *
 * UART baud-rate clock sourced from esysclk via a divisor
*/

static struct clksrc_clk clk_uart = {
	.clk	= {
		.name		= "uartclk",
		.id		= -1,
		.parent		= &clk_esysclk.clk,
	},
	.reg_div = { .reg = S3C2443_CLKDIV1, .size = 4, .shift = 8 },
};


/* hsspi
 *
 * high-speed spi clock, sourced from esysclk
*/

static struct clksrc_clk clk_hsspi = {
	.clk	= {
		.name		= "hsspi",
		.id		= -1,
		.parent		= &clk_esysclk.clk,
		.ctrlbit	= S3C2443_SCLKCON_HSSPICLK,
		.enable		= s3c2443_clkcon_enable_s,
	},
	.reg_div = { .reg = S3C2443_CLKDIV1, .size = 2, .shift = 4 },
};

/* usbhost
 *
 * usb host bus-clock, usually 48MHz to provide USB bus clock timing
*/

static struct clksrc_clk clk_usb_bus_host = {
	.clk	= {
		.name		= "usb-bus-host-parent",
		.id		= -1,
		.parent		= &clk_esysclk.clk,
		.ctrlbit	= S3C2443_SCLKCON_USBHOST,
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
		.id		= -1,
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
	.id		= -1,
	.parent		= &clk_hsmmc_div.clk,
	.enable		= s3c2443_enable_hsmmc,
	.ops		= &(struct clk_ops) {
		.set_parent	= s3c2443_setparent_hsmmc,
	},
};

/* i2s_eplldiv
 *
 * This clock is the output from the I2S divisor of ESYSCLK, and is seperate
 * from the mux that comes after it (cannot merge into one single clock)
*/

static struct clksrc_clk clk_i2s_eplldiv = {
	.clk	= {
		.name		= "i2s-eplldiv",
		.id		= -1,
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
		.id		= -1,
		.ctrlbit	= S3C2443_SCLKCON_I2SCLK,
		.enable		= s3c2443_clkcon_enable_s,

	},
	.sources = &(struct clksrc_sources) {
		.sources = clk_i2s_srclist,
		.nr_sources = ARRAY_SIZE(clk_i2s_srclist),
	},
	.reg_src = { .reg = S3C2443_CLKSRC, .size = 2, .shift = 14 },
};

/* cam-if
 *
 * camera interface bus-clock, divided down from esysclk
*/

static struct clksrc_clk clk_cam = {
	.clk	= {
		.name		= "camif-upll",	/* same as 2440 name */
		.id		= -1,
		.parent		= &clk_esysclk.clk,
		.ctrlbit	= S3C2443_SCLKCON_CAMCLK,
		.enable		= s3c2443_clkcon_enable_s,
	},
	.reg_div = { .reg = S3C2443_CLKDIV1, .size = 4, .shift = 26 },
};

/* display-if
 *
 * display interface clock, divided from esysclk
*/

static struct clksrc_clk clk_display = {
	.clk	= {
		.name		= "display-if",
		.id		= -1,
		.parent		= &clk_esysclk.clk,
		.ctrlbit	= S3C2443_SCLKCON_DISPCLK,
		.enable		= s3c2443_clkcon_enable_s,
	},
	.reg_div = { .reg = S3C2443_CLKDIV1, .size = 8, .shift = 16 },
};

/* prediv
 *
 * this divides the msysclk down to pass to h/p/etc.
 */

static unsigned long s3c2443_prediv_getrate(struct clk *clk)
{
	unsigned long rate = clk_get_rate(clk->parent);
	unsigned long clkdiv0 = __raw_readl(S3C2443_CLKDIV0);

	clkdiv0 &= S3C2443_CLKDIV0_PREDIV_MASK;
	clkdiv0 >>= S3C2443_CLKDIV0_PREDIV_SHIFT;

	return rate / (clkdiv0 + 1);
}

static struct clk clk_prediv = {
	.name		= "prediv",
	.id		= -1,
	.parent		= &clk_msysclk.clk,
	.ops		= &(struct clk_ops) {
		.get_rate	= s3c2443_prediv_getrate,
	},
};

/* standard clock definitions */

static struct clk init_clocks_disable[] = {
	{
		.name		= "nand",
		.id		= -1,
		.parent		= &clk_h,
	}, {
		.name		= "sdi",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_SDI,
	}, {
		.name		= "adc",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_ADC,
	}, {
		.name		= "i2c",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_IIC,
	}, {
		.name		= "iis",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_IIS,
	}, {
		.name		= "spi",
		.id		= 0,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_SPI0,
	}, {
		.name		= "spi",
		.id		= 1,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_SPI1,
	}
};

static struct clk init_clocks[] = {
	{
		.name		= "dma",
		.id		= 0,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_DMA0,
	}, {
		.name		= "dma",
		.id		= 1,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_DMA1,
	}, {
		.name		= "dma",
		.id		= 2,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_DMA2,
	}, {
		.name		= "dma",
		.id		= 3,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_DMA3,
	}, {
		.name		= "dma",
		.id		= 4,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_DMA4,
	}, {
		.name		= "dma",
		.id		= 5,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_DMA5,
	}, {
		.name		= "lcd",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_LCDC,
	}, {
		.name		= "gpio",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_GPIO,
	}, {
		.name		= "usb-host",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_USBH,
	}, {
		.name		= "usb-device",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_USBD,
	}, {
		.name		= "hsmmc",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_HSMMC,
	}, {
		.name		= "cfc",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_CFC,
	}, {
		.name		= "ssmc",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_SSMC,
	}, {
		.name		= "timers",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_PWMT,
	}, {
		.name		= "uart",
		.id		= 0,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_UART0,
	}, {
		.name		= "uart",
		.id		= 1,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_UART1,
	}, {
		.name		= "uart",
		.id		= 2,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_UART2,
	}, {
		.name		= "uart",
		.id		= 3,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_UART3,
	}, {
		.name		= "rtc",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_RTC,
	}, {
		.name		= "watchdog",
		.id		= -1,
		.parent		= &clk_p,
		.ctrlbit	= S3C2443_PCLKCON_WDT,
	}, {
		.name		= "usb-bus-host",
		.id		= -1,
		.parent		= &clk_usb_bus_host.clk,
	}, {
		.name		= "ac97",
		.id		= -1,
		.parent		= &clk_p,
		.ctrlbit	= S3C2443_PCLKCON_AC97,
	}
};

/* clocks to add where we need to check their parentage */

static struct clksrc_clk __initdata *init_list[] = {
	&clk_epllref, /* should be first */
	&clk_esysclk,
	&clk_msysclk,
	&clk_arm,
	&clk_i2s_eplldiv,
	&clk_i2s,
	&clk_cam,
	&clk_uart,
	&clk_display,
	&clk_hsmmc_div,
	&clk_usb_bus_host,
};

static void __init s3c2443_clk_initparents(void)
{
	int ptr;

	for (ptr = 0; ptr < ARRAY_SIZE(init_list); ptr++)
		s3c_set_clksrc(init_list[ptr], true);
}

static inline unsigned long s3c2443_get_hdiv(unsigned long clkcon0)
{
	clkcon0 &= S3C2443_CLKDIV0_HCLKDIV_MASK;

	return clkcon0 + 1;
}

/* clocks to add straight away */

static struct clksrc_clk *clksrcs[] __initdata = {
	&clk_usb_bus_host,
	&clk_epllref,
	&clk_esysclk,
	&clk_msysclk,
	&clk_arm,
	&clk_uart,
	&clk_display,
	&clk_cam,
	&clk_i2s_eplldiv,
	&clk_i2s,
	&clk_hsspi,
	&clk_hsmmc_div,
};

static struct clk *clks[] __initdata = {
	&clk_ext,
	&clk_epll,
	&clk_usb_bus,
	&clk_mpllref,
	&clk_hsmmc,
	&clk_armdiv,
	&clk_prediv,
};

void __init_or_cpufreq s3c2443_setup_clocks(void)
{
	unsigned long mpllcon = __raw_readl(S3C2443_MPLLCON);
	unsigned long clkdiv0 = __raw_readl(S3C2443_CLKDIV0);
	struct clk *xtal_clk;
	unsigned long xtal;
	unsigned long pll;
	unsigned long fclk;
	unsigned long hclk;
	unsigned long pclk;

	xtal_clk = clk_get(NULL, "xtal");
	xtal = clk_get_rate(xtal_clk);
	clk_put(xtal_clk);

	pll = s3c2443_get_mpll(mpllcon, xtal);
	clk_msysclk.clk.rate = pll;

	fclk = pll / s3c2443_fclk_div(clkdiv0);
	hclk = s3c2443_prediv_getrate(&clk_prediv);
	hclk /= s3c2443_get_hdiv(clkdiv0);
 	pclk = hclk / ((clkdiv0 & S3C2443_CLKDIV0_HALF_PCLK) ? 2 : 1);

	s3c24xx_setup_clocks(fclk, hclk, pclk);

	printk("S3C2443: mpll %s %ld.%03ld MHz, cpu %ld.%03ld MHz, mem %ld.%03ld MHz, pclk %ld.%03ld MHz\n",
	       (mpllcon & S3C2443_PLLCON_OFF) ? "off":"on",
	       print_mhz(pll), print_mhz(fclk),
	       print_mhz(hclk), print_mhz(pclk));

	s3c24xx_setup_clocks(fclk, hclk, pclk);
}

void __init s3c2443_init_clocks(int xtal)
{
	struct clk *clkp;
	unsigned long epllcon = __raw_readl(S3C2443_EPLLCON);
	int ret;
	int ptr;

	/* s3c2443 parents h and p clocks from prediv */
	clk_h.parent = &clk_prediv;
	clk_p.parent = &clk_prediv;

	s3c24xx_register_baseclocks(xtal);
	s3c2443_setup_clocks();
	s3c2443_clk_initparents();

	for (ptr = 0; ptr < ARRAY_SIZE(clks); ptr++) {
		clkp = clks[ptr];

		ret = s3c24xx_register_clock(clkp);
		if (ret < 0) {
			printk(KERN_ERR "Failed to register clock %s (%d)\n",
			       clkp->name, ret);
		}
	}

	for (ptr = 0; ptr < ARRAY_SIZE(clksrcs); ptr++)
		s3c_register_clksrc(clksrcs[ptr], 1);

	clk_epll.rate = s3c2443_get_epll(epllcon, xtal);
	clk_epll.parent = &clk_epllref.clk;
	clk_usb_bus.parent = &clk_usb_bus_host.clk;

	/* ensure usb bus clock is within correct rate of 48MHz */

	if (clk_get_rate(&clk_usb_bus_host.clk) != (48 * 1000 * 1000)) {
		printk(KERN_INFO "Warning: USB host bus not at 48MHz\n");
		clk_set_rate(&clk_usb_bus_host.clk, 48*1000*1000);
	}

	printk("S3C2443: epll %s %ld.%03ld MHz, usb-bus %ld.%03ld MHz\n",
	       (epllcon & S3C2443_PLLCON_OFF) ? "off":"on",
	       print_mhz(clk_get_rate(&clk_epll)),
	       print_mhz(clk_get_rate(&clk_usb_bus)));

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

	clkp = init_clocks_disable;
	for (ptr = 0; ptr < ARRAY_SIZE(init_clocks_disable); ptr++, clkp++) {

		ret = s3c24xx_register_clock(clkp);
		if (ret < 0) {
			printk(KERN_ERR "Failed to register clock %s (%d)\n",
			       clkp->name, ret);
		}

		(clkp->enable)(clkp, 0);
	}

	s3c_pwmclk_init();
}
