// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021, Linaro Limited
 *
 */

#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <dt-bindings/interconnect/qcom,sm8350.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"
#include "sm8350.h"

DEFINE_QNODE(qhm_qspi, SM8350_MASTER_QSPI_0, 1, 4, SM8350_SLAVE_A1NOC_SNOC);
DEFINE_QNODE(qhm_qup0, SM8350_MASTER_QUP_0, 1, 4, SM8350_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(qhm_qup1, SM8350_MASTER_QUP_1, 1, 4, SM8350_SLAVE_A1NOC_SNOC);
DEFINE_QNODE(qhm_qup2, SM8350_MASTER_QUP_2, 1, 4, SM8350_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(qnm_a1noc_cfg, SM8350_MASTER_A1NOC_CFG, 1, 4, SM8350_SLAVE_SERVICE_A1NOC);
DEFINE_QNODE(xm_sdc4, SM8350_MASTER_SDCC_4, 1, 8, SM8350_SLAVE_A1NOC_SNOC);
DEFINE_QNODE(xm_ufs_mem, SM8350_MASTER_UFS_MEM, 1, 8, SM8350_SLAVE_A1NOC_SNOC);
DEFINE_QNODE(xm_usb3_0, SM8350_MASTER_USB3_0, 1, 8, SM8350_SLAVE_A1NOC_SNOC);
DEFINE_QNODE(xm_usb3_1, SM8350_MASTER_USB3_1, 1, 8, SM8350_SLAVE_A1NOC_SNOC);
DEFINE_QNODE(qhm_qdss_bam, SM8350_MASTER_QDSS_BAM, 1, 4, SM8350_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(qnm_a2noc_cfg, SM8350_MASTER_A2NOC_CFG, 1, 4, SM8350_SLAVE_SERVICE_A2NOC);
DEFINE_QNODE(qxm_crypto, SM8350_MASTER_CRYPTO, 1, 8, SM8350_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(qxm_ipa, SM8350_MASTER_IPA, 1, 8, SM8350_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(xm_pcie3_0, SM8350_MASTER_PCIE_0, 1, 8, SM8350_SLAVE_ANOC_PCIE_GEM_NOC);
DEFINE_QNODE(xm_pcie3_1, SM8350_MASTER_PCIE_1, 1, 8, SM8350_SLAVE_ANOC_PCIE_GEM_NOC);
DEFINE_QNODE(xm_qdss_etr, SM8350_MASTER_QDSS_ETR, 1, 8, SM8350_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(xm_sdc2, SM8350_MASTER_SDCC_2, 1, 8, SM8350_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(xm_ufs_card, SM8350_MASTER_UFS_CARD, 1, 8, SM8350_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(qnm_gemnoc_cnoc, SM8350_MASTER_GEM_NOC_CNOC, 1, 16, SM8350_SLAVE_AHB2PHY_SOUTH, SM8350_SLAVE_AHB2PHY_NORTH, SM8350_SLAVE_AOSS, SM8350_SLAVE_APPSS, SM8350_SLAVE_CAMERA_CFG, SM8350_SLAVE_CLK_CTL, SM8350_SLAVE_CDSP_CFG, SM8350_SLAVE_RBCPR_CX_CFG, SM8350_SLAVE_RBCPR_MMCX_CFG, SM8350_SLAVE_RBCPR_MX_CFG, SM8350_SLAVE_CRYPTO_0_CFG, SM8350_SLAVE_CX_RDPM, SM8350_SLAVE_DCC_CFG, SM8350_SLAVE_DISPLAY_CFG, SM8350_SLAVE_GFX3D_CFG, SM8350_SLAVE_HWKM, SM8350_SLAVE_IMEM_CFG, SM8350_SLAVE_IPA_CFG, SM8350_SLAVE_IPC_ROUTER_CFG, SM8350_SLAVE_LPASS, SM8350_SLAVE_CNOC_MSS, SM8350_SLAVE_MX_RDPM, SM8350_SLAVE_PCIE_0_CFG, SM8350_SLAVE_PCIE_1_CFG, SM8350_SLAVE_PDM, SM8350_SLAVE_PIMEM_CFG, SM8350_SLAVE_PKA_WRAPPER_CFG, SM8350_SLAVE_PMU_WRAPPER_CFG, SM8350_SLAVE_QDSS_CFG, SM8350_SLAVE_QSPI_0, SM8350_SLAVE_QUP_0, SM8350_SLAVE_QUP_1, SM8350_SLAVE_QUP_2, SM8350_SLAVE_SDCC_2, SM8350_SLAVE_SDCC_4, SM8350_SLAVE_SECURITY, SM8350_SLAVE_SPSS_CFG, SM8350_SLAVE_TCSR, SM8350_SLAVE_TLMM, SM8350_SLAVE_UFS_CARD_CFG, SM8350_SLAVE_UFS_MEM_CFG, SM8350_SLAVE_USB3_0, SM8350_SLAVE_USB3_1, SM8350_SLAVE_VENUS_CFG, SM8350_SLAVE_VSENSE_CTRL_CFG, SM8350_SLAVE_A1NOC_CFG, SM8350_SLAVE_A2NOC_CFG, SM8350_SLAVE_DDRSS_CFG, SM8350_SLAVE_CNOC_MNOC_CFG, SM8350_SLAVE_SNOC_CFG, SM8350_SLAVE_BOOT_IMEM, SM8350_SLAVE_IMEM, SM8350_SLAVE_PIMEM, SM8350_SLAVE_SERVICE_CNOC, SM8350_SLAVE_QDSS_STM, SM8350_SLAVE_TCU);
DEFINE_QNODE(qnm_gemnoc_pcie, SM8350_MASTER_GEM_NOC_PCIE_SNOC, 1, 8, SM8350_SLAVE_PCIE_0, SM8350_SLAVE_PCIE_1);
DEFINE_QNODE(xm_qdss_dap, SM8350_MASTER_QDSS_DAP, 1, 8, SM8350_SLAVE_AHB2PHY_SOUTH, SM8350_SLAVE_AHB2PHY_NORTH, SM8350_SLAVE_AOSS, SM8350_SLAVE_APPSS, SM8350_SLAVE_CAMERA_CFG, SM8350_SLAVE_CLK_CTL, SM8350_SLAVE_CDSP_CFG, SM8350_SLAVE_RBCPR_CX_CFG, SM8350_SLAVE_RBCPR_MMCX_CFG, SM8350_SLAVE_RBCPR_MX_CFG, SM8350_SLAVE_CRYPTO_0_CFG, SM8350_SLAVE_CX_RDPM, SM8350_SLAVE_DCC_CFG, SM8350_SLAVE_DISPLAY_CFG, SM8350_SLAVE_GFX3D_CFG, SM8350_SLAVE_HWKM, SM8350_SLAVE_IMEM_CFG, SM8350_SLAVE_IPA_CFG, SM8350_SLAVE_IPC_ROUTER_CFG, SM8350_SLAVE_LPASS, SM8350_SLAVE_CNOC_MSS, SM8350_SLAVE_MX_RDPM, SM8350_SLAVE_PCIE_0_CFG, SM8350_SLAVE_PCIE_1_CFG, SM8350_SLAVE_PDM, SM8350_SLAVE_PIMEM_CFG, SM8350_SLAVE_PKA_WRAPPER_CFG, SM8350_SLAVE_PMU_WRAPPER_CFG, SM8350_SLAVE_QDSS_CFG, SM8350_SLAVE_QSPI_0, SM8350_SLAVE_QUP_0, SM8350_SLAVE_QUP_1, SM8350_SLAVE_QUP_2, SM8350_SLAVE_SDCC_2, SM8350_SLAVE_SDCC_4, SM8350_SLAVE_SECURITY, SM8350_SLAVE_SPSS_CFG, SM8350_SLAVE_TCSR, SM8350_SLAVE_TLMM, SM8350_SLAVE_UFS_CARD_CFG, SM8350_SLAVE_UFS_MEM_CFG, SM8350_SLAVE_USB3_0, SM8350_SLAVE_USB3_1, SM8350_SLAVE_VENUS_CFG, SM8350_SLAVE_VSENSE_CTRL_CFG, SM8350_SLAVE_A1NOC_CFG, SM8350_SLAVE_A2NOC_CFG, SM8350_SLAVE_DDRSS_CFG, SM8350_SLAVE_CNOC_MNOC_CFG, SM8350_SLAVE_SNOC_CFG, SM8350_SLAVE_BOOT_IMEM, SM8350_SLAVE_IMEM, SM8350_SLAVE_PIMEM, SM8350_SLAVE_SERVICE_CNOC, SM8350_SLAVE_QDSS_STM, SM8350_SLAVE_TCU);
DEFINE_QNODE(qnm_cnoc_dc_noc, SM8350_MASTER_CNOC_DC_NOC, 1, 4, SM8350_SLAVE_LLCC_CFG, SM8350_SLAVE_GEM_NOC_CFG);
DEFINE_QNODE(alm_gpu_tcu, SM8350_MASTER_GPU_TCU, 1, 8, SM8350_SLAVE_GEM_NOC_CNOC, SM8350_SLAVE_LLCC);
DEFINE_QNODE(alm_sys_tcu, SM8350_MASTER_SYS_TCU, 1, 8, SM8350_SLAVE_GEM_NOC_CNOC, SM8350_SLAVE_LLCC);
DEFINE_QNODE(chm_apps, SM8350_MASTER_APPSS_PROC, 2, 32, SM8350_SLAVE_GEM_NOC_CNOC, SM8350_SLAVE_LLCC, SM8350_SLAVE_MEM_NOC_PCIE_SNOC);
DEFINE_QNODE(qnm_cmpnoc, SM8350_MASTER_COMPUTE_NOC, 2, 32, SM8350_SLAVE_GEM_NOC_CNOC, SM8350_SLAVE_LLCC);
DEFINE_QNODE(qnm_gemnoc_cfg, SM8350_MASTER_GEM_NOC_CFG, 1, 4, SM8350_SLAVE_MSS_PROC_MS_MPU_CFG, SM8350_SLAVE_MCDMA_MS_MPU_CFG, SM8350_SLAVE_SERVICE_GEM_NOC_1, SM8350_SLAVE_SERVICE_GEM_NOC_2, SM8350_SLAVE_SERVICE_GEM_NOC);
DEFINE_QNODE(qnm_gpu, SM8350_MASTER_GFX3D, 2, 32, SM8350_SLAVE_GEM_NOC_CNOC, SM8350_SLAVE_LLCC);
DEFINE_QNODE(qnm_mnoc_hf, SM8350_MASTER_MNOC_HF_MEM_NOC, 2, 32, SM8350_SLAVE_LLCC);
DEFINE_QNODE(qnm_mnoc_sf, SM8350_MASTER_MNOC_SF_MEM_NOC, 2, 32, SM8350_SLAVE_GEM_NOC_CNOC, SM8350_SLAVE_LLCC);
DEFINE_QNODE(qnm_pcie, SM8350_MASTER_ANOC_PCIE_GEM_NOC, 1, 16, SM8350_SLAVE_GEM_NOC_CNOC, SM8350_SLAVE_LLCC);
DEFINE_QNODE(qnm_snoc_gc, SM8350_MASTER_SNOC_GC_MEM_NOC, 1, 8, SM8350_SLAVE_LLCC);
DEFINE_QNODE(qnm_snoc_sf, SM8350_MASTER_SNOC_SF_MEM_NOC, 1, 16, SM8350_SLAVE_GEM_NOC_CNOC, SM8350_SLAVE_LLCC, SM8350_SLAVE_MEM_NOC_PCIE_SNOC);
DEFINE_QNODE(qhm_config_noc, SM8350_MASTER_CNOC_LPASS_AG_NOC, 1, 4, SM8350_SLAVE_LPASS_CORE_CFG, SM8350_SLAVE_LPASS_LPI_CFG, SM8350_SLAVE_LPASS_MPU_CFG, SM8350_SLAVE_LPASS_TOP_CFG, SM8350_SLAVE_SERVICES_LPASS_AML_NOC, SM8350_SLAVE_SERVICE_LPASS_AG_NOC);
DEFINE_QNODE(llcc_mc, SM8350_MASTER_LLCC, 4, 4, SM8350_SLAVE_EBI1);
DEFINE_QNODE(qnm_camnoc_hf, SM8350_MASTER_CAMNOC_HF, 2, 32, SM8350_SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(qnm_camnoc_icp, SM8350_MASTER_CAMNOC_ICP, 1, 8, SM8350_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qnm_camnoc_sf, SM8350_MASTER_CAMNOC_SF, 2, 32, SM8350_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qnm_mnoc_cfg, SM8350_MASTER_CNOC_MNOC_CFG, 1, 4, SM8350_SLAVE_SERVICE_MNOC);
DEFINE_QNODE(qnm_video0, SM8350_MASTER_VIDEO_P0, 1, 32, SM8350_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qnm_video1, SM8350_MASTER_VIDEO_P1, 1, 32, SM8350_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qnm_video_cvp, SM8350_MASTER_VIDEO_PROC, 1, 32, SM8350_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qxm_mdp0, SM8350_MASTER_MDP0, 1, 32, SM8350_SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(qxm_mdp1, SM8350_MASTER_MDP1, 1, 32, SM8350_SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(qxm_rot, SM8350_MASTER_ROTATOR, 1, 32, SM8350_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qhm_nsp_noc_config, SM8350_MASTER_CDSP_NOC_CFG, 1, 4, SM8350_SLAVE_SERVICE_NSP_NOC);
DEFINE_QNODE(qxm_nsp, SM8350_MASTER_CDSP_PROC, 2, 32, SM8350_SLAVE_CDSP_MEM_NOC);
DEFINE_QNODE(qnm_aggre1_noc, SM8350_MASTER_A1NOC_SNOC, 1, 16, SM8350_SLAVE_SNOC_GEM_NOC_SF);
DEFINE_QNODE(qnm_aggre2_noc, SM8350_MASTER_A2NOC_SNOC, 1, 16, SM8350_SLAVE_SNOC_GEM_NOC_SF);
DEFINE_QNODE(qnm_snoc_cfg, SM8350_MASTER_SNOC_CFG, 1, 4, SM8350_SLAVE_SERVICE_SNOC);
DEFINE_QNODE(qxm_pimem, SM8350_MASTER_PIMEM, 1, 8, SM8350_SLAVE_SNOC_GEM_NOC_GC);
DEFINE_QNODE(xm_gic, SM8350_MASTER_GIC, 1, 8, SM8350_SLAVE_SNOC_GEM_NOC_GC);
DEFINE_QNODE(qnm_mnoc_hf_disp, SM8350_MASTER_MNOC_HF_MEM_NOC_DISP, 2, 32, SM8350_SLAVE_LLCC_DISP);
DEFINE_QNODE(qnm_mnoc_sf_disp, SM8350_MASTER_MNOC_SF_MEM_NOC_DISP, 2, 32, SM8350_SLAVE_LLCC_DISP);
DEFINE_QNODE(llcc_mc_disp, SM8350_MASTER_LLCC_DISP, 4, 4, SM8350_SLAVE_EBI1_DISP);
DEFINE_QNODE(qxm_mdp0_disp, SM8350_MASTER_MDP0_DISP, 1, 32, SM8350_SLAVE_MNOC_HF_MEM_NOC_DISP);
DEFINE_QNODE(qxm_mdp1_disp, SM8350_MASTER_MDP1_DISP, 1, 32, SM8350_SLAVE_MNOC_HF_MEM_NOC_DISP);
DEFINE_QNODE(qxm_rot_disp, SM8350_MASTER_ROTATOR_DISP, 1, 32, SM8350_SLAVE_MNOC_SF_MEM_NOC_DISP);
DEFINE_QNODE(qns_a1noc_snoc, SM8350_SLAVE_A1NOC_SNOC, 1, 16, SM8350_MASTER_A1NOC_SNOC);
DEFINE_QNODE(srvc_aggre1_noc, SM8350_SLAVE_SERVICE_A1NOC, 1, 4);
DEFINE_QNODE(qns_a2noc_snoc, SM8350_SLAVE_A2NOC_SNOC, 1, 16, SM8350_MASTER_A2NOC_SNOC);
DEFINE_QNODE(qns_pcie_mem_noc, SM8350_SLAVE_ANOC_PCIE_GEM_NOC, 1, 16, SM8350_MASTER_ANOC_PCIE_GEM_NOC);
DEFINE_QNODE(srvc_aggre2_noc, SM8350_SLAVE_SERVICE_A2NOC, 1, 4);
DEFINE_QNODE(qhs_ahb2phy0, SM8350_SLAVE_AHB2PHY_SOUTH, 1, 4);
DEFINE_QNODE(qhs_ahb2phy1, SM8350_SLAVE_AHB2PHY_NORTH, 1, 4);
DEFINE_QNODE(qhs_aoss, SM8350_SLAVE_AOSS, 1, 4);
DEFINE_QNODE(qhs_apss, SM8350_SLAVE_APPSS, 1, 8);
DEFINE_QNODE(qhs_camera_cfg, SM8350_SLAVE_CAMERA_CFG, 1, 4);
DEFINE_QNODE(qhs_clk_ctl, SM8350_SLAVE_CLK_CTL, 1, 4);
DEFINE_QNODE(qhs_compute_cfg, SM8350_SLAVE_CDSP_CFG, 1, 4);
DEFINE_QNODE(qhs_cpr_cx, SM8350_SLAVE_RBCPR_CX_CFG, 1, 4);
DEFINE_QNODE(qhs_cpr_mmcx, SM8350_SLAVE_RBCPR_MMCX_CFG, 1, 4);
DEFINE_QNODE(qhs_cpr_mx, SM8350_SLAVE_RBCPR_MX_CFG, 1, 4);
DEFINE_QNODE(qhs_crypto0_cfg, SM8350_SLAVE_CRYPTO_0_CFG, 1, 4);
DEFINE_QNODE(qhs_cx_rdpm, SM8350_SLAVE_CX_RDPM, 1, 4);
DEFINE_QNODE(qhs_dcc_cfg, SM8350_SLAVE_DCC_CFG, 1, 4);
DEFINE_QNODE(qhs_display_cfg, SM8350_SLAVE_DISPLAY_CFG, 1, 4);
DEFINE_QNODE(qhs_gpuss_cfg, SM8350_SLAVE_GFX3D_CFG, 1, 8);
DEFINE_QNODE(qhs_hwkm, SM8350_SLAVE_HWKM, 1, 4);
DEFINE_QNODE(qhs_imem_cfg, SM8350_SLAVE_IMEM_CFG, 1, 4);
DEFINE_QNODE(qhs_ipa, SM8350_SLAVE_IPA_CFG, 1, 4);
DEFINE_QNODE(qhs_ipc_router, SM8350_SLAVE_IPC_ROUTER_CFG, 1, 4);
DEFINE_QNODE(qhs_lpass_cfg, SM8350_SLAVE_LPASS, 1, 4, SM8350_MASTER_CNOC_LPASS_AG_NOC);
DEFINE_QNODE(qhs_mss_cfg, SM8350_SLAVE_CNOC_MSS, 1, 4);
DEFINE_QNODE(qhs_mx_rdpm, SM8350_SLAVE_MX_RDPM, 1, 4);
DEFINE_QNODE(qhs_pcie0_cfg, SM8350_SLAVE_PCIE_0_CFG, 1, 4);
DEFINE_QNODE(qhs_pcie1_cfg, SM8350_SLAVE_PCIE_1_CFG, 1, 4);
DEFINE_QNODE(qhs_pdm, SM8350_SLAVE_PDM, 1, 4);
DEFINE_QNODE(qhs_pimem_cfg, SM8350_SLAVE_PIMEM_CFG, 1, 4);
DEFINE_QNODE(qhs_pka_wrapper_cfg, SM8350_SLAVE_PKA_WRAPPER_CFG, 1, 4);
DEFINE_QNODE(qhs_pmu_wrapper_cfg, SM8350_SLAVE_PMU_WRAPPER_CFG, 1, 4);
DEFINE_QNODE(qhs_qdss_cfg, SM8350_SLAVE_QDSS_CFG, 1, 4);
DEFINE_QNODE(qhs_qspi, SM8350_SLAVE_QSPI_0, 1, 4);
DEFINE_QNODE(qhs_qup0, SM8350_SLAVE_QUP_0, 1, 4);
DEFINE_QNODE(qhs_qup1, SM8350_SLAVE_QUP_1, 1, 4);
DEFINE_QNODE(qhs_qup2, SM8350_SLAVE_QUP_2, 1, 4);
DEFINE_QNODE(qhs_sdc2, SM8350_SLAVE_SDCC_2, 1, 4);
DEFINE_QNODE(qhs_sdc4, SM8350_SLAVE_SDCC_4, 1, 4);
DEFINE_QNODE(qhs_security, SM8350_SLAVE_SECURITY, 1, 4);
DEFINE_QNODE(qhs_spss_cfg, SM8350_SLAVE_SPSS_CFG, 1, 4);
DEFINE_QNODE(qhs_tcsr, SM8350_SLAVE_TCSR, 1, 4);
DEFINE_QNODE(qhs_tlmm, SM8350_SLAVE_TLMM, 1, 4);
DEFINE_QNODE(qhs_ufs_card_cfg, SM8350_SLAVE_UFS_CARD_CFG, 1, 4);
DEFINE_QNODE(qhs_ufs_mem_cfg, SM8350_SLAVE_UFS_MEM_CFG, 1, 4);
DEFINE_QNODE(qhs_usb3_0, SM8350_SLAVE_USB3_0, 1, 4);
DEFINE_QNODE(qhs_usb3_1, SM8350_SLAVE_USB3_1, 1, 4);
DEFINE_QNODE(qhs_venus_cfg, SM8350_SLAVE_VENUS_CFG, 1, 4);
DEFINE_QNODE(qhs_vsense_ctrl_cfg, SM8350_SLAVE_VSENSE_CTRL_CFG, 1, 4);
DEFINE_QNODE(qns_a1_noc_cfg, SM8350_SLAVE_A1NOC_CFG, 1, 4);
DEFINE_QNODE(qns_a2_noc_cfg, SM8350_SLAVE_A2NOC_CFG, 1, 4);
DEFINE_QNODE(qns_ddrss_cfg, SM8350_SLAVE_DDRSS_CFG, 1, 4);
DEFINE_QNODE(qns_mnoc_cfg, SM8350_SLAVE_CNOC_MNOC_CFG, 1, 4);
DEFINE_QNODE(qns_snoc_cfg, SM8350_SLAVE_SNOC_CFG, 1, 4);
DEFINE_QNODE(qxs_boot_imem, SM8350_SLAVE_BOOT_IMEM, 1, 8);
DEFINE_QNODE(qxs_imem, SM8350_SLAVE_IMEM, 1, 8);
DEFINE_QNODE(qxs_pimem, SM8350_SLAVE_PIMEM, 1, 8);
DEFINE_QNODE(srvc_cnoc, SM8350_SLAVE_SERVICE_CNOC, 1, 4);
DEFINE_QNODE(xs_pcie_0, SM8350_SLAVE_PCIE_0, 1, 8);
DEFINE_QNODE(xs_pcie_1, SM8350_SLAVE_PCIE_1, 1, 8);
DEFINE_QNODE(xs_qdss_stm, SM8350_SLAVE_QDSS_STM, 1, 4);
DEFINE_QNODE(xs_sys_tcu_cfg, SM8350_SLAVE_TCU, 1, 8);
DEFINE_QNODE(qhs_llcc, SM8350_SLAVE_LLCC_CFG, 1, 4);
DEFINE_QNODE(qns_gemnoc, SM8350_SLAVE_GEM_NOC_CFG, 1, 4);
DEFINE_QNODE(qhs_mdsp_ms_mpu_cfg, SM8350_SLAVE_MSS_PROC_MS_MPU_CFG, 1, 4);
DEFINE_QNODE(qhs_modem_ms_mpu_cfg, SM8350_SLAVE_MCDMA_MS_MPU_CFG, 1, 4);
DEFINE_QNODE(qns_gem_noc_cnoc, SM8350_SLAVE_GEM_NOC_CNOC, 1, 16, SM8350_MASTER_GEM_NOC_CNOC);
DEFINE_QNODE(qns_llcc, SM8350_SLAVE_LLCC, 4, 16, SM8350_MASTER_LLCC);
DEFINE_QNODE(qns_pcie, SM8350_SLAVE_MEM_NOC_PCIE_SNOC, 1, 8);
DEFINE_QNODE(srvc_even_gemnoc, SM8350_SLAVE_SERVICE_GEM_NOC_1, 1, 4);
DEFINE_QNODE(srvc_odd_gemnoc, SM8350_SLAVE_SERVICE_GEM_NOC_2, 1, 4);
DEFINE_QNODE(srvc_sys_gemnoc, SM8350_SLAVE_SERVICE_GEM_NOC, 1, 4);
DEFINE_QNODE(qhs_lpass_core, SM8350_SLAVE_LPASS_CORE_CFG, 1, 4);
DEFINE_QNODE(qhs_lpass_lpi, SM8350_SLAVE_LPASS_LPI_CFG, 1, 4);
DEFINE_QNODE(qhs_lpass_mpu, SM8350_SLAVE_LPASS_MPU_CFG, 1, 4);
DEFINE_QNODE(qhs_lpass_top, SM8350_SLAVE_LPASS_TOP_CFG, 1, 4);
DEFINE_QNODE(srvc_niu_aml_noc, SM8350_SLAVE_SERVICES_LPASS_AML_NOC, 1, 4);
DEFINE_QNODE(srvc_niu_lpass_agnoc, SM8350_SLAVE_SERVICE_LPASS_AG_NOC, 1, 4);
DEFINE_QNODE(ebi, SM8350_SLAVE_EBI1, 4, 4);
DEFINE_QNODE(qns_mem_noc_hf, SM8350_SLAVE_MNOC_HF_MEM_NOC, 2, 32, SM8350_MASTER_MNOC_HF_MEM_NOC);
DEFINE_QNODE(qns_mem_noc_sf, SM8350_SLAVE_MNOC_SF_MEM_NOC, 2, 32, SM8350_MASTER_MNOC_SF_MEM_NOC);
DEFINE_QNODE(srvc_mnoc, SM8350_SLAVE_SERVICE_MNOC, 1, 4);
DEFINE_QNODE(qns_nsp_gemnoc, SM8350_SLAVE_CDSP_MEM_NOC, 2, 32, SM8350_MASTER_COMPUTE_NOC);
DEFINE_QNODE(service_nsp_noc, SM8350_SLAVE_SERVICE_NSP_NOC, 1, 4);
DEFINE_QNODE(qns_gemnoc_gc, SM8350_SLAVE_SNOC_GEM_NOC_GC, 1, 8, SM8350_MASTER_SNOC_GC_MEM_NOC);
DEFINE_QNODE(qns_gemnoc_sf, SM8350_SLAVE_SNOC_GEM_NOC_SF, 1, 16, SM8350_MASTER_SNOC_SF_MEM_NOC);
DEFINE_QNODE(srvc_snoc, SM8350_SLAVE_SERVICE_SNOC, 1, 4);
DEFINE_QNODE(qns_llcc_disp, SM8350_SLAVE_LLCC_DISP, 4, 16, SM8350_MASTER_LLCC_DISP);
DEFINE_QNODE(ebi_disp, SM8350_SLAVE_EBI1_DISP, 4, 4);
DEFINE_QNODE(qns_mem_noc_hf_disp, SM8350_SLAVE_MNOC_HF_MEM_NOC_DISP, 2, 32, SM8350_MASTER_MNOC_HF_MEM_NOC_DISP);
DEFINE_QNODE(qns_mem_noc_sf_disp, SM8350_SLAVE_MNOC_SF_MEM_NOC_DISP, 2, 32, SM8350_MASTER_MNOC_SF_MEM_NOC_DISP);

DEFINE_QBCM(bcm_acv, "ACV", false, &ebi);
DEFINE_QBCM(bcm_ce0, "CE0", false, &qxm_crypto);
DEFINE_QBCM(bcm_cn0, "CN0", true, &qnm_gemnoc_cnoc, &qnm_gemnoc_pcie);
DEFINE_QBCM(bcm_cn1, "CN1", false, &xm_qdss_dap, &qhs_ahb2phy0, &qhs_ahb2phy1, &qhs_aoss, &qhs_apss, &qhs_camera_cfg, &qhs_clk_ctl, &qhs_compute_cfg, &qhs_cpr_cx, &qhs_cpr_mmcx, &qhs_cpr_mx, &qhs_crypto0_cfg, &qhs_cx_rdpm, &qhs_dcc_cfg, &qhs_display_cfg, &qhs_gpuss_cfg, &qhs_hwkm, &qhs_imem_cfg, &qhs_ipa, &qhs_ipc_router, &qhs_mss_cfg, &qhs_mx_rdpm, &qhs_pcie0_cfg, &qhs_pcie1_cfg, &qhs_pimem_cfg, &qhs_pka_wrapper_cfg, &qhs_pmu_wrapper_cfg, &qhs_qdss_cfg, &qhs_qup0, &qhs_qup1, &qhs_qup2, &qhs_security, &qhs_spss_cfg, &qhs_tcsr, &qhs_tlmm, &qhs_ufs_card_cfg, &qhs_ufs_mem_cfg, &qhs_usb3_0, &qhs_usb3_1, &qhs_venus_cfg, &qhs_vsense_ctrl_cfg, &qns_a1_noc_cfg, &qns_a2_noc_cfg, &qns_ddrss_cfg, &qns_mnoc_cfg, &qns_snoc_cfg, &srvc_cnoc);
DEFINE_QBCM(bcm_cn2, "CN2", false, &qhs_lpass_cfg, &qhs_pdm, &qhs_qspi, &qhs_sdc2, &qhs_sdc4);
DEFINE_QBCM(bcm_co0, "CO0", false, &qns_nsp_gemnoc);
DEFINE_QBCM(bcm_co3, "CO3", false, &qxm_nsp);
DEFINE_QBCM(bcm_mc0, "MC0", true, &ebi);
DEFINE_QBCM(bcm_mm0, "MM0", true, &qns_mem_noc_hf);
DEFINE_QBCM(bcm_mm1, "MM1", false, &qnm_camnoc_hf, &qxm_mdp0, &qxm_mdp1);
DEFINE_QBCM(bcm_mm4, "MM4", false, &qns_mem_noc_sf);
DEFINE_QBCM(bcm_mm5, "MM5", false, &qnm_camnoc_icp, &qnm_camnoc_sf, &qnm_video0, &qnm_video1, &qnm_video_cvp, &qxm_rot);
DEFINE_QBCM(bcm_sh0, "SH0", true, &qns_llcc);
DEFINE_QBCM(bcm_sh2, "SH2", false, &alm_gpu_tcu, &alm_sys_tcu);
DEFINE_QBCM(bcm_sh3, "SH3", false, &qnm_cmpnoc);
DEFINE_QBCM(bcm_sh4, "SH4", false, &chm_apps);
DEFINE_QBCM(bcm_sn0, "SN0", true, &qns_gemnoc_sf);
DEFINE_QBCM(bcm_sn2, "SN2", false, &qns_gemnoc_gc);
DEFINE_QBCM(bcm_sn3, "SN3", false, &qxs_pimem);
DEFINE_QBCM(bcm_sn4, "SN4", false, &xs_qdss_stm);
DEFINE_QBCM(bcm_sn5, "SN5", false, &xm_pcie3_0);
DEFINE_QBCM(bcm_sn6, "SN6", false, &xm_pcie3_1);
DEFINE_QBCM(bcm_sn7, "SN7", false, &qnm_aggre1_noc);
DEFINE_QBCM(bcm_sn8, "SN8", false, &qnm_aggre2_noc);
DEFINE_QBCM(bcm_sn14, "SN14", false, &qns_pcie_mem_noc);
DEFINE_QBCM(bcm_acv_disp, "ACV", false, &ebi_disp);
DEFINE_QBCM(bcm_mc0_disp, "MC0", false, &ebi_disp);
DEFINE_QBCM(bcm_mm0_disp, "MM0", false, &qns_mem_noc_hf_disp);
DEFINE_QBCM(bcm_mm1_disp, "MM1", false, &qxm_mdp0_disp, &qxm_mdp1_disp);
DEFINE_QBCM(bcm_mm4_disp, "MM4", false, &qns_mem_noc_sf_disp);
DEFINE_QBCM(bcm_mm5_disp, "MM5", false, &qxm_rot_disp);
DEFINE_QBCM(bcm_sh0_disp, "SH0", false, &qns_llcc_disp);

static struct qcom_icc_bcm *aggre1_noc_bcms[] = {
};

static struct qcom_icc_node *aggre1_noc_nodes[] = {
	[MASTER_QSPI_0] = &qhm_qspi,
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_A1NOC_CFG] = &qnm_a1noc_cfg,
	[MASTER_SDCC_4] = &xm_sdc4,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_USB3_0] = &xm_usb3_0,
	[MASTER_USB3_1] = &xm_usb3_1,
	[SLAVE_A1NOC_SNOC] = &qns_a1noc_snoc,
	[SLAVE_SERVICE_A1NOC] = &srvc_aggre1_noc,
};

static struct qcom_icc_desc sm8350_aggre1_noc = {
	.nodes = aggre1_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre1_noc_nodes),
	.bcms = aggre1_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_noc_bcms),
};

static struct qcom_icc_bcm *aggre2_noc_bcms[] = {
	&bcm_ce0,
	&bcm_sn5,
	&bcm_sn6,
	&bcm_sn14,
};

static struct qcom_icc_node *aggre2_noc_nodes[] = {
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_QUP_2] = &qhm_qup2,
	[MASTER_A2NOC_CFG] = &qnm_a2noc_cfg,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_PCIE_0] = &xm_pcie3_0,
	[MASTER_PCIE_1] = &xm_pcie3_1,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_UFS_CARD] = &xm_ufs_card,
	[SLAVE_A2NOC_SNOC] = &qns_a2noc_snoc,
	[SLAVE_ANOC_PCIE_GEM_NOC] = &qns_pcie_mem_noc,
	[SLAVE_SERVICE_A2NOC] = &srvc_aggre2_noc,
};

static struct qcom_icc_desc sm8350_aggre2_noc = {
	.nodes = aggre2_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre2_noc_nodes),
	.bcms = aggre2_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_noc_bcms),
};

static struct qcom_icc_bcm *config_noc_bcms[] = {
	&bcm_cn0,
	&bcm_cn1,
	&bcm_cn2,
	&bcm_sn3,
	&bcm_sn4,
};

static struct qcom_icc_node *config_noc_nodes[] = {
	[MASTER_GEM_NOC_CNOC] = &qnm_gemnoc_cnoc,
	[MASTER_GEM_NOC_PCIE_SNOC] = &qnm_gemnoc_pcie,
	[MASTER_QDSS_DAP] = &xm_qdss_dap,
	[SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_NORTH] = &qhs_ahb2phy1,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_APPSS] = &qhs_apss,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CDSP_CFG] = &qhs_compute_cfg,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MMCX_CFG] = &qhs_cpr_mmcx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_CX_RDPM] = &qhs_cx_rdpm,
	[SLAVE_DCC_CFG] = &qhs_dcc_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_GFX3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_HWKM] = &qhs_hwkm,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[SLAVE_LPASS] = &qhs_lpass_cfg,
	[SLAVE_CNOC_MSS] = &qhs_mss_cfg,
	[SLAVE_MX_RDPM] = &qhs_mx_rdpm,
	[SLAVE_PCIE_0_CFG] = &qhs_pcie0_cfg,
	[SLAVE_PCIE_1_CFG] = &qhs_pcie1_cfg,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PKA_WRAPPER_CFG] = &qhs_pka_wrapper_cfg,
	[SLAVE_PMU_WRAPPER_CFG] = &qhs_pmu_wrapper_cfg,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QSPI_0] = &qhs_qspi,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_QUP_2] = &qhs_qup2,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SDCC_4] = &qhs_sdc4,
	[SLAVE_SECURITY] = &qhs_security,
	[SLAVE_SPSS_CFG] = &qhs_spss_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_UFS_CARD_CFG] = &qhs_ufs_card_cfg,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB3_0] = &qhs_usb3_0,
	[SLAVE_USB3_1] = &qhs_usb3_1,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_A1NOC_CFG] = &qns_a1_noc_cfg,
	[SLAVE_A2NOC_CFG] = &qns_a2_noc_cfg,
	[SLAVE_DDRSS_CFG] = &qns_ddrss_cfg,
	[SLAVE_CNOC_MNOC_CFG] = &qns_mnoc_cfg,
	[SLAVE_SNOC_CFG] = &qns_snoc_cfg,
	[SLAVE_BOOT_IMEM] = &qxs_boot_imem,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_CNOC] = &srvc_cnoc,
	[SLAVE_PCIE_0] = &xs_pcie_0,
	[SLAVE_PCIE_1] = &xs_pcie_1,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static struct qcom_icc_desc sm8350_config_noc = {
	.nodes = config_noc_nodes,
	.num_nodes = ARRAY_SIZE(config_noc_nodes),
	.bcms = config_noc_bcms,
	.num_bcms = ARRAY_SIZE(config_noc_bcms),
};

static struct qcom_icc_bcm *dc_noc_bcms[] = {
};

static struct qcom_icc_node *dc_noc_nodes[] = {
	[MASTER_CNOC_DC_NOC] = &qnm_cnoc_dc_noc,
	[SLAVE_LLCC_CFG] = &qhs_llcc,
	[SLAVE_GEM_NOC_CFG] = &qns_gemnoc,
};

static struct qcom_icc_desc sm8350_dc_noc = {
	.nodes = dc_noc_nodes,
	.num_nodes = ARRAY_SIZE(dc_noc_nodes),
	.bcms = dc_noc_bcms,
	.num_bcms = ARRAY_SIZE(dc_noc_bcms),
};

static struct qcom_icc_bcm *gem_noc_bcms[] = {
	&bcm_sh0,
	&bcm_sh2,
	&bcm_sh3,
	&bcm_sh4,
	&bcm_sh0_disp,
};

static struct qcom_icc_node *gem_noc_nodes[] = {
	[MASTER_GPU_TCU] = &alm_gpu_tcu,
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_COMPUTE_NOC] = &qnm_cmpnoc,
	[MASTER_GEM_NOC_CFG] = &qnm_gemnoc_cfg,
	[MASTER_GFX3D] = &qnm_gpu,
	[MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[MASTER_MNOC_SF_MEM_NOC] = &qnm_mnoc_sf,
	[MASTER_ANOC_PCIE_GEM_NOC] = &qnm_pcie,
	[MASTER_SNOC_GC_MEM_NOC] = &qnm_snoc_gc,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[SLAVE_MSS_PROC_MS_MPU_CFG] = &qhs_mdsp_ms_mpu_cfg,
	[SLAVE_MCDMA_MS_MPU_CFG] = &qhs_modem_ms_mpu_cfg,
	[SLAVE_GEM_NOC_CNOC] = &qns_gem_noc_cnoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_MEM_NOC_PCIE_SNOC] = &qns_pcie,
	[SLAVE_SERVICE_GEM_NOC_1] = &srvc_even_gemnoc,
	[SLAVE_SERVICE_GEM_NOC_2] = &srvc_odd_gemnoc,
	[SLAVE_SERVICE_GEM_NOC] = &srvc_sys_gemnoc,
	[MASTER_MNOC_HF_MEM_NOC_DISP] = &qnm_mnoc_hf_disp,
	[MASTER_MNOC_SF_MEM_NOC_DISP] = &qnm_mnoc_sf_disp,
	[SLAVE_LLCC_DISP] = &qns_llcc_disp,
};

static struct qcom_icc_desc sm8350_gem_noc = {
	.nodes = gem_noc_nodes,
	.num_nodes = ARRAY_SIZE(gem_noc_nodes),
	.bcms = gem_noc_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_bcms),
};

static struct qcom_icc_bcm *lpass_ag_noc_bcms[] = {
};

static struct qcom_icc_node *lpass_ag_noc_nodes[] = {
	[MASTER_CNOC_LPASS_AG_NOC] = &qhm_config_noc,
	[SLAVE_LPASS_CORE_CFG] = &qhs_lpass_core,
	[SLAVE_LPASS_LPI_CFG] = &qhs_lpass_lpi,
	[SLAVE_LPASS_MPU_CFG] = &qhs_lpass_mpu,
	[SLAVE_LPASS_TOP_CFG] = &qhs_lpass_top,
	[SLAVE_SERVICES_LPASS_AML_NOC] = &srvc_niu_aml_noc,
	[SLAVE_SERVICE_LPASS_AG_NOC] = &srvc_niu_lpass_agnoc,
};

static struct qcom_icc_desc sm8350_lpass_ag_noc = {
	.nodes = lpass_ag_noc_nodes,
	.num_nodes = ARRAY_SIZE(lpass_ag_noc_nodes),
	.bcms = lpass_ag_noc_bcms,
	.num_bcms = ARRAY_SIZE(lpass_ag_noc_bcms),
};

static struct qcom_icc_bcm *mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
	&bcm_acv_disp,
	&bcm_mc0_disp,
};

static struct qcom_icc_node *mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
	[MASTER_LLCC_DISP] = &llcc_mc_disp,
	[SLAVE_EBI1_DISP] = &ebi_disp,
};

static struct qcom_icc_desc sm8350_mc_virt = {
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm *mmss_noc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
	&bcm_mm4,
	&bcm_mm5,
	&bcm_mm0_disp,
	&bcm_mm1_disp,
	&bcm_mm4_disp,
	&bcm_mm5_disp,
};

static struct qcom_icc_node *mmss_noc_nodes[] = {
	[MASTER_CAMNOC_HF] = &qnm_camnoc_hf,
	[MASTER_CAMNOC_ICP] = &qnm_camnoc_icp,
	[MASTER_CAMNOC_SF] = &qnm_camnoc_sf,
	[MASTER_CNOC_MNOC_CFG] = &qnm_mnoc_cfg,
	[MASTER_VIDEO_P0] = &qnm_video0,
	[MASTER_VIDEO_P1] = &qnm_video1,
	[MASTER_VIDEO_PROC] = &qnm_video_cvp,
	[MASTER_MDP0] = &qxm_mdp0,
	[MASTER_MDP1] = &qxm_mdp1,
	[MASTER_ROTATOR] = &qxm_rot,
	[SLAVE_MNOC_HF_MEM_NOC] = &qns_mem_noc_hf,
	[SLAVE_MNOC_SF_MEM_NOC] = &qns_mem_noc_sf,
	[SLAVE_SERVICE_MNOC] = &srvc_mnoc,
	[MASTER_MDP0_DISP] = &qxm_mdp0_disp,
	[MASTER_MDP1_DISP] = &qxm_mdp1_disp,
	[MASTER_ROTATOR_DISP] = &qxm_rot_disp,
	[SLAVE_MNOC_HF_MEM_NOC_DISP] = &qns_mem_noc_hf_disp,
	[SLAVE_MNOC_SF_MEM_NOC_DISP] = &qns_mem_noc_sf_disp,
};

static struct qcom_icc_desc sm8350_mmss_noc = {
	.nodes = mmss_noc_nodes,
	.num_nodes = ARRAY_SIZE(mmss_noc_nodes),
	.bcms = mmss_noc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_bcms),
};

static struct qcom_icc_bcm *nsp_noc_bcms[] = {
	&bcm_co0,
	&bcm_co3,
};

static struct qcom_icc_node *nsp_noc_nodes[] = {
	[MASTER_CDSP_NOC_CFG] = &qhm_nsp_noc_config,
	[MASTER_CDSP_PROC] = &qxm_nsp,
	[SLAVE_CDSP_MEM_NOC] = &qns_nsp_gemnoc,
	[SLAVE_SERVICE_NSP_NOC] = &service_nsp_noc,
};

static struct qcom_icc_desc sm8350_compute_noc = {
	.nodes = nsp_noc_nodes,
	.num_nodes = ARRAY_SIZE(nsp_noc_nodes),
	.bcms = nsp_noc_bcms,
	.num_bcms = ARRAY_SIZE(nsp_noc_bcms),
};

static struct qcom_icc_bcm *system_noc_bcms[] = {
	&bcm_sn0,
	&bcm_sn2,
	&bcm_sn7,
	&bcm_sn8,
};

static struct qcom_icc_node *system_noc_nodes[] = {
	[MASTER_A1NOC_SNOC] = &qnm_aggre1_noc,
	[MASTER_A2NOC_SNOC] = &qnm_aggre2_noc,
	[MASTER_SNOC_CFG] = &qnm_snoc_cfg,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_SNOC_GEM_NOC_GC] = &qns_gemnoc_gc,
	[SLAVE_SNOC_GEM_NOC_SF] = &qns_gemnoc_sf,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
};

static struct qcom_icc_desc sm8350_system_noc = {
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sm8350-aggre1-noc", .data = &sm8350_aggre1_noc},
	{ .compatible = "qcom,sm8350-aggre2-noc", .data = &sm8350_aggre2_noc},
	{ .compatible = "qcom,sm8350-config-noc", .data = &sm8350_config_noc},
	{ .compatible = "qcom,sm8350-dc-noc", .data = &sm8350_dc_noc},
	{ .compatible = "qcom,sm8350-gem-noc", .data = &sm8350_gem_noc},
	{ .compatible = "qcom,sm8350-lpass-ag-noc", .data = &sm8350_lpass_ag_noc},
	{ .compatible = "qcom,sm8350-mc-virt", .data = &sm8350_mc_virt},
	{ .compatible = "qcom,sm8350-mmss-noc", .data = &sm8350_mmss_noc},
	{ .compatible = "qcom,sm8350-compute-noc", .data = &sm8350_compute_noc},
	{ .compatible = "qcom,sm8350-system-noc", .data = &sm8350_system_noc},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-sm8350",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(qnoc_driver);

MODULE_DESCRIPTION("SM8350 NoC driver");
MODULE_LICENSE("GPL v2");
