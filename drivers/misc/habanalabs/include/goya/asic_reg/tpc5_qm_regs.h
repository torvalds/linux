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

#ifndef ASIC_REG_TPC5_QM_REGS_H_
#define ASIC_REG_TPC5_QM_REGS_H_

/*
 *****************************************
 *   TPC5_QM (Prototype: QMAN)
 *****************************************
 */

#define mmTPC5_QM_GLBL_CFG0                                          0xF48000

#define mmTPC5_QM_GLBL_CFG1                                          0xF48004

#define mmTPC5_QM_GLBL_PROT                                          0xF48008

#define mmTPC5_QM_GLBL_ERR_CFG                                       0xF4800C

#define mmTPC5_QM_GLBL_ERR_ADDR_LO                                   0xF48010

#define mmTPC5_QM_GLBL_ERR_ADDR_HI                                   0xF48014

#define mmTPC5_QM_GLBL_ERR_WDATA                                     0xF48018

#define mmTPC5_QM_GLBL_SECURE_PROPS                                  0xF4801C

#define mmTPC5_QM_GLBL_NON_SECURE_PROPS                              0xF48020

#define mmTPC5_QM_GLBL_STS0                                          0xF48024

#define mmTPC5_QM_GLBL_STS1                                          0xF48028

#define mmTPC5_QM_PQ_BASE_LO                                         0xF48060

#define mmTPC5_QM_PQ_BASE_HI                                         0xF48064

#define mmTPC5_QM_PQ_SIZE                                            0xF48068

#define mmTPC5_QM_PQ_PI                                              0xF4806C

#define mmTPC5_QM_PQ_CI                                              0xF48070

#define mmTPC5_QM_PQ_CFG0                                            0xF48074

#define mmTPC5_QM_PQ_CFG1                                            0xF48078

#define mmTPC5_QM_PQ_ARUSER                                          0xF4807C

#define mmTPC5_QM_PQ_PUSH0                                           0xF48080

#define mmTPC5_QM_PQ_PUSH1                                           0xF48084

#define mmTPC5_QM_PQ_PUSH2                                           0xF48088

#define mmTPC5_QM_PQ_PUSH3                                           0xF4808C

#define mmTPC5_QM_PQ_STS0                                            0xF48090

#define mmTPC5_QM_PQ_STS1                                            0xF48094

#define mmTPC5_QM_PQ_RD_RATE_LIM_EN                                  0xF480A0

#define mmTPC5_QM_PQ_RD_RATE_LIM_RST_TOKEN                           0xF480A4

#define mmTPC5_QM_PQ_RD_RATE_LIM_SAT                                 0xF480A8

#define mmTPC5_QM_PQ_RD_RATE_LIM_TOUT                                0xF480AC

#define mmTPC5_QM_CQ_CFG0                                            0xF480B0

#define mmTPC5_QM_CQ_CFG1                                            0xF480B4

#define mmTPC5_QM_CQ_ARUSER                                          0xF480B8

#define mmTPC5_QM_CQ_PTR_LO                                          0xF480C0

#define mmTPC5_QM_CQ_PTR_HI                                          0xF480C4

#define mmTPC5_QM_CQ_TSIZE                                           0xF480C8

#define mmTPC5_QM_CQ_CTL                                             0xF480CC

#define mmTPC5_QM_CQ_PTR_LO_STS                                      0xF480D4

#define mmTPC5_QM_CQ_PTR_HI_STS                                      0xF480D8

#define mmTPC5_QM_CQ_TSIZE_STS                                       0xF480DC

#define mmTPC5_QM_CQ_CTL_STS                                         0xF480E0

#define mmTPC5_QM_CQ_STS0                                            0xF480E4

#define mmTPC5_QM_CQ_STS1                                            0xF480E8

#define mmTPC5_QM_CQ_RD_RATE_LIM_EN                                  0xF480F0

#define mmTPC5_QM_CQ_RD_RATE_LIM_RST_TOKEN                           0xF480F4

#define mmTPC5_QM_CQ_RD_RATE_LIM_SAT                                 0xF480F8

#define mmTPC5_QM_CQ_RD_RATE_LIM_TOUT                                0xF480FC

#define mmTPC5_QM_CQ_IFIFO_CNT                                       0xF48108

#define mmTPC5_QM_CP_MSG_BASE0_ADDR_LO                               0xF48120

#define mmTPC5_QM_CP_MSG_BASE0_ADDR_HI                               0xF48124

#define mmTPC5_QM_CP_MSG_BASE1_ADDR_LO                               0xF48128

#define mmTPC5_QM_CP_MSG_BASE1_ADDR_HI                               0xF4812C

#define mmTPC5_QM_CP_MSG_BASE2_ADDR_LO                               0xF48130

#define mmTPC5_QM_CP_MSG_BASE2_ADDR_HI                               0xF48134

#define mmTPC5_QM_CP_MSG_BASE3_ADDR_LO                               0xF48138

#define mmTPC5_QM_CP_MSG_BASE3_ADDR_HI                               0xF4813C

#define mmTPC5_QM_CP_LDMA_TSIZE_OFFSET                               0xF48140

#define mmTPC5_QM_CP_LDMA_SRC_BASE_LO_OFFSET                         0xF48144

#define mmTPC5_QM_CP_LDMA_SRC_BASE_HI_OFFSET                         0xF48148

#define mmTPC5_QM_CP_LDMA_DST_BASE_LO_OFFSET                         0xF4814C

#define mmTPC5_QM_CP_LDMA_DST_BASE_HI_OFFSET                         0xF48150

#define mmTPC5_QM_CP_LDMA_COMMIT_OFFSET                              0xF48154

#define mmTPC5_QM_CP_FENCE0_RDATA                                    0xF48158

#define mmTPC5_QM_CP_FENCE1_RDATA                                    0xF4815C

#define mmTPC5_QM_CP_FENCE2_RDATA                                    0xF48160

#define mmTPC5_QM_CP_FENCE3_RDATA                                    0xF48164

#define mmTPC5_QM_CP_FENCE0_CNT                                      0xF48168

#define mmTPC5_QM_CP_FENCE1_CNT                                      0xF4816C

#define mmTPC5_QM_CP_FENCE2_CNT                                      0xF48170

#define mmTPC5_QM_CP_FENCE3_CNT                                      0xF48174

#define mmTPC5_QM_CP_STS                                             0xF48178

#define mmTPC5_QM_CP_CURRENT_INST_LO                                 0xF4817C

#define mmTPC5_QM_CP_CURRENT_INST_HI                                 0xF48180

#define mmTPC5_QM_CP_BARRIER_CFG                                     0xF48184

#define mmTPC5_QM_CP_DBG_0                                           0xF48188

#define mmTPC5_QM_PQ_BUF_ADDR                                        0xF48300

#define mmTPC5_QM_PQ_BUF_RDATA                                       0xF48304

#define mmTPC5_QM_CQ_BUF_ADDR                                        0xF48308

#define mmTPC5_QM_CQ_BUF_RDATA                                       0xF4830C

#endif /* ASIC_REG_TPC5_QM_REGS_H_ */
