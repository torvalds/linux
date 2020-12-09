// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <dt-bindings/interconnect/qcom,sm8250.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"
#include "sm8250.h"

DEFINE_QNODE(qhm_a1noc_cfg, SM8250_MASTER_A1NOC_CFG, 1, 4, SM8250_SLAVE_SERVICE_A1NOC);
DEFINE_QNODE(qhm_qspi, SM8250_MASTER_QSPI_0, 1, 4, SM8250_A1NOC_SNOC_SLV);
DEFINE_QNODE(qhm_qup1, SM8250_MASTER_QUP_1, 1, 4, SM8250_A1NOC_SNOC_SLV);
DEFINE_QNODE(qhm_qup2, SM8250_MASTER_QUP_2, 1, 4, SM8250_A1NOC_SNOC_SLV);
DEFINE_QNODE(qhm_tsif, SM8250_MASTER_TSIF, 1, 4, SM8250_A1NOC_SNOC_SLV);
DEFINE_QNODE(xm_pcie3_modem, SM8250_MASTER_PCIE_2, 1, 8, SM8250_SLAVE_ANOC_PCIE_GEM_NOC_1);
DEFINE_QNODE(xm_sdc4, SM8250_MASTER_SDCC_4, 1, 8, SM8250_A1NOC_SNOC_SLV);
DEFINE_QNODE(xm_ufs_mem, SM8250_MASTER_UFS_MEM, 1, 8, SM8250_A1NOC_SNOC_SLV);
DEFINE_QNODE(xm_usb3_0, SM8250_MASTER_USB3, 1, 8, SM8250_A1NOC_SNOC_SLV);
DEFINE_QNODE(xm_usb3_1, SM8250_MASTER_USB3_1, 1, 8, SM8250_A1NOC_SNOC_SLV);
DEFINE_QNODE(qhm_a2noc_cfg, SM8250_MASTER_A2NOC_CFG, 1, 4, SM8250_SLAVE_SERVICE_A2NOC);
DEFINE_QNODE(qhm_qdss_bam, SM8250_MASTER_QDSS_BAM, 1, 4, SM8250_A2NOC_SNOC_SLV);
DEFINE_QNODE(qhm_qup0, SM8250_MASTER_QUP_0, 1, 4, SM8250_A2NOC_SNOC_SLV);
DEFINE_QNODE(qnm_cnoc, SM8250_MASTER_CNOC_A2NOC, 1, 8, SM8250_A2NOC_SNOC_SLV);
DEFINE_QNODE(qxm_crypto, SM8250_MASTER_CRYPTO_CORE_0, 1, 8, SM8250_A2NOC_SNOC_SLV);
DEFINE_QNODE(qxm_ipa, SM8250_MASTER_IPA, 1, 8, SM8250_A2NOC_SNOC_SLV);
DEFINE_QNODE(xm_pcie3_0, SM8250_MASTER_PCIE, 1, 8, SM8250_SLAVE_ANOC_PCIE_GEM_NOC);
DEFINE_QNODE(xm_pcie3_1, SM8250_MASTER_PCIE_1, 1, 8, SM8250_SLAVE_ANOC_PCIE_GEM_NOC);
DEFINE_QNODE(xm_qdss_etr, SM8250_MASTER_QDSS_ETR, 1, 8, SM8250_A2NOC_SNOC_SLV);
DEFINE_QNODE(xm_sdc2, SM8250_MASTER_SDCC_2, 1, 8, SM8250_A2NOC_SNOC_SLV);
DEFINE_QNODE(xm_ufs_card, SM8250_MASTER_UFS_CARD, 1, 8, SM8250_A2NOC_SNOC_SLV);
DEFINE_QNODE(qnm_npu, SM8250_MASTER_NPU, 2, 32, SM8250_SLAVE_CDSP_MEM_NOC);
DEFINE_QNODE(qnm_snoc, SM8250_SNOC_CNOC_MAS, 1, 8, SM8250_SLAVE_CDSP_CFG, SM8250_SLAVE_CAMERA_CFG, SM8250_SLAVE_TLMM_SOUTH, SM8250_SLAVE_TLMM_NORTH, SM8250_SLAVE_SDCC_4, SM8250_SLAVE_TLMM_WEST, SM8250_SLAVE_SDCC_2, SM8250_SLAVE_CNOC_MNOC_CFG, SM8250_SLAVE_UFS_MEM_CFG, SM8250_SLAVE_SNOC_CFG, SM8250_SLAVE_PDM, SM8250_SLAVE_CX_RDPM, SM8250_SLAVE_PCIE_1_CFG, SM8250_SLAVE_A2NOC_CFG, SM8250_SLAVE_QDSS_CFG, SM8250_SLAVE_DISPLAY_CFG, SM8250_SLAVE_PCIE_2_CFG, SM8250_SLAVE_TCSR, SM8250_SLAVE_DCC_CFG, SM8250_SLAVE_CNOC_DDRSS, SM8250_SLAVE_IPC_ROUTER_CFG, SM8250_SLAVE_PCIE_0_CFG, SM8250_SLAVE_RBCPR_MMCX_CFG, SM8250_SLAVE_NPU_CFG, SM8250_SLAVE_AHB2PHY_SOUTH, SM8250_SLAVE_AHB2PHY_NORTH, SM8250_SLAVE_GRAPHICS_3D_CFG, SM8250_SLAVE_VENUS_CFG, SM8250_SLAVE_TSIF, SM8250_SLAVE_IPA_CFG, SM8250_SLAVE_IMEM_CFG, SM8250_SLAVE_USB3, SM8250_SLAVE_SERVICE_CNOC, SM8250_SLAVE_UFS_CARD_CFG, SM8250_SLAVE_USB3_1, SM8250_SLAVE_LPASS, SM8250_SLAVE_RBCPR_CX_CFG, SM8250_SLAVE_A1NOC_CFG, SM8250_SLAVE_AOSS, SM8250_SLAVE_PRNG, SM8250_SLAVE_VSENSE_CTRL_CFG, SM8250_SLAVE_QSPI_0, SM8250_SLAVE_CRYPTO_0_CFG, SM8250_SLAVE_PIMEM_CFG, SM8250_SLAVE_RBCPR_MX_CFG, SM8250_SLAVE_QUP_0, SM8250_SLAVE_QUP_1, SM8250_SLAVE_QUP_2, SM8250_SLAVE_CLK_CTL);
DEFINE_QNODE(xm_qdss_dap, SM8250_MASTER_QDSS_DAP, 1, 8, SM8250_SLAVE_CDSP_CFG, SM8250_SLAVE_CAMERA_CFG, SM8250_SLAVE_TLMM_SOUTH, SM8250_SLAVE_TLMM_NORTH, SM8250_SLAVE_SDCC_4, SM8250_SLAVE_TLMM_WEST, SM8250_SLAVE_SDCC_2, SM8250_SLAVE_CNOC_MNOC_CFG, SM8250_SLAVE_UFS_MEM_CFG, SM8250_SLAVE_SNOC_CFG, SM8250_SLAVE_PDM, SM8250_SLAVE_CX_RDPM, SM8250_SLAVE_PCIE_1_CFG, SM8250_SLAVE_A2NOC_CFG, SM8250_SLAVE_QDSS_CFG, SM8250_SLAVE_DISPLAY_CFG, SM8250_SLAVE_PCIE_2_CFG, SM8250_SLAVE_TCSR, SM8250_SLAVE_DCC_CFG, SM8250_SLAVE_CNOC_DDRSS, SM8250_SLAVE_IPC_ROUTER_CFG, SM8250_SLAVE_CNOC_A2NOC, SM8250_SLAVE_PCIE_0_CFG, SM8250_SLAVE_RBCPR_MMCX_CFG, SM8250_SLAVE_NPU_CFG, SM8250_SLAVE_AHB2PHY_SOUTH, SM8250_SLAVE_AHB2PHY_NORTH, SM8250_SLAVE_GRAPHICS_3D_CFG, SM8250_SLAVE_VENUS_CFG, SM8250_SLAVE_TSIF, SM8250_SLAVE_IPA_CFG, SM8250_SLAVE_IMEM_CFG, SM8250_SLAVE_USB3, SM8250_SLAVE_SERVICE_CNOC, SM8250_SLAVE_UFS_CARD_CFG, SM8250_SLAVE_USB3_1, SM8250_SLAVE_LPASS, SM8250_SLAVE_RBCPR_CX_CFG, SM8250_SLAVE_A1NOC_CFG, SM8250_SLAVE_AOSS, SM8250_SLAVE_PRNG, SM8250_SLAVE_VSENSE_CTRL_CFG, SM8250_SLAVE_QSPI_0, SM8250_SLAVE_CRYPTO_0_CFG, SM8250_SLAVE_PIMEM_CFG, SM8250_SLAVE_RBCPR_MX_CFG, SM8250_SLAVE_QUP_0, SM8250_SLAVE_QUP_1, SM8250_SLAVE_QUP_2, SM8250_SLAVE_CLK_CTL);
DEFINE_QNODE(qhm_cnoc_dc_noc, SM8250_MASTER_CNOC_DC_NOC, 1, 4, SM8250_SLAVE_GEM_NOC_CFG, SM8250_SLAVE_LLCC_CFG);
DEFINE_QNODE(alm_gpu_tcu, SM8250_MASTER_GPU_TCU, 1, 8, SM8250_SLAVE_LLCC, SM8250_SLAVE_GEM_NOC_SNOC);
DEFINE_QNODE(alm_sys_tcu, SM8250_MASTER_SYS_TCU, 1, 8, SM8250_SLAVE_LLCC, SM8250_SLAVE_GEM_NOC_SNOC);
DEFINE_QNODE(chm_apps, SM8250_MASTER_AMPSS_M0, 2, 32, SM8250_SLAVE_LLCC, SM8250_SLAVE_GEM_NOC_SNOC, SM8250_SLAVE_MEM_NOC_PCIE_SNOC);
DEFINE_QNODE(qhm_gemnoc_cfg, SM8250_MASTER_GEM_NOC_CFG, 1, 4, SM8250_SLAVE_SERVICE_GEM_NOC_2, SM8250_SLAVE_SERVICE_GEM_NOC_1, SM8250_SLAVE_SERVICE_GEM_NOC);
DEFINE_QNODE(qnm_cmpnoc, SM8250_MASTER_COMPUTE_NOC, 2, 32, SM8250_SLAVE_LLCC, SM8250_SLAVE_GEM_NOC_SNOC);
DEFINE_QNODE(qnm_gpu, SM8250_MASTER_GRAPHICS_3D, 2, 32, SM8250_SLAVE_LLCC, SM8250_SLAVE_GEM_NOC_SNOC);
DEFINE_QNODE(qnm_mnoc_hf, SM8250_MASTER_MNOC_HF_MEM_NOC, 2, 32, SM8250_SLAVE_LLCC);
DEFINE_QNODE(qnm_mnoc_sf, SM8250_MASTER_MNOC_SF_MEM_NOC, 2, 32, SM8250_SLAVE_LLCC, SM8250_SLAVE_GEM_NOC_SNOC);
DEFINE_QNODE(qnm_pcie, SM8250_MASTER_ANOC_PCIE_GEM_NOC, 1, 16, SM8250_SLAVE_LLCC, SM8250_SLAVE_GEM_NOC_SNOC);
DEFINE_QNODE(qnm_snoc_gc, SM8250_MASTER_SNOC_GC_MEM_NOC, 1, 8, SM8250_SLAVE_LLCC);
DEFINE_QNODE(qnm_snoc_sf, SM8250_MASTER_SNOC_SF_MEM_NOC, 1, 16, SM8250_SLAVE_LLCC, SM8250_SLAVE_GEM_NOC_SNOC, SM8250_SLAVE_MEM_NOC_PCIE_SNOC);
DEFINE_QNODE(ipa_core_master, SM8250_MASTER_IPA_CORE, 1, 8, SM8250_SLAVE_IPA_CORE);
DEFINE_QNODE(llcc_mc, SM8250_MASTER_LLCC, 4, 4, SM8250_SLAVE_EBI_CH0);
DEFINE_QNODE(qhm_mnoc_cfg, SM8250_MASTER_CNOC_MNOC_CFG, 1, 4, SM8250_SLAVE_SERVICE_MNOC);
DEFINE_QNODE(qnm_camnoc_hf, SM8250_MASTER_CAMNOC_HF, 2, 32, SM8250_SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(qnm_camnoc_icp, SM8250_MASTER_CAMNOC_ICP, 1, 8, SM8250_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qnm_camnoc_sf, SM8250_MASTER_CAMNOC_SF, 2, 32, SM8250_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qnm_video0, SM8250_MASTER_VIDEO_P0, 1, 32, SM8250_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qnm_video1, SM8250_MASTER_VIDEO_P1, 1, 32, SM8250_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qnm_video_cvp, SM8250_MASTER_VIDEO_PROC, 1, 32, SM8250_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qxm_mdp0, SM8250_MASTER_MDP_PORT0, 1, 32, SM8250_SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(qxm_mdp1, SM8250_MASTER_MDP_PORT1, 1, 32, SM8250_SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(qxm_rot, SM8250_MASTER_ROTATOR, 1, 32, SM8250_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(amm_npu_sys, SM8250_MASTER_NPU_SYS, 4, 32, SM8250_SLAVE_NPU_COMPUTE_NOC);
DEFINE_QNODE(amm_npu_sys_cdp_w, SM8250_MASTER_NPU_CDP, 2, 16, SM8250_SLAVE_NPU_COMPUTE_NOC);
DEFINE_QNODE(qhm_cfg, SM8250_MASTER_NPU_NOC_CFG, 1, 4, SM8250_SLAVE_SERVICE_NPU_NOC, SM8250_SLAVE_ISENSE_CFG, SM8250_SLAVE_NPU_LLM_CFG, SM8250_SLAVE_NPU_INT_DMA_BWMON_CFG, SM8250_SLAVE_NPU_CP, SM8250_SLAVE_NPU_TCM, SM8250_SLAVE_NPU_CAL_DP0, SM8250_SLAVE_NPU_CAL_DP1, SM8250_SLAVE_NPU_DPM);
DEFINE_QNODE(qhm_snoc_cfg, SM8250_MASTER_SNOC_CFG, 1, 4, SM8250_SLAVE_SERVICE_SNOC);
DEFINE_QNODE(qnm_aggre1_noc, SM8250_A1NOC_SNOC_MAS, 1, 16, SM8250_SLAVE_SNOC_GEM_NOC_SF);
DEFINE_QNODE(qnm_aggre2_noc, SM8250_A2NOC_SNOC_MAS, 1, 16, SM8250_SLAVE_SNOC_GEM_NOC_SF);
DEFINE_QNODE(qnm_gemnoc, SM8250_MASTER_GEM_NOC_SNOC, 1, 16, SM8250_SLAVE_PIMEM, SM8250_SLAVE_OCIMEM, SM8250_SLAVE_APPSS, SM8250_SNOC_CNOC_SLV, SM8250_SLAVE_TCU, SM8250_SLAVE_QDSS_STM);
DEFINE_QNODE(qnm_gemnoc_pcie, SM8250_MASTER_GEM_NOC_PCIE_SNOC, 1, 8, SM8250_SLAVE_PCIE_2, SM8250_SLAVE_PCIE_0, SM8250_SLAVE_PCIE_1);
DEFINE_QNODE(qxm_pimem, SM8250_MASTER_PIMEM, 1, 8, SM8250_SLAVE_SNOC_GEM_NOC_GC);
DEFINE_QNODE(xm_gic, SM8250_MASTER_GIC, 1, 8, SM8250_SLAVE_SNOC_GEM_NOC_GC);
DEFINE_QNODE(qns_a1noc_snoc, SM8250_A1NOC_SNOC_SLV, 1, 16, SM8250_A1NOC_SNOC_MAS);
DEFINE_QNODE(qns_pcie_modem_mem_noc, SM8250_SLAVE_ANOC_PCIE_GEM_NOC_1, 1, 16, SM8250_MASTER_ANOC_PCIE_GEM_NOC);
DEFINE_QNODE(srvc_aggre1_noc, SM8250_SLAVE_SERVICE_A1NOC, 1, 4);
DEFINE_QNODE(qns_a2noc_snoc, SM8250_A2NOC_SNOC_SLV, 1, 16, SM8250_A2NOC_SNOC_MAS);
DEFINE_QNODE(qns_pcie_mem_noc, SM8250_SLAVE_ANOC_PCIE_GEM_NOC, 1, 16, SM8250_MASTER_ANOC_PCIE_GEM_NOC);
DEFINE_QNODE(srvc_aggre2_noc, SM8250_SLAVE_SERVICE_A2NOC, 1, 4);
DEFINE_QNODE(qns_cdsp_mem_noc, SM8250_SLAVE_CDSP_MEM_NOC, 2, 32, SM8250_MASTER_COMPUTE_NOC);
DEFINE_QNODE(qhs_a1_noc_cfg, SM8250_SLAVE_A1NOC_CFG, 1, 4, SM8250_MASTER_A1NOC_CFG);
DEFINE_QNODE(qhs_a2_noc_cfg, SM8250_SLAVE_A2NOC_CFG, 1, 4, SM8250_MASTER_A2NOC_CFG);
DEFINE_QNODE(qhs_ahb2phy0, SM8250_SLAVE_AHB2PHY_SOUTH, 1, 4);
DEFINE_QNODE(qhs_ahb2phy1, SM8250_SLAVE_AHB2PHY_NORTH, 1, 4);
DEFINE_QNODE(qhs_aoss, SM8250_SLAVE_AOSS, 1, 4);
DEFINE_QNODE(qhs_camera_cfg, SM8250_SLAVE_CAMERA_CFG, 1, 4);
DEFINE_QNODE(qhs_clk_ctl, SM8250_SLAVE_CLK_CTL, 1, 4);
DEFINE_QNODE(qhs_compute_dsp, SM8250_SLAVE_CDSP_CFG, 1, 4);
DEFINE_QNODE(qhs_cpr_cx, SM8250_SLAVE_RBCPR_CX_CFG, 1, 4);
DEFINE_QNODE(qhs_cpr_mmcx, SM8250_SLAVE_RBCPR_MMCX_CFG, 1, 4);
DEFINE_QNODE(qhs_cpr_mx, SM8250_SLAVE_RBCPR_MX_CFG, 1, 4);
DEFINE_QNODE(qhs_crypto0_cfg, SM8250_SLAVE_CRYPTO_0_CFG, 1, 4);
DEFINE_QNODE(qhs_cx_rdpm, SM8250_SLAVE_CX_RDPM, 1, 4);
DEFINE_QNODE(qhs_dcc_cfg, SM8250_SLAVE_DCC_CFG, 1, 4);
DEFINE_QNODE(qhs_ddrss_cfg, SM8250_SLAVE_CNOC_DDRSS, 1, 4, SM8250_MASTER_CNOC_DC_NOC);
DEFINE_QNODE(qhs_display_cfg, SM8250_SLAVE_DISPLAY_CFG, 1, 4);
DEFINE_QNODE(qhs_gpuss_cfg, SM8250_SLAVE_GRAPHICS_3D_CFG, 1, 8);
DEFINE_QNODE(qhs_imem_cfg, SM8250_SLAVE_IMEM_CFG, 1, 4);
DEFINE_QNODE(qhs_ipa, SM8250_SLAVE_IPA_CFG, 1, 4);
DEFINE_QNODE(qhs_ipc_router, SM8250_SLAVE_IPC_ROUTER_CFG, 1, 4);
DEFINE_QNODE(qhs_lpass_cfg, SM8250_SLAVE_LPASS, 1, 4);
DEFINE_QNODE(qhs_mnoc_cfg, SM8250_SLAVE_CNOC_MNOC_CFG, 1, 4, SM8250_MASTER_CNOC_MNOC_CFG);
DEFINE_QNODE(qhs_npu_cfg, SM8250_SLAVE_NPU_CFG, 1, 4, SM8250_MASTER_NPU_NOC_CFG);
DEFINE_QNODE(qhs_pcie0_cfg, SM8250_SLAVE_PCIE_0_CFG, 1, 4);
DEFINE_QNODE(qhs_pcie1_cfg, SM8250_SLAVE_PCIE_1_CFG, 1, 4);
DEFINE_QNODE(qhs_pcie_modem_cfg, SM8250_SLAVE_PCIE_2_CFG, 1, 4);
DEFINE_QNODE(qhs_pdm, SM8250_SLAVE_PDM, 1, 4);
DEFINE_QNODE(qhs_pimem_cfg, SM8250_SLAVE_PIMEM_CFG, 1, 4);
DEFINE_QNODE(qhs_prng, SM8250_SLAVE_PRNG, 1, 4);
DEFINE_QNODE(qhs_qdss_cfg, SM8250_SLAVE_QDSS_CFG, 1, 4);
DEFINE_QNODE(qhs_qspi, SM8250_SLAVE_QSPI_0, 1, 4);
DEFINE_QNODE(qhs_qup0, SM8250_SLAVE_QUP_0, 1, 4);
DEFINE_QNODE(qhs_qup1, SM8250_SLAVE_QUP_1, 1, 4);
DEFINE_QNODE(qhs_qup2, SM8250_SLAVE_QUP_2, 1, 4);
DEFINE_QNODE(qhs_sdc2, SM8250_SLAVE_SDCC_2, 1, 4);
DEFINE_QNODE(qhs_sdc4, SM8250_SLAVE_SDCC_4, 1, 4);
DEFINE_QNODE(qhs_snoc_cfg, SM8250_SLAVE_SNOC_CFG, 1, 4, SM8250_MASTER_SNOC_CFG);
DEFINE_QNODE(qhs_tcsr, SM8250_SLAVE_TCSR, 1, 4);
DEFINE_QNODE(qhs_tlmm0, SM8250_SLAVE_TLMM_NORTH, 1, 4);
DEFINE_QNODE(qhs_tlmm1, SM8250_SLAVE_TLMM_SOUTH, 1, 4);
DEFINE_QNODE(qhs_tlmm2, SM8250_SLAVE_TLMM_WEST, 1, 4);
DEFINE_QNODE(qhs_tsif, SM8250_SLAVE_TSIF, 1, 4);
DEFINE_QNODE(qhs_ufs_card_cfg, SM8250_SLAVE_UFS_CARD_CFG, 1, 4);
DEFINE_QNODE(qhs_ufs_mem_cfg, SM8250_SLAVE_UFS_MEM_CFG, 1, 4);
DEFINE_QNODE(qhs_usb3_0, SM8250_SLAVE_USB3, 1, 4);
DEFINE_QNODE(qhs_usb3_1, SM8250_SLAVE_USB3_1, 1, 4);
DEFINE_QNODE(qhs_venus_cfg, SM8250_SLAVE_VENUS_CFG, 1, 4);
DEFINE_QNODE(qhs_vsense_ctrl_cfg, SM8250_SLAVE_VSENSE_CTRL_CFG, 1, 4);
DEFINE_QNODE(qns_cnoc_a2noc, SM8250_SLAVE_CNOC_A2NOC, 1, 8, SM8250_MASTER_CNOC_A2NOC);
DEFINE_QNODE(srvc_cnoc, SM8250_SLAVE_SERVICE_CNOC, 1, 4);
DEFINE_QNODE(qhs_llcc, SM8250_SLAVE_LLCC_CFG, 1, 4);
DEFINE_QNODE(qhs_memnoc, SM8250_SLAVE_GEM_NOC_CFG, 1, 4, SM8250_MASTER_GEM_NOC_CFG);
DEFINE_QNODE(qns_gem_noc_snoc, SM8250_SLAVE_GEM_NOC_SNOC, 1, 16, SM8250_MASTER_GEM_NOC_SNOC);
DEFINE_QNODE(qns_llcc, SM8250_SLAVE_LLCC, 4, 16, SM8250_MASTER_LLCC);
DEFINE_QNODE(qns_sys_pcie, SM8250_SLAVE_MEM_NOC_PCIE_SNOC, 1, 8, SM8250_MASTER_GEM_NOC_PCIE_SNOC);
DEFINE_QNODE(srvc_even_gemnoc, SM8250_SLAVE_SERVICE_GEM_NOC_1, 1, 4);
DEFINE_QNODE(srvc_odd_gemnoc, SM8250_SLAVE_SERVICE_GEM_NOC_2, 1, 4);
DEFINE_QNODE(srvc_sys_gemnoc, SM8250_SLAVE_SERVICE_GEM_NOC, 1, 4);
DEFINE_QNODE(ipa_core_slave, SM8250_SLAVE_IPA_CORE, 1, 8);
DEFINE_QNODE(ebi, SM8250_SLAVE_EBI_CH0, 4, 4);
DEFINE_QNODE(qns_mem_noc_hf, SM8250_SLAVE_MNOC_HF_MEM_NOC, 2, 32, SM8250_MASTER_MNOC_HF_MEM_NOC);
DEFINE_QNODE(qns_mem_noc_sf, SM8250_SLAVE_MNOC_SF_MEM_NOC, 2, 32, SM8250_MASTER_MNOC_SF_MEM_NOC);
DEFINE_QNODE(srvc_mnoc, SM8250_SLAVE_SERVICE_MNOC, 1, 4);
DEFINE_QNODE(qhs_cal_dp0, SM8250_SLAVE_NPU_CAL_DP0, 1, 4);
DEFINE_QNODE(qhs_cal_dp1, SM8250_SLAVE_NPU_CAL_DP1, 1, 4);
DEFINE_QNODE(qhs_cp, SM8250_SLAVE_NPU_CP, 1, 4);
DEFINE_QNODE(qhs_dma_bwmon, SM8250_SLAVE_NPU_INT_DMA_BWMON_CFG, 1, 4);
DEFINE_QNODE(qhs_dpm, SM8250_SLAVE_NPU_DPM, 1, 4);
DEFINE_QNODE(qhs_isense, SM8250_SLAVE_ISENSE_CFG, 1, 4);
DEFINE_QNODE(qhs_llm, SM8250_SLAVE_NPU_LLM_CFG, 1, 4);
DEFINE_QNODE(qhs_tcm, SM8250_SLAVE_NPU_TCM, 1, 4);
DEFINE_QNODE(qns_npu_sys, SM8250_SLAVE_NPU_COMPUTE_NOC, 2, 32);
DEFINE_QNODE(srvc_noc, SM8250_SLAVE_SERVICE_NPU_NOC, 1, 4);
DEFINE_QNODE(qhs_apss, SM8250_SLAVE_APPSS, 1, 8);
DEFINE_QNODE(qns_cnoc, SM8250_SNOC_CNOC_SLV, 1, 8, SM8250_SNOC_CNOC_MAS);
DEFINE_QNODE(qns_gemnoc_gc, SM8250_SLAVE_SNOC_GEM_NOC_GC, 1, 8, SM8250_MASTER_SNOC_GC_MEM_NOC);
DEFINE_QNODE(qns_gemnoc_sf, SM8250_SLAVE_SNOC_GEM_NOC_SF, 1, 16, SM8250_MASTER_SNOC_SF_MEM_NOC);
DEFINE_QNODE(qxs_imem, SM8250_SLAVE_OCIMEM, 1, 8);
DEFINE_QNODE(qxs_pimem, SM8250_SLAVE_PIMEM, 1, 8);
DEFINE_QNODE(srvc_snoc, SM8250_SLAVE_SERVICE_SNOC, 1, 4);
DEFINE_QNODE(xs_pcie_0, SM8250_SLAVE_PCIE_0, 1, 8);
DEFINE_QNODE(xs_pcie_1, SM8250_SLAVE_PCIE_1, 1, 8);
DEFINE_QNODE(xs_pcie_modem, SM8250_SLAVE_PCIE_2, 1, 8);
DEFINE_QNODE(xs_qdss_stm, SM8250_SLAVE_QDSS_STM, 1, 4);
DEFINE_QNODE(xs_sys_tcu_cfg, SM8250_SLAVE_TCU, 1, 8);

DEFINE_QBCM(bcm_acv, "ACV", false, &ebi);
DEFINE_QBCM(bcm_mc0, "MC0", true, &ebi);
DEFINE_QBCM(bcm_sh0, "SH0", true, &qns_llcc);
DEFINE_QBCM(bcm_mm0, "MM0", true, &qns_mem_noc_hf);
DEFINE_QBCM(bcm_ce0, "CE0", false, &qxm_crypto);
DEFINE_QBCM(bcm_ip0, "IP0", false, &ipa_core_slave);
DEFINE_QBCM(bcm_mm1, "MM1", false, &qnm_camnoc_hf, &qxm_mdp0, &qxm_mdp1);
DEFINE_QBCM(bcm_sh2, "SH2", false, &alm_gpu_tcu, &alm_sys_tcu);
DEFINE_QBCM(bcm_mm2, "MM2", false, &qns_mem_noc_sf);
DEFINE_QBCM(bcm_qup0, "QUP0", false, &qhm_qup1, &qhm_qup2, &qhm_qup0);
DEFINE_QBCM(bcm_sh3, "SH3", false, &qnm_cmpnoc);
DEFINE_QBCM(bcm_mm3, "MM3", false, &qnm_camnoc_icp, &qnm_camnoc_sf, &qnm_video0, &qnm_video1, &qnm_video_cvp);
DEFINE_QBCM(bcm_sh4, "SH4", false, &chm_apps);
DEFINE_QBCM(bcm_sn0, "SN0", true, &qns_gemnoc_sf);
DEFINE_QBCM(bcm_co0, "CO0", false, &qns_cdsp_mem_noc);
DEFINE_QBCM(bcm_cn0, "CN0", true, &qnm_snoc, &xm_qdss_dap, &qhs_a1_noc_cfg, &qhs_a2_noc_cfg, &qhs_ahb2phy0, &qhs_ahb2phy1, &qhs_aoss, &qhs_camera_cfg, &qhs_clk_ctl, &qhs_compute_dsp, &qhs_cpr_cx, &qhs_cpr_mmcx, &qhs_cpr_mx, &qhs_crypto0_cfg, &qhs_cx_rdpm, &qhs_dcc_cfg, &qhs_ddrss_cfg, &qhs_display_cfg, &qhs_gpuss_cfg, &qhs_imem_cfg, &qhs_ipa, &qhs_ipc_router, &qhs_lpass_cfg, &qhs_mnoc_cfg, &qhs_npu_cfg, &qhs_pcie0_cfg, &qhs_pcie1_cfg, &qhs_pcie_modem_cfg, &qhs_pdm, &qhs_pimem_cfg, &qhs_prng, &qhs_qdss_cfg, &qhs_qspi, &qhs_qup0, &qhs_qup1, &qhs_qup2, &qhs_sdc2, &qhs_sdc4, &qhs_snoc_cfg, &qhs_tcsr, &qhs_tlmm0, &qhs_tlmm1, &qhs_tlmm2, &qhs_tsif, &qhs_ufs_card_cfg, &qhs_ufs_mem_cfg, &qhs_usb3_0, &qhs_usb3_1, &qhs_venus_cfg, &qhs_vsense_ctrl_cfg, &qns_cnoc_a2noc, &srvc_cnoc);
DEFINE_QBCM(bcm_sn1, "SN1", false, &qxs_imem);
DEFINE_QBCM(bcm_sn2, "SN2", false, &qns_gemnoc_gc);
DEFINE_QBCM(bcm_co2, "CO2", false, &qnm_npu);
DEFINE_QBCM(bcm_sn3, "SN3", false, &qxs_pimem);
DEFINE_QBCM(bcm_sn4, "SN4", false, &xs_qdss_stm);
DEFINE_QBCM(bcm_sn5, "SN5", false, &xs_pcie_modem);
DEFINE_QBCM(bcm_sn6, "SN6", false, &xs_pcie_0, &xs_pcie_1);
DEFINE_QBCM(bcm_sn7, "SN7", false, &qnm_aggre1_noc);
DEFINE_QBCM(bcm_sn8, "SN8", false, &qnm_aggre2_noc);
DEFINE_QBCM(bcm_sn9, "SN9", false, &qnm_gemnoc_pcie);
DEFINE_QBCM(bcm_sn11, "SN11", false, &qnm_gemnoc);
DEFINE_QBCM(bcm_sn12, "SN12", false, &qns_pcie_modem_mem_noc, &qns_pcie_mem_noc);

static struct qcom_icc_bcm *aggre1_noc_bcms[] = {
	&bcm_qup0,
	&bcm_sn12,
};

static struct qcom_icc_node *aggre1_noc_nodes[] = {
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

static struct qcom_icc_desc sm8250_aggre1_noc = {
	.nodes = aggre1_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre1_noc_nodes),
	.bcms = aggre1_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_noc_bcms),
};

static struct qcom_icc_bcm *aggre2_noc_bcms[] = {
	&bcm_ce0,
	&bcm_qup0,
	&bcm_sn12,
};

static struct qcom_icc_node *aggre2_noc_nodes[] = {
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

static struct qcom_icc_desc sm8250_aggre2_noc = {
	.nodes = aggre2_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre2_noc_nodes),
	.bcms = aggre2_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_noc_bcms),
};

static struct qcom_icc_bcm *compute_noc_bcms[] = {
	&bcm_co0,
	&bcm_co2,
};

static struct qcom_icc_node *compute_noc_nodes[] = {
	[MASTER_NPU] = &qnm_npu,
	[SLAVE_CDSP_MEM_NOC] = &qns_cdsp_mem_noc,
};

static struct qcom_icc_desc sm8250_compute_noc = {
	.nodes = compute_noc_nodes,
	.num_nodes = ARRAY_SIZE(compute_noc_nodes),
	.bcms = compute_noc_bcms,
	.num_bcms = ARRAY_SIZE(compute_noc_bcms),
};

static struct qcom_icc_bcm *config_noc_bcms[] = {
	&bcm_cn0,
};

static struct qcom_icc_node *config_noc_nodes[] = {
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

static struct qcom_icc_desc sm8250_config_noc = {
	.nodes = config_noc_nodes,
	.num_nodes = ARRAY_SIZE(config_noc_nodes),
	.bcms = config_noc_bcms,
	.num_bcms = ARRAY_SIZE(config_noc_bcms),
};

static struct qcom_icc_bcm *dc_noc_bcms[] = {
};

static struct qcom_icc_node *dc_noc_nodes[] = {
	[MASTER_CNOC_DC_NOC] = &qhm_cnoc_dc_noc,
	[SLAVE_LLCC_CFG] = &qhs_llcc,
	[SLAVE_GEM_NOC_CFG] = &qhs_memnoc,
};

static struct qcom_icc_desc sm8250_dc_noc = {
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
};

static struct qcom_icc_node *gem_noc_nodes[] = {
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

static struct qcom_icc_desc sm8250_gem_noc = {
	.nodes = gem_noc_nodes,
	.num_nodes = ARRAY_SIZE(gem_noc_nodes),
	.bcms = gem_noc_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_bcms),
};

static struct qcom_icc_bcm *ipa_virt_bcms[] = {
	&bcm_ip0,
};

static struct qcom_icc_node *ipa_virt_nodes[] = {
	[MASTER_IPA_CORE] = &ipa_core_master,
	[SLAVE_IPA_CORE] = &ipa_core_slave,
};

static struct qcom_icc_desc sm8250_ipa_virt = {
	.nodes = ipa_virt_nodes,
	.num_nodes = ARRAY_SIZE(ipa_virt_nodes),
	.bcms = ipa_virt_bcms,
	.num_bcms = ARRAY_SIZE(ipa_virt_bcms),
};

static struct qcom_icc_bcm *mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
};

static struct qcom_icc_node *mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI_CH0] = &ebi,
};

static struct qcom_icc_desc sm8250_mc_virt = {
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm *mmss_noc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
	&bcm_mm2,
	&bcm_mm3,
};

static struct qcom_icc_node *mmss_noc_nodes[] = {
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

static struct qcom_icc_desc sm8250_mmss_noc = {
	.nodes = mmss_noc_nodes,
	.num_nodes = ARRAY_SIZE(mmss_noc_nodes),
	.bcms = mmss_noc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_bcms),
};

static struct qcom_icc_bcm *npu_noc_bcms[] = {
};

static struct qcom_icc_node *npu_noc_nodes[] = {
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

static struct qcom_icc_desc sm8250_npu_noc = {
	.nodes = npu_noc_nodes,
	.num_nodes = ARRAY_SIZE(npu_noc_nodes),
	.bcms = npu_noc_bcms,
	.num_bcms = ARRAY_SIZE(npu_noc_bcms),
};

static struct qcom_icc_bcm *system_noc_bcms[] = {
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

static struct qcom_icc_node *system_noc_nodes[] = {
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

static struct qcom_icc_desc sm8250_system_noc = {
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static int qnoc_probe(struct platform_device *pdev)
{
	const struct qcom_icc_desc *desc;
	struct icc_onecell_data *data;
	struct icc_provider *provider;
	struct qcom_icc_node **qnodes;
	struct qcom_icc_provider *qp;
	struct icc_node *node;
	size_t num_nodes, i;
	int ret;

	desc = device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	qnodes = desc->nodes;
	num_nodes = desc->num_nodes;

	qp = devm_kzalloc(&pdev->dev, sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return -ENOMEM;

	data = devm_kcalloc(&pdev->dev, num_nodes, sizeof(*node), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	provider = &qp->provider;
	provider->dev = &pdev->dev;
	provider->set = qcom_icc_set;
	provider->pre_aggregate = qcom_icc_pre_aggregate;
	provider->aggregate = qcom_icc_aggregate;
	provider->xlate = of_icc_xlate_onecell;
	INIT_LIST_HEAD(&provider->nodes);
	provider->data = data;

	qp->dev = &pdev->dev;
	qp->bcms = desc->bcms;
	qp->num_bcms = desc->num_bcms;

	qp->voter = of_bcm_voter_get(qp->dev, NULL);
	if (IS_ERR(qp->voter))
		return PTR_ERR(qp->voter);

	ret = icc_provider_add(provider);
	if (ret) {
		dev_err(&pdev->dev, "error adding interconnect provider\n");
		return ret;
	}

	for (i = 0; i < qp->num_bcms; i++)
		qcom_icc_bcm_init(qp->bcms[i], &pdev->dev);

	for (i = 0; i < num_nodes; i++) {
		size_t j;

		if (!qnodes[i])
			continue;

		node = icc_node_create(qnodes[i]->id);
		if (IS_ERR(node)) {
			ret = PTR_ERR(node);
			goto err;
		}

		node->name = qnodes[i]->name;
		node->data = qnodes[i];
		icc_node_add(node, provider);

		for (j = 0; j < qnodes[i]->num_links; j++)
			icc_link_create(node, qnodes[i]->links[j]);

		data->nodes[i] = node;
	}
	data->num_nodes = num_nodes;

	platform_set_drvdata(pdev, qp);

	return 0;
err:
	icc_nodes_remove(provider);
	icc_provider_del(provider);
	return ret;
}

static int qnoc_remove(struct platform_device *pdev)
{
	struct qcom_icc_provider *qp = platform_get_drvdata(pdev);

	icc_nodes_remove(&qp->provider);
	return icc_provider_del(&qp->provider);
}

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
	{ .compatible = "qcom,sm8250-ipa-virt",
	  .data = &sm8250_ipa_virt},
	{ .compatible = "qcom,sm8250-mc-virt",
	  .data = &sm8250_mc_virt},
	{ .compatible = "qcom,sm8250-mmss-noc",
	  .data = &sm8250_mmss_noc},
	{ .compatible = "qcom,sm8250-npu-noc",
	  .data = &sm8250_npu_noc},
	{ .compatible = "qcom,sm8250-system-noc",
	  .data = &sm8250_system_noc},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qnoc_probe,
	.remove = qnoc_remove,
	.driver = {
		.name = "qnoc-sm8250",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(qnoc_driver);

MODULE_DESCRIPTION("Qualcomm SM8250 NoC driver");
MODULE_LICENSE("GPL v2");
