// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Copyright (C) 2026 Amlogic, Inc. All rights reserved
 */

#include <dt-bindings/clock/amlogic,a9-peripherals-clkc.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "clk-regmap.h"
#include "clk-dualdiv.h"
#include "meson-clkc-utils.h"
#include "vid-pll-div.h"

#define SYS_CLK_EN0_REG0			0x30
#define SYS_CLK_EN0_REG1			0x34
#define SYS_CLK_EN0_REG2			0x38
#define SYS_CLK_EN0_REG3			0x3c
#define SD_EMMC_CLK_CTRL0			0x90
#define SD_EMMC_CLK_CTRL1			0x94
#define PWM_CLK_H_CTRL				0xbc
#define PWM_CLK_I_CTRL				0xc0
#define PWM_CLK_J_CTRL				0xc4
#define PWM_CLK_K_CTRL				0xc8
#define PWM_CLK_L_CTRL				0xcc
#define PWM_CLK_M_CTRL				0xd0
#define PWM_CLK_N_CTRL				0xd4
#define SPISG_CLK_CTRL				0x100
#define SPISG_CLK_CTRL1				0x104
#define SAR_CLK_CTRL				0x150
#define AMFC_CLK_CTRL				0x154
#define NNA_CLK_CTRL				0x15c
#define USB_CLK_CTRL				0x160
#define PCIE_TL_CLK_CTRL			0x164
#define CMPR_CLK_CTRL				0x168
#define DEWARP_CLK_CTRL				0x16c
#define SC_CLK_CTRL				0x170
#define DPTX_CLK_CTRL				0x178
#define ISP_CLK_CTRL				0x17c
#define CVE_CLK_CTRL				0x180
#define PP_CLK_CTRL				0x184
#define GLB_CLK_CTRL				0x188
#define USB_CLK_CTRL0				0x18c
#define USB_CLK_CTRL1				0x190
#define CAN_CLK_CTRL				0x194
#define CAN_CLK_CTRL1				0x198
#define I3C_CLK_CTRL				0x19c
#define TS_CLK_CTRL				0x1a0
#define ETH_CLK_CTRL				0x1a4
#define GEN_CLK_CTRL				0x1a8
#define CLK12_24_CTRL				0x1ac
#define MALI_CLK_CTRL				0x200
#define MALI_STACK_CLK_CTRL			0x204
#define DSPA_CLK_CTRL				0x220
#define HEVCF_CLK_CTRL				0x240
#define HCODEC_CLK_CTRL				0x244
#define VPU_CLK_CTRL				0x260
#define VAPB_CLK_CTRL				0x268
#define VPU_CLKB_CTRL				0x280
#define HDMI_CLK_CTRL				0x284
#define HTX_CLK_CTRL				0x28c
#define HTX_CLK_CTRL1				0x290
#define HRX_CLK_CTRL				0x294
#define HRX_CLK_CTRL1				0x298
#define HRX_CLK_CTRL2				0x29c
#define HRX_CLK_CTRL3				0x2a0
#define VID_LOCK_CLK_CTRL			0x2a4
#define VDIN_MEAS_CLK_CTRL			0x2a8
#define VID_PLL_CLK_DIV				0x2b0
#define VID_CLK_CTRL				0x2c0
#define VID_CLK_CTRL2				0x2c4
#define VID_CLK_DIV				0x2c8
#define VIID_CLK_DIV				0x2cc
#define VIID_CLK_CTRL				0x2d0
#define MIPI_CSI_PHY_CLK_CTRL			0x2e0
#define DSI_MEAS_CLK_CTRL			0x2f4

#define A9_COMP_SEL(_name, _reg, _shift, _mask, _pdata, _table) \
	MESON_COMP_SEL(a9_, _name, _reg, _shift, _mask, _pdata, _table, 0, 0)

#define A9_COMP_DIV(_name, _reg, _shift, _width) \
	MESON_COMP_DIV(a9_, _name, _reg, _shift, _width, 0, CLK_SET_RATE_PARENT)

#define A9_COMP_GATE(_name, _reg, _bit, _iflags) \
	MESON_COMP_GATE(a9_, _name, _reg, _bit, CLK_SET_RATE_PARENT | (_iflags))

static const struct clk_parent_data a9_sys_pclk_parents = { .fw_name = "sys" };

#define A9_SYS_PCLK(_name, _reg, _bit) \
	MESON_PCLK(a9_##_name, _reg, _bit, &a9_sys_pclk_parents, 0)

static A9_SYS_PCLK(sys_am_axi,		SYS_CLK_EN0_REG0, 0);
static A9_SYS_PCLK(sys_dos,		SYS_CLK_EN0_REG0, 1);
static A9_SYS_PCLK(sys_mipi_dsi0,	SYS_CLK_EN0_REG0, 3);
static A9_SYS_PCLK(sys_eth_phy,		SYS_CLK_EN0_REG0, 4);
static A9_SYS_PCLK(sys_amfc,		SYS_CLK_EN0_REG0, 5);
static A9_SYS_PCLK(sys_mali,		SYS_CLK_EN0_REG0, 6);
static A9_SYS_PCLK(sys_nna,		SYS_CLK_EN0_REG0, 7);
static A9_SYS_PCLK(sys_eth_axi,		SYS_CLK_EN0_REG0, 8);
static A9_SYS_PCLK(sys_dp_apb,		SYS_CLK_EN0_REG0, 9);
static A9_SYS_PCLK(sys_edptx_apb,	SYS_CLK_EN0_REG0, 10);
static A9_SYS_PCLK(sys_u3hsg,		SYS_CLK_EN0_REG0, 11);
static A9_SYS_PCLK(sys_aucpu,		SYS_CLK_EN0_REG0, 14);
static A9_SYS_PCLK(sys_glb,		SYS_CLK_EN0_REG0, 15);
static A9_SYS_PCLK(sys_combo_dphy_apb,	SYS_CLK_EN0_REG0, 17);
static A9_SYS_PCLK(sys_hdmirx_apb,	SYS_CLK_EN0_REG0, 18);
static A9_SYS_PCLK(sys_hdmirx_pclk,	SYS_CLK_EN0_REG0, 19);
static A9_SYS_PCLK(sys_mipi_dsi0_phy,	SYS_CLK_EN0_REG0, 20);
static A9_SYS_PCLK(sys_can0,		SYS_CLK_EN0_REG0, 21);
static A9_SYS_PCLK(sys_can1,		SYS_CLK_EN0_REG0, 22);
static A9_SYS_PCLK(sys_sd_emmc_a,	SYS_CLK_EN0_REG0, 24);
static A9_SYS_PCLK(sys_sd_emmc_b,	SYS_CLK_EN0_REG0, 25);
static A9_SYS_PCLK(sys_sd_emmc_c,	SYS_CLK_EN0_REG0, 26);
static A9_SYS_PCLK(sys_sc,		SYS_CLK_EN0_REG0, 27);
static A9_SYS_PCLK(sys_acodec,		SYS_CLK_EN0_REG0, 28);
static A9_SYS_PCLK(sys_mipi_isp,	SYS_CLK_EN0_REG0, 29);
static A9_SYS_PCLK(sys_msr,		SYS_CLK_EN0_REG0, 30);
static A9_SYS_PCLK(sys_audio,		SYS_CLK_EN0_REG1, 0);
static A9_SYS_PCLK(sys_mipi_dsi1,	SYS_CLK_EN0_REG1, 1);
static A9_SYS_PCLK(sys_mipi_dsi1_phy,	SYS_CLK_EN0_REG1, 2);
static A9_SYS_PCLK(sys_eth,		SYS_CLK_EN0_REG1, 3);
static A9_SYS_PCLK(sys_eth_1g_mac,	SYS_CLK_EN0_REG1, 4);
static A9_SYS_PCLK(sys_uart_a,		SYS_CLK_EN0_REG1, 5);
static A9_SYS_PCLK(sys_uart_f,		SYS_CLK_EN0_REG1, 10);
static A9_SYS_PCLK(sys_ts_a55,		SYS_CLK_EN0_REG1, 11);
static A9_SYS_PCLK(sys_eth_1g_axi,	SYS_CLK_EN0_REG1, 12);
static A9_SYS_PCLK(sys_ts_dos,		SYS_CLK_EN0_REG1, 13);
static A9_SYS_PCLK(sys_u3drd_b,		SYS_CLK_EN0_REG1, 14);
static A9_SYS_PCLK(sys_ts_core,		SYS_CLK_EN0_REG1, 15);
static A9_SYS_PCLK(sys_ts_pll,		SYS_CLK_EN0_REG1, 16);
static A9_SYS_PCLK(sys_csi_dig_clkin,	SYS_CLK_EN0_REG1, 18);
static A9_SYS_PCLK(sys_cve,		SYS_CLK_EN0_REG1, 19);
static A9_SYS_PCLK(sys_ge2d,		SYS_CLK_EN0_REG1, 20);
static A9_SYS_PCLK(sys_spisg,		SYS_CLK_EN0_REG1, 21);
static A9_SYS_PCLK(sys_u2h,		SYS_CLK_EN0_REG1, 23);
static A9_SYS_PCLK(sys_pcie_mac_a,	SYS_CLK_EN0_REG1, 24);
static A9_SYS_PCLK(sys_u3drd_a,		SYS_CLK_EN0_REG1, 25);
static A9_SYS_PCLK(sys_u2drd,		SYS_CLK_EN0_REG1, 26);
static A9_SYS_PCLK(sys_pcie_phy,	SYS_CLK_EN0_REG1, 27);
static A9_SYS_PCLK(sys_pcie_mac_b,	SYS_CLK_EN0_REG1, 28);
static A9_SYS_PCLK(sys_periph,		SYS_CLK_EN0_REG1, 29);
static A9_SYS_PCLK(sys_pio,		SYS_CLK_EN0_REG2, 0);
static A9_SYS_PCLK(sys_i3c,		SYS_CLK_EN0_REG2, 1);
static A9_SYS_PCLK(sys_i2c_m_e,		SYS_CLK_EN0_REG2, 2);
static A9_SYS_PCLK(sys_i2c_m_f,		SYS_CLK_EN0_REG2, 3);
static A9_SYS_PCLK(sys_hdmitx_apb,	SYS_CLK_EN0_REG2, 4);
static A9_SYS_PCLK(sys_i2c_m_i,		SYS_CLK_EN0_REG2, 5);
static A9_SYS_PCLK(sys_i2c_m_g,		SYS_CLK_EN0_REG2, 6);
static A9_SYS_PCLK(sys_i2c_m_h,		SYS_CLK_EN0_REG2, 7);
static A9_SYS_PCLK(sys_hdmi20_aes,	SYS_CLK_EN0_REG2, 9);
static A9_SYS_PCLK(sys_csi2_host,	SYS_CLK_EN0_REG2, 16);
static A9_SYS_PCLK(sys_csi2_adapt,	SYS_CLK_EN0_REG2, 17);
static A9_SYS_PCLK(sys_dspa,		SYS_CLK_EN0_REG2, 21);
static A9_SYS_PCLK(sys_pp_dma,		SYS_CLK_EN0_REG2, 22);
static A9_SYS_PCLK(sys_pp_wrapper,	SYS_CLK_EN0_REG2, 23);
static A9_SYS_PCLK(sys_vpu_intr,	SYS_CLK_EN0_REG2, 25);
static A9_SYS_PCLK(sys_csi2_phy,	SYS_CLK_EN0_REG2, 27);
static A9_SYS_PCLK(sys_saradc,		SYS_CLK_EN0_REG2, 28);
static A9_SYS_PCLK(sys_pwm_j,		SYS_CLK_EN0_REG2, 30);
static A9_SYS_PCLK(sys_pwm_i,		SYS_CLK_EN0_REG2, 31);
static A9_SYS_PCLK(sys_pwm_h,		SYS_CLK_EN0_REG3, 0);
static A9_SYS_PCLK(sys_pwm_n,		SYS_CLK_EN0_REG3, 8);
static A9_SYS_PCLK(sys_pwm_m,		SYS_CLK_EN0_REG3, 9);
static A9_SYS_PCLK(sys_pwm_l,		SYS_CLK_EN0_REG3, 10);
static A9_SYS_PCLK(sys_pwm_k,		SYS_CLK_EN0_REG3, 11);

/* Channel 5 is unconnected. */
static u32 a9_sd_emmc_parents_val_table[] = { 0, 1, 2, 3, 4, 6, 7 };
static const struct clk_parent_data a9_sd_emmc_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "fdiv2", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "hifi0", },
	{ .fw_name = "fdiv2p5", },
	{ .fw_name = "gp1", },
	{ .fw_name = "gp0", }
};

static A9_COMP_SEL(sd_emmc_a, SD_EMMC_CLK_CTRL0, 9, 0x7, a9_sd_emmc_parents,
		   a9_sd_emmc_parents_val_table);
static A9_COMP_DIV(sd_emmc_a, SD_EMMC_CLK_CTRL0, 0, 7);
static A9_COMP_GATE(sd_emmc_a, SD_EMMC_CLK_CTRL0, 8, 0);

static A9_COMP_SEL(sd_emmc_b, SD_EMMC_CLK_CTRL0, 25, 0x7, a9_sd_emmc_parents,
		   a9_sd_emmc_parents_val_table);
static A9_COMP_DIV(sd_emmc_b, SD_EMMC_CLK_CTRL0, 16, 7);
static A9_COMP_GATE(sd_emmc_b, SD_EMMC_CLK_CTRL0, 24, 0);

static A9_COMP_SEL(sd_emmc_c, SD_EMMC_CLK_CTRL1, 9, 0x7, a9_sd_emmc_parents,
		   a9_sd_emmc_parents_val_table);
static A9_COMP_DIV(sd_emmc_c, SD_EMMC_CLK_CTRL1, 0, 7);
static A9_COMP_GATE(sd_emmc_c, SD_EMMC_CLK_CTRL1, 8, 0);

static const struct clk_parent_data a9_pwm_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv3", }
};

static A9_COMP_SEL(pwm_h, PWM_CLK_H_CTRL, 9, 0x7, a9_pwm_parents, NULL);
static A9_COMP_DIV(pwm_h, PWM_CLK_H_CTRL, 0, 8);
static A9_COMP_GATE(pwm_h, PWM_CLK_H_CTRL, 8, 0);

static A9_COMP_SEL(pwm_i, PWM_CLK_I_CTRL, 9, 0x7, a9_pwm_parents, NULL);
static A9_COMP_DIV(pwm_i, PWM_CLK_I_CTRL, 0, 8);
static A9_COMP_GATE(pwm_i, PWM_CLK_I_CTRL, 8, 0);

static A9_COMP_SEL(pwm_j, PWM_CLK_J_CTRL, 9, 0x7, a9_pwm_parents, NULL);
static A9_COMP_DIV(pwm_j, PWM_CLK_J_CTRL, 0, 8);
static A9_COMP_GATE(pwm_j, PWM_CLK_J_CTRL, 8, 0);

static A9_COMP_SEL(pwm_k, PWM_CLK_K_CTRL, 9, 0x7, a9_pwm_parents, NULL);
static A9_COMP_DIV(pwm_k, PWM_CLK_K_CTRL, 0, 8);
static A9_COMP_GATE(pwm_k, PWM_CLK_K_CTRL, 8, 0);

static A9_COMP_SEL(pwm_l, PWM_CLK_L_CTRL, 9, 0x7, a9_pwm_parents, NULL);
static A9_COMP_DIV(pwm_l, PWM_CLK_L_CTRL, 0, 8);
static A9_COMP_GATE(pwm_l, PWM_CLK_L_CTRL, 8, 0);

static A9_COMP_SEL(pwm_m, PWM_CLK_M_CTRL, 9, 0x7, a9_pwm_parents, NULL);
static A9_COMP_DIV(pwm_m, PWM_CLK_M_CTRL, 0, 8);
static A9_COMP_GATE(pwm_m, PWM_CLK_M_CTRL, 8, 0);

static A9_COMP_SEL(pwm_n, PWM_CLK_N_CTRL, 9, 0x7, a9_pwm_parents, NULL);
static A9_COMP_DIV(pwm_n, PWM_CLK_N_CTRL, 0, 8);
static A9_COMP_GATE(pwm_n, PWM_CLK_N_CTRL, 8, 0);

static const struct clk_parent_data a9_spisg_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "sys", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv2", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "fdiv7", },
	{ .fw_name = "gp0", }
};

static A9_COMP_SEL(spisg0, SPISG_CLK_CTRL, 9, 0x7, a9_spisg_parents, NULL);
static A9_COMP_DIV(spisg0, SPISG_CLK_CTRL, 0, 6);
static A9_COMP_GATE(spisg0, SPISG_CLK_CTRL, 8, 0);

static A9_COMP_SEL(spisg1, SPISG_CLK_CTRL, 25, 0x7, a9_spisg_parents, NULL);
static A9_COMP_DIV(spisg1, SPISG_CLK_CTRL, 16, 6);
static A9_COMP_GATE(spisg1, SPISG_CLK_CTRL, 24, 0);

static A9_COMP_SEL(spisg2, SPISG_CLK_CTRL1, 9, 0x7, a9_spisg_parents, NULL);
static A9_COMP_DIV(spisg2, SPISG_CLK_CTRL1, 0, 6);
static A9_COMP_GATE(spisg2, SPISG_CLK_CTRL1, 8, 0);

static const struct clk_parent_data a9_saradc_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "sys", }
};

static A9_COMP_SEL(saradc, SAR_CLK_CTRL, 9, 0x7, a9_saradc_parents, NULL);
static A9_COMP_DIV(saradc, SAR_CLK_CTRL, 0, 8);
static A9_COMP_GATE(saradc, SAR_CLK_CTRL, 8, 0);

static const struct clk_parent_data a9_amfc_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "sys", },
	{ .fw_name = "fdiv2", },
	{ .fw_name = "fdiv2p5", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "fdiv7", }
};

static A9_COMP_SEL(amfc, AMFC_CLK_CTRL, 9, 0x7, a9_amfc_parents, NULL);
static A9_COMP_DIV(amfc, AMFC_CLK_CTRL, 0, 6);
static A9_COMP_GATE(amfc, AMFC_CLK_CTRL, 8, 0);

static const struct clk_parent_data a9_nna_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "fdiv2p5", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "fdiv2", },
	{ .fw_name = "gp2", },
	{ .fw_name = "hifi0", }
};

static A9_COMP_SEL(nna, NNA_CLK_CTRL, 9, 0x7, a9_nna_parents, NULL);
static A9_COMP_DIV(nna, NNA_CLK_CTRL, 0, 7);
static A9_COMP_GATE(nna, NNA_CLK_CTRL, 8, 0);

/* Channel 5 and 6 are unconnected. */
static u32 a9_usb_250m_parents_val_table[] = { 0, 1, 2, 3, 4, 7 };
static const struct clk_parent_data a9_usb_250m_parents[] = {
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "fdiv2", },
	{ .fw_name = "fdiv7", },
	{ .fw_name = "fdiv2p5", }
};

static A9_COMP_SEL(usb_250m, USB_CLK_CTRL, 9, 0x7, a9_usb_250m_parents,
		   a9_usb_250m_parents_val_table);
static A9_COMP_DIV(usb_250m, USB_CLK_CTRL, 0, 7);
static A9_COMP_GATE(usb_250m, USB_CLK_CTRL, 8, 0);

static const struct clk_parent_data a9_usb_48m_pre_parents[] = {
	{ .fw_name = "gp0", },
	{ .fw_name = "gp1", },
	{ .fw_name = "gp2", },
	{ .fw_name = "fdiv2", }
};

static A9_COMP_SEL(usb_48m_pre, USB_CLK_CTRL, 25, 0x3, a9_usb_48m_pre_parents, NULL);
static A9_COMP_DIV(usb_48m_pre, USB_CLK_CTRL, 16, 7);
static A9_COMP_GATE(usb_48m_pre, USB_CLK_CTRL, 24, 0);

static const struct clk_parent_data a9_pcie_tl_parents[] = {
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "fdiv2", },
	{ .fw_name = "fdiv2p5", },
	{ .fw_name = "gp0", },
	{ .fw_name = "sys", },
	{ .fw_name = "xtal", }
};

static A9_COMP_SEL(pcie0_tl, PCIE_TL_CLK_CTRL, 9, 0x7, a9_pcie_tl_parents, NULL);
static A9_COMP_DIV(pcie0_tl, PCIE_TL_CLK_CTRL, 0, 7);
static A9_COMP_GATE(pcie0_tl, PCIE_TL_CLK_CTRL, 8, 0);

static A9_COMP_SEL(pcie1_tl, PCIE_TL_CLK_CTRL, 25, 0x7, a9_pcie_tl_parents, NULL);
static A9_COMP_DIV(pcie1_tl, PCIE_TL_CLK_CTRL, 16, 7);
static A9_COMP_GATE(pcie1_tl, PCIE_TL_CLK_CTRL, 24, 0);

static const struct clk_parent_data a9_cmpr_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "fdiv2p5", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "fdiv7", },
	{ .fw_name = "hifi0", },
	{ .fw_name = "gp1", }
};

static A9_COMP_SEL(cmpr, CMPR_CLK_CTRL, 25, 0x7, a9_cmpr_parents, NULL);
static A9_COMP_DIV(cmpr, CMPR_CLK_CTRL, 16, 7);
static A9_COMP_GATE(cmpr, CMPR_CLK_CTRL, 24, 0);

static const struct clk_parent_data a9_dewarpa_parents[] = {
	{ .fw_name = "fdiv2p5", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "fdiv7", },
	{ .fw_name = "gp0", },
	{ .fw_name = "hifi0", },
	{ .fw_name = "gp1", }
};

static A9_COMP_SEL(dewarpa, DEWARP_CLK_CTRL, 9, 0x7, a9_dewarpa_parents, NULL);
static A9_COMP_DIV(dewarpa, DEWARP_CLK_CTRL, 0, 7);
static A9_COMP_GATE(dewarpa, DEWARP_CLK_CTRL, 8, 0);

static const struct clk_parent_data a9_sc_parents[] = {
	{ .fw_name = "fdiv2", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "xtal", }
};

static A9_COMP_SEL(sc_pre, SC_CLK_CTRL, 9, 0x7, a9_sc_parents, NULL);
static A9_COMP_DIV(sc_pre, SC_CLK_CTRL, 0, 8);
static A9_COMP_GATE(sc_pre, SC_CLK_CTRL, 8, 0);

static struct clk_regmap a9_sc = {
	.data = &(struct clk_regmap_div_data) {
		.offset = SC_CLK_CTRL,
		.shift = 16,
		.width = 4,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sc",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_sc_pre.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data a9_dptx_apb2_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "sys", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv5", }
};

static A9_COMP_SEL(dptx_apb2, DPTX_CLK_CTRL, 9, 0x7, a9_dptx_apb2_parents, NULL);
static A9_COMP_DIV(dptx_apb2, DPTX_CLK_CTRL, 0, 7);
static A9_COMP_GATE(dptx_apb2, DPTX_CLK_CTRL, 8, 0);

static const struct clk_parent_data a9_dptx_aud_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "sys", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv4", }
};

static A9_COMP_SEL(dptx_aud, DPTX_CLK_CTRL, 25, 0x7, a9_dptx_aud_parents, NULL);
static A9_COMP_DIV(dptx_aud, DPTX_CLK_CTRL, 16, 7);
static A9_COMP_GATE(dptx_aud, DPTX_CLK_CTRL, 24, 0);

static const struct clk_parent_data a9_isp_parents[] = {
	{ .fw_name = "fdiv2p5", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "gp0", },
	{ .fw_name = "hifi0", },
	{ .fw_name = "fdiv2", },
	{ .fw_name = "xtal", }
};

static A9_COMP_SEL(isp, ISP_CLK_CTRL, 9, 0x7, a9_isp_parents, NULL);
static A9_COMP_DIV(isp, ISP_CLK_CTRL, 0, 7);
static A9_COMP_GATE(isp, ISP_CLK_CTRL, 8, 0);

static const struct clk_parent_data a9_cve_vge_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "fdiv2p5", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "hifi0", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "gp0", },
	{ .fw_name = "rtc", }
};

static A9_COMP_SEL(cve, CVE_CLK_CTRL, 9, 0x7, a9_cve_vge_parents, NULL);
static A9_COMP_DIV(cve, CVE_CLK_CTRL, 0, 7);
static A9_COMP_GATE(cve, CVE_CLK_CTRL, 8, 0);

static A9_COMP_SEL(vge, CVE_CLK_CTRL, 25, 0x7, a9_cve_vge_parents, NULL);
static A9_COMP_DIV(vge, CVE_CLK_CTRL, 16, 7);
static A9_COMP_GATE(vge, CVE_CLK_CTRL, 24, 0);

static const struct clk_parent_data a9_pp_parents[] = {
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "fdiv2", },
	{ .fw_name = "fdiv2p5", },
	{ .fw_name = "gp0", },
	{ .fw_name = "sys", },
	{ .fw_name = "xtal", }
};

static A9_COMP_SEL(pp, PP_CLK_CTRL, 9, 0x7, a9_pp_parents, NULL);
static A9_COMP_DIV(pp, PP_CLK_CTRL, 0, 6);
static A9_COMP_GATE(pp, PP_CLK_CTRL, 8, 0);

static const struct clk_parent_data a9_dspa_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "fdiv2p5", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "gp2", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "hifi0", },
	{ .fw_name = "rtc", }
};

static A9_COMP_SEL(dspa_0, DSPA_CLK_CTRL, 9, 0x7, a9_dspa_parents, NULL);
static A9_COMP_DIV(dspa_0, DSPA_CLK_CTRL, 0, 7);
static A9_COMP_GATE(dspa_0, DSPA_CLK_CTRL, 8, CLK_SET_RATE_GATE);

static A9_COMP_SEL(dspa_1, DSPA_CLK_CTRL, 25, 0x7, a9_dspa_parents, NULL);
static A9_COMP_DIV(dspa_1, DSPA_CLK_CTRL, 16, 7);
static A9_COMP_GATE(dspa_1, DSPA_CLK_CTRL, 24, CLK_SET_RATE_GATE);

static struct clk_regmap a9_dspa = {
	.data = &(struct clk_regmap_mux_data){
		.offset = DSPA_CLK_CTRL,
		.mask = 0x1,
		.shift = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "dspa",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_dspa_0.hw,
			&a9_dspa_1.hw
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/* Channel 6 is unconnected. */
static u32 a9_glb_parents_val_table[] = { 0, 1, 2, 3, 4, 5, 7 };
static const struct clk_parent_data a9_glb_parents[] = {
	{ .fw_name = "xtal", },
	{ .hw = &a9_dspa.hw },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv5", },
	{ .hw = &a9_isp.hw },
	{ .fw_name = "rtc", }
};

static A9_COMP_SEL(glb, GLB_CLK_CTRL, 9, 0x7, a9_glb_parents, a9_glb_parents_val_table);
static A9_COMP_DIV(glb, GLB_CLK_CTRL, 0, 7);
static A9_COMP_GATE(glb, GLB_CLK_CTRL, 8, 0);

static struct clk_regmap a9_usb_48m_dualdiv_in = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = USB_CLK_CTRL,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "usb_48m_dualdiv_in",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_usb_48m_pre.hw
		},
		.num_parents = 1,
	},
};

static const struct meson_clk_dualdiv_param a9_usb_48m_dualdiv_div_table[] = {
	{ 733, 732, 8, 11, 1 },
	{ /* sentinel */ }
};

static struct clk_regmap a9_usb_48m_dualdiv_div = {
	.data = &(struct meson_clk_dualdiv_data) {
		.n1 = {
			.reg_off = USB_CLK_CTRL0,
			.shift   = 0,
			.width   = 12,
		},
		.n2 = {
			.reg_off = USB_CLK_CTRL0,
			.shift   = 12,
			.width   = 12,
		},
		.m1 = {
			.reg_off = USB_CLK_CTRL1,
			.shift   = 0,
			.width   = 12,
		},
		.m2 = {
			.reg_off = USB_CLK_CTRL1,
			.shift   = 12,
			.width   = 12,
		},
		.dual = {
			.reg_off = USB_CLK_CTRL0,
			.shift   = 28,
			.width   = 1,
		},
		.table = a9_usb_48m_dualdiv_div_table,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "usb_48m_dualdiv_div",
		.ops = &meson_clk_dualdiv_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_usb_48m_dualdiv_in.hw
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a9_usb_48m_dualdiv_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = USB_CLK_CTRL1,
		.mask = 0x1,
		.shift = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "usb_48m_dualdiv_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_usb_48m_dualdiv_in.hw,
			&a9_usb_48m_dualdiv_div.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a9_usb_48m_dualdiv = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = USB_CLK_CTRL0,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "usb_48m_dualdiv",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_usb_48m_dualdiv_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a9_usb_48m = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = USB_CLK_CTRL1,
		.mask = 0x3,
		.shift = 30,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "usb_48m",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_usb_48m_pre.hw,
			&a9_usb_48m_dualdiv.hw,
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/* Channel 2 is unconnected. */
static u32 a9_can_pe_parents_val_table[] = { 0, 1, 3 };
static const struct clk_parent_data a9_can_pe_parents[] = {
	{ .fw_name = "sys", },
	{ .fw_name = "xtal", },
	{ .fw_name = "fdiv5", }
};

static A9_COMP_SEL(can0_pe, CAN_CLK_CTRL, 9, 0x7, a9_can_pe_parents, a9_can_pe_parents_val_table);
static A9_COMP_DIV(can0_pe, CAN_CLK_CTRL, 0, 7);
static A9_COMP_GATE(can0_pe, CAN_CLK_CTRL, 8, 0);

static A9_COMP_SEL(can1_pe, CAN_CLK_CTRL, 25, 0x7, a9_can_pe_parents, a9_can_pe_parents_val_table);
static A9_COMP_DIV(can1_pe, CAN_CLK_CTRL, 16, 7);
static A9_COMP_GATE(can1_pe, CAN_CLK_CTRL, 24, 0);

static const struct clk_parent_data a9_can_filter_parents[] = {
	{ .fw_name = "sys", },
	{ .fw_name = "xtal", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv5", }
};

static A9_COMP_SEL(can0_filter, CAN_CLK_CTRL1, 9, 0x7, a9_can_filter_parents, NULL);
static A9_COMP_DIV(can0_filter, CAN_CLK_CTRL1, 0, 7);
static A9_COMP_GATE(can0_filter, CAN_CLK_CTRL1, 8, 0);

static A9_COMP_SEL(can1_filter, CAN_CLK_CTRL1, 25, 0x7, a9_can_filter_parents, NULL);
static A9_COMP_DIV(can1_filter, CAN_CLK_CTRL1, 16, 7);
static A9_COMP_GATE(can1_filter, CAN_CLK_CTRL1, 24, 0);

static const struct clk_parent_data a9_i3c_parents[] = {
	{ .fw_name = "sys", },
	{ .fw_name = "xtal", },
	{ .fw_name = "fdiv5", }
};

static A9_COMP_SEL(i3c, I3C_CLK_CTRL, 9, 0x7, a9_i3c_parents, NULL);
static A9_COMP_DIV(i3c, I3C_CLK_CTRL, 0, 8);
static A9_COMP_GATE(i3c, I3C_CLK_CTRL, 8, 0);

static struct clk_regmap a9_ts_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = TS_CLK_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "ts_div",
		.ops = &clk_regmap_divider_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a9_ts = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = TS_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "ts",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_ts_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_fixed_factor a9_eth_125m_div = {
	.mult = 1,
	.div = 8,
	.hw.init = &(struct clk_init_data) {
		.name = "eth_125m_div",
		.ops = &clk_fixed_factor_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "fdiv2",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a9_eth_125m = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = ETH_CLK_CTRL,
		.bit_idx = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "eth_125m",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_eth_125m_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/*
 * Channel 1, 2, 3, 4, 5 and 6 are unconnected,
 * Channel 7(ext_rmii) connects external PAD. Do not automatically reparent.
 */
static u32 a9_eth_rmii_parents_val_table[] = { 0, 7 };
static const struct clk_parent_data a9_eth_rmii_parents[] = {
	{ .fw_name = "fdiv2", },
	{ .fw_name = "ext_rmii", }
};

static struct clk_regmap a9_eth_rmii_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = ETH_CLK_CTRL,
		.mask = 0x7,
		.shift = 9,
		.table = a9_eth_rmii_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "eth_rmii_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = a9_eth_rmii_parents,
		.num_parents = ARRAY_SIZE(a9_eth_rmii_parents),
		.flags = CLK_SET_RATE_NO_REPARENT,
	},
};

static struct clk_regmap a9_eth_rmii_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = ETH_CLK_CTRL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "eth_rmii_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_eth_rmii_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a9_eth_rmii = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = ETH_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "eth_rmii",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_eth_rmii_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a9_vid_pll_div = {
	.data = &(struct meson_vid_pll_div_data){
		.val = {
			.reg_off = VID_PLL_CLK_DIV,
			.shift   = 0,
			.width   = 15,
		},
		.sel = {
			.reg_off = VID_PLL_CLK_DIV,
			.shift   = 16,
			.width   = 2,
		},
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vid_pll_div",
		.ops = &meson_vid_pll_div_ro_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .fw_name = "hdmiout2", }
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a9_vid_pll_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = VID_PLL_CLK_DIV,
		.mask = 0x1,
		.shift = 18,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vid_pll_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .hw = &a9_vid_pll_div.hw },
			{ .fw_name = "hdmiout2", }
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a9_vid_pll = {
	.data = &(struct clk_regmap_gate_data){
		.offset = VID_PLL_CLK_DIV,
		.bit_idx = 19,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vid_pll",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_vid_pll_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/*
 * Channel 12 (msr_clk) is managed by the clock measurement module and is not part of the clock
 * tree. It depends on the measurement source selected through the measurement control registers.
 *
 * Channel 10, 11, 13, 14 and 16 are unconnected.
 */
static u32 a9_gen_parents_val_table[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 17, 18,
					  19, 20, 21, 22, 23, 24, 25, 26};
static const struct clk_parent_data a9_gen_parents[] = {
	{ .fw_name = "xtal" },
	{ .fw_name = "rtc" },
	{ .fw_name = "sysplldiv16" },
	{ .fw_name = "ddr_test" },
	{ .hw = &a9_vid_pll.hw },
	{ .fw_name = "gp0" },
	{ .fw_name = "hifi1" },
	{ .fw_name = "hifi0" },
	{ .fw_name = "gp1" },
	{ .fw_name = "gp2" },
	{ .fw_name = "dsudiv16" },
	{ .fw_name = "cpudiv16" },
	{ .fw_name = "a78div16" },
	{ .fw_name = "fdiv2" },
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "fdiv7" },
	{ .fw_name = "mclk0" },
	{ .fw_name = "mclk1" }
};

static A9_COMP_SEL(gen, GEN_CLK_CTRL, 12, 0x1f, a9_gen_parents, a9_gen_parents_val_table);
static A9_COMP_DIV(gen, GEN_CLK_CTRL, 0, 11);
static A9_COMP_GATE(gen, GEN_CLK_CTRL, 11, 0);

static struct clk_regmap a9_24m_in = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = CLK12_24_CTRL,
		.bit_idx = 11,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "24m_in",
		.ops = &clk_regmap_gate_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a9_12_24m = {
	.data = &(struct clk_regmap_div_data) {
		.offset = CLK12_24_CTRL,
		.shift = 10,
		.width = 1,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "12_24m",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_24m_in.hw
		},
		.num_parents = 1,
	},
};

static const struct clk_parent_data a9_mali_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "gp1", },
	{ .fw_name = "fdiv2", },
	{ .fw_name = "fdiv2p5", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "fdiv7", }
};

static A9_COMP_SEL(mali_0, MALI_CLK_CTRL, 9, 0x7, a9_mali_parents, NULL);
static A9_COMP_DIV(mali_0, MALI_CLK_CTRL, 0, 7);
static A9_COMP_GATE(mali_0, MALI_CLK_CTRL, 8, CLK_SET_RATE_GATE);

static A9_COMP_SEL(mali_1, MALI_CLK_CTRL, 25, 0x7, a9_mali_parents, NULL);
static A9_COMP_DIV(mali_1, MALI_CLK_CTRL, 16, 7);
static A9_COMP_GATE(mali_1, MALI_CLK_CTRL, 24, CLK_SET_RATE_GATE);

static struct clk_regmap a9_mali = {
	.data = &(struct clk_regmap_mux_data){
		.offset = MALI_CLK_CTRL,
		.mask = 0x1,
		.shift = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mali",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_mali_0.hw,
			&a9_mali_1.hw
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static A9_COMP_SEL(mali_stack_0, MALI_STACK_CLK_CTRL, 9, 0x7, a9_mali_parents, NULL);
static A9_COMP_DIV(mali_stack_0, MALI_STACK_CLK_CTRL, 0, 7);
static A9_COMP_GATE(mali_stack_0, MALI_STACK_CLK_CTRL, 8, CLK_SET_RATE_GATE);

static A9_COMP_SEL(mali_stack_1, MALI_STACK_CLK_CTRL, 25, 0x7, a9_mali_parents, NULL);
static A9_COMP_DIV(mali_stack_1, MALI_STACK_CLK_CTRL, 16, 7);
static A9_COMP_GATE(mali_stack_1, MALI_STACK_CLK_CTRL, 24, CLK_SET_RATE_GATE);

static struct clk_regmap a9_mali_stack = {
	.data = &(struct clk_regmap_mux_data){
		.offset = MALI_STACK_CLK_CTRL,
		.mask = 0x1,
		.shift = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mali_stack",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_mali_stack_0.hw,
			&a9_mali_stack_1.hw
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data a9_hevcf_parents[] = {
	{ .fw_name = "fdiv2p5", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "fdiv7", },
	{ .fw_name = "hifi0", },
	{ .fw_name = "gp1", },
	{ .fw_name = "xtal", }
};

static A9_COMP_SEL(hevcf_0, HEVCF_CLK_CTRL, 9, 0x7, a9_hevcf_parents, NULL);
static A9_COMP_DIV(hevcf_0, HEVCF_CLK_CTRL, 0, 7);
static A9_COMP_GATE(hevcf_0, HEVCF_CLK_CTRL, 8, CLK_SET_RATE_GATE);

static A9_COMP_SEL(hevcf_1, HEVCF_CLK_CTRL, 25, 0x7, a9_hevcf_parents, NULL);
static A9_COMP_DIV(hevcf_1, HEVCF_CLK_CTRL, 16, 7);
static A9_COMP_GATE(hevcf_1, HEVCF_CLK_CTRL, 24, CLK_SET_RATE_GATE);

static struct clk_regmap a9_hevcf = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HEVCF_CLK_CTRL,
		.mask = 0x1,
		.shift = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hevcf",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_hevcf_0.hw,
			&a9_hevcf_1.hw
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data a9_hcodec_parents[] = {
	{ .fw_name = "fdiv2p5", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "fdiv7", },
	{ .fw_name = "hifi0", },
	{ .fw_name = "gp0", },
	{ .fw_name = "xtal", }
};

static A9_COMP_SEL(hcodec_0, HCODEC_CLK_CTRL, 9, 0x7, a9_hcodec_parents, NULL);
static A9_COMP_DIV(hcodec_0, HCODEC_CLK_CTRL, 0, 7);
static A9_COMP_GATE(hcodec_0, HCODEC_CLK_CTRL, 8, CLK_SET_RATE_GATE);

static A9_COMP_SEL(hcodec_1, HCODEC_CLK_CTRL, 25, 0x7, a9_hcodec_parents, NULL);
static A9_COMP_DIV(hcodec_1, HCODEC_CLK_CTRL, 16, 7);
static A9_COMP_GATE(hcodec_1, HCODEC_CLK_CTRL, 24, CLK_SET_RATE_GATE);

static struct clk_regmap a9_hcodec = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HCODEC_CLK_CTRL,
		.mask = 0x1,
		.shift = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hcodec",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_hcodec_0.hw,
			&a9_hcodec_1.hw
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data a9_vpu_parents[] = {
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "vid1", },
	{ .fw_name = "fdiv2", },
	{ .hw = &a9_vid_pll.hw },
	{ .fw_name = "vid2", },
	{ .fw_name = "gp1", }
};

static A9_COMP_SEL(vpu_0, VPU_CLK_CTRL, 9, 0x7, a9_vpu_parents, NULL);
static A9_COMP_DIV(vpu_0, VPU_CLK_CTRL, 0, 7);
static A9_COMP_GATE(vpu_0, VPU_CLK_CTRL, 8, CLK_SET_RATE_GATE);

static A9_COMP_SEL(vpu_1, VPU_CLK_CTRL, 25, 0x7, a9_vpu_parents, NULL);
static A9_COMP_DIV(vpu_1, VPU_CLK_CTRL, 16, 7);
static A9_COMP_GATE(vpu_1, VPU_CLK_CTRL, 24, CLK_SET_RATE_GATE);

static struct clk_regmap a9_vpu = {
	.data = &(struct clk_regmap_mux_data){
		.offset = VPU_CLK_CTRL,
		.mask = 0x1,
		.shift = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vpu",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_vpu_0.hw,
			&a9_vpu_1.hw
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data a9_vapb_parents[] = {
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "fdiv7", },
	{ .fw_name = "fdiv2", },
	{ .hw = &a9_vid_pll.hw },
	{ .fw_name = "hifi0", },
	{ .fw_name = "fdiv2p5", }
};

static A9_COMP_SEL(vapb_0, VAPB_CLK_CTRL, 9, 0x7, a9_vapb_parents, NULL);
static A9_COMP_DIV(vapb_0, VAPB_CLK_CTRL, 0, 7);
static A9_COMP_GATE(vapb_0, VAPB_CLK_CTRL, 8, CLK_SET_RATE_GATE);

static A9_COMP_SEL(vapb_1, VAPB_CLK_CTRL, 25, 0x7, a9_vapb_parents, NULL);
static A9_COMP_DIV(vapb_1, VAPB_CLK_CTRL, 16, 7);
static A9_COMP_GATE(vapb_1, VAPB_CLK_CTRL, 24, CLK_SET_RATE_GATE);

static struct clk_regmap a9_vapb = {
	.data = &(struct clk_regmap_mux_data){
		.offset = VAPB_CLK_CTRL,
		.mask = 0x1,
		.shift = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vapb",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_vapb_0.hw,
			&a9_vapb_1.hw
		},
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a9_ge2d = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = VAPB_CLK_CTRL,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "ge2d",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_vapb.hw,
		},
		.num_parents = 1,
	},
};

static const struct clk_parent_data a9_vpu_clkb_tmp_parents[] = {
	{ .hw = &a9_vpu.hw },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "fdiv7", }
};

static A9_COMP_SEL(vpu_clkb_tmp, VPU_CLKB_CTRL, 25, 0x7, a9_vpu_clkb_tmp_parents, NULL);
static A9_COMP_DIV(vpu_clkb_tmp, VPU_CLKB_CTRL, 16, 4);
static A9_COMP_GATE(vpu_clkb_tmp, VPU_CLKB_CTRL, 24, 0);

static struct clk_regmap a9_vpu_clkb_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = VPU_CLKB_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vpu_clkb_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_vpu_clkb_tmp.hw,
		},
		.num_parents = 1,
	},
};

static struct clk_regmap a9_vpu_clkb = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = VPU_CLKB_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vpu_clkb",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_vpu_clkb_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data a9_hdmi_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv5", }
};

static A9_COMP_SEL(hdmitx_sys, HDMI_CLK_CTRL, 9, 0x7, a9_hdmi_parents, NULL);
static A9_COMP_DIV(hdmitx_sys, HDMI_CLK_CTRL, 0, 7);
static A9_COMP_GATE(hdmitx_sys, HDMI_CLK_CTRL, 8, 0);

static A9_COMP_SEL(hdmitx_prif, HTX_CLK_CTRL, 9, 0x7, a9_hdmi_parents, NULL);
static A9_COMP_DIV(hdmitx_prif, HTX_CLK_CTRL, 0, 7);
static A9_COMP_GATE(hdmitx_prif, HTX_CLK_CTRL, 8, 0);

static A9_COMP_SEL(hdmitx_200m, HTX_CLK_CTRL, 25, 0x7, a9_hdmi_parents, NULL);
static A9_COMP_DIV(hdmitx_200m, HTX_CLK_CTRL, 16, 7);
static A9_COMP_GATE(hdmitx_200m, HTX_CLK_CTRL, 24, 0);

static A9_COMP_SEL(hdmitx_aud, HTX_CLK_CTRL1, 9, 0x7, a9_hdmi_parents, NULL);
static A9_COMP_DIV(hdmitx_aud, HTX_CLK_CTRL1, 0, 7);
static A9_COMP_GATE(hdmitx_aud, HTX_CLK_CTRL1, 8, 0);

static A9_COMP_SEL(hdmirx_5m, HRX_CLK_CTRL, 9, 0x7, a9_hdmi_parents, NULL);
static A9_COMP_DIV(hdmirx_5m, HRX_CLK_CTRL, 0, 7);
static A9_COMP_GATE(hdmirx_5m, HRX_CLK_CTRL, 8, 0);

static A9_COMP_SEL(hdmirx_2m, HRX_CLK_CTRL, 25, 0x7, a9_hdmi_parents, NULL);
static A9_COMP_DIV(hdmirx_2m, HRX_CLK_CTRL, 16, 7);
static A9_COMP_GATE(hdmirx_2m, HRX_CLK_CTRL, 24, 0);

static A9_COMP_SEL(hdmirx_cfg, HRX_CLK_CTRL1, 9, 0x7, a9_hdmi_parents, NULL);
static A9_COMP_DIV(hdmirx_cfg, HRX_CLK_CTRL1, 0, 7);
static A9_COMP_GATE(hdmirx_cfg, HRX_CLK_CTRL1, 8, 0);

static A9_COMP_SEL(hdmirx_hdcp2x, HRX_CLK_CTRL1, 25, 0x7, a9_hdmi_parents, NULL);
static A9_COMP_DIV(hdmirx_hdcp2x, HRX_CLK_CTRL1, 16, 7);
static A9_COMP_GATE(hdmirx_hdcp2x, HRX_CLK_CTRL1, 24, 0);

static A9_COMP_SEL(hdmirx_acr_ref, HRX_CLK_CTRL2, 25, 0x7, a9_hdmi_parents, NULL);
static A9_COMP_DIV(hdmirx_acr_ref, HRX_CLK_CTRL2, 16, 7);
static A9_COMP_GATE(hdmirx_acr_ref, HRX_CLK_CTRL2, 24, 0);

static A9_COMP_SEL(hdmirx_meter, HRX_CLK_CTRL3, 9, 0x7, a9_hdmi_parents, NULL);
static A9_COMP_DIV(hdmirx_meter, HRX_CLK_CTRL3, 0, 7);
static A9_COMP_GATE(hdmirx_meter, HRX_CLK_CTRL3, 8, 0);

static struct clk_regmap a9_vid_pll_vclk = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HDMI_CLK_CTRL,
		.mask = 0x1,
		.shift = 15,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vid_pll_vclk",
		.ops = &clk_regmap_mux_ops,
		.parent_data = (const struct clk_parent_data []) {
			{ .hw = &a9_vid_pll.hw },
			{ .fw_name = "hdmipix", }
		},
		.num_parents = 2,
	},
};

static const struct clk_parent_data a9_vclk_parents[] = {
	{ .hw = &a9_vid_pll_vclk.hw },
	{ .fw_name = "pix0", },
	{ .fw_name = "vid1", },
	{ .fw_name = "pix1", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "vid2", }
};

static struct clk_regmap a9_vclk0_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = VID_CLK_CTRL,
		.mask = 0x7,
		.shift = 16,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk0_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = a9_vclk_parents,
		.num_parents = ARRAY_SIZE(a9_vclk_parents),
	},
};

static struct clk_regmap a9_vclk0_in = {
	.data = &(struct clk_regmap_gate_data){
		.offset = VID_CLK_DIV,
		.bit_idx = 16,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vclk0_in",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &a9_vclk0_sel.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a9_vclk0_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = VID_CLK_DIV,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk0_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_vclk0_in.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a9_vclk0 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = VID_CLK_CTRL,
		.bit_idx = 19,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vclk0",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &a9_vclk0_div.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

#define A9_VCLK_GATE(_name, _reg, _bit,  _parent)			\
struct clk_regmap a9_##_name##_en = {					\
	.data = &(struct clk_regmap_gate_data) {			\
		.offset = (_reg),					\
		.bit_idx = (_bit),					\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name = #_name "_en",					\
		.ops = &clk_regmap_gate_ops,				\
		.parent_hws = (const struct clk_hw *[]) {		\
			&(_parent).hw					\
		},							\
		.num_parents = 1,					\
		.flags = CLK_SET_RATE_PARENT,				\
	},								\
}

#define A9_VCLK_DIV(_name, _div)					\
struct clk_fixed_factor a9_##_name = {					\
	.mult = 1,							\
	.div = (_div),							\
	.hw.init = &(struct clk_init_data) {				\
		.name = #_name,						\
		.ops = &clk_fixed_factor_ops,				\
		.parent_hws = (const struct clk_hw *[]) {		\
			&a9_##_name##_en.hw				\
		},							\
		.num_parents = 1,					\
		.flags = CLK_SET_RATE_PARENT,				\
	},								\
}

static A9_VCLK_GATE(vclk0_div1, VID_CLK_CTRL, 0, a9_vclk0);
static A9_VCLK_GATE(vclk0_div2, VID_CLK_CTRL, 1, a9_vclk0);
static A9_VCLK_DIV(vclk0_div2, 2);
static A9_VCLK_GATE(vclk0_div4, VID_CLK_CTRL, 2, a9_vclk0);
static A9_VCLK_DIV(vclk0_div4, 4);
static A9_VCLK_GATE(vclk0_div6, VID_CLK_CTRL, 3, a9_vclk0);
static A9_VCLK_DIV(vclk0_div6, 6);
static A9_VCLK_GATE(vclk0_div12, VID_CLK_CTRL, 4, a9_vclk0);
static A9_VCLK_DIV(vclk0_div12, 12);

static struct clk_regmap a9_vclk1_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = VIID_CLK_CTRL,
		.mask = 0x7,
		.shift = 16,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk1_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = a9_vclk_parents,
		.num_parents = ARRAY_SIZE(a9_vclk_parents),
	},
};

static struct clk_regmap a9_vclk1_in = {
	.data = &(struct clk_regmap_gate_data){
		.offset = VIID_CLK_DIV,
		.bit_idx = 16,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vclk1_in",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &a9_vclk1_sel.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a9_vclk1_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = VIID_CLK_DIV,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vclk1_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_vclk1_in.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a9_vclk1 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = VIID_CLK_CTRL,
		.bit_idx = 19,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vclk1",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) { &a9_vclk1_div.hw },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static A9_VCLK_GATE(vclk1_div1, VIID_CLK_CTRL, 0, a9_vclk1);
static A9_VCLK_GATE(vclk1_div2, VIID_CLK_CTRL, 1, a9_vclk1);
static A9_VCLK_DIV(vclk1_div2, 2);
static A9_VCLK_GATE(vclk1_div4, VIID_CLK_CTRL, 2, a9_vclk1);
static A9_VCLK_DIV(vclk1_div4, 4);
static A9_VCLK_GATE(vclk1_div6, VIID_CLK_CTRL, 3, a9_vclk1);
static A9_VCLK_DIV(vclk1_div6, 6);
static A9_VCLK_GATE(vclk1_div12, VIID_CLK_CTRL, 4, a9_vclk1);
static A9_VCLK_DIV(vclk1_div12, 12);

/* Channel 5, 6 and 7 are unconnected */
static u32 a9_vid_parents_val_table[] = { 0, 1, 2, 3, 4, 8, 9, 10, 11, 12 };
static const struct clk_hw *a9_vid_parents[] = {
	&a9_vclk0_div1_en.hw,
	&a9_vclk0_div2.hw,
	&a9_vclk0_div4.hw,
	&a9_vclk0_div6.hw,
	&a9_vclk0_div12.hw,
	&a9_vclk1_div1_en.hw,
	&a9_vclk1_div2.hw,
	&a9_vclk1_div4.hw,
	&a9_vclk1_div6.hw,
	&a9_vclk1_div12.hw
};

static struct clk_regmap a9_encoder0_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = VIID_CLK_DIV,
		.mask = 0xf,
		.shift = 12,
		.table = a9_vid_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "encoder0_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = a9_vid_parents,
		.num_parents = ARRAY_SIZE(a9_vid_parents),
	},
};

static struct clk_regmap a9_encoder0 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = VID_CLK_CTRL2,
		.bit_idx = 10,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "encoder0",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_encoder0_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a9_encoder1_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = VIID_CLK_DIV,
		.mask = 0xf,
		.shift = 8,
		.table = a9_vid_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "encoder1_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = a9_vid_parents,
		.num_parents = ARRAY_SIZE(a9_vid_parents),
	},
};

static struct clk_regmap a9_encoder1 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = VID_CLK_CTRL2,
		.bit_idx = 11,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "encoder1",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_encoder1_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data a9_vid_lock_parents[] = {
	{ .fw_name = "xtal", },
	{ .hw = &a9_encoder0.hw },
	{ .hw = &a9_encoder1.hw }
};

static A9_COMP_SEL(vid_lock, VID_LOCK_CLK_CTRL, 9, 0x7, a9_vid_lock_parents, NULL);
static A9_COMP_DIV(vid_lock, VID_LOCK_CLK_CTRL, 0, 7);
static A9_COMP_GATE(vid_lock, VID_LOCK_CLK_CTRL, 8, 0);

static const struct clk_parent_data a9_vdin_meas_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv5", }
};

static A9_COMP_SEL(vdin_meas, VDIN_MEAS_CLK_CTRL, 9, 0x7, a9_vdin_meas_parents, NULL);
static A9_COMP_DIV(vdin_meas, VDIN_MEAS_CLK_CTRL, 0, 7);
static A9_COMP_GATE(vdin_meas, VDIN_MEAS_CLK_CTRL, 8, 0);

static struct clk_regmap a9_vdac_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = VIID_CLK_DIV,
		.mask = 0xf,
		.shift = 28,
		.table = a9_vid_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "vdac_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = a9_vid_parents,
		.num_parents = ARRAY_SIZE(a9_vid_parents),
	},
};

static struct clk_regmap a9_vdac = {
	.data = &(struct clk_regmap_gate_data){
		.offset = VID_CLK_CTRL2,
		.bit_idx = 4,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vdac",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_vdac_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a9_hdmitx0_pixel_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HDMI_CLK_CTRL,
		.mask = 0xf,
		.shift = 16,
		.table = a9_vid_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmitx0_pixel_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = a9_vid_parents,
		.num_parents = ARRAY_SIZE(a9_vid_parents),
	},
};

static struct clk_regmap a9_hdmitx0_pixel = {
	.data = &(struct clk_regmap_gate_data){
		.offset = VID_CLK_CTRL2,
		.bit_idx = 5,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "hdmitx0_pixel",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_hdmitx0_pixel_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a9_hdmitx0_fe_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HDMI_CLK_CTRL,
		.mask = 0xf,
		.shift = 20,
		.table = a9_vid_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmitx0_fe_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = a9_vid_parents,
		.num_parents = ARRAY_SIZE(a9_vid_parents),
	},
};

static struct clk_regmap a9_hdmitx0_fe = {
	.data = &(struct clk_regmap_gate_data){
		.offset = VID_CLK_CTRL2,
		.bit_idx = 9,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "hdmitx0_fe",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_hdmitx0_fe_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a9_hdmitx1_pixel_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HDMI_CLK_CTRL,
		.mask = 0xf,
		.shift = 24,
		.table = a9_vid_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmitx1_pixel_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = a9_vid_parents,
		.num_parents = ARRAY_SIZE(a9_vid_parents),
	},
};

static struct clk_regmap a9_hdmitx1_pixel = {
	.data = &(struct clk_regmap_gate_data){
		.offset = VID_CLK_CTRL2,
		.bit_idx = 12,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "hdmitx1_pixel",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_hdmitx1_pixel_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap a9_hdmitx1_fe_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HDMI_CLK_CTRL,
		.mask = 0xf,
		.shift = 28,
		.table = a9_vid_parents_val_table,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hdmitx1_fe_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_hws = a9_vid_parents,
		.num_parents = ARRAY_SIZE(a9_vid_parents),
	},
};

static struct clk_regmap a9_hdmitx1_fe = {
	.data = &(struct clk_regmap_gate_data){
		.offset = VID_CLK_CTRL2,
		.bit_idx = 13,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "hdmitx1_fe",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&a9_hdmitx1_fe_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data a9_csi_phy_parents[] = {
	{ .fw_name = "fdiv2p5", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv5", },
	{ .fw_name = "gp0", },
	{ .fw_name = "hifi0", },
	{ .fw_name = "fdiv2", },
	{ .fw_name = "xtal", }
};

static A9_COMP_SEL(csi_phy, MIPI_CSI_PHY_CLK_CTRL, 9, 0x7, a9_csi_phy_parents, NULL);
static A9_COMP_DIV(csi_phy, MIPI_CSI_PHY_CLK_CTRL, 0, 7);
static A9_COMP_GATE(csi_phy, MIPI_CSI_PHY_CLK_CTRL, 8, 0);

static const struct clk_parent_data a9_dsi_meas_parents[] = {
	{ .fw_name = "xtal", },
	{ .fw_name = "fdiv4", },
	{ .fw_name = "fdiv3", },
	{ .fw_name = "fdiv5", },
	{ .hw = &a9_vid_pll.hw },
	{ .fw_name = "gp0", },
	{ .fw_name = "vid1", },
	{ .fw_name = "vid2", }
};

static A9_COMP_SEL(dsi0_meas, DSI_MEAS_CLK_CTRL, 9, 0x7, a9_dsi_meas_parents, NULL);
static A9_COMP_DIV(dsi0_meas, DSI_MEAS_CLK_CTRL, 0, 7);
static A9_COMP_GATE(dsi0_meas, DSI_MEAS_CLK_CTRL, 8, 0);

static A9_COMP_SEL(dsi1_meas, DSI_MEAS_CLK_CTRL, 25, 0x7, a9_dsi_meas_parents, NULL);
static A9_COMP_DIV(dsi1_meas, DSI_MEAS_CLK_CTRL, 16, 7);
static A9_COMP_GATE(dsi1_meas, DSI_MEAS_CLK_CTRL, 24, 0);

static struct clk_hw *a9_peripherals_hw_clks[] = {
	[CLKID_SYS_AM_AXI]		= &a9_sys_am_axi.hw,
	[CLKID_SYS_DOS]			= &a9_sys_dos.hw,
	[CLKID_SYS_MIPI_DSI0]		= &a9_sys_mipi_dsi0.hw,
	[CLKID_SYS_ETH_PHY]		= &a9_sys_eth_phy.hw,
	[CLKID_SYS_AMFC]		= &a9_sys_amfc.hw,
	[CLKID_SYS_MALI]		= &a9_sys_mali.hw,
	[CLKID_SYS_NNA]			= &a9_sys_nna.hw,
	[CLKID_SYS_ETH_AXI]		= &a9_sys_eth_axi.hw,
	[CLKID_SYS_DP_APB]		= &a9_sys_dp_apb.hw,
	[CLKID_SYS_EDPTX_APB]		= &a9_sys_edptx_apb.hw,
	[CLKID_SYS_U3HSG]		= &a9_sys_u3hsg.hw,
	[CLKID_SYS_AUCPU]		= &a9_sys_aucpu.hw,
	[CLKID_SYS_GLB]			= &a9_sys_glb.hw,
	[CLKID_SYS_COMBO_DPHY_APB]	= &a9_sys_combo_dphy_apb.hw,
	[CLKID_SYS_HDMIRX_APB]		= &a9_sys_hdmirx_apb.hw,
	[CLKID_SYS_HDMIRX_PCLK]		= &a9_sys_hdmirx_pclk.hw,
	[CLKID_SYS_MIPI_DSI0_PHY]	= &a9_sys_mipi_dsi0_phy.hw,
	[CLKID_SYS_CAN0]		= &a9_sys_can0.hw,
	[CLKID_SYS_CAN1]		= &a9_sys_can1.hw,
	[CLKID_SYS_SD_EMMC_A]		= &a9_sys_sd_emmc_a.hw,
	[CLKID_SYS_SD_EMMC_B]		= &a9_sys_sd_emmc_b.hw,
	[CLKID_SYS_SD_EMMC_C]		= &a9_sys_sd_emmc_c.hw,
	[CLKID_SYS_SC]			= &a9_sys_sc.hw,
	[CLKID_SYS_ACODEC]		= &a9_sys_acodec.hw,
	[CLKID_SYS_MIPI_ISP]		= &a9_sys_mipi_isp.hw,
	[CLKID_SYS_MSR]			= &a9_sys_msr.hw,
	[CLKID_SYS_AUDIO]		= &a9_sys_audio.hw,
	[CLKID_SYS_MIPI_DSI1]		= &a9_sys_mipi_dsi1.hw,
	[CLKID_SYS_MIPI_DSI1_PHY]	= &a9_sys_mipi_dsi1_phy.hw,
	[CLKID_SYS_ETH]			= &a9_sys_eth.hw,
	[CLKID_SYS_ETH_1G_MAC]		= &a9_sys_eth_1g_mac.hw,
	[CLKID_SYS_UART_A]		= &a9_sys_uart_a.hw,
	[CLKID_SYS_UART_F]		= &a9_sys_uart_f.hw,
	[CLKID_SYS_TS_A55]		= &a9_sys_ts_a55.hw,
	[CLKID_SYS_ETH_1G_AXI]		= &a9_sys_eth_1g_axi.hw,
	[CLKID_SYS_TS_DOS]		= &a9_sys_ts_dos.hw,
	[CLKID_SYS_U3DRD_B]		= &a9_sys_u3drd_b.hw,
	[CLKID_SYS_TS_CORE]		= &a9_sys_ts_core.hw,
	[CLKID_SYS_TS_PLL]		= &a9_sys_ts_pll.hw,
	[CLKID_SYS_CSI_DIG_CLKIN]	= &a9_sys_csi_dig_clkin.hw,
	[CLKID_SYS_CVE]			= &a9_sys_cve.hw,
	[CLKID_SYS_GE2D]		= &a9_sys_ge2d.hw,
	[CLKID_SYS_SPISG]		= &a9_sys_spisg.hw,
	[CLKID_SYS_U2H]			= &a9_sys_u2h.hw,
	[CLKID_SYS_PCIE_MAC_A]		= &a9_sys_pcie_mac_a.hw,
	[CLKID_SYS_U3DRD_A]		= &a9_sys_u3drd_a.hw,
	[CLKID_SYS_U2DRD]		= &a9_sys_u2drd.hw,
	[CLKID_SYS_PCIE_PHY]		= &a9_sys_pcie_phy.hw,
	[CLKID_SYS_PCIE_MAC_B]		= &a9_sys_pcie_mac_b.hw,
	[CLKID_SYS_PERIPH]		= &a9_sys_periph.hw,
	[CLKID_SYS_PIO]			= &a9_sys_pio.hw,
	[CLKID_SYS_I3C]			= &a9_sys_i3c.hw,
	[CLKID_SYS_I2C_M_E]		= &a9_sys_i2c_m_e.hw,
	[CLKID_SYS_I2C_M_F]		= &a9_sys_i2c_m_f.hw,
	[CLKID_SYS_HDMITX_APB]		= &a9_sys_hdmitx_apb.hw,
	[CLKID_SYS_I2C_M_I]		= &a9_sys_i2c_m_i.hw,
	[CLKID_SYS_I2C_M_G]		= &a9_sys_i2c_m_g.hw,
	[CLKID_SYS_I2C_M_H]		= &a9_sys_i2c_m_h.hw,
	[CLKID_SYS_HDMI20_AES]		= &a9_sys_hdmi20_aes.hw,
	[CLKID_SYS_CSI2_HOST]		= &a9_sys_csi2_host.hw,
	[CLKID_SYS_CSI2_ADAPT]		= &a9_sys_csi2_adapt.hw,
	[CLKID_SYS_DSPA]		= &a9_sys_dspa.hw,
	[CLKID_SYS_PP_DMA]		= &a9_sys_pp_dma.hw,
	[CLKID_SYS_PP_WRAPPER]		= &a9_sys_pp_wrapper.hw,
	[CLKID_SYS_VPU_INTR]		= &a9_sys_vpu_intr.hw,
	[CLKID_SYS_CSI2_PHY]		= &a9_sys_csi2_phy.hw,
	[CLKID_SYS_SARADC]		= &a9_sys_saradc.hw,
	[CLKID_SYS_PWM_J]		= &a9_sys_pwm_j.hw,
	[CLKID_SYS_PWM_I]		= &a9_sys_pwm_i.hw,
	[CLKID_SYS_PWM_H]		= &a9_sys_pwm_h.hw,
	[CLKID_SYS_PWM_N]		= &a9_sys_pwm_n.hw,
	[CLKID_SYS_PWM_M]		= &a9_sys_pwm_m.hw,
	[CLKID_SYS_PWM_L]		= &a9_sys_pwm_l.hw,
	[CLKID_SYS_PWM_K]		= &a9_sys_pwm_k.hw,
	[CLKID_SD_EMMC_A_SEL]		= &a9_sd_emmc_a_sel.hw,
	[CLKID_SD_EMMC_A_DIV]		= &a9_sd_emmc_a_div.hw,
	[CLKID_SD_EMMC_A]		= &a9_sd_emmc_a.hw,
	[CLKID_SD_EMMC_B_SEL]		= &a9_sd_emmc_b_sel.hw,
	[CLKID_SD_EMMC_B_DIV]		= &a9_sd_emmc_b_div.hw,
	[CLKID_SD_EMMC_B]		= &a9_sd_emmc_b.hw,
	[CLKID_SD_EMMC_C_SEL]		= &a9_sd_emmc_c_sel.hw,
	[CLKID_SD_EMMC_C_DIV]		= &a9_sd_emmc_c_div.hw,
	[CLKID_SD_EMMC_C]		= &a9_sd_emmc_c.hw,
	[CLKID_PWM_H_SEL]		= &a9_pwm_h_sel.hw,
	[CLKID_PWM_H_DIV]		= &a9_pwm_h_div.hw,
	[CLKID_PWM_H]			= &a9_pwm_h.hw,
	[CLKID_PWM_I_SEL]		= &a9_pwm_i_sel.hw,
	[CLKID_PWM_I_DIV]		= &a9_pwm_i_div.hw,
	[CLKID_PWM_I]			= &a9_pwm_i.hw,
	[CLKID_PWM_J_SEL]		= &a9_pwm_j_sel.hw,
	[CLKID_PWM_J_DIV]		= &a9_pwm_j_div.hw,
	[CLKID_PWM_J]			= &a9_pwm_j.hw,
	[CLKID_PWM_K_SEL]		= &a9_pwm_k_sel.hw,
	[CLKID_PWM_K_DIV]		= &a9_pwm_k_div.hw,
	[CLKID_PWM_K]			= &a9_pwm_k.hw,
	[CLKID_PWM_L_SEL]		= &a9_pwm_l_sel.hw,
	[CLKID_PWM_L_DIV]		= &a9_pwm_l_div.hw,
	[CLKID_PWM_L]			= &a9_pwm_l.hw,
	[CLKID_PWM_M_SEL]		= &a9_pwm_m_sel.hw,
	[CLKID_PWM_M_DIV]		= &a9_pwm_m_div.hw,
	[CLKID_PWM_M]			= &a9_pwm_m.hw,
	[CLKID_PWM_N_SEL]		= &a9_pwm_n_sel.hw,
	[CLKID_PWM_N_DIV]		= &a9_pwm_n_div.hw,
	[CLKID_PWM_N]			= &a9_pwm_n.hw,
	[CLKID_SPISG0_SEL]		= &a9_spisg0_sel.hw,
	[CLKID_SPISG0_DIV]		= &a9_spisg0_div.hw,
	[CLKID_SPISG0]			= &a9_spisg0.hw,
	[CLKID_SPISG1_SEL]		= &a9_spisg1_sel.hw,
	[CLKID_SPISG1_DIV]		= &a9_spisg1_div.hw,
	[CLKID_SPISG1]			= &a9_spisg1.hw,
	[CLKID_SPISG2_SEL]		= &a9_spisg2_sel.hw,
	[CLKID_SPISG2_DIV]		= &a9_spisg2_div.hw,
	[CLKID_SPISG2]			= &a9_spisg2.hw,
	[CLKID_SARADC_SEL]		= &a9_saradc_sel.hw,
	[CLKID_SARADC_DIV]		= &a9_saradc_div.hw,
	[CLKID_SARADC]			= &a9_saradc.hw,
	[CLKID_AMFC_SEL]		= &a9_amfc_sel.hw,
	[CLKID_AMFC_DIV]		= &a9_amfc_div.hw,
	[CLKID_AMFC]			= &a9_amfc.hw,
	[CLKID_NNA_SEL]			= &a9_nna_sel.hw,
	[CLKID_NNA_DIV]			= &a9_nna_div.hw,
	[CLKID_NNA]			= &a9_nna.hw,
	[CLKID_USB_250M_SEL]		= &a9_usb_250m_sel.hw,
	[CLKID_USB_250M_DIV]		= &a9_usb_250m_div.hw,
	[CLKID_USB_250M]		= &a9_usb_250m.hw,
	[CLKID_USB_48M_PRE_SEL]		= &a9_usb_48m_pre_sel.hw,
	[CLKID_USB_48M_PRE_DIV]		= &a9_usb_48m_pre_div.hw,
	[CLKID_USB_48M_PRE]		= &a9_usb_48m_pre.hw,
	[CLKID_PCIE0_TL_SEL]		= &a9_pcie0_tl_sel.hw,
	[CLKID_PCIE0_TL_DIV]		= &a9_pcie0_tl_div.hw,
	[CLKID_PCIE0_TL]		= &a9_pcie0_tl.hw,
	[CLKID_PCIE1_TL_SEL]		= &a9_pcie1_tl_sel.hw,
	[CLKID_PCIE1_TL_DIV]		= &a9_pcie1_tl_div.hw,
	[CLKID_PCIE1_TL]		= &a9_pcie1_tl.hw,
	[CLKID_CMPR_SEL]		= &a9_cmpr_sel.hw,
	[CLKID_CMPR_DIV]		= &a9_cmpr_div.hw,
	[CLKID_CMPR]			= &a9_cmpr.hw,
	[CLKID_DEWARPA_SEL]		= &a9_dewarpa_sel.hw,
	[CLKID_DEWARPA_DIV]		= &a9_dewarpa_div.hw,
	[CLKID_DEWARPA]			= &a9_dewarpa.hw,
	[CLKID_SC_PRE_SEL]		= &a9_sc_pre_sel.hw,
	[CLKID_SC_PRE_DIV]		= &a9_sc_pre_div.hw,
	[CLKID_SC_PRE]			= &a9_sc_pre.hw,
	[CLKID_SC]			= &a9_sc.hw,
	[CLKID_DPTX_APB2_SEL]		= &a9_dptx_apb2_sel.hw,
	[CLKID_DPTX_APB2_DIV]		= &a9_dptx_apb2_div.hw,
	[CLKID_DPTX_APB2]		= &a9_dptx_apb2.hw,
	[CLKID_DPTX_AUD_SEL]		= &a9_dptx_aud_sel.hw,
	[CLKID_DPTX_AUD_DIV]		= &a9_dptx_aud_div.hw,
	[CLKID_DPTX_AUD]		= &a9_dptx_aud.hw,
	[CLKID_ISP_SEL]			= &a9_isp_sel.hw,
	[CLKID_ISP_DIV]			= &a9_isp_div.hw,
	[CLKID_ISP]			= &a9_isp.hw,
	[CLKID_CVE_SEL]			= &a9_cve_sel.hw,
	[CLKID_CVE_DIV]			= &a9_cve_div.hw,
	[CLKID_CVE]			= &a9_cve.hw,
	[CLKID_VGE_SEL]			= &a9_vge_sel.hw,
	[CLKID_VGE_DIV]			= &a9_vge_div.hw,
	[CLKID_VGE]			= &a9_vge.hw,
	[CLKID_PP_SEL]			= &a9_pp_sel.hw,
	[CLKID_PP_DIV]			= &a9_pp_div.hw,
	[CLKID_PP]			= &a9_pp.hw,
	[CLKID_GLB_SEL]			= &a9_glb_sel.hw,
	[CLKID_GLB_DIV]			= &a9_glb_div.hw,
	[CLKID_GLB]			= &a9_glb.hw,
	[CLKID_USB_48M_DUALDIV_IN]	= &a9_usb_48m_dualdiv_in.hw,
	[CLKID_USB_48M_DUALDIV_DIV]	= &a9_usb_48m_dualdiv_div.hw,
	[CLKID_USB_48M_DUALDIV_SEL]	= &a9_usb_48m_dualdiv_sel.hw,
	[CLKID_USB_48M_DUALDIV]		= &a9_usb_48m_dualdiv.hw,
	[CLKID_USB_48M]			= &a9_usb_48m.hw,
	[CLKID_CAN0_PE_SEL]		= &a9_can0_pe_sel.hw,
	[CLKID_CAN0_PE_DIV]		= &a9_can0_pe_div.hw,
	[CLKID_CAN0_PE]			= &a9_can0_pe.hw,
	[CLKID_CAN1_PE_SEL]		= &a9_can1_pe_sel.hw,
	[CLKID_CAN1_PE_DIV]		= &a9_can1_pe_div.hw,
	[CLKID_CAN1_PE]			= &a9_can1_pe.hw,
	[CLKID_CAN0_FILTER_SEL]		= &a9_can0_filter_sel.hw,
	[CLKID_CAN0_FILTER_DIV]		= &a9_can0_filter_div.hw,
	[CLKID_CAN0_FILTER]		= &a9_can0_filter.hw,
	[CLKID_CAN1_FILTER_SEL]		= &a9_can1_filter_sel.hw,
	[CLKID_CAN1_FILTER_DIV]		= &a9_can1_filter_div.hw,
	[CLKID_CAN1_FILTER]		= &a9_can1_filter.hw,
	[CLKID_I3C_SEL]			= &a9_i3c_sel.hw,
	[CLKID_I3C_DIV]			= &a9_i3c_div.hw,
	[CLKID_I3C]			= &a9_i3c.hw,
	[CLKID_TS_DIV]			= &a9_ts_div.hw,
	[CLKID_TS]			= &a9_ts.hw,
	[CLKID_ETH_125M_DIV]		= &a9_eth_125m_div.hw,
	[CLKID_ETH_125M]		= &a9_eth_125m.hw,
	[CLKID_ETH_RMII_SEL]		= &a9_eth_rmii_sel.hw,
	[CLKID_ETH_RMII_DIV]		= &a9_eth_rmii_div.hw,
	[CLKID_ETH_RMII]		= &a9_eth_rmii.hw,
	[CLKID_GEN_SEL]			= &a9_gen_sel.hw,
	[CLKID_GEN_DIV]			= &a9_gen_div.hw,
	[CLKID_GEN]			= &a9_gen.hw,
	[CLKID_CLK24M_IN]		= &a9_24m_in.hw,
	[CLKID_CLK12_24M]		= &a9_12_24m.hw,
	[CLKID_MALI_0_SEL]		= &a9_mali_0_sel.hw,
	[CLKID_MALI_0_DIV]		= &a9_mali_0_div.hw,
	[CLKID_MALI_0]			= &a9_mali_0.hw,
	[CLKID_MALI_1_SEL]		= &a9_mali_1_sel.hw,
	[CLKID_MALI_1_DIV]		= &a9_mali_1_div.hw,
	[CLKID_MALI_1]			= &a9_mali_1.hw,
	[CLKID_MALI]			= &a9_mali.hw,
	[CLKID_MALI_STACK_0_SEL]	= &a9_mali_stack_0_sel.hw,
	[CLKID_MALI_STACK_0_DIV]	= &a9_mali_stack_0_div.hw,
	[CLKID_MALI_STACK_0]		= &a9_mali_stack_0.hw,
	[CLKID_MALI_STACK_1_SEL]	= &a9_mali_stack_1_sel.hw,
	[CLKID_MALI_STACK_1_DIV]	= &a9_mali_stack_1_div.hw,
	[CLKID_MALI_STACK_1]		= &a9_mali_stack_1.hw,
	[CLKID_MALI_STACK]		= &a9_mali_stack.hw,
	[CLKID_DSPA_0_SEL]		= &a9_dspa_0_sel.hw,
	[CLKID_DSPA_0_DIV]		= &a9_dspa_0_div.hw,
	[CLKID_DSPA_0]			= &a9_dspa_0.hw,
	[CLKID_DSPA_1_SEL]		= &a9_dspa_1_sel.hw,
	[CLKID_DSPA_1_DIV]		= &a9_dspa_1_div.hw,
	[CLKID_DSPA_1]			= &a9_dspa_1.hw,
	[CLKID_DSPA]			= &a9_dspa.hw,
	[CLKID_HEVCF_0_SEL]		= &a9_hevcf_0_sel.hw,
	[CLKID_HEVCF_0_DIV]		= &a9_hevcf_0_div.hw,
	[CLKID_HEVCF_0]			= &a9_hevcf_0.hw,
	[CLKID_HEVCF_1_SEL]		= &a9_hevcf_1_sel.hw,
	[CLKID_HEVCF_1_DIV]		= &a9_hevcf_1_div.hw,
	[CLKID_HEVCF_1]			= &a9_hevcf_1.hw,
	[CLKID_HEVCF]			= &a9_hevcf.hw,
	[CLKID_HCODEC_0_SEL]		= &a9_hcodec_0_sel.hw,
	[CLKID_HCODEC_0_DIV]		= &a9_hcodec_0_div.hw,
	[CLKID_HCODEC_0]		= &a9_hcodec_0.hw,
	[CLKID_HCODEC_1_SEL]		= &a9_hcodec_1_sel.hw,
	[CLKID_HCODEC_1_DIV]		= &a9_hcodec_1_div.hw,
	[CLKID_HCODEC_1]		= &a9_hcodec_1.hw,
	[CLKID_HCODEC]			= &a9_hcodec.hw,
	[CLKID_VPU_0_SEL]		= &a9_vpu_0_sel.hw,
	[CLKID_VPU_0_DIV]		= &a9_vpu_0_div.hw,
	[CLKID_VPU_0]			= &a9_vpu_0.hw,
	[CLKID_VPU_1_SEL]		= &a9_vpu_1_sel.hw,
	[CLKID_VPU_1_DIV]		= &a9_vpu_1_div.hw,
	[CLKID_VPU_1]			= &a9_vpu_1.hw,
	[CLKID_VPU]			= &a9_vpu.hw,
	[CLKID_VAPB_0_SEL]		= &a9_vapb_0_sel.hw,
	[CLKID_VAPB_0_DIV]		= &a9_vapb_0_div.hw,
	[CLKID_VAPB_0]			= &a9_vapb_0.hw,
	[CLKID_VAPB_1_SEL]		= &a9_vapb_1_sel.hw,
	[CLKID_VAPB_1_DIV]		= &a9_vapb_1_div.hw,
	[CLKID_VAPB_1]			= &a9_vapb_1.hw,
	[CLKID_VAPB]			= &a9_vapb.hw,
	[CLKID_GE2D]			= &a9_ge2d.hw,
	[CLKID_VPU_CLKB_TMP_SEL]	= &a9_vpu_clkb_tmp_sel.hw,
	[CLKID_VPU_CLKB_TMP_DIV]	= &a9_vpu_clkb_tmp_div.hw,
	[CLKID_VPU_CLKB_TMP]		= &a9_vpu_clkb_tmp.hw,
	[CLKID_VPU_CLKB_DIV]		= &a9_vpu_clkb_div.hw,
	[CLKID_VPU_CLKB]		= &a9_vpu_clkb.hw,
	[CLKID_HDMITX_SYS_SEL]		= &a9_hdmitx_sys_sel.hw,
	[CLKID_HDMITX_SYS_DIV]		= &a9_hdmitx_sys_div.hw,
	[CLKID_HDMITX_SYS]		= &a9_hdmitx_sys.hw,
	[CLKID_HDMITX_PRIF_SEL]		= &a9_hdmitx_prif_sel.hw,
	[CLKID_HDMITX_PRIF_DIV]		= &a9_hdmitx_prif_div.hw,
	[CLKID_HDMITX_PRIF]		= &a9_hdmitx_prif.hw,
	[CLKID_HDMITX_200M_SEL]		= &a9_hdmitx_200m_sel.hw,
	[CLKID_HDMITX_200M_DIV]		= &a9_hdmitx_200m_div.hw,
	[CLKID_HDMITX_200M]		= &a9_hdmitx_200m.hw,
	[CLKID_HDMITX_AUD_SEL]		= &a9_hdmitx_aud_sel.hw,
	[CLKID_HDMITX_AUD_DIV]		= &a9_hdmitx_aud_div.hw,
	[CLKID_HDMITX_AUD]		= &a9_hdmitx_aud.hw,
	[CLKID_HDMIRX_5M_SEL]		= &a9_hdmirx_5m_sel.hw,
	[CLKID_HDMIRX_5M_DIV]		= &a9_hdmirx_5m_div.hw,
	[CLKID_HDMIRX_5M]		= &a9_hdmirx_5m.hw,
	[CLKID_HDMIRX_2M_SEL]		= &a9_hdmirx_2m_sel.hw,
	[CLKID_HDMIRX_2M_DIV]		= &a9_hdmirx_2m_div.hw,
	[CLKID_HDMIRX_2M]		= &a9_hdmirx_2m.hw,
	[CLKID_HDMIRX_CFG_SEL]		= &a9_hdmirx_cfg_sel.hw,
	[CLKID_HDMIRX_CFG_DIV]		= &a9_hdmirx_cfg_div.hw,
	[CLKID_HDMIRX_CFG]		= &a9_hdmirx_cfg.hw,
	[CLKID_HDMIRX_HDCP2X_SEL]	= &a9_hdmirx_hdcp2x_sel.hw,
	[CLKID_HDMIRX_HDCP2X_DIV]	= &a9_hdmirx_hdcp2x_div.hw,
	[CLKID_HDMIRX_HDCP2X]		= &a9_hdmirx_hdcp2x.hw,
	[CLKID_HDMIRX_ACR_REF_SEL]	= &a9_hdmirx_acr_ref_sel.hw,
	[CLKID_HDMIRX_ACR_REF_DIV]	= &a9_hdmirx_acr_ref_div.hw,
	[CLKID_HDMIRX_ACR_REF]		= &a9_hdmirx_acr_ref.hw,
	[CLKID_HDMIRX_METER_SEL]	= &a9_hdmirx_meter_sel.hw,
	[CLKID_HDMIRX_METER_DIV]	= &a9_hdmirx_meter_div.hw,
	[CLKID_HDMIRX_METER]		= &a9_hdmirx_meter.hw,
	[CLKID_VID_LOCK_SEL]		= &a9_vid_lock_sel.hw,
	[CLKID_VID_LOCK_DIV]		= &a9_vid_lock_div.hw,
	[CLKID_VID_LOCK]		= &a9_vid_lock.hw,
	[CLKID_VDIN_MEAS_SEL]		= &a9_vdin_meas_sel.hw,
	[CLKID_VDIN_MEAS_DIV]		= &a9_vdin_meas_div.hw,
	[CLKID_VDIN_MEAS]		= &a9_vdin_meas.hw,
	[CLKID_VID_PLL_DIV]		= &a9_vid_pll_div.hw,
	[CLKID_VID_PLL_SEL]		= &a9_vid_pll_sel.hw,
	[CLKID_VID_PLL]			= &a9_vid_pll.hw,
	[CLKID_VID_PLL_VCLK]		= &a9_vid_pll_vclk.hw,
	[CLKID_VCLK0_SEL]		= &a9_vclk0_sel.hw,
	[CLKID_VCLK0_IN]		= &a9_vclk0_in.hw,
	[CLKID_VCLK0_DIV]		= &a9_vclk0_div.hw,
	[CLKID_VCLK0]			= &a9_vclk0.hw,
	[CLKID_VCLK0_DIV1_EN]		= &a9_vclk0_div1_en.hw,
	[CLKID_VCLK0_DIV2_EN]		= &a9_vclk0_div2_en.hw,
	[CLKID_VCLK0_DIV2]		= &a9_vclk0_div2.hw,
	[CLKID_VCLK0_DIV4_EN]		= &a9_vclk0_div4_en.hw,
	[CLKID_VCLK0_DIV4]		= &a9_vclk0_div4.hw,
	[CLKID_VCLK0_DIV6_EN]		= &a9_vclk0_div6_en.hw,
	[CLKID_VCLK0_DIV6]		= &a9_vclk0_div6.hw,
	[CLKID_VCLK0_DIV12_EN]		= &a9_vclk0_div12_en.hw,
	[CLKID_VCLK0_DIV12]		= &a9_vclk0_div12.hw,
	[CLKID_VCLK1_SEL]		= &a9_vclk1_sel.hw,
	[CLKID_VCLK1_IN]		= &a9_vclk1_in.hw,
	[CLKID_VCLK1_DIV]		= &a9_vclk1_div.hw,
	[CLKID_VCLK1]			= &a9_vclk1.hw,
	[CLKID_VCLK1_DIV1_EN]		= &a9_vclk1_div1_en.hw,
	[CLKID_VCLK1_DIV2_EN]		= &a9_vclk1_div2_en.hw,
	[CLKID_VCLK1_DIV2]		= &a9_vclk1_div2.hw,
	[CLKID_VCLK1_DIV4_EN]		= &a9_vclk1_div4_en.hw,
	[CLKID_VCLK1_DIV4]		= &a9_vclk1_div4.hw,
	[CLKID_VCLK1_DIV6_EN]		= &a9_vclk1_div6_en.hw,
	[CLKID_VCLK1_DIV6]		= &a9_vclk1_div6.hw,
	[CLKID_VCLK1_DIV12_EN]		= &a9_vclk1_div12_en.hw,
	[CLKID_VCLK1_DIV12]		= &a9_vclk1_div12.hw,
	[CLKID_VDAC_SEL]		= &a9_vdac_sel.hw,
	[CLKID_VDAC]			= &a9_vdac.hw,
	[CLKID_ENCODER0_SEL]		= &a9_encoder0_sel.hw,
	[CLKID_ENCODER0]		= &a9_encoder0.hw,
	[CLKID_ENCODER1_SEL]		= &a9_encoder1_sel.hw,
	[CLKID_ENCODER1]		= &a9_encoder1.hw,
	[CLKID_HDMITX0_PIXEL_SEL]	= &a9_hdmitx0_pixel_sel.hw,
	[CLKID_HDMITX0_PIXEL]		= &a9_hdmitx0_pixel.hw,
	[CLKID_HDMITX0_FE_SEL]		= &a9_hdmitx0_fe_sel.hw,
	[CLKID_HDMITX0_FE]		= &a9_hdmitx0_fe.hw,
	[CLKID_HDMITX1_PIXEL_SEL]	= &a9_hdmitx1_pixel_sel.hw,
	[CLKID_HDMITX1_PIXEL]		= &a9_hdmitx1_pixel.hw,
	[CLKID_HDMITX1_FE_SEL]		= &a9_hdmitx1_fe_sel.hw,
	[CLKID_HDMITX1_FE]		= &a9_hdmitx1_fe.hw,
	[CLKID_CSI_PHY_SEL]		= &a9_csi_phy_sel.hw,
	[CLKID_CSI_PHY_DIV]		= &a9_csi_phy_div.hw,
	[CLKID_CSI_PHY]			= &a9_csi_phy.hw,
	[CLKID_DSI0_MEAS_SEL]		= &a9_dsi0_meas_sel.hw,
	[CLKID_DSI0_MEAS_DIV]		= &a9_dsi0_meas_div.hw,
	[CLKID_DSI0_MEAS]		= &a9_dsi0_meas.hw,
	[CLKID_DSI1_MEAS_SEL]		= &a9_dsi1_meas_sel.hw,
	[CLKID_DSI1_MEAS_DIV]		= &a9_dsi1_meas_div.hw,
	[CLKID_DSI1_MEAS]		= &a9_dsi1_meas.hw,
};

static const struct meson_clkc_data a9_peripherals_clkc_data = {
	.hw_clks = {
		.hws = a9_peripherals_hw_clks,
		.num = ARRAY_SIZE(a9_peripherals_hw_clks),
	},
};

static const struct of_device_id a9_peripherals_clkc_match_table[] = {
	{
		.compatible = "amlogic,a9-peripherals-clkc",
		.data = &a9_peripherals_clkc_data,
	},
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, a9_peripherals_clkc_match_table);

static struct platform_driver a9_peripherals_clkc_driver = {
	.probe		= meson_clkc_mmio_probe,
	.driver		= {
		.name	= "a9-peripherals-clkc",
		.of_match_table = a9_peripherals_clkc_match_table,
	},
};
module_platform_driver(a9_peripherals_clkc_driver);

MODULE_DESCRIPTION("Amlogic A9 Peripherals Clock Controller driver");
MODULE_AUTHOR("Jian Hu <jian.hu@amlogic.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("CLK_MESON");
