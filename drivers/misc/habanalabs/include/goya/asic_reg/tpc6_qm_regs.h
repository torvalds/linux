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

#ifndef ASIC_REG_TPC6_QM_REGS_H_
#define ASIC_REG_TPC6_QM_REGS_H_

/*
 *****************************************
 *   TPC6_QM (Prototype: QMAN)
 *****************************************
 */

#define mmTPC6_QM_GLBL_CFG0                                          0xF88000

#define mmTPC6_QM_GLBL_CFG1                                          0xF88004

#define mmTPC6_QM_GLBL_PROT                                          0xF88008

#define mmTPC6_QM_GLBL_ERR_CFG                                       0xF8800C

#define mmTPC6_QM_GLBL_ERR_ADDR_LO                                   0xF88010

#define mmTPC6_QM_GLBL_ERR_ADDR_HI                                   0xF88014

#define mmTPC6_QM_GLBL_ERR_WDATA                                     0xF88018

#define mmTPC6_QM_GLBL_SECURE_PROPS                                  0xF8801C

#define mmTPC6_QM_GLBL_NON_SECURE_PROPS                              0xF88020

#define mmTPC6_QM_GLBL_STS0                                          0xF88024

#define mmTPC6_QM_GLBL_STS1                                          0xF88028

#define mmTPC6_QM_PQ_BASE_LO                                         0xF88060

#define mmTPC6_QM_PQ_BASE_HI                                         0xF88064

#define mmTPC6_QM_PQ_SIZE                                            0xF88068

#define mmTPC6_QM_PQ_PI                                              0xF8806C

#define mmTPC6_QM_PQ_CI                                              0xF88070

#define mmTPC6_QM_PQ_CFG0                                            0xF88074

#define mmTPC6_QM_PQ_CFG1                                            0xF88078

#define mmTPC6_QM_PQ_ARUSER                                          0xF8807C

#define mmTPC6_QM_PQ_PUSH0                                           0xF88080

#define mmTPC6_QM_PQ_PUSH1                                           0xF88084

#define mmTPC6_QM_PQ_PUSH2                                           0xF88088

#define mmTPC6_QM_PQ_PUSH3                                           0xF8808C

#define mmTPC6_QM_PQ_STS0                                            0xF88090

#define mmTPC6_QM_PQ_STS1                                            0xF88094

#define mmTPC6_QM_PQ_RD_RATE_LIM_EN                                  0xF880A0

#define mmTPC6_QM_PQ_RD_RATE_LIM_RST_TOKEN                           0xF880A4

#define mmTPC6_QM_PQ_RD_RATE_LIM_SAT                                 0xF880A8

#define mmTPC6_QM_PQ_RD_RATE_LIM_TOUT                                0xF880AC

#define mmTPC6_QM_CQ_CFG0                                            0xF880B0

#define mmTPC6_QM_CQ_CFG1                                            0xF880B4

#define mmTPC6_QM_CQ_ARUSER                                          0xF880B8

#define mmTPC6_QM_CQ_PTR_LO                                          0xF880C0

#define mmTPC6_QM_CQ_PTR_HI                                          0xF880C4

#define mmTPC6_QM_CQ_TSIZE                                           0xF880C8

#define mmTPC6_QM_CQ_CTL                                             0xF880CC

#define mmTPC6_QM_CQ_PTR_LO_STS                                      0xF880D4

#define mmTPC6_QM_CQ_PTR_HI_STS                                      0xF880D8

#define mmTPC6_QM_CQ_TSIZE_STS                                       0xF880DC

#define mmTPC6_QM_CQ_CTL_STS                                         0xF880E0

#define mmTPC6_QM_CQ_STS0                                            0xF880E4

#define mmTPC6_QM_CQ_STS1                                            0xF880E8

#define mmTPC6_QM_CQ_RD_RATE_LIM_EN                                  0xF880F0

#define mmTPC6_QM_CQ_RD_RATE_LIM_RST_TOKEN                           0xF880F4

#define mmTPC6_QM_CQ_RD_RATE_LIM_SAT                                 0xF880F8

#define mmTPC6_QM_CQ_RD_RATE_LIM_TOUT                                0xF880FC

#define mmTPC6_QM_CQ_IFIFO_CNT                                       0xF88108

#define mmTPC6_QM_CP_MSG_BASE0_ADDR_LO                               0xF88120

#define mmTPC6_QM_CP_MSG_BASE0_ADDR_HI                               0xF88124

#define mmTPC6_QM_CP_MSG_BASE1_ADDR_LO                               0xF88128

#define mmTPC6_QM_CP_MSG_BASE1_ADDR_HI                               0xF8812C

#define mmTPC6_QM_CP_MSG_BASE2_ADDR_LO                               0xF88130

#define mmTPC6_QM_CP_MSG_BASE2_ADDR_HI                               0xF88134

#define mmTPC6_QM_CP_MSG_BASE3_ADDR_LO                               0xF88138

#define mmTPC6_QM_CP_MSG_BASE3_ADDR_HI                               0xF8813C

#define mmTPC6_QM_CP_LDMA_TSIZE_OFFSET                               0xF88140

#define mmTPC6_QM_CP_LDMA_SRC_BASE_LO_OFFSET                         0xF88144

#define mmTPC6_QM_CP_LDMA_SRC_BASE_HI_OFFSET                         0xF88148

#define mmTPC6_QM_CP_LDMA_DST_BASE_LO_OFFSET                         0xF8814C

#define mmTPC6_QM_CP_LDMA_DST_BASE_HI_OFFSET                         0xF88150

#define mmTPC6_QM_CP_LDMA_COMMIT_OFFSET                              0xF88154

#define mmTPC6_QM_CP_FENCE0_RDATA                                    0xF88158

#define mmTPC6_QM_CP_FENCE1_RDATA                                    0xF8815C

#define mmTPC6_QM_CP_FENCE2_RDATA                                    0xF88160

#define mmTPC6_QM_CP_FENCE3_RDATA                                    0xF88164

#define mmTPC6_QM_CP_FENCE0_CNT                                      0xF88168

#define mmTPC6_QM_CP_FENCE1_CNT                                      0xF8816C

#define mmTPC6_QM_CP_FENCE2_CNT                                      0xF88170

#define mmTPC6_QM_CP_FENCE3_CNT                                      0xF88174

#define mmTPC6_QM_CP_STS                                             0xF88178

#define mmTPC6_QM_CP_CURRENT_INST_LO                                 0xF8817C

#define mmTPC6_QM_CP_CURRENT_INST_HI                                 0xF88180

#define mmTPC6_QM_CP_BARRIER_CFG                                     0xF88184

#define mmTPC6_QM_CP_DBG_0                                           0xF88188

#define mmTPC6_QM_PQ_BUF_ADDR                                        0xF88300

#define mmTPC6_QM_PQ_BUF_RDATA                                       0xF88304

#define mmTPC6_QM_CQ_BUF_ADDR                                        0xF88308

#define mmTPC6_QM_CQ_BUF_RDATA                                       0xF8830C

#endif /* ASIC_REG_TPC6_QM_REGS_H_ */
