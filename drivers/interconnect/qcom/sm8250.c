// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <dt-bindings/interconnect/qcom,sm8250.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"

static struct qcom_icc_node qhm_a1noc_cfg;
static struct qcom_icc_node qhm_qspi;
static struct qcom_icc_node qhm_qup1;
static struct qcom_icc_node qhm_qup2;
static struct qcom_icc_node qhm_tsif;
static struct qcom_icc_node xm_pcie3_modem;
static struct qcom_icc_node xm_sdc4;
static struct qcom_icc_node xm_ufs_mem;
static struct qcom_icc_node xm_usb3_0;
static struct qcom_icc_node xm_usb3_1;
static struct qcom_icc_node qhm_a2noc_cfg;
static struct qcom_icc_node qhm_qdss_bam;
static struct qcom_icc_node qhm_qup0;
static struct qcom_icc_node qnm_cnoc;
static struct qcom_icc_node qxm_crypto;
static struct qcom_icc_node qxm_ipa;
static struct qcom_icc_node xm_pcie3_0;
static struct qcom_icc_node xm_pcie3_1;
static struct qcom_icc_node xm_qdss_etr;
static struct qcom_icc_node xm_sdc2;
static struct qcom_icc_node xm_ufs_card;
static struct qcom_icc_node qnm_npu;
static struct qcom_icc_node qnm_snoc;
static struct qcom_icc_node xm_qdss_dap;
static struct qcom_icc_node qhm_cnoc_dc_noc;
static struct qcom_icc_node alm_gpu_tcu;
static struct qcom_icc_node alm_sys_tcu;
static struct qcom_icc_node chm_apps;
static struct qcom_icc_node qhm_gemnoc_cfg;
static struct qcom_icc_node qnm_cmpnoc;
static struct qcom_icc_node qnm_gpu;
static struct qcom_icc_node qnm_mnoc_hf;
static struct qcom_icc_node qnm_mnoc_sf;
static struct qcom_icc_node qnm_pcie;
static struct qcom_icc_node qnm_snoc_gc;
static struct qcom_icc_node qnm_snoc_sf;
static struct qcom_icc_node llcc_mc;
static struct qcom_icc_node qhm_mnoc_cfg;
static struct qcom_icc_node qnm_camnoc_hf;
static struct qcom_icc_node qnm_camnoc_icp;
static struct qcom_icc_node qnm_camnoc_sf;
static struct qcom_icc_node qnm_video0;
static struct qcom_icc_node qnm_video1;
static struct qcom_icc_node qnm_video_cvp;
static struct qcom_icc_node qxm_mdp0;
static struct qcom_icc_node qxm_mdp1;
static struct qcom_icc_node qxm_rot;
static struct qcom_icc_node amm_npu_sys;
static struct qcom_icc_node amm_npu_sys_cdp_w;
static struct qcom_icc_node qhm_cfg;
static struct qcom_icc_node qhm_snoc_cfg;
static struct qcom_icc_node qnm_aggre1_noc;
static struct qcom_icc_node qnm_aggre2_noc;
static struct qcom_icc_node qnm_gemnoc;
static struct qcom_icc_node qnm_gemnoc_pcie;
static struct qcom_icc_node qxm_pimem;
static struct qcom_icc_node xm_gic;
static struct qcom_icc_node qns_a1noc_snoc;
static struct qcom_icc_node qns_pcie_modem_mem_noc;
static struct qcom_icc_node srvc_aggre1_noc;
static struct qcom_icc_node qns_a2noc_snoc;
static struct qcom_icc_node qns_pcie_mem_noc;
static struct qcom_icc_node srvc_aggre2_noc;
static struct qcom_icc_node qns_cdsp_mem_noc;
static struct qcom_icc_node qhs_a1_noc_cfg;
static struct qcom_icc_node qhs_a2_noc_cfg;
static struct qcom_icc_node qhs_ahb2phy0;
static struct qcom_icc_node qhs_ahb2phy1;
static struct qcom_icc_node qhs_aoss;
static struct qcom_icc_node qhs_camera_cfg;
static struct qcom_icc_node qhs_clk_ctl;
static struct qcom_icc_node qhs_compute_dsp;
static struct qcom_icc_node qhs_cpr_cx;
static struct qcom_icc_node qhs_cpr_mmcx;
static struct qcom_icc_node qhs_cpr_mx;
static struct qcom_icc_node qhs_crypto0_cfg;
static struct qcom_icc_node qhs_cx_rdpm;
static struct qcom_icc_node qhs_dcc_cfg;
static struct qcom_icc_node qhs_ddrss_cfg;
static struct qcom_icc_node qhs_display_cfg;
static struct qcom_icc_node qhs_gpuss_cfg;
static struct qcom_icc_node qhs_imem_cfg;
static struct qcom_icc_node qhs_ipa;
static struct qcom_icc_node qhs_ipc_router;
static struct qcom_icc_node qhs_lpass_cfg;
static struct qcom_icc_node qhs_mnoc_cfg;
static struct qcom_icc_node qhs_npu_cfg;
static struct qcom_icc_node qhs_pcie0_cfg;
static struct qcom_icc_node qhs_pcie1_cfg;
static struct qcom_icc_node qhs_pcie_modem_cfg;
static struct qcom_icc_node qhs_pdm;
static struct qcom_icc_node qhs_pimem_cfg;
static struct qcom_icc_node qhs_prng;
static struct qcom_icc_node qhs_qdss_cfg;
static struct qcom_icc_node qhs_qspi;
static struct qcom_icc_node qhs_qup0;
static struct qcom_icc_node qhs_qup1;
static struct qcom_icc_node qhs_qup2;
static struct qcom_icc_node qhs_sdc2;
static struct qcom_icc_node qhs_sdc4;
static struct qcom_icc_node qhs_snoc_cfg;
static struct qcom_icc_node qhs_tcsr;
static struct qcom_icc_node qhs_tlmm0;
static struct qcom_icc_node qhs_tlmm1;
static struct qcom_icc_node qhs_tlmm2;
static struct qcom_icc_node qhs_tsif;
static struct qcom_icc_node qhs_ufs_card_cfg;
static struct qcom_icc_node qhs_ufs_mem_cfg;
static struct qcom_icc_node qhs_usb3_0;
static struct qcom_icc_node qhs_usb3_1;
static struct qcom_icc_node qhs_venus_cfg;
static struct qcom_icc_node qhs_vsense_ctrl_cfg;
static struct qcom_icc_node qns_cnoc_a2noc;
static struct qcom_icc_node srvc_cnoc;
static struct qcom_icc_node qhs_llcc;
static struct qcom_icc_node qhs_memnoc;
static struct qcom_icc_node qns_gem_noc_snoc;
static struct qcom_icc_node qns_llcc;
static struct qcom_icc_node qns_sys_pcie;
static struct qcom_icc_node srvc_even_gemnoc;
static struct qcom_icc_node srvc_odd_gemnoc;
static struct qcom_icc_node srvc_sys_gemnoc;
static struct qcom_icc_node ebi;
static struct qcom_icc_node qns_mem_noc_hf;
static struct qcom_icc_node qns_mem_noc_sf;
static struct qcom_icc_node srvc_mnoc;
static struct qcom_icc_node qhs_cal_dp0;
static struct qcom_icc_node qhs_cal_dp1;
static struct qcom_icc_node qhs_cp;
static struct qcom_icc_node qhs_dma_bwmon;
static struct qcom_icc_node qhs_dpm;
static struct qcom_icc_node qhs_isense;
static struct qcom_icc_node qhs_llm;
static struct qcom_icc_node qhs_tcm;
static struct qcom_icc_node qns_npu_sys;
static struct qcom_icc_node srvc_noc;
static struct qcom_icc_node qhs_apss;
static struct qcom_icc_node qns_cnoc;
static struct qcom_icc_node qns_gemnoc_gc;
static struct qcom_icc_node qns_gemnoc_sf;
static struct qcom_icc_node qxs_imem;
static struct qcom_icc_node qxs_pimem;
static struct qcom_icc_node srvc_snoc;
static struct qcom_icc_node xs_pcie_0;
static struct qcom_icc_node xs_pcie_1;
static struct qcom_icc_node xs_pcie_modem;
static struct qcom_icc_node xs_qdss_stm;
static struct qcom_icc_node xs_sys_tcu_cfg;
static struct qcom_icc_node qup0_core_master;
static struct qcom_icc_node qup1_core_master;
static struct qcom_icc_node qup2_core_master;
static struct qcom_icc_node qup0_core_slave;
static struct qcom_icc_node qup1_core_slave;
static struct qcom_icc_node qup2_core_slave;

static struct qcom_icc_node qhm_a1noc_cfg = {
	.name = "qhm_a1noc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &srvc_aggre1_noc },
};

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

static struct qcom_icc_node qhm_qup2 = {
	.name = "qhm_qup2",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node qhm_tsif = {
	.name = "qhm_tsif",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node xm_pcie3_modem = {
	.name = "xm_pcie3_modem",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_pcie_modem_mem_noc },
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
	.buswidth = 8,
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

static struct qcom_icc_node xm_usb3_1 = {
	.name = "xm_usb3_1",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_node qhm_a2noc_cfg = {
	.name = "qhm_a2noc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &srvc_aggre2_noc },
};

static struct qcom_icc_node qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_node qhm_qup0 = {
	.name = "qhm_qup0",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_node qnm_cnoc = {
	.name = "qnm_cnoc",
	.channels = 1,
	.buswidth = 8,
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

static struct qcom_icc_node xm_pcie3_0 = {
	.name = "xm_pcie3_0",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_pcie_mem_noc },
};

static struct qcom_icc_node xm_pcie3_1 = {
	.name = "xm_pcie3_1",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_pcie_mem_noc },
};

static struct qcom_icc_node xm_qdss_etr = {
	.name = "xm_qdss_etr",
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

static struct qcom_icc_node xm_ufs_card = {
	.name = "xm_ufs_card",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_node qnm_npu = {
	.name = "qnm_npu",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_cdsp_mem_noc },
};

static struct qcom_icc_node qnm_snoc = {
	.name = "qnm_snoc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 49,
	.link_nodes = { &qhs_compute_dsp,
			&qhs_camera_cfg,
			&qhs_tlmm1,
			&qhs_tlmm0,
			&qhs_sdc4,
			&qhs_tlmm2,
			&qhs_sdc2,
			&qhs_mnoc_cfg,
			&qhs_ufs_mem_cfg,
			&qhs_snoc_cfg,
			&qhs_pdm,
			&qhs_cx_rdpm,
			&qhs_pcie1_cfg,
			&qhs_a2_noc_cfg,
			&qhs_qdss_cfg,
			&qhs_display_cfg,
			&qhs_pcie_modem_cfg,
			&qhs_tcsr,
			&qhs_dcc_cfg,
			&qhs_ddrss_cfg,
			&qhs_ipc_router,
			&qhs_pcie0_cfg,
			&qhs_cpr_mmcx,
			&qhs_npu_cfg,
			&qhs_ahb2phy0,
			&qhs_ahb2phy1,
			&qhs_gpuss_cfg,
			&qhs_venus_cfg,
			&qhs_tsif,
			&qhs_ipa,
			&qhs_imem_cfg,
			&qhs_usb3_0,
			&srvc_cnoc,
			&qhs_ufs_card_cfg,
			&qhs_usb3_1,
			&qhs_lpass_cfg,
			&qhs_cpr_cx,
			&qhs_a1_noc_cfg,
			&qhs_aoss,
			&qhs_prng,
			&qhs_vsense_ctrl_cfg,
			&qhs_qspi,
			&qhs_crypto0_cfg,
			&qhs_pimem_cfg,
			&qhs_cpr_mx,
			&qhs_qup0,
			&qhs_qup1,
			&qhs_qup2,
			&qhs_clk_ctl },
};

static struct qcom_icc_node xm_qdss_dap = {
	.name = "xm_qdss_dap",
	.channels = 1,
	.buswidth = 8,
	.num_links = 50,
	.link_nodes = { &qhs_compute_dsp,
			&qhs_camera_cfg,
			&qhs_tlmm1,
			&qhs_tlmm0,
			&qhs_sdc4,
			&qhs_tlmm2,
			&qhs_sdc2,
			&qhs_mnoc_cfg,
			&qhs_ufs_mem_cfg,
			&qhs_snoc_cfg,
			&qhs_pdm,
			&qhs_cx_rdpm,
			&qhs_pcie1_cfg,
			&qhs_a2_noc_cfg,
			&qhs_qdss_cfg,
			&qhs_display_cfg,
			&qhs_pcie_modem_cfg,
			&qhs_tcsr,
			&qhs_dcc_cfg,
			&qhs_ddrss_cfg,
			&qhs_ipc_router,
			&qns_cnoc_a2noc,
			&qhs_pcie0_cfg,
			&qhs_cpr_mmcx,
			&qhs_npu_cfg,
			&qhs_ahb2phy0,
			&qhs_ahb2phy1,
			&qhs_gpuss_cfg,
			&qhs_venus_cfg,
			&qhs_tsif,
			&qhs_ipa,
			&qhs_imem_cfg,
			&qhs_usb3_0,
			&srvc_cnoc,
			&qhs_ufs_card_cfg,
			&qhs_usb3_1,
			&qhs_lpass_cfg,
			&qhs_cpr_cx,
			&qhs_a1_noc_cfg,
			&qhs_aoss,
			&qhs_prng,
			&qhs_vsense_ctrl_cfg,
			&qhs_qspi,
			&qhs_crypto0_cfg,
			&qhs_pimem_cfg,
			&qhs_cpr_mx,
			&qhs_qup0,
			&qhs_qup1,
			&qhs_qup2,
			&qhs_clk_ctl },
};

static struct qcom_icc_node qhm_cnoc_dc_noc = {
	.name = "qhm_cnoc_dc_noc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.link_nodes = { &qhs_memnoc,
			&qhs_llcc },
};

static struct qcom_icc_node alm_gpu_tcu = {
	.name = "alm_gpu_tcu",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = { &qns_llcc,
			&qns_gem_noc_snoc },
};

static struct qcom_icc_node alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = { &qns_llcc,
			&qns_gem_noc_snoc },
};

static struct qcom_icc_node chm_apps = {
	.name = "chm_apps",
	.channels = 2,
	.buswidth = 32,
	.num_links = 3,
	.link_nodes = { &qns_llcc,
			&qns_gem_noc_snoc,
			&qns_sys_pcie },
};

static struct qcom_icc_node qhm_gemnoc_cfg = {
	.name = "qhm_gemnoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 3,
	.link_nodes = { &srvc_odd_gemnoc,
			&srvc_even_gemnoc,
			&srvc_sys_gemnoc },
};

static struct qcom_icc_node qnm_cmpnoc = {
	.name = "qnm_cmpnoc",
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.link_nodes = { &qns_llcc,
			&qns_gem_noc_snoc },
};

static struct qcom_icc_node qnm_gpu = {
	.name = "qnm_gpu",
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.link_nodes = { &qns_llcc,
			&qns_gem_noc_snoc },
};

static struct qcom_icc_node qnm_mnoc_hf = {
	.name = "qnm_mnoc_hf",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_llcc },
};

static struct qcom_icc_node qnm_mnoc_sf = {
	.name = "qnm_mnoc_sf",
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.link_nodes = { &qns_llcc,
			&qns_gem_noc_snoc },
};

static struct qcom_icc_node qnm_pcie = {
	.name = "qnm_pcie",
	.channels = 1,
	.buswidth = 16,
	.num_links = 2,
	.link_nodes = { &qns_llcc,
			&qns_gem_noc_snoc },
};

static struct qcom_icc_node qnm_snoc_gc = {
	.name = "qnm_snoc_gc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_llcc },
};

static struct qcom_icc_node qnm_snoc_sf = {
	.name = "qnm_snoc_sf",
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.link_nodes = { &qns_llcc,
			&qns_gem_noc_snoc,
			&qns_sys_pcie },
};

static struct qcom_icc_node llcc_mc = {
	.name = "llcc_mc",
	.channels = 4,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &ebi },
};

static struct qcom_icc_node qhm_mnoc_cfg = {
	.name = "qhm_mnoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &srvc_mnoc },
};

static struct qcom_icc_node qnm_camnoc_hf = {
	.name = "qnm_camnoc_hf",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_node qnm_camnoc_icp = {
	.name = "qnm_camnoc_icp",
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

static struct qcom_icc_node qnm_video0 = {
	.name = "qnm_video0",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_video1 = {
	.name = "qnm_video1",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qnm_video_cvp = {
	.name = "qnm_video_cvp",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qxm_mdp0 = {
	.name = "qxm_mdp0",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_node qxm_mdp1 = {
	.name = "qxm_mdp1",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_node qxm_rot = {
	.name = "qxm_rot",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node amm_npu_sys = {
	.name = "amm_npu_sys",
	.channels = 4,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_npu_sys },
};

static struct qcom_icc_node amm_npu_sys_cdp_w = {
	.name = "amm_npu_sys_cdp_w",
	.channels = 2,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qns_npu_sys },
};

static struct qcom_icc_node qhm_cfg = {
	.name = "qhm_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 9,
	.link_nodes = { &srvc_noc,
			&qhs_isense,
			&qhs_llm,
			&qhs_dma_bwmon,
			&qhs_cp,
			&qhs_tcm,
			&qhs_cal_dp0,
			&qhs_cal_dp1,
			&qhs_dpm },
};

static struct qcom_icc_node qhm_snoc_cfg = {
	.name = "qhm_snoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &srvc_snoc },
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

static struct qcom_icc_node qnm_gemnoc = {
	.name = "qnm_gemnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 6,
	.link_nodes = { &qxs_pimem,
			&qxs_imem,
			&qhs_apss,
			&qns_cnoc,
			&xs_sys_tcu_cfg,
			&xs_qdss_stm },
};

static struct qcom_icc_node qnm_gemnoc_pcie = {
	.name = "qnm_gemnoc_pcie",
	.channels = 1,
	.buswidth = 8,
	.num_links = 3,
	.link_nodes = { &xs_pcie_modem,
			&xs_pcie_0,
			&xs_pcie_1 },
};

static struct qcom_icc_node qxm_pimem = {
	.name = "qxm_pimem",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_gemnoc_gc },
};

static struct qcom_icc_node xm_gic = {
	.name = "xm_gic",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qns_gemnoc_gc },
};

static struct qcom_icc_node qns_a1noc_snoc = {
	.name = "qns_a1noc_snoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_aggre1_noc },
};

static struct qcom_icc_node qns_pcie_modem_mem_noc = {
	.name = "qns_pcie_modem_mem_noc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_pcie },
};

static struct qcom_icc_node srvc_aggre1_noc = {
	.name = "srvc_aggre1_noc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_a2noc_snoc = {
	.name = "qns_a2noc_snoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_aggre2_noc },
};

static struct qcom_icc_node qns_pcie_mem_noc = {
	.name = "qns_pcie_mem_noc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_pcie },
};

static struct qcom_icc_node srvc_aggre2_noc = {
	.name = "srvc_aggre2_noc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_cdsp_mem_noc = {
	.name = "qns_cdsp_mem_noc",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qnm_cmpnoc },
};

static struct qcom_icc_node qhs_a1_noc_cfg = {
	.name = "qhs_a1_noc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qhm_a1noc_cfg },
};

static struct qcom_icc_node qhs_a2_noc_cfg = {
	.name = "qhs_a2_noc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qhm_a2noc_cfg },
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

static struct qcom_icc_node qhs_aoss = {
	.name = "qhs_aoss",
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

static struct qcom_icc_node qhs_compute_dsp = {
	.name = "qhs_compute_dsp",
	.channels = 1,
	.buswidth = 4,
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

static struct qcom_icc_node qhs_dcc_cfg = {
	.name = "qhs_dcc_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ddrss_cfg = {
	.name = "qhs_ddrss_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qhm_cnoc_dc_noc },
};

static struct qcom_icc_node qhs_display_cfg = {
	.name = "qhs_display_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.channels = 1,
	.buswidth = 8,
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
};

static struct qcom_icc_node qhs_mnoc_cfg = {
	.name = "qhs_mnoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qhm_mnoc_cfg },
};

static struct qcom_icc_node qhs_npu_cfg = {
	.name = "qhs_npu_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qhm_cfg },
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

static struct qcom_icc_node qhs_pcie_modem_cfg = {
	.name = "qhs_pcie_modem_cfg",
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

static struct qcom_icc_node qhs_snoc_cfg = {
	.name = "qhs_snoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qhm_snoc_cfg },
};

static struct qcom_icc_node qhs_tcsr = {
	.name = "qhs_tcsr",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_tlmm0 = {
	.name = "qhs_tlmm0",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_tlmm1 = {
	.name = "qhs_tlmm1",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_tlmm2 = {
	.name = "qhs_tlmm2",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_tsif = {
	.name = "qhs_tsif",
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

static struct qcom_icc_node qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_cnoc_a2noc = {
	.name = "qns_cnoc_a2noc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qnm_cnoc },
};

static struct qcom_icc_node srvc_cnoc = {
	.name = "srvc_cnoc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_llcc = {
	.name = "qhs_llcc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_memnoc = {
	.name = "qhs_memnoc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qhm_gemnoc_cfg },
};

static struct qcom_icc_node qns_gem_noc_snoc = {
	.name = "qns_gem_noc_snoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_gemnoc },
};

static struct qcom_icc_node qns_llcc = {
	.name = "qns_llcc",
	.channels = 4,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &llcc_mc },
};

static struct qcom_icc_node qns_sys_pcie = {
	.name = "qns_sys_pcie",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qnm_gemnoc_pcie },
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

static struct qcom_icc_node qhs_cal_dp0 = {
	.name = "qhs_cal_dp0",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_cal_dp1 = {
	.name = "qhs_cal_dp1",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_cp = {
	.name = "qhs_cp",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_dma_bwmon = {
	.name = "qhs_dma_bwmon",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_dpm = {
	.name = "qhs_dpm",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_isense = {
	.name = "qhs_isense",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_llm = {
	.name = "qhs_llm",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_tcm = {
	.name = "qhs_tcm",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_npu_sys = {
	.name = "qns_npu_sys",
	.channels = 2,
	.buswidth = 32,
};

static struct qcom_icc_node srvc_noc = {
	.name = "srvc_noc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_apss = {
	.name = "qhs_apss",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qns_cnoc = {
	.name = "qns_cnoc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qnm_snoc },
};

static struct qcom_icc_node qns_gemnoc_gc = {
	.name = "qns_gemnoc_gc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qnm_snoc_gc },
};

static struct qcom_icc_node qns_gemnoc_sf = {
	.name = "qns_gemnoc_sf",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_snoc_sf },
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

static struct qcom_icc_node srvc_snoc = {
	.name = "srvc_snoc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node xs_pcie_0 = {
	.name = "xs_pcie_0",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node xs_pcie_1 = {
	.name = "xs_pcie_1",
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node xs_pcie_modem = {
	.name = "xs_pcie_modem",
	.channels = 1,
	.buswidth = 8,
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

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.enable_mask = BIT(3),
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_mc0 = {
	.name = "MC0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_mm0 = {
	.name = "MM0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_mm1 = {
	.name = "MM1",
	.keepalive = false,
	.num_nodes = 3,
	.nodes = { &qnm_camnoc_hf, &qxm_mdp0, &qxm_mdp1 },
};

static struct qcom_icc_bcm bcm_sh2 = {
	.name = "SH2",
	.keepalive = false,
	.num_nodes = 2,
	.nodes = { &alm_gpu_tcu, &alm_sys_tcu },
};

static struct qcom_icc_bcm bcm_mm2 = {
	.name = "MM2",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.keepalive = false,
	.num_nodes = 3,
	.nodes = { &qup0_core_master, &qup1_core_master, &qup2_core_master },
};

static struct qcom_icc_bcm bcm_sh3 = {
	.name = "SH3",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qnm_cmpnoc },
};

static struct qcom_icc_bcm bcm_mm3 = {
	.name = "MM3",
	.keepalive = false,
	.num_nodes = 5,
	.nodes = { &qnm_camnoc_icp, &qnm_camnoc_sf, &qnm_video0, &qnm_video1, &qnm_video_cvp },
};

static struct qcom_icc_bcm bcm_sh4 = {
	.name = "SH4",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &chm_apps },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_bcm bcm_co0 = {
	.name = "CO0",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qns_cdsp_mem_noc },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.keepalive = true,
	.num_nodes = 52,
	.nodes = { &qnm_snoc,
		   &xm_qdss_dap,
		   &qhs_a1_noc_cfg,
		   &qhs_a2_noc_cfg,
		   &qhs_ahb2phy0,
		   &qhs_ahb2phy1,
		   &qhs_aoss,
		   &qhs_camera_cfg,
		   &qhs_clk_ctl,
		   &qhs_compute_dsp,
		   &qhs_cpr_cx,
		   &qhs_cpr_mmcx,
		   &qhs_cpr_mx,
		   &qhs_crypto0_cfg,
		   &qhs_cx_rdpm,
		   &qhs_dcc_cfg,
		   &qhs_ddrss_cfg,
		   &qhs_display_cfg,
		   &qhs_gpuss_cfg,
		   &qhs_imem_cfg,
		   &qhs_ipa,
		   &qhs_ipc_router,
		   &qhs_lpass_cfg,
		   &qhs_mnoc_cfg,
		   &qhs_npu_cfg,
		   &qhs_pcie0_cfg,
		   &qhs_pcie1_cfg,
		   &qhs_pcie_modem_cfg,
		   &qhs_pdm,
		   &qhs_pimem_cfg,
		   &qhs_prng,
		   &qhs_qdss_cfg,
		   &qhs_qspi,
		   &qhs_qup0,
		   &qhs_qup1,
		   &qhs_qup2,
		   &qhs_sdc2,
		   &qhs_sdc4,
		   &qhs_snoc_cfg,
		   &qhs_tcsr,
		   &qhs_tlmm0,
		   &qhs_tlmm1,
		   &qhs_tlmm2,
		   &qhs_tsif,
		   &qhs_ufs_card_cfg,
		   &qhs_ufs_mem_cfg,
		   &qhs_usb3_0,
		   &qhs_usb3_1,
		   &qhs_venus_cfg,
		   &qhs_vsense_ctrl_cfg,
		   &qns_cnoc_a2noc,
		   &srvc_cnoc
	},
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qxs_imem },
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qns_gemnoc_gc },
};

static struct qcom_icc_bcm bcm_co2 = {
	.name = "CO2",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qnm_npu },
};

static struct qcom_icc_bcm bcm_sn3 = {
	.name = "SN3",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qxs_pimem },
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &xs_qdss_stm },
};

static struct qcom_icc_bcm bcm_sn5 = {
	.name = "SN5",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &xs_pcie_modem },
};

static struct qcom_icc_bcm bcm_sn6 = {
	.name = "SN6",
	.keepalive = false,
	.num_nodes = 2,
	.nodes = { &xs_pcie_0, &xs_pcie_1 },
};

static struct qcom_icc_bcm bcm_sn7 = {
	.name = "SN7",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qnm_aggre1_noc },
};

static struct qcom_icc_bcm bcm_sn8 = {
	.name = "SN8",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qnm_aggre2_noc },
};

static struct qcom_icc_bcm bcm_sn9 = {
	.name = "SN9",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qnm_gemnoc_pcie },
};

static struct qcom_icc_bcm bcm_sn11 = {
	.name = "SN11",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qnm_gemnoc },
};

static struct qcom_icc_bcm bcm_sn12 = {
	.name = "SN12",
	.keepalive = false,
	.num_nodes = 2,
	.nodes = { &qns_pcie_modem_mem_noc, &qns_pcie_mem_noc },
};

static struct qcom_icc_bcm * const aggre1_noc_bcms[] = {
	&bcm_sn12,
};

static struct qcom_icc_node * const aggre1_noc_nodes[] = {
	[MASTER_A1NOC_CFG] = &qhm_a1noc_cfg,
	[MASTER_QSPI_0] = &qhm_qspi,
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_QUP_2] = &qhm_qup2,
	[MASTER_TSIF] = &qhm_tsif,
	[MASTER_PCIE_2] = &xm_pcie3_modem,
	[MASTER_SDCC_4] = &xm_sdc4,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_USB3] = &xm_usb3_0,
	[MASTER_USB3_1] = &xm_usb3_1,
	[A1NOC_SNOC_SLV] = &qns_a1noc_snoc,
	[SLAVE_ANOC_PCIE_GEM_NOC_1] = &qns_pcie_modem_mem_noc,
	[SLAVE_SERVICE_A1NOC] = &srvc_aggre1_noc,
};

static const struct qcom_icc_desc sm8250_aggre1_noc = {
	.nodes = aggre1_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre1_noc_nodes),
	.bcms = aggre1_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_noc_bcms),
};

static struct qcom_icc_bcm * const aggre2_noc_bcms[] = {
	&bcm_ce0,
	&bcm_sn12,
};

static struct qcom_icc_bcm * const qup_virt_bcms[] = {
	&bcm_qup0,
};

static struct qcom_icc_node * const qup_virt_nodes[] = {
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[MASTER_QUP_CORE_1] = &qup1_core_master,
	[MASTER_QUP_CORE_2] = &qup2_core_master,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_QUP_CORE_1] = &qup1_core_slave,
	[SLAVE_QUP_CORE_2] = &qup2_core_slave,
};

static const struct qcom_icc_desc sm8250_qup_virt = {
	.nodes = qup_virt_nodes,
	.num_nodes = ARRAY_SIZE(qup_virt_nodes),
	.bcms = qup_virt_bcms,
	.num_bcms = ARRAY_SIZE(qup_virt_bcms),
};

static struct qcom_icc_node * const aggre2_noc_nodes[] = {
	[MASTER_A2NOC_CFG] = &qhm_a2noc_cfg,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_CNOC_A2NOC] = &qnm_cnoc,
	[MASTER_CRYPTO_CORE_0] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_PCIE] = &xm_pcie3_0,
	[MASTER_PCIE_1] = &xm_pcie3_1,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_UFS_CARD] = &xm_ufs_card,
	[A2NOC_SNOC_SLV] = &qns_a2noc_snoc,
	[SLAVE_ANOC_PCIE_GEM_NOC] = &qns_pcie_mem_noc,
	[SLAVE_SERVICE_A2NOC] = &srvc_aggre2_noc,
};

static const struct qcom_icc_desc sm8250_aggre2_noc = {
	.nodes = aggre2_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre2_noc_nodes),
	.bcms = aggre2_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_noc_bcms),
};

static struct qcom_icc_bcm * const compute_noc_bcms[] = {
	&bcm_co0,
	&bcm_co2,
};

static struct qcom_icc_node * const compute_noc_nodes[] = {
	[MASTER_NPU] = &qnm_npu,
	[SLAVE_CDSP_MEM_NOC] = &qns_cdsp_mem_noc,
};

static const struct qcom_icc_desc sm8250_compute_noc = {
	.nodes = compute_noc_nodes,
	.num_nodes = ARRAY_SIZE(compute_noc_nodes),
	.bcms = compute_noc_bcms,
	.num_bcms = ARRAY_SIZE(compute_noc_bcms),
};

static struct qcom_icc_bcm * const config_noc_bcms[] = {
	&bcm_cn0,
};

static struct qcom_icc_node * const config_noc_nodes[] = {
	[SNOC_CNOC_MAS] = &qnm_snoc,
	[MASTER_QDSS_DAP] = &xm_qdss_dap,
	[SLAVE_A1NOC_CFG] = &qhs_a1_noc_cfg,
	[SLAVE_A2NOC_CFG] = &qhs_a2_noc_cfg,
	[SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_NORTH] = &qhs_ahb2phy1,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CDSP_CFG] = &qhs_compute_dsp,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MMCX_CFG] = &qhs_cpr_mmcx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_CX_RDPM] = &qhs_cx_rdpm,
	[SLAVE_DCC_CFG] = &qhs_dcc_cfg,
	[SLAVE_CNOC_DDRSS] = &qhs_ddrss_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_GRAPHICS_3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[SLAVE_LPASS] = &qhs_lpass_cfg,
	[SLAVE_CNOC_MNOC_CFG] = &qhs_mnoc_cfg,
	[SLAVE_NPU_CFG] = &qhs_npu_cfg,
	[SLAVE_PCIE_0_CFG] = &qhs_pcie0_cfg,
	[SLAVE_PCIE_1_CFG] = &qhs_pcie1_cfg,
	[SLAVE_PCIE_2_CFG] = &qhs_pcie_modem_cfg,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QSPI_0] = &qhs_qspi,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_QUP_2] = &qhs_qup2,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SDCC_4] = &qhs_sdc4,
	[SLAVE_SNOC_CFG] = &qhs_snoc_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM_NORTH] = &qhs_tlmm0,
	[SLAVE_TLMM_SOUTH] = &qhs_tlmm1,
	[SLAVE_TLMM_WEST] = &qhs_tlmm2,
	[SLAVE_TSIF] = &qhs_tsif,
	[SLAVE_UFS_CARD_CFG] = &qhs_ufs_card_cfg,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB3] = &qhs_usb3_0,
	[SLAVE_USB3_1] = &qhs_usb3_1,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_CNOC_A2NOC] = &qns_cnoc_a2noc,
	[SLAVE_SERVICE_CNOC] = &srvc_cnoc,
};

static const struct qcom_icc_desc sm8250_config_noc = {
	.nodes = config_noc_nodes,
	.num_nodes = ARRAY_SIZE(config_noc_nodes),
	.bcms = config_noc_bcms,
	.num_bcms = ARRAY_SIZE(config_noc_bcms),
};

static struct qcom_icc_bcm * const dc_noc_bcms[] = {
};

static struct qcom_icc_node * const dc_noc_nodes[] = {
	[MASTER_CNOC_DC_NOC] = &qhm_cnoc_dc_noc,
	[SLAVE_LLCC_CFG] = &qhs_llcc,
	[SLAVE_GEM_NOC_CFG] = &qhs_memnoc,
};

static const struct qcom_icc_desc sm8250_dc_noc = {
	.nodes = dc_noc_nodes,
	.num_nodes = ARRAY_SIZE(dc_noc_nodes),
	.bcms = dc_noc_bcms,
	.num_bcms = ARRAY_SIZE(dc_noc_bcms),
};

static struct qcom_icc_bcm * const gem_noc_bcms[] = {
	&bcm_sh0,
	&bcm_sh2,
	&bcm_sh3,
	&bcm_sh4,
};

static struct qcom_icc_node * const gem_noc_nodes[] = {
	[MASTER_GPU_TCU] = &alm_gpu_tcu,
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_AMPSS_M0] = &chm_apps,
	[MASTER_GEM_NOC_CFG] = &qhm_gemnoc_cfg,
	[MASTER_COMPUTE_NOC] = &qnm_cmpnoc,
	[MASTER_GRAPHICS_3D] = &qnm_gpu,
	[MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[MASTER_MNOC_SF_MEM_NOC] = &qnm_mnoc_sf,
	[MASTER_ANOC_PCIE_GEM_NOC] = &qnm_pcie,
	[MASTER_SNOC_GC_MEM_NOC] = &qnm_snoc_gc,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[SLAVE_GEM_NOC_SNOC] = &qns_gem_noc_snoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MEM_NOC_PCIE_SNOC] = &qns_sys_pcie,
	[SLAVE_SERVICE_GEM_NOC_1] = &srvc_even_gemnoc,
	[SLAVE_SERVICE_GEM_NOC_2] = &srvc_odd_gemnoc,
	[SLAVE_SERVICE_GEM_NOC] = &srvc_sys_gemnoc,
};

static const struct qcom_icc_desc sm8250_gem_noc = {
	.nodes = gem_noc_nodes,
	.num_nodes = ARRAY_SIZE(gem_noc_nodes),
	.bcms = gem_noc_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_bcms),
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
};

static struct qcom_icc_node * const mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI_CH0] = &ebi,
};

static const struct qcom_icc_desc sm8250_mc_virt = {
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const mmss_noc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
	&bcm_mm2,
	&bcm_mm3,
};

static struct qcom_icc_node * const mmss_noc_nodes[] = {
	[MASTER_CNOC_MNOC_CFG] = &qhm_mnoc_cfg,
	[MASTER_CAMNOC_HF] = &qnm_camnoc_hf,
	[MASTER_CAMNOC_ICP] = &qnm_camnoc_icp,
	[MASTER_CAMNOC_SF] = &qnm_camnoc_sf,
	[MASTER_VIDEO_P0] = &qnm_video0,
	[MASTER_VIDEO_P1] = &qnm_video1,
	[MASTER_VIDEO_PROC] = &qnm_video_cvp,
	[MASTER_MDP_PORT0] = &qxm_mdp0,
	[MASTER_MDP_PORT1] = &qxm_mdp1,
	[MASTER_ROTATOR] = &qxm_rot,
	[SLAVE_MNOC_HF_MEM_NOC] = &qns_mem_noc_hf,
	[SLAVE_MNOC_SF_MEM_NOC] = &qns_mem_noc_sf,
	[SLAVE_SERVICE_MNOC] = &srvc_mnoc,
};

static const struct qcom_icc_desc sm8250_mmss_noc = {
	.nodes = mmss_noc_nodes,
	.num_nodes = ARRAY_SIZE(mmss_noc_nodes),
	.bcms = mmss_noc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_bcms),
};

static struct qcom_icc_bcm * const npu_noc_bcms[] = {
};

static struct qcom_icc_node * const npu_noc_nodes[] = {
	[MASTER_NPU_SYS] = &amm_npu_sys,
	[MASTER_NPU_CDP] = &amm_npu_sys_cdp_w,
	[MASTER_NPU_NOC_CFG] = &qhm_cfg,
	[SLAVE_NPU_CAL_DP0] = &qhs_cal_dp0,
	[SLAVE_NPU_CAL_DP1] = &qhs_cal_dp1,
	[SLAVE_NPU_CP] = &qhs_cp,
	[SLAVE_NPU_INT_DMA_BWMON_CFG] = &qhs_dma_bwmon,
	[SLAVE_NPU_DPM] = &qhs_dpm,
	[SLAVE_ISENSE_CFG] = &qhs_isense,
	[SLAVE_NPU_LLM_CFG] = &qhs_llm,
	[SLAVE_NPU_TCM] = &qhs_tcm,
	[SLAVE_NPU_COMPUTE_NOC] = &qns_npu_sys,
	[SLAVE_SERVICE_NPU_NOC] = &srvc_noc,
};

static const struct qcom_icc_desc sm8250_npu_noc = {
	.nodes = npu_noc_nodes,
	.num_nodes = ARRAY_SIZE(npu_noc_nodes),
	.bcms = npu_noc_bcms,
	.num_bcms = ARRAY_SIZE(npu_noc_bcms),
};

static struct qcom_icc_bcm * const system_noc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn11,
	&bcm_sn2,
	&bcm_sn3,
	&bcm_sn4,
	&bcm_sn5,
	&bcm_sn6,
	&bcm_sn7,
	&bcm_sn8,
	&bcm_sn9,
};

static struct qcom_icc_node * const system_noc_nodes[] = {
	[MASTER_SNOC_CFG] = &qhm_snoc_cfg,
	[A1NOC_SNOC_MAS] = &qnm_aggre1_noc,
	[A2NOC_SNOC_MAS] = &qnm_aggre2_noc,
	[MASTER_GEM_NOC_SNOC] = &qnm_gemnoc,
	[MASTER_GEM_NOC_PCIE_SNOC] = &qnm_gemnoc_pcie,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_APPSS] = &qhs_apss,
	[SNOC_CNOC_SLV] = &qns_cnoc,
	[SLAVE_SNOC_GEM_NOC_GC] = &qns_gemnoc_gc,
	[SLAVE_SNOC_GEM_NOC_SF] = &qns_gemnoc_sf,
	[SLAVE_OCIMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
	[SLAVE_PCIE_0] = &xs_pcie_0,
	[SLAVE_PCIE_1] = &xs_pcie_1,
	[SLAVE_PCIE_2] = &xs_pcie_modem,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sm8250_system_noc = {
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sm8250-aggre1-noc",
	  .data = &sm8250_aggre1_noc},
	{ .compatible = "qcom,sm8250-aggre2-noc",
	  .data = &sm8250_aggre2_noc},
	{ .compatible = "qcom,sm8250-compute-noc",
	  .data = &sm8250_compute_noc},
	{ .compatible = "qcom,sm8250-config-noc",
	  .data = &sm8250_config_noc},
	{ .compatible = "qcom,sm8250-dc-noc",
	  .data = &sm8250_dc_noc},
	{ .compatible = "qcom,sm8250-gem-noc",
	  .data = &sm8250_gem_noc},
	{ .compatible = "qcom,sm8250-mc-virt",
	  .data = &sm8250_mc_virt},
	{ .compatible = "qcom,sm8250-mmss-noc",
	  .data = &sm8250_mmss_noc},
	{ .compatible = "qcom,sm8250-npu-noc",
	  .data = &sm8250_npu_noc},
	{ .compatible = "qcom,sm8250-qup-virt",
	  .data = &sm8250_qup_virt },
	{ .compatible = "qcom,sm8250-system-noc",
	  .data = &sm8250_system_noc},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-sm8250",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(qnoc_driver);

MODULE_DESCRIPTION("Qualcomm SM8250 NoC driver");
MODULE_LICENSE("GPL v2");
