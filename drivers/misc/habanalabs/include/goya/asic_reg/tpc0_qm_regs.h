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

#ifndef ASIC_REG_TPC0_QM_REGS_H_
#define ASIC_REG_TPC0_QM_REGS_H_

/*
 *****************************************
 *   TPC0_QM (Prototype: QMAN)
 *****************************************
 */

#define mmTPC0_QM_GLBL_CFG0                                          0xE08000

#define mmTPC0_QM_GLBL_CFG1                                          0xE08004

#define mmTPC0_QM_GLBL_PROT                                          0xE08008

#define mmTPC0_QM_GLBL_ERR_CFG                                       0xE0800C

#define mmTPC0_QM_GLBL_ERR_ADDR_LO                                   0xE08010

#define mmTPC0_QM_GLBL_ERR_ADDR_HI                                   0xE08014

#define mmTPC0_QM_GLBL_ERR_WDATA                                     0xE08018

#define mmTPC0_QM_GLBL_SECURE_PROPS                                  0xE0801C

#define mmTPC0_QM_GLBL_NON_SECURE_PROPS                              0xE08020

#define mmTPC0_QM_GLBL_STS0                                          0xE08024

#define mmTPC0_QM_GLBL_STS1                                          0xE08028

#define mmTPC0_QM_PQ_BASE_LO                                         0xE08060

#define mmTPC0_QM_PQ_BASE_HI                                         0xE08064

#define mmTPC0_QM_PQ_SIZE                                            0xE08068

#define mmTPC0_QM_PQ_PI                                              0xE0806C

#define mmTPC0_QM_PQ_CI                                              0xE08070

#define mmTPC0_QM_PQ_CFG0                                            0xE08074

#define mmTPC0_QM_PQ_CFG1                                            0xE08078

#define mmTPC0_QM_PQ_ARUSER                                          0xE0807C

#define mmTPC0_QM_PQ_PUSH0                                           0xE08080

#define mmTPC0_QM_PQ_PUSH1                                           0xE08084

#define mmTPC0_QM_PQ_PUSH2                                           0xE08088

#define mmTPC0_QM_PQ_PUSH3                                           0xE0808C

#define mmTPC0_QM_PQ_STS0                                            0xE08090

#define mmTPC0_QM_PQ_STS1                                            0xE08094

#define mmTPC0_QM_PQ_RD_RATE_LIM_EN                                  0xE080A0

#define mmTPC0_QM_PQ_RD_RATE_LIM_RST_TOKEN                           0xE080A4

#define mmTPC0_QM_PQ_RD_RATE_LIM_SAT                                 0xE080A8

#define mmTPC0_QM_PQ_RD_RATE_LIM_TOUT                                0xE080AC

#define mmTPC0_QM_CQ_CFG0                                            0xE080B0

#define mmTPC0_QM_CQ_CFG1                                            0xE080B4

#define mmTPC0_QM_CQ_ARUSER                                          0xE080B8

#define mmTPC0_QM_CQ_PTR_LO                                          0xE080C0

#define mmTPC0_QM_CQ_PTR_HI                                          0xE080C4

#define mmTPC0_QM_CQ_TSIZE                                           0xE080C8

#define mmTPC0_QM_CQ_CTL                                             0xE080CC

#define mmTPC0_QM_CQ_PTR_LO_STS                                      0xE080D4

#define mmTPC0_QM_CQ_PTR_HI_STS                                      0xE080D8

#define mmTPC0_QM_CQ_TSIZE_STS                                       0xE080DC

#define mmTPC0_QM_CQ_CTL_STS                                         0xE080E0

#define mmTPC0_QM_CQ_STS0                                            0xE080E4

#define mmTPC0_QM_CQ_STS1                                            0xE080E8

#define mmTPC0_QM_CQ_RD_RATE_LIM_EN                                  0xE080F0

#define mmTPC0_QM_CQ_RD_RATE_LIM_RST_TOKEN                           0xE080F4

#define mmTPC0_QM_CQ_RD_RATE_LIM_SAT                                 0xE080F8

#define mmTPC0_QM_CQ_RD_RATE_LIM_TOUT                                0xE080FC

#define mmTPC0_QM_CQ_IFIFO_CNT                                       0xE08108

#define mmTPC0_QM_CP_MSG_BASE0_ADDR_LO                               0xE08120

#define mmTPC0_QM_CP_MSG_BASE0_ADDR_HI                               0xE08124

#define mmTPC0_QM_CP_MSG_BASE1_ADDR_LO                               0xE08128

#define mmTPC0_QM_CP_MSG_BASE1_ADDR_HI                               0xE0812C

#define mmTPC0_QM_CP_MSG_BASE2_ADDR_LO                               0xE08130

#define mmTPC0_QM_CP_MSG_BASE2_ADDR_HI                               0xE08134

#define mmTPC0_QM_CP_MSG_BASE3_ADDR_LO                               0xE08138

#define mmTPC0_QM_CP_MSG_BASE3_ADDR_HI                               0xE0813C

#define mmTPC0_QM_CP_LDMA_TSIZE_OFFSET                               0xE08140

#define mmTPC0_QM_CP_LDMA_SRC_BASE_LO_OFFSET                         0xE08144

#define mmTPC0_QM_CP_LDMA_SRC_BASE_HI_OFFSET                         0xE08148

#define mmTPC0_QM_CP_LDMA_DST_BASE_LO_OFFSET                         0xE0814C

#define mmTPC0_QM_CP_LDMA_DST_BASE_HI_OFFSET                         0xE08150

#define mmTPC0_QM_CP_LDMA_COMMIT_OFFSET                              0xE08154

#define mmTPC0_QM_CP_FENCE0_RDATA                                    0xE08158

#define mmTPC0_QM_CP_FENCE1_RDATA                                    0xE0815C

#define mmTPC0_QM_CP_FENCE2_RDATA                                    0xE08160

#define mmTPC0_QM_CP_FENCE3_RDATA                                    0xE08164

#define mmTPC0_QM_CP_FENCE0_CNT                                      0xE08168

#define mmTPC0_QM_CP_FENCE1_CNT                                      0xE0816C

#define mmTPC0_QM_CP_FENCE2_CNT                                      0xE08170

#define mmTPC0_QM_CP_FENCE3_CNT                                      0xE08174

#define mmTPC0_QM_CP_STS                                             0xE08178

#define mmTPC0_QM_CP_CURRENT_INST_LO                                 0xE0817C

#define mmTPC0_QM_CP_CURRENT_INST_HI                                 0xE08180

#define mmTPC0_QM_CP_BARRIER_CFG                                     0xE08184

#define mmTPC0_QM_CP_DBG_0                                           0xE08188

#define mmTPC0_QM_PQ_BUF_ADDR                                        0xE08300

#define mmTPC0_QM_PQ_BUF_RDATA                                       0xE08304

#define mmTPC0_QM_CQ_BUF_ADDR                                        0xE08308

#define mmTPC0_QM_CQ_BUF_RDATA                                       0xE0830C

#endif /* ASIC_REG_TPC0_QM_REGS_H_ */
