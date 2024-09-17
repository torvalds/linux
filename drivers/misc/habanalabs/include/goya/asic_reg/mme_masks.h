/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2018 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

/************************************
 ** This is an auto-generated file **
 **       DO NOT EDIT BELOW        **
 ************************************/

#ifndef ASIC_REG_MME_MASKS_H_
#define ASIC_REG_MME_MASKS_H_

/*
 *****************************************
 *   MME (Prototype: MME)
 *****************************************
 */

/* MME_ARCH_STATUS */
#define MME_ARCH_STATUS_A_SHIFT                                      0
#define MME_ARCH_STATUS_A_MASK                                       0x1
#define MME_ARCH_STATUS_B_SHIFT                                      1
#define MME_ARCH_STATUS_B_MASK                                       0x2
#define MME_ARCH_STATUS_CIN_SHIFT                                    2
#define MME_ARCH_STATUS_CIN_MASK                                     0x4
#define MME_ARCH_STATUS_COUT_SHIFT                                   3
#define MME_ARCH_STATUS_COUT_MASK                                    0x8
#define MME_ARCH_STATUS_TE_SHIFT                                     4
#define MME_ARCH_STATUS_TE_MASK                                      0x10
#define MME_ARCH_STATUS_LD_SHIFT                                     5
#define MME_ARCH_STATUS_LD_MASK                                      0x20
#define MME_ARCH_STATUS_ST_SHIFT                                     6
#define MME_ARCH_STATUS_ST_MASK                                      0x40
#define MME_ARCH_STATUS_SB_A_EMPTY_SHIFT                             7
#define MME_ARCH_STATUS_SB_A_EMPTY_MASK                              0x80
#define MME_ARCH_STATUS_SB_B_EMPTY_SHIFT                             8
#define MME_ARCH_STATUS_SB_B_EMPTY_MASK                              0x100
#define MME_ARCH_STATUS_SB_CIN_EMPTY_SHIFT                           9
#define MME_ARCH_STATUS_SB_CIN_EMPTY_MASK                            0x200
#define MME_ARCH_STATUS_SB_COUT_EMPTY_SHIFT                          10
#define MME_ARCH_STATUS_SB_COUT_EMPTY_MASK                           0x400
#define MME_ARCH_STATUS_SM_IDLE_SHIFT                                11
#define MME_ARCH_STATUS_SM_IDLE_MASK                                 0x800
#define MME_ARCH_STATUS_WBC_AXI_IDLE_SHIFT                           12
#define MME_ARCH_STATUS_WBC_AXI_IDLE_MASK                            0xF000
#define MME_ARCH_STATUS_SBC_AXI_IDLE_SHIFT                           16
#define MME_ARCH_STATUS_SBC_AXI_IDLE_MASK                            0x30000
#define MME_ARCH_STATUS_SBB_AXI_IDLE_SHIFT                           18
#define MME_ARCH_STATUS_SBB_AXI_IDLE_MASK                            0xC0000
#define MME_ARCH_STATUS_SBA_AXI_IDLE_SHIFT                           20
#define MME_ARCH_STATUS_SBA_AXI_IDLE_MASK                            0x300000
#define MME_ARCH_STATUS_FREE_ACCUMS_SHIFT                            22
#define MME_ARCH_STATUS_FREE_ACCUMS_MASK                             0x1C00000

/* MME_ARCH_A_BASE_ADDR_HIGH */
#define MME_ARCH_A_BASE_ADDR_HIGH_V_SHIFT                            0
#define MME_ARCH_A_BASE_ADDR_HIGH_V_MASK                             0xFFFFFFFF

/* MME_ARCH_B_BASE_ADDR_HIGH */
#define MME_ARCH_B_BASE_ADDR_HIGH_V_SHIFT                            0
#define MME_ARCH_B_BASE_ADDR_HIGH_V_MASK                             0xFFFFFFFF

/* MME_ARCH_CIN_BASE_ADDR_HIGH */
#define MME_ARCH_CIN_BASE_ADDR_HIGH_V_SHIFT                          0
#define MME_ARCH_CIN_BASE_ADDR_HIGH_V_MASK                           0xFFFFFFFF

/* MME_ARCH_COUT_BASE_ADDR_HIGH */
#define MME_ARCH_COUT_BASE_ADDR_HIGH_V_SHIFT                         0
#define MME_ARCH_COUT_BASE_ADDR_HIGH_V_MASK                          0xFFFFFFFF

/* MME_ARCH_BIAS_BASE_ADDR_HIGH */
#define MME_ARCH_BIAS_BASE_ADDR_HIGH_V_SHIFT                         0
#define MME_ARCH_BIAS_BASE_ADDR_HIGH_V_MASK                          0xFFFFFFFF

/* MME_ARCH_A_BASE_ADDR_LOW */
#define MME_ARCH_A_BASE_ADDR_LOW_V_SHIFT                             0
#define MME_ARCH_A_BASE_ADDR_LOW_V_MASK                              0xFFFFFFFF

/* MME_ARCH_B_BASE_ADDR_LOW */
#define MME_ARCH_B_BASE_ADDR_LOW_V_SHIFT                             0
#define MME_ARCH_B_BASE_ADDR_LOW_V_MASK                              0xFFFFFFFF

/* MME_ARCH_CIN_BASE_ADDR_LOW */
#define MME_ARCH_CIN_BASE_ADDR_LOW_V_SHIFT                           0
#define MME_ARCH_CIN_BASE_ADDR_LOW_V_MASK                            0xFFFFFFFF

/* MME_ARCH_COUT_BASE_ADDR_LOW */
#define MME_ARCH_COUT_BASE_ADDR_LOW_V_SHIFT                          0
#define MME_ARCH_COUT_BASE_ADDR_LOW_V_MASK                           0xFFFFFFFF

/* MME_ARCH_BIAS_BASE_ADDR_LOW */
#define MME_ARCH_BIAS_BASE_ADDR_LOW_V_SHIFT                          0
#define MME_ARCH_BIAS_BASE_ADDR_LOW_V_MASK                           0xFFFFFFFF

/* MME_ARCH_HEADER */
#define MME_ARCH_HEADER_SIGNAL_MASK_SHIFT                            0
#define MME_ARCH_HEADER_SIGNAL_MASK_MASK                             0x1F
#define MME_ARCH_HEADER_SIGNAL_EN_SHIFT                              5
#define MME_ARCH_HEADER_SIGNAL_EN_MASK                               0x20
#define MME_ARCH_HEADER_TRANS_A_SHIFT                                6
#define MME_ARCH_HEADER_TRANS_A_MASK                                 0x40
#define MME_ARCH_HEADER_LOWER_A_SHIFT                                7
#define MME_ARCH_HEADER_LOWER_A_MASK                                 0x80
#define MME_ARCH_HEADER_ACCUM_MASK_SHIFT                             8
#define MME_ARCH_HEADER_ACCUM_MASK_MASK                              0xF00
#define MME_ARCH_HEADER_LOAD_BIAS_SHIFT                              12
#define MME_ARCH_HEADER_LOAD_BIAS_MASK                               0x1000
#define MME_ARCH_HEADER_LOAD_CIN_SHIFT                               13
#define MME_ARCH_HEADER_LOAD_CIN_MASK                                0x2000
#define MME_ARCH_HEADER_STORE_OUT_SHIFT                              15
#define MME_ARCH_HEADER_STORE_OUT_MASK                               0x8000
#define MME_ARCH_HEADER_ACC_LD_INC_DISABLE_SHIFT                     16
#define MME_ARCH_HEADER_ACC_LD_INC_DISABLE_MASK                      0x10000
#define MME_ARCH_HEADER_ADVANCE_A_SHIFT                              17
#define MME_ARCH_HEADER_ADVANCE_A_MASK                               0x20000
#define MME_ARCH_HEADER_ADVANCE_B_SHIFT                              18
#define MME_ARCH_HEADER_ADVANCE_B_MASK                               0x40000
#define MME_ARCH_HEADER_ADVANCE_CIN_SHIFT                            19
#define MME_ARCH_HEADER_ADVANCE_CIN_MASK                             0x80000
#define MME_ARCH_HEADER_ADVANCE_COUT_SHIFT                           20
#define MME_ARCH_HEADER_ADVANCE_COUT_MASK                            0x100000
#define MME_ARCH_HEADER_COMPRESSED_B_SHIFT                           21
#define MME_ARCH_HEADER_COMPRESSED_B_MASK                            0x200000
#define MME_ARCH_HEADER_MASK_CONV_END_SHIFT                          22
#define MME_ARCH_HEADER_MASK_CONV_END_MASK                           0x400000
#define MME_ARCH_HEADER_ACC_ST_INC_DISABLE_SHIFT                     23
#define MME_ARCH_HEADER_ACC_ST_INC_DISABLE_MASK                      0x800000
#define MME_ARCH_HEADER_AB_DATA_TYPE_SHIFT                           24
#define MME_ARCH_HEADER_AB_DATA_TYPE_MASK                            0x3000000
#define MME_ARCH_HEADER_CIN_DATA_TYPE_SHIFT                          26
#define MME_ARCH_HEADER_CIN_DATA_TYPE_MASK                           0x1C000000
#define MME_ARCH_HEADER_COUT_DATA_TYPE_SHIFT                         29
#define MME_ARCH_HEADER_COUT_DATA_TYPE_MASK                          0xE0000000

/* MME_ARCH_KERNEL_SIZE_MINUS_1 */
#define MME_ARCH_KERNEL_SIZE_MINUS_1_DIM_0_SHIFT                     0
#define MME_ARCH_KERNEL_SIZE_MINUS_1_DIM_0_MASK                      0xFF
#define MME_ARCH_KERNEL_SIZE_MINUS_1_DIM_1_SHIFT                     8
#define MME_ARCH_KERNEL_SIZE_MINUS_1_DIM_1_MASK                      0xFF00
#define MME_ARCH_KERNEL_SIZE_MINUS_1_DIM_2_SHIFT                     16
#define MME_ARCH_KERNEL_SIZE_MINUS_1_DIM_2_MASK                      0xFF0000
#define MME_ARCH_KERNEL_SIZE_MINUS_1_DIM_3_SHIFT                     24
#define MME_ARCH_KERNEL_SIZE_MINUS_1_DIM_3_MASK                      0xFF000000

/* MME_ARCH_ASSOCIATED_DIMS */
#define MME_ARCH_ASSOCIATED_DIMS_A_0_SHIFT                           0
#define MME_ARCH_ASSOCIATED_DIMS_A_0_MASK                            0x7
#define MME_ARCH_ASSOCIATED_DIMS_B_0_SHIFT                           3
#define MME_ARCH_ASSOCIATED_DIMS_B_0_MASK                            0x38
#define MME_ARCH_ASSOCIATED_DIMS_CIN_0_SHIFT                         6
#define MME_ARCH_ASSOCIATED_DIMS_CIN_0_MASK                          0x1C0
#define MME_ARCH_ASSOCIATED_DIMS_COUT_0_SHIFT                        9
#define MME_ARCH_ASSOCIATED_DIMS_COUT_0_MASK                         0xE00
#define MME_ARCH_ASSOCIATED_DIMS_A_1_SHIFT                           16
#define MME_ARCH_ASSOCIATED_DIMS_A_1_MASK                            0x70000
#define MME_ARCH_ASSOCIATED_DIMS_B_1_SHIFT                           19
#define MME_ARCH_ASSOCIATED_DIMS_B_1_MASK                            0x380000
#define MME_ARCH_ASSOCIATED_DIMS_CIN_1_SHIFT                         22
#define MME_ARCH_ASSOCIATED_DIMS_CIN_1_MASK                          0x1C00000
#define MME_ARCH_ASSOCIATED_DIMS_COUT_1_SHIFT                        25
#define MME_ARCH_ASSOCIATED_DIMS_COUT_1_MASK                         0xE000000

/* MME_ARCH_COUT_SCALE */
#define MME_ARCH_COUT_SCALE_V_SHIFT                                  0
#define MME_ARCH_COUT_SCALE_V_MASK                                   0xFFFFFFFF

/* MME_ARCH_CIN_SCALE */
#define MME_ARCH_CIN_SCALE_V_SHIFT                                   0
#define MME_ARCH_CIN_SCALE_V_MASK                                    0xFFFFFFFF

/* MME_ARCH_GEMMLOWP_ZP */
#define MME_ARCH_GEMMLOWP_ZP_ZP_CIN_SHIFT                            0
#define MME_ARCH_GEMMLOWP_ZP_ZP_CIN_MASK                             0x1FF
#define MME_ARCH_GEMMLOWP_ZP_ZP_COUT_SHIFT                           9
#define MME_ARCH_GEMMLOWP_ZP_ZP_COUT_MASK                            0x3FE00
#define MME_ARCH_GEMMLOWP_ZP_ZP_B_SHIFT                              18
#define MME_ARCH_GEMMLOWP_ZP_ZP_B_MASK                               0x7FC0000
#define MME_ARCH_GEMMLOWP_ZP_GEMMLOWP_EU_EN_SHIFT                    27
#define MME_ARCH_GEMMLOWP_ZP_GEMMLOWP_EU_EN_MASK                     0x8000000
#define MME_ARCH_GEMMLOWP_ZP_ACCUM_SHIFT                             28
#define MME_ARCH_GEMMLOWP_ZP_ACCUM_MASK                              0x10000000
#define MME_ARCH_GEMMLOWP_ZP_ACCUM_BIAS_SHIFT                        29
#define MME_ARCH_GEMMLOWP_ZP_ACCUM_BIAS_MASK                         0x20000000
#define MME_ARCH_GEMMLOWP_ZP_RELU_EN_SHIFT                           30
#define MME_ARCH_GEMMLOWP_ZP_RELU_EN_MASK                            0x40000000

/* MME_ARCH_GEMMLOWP_EXPONENT */
#define MME_ARCH_GEMMLOWP_EXPONENT_EXPONENT_CIN_SHIFT                0
#define MME_ARCH_GEMMLOWP_EXPONENT_EXPONENT_CIN_MASK                 0x3F
#define MME_ARCH_GEMMLOWP_EXPONENT_EXPONENT_COUT_SHIFT               8
#define MME_ARCH_GEMMLOWP_EXPONENT_EXPONENT_COUT_MASK                0x3F00
#define MME_ARCH_GEMMLOWP_EXPONENT_MUL_CIN_EN_SHIFT                  16
#define MME_ARCH_GEMMLOWP_EXPONENT_MUL_CIN_EN_MASK                   0x10000
#define MME_ARCH_GEMMLOWP_EXPONENT_MUL_COUT_EN_SHIFT                 17
#define MME_ARCH_GEMMLOWP_EXPONENT_MUL_COUT_EN_MASK                  0x20000

/* MME_ARCH_A_ROI_BASE_OFFSET */
#define MME_ARCH_A_ROI_BASE_OFFSET_V_SHIFT                           0
#define MME_ARCH_A_ROI_BASE_OFFSET_V_MASK                            0xFFFFFFFF

/* MME_ARCH_A_VALID_ELEMENTS */
#define MME_ARCH_A_VALID_ELEMENTS_V_SHIFT                            0
#define MME_ARCH_A_VALID_ELEMENTS_V_MASK                             0xFFFFFFFF

/* MME_ARCH_A_LOOP_STRIDE */
#define MME_ARCH_A_LOOP_STRIDE_V_SHIFT                               0
#define MME_ARCH_A_LOOP_STRIDE_V_MASK                                0xFFFFFFFF

/* MME_ARCH_A_ROI_SIZE */
#define MME_ARCH_A_ROI_SIZE_V_SHIFT                                  0
#define MME_ARCH_A_ROI_SIZE_V_MASK                                   0xFFFFFFFF

/* MME_ARCH_A_SPATIAL_START_OFFSET */
#define MME_ARCH_A_SPATIAL_START_OFFSET_V_SHIFT                      0
#define MME_ARCH_A_SPATIAL_START_OFFSET_V_MASK                       0xFFFFFFFF

/* MME_ARCH_A_SPATIAL_STRIDE */
#define MME_ARCH_A_SPATIAL_STRIDE_V_SHIFT                            0
#define MME_ARCH_A_SPATIAL_STRIDE_V_MASK                             0xFFFFFFFF

/* MME_ARCH_A_SPATIAL_SIZE_MINUS_1 */
#define MME_ARCH_A_SPATIAL_SIZE_MINUS_1_V_SHIFT                      0
#define MME_ARCH_A_SPATIAL_SIZE_MINUS_1_V_MASK                       0xFFFFFFFF

/* MME_ARCH_B_ROI_BASE_OFFSET */
#define MME_ARCH_B_ROI_BASE_OFFSET_V_SHIFT                           0
#define MME_ARCH_B_ROI_BASE_OFFSET_V_MASK                            0xFFFFFFFF

/* MME_ARCH_B_VALID_ELEMENTS */
#define MME_ARCH_B_VALID_ELEMENTS_V_SHIFT                            0
#define MME_ARCH_B_VALID_ELEMENTS_V_MASK                             0xFFFFFFFF

/* MME_ARCH_B_LOOP_STRIDE */
#define MME_ARCH_B_LOOP_STRIDE_V_SHIFT                               0
#define MME_ARCH_B_LOOP_STRIDE_V_MASK                                0xFFFFFFFF

/* MME_ARCH_B_ROI_SIZE */
#define MME_ARCH_B_ROI_SIZE_V_SHIFT                                  0
#define MME_ARCH_B_ROI_SIZE_V_MASK                                   0xFFFFFFFF

/* MME_ARCH_B_SPATIAL_START_OFFSET */
#define MME_ARCH_B_SPATIAL_START_OFFSET_V_SHIFT                      0
#define MME_ARCH_B_SPATIAL_START_OFFSET_V_MASK                       0xFFFFFFFF

/* MME_ARCH_B_SPATIAL_STRIDE */
#define MME_ARCH_B_SPATIAL_STRIDE_V_SHIFT                            0
#define MME_ARCH_B_SPATIAL_STRIDE_V_MASK                             0xFFFFFFFF

/* MME_ARCH_B_SPATIAL_SIZE_MINUS_1 */
#define MME_ARCH_B_SPATIAL_SIZE_MINUS_1_V_SHIFT                      0
#define MME_ARCH_B_SPATIAL_SIZE_MINUS_1_V_MASK                       0xFFFFFFFF

/* MME_ARCH_C_ROI_BASE_OFFSET */
#define MME_ARCH_C_ROI_BASE_OFFSET_V_SHIFT                           0
#define MME_ARCH_C_ROI_BASE_OFFSET_V_MASK                            0xFFFFFFFF

/* MME_ARCH_C_VALID_ELEMENTS */
#define MME_ARCH_C_VALID_ELEMENTS_V_SHIFT                            0
#define MME_ARCH_C_VALID_ELEMENTS_V_MASK                             0xFFFFFFFF

/* MME_ARCH_C_LOOP_STRIDE */
#define MME_ARCH_C_LOOP_STRIDE_V_SHIFT                               0
#define MME_ARCH_C_LOOP_STRIDE_V_MASK                                0xFFFFFFFF

/* MME_ARCH_C_ROI_SIZE */
#define MME_ARCH_C_ROI_SIZE_V_SHIFT                                  0
#define MME_ARCH_C_ROI_SIZE_V_MASK                                   0xFFFFFFFF

/* MME_ARCH_C_SPATIAL_START_OFFSET */
#define MME_ARCH_C_SPATIAL_START_OFFSET_V_SHIFT                      0
#define MME_ARCH_C_SPATIAL_START_OFFSET_V_MASK                       0xFFFFFFFF

/* MME_ARCH_C_SPATIAL_STRIDE */
#define MME_ARCH_C_SPATIAL_STRIDE_V_SHIFT                            0
#define MME_ARCH_C_SPATIAL_STRIDE_V_MASK                             0xFFFFFFFF

/* MME_ARCH_C_SPATIAL_SIZE_MINUS_1 */
#define MME_ARCH_C_SPATIAL_SIZE_MINUS_1_V_SHIFT                      0
#define MME_ARCH_C_SPATIAL_SIZE_MINUS_1_V_MASK                       0xFFFFFFFF

/* MME_ARCH_SYNC_OBJECT_MESSAGE */
#define MME_ARCH_SYNC_OBJECT_MESSAGE_SO_WRITE_VALUE_SHIFT            0
#define MME_ARCH_SYNC_OBJECT_MESSAGE_SO_WRITE_VALUE_MASK             0xFFFF
#define MME_ARCH_SYNC_OBJECT_MESSAGE_SO_ADDRESS_OFFSET_SHIFT         16
#define MME_ARCH_SYNC_OBJECT_MESSAGE_SO_ADDRESS_OFFSET_MASK          0x7FFF0000
#define MME_ARCH_SYNC_OBJECT_MESSAGE_SO_OPERATION_SHIFT              31
#define MME_ARCH_SYNC_OBJECT_MESSAGE_SO_OPERATION_MASK               0x80000000

/* MME_ARCH_E_PADDING_VALUE_A */
#define MME_ARCH_E_PADDING_VALUE_A_V_SHIFT                           0
#define MME_ARCH_E_PADDING_VALUE_A_V_MASK                            0xFFFF

/* MME_ARCH_E_NUM_ITERATION_MINUS_1 */
#define MME_ARCH_E_NUM_ITERATION_MINUS_1_V_SHIFT                     0
#define MME_ARCH_E_NUM_ITERATION_MINUS_1_V_MASK                      0xFFFFFFFF

/* MME_ARCH_E_BUBBLES_PER_SPLIT */
#define MME_ARCH_E_BUBBLES_PER_SPLIT_A_SHIFT                         0
#define MME_ARCH_E_BUBBLES_PER_SPLIT_A_MASK                          0xFF
#define MME_ARCH_E_BUBBLES_PER_SPLIT_B_SHIFT                         8
#define MME_ARCH_E_BUBBLES_PER_SPLIT_B_MASK                          0xFF00
#define MME_ARCH_E_BUBBLES_PER_SPLIT_CIN_SHIFT                       16
#define MME_ARCH_E_BUBBLES_PER_SPLIT_CIN_MASK                        0xFF0000
#define MME_ARCH_E_BUBBLES_PER_SPLIT_ID_SHIFT                        24
#define MME_ARCH_E_BUBBLES_PER_SPLIT_ID_MASK                         0xFF000000

/* MME_CMD */
#define MME_CMD_EXECUTE_SHIFT                                        0
#define MME_CMD_EXECUTE_MASK                                         0x1

/* MME_DUMMY */
#define MME_DUMMY_V_SHIFT                                            0
#define MME_DUMMY_V_MASK                                             0xFFFFFFFF

/* MME_RESET */
#define MME_RESET_V_SHIFT                                            0
#define MME_RESET_V_MASK                                             0x1

/* MME_STALL */
#define MME_STALL_V_SHIFT                                            0
#define MME_STALL_V_MASK                                             0xFFFFFFFF

/* MME_SM_BASE_ADDRESS_LOW */
#define MME_SM_BASE_ADDRESS_LOW_V_SHIFT                              0
#define MME_SM_BASE_ADDRESS_LOW_V_MASK                               0xFFFFFFFF

/* MME_SM_BASE_ADDRESS_HIGH */
#define MME_SM_BASE_ADDRESS_HIGH_V_SHIFT                             0
#define MME_SM_BASE_ADDRESS_HIGH_V_MASK                              0xFFFFFFFF

/* MME_DBGMEM_ADD */
#define MME_DBGMEM_ADD_V_SHIFT                                       0
#define MME_DBGMEM_ADD_V_MASK                                        0xFFFFFFFF

/* MME_DBGMEM_DATA_WR */
#define MME_DBGMEM_DATA_WR_V_SHIFT                                   0
#define MME_DBGMEM_DATA_WR_V_MASK                                    0xFFFFFFFF

/* MME_DBGMEM_DATA_RD */
#define MME_DBGMEM_DATA_RD_V_SHIFT                                   0
#define MME_DBGMEM_DATA_RD_V_MASK                                    0xFFFFFFFF

/* MME_DBGMEM_CTRL */
#define MME_DBGMEM_CTRL_WR_NRD_SHIFT                                 0
#define MME_DBGMEM_CTRL_WR_NRD_MASK                                  0x1

/* MME_DBGMEM_RC */
#define MME_DBGMEM_RC_VALID_SHIFT                                    0
#define MME_DBGMEM_RC_VALID_MASK                                     0x1
#define MME_DBGMEM_RC_FULL_SHIFT                                     1
#define MME_DBGMEM_RC_FULL_MASK                                      0x2

/* MME_LOG_SHADOW */
#define MME_LOG_SHADOW_MASK_0_SHIFT                                  0
#define MME_LOG_SHADOW_MASK_0_MASK                                   0x7F
#define MME_LOG_SHADOW_MASK_1_SHIFT                                  8
#define MME_LOG_SHADOW_MASK_1_MASK                                   0x7F00
#define MME_LOG_SHADOW_MASK_2_SHIFT                                  16
#define MME_LOG_SHADOW_MASK_2_MASK                                   0x7F0000
#define MME_LOG_SHADOW_MASK_3_SHIFT                                  24
#define MME_LOG_SHADOW_MASK_3_MASK                                   0x7F000000

/* MME_STORE_MAX_CREDIT */
#define MME_STORE_MAX_CREDIT_V_SHIFT                                 0
#define MME_STORE_MAX_CREDIT_V_MASK                                  0x3F

/* MME_AGU */
#define MME_AGU_SBA_MAX_CREDIT_SHIFT                                 0
#define MME_AGU_SBA_MAX_CREDIT_MASK                                  0x1F
#define MME_AGU_SBB_MAX_CREDIT_SHIFT                                 8
#define MME_AGU_SBB_MAX_CREDIT_MASK                                  0x1F00
#define MME_AGU_SBC_MAX_CREDIT_SHIFT                                 16
#define MME_AGU_SBC_MAX_CREDIT_MASK                                  0x1F0000
#define MME_AGU_WBC_MAX_CREDIT_SHIFT                                 24
#define MME_AGU_WBC_MAX_CREDIT_MASK                                  0x3F000000

/* MME_SBA */
#define MME_SBA_MAX_SIZE_SHIFT                                       0
#define MME_SBA_MAX_SIZE_MASK                                        0x3FF
#define MME_SBA_EU_MAX_CREDIT_SHIFT                                  16
#define MME_SBA_EU_MAX_CREDIT_MASK                                   0x1F0000

/* MME_SBB */
#define MME_SBB_MAX_SIZE_SHIFT                                       0
#define MME_SBB_MAX_SIZE_MASK                                        0x3FF
#define MME_SBB_EU_MAX_CREDIT_SHIFT                                  16
#define MME_SBB_EU_MAX_CREDIT_MASK                                   0x1F0000

/* MME_SBC */
#define MME_SBC_MAX_SIZE_SHIFT                                       0
#define MME_SBC_MAX_SIZE_MASK                                        0x3FF
#define MME_SBC_EU_MAX_CREDIT_SHIFT                                  16
#define MME_SBC_EU_MAX_CREDIT_MASK                                   0x1F0000

/* MME_WBC */
#define MME_WBC_MAX_OUTSTANDING_SHIFT                                0
#define MME_WBC_MAX_OUTSTANDING_MASK                                 0xFFF
#define MME_WBC_DISABLE_FAST_END_PE_SHIFT                            12
#define MME_WBC_DISABLE_FAST_END_PE_MASK                             0x1000
#define MME_WBC_LD_INSERT_BUBBLE_DIS_SHIFT                           13
#define MME_WBC_LD_INSERT_BUBBLE_DIS_MASK                            0x2000

/* MME_SBA_CONTROL_DATA */
#define MME_SBA_CONTROL_DATA_ASID_SHIFT                              0
#define MME_SBA_CONTROL_DATA_ASID_MASK                               0x3FF
#define MME_SBA_CONTROL_DATA_MMBP_SHIFT                              10
#define MME_SBA_CONTROL_DATA_MMBP_MASK                               0x400

/* MME_SBB_CONTROL_DATA */
#define MME_SBB_CONTROL_DATA_ASID_SHIFT                              0
#define MME_SBB_CONTROL_DATA_ASID_MASK                               0x3FF
#define MME_SBB_CONTROL_DATA_MMBP_SHIFT                              10
#define MME_SBB_CONTROL_DATA_MMBP_MASK                               0x400

/* MME_SBC_CONTROL_DATA */
#define MME_SBC_CONTROL_DATA_ASID_SHIFT                              0
#define MME_SBC_CONTROL_DATA_ASID_MASK                               0x3FF
#define MME_SBC_CONTROL_DATA_MMBP_SHIFT                              10
#define MME_SBC_CONTROL_DATA_MMBP_MASK                               0x400

/* MME_WBC_CONTROL_DATA */
#define MME_WBC_CONTROL_DATA_ASID_SHIFT                              0
#define MME_WBC_CONTROL_DATA_ASID_MASK                               0x3FF
#define MME_WBC_CONTROL_DATA_MMBP_SHIFT                              10
#define MME_WBC_CONTROL_DATA_MMBP_MASK                               0x400

/* MME_TE */
#define MME_TE_MAX_CREDIT_SHIFT                                      0
#define MME_TE_MAX_CREDIT_MASK                                       0x1F
#define MME_TE_DESC_MAX_CREDIT_SHIFT                                 8
#define MME_TE_DESC_MAX_CREDIT_MASK                                  0x1F00

/* MME_TE2DEC */
#define MME_TE2DEC_MAX_CREDIT_SHIFT                                  0
#define MME_TE2DEC_MAX_CREDIT_MASK                                   0x1F

/* MME_REI_STATUS */
#define MME_REI_STATUS_V_SHIFT                                       0
#define MME_REI_STATUS_V_MASK                                        0xFFFFFFFF

/* MME_REI_MASK */
#define MME_REI_MASK_V_SHIFT                                         0
#define MME_REI_MASK_V_MASK                                          0xFFFFFFFF

/* MME_SEI_STATUS */
#define MME_SEI_STATUS_V_SHIFT                                       0
#define MME_SEI_STATUS_V_MASK                                        0xFFFFFFFF

/* MME_SEI_MASK */
#define MME_SEI_MASK_V_SHIFT                                         0
#define MME_SEI_MASK_V_MASK                                          0xFFFFFFFF

/* MME_SPI_STATUS */
#define MME_SPI_STATUS_V_SHIFT                                       0
#define MME_SPI_STATUS_V_MASK                                        0xFFFFFFFF

/* MME_SPI_MASK */
#define MME_SPI_MASK_V_SHIFT                                         0
#define MME_SPI_MASK_V_MASK                                          0xFFFFFFFF

/* MME_SHADOW_0_STATUS */
#define MME_SHADOW_0_STATUS_A_SHIFT                                  0
#define MME_SHADOW_0_STATUS_A_MASK                                   0x1
#define MME_SHADOW_0_STATUS_B_SHIFT                                  1
#define MME_SHADOW_0_STATUS_B_MASK                                   0x2
#define MME_SHADOW_0_STATUS_CIN_SHIFT                                2
#define MME_SHADOW_0_STATUS_CIN_MASK                                 0x4
#define MME_SHADOW_0_STATUS_COUT_SHIFT                               3
#define MME_SHADOW_0_STATUS_COUT_MASK                                0x8
#define MME_SHADOW_0_STATUS_TE_SHIFT                                 4
#define MME_SHADOW_0_STATUS_TE_MASK                                  0x10
#define MME_SHADOW_0_STATUS_LD_SHIFT                                 5
#define MME_SHADOW_0_STATUS_LD_MASK                                  0x20
#define MME_SHADOW_0_STATUS_ST_SHIFT                                 6
#define MME_SHADOW_0_STATUS_ST_MASK                                  0x40

/* MME_SHADOW_0_A_BASE_ADDR_HIGH */
#define MME_SHADOW_0_A_BASE_ADDR_HIGH_V_SHIFT                        0
#define MME_SHADOW_0_A_BASE_ADDR_HIGH_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_0_B_BASE_ADDR_HIGH */
#define MME_SHADOW_0_B_BASE_ADDR_HIGH_V_SHIFT                        0
#define MME_SHADOW_0_B_BASE_ADDR_HIGH_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_0_CIN_BASE_ADDR_HIGH */
#define MME_SHADOW_0_CIN_BASE_ADDR_HIGH_V_SHIFT                      0
#define MME_SHADOW_0_CIN_BASE_ADDR_HIGH_V_MASK                       0xFFFFFFFF

/* MME_SHADOW_0_COUT_BASE_ADDR_HIGH */
#define MME_SHADOW_0_COUT_BASE_ADDR_HIGH_V_SHIFT                     0
#define MME_SHADOW_0_COUT_BASE_ADDR_HIGH_V_MASK                      0xFFFFFFFF

/* MME_SHADOW_0_BIAS_BASE_ADDR_HIGH */
#define MME_SHADOW_0_BIAS_BASE_ADDR_HIGH_V_SHIFT                     0
#define MME_SHADOW_0_BIAS_BASE_ADDR_HIGH_V_MASK                      0xFFFFFFFF

/* MME_SHADOW_0_A_BASE_ADDR_LOW */
#define MME_SHADOW_0_A_BASE_ADDR_LOW_V_SHIFT                         0
#define MME_SHADOW_0_A_BASE_ADDR_LOW_V_MASK                          0xFFFFFFFF

/* MME_SHADOW_0_B_BASE_ADDR_LOW */
#define MME_SHADOW_0_B_BASE_ADDR_LOW_V_SHIFT                         0
#define MME_SHADOW_0_B_BASE_ADDR_LOW_V_MASK                          0xFFFFFFFF

/* MME_SHADOW_0_CIN_BASE_ADDR_LOW */
#define MME_SHADOW_0_CIN_BASE_ADDR_LOW_V_SHIFT                       0
#define MME_SHADOW_0_CIN_BASE_ADDR_LOW_V_MASK                        0xFFFFFFFF

/* MME_SHADOW_0_COUT_BASE_ADDR_LOW */
#define MME_SHADOW_0_COUT_BASE_ADDR_LOW_V_SHIFT                      0
#define MME_SHADOW_0_COUT_BASE_ADDR_LOW_V_MASK                       0xFFFFFFFF

/* MME_SHADOW_0_BIAS_BASE_ADDR_LOW */
#define MME_SHADOW_0_BIAS_BASE_ADDR_LOW_V_SHIFT                      0
#define MME_SHADOW_0_BIAS_BASE_ADDR_LOW_V_MASK                       0xFFFFFFFF

/* MME_SHADOW_0_HEADER */
#define MME_SHADOW_0_HEADER_SIGNAL_MASK_SHIFT                        0
#define MME_SHADOW_0_HEADER_SIGNAL_MASK_MASK                         0x1F
#define MME_SHADOW_0_HEADER_SIGNAL_EN_SHIFT                          5
#define MME_SHADOW_0_HEADER_SIGNAL_EN_MASK                           0x20
#define MME_SHADOW_0_HEADER_TRANS_A_SHIFT                            6
#define MME_SHADOW_0_HEADER_TRANS_A_MASK                             0x40
#define MME_SHADOW_0_HEADER_LOWER_A_SHIFT                            7
#define MME_SHADOW_0_HEADER_LOWER_A_MASK                             0x80
#define MME_SHADOW_0_HEADER_ACCUM_MASK_SHIFT                         8
#define MME_SHADOW_0_HEADER_ACCUM_MASK_MASK                          0xF00
#define MME_SHADOW_0_HEADER_LOAD_BIAS_SHIFT                          12
#define MME_SHADOW_0_HEADER_LOAD_BIAS_MASK                           0x1000
#define MME_SHADOW_0_HEADER_LOAD_CIN_SHIFT                           13
#define MME_SHADOW_0_HEADER_LOAD_CIN_MASK                            0x2000
#define MME_SHADOW_0_HEADER_STORE_OUT_SHIFT                          15
#define MME_SHADOW_0_HEADER_STORE_OUT_MASK                           0x8000
#define MME_SHADOW_0_HEADER_ACC_LD_INC_DISABLE_SHIFT                 16
#define MME_SHADOW_0_HEADER_ACC_LD_INC_DISABLE_MASK                  0x10000
#define MME_SHADOW_0_HEADER_ADVANCE_A_SHIFT                          17
#define MME_SHADOW_0_HEADER_ADVANCE_A_MASK                           0x20000
#define MME_SHADOW_0_HEADER_ADVANCE_B_SHIFT                          18
#define MME_SHADOW_0_HEADER_ADVANCE_B_MASK                           0x40000
#define MME_SHADOW_0_HEADER_ADVANCE_CIN_SHIFT                        19
#define MME_SHADOW_0_HEADER_ADVANCE_CIN_MASK                         0x80000
#define MME_SHADOW_0_HEADER_ADVANCE_COUT_SHIFT                       20
#define MME_SHADOW_0_HEADER_ADVANCE_COUT_MASK                        0x100000
#define MME_SHADOW_0_HEADER_COMPRESSED_B_SHIFT                       21
#define MME_SHADOW_0_HEADER_COMPRESSED_B_MASK                        0x200000
#define MME_SHADOW_0_HEADER_MASK_CONV_END_SHIFT                      22
#define MME_SHADOW_0_HEADER_MASK_CONV_END_MASK                       0x400000
#define MME_SHADOW_0_HEADER_ACC_ST_INC_DISABLE_SHIFT                 23
#define MME_SHADOW_0_HEADER_ACC_ST_INC_DISABLE_MASK                  0x800000
#define MME_SHADOW_0_HEADER_AB_DATA_TYPE_SHIFT                       24
#define MME_SHADOW_0_HEADER_AB_DATA_TYPE_MASK                        0x3000000
#define MME_SHADOW_0_HEADER_CIN_DATA_TYPE_SHIFT                      26
#define MME_SHADOW_0_HEADER_CIN_DATA_TYPE_MASK                       0x1C000000
#define MME_SHADOW_0_HEADER_COUT_DATA_TYPE_SHIFT                     29
#define MME_SHADOW_0_HEADER_COUT_DATA_TYPE_MASK                      0xE0000000

/* MME_SHADOW_0_KERNEL_SIZE_MINUS_1 */
#define MME_SHADOW_0_KERNEL_SIZE_MINUS_1_DIM_0_SHIFT                 0
#define MME_SHADOW_0_KERNEL_SIZE_MINUS_1_DIM_0_MASK                  0xFF
#define MME_SHADOW_0_KERNEL_SIZE_MINUS_1_DIM_1_SHIFT                 8
#define MME_SHADOW_0_KERNEL_SIZE_MINUS_1_DIM_1_MASK                  0xFF00
#define MME_SHADOW_0_KERNEL_SIZE_MINUS_1_DIM_2_SHIFT                 16
#define MME_SHADOW_0_KERNEL_SIZE_MINUS_1_DIM_2_MASK                  0xFF0000
#define MME_SHADOW_0_KERNEL_SIZE_MINUS_1_DIM_3_SHIFT                 24
#define MME_SHADOW_0_KERNEL_SIZE_MINUS_1_DIM_3_MASK                  0xFF000000

/* MME_SHADOW_0_ASSOCIATED_DIMS */
#define MME_SHADOW_0_ASSOCIATED_DIMS_A_0_SHIFT                       0
#define MME_SHADOW_0_ASSOCIATED_DIMS_A_0_MASK                        0x7
#define MME_SHADOW_0_ASSOCIATED_DIMS_B_0_SHIFT                       3
#define MME_SHADOW_0_ASSOCIATED_DIMS_B_0_MASK                        0x38
#define MME_SHADOW_0_ASSOCIATED_DIMS_CIN_0_SHIFT                     6
#define MME_SHADOW_0_ASSOCIATED_DIMS_CIN_0_MASK                      0x1C0
#define MME_SHADOW_0_ASSOCIATED_DIMS_COUT_0_SHIFT                    9
#define MME_SHADOW_0_ASSOCIATED_DIMS_COUT_0_MASK                     0xE00
#define MME_SHADOW_0_ASSOCIATED_DIMS_A_1_SHIFT                       16
#define MME_SHADOW_0_ASSOCIATED_DIMS_A_1_MASK                        0x70000
#define MME_SHADOW_0_ASSOCIATED_DIMS_B_1_SHIFT                       19
#define MME_SHADOW_0_ASSOCIATED_DIMS_B_1_MASK                        0x380000
#define MME_SHADOW_0_ASSOCIATED_DIMS_CIN_1_SHIFT                     22
#define MME_SHADOW_0_ASSOCIATED_DIMS_CIN_1_MASK                      0x1C00000
#define MME_SHADOW_0_ASSOCIATED_DIMS_COUT_1_SHIFT                    25
#define MME_SHADOW_0_ASSOCIATED_DIMS_COUT_1_MASK                     0xE000000

/* MME_SHADOW_0_COUT_SCALE */
#define MME_SHADOW_0_COUT_SCALE_V_SHIFT                              0
#define MME_SHADOW_0_COUT_SCALE_V_MASK                               0xFFFFFFFF

/* MME_SHADOW_0_CIN_SCALE */
#define MME_SHADOW_0_CIN_SCALE_V_SHIFT                               0
#define MME_SHADOW_0_CIN_SCALE_V_MASK                                0xFFFFFFFF

/* MME_SHADOW_0_GEMMLOWP_ZP */
#define MME_SHADOW_0_GEMMLOWP_ZP_ZP_CIN_SHIFT                        0
#define MME_SHADOW_0_GEMMLOWP_ZP_ZP_CIN_MASK                         0x1FF
#define MME_SHADOW_0_GEMMLOWP_ZP_ZP_COUT_SHIFT                       9
#define MME_SHADOW_0_GEMMLOWP_ZP_ZP_COUT_MASK                        0x3FE00
#define MME_SHADOW_0_GEMMLOWP_ZP_ZP_B_SHIFT                          18
#define MME_SHADOW_0_GEMMLOWP_ZP_ZP_B_MASK                           0x7FC0000
#define MME_SHADOW_0_GEMMLOWP_ZP_GEMMLOWP_EU_EN_SHIFT                27
#define MME_SHADOW_0_GEMMLOWP_ZP_GEMMLOWP_EU_EN_MASK                 0x8000000
#define MME_SHADOW_0_GEMMLOWP_ZP_ACCUM_SHIFT                         28
#define MME_SHADOW_0_GEMMLOWP_ZP_ACCUM_MASK                          0x10000000
#define MME_SHADOW_0_GEMMLOWP_ZP_ACCUM_BIAS_SHIFT                    29
#define MME_SHADOW_0_GEMMLOWP_ZP_ACCUM_BIAS_MASK                     0x20000000
#define MME_SHADOW_0_GEMMLOWP_ZP_RELU_EN_SHIFT                       30
#define MME_SHADOW_0_GEMMLOWP_ZP_RELU_EN_MASK                        0x40000000

/* MME_SHADOW_0_GEMMLOWP_EXPONENT */
#define MME_SHADOW_0_GEMMLOWP_EXPONENT_EXPONENT_CIN_SHIFT            0
#define MME_SHADOW_0_GEMMLOWP_EXPONENT_EXPONENT_CIN_MASK             0x3F
#define MME_SHADOW_0_GEMMLOWP_EXPONENT_EXPONENT_COUT_SHIFT           8
#define MME_SHADOW_0_GEMMLOWP_EXPONENT_EXPONENT_COUT_MASK            0x3F00
#define MME_SHADOW_0_GEMMLOWP_EXPONENT_MUL_CIN_EN_SHIFT              16
#define MME_SHADOW_0_GEMMLOWP_EXPONENT_MUL_CIN_EN_MASK               0x10000
#define MME_SHADOW_0_GEMMLOWP_EXPONENT_MUL_COUT_EN_SHIFT             17
#define MME_SHADOW_0_GEMMLOWP_EXPONENT_MUL_COUT_EN_MASK              0x20000

/* MME_SHADOW_0_A_ROI_BASE_OFFSET */
#define MME_SHADOW_0_A_ROI_BASE_OFFSET_V_SHIFT                       0
#define MME_SHADOW_0_A_ROI_BASE_OFFSET_V_MASK                        0xFFFFFFFF

/* MME_SHADOW_0_A_VALID_ELEMENTS */
#define MME_SHADOW_0_A_VALID_ELEMENTS_V_SHIFT                        0
#define MME_SHADOW_0_A_VALID_ELEMENTS_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_0_A_LOOP_STRIDE */
#define MME_SHADOW_0_A_LOOP_STRIDE_V_SHIFT                           0
#define MME_SHADOW_0_A_LOOP_STRIDE_V_MASK                            0xFFFFFFFF

/* MME_SHADOW_0_A_ROI_SIZE */
#define MME_SHADOW_0_A_ROI_SIZE_V_SHIFT                              0
#define MME_SHADOW_0_A_ROI_SIZE_V_MASK                               0xFFFFFFFF

/* MME_SHADOW_0_A_SPATIAL_START_OFFSET */
#define MME_SHADOW_0_A_SPATIAL_START_OFFSET_V_SHIFT                  0
#define MME_SHADOW_0_A_SPATIAL_START_OFFSET_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_0_A_SPATIAL_STRIDE */
#define MME_SHADOW_0_A_SPATIAL_STRIDE_V_SHIFT                        0
#define MME_SHADOW_0_A_SPATIAL_STRIDE_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_0_A_SPATIAL_SIZE_MINUS_1 */
#define MME_SHADOW_0_A_SPATIAL_SIZE_MINUS_1_V_SHIFT                  0
#define MME_SHADOW_0_A_SPATIAL_SIZE_MINUS_1_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_0_B_ROI_BASE_OFFSET */
#define MME_SHADOW_0_B_ROI_BASE_OFFSET_V_SHIFT                       0
#define MME_SHADOW_0_B_ROI_BASE_OFFSET_V_MASK                        0xFFFFFFFF

/* MME_SHADOW_0_B_VALID_ELEMENTS */
#define MME_SHADOW_0_B_VALID_ELEMENTS_V_SHIFT                        0
#define MME_SHADOW_0_B_VALID_ELEMENTS_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_0_B_LOOP_STRIDE */
#define MME_SHADOW_0_B_LOOP_STRIDE_V_SHIFT                           0
#define MME_SHADOW_0_B_LOOP_STRIDE_V_MASK                            0xFFFFFFFF

/* MME_SHADOW_0_B_ROI_SIZE */
#define MME_SHADOW_0_B_ROI_SIZE_V_SHIFT                              0
#define MME_SHADOW_0_B_ROI_SIZE_V_MASK                               0xFFFFFFFF

/* MME_SHADOW_0_B_SPATIAL_START_OFFSET */
#define MME_SHADOW_0_B_SPATIAL_START_OFFSET_V_SHIFT                  0
#define MME_SHADOW_0_B_SPATIAL_START_OFFSET_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_0_B_SPATIAL_STRIDE */
#define MME_SHADOW_0_B_SPATIAL_STRIDE_V_SHIFT                        0
#define MME_SHADOW_0_B_SPATIAL_STRIDE_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_0_B_SPATIAL_SIZE_MINUS_1 */
#define MME_SHADOW_0_B_SPATIAL_SIZE_MINUS_1_V_SHIFT                  0
#define MME_SHADOW_0_B_SPATIAL_SIZE_MINUS_1_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_0_C_ROI_BASE_OFFSET */
#define MME_SHADOW_0_C_ROI_BASE_OFFSET_V_SHIFT                       0
#define MME_SHADOW_0_C_ROI_BASE_OFFSET_V_MASK                        0xFFFFFFFF

/* MME_SHADOW_0_C_VALID_ELEMENTS */
#define MME_SHADOW_0_C_VALID_ELEMENTS_V_SHIFT                        0
#define MME_SHADOW_0_C_VALID_ELEMENTS_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_0_C_LOOP_STRIDE */
#define MME_SHADOW_0_C_LOOP_STRIDE_V_SHIFT                           0
#define MME_SHADOW_0_C_LOOP_STRIDE_V_MASK                            0xFFFFFFFF

/* MME_SHADOW_0_C_ROI_SIZE */
#define MME_SHADOW_0_C_ROI_SIZE_V_SHIFT                              0
#define MME_SHADOW_0_C_ROI_SIZE_V_MASK                               0xFFFFFFFF

/* MME_SHADOW_0_C_SPATIAL_START_OFFSET */
#define MME_SHADOW_0_C_SPATIAL_START_OFFSET_V_SHIFT                  0
#define MME_SHADOW_0_C_SPATIAL_START_OFFSET_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_0_C_SPATIAL_STRIDE */
#define MME_SHADOW_0_C_SPATIAL_STRIDE_V_SHIFT                        0
#define MME_SHADOW_0_C_SPATIAL_STRIDE_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_0_C_SPATIAL_SIZE_MINUS_1 */
#define MME_SHADOW_0_C_SPATIAL_SIZE_MINUS_1_V_SHIFT                  0
#define MME_SHADOW_0_C_SPATIAL_SIZE_MINUS_1_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_0_SYNC_OBJECT_MESSAGE */
#define MME_SHADOW_0_SYNC_OBJECT_MESSAGE_SO_WRITE_VALUE_SHIFT        0
#define MME_SHADOW_0_SYNC_OBJECT_MESSAGE_SO_WRITE_VALUE_MASK         0xFFFF
#define MME_SHADOW_0_SYNC_OBJECT_MESSAGE_SO_ADDRESS_OFFSET_SHIFT     16
#define MME_SHADOW_0_SYNC_OBJECT_MESSAGE_SO_ADDRESS_OFFSET_MASK      0x7FFF0000
#define MME_SHADOW_0_SYNC_OBJECT_MESSAGE_SO_OPERATION_SHIFT          31
#define MME_SHADOW_0_SYNC_OBJECT_MESSAGE_SO_OPERATION_MASK           0x80000000

/* MME_SHADOW_0_E_PADDING_VALUE_A */
#define MME_SHADOW_0_E_PADDING_VALUE_A_V_SHIFT                       0
#define MME_SHADOW_0_E_PADDING_VALUE_A_V_MASK                        0xFFFF

/* MME_SHADOW_0_E_NUM_ITERATION_MINUS_1 */
#define MME_SHADOW_0_E_NUM_ITERATION_MINUS_1_V_SHIFT                 0
#define MME_SHADOW_0_E_NUM_ITERATION_MINUS_1_V_MASK                  0xFFFFFFFF

/* MME_SHADOW_0_E_BUBBLES_PER_SPLIT */
#define MME_SHADOW_0_E_BUBBLES_PER_SPLIT_A_SHIFT                     0
#define MME_SHADOW_0_E_BUBBLES_PER_SPLIT_A_MASK                      0xFF
#define MME_SHADOW_0_E_BUBBLES_PER_SPLIT_B_SHIFT                     8
#define MME_SHADOW_0_E_BUBBLES_PER_SPLIT_B_MASK                      0xFF00
#define MME_SHADOW_0_E_BUBBLES_PER_SPLIT_CIN_SHIFT                   16
#define MME_SHADOW_0_E_BUBBLES_PER_SPLIT_CIN_MASK                    0xFF0000
#define MME_SHADOW_0_E_BUBBLES_PER_SPLIT_ID_SHIFT                    24
#define MME_SHADOW_0_E_BUBBLES_PER_SPLIT_ID_MASK                     0xFF000000

/* MME_SHADOW_1_STATUS */
#define MME_SHADOW_1_STATUS_A_SHIFT                                  0
#define MME_SHADOW_1_STATUS_A_MASK                                   0x1
#define MME_SHADOW_1_STATUS_B_SHIFT                                  1
#define MME_SHADOW_1_STATUS_B_MASK                                   0x2
#define MME_SHADOW_1_STATUS_CIN_SHIFT                                2
#define MME_SHADOW_1_STATUS_CIN_MASK                                 0x4
#define MME_SHADOW_1_STATUS_COUT_SHIFT                               3
#define MME_SHADOW_1_STATUS_COUT_MASK                                0x8
#define MME_SHADOW_1_STATUS_TE_SHIFT                                 4
#define MME_SHADOW_1_STATUS_TE_MASK                                  0x10
#define MME_SHADOW_1_STATUS_LD_SHIFT                                 5
#define MME_SHADOW_1_STATUS_LD_MASK                                  0x20
#define MME_SHADOW_1_STATUS_ST_SHIFT                                 6
#define MME_SHADOW_1_STATUS_ST_MASK                                  0x40

/* MME_SHADOW_1_A_BASE_ADDR_HIGH */
#define MME_SHADOW_1_A_BASE_ADDR_HIGH_V_SHIFT                        0
#define MME_SHADOW_1_A_BASE_ADDR_HIGH_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_1_B_BASE_ADDR_HIGH */
#define MME_SHADOW_1_B_BASE_ADDR_HIGH_V_SHIFT                        0
#define MME_SHADOW_1_B_BASE_ADDR_HIGH_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_1_CIN_BASE_ADDR_HIGH */
#define MME_SHADOW_1_CIN_BASE_ADDR_HIGH_V_SHIFT                      0
#define MME_SHADOW_1_CIN_BASE_ADDR_HIGH_V_MASK                       0xFFFFFFFF

/* MME_SHADOW_1_COUT_BASE_ADDR_HIGH */
#define MME_SHADOW_1_COUT_BASE_ADDR_HIGH_V_SHIFT                     0
#define MME_SHADOW_1_COUT_BASE_ADDR_HIGH_V_MASK                      0xFFFFFFFF

/* MME_SHADOW_1_BIAS_BASE_ADDR_HIGH */
#define MME_SHADOW_1_BIAS_BASE_ADDR_HIGH_V_SHIFT                     0
#define MME_SHADOW_1_BIAS_BASE_ADDR_HIGH_V_MASK                      0xFFFFFFFF

/* MME_SHADOW_1_A_BASE_ADDR_LOW */
#define MME_SHADOW_1_A_BASE_ADDR_LOW_V_SHIFT                         0
#define MME_SHADOW_1_A_BASE_ADDR_LOW_V_MASK                          0xFFFFFFFF

/* MME_SHADOW_1_B_BASE_ADDR_LOW */
#define MME_SHADOW_1_B_BASE_ADDR_LOW_V_SHIFT                         0
#define MME_SHADOW_1_B_BASE_ADDR_LOW_V_MASK                          0xFFFFFFFF

/* MME_SHADOW_1_CIN_BASE_ADDR_LOW */
#define MME_SHADOW_1_CIN_BASE_ADDR_LOW_V_SHIFT                       0
#define MME_SHADOW_1_CIN_BASE_ADDR_LOW_V_MASK                        0xFFFFFFFF

/* MME_SHADOW_1_COUT_BASE_ADDR_LOW */
#define MME_SHADOW_1_COUT_BASE_ADDR_LOW_V_SHIFT                      0
#define MME_SHADOW_1_COUT_BASE_ADDR_LOW_V_MASK                       0xFFFFFFFF

/* MME_SHADOW_1_BIAS_BASE_ADDR_LOW */
#define MME_SHADOW_1_BIAS_BASE_ADDR_LOW_V_SHIFT                      0
#define MME_SHADOW_1_BIAS_BASE_ADDR_LOW_V_MASK                       0xFFFFFFFF

/* MME_SHADOW_1_HEADER */
#define MME_SHADOW_1_HEADER_SIGNAL_MASK_SHIFT                        0
#define MME_SHADOW_1_HEADER_SIGNAL_MASK_MASK                         0x1F
#define MME_SHADOW_1_HEADER_SIGNAL_EN_SHIFT                          5
#define MME_SHADOW_1_HEADER_SIGNAL_EN_MASK                           0x20
#define MME_SHADOW_1_HEADER_TRANS_A_SHIFT                            6
#define MME_SHADOW_1_HEADER_TRANS_A_MASK                             0x40
#define MME_SHADOW_1_HEADER_LOWER_A_SHIFT                            7
#define MME_SHADOW_1_HEADER_LOWER_A_MASK                             0x80
#define MME_SHADOW_1_HEADER_ACCUM_MASK_SHIFT                         8
#define MME_SHADOW_1_HEADER_ACCUM_MASK_MASK                          0xF00
#define MME_SHADOW_1_HEADER_LOAD_BIAS_SHIFT                          12
#define MME_SHADOW_1_HEADER_LOAD_BIAS_MASK                           0x1000
#define MME_SHADOW_1_HEADER_LOAD_CIN_SHIFT                           13
#define MME_SHADOW_1_HEADER_LOAD_CIN_MASK                            0x2000
#define MME_SHADOW_1_HEADER_STORE_OUT_SHIFT                          15
#define MME_SHADOW_1_HEADER_STORE_OUT_MASK                           0x8000
#define MME_SHADOW_1_HEADER_ACC_LD_INC_DISABLE_SHIFT                 16
#define MME_SHADOW_1_HEADER_ACC_LD_INC_DISABLE_MASK                  0x10000
#define MME_SHADOW_1_HEADER_ADVANCE_A_SHIFT                          17
#define MME_SHADOW_1_HEADER_ADVANCE_A_MASK                           0x20000
#define MME_SHADOW_1_HEADER_ADVANCE_B_SHIFT                          18
#define MME_SHADOW_1_HEADER_ADVANCE_B_MASK                           0x40000
#define MME_SHADOW_1_HEADER_ADVANCE_CIN_SHIFT                        19
#define MME_SHADOW_1_HEADER_ADVANCE_CIN_MASK                         0x80000
#define MME_SHADOW_1_HEADER_ADVANCE_COUT_SHIFT                       20
#define MME_SHADOW_1_HEADER_ADVANCE_COUT_MASK                        0x100000
#define MME_SHADOW_1_HEADER_COMPRESSED_B_SHIFT                       21
#define MME_SHADOW_1_HEADER_COMPRESSED_B_MASK                        0x200000
#define MME_SHADOW_1_HEADER_MASK_CONV_END_SHIFT                      22
#define MME_SHADOW_1_HEADER_MASK_CONV_END_MASK                       0x400000
#define MME_SHADOW_1_HEADER_ACC_ST_INC_DISABLE_SHIFT                 23
#define MME_SHADOW_1_HEADER_ACC_ST_INC_DISABLE_MASK                  0x800000
#define MME_SHADOW_1_HEADER_AB_DATA_TYPE_SHIFT                       24
#define MME_SHADOW_1_HEADER_AB_DATA_TYPE_MASK                        0x3000000
#define MME_SHADOW_1_HEADER_CIN_DATA_TYPE_SHIFT                      26
#define MME_SHADOW_1_HEADER_CIN_DATA_TYPE_MASK                       0x1C000000
#define MME_SHADOW_1_HEADER_COUT_DATA_TYPE_SHIFT                     29
#define MME_SHADOW_1_HEADER_COUT_DATA_TYPE_MASK                      0xE0000000

/* MME_SHADOW_1_KERNEL_SIZE_MINUS_1 */
#define MME_SHADOW_1_KERNEL_SIZE_MINUS_1_DIM_0_SHIFT                 0
#define MME_SHADOW_1_KERNEL_SIZE_MINUS_1_DIM_0_MASK                  0xFF
#define MME_SHADOW_1_KERNEL_SIZE_MINUS_1_DIM_1_SHIFT                 8
#define MME_SHADOW_1_KERNEL_SIZE_MINUS_1_DIM_1_MASK                  0xFF00
#define MME_SHADOW_1_KERNEL_SIZE_MINUS_1_DIM_2_SHIFT                 16
#define MME_SHADOW_1_KERNEL_SIZE_MINUS_1_DIM_2_MASK                  0xFF0000
#define MME_SHADOW_1_KERNEL_SIZE_MINUS_1_DIM_3_SHIFT                 24
#define MME_SHADOW_1_KERNEL_SIZE_MINUS_1_DIM_3_MASK                  0xFF000000

/* MME_SHADOW_1_ASSOCIATED_DIMS */
#define MME_SHADOW_1_ASSOCIATED_DIMS_A_0_SHIFT                       0
#define MME_SHADOW_1_ASSOCIATED_DIMS_A_0_MASK                        0x7
#define MME_SHADOW_1_ASSOCIATED_DIMS_B_0_SHIFT                       3
#define MME_SHADOW_1_ASSOCIATED_DIMS_B_0_MASK                        0x38
#define MME_SHADOW_1_ASSOCIATED_DIMS_CIN_0_SHIFT                     6
#define MME_SHADOW_1_ASSOCIATED_DIMS_CIN_0_MASK                      0x1C0
#define MME_SHADOW_1_ASSOCIATED_DIMS_COUT_0_SHIFT                    9
#define MME_SHADOW_1_ASSOCIATED_DIMS_COUT_0_MASK                     0xE00
#define MME_SHADOW_1_ASSOCIATED_DIMS_A_1_SHIFT                       16
#define MME_SHADOW_1_ASSOCIATED_DIMS_A_1_MASK                        0x70000
#define MME_SHADOW_1_ASSOCIATED_DIMS_B_1_SHIFT                       19
#define MME_SHADOW_1_ASSOCIATED_DIMS_B_1_MASK                        0x380000
#define MME_SHADOW_1_ASSOCIATED_DIMS_CIN_1_SHIFT                     22
#define MME_SHADOW_1_ASSOCIATED_DIMS_CIN_1_MASK                      0x1C00000
#define MME_SHADOW_1_ASSOCIATED_DIMS_COUT_1_SHIFT                    25
#define MME_SHADOW_1_ASSOCIATED_DIMS_COUT_1_MASK                     0xE000000

/* MME_SHADOW_1_COUT_SCALE */
#define MME_SHADOW_1_COUT_SCALE_V_SHIFT                              0
#define MME_SHADOW_1_COUT_SCALE_V_MASK                               0xFFFFFFFF

/* MME_SHADOW_1_CIN_SCALE */
#define MME_SHADOW_1_CIN_SCALE_V_SHIFT                               0
#define MME_SHADOW_1_CIN_SCALE_V_MASK                                0xFFFFFFFF

/* MME_SHADOW_1_GEMMLOWP_ZP */
#define MME_SHADOW_1_GEMMLOWP_ZP_ZP_CIN_SHIFT                        0
#define MME_SHADOW_1_GEMMLOWP_ZP_ZP_CIN_MASK                         0x1FF
#define MME_SHADOW_1_GEMMLOWP_ZP_ZP_COUT_SHIFT                       9
#define MME_SHADOW_1_GEMMLOWP_ZP_ZP_COUT_MASK                        0x3FE00
#define MME_SHADOW_1_GEMMLOWP_ZP_ZP_B_SHIFT                          18
#define MME_SHADOW_1_GEMMLOWP_ZP_ZP_B_MASK                           0x7FC0000
#define MME_SHADOW_1_GEMMLOWP_ZP_GEMMLOWP_EU_EN_SHIFT                27
#define MME_SHADOW_1_GEMMLOWP_ZP_GEMMLOWP_EU_EN_MASK                 0x8000000
#define MME_SHADOW_1_GEMMLOWP_ZP_ACCUM_SHIFT                         28
#define MME_SHADOW_1_GEMMLOWP_ZP_ACCUM_MASK                          0x10000000
#define MME_SHADOW_1_GEMMLOWP_ZP_ACCUM_BIAS_SHIFT                    29
#define MME_SHADOW_1_GEMMLOWP_ZP_ACCUM_BIAS_MASK                     0x20000000
#define MME_SHADOW_1_GEMMLOWP_ZP_RELU_EN_SHIFT                       30
#define MME_SHADOW_1_GEMMLOWP_ZP_RELU_EN_MASK                        0x40000000

/* MME_SHADOW_1_GEMMLOWP_EXPONENT */
#define MME_SHADOW_1_GEMMLOWP_EXPONENT_EXPONENT_CIN_SHIFT            0
#define MME_SHADOW_1_GEMMLOWP_EXPONENT_EXPONENT_CIN_MASK             0x3F
#define MME_SHADOW_1_GEMMLOWP_EXPONENT_EXPONENT_COUT_SHIFT           8
#define MME_SHADOW_1_GEMMLOWP_EXPONENT_EXPONENT_COUT_MASK            0x3F00
#define MME_SHADOW_1_GEMMLOWP_EXPONENT_MUL_CIN_EN_SHIFT              16
#define MME_SHADOW_1_GEMMLOWP_EXPONENT_MUL_CIN_EN_MASK               0x10000
#define MME_SHADOW_1_GEMMLOWP_EXPONENT_MUL_COUT_EN_SHIFT             17
#define MME_SHADOW_1_GEMMLOWP_EXPONENT_MUL_COUT_EN_MASK              0x20000

/* MME_SHADOW_1_A_ROI_BASE_OFFSET */
#define MME_SHADOW_1_A_ROI_BASE_OFFSET_V_SHIFT                       0
#define MME_SHADOW_1_A_ROI_BASE_OFFSET_V_MASK                        0xFFFFFFFF

/* MME_SHADOW_1_A_VALID_ELEMENTS */
#define MME_SHADOW_1_A_VALID_ELEMENTS_V_SHIFT                        0
#define MME_SHADOW_1_A_VALID_ELEMENTS_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_1_A_LOOP_STRIDE */
#define MME_SHADOW_1_A_LOOP_STRIDE_V_SHIFT                           0
#define MME_SHADOW_1_A_LOOP_STRIDE_V_MASK                            0xFFFFFFFF

/* MME_SHADOW_1_A_ROI_SIZE */
#define MME_SHADOW_1_A_ROI_SIZE_V_SHIFT                              0
#define MME_SHADOW_1_A_ROI_SIZE_V_MASK                               0xFFFFFFFF

/* MME_SHADOW_1_A_SPATIAL_START_OFFSET */
#define MME_SHADOW_1_A_SPATIAL_START_OFFSET_V_SHIFT                  0
#define MME_SHADOW_1_A_SPATIAL_START_OFFSET_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_1_A_SPATIAL_STRIDE */
#define MME_SHADOW_1_A_SPATIAL_STRIDE_V_SHIFT                        0
#define MME_SHADOW_1_A_SPATIAL_STRIDE_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_1_A_SPATIAL_SIZE_MINUS_1 */
#define MME_SHADOW_1_A_SPATIAL_SIZE_MINUS_1_V_SHIFT                  0
#define MME_SHADOW_1_A_SPATIAL_SIZE_MINUS_1_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_1_B_ROI_BASE_OFFSET */
#define MME_SHADOW_1_B_ROI_BASE_OFFSET_V_SHIFT                       0
#define MME_SHADOW_1_B_ROI_BASE_OFFSET_V_MASK                        0xFFFFFFFF

/* MME_SHADOW_1_B_VALID_ELEMENTS */
#define MME_SHADOW_1_B_VALID_ELEMENTS_V_SHIFT                        0
#define MME_SHADOW_1_B_VALID_ELEMENTS_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_1_B_LOOP_STRIDE */
#define MME_SHADOW_1_B_LOOP_STRIDE_V_SHIFT                           0
#define MME_SHADOW_1_B_LOOP_STRIDE_V_MASK                            0xFFFFFFFF

/* MME_SHADOW_1_B_ROI_SIZE */
#define MME_SHADOW_1_B_ROI_SIZE_V_SHIFT                              0
#define MME_SHADOW_1_B_ROI_SIZE_V_MASK                               0xFFFFFFFF

/* MME_SHADOW_1_B_SPATIAL_START_OFFSET */
#define MME_SHADOW_1_B_SPATIAL_START_OFFSET_V_SHIFT                  0
#define MME_SHADOW_1_B_SPATIAL_START_OFFSET_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_1_B_SPATIAL_STRIDE */
#define MME_SHADOW_1_B_SPATIAL_STRIDE_V_SHIFT                        0
#define MME_SHADOW_1_B_SPATIAL_STRIDE_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_1_B_SPATIAL_SIZE_MINUS_1 */
#define MME_SHADOW_1_B_SPATIAL_SIZE_MINUS_1_V_SHIFT                  0
#define MME_SHADOW_1_B_SPATIAL_SIZE_MINUS_1_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_1_C_ROI_BASE_OFFSET */
#define MME_SHADOW_1_C_ROI_BASE_OFFSET_V_SHIFT                       0
#define MME_SHADOW_1_C_ROI_BASE_OFFSET_V_MASK                        0xFFFFFFFF

/* MME_SHADOW_1_C_VALID_ELEMENTS */
#define MME_SHADOW_1_C_VALID_ELEMENTS_V_SHIFT                        0
#define MME_SHADOW_1_C_VALID_ELEMENTS_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_1_C_LOOP_STRIDE */
#define MME_SHADOW_1_C_LOOP_STRIDE_V_SHIFT                           0
#define MME_SHADOW_1_C_LOOP_STRIDE_V_MASK                            0xFFFFFFFF

/* MME_SHADOW_1_C_ROI_SIZE */
#define MME_SHADOW_1_C_ROI_SIZE_V_SHIFT                              0
#define MME_SHADOW_1_C_ROI_SIZE_V_MASK                               0xFFFFFFFF

/* MME_SHADOW_1_C_SPATIAL_START_OFFSET */
#define MME_SHADOW_1_C_SPATIAL_START_OFFSET_V_SHIFT                  0
#define MME_SHADOW_1_C_SPATIAL_START_OFFSET_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_1_C_SPATIAL_STRIDE */
#define MME_SHADOW_1_C_SPATIAL_STRIDE_V_SHIFT                        0
#define MME_SHADOW_1_C_SPATIAL_STRIDE_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_1_C_SPATIAL_SIZE_MINUS_1 */
#define MME_SHADOW_1_C_SPATIAL_SIZE_MINUS_1_V_SHIFT                  0
#define MME_SHADOW_1_C_SPATIAL_SIZE_MINUS_1_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_1_SYNC_OBJECT_MESSAGE */
#define MME_SHADOW_1_SYNC_OBJECT_MESSAGE_SO_WRITE_VALUE_SHIFT        0
#define MME_SHADOW_1_SYNC_OBJECT_MESSAGE_SO_WRITE_VALUE_MASK         0xFFFF
#define MME_SHADOW_1_SYNC_OBJECT_MESSAGE_SO_ADDRESS_OFFSET_SHIFT     16
#define MME_SHADOW_1_SYNC_OBJECT_MESSAGE_SO_ADDRESS_OFFSET_MASK      0x7FFF0000
#define MME_SHADOW_1_SYNC_OBJECT_MESSAGE_SO_OPERATION_SHIFT          31
#define MME_SHADOW_1_SYNC_OBJECT_MESSAGE_SO_OPERATION_MASK           0x80000000

/* MME_SHADOW_1_E_PADDING_VALUE_A */
#define MME_SHADOW_1_E_PADDING_VALUE_A_V_SHIFT                       0
#define MME_SHADOW_1_E_PADDING_VALUE_A_V_MASK                        0xFFFF

/* MME_SHADOW_1_E_NUM_ITERATION_MINUS_1 */
#define MME_SHADOW_1_E_NUM_ITERATION_MINUS_1_V_SHIFT                 0
#define MME_SHADOW_1_E_NUM_ITERATION_MINUS_1_V_MASK                  0xFFFFFFFF

/* MME_SHADOW_1_E_BUBBLES_PER_SPLIT */
#define MME_SHADOW_1_E_BUBBLES_PER_SPLIT_A_SHIFT                     0
#define MME_SHADOW_1_E_BUBBLES_PER_SPLIT_A_MASK                      0xFF
#define MME_SHADOW_1_E_BUBBLES_PER_SPLIT_B_SHIFT                     8
#define MME_SHADOW_1_E_BUBBLES_PER_SPLIT_B_MASK                      0xFF00
#define MME_SHADOW_1_E_BUBBLES_PER_SPLIT_CIN_SHIFT                   16
#define MME_SHADOW_1_E_BUBBLES_PER_SPLIT_CIN_MASK                    0xFF0000
#define MME_SHADOW_1_E_BUBBLES_PER_SPLIT_ID_SHIFT                    24
#define MME_SHADOW_1_E_BUBBLES_PER_SPLIT_ID_MASK                     0xFF000000

/* MME_SHADOW_2_STATUS */
#define MME_SHADOW_2_STATUS_A_SHIFT                                  0
#define MME_SHADOW_2_STATUS_A_MASK                                   0x1
#define MME_SHADOW_2_STATUS_B_SHIFT                                  1
#define MME_SHADOW_2_STATUS_B_MASK                                   0x2
#define MME_SHADOW_2_STATUS_CIN_SHIFT                                2
#define MME_SHADOW_2_STATUS_CIN_MASK                                 0x4
#define MME_SHADOW_2_STATUS_COUT_SHIFT                               3
#define MME_SHADOW_2_STATUS_COUT_MASK                                0x8
#define MME_SHADOW_2_STATUS_TE_SHIFT                                 4
#define MME_SHADOW_2_STATUS_TE_MASK                                  0x10
#define MME_SHADOW_2_STATUS_LD_SHIFT                                 5
#define MME_SHADOW_2_STATUS_LD_MASK                                  0x20
#define MME_SHADOW_2_STATUS_ST_SHIFT                                 6
#define MME_SHADOW_2_STATUS_ST_MASK                                  0x40

/* MME_SHADOW_2_A_BASE_ADDR_HIGH */
#define MME_SHADOW_2_A_BASE_ADDR_HIGH_V_SHIFT                        0
#define MME_SHADOW_2_A_BASE_ADDR_HIGH_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_2_B_BASE_ADDR_HIGH */
#define MME_SHADOW_2_B_BASE_ADDR_HIGH_V_SHIFT                        0
#define MME_SHADOW_2_B_BASE_ADDR_HIGH_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_2_CIN_BASE_ADDR_HIGH */
#define MME_SHADOW_2_CIN_BASE_ADDR_HIGH_V_SHIFT                      0
#define MME_SHADOW_2_CIN_BASE_ADDR_HIGH_V_MASK                       0xFFFFFFFF

/* MME_SHADOW_2_COUT_BASE_ADDR_HIGH */
#define MME_SHADOW_2_COUT_BASE_ADDR_HIGH_V_SHIFT                     0
#define MME_SHADOW_2_COUT_BASE_ADDR_HIGH_V_MASK                      0xFFFFFFFF

/* MME_SHADOW_2_BIAS_BASE_ADDR_HIGH */
#define MME_SHADOW_2_BIAS_BASE_ADDR_HIGH_V_SHIFT                     0
#define MME_SHADOW_2_BIAS_BASE_ADDR_HIGH_V_MASK                      0xFFFFFFFF

/* MME_SHADOW_2_A_BASE_ADDR_LOW */
#define MME_SHADOW_2_A_BASE_ADDR_LOW_V_SHIFT                         0
#define MME_SHADOW_2_A_BASE_ADDR_LOW_V_MASK                          0xFFFFFFFF

/* MME_SHADOW_2_B_BASE_ADDR_LOW */
#define MME_SHADOW_2_B_BASE_ADDR_LOW_V_SHIFT                         0
#define MME_SHADOW_2_B_BASE_ADDR_LOW_V_MASK                          0xFFFFFFFF

/* MME_SHADOW_2_CIN_BASE_ADDR_LOW */
#define MME_SHADOW_2_CIN_BASE_ADDR_LOW_V_SHIFT                       0
#define MME_SHADOW_2_CIN_BASE_ADDR_LOW_V_MASK                        0xFFFFFFFF

/* MME_SHADOW_2_COUT_BASE_ADDR_LOW */
#define MME_SHADOW_2_COUT_BASE_ADDR_LOW_V_SHIFT                      0
#define MME_SHADOW_2_COUT_BASE_ADDR_LOW_V_MASK                       0xFFFFFFFF

/* MME_SHADOW_2_BIAS_BASE_ADDR_LOW */
#define MME_SHADOW_2_BIAS_BASE_ADDR_LOW_V_SHIFT                      0
#define MME_SHADOW_2_BIAS_BASE_ADDR_LOW_V_MASK                       0xFFFFFFFF

/* MME_SHADOW_2_HEADER */
#define MME_SHADOW_2_HEADER_SIGNAL_MASK_SHIFT                        0
#define MME_SHADOW_2_HEADER_SIGNAL_MASK_MASK                         0x1F
#define MME_SHADOW_2_HEADER_SIGNAL_EN_SHIFT                          5
#define MME_SHADOW_2_HEADER_SIGNAL_EN_MASK                           0x20
#define MME_SHADOW_2_HEADER_TRANS_A_SHIFT                            6
#define MME_SHADOW_2_HEADER_TRANS_A_MASK                             0x40
#define MME_SHADOW_2_HEADER_LOWER_A_SHIFT                            7
#define MME_SHADOW_2_HEADER_LOWER_A_MASK                             0x80
#define MME_SHADOW_2_HEADER_ACCUM_MASK_SHIFT                         8
#define MME_SHADOW_2_HEADER_ACCUM_MASK_MASK                          0xF00
#define MME_SHADOW_2_HEADER_LOAD_BIAS_SHIFT                          12
#define MME_SHADOW_2_HEADER_LOAD_BIAS_MASK                           0x1000
#define MME_SHADOW_2_HEADER_LOAD_CIN_SHIFT                           13
#define MME_SHADOW_2_HEADER_LOAD_CIN_MASK                            0x2000
#define MME_SHADOW_2_HEADER_STORE_OUT_SHIFT                          15
#define MME_SHADOW_2_HEADER_STORE_OUT_MASK                           0x8000
#define MME_SHADOW_2_HEADER_ACC_LD_INC_DISABLE_SHIFT                 16
#define MME_SHADOW_2_HEADER_ACC_LD_INC_DISABLE_MASK                  0x10000
#define MME_SHADOW_2_HEADER_ADVANCE_A_SHIFT                          17
#define MME_SHADOW_2_HEADER_ADVANCE_A_MASK                           0x20000
#define MME_SHADOW_2_HEADER_ADVANCE_B_SHIFT                          18
#define MME_SHADOW_2_HEADER_ADVANCE_B_MASK                           0x40000
#define MME_SHADOW_2_HEADER_ADVANCE_CIN_SHIFT                        19
#define MME_SHADOW_2_HEADER_ADVANCE_CIN_MASK                         0x80000
#define MME_SHADOW_2_HEADER_ADVANCE_COUT_SHIFT                       20
#define MME_SHADOW_2_HEADER_ADVANCE_COUT_MASK                        0x100000
#define MME_SHADOW_2_HEADER_COMPRESSED_B_SHIFT                       21
#define MME_SHADOW_2_HEADER_COMPRESSED_B_MASK                        0x200000
#define MME_SHADOW_2_HEADER_MASK_CONV_END_SHIFT                      22
#define MME_SHADOW_2_HEADER_MASK_CONV_END_MASK                       0x400000
#define MME_SHADOW_2_HEADER_ACC_ST_INC_DISABLE_SHIFT                 23
#define MME_SHADOW_2_HEADER_ACC_ST_INC_DISABLE_MASK                  0x800000
#define MME_SHADOW_2_HEADER_AB_DATA_TYPE_SHIFT                       24
#define MME_SHADOW_2_HEADER_AB_DATA_TYPE_MASK                        0x3000000
#define MME_SHADOW_2_HEADER_CIN_DATA_TYPE_SHIFT                      26
#define MME_SHADOW_2_HEADER_CIN_DATA_TYPE_MASK                       0x1C000000
#define MME_SHADOW_2_HEADER_COUT_DATA_TYPE_SHIFT                     29
#define MME_SHADOW_2_HEADER_COUT_DATA_TYPE_MASK                      0xE0000000

/* MME_SHADOW_2_KERNEL_SIZE_MINUS_1 */
#define MME_SHADOW_2_KERNEL_SIZE_MINUS_1_DIM_0_SHIFT                 0
#define MME_SHADOW_2_KERNEL_SIZE_MINUS_1_DIM_0_MASK                  0xFF
#define MME_SHADOW_2_KERNEL_SIZE_MINUS_1_DIM_1_SHIFT                 8
#define MME_SHADOW_2_KERNEL_SIZE_MINUS_1_DIM_1_MASK                  0xFF00
#define MME_SHADOW_2_KERNEL_SIZE_MINUS_1_DIM_2_SHIFT                 16
#define MME_SHADOW_2_KERNEL_SIZE_MINUS_1_DIM_2_MASK                  0xFF0000
#define MME_SHADOW_2_KERNEL_SIZE_MINUS_1_DIM_3_SHIFT                 24
#define MME_SHADOW_2_KERNEL_SIZE_MINUS_1_DIM_3_MASK                  0xFF000000

/* MME_SHADOW_2_ASSOCIATED_DIMS */
#define MME_SHADOW_2_ASSOCIATED_DIMS_A_0_SHIFT                       0
#define MME_SHADOW_2_ASSOCIATED_DIMS_A_0_MASK                        0x7
#define MME_SHADOW_2_ASSOCIATED_DIMS_B_0_SHIFT                       3
#define MME_SHADOW_2_ASSOCIATED_DIMS_B_0_MASK                        0x38
#define MME_SHADOW_2_ASSOCIATED_DIMS_CIN_0_SHIFT                     6
#define MME_SHADOW_2_ASSOCIATED_DIMS_CIN_0_MASK                      0x1C0
#define MME_SHADOW_2_ASSOCIATED_DIMS_COUT_0_SHIFT                    9
#define MME_SHADOW_2_ASSOCIATED_DIMS_COUT_0_MASK                     0xE00
#define MME_SHADOW_2_ASSOCIATED_DIMS_A_1_SHIFT                       16
#define MME_SHADOW_2_ASSOCIATED_DIMS_A_1_MASK                        0x70000
#define MME_SHADOW_2_ASSOCIATED_DIMS_B_1_SHIFT                       19
#define MME_SHADOW_2_ASSOCIATED_DIMS_B_1_MASK                        0x380000
#define MME_SHADOW_2_ASSOCIATED_DIMS_CIN_1_SHIFT                     22
#define MME_SHADOW_2_ASSOCIATED_DIMS_CIN_1_MASK                      0x1C00000
#define MME_SHADOW_2_ASSOCIATED_DIMS_COUT_1_SHIFT                    25
#define MME_SHADOW_2_ASSOCIATED_DIMS_COUT_1_MASK                     0xE000000

/* MME_SHADOW_2_COUT_SCALE */
#define MME_SHADOW_2_COUT_SCALE_V_SHIFT                              0
#define MME_SHADOW_2_COUT_SCALE_V_MASK                               0xFFFFFFFF

/* MME_SHADOW_2_CIN_SCALE */
#define MME_SHADOW_2_CIN_SCALE_V_SHIFT                               0
#define MME_SHADOW_2_CIN_SCALE_V_MASK                                0xFFFFFFFF

/* MME_SHADOW_2_GEMMLOWP_ZP */
#define MME_SHADOW_2_GEMMLOWP_ZP_ZP_CIN_SHIFT                        0
#define MME_SHADOW_2_GEMMLOWP_ZP_ZP_CIN_MASK                         0x1FF
#define MME_SHADOW_2_GEMMLOWP_ZP_ZP_COUT_SHIFT                       9
#define MME_SHADOW_2_GEMMLOWP_ZP_ZP_COUT_MASK                        0x3FE00
#define MME_SHADOW_2_GEMMLOWP_ZP_ZP_B_SHIFT                          18
#define MME_SHADOW_2_GEMMLOWP_ZP_ZP_B_MASK                           0x7FC0000
#define MME_SHADOW_2_GEMMLOWP_ZP_GEMMLOWP_EU_EN_SHIFT                27
#define MME_SHADOW_2_GEMMLOWP_ZP_GEMMLOWP_EU_EN_MASK                 0x8000000
#define MME_SHADOW_2_GEMMLOWP_ZP_ACCUM_SHIFT                         28
#define MME_SHADOW_2_GEMMLOWP_ZP_ACCUM_MASK                          0x10000000
#define MME_SHADOW_2_GEMMLOWP_ZP_ACCUM_BIAS_SHIFT                    29
#define MME_SHADOW_2_GEMMLOWP_ZP_ACCUM_BIAS_MASK                     0x20000000
#define MME_SHADOW_2_GEMMLOWP_ZP_RELU_EN_SHIFT                       30
#define MME_SHADOW_2_GEMMLOWP_ZP_RELU_EN_MASK                        0x40000000

/* MME_SHADOW_2_GEMMLOWP_EXPONENT */
#define MME_SHADOW_2_GEMMLOWP_EXPONENT_EXPONENT_CIN_SHIFT            0
#define MME_SHADOW_2_GEMMLOWP_EXPONENT_EXPONENT_CIN_MASK             0x3F
#define MME_SHADOW_2_GEMMLOWP_EXPONENT_EXPONENT_COUT_SHIFT           8
#define MME_SHADOW_2_GEMMLOWP_EXPONENT_EXPONENT_COUT_MASK            0x3F00
#define MME_SHADOW_2_GEMMLOWP_EXPONENT_MUL_CIN_EN_SHIFT              16
#define MME_SHADOW_2_GEMMLOWP_EXPONENT_MUL_CIN_EN_MASK               0x10000
#define MME_SHADOW_2_GEMMLOWP_EXPONENT_MUL_COUT_EN_SHIFT             17
#define MME_SHADOW_2_GEMMLOWP_EXPONENT_MUL_COUT_EN_MASK              0x20000

/* MME_SHADOW_2_A_ROI_BASE_OFFSET */
#define MME_SHADOW_2_A_ROI_BASE_OFFSET_V_SHIFT                       0
#define MME_SHADOW_2_A_ROI_BASE_OFFSET_V_MASK                        0xFFFFFFFF

/* MME_SHADOW_2_A_VALID_ELEMENTS */
#define MME_SHADOW_2_A_VALID_ELEMENTS_V_SHIFT                        0
#define MME_SHADOW_2_A_VALID_ELEMENTS_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_2_A_LOOP_STRIDE */
#define MME_SHADOW_2_A_LOOP_STRIDE_V_SHIFT                           0
#define MME_SHADOW_2_A_LOOP_STRIDE_V_MASK                            0xFFFFFFFF

/* MME_SHADOW_2_A_ROI_SIZE */
#define MME_SHADOW_2_A_ROI_SIZE_V_SHIFT                              0
#define MME_SHADOW_2_A_ROI_SIZE_V_MASK                               0xFFFFFFFF

/* MME_SHADOW_2_A_SPATIAL_START_OFFSET */
#define MME_SHADOW_2_A_SPATIAL_START_OFFSET_V_SHIFT                  0
#define MME_SHADOW_2_A_SPATIAL_START_OFFSET_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_2_A_SPATIAL_STRIDE */
#define MME_SHADOW_2_A_SPATIAL_STRIDE_V_SHIFT                        0
#define MME_SHADOW_2_A_SPATIAL_STRIDE_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_2_A_SPATIAL_SIZE_MINUS_1 */
#define MME_SHADOW_2_A_SPATIAL_SIZE_MINUS_1_V_SHIFT                  0
#define MME_SHADOW_2_A_SPATIAL_SIZE_MINUS_1_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_2_B_ROI_BASE_OFFSET */
#define MME_SHADOW_2_B_ROI_BASE_OFFSET_V_SHIFT                       0
#define MME_SHADOW_2_B_ROI_BASE_OFFSET_V_MASK                        0xFFFFFFFF

/* MME_SHADOW_2_B_VALID_ELEMENTS */
#define MME_SHADOW_2_B_VALID_ELEMENTS_V_SHIFT                        0
#define MME_SHADOW_2_B_VALID_ELEMENTS_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_2_B_LOOP_STRIDE */
#define MME_SHADOW_2_B_LOOP_STRIDE_V_SHIFT                           0
#define MME_SHADOW_2_B_LOOP_STRIDE_V_MASK                            0xFFFFFFFF

/* MME_SHADOW_2_B_ROI_SIZE */
#define MME_SHADOW_2_B_ROI_SIZE_V_SHIFT                              0
#define MME_SHADOW_2_B_ROI_SIZE_V_MASK                               0xFFFFFFFF

/* MME_SHADOW_2_B_SPATIAL_START_OFFSET */
#define MME_SHADOW_2_B_SPATIAL_START_OFFSET_V_SHIFT                  0
#define MME_SHADOW_2_B_SPATIAL_START_OFFSET_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_2_B_SPATIAL_STRIDE */
#define MME_SHADOW_2_B_SPATIAL_STRIDE_V_SHIFT                        0
#define MME_SHADOW_2_B_SPATIAL_STRIDE_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_2_B_SPATIAL_SIZE_MINUS_1 */
#define MME_SHADOW_2_B_SPATIAL_SIZE_MINUS_1_V_SHIFT                  0
#define MME_SHADOW_2_B_SPATIAL_SIZE_MINUS_1_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_2_C_ROI_BASE_OFFSET */
#define MME_SHADOW_2_C_ROI_BASE_OFFSET_V_SHIFT                       0
#define MME_SHADOW_2_C_ROI_BASE_OFFSET_V_MASK                        0xFFFFFFFF

/* MME_SHADOW_2_C_VALID_ELEMENTS */
#define MME_SHADOW_2_C_VALID_ELEMENTS_V_SHIFT                        0
#define MME_SHADOW_2_C_VALID_ELEMENTS_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_2_C_LOOP_STRIDE */
#define MME_SHADOW_2_C_LOOP_STRIDE_V_SHIFT                           0
#define MME_SHADOW_2_C_LOOP_STRIDE_V_MASK                            0xFFFFFFFF

/* MME_SHADOW_2_C_ROI_SIZE */
#define MME_SHADOW_2_C_ROI_SIZE_V_SHIFT                              0
#define MME_SHADOW_2_C_ROI_SIZE_V_MASK                               0xFFFFFFFF

/* MME_SHADOW_2_C_SPATIAL_START_OFFSET */
#define MME_SHADOW_2_C_SPATIAL_START_OFFSET_V_SHIFT                  0
#define MME_SHADOW_2_C_SPATIAL_START_OFFSET_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_2_C_SPATIAL_STRIDE */
#define MME_SHADOW_2_C_SPATIAL_STRIDE_V_SHIFT                        0
#define MME_SHADOW_2_C_SPATIAL_STRIDE_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_2_C_SPATIAL_SIZE_MINUS_1 */
#define MME_SHADOW_2_C_SPATIAL_SIZE_MINUS_1_V_SHIFT                  0
#define MME_SHADOW_2_C_SPATIAL_SIZE_MINUS_1_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_2_SYNC_OBJECT_MESSAGE */
#define MME_SHADOW_2_SYNC_OBJECT_MESSAGE_SO_WRITE_VALUE_SHIFT        0
#define MME_SHADOW_2_SYNC_OBJECT_MESSAGE_SO_WRITE_VALUE_MASK         0xFFFF
#define MME_SHADOW_2_SYNC_OBJECT_MESSAGE_SO_ADDRESS_OFFSET_SHIFT     16
#define MME_SHADOW_2_SYNC_OBJECT_MESSAGE_SO_ADDRESS_OFFSET_MASK      0x7FFF0000
#define MME_SHADOW_2_SYNC_OBJECT_MESSAGE_SO_OPERATION_SHIFT          31
#define MME_SHADOW_2_SYNC_OBJECT_MESSAGE_SO_OPERATION_MASK           0x80000000

/* MME_SHADOW_2_E_PADDING_VALUE_A */
#define MME_SHADOW_2_E_PADDING_VALUE_A_V_SHIFT                       0
#define MME_SHADOW_2_E_PADDING_VALUE_A_V_MASK                        0xFFFF

/* MME_SHADOW_2_E_NUM_ITERATION_MINUS_1 */
#define MME_SHADOW_2_E_NUM_ITERATION_MINUS_1_V_SHIFT                 0
#define MME_SHADOW_2_E_NUM_ITERATION_MINUS_1_V_MASK                  0xFFFFFFFF

/* MME_SHADOW_2_E_BUBBLES_PER_SPLIT */
#define MME_SHADOW_2_E_BUBBLES_PER_SPLIT_A_SHIFT                     0
#define MME_SHADOW_2_E_BUBBLES_PER_SPLIT_A_MASK                      0xFF
#define MME_SHADOW_2_E_BUBBLES_PER_SPLIT_B_SHIFT                     8
#define MME_SHADOW_2_E_BUBBLES_PER_SPLIT_B_MASK                      0xFF00
#define MME_SHADOW_2_E_BUBBLES_PER_SPLIT_CIN_SHIFT                   16
#define MME_SHADOW_2_E_BUBBLES_PER_SPLIT_CIN_MASK                    0xFF0000
#define MME_SHADOW_2_E_BUBBLES_PER_SPLIT_ID_SHIFT                    24
#define MME_SHADOW_2_E_BUBBLES_PER_SPLIT_ID_MASK                     0xFF000000

/* MME_SHADOW_3_STATUS */
#define MME_SHADOW_3_STATUS_A_SHIFT                                  0
#define MME_SHADOW_3_STATUS_A_MASK                                   0x1
#define MME_SHADOW_3_STATUS_B_SHIFT                                  1
#define MME_SHADOW_3_STATUS_B_MASK                                   0x2
#define MME_SHADOW_3_STATUS_CIN_SHIFT                                2
#define MME_SHADOW_3_STATUS_CIN_MASK                                 0x4
#define MME_SHADOW_3_STATUS_COUT_SHIFT                               3
#define MME_SHADOW_3_STATUS_COUT_MASK                                0x8
#define MME_SHADOW_3_STATUS_TE_SHIFT                                 4
#define MME_SHADOW_3_STATUS_TE_MASK                                  0x10
#define MME_SHADOW_3_STATUS_LD_SHIFT                                 5
#define MME_SHADOW_3_STATUS_LD_MASK                                  0x20
#define MME_SHADOW_3_STATUS_ST_SHIFT                                 6
#define MME_SHADOW_3_STATUS_ST_MASK                                  0x40

/* MME_SHADOW_3_A_BASE_ADDR_HIGH */
#define MME_SHADOW_3_A_BASE_ADDR_HIGH_V_SHIFT                        0
#define MME_SHADOW_3_A_BASE_ADDR_HIGH_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_3_B_BASE_ADDR_HIGH */
#define MME_SHADOW_3_B_BASE_ADDR_HIGH_V_SHIFT                        0
#define MME_SHADOW_3_B_BASE_ADDR_HIGH_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_3_CIN_BASE_ADDR_HIGH */
#define MME_SHADOW_3_CIN_BASE_ADDR_HIGH_V_SHIFT                      0
#define MME_SHADOW_3_CIN_BASE_ADDR_HIGH_V_MASK                       0xFFFFFFFF

/* MME_SHADOW_3_COUT_BASE_ADDR_HIGH */
#define MME_SHADOW_3_COUT_BASE_ADDR_HIGH_V_SHIFT                     0
#define MME_SHADOW_3_COUT_BASE_ADDR_HIGH_V_MASK                      0xFFFFFFFF

/* MME_SHADOW_3_BIAS_BASE_ADDR_HIGH */
#define MME_SHADOW_3_BIAS_BASE_ADDR_HIGH_V_SHIFT                     0
#define MME_SHADOW_3_BIAS_BASE_ADDR_HIGH_V_MASK                      0xFFFFFFFF

/* MME_SHADOW_3_A_BASE_ADDR_LOW */
#define MME_SHADOW_3_A_BASE_ADDR_LOW_V_SHIFT                         0
#define MME_SHADOW_3_A_BASE_ADDR_LOW_V_MASK                          0xFFFFFFFF

/* MME_SHADOW_3_B_BASE_ADDR_LOW */
#define MME_SHADOW_3_B_BASE_ADDR_LOW_V_SHIFT                         0
#define MME_SHADOW_3_B_BASE_ADDR_LOW_V_MASK                          0xFFFFFFFF

/* MME_SHADOW_3_CIN_BASE_ADDR_LOW */
#define MME_SHADOW_3_CIN_BASE_ADDR_LOW_V_SHIFT                       0
#define MME_SHADOW_3_CIN_BASE_ADDR_LOW_V_MASK                        0xFFFFFFFF

/* MME_SHADOW_3_COUT_BASE_ADDR_LOW */
#define MME_SHADOW_3_COUT_BASE_ADDR_LOW_V_SHIFT                      0
#define MME_SHADOW_3_COUT_BASE_ADDR_LOW_V_MASK                       0xFFFFFFFF

/* MME_SHADOW_3_BIAS_BASE_ADDR_LOW */
#define MME_SHADOW_3_BIAS_BASE_ADDR_LOW_V_SHIFT                      0
#define MME_SHADOW_3_BIAS_BASE_ADDR_LOW_V_MASK                       0xFFFFFFFF

/* MME_SHADOW_3_HEADER */
#define MME_SHADOW_3_HEADER_SIGNAL_MASK_SHIFT                        0
#define MME_SHADOW_3_HEADER_SIGNAL_MASK_MASK                         0x1F
#define MME_SHADOW_3_HEADER_SIGNAL_EN_SHIFT                          5
#define MME_SHADOW_3_HEADER_SIGNAL_EN_MASK                           0x20
#define MME_SHADOW_3_HEADER_TRANS_A_SHIFT                            6
#define MME_SHADOW_3_HEADER_TRANS_A_MASK                             0x40
#define MME_SHADOW_3_HEADER_LOWER_A_SHIFT                            7
#define MME_SHADOW_3_HEADER_LOWER_A_MASK                             0x80
#define MME_SHADOW_3_HEADER_ACCUM_MASK_SHIFT                         8
#define MME_SHADOW_3_HEADER_ACCUM_MASK_MASK                          0xF00
#define MME_SHADOW_3_HEADER_LOAD_BIAS_SHIFT                          12
#define MME_SHADOW_3_HEADER_LOAD_BIAS_MASK                           0x1000
#define MME_SHADOW_3_HEADER_LOAD_CIN_SHIFT                           13
#define MME_SHADOW_3_HEADER_LOAD_CIN_MASK                            0x2000
#define MME_SHADOW_3_HEADER_STORE_OUT_SHIFT                          15
#define MME_SHADOW_3_HEADER_STORE_OUT_MASK                           0x8000
#define MME_SHADOW_3_HEADER_ACC_LD_INC_DISABLE_SHIFT                 16
#define MME_SHADOW_3_HEADER_ACC_LD_INC_DISABLE_MASK                  0x10000
#define MME_SHADOW_3_HEADER_ADVANCE_A_SHIFT                          17
#define MME_SHADOW_3_HEADER_ADVANCE_A_MASK                           0x20000
#define MME_SHADOW_3_HEADER_ADVANCE_B_SHIFT                          18
#define MME_SHADOW_3_HEADER_ADVANCE_B_MASK                           0x40000
#define MME_SHADOW_3_HEADER_ADVANCE_CIN_SHIFT                        19
#define MME_SHADOW_3_HEADER_ADVANCE_CIN_MASK                         0x80000
#define MME_SHADOW_3_HEADER_ADVANCE_COUT_SHIFT                       20
#define MME_SHADOW_3_HEADER_ADVANCE_COUT_MASK                        0x100000
#define MME_SHADOW_3_HEADER_COMPRESSED_B_SHIFT                       21
#define MME_SHADOW_3_HEADER_COMPRESSED_B_MASK                        0x200000
#define MME_SHADOW_3_HEADER_MASK_CONV_END_SHIFT                      22
#define MME_SHADOW_3_HEADER_MASK_CONV_END_MASK                       0x400000
#define MME_SHADOW_3_HEADER_ACC_ST_INC_DISABLE_SHIFT                 23
#define MME_SHADOW_3_HEADER_ACC_ST_INC_DISABLE_MASK                  0x800000
#define MME_SHADOW_3_HEADER_AB_DATA_TYPE_SHIFT                       24
#define MME_SHADOW_3_HEADER_AB_DATA_TYPE_MASK                        0x3000000
#define MME_SHADOW_3_HEADER_CIN_DATA_TYPE_SHIFT                      26
#define MME_SHADOW_3_HEADER_CIN_DATA_TYPE_MASK                       0x1C000000
#define MME_SHADOW_3_HEADER_COUT_DATA_TYPE_SHIFT                     29
#define MME_SHADOW_3_HEADER_COUT_DATA_TYPE_MASK                      0xE0000000

/* MME_SHADOW_3_KERNEL_SIZE_MINUS_1 */
#define MME_SHADOW_3_KERNEL_SIZE_MINUS_1_DIM_0_SHIFT                 0
#define MME_SHADOW_3_KERNEL_SIZE_MINUS_1_DIM_0_MASK                  0xFF
#define MME_SHADOW_3_KERNEL_SIZE_MINUS_1_DIM_1_SHIFT                 8
#define MME_SHADOW_3_KERNEL_SIZE_MINUS_1_DIM_1_MASK                  0xFF00
#define MME_SHADOW_3_KERNEL_SIZE_MINUS_1_DIM_2_SHIFT                 16
#define MME_SHADOW_3_KERNEL_SIZE_MINUS_1_DIM_2_MASK                  0xFF0000
#define MME_SHADOW_3_KERNEL_SIZE_MINUS_1_DIM_3_SHIFT                 24
#define MME_SHADOW_3_KERNEL_SIZE_MINUS_1_DIM_3_MASK                  0xFF000000

/* MME_SHADOW_3_ASSOCIATED_DIMS */
#define MME_SHADOW_3_ASSOCIATED_DIMS_A_0_SHIFT                       0
#define MME_SHADOW_3_ASSOCIATED_DIMS_A_0_MASK                        0x7
#define MME_SHADOW_3_ASSOCIATED_DIMS_B_0_SHIFT                       3
#define MME_SHADOW_3_ASSOCIATED_DIMS_B_0_MASK                        0x38
#define MME_SHADOW_3_ASSOCIATED_DIMS_CIN_0_SHIFT                     6
#define MME_SHADOW_3_ASSOCIATED_DIMS_CIN_0_MASK                      0x1C0
#define MME_SHADOW_3_ASSOCIATED_DIMS_COUT_0_SHIFT                    9
#define MME_SHADOW_3_ASSOCIATED_DIMS_COUT_0_MASK                     0xE00
#define MME_SHADOW_3_ASSOCIATED_DIMS_A_1_SHIFT                       16
#define MME_SHADOW_3_ASSOCIATED_DIMS_A_1_MASK                        0x70000
#define MME_SHADOW_3_ASSOCIATED_DIMS_B_1_SHIFT                       19
#define MME_SHADOW_3_ASSOCIATED_DIMS_B_1_MASK                        0x380000
#define MME_SHADOW_3_ASSOCIATED_DIMS_CIN_1_SHIFT                     22
#define MME_SHADOW_3_ASSOCIATED_DIMS_CIN_1_MASK                      0x1C00000
#define MME_SHADOW_3_ASSOCIATED_DIMS_COUT_1_SHIFT                    25
#define MME_SHADOW_3_ASSOCIATED_DIMS_COUT_1_MASK                     0xE000000

/* MME_SHADOW_3_COUT_SCALE */
#define MME_SHADOW_3_COUT_SCALE_V_SHIFT                              0
#define MME_SHADOW_3_COUT_SCALE_V_MASK                               0xFFFFFFFF

/* MME_SHADOW_3_CIN_SCALE */
#define MME_SHADOW_3_CIN_SCALE_V_SHIFT                               0
#define MME_SHADOW_3_CIN_SCALE_V_MASK                                0xFFFFFFFF

/* MME_SHADOW_3_GEMMLOWP_ZP */
#define MME_SHADOW_3_GEMMLOWP_ZP_ZP_CIN_SHIFT                        0
#define MME_SHADOW_3_GEMMLOWP_ZP_ZP_CIN_MASK                         0x1FF
#define MME_SHADOW_3_GEMMLOWP_ZP_ZP_COUT_SHIFT                       9
#define MME_SHADOW_3_GEMMLOWP_ZP_ZP_COUT_MASK                        0x3FE00
#define MME_SHADOW_3_GEMMLOWP_ZP_ZP_B_SHIFT                          18
#define MME_SHADOW_3_GEMMLOWP_ZP_ZP_B_MASK                           0x7FC0000
#define MME_SHADOW_3_GEMMLOWP_ZP_GEMMLOWP_EU_EN_SHIFT                27
#define MME_SHADOW_3_GEMMLOWP_ZP_GEMMLOWP_EU_EN_MASK                 0x8000000
#define MME_SHADOW_3_GEMMLOWP_ZP_ACCUM_SHIFT                         28
#define MME_SHADOW_3_GEMMLOWP_ZP_ACCUM_MASK                          0x10000000
#define MME_SHADOW_3_GEMMLOWP_ZP_ACCUM_BIAS_SHIFT                    29
#define MME_SHADOW_3_GEMMLOWP_ZP_ACCUM_BIAS_MASK                     0x20000000
#define MME_SHADOW_3_GEMMLOWP_ZP_RELU_EN_SHIFT                       30
#define MME_SHADOW_3_GEMMLOWP_ZP_RELU_EN_MASK                        0x40000000

/* MME_SHADOW_3_GEMMLOWP_EXPONENT */
#define MME_SHADOW_3_GEMMLOWP_EXPONENT_EXPONENT_CIN_SHIFT            0
#define MME_SHADOW_3_GEMMLOWP_EXPONENT_EXPONENT_CIN_MASK             0x3F
#define MME_SHADOW_3_GEMMLOWP_EXPONENT_EXPONENT_COUT_SHIFT           8
#define MME_SHADOW_3_GEMMLOWP_EXPONENT_EXPONENT_COUT_MASK            0x3F00
#define MME_SHADOW_3_GEMMLOWP_EXPONENT_MUL_CIN_EN_SHIFT              16
#define MME_SHADOW_3_GEMMLOWP_EXPONENT_MUL_CIN_EN_MASK               0x10000
#define MME_SHADOW_3_GEMMLOWP_EXPONENT_MUL_COUT_EN_SHIFT             17
#define MME_SHADOW_3_GEMMLOWP_EXPONENT_MUL_COUT_EN_MASK              0x20000

/* MME_SHADOW_3_A_ROI_BASE_OFFSET */
#define MME_SHADOW_3_A_ROI_BASE_OFFSET_V_SHIFT                       0
#define MME_SHADOW_3_A_ROI_BASE_OFFSET_V_MASK                        0xFFFFFFFF

/* MME_SHADOW_3_A_VALID_ELEMENTS */
#define MME_SHADOW_3_A_VALID_ELEMENTS_V_SHIFT                        0
#define MME_SHADOW_3_A_VALID_ELEMENTS_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_3_A_LOOP_STRIDE */
#define MME_SHADOW_3_A_LOOP_STRIDE_V_SHIFT                           0
#define MME_SHADOW_3_A_LOOP_STRIDE_V_MASK                            0xFFFFFFFF

/* MME_SHADOW_3_A_ROI_SIZE */
#define MME_SHADOW_3_A_ROI_SIZE_V_SHIFT                              0
#define MME_SHADOW_3_A_ROI_SIZE_V_MASK                               0xFFFFFFFF

/* MME_SHADOW_3_A_SPATIAL_START_OFFSET */
#define MME_SHADOW_3_A_SPATIAL_START_OFFSET_V_SHIFT                  0
#define MME_SHADOW_3_A_SPATIAL_START_OFFSET_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_3_A_SPATIAL_STRIDE */
#define MME_SHADOW_3_A_SPATIAL_STRIDE_V_SHIFT                        0
#define MME_SHADOW_3_A_SPATIAL_STRIDE_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_3_A_SPATIAL_SIZE_MINUS_1 */
#define MME_SHADOW_3_A_SPATIAL_SIZE_MINUS_1_V_SHIFT                  0
#define MME_SHADOW_3_A_SPATIAL_SIZE_MINUS_1_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_3_B_ROI_BASE_OFFSET */
#define MME_SHADOW_3_B_ROI_BASE_OFFSET_V_SHIFT                       0
#define MME_SHADOW_3_B_ROI_BASE_OFFSET_V_MASK                        0xFFFFFFFF

/* MME_SHADOW_3_B_VALID_ELEMENTS */
#define MME_SHADOW_3_B_VALID_ELEMENTS_V_SHIFT                        0
#define MME_SHADOW_3_B_VALID_ELEMENTS_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_3_B_LOOP_STRIDE */
#define MME_SHADOW_3_B_LOOP_STRIDE_V_SHIFT                           0
#define MME_SHADOW_3_B_LOOP_STRIDE_V_MASK                            0xFFFFFFFF

/* MME_SHADOW_3_B_ROI_SIZE */
#define MME_SHADOW_3_B_ROI_SIZE_V_SHIFT                              0
#define MME_SHADOW_3_B_ROI_SIZE_V_MASK                               0xFFFFFFFF

/* MME_SHADOW_3_B_SPATIAL_START_OFFSET */
#define MME_SHADOW_3_B_SPATIAL_START_OFFSET_V_SHIFT                  0
#define MME_SHADOW_3_B_SPATIAL_START_OFFSET_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_3_B_SPATIAL_STRIDE */
#define MME_SHADOW_3_B_SPATIAL_STRIDE_V_SHIFT                        0
#define MME_SHADOW_3_B_SPATIAL_STRIDE_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_3_B_SPATIAL_SIZE_MINUS_1 */
#define MME_SHADOW_3_B_SPATIAL_SIZE_MINUS_1_V_SHIFT                  0
#define MME_SHADOW_3_B_SPATIAL_SIZE_MINUS_1_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_3_C_ROI_BASE_OFFSET */
#define MME_SHADOW_3_C_ROI_BASE_OFFSET_V_SHIFT                       0
#define MME_SHADOW_3_C_ROI_BASE_OFFSET_V_MASK                        0xFFFFFFFF

/* MME_SHADOW_3_C_VALID_ELEMENTS */
#define MME_SHADOW_3_C_VALID_ELEMENTS_V_SHIFT                        0
#define MME_SHADOW_3_C_VALID_ELEMENTS_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_3_C_LOOP_STRIDE */
#define MME_SHADOW_3_C_LOOP_STRIDE_V_SHIFT                           0
#define MME_SHADOW_3_C_LOOP_STRIDE_V_MASK                            0xFFFFFFFF

/* MME_SHADOW_3_C_ROI_SIZE */
#define MME_SHADOW_3_C_ROI_SIZE_V_SHIFT                              0
#define MME_SHADOW_3_C_ROI_SIZE_V_MASK                               0xFFFFFFFF

/* MME_SHADOW_3_C_SPATIAL_START_OFFSET */
#define MME_SHADOW_3_C_SPATIAL_START_OFFSET_V_SHIFT                  0
#define MME_SHADOW_3_C_SPATIAL_START_OFFSET_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_3_C_SPATIAL_STRIDE */
#define MME_SHADOW_3_C_SPATIAL_STRIDE_V_SHIFT                        0
#define MME_SHADOW_3_C_SPATIAL_STRIDE_V_MASK                         0xFFFFFFFF

/* MME_SHADOW_3_C_SPATIAL_SIZE_MINUS_1 */
#define MME_SHADOW_3_C_SPATIAL_SIZE_MINUS_1_V_SHIFT                  0
#define MME_SHADOW_3_C_SPATIAL_SIZE_MINUS_1_V_MASK                   0xFFFFFFFF

/* MME_SHADOW_3_SYNC_OBJECT_MESSAGE */
#define MME_SHADOW_3_SYNC_OBJECT_MESSAGE_SO_WRITE_VALUE_SHIFT        0
#define MME_SHADOW_3_SYNC_OBJECT_MESSAGE_SO_WRITE_VALUE_MASK         0xFFFF
#define MME_SHADOW_3_SYNC_OBJECT_MESSAGE_SO_ADDRESS_OFFSET_SHIFT     16
#define MME_SHADOW_3_SYNC_OBJECT_MESSAGE_SO_ADDRESS_OFFSET_MASK      0x7FFF0000
#define MME_SHADOW_3_SYNC_OBJECT_MESSAGE_SO_OPERATION_SHIFT          31
#define MME_SHADOW_3_SYNC_OBJECT_MESSAGE_SO_OPERATION_MASK           0x80000000

/* MME_SHADOW_3_E_PADDING_VALUE_A */
#define MME_SHADOW_3_E_PADDING_VALUE_A_V_SHIFT                       0
#define MME_SHADOW_3_E_PADDING_VALUE_A_V_MASK                        0xFFFF

/* MME_SHADOW_3_E_NUM_ITERATION_MINUS_1 */
#define MME_SHADOW_3_E_NUM_ITERATION_MINUS_1_V_SHIFT                 0
#define MME_SHADOW_3_E_NUM_ITERATION_MINUS_1_V_MASK                  0xFFFFFFFF

/* MME_SHADOW_3_E_BUBBLES_PER_SPLIT */
#define MME_SHADOW_3_E_BUBBLES_PER_SPLIT_A_SHIFT                     0
#define MME_SHADOW_3_E_BUBBLES_PER_SPLIT_A_MASK                      0xFF
#define MME_SHADOW_3_E_BUBBLES_PER_SPLIT_B_SHIFT                     8
#define MME_SHADOW_3_E_BUBBLES_PER_SPLIT_B_MASK                      0xFF00
#define MME_SHADOW_3_E_BUBBLES_PER_SPLIT_CIN_SHIFT                   16
#define MME_SHADOW_3_E_BUBBLES_PER_SPLIT_CIN_MASK                    0xFF0000
#define MME_SHADOW_3_E_BUBBLES_PER_SPLIT_ID_SHIFT                    24
#define MME_SHADOW_3_E_BUBBLES_PER_SPLIT_ID_MASK                     0xFF000000

#endif /* ASIC_REG_MME_MASKS_H_ */
