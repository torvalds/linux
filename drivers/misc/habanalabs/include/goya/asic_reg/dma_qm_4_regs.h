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

#ifndef ASIC_REG_DMA_QM_4_REGS_H_
#define ASIC_REG_DMA_QM_4_REGS_H_

/*
 *****************************************
 *   DMA_QM_4 (Prototype: QMAN)
 *****************************************
 */

#define mmDMA_QM_4_GLBL_CFG0                                         0x420000

#define mmDMA_QM_4_GLBL_CFG1                                         0x420004

#define mmDMA_QM_4_GLBL_PROT                                         0x420008

#define mmDMA_QM_4_GLBL_ERR_CFG                                      0x42000C

#define mmDMA_QM_4_GLBL_ERR_ADDR_LO                                  0x420010

#define mmDMA_QM_4_GLBL_ERR_ADDR_HI                                  0x420014

#define mmDMA_QM_4_GLBL_ERR_WDATA                                    0x420018

#define mmDMA_QM_4_GLBL_SECURE_PROPS                                 0x42001C

#define mmDMA_QM_4_GLBL_NON_SECURE_PROPS                             0x420020

#define mmDMA_QM_4_GLBL_STS0                                         0x420024

#define mmDMA_QM_4_GLBL_STS1                                         0x420028

#define mmDMA_QM_4_PQ_BASE_LO                                        0x420060

#define mmDMA_QM_4_PQ_BASE_HI                                        0x420064

#define mmDMA_QM_4_PQ_SIZE                                           0x420068

#define mmDMA_QM_4_PQ_PI                                             0x42006C

#define mmDMA_QM_4_PQ_CI                                             0x420070

#define mmDMA_QM_4_PQ_CFG0                                           0x420074

#define mmDMA_QM_4_PQ_CFG1                                           0x420078

#define mmDMA_QM_4_PQ_ARUSER                                         0x42007C

#define mmDMA_QM_4_PQ_PUSH0                                          0x420080

#define mmDMA_QM_4_PQ_PUSH1                                          0x420084

#define mmDMA_QM_4_PQ_PUSH2                                          0x420088

#define mmDMA_QM_4_PQ_PUSH3                                          0x42008C

#define mmDMA_QM_4_PQ_STS0                                           0x420090

#define mmDMA_QM_4_PQ_STS1                                           0x420094

#define mmDMA_QM_4_PQ_RD_RATE_LIM_EN                                 0x4200A0

#define mmDMA_QM_4_PQ_RD_RATE_LIM_RST_TOKEN                          0x4200A4

#define mmDMA_QM_4_PQ_RD_RATE_LIM_SAT                                0x4200A8

#define mmDMA_QM_4_PQ_RD_RATE_LIM_TOUT                               0x4200AC

#define mmDMA_QM_4_CQ_CFG0                                           0x4200B0

#define mmDMA_QM_4_CQ_CFG1                                           0x4200B4

#define mmDMA_QM_4_CQ_ARUSER                                         0x4200B8

#define mmDMA_QM_4_CQ_PTR_LO                                         0x4200C0

#define mmDMA_QM_4_CQ_PTR_HI                                         0x4200C4

#define mmDMA_QM_4_CQ_TSIZE                                          0x4200C8

#define mmDMA_QM_4_CQ_CTL                                            0x4200CC

#define mmDMA_QM_4_CQ_PTR_LO_STS                                     0x4200D4

#define mmDMA_QM_4_CQ_PTR_HI_STS                                     0x4200D8

#define mmDMA_QM_4_CQ_TSIZE_STS                                      0x4200DC

#define mmDMA_QM_4_CQ_CTL_STS                                        0x4200E0

#define mmDMA_QM_4_CQ_STS0                                           0x4200E4

#define mmDMA_QM_4_CQ_STS1                                           0x4200E8

#define mmDMA_QM_4_CQ_RD_RATE_LIM_EN                                 0x4200F0

#define mmDMA_QM_4_CQ_RD_RATE_LIM_RST_TOKEN                          0x4200F4

#define mmDMA_QM_4_CQ_RD_RATE_LIM_SAT                                0x4200F8

#define mmDMA_QM_4_CQ_RD_RATE_LIM_TOUT                               0x4200FC

#define mmDMA_QM_4_CQ_IFIFO_CNT                                      0x420108

#define mmDMA_QM_4_CP_MSG_BASE0_ADDR_LO                              0x420120

#define mmDMA_QM_4_CP_MSG_BASE0_ADDR_HI                              0x420124

#define mmDMA_QM_4_CP_MSG_BASE1_ADDR_LO                              0x420128

#define mmDMA_QM_4_CP_MSG_BASE1_ADDR_HI                              0x42012C

#define mmDMA_QM_4_CP_MSG_BASE2_ADDR_LO                              0x420130

#define mmDMA_QM_4_CP_MSG_BASE2_ADDR_HI                              0x420134

#define mmDMA_QM_4_CP_MSG_BASE3_ADDR_LO                              0x420138

#define mmDMA_QM_4_CP_MSG_BASE3_ADDR_HI                              0x42013C

#define mmDMA_QM_4_CP_LDMA_TSIZE_OFFSET                              0x420140

#define mmDMA_QM_4_CP_LDMA_SRC_BASE_LO_OFFSET                        0x420144

#define mmDMA_QM_4_CP_LDMA_SRC_BASE_HI_OFFSET                        0x420148

#define mmDMA_QM_4_CP_LDMA_DST_BASE_LO_OFFSET                        0x42014C

#define mmDMA_QM_4_CP_LDMA_DST_BASE_HI_OFFSET                        0x420150

#define mmDMA_QM_4_CP_LDMA_COMMIT_OFFSET                             0x420154

#define mmDMA_QM_4_CP_FENCE0_RDATA                                   0x420158

#define mmDMA_QM_4_CP_FENCE1_RDATA                                   0x42015C

#define mmDMA_QM_4_CP_FENCE2_RDATA                                   0x420160

#define mmDMA_QM_4_CP_FENCE3_RDATA                                   0x420164

#define mmDMA_QM_4_CP_FENCE0_CNT                                     0x420168

#define mmDMA_QM_4_CP_FENCE1_CNT                                     0x42016C

#define mmDMA_QM_4_CP_FENCE2_CNT                                     0x420170

#define mmDMA_QM_4_CP_FENCE3_CNT                                     0x420174

#define mmDMA_QM_4_CP_STS                                            0x420178

#define mmDMA_QM_4_CP_CURRENT_INST_LO                                0x42017C

#define mmDMA_QM_4_CP_CURRENT_INST_HI                                0x420180

#define mmDMA_QM_4_CP_BARRIER_CFG                                    0x420184

#define mmDMA_QM_4_CP_DBG_0                                          0x420188

#define mmDMA_QM_4_PQ_BUF_ADDR                                       0x420300

#define mmDMA_QM_4_PQ_BUF_RDATA                                      0x420304

#define mmDMA_QM_4_CQ_BUF_ADDR                                       0x420308

#define mmDMA_QM_4_CQ_BUF_RDATA                                      0x42030C

#endif /* ASIC_REG_DMA_QM_4_REGS_H_ */

