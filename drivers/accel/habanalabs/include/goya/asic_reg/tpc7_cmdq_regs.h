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

#ifndef ASIC_REG_TPC7_CMDQ_REGS_H_
#define ASIC_REG_TPC7_CMDQ_REGS_H_

/*
 *****************************************
 *   TPC7_CMDQ (Prototype: CMDQ)
 *****************************************
 */

#define mmTPC7_CMDQ_GLBL_CFG0                                        0xFC9000

#define mmTPC7_CMDQ_GLBL_CFG1                                        0xFC9004

#define mmTPC7_CMDQ_GLBL_PROT                                        0xFC9008

#define mmTPC7_CMDQ_GLBL_ERR_CFG                                     0xFC900C

#define mmTPC7_CMDQ_GLBL_ERR_ADDR_LO                                 0xFC9010

#define mmTPC7_CMDQ_GLBL_ERR_ADDR_HI                                 0xFC9014

#define mmTPC7_CMDQ_GLBL_ERR_WDATA                                   0xFC9018

#define mmTPC7_CMDQ_GLBL_SECURE_PROPS                                0xFC901C

#define mmTPC7_CMDQ_GLBL_NON_SECURE_PROPS                            0xFC9020

#define mmTPC7_CMDQ_GLBL_STS0                                        0xFC9024

#define mmTPC7_CMDQ_GLBL_STS1                                        0xFC9028

#define mmTPC7_CMDQ_CQ_CFG0                                          0xFC90B0

#define mmTPC7_CMDQ_CQ_CFG1                                          0xFC90B4

#define mmTPC7_CMDQ_CQ_ARUSER                                        0xFC90B8

#define mmTPC7_CMDQ_CQ_PTR_LO                                        0xFC90C0

#define mmTPC7_CMDQ_CQ_PTR_HI                                        0xFC90C4

#define mmTPC7_CMDQ_CQ_TSIZE                                         0xFC90C8

#define mmTPC7_CMDQ_CQ_CTL                                           0xFC90CC

#define mmTPC7_CMDQ_CQ_PTR_LO_STS                                    0xFC90D4

#define mmTPC7_CMDQ_CQ_PTR_HI_STS                                    0xFC90D8

#define mmTPC7_CMDQ_CQ_TSIZE_STS                                     0xFC90DC

#define mmTPC7_CMDQ_CQ_CTL_STS                                       0xFC90E0

#define mmTPC7_CMDQ_CQ_STS0                                          0xFC90E4

#define mmTPC7_CMDQ_CQ_STS1                                          0xFC90E8

#define mmTPC7_CMDQ_CQ_RD_RATE_LIM_EN                                0xFC90F0

#define mmTPC7_CMDQ_CQ_RD_RATE_LIM_RST_TOKEN                         0xFC90F4

#define mmTPC7_CMDQ_CQ_RD_RATE_LIM_SAT                               0xFC90F8

#define mmTPC7_CMDQ_CQ_RD_RATE_LIM_TOUT                              0xFC90FC

#define mmTPC7_CMDQ_CQ_IFIFO_CNT                                     0xFC9108

#define mmTPC7_CMDQ_CP_MSG_BASE0_ADDR_LO                             0xFC9120

#define mmTPC7_CMDQ_CP_MSG_BASE0_ADDR_HI                             0xFC9124

#define mmTPC7_CMDQ_CP_MSG_BASE1_ADDR_LO                             0xFC9128

#define mmTPC7_CMDQ_CP_MSG_BASE1_ADDR_HI                             0xFC912C

#define mmTPC7_CMDQ_CP_MSG_BASE2_ADDR_LO                             0xFC9130

#define mmTPC7_CMDQ_CP_MSG_BASE2_ADDR_HI                             0xFC9134

#define mmTPC7_CMDQ_CP_MSG_BASE3_ADDR_LO                             0xFC9138

#define mmTPC7_CMDQ_CP_MSG_BASE3_ADDR_HI                             0xFC913C

#define mmTPC7_CMDQ_CP_LDMA_TSIZE_OFFSET                             0xFC9140

#define mmTPC7_CMDQ_CP_LDMA_SRC_BASE_LO_OFFSET                       0xFC9144

#define mmTPC7_CMDQ_CP_LDMA_SRC_BASE_HI_OFFSET                       0xFC9148

#define mmTPC7_CMDQ_CP_LDMA_DST_BASE_LO_OFFSET                       0xFC914C

#define mmTPC7_CMDQ_CP_LDMA_DST_BASE_HI_OFFSET                       0xFC9150

#define mmTPC7_CMDQ_CP_LDMA_COMMIT_OFFSET                            0xFC9154

#define mmTPC7_CMDQ_CP_FENCE0_RDATA                                  0xFC9158

#define mmTPC7_CMDQ_CP_FENCE1_RDATA                                  0xFC915C

#define mmTPC7_CMDQ_CP_FENCE2_RDATA                                  0xFC9160

#define mmTPC7_CMDQ_CP_FENCE3_RDATA                                  0xFC9164

#define mmTPC7_CMDQ_CP_FENCE0_CNT                                    0xFC9168

#define mmTPC7_CMDQ_CP_FENCE1_CNT                                    0xFC916C

#define mmTPC7_CMDQ_CP_FENCE2_CNT                                    0xFC9170

#define mmTPC7_CMDQ_CP_FENCE3_CNT                                    0xFC9174

#define mmTPC7_CMDQ_CP_STS                                           0xFC9178

#define mmTPC7_CMDQ_CP_CURRENT_INST_LO                               0xFC917C

#define mmTPC7_CMDQ_CP_CURRENT_INST_HI                               0xFC9180

#define mmTPC7_CMDQ_CP_BARRIER_CFG                                   0xFC9184

#define mmTPC7_CMDQ_CP_DBG_0                                         0xFC9188

#define mmTPC7_CMDQ_CQ_BUF_ADDR                                      0xFC9308

#define mmTPC7_CMDQ_CQ_BUF_RDATA                                     0xFC930C

#endif /* ASIC_REG_TPC7_CMDQ_REGS_H_ */
