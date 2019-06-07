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

#ifndef ASIC_REG_TPC2_QM_REGS_H_
#define ASIC_REG_TPC2_QM_REGS_H_

/*
 *****************************************
 *   TPC2_QM (Prototype: QMAN)
 *****************************************
 */

#define mmTPC2_QM_GLBL_CFG0                                          0xE88000

#define mmTPC2_QM_GLBL_CFG1                                          0xE88004

#define mmTPC2_QM_GLBL_PROT                                          0xE88008

#define mmTPC2_QM_GLBL_ERR_CFG                                       0xE8800C

#define mmTPC2_QM_GLBL_ERR_ADDR_LO                                   0xE88010

#define mmTPC2_QM_GLBL_ERR_ADDR_HI                                   0xE88014

#define mmTPC2_QM_GLBL_ERR_WDATA                                     0xE88018

#define mmTPC2_QM_GLBL_SECURE_PROPS                                  0xE8801C

#define mmTPC2_QM_GLBL_NON_SECURE_PROPS                              0xE88020

#define mmTPC2_QM_GLBL_STS0                                          0xE88024

#define mmTPC2_QM_GLBL_STS1                                          0xE88028

#define mmTPC2_QM_PQ_BASE_LO                                         0xE88060

#define mmTPC2_QM_PQ_BASE_HI                                         0xE88064

#define mmTPC2_QM_PQ_SIZE                                            0xE88068

#define mmTPC2_QM_PQ_PI                                              0xE8806C

#define mmTPC2_QM_PQ_CI                                              0xE88070

#define mmTPC2_QM_PQ_CFG0                                            0xE88074

#define mmTPC2_QM_PQ_CFG1                                            0xE88078

#define mmTPC2_QM_PQ_ARUSER                                          0xE8807C

#define mmTPC2_QM_PQ_PUSH0                                           0xE88080

#define mmTPC2_QM_PQ_PUSH1                                           0xE88084

#define mmTPC2_QM_PQ_PUSH2                                           0xE88088

#define mmTPC2_QM_PQ_PUSH3                                           0xE8808C

#define mmTPC2_QM_PQ_STS0                                            0xE88090

#define mmTPC2_QM_PQ_STS1                                            0xE88094

#define mmTPC2_QM_PQ_RD_RATE_LIM_EN                                  0xE880A0

#define mmTPC2_QM_PQ_RD_RATE_LIM_RST_TOKEN                           0xE880A4

#define mmTPC2_QM_PQ_RD_RATE_LIM_SAT                                 0xE880A8

#define mmTPC2_QM_PQ_RD_RATE_LIM_TOUT                                0xE880AC

#define mmTPC2_QM_CQ_CFG0                                            0xE880B0

#define mmTPC2_QM_CQ_CFG1                                            0xE880B4

#define mmTPC2_QM_CQ_ARUSER                                          0xE880B8

#define mmTPC2_QM_CQ_PTR_LO                                          0xE880C0

#define mmTPC2_QM_CQ_PTR_HI                                          0xE880C4

#define mmTPC2_QM_CQ_TSIZE                                           0xE880C8

#define mmTPC2_QM_CQ_CTL                                             0xE880CC

#define mmTPC2_QM_CQ_PTR_LO_STS                                      0xE880D4

#define mmTPC2_QM_CQ_PTR_HI_STS                                      0xE880D8

#define mmTPC2_QM_CQ_TSIZE_STS                                       0xE880DC

#define mmTPC2_QM_CQ_CTL_STS                                         0xE880E0

#define mmTPC2_QM_CQ_STS0                                            0xE880E4

#define mmTPC2_QM_CQ_STS1                                            0xE880E8

#define mmTPC2_QM_CQ_RD_RATE_LIM_EN                                  0xE880F0

#define mmTPC2_QM_CQ_RD_RATE_LIM_RST_TOKEN                           0xE880F4

#define mmTPC2_QM_CQ_RD_RATE_LIM_SAT                                 0xE880F8

#define mmTPC2_QM_CQ_RD_RATE_LIM_TOUT                                0xE880FC

#define mmTPC2_QM_CQ_IFIFO_CNT                                       0xE88108

#define mmTPC2_QM_CP_MSG_BASE0_ADDR_LO                               0xE88120

#define mmTPC2_QM_CP_MSG_BASE0_ADDR_HI                               0xE88124

#define mmTPC2_QM_CP_MSG_BASE1_ADDR_LO                               0xE88128

#define mmTPC2_QM_CP_MSG_BASE1_ADDR_HI                               0xE8812C

#define mmTPC2_QM_CP_MSG_BASE2_ADDR_LO                               0xE88130

#define mmTPC2_QM_CP_MSG_BASE2_ADDR_HI                               0xE88134

#define mmTPC2_QM_CP_MSG_BASE3_ADDR_LO                               0xE88138

#define mmTPC2_QM_CP_MSG_BASE3_ADDR_HI                               0xE8813C

#define mmTPC2_QM_CP_LDMA_TSIZE_OFFSET                               0xE88140

#define mmTPC2_QM_CP_LDMA_SRC_BASE_LO_OFFSET                         0xE88144

#define mmTPC2_QM_CP_LDMA_SRC_BASE_HI_OFFSET                         0xE88148

#define mmTPC2_QM_CP_LDMA_DST_BASE_LO_OFFSET                         0xE8814C

#define mmTPC2_QM_CP_LDMA_DST_BASE_HI_OFFSET                         0xE88150

#define mmTPC2_QM_CP_LDMA_COMMIT_OFFSET                              0xE88154

#define mmTPC2_QM_CP_FENCE0_RDATA                                    0xE88158

#define mmTPC2_QM_CP_FENCE1_RDATA                                    0xE8815C

#define mmTPC2_QM_CP_FENCE2_RDATA                                    0xE88160

#define mmTPC2_QM_CP_FENCE3_RDATA                                    0xE88164

#define mmTPC2_QM_CP_FENCE0_CNT                                      0xE88168

#define mmTPC2_QM_CP_FENCE1_CNT                                      0xE8816C

#define mmTPC2_QM_CP_FENCE2_CNT                                      0xE88170

#define mmTPC2_QM_CP_FENCE3_CNT                                      0xE88174

#define mmTPC2_QM_CP_STS                                             0xE88178

#define mmTPC2_QM_CP_CURRENT_INST_LO                                 0xE8817C

#define mmTPC2_QM_CP_CURRENT_INST_HI                                 0xE88180

#define mmTPC2_QM_CP_BARRIER_CFG                                     0xE88184

#define mmTPC2_QM_CP_DBG_0                                           0xE88188

#define mmTPC2_QM_PQ_BUF_ADDR                                        0xE88300

#define mmTPC2_QM_PQ_BUF_RDATA                                       0xE88304

#define mmTPC2_QM_CQ_BUF_ADDR                                        0xE88308

#define mmTPC2_QM_CQ_BUF_RDATA                                       0xE8830C

#endif /* ASIC_REG_TPC2_QM_REGS_H_ */
