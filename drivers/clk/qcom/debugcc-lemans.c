// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "clk: %s: " fmt, __func__

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "clk-debug.h"
#include "common.h"

static struct measure_clk_data debug_mux_priv = {
	.ctl_reg = 0x72038,
	.status_reg = 0x7203C,
	.xo_div4_cbcr = 0x6E008,
};

static const char *const apss_cc_debug_mux_parent_names[] = {
	"measure_only_apcs_cl1_l3_post_acd_clk",
	"measure_only_apcs_cl0_gold_post_acd_clk",
	"measure_only_apcs_cl1_goldplus_post_acd_clk",
	"measure_only_apcs_cl0_l3_post_acd_clk",
};

static int apss_cc_debug_mux_sels[] = {
	0x61,		/* measure_only_apcs_cl1_l3_post_acd_clk */
	0x21,		/* measure_only_apcs_cl0_gold_post_acd_clk */
	0x25,		/* measure_only_apcs_cl1_goldplus_post_acd_clk */
	0x41,		/* measure_only_apcs_cl0_l3_post_acd_clk */
};

static int apss_cc_debug_mux_pre_divs[] = {
	0x4,		/* measure_only_apcs_cl1_l3_post_acd_clk */
	0x8,		/* measure_only_apcs_cl0_gold_post_acd_clk */
	0x8,		/* measure_only_apcs_cl1_goldplus_post_acd_clk */
	0x4,		/* measure_only_apcs_cl0_l3_post_acd_clk */
};

static struct clk_debug_mux apss_cc_debug_mux = {
	.priv = &debug_mux_priv,
	.debug_offset = 0x18,
	.post_div_offset = 0x18,
	.cbcr_offset = 0x0,
	.src_sel_mask = 0x7F0,
	.src_sel_shift = 4,
	.post_div_mask = 0x7800,
	.post_div_shift = 11,
	.post_div_val = 1,
	.mux_sels = apss_cc_debug_mux_sels,
	.pre_div_vals = apss_cc_debug_mux_pre_divs,
	.hw.init = &(const struct clk_init_data){
		.name = "apss_cc_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = apss_cc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(apss_cc_debug_mux_parent_names),
	},
};

static const char *const cam_cc_debug_mux_parent_names[] = {
	"cam_cc_camnoc_axi_clk",
	"cam_cc_camnoc_dcd_xo_clk",
	"cam_cc_camnoc_xo_clk",
	"cam_cc_cci_0_clk",
	"cam_cc_cci_1_clk",
	"cam_cc_cci_2_clk",
	"cam_cc_cci_3_clk",
	"cam_cc_core_ahb_clk",
	"cam_cc_cpas_ahb_clk",
	"cam_cc_cpas_fast_ahb_clk",
	"cam_cc_cpas_ife_0_clk",
	"cam_cc_cpas_ife_1_clk",
	"cam_cc_cpas_ife_lite_clk",
	"cam_cc_cpas_ipe_clk",
	"cam_cc_cpas_sfe_lite_0_clk",
	"cam_cc_cpas_sfe_lite_1_clk",
	"cam_cc_csi0phytimer_clk",
	"cam_cc_csi1phytimer_clk",
	"cam_cc_csi2phytimer_clk",
	"cam_cc_csi3phytimer_clk",
	"cam_cc_csid_clk",
	"cam_cc_csid_csiphy_rx_clk",
	"cam_cc_csiphy0_clk",
	"cam_cc_csiphy1_clk",
	"cam_cc_csiphy2_clk",
	"cam_cc_csiphy3_clk",
	"cam_cc_icp_ahb_clk",
	"cam_cc_icp_clk",
	"cam_cc_ife_0_clk",
	"cam_cc_ife_0_fast_ahb_clk",
	"cam_cc_ife_1_clk",
	"cam_cc_ife_1_fast_ahb_clk",
	"cam_cc_ife_lite_ahb_clk",
	"cam_cc_ife_lite_clk",
	"cam_cc_ife_lite_cphy_rx_clk",
	"cam_cc_ife_lite_csid_clk",
	"cam_cc_ipe_ahb_clk",
	"cam_cc_ipe_clk",
	"cam_cc_ipe_fast_ahb_clk",
	"cam_cc_mclk0_clk",
	"cam_cc_mclk1_clk",
	"cam_cc_mclk2_clk",
	"cam_cc_mclk3_clk",
	"cam_cc_sfe_lite_0_clk",
	"cam_cc_sfe_lite_0_fast_ahb_clk",
	"cam_cc_sfe_lite_1_clk",
	"cam_cc_sfe_lite_1_fast_ahb_clk",
	"cam_cc_sleep_clk",
	"measure_only_cam_cc_gdsc_clk",
};

static int cam_cc_debug_mux_sels[] = {
	0x35,		/* cam_cc_camnoc_axi_clk */
	0x36,		/* cam_cc_camnoc_dcd_xo_clk */
	0x37,		/* cam_cc_camnoc_xo_clk */
	0x30,		/* cam_cc_cci_0_clk */
	0x31,		/* cam_cc_cci_1_clk */
	0x5,		/* cam_cc_cci_2_clk */
	0x6,		/* cam_cc_cci_3_clk */
	0x3A,		/* cam_cc_core_ahb_clk */
	0x32,		/* cam_cc_cpas_ahb_clk */
	0x33,		/* cam_cc_cpas_fast_ahb_clk */
	0x18,		/* cam_cc_cpas_ife_0_clk */
	0x1D,		/* cam_cc_cpas_ife_1_clk */
	0x22,		/* cam_cc_cpas_ife_lite_clk */
	0x13,		/* cam_cc_cpas_ipe_clk */
	0x27,		/* cam_cc_cpas_sfe_lite_0_clk */
	0x2B,		/* cam_cc_cpas_sfe_lite_1_clk */
	0x9,		/* cam_cc_csi0phytimer_clk */
	0xC,		/* cam_cc_csi1phytimer_clk */
	0xE,		/* cam_cc_csi2phytimer_clk */
	0x10,		/* cam_cc_csi3phytimer_clk */
	0x34,		/* cam_cc_csid_clk */
	0xB,		/* cam_cc_csid_csiphy_rx_clk */
	0xA,		/* cam_cc_csiphy0_clk */
	0xD,		/* cam_cc_csiphy1_clk */
	0xF,		/* cam_cc_csiphy2_clk */
	0x11,		/* cam_cc_csiphy3_clk */
	0x2F,		/* cam_cc_icp_ahb_clk */
	0x2E,		/* cam_cc_icp_clk */
	0x17,		/* cam_cc_ife_0_clk */
	0x1B,		/* cam_cc_ife_0_fast_ahb_clk */
	0x1C,		/* cam_cc_ife_1_clk */
	0x20,		/* cam_cc_ife_1_fast_ahb_clk */
	0x25,		/* cam_cc_ife_lite_ahb_clk */
	0x21,		/* cam_cc_ife_lite_clk */
	0x24,		/* cam_cc_ife_lite_cphy_rx_clk */
	0x23,		/* cam_cc_ife_lite_csid_clk */
	0x15,		/* cam_cc_ipe_ahb_clk */
	0x12,		/* cam_cc_ipe_clk */
	0x16,		/* cam_cc_ipe_fast_ahb_clk */
	0x1,		/* cam_cc_mclk0_clk */
	0x2,		/* cam_cc_mclk1_clk */
	0x3,		/* cam_cc_mclk2_clk */
	0x4,		/* cam_cc_mclk3_clk */
	0x26,		/* cam_cc_sfe_lite_0_clk */
	0x29,		/* cam_cc_sfe_lite_0_fast_ahb_clk */
	0x2A,		/* cam_cc_sfe_lite_1_clk */
	0x2D,		/* cam_cc_sfe_lite_1_fast_ahb_clk */
	0x3C,		/* cam_cc_sleep_clk */
	0x3B,		/* measure_only_cam_cc_gdsc_clk */
};

static struct clk_debug_mux cam_cc_debug_mux = {
	.priv = &debug_mux_priv,
	.debug_offset = 0x150F8,
	.post_div_offset = 0x14004,
	.cbcr_offset = 0x14008,
	.src_sel_mask = 0xFF,
	.src_sel_shift = 0,
	.post_div_mask = 0xF,
	.post_div_shift = 0,
	.post_div_val = 4,
	.mux_sels = cam_cc_debug_mux_sels,
	.hw.init = &(const struct clk_init_data){
		.name = "cam_cc_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = cam_cc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(cam_cc_debug_mux_parent_names),
	},
};

static const char *const disp_cc_0_debug_mux_parent_names[] = {
	"mdss_0_disp_cc_mdss_ahb1_clk",
	"mdss_0_disp_cc_mdss_ahb_clk",
	"mdss_0_disp_cc_mdss_byte0_clk",
	"mdss_0_disp_cc_mdss_byte0_intf_clk",
	"mdss_0_disp_cc_mdss_byte1_clk",
	"mdss_0_disp_cc_mdss_byte1_intf_clk",
	"mdss_0_disp_cc_mdss_dptx0_aux_clk",
	"mdss_0_disp_cc_mdss_dptx0_crypto_clk",
	"mdss_0_disp_cc_mdss_dptx0_link_clk",
	"mdss_0_disp_cc_mdss_dptx0_link_intf_clk",
	"mdss_0_disp_cc_mdss_dptx0_pixel0_clk",
	"mdss_0_disp_cc_mdss_dptx0_pixel1_clk",
	"mdss_0_disp_cc_mdss_dptx0_pixel2_clk",
	"mdss_0_disp_cc_mdss_dptx0_pixel3_clk",
	"mdss_0_disp_cc_mdss_dptx0_usb_router_link_intf_clk",
	"mdss_0_disp_cc_mdss_dptx1_aux_clk",
	"mdss_0_disp_cc_mdss_dptx1_crypto_clk",
	"mdss_0_disp_cc_mdss_dptx1_link_clk",
	"mdss_0_disp_cc_mdss_dptx1_link_intf_clk",
	"mdss_0_disp_cc_mdss_dptx1_pixel0_clk",
	"mdss_0_disp_cc_mdss_dptx1_pixel1_clk",
	"mdss_0_disp_cc_mdss_dptx1_usb_router_link_intf_clk",
	"mdss_0_disp_cc_mdss_esc0_clk",
	"mdss_0_disp_cc_mdss_esc1_clk",
	"mdss_0_disp_cc_mdss_mdp1_clk",
	"mdss_0_disp_cc_mdss_mdp_clk",
	"mdss_0_disp_cc_mdss_mdp_lut1_clk",
	"mdss_0_disp_cc_mdss_mdp_lut_clk",
	"mdss_0_disp_cc_mdss_non_gdsc_ahb_clk",
	"mdss_0_disp_cc_mdss_pclk0_clk",
	"mdss_0_disp_cc_mdss_pclk1_clk",
	"mdss_0_disp_cc_mdss_rscc_ahb_clk",
	"mdss_0_disp_cc_mdss_rscc_vsync_clk",
	"mdss_0_disp_cc_mdss_vsync1_clk",
	"mdss_0_disp_cc_mdss_vsync_clk",
	"mdss_0_disp_cc_sleep_clk",
	"measure_only_mdss_0_disp_cc_xo_clk",
};

static int disp_cc_0_debug_mux_sels[] = {
	0x27,		/* mdss_0_disp_cc_mdss_ahb1_clk */
	0x26,		/* mdss_0_disp_cc_mdss_ahb_clk */
	0x12,		/* mdss_0_disp_cc_mdss_byte0_clk */
	0x13,		/* mdss_0_disp_cc_mdss_byte0_intf_clk */
	0x14,		/* mdss_0_disp_cc_mdss_byte1_clk */
	0x15,		/* mdss_0_disp_cc_mdss_byte1_intf_clk */
	0x1C,		/* mdss_0_disp_cc_mdss_dptx0_aux_clk */
	0x1B,		/* mdss_0_disp_cc_mdss_dptx0_crypto_clk */
	0x18,		/* mdss_0_disp_cc_mdss_dptx0_link_clk */
	0x19,		/* mdss_0_disp_cc_mdss_dptx0_link_intf_clk */
	0x1D,		/* mdss_0_disp_cc_mdss_dptx0_pixel0_clk */
	0x1E,		/* mdss_0_disp_cc_mdss_dptx0_pixel1_clk */
	0x28,		/* mdss_0_disp_cc_mdss_dptx0_pixel2_clk */
	0x29,		/* mdss_0_disp_cc_mdss_dptx0_pixel3_clk */
	0x1A,		/* mdss_0_disp_cc_mdss_dptx0_usb_router_link_intf_clk */
	0x25,		/* mdss_0_disp_cc_mdss_dptx1_aux_clk */
	0x24,		/* mdss_0_disp_cc_mdss_dptx1_crypto_clk */
	0x21,		/* mdss_0_disp_cc_mdss_dptx1_link_clk */
	0x22,		/* mdss_0_disp_cc_mdss_dptx1_link_intf_clk */
	0x1F,		/* mdss_0_disp_cc_mdss_dptx1_pixel0_clk */
	0x20,		/* mdss_0_disp_cc_mdss_dptx1_pixel1_clk */
	0x23,		/* mdss_0_disp_cc_mdss_dptx1_usb_router_link_intf_clk */
	0x16,		/* mdss_0_disp_cc_mdss_esc0_clk */
	0x17,		/* mdss_0_disp_cc_mdss_esc1_clk */
	0xD,		/* mdss_0_disp_cc_mdss_mdp1_clk */
	0xC,		/* mdss_0_disp_cc_mdss_mdp_clk */
	0xF,		/* mdss_0_disp_cc_mdss_mdp_lut1_clk */
	0xE,		/* mdss_0_disp_cc_mdss_mdp_lut_clk */
	0x2A,		/* mdss_0_disp_cc_mdss_non_gdsc_ahb_clk */
	0xA,		/* mdss_0_disp_cc_mdss_pclk0_clk */
	0xB,		/* mdss_0_disp_cc_mdss_pclk1_clk */
	0x2C,		/* mdss_0_disp_cc_mdss_rscc_ahb_clk */
	0x2B,		/* mdss_0_disp_cc_mdss_rscc_vsync_clk */
	0x11,		/* mdss_0_disp_cc_mdss_vsync1_clk */
	0x10,		/* mdss_0_disp_cc_mdss_vsync_clk */
	0x35,		/* mdss_0_disp_cc_sleep_clk */
	0x34,		/* measure_only_mdss_0_disp_cc_xo_clk */
};

static struct clk_debug_mux disp_cc_0_debug_mux = {
	.priv = &debug_mux_priv,
	.debug_offset = 0x11000,
	.post_div_offset = 0xB000,
	.cbcr_offset = 0xB004,
	.src_sel_mask = 0xFF,
	.src_sel_shift = 0,
	.post_div_mask = 0xF,
	.post_div_shift = 0,
	.post_div_val = 4,
	.mux_sels = disp_cc_0_debug_mux_sels,
	.hw.init = &(const struct clk_init_data){
		.name = "disp_cc_0_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = disp_cc_0_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(disp_cc_0_debug_mux_parent_names),
	},
};

static const char *const disp_cc_1_debug_mux_parent_names[] = {
	"mdss_1_disp_cc_mdss_ahb1_clk",
	"mdss_1_disp_cc_mdss_ahb_clk",
	"mdss_1_disp_cc_mdss_byte0_clk",
	"mdss_1_disp_cc_mdss_byte0_intf_clk",
	"mdss_1_disp_cc_mdss_byte1_clk",
	"mdss_1_disp_cc_mdss_byte1_intf_clk",
	"mdss_1_disp_cc_mdss_dptx0_aux_clk",
	"mdss_1_disp_cc_mdss_dptx0_crypto_clk",
	"mdss_1_disp_cc_mdss_dptx0_link_clk",
	"mdss_1_disp_cc_mdss_dptx0_link_intf_clk",
	"mdss_1_disp_cc_mdss_dptx0_pixel0_clk",
	"mdss_1_disp_cc_mdss_dptx0_pixel1_clk",
	"mdss_1_disp_cc_mdss_dptx0_pixel2_clk",
	"mdss_1_disp_cc_mdss_dptx0_pixel3_clk",
	"mdss_1_disp_cc_mdss_dptx0_usb_router_link_intf_clk",
	"mdss_1_disp_cc_mdss_dptx1_aux_clk",
	"mdss_1_disp_cc_mdss_dptx1_crypto_clk",
	"mdss_1_disp_cc_mdss_dptx1_link_clk",
	"mdss_1_disp_cc_mdss_dptx1_link_intf_clk",
	"mdss_1_disp_cc_mdss_dptx1_pixel0_clk",
	"mdss_1_disp_cc_mdss_dptx1_pixel1_clk",
	"mdss_1_disp_cc_mdss_dptx1_usb_router_link_intf_clk",
	"mdss_1_disp_cc_mdss_esc0_clk",
	"mdss_1_disp_cc_mdss_esc1_clk",
	"mdss_1_disp_cc_mdss_mdp1_clk",
	"mdss_1_disp_cc_mdss_mdp_clk",
	"mdss_1_disp_cc_mdss_mdp_lut1_clk",
	"mdss_1_disp_cc_mdss_mdp_lut_clk",
	"mdss_1_disp_cc_mdss_non_gdsc_ahb_clk",
	"mdss_1_disp_cc_mdss_pclk0_clk",
	"mdss_1_disp_cc_mdss_pclk1_clk",
	"mdss_1_disp_cc_mdss_rscc_ahb_clk",
	"mdss_1_disp_cc_mdss_rscc_vsync_clk",
	"mdss_1_disp_cc_mdss_vsync1_clk",
	"mdss_1_disp_cc_mdss_vsync_clk",
	"mdss_1_disp_cc_sleep_clk",
	"measure_only_mdss_1_disp_cc_xo_clk",
};

static int disp_cc_1_debug_mux_sels[] = {
	0x27,		/* mdss_1_disp_cc_mdss_ahb1_clk */
	0x26,		/* mdss_1_disp_cc_mdss_ahb_clk */
	0x12,		/* mdss_1_disp_cc_mdss_byte0_clk */
	0x13,		/* mdss_1_disp_cc_mdss_byte0_intf_clk */
	0x14,		/* mdss_1_disp_cc_mdss_byte1_clk */
	0x15,		/* mdss_1_disp_cc_mdss_byte1_intf_clk */
	0x1C,		/* mdss_1_disp_cc_mdss_dptx0_aux_clk */
	0x1B,		/* mdss_1_disp_cc_mdss_dptx0_crypto_clk */
	0x18,		/* mdss_1_disp_cc_mdss_dptx0_link_clk */
	0x19,		/* mdss_1_disp_cc_mdss_dptx0_link_intf_clk */
	0x1D,		/* mdss_1_disp_cc_mdss_dptx0_pixel0_clk */
	0x1E,		/* mdss_1_disp_cc_mdss_dptx0_pixel1_clk */
	0x28,		/* mdss_1_disp_cc_mdss_dptx0_pixel2_clk */
	0x29,		/* mdss_1_disp_cc_mdss_dptx0_pixel3_clk */
	0x1A,		/* mdss_1_disp_cc_mdss_dptx0_usb_router_link_intf_clk */
	0x25,		/* mdss_1_disp_cc_mdss_dptx1_aux_clk */
	0x24,		/* mdss_1_disp_cc_mdss_dptx1_crypto_clk */
	0x21,		/* mdss_1_disp_cc_mdss_dptx1_link_clk */
	0x22,		/* mdss_1_disp_cc_mdss_dptx1_link_intf_clk */
	0x1F,		/* mdss_1_disp_cc_mdss_dptx1_pixel0_clk */
	0x20,		/* mdss_1_disp_cc_mdss_dptx1_pixel1_clk */
	0x23,		/* mdss_1_disp_cc_mdss_dptx1_usb_router_link_intf_clk */
	0x16,		/* mdss_1_disp_cc_mdss_esc0_clk */
	0x17,		/* mdss_1_disp_cc_mdss_esc1_clk */
	0xD,		/* mdss_1_disp_cc_mdss_mdp1_clk */
	0xC,		/* mdss_1_disp_cc_mdss_mdp_clk */
	0xF,		/* mdss_1_disp_cc_mdss_mdp_lut1_clk */
	0xE,		/* mdss_1_disp_cc_mdss_mdp_lut_clk */
	0x2A,		/* mdss_1_disp_cc_mdss_non_gdsc_ahb_clk */
	0xA,		/* mdss_1_disp_cc_mdss_pclk0_clk */
	0xB,		/* mdss_1_disp_cc_mdss_pclk1_clk */
	0x2C,		/* mdss_1_disp_cc_mdss_rscc_ahb_clk */
	0x2B,		/* mdss_1_disp_cc_mdss_rscc_vsync_clk */
	0x11,		/* mdss_1_disp_cc_mdss_vsync1_clk */
	0x10,		/* mdss_1_disp_cc_mdss_vsync_clk */
	0x35,		/* mdss_1_disp_cc_sleep_clk */
	0x34,		/* measure_only_mdss_1_disp_cc_xo_clk */
};

static struct clk_debug_mux disp_cc_1_debug_mux = {
	.priv = &debug_mux_priv,
	.debug_offset = 0x11000,
	.post_div_offset = 0xB000,
	.cbcr_offset = 0xB004,
	.src_sel_mask = 0xFF,
	.src_sel_shift = 0,
	.post_div_mask = 0xF,
	.post_div_shift = 0,
	.post_div_val = 4,
	.mux_sels = disp_cc_1_debug_mux_sels,
	.hw.init = &(const struct clk_init_data){
		.name = "disp_cc_1_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = disp_cc_1_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(disp_cc_1_debug_mux_parent_names),
	},
};

static const char *const gcc_debug_mux_parent_names[] = {
	"apss_cc_debug_mux",
	"cam_cc_debug_mux",
	"disp_cc_0_debug_mux",
	"disp_cc_1_debug_mux",
	"gcc_aggre_noc_qupv3_axi_clk",
	"gcc_aggre_ufs_card_axi_clk",
	"gcc_aggre_ufs_phy_axi_clk",
	"gcc_aggre_usb2_prim_axi_clk",
	"gcc_aggre_usb3_prim_axi_clk",
	"gcc_aggre_usb3_sec_axi_clk",
	"gcc_ahb2phy0_clk",
	"gcc_ahb2phy2_clk",
	"gcc_ahb2phy3_clk",
	"gcc_boot_rom_ahb_clk",
	"gcc_camera_hf_axi_clk",
	"gcc_camera_sf_axi_clk",
	"gcc_camera_throttle_xo_clk",
	"gcc_cfg_noc_usb2_prim_axi_clk",
	"gcc_cfg_noc_usb3_prim_axi_clk",
	"gcc_cfg_noc_usb3_sec_axi_clk",
	"gcc_ddrss_gpu_axi_clk",
	"gcc_disp1_hf_axi_clk",
	"gcc_disp_hf_axi_clk",
	"gcc_emac0_axi_clk",
	"gcc_emac0_phy_aux_clk",
	"gcc_emac0_ptp_clk",
	"gcc_emac0_rgmii_clk",
	"gcc_emac0_slv_ahb_clk",
	"gcc_emac1_axi_clk",
	"gcc_emac1_phy_aux_clk",
	"gcc_emac1_ptp_clk",
	"gcc_emac1_rgmii_clk",
	"gcc_emac1_slv_ahb_clk",
	"gcc_gp1_clk",
	"gcc_gp2_clk",
	"gcc_gp3_clk",
	"gcc_gp4_clk",
	"gcc_gp5_clk",
	"gcc_gpu_gpll0_clk_src",
	"gcc_gpu_gpll0_div_clk_src",
	"gcc_gpu_memnoc_gfx_clk",
	"gcc_gpu_snoc_dvm_gfx_clk",
	"gcc_gpu_tcu_throttle_ahb_clk",
	"gcc_gpu_tcu_throttle_clk",
	"gcc_pcie_0_aux_clk",
	"gcc_pcie_0_cfg_ahb_clk",
	"gcc_pcie_0_mstr_axi_clk",
	"gcc_pcie_0_phy_aux_clk",
	"gcc_pcie_0_phy_rchng_clk",
	"gcc_pcie_0_pipe_clk",
	"gcc_pcie_0_pipediv2_clk",
	"gcc_pcie_0_slv_axi_clk",
	"gcc_pcie_0_slv_q2a_axi_clk",
	"gcc_pcie_1_aux_clk",
	"gcc_pcie_1_cfg_ahb_clk",
	"gcc_pcie_1_mstr_axi_clk",
	"gcc_pcie_1_phy_aux_clk",
	"gcc_pcie_1_phy_rchng_clk",
	"gcc_pcie_1_pipe_clk",
	"gcc_pcie_1_pipediv2_clk",
	"gcc_pcie_1_slv_axi_clk",
	"gcc_pcie_1_slv_q2a_axi_clk",
	"gcc_pcie_throttle_cfg_clk",
	"gcc_pdm2_clk",
	"gcc_pdm_ahb_clk",
	"gcc_pdm_xo4_clk",
	"gcc_qmip_camera_nrt_ahb_clk",
	"gcc_qmip_camera_rt_ahb_clk",
	"gcc_qmip_disp1_ahb_clk",
	"gcc_qmip_disp1_rot_ahb_clk",
	"gcc_qmip_disp_ahb_clk",
	"gcc_qmip_disp_rot_ahb_clk",
	"gcc_qmip_video_cvp_ahb_clk",
	"gcc_qmip_video_vcodec_ahb_clk",
	"gcc_qmip_video_vcpu_ahb_clk",
	"gcc_qupv3_wrap0_core_2x_clk",
	"gcc_qupv3_wrap0_core_clk",
	"gcc_qupv3_wrap0_s0_clk",
	"gcc_qupv3_wrap0_s1_clk",
	"gcc_qupv3_wrap0_s2_clk",
	"gcc_qupv3_wrap0_s3_clk",
	"gcc_qupv3_wrap0_s4_clk",
	"gcc_qupv3_wrap0_s5_clk",
	"gcc_qupv3_wrap0_s6_clk",
	"gcc_qupv3_wrap1_core_2x_clk",
	"gcc_qupv3_wrap1_core_clk",
	"gcc_qupv3_wrap1_s0_clk",
	"gcc_qupv3_wrap1_s1_clk",
	"gcc_qupv3_wrap1_s2_clk",
	"gcc_qupv3_wrap1_s3_clk",
	"gcc_qupv3_wrap1_s4_clk",
	"gcc_qupv3_wrap1_s5_clk",
	"gcc_qupv3_wrap1_s6_clk",
	"gcc_qupv3_wrap2_core_2x_clk",
	"gcc_qupv3_wrap2_core_clk",
	"gcc_qupv3_wrap2_s0_clk",
	"gcc_qupv3_wrap2_s1_clk",
	"gcc_qupv3_wrap2_s2_clk",
	"gcc_qupv3_wrap2_s3_clk",
	"gcc_qupv3_wrap2_s4_clk",
	"gcc_qupv3_wrap2_s5_clk",
	"gcc_qupv3_wrap2_s6_clk",
	"gcc_qupv3_wrap3_core_2x_clk",
	"gcc_qupv3_wrap3_core_clk",
	"gcc_qupv3_wrap3_qspi_clk",
	"gcc_qupv3_wrap3_s0_clk",
	"gcc_qupv3_wrap_0_m_ahb_clk",
	"gcc_qupv3_wrap_0_s_ahb_clk",
	"gcc_qupv3_wrap_1_m_ahb_clk",
	"gcc_qupv3_wrap_1_s_ahb_clk",
	"gcc_qupv3_wrap_2_m_ahb_clk",
	"gcc_qupv3_wrap_2_s_ahb_clk",
	"gcc_qupv3_wrap_3_m_ahb_clk",
	"gcc_qupv3_wrap_3_s_ahb_clk",
	"gcc_sdcc1_ahb_clk",
	"gcc_sdcc1_apps_clk",
	"gcc_sdcc1_ice_core_clk",
	"gcc_tscss_ahb_clk",
	"gcc_tscss_etu_clk",
	"gcc_tscss_global_cntr_clk",
	"gcc_ufs_card_ahb_clk",
	"gcc_ufs_card_axi_clk",
	"gcc_ufs_card_ice_core_clk",
	"gcc_ufs_card_phy_aux_clk",
	"gcc_ufs_card_rx_symbol_0_clk",
	"gcc_ufs_card_rx_symbol_1_clk",
	"gcc_ufs_card_tx_symbol_0_clk",
	"gcc_ufs_card_unipro_core_clk",
	"gcc_ufs_phy_ahb_clk",
	"gcc_ufs_phy_axi_clk",
	"gcc_ufs_phy_ice_core_clk",
	"gcc_ufs_phy_phy_aux_clk",
	"gcc_ufs_phy_rx_symbol_0_clk",
	"gcc_ufs_phy_rx_symbol_1_clk",
	"gcc_ufs_phy_tx_symbol_0_clk",
	"gcc_ufs_phy_unipro_core_clk",
	"gcc_usb20_master_clk",
	"gcc_usb20_mock_utmi_clk",
	"gcc_usb20_sleep_clk",
	"gcc_usb30_prim_master_clk",
	"gcc_usb30_prim_mock_utmi_clk",
	"gcc_usb30_prim_sleep_clk",
	"gcc_usb30_sec_master_clk",
	"gcc_usb30_sec_mock_utmi_clk",
	"gcc_usb30_sec_sleep_clk",
	"gcc_usb3_prim_phy_aux_clk",
	"gcc_usb3_prim_phy_com_aux_clk",
	"gcc_usb3_prim_phy_pipe_clk",
	"gcc_usb3_sec_phy_aux_clk",
	"gcc_usb3_sec_phy_com_aux_clk",
	"gcc_usb3_sec_phy_pipe_clk",
	"gcc_video_axi0_clk",
	"gcc_video_axi1_clk",
	"gpu_cc_debug_mux",
	"measure_only_cnoc_clk",
	"measure_only_gcc_camera_ahb_clk",
	"measure_only_gcc_camera_xo_clk",
	"measure_only_gcc_disp1_ahb_clk",
	"measure_only_gcc_disp1_xo_clk",
	"measure_only_gcc_disp_ahb_clk",
	"measure_only_gcc_disp_xo_clk",
	"measure_only_gcc_gpu_cfg_ahb_clk",
	"measure_only_gcc_video_ahb_clk",
	"measure_only_gcc_video_xo_clk",
	"measure_only_ipa_2x_clk",
	"measure_only_memnoc_clk",
	"measure_only_snoc_clk",
	"mc_cc_debug_mux",
	"video_cc_debug_mux",
};

static int gcc_debug_mux_sels[] = {
	0x175,		/* apss_cc_debug_mux */
	0x8C,		/* cam_cc_debug_mux */
	0x92,		/* disp_cc_0_debug_mux */
	0x98,		/* disp_cc_1_debug_mux */
	0x224,		/* gcc_aggre_noc_qupv3_axi_clk */
	0x1E8,		/* gcc_aggre_ufs_card_axi_clk */
	0x1E7,		/* gcc_aggre_ufs_phy_axi_clk */
	0x1E5,		/* gcc_aggre_usb2_prim_axi_clk */
	0x1E3,		/* gcc_aggre_usb3_prim_axi_clk */
	0x1E4,		/* gcc_aggre_usb3_sec_axi_clk */
	0xED,		/* gcc_ahb2phy0_clk */
	0xEE,		/* gcc_ahb2phy2_clk */
	0xEF,		/* gcc_ahb2phy3_clk */
	0x114,		/* gcc_boot_rom_ahb_clk */
	0x86,		/* gcc_camera_hf_axi_clk */
	0x88,		/* gcc_camera_sf_axi_clk */
	0x8B,		/* gcc_camera_throttle_xo_clk */
	0x39,		/* gcc_cfg_noc_usb2_prim_axi_clk */
	0x37,		/* gcc_cfg_noc_usb3_prim_axi_clk */
	0x38,		/* gcc_cfg_noc_usb3_sec_axi_clk */
	0x13E,		/* gcc_ddrss_gpu_axi_clk */
	0x96,		/* gcc_disp1_hf_axi_clk */
	0x90,		/* gcc_disp_hf_axi_clk */
	0x204,		/* gcc_emac0_axi_clk */
	0x206,		/* gcc_emac0_phy_aux_clk */
	0x207,		/* gcc_emac0_ptp_clk */
	0x208,		/* gcc_emac0_rgmii_clk */
	0x205,		/* gcc_emac0_slv_ahb_clk */
	0x20D,		/* gcc_emac1_axi_clk */
	0x20F,		/* gcc_emac1_phy_aux_clk */
	0x210,		/* gcc_emac1_ptp_clk */
	0x211,		/* gcc_emac1_rgmii_clk */
	0x20E,		/* gcc_emac1_slv_ahb_clk */
	0x183,		/* gcc_gp1_clk */
	0x184,		/* gcc_gp2_clk */
	0x185,		/* gcc_gp3_clk */
	0x234,		/* gcc_gp4_clk */
	0x235,		/* gcc_gp5_clk */
	0x1FB,		/* gcc_gpu_gpll0_clk_src */
	0x1FC,		/* gcc_gpu_gpll0_div_clk_src */
	0x1F7,		/* gcc_gpu_memnoc_gfx_clk */
	0x1FA,		/* gcc_gpu_snoc_dvm_gfx_clk */
	0x1F4,		/* gcc_gpu_tcu_throttle_ahb_clk */
	0x1F8,		/* gcc_gpu_tcu_throttle_clk */
	0x198,		/* gcc_pcie_0_aux_clk */
	0x196,		/* gcc_pcie_0_cfg_ahb_clk */
	0x195,		/* gcc_pcie_0_mstr_axi_clk */
	0x197,		/* gcc_pcie_0_phy_aux_clk */
	0x19B,		/* gcc_pcie_0_phy_rchng_clk */
	0x199,		/* gcc_pcie_0_pipe_clk */
	0x19A,		/* gcc_pcie_0_pipediv2_clk */
	0x194,		/* gcc_pcie_0_slv_axi_clk */
	0x193,		/* gcc_pcie_0_slv_q2a_axi_clk */
	0x18B,		/* gcc_pcie_1_aux_clk */
	0x189,		/* gcc_pcie_1_cfg_ahb_clk */
	0x188,		/* gcc_pcie_1_mstr_axi_clk */
	0x18A,		/* gcc_pcie_1_phy_aux_clk */
	0x18E,		/* gcc_pcie_1_phy_rchng_clk */
	0x18C,		/* gcc_pcie_1_pipe_clk */
	0x18D,		/* gcc_pcie_1_pipediv2_clk */
	0x187,		/* gcc_pcie_1_slv_axi_clk */
	0x186,		/* gcc_pcie_1_slv_q2a_axi_clk */
	0x5D,		/* gcc_pcie_throttle_cfg_clk */
	0x10C,		/* gcc_pdm2_clk */
	0x10A,		/* gcc_pdm_ahb_clk */
	0x10B,		/* gcc_pdm_xo4_clk */
	0x84,		/* gcc_qmip_camera_nrt_ahb_clk */
	0x85,		/* gcc_qmip_camera_rt_ahb_clk */
	0x94,		/* gcc_qmip_disp1_ahb_clk */
	0x95,		/* gcc_qmip_disp1_rot_ahb_clk */
	0x8E,		/* gcc_qmip_disp_ahb_clk */
	0x8F,		/* gcc_qmip_disp_rot_ahb_clk */
	0x9C,		/* gcc_qmip_video_cvp_ahb_clk */
	0x9D,		/* gcc_qmip_video_vcodec_ahb_clk */
	0x9E,		/* gcc_qmip_video_vcpu_ahb_clk */
	0xF7,		/* gcc_qupv3_wrap0_core_2x_clk */
	0xF6,		/* gcc_qupv3_wrap0_core_clk */
	0xF8,		/* gcc_qupv3_wrap0_s0_clk */
	0xF9,		/* gcc_qupv3_wrap0_s1_clk */
	0xFA,		/* gcc_qupv3_wrap0_s2_clk */
	0xFB,		/* gcc_qupv3_wrap0_s3_clk */
	0xFC,		/* gcc_qupv3_wrap0_s4_clk */
	0xFD,		/* gcc_qupv3_wrap0_s5_clk */
	0xFE,		/* gcc_qupv3_wrap0_s6_clk */
	0x102,		/* gcc_qupv3_wrap1_core_2x_clk */
	0x101,		/* gcc_qupv3_wrap1_core_clk */
	0x103,		/* gcc_qupv3_wrap1_s0_clk */
	0x104,		/* gcc_qupv3_wrap1_s1_clk */
	0x105,		/* gcc_qupv3_wrap1_s2_clk */
	0x106,		/* gcc_qupv3_wrap1_s3_clk */
	0x107,		/* gcc_qupv3_wrap1_s4_clk */
	0x108,		/* gcc_qupv3_wrap1_s5_clk */
	0x109,		/* gcc_qupv3_wrap1_s6_clk */
	0x219,		/* gcc_qupv3_wrap2_core_2x_clk */
	0x218,		/* gcc_qupv3_wrap2_core_clk */
	0x21A,		/* gcc_qupv3_wrap2_s0_clk */
	0x21B,		/* gcc_qupv3_wrap2_s1_clk */
	0x21C,		/* gcc_qupv3_wrap2_s2_clk */
	0x21D,		/* gcc_qupv3_wrap2_s3_clk */
	0x21E,		/* gcc_qupv3_wrap2_s4_clk */
	0x21F,		/* gcc_qupv3_wrap2_s5_clk */
	0x220,		/* gcc_qupv3_wrap2_s6_clk */
	0x225,		/* gcc_qupv3_wrap3_core_2x_clk */
	0x223,		/* gcc_qupv3_wrap3_core_clk */
	0x227,		/* gcc_qupv3_wrap3_qspi_clk */
	0x226,		/* gcc_qupv3_wrap3_s0_clk */
	0xF4,		/* gcc_qupv3_wrap_0_m_ahb_clk */
	0xF5,		/* gcc_qupv3_wrap_0_s_ahb_clk */
	0xFF,		/* gcc_qupv3_wrap_1_m_ahb_clk */
	0x100,		/* gcc_qupv3_wrap_1_s_ahb_clk */
	0x216,		/* gcc_qupv3_wrap_2_m_ahb_clk */
	0x217,		/* gcc_qupv3_wrap_2_s_ahb_clk */
	0x221,		/* gcc_qupv3_wrap_3_m_ahb_clk */
	0x222,		/* gcc_qupv3_wrap_3_s_ahb_clk */
	0xF1,		/* gcc_sdcc1_ahb_clk */
	0xF0,		/* gcc_sdcc1_apps_clk */
	0xF3,		/* gcc_sdcc1_ice_core_clk */
	0x1F1,		/* gcc_tscss_ahb_clk */
	0x1F0,		/* gcc_tscss_etu_clk */
	0x1EF,		/* gcc_tscss_global_cntr_clk */
	0x19F,		/* gcc_ufs_card_ahb_clk */
	0x19E,		/* gcc_ufs_card_axi_clk */
	0x1A5,		/* gcc_ufs_card_ice_core_clk */
	0x1A6,		/* gcc_ufs_card_phy_aux_clk */
	0x1A1,		/* gcc_ufs_card_rx_symbol_0_clk */
	0x1A7,		/* gcc_ufs_card_rx_symbol_1_clk */
	0x1A0,		/* gcc_ufs_card_tx_symbol_0_clk */
	0x1A4,		/* gcc_ufs_card_unipro_core_clk */
	0x1AB,		/* gcc_ufs_phy_ahb_clk */
	0x1AA,		/* gcc_ufs_phy_axi_clk */
	0x1B1,		/* gcc_ufs_phy_ice_core_clk */
	0x1B2,		/* gcc_ufs_phy_phy_aux_clk */
	0x1AD,		/* gcc_ufs_phy_rx_symbol_0_clk */
	0x1B3,		/* gcc_ufs_phy_rx_symbol_1_clk */
	0x1AC,		/* gcc_ufs_phy_tx_symbol_0_clk */
	0x1B0,		/* gcc_ufs_phy_unipro_core_clk */
	0xE3,		/* gcc_usb20_master_clk */
	0xE5,		/* gcc_usb20_mock_utmi_clk */
	0xE4,		/* gcc_usb20_sleep_clk */
	0xD4,		/* gcc_usb30_prim_master_clk */
	0xD6,		/* gcc_usb30_prim_mock_utmi_clk */
	0xD5,		/* gcc_usb30_prim_sleep_clk */
	0xDD,		/* gcc_usb30_sec_master_clk */
	0xDF,		/* gcc_usb30_sec_mock_utmi_clk */
	0xDE,		/* gcc_usb30_sec_sleep_clk */
	0xD7,		/* gcc_usb3_prim_phy_aux_clk */
	0xD8,		/* gcc_usb3_prim_phy_com_aux_clk */
	0xD9,		/* gcc_usb3_prim_phy_pipe_clk */
	0xE0,		/* gcc_usb3_sec_phy_aux_clk */
	0xE1,		/* gcc_usb3_sec_phy_com_aux_clk */
	0xE2,		/* gcc_usb3_sec_phy_pipe_clk */
	0x9F,		/* gcc_video_axi0_clk */
	0xA0,		/* gcc_video_axi1_clk */
	0x1F6,		/* gpu_cc_debug_mux */
	0x2E,		/* measure_only_cnoc_clk */
	0x83,		/* measure_only_gcc_camera_ahb_clk */
	0x8A,		/* measure_only_gcc_camera_xo_clk */
	0x93,		/* measure_only_gcc_disp1_ahb_clk */
	0x97,		/* measure_only_gcc_disp1_xo_clk */
	0x8D,		/* measure_only_gcc_disp_ahb_clk */
	0x91,		/* measure_only_gcc_disp_xo_clk */
	0x1F3,		/* measure_only_gcc_gpu_cfg_ahb_clk */
	0x9B,		/* measure_only_gcc_video_ahb_clk */
	0xA1,		/* measure_only_gcc_video_xo_clk */
	0x1EB,		/* measure_only_ipa_2x_clk */
	0x145,		/* measure_only_memnoc_clk */
	0x19,		/* measure_only_snoc_clk */
	0x149,		/* mc_cc_debug_mux or ddrss_gcc_debug_clk */
	0xA2,		/* video_cc_debug_mux */
};

static struct clk_debug_mux gcc_debug_mux = {
	.priv = &debug_mux_priv,
	.debug_offset = 0x72024,
	.post_div_offset = 0x6E000,
	.cbcr_offset = 0x6E004,
	.src_sel_mask = 0x1FFF,
	.src_sel_shift = 0,
	.post_div_mask = 0xF,
	.post_div_shift = 0,
	.post_div_val = 2,
	.mux_sels = gcc_debug_mux_sels,
	.hw.init = &(const struct clk_init_data){
		.name = "gcc_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = gcc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(gcc_debug_mux_parent_names),
	},
};

static const char *const gpu_cc_debug_mux_parent_names[] = {
	"gpu_cc_ahb_clk",
	"gpu_cc_crc_ahb_clk",
	"gpu_cc_cx_ff_clk",
	"gpu_cc_cx_gmu_clk",
	"gpu_cc_cx_snoc_dvm_clk",
	"gpu_cc_cxo_aon_clk",
	"gpu_cc_cxo_clk",
	"gpu_cc_demet_clk",
	"gpu_cc_hub_aon_clk",
	"gpu_cc_hub_cx_int_clk",
	"gpu_cc_memnoc_gfx_clk",
	"gpu_cc_sleep_clk",
	"measure_only_gcc_gpu_cfg_ahb_clk",
	"measure_only_gpu_cc_cb_clk",
};

static int gpu_cc_debug_mux_sels[] = {
	0x16,		/* gpu_cc_ahb_clk */
	0x17,		/* gpu_cc_crc_ahb_clk */
	0x21,		/* gpu_cc_cx_ff_clk */
	0x1E,		/* gpu_cc_cx_gmu_clk */
	0x1B,		/* gpu_cc_cx_snoc_dvm_clk */
	0xB,		/* gpu_cc_cxo_aon_clk */
	0x1F,		/* gpu_cc_cxo_clk */
	0xD,		/* gpu_cc_demet_clk */
	0x2F,		/* gpu_cc_hub_aon_clk */
	0x20,		/* gpu_cc_hub_cx_int_clk */
	0x22,		/* gpu_cc_memnoc_gfx_clk */
	0x1C,		/* gpu_cc_sleep_clk */
	0x1,		/* measure_only_gcc_gpu_cfg_ahb_clk */
	0x2E,		/* measure_only_gpu_cc_cb_clk */
};

static struct clk_debug_mux gpu_cc_debug_mux = {
	.priv = &debug_mux_priv,
	.debug_offset = 0x9564,
	.post_div_offset = 0x9270,
	.cbcr_offset = 0x9274,
	.src_sel_mask = 0xFF,
	.src_sel_shift = 0,
	.post_div_mask = 0xF,
	.post_div_shift = 0,
	.post_div_val = 2,
	.mux_sels = gpu_cc_debug_mux_sels,
	.hw.init = &(const struct clk_init_data){
		.name = "gpu_cc_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = gpu_cc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(gpu_cc_debug_mux_parent_names),
	},
};

static const char *const video_cc_debug_mux_parent_names[] = {
	"measure_only_video_cc_ahb_clk",
	"measure_only_video_cc_xo_clk",
	"video_cc_mvs0_clk",
	"video_cc_mvs0c_clk",
	"video_cc_mvs1_clk",
	"video_cc_mvs1c_clk",
	"video_cc_sleep_clk",
};

static int video_cc_debug_mux_sels[] = {
	0x7,		/* measure_only_video_cc_ahb_clk */
	0xB,		/* measure_only_video_cc_xo_clk */
	0x3,		/* video_cc_mvs0_clk */
	0x1,		/* video_cc_mvs0c_clk */
	0x5,		/* video_cc_mvs1_clk */
	0x9,		/* video_cc_mvs1c_clk */
	0xC,		/* video_cc_sleep_clk */
};

static struct clk_debug_mux video_cc_debug_mux = {
	.priv = &debug_mux_priv,
	.debug_offset = 0x9A4C,
	.post_div_offset = 0x80F0,
	.cbcr_offset = 0x80F4,
	.src_sel_mask = 0x3F,
	.src_sel_shift = 0,
	.post_div_mask = 0xF,
	.post_div_shift = 0,
	.post_div_val = 3,
	.mux_sels = video_cc_debug_mux_sels,
	.num_mux_sels = ARRAY_SIZE(video_cc_debug_mux_sels),
	.hw.init = &(const struct clk_init_data){
		.name = "video_cc_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = video_cc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(video_cc_debug_mux_parent_names),
	},
};

static const char *const mc_cc_debug_mux_parent_names[] = {
	"measure_only_mccc_clk",
};

static struct clk_debug_mux mc_cc_debug_mux = {
	.period_offset = 0x50,
	.hw.init = &(struct clk_init_data){
		.name = "mc_cc_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = mc_cc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(mc_cc_debug_mux_parent_names),
	},
};

static struct mux_regmap_names mux_list[] = {
	{ .mux = &mc_cc_debug_mux, .regmap_name = "qcom,mccc" },
	{ .mux = &video_cc_debug_mux, .regmap_name = "qcom,videocc" },
	{ .mux = &gpu_cc_debug_mux, .regmap_name = "qcom,gpucc" },
	{ .mux = &disp_cc_0_debug_mux, .regmap_name = "qcom,dispcc0" },
	{ .mux = &disp_cc_1_debug_mux, .regmap_name = "qcom,dispcc1" },
	{ .mux = &cam_cc_debug_mux, .regmap_name = "qcom,camcc" },
	{ .mux = &apss_cc_debug_mux, .regmap_name = "qcom,apsscc" },
	{ .mux = &gcc_debug_mux, .regmap_name = "qcom,gcc" },
};

static struct clk_dummy measure_only_apcs_cl1_l3_post_acd_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_apcs_cl1_l3_post_acd_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_apcs_cl0_gold_post_acd_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_apcs_cl0_gold_post_acd_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_apcs_cl1_goldplus_post_acd_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_apcs_cl1_goldplus_post_acd_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_apcs_cl0_l3_post_acd_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_apcs_cl0_l3_post_acd_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_cam_cc_gdsc_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_cam_cc_gdsc_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_cnoc_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_cnoc_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gcc_camera_ahb_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_gcc_camera_ahb_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gcc_camera_xo_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_gcc_camera_xo_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gcc_disp1_ahb_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_gcc_disp1_ahb_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gcc_disp1_xo_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_gcc_disp1_xo_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gcc_disp_ahb_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_gcc_disp_ahb_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gcc_disp_xo_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_gcc_disp_xo_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gcc_gpu_cfg_ahb_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_gcc_gpu_cfg_ahb_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gcc_video_ahb_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_gcc_video_ahb_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gcc_video_xo_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_gcc_video_xo_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gpu_cc_cb_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_gpu_cc_cb_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_ipa_2x_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_ipa_2x_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_mdss_0_disp_cc_xo_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_mdss_0_disp_cc_xo_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_mdss_1_disp_cc_xo_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_mdss_1_disp_cc_xo_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_memnoc_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_memnoc_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_mccc_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_mccc_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_snoc_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_snoc_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_video_cc_ahb_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_video_cc_ahb_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_video_cc_xo_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_video_cc_xo_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_hw *debugcc_lemans_hws[] = {
	&measure_only_apcs_cl1_l3_post_acd_clk.hw,
	&measure_only_apcs_cl0_gold_post_acd_clk.hw,
	&measure_only_apcs_cl1_goldplus_post_acd_clk.hw,
	&measure_only_apcs_cl0_l3_post_acd_clk.hw,
	&measure_only_cam_cc_gdsc_clk.hw,
	&measure_only_cnoc_clk.hw,
	&measure_only_gcc_camera_ahb_clk.hw,
	&measure_only_gcc_camera_xo_clk.hw,
	&measure_only_gcc_disp1_ahb_clk.hw,
	&measure_only_gcc_disp1_xo_clk.hw,
	&measure_only_gcc_disp_ahb_clk.hw,
	&measure_only_gcc_disp_xo_clk.hw,
	&measure_only_gcc_gpu_cfg_ahb_clk.hw,
	&measure_only_gcc_video_ahb_clk.hw,
	&measure_only_gcc_video_xo_clk.hw,
	&measure_only_gpu_cc_cb_clk.hw,
	&measure_only_ipa_2x_clk.hw,
	&measure_only_mdss_0_disp_cc_xo_clk.hw,
	&measure_only_mdss_1_disp_cc_xo_clk.hw,
	&measure_only_memnoc_clk.hw,
	&measure_only_mccc_clk.hw,
	&measure_only_snoc_clk.hw,
	&measure_only_video_cc_ahb_clk.hw,
	&measure_only_video_cc_xo_clk.hw,
};

static const struct of_device_id clk_debug_match_table[] = {
	{ .compatible = "qcom,lemans-debugcc" },
	{ }
};

static int clk_debug_lemans_probe(struct platform_device *pdev)
{
	struct clk *clk;
	int ret, i;

	BUILD_BUG_ON(ARRAY_SIZE(apss_cc_debug_mux_parent_names) !=
		ARRAY_SIZE(apss_cc_debug_mux_sels));
	BUILD_BUG_ON(ARRAY_SIZE(cam_cc_debug_mux_parent_names) !=
		ARRAY_SIZE(cam_cc_debug_mux_sels));
	BUILD_BUG_ON(ARRAY_SIZE(disp_cc_0_debug_mux_parent_names) !=
		ARRAY_SIZE(disp_cc_0_debug_mux_sels));
	BUILD_BUG_ON(ARRAY_SIZE(disp_cc_1_debug_mux_parent_names) !=
		ARRAY_SIZE(disp_cc_1_debug_mux_sels));
	BUILD_BUG_ON(ARRAY_SIZE(gcc_debug_mux_parent_names) != ARRAY_SIZE(gcc_debug_mux_sels));
	BUILD_BUG_ON(ARRAY_SIZE(gpu_cc_debug_mux_parent_names) !=
		ARRAY_SIZE(gpu_cc_debug_mux_sels));
	BUILD_BUG_ON(ARRAY_SIZE(video_cc_debug_mux_parent_names) !=
		ARRAY_SIZE(video_cc_debug_mux_sels));

	clk = devm_clk_get(&pdev->dev, "xo_clk_src");
	if (IS_ERR(clk)) {
		if (PTR_ERR(clk) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Unable to get xo clock\n");
		return PTR_ERR(clk);
	}

	debug_mux_priv.cxo = clk;

	for (i = 0; i < ARRAY_SIZE(mux_list); i++) {
		if (IS_ERR_OR_NULL(mux_list[i].mux->regmap)) {
			ret = map_debug_bases(pdev, mux_list[i].regmap_name,
					      mux_list[i].mux);
			if (ret == -EBADR)
				continue;
			else if (ret)
				return ret;
		}
	}

	for (i = 0; i < ARRAY_SIZE(debugcc_lemans_hws); i++) {
		clk = devm_clk_register(&pdev->dev, debugcc_lemans_hws[i]);
		if (IS_ERR(clk)) {
			dev_err(&pdev->dev, "Unable to register %s, err:(%d)\n",
				clk_hw_get_name(debugcc_lemans_hws[i]),
				PTR_ERR(clk));
			return PTR_ERR(clk);
		}
	}

	for (i = 0; i < ARRAY_SIZE(mux_list); i++) {
		if (!mux_list[i].mux->regmap)
			continue;

		ret = devm_clk_register_debug_mux(&pdev->dev, mux_list[i].mux);
		if (ret) {
			dev_err(&pdev->dev, "Unable to register mux clk %s, err:(%d)\n",
				qcom_clk_hw_get_name(&mux_list[i].mux->hw),
				ret);
			return ret;
		}
	}

	ret = clk_debug_measure_register(&gcc_debug_mux.hw);
	if (ret) {
		dev_err(&pdev->dev, "Could not register Measure clocks\n");
		return ret;
	}

	dev_info(&pdev->dev, "Registered debug measure clocks\n");

	return ret;
}

static struct platform_driver clk_debug_driver = {
	.probe = clk_debug_lemans_probe,
	.driver = {
		.name = "lemans-debugcc",
		.of_match_table = clk_debug_match_table,
	},
};

static int __init clk_debug_lemans_init(void)
{
	return platform_driver_register(&clk_debug_driver);
}
fs_initcall(clk_debug_lemans_init);

MODULE_DESCRIPTION("QTI DEBUG CC LEMANS Driver");
MODULE_LICENSE("GPL");
