/*
 * SPEAr6xx machines clock framework source file
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <vireshk@kernel.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/spinlock_types.h>
#include "clk.h"

static DEFINE_SPINLOCK(_lock);

#define PLL1_CTR			(misc_base + 0x008)
#define PLL1_FRQ			(misc_base + 0x00C)
#define PLL2_CTR			(misc_base + 0x014)
#define PLL2_FRQ			(misc_base + 0x018)
#define PLL_CLK_CFG			(misc_base + 0x020)
	/* PLL_CLK_CFG register masks */
	#define MCTR_CLK_SHIFT		28
	#define MCTR_CLK_MASK		3

#define CORE_CLK_CFG			(misc_base + 0x024)
	/* CORE CLK CFG register masks */
	#define HCLK_RATIO_SHIFT	10
	#define HCLK_RATIO_MASK		2
	#define PCLK_RATIO_SHIFT	8
	#define PCLK_RATIO_MASK		2

#define PERIP_CLK_CFG			(misc_base + 0x028)
	/* PERIP_CLK_CFG register masks */
	#define CLCD_CLK_SHIFT		2
	#define CLCD_CLK_MASK		2
	#define UART_CLK_SHIFT		4
	#define UART_CLK_MASK		1
	#define FIRDA_CLK_SHIFT		5
	#define FIRDA_CLK_MASK		2
	#define GPT0_CLK_SHIFT		8
	#define GPT1_CLK_SHIFT		10
	#define GPT2_CLK_SHIFT		11
	#define GPT3_CLK_SHIFT		12
	#define GPT_CLK_MASK		1

#define PERIP1_CLK_ENB			(misc_base + 0x02C)
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

#define PRSC0_CLK_CFG			(misc_base + 0x044)
#define PRSC1_CLK_CFG			(misc_base + 0x048)
#define PRSC2_CLK_CFG			(misc_base + 0x04C)

#define CLCD_CLK_SYNT			(misc_base + 0x05C)
#define FIRDA_CLK_SYNT			(misc_base + 0x060)
#define UART_CLK_SYNT			(misc_base + 0x064)

/* vco rate configuration table, in ascending order of rates */
static struct pll_rate_tbl pll_rtbl[] = {
	{.mode = 0, .m = 0x53, .n = 0x0F, .p = 0x1}, /* vco 332 & pll 166 MHz */
	{.mode = 0, .m = 0x85, .n = 0x0F, .p = 0x1}, /* vco 532 & pll 266 MHz */
	{.mode = 0, .m = 0xA6, .n = 0x0F, .p = 0x1}, /* vco 664 & pll 332 MHz */
};

/* aux rate configuration table, in ascending order of rates */
static struct aux_rate_tbl aux_rtbl[] = {
	/* For PLL1 = 332 MHz */
	{.xscale = 2, .yscale = 27, .eq = 0}, /* 12.296 MHz */
	{.xscale = 2, .yscale = 8, .eq = 0}, /* 41.5 MHz */
	{.xscale = 2, .yscale = 4, .eq = 0}, /* 83 MHz */
	{.xscale = 1, .yscale = 2, .eq = 1}, /* 166 MHz */
};

static const char *clcd_parents[] = { "pll3_clk", "clcd_syn_gclk", };
static const char *firda_parents[] = { "pll3_clk", "firda_syn_gclk", };
static const char *uart_parents[] = { "pll3_clk", "uart_syn_gclk", };
static const char *gpt0_1_parents[] = { "pll3_clk", "gpt0_1_syn_clk", };
static const char *gpt2_parents[] = { "pll3_clk", "gpt2_syn_clk", };
static const char *gpt3_parents[] = { "pll3_clk", "gpt3_syn_clk", };
static const char *ddr_parents[] = { "ahb_clk", "ahbmult2_clk", "none",
	"pll2_clk", };

/* gpt rate configuration table, in ascending order of rates */
static struct gpt_rate_tbl gpt_rtbl[] = {
	/* For pll1 = 332 MHz */
	{.mscale = 4, .nscale = 0}, /* 41.5 MHz */
	{.mscale = 2, .nscale = 0}, /* 55.3 MHz */
	{.mscale = 1, .nscale = 0}, /* 83 MHz */
};

void __init spear6xx_clk_init(void __iomem *misc_base)
{
	struct clk *clk, *clk1;

	clk = clk_register_fixed_rate(NULL, "osc_32k_clk", NULL, 0, 32000);
	clk_register_clkdev(clk, "osc_32k_clk", NULL);

	clk = clk_register_fixed_rate(NULL, "osc_30m_clk", NULL, 0, 30000000);
	clk_register_clkdev(clk, "osc_30m_clk", NULL);

	/* clock derived from 32 KHz osc clk */
	clk = clk_register_gate(NULL, "rtc_spear", "osc_32k_clk", 0,
			PERIP1_CLK_ENB, RTC_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "rtc-spear");

	/* clock derived from 30 MHz osc clk */
	clk = clk_register_fixed_rate(NULL, "pll3_clk", "osc_24m_clk", 0,
			48000000);
	clk_register_clkdev(clk, "pll3_clk", NULL);

	clk = clk_register_vco_pll("vco1_clk", "pll1_clk", NULL, "osc_30m_clk",
			0, PLL1_CTR, PLL1_FRQ, pll_rtbl, ARRAY_SIZE(pll_rtbl),
			&_lock, &clk1, NULL);
	clk_register_clkdev(clk, "vco1_clk", NULL);
	clk_register_clkdev(clk1, "pll1_clk", NULL);

	clk = clk_register_vco_pll("vco2_clk", "pll2_clk", NULL, "osc_30m_clk",
			0, PLL2_CTR, PLL2_FRQ, pll_rtbl, ARRAY_SIZE(pll_rtbl),
			&_lock, &clk1, NULL);
	clk_register_clkdev(clk, "vco2_clk", NULL);
	clk_register_clkdev(clk1, "pll2_clk", NULL);

	clk = clk_register_fixed_factor(NULL, "wdt_clk", "osc_30m_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "wdt");

	/* clock derived from pll1 clk */
	clk = clk_register_fixed_factor(NULL, "cpu_clk", "pll1_clk",
			CLK_SET_RATE_PARENT, 1, 1);
	clk_register_clkdev(clk, "cpu_clk", NULL);

	clk = clk_register_divider(NULL, "ahb_clk", "pll1_clk",
			CLK_SET_RATE_PARENT, CORE_CLK_CFG, HCLK_RATIO_SHIFT,
			HCLK_RATIO_MASK, 0, &_lock);
	clk_register_clkdev(clk, "ahb_clk", NULL);

	clk = clk_register_aux("uart_syn_clk", "uart_syn_gclk", "pll1_clk", 0,
			UART_CLK_SYNT, NULL, aux_rtbl, ARRAY_SIZE(aux_rtbl),
			&_lock, &clk1);
	clk_register_clkdev(clk, "uart_syn_clk", NULL);
	clk_register_clkdev(clk1, "uart_syn_gclk", NULL);

	clk = clk_register_mux(NULL, "uart_mclk", uart_parents,
			ARRAY_SIZE(uart_parents), CLK_SET_RATE_NO_REPARENT,
			PERIP_CLK_CFG, UART_CLK_SHIFT, UART_CLK_MASK, 0,
			&_lock);
	clk_register_clkdev(clk, "uart_mclk", NULL);

	clk = clk_register_gate(NULL, "uart0", "uart_mclk", 0, PERIP1_CLK_ENB,
			UART0_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "d0000000.serial");

	clk = clk_register_gate(NULL, "uart1", "uart_mclk", 0, PERIP1_CLK_ENB,
			UART1_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "d0080000.serial");

	clk = clk_register_aux("firda_syn_clk", "firda_syn_gclk", "pll1_clk",
			0, FIRDA_CLK_SYNT, NULL, aux_rtbl, ARRAY_SIZE(aux_rtbl),
			&_lock, &clk1);
	clk_register_clkdev(clk, "firda_syn_clk", NULL);
	clk_register_clkdev(clk1, "firda_syn_gclk", NULL);

	clk = clk_register_mux(NULL, "firda_mclk", firda_parents,
			ARRAY_SIZE(firda_parents), CLK_SET_RATE_NO_REPARENT,
			PERIP_CLK_CFG, FIRDA_CLK_SHIFT, FIRDA_CLK_MASK, 0,
			&_lock);
	clk_register_clkdev(clk, "firda_mclk", NULL);

	clk = clk_register_gate(NULL, "firda_clk", "firda_mclk", 0,
			PERIP1_CLK_ENB, FIRDA_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "firda");

	clk = clk_register_aux("clcd_syn_clk", "clcd_syn_gclk", "pll1_clk",
			0, CLCD_CLK_SYNT, NULL, aux_rtbl, ARRAY_SIZE(aux_rtbl),
			&_lock, &clk1);
	clk_register_clkdev(clk, "clcd_syn_clk", NULL);
	clk_register_clkdev(clk1, "clcd_syn_gclk", NULL);

	clk = clk_register_mux(NULL, "clcd_mclk", clcd_parents,
			ARRAY_SIZE(clcd_parents), CLK_SET_RATE_NO_REPARENT,
			PERIP_CLK_CFG, CLCD_CLK_SHIFT, CLCD_CLK_MASK, 0,
			&_lock);
	clk_register_clkdev(clk, "clcd_mclk", NULL);

	clk = clk_register_gate(NULL, "clcd_clk", "clcd_mclk", 0,
			PERIP1_CLK_ENB, CLCD_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "clcd");

	/* gpt clocks */
	clk = clk_register_gpt("gpt0_1_syn_clk", "pll1_clk", 0, PRSC0_CLK_CFG,
			gpt_rtbl, ARRAY_SIZE(gpt_rtbl), &_lock);
	clk_register_clkdev(clk, "gpt0_1_syn_clk", NULL);

	clk = clk_register_mux(NULL, "gpt0_mclk", gpt0_1_parents,
			ARRAY_SIZE(gpt0_1_parents), CLK_SET_RATE_NO_REPARENT,
			PERIP_CLK_CFG, GPT0_CLK_SHIFT, GPT_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, NULL, "gpt0");

	clk = clk_register_mux(NULL, "gpt1_mclk", gpt0_1_parents,
			ARRAY_SIZE(gpt0_1_parents), CLK_SET_RATE_NO_REPARENT,
			PERIP_CLK_CFG, GPT1_CLK_SHIFT, GPT_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "gpt1_mclk", NULL);

	clk = clk_register_gate(NULL, "gpt1_clk", "gpt1_mclk", 0,
			PERIP1_CLK_ENB, GPT1_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "gpt1");

	clk = clk_register_gpt("gpt2_syn_clk", "pll1_clk", 0, PRSC1_CLK_CFG,
			gpt_rtbl, ARRAY_SIZE(gpt_rtbl), &_lock);
	clk_register_clkdev(clk, "gpt2_syn_clk", NULL);

	clk = clk_register_mux(NULL, "gpt2_mclk", gpt2_parents,
			ARRAY_SIZE(gpt2_parents), CLK_SET_RATE_NO_REPARENT,
			PERIP_CLK_CFG, GPT2_CLK_SHIFT, GPT_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "gpt2_mclk", NULL);

	clk = clk_register_gate(NULL, "gpt2_clk", "gpt2_mclk", 0,
			PERIP1_CLK_ENB, GPT2_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "gpt2");

	clk = clk_register_gpt("gpt3_syn_clk", "pll1_clk", 0, PRSC2_CLK_CFG,
			gpt_rtbl, ARRAY_SIZE(gpt_rtbl), &_lock);
	clk_register_clkdev(clk, "gpt3_syn_clk", NULL);

	clk = clk_register_mux(NULL, "gpt3_mclk", gpt3_parents,
			ARRAY_SIZE(gpt3_parents), CLK_SET_RATE_NO_REPARENT,
			PERIP_CLK_CFG, GPT3_CLK_SHIFT, GPT_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "gpt3_mclk", NULL);

	clk = clk_register_gate(NULL, "gpt3_clk", "gpt3_mclk", 0,
			PERIP1_CLK_ENB, GPT3_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "gpt3");

	/* clock derived from pll3 clk */
	clk = clk_register_gate(NULL, "usbh0_clk", "pll3_clk", 0,
			PERIP1_CLK_ENB, USBH0_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "e1800000.ehci");
	clk_register_clkdev(clk, NULL, "e1900000.ohci");

	clk = clk_register_gate(NULL, "usbh1_clk", "pll3_clk", 0,
			PERIP1_CLK_ENB, USBH1_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "e2000000.ehci");
	clk_register_clkdev(clk, NULL, "e2100000.ohci");

	clk = clk_register_gate(NULL, "usbd_clk", "pll3_clk", 0, PERIP1_CLK_ENB,
			USBD_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "designware_udc");

	/* clock derived from ahb clk */
	clk = clk_register_fixed_factor(NULL, "ahbmult2_clk", "ahb_clk", 0, 2,
			1);
	clk_register_clkdev(clk, "ahbmult2_clk", NULL);

	clk = clk_register_mux(NULL, "ddr_clk", ddr_parents,
			ARRAY_SIZE(ddr_parents), CLK_SET_RATE_NO_REPARENT,
			PLL_CLK_CFG, MCTR_CLK_SHIFT, MCTR_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "ddr_clk", NULL);

	clk = clk_register_divider(NULL, "apb_clk", "ahb_clk",
			CLK_SET_RATE_PARENT, CORE_CLK_CFG, PCLK_RATIO_SHIFT,
			PCLK_RATIO_MASK, 0, &_lock);
	clk_register_clkdev(clk, "apb_clk", NULL);

	clk = clk_register_gate(NULL, "dma_clk", "ahb_clk", 0, PERIP1_CLK_ENB,
			DMA_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "fc400000.dma");

	clk = clk_register_gate(NULL, "fsmc_clk", "ahb_clk", 0, PERIP1_CLK_ENB,
			FSMC_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "d1800000.flash");

	clk = clk_register_gate(NULL, "gmac_clk", "ahb_clk", 0, PERIP1_CLK_ENB,
			GMAC_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "e0800000.ethernet");

	clk = clk_register_gate(NULL, "i2c_clk", "ahb_clk", 0, PERIP1_CLK_ENB,
			I2C_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "d0200000.i2c");

	clk = clk_register_gate(NULL, "jpeg_clk", "ahb_clk", 0, PERIP1_CLK_ENB,
			JPEG_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "jpeg");

	clk = clk_register_gate(NULL, "smi_clk", "ahb_clk", 0, PERIP1_CLK_ENB,
			SMI_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "fc000000.flash");

	/* clock derived from apb clk */
	clk = clk_register_gate(NULL, "adc_clk", "apb_clk", 0, PERIP1_CLK_ENB,
			ADC_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "adc");

	clk = clk_register_fixed_factor(NULL, "gpio0_clk", "apb_clk", 0, 1, 1);
	clk_register_clkdev(clk, NULL, "f0100000.gpio");

	clk = clk_register_gate(NULL, "gpio1_clk", "apb_clk", 0, PERIP1_CLK_ENB,
			GPIO1_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "fc980000.gpio");

	clk = clk_register_gate(NULL, "gpio2_clk", "apb_clk", 0, PERIP1_CLK_ENB,
			GPIO2_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "d8100000.gpio");

	clk = clk_register_gate(NULL, "ssp0_clk", "apb_clk", 0, PERIP1_CLK_ENB,
			SSP0_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "ssp-pl022.0");

	clk = clk_register_gate(NULL, "ssp1_clk", "apb_clk", 0, PERIP1_CLK_ENB,
			SSP1_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "ssp-pl022.1");

	clk = clk_register_gate(NULL, "ssp2_clk", "apb_clk", 0, PERIP1_CLK_ENB,
			SSP2_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "ssp-pl022.2");
}
