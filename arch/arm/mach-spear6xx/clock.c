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
#include <linux/kernel.h>
#include <mach/misc_regs.h>
#include <plat/clock.h>

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
/* pll1 configuration structure */
static struct pll_clk_config pll1_config = {
	.mode_reg = PLL1_CTR,
	.cfg_reg = PLL1_FRQ,
};

/* PLL1 clock */
static struct clk pll1_clk = {
	.pclk = &osc_30m_clk,
	.en_reg = PLL1_CTR,
	.en_reg_bit = PLL_ENABLE,
	.recalc = &pll1_clk_recalc,
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

/* uart configurations */
static struct aux_clk_config uart_config = {
	.synth_reg = UART_CLK_SYNT,
};

/* uart0 clock */
static struct clk uart0_clk = {
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = UART0_CLK_ENB,
	.pclk_sel = &uart_pclk_sel,
	.pclk_sel_shift = UART_CLK_SHIFT,
	.recalc = &aux_clk_recalc,
	.private_data = &uart_config,
};

/* uart1 clock */
static struct clk uart1_clk = {
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = UART1_CLK_ENB,
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

/* clcd configurations */
static struct aux_clk_config clcd_config = {
	.synth_reg = CLCD_CLK_SYNT,
};

/* clcd parents */
static struct pclk_info clcd_pclk_info[] = {
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
	.recalc = &aux_clk_recalc,
	.private_data = &clcd_config,
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

/* gpt0_1 configurations */
static struct aux_clk_config gpt0_1_config = {
	.synth_reg = PRSC1_CLK_CFG,
};

/* gpt0 ARM1 subsystem timer clock */
static struct clk gpt0_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk_sel = &gpt_pclk_sel,
	.pclk_sel_shift = GPT0_CLK_SHIFT,
	.recalc = &gpt_clk_recalc,
	.private_data = &gpt0_1_config,
};

/* gpt1 timer clock */
static struct clk gpt1_clk = {
	.flags = ALWAYS_ENABLED,
	.pclk_sel = &gpt_pclk_sel,
	.pclk_sel_shift = GPT1_CLK_SHIFT,
	.recalc = &gpt_clk_recalc,
	.private_data = &gpt0_1_config,
};

/* gpt2 configurations */
static struct aux_clk_config gpt2_config = {
	.synth_reg = PRSC2_CLK_CFG,
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

/* gpt3 configurations */
static struct aux_clk_config gpt3_config = {
	.synth_reg = PRSC3_CLK_CFG,
};

/* gpt3 timer clock */
static struct clk gpt3_clk = {
	.en_reg = PERIP1_CLK_ENB,
	.en_reg_bit = GPT3_CLK_ENB,
	.pclk_sel = &gpt_pclk_sel,
	.pclk_sel_shift = GPT3_CLK_SHIFT,
	.recalc = &gpt_clk_recalc,
	.private_data = &gpt3_config,
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
	{ .con_id = "apb_pclk",	.clk = &dummy_apb_pclk},
	/* root clks */
	{ .con_id = "osc_32k_clk",	.clk = &osc_32k_clk},
	{ .con_id = "osc_30m_clk",	.clk = &osc_30m_clk},
	/* clock derived from 32 KHz os		 clk */
	{ .dev_id = "rtc",		.clk = &rtc_clk},
	/* clock derived from 30 MHz os		 clk */
	{ .con_id = "pll1_clk",		.clk = &pll1_clk},
	{ .con_id = "pll3_48m_clk",	.clk = &pll3_48m_clk},
	{ .dev_id = "wdt",		.clk = &wdt_clk},
	/* clock derived from pll1 clk */
	{ .con_id = "cpu_clk",		.clk = &cpu_clk},
	{ .con_id = "ahb_clk",		.clk = &ahb_clk},
	{ .dev_id = "uart0",		.clk = &uart0_clk},
	{ .dev_id = "uart1",		.clk = &uart1_clk},
	{ .dev_id = "firda",		.clk = &firda_clk},
	{ .dev_id = "clcd",		.clk = &clcd_clk},
	{ .dev_id = "gpt0",		.clk = &gpt0_clk},
	{ .dev_id = "gpt1",		.clk = &gpt1_clk},
	{ .dev_id = "gpt2",		.clk = &gpt2_clk},
	{ .dev_id = "gpt3",		.clk = &gpt3_clk},
	/* clock derived from pll3 clk */
	{ .dev_id = "usbh0",		.clk = &usbh0_clk},
	{ .dev_id = "usbh1",		.clk = &usbh1_clk},
	{ .dev_id = "usbd",		.clk = &usbd_clk},
	/* clock derived from ahb clk */
	{ .con_id = "apb_clk",		.clk = &apb_clk},
	{ .dev_id = "i2c",		.clk = &i2c_clk},
	{ .dev_id = "dma",		.clk = &dma_clk},
	{ .dev_id = "jpeg",		.clk = &jpeg_clk},
	{ .dev_id = "gmac",		.clk = &gmac_clk},
	{ .dev_id = "smi",		.clk = &smi_clk},
	{ .dev_id = "fsmc",		.clk = &fsmc_clk},
	/* clock derived from apb clk */
	{ .dev_id = "adc",		.clk = &adc_clk},
	{ .dev_id = "ssp0",		.clk = &ssp0_clk},
	{ .dev_id = "ssp1",		.clk = &ssp1_clk},
	{ .dev_id = "ssp2",		.clk = &ssp2_clk},
	{ .dev_id = "gpio0",		.clk = &gpio0_clk},
	{ .dev_id = "gpio1",		.clk = &gpio1_clk},
	{ .dev_id = "gpio2",		.clk = &gpio2_clk},
};

void __init clk_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(spear_clk_lookups); i++)
		clk_register(&spear_clk_lookups[i]);

	recalc_root_clocks();
}
