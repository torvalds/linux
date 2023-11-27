// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
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
	.ctl_reg = 0x62038,
	.status_reg = 0x6203C,
	.xo_div4_cbcr = 0x28008,
};

static const char *const apss_cc_debug_mux_parent_names[] = {
	"measure_only_apcs_gold_post_acd_clk",
	"measure_only_apcs_l3_post_acd_clk",
	"measure_only_apcs_silver_post_acd_clk",
};

static int apss_cc_debug_mux_sels[] = {
	0x25,		/* measure_only_apcs_gold_post_acd_clk */
	0x41,		/* measure_only_apcs_l3_post_acd_clk */
	0x21,		/* measure_only_apcs_silver_post_acd_clk */
};

static int apss_cc_debug_mux_pre_divs[] = {
	0x8,		/* measure_only_apcs_gold_post_acd_clk */
	0x4,		/* measure_only_apcs_l3_post_acd_clk */
	0x4,		/* measure_only_apcs_silver_post_acd_clk */
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
	.num_mux_sels = ARRAY_SIZE(apss_cc_debug_mux_sels),
	.pre_div_vals = apss_cc_debug_mux_pre_divs,
	.hw.init = &(const struct clk_init_data){
		.name = "apss_cc_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = apss_cc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(apss_cc_debug_mux_parent_names),
	},
};

static const char *const disp_cc_debug_mux_parent_names[] = {
	"disp_cc_mdss_accu_clk",
	"disp_cc_mdss_ahb1_clk",
	"disp_cc_mdss_ahb_clk",
	"disp_cc_mdss_byte0_clk",
	"disp_cc_mdss_byte0_intf_clk",
	"disp_cc_mdss_esc0_clk",
	"disp_cc_mdss_mdp1_clk",
	"disp_cc_mdss_mdp_clk",
	"disp_cc_mdss_mdp_lut1_clk",
	"disp_cc_mdss_mdp_lut_clk",
	"disp_cc_mdss_non_gdsc_ahb_clk",
	"disp_cc_mdss_pclk0_clk",
	"disp_cc_mdss_rot1_clk",
	"disp_cc_mdss_rot_clk",
	"disp_cc_mdss_rscc_ahb_clk",
	"disp_cc_mdss_rscc_vsync_clk",
	"disp_cc_mdss_vsync1_clk",
	"disp_cc_mdss_vsync_clk",
	"disp_cc_sleep_clk",
	"measure_only_disp_cc_xo_clk",
};

static int disp_cc_debug_mux_sels[] = {
	0x46,		/* disp_cc_mdss_accu_clk */
	0x39,		/* disp_cc_mdss_ahb1_clk */
	0x34,		/* disp_cc_mdss_ahb_clk */
	0x14,		/* disp_cc_mdss_byte0_clk */
	0x15,		/* disp_cc_mdss_byte0_intf_clk */
	0x16,		/* disp_cc_mdss_esc0_clk */
	0x35,		/* disp_cc_mdss_mdp1_clk */
	0x10,		/* disp_cc_mdss_mdp_clk */
	0x37,		/* disp_cc_mdss_mdp_lut1_clk */
	0x12,		/* disp_cc_mdss_mdp_lut_clk */
	0x3A,		/* disp_cc_mdss_non_gdsc_ahb_clk */
	0xF,		/* disp_cc_mdss_pclk0_clk */
	0x36,		/* disp_cc_mdss_rot1_clk */
	0x11,		/* disp_cc_mdss_rot_clk */
	0x3C,		/* disp_cc_mdss_rscc_ahb_clk */
	0x3B,		/* disp_cc_mdss_rscc_vsync_clk */
	0x38,		/* disp_cc_mdss_vsync1_clk */
	0x13,		/* disp_cc_mdss_vsync_clk */
	0x4A,		/* disp_cc_sleep_clk */
	0x49,		/* measure_only_disp_cc_xo_clk */
};

static struct clk_debug_mux disp_cc_debug_mux = {
	.priv = &debug_mux_priv,
	.debug_offset = 0x11000,
	.post_div_offset = 0xD000,
	.cbcr_offset = 0xD004,
	.src_sel_mask = 0x1FF,
	.src_sel_shift = 0,
	.post_div_mask = 0xF,
	.post_div_shift = 0,
	.post_div_val = 4,
	.mux_sels = disp_cc_debug_mux_sels,
	.num_mux_sels = ARRAY_SIZE(disp_cc_debug_mux_sels),
	.hw.init = &(const struct clk_init_data){
		.name = "disp_cc_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = disp_cc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(disp_cc_debug_mux_parent_names),
	},
};

static const char *const gcc_debug_mux_parent_names[] = {
	"apss_cc_debug_mux",
	"disp_cc_debug_mux",
	"gcc_ahb2phy_csi_clk",
	"gcc_ahb2phy_usb_clk",
	"gcc_bimc_gpu_axi_clk",
	"gcc_boot_rom_ahb_clk",
	"gcc_cam_throttle_nrt_clk",
	"gcc_cam_throttle_rt_clk",
	"gcc_camss_axi_clk",
	"gcc_camss_cci_0_clk",
	"gcc_camss_cci_1_clk",
	"gcc_camss_cphy_0_clk",
	"gcc_camss_cphy_1_clk",
	"gcc_camss_cphy_2_clk",
	"gcc_camss_csi0phytimer_clk",
	"gcc_camss_csi1phytimer_clk",
	"gcc_camss_csi2phytimer_clk",
	"gcc_camss_mclk0_clk",
	"gcc_camss_mclk1_clk",
	"gcc_camss_mclk2_clk",
	"gcc_camss_mclk3_clk",
	"gcc_camss_nrt_axi_clk",
	"gcc_camss_ope_ahb_clk",
	"gcc_camss_ope_clk",
	"gcc_camss_rt_axi_clk",
	"gcc_camss_tfe_0_clk",
	"gcc_camss_tfe_0_cphy_rx_clk",
	"gcc_camss_tfe_0_csid_clk",
	"gcc_camss_tfe_1_clk",
	"gcc_camss_tfe_1_cphy_rx_clk",
	"gcc_camss_tfe_1_csid_clk",
	"gcc_camss_top_ahb_clk",
	"gcc_camss_top_shift_clk",
	"gcc_cfg_noc_usb3_prim_axi_clk",
	"gcc_disp_gpll0_div_clk_src",
	"gcc_disp_hf_axi_clk",
	"gcc_disp_sleep_clk",
	"gcc_disp_throttle_core_clk",
	"gcc_gp1_clk",
	"gcc_gp2_clk",
	"gcc_gp3_clk",
	"gcc_gpu_gpll0_clk_src",
	"gcc_gpu_gpll0_div_clk_src",
	"gcc_gpu_memnoc_gfx_clk",
	"gcc_pdm2_clk",
	"gcc_pdm_ahb_clk",
	"gcc_pdm_xo4_clk",
	"gcc_prng_ahb_clk",
	"gcc_qmip_camera_nrt_ahb_clk",
	"gcc_qmip_camera_rt_ahb_clk",
	"gcc_qmip_disp_ahb_clk",
	"gcc_qmip_gpu_cfg_ahb_clk",
	"gcc_qmip_video_vcodec_ahb_clk",
	"gcc_qupv3_wrap0_core_2x_clk",
	"gcc_qupv3_wrap0_core_clk",
	"gcc_qupv3_wrap0_s0_clk",
	"gcc_qupv3_wrap0_s1_clk",
	"gcc_qupv3_wrap0_s2_clk",
	"gcc_qupv3_wrap0_s3_clk",
	"gcc_qupv3_wrap0_s4_clk",
	"gcc_qupv3_wrap1_core_2x_clk",
	"gcc_qupv3_wrap1_core_clk",
	"gcc_qupv3_wrap1_s0_clk",
	"gcc_qupv3_wrap1_s1_clk",
	"gcc_qupv3_wrap1_s2_clk",
	"gcc_qupv3_wrap1_s3_clk",
	"gcc_qupv3_wrap1_s4_clk",
	"gcc_qupv3_wrap_0_m_ahb_clk",
	"gcc_qupv3_wrap_0_s_ahb_clk",
	"gcc_qupv3_wrap_1_m_ahb_clk",
	"gcc_qupv3_wrap_1_s_ahb_clk",
	"gcc_sdcc1_ahb_clk",
	"gcc_sdcc1_apps_clk",
	"gcc_sdcc1_ice_core_clk",
	"gcc_sdcc2_ahb_clk",
	"gcc_sdcc2_apps_clk",
	"gcc_sys_noc_ufs_phy_axi_clk",
	"gcc_sys_noc_usb3_prim_axi_clk",
	"gcc_ufs_phy_ahb_clk",
	"gcc_ufs_phy_axi_clk",
	"gcc_ufs_phy_ice_core_clk",
	"gcc_ufs_phy_phy_aux_clk",
	"gcc_ufs_phy_rx_symbol_0_clk",
	"gcc_ufs_phy_rx_symbol_1_clk",
	"gcc_ufs_phy_tx_symbol_0_clk",
	"gcc_ufs_phy_unipro_core_clk",
	"gcc_usb30_prim_atb_clk",
	"gcc_usb30_prim_master_clk",
	"gcc_usb30_prim_mock_utmi_clk",
	"gcc_usb30_prim_sleep_clk",
	"gcc_usb3_prim_phy_com_aux_clk",
	"gcc_usb3_prim_phy_pipe_clk",
	"gcc_vcodec0_axi_clk",
	"gcc_venus_ahb_clk",
	"gcc_venus_ctl_axi_clk",
	"gcc_video_axi0_clk",
	"gcc_video_throttle_core_clk",
	"gcc_video_vcodec0_sys_clk",
	"gcc_video_venus_ctl_clk",
	"gpu_cc_debug_mux",
	"mc_cc_debug_mux",
	"measure_only_cnoc_clk",
	"measure_only_gcc_camera_ahb_clk",
	"measure_only_gcc_camera_xo_clk",
	"measure_only_gcc_cpuss_gnoc_clk",
	"measure_only_gcc_disp_ahb_clk",
	"measure_only_gcc_disp_xo_clk",
	"measure_only_gcc_gpu_cfg_ahb_clk",
	"measure_only_gcc_sys_noc_cpuss_ahb_clk",
	"measure_only_gcc_video_ahb_clk",
	"measure_only_gcc_video_xo_clk",
	"measure_only_ipa_2x_clk",
	"measure_only_snoc_clk",
	"measure_only_ufs_phy_rx_symbol_0_clk",
	"measure_only_ufs_phy_rx_symbol_1_clk",
	"measure_only_ufs_phy_tx_symbol_0_clk",
	"measure_only_usb3_phy_wrapper_gcc_usb30_pipe_clk",
};

static int gcc_debug_mux_sels[] = {
	0xC8,		/* apss_cc_debug_mux */
	0x4C,		/* disp_cc_debug_mux */
	0x77,		/* gcc_ahb2phy_csi_clk */
	0x78,		/* gcc_ahb2phy_usb_clk */
	0xAF,		/* gcc_bimc_gpu_axi_clk */
	0x94,		/* gcc_boot_rom_ahb_clk */
	0x56,		/* gcc_cam_throttle_nrt_clk */
	0x55,		/* gcc_cam_throttle_rt_clk */
	0x153,		/* gcc_camss_axi_clk */
	0x150,		/* gcc_camss_cci_0_clk */
	0x151,		/* gcc_camss_cci_1_clk */
	0x143,		/* gcc_camss_cphy_0_clk */
	0x144,		/* gcc_camss_cphy_1_clk */
	0x145,		/* gcc_camss_cphy_2_clk */
	0x136,		/* gcc_camss_csi0phytimer_clk */
	0x137,		/* gcc_camss_csi1phytimer_clk */
	0x138,		/* gcc_camss_csi2phytimer_clk */
	0x139,		/* gcc_camss_mclk0_clk */
	0x13A,		/* gcc_camss_mclk1_clk */
	0x13B,		/* gcc_camss_mclk2_clk */
	0x13C,		/* gcc_camss_mclk3_clk */
	0x159,		/* gcc_camss_nrt_axi_clk */
	0x14F,		/* gcc_camss_ope_ahb_clk */
	0x14D,		/* gcc_camss_ope_clk */
	0x15B,		/* gcc_camss_rt_axi_clk */
	0x13D,		/* gcc_camss_tfe_0_clk */
	0x141,		/* gcc_camss_tfe_0_cphy_rx_clk */
	0x146,		/* gcc_camss_tfe_0_csid_clk */
	0x13F,		/* gcc_camss_tfe_1_clk */
	0x142,		/* gcc_camss_tfe_1_cphy_rx_clk */
	0x148,		/* gcc_camss_tfe_1_csid_clk */
	0x152,		/* gcc_camss_top_ahb_clk */
	0x157,		/* gcc_camss_top_shift_clk */
	0x20,		/* gcc_cfg_noc_usb3_prim_axi_clk */
	0x51,		/* gcc_disp_gpll0_div_clk_src */
	0x47,		/* gcc_disp_hf_axi_clk */
	0x57,		/* gcc_disp_sleep_clk */
	0x53,		/* gcc_disp_throttle_core_clk */
	0xD2,		/* gcc_gp1_clk */
	0xD3,		/* gcc_gp2_clk */
	0xD4,		/* gcc_gp3_clk */
	0x106,		/* gcc_gpu_gpll0_clk_src */
	0x107,		/* gcc_gpu_gpll0_div_clk_src */
	0x104,		/* gcc_gpu_memnoc_gfx_clk */
	0x90,		/* gcc_pdm2_clk */
	0x8E,		/* gcc_pdm_ahb_clk */
	0x8F,		/* gcc_pdm_xo4_clk */
	0x91,		/* gcc_prng_ahb_clk */
	0x44,		/* gcc_qmip_camera_nrt_ahb_clk */
	0x52,		/* gcc_qmip_camera_rt_ahb_clk */
	0x45,		/* gcc_qmip_disp_ahb_clk */
	0x108,		/* gcc_qmip_gpu_cfg_ahb_clk */
	0x43,		/* gcc_qmip_video_vcodec_ahb_clk */
	0x7F,		/* gcc_qupv3_wrap0_core_2x_clk */
	0x7E,		/* gcc_qupv3_wrap0_core_clk */
	0x80,		/* gcc_qupv3_wrap0_s0_clk */
	0x81,		/* gcc_qupv3_wrap0_s1_clk */
	0x82,		/* gcc_qupv3_wrap0_s2_clk */
	0x83,		/* gcc_qupv3_wrap0_s3_clk */
	0x84,		/* gcc_qupv3_wrap0_s4_clk */
	0x88,		/* gcc_qupv3_wrap1_core_2x_clk */
	0x87,		/* gcc_qupv3_wrap1_core_clk */
	0x89,		/* gcc_qupv3_wrap1_s0_clk */
	0x8A,		/* gcc_qupv3_wrap1_s1_clk */
	0x8B,		/* gcc_qupv3_wrap1_s2_clk */
	0x8C,		/* gcc_qupv3_wrap1_s3_clk */
	0x8D,		/* gcc_qupv3_wrap1_s4_clk */
	0x7C,		/* gcc_qupv3_wrap_0_m_ahb_clk */
	0x7D,		/* gcc_qupv3_wrap_0_s_ahb_clk */
	0x85,		/* gcc_qupv3_wrap_1_m_ahb_clk */
	0x86,		/* gcc_qupv3_wrap_1_s_ahb_clk */
	0x10D,		/* gcc_sdcc1_ahb_clk */
	0x10C,		/* gcc_sdcc1_apps_clk */
	0x10E,		/* gcc_sdcc1_ice_core_clk */
	0x7A,		/* gcc_sdcc2_ahb_clk */
	0x79,		/* gcc_sdcc2_apps_clk */
	0x18,		/* gcc_sys_noc_ufs_phy_axi_clk */
	0x17,		/* gcc_sys_noc_usb3_prim_axi_clk */
	0x12B,		/* gcc_ufs_phy_ahb_clk */
	0x12A,		/* gcc_ufs_phy_axi_clk */
	0x131,		/* gcc_ufs_phy_ice_core_clk */
	0x132,		/* gcc_ufs_phy_phy_aux_clk */
	0x12D,		/* gcc_ufs_phy_rx_symbol_0_clk */
	0x135,		/* gcc_ufs_phy_rx_symbol_1_clk */
	0x12C,		/* gcc_ufs_phy_tx_symbol_0_clk */
	0x130,		/* gcc_ufs_phy_unipro_core_clk */
	0x74,		/* gcc_usb30_prim_atb_clk */
	0x6C,		/* gcc_usb30_prim_master_clk */
	0x6E,		/* gcc_usb30_prim_mock_utmi_clk */
	0x6D,		/* gcc_usb30_prim_sleep_clk */
	0x6F,		/* gcc_usb3_prim_phy_com_aux_clk */
	0x70,		/* gcc_usb3_prim_phy_pipe_clk */
	0x161,		/* gcc_vcodec0_axi_clk */
	0x162,		/* gcc_venus_ahb_clk */
	0x160,		/* gcc_venus_ctl_axi_clk */
	0x46,		/* gcc_video_axi0_clk */
	0x54,		/* gcc_video_throttle_core_clk */
	0x15E,		/* gcc_video_vcodec0_sys_clk */
	0x15C,		/* gcc_video_venus_ctl_clk */
	0x103,		/* gpu_cc_debug_mux */
	0xC1,		/* mc_cc_debug_mux */
	0x1E,		/* measure_only_cnoc_clk */
	0x41,		/* measure_only_gcc_camera_ahb_clk */
	0x49,		/* measure_only_gcc_camera_xo_clk */
	0xC3,		/* measure_only_gcc_cpuss_gnoc_clk */
	0x42,		/* measure_only_gcc_disp_ahb_clk */
	0x4A,		/* measure_only_gcc_disp_xo_clk */
	0x101,		/* measure_only_gcc_gpu_cfg_ahb_clk */
	0x9,		/* measure_only_gcc_sys_noc_cpuss_ahb_clk */
	0x40,		/* measure_only_gcc_video_ahb_clk */
	0x48,		/* measure_only_gcc_video_xo_clk */
	0xDE,		/* measure_only_ipa_2x_clk */
	0x7,		/* measure_only_snoc_clk */
	0x12F,		/* measure_only_ufs_phy_rx_symbol_0_clk */
	0x134,		/* measure_only_ufs_phy_rx_symbol_1_clk */
	0x12E,		/* measure_only_ufs_phy_tx_symbol_0_clk */
	0x75,		/* measure_only_usb3_phy_wrapper_gcc_usb30_pipe_clk */
};

static struct clk_debug_mux gcc_debug_mux = {
	.priv = &debug_mux_priv,
	.debug_offset = 0x62000,
	.post_div_offset = 0x30000,
	.cbcr_offset = 0x30004,
	.src_sel_mask = 0x3FF,
	.src_sel_shift = 0,
	.post_div_mask = 0xF,
	.post_div_shift = 0,
	.post_div_val = 1,
	.mux_sels = gcc_debug_mux_sels,
	.num_mux_sels = ARRAY_SIZE(gcc_debug_mux_sels),
	.hw.init = &(const struct clk_init_data){
		.name = "gcc_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = gcc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(gcc_debug_mux_parent_names),
	},
};

static const char *const gpu_cc_debug_mux_parent_names[] = {
	"gpu_cc_crc_ahb_clk",
	"gpu_cc_cx_accu_shift_clk",
	"gpu_cc_cx_gfx3d_clk",
	"gpu_cc_cx_gmu_clk",
	"gpu_cc_cxo_clk",
	"gpu_cc_gx_accu_shift_clk",
	"gpu_cc_gx_gfx3d_clk",
	"gpu_cc_memnoc_gfx_clk",
	"measure_only_gpu_cc_ahb_clk",
	"measure_only_gpu_cc_cxo_aon_clk",
	"measure_only_gpu_cc_demet_clk",
	"measure_only_gpu_cc_gx_cxo_clk",
	"measure_only_gpu_cc_sleep_clk",
};

static int gpu_cc_debug_mux_sels[] = {
	0x11,		/* gpu_cc_crc_ahb_clk */
	0x1F,		/* gpu_cc_cx_accu_shift_clk */
	0x1A,		/* gpu_cc_cx_gfx3d_clk */
	0x18,		/* gpu_cc_cx_gmu_clk */
	0x19,		/* gpu_cc_cxo_clk */
	0x1D,		/* gpu_cc_gx_accu_shift_clk */
	0xB,		/* gpu_cc_gx_gfx3d_clk */
	0xF,		/* gpu_cc_memnoc_gfx_clk */
	0x10,		/* measure_only_gpu_cc_ahb_clk */
	0xA,		/* measure_only_gpu_cc_cxo_aon_clk */
	0x9,		/* measure_only_gpu_cc_demet_clk */
	0xE,		/* measure_only_gpu_cc_gx_cxo_clk */
	0x16,		/* measure_only_gpu_cc_sleep_clk */
};

static struct clk_debug_mux gpu_cc_debug_mux = {
	.priv = &debug_mux_priv,
	.debug_offset = 0x91DC,
	.post_div_offset = 0x913C,
	.cbcr_offset = 0x9140,
	.src_sel_mask = 0xFF,
	.src_sel_shift = 0,
	.post_div_mask = 0xF,
	.post_div_shift = 0,
	.post_div_val = 2,
	.mux_sels = gpu_cc_debug_mux_sels,
	.num_mux_sels = ARRAY_SIZE(gpu_cc_debug_mux_sels),
	.hw.init = &(const struct clk_init_data){
		.name = "gpu_cc_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = gpu_cc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(gpu_cc_debug_mux_parent_names),
	},
};

static const char *const mc_cc_debug_mux_parent_names[] = {
	"measure_only_mccc_clk",
};

static struct clk_debug_mux mc_cc_debug_mux = {
	.period_offset = 0x20,
	.hw.init = &(struct clk_init_data){
		.name = "mc_cc_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = mc_cc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(mc_cc_debug_mux_parent_names),
	},
};

static struct mux_regmap_names mux_list[] = {
	{ .mux = &mc_cc_debug_mux, .regmap_name = "qcom,mccc" },
	{ .mux = &gpu_cc_debug_mux, .regmap_name = "qcom,gpucc" },
	{ .mux = &disp_cc_debug_mux, .regmap_name = "qcom,dispcc" },
	{ .mux = &apss_cc_debug_mux, .regmap_name = "qcom,apsscc" },
	{ .mux = &gcc_debug_mux, .regmap_name = "qcom,gcc" },
};

static struct clk_dummy measure_only_apcs_gold_post_acd_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_apcs_gold_post_acd_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_apcs_l3_post_acd_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_apcs_l3_post_acd_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_apcs_silver_post_acd_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_apcs_silver_post_acd_clk",
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

static struct clk_dummy measure_only_disp_cc_xo_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_disp_cc_xo_clk",
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

static struct clk_dummy measure_only_gcc_cpuss_gnoc_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_gcc_cpuss_gnoc_clk",
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

static struct clk_dummy measure_only_gcc_sys_noc_cpuss_ahb_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_gcc_sys_noc_cpuss_ahb_clk",
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

static struct clk_dummy measure_only_gpu_cc_ahb_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_gpu_cc_ahb_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gpu_cc_cxo_aon_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_gpu_cc_cxo_aon_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gpu_cc_demet_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_gpu_cc_demet_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gpu_cc_gx_cxo_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_gpu_cc_gx_cxo_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gpu_cc_sleep_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_gpu_cc_sleep_clk",
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

static struct clk_dummy measure_only_ufs_phy_rx_symbol_0_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_ufs_phy_rx_symbol_0_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_ufs_phy_rx_symbol_1_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_ufs_phy_rx_symbol_1_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_ufs_phy_tx_symbol_0_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_ufs_phy_tx_symbol_0_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_usb3_phy_wrapper_gcc_usb30_pipe_clk = {
	.rrate = 1000,
	.hw.init = &(const struct clk_init_data){
		.name = "measure_only_usb3_phy_wrapper_gcc_usb30_pipe_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_hw *debugcc_pitti_hws[] = {
	&measure_only_apcs_gold_post_acd_clk.hw,
	&measure_only_apcs_l3_post_acd_clk.hw,
	&measure_only_apcs_silver_post_acd_clk.hw,
	&measure_only_cnoc_clk.hw,
	&measure_only_disp_cc_xo_clk.hw,
	&measure_only_gcc_camera_ahb_clk.hw,
	&measure_only_gcc_camera_xo_clk.hw,
	&measure_only_gcc_cpuss_gnoc_clk.hw,
	&measure_only_gcc_disp_ahb_clk.hw,
	&measure_only_gcc_disp_xo_clk.hw,
	&measure_only_gcc_gpu_cfg_ahb_clk.hw,
	&measure_only_gcc_sys_noc_cpuss_ahb_clk.hw,
	&measure_only_gcc_video_ahb_clk.hw,
	&measure_only_gcc_video_xo_clk.hw,
	&measure_only_gpu_cc_ahb_clk.hw,
	&measure_only_gpu_cc_cxo_aon_clk.hw,
	&measure_only_gpu_cc_demet_clk.hw,
	&measure_only_gpu_cc_gx_cxo_clk.hw,
	&measure_only_gpu_cc_sleep_clk.hw,
	&measure_only_ipa_2x_clk.hw,
	&measure_only_mccc_clk.hw,
	&measure_only_snoc_clk.hw,
	&measure_only_ufs_phy_rx_symbol_0_clk.hw,
	&measure_only_ufs_phy_rx_symbol_1_clk.hw,
	&measure_only_ufs_phy_tx_symbol_0_clk.hw,
	&measure_only_usb3_phy_wrapper_gcc_usb30_pipe_clk.hw,
};

static const struct of_device_id clk_debug_match_table[] = {
	{ .compatible = "qcom,pitti-debugcc" },
	{ }
};

static int clk_debug_pitti_probe(struct platform_device *pdev)
{
	struct clk *clk;
	int ret = 0, i;

	BUILD_BUG_ON(ARRAY_SIZE(apss_cc_debug_mux_parent_names) !=
		ARRAY_SIZE(apss_cc_debug_mux_sels));
	BUILD_BUG_ON(ARRAY_SIZE(disp_cc_debug_mux_parent_names) !=
		ARRAY_SIZE(disp_cc_debug_mux_sels));
	BUILD_BUG_ON(ARRAY_SIZE(gcc_debug_mux_parent_names) != ARRAY_SIZE(gcc_debug_mux_sels));
	BUILD_BUG_ON(ARRAY_SIZE(gpu_cc_debug_mux_parent_names) !=
		ARRAY_SIZE(gpu_cc_debug_mux_sels));

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

	for (i = 0; i < ARRAY_SIZE(debugcc_pitti_hws); i++) {
		clk = devm_clk_register(&pdev->dev, debugcc_pitti_hws[i]);
		if (IS_ERR(clk)) {
			dev_err(&pdev->dev, "Unable to register %s, err:(%ld)\n",
				qcom_clk_hw_get_name(debugcc_pitti_hws[i]),
				PTR_ERR(clk));
			return PTR_ERR(clk);
		}
	}

	for (i = 0; i < ARRAY_SIZE(mux_list); i++) {
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
	.probe = clk_debug_pitti_probe,
	.driver = {
		.name = "pitti-debugcc",
		.of_match_table = clk_debug_match_table,
	},
};

static int __init clk_debug_pitti_init(void)
{
	return platform_driver_register(&clk_debug_driver);
}
fs_initcall(clk_debug_pitti_init);

MODULE_DESCRIPTION("QTI DEBUG CC PITTI Driver");
MODULE_LICENSE("GPL");
