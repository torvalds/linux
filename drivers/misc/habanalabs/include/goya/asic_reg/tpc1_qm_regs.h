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

#ifndef ASIC_REG_TPC1_QM_REGS_H_
#define ASIC_REG_TPC1_QM_REGS_H_

/*
 *****************************************
 *   TPC1_QM (Prototype: QMAN)
 *****************************************
 */

#define mmTPC1_QM_GLBL_CFG0                                          0xE48000

#define mmTPC1_QM_GLBL_CFG1                                          0xE48004

#define mmTPC1_QM_GLBL_PROT                                          0xE48008

#define mmTPC1_QM_GLBL_ERR_CFG                                       0xE4800C

#define mmTPC1_QM_GLBL_ERR_ADDR_LO                                   0xE48010

#define mmTPC1_QM_GLBL_ERR_ADDR_HI                                   0xE48014

#define mmTPC1_QM_GLBL_ERR_WDATA                                     0xE48018

#define mmTPC1_QM_GLBL_SECURE_PROPS                                  0xE4801C

#define mmTPC1_QM_GLBL_NON_SECURE_PROPS                              0xE48020

#define mmTPC1_QM_GLBL_STS0                                          0xE48024

#define mmTPC1_QM_GLBL_STS1                                          0xE48028

#define mmTPC1_QM_PQ_BASE_LO                                         0xE48060

#define mmTPC1_QM_PQ_BASE_HI                                         0xE48064

#define mmTPC1_QM_PQ_SIZE                                            0xE48068

#define mmTPC1_QM_PQ_PI                                              0xE4806C

#define mmTPC1_QM_PQ_CI                                              0xE48070

#define mmTPC1_QM_PQ_CFG0                                            0xE48074

#define mmTPC1_QM_PQ_CFG1                                            0xE48078

#define mmTPC1_QM_PQ_ARUSER                                          0xE4807C

#define mmTPC1_QM_PQ_PUSH0                                           0xE48080

#define mmTPC1_QM_PQ_PUSH1                                           0xE48084

#define mmTPC1_QM_PQ_PUSH2                                           0xE48088

#define mmTPC1_QM_PQ_PUSH3                                           0xE4808C

#define mmTPC1_QM_PQ_STS0                                            0xE48090

#define mmTPC1_QM_PQ_STS1                                            0xE48094

#define mmTPC1_QM_PQ_RD_RATE_LIM_EN                                  0xE480A0

#define mmTPC1_QM_PQ_RD_RATE_LIM_RST_TOKEN                           0xE480A4

#define mmTPC1_QM_PQ_RD_RATE_LIM_SAT                                 0xE480A8

#define mmTPC1_QM_PQ_RD_RATE_LIM_TOUT                                0xE480AC

#define mmTPC1_QM_CQ_CFG0                                            0xE480B0

#define mmTPC1_QM_CQ_CFG1                                            0xE480B4

#define mmTPC1_QM_CQ_ARUSER                                          0xE480B8

#define mmTPC1_QM_CQ_PTR_LO                                          0xE480C0

#define mmTPC1_QM_CQ_PTR_HI                                          0xE480C4

#define mmTPC1_QM_CQ_TSIZE                                           0xE480C8

#define mmTPC1_QM_CQ_CTL                                             0xE480CC

#define mmTPC1_QM_CQ_PTR_LO_STS                                      0xE480D4

#define mmTPC1_QM_CQ_PTR_HI_STS                                      0xE480D8

#define mmTPC1_QM_CQ_TSIZE_STS                                       0xE480DC

#define mmTPC1_QM_CQ_CTL_STS                                         0xE480E0

#define mmTPC1_QM_CQ_STS0                                            0xE480E4

#define mmTPC1_QM_CQ_STS1                                            0xE480E8

#define mmTPC1_QM_CQ_RD_RATE_LIM_EN                                  0xE480F0

#define mmTPC1_QM_CQ_RD_RATE_LIM_RST_TOKEN                           0xE480F4

#define mmTPC1_QM_CQ_RD_RATE_LIM_SAT                                 0xE480F8

#define mmTPC1_QM_CQ_RD_RATE_LIM_TOUT                                0xE480FC

#define mmTPC1_QM_CQ_IFIFO_CNT                                       0xE48108

#define mmTPC1_QM_CP_MSG_BASE0_ADDR_LO                               0xE48120

#define mmTPC1_QM_CP_MSG_BASE0_ADDR_HI                               0xE48124

#define mmTPC1_QM_CP_MSG_BASE1_ADDR_LO                               0xE48128

#define mmTPC1_QM_CP_MSG_BASE1_ADDR_HI                               0xE4812C

#define mmTPC1_QM_CP_MSG_BASE2_ADDR_LO                               0xE48130

#define mmTPC1_QM_CP_MSG_BASE2_ADDR_HI                               0xE48134

#define mmTPC1_QM_CP_MSG_BASE3_ADDR_LO                               0xE48138

#define mmTPC1_QM_CP_MSG_BASE3_ADDR_HI                               0xE4813C

#define mmTPC1_QM_CP_LDMA_TSIZE_OFFSET                               0xE48140

#define mmTPC1_QM_CP_LDMA_SRC_BASE_LO_OFFSET                         0xE48144

#define mmTPC1_QM_CP_LDMA_SRC_BASE_HI_OFFSET                         0xE48148

#define mmTPC1_QM_CP_LDMA_DST_BASE_LO_OFFSET                         0xE4814C

#define mmTPC1_QM_CP_LDMA_DST_BASE_HI_OFFSET                         0xE48150

#define mmTPC1_QM_CP_LDMA_COMMIT_OFFSET                              0xE48154

#define mmTPC1_QM_CP_FENCE0_RDATA                                    0xE48158

#define mmTPC1_QM_CP_FENCE1_RDATA                                    0xE4815C

#define mmTPC1_QM_CP_FENCE2_RDATA                                    0xE48160

#define mmTPC1_QM_CP_FENCE3_RDATA                                    0xE48164

#define mmTPC1_QM_CP_FENCE0_CNT                                      0xE48168

#define mmTPC1_QM_CP_FENCE1_CNT                                      0xE4816C

#define mmTPC1_QM_CP_FENCE2_CNT                                      0xE48170

#define mmTPC1_QM_CP_FENCE3_CNT                                      0xE48174

#define mmTPC1_QM_CP_STS                                             0xE48178

#define mmTPC1_QM_CP_CURRENT_INST_LO                                 0xE4817C

#define mmTPC1_QM_CP_CURRENT_INST_HI                                 0xE48180

#define mmTPC1_QM_CP_BARRIER_CFG                                     0xE48184

#define mmTPC1_QM_CP_DBG_0                                           0xE48188

#define mmTPC1_QM_PQ_BUF_ADDR                                        0xE48300

#define mmTPC1_QM_PQ_BUF_RDATA                                       0xE48304

#define mmTPC1_QM_CQ_BUF_ADDR                                        0xE48308

#define mmTPC1_QM_CQ_BUF_RDATA                                       0xE4830C

#endif /* ASIC_REG_TPC1_QM_REGS_H_ */
