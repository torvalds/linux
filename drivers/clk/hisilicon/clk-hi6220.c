// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hisilicon Hi6220 clock driver
 *
 * Copyright (c) 2015 Hisilicon Limited.
 *
 * Author: Bintian Wang <bintian.wang@huawei.com>
 */

#include <linux/kernel.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <dt-bindings/clock/hi6220-clock.h>

#include "clk.h"


/* clocks in AO (always on) controller */
static struct hisi_fixed_rate_clock hi6220_fixed_rate_clks[] __initdata = {
	{ HI6220_REF32K,	"ref32k",	NULL, 0, 32764,     },
	{ HI6220_CLK_TCXO,	"clk_tcxo",	NULL, 0, 19200000,  },
	{ HI6220_MMC1_PAD,	"mmc1_pad",	NULL, 0, 100000000, },
	{ HI6220_MMC2_PAD,	"mmc2_pad",	NULL, 0, 100000000, },
	{ HI6220_MMC0_PAD,	"mmc0_pad",	NULL, 0, 200000000, },
	{ HI6220_PLL_BBP,	"bbppll0",	NULL, 0, 245760000, },
	{ HI6220_PLL_GPU,	"gpupll",	NULL, 0, 1000000000,},
	{ HI6220_PLL1_DDR,	"ddrpll1",	NULL, 0, 1066000000,},
	{ HI6220_PLL_SYS,	"syspll",	NULL, 0, 1190400000,},
	{ HI6220_PLL_SYS_MEDIA,	"media_syspll",	NULL, 0, 1190400000,},
	{ HI6220_DDR_SRC,	"ddr_sel_src",  NULL, 0, 1200000000,},
	{ HI6220_PLL_MEDIA,	"media_pll",    NULL, 0, 1440000000,},
	{ HI6220_PLL_DDR,	"ddrpll0",      NULL, 0, 1600000000,},
};

static struct hisi_fixed_factor_clock hi6220_fixed_factor_clks[] __initdata = {
	{ HI6220_300M,         "clk_300m",    "syspll",          1, 4, 0, },
	{ HI6220_150M,         "clk_150m",    "clk_300m",        1, 2, 0, },
	{ HI6220_PICOPHY_SRC,  "picophy_src", "clk_150m",        1, 4, 0, },
	{ HI6220_MMC0_SRC_SEL, "mmc0srcsel",  "mmc0_sel",        1, 8, 0, },
	{ HI6220_MMC1_SRC_SEL, "mmc1srcsel",  "mmc1_sel",        1, 8, 0, },
	{ HI6220_MMC2_SRC_SEL, "mmc2srcsel",  "mmc2_sel",        1, 8, 0, },
	{ HI6220_VPU_CODEC,    "vpucodec",    "codec_jpeg_aclk", 1, 2, 0, },
	{ HI6220_MMC0_SMP,     "mmc0_sample", "mmc0_sel",        1, 8, 0, },
	{ HI6220_MMC1_SMP,     "mmc1_sample", "mmc1_sel",        1, 8, 0, },
	{ HI6220_MMC2_SMP,     "mmc2_sample", "mmc2_sel",        1, 8, 0, },
};

static struct hisi_gate_clock hi6220_separated_gate_clks_ao[] __initdata = {
	{ HI6220_WDT0_PCLK,   "wdt0_pclk",   "ref32k",   CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x630, 12, 0, },
	{ HI6220_WDT1_PCLK,   "wdt1_pclk",   "ref32k",   CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x630, 13, 0, },
	{ HI6220_WDT2_PCLK,   "wdt2_pclk",   "ref32k",   CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x630, 14, 0, },
	{ HI6220_TIMER0_PCLK, "timer0_pclk", "clk_tcxo", CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x630, 15, 0, },
	{ HI6220_TIMER1_PCLK, "timer1_pclk", "clk_tcxo", CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x630, 16, 0, },
	{ HI6220_TIMER2_PCLK, "timer2_pclk", "clk_tcxo", CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x630, 17, 0, },
	{ HI6220_TIMER3_PCLK, "timer3_pclk", "clk_tcxo", CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x630, 18, 0, },
	{ HI6220_TIMER4_PCLK, "timer4_pclk", "clk_tcxo", CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x630, 19, 0, },
	{ HI6220_TIMER5_PCLK, "timer5_pclk", "clk_tcxo", CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x630, 20, 0, },
	{ HI6220_TIMER6_PCLK, "timer6_pclk", "clk_tcxo", CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x630, 21, 0, },
	{ HI6220_TIMER7_PCLK, "timer7_pclk", "clk_tcxo", CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x630, 22, 0, },
	{ HI6220_TIMER8_PCLK, "timer8_pclk", "clk_tcxo", CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x630, 23, 0, },
	{ HI6220_UART0_PCLK,  "uart0_pclk",  "clk_tcxo", CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x630, 24, 0, },
	{ HI6220_RTC0_PCLK,   "rtc0_pclk",   "clk_tcxo", CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x630, 25, 0, },
	{ HI6220_RTC1_PCLK,   "rtc1_pclk",   "clk_tcxo", CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x630, 26, 0, },
};

static void __init hi6220_clk_ao_init(struct device_node *np)
{
	struct hisi_clock_data *clk_data_ao;

	clk_data_ao = hisi_clk_init(np, HI6220_AO_NR_CLKS);
	if (!clk_data_ao)
		return;

	hisi_clk_register_fixed_rate(hi6220_fixed_rate_clks,
				ARRAY_SIZE(hi6220_fixed_rate_clks), clk_data_ao);

	hisi_clk_register_fixed_factor(hi6220_fixed_factor_clks,
				ARRAY_SIZE(hi6220_fixed_factor_clks), clk_data_ao);

	hisi_clk_register_gate_sep(hi6220_separated_gate_clks_ao,
				ARRAY_SIZE(hi6220_separated_gate_clks_ao), clk_data_ao);
}
/* Allow reset driver to probe as well */
CLK_OF_DECLARE_DRIVER(hi6220_clk_ao, "hisilicon,hi6220-aoctrl", hi6220_clk_ao_init);


/* clocks in sysctrl */
static const char *mmc0_mux0_p[] __initdata = { "pll_ddr_gate", "syspll", };
static const char *mmc0_mux1_p[] __initdata = { "mmc0_mux0", "pll_media_gate", };
static const char *mmc0_src_p[] __initdata = { "mmc0srcsel", "mmc0_div", };
static const char *mmc1_mux0_p[] __initdata = { "pll_ddr_gate", "syspll", };
static const char *mmc1_mux1_p[] __initdata = { "mmc1_mux0", "pll_media_gate", };
static const char *mmc1_src_p[]  __initdata = { "mmc1srcsel", "mmc1_div", };
static const char *mmc2_mux0_p[] __initdata = { "pll_ddr_gate", "syspll", };
static const char *mmc2_mux1_p[] __initdata = { "mmc2_mux0", "pll_media_gate", };
static const char *mmc2_src_p[]  __initdata = { "mmc2srcsel", "mmc2_div", };
static const char *mmc0_sample_in[] __initdata = { "mmc0_sample", "mmc0_pad", };
static const char *mmc1_sample_in[] __initdata = { "mmc1_sample", "mmc1_pad", };
static const char *mmc2_sample_in[] __initdata = { "mmc2_sample", "mmc2_pad", };
static const char *uart1_src[] __initdata = { "clk_tcxo", "clk_150m", };
static const char *uart2_src[] __initdata = { "clk_tcxo", "clk_150m", };
static const char *uart3_src[] __initdata = { "clk_tcxo", "clk_150m", };
static const char *uart4_src[] __initdata = { "clk_tcxo", "clk_150m", };
static const char *hifi_src[] __initdata = { "syspll", "pll_media_gate", };

static struct hisi_gate_clock hi6220_separated_gate_clks_sys[] __initdata = {
	{ HI6220_MMC0_CLK,      "mmc0_clk",      "mmc0_src",       CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x200, 0,  0, },
	{ HI6220_MMC0_CIUCLK,   "mmc0_ciuclk",   "mmc0_smp_in",    CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x200, 0,  0, },
	{ HI6220_MMC1_CLK,      "mmc1_clk",      "mmc1_src",       CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x200, 1,  0, },
	{ HI6220_MMC1_CIUCLK,   "mmc1_ciuclk",   "mmc1_smp_in",    CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x200, 1,  0, },
	{ HI6220_MMC2_CLK,      "mmc2_clk",      "mmc2_src",       CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x200, 2,  0, },
	{ HI6220_MMC2_CIUCLK,   "mmc2_ciuclk",   "mmc2_smp_in",    CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x200, 2,  0, },
	{ HI6220_USBOTG_HCLK,   "usbotg_hclk",   "clk_bus",        CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x200, 4,  0, },
	{ HI6220_CLK_PICOPHY,   "clk_picophy",   "cs_dapb",        CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x200, 5,  0, },
	{ HI6220_HIFI,          "hifi_clk",      "hifi_div",       CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x210, 0,  0, },
	{ HI6220_DACODEC_PCLK,  "dacodec_pclk",  "clk_bus",        CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x210, 5,  0, },
	{ HI6220_EDMAC_ACLK,    "edmac_aclk",    "clk_bus",        CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x220, 2,  0, },
	{ HI6220_CS_ATB,        "cs_atb",        "cs_atb_div",     CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x230, 0,  0, },
	{ HI6220_I2C0_CLK,      "i2c0_clk",      "clk_150m",       CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x230, 1,  0, },
	{ HI6220_I2C1_CLK,      "i2c1_clk",      "clk_150m",       CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x230, 2,  0, },
	{ HI6220_I2C2_CLK,      "i2c2_clk",      "clk_150m",       CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x230, 3,  0, },
	{ HI6220_I2C3_CLK,      "i2c3_clk",      "clk_150m",       CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x230, 4,  0, },
	{ HI6220_UART1_PCLK,    "uart1_pclk",    "uart1_src",      CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x230, 5,  0, },
	{ HI6220_UART2_PCLK,    "uart2_pclk",    "uart2_src",      CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x230, 6,  0, },
	{ HI6220_UART3_PCLK,    "uart3_pclk",    "uart3_src",      CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x230, 7,  0, },
	{ HI6220_UART4_PCLK,    "uart4_pclk",    "uart4_src",      CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x230, 8,  0, },
	{ HI6220_SPI_CLK,       "spi_clk",       "clk_150m",       CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x230, 9,  0, },
	{ HI6220_TSENSOR_CLK,   "tsensor_clk",   "clk_bus",        CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x230, 12, 0, },
	{ HI6220_DAPB_CLK,      "dapb_clk",      "cs_dapb",        CLK_SET_RATE_PARENT|CLK_IS_CRITICAL,   0x230, 18, 0, },
	{ HI6220_MMU_CLK,       "mmu_clk",       "ddrc_axi1",      CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x240, 11, 0, },
	{ HI6220_HIFI_SEL,      "hifi_sel",      "hifi_src",       CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x270, 0,  0, },
	{ HI6220_MMC0_SYSPLL,   "mmc0_syspll",   "syspll",         CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x270, 1,  0, },
	{ HI6220_MMC1_SYSPLL,   "mmc1_syspll",   "syspll",         CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x270, 2,  0, },
	{ HI6220_MMC2_SYSPLL,   "mmc2_syspll",   "syspll",         CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x270, 3,  0, },
	{ HI6220_MMC0_SEL,      "mmc0_sel",      "mmc0_mux1",      CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x270, 6,  0, },
	{ HI6220_MMC1_SEL,      "mmc1_sel",      "mmc1_mux1",      CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x270, 7,  0, },
	{ HI6220_BBPPLL_SEL,    "bbppll_sel",    "pll0_bbp_gate",  CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x270, 9,  0, },
	{ HI6220_MEDIA_PLL_SRC, "media_pll_src", "pll_media_gate", CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x270, 10, 0, },
	{ HI6220_MMC2_SEL,      "mmc2_sel",      "mmc2_mux1",      CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x270, 11, 0, },
	{ HI6220_CS_ATB_SYSPLL, "cs_atb_syspll", "syspll",         CLK_SET_RATE_PARENT|CLK_IS_CRITICAL,   0x270, 12, 0, },
};

static struct hisi_mux_clock hi6220_mux_clks_sys[] __initdata = {
	{ HI6220_MMC0_SRC,    "mmc0_src",    mmc0_src_p,     ARRAY_SIZE(mmc0_src_p),     CLK_SET_RATE_PARENT, 0x4,   0,  1, 0, },
	{ HI6220_MMC0_SMP_IN, "mmc0_smp_in", mmc0_sample_in, ARRAY_SIZE(mmc0_sample_in), CLK_SET_RATE_PARENT, 0x4,   0,  1, 0, },
	{ HI6220_MMC1_SRC,    "mmc1_src",    mmc1_src_p,     ARRAY_SIZE(mmc1_src_p),     CLK_SET_RATE_PARENT, 0x4,   2,  1, 0, },
	{ HI6220_MMC1_SMP_IN, "mmc1_smp_in", mmc1_sample_in, ARRAY_SIZE(mmc1_sample_in), CLK_SET_RATE_PARENT, 0x4,   2,  1, 0, },
	{ HI6220_MMC2_SRC,    "mmc2_src",    mmc2_src_p,     ARRAY_SIZE(mmc2_src_p),     CLK_SET_RATE_PARENT, 0x4,   4,  1, 0, },
	{ HI6220_MMC2_SMP_IN, "mmc2_smp_in", mmc2_sample_in, ARRAY_SIZE(mmc2_sample_in), CLK_SET_RATE_PARENT, 0x4,   4,  1, 0, },
	{ HI6220_HIFI_SRC,    "hifi_src",    hifi_src,       ARRAY_SIZE(hifi_src),       CLK_SET_RATE_PARENT, 0x400, 0,  1, CLK_MUX_HIWORD_MASK,},
	{ HI6220_UART1_SRC,   "uart1_src",   uart1_src,      ARRAY_SIZE(uart1_src),      CLK_SET_RATE_PARENT, 0x400, 1,  1, CLK_MUX_HIWORD_MASK,},
	{ HI6220_UART2_SRC,   "uart2_src",   uart2_src,      ARRAY_SIZE(uart2_src),      CLK_SET_RATE_PARENT, 0x400, 2,  1, CLK_MUX_HIWORD_MASK,},
	{ HI6220_UART3_SRC,   "uart3_src",   uart3_src,      ARRAY_SIZE(uart3_src),      CLK_SET_RATE_PARENT, 0x400, 3,  1, CLK_MUX_HIWORD_MASK,},
	{ HI6220_UART4_SRC,   "uart4_src",   uart4_src,      ARRAY_SIZE(uart4_src),      CLK_SET_RATE_PARENT, 0x400, 4,  1, CLK_MUX_HIWORD_MASK,},
	{ HI6220_MMC0_MUX0,   "mmc0_mux0",   mmc0_mux0_p,    ARRAY_SIZE(mmc0_mux0_p),    CLK_SET_RATE_PARENT, 0x400, 5,  1, CLK_MUX_HIWORD_MASK,},
	{ HI6220_MMC1_MUX0,   "mmc1_mux0",   mmc1_mux0_p,    ARRAY_SIZE(mmc1_mux0_p),    CLK_SET_RATE_PARENT, 0x400, 11, 1, CLK_MUX_HIWORD_MASK,},
	{ HI6220_MMC2_MUX0,   "mmc2_mux0",   mmc2_mux0_p,    ARRAY_SIZE(mmc2_mux0_p),    CLK_SET_RATE_PARENT, 0x400, 12, 1, CLK_MUX_HIWORD_MASK,},
	{ HI6220_MMC0_MUX1,   "mmc0_mux1",   mmc0_mux1_p,    ARRAY_SIZE(mmc0_mux1_p),    CLK_SET_RATE_PARENT, 0x400, 13, 1, CLK_MUX_HIWORD_MASK,},
	{ HI6220_MMC1_MUX1,   "mmc1_mux1",   mmc1_mux1_p,    ARRAY_SIZE(mmc1_mux1_p),    CLK_SET_RATE_PARENT, 0x400, 14, 1, CLK_MUX_HIWORD_MASK,},
	{ HI6220_MMC2_MUX1,   "mmc2_mux1",   mmc2_mux1_p,    ARRAY_SIZE(mmc2_mux1_p),    CLK_SET_RATE_PARENT, 0x400, 15, 1, CLK_MUX_HIWORD_MASK,},
};

static struct hi6220_divider_clock hi6220_div_clks_sys[] __initdata = {
	{ HI6220_CLK_BUS,     "clk_bus",     "clk_300m",      CLK_SET_RATE_PARENT, 0x490, 0,  4, 7, },
	{ HI6220_MMC0_DIV,    "mmc0_div",    "mmc0_syspll",   CLK_SET_RATE_PARENT, 0x494, 0,  6, 7, },
	{ HI6220_MMC1_DIV,    "mmc1_div",    "mmc1_syspll",   CLK_SET_RATE_PARENT, 0x498, 0,  6, 7, },
	{ HI6220_MMC2_DIV,    "mmc2_div",    "mmc2_syspll",   CLK_SET_RATE_PARENT, 0x49c, 0,  6, 7, },
	{ HI6220_HIFI_DIV,    "hifi_div",    "hifi_sel",      CLK_SET_RATE_PARENT, 0x4a0, 0,  4, 7, },
	{ HI6220_BBPPLL0_DIV, "bbppll0_div", "bbppll_sel",    CLK_SET_RATE_PARENT, 0x4a0, 8,  6, 15,},
	{ HI6220_CS_DAPB,     "cs_dapb",     "picophy_src",   CLK_SET_RATE_PARENT, 0x4a0, 24, 2, 31,},
	{ HI6220_CS_ATB_DIV,  "cs_atb_div",  "cs_atb_syspll", CLK_SET_RATE_PARENT, 0x4a4, 0,  4, 7, },
};

static void __init hi6220_clk_sys_init(struct device_node *np)
{
	struct hisi_clock_data *clk_data;

	clk_data = hisi_clk_init(np, HI6220_SYS_NR_CLKS);
	if (!clk_data)
		return;

	hisi_clk_register_gate_sep(hi6220_separated_gate_clks_sys,
			ARRAY_SIZE(hi6220_separated_gate_clks_sys), clk_data);

	hisi_clk_register_mux(hi6220_mux_clks_sys,
			ARRAY_SIZE(hi6220_mux_clks_sys), clk_data);

	hi6220_clk_register_divider(hi6220_div_clks_sys,
			ARRAY_SIZE(hi6220_div_clks_sys), clk_data);
}
CLK_OF_DECLARE_DRIVER(hi6220_clk_sys, "hisilicon,hi6220-sysctrl", hi6220_clk_sys_init);


/* clocks in media controller */
static const char *clk_1000_1200_src[] __initdata = { "pll_gpu_gate", "media_syspll_src", };
static const char *clk_1440_1200_src[] __initdata = { "media_syspll_src", "media_pll_src", };
static const char *clk_1000_1440_src[] __initdata = { "pll_gpu_gate", "media_pll_src", };

static struct hisi_gate_clock hi6220_separated_gate_clks_media[] __initdata = {
	{ HI6220_DSI_PCLK,       "dsi_pclk",         "vpucodec",      CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x520, 0,  0, },
	{ HI6220_G3D_PCLK,       "g3d_pclk",         "vpucodec",      CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x520, 1,  0, },
	{ HI6220_ACLK_CODEC_VPU, "aclk_codec_vpu",   "ade_core_src",  CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x520, 3,  0, },
	{ HI6220_ISP_SCLK,       "isp_sclk",         "isp_sclk_src",  CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x520, 5,  0, },
	{ HI6220_ADE_CORE,	 "ade_core",	     "ade_core_src",  CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x520, 6,  0, },
	{ HI6220_MED_MMU,        "media_mmu",        "mmu_clk",       CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x520, 8,  0, },
	{ HI6220_CFG_CSI4PHY,    "cfg_csi4phy",      "clk_tcxo",      CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x520, 9,  0, },
	{ HI6220_CFG_CSI2PHY,    "cfg_csi2phy",      "clk_tcxo",      CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x520, 10, 0, },
	{ HI6220_ISP_SCLK_GATE,  "isp_sclk_gate",    "media_pll_src", CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x520, 11, 0, },
	{ HI6220_ISP_SCLK_GATE1, "isp_sclk_gate1",   "media_pll_src", CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x520, 12, 0, },
	{ HI6220_ADE_CORE_GATE,  "ade_core_gate",    "media_pll_src", CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x520, 14, 0, },
	{ HI6220_CODEC_VPU_GATE, "codec_vpu_gate",   "clk_1000_1440", CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x520, 15, 0, },
	{ HI6220_MED_SYSPLL,     "media_syspll_src", "media_syspll",  CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x520, 17, 0, },
};

static struct hisi_mux_clock hi6220_mux_clks_media[] __initdata = {
	{ HI6220_1440_1200, "clk_1440_1200", clk_1440_1200_src, ARRAY_SIZE(clk_1440_1200_src), CLK_SET_RATE_PARENT, 0x51c, 0, 1, 0, },
	{ HI6220_1000_1200, "clk_1000_1200", clk_1000_1200_src, ARRAY_SIZE(clk_1000_1200_src), CLK_SET_RATE_PARENT, 0x51c, 1, 1, 0, },
	{ HI6220_1000_1440, "clk_1000_1440", clk_1000_1440_src, ARRAY_SIZE(clk_1000_1440_src), CLK_SET_RATE_PARENT, 0x51c, 6, 1, 0, },
};

static struct hi6220_divider_clock hi6220_div_clks_media[] __initdata = {
	{ HI6220_CODEC_JPEG,    "codec_jpeg_aclk", "media_pll_src",  CLK_SET_RATE_PARENT, 0xcbc, 0,  4, 23, },
	{ HI6220_ISP_SCLK_SRC,  "isp_sclk_src",    "isp_sclk_gate",  CLK_SET_RATE_PARENT, 0xcbc, 8,  4, 15, },
	{ HI6220_ISP_SCLK1,     "isp_sclk1",       "isp_sclk_gate1", CLK_SET_RATE_PARENT, 0xcbc, 24, 4, 31, },
	{ HI6220_ADE_CORE_SRC,  "ade_core_src",    "ade_core_gate",  CLK_SET_RATE_PARENT, 0xcc0, 16, 3, 23, },
	{ HI6220_ADE_PIX_SRC,   "ade_pix_src",     "clk_1440_1200",  CLK_SET_RATE_PARENT, 0xcc0, 24, 6, 31, },
	{ HI6220_G3D_CLK,       "g3d_clk",         "clk_1000_1200",  CLK_SET_RATE_PARENT, 0xcc4, 8,  4, 15, },
	{ HI6220_CODEC_VPU_SRC, "codec_vpu_src",   "codec_vpu_gate", CLK_SET_RATE_PARENT, 0xcc4, 24, 6, 31, },
};

static void __init hi6220_clk_media_init(struct device_node *np)
{
	struct hisi_clock_data *clk_data;

	clk_data = hisi_clk_init(np, HI6220_MEDIA_NR_CLKS);
	if (!clk_data)
		return;

	hisi_clk_register_gate_sep(hi6220_separated_gate_clks_media,
				ARRAY_SIZE(hi6220_separated_gate_clks_media), clk_data);

	hisi_clk_register_mux(hi6220_mux_clks_media,
				ARRAY_SIZE(hi6220_mux_clks_media), clk_data);

	hi6220_clk_register_divider(hi6220_div_clks_media,
				ARRAY_SIZE(hi6220_div_clks_media), clk_data);
}
CLK_OF_DECLARE_DRIVER(hi6220_clk_media, "hisilicon,hi6220-mediactrl", hi6220_clk_media_init);


/* clocks in pmctrl */
static struct hisi_gate_clock hi6220_gate_clks_power[] __initdata = {
	{ HI6220_PLL_GPU_GATE,   "pll_gpu_gate",   "gpupll",    CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x8,  0,  0, },
	{ HI6220_PLL1_DDR_GATE,  "pll1_ddr_gate",  "ddrpll1",   CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x10, 0,  0, },
	{ HI6220_PLL_DDR_GATE,   "pll_ddr_gate",   "ddrpll0",   CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x18, 0,  0, },
	{ HI6220_PLL_MEDIA_GATE, "pll_media_gate", "media_pll", CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x38, 0,  0, },
	{ HI6220_PLL0_BBP_GATE,  "pll0_bbp_gate",  "bbppll0",   CLK_SET_RATE_PARENT|CLK_IGNORE_UNUSED, 0x48, 0,  0, },
};

static struct hi6220_divider_clock hi6220_div_clks_power[] __initdata = {
	{ HI6220_DDRC_SRC,  "ddrc_src",  "ddr_sel_src", CLK_SET_RATE_PARENT, 0x5a8, 0, 4, 0, },
	{ HI6220_DDRC_AXI1, "ddrc_axi1", "ddrc_src",    CLK_SET_RATE_PARENT, 0x5a8, 8, 2, 0, },
};

static void __init hi6220_clk_power_init(struct device_node *np)
{
	struct hisi_clock_data *clk_data;

	clk_data = hisi_clk_init(np, HI6220_POWER_NR_CLKS);
	if (!clk_data)
		return;

	hisi_clk_register_gate(hi6220_gate_clks_power,
				ARRAY_SIZE(hi6220_gate_clks_power), clk_data);

	hi6220_clk_register_divider(hi6220_div_clks_power,
				ARRAY_SIZE(hi6220_div_clks_power), clk_data);
}
CLK_OF_DECLARE(hi6220_clk_power, "hisilicon,hi6220-pmctrl", hi6220_clk_power_init);

/* clocks in acpu */
static const struct hisi_gate_clock hi6220_acpu_sc_gate_sep_clks[] = {
	{ HI6220_ACPU_SFT_AT_S, "sft_at_s", "cs_atb",
	  CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0xc, 11, 0, },
};

static void __init hi6220_clk_acpu_init(struct device_node *np)
{
	struct hisi_clock_data *clk_data;
	int nr = ARRAY_SIZE(hi6220_acpu_sc_gate_sep_clks);

	clk_data = hisi_clk_init(np, nr);
	if (!clk_data)
		return;

	hisi_clk_register_gate_sep(hi6220_acpu_sc_gate_sep_clks,
				   ARRAY_SIZE(hi6220_acpu_sc_gate_sep_clks),
				   clk_data);
}

CLK_OF_DECLARE(hi6220_clk_acpu, "hisilicon,hi6220-acpu-sctrl", hi6220_clk_acpu_init);
