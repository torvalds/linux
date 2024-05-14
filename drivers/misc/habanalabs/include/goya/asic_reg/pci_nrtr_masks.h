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

#ifndef ASIC_REG_PCI_NRTR_MASKS_H_
#define ASIC_REG_PCI_NRTR_MASKS_H_

/*
 *****************************************
 *   PCI_NRTR (Prototype: IF_NRTR)
 *****************************************
 */

/* PCI_NRTR_HBW_MAX_CRED */
#define PCI_NRTR_HBW_MAX_CRED_WR_RQ_SHIFT                            0
#define PCI_NRTR_HBW_MAX_CRED_WR_RQ_MASK                             0x3F
#define PCI_NRTR_HBW_MAX_CRED_WR_RS_SHIFT                            8
#define PCI_NRTR_HBW_MAX_CRED_WR_RS_MASK                             0x3F00
#define PCI_NRTR_HBW_MAX_CRED_RD_RQ_SHIFT                            16
#define PCI_NRTR_HBW_MAX_CRED_RD_RQ_MASK                             0x3F0000
#define PCI_NRTR_HBW_MAX_CRED_RD_RS_SHIFT                            24
#define PCI_NRTR_HBW_MAX_CRED_RD_RS_MASK                             0x3F000000

/* PCI_NRTR_LBW_MAX_CRED */
#define PCI_NRTR_LBW_MAX_CRED_WR_RQ_SHIFT                            0
#define PCI_NRTR_LBW_MAX_CRED_WR_RQ_MASK                             0x3F
#define PCI_NRTR_LBW_MAX_CRED_WR_RS_SHIFT                            8
#define PCI_NRTR_LBW_MAX_CRED_WR_RS_MASK                             0x3F00
#define PCI_NRTR_LBW_MAX_CRED_RD_RQ_SHIFT                            16
#define PCI_NRTR_LBW_MAX_CRED_RD_RQ_MASK                             0x3F0000
#define PCI_NRTR_LBW_MAX_CRED_RD_RS_SHIFT                            24
#define PCI_NRTR_LBW_MAX_CRED_RD_RS_MASK                             0x3F000000

/* PCI_NRTR_DBG_E_ARB */
#define PCI_NRTR_DBG_E_ARB_W_SHIFT                                   0
#define PCI_NRTR_DBG_E_ARB_W_MASK                                    0x7
#define PCI_NRTR_DBG_E_ARB_S_SHIFT                                   8
#define PCI_NRTR_DBG_E_ARB_S_MASK                                    0x700
#define PCI_NRTR_DBG_E_ARB_N_SHIFT                                   16
#define PCI_NRTR_DBG_E_ARB_N_MASK                                    0x70000
#define PCI_NRTR_DBG_E_ARB_L_SHIFT                                   24
#define PCI_NRTR_DBG_E_ARB_L_MASK                                    0x7000000

/* PCI_NRTR_DBG_W_ARB */
#define PCI_NRTR_DBG_W_ARB_E_SHIFT                                   0
#define PCI_NRTR_DBG_W_ARB_E_MASK                                    0x7
#define PCI_NRTR_DBG_W_ARB_S_SHIFT                                   8
#define PCI_NRTR_DBG_W_ARB_S_MASK                                    0x700
#define PCI_NRTR_DBG_W_ARB_N_SHIFT                                   16
#define PCI_NRTR_DBG_W_ARB_N_MASK                                    0x70000
#define PCI_NRTR_DBG_W_ARB_L_SHIFT                                   24
#define PCI_NRTR_DBG_W_ARB_L_MASK                                    0x7000000

/* PCI_NRTR_DBG_N_ARB */
#define PCI_NRTR_DBG_N_ARB_W_SHIFT                                   0
#define PCI_NRTR_DBG_N_ARB_W_MASK                                    0x7
#define PCI_NRTR_DBG_N_ARB_E_SHIFT                                   8
#define PCI_NRTR_DBG_N_ARB_E_MASK                                    0x700
#define PCI_NRTR_DBG_N_ARB_S_SHIFT                                   16
#define PCI_NRTR_DBG_N_ARB_S_MASK                                    0x70000
#define PCI_NRTR_DBG_N_ARB_L_SHIFT                                   24
#define PCI_NRTR_DBG_N_ARB_L_MASK                                    0x7000000

/* PCI_NRTR_DBG_S_ARB */
#define PCI_NRTR_DBG_S_ARB_W_SHIFT                                   0
#define PCI_NRTR_DBG_S_ARB_W_MASK                                    0x7
#define PCI_NRTR_DBG_S_ARB_E_SHIFT                                   8
#define PCI_NRTR_DBG_S_ARB_E_MASK                                    0x700
#define PCI_NRTR_DBG_S_ARB_N_SHIFT                                   16
#define PCI_NRTR_DBG_S_ARB_N_MASK                                    0x70000
#define PCI_NRTR_DBG_S_ARB_L_SHIFT                                   24
#define PCI_NRTR_DBG_S_ARB_L_MASK                                    0x7000000

/* PCI_NRTR_DBG_L_ARB */
#define PCI_NRTR_DBG_L_ARB_W_SHIFT                                   0
#define PCI_NRTR_DBG_L_ARB_W_MASK                                    0x7
#define PCI_NRTR_DBG_L_ARB_E_SHIFT                                   8
#define PCI_NRTR_DBG_L_ARB_E_MASK                                    0x700
#define PCI_NRTR_DBG_L_ARB_S_SHIFT                                   16
#define PCI_NRTR_DBG_L_ARB_S_MASK                                    0x70000
#define PCI_NRTR_DBG_L_ARB_N_SHIFT                                   24
#define PCI_NRTR_DBG_L_ARB_N_MASK                                    0x7000000

/* PCI_NRTR_DBG_E_ARB_MAX */
#define PCI_NRTR_DBG_E_ARB_MAX_CREDIT_SHIFT                          0
#define PCI_NRTR_DBG_E_ARB_MAX_CREDIT_MASK                           0x3F

/* PCI_NRTR_DBG_W_ARB_MAX */
#define PCI_NRTR_DBG_W_ARB_MAX_CREDIT_SHIFT                          0
#define PCI_NRTR_DBG_W_ARB_MAX_CREDIT_MASK                           0x3F

/* PCI_NRTR_DBG_N_ARB_MAX */
#define PCI_NRTR_DBG_N_ARB_MAX_CREDIT_SHIFT                          0
#define PCI_NRTR_DBG_N_ARB_MAX_CREDIT_MASK                           0x3F

/* PCI_NRTR_DBG_S_ARB_MAX */
#define PCI_NRTR_DBG_S_ARB_MAX_CREDIT_SHIFT                          0
#define PCI_NRTR_DBG_S_ARB_MAX_CREDIT_MASK                           0x3F

/* PCI_NRTR_DBG_L_ARB_MAX */
#define PCI_NRTR_DBG_L_ARB_MAX_CREDIT_SHIFT                          0
#define PCI_NRTR_DBG_L_ARB_MAX_CREDIT_MASK                           0x3F

/* PCI_NRTR_SPLIT_COEF */
#define PCI_NRTR_SPLIT_COEF_VAL_SHIFT                                0
#define PCI_NRTR_SPLIT_COEF_VAL_MASK                                 0xFFFF

/* PCI_NRTR_SPLIT_CFG */
#define PCI_NRTR_SPLIT_CFG_FORCE_WAK_ORDER_SHIFT                     0
#define PCI_NRTR_SPLIT_CFG_FORCE_WAK_ORDER_MASK                      0x1
#define PCI_NRTR_SPLIT_CFG_FORCE_STRONG_ORDER_SHIFT                  1
#define PCI_NRTR_SPLIT_CFG_FORCE_STRONG_ORDER_MASK                   0x2
#define PCI_NRTR_SPLIT_CFG_DEFAULT_MESH_SHIFT                        2
#define PCI_NRTR_SPLIT_CFG_DEFAULT_MESH_MASK                         0xC
#define PCI_NRTR_SPLIT_CFG_RD_RATE_LIM_EN_SHIFT                      4
#define PCI_NRTR_SPLIT_CFG_RD_RATE_LIM_EN_MASK                       0x10
#define PCI_NRTR_SPLIT_CFG_WR_RATE_LIM_EN_SHIFT                      5
#define PCI_NRTR_SPLIT_CFG_WR_RATE_LIM_EN_MASK                       0x20
#define PCI_NRTR_SPLIT_CFG_B2B_OPT_SHIFT                             6
#define PCI_NRTR_SPLIT_CFG_B2B_OPT_MASK                              0x1C0

/* PCI_NRTR_SPLIT_RD_SAT */
#define PCI_NRTR_SPLIT_RD_SAT_VAL_SHIFT                              0
#define PCI_NRTR_SPLIT_RD_SAT_VAL_MASK                               0xFFFF

/* PCI_NRTR_SPLIT_RD_RST_TOKEN */
#define PCI_NRTR_SPLIT_RD_RST_TOKEN_VAL_SHIFT                        0
#define PCI_NRTR_SPLIT_RD_RST_TOKEN_VAL_MASK                         0xFFFF

/* PCI_NRTR_SPLIT_RD_TIMEOUT */
#define PCI_NRTR_SPLIT_RD_TIMEOUT_VAL_SHIFT                          0
#define PCI_NRTR_SPLIT_RD_TIMEOUT_VAL_MASK                           0xFFFFFFFF

/* PCI_NRTR_SPLIT_WR_SAT */
#define PCI_NRTR_SPLIT_WR_SAT_VAL_SHIFT                              0
#define PCI_NRTR_SPLIT_WR_SAT_VAL_MASK                               0xFFFF

/* PCI_NRTR_WPLIT_WR_TST_TOLEN */
#define PCI_NRTR_WPLIT_WR_TST_TOLEN_VAL_SHIFT                        0
#define PCI_NRTR_WPLIT_WR_TST_TOLEN_VAL_MASK                         0xFFFF

/* PCI_NRTR_SPLIT_WR_TIMEOUT */
#define PCI_NRTR_SPLIT_WR_TIMEOUT_VAL_SHIFT                          0
#define PCI_NRTR_SPLIT_WR_TIMEOUT_VAL_MASK                           0xFFFFFFFF

/* PCI_NRTR_HBW_RANGE_HIT */
#define PCI_NRTR_HBW_RANGE_HIT_IND_SHIFT                             0
#define PCI_NRTR_HBW_RANGE_HIT_IND_MASK                              0xFF

/* PCI_NRTR_HBW_RANGE_MASK_L */
#define PCI_NRTR_HBW_RANGE_MASK_L_VAL_SHIFT                          0
#define PCI_NRTR_HBW_RANGE_MASK_L_VAL_MASK                           0xFFFFFFFF

/* PCI_NRTR_HBW_RANGE_MASK_H */
#define PCI_NRTR_HBW_RANGE_MASK_H_VAL_SHIFT                          0
#define PCI_NRTR_HBW_RANGE_MASK_H_VAL_MASK                           0x3FFFF

/* PCI_NRTR_HBW_RANGE_BASE_L */
#define PCI_NRTR_HBW_RANGE_BASE_L_VAL_SHIFT                          0
#define PCI_NRTR_HBW_RANGE_BASE_L_VAL_MASK                           0xFFFFFFFF

/* PCI_NRTR_HBW_RANGE_BASE_H */
#define PCI_NRTR_HBW_RANGE_BASE_H_VAL_SHIFT                          0
#define PCI_NRTR_HBW_RANGE_BASE_H_VAL_MASK                           0x3FFFF

/* PCI_NRTR_LBW_RANGE_HIT */
#define PCI_NRTR_LBW_RANGE_HIT_IND_SHIFT                             0
#define PCI_NRTR_LBW_RANGE_HIT_IND_MASK                              0xFFFF

/* PCI_NRTR_LBW_RANGE_MASK */
#define PCI_NRTR_LBW_RANGE_MASK_VAL_SHIFT                            0
#define PCI_NRTR_LBW_RANGE_MASK_VAL_MASK                             0x3FFFFFF

/* PCI_NRTR_LBW_RANGE_BASE */
#define PCI_NRTR_LBW_RANGE_BASE_VAL_SHIFT                            0
#define PCI_NRTR_LBW_RANGE_BASE_VAL_MASK                             0x3FFFFFF

/* PCI_NRTR_RGLTR */
#define PCI_NRTR_RGLTR_WR_EN_SHIFT                                   0
#define PCI_NRTR_RGLTR_WR_EN_MASK                                    0x1
#define PCI_NRTR_RGLTR_RD_EN_SHIFT                                   4
#define PCI_NRTR_RGLTR_RD_EN_MASK                                    0x10

/* PCI_NRTR_RGLTR_WR_RESULT */
#define PCI_NRTR_RGLTR_WR_RESULT_VAL_SHIFT                           0
#define PCI_NRTR_RGLTR_WR_RESULT_VAL_MASK                            0xFF

/* PCI_NRTR_RGLTR_RD_RESULT */
#define PCI_NRTR_RGLTR_RD_RESULT_VAL_SHIFT                           0
#define PCI_NRTR_RGLTR_RD_RESULT_VAL_MASK                            0xFF

/* PCI_NRTR_SCRAMB_EN */
#define PCI_NRTR_SCRAMB_EN_VAL_SHIFT                                 0
#define PCI_NRTR_SCRAMB_EN_VAL_MASK                                  0x1

/* PCI_NRTR_NON_LIN_SCRAMB */
#define PCI_NRTR_NON_LIN_SCRAMB_EN_SHIFT                             0
#define PCI_NRTR_NON_LIN_SCRAMB_EN_MASK                              0x1

#endif /* ASIC_REG_PCI_NRTR_MASKS_H_ */
