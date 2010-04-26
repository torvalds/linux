/* arch/arm/mach-rk2818/clock.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/version.h>
#include <asm/clkdev.h>
#include <mach/rk2818_iomap.h>

enum 
{
	/* SCU CLK GATE 0 CON */
	SCU_IPID_ARM = 0,
	SCU_IPID_DSP,
	SCU_IPID_DMA,
	SCU_IPID_SRAMARM,
	SCU_IPID_SRAMDSP,
	SCU_IPID_HIF,
	SCU_IPID_OTGBUS,
	SCU_IPID_OTGPHY,
	SCU_IPID_NANDC,
	SCU_IPID_INTC,
	SCU_IPID_DEBLK,     /* 10 */
	SCU_IPID_LCDC,
	SCU_IPID_VIP,       /* as sensor */
	SCU_IPID_I2S,
	SCU_IPID_SDMMC0,    /* 14 */
	SCU_IPID_EBROM,
	SCU_IPID_GPIO0,
	SCU_IPID_GPIO1,
	SCU_IPID_UART0,
	SCU_IPID_UART1,
	SCU_IPID_I2C0,      /* 20 */
	SCU_IPID_I2C1,
	SCU_IPID_SPI0,
	SCU_IPID_SPI1,
	SCU_IPID_PWM,
	SCU_IPID_TIMER,
	SCU_IPID_WDT,
	SCU_IPID_RTC,
	SCU_IPID_LSADC,
	SCU_IPID_UART2,
	SCU_IPID_UART3,		/* 30 */
	SCU_IPID_SDMMC1,

	/* SCU CLK GATE 1 CON */
	SCU_IPID_HSADC = 32,
	SCU_IPID_MOBILE_SDARM_COMMON = 47,
	SCU_IPID_SDRAM_CONTROLLER,
	SCU_IPID_MOBILE_SDRAM_CONTROLLER,
	SCU_IPID_LCDC_SHARE_MEMORY,	/* 50 */
	SCU_IPID_LCDC_HCLK,
	SCU_IPID_DEBLK_H264,
	SCU_IPID_GPU,
	SCU_IPID_DDR_HCLK,
	SCU_IPID_DDR,
	SCU_IPID_CUSTOMIZED_SDRAM_CONTROLLER,
	SCU_IPID_MCDMA,
	SCU_IPID_SDRAM,
	SCU_IPID_DDR_AXI,
	SCU_IPID_DSP_TIMER,	/* 60 */
	SCU_IPID_DSP_SLAVE,
	SCU_IPID_DSP_MASTER,
	SCU_IPID_USB_HOST,

	/* SCU CLK GATE 2 CON */
	SCU_IPID_ARMIBUS = 64,
	SCU_IPID_ARMDBUS,
	SCU_IPID_DSPBUS,
	SCU_IPID_EXPBUS,
	SCU_IPID_APBBUS,
	SCU_IPID_EFUSE,
	SCU_IPID_DTCM1,		/* 70 */
	SCU_IPID_DTCM0,
	SCU_IPID_ITCM,
	SCU_IPID_VIDEOBUS,

	SCU_IPID_GATE_MAX,
};

static struct rockchip_scu_reg_hw
{
	u32 scu_pll_config[3];	/* 0:arm 1:dsp 2:codec */
	u32 scu_mode_config;
	u32 scu_pmu_config;
	u32 scu_clksel0_config;
	u32 scu_clksel1_config;
	u32 scu_clkgate0_config;
	u32 scu_clkgate1_config;
	u32 scu_clkgate2_config;
	u32 scu_softreset_config;
	u32 scu_chipcfg_config;
	u32 scu_cuppd;		/* arm power down */
	u32 scu_clksel2_config;
} *scu_register_base = (struct rockchip_scu_reg_hw *)(RK2818_SCU_BASE);

#define CLKSEL0_REG	(u32 __iomem *)(RK2818_SCU_BASE + 0x14)
#define CLKSEL1_REG	(u32 __iomem *)(RK2818_SCU_BASE + 0x18)
#define CLKSEL2_REG	(u32 __iomem *)(RK2818_SCU_BASE + 0x34)

/* Clock flags */
/* bit 0 is free */
#define RATE_FIXED		(1 << 1)	/* Fixed clock rate */
#define CONFIG_PARTICIPANT	(1 << 10)	/* Fundamental clock */
#define ENABLE_ON_INIT		(1 << 11)	/* Enable upon framework init */

struct clk {
	struct list_head	node;
	const char		*name;
	struct clk		*parent;
	struct list_head	children;
	struct list_head	sibling;	/* node for children */
	unsigned long		rate;
	u32			flags;
	int			(*mode)(struct clk *clk, int on);
	unsigned long		(*recalc)(struct clk *);
	int			(*set_rate)(struct clk *, unsigned long);
	long			(*round_rate)(struct clk *, unsigned long);
	void			(*init)(struct clk *);	/* set clk's parent field from the hardware */
	s16			usecount;
	u8			gate_idx;
	u8			pll_idx;
	u32 __iomem		*clksel_reg;
	u32			clksel_mask;
	u8			clksel_shift;
	u8			clksel_maxdiv;
#if defined(CONFIG_PM_DEBUG) && defined(CONFIG_DEBUG_FS)
	struct dentry		*dent;	/* For visible tree hierarchy */
#endif
};

static void __clk_disable(struct clk *clk);
static void clk_reparent(struct clk *child, struct clk *parent);
static void propagate_rate(struct clk *tclk);

/* Used for clocks that always have same value as the parent clock */
static unsigned long followparent_recalc(struct clk *clk)
{
	return clk->parent->rate;
}

static unsigned long clksel_recalc(struct clk *clk)
{
	u32 div = ((readl(clk->clksel_reg) & clk->clksel_mask) >> clk->clksel_shift) + 1;
	unsigned long rate = clk->parent->rate / div;
	pr_debug("clock: %s new clock rate is %ld (div %d)\n", clk->name, rate, div);
	return rate;
}

static unsigned long clksel_recalc_shift(struct clk *clk)
{
	u32 shift = (readl(clk->clksel_reg) & clk->clksel_mask) >> clk->clksel_shift;
	unsigned long rate = clk->parent->rate >> shift;
	pr_debug("clock: %s new clock rate is %ld (shift %d)\n", clk->name, rate, shift);
	return rate;
}

static int clksel_set_rate(struct clk *clk, unsigned long rate)
{
	u32 div;

	for (div = 1; div <= clk->clksel_maxdiv; div++) {
		u32 new_rate = clk->parent->rate / div;
		if (new_rate <= rate) {
			u32 *reg = clk->clksel_reg;
			u32 v = readl(reg);
			v &= ~clk->clksel_mask;
			v |= (div - 1) << clk->clksel_shift;
			writel(v, reg);
			clk->rate = new_rate;
			pr_debug("clock: clksel_set_rate for clock %s to rate %ld (div %d)\n", clk->name, rate, div);
			return 0;
		}
	}

	return -ENOENT;
}

static int clksel_set_rate_shift(struct clk *clk, unsigned long rate)
{
	u32 shift;

	for (shift = 0; (1 << shift) <= clk->clksel_maxdiv; shift++) {
		u32 new_rate = clk->parent->rate >> shift;
		if (new_rate <= rate) {
			u32 *reg = clk->clksel_reg;
			u32 v = readl(reg);
			v &= ~clk->clksel_mask;
			v |= shift << clk->clksel_shift;
			writel(v, reg);
			clk->rate = new_rate;
			pr_debug("clock: clksel_set_rate for clock %s to rate %ld (shift %d)\n", clk->name, rate, shift);
			return 0;
		}
	}

	return -ENOENT;
}

static struct clk xin24m = {
	.name		= "xin24m",
	.rate		= 24000000,
	.flags		= RATE_FIXED,
};

static struct clk clk12m = {
	.name		= "clk12m",
	.rate		= 12000000,
	.parent		= &xin24m,
	.flags		= RATE_FIXED,
};

static struct clk extclk = {
	.name		= "extclk",
	.rate		= 27000000,
	.flags		= RATE_FIXED,
};

static unsigned long pll_clk_recalc(struct clk *clk)
{
	u32 v = readl(&scu_register_base->scu_pll_config[clk->pll_idx]);
	u32 OD = ((v >> 1) & 0x7) + 1;
	u32 NF = ((v >> 4) & 0xfff) + 1;
	u32 NR = ((v >> 16) & 0x3f) + 1;
	unsigned long rate = clk->parent->rate / NR * NF / OD;
	pr_debug("clock: %s new clock rate is %ld NR %d NF %d OD %d\n", clk->name, rate, NR, NF, OD);
	return rate;
}

#define PLL_CLK(NAME,IDX) \
static struct clk NAME##_pll_clk = { \
	.name		= #NAME"_pll", \
	.parent		= &xin24m, \
	.pll_idx	= IDX, \
	.recalc		= pll_clk_recalc, \
}

PLL_CLK(arm, 0);
PLL_CLK(dsp, 1);
PLL_CLK(codec, 2);

static struct clk arm_clk = {
	.name		= "arm",
	.parent		= &arm_pll_clk,
	.recalc		= clksel_recalc,
//	.set_rate	= arm_clk_set_rate,
	.clksel_reg	= CLKSEL2_REG,
	.clksel_mask	= 0xF,
	.clksel_maxdiv	= 16,
};

static struct clk arm_hclk = {
	.name		= "arm_hclk",
	.parent		= &arm_clk,
	.recalc		= clksel_recalc,
	.clksel_reg	= CLKSEL0_REG,
	.clksel_mask	= 0x3,
	.clksel_maxdiv	= 4,
};

static struct clk clk48m = {
	.name		= "clk48m",
	.parent		= &arm_clk,
	.recalc		= clksel_recalc,
	.flags		= RATE_FIXED,
	.clksel_reg	= CLKSEL2_REG,
	.clksel_mask	= 0xF << 4,
	.clksel_shift	= 4,
};

static struct clk arm_pclk = {
	.name		= "arm_pclk",
	.parent		= &arm_hclk,
	.recalc		= clksel_recalc_shift,
	.set_rate	= clksel_set_rate_shift,
	.clksel_reg	= CLKSEL0_REG,
	.clksel_mask	= 0x3 << 2,
	.clksel_shift	= 2,
	.clksel_maxdiv	= 4,
};

static void demod_clk_init(struct clk *clk)
{
	struct clk *parent = clk->parent;
	u32 r = readl(CLKSEL1_REG);
	if (r & (1 << 26)) {
		parent = &xin24m;
	} else {
		r >>= 24;
		parent = (r == 0) ? &codec_pll_clk : (r == 1) ? &arm_pll_clk : (r == 2) ? &dsp_pll_clk : parent;
	}
	clk_reparent(clk, parent);
}

static struct clk demod_clk = {
	.name		= "demod",
	.parent		= &codec_pll_clk,
	.recalc		= clksel_recalc,
	.set_rate	= clksel_set_rate,
	.init		= demod_clk_init,
	.clksel_reg	= CLKSEL1_REG,
	.clksel_mask	= 0xFF << 16,
	.clksel_shift	= 16,
	.clksel_maxdiv	= 128,
};

static struct clk codec_clk = {
	.name		= "codec",
	.parent		= &codec_pll_clk,
	.recalc		= clksel_recalc,
	.set_rate	= clksel_set_rate,
	.clksel_reg	= CLKSEL1_REG,
	.clksel_mask	= 0x1F << 3,
	.clksel_shift	= 3,
	.clksel_maxdiv	= 32,
};

static struct clk lcdc_divider_clk = {
	.name		= "lcdc_divider",
	.parent		= &arm_pll_clk,
	.recalc		= clksel_recalc,
	.set_rate	= clksel_set_rate,
	.clksel_reg	= CLKSEL0_REG,
	.clksel_mask	= 0xFF << 8,
	.clksel_shift	= 8,
	.clksel_maxdiv	= 128,
};

static void otgphy_clk_init(struct clk *clk)
{
	u32 r = (readl(CLKSEL0_REG) >> 18) & 3;
	struct clk *parent = (r == 0) ? &xin24m : (r == 1) ? &clk12m : (r == 2) ? &clk48m : clk->parent;
	clk_reparent(clk, parent);
}

static void lcdc_clk_init(struct clk *clk)
{
	u32 r = readl(CLKSEL0_REG) & (1 << 7);
	struct clk *parent = r ? &extclk : &lcdc_divider_clk;
	clk_reparent(clk, parent);
}

static void vip_clk_init(struct clk *clk)
{
	u32 r = (readl(CLKSEL0_REG) >> 23) & 3;
	struct clk *parent = (r == 0) ? &xin24m : (r == 1) ? &extclk : (r == 2) ? &clk48m : clk->parent;
	clk_reparent(clk, parent);
}

static void ddr_clk_init(struct clk *clk)
{
	u32 r = (readl(CLKSEL0_REG) >> 28) & 3;
	struct clk *parent = (r == 0) ? &codec_pll_clk : (r == 1) ? &arm_pll_clk : (r == 2) ? &dsp_pll_clk : clk->parent;
	clk_reparent(clk, parent);
}

static void i2s_clk_init(struct clk *clk)
{
	u32 r = readl(CLKSEL1_REG) & (1 << 2);
	struct clk *parent = r ? &clk12m : &codec_clk;
	clk_reparent(clk, parent);
}

static int gate_mode(struct clk *clk, int on)
{
	u32 *reg;
	int idx = clk->gate_idx;
	u32 v;

	if (idx >= SCU_IPID_GATE_MAX)
		return -EINVAL;

	reg = &scu_register_base->scu_clkgate0_config;
	reg += (idx >> 5);
	idx &= 0x1F;

	v = readl(reg);
	if (on) {
		v &= ~(1 << idx);	// clear bit 
	} else {
		v |= (1 << idx);	// set bit
	}
	writel(v, reg);

	return 0;
}

/**
 * uart_clk_init_ - set a uart clk's parent field from the hardware
 * @clk: clock struct ptr to use
 *
 * Given a pointer to a source-selectable struct clk, read the hardware
 * register and determine what its parent is currently set to.  Update the
 * clk->parent field with the appropriate clk ptr.
 */
static void uart_clk_init(struct clk *clk)
{
	u32 r = readl(&scu_register_base->scu_clksel1_config) >> 31;
	struct clk *parent = r ? &clk48m : &xin24m;
	clk_reparent(clk, parent);
}

#define UART_CLK(n) \
static struct clk uart##n##_clk = { \
	.name		= "uart"#n, \
	.parent		= &xin24m, \
	.mode		= gate_mode, \
	.recalc		= followparent_recalc, \
	.init		= uart_clk_init, \
	.gate_idx	= SCU_IPID_UART##n, \
}

#define GATE_CLK(NAME,PARENT,ID) \
static struct clk NAME##_clk = { \
	.name		= #NAME, \
	.parent		= &PARENT, \
	.mode		= gate_mode, \
	.recalc		= followparent_recalc, \
	.gate_idx	= SCU_IPID_##ID, \
}

GATE_CLK(arm_core, arm_clk, ARM);
GATE_CLK(dsp, dsp_pll_clk, DSP);
GATE_CLK(dma, arm_hclk, DMA);
GATE_CLK(sramarm, arm_hclk, SRAMARM);
GATE_CLK(sramdsp, arm_hclk, SRAMDSP);
GATE_CLK(hif, arm_hclk, HIF);
GATE_CLK(otgbus, arm_hclk, OTGBUS);
static struct clk otgphy_clk = {
	.name		= "otgphy",
	.parent		= &xin24m,
	.mode		= gate_mode,
	.recalc		= followparent_recalc,
	.init		= otgphy_clk_init,
	.gate_idx	= SCU_IPID_OTGPHY,
};
GATE_CLK(nandc, arm_hclk, NANDC);
GATE_CLK(intc, arm_hclk, INTC);
GATE_CLK(deblocking_rv, arm_hclk, DEBLK);
static struct clk lcdc_clk = {
	.name		= "lcdc",
	.parent		= &lcdc_divider_clk,
	.mode		= gate_mode,
	.recalc		= followparent_recalc,
	.init		= lcdc_clk_init,
	.gate_idx	= SCU_IPID_LCDC,
};
static struct clk vip_clk = {
	.name		= "vip",
	.parent		= &xin24m,
	.mode		= gate_mode,
	.recalc		= followparent_recalc,
	.init		= vip_clk_init,
	.gate_idx	= SCU_IPID_VIP,
};
static struct clk i2s_clk = {
	.name		= "i2s",
	.parent		= &clk12m,
	.mode		= gate_mode,
	.recalc		= followparent_recalc,
	.init		= i2s_clk_init,
	.gate_idx	= SCU_IPID_I2S,
};
static struct clk sdmmc0_clk = {
	.name		= "sdmmc0",
	.parent		= &arm_hclk,
	.mode		= gate_mode,
	.recalc		= clksel_recalc,
	.set_rate	= clksel_set_rate,
	.gate_idx	= SCU_IPID_SDMMC0,
	.clksel_reg	= CLKSEL0_REG,
	.clksel_mask	= 7 << 4,
	.clksel_shift	= 4,
	.clksel_maxdiv	= 8,
};

GATE_CLK(ebrom, arm_hclk, EBROM);
GATE_CLK(gpio0, arm_pclk, GPIO0);
GATE_CLK(gpio1, arm_pclk, GPIO1);
UART_CLK(0);
UART_CLK(1);
GATE_CLK(i2c0, arm_pclk, I2C0);
GATE_CLK(i2c1, arm_pclk, I2C1);
GATE_CLK(spi0, arm_pclk, SPI0);
GATE_CLK(spi1, arm_pclk, SPI1);
GATE_CLK(pwm, arm_pclk, PWM);
GATE_CLK(timer, arm_pclk, TIMER);
GATE_CLK(wdt, arm_pclk, WDT);
GATE_CLK(rtc, arm_pclk, RTC);
static struct clk lsadc_clk = {
	.name		= "lsadc",
	.parent		= &arm_pclk,
	.mode		= gate_mode,
	.recalc		= clksel_recalc,
	.set_rate	= clksel_set_rate,
	.gate_idx	= SCU_IPID_LSADC,
	.clksel_reg	= CLKSEL1_REG,
	.clksel_mask	= 0xFF << 8,
	.clksel_shift	= 8,
	.clksel_maxdiv	= 128,
};
UART_CLK(2);
UART_CLK(3);
static struct clk sdmmc1_clk = {
	.name		= "sdmmc1",
	.parent		= &arm_hclk,
	.mode		= gate_mode,
	.recalc		= clksel_recalc,
	.set_rate	= clksel_set_rate,
	.gate_idx	= SCU_IPID_SDMMC1,
	.clksel_reg	= CLKSEL2_REG,
	.clksel_mask	= 7 << 8,
	.clksel_shift	= 8,
	.clksel_maxdiv	= 8,
};

static unsigned long hsadc_clk_recalc(struct clk *clk)
{
	return clk->parent->rate >> 1;
}

static struct clk hsadc_clk = {
	.name		= "hsadc",
	.parent		= &demod_clk,
	.mode		= gate_mode,
	.recalc		= hsadc_clk_recalc,
	.gate_idx	= SCU_IPID_HSADC,
};
GATE_CLK(mobile_sdram_common, arm_hclk, MOBILE_SDARM_COMMON);
GATE_CLK(sdram_controller, arm_hclk, SDRAM_CONTROLLER);
GATE_CLK(mobile_sdram_controller, arm_hclk, MOBILE_SDRAM_CONTROLLER);
GATE_CLK(lcdc_share_memory, arm_hclk, LCDC_SHARE_MEMORY);
GATE_CLK(lcdc_hclk, arm_hclk, LCDC_HCLK);
GATE_CLK(deblocking_h264, arm_hclk, DEBLK_H264);
GATE_CLK(gpu, arm_hclk, GPU);
GATE_CLK(ddr_hclk, arm_hclk, DDR_HCLK);
static struct clk ddr_clk = {
	.name		= "ddr",
	.parent		= &codec_pll_clk,
	.mode		= gate_mode,
	.recalc		= clksel_recalc_shift,
	.set_rate	= clksel_set_rate_shift,
	.init		= ddr_clk_init,
	.gate_idx	= SCU_IPID_DDR,
	.clksel_reg	= CLKSEL0_REG,
	.clksel_mask	= 0x3 << 30,
	.clksel_shift	= 30,
	.clksel_maxdiv	= 8,
};
GATE_CLK(customized_sdram_controller, arm_hclk, CUSTOMIZED_SDRAM_CONTROLLER);
GATE_CLK(mcdma, arm_hclk, MCDMA);
GATE_CLK(sdram, arm_hclk, SDRAM);
GATE_CLK(ddr_axi, arm_hclk, DDR_AXI);
GATE_CLK(dsp_timer, arm_hclk, DSP_TIMER);
GATE_CLK(dsp_slave, arm_hclk, DSP_SLAVE);
GATE_CLK(dsp_master, arm_hclk, DSP_MASTER);
GATE_CLK(usb_host, clk48m, USB_HOST);

GATE_CLK(armibus, arm_hclk, ARMIBUS);
GATE_CLK(armdbus, arm_hclk, ARMDBUS);
GATE_CLK(dspbus, arm_hclk, DSPBUS);
GATE_CLK(expbus, arm_hclk, EXPBUS);
GATE_CLK(apbbus, arm_hclk, APBBUS);
GATE_CLK(efuse, arm_pclk, EFUSE);
GATE_CLK(dtcm1, arm_clk, DTCM1);
GATE_CLK(dtcm0, arm_clk, DTCM0);
GATE_CLK(itcm, arm_clk, ITCM);
GATE_CLK(videobus, arm_hclk, VIDEOBUS);

#define CLK(dev, con, ck) \
	{ \
		.dev_id = dev, \
		.con_id = con, \
		.clk = ck, \
	}

#define CLK1(name) \
	{ \
		.dev_id = NULL, \
		.con_id = #name, \
		.clk = &name##_clk, \
	}

static struct clk_lookup clks[] = {
	CLK(NULL, "xin24m", &xin24m),
	CLK(NULL, "extclk", &extclk),

	CLK(NULL, "clk12m", &clk12m),
	CLK1(arm_pll),
	CLK1(dsp_pll),
	CLK1(codec_pll),
	CLK1(arm),
	CLK(NULL, "arm_hclk", &arm_hclk),
	CLK(NULL, "clk48m", &clk48m),
	CLK(NULL, "arm_pclk", &arm_pclk),
	CLK1(demod),
	CLK1(codec),
	CLK1(lcdc_divider),

	CLK1(arm_core),
	CLK1(dsp),
	CLK1(dma),
	CLK1(sramarm),
	CLK1(sramdsp),
	CLK1(hif),
	CLK1(otgbus),
	CLK1(otgphy),
	CLK1(nandc),
	CLK1(intc),
	CLK1(deblocking_rv),
	CLK1(lcdc),
	CLK1(vip),
	CLK1(i2s),
	CLK1(sdmmc0),
	CLK1(ebrom),
	CLK1(gpio0),
	CLK1(gpio1),
	CLK("rk2818_serial.0", "uart", &uart0_clk),
	CLK("rk2818_serial.1", "uart", &uart1_clk),
	CLK1(i2c0),
	CLK1(i2c1),
	CLK1(spi0),
	CLK1(spi1),
	CLK1(pwm),
	CLK1(timer),
	CLK1(wdt),
	CLK1(rtc),
	CLK1(lsadc),
	CLK("rk2818_serial.2", "uart", &uart2_clk),
	CLK("rk2818_serial.3", "uart", &uart3_clk),
	CLK1(sdmmc1),

	CLK1(hsadc),
	CLK1(mobile_sdram_common),
	CLK1(sdram_controller),
	CLK1(mobile_sdram_controller),
	CLK1(lcdc_share_memory),
	CLK1(lcdc_hclk),
	CLK1(deblocking_h264),
	CLK1(gpu),
	CLK1(ddr_hclk),
	CLK1(ddr),
	CLK1(customized_sdram_controller),
	CLK1(mcdma),
	CLK1(sdram),
	CLK1(ddr_axi),
	CLK1(dsp_timer),
	CLK1(dsp_slave),
	CLK1(dsp_master),
	CLK1(usb_host),

	CLK1(armibus),
	CLK1(armdbus),
	CLK1(dspbus),
	CLK1(expbus),
	CLK1(apbbus),
	CLK1(efuse),
	CLK1(dtcm1),
	CLK1(dtcm0),
	CLK1(itcm),
	CLK1(videobus),
};

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);
static DEFINE_SPINLOCK(clockfw_lock);

static int __clk_enable(struct clk *clk)
{
	int ret = 0;

	if (clk->usecount == 0) {
		if (clk->parent) {
			ret = __clk_enable(clk->parent);
			if (ret)
				return ret;
		}

		if (clk->mode) {
			ret = clk->mode(clk, 1);
			if (ret) {
				if (clk->parent)
					__clk_disable(clk->parent);
				return ret;
			}
		}
		pr_debug("clock: %s enabled\n", clk->name);
	}
	clk->usecount++;

	return ret;
}

int clk_enable(struct clk *clk)
{
	int ret = 0;
	unsigned long flags;

	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	spin_lock_irqsave(&clockfw_lock, flags);
	ret = __clk_enable(clk);
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_enable);

static void __clk_disable(struct clk *clk)
{
	if (--clk->usecount == 0) {
		if (clk->mode)
			clk->mode(clk, 0);
		pr_debug("clock: %s disabled\n", clk->name);
	}
	if (clk->parent)
		__clk_disable(clk->parent);
}

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	if (clk == NULL || IS_ERR(clk))
		return;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (clk->usecount == 0) {
		printk(KERN_ERR "Trying disable clock %s with 0 usecount\n", clk->name);
		WARN_ON(1);
		goto out;
	}

	__clk_disable(clk);

out:
	spin_unlock_irqrestore(&clockfw_lock, flags);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	unsigned long flags;
	unsigned long ret;

	if (clk == NULL || IS_ERR(clk))
		return 0;

	spin_lock_irqsave(&clockfw_lock, flags);
	ret = clk->rate;
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_get_rate);

/*-------------------------------------------------------------------------
 * Optional clock functions defined in include/linux/clk.h
 *-------------------------------------------------------------------------*/

/* Given a clock and a rate apply a clock specific rounding function */
static long __clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (clk->round_rate)
		return clk->round_rate(clk, rate);

	if (clk->flags & RATE_FIXED)
		printk(KERN_ERR "clock: clk_round_rate called on fixed-rate clock %s\n", clk->name);

	return clk->rate;
}

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	unsigned long flags;
	long ret = 0;

	if (clk == NULL || IS_ERR(clk))
		return ret;

	spin_lock_irqsave(&clockfw_lock, flags);
	ret = __clk_round_rate(clk, rate);
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_round_rate);

/* Set the clock rate for a clock source */
static int __clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = -EINVAL;

	pr_debug("clock: set_rate for clock %s to rate %ld\n", clk->name, rate);

	if (clk->flags & CONFIG_PARTICIPANT)
		return -EINVAL;

	if (clk->set_rate)
		ret = clk->set_rate(clk, rate);

	return ret;
}

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long flags;
	int ret = -EINVAL;

	if (clk == NULL || IS_ERR(clk))
		return ret;

	spin_lock_irqsave(&clockfw_lock, flags);
	ret = __clk_set_rate(clk, rate);
	if (ret == 0) {
		if (clk->recalc)
			clk->rate = clk->recalc(clk);
		propagate_rate(clk);
	}
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_set_rate);

static int __clk_set_parent(struct clk *clk, struct clk *new_parent)
{
	return -EINVAL;	// FIXME
}

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	unsigned long flags;
	int ret = -EINVAL;

	if (clk == NULL || IS_ERR(clk) || parent == NULL || IS_ERR(parent))
		return ret;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (clk->usecount == 0) {
		ret = __clk_set_parent(clk, parent);
		if (ret == 0) {
			if (clk->recalc)
				clk->rate = clk->recalc(clk);
			propagate_rate(clk);
		}
	} else
		ret = -EBUSY;
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_set_parent);

struct clk *clk_get_parent(struct clk *clk)
{
	return clk->parent;
}
EXPORT_SYMBOL(clk_get_parent);

static void clk_reparent(struct clk *child, struct clk *parent)
{
	if (child->parent == parent)
		return;
	pr_debug("clock: %s reparent to %s (was %s)\n", child->name, parent->name, ((child->parent) ? child->parent->name : "NULL"));

	list_del_init(&child->sibling);
	if (parent)
		list_add(&child->sibling, &parent->children);
	child->parent = parent;

	/* now do the debugfs renaming to reattach the child
	   to the proper parent */
}

/* Propagate rate to children */
static void propagate_rate(struct clk *tclk)
{
	struct clk *clkp;

	list_for_each_entry(clkp, &tclk->children, sibling) {
		if (clkp->recalc)
			clkp->rate = clkp->recalc(clkp);
		propagate_rate(clkp);
	}
}

static LIST_HEAD(root_clks);

/**
 * recalculate_root_clocks - recalculate and propagate all root clocks
 *
 * Recalculates all root clocks (clocks with no parent), which if the
 * clock's .recalc is set correctly, should also propagate their rates.
 * Called at init.
 */
static void recalculate_root_clocks(void)
{
	struct clk *clkp;

	list_for_each_entry(clkp, &root_clks, sibling) {
		if (clkp->recalc)
			clkp->rate = clkp->recalc(clkp);
		propagate_rate(clkp);
	}
}

/**
 * clk_preinit - initialize any fields in the struct clk before clk init
 * @clk: struct clk * to initialize
 *
 * Initialize any struct clk fields needed before normal clk initialization
 * can run.  No return value.
 */
static void clk_preinit(struct clk *clk)
{
	INIT_LIST_HEAD(&clk->children);
}

static int clk_register(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	/*
	 * trap out already registered clocks
	 */
	if (clk->node.next || clk->node.prev)
		return 0;

	mutex_lock(&clocks_mutex);
	if (clk->parent)
		list_add(&clk->sibling, &clk->parent->children);
	else
		list_add(&clk->sibling, &root_clks);

	list_add(&clk->node, &clocks);
	if (clk->init)
		clk->init(clk);
	mutex_unlock(&clocks_mutex);

	return 0;
}

static void clk_enable_init_clocks(void)
{
	struct clk *clkp;

	list_for_each_entry(clkp, &clocks, node) {
		if (clkp->flags & ENABLE_ON_INIT)
			clk_enable(clkp);
	}
}

#ifdef CONFIG_CPU_FREQ
void clk_init_cpufreq_table(struct cpufreq_frequency_table **table)
{
	unsigned long flags;

	spin_lock_irqsave(&clockfw_lock, flags);
	__clk_init_cpufreq_table(table);
	spin_unlock_irqrestore(&clockfw_lock, flags);
}
EXPORT_SYMBOL(clk_init_cpufreq_table);
#endif

static unsigned int __initdata armclk;

/*
 * By default we use the rate set by the bootloader.
 * You can override this with armclk= cmdline option.
 */
static int __init clk_setup(char *str)
{
	get_option(&str, &armclk);

	if (!armclk)
		return 1;

	if (armclk < 1000)
		armclk *= 1000000;

	return 1;
}
__setup("armclk=", clk_setup);

/*
 * Switch the arm_clk rate if specified on cmdline.
 * We cannot do this early until cmdline is parsed.
 */
static int __init rk28_clk_arch_init(void)
{
	if (!armclk)
		return -EINVAL;

	if (clk_set_rate(&arm_clk, armclk))
		printk(KERN_ERR "*** Unable to set arm_clk rate\n");

	recalculate_root_clocks();

	printk(KERN_INFO "Switched to new clocking rate (pll/arm/hclk/pclk): "
	       "%ld/%ld/%ld/%ld MHz\n",
	       arm_pll_clk.rate / 1000000, arm_clk.rate / 1000000,
	       arm_hclk.rate / 1000000, arm_pclk.rate / 1000000);

	calibrate_delay();

	return 0;
}
arch_initcall(rk28_clk_arch_init);

void __init rk2818_clock_init(void)
{
	struct clk_lookup *lk;

	for (lk = clks; lk < clks + ARRAY_SIZE(clks); lk++)
		clk_preinit(lk->clk);

	for (lk = clks; lk < clks + ARRAY_SIZE(clks); lk++) {
		clkdev_add(lk);
		clk_register(lk->clk);
	}

	recalculate_root_clocks();

	printk(KERN_INFO "Clocking rate (pll/arm/hclk/pclk): "
	       "%ld/%ld/%ld/%ld MHz\n",
	       arm_pll_clk.rate / 1000000, arm_clk.rate / 1000000,
	       arm_hclk.rate / 1000000, arm_pclk.rate / 1000000);

	/*
	 * Only enable those clocks we will need, let the drivers
	 * enable other clocks as necessary
	 */
	clk_enable_init_clocks();
}

#if defined(CONFIG_PM_DEBUG) && defined(CONFIG_DEBUG_FS)
/*
 *	debugfs support to trace clock tree hierarchy and attributes
 */
static struct dentry *clk_debugfs_root;

static int clk_debugfs_register_one(struct clk *c)
{
	int err;
	struct dentry *d, *child;
	struct clk *pa = c->parent;
	char s[255];
	char *p = s;

	p += sprintf(p, "%s", c->name);
	d = debugfs_create_dir(s, pa ? pa->dent : clk_debugfs_root);
	if (!d)
		return -ENOMEM;
	c->dent = d;

	d = debugfs_create_u8("usecount", S_IRUGO, c->dent, (u8 *)&c->usecount);
	if (!d) {
		err = -ENOMEM;
		goto err_out;
	}
	d = debugfs_create_u32("rate", S_IRUGO, c->dent, (u32 *)&c->rate);
	if (!d) {
		err = -ENOMEM;
		goto err_out;
	}
	d = debugfs_create_x32("flags", S_IRUGO, c->dent, (u32 *)&c->flags);
	if (!d) {
		err = -ENOMEM;
		goto err_out;
	}
	return 0;

err_out:
	d = c->dent;
	list_for_each_entry(child, &d->d_subdirs, d_u.d_child)
		debugfs_remove(child);
	debugfs_remove(c->dent);
	return err;
}

static int clk_debugfs_register(struct clk *c)
{
	int err;
	struct clk *pa = c->parent;

	if (pa && !pa->dent) {
		err = clk_debugfs_register(pa);
		if (err)
			return err;
	}

	if (!c->dent) {
		err = clk_debugfs_register_one(c);
		if (err)
			return err;
	}
	return 0;
}

static int __init clk_debugfs_init(void)
{
	struct clk *c;
	struct dentry *d;
	int err;

	d = debugfs_create_dir("clock", NULL);
	if (!d)
		return -ENOMEM;
	clk_debugfs_root = d;

	list_for_each_entry(c, &clocks, node) {
		err = clk_debugfs_register(c);
		if (err)
			goto err_out;
	}
	return 0;
err_out:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	debugfs_remove_recursive(clk_debugfs_root);
#endif
	return err;
}
late_initcall(clk_debugfs_init);

#endif /* defined(CONFIG_PM_DEBUG) && defined(CONFIG_DEBUG_FS) */

