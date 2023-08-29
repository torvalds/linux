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
	.xo_div4_cbcr = 0x43008,
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
	.hw.init = &(struct clk_init_data){
		.name = "apss_cc_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = apss_cc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(apss_cc_debug_mux_parent_names),
	},
};

static const char *const cam_cc_debug_mux_parent_names[] = {
	"cam_cc_bps_ahb_clk",
	"cam_cc_bps_areg_clk",
	"cam_cc_bps_axi_clk",
	"cam_cc_bps_clk",
	"cam_cc_camnoc_axi_clk",
	"cam_cc_camnoc_dcd_xo_clk",
	"cam_cc_cci_0_clk",
	"cam_cc_cci_1_clk",
	"cam_cc_cci_2_clk",
	"cam_cc_cci_3_clk",
	"cam_cc_core_ahb_clk",
	"cam_cc_cpas_ahb_clk",
	"cam_cc_csi0phytimer_clk",
	"cam_cc_csi1phytimer_clk",
	"cam_cc_csi2phytimer_clk",
	"cam_cc_csi3phytimer_clk",
	"cam_cc_csiphy0_clk",
	"cam_cc_csiphy1_clk",
	"cam_cc_csiphy2_clk",
	"cam_cc_csiphy3_clk",
	"cam_cc_fd_core_clk",
	"cam_cc_fd_core_uar_clk",
	"cam_cc_icp_ahb_clk",
	"cam_cc_icp_clk",
	"cam_cc_ife_0_axi_clk",
	"cam_cc_ife_0_clk",
	"cam_cc_ife_0_cphy_rx_clk",
	"cam_cc_ife_0_csid_clk",
	"cam_cc_ife_0_dsp_clk",
	"cam_cc_ife_1_axi_clk",
	"cam_cc_ife_1_clk",
	"cam_cc_ife_1_cphy_rx_clk",
	"cam_cc_ife_1_csid_clk",
	"cam_cc_ife_1_dsp_clk",
	"cam_cc_ife_2_axi_clk",
	"cam_cc_ife_2_clk",
	"cam_cc_ife_2_cphy_rx_clk",
	"cam_cc_ife_2_csid_clk",
	"cam_cc_ife_2_dsp_clk",
	"cam_cc_ife_3_axi_clk",
	"cam_cc_ife_3_clk",
	"cam_cc_ife_3_cphy_rx_clk",
	"cam_cc_ife_3_csid_clk",
	"cam_cc_ife_3_dsp_clk",
	"cam_cc_ife_lite_0_clk",
	"cam_cc_ife_lite_0_cphy_rx_clk",
	"cam_cc_ife_lite_0_csid_clk",
	"cam_cc_ife_lite_1_clk",
	"cam_cc_ife_lite_1_cphy_rx_clk",
	"cam_cc_ife_lite_1_csid_clk",
	"cam_cc_ife_lite_2_clk",
	"cam_cc_ife_lite_2_cphy_rx_clk",
	"cam_cc_ife_lite_2_csid_clk",
	"cam_cc_ife_lite_3_clk",
	"cam_cc_ife_lite_3_cphy_rx_clk",
	"cam_cc_ife_lite_3_csid_clk",
	"cam_cc_ipe_0_ahb_clk",
	"cam_cc_ipe_0_areg_clk",
	"cam_cc_ipe_0_axi_clk",
	"cam_cc_ipe_0_clk",
	"cam_cc_ipe_1_ahb_clk",
	"cam_cc_ipe_1_areg_clk",
	"cam_cc_ipe_1_axi_clk",
	"cam_cc_ipe_1_clk",
	"cam_cc_jpeg_clk",
	"cam_cc_lrme_clk",
	"cam_cc_mclk0_clk",
	"cam_cc_mclk1_clk",
	"cam_cc_mclk2_clk",
	"cam_cc_mclk3_clk",
	"cam_cc_mclk4_clk",
	"cam_cc_mclk5_clk",
	"cam_cc_mclk6_clk",
	"cam_cc_mclk7_clk",
	"cam_cc_sleep_clk",
};

static int cam_cc_debug_mux_sels[] = {
	0xE,		/* cam_cc_bps_ahb_clk */
	0xD,		/* cam_cc_bps_areg_clk */
	0xC,		/* cam_cc_bps_axi_clk */
	0xB,		/* cam_cc_bps_clk */
	0x27,		/* cam_cc_camnoc_axi_clk */
	0x33,		/* cam_cc_camnoc_dcd_xo_clk */
	0x2A,		/* cam_cc_cci_0_clk */
	0x3B,		/* cam_cc_cci_1_clk */
	0x5B,		/* cam_cc_cci_2_clk */
	0x5C,		/* cam_cc_cci_3_clk */
	0x2E,		/* cam_cc_core_ahb_clk */
	0x2C,		/* cam_cc_cpas_ahb_clk */
	0x5,		/* cam_cc_csi0phytimer_clk */
	0x7,		/* cam_cc_csi1phytimer_clk */
	0x9,		/* cam_cc_csi2phytimer_clk */
	0x35,		/* cam_cc_csi3phytimer_clk */
	0x6,		/* cam_cc_csiphy0_clk */
	0x8,		/* cam_cc_csiphy1_clk */
	0xA,		/* cam_cc_csiphy2_clk */
	0x36,		/* cam_cc_csiphy3_clk */
	0x28,		/* cam_cc_fd_core_clk */
	0x29,		/* cam_cc_fd_core_uar_clk */
	0x37,		/* cam_cc_icp_ahb_clk */
	0x26,		/* cam_cc_icp_clk */
	0x1B,		/* cam_cc_ife_0_axi_clk */
	0x17,		/* cam_cc_ife_0_clk */
	0x1A,		/* cam_cc_ife_0_cphy_rx_clk */
	0x19,		/* cam_cc_ife_0_csid_clk */
	0x18,		/* cam_cc_ife_0_dsp_clk */
	0x21,		/* cam_cc_ife_1_axi_clk */
	0x1D,		/* cam_cc_ife_1_clk */
	0x20,		/* cam_cc_ife_1_cphy_rx_clk */
	0x1F,		/* cam_cc_ife_1_csid_clk */
	0x1E,		/* cam_cc_ife_1_dsp_clk */
	0x4A,		/* cam_cc_ife_2_axi_clk */
	0x44,		/* cam_cc_ife_2_clk */
	0x49,		/* cam_cc_ife_2_cphy_rx_clk */
	0x47,		/* cam_cc_ife_2_csid_clk */
	0x46,		/* cam_cc_ife_2_dsp_clk */
	0x51,		/* cam_cc_ife_3_axi_clk */
	0x4B,		/* cam_cc_ife_3_clk */
	0x50,		/* cam_cc_ife_3_cphy_rx_clk */
	0x4E,		/* cam_cc_ife_3_csid_clk */
	0x4D,		/* cam_cc_ife_3_dsp_clk */
	0x22,		/* cam_cc_ife_lite_0_clk */
	0x24,		/* cam_cc_ife_lite_0_cphy_rx_clk */
	0x23,		/* cam_cc_ife_lite_0_csid_clk */
	0x38,		/* cam_cc_ife_lite_1_clk */
	0x3A,		/* cam_cc_ife_lite_1_cphy_rx_clk */
	0x39,		/* cam_cc_ife_lite_1_csid_clk */
	0x55,		/* cam_cc_ife_lite_2_clk */
	0x57,		/* cam_cc_ife_lite_2_cphy_rx_clk */
	0x56,		/* cam_cc_ife_lite_2_csid_clk */
	0x58,		/* cam_cc_ife_lite_3_clk */
	0x5A,		/* cam_cc_ife_lite_3_cphy_rx_clk */
	0x59,		/* cam_cc_ife_lite_3_csid_clk */
	0x12,		/* cam_cc_ipe_0_ahb_clk */
	0x11,		/* cam_cc_ipe_0_areg_clk */
	0x10,		/* cam_cc_ipe_0_axi_clk */
	0xF,		/* cam_cc_ipe_0_clk */
	0x16,		/* cam_cc_ipe_1_ahb_clk */
	0x15,		/* cam_cc_ipe_1_areg_clk */
	0x14,		/* cam_cc_ipe_1_axi_clk */
	0x13,		/* cam_cc_ipe_1_clk */
	0x25,		/* cam_cc_jpeg_clk */
	0x2B,		/* cam_cc_lrme_clk */
	0x1,		/* cam_cc_mclk0_clk */
	0x2,		/* cam_cc_mclk1_clk */
	0x3,		/* cam_cc_mclk2_clk */
	0x4,		/* cam_cc_mclk3_clk */
	0x5D,		/* cam_cc_mclk4_clk */
	0x5E,		/* cam_cc_mclk5_clk */
	0x5F,		/* cam_cc_mclk6_clk */
	0x60,		/* cam_cc_mclk7_clk */
	0x3F,		/* cam_cc_sleep_clk */
};

static struct clk_debug_mux cam_cc_debug_mux = {
	.priv = &debug_mux_priv,
	.debug_offset = 0xD000,
	.post_div_offset = 0xD004,
	.cbcr_offset = 0xD008,
	.src_sel_mask = 0xFF,
	.src_sel_shift = 0,
	.post_div_mask = 0xF,
	.post_div_shift = 0,
	.post_div_val = 4,
	.mux_sels = cam_cc_debug_mux_sels,
	.num_mux_sels = ARRAY_SIZE(cam_cc_debug_mux_sels),
	.hw.init = &(struct clk_init_data){
		.name = "cam_cc_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = cam_cc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(cam_cc_debug_mux_parent_names),
	},
};

static const char *const disp_cc_debug_mux_parent_names[] = {
	"disp_cc_mdss_ahb_clk",
	"disp_cc_mdss_byte0_clk",
	"disp_cc_mdss_byte0_intf_clk",
	"disp_cc_mdss_byte1_clk",
	"disp_cc_mdss_byte1_intf_clk",
	"disp_cc_mdss_dp_aux1_clk",
	"disp_cc_mdss_dp_aux_clk",
	"disp_cc_mdss_dp_link1_clk",
	"disp_cc_mdss_dp_link1_intf_clk",
	"disp_cc_mdss_dp_link_clk",
	"disp_cc_mdss_dp_link_intf_clk",
	"disp_cc_mdss_dp_pixel1_clk",
	"disp_cc_mdss_dp_pixel2_clk",
	"disp_cc_mdss_dp_pixel_clk",
	"disp_cc_mdss_edp_aux_clk",
	"disp_cc_mdss_edp_gtc_clk",
	"disp_cc_mdss_edp_link_clk",
	"disp_cc_mdss_edp_link_intf_clk",
	"disp_cc_mdss_edp_pixel_clk",
	"disp_cc_mdss_esc0_clk",
	"disp_cc_mdss_esc1_clk",
	"disp_cc_mdss_mdp_clk",
	"disp_cc_mdss_mdp_lut_clk",
	"disp_cc_mdss_non_gdsc_ahb_clk",
	"disp_cc_mdss_pclk0_clk",
	"disp_cc_mdss_pclk1_clk",
	"disp_cc_mdss_rot_clk",
	"disp_cc_mdss_rscc_ahb_clk",
	"disp_cc_mdss_rscc_vsync_clk",
	"disp_cc_mdss_vsync_clk",
};

static int disp_cc_debug_mux_sels[] = {
	0x2B,		/* disp_cc_mdss_ahb_clk */
	0x15,		/* disp_cc_mdss_byte0_clk */
	0x16,		/* disp_cc_mdss_byte0_intf_clk */
	0x17,		/* disp_cc_mdss_byte1_clk */
	0x18,		/* disp_cc_mdss_byte1_intf_clk */
	0x25,		/* disp_cc_mdss_dp_aux1_clk */
	0x20,		/* disp_cc_mdss_dp_aux_clk */
	0x22,		/* disp_cc_mdss_dp_link1_clk */
	0x23,		/* disp_cc_mdss_dp_link1_intf_clk */
	0x1B,		/* disp_cc_mdss_dp_link_clk */
	0x1C,		/* disp_cc_mdss_dp_link_intf_clk */
	0x1F,		/* disp_cc_mdss_dp_pixel1_clk */
	0x21,		/* disp_cc_mdss_dp_pixel2_clk */
	0x1E,		/* disp_cc_mdss_dp_pixel_clk */
	0x29,		/* disp_cc_mdss_edp_aux_clk */
	0x2A,		/* disp_cc_mdss_edp_gtc_clk */
	0x27,		/* disp_cc_mdss_edp_link_clk */
	0x28,		/* disp_cc_mdss_edp_link_intf_clk */
	0x26,		/* disp_cc_mdss_edp_pixel_clk */
	0x19,		/* disp_cc_mdss_esc0_clk */
	0x1A,		/* disp_cc_mdss_esc1_clk */
	0x11,		/* disp_cc_mdss_mdp_clk */
	0x13,		/* disp_cc_mdss_mdp_lut_clk */
	0x2C,		/* disp_cc_mdss_non_gdsc_ahb_clk */
	0xF,		/* disp_cc_mdss_pclk0_clk */
	0x10,		/* disp_cc_mdss_pclk1_clk */
	0x12,		/* disp_cc_mdss_rot_clk */
	0x2E,		/* disp_cc_mdss_rscc_ahb_clk */
	0x2D,		/* disp_cc_mdss_rscc_vsync_clk */
	0x14,		/* disp_cc_mdss_vsync_clk */
};

static struct clk_debug_mux disp_cc_debug_mux = {
	.priv = &debug_mux_priv,
	.debug_offset = 0x7000,
	.post_div_offset = 0x5008,
	.cbcr_offset = 0x500C,
	.src_sel_mask = 0xFF,
	.src_sel_shift = 0,
	.post_div_mask = 0x3,
	.post_div_shift = 0,
	.post_div_val = 4,
	.mux_sels = disp_cc_debug_mux_sels,
	.num_mux_sels = ARRAY_SIZE(disp_cc_debug_mux_sels),
	.hw.init = &(struct clk_init_data){
		.name = "disp_cc_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = disp_cc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(disp_cc_debug_mux_parent_names),
	},
};

static const char *const gcc_debug_mux_parent_names[] = {
	"apss_cc_debug_mux",
	"cam_cc_debug_mux",
	"disp_cc_debug_mux",
	"gcc_aggre_noc_pcie_tbu_clk",
	"gcc_aggre_ufs_card_2_axi_clk",
	"gcc_aggre_ufs_card_axi_clk",
	"gcc_aggre_ufs_phy_axi_clk",
	"gcc_aggre_usb3_mp_axi_clk",
	"gcc_aggre_usb3_prim_axi_clk",
	"gcc_aggre_usb3_sec_axi_clk",
	"gcc_boot_rom_ahb_clk",
	"gcc_camera_hf_axi_clk",
	"gcc_camera_sf_axi_clk",
	"gcc_cfg_noc_usb3_mp_axi_clk",
	"gcc_cfg_noc_usb3_prim_axi_clk",
	"gcc_cfg_noc_usb3_sec_axi_clk",
	"gcc_cpuss_ahb_clk",
	"gcc_cpuss_rbcpr_clk",
	"gcc_ddrss_gpu_axi_clk",
	"gcc_disp_hf_axi_clk",
	"gcc_disp_sf_axi_clk",
	"gcc_emac_axi_clk",
	"gcc_emac_ptp_clk",
	"gcc_emac_rgmii_clk",
	"gcc_emac_slv_ahb_clk",
	"gcc_gp1_clk",
	"gcc_gp2_clk",
	"gcc_gp3_clk",
	"gcc_gp4_clk",
	"gcc_gp5_clk",
	"gcc_gpu_gpll0_clk_src",
	"gcc_gpu_gpll0_div_clk_src",
	"gcc_gpu_memnoc_gfx_clk",
	"gcc_gpu_snoc_dvm_gfx_clk",
	"gcc_npu_axi_clk",
	"gcc_npu_gpll0_clk_src",
	"gcc_npu_gpll0_div_clk_src",
	"gcc_pcie0_phy_refgen_clk",
	"gcc_pcie1_phy_refgen_clk",
	"gcc_pcie2_phy_refgen_clk",
	"gcc_pcie3_phy_refgen_clk",
	"gcc_pcie_0_aux_clk",
	"gcc_pcie_0_cfg_ahb_clk",
	"gcc_pcie_0_mstr_axi_clk",
	"gcc_pcie_0_pipe_clk",
	"gcc_pcie_0_slv_axi_clk",
	"gcc_pcie_0_slv_q2a_axi_clk",
	"gcc_pcie_1_aux_clk",
	"gcc_pcie_1_cfg_ahb_clk",
	"gcc_pcie_1_mstr_axi_clk",
	"gcc_pcie_1_pipe_clk",
	"gcc_pcie_1_slv_axi_clk",
	"gcc_pcie_1_slv_q2a_axi_clk",
	"gcc_pcie_2_aux_clk",
	"gcc_pcie_2_cfg_ahb_clk",
	"gcc_pcie_2_mstr_axi_clk",
	"gcc_pcie_2_pipe_clk",
	"gcc_pcie_2_slv_axi_clk",
	"gcc_pcie_2_slv_q2a_axi_clk",
	"gcc_pcie_3_aux_clk",
	"gcc_pcie_3_cfg_ahb_clk",
	"gcc_pcie_3_mstr_axi_clk",
	"gcc_pcie_3_pipe_clk",
	"gcc_pcie_3_slv_axi_clk",
	"gcc_pcie_3_slv_q2a_axi_clk",
	"gcc_pcie_phy_aux_clk",
	"gcc_pdm2_clk",
	"gcc_pdm_ahb_clk",
	"gcc_pdm_xo4_clk",
	"gcc_prng_ahb_clk",
	"gcc_qmip_camera_nrt_ahb_clk",
	"gcc_qmip_camera_rt_ahb_clk",
	"gcc_qmip_disp_ahb_clk",
	"gcc_qmip_video_cvp_ahb_clk",
	"gcc_qmip_video_vcodec_ahb_clk",
	"gcc_qspi_1_cnoc_periph_ahb_clk",
	"gcc_qspi_1_core_clk",
	"gcc_qspi_cnoc_periph_ahb_clk",
	"gcc_qspi_core_clk",
	"gcc_qupv3_wrap0_s0_clk",
	"gcc_qupv3_wrap0_s1_clk",
	"gcc_qupv3_wrap0_s2_clk",
	"gcc_qupv3_wrap0_s3_clk",
	"gcc_qupv3_wrap0_s4_clk",
	"gcc_qupv3_wrap0_s5_clk",
	"gcc_qupv3_wrap0_s6_clk",
	"gcc_qupv3_wrap0_s7_clk",
	"gcc_qupv3_wrap1_s0_clk",
	"gcc_qupv3_wrap1_s1_clk",
	"gcc_qupv3_wrap1_s2_clk",
	"gcc_qupv3_wrap1_s3_clk",
	"gcc_qupv3_wrap1_s4_clk",
	"gcc_qupv3_wrap1_s5_clk",
	"gcc_qupv3_wrap2_s0_clk",
	"gcc_qupv3_wrap2_s1_clk",
	"gcc_qupv3_wrap2_s2_clk",
	"gcc_qupv3_wrap2_s3_clk",
	"gcc_qupv3_wrap2_s4_clk",
	"gcc_qupv3_wrap2_s5_clk",
	"gcc_qupv3_wrap_0_m_ahb_clk",
	"gcc_qupv3_wrap_0_s_ahb_clk",
	"gcc_qupv3_wrap_1_m_ahb_clk",
	"gcc_qupv3_wrap_1_s_ahb_clk",
	"gcc_qupv3_wrap_2_m_ahb_clk",
	"gcc_qupv3_wrap_2_s_ahb_clk",
	"gcc_sdcc2_ahb_clk",
	"gcc_sdcc2_apps_clk",
	"gcc_sdcc4_ahb_clk",
	"gcc_sdcc4_apps_clk",
	"gcc_sys_noc_cpuss_ahb_clk",
	"gcc_tsif_ahb_clk",
	"gcc_tsif_inactivity_timers_clk",
	"gcc_tsif_ref_clk",
	"gcc_ufs_card_2_ahb_clk",
	"gcc_ufs_card_2_axi_clk",
	"gcc_ufs_card_2_ice_core_clk",
	"gcc_ufs_card_2_phy_aux_clk",
	"gcc_ufs_card_2_rx_symbol_0_clk",
	"gcc_ufs_card_2_rx_symbol_1_clk",
	"gcc_ufs_card_2_tx_symbol_0_clk",
	"gcc_ufs_card_2_unipro_core_clk",
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
	"gcc_usb30_mp_master_clk",
	"gcc_usb30_mp_mock_utmi_clk",
	"gcc_usb30_mp_sleep_clk",
	"gcc_usb30_prim_master_clk",
	"gcc_usb30_prim_mock_utmi_clk",
	"gcc_usb30_prim_sleep_clk",
	"gcc_usb30_sec_master_clk",
	"gcc_usb30_sec_mock_utmi_clk",
	"gcc_usb30_sec_sleep_clk",
	"gcc_usb3_mp_phy_aux_clk",
	"gcc_usb3_mp_phy_com_aux_clk",
	"gcc_usb3_mp_phy_pipe_0_clk",
	"gcc_usb3_mp_phy_pipe_1_clk",
	"gcc_usb3_prim_phy_aux_clk",
	"gcc_usb3_prim_phy_com_aux_clk",
	"gcc_usb3_prim_phy_pipe_clk",
	"gcc_usb3_sec_phy_aux_clk",
	"gcc_usb3_sec_phy_com_aux_clk",
	"gcc_usb3_sec_phy_pipe_clk",
	"gcc_video_axi0_clk",
	"gcc_video_axi1_clk",
	"gcc_video_axic_clk",
	"gpu_cc_debug_mux",
	"measure_only_cnoc_clk",
	"measure_only_ipa_2x_clk",
	"measure_only_snoc_clk",
	"npu_cc_debug_mux",
	"video_cc_debug_mux",
	"mc_cc_debug_mux",
};

static int gcc_debug_mux_sels[] = {
	0xD6,		/* apss_cc_debug_mux */
	0x46,		/* cam_cc_debug_mux */
	0x47,		/* disp_cc_debug_mux */
	0x2D,		/* gcc_aggre_noc_pcie_tbu_clk */
	0x1CD,		/* gcc_aggre_ufs_card_2_axi_clk */
	0x11E,		/* gcc_aggre_ufs_card_axi_clk */
	0x11D,		/* gcc_aggre_ufs_phy_axi_clk */
	0x1C8,		/* gcc_aggre_usb3_mp_axi_clk */
	0x11B,		/* gcc_aggre_usb3_prim_axi_clk */
	0x11C,		/* gcc_aggre_usb3_sec_axi_clk */
	0x94,		/* gcc_boot_rom_ahb_clk */
	0x40,		/* gcc_camera_hf_axi_clk */
	0x1A9,		/* gcc_camera_sf_axi_clk */
	0x1C7,		/* gcc_cfg_noc_usb3_mp_axi_clk */
	0x1D,		/* gcc_cfg_noc_usb3_prim_axi_clk */
	0x1E,		/* gcc_cfg_noc_usb3_sec_axi_clk */
	0xCE,		/* gcc_cpuss_ahb_clk */
	0xD0,		/* gcc_cpuss_rbcpr_clk */
	0xBB,		/* gcc_ddrss_gpu_axi_clk */
	0x41,		/* gcc_disp_hf_axi_clk */
	0x161,		/* gcc_disp_sf_axi_clk */
	0x16D,		/* gcc_emac_axi_clk */
	0x1D3,		/* gcc_emac_ptp_clk */
	0x16A,		/* gcc_emac_rgmii_clk */
	0x16C,		/* gcc_emac_slv_ahb_clk */
	0xDE,		/* gcc_gp1_clk */
	0xDF,		/* gcc_gp2_clk */
	0xE0,		/* gcc_gp3_clk */
	0x17F,		/* gcc_gp4_clk */
	0x180,		/* gcc_gp5_clk */
	0x148,		/* gcc_gpu_gpll0_clk_src */
	0x149,		/* gcc_gpu_gpll0_div_clk_src */
	0x145,		/* gcc_gpu_memnoc_gfx_clk */
	0x147,		/* gcc_gpu_snoc_dvm_gfx_clk */
	0x1E9,		/* gcc_npu_axi_clk */
	0x1A1,		/* gcc_npu_gpll0_clk_src */
	0x1A2,		/* gcc_npu_gpll0_div_clk_src */
	0x1F3,		/* gcc_pcie0_phy_refgen_clk */
	0x164,		/* gcc_pcie1_phy_refgen_clk */
	0x163,		/* gcc_pcie2_phy_refgen_clk */
	0x162,		/* gcc_pcie3_phy_refgen_clk */
	0xE5,		/* gcc_pcie_0_aux_clk */
	0xE4,		/* gcc_pcie_0_cfg_ahb_clk */
	0xE3,		/* gcc_pcie_0_mstr_axi_clk */
	0xE6,		/* gcc_pcie_0_pipe_clk */
	0xE2,		/* gcc_pcie_0_slv_axi_clk */
	0xE1,		/* gcc_pcie_0_slv_q2a_axi_clk */
	0xEC,		/* gcc_pcie_1_aux_clk */
	0xEB,		/* gcc_pcie_1_cfg_ahb_clk */
	0xEA,		/* gcc_pcie_1_mstr_axi_clk */
	0xED,		/* gcc_pcie_1_pipe_clk */
	0xE9,		/* gcc_pcie_1_slv_axi_clk */
	0xE8,		/* gcc_pcie_1_slv_q2a_axi_clk */
	0x175,		/* gcc_pcie_2_aux_clk */
	0x174,		/* gcc_pcie_2_cfg_ahb_clk */
	0x173,		/* gcc_pcie_2_mstr_axi_clk */
	0x176,		/* gcc_pcie_2_pipe_clk */
	0x172,		/* gcc_pcie_2_slv_axi_clk */
	0x171,		/* gcc_pcie_2_slv_q2a_axi_clk */
	0x17C,		/* gcc_pcie_3_aux_clk */
	0x17B,		/* gcc_pcie_3_cfg_ahb_clk */
	0x17A,		/* gcc_pcie_3_mstr_axi_clk */
	0x17D,		/* gcc_pcie_3_pipe_clk */
	0x179,		/* gcc_pcie_3_slv_axi_clk */
	0x178,		/* gcc_pcie_3_slv_q2a_axi_clk */
	0xEF,		/* gcc_pcie_phy_aux_clk */
	0x8E,		/* gcc_pdm2_clk */
	0x8C,		/* gcc_pdm_ahb_clk */
	0x8D,		/* gcc_pdm_xo4_clk */
	0x8F,		/* gcc_prng_ahb_clk */
	0x3D,		/* gcc_qmip_camera_nrt_ahb_clk */
	0x160,		/* gcc_qmip_camera_rt_ahb_clk */
	0x3E,		/* gcc_qmip_disp_ahb_clk */
	0x3C,		/* gcc_qmip_video_cvp_ahb_clk */
	0x15C,		/* gcc_qmip_video_vcodec_ahb_clk */
	0x16F,		/* gcc_qspi_1_cnoc_periph_ahb_clk */
	0x170,		/* gcc_qspi_1_core_clk */
	0x168,		/* gcc_qspi_cnoc_periph_ahb_clk */
	0x169,		/* gcc_qspi_core_clk */
	0x78,		/* gcc_qupv3_wrap0_s0_clk */
	0x79,		/* gcc_qupv3_wrap0_s1_clk */
	0x7A,		/* gcc_qupv3_wrap0_s2_clk */
	0x7B,		/* gcc_qupv3_wrap0_s3_clk */
	0x7C,		/* gcc_qupv3_wrap0_s4_clk */
	0x7D,		/* gcc_qupv3_wrap0_s5_clk */
	0x7E,		/* gcc_qupv3_wrap0_s6_clk */
	0x1DA,		/* gcc_qupv3_wrap0_s7_clk */
	0x84,		/* gcc_qupv3_wrap1_s0_clk */
	0x85,		/* gcc_qupv3_wrap1_s1_clk */
	0x86,		/* gcc_qupv3_wrap1_s2_clk */
	0x87,		/* gcc_qupv3_wrap1_s3_clk */
	0x88,		/* gcc_qupv3_wrap1_s4_clk */
	0x89,		/* gcc_qupv3_wrap1_s5_clk */
	0x199,		/* gcc_qupv3_wrap2_s0_clk */
	0x19A,		/* gcc_qupv3_wrap2_s1_clk */
	0x19B,		/* gcc_qupv3_wrap2_s2_clk */
	0x19C,		/* gcc_qupv3_wrap2_s3_clk */
	0x19D,		/* gcc_qupv3_wrap2_s4_clk */
	0x19E,		/* gcc_qupv3_wrap2_s5_clk */
	0x74,		/* gcc_qupv3_wrap_0_m_ahb_clk */
	0x75,		/* gcc_qupv3_wrap_0_s_ahb_clk */
	0x80,		/* gcc_qupv3_wrap_1_m_ahb_clk */
	0x81,		/* gcc_qupv3_wrap_1_s_ahb_clk */
	0x195,		/* gcc_qupv3_wrap_2_m_ahb_clk */
	0x196,		/* gcc_qupv3_wrap_2_s_ahb_clk */
	0x71,		/* gcc_sdcc2_ahb_clk */
	0x70,		/* gcc_sdcc2_apps_clk */
	0x73,		/* gcc_sdcc4_ahb_clk */
	0x72,		/* gcc_sdcc4_apps_clk */
	0xC,		/* gcc_sys_noc_cpuss_ahb_clk */
	0x90,		/* gcc_tsif_ahb_clk */
	0x92,		/* gcc_tsif_inactivity_timers_clk */
	0x91,		/* gcc_tsif_ref_clk */
	0x18B,		/* gcc_ufs_card_2_ahb_clk */
	0x18A,		/* gcc_ufs_card_2_axi_clk */
	0x191,		/* gcc_ufs_card_2_ice_core_clk */
	0x192,		/* gcc_ufs_card_2_phy_aux_clk */
	0x18D,		/* gcc_ufs_card_2_rx_symbol_0_clk */
	0x193,		/* gcc_ufs_card_2_rx_symbol_1_clk */
	0x18C,		/* gcc_ufs_card_2_tx_symbol_0_clk */
	0x190,		/* gcc_ufs_card_2_unipro_core_clk */
	0xF1,		/* gcc_ufs_card_ahb_clk */
	0xF0,		/* gcc_ufs_card_axi_clk */
	0xF7,		/* gcc_ufs_card_ice_core_clk */
	0xF8,		/* gcc_ufs_card_phy_aux_clk */
	0xF3,		/* gcc_ufs_card_rx_symbol_0_clk */
	0xF9,		/* gcc_ufs_card_rx_symbol_1_clk */
	0xF2,		/* gcc_ufs_card_tx_symbol_0_clk */
	0xF6,		/* gcc_ufs_card_unipro_core_clk */
	0xFC,		/* gcc_ufs_phy_ahb_clk */
	0xFB,		/* gcc_ufs_phy_axi_clk */
	0x102,		/* gcc_ufs_phy_ice_core_clk */
	0x103,		/* gcc_ufs_phy_phy_aux_clk */
	0xFE,		/* gcc_ufs_phy_rx_symbol_0_clk */
	0x104,		/* gcc_ufs_phy_rx_symbol_1_clk */
	0xFD,		/* gcc_ufs_phy_tx_symbol_0_clk */
	0x101,		/* gcc_ufs_phy_unipro_core_clk */
	0x1AF,		/* gcc_usb30_mp_master_clk */
	0x1B1,		/* gcc_usb30_mp_mock_utmi_clk */
	0x1B0,		/* gcc_usb30_mp_sleep_clk */
	0x5F,		/* gcc_usb30_prim_master_clk */
	0x61,		/* gcc_usb30_prim_mock_utmi_clk */
	0x60,		/* gcc_usb30_prim_sleep_clk */
	0x65,		/* gcc_usb30_sec_master_clk */
	0x67,		/* gcc_usb30_sec_mock_utmi_clk */
	0x66,		/* gcc_usb30_sec_sleep_clk */
	0x1BD,		/* gcc_usb3_mp_phy_aux_clk */
	0x1BE,		/* gcc_usb3_mp_phy_com_aux_clk */
	0x1BF,		/* gcc_usb3_mp_phy_pipe_0_clk */
	0x1C0,		/* gcc_usb3_mp_phy_pipe_1_clk */
	0x62,		/* gcc_usb3_prim_phy_aux_clk */
	0x63,		/* gcc_usb3_prim_phy_com_aux_clk */
	0x64,		/* gcc_usb3_prim_phy_pipe_clk */
	0x68,		/* gcc_usb3_sec_phy_aux_clk */
	0x69,		/* gcc_usb3_sec_phy_com_aux_clk */
	0x6A,		/* gcc_usb3_sec_phy_pipe_clk */
	0x3F,		/* gcc_video_axi0_clk */
	0x1A7,		/* gcc_video_axi1_clk */
	0x1A8,		/* gcc_video_axic_clk */
	0x144,		/* gpu_cc_debug_mux */
	0x15,		/* measure_only_cnoc_clk */
	0x128,		/* measure_only_ipa_2x_clk */
	0x7,		/* measure_only_snoc_clk */
	0x1C6,		/* npu_cc_debug_mux */
	0x48,		/* video_cc_debug_mux */
	0xC2,		/* mc_cc_debug_mux */
};

static struct clk_debug_mux gcc_debug_mux = {
	.priv = &debug_mux_priv,
	.debug_offset = 0x62000,
	.post_div_offset = 0x62004,
	.cbcr_offset = 0x62008,
	.src_sel_mask = 0x3FF,
	.src_sel_shift = 0,
	.post_div_mask = 0xF,
	.post_div_shift = 0,
	.post_div_val = 2,
	.mux_sels = gcc_debug_mux_sels,
	.num_mux_sels = ARRAY_SIZE(gcc_debug_mux_sels),
	.hw.init = &(struct clk_init_data){
		.name = "gcc_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = gcc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(gcc_debug_mux_parent_names),
	},
};

static const char *const gpu_cc_debug_mux_parent_names[] = {
	"gpu_cc_crc_ahb_clk",
	"gpu_cc_cx_gmu_clk",
	"gpu_cc_cx_snoc_dvm_clk",
	"gpu_cc_cxo_aon_clk",
	"gpu_cc_cxo_clk",
	"gpu_cc_gx_gmu_clk",
	"gpu_cc_sleep_clk",
};

static int gpu_cc_debug_mux_sels[] = {
	0x11,		/* gpu_cc_crc_ahb_clk */
	0x18,		/* gpu_cc_cx_gmu_clk */
	0x15,		/* gpu_cc_cx_snoc_dvm_clk */
	0xA,		/* gpu_cc_cxo_aon_clk */
	0x19,		/* gpu_cc_cxo_clk */
	0xF,		/* gpu_cc_gx_gmu_clk */
	0x16,		/* gpu_cc_sleep_clk */
};

static struct clk_debug_mux gpu_cc_debug_mux = {
	.priv = &debug_mux_priv,
	.debug_offset = 0x1568,
	.post_div_offset = 0x10FC,
	.cbcr_offset = 0x1100,
	.src_sel_mask = 0xFF,
	.src_sel_shift = 0,
	.post_div_mask = 0x3,
	.post_div_shift = 0,
	.post_div_val = 2,
	.mux_sels = gpu_cc_debug_mux_sels,
	.num_mux_sels = ARRAY_SIZE(gpu_cc_debug_mux_sels),
	.hw.init = &(struct clk_init_data){
		.name = "gpu_cc_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = gpu_cc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(gpu_cc_debug_mux_parent_names),
	},
};

static const char *const npu_cc_debug_mux_parent_names[] = {
	"npu_cc_armwic_core_clk",
	"npu_cc_bto_core_clk",
	"npu_cc_bwmon_clk",
	"npu_cc_cal_dp_cdc_clk",
	"npu_cc_cal_dp_clk",
	"npu_cc_comp_noc_axi_clk",
	"npu_cc_conf_noc_ahb_clk",
	"npu_cc_npu_core_clk",
	"npu_cc_npu_core_cti_clk",
	"npu_cc_npu_cpc_clk",
	"npu_cc_npu_cpc_timer_clk",
	"npu_cc_perf_cnt_clk",
	"npu_cc_qtimer_core_clk",
	"npu_cc_sleep_clk",
};

static int npu_cc_debug_mux_sels[] = {
	0x4,		/* npu_cc_armwic_core_clk */
	0x12,		/* npu_cc_bto_core_clk */
	0xF,		/* npu_cc_bwmon_clk */
	0x8,		/* npu_cc_cal_dp_cdc_clk */
	0x1,		/* npu_cc_cal_dp_clk */
	0x9,		/* npu_cc_comp_noc_axi_clk */
	0xA,		/* npu_cc_conf_noc_ahb_clk */
	0x2,		/* npu_cc_npu_core_clk */
	0xC,		/* npu_cc_npu_core_cti_clk */
	0x3,		/* npu_cc_npu_cpc_clk */
	0x5,		/* npu_cc_npu_cpc_timer_clk */
	0x10,		/* npu_cc_perf_cnt_clk */
	0x6,		/* npu_cc_qtimer_core_clk */
	0x7,		/* npu_cc_sleep_clk */
};

static struct clk_debug_mux npu_cc_debug_mux = {
	.priv = &debug_mux_priv,
	.debug_offset = 0x4000,
	.post_div_offset = 0x3004,
	.cbcr_offset = 0x3008,
	.src_sel_mask = 0xFF,
	.src_sel_shift = 0,
	.post_div_mask = 0x3,
	.post_div_shift = 0,
	.post_div_val = 2,
	.mux_sels = npu_cc_debug_mux_sels,
	.num_mux_sels = ARRAY_SIZE(npu_cc_debug_mux_sels),
	.hw.init = &(struct clk_init_data){
		.name = "npu_cc_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = npu_cc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(npu_cc_debug_mux_parent_names),
	},
};

static const char *const video_cc_debug_mux_parent_names[] = {
	"video_cc_iris_ahb_clk",
	"video_cc_mvs0_core_clk",
	"video_cc_mvs1_core_clk",
	"video_cc_mvsc_core_clk",
};

static int video_cc_debug_mux_sels[] = {
	0x7,		/* video_cc_iris_ahb_clk */
	0x3,		/* video_cc_mvs0_core_clk */
	0x5,		/* video_cc_mvs1_core_clk */
	0x1,		/* video_cc_mvsc_core_clk */
};

static struct clk_debug_mux video_cc_debug_mux = {
	.priv = &debug_mux_priv,
	.debug_offset = 0xA4C,
	.post_div_offset = 0x938,
	.cbcr_offset = 0x940,
	.src_sel_mask = 0x3F,
	.src_sel_shift = 0,
	.post_div_mask = 0x7,
	.post_div_shift = 0,
	.post_div_val = 5,
	.mux_sels = video_cc_debug_mux_sels,
	.num_mux_sels = ARRAY_SIZE(video_cc_debug_mux_sels),
	.hw.init = &(struct clk_init_data){
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
	{ .mux = &apss_cc_debug_mux, .regmap_name = "qcom,apsscc" },
	{ .mux = &cam_cc_debug_mux, .regmap_name = "qcom,camcc" },
	{ .mux = &disp_cc_debug_mux, .regmap_name = "qcom,dispcc" },
	{ .mux = &gpu_cc_debug_mux, .regmap_name = "qcom,gpucc" },
	{ .mux = &mc_cc_debug_mux, .regmap_name = "qcom,mccc" },
	{ .mux = &npu_cc_debug_mux, .regmap_name = "qcom,npucc" },
	{ .mux = &video_cc_debug_mux, .regmap_name = "qcom,videocc" },
	{ .mux = &gcc_debug_mux, .regmap_name = "qcom,gcc" },
};

static struct clk_dummy measure_only_apcs_gold_post_acd_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_apcs_gold_post_acd_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_apcs_l3_post_acd_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_apcs_l3_post_acd_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_apcs_silver_post_acd_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_apcs_silver_post_acd_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_cnoc_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_cnoc_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_ipa_2x_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_ipa_2x_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_mccc_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_mccc_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_snoc_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_snoc_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_hw *debugcc_sc8180x_hws[] = {
	&measure_only_apcs_gold_post_acd_clk.hw,
	&measure_only_apcs_l3_post_acd_clk.hw,
	&measure_only_apcs_silver_post_acd_clk.hw,
	&measure_only_cnoc_clk.hw,
	&measure_only_ipa_2x_clk.hw,
	&measure_only_mccc_clk.hw,
	&measure_only_snoc_clk.hw,
};

static const struct of_device_id clk_debug_match_table[] = {
	{ .compatible = "qcom,sc8180x-debugcc" },
	{ }
};

static int clk_debug_sc8180x_probe(struct platform_device *pdev)
{
	struct clk *clk;
	int ret, i;

	BUILD_BUG_ON(ARRAY_SIZE(apss_cc_debug_mux_parent_names) !=
		ARRAY_SIZE(apss_cc_debug_mux_sels));
	BUILD_BUG_ON(ARRAY_SIZE(cam_cc_debug_mux_parent_names) !=
		ARRAY_SIZE(cam_cc_debug_mux_sels));
	BUILD_BUG_ON(ARRAY_SIZE(disp_cc_debug_mux_parent_names) !=
		ARRAY_SIZE(disp_cc_debug_mux_sels));
	BUILD_BUG_ON(ARRAY_SIZE(gcc_debug_mux_parent_names) !=
		ARRAY_SIZE(gcc_debug_mux_sels));
	BUILD_BUG_ON(ARRAY_SIZE(gpu_cc_debug_mux_parent_names) !=
		ARRAY_SIZE(gpu_cc_debug_mux_sels));
	BUILD_BUG_ON(ARRAY_SIZE(npu_cc_debug_mux_parent_names) !=
		ARRAY_SIZE(npu_cc_debug_mux_sels));
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
		ret = map_debug_bases(pdev, mux_list[i].regmap_name,
				      mux_list[i].mux);
		if (ret == -EBADR)
			continue;
		else if (ret)
			return ret;
	}

	for (i = 0; i < ARRAY_SIZE(debugcc_sc8180x_hws); i++) {
		clk = devm_clk_register(&pdev->dev, debugcc_sc8180x_hws[i]);
		if (IS_ERR(clk)) {
			dev_err(&pdev->dev, "Unable to register %s, err:(%d)\n",
				clk_hw_get_name(debugcc_sc8180x_hws[i]),
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
				clk_hw_get_name(&mux_list[i].mux->hw),
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
	.probe = clk_debug_sc8180x_probe,
	.driver = {
		.name = "sc8180x-debugcc",
		.of_match_table = clk_debug_match_table,
	},
};

static int __init clk_debug_sc8180x_init(void)
{
	return platform_driver_register(&clk_debug_driver);
}
fs_initcall(clk_debug_sc8180x_init);

MODULE_DESCRIPTION("QTI DEBUG CC SC8180X Driver");
MODULE_LICENSE("GPL");
