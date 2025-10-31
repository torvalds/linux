// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021, Linaro Ltd.
 */

#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

#include <dt-bindings/interconnect/qcom,sc8180x.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"

static struct qcom_icc_node mas_qhm_a1noc_cfg;
static struct qcom_icc_node mas_xm_ufs_card;
static struct qcom_icc_node mas_xm_ufs_g4;
static struct qcom_icc_node mas_xm_ufs_mem;
static struct qcom_icc_node mas_xm_usb3_0;
static struct qcom_icc_node mas_xm_usb3_1;
static struct qcom_icc_node mas_xm_usb3_2;
static struct qcom_icc_node mas_qhm_a2noc_cfg;
static struct qcom_icc_node mas_qhm_qdss_bam;
static struct qcom_icc_node mas_qhm_qspi;
static struct qcom_icc_node mas_qhm_qspi1;
static struct qcom_icc_node mas_qhm_qup0;
static struct qcom_icc_node mas_qhm_qup1;
static struct qcom_icc_node mas_qhm_qup2;
static struct qcom_icc_node mas_qhm_sensorss_ahb;
static struct qcom_icc_node mas_qxm_crypto;
static struct qcom_icc_node mas_qxm_ipa;
static struct qcom_icc_node mas_xm_emac;
static struct qcom_icc_node mas_xm_pcie3_0;
static struct qcom_icc_node mas_xm_pcie3_1;
static struct qcom_icc_node mas_xm_pcie3_2;
static struct qcom_icc_node mas_xm_pcie3_3;
static struct qcom_icc_node mas_xm_qdss_etr;
static struct qcom_icc_node mas_xm_sdc2;
static struct qcom_icc_node mas_xm_sdc4;
static struct qcom_icc_node mas_qxm_camnoc_hf0_uncomp;
static struct qcom_icc_node mas_qxm_camnoc_hf1_uncomp;
static struct qcom_icc_node mas_qxm_camnoc_sf_uncomp;
static struct qcom_icc_node mas_qnm_npu;
static struct qcom_icc_node mas_qnm_snoc;
static struct qcom_icc_node mas_qhm_cnoc_dc_noc;
static struct qcom_icc_node mas_acm_apps;
static struct qcom_icc_node mas_acm_gpu_tcu;
static struct qcom_icc_node mas_acm_sys_tcu;
static struct qcom_icc_node mas_qhm_gemnoc_cfg;
static struct qcom_icc_node mas_qnm_cmpnoc;
static struct qcom_icc_node mas_qnm_gpu;
static struct qcom_icc_node mas_qnm_mnoc_hf;
static struct qcom_icc_node mas_qnm_mnoc_sf;
static struct qcom_icc_node mas_qnm_pcie;
static struct qcom_icc_node mas_qnm_snoc_gc;
static struct qcom_icc_node mas_qnm_snoc_sf;
static struct qcom_icc_node mas_qxm_ecc;
static struct qcom_icc_node mas_llcc_mc;
static struct qcom_icc_node mas_qhm_mnoc_cfg;
static struct qcom_icc_node mas_qxm_camnoc_hf0;
static struct qcom_icc_node mas_qxm_camnoc_hf1;
static struct qcom_icc_node mas_qxm_camnoc_sf;
static struct qcom_icc_node mas_qxm_mdp0;
static struct qcom_icc_node mas_qxm_mdp1;
static struct qcom_icc_node mas_qxm_rot;
static struct qcom_icc_node mas_qxm_venus0;
static struct qcom_icc_node mas_qxm_venus1;
static struct qcom_icc_node mas_qxm_venus_arm9;
static struct qcom_icc_node mas_qhm_snoc_cfg;
static struct qcom_icc_node mas_qnm_aggre1_noc;
static struct qcom_icc_node mas_qnm_aggre2_noc;
static struct qcom_icc_node mas_qnm_gemnoc;
static struct qcom_icc_node mas_qxm_pimem;
static struct qcom_icc_node mas_xm_gic;
static struct qcom_icc_node mas_qup_core_0;
static struct qcom_icc_node mas_qup_core_1;
static struct qcom_icc_node mas_qup_core_2;
static struct qcom_icc_node slv_qns_a1noc_snoc;
static struct qcom_icc_node slv_srvc_aggre1_noc;
static struct qcom_icc_node slv_qns_a2noc_snoc;
static struct qcom_icc_node slv_qns_pcie_mem_noc;
static struct qcom_icc_node slv_srvc_aggre2_noc;
static struct qcom_icc_node slv_qns_camnoc_uncomp;
static struct qcom_icc_node slv_qns_cdsp_mem_noc;
static struct qcom_icc_node slv_qhs_a1_noc_cfg;
static struct qcom_icc_node slv_qhs_a2_noc_cfg;
static struct qcom_icc_node slv_qhs_ahb2phy_refgen_center;
static struct qcom_icc_node slv_qhs_ahb2phy_refgen_east;
static struct qcom_icc_node slv_qhs_ahb2phy_refgen_west;
static struct qcom_icc_node slv_qhs_ahb2phy_south;
static struct qcom_icc_node slv_qhs_aop;
static struct qcom_icc_node slv_qhs_aoss;
static struct qcom_icc_node slv_qhs_camera_cfg;
static struct qcom_icc_node slv_qhs_clk_ctl;
static struct qcom_icc_node slv_qhs_compute_dsp;
static struct qcom_icc_node slv_qhs_cpr_cx;
static struct qcom_icc_node slv_qhs_cpr_mmcx;
static struct qcom_icc_node slv_qhs_cpr_mx;
static struct qcom_icc_node slv_qhs_crypto0_cfg;
static struct qcom_icc_node slv_qhs_ddrss_cfg;
static struct qcom_icc_node slv_qhs_display_cfg;
static struct qcom_icc_node slv_qhs_emac_cfg;
static struct qcom_icc_node slv_qhs_glm;
static struct qcom_icc_node slv_qhs_gpuss_cfg;
static struct qcom_icc_node slv_qhs_imem_cfg;
static struct qcom_icc_node slv_qhs_ipa;
static struct qcom_icc_node slv_qhs_mnoc_cfg;
static struct qcom_icc_node slv_qhs_npu_cfg;
static struct qcom_icc_node slv_qhs_pcie0_cfg;
static struct qcom_icc_node slv_qhs_pcie1_cfg;
static struct qcom_icc_node slv_qhs_pcie2_cfg;
static struct qcom_icc_node slv_qhs_pcie3_cfg;
static struct qcom_icc_node slv_qhs_pdm;
static struct qcom_icc_node slv_qhs_pimem_cfg;
static struct qcom_icc_node slv_qhs_prng;
static struct qcom_icc_node slv_qhs_qdss_cfg;
static struct qcom_icc_node slv_qhs_qspi_0;
static struct qcom_icc_node slv_qhs_qspi_1;
static struct qcom_icc_node slv_qhs_qupv3_east0;
static struct qcom_icc_node slv_qhs_qupv3_east1;
static struct qcom_icc_node slv_qhs_qupv3_west;
static struct qcom_icc_node slv_qhs_sdc2;
static struct qcom_icc_node slv_qhs_sdc4;
static struct qcom_icc_node slv_qhs_security;
static struct qcom_icc_node slv_qhs_snoc_cfg;
static struct qcom_icc_node slv_qhs_spss_cfg;
static struct qcom_icc_node slv_qhs_tcsr;
static struct qcom_icc_node slv_qhs_tlmm_east;
static struct qcom_icc_node slv_qhs_tlmm_south;
static struct qcom_icc_node slv_qhs_tlmm_west;
static struct qcom_icc_node slv_qhs_tsif;
static struct qcom_icc_node slv_qhs_ufs_card_cfg;
static struct qcom_icc_node slv_qhs_ufs_mem0_cfg;
static struct qcom_icc_node slv_qhs_ufs_mem1_cfg;
static struct qcom_icc_node slv_qhs_usb3_0;
static struct qcom_icc_node slv_qhs_usb3_1;
static struct qcom_icc_node slv_qhs_usb3_2;
static struct qcom_icc_node slv_qhs_venus_cfg;
static struct qcom_icc_node slv_qhs_vsense_ctrl_cfg;
static struct qcom_icc_node slv_srvc_cnoc;
static struct qcom_icc_node slv_qhs_gemnoc;
static struct qcom_icc_node slv_qhs_llcc;
static struct qcom_icc_node slv_qhs_mdsp_ms_mpu_cfg;
static struct qcom_icc_node slv_qns_ecc;
static struct qcom_icc_node slv_qns_gem_noc_snoc;
static struct qcom_icc_node slv_qns_llcc;
static struct qcom_icc_node slv_srvc_gemnoc;
static struct qcom_icc_node slv_srvc_gemnoc1;
static struct qcom_icc_node slv_ebi;
static struct qcom_icc_node slv_qns2_mem_noc;
static struct qcom_icc_node slv_qns_mem_noc_hf;
static struct qcom_icc_node slv_srvc_mnoc;
static struct qcom_icc_node slv_qhs_apss;
static struct qcom_icc_node slv_qns_cnoc;
static struct qcom_icc_node slv_qns_gemnoc_gc;
static struct qcom_icc_node slv_qns_gemnoc_sf;
static struct qcom_icc_node slv_qxs_imem;
static struct qcom_icc_node slv_qxs_pimem;
static struct qcom_icc_node slv_srvc_snoc;
static struct qcom_icc_node slv_xs_pcie_0;
static struct qcom_icc_node slv_xs_pcie_1;
static struct qcom_icc_node slv_xs_pcie_2;
static struct qcom_icc_node slv_xs_pcie_3;
static struct qcom_icc_node slv_xs_qdss_stm;
static struct qcom_icc_node slv_xs_sys_tcu_cfg;
static struct qcom_icc_node slv_qup_core_0;
static struct qcom_icc_node slv_qup_core_1;
static struct qcom_icc_node slv_qup_core_2;

static struct qcom_icc_node mas_qhm_a1noc_cfg = {
	.name = "mas_qhm_a1noc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &slv_srvc_aggre1_noc },
};

static struct qcom_icc_node mas_xm_ufs_card = {
	.name = "mas_xm_ufs_card",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &slv_qns_a1noc_snoc },
};

static struct qcom_icc_node mas_xm_ufs_g4 = {
	.name = "mas_xm_ufs_g4",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &slv_qns_a1noc_snoc },
};

static struct qcom_icc_node mas_xm_ufs_mem = {
	.name = "mas_xm_ufs_mem",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &slv_qns_a1noc_snoc },
};

static struct qcom_icc_node mas_xm_usb3_0 = {
	.name = "mas_xm_usb3_0",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &slv_qns_a1noc_snoc },
};

static struct qcom_icc_node mas_xm_usb3_1 = {
	.name = "mas_xm_usb3_1",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &slv_qns_a1noc_snoc },
};

static struct qcom_icc_node mas_xm_usb3_2 = {
	.name = "mas_xm_usb3_2",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &slv_qns_a1noc_snoc },
};

static struct qcom_icc_node mas_qhm_a2noc_cfg = {
	.name = "mas_qhm_a2noc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &slv_srvc_aggre2_noc },
};

static struct qcom_icc_node mas_qhm_qdss_bam = {
	.name = "mas_qhm_qdss_bam",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &slv_qns_a2noc_snoc },
};

static struct qcom_icc_node mas_qhm_qspi = {
	.name = "mas_qhm_qspi",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &slv_qns_a2noc_snoc },
};

static struct qcom_icc_node mas_qhm_qspi1 = {
	.name = "mas_qhm_qspi1",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &slv_qns_a2noc_snoc },
};

static struct qcom_icc_node mas_qhm_qup0 = {
	.name = "mas_qhm_qup0",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &slv_qns_a2noc_snoc },
};

static struct qcom_icc_node mas_qhm_qup1 = {
	.name = "mas_qhm_qup1",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &slv_qns_a2noc_snoc },
};

static struct qcom_icc_node mas_qhm_qup2 = {
	.name = "mas_qhm_qup2",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &slv_qns_a2noc_snoc },
};

static struct qcom_icc_node mas_qhm_sensorss_ahb = {
	.name = "mas_qhm_sensorss_ahb",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &slv_qns_a2noc_snoc },
};

static struct qcom_icc_node mas_qxm_crypto = {
	.name = "mas_qxm_crypto",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &slv_qns_a2noc_snoc },
};

static struct qcom_icc_node mas_qxm_ipa = {
	.name = "mas_qxm_ipa",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &slv_qns_a2noc_snoc },
};

static struct qcom_icc_node mas_xm_emac = {
	.name = "mas_xm_emac",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &slv_qns_a2noc_snoc },
};

static struct qcom_icc_node mas_xm_pcie3_0 = {
	.name = "mas_xm_pcie3_0",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &slv_qns_pcie_mem_noc },
};

static struct qcom_icc_node mas_xm_pcie3_1 = {
	.name = "mas_xm_pcie3_1",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &slv_qns_pcie_mem_noc },
};

static struct qcom_icc_node mas_xm_pcie3_2 = {
	.name = "mas_xm_pcie3_2",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &slv_qns_pcie_mem_noc },
};

static struct qcom_icc_node mas_xm_pcie3_3 = {
	.name = "mas_xm_pcie3_3",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &slv_qns_pcie_mem_noc },
};

static struct qcom_icc_node mas_xm_qdss_etr = {
	.name = "mas_xm_qdss_etr",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &slv_qns_a2noc_snoc },
};

static struct qcom_icc_node mas_xm_sdc2 = {
	.name = "mas_xm_sdc2",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &slv_qns_a2noc_snoc },
};

static struct qcom_icc_node mas_xm_sdc4 = {
	.name = "mas_xm_sdc4",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &slv_qns_a2noc_snoc },
};

static struct qcom_icc_node mas_qxm_camnoc_hf0_uncomp = {
	.name = "mas_qxm_camnoc_hf0_uncomp",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &slv_qns_camnoc_uncomp },
};

static struct qcom_icc_node mas_qxm_camnoc_hf1_uncomp = {
	.name = "mas_qxm_camnoc_hf1_uncomp",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &slv_qns_camnoc_uncomp },
};

static struct qcom_icc_node mas_qxm_camnoc_sf_uncomp = {
	.name = "mas_qxm_camnoc_sf_uncomp",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &slv_qns_camnoc_uncomp },
};

static struct qcom_icc_node mas_qnm_npu = {
	.name = "mas_qnm_npu",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &slv_qns_cdsp_mem_noc },
};

static struct qcom_icc_node mas_qnm_snoc = {
	.name = "mas_qnm_snoc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 56,
	.link_nodes = { &slv_qhs_tlmm_south,
			&slv_qhs_compute_dsp,
			&slv_qhs_spss_cfg,
			&slv_qhs_camera_cfg,
			&slv_qhs_sdc4,
			&slv_qhs_ahb2phy_refgen_center,
			&slv_qhs_sdc2,
			&slv_qhs_pcie2_cfg,
			&slv_qhs_mnoc_cfg,
			&slv_qhs_emac_cfg,
			&slv_qhs_qspi_0,
			&slv_qhs_qspi_1,
			&slv_qhs_tlmm_east,
			&slv_qhs_snoc_cfg,
			&slv_qhs_ahb2phy_refgen_east,
			&slv_qhs_glm,
			&slv_qhs_pdm,
			&slv_qhs_pcie1_cfg,
			&slv_qhs_a2_noc_cfg,
			&slv_qhs_qdss_cfg,
			&slv_qhs_display_cfg,
			&slv_qhs_tcsr,
			&slv_qhs_ufs_mem0_cfg,
			&slv_qhs_ddrss_cfg,
			&slv_qhs_pcie0_cfg,
			&slv_qhs_qupv3_east0,
			&slv_qhs_qupv3_east1,
			&slv_qhs_npu_cfg,
			&slv_qhs_crypto0_cfg,
			&slv_qhs_gpuss_cfg,
			&slv_qhs_venus_cfg,
			&slv_qhs_tsif,
			&slv_qhs_ipa,
			&slv_qhs_clk_ctl,
			&slv_qhs_security,
			&slv_qhs_aop,
			&slv_qhs_ahb2phy_refgen_west,
			&slv_qhs_ahb2phy_south,
			&slv_srvc_cnoc,
			&slv_qhs_ufs_card_cfg,
			&slv_qhs_usb3_1,
			&slv_qhs_usb3_2,
			&slv_qhs_pcie3_cfg,
			&slv_qhs_cpr_cx,
			&slv_qhs_tlmm_west,
			&slv_qhs_a1_noc_cfg,
			&slv_qhs_aoss,
			&slv_qhs_prng,
			&slv_qhs_vsense_ctrl_cfg,
			&slv_qhs_qupv3_west,
			&slv_qhs_usb3_0,
			&slv_qhs_cpr_mmcx,
			&slv_qhs_pimem_cfg,
			&slv_qhs_ufs_mem1_cfg,
			&slv_qhs_cpr_mx,
			&slv_qhs_imem_cfg },
};

static struct qcom_icc_node mas_qhm_cnoc_dc_noc = {
	.name = "mas_qhm_cnoc_dc_noc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.link_nodes = { &slv_qhs_llcc,
			&slv_qhs_gemnoc },
};

static struct qcom_icc_node mas_acm_apps = {
	.name = "mas_acm_apps",
	.channels = 4,
	.buswidth = 64,
	.num_links = 3,
	.link_nodes = { &slv_qns_ecc,
			&slv_qns_llcc,
			&slv_qns_gem_noc_snoc },
};

static struct qcom_icc_node mas_acm_gpu_tcu = {
	.name = "mas_acm_gpu_tcu",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = { &slv_qns_llcc,
			&slv_qns_gem_noc_snoc },
};

static struct qcom_icc_node mas_acm_sys_tcu = {
	.name = "mas_acm_sys_tcu",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = { &slv_qns_llcc,
			&slv_qns_gem_noc_snoc },
};

static struct qcom_icc_node mas_qhm_gemnoc_cfg = {
	.name = "mas_qhm_gemnoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 3,
	.link_nodes = { &slv_srvc_gemnoc1,
			&slv_srvc_gemnoc,
			&slv_qhs_mdsp_ms_mpu_cfg },
};

static struct qcom_icc_node mas_qnm_cmpnoc = {
	.name = "mas_qnm_cmpnoc",
	.channels = 2,
	.buswidth = 32,
	.num_links = 3,
	.link_nodes = { &slv_qns_ecc,
			&slv_qns_llcc,
			&slv_qns_gem_noc_snoc },
};

static struct qcom_icc_node mas_qnm_gpu = {
	.name = "mas_qnm_gpu",
	.channels = 4,
	.buswidth = 32,
	.num_links = 2,
	.link_nodes = { &slv_qns_llcc,
			&slv_qns_gem_noc_snoc },
};

static struct qcom_icc_node mas_qnm_mnoc_hf = {
	.name = "mas_qnm_mnoc_hf",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &slv_qns_llcc },
};

static struct qcom_icc_node mas_qnm_mnoc_sf = {
	.name = "mas_qnm_mnoc_sf",
	.channels = 1,
	.buswidth = 32,
	.num_links = 2,
	.link_nodes = { &slv_qns_llcc,
			&slv_qns_gem_noc_snoc },
};

static struct qcom_icc_node mas_qnm_pcie = {
	.name = "mas_qnm_pcie",
	.channels = 1,
	.buswidth = 32,
	.num_links = 2,
	.link_nodes = { &slv_qns_llcc,
			&slv_qns_gem_noc_snoc },
};

static struct qcom_icc_node mas_qnm_snoc_gc = {
	.name = "mas_qnm_snoc_gc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &slv_qns_llcc },
};

static struct qcom_icc_node mas_qnm_snoc_sf = {
	.name = "mas_qnm_snoc_sf",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &slv_qns_llcc },
};

static struct qcom_icc_node mas_qxm_ecc = {
	.name = "mas_qxm_ecc",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &slv_qns_llcc },
};

static struct qcom_icc_node mas_llcc_mc = {
	.name = "mas_llcc_mc",
	.channels = 8,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &slv_ebi },
};

static struct qcom_icc_node mas_qhm_mnoc_cfg = {
	.name = "mas_qhm_mnoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &slv_srvc_mnoc },
};

static struct qcom_icc_node mas_qxm_camnoc_hf0 = {
	.name = "mas_qxm_camnoc_hf0",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &slv_qns_mem_noc_hf },
};

static struct qcom_icc_node mas_qxm_camnoc_hf1 = {
	.name = "mas_qxm_camnoc_hf1",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &slv_qns_mem_noc_hf },
};

static struct qcom_icc_node mas_qxm_camnoc_sf = {
	.name = "mas_qxm_camnoc_sf",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &slv_qns2_mem_noc },
};

static struct qcom_icc_node mas_qxm_mdp0 = {
	.name = "mas_qxm_mdp0",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &slv_qns_mem_noc_hf },
};

static struct qcom_icc_node mas_qxm_mdp1 = {
	.name = "mas_qxm_mdp1",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &slv_qns_mem_noc_hf },
};

static struct qcom_icc_node mas_qxm_rot = {
	.name = "mas_qxm_rot",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &slv_qns2_mem_noc },
};

static struct qcom_icc_node mas_qxm_venus0 = {
	.name = "mas_qxm_venus0",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &slv_qns2_mem_noc },
};

static struct qcom_icc_node mas_qxm_venus1 = {
	.name = "mas_qxm_venus1",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &slv_qns2_mem_noc },
};

static struct qcom_icc_node mas_qxm_venus_arm9 = {
	.name = "mas_qxm_venus_arm9",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &slv_qns2_mem_noc },
};

static struct qcom_icc_node mas_qhm_snoc_cfg = {
	.name = "mas_qhm_snoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &slv_srvc_snoc },
};

static struct qcom_icc_node mas_qnm_aggre1_noc = {
	.name = "mas_qnm_aggre1_noc",
	.channels = 1,
	.buswidth = 32,
	.num_links = 6,
	.link_nodes = { &slv_qns_gemnoc_sf,
			&slv_qxs_pimem,
			&slv_qxs_imem,
			&slv_qhs_apss,
			&slv_qns_cnoc,
			&slv_xs_qdss_stm },
};

static struct qcom_icc_node mas_qnm_aggre2_noc = {
	.name = "mas_qnm_aggre2_noc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 11,
	.link_nodes = { &slv_qns_gemnoc_sf,
			&slv_qxs_pimem,
			&slv_xs_pcie_3,
			&slv_qxs_imem,
			&slv_qhs_apss,
			&slv_xs_pcie_2,
			&slv_qns_cnoc,
			&slv_xs_pcie_0,
			&slv_xs_pcie_1,
			&slv_xs_sys_tcu_cfg,
			&slv_xs_qdss_stm },
};

static struct qcom_icc_node mas_qnm_gemnoc = {
	.name = "mas_qnm_gemnoc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 6,
	.link_nodes = { &slv_qxs_pimem,
			&slv_qxs_imem,
			&slv_qhs_apss,
			&slv_qns_cnoc,
			&slv_xs_sys_tcu_cfg,
			&slv_xs_qdss_stm },
};

static struct qcom_icc_node mas_qxm_pimem = {
	.name = "mas_qxm_pimem",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = { &slv_qns_gemnoc_gc,
			&slv_qxs_imem },
};

static struct qcom_icc_node mas_xm_gic = {
	.name = "mas_xm_gic",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = { &slv_qns_gemnoc_gc,
			&slv_qxs_imem },
};

static struct qcom_icc_node mas_qup_core_0 = {
	.name = "mas_qup_core_0",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &slv_qup_core_0 },
};

static struct qcom_icc_node mas_qup_core_1 = {
	.name = "mas_qup_core_1",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &slv_qup_core_1 },
};

static struct qcom_icc_node mas_qup_core_2 = {
	.name = "mas_qup_core_2",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &slv_qup_core_2 },
};

static struct qcom_icc_node slv_qns_a1noc_snoc = {
	.name = "slv_qns_a1noc_snoc",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &mas_qnm_aggre1_noc },
};

static struct qcom_icc_node slv_srvc_aggre1_noc = {
	.name = "slv_srvc_aggre1_noc",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qns_a2noc_snoc = {
	.name = "slv_qns_a2noc_snoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &mas_qnm_aggre2_noc },
};

static struct qcom_icc_node slv_qns_pcie_mem_noc = {
	.name = "slv_qns_pcie_mem_noc",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &mas_qnm_pcie },
};

static struct qcom_icc_node slv_srvc_aggre2_noc = {
	.name = "slv_srvc_aggre2_noc",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qns_camnoc_uncomp = {
	.name = "slv_qns_camnoc_uncomp",
	.channels = 1,
	.buswidth = 32
};

static struct qcom_icc_node slv_qns_cdsp_mem_noc = {
	.name = "slv_qns_cdsp_mem_noc",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &mas_qnm_cmpnoc },
};

static struct qcom_icc_node slv_qhs_a1_noc_cfg = {
	.name = "slv_qhs_a1_noc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &mas_qhm_a1noc_cfg },
};

static struct qcom_icc_node slv_qhs_a2_noc_cfg = {
	.name = "slv_qhs_a2_noc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &mas_qhm_a2noc_cfg },
};

static struct qcom_icc_node slv_qhs_ahb2phy_refgen_center = {
	.name = "slv_qhs_ahb2phy_refgen_center",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_ahb2phy_refgen_east = {
	.name = "slv_qhs_ahb2phy_refgen_east",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_ahb2phy_refgen_west = {
	.name = "slv_qhs_ahb2phy_refgen_west",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_ahb2phy_south = {
	.name = "slv_qhs_ahb2phy_south",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_aop = {
	.name = "slv_qhs_aop",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_aoss = {
	.name = "slv_qhs_aoss",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_camera_cfg = {
	.name = "slv_qhs_camera_cfg",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_clk_ctl = {
	.name = "slv_qhs_clk_ctl",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_compute_dsp = {
	.name = "slv_qhs_compute_dsp",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_cpr_cx = {
	.name = "slv_qhs_cpr_cx",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_cpr_mmcx = {
	.name = "slv_qhs_cpr_mmcx",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_cpr_mx = {
	.name = "slv_qhs_cpr_mx",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_crypto0_cfg = {
	.name = "slv_qhs_crypto0_cfg",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_ddrss_cfg = {
	.name = "slv_qhs_ddrss_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &mas_qhm_cnoc_dc_noc },
};

static struct qcom_icc_node slv_qhs_display_cfg = {
	.name = "slv_qhs_display_cfg",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_emac_cfg = {
	.name = "slv_qhs_emac_cfg",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_glm = {
	.name = "slv_qhs_glm",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_gpuss_cfg = {
	.name = "slv_qhs_gpuss_cfg",
	.channels = 1,
	.buswidth = 8
};

static struct qcom_icc_node slv_qhs_imem_cfg = {
	.name = "slv_qhs_imem_cfg",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_ipa = {
	.name = "slv_qhs_ipa",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_mnoc_cfg = {
	.name = "slv_qhs_mnoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &mas_qhm_mnoc_cfg },
};

static struct qcom_icc_node slv_qhs_npu_cfg = {
	.name = "slv_qhs_npu_cfg",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_pcie0_cfg = {
	.name = "slv_qhs_pcie0_cfg",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_pcie1_cfg = {
	.name = "slv_qhs_pcie1_cfg",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_pcie2_cfg = {
	.name = "slv_qhs_pcie2_cfg",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_pcie3_cfg = {
	.name = "slv_qhs_pcie3_cfg",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_pdm = {
	.name = "slv_qhs_pdm",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_pimem_cfg = {
	.name = "slv_qhs_pimem_cfg",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_prng = {
	.name = "slv_qhs_prng",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_qdss_cfg = {
	.name = "slv_qhs_qdss_cfg",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_qspi_0 = {
	.name = "slv_qhs_qspi_0",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_qspi_1 = {
	.name = "slv_qhs_qspi_1",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_qupv3_east0 = {
	.name = "slv_qhs_qupv3_east0",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_qupv3_east1 = {
	.name = "slv_qhs_qupv3_east1",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_qupv3_west = {
	.name = "slv_qhs_qupv3_west",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_sdc2 = {
	.name = "slv_qhs_sdc2",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_sdc4 = {
	.name = "slv_qhs_sdc4",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_security = {
	.name = "slv_qhs_security",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_snoc_cfg = {
	.name = "slv_qhs_snoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &mas_qhm_snoc_cfg },
};

static struct qcom_icc_node slv_qhs_spss_cfg = {
	.name = "slv_qhs_spss_cfg",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_tcsr = {
	.name = "slv_qhs_tcsr",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_tlmm_east = {
	.name = "slv_qhs_tlmm_east",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_tlmm_south = {
	.name = "slv_qhs_tlmm_south",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_tlmm_west = {
	.name = "slv_qhs_tlmm_west",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_tsif = {
	.name = "slv_qhs_tsif",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_ufs_card_cfg = {
	.name = "slv_qhs_ufs_card_cfg",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_ufs_mem0_cfg = {
	.name = "slv_qhs_ufs_mem0_cfg",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_ufs_mem1_cfg = {
	.name = "slv_qhs_ufs_mem1_cfg",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_usb3_0 = {
	.name = "slv_qhs_usb3_0",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_usb3_1 = {
	.name = "slv_qhs_usb3_1",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_usb3_2 = {
	.name = "slv_qhs_usb3_2",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_venus_cfg = {
	.name = "slv_qhs_venus_cfg",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_vsense_ctrl_cfg = {
	.name = "slv_qhs_vsense_ctrl_cfg",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_srvc_cnoc = {
	.name = "slv_srvc_cnoc",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_gemnoc = {
	.name = "slv_qhs_gemnoc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &mas_qhm_gemnoc_cfg },
};

static struct qcom_icc_node slv_qhs_llcc = {
	.name = "slv_qhs_llcc",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_mdsp_ms_mpu_cfg = {
	.name = "slv_qhs_mdsp_ms_mpu_cfg",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qns_ecc = {
	.name = "slv_qns_ecc",
	.channels = 1,
	.buswidth = 32
};

static struct qcom_icc_node slv_qns_gem_noc_snoc = {
	.name = "slv_qns_gem_noc_snoc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &mas_qnm_gemnoc },
};

static struct qcom_icc_node slv_qns_llcc = {
	.name = "slv_qns_llcc",
	.channels = 8,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &mas_llcc_mc },
};

static struct qcom_icc_node slv_srvc_gemnoc = {
	.name = "slv_srvc_gemnoc",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_srvc_gemnoc1 = {
	.name = "slv_srvc_gemnoc1",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_ebi = {
	.name = "slv_ebi",
	.channels = 8,
	.buswidth = 4
};

static struct qcom_icc_node slv_qns2_mem_noc = {
	.name = "slv_qns2_mem_noc",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &mas_qnm_mnoc_sf },
};

static struct qcom_icc_node slv_qns_mem_noc_hf = {
	.name = "slv_qns_mem_noc_hf",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &mas_qnm_mnoc_hf },
};

static struct qcom_icc_node slv_srvc_mnoc = {
	.name = "slv_srvc_mnoc",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qhs_apss = {
	.name = "slv_qhs_apss",
	.channels = 1,
	.buswidth = 8
};

static struct qcom_icc_node slv_qns_cnoc = {
	.name = "slv_qns_cnoc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &mas_qnm_snoc },
};

static struct qcom_icc_node slv_qns_gemnoc_gc = {
	.name = "slv_qns_gemnoc_gc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &mas_qnm_snoc_gc },
};

static struct qcom_icc_node slv_qns_gemnoc_sf = {
	.name = "slv_qns_gemnoc_sf",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &mas_qnm_snoc_sf },
};

static struct qcom_icc_node slv_qxs_imem = {
	.name = "slv_qxs_imem",
	.channels = 1,
	.buswidth = 8
};

static struct qcom_icc_node slv_qxs_pimem = {
	.name = "slv_qxs_pimem",
	.channels = 1,
	.buswidth = 8
};

static struct qcom_icc_node slv_srvc_snoc = {
	.name = "slv_srvc_snoc",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_xs_pcie_0 = {
	.name = "slv_xs_pcie_0",
	.channels = 1,
	.buswidth = 8
};

static struct qcom_icc_node slv_xs_pcie_1 = {
	.name = "slv_xs_pcie_1",
	.channels = 1,
	.buswidth = 8
};

static struct qcom_icc_node slv_xs_pcie_2 = {
	.name = "slv_xs_pcie_2",
	.channels = 1,
	.buswidth = 8
};

static struct qcom_icc_node slv_xs_pcie_3 = {
	.name = "slv_xs_pcie_3",
	.channels = 1,
	.buswidth = 8
};

static struct qcom_icc_node slv_xs_qdss_stm = {
	.name = "slv_xs_qdss_stm",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_xs_sys_tcu_cfg = {
	.name = "slv_xs_sys_tcu_cfg",
	.channels = 1,
	.buswidth = 8
};

static struct qcom_icc_node slv_qup_core_0 = {
	.name = "slv_qup_core_0",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qup_core_1 = {
	.name = "slv_qup_core_1",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_node slv_qup_core_2 = {
	.name = "slv_qup_core_2",
	.channels = 1,
	.buswidth = 4
};

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.enable_mask = BIT(3),
	.num_nodes = 1,
	.nodes = { &slv_ebi }
};

static struct qcom_icc_bcm bcm_mc0 = {
	.name = "MC0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &slv_ebi }
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &slv_qns_llcc }
};

static struct qcom_icc_bcm bcm_mm0 = {
	.name = "MM0",
	.num_nodes = 1,
	.nodes = { &slv_qns_mem_noc_hf }
};

static struct qcom_icc_bcm bcm_co0 = {
	.name = "CO0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &slv_qns_cdsp_mem_noc }
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.num_nodes = 1,
	.nodes = { &mas_qxm_crypto }
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.keepalive = true,
	.num_nodes = 57,
	.nodes = { &mas_qnm_snoc,
		   &slv_qhs_a1_noc_cfg,
		   &slv_qhs_a2_noc_cfg,
		   &slv_qhs_ahb2phy_refgen_center,
		   &slv_qhs_ahb2phy_refgen_east,
		   &slv_qhs_ahb2phy_refgen_west,
		   &slv_qhs_ahb2phy_south,
		   &slv_qhs_aop,
		   &slv_qhs_aoss,
		   &slv_qhs_camera_cfg,
		   &slv_qhs_clk_ctl,
		   &slv_qhs_compute_dsp,
		   &slv_qhs_cpr_cx,
		   &slv_qhs_cpr_mmcx,
		   &slv_qhs_cpr_mx,
		   &slv_qhs_crypto0_cfg,
		   &slv_qhs_ddrss_cfg,
		   &slv_qhs_display_cfg,
		   &slv_qhs_emac_cfg,
		   &slv_qhs_glm,
		   &slv_qhs_gpuss_cfg,
		   &slv_qhs_imem_cfg,
		   &slv_qhs_ipa,
		   &slv_qhs_mnoc_cfg,
		   &slv_qhs_npu_cfg,
		   &slv_qhs_pcie0_cfg,
		   &slv_qhs_pcie1_cfg,
		   &slv_qhs_pcie2_cfg,
		   &slv_qhs_pcie3_cfg,
		   &slv_qhs_pdm,
		   &slv_qhs_pimem_cfg,
		   &slv_qhs_prng,
		   &slv_qhs_qdss_cfg,
		   &slv_qhs_qspi_0,
		   &slv_qhs_qspi_1,
		   &slv_qhs_qupv3_east0,
		   &slv_qhs_qupv3_east1,
		   &slv_qhs_qupv3_west,
		   &slv_qhs_sdc2,
		   &slv_qhs_sdc4,
		   &slv_qhs_security,
		   &slv_qhs_snoc_cfg,
		   &slv_qhs_spss_cfg,
		   &slv_qhs_tcsr,
		   &slv_qhs_tlmm_east,
		   &slv_qhs_tlmm_south,
		   &slv_qhs_tlmm_west,
		   &slv_qhs_tsif,
		   &slv_qhs_ufs_card_cfg,
		   &slv_qhs_ufs_mem0_cfg,
		   &slv_qhs_ufs_mem1_cfg,
		   &slv_qhs_usb3_0,
		   &slv_qhs_usb3_1,
		   &slv_qhs_usb3_2,
		   &slv_qhs_venus_cfg,
		   &slv_qhs_vsense_ctrl_cfg,
		   &slv_srvc_cnoc }
};

static struct qcom_icc_bcm bcm_mm1 = {
	.name = "MM1",
	.num_nodes = 7,
	.nodes = { &mas_qxm_camnoc_hf0_uncomp,
		   &mas_qxm_camnoc_hf1_uncomp,
		   &mas_qxm_camnoc_sf_uncomp,
		   &mas_qxm_camnoc_hf0,
		   &mas_qxm_camnoc_hf1,
		   &mas_qxm_mdp0,
		   &mas_qxm_mdp1 }
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.num_nodes = 3,
	.nodes = { &mas_qup_core_0,
		   &mas_qup_core_1,
		   &mas_qup_core_2 }
};

static struct qcom_icc_bcm bcm_sh2 = {
	.name = "SH2",
	.num_nodes = 1,
	.nodes = { &slv_qns_gem_noc_snoc }
};

static struct qcom_icc_bcm bcm_mm2 = {
	.name = "MM2",
	.num_nodes = 6,
	.nodes = { &mas_qxm_camnoc_sf,
		   &mas_qxm_rot,
		   &mas_qxm_venus0,
		   &mas_qxm_venus1,
		   &mas_qxm_venus_arm9,
		   &slv_qns2_mem_noc }
};

static struct qcom_icc_bcm bcm_sh3 = {
	.name = "SH3",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &mas_acm_apps }
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.num_nodes = 1,
	.nodes = { &slv_qns_gemnoc_sf }
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.num_nodes = 1,
	.nodes = { &slv_qxs_imem }
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &slv_qns_gemnoc_gc }
};

static struct qcom_icc_bcm bcm_co2 = {
	.name = "CO2",
	.num_nodes = 1,
	.nodes = { &mas_qnm_npu }
};

static struct qcom_icc_bcm bcm_sn3 = {
	.name = "SN3",
	.keepalive = true,
	.num_nodes = 2,
	.nodes = { &slv_srvc_aggre1_noc,
		  &slv_qns_cnoc }
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.num_nodes = 1,
	.nodes = { &slv_qxs_pimem }
};

static struct qcom_icc_bcm bcm_sn8 = {
	.name = "SN8",
	.num_nodes = 4,
	.nodes = { &slv_xs_pcie_0,
		   &slv_xs_pcie_1,
		   &slv_xs_pcie_2,
		   &slv_xs_pcie_3 }
};

static struct qcom_icc_bcm bcm_sn9 = {
	.name = "SN9",
	.num_nodes = 1,
	.nodes = { &mas_qnm_aggre1_noc }
};

static struct qcom_icc_bcm bcm_sn11 = {
	.name = "SN11",
	.num_nodes = 1,
	.nodes = { &mas_qnm_aggre2_noc }
};

static struct qcom_icc_bcm bcm_sn14 = {
	.name = "SN14",
	.num_nodes = 1,
	.nodes = { &slv_qns_pcie_mem_noc }
};

static struct qcom_icc_bcm bcm_sn15 = {
	.name = "SN15",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &mas_qnm_gemnoc }
};

static struct qcom_icc_bcm * const aggre1_noc_bcms[] = {
	&bcm_sn3,
	&bcm_ce0,
};

static struct qcom_icc_bcm * const aggre2_noc_bcms[] = {
	&bcm_sn14,
	&bcm_ce0,
};

static struct qcom_icc_bcm * const camnoc_virt_bcms[] = {
	&bcm_mm1,
};

static struct qcom_icc_bcm * const compute_noc_bcms[] = {
	&bcm_co0,
	&bcm_co2,
};

static struct qcom_icc_bcm * const config_noc_bcms[] = {
	&bcm_cn0,
};

static struct qcom_icc_bcm * const gem_noc_bcms[] = {
	&bcm_sh0,
	&bcm_sh2,
	&bcm_sh3,
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_mc0,
	&bcm_acv,
};

static struct qcom_icc_bcm * const mmss_noc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
	&bcm_mm2,
};

static struct qcom_icc_bcm * const system_noc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn2,
	&bcm_sn3,
	&bcm_sn4,
	&bcm_sn8,
	&bcm_sn9,
	&bcm_sn11,
	&bcm_sn15,
};

static struct qcom_icc_node * const aggre1_noc_nodes[] = {
	[MASTER_A1NOC_CFG] = &mas_qhm_a1noc_cfg,
	[MASTER_UFS_CARD] = &mas_xm_ufs_card,
	[MASTER_UFS_GEN4] = &mas_xm_ufs_g4,
	[MASTER_UFS_MEM] = &mas_xm_ufs_mem,
	[MASTER_USB3] = &mas_xm_usb3_0,
	[MASTER_USB3_1] = &mas_xm_usb3_1,
	[MASTER_USB3_2] = &mas_xm_usb3_2,
	[A1NOC_SNOC_SLV] = &slv_qns_a1noc_snoc,
	[SLAVE_SERVICE_A1NOC] = &slv_srvc_aggre1_noc,
};

static struct qcom_icc_node * const aggre2_noc_nodes[] = {
	[MASTER_A2NOC_CFG] = &mas_qhm_a2noc_cfg,
	[MASTER_QDSS_BAM] = &mas_qhm_qdss_bam,
	[MASTER_QSPI_0] = &mas_qhm_qspi,
	[MASTER_QSPI_1] = &mas_qhm_qspi1,
	[MASTER_QUP_0] = &mas_qhm_qup0,
	[MASTER_QUP_1] = &mas_qhm_qup1,
	[MASTER_QUP_2] = &mas_qhm_qup2,
	[MASTER_SENSORS_AHB] = &mas_qhm_sensorss_ahb,
	[MASTER_CRYPTO_CORE_0] = &mas_qxm_crypto,
	[MASTER_IPA] = &mas_qxm_ipa,
	[MASTER_EMAC] = &mas_xm_emac,
	[MASTER_PCIE] = &mas_xm_pcie3_0,
	[MASTER_PCIE_1] = &mas_xm_pcie3_1,
	[MASTER_PCIE_2] = &mas_xm_pcie3_2,
	[MASTER_PCIE_3] = &mas_xm_pcie3_3,
	[MASTER_QDSS_ETR] = &mas_xm_qdss_etr,
	[MASTER_SDCC_2] = &mas_xm_sdc2,
	[MASTER_SDCC_4] = &mas_xm_sdc4,
	[A2NOC_SNOC_SLV] = &slv_qns_a2noc_snoc,
	[SLAVE_ANOC_PCIE_GEM_NOC] = &slv_qns_pcie_mem_noc,
	[SLAVE_SERVICE_A2NOC] = &slv_srvc_aggre2_noc,
};

static struct qcom_icc_node * const camnoc_virt_nodes[] = {
	[MASTER_CAMNOC_HF0_UNCOMP] = &mas_qxm_camnoc_hf0_uncomp,
	[MASTER_CAMNOC_HF1_UNCOMP] = &mas_qxm_camnoc_hf1_uncomp,
	[MASTER_CAMNOC_SF_UNCOMP] = &mas_qxm_camnoc_sf_uncomp,
	[SLAVE_CAMNOC_UNCOMP] = &slv_qns_camnoc_uncomp,
};

static struct qcom_icc_node * const compute_noc_nodes[] = {
	[MASTER_NPU] = &mas_qnm_npu,
	[SLAVE_CDSP_MEM_NOC] = &slv_qns_cdsp_mem_noc,
};

static struct qcom_icc_node * const config_noc_nodes[] = {
	[SNOC_CNOC_MAS] = &mas_qnm_snoc,
	[SLAVE_A1NOC_CFG] = &slv_qhs_a1_noc_cfg,
	[SLAVE_A2NOC_CFG] = &slv_qhs_a2_noc_cfg,
	[SLAVE_AHB2PHY_CENTER] = &slv_qhs_ahb2phy_refgen_center,
	[SLAVE_AHB2PHY_EAST] = &slv_qhs_ahb2phy_refgen_east,
	[SLAVE_AHB2PHY_WEST] = &slv_qhs_ahb2phy_refgen_west,
	[SLAVE_AHB2PHY_SOUTH] = &slv_qhs_ahb2phy_south,
	[SLAVE_AOP] = &slv_qhs_aop,
	[SLAVE_AOSS] = &slv_qhs_aoss,
	[SLAVE_CAMERA_CFG] = &slv_qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &slv_qhs_clk_ctl,
	[SLAVE_CDSP_CFG] = &slv_qhs_compute_dsp,
	[SLAVE_RBCPR_CX_CFG] = &slv_qhs_cpr_cx,
	[SLAVE_RBCPR_MMCX_CFG] = &slv_qhs_cpr_mmcx,
	[SLAVE_RBCPR_MX_CFG] = &slv_qhs_cpr_mx,
	[SLAVE_CRYPTO_0_CFG] = &slv_qhs_crypto0_cfg,
	[SLAVE_CNOC_DDRSS] = &slv_qhs_ddrss_cfg,
	[SLAVE_DISPLAY_CFG] = &slv_qhs_display_cfg,
	[SLAVE_EMAC_CFG] = &slv_qhs_emac_cfg,
	[SLAVE_GLM] = &slv_qhs_glm,
	[SLAVE_GRAPHICS_3D_CFG] = &slv_qhs_gpuss_cfg,
	[SLAVE_IMEM_CFG] = &slv_qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &slv_qhs_ipa,
	[SLAVE_CNOC_MNOC_CFG] = &slv_qhs_mnoc_cfg,
	[SLAVE_NPU_CFG] = &slv_qhs_npu_cfg,
	[SLAVE_PCIE_0_CFG] = &slv_qhs_pcie0_cfg,
	[SLAVE_PCIE_1_CFG] = &slv_qhs_pcie1_cfg,
	[SLAVE_PCIE_2_CFG] = &slv_qhs_pcie2_cfg,
	[SLAVE_PCIE_3_CFG] = &slv_qhs_pcie3_cfg,
	[SLAVE_PDM] = &slv_qhs_pdm,
	[SLAVE_PIMEM_CFG] = &slv_qhs_pimem_cfg,
	[SLAVE_PRNG] = &slv_qhs_prng,
	[SLAVE_QDSS_CFG] = &slv_qhs_qdss_cfg,
	[SLAVE_QSPI_0] = &slv_qhs_qspi_0,
	[SLAVE_QSPI_1] = &slv_qhs_qspi_1,
	[SLAVE_QUP_1] = &slv_qhs_qupv3_east0,
	[SLAVE_QUP_2] = &slv_qhs_qupv3_east1,
	[SLAVE_QUP_0] = &slv_qhs_qupv3_west,
	[SLAVE_SDCC_2] = &slv_qhs_sdc2,
	[SLAVE_SDCC_4] = &slv_qhs_sdc4,
	[SLAVE_SECURITY] = &slv_qhs_security,
	[SLAVE_SNOC_CFG] = &slv_qhs_snoc_cfg,
	[SLAVE_SPSS_CFG] = &slv_qhs_spss_cfg,
	[SLAVE_TCSR] = &slv_qhs_tcsr,
	[SLAVE_TLMM_EAST] = &slv_qhs_tlmm_east,
	[SLAVE_TLMM_SOUTH] = &slv_qhs_tlmm_south,
	[SLAVE_TLMM_WEST] = &slv_qhs_tlmm_west,
	[SLAVE_TSIF] = &slv_qhs_tsif,
	[SLAVE_UFS_CARD_CFG] = &slv_qhs_ufs_card_cfg,
	[SLAVE_UFS_MEM_0_CFG] = &slv_qhs_ufs_mem0_cfg,
	[SLAVE_UFS_MEM_1_CFG] = &slv_qhs_ufs_mem1_cfg,
	[SLAVE_USB3] = &slv_qhs_usb3_0,
	[SLAVE_USB3_1] = &slv_qhs_usb3_1,
	[SLAVE_USB3_2] = &slv_qhs_usb3_2,
	[SLAVE_VENUS_CFG] = &slv_qhs_venus_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &slv_qhs_vsense_ctrl_cfg,
	[SLAVE_SERVICE_CNOC] = &slv_srvc_cnoc,
};

static struct qcom_icc_node * const dc_noc_nodes[] = {
	[MASTER_CNOC_DC_NOC] = &mas_qhm_cnoc_dc_noc,
	[SLAVE_GEM_NOC_CFG] = &slv_qhs_gemnoc,
	[SLAVE_LLCC_CFG] = &slv_qhs_llcc,
};

static struct qcom_icc_node * const gem_noc_nodes[] = {
	[MASTER_AMPSS_M0] = &mas_acm_apps,
	[MASTER_GPU_TCU] = &mas_acm_gpu_tcu,
	[MASTER_SYS_TCU] = &mas_acm_sys_tcu,
	[MASTER_GEM_NOC_CFG] = &mas_qhm_gemnoc_cfg,
	[MASTER_COMPUTE_NOC] = &mas_qnm_cmpnoc,
	[MASTER_GRAPHICS_3D] = &mas_qnm_gpu,
	[MASTER_MNOC_HF_MEM_NOC] = &mas_qnm_mnoc_hf,
	[MASTER_MNOC_SF_MEM_NOC] = &mas_qnm_mnoc_sf,
	[MASTER_GEM_NOC_PCIE_SNOC] = &mas_qnm_pcie,
	[MASTER_SNOC_GC_MEM_NOC] = &mas_qnm_snoc_gc,
	[MASTER_SNOC_SF_MEM_NOC] = &mas_qnm_snoc_sf,
	[MASTER_ECC] = &mas_qxm_ecc,
	[SLAVE_MSS_PROC_MS_MPU_CFG] = &slv_qhs_mdsp_ms_mpu_cfg,
	[SLAVE_ECC] = &slv_qns_ecc,
	[SLAVE_GEM_NOC_SNOC] = &slv_qns_gem_noc_snoc,
	[SLAVE_LLCC] = &slv_qns_llcc,
	[SLAVE_SERVICE_GEM_NOC] = &slv_srvc_gemnoc,
	[SLAVE_SERVICE_GEM_NOC_1] = &slv_srvc_gemnoc1,
};

static struct qcom_icc_node * const mc_virt_nodes[] = {
	[MASTER_LLCC] = &mas_llcc_mc,
	[SLAVE_EBI_CH0] = &slv_ebi,
};

static struct qcom_icc_node * const mmss_noc_nodes[] = {
	[MASTER_CNOC_MNOC_CFG] = &mas_qhm_mnoc_cfg,
	[MASTER_CAMNOC_HF0] = &mas_qxm_camnoc_hf0,
	[MASTER_CAMNOC_HF1] = &mas_qxm_camnoc_hf1,
	[MASTER_CAMNOC_SF] = &mas_qxm_camnoc_sf,
	[MASTER_MDP_PORT0] = &mas_qxm_mdp0,
	[MASTER_MDP_PORT1] = &mas_qxm_mdp1,
	[MASTER_ROTATOR] = &mas_qxm_rot,
	[MASTER_VIDEO_P0] = &mas_qxm_venus0,
	[MASTER_VIDEO_P1] = &mas_qxm_venus1,
	[MASTER_VIDEO_PROC] = &mas_qxm_venus_arm9,
	[SLAVE_MNOC_SF_MEM_NOC] = &slv_qns2_mem_noc,
	[SLAVE_MNOC_HF_MEM_NOC] = &slv_qns_mem_noc_hf,
	[SLAVE_SERVICE_MNOC] = &slv_srvc_mnoc,
};

static struct qcom_icc_node * const system_noc_nodes[] = {
	[MASTER_SNOC_CFG] = &mas_qhm_snoc_cfg,
	[A1NOC_SNOC_MAS] = &mas_qnm_aggre1_noc,
	[A2NOC_SNOC_MAS] = &mas_qnm_aggre2_noc,
	[MASTER_GEM_NOC_SNOC] = &mas_qnm_gemnoc,
	[MASTER_PIMEM] = &mas_qxm_pimem,
	[MASTER_GIC] = &mas_xm_gic,
	[SLAVE_APPSS] = &slv_qhs_apss,
	[SNOC_CNOC_SLV] = &slv_qns_cnoc,
	[SLAVE_SNOC_GEM_NOC_GC] = &slv_qns_gemnoc_gc,
	[SLAVE_SNOC_GEM_NOC_SF] = &slv_qns_gemnoc_sf,
	[SLAVE_OCIMEM] = &slv_qxs_imem,
	[SLAVE_PIMEM] = &slv_qxs_pimem,
	[SLAVE_SERVICE_SNOC] = &slv_srvc_snoc,
	[SLAVE_QDSS_STM] = &slv_xs_qdss_stm,
	[SLAVE_TCU] = &slv_xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sc8180x_aggre1_noc = {
	.nodes = aggre1_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre1_noc_nodes),
	.bcms = aggre1_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_noc_bcms),
};

static const struct qcom_icc_desc sc8180x_aggre2_noc = {
	.nodes = aggre2_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre2_noc_nodes),
	.bcms = aggre2_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_noc_bcms),
};

static const struct qcom_icc_desc sc8180x_camnoc_virt = {
	.nodes = camnoc_virt_nodes,
	.num_nodes = ARRAY_SIZE(camnoc_virt_nodes),
	.bcms = camnoc_virt_bcms,
	.num_bcms = ARRAY_SIZE(camnoc_virt_bcms),
};

static const struct qcom_icc_desc sc8180x_compute_noc = {
	.nodes = compute_noc_nodes,
	.num_nodes = ARRAY_SIZE(compute_noc_nodes),
	.bcms = compute_noc_bcms,
	.num_bcms = ARRAY_SIZE(compute_noc_bcms),
};

static const struct qcom_icc_desc sc8180x_config_noc = {
	.nodes = config_noc_nodes,
	.num_nodes = ARRAY_SIZE(config_noc_nodes),
	.bcms = config_noc_bcms,
	.num_bcms = ARRAY_SIZE(config_noc_bcms),
};

static const struct qcom_icc_desc sc8180x_dc_noc = {
	.nodes = dc_noc_nodes,
	.num_nodes = ARRAY_SIZE(dc_noc_nodes),
};

static const struct qcom_icc_desc sc8180x_gem_noc  = {
	.nodes = gem_noc_nodes,
	.num_nodes = ARRAY_SIZE(gem_noc_nodes),
	.bcms = gem_noc_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_bcms),
};

static const struct qcom_icc_desc sc8180x_mc_virt  = {
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static const struct qcom_icc_desc sc8180x_mmss_noc  = {
	.nodes = mmss_noc_nodes,
	.num_nodes = ARRAY_SIZE(mmss_noc_nodes),
	.bcms = mmss_noc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_bcms),
};

static const struct qcom_icc_desc sc8180x_system_noc  = {
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static struct qcom_icc_bcm * const qup_virt_bcms[] = {
	&bcm_qup0,
};

static struct qcom_icc_node * const qup_virt_nodes[] = {
	[MASTER_QUP_CORE_0] = &mas_qup_core_0,
	[MASTER_QUP_CORE_1] = &mas_qup_core_1,
	[MASTER_QUP_CORE_2] = &mas_qup_core_2,
	[SLAVE_QUP_CORE_0] = &slv_qup_core_0,
	[SLAVE_QUP_CORE_1] = &slv_qup_core_1,
	[SLAVE_QUP_CORE_2] = &slv_qup_core_2,
};

static const struct qcom_icc_desc sc8180x_qup_virt = {
	.nodes = qup_virt_nodes,
	.num_nodes = ARRAY_SIZE(qup_virt_nodes),
	.bcms = qup_virt_bcms,
	.num_bcms = ARRAY_SIZE(qup_virt_bcms),
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sc8180x-aggre1-noc", .data = &sc8180x_aggre1_noc },
	{ .compatible = "qcom,sc8180x-aggre2-noc", .data = &sc8180x_aggre2_noc },
	{ .compatible = "qcom,sc8180x-camnoc-virt", .data = &sc8180x_camnoc_virt },
	{ .compatible = "qcom,sc8180x-compute-noc", .data = &sc8180x_compute_noc, },
	{ .compatible = "qcom,sc8180x-config-noc", .data = &sc8180x_config_noc },
	{ .compatible = "qcom,sc8180x-dc-noc", .data = &sc8180x_dc_noc },
	{ .compatible = "qcom,sc8180x-gem-noc", .data = &sc8180x_gem_noc },
	{ .compatible = "qcom,sc8180x-mc-virt", .data = &sc8180x_mc_virt },
	{ .compatible = "qcom,sc8180x-mmss-noc", .data = &sc8180x_mmss_noc },
	{ .compatible = "qcom,sc8180x-qup-virt", .data = &sc8180x_qup_virt },
	{ .compatible = "qcom,sc8180x-system-noc", .data = &sc8180x_system_noc },
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-sc8180x",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(qnoc_driver);

MODULE_DESCRIPTION("Qualcomm sc8180x NoC driver");
MODULE_LICENSE("GPL v2");
