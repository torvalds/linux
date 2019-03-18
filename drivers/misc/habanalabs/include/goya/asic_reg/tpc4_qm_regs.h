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

#ifndef ASIC_REG_TPC4_QM_REGS_H_
#define ASIC_REG_TPC4_QM_REGS_H_

/*
 *****************************************
 *   TPC4_QM (Prototype: QMAN)
 *****************************************
 */

#define mmTPC4_QM_GLBL_CFG0                                          0xF08000

#define mmTPC4_QM_GLBL_CFG1                                          0xF08004

#define mmTPC4_QM_GLBL_PROT                                          0xF08008

#define mmTPC4_QM_GLBL_ERR_CFG                                       0xF0800C

#define mmTPC4_QM_GLBL_ERR_ADDR_LO                                   0xF08010

#define mmTPC4_QM_GLBL_ERR_ADDR_HI                                   0xF08014

#define mmTPC4_QM_GLBL_ERR_WDATA                                     0xF08018

#define mmTPC4_QM_GLBL_SECURE_PROPS                                  0xF0801C

#define mmTPC4_QM_GLBL_NON_SECURE_PROPS                              0xF08020

#define mmTPC4_QM_GLBL_STS0                                          0xF08024

#define mmTPC4_QM_GLBL_STS1                                          0xF08028

#define mmTPC4_QM_PQ_BASE_LO                                         0xF08060

#define mmTPC4_QM_PQ_BASE_HI                                         0xF08064

#define mmTPC4_QM_PQ_SIZE                                            0xF08068

#define mmTPC4_QM_PQ_PI                                              0xF0806C

#define mmTPC4_QM_PQ_CI                                              0xF08070

#define mmTPC4_QM_PQ_CFG0                                            0xF08074

#define mmTPC4_QM_PQ_CFG1                                            0xF08078

#define mmTPC4_QM_PQ_ARUSER                                          0xF0807C

#define mmTPC4_QM_PQ_PUSH0                                           0xF08080

#define mmTPC4_QM_PQ_PUSH1                                           0xF08084

#define mmTPC4_QM_PQ_PUSH2                                           0xF08088

#define mmTPC4_QM_PQ_PUSH3                                           0xF0808C

#define mmTPC4_QM_PQ_STS0                                            0xF08090

#define mmTPC4_QM_PQ_STS1                                            0xF08094

#define mmTPC4_QM_PQ_RD_RATE_LIM_EN                                  0xF080A0

#define mmTPC4_QM_PQ_RD_RATE_LIM_RST_TOKEN                           0xF080A4

#define mmTPC4_QM_PQ_RD_RATE_LIM_SAT                                 0xF080A8

#define mmTPC4_QM_PQ_RD_RATE_LIM_TOUT                                0xF080AC

#define mmTPC4_QM_CQ_CFG0                                            0xF080B0

#define mmTPC4_QM_CQ_CFG1                                            0xF080B4

#define mmTPC4_QM_CQ_ARUSER                                          0xF080B8

#define mmTPC4_QM_CQ_PTR_LO                                          0xF080C0

#define mmTPC4_QM_CQ_PTR_HI                                          0xF080C4

#define mmTPC4_QM_CQ_TSIZE                                           0xF080C8

#define mmTPC4_QM_CQ_CTL                                             0xF080CC

#define mmTPC4_QM_CQ_PTR_LO_STS                                      0xF080D4

#define mmTPC4_QM_CQ_PTR_HI_STS                                      0xF080D8

#define mmTPC4_QM_CQ_TSIZE_STS                                       0xF080DC

#define mmTPC4_QM_CQ_CTL_STS                                         0xF080E0

#define mmTPC4_QM_CQ_STS0                                            0xF080E4

#define mmTPC4_QM_CQ_STS1                                            0xF080E8

#define mmTPC4_QM_CQ_RD_RATE_LIM_EN                                  0xF080F0

#define mmTPC4_QM_CQ_RD_RATE_LIM_RST_TOKEN                           0xF080F4

#define mmTPC4_QM_CQ_RD_RATE_LIM_SAT                                 0xF080F8

#define mmTPC4_QM_CQ_RD_RATE_LIM_TOUT                                0xF080FC

#define mmTPC4_QM_CQ_IFIFO_CNT                                       0xF08108

#define mmTPC4_QM_CP_MSG_BASE0_ADDR_LO                               0xF08120

#define mmTPC4_QM_CP_MSG_BASE0_ADDR_HI                               0xF08124

#define mmTPC4_QM_CP_MSG_BASE1_ADDR_LO                               0xF08128

#define mmTPC4_QM_CP_MSG_BASE1_ADDR_HI                               0xF0812C

#define mmTPC4_QM_CP_MSG_BASE2_ADDR_LO                               0xF08130

#define mmTPC4_QM_CP_MSG_BASE2_ADDR_HI                               0xF08134

#define mmTPC4_QM_CP_MSG_BASE3_ADDR_LO                               0xF08138

#define mmTPC4_QM_CP_MSG_BASE3_ADDR_HI                               0xF0813C

#define mmTPC4_QM_CP_LDMA_TSIZE_OFFSET                               0xF08140

#define mmTPC4_QM_CP_LDMA_SRC_BASE_LO_OFFSET                         0xF08144

#define mmTPC4_QM_CP_LDMA_SRC_BASE_HI_OFFSET                         0xF08148

#define mmTPC4_QM_CP_LDMA_DST_BASE_LO_OFFSET                         0xF0814C

#define mmTPC4_QM_CP_LDMA_DST_BASE_HI_OFFSET                         0xF08150

#define mmTPC4_QM_CP_LDMA_COMMIT_OFFSET                              0xF08154

#define mmTPC4_QM_CP_FENCE0_RDATA                                    0xF08158

#define mmTPC4_QM_CP_FENCE1_RDATA                                    0xF0815C

#define mmTPC4_QM_CP_FENCE2_RDATA                                    0xF08160

#define mmTPC4_QM_CP_FENCE3_RDATA                                    0xF08164

#define mmTPC4_QM_CP_FENCE0_CNT                                      0xF08168

#define mmTPC4_QM_CP_FENCE1_CNT                                      0xF0816C

#define mmTPC4_QM_CP_FENCE2_CNT                                      0xF08170

#define mmTPC4_QM_CP_FENCE3_CNT                                      0xF08174

#define mmTPC4_QM_CP_STS                                             0xF08178

#define mmTPC4_QM_CP_CURRENT_INST_LO                                 0xF0817C

#define mmTPC4_QM_CP_CURRENT_INST_HI                                 0xF08180

#define mmTPC4_QM_CP_BARRIER_CFG                                     0xF08184

#define mmTPC4_QM_CP_DBG_0                                           0xF08188

#define mmTPC4_QM_PQ_BUF_ADDR                                        0xF08300

#define mmTPC4_QM_PQ_BUF_RDATA                                       0xF08304

#define mmTPC4_QM_CQ_BUF_ADDR                                        0xF08308

#define mmTPC4_QM_CQ_BUF_RDATA                                       0xF0830C

#endif /* ASIC_REG_TPC4_QM_REGS_H_ */

