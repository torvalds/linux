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

#ifndef ASIC_REG_DMA_QM_1_REGS_H_
#define ASIC_REG_DMA_QM_1_REGS_H_

/*
 *****************************************
 *   DMA_QM_1 (Prototype: QMAN)
 *****************************************
 */

#define mmDMA_QM_1_GLBL_CFG0                                         0x408000

#define mmDMA_QM_1_GLBL_CFG1                                         0x408004

#define mmDMA_QM_1_GLBL_PROT                                         0x408008

#define mmDMA_QM_1_GLBL_ERR_CFG                                      0x40800C

#define mmDMA_QM_1_GLBL_ERR_ADDR_LO                                  0x408010

#define mmDMA_QM_1_GLBL_ERR_ADDR_HI                                  0x408014

#define mmDMA_QM_1_GLBL_ERR_WDATA                                    0x408018

#define mmDMA_QM_1_GLBL_SECURE_PROPS                                 0x40801C

#define mmDMA_QM_1_GLBL_NON_SECURE_PROPS                             0x408020

#define mmDMA_QM_1_GLBL_STS0                                         0x408024

#define mmDMA_QM_1_GLBL_STS1                                         0x408028

#define mmDMA_QM_1_PQ_BASE_LO                                        0x408060

#define mmDMA_QM_1_PQ_BASE_HI                                        0x408064

#define mmDMA_QM_1_PQ_SIZE                                           0x408068

#define mmDMA_QM_1_PQ_PI                                             0x40806C

#define mmDMA_QM_1_PQ_CI                                             0x408070

#define mmDMA_QM_1_PQ_CFG0                                           0x408074

#define mmDMA_QM_1_PQ_CFG1                                           0x408078

#define mmDMA_QM_1_PQ_ARUSER                                         0x40807C

#define mmDMA_QM_1_PQ_PUSH0                                          0x408080

#define mmDMA_QM_1_PQ_PUSH1                                          0x408084

#define mmDMA_QM_1_PQ_PUSH2                                          0x408088

#define mmDMA_QM_1_PQ_PUSH3                                          0x40808C

#define mmDMA_QM_1_PQ_STS0                                           0x408090

#define mmDMA_QM_1_PQ_STS1                                           0x408094

#define mmDMA_QM_1_PQ_RD_RATE_LIM_EN                                 0x4080A0

#define mmDMA_QM_1_PQ_RD_RATE_LIM_RST_TOKEN                          0x4080A4

#define mmDMA_QM_1_PQ_RD_RATE_LIM_SAT                                0x4080A8

#define mmDMA_QM_1_PQ_RD_RATE_LIM_TOUT                               0x4080AC

#define mmDMA_QM_1_CQ_CFG0                                           0x4080B0

#define mmDMA_QM_1_CQ_CFG1                                           0x4080B4

#define mmDMA_QM_1_CQ_ARUSER                                         0x4080B8

#define mmDMA_QM_1_CQ_PTR_LO                                         0x4080C0

#define mmDMA_QM_1_CQ_PTR_HI                                         0x4080C4

#define mmDMA_QM_1_CQ_TSIZE                                          0x4080C8

#define mmDMA_QM_1_CQ_CTL                                            0x4080CC

#define mmDMA_QM_1_CQ_PTR_LO_STS                                     0x4080D4

#define mmDMA_QM_1_CQ_PTR_HI_STS                                     0x4080D8

#define mmDMA_QM_1_CQ_TSIZE_STS                                      0x4080DC

#define mmDMA_QM_1_CQ_CTL_STS                                        0x4080E0

#define mmDMA_QM_1_CQ_STS0                                           0x4080E4

#define mmDMA_QM_1_CQ_STS1                                           0x4080E8

#define mmDMA_QM_1_CQ_RD_RATE_LIM_EN                                 0x4080F0

#define mmDMA_QM_1_CQ_RD_RATE_LIM_RST_TOKEN                          0x4080F4

#define mmDMA_QM_1_CQ_RD_RATE_LIM_SAT                                0x4080F8

#define mmDMA_QM_1_CQ_RD_RATE_LIM_TOUT                               0x4080FC

#define mmDMA_QM_1_CQ_IFIFO_CNT                                      0x408108

#define mmDMA_QM_1_CP_MSG_BASE0_ADDR_LO                              0x408120

#define mmDMA_QM_1_CP_MSG_BASE0_ADDR_HI                              0x408124

#define mmDMA_QM_1_CP_MSG_BASE1_ADDR_LO                              0x408128

#define mmDMA_QM_1_CP_MSG_BASE1_ADDR_HI                              0x40812C

#define mmDMA_QM_1_CP_MSG_BASE2_ADDR_LO                              0x408130

#define mmDMA_QM_1_CP_MSG_BASE2_ADDR_HI                              0x408134

#define mmDMA_QM_1_CP_MSG_BASE3_ADDR_LO                              0x408138

#define mmDMA_QM_1_CP_MSG_BASE3_ADDR_HI                              0x40813C

#define mmDMA_QM_1_CP_LDMA_TSIZE_OFFSET                              0x408140

#define mmDMA_QM_1_CP_LDMA_SRC_BASE_LO_OFFSET                        0x408144

#define mmDMA_QM_1_CP_LDMA_SRC_BASE_HI_OFFSET                        0x408148

#define mmDMA_QM_1_CP_LDMA_DST_BASE_LO_OFFSET                        0x40814C

#define mmDMA_QM_1_CP_LDMA_DST_BASE_HI_OFFSET                        0x408150

#define mmDMA_QM_1_CP_LDMA_COMMIT_OFFSET                             0x408154

#define mmDMA_QM_1_CP_FENCE0_RDATA                                   0x408158

#define mmDMA_QM_1_CP_FENCE1_RDATA                                   0x40815C

#define mmDMA_QM_1_CP_FENCE2_RDATA                                   0x408160

#define mmDMA_QM_1_CP_FENCE3_RDATA                                   0x408164

#define mmDMA_QM_1_CP_FENCE0_CNT                                     0x408168

#define mmDMA_QM_1_CP_FENCE1_CNT                                     0x40816C

#define mmDMA_QM_1_CP_FENCE2_CNT                                     0x408170

#define mmDMA_QM_1_CP_FENCE3_CNT                                     0x408174

#define mmDMA_QM_1_CP_STS                                            0x408178

#define mmDMA_QM_1_CP_CURRENT_INST_LO                                0x40817C

#define mmDMA_QM_1_CP_CURRENT_INST_HI                                0x408180

#define mmDMA_QM_1_CP_BARRIER_CFG                                    0x408184

#define mmDMA_QM_1_CP_DBG_0                                          0x408188

#define mmDMA_QM_1_PQ_BUF_ADDR                                       0x408300

#define mmDMA_QM_1_PQ_BUF_RDATA                                      0x408304

#define mmDMA_QM_1_CQ_BUF_ADDR                                       0x408308

#define mmDMA_QM_1_CQ_BUF_RDATA                                      0x40830C

#endif /* ASIC_REG_DMA_QM_1_REGS_H_ */

