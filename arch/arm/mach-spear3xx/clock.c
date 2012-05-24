/*
 * arch/arm/mach-spear3xx/clock.c
 *
 * SPEAr3xx machines clock framework source file
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
#include <asm/mach-types.h>
#include <plat/clock.h>
#include <mach/misc_regs.h>

/* root clks */
/* 32 KHz oscillator clock */
static struct clk osc_32k_clk = {
	.flags = ALWAYS_ENABLED,
	.rate = 32000,
};

/* 24 MHz oscillator clock */
static struct clk osc_24m_clk = {
	.flags = ALWAYS_ENABLED,
	.rate = 24000000,
};

/* clock derived from 32 KHz osc clk */
/* rtc clock */
static struct clk rtc_clk = {
	.pclk = &osc_32k_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = RTC_CLK_ENB,
	.recalc = &follow_parent,
};

/* clock derived from 24 MHz osc clk */
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
	.pclk = &osc_24m_clk,
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
	.pclk = &osc_24m_clk,
	.rate = 48000000,
};

/* watch dog timer clock */
static struct clk wdt_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &osc_24m_clk,
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

/* uart synth configurations */
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
	.rate_config = {aux_rtbl, ARRAY_SIZE(aux_rtbl), 1},
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

/* uart clock */
static struct clk uart_clk = {
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = UART_CLK_ENB,
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
	.rate_config = {aux_rtbl, ARRAY_SIZE(aux_rtbl), 1},
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

/* gpt0 timer clock */
static struct clk gpt0_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk_sel = &gpt0_pclk_sel,
	.pclk_sel_shift = GPT0_CLK_SHIFT,
	.recalc = &follow_parent,
};

/* gpt1 synth clk configurations */
static struct gpt_clk_config gpt1_synth_config = {
	.synth_reg = PRSC2_CLK_CFG,
	.masks = &gpt_masks,
};

/* gpt1 synth clock */
static struct clk gpt1_synth_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &pll1_clk,
	.calc_rate = &gpt_calc_rate,
	.recalc = &gpt_clk_recalc,
	.set_rate = &gpt_clk_set_rate,
	.rate_config = {gpt_rtbl, ARRAY_SIZE(gpt_rtbl), 2},
	.private_data = &gpt1_synth_config,
};

static struct pclk_info gpt1_pclk_info[] = {
	{
		.pclk = &gpt1_synth_clk,
		.pclk_val = AUX_CLK_PLL1_VAL,
	}, {
		.pclk = &pll3_48m_clk,
		.pclk_val = AUX_CLK_PLL3_VAL,
	},
};

/* gpt parent select structure */
static struct pclk_sel gpt1_pclk_sel = {
	.pclk_info = gpt1_pclk_info,
	.pclk_count = ARRAY_SIZE(gpt1_pclk_info),
	.pclk_sel_reg = PERIP_CLK_CFG,
	.pclk_sel_mask = GPT_CLK_MASK,
};

/* gpt1 timer clock */
static struct clk gpt1_clk = {
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = GPT1_CLK_ENB,
	.pclk_sel = &gpt1_pclk_sel,
	.pclk_sel_shift = GPT1_CLK_SHIFT,
	.recalc = &follow_parent,
};

/* gpt2 synth clk configurations */
static struct gpt_clk_config gpt2_synth_config = {
	.synth_reg = PRSC3_CLK_CFG,
	.masks = &gpt_masks,
};

/* gpt1 synth clock */
static struct clk gpt2_synth_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &pll1_clk,
	.calc_rate = &gpt_calc_rate,
	.recalc = &gpt_clk_recalc,
	.set_rate = &gpt_clk_set_rate,
	.rate_config = {gpt_rtbl, ARRAY_SIZE(gpt_rtbl), 2},
	.private_data = &gpt2_synth_config,
};

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
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = GPT2_CLK_ENB,
	.pclk_sel = &gpt2_pclk_sel,
	.pclk_sel_shift = GPT2_CLK_SHIFT,
	.recalc = &follow_parent,
};

/* clock derived from pll3 clk */
/* usbh clock */
static struct clk usbh_clk = {
	.pclk = &pll3_48m_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = USBH_CLK_ENB,
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

/* c3 clock */
static struct clk c3_clk = {
	.pclk = &ahb_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = C3_CLK_ENB,
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

#if defined(CONFIG_MACH_SPEAR310) || defined(CONFIG_MACH_SPEAR320)
/* emi clock */
static struct clk emi_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &ahb_clk,
	.recalc = &follow_parent,
};
#endif

/* ssp clock */
static struct clk ssp0_clk = {
	.pclk = &apb_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = SSP_CLK_ENB,
	.recalc = &follow_parent,
};

/* gpio clock */
static struct clk gpio_clk = {
	.pclk = &apb_clk,
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = GPIO_CLK_ENB,
	.recalc = &follow_parent,
};

static struct clk dummy_apb_pclk;

#if defined(CONFIG_MACH_SPEAR300) || defined(CONFIG_MACH_SPEAR310) || \
	defined(CONFIG_MACH_SPEAR320)
/* fsmc clock */
static struct clk fsmc_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &ahb_clk,
	.recalc = &follow_parent,
};
#endif

/* common clocks to spear310 and spear320 */
#if defined(CONFIG_MACH_SPEAR310) || defined(CONFIG_MACH_SPEAR320)
/* uart1 clock */
static struct clk uart1_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &apb_clk,
	.recalc = &follow_parent,
};

/* uart2 clock */
static struct clk uart2_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &apb_clk,
	.recalc = &follow_parent,
};
#endif /* CONFIG_MACH_SPEAR310 || CONFIG_MACH_SPEAR320 */

/* common clocks to spear300 and spear320 */
#if defined(CONFIG_MACH_SPEAR300) || defined(CONFIG_MACH_SPEAR320)
/* clcd clock */
static struct clk clcd_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &pll3_48m_clk,
	.recalc = &follow_parent,
};

/* sdhci clock */
static struct clk sdhci_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &ahb_clk,
	.recalc = &follow_parent,
};
#endif /* CONFIG_MACH_SPEAR300 || CONFIG_MACH_SPEAR320 */

/* spear300 machine specific clock structures */
#ifdef CONFIG_MACH_SPEAR300
/* gpio1 clock */
static struct clk gpio1_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &apb_clk,
	.recalc = &follow_parent,
};

/* keyboard clock */
static struct clk kbd_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &apb_clk,
	.recalc = &follow_parent,
};

#endif

/* spear310 machine specific clock structures */
#ifdef CONFIG_MACH_SPEAR310
/* uart3 clock */
static struct clk uart3_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &apb_clk,
	.recalc = &follow_parent,
};

/* uart4 clock */
static struct clk uart4_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &apb_clk,
	.recalc = &follow_parent,
};

/* uart5 clock */
static struct clk uart5_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &apb_clk,
	.recalc = &follow_parent,
};
#endif

/* spear320 machine specific clock structures */
#ifdef CONFIG_MACH_SPEAR320
/* can0 clock */
static struct clk can0_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &apb_clk,
	.recalc = &follow_parent,
};

/* can1 clock */
static struct clk can1_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &apb_clk,
	.recalc = &follow_parent,
};

/* i2c1 clock */
static struct clk i2c1_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &ahb_clk,
	.recalc = &follow_parent,
};

/* ssp1 clock */
static struct clk ssp1_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &apb_clk,
	.recalc = &follow_parent,
};

/* ssp2 clock */
static struct clk ssp2_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &apb_clk,
	.recalc = &follow_parent,
};

/* pwm clock */
static struct clk pwm_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &apb_clk,
	.recalc = &follow_parent,
};
#endif

/* array of all spear 3xx clock lookups */
static struct clk_lookup spear_clk_lookups[] = {
	{ .con_id = "apb_pclk",		.clk = &dummy_apb_pclk},
	/* root clks */
	{ .con_id = "osc_32k_clk",	.clk = &osc_32k_clk},
	{ .con_id = "osc_24m_clk",	.clk = &osc_24m_clk},
	/* clock derived from 32 KHz osc clk */
	{ .dev_id = "rtc-spear",	.clk = &rtc_clk},
	/* clock derived from 24 MHz osc clk */
	{ .con_id = "pll1_clk",		.clk = &pll1_clk},
	{ .con_id = "pll3_48m_clk",	.clk = &pll3_48m_clk},
	{ .dev_id = "wdt",		.clk = &wdt_clk},
	/* clock derived from pll1 clk */
	{ .con_id = "cpu_clk",		.clk = &cpu_clk},
	{ .con_id = "ahb_clk",		.clk = &ahb_clk},
	{ .con_id = "uart_synth_clk",	.clk = &uart_synth_clk},
	{ .con_id = "firda_synth_clk",	.clk = &firda_synth_clk},
	{ .con_id = "gpt0_synth_clk",	.clk = &gpt0_synth_clk},
	{ .con_id = "gpt1_synth_clk",	.clk = &gpt1_synth_clk},
	{ .con_id = "gpt2_synth_clk",	.clk = &gpt2_synth_clk},
	{ .dev_id = "uart",		.clk = &uart_clk},
	{ .dev_id = "firda",		.clk = &firda_clk},
	{ .dev_id = "gpt0",		.clk = &gpt0_clk},
	{ .dev_id = "gpt1",		.clk = &gpt1_clk},
	{ .dev_id = "gpt2",		.clk = &gpt2_clk},
	/* clock derived from pll3 clk */
	{ .dev_id = "designware_udc",   .clk = &usbd_clk},
	{ .con_id = "usbh_clk",		.clk = &usbh_clk},
	/* clock derived from ahb clk */
	{ .con_id = "apb_clk",		.clk = &apb_clk},
	{ .dev_id = "i2c_designware.0",	.clk = &i2c_clk},
	{ .dev_id = "dma",		.clk = &dma_clk},
	{ .dev_id = "jpeg",		.clk = &jpeg_clk},
	{ .dev_id = "gmac",		.clk = &gmac_clk},
	{ .dev_id = "smi",		.clk = &smi_clk},
	{ .dev_id = "c3",		.clk = &c3_clk},
	/* clock derived from apb clk */
	{ .dev_id = "adc",		.clk = &adc_clk},
	{ .dev_id = "ssp-pl022.0",	.clk = &ssp0_clk},
	{ .dev_id = "gpio",		.clk = &gpio_clk},
};

/* array of all spear 300 clock lookups */
#ifdef CONFIG_MACH_SPEAR300
static struct clk_lookup spear300_clk_lookups[] = {
	{ .dev_id = "clcd",		.clk = &clcd_clk},
	{ .con_id = "fsmc",		.clk = &fsmc_clk},
	{ .dev_id = "gpio1",		.clk = &gpio1_clk},
	{ .dev_id = "keyboard",		.clk = &kbd_clk},
	{ .dev_id = "sdhci",		.clk = &sdhci_clk},
};
#endif

/* array of all spear 310 clock lookups */
#ifdef CONFIG_MACH_SPEAR310
static struct clk_lookup spear310_clk_lookups[] = {
	{ .con_id = "fsmc",		.clk = &fsmc_clk},
	{ .con_id = "emi",		.clk = &emi_clk},
	{ .dev_id = "uart1",		.clk = &uart1_clk},
	{ .dev_id = "uart2",		.clk = &uart2_clk},
	{ .dev_id = "uart3",		.clk = &uart3_clk},
	{ .dev_id = "uart4",		.clk = &uart4_clk},
	{ .dev_id = "uart5",		.clk = &uart5_clk},
};
#endif

/* array of all spear 320 clock lookups */
#ifdef CONFIG_MACH_SPEAR320
static struct clk_lookup spear320_clk_lookups[] = {
	{ .dev_id = "clcd",		.clk = &clcd_clk},
	{ .con_id = "fsmc",		.clk = &fsmc_clk},
	{ .dev_id = "i2c_designware.1",	.clk = &i2c1_clk},
	{ .con_id = "emi",		.clk = &emi_clk},
	{ .dev_id = "pwm",		.clk = &pwm_clk},
	{ .dev_id = "sdhci",		.clk = &sdhci_clk},
	{ .dev_id = "c_can_platform.0",	.clk = &can0_clk},
	{ .dev_id = "c_can_platform.1",	.clk = &can1_clk},
	{ .dev_id = "ssp-pl022.1",	.clk = &ssp1_clk},
	{ .dev_id = "ssp-pl022.2",	.clk = &ssp2_clk},
	{ .dev_id = "uart1",		.clk = &uart1_clk},
	{ .dev_id = "uart2",		.clk = &uart2_clk},
};
#endif

void __init spear3xx_clk_init(void)
{
	int i, cnt;
	struct clk_lookup *lookups;

	if (machine_is_spear300()) {
		cnt = ARRAY_SIZE(spear300_clk_lookups);
		lookups = spear300_clk_lookups;
	} else if (machine_is_spear310()) {
		cnt = ARRAY_SIZE(spear310_clk_lookups);
		lookups = spear310_clk_lookups;
	} else {
		cnt = ARRAY_SIZE(spear320_clk_lookups);
		lookups = spear320_clk_lookups;
	}

	for (i = 0; i < ARRAY_SIZE(spear_clk_lookups); i++)
		clk_register(&spear_clk_lookups[i]);

	for (i = 0; i < cnt; i++)
		clk_register(&lookups[i]);

	clk_init();
}
