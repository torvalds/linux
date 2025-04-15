// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <dt-bindings/interconnect/qcom,sa8775p-rpmh.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"

static struct qcom_icc_node qxm_qup3;
static struct qcom_icc_node xm_emac_0;
static struct qcom_icc_node xm_emac_1;
static struct qcom_icc_node xm_sdc1;
static struct qcom_icc_node xm_ufs_mem;
static struct qcom_icc_node xm_usb2_2;
static struct qcom_icc_node xm_usb3_0;
static struct qcom_icc_node xm_usb3_1;
static struct qcom_icc_node qns_a1noc_snoc;
static struct qcom_icc_node qhm_qdss_bam;
static struct qcom_icc_node qhm_qup0;
static struct qcom_icc_node qhm_qup1;
static struct qcom_icc_node qhm_qup2;
static struct qcom_icc_node qnm_cnoc_datapath;
static struct qcom_icc_node qxm_crypto_0;
static struct qcom_icc_node qxm_crypto_1;
static struct qcom_icc_node qxm_ipa;
static struct qcom_icc_node xm_qdss_etr_0;
static struct qcom_icc_node xm_qdss_etr_1;
static struct qcom_icc_node xm_ufs_card;
static struct qcom_icc_node qns_a2noc_snoc;
static struct qcom_icc_node qup0_core_master;
static struct qcom_icc_node qup1_core_master;
static struct qcom_icc_node qup2_core_master;
static struct qcom_icc_node qup3_core_master;
static struct qcom_icc_node qup0_core_slave;
static struct qcom_icc_node qup1_core_slave;
static struct qcom_icc_node qup2_core_slave;
static struct qcom_icc_node qup3_core_slave;
static struct qcom_icc_node qnm_gemnoc_cnoc;
static struct qcom_icc_node qnm_gemnoc_pcie;
static struct qcom_icc_node qhs_ahb2phy0;
static struct qcom_icc_node qhs_ahb2phy1;
static struct qcom_icc_node qhs_ahb2phy2;
static struct qcom_icc_node qhs_ahb2phy3;
static struct qcom_icc_node qhs_anoc_throttle_cfg;
static struct qcom_icc_node qhs_aoss;
static struct qcom_icc_node qhs_apss;
static struct qcom_icc_node qhs_boot_rom;
static struct qcom_icc_node qhs_camera_cfg;
static struct qcom_icc_node qhs_camera_nrt_throttle_cfg;
static struct qcom_icc_node qhs_camera_rt_throttle_cfg;
static struct qcom_icc_node qhs_clk_ctl;
static struct qcom_icc_node qhs_compute0_cfg;
static struct qcom_icc_node qhs_compute1_cfg;
static struct qcom_icc_node qhs_cpr_cx;
static struct qcom_icc_node qhs_cpr_mmcx;
static struct qcom_icc_node qhs_cpr_mx;
static struct qcom_icc_node qhs_cpr_nspcx;
static struct qcom_icc_node qhs_crypto0_cfg;
static struct qcom_icc_node qhs_cx_rdpm;
static struct qcom_icc_node qhs_display0_cfg;
static struct qcom_icc_node qhs_display0_rt_throttle_cfg;
static struct qcom_icc_node qhs_display1_cfg;
static struct qcom_icc_node qhs_display1_rt_throttle_cfg;
static struct qcom_icc_node qhs_emac0_cfg;
static struct qcom_icc_node qhs_emac1_cfg;
static struct qcom_icc_node qhs_gp_dsp0_cfg;
static struct qcom_icc_node qhs_gp_dsp1_cfg;
static struct qcom_icc_node qhs_gpdsp0_throttle_cfg;
static struct qcom_icc_node qhs_gpdsp1_throttle_cfg;
static struct qcom_icc_node qhs_gpu_tcu_throttle_cfg;
static struct qcom_icc_node qhs_gpuss_cfg;
static struct qcom_icc_node qhs_hwkm;
static struct qcom_icc_node qhs_imem_cfg;
static struct qcom_icc_node qhs_ipa;
static struct qcom_icc_node qhs_ipc_router;
static struct qcom_icc_node qhs_lpass_cfg;
static struct qcom_icc_node qhs_lpass_throttle_cfg;
static struct qcom_icc_node qhs_mx_rdpm;
static struct qcom_icc_node qhs_mxc_rdpm;
static struct qcom_icc_node qhs_pcie0_cfg;
static struct qcom_icc_node qhs_pcie1_cfg;
static struct qcom_icc_node qhs_pcie_rsc_cfg;
static struct qcom_icc_node qhs_pcie_tcu_throttle_cfg;
static struct qcom_icc_node qhs_pcie_throttle_cfg;
static struct qcom_icc_node qhs_pdm;
static struct qcom_icc_node qhs_pimem_cfg;
static struct qcom_icc_node qhs_pke_wrapper_cfg;
static struct qcom_icc_node qhs_qdss_cfg;
static struct qcom_icc_node qhs_qm_cfg;
static struct qcom_icc_node qhs_qm_mpu_cfg;
static struct qcom_icc_node qhs_qup0;
static struct qcom_icc_node qhs_qup1;
static struct qcom_icc_node qhs_qup2;
static struct qcom_icc_node qhs_qup3;
static struct qcom_icc_node qhs_sail_throttle_cfg;
static struct qcom_icc_node qhs_sdc1;
static struct qcom_icc_node qhs_security;
static struct qcom_icc_node qhs_snoc_throttle_cfg;
static struct qcom_icc_node qhs_tcsr;
static struct qcom_icc_node qhs_tlmm;
static struct qcom_icc_node qhs_tsc_cfg;
static struct qcom_icc_node qhs_ufs_card_cfg;
static struct qcom_icc_node qhs_ufs_mem_cfg;
static struct qcom_icc_node qhs_usb2_0;
static struct qcom_icc_node qhs_usb3_0;
static struct qcom_icc_node qhs_usb3_1;
static struct qcom_icc_node qhs_venus_cfg;
static struct qcom_icc_node qhs_venus_cvp_throttle_cfg;
static struct qcom_icc_node qhs_venus_v_cpu_throttle_cfg;
static struct qcom_icc_node qhs_venus_vcodec_throttle_cfg;
static struct qcom_icc_node qns_ddrss_cfg;
static struct qcom_icc_node qns_gpdsp_noc_cfg;
static struct qcom_icc_node qns_mnoc_hf_cfg;
static struct qcom_icc_node qns_mnoc_sf_cfg;
static struct qcom_icc_node qns_pcie_anoc_cfg;
static struct qcom_icc_node qns_snoc_cfg;
static struct qcom_icc_node qxs_boot_imem;
static struct qcom_icc_node qxs_imem;
static struct qcom_icc_node qxs_pimem;
static struct qcom_icc_node xs_pcie_0;
static struct qcom_icc_node xs_pcie_1;
static struct qcom_icc_node xs_qdss_stm;
static struct qcom_icc_node xs_sys_tcu_cfg;
static struct qcom_icc_node qnm_cnoc_dc_noc;
static struct qcom_icc_node qhs_llcc;
static struct qcom_icc_node qns_gemnoc;
static struct qcom_icc_node alm_gpu_tcu;
static struct qcom_icc_node alm_pcie_tcu;
static struct qcom_icc_node alm_sys_tcu;
static struct qcom_icc_node chm_apps;
static struct qcom_icc_node qnm_cmpnoc0;
static struct qcom_icc_node qnm_cmpnoc1;
static struct qcom_icc_node qnm_gemnoc_cfg;
static struct qcom_icc_node qnm_gpdsp_sail;
static struct qcom_icc_node qnm_gpu;
static struct qcom_icc_node qnm_mnoc_hf;
static struct qcom_icc_node qnm_mnoc_sf;
static struct qcom_icc_node qnm_pcie;
static struct qcom_icc_node qnm_snoc_gc;
static struct qcom_icc_node qnm_snoc_sf;
static struct qcom_icc_node qns_gem_noc_cnoc;
static struct qcom_icc_node qns_llcc;
static struct qcom_icc_node qns_pcie;
static struct qcom_icc_node srvc_even_gemnoc;
static struct qcom_icc_node srvc_odd_gemnoc;
static struct qcom_icc_node srvc_sys_gemnoc;
static struct qcom_icc_node srvc_sys_gemnoc_2;
static struct qcom_icc_node qxm_dsp0;
static struct qcom_icc_node qxm_dsp1;
static struct qcom_icc_node qns_gp_dsp_sail_noc;
static struct qcom_icc_node qhm_config_noc;
static struct qcom_icc_node qxm_lpass_dsp;
static struct qcom_icc_node qhs_lpass_core;
static struct qcom_icc_node qhs_lpass_lpi;
static struct qcom_icc_node qhs_lpass_mpu;
static struct qcom_icc_node qhs_lpass_top;
static struct qcom_icc_node qns_sysnoc;
static struct qcom_icc_node srvc_niu_aml_noc;
static struct qcom_icc_node srvc_niu_lpass_agnoc;
static struct qcom_icc_node llcc_mc;
static struct qcom_icc_node ebi;
static struct qcom_icc_node qnm_camnoc_hf;
static struct qcom_icc_node qnm_camnoc_icp;
static struct qcom_icc_node qnm_camnoc_sf;
static struct qcom_icc_node qnm_mdp0_0;
static struct qcom_icc_node qnm_mdp0_1;
static struct qcom_icc_node qnm_mdp1_0;
static struct qcom_icc_node qnm_mdp1_1;
static struct qcom_icc_node qnm_mnoc_hf_cfg;
static struct qcom_icc_node qnm_mnoc_sf_cfg;
static struct qcom_icc_node qnm_video0;
static struct qcom_icc_node qnm_video1;
static struct qcom_icc_node qnm_video_cvp;
static struct qcom_icc_node qnm_video_v_cpu;
static struct qcom_icc_node qns_mem_noc_hf;
static struct qcom_icc_node qns_mem_noc_sf;
static struct qcom_icc_node srvc_mnoc_hf;
static struct qcom_icc_node srvc_mnoc_sf;
static struct qcom_icc_node qhm_nsp_noc_config;
static struct qcom_icc_node qxm_nsp;
static struct qcom_icc_node qns_hcp;
static struct qcom_icc_node qns_nsp_gemnoc;
static struct qcom_icc_node service_nsp_noc;
static struct qcom_icc_node qhm_nspb_noc_config;
static struct qcom_icc_node qxm_nspb;
static struct qcom_icc_node qns_nspb_gemnoc;
static struct qcom_icc_node qns_nspb_hcp;
static struct qcom_icc_node service_nspb_noc;
static struct qcom_icc_node xm_pcie3_0;
static struct qcom_icc_node xm_pcie3_1;
static struct qcom_icc_node qns_pcie_mem_noc;
static struct qcom_icc_node qhm_gic;
static struct qcom_icc_node qnm_aggre1_noc;
static struct qcom_icc_node qnm_aggre2_noc;
static struct qcom_icc_node qnm_lpass_noc;
static struct qcom_icc_node qnm_snoc_cfg;
static struct qcom_icc_node qxm_pimem;
static struct qcom_icc_node xm_gic;
static struct qcom_icc_node qns_gemnoc_gc;
static struct qcom_icc_node qns_gemnoc_sf;
static struct qcom_icc_node srvc_snoc;

static struct qcom_icc_node qxm_qup3 = {
	.name = "qxm_qup3",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a1noc_snoc },
};

static struct qcom_icc_node xm_emac_0 = {
	.name = "xm_emac_0",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a1noc_snoc },
};

static struct qcom_icc_node xm_emac_1 = {
	.name = "xm_emac_1",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a1noc_snoc },
};

static struct qcom_icc_node xm_sdc1 = {
	.name = "xm_sdc1",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a1noc_snoc },
};

static struct qcom_icc_node xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a1noc_snoc },
};

static struct qcom_icc_node xm_usb2_2 = {
	.name = "xm_usb2_2",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a1noc_snoc },
};

static struct qcom_icc_node xm_usb3_0 = {
	.name = "xm_usb3_0",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a1noc_snoc },
};

static struct qcom_icc_node xm_usb3_1 = {
	.name = "xm_usb3_1",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a1noc_snoc },
};

static struct qcom_icc_node qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a2noc_snoc },
};

static struct qcom_icc_node qhm_qup0 = {
	.name = "qhm_qup0",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a2noc_snoc },
};

static struct qcom_icc_node qhm_qup1 = {
	.name = "qhm_qup1",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a2noc_snoc },
};

static struct qcom_icc_node qhm_qup2 = {
	.name = "qhm_qup2",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a2noc_snoc },
};

static struct qcom_icc_node qnm_cnoc_datapath = {
	.name = "qnm_cnoc_datapath",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a2noc_snoc },
};

static struct qcom_icc_node qxm_crypto_0 = {
	.name = "qxm_crypto_0",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a2noc_snoc },
};

static struct qcom_icc_node qxm_crypto_1 = {
	.name = "qxm_crypto_1",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a2noc_snoc },
};

static struct qcom_icc_node qxm_ipa = {
	.name = "qxm_ipa",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a2noc_snoc },
};

static struct qcom_icc_node xm_qdss_etr_0 = {
	.name = "xm_qdss_etr_0",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a2noc_snoc },
};

static struct qcom_icc_node xm_qdss_etr_1 = {
	.name = "xm_qdss_etr_1",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a2noc_snoc },
};

static struct qcom_icc_node xm_ufs_card = {
	.name = "xm_ufs_card",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_a2noc_snoc },
};

static struct qcom_icc_node qup0_core_master = {
	.name = "qup0_core_master",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qup0_core_slave },
};

static struct qcom_icc_node qup1_core_master = {
	.name = "qup1_core_master",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qup1_core_slave },
};

static struct qcom_icc_node qup2_core_master = {
	.name = "qup2_core_master",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qup2_core_slave },
};

static struct qcom_icc_node qup3_core_master = {
	.name = "qup3_core_master",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qup3_core_slave },
};

static struct qcom_icc_node qnm_gemnoc_cnoc = {
	.name = "qnm_gemnoc_cnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 82,
	.link_nodes = (struct qcom_icc_node *[]) { &qhs_ahb2phy0, &qhs_ahb2phy1,
		      &qhs_ahb2phy2, &qhs_ahb2phy3,
		      &qhs_anoc_throttle_cfg, &qhs_aoss,
		      &qhs_apss, &qhs_boot_rom,
		      &qhs_camera_cfg, &qhs_camera_nrt_throttle_cfg,
		      &qhs_camera_rt_throttle_cfg, &qhs_clk_ctl,
		      &qhs_compute0_cfg, &qhs_compute1_cfg,
		      &qhs_cpr_cx, &qhs_cpr_mmcx,
		      &qhs_cpr_mx, &qhs_cpr_nspcx,
		      &qhs_crypto0_cfg, &qhs_cx_rdpm,
		      &qhs_display0_cfg, &qhs_display0_rt_throttle_cfg,
		      &qhs_display1_cfg, &qhs_display1_rt_throttle_cfg,
		      &qhs_emac0_cfg, &qhs_emac1_cfg,
		      &qhs_gp_dsp0_cfg, &qhs_gp_dsp1_cfg,
		      &qhs_gpdsp0_throttle_cfg, &qhs_gpdsp1_throttle_cfg,
		      &qhs_gpu_tcu_throttle_cfg, &qhs_gpuss_cfg,
		      &qhs_hwkm, &qhs_imem_cfg,
		      &qhs_ipa, &qhs_ipc_router,
		      &qhs_lpass_cfg, &qhs_lpass_throttle_cfg,
		      &qhs_mx_rdpm, &qhs_mxc_rdpm,
		      &qhs_pcie0_cfg, &qhs_pcie1_cfg,
		      &qhs_pcie_rsc_cfg, &qhs_pcie_tcu_throttle_cfg,
		      &qhs_pcie_throttle_cfg, &qhs_pdm,
		      &qhs_pimem_cfg, &qhs_pke_wrapper_cfg,
		      &qhs_qdss_cfg, &qhs_qm_cfg,
		      &qhs_qm_mpu_cfg, &qhs_qup0,
		      &qhs_qup1, &qhs_qup2,
		      &qhs_qup3, &qhs_sail_throttle_cfg,
		      &qhs_sdc1, &qhs_security,
		      &qhs_snoc_throttle_cfg, &qhs_tcsr,
		      &qhs_tlmm, &qhs_tsc_cfg,
		      &qhs_ufs_card_cfg, &qhs_ufs_mem_cfg,
		      &qhs_usb2_0, &qhs_usb3_0,
		      &qhs_usb3_1, &qhs_venus_cfg,
		      &qhs_venus_cvp_throttle_cfg, &qhs_venus_v_cpu_throttle_cfg,
		      &qhs_venus_vcodec_throttle_cfg, &qns_ddrss_cfg,
		      &qns_gpdsp_noc_cfg, &qns_mnoc_hf_cfg,
		      &qns_mnoc_sf_cfg, &qns_pcie_anoc_cfg,
		      &qns_snoc_cfg, &qxs_boot_imem,
		      &qxs_imem, &qxs_pimem,
		      &xs_qdss_stm, &xs_sys_tcu_cfg },
};

static struct qcom_icc_node qnm_gemnoc_pcie = {
	.name = "qnm_gemnoc_pcie",
	.channels = 1,
	.buswidth = 16,
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &xs_pcie_0, &xs_pcie_1 },
};

static struct qcom_icc_node qnm_cnoc_dc_noc = {
	.name = "qnm_cnoc_dc_noc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &qhs_llcc, &qns_gemnoc },
};

static struct qcom_icc_node alm_gpu_tcu = {
	.name = "alm_gpu_tcu",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gem_noc_cnoc, &qns_llcc },
};

static struct qcom_icc_node alm_pcie_tcu = {
	.name = "alm_pcie_tcu",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gem_noc_cnoc, &qns_llcc },
};

static struct qcom_icc_node alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gem_noc_cnoc, &qns_llcc },
};

static struct qcom_icc_node chm_apps = {
	.name = "chm_apps",
	.channels = 4,
	.buswidth = 32,
	.num_links = 3,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gem_noc_cnoc, &qns_llcc,
		      &qns_pcie },
};

static struct qcom_icc_node qnm_cmpnoc0 = {
	.name = "qnm_cmpnoc0",
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gem_noc_cnoc, &qns_llcc },
};

static struct qcom_icc_node qnm_cmpnoc1 = {
	.name = "qnm_cmpnoc1",
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gem_noc_cnoc, &qns_llcc },
};

static struct qcom_icc_node qnm_gemnoc_cfg = {
	.name = "qnm_gemnoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 4,
	.link_nodes = (struct qcom_icc_node *[]) { &srvc_even_gemnoc, &srvc_odd_gemnoc,
		      &srvc_sys_gemnoc, &srvc_sys_gemnoc_2 },
};

static struct qcom_icc_node qnm_gpdsp_sail = {
	.name = "qnm_gpdsp_sail",
	.channels = 1,
	.buswidth = 16,
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gem_noc_cnoc, &qns_llcc },
};

static struct qcom_icc_node qnm_gpu = {
	.name = "qnm_gpu",
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gem_noc_cnoc, &qns_llcc },
};

static struct qcom_icc_node qnm_mnoc_hf = {
	.name = "qnm_mnoc_hf",
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_llcc, &qns_pcie },
};

static struct qcom_icc_node qnm_mnoc_sf = {
	.name = "qnm_mnoc_sf",
	.channels = 2,
	.buswidth = 32,
	.num_links = 3,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gem_noc_cnoc, &qns_llcc,
		      &qns_pcie },
};

static struct qcom_icc_node qnm_pcie = {
	.name = "qnm_pcie",
	.channels = 1,
	.buswidth = 32,
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gem_noc_cnoc, &qns_llcc },
};

static struct qcom_icc_node qnm_snoc_gc = {
	.name = "qnm_snoc_gc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_llcc },
};

static struct qcom_icc_node qnm_snoc_sf = {
	.name = "qnm_snoc_sf",
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gem_noc_cnoc, &qns_llcc,
		      &qns_pcie },
};

static struct qcom_icc_node qxm_dsp0 = {
	.name = "qxm_dsp0",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gp_dsp_sail_noc },
};

static struct qcom_icc_node qxm_dsp1 = {
	.name = "qxm_dsp1",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gp_dsp_sail_noc },
};

static struct qcom_icc_node qhm_config_noc = {
	.name = "qhm_config_noc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 6,
	.link_nodes = (struct qcom_icc_node *[]) { &qhs_lpass_core, &qhs_lpass_lpi,
		      &qhs_lpass_mpu, &qhs_lpass_top,
		      &srvc_niu_aml_noc, &srvc_niu_lpass_agnoc },
};

static struct qcom_icc_node qxm_lpass_dsp = {
	.name = "qxm_lpass_dsp",
	.channels = 1,
	.buswidth = 8,
	.num_links = 4,
	.link_nodes = (struct qcom_icc_node *[]) { &qhs_lpass_top, &qns_sysnoc,
		      &srvc_niu_aml_noc, &srvc_niu_lpass_agnoc },
};

static struct qcom_icc_node llcc_mc = {
	.name = "llcc_mc",
	.channels = 8,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &ebi },
};

static struct qcom_icc_node qnm_camnoc_hf = {
	.name = "qnm_camnoc_hf",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_hf },
};

static struct qcom_icc_node qnm_camnoc_icp = {
	.name = "qnm_camnoc_icp",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_camnoc_sf = {
	.name = "qnm_camnoc_sf",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_mdp0_0 = {
	.name = "qnm_mdp0_0",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_hf },
};

static struct qcom_icc_node qnm_mdp0_1 = {
	.name = "qnm_mdp0_1",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_hf },
};

static struct qcom_icc_node qnm_mdp1_0 = {
	.name = "qnm_mdp1_0",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_hf },
};

static struct qcom_icc_node qnm_mdp1_1 = {
	.name = "qnm_mdp1_1",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_hf },
};

static struct qcom_icc_node qnm_mnoc_hf_cfg = {
	.name = "qnm_mnoc_hf_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &srvc_mnoc_hf },
};

static struct qcom_icc_node qnm_mnoc_sf_cfg = {
	.name = "qnm_mnoc_sf_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &srvc_mnoc_sf },
};

static struct qcom_icc_node qnm_video0 = {
	.name = "qnm_video0",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_video1 = {
	.name = "qnm_video1",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_video_cvp = {
	.name = "qnm_video_cvp",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_video_v_cpu = {
	.name = "qnm_video_v_cpu",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_mem_noc_sf },
};

static struct qcom_icc_node qhm_nsp_noc_config = {
	.name = "qhm_nsp_noc_config",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &service_nsp_noc },
};

static struct qcom_icc_node qxm_nsp = {
	.name = "qxm_nsp",
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_hcp, &qns_nsp_gemnoc },
};

static struct qcom_icc_node qhm_nspb_noc_config = {
	.name = "qhm_nspb_noc_config",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &service_nspb_noc },
};

static struct qcom_icc_node qxm_nspb = {
	.name = "qxm_nspb",
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_nspb_hcp, &qns_nspb_gemnoc },
};

static struct qcom_icc_node xm_pcie3_0 = {
	.name = "xm_pcie3_0",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_pcie_mem_noc },
};

static struct qcom_icc_node xm_pcie3_1 = {
	.name = "xm_pcie3_1",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_pcie_mem_noc },
};

static struct qcom_icc_node qhm_gic = {
	.name = "qhm_gic",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gemnoc_sf },
};

static struct qcom_icc_node qnm_aggre1_noc = {
	.name = "qnm_aggre1_noc",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gemnoc_sf },
};

static struct qcom_icc_node qnm_aggre2_noc = {
	.name = "qnm_aggre2_noc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gemnoc_sf },
};

static struct qcom_icc_node qnm_lpass_noc = {
	.name = "qnm_lpass_noc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gemnoc_sf },
};

static struct qcom_icc_node qnm_snoc_cfg = {
	.name = "qnm_snoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &srvc_snoc },
};

static struct qcom_icc_node qxm_pimem = {
	.name = "qxm_pimem",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gemnoc_gc },
};

static struct qcom_icc_node xm_gic = {
	.name = "xm_gic",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qns_gemnoc_gc },
};

static struct qcom_icc_node qns_a1noc_snoc = {
	.name = "qns_a1noc_snoc",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_aggre1_noc },
};

static struct qcom_icc_node qns_a2noc_snoc = {
	.name = "qns_a2noc_snoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_aggre2_noc },
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

static struct qcom_icc_node qup3_core_slave = {
	.name = "qup3_core_slave",
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

static struct qcom_icc_node qhs_ahb2phy2 = {
	.name = "qhs_ahb2phy2",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ahb2phy3 = {
	.name = "qhs_ahb2phy3",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_anoc_throttle_cfg = {
	.name = "qhs_anoc_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_aoss = {
	.name = "qhs_aoss",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_apss = {
	.name = "qhs_apss",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qhs_boot_rom = {
	.name = "qhs_boot_rom",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_camera_cfg = {
	.name = "qhs_camera_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_camera_nrt_throttle_cfg = {
	.name = "qhs_camera_nrt_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_camera_rt_throttle_cfg = {
	.name = "qhs_camera_rt_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_compute0_cfg = {
	.name = "qhs_compute0_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qhm_nsp_noc_config },
};

static struct qcom_icc_node qhs_compute1_cfg = {
	.name = "qhs_compute1_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qhm_nspb_noc_config },
};

static struct qcom_icc_node qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_cpr_mmcx = {
	.name = "qhs_cpr_mmcx",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_cpr_mx = {
	.name = "qhs_cpr_mx",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_cpr_nspcx = {
	.name = "qhs_cpr_nspcx",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_cx_rdpm = {
	.name = "qhs_cx_rdpm",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_display0_cfg = {
	.name = "qhs_display0_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_display0_rt_throttle_cfg = {
	.name = "qhs_display0_rt_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_display1_cfg = {
	.name = "qhs_display1_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_display1_rt_throttle_cfg = {
	.name = "qhs_display1_rt_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_emac0_cfg = {
	.name = "qhs_emac0_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_emac1_cfg = {
	.name = "qhs_emac1_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gp_dsp0_cfg = {
	.name = "qhs_gp_dsp0_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gp_dsp1_cfg = {
	.name = "qhs_gp_dsp1_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gpdsp0_throttle_cfg = {
	.name = "qhs_gpdsp0_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gpdsp1_throttle_cfg = {
	.name = "qhs_gpdsp1_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gpu_tcu_throttle_cfg = {
	.name = "qhs_gpu_tcu_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qhs_hwkm = {
	.name = "qhs_hwkm",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
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

static struct qcom_icc_node qhs_lpass_cfg = {
	.name = "qhs_lpass_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qhm_config_noc },
};

static struct qcom_icc_node qhs_lpass_throttle_cfg = {
	.name = "qhs_lpass_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_mx_rdpm = {
	.name = "qhs_mx_rdpm",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_mxc_rdpm = {
	.name = "qhs_mxc_rdpm",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie0_cfg = {
	.name = "qhs_pcie0_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie1_cfg = {
	.name = "qhs_pcie1_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie_rsc_cfg = {
	.name = "qhs_pcie_rsc_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie_tcu_throttle_cfg = {
	.name = "qhs_pcie_tcu_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie_throttle_cfg = {
	.name = "qhs_pcie_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pdm = {
	.name = "qhs_pdm",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pke_wrapper_cfg = {
	.name = "qhs_pke_wrapper_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qm_cfg = {
	.name = "qhs_qm_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qm_mpu_cfg = {
	.name = "qhs_qm_mpu_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qup0 = {
	.name = "qhs_qup0",
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

static struct qcom_icc_node qhs_qup3 = {
	.name = "qhs_qup3",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_sail_throttle_cfg = {
	.name = "qhs_sail_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_sdc1 = {
	.name = "qhs_sdc1",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_security = {
	.name = "qhs_security",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_snoc_throttle_cfg = {
	.name = "qhs_snoc_throttle_cfg",
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

static struct qcom_icc_node qhs_tsc_cfg = {
	.name = "qhs_tsc_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ufs_card_cfg = {
	.name = "qhs_ufs_card_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb2_0 = {
	.name = "qhs_usb2_0",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb3_0 = {
	.name = "qhs_usb3_0",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb3_1 = {
	.name = "qhs_usb3_1",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_venus_cvp_throttle_cfg = {
	.name = "qhs_venus_cvp_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_venus_v_cpu_throttle_cfg = {
	.name = "qhs_venus_v_cpu_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_venus_vcodec_throttle_cfg = {
	.name = "qhs_venus_vcodec_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_ddrss_cfg = {
	.name = "qns_ddrss_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_cnoc_dc_noc },
};

static struct qcom_icc_node qns_gpdsp_noc_cfg = {
	.name = "qns_gpdsp_noc_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_mnoc_hf_cfg = {
	.name = "qns_mnoc_hf_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_mnoc_hf_cfg },
};

static struct qcom_icc_node qns_mnoc_sf_cfg = {
	.name = "qns_mnoc_sf_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_mnoc_sf_cfg },
};

static struct qcom_icc_node qns_pcie_anoc_cfg = {
	.name = "qns_pcie_anoc_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_snoc_cfg = {
	.name = "qns_snoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_snoc_cfg },
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

static struct qcom_icc_node qxs_pimem = {
	.name = "qxs_pimem",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node xs_pcie_0 = {
	.name = "xs_pcie_0",
	.channels = 1,
	.buswidth = 16,
};

static struct qcom_icc_node xs_pcie_1 = {
	.name = "xs_pcie_1",
	.channels = 1,
	.buswidth = 32,
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

static struct qcom_icc_node qhs_llcc = {
	.name = "qhs_llcc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_gemnoc = {
	.name = "qns_gemnoc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_gemnoc_cfg },
};

static struct qcom_icc_node qns_gem_noc_cnoc = {
	.name = "qns_gem_noc_cnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_gemnoc_cnoc },
};

static struct qcom_icc_node qns_llcc = {
	.name = "qns_llcc",
	.channels = 6,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &llcc_mc },
};

static struct qcom_icc_node qns_pcie = {
	.name = "qns_pcie",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_gemnoc_pcie },
};

static struct qcom_icc_node srvc_even_gemnoc = {
	.name = "srvc_even_gemnoc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_odd_gemnoc = {
	.name = "srvc_odd_gemnoc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_sys_gemnoc = {
	.name = "srvc_sys_gemnoc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_sys_gemnoc_2 = {
	.name = "srvc_sys_gemnoc_2",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_gp_dsp_sail_noc = {
	.name = "qns_gp_dsp_sail_noc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_gpdsp_sail },
};

static struct qcom_icc_node qhs_lpass_core = {
	.name = "qhs_lpass_core",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_lpass_lpi = {
	.name = "qhs_lpass_lpi",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_lpass_mpu = {
	.name = "qhs_lpass_mpu",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_lpass_top = {
	.name = "qhs_lpass_top",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_sysnoc = {
	.name = "qns_sysnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_lpass_noc },
};

static struct qcom_icc_node srvc_niu_aml_noc = {
	.name = "srvc_niu_aml_noc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_niu_lpass_agnoc = {
	.name = "srvc_niu_lpass_agnoc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.channels = 8,
	.buswidth = 4,
};

static struct qcom_icc_node qns_mem_noc_hf = {
	.name = "qns_mem_noc_hf",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_mnoc_hf },
};

static struct qcom_icc_node qns_mem_noc_sf = {
	.name = "qns_mem_noc_sf",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_mnoc_sf },
};

static struct qcom_icc_node srvc_mnoc_hf = {
	.name = "srvc_mnoc_hf",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_mnoc_sf = {
	.name = "srvc_mnoc_sf",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_hcp = {
	.name = "qns_hcp",
	.channels = 2,
	.buswidth = 32,
};

static struct qcom_icc_node qns_nsp_gemnoc = {
	.name = "qns_nsp_gemnoc",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_cmpnoc0 },
};

static struct qcom_icc_node service_nsp_noc = {
	.name = "service_nsp_noc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_nspb_gemnoc = {
	.name = "qns_nspb_gemnoc",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_cmpnoc1 },
};

static struct qcom_icc_node qns_nspb_hcp = {
	.name = "qns_nspb_hcp",
	.channels = 2,
	.buswidth = 32,
};

static struct qcom_icc_node service_nspb_noc = {
	.name = "service_nspb_noc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_pcie_mem_noc = {
	.name = "qns_pcie_mem_noc",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_pcie },
};

static struct qcom_icc_node qns_gemnoc_gc = {
	.name = "qns_gemnoc_gc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_snoc_gc },
};

static struct qcom_icc_node qns_gemnoc_sf = {
	.name = "qns_gemnoc_sf",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = (struct qcom_icc_node *[]) { &qnm_snoc_sf },
};

static struct qcom_icc_node srvc_snoc = {
	.name = "srvc_snoc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.enable_mask = 0x8,
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.num_nodes = 2,
	.nodes = { &qxm_crypto_0, &qxm_crypto_1 },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.keepalive = true,
	.num_nodes = 2,
	.nodes = { &qnm_gemnoc_cnoc, &qnm_gemnoc_pcie },
};

static struct qcom_icc_bcm bcm_cn1 = {
	.name = "CN1",
	.num_nodes = 76,
	.nodes = { &qhs_ahb2phy0, &qhs_ahb2phy1,
		   &qhs_ahb2phy2, &qhs_ahb2phy3,
		   &qhs_anoc_throttle_cfg, &qhs_aoss,
		   &qhs_apss, &qhs_boot_rom,
		   &qhs_camera_cfg, &qhs_camera_nrt_throttle_cfg,
		   &qhs_camera_rt_throttle_cfg, &qhs_clk_ctl,
		   &qhs_compute0_cfg, &qhs_compute1_cfg,
		   &qhs_cpr_cx, &qhs_cpr_mmcx,
		   &qhs_cpr_mx, &qhs_cpr_nspcx,
		   &qhs_crypto0_cfg, &qhs_cx_rdpm,
		   &qhs_display0_cfg, &qhs_display0_rt_throttle_cfg,
		   &qhs_display1_cfg, &qhs_display1_rt_throttle_cfg,
		   &qhs_emac0_cfg, &qhs_emac1_cfg,
		   &qhs_gp_dsp0_cfg, &qhs_gp_dsp1_cfg,
		   &qhs_gpdsp0_throttle_cfg, &qhs_gpdsp1_throttle_cfg,
		   &qhs_gpu_tcu_throttle_cfg, &qhs_gpuss_cfg,
		   &qhs_hwkm, &qhs_imem_cfg,
		   &qhs_ipa, &qhs_ipc_router,
		   &qhs_lpass_cfg, &qhs_lpass_throttle_cfg,
		   &qhs_mx_rdpm, &qhs_mxc_rdpm,
		   &qhs_pcie0_cfg, &qhs_pcie1_cfg,
		   &qhs_pcie_rsc_cfg, &qhs_pcie_tcu_throttle_cfg,
		   &qhs_pcie_throttle_cfg, &qhs_pdm,
		   &qhs_pimem_cfg, &qhs_pke_wrapper_cfg,
		   &qhs_qdss_cfg, &qhs_qm_cfg,
		   &qhs_qm_mpu_cfg, &qhs_sail_throttle_cfg,
		   &qhs_sdc1, &qhs_security,
		   &qhs_snoc_throttle_cfg, &qhs_tcsr,
		   &qhs_tlmm, &qhs_tsc_cfg,
		   &qhs_ufs_card_cfg, &qhs_ufs_mem_cfg,
		   &qhs_usb2_0, &qhs_usb3_0,
		   &qhs_usb3_1, &qhs_venus_cfg,
		   &qhs_venus_cvp_throttle_cfg, &qhs_venus_v_cpu_throttle_cfg,
		   &qhs_venus_vcodec_throttle_cfg, &qns_ddrss_cfg,
		   &qns_gpdsp_noc_cfg, &qns_mnoc_hf_cfg,
		   &qns_mnoc_sf_cfg, &qns_pcie_anoc_cfg,
		   &qns_snoc_cfg, &qxs_boot_imem,
		   &qxs_imem, &xs_sys_tcu_cfg },
};

static struct qcom_icc_bcm bcm_cn2 = {
	.name = "CN2",
	.num_nodes = 4,
	.nodes = { &qhs_qup0, &qhs_qup1,
		   &qhs_qup2, &qhs_qup3 },
};

static struct qcom_icc_bcm bcm_cn3 = {
	.name = "CN3",
	.num_nodes = 2,
	.nodes = { &xs_pcie_0, &xs_pcie_1 },
};

static struct qcom_icc_bcm bcm_gna0 = {
	.name = "GNA0",
	.num_nodes = 1,
	.nodes = { &qxm_dsp0 },
};

static struct qcom_icc_bcm bcm_gnb0 = {
	.name = "GNB0",
	.num_nodes = 1,
	.nodes = { &qxm_dsp1 },
};

static struct qcom_icc_bcm bcm_mc0 = {
	.name = "MC0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_mm0 = {
	.name = "MM0",
	.keepalive = true,
	.num_nodes = 5,
	.nodes = { &qnm_camnoc_hf, &qnm_mdp0_0,
		   &qnm_mdp0_1, &qnm_mdp1_0,
		   &qns_mem_noc_hf },
};

static struct qcom_icc_bcm bcm_mm1 = {
	.name = "MM1",
	.num_nodes = 7,
	.nodes = { &qnm_camnoc_icp, &qnm_camnoc_sf,
		   &qnm_video0, &qnm_video1,
		   &qnm_video_cvp, &qnm_video_v_cpu,
		   &qns_mem_noc_sf },
};

static struct qcom_icc_bcm bcm_nsa0 = {
	.name = "NSA0",
	.num_nodes = 2,
	.nodes = { &qns_hcp, &qns_nsp_gemnoc },
};

static struct qcom_icc_bcm bcm_nsa1 = {
	.name = "NSA1",
	.num_nodes = 1,
	.nodes = { &qxm_nsp },
};

static struct qcom_icc_bcm bcm_nsb0 = {
	.name = "NSB0",
	.num_nodes = 2,
	.nodes = { &qns_nspb_gemnoc, &qns_nspb_hcp },
};

static struct qcom_icc_bcm bcm_nsb1 = {
	.name = "NSB1",
	.num_nodes = 1,
	.nodes = { &qxm_nspb },
};

static struct qcom_icc_bcm bcm_pci0 = {
	.name = "PCI0",
	.num_nodes = 1,
	.nodes = { &qns_pcie_mem_noc },
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.vote_scale = 1,
	.num_nodes = 1,
	.nodes = { &qup0_core_slave },
};

static struct qcom_icc_bcm bcm_qup1 = {
	.name = "QUP1",
	.vote_scale = 1,
	.num_nodes = 1,
	.nodes = { &qup1_core_slave },
};

static struct qcom_icc_bcm bcm_qup2 = {
	.name = "QUP2",
	.vote_scale = 1,
	.num_nodes = 2,
	.nodes = { &qup2_core_slave, &qup3_core_slave },
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_sh2 = {
	.name = "SH2",
	.num_nodes = 1,
	.nodes = { &chm_apps },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.num_nodes = 1,
	.nodes = { &qns_gemnoc_gc },
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.num_nodes = 1,
	.nodes = { &qxs_pimem },
};

static struct qcom_icc_bcm bcm_sn3 = {
	.name = "SN3",
	.num_nodes = 2,
	.nodes = { &qns_a1noc_snoc, &qnm_aggre1_noc },
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.num_nodes = 2,
	.nodes = { &qns_a2noc_snoc, &qnm_aggre2_noc },
};

static struct qcom_icc_bcm bcm_sn9 = {
	.name = "SN9",
	.num_nodes = 2,
	.nodes = { &qns_sysnoc, &qnm_lpass_noc },
};

static struct qcom_icc_bcm bcm_sn10 = {
	.name = "SN10",
	.num_nodes = 1,
	.nodes = { &xs_qdss_stm },
};

static struct qcom_icc_bcm * const aggre1_noc_bcms[] = {
	&bcm_sn3,
};

static struct qcom_icc_node * const aggre1_noc_nodes[] = {
	[MASTER_QUP_3] = &qxm_qup3,
	[MASTER_EMAC] = &xm_emac_0,
	[MASTER_EMAC_1] = &xm_emac_1,
	[MASTER_SDC] = &xm_sdc1,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_USB2] = &xm_usb2_2,
	[MASTER_USB3_0] = &xm_usb3_0,
	[MASTER_USB3_1] = &xm_usb3_1,
	[SLAVE_A1NOC_SNOC] = &qns_a1noc_snoc,
};

static const struct qcom_icc_desc sa8775p_aggre1_noc = {
	.nodes = aggre1_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre1_noc_nodes),
	.bcms = aggre1_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_noc_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const aggre2_noc_bcms[] = {
	&bcm_ce0,
	&bcm_sn4,
};

static struct qcom_icc_node * const aggre2_noc_nodes[] = {
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_QUP_2] = &qhm_qup2,
	[MASTER_CNOC_A2NOC] = &qnm_cnoc_datapath,
	[MASTER_CRYPTO_CORE0] = &qxm_crypto_0,
	[MASTER_CRYPTO_CORE1] = &qxm_crypto_1,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_QDSS_ETR_0] = &xm_qdss_etr_0,
	[MASTER_QDSS_ETR_1] = &xm_qdss_etr_1,
	[MASTER_UFS_CARD] = &xm_ufs_card,
	[SLAVE_A2NOC_SNOC] = &qns_a2noc_snoc,
};

static const struct qcom_icc_desc sa8775p_aggre2_noc = {
	.nodes = aggre2_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre2_noc_nodes),
	.bcms = aggre2_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_noc_bcms),
	.alloc_dyn_id = true,
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
	[MASTER_QUP_CORE_3] = &qup3_core_master,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_QUP_CORE_1] = &qup1_core_slave,
	[SLAVE_QUP_CORE_2] = &qup2_core_slave,
	[SLAVE_QUP_CORE_3] = &qup3_core_slave,
};

static const struct qcom_icc_desc sa8775p_clk_virt = {
	.nodes = clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(clk_virt_nodes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const config_noc_bcms[] = {
	&bcm_cn0,
	&bcm_cn1,
	&bcm_cn2,
	&bcm_cn3,
	&bcm_sn2,
	&bcm_sn10,
};

static struct qcom_icc_node * const config_noc_nodes[] = {
	[MASTER_GEM_NOC_CNOC] = &qnm_gemnoc_cnoc,
	[MASTER_GEM_NOC_PCIE_SNOC] = &qnm_gemnoc_pcie,
	[SLAVE_AHB2PHY_0] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_1] = &qhs_ahb2phy1,
	[SLAVE_AHB2PHY_2] = &qhs_ahb2phy2,
	[SLAVE_AHB2PHY_3] = &qhs_ahb2phy3,
	[SLAVE_ANOC_THROTTLE_CFG] = &qhs_anoc_throttle_cfg,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_APPSS] = &qhs_apss,
	[SLAVE_BOOT_ROM] = &qhs_boot_rom,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CAMERA_NRT_THROTTLE_CFG] = &qhs_camera_nrt_throttle_cfg,
	[SLAVE_CAMERA_RT_THROTTLE_CFG] = &qhs_camera_rt_throttle_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CDSP_CFG] = &qhs_compute0_cfg,
	[SLAVE_CDSP1_CFG] = &qhs_compute1_cfg,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MMCX_CFG] = &qhs_cpr_mmcx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CPR_NSPCX] = &qhs_cpr_nspcx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_CX_RDPM] = &qhs_cx_rdpm,
	[SLAVE_DISPLAY_CFG] = &qhs_display0_cfg,
	[SLAVE_DISPLAY_RT_THROTTLE_CFG] = &qhs_display0_rt_throttle_cfg,
	[SLAVE_DISPLAY1_CFG] = &qhs_display1_cfg,
	[SLAVE_DISPLAY1_RT_THROTTLE_CFG] = &qhs_display1_rt_throttle_cfg,
	[SLAVE_EMAC_CFG] = &qhs_emac0_cfg,
	[SLAVE_EMAC1_CFG] = &qhs_emac1_cfg,
	[SLAVE_GP_DSP0_CFG] = &qhs_gp_dsp0_cfg,
	[SLAVE_GP_DSP1_CFG] = &qhs_gp_dsp1_cfg,
	[SLAVE_GPDSP0_THROTTLE_CFG] = &qhs_gpdsp0_throttle_cfg,
	[SLAVE_GPDSP1_THROTTLE_CFG] = &qhs_gpdsp1_throttle_cfg,
	[SLAVE_GPU_TCU_THROTTLE_CFG] = &qhs_gpu_tcu_throttle_cfg,
	[SLAVE_GFX3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_HWKM] = &qhs_hwkm,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[SLAVE_LPASS] = &qhs_lpass_cfg,
	[SLAVE_LPASS_THROTTLE_CFG] = &qhs_lpass_throttle_cfg,
	[SLAVE_MX_RDPM] = &qhs_mx_rdpm,
	[SLAVE_MXC_RDPM] = &qhs_mxc_rdpm,
	[SLAVE_PCIE_0_CFG] = &qhs_pcie0_cfg,
	[SLAVE_PCIE_1_CFG] = &qhs_pcie1_cfg,
	[SLAVE_PCIE_RSC_CFG] = &qhs_pcie_rsc_cfg,
	[SLAVE_PCIE_TCU_THROTTLE_CFG] = &qhs_pcie_tcu_throttle_cfg,
	[SLAVE_PCIE_THROTTLE_CFG] = &qhs_pcie_throttle_cfg,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PKA_WRAPPER_CFG] = &qhs_pke_wrapper_cfg,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QM_CFG] = &qhs_qm_cfg,
	[SLAVE_QM_MPU_CFG] = &qhs_qm_mpu_cfg,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_QUP_2] = &qhs_qup2,
	[SLAVE_QUP_3] = &qhs_qup3,
	[SLAVE_SAIL_THROTTLE_CFG] = &qhs_sail_throttle_cfg,
	[SLAVE_SDC1] = &qhs_sdc1,
	[SLAVE_SECURITY] = &qhs_security,
	[SLAVE_SNOC_THROTTLE_CFG] = &qhs_snoc_throttle_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_TSC_CFG] = &qhs_tsc_cfg,
	[SLAVE_UFS_CARD_CFG] = &qhs_ufs_card_cfg,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB2] = &qhs_usb2_0,
	[SLAVE_USB3_0] = &qhs_usb3_0,
	[SLAVE_USB3_1] = &qhs_usb3_1,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VENUS_CVP_THROTTLE_CFG] = &qhs_venus_cvp_throttle_cfg,
	[SLAVE_VENUS_V_CPU_THROTTLE_CFG] = &qhs_venus_v_cpu_throttle_cfg,
	[SLAVE_VENUS_VCODEC_THROTTLE_CFG] = &qhs_venus_vcodec_throttle_cfg,
	[SLAVE_DDRSS_CFG] = &qns_ddrss_cfg,
	[SLAVE_GPDSP_NOC_CFG] = &qns_gpdsp_noc_cfg,
	[SLAVE_CNOC_MNOC_HF_CFG] = &qns_mnoc_hf_cfg,
	[SLAVE_CNOC_MNOC_SF_CFG] = &qns_mnoc_sf_cfg,
	[SLAVE_PCIE_ANOC_CFG] = &qns_pcie_anoc_cfg,
	[SLAVE_SNOC_CFG] = &qns_snoc_cfg,
	[SLAVE_BOOT_IMEM] = &qxs_boot_imem,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_PCIE_0] = &xs_pcie_0,
	[SLAVE_PCIE_1] = &xs_pcie_1,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sa8775p_config_noc = {
	.nodes = config_noc_nodes,
	.num_nodes = ARRAY_SIZE(config_noc_nodes),
	.bcms = config_noc_bcms,
	.num_bcms = ARRAY_SIZE(config_noc_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const dc_noc_bcms[] = {
};

static struct qcom_icc_node * const dc_noc_nodes[] = {
	[MASTER_CNOC_DC_NOC] = &qnm_cnoc_dc_noc,
	[SLAVE_LLCC_CFG] = &qhs_llcc,
	[SLAVE_GEM_NOC_CFG] = &qns_gemnoc,
};

static const struct qcom_icc_desc sa8775p_dc_noc = {
	.nodes = dc_noc_nodes,
	.num_nodes = ARRAY_SIZE(dc_noc_nodes),
	.bcms = dc_noc_bcms,
	.num_bcms = ARRAY_SIZE(dc_noc_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const gem_noc_bcms[] = {
	&bcm_sh0,
	&bcm_sh2,
};

static struct qcom_icc_node * const gem_noc_nodes[] = {
	[MASTER_GPU_TCU] = &alm_gpu_tcu,
	[MASTER_PCIE_TCU] = &alm_pcie_tcu,
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_COMPUTE_NOC] = &qnm_cmpnoc0,
	[MASTER_COMPUTE_NOC_1] = &qnm_cmpnoc1,
	[MASTER_GEM_NOC_CFG] = &qnm_gemnoc_cfg,
	[MASTER_GPDSP_SAIL] = &qnm_gpdsp_sail,
	[MASTER_GFX3D] = &qnm_gpu,
	[MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[MASTER_MNOC_SF_MEM_NOC] = &qnm_mnoc_sf,
	[MASTER_ANOC_PCIE_GEM_NOC] = &qnm_pcie,
	[MASTER_SNOC_GC_MEM_NOC] = &qnm_snoc_gc,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[SLAVE_GEM_NOC_CNOC] = &qns_gem_noc_cnoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_GEM_NOC_PCIE_CNOC] = &qns_pcie,
	[SLAVE_SERVICE_GEM_NOC_1] = &srvc_even_gemnoc,
	[SLAVE_SERVICE_GEM_NOC_2] = &srvc_odd_gemnoc,
	[SLAVE_SERVICE_GEM_NOC] = &srvc_sys_gemnoc,
	[SLAVE_SERVICE_GEM_NOC2] = &srvc_sys_gemnoc_2,
};

static const struct qcom_icc_desc sa8775p_gem_noc = {
	.nodes = gem_noc_nodes,
	.num_nodes = ARRAY_SIZE(gem_noc_nodes),
	.bcms = gem_noc_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const gpdsp_anoc_bcms[] = {
	&bcm_gna0,
	&bcm_gnb0,
};

static struct qcom_icc_node * const gpdsp_anoc_nodes[] = {
	[MASTER_DSP0] = &qxm_dsp0,
	[MASTER_DSP1] = &qxm_dsp1,
	[SLAVE_GP_DSP_SAIL_NOC] = &qns_gp_dsp_sail_noc,
};

static const struct qcom_icc_desc sa8775p_gpdsp_anoc = {
	.nodes = gpdsp_anoc_nodes,
	.num_nodes = ARRAY_SIZE(gpdsp_anoc_nodes),
	.bcms = gpdsp_anoc_bcms,
	.num_bcms = ARRAY_SIZE(gpdsp_anoc_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const lpass_ag_noc_bcms[] = {
	&bcm_sn9,
};

static struct qcom_icc_node * const lpass_ag_noc_nodes[] = {
	[MASTER_CNOC_LPASS_AG_NOC] = &qhm_config_noc,
	[MASTER_LPASS_PROC] = &qxm_lpass_dsp,
	[SLAVE_LPASS_CORE_CFG] = &qhs_lpass_core,
	[SLAVE_LPASS_LPI_CFG] = &qhs_lpass_lpi,
	[SLAVE_LPASS_MPU_CFG] = &qhs_lpass_mpu,
	[SLAVE_LPASS_TOP_CFG] = &qhs_lpass_top,
	[SLAVE_LPASS_SNOC] = &qns_sysnoc,
	[SLAVE_SERVICES_LPASS_AML_NOC] = &srvc_niu_aml_noc,
	[SLAVE_SERVICE_LPASS_AG_NOC] = &srvc_niu_lpass_agnoc,
};

static const struct qcom_icc_desc sa8775p_lpass_ag_noc = {
	.nodes = lpass_ag_noc_nodes,
	.num_nodes = ARRAY_SIZE(lpass_ag_noc_nodes),
	.bcms = lpass_ag_noc_bcms,
	.num_bcms = ARRAY_SIZE(lpass_ag_noc_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
};

static struct qcom_icc_node * const mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
};

static const struct qcom_icc_desc sa8775p_mc_virt = {
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const mmss_noc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
};

static struct qcom_icc_node * const mmss_noc_nodes[] = {
	[MASTER_CAMNOC_HF] = &qnm_camnoc_hf,
	[MASTER_CAMNOC_ICP] = &qnm_camnoc_icp,
	[MASTER_CAMNOC_SF] = &qnm_camnoc_sf,
	[MASTER_MDP0] = &qnm_mdp0_0,
	[MASTER_MDP1] = &qnm_mdp0_1,
	[MASTER_MDP_CORE1_0] = &qnm_mdp1_0,
	[MASTER_MDP_CORE1_1] = &qnm_mdp1_1,
	[MASTER_CNOC_MNOC_HF_CFG] = &qnm_mnoc_hf_cfg,
	[MASTER_CNOC_MNOC_SF_CFG] = &qnm_mnoc_sf_cfg,
	[MASTER_VIDEO_P0] = &qnm_video0,
	[MASTER_VIDEO_P1] = &qnm_video1,
	[MASTER_VIDEO_PROC] = &qnm_video_cvp,
	[MASTER_VIDEO_V_PROC] = &qnm_video_v_cpu,
	[SLAVE_MNOC_HF_MEM_NOC] = &qns_mem_noc_hf,
	[SLAVE_MNOC_SF_MEM_NOC] = &qns_mem_noc_sf,
	[SLAVE_SERVICE_MNOC_HF] = &srvc_mnoc_hf,
	[SLAVE_SERVICE_MNOC_SF] = &srvc_mnoc_sf,
};

static const struct qcom_icc_desc sa8775p_mmss_noc = {
	.nodes = mmss_noc_nodes,
	.num_nodes = ARRAY_SIZE(mmss_noc_nodes),
	.bcms = mmss_noc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const nspa_noc_bcms[] = {
	&bcm_nsa0,
	&bcm_nsa1,
};

static struct qcom_icc_node * const nspa_noc_nodes[] = {
	[MASTER_CDSP_NOC_CFG] = &qhm_nsp_noc_config,
	[MASTER_CDSP_PROC] = &qxm_nsp,
	[SLAVE_HCP_A] = &qns_hcp,
	[SLAVE_CDSP_MEM_NOC] = &qns_nsp_gemnoc,
	[SLAVE_SERVICE_NSP_NOC] = &service_nsp_noc,
};

static const struct qcom_icc_desc sa8775p_nspa_noc = {
	.nodes = nspa_noc_nodes,
	.num_nodes = ARRAY_SIZE(nspa_noc_nodes),
	.bcms = nspa_noc_bcms,
	.num_bcms = ARRAY_SIZE(nspa_noc_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const nspb_noc_bcms[] = {
	&bcm_nsb0,
	&bcm_nsb1,
};

static struct qcom_icc_node * const nspb_noc_nodes[] = {
	[MASTER_CDSPB_NOC_CFG] = &qhm_nspb_noc_config,
	[MASTER_CDSP_PROC_B] = &qxm_nspb,
	[SLAVE_CDSPB_MEM_NOC] = &qns_nspb_gemnoc,
	[SLAVE_HCP_B] = &qns_nspb_hcp,
	[SLAVE_SERVICE_NSPB_NOC] = &service_nspb_noc,
};

static const struct qcom_icc_desc sa8775p_nspb_noc = {
	.nodes = nspb_noc_nodes,
	.num_nodes = ARRAY_SIZE(nspb_noc_nodes),
	.bcms = nspb_noc_bcms,
	.num_bcms = ARRAY_SIZE(nspb_noc_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const pcie_anoc_bcms[] = {
	&bcm_pci0,
};

static struct qcom_icc_node * const pcie_anoc_nodes[] = {
	[MASTER_PCIE_0] = &xm_pcie3_0,
	[MASTER_PCIE_1] = &xm_pcie3_1,
	[SLAVE_ANOC_PCIE_GEM_NOC] = &qns_pcie_mem_noc,
};

static const struct qcom_icc_desc sa8775p_pcie_anoc = {
	.nodes = pcie_anoc_nodes,
	.num_nodes = ARRAY_SIZE(pcie_anoc_nodes),
	.bcms = pcie_anoc_bcms,
	.num_bcms = ARRAY_SIZE(pcie_anoc_bcms),
	.alloc_dyn_id = true,
};

static struct qcom_icc_bcm * const system_noc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn3,
	&bcm_sn4,
	&bcm_sn9,
};

static struct qcom_icc_node * const system_noc_nodes[] = {
	[MASTER_GIC_AHB] = &qhm_gic,
	[MASTER_A1NOC_SNOC] = &qnm_aggre1_noc,
	[MASTER_A2NOC_SNOC] = &qnm_aggre2_noc,
	[MASTER_LPASS_ANOC] = &qnm_lpass_noc,
	[MASTER_SNOC_CFG] = &qnm_snoc_cfg,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_SNOC_GEM_NOC_GC] = &qns_gemnoc_gc,
	[SLAVE_SNOC_GEM_NOC_SF] = &qns_gemnoc_sf,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
};

static const struct qcom_icc_desc sa8775p_system_noc = {
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
	.alloc_dyn_id = true,
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sa8775p-aggre1-noc", .data = &sa8775p_aggre1_noc, },
	{ .compatible = "qcom,sa8775p-aggre2-noc", .data = &sa8775p_aggre2_noc, },
	{ .compatible = "qcom,sa8775p-clk-virt", .data = &sa8775p_clk_virt, },
	{ .compatible = "qcom,sa8775p-config-noc", .data = &sa8775p_config_noc, },
	{ .compatible = "qcom,sa8775p-dc-noc", .data = &sa8775p_dc_noc, },
	{ .compatible = "qcom,sa8775p-gem-noc", .data = &sa8775p_gem_noc, },
	{ .compatible = "qcom,sa8775p-gpdsp-anoc", .data = &sa8775p_gpdsp_anoc, },
	{ .compatible = "qcom,sa8775p-lpass-ag-noc", .data = &sa8775p_lpass_ag_noc, },
	{ .compatible = "qcom,sa8775p-mc-virt", .data = &sa8775p_mc_virt, },
	{ .compatible = "qcom,sa8775p-mmss-noc", .data = &sa8775p_mmss_noc, },
	{ .compatible = "qcom,sa8775p-nspa-noc", .data = &sa8775p_nspa_noc, },
	{ .compatible = "qcom,sa8775p-nspb-noc", .data = &sa8775p_nspb_noc, },
	{ .compatible = "qcom,sa8775p-pcie-anoc", .data = &sa8775p_pcie_anoc, },
	{ .compatible = "qcom,sa8775p-system-noc", .data = &sa8775p_system_noc, },
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-sa8775p",
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

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. SA8775P NoC driver");
MODULE_LICENSE("GPL");
