/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2020-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef ASIC_REG_GAUDI2_REGS_H_
#define ASIC_REG_GAUDI2_REGS_H_

#include "gaudi2_blocks_linux_driver.h"
#include "psoc_reset_conf_regs.h"
#include "psoc_global_conf_regs.h"
#include "cpu_if_regs.h"
#include "pcie_aux_regs.h"
#include "pcie_dbi_regs.h"
#include "pcie_wrap_regs.h"
#include "pmmu_hbw_stlb_regs.h"
#include "psoc_timestamp_regs.h"
#include "psoc_etr_regs.h"
#include "xbar_edge_0_regs.h"
#include "xbar_mid_0_regs.h"
#include "arc_farm_kdma_regs.h"
#include "arc_farm_kdma_ctx_regs.h"
#include "arc_farm_kdma_kdma_cgm_regs.h"
#include "arc_farm_arc0_aux_regs.h"
#include "arc_farm_arc0_acp_eng_regs.h"
#include "arc_farm_kdma_ctx_axuser_regs.h"
#include "arc_farm_arc0_dup_eng_axuser_regs.h"
#include "arc_farm_arc0_dup_eng_regs.h"
#include "dcore0_sync_mngr_objs_regs.h"
#include "dcore0_sync_mngr_glbl_regs.h"
#include "dcore0_sync_mngr_mstr_if_axuser_regs.h"
#include "dcore1_sync_mngr_glbl_regs.h"
#include "pdma0_qm_arc_aux_regs.h"
#include "pdma0_core_ctx_regs.h"
#include "pdma0_core_regs.h"
#include "pdma0_qm_axuser_secured_regs.h"
#include "pdma0_qm_regs.h"
#include "pdma0_qm_cgm_regs.h"
#include "pdma0_core_ctx_axuser_regs.h"
#include "pdma1_core_ctx_axuser_regs.h"
#include "pdma0_qm_axuser_nonsecured_regs.h"
#include "pdma1_qm_axuser_nonsecured_regs.h"
#include "dcore0_tpc0_qm_regs.h"
#include "dcore0_tpc0_qm_cgm_regs.h"
#include "dcore0_tpc0_qm_axuser_nonsecured_regs.h"
#include "dcore0_tpc0_qm_arc_aux_regs.h"
#include "dcore0_tpc0_cfg_regs.h"
#include "dcore0_tpc0_cfg_qm_regs.h"
#include "dcore0_tpc0_cfg_axuser_regs.h"
#include "dcore0_tpc0_cfg_qm_sync_object_regs.h"
#include "dcore0_tpc0_cfg_kernel_regs.h"
#include "dcore0_tpc0_cfg_kernel_tensor_0_regs.h"
#include "dcore0_tpc0_cfg_qm_tensor_0_regs.h"
#include "dcore0_tpc0_cfg_special_regs.h"
#include "dcore0_tpc0_eml_funnel_regs.h"
#include "dcore0_tpc0_eml_etf_regs.h"
#include "dcore0_tpc0_eml_stm_regs.h"
#include "dcore0_tpc0_eml_busmon_0_regs.h"
#include "dcore0_tpc0_eml_spmu_regs.h"
#include "pmmu_pif_regs.h"
#include "dcore0_edma0_qm_cgm_regs.h"
#include "dcore0_edma0_core_regs.h"
#include "dcore0_edma0_qm_regs.h"
#include "dcore0_edma0_qm_arc_aux_regs.h"
#include "dcore0_edma0_core_ctx_regs.h"
#include "dcore0_edma0_core_ctx_axuser_regs.h"
#include "dcore0_edma0_qm_axuser_nonsecured_regs.h"
#include "dcore0_edma1_core_ctx_axuser_regs.h"
#include "dcore0_edma1_qm_axuser_nonsecured_regs.h"
#include "dcore0_hmmu0_stlb_regs.h"
#include "dcore0_hmmu0_mmu_regs.h"
#include "rot0_qm_regs.h"
#include "rot0_qm_cgm_regs.h"
#include "rot0_qm_arc_aux_regs.h"
#include "rot0_regs.h"
#include "rot0_desc_regs.h"
#include "rot0_qm_axuser_nonsecured_regs.h"
#include "dcore0_rtr0_mstr_if_rr_prvt_hbw_regs.h"
#include "dcore0_rtr0_mstr_if_rr_prvt_lbw_regs.h"
#include "dcore0_rtr0_mstr_if_rr_shrd_hbw_regs.h"
#include "dcore0_rtr0_mstr_if_rr_shrd_lbw_regs.h"
#include "dcore0_rtr0_ctrl_regs.h"
#include "dcore0_dec0_cmd_regs.h"
#include "dcore0_vdec0_brdg_ctrl_regs.h"
#include "dcore0_vdec0_brdg_ctrl_axuser_dec_regs.h"
#include "dcore0_vdec0_brdg_ctrl_axuser_msix_abnrm_regs.h"
#include "dcore0_vdec0_brdg_ctrl_axuser_msix_l2c_regs.h"
#include "dcore0_vdec0_brdg_ctrl_axuser_msix_nrm_regs.h"
#include "dcore0_vdec0_brdg_ctrl_axuser_msix_vcd_regs.h"
#include "dcore0_vdec0_ctrl_special_regs.h"
#include "pcie_vdec0_brdg_ctrl_axuser_dec_regs.h"
#include "pcie_vdec0_brdg_ctrl_axuser_msix_abnrm_regs.h"
#include "pcie_vdec0_brdg_ctrl_axuser_msix_l2c_regs.h"
#include "pcie_vdec0_brdg_ctrl_axuser_msix_nrm_regs.h"
#include "pcie_vdec0_brdg_ctrl_axuser_msix_vcd_regs.h"
#include "pcie_dec0_cmd_regs.h"
#include "pcie_vdec0_brdg_ctrl_regs.h"
#include "pcie_vdec0_ctrl_special_regs.h"
#include "dcore0_mme_qm_regs.h"
#include "dcore0_mme_qm_arc_aux_regs.h"
#include "dcore0_mme_qm_axuser_secured_regs.h"
#include "dcore0_mme_qm_cgm_regs.h"
#include "dcore0_mme_qm_arc_acp_eng_regs.h"
#include "dcore0_mme_qm_axuser_nonsecured_regs.h"
#include "dcore0_mme_qm_arc_dup_eng_regs.h"
#include "dcore0_mme_qm_arc_dup_eng_axuser_regs.h"
#include "dcore0_mme_sbte0_mstr_if_axuser_regs.h"
#include "dcore0_mme_wb0_mstr_if_axuser_regs.h"
#include "dcore0_mme_acc_regs.h"
#include "dcore0_mme_ctrl_lo_regs.h"
#include "dcore1_mme_ctrl_lo_regs.h"
#include "dcore3_mme_ctrl_lo_regs.h"
#include "dcore0_mme_ctrl_lo_mme_axuser_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_cout0_master_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_cout0_slave_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_cout1_master_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_cout1_slave_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_in0_master_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_in0_slave_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_in1_master_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_in1_slave_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_in2_master_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_in2_slave_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_in3_master_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_in3_slave_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_in4_master_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_in4_slave_regs.h"
#include "dcore0_mme_ctrl_lo_arch_base_addr_regs.h"
#include "dcore0_mme_ctrl_lo_arch_non_tensor_end_regs.h"
#include "dcore0_mme_ctrl_lo_arch_non_tensor_start_regs.h"
#include "dcore0_mme_ctrl_lo_arch_tensor_a_regs.h"
#include "dcore0_mme_ctrl_lo_arch_tensor_b_regs.h"
#include "dcore0_mme_ctrl_lo_arch_tensor_cout_regs.h"
#include "pcie_wrap_special_regs.h"

#include "pdma0_qm_masks.h"
#include "pdma0_core_masks.h"
#include "pdma0_core_special_masks.h"
#include "psoc_global_conf_masks.h"
#include "psoc_reset_conf_masks.h"
#include "arc_farm_kdma_masks.h"
#include "arc_farm_kdma_ctx_masks.h"
#include "arc_farm_arc0_aux_masks.h"
#include "arc_farm_kdma_ctx_axuser_masks.h"
#include "dcore0_sync_mngr_objs_masks.h"
#include "dcore0_sync_mngr_glbl_masks.h"
#include "dcore0_sync_mngr_mstr_if_axuser_masks.h"
#include "dcore0_tpc0_cfg_masks.h"
#include "dcore0_mme_ctrl_lo_masks.h"
#include "dcore0_mme_sbte0_masks.h"
#include "dcore0_edma0_qm_masks.h"
#include "dcore0_edma0_core_masks.h"
#include "dcore0_hmmu0_stlb_masks.h"
#include "dcore0_hmmu0_mmu_masks.h"
#include "dcore0_dec0_cmd_masks.h"
#include "dcore0_vdec0_brdg_ctrl_masks.h"
#include "pcie_dec0_cmd_masks.h"
#include "pcie_vdec0_brdg_ctrl_masks.h"
#include "rot0_masks.h"
#include "pmmu_hbw_stlb_masks.h"
#include "psoc_etr_masks.h"

#define mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR	0x4800040

#define SM_OBJS_PROT_BITS_OFFS			0x14000

#define DCORE_OFFSET			(mmDCORE1_TPC0_QM_BASE - mmDCORE0_TPC0_QM_BASE)
#define DCORE_EDMA_OFFSET		(mmDCORE0_EDMA1_QM_BASE - mmDCORE0_EDMA0_QM_BASE)
#define DCORE_TPC_OFFSET		(mmDCORE0_TPC1_QM_BASE - mmDCORE0_TPC0_QM_BASE)
#define DCORE_DEC_OFFSET		(mmDCORE0_DEC1_VSI_BASE - mmDCORE0_DEC0_VSI_BASE)
#define DCORE_HMMU_OFFSET		(mmDCORE0_HMMU1_MMU_BASE - mmDCORE0_HMMU0_MMU_BASE)
#define NIC_QM_OFFSET			(mmNIC0_QM1_BASE - mmNIC0_QM0_BASE)
#define PDMA_OFFSET			(mmPDMA1_QM_BASE - mmPDMA0_QM_BASE)
#define ROT_OFFSET			(mmROT1_BASE - mmROT0_BASE)

#define TPC_CFG_BASE_ADDRESS_HIGH_OFFSET \
			(mmDCORE0_TPC0_CFG_CFG_BASE_ADDRESS_HIGH - mmDCORE0_TPC0_CFG_BASE)

#define TPC_CFG_SM_BASE_ADDRESS_HIGH_OFFSET \
			(mmDCORE0_TPC0_CFG_SM_BASE_ADDRESS_HIGH - mmDCORE0_TPC0_CFG_BASE)

#define TPC_CFG_STALL_OFFSET		(mmDCORE0_TPC0_CFG_TPC_STALL - mmDCORE0_TPC0_CFG_BASE)
#define TPC_CFG_STALL_ON_ERR_OFFSET	(mmDCORE0_TPC0_CFG_STALL_ON_ERR - mmDCORE0_TPC0_CFG_BASE)
#define TPC_CFG_TPC_INTR_MASK_OFFSET	(mmDCORE0_TPC0_CFG_TPC_INTR_MASK - mmDCORE0_TPC0_CFG_BASE)
#define TPC_CFG_MSS_CONFIG_OFFSET	(mmDCORE0_TPC0_CFG_MSS_CONFIG - mmDCORE0_TPC0_CFG_BASE)

#define MME_ACC_INTR_MASK_OFFSET	(mmDCORE0_MME_ACC_INTR_MASK - mmDCORE0_MME_ACC_BASE)
#define MME_ACC_WR_AXI_AGG_COUT0_OFFSET	(mmDCORE0_MME_ACC_WR_AXI_AGG_COUT0 - mmDCORE0_MME_ACC_BASE)
#define MME_ACC_WR_AXI_AGG_COUT1_OFFSET	(mmDCORE0_MME_ACC_WR_AXI_AGG_COUT1 - mmDCORE0_MME_ACC_BASE)
#define MME_ACC_AP_LFSR_POLY_OFFSET	(mmDCORE0_MME_ACC_AP_LFSR_POLY - mmDCORE0_MME_ACC_BASE)
#define MME_ACC_AP_LFSR_SEED_SEL_OFFSET	(mmDCORE0_MME_ACC_AP_LFSR_SEED_SEL - mmDCORE0_MME_ACC_BASE)
#define MME_ACC_AP_LFSR_SEED_WDATA_OFFSET \
	(mmDCORE0_MME_ACC_AP_LFSR_SEED_WDATA - mmDCORE0_MME_ACC_BASE)

#define DMA_CORE_CFG_0_OFFSET		(mmARC_FARM_KDMA_CFG_0 - mmARC_FARM_KDMA_BASE)
#define DMA_CORE_CFG_1_OFFSET		(mmARC_FARM_KDMA_CFG_1 - mmARC_FARM_KDMA_BASE)
#define DMA_CORE_PROT_OFFSET		(mmARC_FARM_KDMA_PROT - mmARC_FARM_KDMA_BASE)
#define DMA_CORE_ERRMSG_ADDR_LO_OFFSET	(mmARC_FARM_KDMA_ERRMSG_ADDR_LO - mmARC_FARM_KDMA_BASE)
#define DMA_CORE_ERRMSG_ADDR_HI_OFFSET	(mmARC_FARM_KDMA_ERRMSG_ADDR_HI - mmARC_FARM_KDMA_BASE)
#define DMA_CORE_ERRMSG_WDATA_OFFSET	(mmARC_FARM_KDMA_ERRMSG_WDATA - mmARC_FARM_KDMA_BASE)

#define QM_PQ_BASE_LO_0_OFFSET		(mmPDMA0_QM_PQ_BASE_LO_0 - mmPDMA0_QM_BASE)
#define QM_PQ_BASE_HI_0_OFFSET		(mmPDMA0_QM_PQ_BASE_HI_0 - mmPDMA0_QM_BASE)
#define QM_PQ_SIZE_0_OFFSET		(mmPDMA0_QM_PQ_SIZE_0 - mmPDMA0_QM_BASE)
#define QM_PQ_PI_0_OFFSET		(mmPDMA0_QM_PQ_PI_0 - mmPDMA0_QM_BASE)
#define QM_PQ_CI_0_OFFSET		(mmPDMA0_QM_PQ_CI_0 - mmPDMA0_QM_BASE)
#define QM_CP_FENCE0_CNT_0_OFFSET	(mmPDMA0_QM_CP_FENCE0_CNT_0 - mmPDMA0_QM_BASE)

#define QM_CP_MSG_BASE0_ADDR_LO_0_OFFSET (mmPDMA0_QM_CP_MSG_BASE0_ADDR_LO_0 - mmPDMA0_QM_BASE)
#define QM_CP_MSG_BASE0_ADDR_HI_0_OFFSET (mmPDMA0_QM_CP_MSG_BASE0_ADDR_HI_0 - mmPDMA0_QM_BASE)
#define QM_CP_MSG_BASE1_ADDR_LO_0_OFFSET (mmPDMA0_QM_CP_MSG_BASE1_ADDR_LO_0 - mmPDMA0_QM_BASE)
#define QM_CP_MSG_BASE1_ADDR_HI_0_OFFSET (mmPDMA0_QM_CP_MSG_BASE1_ADDR_HI_0 - mmPDMA0_QM_BASE)

#define QM_CP_CFG_OFFSET		(mmPDMA0_QM_CP_CFG - mmPDMA0_QM_BASE)
#define QM_PQC_HBW_BASE_LO_0_OFFSET	(mmPDMA0_QM_PQC_HBW_BASE_LO_0 - mmPDMA0_QM_BASE)
#define QM_PQC_HBW_BASE_HI_0_OFFSET	(mmPDMA0_QM_PQC_HBW_BASE_HI_0 - mmPDMA0_QM_BASE)
#define QM_PQC_SIZE_0_OFFSET		(mmPDMA0_QM_PQC_SIZE_0 - mmPDMA0_QM_BASE)
#define QM_PQC_PI_0_OFFSET		(mmPDMA0_QM_PQC_PI_0 - mmPDMA0_QM_BASE)
#define QM_PQC_LBW_WDATA_0_OFFSET	(mmPDMA0_QM_PQC_LBW_WDATA_0 - mmPDMA0_QM_BASE)
#define QM_PQC_LBW_BASE_LO_0_OFFSET	(mmPDMA0_QM_PQC_LBW_BASE_LO_0 - mmPDMA0_QM_BASE)
#define QM_PQC_LBW_BASE_HI_0_OFFSET	(mmPDMA0_QM_PQC_LBW_BASE_HI_0 - mmPDMA0_QM_BASE)
#define QM_GLBL_ERR_ADDR_LO_OFFSET	(mmPDMA0_QM_GLBL_ERR_ADDR_LO - mmPDMA0_QM_BASE)
#define QM_PQC_CFG_OFFSET		(mmPDMA0_QM_PQC_CFG - mmPDMA0_QM_BASE)
#define QM_ARB_CFG_0_OFFSET		(mmPDMA0_QM_ARB_CFG_0 - mmPDMA0_QM_BASE)
#define QM_GLBL_CFG0_OFFSET		(mmPDMA0_QM_GLBL_CFG0 -	mmPDMA0_QM_BASE)
#define QM_GLBL_CFG1_OFFSET		(mmPDMA0_QM_GLBL_CFG1 - mmPDMA0_QM_BASE)
#define QM_GLBL_CFG2_OFFSET		(mmPDMA0_QM_GLBL_CFG2 - mmPDMA0_QM_BASE)
#define QM_GLBL_PROT_OFFSET		(mmPDMA0_QM_GLBL_PROT -	mmPDMA0_QM_BASE)
#define QM_GLBL_ERR_CFG_OFFSET		(mmPDMA0_QM_GLBL_ERR_CFG - mmPDMA0_QM_BASE)
#define QM_GLBL_ERR_CFG1_OFFSET		(mmPDMA0_QM_GLBL_ERR_CFG1 - mmPDMA0_QM_BASE)
#define QM_GLBL_ERR_ADDR_HI_OFFSET	(mmPDMA0_QM_GLBL_ERR_ADDR_HI - mmPDMA0_QM_BASE)
#define QM_GLBL_ERR_WDATA_OFFSET	(mmPDMA0_QM_GLBL_ERR_WDATA - mmPDMA0_QM_BASE)
#define QM_ARB_ERR_MSG_EN_OFFSET	(mmPDMA0_QM_ARB_ERR_MSG_EN - mmPDMA0_QM_BASE)
#define QM_ARB_SLV_CHOISE_WDT_OFFSET	(mmPDMA0_QM_ARB_SLV_CHOICE_WDT - mmPDMA0_QM_BASE)
#define QM_FENCE2_OFFSET		(mmPDMA0_QM_CP_FENCE2_RDATA_0 - mmPDMA0_QM_BASE)
#define QM_SEI_STATUS_OFFSET		(mmPDMA0_QM_SEI_STATUS - mmPDMA0_QM_BASE)

#define SFT_OFFSET		(mmSFT1_HBW_RTR_IF0_RTR_H3_BASE - mmSFT0_HBW_RTR_IF0_RTR_H3_BASE)
#define SFT_IF_RTR_OFFSET	(mmSFT0_HBW_RTR_IF1_RTR_H3_BASE - mmSFT0_HBW_RTR_IF0_RTR_H3_BASE)

#define ARC_HALT_REQ_OFFSET	(mmARC_FARM_ARC0_AUX_RUN_HALT_REQ - mmARC_FARM_ARC0_AUX_BASE)
#define ARC_HALT_ACK_OFFSET	(mmARC_FARM_ARC0_AUX_RUN_HALT_ACK - mmARC_FARM_ARC0_AUX_BASE)

#define ARC_REGION_CFG_OFFSET(region) \
	(mmARC_FARM_ARC0_AUX_ARC_REGION_CFG_0 + (region * 4) - mmARC_FARM_ARC0_AUX_BASE)

#define ARC_DCCM_UPPER_EN_OFFSET \
	(mmARC_FARM_ARC0_AUX_MME_ARC_UPPER_DCCM_EN - mmARC_FARM_ARC0_AUX_BASE)

#define PCIE_VDEC_OFFSET	\
	(mmPCIE_VDEC1_MSTR_IF_RR_SHRD_HBW_BASE - mmPCIE_VDEC0_MSTR_IF_RR_SHRD_HBW_BASE)

#define DCORE_MME_SBTE_OFFSET	\
	(mmDCORE0_MME_SBTE1_MSTR_IF_RR_SHRD_HBW_BASE - mmDCORE0_MME_SBTE0_MSTR_IF_RR_SHRD_HBW_BASE)

#define DCORE_MME_WB_OFFSET	\
	(mmDCORE0_MME_WB1_MSTR_IF_RR_SHRD_HBW_BASE - mmDCORE0_MME_WB0_MSTR_IF_RR_SHRD_HBW_BASE)

#define DCORE_RTR_OFFSET	\
	(mmDCORE0_RTR1_MSTR_IF_RR_SHRD_HBW_BASE - mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

#define DCORE_VDEC_OFFSET	\
	(mmDCORE0_VDEC1_MSTR_IF_RR_SHRD_HBW_BASE - mmDCORE0_VDEC0_MSTR_IF_RR_SHRD_HBW_BASE)

#define MMU_OFFSET(REG)			(REG - mmDCORE0_HMMU0_MMU_BASE)
#define MMU_BYPASS_OFFSET		MMU_OFFSET(mmDCORE0_HMMU0_MMU_MMU_BYPASS)
#define MMU_SPI_SEI_MASK_OFFSET		MMU_OFFSET(mmDCORE0_HMMU0_MMU_SPI_SEI_MASK)
#define MMU_SPI_SEI_CAUSE_OFFSET	MMU_OFFSET(mmDCORE0_HMMU0_MMU_SPI_SEI_CAUSE)
#define MMU_ENABLE_OFFSET		MMU_OFFSET(mmDCORE0_HMMU0_MMU_MMU_ENABLE)
#define MMU_DDR_RANGE_REG_ENABLE	MMU_OFFSET(mmDCORE0_HMMU0_MMU_DDR_RANGE_REG_ENABLE)
#define MMU_RR_SEC_MIN_63_32_0_OFFSET	MMU_OFFSET(mmDCORE0_HMMU0_MMU_MMU_RR_SEC_MIN_63_32_0)
#define MMU_RR_SEC_MIN_31_0_0_OFFSET	MMU_OFFSET(mmDCORE0_HMMU0_MMU_MMU_RR_SEC_MIN_31_0_0)
#define MMU_RR_SEC_MAX_63_32_0_OFFSET	MMU_OFFSET(mmDCORE0_HMMU0_MMU_MMU_RR_SEC_MAX_63_32_0)
#define MMU_RR_SEC_MAX_31_0_0_OFFSET	MMU_OFFSET(mmDCORE0_HMMU0_MMU_MMU_RR_SEC_MAX_31_0_0)
#define MMU_RR_PRIV_MIN_63_32_0_OFFSET	MMU_OFFSET(mmDCORE0_HMMU0_MMU_MMU_RR_PRIV_MIN_63_32_0)
#define MMU_RR_PRIV_MIN_31_0_0_OFFSET	MMU_OFFSET(mmDCORE0_HMMU0_MMU_MMU_RR_PRIV_MIN_31_0_0)
#define MMU_RR_PRIV_MAX_63_32_0_OFFSET	MMU_OFFSET(mmDCORE0_HMMU0_MMU_MMU_RR_PRIV_MAX_63_32_0)
#define MMU_RR_PRIV_MAX_31_0_0_OFFSET	MMU_OFFSET(mmDCORE0_HMMU0_MMU_MMU_RR_PRIV_MAX_31_0_0)
#define MMU_INTERRUPT_CLR_OFFSET	MMU_OFFSET(mmDCORE0_HMMU0_MMU_INTERRUPT_CLR)

#define STLB_OFFSET(REG)		(REG - mmDCORE0_HMMU0_STLB_BASE)
#define STLB_BUSY_OFFSET		STLB_OFFSET(mmDCORE0_HMMU0_STLB_BUSY)
#define STLB_ASID_OFFSET		STLB_OFFSET(mmDCORE0_HMMU0_STLB_ASID)
#define STLB_HOP0_PA43_12_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_HOP0_PA43_12)
#define STLB_HOP0_PA63_44_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_HOP0_PA63_44)
#define STLB_HOP_CONFIGURATION_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_HOP_CONFIGURATION)
#define STLB_INV_ALL_START_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_INV_ALL_START)
#define STLB_SRAM_INIT_OFFSET		STLB_OFFSET(mmDCORE0_HMMU0_STLB_SRAM_INIT)
#define STLB_SET_THRESHOLD_HOP3_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_SET_THRESHOLD_HOP3)
#define STLB_SET_THRESHOLD_HOP2_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_SET_THRESHOLD_HOP2)
#define STLB_SET_THRESHOLD_HOP1_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_SET_THRESHOLD_HOP1)
#define STLB_SET_THRESHOLD_HOP0_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_SET_THRESHOLD_HOP0)
#define STLB_RANGE_INV_START_LSB_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_RANGE_INV_START_LSB)
#define STLB_RANGE_INV_START_MSB_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_RANGE_INV_START_MSB)
#define STLB_RANGE_INV_END_LSB_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_RANGE_INV_END_LSB)
#define STLB_RANGE_INV_END_MSB_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_RANGE_INV_END_MSB)

#define STLB_LL_LOOKUP_MASK_63_32_OFFSET	\
			STLB_OFFSET(mmDCORE0_HMMU0_STLB_LINK_LIST_LOOKUP_MASK_63_32)

#define STLB_RANGE_CACHE_INVALIDATION_OFFSET	\
			STLB_OFFSET(mmDCORE0_HMMU0_STLB_RANGE_CACHE_INVALIDATION)

/* RTR CTR RAZWI related offsets */
#define RTR_MSTR_IF_OFFSET	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE - mmDCORE0_RTR0_CTRL_BASE)

#define RTR_LBW_MSTR_IF_OFFSET	\
			(mmSFT0_LBW_RTR_IF_MSTR_IF_RR_SHRD_HBW_BASE - mmDCORE0_RTR0_CTRL_BASE)

/* RAZWI captured hbw aw addr high */
#define DEC_RAZWI_HBW_AW_ADDR_HI	\
			(mmDCORE0_RTR0_CTRL_DEC_RAZWI_HBW_AW_HI_ADDR - mmDCORE0_RTR0_CTRL_BASE)

/* RAZWI captured hbw aw addr low */
#define DEC_RAZWI_HBW_AW_ADDR_LO	\
			(mmDCORE0_RTR0_CTRL_DEC_RAZWI_HBW_AW_LO_ADDR - mmDCORE0_RTR0_CTRL_BASE)

/* RAZWI captured hbw aw set */
#define DEC_RAZWI_HBW_AW_SET		\
			(mmDCORE0_RTR0_CTRL_DEC_RAZWI_HBW_AW_SET - mmDCORE0_RTR0_CTRL_BASE)

/* RAZWI captured hbw ar addr high */
#define DEC_RAZWI_HBW_AR_ADDR_HI \
			(mmDCORE0_RTR0_CTRL_DEC_RAZWI_HBW_AR_HI_ADDR - mmDCORE0_RTR0_CTRL_BASE)

/* RAZWI captured hbw ar addr low */
#define DEC_RAZWI_HBW_AR_ADDR_LO	\
			(mmDCORE0_RTR0_CTRL_DEC_RAZWI_HBW_AR_LO_ADDR - mmDCORE0_RTR0_CTRL_BASE)

/* RAZWI captured hbw ar set */
#define DEC_RAZWI_HBW_AR_SET		\
			(mmDCORE0_RTR0_CTRL_DEC_RAZWI_HBW_AR_SET - mmDCORE0_RTR0_CTRL_BASE)

/* RAZWI captured lbw aw addr */
#define DEC_RAZWI_LBW_AW_ADDR		\
			(mmDCORE0_RTR0_CTRL_DEC_RAZWI_LBW_AW_ADDR - mmDCORE0_RTR0_CTRL_BASE)

/* RAZWI captured lbw aw set */
#define DEC_RAZWI_LBW_AW_SET		\
			(mmDCORE0_RTR0_CTRL_DEC_RAZWI_HBW_AW_SET - mmDCORE0_RTR0_CTRL_BASE)

/* RAZWI captured lbw ar addr */
#define DEC_RAZWI_LBW_AR_ADDR		\
			(mmDCORE0_RTR0_CTRL_DEC_RAZWI_LBW_AR_ADDR - mmDCORE0_RTR0_CTRL_BASE)

/* RAZWI captured lbw ar set */
#define DEC_RAZWI_LBW_AR_SET		\
			(mmDCORE0_RTR0_CTRL_DEC_RAZWI_LBW_AR_SET - mmDCORE0_RTR0_CTRL_BASE)

/* RAZWI captured shared hbw aw addr high */
#define RR_SHRD_HBW_AW_RAZWI_HI		\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_AW_RAZWI_HI - mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

/* RAZWI captured shared hbw aw addr low */
#define RR_SHRD_HBW_AW_RAZWI_LO		\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_AW_RAZWI_LO - mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

/* RAZWI captured shared hbw ar addr high */
#define RR_SHRD_HBW_AR_RAZWI_HI		\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_AR_RAZWI_HI - mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

/* RAZWI captured shared hbw ar addr low */
#define RR_SHRD_HBW_AR_RAZWI_LO		\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_AR_RAZWI_LO - mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

/* RAZWI captured shared aw XY coordinates */
#define RR_SHRD_HBW_AW_RAZWI_XY		\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_AW_RAZWI_XY - mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

/* RAZWI captured shared ar XY coordinates */
#define RR_SHRD_HBW_AR_RAZWI_XY		\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_AR_RAZWI_XY - mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

/* RAZWI hbw shared occurred due to write access */
#define RR_SHRD_HBW_AW_RAZWI_HAPPENED	\
			(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_AW_RAZWI_HAPPENED - \
				mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

/* RAZWI hbw shared occurred due to read access */
#define RR_SHRD_HBW_AR_RAZWI_HAPPENED	\
			(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_AR_RAZWI_HAPPENED - \
				mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

/* RAZWI captured shared lbw aw addr */
#define RR_SHRD_LBW_AW_RAZWI		\
			(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_AW_RAZWI - \
					mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

/* RAZWI captured shared lbw ar addr */
#define RR_SHRD_LBW_AR_RAZWI		\
			(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_AR_RAZWI - \
					mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

/* RAZWI captured shared lbw aw XY coordinates */
#define RR_SHRD_LBW_AW_RAZWI_XY		\
			(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_AW_RAZWI_XY - \
					mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

/* RAZWI captured shared lbw ar XY coordinates */
#define RR_SHRD_LBW_AR_RAZWI_XY		\
			(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_AR_RAZWI_XY - \
					mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

/* RAZWI lbw shared occurred due to write access */
#define RR_SHRD_LBW_AW_RAZWI_HAPPENED	\
			(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_AW_RAZWI_HAPPENED - \
					mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

/* RAZWI lbw shared occurred due to read access */
#define RR_SHRD_LBW_AR_RAZWI_HAPPENED	\
			(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_AR_RAZWI_HAPPENED - \
				mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

#define BRDG_CTRL_BLOCK_OFFSET (mmDCORE0_VDEC0_BRDG_CTRL_BASE - mmDCORE0_DEC0_CMD_BASE)
#define SPECIAL_BLOCK_OFFSET (mmDCORE0_VDEC0_BRDG_CTRL_SPECIAL_BASE - mmDCORE0_DEC0_CMD_BASE)
#define SFT_DCORE_OFFSET (mmSFT1_HBW_RTR_IF0_RTR_CTRL_BASE - mmSFT0_HBW_RTR_IF0_RTR_CTRL_BASE)
#define SFT_IF_OFFSET (mmSFT0_HBW_RTR_IF1_RTR_CTRL_BASE - mmSFT0_HBW_RTR_IF0_RTR_CTRL_BASE)

#define BRDG_CTRL_NRM_MSIX_LBW_AWADDR	\
	(mmDCORE0_VDEC0_BRDG_CTRL_NRM_MSIX_LBW_AWADDR - mmDCORE0_VDEC0_BRDG_CTRL_BASE)

#define BRDG_CTRL_NRM_MSIX_LBW_WDATA	\
	(mmDCORE0_VDEC0_BRDG_CTRL_NRM_MSIX_LBW_WDATA - mmDCORE0_VDEC0_BRDG_CTRL_BASE)

#define BRDG_CTRL_ABNRM_MSIX_LBW_AWADDR	\
	(mmDCORE0_VDEC0_BRDG_CTRL_ABNRM_MSIX_LBW_AWADDR - mmDCORE0_VDEC0_BRDG_CTRL_BASE)

#define BRDG_CTRL_ABNRM_MSIX_LBW_WDATA	\
	(mmDCORE0_VDEC0_BRDG_CTRL_ABNRM_MSIX_LBW_WDATA - mmDCORE0_VDEC0_BRDG_CTRL_BASE)

#define RR_SHRD_HBW_SEC_RANGE_MIN_SHORT_LO_0_OFFSET	\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_SEC_RANGE_MIN_SHORT_LO_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

#define RR_SHRD_HBW_SEC_RANGE_MIN_SHORT_HI_0_OFFSET	\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_SEC_RANGE_MIN_SHORT_HI_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

#define RR_SHRD_HBW_SEC_RANGE_MAX_SHORT_LO_0_OFFSET	\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_SEC_RANGE_MAX_SHORT_LO_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

#define RR_SHRD_HBW_SEC_RANGE_MAX_SHORT_HI_0_OFFSET	\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_SEC_RANGE_MAX_SHORT_HI_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

#define RR_SHRD_HBW_PRIV_RANGE_MIN_SHORT_LO_0_OFFSET	\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_PRIV_RANGE_MIN_SHORT_LO_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

#define RR_SHRD_HBW_PRIV_RANGE_MIN_SHORT_HI_0_OFFSET	\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_PRIV_RANGE_MIN_SHORT_HI_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

#define RR_SHRD_HBW_PRIV_RANGE_MAX_SHORT_LO_0_OFFSET	\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_PRIV_RANGE_MAX_SHORT_LO_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

#define RR_SHRD_HBW_PRIV_RANGE_MAX_SHORT_HI_0_OFFSET	\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_PRIV_RANGE_MAX_SHORT_HI_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

#define RR_SHRD_HBW_SEC_RANGE_MIN_HI_0_OFFSET	\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_SEC_RANGE_MIN_HI_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

#define RR_SHRD_HBW_SEC_RANGE_MIN_LO_0_OFFSET	\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_SEC_RANGE_MIN_LO_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

#define RR_SHRD_HBW_SEC_RANGE_MAX_HI_0_OFFSET	\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_SEC_RANGE_MAX_HI_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

#define RR_SHRD_HBW_SEC_RANGE_MAX_LO_0_OFFSET	\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_SEC_RANGE_MAX_LO_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

#define RR_SHRD_HBW_PRIV_RANGE_MIN_HI_0_OFFSET	\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_PRIV_RANGE_MIN_HI_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

#define RR_SHRD_HBW_PRIV_RANGE_MIN_LO_0_OFFSET	\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_PRIV_RANGE_MIN_LO_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

#define RR_SHRD_HBW_PRIV_RANGE_MAX_HI_0_OFFSET	\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_PRIV_RANGE_MAX_HI_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

#define RR_SHRD_HBW_PRIV_RANGE_MAX_LO_0_OFFSET	\
	(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_PRIV_RANGE_MAX_LO_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_HBW_BASE)

#define RR_LBW_SEC_RANGE_MIN_SHORT_0_OFFSET	\
		(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_SEC_RANGE_MIN_SHORT_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_BASE)

#define RR_LBW_SEC_RANGE_MAX_SHORT_0_OFFSET	\
		(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_SEC_RANGE_MAX_SHORT_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_BASE)

#define RR_LBW_PRIV_RANGE_MIN_SHORT_0_OFFSET	\
		(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_PRIV_RANGE_MIN_SHORT_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_BASE)

#define RR_LBW_PRIV_RANGE_MAX_SHORT_0_OFFSET	\
		(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_PRIV_RANGE_MAX_SHORT_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_BASE)

#define RR_LBW_SEC_RANGE_MIN_0_OFFSET	\
		(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_SEC_RANGE_MIN_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_BASE)

#define RR_LBW_SEC_RANGE_MAX_0_OFFSET	\
		(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_SEC_RANGE_MAX_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_BASE)

#define RR_LBW_PRIV_RANGE_MIN_0_OFFSET	\
		(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_PRIV_RANGE_MIN_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_BASE)

#define RR_LBW_PRIV_RANGE_MAX_0_OFFSET	\
		(mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_PRIV_RANGE_MAX_0 - \
			mmDCORE0_RTR0_MSTR_IF_RR_SHRD_LBW_BASE)

#define ARC_AUX_DCCM_QUEUE_PUSH_REG_0_OFFSET	\
		(mmARC_FARM_ARC0_AUX_DCCM_QUEUE_PUSH_REG_0 - mmARC_FARM_ARC0_AUX_BASE)

#define MMU_STATIC_MULTI_PAGE_SIZE_OFFSET	\
	(mmDCORE0_HMMU0_MMU_STATIC_MULTI_PAGE_SIZE - mmDCORE0_HMMU0_MMU_BASE)

#define HBM_MC_SPI_TEMP_PIN_CHG_MASK		BIT(0)
#define HBM_MC_SPI_THR_ENG_MASK			BIT(1)
#define HBM_MC_SPI_THR_DIS_ENG_MASK		BIT(2)
#define HBM_MC_SPI_IEEE1500_COMP_MASK		BIT(3)
#define HBM_MC_SPI_IEEE1500_PAUSED_MASK		BIT(4)

#include "nic0_qpc0_regs.h"
#include "nic0_qm0_regs.h"
#include "nic0_qm_arc_aux0_regs.h"
#include "nic0_qm0_cgm_regs.h"
#include "nic0_umr0_0_completion_queue_ci_1_regs.h"
#include "nic0_umr0_0_unsecure_doorbell0_regs.h"

#define NIC_OFFSET		(mmNIC1_MSTR_IF_RR_SHRD_HBW_BASE - mmNIC0_MSTR_IF_RR_SHRD_HBW_BASE)

#define NIC_UMR_OFFSET \
	(mmNIC0_UMR0_1_UNSECURE_DOORBELL0_BASE - mmNIC0_UMR0_0_UNSECURE_DOORBELL0_BASE)

#endif /* ASIC_REG_GAUDI2_REGS_H_ */
