/*
 * arch/arm/mach-spear13xx/spear1340_clock.c
 *
 * SPEAr1340 machine clock framework source file
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <vireshk@kernel.org>
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

/* Clock Configuration Registers */
#define SPEAR1340_SYS_CLK_CTRL			(misc_base + 0x200)
	#define SPEAR1340_HCLK_SRC_SEL_SHIFT	27
	#define SPEAR1340_HCLK_SRC_SEL_MASK	1
	#define SPEAR1340_SCLK_SRC_SEL_SHIFT	23
	#define SPEAR1340_SCLK_SRC_SEL_MASK	3

/* PLL related registers and bit values */
#define SPEAR1340_PLL_CFG			(misc_base + 0x210)
	/* PLL_CFG bit values */
	#define SPEAR1340_CLCD_SYNT_CLK_MASK		1
	#define SPEAR1340_CLCD_SYNT_CLK_SHIFT		31
	#define SPEAR1340_GEN_SYNT2_3_CLK_SHIFT		29
	#define SPEAR1340_GEN_SYNT_CLK_MASK		2
	#define SPEAR1340_GEN_SYNT0_1_CLK_SHIFT		27
	#define SPEAR1340_PLL_CLK_MASK			2
	#define SPEAR1340_PLL3_CLK_SHIFT		24
	#define SPEAR1340_PLL2_CLK_SHIFT		22
	#define SPEAR1340_PLL1_CLK_SHIFT		20

#define SPEAR1340_PLL1_CTR			(misc_base + 0x214)
#define SPEAR1340_PLL1_FRQ			(misc_base + 0x218)
#define SPEAR1340_PLL2_CTR			(misc_base + 0x220)
#define SPEAR1340_PLL2_FRQ			(misc_base + 0x224)
#define SPEAR1340_PLL3_CTR			(misc_base + 0x22C)
#define SPEAR1340_PLL3_FRQ			(misc_base + 0x230)
#define SPEAR1340_PLL4_CTR			(misc_base + 0x238)
#define SPEAR1340_PLL4_FRQ			(misc_base + 0x23C)
#define SPEAR1340_PERIP_CLK_CFG			(misc_base + 0x244)
	/* PERIP_CLK_CFG bit values */
	#define SPEAR1340_SPDIF_CLK_MASK		1
	#define SPEAR1340_SPDIF_OUT_CLK_SHIFT		15
	#define SPEAR1340_SPDIF_IN_CLK_SHIFT		14
	#define SPEAR1340_GPT3_CLK_SHIFT		13
	#define SPEAR1340_GPT2_CLK_SHIFT		12
	#define SPEAR1340_GPT_CLK_MASK			1
	#define SPEAR1340_GPT1_CLK_SHIFT		9
	#define SPEAR1340_GPT0_CLK_SHIFT		8
	#define SPEAR1340_UART_CLK_MASK			2
	#define SPEAR1340_UART1_CLK_SHIFT		6
	#define SPEAR1340_UART0_CLK_SHIFT		4
	#define SPEAR1340_CLCD_CLK_MASK			2
	#define SPEAR1340_CLCD_CLK_SHIFT		2
	#define SPEAR1340_C3_CLK_MASK			1
	#define SPEAR1340_C3_CLK_SHIFT			1

#define SPEAR1340_GMAC_CLK_CFG			(misc_base + 0x248)
	#define SPEAR1340_GMAC_PHY_CLK_MASK		1
	#define SPEAR1340_GMAC_PHY_CLK_SHIFT		2
	#define SPEAR1340_GMAC_PHY_INPUT_CLK_MASK	2
	#define SPEAR1340_GMAC_PHY_INPUT_CLK_SHIFT	0

#define SPEAR1340_I2S_CLK_CFG			(misc_base + 0x24C)
	/* I2S_CLK_CFG register mask */
	#define SPEAR1340_I2S_SCLK_X_MASK		0x1F
	#define SPEAR1340_I2S_SCLK_X_SHIFT		27
	#define SPEAR1340_I2S_SCLK_Y_MASK		0x1F
	#define SPEAR1340_I2S_SCLK_Y_SHIFT		22
	#define SPEAR1340_I2S_SCLK_EQ_SEL_SHIFT		21
	#define SPEAR1340_I2S_SCLK_SYNTH_ENB		20
	#define SPEAR1340_I2S_PRS1_CLK_X_MASK		0xFF
	#define SPEAR1340_I2S_PRS1_CLK_X_SHIFT		12
	#define SPEAR1340_I2S_PRS1_CLK_Y_MASK		0xFF
	#define SPEAR1340_I2S_PRS1_CLK_Y_SHIFT		4
	#define SPEAR1340_I2S_PRS1_EQ_SEL_SHIFT		3
	#define SPEAR1340_I2S_REF_SEL_MASK		1
	#define SPEAR1340_I2S_REF_SHIFT			2
	#define SPEAR1340_I2S_SRC_CLK_MASK		2
	#define SPEAR1340_I2S_SRC_CLK_SHIFT		0

#define SPEAR1340_C3_CLK_SYNT			(misc_base + 0x250)
#define SPEAR1340_UART0_CLK_SYNT		(misc_base + 0x254)
#define SPEAR1340_UART1_CLK_SYNT		(misc_base + 0x258)
#define SPEAR1340_GMAC_CLK_SYNT			(misc_base + 0x25C)
#define SPEAR1340_SDHCI_CLK_SYNT		(misc_base + 0x260)
#define SPEAR1340_CFXD_CLK_SYNT			(misc_base + 0x264)
#define SPEAR1340_ADC_CLK_SYNT			(misc_base + 0x270)
#define SPEAR1340_AMBA_CLK_SYNT			(misc_base + 0x274)
#define SPEAR1340_CLCD_CLK_SYNT			(misc_base + 0x27C)
#define SPEAR1340_SYS_CLK_SYNT			(misc_base + 0x284)
#define SPEAR1340_GEN_CLK_SYNT0			(misc_base + 0x28C)
#define SPEAR1340_GEN_CLK_SYNT1			(misc_base + 0x294)
#define SPEAR1340_GEN_CLK_SYNT2			(misc_base + 0x29C)
#define SPEAR1340_GEN_CLK_SYNT3			(misc_base + 0x304)
#define SPEAR1340_PERIP1_CLK_ENB		(misc_base + 0x30C)
	#define SPEAR1340_RTC_CLK_ENB			31
	#define SPEAR1340_ADC_CLK_ENB			30
	#define SPEAR1340_C3_CLK_ENB			29
	#define SPEAR1340_CLCD_CLK_ENB			27
	#define SPEAR1340_DMA_CLK_ENB			25
	#define SPEAR1340_GPIO1_CLK_ENB			24
	#define SPEAR1340_GPIO0_CLK_ENB			23
	#define SPEAR1340_GPT1_CLK_ENB			22
	#define SPEAR1340_GPT0_CLK_ENB			21
	#define SPEAR1340_I2S_PLAY_CLK_ENB		20
	#define SPEAR1340_I2S_REC_CLK_ENB		19
	#define SPEAR1340_I2C0_CLK_ENB			18
	#define SPEAR1340_SSP_CLK_ENB			17
	#define SPEAR1340_UART0_CLK_ENB			15
	#define SPEAR1340_PCIE_SATA_CLK_ENB		12
	#define SPEAR1340_UOC_CLK_ENB			11
	#define SPEAR1340_UHC1_CLK_ENB			10
	#define SPEAR1340_UHC0_CLK_ENB			9
	#define SPEAR1340_GMAC_CLK_ENB			8
	#define SPEAR1340_CFXD_CLK_ENB			7
	#define SPEAR1340_SDHCI_CLK_ENB			6
	#define SPEAR1340_SMI_CLK_ENB			5
	#define SPEAR1340_FSMC_CLK_ENB			4
	#define SPEAR1340_SYSRAM0_CLK_ENB		3
	#define SPEAR1340_SYSRAM1_CLK_ENB		2
	#define SPEAR1340_SYSROM_CLK_ENB		1
	#define SPEAR1340_BUS_CLK_ENB			0

#define SPEAR1340_PERIP2_CLK_ENB		(misc_base + 0x310)
	#define SPEAR1340_THSENS_CLK_ENB		8
	#define SPEAR1340_I2S_REF_PAD_CLK_ENB		7
	#define SPEAR1340_ACP_CLK_ENB			6
	#define SPEAR1340_GPT3_CLK_ENB			5
	#define SPEAR1340_GPT2_CLK_ENB			4
	#define SPEAR1340_KBD_CLK_ENB			3
	#define SPEAR1340_CPU_DBG_CLK_ENB		2
	#define SPEAR1340_DDR_CORE_CLK_ENB		1
	#define SPEAR1340_DDR_CTRL_CLK_ENB		0

#define SPEAR1340_PERIP3_CLK_ENB		(misc_base + 0x314)
	#define SPEAR1340_PLGPIO_CLK_ENB		18
	#define SPEAR1340_VIDEO_DEC_CLK_ENB		16
	#define SPEAR1340_VIDEO_ENC_CLK_ENB		15
	#define SPEAR1340_SPDIF_OUT_CLK_ENB		13
	#define SPEAR1340_SPDIF_IN_CLK_ENB		12
	#define SPEAR1340_VIDEO_IN_CLK_ENB		11
	#define SPEAR1340_CAM0_CLK_ENB			10
	#define SPEAR1340_CAM1_CLK_ENB			9
	#define SPEAR1340_CAM2_CLK_ENB			8
	#define SPEAR1340_CAM3_CLK_ENB			7
	#define SPEAR1340_MALI_CLK_ENB			6
	#define SPEAR1340_CEC0_CLK_ENB			5
	#define SPEAR1340_CEC1_CLK_ENB			4
	#define SPEAR1340_PWM_CLK_ENB			3
	#define SPEAR1340_I2C1_CLK_ENB			2
	#define SPEAR1340_UART1_CLK_ENB			1

static DEFINE_SPINLOCK(_lock);

/* pll rate configuration table, in ascending order of rates */
static struct pll_rate_tbl pll_rtbl[] = {
	/* PCLK 24MHz */
	{.mode = 0, .m = 0x83, .n = 0x04, .p = 0x5}, /* vco 1572, pll 49.125 MHz */
	{.mode = 0, .m = 0x7D, .n = 0x06, .p = 0x3}, /* vco 1000, pll 125 MHz */
	{.mode = 0, .m = 0x64, .n = 0x06, .p = 0x1}, /* vco 800, pll 400 MHz */
	{.mode = 0, .m = 0x7D, .n = 0x06, .p = 0x1}, /* vco 1000, pll 500 MHz */
	{.mode = 0, .m = 0xA6, .n = 0x06, .p = 0x1}, /* vco 1328, pll 664 MHz */
	{.mode = 0, .m = 0xC8, .n = 0x06, .p = 0x1}, /* vco 1600, pll 800 MHz */
	{.mode = 0, .m = 0x7D, .n = 0x06, .p = 0x0}, /* vco 1, pll 1 GHz */
	{.mode = 0, .m = 0x96, .n = 0x06, .p = 0x0}, /* vco 1200, pll 1200 MHz */
};

/* vco-pll4 rate configuration table, in ascending order of rates */
static struct pll_rate_tbl pll4_rtbl[] = {
	{.mode = 0, .m = 0x7D, .n = 0x06, .p = 0x2}, /* vco 1000, pll 250 MHz */
	{.mode = 0, .m = 0xA6, .n = 0x06, .p = 0x2}, /* vco 1328, pll 332 MHz */
	{.mode = 0, .m = 0xC8, .n = 0x06, .p = 0x2}, /* vco 1600, pll 400 MHz */
	{.mode = 0, .m = 0x7D, .n = 0x06, .p = 0x0}, /* vco 1, pll 1 GHz */
};

/*
 * All below entries generate 166 MHz for
 * different values of vco1div2
 */
static struct frac_rate_tbl amba_synth_rtbl[] = {
	{.div = 0x073A8}, /* for vco1div2 = 600 MHz */
	{.div = 0x06062}, /* for vco1div2 = 500 MHz */
	{.div = 0x04D1B}, /* for vco1div2 = 400 MHz */
	{.div = 0x04000}, /* for vco1div2 = 332 MHz */
	{.div = 0x03031}, /* for vco1div2 = 250 MHz */
	{.div = 0x0268D}, /* for vco1div2 = 200 MHz */
};

/*
 * Synthesizer Clock derived from vcodiv2. This clock is one of the
 * possible clocks to feed cpu directly.
 * We can program this synthesizer to make cpu run on different clock
 * frequencies.
 * Following table provides configuration values to let cpu run on 200,
 * 250, 332, 400 or 500 MHz considering different possibilites of input
 * (vco1div2) clock.
 *
 * --------------------------------------------------------------------
 * vco1div2(Mhz)	fout(Mhz)	cpuclk = fout/2		div
 * --------------------------------------------------------------------
 * 400			200		100			0x04000
 * 400			250		125			0x03333
 * 400			332		166			0x0268D
 * 400			400		200			0x02000
 * --------------------------------------------------------------------
 * 500			200		100			0x05000
 * 500			250		125			0x04000
 * 500			332		166			0x03031
 * 500			400		200			0x02800
 * 500			500		250			0x02000
 * --------------------------------------------------------------------
 * 600			200		100			0x06000
 * 600			250		125			0x04CCE
 * 600			332		166			0x039D5
 * 600			400		200			0x03000
 * 600			500		250			0x02666
 * --------------------------------------------------------------------
 * 664			200		100			0x06a38
 * 664			250		125			0x054FD
 * 664			332		166			0x04000
 * 664			400		200			0x0351E
 * 664			500		250			0x02A7E
 * --------------------------------------------------------------------
 * 800			200		100			0x08000
 * 800			250		125			0x06666
 * 800			332		166			0x04D18
 * 800			400		200			0x04000
 * 800			500		250			0x03333
 * --------------------------------------------------------------------
 * sys rate configuration table is in descending order of divisor.
 */
static struct frac_rate_tbl sys_synth_rtbl[] = {
	{.div = 0x08000},
	{.div = 0x06a38},
	{.div = 0x06666},
	{.div = 0x06000},
	{.div = 0x054FD},
	{.div = 0x05000},
	{.div = 0x04D18},
	{.div = 0x04CCE},
	{.div = 0x04000},
	{.div = 0x039D5},
	{.div = 0x0351E},
	{.div = 0x03333},
	{.div = 0x03031},
	{.div = 0x03000},
	{.div = 0x02A7E},
	{.div = 0x02800},
	{.div = 0x0268D},
	{.div = 0x02666},
	{.div = 0x02000},
};

/* aux rate configuration table, in ascending order of rates */
static struct aux_rate_tbl aux_rtbl[] = {
	/* 12.29MHz for vic1div2=600MHz and 10.24MHz for VCO1div2=500MHz */
	{.xscale = 5, .yscale = 122, .eq = 0},
	/* 14.70MHz for vic1div2=600MHz and 12.29MHz for VCO1div2=500MHz */
	{.xscale = 10, .yscale = 204, .eq = 0},
	/* 48MHz for vic1div2=600MHz and 40 MHz for VCO1div2=500MHz */
	{.xscale = 4, .yscale = 25, .eq = 0},
	/* 57.14MHz for vic1div2=600MHz and 48 MHz for VCO1div2=500MHz */
	{.xscale = 4, .yscale = 21, .eq = 0},
	/* 83.33MHz for vic1div2=600MHz and 69.44MHz for VCO1div2=500MHz */
	{.xscale = 5, .yscale = 18, .eq = 0},
	/* 100MHz for vic1div2=600MHz and 83.33 MHz for VCO1div2=500MHz */
	{.xscale = 2, .yscale = 6, .eq = 0},
	/* 125MHz for vic1div2=600MHz and 104.1MHz for VCO1div2=500MHz */
	{.xscale = 5, .yscale = 12, .eq = 0},
	/* 150MHz for vic1div2=600MHz and 125MHz for VCO1div2=500MHz */
	{.xscale = 2, .yscale = 4, .eq = 0},
	/* 166MHz for vic1div2=600MHz and 138.88MHz for VCO1div2=500MHz */
	{.xscale = 5, .yscale = 18, .eq = 1},
	/* 200MHz for vic1div2=600MHz and 166MHz for VCO1div2=500MHz */
	{.xscale = 1, .yscale = 3, .eq = 1},
	/* 250MHz for vic1div2=600MHz and 208.33MHz for VCO1div2=500MHz */
	{.xscale = 5, .yscale = 12, .eq = 1},
	/* 300MHz for vic1div2=600MHz and 250MHz for VCO1div2=500MHz */
	{.xscale = 1, .yscale = 2, .eq = 1},
};

/* gmac rate configuration table, in ascending order of rates */
static struct aux_rate_tbl gmac_rtbl[] = {
	/* For gmac phy input clk */
	{.xscale = 2, .yscale = 6, .eq = 0}, /* divided by 6 */
	{.xscale = 2, .yscale = 4, .eq = 0}, /* divided by 4 */
	{.xscale = 1, .yscale = 3, .eq = 1}, /* divided by 3 */
	{.xscale = 1, .yscale = 2, .eq = 1}, /* divided by 2 */
};

/* clcd rate configuration table, in ascending order of rates */
static struct frac_rate_tbl clcd_rtbl[] = {
	{.div = 0x18000}, /* 25 Mhz , for vc01div4 = 300 MHz*/
	{.div = 0x1638E}, /* 27 Mhz , for vc01div4 = 300 MHz*/
	{.div = 0x14000}, /* 25 Mhz , for vc01div4 = 250 MHz*/
	{.div = 0x1284B}, /* 27 Mhz , for vc01div4 = 250 MHz*/
	{.div = 0x0D8D3}, /* 58 Mhz , for vco1div4 = 393 MHz */
	{.div = 0x0B72C}, /* 58 Mhz , for vco1div4 = 332 MHz */
	{.div = 0x0A584}, /* 58 Mhz , for vco1div4 = 300 MHz */
	{.div = 0x093B1}, /* 65 Mhz , for vc01div4 = 300 MHz*/
	{.div = 0x089EE}, /* 58 Mhz , for vc01div4 = 250 MHz*/
	{.div = 0x081BA}, /* 74 Mhz , for vc01div4 = 300 MHz*/
	{.div = 0x07BA0}, /* 65 Mhz , for vc01div4 = 250 MHz*/
	{.div = 0x06f1C}, /* 72 Mhz , for vc01div4 = 250 MHz*/
	{.div = 0x06E58}, /* 58 Mhz , for vco1div4 = 200 MHz */
	{.div = 0x06c1B}, /* 74 Mhz , for vc01div4 = 250 MHz*/
	{.div = 0x058E3}, /* 108 Mhz , for vc01div4 = 300 MHz*/
	{.div = 0x04A12}, /* 108 Mhz , for vc01div4 = 250 MHz*/
	{.div = 0x040A5}, /* 148.5 Mhz , for vc01div4 = 300 MHz*/
	{.div = 0x0378E}, /* 144 Mhz , for vc01div4 = 250 MHz*/
	{.div = 0x0360D}, /* 148 Mhz , for vc01div4 = 250 MHz*/
	{.div = 0x035E0}, /* 148.5 MHz, for vc01div4 = 250 MHz*/
};

/* i2s prescaler1 masks */
static struct aux_clk_masks i2s_prs1_masks = {
	.eq_sel_mask = AUX_EQ_SEL_MASK,
	.eq_sel_shift = SPEAR1340_I2S_PRS1_EQ_SEL_SHIFT,
	.eq1_mask = AUX_EQ1_SEL,
	.eq2_mask = AUX_EQ2_SEL,
	.xscale_sel_mask = SPEAR1340_I2S_PRS1_CLK_X_MASK,
	.xscale_sel_shift = SPEAR1340_I2S_PRS1_CLK_X_SHIFT,
	.yscale_sel_mask = SPEAR1340_I2S_PRS1_CLK_Y_MASK,
	.yscale_sel_shift = SPEAR1340_I2S_PRS1_CLK_Y_SHIFT,
};

/* i2s sclk (bit clock) syynthesizers masks */
static struct aux_clk_masks i2s_sclk_masks = {
	.eq_sel_mask = AUX_EQ_SEL_MASK,
	.eq_sel_shift = SPEAR1340_I2S_SCLK_EQ_SEL_SHIFT,
	.eq1_mask = AUX_EQ1_SEL,
	.eq2_mask = AUX_EQ2_SEL,
	.xscale_sel_mask = SPEAR1340_I2S_SCLK_X_MASK,
	.xscale_sel_shift = SPEAR1340_I2S_SCLK_X_SHIFT,
	.yscale_sel_mask = SPEAR1340_I2S_SCLK_Y_MASK,
	.yscale_sel_shift = SPEAR1340_I2S_SCLK_Y_SHIFT,
	.enable_bit = SPEAR1340_I2S_SCLK_SYNTH_ENB,
};

/* i2s prs1 aux rate configuration table, in ascending order of rates */
static struct aux_rate_tbl i2s_prs1_rtbl[] = {
	/* For parent clk = 49.152 MHz */
	{.xscale = 1, .yscale = 12, .eq = 0}, /* 2.048 MHz, smp freq = 8Khz */
	{.xscale = 11, .yscale = 96, .eq = 0}, /* 2.816 MHz, smp freq = 11Khz */
	{.xscale = 1, .yscale = 6, .eq = 0}, /* 4.096 MHz, smp freq = 16Khz */
	{.xscale = 11, .yscale = 48, .eq = 0}, /* 5.632 MHz, smp freq = 22Khz */

	/*
	 * with parent clk = 49.152, freq gen is 8.192 MHz, smp freq = 32Khz
	 * with parent clk = 12.288, freq gen is 2.048 MHz, smp freq = 8Khz
	 */
	{.xscale = 1, .yscale = 3, .eq = 0},

	/* For parent clk = 49.152 MHz */
	{.xscale = 17, .yscale = 37, .eq = 0}, /* 11.289 MHz, smp freq = 44Khz*/
	{.xscale = 1, .yscale = 2, .eq = 0}, /* 12.288 MHz, smp freq = 48Khz*/
};

/* i2s sclk aux rate configuration table, in ascending order of rates */
static struct aux_rate_tbl i2s_sclk_rtbl[] = {
	/* For sclk = ref_clk * x/2/y */
	{.xscale = 1, .yscale = 4, .eq = 0},
	{.xscale = 1, .yscale = 2, .eq = 0},
};

/* adc rate configuration table, in ascending order of rates */
/* possible adc range is 2.5 MHz to 20 MHz. */
static struct aux_rate_tbl adc_rtbl[] = {
	/* For ahb = 166.67 MHz */
	{.xscale = 1, .yscale = 31, .eq = 0}, /* 2.68 MHz */
	{.xscale = 2, .yscale = 21, .eq = 0}, /* 7.94 MHz */
	{.xscale = 4, .yscale = 21, .eq = 0}, /* 15.87 MHz */
	{.xscale = 10, .yscale = 42, .eq = 0}, /* 19.84 MHz */
};

/* General synth rate configuration table, in ascending order of rates */
static struct frac_rate_tbl gen_rtbl[] = {
	{.div = 0x1A92B}, /* 22.5792 MHz for vco1div4=300 MHz*/
	{.div = 0x186A0}, /* 24.576 MHz for vco1div4=300 MHz*/
	{.div = 0x18000}, /* 25 MHz for vco1div4=300 MHz*/
	{.div = 0x1624E}, /* 22.5792 MHz for vco1div4=250 MHz*/
	{.div = 0x14585}, /* 24.576 MHz for vco1div4=250 MHz*/
	{.div = 0x14000}, /* 25 MHz for vco1div4=250 MHz*/
	{.div = 0x0D495}, /* 45.1584 MHz for vco1div4=300 MHz*/
	{.div = 0x0C000}, /* 50 MHz for vco1div4=300 MHz*/
	{.div = 0x0B127}, /* 45.1584 MHz for vco1div4=250 MHz*/
	{.div = 0x0A000}, /* 50 MHz for vco1div4=250 MHz*/
	{.div = 0x07530}, /* 81.92 MHz for vco1div4=300 MHz*/
	{.div = 0x061A8}, /* 81.92 MHz for vco1div4=250 MHz*/
	{.div = 0x06000}, /* 100 MHz for vco1div4=300 MHz*/
	{.div = 0x05000}, /* 100 MHz for vco1div4=250 MHz*/
	{.div = 0x03000}, /* 200 MHz for vco1div4=300 MHz*/
	{.div = 0x02DB6}, /* 210 MHz for vco1div4=300 MHz*/
	{.div = 0x02BA2}, /* 220 MHz for vco1div4=300 MHz*/
	{.div = 0x029BD}, /* 230 MHz for vco1div4=300 MHz*/
	{.div = 0x02800}, /* 200 MHz for vco1div4=250 MHz*/
	{.div = 0x02666}, /* 250 MHz for vco1div4=300 MHz*/
	{.div = 0x02620}, /* 210 MHz for vco1div4=250 MHz*/
	{.div = 0x02460}, /* 220 MHz for vco1div4=250 MHz*/
	{.div = 0x022C0}, /* 230 MHz for vco1div4=250 MHz*/
	{.div = 0x02160}, /* 240 MHz for vco1div4=250 MHz*/
	{.div = 0x02000}, /* 250 MHz for vco1div4=250 MHz*/
};

/* clock parents */
static const char *vco_parents[] = { "osc_24m_clk", "osc_25m_clk", };
static const char *sys_parents[] = { "pll1_clk", "pll1_clk", "pll1_clk",
	"pll1_clk", "sys_syn_clk", "sys_syn_clk", "pll2_clk", "pll3_clk", };
static const char *ahb_parents[] = { "cpu_div3_clk", "amba_syn_clk", };
static const char *gpt_parents[] = { "osc_24m_clk", "apb_clk", };
static const char *uart0_parents[] = { "pll5_clk", "osc_24m_clk",
	"uart0_syn_gclk", };
static const char *uart1_parents[] = { "pll5_clk", "osc_24m_clk",
	"uart1_syn_gclk", };
static const char *c3_parents[] = { "pll5_clk", "c3_syn_gclk", };
static const char *gmac_phy_input_parents[] = { "gmii_pad_clk", "pll2_clk",
	"osc_25m_clk", };
static const char *gmac_phy_parents[] = { "phy_input_mclk", "phy_syn_gclk", };
static const char *clcd_synth_parents[] = { "vco1div4_clk", "pll2_clk", };
static const char *clcd_pixel_parents[] = { "pll5_clk", "clcd_syn_clk", };
static const char *i2s_src_parents[] = { "vco1div2_clk", "pll2_clk", "pll3_clk",
	"i2s_src_pad_clk", };
static const char *i2s_ref_parents[] = { "i2s_src_mclk", "i2s_prs1_clk", };
static const char *spdif_out_parents[] = { "i2s_src_pad_clk", "gen_syn2_clk", };
static const char *spdif_in_parents[] = { "pll2_clk", "gen_syn3_clk", };

static const char *gen_synth0_1_parents[] = { "vco1div4_clk", "vco3div2_clk",
	"pll3_clk", };
static const char *gen_synth2_3_parents[] = { "vco1div4_clk", "vco2div2_clk",
	"pll2_clk", };

void __init spear1340_clk_init(void __iomem *misc_base)
{
	struct clk *clk, *clk1;

	clk = clk_register_fixed_rate(NULL, "osc_32k_clk", NULL, CLK_IS_ROOT,
			32000);
	clk_register_clkdev(clk, "osc_32k_clk", NULL);

	clk = clk_register_fixed_rate(NULL, "osc_24m_clk", NULL, CLK_IS_ROOT,
			24000000);
	clk_register_clkdev(clk, "osc_24m_clk", NULL);

	clk = clk_register_fixed_rate(NULL, "osc_25m_clk", NULL, CLK_IS_ROOT,
			25000000);
	clk_register_clkdev(clk, "osc_25m_clk", NULL);

	clk = clk_register_fixed_rate(NULL, "gmii_pad_clk", NULL, CLK_IS_ROOT,
			125000000);
	clk_register_clkdev(clk, "gmii_pad_clk", NULL);

	clk = clk_register_fixed_rate(NULL, "i2s_src_pad_clk", NULL,
			CLK_IS_ROOT, 12288000);
	clk_register_clkdev(clk, "i2s_src_pad_clk", NULL);

	/* clock derived from 32 KHz osc clk */
	clk = clk_register_gate(NULL, "rtc-spear", "osc_32k_clk", 0,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_RTC_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "e0580000.rtc");

	/* clock derived from 24 or 25 MHz osc clk */
	/* vco-pll */
	clk = clk_register_mux(NULL, "vco1_mclk", vco_parents,
			ARRAY_SIZE(vco_parents), CLK_SET_RATE_NO_REPARENT,
			SPEAR1340_PLL_CFG, SPEAR1340_PLL1_CLK_SHIFT,
			SPEAR1340_PLL_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "vco1_mclk", NULL);
	clk = clk_register_vco_pll("vco1_clk", "pll1_clk", NULL, "vco1_mclk", 0,
			SPEAR1340_PLL1_CTR, SPEAR1340_PLL1_FRQ, pll_rtbl,
			ARRAY_SIZE(pll_rtbl), &_lock, &clk1, NULL);
	clk_register_clkdev(clk, "vco1_clk", NULL);
	clk_register_clkdev(clk1, "pll1_clk", NULL);

	clk = clk_register_mux(NULL, "vco2_mclk", vco_parents,
			ARRAY_SIZE(vco_parents), CLK_SET_RATE_NO_REPARENT,
			SPEAR1340_PLL_CFG, SPEAR1340_PLL2_CLK_SHIFT,
			SPEAR1340_PLL_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "vco2_mclk", NULL);
	clk = clk_register_vco_pll("vco2_clk", "pll2_clk", NULL, "vco2_mclk", 0,
			SPEAR1340_PLL2_CTR, SPEAR1340_PLL2_FRQ, pll_rtbl,
			ARRAY_SIZE(pll_rtbl), &_lock, &clk1, NULL);
	clk_register_clkdev(clk, "vco2_clk", NULL);
	clk_register_clkdev(clk1, "pll2_clk", NULL);

	clk = clk_register_mux(NULL, "vco3_mclk", vco_parents,
			ARRAY_SIZE(vco_parents), CLK_SET_RATE_NO_REPARENT,
			SPEAR1340_PLL_CFG, SPEAR1340_PLL3_CLK_SHIFT,
			SPEAR1340_PLL_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "vco3_mclk", NULL);
	clk = clk_register_vco_pll("vco3_clk", "pll3_clk", NULL, "vco3_mclk", 0,
			SPEAR1340_PLL3_CTR, SPEAR1340_PLL3_FRQ, pll_rtbl,
			ARRAY_SIZE(pll_rtbl), &_lock, &clk1, NULL);
	clk_register_clkdev(clk, "vco3_clk", NULL);
	clk_register_clkdev(clk1, "pll3_clk", NULL);

	clk = clk_register_vco_pll("vco4_clk", "pll4_clk", NULL, "osc_24m_clk",
			0, SPEAR1340_PLL4_CTR, SPEAR1340_PLL4_FRQ, pll4_rtbl,
			ARRAY_SIZE(pll4_rtbl), &_lock, &clk1, NULL);
	clk_register_clkdev(clk, "vco4_clk", NULL);
	clk_register_clkdev(clk1, "pll4_clk", NULL);

	clk = clk_register_fixed_rate(NULL, "pll5_clk", "osc_24m_clk", 0,
			48000000);
	clk_register_clkdev(clk, "pll5_clk", NULL);

	clk = clk_register_fixed_rate(NULL, "pll6_clk", "osc_25m_clk", 0,
			25000000);
	clk_register_clkdev(clk, "pll6_clk", NULL);

	/* vco div n clocks */
	clk = clk_register_fixed_factor(NULL, "vco1div2_clk", "vco1_clk", 0, 1,
			2);
	clk_register_clkdev(clk, "vco1div2_clk", NULL);

	clk = clk_register_fixed_factor(NULL, "vco1div4_clk", "vco1_clk", 0, 1,
			4);
	clk_register_clkdev(clk, "vco1div4_clk", NULL);

	clk = clk_register_fixed_factor(NULL, "vco2div2_clk", "vco2_clk", 0, 1,
			2);
	clk_register_clkdev(clk, "vco2div2_clk", NULL);

	clk = clk_register_fixed_factor(NULL, "vco3div2_clk", "vco3_clk", 0, 1,
			2);
	clk_register_clkdev(clk, "vco3div2_clk", NULL);

	/* peripherals */
	clk_register_fixed_factor(NULL, "thermal_clk", "osc_24m_clk", 0, 1,
			128);
	clk = clk_register_gate(NULL, "thermal_gclk", "thermal_clk", 0,
			SPEAR1340_PERIP2_CLK_ENB, SPEAR1340_THSENS_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "e07008c4.thermal");

	/* clock derived from pll4 clk */
	clk = clk_register_fixed_factor(NULL, "ddr_clk", "pll4_clk", 0, 1,
			1);
	clk_register_clkdev(clk, "ddr_clk", NULL);

	/* clock derived from pll1 clk */
	clk = clk_register_frac("sys_syn_clk", "vco1div2_clk", 0,
			SPEAR1340_SYS_CLK_SYNT, sys_synth_rtbl,
			ARRAY_SIZE(sys_synth_rtbl), &_lock);
	clk_register_clkdev(clk, "sys_syn_clk", NULL);

	clk = clk_register_frac("amba_syn_clk", "vco1div2_clk", 0,
			SPEAR1340_AMBA_CLK_SYNT, amba_synth_rtbl,
			ARRAY_SIZE(amba_synth_rtbl), &_lock);
	clk_register_clkdev(clk, "amba_syn_clk", NULL);

	clk = clk_register_mux(NULL, "sys_mclk", sys_parents,
			ARRAY_SIZE(sys_parents), CLK_SET_RATE_NO_REPARENT,
			SPEAR1340_SYS_CLK_CTRL, SPEAR1340_SCLK_SRC_SEL_SHIFT,
			SPEAR1340_SCLK_SRC_SEL_MASK, 0, &_lock);
	clk_register_clkdev(clk, "sys_mclk", NULL);

	clk = clk_register_fixed_factor(NULL, "cpu_clk", "sys_mclk", 0, 1,
			2);
	clk_register_clkdev(clk, "cpu_clk", NULL);

	clk = clk_register_fixed_factor(NULL, "cpu_div3_clk", "cpu_clk", 0, 1,
			3);
	clk_register_clkdev(clk, "cpu_div3_clk", NULL);

	clk = clk_register_fixed_factor(NULL, "wdt_clk", "cpu_clk", 0, 1,
			2);
	clk_register_clkdev(clk, NULL, "ec800620.wdt");

	clk = clk_register_fixed_factor(NULL, "smp_twd_clk", "cpu_clk", 0, 1,
			2);
	clk_register_clkdev(clk, NULL, "smp_twd");

	clk = clk_register_mux(NULL, "ahb_clk", ahb_parents,
			ARRAY_SIZE(ahb_parents), CLK_SET_RATE_NO_REPARENT,
			SPEAR1340_SYS_CLK_CTRL, SPEAR1340_HCLK_SRC_SEL_SHIFT,
			SPEAR1340_HCLK_SRC_SEL_MASK, 0, &_lock);
	clk_register_clkdev(clk, "ahb_clk", NULL);

	clk = clk_register_fixed_factor(NULL, "apb_clk", "ahb_clk", 0, 1,
			2);
	clk_register_clkdev(clk, "apb_clk", NULL);

	/* gpt clocks */
	clk = clk_register_mux(NULL, "gpt0_mclk", gpt_parents,
			ARRAY_SIZE(gpt_parents), CLK_SET_RATE_NO_REPARENT,
			SPEAR1340_PERIP_CLK_CFG, SPEAR1340_GPT0_CLK_SHIFT,
			SPEAR1340_GPT_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "gpt0_mclk", NULL);
	clk = clk_register_gate(NULL, "gpt0_clk", "gpt0_mclk", 0,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_GPT0_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "gpt0");

	clk = clk_register_mux(NULL, "gpt1_mclk", gpt_parents,
			ARRAY_SIZE(gpt_parents), CLK_SET_RATE_NO_REPARENT,
			SPEAR1340_PERIP_CLK_CFG, SPEAR1340_GPT1_CLK_SHIFT,
			SPEAR1340_GPT_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "gpt1_mclk", NULL);
	clk = clk_register_gate(NULL, "gpt1_clk", "gpt1_mclk", 0,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_GPT1_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "gpt1");

	clk = clk_register_mux(NULL, "gpt2_mclk", gpt_parents,
			ARRAY_SIZE(gpt_parents), CLK_SET_RATE_NO_REPARENT,
			SPEAR1340_PERIP_CLK_CFG, SPEAR1340_GPT2_CLK_SHIFT,
			SPEAR1340_GPT_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "gpt2_mclk", NULL);
	clk = clk_register_gate(NULL, "gpt2_clk", "gpt2_mclk", 0,
			SPEAR1340_PERIP2_CLK_ENB, SPEAR1340_GPT2_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "gpt2");

	clk = clk_register_mux(NULL, "gpt3_mclk", gpt_parents,
			ARRAY_SIZE(gpt_parents), CLK_SET_RATE_NO_REPARENT,
			SPEAR1340_PERIP_CLK_CFG, SPEAR1340_GPT3_CLK_SHIFT,
			SPEAR1340_GPT_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "gpt3_mclk", NULL);
	clk = clk_register_gate(NULL, "gpt3_clk", "gpt3_mclk", 0,
			SPEAR1340_PERIP2_CLK_ENB, SPEAR1340_GPT3_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "gpt3");

	/* others */
	clk = clk_register_aux("uart0_syn_clk", "uart0_syn_gclk",
			"vco1div2_clk", 0, SPEAR1340_UART0_CLK_SYNT, NULL,
			aux_rtbl, ARRAY_SIZE(aux_rtbl), &_lock, &clk1);
	clk_register_clkdev(clk, "uart0_syn_clk", NULL);
	clk_register_clkdev(clk1, "uart0_syn_gclk", NULL);

	clk = clk_register_mux(NULL, "uart0_mclk", uart0_parents,
			ARRAY_SIZE(uart0_parents),
			CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			SPEAR1340_PERIP_CLK_CFG, SPEAR1340_UART0_CLK_SHIFT,
			SPEAR1340_UART_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "uart0_mclk", NULL);

	clk = clk_register_gate(NULL, "uart0_clk", "uart0_mclk",
			CLK_SET_RATE_PARENT, SPEAR1340_PERIP1_CLK_ENB,
			SPEAR1340_UART0_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "e0000000.serial");

	clk = clk_register_aux("uart1_syn_clk", "uart1_syn_gclk",
			"vco1div2_clk", 0, SPEAR1340_UART1_CLK_SYNT, NULL,
			aux_rtbl, ARRAY_SIZE(aux_rtbl), &_lock, &clk1);
	clk_register_clkdev(clk, "uart1_syn_clk", NULL);
	clk_register_clkdev(clk1, "uart1_syn_gclk", NULL);

	clk = clk_register_mux(NULL, "uart1_mclk", uart1_parents,
			ARRAY_SIZE(uart1_parents), CLK_SET_RATE_NO_REPARENT,
			SPEAR1340_PERIP_CLK_CFG, SPEAR1340_UART1_CLK_SHIFT,
			SPEAR1340_UART_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "uart1_mclk", NULL);

	clk = clk_register_gate(NULL, "uart1_clk", "uart1_mclk", 0,
			SPEAR1340_PERIP3_CLK_ENB, SPEAR1340_UART1_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "b4100000.serial");

	clk = clk_register_aux("sdhci_syn_clk", "sdhci_syn_gclk",
			"vco1div2_clk", 0, SPEAR1340_SDHCI_CLK_SYNT, NULL,
			aux_rtbl, ARRAY_SIZE(aux_rtbl), &_lock, &clk1);
	clk_register_clkdev(clk, "sdhci_syn_clk", NULL);
	clk_register_clkdev(clk1, "sdhci_syn_gclk", NULL);

	clk = clk_register_gate(NULL, "sdhci_clk", "sdhci_syn_gclk",
			CLK_SET_RATE_PARENT, SPEAR1340_PERIP1_CLK_ENB,
			SPEAR1340_SDHCI_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "b3000000.sdhci");

	clk = clk_register_aux("cfxd_syn_clk", "cfxd_syn_gclk", "vco1div2_clk",
			0, SPEAR1340_CFXD_CLK_SYNT, NULL, aux_rtbl,
			ARRAY_SIZE(aux_rtbl), &_lock, &clk1);
	clk_register_clkdev(clk, "cfxd_syn_clk", NULL);
	clk_register_clkdev(clk1, "cfxd_syn_gclk", NULL);

	clk = clk_register_gate(NULL, "cfxd_clk", "cfxd_syn_gclk",
			CLK_SET_RATE_PARENT, SPEAR1340_PERIP1_CLK_ENB,
			SPEAR1340_CFXD_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "b2800000.cf");
	clk_register_clkdev(clk, NULL, "arasan_xd");

	clk = clk_register_aux("c3_syn_clk", "c3_syn_gclk", "vco1div2_clk", 0,
			SPEAR1340_C3_CLK_SYNT, NULL, aux_rtbl,
			ARRAY_SIZE(aux_rtbl), &_lock, &clk1);
	clk_register_clkdev(clk, "c3_syn_clk", NULL);
	clk_register_clkdev(clk1, "c3_syn_gclk", NULL);

	clk = clk_register_mux(NULL, "c3_mclk", c3_parents,
			ARRAY_SIZE(c3_parents),
			CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			SPEAR1340_PERIP_CLK_CFG, SPEAR1340_C3_CLK_SHIFT,
			SPEAR1340_C3_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "c3_mclk", NULL);

	clk = clk_register_gate(NULL, "c3_clk", "c3_mclk", CLK_SET_RATE_PARENT,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_C3_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "e1800000.c3");

	/* gmac */
	clk = clk_register_mux(NULL, "phy_input_mclk", gmac_phy_input_parents,
			ARRAY_SIZE(gmac_phy_input_parents),
			CLK_SET_RATE_NO_REPARENT, SPEAR1340_GMAC_CLK_CFG,
			SPEAR1340_GMAC_PHY_INPUT_CLK_SHIFT,
			SPEAR1340_GMAC_PHY_INPUT_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "phy_input_mclk", NULL);

	clk = clk_register_aux("phy_syn_clk", "phy_syn_gclk", "phy_input_mclk",
			0, SPEAR1340_GMAC_CLK_SYNT, NULL, gmac_rtbl,
			ARRAY_SIZE(gmac_rtbl), &_lock, &clk1);
	clk_register_clkdev(clk, "phy_syn_clk", NULL);
	clk_register_clkdev(clk1, "phy_syn_gclk", NULL);

	clk = clk_register_mux(NULL, "phy_mclk", gmac_phy_parents,
			ARRAY_SIZE(gmac_phy_parents), CLK_SET_RATE_NO_REPARENT,
			SPEAR1340_PERIP_CLK_CFG, SPEAR1340_GMAC_PHY_CLK_SHIFT,
			SPEAR1340_GMAC_PHY_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "stmmacphy.0", NULL);

	/* clcd */
	clk = clk_register_mux(NULL, "clcd_syn_mclk", clcd_synth_parents,
			ARRAY_SIZE(clcd_synth_parents),
			CLK_SET_RATE_NO_REPARENT, SPEAR1340_CLCD_CLK_SYNT,
			SPEAR1340_CLCD_SYNT_CLK_SHIFT,
			SPEAR1340_CLCD_SYNT_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "clcd_syn_mclk", NULL);

	clk = clk_register_frac("clcd_syn_clk", "clcd_syn_mclk", 0,
			SPEAR1340_CLCD_CLK_SYNT, clcd_rtbl,
			ARRAY_SIZE(clcd_rtbl), &_lock);
	clk_register_clkdev(clk, "clcd_syn_clk", NULL);

	clk = clk_register_mux(NULL, "clcd_pixel_mclk", clcd_pixel_parents,
			ARRAY_SIZE(clcd_pixel_parents),
			CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			SPEAR1340_PERIP_CLK_CFG, SPEAR1340_CLCD_CLK_SHIFT,
			SPEAR1340_CLCD_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "clcd_pixel_mclk", NULL);

	clk = clk_register_gate(NULL, "clcd_clk", "clcd_pixel_mclk", 0,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_CLCD_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "e1000000.clcd");

	/* i2s */
	clk = clk_register_mux(NULL, "i2s_src_mclk", i2s_src_parents,
			ARRAY_SIZE(i2s_src_parents), CLK_SET_RATE_NO_REPARENT,
			SPEAR1340_I2S_CLK_CFG, SPEAR1340_I2S_SRC_CLK_SHIFT,
			SPEAR1340_I2S_SRC_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "i2s_src_mclk", NULL);

	clk = clk_register_aux("i2s_prs1_clk", NULL, "i2s_src_mclk",
			CLK_SET_RATE_PARENT, SPEAR1340_I2S_CLK_CFG,
			&i2s_prs1_masks, i2s_prs1_rtbl,
			ARRAY_SIZE(i2s_prs1_rtbl), &_lock, NULL);
	clk_register_clkdev(clk, "i2s_prs1_clk", NULL);

	clk = clk_register_mux(NULL, "i2s_ref_mclk", i2s_ref_parents,
			ARRAY_SIZE(i2s_ref_parents),
			CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			SPEAR1340_I2S_CLK_CFG, SPEAR1340_I2S_REF_SHIFT,
			SPEAR1340_I2S_REF_SEL_MASK, 0, &_lock);
	clk_register_clkdev(clk, "i2s_ref_mclk", NULL);

	clk = clk_register_gate(NULL, "i2s_ref_pad_clk", "i2s_ref_mclk", 0,
			SPEAR1340_PERIP2_CLK_ENB, SPEAR1340_I2S_REF_PAD_CLK_ENB,
			0, &_lock);
	clk_register_clkdev(clk, "i2s_ref_pad_clk", NULL);

	clk = clk_register_aux("i2s_sclk_clk", "i2s_sclk_gclk", "i2s_ref_mclk",
			0, SPEAR1340_I2S_CLK_CFG, &i2s_sclk_masks,
			i2s_sclk_rtbl, ARRAY_SIZE(i2s_sclk_rtbl), &_lock,
			&clk1);
	clk_register_clkdev(clk, "i2s_sclk_clk", NULL);
	clk_register_clkdev(clk1, "i2s_sclk_gclk", NULL);

	/* clock derived from ahb clk */
	clk = clk_register_gate(NULL, "i2c0_clk", "ahb_clk", 0,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_I2C0_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "e0280000.i2c");

	clk = clk_register_gate(NULL, "i2c1_clk", "ahb_clk", 0,
			SPEAR1340_PERIP3_CLK_ENB, SPEAR1340_I2C1_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "b4000000.i2c");

	clk = clk_register_gate(NULL, "dma_clk", "ahb_clk", 0,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_DMA_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "ea800000.dma");
	clk_register_clkdev(clk, NULL, "eb000000.dma");

	clk = clk_register_gate(NULL, "gmac_clk", "ahb_clk", 0,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_GMAC_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "e2000000.eth");

	clk = clk_register_gate(NULL, "fsmc_clk", "ahb_clk", 0,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_FSMC_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "b0000000.flash");

	clk = clk_register_gate(NULL, "smi_clk", "ahb_clk", 0,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_SMI_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "ea000000.flash");

	clk = clk_register_gate(NULL, "usbh0_clk", "ahb_clk", 0,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_UHC0_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "e4000000.ohci");
	clk_register_clkdev(clk, NULL, "e4800000.ehci");

	clk = clk_register_gate(NULL, "usbh1_clk", "ahb_clk", 0,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_UHC1_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "e5000000.ohci");
	clk_register_clkdev(clk, NULL, "e5800000.ehci");

	clk = clk_register_gate(NULL, "uoc_clk", "ahb_clk", 0,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_UOC_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "e3800000.otg");

	clk = clk_register_gate(NULL, "pcie_sata_clk", "ahb_clk", 0,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_PCIE_SATA_CLK_ENB,
			0, &_lock);
	clk_register_clkdev(clk, NULL, "b1000000.pcie");
	clk_register_clkdev(clk, NULL, "b1000000.ahci");

	clk = clk_register_gate(NULL, "sysram0_clk", "ahb_clk", 0,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_SYSRAM0_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, "sysram0_clk", NULL);

	clk = clk_register_gate(NULL, "sysram1_clk", "ahb_clk", 0,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_SYSRAM1_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, "sysram1_clk", NULL);

	clk = clk_register_aux("adc_syn_clk", "adc_syn_gclk", "ahb_clk",
			0, SPEAR1340_ADC_CLK_SYNT, NULL, adc_rtbl,
			ARRAY_SIZE(adc_rtbl), &_lock, &clk1);
	clk_register_clkdev(clk, "adc_syn_clk", NULL);
	clk_register_clkdev(clk1, "adc_syn_gclk", NULL);

	clk = clk_register_gate(NULL, "adc_clk", "adc_syn_gclk",
			CLK_SET_RATE_PARENT, SPEAR1340_PERIP1_CLK_ENB,
			SPEAR1340_ADC_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "e0080000.adc");

	/* clock derived from apb clk */
	clk = clk_register_gate(NULL, "ssp_clk", "apb_clk", 0,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_SSP_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "e0100000.spi");

	clk = clk_register_gate(NULL, "gpio0_clk", "apb_clk", 0,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_GPIO0_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "e0600000.gpio");

	clk = clk_register_gate(NULL, "gpio1_clk", "apb_clk", 0,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_GPIO1_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "e0680000.gpio");

	clk = clk_register_gate(NULL, "i2s_play_clk", "apb_clk", 0,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_I2S_PLAY_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "b2400000.i2s-play");

	clk = clk_register_gate(NULL, "i2s_rec_clk", "apb_clk", 0,
			SPEAR1340_PERIP1_CLK_ENB, SPEAR1340_I2S_REC_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "b2000000.i2s-rec");

	clk = clk_register_gate(NULL, "kbd_clk", "apb_clk", 0,
			SPEAR1340_PERIP2_CLK_ENB, SPEAR1340_KBD_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "e0300000.kbd");

	/* RAS clks */
	clk = clk_register_mux(NULL, "gen_syn0_1_mclk", gen_synth0_1_parents,
			ARRAY_SIZE(gen_synth0_1_parents),
			CLK_SET_RATE_NO_REPARENT, SPEAR1340_PLL_CFG,
			SPEAR1340_GEN_SYNT0_1_CLK_SHIFT,
			SPEAR1340_GEN_SYNT_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "gen_syn0_1_mclk", NULL);

	clk = clk_register_mux(NULL, "gen_syn2_3_mclk", gen_synth2_3_parents,
			ARRAY_SIZE(gen_synth2_3_parents),
			CLK_SET_RATE_NO_REPARENT, SPEAR1340_PLL_CFG,
			SPEAR1340_GEN_SYNT2_3_CLK_SHIFT,
			SPEAR1340_GEN_SYNT_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "gen_syn2_3_mclk", NULL);

	clk = clk_register_frac("gen_syn0_clk", "gen_syn0_1_mclk", 0,
			SPEAR1340_GEN_CLK_SYNT0, gen_rtbl, ARRAY_SIZE(gen_rtbl),
			&_lock);
	clk_register_clkdev(clk, "gen_syn0_clk", NULL);

	clk = clk_register_frac("gen_syn1_clk", "gen_syn0_1_mclk", 0,
			SPEAR1340_GEN_CLK_SYNT1, gen_rtbl, ARRAY_SIZE(gen_rtbl),
			&_lock);
	clk_register_clkdev(clk, "gen_syn1_clk", NULL);

	clk = clk_register_frac("gen_syn2_clk", "gen_syn2_3_mclk", 0,
			SPEAR1340_GEN_CLK_SYNT2, gen_rtbl, ARRAY_SIZE(gen_rtbl),
			&_lock);
	clk_register_clkdev(clk, "gen_syn2_clk", NULL);

	clk = clk_register_frac("gen_syn3_clk", "gen_syn2_3_mclk", 0,
			SPEAR1340_GEN_CLK_SYNT3, gen_rtbl, ARRAY_SIZE(gen_rtbl),
			&_lock);
	clk_register_clkdev(clk, "gen_syn3_clk", NULL);

	clk = clk_register_gate(NULL, "mali_clk", "gen_syn3_clk",
			CLK_SET_RATE_PARENT, SPEAR1340_PERIP3_CLK_ENB,
			SPEAR1340_MALI_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "mali");

	clk = clk_register_gate(NULL, "cec0_clk", "ahb_clk", 0,
			SPEAR1340_PERIP3_CLK_ENB, SPEAR1340_CEC0_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "spear_cec.0");

	clk = clk_register_gate(NULL, "cec1_clk", "ahb_clk", 0,
			SPEAR1340_PERIP3_CLK_ENB, SPEAR1340_CEC1_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "spear_cec.1");

	clk = clk_register_mux(NULL, "spdif_out_mclk", spdif_out_parents,
			ARRAY_SIZE(spdif_out_parents),
			CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			SPEAR1340_PERIP_CLK_CFG, SPEAR1340_SPDIF_OUT_CLK_SHIFT,
			SPEAR1340_SPDIF_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "spdif_out_mclk", NULL);

	clk = clk_register_gate(NULL, "spdif_out_clk", "spdif_out_mclk",
			CLK_SET_RATE_PARENT, SPEAR1340_PERIP3_CLK_ENB,
			SPEAR1340_SPDIF_OUT_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "d0000000.spdif-out");

	clk = clk_register_mux(NULL, "spdif_in_mclk", spdif_in_parents,
			ARRAY_SIZE(spdif_in_parents),
			CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			SPEAR1340_PERIP_CLK_CFG, SPEAR1340_SPDIF_IN_CLK_SHIFT,
			SPEAR1340_SPDIF_CLK_MASK, 0, &_lock);
	clk_register_clkdev(clk, "spdif_in_mclk", NULL);

	clk = clk_register_gate(NULL, "spdif_in_clk", "spdif_in_mclk",
			CLK_SET_RATE_PARENT, SPEAR1340_PERIP3_CLK_ENB,
			SPEAR1340_SPDIF_IN_CLK_ENB, 0, &_lock);
	clk_register_clkdev(clk, NULL, "d0100000.spdif-in");

	clk = clk_register_gate(NULL, "acp_clk", "ahb_clk", 0,
			SPEAR1340_PERIP2_CLK_ENB, SPEAR1340_ACP_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "acp_clk");

	clk = clk_register_gate(NULL, "plgpio_clk", "ahb_clk", 0,
			SPEAR1340_PERIP3_CLK_ENB, SPEAR1340_PLGPIO_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "e2800000.gpio");

	clk = clk_register_gate(NULL, "video_dec_clk", "ahb_clk", 0,
			SPEAR1340_PERIP3_CLK_ENB, SPEAR1340_VIDEO_DEC_CLK_ENB,
			0, &_lock);
	clk_register_clkdev(clk, NULL, "video_dec");

	clk = clk_register_gate(NULL, "video_enc_clk", "ahb_clk", 0,
			SPEAR1340_PERIP3_CLK_ENB, SPEAR1340_VIDEO_ENC_CLK_ENB,
			0, &_lock);
	clk_register_clkdev(clk, NULL, "video_enc");

	clk = clk_register_gate(NULL, "video_in_clk", "ahb_clk", 0,
			SPEAR1340_PERIP3_CLK_ENB, SPEAR1340_VIDEO_IN_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "spear_vip");

	clk = clk_register_gate(NULL, "cam0_clk", "ahb_clk", 0,
			SPEAR1340_PERIP3_CLK_ENB, SPEAR1340_CAM0_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "d0200000.cam0");

	clk = clk_register_gate(NULL, "cam1_clk", "ahb_clk", 0,
			SPEAR1340_PERIP3_CLK_ENB, SPEAR1340_CAM1_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "d0300000.cam1");

	clk = clk_register_gate(NULL, "cam2_clk", "ahb_clk", 0,
			SPEAR1340_PERIP3_CLK_ENB, SPEAR1340_CAM2_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "d0400000.cam2");

	clk = clk_register_gate(NULL, "cam3_clk", "ahb_clk", 0,
			SPEAR1340_PERIP3_CLK_ENB, SPEAR1340_CAM3_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "d0500000.cam3");

	clk = clk_register_gate(NULL, "pwm_clk", "ahb_clk", 0,
			SPEAR1340_PERIP3_CLK_ENB, SPEAR1340_PWM_CLK_ENB, 0,
			&_lock);
	clk_register_clkdev(clk, NULL, "e0180000.pwm");
}
