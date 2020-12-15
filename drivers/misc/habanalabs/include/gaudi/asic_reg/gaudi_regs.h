/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef ASIC_REG_GAUDI_REGS_H_
#define ASIC_REG_GAUDI_REGS_H_

#include "gaudi_blocks.h"
#include "psoc_global_conf_regs.h"
#include "psoc_timestamp_regs.h"
#include "cpu_if_regs.h"
#include "mmu_up_regs.h"
#include "stlb_regs.h"
#include "dma0_qm_regs.h"
#include "dma1_qm_regs.h"
#include "dma2_qm_regs.h"
#include "dma3_qm_regs.h"
#include "dma4_qm_regs.h"
#include "dma5_qm_regs.h"
#include "dma6_qm_regs.h"
#include "dma7_qm_regs.h"
#include "dma0_core_regs.h"
#include "dma1_core_regs.h"
#include "dma2_core_regs.h"
#include "dma3_core_regs.h"
#include "dma4_core_regs.h"
#include "dma5_core_regs.h"
#include "dma6_core_regs.h"
#include "dma7_core_regs.h"
#include "mme0_ctrl_regs.h"
#include "mme1_ctrl_regs.h"
#include "mme2_ctrl_regs.h"
#include "mme3_ctrl_regs.h"
#include "mme0_qm_regs.h"
#include "mme2_qm_regs.h"
#include "tpc0_cfg_regs.h"
#include "tpc1_cfg_regs.h"
#include "tpc2_cfg_regs.h"
#include "tpc3_cfg_regs.h"
#include "tpc4_cfg_regs.h"
#include "tpc5_cfg_regs.h"
#include "tpc6_cfg_regs.h"
#include "tpc7_cfg_regs.h"
#include "tpc0_qm_regs.h"
#include "tpc1_qm_regs.h"
#include "tpc2_qm_regs.h"
#include "tpc3_qm_regs.h"
#include "tpc4_qm_regs.h"
#include "tpc5_qm_regs.h"
#include "tpc6_qm_regs.h"
#include "tpc7_qm_regs.h"
#include "dma_if_e_n_down_ch0_regs.h"
#include "dma_if_e_n_down_ch1_regs.h"
#include "dma_if_e_s_down_ch0_regs.h"
#include "dma_if_e_s_down_ch1_regs.h"
#include "dma_if_w_n_down_ch0_regs.h"
#include "dma_if_w_n_down_ch1_regs.h"
#include "dma_if_w_s_down_ch0_regs.h"
#include "dma_if_w_s_down_ch1_regs.h"
#include "dma_if_e_n_regs.h"
#include "dma_if_e_s_regs.h"
#include "dma_if_w_n_regs.h"
#include "dma_if_w_s_regs.h"
#include "nif_rtr_ctrl_0_regs.h"
#include "nif_rtr_ctrl_1_regs.h"
#include "nif_rtr_ctrl_2_regs.h"
#include "nif_rtr_ctrl_3_regs.h"
#include "nif_rtr_ctrl_4_regs.h"
#include "nif_rtr_ctrl_5_regs.h"
#include "nif_rtr_ctrl_6_regs.h"
#include "nif_rtr_ctrl_7_regs.h"
#include "sif_rtr_ctrl_0_regs.h"
#include "sif_rtr_ctrl_1_regs.h"
#include "sif_rtr_ctrl_2_regs.h"
#include "sif_rtr_ctrl_3_regs.h"
#include "sif_rtr_ctrl_4_regs.h"
#include "sif_rtr_ctrl_5_regs.h"
#include "sif_rtr_ctrl_6_regs.h"
#include "sif_rtr_ctrl_7_regs.h"
#include "psoc_etr_regs.h"

#include "dma0_qm_masks.h"
#include "mme0_qm_masks.h"
#include "tpc0_qm_masks.h"
#include "dma0_core_masks.h"
#include "tpc0_cfg_masks.h"
#include "psoc_global_conf_masks.h"

#include "psoc_pci_pll_regs.h"
#include "psoc_hbm_pll_regs.h"
#include "psoc_cpu_pll_regs.h"

#define GAUDI_ECC_MEM_SEL_OFFSET		0xF18
#define GAUDI_ECC_ADDRESS_OFFSET		0xF1C
#define GAUDI_ECC_SYNDROME_OFFSET		0xF20
#define GAUDI_ECC_MEM_INFO_CLR_OFFSET		0xF28
#define GAUDI_ECC_MEM_INFO_CLR_SERR_MASK	BIT(8)
#define GAUDI_ECC_MEM_INFO_CLR_DERR_MASK	BIT(9)
#define GAUDI_ECC_SERR0_OFFSET			0xF30
#define GAUDI_ECC_DERR0_OFFSET			0xF40

#define mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_SOB_OBJ_0                     0x492000
#define mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0               0x494000
#define mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_PAY_ADDRH_0               0x494800
#define mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_PAY_DATA_0                0x495000
#define mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_ARM_0                     0x495800
#define mmSYNC_MNGR_W_S_SYNC_MNGR_OBJS_MON_STATUS_0                  0x496000
#define mmSYNC_MNGR_E_S_SYNC_MNGR_OBJS_SOB_OBJ_0                     0x4B2000
#define mmSYNC_MNGR_E_S_SYNC_MNGR_OBJS_MON_STATUS_0                  0x4B6000
#define mmSYNC_MNGR_W_N_SYNC_MNGR_OBJS_SOB_OBJ_0                     0x4D2000
#define mmSYNC_MNGR_W_N_SYNC_MNGR_OBJS_MON_STATUS_0                  0x4D6000
#define mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0                     0x4F2000
#define mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_1                     0x4F2004
#define mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_2047                  0x4F3FFC
#define mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0               0x4F4000
#define mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_STATUS_0                  0x4F6000
#define mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_STATUS_511                0x4F67FC

#define mmSIF_RTR_0_LBW_RANGE_PROT_HIT_AW                            0x300400
#define mmSIF_RTR_1_LBW_RANGE_PROT_HIT_AW                            0x310400
#define mmSIF_RTR_2_LBW_RANGE_PROT_HIT_AW                            0x320400
#define mmSIF_RTR_3_LBW_RANGE_PROT_HIT_AW                            0x330400
#define mmSIF_RTR_4_LBW_RANGE_PROT_HIT_AW                            0x340400
#define mmSIF_RTR_5_LBW_RANGE_PROT_HIT_AW                            0x350400
#define mmSIF_RTR_6_LBW_RANGE_PROT_HIT_AW                            0x360400
#define mmSIF_RTR_7_LBW_RANGE_PROT_HIT_AW                            0x370400

#define mmSIF_RTR_0_LBW_RANGE_PROT_HIT_AR                            0x300490
#define mmSIF_RTR_1_LBW_RANGE_PROT_HIT_AR                            0x310490
#define mmSIF_RTR_2_LBW_RANGE_PROT_HIT_AR                            0x320490
#define mmSIF_RTR_3_LBW_RANGE_PROT_HIT_AR                            0x330490
#define mmSIF_RTR_4_LBW_RANGE_PROT_HIT_AR                            0x340490
#define mmSIF_RTR_5_LBW_RANGE_PROT_HIT_AR                            0x350490
#define mmSIF_RTR_6_LBW_RANGE_PROT_HIT_AR                            0x360490
#define mmSIF_RTR_7_LBW_RANGE_PROT_HIT_AR                            0x370490

#define mmSIF_RTR_0_LBW_RANGE_PROT_MIN_AW_0                          0x300410
#define mmSIF_RTR_1_LBW_RANGE_PROT_MIN_AW_0                          0x310410
#define mmSIF_RTR_2_LBW_RANGE_PROT_MIN_AW_0                          0x320410
#define mmSIF_RTR_3_LBW_RANGE_PROT_MIN_AW_0                          0x330410
#define mmSIF_RTR_4_LBW_RANGE_PROT_MIN_AW_0                          0x340410
#define mmSIF_RTR_5_LBW_RANGE_PROT_MIN_AW_0                          0x350410
#define mmSIF_RTR_6_LBW_RANGE_PROT_MIN_AW_0                          0x360410
#define mmSIF_RTR_7_LBW_RANGE_PROT_MIN_AW_0                          0x370410

#define mmSIF_RTR_0_LBW_RANGE_PROT_MAX_AW_0                          0x300450
#define mmSIF_RTR_1_LBW_RANGE_PROT_MAX_AW_0                          0x310450
#define mmSIF_RTR_2_LBW_RANGE_PROT_MAX_AW_0                          0x320450
#define mmSIF_RTR_3_LBW_RANGE_PROT_MAX_AW_0                          0x330450
#define mmSIF_RTR_4_LBW_RANGE_PROT_MAX_AW_0                          0x340450
#define mmSIF_RTR_5_LBW_RANGE_PROT_MAX_AW_0                          0x350450
#define mmSIF_RTR_6_LBW_RANGE_PROT_MAX_AW_0                          0x360450
#define mmSIF_RTR_7_LBW_RANGE_PROT_MAX_AW_0                          0x370450

#define mmSIF_RTR_0_LBW_RANGE_PROT_MIN_AR_0                          0x3004A0
#define mmSIF_RTR_1_LBW_RANGE_PROT_MIN_AR_0                          0x3104A0
#define mmSIF_RTR_2_LBW_RANGE_PROT_MIN_AR_0                          0x3204A0
#define mmSIF_RTR_3_LBW_RANGE_PROT_MIN_AR_0                          0x3304A0
#define mmSIF_RTR_4_LBW_RANGE_PROT_MIN_AR_0                          0x3404A0
#define mmSIF_RTR_5_LBW_RANGE_PROT_MIN_AR_0                          0x3504A0
#define mmSIF_RTR_6_LBW_RANGE_PROT_MIN_AR_0                          0x3604A0
#define mmSIF_RTR_7_LBW_RANGE_PROT_MIN_AR_0                          0x3704A0

#define mmSIF_RTR_0_LBW_RANGE_PROT_MAX_AR_0                          0x3004E0
#define mmSIF_RTR_1_LBW_RANGE_PROT_MAX_AR_0                          0x3104E0
#define mmSIF_RTR_2_LBW_RANGE_PROT_MAX_AR_0                          0x3204E0
#define mmSIF_RTR_3_LBW_RANGE_PROT_MAX_AR_0                          0x3304E0
#define mmSIF_RTR_4_LBW_RANGE_PROT_MAX_AR_0                          0x3404E0
#define mmSIF_RTR_5_LBW_RANGE_PROT_MAX_AR_0                          0x3504E0
#define mmSIF_RTR_6_LBW_RANGE_PROT_MAX_AR_0                          0x3604E0
#define mmSIF_RTR_7_LBW_RANGE_PROT_MAX_AR_0                          0x3704E0

#define mmNIF_RTR_0_LBW_RANGE_PROT_HIT_AW                            0x380400
#define mmNIF_RTR_1_LBW_RANGE_PROT_HIT_AW                            0x390400
#define mmNIF_RTR_2_LBW_RANGE_PROT_HIT_AW                            0x3A0400
#define mmNIF_RTR_3_LBW_RANGE_PROT_HIT_AW                            0x3B0400
#define mmNIF_RTR_4_LBW_RANGE_PROT_HIT_AW                            0x3C0400
#define mmNIF_RTR_5_LBW_RANGE_PROT_HIT_AW                            0x3D0400
#define mmNIF_RTR_6_LBW_RANGE_PROT_HIT_AW                            0x3E0400
#define mmNIF_RTR_7_LBW_RANGE_PROT_HIT_AW                            0x3F0400

#define mmNIF_RTR_0_LBW_RANGE_PROT_HIT_AR                            0x380490
#define mmNIF_RTR_1_LBW_RANGE_PROT_HIT_AR                            0x390490
#define mmNIF_RTR_2_LBW_RANGE_PROT_HIT_AR                            0x3A0490
#define mmNIF_RTR_3_LBW_RANGE_PROT_HIT_AR                            0x3B0490
#define mmNIF_RTR_4_LBW_RANGE_PROT_HIT_AR                            0x3C0490
#define mmNIF_RTR_5_LBW_RANGE_PROT_HIT_AR                            0x3D0490
#define mmNIF_RTR_6_LBW_RANGE_PROT_HIT_AR                            0x3E0490
#define mmNIF_RTR_7_LBW_RANGE_PROT_HIT_AR                            0x3F0490

#define mmNIF_RTR_0_LBW_RANGE_PROT_MIN_AW_0                          0x380410
#define mmNIF_RTR_1_LBW_RANGE_PROT_MIN_AW_0                          0x390410
#define mmNIF_RTR_2_LBW_RANGE_PROT_MIN_AW_0                          0x3A0410
#define mmNIF_RTR_3_LBW_RANGE_PROT_MIN_AW_0                          0x3B0410
#define mmNIF_RTR_4_LBW_RANGE_PROT_MIN_AW_0                          0x3C0410
#define mmNIF_RTR_5_LBW_RANGE_PROT_MIN_AW_0                          0x3D0410
#define mmNIF_RTR_6_LBW_RANGE_PROT_MIN_AW_0                          0x3E0410
#define mmNIF_RTR_7_LBW_RANGE_PROT_MIN_AW_0                          0x3F0410

#define mmNIF_RTR_0_LBW_RANGE_PROT_MAX_AW_0                          0x380450
#define mmNIF_RTR_1_LBW_RANGE_PROT_MAX_AW_0                          0x390450
#define mmNIF_RTR_2_LBW_RANGE_PROT_MAX_AW_0                          0x3A0450
#define mmNIF_RTR_3_LBW_RANGE_PROT_MAX_AW_0                          0x3B0450
#define mmNIF_RTR_4_LBW_RANGE_PROT_MAX_AW_0                          0x3C0450
#define mmNIF_RTR_5_LBW_RANGE_PROT_MAX_AW_0                          0x3D0450
#define mmNIF_RTR_6_LBW_RANGE_PROT_MAX_AW_0                          0x3E0450
#define mmNIF_RTR_7_LBW_RANGE_PROT_MAX_AW_0                          0x3F0450

#define mmNIF_RTR_0_LBW_RANGE_PROT_MIN_AR_0                          0x3804A0
#define mmNIF_RTR_1_LBW_RANGE_PROT_MIN_AR_0                          0x3904A0
#define mmNIF_RTR_2_LBW_RANGE_PROT_MIN_AR_0                          0x3A04A0
#define mmNIF_RTR_3_LBW_RANGE_PROT_MIN_AR_0                          0x3B04A0
#define mmNIF_RTR_4_LBW_RANGE_PROT_MIN_AR_0                          0x3C04A0
#define mmNIF_RTR_5_LBW_RANGE_PROT_MIN_AR_0                          0x3D04A0
#define mmNIF_RTR_6_LBW_RANGE_PROT_MIN_AR_0                          0x3E04A0
#define mmNIF_RTR_7_LBW_RANGE_PROT_MIN_AR_0                          0x3F04A0

#define mmNIF_RTR_0_LBW_RANGE_PROT_MAX_AR_0                          0x3804E0
#define mmNIF_RTR_1_LBW_RANGE_PROT_MAX_AR_0                          0x3904E0
#define mmNIF_RTR_2_LBW_RANGE_PROT_MAX_AR_0                          0x3A04E0
#define mmNIF_RTR_3_LBW_RANGE_PROT_MAX_AR_0                          0x3B04E0
#define mmNIF_RTR_4_LBW_RANGE_PROT_MAX_AR_0                          0x3C04E0
#define mmNIF_RTR_5_LBW_RANGE_PROT_MAX_AR_0                          0x3D04E0
#define mmNIF_RTR_6_LBW_RANGE_PROT_MAX_AR_0                          0x3E04E0
#define mmNIF_RTR_7_LBW_RANGE_PROT_MAX_AR_0                          0x3F04E0

#define mmDMA_IF_W_S_DOWN_RSP_MID_WGHT_0                             0x489030
#define mmDMA_IF_W_S_DOWN_RSP_MID_WGHT_1                             0x489034

#define mmDMA_IF_E_S_DOWN_RSP_MID_WGHT_0                             0x4A9030
#define mmDMA_IF_E_S_DOWN_RSP_MID_WGHT_1                             0x4A9034

#define mmDMA_IF_W_N_DOWN_RSP_MID_WGHT_0                             0x4C9030
#define mmDMA_IF_W_N_DOWN_RSP_MID_WGHT_1                             0x4C9034

#define mmDMA_IF_E_N_DOWN_RSP_MID_WGHT_0                             0x4E9030
#define mmDMA_IF_E_N_DOWN_RSP_MID_WGHT_1                             0x4E9034

#define mmMME1_QM_GLBL_CFG0                                          0xE8000
#define mmMME1_QM_GLBL_STS0                                          0xE8038

#define mmMME0_SBAB_SB_STALL                                         0x4002C
#define mmMME0_SBAB_ARUSER0                                          0x40034
#define mmMME0_SBAB_ARUSER1                                          0x40038
#define mmMME0_SBAB_PROT                                             0x40050

#define mmMME1_SBAB_SB_STALL                                         0xC002C
#define mmMME1_SBAB_ARUSER0                                          0xC0034
#define mmMME1_SBAB_ARUSER1                                          0xC0038
#define mmMME1_SBAB_PROT                                             0xC0050

#define mmMME2_SBAB_SB_STALL                                         0x14002C
#define mmMME2_SBAB_ARUSER0                                          0x140034
#define mmMME2_SBAB_ARUSER1                                          0x140038
#define mmMME2_SBAB_PROT                                             0x140050

#define mmMME3_SBAB_SB_STALL                                         0x1C002C
#define mmMME3_SBAB_ARUSER0                                          0x1C0034
#define mmMME3_SBAB_ARUSER1                                          0x1C0038
#define mmMME3_SBAB_PROT                                             0x1C0050

#define mmMME0_ACC_ACC_STALL                                         0x20028
#define mmMME0_ACC_WBC                                               0x20038
#define mmMME0_ACC_PROT                                              0x20050

#define mmMME1_ACC_ACC_STALL                                         0xA0028
#define mmMME1_ACC_WBC                                               0xA0038
#define mmMME1_ACC_PROT                                              0xA0050

#define mmMME2_ACC_ACC_STALL                                         0x120028
#define mmMME2_ACC_WBC                                               0x120038
#define mmMME2_ACC_PROT                                              0x120050

#define mmMME3_ACC_ACC_STALL                                         0x1A0028
#define mmMME3_ACC_WBC                                               0x1A0038
#define mmMME3_ACC_PROT                                              0x1A0050

#define mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR                         0x800040

#define mmPSOC_EFUSE_READ                                            0xC4A000
#define mmPSOC_EFUSE_DATA_0                                          0xC4A080

#define mmPCIE_WRAP_MAX_OUTSTAND                                     0xC01B20
#define mmPCIE_WRAP_LBW_PROT_OVR                                     0xC01B48
#define mmPCIE_WRAP_HBW_DRAIN_CFG                                    0xC01D54
#define mmPCIE_WRAP_LBW_DRAIN_CFG                                    0xC01D5C

#define mmPCIE_MSI_INTR_0                                            0xC13000

#define mmPCIE_DBI_DEVICE_ID_VENDOR_ID_REG                           0xC02000

#define mmPCIE_AUX_FLR_CTRL                                          0xC07394
#define mmPCIE_AUX_DBI                                               0xC07490

#endif /* ASIC_REG_GAUDI_REGS_H_ */
