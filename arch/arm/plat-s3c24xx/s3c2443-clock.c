/* linux/arch/arm/plat-s3c24xx/s3c2443-clock.c
 *
 * Copyright (c) 2007, 2010 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2443 Clock control suport - common code
 */

#include <linux/init.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <mach/regs-s3c2443-clock.h>

#include <plat/s3c2443.h>
#include <plat/clock.h>
#include <plat/clock-clksrc.h>
#include <plat/cpu.h>

#include <plat/cpu-freq.h>


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

int s3c2443_clkcon_enable_h(struct clk *clk, int enable)
{
	return s3c2443_gate(S3C2443_HCLKCON, clk, enable);
}

int s3c2443_clkcon_enable_p(struct clk *clk, int enable)
{
	return s3c2443_gate(S3C2443_PCLKCON, clk, enable);
}

int s3c2443_clkcon_enable_s(struct clk *clk, int enable)
{
	return s3c2443_gate(S3C2443_SCLKCON, clk, enable);
}

/* mpllref is a direct descendant of clk_xtal by default, but it is not
 * elided as the EPLL can be either sourced by the XTAL or EXTCLK and as
 * such directly equating the two source clocks is impossible.
 */
struct clk clk_mpllref = {
	.name		= "mpllref",
	.parent		= &clk_xtal,
	.id		= -1,
};

static struct clk *clk_epllref_sources[] = {
	[0] = &clk_mpllref,
	[1] = &clk_mpllref,
	[2] = &clk_xtal,
	[3] = &clk_ext,
};

struct clksrc_clk clk_epllref = {
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

/* esysclk
 *
 * this is sourced from either the EPLL or the EPLLref clock
*/

static struct clk *clk_sysclk_sources[] = {
	[0] = &clk_epllref.clk,
	[1] = &clk_epll,
};

struct clksrc_clk clk_esysclk = {
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

struct clksrc_clk clk_msysclk = {
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

/* common clksrc clocks */

static struct clksrc_clk clksrc_clks[] = {
	{
		/* ART baud-rate clock sourced from esysclk via a divisor */
		.clk	= {
			.name		= "uartclk",
			.id		= -1,
			.parent		= &clk_esysclk.clk,
		},
		.reg_div = { .reg = S3C2443_CLKDIV1, .size = 4, .shift = 8 },
	}, {
		/* camera interface bus-clock, divided down from esysclk */
		.clk	= {
			.name		= "camif-upll",	/* same as 2440 name */
			.id		= -1,
			.parent		= &clk_esysclk.clk,
			.ctrlbit	= S3C2443_SCLKCON_CAMCLK,
			.enable		= s3c2443_clkcon_enable_s,
		},
		.reg_div = { .reg = S3C2443_CLKDIV1, .size = 4, .shift = 26 },
	}, {
		.clk	= {
			.name		= "display-if",
			.id		= -1,
			.parent		= &clk_esysclk.clk,
			.ctrlbit	= S3C2443_SCLKCON_DISPCLK,
			.enable		= s3c2443_clkcon_enable_s,
		},
		.reg_div = { .reg = S3C2443_CLKDIV1, .size = 8, .shift = 16 },
	},
};


static struct clk init_clocks_off[] = {
	{
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
		.name		= "hsmmc",
		.id		= 0,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_HSMMC,
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
		.name		= "lcd",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_LCDC,

	}, {
		.name		= "timers",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_PWMT,
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
		.name		= "ac97",
		.id		= -1,
		.parent		= &clk_p,
		.ctrlbit	= S3C2443_PCLKCON_AC97,
	}, {
		.name		= "nand",
		.id		= -1,
		.parent		= &clk_h,
	}, {
		.name		= "usb-bus-host",
		.id		= -1,
		.parent		= &clk_usb_bus_host.clk,
	}
};

static inline unsigned long s3c2443_get_hdiv(unsigned long clkcon0)
{
	clkcon0 &= S3C2443_CLKDIV0_HCLKDIV_MASK;

	return clkcon0 + 1;
}

/* EPLLCON compatible enough to get on/off information */

void __init_or_cpufreq s3c2443_common_setup_clocks(pll_fn get_mpll,
						   fdiv_fn get_fdiv)
{
	unsigned long epllcon = __raw_readl(S3C2443_EPLLCON);
	unsigned long mpllcon = __raw_readl(S3C2443_MPLLCON);
	unsigned long clkdiv0 = __raw_readl(S3C2443_CLKDIV0);
	struct clk *xtal_clk;
	unsigned long xtal;
	unsigned long pll;
	unsigned long fclk;
	unsigned long hclk;
	unsigned long pclk;
	int ptr;

	xtal_clk = clk_get(NULL, "xtal");
	xtal = clk_get_rate(xtal_clk);
	clk_put(xtal_clk);

	pll = get_mpll(mpllcon, xtal);
	clk_msysclk.clk.rate = pll;

	fclk = pll / get_fdiv(clkdiv0);
	hclk = s3c2443_prediv_getrate(&clk_prediv);
	hclk /= s3c2443_get_hdiv(clkdiv0);
	pclk = hclk / ((clkdiv0 & S3C2443_CLKDIV0_HALF_PCLK) ? 2 : 1);

	s3c24xx_setup_clocks(fclk, hclk, pclk);

	printk("CPU: MPLL %s %ld.%03ld MHz, cpu %ld.%03ld MHz, mem %ld.%03ld MHz, pclk %ld.%03ld MHz\n",
	       (mpllcon & S3C2443_PLLCON_OFF) ? "off":"on",
	       print_mhz(pll), print_mhz(fclk),
	       print_mhz(hclk), print_mhz(pclk));

	for (ptr = 0; ptr < ARRAY_SIZE(clksrc_clks); ptr++)
		s3c_set_clksrc(&clksrc_clks[ptr], true);

	/* ensure usb bus clock is within correct rate of 48MHz */

	if (clk_get_rate(&clk_usb_bus_host.clk) != (48 * 1000 * 1000)) {
		printk(KERN_INFO "Warning: USB host bus not at 48MHz\n");
		clk_set_rate(&clk_usb_bus_host.clk, 48*1000*1000);
	}

	printk("CPU: EPLL %s %ld.%03ld MHz, usb-bus %ld.%03ld MHz\n",
	       (epllcon & S3C2443_PLLCON_OFF) ? "off":"on",
	       print_mhz(clk_get_rate(&clk_epll)),
	       print_mhz(clk_get_rate(&clk_usb_bus)));
}

static struct clk *clks[] __initdata = {
	&clk_prediv,
	&clk_mpllref,
	&clk_mdivclk,
	&clk_ext,
	&clk_epll,
	&clk_usb_bus,
};

static struct clksrc_clk *clksrcs[] __initdata = {
	&clk_usb_bus_host,
	&clk_epllref,
	&clk_esysclk,
	&clk_msysclk,
};

void __init s3c2443_common_init_clocks(int xtal, pll_fn get_mpll,
				       fdiv_fn get_fdiv)
{
	int ptr;

	/* s3c2443 parents h and p clocks from prediv */
	clk_h.parent = &clk_prediv;
	clk_p.parent = &clk_prediv;

	clk_usb_bus.parent = &clk_usb_bus_host.clk;
	clk_epll.parent = &clk_epllref.clk;

	s3c24xx_register_baseclocks(xtal);
	s3c24xx_register_clocks(clks, ARRAY_SIZE(clks));

	for (ptr = 0; ptr < ARRAY_SIZE(clksrcs); ptr++)
		s3c_register_clksrc(clksrcs[ptr], 1);

	s3c_register_clksrc(clksrc_clks, ARRAY_SIZE(clksrc_clks));
	s3c_register_clocks(init_clocks, ARRAY_SIZE(init_clocks));

	/* See s3c2443/etc notes on disabling clocks at init time */
	s3c_register_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));
	s3c_disable_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));

	s3c2443_common_setup_clocks(get_mpll, get_fdiv);
}
