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

#ifndef ASIC_REG_TPC7_QM_REGS_H_
#define ASIC_REG_TPC7_QM_REGS_H_

/*
 *****************************************
 *   TPC7_QM (Prototype: QMAN)
 *****************************************
 */

#define mmTPC7_QM_GLBL_CFG0                                          0xFC8000

#define mmTPC7_QM_GLBL_CFG1                                          0xFC8004

#define mmTPC7_QM_GLBL_PROT                                          0xFC8008

#define mmTPC7_QM_GLBL_ERR_CFG                                       0xFC800C

#define mmTPC7_QM_GLBL_ERR_ADDR_LO                                   0xFC8010

#define mmTPC7_QM_GLBL_ERR_ADDR_HI                                   0xFC8014

#define mmTPC7_QM_GLBL_ERR_WDATA                                     0xFC8018

#define mmTPC7_QM_GLBL_SECURE_PROPS                                  0xFC801C

#define mmTPC7_QM_GLBL_NON_SECURE_PROPS                              0xFC8020

#define mmTPC7_QM_GLBL_STS0                                          0xFC8024

#define mmTPC7_QM_GLBL_STS1                                          0xFC8028

#define mmTPC7_QM_PQ_BASE_LO                                         0xFC8060

#define mmTPC7_QM_PQ_BASE_HI                                         0xFC8064

#define mmTPC7_QM_PQ_SIZE                                            0xFC8068

#define mmTPC7_QM_PQ_PI                                              0xFC806C

#define mmTPC7_QM_PQ_CI                                              0xFC8070

#define mmTPC7_QM_PQ_CFG0                                            0xFC8074

#define mmTPC7_QM_PQ_CFG1                                            0xFC8078

#define mmTPC7_QM_PQ_ARUSER                                          0xFC807C

#define mmTPC7_QM_PQ_PUSH0                                           0xFC8080

#define mmTPC7_QM_PQ_PUSH1                                           0xFC8084

#define mmTPC7_QM_PQ_PUSH2                                           0xFC8088

#define mmTPC7_QM_PQ_PUSH3                                           0xFC808C

#define mmTPC7_QM_PQ_STS0                                            0xFC8090

#define mmTPC7_QM_PQ_STS1                                            0xFC8094

#define mmTPC7_QM_PQ_RD_RATE_LIM_EN                                  0xFC80A0

#define mmTPC7_QM_PQ_RD_RATE_LIM_RST_TOKEN                           0xFC80A4

#define mmTPC7_QM_PQ_RD_RATE_LIM_SAT                                 0xFC80A8

#define mmTPC7_QM_PQ_RD_RATE_LIM_TOUT                                0xFC80AC

#define mmTPC7_QM_CQ_CFG0                                            0xFC80B0

#define mmTPC7_QM_CQ_CFG1                                            0xFC80B4

#define mmTPC7_QM_CQ_ARUSER                                          0xFC80B8

#define mmTPC7_QM_CQ_PTR_LO                                          0xFC80C0

#define mmTPC7_QM_CQ_PTR_HI                                          0xFC80C4

#define mmTPC7_QM_CQ_TSIZE                                           0xFC80C8

#define mmTPC7_QM_CQ_CTL                                             0xFC80CC

#define mmTPC7_QM_CQ_PTR_LO_STS                                      0xFC80D4

#define mmTPC7_QM_CQ_PTR_HI_STS                                      0xFC80D8

#define mmTPC7_QM_CQ_TSIZE_STS                                       0xFC80DC

#define mmTPC7_QM_CQ_CTL_STS                                         0xFC80E0

#define mmTPC7_QM_CQ_STS0                                            0xFC80E4

#define mmTPC7_QM_CQ_STS1                                            0xFC80E8

#define mmTPC7_QM_CQ_RD_RATE_LIM_EN                                  0xFC80F0

#define mmTPC7_QM_CQ_RD_RATE_LIM_RST_TOKEN                           0xFC80F4

#define mmTPC7_QM_CQ_RD_RATE_LIM_SAT                                 0xFC80F8

#define mmTPC7_QM_CQ_RD_RATE_LIM_TOUT                                0xFC80FC

#define mmTPC7_QM_CQ_IFIFO_CNT                                       0xFC8108

#define mmTPC7_QM_CP_MSG_BASE0_ADDR_LO                               0xFC8120

#define mmTPC7_QM_CP_MSG_BASE0_ADDR_HI                               0xFC8124

#define mmTPC7_QM_CP_MSG_BASE1_ADDR_LO                               0xFC8128

#define mmTPC7_QM_CP_MSG_BASE1_ADDR_HI                               0xFC812C

#define mmTPC7_QM_CP_MSG_BASE2_ADDR_LO                               0xFC8130

#define mmTPC7_QM_CP_MSG_BASE2_ADDR_HI                               0xFC8134

#define mmTPC7_QM_CP_MSG_BASE3_ADDR_LO                               0xFC8138

#define mmTPC7_QM_CP_MSG_BASE3_ADDR_HI                               0xFC813C

#define mmTPC7_QM_CP_LDMA_TSIZE_OFFSET                               0xFC8140

#define mmTPC7_QM_CP_LDMA_SRC_BASE_LO_OFFSET                         0xFC8144

#define mmTPC7_QM_CP_LDMA_SRC_BASE_HI_OFFSET                         0xFC8148

#define mmTPC7_QM_CP_LDMA_DST_BASE_LO_OFFSET                         0xFC814C

#define mmTPC7_QM_CP_LDMA_DST_BASE_HI_OFFSET                         0xFC8150

#define mmTPC7_QM_CP_LDMA_COMMIT_OFFSET                              0xFC8154

#define mmTPC7_QM_CP_FENCE0_RDATA                                    0xFC8158

#define mmTPC7_QM_CP_FENCE1_RDATA                                    0xFC815C

#define mmTPC7_QM_CP_FENCE2_RDATA                                    0xFC8160

#define mmTPC7_QM_CP_FENCE3_RDATA                                    0xFC8164

#define mmTPC7_QM_CP_FENCE0_CNT                                      0xFC8168

#define mmTPC7_QM_CP_FENCE1_CNT                                      0xFC816C

#define mmTPC7_QM_CP_FENCE2_CNT                                      0xFC8170

#define mmTPC7_QM_CP_FENCE3_CNT                                      0xFC8174

#define mmTPC7_QM_CP_STS                                             0xFC8178

#define mmTPC7_QM_CP_CURRENT_INST_LO                                 0xFC817C

#define mmTPC7_QM_CP_CURRENT_INST_HI                                 0xFC8180

#define mmTPC7_QM_CP_BARRIER_CFG                                     0xFC8184

#define mmTPC7_QM_CP_DBG_0                                           0xFC8188

#define mmTPC7_QM_PQ_BUF_ADDR                                        0xFC8300

#define mmTPC7_QM_PQ_BUF_RDATA                                       0xFC8304

#define mmTPC7_QM_CQ_BUF_ADDR                                        0xFC8308

#define mmTPC7_QM_CQ_BUF_RDATA                                       0xFC830C

#endif /* ASIC_REG_TPC7_QM_REGS_H_ */
