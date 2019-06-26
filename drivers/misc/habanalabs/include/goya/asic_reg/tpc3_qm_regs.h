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

#ifndef ASIC_REG_TPC3_QM_REGS_H_
#define ASIC_REG_TPC3_QM_REGS_H_

/*
 *****************************************
 *   TPC3_QM (Prototype: QMAN)
 *****************************************
 */

#define mmTPC3_QM_GLBL_CFG0                                          0xEC8000

#define mmTPC3_QM_GLBL_CFG1                                          0xEC8004

#define mmTPC3_QM_GLBL_PROT                                          0xEC8008

#define mmTPC3_QM_GLBL_ERR_CFG                                       0xEC800C

#define mmTPC3_QM_GLBL_ERR_ADDR_LO                                   0xEC8010

#define mmTPC3_QM_GLBL_ERR_ADDR_HI                                   0xEC8014

#define mmTPC3_QM_GLBL_ERR_WDATA                                     0xEC8018

#define mmTPC3_QM_GLBL_SECURE_PROPS                                  0xEC801C

#define mmTPC3_QM_GLBL_NON_SECURE_PROPS                              0xEC8020

#define mmTPC3_QM_GLBL_STS0                                          0xEC8024

#define mmTPC3_QM_GLBL_STS1                                          0xEC8028

#define mmTPC3_QM_PQ_BASE_LO                                         0xEC8060

#define mmTPC3_QM_PQ_BASE_HI                                         0xEC8064

#define mmTPC3_QM_PQ_SIZE                                            0xEC8068

#define mmTPC3_QM_PQ_PI                                              0xEC806C

#define mmTPC3_QM_PQ_CI                                              0xEC8070

#define mmTPC3_QM_PQ_CFG0                                            0xEC8074

#define mmTPC3_QM_PQ_CFG1                                            0xEC8078

#define mmTPC3_QM_PQ_ARUSER                                          0xEC807C

#define mmTPC3_QM_PQ_PUSH0                                           0xEC8080

#define mmTPC3_QM_PQ_PUSH1                                           0xEC8084

#define mmTPC3_QM_PQ_PUSH2                                           0xEC8088

#define mmTPC3_QM_PQ_PUSH3                                           0xEC808C

#define mmTPC3_QM_PQ_STS0                                            0xEC8090

#define mmTPC3_QM_PQ_STS1                                            0xEC8094

#define mmTPC3_QM_PQ_RD_RATE_LIM_EN                                  0xEC80A0

#define mmTPC3_QM_PQ_RD_RATE_LIM_RST_TOKEN                           0xEC80A4

#define mmTPC3_QM_PQ_RD_RATE_LIM_SAT                                 0xEC80A8

#define mmTPC3_QM_PQ_RD_RATE_LIM_TOUT                                0xEC80AC

#define mmTPC3_QM_CQ_CFG0                                            0xEC80B0

#define mmTPC3_QM_CQ_CFG1                                            0xEC80B4

#define mmTPC3_QM_CQ_ARUSER                                          0xEC80B8

#define mmTPC3_QM_CQ_PTR_LO                                          0xEC80C0

#define mmTPC3_QM_CQ_PTR_HI                                          0xEC80C4

#define mmTPC3_QM_CQ_TSIZE                                           0xEC80C8

#define mmTPC3_QM_CQ_CTL                                             0xEC80CC

#define mmTPC3_QM_CQ_PTR_LO_STS                                      0xEC80D4

#define mmTPC3_QM_CQ_PTR_HI_STS                                      0xEC80D8

#define mmTPC3_QM_CQ_TSIZE_STS                                       0xEC80DC

#define mmTPC3_QM_CQ_CTL_STS                                         0xEC80E0

#define mmTPC3_QM_CQ_STS0                                            0xEC80E4

#define mmTPC3_QM_CQ_STS1                                            0xEC80E8

#define mmTPC3_QM_CQ_RD_RATE_LIM_EN                                  0xEC80F0

#define mmTPC3_QM_CQ_RD_RATE_LIM_RST_TOKEN                           0xEC80F4

#define mmTPC3_QM_CQ_RD_RATE_LIM_SAT                                 0xEC80F8

#define mmTPC3_QM_CQ_RD_RATE_LIM_TOUT                                0xEC80FC

#define mmTPC3_QM_CQ_IFIFO_CNT                                       0xEC8108

#define mmTPC3_QM_CP_MSG_BASE0_ADDR_LO                               0xEC8120

#define mmTPC3_QM_CP_MSG_BASE0_ADDR_HI                               0xEC8124

#define mmTPC3_QM_CP_MSG_BASE1_ADDR_LO                               0xEC8128

#define mmTPC3_QM_CP_MSG_BASE1_ADDR_HI                               0xEC812C

#define mmTPC3_QM_CP_MSG_BASE2_ADDR_LO                               0xEC8130

#define mmTPC3_QM_CP_MSG_BASE2_ADDR_HI                               0xEC8134

#define mmTPC3_QM_CP_MSG_BASE3_ADDR_LO                               0xEC8138

#define mmTPC3_QM_CP_MSG_BASE3_ADDR_HI                               0xEC813C

#define mmTPC3_QM_CP_LDMA_TSIZE_OFFSET                               0xEC8140

#define mmTPC3_QM_CP_LDMA_SRC_BASE_LO_OFFSET                         0xEC8144

#define mmTPC3_QM_CP_LDMA_SRC_BASE_HI_OFFSET                         0xEC8148

#define mmTPC3_QM_CP_LDMA_DST_BASE_LO_OFFSET                         0xEC814C

#define mmTPC3_QM_CP_LDMA_DST_BASE_HI_OFFSET                         0xEC8150

#define mmTPC3_QM_CP_LDMA_COMMIT_OFFSET                              0xEC8154

#define mmTPC3_QM_CP_FENCE0_RDATA                                    0xEC8158

#define mmTPC3_QM_CP_FENCE1_RDATA                                    0xEC815C

#define mmTPC3_QM_CP_FENCE2_RDATA                                    0xEC8160

#define mmTPC3_QM_CP_FENCE3_RDATA                                    0xEC8164

#define mmTPC3_QM_CP_FENCE0_CNT                                      0xEC8168

#define mmTPC3_QM_CP_FENCE1_CNT                                      0xEC816C

#define mmTPC3_QM_CP_FENCE2_CNT                                      0xEC8170

#define mmTPC3_QM_CP_FENCE3_CNT                                      0xEC8174

#define mmTPC3_QM_CP_STS                                             0xEC8178

#define mmTPC3_QM_CP_CURRENT_INST_LO                                 0xEC817C

#define mmTPC3_QM_CP_CURRENT_INST_HI                                 0xEC8180

#define mmTPC3_QM_CP_BARRIER_CFG                                     0xEC8184

#define mmTPC3_QM_CP_DBG_0                                           0xEC8188

#define mmTPC3_QM_PQ_BUF_ADDR                                        0xEC8300

#define mmTPC3_QM_PQ_BUF_RDATA                                       0xEC8304

#define mmTPC3_QM_CQ_BUF_ADDR                                        0xEC8308

#define mmTPC3_QM_CQ_BUF_RDATA                                       0xEC830C

#endif /* ASIC_REG_TPC3_QM_REGS_H_ */

