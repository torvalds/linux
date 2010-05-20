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
#include <linux/kernel.h>
#include <mach/misc_regs.h>
#include <plat/clock.h>

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
/* pll1 configuration structure */
static struct pll_clk_config pll1_config = {
	.mode_reg = PLL1_CTR,
	.cfg_reg = PLL1_FRQ,
};

/* PLL1 clock */
static struct clk pll1_clk = {
	.pclk = &osc_24m_clk,
	.en_reg = PLL1_CTR,
	.en_reg_bit = PLL_ENABLE,
	.recalc = &pll1_clk_recalc,
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

/* ahb configuration structure */
static struct bus_clk_config ahb_config = {
	.reg = CORE_CLK_CFG,
	.mask = PLL_HCLK_RATIO_MASK,
	.shift = PLL_HCLK_RATIO_SHIFT,
};

/* ahb clock */
static struct clk ahb_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &pll1_clk,
	.recalc = &bus_clk_recalc,
	.private_data = &ahb_config,
};

/* uart configurations */
static struct aux_clk_config uart_config = {
	.synth_reg = UART_CLK_SYNT,
};

/* uart parents */
static struct pclk_info uart_pclk_info[] = {
	{
		.pclk = &pll1_clk,
		.pclk_mask = AUX_CLK_PLL1_MASK,
		.scalable = 1,
	}, {
		.pclk = &pll3_48m_clk,
		.pclk_mask = AUX_CLK_PLL3_MASK,
		.scalable = 0,
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
	.recalc = &aux_clk_recalc,
	.private_data = &uart_config,
};

/* firda configurations */
static struct aux_clk_config firda_config = {
	.synth_reg = FIRDA_CLK_SYNT,
};

/* firda parents */
static struct pclk_info firda_pclk_info[] = {
	{
		.pclk = &pll1_clk,
		.pclk_mask = AUX_CLK_PLL1_MASK,
		.scalable = 1,
	}, {
		.pclk = &pll3_48m_clk,
		.pclk_mask = AUX_CLK_PLL3_MASK,
		.scalable = 0,
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
	.recalc = &aux_clk_recalc,
	.private_data = &firda_config,
};

/* gpt parents */
static struct pclk_info gpt_pclk_info[] = {
	{
		.pclk = &pll1_clk,
		.pclk_mask = AUX_CLK_PLL1_MASK,
		.scalable = 1,
	}, {
		.pclk = &pll3_48m_clk,
		.pclk_mask = AUX_CLK_PLL3_MASK,
		.scalable = 0,
	},
};

/* gpt parent select structure */
static struct pclk_sel gpt_pclk_sel = {
	.pclk_info = gpt_pclk_info,
	.pclk_count = ARRAY_SIZE(gpt_pclk_info),
	.pclk_sel_reg = PERIP_CLK_CFG,
	.pclk_sel_mask = GPT_CLK_MASK,
};

/* gpt0 configurations */
static struct aux_clk_config gpt0_config = {
	.synth_reg = PRSC1_CLK_CFG,
};

/* gpt0 timer clock */
static struct clk gpt0_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk_sel = &gpt_pclk_sel,
	.pclk_sel_shift = GPT0_CLK_SHIFT,
	.recalc = &gpt_clk_recalc,
	.private_data = &gpt0_config,
};

/* gpt1 configurations */
static struct aux_clk_config gpt1_config = {
	.synth_reg = PRSC2_CLK_CFG,
};

/* gpt1 timer clock */
static struct clk gpt1_clk = {
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = GPT1_CLK_ENB,
	.pclk_sel = &gpt_pclk_sel,
	.pclk_sel_shift = GPT1_CLK_SHIFT,
	.recalc = &gpt_clk_recalc,
	.private_data = &gpt1_config,
};

/* gpt2 configurations */
static struct aux_clk_config gpt2_config = {
	.synth_reg = PRSC3_CLK_CFG,
};

/* gpt2 timer clock */
static struct clk gpt2_clk = {
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = GPT2_CLK_ENB,
	.pclk_sel = &gpt_pclk_sel,
	.pclk_sel_shift = GPT2_CLK_SHIFT,
	.recalc = &gpt_clk_recalc,
	.private_data = &gpt2_config,
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

/* clcd clock */
static struct clk clcd_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &pll3_48m_clk,
	.recalc = &follow_parent,
};

/* clock derived from ahb clk */
/* apb configuration structure */
static struct bus_clk_config apb_config = {
	.reg = CORE_CLK_CFG,
	.mask = HCLK_PCLK_RATIO_MASK,
	.shift = HCLK_PCLK_RATIO_SHIFT,
};

/* apb clock */
static struct clk apb_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk = &ahb_clk,
	.recalc = &bus_clk_recalc,
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

/* ssp clock */
static struct clk ssp_clk = {
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

/* array of all spear 3xx clock lookups */
static struct clk_lookup spear_clk_lookups[] = {
	/* root clks */
	{ .con_id = "osc_32k_clk",	.clk = &osc_32k_clk},
	{ .con_id = "osc_24m_clk",	.clk = &osc_24m_clk},
	/* clock derived from 32 KHz osc clk */
	{ .dev_id = "rtc",		.clk = &rtc_clk},
	/* clock derived from 24 MHz osc clk */
	{ .con_id = "pll1_clk",		.clk = &pll1_clk},
	{ .con_id = "pll3_48m_clk",	.clk = &pll3_48m_clk},
	{ .dev_id = "wdt",		.clk = &wdt_clk},
	/* clock derived from pll1 clk */
	{ .con_id = "cpu_clk",		.clk = &cpu_clk},
	{ .con_id = "ahb_clk",		.clk = &ahb_clk},
	{ .dev_id = "uart",		.clk = &uart_clk},
	{ .dev_id = "firda",		.clk = &firda_clk},
	{ .dev_id = "gpt0",		.clk = &gpt0_clk},
	{ .dev_id = "gpt1",		.clk = &gpt1_clk},
	{ .dev_id = "gpt2",		.clk = &gpt2_clk},
	/* clock derived from pll3 clk */
	{ .dev_id = "usbh",		.clk = &usbh_clk},
	{ .dev_id = "usbd",		.clk = &usbd_clk},
	{ .dev_id = "clcd",		.clk = &clcd_clk},
	/* clock derived from ahb clk */
	{ .con_id = "apb_clk",		.clk = &apb_clk},
	{ .dev_id = "i2c",		.clk = &i2c_clk},
	{ .dev_id = "dma",		.clk = &dma_clk},
	{ .dev_id = "jpeg",		.clk = &jpeg_clk},
	{ .dev_id = "gmac",		.clk = &gmac_clk},
	{ .dev_id = "smi",		.clk = &smi_clk},
	{ .dev_id = "c3",		.clk = &c3_clk},
	/* clock derived from apb clk */
	{ .dev_id = "adc",		.clk = &adc_clk},
	{ .dev_id = "ssp",		.clk = &ssp_clk},
	{ .dev_id = "gpio",		.clk = &gpio_clk},
};

void __init clk_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(spear_clk_lookups); i++)
		clk_register(&spear_clk_lookups[i]);

	recalc_root_clocks();
}
