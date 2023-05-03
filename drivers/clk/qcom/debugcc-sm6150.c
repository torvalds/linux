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
	"cam_cc_cci_clk",
	"cam_cc_core_ahb_clk",
	"cam_cc_cpas_ahb_clk",
	"cam_cc_csi0phytimer_clk",
	"cam_cc_csi1phytimer_clk",
	"cam_cc_csi2phytimer_clk",
	"cam_cc_csiphy0_clk",
	"cam_cc_csiphy1_clk",
	"cam_cc_csiphy2_clk",
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
	"cam_cc_ife_lite_clk",
	"cam_cc_ife_lite_cphy_rx_clk",
	"cam_cc_ife_lite_csid_clk",
	"cam_cc_ipe_0_ahb_clk",
	"cam_cc_ipe_0_areg_clk",
	"cam_cc_ipe_0_axi_clk",
	"cam_cc_ipe_0_clk",
	"cam_cc_jpeg_clk",
	"cam_cc_lrme_clk",
	"cam_cc_mclk0_clk",
	"cam_cc_mclk1_clk",
	"cam_cc_mclk2_clk",
	"cam_cc_mclk3_clk",
	"cam_cc_soc_ahb_clk",
	"cam_cc_sys_tmr_clk",
};

static int cam_cc_debug_mux_sels[] = {
	0xE,		/* cam_cc_bps_ahb_clk */
	0xD,		/* cam_cc_bps_areg_clk */
	0xC,		/* cam_cc_bps_axi_clk */
	0xB,		/* cam_cc_bps_clk */
	0x2D,		/* cam_cc_camnoc_axi_clk */
	0x2A,		/* cam_cc_cci_clk */
	0x3A,		/* cam_cc_core_ahb_clk */
	0x2C,		/* cam_cc_cpas_ahb_clk */
	0x5,		/* cam_cc_csi0phytimer_clk */
	0x7,		/* cam_cc_csi1phytimer_clk */
	0x9,		/* cam_cc_csi2phytimer_clk */
	0x6,		/* cam_cc_csiphy0_clk */
	0x8,		/* cam_cc_csiphy1_clk */
	0xA,		/* cam_cc_csiphy2_clk */
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
	0x22,		/* cam_cc_ife_lite_clk */
	0x24,		/* cam_cc_ife_lite_cphy_rx_clk */
	0x23,		/* cam_cc_ife_lite_csid_clk */
	0x12,		/* cam_cc_ipe_0_ahb_clk */
	0x11,		/* cam_cc_ipe_0_areg_clk */
	0x10,		/* cam_cc_ipe_0_axi_clk */
	0xF,		/* cam_cc_ipe_0_clk */
	0x25,		/* cam_cc_jpeg_clk */
	0x2B,		/* cam_cc_lrme_clk */
	0x1,		/* cam_cc_mclk0_clk */
	0x2,		/* cam_cc_mclk1_clk */
	0x3,		/* cam_cc_mclk2_clk */
	0x4,		/* cam_cc_mclk3_clk */
	0x2E,		/* cam_cc_soc_ahb_clk */
	0x33,		/* cam_cc_sys_tmr_clk */
};

static struct clk_debug_mux cam_cc_debug_mux = {
	.priv = &debug_mux_priv,
	.debug_offset = 0xC000,
	.post_div_offset = 0xC004,
	.cbcr_offset = 0xC008,
	.src_sel_mask = 0xFF,
	.src_sel_shift = 0,
	.post_div_mask = 0x3,
	.post_div_shift = 0,
	.post_div_val = 2,
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
	"disp_cc_mdss_dp_aux_clk",
	"disp_cc_mdss_dp_crypto_clk",
	"disp_cc_mdss_dp_link_clk",
	"disp_cc_mdss_dp_link_intf_clk",
	"disp_cc_mdss_dp_pixel1_clk",
	"disp_cc_mdss_dp_pixel_clk",
	"disp_cc_mdss_esc0_clk",
	"disp_cc_mdss_mdp_clk",
	"disp_cc_mdss_mdp_lut_clk",
	"disp_cc_mdss_non_gdsc_ahb_clk",
	"disp_cc_mdss_pclk0_clk",
	"disp_cc_mdss_rot_clk",
	"disp_cc_mdss_rscc_ahb_clk",
	"disp_cc_mdss_rscc_vsync_clk",
	"disp_cc_mdss_vsync_clk",
};

static int disp_cc_debug_mux_sels[] = {
	0x1B,		/* disp_cc_mdss_ahb_clk */
	0x12,		/* disp_cc_mdss_byte0_clk */
	0x13,		/* disp_cc_mdss_byte0_intf_clk */
	0x1A,		/* disp_cc_mdss_dp_aux_clk */
	0x17,		/* disp_cc_mdss_dp_crypto_clk */
	0x15,		/* disp_cc_mdss_dp_link_clk */
	0x16,		/* disp_cc_mdss_dp_link_intf_clk */
	0x19,		/* disp_cc_mdss_dp_pixel1_clk */
	0x18,		/* disp_cc_mdss_dp_pixel_clk */
	0x14,		/* disp_cc_mdss_esc0_clk */
	0xE,		/* disp_cc_mdss_mdp_clk */
	0x10,		/* disp_cc_mdss_mdp_lut_clk */
	0x1C,		/* disp_cc_mdss_non_gdsc_ahb_clk */
	0xD,		/* disp_cc_mdss_pclk0_clk */
	0xF,		/* disp_cc_mdss_rot_clk */
	0x1E,		/* disp_cc_mdss_rscc_ahb_clk */
	0x1D,		/* disp_cc_mdss_rscc_vsync_clk */
	0x11,		/* disp_cc_mdss_vsync_clk */
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
	"gcc_aggre_ufs_phy_axi_clk",
	"gcc_aggre_usb2_sec_axi_clk",
	"gcc_aggre_usb3_prim_axi_clk",
	"gcc_ahb2phy_east_clk",
	"gcc_ahb2phy_west_clk",
	"gcc_boot_rom_ahb_clk",
	"gcc_camera_hf_axi_clk",
	"gcc_ce1_ahb_clk",
	"gcc_ce1_axi_clk",
	"gcc_ce1_clk",
	"gcc_cfg_noc_usb2_sec_axi_clk",
	"gcc_cfg_noc_usb3_prim_axi_clk",
	"gcc_cpuss_ahb_clk",
	"gcc_ddrss_gpu_axi_clk",
	"gcc_disp_gpll0_div_clk_src",
	"gcc_disp_hf_axi_clk",
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
	"gcc_pcie0_phy_refgen_clk",
	"gcc_pcie_0_aux_clk",
	"gcc_pcie_0_cfg_ahb_clk",
	"gcc_pcie_0_mstr_axi_clk",
	"gcc_pcie_0_pipe_clk",
	"gcc_pcie_0_slv_axi_clk",
	"gcc_pcie_0_slv_q2a_axi_clk",
	"gcc_pcie_phy_aux_clk",
	"gcc_pdm2_clk",
	"gcc_pdm_ahb_clk",
	"gcc_pdm_xo4_clk",
	"gcc_prng_ahb_clk",
	"gcc_qmip_camera_nrt_ahb_clk",
	"gcc_qmip_disp_ahb_clk",
	"gcc_qmip_pcie_ahb_clk",
	"gcc_qmip_video_vcodec_ahb_clk",
	"gcc_qspi_cnoc_periph_ahb_clk",
	"gcc_qspi_core_clk",
	"gcc_qupv3_wrap0_core_2x_clk",
	"gcc_qupv3_wrap0_core_clk",
	"gcc_qupv3_wrap0_s0_clk",
	"gcc_qupv3_wrap0_s1_clk",
	"gcc_qupv3_wrap0_s2_clk",
	"gcc_qupv3_wrap0_s3_clk",
	"gcc_qupv3_wrap0_s4_clk",
	"gcc_qupv3_wrap0_s5_clk",
	"gcc_qupv3_wrap1_core_2x_clk",
	"gcc_qupv3_wrap1_core_clk",
	"gcc_qupv3_wrap1_s0_clk",
	"gcc_qupv3_wrap1_s1_clk",
	"gcc_qupv3_wrap1_s2_clk",
	"gcc_qupv3_wrap1_s3_clk",
	"gcc_qupv3_wrap1_s4_clk",
	"gcc_qupv3_wrap1_s5_clk",
	"gcc_qupv3_wrap_0_m_ahb_clk",
	"gcc_qupv3_wrap_0_s_ahb_clk",
	"gcc_qupv3_wrap_1_m_ahb_clk",
	"gcc_qupv3_wrap_1_s_ahb_clk",
	"gcc_sdcc1_ahb_clk",
	"gcc_sdcc1_apps_clk",
	"gcc_sdcc1_ice_core_clk",
	"gcc_sdcc2_ahb_clk",
	"gcc_sdcc2_apps_clk",
	"gcc_sdr_core_clk",
	"gcc_sdr_csr_hclk",
	"gcc_sdr_pri_mi2s_clk",
	"gcc_sdr_sec_mi2s_clk",
	"gcc_sdr_wr0_mem_clk",
	"gcc_sdr_wr1_mem_clk",
	"gcc_sdr_wr2_mem_clk",
	"gcc_sys_noc_cpuss_ahb_clk",
	"gcc_ufs_phy_ahb_clk",
	"gcc_ufs_phy_axi_clk",
	"gcc_ufs_phy_ice_core_clk",
	"gcc_ufs_phy_phy_aux_clk",
	"gcc_ufs_phy_rx_symbol_0_clk",
	"gcc_ufs_phy_tx_symbol_0_clk",
	"gcc_ufs_phy_unipro_core_clk",
	"gcc_usb20_sec_master_clk",
	"gcc_usb20_sec_mock_utmi_clk",
	"gcc_usb20_sec_sleep_clk",
	"gcc_usb2_sec_phy_aux_clk",
	"gcc_usb2_sec_phy_com_aux_clk",
	"gcc_usb2_sec_phy_pipe_clk",
	"gcc_usb30_prim_master_clk",
	"gcc_usb30_prim_mock_utmi_clk",
	"gcc_usb30_prim_sleep_clk",
	"gcc_usb3_prim_phy_aux_clk",
	"gcc_usb3_prim_phy_com_aux_clk",
	"gcc_usb3_prim_phy_pipe_clk",
	"gcc_video_axi0_clk",
	"gpu_cc_debug_mux",
	"measure_only_cnoc_clk",
	"measure_only_ipa_2x_clk",
	"mc_cc_debug_mux",
	"measure_only_snoc_clk",
	"video_cc_debug_mux",
};

static int gcc_debug_mux_sels[] = {
	0xD6,		/* apss_cc_debug_mux */
	0x46,		/* cam_cc_debug_mux */
	0x47,		/* disp_cc_debug_mux */
	0x11D,		/* gcc_aggre_ufs_phy_axi_clk */
	0x1C8,		/* gcc_aggre_usb2_sec_axi_clk */
	0x11B,		/* gcc_aggre_usb3_prim_axi_clk */
	0x73,		/* gcc_ahb2phy_east_clk */
	0x6F,		/* gcc_ahb2phy_west_clk */
	0x94,		/* gcc_boot_rom_ahb_clk */
	0x40,		/* gcc_camera_hf_axi_clk */
	0xA9,		/* gcc_ce1_ahb_clk */
	0xA8,		/* gcc_ce1_axi_clk */
	0xA7,		/* gcc_ce1_clk */
	0x1C7,		/* gcc_cfg_noc_usb2_sec_axi_clk */
	0x1D,		/* gcc_cfg_noc_usb3_prim_axi_clk */
	0xCE,		/* gcc_cpuss_ahb_clk */
	0xBB,		/* gcc_ddrss_gpu_axi_clk */
	0x152,		/* gcc_disp_gpll0_div_clk_src */
	0x41,		/* gcc_disp_hf_axi_clk */
	0x16D,		/* gcc_emac_axi_clk */
	0x1D3,		/* gcc_emac_ptp_clk */
	0x16A,		/* gcc_emac_rgmii_clk */
	0x16C,		/* gcc_emac_slv_ahb_clk */
	0xDE,		/* gcc_gp1_clk */
	0xDF,		/* gcc_gp2_clk */
	0xE0,		/* gcc_gp3_clk */
	0x148,		/* gcc_gpu_gpll0_clk_src */
	0x149,		/* gcc_gpu_gpll0_div_clk_src */
	0x145,		/* gcc_gpu_memnoc_gfx_clk */
	0x147,		/* gcc_gpu_snoc_dvm_gfx_clk */
	0x1F3,		/* gcc_pcie0_phy_refgen_clk */
	0xE5,		/* gcc_pcie_0_aux_clk */
	0xE4,		/* gcc_pcie_0_cfg_ahb_clk */
	0xE3,		/* gcc_pcie_0_mstr_axi_clk */
	0xE6,		/* gcc_pcie_0_pipe_clk */
	0xE2,		/* gcc_pcie_0_slv_axi_clk */
	0xE1,		/* gcc_pcie_0_slv_q2a_axi_clk */
	0xEF,		/* gcc_pcie_phy_aux_clk */
	0x8E,		/* gcc_pdm2_clk */
	0x8C,		/* gcc_pdm_ahb_clk */
	0x8D,		/* gcc_pdm_xo4_clk */
	0x8F,		/* gcc_prng_ahb_clk */
	0x3D,		/* gcc_qmip_camera_nrt_ahb_clk */
	0x3E,		/* gcc_qmip_disp_ahb_clk */
	0xE8,		/* gcc_qmip_pcie_ahb_clk */
	0x15C,		/* gcc_qmip_video_vcodec_ahb_clk */
	0x168,		/* gcc_qspi_cnoc_periph_ahb_clk */
	0x169,		/* gcc_qspi_core_clk */
	0x77,		/* gcc_qupv3_wrap0_core_2x_clk */
	0x76,		/* gcc_qupv3_wrap0_core_clk */
	0x78,		/* gcc_qupv3_wrap0_s0_clk */
	0x79,		/* gcc_qupv3_wrap0_s1_clk */
	0x7A,		/* gcc_qupv3_wrap0_s2_clk */
	0x7B,		/* gcc_qupv3_wrap0_s3_clk */
	0x7C,		/* gcc_qupv3_wrap0_s4_clk */
	0x7D,		/* gcc_qupv3_wrap0_s5_clk */
	0x194,		/* gcc_qupv3_wrap1_core_2x_clk */
	0x193,		/* gcc_qupv3_wrap1_core_clk */
	0x195,		/* gcc_qupv3_wrap1_s0_clk */
	0x196,		/* gcc_qupv3_wrap1_s1_clk */
	0x197,		/* gcc_qupv3_wrap1_s2_clk */
	0x198,		/* gcc_qupv3_wrap1_s3_clk */
	0x199,		/* gcc_qupv3_wrap1_s4_clk */
	0x19A,		/* gcc_qupv3_wrap1_s5_clk */
	0x74,		/* gcc_qupv3_wrap_0_m_ahb_clk */
	0x75,		/* gcc_qupv3_wrap_0_s_ahb_clk */
	0x191,		/* gcc_qupv3_wrap_1_m_ahb_clk */
	0x192,		/* gcc_qupv3_wrap_1_s_ahb_clk */
	0x1F0,		/* gcc_sdcc1_ahb_clk */
	0x1CB,		/* gcc_sdcc1_apps_clk */
	0x185,		/* gcc_sdcc1_ice_core_clk */
	0x71,		/* gcc_sdcc2_ahb_clk */
	0x70,		/* gcc_sdcc2_apps_clk */
	0x65,		/* gcc_sdr_core_clk */
	0x69,		/* gcc_sdr_csr_hclk */
	0x1A1,		/* gcc_sdr_pri_mi2s_clk */
	0x1A2,		/* gcc_sdr_sec_mi2s_clk */
	0x66,		/* gcc_sdr_wr0_mem_clk */
	0x67,		/* gcc_sdr_wr1_mem_clk */
	0x68,		/* gcc_sdr_wr2_mem_clk */
	0xC,		/* gcc_sys_noc_cpuss_ahb_clk */
	0xFC,		/* gcc_ufs_phy_ahb_clk */
	0xFB,		/* gcc_ufs_phy_axi_clk */
	0x102,		/* gcc_ufs_phy_ice_core_clk */
	0x103,		/* gcc_ufs_phy_phy_aux_clk */
	0xFE,		/* gcc_ufs_phy_rx_symbol_0_clk */
	0xFD,		/* gcc_ufs_phy_tx_symbol_0_clk */
	0x101,		/* gcc_ufs_phy_unipro_core_clk */
	0x1AF,		/* gcc_usb20_sec_master_clk */
	0x1B1,		/* gcc_usb20_sec_mock_utmi_clk */
	0x1B0,		/* gcc_usb20_sec_sleep_clk */
	0x1BD,		/* gcc_usb2_sec_phy_aux_clk */
	0x1BE,		/* gcc_usb2_sec_phy_com_aux_clk */
	0x1BF,		/* gcc_usb2_sec_phy_pipe_clk */
	0x5F,		/* gcc_usb30_prim_master_clk */
	0x61,		/* gcc_usb30_prim_mock_utmi_clk */
	0x60,		/* gcc_usb30_prim_sleep_clk */
	0x62,		/* gcc_usb3_prim_phy_aux_clk */
	0x63,		/* gcc_usb3_prim_phy_com_aux_clk */
	0x64,		/* gcc_usb3_prim_phy_pipe_clk */
	0x3F,		/* gcc_video_axi0_clk */
	0x144,		/* gpu_cc_debug_mux */
	0x15,		/* measure_only_cnoc_clk */
	0x128,		/* measure_only_ipa_2x_clk */
	0xC2,		/* mc_cc_debug_mux */
	0x7,		/* measure_only_snoc_clk */
	0x48,		/* video_cc_debug_mux */
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
	.post_div_val = 1,
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
	"gpu_cc_cx_gfx3d_clk",
	"gpu_cc_cx_gmu_clk",
	"gpu_cc_cx_snoc_dvm_clk",
	"gpu_cc_cxo_aon_clk",
	"gpu_cc_cxo_clk",
	"gpu_cc_gx_gfx3d_clk",
	"gpu_cc_gx_gmu_clk",
	"gpu_cc_sleep_clk",
};

static int gpu_cc_debug_mux_sels[] = {
	0x11,		/* gpu_cc_crc_ahb_clk */
	0x1A,		/* gpu_cc_cx_gfx3d_clk */
	0x18,		/* gpu_cc_cx_gmu_clk */
	0x15,		/* gpu_cc_cx_snoc_dvm_clk */
	0xA,		/* gpu_cc_cxo_aon_clk */
	0x19,		/* gpu_cc_cxo_clk */
	0xB,		/* gpu_cc_gx_gfx3d_clk */
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

static const char *const video_cc_debug_mux_parent_names[] = {
	"video_cc_sleep_clk",
	"video_cc_vcodec0_axi_clk",
	"video_cc_vcodec0_core_clk",
	"video_cc_venus_ahb_clk",
	"video_cc_venus_ctl_axi_clk",
	"video_cc_venus_ctl_core_clk",
};

static int video_cc_debug_mux_sels[] = {
	0xD,		/* video_cc_sleep_clk */
	0x6,		/* video_cc_vcodec0_axi_clk */
	0x3,		/* video_cc_vcodec0_core_clk */
	0x9,		/* video_cc_venus_ahb_clk */
	0x5,		/* video_cc_venus_ctl_axi_clk */
	0x1,		/* video_cc_venus_ctl_core_clk */
};

static struct clk_debug_mux video_cc_debug_mux = {
	.priv = &debug_mux_priv,
	.debug_offset = 0xA4C,
	.post_div_offset = 0xA30,
	.cbcr_offset = 0xA38,
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

static struct clk_hw *debugcc_sm6150_hws[] = {
	&measure_only_apcs_gold_post_acd_clk.hw,
	&measure_only_apcs_l3_post_acd_clk.hw,
	&measure_only_apcs_silver_post_acd_clk.hw,
	&measure_only_cnoc_clk.hw,
	&measure_only_ipa_2x_clk.hw,
	&measure_only_mccc_clk.hw,
	&measure_only_snoc_clk.hw,
};

static const struct of_device_id clk_debug_match_table[] = {
	{ .compatible = "qcom,sm6150-debugcc" },
	{ }
};

static int clk_debug_sm6150_probe(struct platform_device *pdev)
{
	struct clk *clk;
	int ret = 0, i;

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

	for (i = 0; i < ARRAY_SIZE(debugcc_sm6150_hws); i++) {
		clk = devm_clk_register(&pdev->dev, debugcc_sm6150_hws[i]);
		if (IS_ERR(clk)) {
			dev_err(&pdev->dev, "Unable to register %s, err:(%d)\n",
				clk_hw_get_name(debugcc_sm6150_hws[i]),
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
	.probe = clk_debug_sm6150_probe,
	.driver = {
		.name = "sm6150-debugcc",
		.of_match_table = clk_debug_match_table,
	},
};

static int __init clk_debug_sm6150_init(void)
{
	return platform_driver_register(&clk_debug_driver);
}
fs_initcall(clk_debug_sm6150_init);

MODULE_DESCRIPTION("QTI DEBUG CC SM6150 Driver");
MODULE_LICENSE("GPL");
