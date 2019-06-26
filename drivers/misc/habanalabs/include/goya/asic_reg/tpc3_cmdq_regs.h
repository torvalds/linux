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

#ifndef ASIC_REG_TPC3_CMDQ_REGS_H_
#define ASIC_REG_TPC3_CMDQ_REGS_H_

/*
 *****************************************
 *   TPC3_CMDQ (Prototype: CMDQ)
 *****************************************
 */

#define mmTPC3_CMDQ_GLBL_CFG0                                        0xEC9000

#define mmTPC3_CMDQ_GLBL_CFG1                                        0xEC9004

#define mmTPC3_CMDQ_GLBL_PROT                                        0xEC9008

#define mmTPC3_CMDQ_GLBL_ERR_CFG                                     0xEC900C

#define mmTPC3_CMDQ_GLBL_ERR_ADDR_LO                                 0xEC9010

#define mmTPC3_CMDQ_GLBL_ERR_ADDR_HI                                 0xEC9014

#define mmTPC3_CMDQ_GLBL_ERR_WDATA                                   0xEC9018

#define mmTPC3_CMDQ_GLBL_SECURE_PROPS                                0xEC901C

#define mmTPC3_CMDQ_GLBL_NON_SECURE_PROPS                            0xEC9020

#define mmTPC3_CMDQ_GLBL_STS0                                        0xEC9024

#define mmTPC3_CMDQ_GLBL_STS1                                        0xEC9028

#define mmTPC3_CMDQ_CQ_CFG0                                          0xEC90B0

#define mmTPC3_CMDQ_CQ_CFG1                                          0xEC90B4

#define mmTPC3_CMDQ_CQ_ARUSER                                        0xEC90B8

#define mmTPC3_CMDQ_CQ_PTR_LO                                        0xEC90C0

#define mmTPC3_CMDQ_CQ_PTR_HI                                        0xEC90C4

#define mmTPC3_CMDQ_CQ_TSIZE                                         0xEC90C8

#define mmTPC3_CMDQ_CQ_CTL                                           0xEC90CC

#define mmTPC3_CMDQ_CQ_PTR_LO_STS                                    0xEC90D4

#define mmTPC3_CMDQ_CQ_PTR_HI_STS                                    0xEC90D8

#define mmTPC3_CMDQ_CQ_TSIZE_STS                                     0xEC90DC

#define mmTPC3_CMDQ_CQ_CTL_STS                                       0xEC90E0

#define mmTPC3_CMDQ_CQ_STS0                                          0xEC90E4

#define mmTPC3_CMDQ_CQ_STS1                                          0xEC90E8

#define mmTPC3_CMDQ_CQ_RD_RATE_LIM_EN                                0xEC90F0

#define mmTPC3_CMDQ_CQ_RD_RATE_LIM_RST_TOKEN                         0xEC90F4

#define mmTPC3_CMDQ_CQ_RD_RATE_LIM_SAT                               0xEC90F8

#define mmTPC3_CMDQ_CQ_RD_RATE_LIM_TOUT                              0xEC90FC

#define mmTPC3_CMDQ_CQ_IFIFO_CNT                                     0xEC9108

#define mmTPC3_CMDQ_CP_MSG_BASE0_ADDR_LO                             0xEC9120

#define mmTPC3_CMDQ_CP_MSG_BASE0_ADDR_HI                             0xEC9124

#define mmTPC3_CMDQ_CP_MSG_BASE1_ADDR_LO                             0xEC9128

#define mmTPC3_CMDQ_CP_MSG_BASE1_ADDR_HI                             0xEC912C

#define mmTPC3_CMDQ_CP_MSG_BASE2_ADDR_LO                             0xEC9130

#define mmTPC3_CMDQ_CP_MSG_BASE2_ADDR_HI                             0xEC9134

#define mmTPC3_CMDQ_CP_MSG_BASE3_ADDR_LO                             0xEC9138

#define mmTPC3_CMDQ_CP_MSG_BASE3_ADDR_HI                             0xEC913C

#define mmTPC3_CMDQ_CP_LDMA_TSIZE_OFFSET                             0xEC9140

#define mmTPC3_CMDQ_CP_LDMA_SRC_BASE_LO_OFFSET                       0xEC9144

#define mmTPC3_CMDQ_CP_LDMA_SRC_BASE_HI_OFFSET                       0xEC9148

#define mmTPC3_CMDQ_CP_LDMA_DST_BASE_LO_OFFSET                       0xEC914C

#define mmTPC3_CMDQ_CP_LDMA_DST_BASE_HI_OFFSET                       0xEC9150

#define mmTPC3_CMDQ_CP_LDMA_COMMIT_OFFSET                            0xEC9154

#define mmTPC3_CMDQ_CP_FENCE0_RDATA                                  0xEC9158

#define mmTPC3_CMDQ_CP_FENCE1_RDATA                                  0xEC915C

#define mmTPC3_CMDQ_CP_FENCE2_RDATA                                  0xEC9160

#define mmTPC3_CMDQ_CP_FENCE3_RDATA                                  0xEC9164

#define mmTPC3_CMDQ_CP_FENCE0_CNT                                    0xEC9168

#define mmTPC3_CMDQ_CP_FENCE1_CNT                                    0xEC916C

#define mmTPC3_CMDQ_CP_FENCE2_CNT                                    0xEC9170

#define mmTPC3_CMDQ_CP_FENCE3_CNT                                    0xEC9174

#define mmTPC3_CMDQ_CP_STS                                           0xEC9178

#define mmTPC3_CMDQ_CP_CURRENT_INST_LO                               0xEC917C

#define mmTPC3_CMDQ_CP_CURRENT_INST_HI                               0xEC9180

#define mmTPC3_CMDQ_CP_BARRIER_CFG                                   0xEC9184

#define mmTPC3_CMDQ_CP_DBG_0                                         0xEC9188

#define mmTPC3_CMDQ_CQ_BUF_ADDR                                      0xEC9308

#define mmTPC3_CMDQ_CQ_BUF_RDATA                                     0xEC930C

#endif /* ASIC_REG_TPC3_CMDQ_REGS_H_ */

