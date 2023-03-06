// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Chen Zhong <chen.zhong@mediatek.com>
 *	   Sean Wang <sean.wang@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-cpumux.h"
#include "clk-gate.h"
#include "clk-mtk.h"
#include "clk-pll.h"

#include <dt-bindings/clock/mt7622-clk.h>
#include <linux/clk.h> /* for consumer */

#define MT7622_PLL_FMAX		(2500UL * MHZ)
#define CON0_MT7622_RST_BAR	BIT(27)

#define PLL_xtal(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits,\
			_pd_reg, _pd_shift, _tuner_reg, _pcw_reg,	\
			_pcw_shift, _div_table, _parent_name) {		\
		.id = _id,						\
		.name = _name,						\
		.reg = _reg,						\
		.pwr_reg = _pwr_reg,					\
		.en_mask = _en_mask,					\
		.flags = _flags,					\
		.rst_bar_mask = CON0_MT7622_RST_BAR,			\
		.fmax = MT7622_PLL_FMAX,				\
		.pcwbits = _pcwbits,					\
		.pd_reg = _pd_reg,					\
		.pd_shift = _pd_shift,					\
		.tuner_reg = _tuner_reg,				\
		.pcw_reg = _pcw_reg,					\
		.pcw_shift = _pcw_shift,				\
		.div_table = _div_table,				\
		.parent_name = _parent_name,				\
	}

#define PLL(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits,	\
			_pd_reg, _pd_shift, _tuner_reg, _pcw_reg,	\
			_pcw_shift)					\
	PLL_xtal(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits,\
		 _pd_reg, _pd_shift, _tuner_reg, _pcw_reg, _pcw_shift,  \
		 NULL, "clkxtal")

#define GATE_APMIXED_AO(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &apmixed_cg_regs, _shift,	\
		 &mtk_clk_gate_ops_no_setclr_inv, CLK_IS_CRITICAL)

#define GATE_INFRA(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &infra_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_TOP0(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &top0_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr)

#define GATE_TOP1(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &top1_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr)

#define GATE_PERI0(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &peri0_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_PERI0_AO(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &peri0_cg_regs, _shift,	\
		 &mtk_clk_gate_ops_setclr, CLK_IS_CRITICAL)

#define GATE_PERI1(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &peri1_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static DEFINE_SPINLOCK(mt7622_clk_lock);

static const char * const infra_mux1_parents[] = {
	"clkxtal",
	"armpll",
	"main_core_en",
	"armpll"
};

static const char * const axi_parents[] = {
	"clkxtal",
	"syspll1_d2",
	"syspll_d5",
	"syspll1_d4",
	"univpll_d5",
	"univpll2_d2",
	"univpll_d7"
};

static const char * const mem_parents[] = {
	"clkxtal",
	"dmpll_ck"
};

static const char * const ddrphycfg_parents[] = {
	"clkxtal",
	"syspll1_d8"
};

static const char * const eth_parents[] = {
	"clkxtal",
	"syspll1_d2",
	"univpll1_d2",
	"syspll1_d4",
	"univpll_d5",
	"clk_null",
	"univpll_d7"
};

static const char * const pwm_parents[] = {
	"clkxtal",
	"univpll2_d4"
};

static const char * const f10m_ref_parents[] = {
	"clkxtal",
	"syspll4_d16"
};

static const char * const nfi_infra_parents[] = {
	"clkxtal",
	"clkxtal",
	"clkxtal",
	"clkxtal",
	"clkxtal",
	"clkxtal",
	"clkxtal",
	"clkxtal",
	"univpll2_d8",
	"syspll1_d8",
	"univpll1_d8",
	"syspll4_d2",
	"univpll2_d4",
	"univpll3_d2",
	"syspll1_d4"
};

static const char * const flash_parents[] = {
	"clkxtal",
	"univpll_d80_d4",
	"syspll2_d8",
	"syspll3_d4",
	"univpll3_d4",
	"univpll1_d8",
	"syspll2_d4",
	"univpll2_d4"
};

static const char * const uart_parents[] = {
	"clkxtal",
	"univpll2_d8"
};

static const char * const spi0_parents[] = {
	"clkxtal",
	"syspll3_d2",
	"clkxtal",
	"syspll2_d4",
	"syspll4_d2",
	"univpll2_d4",
	"univpll1_d8",
	"clkxtal"
};

static const char * const spi1_parents[] = {
	"clkxtal",
	"syspll3_d2",
	"clkxtal",
	"syspll4_d4",
	"syspll4_d2",
	"univpll2_d4",
	"univpll1_d8",
	"clkxtal"
};

static const char * const msdc30_0_parents[] = {
	"clkxtal",
	"univpll2_d16",
	"univ48m"
};

static const char * const a1sys_hp_parents[] = {
	"clkxtal",
	"aud1pll_ck",
	"aud2pll_ck",
	"clkxtal"
};

static const char * const intdir_parents[] = {
	"clkxtal",
	"syspll_d2",
	"univpll_d2",
	"sgmiipll_ck"
};

static const char * const aud_intbus_parents[] = {
	"clkxtal",
	"syspll1_d4",
	"syspll4_d2",
	"syspll3_d2"
};

static const char * const pmicspi_parents[] = {
	"clkxtal",
	"clk_null",
	"clk_null",
	"clk_null",
	"clk_null",
	"univpll2_d16"
};

static const char * const atb_parents[] = {
	"clkxtal",
	"syspll1_d2",
	"syspll_d5"
};

static const char * const audio_parents[] = {
	"clkxtal",
	"syspll3_d4",
	"syspll4_d4",
	"univpll1_d16"
};

static const char * const usb20_parents[] = {
	"clkxtal",
	"univpll3_d4",
	"syspll1_d8",
	"clkxtal"
};

static const char * const aud1_parents[] = {
	"clkxtal",
	"aud1pll_ck"
};

static const char * const aud2_parents[] = {
	"clkxtal",
	"aud2pll_ck"
};

static const char * const asm_l_parents[] = {
	"clkxtal",
	"syspll_d5",
	"univpll2_d2",
	"univpll2_d4"
};

static const char * const apll1_ck_parents[] = {
	"aud1_sel",
	"aud2_sel"
};

static const char * const peribus_ck_parents[] = {
	"syspll1_d8",
	"syspll1_d4"
};

static const struct mtk_gate_regs apmixed_cg_regs = {
	.set_ofs = 0x8,
	.clr_ofs = 0x8,
	.sta_ofs = 0x8,
};

static const struct mtk_gate_regs infra_cg_regs = {
	.set_ofs = 0x40,
	.clr_ofs = 0x44,
	.sta_ofs = 0x48,
};

static const struct mtk_gate_regs top0_cg_regs = {
	.set_ofs = 0x120,
	.clr_ofs = 0x120,
	.sta_ofs = 0x120,
};

static const struct mtk_gate_regs top1_cg_regs = {
	.set_ofs = 0x128,
	.clr_ofs = 0x128,
	.sta_ofs = 0x128,
};

static const struct mtk_gate_regs peri0_cg_regs = {
	.set_ofs = 0x8,
	.clr_ofs = 0x10,
	.sta_ofs = 0x18,
};

static const struct mtk_gate_regs peri1_cg_regs = {
	.set_ofs = 0xC,
	.clr_ofs = 0x14,
	.sta_ofs = 0x1C,
};

static const struct mtk_pll_data plls[] = {
	PLL(CLK_APMIXED_ARMPLL, "armpll", 0x0200, 0x020C, 0,
	    PLL_AO, 21, 0x0204, 24, 0, 0x0204, 0),
	PLL(CLK_APMIXED_MAINPLL, "mainpll", 0x0210, 0x021C, 0,
	    HAVE_RST_BAR, 21, 0x0214, 24, 0, 0x0214, 0),
	PLL(CLK_APMIXED_UNIV2PLL, "univ2pll", 0x0220, 0x022C, 0,
	    HAVE_RST_BAR, 7, 0x0224, 24, 0, 0x0224, 14),
	PLL(CLK_APMIXED_ETH1PLL, "eth1pll", 0x0300, 0x0310, 0,
	    0, 21, 0x0300, 1, 0, 0x0304, 0),
	PLL(CLK_APMIXED_ETH2PLL, "eth2pll", 0x0314, 0x0320, 0,
	    0, 21, 0x0314, 1, 0, 0x0318, 0),
	PLL(CLK_APMIXED_AUD1PLL, "aud1pll", 0x0324, 0x0330, 0,
	    0, 31, 0x0324, 1, 0, 0x0328, 0),
	PLL(CLK_APMIXED_AUD2PLL, "aud2pll", 0x0334, 0x0340, 0,
	    0, 31, 0x0334, 1, 0, 0x0338, 0),
	PLL(CLK_APMIXED_TRGPLL, "trgpll", 0x0344, 0x0354, 0,
	    0, 21, 0x0344, 1, 0, 0x0348, 0),
	PLL(CLK_APMIXED_SGMIPLL, "sgmipll", 0x0358, 0x0368, 0,
	    0, 21, 0x0358, 1, 0, 0x035C, 0),
};

static const struct mtk_gate apmixed_clks[] = {
	GATE_APMIXED_AO(CLK_APMIXED_MAIN_CORE_EN, "main_core_en", "mainpll", 5),
};

static const struct mtk_gate infra_clks[] = {
	GATE_INFRA(CLK_INFRA_DBGCLK_PD, "infra_dbgclk_pd", "axi_sel", 0),
	GATE_INFRA(CLK_INFRA_TRNG, "trng_ck", "axi_sel", 2),
	GATE_INFRA(CLK_INFRA_AUDIO_PD, "infra_audio_pd", "aud_intbus_sel", 5),
	GATE_INFRA(CLK_INFRA_IRRX_PD, "infra_irrx_pd", "irrx_sel", 16),
	GATE_INFRA(CLK_INFRA_APXGPT_PD, "infra_apxgpt_pd", "f10m_ref_sel", 18),
	GATE_INFRA(CLK_INFRA_PMIC_PD, "infra_pmic_pd", "pmicspi_sel", 22),
};

static const struct mtk_fixed_clk top_fixed_clks[] = {
	FIXED_CLK(CLK_TOP_TO_U2_PHY, "to_u2_phy", "clkxtal",
		  31250000),
	FIXED_CLK(CLK_TOP_TO_U2_PHY_1P, "to_u2_phy_1p", "clkxtal",
		  31250000),
	FIXED_CLK(CLK_TOP_PCIE0_PIPE_EN, "pcie0_pipe_en", "clkxtal",
		  125000000),
	FIXED_CLK(CLK_TOP_PCIE1_PIPE_EN, "pcie1_pipe_en", "clkxtal",
		  125000000),
	FIXED_CLK(CLK_TOP_SSUSB_TX250M, "ssusb_tx250m", "clkxtal",
		  250000000),
	FIXED_CLK(CLK_TOP_SSUSB_EQ_RX250M, "ssusb_eq_rx250m", "clkxtal",
		  250000000),
	FIXED_CLK(CLK_TOP_SSUSB_CDR_REF, "ssusb_cdr_ref", "clkxtal",
		  33333333),
	FIXED_CLK(CLK_TOP_SSUSB_CDR_FB, "ssusb_cdr_fb", "clkxtal",
		  50000000),
	FIXED_CLK(CLK_TOP_SATA_ASIC, "sata_asic", "clkxtal",
		  50000000),
	FIXED_CLK(CLK_TOP_SATA_RBC, "sata_rbc", "clkxtal",
		  50000000),
};

static const struct mtk_fixed_factor top_divs[] = {
	FACTOR(CLK_TOP_TO_USB3_SYS, "to_usb3_sys", "eth1pll", 1, 4),
	FACTOR(CLK_TOP_P1_1MHZ, "p1_1mhz", "eth1pll", 1, 500),
	FACTOR(CLK_TOP_4MHZ, "free_run_4mhz", "eth1pll", 1, 125),
	FACTOR(CLK_TOP_P0_1MHZ, "p0_1mhz", "eth1pll", 1, 500),
	FACTOR(CLK_TOP_TXCLK_SRC_PRE, "txclk_src_pre", "sgmiipll_d2", 1, 1),
	FACTOR(CLK_TOP_RTC, "rtc", "clkxtal", 1, 1024),
	FACTOR(CLK_TOP_MEMPLL, "mempll", "clkxtal", 32, 1),
	FACTOR(CLK_TOP_DMPLL, "dmpll_ck", "mempll", 1, 1),
	FACTOR(CLK_TOP_SYSPLL_D2, "syspll_d2", "mainpll", 1, 2),
	FACTOR(CLK_TOP_SYSPLL1_D2, "syspll1_d2", "mainpll", 1, 4),
	FACTOR(CLK_TOP_SYSPLL1_D4, "syspll1_d4", "mainpll", 1, 8),
	FACTOR(CLK_TOP_SYSPLL1_D8, "syspll1_d8", "mainpll", 1, 16),
	FACTOR(CLK_TOP_SYSPLL2_D4, "syspll2_d4", "mainpll", 1, 12),
	FACTOR(CLK_TOP_SYSPLL2_D8, "syspll2_d8", "mainpll", 1, 24),
	FACTOR(CLK_TOP_SYSPLL_D5, "syspll_d5", "mainpll", 1, 5),
	FACTOR(CLK_TOP_SYSPLL3_D2, "syspll3_d2", "mainpll", 1, 10),
	FACTOR(CLK_TOP_SYSPLL3_D4, "syspll3_d4", "mainpll", 1, 20),
	FACTOR(CLK_TOP_SYSPLL4_D2, "syspll4_d2", "mainpll", 1, 14),
	FACTOR(CLK_TOP_SYSPLL4_D4, "syspll4_d4", "mainpll", 1, 28),
	FACTOR(CLK_TOP_SYSPLL4_D16, "syspll4_d16", "mainpll", 1, 112),
	FACTOR(CLK_TOP_UNIVPLL, "univpll", "univ2pll", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL_D2, "univpll_d2", "univpll", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL1_D2, "univpll1_d2", "univpll", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL1_D4, "univpll1_d4", "univpll", 1, 8),
	FACTOR(CLK_TOP_UNIVPLL1_D8, "univpll1_d8", "univpll", 1, 16),
	FACTOR(CLK_TOP_UNIVPLL1_D16, "univpll1_d16", "univpll", 1, 32),
	FACTOR(CLK_TOP_UNIVPLL2_D2, "univpll2_d2", "univpll", 1, 6),
	FACTOR(CLK_TOP_UNIVPLL2_D4, "univpll2_d4", "univpll", 1, 12),
	FACTOR(CLK_TOP_UNIVPLL2_D8, "univpll2_d8", "univpll", 1, 24),
	FACTOR(CLK_TOP_UNIVPLL2_D16, "univpll2_d16", "univpll", 1, 48),
	FACTOR(CLK_TOP_UNIVPLL_D5, "univpll_d5", "univpll", 1, 5),
	FACTOR(CLK_TOP_UNIVPLL3_D2, "univpll3_d2", "univpll", 1, 10),
	FACTOR(CLK_TOP_UNIVPLL3_D4, "univpll3_d4", "univpll", 1, 20),
	FACTOR(CLK_TOP_UNIVPLL3_D16, "univpll3_d16", "univpll", 1, 80),
	FACTOR(CLK_TOP_UNIVPLL_D7, "univpll_d7", "univpll", 1, 7),
	FACTOR(CLK_TOP_UNIVPLL_D80_D4, "univpll_d80_d4", "univpll", 1, 320),
	FACTOR(CLK_TOP_UNIV48M, "univ48m", "univpll", 1, 25),
	FACTOR(CLK_TOP_SGMIIPLL, "sgmiipll_ck", "sgmipll", 1, 1),
	FACTOR(CLK_TOP_SGMIIPLL_D2, "sgmiipll_d2", "sgmipll", 1, 2),
	FACTOR(CLK_TOP_AUD1PLL, "aud1pll_ck", "aud1pll", 1, 1),
	FACTOR(CLK_TOP_AUD2PLL, "aud2pll_ck", "aud2pll", 1, 1),
	FACTOR(CLK_TOP_AUD_I2S2_MCK, "aud_i2s2_mck", "i2s2_mck_sel", 1, 2),
	FACTOR(CLK_TOP_TO_USB3_REF, "to_usb3_ref", "univpll2_d4", 1, 4),
	FACTOR(CLK_TOP_PCIE1_MAC_EN, "pcie1_mac_en", "univpll1_d4", 1, 1),
	FACTOR(CLK_TOP_PCIE0_MAC_EN, "pcie0_mac_en", "univpll1_d4", 1, 1),
	FACTOR(CLK_TOP_ETH_500M, "eth_500m", "eth1pll", 1, 1),
};

static const struct mtk_gate top_clks[] = {
	/* TOP0 */
	GATE_TOP0(CLK_TOP_APLL1_DIV_PD, "apll1_ck_div_pd", "apll1_ck_div", 0),
	GATE_TOP0(CLK_TOP_APLL2_DIV_PD, "apll2_ck_div_pd", "apll2_ck_div", 1),
	GATE_TOP0(CLK_TOP_I2S0_MCK_DIV_PD, "i2s0_mck_div_pd", "i2s0_mck_div",
		  2),
	GATE_TOP0(CLK_TOP_I2S1_MCK_DIV_PD, "i2s1_mck_div_pd", "i2s1_mck_div",
		  3),
	GATE_TOP0(CLK_TOP_I2S2_MCK_DIV_PD, "i2s2_mck_div_pd", "i2s2_mck_div",
		  4),
	GATE_TOP0(CLK_TOP_I2S3_MCK_DIV_PD, "i2s3_mck_div_pd", "i2s3_mck_div",
		  5),

	/* TOP1 */
	GATE_TOP1(CLK_TOP_A1SYS_HP_DIV_PD, "a1sys_div_pd", "a1sys_div", 0),
	GATE_TOP1(CLK_TOP_A2SYS_HP_DIV_PD, "a2sys_div_pd", "a2sys_div", 16),
};

static const struct mtk_clk_divider top_adj_divs[] = {
	DIV_ADJ(CLK_TOP_APLL1_DIV, "apll1_ck_div", "apll1_ck_sel",
		0x120, 24, 3),
	DIV_ADJ(CLK_TOP_APLL2_DIV, "apll2_ck_div", "apll2_ck_sel",
		0x120, 28, 3),
	DIV_ADJ(CLK_TOP_I2S0_MCK_DIV, "i2s0_mck_div", "i2s0_mck_sel",
		0x124, 0, 7),
	DIV_ADJ(CLK_TOP_I2S1_MCK_DIV, "i2s1_mck_div", "i2s1_mck_sel",
		0x124, 8, 7),
	DIV_ADJ(CLK_TOP_I2S2_MCK_DIV, "i2s2_mck_div", "aud_i2s2_mck",
		0x124, 16, 7),
	DIV_ADJ(CLK_TOP_I2S3_MCK_DIV, "i2s3_mck_div", "i2s3_mck_sel",
		0x124, 24, 7),
	DIV_ADJ(CLK_TOP_A1SYS_HP_DIV, "a1sys_div", "a1sys_hp_sel",
		0x128, 8, 7),
	DIV_ADJ(CLK_TOP_A2SYS_HP_DIV, "a2sys_div", "a2sys_hp_sel",
		0x128, 24, 7),
};

static const struct mtk_gate peri_clks[] = {
	/* PERI0 */
	GATE_PERI0(CLK_PERI_THERM_PD, "peri_therm_pd", "axi_sel", 1),
	GATE_PERI0(CLK_PERI_PWM1_PD, "peri_pwm1_pd", "clkxtal", 2),
	GATE_PERI0(CLK_PERI_PWM2_PD, "peri_pwm2_pd", "clkxtal", 3),
	GATE_PERI0(CLK_PERI_PWM3_PD, "peri_pwm3_pd", "clkxtal", 4),
	GATE_PERI0(CLK_PERI_PWM4_PD, "peri_pwm4_pd", "clkxtal", 5),
	GATE_PERI0(CLK_PERI_PWM5_PD, "peri_pwm5_pd", "clkxtal", 6),
	GATE_PERI0(CLK_PERI_PWM6_PD, "peri_pwm6_pd", "clkxtal", 7),
	GATE_PERI0(CLK_PERI_PWM7_PD, "peri_pwm7_pd", "clkxtal", 8),
	GATE_PERI0(CLK_PERI_PWM_PD, "peri_pwm_pd", "clkxtal", 9),
	GATE_PERI0(CLK_PERI_AP_DMA_PD, "peri_ap_dma_pd", "axi_sel", 12),
	GATE_PERI0(CLK_PERI_MSDC30_0_PD, "peri_msdc30_0", "msdc30_0_sel", 13),
	GATE_PERI0(CLK_PERI_MSDC30_1_PD, "peri_msdc30_1", "msdc30_1_sel", 14),
	GATE_PERI0_AO(CLK_PERI_UART0_PD, "peri_uart0_pd", "axi_sel", 17),
	GATE_PERI0(CLK_PERI_UART1_PD, "peri_uart1_pd", "axi_sel", 18),
	GATE_PERI0(CLK_PERI_UART2_PD, "peri_uart2_pd", "axi_sel", 19),
	GATE_PERI0(CLK_PERI_UART3_PD, "peri_uart3_pd", "axi_sel", 20),
	GATE_PERI0(CLK_PERI_UART4_PD, "peri_uart4_pd", "axi_sel", 21),
	GATE_PERI0(CLK_PERI_BTIF_PD, "peri_btif_pd", "axi_sel", 22),
	GATE_PERI0(CLK_PERI_I2C0_PD, "peri_i2c0_pd", "axi_sel", 23),
	GATE_PERI0(CLK_PERI_I2C1_PD, "peri_i2c1_pd", "axi_sel", 24),
	GATE_PERI0(CLK_PERI_I2C2_PD, "peri_i2c2_pd", "axi_sel", 25),
	GATE_PERI0(CLK_PERI_SPI1_PD, "peri_spi1_pd", "spi1_sel", 26),
	GATE_PERI0(CLK_PERI_AUXADC_PD, "peri_auxadc_pd", "clkxtal", 27),
	GATE_PERI0(CLK_PERI_SPI0_PD, "peri_spi0_pd", "spi0_sel", 28),
	GATE_PERI0(CLK_PERI_SNFI_PD, "peri_snfi_pd", "nfi_infra_sel", 29),
	GATE_PERI0(CLK_PERI_NFI_PD, "peri_nfi_pd", "axi_sel", 30),
	GATE_PERI0(CLK_PERI_NFIECC_PD, "peri_nfiecc_pd", "axi_sel", 31),

	/* PERI1 */
	GATE_PERI1(CLK_PERI_FLASH_PD, "peri_flash_pd", "flash_sel", 1),
	GATE_PERI1(CLK_PERI_IRTX_PD, "peri_irtx_pd", "irtx_sel", 2),
};

static struct mtk_composite infra_muxes[] = {
	MUX(CLK_INFRA_MUX1_SEL, "infra_mux1_sel", infra_mux1_parents,
	    0x000, 2, 2),
};

static struct mtk_composite top_muxes[] = {
	/* CLK_CFG_0 */
	MUX_GATE_FLAGS(CLK_TOP_AXI_SEL, "axi_sel", axi_parents,
		       0x040, 0, 3, 7, CLK_IS_CRITICAL),
	MUX_GATE_FLAGS(CLK_TOP_MEM_SEL, "mem_sel", mem_parents,
		       0x040, 8, 1, 15, CLK_IS_CRITICAL),
	MUX_GATE_FLAGS(CLK_TOP_DDRPHYCFG_SEL, "ddrphycfg_sel", ddrphycfg_parents,
		       0x040, 16, 1, 23, CLK_IS_CRITICAL),
	MUX_GATE(CLK_TOP_ETH_SEL, "eth_sel", eth_parents,
		 0x040, 24, 3, 31),

	/* CLK_CFG_1 */
	MUX_GATE(CLK_TOP_PWM_SEL, "pwm_sel", pwm_parents,
		 0x050, 0, 2, 7),
	MUX_GATE(CLK_TOP_F10M_REF_SEL, "f10m_ref_sel", f10m_ref_parents,
		 0x050, 8, 1, 15),
	MUX_GATE(CLK_TOP_NFI_INFRA_SEL, "nfi_infra_sel", nfi_infra_parents,
		 0x050, 16, 4, 23),
	MUX_GATE(CLK_TOP_FLASH_SEL, "flash_sel", flash_parents,
		 0x050, 24, 3, 31),

	/* CLK_CFG_2 */
	MUX_GATE(CLK_TOP_UART_SEL, "uart_sel", uart_parents,
		 0x060, 0, 1, 7),
	MUX_GATE(CLK_TOP_SPI0_SEL, "spi0_sel", spi0_parents,
		 0x060, 8, 3, 15),
	MUX_GATE(CLK_TOP_SPI1_SEL, "spi1_sel", spi1_parents,
		 0x060, 16, 3, 23),
	MUX_GATE(CLK_TOP_MSDC50_0_SEL, "msdc50_0_sel", uart_parents,
		 0x060, 24, 3, 31),

	/* CLK_CFG_3 */
	MUX_GATE(CLK_TOP_MSDC30_0_SEL, "msdc30_0_sel", msdc30_0_parents,
		 0x070, 0, 3, 7),
	MUX_GATE(CLK_TOP_MSDC30_1_SEL, "msdc30_1_sel", msdc30_0_parents,
		 0x070, 8, 3, 15),
	MUX_GATE(CLK_TOP_A1SYS_HP_SEL, "a1sys_hp_sel", a1sys_hp_parents,
		 0x070, 16, 2, 23),
	MUX_GATE(CLK_TOP_A2SYS_HP_SEL, "a2sys_hp_sel", a1sys_hp_parents,
		 0x070, 24, 2, 31),

	/* CLK_CFG_4 */
	MUX_GATE(CLK_TOP_INTDIR_SEL, "intdir_sel", intdir_parents,
		 0x080, 0, 2, 7),
	MUX_GATE(CLK_TOP_AUD_INTBUS_SEL, "aud_intbus_sel", aud_intbus_parents,
		 0x080, 8, 2, 15),
	MUX_GATE(CLK_TOP_PMICSPI_SEL, "pmicspi_sel", pmicspi_parents,
		 0x080, 16, 3, 23),
	MUX_GATE(CLK_TOP_SCP_SEL, "scp_sel", ddrphycfg_parents,
		 0x080, 24, 2, 31),

	/* CLK_CFG_5 */
	MUX_GATE(CLK_TOP_ATB_SEL, "atb_sel", atb_parents,
		 0x090, 0, 2, 7),
	MUX_GATE(CLK_TOP_HIF_SEL, "hif_sel", eth_parents,
		 0x090, 8, 3, 15),
	MUX_GATE(CLK_TOP_AUDIO_SEL, "audio_sel", audio_parents,
		 0x090, 16, 2, 23),
	MUX_GATE(CLK_TOP_U2_SEL, "usb20_sel", usb20_parents,
		 0x090, 24, 2, 31),

	/* CLK_CFG_6 */
	MUX_GATE(CLK_TOP_AUD1_SEL, "aud1_sel", aud1_parents,
		 0x0A0, 0, 1, 7),
	MUX_GATE(CLK_TOP_AUD2_SEL, "aud2_sel", aud2_parents,
		 0x0A0, 8, 1, 15),
	MUX_GATE(CLK_TOP_IRRX_SEL, "irrx_sel", f10m_ref_parents,
		 0x0A0, 16, 1, 23),
	MUX_GATE(CLK_TOP_IRTX_SEL, "irtx_sel", f10m_ref_parents,
		 0x0A0, 24, 1, 31),

	/* CLK_CFG_7 */
	MUX_GATE(CLK_TOP_ASM_L_SEL, "asm_l_sel", asm_l_parents,
		 0x0B0, 0, 2, 7),
	MUX_GATE(CLK_TOP_ASM_M_SEL, "asm_m_sel", asm_l_parents,
		 0x0B0, 8, 2, 15),
	MUX_GATE(CLK_TOP_ASM_H_SEL, "asm_h_sel", asm_l_parents,
		 0x0B0, 16, 2, 23),

	/* CLK_AUDDIV_0 */
	MUX(CLK_TOP_APLL1_SEL, "apll1_ck_sel", apll1_ck_parents,
	    0x120, 6, 1),
	MUX(CLK_TOP_APLL2_SEL, "apll2_ck_sel", apll1_ck_parents,
	    0x120, 7, 1),
	MUX(CLK_TOP_I2S0_MCK_SEL, "i2s0_mck_sel", apll1_ck_parents,
	    0x120, 8, 1),
	MUX(CLK_TOP_I2S1_MCK_SEL, "i2s1_mck_sel", apll1_ck_parents,
	    0x120, 9, 1),
	MUX(CLK_TOP_I2S2_MCK_SEL, "i2s2_mck_sel", apll1_ck_parents,
	    0x120, 10, 1),
	MUX(CLK_TOP_I2S3_MCK_SEL, "i2s3_mck_sel", apll1_ck_parents,
	    0x120, 11, 1),
};

static struct mtk_composite peri_muxes[] = {
	/* PERI_GLOBALCON_CKSEL */
	MUX(CLK_PERIBUS_SEL, "peribus_ck_sel", peribus_ck_parents, 0x05C, 0, 1),
};

static u16 infrasys_rst_ofs[] = { 0x30, };
static u16 pericfg_rst_ofs[] = { 0x0, 0x4, };

static const struct mtk_clk_rst_desc clk_rst_desc[] = {
	/* infrasys */
	{
		.version = MTK_RST_SIMPLE,
		.rst_bank_ofs = infrasys_rst_ofs,
		.rst_bank_nr = ARRAY_SIZE(infrasys_rst_ofs),
	},
	/* pericfg */
	{
		.version = MTK_RST_SIMPLE,
		.rst_bank_ofs = pericfg_rst_ofs,
		.rst_bank_nr = ARRAY_SIZE(pericfg_rst_ofs),
	},
};

static int mtk_topckgen_init(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	void __iomem *base;
	struct device_node *node = pdev->dev.of_node;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk_data = mtk_alloc_clk_data(CLK_TOP_NR_CLK);

	mtk_clk_register_fixed_clks(top_fixed_clks, ARRAY_SIZE(top_fixed_clks),
				    clk_data);

	mtk_clk_register_factors(top_divs, ARRAY_SIZE(top_divs),
				 clk_data);

	mtk_clk_register_composites(&pdev->dev, top_muxes,
				    ARRAY_SIZE(top_muxes), base,
				    &mt7622_clk_lock, clk_data);

	mtk_clk_register_dividers(top_adj_divs, ARRAY_SIZE(top_adj_divs),
				  base, &mt7622_clk_lock, clk_data);

	mtk_clk_register_gates(&pdev->dev, node, top_clks,
			       ARRAY_SIZE(top_clks), clk_data);

	return of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
}

static int mtk_infrasys_init(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct clk_hw_onecell_data *clk_data;
	int r;

	clk_data = mtk_alloc_clk_data(CLK_INFRA_NR_CLK);

	mtk_clk_register_gates(&pdev->dev, node, infra_clks,
			       ARRAY_SIZE(infra_clks), clk_data);

	mtk_clk_register_cpumuxes(&pdev->dev, node, infra_muxes,
				  ARRAY_SIZE(infra_muxes), clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get,
				   clk_data);
	if (r)
		return r;

	mtk_register_reset_controller_with_dev(&pdev->dev, &clk_rst_desc[0]);

	return 0;
}

static int mtk_apmixedsys_init(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;

	clk_data = mtk_alloc_clk_data(CLK_APMIXED_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	mtk_clk_register_plls(node, plls, ARRAY_SIZE(plls),
			      clk_data);

	mtk_clk_register_gates(&pdev->dev, node, apmixed_clks,
			       ARRAY_SIZE(apmixed_clks), clk_data);

	return of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
}

static int mtk_pericfg_init(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	void __iomem *base;
	int r;
	struct device_node *node = pdev->dev.of_node;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk_data = mtk_alloc_clk_data(CLK_PERI_NR_CLK);

	mtk_clk_register_gates(&pdev->dev, node, peri_clks,
			       ARRAY_SIZE(peri_clks), clk_data);

	mtk_clk_register_composites(&pdev->dev, peri_muxes,
				    ARRAY_SIZE(peri_muxes), base,
				    &mt7622_clk_lock, clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (r)
		return r;

	mtk_register_reset_controller_with_dev(&pdev->dev, &clk_rst_desc[1]);

	return 0;
}

static const struct of_device_id of_match_clk_mt7622[] = {
	{
		.compatible = "mediatek,mt7622-apmixedsys",
		.data = mtk_apmixedsys_init,
	}, {
		.compatible = "mediatek,mt7622-infracfg",
		.data = mtk_infrasys_init,
	}, {
		.compatible = "mediatek,mt7622-topckgen",
		.data = mtk_topckgen_init,
	}, {
		.compatible = "mediatek,mt7622-pericfg",
		.data = mtk_pericfg_init,
	}, {
		/* sentinel */
	}
};

static int clk_mt7622_probe(struct platform_device *pdev)
{
	int (*clk_init)(struct platform_device *);
	int r;

	clk_init = of_device_get_match_data(&pdev->dev);
	if (!clk_init)
		return -EINVAL;

	r = clk_init(pdev);
	if (r)
		dev_err(&pdev->dev,
			"could not register clock provider: %s: %d\n",
			pdev->name, r);

	return r;
}

static struct platform_driver clk_mt7622_drv = {
	.probe = clk_mt7622_probe,
	.driver = {
		.name = "clk-mt7622",
		.of_match_table = of_match_clk_mt7622,
	},
};

static int clk_mt7622_init(void)
{
	return platform_driver_register(&clk_mt7622_drv);
}

arch_initcall(clk_mt7622_init);
