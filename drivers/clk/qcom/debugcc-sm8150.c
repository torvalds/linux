// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
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

static const char *const cpu_cc_debug_mux_parent_names[] = {
	"l3_clk",
	"perfcl_clk",
	"perfpcl_clk",
	"pwrcl_clk",
};

static int cpu_cc_debug_mux_sels[] = {
	0x46,		/* l3_clk */
	0x45,		/* perfcl_clk */
	0x47,		/* perfpcl_clk */
	0x44,		/* pwrcl_clk */
};

static int apss_cc_debug_mux_pre_divs[] = {
	0x10,		/* l3_clk */
	0x10,		/* perfcl_clk */
	0x10,		/* perfpcl_clk */
	0x10,		/* pwrcl_clk */
};

static struct clk_debug_mux cpu_cc_debug_mux = {
	.priv = &debug_mux_priv,
	.debug_offset = 0x18,
	.post_div_offset = 0x18,
	.cbcr_offset = 0x0,
	.src_sel_mask = 0x7F0,
	.src_sel_shift = 4,
	.post_div_mask = 0x7800,
	.post_div_shift = 11,
	.post_div_val = 1,
	.mux_sels = cpu_cc_debug_mux_sels,
	.num_mux_sels = ARRAY_SIZE(cpu_cc_debug_mux_sels),
	.pre_div_vals = apss_cc_debug_mux_pre_divs,
	.hw.init = &(struct clk_init_data){
		.name = "cpu_cc_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = cpu_cc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(cpu_cc_debug_mux_parent_names),
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
	"cam_cc_ife_lite_0_clk",
	"cam_cc_ife_lite_0_cphy_rx_clk",
	"cam_cc_ife_lite_0_csid_clk",
	"cam_cc_ife_lite_1_clk",
	"cam_cc_ife_lite_1_cphy_rx_clk",
	"cam_cc_ife_lite_1_csid_clk",
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
	"measure_only_cam_cc_gdsc_clk",
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
	0x22,		/* cam_cc_ife_lite_0_clk */
	0x24,		/* cam_cc_ife_lite_0_cphy_rx_clk */
	0x23,		/* cam_cc_ife_lite_0_csid_clk */
	0x38,		/* cam_cc_ife_lite_1_clk */
	0x3A,		/* cam_cc_ife_lite_1_cphy_rx_clk */
	0x39,		/* cam_cc_ife_lite_1_csid_clk */
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
	0x3C,		/* measure_only_cam_cc_gdsc_clk */
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
	"disp_cc_mdss_dp_crypto1_clk",
	"disp_cc_mdss_dp_crypto_clk",
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
	"measure_only_disp_cc_xo_clk",
};

static int disp_cc_debug_mux_sels[] = {
	0x2B,		/* disp_cc_mdss_ahb_clk */
	0x15,		/* disp_cc_mdss_byte0_clk */
	0x16,		/* disp_cc_mdss_byte0_intf_clk */
	0x17,		/* disp_cc_mdss_byte1_clk */
	0x18,		/* disp_cc_mdss_byte1_intf_clk */
	0x25,		/* disp_cc_mdss_dp_aux1_clk */
	0x20,		/* disp_cc_mdss_dp_aux_clk */
	0x24,		/* disp_cc_mdss_dp_crypto1_clk */
	0x1D,		/* disp_cc_mdss_dp_crypto_clk */
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
	0x36,		/* measure_only_disp_cc_xo_clk */
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
	"cpu_cc_debug_mux",
	"cam_cc_debug_mux",
	"disp_cc_debug_mux",
	"gcc_aggre_noc_pcie_tbu_clk",
	"gcc_aggre_ufs_card_axi_clk",
	"gcc_aggre_ufs_phy_axi_clk",
	"gcc_aggre_usb3_prim_axi_clk",
	"gcc_aggre_usb3_sec_axi_clk",
	"gcc_boot_rom_ahb_clk",
	"gcc_camera_hf_axi_clk",
	"gcc_camera_sf_axi_clk",
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
	"gcc_gpu_gpll0_clk_src",
	"gcc_gpu_gpll0_div_clk_src",
	"gcc_gpu_memnoc_gfx_clk",
	"gcc_gpu_snoc_dvm_gfx_clk",
	"gcc_npu_at_clk",
	"gcc_npu_axi_clk",
	"gcc_npu_gpll0_clk_src",
	"gcc_npu_gpll0_div_clk_src",
	"gcc_npu_trig_clk",
	"gcc_pcie0_phy_refgen_clk",
	"gcc_pcie1_phy_refgen_clk",
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
	"gcc_video_axic_clk",
	"gpu_cc_debug_mux",
	"measure_only_cdsp_clk",
	"measure_only_cnoc_clk",
	"measure_only_gcc_camera_ahb_clk",
	"measure_only_gcc_camera_xo_clk",
	"measure_only_gcc_cpuss_dvm_bus_clk",
	"measure_only_gcc_cpuss_gnoc_clk",
	"measure_only_gcc_disp_ahb_clk",
	"measure_only_gcc_disp_xo_clk",
	"measure_only_gcc_gpu_cfg_ahb_clk",
	"measure_only_gcc_npu_cfg_ahb_clk",
	"measure_only_gcc_video_ahb_clk",
	"measure_only_gcc_video_xo_clk",
	"measure_only_ipa_2x_clk",
	"measure_only_snoc_clk",
	"npu_cc_debug_mux",
	"video_cc_debug_mux",
	"mc_cc_debug_mux",
};

static int gcc_debug_mux_sels[] = {
	0xE8,		/* cpu_cc_debug_mux */
	0x55,		/* cam_cc_debug_mux */
	0x56,		/* disp_cc_debug_mux */
	0x36,		/* gcc_aggre_noc_pcie_tbu_clk */
	0x141,		/* gcc_aggre_ufs_card_axi_clk */
	0x140,		/* gcc_aggre_ufs_phy_axi_clk */
	0x13E,		/* gcc_aggre_usb3_prim_axi_clk */
	0x13F,		/* gcc_aggre_usb3_sec_axi_clk */
	0xA0,		/* gcc_boot_rom_ahb_clk */
	0x4D,		/* gcc_camera_hf_axi_clk */
	0x4E,		/* gcc_camera_sf_axi_clk */
	0x22,		/* gcc_cfg_noc_usb3_prim_axi_clk */
	0x23,		/* gcc_cfg_noc_usb3_sec_axi_clk */
	0xE0,		/* gcc_cpuss_ahb_clk */
	0xE2,		/* gcc_cpuss_rbcpr_clk */
	0xC0,		/* gcc_ddrss_gpu_axi_clk */
	0x4F,		/* gcc_disp_hf_axi_clk */
	0x50,		/* gcc_disp_sf_axi_clk */
	0x18D,		/* gcc_emac_axi_clk */
	0x190,		/* gcc_emac_ptp_clk */
	0x18F,		/* gcc_emac_rgmii_clk */
	0x18E,		/* gcc_emac_slv_ahb_clk */
	0xF0,		/* gcc_gp1_clk */
	0xF1,		/* gcc_gp2_clk */
	0xF2,		/* gcc_gp3_clk */
	0x166,		/* gcc_gpu_gpll0_clk_src */
	0x167,		/* gcc_gpu_gpll0_div_clk_src */
	0x163,		/* gcc_gpu_memnoc_gfx_clk */
	0x165,		/* gcc_gpu_snoc_dvm_gfx_clk */
	0x17D,		/* gcc_npu_at_clk */
	0x17B,		/* gcc_npu_axi_clk */
	0x17E,		/* gcc_npu_gpll0_clk_src */
	0x17F,		/* gcc_npu_gpll0_div_clk_src */
	0x17C,		/* gcc_npu_trig_clk */
	0x104,		/* gcc_pcie0_phy_refgen_clk */
	0x105,		/* gcc_pcie1_phy_refgen_clk */
	0xF7,		/* gcc_pcie_0_aux_clk */
	0xF6,		/* gcc_pcie_0_cfg_ahb_clk */
	0xF5,		/* gcc_pcie_0_mstr_axi_clk */
	0xF8,		/* gcc_pcie_0_pipe_clk */
	0xF4,		/* gcc_pcie_0_slv_axi_clk */
	0xF3,		/* gcc_pcie_0_slv_q2a_axi_clk */
	0xFF,		/* gcc_pcie_1_aux_clk */
	0xFE,		/* gcc_pcie_1_cfg_ahb_clk */
	0xFD,		/* gcc_pcie_1_mstr_axi_clk */
	0x100,		/* gcc_pcie_1_pipe_clk */
	0xFC,		/* gcc_pcie_1_slv_axi_clk */
	0xFB,		/* gcc_pcie_1_slv_q2a_axi_clk */
	0x103,		/* gcc_pcie_phy_aux_clk */
	0x9A,		/* gcc_pdm2_clk */
	0x98,		/* gcc_pdm_ahb_clk */
	0x99,		/* gcc_pdm_xo4_clk */
	0x9B,		/* gcc_prng_ahb_clk */
	0x47,		/* gcc_qmip_camera_nrt_ahb_clk */
	0x48,		/* gcc_qmip_camera_rt_ahb_clk */
	0x49,		/* gcc_qmip_disp_ahb_clk */
	0x45,		/* gcc_qmip_video_cvp_ahb_clk */
	0x46,		/* gcc_qmip_video_vcodec_ahb_clk */
	0x178,		/* gcc_qspi_cnoc_periph_ahb_clk */
	0x179,		/* gcc_qspi_core_clk */
	0x86,		/* gcc_qupv3_wrap0_s0_clk */
	0x87,		/* gcc_qupv3_wrap0_s1_clk */
	0x88,		/* gcc_qupv3_wrap0_s2_clk */
	0x89,		/* gcc_qupv3_wrap0_s3_clk */
	0x8A,		/* gcc_qupv3_wrap0_s4_clk */
	0x8B,		/* gcc_qupv3_wrap0_s5_clk */
	0x8C,		/* gcc_qupv3_wrap0_s6_clk */
	0x8D,		/* gcc_qupv3_wrap0_s7_clk */
	0x92,		/* gcc_qupv3_wrap1_s0_clk */
	0x93,		/* gcc_qupv3_wrap1_s1_clk */
	0x94,		/* gcc_qupv3_wrap1_s2_clk */
	0x95,		/* gcc_qupv3_wrap1_s3_clk */
	0x96,		/* gcc_qupv3_wrap1_s4_clk */
	0x97,		/* gcc_qupv3_wrap1_s5_clk */
	0x185,		/* gcc_qupv3_wrap2_s0_clk */
	0x186,		/* gcc_qupv3_wrap2_s1_clk */
	0x187,		/* gcc_qupv3_wrap2_s2_clk */
	0x188,		/* gcc_qupv3_wrap2_s3_clk */
	0x189,		/* gcc_qupv3_wrap2_s4_clk */
	0x18A,		/* gcc_qupv3_wrap2_s5_clk */
	0x82,		/* gcc_qupv3_wrap_0_m_ahb_clk */
	0x83,		/* gcc_qupv3_wrap_0_s_ahb_clk */
	0x8E,		/* gcc_qupv3_wrap_1_m_ahb_clk */
	0x8F,		/* gcc_qupv3_wrap_1_s_ahb_clk */
	0x181,		/* gcc_qupv3_wrap_2_m_ahb_clk */
	0x182,		/* gcc_qupv3_wrap_2_s_ahb_clk */
	0x7F,		/* gcc_sdcc2_ahb_clk */
	0x7E,		/* gcc_sdcc2_apps_clk */
	0x81,		/* gcc_sdcc4_ahb_clk */
	0x80,		/* gcc_sdcc4_apps_clk */
	0xC,		/* gcc_sys_noc_cpuss_ahb_clk */
	0x9C,		/* gcc_tsif_ahb_clk */
	0x9E,		/* gcc_tsif_inactivity_timers_clk */
	0x9D,		/* gcc_tsif_ref_clk */
	0x107,		/* gcc_ufs_card_ahb_clk */
	0x106,		/* gcc_ufs_card_axi_clk */
	0x10D,		/* gcc_ufs_card_ice_core_clk */
	0x10E,		/* gcc_ufs_card_phy_aux_clk */
	0x109,		/* gcc_ufs_card_rx_symbol_0_clk */
	0x10F,		/* gcc_ufs_card_rx_symbol_1_clk */
	0x108,		/* gcc_ufs_card_tx_symbol_0_clk */
	0x10C,		/* gcc_ufs_card_unipro_core_clk */
	0x113,		/* gcc_ufs_phy_ahb_clk */
	0x112,		/* gcc_ufs_phy_axi_clk */
	0x119,		/* gcc_ufs_phy_ice_core_clk */
	0x11A,		/* gcc_ufs_phy_phy_aux_clk */
	0x115,		/* gcc_ufs_phy_rx_symbol_0_clk */
	0x11B,		/* gcc_ufs_phy_rx_symbol_1_clk */
	0x114,		/* gcc_ufs_phy_tx_symbol_0_clk */
	0x118,		/* gcc_ufs_phy_unipro_core_clk */
	0x6B,		/* gcc_usb30_prim_master_clk */
	0x6D,		/* gcc_usb30_prim_mock_utmi_clk */
	0x6C,		/* gcc_usb30_prim_sleep_clk */
	0x72,		/* gcc_usb30_sec_master_clk */
	0x74,		/* gcc_usb30_sec_mock_utmi_clk */
	0x73,		/* gcc_usb30_sec_sleep_clk */
	0x6E,		/* gcc_usb3_prim_phy_aux_clk */
	0x6F,		/* gcc_usb3_prim_phy_com_aux_clk */
	0x70,		/* gcc_usb3_prim_phy_pipe_clk */
	0x75,		/* gcc_usb3_sec_phy_aux_clk */
	0x76,		/* gcc_usb3_sec_phy_com_aux_clk */
	0x77,		/* gcc_usb3_sec_phy_pipe_clk */
	0x4A,		/* gcc_video_axi0_clk */
	0x4B,		/* gcc_video_axi1_clk */
	0x4C,		/* gcc_video_axic_clk */
	0x162,		/* gpu_cc_debug_mux */
	0xDB,		/* measure_only_cdsp_clk */
	0x19,		/* measure_only_cnoc_clk */
	0x43,		/* measure_only_gcc_camera_ahb_clk */
	0x52,		/* measure_only_gcc_camera_xo_clk */
	0xE5,		/* measure_only_gcc_cpuss_dvm_bus_clk */
	0xE1,		/* measure_only_gcc_cpuss_gnoc_clk */
	0x44,		/* measure_only_gcc_disp_ahb_clk */
	0x53,		/* measure_only_gcc_disp_xo_clk */
	0x160,		/* measure_only_gcc_gpu_cfg_ahb_clk */
	0x17A,		/* measure_only_gcc_npu_cfg_ahb_clk */
	0x42,		/* measure_only_gcc_video_ahb_clk */
	0x51,		/* measure_only_gcc_video_xo_clk */
	0x147,		/* measure_only_ipa_2x_clk */
	0x7,		/* measure_only_snoc_clk */
	0x180,		/* npu_cc_debug_mux */
	0x57,		/* video_cc_debug_mux */
	0xD0,		/* mc_cc_debug_mux */
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
	"measure_only_gpu_cc_ahb_clk",
	"measure_only_gpu_cc_cx_gfx3d_clk",
	"measure_only_gpu_cc_cx_gfx3d_slv_clk",
	"measure_only_gpu_cc_gx_gfx3d_clk",
};

static int gpu_cc_debug_mux_sels[] = {
	0x11,		/* gpu_cc_crc_ahb_clk */
	0x18,		/* gpu_cc_cx_gmu_clk */
	0x15,		/* gpu_cc_cx_snoc_dvm_clk */
	0xA,		/* gpu_cc_cxo_aon_clk */
	0x19,		/* gpu_cc_cxo_clk */
	0xF,		/* gpu_cc_gx_gmu_clk */
	0x16,		/* gpu_cc_sleep_clk */
	0x10,		/* measure_only_gpu_cc_ahb_clk */
	0x1A,		/* measure_only_gpu_cc_cx_gfx3d_clk */
	0x1B,		/* measure_only_gpu_cc_cx_gfx3d_slv_clk */
	0xB,		/* measure_only_gpu_cc_gx_gfx3d_clk */
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
	"measure_only_npu_cc_xo_clk",
	"npu_cc_armwic_core_clk",
	"npu_cc_bto_core_clk",
	"npu_cc_bwmon_clk",
	"npu_cc_cal_dp_cdc_clk",
	"npu_cc_cal_dp_clk",
	"npu_cc_comp_noc_axi_clk",
	"npu_cc_conf_noc_ahb_clk",
	"npu_cc_npu_core_apb_clk",
	"npu_cc_npu_core_atb_clk",
	"npu_cc_npu_core_clk",
	"npu_cc_npu_core_cti_clk",
	"npu_cc_npu_cpc_clk",
	"npu_cc_npu_cpc_timer_clk",
	"npu_cc_perf_cnt_clk",
	"npu_cc_qtimer_core_clk",
	"npu_cc_sleep_clk",
};

static int npu_cc_debug_mux_sels[] = {
	0x11,		/* measure_only_npu_cc_xo_clk */
	0x4,		/* npu_cc_armwic_core_clk */
	0x12,		/* npu_cc_bto_core_clk */
	0xF,		/* npu_cc_bwmon_clk */
	0x8,		/* npu_cc_cal_dp_cdc_clk */
	0x1,		/* npu_cc_cal_dp_clk */
	0x9,		/* npu_cc_comp_noc_axi_clk */
	0xA,		/* npu_cc_conf_noc_ahb_clk */
	0xE,		/* npu_cc_npu_core_apb_clk */
	0xB,		/* npu_cc_npu_core_atb_clk */
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
	"measure_only_video_cc_xo_clk",
	"video_cc_iris_ahb_clk",
	"video_cc_mvs0_core_clk",
	"video_cc_mvs1_core_clk",
	"video_cc_mvsc_core_clk",
};

static int video_cc_debug_mux_sels[] = {
	0x8,		/* measure_only_video_cc_xo_clk */
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
	{ .mux = &cpu_cc_debug_mux, .regmap_name = "qcom,cpucc" },
	{ .mux = &cam_cc_debug_mux, .regmap_name = "qcom,camcc" },
	{ .mux = &disp_cc_debug_mux, .regmap_name = "qcom,dispcc" },
	{ .mux = &gpu_cc_debug_mux, .regmap_name = "qcom,gpucc" },
	{ .mux = &mc_cc_debug_mux, .regmap_name = "qcom,mccc" },
	{ .mux = &npu_cc_debug_mux, .regmap_name = "qcom,npucc" },
	{ .mux = &video_cc_debug_mux, .regmap_name = "qcom,videocc" },
	{ .mux = &gcc_debug_mux, .regmap_name = "qcom,gcc" },
};

static struct clk_dummy l3_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "l3_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_cam_cc_gdsc_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_cam_cc_gdsc_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_cdsp_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_cdsp_clk",
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

static struct clk_dummy measure_only_disp_cc_xo_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_disp_cc_xo_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gcc_camera_ahb_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_gcc_camera_ahb_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gcc_camera_xo_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_gcc_camera_xo_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gcc_cpuss_dvm_bus_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_gcc_cpuss_dvm_bus_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gcc_cpuss_gnoc_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_gcc_cpuss_gnoc_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gcc_disp_ahb_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_gcc_disp_ahb_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gcc_disp_xo_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_gcc_disp_xo_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gcc_gpu_cfg_ahb_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_gcc_gpu_cfg_ahb_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gcc_npu_cfg_ahb_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_gcc_npu_cfg_ahb_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gcc_video_ahb_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_gcc_video_ahb_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gcc_video_xo_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_gcc_video_xo_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gpu_cc_ahb_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_gpu_cc_ahb_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gpu_cc_cx_gfx3d_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_gpu_cc_cx_gfx3d_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gpu_cc_cx_gfx3d_slv_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_gpu_cc_cx_gfx3d_slv_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_gpu_cc_gx_gfx3d_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_gpu_cc_gx_gfx3d_clk",
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

static struct clk_dummy measure_only_npu_cc_xo_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_npu_cc_xo_clk",
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

static struct clk_dummy perfcl_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "perfcl_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy perfpcl_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "perfpcl_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy pwrcl_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "pwrcl_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_dummy measure_only_video_cc_xo_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_video_cc_xo_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_hw *debugcc_sm8150_hws[] = {
	&l3_clk.hw,
	&measure_only_cam_cc_gdsc_clk.hw,
	&measure_only_cdsp_clk.hw,
	&measure_only_cnoc_clk.hw,
	&measure_only_disp_cc_xo_clk.hw,
	&measure_only_gcc_camera_ahb_clk.hw,
	&measure_only_gcc_camera_xo_clk.hw,
	&measure_only_gcc_cpuss_dvm_bus_clk.hw,
	&measure_only_gcc_cpuss_gnoc_clk.hw,
	&measure_only_gcc_disp_ahb_clk.hw,
	&measure_only_gcc_disp_xo_clk.hw,
	&measure_only_gcc_gpu_cfg_ahb_clk.hw,
	&measure_only_gcc_npu_cfg_ahb_clk.hw,
	&measure_only_gcc_video_ahb_clk.hw,
	&measure_only_gcc_video_xo_clk.hw,
	&measure_only_gpu_cc_ahb_clk.hw,
	&measure_only_gpu_cc_cx_gfx3d_clk.hw,
	&measure_only_gpu_cc_cx_gfx3d_slv_clk.hw,
	&measure_only_gpu_cc_gx_gfx3d_clk.hw,
	&measure_only_ipa_2x_clk.hw,
	&measure_only_mccc_clk.hw,
	&measure_only_npu_cc_xo_clk.hw,
	&measure_only_snoc_clk.hw,
	&perfcl_clk.hw,
	&perfpcl_clk.hw,
	&pwrcl_clk.hw,
	&measure_only_video_cc_xo_clk.hw,
};

static const struct of_device_id clk_debug_match_table[] = {
	{ .compatible = "qcom,sm8150-debugcc" },
	{ }
};

static int clk_debug_sm8150_probe(struct platform_device *pdev)
{
	struct clk *clk;
	int ret, i;

	BUILD_BUG_ON(ARRAY_SIZE(cpu_cc_debug_mux_parent_names) !=
		ARRAY_SIZE(cpu_cc_debug_mux_sels));
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
		if (IS_ERR_OR_NULL(mux_list[i].mux->regmap)) {
			ret = map_debug_bases(pdev,
				mux_list[i].regmap_name, mux_list[i].mux);
			if (ret == -EBADR)
				continue;
			else if (ret)
				return ret;
		}
	}

	for (i = 0; i < ARRAY_SIZE(debugcc_sm8150_hws); i++) {
		clk = devm_clk_register(&pdev->dev, debugcc_sm8150_hws[i]);
		if (IS_ERR(clk)) {
			dev_err(&pdev->dev, "Unable to register %s, err:(%d)\n",
				clk_hw_get_name(debugcc_sm8150_hws[i]),
				PTR_ERR(clk));
			return PTR_ERR(clk);
		}
	}

	for (i = 0; i < ARRAY_SIZE(mux_list); i++) {
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
	.probe = clk_debug_sm8150_probe,
	.driver = {
		.name = "sm8150-debugcc",
		.of_match_table = clk_debug_match_table,
	},
};

static int __init clk_debug_sm8150_init(void)
{
	return platform_driver_register(&clk_debug_driver);
}
fs_initcall(clk_debug_sm8150_init);

MODULE_DESCRIPTION("QTI DEBUG CC SM8150 Driver");
MODULE_LICENSE("GPL");
