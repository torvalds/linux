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
#include <dt-bindings/interconnect/qcom,sc7180.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"
#include "sc7180.h"

DEFINE_QNODE(qhm_a1noc_cfg, SC7180_MASTER_A1NOC_CFG, 1, 4, SC7180_SLAVE_SERVICE_A1NOC);
DEFINE_QNODE(qhm_qspi, SC7180_MASTER_QSPI, 1, 4, SC7180_SLAVE_A1NOC_SNOC);
DEFINE_QNODE(qhm_qup_0, SC7180_MASTER_QUP_0, 1, 4, SC7180_SLAVE_A1NOC_SNOC);
DEFINE_QNODE(xm_sdc2, SC7180_MASTER_SDCC_2, 1, 8, SC7180_SLAVE_A1NOC_SNOC);
DEFINE_QNODE(xm_emmc, SC7180_MASTER_EMMC, 1, 8, SC7180_SLAVE_A1NOC_SNOC);
DEFINE_QNODE(xm_ufs_mem, SC7180_MASTER_UFS_MEM, 1, 8, SC7180_SLAVE_A1NOC_SNOC);
DEFINE_QNODE(qhm_a2noc_cfg, SC7180_MASTER_A2NOC_CFG, 1, 4, SC7180_SLAVE_SERVICE_A2NOC);
DEFINE_QNODE(qhm_qdss_bam, SC7180_MASTER_QDSS_BAM, 1, 4, SC7180_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(qhm_qup_1, SC7180_MASTER_QUP_1, 1, 4, SC7180_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(qxm_crypto, SC7180_MASTER_CRYPTO, 1, 8, SC7180_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(qxm_ipa, SC7180_MASTER_IPA, 1, 8, SC7180_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(xm_qdss_etr, SC7180_MASTER_QDSS_ETR, 1, 8, SC7180_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(qhm_usb3, SC7180_MASTER_USB3, 1, 8, SC7180_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(qxm_camnoc_hf0_uncomp, SC7180_MASTER_CAMNOC_HF0_UNCOMP, 1, 32, SC7180_SLAVE_CAMNOC_UNCOMP);
DEFINE_QNODE(qxm_camnoc_hf1_uncomp, SC7180_MASTER_CAMNOC_HF1_UNCOMP, 1, 32, SC7180_SLAVE_CAMNOC_UNCOMP);
DEFINE_QNODE(qxm_camnoc_sf_uncomp, SC7180_MASTER_CAMNOC_SF_UNCOMP, 1, 32, SC7180_SLAVE_CAMNOC_UNCOMP);
DEFINE_QNODE(qnm_npu, SC7180_MASTER_NPU, 2, 32, SC7180_SLAVE_CDSP_GEM_NOC);
DEFINE_QNODE(qxm_npu_dsp, SC7180_MASTER_NPU_PROC, 1, 8, SC7180_SLAVE_CDSP_GEM_NOC);
DEFINE_QNODE(qnm_snoc, SC7180_MASTER_SNOC_CNOC, 1, 8, SC7180_SLAVE_A1NOC_CFG, SC7180_SLAVE_A2NOC_CFG, SC7180_SLAVE_AHB2PHY_SOUTH, SC7180_SLAVE_AHB2PHY_CENTER, SC7180_SLAVE_AOP, SC7180_SLAVE_AOSS, SC7180_SLAVE_BOOT_ROM, SC7180_SLAVE_CAMERA_CFG, SC7180_SLAVE_CAMERA_NRT_THROTTLE_CFG, SC7180_SLAVE_CAMERA_RT_THROTTLE_CFG, SC7180_SLAVE_CLK_CTL, SC7180_SLAVE_RBCPR_CX_CFG, SC7180_SLAVE_RBCPR_MX_CFG, SC7180_SLAVE_CRYPTO_0_CFG, SC7180_SLAVE_DCC_CFG, SC7180_SLAVE_CNOC_DDRSS, SC7180_SLAVE_DISPLAY_CFG, SC7180_SLAVE_DISPLAY_RT_THROTTLE_CFG, SC7180_SLAVE_DISPLAY_THROTTLE_CFG, SC7180_SLAVE_EMMC_CFG, SC7180_SLAVE_GLM,
		SC7180_SLAVE_GFX3D_CFG, SC7180_SLAVE_IMEM_CFG, SC7180_SLAVE_IPA_CFG, SC7180_SLAVE_CNOC_MNOC_CFG, SC7180_SLAVE_CNOC_MSS, SC7180_SLAVE_NPU_CFG, SC7180_SLAVE_NPU_DMA_BWMON_CFG, SC7180_SLAVE_NPU_PROC_BWMON_CFG, SC7180_SLAVE_PDM, SC7180_SLAVE_PIMEM_CFG, SC7180_SLAVE_PRNG, SC7180_SLAVE_QDSS_CFG, SC7180_SLAVE_QM_CFG, SC7180_SLAVE_QM_MPU_CFG, SC7180_SLAVE_QSPI_0, SC7180_SLAVE_QUP_0, SC7180_SLAVE_QUP_1, SC7180_SLAVE_SDCC_2, SC7180_SLAVE_SECURITY, SC7180_SLAVE_SNOC_CFG, SC7180_SLAVE_TCSR, SC7180_SLAVE_TLMM_WEST, SC7180_SLAVE_TLMM_NORTH, SC7180_SLAVE_TLMM_SOUTH, SC7180_SLAVE_UFS_MEM_CFG, SC7180_SLAVE_USB3, SC7180_SLAVE_VENUS_CFG, SC7180_SLAVE_VENUS_THROTTLE_CFG, SC7180_SLAVE_VSENSE_CTRL_CFG, SC7180_SLAVE_SERVICE_CNOC);
DEFINE_QNODE(xm_qdss_dap, SC7180_MASTER_QDSS_DAP, 1, 8, SC7180_SLAVE_A1NOC_CFG, SC7180_SLAVE_A2NOC_CFG, SC7180_SLAVE_AHB2PHY_SOUTH, SC7180_SLAVE_AHB2PHY_CENTER, SC7180_SLAVE_AOP, SC7180_SLAVE_AOSS, SC7180_SLAVE_BOOT_ROM, SC7180_SLAVE_CAMERA_CFG, SC7180_SLAVE_CAMERA_NRT_THROTTLE_CFG, SC7180_SLAVE_CAMERA_RT_THROTTLE_CFG, SC7180_SLAVE_CLK_CTL, SC7180_SLAVE_RBCPR_CX_CFG, SC7180_SLAVE_RBCPR_MX_CFG, SC7180_SLAVE_CRYPTO_0_CFG, SC7180_SLAVE_DCC_CFG, SC7180_SLAVE_CNOC_DDRSS, SC7180_SLAVE_DISPLAY_CFG, SC7180_SLAVE_DISPLAY_RT_THROTTLE_CFG, SC7180_SLAVE_DISPLAY_THROTTLE_CFG, SC7180_SLAVE_EMMC_CFG, SC7180_SLAVE_GLM, SC7180_SLAVE_GFX3D_CFG, SC7180_SLAVE_IMEM_CFG, SC7180_SLAVE_IPA_CFG, SC7180_SLAVE_CNOC_MNOC_CFG, SC7180_SLAVE_CNOC_MSS, SC7180_SLAVE_NPU_CFG, SC7180_SLAVE_NPU_DMA_BWMON_CFG,
SC7180_SLAVE_NPU_PROC_BWMON_CFG, SC7180_SLAVE_PDM, SC7180_SLAVE_PIMEM_CFG, SC7180_SLAVE_PRNG, SC7180_SLAVE_QDSS_CFG, SC7180_SLAVE_QM_CFG, SC7180_SLAVE_QM_MPU_CFG, SC7180_SLAVE_QSPI_0, SC7180_SLAVE_QUP_0, SC7180_SLAVE_QUP_1, SC7180_SLAVE_SDCC_2, SC7180_SLAVE_SECURITY, SC7180_SLAVE_SNOC_CFG, SC7180_SLAVE_TCSR, SC7180_SLAVE_TLMM_WEST, SC7180_SLAVE_TLMM_NORTH, SC7180_SLAVE_TLMM_SOUTH, SC7180_SLAVE_UFS_MEM_CFG, SC7180_SLAVE_USB3, SC7180_SLAVE_VENUS_CFG, SC7180_SLAVE_VENUS_THROTTLE_CFG, SC7180_SLAVE_VSENSE_CTRL_CFG, SC7180_SLAVE_SERVICE_CNOC);
DEFINE_QNODE(qhm_cnoc_dc_noc, SC7180_MASTER_CNOC_DC_NOC, 1, 4, SC7180_SLAVE_GEM_NOC_CFG, SC7180_SLAVE_LLCC_CFG);
DEFINE_QNODE(acm_apps0, SC7180_MASTER_APPSS_PROC, 1, 16, SC7180_SLAVE_GEM_NOC_SNOC, SC7180_SLAVE_LLCC);
DEFINE_QNODE(acm_sys_tcu, SC7180_MASTER_SYS_TCU, 1, 8, SC7180_SLAVE_GEM_NOC_SNOC, SC7180_SLAVE_LLCC);
DEFINE_QNODE(qhm_gemnoc_cfg, SC7180_MASTER_GEM_NOC_CFG, 1, 4, SC7180_SLAVE_MSS_PROC_MS_MPU_CFG, SC7180_SLAVE_SERVICE_GEM_NOC);
DEFINE_QNODE(qnm_cmpnoc, SC7180_MASTER_COMPUTE_NOC, 1, 32, SC7180_SLAVE_GEM_NOC_SNOC, SC7180_SLAVE_LLCC);
DEFINE_QNODE(qnm_mnoc_hf, SC7180_MASTER_MNOC_HF_MEM_NOC, 1, 32, SC7180_SLAVE_LLCC);
DEFINE_QNODE(qnm_mnoc_sf, SC7180_MASTER_MNOC_SF_MEM_NOC, 1, 32, SC7180_SLAVE_GEM_NOC_SNOC, SC7180_SLAVE_LLCC);
DEFINE_QNODE(qnm_snoc_gc, SC7180_MASTER_SNOC_GC_MEM_NOC, 1, 8, SC7180_SLAVE_LLCC);
DEFINE_QNODE(qnm_snoc_sf, SC7180_MASTER_SNOC_SF_MEM_NOC, 1, 16, SC7180_SLAVE_LLCC);
DEFINE_QNODE(qxm_gpu, SC7180_MASTER_GFX3D, 2, 32, SC7180_SLAVE_GEM_NOC_SNOC, SC7180_SLAVE_LLCC);
DEFINE_QNODE(llcc_mc, SC7180_MASTER_LLCC, 2, 4, SC7180_SLAVE_EBI1);
DEFINE_QNODE(qhm_mnoc_cfg, SC7180_MASTER_CNOC_MNOC_CFG, 1, 4, SC7180_SLAVE_SERVICE_MNOC);
DEFINE_QNODE(qxm_camnoc_hf0, SC7180_MASTER_CAMNOC_HF0, 2, 32, SC7180_SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(qxm_camnoc_hf1, SC7180_MASTER_CAMNOC_HF1, 2, 32, SC7180_SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(qxm_camnoc_sf, SC7180_MASTER_CAMNOC_SF, 1, 32, SC7180_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qxm_mdp0, SC7180_MASTER_MDP0, 1, 32, SC7180_SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(qxm_rot, SC7180_MASTER_ROTATOR, 1, 16, SC7180_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qxm_venus0, SC7180_MASTER_VIDEO_P0, 1, 32, SC7180_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(qxm_venus_arm9, SC7180_MASTER_VIDEO_PROC, 1, 8, SC7180_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(amm_npu_sys, SC7180_MASTER_NPU_SYS, 2, 32, SC7180_SLAVE_NPU_COMPUTE_NOC);
DEFINE_QNODE(qhm_npu_cfg, SC7180_MASTER_NPU_NOC_CFG, 1, 4, SC7180_SLAVE_NPU_CAL_DP0, SC7180_SLAVE_NPU_CP, SC7180_SLAVE_NPU_INT_DMA_BWMON_CFG, SC7180_SLAVE_NPU_DPM, SC7180_SLAVE_ISENSE_CFG, SC7180_SLAVE_NPU_LLM_CFG, SC7180_SLAVE_NPU_TCM, SC7180_SLAVE_SERVICE_NPU_NOC);
DEFINE_QNODE(qup_core_master_1, SC7180_MASTER_QUP_CORE_0, 1, 4, SC7180_SLAVE_QUP_CORE_0);
DEFINE_QNODE(qup_core_master_2, SC7180_MASTER_QUP_CORE_1, 1, 4, SC7180_SLAVE_QUP_CORE_1);
DEFINE_QNODE(qhm_snoc_cfg, SC7180_MASTER_SNOC_CFG, 1, 4, SC7180_SLAVE_SERVICE_SNOC);
DEFINE_QNODE(qnm_aggre1_noc, SC7180_MASTER_A1NOC_SNOC, 1, 16, SC7180_SLAVE_APPSS, SC7180_SLAVE_SNOC_CNOC, SC7180_SLAVE_SNOC_GEM_NOC_SF, SC7180_SLAVE_IMEM, SC7180_SLAVE_PIMEM, SC7180_SLAVE_QDSS_STM);
DEFINE_QNODE(qnm_aggre2_noc, SC7180_MASTER_A2NOC_SNOC, 1, 16, SC7180_SLAVE_APPSS, SC7180_SLAVE_SNOC_CNOC, SC7180_SLAVE_SNOC_GEM_NOC_SF, SC7180_SLAVE_IMEM, SC7180_SLAVE_PIMEM, SC7180_SLAVE_QDSS_STM, SC7180_SLAVE_TCU);
DEFINE_QNODE(qnm_gemnoc, SC7180_MASTER_GEM_NOC_SNOC, 1, 8, SC7180_SLAVE_APPSS, SC7180_SLAVE_SNOC_CNOC, SC7180_SLAVE_IMEM, SC7180_SLAVE_PIMEM, SC7180_SLAVE_QDSS_STM, SC7180_SLAVE_TCU);
DEFINE_QNODE(qxm_pimem, SC7180_MASTER_PIMEM, 1, 8, SC7180_SLAVE_SNOC_GEM_NOC_GC, SC7180_SLAVE_IMEM);
DEFINE_QNODE(qns_a1noc_snoc, SC7180_SLAVE_A1NOC_SNOC, 1, 16, SC7180_MASTER_A1NOC_SNOC);
DEFINE_QNODE(srvc_aggre1_noc, SC7180_SLAVE_SERVICE_A1NOC, 1, 4);
DEFINE_QNODE(qns_a2noc_snoc, SC7180_SLAVE_A2NOC_SNOC, 1, 16, SC7180_MASTER_A2NOC_SNOC);
DEFINE_QNODE(srvc_aggre2_noc, SC7180_SLAVE_SERVICE_A2NOC, 1, 4);
DEFINE_QNODE(qns_camnoc_uncomp, SC7180_SLAVE_CAMNOC_UNCOMP, 1, 32);
DEFINE_QNODE(qns_cdsp_gemnoc, SC7180_SLAVE_CDSP_GEM_NOC, 1, 32, SC7180_MASTER_COMPUTE_NOC);
DEFINE_QNODE(qhs_a1_noc_cfg, SC7180_SLAVE_A1NOC_CFG, 1, 4, SC7180_MASTER_A1NOC_CFG);
DEFINE_QNODE(qhs_a2_noc_cfg, SC7180_SLAVE_A2NOC_CFG, 1, 4, SC7180_MASTER_A2NOC_CFG);
DEFINE_QNODE(qhs_ahb2phy0, SC7180_SLAVE_AHB2PHY_SOUTH, 1, 4);
DEFINE_QNODE(qhs_ahb2phy2, SC7180_SLAVE_AHB2PHY_CENTER, 1, 4);
DEFINE_QNODE(qhs_aop, SC7180_SLAVE_AOP, 1, 4);
DEFINE_QNODE(qhs_aoss, SC7180_SLAVE_AOSS, 1, 4);
DEFINE_QNODE(qhs_boot_rom, SC7180_SLAVE_BOOT_ROM, 1, 4);
DEFINE_QNODE(qhs_camera_cfg, SC7180_SLAVE_CAMERA_CFG, 1, 4);
DEFINE_QNODE(qhs_camera_nrt_throttle_cfg, SC7180_SLAVE_CAMERA_NRT_THROTTLE_CFG, 1, 4);
DEFINE_QNODE(qhs_camera_rt_throttle_cfg, SC7180_SLAVE_CAMERA_RT_THROTTLE_CFG, 1, 4);
DEFINE_QNODE(qhs_clk_ctl, SC7180_SLAVE_CLK_CTL, 1, 4);
DEFINE_QNODE(qhs_cpr_cx, SC7180_SLAVE_RBCPR_CX_CFG, 1, 4);
DEFINE_QNODE(qhs_cpr_mx, SC7180_SLAVE_RBCPR_MX_CFG, 1, 4);
DEFINE_QNODE(qhs_crypto0_cfg, SC7180_SLAVE_CRYPTO_0_CFG, 1, 4);
DEFINE_QNODE(qhs_dcc_cfg, SC7180_SLAVE_DCC_CFG, 1, 4);
DEFINE_QNODE(qhs_ddrss_cfg, SC7180_SLAVE_CNOC_DDRSS, 1, 4, SC7180_MASTER_CNOC_DC_NOC);
DEFINE_QNODE(qhs_display_cfg, SC7180_SLAVE_DISPLAY_CFG, 1, 4);
DEFINE_QNODE(qhs_display_rt_throttle_cfg, SC7180_SLAVE_DISPLAY_RT_THROTTLE_CFG, 1, 4);
DEFINE_QNODE(qhs_display_throttle_cfg, SC7180_SLAVE_DISPLAY_THROTTLE_CFG, 1, 4);
DEFINE_QNODE(qhs_emmc_cfg, SC7180_SLAVE_EMMC_CFG, 1, 4);
DEFINE_QNODE(qhs_glm, SC7180_SLAVE_GLM, 1, 4);
DEFINE_QNODE(qhs_gpuss_cfg, SC7180_SLAVE_GFX3D_CFG, 1, 8);
DEFINE_QNODE(qhs_imem_cfg, SC7180_SLAVE_IMEM_CFG, 1, 4);
DEFINE_QNODE(qhs_ipa, SC7180_SLAVE_IPA_CFG, 1, 4);
DEFINE_QNODE(qhs_mnoc_cfg, SC7180_SLAVE_CNOC_MNOC_CFG, 1, 4, SC7180_MASTER_CNOC_MNOC_CFG);
DEFINE_QNODE(qhs_mss_cfg, SC7180_SLAVE_CNOC_MSS, 1, 4);
DEFINE_QNODE(qhs_npu_cfg, SC7180_SLAVE_NPU_CFG, 1, 4, SC7180_MASTER_NPU_NOC_CFG);
DEFINE_QNODE(qhs_npu_dma_throttle_cfg, SC7180_SLAVE_NPU_DMA_BWMON_CFG, 1, 4);
DEFINE_QNODE(qhs_npu_dsp_throttle_cfg, SC7180_SLAVE_NPU_PROC_BWMON_CFG, 1, 4);
DEFINE_QNODE(qhs_pdm, SC7180_SLAVE_PDM, 1, 4);
DEFINE_QNODE(qhs_pimem_cfg, SC7180_SLAVE_PIMEM_CFG, 1, 4);
DEFINE_QNODE(qhs_prng, SC7180_SLAVE_PRNG, 1, 4);
DEFINE_QNODE(qhs_qdss_cfg, SC7180_SLAVE_QDSS_CFG, 1, 4);
DEFINE_QNODE(qhs_qm_cfg, SC7180_SLAVE_QM_CFG, 1, 4);
DEFINE_QNODE(qhs_qm_mpu_cfg, SC7180_SLAVE_QM_MPU_CFG, 1, 4);
DEFINE_QNODE(qhs_qspi, SC7180_SLAVE_QSPI_0, 1, 4);
DEFINE_QNODE(qhs_qup0, SC7180_SLAVE_QUP_0, 1, 4);
DEFINE_QNODE(qhs_qup1, SC7180_SLAVE_QUP_1, 1, 4);
DEFINE_QNODE(qhs_sdc2, SC7180_SLAVE_SDCC_2, 1, 4);
DEFINE_QNODE(qhs_security, SC7180_SLAVE_SECURITY, 1, 4);
DEFINE_QNODE(qhs_snoc_cfg, SC7180_SLAVE_SNOC_CFG, 1, 4, SC7180_MASTER_SNOC_CFG);
DEFINE_QNODE(qhs_tcsr, SC7180_SLAVE_TCSR, 1, 4);
DEFINE_QNODE(qhs_tlmm_1, SC7180_SLAVE_TLMM_WEST, 1, 4);
DEFINE_QNODE(qhs_tlmm_2, SC7180_SLAVE_TLMM_NORTH, 1, 4);
DEFINE_QNODE(qhs_tlmm_3, SC7180_SLAVE_TLMM_SOUTH, 1, 4);
DEFINE_QNODE(qhs_ufs_mem_cfg, SC7180_SLAVE_UFS_MEM_CFG, 1, 4);
DEFINE_QNODE(qhs_usb3, SC7180_SLAVE_USB3, 1, 4);
DEFINE_QNODE(qhs_venus_cfg, SC7180_SLAVE_VENUS_CFG, 1, 4);
DEFINE_QNODE(qhs_venus_throttle_cfg, SC7180_SLAVE_VENUS_THROTTLE_CFG, 1, 4);
DEFINE_QNODE(qhs_vsense_ctrl_cfg, SC7180_SLAVE_VSENSE_CTRL_CFG, 1, 4);
DEFINE_QNODE(srvc_cnoc, SC7180_SLAVE_SERVICE_CNOC, 1, 4);
DEFINE_QNODE(qhs_gemnoc, SC7180_SLAVE_GEM_NOC_CFG, 1, 4, SC7180_MASTER_GEM_NOC_CFG);
DEFINE_QNODE(qhs_llcc, SC7180_SLAVE_LLCC_CFG, 1, 4);
DEFINE_QNODE(qhs_mdsp_ms_mpu_cfg, SC7180_SLAVE_MSS_PROC_MS_MPU_CFG, 1, 4);
DEFINE_QNODE(qns_gem_noc_snoc, SC7180_SLAVE_GEM_NOC_SNOC, 1, 8, SC7180_MASTER_GEM_NOC_SNOC);
DEFINE_QNODE(qns_llcc, SC7180_SLAVE_LLCC, 1, 16, SC7180_MASTER_LLCC);
DEFINE_QNODE(srvc_gemnoc, SC7180_SLAVE_SERVICE_GEM_NOC, 1, 4);
DEFINE_QNODE(ebi, SC7180_SLAVE_EBI1, 2, 4);
DEFINE_QNODE(qns_mem_noc_hf, SC7180_SLAVE_MNOC_HF_MEM_NOC, 1, 32, SC7180_MASTER_MNOC_HF_MEM_NOC);
DEFINE_QNODE(qns_mem_noc_sf, SC7180_SLAVE_MNOC_SF_MEM_NOC, 1, 32, SC7180_MASTER_MNOC_SF_MEM_NOC);
DEFINE_QNODE(srvc_mnoc, SC7180_SLAVE_SERVICE_MNOC, 1, 4);
DEFINE_QNODE(qhs_cal_dp0, SC7180_SLAVE_NPU_CAL_DP0, 1, 4);
DEFINE_QNODE(qhs_cp, SC7180_SLAVE_NPU_CP, 1, 4);
DEFINE_QNODE(qhs_dma_bwmon, SC7180_SLAVE_NPU_INT_DMA_BWMON_CFG, 1, 4);
DEFINE_QNODE(qhs_dpm, SC7180_SLAVE_NPU_DPM, 1, 4);
DEFINE_QNODE(qhs_isense, SC7180_SLAVE_ISENSE_CFG, 1, 4);
DEFINE_QNODE(qhs_llm, SC7180_SLAVE_NPU_LLM_CFG, 1, 4);
DEFINE_QNODE(qhs_tcm, SC7180_SLAVE_NPU_TCM, 1, 4);
DEFINE_QNODE(qns_npu_sys, SC7180_SLAVE_NPU_COMPUTE_NOC, 2, 32);
DEFINE_QNODE(srvc_noc, SC7180_SLAVE_SERVICE_NPU_NOC, 1, 4);
DEFINE_QNODE(qup_core_slave_1, SC7180_SLAVE_QUP_CORE_0, 1, 4);
DEFINE_QNODE(qup_core_slave_2, SC7180_SLAVE_QUP_CORE_1, 1, 4);
DEFINE_QNODE(qhs_apss, SC7180_SLAVE_APPSS, 1, 8);
DEFINE_QNODE(qns_cnoc, SC7180_SLAVE_SNOC_CNOC, 1, 8, SC7180_MASTER_SNOC_CNOC);
DEFINE_QNODE(qns_gemnoc_gc, SC7180_SLAVE_SNOC_GEM_NOC_GC, 1, 8, SC7180_MASTER_SNOC_GC_MEM_NOC);
DEFINE_QNODE(qns_gemnoc_sf, SC7180_SLAVE_SNOC_GEM_NOC_SF, 1, 16, SC7180_MASTER_SNOC_SF_MEM_NOC);
DEFINE_QNODE(qxs_imem, SC7180_SLAVE_IMEM, 1, 8);
DEFINE_QNODE(qxs_pimem, SC7180_SLAVE_PIMEM, 1, 8);
DEFINE_QNODE(srvc_snoc, SC7180_SLAVE_SERVICE_SNOC, 1, 4);
DEFINE_QNODE(xs_qdss_stm, SC7180_SLAVE_QDSS_STM, 1, 4);
DEFINE_QNODE(xs_sys_tcu_cfg, SC7180_SLAVE_TCU, 1, 8);

DEFINE_QBCM(bcm_acv, "ACV", false, &ebi);
DEFINE_QBCM(bcm_mc0, "MC0", true, &ebi);
DEFINE_QBCM(bcm_sh0, "SH0", true, &qns_llcc);
DEFINE_QBCM(bcm_mm0, "MM0", false, &qns_mem_noc_hf);
DEFINE_QBCM(bcm_ce0, "CE0", false, &qxm_crypto);
DEFINE_QBCM(bcm_cn0, "CN0", true, &qnm_snoc, &xm_qdss_dap, &qhs_a1_noc_cfg, &qhs_a2_noc_cfg, &qhs_ahb2phy0, &qhs_aop, &qhs_aoss, &qhs_boot_rom, &qhs_camera_cfg, &qhs_camera_nrt_throttle_cfg, &qhs_camera_rt_throttle_cfg, &qhs_clk_ctl, &qhs_cpr_cx, &qhs_cpr_mx, &qhs_crypto0_cfg, &qhs_dcc_cfg, &qhs_ddrss_cfg, &qhs_display_cfg, &qhs_display_rt_throttle_cfg, &qhs_display_throttle_cfg, &qhs_glm, &qhs_gpuss_cfg, &qhs_imem_cfg, &qhs_ipa, &qhs_mnoc_cfg, &qhs_mss_cfg, &qhs_npu_cfg, &qhs_npu_dma_throttle_cfg, &qhs_npu_dsp_throttle_cfg, &qhs_pimem_cfg, &qhs_prng, &qhs_qdss_cfg, &qhs_qm_cfg, &qhs_qm_mpu_cfg, &qhs_qup0, &qhs_qup1, &qhs_security, &qhs_snoc_cfg, &qhs_tcsr, &qhs_tlmm_1, &qhs_tlmm_2, &qhs_tlmm_3, &qhs_ufs_mem_cfg, &qhs_usb3, &qhs_venus_cfg, &qhs_venus_throttle_cfg, &qhs_vsense_ctrl_cfg, &srvc_cnoc);
DEFINE_QBCM(bcm_mm1, "MM1", false, &qxm_camnoc_hf0_uncomp, &qxm_camnoc_hf1_uncomp, &qxm_camnoc_sf_uncomp, &qhm_mnoc_cfg, &qxm_mdp0, &qxm_rot, &qxm_venus0, &qxm_venus_arm9);
DEFINE_QBCM(bcm_sh2, "SH2", false, &acm_sys_tcu);
DEFINE_QBCM(bcm_mm2, "MM2", false, &qns_mem_noc_sf);
DEFINE_QBCM(bcm_qup0, "QUP0", false, &qup_core_master_1, &qup_core_master_2);
DEFINE_QBCM(bcm_sh3, "SH3", false, &qnm_cmpnoc);
DEFINE_QBCM(bcm_sh4, "SH4", false, &acm_apps0);
DEFINE_QBCM(bcm_sn0, "SN0", true, &qns_gemnoc_sf);
DEFINE_QBCM(bcm_co0, "CO0", false, &qns_cdsp_gemnoc);
DEFINE_QBCM(bcm_sn1, "SN1", false, &qxs_imem);
DEFINE_QBCM(bcm_cn1, "CN1", false, &qhm_qspi, &xm_sdc2, &xm_emmc, &qhs_ahb2phy2, &qhs_emmc_cfg, &qhs_pdm, &qhs_qspi, &qhs_sdc2);
DEFINE_QBCM(bcm_sn2, "SN2", false, &qxm_pimem, &qns_gemnoc_gc);
DEFINE_QBCM(bcm_co2, "CO2", false, &qnm_npu);
DEFINE_QBCM(bcm_sn3, "SN3", false, &qxs_pimem);
DEFINE_QBCM(bcm_co3, "CO3", false, &qxm_npu_dsp);
DEFINE_QBCM(bcm_sn4, "SN4", false, &xs_qdss_stm);
DEFINE_QBCM(bcm_sn7, "SN7", false, &qnm_aggre1_noc);
DEFINE_QBCM(bcm_sn9, "SN9", false, &qnm_aggre2_noc);
DEFINE_QBCM(bcm_sn12, "SN12", false, &qnm_gemnoc);

static struct qcom_icc_bcm * const aggre1_noc_bcms[] = {
	&bcm_cn1,
};

static struct qcom_icc_node * const aggre1_noc_nodes[] = {
	[MASTER_A1NOC_CFG] = &qhm_a1noc_cfg,
	[MASTER_QSPI] = &qhm_qspi,
	[MASTER_QUP_0] = &qhm_qup_0,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_EMMC] = &xm_emmc,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[SLAVE_A1NOC_SNOC] = &qns_a1noc_snoc,
	[SLAVE_SERVICE_A1NOC] = &srvc_aggre1_noc,
};

static const struct qcom_icc_desc sc7180_aggre1_noc = {
	.nodes = aggre1_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre1_noc_nodes),
	.bcms = aggre1_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_noc_bcms),
};

static struct qcom_icc_bcm * const aggre2_noc_bcms[] = {
	&bcm_ce0,
};

static struct qcom_icc_node * const aggre2_noc_nodes[] = {
	[MASTER_A2NOC_CFG] = &qhm_a2noc_cfg,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QUP_1] = &qhm_qup_1,
	[MASTER_USB3] = &qhm_usb3,
	[MASTER_CRYPTO] = &qxm_crypto,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[SLAVE_A2NOC_SNOC] = &qns_a2noc_snoc,
	[SLAVE_SERVICE_A2NOC] = &srvc_aggre2_noc,
};

static const struct qcom_icc_desc sc7180_aggre2_noc = {
	.nodes = aggre2_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre2_noc_nodes),
	.bcms = aggre2_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_noc_bcms),
};

static struct qcom_icc_bcm * const camnoc_virt_bcms[] = {
	&bcm_mm1,
};

static struct qcom_icc_node * const camnoc_virt_nodes[] = {
	[MASTER_CAMNOC_HF0_UNCOMP] = &qxm_camnoc_hf0_uncomp,
	[MASTER_CAMNOC_HF1_UNCOMP] = &qxm_camnoc_hf1_uncomp,
	[MASTER_CAMNOC_SF_UNCOMP] = &qxm_camnoc_sf_uncomp,
	[SLAVE_CAMNOC_UNCOMP] = &qns_camnoc_uncomp,
};

static const struct qcom_icc_desc sc7180_camnoc_virt = {
	.nodes = camnoc_virt_nodes,
	.num_nodes = ARRAY_SIZE(camnoc_virt_nodes),
	.bcms = camnoc_virt_bcms,
	.num_bcms = ARRAY_SIZE(camnoc_virt_bcms),
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

static const struct qcom_icc_desc sc7180_compute_noc = {
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
	[MASTER_SNOC_CNOC] = &qnm_snoc,
	[MASTER_QDSS_DAP] = &xm_qdss_dap,
	[SLAVE_A1NOC_CFG] = &qhs_a1_noc_cfg,
	[SLAVE_A2NOC_CFG] = &qhs_a2_noc_cfg,
	[SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_CENTER] = &qhs_ahb2phy2,
	[SLAVE_AOP] = &qhs_aop,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_BOOT_ROM] = &qhs_boot_rom,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CAMERA_NRT_THROTTLE_CFG] = &qhs_camera_nrt_throttle_cfg,
	[SLAVE_CAMERA_RT_THROTTLE_CFG] = &qhs_camera_rt_throttle_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_DCC_CFG] = &qhs_dcc_cfg,
	[SLAVE_CNOC_DDRSS] = &qhs_ddrss_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[SLAVE_DISPLAY_RT_THROTTLE_CFG] = &qhs_display_rt_throttle_cfg,
	[SLAVE_DISPLAY_THROTTLE_CFG] = &qhs_display_throttle_cfg,
	[SLAVE_EMMC_CFG] = &qhs_emmc_cfg,
	[SLAVE_GLM] = &qhs_glm,
	[SLAVE_GFX3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_CNOC_MNOC_CFG] = &qhs_mnoc_cfg,
	[SLAVE_CNOC_MSS] = &qhs_mss_cfg,
	[SLAVE_NPU_CFG] = &qhs_npu_cfg,
	[SLAVE_NPU_DMA_BWMON_CFG] = &qhs_npu_dma_throttle_cfg,
	[SLAVE_NPU_PROC_BWMON_CFG] = &qhs_npu_dsp_throttle_cfg,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QM_CFG] = &qhs_qm_cfg,
	[SLAVE_QM_MPU_CFG] = &qhs_qm_mpu_cfg,
	[SLAVE_QSPI_0] = &qhs_qspi,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SECURITY] = &qhs_security,
	[SLAVE_SNOC_CFG] = &qhs_snoc_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM_WEST] = &qhs_tlmm_1,
	[SLAVE_TLMM_NORTH] = &qhs_tlmm_2,
	[SLAVE_TLMM_SOUTH] = &qhs_tlmm_3,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB3] = &qhs_usb3,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VENUS_THROTTLE_CFG] = &qhs_venus_throttle_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_SERVICE_CNOC] = &srvc_cnoc,
};

static const struct qcom_icc_desc sc7180_config_noc = {
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

static const struct qcom_icc_desc sc7180_dc_noc = {
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
	[MASTER_APPSS_PROC] = &acm_apps0,
	[MASTER_SYS_TCU] = &acm_sys_tcu,
	[MASTER_GEM_NOC_CFG] = &qhm_gemnoc_cfg,
	[MASTER_COMPUTE_NOC] = &qnm_cmpnoc,
	[MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[MASTER_MNOC_SF_MEM_NOC] = &qnm_mnoc_sf,
	[MASTER_SNOC_GC_MEM_NOC] = &qnm_snoc_gc,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[MASTER_GFX3D] = &qxm_gpu,
	[SLAVE_MSS_PROC_MS_MPU_CFG] = &qhs_mdsp_ms_mpu_cfg,
	[SLAVE_GEM_NOC_SNOC] = &qns_gem_noc_snoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_SERVICE_GEM_NOC] = &srvc_gemnoc,
};

static const struct qcom_icc_desc sc7180_gem_noc = {
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
	[SLAVE_EBI1] = &ebi,
};

static const struct qcom_icc_desc sc7180_mc_virt = {
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm * const mmss_noc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
	&bcm_mm2,
};

static struct qcom_icc_node * const mmss_noc_nodes[] = {
	[MASTER_CNOC_MNOC_CFG] = &qhm_mnoc_cfg,
	[MASTER_CAMNOC_HF0] = &qxm_camnoc_hf0,
	[MASTER_CAMNOC_HF1] = &qxm_camnoc_hf1,
	[MASTER_CAMNOC_SF] = &qxm_camnoc_sf,
	[MASTER_MDP0] = &qxm_mdp0,
	[MASTER_ROTATOR] = &qxm_rot,
	[MASTER_VIDEO_P0] = &qxm_venus0,
	[MASTER_VIDEO_PROC] = &qxm_venus_arm9,
	[SLAVE_MNOC_HF_MEM_NOC] = &qns_mem_noc_hf,
	[SLAVE_MNOC_SF_MEM_NOC] = &qns_mem_noc_sf,
	[SLAVE_SERVICE_MNOC] = &srvc_mnoc,
};

static const struct qcom_icc_desc sc7180_mmss_noc = {
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

static const struct qcom_icc_desc sc7180_npu_noc = {
	.nodes = npu_noc_nodes,
	.num_nodes = ARRAY_SIZE(npu_noc_nodes),
};

static struct qcom_icc_bcm * const qup_virt_bcms[] = {
	&bcm_qup0,
};

static struct qcom_icc_node * const qup_virt_nodes[] = {
	[MASTER_QUP_CORE_0] = &qup_core_master_1,
	[MASTER_QUP_CORE_1] = &qup_core_master_2,
	[SLAVE_QUP_CORE_0] = &qup_core_slave_1,
	[SLAVE_QUP_CORE_1] = &qup_core_slave_2,
};

static const struct qcom_icc_desc sc7180_qup_virt = {
	.nodes = qup_virt_nodes,
	.num_nodes = ARRAY_SIZE(qup_virt_nodes),
	.bcms = qup_virt_bcms,
	.num_bcms = ARRAY_SIZE(qup_virt_bcms),
};

static struct qcom_icc_bcm * const system_noc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn2,
	&bcm_sn3,
	&bcm_sn4,
	&bcm_sn7,
	&bcm_sn9,
	&bcm_sn12,
};

static struct qcom_icc_node * const system_noc_nodes[] = {
	[MASTER_SNOC_CFG] = &qhm_snoc_cfg,
	[MASTER_A1NOC_SNOC] = &qnm_aggre1_noc,
	[MASTER_A2NOC_SNOC] = &qnm_aggre2_noc,
	[MASTER_GEM_NOC_SNOC] = &qnm_gemnoc,
	[MASTER_PIMEM] = &qxm_pimem,
	[SLAVE_APPSS] = &qhs_apss,
	[SLAVE_SNOC_CNOC] = &qns_cnoc,
	[SLAVE_SNOC_GEM_NOC_GC] = &qns_gemnoc_gc,
	[SLAVE_SNOC_GEM_NOC_SF] = &qns_gemnoc_sf,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sc7180_system_noc = {
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sc7180-aggre1-noc",
	  .data = &sc7180_aggre1_noc},
	{ .compatible = "qcom,sc7180-aggre2-noc",
	  .data = &sc7180_aggre2_noc},
	{ .compatible = "qcom,sc7180-camnoc-virt",
	  .data = &sc7180_camnoc_virt},
	{ .compatible = "qcom,sc7180-compute-noc",
	  .data = &sc7180_compute_noc},
	{ .compatible = "qcom,sc7180-config-noc",
	  .data = &sc7180_config_noc},
	{ .compatible = "qcom,sc7180-dc-noc",
	  .data = &sc7180_dc_noc},
	{ .compatible = "qcom,sc7180-gem-noc",
	  .data = &sc7180_gem_noc},
	{ .compatible = "qcom,sc7180-mc-virt",
	  .data = &sc7180_mc_virt},
	{ .compatible = "qcom,sc7180-mmss-noc",
	  .data = &sc7180_mmss_noc},
	{ .compatible = "qcom,sc7180-npu-noc",
	  .data = &sc7180_npu_noc},
	{ .compatible = "qcom,sc7180-qup-virt",
	  .data = &sc7180_qup_virt},
	{ .compatible = "qcom,sc7180-system-noc",
	  .data = &sc7180_system_noc},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-sc7180",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(qnoc_driver);

MODULE_DESCRIPTION("Qualcomm SC7180 NoC driver");
MODULE_LICENSE("GPL v2");
