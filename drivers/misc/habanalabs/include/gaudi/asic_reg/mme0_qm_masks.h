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

#ifndef ASIC_REG_MME0_QM_MASKS_H_
#define ASIC_REG_MME0_QM_MASKS_H_

/*
 *****************************************
 *   MME0_QM (Prototype: QMAN)
 *****************************************
 */

/* MME0_QM_GLBL_CFG0 */
#define MME0_QM_GLBL_CFG0_PQF_EN_SHIFT                               0
#define MME0_QM_GLBL_CFG0_PQF_EN_MASK                                0xF
#define MME0_QM_GLBL_CFG0_CQF_EN_SHIFT                               4
#define MME0_QM_GLBL_CFG0_CQF_EN_MASK                                0x1F0
#define MME0_QM_GLBL_CFG0_CP_EN_SHIFT                                9
#define MME0_QM_GLBL_CFG0_CP_EN_MASK                                 0x3E00

/* MME0_QM_GLBL_CFG1 */
#define MME0_QM_GLBL_CFG1_PQF_STOP_SHIFT                             0
#define MME0_QM_GLBL_CFG1_PQF_STOP_MASK                              0xF
#define MME0_QM_GLBL_CFG1_CQF_STOP_SHIFT                             4
#define MME0_QM_GLBL_CFG1_CQF_STOP_MASK                              0x1F0
#define MME0_QM_GLBL_CFG1_CP_STOP_SHIFT                              9
#define MME0_QM_GLBL_CFG1_CP_STOP_MASK                               0x3E00
#define MME0_QM_GLBL_CFG1_PQF_FLUSH_SHIFT                            16
#define MME0_QM_GLBL_CFG1_PQF_FLUSH_MASK                             0xF0000
#define MME0_QM_GLBL_CFG1_CQF_FLUSH_SHIFT                            20
#define MME0_QM_GLBL_CFG1_CQF_FLUSH_MASK                             0x1F00000
#define MME0_QM_GLBL_CFG1_CP_FLUSH_SHIFT                             25
#define MME0_QM_GLBL_CFG1_CP_FLUSH_MASK                              0x3E000000

/* MME0_QM_GLBL_PROT */
#define MME0_QM_GLBL_PROT_PQF_SHIFT                                  0
#define MME0_QM_GLBL_PROT_PQF_MASK                                   0xF
#define MME0_QM_GLBL_PROT_CQF_SHIFT                                  4
#define MME0_QM_GLBL_PROT_CQF_MASK                                   0x1F0
#define MME0_QM_GLBL_PROT_CP_SHIFT                                   9
#define MME0_QM_GLBL_PROT_CP_MASK                                    0x3E00
#define MME0_QM_GLBL_PROT_ERR_SHIFT                                  14
#define MME0_QM_GLBL_PROT_ERR_MASK                                   0x4000
#define MME0_QM_GLBL_PROT_ARB_SHIFT                                  15
#define MME0_QM_GLBL_PROT_ARB_MASK                                   0x8000

/* MME0_QM_GLBL_ERR_CFG */
#define MME0_QM_GLBL_ERR_CFG_PQF_ERR_MSG_EN_SHIFT                    0
#define MME0_QM_GLBL_ERR_CFG_PQF_ERR_MSG_EN_MASK                     0xF
#define MME0_QM_GLBL_ERR_CFG_CQF_ERR_MSG_EN_SHIFT                    4
#define MME0_QM_GLBL_ERR_CFG_CQF_ERR_MSG_EN_MASK                     0x1F0
#define MME0_QM_GLBL_ERR_CFG_CP_ERR_MSG_EN_SHIFT                     9
#define MME0_QM_GLBL_ERR_CFG_CP_ERR_MSG_EN_MASK                      0x3E00
#define MME0_QM_GLBL_ERR_CFG_PQF_STOP_ON_ERR_SHIFT                   16
#define MME0_QM_GLBL_ERR_CFG_PQF_STOP_ON_ERR_MASK                    0xF0000
#define MME0_QM_GLBL_ERR_CFG_CQF_STOP_ON_ERR_SHIFT                   20
#define MME0_QM_GLBL_ERR_CFG_CQF_STOP_ON_ERR_MASK                    0x1F00000
#define MME0_QM_GLBL_ERR_CFG_CP_STOP_ON_ERR_SHIFT                    25
#define MME0_QM_GLBL_ERR_CFG_CP_STOP_ON_ERR_MASK                     0x3E000000
#define MME0_QM_GLBL_ERR_CFG_ARB_STOP_ON_ERR_SHIFT                   31
#define MME0_QM_GLBL_ERR_CFG_ARB_STOP_ON_ERR_MASK                    0x80000000

/* MME0_QM_GLBL_SECURE_PROPS */
#define MME0_QM_GLBL_SECURE_PROPS_0_ASID_SHIFT                       0
#define MME0_QM_GLBL_SECURE_PROPS_0_ASID_MASK                        0x3FF
#define MME0_QM_GLBL_SECURE_PROPS_1_ASID_SHIFT                       0
#define MME0_QM_GLBL_SECURE_PROPS_1_ASID_MASK                        0x3FF
#define MME0_QM_GLBL_SECURE_PROPS_2_ASID_SHIFT                       0
#define MME0_QM_GLBL_SECURE_PROPS_2_ASID_MASK                        0x3FF
#define MME0_QM_GLBL_SECURE_PROPS_3_ASID_SHIFT                       0
#define MME0_QM_GLBL_SECURE_PROPS_3_ASID_MASK                        0x3FF
#define MME0_QM_GLBL_SECURE_PROPS_4_ASID_SHIFT                       0
#define MME0_QM_GLBL_SECURE_PROPS_4_ASID_MASK                        0x3FF
#define MME0_QM_GLBL_SECURE_PROPS_0_MMBP_SHIFT                       10
#define MME0_QM_GLBL_SECURE_PROPS_0_MMBP_MASK                        0x400
#define MME0_QM_GLBL_SECURE_PROPS_1_MMBP_SHIFT                       10
#define MME0_QM_GLBL_SECURE_PROPS_1_MMBP_MASK                        0x400
#define MME0_QM_GLBL_SECURE_PROPS_2_MMBP_SHIFT                       10
#define MME0_QM_GLBL_SECURE_PROPS_2_MMBP_MASK                        0x400
#define MME0_QM_GLBL_SECURE_PROPS_3_MMBP_SHIFT                       10
#define MME0_QM_GLBL_SECURE_PROPS_3_MMBP_MASK                        0x400
#define MME0_QM_GLBL_SECURE_PROPS_4_MMBP_SHIFT                       10
#define MME0_QM_GLBL_SECURE_PROPS_4_MMBP_MASK                        0x400

/* MME0_QM_GLBL_NON_SECURE_PROPS */
#define MME0_QM_GLBL_NON_SECURE_PROPS_0_ASID_SHIFT                   0
#define MME0_QM_GLBL_NON_SECURE_PROPS_0_ASID_MASK                    0x3FF
#define MME0_QM_GLBL_NON_SECURE_PROPS_1_ASID_SHIFT                   0
#define MME0_QM_GLBL_NON_SECURE_PROPS_1_ASID_MASK                    0x3FF
#define MME0_QM_GLBL_NON_SECURE_PROPS_2_ASID_SHIFT                   0
#define MME0_QM_GLBL_NON_SECURE_PROPS_2_ASID_MASK                    0x3FF
#define MME0_QM_GLBL_NON_SECURE_PROPS_3_ASID_SHIFT                   0
#define MME0_QM_GLBL_NON_SECURE_PROPS_3_ASID_MASK                    0x3FF
#define MME0_QM_GLBL_NON_SECURE_PROPS_4_ASID_SHIFT                   0
#define MME0_QM_GLBL_NON_SECURE_PROPS_4_ASID_MASK                    0x3FF
#define MME0_QM_GLBL_NON_SECURE_PROPS_0_MMBP_SHIFT                   10
#define MME0_QM_GLBL_NON_SECURE_PROPS_0_MMBP_MASK                    0x400
#define MME0_QM_GLBL_NON_SECURE_PROPS_1_MMBP_SHIFT                   10
#define MME0_QM_GLBL_NON_SECURE_PROPS_1_MMBP_MASK                    0x400
#define MME0_QM_GLBL_NON_SECURE_PROPS_2_MMBP_SHIFT                   10
#define MME0_QM_GLBL_NON_SECURE_PROPS_2_MMBP_MASK                    0x400
#define MME0_QM_GLBL_NON_SECURE_PROPS_3_MMBP_SHIFT                   10
#define MME0_QM_GLBL_NON_SECURE_PROPS_3_MMBP_MASK                    0x400
#define MME0_QM_GLBL_NON_SECURE_PROPS_4_MMBP_SHIFT                   10
#define MME0_QM_GLBL_NON_SECURE_PROPS_4_MMBP_MASK                    0x400

/* MME0_QM_GLBL_STS0 */
#define MME0_QM_GLBL_STS0_PQF_IDLE_SHIFT                             0
#define MME0_QM_GLBL_STS0_PQF_IDLE_MASK                              0xF
#define MME0_QM_GLBL_STS0_CQF_IDLE_SHIFT                             4
#define MME0_QM_GLBL_STS0_CQF_IDLE_MASK                              0x1F0
#define MME0_QM_GLBL_STS0_CP_IDLE_SHIFT                              9
#define MME0_QM_GLBL_STS0_CP_IDLE_MASK                               0x3E00
#define MME0_QM_GLBL_STS0_PQF_IS_STOP_SHIFT                          16
#define MME0_QM_GLBL_STS0_PQF_IS_STOP_MASK                           0xF0000
#define MME0_QM_GLBL_STS0_CQF_IS_STOP_SHIFT                          20
#define MME0_QM_GLBL_STS0_CQF_IS_STOP_MASK                           0x1F00000
#define MME0_QM_GLBL_STS0_CP_IS_STOP_SHIFT                           25
#define MME0_QM_GLBL_STS0_CP_IS_STOP_MASK                            0x3E000000
#define MME0_QM_GLBL_STS0_ARB_IS_STOP_SHIFT                          31
#define MME0_QM_GLBL_STS0_ARB_IS_STOP_MASK                           0x80000000

/* MME0_QM_GLBL_STS1 */
#define MME0_QM_GLBL_STS1_PQF_RD_ERR_SHIFT                           0
#define MME0_QM_GLBL_STS1_PQF_RD_ERR_MASK                            0x1
#define MME0_QM_GLBL_STS1_CQF_RD_ERR_SHIFT                           1
#define MME0_QM_GLBL_STS1_CQF_RD_ERR_MASK                            0x2
#define MME0_QM_GLBL_STS1_CP_RD_ERR_SHIFT                            2
#define MME0_QM_GLBL_STS1_CP_RD_ERR_MASK                             0x4
#define MME0_QM_GLBL_STS1_CP_UNDEF_CMD_ERR_SHIFT                     3
#define MME0_QM_GLBL_STS1_CP_UNDEF_CMD_ERR_MASK                      0x8
#define MME0_QM_GLBL_STS1_CP_STOP_OP_SHIFT                           4
#define MME0_QM_GLBL_STS1_CP_STOP_OP_MASK                            0x10
#define MME0_QM_GLBL_STS1_CP_MSG_WR_ERR_SHIFT                        5
#define MME0_QM_GLBL_STS1_CP_MSG_WR_ERR_MASK                         0x20
#define MME0_QM_GLBL_STS1_CP_WREG_ERR_SHIFT                          6
#define MME0_QM_GLBL_STS1_CP_WREG_ERR_MASK                           0x40
#define MME0_QM_GLBL_STS1_CP_FENCE0_OVF_ERR_SHIFT                    8
#define MME0_QM_GLBL_STS1_CP_FENCE0_OVF_ERR_MASK                     0x100
#define MME0_QM_GLBL_STS1_CP_FENCE1_OVF_ERR_SHIFT                    9
#define MME0_QM_GLBL_STS1_CP_FENCE1_OVF_ERR_MASK                     0x200
#define MME0_QM_GLBL_STS1_CP_FENCE2_OVF_ERR_SHIFT                    10
#define MME0_QM_GLBL_STS1_CP_FENCE2_OVF_ERR_MASK                     0x400
#define MME0_QM_GLBL_STS1_CP_FENCE3_OVF_ERR_SHIFT                    11
#define MME0_QM_GLBL_STS1_CP_FENCE3_OVF_ERR_MASK                     0x800
#define MME0_QM_GLBL_STS1_CP_FENCE0_UDF_ERR_SHIFT                    12
#define MME0_QM_GLBL_STS1_CP_FENCE0_UDF_ERR_MASK                     0x1000
#define MME0_QM_GLBL_STS1_CP_FENCE1_UDF_ERR_SHIFT                    13
#define MME0_QM_GLBL_STS1_CP_FENCE1_UDF_ERR_MASK                     0x2000
#define MME0_QM_GLBL_STS1_CP_FENCE2_UDF_ERR_SHIFT                    14
#define MME0_QM_GLBL_STS1_CP_FENCE2_UDF_ERR_MASK                     0x4000
#define MME0_QM_GLBL_STS1_CP_FENCE3_UDF_ERR_SHIFT                    15
#define MME0_QM_GLBL_STS1_CP_FENCE3_UDF_ERR_MASK                     0x8000

/* MME0_QM_GLBL_STS1_4 */
#define MME0_QM_GLBL_STS1_4_CQF_RD_ERR_SHIFT                         1
#define MME0_QM_GLBL_STS1_4_CQF_RD_ERR_MASK                          0x2
#define MME0_QM_GLBL_STS1_4_CP_RD_ERR_SHIFT                          2
#define MME0_QM_GLBL_STS1_4_CP_RD_ERR_MASK                           0x4
#define MME0_QM_GLBL_STS1_4_CP_UNDEF_CMD_ERR_SHIFT                   3
#define MME0_QM_GLBL_STS1_4_CP_UNDEF_CMD_ERR_MASK                    0x8
#define MME0_QM_GLBL_STS1_4_CP_STOP_OP_SHIFT                         4
#define MME0_QM_GLBL_STS1_4_CP_STOP_OP_MASK                          0x10
#define MME0_QM_GLBL_STS1_4_CP_MSG_WR_ERR_SHIFT                      5
#define MME0_QM_GLBL_STS1_4_CP_MSG_WR_ERR_MASK                       0x20
#define MME0_QM_GLBL_STS1_4_CP_WREG_ERR_SHIFT                        6
#define MME0_QM_GLBL_STS1_4_CP_WREG_ERR_MASK                         0x40
#define MME0_QM_GLBL_STS1_4_CP_FENCE0_OVF_ERR_SHIFT                  8
#define MME0_QM_GLBL_STS1_4_CP_FENCE0_OVF_ERR_MASK                   0x100
#define MME0_QM_GLBL_STS1_4_CP_FENCE1_OVF_ERR_SHIFT                  9
#define MME0_QM_GLBL_STS1_4_CP_FENCE1_OVF_ERR_MASK                   0x200
#define MME0_QM_GLBL_STS1_4_CP_FENCE2_OVF_ERR_SHIFT                  10
#define MME0_QM_GLBL_STS1_4_CP_FENCE2_OVF_ERR_MASK                   0x400
#define MME0_QM_GLBL_STS1_4_CP_FENCE3_OVF_ERR_SHIFT                  11
#define MME0_QM_GLBL_STS1_4_CP_FENCE3_OVF_ERR_MASK                   0x800
#define MME0_QM_GLBL_STS1_4_CP_FENCE0_UDF_ERR_SHIFT                  12
#define MME0_QM_GLBL_STS1_4_CP_FENCE0_UDF_ERR_MASK                   0x1000
#define MME0_QM_GLBL_STS1_4_CP_FENCE1_UDF_ERR_SHIFT                  13
#define MME0_QM_GLBL_STS1_4_CP_FENCE1_UDF_ERR_MASK                   0x2000
#define MME0_QM_GLBL_STS1_4_CP_FENCE2_UDF_ERR_SHIFT                  14
#define MME0_QM_GLBL_STS1_4_CP_FENCE2_UDF_ERR_MASK                   0x4000
#define MME0_QM_GLBL_STS1_4_CP_FENCE3_UDF_ERR_SHIFT                  15
#define MME0_QM_GLBL_STS1_4_CP_FENCE3_UDF_ERR_MASK                   0x8000

/* MME0_QM_GLBL_MSG_EN */
#define MME0_QM_GLBL_MSG_EN_PQF_RD_ERR_SHIFT                         0
#define MME0_QM_GLBL_MSG_EN_PQF_RD_ERR_MASK                          0x1
#define MME0_QM_GLBL_MSG_EN_CQF_RD_ERR_SHIFT                         1
#define MME0_QM_GLBL_MSG_EN_CQF_RD_ERR_MASK                          0x2
#define MME0_QM_GLBL_MSG_EN_CP_RD_ERR_SHIFT                          2
#define MME0_QM_GLBL_MSG_EN_CP_RD_ERR_MASK                           0x4
#define MME0_QM_GLBL_MSG_EN_CP_UNDEF_CMD_ERR_SHIFT                   3
#define MME0_QM_GLBL_MSG_EN_CP_UNDEF_CMD_ERR_MASK                    0x8
#define MME0_QM_GLBL_MSG_EN_CP_STOP_OP_SHIFT                         4
#define MME0_QM_GLBL_MSG_EN_CP_STOP_OP_MASK                          0x10
#define MME0_QM_GLBL_MSG_EN_CP_MSG_WR_ERR_SHIFT                      5
#define MME0_QM_GLBL_MSG_EN_CP_MSG_WR_ERR_MASK                       0x20
#define MME0_QM_GLBL_MSG_EN_CP_WREG_ERR_SHIFT                        6
#define MME0_QM_GLBL_MSG_EN_CP_WREG_ERR_MASK                         0x40
#define MME0_QM_GLBL_MSG_EN_CP_FENCE0_OVF_ERR_SHIFT                  8
#define MME0_QM_GLBL_MSG_EN_CP_FENCE0_OVF_ERR_MASK                   0x100
#define MME0_QM_GLBL_MSG_EN_CP_FENCE1_OVF_ERR_SHIFT                  9
#define MME0_QM_GLBL_MSG_EN_CP_FENCE1_OVF_ERR_MASK                   0x200
#define MME0_QM_GLBL_MSG_EN_CP_FENCE2_OVF_ERR_SHIFT                  10
#define MME0_QM_GLBL_MSG_EN_CP_FENCE2_OVF_ERR_MASK                   0x400
#define MME0_QM_GLBL_MSG_EN_CP_FENCE3_OVF_ERR_SHIFT                  11
#define MME0_QM_GLBL_MSG_EN_CP_FENCE3_OVF_ERR_MASK                   0x800
#define MME0_QM_GLBL_MSG_EN_CP_FENCE0_UDF_ERR_SHIFT                  12
#define MME0_QM_GLBL_MSG_EN_CP_FENCE0_UDF_ERR_MASK                   0x1000
#define MME0_QM_GLBL_MSG_EN_CP_FENCE1_UDF_ERR_SHIFT                  13
#define MME0_QM_GLBL_MSG_EN_CP_FENCE1_UDF_ERR_MASK                   0x2000
#define MME0_QM_GLBL_MSG_EN_CP_FENCE2_UDF_ERR_SHIFT                  14
#define MME0_QM_GLBL_MSG_EN_CP_FENCE2_UDF_ERR_MASK                   0x4000
#define MME0_QM_GLBL_MSG_EN_CP_FENCE3_UDF_ERR_SHIFT                  15
#define MME0_QM_GLBL_MSG_EN_CP_FENCE3_UDF_ERR_MASK                   0x8000

/* MME0_QM_GLBL_MSG_EN_4 */
#define MME0_QM_GLBL_MSG_EN_4_CQF_RD_ERR_SHIFT                       1
#define MME0_QM_GLBL_MSG_EN_4_CQF_RD_ERR_MASK                        0x2
#define MME0_QM_GLBL_MSG_EN_4_CP_RD_ERR_SHIFT                        2
#define MME0_QM_GLBL_MSG_EN_4_CP_RD_ERR_MASK                         0x4
#define MME0_QM_GLBL_MSG_EN_4_CP_UNDEF_CMD_ERR_SHIFT                 3
#define MME0_QM_GLBL_MSG_EN_4_CP_UNDEF_CMD_ERR_MASK                  0x8
#define MME0_QM_GLBL_MSG_EN_4_CP_STOP_OP_SHIFT                       4
#define MME0_QM_GLBL_MSG_EN_4_CP_STOP_OP_MASK                        0x10
#define MME0_QM_GLBL_MSG_EN_4_CP_MSG_WR_ERR_SHIFT                    5
#define MME0_QM_GLBL_MSG_EN_4_CP_MSG_WR_ERR_MASK                     0x20
#define MME0_QM_GLBL_MSG_EN_4_CP_WREG_ERR_SHIFT                      6
#define MME0_QM_GLBL_MSG_EN_4_CP_WREG_ERR_MASK                       0x40
#define MME0_QM_GLBL_MSG_EN_4_CP_FENCE0_OVF_ERR_SHIFT                8
#define MME0_QM_GLBL_MSG_EN_4_CP_FENCE0_OVF_ERR_MASK                 0x100
#define MME0_QM_GLBL_MSG_EN_4_CP_FENCE1_OVF_ERR_SHIFT                9
#define MME0_QM_GLBL_MSG_EN_4_CP_FENCE1_OVF_ERR_MASK                 0x200
#define MME0_QM_GLBL_MSG_EN_4_CP_FENCE2_OVF_ERR_SHIFT                10
#define MME0_QM_GLBL_MSG_EN_4_CP_FENCE2_OVF_ERR_MASK                 0x400
#define MME0_QM_GLBL_MSG_EN_4_CP_FENCE3_OVF_ERR_SHIFT                11
#define MME0_QM_GLBL_MSG_EN_4_CP_FENCE3_OVF_ERR_MASK                 0x800
#define MME0_QM_GLBL_MSG_EN_4_CP_FENCE0_UDF_ERR_SHIFT                12
#define MME0_QM_GLBL_MSG_EN_4_CP_FENCE0_UDF_ERR_MASK                 0x1000
#define MME0_QM_GLBL_MSG_EN_4_CP_FENCE1_UDF_ERR_SHIFT                13
#define MME0_QM_GLBL_MSG_EN_4_CP_FENCE1_UDF_ERR_MASK                 0x2000
#define MME0_QM_GLBL_MSG_EN_4_CP_FENCE2_UDF_ERR_SHIFT                14
#define MME0_QM_GLBL_MSG_EN_4_CP_FENCE2_UDF_ERR_MASK                 0x4000
#define MME0_QM_GLBL_MSG_EN_4_CP_FENCE3_UDF_ERR_SHIFT                15
#define MME0_QM_GLBL_MSG_EN_4_CP_FENCE3_UDF_ERR_MASK                 0x8000

/* MME0_QM_PQ_BASE_LO */
#define MME0_QM_PQ_BASE_LO_VAL_SHIFT                                 0
#define MME0_QM_PQ_BASE_LO_VAL_MASK                                  0xFFFFFFFF

/* MME0_QM_PQ_BASE_HI */
#define MME0_QM_PQ_BASE_HI_VAL_SHIFT                                 0
#define MME0_QM_PQ_BASE_HI_VAL_MASK                                  0xFFFFFFFF

/* MME0_QM_PQ_SIZE */
#define MME0_QM_PQ_SIZE_VAL_SHIFT                                    0
#define MME0_QM_PQ_SIZE_VAL_MASK                                     0xFFFFFFFF

/* MME0_QM_PQ_PI */
#define MME0_QM_PQ_PI_VAL_SHIFT                                      0
#define MME0_QM_PQ_PI_VAL_MASK                                       0xFFFFFFFF

/* MME0_QM_PQ_CI */
#define MME0_QM_PQ_CI_VAL_SHIFT                                      0
#define MME0_QM_PQ_CI_VAL_MASK                                       0xFFFFFFFF

/* MME0_QM_PQ_CFG0 */
#define MME0_QM_PQ_CFG0_RESERVED_SHIFT                               0
#define MME0_QM_PQ_CFG0_RESERVED_MASK                                0x1

/* MME0_QM_PQ_CFG1 */
#define MME0_QM_PQ_CFG1_CREDIT_LIM_SHIFT                             0
#define MME0_QM_PQ_CFG1_CREDIT_LIM_MASK                              0xFFFF
#define MME0_QM_PQ_CFG1_MAX_INFLIGHT_SHIFT                           16
#define MME0_QM_PQ_CFG1_MAX_INFLIGHT_MASK                            0xFFFF0000

/* MME0_QM_PQ_ARUSER_31_11 */
#define MME0_QM_PQ_ARUSER_31_11_VAL_SHIFT                            0
#define MME0_QM_PQ_ARUSER_31_11_VAL_MASK                             0x1FFFFF

/* MME0_QM_PQ_STS0 */
#define MME0_QM_PQ_STS0_PQ_CREDIT_CNT_SHIFT                          0
#define MME0_QM_PQ_STS0_PQ_CREDIT_CNT_MASK                           0xFFFF
#define MME0_QM_PQ_STS0_PQ_FREE_CNT_SHIFT                            16
#define MME0_QM_PQ_STS0_PQ_FREE_CNT_MASK                             0xFFFF0000

/* MME0_QM_PQ_STS1 */
#define MME0_QM_PQ_STS1_PQ_INFLIGHT_CNT_SHIFT                        0
#define MME0_QM_PQ_STS1_PQ_INFLIGHT_CNT_MASK                         0xFFFF
#define MME0_QM_PQ_STS1_PQ_BUF_EMPTY_SHIFT                           30
#define MME0_QM_PQ_STS1_PQ_BUF_EMPTY_MASK                            0x40000000
#define MME0_QM_PQ_STS1_PQ_BUSY_SHIFT                                31
#define MME0_QM_PQ_STS1_PQ_BUSY_MASK                                 0x80000000

/* MME0_QM_CQ_CFG0 */
#define MME0_QM_CQ_CFG0_RESERVED_SHIFT                               0
#define MME0_QM_CQ_CFG0_RESERVED_MASK                                0x1

/* MME0_QM_CQ_CFG1 */
#define MME0_QM_CQ_CFG1_CREDIT_LIM_SHIFT                             0
#define MME0_QM_CQ_CFG1_CREDIT_LIM_MASK                              0xFFFF
#define MME0_QM_CQ_CFG1_MAX_INFLIGHT_SHIFT                           16
#define MME0_QM_CQ_CFG1_MAX_INFLIGHT_MASK                            0xFFFF0000

/* MME0_QM_CQ_ARUSER_31_11 */
#define MME0_QM_CQ_ARUSER_31_11_VAL_SHIFT                            0
#define MME0_QM_CQ_ARUSER_31_11_VAL_MASK                             0x1FFFFF

/* MME0_QM_CQ_STS0 */
#define MME0_QM_CQ_STS0_CQ_CREDIT_CNT_SHIFT                          0
#define MME0_QM_CQ_STS0_CQ_CREDIT_CNT_MASK                           0xFFFF
#define MME0_QM_CQ_STS0_CQ_FREE_CNT_SHIFT                            16
#define MME0_QM_CQ_STS0_CQ_FREE_CNT_MASK                             0xFFFF0000

/* MME0_QM_CQ_STS1 */
#define MME0_QM_CQ_STS1_CQ_INFLIGHT_CNT_SHIFT                        0
#define MME0_QM_CQ_STS1_CQ_INFLIGHT_CNT_MASK                         0xFFFF
#define MME0_QM_CQ_STS1_CQ_BUF_EMPTY_SHIFT                           30
#define MME0_QM_CQ_STS1_CQ_BUF_EMPTY_MASK                            0x40000000
#define MME0_QM_CQ_STS1_CQ_BUSY_SHIFT                                31
#define MME0_QM_CQ_STS1_CQ_BUSY_MASK                                 0x80000000

/* MME0_QM_CQ_PTR_LO_0 */
#define MME0_QM_CQ_PTR_LO_0_VAL_SHIFT                                0
#define MME0_QM_CQ_PTR_LO_0_VAL_MASK                                 0xFFFFFFFF

/* MME0_QM_CQ_PTR_HI_0 */
#define MME0_QM_CQ_PTR_HI_0_VAL_SHIFT                                0
#define MME0_QM_CQ_PTR_HI_0_VAL_MASK                                 0xFFFFFFFF

/* MME0_QM_CQ_TSIZE_0 */
#define MME0_QM_CQ_TSIZE_0_VAL_SHIFT                                 0
#define MME0_QM_CQ_TSIZE_0_VAL_MASK                                  0xFFFFFFFF

/* MME0_QM_CQ_CTL_0 */
#define MME0_QM_CQ_CTL_0_RPT_SHIFT                                   0
#define MME0_QM_CQ_CTL_0_RPT_MASK                                    0xFFFF
#define MME0_QM_CQ_CTL_0_CTL_SHIFT                                   16
#define MME0_QM_CQ_CTL_0_CTL_MASK                                    0xFFFF0000

/* MME0_QM_CQ_PTR_LO_1 */
#define MME0_QM_CQ_PTR_LO_1_VAL_SHIFT                                0
#define MME0_QM_CQ_PTR_LO_1_VAL_MASK                                 0xFFFFFFFF

/* MME0_QM_CQ_PTR_HI_1 */
#define MME0_QM_CQ_PTR_HI_1_VAL_SHIFT                                0
#define MME0_QM_CQ_PTR_HI_1_VAL_MASK                                 0xFFFFFFFF

/* MME0_QM_CQ_TSIZE_1 */
#define MME0_QM_CQ_TSIZE_1_VAL_SHIFT                                 0
#define MME0_QM_CQ_TSIZE_1_VAL_MASK                                  0xFFFFFFFF

/* MME0_QM_CQ_CTL_1 */
#define MME0_QM_CQ_CTL_1_RPT_SHIFT                                   0
#define MME0_QM_CQ_CTL_1_RPT_MASK                                    0xFFFF
#define MME0_QM_CQ_CTL_1_CTL_SHIFT                                   16
#define MME0_QM_CQ_CTL_1_CTL_MASK                                    0xFFFF0000

/* MME0_QM_CQ_PTR_LO_2 */
#define MME0_QM_CQ_PTR_LO_2_VAL_SHIFT                                0
#define MME0_QM_CQ_PTR_LO_2_VAL_MASK                                 0xFFFFFFFF

/* MME0_QM_CQ_PTR_HI_2 */
#define MME0_QM_CQ_PTR_HI_2_VAL_SHIFT                                0
#define MME0_QM_CQ_PTR_HI_2_VAL_MASK                                 0xFFFFFFFF

/* MME0_QM_CQ_TSIZE_2 */
#define MME0_QM_CQ_TSIZE_2_VAL_SHIFT                                 0
#define MME0_QM_CQ_TSIZE_2_VAL_MASK                                  0xFFFFFFFF

/* MME0_QM_CQ_CTL_2 */
#define MME0_QM_CQ_CTL_2_RPT_SHIFT                                   0
#define MME0_QM_CQ_CTL_2_RPT_MASK                                    0xFFFF
#define MME0_QM_CQ_CTL_2_CTL_SHIFT                                   16
#define MME0_QM_CQ_CTL_2_CTL_MASK                                    0xFFFF0000

/* MME0_QM_CQ_PTR_LO_3 */
#define MME0_QM_CQ_PTR_LO_3_VAL_SHIFT                                0
#define MME0_QM_CQ_PTR_LO_3_VAL_MASK                                 0xFFFFFFFF

/* MME0_QM_CQ_PTR_HI_3 */
#define MME0_QM_CQ_PTR_HI_3_VAL_SHIFT                                0
#define MME0_QM_CQ_PTR_HI_3_VAL_MASK                                 0xFFFFFFFF

/* MME0_QM_CQ_TSIZE_3 */
#define MME0_QM_CQ_TSIZE_3_VAL_SHIFT                                 0
#define MME0_QM_CQ_TSIZE_3_VAL_MASK                                  0xFFFFFFFF

/* MME0_QM_CQ_CTL_3 */
#define MME0_QM_CQ_CTL_3_RPT_SHIFT                                   0
#define MME0_QM_CQ_CTL_3_RPT_MASK                                    0xFFFF
#define MME0_QM_CQ_CTL_3_CTL_SHIFT                                   16
#define MME0_QM_CQ_CTL_3_CTL_MASK                                    0xFFFF0000

/* MME0_QM_CQ_PTR_LO_4 */
#define MME0_QM_CQ_PTR_LO_4_VAL_SHIFT                                0
#define MME0_QM_CQ_PTR_LO_4_VAL_MASK                                 0xFFFFFFFF

/* MME0_QM_CQ_PTR_HI_4 */
#define MME0_QM_CQ_PTR_HI_4_VAL_SHIFT                                0
#define MME0_QM_CQ_PTR_HI_4_VAL_MASK                                 0xFFFFFFFF

/* MME0_QM_CQ_TSIZE_4 */
#define MME0_QM_CQ_TSIZE_4_VAL_SHIFT                                 0
#define MME0_QM_CQ_TSIZE_4_VAL_MASK                                  0xFFFFFFFF

/* MME0_QM_CQ_CTL_4 */
#define MME0_QM_CQ_CTL_4_RPT_SHIFT                                   0
#define MME0_QM_CQ_CTL_4_RPT_MASK                                    0xFFFF
#define MME0_QM_CQ_CTL_4_CTL_SHIFT                                   16
#define MME0_QM_CQ_CTL_4_CTL_MASK                                    0xFFFF0000

/* MME0_QM_CQ_PTR_LO_STS */
#define MME0_QM_CQ_PTR_LO_STS_VAL_SHIFT                              0
#define MME0_QM_CQ_PTR_LO_STS_VAL_MASK                               0xFFFFFFFF

/* MME0_QM_CQ_PTR_HI_STS */
#define MME0_QM_CQ_PTR_HI_STS_VAL_SHIFT                              0
#define MME0_QM_CQ_PTR_HI_STS_VAL_MASK                               0xFFFFFFFF

/* MME0_QM_CQ_TSIZE_STS */
#define MME0_QM_CQ_TSIZE_STS_VAL_SHIFT                               0
#define MME0_QM_CQ_TSIZE_STS_VAL_MASK                                0xFFFFFFFF

/* MME0_QM_CQ_CTL_STS */
#define MME0_QM_CQ_CTL_STS_RPT_SHIFT                                 0
#define MME0_QM_CQ_CTL_STS_RPT_MASK                                  0xFFFF
#define MME0_QM_CQ_CTL_STS_CTL_SHIFT                                 16
#define MME0_QM_CQ_CTL_STS_CTL_MASK                                  0xFFFF0000

/* MME0_QM_CQ_IFIFO_CNT */
#define MME0_QM_CQ_IFIFO_CNT_VAL_SHIFT                               0
#define MME0_QM_CQ_IFIFO_CNT_VAL_MASK                                0x3

/* MME0_QM_CP_MSG_BASE0_ADDR_LO */
#define MME0_QM_CP_MSG_BASE0_ADDR_LO_VAL_SHIFT                       0
#define MME0_QM_CP_MSG_BASE0_ADDR_LO_VAL_MASK                        0xFFFFFFFF

/* MME0_QM_CP_MSG_BASE0_ADDR_HI */
#define MME0_QM_CP_MSG_BASE0_ADDR_HI_VAL_SHIFT                       0
#define MME0_QM_CP_MSG_BASE0_ADDR_HI_VAL_MASK                        0xFFFFFFFF

/* MME0_QM_CP_MSG_BASE1_ADDR_LO */
#define MME0_QM_CP_MSG_BASE1_ADDR_LO_VAL_SHIFT                       0
#define MME0_QM_CP_MSG_BASE1_ADDR_LO_VAL_MASK                        0xFFFFFFFF

/* MME0_QM_CP_MSG_BASE1_ADDR_HI */
#define MME0_QM_CP_MSG_BASE1_ADDR_HI_VAL_SHIFT                       0
#define MME0_QM_CP_MSG_BASE1_ADDR_HI_VAL_MASK                        0xFFFFFFFF

/* MME0_QM_CP_MSG_BASE2_ADDR_LO */
#define MME0_QM_CP_MSG_BASE2_ADDR_LO_VAL_SHIFT                       0
#define MME0_QM_CP_MSG_BASE2_ADDR_LO_VAL_MASK                        0xFFFFFFFF

/* MME0_QM_CP_MSG_BASE2_ADDR_HI */
#define MME0_QM_CP_MSG_BASE2_ADDR_HI_VAL_SHIFT                       0
#define MME0_QM_CP_MSG_BASE2_ADDR_HI_VAL_MASK                        0xFFFFFFFF

/* MME0_QM_CP_MSG_BASE3_ADDR_LO */
#define MME0_QM_CP_MSG_BASE3_ADDR_LO_VAL_SHIFT                       0
#define MME0_QM_CP_MSG_BASE3_ADDR_LO_VAL_MASK                        0xFFFFFFFF

/* MME0_QM_CP_MSG_BASE3_ADDR_HI */
#define MME0_QM_CP_MSG_BASE3_ADDR_HI_VAL_SHIFT                       0
#define MME0_QM_CP_MSG_BASE3_ADDR_HI_VAL_MASK                        0xFFFFFFFF

/* MME0_QM_CP_LDMA_TSIZE_OFFSET */
#define MME0_QM_CP_LDMA_TSIZE_OFFSET_VAL_SHIFT                       0
#define MME0_QM_CP_LDMA_TSIZE_OFFSET_VAL_MASK                        0xFFFFFFFF

/* MME0_QM_CP_LDMA_SRC_BASE_LO_OFFSET */
#define MME0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_VAL_SHIFT                 0
#define MME0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_VAL_MASK                  0xFFFFFFFF

/* MME0_QM_CP_LDMA_DST_BASE_LO_OFFSET */
#define MME0_QM_CP_LDMA_DST_BASE_LO_OFFSET_VAL_SHIFT                 0
#define MME0_QM_CP_LDMA_DST_BASE_LO_OFFSET_VAL_MASK                  0xFFFFFFFF

/* MME0_QM_CP_FENCE0_RDATA */
#define MME0_QM_CP_FENCE0_RDATA_INC_VAL_SHIFT                        0
#define MME0_QM_CP_FENCE0_RDATA_INC_VAL_MASK                         0xF

/* MME0_QM_CP_FENCE1_RDATA */
#define MME0_QM_CP_FENCE1_RDATA_INC_VAL_SHIFT                        0
#define MME0_QM_CP_FENCE1_RDATA_INC_VAL_MASK                         0xF

/* MME0_QM_CP_FENCE2_RDATA */
#define MME0_QM_CP_FENCE2_RDATA_INC_VAL_SHIFT                        0
#define MME0_QM_CP_FENCE2_RDATA_INC_VAL_MASK                         0xF

/* MME0_QM_CP_FENCE3_RDATA */
#define MME0_QM_CP_FENCE3_RDATA_INC_VAL_SHIFT                        0
#define MME0_QM_CP_FENCE3_RDATA_INC_VAL_MASK                         0xF

/* MME0_QM_CP_FENCE0_CNT */
#define MME0_QM_CP_FENCE0_CNT_VAL_SHIFT                              0
#define MME0_QM_CP_FENCE0_CNT_VAL_MASK                               0x3FFF

/* MME0_QM_CP_FENCE1_CNT */
#define MME0_QM_CP_FENCE1_CNT_VAL_SHIFT                              0
#define MME0_QM_CP_FENCE1_CNT_VAL_MASK                               0x3FFF

/* MME0_QM_CP_FENCE2_CNT */
#define MME0_QM_CP_FENCE2_CNT_VAL_SHIFT                              0
#define MME0_QM_CP_FENCE2_CNT_VAL_MASK                               0x3FFF

/* MME0_QM_CP_FENCE3_CNT */
#define MME0_QM_CP_FENCE3_CNT_VAL_SHIFT                              0
#define MME0_QM_CP_FENCE3_CNT_VAL_MASK                               0x3FFF

/* MME0_QM_CP_STS */
#define MME0_QM_CP_STS_MSG_INFLIGHT_CNT_SHIFT                        0
#define MME0_QM_CP_STS_MSG_INFLIGHT_CNT_MASK                         0xFFFF
#define MME0_QM_CP_STS_ERDY_SHIFT                                    16
#define MME0_QM_CP_STS_ERDY_MASK                                     0x10000
#define MME0_QM_CP_STS_RRDY_SHIFT                                    17
#define MME0_QM_CP_STS_RRDY_MASK                                     0x20000
#define MME0_QM_CP_STS_MRDY_SHIFT                                    18
#define MME0_QM_CP_STS_MRDY_MASK                                     0x40000
#define MME0_QM_CP_STS_SW_STOP_SHIFT                                 19
#define MME0_QM_CP_STS_SW_STOP_MASK                                  0x80000
#define MME0_QM_CP_STS_FENCE_ID_SHIFT                                20
#define MME0_QM_CP_STS_FENCE_ID_MASK                                 0x300000
#define MME0_QM_CP_STS_FENCE_IN_PROGRESS_SHIFT                       22
#define MME0_QM_CP_STS_FENCE_IN_PROGRESS_MASK                        0x400000

/* MME0_QM_CP_CURRENT_INST_LO */
#define MME0_QM_CP_CURRENT_INST_LO_VAL_SHIFT                         0
#define MME0_QM_CP_CURRENT_INST_LO_VAL_MASK                          0xFFFFFFFF

/* MME0_QM_CP_CURRENT_INST_HI */
#define MME0_QM_CP_CURRENT_INST_HI_VAL_SHIFT                         0
#define MME0_QM_CP_CURRENT_INST_HI_VAL_MASK                          0xFFFFFFFF

/* MME0_QM_CP_BARRIER_CFG */
#define MME0_QM_CP_BARRIER_CFG_EBGUARD_SHIFT                         0
#define MME0_QM_CP_BARRIER_CFG_EBGUARD_MASK                          0xFFF
#define MME0_QM_CP_BARRIER_CFG_RBGUARD_SHIFT                         16
#define MME0_QM_CP_BARRIER_CFG_RBGUARD_MASK                          0xF0000

/* MME0_QM_CP_DBG_0 */
#define MME0_QM_CP_DBG_0_CS_SHIFT                                    0
#define MME0_QM_CP_DBG_0_CS_MASK                                     0xF
#define MME0_QM_CP_DBG_0_EB_CNT_NOT_ZERO_SHIFT                       4
#define MME0_QM_CP_DBG_0_EB_CNT_NOT_ZERO_MASK                        0x10
#define MME0_QM_CP_DBG_0_BULK_CNT_NOT_ZERO_SHIFT                     5
#define MME0_QM_CP_DBG_0_BULK_CNT_NOT_ZERO_MASK                      0x20
#define MME0_QM_CP_DBG_0_MREB_STALL_SHIFT                            6
#define MME0_QM_CP_DBG_0_MREB_STALL_MASK                             0x40
#define MME0_QM_CP_DBG_0_STALL_SHIFT                                 7
#define MME0_QM_CP_DBG_0_STALL_MASK                                  0x80

/* MME0_QM_CP_ARUSER_31_11 */
#define MME0_QM_CP_ARUSER_31_11_VAL_SHIFT                            0
#define MME0_QM_CP_ARUSER_31_11_VAL_MASK                             0x1FFFFF

/* MME0_QM_CP_AWUSER_31_11 */
#define MME0_QM_CP_AWUSER_31_11_VAL_SHIFT                            0
#define MME0_QM_CP_AWUSER_31_11_VAL_MASK                             0x1FFFFF

/* MME0_QM_ARB_CFG_0 */
#define MME0_QM_ARB_CFG_0_TYPE_SHIFT                                 0
#define MME0_QM_ARB_CFG_0_TYPE_MASK                                  0x1
#define MME0_QM_ARB_CFG_0_IS_MASTER_SHIFT                            4
#define MME0_QM_ARB_CFG_0_IS_MASTER_MASK                             0x10
#define MME0_QM_ARB_CFG_0_EN_SHIFT                                   8
#define MME0_QM_ARB_CFG_0_EN_MASK                                    0x100
#define MME0_QM_ARB_CFG_0_MASK_SHIFT                                 12
#define MME0_QM_ARB_CFG_0_MASK_MASK                                  0xF000
#define MME0_QM_ARB_CFG_0_MST_MSG_NOSTALL_SHIFT                      16
#define MME0_QM_ARB_CFG_0_MST_MSG_NOSTALL_MASK                       0x10000

/* MME0_QM_ARB_CHOISE_Q_PUSH */
#define MME0_QM_ARB_CHOISE_Q_PUSH_VAL_SHIFT                          0
#define MME0_QM_ARB_CHOISE_Q_PUSH_VAL_MASK                           0x3

/* MME0_QM_ARB_WRR_WEIGHT */
#define MME0_QM_ARB_WRR_WEIGHT_VAL_SHIFT                             0
#define MME0_QM_ARB_WRR_WEIGHT_VAL_MASK                              0xFFFFFFFF

/* MME0_QM_ARB_CFG_1 */
#define MME0_QM_ARB_CFG_1_CLR_SHIFT                                  0
#define MME0_QM_ARB_CFG_1_CLR_MASK                                   0x1

/* MME0_QM_ARB_MST_AVAIL_CRED */
#define MME0_QM_ARB_MST_AVAIL_CRED_VAL_SHIFT                         0
#define MME0_QM_ARB_MST_AVAIL_CRED_VAL_MASK                          0x7F

/* MME0_QM_ARB_MST_CRED_INC */
#define MME0_QM_ARB_MST_CRED_INC_VAL_SHIFT                           0
#define MME0_QM_ARB_MST_CRED_INC_VAL_MASK                            0xFFFFFFFF

/* MME0_QM_ARB_MST_CHOISE_PUSH_OFST */
#define MME0_QM_ARB_MST_CHOISE_PUSH_OFST_VAL_SHIFT                   0
#define MME0_QM_ARB_MST_CHOISE_PUSH_OFST_VAL_MASK                    0xFFFFFFFF

/* MME0_QM_ARB_SLV_MASTER_INC_CRED_OFST */
#define MME0_QM_ARB_SLV_MASTER_INC_CRED_OFST_VAL_SHIFT               0
#define MME0_QM_ARB_SLV_MASTER_INC_CRED_OFST_VAL_MASK                0xFFFFFFFF

/* MME0_QM_ARB_MST_SLAVE_EN */
#define MME0_QM_ARB_MST_SLAVE_EN_VAL_SHIFT                           0
#define MME0_QM_ARB_MST_SLAVE_EN_VAL_MASK                            0xFFFFFFFF

/* MME0_QM_ARB_MST_QUIET_PER */
#define MME0_QM_ARB_MST_QUIET_PER_VAL_SHIFT                          0
#define MME0_QM_ARB_MST_QUIET_PER_VAL_MASK                           0xFFFFFFFF

/* MME0_QM_ARB_SLV_CHOISE_WDT */
#define MME0_QM_ARB_SLV_CHOISE_WDT_VAL_SHIFT                         0
#define MME0_QM_ARB_SLV_CHOISE_WDT_VAL_MASK                          0xFFFFFFFF

/* MME0_QM_ARB_SLV_ID */
#define MME0_QM_ARB_SLV_ID_VAL_SHIFT                                 0
#define MME0_QM_ARB_SLV_ID_VAL_MASK                                  0x1F

/* MME0_QM_ARB_MSG_MAX_INFLIGHT */
#define MME0_QM_ARB_MSG_MAX_INFLIGHT_VAL_SHIFT                       0
#define MME0_QM_ARB_MSG_MAX_INFLIGHT_VAL_MASK                        0x3F

/* MME0_QM_ARB_MSG_AWUSER_31_11 */
#define MME0_QM_ARB_MSG_AWUSER_31_11_VAL_SHIFT                       0
#define MME0_QM_ARB_MSG_AWUSER_31_11_VAL_MASK                        0x1FFFFF

/* MME0_QM_ARB_MSG_AWUSER_SEC_PROP */
#define MME0_QM_ARB_MSG_AWUSER_SEC_PROP_ASID_SHIFT                   0
#define MME0_QM_ARB_MSG_AWUSER_SEC_PROP_ASID_MASK                    0x3FF
#define MME0_QM_ARB_MSG_AWUSER_SEC_PROP_MMBP_SHIFT                   10
#define MME0_QM_ARB_MSG_AWUSER_SEC_PROP_MMBP_MASK                    0x400

/* MME0_QM_ARB_MSG_AWUSER_NON_SEC_PROP */
#define MME0_QM_ARB_MSG_AWUSER_NON_SEC_PROP_ASID_SHIFT               0
#define MME0_QM_ARB_MSG_AWUSER_NON_SEC_PROP_ASID_MASK                0x3FF
#define MME0_QM_ARB_MSG_AWUSER_NON_SEC_PROP_MMBP_SHIFT               10
#define MME0_QM_ARB_MSG_AWUSER_NON_SEC_PROP_MMBP_MASK                0x400

/* MME0_QM_ARB_BASE_LO */
#define MME0_QM_ARB_BASE_LO_VAL_SHIFT                                0
#define MME0_QM_ARB_BASE_LO_VAL_MASK                                 0xFFFFFFFF

/* MME0_QM_ARB_BASE_HI */
#define MME0_QM_ARB_BASE_HI_VAL_SHIFT                                0
#define MME0_QM_ARB_BASE_HI_VAL_MASK                                 0xFFFFFFFF

/* MME0_QM_ARB_STATE_STS */
#define MME0_QM_ARB_STATE_STS_VAL_SHIFT                              0
#define MME0_QM_ARB_STATE_STS_VAL_MASK                               0xFFFFFFFF

/* MME0_QM_ARB_CHOISE_FULLNESS_STS */
#define MME0_QM_ARB_CHOISE_FULLNESS_STS_VAL_SHIFT                    0
#define MME0_QM_ARB_CHOISE_FULLNESS_STS_VAL_MASK                     0x7F

/* MME0_QM_ARB_MSG_STS */
#define MME0_QM_ARB_MSG_STS_FULL_SHIFT                               0
#define MME0_QM_ARB_MSG_STS_FULL_MASK                                0x1
#define MME0_QM_ARB_MSG_STS_NO_INFLIGHT_SHIFT                        1
#define MME0_QM_ARB_MSG_STS_NO_INFLIGHT_MASK                         0x2

/* MME0_QM_ARB_SLV_CHOISE_Q_HEAD */
#define MME0_QM_ARB_SLV_CHOISE_Q_HEAD_VAL_SHIFT                      0
#define MME0_QM_ARB_SLV_CHOISE_Q_HEAD_VAL_MASK                       0x3

/* MME0_QM_ARB_ERR_CAUSE */
#define MME0_QM_ARB_ERR_CAUSE_CHOISE_OVF_SHIFT                       0
#define MME0_QM_ARB_ERR_CAUSE_CHOISE_OVF_MASK                        0x1
#define MME0_QM_ARB_ERR_CAUSE_CHOISE_WDT_SHIFT                       1
#define MME0_QM_ARB_ERR_CAUSE_CHOISE_WDT_MASK                        0x2
#define MME0_QM_ARB_ERR_CAUSE_AXI_LBW_ERR_SHIFT                      2
#define MME0_QM_ARB_ERR_CAUSE_AXI_LBW_ERR_MASK                       0x4

/* MME0_QM_ARB_ERR_MSG_EN */
#define MME0_QM_ARB_ERR_MSG_EN_CHOISE_OVF_SHIFT                      0
#define MME0_QM_ARB_ERR_MSG_EN_CHOISE_OVF_MASK                       0x1
#define MME0_QM_ARB_ERR_MSG_EN_CHOISE_WDT_SHIFT                      1
#define MME0_QM_ARB_ERR_MSG_EN_CHOISE_WDT_MASK                       0x2
#define MME0_QM_ARB_ERR_MSG_EN_AXI_LBW_ERR_SHIFT                     2
#define MME0_QM_ARB_ERR_MSG_EN_AXI_LBW_ERR_MASK                      0x4

/* MME0_QM_ARB_ERR_STS_DRP */
#define MME0_QM_ARB_ERR_STS_DRP_VAL_SHIFT                            0
#define MME0_QM_ARB_ERR_STS_DRP_VAL_MASK                             0x3

/* MME0_QM_ARB_MST_CRED_STS */
#define MME0_QM_ARB_MST_CRED_STS_VAL_SHIFT                           0
#define MME0_QM_ARB_MST_CRED_STS_VAL_MASK                            0x7F

/* MME0_QM_CGM_CFG */
#define MME0_QM_CGM_CFG_IDLE_TH_SHIFT                                0
#define MME0_QM_CGM_CFG_IDLE_TH_MASK                                 0xFFF
#define MME0_QM_CGM_CFG_G2F_TH_SHIFT                                 16
#define MME0_QM_CGM_CFG_G2F_TH_MASK                                  0xFF0000
#define MME0_QM_CGM_CFG_CP_IDLE_MASK_SHIFT                           24
#define MME0_QM_CGM_CFG_CP_IDLE_MASK_MASK                            0x1F000000
#define MME0_QM_CGM_CFG_EN_SHIFT                                     31
#define MME0_QM_CGM_CFG_EN_MASK                                      0x80000000

/* MME0_QM_CGM_STS */
#define MME0_QM_CGM_STS_ST_SHIFT                                     0
#define MME0_QM_CGM_STS_ST_MASK                                      0x3
#define MME0_QM_CGM_STS_CG_SHIFT                                     4
#define MME0_QM_CGM_STS_CG_MASK                                      0x10
#define MME0_QM_CGM_STS_AGENT_IDLE_SHIFT                             8
#define MME0_QM_CGM_STS_AGENT_IDLE_MASK                              0x100
#define MME0_QM_CGM_STS_AXI_IDLE_SHIFT                               9
#define MME0_QM_CGM_STS_AXI_IDLE_MASK                                0x200
#define MME0_QM_CGM_STS_CP_IDLE_SHIFT                                10
#define MME0_QM_CGM_STS_CP_IDLE_MASK                                 0x400

/* MME0_QM_CGM_CFG1 */
#define MME0_QM_CGM_CFG1_MASK_TH_SHIFT                               0
#define MME0_QM_CGM_CFG1_MASK_TH_MASK                                0xFF

/* MME0_QM_LOCAL_RANGE_BASE */
#define MME0_QM_LOCAL_RANGE_BASE_VAL_SHIFT                           0
#define MME0_QM_LOCAL_RANGE_BASE_VAL_MASK                            0xFFFF

/* MME0_QM_LOCAL_RANGE_SIZE */
#define MME0_QM_LOCAL_RANGE_SIZE_VAL_SHIFT                           0
#define MME0_QM_LOCAL_RANGE_SIZE_VAL_MASK                            0xFFFF

/* MME0_QM_CSMR_STRICT_PRIO_CFG */
#define MME0_QM_CSMR_STRICT_PRIO_CFG_TYPE_SHIFT                      0
#define MME0_QM_CSMR_STRICT_PRIO_CFG_TYPE_MASK                       0x1

/* MME0_QM_HBW_RD_RATE_LIM_CFG_1 */
#define MME0_QM_HBW_RD_RATE_LIM_CFG_1_TOUT_SHIFT                     0
#define MME0_QM_HBW_RD_RATE_LIM_CFG_1_TOUT_MASK                      0xFF
#define MME0_QM_HBW_RD_RATE_LIM_CFG_1_EN_SHIFT                       31
#define MME0_QM_HBW_RD_RATE_LIM_CFG_1_EN_MASK                        0x80000000

/* MME0_QM_LBW_WR_RATE_LIM_CFG_0 */
#define MME0_QM_LBW_WR_RATE_LIM_CFG_0_RST_TOKEN_SHIFT                0
#define MME0_QM_LBW_WR_RATE_LIM_CFG_0_RST_TOKEN_MASK                 0xFF
#define MME0_QM_LBW_WR_RATE_LIM_CFG_0_SAT_SHIFT                      16
#define MME0_QM_LBW_WR_RATE_LIM_CFG_0_SAT_MASK                       0xFF0000

/* MME0_QM_LBW_WR_RATE_LIM_CFG_1 */
#define MME0_QM_LBW_WR_RATE_LIM_CFG_1_TOUT_SHIFT                     0
#define MME0_QM_LBW_WR_RATE_LIM_CFG_1_TOUT_MASK                      0xFF
#define MME0_QM_LBW_WR_RATE_LIM_CFG_1_EN_SHIFT                       31
#define MME0_QM_LBW_WR_RATE_LIM_CFG_1_EN_MASK                        0x80000000

/* MME0_QM_HBW_RD_RATE_LIM_CFG_0 */
#define MME0_QM_HBW_RD_RATE_LIM_CFG_0_RST_TOKEN_SHIFT                0
#define MME0_QM_HBW_RD_RATE_LIM_CFG_0_RST_TOKEN_MASK                 0xFF
#define MME0_QM_HBW_RD_RATE_LIM_CFG_0_SAT_SHIFT                      16
#define MME0_QM_HBW_RD_RATE_LIM_CFG_0_SAT_MASK                       0xFF0000

/* MME0_QM_GLBL_AXCACHE */
#define MME0_QM_GLBL_AXCACHE_AR_SHIFT                                0
#define MME0_QM_GLBL_AXCACHE_AR_MASK                                 0xF
#define MME0_QM_GLBL_AXCACHE_AW_SHIFT                                16
#define MME0_QM_GLBL_AXCACHE_AW_MASK                                 0xF0000

/* MME0_QM_IND_GW_APB_CFG */
#define MME0_QM_IND_GW_APB_CFG_ADDR_SHIFT                            0
#define MME0_QM_IND_GW_APB_CFG_ADDR_MASK                             0x7FFFFFFF
#define MME0_QM_IND_GW_APB_CFG_CMD_SHIFT                             31
#define MME0_QM_IND_GW_APB_CFG_CMD_MASK                              0x80000000

/* MME0_QM_IND_GW_APB_WDATA */
#define MME0_QM_IND_GW_APB_WDATA_VAL_SHIFT                           0
#define MME0_QM_IND_GW_APB_WDATA_VAL_MASK                            0xFFFFFFFF

/* MME0_QM_IND_GW_APB_RDATA */
#define MME0_QM_IND_GW_APB_RDATA_VAL_SHIFT                           0
#define MME0_QM_IND_GW_APB_RDATA_VAL_MASK                            0xFFFFFFFF

/* MME0_QM_IND_GW_APB_STATUS */
#define MME0_QM_IND_GW_APB_STATUS_RDY_SHIFT                          0
#define MME0_QM_IND_GW_APB_STATUS_RDY_MASK                           0x1
#define MME0_QM_IND_GW_APB_STATUS_ERR_SHIFT                          1
#define MME0_QM_IND_GW_APB_STATUS_ERR_MASK                           0x2

/* MME0_QM_GLBL_ERR_ADDR_LO */
#define MME0_QM_GLBL_ERR_ADDR_LO_VAL_SHIFT                           0
#define MME0_QM_GLBL_ERR_ADDR_LO_VAL_MASK                            0xFFFFFFFF

/* MME0_QM_GLBL_ERR_ADDR_HI */
#define MME0_QM_GLBL_ERR_ADDR_HI_VAL_SHIFT                           0
#define MME0_QM_GLBL_ERR_ADDR_HI_VAL_MASK                            0xFFFFFFFF

/* MME0_QM_GLBL_ERR_WDATA */
#define MME0_QM_GLBL_ERR_WDATA_VAL_SHIFT                             0
#define MME0_QM_GLBL_ERR_WDATA_VAL_MASK                              0xFFFFFFFF

/* MME0_QM_GLBL_MEM_INIT_BUSY */
#define MME0_QM_GLBL_MEM_INIT_BUSY_RBUF_SHIFT                        0
#define MME0_QM_GLBL_MEM_INIT_BUSY_RBUF_MASK                         0xF

#endif /* ASIC_REG_MME0_QM_MASKS_H_ */
