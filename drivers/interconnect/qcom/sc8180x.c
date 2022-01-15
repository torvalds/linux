// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021, Linaro Ltd.
 */

#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/of_device.h>

#include <dt-bindings/interconnect/qcom,sc8180x.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"
#include "sc8180x.h"

DEFINE_QNODE(mas_qhm_a1noc_cfg, SC8180X_MASTER_A1NOC_CFG, 1, 4, SC8180X_SLAVE_SERVICE_A1NOC);
DEFINE_QNODE(mas_xm_ufs_card, SC8180X_MASTER_UFS_CARD, 1, 8, SC8180X_A1NOC_SNOC_SLV);
DEFINE_QNODE(mas_xm_ufs_g4, SC8180X_MASTER_UFS_GEN4, 1, 8, SC8180X_A1NOC_SNOC_SLV);
DEFINE_QNODE(mas_xm_ufs_mem, SC8180X_MASTER_UFS_MEM, 1, 8, SC8180X_A1NOC_SNOC_SLV);
DEFINE_QNODE(mas_xm_usb3_0, SC8180X_MASTER_USB3, 1, 8, SC8180X_A1NOC_SNOC_SLV);
DEFINE_QNODE(mas_xm_usb3_1, SC8180X_MASTER_USB3_1, 1, 8, SC8180X_A1NOC_SNOC_SLV);
DEFINE_QNODE(mas_xm_usb3_2, SC8180X_MASTER_USB3_2, 1, 16, SC8180X_A1NOC_SNOC_SLV);
DEFINE_QNODE(mas_qhm_a2noc_cfg, SC8180X_MASTER_A2NOC_CFG, 1, 4, SC8180X_SLAVE_SERVICE_A2NOC);
DEFINE_QNODE(mas_qhm_qdss_bam, SC8180X_MASTER_QDSS_BAM, 1, 4, SC8180X_A2NOC_SNOC_SLV);
DEFINE_QNODE(mas_qhm_qspi, SC8180X_MASTER_QSPI_0, 1, 4, SC8180X_A2NOC_SNOC_SLV);
DEFINE_QNODE(mas_qhm_qspi1, SC8180X_MASTER_QSPI_1, 1, 4, SC8180X_A2NOC_SNOC_SLV);
DEFINE_QNODE(mas_qhm_qup0, SC8180X_MASTER_QUP_0, 1, 4, SC8180X_A2NOC_SNOC_SLV);
DEFINE_QNODE(mas_qhm_qup1, SC8180X_MASTER_QUP_1, 1, 4, SC8180X_A2NOC_SNOC_SLV);
DEFINE_QNODE(mas_qhm_qup2, SC8180X_MASTER_QUP_2, 1, 4, SC8180X_A2NOC_SNOC_SLV);
DEFINE_QNODE(mas_qhm_sensorss_ahb, SC8180X_MASTER_SENSORS_AHB, 1, 4, SC8180X_A2NOC_SNOC_SLV);
DEFINE_QNODE(mas_qxm_crypto, SC8180X_MASTER_CRYPTO_CORE_0, 1, 8, SC8180X_A2NOC_SNOC_SLV);
DEFINE_QNODE(mas_qxm_ipa, SC8180X_MASTER_IPA, 1, 8, SC8180X_A2NOC_SNOC_SLV);
DEFINE_QNODE(mas_xm_emac, SC8180X_MASTER_EMAC, 1, 8, SC8180X_A2NOC_SNOC_SLV);
DEFINE_QNODE(mas_xm_pcie3_0, SC8180X_MASTER_PCIE, 1, 8, SC8180X_SLAVE_ANOC_PCIE_GEM_NOC);
DEFINE_QNODE(mas_xm_pcie3_1, SC8180X_MASTER_PCIE_1, 1, 16, SC8180X_SLAVE_ANOC_PCIE_GEM_NOC);
DEFINE_QNODE(mas_xm_pcie3_2, SC8180X_MASTER_PCIE_2, 1, 8, SC8180X_SLAVE_ANOC_PCIE_GEM_NOC);
DEFINE_QNODE(mas_xm_pcie3_3, SC8180X_MASTER_PCIE_3, 1, 16, SC8180X_SLAVE_ANOC_PCIE_GEM_NOC);
DEFINE_QNODE(mas_xm_qdss_etr, SC8180X_MASTER_QDSS_ETR, 1, 8, SC8180X_A2NOC_SNOC_SLV);
DEFINE_QNODE(mas_xm_sdc2, SC8180X_MASTER_SDCC_2, 1, 8, SC8180X_A2NOC_SNOC_SLV);
DEFINE_QNODE(mas_xm_sdc4, SC8180X_MASTER_SDCC_4, 1, 8, SC8180X_A2NOC_SNOC_SLV);
DEFINE_QNODE(mas_qxm_camnoc_hf0_uncomp, SC8180X_MASTER_CAMNOC_HF0_UNCOMP, 1, 32, SC8180X_SLAVE_CAMNOC_UNCOMP);
DEFINE_QNODE(mas_qxm_camnoc_hf1_uncomp, SC8180X_MASTER_CAMNOC_HF1_UNCOMP, 1, 32, SC8180X_SLAVE_CAMNOC_UNCOMP);
DEFINE_QNODE(mas_qxm_camnoc_sf_uncomp, SC8180X_MASTER_CAMNOC_SF_UNCOMP, 1, 32, SC8180X_SLAVE_CAMNOC_UNCOMP);
DEFINE_QNODE(mas_qnm_npu, SC8180X_MASTER_NPU, 1, 32, SC8180X_SLAVE_CDSP_MEM_NOC);
DEFINE_QNODE(mas_qnm_snoc, SC8180X_SNOC_CNOC_MAS, 1, 8, SC8180X_SLAVE_TLMM_SOUTH, SC8180X_SLAVE_CDSP_CFG, SC8180X_SLAVE_SPSS_CFG, SC8180X_SLAVE_CAMERA_CFG, SC8180X_SLAVE_SDCC_4, SC8180X_SLAVE_AHB2PHY_CENTER, SC8180X_SLAVE_SDCC_2, SC8180X_SLAVE_PCIE_2_CFG, SC8180X_SLAVE_CNOC_MNOC_CFG, SC8180X_SLAVE_EMAC_CFG, SC8180X_SLAVE_QSPI_0, SC8180X_SLAVE_QSPI_1, SC8180X_SLAVE_TLMM_EAST, SC8180X_SLAVE_SNOC_CFG, SC8180X_SLAVE_AHB2PHY_EAST, SC8180X_SLAVE_GLM, SC8180X_SLAVE_PDM, SC8180X_SLAVE_PCIE_1_CFG, SC8180X_SLAVE_A2NOC_CFG, SC8180X_SLAVE_QDSS_CFG, SC8180X_SLAVE_DISPLAY_CFG, SC8180X_SLAVE_TCSR, SC8180X_SLAVE_UFS_MEM_0_CFG, SC8180X_SLAVE_CNOC_DDRSS, SC8180X_SLAVE_PCIE_0_CFG, SC8180X_SLAVE_QUP_1, SC8180X_SLAVE_QUP_2, SC8180X_SLAVE_NPU_CFG, SC8180X_SLAVE_CRYPTO_0_CFG, SC8180X_SLAVE_GRAPHICS_3D_CFG, SC8180X_SLAVE_VENUS_CFG, SC8180X_SLAVE_TSIF, SC8180X_SLAVE_IPA_CFG, SC8180X_SLAVE_CLK_CTL, SC8180X_SLAVE_SECURITY, SC8180X_SLAVE_AOP, SC8180X_SLAVE_AHB2PHY_WEST, SC8180X_SLAVE_AHB2PHY_SOUTH, SC8180X_SLAVE_SERVICE_CNOC, SC8180X_SLAVE_UFS_CARD_CFG, SC8180X_SLAVE_USB3_1, SC8180X_SLAVE_USB3_2, SC8180X_SLAVE_PCIE_3_CFG, SC8180X_SLAVE_RBCPR_CX_CFG, SC8180X_SLAVE_TLMM_WEST, SC8180X_SLAVE_A1NOC_CFG, SC8180X_SLAVE_AOSS, SC8180X_SLAVE_PRNG, SC8180X_SLAVE_VSENSE_CTRL_CFG, SC8180X_SLAVE_QUP_0, SC8180X_SLAVE_USB3, SC8180X_SLAVE_RBCPR_MMCX_CFG, SC8180X_SLAVE_PIMEM_CFG, SC8180X_SLAVE_UFS_MEM_1_CFG, SC8180X_SLAVE_RBCPR_MX_CFG, SC8180X_SLAVE_IMEM_CFG);
DEFINE_QNODE(mas_qhm_cnoc_dc_noc, SC8180X_MASTER_CNOC_DC_NOC, 1, 4, SC8180X_SLAVE_LLCC_CFG, SC8180X_SLAVE_GEM_NOC_CFG);
DEFINE_QNODE(mas_acm_apps, SC8180X_MASTER_AMPSS_M0, 4, 64, SC8180X_SLAVE_ECC, SC8180X_SLAVE_LLCC, SC8180X_SLAVE_GEM_NOC_SNOC);
DEFINE_QNODE(mas_acm_gpu_tcu, SC8180X_MASTER_GPU_TCU, 1, 8, SC8180X_SLAVE_LLCC, SC8180X_SLAVE_GEM_NOC_SNOC);
DEFINE_QNODE(mas_acm_sys_tcu, SC8180X_MASTER_SYS_TCU, 1, 8, SC8180X_SLAVE_LLCC, SC8180X_SLAVE_GEM_NOC_SNOC);
DEFINE_QNODE(mas_qhm_gemnoc_cfg, SC8180X_MASTER_GEM_NOC_CFG, 1, 4, SC8180X_SLAVE_SERVICE_GEM_NOC_1, SC8180X_SLAVE_SERVICE_GEM_NOC, SC8180X_SLAVE_MSS_PROC_MS_MPU_CFG);
DEFINE_QNODE(mas_qnm_cmpnoc, SC8180X_MASTER_COMPUTE_NOC, 2, 32, SC8180X_SLAVE_ECC, SC8180X_SLAVE_LLCC, SC8180X_SLAVE_GEM_NOC_SNOC);
DEFINE_QNODE(mas_qnm_gpu, SC8180X_MASTER_GRAPHICS_3D, 4, 32, SC8180X_SLAVE_LLCC, SC8180X_SLAVE_GEM_NOC_SNOC);
DEFINE_QNODE(mas_qnm_mnoc_hf, SC8180X_MASTER_MNOC_HF_MEM_NOC, 2, 32, SC8180X_SLAVE_LLCC);
DEFINE_QNODE(mas_qnm_mnoc_sf, SC8180X_MASTER_MNOC_SF_MEM_NOC, 1, 32, SC8180X_SLAVE_LLCC, SC8180X_SLAVE_GEM_NOC_SNOC);
DEFINE_QNODE(mas_qnm_pcie, SC8180X_MASTER_GEM_NOC_PCIE_SNOC, 1, 32, SC8180X_SLAVE_LLCC, SC8180X_SLAVE_GEM_NOC_SNOC);
DEFINE_QNODE(mas_qnm_snoc_gc, SC8180X_MASTER_SNOC_GC_MEM_NOC, 1, 8, SC8180X_SLAVE_LLCC);
DEFINE_QNODE(mas_qnm_snoc_sf, SC8180X_MASTER_SNOC_SF_MEM_NOC, 1, 32, SC8180X_SLAVE_LLCC);
DEFINE_QNODE(mas_qxm_ecc, SC8180X_MASTER_ECC, 2, 32, SC8180X_SLAVE_LLCC);
DEFINE_QNODE(mas_ipa_core_master, SC8180X_MASTER_IPA_CORE, 1, 8, SC8180X_SLAVE_IPA_CORE);
DEFINE_QNODE(mas_llcc_mc, SC8180X_MASTER_LLCC, 8, 4, SC8180X_SLAVE_EBI_CH0);
DEFINE_QNODE(mas_qhm_mnoc_cfg, SC8180X_MASTER_CNOC_MNOC_CFG, 1, 4, SC8180X_SLAVE_SERVICE_MNOC);
DEFINE_QNODE(mas_qxm_camnoc_hf0, SC8180X_MASTER_CAMNOC_HF0, 1, 32, SC8180X_SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(mas_qxm_camnoc_hf1, SC8180X_MASTER_CAMNOC_HF1, 1, 32, SC8180X_SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(mas_qxm_camnoc_sf, SC8180X_MASTER_CAMNOC_SF, 1, 32, SC8180X_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(mas_qxm_mdp0, SC8180X_MASTER_MDP_PORT0, 1, 32, SC8180X_SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(mas_qxm_mdp1, SC8180X_MASTER_MDP_PORT1, 1, 32, SC8180X_SLAVE_MNOC_HF_MEM_NOC);
DEFINE_QNODE(mas_qxm_rot, SC8180X_MASTER_ROTATOR, 1, 32, SC8180X_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(mas_qxm_venus0, SC8180X_MASTER_VIDEO_P0, 1, 32, SC8180X_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(mas_qxm_venus1, SC8180X_MASTER_VIDEO_P1, 1, 32, SC8180X_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(mas_qxm_venus_arm9, SC8180X_MASTER_VIDEO_PROC, 1, 8, SC8180X_SLAVE_MNOC_SF_MEM_NOC);
DEFINE_QNODE(mas_qhm_snoc_cfg, SC8180X_MASTER_SNOC_CFG, 1, 4, SC8180X_SLAVE_SERVICE_SNOC);
DEFINE_QNODE(mas_qnm_aggre1_noc, SC8180X_A1NOC_SNOC_MAS, 1, 32, SC8180X_SLAVE_SNOC_GEM_NOC_SF, SC8180X_SLAVE_PIMEM, SC8180X_SLAVE_OCIMEM, SC8180X_SLAVE_APPSS, SC8180X_SNOC_CNOC_SLV, SC8180X_SLAVE_QDSS_STM);
DEFINE_QNODE(mas_qnm_aggre2_noc, SC8180X_A2NOC_SNOC_MAS, 1, 16, SC8180X_SLAVE_SNOC_GEM_NOC_SF, SC8180X_SLAVE_PIMEM, SC8180X_SLAVE_PCIE_3, SC8180X_SLAVE_OCIMEM, SC8180X_SLAVE_APPSS, SC8180X_SLAVE_PCIE_2, SC8180X_SNOC_CNOC_SLV, SC8180X_SLAVE_PCIE_0, SC8180X_SLAVE_PCIE_1, SC8180X_SLAVE_TCU, SC8180X_SLAVE_QDSS_STM);
DEFINE_QNODE(mas_qnm_gemnoc, SC8180X_MASTER_GEM_NOC_SNOC, 1, 8, SC8180X_SLAVE_PIMEM, SC8180X_SLAVE_OCIMEM, SC8180X_SLAVE_APPSS, SC8180X_SNOC_CNOC_SLV, SC8180X_SLAVE_TCU, SC8180X_SLAVE_QDSS_STM);
DEFINE_QNODE(mas_qxm_pimem, SC8180X_MASTER_PIMEM, 1, 8, SC8180X_SLAVE_SNOC_GEM_NOC_GC, SC8180X_SLAVE_OCIMEM);
DEFINE_QNODE(mas_xm_gic, SC8180X_MASTER_GIC, 1, 8, SC8180X_SLAVE_SNOC_GEM_NOC_GC, SC8180X_SLAVE_OCIMEM);
DEFINE_QNODE(slv_qns_a1noc_snoc, SC8180X_A1NOC_SNOC_SLV, 1, 32, SC8180X_A1NOC_SNOC_MAS);
DEFINE_QNODE(slv_srvc_aggre1_noc, SC8180X_SLAVE_SERVICE_A1NOC, 1, 4);
DEFINE_QNODE(slv_qns_a2noc_snoc, SC8180X_A2NOC_SNOC_SLV, 1, 16, SC8180X_A2NOC_SNOC_MAS);
DEFINE_QNODE(slv_qns_pcie_mem_noc, SC8180X_SLAVE_ANOC_PCIE_GEM_NOC, 1, 32, SC8180X_MASTER_GEM_NOC_PCIE_SNOC);
DEFINE_QNODE(slv_srvc_aggre2_noc, SC8180X_SLAVE_SERVICE_A2NOC, 1, 4);
DEFINE_QNODE(slv_qns_camnoc_uncomp, SC8180X_SLAVE_CAMNOC_UNCOMP, 1, 32);
DEFINE_QNODE(slv_qns_cdsp_mem_noc, SC8180X_SLAVE_CDSP_MEM_NOC, 2, 32, SC8180X_MASTER_COMPUTE_NOC);
DEFINE_QNODE(slv_qhs_a1_noc_cfg, SC8180X_SLAVE_A1NOC_CFG, 1, 4, SC8180X_MASTER_A1NOC_CFG);
DEFINE_QNODE(slv_qhs_a2_noc_cfg, SC8180X_SLAVE_A2NOC_CFG, 1, 4, SC8180X_MASTER_A2NOC_CFG);
DEFINE_QNODE(slv_qhs_ahb2phy_refgen_center, SC8180X_SLAVE_AHB2PHY_CENTER, 1, 4);
DEFINE_QNODE(slv_qhs_ahb2phy_refgen_east, SC8180X_SLAVE_AHB2PHY_EAST, 1, 4);
DEFINE_QNODE(slv_qhs_ahb2phy_refgen_west, SC8180X_SLAVE_AHB2PHY_WEST, 1, 4);
DEFINE_QNODE(slv_qhs_ahb2phy_south, SC8180X_SLAVE_AHB2PHY_SOUTH, 1, 4);
DEFINE_QNODE(slv_qhs_aop, SC8180X_SLAVE_AOP, 1, 4);
DEFINE_QNODE(slv_qhs_aoss, SC8180X_SLAVE_AOSS, 1, 4);
DEFINE_QNODE(slv_qhs_camera_cfg, SC8180X_SLAVE_CAMERA_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_clk_ctl, SC8180X_SLAVE_CLK_CTL, 1, 4);
DEFINE_QNODE(slv_qhs_compute_dsp, SC8180X_SLAVE_CDSP_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_cpr_cx, SC8180X_SLAVE_RBCPR_CX_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_cpr_mmcx, SC8180X_SLAVE_RBCPR_MMCX_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_cpr_mx, SC8180X_SLAVE_RBCPR_MX_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_crypto0_cfg, SC8180X_SLAVE_CRYPTO_0_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_ddrss_cfg, SC8180X_SLAVE_CNOC_DDRSS, 1, 4, SC8180X_MASTER_CNOC_DC_NOC);
DEFINE_QNODE(slv_qhs_display_cfg, SC8180X_SLAVE_DISPLAY_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_emac_cfg, SC8180X_SLAVE_EMAC_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_glm, SC8180X_SLAVE_GLM, 1, 4);
DEFINE_QNODE(slv_qhs_gpuss_cfg, SC8180X_SLAVE_GRAPHICS_3D_CFG, 1, 8);
DEFINE_QNODE(slv_qhs_imem_cfg, SC8180X_SLAVE_IMEM_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_ipa, SC8180X_SLAVE_IPA_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_mnoc_cfg, SC8180X_SLAVE_CNOC_MNOC_CFG, 1, 4, SC8180X_MASTER_CNOC_MNOC_CFG);
DEFINE_QNODE(slv_qhs_npu_cfg, SC8180X_SLAVE_NPU_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_pcie0_cfg, SC8180X_SLAVE_PCIE_0_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_pcie1_cfg, SC8180X_SLAVE_PCIE_1_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_pcie2_cfg, SC8180X_SLAVE_PCIE_2_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_pcie3_cfg, SC8180X_SLAVE_PCIE_3_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_pdm, SC8180X_SLAVE_PDM, 1, 4);
DEFINE_QNODE(slv_qhs_pimem_cfg, SC8180X_SLAVE_PIMEM_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_prng, SC8180X_SLAVE_PRNG, 1, 4);
DEFINE_QNODE(slv_qhs_qdss_cfg, SC8180X_SLAVE_QDSS_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_qspi_0, SC8180X_SLAVE_QSPI_0, 1, 4);
DEFINE_QNODE(slv_qhs_qspi_1, SC8180X_SLAVE_QSPI_1, 1, 4);
DEFINE_QNODE(slv_qhs_qupv3_east0, SC8180X_SLAVE_QUP_1, 1, 4);
DEFINE_QNODE(slv_qhs_qupv3_east1, SC8180X_SLAVE_QUP_2, 1, 4);
DEFINE_QNODE(slv_qhs_qupv3_west, SC8180X_SLAVE_QUP_0, 1, 4);
DEFINE_QNODE(slv_qhs_sdc2, SC8180X_SLAVE_SDCC_2, 1, 4);
DEFINE_QNODE(slv_qhs_sdc4, SC8180X_SLAVE_SDCC_4, 1, 4);
DEFINE_QNODE(slv_qhs_security, SC8180X_SLAVE_SECURITY, 1, 4);
DEFINE_QNODE(slv_qhs_snoc_cfg, SC8180X_SLAVE_SNOC_CFG, 1, 4, SC8180X_MASTER_SNOC_CFG);
DEFINE_QNODE(slv_qhs_spss_cfg, SC8180X_SLAVE_SPSS_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_tcsr, SC8180X_SLAVE_TCSR, 1, 4);
DEFINE_QNODE(slv_qhs_tlmm_east, SC8180X_SLAVE_TLMM_EAST, 1, 4);
DEFINE_QNODE(slv_qhs_tlmm_south, SC8180X_SLAVE_TLMM_SOUTH, 1, 4);
DEFINE_QNODE(slv_qhs_tlmm_west, SC8180X_SLAVE_TLMM_WEST, 1, 4);
DEFINE_QNODE(slv_qhs_tsif, SC8180X_SLAVE_TSIF, 1, 4);
DEFINE_QNODE(slv_qhs_ufs_card_cfg, SC8180X_SLAVE_UFS_CARD_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_ufs_mem0_cfg, SC8180X_SLAVE_UFS_MEM_0_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_ufs_mem1_cfg, SC8180X_SLAVE_UFS_MEM_1_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_usb3_0, SC8180X_SLAVE_USB3, 1, 4);
DEFINE_QNODE(slv_qhs_usb3_1, SC8180X_SLAVE_USB3_1, 1, 4);
DEFINE_QNODE(slv_qhs_usb3_2, SC8180X_SLAVE_USB3_2, 1, 4);
DEFINE_QNODE(slv_qhs_venus_cfg, SC8180X_SLAVE_VENUS_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_vsense_ctrl_cfg, SC8180X_SLAVE_VSENSE_CTRL_CFG, 1, 4);
DEFINE_QNODE(slv_srvc_cnoc, SC8180X_SLAVE_SERVICE_CNOC, 1, 4);
DEFINE_QNODE(slv_qhs_gemnoc, SC8180X_SLAVE_GEM_NOC_CFG, 1, 4, SC8180X_MASTER_GEM_NOC_CFG);
DEFINE_QNODE(slv_qhs_llcc, SC8180X_SLAVE_LLCC_CFG, 1, 4);
DEFINE_QNODE(slv_qhs_mdsp_ms_mpu_cfg, SC8180X_SLAVE_MSS_PROC_MS_MPU_CFG, 1, 4);
DEFINE_QNODE(slv_qns_ecc, SC8180X_SLAVE_ECC, 1, 32);
DEFINE_QNODE(slv_qns_gem_noc_snoc, SC8180X_SLAVE_GEM_NOC_SNOC, 1, 8, SC8180X_MASTER_GEM_NOC_SNOC);
DEFINE_QNODE(slv_qns_llcc, SC8180X_SLAVE_LLCC, 8, 16, SC8180X_MASTER_LLCC);
DEFINE_QNODE(slv_srvc_gemnoc, SC8180X_SLAVE_SERVICE_GEM_NOC, 1, 4);
DEFINE_QNODE(slv_srvc_gemnoc1, SC8180X_SLAVE_SERVICE_GEM_NOC_1, 1, 4);
DEFINE_QNODE(slv_ipa_core_slave, SC8180X_SLAVE_IPA_CORE, 1, 8);
DEFINE_QNODE(slv_ebi, SC8180X_SLAVE_EBI_CH0, 8, 4);
DEFINE_QNODE(slv_qns2_mem_noc, SC8180X_SLAVE_MNOC_SF_MEM_NOC, 1, 32, SC8180X_MASTER_MNOC_SF_MEM_NOC);
DEFINE_QNODE(slv_qns_mem_noc_hf, SC8180X_SLAVE_MNOC_HF_MEM_NOC, 2, 32, SC8180X_MASTER_MNOC_HF_MEM_NOC);
DEFINE_QNODE(slv_srvc_mnoc, SC8180X_SLAVE_SERVICE_MNOC, 1, 4);
DEFINE_QNODE(slv_qhs_apss, SC8180X_SLAVE_APPSS, 1, 8);
DEFINE_QNODE(slv_qns_cnoc, SC8180X_SNOC_CNOC_SLV, 1, 8, SC8180X_SNOC_CNOC_MAS);
DEFINE_QNODE(slv_qns_gemnoc_gc, SC8180X_SLAVE_SNOC_GEM_NOC_GC, 1, 8, SC8180X_MASTER_SNOC_GC_MEM_NOC);
DEFINE_QNODE(slv_qns_gemnoc_sf, SC8180X_SLAVE_SNOC_GEM_NOC_SF, 1, 32, SC8180X_MASTER_SNOC_SF_MEM_NOC);
DEFINE_QNODE(slv_qxs_imem, SC8180X_SLAVE_OCIMEM, 1, 8);
DEFINE_QNODE(slv_qxs_pimem, SC8180X_SLAVE_PIMEM, 1, 8);
DEFINE_QNODE(slv_srvc_snoc, SC8180X_SLAVE_SERVICE_SNOC, 1, 4);
DEFINE_QNODE(slv_xs_pcie_0, SC8180X_SLAVE_PCIE_0, 1, 8);
DEFINE_QNODE(slv_xs_pcie_1, SC8180X_SLAVE_PCIE_1, 1, 8);
DEFINE_QNODE(slv_xs_pcie_2, SC8180X_SLAVE_PCIE_2, 1, 8);
DEFINE_QNODE(slv_xs_pcie_3, SC8180X_SLAVE_PCIE_3, 1, 8);
DEFINE_QNODE(slv_xs_qdss_stm, SC8180X_SLAVE_QDSS_STM, 1, 4);
DEFINE_QNODE(slv_xs_sys_tcu_cfg, SC8180X_SLAVE_TCU, 1, 8);

DEFINE_QBCM(bcm_acv, "ACV", false, &slv_ebi);
DEFINE_QBCM(bcm_mc0, "MC0", false, &slv_ebi);
DEFINE_QBCM(bcm_sh0, "SH0", false, &slv_qns_llcc);
DEFINE_QBCM(bcm_mm0, "MM0", false, &slv_qns_mem_noc_hf);
DEFINE_QBCM(bcm_co0, "CO0", false, &slv_qns_cdsp_mem_noc);
DEFINE_QBCM(bcm_ce0, "CE0", false, &mas_qxm_crypto);
DEFINE_QBCM(bcm_cn0, "CN0", false, &mas_qnm_snoc, &slv_qhs_a1_noc_cfg, &slv_qhs_a2_noc_cfg, &slv_qhs_ahb2phy_refgen_center, &slv_qhs_ahb2phy_refgen_east, &slv_qhs_ahb2phy_refgen_west, &slv_qhs_ahb2phy_south, &slv_qhs_aop, &slv_qhs_aoss, &slv_qhs_camera_cfg, &slv_qhs_clk_ctl, &slv_qhs_compute_dsp, &slv_qhs_cpr_cx, &slv_qhs_cpr_mmcx, &slv_qhs_cpr_mx, &slv_qhs_crypto0_cfg, &slv_qhs_ddrss_cfg, &slv_qhs_display_cfg, &slv_qhs_emac_cfg, &slv_qhs_glm, &slv_qhs_gpuss_cfg, &slv_qhs_imem_cfg, &slv_qhs_ipa, &slv_qhs_mnoc_cfg, &slv_qhs_npu_cfg, &slv_qhs_pcie0_cfg, &slv_qhs_pcie1_cfg, &slv_qhs_pcie2_cfg, &slv_qhs_pcie3_cfg, &slv_qhs_pdm, &slv_qhs_pimem_cfg, &slv_qhs_prng, &slv_qhs_qdss_cfg, &slv_qhs_qspi_0, &slv_qhs_qspi_1, &slv_qhs_qupv3_east0, &slv_qhs_qupv3_east1, &slv_qhs_qupv3_west, &slv_qhs_sdc2, &slv_qhs_sdc4, &slv_qhs_security, &slv_qhs_snoc_cfg, &slv_qhs_spss_cfg, &slv_qhs_tcsr, &slv_qhs_tlmm_east, &slv_qhs_tlmm_south, &slv_qhs_tlmm_west, &slv_qhs_tsif, &slv_qhs_ufs_card_cfg, &slv_qhs_ufs_mem0_cfg, &slv_qhs_ufs_mem1_cfg, &slv_qhs_usb3_0, &slv_qhs_usb3_1, &slv_qhs_usb3_2, &slv_qhs_venus_cfg, &slv_qhs_vsense_ctrl_cfg, &slv_srvc_cnoc);
DEFINE_QBCM(bcm_mm1, "MM1", false, &mas_qxm_camnoc_hf0_uncomp, &mas_qxm_camnoc_hf1_uncomp, &mas_qxm_camnoc_sf_uncomp, &mas_qxm_camnoc_hf0, &mas_qxm_camnoc_hf1, &mas_qxm_mdp0, &mas_qxm_mdp1);
DEFINE_QBCM(bcm_qup0, "QUP0", false, &mas_qhm_qup0, &mas_qhm_qup1, &mas_qhm_qup2);
DEFINE_QBCM(bcm_sh2, "SH2", false, &slv_qns_gem_noc_snoc);
DEFINE_QBCM(bcm_mm2, "MM2", false, &mas_qxm_camnoc_sf, &mas_qxm_rot, &mas_qxm_venus0, &mas_qxm_venus1, &mas_qxm_venus_arm9, &slv_qns2_mem_noc);
DEFINE_QBCM(bcm_sh3, "SH3", false, &mas_acm_apps);
DEFINE_QBCM(bcm_sn0, "SN0", false, &slv_qns_gemnoc_sf);
DEFINE_QBCM(bcm_sn1, "SN1", false, &slv_qxs_imem);
DEFINE_QBCM(bcm_sn2, "SN2", false, &slv_qns_gemnoc_gc);
DEFINE_QBCM(bcm_co2, "CO2", false, &mas_qnm_npu);
DEFINE_QBCM(bcm_ip0, "IP0", false, &slv_ipa_core_slave);
DEFINE_QBCM(bcm_sn3, "SN3", false, &slv_srvc_aggre1_noc, &slv_qns_cnoc);
DEFINE_QBCM(bcm_sn4, "SN4", false, &slv_qxs_pimem);
DEFINE_QBCM(bcm_sn8, "SN8", false, &slv_xs_pcie_0, &slv_xs_pcie_1, &slv_xs_pcie_2, &slv_xs_pcie_3);
DEFINE_QBCM(bcm_sn9, "SN9", false, &mas_qnm_aggre1_noc);
DEFINE_QBCM(bcm_sn11, "SN11", false, &mas_qnm_aggre2_noc);
DEFINE_QBCM(bcm_sn14, "SN14", false, &slv_qns_pcie_mem_noc);
DEFINE_QBCM(bcm_sn15, "SN15", false, &mas_qnm_gemnoc);

static struct qcom_icc_bcm *aggre1_noc_bcms[] = {
	&bcm_sn3,
	&bcm_ce0,
	&bcm_qup0,
};

static struct qcom_icc_bcm *aggre2_noc_bcms[] = {
	&bcm_sn14,
	&bcm_ce0,
	&bcm_qup0,
};

static struct qcom_icc_bcm *camnoc_virt_bcms[] = {
	&bcm_mm1,
};

static struct qcom_icc_bcm *compute_noc_bcms[] = {
	&bcm_co0,
	&bcm_co2,
};

static struct qcom_icc_bcm *config_noc_bcms[] = {
	&bcm_cn0,
};

static struct qcom_icc_bcm *gem_noc_bcms[] = {
	&bcm_sh0,
	&bcm_sh2,
	&bcm_sh3,
};

static struct qcom_icc_bcm *ipa_virt_bcms[] = {
	&bcm_ip0,
};

static struct qcom_icc_bcm *mc_virt_bcms[] = {
	&bcm_mc0,
	&bcm_acv,
};

static struct qcom_icc_bcm *mmss_noc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
	&bcm_mm2,
};

static struct qcom_icc_bcm *system_noc_bcms[] = {
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

static struct qcom_icc_node *aggre1_noc_nodes[] = {
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

static struct qcom_icc_node *aggre2_noc_nodes[] = {
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

static struct qcom_icc_node *camnoc_virt_nodes[] = {
	[MASTER_CAMNOC_HF0_UNCOMP] = &mas_qxm_camnoc_hf0_uncomp,
	[MASTER_CAMNOC_HF1_UNCOMP] = &mas_qxm_camnoc_hf1_uncomp,
	[MASTER_CAMNOC_SF_UNCOMP] = &mas_qxm_camnoc_sf_uncomp,
	[SLAVE_CAMNOC_UNCOMP] = &slv_qns_camnoc_uncomp,
};

static struct qcom_icc_node *compute_noc_nodes[] = {
	[MASTER_NPU] = &mas_qnm_npu,
	[SLAVE_CDSP_MEM_NOC] = &slv_qns_cdsp_mem_noc,
};

static struct qcom_icc_node *config_noc_nodes[] = {
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

static struct qcom_icc_node *dc_noc_nodes[] = {
	[MASTER_CNOC_DC_NOC] = &mas_qhm_cnoc_dc_noc,
	[SLAVE_GEM_NOC_CFG] = &slv_qhs_gemnoc,
	[SLAVE_LLCC_CFG] = &slv_qhs_llcc,
};

static struct qcom_icc_node *gem_noc_nodes[] = {
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

static struct qcom_icc_node *ipa_virt_nodes[] = {
	[MASTER_IPA_CORE] = &mas_ipa_core_master,
	[SLAVE_IPA_CORE] = &slv_ipa_core_slave,
};

static struct qcom_icc_node *mc_virt_nodes[] = {
	[MASTER_LLCC] = &mas_llcc_mc,
	[SLAVE_EBI_CH0] = &slv_ebi,
};

static struct qcom_icc_node *mmss_noc_nodes[] = {
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

static struct qcom_icc_node *system_noc_nodes[] = {
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

static const struct qcom_icc_desc sc8180x_ipa_virt  = {
	.nodes = ipa_virt_nodes,
	.num_nodes = ARRAY_SIZE(ipa_virt_nodes),
	.bcms = ipa_virt_bcms,
	.num_bcms = ARRAY_SIZE(ipa_virt_bcms),
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
	{ .compatible = "qcom,sc8180x-aggre1-noc", .data = &sc8180x_aggre1_noc },
	{ .compatible = "qcom,sc8180x-aggre2-noc", .data = &sc8180x_aggre2_noc },
	{ .compatible = "qcom,sc8180x-camnoc-virt", .data = &sc8180x_camnoc_virt },
	{ .compatible = "qcom,sc8180x-compute-noc", .data = &sc8180x_compute_noc, },
	{ .compatible = "qcom,sc8180x-config-noc", .data = &sc8180x_config_noc },
	{ .compatible = "qcom,sc8180x-dc-noc", .data = &sc8180x_dc_noc },
	{ .compatible = "qcom,sc8180x-gem-noc", .data = &sc8180x_gem_noc },
	{ .compatible = "qcom,sc8180x-ipa-virt", .data = &sc8180x_ipa_virt },
	{ .compatible = "qcom,sc8180x-mc-virt", .data = &sc8180x_mc_virt },
	{ .compatible = "qcom,sc8180x-mmss-noc", .data = &sc8180x_mmss_noc },
	{ .compatible = "qcom,sc8180x-system-noc", .data = &sc8180x_system_noc },
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qnoc_probe,
	.remove = qnoc_remove,
	.driver = {
		.name = "qnoc-sc8180x",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(qnoc_driver);

MODULE_DESCRIPTION("Qualcomm sc8180x NoC driver");
MODULE_LICENSE("GPL v2");
