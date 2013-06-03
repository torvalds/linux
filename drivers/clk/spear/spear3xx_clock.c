/*
 * SPEAr3xx machines clock framework source file
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of_platform.h>
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
	#define GEN_SYNTH2_3_CLK_SHIFT	18
	#define GEN_SYNTH2_3_CLK_MASK	1

	#define HCLK_RATIO_SHIFT	10
	#define HCLK_RATIO_MASK		2
	#define PCLK_RATIO_SHIFT	8
	#define PCLK_RATIO_MASK		2

#define PERIP_CLK_CFG			(misc_base + 0x028)
	/* PERIP_CLK_CFG register masks */
	#define UART_CLK_SHIFT		4
	#define UART_CLK_MASK		1
	#define FIRDA_CLK_SHIFT		5
	#define FIRDA_CLK_MASK		2
	#define GPT0_CLK_SHIFT		8
	#define GPT1_CLK_SHIFT		11
	#define GPT2_CLK_SHIFT		12
	#define GPT_CLK_MASK		1

#define PERIP1_CLK_ENB			(misc_base + 0x02C)
	/* PERIP1_CLK_ENB register masks */
	#define UART_CLK_ENB		3
	#define SSP_CLK_ENB		5
	#define I2C_CLK_ENB		7
	#define JPEG_CLK_ENB		8
	#define FIRDA_CLK_ENB		10
	#define GPT1_CLK_ENB		11
	#define GPT2_CLK_ENB		12
	#define ADC_CLK_ENB		15
	#define RTC_CLK_ENB		17
	#define GPIO_CLK_ENB		18
	#define DMA_CLK_ENB		19
	#define SMI_CLK_ENB		21
	#define GMAC_CLK_ENB		23
	#define USBD_CLK_ENB		24
	#define USBH_CLK_ENB		25
	#define C3_CLK_ENB		31

#define RAS_CLK_ENB			(misc_base + 0x034)
	#define RAS_AHB_CLK_ENB		0
	#define RAS_PLL1_CLK_ENB	1
	#define RAS_APB_CLK_ENB		2
	#define RAS_32K_CLK_ENB		3
	#define RAS_24M_CLK_ENB		4
	#define RAS_48M_CLK_ENB		5
	#define RAS_PLL2_CLK_ENB	7
	#define RAS_SYNT0_CLK_ENB	8
	#define RAS_SYNT1_CLK_ENB	9
	#define RAS_SYNT2_CLK_ENB	10
	#define RAS_SYNT3_CLK_ENB	11

#define PRSC0_CLK_CFG			(misc_base + 0x044)
#define PRSC1_CLK_CFG			(misc_base + 0x048)
#define PRSC2_CLK_CFG			(misc_base + 0x04C)
#define AMEM_CLK_CFG			(misc_base + 0x050)
	#define AMEM_CLK_ENB		0

#define CLCD_CLK_SYNT			(misc_base + 0x05C)
#define FIRDA_CLK_SYNT			(misc_base + 0x060)
#define UART_CLK_SYNT			(misc_base + 0x064)
#define GMAC_CLK_SYNT			(misc_base + 0x068)
#define GEN0_CLK_SYNT			(misc_base + 0x06C)
#define GEN1_CLK_SYNT			(misc_base + 0x070)
#define GEN2_CLK_SYNT			(misc_base + 0x074)
#define GEN3_CLK_SYNT			(misc_base + 0x078)

/* pll rate configuration table, in ascending order of rates */
static struct pll_rate_tbl pll_rtbl[] = {
	{.mode = 0, .m = 0x53, .n = 0x0C, .p = 0x1}, /* vco 332 & pll 166 MHz */
	{.mode = 0, .m = 0x85, .n = 0x0C, .p = 0x1}, /* vco 532 & pll 266 MHz */
	{.mode = 0, .m = 0xA6, .n = 0x0C, .p = 0x1}, /* vco 664 & pll 332 MHz */
};

/* aux rate configuration table, in ascending order of rates */
static struct aux_rate_tbl aux_rtbl[] = {
	/* For PLL1 = 332 MHz */
	{.xscale = 1, .yscale = 81, .eq = 0}, /* 2.049 MHz */
	{.xscale = 1, .yscale = 59, .eq = 0}, /* 2.822 MHz */
	{.xscale = 2, .yscale = 81, .eq = 0}, /* 4.098 MHz */
	{.xscale = 3, .yscale = 89, .eq = 0}, /* 5.644 MHz */
	{.xscale = 4, .yscale = 81, .eq = 0}, /* 8.197 MHz */
	{.xscale = 4, .yscale = 59, .eq = 0}, /* 11.254 MHz */
	{.xscale = 2, .yscale = 27, .eq = 0}, /* 12.296 MHz */
	{.xscale = 2, .yscale = 8, .eq = 0}, /* 41.5 MHz */
	{.xscale = 2, .yscale = 4, .eq = 0}, /* 83 MHz */
	{.xscale = 1, .yscale = 2, .eq = 1}, /* 166 MHz */
};

/* gpt rate configuration table, in ascending order of rates */
static struct gpt_rate_tbl gpt_rtbl[] = {
	/* For pll1 = 332 MHz */
	{.mscale = 4, .nscale = 0}, /* 41.5 MHz */
	{.mscale = 2, .nscale = 0}, /* 55.3 MHz */
	{.mscale = 1, .nscale = 0}, /* 83 MHz */
};

/* clock parents */
static const char *uart0_parents[] = { "pll3_clk", "uart_syn_gclk", };
static const char *firda_parents[] = { "pll3_clk", "firda_syn_gclk",
};
static const char *gpt0_parents[] = { "pll3_clk", "gpt0_syn_clk", };
static const char *gpt1_parents[] = { "pll3_clk", "gpt1_syn_clk", };
static const char *gpt2_parents[] = { "pll3_clk", "gpt2_syn_clk", };
static const char *gen2_3_parents[] = { "pll1_clk", "pll2_clk", };
static const char *ddr_parents[] = { "ahb_clk", "ahbmult2_clk", "none",
	"pll2_clk", };

#ifdef CONFIG_MACH_SPEAR300
static void __init spear300_clk_init(void)
{
	struct clk *clk;

	clk = clk_register_fixed_factor(NULL, "clcd_clk", "ras_pll3_clk", 0,
			1, 1);
	clk_register_clkdev(clk, NULL, "60000000.clcd");

	clk = clk_register_fixed_factor(NULL, "fsmc_clk", "ras_ahb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "94000000.flash");

	clk = clk_register_fixed_factor(NULL, "sdhci_clk", "ras_ahb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "70000000.sdhci");

	clk = clk_register_fixed_factor(NULL, "gpio1_clk", "ras_apb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "a9000000.gpio");

	clk = clk_register_fixed_factor(NULL, "kbd_clk", "ras_apb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "a0000000.kbd");
}
#else
static inline void spear300_clk_init(void) { }
#endif

/* array of all spear 310 clock lookups */
#ifdef CONFIG_MACH_SPEAR310
static void __init spear310_clk_init(void)
{
	struct clk *clk;

	clk = clk_register_fixed_factor(NULL, "emi_clk", "ras_ahb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, "emi", NULL);

	clk = clk_register_fixed_factor(NULL, "fsmc_clk", "ras_ahb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "44000000.flash");

	clk = clk_register_fixed_factor(NULL, "tdm_clk", "ras_ahb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "tdm");

	clk = clk_register_fixed_factor(NULL, "uart1_clk", "ras_apb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "b2000000.serial");

	clk = clk_register_fixed_factor(NULL, "uart2_clk", "ras_apb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "b2080000.serial");

	clk = clk_register_fixed_factor(NULL, "uart3_clk", "ras_apb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "b2100000.serial");

	clk = clk_register_fixed_factor(NULL, "uart4_clk", "ras_apb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "b2180000.serial");

	clk = clk_register_fixed_factor(NULL, "uart5_clk", "ras_apb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "b2200000.serial");
}
#else
static inline void spear310_clk_init(void) { }
#endif

/* array of all spear 320 clock lookups */
#ifdef CONFIG_MACH_SPEAR320

#define SPEAR320_CONTROL_REG		(soc_config_base + 0x0000)
#define SPEAR320_EXT_CTRL_REG		(soc_config_base + 0x0018)

	#define SPEAR320_UARTX_PCLK_MASK		0x1
	#define SPEAR320_UART2_PCLK_SHIFT		8
	#define SPEAR320_UART3_PCLK_SHIFT		9
	#define SPEAR320_UART4_PCLK_SHIFT		10
	#define SPEAR320_UART5_PCLK_SHIFT		11
	#define SPEAR320_UART6_PCLK_SHIFT		12
	#define SPEAR320_RS485_PCLK_SHIFT		13
	#define SMII_PCLK_SHIFT				18
	#define SMII_PCLK_MASK				2
	#define SMII_PCLK_VAL_PAD			0x0
	#define SMII_PCLK_VAL_PLL2			0x1
	#define SMII_PCLK_VAL_SYNTH0			0x2
	#define SDHCI_PCLK_SHIFT			15
	#define SDHCI_PCLK_MASK				1
	#define SDHCI_PCLK_VAL_48M			0x0
	#define SDHCI_PCLK_VAL_SYNTH3			0x1
	#define I2S_REF_PCLK_SHIFT			8
	#define I2S_REF_PCLK_MASK			1
	#define I2S_REF_PCLK_SYNTH_VAL			0x1
	#define I2S_REF_PCLK_PLL2_VAL			0x0
	#define UART1_PCLK_SHIFT			6
	#define UART1_PCLK_MASK				1
	#define SPEAR320_UARTX_PCLK_VAL_SYNTH1		0x0
	#define SPEAR320_UARTX_PCLK_VAL_APB		0x1

static const char *i2s_ref_parents[] = { "ras_pll2_clk", "ras_syn2_gclk", };
static const char *sdhci_parents[] = { "ras_pll3_clk", "ras_syn3_gclk", };
static const char *smii0_parents[] = { "smii_125m_pad", "ras_pll2_clk",
	"ras_syn0_gclk", };
static const char *uartx_parents[] = { "ras_syn1_gclk", "ras_apb_clk", };

static void __init spear320_clk_init(void __iomem *soc_config_base)
{
	struct clk *clk;

	clk = clk_register_fixed_rate(NULL, "smii_125m_pad_clk", NULL,
			CLK_IS_ROOT, 125000000);
	clk_register_clkdev(clk, "smii_125m_pad", NULL);

	clk = clk_register_fixed_factor(NULL, "clcd_clk", "ras_pll3_clk", 0,
			1, 1);
	clk_register_clkdev(clk, NULL, "90000000.clcd");

	clk = clk_register_fixed_factor(NULL, "emi_clk", "ras_ahb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, "emi", NULL);

	clk = clk_register_fixed_factor(NULL, "fsmc_clk", "ras_ahb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "4c000000.flash");

	clk = clk_register_fixed_factor(NULL, "i2c1_clk", "ras_ahb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "a7000000.i2c");

	clk = clk_register_fixed_factor(NULL, "pwm_clk", "ras_ahb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "a8000000.pwm");

	clk = clk_register_fixed_factor(NULL, "ssp1_clk", "ras_ahb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "a5000000.spi");

	clk = clk_register_fixed_factor(NULL, "ssp2_clk", "ras_ahb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "a6000000.spi");

	clk = clk_register_fixed_factor(NULL, "can0_clk", "ras_apb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "c_can_platform.0");

	clk = clk_register_fixed_factor(NULL, "can1_clk", "ras_apb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "c_can_platform.1");

	clk = clk_register_fixed_factor(NULL, "i2s_clk", "ras_apb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "a9400000.i2s");

	clk = clk_register_mux(NULL, "i2s_ref_clk", i2s_ref_parents,
			ARRAY_SIZE(i2s_ref_parents), CLK_SET_RATE_PARENT,
			SPEAR320_CONTROL_REG, I2S_REF_PCLK_SHIFT,
			I2S_REF_PCLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "i2s_ref_clk", NULL);

	clk = clk_register_fixed_factor(NULL, "i2s_sclk", "i2s_ref_clk",
			CLK_SET_RATE_PARENT, 1,
			4);
	clk_register_clkdev(clk, "i2s_sclk", NULL);

	clk = clk_register_fixed_factor(NULL, "macb1_clk", "ras_apb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, "hclk", "aa000000.eth");

	clk = clk_register_fixed_factor(NULL, "macb2_clk", "ras_apb_clk", 0, 1,
			1);
	clk_register_clkdev(clk, "hclk", "ab000000.eth");

	clk = clk_register_mux(NULL, "rs485_clk", uartx_parents,
			ARRAY_SIZE(uartx_parents), CLK_SET_RATE_PARENT,
			SPEAR320_EXT_CTRL_REG, SPEAR320_RS485_PCLK_SHIFT,
			SPEAR320_UARTX_PCLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, NULL, "a9300000.serial");

	clk = clk_register_mux(NULL, "sdhci_clk", sdhci_parents,
			ARRAY_SIZE(sdhci_parents), CLK_SET_RATE_PARENT,
			SPEAR320_CONTROL_REG, SDHCI_PCLK_SHIFT, SDHCI_PCLK_MASK,
			0, &_lock);
	clk_register_clkdev(clk, NULL, "70000000.sdhci");

	clk = clk_register_mux(NULL, "smii_pclk", smii0_parents,
			ARRAY_SIZE(smii0_parents), 0, SPEAR320_CONTROL_REG,
			SMII_PCLK_SHIFT, SMII_PCLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, NULL, "smii_pclk");

	clk = clk_register_fixed_factor(NULL, "smii_clk", "smii_pclk", 0, 1, 1);
	clk_register_clkdev(clk, NULL, "smii");

	clk = clk_register_mux(NULL, "uart1_clk", uartx_parents,
			ARRAY_SIZE(uartx_parents), CLK_SET_RATE_PARENT,
			SPEAR320_CONTROL_REG, UART1_PCLK_SHIFT, UART1_PCLK_MASK,
			0, &_lock);
	clk_register_clkdev(clk, NULL, "a3000000.serial");

	clk = clk_register_mux(NULL, "uart2_clk", uartx_parents,
			ARRAY_SIZE(uartx_parents), CLK_SET_RATE_PARENT,
			SPEAR320_EXT_CTRL_REG, SPEAR320_UART2_PCLK_SHIFT,
			SPEAR320_UARTX_PCLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, NULL, "a4000000.serial");

	clk = clk_register_mux(NULL, "uart3_clk", uartx_parents,
			ARRAY_SIZE(uartx_parents), CLK_SET_RATE_PARENT,
			SPEAR320_EXT_CTRL_REG, SPEAR320_UART3_PCLK_SHIFT,
			SPEAR320_UARTX_PCLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, NULL, "a9100000.serial");

	clk = clk_register_mux(NULL, "uart4_clk", uartx_parents,
			ARRAY_SIZE(uartx_parents), CLK_SET_RATE_PARENT,
			SPEAR320_EXT_CTRL_REG, SPEAR320_UART4_PCLK_SHIFT,
			SPEAR320_UARTX_PCLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, NULL, "a9200000.serial");

	clk = clk_register_mux(NULL, "uart5_clk", uartx_parents,
			ARRAY_SIZE(uartx_parents), CLK_SET_RATE_PARENT,
			SPEAR320_EXT_CTRL_REG, SPEAR320_UART5_PCLK_SHIFT,
			SPEAR320_UARTX_PCLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, NULL, "60000000.serial");

	clk = clk_register_mux(NULL, "uart6_clk", uartx_parents,
			ARRAY_SIZE(uartx_parents), CLK_SET_RATE_PARENT,
			SPEAR320_EXT_CTRL_REG, SPEAR320_UART6_PCLK_SHIFT,
			SPEAR320_UARTX_PCLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, NULL, "60100000.serial");
}
#else
static inline void spear320_clk_init(void __iomem *soc_config_base) { }
#endif

void __init spear3xx_clk_init(void __iomem *misc_base, void __iomem *soc_config_base)
{
	struct clk *clk, *clk1;

	clk = clk_register_fixed_rate(NULL, "osc_32k_clk", NULL, CLK_IS_ROOT,
			32000);
	clk_register_clkdev(clk, "osc_32k_clk", NULL);

	clk = clk_register_fixed_rate(NULL, "osc_24m_clk", NULL, CLK_IS_ROOT,
			24000000);
	clk_register_clkdev(clk, "osc_24m_clk", NULL);

	/* clock derived from 32 KHz osc clk */
	clk = clk_register_gate(NULL, "rtc-spear", "osc_32k_clk", 0,
			PERIP1_CLK_ENB, RTC_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "fc900000.rtc");

	/* clock derived from 24 MHz osc clk */
	clk = clk_register_fixed_rate(NULL, "pll3_clk", "osc_24m_clk", 0,
			48000000);
	clk_register_clkdev(clk, "pll3_clk", NULL);

	clk = clk_register_fixed_factor(NULL, "wdt_clk", "osc_24m_clk", 0, 1,
			1);
	clk_register_clkdev(clk, NULL, "fc880000.wdt");

	clk = clk_register_vco_pll("vco1_clk", "pll1_clk", NULL,
			"osc_24m_clk", 0, PLL1_CTR, PLL1_FRQ, pll_rtbl,
			ARRAY_SIZE(pll_rtbl), &_lock, &clk1, NULL);
	clk_register_clkdev(clk, "vco1_clk", NULL);
	clk_register_clkdev(clk1, "pll1_clk", NULL);

	clk = clk_register_vco_pll("vco2_clk", "pll2_clk", NULL,
			"osc_24m_clk", 0, PLL2_CTR, PLL2_FRQ, pll_rtbl,
			ARRAY_SIZE(pll_rtbl), &_lock, &clk1, NULL);
	clk_register_clkdev(clk, "vco2_clk", NULL);
	clk_register_clkdev(clk1, "pll2_clk", NULL);

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

	clk = clk_register_mux(NULL, "uart0_mclk", uart0_parents,
			ARRAY_SIZE(uart0_parents), CLK_SET_RATE_PARENT,
			PERIP_CLK_CFG, UART_CLK_SHIFT, UART_CLK_MASK, 0,
			&_lock);
	clk_register_clkdev(clk, "uart0_mclk", NULL);

	clk = clk_register_gate(NULL, "uart0", "uart0_mclk",
			CLK_SET_RATE_PARENT, PERIP1_CLK_ENB, UART_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "d0000000.serial");

	clk = clk_register_aux("firda_syn_clk", "firda_syn_gclk", "pll1_clk", 0,
			FIRDA_CLK_SYNT, NULL, aux_rtbl, ARRAY_SIZE(aux_rtbl),
			&_lock, &clk1);
	clk_register_clkdev(clk, "firda_syn_clk", NULL);
	clk_register_clkdev(clk1, "firda_syn_gclk", NULL);

	clk = clk_register_mux(NULL, "firda_mclk", firda_parents,
			ARRAY_SIZE(firda_parents), CLK_SET_RATE_PARENT,
			PERIP_CLK_CFG, FIRDA_CLK_SHIFT, FIRDA_CLK_MASK, 0,
			&_lock);
	clk_register_clkdev(clk, "firda_mclk", NULL);

	clk = clk_register_gate(NULL, "firda_clk", "firda_mclk",
			CLK_SET_RATE_PARENT, PERIP1_CLK_ENB, FIRDA_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "firda");

	/* gpt clocks */
	clk_register_gpt("gpt0_syn_clk", "pll1_clk", 0, PRSC0_CLK_CFG, gpt_rtbl,
			ARRAY_SIZE(gpt_rtbl), &_lock);
	clk = clk_register_mux(NULL, "gpt0_clk", gpt0_parents,
			ARRAY_SIZE(gpt0_parents), CLK_SET_RATE_PARENT,
			PERIP_CLK_CFG, GPT0_CLK_SHIFT, GPT_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, NULL, "gpt0");

	clk_register_gpt("gpt1_syn_clk", "pll1_clk", 0, PRSC1_CLK_CFG, gpt_rtbl,
			ARRAY_SIZE(gpt_rtbl), &_lock);
	clk = clk_register_mux(NULL, "gpt1_mclk", gpt1_parents,
			ARRAY_SIZE(gpt1_parents), CLK_SET_RATE_PARENT,
			PERIP_CLK_CFG, GPT1_CLK_SHIFT, GPT_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "gpt1_mclk", NULL);
	clk = clk_register_gate(NULL, "gpt1_clk", "gpt1_mclk",
			CLK_SET_RATE_PARENT, PERIP1_CLK_ENB, GPT1_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "gpt1");

	clk_register_gpt("gpt2_syn_clk", "pll1_clk", 0, PRSC2_CLK_CFG, gpt_rtbl,
			ARRAY_SIZE(gpt_rtbl), &_lock);
	clk = clk_register_mux(NULL, "gpt2_mclk", gpt2_parents,
			ARRAY_SIZE(gpt2_parents), CLK_SET_RATE_PARENT,
			PERIP_CLK_CFG, GPT2_CLK_SHIFT, GPT_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "gpt2_mclk", NULL);
	clk = clk_register_gate(NULL, "gpt2_clk", "gpt2_mclk",
			CLK_SET_RATE_PARENT, PERIP1_CLK_ENB, GPT2_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "gpt2");

	/* general synths clocks */
	clk = clk_register_aux("gen0_syn_clk", "gen0_syn_gclk", "pll1_clk",
			0, GEN0_CLK_SYNT, NULL, aux_rtbl, ARRAY_SIZE(aux_rtbl),
			&_lock, &clk1);
	clk_register_clkdev(clk, "gen0_syn_clk", NULL);
	clk_register_clkdev(clk1, "gen0_syn_gclk", NULL);

	clk = clk_register_aux("gen1_syn_clk", "gen1_syn_gclk", "pll1_clk",
			0, GEN1_CLK_SYNT, NULL, aux_rtbl, ARRAY_SIZE(aux_rtbl),
			&_lock, &clk1);
	clk_register_clkdev(clk, "gen1_syn_clk", NULL);
	clk_register_clkdev(clk1, "gen1_syn_gclk", NULL);

	clk = clk_register_mux(NULL, "gen2_3_par_clk", gen2_3_parents,
			ARRAY_SIZE(gen2_3_parents), 0, CORE_CLK_CFG,
			GEN_SYNTH2_3_CLK_SHIFT, GEN_SYNTH2_3_CLK_MASK, 0,
			&_lock);
	clk_register_clkdev(clk, "gen2_3_par_clk", NULL);

	clk = clk_register_aux("gen2_syn_clk", "gen2_syn_gclk",
			"gen2_3_par_clk", 0, GEN2_CLK_SYNT, NULL, aux_rtbl,
			ARRAY_SIZE(aux_rtbl), &_lock, &clk1);
	clk_register_clkdev(clk, "gen2_syn_clk", NULL);
	clk_register_clkdev(clk1, "gen2_syn_gclk", NULL);

	clk = clk_register_aux("gen3_syn_clk", "gen3_syn_gclk",
			"gen2_3_par_clk", 0, GEN3_CLK_SYNT, NULL, aux_rtbl,
			ARRAY_SIZE(aux_rtbl), &_lock, &clk1);
	clk_register_clkdev(clk, "gen3_syn_clk", NULL);
	clk_register_clkdev(clk1, "gen3_syn_gclk", NULL);

	/* clock derived from pll3 clk */
	clk = clk_register_gate(NULL, "usbh_clk", "pll3_clk", 0, PERIP1_CLK_ENB,
			USBH_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "e1800000.ehci");
	clk_register_clkdev(clk, NULL, "e1900000.ohci");
	clk_register_clkdev(clk, NULL, "e2100000.ohci");

	clk = clk_register_fixed_factor(NULL, "usbh.0_clk", "usbh_clk", 0, 1,
			1);
	clk_register_clkdev(clk, "usbh.0_clk", NULL);

	clk = clk_register_fixed_factor(NULL, "usbh.1_clk", "usbh_clk", 0, 1,
			1);
	clk_register_clkdev(clk, "usbh.1_clk", NULL);

	clk = clk_register_gate(NULL, "usbd_clk", "pll3_clk", 0, PERIP1_CLK_ENB,
			USBD_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "e1100000.usbd");

	/* clock derived from ahb clk */
	clk = clk_register_fixed_factor(NULL, "ahbmult2_clk", "ahb_clk", 0, 2,
			1);
	clk_register_clkdev(clk, "ahbmult2_clk", NULL);

	clk = clk_register_mux(NULL, "ddr_clk", ddr_parents,
			ARRAY_SIZE(ddr_parents), 0, PLL_CLK_CFG, MCTR_CLK_SHIFT,
			MCTR_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "ddr_clk", NULL);

	clk = clk_register_divider(NULL, "apb_clk", "ahb_clk",
			CLK_SET_RATE_PARENT, CORE_CLK_CFG, PCLK_RATIO_SHIFT,
			PCLK_RATIO_MASK, 0, &_lock);
	clk_register_clkdev(clk, "apb_clk", NULL);

	clk = clk_register_gate(NULL, "amem_clk", "ahb_clk", 0, AMEM_CLK_CFG,
			AMEM_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, "amem_clk", NULL);

	clk = clk_register_gate(NULL, "c3_clk", "ahb_clk", 0, PERIP1_CLK_ENB,
			C3_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "c3_clk");

	clk = clk_register_gate(NULL, "dma_clk", "ahb_clk", 0, PERIP1_CLK_ENB,
			DMA_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "fc400000.dma");

	clk = clk_register_gate(NULL, "gmac_clk", "ahb_clk", 0, PERIP1_CLK_ENB,
			GMAC_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "e0800000.eth");

	clk = clk_register_gate(NULL, "i2c0_clk", "ahb_clk", 0, PERIP1_CLK_ENB,
			I2C_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "d0180000.i2c");

	clk = clk_register_gate(NULL, "jpeg_clk", "ahb_clk", 0, PERIP1_CLK_ENB,
			JPEG_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "jpeg");

	clk = clk_register_gate(NULL, "smi_clk", "ahb_clk", 0, PERIP1_CLK_ENB,
			SMI_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "fc000000.flash");

	/* clock derived from apb clk */
	clk = clk_register_gate(NULL, "adc_clk", "apb_clk", 0, PERIP1_CLK_ENB,
			ADC_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "d0080000.adc");

	clk = clk_register_gate(NULL, "gpio0_clk", "apb_clk", 0, PERIP1_CLK_ENB,
			GPIO_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "fc980000.gpio");

	clk = clk_register_gate(NULL, "ssp0_clk", "apb_clk", 0, PERIP1_CLK_ENB,
			SSP_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "d0100000.spi");

	/* RAS clk enable */
	clk = clk_register_gate(NULL, "ras_ahb_clk", "ahb_clk", 0, RAS_CLK_ENB,
			RAS_AHB_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, "ras_ahb_clk", NULL);

	clk = clk_register_gate(NULL, "ras_apb_clk", "apb_clk", 0, RAS_CLK_ENB,
			RAS_APB_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, "ras_apb_clk", NULL);

	clk = clk_register_gate(NULL, "ras_32k_clk", "osc_32k_clk", 0,
			RAS_CLK_ENB, RAS_32K_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, "ras_32k_clk", NULL);

	clk = clk_register_gate(NULL, "ras_24m_clk", "osc_24m_clk", 0,
			RAS_CLK_ENB, RAS_24M_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, "ras_24m_clk", NULL);

	clk = clk_register_gate(NULL, "ras_pll1_clk", "pll1_clk", 0,
			RAS_CLK_ENB, RAS_PLL1_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, "ras_pll1_clk", NULL);

	clk = clk_register_gate(NULL, "ras_pll2_clk", "pll2_clk", 0,
			RAS_CLK_ENB, RAS_PLL2_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, "ras_pll2_clk", NULL);

	clk = clk_register_gate(NULL, "ras_pll3_clk", "pll3_clk", 0,
			RAS_CLK_ENB, RAS_48M_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, "ras_pll3_clk", NULL);

	clk = clk_register_gate(NULL, "ras_syn0_gclk", "gen0_syn_gclk",
			CLK_SET_RATE_PARENT, RAS_CLK_ENB, RAS_SYNT0_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, "ras_syn0_gclk", NULL);

	clk = clk_register_gate(NULL, "ras_syn1_gclk", "gen1_syn_gclk",
			CLK_SET_RATE_PARENT, RAS_CLK_ENB, RAS_SYNT1_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, "ras_syn1_gclk", NULL);

	clk = clk_register_gate(NULL, "ras_syn2_gclk", "gen2_syn_gclk",
			CLK_SET_RATE_PARENT, RAS_CLK_ENB, RAS_SYNT2_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, "ras_syn2_gclk", NULL);

	clk = clk_register_gate(NULL, "ras_syn3_gclk", "gen3_syn_gclk",
			CLK_SET_RATE_PARENT, RAS_CLK_ENB, RAS_SYNT3_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, "ras_syn3_gclk", NULL);

	if (of_machine_is_compatible("st,spear300"))
		spear300_clk_init();
	else if (of_machine_is_compatible("st,spear310"))
		spear310_clk_init();
	else if (of_machine_is_compatible("st,spear320"))
		spear320_clk_init(soc_config_base);
}
