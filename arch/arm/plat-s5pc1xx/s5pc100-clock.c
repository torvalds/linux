/* linux/arch/arm/plat-s5pc1xx/s5pc100-clock.c
 *
 * Copyright 2009 Samsung Electronics, Co.
 *	Byungho Min <bhmin@samsung.com>
 *
 * S5PC100 based common clock support
 *
 * Based on plat-s3c64xx/s3c6400-clock.c
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
#include <plat/cpu.h>
#include <plat/pll.h>
#include <plat/devs.h>
#include <plat/s5pc100.h>

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
#define clk_fin_hpll clk_ext_xtal_mux

#define clk_fout_mpll	clk_mpll

struct clk_sources {
	unsigned int	nr_sources;
	struct clk	**sources;
};

struct clksrc_clk {
	struct clk		clk;
	unsigned int		mask;
	unsigned int		shift;

	struct clk_sources	*sources;

	unsigned int		divider_shift;
	void __iomem		*reg_divider;
	void __iomem		*reg_source;
};

static int clk_default_setrate(struct clk *clk, unsigned long rate)
{
	clk->rate = rate;
	return 1;
}

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

	val = __raw_readl(S5PC1XX_CLK_SRC1);
	if (enable)
		val |= S5PC100_CLKSRC1_CLK48M_MASK;
	else
		val &= ~S5PC100_CLKSRC1_CLK48M_MASK;

	__raw_writel(val, S5PC1XX_CLK_SRC1);
	local_irq_restore(flags);

	return 0;
}

struct clk clk_48m = {
	.name		= "clk_48m",
	.id		= -1,
	.rate		= 48000000,
	.enable		= clk_48m_ctrl,
};

struct clk clk_54m = {
	.name		= "clk_54m",
	.id		= -1,
	.rate		= 54000000,
};

struct clk clk_hpll = {
	.name		= "hpll",
	.id		= -1,
};

struct clk clk_hd0 = {
	.name		= "hclkd0",
	.id		= -1,
	.rate		= 0,
	.parent		= NULL,
	.ctrlbit	= 0,
	.set_rate	= clk_default_setrate,
};

struct clk clk_pd0 = {
	.name		= "pclkd0",
	.id		= -1,
	.rate		= 0,
	.parent		= NULL,
	.ctrlbit	= 0,
	.set_rate	= clk_default_setrate,
};

static int s5pc1xx_clk_gate(void __iomem *reg,
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

static int s5pc1xx_clk_d00_ctrl(struct clk *clk, int enable)
{
	return s5pc1xx_clk_gate(S5PC100_CLKGATE_D00, clk, enable);
}

static int s5pc1xx_clk_d01_ctrl(struct clk *clk, int enable)
{
	return s5pc1xx_clk_gate(S5PC100_CLKGATE_D01, clk, enable);
}

static int s5pc1xx_clk_d02_ctrl(struct clk *clk, int enable)
{
	return s5pc1xx_clk_gate(S5PC100_CLKGATE_D02, clk, enable);
}

static int s5pc1xx_clk_d10_ctrl(struct clk *clk, int enable)
{
	return s5pc1xx_clk_gate(S5PC100_CLKGATE_D10, clk, enable);
}

static int s5pc1xx_clk_d11_ctrl(struct clk *clk, int enable)
{
	return s5pc1xx_clk_gate(S5PC100_CLKGATE_D11, clk, enable);
}

static int s5pc1xx_clk_d12_ctrl(struct clk *clk, int enable)
{
	return s5pc1xx_clk_gate(S5PC100_CLKGATE_D12, clk, enable);
}

static int s5pc1xx_clk_d13_ctrl(struct clk *clk, int enable)
{
	return s5pc1xx_clk_gate(S5PC100_CLKGATE_D13, clk, enable);
}

static int s5pc1xx_clk_d14_ctrl(struct clk *clk, int enable)
{
	return s5pc1xx_clk_gate(S5PC100_CLKGATE_D14, clk, enable);
}

static int s5pc1xx_clk_d15_ctrl(struct clk *clk, int enable)
{
	return s5pc1xx_clk_gate(S5PC100_CLKGATE_D15, clk, enable);
}

static int s5pc1xx_clk_d20_ctrl(struct clk *clk, int enable)
{
	return s5pc1xx_clk_gate(S5PC100_CLKGATE_D20, clk, enable);
}

int s5pc1xx_sclk0_ctrl(struct clk *clk, int enable)
{
	return s5pc1xx_clk_gate(S5PC100_SCLKGATE0, clk, enable);
}

int s5pc1xx_sclk1_ctrl(struct clk *clk, int enable)
{
	return s5pc1xx_clk_gate(S5PC100_SCLKGATE1, clk, enable);
}

static struct clk init_clocks_disable[] = {
	{
		.name		= "dsi",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d11_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D11_DSI,
	}, {
		.name		= "csi",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d11_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D11_CSI,
	}, {
		.name		= "ccan0",
		.id		= 0,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d14_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D14_CCAN0,
	}, {
		.name		= "ccan1",
		.id		= 1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d14_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D14_CCAN1,
	}, {
		.name		= "keypad",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d15_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D15_KEYIF,
	}, {
		.name		= "hclkd2",
		.id		= -1,
		.parent		= NULL,
		.enable		= s5pc1xx_clk_d20_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D20_HCLKD2,
	}, {
		.name		= "iis-d2",
		.id		= -1,
		.parent		= NULL,
		.enable		= s5pc1xx_clk_d20_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D20_I2SD2,
	}, {
		.name		= "otg",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d10_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D10_USBOTG,
	},
};

static struct clk init_clocks[] = {
	/* System1 (D0_0) devices */
	{
		.name		= "intc",
		.id		= -1,
		.parent		= &clk_hd0,
		.enable		= s5pc1xx_clk_d00_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D00_INTC,
	}, {
		.name		= "tzic",
		.id		= -1,
		.parent		= &clk_hd0,
		.enable		= s5pc1xx_clk_d00_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D00_TZIC,
	}, {
		.name		= "cf-ata",
		.id		= -1,
		.parent		= &clk_hd0,
		.enable		= s5pc1xx_clk_d00_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D00_CFCON,
	}, {
		.name		= "mdma",
		.id		= -1,
		.parent		= &clk_hd0,
		.enable		= s5pc1xx_clk_d00_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D00_MDMA,
	}, {
		.name		= "g2d",
		.id		= -1,
		.parent		= &clk_hd0,
		.enable		= s5pc1xx_clk_d00_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D00_G2D,
	}, {
		.name		= "secss",
		.id		= -1,
		.parent		= &clk_hd0,
		.enable		= s5pc1xx_clk_d00_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D00_SECSS,
	}, {
		.name		= "cssys",
		.id		= -1,
		.parent		= &clk_hd0,
		.enable		= s5pc1xx_clk_d00_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D00_CSSYS,
	},

	/* Memory (D0_1) devices */
	{
		.name		= "dmc",
		.id		= -1,
		.parent		= &clk_hd0,
		.enable		= s5pc1xx_clk_d01_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D01_DMC,
	}, {
		.name		= "sromc",
		.id		= -1,
		.parent		= &clk_hd0,
		.enable		= s5pc1xx_clk_d01_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D01_SROMC,
	}, {
		.name		= "onenand",
		.id		= -1,
		.parent		= &clk_hd0,
		.enable		= s5pc1xx_clk_d01_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D01_ONENAND,
	}, {
		.name		= "nand",
		.id		= -1,
		.parent		= &clk_hd0,
		.enable		= s5pc1xx_clk_d01_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D01_NFCON,
	}, {
		.name		= "intmem",
		.id		= -1,
		.parent		= &clk_hd0,
		.enable		= s5pc1xx_clk_d01_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D01_INTMEM,
	}, {
		.name		= "ebi",
		.id		= -1,
		.parent		= &clk_hd0,
		.enable		= s5pc1xx_clk_d01_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D01_EBI,
	},

	/* System2 (D0_2) devices */
	{
		.name		= "seckey",
		.id		= -1,
		.parent		= &clk_pd0,
		.enable		= s5pc1xx_clk_d02_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D02_SECKEY,
	}, {
		.name		= "sdm",
		.id		= -1,
		.parent		= &clk_hd0,
		.enable		= s5pc1xx_clk_d02_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D02_SDM,
	},

	/* File (D1_0) devices */
	{
		.name		= "pdma0",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d10_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D10_PDMA0,
	}, {
		.name		= "pdma1",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d10_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D10_PDMA1,
	}, {
		.name		= "usb-host",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d10_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D10_USBHOST,
	}, {
		.name		= "modem",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d10_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D10_MODEMIF,
	}, {
		.name		= "hsmmc",
		.id		= 0,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d10_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D10_HSMMC0,
	}, {
		.name		= "hsmmc",
		.id		= 1,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d10_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D10_HSMMC1,
	}, {
		.name		= "hsmmc",
		.id		= 2,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d10_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D10_HSMMC2,
	},

	/* Multimedia1 (D1_1) devices */
	{
		.name		= "lcd",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d11_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D11_LCD,
	}, {
		.name		= "rotator",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d11_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D11_ROTATOR,
	}, {
		.name		= "fimc",
		.id		= 0,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d11_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D11_FIMC0,
	}, {
		.name		= "fimc",
		.id		= 1,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d11_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D11_FIMC1,
	}, {
		.name		= "fimc",
		.id		= 2,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d11_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D11_FIMC2,
	}, {
		.name		= "jpeg",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d11_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D11_JPEG,
	}, {
		.name		= "g3d",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d11_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D11_G3D,
	},

	/* Multimedia2 (D1_2) devices */
	{
		.name		= "tv",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d12_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D12_TV,
	}, {
		.name		= "vp",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d12_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D12_VP,
	}, {
		.name		= "mixer",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d12_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D12_MIXER,
	}, {
		.name		= "hdmi",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d12_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D12_HDMI,
	}, {
		.name		= "mfc",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s5pc1xx_clk_d12_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D12_MFC,
	},

	/* System (D1_3) devices */
	{
		.name		= "chipid",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d13_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D13_CHIPID,
	}, {
		.name		= "gpio",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d13_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D13_GPIO,
	}, {
		.name		= "apc",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d13_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D13_APC,
	}, {
		.name		= "iec",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d13_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D13_IEC,
	}, {
		.name		= "timers",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d13_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D13_PWM,
	}, {
		.name		= "systimer",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d13_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D13_SYSTIMER,
	}, {
		.name		= "watchdog",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d13_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D13_WDT,
	}, {
		.name		= "rtc",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d13_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D13_RTC,
	},

	/* Connectivity (D1_4) devices */
	{
		.name		= "uart",
		.id		= 0,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d14_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D14_UART0,
	}, {
		.name		= "uart",
		.id		= 1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d14_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D14_UART1,
	}, {
		.name		= "uart",
		.id		= 2,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d14_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D14_UART2,
	}, {
		.name		= "uart",
		.id		= 3,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d14_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D14_UART3,
	}, {
		.name		= "i2c",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d14_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D14_IIC,
	}, {
		.name		= "hdmi-i2c",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d14_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D14_HDMI_IIC,
	}, {
		.name		= "spi",
		.id		= 0,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d14_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D14_SPI0,
	}, {
		.name		= "spi",
		.id		= 1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d14_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D14_SPI1,
	}, {
		.name		= "spi",
		.id		= 2,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d14_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D14_SPI2,
	}, {
		.name		= "irda",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d14_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D14_IRDA,
	}, {
		.name		= "hsitx",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d14_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D14_HSITX,
	}, {
		.name		= "hsirx",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d14_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D14_HSIRX,
	},

	/* Audio (D1_5) devices */
	{
		.name		= "iis",
		.id		= 0,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d15_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D15_IIS0,
	}, {
		.name		= "iis",
		.id		= 1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d15_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D15_IIS1,
	}, {
		.name		= "iis",
		.id		= 2,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d15_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D15_IIS2,
	}, {
		.name		= "ac97",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d15_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D15_AC97,
	}, {
		.name		= "pcm",
		.id		= 0,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d15_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D15_PCM0,
	}, {
		.name		= "pcm",
		.id		= 1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d15_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D15_PCM1,
	}, {
		.name		= "spdif",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d15_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D15_SPDIF,
	}, {
		.name		= "adc",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d15_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D15_TSADC,
	}, {
		.name		= "keyif",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d15_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D15_KEYIF,
	}, {
		.name		= "cg",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s5pc1xx_clk_d15_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_D15_CG,
	},

	/* Audio (D2_0) devices: all disabled */

	/* Special Clocks 1 */
	{
		.name		= "sclk_hpm",
		.id		= -1,
		.parent		= NULL,
		.enable		= s5pc1xx_sclk0_ctrl,
		.ctrlbit	= S5PC1XX_CLKGATE_SCLK0_HPM,
	}, {
		.name		= "sclk_onenand",
		.id		= -1,
		.parent		= NULL,
		.enable		= s5pc1xx_sclk0_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_SCLK0_ONENAND,
	}, {
		.name		= "sclk_spi_48",
		.id		= 0,
		.parent		= &clk_48m,
		.enable		= s5pc1xx_sclk0_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_SCLK0_SPI0_48,
	}, {
		.name		= "sclk_spi_48",
		.id		= 1,
		.parent		= &clk_48m,
		.enable		= s5pc1xx_sclk0_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_SCLK0_SPI1_48,
	}, {
		.name		= "sclk_spi_48",
		.id		= 2,
		.parent		= &clk_48m,
		.enable		= s5pc1xx_sclk0_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_SCLK0_SPI2_48,
	}, {
		.name		= "sclk_mmc_48",
		.id		= 0,
		.parent		= &clk_48m,
		.enable		= s5pc1xx_sclk0_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_SCLK0_MMC0_48,
	}, {
		.name		= "sclk_mmc_48",
		.id		= 1,
		.parent		= &clk_48m,
		.enable		= s5pc1xx_sclk0_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_SCLK0_MMC1_48,
	}, {
		.name		= "sclk_mmc_48",
		.id		= 2,
		.parent		= &clk_48m,
		.enable		= s5pc1xx_sclk0_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_SCLK0_MMC2_48,
	},

	/* Special Clocks 2 */
	{
		.name		= "sclk_tv_54",
		.id		= -1,
		.parent		= &clk_54m,
		.enable		= s5pc1xx_sclk1_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_SCLK1_TV54,
	}, {
		.name		= "sclk_vdac_54",
		.id		= -1,
		.parent		= &clk_54m,
		.enable		= s5pc1xx_sclk1_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_SCLK1_VDAC54,
	}, {
		.name		= "sclk_spdif",
		.id		= -1,
		.parent		= NULL,
		.enable		= s5pc1xx_sclk1_ctrl,
		.ctrlbit	= S5PC100_CLKGATE_SCLK1_SPDIF,
	},
};

void __init s5pc1xx_register_clocks(void)
{
	struct clk *clkp;
	int ret;
	int ptr;

	clkp = init_clocks;
	for (ptr = 0; ptr < ARRAY_SIZE(init_clocks); ptr++, clkp++) {
		ret = s3c24xx_register_clock(clkp);
		if (ret < 0) {
			printk(KERN_ERR "Failed to register clock %s (%d)\n",
			       clkp->name, ret);
		}
	}

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
static struct clk clk_fout_apll = {
	.name		= "fout_apll",
	.id		= -1,
};

static struct clk *clk_src_apll_list[] = {
	[0] = &clk_fin_apll,
	[1] = &clk_fout_apll,
};

static struct clk_sources clk_src_apll = {
	.sources	= clk_src_apll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_apll_list),
};

static struct clksrc_clk clk_mout_apll = {
	.clk	= {
		.name		= "mout_apll",
		.id		= -1,
	},
	.shift		= S5PC1XX_CLKSRC0_APLL_SHIFT,
	.mask		= S5PC1XX_CLKSRC0_APLL_MASK,
	.sources	= &clk_src_apll,
	.reg_source	= S5PC1XX_CLK_SRC0,
};

static struct clk clk_fout_epll = {
	.name		= "fout_epll",
	.id		= -1,
};

static struct clk *clk_src_epll_list[] = {
	[0] = &clk_fin_epll,
	[1] = &clk_fout_epll,
};

static struct clk_sources clk_src_epll = {
	.sources	= clk_src_epll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_epll_list),
};

static struct clksrc_clk clk_mout_epll = {
	.clk	= {
		.name		= "mout_epll",
		.id		= -1,
	},
	.shift		= S5PC1XX_CLKSRC0_EPLL_SHIFT,
	.mask		= S5PC1XX_CLKSRC0_EPLL_MASK,
	.sources	= &clk_src_epll,
	.reg_source	= S5PC1XX_CLK_SRC0,
};

static struct clk *clk_src_mpll_list[] = {
	[0] = &clk_fin_mpll,
	[1] = &clk_fout_mpll,
};

static struct clk_sources clk_src_mpll = {
	.sources	= clk_src_mpll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_mpll_list),
};

static struct clksrc_clk clk_mout_mpll = {
	.clk = {
		.name		= "mout_mpll",
		.id		= -1,
	},
	.shift		= S5PC1XX_CLKSRC0_MPLL_SHIFT,
	.mask		= S5PC1XX_CLKSRC0_MPLL_MASK,
	.sources	= &clk_src_mpll,
	.reg_source	= S5PC1XX_CLK_SRC0,
};

static unsigned long s5pc1xx_clk_doutmpll_get_rate(struct clk *clk)
{
	unsigned long rate = clk_get_rate(clk->parent);
	unsigned long clkdiv;

	printk(KERN_DEBUG "%s: parent is %ld\n", __func__, rate);

	clkdiv = __raw_readl(S5PC1XX_CLK_DIV1) & S5PC100_CLKDIV1_MPLL_MASK;
	rate /= (clkdiv >> S5PC100_CLKDIV1_MPLL_SHIFT) + 1;

	return rate;
}

static struct clk clk_dout_mpll = {
	.name		= "dout_mpll",
	.id		= -1,
	.parent		= &clk_mout_mpll.clk,
	.get_rate	= s5pc1xx_clk_doutmpll_get_rate,
};

static unsigned long s5pc1xx_clk_doutmpll2_get_rate(struct clk *clk)
{
	unsigned long rate = clk_get_rate(clk->parent);
	unsigned long clkdiv;

	printk(KERN_DEBUG "%s: parent is %ld\n", __func__, rate);

	clkdiv = __raw_readl(S5PC1XX_CLK_DIV1) & S5PC100_CLKDIV1_MPLL2_MASK;
	rate /= (clkdiv >> S5PC100_CLKDIV1_MPLL2_SHIFT) + 1;

	return rate;
}

struct clk clk_dout_mpll2 = {
	.name		= "dout_mpll2",
	.id		= -1,
	.parent		= &clk_mout_mpll.clk,
	.get_rate	= s5pc1xx_clk_doutmpll2_get_rate,
};

static struct clk *clkset_uart_list[] = {
	&clk_mout_epll.clk,
	&clk_dout_mpll,
	NULL,
	NULL
};

static struct clk_sources clkset_uart = {
	.sources	= clkset_uart_list,
	.nr_sources	= ARRAY_SIZE(clkset_uart_list),
};

static inline struct clksrc_clk *to_clksrc(struct clk *clk)
{
	return container_of(clk, struct clksrc_clk, clk);
}

static unsigned long s5pc1xx_getrate_clksrc(struct clk *clk)
{
	struct clksrc_clk *sclk = to_clksrc(clk);
	unsigned long rate = clk_get_rate(clk->parent);
	u32 clkdiv = __raw_readl(sclk->reg_divider);

	clkdiv >>= sclk->divider_shift;
	clkdiv &= 0xf;
	clkdiv++;

	rate /= clkdiv;
	return rate;
}

static int s5pc1xx_setrate_clksrc(struct clk *clk, unsigned long rate)
{
	struct clksrc_clk *sclk = to_clksrc(clk);
	void __iomem *reg = sclk->reg_divider;
	unsigned int div;
	u32 val;

	rate = clk_round_rate(clk, rate);
	div = clk_get_rate(clk->parent) / rate;
	if (div > 16)
		return -EINVAL;

	val = __raw_readl(reg);
	val &= ~(0xf << sclk->shift);
	val |= (div - 1) << sclk->shift;
	__raw_writel(val, reg);

	return 0;
}

static int s5pc1xx_setparent_clksrc(struct clk *clk, struct clk *parent)
{
	struct clksrc_clk *sclk = to_clksrc(clk);
	struct clk_sources *srcs = sclk->sources;
	u32 clksrc = __raw_readl(sclk->reg_source);
	int src_nr = -1;
	int ptr;

	for (ptr = 0; ptr < srcs->nr_sources; ptr++)
		if (srcs->sources[ptr] == parent) {
			src_nr = ptr;
			break;
		}

	if (src_nr >= 0) {
		clksrc &= ~sclk->mask;
		clksrc |= src_nr << sclk->shift;

		__raw_writel(clksrc, sclk->reg_source);
		return 0;
	}

	return -EINVAL;
}

static unsigned long s5pc1xx_roundrate_clksrc(struct clk *clk,
					      unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	int div;

	if (rate > parent_rate)
		rate = parent_rate;
	else {
		div = rate / parent_rate;

		if (div == 0)
			div = 1;
		if (div > 16)
			div = 16;

		rate = parent_rate / div;
	}

	return rate;
}

static struct clksrc_clk clk_uart_uclk1 = {
	.clk	= {
		.name		= "uclk1",
		.id		= -1,
		.ctrlbit        = S5PC100_CLKGATE_SCLK0_UART,
		.enable		= s5pc1xx_sclk0_ctrl,
		.set_parent	= s5pc1xx_setparent_clksrc,
		.get_rate	= s5pc1xx_getrate_clksrc,
		.set_rate	= s5pc1xx_setrate_clksrc,
		.round_rate	= s5pc1xx_roundrate_clksrc,
	},
	.shift		= S5PC100_CLKSRC1_UART_SHIFT,
	.mask		= S5PC100_CLKSRC1_UART_MASK,
	.sources	= &clkset_uart,
	.divider_shift	= S5PC100_CLKDIV2_UART_SHIFT,
	.reg_divider	= S5PC1XX_CLK_DIV2,
	.reg_source	= S5PC1XX_CLK_SRC1,
};

/* Clock initialisation code */

static struct clksrc_clk *init_parents[] = {
	&clk_mout_apll,
	&clk_mout_epll,
	&clk_mout_mpll,
	&clk_uart_uclk1,
};

static void __init_or_cpufreq s5pc1xx_set_clksrc(struct clksrc_clk *clk)
{
	struct clk_sources *srcs = clk->sources;
	u32 clksrc = __raw_readl(clk->reg_source);

	clksrc &= clk->mask;
	clksrc >>= clk->shift;

	if (clksrc > srcs->nr_sources || !srcs->sources[clksrc]) {
		printk(KERN_ERR "%s: bad source %d\n",
		       clk->clk.name, clksrc);
		return;
	}

	clk->clk.parent = srcs->sources[clksrc];

	printk(KERN_INFO "%s: source is %s (%d), rate is %ld\n",
	       clk->clk.name, clk->clk.parent->name, clksrc,
	       clk_get_rate(&clk->clk));
}

#define GET_DIV(clk, field) ((((clk) & field##_MASK) >> field##_SHIFT) + 1)

void __init_or_cpufreq s5pc100_setup_clocks(void)
{
	struct clk *xtal_clk;
	unsigned long xtal;
	unsigned long armclk;
	unsigned long hclkd0;
	unsigned long hclk;
	unsigned long pclkd0;
	unsigned long pclk;
	unsigned long apll;
	unsigned long mpll;
	unsigned long hpll;
	unsigned long epll;
	unsigned int ptr;
	u32 clkdiv0, clkdiv1;

	printk(KERN_DEBUG "%s: registering clocks\n", __func__);

	clkdiv0 = __raw_readl(S5PC1XX_CLK_DIV0);
	clkdiv1 = __raw_readl(S5PC1XX_CLK_DIV1);

	printk(KERN_DEBUG "%s: clkdiv0 = %08x, clkdiv1 = %08x\n",
			__func__, clkdiv0, clkdiv1);

	xtal_clk = clk_get(NULL, "xtal");
	BUG_ON(IS_ERR(xtal_clk));

	xtal = clk_get_rate(xtal_clk);
	clk_put(xtal_clk);

	printk(KERN_DEBUG "%s: xtal is %ld\n", __func__, xtal);

	apll = s5pc1xx_get_pll(xtal, __raw_readl(S5PC1XX_APLL_CON));
	mpll = s5pc1xx_get_pll(xtal, __raw_readl(S5PC1XX_MPLL_CON));
	epll = s5pc1xx_get_pll(xtal, __raw_readl(S5PC1XX_EPLL_CON));
	hpll = s5pc1xx_get_pll(xtal, __raw_readl(S5PC100_HPLL_CON));

	printk(KERN_INFO "S5PC100: PLL settings, A=%ld, M=%ld, E=%ld, H=%ld\n",
	       apll, mpll, epll, hpll);

	armclk = apll / GET_DIV(clkdiv0, S5PC1XX_CLKDIV0_APLL);
	armclk = armclk / GET_DIV(clkdiv0, S5PC100_CLKDIV0_ARM);
	hclkd0 = armclk / GET_DIV(clkdiv0, S5PC100_CLKDIV0_D0);
	pclkd0 = hclkd0 / GET_DIV(clkdiv0, S5PC100_CLKDIV0_PCLKD0);
	hclk = mpll / GET_DIV(clkdiv1, S5PC100_CLKDIV1_D1);
	pclk = hclk / GET_DIV(clkdiv1, S5PC100_CLKDIV1_PCLKD1);

	printk(KERN_INFO "S5PC100: ARMCLK=%ld, HCLKD0=%ld, PCLKD0=%ld, HCLK=%ld, PCLK=%ld\n",
	       armclk, hclkd0, pclkd0, hclk, pclk);

	clk_fout_apll.rate = apll;
	clk_fout_mpll.rate = mpll;
	clk_fout_epll.rate = epll;
	clk_fout_apll.rate = apll;

	clk_h.rate = hclk;
	clk_p.rate = pclk;

	for (ptr = 0; ptr < ARRAY_SIZE(init_parents); ptr++)
		s5pc1xx_set_clksrc(init_parents[ptr]);
}

static struct clk *clks[] __initdata = {
	&clk_ext_xtal_mux,
	&clk_mout_epll.clk,
	&clk_fout_epll,
	&clk_mout_mpll.clk,
	&clk_dout_mpll,
	&clk_uart_uclk1.clk,
	&clk_ext,
	&clk_epll,
	&clk_27m,
	&clk_48m,
	&clk_54m,
};

void __init s5pc100_register_clocks(void)
{
	struct clk *clkp;
	int ret;
	int ptr;

	for (ptr = 0; ptr < ARRAY_SIZE(clks); ptr++) {
		clkp = clks[ptr];
		ret = s3c24xx_register_clock(clkp);
		if (ret < 0) {
			printk(KERN_ERR "Failed to register clock %s (%d)\n",
			       clkp->name, ret);
		}
	}

	clk_mpll.parent = &clk_mout_mpll.clk;
	clk_epll.parent = &clk_mout_epll.clk;
}
