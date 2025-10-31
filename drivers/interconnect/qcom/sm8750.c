// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <dt-bindings/interconnect/qcom,sm8750-rpmh.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"

static struct qcom_icc_node qhm_qspi;
static struct qcom_icc_node qhm_qup1;
static struct qcom_icc_node qxm_qup02;
static struct qcom_icc_node xm_sdc4;
static struct qcom_icc_node xm_ufs_mem;
static struct qcom_icc_node xm_usb3_0;
static struct qcom_icc_node qhm_qdss_bam;
static struct qcom_icc_node qhm_qup2;
static struct qcom_icc_node qxm_crypto;
static struct qcom_icc_node qxm_ipa;
static struct qcom_icc_node qxm_soccp;
static struct qcom_icc_node qxm_sp;
static struct qcom_icc_node xm_qdss_etr_0;
static struct qcom_icc_node xm_qdss_etr_1;
static struct qcom_icc_node xm_sdc2;
static struct qcom_icc_node qup0_core_master;
static struct qcom_icc_node qup1_core_master;
static struct qcom_icc_node qup2_core_master;
static struct qcom_icc_node qsm_cfg;
static struct qcom_icc_node qnm_gemnoc_cnoc;
static struct qcom_icc_node qnm_gemnoc_pcie;
static struct qcom_icc_node alm_gpu_tcu;
static struct qcom_icc_node alm_sys_tcu;
static struct qcom_icc_node chm_apps;
static struct qcom_icc_node qnm_gpu;
static struct qcom_icc_node qnm_lpass_gemnoc;
static struct qcom_icc_node qnm_mdsp;
static struct qcom_icc_node qnm_mnoc_hf;
static struct qcom_icc_node qnm_mnoc_sf;
static struct qcom_icc_node qnm_nsp_gemnoc;
static struct qcom_icc_node qnm_pcie;
static struct qcom_icc_node qnm_snoc_sf;
static struct qcom_icc_node qnm_ubwc_p;
static struct qcom_icc_node xm_gic;
static struct qcom_icc_node qnm_lpiaon_noc;
static struct qcom_icc_node qnm_lpass_lpinoc;
static struct qcom_icc_node qnm_lpinoc_dsp_qns4m;
static struct qcom_icc_node llcc_mc;
static struct qcom_icc_node qnm_camnoc_hf;
static struct qcom_icc_node qnm_camnoc_nrt_icp_sf;
static struct qcom_icc_node qnm_camnoc_rt_cdm_sf;
static struct qcom_icc_node qnm_camnoc_sf;
static struct qcom_icc_node qnm_mdp;
static struct qcom_icc_node qnm_vapss_hcp;
static struct qcom_icc_node qnm_video_cv_cpu;
static struct qcom_icc_node qnm_video_eva;
static struct qcom_icc_node qnm_video_mvp;
static struct qcom_icc_node qnm_video_v_cpu;
static struct qcom_icc_node qsm_mnoc_cfg;
static struct qcom_icc_node qnm_nsp;
static struct qcom_icc_node qsm_pcie_anoc_cfg;
static struct qcom_icc_node xm_pcie3;
static struct qcom_icc_node qnm_aggre1_noc;
static struct qcom_icc_node qnm_aggre2_noc;
static struct qcom_icc_node qns_a1noc_snoc;
static struct qcom_icc_node qns_a2noc_snoc;
static struct qcom_icc_node qup0_core_slave;
static struct qcom_icc_node qup1_core_slave;
static struct qcom_icc_node qup2_core_slave;
static struct qcom_icc_node qhs_ahb2phy0;
static struct qcom_icc_node qhs_ahb2phy1;
static struct qcom_icc_node qhs_camera_cfg;
static struct qcom_icc_node qhs_clk_ctl;
static struct qcom_icc_node qhs_crypto0_cfg;
static struct qcom_icc_node qhs_display_cfg;
static struct qcom_icc_node qhs_eva_cfg;
static struct qcom_icc_node qhs_gpuss_cfg;
static struct qcom_icc_node qhs_i2c;
static struct qcom_icc_node qhs_i3c_ibi0_cfg;
static struct qcom_icc_node qhs_i3c_ibi1_cfg;
static struct qcom_icc_node qhs_imem_cfg;
static struct qcom_icc_node qhs_mss_cfg;
static struct qcom_icc_node qhs_pcie_cfg;
static struct qcom_icc_node qhs_prng;
static struct qcom_icc_node qhs_qdss_cfg;
static struct qcom_icc_node qhs_qspi;
static struct qcom_icc_node qhs_qup02;
static struct qcom_icc_node qhs_qup1;
static struct qcom_icc_node qhs_qup2;
static struct qcom_icc_node qhs_sdc2;
static struct qcom_icc_node qhs_sdc4;
static struct qcom_icc_node qhs_spss_cfg;
static struct qcom_icc_node qhs_tcsr;
static struct qcom_icc_node qhs_tlmm;
static struct qcom_icc_node qhs_ufs_mem_cfg;
static struct qcom_icc_node qhs_usb3_0;
static struct qcom_icc_node qhs_venus_cfg;
static struct qcom_icc_node qhs_vsense_ctrl_cfg;
static struct qcom_icc_node qss_mnoc_cfg;
static struct qcom_icc_node qss_pcie_anoc_cfg;
static struct qcom_icc_node xs_qdss_stm;
static struct qcom_icc_node xs_sys_tcu_cfg;
static struct qcom_icc_node qhs_aoss;
static struct qcom_icc_node qhs_ipa;
static struct qcom_icc_node qhs_ipc_router;
static struct qcom_icc_node qhs_soccp;
static struct qcom_icc_node qhs_tme_cfg;
static struct qcom_icc_node qns_apss;
static struct qcom_icc_node qss_cfg;
static struct qcom_icc_node qss_ddrss_cfg;
static struct qcom_icc_node qxs_boot_imem;
static struct qcom_icc_node qxs_imem;
static struct qcom_icc_node qxs_modem_boot_imem;
static struct qcom_icc_node srvc_cnoc_main;
static struct qcom_icc_node xs_pcie;
static struct qcom_icc_node chs_ubwc_p;
static struct qcom_icc_node qns_gem_noc_cnoc;
static struct qcom_icc_node qns_llcc;
static struct qcom_icc_node qns_pcie;
static struct qcom_icc_node qns_lpass_ag_noc_gemnoc;
static struct qcom_icc_node qns_lpass_aggnoc;
static struct qcom_icc_node qns_lpi_aon_noc;
static struct qcom_icc_node ebi;
static struct qcom_icc_node qns_mem_noc_hf;
static struct qcom_icc_node qns_mem_noc_sf;
static struct qcom_icc_node srvc_mnoc;
static struct qcom_icc_node qns_nsp_gemnoc;
static struct qcom_icc_node qns_pcie_mem_noc;
static struct qcom_icc_node srvc_pcie_aggre_noc;
static struct qcom_icc_node qns_gemnoc_sf;

static struct qcom_icc_node qhm_qspi = {
	.name = "qhm_qspi",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node qhm_qup1 = {
	.name = "qhm_qup1",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node qxm_qup02 = {
	.name = "qxm_qup02",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node xm_sdc4 = {
	.name = "xm_sdc4",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node xm_usb3_0 = {
	.name = "xm_usb3_0",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_node qhm_qup2 = {
	.name = "qhm_qup2",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_node qxm_crypto = {
	.name = "qxm_crypto",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_node qxm_ipa = {
	.name = "qxm_ipa",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_node qxm_soccp = {
	.name = "qxm_soccp",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_node qxm_sp = {
	.name = "qxm_sp",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_node xm_qdss_etr_0 = {
	.name = "xm_qdss_etr_0",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_node xm_qdss_etr_1 = {
	.name = "xm_qdss_etr_1",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_node xm_sdc2 = {
	.name = "xm_sdc2",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_node qup0_core_master = {
	.name = "qup0_core_master",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qup0_core_slave },
};

static struct qcom_icc_node qup1_core_master = {
	.name = "qup1_core_master",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qup1_core_slave },
};

static struct qcom_icc_node qup2_core_master = {
	.name = "qup2_core_master",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qup2_core_slave },
};

static struct qcom_icc_node qsm_cfg = {
	.name = "qsm_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 33,
	.link_nodes = { &qhs_ahb2phy0, &qhs_ahb2phy1,
			&qhs_camera_cfg, &qhs_clk_ctl,
			&qhs_crypto0_cfg, &qhs_display_cfg,
			&qhs_eva_cfg, &qhs_gpuss_cfg,
			&qhs_i2c, &qhs_i3c_ibi0_cfg,
			&qhs_i3c_ibi1_cfg, &qhs_imem_cfg,
			&qhs_mss_cfg, &qhs_pcie_cfg,
			&qhs_prng, &qhs_qdss_cfg,
			&qhs_qspi, &qhs_qup02,
			&qhs_qup1, &qhs_qup2,
			&qhs_sdc2, &qhs_sdc4,
			&qhs_spss_cfg, &qhs_tcsr,
			&qhs_tlmm, &qhs_ufs_mem_cfg,
			&qhs_usb3_0, &qhs_venus_cfg,
			&qhs_vsense_ctrl_cfg, &qss_mnoc_cfg,
			&qss_pcie_anoc_cfg, &xs_qdss_stm,
			&xs_sys_tcu_cfg },
};

static struct qcom_icc_node qnm_gemnoc_cnoc = {
	.name = "qnm_gemnoc_cnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 12,
	.link_nodes = { &qhs_aoss, &qhs_ipa,
			&qhs_ipc_router, &qhs_soccp,
			&qhs_tme_cfg, &qns_apss,
			&qss_cfg, &qss_ddrss_cfg,
			&qxs_boot_imem, &qxs_imem,
			&qxs_modem_boot_imem, &srvc_cnoc_main },
};

static struct qcom_icc_node qnm_gemnoc_pcie = {
	.name = "qnm_gemnoc_pcie",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &xs_pcie },
};

static struct qcom_icc_node alm_gpu_tcu = {
	.name = "alm_gpu_tcu",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc },
};

static struct qcom_icc_node alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc },
};

static struct qcom_icc_node chm_apps = {
	.name = "chm_apps",
	.channels = 4,
	.buswidth = 32,
	.num_links = 4,
	.link_nodes = { &chs_ubwc_p, &qns_gem_noc_cnoc,
			&qns_llcc, &qns_pcie },
};

static struct qcom_icc_node qnm_gpu = {
	.name = "qnm_gpu",
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc },
};

static struct qcom_icc_node qnm_lpass_gemnoc = {
	.name = "qnm_lpass_gemnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_mdsp = {
	.name = "qnm_mdsp",
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_mnoc_hf = {
	.name = "qnm_mnoc_hf",
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc },
};

static struct qcom_icc_node qnm_mnoc_sf = {
	.name = "qnm_mnoc_sf",
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc },
};

static struct qcom_icc_node qnm_nsp_gemnoc = {
	.name = "qnm_nsp_gemnoc",
	.channels = 2,
	.buswidth = 32,
	.num_links = 3,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_pcie = {
	.name = "qnm_pcie",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc },
};

static struct qcom_icc_node qnm_snoc_sf = {
	.name = "qnm_snoc_sf",
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qnm_ubwc_p = {
	.name = "qnm_ubwc_p",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_llcc },
};

static struct qcom_icc_node xm_gic = {
	.name = "xm_gic",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_llcc },
};

static struct qcom_icc_node qnm_lpiaon_noc = {
	.name = "qnm_lpiaon_noc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qns_lpass_ag_noc_gemnoc },
};

static struct qcom_icc_node qnm_lpass_lpinoc = {
	.name = "qnm_lpass_lpinoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qns_lpass_aggnoc },
};

static struct qcom_icc_node qnm_lpinoc_dsp_qns4m = {
	.name = "qnm_lpinoc_dsp_qns4m",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qns_lpi_aon_noc },
};

static struct qcom_icc_node llcc_mc = {
	.name = "llcc_mc",
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &ebi },
};

static struct qcom_icc_node qnm_camnoc_hf = {
	.name = "qnm_camnoc_hf",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_node qnm_camnoc_nrt_icp_sf = {
	.name = "qnm_camnoc_nrt_icp_sf",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_camnoc_rt_cdm_sf = {
	.name = "qnm_camnoc_rt_cdm_sf",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_camnoc_sf = {
	.name = "qnm_camnoc_sf",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_mdp = {
	.name = "qnm_mdp",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_node qnm_vapss_hcp = {
	.name = "qnm_vapss_hcp",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_video_cv_cpu = {
	.name = "qnm_video_cv_cpu",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_video_eva = {
	.name = "qnm_video_eva",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_video_mvp = {
	.name = "qnm_video_mvp",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_video_v_cpu = {
	.name = "qnm_video_v_cpu",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qsm_mnoc_cfg = {
	.name = "qsm_mnoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &srvc_mnoc },
};

static struct qcom_icc_node qnm_nsp = {
	.name = "qnm_nsp",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_nsp_gemnoc },
};

static struct qcom_icc_node qsm_pcie_anoc_cfg = {
	.name = "qsm_pcie_anoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &srvc_pcie_aggre_noc },
};

static struct qcom_icc_node xm_pcie3 = {
	.name = "xm_pcie3",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_pcie_mem_noc },
};

static struct qcom_icc_node qnm_aggre1_noc = {
	.name = "qnm_aggre1_noc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_node qnm_aggre2_noc = {
	.name = "qnm_aggre2_noc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_node qns_a1noc_snoc = {
	.name = "qns_a1noc_snoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_aggre1_noc },
};

static struct qcom_icc_node qns_a2noc_snoc = {
	.name = "qns_a2noc_snoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_aggre2_noc },
};

static struct qcom_icc_node qup0_core_slave = {
	.name = "qup0_core_slave",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qup1_core_slave = {
	.name = "qup1_core_slave",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qup2_core_slave = {
	.name = "qup2_core_slave",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ahb2phy0 = {
	.name = "qhs_ahb2phy0",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ahb2phy1 = {
	.name = "qhs_ahb2phy1",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_camera_cfg = {
	.name = "qhs_camera_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_display_cfg = {
	.name = "qhs_display_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_eva_cfg = {
	.name = "qhs_eva_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qhs_i2c = {
	.name = "qhs_i2c",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_i3c_ibi0_cfg = {
	.name = "qhs_i3c_ibi0_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_i3c_ibi1_cfg = {
	.name = "qhs_i3c_ibi1_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_mss_cfg = {
	.name = "qhs_mss_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie_cfg = {
	.name = "qhs_pcie_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_prng = {
	.name = "qhs_prng",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qspi = {
	.name = "qhs_qspi",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qup02 = {
	.name = "qhs_qup02",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qup1 = {
	.name = "qhs_qup1",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qup2 = {
	.name = "qhs_qup2",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_sdc2 = {
	.name = "qhs_sdc2",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_sdc4 = {
	.name = "qhs_sdc4",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_spss_cfg = {
	.name = "qhs_spss_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_tcsr = {
	.name = "qhs_tcsr",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_tlmm = {
	.name = "qhs_tlmm",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb3_0 = {
	.name = "qhs_usb3_0",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qss_mnoc_cfg = {
	.name = "qss_mnoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qsm_mnoc_cfg },
};

static struct qcom_icc_node qss_pcie_anoc_cfg = {
	.name = "qss_pcie_anoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qsm_pcie_anoc_cfg },
};

static struct qcom_icc_node xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qhs_aoss = {
	.name = "qhs_aoss",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ipa = {
	.name = "qhs_ipa",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ipc_router = {
	.name = "qhs_ipc_router",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_soccp = {
	.name = "qhs_soccp",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_tme_cfg = {
	.name = "qhs_tme_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_apss = {
	.name = "qns_apss",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qss_cfg = {
	.name = "qss_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qsm_cfg },
};

static struct qcom_icc_node qss_ddrss_cfg = {
	.name = "qss_ddrss_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qxs_boot_imem = {
	.name = "qxs_boot_imem",
	.channels = 1,
	.buswidth = 16,
};

static struct qcom_icc_node qxs_imem = {
	.name = "qxs_imem",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qxs_modem_boot_imem = {
	.name = "qxs_modem_boot_imem",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node srvc_cnoc_main = {
	.name = "srvc_cnoc_main",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node xs_pcie = {
	.name = "xs_pcie",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node chs_ubwc_p = {
	.name = "chs_ubwc_p",
	.channels = 1,
	.buswidth = 32,
};

static struct qcom_icc_node qns_gem_noc_cnoc = {
	.name = "qns_gem_noc_cnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_gemnoc_cnoc },
};

static struct qcom_icc_node qns_llcc = {
	.name = "qns_llcc",
	.channels = 4,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &llcc_mc },
};

static struct qcom_icc_node qns_pcie = {
	.name = "qns_pcie",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qnm_gemnoc_pcie },
};

static struct qcom_icc_node qns_lpass_ag_noc_gemnoc = {
	.name = "qns_lpass_ag_noc_gemnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_lpass_gemnoc },
};

static struct qcom_icc_node qns_lpass_aggnoc = {
	.name = "qns_lpass_aggnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_lpiaon_noc },
};

static struct qcom_icc_node qns_lpi_aon_noc = {
	.name = "qns_lpi_aon_noc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_lpass_lpinoc },
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.channels = 4,
	.buswidth = 4,
};

static struct qcom_icc_node qns_mem_noc_hf = {
	.name = "qns_mem_noc_hf",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qnm_mnoc_hf },
};

static struct qcom_icc_node qns_mem_noc_sf = {
	.name = "qns_mem_noc_sf",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qnm_mnoc_sf },
};

static struct qcom_icc_node srvc_mnoc = {
	.name = "srvc_mnoc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_nsp_gemnoc = {
	.name = "qns_nsp_gemnoc",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qnm_nsp_gemnoc },
};

static struct qcom_icc_node qns_pcie_mem_noc = {
	.name = "qns_pcie_mem_noc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qnm_pcie },
};

static struct qcom_icc_node srvc_pcie_aggre_noc = {
	.name = "srvc_pcie_aggre_noc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_gemnoc_sf = {
	.name = "qns_gemnoc_sf",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_snoc_sf },
};

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.enable_mask = BIT(0),
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.num_nodes = 1,
	.nodes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.enable_mask = BIT(0),
	.keepalive = true,
	.num_nodes = 44,
	.nodes = { &qsm_cfg, &qhs_ahb2phy0,
		   &qhs_ahb2phy1, &qhs_camera_cfg,
		   &qhs_clk_ctl, &qhs_crypto0_cfg,
		   &qhs_eva_cfg, &qhs_gpuss_cfg,
		   &qhs_i3c_ibi0_cfg, &qhs_i3c_ibi1_cfg,
		   &qhs_imem_cfg, &qhs_mss_cfg,
		   &qhs_pcie_cfg, &qhs_prng,
		   &qhs_qdss_cfg, &qhs_qspi,
		   &qhs_sdc2, &qhs_sdc4,
		   &qhs_spss_cfg, &qhs_tcsr,
		   &qhs_tlmm, &qhs_ufs_mem_cfg,
		   &qhs_usb3_0, &qhs_venus_cfg,
		   &qhs_vsense_ctrl_cfg, &qss_mnoc_cfg,
		   &qss_pcie_anoc_cfg, &xs_qdss_stm,
		   &xs_sys_tcu_cfg, &qnm_gemnoc_cnoc,
		   &qnm_gemnoc_pcie, &qhs_aoss,
		   &qhs_ipa, &qhs_ipc_router,
		   &qhs_soccp, &qhs_tme_cfg,
		   &qns_apss, &qss_cfg,
		   &qss_ddrss_cfg, &qxs_boot_imem,
		   &qxs_imem, &qxs_modem_boot_imem,
		   &srvc_cnoc_main, &xs_pcie },
};

static struct qcom_icc_bcm bcm_cn1 = {
	.name = "CN1",
	.num_nodes = 5,
	.nodes = { &qhs_display_cfg, &qhs_i2c,
		   &qhs_qup02, &qhs_qup1,
		   &qhs_qup2 },
};

static struct qcom_icc_bcm bcm_co0 = {
	.name = "CO0",
	.enable_mask = BIT(0),
	.num_nodes = 2,
	.nodes = { &qnm_nsp, &qns_nsp_gemnoc },
};

static struct qcom_icc_bcm bcm_lp0 = {
	.name = "LP0",
	.num_nodes = 2,
	.nodes = { &qnm_lpass_lpinoc, &qns_lpass_aggnoc },
};

static struct qcom_icc_bcm bcm_mc0 = {
	.name = "MC0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_mm0 = {
	.name = "MM0",
	.num_nodes = 1,
	.nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_bcm bcm_mm1 = {
	.name = "MM1",
	.enable_mask = BIT(0),
	.num_nodes = 9,
	.nodes = { &qnm_camnoc_hf, &qnm_camnoc_nrt_icp_sf,
		   &qnm_camnoc_rt_cdm_sf, &qnm_camnoc_sf,
		   &qnm_vapss_hcp, &qnm_video_cv_cpu,
		   &qnm_video_mvp, &qnm_video_v_cpu,
		   &qns_mem_noc_sf },
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.keepalive = true,
	.vote_scale = 1,
	.num_nodes = 1,
	.nodes = { &qup0_core_slave },
};

static struct qcom_icc_bcm bcm_qup1 = {
	.name = "QUP1",
	.keepalive = true,
	.vote_scale = 1,
	.num_nodes = 1,
	.nodes = { &qup1_core_slave },
};

static struct qcom_icc_bcm bcm_qup2 = {
	.name = "QUP2",
	.keepalive = true,
	.vote_scale = 1,
	.num_nodes = 1,
	.nodes = { &qup2_core_slave },
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_sh1 = {
	.name = "SH1",
	.enable_mask = BIT(0),
	.num_nodes = 14,
	.nodes = { &alm_gpu_tcu, &alm_sys_tcu,
		   &chm_apps, &qnm_gpu,
		   &qnm_mdsp, &qnm_mnoc_hf,
		   &qnm_mnoc_sf, &qnm_nsp_gemnoc,
		   &qnm_pcie, &qnm_snoc_sf,
		   &xm_gic, &chs_ubwc_p,
		   &qns_gem_noc_cnoc, &qns_pcie },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.num_nodes = 1,
	.nodes = { &qnm_aggre1_noc },
};

static struct qcom_icc_bcm bcm_sn3 = {
	.name = "SN3",
	.num_nodes = 1,
	.nodes = { &qnm_aggre2_noc },
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.num_nodes = 1,
	.nodes = { &qns_pcie_mem_noc },
};

static struct qcom_icc_bcm bcm_ubw0 = {
	.name = "UBW0",
	.num_nodes = 1,
	.nodes = { &qnm_ubwc_p },
};

static struct qcom_icc_node * const aggre1_noc_nodes[] = {
	[MASTER_QSPI_0] = &qhm_qspi,
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_QUP_3] = &qxm_qup02,
	[MASTER_SDCC_4] = &xm_sdc4,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_USB3_0] = &xm_usb3_0,
	[SLAVE_A1NOC_SNOC] = &qns_a1noc_snoc,
};

static const struct qcom_icc_desc sm8750_aggre1_noc = {
	.nodes = aggre1_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre1_noc_nodes),
};

static struct qcom_icc_bcm * const aggre2_noc_bcms[] = {
	&bcm_ce0,
};

static struct qcom_icc_node * const aggre2_noc_nodes[] = {
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QUP_2] = &qhm_qup2,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_SOCCP_AGGR_NOC] = &qxm_soccp,
	[MASTER_SP] = &qxm_sp,
	[MASTER_QDSS_ETR] = &xm_qdss_etr_0,
	[MASTER_QDSS_ETR_1] = &xm_qdss_etr_1,
	[MASTER_SDCC_2] = &xm_sdc2,
	[SLAVE_A2NOC_SNOC] = &qns_a2noc_snoc,
};

static const struct qcom_icc_desc sm8750_aggre2_noc = {
	.nodes = aggre2_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre2_noc_nodes),
	.bcms = aggre2_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_noc_bcms),
};

static struct qcom_icc_bcm * const clk_virt_bcms[] = {
	&bcm_qup0,
	&bcm_qup1,
	&bcm_qup2,
};

static struct qcom_icc_node * const clk_virt_nodes[] = {
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[MASTER_QUP_CORE_1] = &qup1_core_master,
	[MASTER_QUP_CORE_2] = &qup2_core_master,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_QUP_CORE_1] = &qup1_core_slave,
	[SLAVE_QUP_CORE_2] = &qup2_core_slave,
};

static const struct qcom_icc_desc sm8750_clk_virt = {
	.nodes = clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(clk_virt_nodes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
};

static struct qcom_icc_bcm * const config_noc_bcms[] = {
	&bcm_cn0,
	&bcm_cn1,
};

static struct qcom_icc_node * const config_noc_nodes[] = {
	[MASTER_CNOC_CFG] = &qsm_cfg,
	[SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_NORTH] = &qhs_ahb2phy1,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_EVA_CFG] = &qhs_eva_cfg,
	[SLAVE_GFX3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_I2C] = &qhs_i2c,
	[SLAVE_I3C_IBI0_CFG] = &qhs_i3c_ibi0_cfg,
	[SLAVE_I3C_IBI1_CFG] = &qhs_i3c_ibi1_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_CNOC_MSS] = &qhs_mss_cfg,
	[SLAVE_PCIE_CFG] = &qhs_pcie_cfg,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QSPI_0] = &qhs_qspi,
	[SLAVE_QUP_3] = &qhs_qup02,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_QUP_2] = &qhs_qup2,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SDCC_4] = &qhs_sdc4,
	[SLAVE_SPSS_CFG] = &qhs_spss_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB3_0] = &qhs_usb3_0,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_CNOC_MNOC_CFG] = &qss_mnoc_cfg,
	[SLAVE_PCIE_ANOC_CFG] = &qss_pcie_anoc_cfg,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sm8750_config_noc = {
	.nodes = config_noc_nodes,
	.num_nodes = ARRAY_SIZE(config_noc_nodes),
	.bcms = config_noc_bcms,
	.num_bcms = ARRAY_SIZE(config_noc_bcms),
};

static struct qcom_icc_bcm * const cnoc_main_bcms[] = {
	&bcm_cn0,
};

static struct qcom_icc_node * const cnoc_main_nodes[] = {
	[MASTER_GEM_NOC_CNOC] = &qnm_gemnoc_cnoc,
	[MASTER_GEM_NOC_PCIE_SNOC] = &qnm_gemnoc_pcie,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[SLAVE_SOCCP] = &qhs_soccp,
	[SLAVE_TME_CFG] = &qhs_tme_cfg,
	[SLAVE_APPSS] = &qns_apss,
	[SLAVE_CNOC_CFG] = &qss_cfg,
	[SLAVE_DDRSS_CFG] = &qss_ddrss_cfg,
	[SLAVE_BOOT_IMEM] = &qxs_boot_imem,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_BOOT_IMEM_2] = &qxs_modem_boot_imem,
	[SLAVE_SERVICE_CNOC] = &srvc_cnoc_main,
	[SLAVE_PCIE_0] = &xs_pcie,
};

static const struct qcom_icc_desc sm8750_cnoc_main = {
	.nodes = cnoc_main_nodes,
	.num_nodes = ARRAY_SIZE(cnoc_main_nodes),
	.bcms = cnoc_main_bcms,
	.num_bcms = ARRAY_SIZE(cnoc_main_bcms),
};

static struct qcom_icc_bcm * const gem_noc_bcms[] = {
	&bcm_sh0,
	&bcm_sh1,
	&bcm_ubw0,
};

static struct qcom_icc_node * const gem_noc_nodes[] = {
	[MASTER_GPU_TCU] = &alm_gpu_tcu,
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_GFX3D] = &qnm_gpu,
	[MASTER_LPASS_GEM_NOC] = &qnm_lpass_gemnoc,
	[MASTER_MSS_PROC] = &qnm_mdsp,
	[MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[MASTER_MNOC_SF_MEM_NOC] = &qnm_mnoc_sf,
	[MASTER_COMPUTE_NOC] = &qnm_nsp_gemnoc,
	[MASTER_ANOC_PCIE_GEM_NOC] = &qnm_pcie,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[MASTER_UBWC_P] = &qnm_ubwc_p,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_UBWC_P] = &chs_ubwc_p,
	[SLAVE_GEM_NOC_CNOC] = &qns_gem_noc_cnoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MEM_NOC_PCIE_SNOC] = &qns_pcie,
};

static const struct qcom_icc_desc sm8750_gem_noc = {
	.nodes = gem_noc_nodes,
	.num_nodes = ARRAY_SIZE(gem_noc_nodes),
	.bcms = gem_noc_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_bcms),
};

static struct qcom_icc_node * const lpass_ag_noc_nodes[] = {
	[MASTER_LPIAON_NOC] = &qnm_lpiaon_noc,
	[SLAVE_LPASS_GEM_NOC] = &qns_lpass_ag_noc_gemnoc,
};

static const struct qcom_icc_desc sm8750_lpass_ag_noc = {
	.nodes = lpass_ag_noc_nodes,
	.num_nodes = ARRAY_SIZE(lpass_ag_noc_nodes),
};

static struct qcom_icc_bcm * const lpass_lpiaon_noc_bcms[] = {
	&bcm_lp0,
};

static struct qcom_icc_node * const lpass_lpiaon_noc_nodes[] = {
	[MASTER_LPASS_LPINOC] = &qnm_lpass_lpinoc,
	[SLAVE_LPIAON_NOC_LPASS_AG_NOC] = &qns_lpass_aggnoc,
};

static const struct qcom_icc_desc sm8750_lpass_lpiaon_noc = {
	.nodes = lpass_lpiaon_noc_nodes,
	.num_nodes = ARRAY_SIZE(lpass_lpiaon_noc_nodes),
	.bcms = lpass_lpiaon_noc_bcms,
	.num_bcms = ARRAY_SIZE(lpass_lpiaon_noc_bcms),
};

static struct qcom_icc_node * const lpass_lpicx_noc_nodes[] = {
	[MASTER_LPASS_PROC] = &qnm_lpinoc_dsp_qns4m,
	[SLAVE_LPICX_NOC_LPIAON_NOC] = &qns_lpi_aon_noc,
};

static const struct qcom_icc_desc sm8750_lpass_lpicx_noc = {
	.nodes = lpass_lpicx_noc_nodes,
	.num_nodes = ARRAY_SIZE(lpass_lpicx_noc_nodes),
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
};

static struct qcom_icc_node * const mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
};

static const struct qcom_icc_desc sm8750_mc_virt = {
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const mmss_noc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
};

static struct qcom_icc_node * const mmss_noc_nodes[] = {
	[MASTER_CAMNOC_HF] = &qnm_camnoc_hf,
	[MASTER_CAMNOC_NRT_ICP_SF] = &qnm_camnoc_nrt_icp_sf,
	[MASTER_CAMNOC_RT_CDM_SF] = &qnm_camnoc_rt_cdm_sf,
	[MASTER_CAMNOC_SF] = &qnm_camnoc_sf,
	[MASTER_MDP] = &qnm_mdp,
	[MASTER_CDSP_HCP] = &qnm_vapss_hcp,
	[MASTER_VIDEO_CV_PROC] = &qnm_video_cv_cpu,
	[MASTER_VIDEO_EVA] = &qnm_video_eva,
	[MASTER_VIDEO_MVP] = &qnm_video_mvp,
	[MASTER_VIDEO_V_PROC] = &qnm_video_v_cpu,
	[MASTER_CNOC_MNOC_CFG] = &qsm_mnoc_cfg,
	[SLAVE_MNOC_HF_MEM_NOC] = &qns_mem_noc_hf,
	[SLAVE_MNOC_SF_MEM_NOC] = &qns_mem_noc_sf,
	[SLAVE_SERVICE_MNOC] = &srvc_mnoc,
};

static const struct qcom_icc_desc sm8750_mmss_noc = {
	.nodes = mmss_noc_nodes,
	.num_nodes = ARRAY_SIZE(mmss_noc_nodes),
	.bcms = mmss_noc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_bcms),
};

static struct qcom_icc_bcm * const nsp_noc_bcms[] = {
	&bcm_co0,
};

static struct qcom_icc_node * const nsp_noc_nodes[] = {
	[MASTER_CDSP_PROC] = &qnm_nsp,
	[SLAVE_CDSP_MEM_NOC] = &qns_nsp_gemnoc,
};

static const struct qcom_icc_desc sm8750_nsp_noc = {
	.nodes = nsp_noc_nodes,
	.num_nodes = ARRAY_SIZE(nsp_noc_nodes),
	.bcms = nsp_noc_bcms,
	.num_bcms = ARRAY_SIZE(nsp_noc_bcms),
};

static struct qcom_icc_bcm * const pcie_anoc_bcms[] = {
	&bcm_sn4,
};

static struct qcom_icc_node * const pcie_anoc_nodes[] = {
	[MASTER_PCIE_ANOC_CFG] = &qsm_pcie_anoc_cfg,
	[MASTER_PCIE_0] = &xm_pcie3,
	[SLAVE_ANOC_PCIE_GEM_NOC] = &qns_pcie_mem_noc,
	[SLAVE_SERVICE_PCIE_ANOC] = &srvc_pcie_aggre_noc,
};

static const struct qcom_icc_desc sm8750_pcie_anoc = {
	.nodes = pcie_anoc_nodes,
	.num_nodes = ARRAY_SIZE(pcie_anoc_nodes),
	.bcms = pcie_anoc_bcms,
	.num_bcms = ARRAY_SIZE(pcie_anoc_bcms),
};

static struct qcom_icc_bcm * const system_noc_bcms[] = {
	&bcm_sn0,
	&bcm_sn2,
	&bcm_sn3,
};

static struct qcom_icc_node * const system_noc_nodes[] = {
	[MASTER_A1NOC_SNOC] = &qnm_aggre1_noc,
	[MASTER_A2NOC_SNOC] = &qnm_aggre2_noc,
	[SLAVE_SNOC_GEM_NOC_SF] = &qns_gemnoc_sf,
};

static const struct qcom_icc_desc sm8750_system_noc = {
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sm8750-aggre1-noc", .data = &sm8750_aggre1_noc},
	{ .compatible = "qcom,sm8750-aggre2-noc", .data = &sm8750_aggre2_noc},
	{ .compatible = "qcom,sm8750-clk-virt", .data = &sm8750_clk_virt},
	{ .compatible = "qcom,sm8750-config-noc", .data = &sm8750_config_noc},
	{ .compatible = "qcom,sm8750-cnoc-main", .data = &sm8750_cnoc_main},
	{ .compatible = "qcom,sm8750-gem-noc", .data = &sm8750_gem_noc},
	{ .compatible = "qcom,sm8750-lpass-ag-noc", .data = &sm8750_lpass_ag_noc},
	{ .compatible = "qcom,sm8750-lpass-lpiaon-noc", .data = &sm8750_lpass_lpiaon_noc},
	{ .compatible = "qcom,sm8750-lpass-lpicx-noc", .data = &sm8750_lpass_lpicx_noc},
	{ .compatible = "qcom,sm8750-mc-virt", .data = &sm8750_mc_virt},
	{ .compatible = "qcom,sm8750-mmss-noc", .data = &sm8750_mmss_noc},
	{ .compatible = "qcom,sm8750-nsp-noc", .data = &sm8750_nsp_noc},
	{ .compatible = "qcom,sm8750-pcie-anoc", .data = &sm8750_pcie_anoc},
	{ .compatible = "qcom,sm8750-system-noc", .data = &sm8750_system_noc},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-sm8750",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
	},
};

static int __init qnoc_driver_init(void)
{
	return platform_driver_register(&qnoc_driver);
}
core_initcall(qnoc_driver_init);

static void __exit qnoc_driver_exit(void)
{
	platform_driver_unregister(&qnoc_driver);
}
module_exit(qnoc_driver_exit);

MODULE_DESCRIPTION("SM8750 NoC driver");
MODULE_LICENSE("GPL");
