/*
 * arch/arm/mach-spear6xx/clock.c
 *
 * SPEAr6xx machines clock framework source file
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <plat/clock.h>
#include <mach/misc_regs.h>
#include <mach/spear.h>

#define PLL1_CTR		(MISC_BASE + 0x008)
#define PLL1_FRQ		(MISC_BASE + 0x00C)
#define PLL1_MOD		(MISC_BASE + 0x010)
#define PLL2_CTR		(MISC_BASE + 0x014)
/* PLL_CTR register masks */
#define PLL_ENABLE		2
#define PLL_MODE_SHIFT		4
#define PLL_MODE_MASK		0x3
#define PLL_MODE_NORMAL		0
#define PLL_MODE_FRACTION	1
#define PLL_MODE_DITH_DSB	2
#define PLL_MODE_DITH_SSB	3

#define PLL2_FRQ		(MISC_BASE + 0x018)
/* PLL FRQ register masks */
#define PLL_DIV_N_SHIFT		0
#define PLL_DIV_N_MASK		0xFF
#define PLL_DIV_P_SHIFT		8
#define PLL_DIV_P_MASK		0x7
#define PLL_NORM_FDBK_M_SHIFT	24
#define PLL_NORM_FDBK_M_MASK	0xFF
#define PLL_DITH_FDBK_M_SHIFT	16
#define PLL_DITH_FDBK_M_MASK	0xFFFF

#define PLL2_MOD		(MISC_BASE + 0x01C)
#define PLL_CLK_CFG		(MISC_BASE + 0x020)
#define CORE_CLK_CFG		(MISC_BASE + 0x024)
/* CORE CLK CFG register masks */
#define PLL_HCLK_RATIO_SHIFT	10
#define PLL_HCLK_RATIO_MASK	0x3
#define HCLK_PCLK_RATIO_SHIFT	8
#define HCLK_PCLK_RATIO_MASK	0x3

#define PERIP_CLK_CFG		(MISC_BASE + 0x028)
/* PERIP_CLK_CFG register masks */
#define CLCD_CLK_SHIFT		2
#define CLCD_CLK_MASK		0x3
#define UART_CLK_SHIFT		4
#define UART_CLK_MASK		0x1
#define FIRDA_CLK_SHIFT		5
#define FIRDA_CLK_MASK		0x3
#define GPT0_CLK_SHIFT		8
#define GPT1_CLK_SHIFT		10
#define GPT2_CLK_SHIFT		11
#define GPT3_CLK_SHIFT		12
#define GPT_CLK_MASK		0x1
#define AUX_CLK_PLL3_VAL	0
#define AUX_CLK_PLL1_VAL	1

#define PERIP1_CLK_ENB		(MISC_BASE + 0x02C)
/* PERIP1_CLK_ENB register masks */
#define UART0_CLK_ENB		3
#define UART1_CLK_ENB		4
#define SSP0_CLK_ENB		5
#define SSP1_CLK_ENB		6
#define I2C_CLK_ENB		7
#define JPEG_CLK_ENB		8
#define FSMC_CLK_ENB		9
#define FIRDA_CLK_ENB		10
#define GPT2_CLK_ENB		11
#define GPT3_CLK_ENB		12
#define GPIO2_CLK_ENB		13
#define SSP2_CLK_ENB		14
#define ADC_CLK_ENB		15
#define GPT1_CLK_ENB		11
#define RTC_CLK_ENB		17
#define GPIO1_CLK_ENB		18
#define DMA_CLK_ENB		19
#define SMI_CLK_ENB		21
#define CLCD_CLK_ENB		22
#define GMAC_CLK_ENB		23
#define USBD_CLK_ENB		24
#define USBH0_CLK_ENB		25
#define USBH1_CLK_ENB		26

#define PRSC1_CLK_CFG		(MISC_BASE + 0x044)
#define PRSC2_CLK_CFG		(MISC_BASE + 0x048)
#define PRSC3_CLK_CFG		(MISC_BASE + 0x04C)
/* gpt synthesizer register masks */
#define GPT_MSCALE_SHIFT	0
#define GPT_MSCALE_MASK		0xFFF
#define GPT_NSCALE_SHIFT	12
#define GPT_NSCALE_MASK		0xF

#define AMEM_CLK_CFG		(MISC_BASE + 0x050)
#define EXPI_CLK_CFG		(MISC_BASE + 0x054)
#define CLCD_CLK_SYNT		(MISC_BASE + 0x05C)
#define FIRDA_CLK_SYNT		(MISC_BASE + 0x060)
#define UART_CLK_SYNT		(MISC_BASE + 0x064)
#define GMAC_CLK_SYNT		(MISC_BASE + 0x068)
#define RAS1_CLK_SYNT		(MISC_BASE + 0x06C)
#define RAS2_CLK_SYNT		(MISC_BASE + 0x070)
#define RAS3_CLK_SYNT		(MISC_BASE + 0x074)
#define RAS4_CLK_SYNT		(MISC_BASE + 0x078)
/* aux clk synthesiser register masks for irda to ras4 */
#define AUX_SYNT_ENB		31
#define AUX_EQ_SEL_SHIFT	30
#define AUX_EQ_SEL_MASK		1
#define AUX_EQ1_SEL		0
#define AUX_EQ2_SEL		1
#define AUX_XSCALE_SHIFT	16
#define AUX_XSCALE_MASK		0xFFF
#define AUX_YSCALE_SHIFT	0
#define AUX_YSCALE_MASK		0xFFF

/* root clks */
/* 32 KHz oscillator clock */
static struct clk osc_32k_clk = {
	.flags = ALWAYS_ENABLED,
	.rate = 32000,
};

/* 30 MHz oscillator clock */
static struct clk osc_30m_clk = {
	.flags = ALWAYS_ENABLED,
	.rate = 30000000,
};

/* clock derived from 32 KHz osc clk */
/* rtc clock */
static struct clk rtc_clk = {
	.pclk = &osc_32k_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = RTC_CLK_ENB,
	.recalc = &follow_parent,
};

/* clock derived from 30 MHz osc clk */
/* pll masks structure */
static struct pll_clk_masks pll1_masks = {
	.mode_mask = PLL_MODE_MASK,
	.mode_shift = PLL_MODE_SHIFT,
	.norm_fdbk_m_mask = PLL_NORM_FDBK_M_MASK,
	.norm_fdbk_m_shift = PLL_NORM_FDBK_M_SHIFT,
	.dith_fdbk_m_mask = PLL_DITH_FDBK_M_MASK,
	.dith_fdbk_m_shift = PLL_DITH_FDBK_M_SHIFT,
	.div_p_mask = PLL_DIV_P_MASK,
	.div_p_shift = PLL_DIV_P_SHIFT,
	.div_n_mask = PLL_DIV_N_MASK,
	.div_n_shift = PLL_DIV_N_SHIFT,
};

/* pll1 configuration structure */
static struct pll_clk_config pll1_config = {
	.mode_reg = PLL1_CTR,
	.cfg_reg = PLL1_FRQ,
	.masks = &pll1_masks,
};

/* pll rate configuration table, in ascending order of rates */
struct pll_rate_tbl pll_rtbl[] = {
	{.mode = 0, .m = 0x85, .n = 0x0C, .p = 0x1}, /* 266 MHz */
	{.mode = 0, .m = 0xA6, .n = 0x0C, .p = 0x1}, /* 332 MHz */
};

/* PLL1 clock */
static struct clk pll1_clk = {
	.flags = ENABLED_ON_INIT,
	.pclk = &osc_30m_clk,
	.en_reg = PLL1_CTR,
	.en_reg_bit = PLL_ENABLE,
	.calc_rate = &pll_calc_rate,
	.recalc = &pll_clk_recalc,
	.set_rate = &pll_clk_set_rate,
	.rate_config = {pll_rtbl, ARRAY_SIZE(pll_rtbl), 1},
	.private_data = &pll1_config,
};

/* PLL3 48 MHz clock */
static struct clk pll3_48m_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &osc_30m_clk,
	.rate = 48000000,
};

/* watch dog timer clock */
static struct clk wdt_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &osc_30m_clk,
	.recalc = &follow_parent,
};

/* clock derived from pll1 clk */
/* cpu clock */
static struct clk cpu_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &pll1_clk,
	.recalc = &follow_parent,
};

/* ahb masks structure */
static struct bus_clk_masks ahb_masks = {
	.mask = PLL_HCLK_RATIO_MASK,
	.shift = PLL_HCLK_RATIO_SHIFT,
};

/* ahb configuration structure */
static struct bus_clk_config ahb_config = {
	.reg = CORE_CLK_CFG,
	.masks = &ahb_masks,
};

/* ahb rate configuration table, in ascending order of rates */
struct bus_rate_tbl bus_rtbl[] = {
	{.div = 3}, /* == parent divided by 4 */
	{.div = 2}, /* == parent divided by 3 */
	{.div = 1}, /* == parent divided by 2 */
	{.div = 0}, /* == parent divided by 1 */
};

/* ahb clock */
static struct clk ahb_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &pll1_clk,
	.calc_rate = &bus_calc_rate,
	.recalc = &bus_clk_recalc,
	.set_rate = &bus_clk_set_rate,
	.rate_config = {bus_rtbl, ARRAY_SIZE(bus_rtbl), 2},
	.private_data = &ahb_config,
};

/* auxiliary synthesizers masks */
static struct aux_clk_masks aux_masks = {
	.eq_sel_mask = AUX_EQ_SEL_MASK,
	.eq_sel_shift = AUX_EQ_SEL_SHIFT,
	.eq1_mask = AUX_EQ1_SEL,
	.eq2_mask = AUX_EQ2_SEL,
	.xscale_sel_mask = AUX_XSCALE_MASK,
	.xscale_sel_shift = AUX_XSCALE_SHIFT,
	.yscale_sel_mask = AUX_YSCALE_MASK,
	.yscale_sel_shift = AUX_YSCALE_SHIFT,
};

/* uart configurations */
static struct aux_clk_config uart_synth_config = {
	.synth_reg = UART_CLK_SYNT,
	.masks = &aux_masks,
};

/* aux rate configuration table, in ascending order of rates */
struct aux_rate_tbl aux_rtbl[] = {
	/* For PLL1 = 332 MHz */
	{.xscale = 1, .yscale = 8, .eq = 1}, /* 41.5 MHz */
	{.xscale = 1, .yscale = 4, .eq = 1}, /* 83 MHz */
	{.xscale = 1, .yscale = 2, .eq = 1}, /* 166 MHz */
};

/* uart synth clock */
static struct clk uart_synth_clk = {
	.en_reg = UART_CLK_SYNT,
	.en_reg_bit = AUX_SYNT_ENB,
	.pclk = &pll1_clk,
	.calc_rate = &aux_calc_rate,
	.recalc = &aux_clk_recalc,
	.set_rate = &aux_clk_set_rate,
	.rate_config = {aux_rtbl, ARRAY_SIZE(aux_rtbl), 2},
	.private_data = &uart_synth_config,
};

/* uart parents */
static struct pclk_info uart_pclk_info[] = {
	{
		.pclk = &uart_synth_clk,
		.pclk_val = AUX_CLK_PLL1_VAL,
	}, {
		.pclk = &pll3_48m_clk,
		.pclk_val = AUX_CLK_PLL3_VAL,
	},
};

/* uart parent select structure */
static struct pclk_sel uart_pclk_sel = {
	.pclk_info = uart_pclk_info,
	.pclk_count = ARRAY_SIZE(uart_pclk_info),
	.pclk_sel_reg = PERIP_CLK_CFG,
	.pclk_sel_mask = UART_CLK_MASK,
};

/* uart0 clock */
static struct clk uart0_clk = {
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = UART0_CLK_ENB,
	.pclk_sel = &uart_pclk_sel,
	.pclk_sel_shift = UART_CLK_SHIFT,
	.recalc = &follow_parent,
};

/* uart1 clock */
static struct clk uart1_clk = {
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = UART1_CLK_ENB,
	.pclk_sel = &uart_pclk_sel,
	.pclk_sel_shift = UART_CLK_SHIFT,
	.recalc = &follow_parent,
};

/* firda configurations */
static struct aux_clk_config firda_synth_config = {
	.synth_reg = FIRDA_CLK_SYNT,
	.masks = &aux_masks,
};

/* firda synth clock */
static struct clk firda_synth_clk = {
	.en_reg = FIRDA_CLK_SYNT,
	.en_reg_bit = AUX_SYNT_ENB,
	.pclk = &pll1_clk,
	.calc_rate = &aux_calc_rate,
	.recalc = &aux_clk_recalc,
	.set_rate = &aux_clk_set_rate,
	.rate_config = {aux_rtbl, ARRAY_SIZE(aux_rtbl), 2},
	.private_data = &firda_synth_config,
};

/* firda parents */
static struct pclk_info firda_pclk_info[] = {
	{
		.pclk = &firda_synth_clk,
		.pclk_val = AUX_CLK_PLL1_VAL,
	}, {
		.pclk = &pll3_48m_clk,
		.pclk_val = AUX_CLK_PLL3_VAL,
	},
};

/* firda parent select structure */
static struct pclk_sel firda_pclk_sel = {
	.pclk_info = firda_pclk_info,
	.pclk_count = ARRAY_SIZE(firda_pclk_info),
	.pclk_sel_reg = PERIP_CLK_CFG,
	.pclk_sel_mask = FIRDA_CLK_MASK,
};

/* firda clock */
static struct clk firda_clk = {
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = FIRDA_CLK_ENB,
	.pclk_sel = &firda_pclk_sel,
	.pclk_sel_shift = FIRDA_CLK_SHIFT,
	.recalc = &follow_parent,
};

/* clcd configurations */
static struct aux_clk_config clcd_synth_config = {
	.synth_reg = CLCD_CLK_SYNT,
	.masks = &aux_masks,
};

/* firda synth clock */
static struct clk clcd_synth_clk = {
	.en_reg = CLCD_CLK_SYNT,
	.en_reg_bit = AUX_SYNT_ENB,
	.pclk = &pll1_clk,
	.calc_rate = &aux_calc_rate,
	.recalc = &aux_clk_recalc,
	.set_rate = &aux_clk_set_rate,
	.rate_config = {aux_rtbl, ARRAY_SIZE(aux_rtbl), 2},
	.private_data = &clcd_synth_config,
};

/* clcd parents */
static struct pclk_info clcd_pclk_info[] = {
	{
		.pclk = &clcd_synth_clk,
		.pclk_val = AUX_CLK_PLL1_VAL,
	}, {
		.pclk = &pll3_48m_clk,
		.pclk_val = AUX_CLK_PLL3_VAL,
	},
};

/* clcd parent select structure */
static struct pclk_sel clcd_pclk_sel = {
	.pclk_info = clcd_pclk_info,
	.pclk_count = ARRAY_SIZE(clcd_pclk_info),
	.pclk_sel_reg = PERIP_CLK_CFG,
	.pclk_sel_mask = CLCD_CLK_MASK,
};

/* clcd clock */
static struct clk clcd_clk = {
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = CLCD_CLK_ENB,
	.pclk_sel = &clcd_pclk_sel,
	.pclk_sel_shift = CLCD_CLK_SHIFT,
	.recalc = &follow_parent,
};

/* gpt synthesizer masks */
static struct gpt_clk_masks gpt_masks = {
	.mscale_sel_mask = GPT_MSCALE_MASK,
	.mscale_sel_shift = GPT_MSCALE_SHIFT,
	.nscale_sel_mask = GPT_NSCALE_MASK,
	.nscale_sel_shift = GPT_NSCALE_SHIFT,
};

/* gpt rate configuration table, in ascending order of rates */
struct gpt_rate_tbl gpt_rtbl[] = {
	/* For pll1 = 332 MHz */
	{.mscale = 4, .nscale = 0}, /* 41.5 MHz */
	{.mscale = 2, .nscale = 0}, /* 55.3 MHz */
	{.mscale = 1, .nscale = 0}, /* 83 MHz */
};

/* gpt0 synth clk config*/
static struct gpt_clk_config gpt0_synth_config = {
	.synth_reg = PRSC1_CLK_CFG,
	.masks = &gpt_masks,
};

/* gpt synth clock */
static struct clk gpt0_synth_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &pll1_clk,
	.calc_rate = &gpt_calc_rate,
	.recalc = &gpt_clk_recalc,
	.set_rate = &gpt_clk_set_rate,
	.rate_config = {gpt_rtbl, ARRAY_SIZE(gpt_rtbl), 2},
	.private_data = &gpt0_synth_config,
};

/* gpt parents */
static struct pclk_info gpt0_pclk_info[] = {
	{
		.pclk = &gpt0_synth_clk,
		.pclk_val = AUX_CLK_PLL1_VAL,
	}, {
		.pclk = &pll3_48m_clk,
		.pclk_val = AUX_CLK_PLL3_VAL,
	},
};

/* gpt parent select structure */
static struct pclk_sel gpt0_pclk_sel = {
	.pclk_info = gpt0_pclk_info,
	.pclk_count = ARRAY_SIZE(gpt0_pclk_info),
	.pclk_sel_reg = PERIP_CLK_CFG,
	.pclk_sel_mask = GPT_CLK_MASK,
};

/* gpt0 ARM1 subsystem timer clock */
static struct clk gpt0_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk_sel = &gpt0_pclk_sel,
	.pclk_sel_shift = GPT0_CLK_SHIFT,
	.recalc = &follow_parent,
};


/* Note: gpt0 and gpt1 share same parent clocks */
/* gpt parent select structure */
static struct pclk_sel gpt1_pclk_sel = {
	.pclk_info = gpt0_pclk_info,
	.pclk_count = ARRAY_SIZE(gpt0_pclk_info),
	.pclk_sel_reg = PERIP_CLK_CFG,
	.pclk_sel_mask = GPT_CLK_MASK,
};

/* gpt1 timer clock */
static struct clk gpt1_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk_sel = &gpt1_pclk_sel,
	.pclk_sel_shift = GPT1_CLK_SHIFT,
	.recalc = &follow_parent,
};

/* gpt2 synth clk config*/
static struct gpt_clk_config gpt2_synth_config = {
	.synth_reg = PRSC2_CLK_CFG,
	.masks = &gpt_masks,
};

/* gpt synth clock */
static struct clk gpt2_synth_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &pll1_clk,
	.calc_rate = &gpt_calc_rate,
	.recalc = &gpt_clk_recalc,
	.set_rate = &gpt_clk_set_rate,
	.rate_config = {gpt_rtbl, ARRAY_SIZE(gpt_rtbl), 2},
	.private_data = &gpt2_synth_config,
};

/* gpt parents */
static struct pclk_info gpt2_pclk_info[] = {
	{
		.pclk = &gpt2_synth_clk,
		.pclk_val = AUX_CLK_PLL1_VAL,
	}, {
		.pclk = &pll3_48m_clk,
		.pclk_val = AUX_CLK_PLL3_VAL,
	},
};

/* gpt parent select structure */
static struct pclk_sel gpt2_pclk_sel = {
	.pclk_info = gpt2_pclk_info,
	.pclk_count = ARRAY_SIZE(gpt2_pclk_info),
	.pclk_sel_reg = PERIP_CLK_CFG,
	.pclk_sel_mask = GPT_CLK_MASK,
};

/* gpt2 timer clock */
static struct clk gpt2_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk_sel = &gpt2_pclk_sel,
	.pclk_sel_shift = GPT2_CLK_SHIFT,
	.recalc = &follow_parent,
};

/* gpt3 synth clk config*/
static struct gpt_clk_config gpt3_synth_config = {
	.synth_reg = PRSC3_CLK_CFG,
	.masks = &gpt_masks,
};

/* gpt synth clock */
static struct clk gpt3_synth_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &pll1_clk,
	.calc_rate = &gpt_calc_rate,
	.recalc = &gpt_clk_recalc,
	.set_rate = &gpt_clk_set_rate,
	.rate_config = {gpt_rtbl, ARRAY_SIZE(gpt_rtbl), 2},
	.private_data = &gpt3_synth_config,
};

/* gpt parents */
static struct pclk_info gpt3_pclk_info[] = {
	{
		.pclk = &gpt3_synth_clk,
		.pclk_val = AUX_CLK_PLL1_VAL,
	}, {
		.pclk = &pll3_48m_clk,
		.pclk_val = AUX_CLK_PLL3_VAL,
	},
};

/* gpt parent select structure */
static struct pclk_sel gpt3_pclk_sel = {
	.pclk_info = gpt3_pclk_info,
	.pclk_count = ARRAY_SIZE(gpt3_pclk_info),
	.pclk_sel_reg = PERIP_CLK_CFG,
	.pclk_sel_mask = GPT_CLK_MASK,
};

/* gpt3 timer clock */
static struct clk gpt3_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk_sel = &gpt3_pclk_sel,
	.pclk_sel_shift = GPT3_CLK_SHIFT,
	.recalc = &follow_parent,
};

/* clock derived from pll3 clk */
/* usbh0 clock */
static struct clk usbh0_clk = {
	.pclk = &pll3_48m_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = USBH0_CLK_ENB,
	.recalc = &follow_parent,
};

/* usbh1 clock */
static struct clk usbh1_clk = {
	.pclk = &pll3_48m_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = USBH1_CLK_ENB,
	.recalc = &follow_parent,
};

/* usbd clock */
static struct clk usbd_clk = {
	.pclk = &pll3_48m_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = USBD_CLK_ENB,
	.recalc = &follow_parent,
};

/* clock derived from ahb clk */
/* apb masks structure */
static struct bus_clk_masks apb_masks = {
	.mask = HCLK_PCLK_RATIO_MASK,
	.shift = HCLK_PCLK_RATIO_SHIFT,
};

/* apb configuration structure */
static struct bus_clk_config apb_config = {
	.reg = CORE_CLK_CFG,
	.masks = &apb_masks,
};

/* apb clock */
static struct clk apb_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &ahb_clk,
	.calc_rate = &bus_calc_rate,
	.recalc = &bus_clk_recalc,
	.set_rate = &bus_clk_set_rate,
	.rate_config = {bus_rtbl, ARRAY_SIZE(bus_rtbl), 2},
	.private_data = &apb_config,
};

/* i2c clock */
static struct clk i2c_clk = {
	.pclk = &ahb_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = I2C_CLK_ENB,
	.recalc = &follow_parent,
};

/* dma clock */
static struct clk dma_clk = {
	.pclk = &ahb_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = DMA_CLK_ENB,
	.recalc = &follow_parent,
};

/* jpeg clock */
static struct clk jpeg_clk = {
	.pclk = &ahb_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = JPEG_CLK_ENB,
	.recalc = &follow_parent,
};

/* gmac clock */
static struct clk gmac_clk = {
	.pclk = &ahb_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = GMAC_CLK_ENB,
	.recalc = &follow_parent,
};

/* smi clock */
static struct clk smi_clk = {
	.pclk = &ahb_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = SMI_CLK_ENB,
	.recalc = &follow_parent,
};

/* fsmc clock */
static struct clk fsmc_clk = {
	.pclk = &ahb_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = FSMC_CLK_ENB,
	.recalc = &follow_parent,
};

/* clock derived from apb clk */
/* adc clock */
static struct clk adc_clk = {
	.pclk = &apb_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = ADC_CLK_ENB,
	.recalc = &follow_parent,
};

/* ssp0 clock */
static struct clk ssp0_clk = {
	.pclk = &apb_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = SSP0_CLK_ENB,
	.recalc = &follow_parent,
};

/* ssp1 clock */
static struct clk ssp1_clk = {
	.pclk = &apb_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = SSP1_CLK_ENB,
	.recalc = &follow_parent,
};

/* ssp2 clock */
static struct clk ssp2_clk = {
	.pclk = &apb_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = SSP2_CLK_ENB,
	.recalc = &follow_parent,
};

/* gpio0 ARM subsystem clock */
static struct clk gpio0_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &apb_clk,
	.recalc = &follow_parent,
};

/* gpio1 clock */
static struct clk gpio1_clk = {
	.pclk = &apb_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = GPIO1_CLK_ENB,
	.recalc = &follow_parent,
};

/* gpio2 clock */
static struct clk gpio2_clk = {
	.pclk = &apb_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = GPIO2_CLK_ENB,
	.recalc = &follow_parent,
};

static struct clk dummy_apb_pclk;

/* array of all spear 6xx clock lookups */
static struct clk_lookup spear_clk_lookups[] = {
	CLKDEV_INIT(NULL, "apb_pclk", &dummy_apb_pclk),
	/* root clks */
	CLKDEV_INIT(NULL, "osc_32k_clk", &osc_32k_clk),
	CLKDEV_INIT(NULL, "osc_30m_clk", &osc_30m_clk),
	/* clock derived from 32 KHz os		 clk */
	CLKDEV_INIT("rtc-spear", NULL, &rtc_clk),
	/* clock derived from 30 MHz os		 clk */
	CLKDEV_INIT(NULL, "pll1_clk", &pll1_clk),
	CLKDEV_INIT(NULL, "pll3_48m_clk", &pll3_48m_clk),
	CLKDEV_INIT("wdt", NULL, &wdt_clk),
	/* clock derived from pll1 clk */
	CLKDEV_INIT(NULL, "cpu_clk", &cpu_clk),
	CLKDEV_INIT(NULL, "ahb_clk", &ahb_clk),
	CLKDEV_INIT(NULL, "uart_synth_clk", &uart_synth_clk),
	CLKDEV_INIT(NULL, "firda_synth_clk", &firda_synth_clk),
	CLKDEV_INIT(NULL, "clcd_synth_clk", &clcd_synth_clk),
	CLKDEV_INIT(NULL, "gpt0_synth_clk", &gpt0_synth_clk),
	CLKDEV_INIT(NULL, "gpt2_synth_clk", &gpt2_synth_clk),
	CLKDEV_INIT(NULL, "gpt3_synth_clk", &gpt3_synth_clk),
	CLKDEV_INIT("d0000000.serial", NULL, &uart0_clk),
	CLKDEV_INIT("d0080000.serial", NULL, &uart1_clk),
	CLKDEV_INIT("firda", NULL, &firda_clk),
	CLKDEV_INIT("clcd", NULL, &clcd_clk),
	CLKDEV_INIT("gpt0", NULL, &gpt0_clk),
	CLKDEV_INIT("gpt1", NULL, &gpt1_clk),
	CLKDEV_INIT("gpt2", NULL, &gpt2_clk),
	CLKDEV_INIT("gpt3", NULL, &gpt3_clk),
	/* clock derived from pll3 clk */
	CLKDEV_INIT("designware_udc", NULL, &usbd_clk),
	CLKDEV_INIT(NULL, "usbh.0_clk", &usbh0_clk),
	CLKDEV_INIT(NULL, "usbh.1_clk", &usbh1_clk),
	/* clock derived from ahb clk */
	CLKDEV_INIT(NULL, "apb_clk", &apb_clk),
	CLKDEV_INIT("d0200000.i2c", NULL, &i2c_clk),
	CLKDEV_INIT("fc400000.dma", NULL, &dma_clk),
	CLKDEV_INIT("jpeg", NULL, &jpeg_clk),
	CLKDEV_INIT("gmac", NULL, &gmac_clk),
	CLKDEV_INIT("fc000000.flash", NULL, &smi_clk),
	CLKDEV_INIT("d1800000.flash", NULL, &fsmc_clk),
	/* clock derived from apb clk */
	CLKDEV_INIT("adc", NULL, &adc_clk),
	CLKDEV_INIT("ssp-pl022.0", NULL, &ssp0_clk),
	CLKDEV_INIT("ssp-pl022.1", NULL, &ssp1_clk),
	CLKDEV_INIT("ssp-pl022.2", NULL, &ssp2_clk),
	CLKDEV_INIT("f0100000.gpio", NULL, &gpio0_clk),
	CLKDEV_INIT("fc980000.gpio", NULL, &gpio1_clk),
	CLKDEV_INIT("d8100000.gpio", NULL, &gpio2_clk),
};

void __init spear6xx_clk_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(spear_clk_lookups); i++)
		clk_register(&spear_clk_lookups[i]);

	clk_init();
}
