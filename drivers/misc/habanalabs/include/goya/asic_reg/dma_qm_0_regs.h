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

#ifndef ASIC_REG_DMA_QM_0_REGS_H_
#define ASIC_REG_DMA_QM_0_REGS_H_

/*
 *****************************************
 *   DMA_QM_0 (Prototype: QMAN)
 *****************************************
 */

#define mmDMA_QM_0_GLBL_CFG0                                         0x400000

#define mmDMA_QM_0_GLBL_CFG1                                         0x400004

#define mmDMA_QM_0_GLBL_PROT                                         0x400008

#define mmDMA_QM_0_GLBL_ERR_CFG                                      0x40000C

#define mmDMA_QM_0_GLBL_ERR_ADDR_LO                                  0x400010

#define mmDMA_QM_0_GLBL_ERR_ADDR_HI                                  0x400014

#define mmDMA_QM_0_GLBL_ERR_WDATA                                    0x400018

#define mmDMA_QM_0_GLBL_SECURE_PROPS                                 0x40001C

#define mmDMA_QM_0_GLBL_NON_SECURE_PROPS                             0x400020

#define mmDMA_QM_0_GLBL_STS0                                         0x400024

#define mmDMA_QM_0_GLBL_STS1                                         0x400028

#define mmDMA_QM_0_PQ_BASE_LO                                        0x400060

#define mmDMA_QM_0_PQ_BASE_HI                                        0x400064

#define mmDMA_QM_0_PQ_SIZE                                           0x400068

#define mmDMA_QM_0_PQ_PI                                             0x40006C

#define mmDMA_QM_0_PQ_CI                                             0x400070

#define mmDMA_QM_0_PQ_CFG0                                           0x400074

#define mmDMA_QM_0_PQ_CFG1                                           0x400078

#define mmDMA_QM_0_PQ_ARUSER                                         0x40007C

#define mmDMA_QM_0_PQ_PUSH0                                          0x400080

#define mmDMA_QM_0_PQ_PUSH1                                          0x400084

#define mmDMA_QM_0_PQ_PUSH2                                          0x400088

#define mmDMA_QM_0_PQ_PUSH3                                          0x40008C

#define mmDMA_QM_0_PQ_STS0                                           0x400090

#define mmDMA_QM_0_PQ_STS1                                           0x400094

#define mmDMA_QM_0_PQ_RD_RATE_LIM_EN                                 0x4000A0

#define mmDMA_QM_0_PQ_RD_RATE_LIM_RST_TOKEN                          0x4000A4

#define mmDMA_QM_0_PQ_RD_RATE_LIM_SAT                                0x4000A8

#define mmDMA_QM_0_PQ_RD_RATE_LIM_TOUT                               0x4000AC

#define mmDMA_QM_0_CQ_CFG0                                           0x4000B0

#define mmDMA_QM_0_CQ_CFG1                                           0x4000B4

#define mmDMA_QM_0_CQ_ARUSER                                         0x4000B8

#define mmDMA_QM_0_CQ_PTR_LO                                         0x4000C0

#define mmDMA_QM_0_CQ_PTR_HI                                         0x4000C4

#define mmDMA_QM_0_CQ_TSIZE                                          0x4000C8

#define mmDMA_QM_0_CQ_CTL                                            0x4000CC

#define mmDMA_QM_0_CQ_PTR_LO_STS                                     0x4000D4

#define mmDMA_QM_0_CQ_PTR_HI_STS                                     0x4000D8

#define mmDMA_QM_0_CQ_TSIZE_STS                                      0x4000DC

#define mmDMA_QM_0_CQ_CTL_STS                                        0x4000E0

#define mmDMA_QM_0_CQ_STS0                                           0x4000E4

#define mmDMA_QM_0_CQ_STS1                                           0x4000E8

#define mmDMA_QM_0_CQ_RD_RATE_LIM_EN                                 0x4000F0

#define mmDMA_QM_0_CQ_RD_RATE_LIM_RST_TOKEN                          0x4000F4

#define mmDMA_QM_0_CQ_RD_RATE_LIM_SAT                                0x4000F8

#define mmDMA_QM_0_CQ_RD_RATE_LIM_TOUT                               0x4000FC

#define mmDMA_QM_0_CQ_IFIFO_CNT                                      0x400108

#define mmDMA_QM_0_CP_MSG_BASE0_ADDR_LO                              0x400120

#define mmDMA_QM_0_CP_MSG_BASE0_ADDR_HI                              0x400124

#define mmDMA_QM_0_CP_MSG_BASE1_ADDR_LO                              0x400128

#define mmDMA_QM_0_CP_MSG_BASE1_ADDR_HI                              0x40012C

#define mmDMA_QM_0_CP_MSG_BASE2_ADDR_LO                              0x400130

#define mmDMA_QM_0_CP_MSG_BASE2_ADDR_HI                              0x400134

#define mmDMA_QM_0_CP_MSG_BASE3_ADDR_LO                              0x400138

#define mmDMA_QM_0_CP_MSG_BASE3_ADDR_HI                              0x40013C

#define mmDMA_QM_0_CP_LDMA_TSIZE_OFFSET                              0x400140

#define mmDMA_QM_0_CP_LDMA_SRC_BASE_LO_OFFSET                        0x400144

#define mmDMA_QM_0_CP_LDMA_SRC_BASE_HI_OFFSET                        0x400148

#define mmDMA_QM_0_CP_LDMA_DST_BASE_LO_OFFSET                        0x40014C

#define mmDMA_QM_0_CP_LDMA_DST_BASE_HI_OFFSET                        0x400150

#define mmDMA_QM_0_CP_LDMA_COMMIT_OFFSET                             0x400154

#define mmDMA_QM_0_CP_FENCE0_RDATA                                   0x400158

#define mmDMA_QM_0_CP_FENCE1_RDATA                                   0x40015C

#define mmDMA_QM_0_CP_FENCE2_RDATA                                   0x400160

#define mmDMA_QM_0_CP_FENCE3_RDATA                                   0x400164

#define mmDMA_QM_0_CP_FENCE0_CNT                                     0x400168

#define mmDMA_QM_0_CP_FENCE1_CNT                                     0x40016C

#define mmDMA_QM_0_CP_FENCE2_CNT                                     0x400170

#define mmDMA_QM_0_CP_FENCE3_CNT                                     0x400174

#define mmDMA_QM_0_CP_STS                                            0x400178

#define mmDMA_QM_0_CP_CURRENT_INST_LO                                0x40017C

#define mmDMA_QM_0_CP_CURRENT_INST_HI                                0x400180

#define mmDMA_QM_0_CP_BARRIER_CFG                                    0x400184

#define mmDMA_QM_0_CP_DBG_0                                          0x400188

#define mmDMA_QM_0_PQ_BUF_ADDR                                       0x400300

#define mmDMA_QM_0_PQ_BUF_RDATA                                      0x400304

#define mmDMA_QM_0_CQ_BUF_ADDR                                       0x400308

#define mmDMA_QM_0_CQ_BUF_RDATA                                      0x40030C

#endif /* ASIC_REG_DMA_QM_0_REGS_H_ */

