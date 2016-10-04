/*
 * Copyright (C) 2015 - 2016 ZTE Corporation.
 * Copyright (C) 2016 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/zx296718-clock.h>
#include "clk.h"

/* TOP CRM */
#define TOP_CLK_MUX0	0x04
#define TOP_CLK_MUX1	0x08
#define TOP_CLK_MUX2	0x0c
#define TOP_CLK_MUX3	0x10
#define TOP_CLK_MUX4	0x14
#define TOP_CLK_MUX5	0x18
#define TOP_CLK_MUX6	0x1c
#define TOP_CLK_MUX7	0x20
#define TOP_CLK_MUX9	0x28


#define TOP_CLK_GATE0	0x34
#define TOP_CLK_GATE1	0x38
#define TOP_CLK_GATE2	0x3c
#define TOP_CLK_GATE3	0x40
#define TOP_CLK_GATE4	0x44
#define TOP_CLK_GATE5	0x48
#define TOP_CLK_GATE6	0x4c

#define TOP_CLK_DIV0	0x58

#define PLL_CPU_REG	0x80
#define PLL_VGA_REG	0xb0
#define PLL_DDR_REG	0xa0

/* LSP0 CRM */
#define LSP0_TIMER3_CLK	0x4
#define LSP0_TIMER4_CLK	0x8
#define LSP0_TIMER5_CLK	0xc
#define LSP0_UART3_CLK	0x10
#define LSP0_UART1_CLK	0x14
#define LSP0_UART2_CLK	0x18
#define LSP0_SPIFC0_CLK	0x1c
#define LSP0_I2C4_CLK	0x20
#define LSP0_I2C5_CLK	0x24
#define LSP0_SSP0_CLK	0x28
#define LSP0_SSP1_CLK	0x2c
#define LSP0_USIM0_CLK	0x30
#define LSP0_GPIO_CLK	0x34
#define LSP0_I2C3_CLK	0x38

/* LSP1 CRM */
#define LSP1_UART4_CLK	0x08
#define LSP1_UART5_CLK	0x0c
#define LSP1_PWM_CLK	0x10
#define LSP1_I2C2_CLK	0x14
#define LSP1_SSP2_CLK	0x1c
#define LSP1_SSP3_CLK	0x20
#define LSP1_SSP4_CLK	0x24
#define LSP1_USIM1_CLK	0x28

/* audio lsp */
#define AUDIO_I2S0_DIV_CFG1	0x10
#define AUDIO_I2S0_DIV_CFG2	0x14
#define AUDIO_I2S0_CLK		0x18
#define AUDIO_I2S1_DIV_CFG1	0x20
#define AUDIO_I2S1_DIV_CFG2	0x24
#define AUDIO_I2S1_CLK		0x28
#define AUDIO_I2S2_DIV_CFG1	0x30
#define AUDIO_I2S2_DIV_CFG2	0x34
#define AUDIO_I2S2_CLK		0x38
#define AUDIO_I2S3_DIV_CFG1	0x40
#define AUDIO_I2S3_DIV_CFG2	0x44
#define AUDIO_I2S3_CLK		0x48
#define AUDIO_I2C0_CLK		0x50
#define AUDIO_SPDIF0_DIV_CFG1	0x60
#define AUDIO_SPDIF0_DIV_CFG2	0x64
#define AUDIO_SPDIF0_CLK	0x68
#define AUDIO_SPDIF1_DIV_CFG1	0x70
#define AUDIO_SPDIF1_DIV_CFG2	0x74
#define AUDIO_SPDIF1_CLK	0x78
#define AUDIO_TIMER_CLK		0x80
#define AUDIO_TDM_CLK		0x90
#define AUDIO_TS_CLK		0xa0

static DEFINE_SPINLOCK(clk_lock);

static struct zx_pll_config pll_cpu_table[] = {
	PLL_RATE(1312000000, 0x00103621, 0x04aaaaaa),
	PLL_RATE(1407000000, 0x00103a21, 0x04aaaaaa),
	PLL_RATE(1503000000, 0x00103e21, 0x04aaaaaa),
	PLL_RATE(1600000000, 0x00104221, 0x04aaaaaa),
};

PNAME(osc) = {
	"osc24m",
	"osc32k",
};

PNAME(dbg_wclk_p) = {
	"clk334m",
	"clk466m",
	"clk396m",
	"clk250m",
};

PNAME(a72_coreclk_p) = {
	"osc24m",
	"pll_mm0_1188m",
	"pll_mm1_1296m",
	"clk1000m",
	"clk648m",
	"clk1600m",
	"pll_audio_1800m",
	"pll_vga_1800m",
};

PNAME(cpu_periclk_p) = {
	"osc24m",
	"clk500m",
	"clk594m",
	"clk466m",
	"clk294m",
	"clk334m",
	"clk250m",
	"clk125m",
};

PNAME(a53_coreclk_p) = {
	"osc24m",
	"clk1000m",
	"pll_mm0_1188m",
	"clk648m",
	"clk500m",
	"clk800m",
	"clk1600m",
	"pll_audio_1800m",
};

PNAME(sec_wclk_p) = {
	"osc24m",
	"clk396m",
	"clk334m",
	"clk297m",
	"clk250m",
	"clk198m",
	"clk148m5",
	"clk99m",
};

PNAME(sd_nand_wclk_p) = {
	"osc24m",
	"clk49m5",
	"clk99m",
	"clk198m",
	"clk167m",
	"clk148m5",
	"clk125m",
	"clk216m",
};

PNAME(emmc_wclk_p) = {
	"osc24m",
	"clk198m",
	"clk99m",
	"clk396m",
	"clk334m",
	"clk297m",
	"clk250m",
	"clk148m5",
};

PNAME(clk32_p) = {
	"osc32k",
	"clk32k768",
};

PNAME(usb_ref24m_p) = {
	"osc32k",
	"clk32k768",
};

PNAME(sys_noc_alck_p) = {
	"osc24m",
	"clk250m",
	"clk198m",
	"clk148m5",
	"clk108m",
	"clk54m",
	"clk216m",
	"clk240m",
};

PNAME(vde_aclk_p) = {
	"clk334m",
	"clk594m",
	"clk500m",
	"clk432m",
	"clk480m",
	"clk297m",
	"clk_vga",  /*600MHz*/
	"clk294m",
};

PNAME(vce_aclk_p) = {
	"clk334m",
	"clk594m",
	"clk500m",
	"clk432m",
	"clk396m",
	"clk297m",
	"clk_vga",  /*600MHz*/
	"clk294m",
};

PNAME(hde_aclk_p) = {
	"clk334m",
	"clk594m",
	"clk500m",
	"clk432m",
	"clk396m",
	"clk297m",
	"clk_vga",  /*600MHz*/
	"clk294m",
};

PNAME(gpu_aclk_p) = {
	"clk334m",
	"clk648m",
	"clk594m",
	"clk500m",
	"clk396m",
	"clk297m",
	"clk_vga",  /*600MHz*/
	"clk294m",
};

PNAME(sappu_aclk_p) = {
	"clk396m",
	"clk500m",
	"clk250m",
	"clk148m5",
};

PNAME(sappu_wclk_p) = {
	"clk198m",
	"clk396m",
	"clk334m",
	"clk297m",
	"clk250m",
	"clk148m5",
	"clk125m",
	"clk99m",
};

PNAME(vou_aclk_p) = {
	"clk334m",
	"clk594m",
	"clk500m",
	"clk432m",
	"clk396m",
	"clk297m",
	"clk_vga",  /*600MHz*/
	"clk294m",
};

PNAME(vou_main_wclk_p) = {
	"clk108m",
	"clk594m",
	"clk297m",
	"clk148m5",
	"clk74m25",
	"clk54m",
	"clk27m",
	"clk_vga",
};

PNAME(vou_aux_wclk_p) = {
	"clk108m",
	"clk148m5",
	"clk74m25",
	"clk54m",
	"clk27m",
	"clk_vga",
	"clk54m_mm0",
	"clk"
};

PNAME(vou_ppu_wclk_p) = {
	"clk334m",
	"clk432m",
	"clk396m",
	"clk297m",
	"clk250m",
	"clk125m",
	"clk198m",
	"clk99m",
};

PNAME(vga_i2c_wclk_p) = {
	"osc24m",
	"clk99m",
};

PNAME(viu_m0_aclk_p) = {
	"clk334m",
	"clk432m",
	"clk396m",
	"clk297m",
	"clk250m",
	"clk125m",
	"clk198m",
	"osc24m",
};

PNAME(viu_m1_aclk_p) = {
	"clk198m",
	"clk250m",
	"clk297m",
	"clk125m",
	"clk396m",
	"clk334m",
	"clk148m5",
	"osc24m",
};

PNAME(viu_clk_p) = {
	"clk198m",
	"clk334m",
	"clk297m",
	"clk250m",
	"clk396m",
	"clk125m",
	"clk99m",
	"clk148m5",
};

PNAME(viu_jpeg_clk_p) = {
	"clk334m",
	"clk480m",
	"clk432m",
	"clk396m",
	"clk297m",
	"clk250m",
	"clk125m",
	"clk198m",
};

PNAME(ts_sys_clk_p) = {
	"clk192m",
	"clk167m",
	"clk125m",
	"clk99m",
};

PNAME(wdt_ares_p) = {
	"osc24m",
	"clk32k"
};

static struct clk_zx_pll zx296718_pll_clk[] = {
	ZX296718_PLL("pll_cpu",	"osc24m",	PLL_CPU_REG,	pll_cpu_table),
};

static struct zx_clk_fixed_factor top_ffactor_clk[] = {
	FFACTOR(0, "clk4m",		"osc24m", 1, 6,  0),
	FFACTOR(0, "clk2m",		"osc24m", 1, 12, 0),
	/* pll cpu */
	FFACTOR(0, "clk1600m",		"pll_cpu", 1, 1, CLK_SET_RATE_PARENT),
	FFACTOR(0, "clk800m",		"pll_cpu", 1, 2, CLK_SET_RATE_PARENT),
	/* pll mac */
	FFACTOR(0, "clk25m",		"pll_mac", 1, 40, 0),
	FFACTOR(0, "clk125m",		"pll_mac", 1, 8, 0),
	FFACTOR(0, "clk250m",		"pll_mac", 1, 4, 0),
	FFACTOR(0, "clk50m",		"pll_mac", 1, 20, 0),
	FFACTOR(0, "clk500m",		"pll_mac", 1, 2, 0),
	FFACTOR(0, "clk1000m",		"pll_mac", 1, 1, 0),
	FFACTOR(0, "clk334m",		"pll_mac", 1, 3, 0),
	FFACTOR(0, "clk167m",		"pll_mac", 1, 6, 0),
	/* pll mm */
	FFACTOR(0, "clk54m_mm0",	"pll_mm0", 1, 22, 0),
	FFACTOR(0, "clk74m25",		"pll_mm0", 1, 16, 0),
	FFACTOR(0, "clk148m5",		"pll_mm0", 1, 8, 0),
	FFACTOR(0, "clk297m",		"pll_mm0", 1, 4, 0),
	FFACTOR(0, "clk594m",		"pll_mm0", 1, 2, 0),
	FFACTOR(0, "pll_mm0_1188m",	"pll_mm0", 1, 1, 0),
	FFACTOR(0, "clk396m",		"pll_mm0", 1, 3, 0),
	FFACTOR(0, "clk198m",		"pll_mm0", 1, 6, 0),
	FFACTOR(0, "clk99m",		"pll_mm0", 1, 12, 0),
	FFACTOR(0, "clk49m5",		"pll_mm0", 1, 24, 0),
	/* pll mm */
	FFACTOR(0, "clk324m",		"pll_mm1", 1, 4, 0),
	FFACTOR(0, "clk648m",		"pll_mm1", 1, 2, 0),
	FFACTOR(0, "pll_mm1_1296m",	"pll_mm1", 1, 1, 0),
	FFACTOR(0, "clk216m",		"pll_mm1", 1, 6, 0),
	FFACTOR(0, "clk432m",		"pll_mm1", 1, 3, 0),
	FFACTOR(0, "clk108m",		"pll_mm1", 1, 12, 0),
	FFACTOR(0, "clk72m",		"pll_mm1", 1, 18, 0),
	FFACTOR(0, "clk27m",		"pll_mm1", 1, 48, 0),
	FFACTOR(0, "clk54m",		"pll_mm1", 1, 24, 0),
	/* vga */
	FFACTOR(0, "pll_vga_1800m",	"pll_vga", 1, 1, 0),
	FFACTOR(0, "clk_vga",		"pll_vga", 1, 2, 0),
	/* pll ddr */
	FFACTOR(0, "clk466m",		"pll_ddr", 1, 2, 0),

	/* pll audio */
	FFACTOR(0, "pll_audio_1800m",	"pll_audio", 1, 1, 0),
	FFACTOR(0, "clk32k768",		"pll_audio", 1, 27000, 0),
	FFACTOR(0, "clk16m384",		"pll_audio", 1, 54, 0),
	FFACTOR(0, "clk294m",		"pll_audio", 1, 3, 0),

	/* pll hsic*/
	FFACTOR(0, "clk240m",		"pll_hsic", 1, 4, 0),
	FFACTOR(0, "clk480m",		"pll_hsic", 1, 2, 0),
	FFACTOR(0, "clk192m",		"pll_hsic", 1, 5, 0),
	FFACTOR(0, "clk_pll_24m",	"pll_hsic", 1, 40, 0),
	FFACTOR(0, "emmc_mux_div2",	"emmc_mux", 1, 2, CLK_SET_RATE_PARENT),
};

static struct clk_div_table noc_div_table[] = {
	{ .val = 1, .div = 2, },
	{ .val = 3, .div = 4, },
};
static struct zx_clk_div top_div_clk[] = {
	DIV_T(0, "sys_noc_hclk", "sys_noc_aclk", TOP_CLK_DIV0, 0, 2, 0, noc_div_table),
	DIV_T(0, "sys_noc_pclk", "sys_noc_aclk", TOP_CLK_DIV0, 4, 2, 0, noc_div_table),
};

static struct zx_clk_mux top_mux_clk[] = {
	MUX(0, "dbg_mux",	 dbg_wclk_p,	  TOP_CLK_MUX0, 12, 2),
	MUX(0, "a72_mux",	 a72_coreclk_p,	  TOP_CLK_MUX0, 8, 3),
	MUX(0, "cpu_peri_mux",	 cpu_periclk_p,	  TOP_CLK_MUX0, 4, 3),
	MUX_F(0, "a53_mux",	 a53_coreclk_p,	  TOP_CLK_MUX0, 0, 3, CLK_SET_RATE_PARENT, 0),
	MUX(0, "sys_noc_aclk",	 sys_noc_alck_p,  TOP_CLK_MUX1, 0, 3),
	MUX(0, "sec_mux",	 sec_wclk_p,	  TOP_CLK_MUX2, 16, 3),
	MUX(0, "sd1_mux",	 sd_nand_wclk_p,  TOP_CLK_MUX2, 12, 3),
	MUX(0, "sd0_mux",	 sd_nand_wclk_p,  TOP_CLK_MUX2, 8, 3),
	MUX(0, "emmc_mux",	 emmc_wclk_p,	  TOP_CLK_MUX2, 4, 3),
	MUX(0, "nand_mux",	 sd_nand_wclk_p,  TOP_CLK_MUX2, 0, 3),
	MUX(0, "usb_ref24m_mux", usb_ref24m_p,	  TOP_CLK_MUX9, 16, 1),
	MUX(0, "clk32k",	 clk32_p,	  TOP_CLK_MUX9, 12, 1),
	MUX_F(0, "wdt_mux",	 wdt_ares_p,	  TOP_CLK_MUX9, 8, 1, CLK_SET_RATE_PARENT, 0),
	MUX(0, "timer_mux",	 osc,		  TOP_CLK_MUX9, 4, 1),
	MUX(0, "vde_mux",	 vde_aclk_p,	  TOP_CLK_MUX4,  0, 3),
	MUX(0, "vce_mux",	 vce_aclk_p,	  TOP_CLK_MUX4,  4, 3),
	MUX(0, "hde_mux",	 hde_aclk_p,	  TOP_CLK_MUX4,  8, 3),
	MUX(0, "gpu_mux",	 gpu_aclk_p,	  TOP_CLK_MUX5,  0, 3),
	MUX(0, "sappu_a_mux",	 sappu_aclk_p,	  TOP_CLK_MUX5,  4, 2),
	MUX(0, "sappu_w_mux",	 sappu_wclk_p,	  TOP_CLK_MUX5,  8, 3),
	MUX(0, "vou_a_mux",	 vou_aclk_p,	  TOP_CLK_MUX7,  0, 3),
	MUX(0, "vou_main_w_mux", vou_main_wclk_p, TOP_CLK_MUX7,  4, 3),
	MUX(0, "vou_aux_w_mux",	 vou_aux_wclk_p,  TOP_CLK_MUX7,  8, 3),
	MUX(0, "vou_ppu_w_mux",	 vou_ppu_wclk_p,  TOP_CLK_MUX7, 12, 3),
	MUX(0, "vga_i2c_mux",	 vga_i2c_wclk_p,  TOP_CLK_MUX7, 16, 1),
	MUX(0, "viu_m0_a_mux",	 viu_m0_aclk_p,	  TOP_CLK_MUX6,  0, 3),
	MUX(0, "viu_m1_a_mux",	 viu_m1_aclk_p,	  TOP_CLK_MUX6,  4, 3),
	MUX(0, "viu_w_mux",	 viu_clk_p,	  TOP_CLK_MUX6,  8, 3),
	MUX(0, "viu_jpeg_w_mux", viu_jpeg_clk_p,  TOP_CLK_MUX6, 12, 3),
	MUX(0, "ts_sys_mux",	 ts_sys_clk_p,    TOP_CLK_MUX6, 16, 2),
};

static struct zx_clk_gate top_gate_clk[] = {
	GATE(CPU_DBG_GATE,    "dbg_wclk",        "dbg_mux",        TOP_CLK_GATE0, 4, CLK_SET_RATE_PARENT, 0),
	GATE(A72_GATE,        "a72_coreclk",     "a72_mux",        TOP_CLK_GATE0, 3, CLK_SET_RATE_PARENT, 0),
	GATE(CPU_PERI_GATE,   "cpu_peri",        "cpu_peri_mux",   TOP_CLK_GATE0, 1, CLK_SET_RATE_PARENT, 0),
	GATE(A53_GATE,        "a53_coreclk",     "a53_mux",        TOP_CLK_GATE0, 0, CLK_SET_RATE_PARENT, 0),
	GATE(SD1_WCLK,        "sd1_wclk",        "sd1_mux",        TOP_CLK_GATE1, 13, CLK_SET_RATE_PARENT, 0),
	GATE(SD0_WCLK,        "sd0_wclk",        "sd0_mux",        TOP_CLK_GATE1, 9, CLK_SET_RATE_PARENT, 0),
	GATE(EMMC_WCLK,       "emmc_wclk",       "emmc_mux_div2",  TOP_CLK_GATE0, 5, CLK_SET_RATE_PARENT, 0),
	GATE(EMMC_NAND_AXI,   "emmc_nand_aclk",  "sys_noc_aclk",   TOP_CLK_GATE1, 4, CLK_SET_RATE_PARENT, 0),
	GATE(NAND_WCLK,       "nand_wclk",       "nand_mux",       TOP_CLK_GATE0, 1, CLK_SET_RATE_PARENT, 0),
	GATE(EMMC_NAND_AHB,   "emmc_nand_hclk",  "sys_noc_hclk",   TOP_CLK_GATE1, 0, CLK_SET_RATE_PARENT, 0),
	GATE(0,               "lsp1_pclk",       "sys_noc_pclk",   TOP_CLK_GATE2, 31, 0,                  0),
	GATE(LSP1_148M5,      "lsp1_148m5",      "clk148m5",       TOP_CLK_GATE2, 30, 0,                  0),
	GATE(LSP1_99M,        "lsp1_99m",        "clk99m",         TOP_CLK_GATE2, 29, 0,                  0),
	GATE(LSP1_24M,        "lsp1_24m",        "osc24m",         TOP_CLK_GATE2, 28, 0,                  0),
	GATE(LSP0_74M25,      "lsp0_74m25",      "clk74m25",       TOP_CLK_GATE2, 25, 0,                  0),
	GATE(0,               "lsp0_pclk",       "sys_noc_pclk",   TOP_CLK_GATE2, 24, 0,                  0),
	GATE(LSP0_32K,        "lsp0_32k",        "osc32k",         TOP_CLK_GATE2, 23, 0,                  0),
	GATE(LSP0_148M5,      "lsp0_148m5",      "clk148m5",       TOP_CLK_GATE2, 22, 0,                  0),
	GATE(LSP0_99M,        "lsp0_99m",        "clk99m",         TOP_CLK_GATE2, 21, 0,                  0),
	GATE(LSP0_24M,        "lsp0_24m",        "osc24m",         TOP_CLK_GATE2, 20, 0,                  0),
	GATE(AUDIO_99M,       "audio_99m",       "clk99m",         TOP_CLK_GATE5, 27, 0,                  0),
	GATE(AUDIO_24M,       "audio_24m",       "osc24m",         TOP_CLK_GATE5, 28, 0,                  0),
	GATE(AUDIO_16M384,    "audio_16m384",    "clk16m384",      TOP_CLK_GATE5, 29, 0,                  0),
	GATE(AUDIO_32K,       "audio_32k",       "clk32k",         TOP_CLK_GATE5, 30, 0,                  0),
	GATE(WDT_WCLK,        "wdt_wclk",        "wdt_mux",        TOP_CLK_GATE6, 9, CLK_SET_RATE_PARENT, 0),
	GATE(TIMER_WCLK,      "timer_wclk",      "timer_mux",      TOP_CLK_GATE6, 5, CLK_SET_RATE_PARENT, 0),
	GATE(VDE_ACLK,        "vde_aclk",        "vde_mux",        TOP_CLK_GATE3, 0,  CLK_SET_RATE_PARENT, 0),
	GATE(VCE_ACLK,        "vce_aclk",        "vce_mux",        TOP_CLK_GATE3, 4,  CLK_SET_RATE_PARENT, 0),
	GATE(HDE_ACLK,        "hde_aclk",        "hde_mux",        TOP_CLK_GATE3, 8,  CLK_SET_RATE_PARENT, 0),
	GATE(GPU_ACLK,        "gpu_aclk",        "gpu_mux",        TOP_CLK_GATE3, 16, CLK_SET_RATE_PARENT, 0),
	GATE(SAPPU_ACLK,      "sappu_aclk",      "sappu_a_mux",    TOP_CLK_GATE3, 20, CLK_SET_RATE_PARENT, 0),
	GATE(SAPPU_WCLK,      "sappu_wclk",      "sappu_w_mux",    TOP_CLK_GATE3, 22, CLK_SET_RATE_PARENT, 0),
	GATE(VOU_ACLK,        "vou_aclk",        "vou_a_mux",      TOP_CLK_GATE4, 16, CLK_SET_RATE_PARENT, 0),
	GATE(VOU_MAIN_WCLK,   "vou_main_wclk",   "vou_main_w_mux", TOP_CLK_GATE4, 18, CLK_SET_RATE_PARENT, 0),
	GATE(VOU_AUX_WCLK,    "vou_aux_wclk",    "vou_aux_w_mux",  TOP_CLK_GATE4, 19, CLK_SET_RATE_PARENT, 0),
	GATE(VOU_PPU_WCLK,    "vou_ppu_wclk",    "vou_ppu_w_mux",  TOP_CLK_GATE4, 20, CLK_SET_RATE_PARENT, 0),
	GATE(MIPI_CFG_CLK,    "mipi_cfg_clk",    "osc24m",         TOP_CLK_GATE4, 21, 0,                   0),
	GATE(VGA_I2C_WCLK,    "vga_i2c_wclk",    "vga_i2c_mux",    TOP_CLK_GATE4, 23, CLK_SET_RATE_PARENT, 0),
	GATE(MIPI_REF_CLK,    "mipi_ref_clk",    "clk27m",         TOP_CLK_GATE4, 24, 0,                   0),
	GATE(HDMI_OSC_CEC,    "hdmi_osc_cec",    "clk2m",          TOP_CLK_GATE4, 22, 0,                   0),
	GATE(HDMI_OSC_CLK,    "hdmi_osc_clk",    "clk240m",        TOP_CLK_GATE4, 25, 0,                   0),
	GATE(HDMI_XCLK,       "hdmi_xclk",       "osc24m",         TOP_CLK_GATE4, 26, 0,                   0),
	GATE(VIU_M0_ACLK,     "viu_m0_aclk",     "viu_m0_a_mux",   TOP_CLK_GATE4, 0,  CLK_SET_RATE_PARENT, 0),
	GATE(VIU_M1_ACLK,     "viu_m1_aclk",     "viu_m1_a_mux",   TOP_CLK_GATE4, 1,  CLK_SET_RATE_PARENT, 0),
	GATE(VIU_WCLK,        "viu_wclk",        "viu_w_mux",      TOP_CLK_GATE4, 2,  CLK_SET_RATE_PARENT, 0),
	GATE(VIU_JPEG_WCLK,   "viu_jpeg_wclk",   "viu_jpeg_w_mux", TOP_CLK_GATE4, 3,  CLK_SET_RATE_PARENT, 0),
	GATE(VIU_CFG_CLK,     "viu_cfg_clk",     "osc24m",         TOP_CLK_GATE4, 6,  0,                   0),
	GATE(TS_SYS_WCLK,     "ts_sys_wclk",     "ts_sys_mux",     TOP_CLK_GATE5, 2,  CLK_SET_RATE_PARENT, 0),
	GATE(TS_SYS_108M,     "ts_sys_108m",     "clk108m",        TOP_CLK_GATE5, 3,  0,                   0),
	GATE(USB20_HCLK,      "usb20_hclk",      "sys_noc_hclk",   TOP_CLK_GATE2, 12, 0,                   0),
	GATE(USB20_PHY_CLK,   "usb20_phy_clk",   "usb_ref24m_mux", TOP_CLK_GATE2, 13, 0,                   0),
	GATE(USB21_HCLK,      "usb21_hclk",      "sys_noc_hclk",   TOP_CLK_GATE2, 14, 0,                   0),
	GATE(USB21_PHY_CLK,   "usb21_phy_clk",   "usb_ref24m_mux", TOP_CLK_GATE2, 15, 0,                   0),
	GATE(GMAC_RMIICLK,    "gmac_rmii_clk",   "clk50m",         TOP_CLK_GATE2, 3, 0,                    0),
	GATE(GMAC_PCLK,       "gmac_pclk",       "clk198m",        TOP_CLK_GATE2, 1, 0,                    0),
	GATE(GMAC_ACLK,       "gmac_aclk",       "clk49m5",        TOP_CLK_GATE2, 0, 0,                    0),
	GATE(GMAC_RFCLK,      "gmac_refclk",     "clk25m",         TOP_CLK_GATE2, 4, 0,                    0),
	GATE(SD1_AHB,         "sd1_hclk",        "sys_noc_hclk",   TOP_CLK_GATE1, 12,  0,                  0),
	GATE(SD0_AHB,         "sd0_hclk",        "sys_noc_hclk",   TOP_CLK_GATE1, 8,  0,                   0),
	GATE(TEMPSENSOR_GATE, "tempsensor_gate", "clk4m",          TOP_CLK_GATE5, 31,  0,                  0),
};

static struct clk_hw_onecell_data top_hw_onecell_data = {
	.num = TOP_NR_CLKS,
	.hws = {
		[TOP_NR_CLKS - 1] = NULL,
	},
};

static int __init top_clocks_init(struct device_node *np)
{
	void __iomem *reg_base;
	int i, ret;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: Unable to map clk base\n", __func__);
		return -ENXIO;
	}

	for (i = 0; i < ARRAY_SIZE(zx296718_pll_clk); i++) {
		zx296718_pll_clk[i].reg_base += (uintptr_t)reg_base;
		ret = clk_hw_register(NULL, &zx296718_pll_clk[i].hw);
		if (ret) {
			pr_warn("top clk %s init error!\n",
				zx296718_pll_clk[i].hw.init->name);
		}
	}

	for (i = 0; i < ARRAY_SIZE(top_ffactor_clk); i++) {
		if (top_ffactor_clk[i].id)
			top_hw_onecell_data.hws[top_ffactor_clk[i].id] =
					&top_ffactor_clk[i].factor.hw;

		ret = clk_hw_register(NULL, &top_ffactor_clk[i].factor.hw);
		if (ret) {
			pr_warn("top clk %s init error!\n",
				top_ffactor_clk[i].factor.hw.init->name);
		}
	}

	for (i = 0; i < ARRAY_SIZE(top_mux_clk); i++) {
		if (top_mux_clk[i].id)
			top_hw_onecell_data.hws[top_mux_clk[i].id] =
					&top_mux_clk[i].mux.hw;

		top_mux_clk[i].mux.reg += (uintptr_t)reg_base;
		ret = clk_hw_register(NULL, &top_mux_clk[i].mux.hw);
		if (ret) {
			pr_warn("top clk %s init error!\n",
				top_mux_clk[i].mux.hw.init->name);
		}
	}

	for (i = 0; i < ARRAY_SIZE(top_gate_clk); i++) {
		if (top_gate_clk[i].id)
			top_hw_onecell_data.hws[top_gate_clk[i].id] =
					&top_gate_clk[i].gate.hw;

		top_gate_clk[i].gate.reg += (uintptr_t)reg_base;
		ret = clk_hw_register(NULL, &top_gate_clk[i].gate.hw);
		if (ret) {
			pr_warn("top clk %s init error!\n",
				top_gate_clk[i].gate.hw.init->name);
		}
	}

	for (i = 0; i < ARRAY_SIZE(top_div_clk); i++) {
		if (top_div_clk[i].id)
			top_hw_onecell_data.hws[top_div_clk[i].id] =
					&top_div_clk[i].div.hw;

		top_div_clk[i].div.reg += (uintptr_t)reg_base;
		ret = clk_hw_register(NULL, &top_div_clk[i].div.hw);
		if (ret) {
			pr_warn("top clk %s init error!\n",
				top_div_clk[i].div.hw.init->name);
		}
	}

	if (of_clk_add_hw_provider(np, of_clk_hw_onecell_get, &top_hw_onecell_data))
		panic("could not register clk provider\n");
	pr_info("top clk init over, nr:%d\n", TOP_NR_CLKS);

	return 0;
}

static struct clk_div_table common_even_div_table[] = {
	{ .val = 0, .div = 1, },
	{ .val = 1, .div = 2, },
	{ .val = 3, .div = 4, },
	{ .val = 5, .div = 6, },
	{ .val = 7, .div = 8, },
	{ .val = 9, .div = 10, },
	{ .val = 11, .div = 12, },
	{ .val = 13, .div = 14, },
	{ .val = 15, .div = 16, },
};

static struct clk_div_table common_div_table[] = {
	{ .val = 0, .div = 1, },
	{ .val = 1, .div = 2, },
	{ .val = 2, .div = 3, },
	{ .val = 3, .div = 4, },
	{ .val = 4, .div = 5, },
	{ .val = 5, .div = 6, },
	{ .val = 6, .div = 7, },
	{ .val = 7, .div = 8, },
	{ .val = 8, .div = 9, },
	{ .val = 9, .div = 10, },
	{ .val = 10, .div = 11, },
	{ .val = 11, .div = 12, },
	{ .val = 12, .div = 13, },
	{ .val = 13, .div = 14, },
	{ .val = 14, .div = 15, },
	{ .val = 15, .div = 16, },
};

PNAME(lsp0_wclk_common_p) = {
	"lsp0_24m",
	"lsp0_99m",
};

PNAME(lsp0_wclk_timer3_p) = {
	"timer3_div",
	"lsp0_32k"
};

PNAME(lsp0_wclk_timer4_p) = {
	"timer4_div",
	"lsp0_32k"
};

PNAME(lsp0_wclk_timer5_p) = {
	"timer5_div",
	"lsp0_32k"
};

PNAME(lsp0_wclk_spifc0_p) = {
	"lsp0_148m5",
	"lsp0_24m",
	"lsp0_99m",
	"lsp0_74m25"
};

PNAME(lsp0_wclk_ssp_p) = {
	"lsp0_148m5",
	"lsp0_99m",
	"lsp0_24m",
};

static struct zx_clk_mux lsp0_mux_clk[] = {
	MUX(0, "timer3_wclk_mux", lsp0_wclk_timer3_p, LSP0_TIMER3_CLK, 4, 1),
	MUX(0, "timer4_wclk_mux", lsp0_wclk_timer4_p, LSP0_TIMER4_CLK, 4, 1),
	MUX(0, "timer5_wclk_mux", lsp0_wclk_timer5_p, LSP0_TIMER5_CLK, 4, 1),
	MUX(0, "uart3_wclk_mux",  lsp0_wclk_common_p, LSP0_UART3_CLK,  4, 1),
	MUX(0, "uart1_wclk_mux",  lsp0_wclk_common_p, LSP0_UART1_CLK,  4, 1),
	MUX(0, "uart2_wclk_mux",  lsp0_wclk_common_p, LSP0_UART2_CLK,  4, 1),
	MUX(0, "spifc0_wclk_mux", lsp0_wclk_spifc0_p, LSP0_SPIFC0_CLK, 4, 2),
	MUX(0, "i2c4_wclk_mux",   lsp0_wclk_common_p, LSP0_I2C4_CLK,   4, 1),
	MUX(0, "i2c5_wclk_mux",   lsp0_wclk_common_p, LSP0_I2C5_CLK,   4, 1),
	MUX(0, "ssp0_wclk_mux",   lsp0_wclk_ssp_p,    LSP0_SSP0_CLK,   4, 1),
	MUX(0, "ssp1_wclk_mux",   lsp0_wclk_ssp_p,    LSP0_SSP1_CLK,   4, 1),
	MUX(0, "i2c3_wclk_mux",   lsp0_wclk_common_p, LSP0_I2C3_CLK,   4, 1),
};

static struct zx_clk_gate lsp0_gate_clk[] = {
	GATE(LSP0_TIMER3_WCLK, "timer3_wclk", "timer3_wclk_mux", LSP0_TIMER3_CLK, 1, CLK_SET_RATE_PARENT, 0),
	GATE(LSP0_TIMER4_WCLK, "timer4_wclk", "timer4_wclk_mux", LSP0_TIMER4_CLK, 1, CLK_SET_RATE_PARENT, 0),
	GATE(LSP0_TIMER5_WCLK, "timer5_wclk", "timer5_wclk_mux", LSP0_TIMER5_CLK, 1, CLK_SET_RATE_PARENT, 0),
	GATE(LSP0_UART3_WCLK,  "uart3_wclk",  "uart3_wclk_mux",  LSP0_UART3_CLK,  1, CLK_SET_RATE_PARENT, 0),
	GATE(LSP0_UART1_WCLK,  "uart1_wclk",  "uart1_wclk_mux",  LSP0_UART1_CLK,  1, CLK_SET_RATE_PARENT, 0),
	GATE(LSP0_UART2_WCLK,  "uart2_wclk",  "uart2_wclk_mux",  LSP0_UART2_CLK,  1, CLK_SET_RATE_PARENT, 0),
	GATE(LSP0_SPIFC0_WCLK, "spifc0_wclk", "spifc0_wclk_mux", LSP0_SPIFC0_CLK, 1, CLK_SET_RATE_PARENT, 0),
	GATE(LSP0_I2C4_WCLK,   "i2c4_wclk",   "i2c4_wclk_mux",   LSP0_I2C4_CLK,   1, CLK_SET_RATE_PARENT, 0),
	GATE(LSP0_I2C5_WCLK,   "i2c5_wclk",   "i2c5_wclk_mux",   LSP0_I2C5_CLK,   1, CLK_SET_RATE_PARENT, 0),
	GATE(LSP0_SSP0_WCLK,   "ssp0_wclk",   "ssp0_div",        LSP0_SSP0_CLK,   1, CLK_SET_RATE_PARENT, 0),
	GATE(LSP0_SSP1_WCLK,   "ssp1_wclk",   "ssp1_div",        LSP0_SSP1_CLK,   1, CLK_SET_RATE_PARENT, 0),
	GATE(LSP0_I2C3_WCLK,   "i2c3_wclk",   "i2c3_wclk_mux",   LSP0_I2C3_CLK,   1, CLK_SET_RATE_PARENT, 0),
};

static struct zx_clk_div lsp0_div_clk[] = {
	DIV_T(0, "timer3_div", "lsp0_24m", LSP0_TIMER3_CLK,  12, 4, 0, common_even_div_table),
	DIV_T(0, "timer4_div", "lsp0_24m", LSP0_TIMER4_CLK,  12, 4, 0, common_even_div_table),
	DIV_T(0, "timer5_div", "lsp0_24m", LSP0_TIMER5_CLK,  12, 4, 0, common_even_div_table),
	DIV_T(0, "ssp0_div", "ssp0_wclk_mux", LSP0_SSP0_CLK, 12, 4, 0, common_even_div_table),
	DIV_T(0, "ssp1_div", "ssp1_wclk_mux", LSP0_SSP1_CLK, 12, 4, 0, common_even_div_table),
};

static struct clk_hw_onecell_data lsp0_hw_onecell_data = {
	.num = LSP0_NR_CLKS,
	.hws = {
		[LSP0_NR_CLKS - 1] = NULL,
	},
};

static int __init lsp0_clocks_init(struct device_node *np)
{
	void __iomem *reg_base;
	int i, ret;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: Unable to map clk base\n", __func__);
		return -ENXIO;
	}

	for (i = 0; i < ARRAY_SIZE(lsp0_mux_clk); i++) {
		if (lsp0_mux_clk[i].id)
			lsp0_hw_onecell_data.hws[lsp0_mux_clk[i].id] =
					&lsp0_mux_clk[i].mux.hw;

		lsp0_mux_clk[i].mux.reg += (uintptr_t)reg_base;
		ret = clk_hw_register(NULL, &lsp0_mux_clk[i].mux.hw);
		if (ret) {
			pr_warn("lsp0 clk %s init error!\n",
				lsp0_mux_clk[i].mux.hw.init->name);
		}
	}

	for (i = 0; i < ARRAY_SIZE(lsp0_gate_clk); i++) {
		if (lsp0_gate_clk[i].id)
			lsp0_hw_onecell_data.hws[lsp0_gate_clk[i].id] =
					&lsp0_gate_clk[i].gate.hw;

		lsp0_gate_clk[i].gate.reg += (uintptr_t)reg_base;
		ret = clk_hw_register(NULL, &lsp0_gate_clk[i].gate.hw);
		if (ret) {
			pr_warn("lsp0 clk %s init error!\n",
				lsp0_gate_clk[i].gate.hw.init->name);
		}
	}

	for (i = 0; i < ARRAY_SIZE(lsp0_div_clk); i++) {
		if (lsp0_div_clk[i].id)
			lsp0_hw_onecell_data.hws[lsp0_div_clk[i].id] =
					&lsp0_div_clk[i].div.hw;

		lsp0_div_clk[i].div.reg += (uintptr_t)reg_base;
		ret = clk_hw_register(NULL, &lsp0_div_clk[i].div.hw);
		if (ret) {
			pr_warn("lsp0 clk %s init error!\n",
				lsp0_div_clk[i].div.hw.init->name);
		}
	}

	if (of_clk_add_hw_provider(np, of_clk_hw_onecell_get, &lsp0_hw_onecell_data))
		panic("could not register clk provider\n");
	pr_info("lsp0-clk init over:%d\n", LSP0_NR_CLKS);

	return 0;
}

PNAME(lsp1_wclk_common_p) = {
	"lsp1_24m",
	"lsp1_99m",
};

PNAME(lsp1_wclk_ssp_p) = {
	"lsp1_148m5",
	"lsp1_99m",
	"lsp1_24m",
};

static struct zx_clk_mux lsp1_mux_clk[] = {
	MUX(0, "uart4_wclk_mux", lsp1_wclk_common_p, LSP1_UART4_CLK, 4, 1),
	MUX(0, "uart5_wclk_mux", lsp1_wclk_common_p, LSP1_UART5_CLK, 4, 1),
	MUX(0, "pwm_wclk_mux",   lsp1_wclk_common_p, LSP1_PWM_CLK,   4, 1),
	MUX(0, "i2c2_wclk_mux",  lsp1_wclk_common_p, LSP1_I2C2_CLK,  4, 1),
	MUX(0, "ssp2_wclk_mux",  lsp1_wclk_ssp_p,    LSP1_SSP2_CLK,  4, 2),
	MUX(0, "ssp3_wclk_mux",  lsp1_wclk_ssp_p,    LSP1_SSP3_CLK,  4, 2),
	MUX(0, "ssp4_wclk_mux",  lsp1_wclk_ssp_p,    LSP1_SSP4_CLK,  4, 2),
	MUX(0, "usim1_wclk_mux", lsp1_wclk_common_p, LSP1_USIM1_CLK, 4, 1),
};

static struct zx_clk_div lsp1_div_clk[] = {
	DIV_T(0, "pwm_div",  "pwm_wclk_mux",  LSP1_PWM_CLK,  12, 4, CLK_SET_RATE_PARENT, common_div_table),
	DIV_T(0, "ssp2_div", "ssp2_wclk_mux", LSP1_SSP2_CLK, 12, 4, CLK_SET_RATE_PARENT, common_even_div_table),
	DIV_T(0, "ssp3_div", "ssp3_wclk_mux", LSP1_SSP3_CLK, 12, 4, CLK_SET_RATE_PARENT, common_even_div_table),
	DIV_T(0, "ssp4_div", "ssp4_wclk_mux", LSP1_SSP4_CLK, 12, 4, CLK_SET_RATE_PARENT, common_even_div_table),
};

static struct zx_clk_gate lsp1_gate_clk[] = {
	GATE(LSP1_UART4_WCLK, "lsp1_uart4_wclk", "uart4_wclk_mux", LSP1_UART4_CLK, 1, CLK_SET_RATE_PARENT, 0),
	GATE(LSP1_UART5_WCLK, "lsp1_uart5_wclk", "uart5_wclk_mux", LSP1_UART5_CLK, 1, CLK_SET_RATE_PARENT, 0),
	GATE(LSP1_PWM_WCLK,   "lsp1_pwm_wclk",   "pwm_div",        LSP1_PWM_CLK,   1, CLK_SET_RATE_PARENT, 0),
	GATE(LSP1_PWM_PCLK,   "lsp1_pwm_pclk",   "lsp1_pclk",      LSP1_PWM_CLK,   0, 0,		   0),
	GATE(LSP1_I2C2_WCLK,  "lsp1_i2c2_wclk",  "i2c2_wclk_mux",  LSP1_I2C2_CLK,  1, CLK_SET_RATE_PARENT, 0),
	GATE(LSP1_SSP2_WCLK,  "lsp1_ssp2_wclk",  "ssp2_div",       LSP1_SSP2_CLK,  1, CLK_SET_RATE_PARENT, 0),
	GATE(LSP1_SSP3_WCLK,  "lsp1_ssp3_wclk",  "ssp3_div",       LSP1_SSP3_CLK,  1, CLK_SET_RATE_PARENT, 0),
	GATE(LSP1_SSP4_WCLK,  "lsp1_ssp4_wclk",  "ssp4_div",       LSP1_SSP4_CLK,  1, CLK_SET_RATE_PARENT, 0),
	GATE(LSP1_USIM1_WCLK, "lsp1_usim1_wclk", "usim1_wclk_mux", LSP1_USIM1_CLK, 1, CLK_SET_RATE_PARENT, 0),
};

static struct clk_hw_onecell_data lsp1_hw_onecell_data = {
	.num = LSP1_NR_CLKS,
	.hws = {
		[LSP1_NR_CLKS - 1] = NULL,
	},
};

static int __init lsp1_clocks_init(struct device_node *np)
{
	void __iomem *reg_base;
	int i, ret;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: Unable to map clk base\n", __func__);
		return -ENXIO;
	}

	for (i = 0; i < ARRAY_SIZE(lsp1_mux_clk); i++) {
		if (lsp1_mux_clk[i].id)
			lsp1_hw_onecell_data.hws[lsp1_mux_clk[i].id] =
					&lsp0_mux_clk[i].mux.hw;

		lsp1_mux_clk[i].mux.reg += (uintptr_t)reg_base;
		ret = clk_hw_register(NULL, &lsp1_mux_clk[i].mux.hw);
		if (ret) {
			pr_warn("lsp1 clk %s init error!\n",
				lsp1_mux_clk[i].mux.hw.init->name);
		}
	}

	for (i = 0; i < ARRAY_SIZE(lsp1_gate_clk); i++) {
		if (lsp1_gate_clk[i].id)
			lsp1_hw_onecell_data.hws[lsp1_gate_clk[i].id] =
					&lsp1_gate_clk[i].gate.hw;

		lsp1_gate_clk[i].gate.reg += (uintptr_t)reg_base;
		ret = clk_hw_register(NULL, &lsp1_gate_clk[i].gate.hw);
		if (ret) {
			pr_warn("lsp1 clk %s init error!\n",
				lsp1_gate_clk[i].gate.hw.init->name);
		}
	}

	for (i = 0; i < ARRAY_SIZE(lsp1_div_clk); i++) {
		if (lsp1_div_clk[i].id)
			lsp1_hw_onecell_data.hws[lsp1_div_clk[i].id] =
					&lsp1_div_clk[i].div.hw;

		lsp1_div_clk[i].div.reg += (uintptr_t)reg_base;
		ret = clk_hw_register(NULL, &lsp1_div_clk[i].div.hw);
		if (ret) {
			pr_warn("lsp1 clk %s init error!\n",
				lsp1_div_clk[i].div.hw.init->name);
		}
	}

	if (of_clk_add_hw_provider(np, of_clk_hw_onecell_get, &lsp1_hw_onecell_data))
		panic("could not register clk provider\n");
	pr_info("lsp1-clk init over, nr:%d\n", LSP1_NR_CLKS);

	return 0;
}

static const struct of_device_id zx_clkc_match_table[] = {
	{ .compatible = "zte,zx296718-topcrm", .data = &top_clocks_init },
	{ .compatible = "zte,zx296718-lsp0crm", .data = &lsp0_clocks_init },
	{ .compatible = "zte,zx296718-lsp1crm", .data = &lsp1_clocks_init },
	{ }
};

static int zx_clkc_probe(struct platform_device *pdev)
{
	int (*init_fn)(struct device_node *np);
	struct device_node *np = pdev->dev.of_node;

	init_fn = of_device_get_match_data(&pdev->dev);
	if (!init_fn) {
		dev_err(&pdev->dev, "Error: No device match found\n");
		return -ENODEV;
	}

	return init_fn(np);
}

static struct platform_driver zx_clk_driver = {
	.probe		= zx_clkc_probe,
	.driver		= {
		.name	= "zx296718-clkc",
		.of_match_table = zx_clkc_match_table,
	},
};

static int __init zx_clk_init(void)
{
	return platform_driver_register(&zx_clk_driver);
}
core_initcall(zx_clk_init);
