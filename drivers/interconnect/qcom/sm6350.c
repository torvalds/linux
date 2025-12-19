// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Luca Weiss <luca.weiss@fairphone.com>
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <dt-bindings/interconnect/qcom,sm6350.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"

static struct qcom_icc_node qhm_a1noc_cfg;
static struct qcom_icc_node qhm_qup_0;
static struct qcom_icc_node xm_emmc;
static struct qcom_icc_node xm_ufs_mem;
static struct qcom_icc_node qhm_a2noc_cfg;
static struct qcom_icc_node qhm_qdss_bam;
static struct qcom_icc_node qhm_qup_1;
static struct qcom_icc_node qxm_crypto;
static struct qcom_icc_node qxm_ipa;
static struct qcom_icc_node xm_qdss_etr;
static struct qcom_icc_node xm_sdc2;
static struct qcom_icc_node xm_usb3_0;
static struct qcom_icc_node qxm_camnoc_hf0_uncomp;
static struct qcom_icc_node qxm_camnoc_icp_uncomp;
static struct qcom_icc_node qxm_camnoc_sf_uncomp;
static struct qcom_icc_node qup0_core_master;
static struct qcom_icc_node qup1_core_master;
static struct qcom_icc_node qnm_npu;
static struct qcom_icc_node qxm_npu_dsp;
static struct qcom_icc_node qnm_snoc;
static struct qcom_icc_node xm_qdss_dap;
static struct qcom_icc_node qhm_cnoc_dc_noc;
static struct qcom_icc_node acm_apps;
static struct qcom_icc_node acm_sys_tcu;
static struct qcom_icc_node qhm_gemnoc_cfg;
static struct qcom_icc_node qnm_cmpnoc;
static struct qcom_icc_node qnm_mnoc_hf;
static struct qcom_icc_node qnm_mnoc_sf;
static struct qcom_icc_node qnm_snoc_gc;
static struct qcom_icc_node qnm_snoc_sf;
static struct qcom_icc_node qxm_gpu;
static struct qcom_icc_node llcc_mc;
static struct qcom_icc_node qhm_mnoc_cfg;
static struct qcom_icc_node qnm_video0;
static struct qcom_icc_node qnm_video_cvp;
static struct qcom_icc_node qxm_camnoc_hf;
static struct qcom_icc_node qxm_camnoc_icp;
static struct qcom_icc_node qxm_camnoc_sf;
static struct qcom_icc_node qxm_mdp0;
static struct qcom_icc_node amm_npu_sys;
static struct qcom_icc_node qhm_npu_cfg;
static struct qcom_icc_node qhm_snoc_cfg;
static struct qcom_icc_node qnm_aggre1_noc;
static struct qcom_icc_node qnm_aggre2_noc;
static struct qcom_icc_node qnm_gemnoc;
static struct qcom_icc_node qxm_pimem;
static struct qcom_icc_node xm_gic;
static struct qcom_icc_node qns_a1noc_snoc;
static struct qcom_icc_node srvc_aggre1_noc;
static struct qcom_icc_node qns_a2noc_snoc;
static struct qcom_icc_node srvc_aggre2_noc;
static struct qcom_icc_node qns_camnoc_uncomp;
static struct qcom_icc_node qup0_core_slave;
static struct qcom_icc_node qup1_core_slave;
static struct qcom_icc_node qns_cdsp_gemnoc;
static struct qcom_icc_node qhs_a1_noc_cfg;
static struct qcom_icc_node qhs_a2_noc_cfg;
static struct qcom_icc_node qhs_ahb2phy0;
static struct qcom_icc_node qhs_ahb2phy2;
static struct qcom_icc_node qhs_aoss;
static struct qcom_icc_node qhs_boot_rom;
static struct qcom_icc_node qhs_camera_cfg;
static struct qcom_icc_node qhs_camera_nrt_thrott_cfg;
static struct qcom_icc_node qhs_camera_rt_throttle_cfg;
static struct qcom_icc_node qhs_clk_ctl;
static struct qcom_icc_node qhs_cpr_cx;
static struct qcom_icc_node qhs_cpr_mx;
static struct qcom_icc_node qhs_crypto0_cfg;
static struct qcom_icc_node qhs_dcc_cfg;
static struct qcom_icc_node qhs_ddrss_cfg;
static struct qcom_icc_node qhs_display_cfg;
static struct qcom_icc_node qhs_display_throttle_cfg;
static struct qcom_icc_node qhs_emmc_cfg;
static struct qcom_icc_node qhs_glm;
static struct qcom_icc_node qhs_gpuss_cfg;
static struct qcom_icc_node qhs_imem_cfg;
static struct qcom_icc_node qhs_ipa;
static struct qcom_icc_node qhs_mnoc_cfg;
static struct qcom_icc_node qhs_mss_cfg;
static struct qcom_icc_node qhs_npu_cfg;
static struct qcom_icc_node qhs_pdm;
static struct qcom_icc_node qhs_pimem_cfg;
static struct qcom_icc_node qhs_prng;
static struct qcom_icc_node qhs_qdss_cfg;
static struct qcom_icc_node qhs_qm_cfg;
static struct qcom_icc_node qhs_qm_mpu_cfg;
static struct qcom_icc_node qhs_qup0;
static struct qcom_icc_node qhs_qup1;
static struct qcom_icc_node qhs_sdc2;
static struct qcom_icc_node qhs_security;
static struct qcom_icc_node qhs_snoc_cfg;
static struct qcom_icc_node qhs_tcsr;
static struct qcom_icc_node qhs_ufs_mem_cfg;
static struct qcom_icc_node qhs_usb3_0;
static struct qcom_icc_node qhs_venus_cfg;
static struct qcom_icc_node qhs_venus_throttle_cfg;
static struct qcom_icc_node qhs_vsense_ctrl_cfg;
static struct qcom_icc_node srvc_cnoc;
static struct qcom_icc_node qhs_gemnoc;
static struct qcom_icc_node qhs_llcc;
static struct qcom_icc_node qhs_mcdma_ms_mpu_cfg;
static struct qcom_icc_node qhs_mdsp_ms_mpu_cfg;
static struct qcom_icc_node qns_gem_noc_snoc;
static struct qcom_icc_node qns_llcc;
static struct qcom_icc_node srvc_gemnoc;
static struct qcom_icc_node ebi;
static struct qcom_icc_node qns_mem_noc_hf;
static struct qcom_icc_node qns_mem_noc_sf;
static struct qcom_icc_node srvc_mnoc;
static struct qcom_icc_node qhs_cal_dp0;
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
static struct qcom_icc_node xs_qdss_stm;
static struct qcom_icc_node xs_sys_tcu_cfg;

static struct qcom_icc_node qhm_a1noc_cfg = {
	.name = "qhm_a1noc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &srvc_aggre1_noc },
};

static struct qcom_icc_qosbox qhm_qup_0_qos = {
	.num_ports = 1,
	.port_offsets = { 0xa000 },
	.prio = 2,
	.urg_fwd = 0,
};

static struct qcom_icc_node qhm_qup_0 = {
	.name = "qhm_qup_0",
	.channels = 1,
	.buswidth = 4,
	.qosbox = &qhm_qup_0_qos,
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_qosbox xm_emmc_qos = {
	.num_ports = 1,
	.port_offsets = { 0x7000 },
	.prio = 2,
	.urg_fwd = 0,
};

static struct qcom_icc_node xm_emmc = {
	.name = "xm_emmc",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &xm_emmc_qos,
	.num_links = 1,
	.link_nodes = { &qns_a1noc_snoc },
};

static struct qcom_icc_qosbox xm_ufs_mem_qos = {
	.num_ports = 1,
	.port_offsets = { 0x8000 },
	.prio = 4,
	.urg_fwd = 0,
};

static struct qcom_icc_node xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &xm_ufs_mem_qos,
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

static struct qcom_icc_qosbox qhm_qdss_bam_qos = {
	.num_ports = 1,
	.port_offsets = { 0xb000 },
	.prio = 2,
	.urg_fwd = 0,
};

static struct qcom_icc_node qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.channels = 1,
	.buswidth = 4,
	.qosbox = &qhm_qdss_bam_qos,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_qosbox qhm_qup_1_qos = {
	.num_ports = 1,
	.port_offsets = { 0x9000 },
	.prio = 2,
	.urg_fwd = 0,
};
static struct qcom_icc_node qhm_qup_1 = {
	.name = "qhm_qup_1",
	.channels = 1,
	.buswidth = 4,
	.qosbox = &qhm_qup_1_qos,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_qosbox qxm_crypto_qos = {
	.num_ports = 1,
	.port_offsets = { 0x6000 },
	.prio = 2,
	.urg_fwd = 0,
};

static struct qcom_icc_node qxm_crypto = {
	.name = "qxm_crypto",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &qxm_crypto_qos,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_qosbox qxm_ipa_qos = {
	.num_ports = 1,
	.port_offsets = { 0x7000 },
	.prio = 2,
	.urg_fwd = 0,
};

static struct qcom_icc_node qxm_ipa = {
	.name = "qxm_ipa",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &qxm_ipa_qos,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_qosbox xm_qdss_etr_qos = {
	.num_ports = 1,
	.port_offsets = { 0xc000 },
	.prio = 2,
	.urg_fwd = 0,
};

static struct qcom_icc_node xm_qdss_etr = {
	.name = "xm_qdss_etr",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &xm_qdss_etr_qos,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_qosbox xm_sdc2_qos = {
	.num_ports = 1,
	.port_offsets = { 0x18000 },
	.prio = 2,
	.urg_fwd = 0,
};

static struct qcom_icc_node xm_sdc2 = {
	.name = "xm_sdc2",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &xm_sdc2_qos,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_qosbox xm_usb3_0_qos = {
	.num_ports = 1,
	.port_offsets = { 0xd000 },
	.prio = 2,
	.urg_fwd = 0,
};

static struct qcom_icc_node xm_usb3_0 = {
	.name = "xm_usb3_0",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &xm_usb3_0_qos,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_node qxm_camnoc_hf0_uncomp = {
	.name = "qxm_camnoc_hf0_uncomp",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_camnoc_uncomp },
};

static struct qcom_icc_node qxm_camnoc_icp_uncomp = {
	.name = "qxm_camnoc_icp_uncomp",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_camnoc_uncomp },
};

static struct qcom_icc_node qxm_camnoc_sf_uncomp = {
	.name = "qxm_camnoc_sf_uncomp",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_camnoc_uncomp },
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

static struct qcom_icc_qosbox qnm_npu_qos = {
	.num_ports = 2,
	.port_offsets = { 0xf000, 0x11000 },
	.prio = 0,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_npu = {
	.name = "qnm_npu",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &qnm_npu_qos,
	.num_links = 1,
	.link_nodes = { &qns_cdsp_gemnoc },
};

static struct qcom_icc_qosbox qxm_npu_dsp_qos = {
	.num_ports = 1,
	.port_offsets = { 0x13000 },
	.prio = 0,
	.urg_fwd = 1,
};

static struct qcom_icc_node qxm_npu_dsp = {
	.name = "qxm_npu_dsp",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &qxm_npu_dsp_qos,
	.num_links = 1,
	.link_nodes = { &qns_cdsp_gemnoc },
};

static struct qcom_icc_node qnm_snoc = {
	.name = "qnm_snoc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 42,
	.link_nodes = { &qhs_camera_cfg,
			&qhs_sdc2,
			&qhs_mnoc_cfg,
			&qhs_ufs_mem_cfg,
			&qhs_qm_cfg,
			&qhs_snoc_cfg,
			&qhs_qm_mpu_cfg,
			&qhs_glm,
			&qhs_pdm,
			&qhs_camera_nrt_thrott_cfg,
			&qhs_a2_noc_cfg,
			&qhs_qdss_cfg,
			&qhs_vsense_ctrl_cfg,
			&qhs_camera_rt_throttle_cfg,
			&qhs_display_cfg,
			&qhs_tcsr,
			&qhs_dcc_cfg,
			&qhs_ddrss_cfg,
			&qhs_display_throttle_cfg,
			&qhs_npu_cfg,
			&qhs_ahb2phy0,
			&qhs_gpuss_cfg,
			&qhs_boot_rom,
			&qhs_venus_cfg,
			&qhs_ipa,
			&qhs_security,
			&qhs_imem_cfg,
			&qhs_mss_cfg,
			&srvc_cnoc,
			&qhs_usb3_0,
			&qhs_venus_throttle_cfg,
			&qhs_cpr_cx,
			&qhs_a1_noc_cfg,
			&qhs_aoss,
			&qhs_prng,
			&qhs_emmc_cfg,
			&qhs_crypto0_cfg,
			&qhs_pimem_cfg,
			&qhs_cpr_mx,
			&qhs_qup0,
			&qhs_qup1,
			&qhs_clk_ctl },
};

static struct qcom_icc_node xm_qdss_dap = {
	.name = "xm_qdss_dap",
	.channels = 1,
	.buswidth = 8,
	.num_links = 42,
	.link_nodes = { &qhs_camera_cfg,
			&qhs_sdc2,
			&qhs_mnoc_cfg,
			&qhs_ufs_mem_cfg,
			&qhs_qm_cfg,
			&qhs_snoc_cfg,
			&qhs_qm_mpu_cfg,
			&qhs_glm,
			&qhs_pdm,
			&qhs_camera_nrt_thrott_cfg,
			&qhs_a2_noc_cfg,
			&qhs_qdss_cfg,
			&qhs_vsense_ctrl_cfg,
			&qhs_camera_rt_throttle_cfg,
			&qhs_display_cfg,
			&qhs_tcsr,
			&qhs_dcc_cfg,
			&qhs_ddrss_cfg,
			&qhs_display_throttle_cfg,
			&qhs_npu_cfg,
			&qhs_ahb2phy0,
			&qhs_gpuss_cfg,
			&qhs_boot_rom,
			&qhs_venus_cfg,
			&qhs_ipa,
			&qhs_security,
			&qhs_imem_cfg,
			&qhs_mss_cfg,
			&srvc_cnoc,
			&qhs_usb3_0,
			&qhs_venus_throttle_cfg,
			&qhs_cpr_cx,
			&qhs_a1_noc_cfg,
			&qhs_aoss,
			&qhs_prng,
			&qhs_emmc_cfg,
			&qhs_crypto0_cfg,
			&qhs_pimem_cfg,
			&qhs_cpr_mx,
			&qhs_qup0,
			&qhs_qup1,
			&qhs_clk_ctl },
};

static struct qcom_icc_node qhm_cnoc_dc_noc = {
	.name = "qhm_cnoc_dc_noc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.link_nodes = { &qhs_llcc,
			&qhs_gemnoc },
};

static struct qcom_icc_qosbox acm_apps_qos = {
	.num_ports = 2,
	.port_offsets = { 0x2f100, 0x2f000 },
	.prio = 0,
	.urg_fwd = 0,
};

static struct qcom_icc_node acm_apps = {
	.name = "acm_apps",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &acm_apps_qos,
	.num_links = 2,
	.link_nodes = { &qns_llcc,
			&qns_gem_noc_snoc },
};

static struct qcom_icc_qosbox acm_sys_tcu_qos = {
	.num_ports = 1,
	.port_offsets = { 0x35000 },
	.prio = 6,
	.urg_fwd = 0,
};

static struct qcom_icc_node acm_sys_tcu = {
	.name = "acm_sys_tcu",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &acm_sys_tcu_qos,
	.num_links = 2,
	.link_nodes = { &qns_llcc,
			&qns_gem_noc_snoc },
};

static struct qcom_icc_node qhm_gemnoc_cfg = {
	.name = "qhm_gemnoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 3,
	.link_nodes = { &qhs_mcdma_ms_mpu_cfg,
			&srvc_gemnoc,
			&qhs_mdsp_ms_mpu_cfg },
};

static struct qcom_icc_qosbox qnm_cmpnoc_qos = {
	.num_ports = 1,
	.port_offsets = { 0x2e000 },
	.prio = 0,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_cmpnoc = {
	.name = "qnm_cmpnoc",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &qnm_cmpnoc_qos,
	.num_links = 2,
	.link_nodes = { &qns_llcc,
			&qns_gem_noc_snoc },
};

static struct qcom_icc_qosbox qnm_mnoc_hf_qos = {
	.num_ports = 1,
	.port_offsets = { 0x30000 },
	.prio = 0,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_mnoc_hf = {
	.name = "qnm_mnoc_hf",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &qnm_mnoc_hf_qos,
	.num_links = 2,
	.link_nodes = { &qns_llcc,
			&qns_gem_noc_snoc },
};

static struct qcom_icc_qosbox qnm_mnoc_sf_qos = {
	.num_ports = 1,
	.port_offsets = { 0x34000 },
	.prio = 0,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_mnoc_sf = {
	.name = "qnm_mnoc_sf",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &qnm_mnoc_sf_qos,
	.num_links = 2,
	.link_nodes = { &qns_llcc,
			&qns_gem_noc_snoc },
};

static struct qcom_icc_qosbox qnm_snoc_gc_qos = {
	.num_ports = 1,
	.port_offsets = { 0x32000 },
	.prio = 0,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_snoc_gc = {
	.name = "qnm_snoc_gc",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &qnm_snoc_gc_qos,
	.num_links = 1,
	.link_nodes = { &qns_llcc },
};

static struct qcom_icc_qosbox qnm_snoc_sf_qos = {
	.num_ports = 1,
	.port_offsets = { 0x31000 },
	.prio = 0,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_snoc_sf = {
	.name = "qnm_snoc_sf",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &qnm_snoc_sf_qos,
	.num_links = 1,
	.link_nodes = { &qns_llcc },
};

static struct qcom_icc_qosbox qxm_gpu_qos = {
	.num_ports = 2,
	.port_offsets = { 0x33000, 0x33080 },
	.prio = 0,
	.urg_fwd = 0,
};

static struct qcom_icc_node qxm_gpu = {
	.name = "qxm_gpu",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &qxm_gpu_qos,
	.num_links = 2,
	.link_nodes = { &qns_llcc,
			&qns_gem_noc_snoc },
};

static struct qcom_icc_node llcc_mc = {
	.name = "llcc_mc",
	.channels = 2,
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

static struct qcom_icc_qosbox qnm_video0_qos = {
	.num_ports = 1,
	.port_offsets = { 0xf000 },
	.prio = 2,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_video0 = {
	.name = "qnm_video0",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &qnm_video0_qos,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_qosbox qnm_video_cvp_qos = {
	.num_ports = 1,
	.port_offsets = { 0xe000 },
	.prio = 5,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_video_cvp = {
	.name = "qnm_video_cvp",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &qnm_video_cvp_qos,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_qosbox qxm_camnoc_hf_qos = {
	.num_ports = 2,
	.port_offsets = { 0xa000, 0xb000 },
	.prio = 3,
	.urg_fwd = 1,
};

static struct qcom_icc_node qxm_camnoc_hf = {
	.name = "qxm_camnoc_hf",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &qxm_camnoc_hf_qos,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_qosbox qxm_camnoc_icp_qos = {
	.num_ports = 1,
	.port_offsets = { 0xd000 },
	.prio = 5,
	.urg_fwd = 0,
};

static struct qcom_icc_node qxm_camnoc_icp = {
	.name = "qxm_camnoc_icp",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &qxm_camnoc_icp_qos,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_qosbox qxm_camnoc_sf_qos = {
	.num_ports = 1,
	.port_offsets = { 0x9000 },
	.prio = 3,
	.urg_fwd = 1,
};

static struct qcom_icc_node qxm_camnoc_sf = {
	.name = "qxm_camnoc_sf",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &qxm_camnoc_sf_qos,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_qosbox qxm_mdp0_qos = {
	.num_ports = 1,
	.port_offsets = { 0xc000 },
	.prio = 3,
	.urg_fwd = 1,
};

static struct qcom_icc_node qxm_mdp0 = {
	.name = "qxm_mdp0",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &qxm_mdp0_qos,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_node amm_npu_sys = {
	.name = "amm_npu_sys",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_npu_sys },
};

static struct qcom_icc_node qhm_npu_cfg = {
	.name = "qhm_npu_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 8,
	.link_nodes = { &srvc_noc,
			&qhs_isense,
			&qhs_llm,
			&qhs_dma_bwmon,
			&qhs_cp,
			&qhs_tcm,
			&qhs_cal_dp0,
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
	.num_links = 6,
	.link_nodes = { &qns_gemnoc_sf,
			&qxs_pimem,
			&qxs_imem,
			&qhs_apss,
			&qns_cnoc,
			&xs_qdss_stm },
};

static struct qcom_icc_node qnm_aggre2_noc = {
	.name = "qnm_aggre2_noc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 7,
	.link_nodes = { &qns_gemnoc_sf,
			&qxs_pimem,
			&qxs_imem,
			&qhs_apss,
			&qns_cnoc,
			&xs_sys_tcu_cfg,
			&xs_qdss_stm },
};

static struct qcom_icc_node qnm_gemnoc = {
	.name = "qnm_gemnoc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 6,
	.link_nodes = { &qxs_pimem,
			&qxs_imem,
			&qhs_apss,
			&qns_cnoc,
			&xs_sys_tcu_cfg,
			&xs_qdss_stm },
};

static struct qcom_icc_qosbox qxm_pimem_qos = {
	.num_ports = 1,
	.port_offsets = { 0xd000 },
	.prio = 2,
	.urg_fwd = 0,
};

static struct qcom_icc_node qxm_pimem = {
	.name = "qxm_pimem",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &qxm_pimem_qos,
	.num_links = 2,
	.link_nodes = { &qns_gemnoc_gc,
			&qxs_imem },
};

static struct qcom_icc_qosbox xm_gic_qos = {
	.num_ports = 1,
	.port_offsets = { 0xb000 },
	.prio = 3,
	.urg_fwd = 0,
};

static struct qcom_icc_node xm_gic = {
	.name = "xm_gic",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &xm_gic_qos,
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

static struct qcom_icc_node srvc_aggre2_noc = {
	.name = "srvc_aggre2_noc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_camnoc_uncomp = {
	.name = "qns_camnoc_uncomp",
	.channels = 1,
	.buswidth = 32,
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

static struct qcom_icc_node qns_cdsp_gemnoc = {
	.name = "qns_cdsp_gemnoc",
	.channels = 1,
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

static struct qcom_icc_node qhs_ahb2phy2 = {
	.name = "qhs_ahb2phy2",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_aoss = {
	.name = "qhs_aoss",
	.channels = 1,
	.buswidth = 4,
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

static struct qcom_icc_node qhs_camera_nrt_thrott_cfg = {
	.name = "qhs_camera_nrt_thrott_cfg",
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

static struct qcom_icc_node qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
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

static struct qcom_icc_node qhs_display_throttle_cfg = {
	.name = "qhs_display_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_emmc_cfg = {
	.name = "qhs_emmc_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_glm = {
	.name = "qhs_glm",
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

static struct qcom_icc_node qhs_mnoc_cfg = {
	.name = "qhs_mnoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qhm_mnoc_cfg },
};

static struct qcom_icc_node qhs_mss_cfg = {
	.name = "qhs_mss_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_npu_cfg = {
	.name = "qhs_npu_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qhm_npu_cfg },
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

static struct qcom_icc_node qhs_sdc2 = {
	.name = "qhs_sdc2",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_security = {
	.name = "qhs_security",
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

static struct qcom_icc_node qhs_venus_throttle_cfg = {
	.name = "qhs_venus_throttle_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_cnoc = {
	.name = "srvc_cnoc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gemnoc = {
	.name = "qhs_gemnoc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qhm_gemnoc_cfg },
};

static struct qcom_icc_node qhs_llcc = {
	.name = "qhs_llcc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_mcdma_ms_mpu_cfg = {
	.name = "qhs_mcdma_ms_mpu_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_mdsp_ms_mpu_cfg = {
	.name = "qhs_mdsp_ms_mpu_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_gem_noc_snoc = {
	.name = "qns_gem_noc_snoc",
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.link_nodes = { &qnm_gemnoc },
};

static struct qcom_icc_node qns_llcc = {
	.name = "qns_llcc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &llcc_mc },
};

static struct qcom_icc_node srvc_gemnoc = {
	.name = "srvc_gemnoc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.channels = 2,
	.buswidth = 4,
};

static struct qcom_icc_node qns_mem_noc_hf = {
	.name = "qns_mem_noc_hf",
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qnm_mnoc_hf },
};

static struct qcom_icc_node qns_mem_noc_sf = {
	.name = "qns_mem_noc_sf",
	.channels = 1,
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

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.enable_mask = BIT(3),
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qxm_crypto },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.keepalive = true,
	.num_nodes = 41,
	.nodes = { &qnm_snoc,
		   &xm_qdss_dap,
		   &qhs_a1_noc_cfg,
		   &qhs_a2_noc_cfg,
		   &qhs_ahb2phy0,
		   &qhs_aoss,
		   &qhs_boot_rom,
		   &qhs_camera_cfg,
		   &qhs_camera_nrt_thrott_cfg,
		   &qhs_camera_rt_throttle_cfg,
		   &qhs_clk_ctl,
		   &qhs_cpr_cx,
		   &qhs_cpr_mx,
		   &qhs_crypto0_cfg,
		   &qhs_dcc_cfg,
		   &qhs_ddrss_cfg,
		   &qhs_display_cfg,
		   &qhs_display_throttle_cfg,
		   &qhs_glm,
		   &qhs_gpuss_cfg,
		   &qhs_imem_cfg,
		   &qhs_ipa,
		   &qhs_mnoc_cfg,
		   &qhs_mss_cfg,
		   &qhs_npu_cfg,
		   &qhs_pimem_cfg,
		   &qhs_prng,
		   &qhs_qdss_cfg,
		   &qhs_qm_cfg,
		   &qhs_qm_mpu_cfg,
		   &qhs_qup0,
		   &qhs_qup1,
		   &qhs_security,
		   &qhs_snoc_cfg,
		   &qhs_tcsr,
		   &qhs_ufs_mem_cfg,
		   &qhs_usb3_0,
		   &qhs_venus_cfg,
		   &qhs_venus_throttle_cfg,
		   &qhs_vsense_ctrl_cfg,
		   &srvc_cnoc
	},
};

static struct qcom_icc_bcm bcm_cn1 = {
	.name = "CN1",
	.keepalive = false,
	.num_nodes = 6,
	.nodes = { &xm_emmc,
		   &xm_sdc2,
		   &qhs_ahb2phy2,
		   &qhs_emmc_cfg,
		   &qhs_pdm,
		   &qhs_sdc2
	},
};

static struct qcom_icc_bcm bcm_co0 = {
	.name = "CO0",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qns_cdsp_gemnoc },
};

static struct qcom_icc_bcm bcm_co2 = {
	.name = "CO2",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qnm_npu },
};

static struct qcom_icc_bcm bcm_co3 = {
	.name = "CO3",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qxm_npu_dsp },
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
	.num_nodes = 1,
	.nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_bcm bcm_mm1 = {
	.name = "MM1",
	.keepalive = true,
	.num_nodes = 5,
	.nodes = { &qxm_camnoc_hf0_uncomp,
		   &qxm_camnoc_icp_uncomp,
		   &qxm_camnoc_sf_uncomp,
		   &qxm_camnoc_hf,
		   &qxm_mdp0
	},
};

static struct qcom_icc_bcm bcm_mm2 = {
	.name = "MM2",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_bcm bcm_mm3 = {
	.name = "MM3",
	.keepalive = false,
	.num_nodes = 4,
	.nodes = { &qhm_mnoc_cfg, &qnm_video0, &qnm_video_cvp, &qxm_camnoc_sf },
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.keepalive = false,
	.num_nodes = 4,
	.nodes = { &qup0_core_master, &qup1_core_master, &qup0_core_slave, &qup1_core_slave },
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_sh2 = {
	.name = "SH2",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &acm_sys_tcu },
};

static struct qcom_icc_bcm bcm_sh3 = {
	.name = "SH3",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qnm_cmpnoc },
};

static struct qcom_icc_bcm bcm_sh4 = {
	.name = "SH4",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &acm_apps },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_gemnoc_sf },
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
	.nodes = { &qnm_aggre1_noc },
};

static struct qcom_icc_bcm bcm_sn6 = {
	.name = "SN6",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qnm_aggre2_noc },
};

static struct qcom_icc_bcm bcm_sn10 = {
	.name = "SN10",
	.keepalive = false,
	.num_nodes = 1,
	.nodes = { &qnm_gemnoc },
};

static struct qcom_icc_bcm * const aggre1_noc_bcms[] = {
	&bcm_cn1,
};

static struct qcom_icc_node * const aggre1_noc_nodes[] = {
	[MASTER_A1NOC_CFG] = &qhm_a1noc_cfg,
	[MASTER_QUP_0] = &qhm_qup_0,
	[MASTER_EMMC] = &xm_emmc,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[A1NOC_SNOC_SLV] = &qns_a1noc_snoc,
	[SLAVE_SERVICE_A1NOC] = &srvc_aggre1_noc,
};

static const struct regmap_config sm6350_aggre1_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x15080,
	.fast_io = true,
};

static const struct qcom_icc_desc sm6350_aggre1_noc = {
	.config = &sm6350_aggre1_noc_regmap_config,
	.nodes = aggre1_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre1_noc_nodes),
	.bcms = aggre1_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_noc_bcms),
	.qos_requires_clocks = true,
};

static struct qcom_icc_bcm * const aggre2_noc_bcms[] = {
	&bcm_ce0,
	&bcm_cn1,
};

static struct qcom_icc_node * const aggre2_noc_nodes[] = {
	[MASTER_A2NOC_CFG] = &qhm_a2noc_cfg,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QUP_1] = &qhm_qup_1,
	[MASTER_CRYPTO_CORE_0] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_USB3] = &xm_usb3_0,
	[A2NOC_SNOC_SLV] = &qns_a2noc_snoc,
	[SLAVE_SERVICE_A2NOC] = &srvc_aggre2_noc,
};

static const struct regmap_config sm6350_aggre2_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1f880,
	.fast_io = true,
};

static const struct qcom_icc_desc sm6350_aggre2_noc = {
	.config = &sm6350_aggre2_noc_regmap_config,
	.nodes = aggre2_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre2_noc_nodes),
	.bcms = aggre2_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_noc_bcms),
	.qos_requires_clocks = true,
};

static struct qcom_icc_bcm * const clk_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
	&bcm_mm1,
	&bcm_qup0,
};

static struct qcom_icc_node * const clk_virt_nodes[] = {
	[MASTER_CAMNOC_HF0_UNCOMP] = &qxm_camnoc_hf0_uncomp,
	[MASTER_CAMNOC_ICP_UNCOMP] = &qxm_camnoc_icp_uncomp,
	[MASTER_CAMNOC_SF_UNCOMP] = &qxm_camnoc_sf_uncomp,
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[MASTER_QUP_CORE_1] = &qup1_core_master,
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_CAMNOC_UNCOMP] = &qns_camnoc_uncomp,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_QUP_CORE_1] = &qup1_core_slave,
	[SLAVE_EBI_CH0] = &ebi,
};

static const struct qcom_icc_desc sm6350_clk_virt = {
	.nodes = clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(clk_virt_nodes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
};

static struct qcom_icc_bcm * const compute_noc_bcms[] = {
	&bcm_co0,
	&bcm_co2,
	&bcm_co3,
};

static struct qcom_icc_node * const compute_noc_nodes[] = {
	[MASTER_NPU] = &qnm_npu,
	[MASTER_NPU_PROC] = &qxm_npu_dsp,
	[SLAVE_CDSP_GEM_NOC] = &qns_cdsp_gemnoc,
};

static const struct regmap_config sm6350_compute_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1f880,
	.fast_io = true,
};

static const struct qcom_icc_desc sm6350_compute_noc = {
	.config = &sm6350_compute_noc_regmap_config,
	.nodes = compute_noc_nodes,
	.num_nodes = ARRAY_SIZE(compute_noc_nodes),
	.bcms = compute_noc_bcms,
	.num_bcms = ARRAY_SIZE(compute_noc_bcms),
};

static struct qcom_icc_bcm * const config_noc_bcms[] = {
	&bcm_cn0,
	&bcm_cn1,
};

static struct qcom_icc_node * const config_noc_nodes[] = {
	[SNOC_CNOC_MAS] = &qnm_snoc,
	[MASTER_QDSS_DAP] = &xm_qdss_dap,
	[SLAVE_A1NOC_CFG] = &qhs_a1_noc_cfg,
	[SLAVE_A2NOC_CFG] = &qhs_a2_noc_cfg,
	[SLAVE_AHB2PHY] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_2] = &qhs_ahb2phy2,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_BOOT_ROM] = &qhs_boot_rom,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CAMERA_NRT_THROTTLE_CFG] = &qhs_camera_nrt_thrott_cfg,
	[SLAVE_CAMERA_RT_THROTTLE_CFG] = &qhs_camera_rt_throttle_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_DCC_CFG] = &qhs_dcc_cfg,
	[SLAVE_CNOC_DDRSS] = &qhs_ddrss_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_DISPLAY_THROTTLE_CFG] = &qhs_display_throttle_cfg,
	[SLAVE_EMMC_CFG] = &qhs_emmc_cfg,
	[SLAVE_GLM] = &qhs_glm,
	[SLAVE_GRAPHICS_3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_CNOC_MNOC_CFG] = &qhs_mnoc_cfg,
	[SLAVE_CNOC_MSS] = &qhs_mss_cfg,
	[SLAVE_NPU_CFG] = &qhs_npu_cfg,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QM_CFG] = &qhs_qm_cfg,
	[SLAVE_QM_MPU_CFG] = &qhs_qm_mpu_cfg,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SECURITY] = &qhs_security,
	[SLAVE_SNOC_CFG] = &qhs_snoc_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB3] = &qhs_usb3_0,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VENUS_THROTTLE_CFG] = &qhs_venus_throttle_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_SERVICE_CNOC] = &srvc_cnoc,
};

static const struct qcom_icc_desc sm6350_config_noc = {
	.nodes = config_noc_nodes,
	.num_nodes = ARRAY_SIZE(config_noc_nodes),
	.bcms = config_noc_bcms,
	.num_bcms = ARRAY_SIZE(config_noc_bcms),
};

static struct qcom_icc_node * const dc_noc_nodes[] = {
	[MASTER_CNOC_DC_NOC] = &qhm_cnoc_dc_noc,
	[SLAVE_GEM_NOC_CFG] = &qhs_gemnoc,
	[SLAVE_LLCC_CFG] = &qhs_llcc,
};

static const struct regmap_config sm6350_dc_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x3200,
	.fast_io = true,
};

static const struct qcom_icc_desc sm6350_dc_noc = {
	.config = &sm6350_dc_noc_regmap_config,
	.nodes = dc_noc_nodes,
	.num_nodes = ARRAY_SIZE(dc_noc_nodes),
};

static struct qcom_icc_bcm * const gem_noc_bcms[] = {
	&bcm_sh0,
	&bcm_sh2,
	&bcm_sh3,
	&bcm_sh4,
};

static struct qcom_icc_node * const gem_noc_nodes[] = {
	[MASTER_AMPSS_M0] = &acm_apps,
	[MASTER_SYS_TCU] = &acm_sys_tcu,
	[MASTER_GEM_NOC_CFG] = &qhm_gemnoc_cfg,
	[MASTER_COMPUTE_NOC] = &qnm_cmpnoc,
	[MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[MASTER_MNOC_SF_MEM_NOC] = &qnm_mnoc_sf,
	[MASTER_SNOC_GC_MEM_NOC] = &qnm_snoc_gc,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[MASTER_GRAPHICS_3D] = &qxm_gpu,
	[SLAVE_MCDMA_MS_MPU_CFG] = &qhs_mcdma_ms_mpu_cfg,
	[SLAVE_MSS_PROC_MS_MPU_CFG] = &qhs_mdsp_ms_mpu_cfg,
	[SLAVE_GEM_NOC_SNOC] = &qns_gem_noc_snoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_SERVICE_GEM_NOC] = &srvc_gemnoc,
};

static const struct regmap_config sm6350_gem_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x3e200,
	.fast_io = true,
};

static const struct qcom_icc_desc sm6350_gem_noc = {
	.config = &sm6350_gem_noc_regmap_config,
	.nodes = gem_noc_nodes,
	.num_nodes = ARRAY_SIZE(gem_noc_nodes),
	.bcms = gem_noc_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_bcms),
};

static struct qcom_icc_bcm * const mmss_noc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
	&bcm_mm2,
	&bcm_mm3,
};

static struct qcom_icc_node * const mmss_noc_nodes[] = {
	[MASTER_CNOC_MNOC_CFG] = &qhm_mnoc_cfg,
	[MASTER_VIDEO_P0] = &qnm_video0,
	[MASTER_VIDEO_PROC] = &qnm_video_cvp,
	[MASTER_CAMNOC_HF] = &qxm_camnoc_hf,
	[MASTER_CAMNOC_ICP] = &qxm_camnoc_icp,
	[MASTER_CAMNOC_SF] = &qxm_camnoc_sf,
	[MASTER_MDP_PORT0] = &qxm_mdp0,
	[SLAVE_MNOC_HF_MEM_NOC] = &qns_mem_noc_hf,
	[SLAVE_MNOC_SF_MEM_NOC] = &qns_mem_noc_sf,
	[SLAVE_SERVICE_MNOC] = &srvc_mnoc,
};

static const struct regmap_config sm6350_mmss_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x1c100,
	.fast_io = true,
};

static const struct qcom_icc_desc sm6350_mmss_noc = {
	.config = &sm6350_mmss_noc_regmap_config,
	.nodes = mmss_noc_nodes,
	.num_nodes = ARRAY_SIZE(mmss_noc_nodes),
	.bcms = mmss_noc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_bcms),
};

static struct qcom_icc_node * const npu_noc_nodes[] = {
	[MASTER_NPU_SYS] = &amm_npu_sys,
	[MASTER_NPU_NOC_CFG] = &qhm_npu_cfg,
	[SLAVE_NPU_CAL_DP0] = &qhs_cal_dp0,
	[SLAVE_NPU_CP] = &qhs_cp,
	[SLAVE_NPU_INT_DMA_BWMON_CFG] = &qhs_dma_bwmon,
	[SLAVE_NPU_DPM] = &qhs_dpm,
	[SLAVE_ISENSE_CFG] = &qhs_isense,
	[SLAVE_NPU_LLM_CFG] = &qhs_llm,
	[SLAVE_NPU_TCM] = &qhs_tcm,
	[SLAVE_NPU_COMPUTE_NOC] = &qns_npu_sys,
	[SLAVE_SERVICE_NPU_NOC] = &srvc_noc,
};

static const struct qcom_icc_desc sm6350_npu_noc = {
	.nodes = npu_noc_nodes,
	.num_nodes = ARRAY_SIZE(npu_noc_nodes),
};

static struct qcom_icc_bcm * const system_noc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn10,
	&bcm_sn2,
	&bcm_sn3,
	&bcm_sn4,
	&bcm_sn5,
	&bcm_sn6,
};

static struct qcom_icc_node * const system_noc_nodes[] = {
	[MASTER_SNOC_CFG] = &qhm_snoc_cfg,
	[A1NOC_SNOC_MAS] = &qnm_aggre1_noc,
	[A2NOC_SNOC_MAS] = &qnm_aggre2_noc,
	[MASTER_GEM_NOC_SNOC] = &qnm_gemnoc,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_APPSS] = &qhs_apss,
	[SNOC_CNOC_SLV] = &qns_cnoc,
	[SLAVE_SNOC_GEM_NOC_GC] = &qns_gemnoc_gc,
	[SLAVE_SNOC_GEM_NOC_SF] = &qns_gemnoc_sf,
	[SLAVE_OCIMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct regmap_config sm6350_system_noc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x17080,
	.fast_io = true,
};

static const struct qcom_icc_desc sm6350_system_noc = {
	.config = &sm6350_system_noc_regmap_config,
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sm6350-aggre1-noc",
	  .data = &sm6350_aggre1_noc},
	{ .compatible = "qcom,sm6350-aggre2-noc",
	  .data = &sm6350_aggre2_noc},
	{ .compatible = "qcom,sm6350-clk-virt",
	  .data = &sm6350_clk_virt},
	{ .compatible = "qcom,sm6350-compute-noc",
	  .data = &sm6350_compute_noc},
	{ .compatible = "qcom,sm6350-config-noc",
	  .data = &sm6350_config_noc},
	{ .compatible = "qcom,sm6350-dc-noc",
	  .data = &sm6350_dc_noc},
	{ .compatible = "qcom,sm6350-gem-noc",
	  .data = &sm6350_gem_noc},
	{ .compatible = "qcom,sm6350-mmss-noc",
	  .data = &sm6350_mmss_noc},
	{ .compatible = "qcom,sm6350-npu-noc",
	  .data = &sm6350_npu_noc},
	{ .compatible = "qcom,sm6350-system-noc",
	  .data = &sm6350_system_noc},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-sm6350",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(qnoc_driver);

MODULE_DESCRIPTION("Qualcomm SM6350 NoC driver");
MODULE_LICENSE("GPL v2");
