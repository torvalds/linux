// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2024, Linaro Ltd.
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sort.h>
#include <dt-bindings/interconnect/qcom,sar2130p-rpmh.h>

#include "bcm-voter.h"
#include "icc-common.h"
#include "icc-rpmh.h"

static struct qcom_icc_node qup0_core_master;
static struct qcom_icc_node qup1_core_master;
static struct qcom_icc_node qnm_gemnoc_cnoc;
static struct qcom_icc_node qnm_gemnoc_pcie;
static struct qcom_icc_node xm_qdss_dap;
static struct qcom_icc_node alm_gpu_tcu;
static struct qcom_icc_node alm_sys_tcu;
static struct qcom_icc_node chm_apps;
static struct qcom_icc_node qnm_gpu;
static struct qcom_icc_node qnm_mnoc_hf;
static struct qcom_icc_node qnm_mnoc_sf;
static struct qcom_icc_node qnm_nsp_gemnoc;
static struct qcom_icc_node qnm_pcie;
static struct qcom_icc_node qnm_snoc_gc;
static struct qcom_icc_node qnm_snoc_sf;
static struct qcom_icc_node qxm_wlan_q6;
static struct qcom_icc_node qhm_config_noc;
static struct qcom_icc_node qxm_lpass_dsp;
static struct qcom_icc_node llcc_mc;
static struct qcom_icc_node qnm_camnoc_hf;
static struct qcom_icc_node qnm_camnoc_icp;
static struct qcom_icc_node qnm_camnoc_sf;
static struct qcom_icc_node qnm_lsr;
static struct qcom_icc_node qnm_mdp;
static struct qcom_icc_node qnm_mnoc_cfg;
static struct qcom_icc_node qnm_video;
static struct qcom_icc_node qnm_video_cv_cpu;
static struct qcom_icc_node qnm_video_cvp;
static struct qcom_icc_node qnm_video_v_cpu;
static struct qcom_icc_node qhm_nsp_noc_config;
static struct qcom_icc_node qxm_nsp;
static struct qcom_icc_node xm_pcie3_0;
static struct qcom_icc_node xm_pcie3_1;
static struct qcom_icc_node qhm_gic;
static struct qcom_icc_node qhm_qdss_bam;
static struct qcom_icc_node qhm_qspi;
static struct qcom_icc_node qhm_qup0;
static struct qcom_icc_node qhm_qup1;
static struct qcom_icc_node qnm_aggre2_noc;
static struct qcom_icc_node qnm_cnoc_datapath;
static struct qcom_icc_node qnm_lpass_noc;
static struct qcom_icc_node qnm_snoc_cfg;
static struct qcom_icc_node qxm_crypto;
static struct qcom_icc_node qxm_pimem;
static struct qcom_icc_node xm_gic;
static struct qcom_icc_node xm_qdss_etr_0;
static struct qcom_icc_node xm_qdss_etr_1;
static struct qcom_icc_node xm_sdc1;
static struct qcom_icc_node xm_usb3_0;
static struct qcom_icc_node qup0_core_slave;
static struct qcom_icc_node qup1_core_slave;
static struct qcom_icc_node qhs_ahb2phy0;
static struct qcom_icc_node qhs_aoss;
static struct qcom_icc_node qhs_camera_cfg;
static struct qcom_icc_node qhs_clk_ctl;
static struct qcom_icc_node qhs_compute_cfg;
static struct qcom_icc_node qhs_cpr_cx;
static struct qcom_icc_node qhs_cpr_mmcx;
static struct qcom_icc_node qhs_cpr_mxa;
static struct qcom_icc_node qhs_cpr_mxc;
static struct qcom_icc_node qhs_cpr_nspcx;
static struct qcom_icc_node qhs_crypto0_cfg;
static struct qcom_icc_node qhs_cx_rdpm;
static struct qcom_icc_node qhs_display_cfg;
static struct qcom_icc_node qhs_gpuss_cfg;
static struct qcom_icc_node qhs_imem_cfg;
static struct qcom_icc_node qhs_ipc_router;
static struct qcom_icc_node qhs_lpass_cfg;
static struct qcom_icc_node qhs_mx_rdpm;
static struct qcom_icc_node qhs_pcie0_cfg;
static struct qcom_icc_node qhs_pcie1_cfg;
static struct qcom_icc_node qhs_pdm;
static struct qcom_icc_node qhs_pimem_cfg;
static struct qcom_icc_node qhs_prng;
static struct qcom_icc_node qhs_qdss_cfg;
static struct qcom_icc_node qhs_qspi;
static struct qcom_icc_node qhs_qup0;
static struct qcom_icc_node qhs_qup1;
static struct qcom_icc_node qhs_sdc1;
static struct qcom_icc_node qhs_tcsr;
static struct qcom_icc_node qhs_tlmm;
static struct qcom_icc_node qhs_tme_cfg;
static struct qcom_icc_node qhs_usb3_0;
static struct qcom_icc_node qhs_venus_cfg;
static struct qcom_icc_node qhs_vsense_ctrl_cfg;
static struct qcom_icc_node qhs_wlan_q6;
static struct qcom_icc_node qns_ddrss_cfg;
static struct qcom_icc_node qns_mnoc_cfg;
static struct qcom_icc_node qns_snoc_cfg;
static struct qcom_icc_node qxs_imem;
static struct qcom_icc_node qxs_pimem;
static struct qcom_icc_node srvc_cnoc;
static struct qcom_icc_node xs_pcie_0;
static struct qcom_icc_node xs_pcie_1;
static struct qcom_icc_node xs_qdss_stm;
static struct qcom_icc_node xs_sys_tcu_cfg;
static struct qcom_icc_node qns_gem_noc_cnoc;
static struct qcom_icc_node qns_llcc;
static struct qcom_icc_node qns_pcie;
static struct qcom_icc_node qhs_lpass_core;
static struct qcom_icc_node qhs_lpass_lpi;
static struct qcom_icc_node qhs_lpass_mpu;
static struct qcom_icc_node qhs_lpass_top;
static struct qcom_icc_node qns_sysnoc;
static struct qcom_icc_node srvc_niu_aml_noc;
static struct qcom_icc_node srvc_niu_lpass_agnoc;
static struct qcom_icc_node ebi;
static struct qcom_icc_node qns_mem_noc_hf;
static struct qcom_icc_node qns_mem_noc_sf;
static struct qcom_icc_node srvc_mnoc;
static struct qcom_icc_node qns_nsp_gemnoc;
static struct qcom_icc_node service_nsp_noc;
static struct qcom_icc_node qns_pcie_mem_noc;
static struct qcom_icc_node qns_a2noc_snoc;
static struct qcom_icc_node qns_gemnoc_gc;
static struct qcom_icc_node qns_gemnoc_sf;
static struct qcom_icc_node srvc_snoc;

static const struct regmap_config icc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
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

static struct qcom_icc_node qnm_gemnoc_cnoc = {
	.name = "qnm_gemnoc_cnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 43,
	.link_nodes = { &qhs_ahb2phy0, &qhs_aoss,
			&qhs_camera_cfg, &qhs_clk_ctl,
			&qhs_compute_cfg, &qhs_cpr_cx,
			&qhs_cpr_mmcx, &qhs_cpr_mxa,
			&qhs_cpr_mxc, &qhs_cpr_nspcx,
			&qhs_crypto0_cfg, &qhs_cx_rdpm,
			&qhs_display_cfg, &qhs_gpuss_cfg,
			&qhs_imem_cfg, &qhs_ipc_router,
			&qhs_lpass_cfg, &qhs_mx_rdpm,
			&qhs_pcie0_cfg, &qhs_pcie1_cfg,
			&qhs_pdm, &qhs_pimem_cfg,
			&qhs_prng, &qhs_qdss_cfg,
			&qhs_qspi, &qhs_qup0,
			&qhs_qup1, &qhs_sdc1,
			&qhs_tcsr, &qhs_tlmm,
			&qhs_tme_cfg, &qhs_usb3_0,
			&qhs_venus_cfg, &qhs_vsense_ctrl_cfg,
			&qhs_wlan_q6, &qns_ddrss_cfg,
			&qns_mnoc_cfg, &qns_snoc_cfg,
			&qxs_imem, &qxs_pimem,
			&srvc_cnoc, &xs_qdss_stm,
			&xs_sys_tcu_cfg },
};

static struct qcom_icc_node qnm_gemnoc_pcie = {
	.name = "qnm_gemnoc_pcie",
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.link_nodes = { &xs_pcie_0, &xs_pcie_1 },
};

static struct qcom_icc_node xm_qdss_dap = {
	.name = "xm_qdss_dap",
	.channels = 1,
	.buswidth = 8,
	.num_links = 43,
	.link_nodes = { &qhs_ahb2phy0, &qhs_aoss,
			&qhs_camera_cfg, &qhs_clk_ctl,
			&qhs_compute_cfg, &qhs_cpr_cx,
			&qhs_cpr_mmcx, &qhs_cpr_mxa,
			&qhs_cpr_mxc, &qhs_cpr_nspcx,
			&qhs_crypto0_cfg, &qhs_cx_rdpm,
			&qhs_display_cfg, &qhs_gpuss_cfg,
			&qhs_imem_cfg, &qhs_ipc_router,
			&qhs_lpass_cfg, &qhs_mx_rdpm,
			&qhs_pcie0_cfg, &qhs_pcie1_cfg,
			&qhs_pdm, &qhs_pimem_cfg,
			&qhs_prng, &qhs_qdss_cfg,
			&qhs_qspi, &qhs_qup0,
			&qhs_qup1, &qhs_sdc1,
			&qhs_tcsr, &qhs_tlmm,
			&qhs_tme_cfg, &qhs_usb3_0,
			&qhs_venus_cfg, &qhs_vsense_ctrl_cfg,
			&qhs_wlan_q6, &qns_ddrss_cfg,
			&qns_mnoc_cfg, &qns_snoc_cfg,
			&qxs_imem, &qxs_pimem,
			&srvc_cnoc, &xs_qdss_stm,
			&xs_sys_tcu_cfg },
};

static const struct qcom_icc_qosbox alm_gpu_tcu_qos = {
	.num_ports = 1,
	.port_offsets = { 0x9e000 },
	.prio = 1,
	.urg_fwd = 0,
	.prio_fwd_disable = 1,
};

static struct qcom_icc_node alm_gpu_tcu = {
	.name = "alm_gpu_tcu",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &alm_gpu_tcu_qos,
	.num_links = 2,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc },
};

static const struct qcom_icc_qosbox alm_sys_tcu_qos = {
	.num_ports = 1,
	.port_offsets = { 0x9f000 },
	.prio = 6,
	.urg_fwd = 0,
	.prio_fwd_disable = 1,
};

static struct qcom_icc_node alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &alm_sys_tcu_qos,
	.num_links = 2,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc },
};

static struct qcom_icc_node chm_apps = {
	.name = "chm_apps",
	.channels = 1,
	.buswidth = 32,
	.num_links = 3,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc,
			&qns_pcie },
};

static const struct qcom_icc_qosbox qnm_gpu_qos = {
	.num_ports = 2,
	.port_offsets = { 0xe000, 0x4e000 },
	.prio = 0,
	.urg_fwd = 0,
	.prio_fwd_disable = 1,
};

static struct qcom_icc_node qnm_gpu = {
	.name = "qnm_gpu",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &qnm_gpu_qos,
	.num_links = 2,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc },
};

static const struct qcom_icc_qosbox qnm_mnoc_hf_qos = {
	.num_ports = 2,
	.port_offsets = { 0xf000, 0x4f000 },
	.prio = 0,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_mnoc_hf = {
	.name = "qnm_mnoc_hf",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &qnm_mnoc_hf_qos,
	.num_links = 2,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc },
};

static const struct qcom_icc_qosbox qnm_mnoc_sf_qos = {
	.num_ports = 1,
	.port_offsets = { 0x9d000 },
	.prio = 0,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_mnoc_sf = {
	.name = "qnm_mnoc_sf",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &qnm_mnoc_sf_qos,
	.num_links = 2,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc },
};

static const struct qcom_icc_qosbox qnm_nsp_gemnoc_qos = {
	.num_ports = 2,
	.port_offsets = { 0x10000, 0x50000 },
	.prio = 0,
	.urg_fwd = 0,
	.prio_fwd_disable = 1,
};

static struct qcom_icc_node qnm_nsp_gemnoc = {
	.name = "qnm_nsp_gemnoc",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &qnm_nsp_gemnoc_qos,
	.num_links = 2,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc },
};

static const struct qcom_icc_qosbox qnm_pcie_qos = {
	.num_ports = 1,
	.port_offsets = { 0xa2000 },
	.prio = 2,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_pcie = {
	.name = "qnm_pcie",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &qnm_pcie_qos,
	.num_links = 2,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc },
};

static const struct qcom_icc_qosbox qnm_snoc_gc_qos = {
	.num_ports = 1,
	.port_offsets = { 0xa0000 },
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

static const struct qcom_icc_qosbox qnm_snoc_sf_qos = {
	.num_ports = 1,
	.port_offsets = { 0xa1000 },
	.prio = 0,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_snoc_sf = {
	.name = "qnm_snoc_sf",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &qnm_snoc_sf_qos,
	.num_links = 3,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qxm_wlan_q6 = {
	.name = "qxm_wlan_q6",
	.channels = 1,
	.buswidth = 8,
	.num_links = 3,
	.link_nodes = { &qns_gem_noc_cnoc, &qns_llcc,
			&qns_pcie },
};

static struct qcom_icc_node qhm_config_noc = {
	.name = "qhm_config_noc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 6,
	.link_nodes = { &qhs_lpass_core, &qhs_lpass_lpi,
			&qhs_lpass_mpu, &qhs_lpass_top,
			&srvc_niu_aml_noc, &srvc_niu_lpass_agnoc },
};

static struct qcom_icc_node qxm_lpass_dsp = {
	.name = "qxm_lpass_dsp",
	.channels = 1,
	.buswidth = 8,
	.num_links = 4,
	.link_nodes = { &qhs_lpass_top, &qns_sysnoc,
			&srvc_niu_aml_noc, &srvc_niu_lpass_agnoc },
};

static struct qcom_icc_node llcc_mc = {
	.name = "llcc_mc",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &ebi },
};

static const struct qcom_icc_qosbox qnm_camnoc_hf_qos = {
	.num_ports = 1,
	.port_offsets = { 0x1c000 },
	.prio = 0,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_camnoc_hf = {
	.name = "qnm_camnoc_hf",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &qnm_camnoc_hf_qos,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_hf },
};

static const struct qcom_icc_qosbox qnm_camnoc_icp_qos = {
	.num_ports = 1,
	.port_offsets = { 0x1c080 },
	.prio = 4,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_camnoc_icp = {
	.name = "qnm_camnoc_icp",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &qnm_camnoc_icp_qos,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static const struct qcom_icc_qosbox qnm_camnoc_sf_qos = {
	.num_ports = 1,
	.port_offsets = { 0x1c100 },
	.prio = 0,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_camnoc_sf = {
	.name = "qnm_camnoc_sf",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &qnm_camnoc_sf_qos,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static const struct qcom_icc_qosbox qnm_lsr_qos = {
	.num_ports = 2,
	.port_offsets = { 0x1f000, 0x1f080 },
	.prio = 3,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_lsr = {
	.name = "qnm_lsr",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &qnm_lsr_qos,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_hf },
};

static const struct qcom_icc_qosbox qnm_mdp_qos = {
	.num_ports = 2,
	.port_offsets = { 0x1d000, 0x1d080 },
	.prio = 0,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_mdp = {
	.name = "qnm_mdp",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &qnm_mdp_qos,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_hf },
};

static struct qcom_icc_node qnm_mnoc_cfg = {
	.name = "qnm_mnoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &srvc_mnoc },
};

static const struct qcom_icc_qosbox qnm_video_qos = {
	.num_ports = 2,
	.port_offsets = { 0x1e000, 0x1e080 },
	.prio = 0,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_video = {
	.name = "qnm_video",
	.channels = 2,
	.buswidth = 32,
	.qosbox = &qnm_video_qos,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static const struct qcom_icc_qosbox qnm_video_cv_cpu_qos = {
	.num_ports = 1,
	.port_offsets = { 0x1e100 },
	.prio = 4,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_video_cv_cpu = {
	.name = "qnm_video_cv_cpu",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &qnm_video_cv_cpu_qos,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static const struct qcom_icc_qosbox qnm_video_cvp_qos = {
	.num_ports = 1,
	.port_offsets = { 0x1e180 },
	.prio = 0,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_video_cvp = {
	.name = "qnm_video_cvp",
	.channels = 1,
	.buswidth = 32,
	.qosbox = &qnm_video_cvp_qos,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static const struct qcom_icc_qosbox qnm_video_v_cpu_qos = {
	.num_ports = 1,
	.port_offsets = { 0x1e200 },
	.prio = 4,
	.urg_fwd = 1,
};

static struct qcom_icc_node qnm_video_v_cpu = {
	.name = "qnm_video_v_cpu",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &qnm_video_v_cpu_qos,
	.num_links = 1,
	.link_nodes = { &qns_mem_noc_sf },
};

static struct qcom_icc_node qhm_nsp_noc_config = {
	.name = "qhm_nsp_noc_config",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &service_nsp_noc },
};

static struct qcom_icc_node qxm_nsp = {
	.name = "qxm_nsp",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qns_nsp_gemnoc },
};

static const struct qcom_icc_qosbox xm_pcie3_0_qos = {
	.num_ports = 1,
	.port_offsets = { 0x9000 },
	.prio = 3,
	.urg_fwd = 0,
	.prio_fwd_disable = 1,
};

static struct qcom_icc_node xm_pcie3_0 = {
	.name = "xm_pcie3_0",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &xm_pcie3_0_qos,
	.num_links = 1,
	.link_nodes = { &qns_pcie_mem_noc },
};

static const struct qcom_icc_qosbox xm_pcie3_1_qos = {
	.num_ports = 1,
	.port_offsets = { 0xa000 },
	.prio = 2,
	.urg_fwd = 0,
	.prio_fwd_disable = 1,
};

static struct qcom_icc_node xm_pcie3_1 = {
	.name = "xm_pcie3_1",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &xm_pcie3_1_qos,
	.num_links = 1,
	.link_nodes = { &qns_pcie_mem_noc },
};

static const struct qcom_icc_qosbox qhm_gic_qos = {
	.num_ports = 1,
	.port_offsets = { 0x1d000 },
	.prio = 2,
	.urg_fwd = 0,
	.prio_fwd_disable = 1,
};

static struct qcom_icc_node qhm_gic = {
	.name = "qhm_gic",
	.channels = 1,
	.buswidth = 4,
	.qosbox = &qhm_gic_qos,
	.num_links = 1,
	.link_nodes = { &qns_gemnoc_sf },
};

static const struct qcom_icc_qosbox qhm_qdss_bam_qos = {
	.num_ports = 1,
	.port_offsets = { 0x22000 },
	.prio = 2,
	.urg_fwd = 0,
	.prio_fwd_disable = 1,
};

static struct qcom_icc_node qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.channels = 1,
	.buswidth = 4,
	.qosbox = &qhm_qdss_bam_qos,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static const struct qcom_icc_qosbox qhm_qspi_qos = {
	.num_ports = 1,
	.port_offsets = { 0x23000 },
	.prio = 2,
	.urg_fwd = 0,
	.prio_fwd_disable = 1,
};

static struct qcom_icc_node qhm_qspi = {
	.name = "qhm_qspi",
	.channels = 1,
	.buswidth = 4,
	.qosbox = &qhm_qspi_qos,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static const struct qcom_icc_qosbox qhm_qup0_qos = {
	.num_ports = 1,
	.port_offsets = { 0x24000 },
	.prio = 2,
	.urg_fwd = 0,
	.prio_fwd_disable = 1,
};

static struct qcom_icc_node qhm_qup0 = {
	.name = "qhm_qup0",
	.channels = 1,
	.buswidth = 4,
	.qosbox = &qhm_qup0_qos,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static const struct qcom_icc_qosbox qhm_qup1_qos = {
	.num_ports = 1,
	.port_offsets = { 0x25000 },
	.prio = 2,
	.urg_fwd = 0,
	.prio_fwd_disable = 1,
};

static struct qcom_icc_node qhm_qup1 = {
	.name = "qhm_qup1",
	.channels = 1,
	.buswidth = 4,
	.qosbox = &qhm_qup1_qos,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static struct qcom_icc_node qnm_aggre2_noc = {
	.name = "qnm_aggre2_noc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qns_gemnoc_sf },
};

static const struct qcom_icc_qosbox qnm_cnoc_datapath_qos = {
	.num_ports = 1,
	.port_offsets = { 0x26000 },
	.prio = 2,
	.urg_fwd = 0,
	.prio_fwd_disable = 1,
};

static struct qcom_icc_node qnm_cnoc_datapath = {
	.name = "qnm_cnoc_datapath",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &qnm_cnoc_datapath_qos,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static const struct qcom_icc_qosbox qnm_lpass_noc_qos = {
	.num_ports = 1,
	.port_offsets = { 0x1e000 },
	.prio = 0,
	.urg_fwd = 0,
	.prio_fwd_disable = 1,
};

static struct qcom_icc_node qnm_lpass_noc = {
	.name = "qnm_lpass_noc",
	.channels = 1,
	.buswidth = 16,
	.qosbox = &qnm_lpass_noc_qos,
	.num_links = 1,
	.link_nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_node qnm_snoc_cfg = {
	.name = "qnm_snoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &srvc_snoc },
};

static const struct qcom_icc_qosbox qxm_crypto_qos = {
	.num_ports = 1,
	.port_offsets = { 0x27000 },
	.prio = 2,
	.urg_fwd = 0,
	.prio_fwd_disable = 1,
};

static struct qcom_icc_node qxm_crypto = {
	.name = "qxm_crypto",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &qxm_crypto_qos,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static const struct qcom_icc_qosbox qxm_pimem_qos = {
	.num_ports = 1,
	.port_offsets = { 0x1f000 },
	.prio = 2,
	.urg_fwd = 0,
	.prio_fwd_disable = 1,
};

static struct qcom_icc_node qxm_pimem = {
	.name = "qxm_pimem",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &qxm_pimem_qos,
	.num_links = 1,
	.link_nodes = { &qns_gemnoc_gc },
};

static const struct qcom_icc_qosbox xm_gic_qos = {
	.num_ports = 1,
	.port_offsets = { 0x21000 },
	.prio = 2,
	.urg_fwd = 0,
	.prio_fwd_disable = 1,
};

static struct qcom_icc_node xm_gic = {
	.name = "xm_gic",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &xm_gic_qos,
	.num_links = 1,
	.link_nodes = { &qns_gemnoc_gc },
};

static const struct qcom_icc_qosbox xm_qdss_etr_0_qos = {
	.num_ports = 1,
	.port_offsets = { 0x1b000 },
	.prio = 2,
	.urg_fwd = 0,
	.prio_fwd_disable = 1,
};

static struct qcom_icc_node xm_qdss_etr_0 = {
	.name = "xm_qdss_etr_0",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &xm_qdss_etr_0_qos,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static const struct qcom_icc_qosbox xm_qdss_etr_1_qos = {
	.num_ports = 1,
	.port_offsets = { 0x1c000 },
	.prio = 2,
	.urg_fwd = 0,
	.prio_fwd_disable = 1,
};

static struct qcom_icc_node xm_qdss_etr_1 = {
	.name = "xm_qdss_etr_1",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &xm_qdss_etr_1_qos,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static const struct qcom_icc_qosbox xm_sdc1_qos = {
	.num_ports = 1,
	.port_offsets = { 0x29000 },
	.prio = 2,
	.urg_fwd = 0,
	.prio_fwd_disable = 1,
};

static struct qcom_icc_node xm_sdc1 = {
	.name = "xm_sdc1",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &xm_sdc1_qos,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
};

static const struct qcom_icc_qosbox xm_usb3_0_qos = {
	.num_ports = 1,
	.port_offsets = { 0x28000 },
	.prio = 2,
	.urg_fwd = 0,
	.prio_fwd_disable = 1,
};

static struct qcom_icc_node xm_usb3_0 = {
	.name = "xm_usb3_0",
	.channels = 1,
	.buswidth = 8,
	.qosbox = &xm_usb3_0_qos,
	.num_links = 1,
	.link_nodes = { &qns_a2noc_snoc },
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

static struct qcom_icc_node qhs_ahb2phy0 = {
	.name = "qhs_ahb2phy0",
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

static struct qcom_icc_node qhs_compute_cfg = {
	.name = "qhs_compute_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qhm_nsp_noc_config },
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

static struct qcom_icc_node qhs_cpr_mxa = {
	.name = "qhs_cpr_mxa",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_cpr_mxc = {
	.name = "qhs_cpr_mxc",
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
	.link_nodes = { &qhm_config_noc },
};

static struct qcom_icc_node qhs_mx_rdpm = {
	.name = "qhs_mx_rdpm",
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

static struct qcom_icc_node qhs_sdc1 = {
	.name = "qhs_sdc1",
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

static struct qcom_icc_node qhs_tme_cfg = {
	.name = "qhs_tme_cfg",
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

static struct qcom_icc_node qhs_wlan_q6 = {
	.name = "qhs_wlan_q6",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_ddrss_cfg = {
	.name = "qns_ddrss_cfg",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_mnoc_cfg = {
	.name = "qns_mnoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qnm_mnoc_cfg },
};

static struct qcom_icc_node qns_snoc_cfg = {
	.name = "qns_snoc_cfg",
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.link_nodes = { &qnm_snoc_cfg },
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

static struct qcom_icc_node srvc_cnoc = {
	.name = "srvc_cnoc",
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

static struct qcom_icc_node qns_gem_noc_cnoc = {
	.name = "qns_gem_noc_cnoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_gemnoc_cnoc },
};

static struct qcom_icc_node qns_llcc = {
	.name = "qns_llcc",
	.channels = 2,
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
	.link_nodes = { &qnm_lpass_noc },
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
	.channels = 1,
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

static struct qcom_icc_node qns_nsp_gemnoc = {
	.name = "qns_nsp_gemnoc",
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.link_nodes = { &qnm_nsp_gemnoc },
};

static struct qcom_icc_node service_nsp_noc = {
	.name = "service_nsp_noc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_pcie_mem_noc = {
	.name = "qns_pcie_mem_noc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_pcie },
};

static struct qcom_icc_node qns_a2noc_snoc = {
	.name = "qns_a2noc_snoc",
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.link_nodes = { &qnm_aggre2_noc },
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

static struct qcom_icc_node srvc_snoc = {
	.name = "srvc_snoc",
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.enable_mask = BIT(3),
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
	.num_nodes = 48,
	.nodes = { &qnm_gemnoc_cnoc, &qnm_gemnoc_pcie,
		   &xm_qdss_dap, &qhs_ahb2phy0,
		   &qhs_aoss, &qhs_camera_cfg,
		   &qhs_clk_ctl, &qhs_compute_cfg,
		   &qhs_cpr_cx, &qhs_cpr_mmcx,
		   &qhs_cpr_mxa, &qhs_cpr_mxc,
		   &qhs_cpr_nspcx, &qhs_crypto0_cfg,
		   &qhs_cx_rdpm, &qhs_display_cfg,
		   &qhs_gpuss_cfg, &qhs_imem_cfg,
		   &qhs_ipc_router, &qhs_lpass_cfg,
		   &qhs_mx_rdpm, &qhs_pcie0_cfg,
		   &qhs_pcie1_cfg, &qhs_pdm,
		   &qhs_pimem_cfg, &qhs_prng,
		   &qhs_qdss_cfg, &qhs_qspi,
		   &qhs_qup0, &qhs_qup1,
		   &qhs_sdc1, &qhs_tcsr,
		   &qhs_tlmm, &qhs_tme_cfg,
		   &qhs_usb3_0, &qhs_venus_cfg,
		   &qhs_vsense_ctrl_cfg, &qhs_wlan_q6,
		   &qns_ddrss_cfg, &qns_mnoc_cfg,
		   &qns_snoc_cfg, &qxs_imem,
		   &qxs_pimem, &srvc_cnoc,
		   &xs_pcie_0, &xs_pcie_1,
		   &xs_qdss_stm, &xs_sys_tcu_cfg },
};

static struct qcom_icc_bcm bcm_co0 = {
	.name = "CO0",
	.enable_mask = BIT(0),
	.num_nodes = 2,
	.nodes = { &qxm_nsp, &qns_nsp_gemnoc },
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
	.enable_mask = BIT(0),
	.num_nodes = 11,
	.nodes = { &qnm_camnoc_hf, &qnm_camnoc_icp,
		   &qnm_camnoc_sf, &qnm_lsr,
		   &qnm_mdp, &qnm_mnoc_cfg,
		   &qnm_video, &qnm_video_cv_cpu,
		   &qnm_video_cvp, &qnm_video_v_cpu,
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

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_sh1 = {
	.name = "SH1",
	.enable_mask = BIT(0),
	.num_nodes = 13,
	.nodes = { &alm_gpu_tcu, &alm_sys_tcu,
		   &chm_apps, &qnm_gpu,
		   &qnm_mnoc_hf, &qnm_mnoc_sf,
		   &qnm_nsp_gemnoc, &qnm_pcie,
		   &qnm_snoc_gc, &qnm_snoc_sf,
		   &qxm_wlan_q6, &qns_gem_noc_cnoc,
		   &qns_pcie },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.enable_mask = BIT(0),
	.num_nodes = 4,
	.nodes = { &qhm_gic, &qxm_pimem,
		   &xm_gic, &qns_gemnoc_gc },
};

static struct qcom_icc_bcm bcm_sn3 = {
	.name = "SN3",
	.num_nodes = 1,
	.nodes = { &qnm_aggre2_noc },
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.num_nodes = 1,
	.nodes = { &qnm_lpass_noc },
};

static struct qcom_icc_bcm bcm_sn7 = {
	.name = "SN7",
	.num_nodes = 1,
	.nodes = { &qns_pcie_mem_noc },
};

static struct qcom_icc_bcm * const clk_virt_bcms[] = {
	&bcm_qup0,
	&bcm_qup1,
};

static struct qcom_icc_node * const clk_virt_nodes[] = {
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[MASTER_QUP_CORE_1] = &qup1_core_master,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_QUP_CORE_1] = &qup1_core_slave,
};

static const struct qcom_icc_desc sar2130p_clk_virt = {
	.nodes = clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(clk_virt_nodes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
};

static struct qcom_icc_bcm * const config_noc_bcms[] = {
	&bcm_cn0,
};

static struct qcom_icc_node * const config_noc_nodes[] = {
	[MASTER_GEM_NOC_CNOC] = &qnm_gemnoc_cnoc,
	[MASTER_GEM_NOC_PCIE_SNOC] = &qnm_gemnoc_pcie,
	[MASTER_QDSS_DAP] = &xm_qdss_dap,
	[SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy0,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CDSP_CFG] = &qhs_compute_cfg,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MMCX_CFG] = &qhs_cpr_mmcx,
	[SLAVE_RBCPR_MXA_CFG] = &qhs_cpr_mxa,
	[SLAVE_RBCPR_MXC_CFG] = &qhs_cpr_mxc,
	[SLAVE_CPR_NSPCX] = &qhs_cpr_nspcx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_CX_RDPM] = &qhs_cx_rdpm,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_GFX3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[SLAVE_LPASS] = &qhs_lpass_cfg,
	[SLAVE_MX_RDPM] = &qhs_mx_rdpm,
	[SLAVE_PCIE_0_CFG] = &qhs_pcie0_cfg,
	[SLAVE_PCIE_1_CFG] = &qhs_pcie1_cfg,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QSPI_0] = &qhs_qspi,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_SDCC_1] = &qhs_sdc1,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_TME_CFG] = &qhs_tme_cfg,
	[SLAVE_USB3_0] = &qhs_usb3_0,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_WLAN_Q6_CFG] = &qhs_wlan_q6,
	[SLAVE_DDRSS_CFG] = &qns_ddrss_cfg,
	[SLAVE_CNOC_MNOC_CFG] = &qns_mnoc_cfg,
	[SLAVE_SNOC_CFG] = &qns_snoc_cfg,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_CNOC] = &srvc_cnoc,
	[SLAVE_PCIE_0] = &xs_pcie_0,
	[SLAVE_PCIE_1] = &xs_pcie_1,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sar2130p_config_noc = {
	.config = &icc_regmap_config,
	.nodes = config_noc_nodes,
	.num_nodes = ARRAY_SIZE(config_noc_nodes),
	.bcms = config_noc_bcms,
	.num_bcms = ARRAY_SIZE(config_noc_bcms),
};

static struct qcom_icc_bcm * const gem_noc_bcms[] = {
	&bcm_sh0,
	&bcm_sh1,
};

static struct qcom_icc_node * const gem_noc_nodes[] = {
	[MASTER_GPU_TCU] = &alm_gpu_tcu,
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_GFX3D] = &qnm_gpu,
	[MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[MASTER_MNOC_SF_MEM_NOC] = &qnm_mnoc_sf,
	[MASTER_COMPUTE_NOC] = &qnm_nsp_gemnoc,
	[MASTER_ANOC_PCIE_GEM_NOC] = &qnm_pcie,
	[MASTER_SNOC_GC_MEM_NOC] = &qnm_snoc_gc,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[MASTER_WLAN_Q6] = &qxm_wlan_q6,
	[SLAVE_GEM_NOC_CNOC] = &qns_gem_noc_cnoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MEM_NOC_PCIE_SNOC] = &qns_pcie,
};

static const struct qcom_icc_desc sar2130p_gem_noc = {
	.config = &icc_regmap_config,
	.nodes = gem_noc_nodes,
	.num_nodes = ARRAY_SIZE(gem_noc_nodes),
	.bcms = gem_noc_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_bcms),
};

static struct qcom_icc_bcm * const lpass_ag_noc_bcms[] = {
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

static const struct qcom_icc_desc sar2130p_lpass_ag_noc = {
	.config = &icc_regmap_config,
	.nodes = lpass_ag_noc_nodes,
	.num_nodes = ARRAY_SIZE(lpass_ag_noc_nodes),
	.bcms = lpass_ag_noc_bcms,
	.num_bcms = ARRAY_SIZE(lpass_ag_noc_bcms),
};

static struct qcom_icc_bcm * const mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
};

static struct qcom_icc_node * const mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
};

static const struct qcom_icc_desc sar2130p_mc_virt = {
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
	[MASTER_CAMNOC_ICP] = &qnm_camnoc_icp,
	[MASTER_CAMNOC_SF] = &qnm_camnoc_sf,
	[MASTER_LSR] = &qnm_lsr,
	[MASTER_MDP] = &qnm_mdp,
	[MASTER_CNOC_MNOC_CFG] = &qnm_mnoc_cfg,
	[MASTER_VIDEO] = &qnm_video,
	[MASTER_VIDEO_CV_PROC] = &qnm_video_cv_cpu,
	[MASTER_VIDEO_PROC] = &qnm_video_cvp,
	[MASTER_VIDEO_V_PROC] = &qnm_video_v_cpu,
	[SLAVE_MNOC_HF_MEM_NOC] = &qns_mem_noc_hf,
	[SLAVE_MNOC_SF_MEM_NOC] = &qns_mem_noc_sf,
	[SLAVE_SERVICE_MNOC] = &srvc_mnoc,
};

static const struct qcom_icc_desc sar2130p_mmss_noc = {
	.config = &icc_regmap_config,
	.nodes = mmss_noc_nodes,
	.num_nodes = ARRAY_SIZE(mmss_noc_nodes),
	.bcms = mmss_noc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_bcms),
};

static struct qcom_icc_bcm * const nsp_noc_bcms[] = {
	&bcm_co0,
};

static struct qcom_icc_node * const nsp_noc_nodes[] = {
	[MASTER_CDSP_NOC_CFG] = &qhm_nsp_noc_config,
	[MASTER_CDSP_PROC] = &qxm_nsp,
	[SLAVE_CDSP_MEM_NOC] = &qns_nsp_gemnoc,
	[SLAVE_SERVICE_NSP_NOC] = &service_nsp_noc,
};

static const struct qcom_icc_desc sar2130p_nsp_noc = {
	.config = &icc_regmap_config,
	.nodes = nsp_noc_nodes,
	.num_nodes = ARRAY_SIZE(nsp_noc_nodes),
	.bcms = nsp_noc_bcms,
	.num_bcms = ARRAY_SIZE(nsp_noc_bcms),
};

static struct qcom_icc_bcm * const pcie_anoc_bcms[] = {
	&bcm_sn7,
};

static struct qcom_icc_node * const pcie_anoc_nodes[] = {
	[MASTER_PCIE_0] = &xm_pcie3_0,
	[MASTER_PCIE_1] = &xm_pcie3_1,
	[SLAVE_ANOC_PCIE_GEM_NOC] = &qns_pcie_mem_noc,
};

static const struct qcom_icc_desc sar2130p_pcie_anoc = {
	.config = &icc_regmap_config,
	.nodes = pcie_anoc_nodes,
	.num_nodes = ARRAY_SIZE(pcie_anoc_nodes),
	.bcms = pcie_anoc_bcms,
	.num_bcms = ARRAY_SIZE(pcie_anoc_bcms),
};

static struct qcom_icc_bcm * const system_noc_bcms[] = {
	&bcm_ce0,
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn3,
	&bcm_sn4,
};

static struct qcom_icc_node * const system_noc_nodes[] = {
	[MASTER_GIC_AHB] = &qhm_gic,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QSPI_0] = &qhm_qspi,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_A2NOC_SNOC] = &qnm_aggre2_noc,
	[MASTER_CNOC_DATAPATH] = &qnm_cnoc_datapath,
	[MASTER_LPASS_ANOC] = &qnm_lpass_noc,
	[MASTER_SNOC_CFG] = &qnm_snoc_cfg,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_GIC] = &xm_gic,
	[MASTER_QDSS_ETR] = &xm_qdss_etr_0,
	[MASTER_QDSS_ETR_1] = &xm_qdss_etr_1,
	[MASTER_SDCC_1] = &xm_sdc1,
	[MASTER_USB3_0] = &xm_usb3_0,
	[SLAVE_A2NOC_SNOC] = &qns_a2noc_snoc,
	[SLAVE_SNOC_GEM_NOC_GC] = &qns_gemnoc_gc,
	[SLAVE_SNOC_GEM_NOC_SF] = &qns_gemnoc_sf,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
};

static const struct qcom_icc_desc sar2130p_system_noc = {
	.config = &icc_regmap_config,
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sar2130p-clk-virt", .data = &sar2130p_clk_virt},
	{ .compatible = "qcom,sar2130p-config-noc", .data = &sar2130p_config_noc},
	{ .compatible = "qcom,sar2130p-gem-noc", .data = &sar2130p_gem_noc},
	{ .compatible = "qcom,sar2130p-lpass-ag-noc", .data = &sar2130p_lpass_ag_noc},
	{ .compatible = "qcom,sar2130p-mc-virt", .data = &sar2130p_mc_virt},
	{ .compatible = "qcom,sar2130p-mmss-noc", .data = &sar2130p_mmss_noc},
	{ .compatible = "qcom,sar2130p-nsp-noc", .data = &sar2130p_nsp_noc},
	{ .compatible = "qcom,sar2130p-pcie-anoc", .data = &sar2130p_pcie_anoc},
	{ .compatible = "qcom,sar2130p-system-noc", .data = &sar2130p_system_noc},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-sar2130p",
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
MODULE_DESCRIPTION("Qualcomm SAR2130P NoC driver");
MODULE_LICENSE("GPL");
