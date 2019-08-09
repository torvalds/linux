// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2014 Linaro Ltd.
 * Copyright (C) 2014 ZTE Corporation.
 */

#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <dt-bindings/clock/zx296702-clock.h>
#include "clk.h"

static DEFINE_SPINLOCK(reg_lock);

static void __iomem *topcrm_base;
static void __iomem *lsp0crpm_base;
static void __iomem *lsp1crpm_base;

static struct clk *topclk[ZX296702_TOPCLK_END];
static struct clk *lsp0clk[ZX296702_LSP0CLK_END];
static struct clk *lsp1clk[ZX296702_LSP1CLK_END];

static struct clk_onecell_data topclk_data;
static struct clk_onecell_data lsp0clk_data;
static struct clk_onecell_data lsp1clk_data;

#define CLK_MUX			(topcrm_base + 0x04)
#define CLK_DIV			(topcrm_base + 0x08)
#define CLK_EN0			(topcrm_base + 0x0c)
#define CLK_EN1			(topcrm_base + 0x10)
#define VOU_LOCAL_CLKEN		(topcrm_base + 0x68)
#define VOU_LOCAL_CLKSEL	(topcrm_base + 0x70)
#define VOU_LOCAL_DIV2_SET	(topcrm_base + 0x74)
#define CLK_MUX1		(topcrm_base + 0x8c)

#define CLK_SDMMC1		(lsp0crpm_base + 0x0c)
#define CLK_GPIO		(lsp0crpm_base + 0x2c)
#define CLK_SPDIF0		(lsp0crpm_base + 0x10)
#define SPDIF0_DIV		(lsp0crpm_base + 0x14)
#define CLK_I2S0		(lsp0crpm_base + 0x18)
#define I2S0_DIV		(lsp0crpm_base + 0x1c)
#define CLK_I2S1		(lsp0crpm_base + 0x20)
#define I2S1_DIV		(lsp0crpm_base + 0x24)
#define CLK_I2S2		(lsp0crpm_base + 0x34)
#define I2S2_DIV		(lsp0crpm_base + 0x38)

#define CLK_UART0		(lsp1crpm_base + 0x20)
#define CLK_UART1		(lsp1crpm_base + 0x24)
#define CLK_SDMMC0		(lsp1crpm_base + 0x2c)
#define CLK_SPDIF1		(lsp1crpm_base + 0x30)
#define SPDIF1_DIV		(lsp1crpm_base + 0x34)

static const struct zx_pll_config pll_a9_config[] = {
	{ .rate = 700000000, .cfg0 = 0x800405d1, .cfg1 = 0x04555555 },
	{ .rate = 800000000, .cfg0 = 0x80040691, .cfg1 = 0x04aaaaaa },
	{ .rate = 900000000, .cfg0 = 0x80040791, .cfg1 = 0x04000000 },
	{ .rate = 1000000000, .cfg0 = 0x80040851, .cfg1 = 0x04555555 },
	{ .rate = 1100000000, .cfg0 = 0x80040911, .cfg1 = 0x04aaaaaa },
	{ .rate = 1200000000, .cfg0 = 0x80040a11, .cfg1 = 0x04000000 },
};

static const struct clk_div_table main_hlk_div[] = {
	{ .val = 1, .div = 2, },
	{ .val = 3, .div = 4, },
	{ /* sentinel */ }
};

static const struct clk_div_table a9_as1_aclk_divider[] = {
	{ .val = 0, .div = 1, },
	{ .val = 1, .div = 2, },
	{ .val = 3, .div = 4, },
	{ /* sentinel */ }
};

static const struct clk_div_table sec_wclk_divider[] = {
	{ .val = 0, .div = 1, },
	{ .val = 1, .div = 2, },
	{ .val = 3, .div = 4, },
	{ .val = 5, .div = 6, },
	{ .val = 7, .div = 8, },
	{ /* sentinel */ }
};

static const char * const matrix_aclk_sel[] = {
	"pll_mm0_198M",
	"osc",
	"clk_148M5",
	"pll_lsp_104M",
};

static const char * const a9_wclk_sel[] = {
	"pll_a9",
	"osc",
	"clk_500",
	"clk_250",
};

static const char * const a9_as1_aclk_sel[] = {
	"clk_250",
	"osc",
	"pll_mm0_396M",
	"pll_mac_333M",
};

static const char * const a9_trace_clkin_sel[] = {
	"clk_74M25",
	"pll_mm1_108M",
	"clk_125",
	"clk_148M5",
};

static const char * const decppu_aclk_sel[] = {
	"clk_250",
	"pll_mm0_198M",
	"pll_lsp_104M",
	"pll_audio_294M912",
};

static const char * const vou_main_wclk_sel[] = {
	"clk_148M5",
	"clk_74M25",
	"clk_27",
	"pll_mm1_54M",
};

static const char * const vou_scaler_wclk_sel[] = {
	"clk_250",
	"pll_mac_333M",
	"pll_audio_294M912",
	"pll_mm0_198M",
};

static const char * const r2d_wclk_sel[] = {
	"pll_audio_294M912",
	"pll_mac_333M",
	"pll_a9_350M",
	"pll_mm0_396M",
};

static const char * const ddr_wclk_sel[] = {
	"pll_mac_333M",
	"pll_ddr_266M",
	"pll_audio_294M912",
	"pll_mm0_198M",
};

static const char * const nand_wclk_sel[] = {
	"pll_lsp_104M",
	"osc",
};

static const char * const lsp_26_wclk_sel[] = {
	"pll_lsp_26M",
	"osc",
};

static const char * const vl0_sel[] = {
	"vou_main_channel_div",
	"vou_aux_channel_div",
};

static const char * const hdmi_sel[] = {
	"vou_main_channel_wclk",
	"vou_aux_channel_wclk",
};

static const char * const sdmmc0_wclk_sel[] = {
	"lsp1_104M_wclk",
	"lsp1_26M_wclk",
};

static const char * const sdmmc1_wclk_sel[] = {
	"lsp0_104M_wclk",
	"lsp0_26M_wclk",
};

static const char * const uart_wclk_sel[] = {
	"lsp1_104M_wclk",
	"lsp1_26M_wclk",
};

static const char * const spdif0_wclk_sel[] = {
	"lsp0_104M_wclk",
	"lsp0_26M_wclk",
};

static const char * const spdif1_wclk_sel[] = {
	"lsp1_104M_wclk",
	"lsp1_26M_wclk",
};

static const char * const i2s_wclk_sel[] = {
	"lsp0_104M_wclk",
	"lsp0_26M_wclk",
};

static inline struct clk *zx_divtbl(const char *name, const char *parent,
				    void __iomem *reg, u8 shift, u8 width,
				    const struct clk_div_table *table)
{
	return clk_register_divider_table(NULL, name, parent, 0, reg, shift,
					  width, 0, table, &reg_lock);
}

static inline struct clk *zx_div(const char *name, const char *parent,
				 void __iomem *reg, u8 shift, u8 width)
{
	return clk_register_divider(NULL, name, parent, 0,
				    reg, shift, width, 0, &reg_lock);
}

static inline struct clk *zx_mux(const char *name, const char * const *parents,
		int num_parents, void __iomem *reg, u8 shift, u8 width)
{
	return clk_register_mux(NULL, name, parents, num_parents,
				0, reg, shift, width, 0, &reg_lock);
}

static inline struct clk *zx_gate(const char *name, const char *parent,
				  void __iomem *reg, u8 shift)
{
	return clk_register_gate(NULL, name, parent, CLK_IGNORE_UNUSED,
				 reg, shift, CLK_SET_RATE_PARENT, &reg_lock);
}

static void __init zx296702_top_clocks_init(struct device_node *np)
{
	struct clk **clk = topclk;
	int i;

	topcrm_base = of_iomap(np, 0);
	WARN_ON(!topcrm_base);

	clk[ZX296702_OSC] =
		clk_register_fixed_rate(NULL, "osc", NULL, 0, 30000000);
	clk[ZX296702_PLL_A9] =
		clk_register_zx_pll("pll_a9", "osc", 0, topcrm_base
				+ 0x01c, pll_a9_config,
				ARRAY_SIZE(pll_a9_config), &reg_lock);

	/* TODO: pll_a9_350M look like changeble follow a9 pll */
	clk[ZX296702_PLL_A9_350M] =
		clk_register_fixed_rate(NULL, "pll_a9_350M", "osc", 0,
				350000000);
	clk[ZX296702_PLL_MAC_1000M] =
		clk_register_fixed_rate(NULL, "pll_mac_1000M", "osc", 0,
				1000000000);
	clk[ZX296702_PLL_MAC_333M] =
		clk_register_fixed_rate(NULL, "pll_mac_333M",	 "osc", 0,
				333000000);
	clk[ZX296702_PLL_MM0_1188M] =
		clk_register_fixed_rate(NULL, "pll_mm0_1188M", "osc", 0,
				1188000000);
	clk[ZX296702_PLL_MM0_396M] =
		clk_register_fixed_rate(NULL, "pll_mm0_396M",  "osc", 0,
				396000000);
	clk[ZX296702_PLL_MM0_198M] =
		clk_register_fixed_rate(NULL, "pll_mm0_198M",  "osc", 0,
				198000000);
	clk[ZX296702_PLL_MM1_108M] =
		clk_register_fixed_rate(NULL, "pll_mm1_108M",  "osc", 0,
				108000000);
	clk[ZX296702_PLL_MM1_72M] =
		clk_register_fixed_rate(NULL, "pll_mm1_72M",	 "osc", 0,
				72000000);
	clk[ZX296702_PLL_MM1_54M] =
		clk_register_fixed_rate(NULL, "pll_mm1_54M",	 "osc", 0,
				54000000);
	clk[ZX296702_PLL_LSP_104M] =
		clk_register_fixed_rate(NULL, "pll_lsp_104M",  "osc", 0,
				104000000);
	clk[ZX296702_PLL_LSP_26M] =
		clk_register_fixed_rate(NULL, "pll_lsp_26M",	 "osc", 0,
				26000000);
	clk[ZX296702_PLL_DDR_266M] =
		clk_register_fixed_rate(NULL, "pll_ddr_266M",	 "osc", 0,
				266000000);
	clk[ZX296702_PLL_AUDIO_294M912] =
		clk_register_fixed_rate(NULL, "pll_audio_294M912", "osc", 0,
				294912000);

	/* bus clock */
	clk[ZX296702_MATRIX_ACLK] =
		zx_mux("matrix_aclk", matrix_aclk_sel,
				ARRAY_SIZE(matrix_aclk_sel), CLK_MUX, 2, 2);
	clk[ZX296702_MAIN_HCLK] =
		zx_divtbl("main_hclk", "matrix_aclk", CLK_DIV, 0, 2,
				main_hlk_div);
	clk[ZX296702_MAIN_PCLK] =
		zx_divtbl("main_pclk", "matrix_aclk", CLK_DIV, 2, 2,
				main_hlk_div);

	/* cpu clock */
	clk[ZX296702_CLK_500] =
		clk_register_fixed_factor(NULL, "clk_500", "pll_mac_1000M", 0,
				1, 2);
	clk[ZX296702_CLK_250] =
		clk_register_fixed_factor(NULL, "clk_250", "pll_mac_1000M", 0,
				1, 4);
	clk[ZX296702_CLK_125] =
		clk_register_fixed_factor(NULL, "clk_125", "clk_250", 0, 1, 2);
	clk[ZX296702_CLK_148M5] =
		clk_register_fixed_factor(NULL, "clk_148M5", "pll_mm0_1188M", 0,
				1, 8);
	clk[ZX296702_CLK_74M25] =
		clk_register_fixed_factor(NULL, "clk_74M25", "pll_mm0_1188M", 0,
				1, 16);
	clk[ZX296702_A9_WCLK] =
		zx_mux("a9_wclk", a9_wclk_sel, ARRAY_SIZE(a9_wclk_sel), CLK_MUX,
				0, 2);
	clk[ZX296702_A9_AS1_ACLK_MUX] =
		zx_mux("a9_as1_aclk_mux", a9_as1_aclk_sel,
				ARRAY_SIZE(a9_as1_aclk_sel), CLK_MUX, 4, 2);
	clk[ZX296702_A9_TRACE_CLKIN_MUX] =
		zx_mux("a9_trace_clkin_mux", a9_trace_clkin_sel,
				ARRAY_SIZE(a9_trace_clkin_sel), CLK_MUX1, 0, 2);
	clk[ZX296702_A9_AS1_ACLK_DIV] =
		zx_divtbl("a9_as1_aclk_div", "a9_as1_aclk_mux", CLK_DIV, 4, 2,
				a9_as1_aclk_divider);

	/* multi-media clock */
	clk[ZX296702_CLK_2] =
		clk_register_fixed_factor(NULL, "clk_2", "pll_mm1_72M", 0,
				1, 36);
	clk[ZX296702_CLK_27] =
		clk_register_fixed_factor(NULL, "clk_27", "pll_mm1_54M", 0,
				1, 2);
	clk[ZX296702_DECPPU_ACLK_MUX] =
		zx_mux("decppu_aclk_mux", decppu_aclk_sel,
				ARRAY_SIZE(decppu_aclk_sel), CLK_MUX, 6, 2);
	clk[ZX296702_PPU_ACLK_MUX] =
		zx_mux("ppu_aclk_mux", decppu_aclk_sel,
				ARRAY_SIZE(decppu_aclk_sel), CLK_MUX, 8, 2);
	clk[ZX296702_MALI400_ACLK_MUX] =
		zx_mux("mali400_aclk_mux", decppu_aclk_sel,
				ARRAY_SIZE(decppu_aclk_sel), CLK_MUX, 12, 2);
	clk[ZX296702_VOU_ACLK_MUX] =
		zx_mux("vou_aclk_mux", decppu_aclk_sel,
				ARRAY_SIZE(decppu_aclk_sel), CLK_MUX, 10, 2);
	clk[ZX296702_VOU_MAIN_WCLK_MUX] =
		zx_mux("vou_main_wclk_mux", vou_main_wclk_sel,
				ARRAY_SIZE(vou_main_wclk_sel), CLK_MUX, 14, 2);
	clk[ZX296702_VOU_AUX_WCLK_MUX] =
		zx_mux("vou_aux_wclk_mux", vou_main_wclk_sel,
				ARRAY_SIZE(vou_main_wclk_sel), CLK_MUX, 16, 2);
	clk[ZX296702_VOU_SCALER_WCLK_MUX] =
		zx_mux("vou_scaler_wclk_mux", vou_scaler_wclk_sel,
				ARRAY_SIZE(vou_scaler_wclk_sel), CLK_MUX,
				18, 2);
	clk[ZX296702_R2D_ACLK_MUX] =
		zx_mux("r2d_aclk_mux", decppu_aclk_sel,
				ARRAY_SIZE(decppu_aclk_sel), CLK_MUX, 20, 2);
	clk[ZX296702_R2D_WCLK_MUX] =
		zx_mux("r2d_wclk_mux", r2d_wclk_sel,
				ARRAY_SIZE(r2d_wclk_sel), CLK_MUX, 22, 2);

	/* other clock */
	clk[ZX296702_CLK_50] =
		clk_register_fixed_factor(NULL, "clk_50", "pll_mac_1000M",
				0, 1, 20);
	clk[ZX296702_CLK_25] =
		clk_register_fixed_factor(NULL, "clk_25", "pll_mac_1000M",
				0, 1, 40);
	clk[ZX296702_CLK_12] =
		clk_register_fixed_factor(NULL, "clk_12", "pll_mm1_72M",
				0, 1, 6);
	clk[ZX296702_CLK_16M384] =
		clk_register_fixed_factor(NULL, "clk_16M384",
				"pll_audio_294M912", 0, 1, 18);
	clk[ZX296702_CLK_32K768] =
		clk_register_fixed_factor(NULL, "clk_32K768", "clk_16M384",
				0, 1, 500);
	clk[ZX296702_SEC_WCLK_DIV] =
		zx_divtbl("sec_wclk_div", "pll_lsp_104M", CLK_DIV, 6, 3,
				sec_wclk_divider);
	clk[ZX296702_DDR_WCLK_MUX] =
		zx_mux("ddr_wclk_mux", ddr_wclk_sel,
				ARRAY_SIZE(ddr_wclk_sel), CLK_MUX, 24, 2);
	clk[ZX296702_NAND_WCLK_MUX] =
		zx_mux("nand_wclk_mux", nand_wclk_sel,
				ARRAY_SIZE(nand_wclk_sel), CLK_MUX, 24, 2);
	clk[ZX296702_LSP_26_WCLK_MUX] =
		zx_mux("lsp_26_wclk_mux", lsp_26_wclk_sel,
				ARRAY_SIZE(lsp_26_wclk_sel), CLK_MUX, 27, 1);

	/* gates */
	clk[ZX296702_A9_AS0_ACLK] =
		zx_gate("a9_as0_aclk",	"matrix_aclk",		CLK_EN0, 0);
	clk[ZX296702_A9_AS1_ACLK] =
		zx_gate("a9_as1_aclk",	"a9_as1_aclk_div",	CLK_EN0, 1);
	clk[ZX296702_A9_TRACE_CLKIN] =
		zx_gate("a9_trace_clkin", "a9_trace_clkin_mux",	CLK_EN0, 2);
	clk[ZX296702_DECPPU_AXI_M_ACLK] =
		zx_gate("decppu_axi_m_aclk", "decppu_aclk_mux", CLK_EN0, 3);
	clk[ZX296702_DECPPU_AHB_S_HCLK] =
		zx_gate("decppu_ahb_s_hclk",	"main_hclk",	CLK_EN0, 4);
	clk[ZX296702_PPU_AXI_M_ACLK] =
		zx_gate("ppu_axi_m_aclk",	"ppu_aclk_mux",	CLK_EN0, 5);
	clk[ZX296702_PPU_AHB_S_HCLK] =
		zx_gate("ppu_ahb_s_hclk",	"main_hclk",	CLK_EN0, 6);
	clk[ZX296702_VOU_AXI_M_ACLK] =
		zx_gate("vou_axi_m_aclk",	"vou_aclk_mux",	CLK_EN0, 7);
	clk[ZX296702_VOU_APB_PCLK] =
		zx_gate("vou_apb_pclk",	"main_pclk",		CLK_EN0, 8);
	clk[ZX296702_VOU_MAIN_CHANNEL_WCLK] =
		zx_gate("vou_main_channel_wclk", "vou_main_wclk_mux",
				CLK_EN0, 9);
	clk[ZX296702_VOU_AUX_CHANNEL_WCLK] =
		zx_gate("vou_aux_channel_wclk", "vou_aux_wclk_mux",
				CLK_EN0, 10);
	clk[ZX296702_VOU_HDMI_OSCLK_CEC] =
		zx_gate("vou_hdmi_osclk_cec", "clk_2",		CLK_EN0, 11);
	clk[ZX296702_VOU_SCALER_WCLK] =
		zx_gate("vou_scaler_wclk", "vou_scaler_wclk_mux", CLK_EN0, 12);
	clk[ZX296702_MALI400_AXI_M_ACLK] =
		zx_gate("mali400_axi_m_aclk", "mali400_aclk_mux", CLK_EN0, 13);
	clk[ZX296702_MALI400_APB_PCLK] =
		zx_gate("mali400_apb_pclk",	"main_pclk",	CLK_EN0, 14);
	clk[ZX296702_R2D_WCLK] =
		zx_gate("r2d_wclk",		"r2d_wclk_mux",	CLK_EN0, 15);
	clk[ZX296702_R2D_AXI_M_ACLK] =
		zx_gate("r2d_axi_m_aclk",	"r2d_aclk_mux",	CLK_EN0, 16);
	clk[ZX296702_R2D_AHB_HCLK] =
		zx_gate("r2d_ahb_hclk",		"main_hclk",	CLK_EN0, 17);
	clk[ZX296702_DDR3_AXI_S0_ACLK] =
		zx_gate("ddr3_axi_s0_aclk",	"matrix_aclk",	CLK_EN0, 18);
	clk[ZX296702_DDR3_APB_PCLK] =
		zx_gate("ddr3_apb_pclk",	"main_pclk",	CLK_EN0, 19);
	clk[ZX296702_DDR3_WCLK] =
		zx_gate("ddr3_wclk",		"ddr_wclk_mux",	CLK_EN0, 20);
	clk[ZX296702_USB20_0_AHB_HCLK] =
		zx_gate("usb20_0_ahb_hclk",	"main_hclk",	CLK_EN0, 21);
	clk[ZX296702_USB20_0_EXTREFCLK] =
		zx_gate("usb20_0_extrefclk",	"clk_12",	CLK_EN0, 22);
	clk[ZX296702_USB20_1_AHB_HCLK] =
		zx_gate("usb20_1_ahb_hclk",	"main_hclk",	CLK_EN0, 23);
	clk[ZX296702_USB20_1_EXTREFCLK] =
		zx_gate("usb20_1_extrefclk",	"clk_12",	CLK_EN0, 24);
	clk[ZX296702_USB20_2_AHB_HCLK] =
		zx_gate("usb20_2_ahb_hclk",	"main_hclk",	CLK_EN0, 25);
	clk[ZX296702_USB20_2_EXTREFCLK] =
		zx_gate("usb20_2_extrefclk",	"clk_12",	CLK_EN0, 26);
	clk[ZX296702_GMAC_AXI_M_ACLK] =
		zx_gate("gmac_axi_m_aclk",	"matrix_aclk",	CLK_EN0, 27);
	clk[ZX296702_GMAC_APB_PCLK] =
		zx_gate("gmac_apb_pclk",	"main_pclk",	CLK_EN0, 28);
	clk[ZX296702_GMAC_125_CLKIN] =
		zx_gate("gmac_125_clkin",	"clk_125",	CLK_EN0, 29);
	clk[ZX296702_GMAC_RMII_CLKIN] =
		zx_gate("gmac_rmii_clkin",	"clk_50",	CLK_EN0, 30);
	clk[ZX296702_GMAC_25M_CLK] =
		zx_gate("gmac_25M_clk",		"clk_25",	CLK_EN0, 31);
	clk[ZX296702_NANDFLASH_AHB_HCLK] =
		zx_gate("nandflash_ahb_hclk", "main_hclk",	CLK_EN1, 0);
	clk[ZX296702_NANDFLASH_WCLK] =
		zx_gate("nandflash_wclk",     "nand_wclk_mux",	CLK_EN1, 1);
	clk[ZX296702_LSP0_APB_PCLK] =
		zx_gate("lsp0_apb_pclk",	"main_pclk",	CLK_EN1, 2);
	clk[ZX296702_LSP0_AHB_HCLK] =
		zx_gate("lsp0_ahb_hclk",	"main_hclk",	CLK_EN1, 3);
	clk[ZX296702_LSP0_26M_WCLK] =
		zx_gate("lsp0_26M_wclk",   "lsp_26_wclk_mux",	CLK_EN1, 4);
	clk[ZX296702_LSP0_104M_WCLK] =
		zx_gate("lsp0_104M_wclk",	"pll_lsp_104M",	CLK_EN1, 5);
	clk[ZX296702_LSP0_16M384_WCLK] =
		zx_gate("lsp0_16M384_wclk",	"clk_16M384",	CLK_EN1, 6);
	clk[ZX296702_LSP1_APB_PCLK] =
		zx_gate("lsp1_apb_pclk",	"main_pclk",	CLK_EN1, 7);
	/* FIXME: wclk enable bit is bit8. We hack it as reserved 31 for
	 * UART does not work after parent clk is disabled/enabled */
	clk[ZX296702_LSP1_26M_WCLK] =
		zx_gate("lsp1_26M_wclk",     "lsp_26_wclk_mux",	CLK_EN1, 31);
	clk[ZX296702_LSP1_104M_WCLK] =
		zx_gate("lsp1_104M_wclk",    "pll_lsp_104M",	CLK_EN1, 9);
	clk[ZX296702_LSP1_32K_CLK] =
		zx_gate("lsp1_32K_clk",	"clk_32K768",		CLK_EN1, 10);
	clk[ZX296702_AON_HCLK] =
		zx_gate("aon_hclk",		"main_hclk",	CLK_EN1, 11);
	clk[ZX296702_SYS_CTRL_PCLK] =
		zx_gate("sys_ctrl_pclk",	"main_pclk",	CLK_EN1, 12);
	clk[ZX296702_DMA_PCLK] =
		zx_gate("dma_pclk",		"main_pclk",	CLK_EN1, 13);
	clk[ZX296702_DMA_ACLK] =
		zx_gate("dma_aclk",		"matrix_aclk",	CLK_EN1, 14);
	clk[ZX296702_SEC_HCLK] =
		zx_gate("sec_hclk",		"main_hclk",	CLK_EN1, 15);
	clk[ZX296702_AES_WCLK] =
		zx_gate("aes_wclk",		"sec_wclk_div",	CLK_EN1, 16);
	clk[ZX296702_DES_WCLK] =
		zx_gate("des_wclk",		"sec_wclk_div",	CLK_EN1, 17);
	clk[ZX296702_IRAM_ACLK] =
		zx_gate("iram_aclk",		"matrix_aclk",	CLK_EN1, 18);
	clk[ZX296702_IROM_ACLK] =
		zx_gate("irom_aclk",		"matrix_aclk",	CLK_EN1, 19);
	clk[ZX296702_BOOT_CTRL_HCLK] =
		zx_gate("boot_ctrl_hclk",	"main_hclk",	CLK_EN1, 20);
	clk[ZX296702_EFUSE_CLK_30] =
		zx_gate("efuse_clk_30",	"osc",			CLK_EN1, 21);

	/* TODO: add VOU Local clocks */
	clk[ZX296702_VOU_MAIN_CHANNEL_DIV] =
		zx_div("vou_main_channel_div", "vou_main_channel_wclk",
				VOU_LOCAL_DIV2_SET, 1, 1);
	clk[ZX296702_VOU_AUX_CHANNEL_DIV] =
		zx_div("vou_aux_channel_div", "vou_aux_channel_wclk",
				VOU_LOCAL_DIV2_SET, 0, 1);
	clk[ZX296702_VOU_TV_ENC_HD_DIV] =
		zx_div("vou_tv_enc_hd_div", "vou_tv_enc_hd_mux",
				VOU_LOCAL_DIV2_SET, 3, 1);
	clk[ZX296702_VOU_TV_ENC_SD_DIV] =
		zx_div("vou_tv_enc_sd_div", "vou_tv_enc_sd_mux",
				VOU_LOCAL_DIV2_SET, 2, 1);
	clk[ZX296702_VL0_MUX] =
		zx_mux("vl0_mux", vl0_sel, ARRAY_SIZE(vl0_sel),
				VOU_LOCAL_CLKSEL, 8, 1);
	clk[ZX296702_VL1_MUX] =
		zx_mux("vl1_mux", vl0_sel, ARRAY_SIZE(vl0_sel),
				VOU_LOCAL_CLKSEL, 9, 1);
	clk[ZX296702_VL2_MUX] =
		zx_mux("vl2_mux", vl0_sel, ARRAY_SIZE(vl0_sel),
				VOU_LOCAL_CLKSEL, 10, 1);
	clk[ZX296702_GL0_MUX] =
		zx_mux("gl0_mux", vl0_sel, ARRAY_SIZE(vl0_sel),
				VOU_LOCAL_CLKSEL, 5, 1);
	clk[ZX296702_GL1_MUX] =
		zx_mux("gl1_mux", vl0_sel, ARRAY_SIZE(vl0_sel),
				VOU_LOCAL_CLKSEL, 6, 1);
	clk[ZX296702_GL2_MUX] =
		zx_mux("gl2_mux", vl0_sel, ARRAY_SIZE(vl0_sel),
				VOU_LOCAL_CLKSEL, 7, 1);
	clk[ZX296702_WB_MUX] =
		zx_mux("wb_mux",  vl0_sel, ARRAY_SIZE(vl0_sel),
				VOU_LOCAL_CLKSEL, 11, 1);
	clk[ZX296702_HDMI_MUX] =
		zx_mux("hdmi_mux", hdmi_sel, ARRAY_SIZE(hdmi_sel),
				VOU_LOCAL_CLKSEL, 4, 1);
	clk[ZX296702_VOU_TV_ENC_HD_MUX] =
		zx_mux("vou_tv_enc_hd_mux", hdmi_sel, ARRAY_SIZE(hdmi_sel),
				VOU_LOCAL_CLKSEL, 3, 1);
	clk[ZX296702_VOU_TV_ENC_SD_MUX] =
		zx_mux("vou_tv_enc_sd_mux", hdmi_sel, ARRAY_SIZE(hdmi_sel),
				VOU_LOCAL_CLKSEL, 2, 1);
	clk[ZX296702_VL0_CLK] =
		zx_gate("vl0_clk", "vl0_mux", VOU_LOCAL_CLKEN, 8);
	clk[ZX296702_VL1_CLK] =
		zx_gate("vl1_clk", "vl1_mux", VOU_LOCAL_CLKEN, 9);
	clk[ZX296702_VL2_CLK] =
		zx_gate("vl2_clk", "vl2_mux", VOU_LOCAL_CLKEN, 10);
	clk[ZX296702_GL0_CLK] =
		zx_gate("gl0_clk", "gl0_mux", VOU_LOCAL_CLKEN, 5);
	clk[ZX296702_GL1_CLK] =
		zx_gate("gl1_clk", "gl1_mux", VOU_LOCAL_CLKEN, 6);
	clk[ZX296702_GL2_CLK] =
		zx_gate("gl2_clk", "gl2_mux", VOU_LOCAL_CLKEN, 7);
	clk[ZX296702_WB_CLK] =
		zx_gate("wb_clk", "wb_mux", VOU_LOCAL_CLKEN, 11);
	clk[ZX296702_CL_CLK] =
		zx_gate("cl_clk", "vou_main_channel_div", VOU_LOCAL_CLKEN, 12);
	clk[ZX296702_MAIN_MIX_CLK] =
		zx_gate("main_mix_clk", "vou_main_channel_div",
				VOU_LOCAL_CLKEN, 4);
	clk[ZX296702_AUX_MIX_CLK] =
		zx_gate("aux_mix_clk", "vou_aux_channel_div",
				VOU_LOCAL_CLKEN, 3);
	clk[ZX296702_HDMI_CLK] =
		zx_gate("hdmi_clk", "hdmi_mux", VOU_LOCAL_CLKEN, 2);
	clk[ZX296702_VOU_TV_ENC_HD_DAC_CLK] =
		zx_gate("vou_tv_enc_hd_dac_clk", "vou_tv_enc_hd_div",
				VOU_LOCAL_CLKEN, 1);
	clk[ZX296702_VOU_TV_ENC_SD_DAC_CLK] =
		zx_gate("vou_tv_enc_sd_dac_clk", "vou_tv_enc_sd_div",
				VOU_LOCAL_CLKEN, 0);

	/* CA9 PERIPHCLK = a9_wclk / 2 */
	clk[ZX296702_A9_PERIPHCLK] =
		clk_register_fixed_factor(NULL, "a9_periphclk", "a9_wclk",
				0, 1, 2);

	for (i = 0; i < ARRAY_SIZE(topclk); i++) {
		if (IS_ERR(clk[i])) {
			pr_err("zx296702 clk %d: register failed with %ld\n",
				i, PTR_ERR(clk[i]));
			return;
		}
	}

	topclk_data.clks = topclk;
	topclk_data.clk_num = ARRAY_SIZE(topclk);
	of_clk_add_provider(np, of_clk_src_onecell_get, &topclk_data);
}
CLK_OF_DECLARE(zx296702_top_clk, "zte,zx296702-topcrm-clk",
		zx296702_top_clocks_init);

static void __init zx296702_lsp0_clocks_init(struct device_node *np)
{
	struct clk **clk = lsp0clk;
	int i;

	lsp0crpm_base = of_iomap(np, 0);
	WARN_ON(!lsp0crpm_base);

	/* SDMMC1 */
	clk[ZX296702_SDMMC1_WCLK_MUX] =
		zx_mux("sdmmc1_wclk_mux", sdmmc1_wclk_sel,
				ARRAY_SIZE(sdmmc1_wclk_sel), CLK_SDMMC1, 4, 1);
	clk[ZX296702_SDMMC1_WCLK_DIV] =
		zx_div("sdmmc1_wclk_div", "sdmmc1_wclk_mux", CLK_SDMMC1, 12, 4);
	clk[ZX296702_SDMMC1_WCLK] =
		zx_gate("sdmmc1_wclk", "sdmmc1_wclk_div", CLK_SDMMC1, 1);
	clk[ZX296702_SDMMC1_PCLK] =
		zx_gate("sdmmc1_pclk", "lsp0_apb_pclk", CLK_SDMMC1, 0);

	clk[ZX296702_GPIO_CLK] =
		zx_gate("gpio_clk", "lsp0_apb_pclk", CLK_GPIO, 0);

	/* SPDIF */
	clk[ZX296702_SPDIF0_WCLK_MUX] =
		zx_mux("spdif0_wclk_mux", spdif0_wclk_sel,
				ARRAY_SIZE(spdif0_wclk_sel), CLK_SPDIF0, 4, 1);
	clk[ZX296702_SPDIF0_WCLK] =
		zx_gate("spdif0_wclk", "spdif0_wclk_mux", CLK_SPDIF0, 1);
	clk[ZX296702_SPDIF0_PCLK] =
		zx_gate("spdif0_pclk", "lsp0_apb_pclk", CLK_SPDIF0, 0);

	clk[ZX296702_SPDIF0_DIV] =
		clk_register_zx_audio("spdif0_div", "spdif0_wclk", 0,
				SPDIF0_DIV);

	/* I2S */
	clk[ZX296702_I2S0_WCLK_MUX] =
		zx_mux("i2s0_wclk_mux", i2s_wclk_sel,
				ARRAY_SIZE(i2s_wclk_sel), CLK_I2S0, 4, 1);
	clk[ZX296702_I2S0_WCLK] =
		zx_gate("i2s0_wclk", "i2s0_wclk_mux", CLK_I2S0, 1);
	clk[ZX296702_I2S0_PCLK] =
		zx_gate("i2s0_pclk", "lsp0_apb_pclk", CLK_I2S0, 0);

	clk[ZX296702_I2S0_DIV] =
		clk_register_zx_audio("i2s0_div", "i2s0_wclk", 0, I2S0_DIV);

	clk[ZX296702_I2S1_WCLK_MUX] =
		zx_mux("i2s1_wclk_mux", i2s_wclk_sel,
				ARRAY_SIZE(i2s_wclk_sel), CLK_I2S1, 4, 1);
	clk[ZX296702_I2S1_WCLK] =
		zx_gate("i2s1_wclk", "i2s1_wclk_mux", CLK_I2S1, 1);
	clk[ZX296702_I2S1_PCLK] =
		zx_gate("i2s1_pclk", "lsp0_apb_pclk", CLK_I2S1, 0);

	clk[ZX296702_I2S1_DIV] =
		clk_register_zx_audio("i2s1_div", "i2s1_wclk", 0, I2S1_DIV);

	clk[ZX296702_I2S2_WCLK_MUX] =
		zx_mux("i2s2_wclk_mux", i2s_wclk_sel,
				ARRAY_SIZE(i2s_wclk_sel), CLK_I2S2, 4, 1);
	clk[ZX296702_I2S2_WCLK] =
		zx_gate("i2s2_wclk", "i2s2_wclk_mux", CLK_I2S2, 1);
	clk[ZX296702_I2S2_PCLK] =
		zx_gate("i2s2_pclk", "lsp0_apb_pclk", CLK_I2S2, 0);

	clk[ZX296702_I2S2_DIV] =
		clk_register_zx_audio("i2s2_div", "i2s2_wclk", 0, I2S2_DIV);

	for (i = 0; i < ARRAY_SIZE(lsp0clk); i++) {
		if (IS_ERR(clk[i])) {
			pr_err("zx296702 clk %d: register failed with %ld\n",
				i, PTR_ERR(clk[i]));
			return;
		}
	}

	lsp0clk_data.clks = lsp0clk;
	lsp0clk_data.clk_num = ARRAY_SIZE(lsp0clk);
	of_clk_add_provider(np, of_clk_src_onecell_get, &lsp0clk_data);
}
CLK_OF_DECLARE(zx296702_lsp0_clk, "zte,zx296702-lsp0crpm-clk",
		zx296702_lsp0_clocks_init);

static void __init zx296702_lsp1_clocks_init(struct device_node *np)
{
	struct clk **clk = lsp1clk;
	int i;

	lsp1crpm_base = of_iomap(np, 0);
	WARN_ON(!lsp1crpm_base);

	/* UART0 */
	clk[ZX296702_UART0_WCLK_MUX] =
		zx_mux("uart0_wclk_mux", uart_wclk_sel,
				ARRAY_SIZE(uart_wclk_sel), CLK_UART0, 4, 1);
	/* FIXME: uart wclk enable bit is bit1 in. We hack it as reserved 31 for
	 * UART does not work after parent clk is disabled/enabled */
	clk[ZX296702_UART0_WCLK] =
		zx_gate("uart0_wclk", "uart0_wclk_mux", CLK_UART0, 31);
	clk[ZX296702_UART0_PCLK] =
		zx_gate("uart0_pclk", "lsp1_apb_pclk", CLK_UART0, 0);

	/* UART1 */
	clk[ZX296702_UART1_WCLK_MUX] =
		zx_mux("uart1_wclk_mux", uart_wclk_sel,
				ARRAY_SIZE(uart_wclk_sel), CLK_UART1, 4, 1);
	clk[ZX296702_UART1_WCLK] =
		zx_gate("uart1_wclk", "uart1_wclk_mux", CLK_UART1, 1);
	clk[ZX296702_UART1_PCLK] =
		zx_gate("uart1_pclk", "lsp1_apb_pclk", CLK_UART1, 0);

	/* SDMMC0 */
	clk[ZX296702_SDMMC0_WCLK_MUX] =
		zx_mux("sdmmc0_wclk_mux", sdmmc0_wclk_sel,
				ARRAY_SIZE(sdmmc0_wclk_sel), CLK_SDMMC0, 4, 1);
	clk[ZX296702_SDMMC0_WCLK_DIV] =
		zx_div("sdmmc0_wclk_div", "sdmmc0_wclk_mux", CLK_SDMMC0, 12, 4);
	clk[ZX296702_SDMMC0_WCLK] =
		zx_gate("sdmmc0_wclk", "sdmmc0_wclk_div", CLK_SDMMC0, 1);
	clk[ZX296702_SDMMC0_PCLK] =
		zx_gate("sdmmc0_pclk", "lsp1_apb_pclk", CLK_SDMMC0, 0);

	clk[ZX296702_SPDIF1_WCLK_MUX] =
		zx_mux("spdif1_wclk_mux", spdif1_wclk_sel,
				ARRAY_SIZE(spdif1_wclk_sel), CLK_SPDIF1, 4, 1);
	clk[ZX296702_SPDIF1_WCLK] =
		zx_gate("spdif1_wclk", "spdif1_wclk_mux", CLK_SPDIF1, 1);
	clk[ZX296702_SPDIF1_PCLK] =
		zx_gate("spdif1_pclk", "lsp1_apb_pclk", CLK_SPDIF1, 0);

	clk[ZX296702_SPDIF1_DIV] =
		clk_register_zx_audio("spdif1_div", "spdif1_wclk", 0,
				SPDIF1_DIV);

	for (i = 0; i < ARRAY_SIZE(lsp1clk); i++) {
		if (IS_ERR(clk[i])) {
			pr_err("zx296702 clk %d: register failed with %ld\n",
				i, PTR_ERR(clk[i]));
			return;
		}
	}

	lsp1clk_data.clks = lsp1clk;
	lsp1clk_data.clk_num = ARRAY_SIZE(lsp1clk);
	of_clk_add_provider(np, of_clk_src_onecell_get, &lsp1clk_data);
}
CLK_OF_DECLARE(zx296702_lsp1_clk, "zte,zx296702-lsp1crpm-clk",
		zx296702_lsp1_clocks_init);
